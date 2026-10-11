/**
 * @file   TrajectoryPluginAssembly.hpp
 * @brief  trajectory 插件装配门面——宿主装配层消费 trajectory 插件的
 *         **唯一公共入口**（WP-16-T03 最小可注册形态＋WP-16-T12 面板/
 *         命令登记面与 ui 类型翻译注册面扩展）。
 *
 * 设计依据：
 *   - units/trajectory.md §16.1（插件目标与形态——sdurws_ird_trajectory_
 *     plugin 为 Qt Widgets 界面载体，"零计算逻辑：规划、碰撞、时间参数
 *     化、复检全部在 worker/计算库；UI 线程只做投影、插值查表（动画）、
 *     命令提交"）、§14.5.1（领域命令族——trajectory.plan-sequence 等七
 *     条会话命令，修订列全部"无（AT-04）"）、§16.2（零修订与零计算红线
 *     ——机器可断言表）
 *   - units/ui.md §10.9（PluginUiDescriptor 冻结形状／registerPluginUi
 *     校验序／装配期一次）、§11.1（白名单 token 词表——"trajectory" 为
 *     在册 token，ui/src/AboutDialog.cpp pluginUiWhitelist 实测含
 *     "trajectory" 行；装配序＝白名单序）、§6.4（七阶段 token——本域挂
 *     位 "trajectory-dynamics"）、§6.5（域注册键——就绪投影行
 *     domainKey="trajectory"）、§3.5（文案键族 plugin.<id>.title／
 *     cmd.<id>.title——值归 ui 文案资源，UX-02：token 本身不进用户文本）、
 *     §7.1（CommandDescriptor 缺省值——本域命令描述符翻译取缺省面）
 *   - 先例：dynamics/assembly/DynamicsPluginAssembly.hpp（WP-17-T02 自持
 *     描述符→WP-17-T09 面板/命令登记面→ASM-PLUG 收口批翻译注册面的三
 *     步演进同款；selection WP-19-T02/workflow WP-22-T02 更早先例）——
 *     trajectory 与 dynamics/selection 同属业务域单元，本头按 dynamics
 *     已验收形态一次落位登记面＋翻译注册面
 *
 * ★ 落地面口径（诚实登记，DTB §5.4 精神——单元卡 §1.2 T12 注同步）：
 *   1. **T03 两登记字段逐字保留**（pluginId＝"trajectory"、titleKey＝
 *      "plugin.trajectory.title"——登记值与既有契约测试的值断言不动；
 *      工厂产物自 T12 起为门面类，既有契约测试访问路径按 dynamics
 *      WP-17-T09 先例同款调整为 .descriptor.<字段>）。
 *   2. **T12 新增登记面**：阶段挂位 token（kTrjStageToken）、域注册键
 *      （kTrjDomainKey）、七命令描述符清单（TrjCommandDescriptor——
 *      token 词表唯一书写点在 Commands.hpp kCommandTokens，本头零字面
 *      复制）与一条主面板登记记录（工作流页＋曲线视图入口合一 Tab——
 *      曲线联动/动画读数是工作流页命令的直达呈现面，拆分双面板会造成
 *      同数据双实例的呈现漂移，dynamics 先例同款口径），字段与
 *      ui::PluginUiDescriptor 逐一对应、零增删。
 *   3. **翻译注册面（真实 ui 类型）**：translatePluginUiDescriptor 把
 *      自持描述符逐字段翻译为 ui::PluginUiDescriptor（字段同构缺口由
 *      编译器校验——ui 公共头在本编译单元真实可见）；registerWithHost-
 *      Registrar 经 ui::IPluginUiRegistrar 接口虚调用提交登记（白名单/
 *      重复/描述符合法性校验全走宿主实现——域侧零本地判定）。**本编译
 *      单元零 ui 库链接**（R-2 纪律下的纯头消费：ui 公共头全部为值类型
 *      与抽象接口，虚调用经调用方注入的实例完成，零符号依赖）——真实
 *      registrar 实例（createPluginUiRegistrar 工厂）的消费归宿主装配
 *      批次收口（ui 编译边建立＋白名单增登按治理程序办理——单元卡
 *      §16.3 衔接缝宿主半区如实登记）；本批以接口替身在契约测试中验证
 *      端口协议（testkit 替身纪律：替身只承载端口/状态/错误传播）。
 *   4. 零业务计算逻辑（卡 §16.1/§16.2 红线）：本门面是纯值聚合＋工厂
 *      闭包转接——轨迹域计算类符号零出现（契约测试词表扫描钉住；该扫
 *      描为全文扫描、不剥注释——本头注释亦不书写词表符号字样）。
 *
 * 落位形态说明（文件位置）：本头位于 trajectory 单元的 `assembly/` 目录
 * （plugin 目标的 PUBLIC include 面——插件界面目标的装配契约头，非产品
 * include/ 扫描域；与 plugin/ 同理在零 Qt 红线的文件域之外——ird_gates
 * 第 4 步产品面扫描域为 include/**＋src/**）。实现
 * （plugin/TrajectoryPluginAssembly.cpp）编入 sdurws_ird_trajectory_
 * plugin 目标。
 *
 * 线程模型：createTrajectoryPluginAssembly／bind 系／setServices／
 * translatePluginUiDescriptor／registerWithHostRegistrar 在装配线程调用
 * （ui.md §10.9 同期）；面板工厂闭包仅 UI 线程调用（ui.md §10.9 线程行）；
 * session() 返回引用仅 UI 线程访问（卡 §3.4）。
 */

