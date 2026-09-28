/**
 * @file   PropertyInspector.hpp
 * @brief  共享属性检查器（PropertyInspector）——全域唯一跨域属性呈现面
 *         （D5）＋域页面注册协议（PropertyPagesProvider，B1-SPEC §5.1
 *         迁移三接入面之二）：常用字段呈现面＋复杂编辑页注册/激活协议
 *         （D6）；检查器只做呈现与编排，域判定零入检查器。
 *
 * 设计依据：
 *   - 任务契约 tasks/foundation/UI-T22.json acceptance 1~3（共享检查器
 *     落位＋L1 全核查基线／复杂编辑页宿装与 D5/D6 分野断言／域判定零入
 *     检查器＋FormEditCommon 构件复用）；
 *   - B1-SPEC §2 D5（属性检查器为全域共享面板〔宿主右 Dock〕，呈现当前
 *     选中对象的常用编辑字段；唯一跨域属性呈现面，域私有大批量编辑不在
 *     此展开）、D6（DH/物性/区域定义/导入向导/求解配置等复杂编辑收口到
 *     各域复杂编辑页面——域面板内的编辑视图/对话框，不经共享检查器展开
 *     大批量字段）、§4.2 L1（项目树选中业务对象后，检查器**必须**刷新为
 *     该对象的常用字段视图）、§5.1（迁移交付面之二
 *     PropertyPagesProvider——供给本域常用字段页＋声明本域复杂编辑页面
 *     入口；本任务冻结协议形状，供 WP-13-T20/WP-14-T10/WP-15-T18 三域
 *     迁移消费）、§5.3（共享 UI 互斥——本头属 UI-T21/T22/T23 共享面）；
 *   - units/ui.md §4.1/§4.2（右栏属性编辑区——属性编辑产生的修改只能进
 *     草稿〔经域编辑器接口〕，不得就地写权威对象）、§13 UI-T22 行、
 *     §3.1（ui 产品面对业务域零链接零 include——页面内容由域 Provider
 *     以值供给，检查器对域零类型知识）；
 *   - ARCHITECTURE §7.12/SA-18（D5/D6 面板级落点；呈现与计算隔离）；
 *   - SelectionService.hpp（选中唯一汇聚点——检查器是其 L1 消费者，
 *     UI-T21 落位登记注"检查器 UI-T22 本体订阅即得刷新"）、
 *     IndustrialProjectTree.hpp（IUiTreeNodesProvider 同构先例——域
 *     供给协议的形状纪律：值供给/现取现拼/域判定在域）、
 *     FormEditCommon.hpp（QuantityFieldSpec/IFormEditOutlet——常用字段
 *     页的构件与移交面，acceptance 3 的复用面）；
 *   - knownPitfalls：P-UI-6（workflow 域就绪源单侧冻结——检查器呈现
 *     就绪/只读等**事实**但不拥有门控规则；单侧纪律延伸到页面注册协议：
 *     本协议形状是 ui 单侧冻结的谈判起点，域/workflow 侧产出后核对，
 *     不兼容时按影响面增量同步，不私改对端）；O-43（宿主右 Dock 既有
 *     承载——检查器升格不再动 Dock 拓扑，本组件只交付内容面板，宿主
 *     挂位归集成收口任务〔UI-T23/WP-24-T08〕）。
 *
 * 背景说明（D5/D6 为什么必须分野、以及为什么域判定不能进检查器）：
 *   方案 B.1 的产品形态下，"属性呈现"有两个通道——常用字段（少量高频
 *   编辑字段，就地呈现于共享检查器）与复杂编辑页（DH 参数表、物性表、
 *   区域定义向导、求解配置等大批量编辑收口页，归属各域面板）。若检查器
 *   允许展开大批量字段，右栏会变成第二编辑工作区（与域面板双头编辑同一
 *   对象，D6 收口语义失效）；若检查器内藏"这个对象属于哪个域"的判定表，
 *   则每迁移一个域都要改 ui（R-1 业务域互链禁止的呈现面翻版）。因此本
 *   头把两个通道做成**两条独立协议**：常用字段经 commonFieldsPage 供给
 *   （检查器只渲染——超量即整页拒绝，分野哨兵），复杂编辑经
 *   complexPageEntries/activateComplexPage 注册与激活（检查器只呈现入口
 *   按钮与宿装视图，编辑本体在域侧）；"对象归哪个域"由各域 Provider
 *   自答（commonFieldsPage 返回 nullopt＝非本域对象）——检查器只按注册
 *   序询问、不持有任何域判定表。
 *
 * O-31 处置（同 UI-T21 先例）：检查器对业务域零链接零 include——页面
 *   内容全部经 ui 自有协议以值/句柄供给；编辑提交经页面自带的
 *   IFormEditOutlet（域编辑器实现——它把修改转译为域命令经①命令端口
 *   提交），检查器依赖闭包零命令网关（结构承载：本头与实现 TU 不出现
 *   IUiCommandGateway，acceptance 3 的"编辑提交仍走域命令面"由结构自证
 *   ——放行权威在 project 命令边界 §9.2）。
 *
 * 线程模型：模型层全部入口只在 UI 线程调用（ui.md §3.4 M-1——选中
 *   回调/页面供给/激活编排都是 UI 线程交互面）；面板经工厂在 UI 线程
 *   构建。非线程安全。
 *
 * 生命周期/所有权：PropertyInspectorModel 由装配层持有（shared_ptr——
 *   模型与面板共享）；Provider 由装配层持有（模型持强引用登记，同
 *   ProjectTreeModel 惯例）；面板 QWidget 所有权随 Qt 父子树（工厂返回
 *   unique_ptr 把手、widget() 出口随实例——IndustrialProjectTreePanel
 *   同款惯例）；宿装复杂页视图的所有权随宿装容器父子树（Provider 以
 *   parent 构造、检查器不接管——Qt 常规）。
 */

