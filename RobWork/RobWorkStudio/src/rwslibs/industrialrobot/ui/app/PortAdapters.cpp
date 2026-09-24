/**
 * @file   PortAdapters.cpp
 * @brief  工作台验证 harness 的端口适配器实现（对端语义原样翻译——零加工、
 *         零吞错、零虚构；翻译规则的契约锚点见 PortAdapters.hpp 各类注释）。
 *
 * 设计依据：
 *   - project.md §5.1（打开/关闭协议——OpenStoreRequest/Result、StoreError
 *     稳定 token、LockInfo 视图）、§5.4（DraftService——v0.1 桩的诚实边界
 *     对照面）、§9.3（canonicalPath 规范化口径）；
 *   - UiPorts.hpp C-3 工厂行（"成功才返回 ok=true；降级只读也是成功"）、
 *     C-3 关闭协议行（requestClose 幂等/subscribeClose 一次性回调）；
 *   - diagnostics §9.2（create 唯一入口）、§6.2（Dev 级走日志不入目录）。
 *
 * 线程约束：全部方法 UI 线程调用（UiPorts.hpp 头注 M-1 纪律）；打开协议
 * 内部的文件 IO 线程纪律由对端 ProjectStoreFactory 自行承担（§5.2——
 * 适配器不感知不复制）。
 */

#include "PortAdapters.hpp"

#include <sdurws/ird/diagnostics/Errors.hpp>      // DiagnosticsError/token（report 拒绝纪律的异常面与稳定 token）
#include <sdurws/ird/project/DraftService.hpp>    // project::DraftService/SaveResult/DiscardResult（drafts() 完整定义——草稿写半区直转）
#include <sdurws/ird/project/PersistenceFormat.hpp> // project::DraftDocument/DraftOrigin（草稿文档值翻译——UI-T17 写半区）
#include <sdurws/ird/project/QueryPort.hpp>       // project::IProjectQueryPort/ProjectMetadataView（query() 完整定义——权威元数据读取）