#ifndef IRD_TRAJECTORY_ASSEMBLY_TRAJECTORYPLUGINASSEMBLY_HPP
#define IRD_TRAJECTORY_ASSEMBLY_TRAJECTORYPLUGINASSEMBLY_HPP

#include <functional>
#include <memory>
#include <string>
#include <vector>

// T12 批（WP-16-T12）：本头新增真实注册面——消费 ui 单元公共类型（
// §10.9 装配描述符/登记结果、§7 命令描述符）。包含面纪律同 dynamics
// 装配门面先例：ICommandRegistry.hpp 须显式包含（IPluginUiRegistrar.hpp
// 对 CommandDescriptor 仅前向声明，而本头携带的翻译函数签名与消费 TU
// 中 vector 成员析构需完整类型——声明点解析一致义务）。本头为纯头消费
 // ——零 ui 库符号依赖（虚调用经调用方注入实例；见文件头"落地面口径 3"）。
#include <sdurws/ird/ui/ICommandRegistry.hpp>    // ui::CommandDescriptor 完整类型（翻译函数签名面）
#include <sdurws/ird/ui/IPluginUiRegistrar.hpp>  // ui::PluginUiDescriptor/RegistrationOutcome/注册端口

class QWidget;  // 前置声明：面板工厂产物（全局域——本头不拖入 Widgets）

