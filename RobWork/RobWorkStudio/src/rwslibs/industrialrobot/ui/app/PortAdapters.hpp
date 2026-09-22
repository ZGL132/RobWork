/**
 * @file   PortAdapters.hpp
 * @brief  工作台验证 harness（sdurws_ird_ui_app）的端口适配器集——L5 装配层
 *         雏形的"对端适配 ui 自有端口"单行适配面。
 *
 * 设计依据：
 *   - units/ui.md §10.1（ShellWiring 注入包）/§10.5（UiSessionControllerDeps）
 *     /§3.1（O-31 裁决：对端类型不进 ui 头，L5 装配器同时看见两边并写适配
 *     器）；本文件即该裁决所述"L5 装配器"的开发期载体（任务 UI-T15）；
 *   - ui/include/sdurws/ird/ui/UiPorts.hpp 各端口方法注释的"语义冻结（不
 *     改义）"行——每个适配器的职责都是**原样翻译**对端语义，不改写、不吞
 *     错、不虚构（UX-02 零内部名/零虚构纪律在装配侧同样成立）；
 *   - 对端契约冻结基准：project.md §5.1（打开/关闭协议）、§5.4（草稿服务）；
 *     policy.md §9.1（IPolicyProvider）；runtime.md §7.3（名称解析）。
 *
 * 背景说明（为什么这些适配器不属于任何产品单元）：
 *   产品面（ui 库本身）对 project/policy/runtime 零链接零 include（ARCH
 *   §3.5 白名单只有 ui->core、ui->diagnostics 两条边）；而"打开工程"这条
 *   端到端链路必须有人把 project::ProjectStoreFactory 翻译成
 *   ui::IUiStoreFactoryPort——这个"人"就是 L5 应用壳（装配层）。产品装配
 *   层随插件装配任务落位前，本 harness 是它的开发期替身：开发者在建模
 *   （WP-13+）开发期间用本 harness 以真实端口链路交互验证平台逻辑。
 *
 *   适配器全部为 harness 私有（app/ 目录，不进 include/ 公共头——R-2）；
 *   生命周期：所有权在 HarnessMain 装配序列，经 shared_ptr 交给 ui 消费方
 *   （壳/会话控制器只持共享引用——§10.1 所有权行）。
 *
 * 线程约束（对齐 UiPorts.hpp 头注）：ui 侧对本组端口的调用一律发生在 UI
 *   线程（§3.4 M-1）；IUiDraftStorePort 类的"后台落盘线程"纪律在 v0.1 不
 *   适用（harness 未装配 DraftController，见该桩类注释）。
 */

#ifndef SDURWS_IRD_UI_APP_PORT_ADAPTERS_HPP
#define SDURWS_IRD_UI_APP_PORT_ADAPTERS_HPP

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <sdurws/ird/core/Events.hpp>             // core::IEventSubscription（subscribeClose 句柄）
#include <sdurws/ird/diagnostics/Catalog.hpp>     // diagnostics::IDiagnosticSink（桥的下游）
#include <sdurws/ird/diagnostics/Factory.hpp>     // diagnostics::IDiagnosticFactory（create 唯一入口）
#include <sdurws/ird/diagnostics/Logging.hpp>     // diagnostics::IDevLogSink（Dev 通道下游）
#include <sdurws/ird/project/ProjectStore.hpp>    // project::ProjectStore/ProjectStoreFactory（被适配对端）
#include <sdurws/ird/project/StoreTypes.hpp>      // project::IDiagnosticsSink/StoreError（对端注入面）
#include <sdurws/ird/ui/UiPorts.hpp>              // ui 自有端口（被实现面）
#include <sdurws/ird/ui/UiProjections.hpp>        // ui 值投影（翻译目标）

