/**
 * @file   SelectionPluginAssembly.hpp
 * @brief  selection 插件装配门面——宿主装配层消费 selection 插件的
 *         **唯一公共入口**（WP-19-T02 最小可注册形态＋WP-19-T10 工作流
 *         页/目录管理/候选表面板与命令登记面扩展）。
 *
 * 设计依据：
 *   - units/selection.md §3.1（插件组成——sdurws_ird_selection_plugin
 *     ＝Qt Widgets 插件界面：目录管理页/筛选条件表单/候选表/淘汰原因
 *     视图/回填入口/进度与取消，随 WP-19-T10 落位）、§3.2（插件依赖
 *     仅两条：本计算库〔单元内〕＋Qt Widgets——**无 ui 编译边**，树/
 *     属性页协议列在"运行时注入/端口"列）、§3.4（插件零计算红线——
 *     机器可断言）、§3.5（目标布局——_plugin 目标）、§16（WP-19-T10
 *     行——选型工作流页＋目录管理＋候选表的界面交付面）
 *   - units/ui.md §11.1（IPluginUiRegistrar 静态白名单八 token 含
 *     "selection"——第 6 token，先行登记）、§3.5（文案键族
 *     plugin.<id>.title／cmd.<id>.title）、§6.4（七阶段词表第 5——
 *     "selection（选型）"）、§6.5（域注册键——DomainReadinessItem 的
 *     domainKey 词表）、UI-T21/T22 登记注（IUiTreeNodesProvider/
 *     IUiPropertyPagesProvider 域供给协议——卡 §3.1 点名的插件域供给
 *     形态）；§10.9（装配时序——面板工厂仅 UI 线程）
 *   - 先例：dynamics/assembly/DynamicsPluginAssembly.hpp（WP-17-T09
 *     同款"自持描述符扩展＋面板工厂闭包转接模块"形态——selection 与
 *     dynamics 同属业务域单元、同零 ui 编译边，落位形态逐结构同构）
 *   - 任务契约 tasks/foundation/WP-19-T02.json（描述符两登记值）、
 *     tasks/foundation/WP-19-T10.json（acceptance 1/2）
 *
 * ★ 落地面口径（诚实登记，DTB §5.4 精神——单元卡 §1.2 同步登记）：
 *   1. **契约编译期缺口按 WP-17-T09 先例 B 方案落位**：acceptance 1
 *      "经 IPluginUiRegistrar 白名单挂位"需 selection->ui 编译边，本
 *      单元依赖白名单（卡 §3.2 两登记边）与 ird_gates 机器面均无该边，
 *      且白名单文件不在本任务 allowedFiles 不可增登——装配面以**字段
 *      同构自持描述符**承载（与 ui::PluginUiDescriptor 逐一对应、零增
 *      删），白名单 token 对账以契约测试自持词表（ui.md §11.1 八
 *      token）钉住，真实注册端口消费归宿主装配批次收口（缺口登记＝
 *      单元卡 P-SEL-10）。UI-T21/T22 树/属性页域供给协议同因零 ui 边
 *      不在本批落位——同登记 P-SEL-10 收口。
 *   2. **面板面形态**：本域工作流页、目录管理页、候选表页（含淘汰
 *      原因明细区）承载于**同一主面板**（Tab 容器三页——回填入口/
 *      就绪投影是工作流页命令的直达呈现面，候选表与淘汰原因同数据
 *      分区呈现，拆分多面板会造成同数据多实例的呈现漂移），故 panels
 *      恰一条登记记录（advanced=false——主面板位）。
 *   3. **登记值类型的归属**：SelCommandDescriptor/SelPanelRegistration
 *      定义于本头（assembly/＝plugin 目标的 PUBLIC include 面——宿主
 *      装配层只见门面头即可完成登记面翻译，R-2 零插件私有头依赖）；
 *      命令 token 自持常量在 plugin/ 私有目录（实现细节——Backfill
 *      公共头传递携带 project 公共头，插件面零 project 包含〔契约
 *      测试直接访问词表钉住〕；token 逐字对账由契约测试消费计算库
 *      常量钉住——契约测试不在插件面扫描域）。
 *   4. 零业务计算逻辑（卡 §3.4 红线）：本门面是纯值聚合＋工厂闭包
 *      转接——选型计算类符号零出现（契约测试全文词表扫描钉住；
 *      该扫描为全文扫描、不剥注释——本头注释亦不书写词表符号字样）。
 *
 * 落位形态说明（文件位置）：本头位于 selection 单元的 `assembly/` 目录
 * （plugin 目标的 PUBLIC include 面——插件界面目标的装配契约头，非产品
 * include/ 扫描域；与 plugin/ 同理在零 Qt 红线的文件域之外——ird_gates
 * 第 4 步产品面扫描域为 include/**＋src/**）。实现
 * （plugin/SelectionPluginAssembly.cpp）编入 sdurws_ird_selection_plugin
 * 目标。
 *
 * 线程模型：createSelectionPluginAssembly／bind*／setServices 在装配
 * 线程调用（ui.md §10.9 同期）；面板工厂闭包仅 UI 线程调用；session()
 * 的返回引用仅 UI 线程访问（卡 §3.4）。
 */