namespace sdurws::ird::trajectory {

class TrajPanelModule;        ///< 前置声明（unique_ptr 成员——析构在 cpp）
class TrajUiModule;           ///< 前置声明（§11.2 模块半区——unique_ptr 成员）
struct TrajPanelServices;     ///< 前置声明（setServices 引用入参——完整类型在 plugin/TrajPanelTypes.hpp）
struct TrajModuleSessionState; ///< 前置声明（session() 返回引用）

// =====================================================================
// 挂位/域键/面板标题词表（登记出处常量——本头是门面登记面的唯一书写
// 点；命令 token 词表的运行期载体在 Commands.hpp kCommandTokens，两处
// 值契约测试对账钉住）。
// =====================================================================

/// 挂位阶段 token（ui.md §6.4 七阶段第 4——"trajectory-dynamics"，工作
/// 流七阶段"轨迹/动力学"阶段；StageId::TrajectoryDynamics 的 token 形态。
/// 卡 §16.1：轨迹工作流＋曲线视图入口由该阶段页面承载——T12 验收口径）。
inline constexpr const char* kTrjStageToken = "trajectory-dynamics";

/// 域注册键（ui.md §6.5 域注册词表——就绪投影行的 domainKey 值；§14.5.2
/// DomainReadinessItem 供给行的对账锚）。
inline constexpr const char* kTrjDomainKey = "trajectory";

/// 工作流页标题键（Tab 页一——就绪投影＋命令区＋动画播放区；§3.5 键族
/// plugin.<id>.panel.<page>.title——值归宿主文案资源，本域零文案值）。
inline constexpr const char* kTrjWorkflowPageKey =
    "plugin.trajectory.panel.workflow.title";

/// 曲线视图页标题键（Tab 页二——q/qd/qdd 曲线只读投影；show-curve 的
/// 呈现入口页，与工作流页合一主面板）。
inline constexpr const char* kTrjCurvesPageKey =
    "plugin.trajectory.panel.curves.title";

// =====================================================================
// 命令描述符（ui::CommandDescriptor 字段同构自持值——本域承载面）。
// =====================================================================

/**
 * @brief 域命令描述符（§14.5.1 表一行的值承载——token 取自 Commands.hpp
 *        词表常量，titleKey 按 ui.md §3.5 键族 cmd.<id>.title 派生）。
 *
 * 修订语义注（§14.5.1 表"修订"列全部"无"）：七命令全部为零修订会话命
 * 令（Commands.hpp kTrajectorySessionContract 四常量），故本描述符不携
 * 带"只读模式拒绝"位（本域无写类命令；可用性统一由命令可用性缝判定
 * ——宿主权威，插件零本地二次判定）。值语义纯结构；线程安全。
 */
struct TrjCommandDescriptor {
    std::string token;     ///< 命令 token（Commands.hpp 词表值拷贝——
                           ///<   词表唯一书写点在那里，本处零字面复制）
    std::string titleKey;  ///< 命令标题键（"cmd.<token>.title" 键族——
                           ///<   值归宿主文案资源，UX-02）
};

// =====================================================================
// 面板登记记录（ui::PanelRegistration 字段同构自持值）。
// =====================================================================

/**
 * @brief 阶段面板登记记录（§10.9 形状的同构承载——挂位阶段 token、
 *        标题键、工厂、高级位；宿主装配批次按字段翻译为 ui 类型注册）。
 *
 * 工厂契约：每次调用新建面板 widget（归调用方接管——宿主层持有）；
 * 仅 UI 线程调用（ui.md §10.9 线程行）；工厂闭包的模块接线由装配门面
 * （TrajectoryPluginAssembly）创建时组装。
 * 值语义：可拷贝（工厂闭包共享门面模块——shared 语义由闭包自然承载）。
 */
struct TrjPanelRegistration {
    std::string stageToken;  ///< 挂位阶段 token（kTrjStageToken——§6.4 词表）
    std::string titleKey;    ///< 面板标题键（§3.5 键族——值归宿主文案资源）
    std::function<QWidget*()> factory; ///< 面板工厂（UI 线程调用）
    bool advanced = false;   ///< UX-04 高级面板标记（false＝主面板）
};

// =====================================================================
// 装配描述符（T03 两字段＋T12 登记面——字段与 ui::PluginUiDescriptor
// 逐一对应、零增删）。
// =====================================================================

/**
 * @brief trajectory 插件装配描述符（T03 登记字段＋T12 面板/命令登记面）。
 *
 * 字段取舍口径（逐字段有据，零私造词表）：
 *   - pluginId：ui.md §11.1 白名单 token "trajectory"（编译期/装配期
 *     常量词表，L5 应用壳固定）——插件身份的唯一书写点；
 *   - titleKey：ui.md §3.5 键族 plugin.<id>.title 的 "plugin.trajectory.
 *     title"（UX-02：token 本身不进用户文本——用户见中文标题，值归
 *     ui 文案资源，本域不携带值）；
 *   - stageToken：ui.md §6.4 七阶段第 4 token（kTrjStageToken）；
 *   - readinessDomainKey：ui.md §6.5 域注册键（kTrjDomainKey——宿主
 *     汇聚翻译对账锚）；
 *   - commands：§14.5.1 七命令描述符（工厂现产——token 词表唯一书写
 *     点在 Commands.hpp）；
 *   - panels：一条主面板登记记录（工作流页＋曲线视图入口合一 Tab——
 *     advanced=false；工厂闭包转接模块 createPanel）。
 */
struct TrajectoryPluginDescriptor {
    /// 插件身份（ui.md §11.1 白名单 token——"trajectory"；显示名永不
    /// 替代身份，ARC-04 纪律；T03 登记值逐字保留）。
    std::string pluginId;
    /// 标题文案键（ui.md §3.5 键族 plugin.<id>.title——值归 ui 文案
    /// 资源文件，本域零文案值；T03 登记值逐字保留）。
    std::string titleKey;
    /// 挂位阶段 token（§6.4 词表——kTrjStageToken 值；T12 新增）。
    std::string stageToken;
    /// 就绪投影域注册键（§6.5——kTrjDomainKey 值；T12 新增）。
    std::string readinessDomainKey;
    /// 随装配登记的命令描述符（§14.5.1 七命令——TrjCommandDescriptor）。
    std::vector<TrjCommandDescriptor> commands;
    /// 阶段面板登记记录（一条主面板——见类型注"面板面形态"）。
    std::vector<TrjPanelRegistration> panels;
};

// =====================================================================
// 装配门面（描述符＋模块的装配载体——宿主装配层与开发 harness 的
// 消费入口；dynamics 门面同款形态）。
// =====================================================================

/**
 * @brief trajectory 插件装配门面（描述符＋模块的装配载体）。
 *
 * 生命周期：工厂返回值（移动语义）；模块归门面 unique_ptr 持有；
 * 面板工厂闭包捕获的模块指针存活期由装配层保证（宿主在面板创建期
 * 与面板存活期保持门面存活——ui.md §10.9 装配时序同款纪律）。
 */
class TrajectoryPluginAssembly {
public:
    TrajectoryPluginAssembly() = default;
    ~TrajectoryPluginAssembly();  // 析构/移动在 cpp（unique_ptr 不完整类型
                                  //   TrajPanelModule/TrajUiModule——Pimpl
                                  //   手法：特殊成员函数的定义点需完整类型，
                                  //   故声明在此、定义在实现 TU）
    TrajectoryPluginAssembly(TrajectoryPluginAssembly&&) noexcept;
    TrajectoryPluginAssembly& operator=(TrajectoryPluginAssembly&&) noexcept;
    TrajectoryPluginAssembly(const TrajectoryPluginAssembly&) = delete;
    TrajectoryPluginAssembly& operator=(const TrajectoryPluginAssembly&) = delete;

