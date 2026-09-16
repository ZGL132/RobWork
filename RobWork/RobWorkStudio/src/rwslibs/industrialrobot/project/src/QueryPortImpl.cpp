/**
 * @file   QueryPortImpl.cpp
 * @brief  查询端口实现（QueryPortImpl）——§5.2 各方法的实现体（私有
 *         实现，契约见 QueryPort.hpp/QueryPortImpl.hpp 注释）。
 *
 * 设计依据：
 *   - units/project.md §5.2（查询端口契约表）、§4.5（闭包规则/INV-M3）、
 *     §4.6（对象读取/缓存/引用存在性）、§4.7（只读查询线程与生命周期）、
 *     §4.1（drafts/results/revisions 目录命名与"完整"判定）、§6.3
 *     （hasUnresolvedPayload）、§10.1/D-13（仅 finalize 运行可列出）；
 *   - 任务契约 tasks/foundation/PRJ-T09.json acceptance 1～4。
 *
 * 实现口径登记（DTB §5.4，随单元卡 §12 v0.11 同步）：
 *   1. tryObject 的 noexcept 边界：§5.2 签名原文 noexcept 与底层"损坏＝
 *      抛 StoreCorrupt"的二分在异常面冲突——落位为 noexcept 契约优先：
 *      一切失败（关闭/不存在/损坏/无效身份）→ nullopt＋reportDev 开发
 *      诊断（损坏不静默）；强语义调用方走 object()。
 *   2. hasUnresolvedPayload 判据注入：本阶段无注册表（PRJ-T10 产物），
 *      判据为空 → 字段恒 false（§5.2 字段默认值语义——"无法判定"≠
 *      "已知未知"）；T10 注入注册表查询后真判定。
 *   3. branchHistory 的"未知分支抛"：StoreErrorCode 封闭集无 unknown-
 *      branch 码（不私扩——新增码＝单元卡增量修订），落位为
 *      std::invalid_argument（调用方违约 fail-fast，错误二分一致）。
 *   4. listDrafts/listRuns 的扫描容错：单个草稿/manifest 解析失败 →
 *      跳过＋reportDev（清单面保守不虚报可用条目——§4.1"无 manifest
 *      ＝不列为完整运行""损坏草稿处置归恢复流程"），不中断整表。
 *
 * 线程模型：见 QueryPortImpl.hpp 文件头"一致性窗口与锁序"。
 */

#include "QueryPortImpl.hpp"

#include "Codec.hpp"
#include "ObjectStore.hpp"
#include "ProjectStoreImpl.hpp"
#include "RevisionIndex.hpp"

#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <system_error>
#include <utility>

#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/core/Identity.hpp>

namespace fs = std::filesystem;

namespace sdurws::ird::project {

namespace {

// =====================================================================
// 文件读取辅助（数据侧错误统一 StoreCorrupt——PM-02 读校验语义）
// =====================================================================

/**
 * @brief 整读一个存储格式文件（HEAD/command.json/manifest.json/草稿）。
 *
 * @param file [in] 文件绝对路径（项目目录内的规范条目——§4.1）
 * @return 文件字节
 *
 * @throws StoreError StoreCorrupt（文件不存在/不可读——发布后就位的
 *         存储条目缺失即数据侧损坏，定位路径进 detail；环境性拒绝如
 *         AccessDenied 亦归并到读校验失败——查询端口契约只有数据侧与
 *         状态侧两轴，介质层面的细分码由打开协议/写路径承载）
 *
 * 实现形态：与 ProjectStoreImpl/ObjectStore 的读取口同款（std::ifstream
 * ——MSVC 支持宽路径；查询期低频小文件，可读性优先）。
 */
std::string readStoreFile(const fs::path& file)
{
    std::error_code ec;
    if (!fs::is_regular_file(file, ec) || ec) {
        throw StoreError(StoreErrorCode::StoreCorrupt,
                         "project/query: store file unreadable file=" + file.string()
                             + (ec ? " error=" + ec.message() : std::string()));
    }
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        throw StoreError(StoreErrorCode::StoreCorrupt,
                         "project/query: store file unopenable file=" + file.string());
    }
    // size 已知可预分配，但 istreambuf 迭代形态与项目其余读取口同款；
    // 查询期文件均为小体积（清单/命令/HEAD/草稿），无性能热点。
    std::string bytes{std::istreambuf_iterator<char>(in),
                      std::istreambuf_iterator<char>()};
    if (in.bad()) {
        // 读中途硬件错误——不可作为"内容为空"使用（数据侧拒绝）。
        throw StoreError(StoreErrorCode::StoreCorrupt,
                         "project/query: store file read error file=" + file.string());
    }
    return bytes;
}

}  // namespace

