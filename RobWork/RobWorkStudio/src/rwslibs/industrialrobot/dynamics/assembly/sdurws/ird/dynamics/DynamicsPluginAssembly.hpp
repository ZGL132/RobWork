/**
 * @file   DynamicsPluginAssembly.hpp
 * @brief  dynamics 插件装配门面——宿主装配层消费 dynamics 插件的
 *         **唯一公共入口**（WP-17-T02 最小可注册形态＋WP-17-T09 面板/
 *         命令登记面扩展）。
 *
 * 设计依据：
 *   - units/dynamics.md §9.5（UI 协作——dynamics.analyze/show-curves/
 *     locate-peak/replay-at/export-curve-data 命令族与"UI 线程不得执行
 *     动力学计算"红线；工作流页＋曲线视图随 WP-17-T09 落位）、
 *     §10.7（命令适配器硬边界——零计算、零修订；命令注册权威＝ui
 *     CommandRegistry）、§11.5（GUI 手动点验流程——本门面即 harness
 *     的装配入口）、§2.4 O13（适配器不是全局命令注册表）
 *   - units/ui.md §10.9/§11.1（装配描述符形状与白名单 token 词表——
 *     本门面以**字段同构的自持描述符**承载可注册面；§3.5 键族
 *     plugin.<id>.title）；§11.1"插件计算库与界面目标分离"（ARCH §3.3
 *     ——二分结构）
 *   - 先例：selection/assembly/SelectionPluginAssembly.hpp（WP-19-T02
 *     自持描述符先例）＋kinematics/assembly（WP-15-T12 门面＋面板工厂
 *     形态——dynamics 按两者收缩：零 ui 编译边的同构承载）
 *   - 任务契约 tasks/foundation/WP-17-T02.json（描述符两字段登记值）、
 *     tasks/foundation/WP-17-T09.json（acceptance 1/2）
 *
 * ★ 落地面口径（诚实登记，DTB §5.4 精神——单元卡 §1.2 同步登记）：
 *   1. **自持描述符扩展（WP-17-T09）＋ASM-PLUG 收口（P-DYN-8 消账）**：
 *      T02 两字段（pluginId/titleKey）逐字保留（登记值与既有契约测试
 *      不动）；T09 批新增阶段挂位 token、域注册键、命令描述符清单
 *      （§9.5 五命令——DynCommandDescriptor）与一条面板登记记录（工作
 *      流页＋曲线视图合一 Tab——见类型注）的同构字段。字段与
 *      ui::PluginUiDescriptor 逐一对应、零增删。**ASM-PLUG 收口批
 *      （2026-10-10，所有者授权宿主装配批次）已兑现翻译注册**：单元边
 *      dynamics->ui 已按治理程序增登（ird_gates_whitelist.cmake＋
 *      dependency-graph.json 双面留痕），本头尾部 translatePluginUi-
 *      Descriptor/registerWithHostRegistrar 即翻译注册落点（字段同构
 *      缺口随真实编译边由编译器校验——"无 ui 侧编译期校验"的登记警
 *      告解除；§11.2 模块半区＝plugin/DynUiModule）。
 *   2. **面板面形态**：本域工作流页与曲线视图承载于**同一主面板**
 *      （Tab 容器两页——曲线联动/峰值定位/时刻回放是工作流页命令的
 *      直达呈现面，拆分双面板会造成同数据双实例的呈现漂移），故
 *      panels 恰一条登记记录（advanced=false——主面板位）；曲线视图
 *      是该记录的页半区，不是独立挂位面。
 *   3. **登记值类型的归属**：DynCommandDescriptor/DynPanelRegistration
 *      定义于本头（assembly/＝plugin 目标的 PUBLIC include 面——宿主
 *      装配层只见门面头即可完成登记面翻译，R-2 零插件私有头依赖；
 *      plugin/ 目录只承载实现细节）；命令 token 词表唯一书写点在
 *      Commands.hpp（同单元公共头），目录函数 dynDomainCommands 的
 *      声明面在 plugin/ 私有头（实现细节——本头零依赖，描述符值由
 *      工厂现产填充）。
 *   4. 零业务计算逻辑（卡 §9.5 红线）：本门面是纯值聚合＋工厂闭包
 *      转接——动力学计算类符号零出现（契约测试全文词表扫描钉住；
 *      该扫描为全文扫描、不剥注释——本头注释亦不书写词表符号字样）。
 *
 * 落位形态说明（文件位置）：本头位于 dynamics 单元的 `assembly/` 目录
 * （plugin 目标的 PUBLIC include 面——插件界面目标的装配契约头，非产品
 * include/ 扫描域；与 plugin/ 同理在零 Qt 红线的文件域之外——ird_gates
 * 第 4 步产品面扫描域为 include/**＋src/**）。实现
 * （plugin/DynamicsPluginAssembly.cpp）编入 sdurws_ird_dynamics_plugin
 * 目标。
 *
 * 线程模型：createDynamicsPluginAssembly／bind*／setServices 在装配
 * 线程调用（§10.9 同期）；面板工厂闭包仅 UI 线程调用（ui.md §10.9
 * 线程行）；session() 的返回引用仅 UI 线程访问（卡 §3.4）。
 */

