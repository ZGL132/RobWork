/**
 * @file   DiagnosticPresentation.cpp
 * @brief  诊断呈现模型实现（UI-T13——§9.1/§10.8 的落位承载）。
 *
 * 设计依据：
 *   - units/ui.md §9.1（呈现映射/动作映射表/三要素同显/恢复横幅/日志
 *     面板/脱敏纪律）、§10.8（IDiagnosticPresentationModel 接口契约表——
 *     本实现即其行为自证面）、§3.4（M-1 Marshal 纪律：目录通知线程→
 *     UI 线程排队投递）、§3.5（文案键唯一出口 UiText）；
 *   - diagnostics.md §9.7（snapshot 过滤/稳定序/去重计数语义——模型零
 *     再加工）、§4.4（actionKind 词表）、§6.4（同键折叠＝occurrences）；
 *   - 需求 UX-03/PM-15/NFR-REL-05/NFR-SEC-07（见头文件逐项锚点）。
 *
 * 实现口径登记（ui.md §16.7 v1.5 同步）：
 *   - expandChain 用**全量快照**建 entryId→条目索引（DiagQuery 全空——
 *     原因链不得被表过滤器截断），沿 causedBy 单父链上溯，visited 集
 *     防御性截断（目录构造期 DAG 性质下不会触发——纯防御）；
 *   - subscribe 的 Marshal 经构造线程建立的 QObject 上下文排队投递
 *     （QMetaObject::invokeMethod 函子重载——无需 Q_OBJECT，零 AUTOMOC
 *     维持）；退订置丢弃位，在途排队通知被安全丢弃（悬垂防御）。
 */

#include <sdurws/ird/ui/IDiagnosticPresentationModel.hpp>

#include <sdurws/ird/ui/UiPorts.hpp>   // IUiFindingQueryPort/IUiUserLogSource（依赖端口完整定义——值面取用）
#include <sdurws/ird/ui/UiText.hpp>    // resolveText/notApplicableText/ensureNoInternalIdentity（§3.5 唯一出口）

#include <QMetaObject>
#include <QObject>

#include <exception>
#include <locale>
#include <map>
#include <sstream>
#include <stdexcept>

