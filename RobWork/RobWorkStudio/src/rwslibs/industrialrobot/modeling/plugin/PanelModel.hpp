/**
 * @file   PanelModel.hpp
 * @brief  建模面板呈现模型（零 Qt）——五区信息架构的投影层（卡 §9.7.1）。
 *
 * 设计依据：
 *   - units/modeling.md §9.7/§9.7.1（五区面板组成与信息架构；MDL-07）、
 *     §9.7.4（面板不缓存模型权威数据——每次投影从工作集/查询端口取值）、
 *     §3.4（编辑器/工作集仅 UI 线程访问）、§7.2（DH 权威下轴/原点灰显只读）、
 *     §4.4/§4.5/§4.7（工具/场景/传动投影字段表）、§2.5/D-MDL-10（预览页
 *     仅基于已应用修订——编辑态即时 XML 预览不提供）
 *   - units/ui.md §4.2/§6.6（UX-02 工程用语：节点只显示 localName＋工程
 *     用语标签，零哈希/Schema/插件名）
 *   - 需求 MDL-07（参数化编辑界面）、UX-02（零内部标识呈现）、D-MDL-10
 *   - 任务契约 tasks/foundation/WP-13-T15.json acceptance 2/5
 *
 * 背景说明：插件界面是建模计算库的唯一交互前端，只消费计算库公共接口与
 * ui 平台契约，不持有计算逻辑与业务判定（ARCH §3.3）。本头把"界面该显示
 * 什么"的全部信息架构决策实现为**纯函数投影**：输入＝工作集/就绪报告等
 * 计算库值，输出＝视图行数据。投影无副作用、无缓存成员（ACC5"防 UI 副本
 * 成为第二真值"的结构性保证——每次调用现场重算，同输入同输出，NFR-COR-02）。
 *
 * ★ P-MDL-8 处置（契约 note ③）：本头消费的 ui 对端契约以 ui.md 当前文本
 *   为基线——UX-02 守卫复用已落位的 ui::UiText::ensureNoInternalIdentity；
 *   ui SelectionModel/IPluginUiRegistrar 等未落位面的接缝见 PanelSelection/
 *   PanelCommandCatalog 各自头注。对端落位后按 R-MDL-1 增量同步（单元卡
 *   §14.6 变更记录登记）。
 *
 * 线程约束：全部函数为纯函数（无共享可变状态），可重入；但**入参工作集
 * 本身仅 UI 线程可变**（§3.4——调用方保证传参期间无并发写）。确定性：
 * 同输入→同输出字节（NFR-COR-02；无 locale/时钟/环境读取）。
 */

#ifndef IRD_MODELING_PLUGIN_PANELMODEL_HPP
#define IRD_MODELING_PLUGIN_PANELMODEL_HPP

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <sdurws/ird/core/Identity.hpp>      // core::ObjectId（节点锚/定位跳转目标）
#include <sdurws/ird/core/Provenance.hpp>    // core::ProvenanceKind（连杆物性来源徽标）
#include <sdurws/ird/modeling/Readiness.hpp> // modeling::ModelReadinessReport（就绪诊断条数据源）
#include <sdurws/ird/modeling/RobotDesign.hpp>  // modeling::RobotDesign/JointEntry/LinkEntry/AuthorityMode
#include <sdurws/ird/modeling/Template.hpp>  // modeling::ModelingWorkingSet（②查询端口工作集值）

namespace sdurws::ird::modeling {

// =====================================================================
// 建模结构树投影（左栏对象树的建模节点——卡 §9.7.1 第一行）
// =====================================================================

/**
 * @brief 树节点类型词表（结构树的分组/叶子两级——投影判别用，非存储枚举）。
 *
 * 词表固定为卡 §9.7.1 行"模型根→基座安装→关节链（串联序）→连杆→工具/
 * 场景/位姿集/传动分组"的节点类别；呈现文案归 widget 层（键即契约——
 * ui.md §3.5 同案），本枚举只承载结构判别。
 */
enum class StructureNodeKind {
    ModelRoot,      ///< 模型根（robot-design 根对象）
    BaseInstall,    ///< 基座安装（BasePlacement 节点——MDL-22）
    Joint,          ///< 关节（链序叶子）
    Link,           ///< 连杆（链序叶子）
    ToolsGroup,     ///< 工具分组（toolRefs 引用的 tool-definition 对象）
    SceneGroup,     ///< 场景分组（sceneRefs 引用的 scene-object 对象）
    PoseSetGroup,   ///< 位姿集分组（named-pose-set——至多一份）
    DrivetrainGroup ///< 传动分组（robot-drivetrain——至多一份）
};

/**
 * @brief 建模结构树的一个节点行（纯值——widget 层 QTreeWidget 的行数据源）。
 *
 * ★ 节点锚＝ObjectId（卡 §9.7.1 行原文）：树/属性区/就绪条/三维拾取四方
 * 以同一 ObjectId 关联（L-1 选中联动的关联键）。显示面＝localName＋工程
 * 用语标签（UX-02）——构造即经 ensureNoInternalIdentity 守卫，哈希/内部
 * token 形态的串不可能进入 displayLabel（守卫抛错即实现缺陷 fail-fast）。
 *
 * 线程安全：纯值类型。
 */
struct StructureNode {
    StructureNodeKind kind = StructureNodeKind::ModelRoot;  ///< 节点类别
    std::optional<core::ObjectId> objectId;  ///< 节点锚（分组节点无对象身份＝nullopt——分组仅是呈现折叠）
    std::string localName;                   ///< 对象局部名（UX-02 呈现第一要素；分组节点为空）
    std::string displayLabel;                ///< 工程用语标签（已过 UX-02 守卫——零哈希/Schema/插件名）
    std::size_t chainIndex = 0;              ///< 链序下标（关节/连杆叶子有效——串联序呈现序）

