/**
 * @file   TrajectoryPluginAssembly.cpp
 * @brief  trajectory 插件装配门面的实现翻译单元——描述符现产＋模块装配
 *         API＋ui 类型翻译注册面的落点（WP-16-T03 最小可注册形态＋
 *         WP-16-T12 面板/命令登记面扩展）。
 *
 * 设计依据：
 *   - units/trajectory.md §16.1（插件界面面——"零计算逻辑：规划、碰撞、
 *     时间参数化、复检全部在 worker/计算库"红线——本 TU 零轨迹域计算类
 *     符号〔词表见契约测试；其扫描为全文扫描，本注释不书写词表符号字
 *     样〕）、§14.5.1（领域命令族——命令登记面）、§16.2（零修订红线
 *     ——会话命令族全部零修订）
 *   - units/ui.md §10.9（PluginUiDescriptor 冻结形状/registerPluginUi
 *     校验序）、§11.1（白名单 token "trajectory"）、§6.4（七阶段 token
 *     词表）、§3.5（键族 plugin.<id>.title／cmd.<id>.title）
 *   - 先例：dynamics/plugin/DynamicsPluginAssembly.cpp（WP-17-T02 装配
 *     形态→WP-17-T09 面板/命令登记面→ASM-PLUG 收口批翻译注册面的同款
 *     全演进——trajectory 按 dynamics 已验收终态一次落位）
 *
 * 线程模型：工厂/bind 系/setServices/翻译/激活注册在装配线程；面板工厂
 * 闭包仅 UI 线程（ui.md §10.9 线程行）。
 *
 * Qt 说明（诚实登记）：本 TU 消费 QWidget（面板工厂产物类型——经
 * TrajPanelModule.hpp 前置声明与 TrajectoryPanelWidget 完整类型到达）；
 * Qt6 链接在 sdurws_ird_trajectory_plugin 目标层面建立（卡 §16.1 二分
 * 结构"Qt Widgets 插件"例外载体面）。零 Q_OBJECT 类故目标不开 AUTOMOC
 * （DTB §5.1 v0.17 行口径——面板/曲线视图均为 QWidget 派生纯虚
 * override 形态，无自定义信号槽需求）。
 *
 * ui 头消费说明（零链接纪律）：本 TU 经 assembly 公共头消费 ui 公共类
 * 型（装配描述符/命令描述符/注册端口）——全部为值类型与抽象接口，虚
 * 调用经调用方注入实例完成，零 ui 库符号依赖（真实 registrar 工厂的
 * 消费归宿主装配批次收口——assembly 头"落地面口径 3"）。
 */

#include <sdurws/ird/trajectory/TrajectoryPluginAssembly.hpp>

// 同单元私有头（R-2 不跨单元——本 TU 编入 trajectory 插件目标自身）。
#include "TrajPanelCommandCatalog.hpp" // trjDomainCommands（七命令目录现产）
#include "TrajPanelModule.hpp"         // TrajPanelModule（完整类型——析构/
                                       //   createPanel/refreshFromSession）
#include "TrajPanelTypes.hpp"          // TrajPanelServices/TrajModuleSessionState
                                       //   （API 签名完整型）
#include "TrajUiModule.hpp"            // TrajUiModule（§11.2 模块半区——激活
                                       //   路径创建）
#include "TrajectoryPanelWidget.hpp"   // TrajectoryPanelWidget（面板工厂产物）

#include <stdexcept>  // std::invalid_argument（词表外 token／空 registrar
                      //   的 fail-fast——调用方装配违约，不静默吞）