#include <condition_variable>
#include <deque>
#include <filesystem>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace sdurws::ird {
namespace ui {
namespace app {

namespace {

// ---------------------------------------------------------------------
// StoreError 稳定 token 映射（§5.3 显示差异的判别输入）
// ---------------------------------------------------------------------

/**
 * @brief StoreErrorCode → 稳定 token（token 串逐字取自 StoreTypes.hpp 枚举
 *        行注释——NFR-MNT-03 词表唯一权威；§4.4.8"稳定 token 对应的枚举值"
 *        的展示半区）。
 *
 * 为什么本地映射而不是取 what()：what() 是"码＋开发诊断 detail"的整串
 * （面向开发），failure.errorCodeToken 契约要求的是裸稳定 token（§5.3
 * 错误页按 token 判别显示差异）；detail 单独经 StoreError::what() 透传。
 * default 分支不可达（枚举穷举）——保底返回枚举序不做字符串拼接（新增枚举
 * 值时编译器 -Wswitch 会在此提示补行）。
 */
std::string storeErrorToken(project::StoreErrorCode code)
{
    switch (code) {
    case project::StoreErrorCode::LockHeldByOther:         return "lock-held-by-other";
    case project::StoreErrorCode::MediaReadOnly:           return "media-read-only";
    case project::StoreErrorCode::AccessDenied:            return "access-denied";
    case project::StoreErrorCode::NotAProject:             return "not-a-project";
    case project::StoreErrorCode::FormatLegacy:            return "format-legacy";
    case project::StoreErrorCode::SchemaFuture:            return "schema-future";
    case project::StoreErrorCode::StoreCorrupt:            return "store-corrupt";
    case project::StoreErrorCode::WriteRejected:           return "write-rejected";
    case project::StoreErrorCode::DiskFull:                return "disk-full";
    case project::StoreErrorCode::ContextClosed:           return "context-closed";
    case project::StoreErrorCode::StaleRevisionRejected:   return "stale-revision-rejected";
    case project::StoreErrorCode::UnknownCommand:          return "unknown-command";
    case project::StoreErrorCode::InvalidPayload:          return "invalid-payload";
    case project::StoreErrorCode::ConfirmationsUnresolved: return "confirmations-unresolved";
    case project::StoreErrorCode::InteractionLost:         return "interaction-lost";
    case project::StoreErrorCode::CommandAborted:          return "command-aborted";
    case project::StoreErrorCode::CompileFailed:           return "compile-failed";
    case project::StoreErrorCode::ArchiveConflict:         return "archive-conflict";
    case project::StoreErrorCode::ArchiveTargetMissing:    return "archive-target-missing";
    case project::StoreErrorCode::DraftCorrupt:            return "draft-corrupt";
    case project::StoreErrorCode::BranchMetadataRegression:return "branch-metadata-regression";
    }
    return {};
}

/// 订阅句柄的无操作实现（v0.1 诚实边界——见 StorePortAdapter 类注释）。
class NoopSubscription final : public core::IEventSubscription {
public:
    /// project 关闭契约"回调至多一次、无退订"——句柄析构无资源可释放。
    void unsubscribe() override {}
};

}  // namespace

// =====================================================================
// ProjectDiagnosticsBridge
// =====================================================================

ProjectDiagnosticsBridge::ProjectDiagnosticsBridge(
    std::shared_ptr<diagnostics::IDiagnosticSink> sink,
    std::shared_ptr<diagnostics::IDiagnosticFactory> factory,
    std::shared_ptr<diagnostics::IDevLogSink> devLog)
    : m_sink(std::move(sink))
    , m_factory(std::move(factory))
    , m_devLog(std::move(devLog))
{
    // 装配期 fail-fast：空下游的桥会把打开协议诊断静默丢弃（违反"禁止
    // 吞错"）——与其留给运行期假绿，不如装配即拦截。
    if (!m_sink || !m_factory || !m_devLog) {
        throw std::invalid_argument(
            "ProjectDiagnosticsBridge：sink/factory/devLog 必须非空（装配错误）");
    }
}

void ProjectDiagnosticsBridge::report(const core::DiagnosticRecord& record)
{
    // 经 create 唯一入口产条目（§9.2）：码表校验、entryId 分配、dedupKey
    // 计算全在工厂内。上下文＝对端宿主标识（DiagnosticsSinkImpl §9.7 同款
    // 形态——本桥是 project 打开协议的 sink，记录来源单元即 project；
    // sourceInterface 用其默认报告通道名）。params 保持空：core::
    // DiagnosticRecord 无参数字段（载荷由对端写入上下文文本——StoreLock
    // 的 PRJ-LOCK-HELD 构造注释即该口径），本桥不做"从文本反解结构化参数"
    // 的加工——反解既是脆弱的字面匹配又是数值虚构，两样都在纪律红线外。
    diagnostics::DiagContext context;
    context.sourceUnit = "project";
    context.sourceInterface = "sink.report";

    try {
        m_sink->append(m_factory->create(record, context));
        return;
    } catch (const diagnostics::DiagnosticsError& error) {
        // 拒绝分两类（显式纪律——PortAdapters.hpp 桥类注释；WP-10-T15 验收
        // attempt 1 阻断项 B-1 的返工落定点）。switch 全枚举语义分组：装配/
        // 桥自身缺陷上抛 fail-fast，对端记录契约缺口具名上报后协议继续。
        switch (error.code()) {
        case diagnostics::DiagnosticsErrorCode::CodeUnknown:
            // 码表缺对端在用码＝HarnessMain 装配期收编清单缺漏（装配错误）
            // ——必须 fail-fast，不允减带病装配继续跑。
        case diagnostics::DiagnosticsErrorCode::Usage:
            // 上下文 token 越界＝本桥构造的 context 违约（桥自身缺陷）——
            // 同为 fail-fast。两路异常由 HarnessMain 打开编排兜底转为
            // 可观测错误页＋非零退出（不落 std::terminate 进程死亡）。
            throw;
        case diagnostics::DiagnosticsErrorCode::CodeDeprecated:
        case diagnostics::DiagnosticsErrorCode::SubjectMissing:
        case diagnostics::DiagnosticsErrorCode::ComparisonMissing:
        case diagnostics::DiagnosticsErrorCode::ParamSchemaMismatch:
        case diagnostics::DiagnosticsErrorCode::ContextMissing:
            // 对端记录内容 vs 工厂校验链的契约缺口：记录是对端按其契约发射
            // 的事实（如 PRJ-LOCK-HELD 项目级事件 subject=∅——project 侧
            // 注释引 diagnostics §7.7 映射表"lock-held-by-other，subject=∅"
            // ），harness 修不了也不许虚构修补（补 subject＝伪造绑定对象，
            // 补 params＝伪造参数值）；但更不许让它杀死打开协议——PM-07
            // 第二写者降级只读是 open 的成功形态，attempt 1 的进程死亡
            // （std::terminate）正是缺失本纪律所致。处置＝Dev 通道具名
            // 落盘（消息含码/拒绝 token/工厂 detail/缺口归属）后返回——
            // 条目未入目录的事实可观测、可审计，不是静默吞。
            {
                std::string message;
                message += "对端用户级诊断未入目录（diagnostics 工厂拒绝）：code=";
                message += record.code;
                message += " rejection=";
                message += diagnostics::token(error.code());
                message += " detail=\"";
                message += error.what();
                message += "\"——跨单元契约缺口（发射面补 subject 或工厂校验"
                           "豁免面，裁决归 project/diagnostics 所有者——"
                           "WP-10-T15 验收 F-290 登记）；打开协议继续，"
                           "PM-07 降级语义不受阻";
                m_devLog->logDev("ird.harness.diagbridge", message);
            }
            return;
        }
        // 工厂错误码全表 11 值在上方两个分组穷举——不可达兜底仍按装配缺陷
        // 上抛（新增枚举值时在此显式归组，编译期由 -Wswitch 提示补行）。
        throw;
    }
}

void ProjectDiagnosticsBridge::reportDev(const std::string& channel,
                                         const std::string& message)
{
    // Dev 级事实不入目录（diagnostics §6.2——IDiagnosticSink::append 拒绝
    // Dev 码），直转开发日志通道。
    m_devLog->logDev(channel, message);
}

// =====================================================================
// SerialTaskExecutor（§3.4 落盘线程的开发期宿主——见 PortAdapters.hpp 类注释）
// =====================================================================

/// 执行器私有状态（互斥＋条件变量＋任务队列＋运行位——隔离平台线程头）。
struct SerialTaskExecutor::Impl {
    std::mutex mutex;                          ///< 队列与运行位的互斥（临界区极短）
    std::condition_variable cv;                ///< 任务到达/停止通知
    std::deque<std::function<void()>> queue;   ///< 待执行任务（FIFO＝串行语义）
    bool running = true;                       ///< 运行位（stop 置停；排空后收线程）
};

SerialTaskExecutor::SerialTaskExecutor()
    : m_impl(new Impl)
    , m_thread([this] { run(); })
{
}

SerialTaskExecutor::~SerialTaskExecutor()
{
    stop();
    delete m_impl;
}

void SerialTaskExecutor::post(std::function<void()> task)
{
    {
        std::lock_guard<std::mutex> lock(m_impl->mutex);
        m_impl->queue.push_back(std::move(task));
    }
    m_impl->cv.notify_one();
}

void SerialTaskExecutor::stop()
{
    {
        std::lock_guard<std::mutex> lock(m_impl->mutex);
        if (!m_impl->running) {
            return;  // 幂等（已置停——重复 stop 空操作）
        }
        m_impl->running = false;
    }
    m_impl->cv.notify_all();
    if (m_thread.joinable()) {
        m_thread.join();  // 有界排空：队列余量执行完（run 的取空即退）再收
    }
}

void SerialTaskExecutor::run()
{
    for (;;) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(m_impl->mutex);
            m_impl->cv.wait(lock, [this] {
                return !m_impl->queue.empty() || !m_impl->running;
            });
            if (m_impl->queue.empty()) {
                return;  // 已置停且排空——线程收尾（stop 的 join 返回）
            }
            task = std::move(m_impl->queue.front());
            m_impl->queue.pop_front();
        }
        // 落盘任务按契约不抛（对端返回值轨——DraftController 的磁盘段收敛
        // 纪律）；违约＝装配缺陷，fail-fast 不吞错（吞错会制造"以为保存了"
        // 的假象——AGENTS §3 错误语义）。
        task();
    }
}