// =====================================================================
// 构造与门卫
// =====================================================================

QueryPortImpl::QueryPortImpl(ProjectStoreImpl& host, CommandKnownFn knownFn)
    : m_host(host), m_commandKnown(std::move(knownFn))
{
}

void QueryPortImpl::requireOpen() const
{
    // 锁序第一步：lifecycle 锁内读状态位（与 finishClose 的置位互斥——
    // §9.6①"关闭路径同步置位状态"）。锁在语句块结束即释放；此后窗口内
    // 发生 close 也安全——close 不删除任何被读数据（§5.1 关闭不清理
    // 磁盘），查询结果仍为有效的磁盘事实。
    std::lock_guard<std::mutex> lifecycleLock(m_host.m_lifecycleMutex);
    if (m_host.m_state == StoreLifecycleState::Closed) {
        // §4.7：上下文关闭后查询端口进入 context-closed 拒绝（稳定码
        // §4.4.8——与写入口的 PRJ-TX-9 拒绝同码，语义轴不同：查询的
        // 拒绝无写权限含义）。
        throw StoreError(StoreErrorCode::ContextClosed,
                         "project/query: context-closed（存储上下文已关闭——§4.7）");
    }
}

bool QueryPortImpl::isOpen() const noexcept
{
    try {
        std::lock_guard<std::mutex> lifecycleLock(m_host.m_lifecycleMutex);
        return m_host.m_state != StoreLifecycleState::Closed;
    } catch (...) {
        // 互斥体故障（不可达防御）——noexcept 边界按"拒绝"处理（保守）。
        return false;
    }
}

void QueryPortImpl::reportDevQuietly(const ProjectStoreImpl& host,
                                     const std::string& message) noexcept
{
    // sink 调用包 try：noexcept 调用方（tryObject）不可因诊断通道故障
    // 终止——sink 实现按契约不抛，此处为防御最后一道（吞掉的只是
    // "诊断上报失败"这一层，查询语义已定）。sink 可空＝丢弃（§5.0）。
    try {
        if (IDiagnosticsSink* sink = host.queryDevSink()) {
            sink->reportDev("project/query", message);
        }
    } catch (...) {
        // 诊断通道故障不升级为查询失败（见上）。
    }
}

// =====================================================================
// HEAD 与修订视图（一致性窗口在 writer 锁内）
// =====================================================================

RevisionView QueryPortImpl::head() const
{
    requireOpen();

    RevisionManifest manifest;
    {
        // 一致性窗口（§4.5.1 结论④）：提交的 S6 时序是"注册新修订进
        // 索引→发布内容→原子切换 HEAD"，全程持 writer 锁。本查询在
        // 同一把锁内重读 HEAD 并取清单，保证只看到"已切换 HEAD"的完整
        // 提交——绝无"清单可查但 HEAD 未切"的中间态视图（否则该修订
        // 会被误认已提交——自审项 A-6 的反例形态）。
        std::lock_guard<std::mutex> writerLock(m_host.m_writerMutex);
        // 重读磁盘 HEAD（§5.2 原文"重读 HEAD 文件"——地面事实，不依赖
        // 端口缓存）。parse 失败（缺失/损坏）由 Codec 以 StoreCorrupt 抛出。
        const HeadRecord head = codec::parseHeadRecord(
            readStoreFile(m_host.m_canonicalDirFs / "HEAD"));
        // HEAD 指向的修订必须在会话索引（打开时装载了闭包内全部修订、
        // 提交成功后索引同步前进）。查不到＝HEAD 被外部篡改指向未装载
        // 修订（悬挂）——数据侧损坏，防误认已提交（A-6）。
        const auto loaded = m_host.m_index->tryRevision(head.revisionId);
        if (!loaded.has_value()) {
            throw StoreError(StoreErrorCode::StoreCorrupt,
                             "project/query: HEAD revision not in session index revision="
                                 + head.revisionId.toCanonical());
        }
        manifest = *loaded;
    }
    // 视图组装（读 command.json）在锁外——修订目录发布后就位只增不改
    // （PA-2），锁外读安全且不放大 writer 锁持有时长。
    return assembleView(manifest);
}