namespace sdurws::ird::trajectory {

namespace {

/**
 * @brief 挂位阶段 token → ui::StageId 词表翻译（装配期单点）。
 *
 * 词表＝ui.md §6.4 七阶段 token（宿主挂位位的唯一权威）；本域恰一挂位
 * 面（"trajectory-dynamics"——七阶段第 4），表保留全七行以承载翻译函数
 * 的通用形态（宿主装配批次若复用本点翻译他域 token，词表外值走同一
 * fail-fast 轨——零私造词表）。
 *
 * @param token [in] 挂位阶段 token（§6.4 词表值——assembly 头常量）
 * @return 对应 ui::StageId（§6.4 词表序）
 *
 * @throws std::invalid_argument token 不在七阶段词表（装配期数据违约
 *         ——调用方错误，fail-fast；宿主挂位断链宁可显式失败不静默）
 */
ui::StageId stageTokenToStageId(const std::string& token)
{
    // 查表翻译（词表序＝ui.md §6.4 冻结呈现序——UX-12）。
    if (token == "modeling")              { return ui::StageId::Modeling; }
    if (token == "requirements")          { return ui::StageId::Requirements; }
    if (token == "kinematics")            { return ui::StageId::Kinematics; }
    if (token == "trajectory-dynamics")   { return ui::StageId::TrajectoryDynamics; }
    if (token == "selection")             { return ui::StageId::Selection; }
    if (token == "optimization")          { return ui::StageId::Optimization; }
    if (token == "reporting")             { return ui::StageId::Reporting; }
    // 词表外 token＝装配期数据违约（宿主挂位位断链——显式失败不静默，
    // 与 RegistrationOutcome::InvalidDescriptor 的"描述符非法"语义同源；
    // 错误归类：调用方错误 fail-fast，非环境错误）。
    throw std::invalid_argument(
        "trajectory 装配面：挂位阶段 token 不在 ui.md §6.4 七阶段词表（宿主"
        "挂位断链——装配期数据违约）: " + token);
}

}  // namespace

// =====================================================================
// 门面装配 API（转发模块——dynamics 先例同款转发纪律：门面零业务语义，
// 模块是缝/会话态的唯一归属）。
// =====================================================================

TrajectoryPluginAssembly::~TrajectoryPluginAssembly() = default;
// 移动特殊成员同样在完整类型可见处默认（unique_ptr 成员——Pimpl 手法
// 与析构同口径）。
TrajectoryPluginAssembly::TrajectoryPluginAssembly(TrajectoryPluginAssembly&&) noexcept = default;
TrajectoryPluginAssembly& TrajectoryPluginAssembly::operator=(
    TrajectoryPluginAssembly&&) noexcept = default;

void TrajectoryPluginAssembly::setServices(const TrajPanelServices& services)
{
    if (m_module == nullptr) {
        return;  // 模块缺位＝装配缺陷（门面构造即持有模块——防御面
                 // 静默返回与既有 bind* 同纪律：不虚构注入成功）
    }
    m_module->services = services;
}

void TrajectoryPluginAssembly::bindCommandSubmit(
    std::function<void(const std::string&)> submit)
{
    if (m_module != nullptr) {
        m_module->services.commandSubmit = std::move(submit);
    }
}

void TrajectoryPluginAssembly::bindCommandAvailability(
    std::function<bool(const std::string&)> availability)
{
    if (m_module != nullptr) {
        m_module->services.commandAvailability = std::move(availability);
    }
}

void TrajectoryPluginAssembly::bindTextResolver(
    std::function<std::string(const std::string& titleKey)> resolve)
{
    if (m_module != nullptr) {
        m_module->services.textResolver = std::move(resolve);
    }
}

TrajModuleSessionState& TrajectoryPluginAssembly::session()
{
    // 会话权威态访问（装配层注入事实投影——模块内唯一载体；模块缺位
    // 不可达：工厂即建模块，null 解引用属装配期程序缺陷而非环境态，
    // 由 unique_ptr 契约保证）。
    return m_module->session;
}

ui::IPluginUiModule* TrajectoryPluginAssembly::uiModule() noexcept
{
    // §11.2 模块半区接口面（上行转换在此完成——TrajUiModule 完整类型
    // 经 TrajUiModule.hpp 可见；未激活＝unique_ptr 空 → 接口指针空）。
    return m_uiModule.get();
}

void TrajectoryPluginAssembly::refreshFromSession()
{
    if (m_module != nullptr) {
        m_module->refreshFromSession();
    }
}

// =====================================================================
// 模块面板工厂落点（TrajPanelModule——面板创建与会话刷新的执行面）。
// =====================================================================

QWidget* TrajPanelModule::createPanel()
{
    // 每次调用新建面板（归调用方接管——宿主层持有；构造即整面刷新）。
    m_panel = new TrajectoryPanelWidget(*this);
    return m_panel;
}

void TrajPanelModule::refreshFromSession()
{
    if (m_panel != nullptr) {
        m_panel->refreshFromSession();
    }
}

// =====================================================================
// 工厂（描述符现产——登记值与 ui.md 出处逐条对应，零私造词表）。
// =====================================================================

TrajectoryPluginAssembly createTrajectoryPluginAssembly()
{
    TrajectoryPluginAssembly result;
    // 具体模块（unique_ptr 持有——消费方经门面 API 与 module() 访问）。
    result.m_module = std::make_unique<TrajPanelModule>();

    // ---- 描述符两登记字段（T03 契约值逐字保留——既有契约测试的值断
    //      言不动；访问路径自 T12 起为 .descriptor.<字段>，dynamics
    //      WP-17-T09 先例同款）。 --------------------------------------
    TrajectoryPluginDescriptor& descriptor = result.descriptor;
    descriptor.pluginId = "trajectory";  // ui.md §11.1 白名单 token（
                                         //   AboutDialog.cpp 在册）
    descriptor.titleKey =
        "plugin.trajectory.title";  // ui.md §3.5 键族（值归 ui 文案资源）

    // ---- 挂位/域键词表（T12 新增——assembly 头常量直拷）。 ---------
    descriptor.stageToken = kTrjStageToken;         // §6.4 七阶段第 4
    descriptor.readinessDomainKey = kTrjDomainKey;  // §6.5 域注册键

    // ---- 命令登记面（§14.5.1 七命令——目录现产，词表书写点在
    //      Commands.hpp；键族派生规则在目录实现 TU）。 ---------------
    descriptor.commands = trjDomainCommands();

    // ---- 面板登记面（一条主面板——工作流页＋曲线视图入口合一 Tab；
    //      工厂闭包转接模块 createPanel，模块指针存活期由装配层保证）。 -
    TrjPanelRegistration panel;
    panel.stageToken = kTrjStageToken;
    panel.titleKey = kTrjWorkflowPageKey;  // 主面板标题键（工作流页——
                                           //   曲线视图为页半区）
    panel.advanced = false;                // 主面板位（UX-04 非 advanced）
    TrajPanelModule* moduleRaw = result.m_module.get();
    panel.factory = [moduleRaw]() -> QWidget* {
        // 闭包捕获模块裸指针（ui.md §10.9 装配时序——工厂调用期与面板
        // 存活期由装配层保证门面存活；每次调用新建面板）。
        return moduleRaw->createPanel();
    };
    descriptor.panels.push_back(std::move(panel));
    return result;
}

// =====================================================================
// T12 批（WP-16-T12）——真实注册面实现（翻译＋激活）。
// 设计依据：units/ui.md §10.9（PluginUiDescriptor 冻结形状/registerPluginUi
// 校验序）、§6.4/§6.5（StageId/域键词表）、§7.1（CommandDescriptor 缺省
// 值）；units/trajectory.md §16.1/§16.3（宿主装配面衔接——P-TRJ-8 宿主
// 半区：真实 registrar 实例的消费归宿主装配批次收口）。
// =====================================================================

ui::PluginUiDescriptor translatePluginUiDescriptor(
    const TrajectoryPluginDescriptor& descriptor)
{
    ui::PluginUiDescriptor translated;

    // ---- 身份两字段（§11.1 白名单 token／§3.5 键族——逐字直拷，零改写；
    //      登记值唯一书写点仍是 assembly 头常量与工厂）。 -----------------
    translated.pluginId = descriptor.pluginId;
    translated.titleKey = descriptor.titleKey;

    // ---- 覆盖阶段清单（恰一阶段——本域单挂位面；token→StageId 词表
    //      翻译，词表外 fail-fast——见 stageTokenToStageId 类注）。 -------
    translated.stages.push_back(stageTokenToStageId(descriptor.stageToken));

    // ---- 能力声明（§10.9"描述性，非判定性"——按描述符现状如实声明：
    //      面板非空＝提供阶段面板；命令非空＝登记命令；域就绪投影行恒
    //      经 §11.2 模块自报（readonlyProjections 透传流）＝参与汇聚）。 -
    translated.capabilities.providesStagePanel = !descriptor.panels.empty();
    translated.capabilities.registersCommands = !descriptor.commands.empty();
    translated.capabilities.providesReadonlyProjection = true;

    // ---- 命令登记面（逐条翻译：id＝token〔点分小写——§7.2 句法校验由
    //      宿主注册端口执行〕；ownerUnit＝pluginId〔§7.2 第 1 步白名单
    //      校验的对位值——域侧填本插件身份〕；titleKey 直拷；其余字段取
    //      §7.1 缺省：关键词空/分类 Workbench/作用域会话/只读可用/可绑
    //      键/无默认键/空菜单路径/空参数 schema）。本域七命令全部零修
    //      订会话命令，readOnlyAllowed=true 保持缺省——只读门控由命令
    //      可用性缝统一判定（宿主权威，插件零本地二次判定）。 -----------
    translated.commands.reserve(descriptor.commands.size());
    for (const TrjCommandDescriptor& command : descriptor.commands) {
        ui::CommandDescriptor uiCommand;
        uiCommand.id = command.token;             // §7.2 句法（点分小写）
        uiCommand.ownerUnit = descriptor.pluginId; // §7.2 owner 对位值
        uiCommand.titleKey = command.titleKey;     // §3.5 键族派生值直拷
        translated.commands.push_back(std::move(uiCommand));
    }

    // ---- 面板登记面（逐条翻译：stage 经词表翻译；titleKey/advanced 直
    //      拷；factory 闭包原样转接——std::function<QWidget*()> 同型，
    //      模块指针存活期由装配层保证的既有纪律随闭包语义不变）。 -----
    translated.panels.reserve(descriptor.panels.size());
    for (const TrjPanelRegistration& panel : descriptor.panels) {
        ui::PanelRegistration uiPanel;
        uiPanel.stage = stageTokenToStageId(panel.stageToken);
        uiPanel.titleKey = panel.titleKey;
        uiPanel.factory = panel.factory;
        uiPanel.advanced = panel.advanced;
        translated.panels.push_back(std::move(uiPanel));
    }
    return translated;
}

ui::RegistrationOutcome registerWithHostRegistrar(
    TrajectoryPluginAssembly& assembly, ui::IPluginUiRegistrar* registrar)
{
    // 第一步：无 registrar 实现＝调用方装配违约（宿主装配批次必传注册
    // 端口——§16.3 衔接缝的宿主半区义务）。显式 fail-fast，不静默吞、
    // 不虚构登记成功（错误归类：调用方错误——装配程序缺陷，非环境错误）。
    if (registrar == nullptr) {
        throw std::invalid_argument(
            "trajectory 装配激活：宿主注册端口为空（无 registrar 实现即 "
            "fail-fast——§10.9 装配期一次的登记契约无端口不可履行）");
    }

    // 第二步：§11.2 模块半区（TrajUiModule）创建/复用——首次激活创建并
    // 归门面 unique_ptr 持有；重复调用复用同实例（registrar 弱引用稳定，
    // 不因重入悬垂；§11.1"每插件恰好一次"下重入本身会被宿主判重复，
    // 这里保证的是弱引用不悬垂的防御面）。
    if (assembly.m_uiModule == nullptr) {
        // 面板模块指针此时必非空（工厂构造即建模块——null 属装配程序
        // 缺陷，unique_ptr 契约保证）。
        assembly.m_uiModule = std::make_unique<TrajUiModule>(assembly.m_module.get());
    }

    // 第三步：描述符翻译（纯值函数——字段逐一对应，词表外 token 在此
    // fail-fast 透出）。
    const ui::PluginUiDescriptor translated =
        translatePluginUiDescriptor(assembly.descriptor);

    // 第四步：宿主登记（§10.9 校验序——白名单→重复→描述符合法性全走
    // 宿主实现，域侧零本地判定；登记结果四值如实透传给调用方——失败
    // 隔离与呈现归宿主，§11.3）。
    return registrar->registerPluginUi(translated, *assembly.m_uiModule);
}

}  // namespace sdurws::ird::trajectory