namespace sdurws::ird {
namespace ui {
namespace app {

// =====================================================================
// ProjectDiagnosticsBridge——project 自有诊断注入面 → diagnostics 管线
// =====================================================================

/**
 * @brief project::IDiagnosticsSink 的 diagnostics 侧适配器（P-PR-6"注入式
 *        先行"的 L5 侧落点）。
 *
 * 为什么需要本桥：project 打开协议（§5.1⑤）产出用户级诊断（PRJ-LOCK-HELD、
 * PRJ-RECOVERY-ORPHAN-DRAFT 等）经自有接口 project::IDiagnosticsSink 上报，
 * 而 project 零链接 diagnostics（架构红线——SA 同款注入纪律）；两套接口的
 * 对接只能在同时看见两边的装配层完成。翻译规则（不改义）：
 *   - report(record)：经 diagnostics::IDiagnosticFactory::create 唯一入口
 *     产条目入目录（§9.2），上下文＝对端宿主标识（sourceUnit="project"——
 *     DiagnosticsSinkImpl §9.7"无上下文时 sourceUnit 取注入的宿主标识"
 *     同款；params 保持空——record 无参数字段，见实现注释）。create 拒绝
 *     分两类纪律（WP-10-T15 验收 attempt 1 阻断项 B-1 返工落定）：
 *       · 装配/桥自身缺陷（CodeUnknown＝码表缺对端在用码——87 码收编缺漏；
 *         Usage＝本桥构造的上下文 token 越界）：异常原样上抛 fail-fast
 *         （AGENTS"禁止吞错"；装配清单缺漏属装配错误，HarnessMain 的打开
 *         编排兜底把它转为可观测错误页而非进程死亡）；
 *       · 对端记录契约拒绝（SubjectMissing/ComparisonMissing/ParamSchema
 *         Mismatch/ContextMissing/CodeDeprecated——对端按 §9.7 快捷 sink
 *         形态发射的记录与工厂校验链的差距；已知面：PRJ-LOCK-HELD 等
 *         项目级事件 subject=∅ 而用户级码要求 subject〔core §4.8〕，
 *         paramSchema 占位无法经快捷形态携带）：Dev 通道具名上报后**协议
 *         继续**——PM-07 降级只读是打开协议的成功形态（UiPorts.hpp C-3
 *         "降级只读也是成功"），不得因诊断侧拒绝而进程死亡或打开失败；
 *         该缺口的裁决权在 project/diagnostics 所有者（验收记录 F-290
 *         候选登记：发射面补 subject 或工厂校验豁免面），本桥不代修补
 *         （代补 subject/params＝虚构绑定）——具名上报即不吞错。
 *   - reportDev(channel, message)：直转 IDevLogSink::logDev（diagnostics
 *     §6.2——Dev 级事实不入目录，走开发日志）。
 *
 * 所有权：HarnessMain 持有本桥并保证其存活期覆盖打开协议调用窗口（桥以
 * 裸指针注入 OpenStoreRequest.diagnostics——对端契约"非 owning"）。
 */
class ProjectDiagnosticsBridge final : public project::IDiagnosticsSink {
public:
    /**
     * @brief 装配构造（下游三者必须非空——空下游的桥没有存在意义，装配期
     *        fail-fast 拦截）。
     *
     * @param sink    [in] 会话级诊断目录（DiagCatalog——用户级条目下游）
     * @param factory [in] 诊断工厂（create 唯一入口——码表校验在此发生）
     * @param devLog  [in] 开发日志通道（Dev 级出线，§6.2）
     *
     * @throws std::invalid_argument 任一下游为空（装配错误）
     */
    ProjectDiagnosticsBridge(std::shared_ptr<diagnostics::IDiagnosticSink> sink,
                             std::shared_ptr<diagnostics::IDiagnosticFactory> factory,
                             std::shared_ptr<diagnostics::IDevLogSink> devLog);

    /// @brief 用户级记录：经工厂 create 入目录；拒绝分装配缺陷（上抛
    ///        fail-fast）与对端记录契约缺口（Dev 具名上报后协议继续——
    ///        显式纪律见类注释，两路都不静默）。
    void report(const core::DiagnosticRecord& record) override;