std::optional<RevisionView> QueryPortImpl::tryRevision(core::RevisionId id) const
{
    requireOpen();
    // 全零＝"未设置"（§4.1 U-1 保留值纪律）——按"没有这个修订"返回空，
    // 不视为调用方错误（与 RevisionIndex::tryRevision 的口径一致）。
    if (!id.isValid()) {
        return std::nullopt;
    }

    std::optional<RevisionManifest> manifest;
    {
        // 一致性窗口：提交中（已注册未切 HEAD）的新修订被 writer 锁挡住
        // ——窗口内 tryRevision(新修订) 拿不到（索引虽然已有，但本查询
        // 等锁），窗口外（HEAD 已切换）它就是合法的已提交修订。闭包外
        // 修订（磁盘残留）则根本不在索引（装载纪律，§7.3）——两级防线
        // 共同保证"仅闭包内修订可取"（§5.2/A-6）。
        std::lock_guard<std::mutex> writerLock(m_host.m_writerMutex);
        manifest = m_host.m_index->tryRevision(id);
    }
    if (!manifest.has_value()) {
        return std::nullopt;
    }
    return assembleView(*manifest);
}

RevisionView QueryPortImpl::revision(core::RevisionId id) const
{
    // 全零在强语义下是调用方契约违约（"查询没有的修订"合法、"查询未
    // 设置的身份"违约——fail-fast，不产出 store-corrupt 假象）。
    if (!id.isValid()) {
        throw std::invalid_argument(
            "project/query: revision id is all-zero（全零保留值——调用方契约违约）");
    }
    // 复用 try 轨（含生命周期门卫与一致性窗口）；查不到按 §5.2 错误表
    // 升级为 store-corrupt——调用方已断言存在，查不到即数据侧异常。
    const auto view = tryRevision(id);
    if (!view.has_value()) {
        throw StoreError(StoreErrorCode::StoreCorrupt,
                         "project/query: revision not in committed closure revision="
                             + id.toCanonical());
    }
    return *view;
}

RevisionView QueryPortImpl::assembleView(const RevisionManifest& manifest) const
{
    RevisionView view;
    view.id = manifest.revisionId;
    view.seq = manifest.revisionSeq;
    view.parent = manifest.parentRevisionId;
    view.branch = manifest.branchId;
    view.objectRefs = manifest.objectRefs;

    // 第一步：metadataRef 的完整引用形态。清单的 metadataRef 是最小
    // 引用对 (oid,cv)，§4.4.2 约束 objectRefs 必含其完整引用（含类型
    // token 与摘要）；装载由事务引擎装配、正常不可达，此处防御性校验
    // （清单结构违约＝数据侧损坏——不产半截视图）。
    const ObjectRef* metadataEntry = nullptr;
    for (const ObjectRef& ref : manifest.objectRefs) {
        if (ref.objectId == manifest.metadataRef.objectId
            && ref.contentVersion == manifest.metadataRef.contentVersion) {
            metadataEntry = &ref;
            break;
        }
    }
    if (metadataEntry == nullptr) {
        throw StoreError(StoreErrorCode::StoreCorrupt,
                         "project/query: metadataRef missing from objectRefs revision="
                             + manifest.revisionId.toCanonical());
    }
    view.metadataRef = *metadataEntry;

    // 第二步：命令留痕（§4.1 revisions 行：目录内含 manifest.json＋
    // command.json——发布后就位；缺失/损坏＝数据侧，历史浏览不可降级
    // ——PM-12-S1 的摘要展示面是修订视图的必备字段）。
    const CommandRecord command = codec::parseCommandRecord(readStoreFile(
        m_host.m_canonicalDirFs / "revisions" / manifest.revisionId.toCanonical()
        / "command.json"));
    view.commandSummary = command.summary;
    // 逆命令直拷（InverseRef＝InverseCommand 别名——同一契约类型，
    // nullopt＝不可逆命令，§6.9）。
    view.inverse = command.inverse;

    // 第三步：未知命令标志。判据为空（注册表未装配——PRJ-T10 前）→
    // 字段默认 false（§5.2 字面语义；实现口径 2）；判据在→按判据。
    view.hasUnresolvedPayload =
        m_commandKnown ? !m_commandKnown(command.commandType) : false;
    return view;
}