#ifndef SDURWS_IRD_UI_PROPERTYINSPECTOR_HPP
#define SDURWS_IRD_UI_PROPERTYINSPECTOR_HPP

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <sdurws/ird/core/Identity.hpp>       // core::ObjectId（选中对象身份——CON-01，表内登记边直用）
#include <sdurws/ird/diagnostics/Catalog.hpp>  // diagnostics::IDevLogSink（Dev 级留痕通道——表内登记边；可空）
#include <sdurws/ird/ui/FormEditCommon.hpp>    // ui::QuantityFieldSpec/IFormEditOutlet（常用字段页构件与编辑移交面——acceptance 3 复用面）
#include <sdurws/ird/ui/SelectionService.hpp>  // ui::IUiSelectionObserver/SelectionChange（L1 消费面——选中唯一汇聚点的订阅缝）
#include <sdurws/ird/ui/UiPorts.hpp>           // ui::IUiNameResolver（标题显示名解析端口——UX-02，C-11 既有冻结形状零改动）

class QWidget;  // 前置声明：宿装复杂页视图与面板工厂返回（头文件不拖入 Widgets——FormEditCommon 同款）

namespace sdurws {
namespace ird {
namespace ui {

// =====================================================================
// 冻结文案（检查器呈现文案单点——模型测试与 GUI 同源消费；
// FormEditCommon/PolicySummaryCard 同款惯例：文案改动＝契约改动，
// 须升单元卡修订）
// =====================================================================

/// 无业务选中时的占位文案（kind==NoSelection——右栏常驻态，不是错误）。
inline constexpr const char* kInspectorNoSelectionText = "未选中对象";

/// 多选中时的占位文案（kind==MultiSelection——D5 呈现语义是"当前选中
/// 对象"单数；多选不展开任何字段，提示用户收窄选择）。
inline constexpr const char* kInspectorMultiSelectionText =
    "已选中多个对象（检查器呈现单个对象的常用字段）";

/// 仅运行时对象选中时的提示文案（kind==RuntimeOnly——L3 反解失败的
/// 暂态透传：呈现事实、零伪造业务字段；业务选中集未变，本文案只作提示）。
inline constexpr const char* kInspectorRuntimeOnlyText =
    "当前选中的是运行时对象（无业务编辑字段）";

/// 选中对象无任何域应答时的占位文案（kind==NoProviderAnswer——迁移期
/// 常态：域尚未接入 Provider，检查器如实呈现"暂无"，不虚构字段也不报错
/// ——B1-SPEC §5.2 双形态并存的检查器侧承载）。
inline constexpr const char* kInspectorNoProviderAnswerText =
    "该对象暂无可呈现的常用字段（所属域未接入检查器）";

/// 常用字段页被拒绝时的占位文案（kind==PageRejected——分野哨兵触发：
/// 页供给违约〔超量/空集/基线键未知〕整页拒绝，原因 token 走 Dev 留痕，
/// 用户面只见工程化占位——拒绝优于残缺，ProjectTreeModel 同款纪律）。
inline constexpr const char* kInspectorPageRejectedText =
    "常用字段页不可用（页面供给违约，已记录开发日志）";

/// 只读呈现徽标文案（view.readOnly==true 时标题行附加——P-UI-6 单侧
/// 纪律：只读是页面供给的**事实**，检查器只呈现；可编辑性的门控判定
/// 权威归 project 命令边界，检查器不二次判定）。
inline constexpr const char* kInspectorReadOnlyBadgeText = "只读";

/// 复杂编辑入口区的标题文案（D6——复杂编辑不在检查器展开字段，只呈现
/// 收口页入口）。
inline constexpr const char* kInspectorComplexSectionText = "复杂编辑";

/// 宿装视图未返回时的防御占位文案（hosted 页激活返回空——Provider 契约
/// 违约的呈现面兜底；原因 token 走 Dev 留痕）。
inline constexpr const char* kInspectorHostedPageMissingText =
    "页面未返回视图（装配缺陷，已记录开发日志）";

// =====================================================================
// 常用字段页值与分野哨兵（D5 供给面——检查器只渲染，不裁剪不补页）
// =====================================================================

/**
 * @brief 常用字段页的单字段基线值（现取现拼的值载体）。
 *
 * 字段规格（键/标签/量纲/单位/约束）由页面 fields 携带；基线值按键寻址
 * 注入 ParamEditModel（FormEditCommon 编辑会话模型——三层值语义的基线
 * 层）。nullopt＝该字段未设值（呈现 kFieldUnsetText 占位，不伪造 0）。
 */
struct InspectorFieldValue {
    /// 字段稳定键（须等于 fields 中同键 QuantityFieldSpec.key——违约整页
    /// 拒绝，reason "baseline-key-unknown"）。
    std::string key;
    /// 基线值（SI 真值；nullopt＝未设——呈现占位）。
    std::optional<double> siValue;
};

/**
 * @brief 域供给的常用字段页（D5——Provider::commonFieldsPage 的返回值）。
 *
 * 页面违约的封闭词表（整页拒绝，拒绝优于残缺）：
 *   - "empty-common-fields"：fields 为空（常用字段页至少一字段——空页
 *     供给无呈现语义，属 Provider 装配缺陷）；
 *   - "common-fields-over-limit"：fields 数量 > kMaxCommonFieldsPerObject
 *     （**D5/D6 分野哨兵**——大批量字段必须走 D6 复杂编辑页通道，塞进
 *     常用字段通道即违约；这是 acceptance 2 分野断言的数据面）；
 *   - "baseline-key-unknown"：values 中出现 fields 未声明的键（基线无法
 *     寻址注入——数据不一致比缺数据更危险，整页拒绝）。
 */
struct CommonFieldsPage {
    /// 页标题（域供给工程用语——UX-02；如"连杆常用参数"）。
    std::string title;
    /// 只读呈现事实（P-UI-6 单侧纪律——检查器只呈现该事实〔只读徽标＋
    /// 编辑出口收口〕，不做也不做不了任何门控判定；放行权威归 project）。
    bool readOnly = false;
    /// 常用字段集（注册序＝呈现序——NFR-COR-02；非空且不超分野哨兵上限）。
    std::vector<QuantityFieldSpec> fields;
    /// 字段基线值（键寻址注入；可空集＝全部字段未设——合法常态）。
    std::vector<InspectorFieldValue> values;
    /// 域编辑器出口（确认应用的移交面——域编辑器把它转译为域命令经①命令
    /// 端口提交；可空＝纯呈现页〔apply 禁用，kNoOutletTooltip——不虚构
    /// 可用性〕。readOnly==true 时检查器**忽略**本出口——只读页不提供
    /// 编辑移交面，两事实矛盾时以只读为准〔保守收口〕）。
    IFormEditOutlet* editOutlet = nullptr;
};

/// D5/D6 分野哨兵：单个对象的常用字段数上限（超过即整页拒绝——常用
/// 字段通道只承载"少量高频"字段；大批量编辑必须走 D6 复杂编辑页通道）。
/// 取值 16＝一屏参数组量级（4×4），与 FormEditCommon 高级面板三字段/
/// 参数表几十行量级之间取检查器呈现的合理上限；调整＝产品形态变更，
/// 须升单元卡修订。
constexpr std::size_t kMaxCommonFieldsPerObject = 16;

// =====================================================================
// 复杂编辑页注册/激活协议（D6——检查器只呈现入口与宿装，编辑本体在域）
// =====================================================================

/**
 * @brief 复杂编辑页入口声明（Provider::complexPageEntries 的行值）。
 *
 * 入口只声明"有一个收口页可激活"，不携带任何字段内容——D6 分野的协议
 * 级承载：检查器从入口值**拿不到**字段集，大批量字段想进检查器在类型
 * 面就没有通道。
 */
struct ComplexPageEntry {
    /// 页稳定键（激活寻址锚——activateComplexPage 的 pageKey 入参；
    /// 建议小写连字符词法〔"dh-parameters"/"solve-config"——与
    /// QuantityFieldSpec.key 同风格〕，协议不强制词法）。
    std::string pageKey;
    /// 入口呈现文案（域供给工程用语——UX-02；如"DH 参数""物性""区域
    /// 定义""导入向导""求解配置"）。
    std::string title;
    /// 宿装形态声明：true＝激活返回视图挂检查器宿装区（检查器内嵌呈现
    /// ——复杂页视图本体仍是域侧构建）；false＝域自持打开（域面板内
    /// 编辑视图/对话框——D6"域面板内"形态），激活后检查器零宿装。
    bool hosted = false;
};

/**
 * @brief 复杂编辑页激活结果（activateComplexPage 编排的返回报告）。
 *
 * ok==false 时 reason 携带封闭词表 token（Dev 留痕与测试断言面）：
 *   - "activation-unknown-page"：pageKey 不在该对象已声明的入口集内
 *     （编排面拒绝——面板只可能传递入口按钮绑定的键，出现未知键＝
 *     调用方违约）；
 *   - "activation-hosted-null-widget"：hosted 页激活返回空视图（Provider
 *     契约违约——声明宿装却给不出视图；面板呈现
 *     kInspectorHostedPageMissingText 防御占位）；
 *   - "activation-unexpected-widget"：非宿装页激活返回了视图（Provider
 *     契约违约——声明自持打开却回传视图；检查器不宿装不接管，Dev 留痕）；
 *   - "activation-object-not-owned"：该对象当前无域应答（选中态已漂移
 *     ——激活编排要求对象仍被应答，未应答一律拒绝，不按旧页存根激活）。
 */
struct ComplexPageActivationReport {
    /// 是否成功（true＝宿装视图已产出或自持页已打开〔编排面视角〕）。
    bool ok = false;
    /// 拒绝/异常原因（ok==false 时非空；封闭词表——见结构注释）。
    std::string reason;
    /// 宿装视图（hosted 页且 ok==true 时非空——已以入参 parent 为父；
    /// 外观归 Provider、父权随宿装容器）。自持页恒为空。
    QWidget* hostedWidget = nullptr;
};

// =====================================================================
// 域页面供给协议（B1-SPEC §5.1 PropertyPagesProvider——形状本任务冻结）
// =====================================================================

/**
 * @brief 域属性页供给协议（B1-SPEC §5.1 迁移三接入面之二——形状随本
 *        任务冻结，供 WP-13-T20/WP-14-T10/WP-15-T18 三域迁移消费）。
 *
 * 协议纪律（冻结面——三域迁移按此实现，不改签名；P-UI-6 单侧冻结：
 * 本形状是 ui 单侧给定的谈判起点，域侧产出后核对，不兼容时按影响面
 * 增量同步——不私改对端语义）：
 *   - domainKey：域注册键（"modeling"/"requirements"/"kinematics"——与
 *     IUiTreeNodesProvider.domainKey 同一词表口径）；同键重复注册在登记
 *     边界拒绝（装配缺陷 fail-fast）；
 *   - 域判定在域（acceptance 3）：对象是否属于本域由 Provider 自答——
 *     commonFieldsPage 返回 nullopt 且 complexPageEntries 返回空集＝
 *     非本域对象；检查器只按注册序询问、取首个应答者（first-wins），
 *     自身不持有任何"对象→域"判定表；
 *   - 现取现拼（IUiTreeNodesProvider.treeNodes 同款）：页面值在每次询问
 *     时现调现拼（不缓存域侧数据——域修订后选中刷新即见新内容）；实现
 *     应快速返回（UI 线程呈现路径——NFR-PERF-01，长查询由域侧自缓存）。
 *
 * 实现方（域迁移任务）义务：只供给**本域**对象（伪造他域对象应答＝范围
 * 越界，验收对抗项）；字段语义（键/标签/量纲/约束）是域单元的权威——
 * ui 只按 QuantityFieldSpec 执行 FormEditCommon 公共编辑规则，不解释
 * 业务含义；复杂编辑页的字段内容零进协议（入口声明不携带字段集——D6
 * 分野的类型级承载）。
 */
class IUiPropertyPagesProvider {
public:
    virtual ~IUiPropertyPagesProvider() = default;