#ifndef IRD_DYNAMICS_ASSEMBLY_DYNAMICSPLUGINASSEMBLY_HPP
#define IRD_DYNAMICS_ASSEMBLY_DYNAMICSPLUGINASSEMBLY_HPP

#include <functional>
#include <memory>
#include <string>
#include <vector>

// ASM-PLUG 收口批（P-DYN-8 消账）：本头新增真实注册面——消费 ui 单元
// 公共类型（§10.9 装配描述符/登记结果、§7 命令描述符）。包含面纪律同
// workflow/kinematics 装配门面先例：ICommandRegistry.hpp 须显式包含
// （IPluginUiRegistrar.hpp 对 CommandDescriptor 仅前向声明，而本头携带
// 的翻译函数签名与消费 TU 中 vector 成员析构需完整类型——声明点解析一
// 致义务）。单元边 dynamics->ui 已随本批登记（ird_gates_whitelist.cmake
// IRD_ALLOWED_UNIT_EDGES＋traceability/dependency-graph.json 双面留痕）。
#include <sdurws/ird/ui/ICommandRegistry.hpp>    // ui::CommandDescriptor 完整类型（翻译函数签名面）
#include <sdurws/ird/ui/IPluginUiRegistrar.hpp>  // ui::PluginUiDescriptor/RegistrationOutcome/注册端口

class QWidget;  // 前置声明：面板工厂产物（全局域——本头不拖入 Widgets）