namespace sdurws::ird {
namespace ui {

// =====================================================================
// §9.1 动作映射表（ui 冻结呈现层映射——唯一权威实现点）
// =====================================================================

DiagAction diagActionFor(const std::string& actionKind)
{
    // §9.1 动作行逐条映射；token 词表＝diagnostics §4.4 actionKindToken
    // 的产出集（小写连字符）。未知 token→None：词表演进（§4.4 修订新增
    // 动作族）先于映射表登记到达时的安全兜底——不虚构动作、不报错
    // （呈现层只少一个按钮，不渲染错误语义）。
    DiagAction action;
    if (actionKind == "fix-input") {
        action.kind = DiagActionKind::NavigateToEdit;      // 跳转编辑（§9.1 原文）
    } else if (actionKind == "contact-holder") {
        action.kind = DiagActionKind::ShowLockHolder;      // 显示 PID（§9.1 原文——PM-07 横幅动作）
    } else if (actionKind == "retry-readonly") {
        action.kind = DiagActionKind::ShowLockHolder;      // 只读重试→只读横幅呈现（§4.4 PermissionOrLock 首动作——横幅含持有者 PID 与只读成因，PM-07）
    } else if (actionKind == "rerun-interrupted") {
        action.kind = DiagActionKind::OpenRerunEntry;      // 重跑入口（§9.1 原文——NFR-REL-03）
    } else if (actionKind == "confirm-or-fix") {
        action.kind = DiagActionKind::OpenConfirmDialog;   // 确认对话（§9.1 原文——SA-15，§9.2 Bridge 承载）
    } else if (actionKind == "inspect-log") {
        action.kind = DiagActionKind::OpenLogPanel;        // 日志页（§9.1 原文）
    } else if (actionKind == "adjust-policy") {
        action.kind = DiagActionKind::OpenPolicyEntry;     // 策略入口（§6.7 摘要卡路径说明）
    } else if (actionKind == "open-upgrade-guide") {
        action.kind = DiagActionKind::OpenUpgradeGuide;    // 升级指引（PM-06）
    } else if (actionKind == "relink") {
        action.kind = DiagActionKind::RelinkResource;      // 重链资源流程入口（PM-09）
    } else if (actionKind == "supply-evidence") {
        action.kind = DiagActionKind::SupplyEvidence;      // 证据清单（§4.4 表 2④ 全量列出）
    } else if (actionKind == "review-proof") {
        action.kind = DiagActionKind::ReviewProof;         // 评审记录（RPT-05）
    } else if (actionKind == "report-bug") {
        action.kind = DiagActionKind::ReportBug;           // 开发日志（internal 类出线）
    } else if (actionKind == "inspect-resource") {
        action.kind = DiagActionKind::InspectResource;     // 资源检视（NFR-SEC-01~03）
    } else if (actionKind == "retry-task") {
        action.kind = DiagActionKind::OpenTaskPanel;       // 失败态重试入口（任务面板——§9.4 重跑/重试所在面）
    } else if (actionKind == "none") {
        action.kind = DiagActionKind::None;                // 无动作（UX-03——正常取消无错误呈现）
    } else {
        action.kind = DiagActionKind::None;                // 未知族兜底（见函数头注释）
    }
    return action;
}

DiagPresentationRoute diagnosticRouteFor(diagnostics::DiagnosticCategory category)
{
    // §9.1"呈现映射"段逐行落地（分类→去向）。ui 不改分类/严重级别
    // （N-7）：本函数只决定诊断放进哪个呈现面。
    switch (category) {
    case diagnostics::DiagnosticCategory::InputInvalid:
        // "输入非法→失败态＋对象定位"（§9.1 原文）。
        return DiagPresentationRoute::FailureWithObjectFocus;
    case diagnostics::DiagnosticCategory::PermissionOrLock:
        // "权限或锁→只读横幅"（§9.1 原文——PM-07）。
        return DiagPresentationRoute::ReadOnlyBanner;
    case diagnostics::DiagnosticCategory::Confirmable:
        // "可确认→确认对话"（§9.1 原文——SA-15）。
        return DiagPresentationRoute::ConfirmDialog;
    case diagnostics::DiagnosticCategory::Canceled:
        // "取消→无错误呈现"（§9.1 原文——UX-03 正常用户取消无错误）。
        return DiagPresentationRoute::NoErrorPresentation;
    case diagnostics::DiagnosticCategory::Interrupted:
        // "中断→'已中断'可重跑"（§9.1 原文——NFR-REL-03）。
        return DiagPresentationRoute::InterruptedRerunnable;
    case diagnostics::DiagnosticCategory::Internal:
        // "内部→开发日志/恢复横幅"（§9.1 原文——不入用户可执行面）。
        return DiagPresentationRoute::DevLogOrRecoveryBanner;
    case diagnostics::DiagnosticCategory::InfeasibilityProof:
        // 不可行证明＝有效工程结论（Info、RPT-05 评审记录）——非错误
        // 呈现面，与取消同路由（不渲染错误语义）。
        return DiagPresentationRoute::NoErrorPresentation;
    case diagnostics::DiagnosticCategory::FormatOrVersion:
    case diagnostics::DiagnosticCategory::ResourceMissing:
    case diagnostics::DiagnosticCategory::PolicyDenied:
    case diagnostics::DiagnosticCategory::ExecutionFailed:
    case diagnostics::DiagnosticCategory::Timeout:
    case diagnostics::DiagnosticCategory::DataInsufficient:
    case diagnostics::DiagnosticCategory::EvidenceMissing:
    case diagnostics::DiagnosticCategory::SecurityOrRedaction:
        // 其余阻断/缺失/安全类统一失败态呈现（对象定位由条目 subject/
        // localName 携带；动作建议由 diagActionFor 按 actionKind 给出）。
        return DiagPresentationRoute::FailureWithObjectFocus;
    }
    // 全枚举不可达；保守取失败态（新增枚举值未映射时不静默丢弃）。
    return DiagPresentationRoute::FailureWithObjectFocus;
}

// =====================================================================
// §9.1 比较型三要素呈现（数值＋单位同显；四态占位不伪造数值）
// =====================================================================

ComparisonValueText formatComparisonValue(const core::ComparativeValue& value)
{
    ComparisonValueText out;
    out.state = value.quantity.state();
    switch (out.state) {
    case core::FieldState::Provided: {
        // Provided：数值＋单位同显（KIN-12 纪律——数值与单位一串呈现，
        // 不拆列不拆词）。数值格式＝6 位有效数字 general（与 UiText::
        // formatQuantityText 的格式约定一致——同会话同一数值面孔）；
        // locale 无关（经典 "C" locale 摩面——小数点恒为 '.'）。
        const double number = value.quantity.value();
        std::ostringstream text;
        text.imbue(std::locale::classic());
        text.precision(6);
        text << number;
        out.text = text.str();
        if (value.unit.isValid()) {
            // 单位 token 原文直显（冻结注册表词表——core Units 权威）；
            // 无效句柄（构造缺省——数据面违约）降级为仅数值：呈现层不
            // 补救不虚构单位名，Dev 排障由产出侧负责。
            out.text += ' ';
            out.text += value.unit.symbol();
        }
        break;
    }
    case core::FieldState::NotApplicable:
        // 「不适用」显式占位（ERR-01——不伪造数值；UiText 唯一出口）。
        out.text = notApplicableText();
        break;
    case core::FieldState::Invalid:
        // 「无效（原串）」——保留原文（NFR-COR-03 不得静默转 0）；
        // 原串经 resolveText 参数路径的 ensureNoInternalIdentity 守卫
        // （哈希形态拒绝——UX-02 红线在呈现边界执行）。
        out.text = resolveText("ui.diag.comparison.invalid",
                               {value.quantity.invalidRawInput()});
        break;
    case core::FieldState::NotProvided:
        // 「未提供」——MDL-06 降级语义的事实呈现（与"不适用"区分：
        // 未提供是缺数据，不适用是该字段无意义）。
        out.text = resolveText("ui.diag.comparison.not-provided");
        break;
    }
    return out;
}

ComparisonText formatComparison(const core::ComparativeFields& comparison)
{
    ComparisonText out;
    out.actual = formatComparisonValue(comparison.actual);
    out.expected = formatComparisonValue(comparison.expected);
    return out;
}

// =====================================================================
// §9.1 恢复横幅（PM-15）与 Tier-U 日志面板装配
// =====================================================================

SessionRecoveryBannerProjection
assembleRecoveryBanner(bool ignoredSaves, bool interruptedTasks,
                       std::size_t orphanDraftCount)
{
    SessionRecoveryBannerProjection banner;
    banner.orphanDraftCount = orphanDraftCount;
    banner.actionDetailsKey = "ui.recovery.banner.action.details";
    banner.actionRestoreKey = "ui.recovery.banner.action.restore";
    banner.actionDiscardKey = "ui.recovery.banner.action.discard";

    // 三事实皆空→any=false（调用方不渲染——不虚构恢复叙事）。
    banner.any = ignoredSaves || interruptedTasks || orphanDraftCount > 0;
    if (!banner.any) {
        return banner;
    }

    // 一句话主文案＝按行动紧迫度择一（忽略保存＞中断任务＞未保存草稿），
    // 其余事实降级为详情行——横幅只一句话汇总（PM-15 原文），不堆叠。
    if (ignoredSaves) {
        banner.summaryKey = "ui.recovery.banner.summary.ignored-saves";
        if (interruptedTasks) {
            banner.detailKeys.push_back("ui.recovery.banner.summary.interrupted");
        }
        if (orphanDraftCount > 0) {
            banner.detailKeys.push_back("ui.recovery.banner.summary.orphan-drafts");
        }
    } else if (interruptedTasks) {
        banner.summaryKey = "ui.recovery.banner.summary.interrupted";
        if (orphanDraftCount > 0) {
            banner.detailKeys.push_back("ui.recovery.banner.summary.orphan-drafts");
        }
    } else {
        // 主文案＝草稿计数行（含 {0} 占位——调用方以 orphanDraftCount
        // 渲染参数；此处把参数面随投影携带，渲染归 GUI）。
        banner.summaryKey = "ui.recovery.banner.summary.orphan-drafts";
        banner.summaryArgs.push_back(std::to_string(orphanDraftCount));
    }
    return banner;
}

UserLogPanelSnapshot assembleUserLogPanel(const IUiUserLogSource* source)
{
    UserLogPanelSnapshot panel;
    panel.emptyPanelKey = "ui.logpanel.empty";
    panel.devJumpLabelKey = "ui.logpanel.dev.jump";
    if (source == nullptr) {
        // 无数据源＝空面板＋无跳转（不虚构条目/路径——§11.4 零虚构同案）。
        return panel;
    }
    panel.entries = source->snapshot();
    panel.devFileJumpAvailable = source->devLogFileAvailable();
    return panel;
}

// =====================================================================
// §10.8 模型实现
// =====================================================================

namespace {

/// 目录通知的 Marshal 通道 token（§3.5 "diag/<域>" 命名族的 ui 消费面）。
constexpr std::string_view kDiagPresentationDevChannel = "diag/ui-presentation";

/**
 * @brief UI 线程投递上下文（构造线程建立——Marshal 纪律 M-1 的锚点）。
 *
 * 为什么需要 QObject：QMetaObject::invokeMethod 的函子重载以 QObject*
 * 决定投递目标线程——上下文在本模型构造线程（UI 线程）创建，排队调用
 * 即在该线程执行。零 Q_OBJECT（不开 AUTOMOC——CMakeLists 既有登记口径，
 * 函子重载不需要元对象声明）。
 */
class MarshalContext final : public QObject
{
public:
    MarshalContext() = default;
    ~MarshalContext() override = default;
};

/**
 * @brief 目录订阅的 Marshal 适配器（目录通知线程→UI 线程排队投递）。
 *
 * 生命周期与悬垂防御：alive 位（shared_ptr 持有）在订阅句柄析构/退订
 * 时置 false——在途排队通知检查该位后丢弃，lambda 持有的只是 shared
 * 控制块与观察者裸指针，不会触碰已销毁的适配器（观察者引用的生存期
 * 契约由调用方保证：观察者析构前先退订——§10.8 前置行）。
 */
class MarshaledDiagObserver final : public diagnostics::IDiagObserver
{
public:
    MarshaledDiagObserver(MarshalContext* context, diagnostics::IDiagObserver& target)
        : m_context(context)
        , m_target(&target)
        , m_alive(std::make_shared<bool>(true))
    {
    }