// =====================================================================
// StorePortAdapter
// =====================================================================

namespace {

/**
 * @brief 关闭回调的类型翻译观察者（project::ICloseObserver → ui::
 *        IUiStoreCloseObserver）。
 *
 * 由 StorePortAdapter 持有（存活期覆盖 store 的订阅表——悬挂不可能）；
 * 翻译规则：onStoreClosed(store) → observer.onStoreClosed(store.projectId())
 * ——ui 侧只关心"哪个项目已释放"（INV-SES-3 持有点按项目身份释放）。
 */
class ForwardingCloseObserver final : public project::ICloseObserver {
public:
    /// @param observer [in] ui 侧观察者（弱引用语义——UiPorts.hpp 契约；
    ///             观察者须保证回调发生点存活，纪律由 UiSessionController
    ///             的成员订阅句柄承担）。
    explicit ForwardingCloseObserver(IUiStoreCloseObserver& observer)
        : m_observer(observer)
    {
    }

    /// @brief 存储上下文 Closed 后的回调翻译（回调内不得再触上下文方法
    ///        ——project 契约；本翻译只取身份，天然合规）。
    void onStoreClosed(project::ProjectStore& store) override
    {
        m_observer.onStoreClosed(store.projectId());
    }

private:
    /// ui 侧观察者（非 owning——见构造注释）。
    IUiStoreCloseObserver& m_observer;
};

}  // namespace