    /// @brief 开发级事实：直转 Dev 日志通道（不入目录——diagnostics §6.2）。
    void reportDev(const std::string& channel, const std::string& message) override;

private:
    /// 诊断目录（共享引用——所有权在 HarnessMain，桥只持句柄）。
    std::shared_ptr<diagnostics::IDiagnosticSink> m_sink;
    /// 诊断工厂（record→entry 的唯一合法通道，§9.2）。
    std::shared_ptr<diagnostics::IDiagnosticFactory> m_factory;
    /// 开发日志通道（reportDev 的下游）。
    std::shared_ptr<diagnostics::IDevLogSink> m_devLog;
};

// =====================================================================
// StorePortAdapter——project::ProjectStore → ui::IUiProjectStorePort（C-3）
// =====================================================================

/**
 * @brief 存储上下文关闭协议面适配器（一次成功打开产出一个实例）。
 *
 * 翻译规则（不改义——project.md §5.1 关闭协议 → UiPorts.hpp C-3 行）：
 *   - requestClose() → ProjectStore::requestClose()（幂等信号，返回在途
 *     引用数——不催促不跳过归档的语义由对端保证）；
 *   - isClosed() → ProjectStore 上下文状态位；
 *   - subscribeClose(observer) → 经内部 ForwardingObserver 把 project 的
 *     ICloseObserver::onStoreClosed(store) 翻译为 ui 的
 *     IUiStoreCloseObserver::onStoreClosed(store.projectId())。
 *
 * 所有权与保活（INV-SES-3 的适配器侧落点）：本适配器**独占持有**
 * ProjectStore（unique_ptr 自 OpenStoreResult 移入）——ui 经 shared_ptr 持
 * 有本适配器＝持有存储上下文保活引用（"保持 shared 持有直到 subscribeClose
 * 回调"原文的代码面）。
 *
 * v0.1 诚实边界（登记于 UI-T15 契约）：subscribeClose 返回的 RAII 句柄是
 * 无操作实现（project 关闭契约"回调至多一次、无退订"——句柄析构无资源可
 * 释放）；转发观察者由本适配器持有到析构（先于 store 析构——悬挂不可能）。
 * 产品装配层落位时按同一形状替换为真实订阅面（适配点单一，ui 侧零改动）。
 */
class StorePortAdapter final : public IUiProjectStorePort {
public:
    /// @brief 接管打开结果中的存储上下文（unique_ptr 所有权即刻移入）。
    explicit StorePortAdapter(std::unique_ptr<project::ProjectStore> store);

    /// @brief 析构：适配器消亡即存储上下文消亡（Active 态析构＝对端隐式
    ///        排空收尾——project.md §5.1 生命周期行）。
    ~StorePortAdapter() override;

    /// @brief 请求关闭存储上下文（直转；返回在途引用数）。
    std::uint32_t requestClose() override;

    /// @brief 上下文是否已释放（ui 端口冻结名 isClosed → 对端 closed()）。
    bool isClosed() const override;

    /// @brief 订阅关闭完成回调（翻译观察者类型；句柄语义见类注释 v0.1 边界）。
    std::unique_ptr<core::IEventSubscription>
    subscribeClose(IUiStoreCloseObserver& observer) override;

private:
    /// 被适配的存储上下文（独占——本类析构即对端析构）。
    std::unique_ptr<project::ProjectStore> m_store;
    /// 转发观察者集合（适配器持有＝存活期覆盖 store 的订阅表——v0.1 边界
    /// 的悬挂防御；产品装配层按真实退订句柄替换）。元素基类型＝project 的
    /// 关闭观察者接口（ForwardingCloseObserver 在 .cpp 内实现）。
    std::vector<std::unique_ptr<project::ICloseObserver>> m_forwarders;
};

// =====================================================================
// StoreFactoryPortAdapter——ProjectStoreFactory → ui::IUiStoreFactoryPort
// =====================================================================

/**
 * @brief 打开五步协议适配器（§5.2 Opening 态的 ui 侧入口 → project 静态工厂）。
 *
 * 翻译规则（不改义——project.md §5.1 factory.open 契约 → UiPorts.hpp C-3
 * 工厂行）：
 *   - 路径先 weakly_canonical（失败错误页"定位具体文件"的数据源口径）；
 *   - UiOpenMode→project::OpenMode 直映（Writable 降级只读不阻塞——PM-07
 *     的降级发生在对端锁半步，适配器零加工）；
 *   - 成功：装配 SessionPortBundle（store＝StorePortAdapter；drafts/tasks
 *     ＝v0.1 桩——见各桩类注释）＋OpenedProjectFacts（projectId/displayName
 *     取自存储上下文权威面，零虚构）；
 *   - 失败：StoreError 折叠为 OpenStoreOutcome.ok=false＋稳定 token（§5.3
 *     显示差异的判别输入）＋detail 原样透传（面向开发诊断）——不吞错。
 *
 * 诊断注入：OpenStoreRequest.diagnostics＝ProjectDiagnosticsBridge（打开
 * 协议产出的 PRJ-* 用户级诊断经桥上报——工厂接受的条目入目录，被工厂拒绝
 * 的按桥类注释的显式纪律具名落 Dev 日志，打开协议不受阻）。PM-07 只读横幅
 * 的数据源是打开结果自带的锁事实（readOnlyCause/lockHolder），不依赖目录
 * 条目在位——横幅呈现与诊断出线是两条独立通道。
 */
class StoreFactoryPortAdapter final : public IUiStoreFactoryPort {
public:
    /**
     * @brief 装配构造（桥必须非空——打开协议的诊断出线通道，装配期拦截）。
     *
     * @throws std::invalid_argument bridge 为空
     */
    explicit StoreFactoryPortAdapter(ProjectDiagnosticsBridge& bridge);