    bool operator==(const StructureNode& o) const
    {
        return kind == o.kind && objectId == o.objectId && localName == o.localName
            && displayLabel == o.displayLabel && chainIndex == o.chainIndex;
    }
    bool operator!=(const StructureNode& o) const { return !(*this == o); }
};

/**
 * @brief 构建建模结构树投影（§9.7.1 第一行——模型根→基座安装→关节链→
 *        连杆→工具/场景/位姿集/传动分组）。
 *
 * 结构与呈现规则：
 *   - 节点序＝卡面固定序（根、基座、逐关节〔链序〕、逐连杆〔链序〕、
 *     工具组、场景组、位姿集组、传动组）；组内子节点按 localName 字典序
 *     稳定排列（NFR-COR-02 界面延伸——组内容随引用表变化时行序不漂移）。
 *   - displayLabel＝"工程用语标签"（如 "关节 3（旋转）"），数值下标来自
 *     链序；标签模板在本函数内固定（确定性来源——同输入同标签）。
 *   - UX-02 守卫：每个 displayLabel 过 ui::ensureNoInternalIdentity——
 *     localName 若为哈希形态（64 位十六进制串等）即抛错（调用方错误：
 *     非法模型名不应到达面板；构造边界已拒）。
 *
 * @param ws [in] 工作集（只读；仅 UI 线程可变——§3.4）
 * @return 树节点行序列（行序＝呈现序；纯投影——不缓存）
 *
 * 纯函数；确定性；不抛（UX-02 守卫抛 std::invalid_argument 除外——
 * 见 ensureNoInternalIdentity 契约；那是非法输入的 fail-fast）。
 */
std::vector<StructureNode> buildStructureTree(const ModelingWorkingSet& ws);

// =====================================================================
// 属性编辑区投影（右栏——卡 §9.7.1 第二行：选中只显示相关属性，MDL-07）
// =====================================================================

/**
 * @brief 属性字段的编辑使能三态（灰显只读的表达——L-7/DH 权威共用）。
 *
 * Editable＝可编辑控件；ReadOnlyGrey＝灰显只读（DH 权威下轴/原点——§7.2；
 * 或只读会话 writable=false——L-7）；Hidden＝该选中类型不显示此字段
 * （MDL-07"只显示相关属性"）。
 */
enum class FieldEnablement {
    Editable,     ///< 可编辑
    ReadOnlyGrey, ///< 灰显只读（值仍投影显示——灰显不是隐藏）
    Hidden        ///< 不显示（选中类型无关字段）
};

/**
 * @brief 属性区的一个字段行（纯值——属性表单的行数据源）。
 *
 * valueText/unitText 两串分离（UX-05"数值＋单位同显"的投影半区；换算与
 * 拼接归 widget 层经 ui FormEditCommon/core displayValueIn——本层不重复
 * 实现单位换算，SA-12 单一换算入口纪律）。provenance＝来源徽标枚举
 * （连杆物性行携带；无关字段为 nullopt）。
 */
struct PropertyFieldRow {
    std::string fieldKey;        ///< 字段键（呈现与测试判别；固定词表——见 .cpp 产出行序）
    std::string valueText;       ///< 值文本（确定性文本化；缺失态＝"未提供"占位——不伪造数值）
    std::string unitText;        ///< 单位文本（rad/m/kg/kg·m² 等——随字段；无量纲字段为空）
    FieldEnablement enablement = FieldEnablement::Editable;  ///< 使能三态
    std::optional<core::ProvenanceKind> provenance;  ///< 来源徽标（ValueProvenance 投影；无关＝nullopt）