    /// 装配描述符（工厂填充——T03 两字段＋T12 面板/命令登记面）。
    TrajectoryPluginDescriptor descriptor;

    /**
     * @brief 注入服务缝（面板创建前调用生效——创建后注入仅影响后建
     *        面板；装配层一次性组装全缝）。
     *
     * @param services [in] 服务缝聚合（整体替换语义；逐缝语义与空缝
     *                 降级见 plugin/TrajPanelTypes.hpp）
     */
    void setServices(const TrajPanelServices& services);

    /**
     * @brief 绑定命令提交出口（面板创建前后皆可——宿主 CommandRegistry
     *        注册权威归宿主，本出口只是插件侧转发缝；§15.11"注册经 ui
     *        CommandRegistry"的域侧出口半区）。
     *
     * @param submit [in] 提交函数（token→宿主命令管线；置空＝解除）
     */
    void bindCommandSubmit(std::function<void(const std::string&)> submit);

    /**
     * @brief 绑定命令可用性查询（统一按钮门控——宿主权威判定）。
     *
     * @param availability [in] 可用性函数（token→可用位；置空＝解除）
     */
    void bindCommandAvailability(
        std::function<bool(const std::string&)> availability);

    /**
     * @brief 绑定文案解析器（键→工程用语——宿主接 ui 文案资源；
     *        不绑定＝按钮呈现键名原文——开发态可见缺口）。
     *
     * @param resolve [in] 文案解析函数（置空＝解除）
     */
    void bindTextResolver(
        std::function<std::string(const std::string& titleKey)> resolve);

    /**
     * @brief 会话态访问（装配层注入快照绑定/事实投影的唯一入口——
     *        仅 UI 线程访问）。
     *
     * @return 会话态引用（模块成员——门面存活期内有效）
     */
    TrajModuleSessionState& session();

    /// @brief 会话刷新（会话事实注入后的面板同步——面板未创建＝空操作）。
    void refreshFromSession();

    /// 模块访问（测试/harness 装配面——缝注入的等价路径；产品装配层
    /// 经 setServices/bind* 即可，零触模块内部）。
    TrajPanelModule* module() noexcept { return m_module.get(); }

    /// §11.2 界面模块访问（T12 批——激活路径创建的模块半区；未激活＝空。
    /// 接口面承载：宿主装配层与测试只见 ui 公共接口，不触插件私有实现
    /// 类——R-2 装配纪律；上行转换在实现 TU 完成——完整类型可见处）。
    ui::IPluginUiModule* uiModule() noexcept;

private:
    // 工厂是描述符现产＋模块创建的唯一装配点（同 TU——访问私有成员的
    // 友元豁免；dynamics 工厂同款形态）。激活函数同豁免：挂载 §11.2
    // 模块半区需要触达两个 unique_ptr 成员（同 TU 实现面）。
    friend TrajectoryPluginAssembly createTrajectoryPluginAssembly();
    friend ui::RegistrationOutcome registerWithHostRegistrar(
        TrajectoryPluginAssembly& assembly, ui::IPluginUiRegistrar* registrar);