    /// 退订/析构前置：置丢弃位（任意线程——目录通知线程可能正在派发）。
    void drop() noexcept
    {
        *m_alive = false;
    }

    void onCatalogChanged() override
    {
        // 目录通知线程：排队投递到 UI 线程（§3.4 M-1——通知无载荷，
        // 订阅方经 query 拉取，排队即最小搬运）。
        const auto alive = m_alive;
        auto* target = m_target;
        QMetaObject::invokeMethod(
            m_context,
            [alive, target] {
                if (*alive) {
                    target->onCatalogChanged();   // UI 线程分发（§10.8 线程行）
                }
            },
            Qt::QueuedConnection);
    }

private:
    MarshalContext* m_context;                 ///< 投递上下文（非 owning；模型内成员）
    diagnostics::IDiagObserver* m_target;      ///< 上层观察者（非 owning——生存期契约见类注释）
    std::shared_ptr<bool> m_alive;             ///< 丢弃位（与在途 lambda 共享——悬垂防御）
};

/// 订阅句柄具体类型定义于 DiagnosticPresentationModel 内部（私有嵌套——
/// 适配器以 shared 持有，句柄析构/退订先 drop 分发再退订底层目录）。

/**
 * @brief 模型实现（§10.8 五方法——行为契约逐条见头文件）。
 *
 * 线程约束：公共方法 UI 线程（sink 快照查询短临界区——NFR-PERF-01）；
 * 目录订阅回调经 MarshaledDiagObserver 转 UI 线程。
 */
class DiagnosticPresentationModel final : public IDiagnosticPresentationModel
{
public:
    explicit DiagnosticPresentationModel(DiagnosticPresentationDeps deps)
        : m_deps(std::move(deps))
    {
        // 上下文在构造线程建立——构造契约＝UI 线程（工厂注释）。
        m_context = std::make_unique<MarshalContext>();
    }