StorePortAdapter::StorePortAdapter(std::unique_ptr<project::ProjectStore> store,
                                   core::BranchId draftBranchAnchor)
    : m_store(std::move(store))
    , m_draftBranchAnchor(draftBranchAnchor)
{
}

StorePortAdapter::~StorePortAdapter() = default;

std::uint32_t StorePortAdapter::requestClose()
{
    // 幂等信号直转：拒绝新写＋返回在途引用数（归档会话/在途事务/草稿落盘
    // ——§5.7）；"不催促不跳过归档"由对端保证，适配器零加工。
    return m_store->requestClose();
}

bool StorePortAdapter::isClosed() const
{
    // §5.6 防线 4 的兜底轮询面（回调丢失时轮询 closed()）——ui 端口冻结名
    // isClosed 与对端 closed() 的命名差在本适配器内收敛（不改义）。
    return m_store->closed();
}

std::unique_ptr<core::IEventSubscription>
StorePortAdapter::subscribeClose(IUiStoreCloseObserver& observer)
{
    // 创建翻译观察者并由本适配器持有（析构序：观察者先于 store——store 的
    // 订阅表在 store 存活期内永远指向存活对象）。
    m_forwarders.push_back(std::make_unique<ForwardingCloseObserver>(observer));
    project::ICloseObserver& forwarder = *m_forwarders.back();
    m_store->subscribeClose(forwarder);
    // RAII 句柄按契约返回（v0.1＝无操作实现——NoopSubscription 注释）。
    return std::make_unique<NoopSubscription>();
}

// ---------------------------------------------------------------------
// StorePortAdapter——IUiDraftStorePort（C-5 写半区；UI-T17 增量）
// ---------------------------------------------------------------------