namespace sdurws::ird::dynamics {

class DynPanelModule;         // 前置声明（unique_ptr 成员——析构在 cpp）
class DynUiModule;            // 前置声明（§11.2 模块半区——unique_ptr 成
                              //   员，析构在 cpp；ASM-PLUG 收口批新增）
struct DynPanelServices;      // 前置声明（setServices 引用入参——完整
                              //   类型在 plugin/DynPanelTypes.hpp）
struct DynModuleSessionState; // 前置声明（session() 返回引用）

// =====================================================================
// 命令 token 与键词表（登记出处常量——本头是门面登记面的唯一书写点；
// 命令 token 的运行期词表载体在 Commands.hpp kCommandTokens，两处值
// 契约测试对账钉住）。
// =====================================================================

/// 挂位阶段 token（ui.md §6.4 七阶段第 4——"trajectory-dynamics"，工作
/// 流七阶段"轨迹/动力学"阶段；dynamics 工作流页的宿主挂位位）。
inline constexpr const char* kDynStageToken = "trajectory-dynamics";

/// 域注册键（ui.md §6.5 域注册词表——就绪投影行的 domainKey 值）。
inline constexpr const char* kDynDomainKey = "dynamics";

// =====================================================================
// 命令描述符（ui::CommandDescriptor 字段同构自持值——本域承载面）。
// =====================================================================

/**
 * @brief 域命令描述符（§9.5 表一行的值承载——token 取自 Commands.hpp
 *        词表常量，titleKey 按 ui.md §3.5 键族 cmd.<id>.title 派生）。
 *
 * 修订语义注（§9.5 表"修订"列全部"无"）：五命令全部为会话命令
 * （零修订——Commands.hpp kReplaySessionContract 四常量），故本描述符
 * 不携带"只读模式拒绝"位（本域无写类命令；可用性统一由命令可用性
 * 缝判定——宿主权威，插件零本地判定）。值语义纯结构；线程安全。
 */
struct DynCommandDescriptor {
    std::string token;     ///< 命令 token（Commands.hpp 词表值拷贝——
                           ///<   词表唯一书写点在那里，本处零字面复制）
    std::string titleKey;  ///< 命令标题键（"cmd.dynamics.<leaf>.title"
                           ///<   键族——值归宿主文案资源，UX-02）
};

// =====================================================================
// 面板登记记录（ui::PanelRegistration 字段同构自持值）。
// =====================================================================

/**
 * @brief 阶段面板登记记录（§10.9 形状的同构承载——挂位阶段 token、
 *        标题键、工厂、高级位；宿主装配批次按字段翻译为 ui 类型注册）。
 *
 * 工厂契约：每次调用新建面板 widget（归调用方接管——宿主层持有）；
 * 仅 UI 线程调用（ui.md §10.9 线程行）；工厂闭包的模块接线由装配
 * 门面（DynamicsPluginAssembly）创建时组装。
 * 值语义：可拷贝（工厂闭包共享门面模块——shared 语义由闭包自然承载）。
 */
struct DynPanelRegistration {
    std::string stageToken;  ///< 挂位阶段 token（kDynStageToken——§6.4 词表）
    std::string titleKey;    ///< 面板标题键（§3.5 键族——值归宿主文案资源）
    std::function<QWidget*()> factory; ///< 面板工厂（UI 线程调用）
    bool advanced = false;   ///< UX-04 高级面板标记（false＝主面板）
};

// =====================================================================
// 面板标题键（§3.5 键族——两页标题；值归宿主文案资源，本域零文案值）。
// =====================================================================

/// 工作流页标题键（Tab 页一——就绪投影＋命令区＋回放区）。
inline constexpr const char* kDynWorkflowPageKey =
    "plugin.dynamics.panel.workflow.title";
/// 曲线视图页标题键（Tab 页二——通道曲线＋游标联动＋峰值定位）。
inline constexpr const char* kDynCurvesPageKey =
    "plugin.dynamics.panel.curves.title";

/**
 * @brief dynamics 插件装配描述符（T02 登记字段＋T09 面板/命令登记面）。
 *
 * 字段取舍口径（逐字段有据，零私造词表）：
 *   - pluginId：ui.md §11.1 白名单 token "dynamics"（编译期/装配期
 *     常量词表，L5 应用壳固定）——插件身份的唯一书写点；
 *   - titleKey：ui.md §3.5 键族 plugin.<id>.title 的 "plugin.dynamics.
 *     title"（UX-02：token 本身不进用户文本——用户见中文标题，值归
 *     ui 文案资源，本域不携带值）；
 *   - stageToken：ui.md §6.4 七阶段第 4 token（kDynStageToken）；
 *   - readinessDomainKey：ui.md §6.5 域注册键（kDynDomainKey——宿主
 *     汇聚翻译对账锚）；
 *   - commands：§9.5 五命令描述符（工厂现产——token 词表唯一书写点
 *     在 Commands.hpp）；
 *   - panels：一条主面板登记记录（工作流页＋曲线视图合一 Tab——
 *     advanced=false；工厂闭包转接模块 createPanel）。
 */
struct DynamicsPluginDescriptor {
    /// 插件身份（ui.md §11.1 白名单 token——"dynamics"；显示名永不
    /// 替代身份，ARC-04 纪律）。
    std::string pluginId;
    /// 标题文案键（ui.md §3.5 键族 plugin.<id>.title——值归 ui 文案
    /// 资源文件，本域零文案值）。
    std::string titleKey;
    /// 挂位阶段 token（§6.4 词表——kDynStageToken 值）。
    std::string stageToken;
    /// 就绪投影域注册键（§6.5——kDynDomainKey 值）。
    std::string readinessDomainKey;
    /// 随装配登记的命令描述符（§9.5 五命令——DynCommandDescriptor）。
    std::vector<DynCommandDescriptor> commands;
    /// 阶段面板登记记录（一条主面板——见类型注"面板面形态"）。
    std::vector<DynPanelRegistration> panels;
};

/**
 * @brief dynamics 插件装配门面（描述符＋模块的装配载体——宿主装配层
 *        与开发 harness 的消费入口）。
 *
 * 生命周期：工厂返回值（移动语义）；模块归门面 unique_ptr 持有；
 * 面板工厂闭包捕获的模块指针存活期由装配层保证（宿主在面板创建期
 * 与面板存活期保持门面存活——ui.md §10.9 装配时序同款纪律）。
 */
class DynamicsPluginAssembly {
public:
    DynamicsPluginAssembly() = default;
    ~DynamicsPluginAssembly();  // 析构/移动在 cpp（unique_ptr 不完整类型
                                //   DynPanelModule——Pimpl 手法：特殊成员
                                //   函数的定义点需完整类型，故声明在此、
                                //   定义在实现 TU）
    DynamicsPluginAssembly(DynamicsPluginAssembly&&) noexcept;
    DynamicsPluginAssembly& operator=(DynamicsPluginAssembly&&) noexcept;
    DynamicsPluginAssembly(const DynamicsPluginAssembly&) = delete;
    DynamicsPluginAssembly& operator=(const DynamicsPluginAssembly&) = delete;

