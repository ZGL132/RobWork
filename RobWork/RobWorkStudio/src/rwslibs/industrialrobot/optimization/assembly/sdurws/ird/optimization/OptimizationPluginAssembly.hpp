/**
 * @file   OptimizationPluginAssembly.hpp
 * @brief  optimization 插件装配门面——宿主装配层消费 optimization 插件的
 *         **唯一公共入口**（WP-20-T02 最小可注册形态＋WP-20-T10 面板
 *         登记面扩展）。
 *
 * 设计依据：
 *   - units/optimization.md §3.1/§3.2（目标布局——plugin/ 目录承载
 *     sdurws_ird_optimization_plugin Qt Widgets 界面目标；插件界面四页
 *     ＝变量表/约束页/运行控制〔取消/进度漏斗〕/候选表与对比，随
 *     WP-20-T10 落位）、§9.1（运行控制 R1 边界：取消＋进度为 B 期承诺，
 *     暂停相关能力归 OPT-D/R2——本插件面零其呈现入口）、§6.5（阶段锁
 *     诊断呈现为阻塞横幅＋缺项清单，绝不呈现为候选淘汰）、§2.4（N11
 *     非所有权——Qt UI 真值归 ui；本插件只持只读投影消费面）
 *   - units/ui.md §11.1（静态白名单八 token 含 "optimization"——
 *     ui/src/AboutDialog.cpp pluginUiWhitelist 实测在册行；StageId::
 *     Optimization 阶段挂位＝§6.4 七阶段第 6 token "optimization"）、
 *     §3.5（文案键族 plugin.<id>.title／cmd.<id>.title——值归 ui 文案
 *     资源；UX-02：token 本身不进用户文本）、§10.9（装配时序——工厂/
 *     注入在装配期，面板工厂仅 UI 线程）
 *   - 先例：dynamics/assembly/DynamicsPluginAssembly.hpp（WP-17-T09
 *     面板登记面扩展的同款形态——T02 两登记字段逐字保留＋T10 新增
 *     挂位/域键/面板登记的同构字段与模块门面；零 ui 编译边）＋
 *     trajectory WP-16-T03/selection WP-19-T02（更早自持描述符先例）
 *   - 任务契约 tasks/foundation/WP-20-T10.json（acceptance 1/2/3）
 *
 * ★ 落地面口径（诚实登记，DTB §5.4 精神——单元卡 §1.3 同步登记）：
 *   1. **自持描述符（B 方案——编排侧既有裁决先例）**：T02 两字段
 *      （pluginId/titleKey）逐字保留（登记值不动、既有契约值断言保留）；
 *      本批新增阶段挂位 token、域注册键与一条面板登记记录的同构字段。
 *      字段与 ui::PluginUiDescriptor 对应子集一一对应、零增删——宿主
 *      装配批次收口时按字段翻译注册。**缺口登记**："经 IPluginUiRegistrar
 *      白名单挂位"需要 optimization→ui 编译边，本单元依赖白名单（卡
 *      §3.2 九条登记边）与 ird_gates 机器面均无该边、白名单文件不在
 *      本任务 allowedFiles 内不可增登——ui 单元公共类型本批零消费；
 *      白名单 token 对账以测试自持词表（ui.md §11.1 八 token）钉住
 *      （WP-17-T09 同款先例，P-OPT-10 承接登记见单元卡）。
 *   2. **命令登记面缺席（诚实缺席非遗漏）**：本描述符**不携带命令
 *      清单字段**——R1 域命令词表未随卡面登记（卡 §10.2 候选应用经
 *      project 命令组合、运行启动/取消经宿主绑定的运行控制缝直达，
 *      不经 ui 命令注册表），零私造词表（NFR-MNT-03）；宿主装配批次
 *      若按 ui.md SA-16 登记域命令，随该批次增登本面（不预建占位，
 *      NFR-MNT-04）。
 *   3. **面板面形态**：四页（变量表/约束页/运行控制/候选表与对比）
 *      承载于**同一主面板**（Tab 容器四页——四页共享同一会话态与
 *      运行上下文，拆分多面板会造成同数据多实例的呈现漂移），故
 *      panels 恰一条登记记录（advanced=false——主面板位，UX-04）。
 *   4. 零业务计算逻辑（卡 §3.2 红线"只消费端口与只读投影，不持
 *      算法/判定真值"）：本门面是纯值聚合＋工厂闭包转接——优化域
 *      计算类符号零出现（契约测试全文词表扫描钉住；该扫描为全文扫描、
 *      不剥注释——本头注释亦不书写词表符号字样）。
 *
 * 落位形态说明（文件位置）：本头位于 optimization 单元的 `assembly/`
 * 目录（plugin 目标的 PUBLIC include 面——插件界面目标的装配契约头，
 * 非产品 include/ 扫描域；与 plugin/ 同理在零 Qt 红线的文件域之外——
 * ird_gates 第 4 步产品面扫描域为 include/**＋src/**）。实现
 * （plugin/OptimizationPluginAssembly.cpp）编入 sdurws_ird_optimization_
 * plugin 目标。
 *
 * 线程模型：createOptimizationPluginAssembly／bind 系／setServices 在
 * 装配线程调用（ui.md §10.9 同期）；面板工厂闭包仅 UI 线程调用；
 * session() 的返回引用仅 UI 线程访问（卡 §9.1——运行控制面板的会话态
 * 与缝全部 UI 线程；候选评估绝不在 UI 线程执行——评估唯一经缝背后的
 * 宿主编排/worker 侧，NFR-PERF-01）。
 */

