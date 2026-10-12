/**
 * @file   StoreClone.cpp
 * @brief  存储快照与复制共享层实现——快照捕获（HEAD 锚定闭包行走＋
 *         勾选树清单＋§8.9 引用完整性过滤）、同卷临时位复制（write-
 *         through 闸门）、三处改写点重写、发布原语与快照视图（PRJ-T18）。
 *
 * 设计依据：见 StoreClone.hpp 文件头（§8.8/§8.9/§8.10/D-17/D-16 与
 *   实现口径①～⑦的登记锚点；本文件按各函数注释逐条落地）。
 *
 * 错误语义（错误二分，AGENTS §3/单元卡 §5.0）：
 *   - 调用方错误 → std::invalid_argument（本层不产——契约校验收敛在
 *     服务入口；视图 read() 的编解码违约除外，见其注释）；
 *   - 数据侧错误 → StoreError(StoreCorrupt)（快照与磁盘失真/解析失败）；
 *   - 环境错误 → StoreError(DiskFull/AccessDenied/WriteRejected)（映射
 *     同表——osToStoreError 单点）。
 *
 * 线程约束：见 StoreClone.hpp 文件头（无共享状态；并发由调用方串行化）。
 */

#include "StoreClone.hpp"

#include "Codec.hpp"

#include <algorithm>
#include <fstream>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace sdurws::ird::project::clone {

namespace {

// =====================================================================
// 文件读取辅助（读轨——不涉写入原语）
// =====================================================================

/// 读取整个文件为字符串；缺失/不可读＝nullopt（不抛——读失败语义由
///   调用方按文件类别分流：不可变文件＝失真、drafts＝跳过）。
std::optional<std::string> tryReadFile(const std::filesystem::path& path)
{
    // 二进制整读（文件尺寸来自 directory_entry/文件系统——与读得字节数
    // 的偏差按实际读得为准，调用方不依赖二者相等）。
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec) || ec) {
        return std::nullopt;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    if (in.bad()) {
        return std::nullopt;
    }
    return buffer.str();
}

/// 目录条目相对路径 → 正斜杠 relPath（Windows 分隔符统一——快照清单
/// 与包内编址的共同形态）。
std::string toRelSlash(const std::filesystem::path& root, const std::filesystem::path& p)
{
    const std::string generic = p.lexically_relative(root).generic_string();
    return generic;
}

/// 根路径＋正斜杠 relPath → 本机分隔符绝对路径（make_preferred——源
/// 项目根是 GetFinalPathNameByHandleW 的 \\?\ 扩展形态，该前缀下
/// Win32 **不**做 '/'→'\' 规范化，正斜杠相对段会直接打不开；合并后
/// 统一转本机分隔符是扩展路径下的正确拼装方式）。
std::filesystem::path joinNative(const std::filesystem::path& root,
                                 const std::string& rel)
{
    std::filesystem::path p = root / std::filesystem::path(rel);
    return p.make_preferred();
}

/// 递归收集目录下全部普通文件的 relPath（正斜杠；确定性排序由调用方
///   统一做——本函数只枚举）。目录缺失＝空清单（reports/results/drafts
///   未勾选或尚无内容是常态不是错误）。
void collectFilesRecursive(const std::filesystem::path& root,
                           const std::filesystem::path& dir,
                           std::vector<std::string>& out)
{
    std::error_code ec;
    std::filesystem::directory_iterator it(dir, ec);
    if (ec) {
        return;
    }
    for (const auto& entry : it) {
        std::error_code entryEc;
        if (entry.is_directory(entryEc) && !entryEc) {
            // 递归子目录（目录树深度由项目布局界定——身份 token 目录
            // 两层为常态；无环由文件系统保证）。
            collectFilesRecursive(root, entry.path(), out);
        } else if (entry.is_regular_file(entryEc) && !entryEc) {
            out.push_back(toRelSlash(root, entry.path()));
        }
    }
}