// =====================================================================
// 元数据与分支视图（权威＝HEAD 引用版本，INV-M3）
// =====================================================================

ProjectMetadataView QueryPortImpl::currentMetadata() const
{
    requireOpen();
    // writer 锁内整体拷贝权威版本：m_authoritative/m_authoritativeRef 在
    // executeCommit 成功路径同步前进（writer 锁内），锁内拷贝保证 ref 与
    // record 是同一权威版本（不会拿到"新 ref 配旧 record"的撕裂对）。
    std::lock_guard<std::mutex> writerLock(m_host.m_writerMutex);
    ProjectMetadataView view;
    view.ref = m_host.m_authoritativeRef;
    view.record = m_host.m_authoritative;  // 深拷贝（值语义——PA-3）
    return view;
}

std::optional<ProjectMetadataView> QueryPortImpl::metadataAt(core::RevisionId id) const
{
    requireOpen();
    if (!id.isValid()) {
        return std::nullopt;  // 全零＝未设置（同 tryRevision 口径）
    }

    std::optional<ProjectMetadataView> result;
    {
        // 一致性窗口：修订与其元数据的两段索引读在同一 writer 锁内完成
        // （metadataRef 指向的元数据一经注册不可变，实际上锁外也安全；
        // 同锁完成消除了"修订在窗口前后"的讨论——简单且正确）。
        std::lock_guard<std::mutex> writerLock(m_host.m_writerMutex);
        const auto manifest = m_host.m_index->tryRevision(id);
        if (!manifest.has_value()) {
            return std::nullopt;  // 闭包外/不存在——可见性纪律同 tryRevision
        }
        const auto record = m_host.m_index->tryMetadata(manifest->metadataRef);
        if (!record.has_value()) {
            // 修订命中但其 metadataRef 悬挂＝索引装载不完整/存储损坏
            // ——数据侧（打开协议的闭包校验正常不可达，防御性拒绝）。
            throw StoreError(StoreErrorCode::StoreCorrupt,
                             "project/query: metadataRef dangling revision="
                                 + id.toCanonical());
        }
        result = ProjectMetadataView{manifest->metadataRef, *record};
    }
    return result;
}

std::vector<BranchTip> QueryPortImpl::branchTips() const
{
    requireOpen();
    // 锁内快照权威元数据，锁外投影分支表（BranchTip 是纯值投影，
    // 不再触碰存储）。
    ProjectMetadataRecord authoritative;
    {
        std::lock_guard<std::mutex> writerLock(m_host.m_writerMutex);
        authoritative = m_host.m_authoritative;
    }
    // 保持权威元数据 branches 数组的磁盘序：该序由 deriveNextMetadata
    // 逐字继承（D-5），随提交稳定，同磁盘状态必得同输出（NFR-COR-02）。
    std::vector<BranchTip> tips;
    tips.reserve(authoritative.branches.size());
    for (const BranchRecord& b : authoritative.branches) {
        BranchTip tip;
        tip.id = b.branchId;
        tip.label = b.label;
        tip.base = b.baseRevisionId;
        tip.tip = b.tipRevisionId;  // 权威版本的 tip（INV-M3——不取自历史元数据）
        tips.push_back(std::move(tip));
    }
    return tips;
}