#ifndef IRD_OPTIMIZATION_ASSEMBLY_OPTIMIZATIONPLUGINASSEMBLY_HPP
#define IRD_OPTIMIZATION_ASSEMBLY_OPTIMIZATIONPLUGINASSEMBLY_HPP

#include <functional>
#include <memory>
#include <string>
#include <vector>

class QWidget;  // 前置声明：面板工厂产物（全局域——本头不拖入 Widgets）

namespace sdurws::ird::optimization {

class OptPanelModule;         // 前置声明（unique_ptr 成员——析构在 cpp）
struct OptPanelServices;      // 前置声明（setServices 引用入参——完整
                              //   类型在 plugin/OptPanelTypes.hpp）
struct OptModuleSessionState; // 前置声明（session() 返回引用）

// =====================================================================
// 挂位/域键词表（登记出处常量——本头是门面登记面的唯一书写点）。
// =====================================================================

/// 挂位阶段 token（ui.md §6.4 七阶段第 6——"optimization"，工作流七
/// 阶段"优化"阶段；StageId::Optimization 的 token 形态，优化主面板的
/// 宿主挂位位）。
inline constexpr const char* kOptStageToken = "optimization";

/// 域注册键（ui.md §6.5 域注册词表——就绪投影行的 domainKey 值）。
inline constexpr const char* kOptDomainKey = "optimization";

// =====================================================================
// 面板标题键（ui.md §3.5 键族——主面板标题；四页标题键在面板模型层
// 常量表（plugin/OptPanelModel.hpp）承载——本头只登记主面板记录所需
// 的面板级键；值归宿主文案资源，本域零文案值）。
// =====================================================================

/// 优化主面板标题键（ui.md §3.5 键族 plugin.<id>.panel.<页>.title 的
/// 面板级形态——宿主 Dock/Tab 标题呈现）。
inline constexpr const char* kOptPanelTitleKey =
    "plugin.optimization.panel.title";

// =====================================================================
// 面板登记记录（ui::PanelRegistration 字段同构自持值）。
// =====================================================================

/**
 * @brief 阶段面板登记记录（ui.md §10.9 形状的同构承载——挂位阶段
 *        token、标题键、工厂、高级位；宿主装配批次按字段翻译为 ui
 *        类型注册）。
 *
 * 工厂契约：每次调用新建面板 widget（归调用方接管——宿主层持有）；
 * 仅 UI 线程调用（ui.md §10.9 线程行）；工厂闭包的模块接线由装配
 * 门面（OptimizationPluginAssembly）创建时组装。
 * 值语义：可拷贝（工厂闭包共享门面模块——shared 语义由闭包自然承载）。
 */
struct OptPanelRegistration {
    std::string stageToken;  ///< 挂位阶段 token（kOptStageToken——§6.4 词表）
    std::string titleKey;    ///< 面板标题键（kOptPanelTitleKey——§3.5 键族）
    std::function<QWidget*()> factory; ///< 面板工厂（UI 线程调用）
    bool advanced = false;   ///< UX-04 高级面板标记（false＝主面板）
};

/**
 * @brief optimization 插件装配描述符（T02 登记字段＋T10 面板登记面）。
 *
 * 字段取舍口径（逐字段有据，零私造词表；命令面缺席口径见文件头注 2）：
 *   - pluginId：ui.md §11.1 白名单 token "optimization"（编译期/装配期
 *     常量词表，L5 应用壳固定）——插件身份的唯一书写点；
 *   - titleKey：ui.md §3.5 键族 plugin.<id>.title 的 "plugin.optimization.
 *     title"（UX-02：token 本身不进用户文本——用户见中文标题，值归
 *     ui 文案资源，本域不携带值）；
 *   - stageToken：ui.md §6.4 七阶段第 6 token（kOptStageToken）；
 *   - readinessDomainKey：ui.md §6.5 域注册键（kOptDomainKey——宿主
 *     汇聚翻译对账锚）；
 *   - panels：一条主面板登记记录（四页合一 Tab——advanced=false；
 *     工厂闭包转接模块 createPanel）。
 */
struct OptimizationPluginDescriptor {
    /// 插件身份（ui.md §11.1 白名单 token——"optimization"；显示名永不
    /// 替代身份，ARC-04 纪律）。
    std::string pluginId;
    /// 标题文案键（ui.md §3.5 键族 plugin.<id>.title——值归 ui 文案
    /// 资源文件，本域零文案值）。
    std::string titleKey;
    /// 挂位阶段 token（§6.4 词表——kOptStageToken 值）。
    std::string stageToken;
    /// 就绪投影域注册键（§6.5——kOptDomainKey 值）。
    std::string readinessDomainKey;
    /// 阶段面板登记记录（一条主面板——见类型注"面板面形态"）。
    std::vector<OptPanelRegistration> panels;
};

/**
 * @brief optimization 插件装配门面（描述符＋模块的装配载体——宿主装配
 *        层与开发 harness 的消费入口）。
 *
 * 生命周期：工厂返回值（移动语义）；模块归门面 unique_ptr 持有；
 * 面板工厂闭包捕获的模块指针存活期由装配层保证（宿主在面板创建期
 * 与面板存活期保持门面存活——ui.md §10.9 装配时序同款纪律）。
 */
class OptimizationPluginAssembly {
public:
    OptimizationPluginAssembly() = default;
    ~OptimizationPluginAssembly();  // 析构/移动在 cpp（unique_ptr 不完整
                                    //   类型 OptPanelModule——Pimpl 手法：
                                    //   特殊成员函数的定义点需完整类型，
                                    //   故声明在此、定义在实现 TU）
    OptimizationPluginAssembly(OptimizationPluginAssembly&&) noexcept;
    OptimizationPluginAssembly& operator=(OptimizationPluginAssembly&&) noexcept;
    OptimizationPluginAssembly(const OptimizationPluginAssembly&) = delete;
    OptimizationPluginAssembly& operator=(const OptimizationPluginAssembly&) = delete;