    bool operator==(const PropertyFieldRow& o) const
    {
        return fieldKey == o.fieldKey && valueText == o.valueText && unitText == o.unitText
            && enablement == o.enablement && provenance == o.provenance;
    }
    bool operator!=(const PropertyFieldRow& o) const { return !(*this == o); }
};

/**
 * @brief 选中对象的类别判别（属性区按类别投影的输入——L-1 选中联动的
 *        属性区半区输入）。
 *
 * 选中锚＝ObjectId；本函数在五对象表＋根对象内解析该身份的类别与位置
 * （关节/连杆按下标，部件对象按对象值表序）。未命中（引用了闭包外身份）
 * 返回 nullopt——属性区呈现"无选中"空态（不伪造属性行）。
 */
struct SelectedTarget {
    enum class Kind { ModelRoot, BaseInstall, Joint, Link, Tool, Scene, PoseSet, Drivetrain };
    Kind kind = Kind::ModelRoot;  ///< 选中类别
    std::size_t index = 0;        ///< 类别内下标（Joint/Link＝链序；Tool/Scene＝工作集对象表下标）
};

/**
 * @brief 解析选中身份到属性区投影目标（纯查询；L-1 属性区半区的入口）。
 *
 * @param ws       [in] 工作集（只读）
 * @param selected [in] 选中锚（树/View3D 拾取的 ObjectId——L-1）
 * @return 命中＝类别＋下标；未命中＝nullopt
 *
 * 纯函数；确定性。
 */
std::optional<SelectedTarget> resolveSelection(const ModelingWorkingSet& ws,
                                               const core::ObjectId& selected);

/**
 * @brief 按选中目标投影属性字段行（§9.7.1 第二行——MDL-07 选中只显示
 *        相关属性）。
 *
 * 逐类别字段面（卡 §9.7.1/§4.3/§4.4/§4.5/§4.7 权威）：
 *   - 关节：类型/轴/原点/零位/限位/工作范围——其中轴/原点在 StandardDH
 *     权威态＝ReadOnlyGrey（§7.2 派生只读；来源徽标 DerivedReadOnly）；
 *     速度/加速度限值属传动对象（MDL-16——§4.7），关节选中不显示（传动
 *     引用存在与否不改变本判定——"相关属性"按对象类别划界）。
 *   - 连杆：物性三行（质量 kg/质心 m/惯量 kg·m²）各带 ValueProvenance
 *     来源徽标（用户/估算/导入/目录/派生——core::ProvenanceKind 直投）＋
 *     几何引用行（visual/collision 有无）。
 *   - 工具：安装接口/TCP 列表（逐 TCP 行：key＋displayName——§4.4）。
 *   - 场景：世界位姿（世界系固连——§4.5）/角色。
 *   - 传动：比率/摩擦/力矩行（§4.7 字段表投影——值面直投，判定零参与）。
 *   - 根/基座：显示名/权威模式/安装预设行（编辑走域命令——本区只投影）。
 *
 * @param ws   [in] 工作集（只读）
 * @param target [in] 选中目标（resolveSelection 的产出——调用方先解析）
 * @return 属性行序列（行序＝各类别固定词表序；确定性）
 *
 * 纯函数；确定性；不抛。
 */
std::vector<PropertyFieldRow> propertyFieldsFor(const ModelingWorkingSet& ws,
                                                const SelectedTarget& target);

// =====================================================================
// 就绪与诊断条投影（卡 §9.7.1 第四行：L0～L11 三组计数＋逐项定位跳转）
// =====================================================================

/**
 * @brief 就绪条的单项定位跳转行（"逐项定位跳转到树节点"的载体）。
 *
 * jumpTarget＝树节点锚（DiagnosticRecord.subject→树 ObjectId 同键关联）；
 * 无 subject 的层结论（如策略不可解析）无跳转目标＝nullopt（呈现为不可
 * 点击行——不伪造定位）。层归属轴由 layerResults 行承载（记录级无层
 * 字段——单一事实来源纪律，不私写码→层第二映射）。
 */
struct ReadinessItemRow {
    std::string severity;    ///< 组别（"blocking"/"warning"/"confirmable"/"note"——组词表固定）
    std::string summary;     ///< 人读一行（报告原文——呈现层不加工业务文案）
    std::optional<core::ObjectId> jumpTarget;  ///< 跳转目标（树节点锚；无定位＝nullopt）