/// 从文本中提取 run- 规范文本 token（"run-" + 32 hex；实现口径③——
///   reports 引用扫描面。存在性保守判定：报告文本中出现即视为引用）。
std::set<std::string> extractRunTokens(const std::string& text)
{
    static constexpr std::size_t kRunIdHexLen = 32;
    static const char kTag[] = "run-";
    static constexpr std::size_t kTagLen = 4;
    const auto isHex = [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    };
    std::set<std::string> tokens;
    std::size_t pos = 0;
    while ((pos = text.find(kTag, pos)) != std::string::npos) {
        // "run-" 后须恰跟 32 个小写 hex（core 规范文本形态——后随字符
        // 超 32 位仍为 hex 时取前 32 位即完整 id，规范文本定长无歧义；
        // 第 33 位若也是 hex 则本文本非规范引用形态，按前 32 位处理仍
        // 保守成立——不会漏判引用）。
        if (pos + kTagLen + kRunIdHexLen <= text.size()) {
            bool allHex = true;
            for (std::size_t i = 0; i < kRunIdHexLen; ++i) {
                if (!isHex(text[pos + kTagLen + i])) {
                    allHex = false;
                    break;
                }
            }
            if (allHex) {
                tokens.emplace(text.substr(pos, kTagLen + kRunIdHexLen));
            }
        }
        pos += kTagLen;
    }
    return tokens;
}

/// 开发诊断出口（可空 sink＝丢弃——§5.0 sink 约定）。
void reportDev(IDiagnosticsSink* sink, const char* channel, const std::string& message)
{
    if (sink != nullptr) {
        sink->reportDev(channel, message);
    }
}

}  // namespace

// =====================================================================
// 快照捕获
// =====================================================================