std::vector<RevisionView> QueryPortImpl::branchHistory(core::BranchId branch,
                                                       std::uint32_t maxCount) const
{
    requireOpen();

    // 第一步（一致性窗口内）：自权威元数据取该分支 tip（§5.2 表前置
    // "分支存在于权威元数据"；INV-M3——tip 一律取自 HEAD 引用版本，
    // 禁止经历史修订的元数据重建）。分支不存在＝调用方违约 → fail-fast
    // （实现口径 3：封闭集无对应稳定码，不私扩）。
    core::RevisionId tip{};
    {
        std::lock_guard<std::mutex> writerLock(m_host.m_writerMutex);
        const auto found = revindex::RevisionIndex::branchTip(m_host.m_authoritative,
                                                              branch);
        if (!found.has_value()) {
            throw std::invalid_argument(
                "project/query: unknown branch branch=" + branch.toCanonical()
                + "（§5.2 表：未知分支抛——调用方契约违约）");
        }
        tip = *found;
    }

    // 平凡请求：maxCount==0 → 空列表（字面语义"最多 0 条"；实现口径）。
    std::vector<RevisionView> history;
    if (maxCount == 0) {
        return history;
    }

    // 第二步（锁外沿链）：自 tip 沿 parent 逐级回溯至首修订或凑满
    // maxCount。已提交修订一经索引注册永在且不可变（PA-2——索引只增
    // 无删除），锁外查索引安全；分支 tip 维度的 parent 语义保证只含
    // 本分支提交（§4.5——其他分支的提交不在本链上）。
    history.reserve(maxCount);
    std::optional<core::RevisionId> current = tip;
    while (current.has_value() && history.size() < maxCount) {
        const auto manifest = m_host.m_index->tryRevision(*current);
        if (!manifest.has_value()) {
            // parent 链悬挂＝装载不完整/存储损坏——数据侧（打开闭包
            // 校验正常不可达，防御性拒绝）。
            throw StoreError(StoreErrorCode::StoreCorrupt,
                             "project/query: branch history dangling parent revision="
                                 + current->toCanonical());
        }
        history.push_back(assembleView(*manifest));
        current = manifest->parentRevisionId;
    }
    return history;
}

// =====================================================================
// 对象读取（§4.6——惰性＋缓存＋首读校验；预算随打开请求注入）
// =====================================================================

std::optional<std::vector<std::uint8_t>> QueryPortImpl::tryObject(
    core::ObjectId oid, core::ContentVersion cv) const noexcept
{
    // noexcept 契约下的拒绝表达（实现口径 1）：关闭态 → nullopt＋开发
    // 诊断（拒绝态与"对象不存在"对调用方是两种事实——诊断通道保留
    // 可见性，避免调试期把关闭拒绝误读为引用缺失）。
    if (!isOpen()) {
        reportDevQuietly(m_host,
                         "tryObject rejected oid=" + oid.toCanonical()
                             + " cv=" + cv.toCanonical()
                             + " reason=context-closed（§4.7 拒绝态）");
        return std::nullopt;
    }
    try {
        // 直通 ObjectStore：不存在 → nullopt（§4.6 引用存在性 try 轨）；
        // 损坏 → StoreCorrupt——在此被 catch 降级（见下）。缓存互斥在
        // ObjectStore 内部（§4.6"缓存互斥内部实现"），与提交并发安全
        // （对象文件不可变）。
        return m_host.m_objects->tryObject(oid, cv);
    } catch (const std::exception& e) {
        // noexcept 边界：损坏（篡改/位翻转）与环境失败都不可抛——开发
        // 诊断保留可见性（不静默），调用方以 nullopt 表达"不可读取"。
        // 需要强校验/强错误语义的调用方走 object()（§5.2 二分的抛出面）。
        reportDevQuietly(m_host,
                         "tryObject failed oid=" + oid.toCanonical()
                             + " cv=" + cv.toCanonical() + " reason=" + e.what());
        return std::nullopt;
    } catch (...) {
        // 非标准异常（不可达防御）——同样降级，无法取 what()。
        reportDevQuietly(m_host,
                         "tryObject failed oid=" + oid.toCanonical()
                             + " cv=" + cv.toCanonical() + " reason=unknown");
        return std::nullopt;
    }
}

std::vector<std::uint8_t> QueryPortImpl::object(core::ObjectId oid,
                                                core::ContentVersion cv) const
{
    requireOpen();
    // 强语义直通：引用缺失/摘要不符 → StoreCorrupt（§5.2 错误表"引用
    // 缺失＝store-corrupt"）；全零身份 → invalid_argument（ObjectStore
    // 契约透传）。不取 writer 锁——对象文件不可变（内容寻址只增）。
    return m_host.m_objects->object(oid, cv);
}

// =====================================================================
// 草稿清单（§5.2/§4.4.5——含 baseRevision 与过期标记）
// =====================================================================