    /// @brief 域注册键（"modeling"/"requirements"/"kinematics"——注册
    ///         边界按此查重；返回值约定为常量字面词，跨调用稳定）。
    virtual std::string domainKey() const = 0;

    /**
     * @brief 供给选中对象的常用字段页（D5——现取现拼，值拷贝）。
     *
     * @param object [in] 当前选中的业务对象身份
     * @return 常用字段页；非本域对象 → nullopt（域判定在域——**合法
     *         二态**，不是错误；检查器顺延询问下一注册者）
     *
     * @note UI 线程调用；应快速返回（呈现路径——NFR-PERF-01）。返回页
     *       须满足 CommonFieldsPage 注释的三条完整性（非空/不超哨兵/
     *       基线键闭合）——违约整页拒绝（不裁剪不补页）。
     */
    virtual std::optional<CommonFieldsPage>
    commonFieldsPage(const core::ObjectId& object) const = 0;

    /**
     * @brief 声明选中对象的复杂编辑页入口集（D6——现取现拼）。
     *
     * @param object [in] 当前选中的业务对象身份
     * @return 入口声明集（可空＝本对象无复杂编辑入口〔合法常态〕；非
     *         本域对象返回空集——与 commonFieldsPage 的 nullopt 同答）。
     *         pageKey 在集内唯一（同键双入口＝声明违约，模型拒绝该页并
     *         Dev 留痕——reason "duplicate-page-key"）
     *
     * @note UI 线程调用；应快速返回（入口声明是轻量值集——零字段内容）。
     */
    virtual std::vector<ComplexPageEntry>
    complexPageEntries(const core::ObjectId& object) const = 0;