namespace {

/**
 * @brief ui 草稿来源词表 → project 冻结 token 对应枚举（两词表同为冻结
 *        三值 autosave/manual/apply-retained——PersistenceFormat.hpp §4.4.5；
 *        按名直映不按底层序号，防两枚举布局各自演化时静默错位）。
 */
project::DraftOrigin draftOriginToProject(ui::DraftOrigin origin)
{
    switch (origin) {
    case ui::DraftOrigin::Manual:        return project::DraftOrigin::Manual;
    case ui::DraftOrigin::ApplyRetained: return project::DraftOrigin::ApplyRetained;
    case ui::DraftOrigin::Autosave:      break;
    }
    return project::DraftOrigin::Autosave;
}

/**
 * @brief project 草稿来源枚举 → ui 词表（token 语义一致——tryLoad 回装的
 *        逆翻译；与 draftOriginToProject 同款按名直映）。
 */
ui::DraftOrigin draftOriginFromProject(project::DraftOrigin origin)
{
    switch (origin) {
    case project::DraftOrigin::Manual:        return ui::DraftOrigin::Manual;
    case project::DraftOrigin::ApplyRetained: return ui::DraftOrigin::ApplyRetained;
    case project::DraftOrigin::Autosave:      break;
    }
    return ui::DraftOrigin::Autosave;
}

}  // namespace

DraftSaveOutcome StorePortAdapter::save(const DraftDocumentProjection& document)
{
    // 值翻译（零加工）：schemaVersion 0＝"未填写保留值"（投影契约禁止落盘
    // 0——DraftController 侧保证；此处直传，对端校验链兜底）；payload 为域
    // canonical 字节原样透传（CR-02/D-10——适配器不解析）。分支由文档自身
    // 携带（branchId 字段），分支锚不消费。
    project::DraftDocument doc;
    doc.schemaVersion = static_cast<int>(document.schemaVersion);
    doc.projectId = document.projectId;
    doc.branchId = document.branchId;
    doc.moduleId = document.moduleId;
    doc.baseRevisionId = document.baseRevisionId;
    doc.payload = document.payload;
    doc.savedAtUtc = document.savedAtUtc;
    doc.origin = draftOriginToProject(document.origin);
    try {
        // 门卫（只读/关闭/失权拒绝）走对端返回值轨；门卫通过后在途票据由
        // 对端维护（requestClose 排空等待在途保存完成——§5.7 语义由对端
        // 保证，适配器零加工）。
        const project::SaveResult result = m_store->drafts().save(doc);
        DraftSaveOutcome out;
        out.ok = result.ok;
        if (!result.ok && result.error.has_value()) {
            out.errorToken = storeErrorToken(result.error->code());
            out.detail = result.error->what();
        }
        return out;
    } catch (const project::StoreError& error) {
        // 对端异常轨（invalid_argument 属控制器侧文档装配违约，不经此——
        // 调用方违约原样上抛；StoreError＝对端拒绝，折叠返回值轨）。
        DraftSaveOutcome out;
        out.ok = false;
        out.errorToken = storeErrorToken(error.code());
        out.detail = error.what();
        return out;
    }
}

DraftLoadOutcome StorePortAdapter::tryLoad(const std::string& moduleId)
{
    DraftLoadOutcome out;
    // 分支锚（诚实边界——类注释）：当前缺省值下对端按不存在的分支目录
    // 寻址＝Missing（"无草稿是常态不是错误"），行为有定义且不虚构。
    std::vector<core::DiagnosticRecord> diags;
    try {
        std::optional<project::DraftDocument> doc =
            m_store->drafts().tryLoad(m_draftBranchAnchor, moduleId, diags);
        if (doc.has_value()) {
            // 值回装（与 save 的翻译互逆——字段一一对应）。
            out.document.schemaVersion = static_cast<std::uint32_t>(doc->schemaVersion);
            out.document.projectId = doc->projectId;
            out.document.branchId = doc->branchId;
            out.document.moduleId = doc->moduleId;
            out.document.baseRevisionId = doc->baseRevisionId;
            out.document.payload = doc->payload;
            out.document.savedAtUtc = doc->savedAtUtc;
            out.document.origin = draftOriginFromProject(doc->origin);
            // 有文档＋伴随诊断＝current 损坏经 .bak 回退（对端把损坏记录
            // 随 diags 带回——PM-08 的 RecoveredFromBackup 判别锚）。
            out.status = diags.empty() ? DraftLoadOutcome::Status::Loaded
                                       : DraftLoadOutcome::Status::RecoveredFromBackup;
            out.newResidueDropped = false;
        } else {
            // 无文档：无诊断＝Missing（常态）；有诊断＝Corrupt（损坏 token
            // 取 §4.4.8 词表 draft-corrupt——对端 DraftCorrupt 码的稳定串；
            // 诊断明细计数进 Dev 面 detail，条目转发归草稿链路完整装配任务
            // ——UI-T17 立项登记注③诚实边界）。
            if (diags.empty()) {
                out.status = DraftLoadOutcome::Status::Missing;
            } else {
                out.status = DraftLoadOutcome::Status::Corrupt;
                out.errorToken = "draft-corrupt";
                out.detail = "project/draft: tryLoad 损坏且 .bak 不可用（伴随诊断 "
                             + std::to_string(diags.size()) + " 条）";
            }
        }
    } catch (const project::StoreError& error) {
        // 上下文已关闭等对端拒绝——折叠 Corrupt＋稳定 token（Draining 排空
        // 期对端可用，Closed 后的迟到读取按拒绝呈现，不虚构 Missing）。
        out.status = DraftLoadOutcome::Status::Corrupt;
        out.errorToken = storeErrorToken(error.code());
        out.detail = error.what();
    }
    return out;
}