std::vector<DraftInfo> QueryPortImpl::listDrafts(core::BranchId branch) const
{
    requireOpen();

    // 第一步（一致性窗口内）：权威 tip（stale 判据的数据源）＋分支存在
    // 性。分支不在权威表 → branchKnown=false，条目全部保守标记 stale
    // （孤儿草稿不私裁丢弃——处置权在草稿服务/恢复流程，§7.4③）。
    bool branchKnown = false;
    core::RevisionId tip{};
    {
        std::lock_guard<std::mutex> writerLock(m_host.m_writerMutex);
        const auto found = revindex::RevisionIndex::branchTip(m_host.m_authoritative,
                                                              branch);
        if (found.has_value()) {
            branchKnown = true;
            tip = *found;
        }
    }

    // 第二步（锁外扫盘）：drafts/<branch-id>/ 下当前有效草稿。目录不
    // 存在＝该分支无草稿（常态，返回空表）。
    std::vector<DraftInfo> drafts;
    const fs::path branchDir =
        m_host.m_canonicalDirFs / "drafts" / branch.toCanonical();
    std::error_code ec;
    if (!fs::exists(branchDir, ec) || ec) {
        if (ec) {
            // stat 失败（权限等环境原因）——如实按数据侧拒绝，不静默
            // 空表（空表语义保留给"确实没有"）。
            throw StoreError(StoreErrorCode::StoreCorrupt,
                             "project/query: drafts dir unreadable dir="
                                 + branchDir.string() + " error=" + ec.message());
        }
        return drafts;
    }

    // 手写迭代循环（非范围 for）：increment(ec) 的非抛推进形态——目录
    // 扫描容错（单条目权限失败不中断整表），错误集中检查。
    const fs::directory_iterator end;
    fs::directory_iterator it(branchDir, fs::directory_options::skip_permission_denied,
                              ec);
    if (ec) {
        throw StoreError(StoreErrorCode::StoreCorrupt,
                         "project/query: drafts dir unscannable dir="
                             + branchDir.string() + " error=" + ec.message());
    }
    const std::string suffix = ".draft.json";
    for (; it != end && !ec; it.increment(ec)) {
        const fs::directory_entry& entry = *it;
        // 只收当前有效草稿文件：<module>.draft.json。崩溃残留 .new/.bak
        // 形态（"<module>.draft.json.new"）不以 ".draft.json" 结尾，
        // 天然排除（§8.4——残留处置归恢复诊断，不进可用清单）。
        const std::string name = entry.path().filename().string();
        std::error_code entryEc;
        const bool regular = entry.is_regular_file(entryEc);
        if (entryEc || !regular || name.size() <= suffix.size()
            || name.compare(name.size() - suffix.size(), suffix.size(), suffix) != 0) {
            continue;
        }
        try {
            const DraftDocument doc =
                codec::parseDraftDocument(readStoreFile(entry.path()));
            DraftInfo info;
            // moduleId 取解析值（内容与文件名的一致性校验归草稿服务
            // T12——本端口是清单投影，透传解析事实）。
            info.moduleId = doc.moduleId;
            info.baseRevision = doc.baseRevisionId;
            info.savedAtUtc = doc.savedAtUtc;
            info.origin = doc.origin;
            // 过期标记：base ≠ 权威 tip → 应用被拒（PM-04 stale 判据）；
            // 分支不存在 → 保守 true（基线不可达）。
            info.stale = branchKnown ? (doc.baseRevisionId != tip) : true;
            drafts.push_back(std::move(info));
        } catch (const std::exception& e) {
            // 单条目损坏：跳过＋开发诊断（实现口径 4——清单面不虚报
            // 可用草稿；恢复处置权在草稿服务/PM-15 横幅）。
            reportDevQuietly(m_host, "listDrafts skip corrupt draft file="
                                         + entry.path().string() + " reason="
                                         + e.what());
        }
    }
    if (ec) {
        // 迭代中途失败（非单条目权限类）——整表按数据侧拒绝（已收集
        // 部分不交付——半表是更危险的假象）。
        throw StoreError(StoreErrorCode::StoreCorrupt,
                         "project/query: drafts scan failed dir=" + branchDir.string()
                             + " error=" + ec.message());
    }

    // 确定性排序：moduleId 字典序（目录枚举序 OS 相关——NFR-COR-02
    // 要求同状态同输出，显式排序）。
    std::sort(drafts.begin(), drafts.end(),
              [](const DraftInfo& a, const DraftInfo& b) {
                  return a.moduleId < b.moduleId;
              });
    return drafts;
}

// =====================================================================
// 运行清单与目录（§5.2/D-13——仅 finalize（有 manifest）的运行）
// =====================================================================