CaptureResult captureSnapshot(const IProjectQueryPort& query,
                              const std::filesystem::path& projectDir,
                              bool includeResults,
                              bool includeReports,
                              bool includeDrafts,
                              IDiagnosticsSink* sink)
{
    CaptureResult capture;

    // ---- 第 1 步：HEAD 字节一次读入＋解析（快照锚——文件头"背景说明"：
    // 并发提交并存时以锚为界保持捕获内部自洽；HEAD 原子替换保证读到
    // 旧或新完整内容之一）。HEAD 缺失/损坏＝源存储数据侧错误（§4.1
    // "损坏/缺失＝CorruptStoreDetected"），如实上抛。
    const std::optional<std::string> headBytes = tryReadFile(projectDir / "HEAD");
    if (!headBytes.has_value()) {
        throw StoreError(StoreErrorCode::StoreCorrupt,
                         "project/save-as: HEAD 不可读（源存储损坏——§4.1）");
    }
    StoreSnapshot& snapshot = capture.snapshot;
    snapshot.headBytes = *headBytes;
    snapshot.head = codec::parseHeadRecord(*headBytes);

    // 锚定修订视图（其 metadataRef 即锚定元数据的编址——drafts 分支表
    // 来源；锚定修订必在会话闭包内〔打开装载纪律〕，查不到＝装载失真）。
    const std::optional<RevisionView> headView = query.tryRevision(snapshot.head.revisionId);
    if (!headView.has_value()) {
        throw StoreError(StoreErrorCode::StoreCorrupt,
                         "project/save-as: HEAD 修订不可达（会话闭包失真）");
    }
    snapshot.headMetadataRef = ObjectRefPair{headView->metadataRef.objectId,
                                             headView->metadataRef.contentVersion};

    // ---- 第 2 步：闭包双通道行走（实现口径①——磁盘 HEAD 锚定的
    // committedClosure 语义版：通道一 parent 链 ∪ 通道二元数据谱系
    // supersedes→committedBy，D-4）。行走全部经②端口与 Codec 解析，
    // 不触碰存储内部部件（ARC-02 卫生）；端口对悬挂引用以 store-corrupt
    // 拒绝＝数据侧错误如实上抛。
    std::vector<RevisionId> revQueue{snapshot.head.revisionId};
    std::vector<ObjectRefPair> metaQueue;
    std::set<std::string> visitedRevs;    // 修订规范文本访问集（终止保证）
    std::set<std::string> visitedMetas;   // 元数据引用键访问集
    // 视图仓库：先收集值后统一排序（map 迭代序不适合——seq 才是稳定序）。
    std::vector<RevisionView> revViews;

    const auto metaKey = [](const ObjectRefPair& ref) {
        return ref.objectId.toCanonical() + "|" + ref.contentVersion.toCanonical();
    };

    while (!revQueue.empty() || !metaQueue.empty()) {
        // 通道一：修订节点——标记，装配其元数据引用与对象引用，沿
        // parent 上行（DAG 深链安全——访问集去重，合流静默）。
        while (!revQueue.empty()) {
            const RevisionId id = revQueue.back();
            revQueue.pop_back();
            const std::string key = id.toCanonical();
            if (!visitedRevs.insert(key).second) {
                continue;
            }
            const std::optional<RevisionView> view = query.tryRevision(id);
            if (!view.has_value()) {
                // 锚定闭包内修订不可见＝装载/存储失真（§5.2 数据侧）。
                throw StoreError(StoreErrorCode::StoreCorrupt,
                                 "project/save-as: 闭包行走遇不可达修订 " + key);
            }
            revViews.push_back(*view);
            metaQueue.push_back(ObjectRefPair{view->metadataRef.objectId,
                                              view->metadataRef.contentVersion});
            if (view->parent.has_value()) {
                revQueue.push_back(*view->parent);
            }
        }
        // 通道二：元数据节点——标记，读对象字节解析记录（元数据对象
        // 负载＝canonical ProjectMetadataRecord——§4.2 右列），将其
        // committedBy 修订作为修订继续访问，沿 supersedes 继续谱系
        // （§4.5.1 结论③的行走形态——CreateBranch 修订经此可达）。
        while (!metaQueue.empty()) {
            const ObjectRefPair ref = metaQueue.back();
            metaQueue.pop_back();
            if (!visitedMetas.insert(metaKey(ref)).second) {
                continue;
            }
            const std::vector<std::uint8_t> bytes
                = query.object(ref.objectId, ref.contentVersion);
            const std::string text(bytes.begin(), bytes.end());
            const ProjectMetadataRecord record = codec::parseMetadataRecord(text);
            if (ref == snapshot.headMetadataRef) {
                // 锚定元数据版本（drafts 分支表来源——实现口径④的
                // "锚定分支表"）。
                snapshot.headMetadata = record;
            }
            revQueue.push_back(record.committedBy);
            if (record.supersedes.has_value()) {
                metaQueue.push_back(*record.supersedes);
            }
        }
    }
    // 锚定元数据必须命中（HEAD 修订清单的 metadataRef 恒被通道二处理
    // ——§4.4.2 ≥1 约束）；未命中＝实现缺陷，fail-fast 不伪造分支表。
    if (snapshot.headMetadata.branches.empty()) {
        throw std::logic_error("project/save-as: 锚定元数据未捕获（实现缺陷——不可达）");
    }

    // ---- 第 3 步：对象集＝闭包修订 objectRefs 并集 ∪ 元数据谱系键
    // （元数据对象本身——防御性并集：它同时是 committedBy 修订清单的
    // 成员，§4.4.2 约束；并集使两处登记漂移都不致漏复制）。
    std::set<std::string> objectKeys;  // "oid|cvhex" 访问集
    std::vector<std::pair<core::ObjectId, std::string>> objects;  // oid + cv hex 文件名
    const auto addObject = [&](const core::ObjectId& oid, const core::ContentVersion& cv) {
        const std::string cvCanonical = cv.toCanonical();
        // 文件名＝cv 规范文本剥离 "cv-" tag 的 64 hex（§4.1 objects 行
        // "无 tag"——与 ObjectStore/TxEngine 同源同序，本地零第二格式化）。
        const std::string key = oid.toCanonical() + "|" + cvCanonical;
        if (objectKeys.insert(key).second) {
            objects.emplace_back(oid, cvCanonical.substr(3));
        }
    };

    // 修订确定性排序（seq 升序 → id 规范文本升序——RevisionIndex::
    // CommittedClosure 同款稳定序，NFR-COR-02）。
    std::sort(revViews.begin(), revViews.end(),
              [](const RevisionView& a, const RevisionView& b) {
                  if (a.seq != b.seq) {
                      return a.seq < b.seq;
                  }
                  return a.id.toCanonical() < b.id.toCanonical();
              });

    // project.json（静态标识——§4.2"仅另存为/升级时整体重写"的复制面）
    // 与 HEAD（捕获字节——尺寸即捕获尺寸）先行入清单。
    {
        std::error_code ec;
        const auto projectJsonSize = std::filesystem::file_size(projectDir / "project.json", ec);
        if (ec) {
            throw StoreError(StoreErrorCode::StoreCorrupt,
                             "project/save-as: project.json 不可读（源存储损坏——§4.1）");
        }
        snapshot.files.push_back(SnapshotFile{"project.json", projectJsonSize});
        snapshot.files.push_back(SnapshotFile{"HEAD", snapshot.headBytes.size()});
    }

    std::set<std::string> selectedRunIds;
    for (const RevisionView& view : revViews) {
        const std::string revCanonical = view.id.toCanonical();
        // 修订两件套（§4.1 revisions 行：manifest.json＋command.json；
        // command.json 以磁盘事实为准——存在才复制，防格式演进引入的
        // 可选形态漏判）。
        snapshot.files.push_back(
                            SnapshotFile{"revisions/" + revCanonical + "/manifest.json", 0});
        if (tryReadFile(projectDir / "revisions" / revCanonical / "command.json")
                .has_value()) {
            snapshot.files.push_back(
                                SnapshotFile{"revisions/" + revCanonical + "/command.json", 0});
        }
        for (const ObjectRef& ref : view.objectRefs) {
            addObject(ref.objectId, ref.contentVersion);
        }
        // results 勾选集（实现口径②）：闭包修订的完整运行并集——
        // listRuns 按 D-13 只认 manifest 发布的运行（§4.1 results 行
        // "查询不列为完整运行"）；五元组绑定修订，跨修订不重（dedupe
        // 仍按 runId 保守执行）。
        if (includeResults) {
            for (const RunInfo& run : query.listRuns(view.id)) {
                const std::string runCanonical = run.runId.toCanonical();
                if (selectedRunIds.insert(runCanonical).second) {
                    // 整 run 目录入清单（§8.9"整 run 目录＋manifest 原子
                    // 出现"——递归枚举，manifest 为最后成员由排序保证
                    // 语义顺序无关）。
                    std::vector<std::string> runFiles;
                    collectFilesRecursive(projectDir,
                                          projectDir / "results" / runCanonical,
                                          runFiles);
                    for (std::string& f : runFiles) {
                        snapshot.files.push_back(SnapshotFile{std::move(f), 0});
                    }
                }
            }
        }
    }
    for (const auto& [oid, cvHex] : objects) {
        snapshot.files.push_back(
            SnapshotFile{"objects/" + oid.toCanonical() + "/" + cvHex, 0});
    }

    // ---- 第 4 步：reports 勾选（§8.9 引用完整性——实现口径③）：
    // 每个报告目录读 report.json 文本提取 run- 规范 token；引用的 run
    // 不全在勾选集 → 整个报告不入清单＋skippedReports 登记＋开发诊断
    //（"报告在而证据缺"的悬空引用防护）。无 report.json/无引用＝无
    // 引用事实，照常复制（保守不拒）。
    if (includeReports) {
        const std::filesystem::path reportsDir = projectDir / "reports";
        std::error_code ec;
        if (std::filesystem::exists(reportsDir, ec) && !ec) {
            std::vector<std::string> reportIds;
            for (const auto& entry : std::filesystem::directory_iterator(reportsDir, ec)) {
                std::error_code entryEc;
                if (entry.is_directory(entryEc) && !entryEc) {
                    reportIds.push_back(entry.path().filename().string());
                }
            }
            std::sort(reportIds.begin(), reportIds.end());
            const std::set<std::string> selectedSet(selectedRunIds.begin(),
                                                    selectedRunIds.end());
            for (const std::string& reportId : reportIds) {
                const std::filesystem::path reportDir = reportsDir / reportId;
                std::set<std::string> referenced;
                if (const auto reportJson = tryReadFile(reportDir / "report.json")) {
                    referenced = extractRunTokens(*reportJson);
                }
                bool allSelected = true;
                for (const std::string& run : referenced) {
                    if (selectedSet.find(run) == selectedSet.end()) {
                        allSelected = false;
                        break;
                    }
                }
                if (!allSelected) {
                    // §8.9："该报告不复制＋诊断列出"——清单跳过＋结果
                    // 面登记＋开发诊断（稳定码不为此私造——CR-08）。
                    capture.skippedReports.push_back(reportId);
                    reportDev(sink, "project/save-as",
                              "report 跳过（引用 run 不在勾选集）: " + reportId);
                    continue;
                }
                std::vector<std::string> reportFiles;
                collectFilesRecursive(projectDir, reportDir, reportFiles);
                for (std::string& f : reportFiles) {
                    snapshot.files.push_back(SnapshotFile{std::move(f), 0});
                }
            }
        }
    }

    // ---- 第 5 步：drafts 勾选（§8.9"drafts 独立可复制"——按锚定
    // 分支表枚举当前有效草稿〔.new/.bak 残留不列——§8.4；listDrafts
    // 语义同源〕；锚定表而非 currentMetadata——捕获自洽）。
    if (includeDrafts) {
        for (const BranchRecord& branch : snapshot.headMetadata.branches) {
            for (const DraftInfo& draft : query.listDrafts(branch.branchId)) {
                const std::string rel = "drafts/" + branch.branchId.toCanonical()
                    + "/" + draft.moduleId + ".draft.json";
                snapshot.files.push_back(SnapshotFile{rel, 0});
            }
        }
    }

    // ---- 第 6 步：清单尺寸补齐＋全表确定性排序（relPath 字典序——
    // NFR-COR-02：同磁盘状态必得同清单，测试与验收可逐元素比对）。
    for (SnapshotFile& file : snapshot.files) {
        if (file.relPath == "HEAD") {
            file.sizeBytes = snapshot.headBytes.size();
            continue;
        }
        std::error_code ec;
        const auto size = std::filesystem::file_size(joinNative(projectDir,
                                                                 file.relPath),
                                                     ec);
        file.sizeBytes = ec ? 0 : static_cast<std::uint64_t>(size);
    }
    std::sort(snapshot.files.begin(), snapshot.files.end(),
              [](const SnapshotFile& a, const SnapshotFile& b) {
                  return a.relPath < b.relPath;
              });

    // 勾选集确定性排序（runId 规范文本字典序——NFR-COR-02；报告引用
    // 判定用 set 已有序，这里为快照结果面排序）。
    snapshot.selectedRunIds.assign(selectedRunIds.begin(), selectedRunIds.end());
    return capture;
}