    /**
     * @brief 激活一个复杂编辑页（D6 编排出口——检查器入口按钮触发）。
     *
     * 实现语义随入口的 hosted 声明分流：
     *   - hosted==true：构建复杂页视图（以 parent 为父——Qt 父子树接管
     *     所有权）并返回；返回空视图＝契约违约（呈现侧防御占位＋Dev
     *     留痕）；
     *   - hosted==false：域自持打开编辑视图/对话框（D6"域面板内"形态
     *     ——打开动作在本调用内完成），返回 nullopt；返回非空视图＝
     *     契约违约（检查器不宿装不接管，Dev 留痕）。
     *
     * @param object  [in] 目标业务对象身份（页内容的编辑主体）
     * @param pageKey [in] 目标页稳定键（须在该对象已声明的入口集内）
     * @param parent  [in] 宿装父控件（hosted 页的 Qt 父——自持页忽略）
     * @return 激活报告（ok/reason/hostedWidget——见结构注释的封闭词表）
     *
     * @note UI 线程调用；宿装页构建应轻量（复杂页的大数据加载由页自身
     *       异步编排——UI 线程零长计算 NFR-PERF-01）。
     */
    virtual ComplexPageActivationReport
    activateComplexPage(const core::ObjectId& object, const std::string& pageKey,
                        QWidget* parent) = 0;
};

// =====================================================================
// 呈现状态值（模型→面板的唯一渲染数据源——只读值聚合）
// =====================================================================

/**
 * @brief 检查器呈现状态词表（PropertyInspectorView.kind 的封闭词表）。
 *
 * 六态互斥（一次呈现恰为一态）：前四态是占位呈现（零字段渲染），
 * ObjectFields 是正常字段页，PageRejected 是防御占位（复杂编辑入口
 * **照常呈现**——分野两通道互不遮蔽：常用字段通道被拒不剥夺复杂编辑
 * 通道，大批量编辑始终有正确去处）。
 */
enum class InspectorContentKind : std::uint8_t {
    /// 无业务选中（空选中集——右栏常驻态）。
    NoSelection,
    /// 多选中（选中集 >1——D5 单对象呈现语义，提示收窄）。
    MultiSelection,
    /// 仅运行时对象选中（L3 反解失败暂态透传——业务呈现零触碰）。
    RuntimeOnly,
    /// 选中对象无任何域应答（迁移期常态——B1-SPEC §5.2 双形态并存的
    /// 诚实占位，不虚构字段不报错）。
    NoProviderAnswer,
    /// 常用字段页正常呈现（业务选中恰一对象且有域应答且页面完整）。
    ObjectFields,
    /// 常用字段页被拒绝（页面供给违约——防御占位＋复杂编辑入口照常）。
    PageRejected,
};

/**
 * @brief 检查器当前呈现状态（模型组装、面板渲染的唯一数据源——只读值）。
 *
 * 字段有效性按 kind 分态：objectId/domainKey/title/readOnly/fields/
 * values/editOutlet 仅在 kind==ObjectFields 或 PageRejected〔拒绝页仍
 * 携带 objectId 与供给域键供留痕对账；其余呈现字段无效〕时有意义；
 * complexEntries 在对象有域应答时有效（两通道独立组装）。零虚构纪律：
 * 无应答时 complexEntries 为空集，面板按 kind 呈现占位——不虚构字段、
 * 不虚构入口。
 */
struct PropertyInspectorView {
    /// 当前呈现态（六态封闭词表——见枚举注释）。
    InspectorContentKind kind = InspectorContentKind::NoSelection;
    /// 呈现对象身份（ObjectFields/PageRejected 时有效）。
    core::ObjectId objectId{};
    /// 供给域键（first-wins 应答者——呈现溯源；检查器不参与域判定，
    /// 只透传应答者自报的键）。
    std::string domainKey;
    /// 页标题（页面供给原文——零加工）。
    std::string title;
    /// 只读呈现事实（页面供给原文——P-UI-6 透传，检查器不判定）。
    bool readOnly = false;
    /// 常用字段集（页面供给原文——注册序；ObjectFields 时有效）。
    std::vector<QuantityFieldSpec> fields;
    /// 字段基线值（页面供给原文——键寻址注入 ParamEditModel）。
    std::vector<InspectorFieldValue> values;
    /// 编辑移交出口（readOnly==true 时恒为空——只读页不提供移交面；
    /// 可空＝纯呈现页〔apply 禁用〕；所有权归 Provider 侧——裸指针，
    /// 调用方保证存活期覆盖呈现期）。
    IFormEditOutlet* editOutlet = nullptr;
    /// 复杂编辑入口集（应答域声明——D6；注册序＝呈现序）。
    std::vector<ComplexPageEntry> complexEntries;
    /// 页面拒绝原因（PageRejected 时非空；封闭词表——见 CommonFieldsPage/
    /// ComplexPageEntry 注释；其余态为空串）。
    std::string rejectReason;
};

// =====================================================================
// PropertyInspectorModel——检查器模型（注册编排＋L1 刷新＋分野哨兵；
// 零 Qt——模型层可测）
// =====================================================================

/**
 * @brief 共享属性检查器模型（acceptance 1/2/3 的模型半区）。
 *
 * 职责（只做呈现编排——域判定零入）：
 *   - Provider 注册（装配期静态登记——SA-01 精神，无运行期注销通道；
 *     同 ProjectTreeModel 惯例）；
 *   - L1 刷新（实现 IUiSelectionObserver——订阅 SelectionService 即得
 *     选中变更）：业务选中恰一对象→按注册序询问 Provider→首个应答者
 *     组装呈现状态；多选/无选/仅运行时→对应占位态（零字段渲染）；
 *   - 分野哨兵（acceptance 2）：页面供给违约〔空集/超哨兵上限/基线键
 *     未知/入口键重复〕→ 整页拒绝（PageRejected）＋Dev 留痕——大批量
 *     字段进不了常用字段通道，而复杂编辑入口照常呈现（两通道独立）；
 *   - D6 激活编排（acceptance 2）：面板入口按钮→本模型→应答域
 *     Provider——编排面校验〔对象仍被应答、pageKey 在声明集内、宿装/
     自持形态与返回值一致〕后转调 Provider，报告透传。
 *
 * 零越界声明（结构承载，验收对抗项）：依赖闭包＝Provider 登记＋Dev
 *   日志——**零命令网关**（编辑提交经页面自带 IFormEditOutlet 出线，
 *   放行权威在 project 命令边界 §9.2）、零域判定表（对象归属由 Provider
 *   自答）、零门控规则（只读/就绪是透传的事实——P-UI-6）、零字段裁剪
 *   补页（页面违约整体拒绝）。
 *
 * 错误语义（AGENTS §3 二分）：
 *   - 调用方契约违约 → std::invalid_argument fail-fast（空 Provider/
 *     重复 domainKey 注册——静默收下会制造"两个域同时供给"的未定语义）；
 *   - 页面供给违约 → 返回值轨整体拒绝（PageRejected＋Dev 留痕——域侧
 *     数据缺陷是可修复常态，不抛）。
 *
 * 线程约束：非线程安全——仅 UI 线程访问（ui.md §3.4 M-1）。生命周期：
 *   装配层持有（shared_ptr）；Provider 强引用登记（析构序天然安全）。
 */
class PropertyInspectorModel final : public IUiSelectionObserver {
public:
    /**
     * @brief 装配依赖（装配期一次性给出——其余控制器同款纪律）。
     *
     * devLog 可空＝显式声明的无日志场景（留痕静默——SelectionService
     * Deps.devLog 同款）。
     */
    struct Deps {
        /// 开发日志通道（可空——分野哨兵/激活违约的 Dev 留痕面）。
        std::shared_ptr<diagnostics::IDevLogSink> devLog;
    };