std::vector<RunInfo> QueryPortImpl::listRuns(core::RevisionId revision) const
{
    requireOpen();
    std::vector<RunInfo> runs;
    // 全零＝未设置修订——没有运行绑定于"未设置"（空表，同 try 轨口径）。
    if (!revision.isValid()) {
        return runs;
    }

    const fs::path resultsDir = m_host.m_canonicalDirFs / "results";
    std::error_code ec;
    if (!fs::exists(resultsDir, ec) || ec) {
        if (ec) {
            throw StoreError(StoreErrorCode::StoreCorrupt,
                             "project/query: results dir unreadable dir="
                                 + resultsDir.string() + " error=" + ec.message());
        }
        return runs;  // 无 results 目录＝尚无任何归档（常态）
    }

    // 手写迭代循环（同 listDrafts——increment(ec) 非抛形态）。
    const fs::directory_iterator end;
    fs::directory_iterator it(resultsDir, fs::directory_options::skip_permission_denied,
                              ec);
    if (ec) {
        throw StoreError(StoreErrorCode::StoreCorrupt,
                         "project/query: results dir unscannable dir="
                             + resultsDir.string() + " error=" + ec.message());
    }
    for (; it != end && !ec; it.increment(ec)) {
        const fs::directory_entry& entry = *it;
        std::error_code entryEc;
        if (!entry.is_directory(entryEc) || entryEc) {
            continue;  // 非目录条目（不应出现——防御跳过）
        }
        // 目录名必须为 run- 规范文本（§4.1 命名规则）；不合规条目跳过
        // ＋开发诊断（不中断整表——命名残骸的处置归恢复扫描）。
        const std::string dirName = entry.path().filename().string();
        const auto runId = core::RunId::tryFromCanonical(dirName);
        if (!runId.has_value()) {
            reportDevQuietly(m_host,
                             "listRuns skip non-run directory name=" + dirName);
            continue;
        }
        // manifest 缺失＝未完成归档（分批写入中/abandon 残留）——§4.1
        // results 行"查询不列为完整运行"（D-13），静默跳过是设计行为
        // （恢复诊断已列出，查询面不重复产码）。
        const fs::path manifestFile = entry.path() / "manifest.json";
        if (!fs::exists(manifestFile, entryEc) || entryEc) {
            continue;
        }
        try {
            const RunManifest manifest =
                codec::parseRunManifest(readStoreFile(manifestFile));
            // 按修订过滤（§5.2 listRuns(revision)——归档绑定原修订的
            // 反向查询，§10.1 绑定语义）。
            if (manifest.taskIdentity.revision != revision) {
                continue;
            }
            RunInfo info;
            info.runId = *runId;
            info.task = manifest.taskIdentity;
            info.runKind = manifest.runKind;
            info.evaluationKey = manifest.evaluationKey;
            info.finalizedAtUtc = manifest.finalizedAtUtc;
            runs.push_back(std::move(info));
        } catch (const std::exception& e) {
            // manifest 在但解析失败＝数据损坏（发布原子性保证"在即完整"，
            // 损坏必是外部篡改/介质故障）——不列为完整运行＋开发诊断
            // （实现口径 4；篡改定位归恢复扫描的 HEAD 闭包校验）。
            reportDevQuietly(m_host, "listRuns skip corrupt manifest file="
                                         + manifestFile.string() + " reason="
                                         + e.what());
        }
    }
    if (ec) {
        throw StoreError(StoreErrorCode::StoreCorrupt,
                         "project/query: results scan failed dir="
                             + resultsDir.string() + " error=" + ec.message());
    }

    // 确定性排序：runId 规范文本字典序（NFR-COR-02）。
    std::sort(runs.begin(), runs.end(),
              [](const RunInfo& a, const RunInfo& b) {
                  return a.runId.toCanonical() < b.runId.toCanonical();
              });
    return runs;
}

std::filesystem::path QueryPortImpl::runDir(core::RunId run) const
{
    requireOpen();
    // 全零＝调用方契约违约（路径推导无法对"未设置"成立）——fail-fast。
    if (!run.isValid()) {
        throw std::invalid_argument(
            "project/query: run id is all-zero（全零保留值——调用方契约违约）");
    }
    // 规范编址：results/<run-id 规范文本>/（§4.1 命名规则——目录名即
    // runId 规范文本）。目录可能尚不存在（未完成归档）——路径纯计算，
    // 存在性由调用方按 listRuns/D-13 判定（runDir 契约注释）。
    return m_host.m_canonicalDirFs / "results" / run.toCanonical();
}

}  // namespace sdurws::ird::project