DraftDiscardOutcome StorePortAdapter::discard(const std::string& moduleId)
{
    DraftDiscardOutcome out;
    try {
        // 幂等放弃直转（"本来就没有也是 ok"语义由对端保证）；分支锚语义
        // 同 tryLoad（类注释诚实边界）。
        const project::DiscardResult result =
            m_store->drafts().discard(m_draftBranchAnchor, moduleId);
        out.ok = result.ok;
        if (!result.ok && result.error.has_value()) {
            out.errorToken = storeErrorToken(result.error->code());
            out.detail = result.error->what();
        }
    } catch (const project::StoreError& error) {
        out.ok = false;
        out.errorToken = storeErrorToken(error.code());
        out.detail = error.what();
    }
    return out;
}

// =====================================================================
// StoreFactoryPortAdapter
// =====================================================================

StoreFactoryPortAdapter::StoreFactoryPortAdapter(ProjectDiagnosticsBridge& bridge)
    : m_bridge(bridge)
{
}

OpenStoreOutcome StoreFactoryPortAdapter::open(const std::string& canonicalPath,
                                               UiOpenMode mode,
                                               SessionPortBundle& outBindings)
{
    OpenStoreOutcome outcome;

    // 第一步：路径规范化（§9.3 口径——失败错误页"定位具体文件"的数据源；
    // 规范化失败的输入路径原样回填，错误页仍可定位）。
    std::filesystem::path path;
    try {
        path = std::filesystem::weakly_canonical(
            std::filesystem::u8path(canonicalPath));
    } catch (const std::filesystem::filesystem_error&) {
        outcome.ok = false;
        outcome.failure.errorCodeToken = "not-a-project";
        outcome.failure.detail = "project/open: 路径规范化失败（weakly_canonical 异常）";
        outcome.failure.projectPath = canonicalPath;
        return outcome;
    }

    // 第二步：执行打开五步协议（①兜底校验→②形态/版本/锁→③读校验→⑤孤儿
    // 草稿扫描全在对端）。诊断桥注入——协议内 PRJ-* 用户级诊断经桥上报
    // （工厂接受的条目入目录；被工厂拒绝的按桥的显式纪律具名落 Dev 日志，
    // 打开协议不受阻——PortAdapters.hpp 桥类注释）。
    project::OpenStoreRequest request;
    request.path = path;
    request.mode = (mode == UiOpenMode::Writable) ? project::OpenMode::Writable
                                                  : project::OpenMode::ReadOnly;
    request.diagnostics = &m_bridge;

    try {
        project::OpenStoreResult result = project::ProjectStoreFactory::open(request);

        // 权威元数据先取（store 的 unique_ptr 移入端口前——打开③步已校验
        // projectId 与 project.json/HEAD 一致；displayName 来自 M0 权威元
        // 数据，零虚构）。
        core::ProjectId projectId = result.store->projectId();
        std::string displayName;
        {
            const project::ProjectMetadataView view =
                result.store->query().currentMetadata();
            displayName = view.record.projectDisplayName;
        }

        // 成功路径（含降级只读——PM-07：降级不是失败）：装配会话端口绑定。
        // store 端口接管 unique_ptr＝ui 经 shared 持有存储上下文保活引用
        // （INV-SES-3）；drafts/tasks 为 v0.1 桩（诚实边界见各桩类注释）。
        std::shared_ptr<StorePortAdapter> storePort =
            std::make_shared<StorePortAdapter>(std::move(result.store));

        outcome.ok = true;
        outcome.opened.metadata.projectId = projectId;
        outcome.opened.metadata.projectDisplayName = std::move(displayName);
        outcome.opened.metadata.writable = result.writable;

        // 只读归因（PM-07 横幅判别输入）：锁被具体持有者占据→LockHeld
        // （横幅含 PID）；否则为介质/权限族——打开结果不携带原始错误码，
        // v0.1 以 MediaReadOnly 归并呈现（诚实边界：介质只读与权限不足
        // 的事后不可区分是协议形态使然，精确归因以目录内 PRJ-* 诊断为
        // 准——bridge 已把诊断送入目录）。
        if (!result.writable) {
            if (!result.lockInfo.isSelf && result.lockInfo.holder.pid != 0) {
                outcome.opened.readOnlyCause = ReadOnlyOpenCause::LockHeld;
                outcome.opened.lockHolder = LockHolderProjection{};
                outcome.opened.lockHolder->pid = result.lockInfo.holder.pid;
                outcome.opened.lockHolder->host = result.lockInfo.holder.host;
            } else {
                outcome.opened.readOnlyCause = ReadOnlyOpenCause::MediaReadOnly;
            }
        }

        // 端口绑定集出参（"失败＝无绑定泄漏在签名上自明"——成功才写）。
        outBindings.store = std::move(storePort);
        outBindings.drafts = std::make_shared<DraftQueryPortStub>();
        outBindings.tasks = std::make_shared<SessionTaskPortStub>();
        return outcome;
    } catch (const project::StoreError& error) {
        // 失败路径：稳定 token＋detail 原样折叠（"错误页定位具体文件；
        // 当前项目不动"由控制器保证——适配器只做值翻译，不吞错不改义）。
        outcome.ok = false;
        outcome.failure.errorCodeToken = storeErrorToken(error.code());
        outcome.failure.detail = error.what();
        outcome.failure.projectPath = std::filesystem::path(path).u8string();
        return outcome;
    }
}