// =====================================================================
// 复制与改写
// =====================================================================

void copySnapshotFiles(const std::filesystem::path& sourceRoot,
                       const StoreSnapshot& snapshot,
                       const std::filesystem::path& stagingDir,
                       win32::AtomicFile& atomic,
                       win32::IFileOps& ops,
                       IDiagnosticsSink* sink)
{
    for (const SnapshotFile& file : snapshot.files) {
        // 逐文件复制（清单为唯一范围——清单外一律不碰，D-17 双向契约）。
        const std::filesystem::path target = joinNative(stagingDir, file.relPath);
        const std::filesystem::path parent = target.parent_path();
        const win32::FileResult dirs = ops.createDirectories(parent.wstring());
        if (!dirs.ok) {
            throw osToStoreError(dirs.osError, "saveas-copy-create-dirs",
                                 file.relPath);
        }

        // 字节来源分流：HEAD 用快照捕获字节（不重读磁盘——防并发提交
        // 换 HEAD 造成"新 HEAD 引用未复制修订"的混合副本，文件头背景
        // 说明）；其余按类别读源。
        std::optional<std::string> bytes;
        if (file.relPath == "HEAD") {
            bytes = snapshot.headBytes;
        } else {
            bytes = tryReadFile(joinNative(sourceRoot, file.relPath));
        }
        if (!bytes.has_value()) {
            const bool isDraft = file.relPath.rfind("drafts/", 0) == 0;
            if (isDraft) {
                // 实现口径④：捕获后被放弃（discard 删除）的草稿＝编辑态
                // 的合法并发事实——跳过＋开发诊断，不失败（草稿独立，
                // §8.9；导出路径的对照语义见 SnapshotView::read）。
                reportDev(sink, "project/save-as",
                          "draft 跳过（复制期已不可读）: " + file.relPath);
                continue;
            }
            throw StoreError(StoreErrorCode::StoreCorrupt,
                             "project/save-as: 快照文件不可读（失真）: " + file.relPath);
        }

        // write-through 持久性闸门（§7.2 行 1——副本文件落盘承诺；
        // CREATE_ALWAYS 截断语义使三处改写点的就地重写同走本通道）。
        const win32::FileResult written = atomic.writeThrough(
            target.wstring(), bytes->data(), bytes->size());
        if (!written.ok) {
            throw osToStoreError(written.osError, "saveas-copy-write",
                                 file.relPath);
        }
    }
}