    /// 装配描述符（工厂填充——两登记字段＋T10 面板登记面）。
    OptimizationPluginDescriptor descriptor;

    /**
     * @brief 注入服务缝（面板创建前调用生效——创建后注入仅影响后建
     *        面板；装配层一次性组装全缝）。
     *
     * @param services [in] 服务缝聚合（整体替换语义；逐缝语义与空缝
     *                 降级见 OptPanelTypes.hpp）
     */
    void setServices(const OptPanelServices& services);

    /**
     * @brief 绑定运行启动缝（面板"检查并计算"按钮的提交出口——宿主
     *        编排面执行预检与任务提交；置空＝解除）。
     *
     * 为什么是缝而不是命令注册表：R1 域命令词表未随卡面登记（文件头
     * 注 2——零私造词表），运行启动语义（预检→提交）归宿主编排
     * （§12.4 合法调用序），本缝只是插件侧转发面。
     *
     * @param start [in] 启动函数（返回受理位——false＝宿主拒绝，用户
     *              可见不受理非异常；置空＝解除）
     */
    void bindRunStart(std::function<bool()> start);

    /**
     * @brief 绑定取消请求缝（面板"取消计算"按钮的提交出口——宿主
     *        转发执行侧协作取消请求（TASK-01——批边界粒度）；置空＝
     *        解除）。取消不产生错误诊断（UX-03）——受理位只反映
     *        宿主是否接受请求，非成败语义。
     *
     * @param cancel [in] 取消函数（返回受理位；置空＝解除）
     */
    void bindRunCancel(std::function<bool()> cancel);

    /**
     * @brief 绑定文案解析器（键→工程用语——宿主接 ui::resolveText；
     *        不绑定＝按钮呈现键名原文——开发态可见缺口）。
     *
     * @param resolve [in] 文案解析函数（置空＝解除）
     */
    void bindTextResolver(
        std::function<std::string(const std::string& titleKey)> resolve);

    /**
     * @brief 会话态访问（装配层注入事实投影的唯一入口——仅 UI 线程
     *        访问）。
     *
     * @return 会话态引用（模块成员——门面存活期内有效）
     */
    OptModuleSessionState& session();

    /// @brief 会话刷新（会话事实注入后的面板同步——面板未创建＝空操作）。
    void refreshFromSession();

    /// 模块访问（测试/harness 装配面——缝注入的等价路径；产品装配层
    /// 经 setServices/bind* 即可，零触模块内部）。
    OptPanelModule* module() noexcept { return m_module.get(); }

private:
    // 工厂是描述符现产＋模块创建的唯一装配点（同 TU——访问私有成员的
    // 友元豁免；dynamics 工厂同款形态）。
    friend OptimizationPluginAssembly createOptimizationPluginAssembly();

    std::unique_ptr<OptPanelModule> m_module;  ///< 模块（缝/会话态/面板引用）
};

/**
 * @brief 创建 optimization 插件装配门面（每次调用全新实例——无缓存）。
 *
 * 装配规则（逐字段见类型注）：pluginId＝"optimization"、titleKey＝
 * "plugin.optimization.title"、stageToken＝"optimization"、
 * readinessDomainKey＝"optimization"、panels＝一条主面板记录（四页
 * 合一 Tab，advanced=false；工厂闭包转接模块 createPanel）。
 *
 * @return 装配门面（零业务计算——纯值聚合＋工厂闭包转接）
 */
OptimizationPluginAssembly createOptimizationPluginAssembly();

}  // namespace sdurws::ird::optimization

#endif  // IRD_OPTIMIZATION_ASSEMBLY_OPTIMIZATIONPLUGINASSEMBLY_HPP