    std::unique_ptr<TrajPanelModule> m_module;  ///< 模块（缝/会话态/面板引用）
    std::unique_ptr<TrajUiModule> m_uiModule;   ///< §11.2 模块半区（激活路径
                                                ///<   创建——unique_ptr 不完整
                                                ///<   类型，析构在实现 TU）
};

/**
 * @brief 创建 trajectory 插件装配门面（每次调用全新实例——无缓存）。
 *
 * 装配规则（逐字段见类型注）：pluginId＝"trajectory"、titleKey＝
 * "plugin.trajectory.title"、stageToken＝"trajectory-dynamics"、
 * readinessDomainKey＝"trajectory"、commands＝七命令目录、panels＝一条
 * 主面板记录（工厂闭包转接模块 createPanel）。
 *
 * @return 装配门面（零业务计算——纯值聚合＋工厂闭包转接）
 */
TrajectoryPluginAssembly createTrajectoryPluginAssembly();

// =====================================================================
// T12 批（WP-16-T12）——真实注册面：自持描述符翻译为 ui::PluginUi-
// Descriptor 并经宿主注册端口登记（§10.9 装配期一次）。字段同构缺口随
// ui 公共头在本编译单元的真实可见而由编译器校验（字段名/类型漂移即编
// 译错误）；真实 registrar 实例的消费归宿主装配批次收口（见文件头
// "落地面口径 3"）。
// =====================================================================

/**
 * @brief 自持描述符 → ui::PluginUiDescriptor 逐字段翻译（纯值函数）。
 *
 * 翻译规则（与 ui.md §10.9 冻结形状逐一对应，零增删）：
 *   - pluginId/titleKey：逐字直拷（§11.1 白名单 token／§3.5 键族）；
 *   - stages：stageToken 经词表翻译为 ui::StageId（恰一阶段——本域单
 *     挂位面；词表外 token＝装配期数据违约，fail-fast）；
 *   - capabilities：providesStagePanel＝panels 非空、registersCommands＝
 *     commands 非空、providesReadonlyProjection＝true（域行经 TrajUiModule
 *     自报——§6.5 汇聚源；能力声明"描述性，非判定性"）；
 *   - commands：逐条翻译（id＝token、ownerUnit＝pluginId、titleKey 直拷、
 *     其余字段取 §7.1 缺省——keywordKeys 空/category Workbench/scope
 *     Session/readOnlyAllowed true/bindable true/无默认键/空 menuPath/
 *     空 schema。本域七命令全部零修订会话命令，readOnlyAllowed 保持
 *     true：只读门控由命令可用性缝统一判定——宿主权威，插件零本地二
 *     次判定）；
 *   - panels：逐条翻译（stage 经词表翻译、titleKey/advanced 直拷、
 *     factory 闭包原样转接——std::function<QWidget*()> 同型）。
 *
 * @param descriptor [in] 自持描述符（工厂现产值——只读）
 * @return ui 装配描述符（值拷贝——调用方可即弃原描述符）
 *
 * @throws std::invalid_argument stageToken 落于 ui.md §6.4 七阶段词表外
 *         （装配期数据违约——调用方错误，fail-fast）
 */
ui::PluginUiDescriptor translatePluginUiDescriptor(
    const TrajectoryPluginDescriptor& descriptor);

/**
 * @brief 激活注册：向宿主注册端口登记本插件（§10.9 装配期一次）。
 *
 * 执行序：①registrar 空检查（无注册端口实现＝调用方装配违约，fail-fast
 * ——宿主装配批次必传端口，不静默吞）；②创建/复用门面持有的 §11.2 模
 * 块半区（TrajUiModule——首次激活创建，重复调用复用同实例保证 registrar
 * 弱引用稳定）；③描述符翻译（translatePluginUiDescriptor）；④
 * registrar->registerPluginUi(descriptor, *module)——白名单/重复/描述
 * 符合法性校验全走宿主实现（域侧零本地判定），登记结果四值如实透传
 * （NotWhitelisted/DuplicatePlugin/InvalidDescriptor 不抛不吞——§11.3
 * 失败隔离归宿主呈现）。
 *
 * 线程模型：装配线程调用（§10.9 同期）；registrar 弱引用的存活期由装
 * 配层保证（宿主保持门面存活至壳拆除——§10.9 所有权行）。
 *
 * @param assembly  [in,out] 装配门面（模块半区挂载其上——宿主保持存活）
 * @param registrar [in] 宿主注册端口（接口注入——运行期宿主传参属 ui
 *                   宿主面；空指针＝调用方装配违约）
 * @return 登记结果（§10.9 四值——Ok/白名单外/重复/描述符非法）
 *
 * @throws std::invalid_argument registrar 为空（无 registrar 实现即
 *         fail-fast——不虚构登记成功）
 */
ui::RegistrationOutcome registerWithHostRegistrar(
    TrajectoryPluginAssembly& assembly, ui::IPluginUiRegistrar* registrar);

}  // namespace sdurws::ird::trajectory

#endif  // IRD_TRAJECTORY_ASSEMBLY_TRAJECTORYPLUGINASSEMBLY_HPP