    /// 装配描述符（工厂填充——两登记字段＋T09 面板/命令登记面）。
    DynamicsPluginDescriptor descriptor;

    /**
     * @brief 注入服务缝（面板创建前调用生效——创建后注入仅影响后建
     *        面板；装配层一次性组装全缝）。
     *
     * @param services [in] 服务缝聚合（整体替换语义；逐缝语义与空缝
     *                 降级见 DynPanelTypes.hpp）
     */
    void setServices(const DynPanelServices& services);

    /**
     * @brief 绑定命令提交出口（面板创建前后皆可——宿主 CommandRegistry
     *        注册权威归宿主，本出口只是插件侧转发缝）。
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
     * @brief 绑定文案解析器（键→工程用语——宿主接 ui::resolveText；
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
    DynModuleSessionState& session();

    /// @brief 会话刷新（会话事实注入后的面板同步——面板未创建＝空操作）。
    void refreshFromSession();

    /// 模块访问（测试/harness 装配面——缝注入的等价路径；产品装配层
    /// 经 setServices/bind* 即可，零触模块内部）。
    DynPanelModule* module() noexcept { return m_module.get(); }

    /// §11.2 界面模块访问（ASM-PLUG 收口批——激活路径创建的模块半区；
    /// 未激活＝空。接口面承载：宿主装配层与测试只见 ui 公共接口，不触
    /// 插件私有实现类——R-2 装配纪律；上行转换在实现 TU 完成——完整
    /// 类型可见处）。
    ui::IPluginUiModule* uiModule() noexcept;

private:
    // 工厂是描述符现产＋模块创建的唯一装配点（同 TU——访问私有成员的
    // 友元豁免；kinematics 工厂同款形态）。激活函数同豁免：挂载 §11.2
    // 模块半区需要触达两个 unique_ptr 成员（同 TU 实现面）。
    friend DynamicsPluginAssembly createDynamicsPluginAssembly();
    friend ui::RegistrationOutcome registerWithHostRegistrar(
        DynamicsPluginAssembly& assembly, ui::IPluginUiRegistrar* registrar);

    std::unique_ptr<DynPanelModule> m_module;  ///< 模块（缝/会话态/面板引用）
    std::unique_ptr<DynUiModule> m_uiModule;   ///< §11.2 模块半区（激活路径
                                               ///<   创建——unique_ptr 不完整
                                               ///<   类型，析构在实现 TU；
                                               ///<   ASM-PLUG 收口批新增）
};

/**
 * @brief 创建 dynamics 插件装配门面（每次调用全新实例——无缓存）。
 *
 * 装配规则（逐字段见类型注）：pluginId＝"dynamics"、titleKey＝
 * "plugin.dynamics.title"、stageToken＝"trajectory-dynamics"、
 * readinessDomainKey＝"dynamics"、commands＝五命令目录、panels＝一条
 * 主面板记录（工厂闭包转接模块 createPanel）。
 *
 * @return 装配门面（零业务计算——纯值聚合＋工厂闭包转接）
 */
DynamicsPluginAssembly createDynamicsPluginAssembly();

// =====================================================================
// ASM-PLUG 收口批（P-DYN-8 消账）——真实注册面：自持描述符翻译为
// ui::PluginUiDescriptor 并经宿主注册端口登记（§10.9 装配期一次）。
// 字段同构缺口随真实编译边建立而由编译器校验（字段名/类型漂移即编
// 译错误），"无 ui 侧编译期校验"的登记警告就此解除。
// =====================================================================

/**
 * @brief 自持描述符 → ui::PluginUiDescriptor 逐字段翻译（纯值函数）。
 *
 * 翻译规则（与 ui.md §10.9 冻结形状逐一对应，零增删）：
 *   - pluginId/titleKey：逐字直拷（§11.1 白名单 token／§3.5 键族）；
 *   - stages：stageToken 经词表翻译为 ui::StageId（恰一阶段——本域单
 *     挂位面；词表外 token＝装配期数据违约，fail-fast）；
 *   - capabilities：providesStagePanel＝panels 非空、registersCommands＝
 *     commands 非空、providesReadonlyProjection＝true（域行经 DynUiModule
 *     自报——§6.5 汇聚源；能力声明"描述性，非判定性"）；
 *   - commands：逐条翻译（id＝token、ownerUnit＝pluginId、titleKey 直拷、
 *     其余字段取 §7.1 缺省——keywordKeys 空/category Workbench/scope
 *     Session/readOnlyAllowed true/bindable true/无默认键/空 menuPath/
 *     空 schema。本域五命令全部零修订会话命令，readOnlyAllowed 保持
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
    const DynamicsPluginDescriptor& descriptor);

/**
 * @brief 激活注册：向宿主注册端口登记本插件（§10.9 装配期一次——
 *        P-DYN-8 明文义务"真实 IPluginUiRegistrar 注册归宿主装配批次
 *        收口"的兑现落点）。
 *
 * 执行序：①registrar 空检查（无注册端口实现＝调用方装配违约，fail-fast
 * ——宿主装配批次必传端口，不静默吞）；②创建/复用门面持有的 §11.2 模
 * 块半区（DynUiModule——首次激活创建，重复调用复用同实例保证 registrar
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
    DynamicsPluginAssembly& assembly, ui::IPluginUiRegistrar* registrar);

}  // namespace sdurws::ird::dynamics

#endif  // IRD_DYNAMICS_ASSEMBLY_DYNAMICSPLUGINASSEMBLY_HPP