    // ---- query（§10.8：过滤/去重/稳定序全在 sink——透传值拷贝）----
    std::vector<diagnostics::DiagProjectionItem>
    query(const diagnostics::DiagQuery& query) const override
    {
        try {
            // sink.snapshot 异常穿透面（§10.8 错误类型行"快照失败→空集＋
            // Dev 日志，无异常穿透"——目录实现标称不抛，防御性收口：
            // 呈现路径的任何意外都不该击穿 UI 线程）。
            return m_deps.sink->snapshot(query);
        } catch (const std::exception& error) {
            logDevFailure(std::string("snapshot 失败：") + error.what());
            return {};
        } catch (...) {
            logDevFailure("snapshot 失败：未知异常");
            return {};
        }
    }

    // ---- expandChain（§10.8：causedBy 单父 DAG 逐级上溯）----
    std::vector<diagnostics::DiagProjectionItem>
    expandChain(std::uint64_t entryId) const override
    {
        // 第 1 步：全量快照建索引（DiagQuery 全空——原因链不得被诊断表
        // 的过滤器截断；条目数＝目录容量上界内，索引构建在 UI 线程预算内
        // ——§9.7 快照为值拷贝，本查询低频〔详情页打开时〕）。
        std::vector<diagnostics::DiagProjectionItem> snapshot;
        try {
            snapshot = m_deps.sink->snapshot(diagnostics::DiagQuery{});
        } catch (const std::exception& error) {
            logDevFailure(std::string("expandChain 快照失败：") + error.what());
            return {};
        } catch (...) {
            logDevFailure("expandChain 快照失败：未知异常");
            return {};
        }
        std::map<diagnostics::DiagEntryId, const diagnostics::DiagProjectionItem*> index;
        for (const auto& item : snapshot) {
            index.emplace(item.entryId, &item);
        }

        // 第 2 步：起始条目必须存在（不存在→空集——不虚构链）。
        const auto start = index.find(entryId);
        if (start == index.end()) {
            return {};
        }

        // 第 3 步：求链身份序列。注入链查询缝（L5 侧经 diagnostics
        // CauseLink::chainOf 供给——起点→根因，§16.7 v1.5 单侧冻结）时
        // 按链序装配；未注入（装配缺省）＝退化"起点自身即根"（§6.3
        // rootOf 语义的退化形态——不虚构链，缺口随缝登记）。缝实现抛出
        // （CauseLink 严格契约：断链/环 Usage）按 §10.8 错误类型行收口
        // ——空集＋Dev 日志，无异常穿透。
        std::vector<diagnostics::DiagEntryId> chainIds{entryId};
        if (m_deps.chainOf != nullptr) {
            try {
                auto fullChain = m_deps.chainOf(entryId);
                if (!fullChain.empty()) {
                    chainIds = std::move(fullChain);
                }
            } catch (const std::exception& error) {
                logDevFailure(std::string("expandChain 链查询失败：") + error.what());
                return {};
            } catch (...) {
                logDevFailure("expandChain 链查询失败：未知异常");
                return {};
            }
        }

        // 第 4 步：身份序列→投影值拷贝（链上条目若已被容量淘汰则跳过
        // ——链到此为止的如实呈现，不虚构缺失环节）。
        std::vector<diagnostics::DiagProjectionItem> chain;
        chain.reserve(chainIds.size());
        for (const auto id : chainIds) {
            const auto it = index.find(id);
            if (it != index.end()) {
                chain.push_back(*it->second);
            }
        }
        return chain;
    }