    /**
     * @brief 构造检查器模型（空模型合法——Provider 装配前选中即
     *        NoProviderAnswer 呈现，迁移期常态）。
     *
     * @param deps [in] 装配依赖（可空缺省＝无日志场景）
     */
    explicit PropertyInspectorModel(Deps deps = {});

    // ---- Provider 注册（装配期静态登记）----

    /**
     * @brief 注册一个域页面供给者（装配期一次——选中刷新时现调其供给）。
     *
     * @param provider [in] 供给者（shared_ptr 保活由装配层负责——模型持
     *                 强引用，析构序天然安全）
     * @throws std::invalid_argument provider 为空，或 domainKey 与已注册
     *         者重复（装配缺陷——同域双供给源的页面合并语义未定义，禁收）
     */
    void addProvider(std::shared_ptr<IUiPropertyPagesProvider> provider);

    /// @brief 已注册供给者数（装配自证/测试观测面）。
    std::size_t providerCount() const noexcept;

    // ---- L1 刷新（IUiSelectionObserver——订阅 SelectionService 即得）----

    /**
     * @brief 选择已变更（L1 全核查基线的刷新入口——acceptance 1）。
     *
     * 编排（按 SelectionChange 两态分流）：
     *   - runtimeOnly==true：呈现状态**零触碰**（业务选中集未变——L3
     *     反解失败暂态不剥夺既有字段呈现；具名测试承载）；
     *   - 选中集为空 → kind=NoSelection；选中集 >1 → kind=MultiSelection
     *     （D5 单对象语义——多选零字段渲染）；恰一对象 → 询问 Provider
     *     组装（首个应答者 first-wins；无应答 → NoProviderAnswer 呈现——
     *     迁移期常态，不报错）；
     *   - 应答页完整性三查（非空/哨兵/基线键闭合）任一不过 → 整页拒绝
     *     （PageRejected＋rejectReason＋Dev 留痕）。
     *
     * @param change [in] 变更事实（SelectionService 广播值——值拷贝）
     */
    void onSelectionChanged(const SelectionChange& change) override;