    /// @brief 执行打开五步协议（翻译规则见类注释；不抛——StoreError 折叠）。
    OpenStoreOutcome open(const std::string& canonicalPath,
                          UiOpenMode mode,
                          SessionPortBundle& outBindings) override;

private:
    /// 打开协议诊断桥（HarnessMain 持有——引用须覆盖本适配器存活期）。
    ProjectDiagnosticsBridge& m_bridge;
};

// =====================================================================
// v0.1 桩端口（如实承载"该链路尚未装配"的零值语义——零虚构纪律）
// =====================================================================

/**
 * @brief 草稿清单桩端口（C-5 读半区）：v0.1 harness 未装配 DraftController
 *        （§8 草稿链路的装配任务未启动），开发期无草稿写路径——磁盘草稿
 *        行集如实恒空（关闭对话框草稿区呈现"无草稿"，不虚构行）。
 *
 * 真实适配（project::DraftService::list(main) → DraftRowProjection）随草稿
 * 链路装配任务落位时替换；适配点单一（本类），ui 侧零改动。
 */
class DraftQueryPortStub final : public IUiDraftQueryPort {
public:
    /// @brief 恒空行集（v0.1 桩——见类注释诚实边界）。
    std::vector<DraftRowProjection> listDrafts() const override;
};

/**
 * @brief 会话任务桩端口（C-8）：v0.1 harness 无 execution 引擎装配——无在
 *        途任务（恒空清单），协作取消/强制终止无任务可作用（恒不受理）。
 *
 * 语义自查：空清单使关闭对话框任务区呈现"无活动任务"、noActiveTasks=true
 * （§5.4 S1 装配规则）——与"开发期确无任务"的事实一致，不是能力伪造。
 */
class SessionTaskPortStub final : public IUiSessionTaskPort {
public:
    /// @brief 恒空任务行集（无执行引擎＝无任务，零虚构）。
    std::vector<TaskRowProjection>
    nonTerminalTasks(const core::ProjectId& project) const override;

    /// @brief 恒不受理（无任务可取消——对端 Ack 的折叠 false 同款语义）。
    bool requestCancel(const core::TaskIdentity& task) override;

    /// @brief 恒不受理（无任务可强杀）。
    bool requestForceTerminate(const core::TaskIdentity& task) override;
};

/**
 * @brief 策略摘要桩端口（C-10）：v0.1 harness 不装配 policy::IPolicyProvider
 *        （其装配前置＝project 对象存储字节源＋修订闭包查询＋碰撞评估器，
 *        属完整 L5 装配线——policy.md §6.5）——策略未装载如实承载
 *        （available=false，右栏摘要卡呈现占位，UX-08 合法形态）。
 *
 * 真实适配随策略装配任务落位时替换；本桩不改写任何数值（零虚构——
 * "未装载"与"装载了空策略"是两个事实，本桩只表达前者）。
 */
class UnloadedPolicySource final : public IPolicySummarySource {
public:
    /// @brief 恒 available=false（策略未装载的诚实占位）。
    PolicySummaryProjection summary() const override;
};

/**
 * @brief 名称解析桩端口（C-11）：v0.1 harness 无 runtime 名称表装配——
 *        恒不可解析（nullopt），消费方呈现占位文案（不拼名不截断——R-4）。
 */
class NullUiNameResolver final : public IUiNameResolver {
public:
    /// @brief 恒 nullopt（无名称表＝不可解析，不虚构名称）。
    std::optional<std::string> resolveObjectId(core::ObjectId id) const override;
};

/**
 * @brief 关于框数据源桩端口（§11.4）：装配报告空集（清单退化为白名单占位
 *        行——UI-PLG-2 合法形态）＋版本基线未装载（WP-24-T01 产出前
 *        available=false，版本区「未装载」占位——版本值零虚构）。
 */
class HarnessAboutSource final : public IUiAboutDataSource {
public:
    /// @brief 恒空报告集（装配器未落地＝空集，不虚构装配事实）。
    std::vector<PluginAssemblyReport> assemblyReports() const override;

    /// @brief 恒 available=false（版本基线未装载的诚实占位）。
    AboutVersionBaseline versionBaseline() const override;
};

}  // namespace app
}  // namespace ui
}  // namespace ird

#endif  // SDURWS_IRD_UI_APP_PORT_ADAPTERS_HPP