void rewriteProjectIdForCopy(const StoreSnapshot& snapshot,
                             const core::ProjectId& newId,
                             const std::filesystem::path& stagingDir,
                             win32::AtomicFile& atomic,
                             IDiagnosticsSink* sink)
{
    (void)sink;
    // ---- 改写点 1/3：project.json.projectId（§4.2 静态标识——另存为
    // 整体重写，其余字段逐字保留；parse→仅改→dump 的重写形态＝字节级
    // 最小改写，canonical 固定字段序保证同解释）。
    {
        const auto bytes = tryReadFile(stagingDir / "project.json");
        if (!bytes.has_value()) {
            throw StoreError(StoreErrorCode::StoreCorrupt,
                             "project/save-as: 副本 project.json 不可读（复制失真）");
        }
        ProjectStaticIdentity identity = codec::parseStaticIdentity(*bytes);
        identity.projectId = newId;
        const std::string dumped = codec::dump(identity);
        const win32::FileResult written = atomic.writeThrough(
            (stagingDir / "project.json").wstring(), dumped.data(), dumped.size());
        if (!written.ok) {
            throw osToStoreError(written.osError, "saveas-rewrite-project-json",
                                 "project.json");
        }
    }

    // ---- 改写点 2/3：HEAD.projectId（§8.8"HeadRecord 字段"——用快照
    // 捕获字节解析，与复制进副本的内容同源；manifestDigest 等其余字段
    // 透传不改——修订清单字节未动，自校验语义保持）。
    {
        HeadRecord head = codec::parseHeadRecord(snapshot.headBytes);
        head.projectId = newId;
        const std::string dumped = codec::dump(head);
        const win32::FileResult written = atomic.writeThrough(
            (stagingDir / "HEAD").wstring(), dumped.data(), dumped.size());
        if (!written.ok) {
            throw osToStoreError(written.osError, "saveas-rewrite-head", "HEAD");
        }
    }

    // ---- 改写点 3/3：drafts 逐文件 projectId（§8.8"drafts 的 projectId
    // 逐文件重写"——归属三元组校验的数据前提；清单中 drafts/ 前缀且
    // .draft.json 后缀的成员即草稿）。
    for (const SnapshotFile& file : snapshot.files) {
        if (file.relPath.rfind("drafts/", 0) != 0
            || file.relPath.size() < std::string(".draft.json").size()
            || file.relPath.compare(file.relPath.size() - std::string(".draft.json").size(),
                                    std::string(".draft.json").size(),
                                    ".draft.json") != 0) {
            continue;
        }
        const auto bytes = tryReadFile(joinNative(stagingDir, file.relPath));
        if (!bytes.has_value()) {
            // 复制期被跳过的草稿（实现口径④）不在清单重写面——清单仍
            // 含其条目而文件缺失时按跳过处理（与复制侧同口径，避免双
            // 处不一致）。
            continue;
        }
        DraftDocument doc = codec::parseDraftDocument(*bytes);
        doc.projectId = newId;
        const std::string dumped = codec::dump(doc);
        const win32::FileResult written = atomic.writeThrough(
            joinNative(stagingDir, file.relPath).wstring(),
            dumped.data(), dumped.size());
        if (!written.ok) {
            throw osToStoreError(written.osError, "saveas-rewrite-draft",
                                 file.relPath);
        }
    }
}