#ifndef IRD_SELECTION_ASSEMBLY_SELECTIONPLUGINASSEMBLY_HPP
#define IRD_SELECTION_ASSEMBLY_SELECTIONPLUGINASSEMBLY_HPP

#include <functional>
#include <memory>
#include <string>
#include <vector>

class QWidget;  // 前置声明：面板工厂产物（全局域——本头不拖入 Widgets）

namespace sdurws::ird::selection {

class SelPanelModule;         // 前置声明（unique_ptr 成员——析构在 cpp）
struct SelPanelServices;      // 前置声明（setServices 引用入参——完整
                              //   类型在 plugin/SelPanelTypes.hpp）
struct SelModuleSessionState; // 前置声明（session() 返回引用）

// =====================================================================
// 挂位/域键词表（登记出处常量——本头是门面登记面的唯一书写点；回填
// 命令 token 词表载体在 plugin/SelPanelCommandCatalog.hpp〔自持〕，与
// 计算库公共常量的逐字对账由契约测试钉住）。
// =====================================================================

/// 挂位阶段 token（ui.md §6.4 七阶段第 5——"selection（选型）"，工作
/// 流七阶段"选型"阶段；selection 工作流页的宿主挂位位）。
inline constexpr const char* kSelStageToken = "selection";

/// 域注册键（ui.md §6.5 域注册词表——就绪投影行的 domainKey 值）。
inline constexpr const char* kSelDomainKey = "selection";

// =====================================================================
// 命令描述符（ui::CommandDescriptor 字段同构自持值——本域承载面）。
// =====================================================================

/**
 * @brief 域命令描述符（卡 §16 T10 行"回填入口"的登记面一行——token
 *        自持常量值，titleKey 按 ui.md §3.5 键族 cmd.<id>.title 派生）。
 *
 * 修订语义注（与 dynamics 五会话命令的差异，诚实登记）：本域登记的
 * 命令是**写命令**（经①命令端口提交后由 project 命令服务产生恰一新
 * 修订——卡 §12.1 时序），不是会话命令；故本描述符同样不携带"只读
 * 模式拒绝"位（可用性统一由命令可用性缝判定——宿主权威，插件零本地
 * 判定；写命令的可用性含"只读项目拒绝"〔卡 §12.2 纪律 9〕由宿主门
 * 控）。载荷组装归宿主装配批次（当前选中组合→回填载荷——插件面只
 * 发起意图 token，零业务组装）。值语义纯结构；线程安全。
 */
struct SelCommandDescriptor {
    std::string token;     ///< 命令 token（自持词表常量值——唯一书写点
                           ///<   在 plugin/SelPanelCommandCatalog.hpp）
    std::string titleKey;  ///< 命令标题键（"cmd.<token>.title" 键族——
                           ///<   值归宿主文案资源，UX-02）
};

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
 * 门面（SelectionPluginAssembly）创建时组装。
 * 值语义：可拷贝（工厂闭包共享门面模块——shared 语义由闭包自然承载）。
 */
struct SelPanelRegistration {
    std::string stageToken;  ///< 挂位阶段 token（kSelStageToken——§6.4 词表）
    std::string titleKey;    ///< 面板标题键（§3.5 键族——值归宿主文案资源）
    std::function<QWidget*()> factory; ///< 面板工厂（UI 线程调用）
    bool advanced = false;   ///< UX-04 高级面板标记（false＝主面板）
};

// =====================================================================
// 面板页标题键（§3.5 键族 plugin.<id>.panel.<page>.title——三页标题；
// 值归宿主文案资源，本域零文案值；键词形与 dynamics 面板页键族同构）。
// =====================================================================

/// 工作流页标题键（Tab 页一——就绪投影＋回填入口＋最近提交＋复算提示）。
inline constexpr const char* kSelWorkflowPageKey =
    "plugin.selection.panel.workflow.title";
/// 目录管理页标题键（Tab 页二——已锁定目录版本清单＋选中显示）。
inline constexpr const char* kSelCatalogPageKey =
    "plugin.selection.panel.catalog.title";
/// 候选表页标题键（Tab 页三——候选表＋淘汰原因明细区）。
inline constexpr const char* kSelCandidatesPageKey =
    "plugin.selection.panel.candidates.title";

/**
 * @brief selection 插件装配描述符（T02 登记字段＋T10 面板/命令登记面）。
 *
 * 字段取舍口径（逐字段有据，零私造词表）：
 *   - pluginId：ui.md §11.1 白名单 token "selection"（编译期/装配期
 *     常量词表，L5 应用壳固定）——插件身份的唯一书写点；
 *   - titleKey：ui.md §3.5 键族 plugin.<id>.title 的 "plugin.selection.
 *     title"（UX-02：token 本身不进用户文本——用户见中文标题，值归
 *     ui 文案资源，本域不携带值）；
 *   - stageToken：ui.md §6.4 七阶段第 5 token（kSelStageToken）；
 *   - readinessDomainKey：ui.md §6.5 域注册键（kSelDomainKey——宿主
 *     汇聚翻译对账锚）；
 *   - commands：域命令描述符（回填入口一条——目录现产）；
 *   - panels：一条主面板登记记录（工作流页＋目录管理页＋候选表页
 *     合一 Tab——advanced=false；工厂闭包转接模块 createPanel）。
 */
struct SelectionPluginDescriptor {
    /// 插件身份（ui.md §11.1 白名单 token——"selection"；显示名永不
    /// 替代身份，ARC-04 纪律）。
    std::string pluginId;
    /// 标题文案键（ui.md §3.5 键族 plugin.<id>.title——值归 ui 文案
    /// 资源文件，本域零文案值）。
    std::string titleKey;
    /// 挂位阶段 token（§6.4 词表——kSelStageToken 值）。
    std::string stageToken;
    /// 就绪投影域注册键（§6.5——kSelDomainKey 值）。
    std::string readinessDomainKey;
    /// 随装配登记的命令描述符（回填入口——SelCommandDescriptor）。
    std::vector<SelCommandDescriptor> commands;
    /// 阶段面板登记记录（一条主面板——见类型注"面板面形态"）。
    std::vector<SelPanelRegistration> panels;
};

/**
 * @brief selection 插件装配门面（描述符＋模块的装配载体——宿主装配层
 *        与开发 harness 的消费入口；WP-17-T09 dynamics 同款结构）。
 *
 * 生命周期：工厂返回值（移动语义）；模块归门面 unique_ptr 持有；
 * 面板工厂闭包捕获的模块指针存活期由装配层保证（宿主在面板创建期
 * 与面板存活期保持门面存活——ui.md §10.9 装配时序同款纪律）。
 */
class SelectionPluginAssembly {
public:
    SelectionPluginAssembly() = default;
    ~SelectionPluginAssembly();  // 析构/移动在 cpp（unique_ptr 不完整类型
                                 //   SelPanelModule——Pimpl 手法：特殊成员
                                 //   函数的定义点需完整类型，故声明在此、
                                 //   定义在实现 TU）
    SelectionPluginAssembly(SelectionPluginAssembly&&) noexcept;
    SelectionPluginAssembly& operator=(SelectionPluginAssembly&&) noexcept;
    SelectionPluginAssembly(const SelectionPluginAssembly&) = delete;
    SelectionPluginAssembly& operator=(const SelectionPluginAssembly&) = delete;