    // ---- pendingConfirmations（§9.2"经 project 间接读"——端口转发）----
    std::vector<diagnostics::FindingRecord> pendingConfirmations() const override
    {
        if (m_deps.findingQuery == nullptr) {
            return {};   // 无确认呈现面（装配显式声明）——空集，不虚构
        }
        return m_deps.findingQuery->pendingConfirmations();
    }

    // ---- subscribe（§10.8：目录变更→Marshal→UI 线程分发）----
    std::unique_ptr<core::IEventSubscription>
    subscribe(diagnostics::IDiagObserver& observer) override
    {
        // 适配器与句柄分离生存期：适配器由句柄 shared 持有（句柄析构→
        // drop＋底层退订）；在途排队 lambda 只捕获 alive 控制块——即使
        // 句柄已析构也不触碰适配器（悬垂防御见 MarshaledDiagObserver）。
        auto adapter = std::make_shared<MarshaledDiagObserver>(m_context.get(), observer);
        auto inner = m_deps.sink->subscribe(*adapter);
        return std::unique_ptr<core::IEventSubscription>(
            new DiagPresentationSubscriptionHandle(std::move(inner), std::move(adapter)));
    }

    // ---- redactedPath（§10.8：IRedactionService 只读转发——不加工）----
    std::string redactedPath(std::string_view raw) const override
    {
        // 服务为必注入依赖（工厂校验）——此处直转发（noexcept 契约：
        // redactPath 绝不抛，Redaction.hpp）。
        return m_deps.redaction->redactPath(raw);
    }

private:
    /// 句柄具体类型（成员含 shared 适配器——drop 后底层退订，适配器
    /// 随句柄析构；在途 lambda 凭 alive 位安全丢弃）。
    class DiagPresentationSubscriptionHandle final : public core::IEventSubscription
    {
    public:
        DiagPresentationSubscriptionHandle(
            std::unique_ptr<diagnostics::ISubscription> inner,
            std::shared_ptr<MarshaledDiagObserver> adapter)
            : m_inner(std::move(inner))
            , m_adapter(std::move(adapter))
        {
        }