// =====================================================================
// 临时位与发布原语
// =====================================================================

std::filesystem::path makeTempSibling(const std::filesystem::path& target,
                                      const char* prefix)
{
    // 8 位随机 hex（实现口径⑤——random_device 播种 mt19937_64，线程
    // 局部引擎；TxEngine::generateTxId 同源形态。进程内再叠加单调序号
    // 无必要：目录创建失败（撞名）即整体失败重试由调用方语义承担——
    // 实际撞名概率 1/2^32 且临时位随操作消亡）。
    thread_local std::mt19937_64 rng{std::random_device{}()};
    char hex[9];
    static const char* kDigits = "0123456789abcdef";
    const std::uint64_t v = rng();
    for (int i = 0; i < 8; ++i) {
        hex[i] = kDigits[(v >> (i * 4)) & 0xF];
    }
    hex[8] = '\0';
    return target.parent_path()
        / (std::string(".") + prefix + "-" + hex + ".tmp");
}

void removeTreeBestEffort(const std::filesystem::path& dir,
                          win32::IFileOps& ops,
                          IDiagnosticsSink* sink,
                          const char* channel)
{
    const win32::FileResult removed = ops.removeTree(dir.wstring());
    if (!removed.ok) {
        // 清理失败＝残留（句柄占用等）——开发诊断如实上报（PM-05 的
        // "不留半成品"已尽尽力义务；失败升级为异常只会掩盖原始错误——
        // 调用方拿到的是第一手失败原因）。
        reportDev(sink, channel,
                  "临时位清理失败（残留，可手工删除）: "
                      + dir.string() + " osError=" + std::to_string(removed.osError));
    }
}