    /// 装配描述符（工厂填充——两登记字段＋T10 面板/命令登记面）。
    SelectionPluginDescriptor descriptor;

    /**
     * @brief 注入服务缝（面板创建前调用生效——创建后注入仅影响后建
     *        面板；装配层一次性组装全缝）。
     *
     * @param services [in] 服务缝聚合（整体替换语义；逐缝语义与空缝
     *                 降级见 SelPanelTypes.hpp）
     */
    void setServices(const SelPanelServices& services);

    /**
     * @brief 绑定回填提交出口（面板创建前后皆可——宿主 CommandRegistry
     *        与①命令端口的注册权威归宿主，本出口只是插件侧转发缝）。
     *
     * @param submit [in] 提交函数（token→宿主命令管线；置空＝解除）
     */
    void bindBackfillSubmit(std::function<void(const std::string&)> submit);

    /**
     * @brief 绑定回填可用性查询（统一按钮门控——宿主权威判定）。
     *
     * @param availability [in] 可用性函数（token→可用位；置空＝解除）
     */
    void bindBackfillAvailability(
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
    SelModuleSessionState& session();

    /// @brief 会话刷新（会话事实注入后的面板同步——面板未创建＝空操作）。
    void refreshFromSession();

    /// 模块访问（测试/harness 装配面——缝注入的等价路径；产品装配层
    /// 经 setServices/bind* 即可，零触模块内部）。
    SelPanelModule* module() noexcept { return m_module.get(); }

private:
    // 工厂是描述符现产＋模块创建的唯一装配点（同 TU——访问私有成员的
    // 友元豁免；dynamics 工厂同款形态）。
    friend SelectionPluginAssembly createSelectionPluginAssembly();

    std::unique_ptr<SelPanelModule> m_module;  ///< 模块（缝/会话态/面板引用）
};

/**
 * @brief 创建 selection 插件装配门面（每次调用全新实例——无缓存）。
 *
 * T10 批次装配规则（逐字段见类型注）：pluginId＝"selection"、
 * titleKey＝"plugin.selection.title"、stageToken＝"selection"、
 * readinessDomainKey＝"selection"、commands＝回填入口目录、panels＝
 * 一条主面板记录（工厂闭包转接模块 createPanel）。
 *
 * @return 装配门面（零业务计算——纯值聚合＋工厂闭包转接）
 */
SelectionPluginAssembly createSelectionPluginAssembly();

}  // namespace sdurws::ird::selection

#endif  // IRD_SELECTION_ASSEMBLY_SELECTIONPLUGINASSEMBLY_HPP