        ~DiagPresentationSubscriptionHandle() override
        {
            if (m_adapter) {
                m_adapter->drop();
            }
        }
        DiagPresentationSubscriptionHandle(const DiagPresentationSubscriptionHandle&) = delete;
        DiagPresentationSubscriptionHandle&
        operator=(const DiagPresentationSubscriptionHandle&) = delete;

        void unsubscribe() override
        {
            if (m_adapter) {
                m_adapter->drop();     // 先停分发——在途排队通知被丢弃
                m_adapter.reset();
            }
            m_inner.reset();           // 底层目录退订（RAII）
        }

    private:
        std::unique_ptr<diagnostics::ISubscription> m_inner;
        std::shared_ptr<MarshaledDiagObserver> m_adapter;
    };

    /// Dev 日志出线（可空——静默跳过；快照失败事实不进用户目录——
    /// §10.8 错误类型行"空集＋Dev 日志"）。
    void logDevFailure(const std::string& message) const
    {
        if (m_deps.devLog != nullptr) {
            m_deps.devLog->logDev(kDiagPresentationDevChannel, message);
        }
    }

    DiagnosticPresentationDeps m_deps;                 ///< 装配依赖（非 owning）
    std::unique_ptr<MarshalContext> m_context;         ///< Marshal 上下文（构造线程＝UI 线程）
};

}  // namespace

std::unique_ptr<IDiagnosticPresentationModel>
createDiagnosticPresentationModel(DiagnosticPresentationDeps deps)
{
    // 装配契约 fail-fast（§2.3 错误语义：调用方错误——缺目录/缺脱敏服务
    // 的模型不可交付：前者无数据源，后者 redactedPath 会呈现原文路径
    // 违 NFR-SEC-07——宁构造失败不降级泄漏）。
    if (deps.sink == nullptr) {
        throw std::invalid_argument(
            "ui/diag-presentation/deps: sink 为空——诊断呈现模型必注入诊断目录只读投影面");
    }
    if (deps.redaction == nullptr) {
        throw std::invalid_argument(
            "ui/diag-presentation/deps: redaction 为空——路径脱敏转发必注入 IRedactionService"
            "（NFR-SEC-07：无服务不得呈现原文路径）");
    }
    return std::make_unique<DiagnosticPresentationModel>(std::move(deps));
}

}  // namespace ui
}  // namespace ird