win32::FileResult publishDirectory(const std::filesystem::path& stagingDir,
                                   const std::filesystem::path& targetDir,
                                   win32::IFileOps& ops,
                                   const char* /*what*/)
{
    // 只增发布原语（§7.2 行 2——MoveFileExW 不带 REPLACE；目录形态
    // 同语义：目标已存在即失败，不覆盖——PM-05/导入威胁矩阵
    // "不覆盖既有目录"的机制承载；同卷由调用方"临时位＝目标同父目录"
    // 的装配保证）。
    return ops.publishNew(stagingDir.wstring(), targetDir.wstring());
}

StoreError osToStoreError(unsigned long osError,
                          const std::string& what,
                          const std::string& path)
{
    // 与 TxEngine/ObjectStore 同表（实现口径⑥）：磁盘满→DiskFull、
    // 拒绝访问→AccessDenied、其余→WriteRejected；分类逻辑单点复用
    // （tx::osToStoreError 为事务域私有——本层按同表重铸，detail 前缀
    // 区分域，两处登记同源不漂移）。
    constexpr unsigned long kDiskFull = 70;        // ERROR_DISK_FULL
    constexpr unsigned long kQuotaExceeded = 395;  // ERROR_DISK_QUOTA_EXCEEDED
    constexpr unsigned long kAccessDenied = 5;     // ERROR_ACCESS_DENIED
    const std::string detail = "project/save-as: " + what
        + " path=" + path + " osError=" + std::to_string(osError);
    if (osError == kDiskFull || osError == kQuotaExceeded) {
        return StoreError(StoreErrorCode::DiskFull, detail);
    }
    if (osError == kAccessDenied) {
        return StoreError(StoreErrorCode::AccessDenied, detail);
    }
    return StoreError(StoreErrorCode::WriteRejected, detail);
}