    // ---- 呈现状态查询（面板渲染数据源——只读）----

    /// @brief 当前呈现状态（最近一次刷新的组装产物——值拷贝读出）。
    const PropertyInspectorView& view() const noexcept;

    // ---- D6 激活编排（面板入口按钮→模型→应答域 Provider）----

    /**
     * @brief 激活选中对象的指定复杂编辑页（acceptance 2 的编排半区）。
     *
     * 编排序：①呈现态须为对象应答态（ObjectFields/PageRejected——其余
     * 态无应答域可转调，返回 "activation-object-not-owned"）；②pageKey
     * 须在该对象已声明入口集内（否则 "activation-unknown-page"）；③
     * 转调应答域 Provider——宿装/自持形态与返回值一致性由模型核对
     * （违约词表见 ComplexPageActivationReport）。
     *
     * @param object  [in] 目标对象身份（须与当前呈现对象一致——面板只
     *                会传递自身入口按钮绑定的对象，错配即调用方违约）
     * @param pageKey [in] 目标页稳定键（入口按钮绑定值）
     * @param parent  [in] 宿装父控件（hosted 页的 Qt 父——自持页忽略）
     * @return 激活报告（宿装视图已以 parent 为父；所有权随 Qt 父子树）
     */
    ComplexPageActivationReport
    activateComplexPage(const core::ObjectId& object, const std::string& pageKey,
                        QWidget* parent);

private:
    /// @brief Dev 留痕（允许为空＝显式声明的无日志场景）。
    void emitDevLine(const std::string& message) const;

