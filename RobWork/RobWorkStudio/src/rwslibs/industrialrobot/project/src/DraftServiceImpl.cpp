/**
 * @file   DraftServiceImpl.cpp
 * @brief  草稿服务实现——§5.4 落盘协议（.new→.bak 轮换→原子就位）、
 *         §8.4 恢复/损坏处置、§8.5 汇总投影、§8.6 只读/生命周期边界的
 *         执行体（PRJ-T12；契约与实现口径见 DraftService.hpp/
 *         DraftServiceImpl.hpp 文件头，本文件注释聚焦"每一步在做什么、
 *         为什么"）。
 *
 * 设计依据：
 *   - units/project.md §5.4/§8.1～§8.6（语义锚点逐段对应——各方法注释
 *     标注出处）、§9.6～§9.8（门卫/互斥/票据）、§7.2（AtomicFile 原语的
 *     持久性/原子可见性保证——本文件的落盘序列建立在"writeThrough 失败
 *     绝不进入可见性切换"的原语契约之上）；
 *   - 需求 PM-04/PM-07/PM-08/PM-15、NFR-COR-02；
 *   - 任务契约 tasks/foundation/PRJ-T12.json acceptance 1～4。
 */

#include "DraftServiceImpl.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <mutex>
#include <system_error>

#include <sdurws/ird/core/Identity.hpp>

#include "Codec.hpp"
#include "DiagRecords.hpp"
#include "ProjectStoreImpl.hpp"
#include "RevisionIndex.hpp"

// Win32 原始错误码（mapOsError 的映射键——IFileOps 契约：FileResult.osError
// 携带 GetLastError 原始值）。本翻译单元经 PathCanonical.hpp→windows.h
// 已可见，显式列出依赖的两个码值便于对照。
#ifndef ERROR_DISK_FULL
#error "windows.h 未可见——DraftServiceImpl 的错误映射依赖 Win32 错误码"
#endif