win32::IFileOps* defaultFileOps()
{
    // 进程级共享的无状态实现（AtomicFile 默认构造同款取舍——函数级
    // 静态，C++11 线程安全初始化）。
    static win32::Win32FileOps instance;
    return &instance;
}

// =====================================================================
// 快照视图（D-17 导出取数通道）
// =====================================================================

SnapshotView::SnapshotView(const StoreSnapshot& snapshot, std::filesystem::path sourceRoot)
    : m_snapshot(&snapshot)
    , m_sourceRoot(std::move(sourceRoot))
{
}

std::vector<PackFileEntry> SnapshotView::enumerate() const
{
    // 冻结清单投影（packPath ＝ "payload/" 前缀＋relPath——包内编址
    // 契约；每次调用同值——D-17"同一导出内多次调用返回全等清单"）。
    std::vector<PackFileEntry> entries;
    entries.reserve(m_snapshot->files.size());
    for (const SnapshotFile& file : m_snapshot->files) {
        PackFileEntry entry;
        entry.packPath = "payload/" + file.relPath;
        entry.sizeBytes = file.sizeBytes;
        entries.push_back(std::move(entry));
    }
    return entries;
}

bool SnapshotView::read(const std::string& packPath, std::vector<std::uint8_t>& outBytes)
{
    const std::string rel = stripPackPrefix(packPath);
    if (rel == "HEAD") {
        // 快照捕获字节（与另存为复制侧同纪律——不重读磁盘，防并发提交
        // 换 HEAD 造成混合包）。
        outBytes.assign(m_snapshot->headBytes.begin(), m_snapshot->headBytes.end());
        return true;
    }
    const auto bytes = tryReadFile(joinNative(m_sourceRoot, rel));
    if (!bytes.has_value()) {
        // 不可读（drafts 被放弃删除等）——按接口契约返回 false：编解码
        // 实现应中止导出并清理，不产出混合版本包（io.md §7.2"仍失败→
        // 导出失败清理"同口径）。
        return false;
    }
    outBytes.assign(bytes->begin(), bytes->end());
    return true;
}

std::string SnapshotView::stripPackPrefix(const std::string& packPath) const
{
    // "payload/" 前缀校验＋清单成员校验（编解码实现违约 fail-fast——
    // "清单外文件一概不读"是双方共同契约，io.md §7.2 步②同文）。
    static const std::string kPrefix = "payload/";
    if (packPath.rfind(kPrefix, 0) != 0) {
        throw std::invalid_argument("project/package: packPath 缺 payload/ 前缀: "
                                    + packPath);
    }
    const std::string rel = packPath.substr(kPrefix.size());
    const auto match = std::find_if(m_snapshot->files.begin(), m_snapshot->files.end(),
                                    [&](const SnapshotFile& f) { return f.relPath == rel; });
    if (match == m_snapshot->files.end()) {
        throw std::invalid_argument("project/package: packPath 不在快照清单内: "
                                    + packPath);
    }
    return rel;
}

}  // namespace sdurws::ird::project::clone