    /// @brief 以单个对象询问 Provider 并组装呈现状态（first-wins＋三查）。
    void assembleForObject(const core::ObjectId& object);

    /// 装配依赖（devLog 可空）。
    Deps m_deps;
    /// 已注册供给者（注册序＝询问序——first-wins 的裁决序，NFR-COR-02）。
    std::vector<std::shared_ptr<IUiPropertyPagesProvider>> m_providers;
    /// 当前呈现状态（onSelectionChanged/activate 编排的唯一写点产物）。
    PropertyInspectorView m_view;
    /// 首个应答者在注册表中的位序（D6 激活编排的转调目标——注册无注销
    /// 通道，位序登记后稳定）；m_hasAnswer==false 时无意义。
    std::size_t m_answerIndex = 0;
    /// 当前呈现是否由某个 Provider 应答（激活编排前置——占位态无转调面；
    /// NoProviderAnswer/PageRejected 等态的应答事实独立于呈现语义记录）。
    bool m_hasAnswer = false;
};

// =====================================================================
// 面板装配缝（Qt 渲染层——模型的 Widget 呈现；工厂形态零 Q_OBJECT）
// =====================================================================

class PropertyInspectorPanel;  // 前置声明（工厂返回类型——实现封闭在库内，R-2）

/**
 * @brief 面板装配依赖（createPropertyInspectorPanel 一次性给出）。
 *
 * model/nameResolver 必填非空（工厂校验——无模型的面板没有呈现语义、
 * 无名称端口的标题行只能拼名称〔R-4 禁止〕，均属装配缺陷构造期拒绝）。
 */
struct PropertyInspectorPanelDeps {
    /// 检查器模型（shared 共享——模型刷新后面板 refresh 即见新状态）。
    std::shared_ptr<PropertyInspectorModel> model;
    /// 名称解析端口（标题行显示名唯一来源——C-11 既有端口零新形状；
    /// 解析失败显示占位，不拼接名称——UX-02/R-4）。
    std::shared_ptr<IUiNameResolver> nameResolver;
};

/**
 * @brief 共享属性检查器面板把手（内容控件的类型化出口——实现类封闭在
 *        库内，R-2）。
 *
 * 面板行为（L1 的呈现半区＋D6 宿装面）：
 *   - 渲染：refresh() 以 model->view() 为唯一事实源分态呈现——占位态
 *     （NoSelection/MultiSelection/RuntimeOnly/NoProviderAnswer/
 *     PageRejected）呈现对应冻结文案；ObjectFields 呈现标题行（显示名
 *     经 nameResolver 现取〔失败占位——UX-02〕＋只读徽标〔事实透传〕）＋
 *     常用字段区（**FormEditCommon 参数表面板构件复用**——acceptance 3：
 *     以页面 fields+values 装配 ParamEditModel、页面 editOutlet 作移交
 *     出口，公共编辑规则零第二实现）＋复杂编辑入口区（D6 入口按钮——
 *     零字段内容可渲染）；
 *   - D6 宿装：入口按钮点击→model->activateComplexPage(object, key,
 *     宿装容器)——hosted 页返回的视图挂入宿装区（切换页面前清空上一
 *     页视图）；自持页零宿装（域自行打开——检查器呈现面不动）；
 *   - 模型刷新后由装配层调用 refresh() 同步呈现（不自动监听——刷新
 *     时机归装配编排，IndustrialProjectTreePanel 同款口径）。
 *
 * 生命周期/所有权：实例 unique_ptr（工厂移交）；widget() 出口的 Qt 控件
 *   所有权随实例；宿装页视图所有权随宿装容器父子树（实例析构连带清空
 *   ——Provider 不被要求持有视图回引）。
 */
class PropertyInspectorPanel {
public:
    virtual ~PropertyInspectorPanel() = default;