// =====================================================================
// v0.1 桩端口实现（恒值——诚实边界见 PortAdapters.hpp 各类注释）
// =====================================================================

std::vector<DraftRowProjection> DraftQueryPortStub::listDrafts() const
{
    return {};
}

std::vector<TaskRowProjection>
SessionTaskPortStub::nonTerminalTasks(const core::ProjectId&) const
{
    return {};
}

bool SessionTaskPortStub::requestCancel(const core::TaskIdentity&)
{
    // 无任务可作用＝对端拒绝同款语义（false＝未受理）。
    return false;
}

bool SessionTaskPortStub::requestForceTerminate(const core::TaskIdentity&)
{
    return false;
}

PolicySummaryProjection UnloadedPolicySource::summary() const
{
    // 默认构造即 available=false（"策略未装载"的投影语义）。
    return {};
}

std::optional<std::string> NullUiNameResolver::resolveObjectId(core::ObjectId) const
{
    return std::nullopt;
}

std::vector<PluginAssemblyReport> HarnessAboutSource::assemblyReports() const
{
    return {};
}

AboutVersionBaseline HarnessAboutSource::versionBaseline() const
{
    // 默认构造即 available=false（WP-24-T01 基线未产出的占位形态）。
    return {};
}

}  // namespace app
}  // namespace ui
}  // namespace ird