    bool operator==(const ReadinessItemRow& o) const
    {
        return severity == o.severity && summary == o.summary && jumpTarget == o.jumpTarget;
    }
    bool operator!=(const ReadinessItemRow& o) const { return !(*this == o); }
};

/**
 * @brief 就绪条的层结果行（"L0～L11 分层结果"的承载——LayerDetail 保序投影）。
 */
struct LayerResultRow {
    ReadinessLayer layer = ReadinessLayer::L0Structure;  ///< 层号（L0～L11）
    bool passed = false;  ///< 该层无阻断结论（就绪报告 LayerDetail.passed 直投）
    std::string note;     ///< 层结论说明（报告原文——呈现零加工）

    bool operator==(const LayerResultRow& o) const
    {
        return layer == o.layer && passed == o.passed && note == o.note;
    }
    bool operator!=(const LayerResultRow& o) const { return !(*this == o); }
};

/**
 * @brief 就绪条的三组计数行（"Blocking/Warning/Confirmable 计数"的承载；
 *        note 组计数随附——呈现级结论在条上同显）。
 */
struct ReadinessGroupCounts {
    std::size_t blockers = 0;      ///< 阻断计数
    std::size_t warnings = 0;      ///< 警告计数
    std::size_t confirmables = 0;  ///< 待确认计数
    std::size_t notes = 0;         ///< 呈现级结论计数

    bool operator==(const ReadinessGroupCounts& o) const noexcept
    {
        return blockers == o.blockers && warnings == o.warnings && confirmables == o.confirmables
            && notes == o.notes;
    }
    bool operator!=(const ReadinessGroupCounts& o) const noexcept { return !(*this == o); }
};

/**
 * @brief 就绪诊断条投影（§9.7.1 第四行——ModelReadinessReport 的呈现面）。
 *
 * 行序：层结果行固定 L0→L11（数组下标序）；逐项行按"组序
 * blocking→warning→confirmable→note→组内报告稳定序"（报告三组本身已按
 * 层号→subject→code 稳定排序——Readiness.hpp 排序契约；本投影保序展开，
 * 不二次排序——单一排序权威纪律）。
 *
 * @param report [in] 就绪报告（IModelReadinessChecker 产出——判定权威在
 *                     计算库，本投影零判定）
 * @return 层结果行（12 行，下标 0..11 ↔ L0..L11）＋三组计数＋逐项行
 *
 * 纯函数；确定性；不抛。
 */
struct ReadinessBarProjection {
    std::array<LayerResultRow, 12> layerResults;  ///< 逐层结果（下标＝层号；L0～L11 分层结果行）
    ReadinessGroupCounts counts;                  ///< 三组计数（＋note 组随附）
    std::vector<ReadinessItemRow> items;          ///< 逐项行（组序→报告稳定序；逐项定位跳转）
};

ReadinessBarProjection projectReadinessBar(const ModelReadinessReport& report);

// =====================================================================
// 预览页纪律（卡 §9.7.1 第五行＋D-MDL-10：仅基于已应用修订）
// =====================================================================

/**
 * @brief 已应用修订视图（预览页唯一合法数据源的类型化标记）。
 *
 * 为什么需要独立类型：D-MDL-10"编辑态即时 XML 预览不提供"（草稿不编译）
 * 的强制点若只写在注释里，编译器无法拦截误用；本类型只提供"从已应用
 * 修订闭包构造"的入口（构造函数收修订身份＋快照摘要），不接受
 * ModelingWorkingSet——编辑态数据在类型层面进不了预览构建函数
 * （buildPreviewPage 只收本类型）。快照内容字节由 L5 装配层从修订闭包
 * 取得（ modeling 只消费，不触 ②端口之外的面——PA-1）。
 *
 * 线程安全：纯值类型。
 */
struct AppliedRevisionView {
    core::RevisionId revision{};  ///< 预览基线修订（空值＝无已应用修订——预览页空态）
    std::string summaryText;      ///< 已应用快照的人读摘要（如 WC/DWC XML 外供预览素材——MDL-20 外供内容）

    bool operator==(const AppliedRevisionView& o) const
    {
        return revision == o.revision && summaryText == o.summaryText;
    }
    bool operator!=(const AppliedRevisionView& o) const { return !(*this == o); }
};

/**
 * @brief 预览页投影（§9.7.1 第五行——WC/DWC XML 导出预览＋模型几何查看
 *        的只读数据面；编辑态即时 XML 预览不提供——D-MDL-10）。
 *
 * @param applied [in] 已应用修订视图（唯一数据源——类型强制）
 * @return 预览页行（摘要文本按行拆分；空修订＝空页——不伪造内容）
 *
 * 纯函数；确定性；不抛。
 */
std::vector<std::string> buildPreviewPage(const AppliedRevisionView& applied);

}  // namespace sdurws::ird::modeling

#endif  // IRD_MODELING_PLUGIN_PANELMODEL_HPP