namespace sdurws::ird::project {

namespace {

// ---- 磁盘编址常量（§4.1 drafts 行命名规则）----
// 目录名与查询端口（QueryPortImpl.cpp 内字面量）、打开协议⑤步扫描
// （ProjectStoreImpl.cpp kDraftsDirName）同值——三处消费同一 §4.1 契约；
// 未收敛为共享常量的原因：各文件匿名命名空间内的私有字面量，值变更属
// 格式契约变更（须同步修订 §4.1），字面量就地可读性优先（登记口径）。
constexpr const char* kDraftsDirName = "drafts";
/// 当前草稿文件名后缀（<module>.draft.json——§4.1）。
constexpr std::string_view kDraftSuffix = ".draft.json";

/**
 * @brief 非抛整读一个小文件（草稿文件量级——模块负载，MB 内）。
 *
 * 读失败（不存在/权限）与读到空文件都如实区分：返回 nullopt＝I/O 层
 * 失败（损坏判定交给调用方归类 reason）；空字节串＝合法的零字节文件
 * （解析层会拒绝——缺字段）。
 *
 * @param file [in] 目标文件路径
 * @return 文件字节；I/O 失败＝nullopt
 */
std::optional<std::string> readDraftFile(const std::filesystem::path& file)
{
    std::error_code ec;
    // 存在性先行：ifstream 打开失败无法区分"不存在"与"权限拒绝"，但两
    // 者对恢复流程的处置一致（＝该候选不可用），故合并为 nullopt——
    // 差异细节进 reason 由 exists 预判补充。
    if (!std::filesystem::exists(file, ec) || ec) {
        return std::nullopt;
    }
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    std::string bytes{std::istreambuf_iterator<char>(in),
                      std::istreambuf_iterator<char>()};
    if (in.bad()) {
        // 读中途 I/O 错误（如网络盘抖动）——半截内容不能当完整文件用。
        return std::nullopt;
    }
    return bytes;
}

/**
 * @brief Win32 原始错误码 → 稳定错误码映射（草稿落盘 I/O 面）。
 *
 * 映射规则（封闭集内就近归类——§4.4.8 不私扩）：
 *   - ERROR_DISK_FULL（含 0xE0 扩展面的等价值不逐一枚举——底层以原始码
 *     进 detail）→ DiskFull：§8.2 点名的"落盘失败（磁盘满）"场景；
 *   - ERROR_ACCESS_DENIED → AccessDenied：介质/权限拒绝（§9.5 行 1/2 的
 *     写路径面）；
 *   - 其余 → WriteRejected：写被环境拒绝的兜底类（共享冲突/句柄失效/
 *     介质保护等）——原始码随 detail 保留定位能力。
 *
 * @param osError [in] FileResult 携带的 Win32 原始错误码（无物理单位）
 * @return 稳定错误码
 */
StoreErrorCode mapOsError(unsigned long osError)
{
    if (osError == ERROR_DISK_FULL) {
        return StoreErrorCode::DiskFull;
    }
    if (osError == ERROR_ACCESS_DENIED) {
        return StoreErrorCode::AccessDenied;
    }
    return StoreErrorCode::WriteRejected;
}

/**
 * @brief 单个草稿候选文件的装载＋归属校验（非抛——try 轨的共享执行体）。
 *
 * 校验链（§4.4.5"加载时与路径和 project.json 校验，不一致＝
 * DraftCorruptDetected"的三项分解）：
 *   ①读文件（I/O 失败＝不可用）；
 *   ②canonical 解析（codec::parseDraftDocument——语法/版本/字段级校验；
 *     StoreCorrupt/FormatLegacy/SchemaFuture 三类版本与结构失败都算
 *     "该候选不可解读"——版本类的恢复语义同损坏：草稿不是权威状态，
 *     未来格式草稿按恢复场景报告而非阻塞，诊断 detail 携带 reason 键值）；
 *   ③归属三元组：projectId＝本上下文（跨项目脏数据拒绝）、branchId＝
 *     参数分支、moduleId＝调用方请求（路径与内容一致性——文件名推导
 *     由调用方在扫描面完成，本函数以参数为准做内容面校验）。
 *
 * 任一环节失败即整体失败（reason 携带环节标识供诊断/开发日志分类），
 * 不产出半可用文档。
 */
struct LoadOutcome {
    bool ok = false;      ///< true＝doc 可用（校验链全部通过）
    std::string reason;   ///< 失败环节标识（read/parse/ownership:<字段>）
    DraftDocument doc;    ///< 装载结果（ok==true 时有效）
};

LoadOutcome loadDraftFile(const std::filesystem::path& file,
                          core::BranchId branch, std::string_view moduleId,
                          core::ProjectId contextProject)
{
    // ① 读：I/O 失败——候选不可用（不存在走不到这里——调用方先判存在）。
    const auto bytes = readDraftFile(file);
    if (!bytes.has_value()) {
        return LoadOutcome{false, "read", {}};
    }
    // ② 解析：canonical 语法/版本/字段。异常→失败原因归类（版本类与
    //    结构类同处置，reason 保留稳定码名供诊断 detail）。
    DraftDocument doc;
    try {
        doc = codec::parseDraftDocument(*bytes);
    } catch (const StoreError& e) {
        return LoadOutcome{false, std::string{"parse:"} + e.what(), {}};
    } catch (const std::exception&) {
        return LoadOutcome{false, "parse:unknown", {}};
    }
    // ③ 归属三元组（逐字段给 reason——恢复横幅/开发定位用）：
    //    projectId 是跨项目脏数据的最后防线（防 A 项目草稿被 B 项目
    //    加载——内容寻址身份的运行时面）。
    if (!(doc.projectId == contextProject)) {
        return LoadOutcome{false, "ownership:projectId", {}};
    }
    if (!(doc.branchId == branch)) {
        return LoadOutcome{false, "ownership:branchId", {}};
    }
    if (doc.moduleId != moduleId) {
        return LoadOutcome{false, "ownership:moduleId", {}};
    }
    return LoadOutcome{true, {}, std::move(doc)};
}

/**
 * @brief 目录内枚举 <module>.draft.json 的 moduleId（文件名推导面）。
 *
 * 规则（与查询端口 listDrafts 同口径）：只收以 .draft.json 结尾的常规
 * 文件——.new/.bak 残留（"<module>.draft.json.new"/".bak"）不以该后缀
 * 结尾，天然排除（§8.4：残留不是当前草稿，处置归 tryLoad/discard）。
 *
 * @param name [in] 目录条目文件名
 * @return 推导的模块 token；形态不符＝空串（调用方跳过）
 */
std::string moduleFromFileName(const std::string& name)
{
    const std::string suffix{kDraftSuffix};
    if (name.size() <= suffix.size()
        || name.compare(name.size() - suffix.size(), suffix.size(), suffix)
               != 0) {
        return {};
    }
    return name.substr(0, name.size() - suffix.size());
}

}  // namespace

// =====================================================================
// 构造与私有设施
// =====================================================================

DraftServiceImpl::DraftServiceImpl(ProjectStoreImpl& host) : m_host(host)
{
}

void DraftServiceImpl::validateModuleId(std::string_view moduleId)
{
    // 白名单：1～64 个 ASCII 字母/数字/下划线/连字符。逐字符判定（非法
    // 字节如 UTF-8 多字节序列自然被排除——ASCII 范围外一律拒绝）。
    const bool sizeOk = !moduleId.empty() && moduleId.size() <= 64;
    if (!sizeOk) {
        throw std::invalid_argument(
            "project/draft: moduleId 长度越界（1~64）");
    }
    for (const char c : moduleId) {
        const bool allowed = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
            || (c >= '0' && c <= '9') || c == '_' || c == '-';
        if (!allowed) {
            // 点/斜杠/空格等一律拒绝：moduleId 直接拼磁盘路径，白名单
            // 是路径穿越与后缀歧义的结构性防线（不静默净化——净化会让
            // 调用方误以为保存到了它指定的名字下）。
            throw std::invalid_argument(
                "project/draft: moduleId 含非法字符（白名单 A-Za-z0-9_-）");
        }
    }
}

std::optional<DraftServiceImpl::GuardFailure> DraftServiceImpl::
    checkWriteGuard(std::string_view action) const
{
    // ---- 门卫第①道：上下文状态机（§9.6①；与 executeCommit 同序同码
    // ——拒绝码与用户级诊断逐字对齐，差异仅在错误出口走返回值轨）。
    {
        std::lock_guard<std::mutex> guard(m_host.m_lifecycleMutex);
        if (m_host.m_state != StoreLifecycleState::Active) {
            const char* stateName
                = m_host.m_state == StoreLifecycleState::Draining ? "draining"
                                                                  : "closed";
            if (m_host.m_sink != nullptr) {
                m_host.m_sink->report(diagrec::makeWriteAuthorityLost(
                    "project/draft: 写拒绝（草稿保存/放弃——上下文非 "
                    "Active） action="
                    + std::string{action} + " state=" + stateName));
            }
            return GuardFailure{
                StoreErrorCode::ContextClosed,
                "project/draft: context not writable state="
                    + std::string{stateName}};
        }
    }

    // ---- 门卫第②道：从未持锁＝只读上下文（PM-07 禁编辑的存储侧落实
    // ——§8.6"草稿不可保存/放弃"；诊断 PRJ-LOCK-HELD 同 executeCommit）。
    if (m_host.m_lock == nullptr) {
        if (m_host.m_sink != nullptr) {
            m_host.m_sink->report(diagrec::makeLockHeld(
                "project/draft: 写拒绝（草稿保存/放弃——只读上下文） action="
                + std::string{action}));
        }
        return GuardFailure{StoreErrorCode::LockHeldByOther,
                            "project/draft: readonly context（未持有写锁）"};
    }

    // ---- 门卫第③道：StoreLock 权威探测（§9.6①②——状态＋句柄有效性；
    // 已释放/已失权的诊断 PRJ-WRITE-AUTHORITY-LOST 由 StoreLock 产出）。
    if (!m_host.m_lock->requireWriteAuthority()) {
        return GuardFailure{
            StoreErrorCode::WriteRejected,
            "project/draft: write authority check failed（句柄已释放或已失权）"};
    }
    return std::nullopt;
}

void DraftServiceImpl::requireOpen() const
{
    // 读轨拒绝语义（§4.7 同查询端口）：仅 Closed 拒——Draining 排空期
    // 查询/恢复仍可用（PM-03 关闭对话框需呈现草稿恢复态）。
    std::lock_guard<std::mutex> guard(m_host.m_lifecycleMutex);
    if (m_host.m_state == StoreLifecycleState::Closed) {
        throw StoreError(StoreErrorCode::ContextClosed,
                         "project/draft: context closed（读轨拒绝）");
    }
}

void DraftServiceImpl::reportDev(const std::string& channel,
                                 const std::string& message) const noexcept
{
    // sink 可空＝丢弃（§5.0——装配方失去观察面是其自身选择）；诊断通道
    // 自身故障不得终止业务路径（包 try——noexcept 边界的常规防线）。
    if (m_host.m_sink == nullptr) {
        return;
    }
    try {
        m_host.m_sink->reportDev(channel, message);
    } catch (...) {
        // 开发诊断通道故障静默——业务结果（SaveResult/DiscardResult/
        // tryLoad 返回值）承载真实成败，诊断缺失不改变数据面语义。
    }
}

void DraftServiceImpl::reportUser(
    const core::DiagnosticRecord& record,
    std::vector<core::DiagnosticRecord>& diags) const
{
    // 双通道：sink 即时上报＋追加进调用方向量（同源同序——RecoveryReport
    // 口径；消费方按需取用）。sink 侧包 try（同 reportDev——记录已进
    // diags，上报失败不丢失可见性）。
    diags.push_back(record);
    if (m_host.m_sink != nullptr) {
        try {
            m_host.m_sink->report(record);
        } catch (...) {
            // 同上：诊断通道故障不终止恢复流程。
        }
    }
}

std::filesystem::path DraftServiceImpl::draftFilePath(
    core::BranchId branch, std::string_view moduleId) const
{
    // §4.1 drafts 行编址：drafts/<branch-id>/<module>.draft.json——分支
    // 目录名＝分支规范文本（与打开协议⑤步扫描、查询端口清单同一编址，
    // 归属校验"路径与内容一致"的锚点）。
    return m_host.m_canonicalDirFs / kDraftsDirName / branch.toCanonical()
        / (std::string{moduleId} + std::string{kDraftSuffix});
}

// =====================================================================
// save——落盘协议（§5.4 原文序列；PM-04 不产生修订）
// =====================================================================

SaveResult DraftServiceImpl::save(const DraftDocument& doc)
{
    // ---- 前置校验（调用方错误 fail-fast——先于一切锁与 I/O）：
    // 模块 token 白名单（路径安全面）＋项目归属（跨项目脏数据的最后
    // 防线——落盘即制造他项目目录下的伪草稿）。
    validateModuleId(doc.moduleId);
    if (!(doc.projectId == m_host.projectId())) {
        throw std::invalid_argument(
            "project/draft: doc.projectId 与存储上下文不一致（拒绝跨项目"
            "落盘）");
    }

    // ---- 规范化编码（锁外——纯 CPU，缩短 writer 临界区；NFR-COR-02
    // 确定性字节）。payload 无效 UTF-8 在此抛 invalid_argument（调用方
    // 错误透传——Codec 契约），不进入落盘序列。
    const std::string canonical = codec::dump(doc);

    // ---- 写门卫三道（拒绝走返回值轨——ui 定时器安全；诊断已在门卫内
    // 产出）。detail 带 moduleId 便于定位涉事草稿。
    if (const auto failure = checkWriteGuard("save:" + doc.moduleId);
        failure.has_value()) {
        return SaveResult{false, StoreError{failure->code, failure->detail},
                          {}};
    }

    // ---- 在途票据（§9.7"DraftService（在途保存）"——requestClose 的
    // 排空等待覆盖在途草稿落盘）。票据获取在 lifecycle 锁下判定：上下文
    // 已 Closed 时返回空票（门卫①与取票之间的关闭窗口在此闭合——空票
    // ＝写面已关闭，拒绝而非继续写）。
    auto inFlight = m_host.acquireInFlight();
    if (!inFlight) {
        return SaveResult{
            false,
            StoreError{StoreErrorCode::ContextClosed,
                       "project/draft: context closed（票据获取失败——关闭"
                       "窗口）"},
            {}};
    }

    // ---- writer 互斥段（§9.8：变更性文件操作的串行化点——草稿写与
    // 事务/归档写在此汇合，目录内单写者假设成立）。
    const std::lock_guard<std::mutex> writerGuard(m_host.m_writerMutex);

    const std::filesystem::path current
        = draftFilePath(doc.branchId, doc.moduleId);
    // .new＝暂存名；.bak＝上一版名（§4.1 drafts 行"覆盖写（原子替换）＋
    // .bak 保留上一版"；任务约束§五.6 D-12 模块粒度单文件＋.bak 轮换）。
    const std::filesystem::path newFile
        = current.wstring() + L".new";
    const std::filesystem::path bakFile
        = current.wstring() + L".bak";

    // 失败收尾的公共形态：删除 .new（"清理失败的 .new"——绝不留半写
    // 文件），返回失败结果。删除本身也可能失败（权限）——残留经开发
    // 诊断可见，主错误优先不覆盖。
    auto failWith = [&](StoreErrorCode code, const std::string& detail,
                        unsigned long osError) {
        std::error_code rmEc;
        std::filesystem::remove(newFile, rmEc);
        if (rmEc) {
            reportDev("project/draft",
                      "save cleanup failed（.new 清理失败） file="
                          + newFile.string() + " osError="
                          + std::to_string(rmEc.value()));
        }
        reportDev("project/draft-save-failed",
                  "save failed module=" + doc.moduleId + " detail=" + detail
                      + " osError=" + std::to_string(osError));
        return SaveResult{false, StoreError{code, detail}, {}};
    };

    // 第 0 步：分支目录就位（首次保存时 drafts/<branch>/ 可能不存在；
    // std::filesystem 非抛形态——目录创建不是原子替换协议的环节，失败
    // 经 ec 值归类，Windows 上 value() 即 Win32 原始码）。
    std::error_code ec;
    std::filesystem::create_directories(current.parent_path(), ec);
    if (ec) {
        return failWith(mapOsError(static_cast<unsigned long>(ec.value())),
                        "project/draft: create drafts dir failed path="
                            + current.parent_path().string(),
                        static_cast<unsigned long>(ec.value()));
    }

    // 第 1 步：写 .new（writeThrough＝写穿透＋FlushFileBuffers 持久性
    // 闸门——§7.2 行 1；闸门失败绝不进入可见性切换）。
    const auto written = m_file.writeThrough(
        newFile.wstring(), canonical.data(), canonical.size());
    if (!written.ok) {
        // 持久性闸门失败：.new 已不可信（半写/未刷盘）——删除并失败
        // 返回；current/.bak 未被触碰，调用方数据无损。
        return failWith(mapOsError(written.osError),
                        "project/draft: write .new failed module="
                            + doc.moduleId,
                        written.osError);
    }

    // 第 2 步：旧版轮换——current 存在则改名 .bak（REPLACE_EXISTING 覆盖
    // 旧 .bak，.bak 始终是上一版）。current 不存在＝首次保存（无轮换）。
    // 存在性判定带 ec：stat 失败按环境错误处理（不能把"查不到"当"没有"
    // ——可能权限类故障，误判会跳过轮换直接替换）。
    const bool currentExists = std::filesystem::exists(current, ec);
    if (ec) {
        return failWith(
            mapOsError(static_cast<unsigned long>(ec.value())),
            "project/draft: stat current failed module=" + doc.moduleId,
            static_cast<unsigned long>(ec.value()));
    }
    if (currentExists) {
        // 失败时 current 未动（rename 原子——目标保持原状），数据无损。
        const auto rotated = m_file.replaceFile(current.wstring(),
                                                    bakFile.wstring());
        if (!rotated.ok) {
            return failWith(
                mapOsError(rotated.osError),
                "project/draft: rotate current→.bak failed module="
                    + doc.moduleId,
                rotated.osError);
        }
    }

    // 第 3 步：.new 原子就位为 current（replaceExisting＝MoveFileExW
    // REPLACE_EXISTING|WRITE_THROUGH——读者见旧或新完整内容之一，§7.2
    // 行 3；此时目标不存在〔已轮换为 .bak〕或不存在〔首次保存〕，仍用
    // REPLACE 形态防御外部干预的竞态残留）。
    const auto published
        = m_file.replaceFile(newFile.wstring(), current.wstring());
    if (!published.ok) {
        // 失败：current 已是 .bak（上一版数据完好——"旧版保留"承诺在
        // 失败路径依然成立），.new 已被清理；返回失败由调用方决定重试。
        return failWith(
            mapOsError(published.osError),
            "project/draft: publish .new→current failed module="
                + doc.moduleId,
            published.osError);
    }

    // 成功：草稿落盘完毕——不产生任何修订（PM-04；revisions/ 与 HEAD
    // 均未触碰，PRJ-TX-6 的"保存与应用分离"断言点）。
    return SaveResult{true, std::nullopt, current};
}

// =====================================================================
// tryLoad——恢复入口（§8.4：损坏诊断＋.bak 回退＋.new 残留丢弃）
// =====================================================================

std::optional<DraftDocument> DraftServiceImpl::tryLoad(
    core::BranchId branch, std::string_view moduleId,
    std::vector<core::DiagnosticRecord>& diags) const
{
    // 调用方错误先行（fail-fast）；随后读轨开放判定（Closed 拒）。
    validateModuleId(moduleId);
    requireOpen();

    const std::filesystem::path current = draftFilePath(branch, moduleId);
    const std::filesystem::path newFile = current.wstring() + L".new";
    const std::filesystem::path bakFile = current.wstring() + L".bak";

    // ---- .new 残留处置（§8.4"`.new` 残留＝保存崩溃现场→丢弃 .new、
    // 保留 current/.bak 并报告"）：存在即删除——它是失败保存的半成品，
    // 不参与恢复候选；删除事实（成败）经开发诊断保留可见性（用户级
    // 无对应收编码——CR-08 不私造；current 损坏时另有用户级诊断）。
    std::error_code ec;
    if (std::filesystem::exists(newFile, ec) && !ec) {
        const bool removed = std::filesystem::remove(newFile, ec);
        reportDev("project/draft",
                  std::string{"stale .new residue "}
                      + (removed && !ec ? "discarded" : "discard-failed")
                      + " file=" + newFile.string()
                      + (ec ? " osError=" + std::to_string(ec.value()) : ""));
    }

    // ---- 读 current：不存在＝无草稿（常态，nullopt 且无诊断——try 轨
    // 对"没有"返回空，§4.6 存在性语义）。
    if (!std::filesystem::exists(current, ec) || ec) {
        return std::nullopt;
    }
    const core::ProjectId contextProject = m_host.projectId();
    const LoadOutcome currentOutcome
        = loadDraftFile(current, branch, moduleId, contextProject);
    if (currentOutcome.ok) {
        return currentOutcome.doc;
    }

    // ---- current 不可用：draft-corrupt 用户级诊断（diagnostics.md §8.2
    // 映射行"draft-corrupt → PRJ-RECOVERY-ORPHAN-DRAFT，恢复场景/Info，
    // 附加上下文＝模块/分支"——diagrec 唯一装配点工厂），双通道上报。
    reportUser(diagrec::makeDraftCorrupt(
                   std::string{moduleId}, branch.toCanonical(),
                   "current-unusable file=" + current.string()
                       + " reason=" + currentOutcome.reason),
               diags);

    // ---- .bak 回退（§8.4"旧草稿保留：.bak（上一版）优先恢复"）：可
    // 解析且归属通过→返回上一版（diags 仍含 current 损坏记录——横幅
    // 需要知道发生了恢复而非一切正常）。
    if (std::filesystem::exists(bakFile, ec) && !ec) {
        const LoadOutcome bakOutcome
            = loadDraftFile(bakFile, branch, moduleId, contextProject);
        if (bakOutcome.ok) {
            reportDev("project/draft",
                      "recovered from .bak module=" + std::string{moduleId}
                          + " file=" + bakFile.string());
            return bakOutcome.doc;
        }
        // 上一版也坏：如实追加诊断（两代皆损——恢复失败，nullopt；
        // 项目权威状态不受影响——草稿只是编辑中临时态）。
        reportUser(diagrec::makeDraftCorrupt(
                       std::string{moduleId}, branch.toCanonical(),
                       "backup-unusable file=" + bakFile.string()
                           + " reason=" + bakOutcome.reason),
                   diags);
    }

    return std::nullopt;
}

// =====================================================================
// summarize——多模块汇总投影（§8.5；磁盘事实投影——文件头口径 3）
// =====================================================================

DraftProjection DraftServiceImpl::summarize(core::BranchId branch) const
{
    requireOpen();

    // 第一步（一致性窗口内）：权威 tip（stale 判据的数据源——INV-M3：
    // 取 HEAD 引用的权威元数据，不经历史版本）。分支不在权威表→
    // branchKnown=false，条目保守标记 stale（孤儿草稿——同查询端口
    // DraftInfo 口径，不私裁丢弃）。
    bool branchKnown = false;
    core::RevisionId tip{};
    {
        std::lock_guard<std::mutex> writerLock(m_host.m_writerMutex);
        const auto found = revindex::RevisionIndex::branchTip(
            m_host.m_authoritative, branch);
        if (found.has_value()) {
            branchKnown = true;
            tip = *found;
        }
    }

    // 第二步（锁外扫盘）：单文件原子替换保证读到完整文件——与保存并发
    // 天然安全（§5.4 原语保证）。
    DraftProjection projection;
    projection.branch = branch;
    const std::filesystem::path branchDir
        = m_host.m_canonicalDirFs / kDraftsDirName / branch.toCanonical();
    std::error_code ec;
    if (!std::filesystem::exists(branchDir, ec) || ec) {
        // 目录不存在＝该分支无草稿（空投影是常态不是错误）；stat 失败
        // （权限类）按数据侧拒绝——空投影语义保留给"确实没有"。
        if (ec) {
            throw StoreError(
                StoreErrorCode::StoreCorrupt,
                "project/draft: drafts dir unreadable dir="
                    + branchDir.string() + " error=" + ec.message());
        }
        return projection;
    }

    const std::filesystem::directory_iterator end;
    std::filesystem::directory_iterator it(
        branchDir, std::filesystem::directory_options::skip_permission_denied,
        ec);
    if (ec) {
        throw StoreError(StoreErrorCode::StoreCorrupt,
                         "project/draft: drafts dir unscannable dir="
                             + branchDir.string() + " error=" + ec.message());
    }
    // 手写迭代循环（非范围 for）：increment(ec) 的非抛推进形态——单条目
    // 权限失败不中断整表；迭代中途失败按数据侧拒绝（半投影比空投影更
    // 危险——"未应用修改"标记会漏报）。
    for (; it != end && !ec; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        const std::string moduleId = moduleFromFileName(name);
        if (moduleId.empty() || !it->is_regular_file(ec) || ec) {
            continue;  // 非 .draft.json 形态/子目录/条目 stat 失败——跳过
        }
        const LoadOutcome outcome
            = loadDraftFile(it->path(), branch, moduleId, m_host.projectId());
        DraftProjectionItem item;
        item.moduleId = moduleId;
        if (outcome.ok) {
            // 可用草稿：present＋元数据透传（project 不解释 payload——
            // 投影只消费磁盘元数据字段，CR-02/D-10 对投影同样成立）。
            item.present = true;
            item.baseRevision = outcome.doc.baseRevisionId;
            item.stale = branchKnown ? (outcome.doc.baseRevisionId != tip)
                                     : true;
            item.savedAtUtc = outcome.doc.savedAtUtc;
            item.origin = std::string{
                toToken(outcome.doc.origin)};  // 冻结 token 透传（§8.5）
        } else {
            // 损坏草稿：present=false（投影面保守不虚报可用草稿——
            // "未应用修改"标记不得统计坏文件）＋开发诊断保留可见性。
            // 不产用户级码：恢复场景的用户级诊断归 tryLoad 的恢复入口
            // （双通道），投影是清单面（与查询端口 listDrafts 跳过口径
            // 同源——QueryPortImpl 实现口径 4）。
            reportDev("project/draft",
                      "summarize skip unusable draft file=" + it->path().string()
                          + " reason=" + outcome.reason);
            item.present = false;
        }
        projection.items.push_back(std::move(item));
    }
    if (ec) {
        throw StoreError(StoreErrorCode::StoreCorrupt,
                         "project/draft: drafts scan failed dir="
                             + branchDir.string() + " error=" + ec.message());
    }

    // 确定性排序（NFR-COR-02：目录枚举序 OS 相关——同磁盘状态必得同投影）。
    std::sort(projection.items.begin(), projection.items.end(),
              [](const DraftProjectionItem& a, const DraftProjectionItem& b) {
                  return a.moduleId < b.moduleId;
              });
    return projection;
}

// =====================================================================
// discard——放弃草稿（§8.6：写操作，只读拒绝；幂等目标态语义）
// =====================================================================

DiscardResult DraftServiceImpl::discard(core::BranchId branch,
                                        std::string_view moduleId)
{
    // 调用方错误先行（与 save 同面：路径安全白名单）。
    validateModuleId(moduleId);

    // 写门卫三道（§8.6"放弃＝写操作，只读模式拒绝＋诊断"——PM-07 存储
    // 侧落实；拒绝走返回值轨，诊断已在门卫内产出）。
    if (const auto failure = checkWriteGuard("discard:" + std::string{moduleId});
        failure.has_value()) {
        return DiscardResult{false,
                             StoreError{failure->code, failure->detail},
                             {}, false, false};
    }

    // 在途票据（§9.7——放弃与保存同属草稿写面；关闭窗口的空票判定同
    // save：Closed 后不再接受草稿删除动作，防止与关闭收尾竞争）。
    auto inFlight = m_host.acquireInFlight();
    if (!inFlight) {
        return DiscardResult{
            false,
            StoreError{StoreErrorCode::ContextClosed,
                       "project/draft: context closed（票据获取失败——关闭"
                       "窗口）"},
            {}, false, false};
    }

    const std::lock_guard<std::mutex> writerGuard(m_host.m_writerMutex);

    const std::filesystem::path current = draftFilePath(branch, moduleId);
    const std::filesystem::path newFile = current.wstring() + L".new";
    const std::filesystem::path bakFile = current.wstring() + L".bak";

    DiscardResult result;
    result.file = current;

    // 删除目标三件：current＋.bak 是 §5.4 表点名的放弃对象；.new 残留
    // 一并清理（实现口径登记：放弃的完备语义——用户明确不要的数据不因
    // 崩溃残留复活；§8.4 的 .new"丢弃"处置在 tryLoad 是恢复场景的副产
    // 清理，这里是用户意图的直接执行）。
    std::error_code ec;

    // current：不存在＝幂等（目标态部分成立）；存在则必须删净，删除
    // 失败（权限）＝ok=false——半删状态由明细位如实反映，不回滚（删除
    // 无"回滚"意义，重试即可）。
    if (std::filesystem::exists(current, ec) && !ec) {
        if (std::filesystem::remove(current, ec) && !ec) {
            result.removedCurrent = true;
        } else {
            result.error = StoreError{
                mapOsError(static_cast<unsigned long>(ec.value())),
                "project/draft: discard remove current failed file="
                    + current.string() + " osError="
                    + std::to_string(ec.value())};
            reportDev("project/draft-discard-failed",
                      "discard remove current failed file=" + current.string()
                          + " osError=" + std::to_string(ec.value()));
            result.ok = false;
            return result;  // current 删不掉即目标态未达成（.bak 仍可尝试
                            // 清理——继续执行会让结果面复杂化；先行返回，
                            // 残留由重试/恢复流程处置）。
        }
    } else if (ec) {
        // stat 失败（权限类）——按环境错误拒绝（不能把"查不到"当"没有"）。
        result.error = StoreError{
            mapOsError(static_cast<unsigned long>(ec.value())),
            "project/draft: discard stat current failed file="
                + current.string() + " osError=" + std::to_string(ec.value())};
        return result;
    }

    // .bak：与 current 同语义（上一版也是用户要放弃的数据）。
    if (std::filesystem::exists(bakFile, ec) && !ec) {
        if (std::filesystem::remove(bakFile, ec) && !ec) {
            result.removedBackup = true;
        } else {
            result.error = StoreError{
                mapOsError(static_cast<unsigned long>(ec.value())),
                "project/draft: discard remove .bak failed file="
                    + bakFile.string() + " osError="
                    + std::to_string(ec.value())};
            reportDev("project/draft-discard-failed",
                      "discard remove .bak failed file=" + bakFile.string()
                          + " osError=" + std::to_string(ec.value()));
            result.ok = false;
            return result;
        }
    } else if (ec) {
        result.error = StoreError{
            mapOsError(static_cast<unsigned long>(ec.value())),
            "project/draft: discard stat .bak failed file=" + bakFile.string()
                + " osError=" + std::to_string(ec.value())};
        return result;
    }

    // .new 残留清理：尽力而为（失败不改变目标态判定——它本来就不是
    // "当前草稿"，清理失败经开发诊断可见，恢复流程兜底）。
    if (std::filesystem::exists(newFile, ec) && !ec) {
        const bool removed = std::filesystem::remove(newFile, ec);
        reportDev("project/draft",
                  std::string{"discard .new residue "}
                      + (removed && !ec ? "removed" : "remove-failed")
                      + " file=" + newFile.string()
                      + (ec ? " osError=" + std::to_string(ec.value()) : ""));
    }

    // 目标态达成：current 与 .bak 均不存在（含"本来就没有"的幂等形态
    // ——两位明细 false 且 ok=true，DiscardResult 注释口径）。
    result.ok = true;
    return result;
}

// =====================================================================
// list——草稿清单（§8.4"打开时 listDrafts＋tryLoad"的 list 半边）
// =====================================================================

std::vector<DraftInfo> DraftServiceImpl::list(core::BranchId branch) const
{
    requireOpen();

    // 权威 tip（stale 判据——与 summarize 同款一致性窗口；INV-M3）。
    bool branchKnown = false;
    core::RevisionId tip{};
    {
        std::lock_guard<std::mutex> writerLock(m_host.m_writerMutex);
        const auto found = revindex::RevisionIndex::branchTip(
            m_host.m_authoritative, branch);
        if (found.has_value()) {
            branchKnown = true;
            tip = *found;
        }
    }

    std::vector<DraftInfo> drafts;
    const std::filesystem::path branchDir
        = m_host.m_canonicalDirFs / kDraftsDirName / branch.toCanonical();
    std::error_code ec;
    if (!std::filesystem::exists(branchDir, ec) || ec) {
        if (ec) {
            throw StoreError(
                StoreErrorCode::StoreCorrupt,
                "project/draft: drafts dir unreadable dir="
                    + branchDir.string() + " error=" + ec.message());
        }
        return drafts;  // 无草稿是常态（§5.2 表本行无前置）
    }
    const std::filesystem::directory_iterator end;
    std::filesystem::directory_iterator it(
        branchDir, std::filesystem::directory_options::skip_permission_denied,
        ec);
    if (ec) {
        throw StoreError(StoreErrorCode::StoreCorrupt,
                         "project/draft: drafts dir unscannable dir="
                             + branchDir.string() + " error=" + ec.message());
    }
    for (; it != end && !ec; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        const std::string moduleId = moduleFromFileName(name);
        if (moduleId.empty() || !it->is_regular_file(ec) || ec) {
            continue;  // 残留形态（.new/.bak）天然被后缀推导排除——§8.4
        }
        // 清单面装载：文件名推导 moduleId 与内容一致性在此统一校验
        // （归属不符＝该文件不是它声称的草稿——按损坏跳过，不进可用
        // 清单；tryLoad 的恢复入口才产出用户级诊断）。
        const LoadOutcome outcome
            = loadDraftFile(it->path(), branch, moduleId, m_host.projectId());
        if (!outcome.ok) {
            reportDev("project/draft",
                      "list skip unusable draft file=" + it->path().string()
                          + " reason=" + outcome.reason);
            continue;
        }
        DraftInfo info;
        info.moduleId = outcome.doc.moduleId;
        info.baseRevision = outcome.doc.baseRevisionId;
        info.stale = branchKnown ? (outcome.doc.baseRevisionId != tip) : true;
        info.savedAtUtc = outcome.doc.savedAtUtc;
        info.origin = outcome.doc.origin;
        drafts.push_back(std::move(info));
    }
    if (ec) {
        throw StoreError(StoreErrorCode::StoreCorrupt,
                         "project/draft: drafts scan failed dir="
                             + branchDir.string() + " error=" + ec.message());
    }

    // 确定性排序（NFR-COR-02——同查询端口 listDrafts 口径）。
    std::sort(drafts.begin(), drafts.end(),
              [](const DraftInfo& a, const DraftInfo& b) {
                  return a.moduleId < b.moduleId;
              });
    return drafts;
}

}  // namespace sdurws::ird::project