    /// @brief 内容控件出口（挂入宿主容器/测试驱动用；实例存活期内有效）。
    virtual QWidget* widget() = 0;

    /**
     * @brief 以模型当前状态全量重渲染（onSelectionChanged 后的呈现同步
     *        动作——占位态/字段页/入口区/宿装区一次性对齐模型事实源；
     *        宿装区在非对象态下清空——呈现面不残留已失效对象的宿装页）。
     */
    virtual void refresh() = 0;
};

/**
 * @brief 构建共享属性检查器面板（Qt 渲染层——模型状态的 Widget 呈现；
 *        FormEditCommon 构件复用＋D6 入口宿装；选中经模型订阅
 *        SelectionService——L1 链路的呈现末端）。
 *
 * @param deps   [in] 装配依赖（model/nameResolver 必填非空，缺失抛
 *               std::invalid_argument）
 * @param parent [in] Qt 父控件（可空——widget() 出口所有权随实例；
 *               挂入宿主容器时把 widget() 重挂父子即可）
 * @return 面板把手（unique_ptr——实现类封闭在库内，R-2 同工厂惯例；
 *         零 Q_OBJECT——信号接线全经 lambda，AUTOMOC 口径不变）
 *
 * @throws std::invalid_argument deps 任一成员为空
 */
std::unique_ptr<PropertyInspectorPanel> createPropertyInspectorPanel(
    const PropertyInspectorPanelDeps& deps, QWidget* parent);

}  // namespace ui
}  // namespace ird
}  // namespace sdurws

#endif  // SDURWS_IRD_UI_PROPERTYINSPECTOR_HPP
