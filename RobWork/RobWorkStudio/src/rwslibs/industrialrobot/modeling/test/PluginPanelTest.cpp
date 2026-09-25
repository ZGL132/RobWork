/**
 * @file   PluginPanelTest.cpp
 * @brief  建模插件面板呈现模型测试（模型层——QCoreApplication 级）——
 *         任务契约 WP-13-T15 acceptance 2/3/4/5 的具名自证面。
 *
 * 设计依据：
 *   - units/modeling.md §9.7.1（五区信息架构——ACC2）、§9.7.2（十条数据
 *     流抽测——ACC3：L-1/L-2/L-3 模型层可测三条；L-8 内联二选一数据面；
 *     L-8/L-9 GUI 呈现登记为 V-30 用例，本目标不启动 GUI）、§9.7.3（域
 *     命令登记清单——ACC4）、§9.7.4（刷新与真值纪律——ACC5）
 *   - 需求 MDL-07（界面）、MDL-05（平行轴）、MDL-02/09（权威互斥）、
 *     KIN-06（会话零修订——AT-04）、UX-02/03/05/07、SA-15/SA-16
 *   - 先例：ui/test/CommandInteractionBridgeTest.cpp（桥的 Marshal 测试
 *     形态——替身呈现器＋主线程泵送）；TemplateTest.cpp（六轴草稿夹具）
 *
 * 测试范围声明（ACC3）：L-8/L-9 的 GUI 呈现按契约登记为 V-30 用例
 * （traceability/builds/wp13-t15/ 流程登记），本目标不启动 GUI——L-8
 * 只测模型层数据面（内联二选一的预览数值与三分支分流）；L-9 的结果页
 * 数据装配随其命令面（prepareAuthoritySwitch——T09 已测）承载。
 */

#include <gtest/gtest.h>

#include <QCoreApplication>

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯登记（testkit §7.3）

#include <atomic>
#include <regex>
#include <set>
#include <thread>
#include <vector>

#include <sdurws/ird/core/Units.hpp>               // core::UnitToken::find（比较型三要素单位——L-3）
#include <sdurws/ird/modeling/CommandHandlers.hpp> // kCmdApply* token（命令命名空间分离断言——ACC4）
#include <sdurws/ird/modeling/DiagCodes.hpp>       // kMdl06TravelLimit 等（已注册码字面——不臆造码）
#include <sdurws/ird/modeling/Parts.hpp>           // ToolDefinition/SceneObject/DrivetrainDesign（部件投影夹具）
#include <sdurws/ird/modeling/Template.hpp>        // RobotDesignTemplateFactory/createDraft（六轴草稿夹具）
#include "plugin/PanelCommandCatalog.hpp"  // 命令目录/装配记录/L-7（ACC4——插件私有头，经单元根解析）
#include "plugin/PanelEditFlow.hpp"        // L-2/L-8 编辑流（ACC3）
#include "plugin/PanelModel.hpp"           // 五区投影（ACC2/ACC5）
#include "plugin/PanelRefresh.hpp"         // 刷新协调器/线程守卫（ACC5）
#include "plugin/PanelSelection.hpp"       // L-1 会话选中（ACC3）
#include <sdurws/ird/ui/CommandInteractionBridge.hpp>          // ui 确认交互桥（L-3——UI-T13 落位面）

// ---- 使用声明（与被测头同一命名空间——用例可读性）--------------------
using namespace sdurws::ird;
using namespace sdurws::ird::modeling;

namespace {

/// generic-6r 草稿的便捷创建（TemplateTest 同款——成功前置起步夹具）。
ModelingWorkingSet makeSixAxisDraft()
{
    const RobotDesignTemplateFactory factory;
    std::vector<core::DiagnosticRecord> diags;
    const TemplateOutcome outcome = factory.createDraft(
        TemplateId{kTemplateIdGeneric6R},
        runtime::InstallationPresetToken::Ground, "demo", diags);
    return outcome.get();  // 成功前置——失败即测试自身装配错误（logic_error）
}

/// 确定性占位锚（部件对象夹具用——非全零、字节可控）。
core::ObjectId fixedOid(std::uint8_t tag)
{
    core::ObjectId oid;
    oid.bytes[0] = tag;
    return oid;
}

/// 无对象身份的连线字段（具名常量提高用例可读性）。
constexpr double kPi = 3.14159265358979323846;

/**
 * @brief 编辑流记录替身（IPanelEditSink 实现——三路回调的捕获面）。
 *
 * 断言面：appliedPaths/rejections 记录逐次回调内容；dirtyCount 计数
 * notifySessionDirty；"无模态"由结构保证（替身无任何阻塞/对话框路径）
 * ——拒绝断言只看记录，不需要交互。
 */
struct RecordingEditSink final : IPanelEditSink {
    std::vector<std::string> appliedPaths;  ///< onEditApplied 捕获（定位路径序）
    std::vector<EditRejection> rejections;  ///< onEditRejected 捕获（就地错误序）
    int dirtyCount = 0;                     ///< notifySessionDirty 计数

    void onEditApplied(const std::string& subjectPath) override
    {
        appliedPaths.push_back(subjectPath);
    }
    void notifySessionDirty() override { ++dirtyCount; }
    void onEditRejected(const EditRejection& rejection) override
    {
        rejections.push_back(rejection);
    }
};

/**
 * @brief 手填就绪报告（ACC2 就绪条投影输入——三组各一＋note 一条；
 *        DiagnosticRecord 经工厂构造——码面取 T08 已注册码字面）。
 */
ModelReadinessReport makeSampleReport()
{
    ModelReadinessReport report;

    // blocker 一条（subject＝关节 0 锚——定位跳转断言目标；码取 T08
    // 已注册行程码——不用臆造码字面）。
    core::ComparativeFields cmp;
    cmp.actual.quantity = core::SourcedValue<double>::provided(
        42.0, core::ValueProvenance::make(core::ProvenanceKind::UserProvided));
    cmp.actual.unit = core::UnitToken::find("N*m").value();
    cmp.expected.quantity = core::SourcedValue<double>::provided(
        40.0, core::ValueProvenance::make(core::ProvenanceKind::UserProvided));
    cmp.expected.unit = core::UnitToken::find("N*m").value();
    core::DiagnosticRecord blocker = core::DiagnosticRecord::make(
        core::DiagCode(kMdl06TravelLimit), fixedOid(1), std::string("j0"),
        std::nullopt, "行程上限", "实际行程超过阈值", "确认或修正", cmp);
    report.blockers.push_back(blocker);

    // warning 一条（无 subject——不可定位行；码＝L6 资源状态已注册码）。
    core::DiagnosticRecord warning = core::DiagnosticRecord::make(
        core::DiagCode(kMdlReadinessResourceState), std::nullopt, std::nullopt,
        std::nullopt, "资源状态", "网格资源未固化", "执行固化或生成占位几何",
        std::nullopt);
    report.warnings.push_back(warning);

    // confirmable 一条（C-1：comparison 必在——ConfirmableFinding 工厂校验）。
    report.confirmables.push_back(core::ConfirmableFinding::make(blocker));

    // note 一条（呈现级缺项预告——L5）。
    ReadinessNote note;
    note.layer = ReadinessLayer::L5InertiaPhysical;
    note.subjectPath = "links[1].body";
    note.summary = "连杆 2 物性未提供（DataInsufficient 预告）";
    note.blocking = false;
    report.notes.push_back(note);

    return report;
}

}  // namespace

// =====================================================================
// ACC2——五区面板信息架构（卡 §9.7.1；MDL-07）
// =====================================================================

/// 树投影：节点序＝卡面固定序；节点锚＝ObjectId；标签＝工程用语＋localName。
TEST(PluginPanel, Tree_ObjectIdAnchorAndEngineeringLabels_WP13T15_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-07", "UX-02"},
                  std::vector<std::string>{});

    const ModelingWorkingSet ws = makeSixAxisDraft();
    const auto nodes = buildStructureTree(ws);

    // 行序：根→基座→6 关节→7 连杆→工具组→场景组＝17 行（无位姿集/传动）。
    ASSERT_EQ(nodes.size(), 2U + 6U + 7U + 2U);
    EXPECT_EQ(nodes[0].kind, StructureNodeKind::ModelRoot);
    EXPECT_EQ(nodes[1].kind, StructureNodeKind::BaseInstall);
    EXPECT_EQ(nodes[2].kind, StructureNodeKind::Joint);
    EXPECT_EQ(nodes[8].kind, StructureNodeKind::Link);

    // 节点锚＝ObjectId（关节 0 锚与工作集逐字节一致——L-1 关联键）。
    ASSERT_TRUE(nodes[2].objectId.has_value());
    EXPECT_TRUE(*nodes[2].objectId == ws.design.joints[0].objectId);
    EXPECT_EQ(nodes[2].chainIndex, 0U);
    EXPECT_EQ(nodes[8].chainIndex, 0U);

    // 工程用语标签：含 localName 且非空（UX-02 呈现第一要素）。
    EXPECT_NE(nodes[2].displayLabel.find(ws.design.joints[0].localName),
              std::string::npos);
    // 分组节点无锚（分组仅折叠呈现——锚语义只属于对象叶子；下标 15/16
    // ＝工具组/场景组——组前是 7 条连杆叶子）。
    EXPECT_FALSE(nodes[15].objectId.has_value());
    EXPECT_EQ(nodes[15].kind, StructureNodeKind::ToolsGroup);
    EXPECT_EQ(nodes[16].kind, StructureNodeKind::SceneGroup);
}

/// 树投影 UX-02 守卫：哈希形态显示名（64 位十六进制）进入标签即抛
/// （ensureNoInternalIdentity 唯一出口——零哈希/Schema/插件名界面红线）。
TEST(PluginPanel, Tree_HashLikeLabelRejectedByUX02Guard_WP13T15_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-02"}, std::vector<std::string>{});

    ModelingWorkingSet ws = makeSixAxisDraft();
    // 64 位十六进制串＝内容摘要的呈现形态（CON-05 身份不可进界面）。
    ws.design.displayName = std::string(64, 'a');
    EXPECT_THROW((void)buildStructureTree(ws), std::exception)
        << "UX-02 守卫应拒绝哈希形态标签（零内部标识呈现）";
}

/// 属性区：关节选中只显示相关属性（MDL-07）＋DH 权威下轴/原点灰显只读
/// （§7.2）；速度/加速度不属关节 schema（卡 §15 v0.4 ③c 划界）。
TEST(PluginPanel, Properties_JointRelevantFieldsAndDHGrey_WP13T15_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-07", "MDL-09"},
                  std::vector<std::string>{});

    ModelingWorkingSet ws = makeSixAxisDraft();
    const auto target = resolveSelection(ws, ws.design.joints[0].objectId);
    ASSERT_TRUE(target.has_value());
    ASSERT_EQ(target->kind, SelectedTarget::Kind::Joint);

    // Explicit 权威：轴/原点可编辑；字段键集合＝类型/轴/原点/零位/限位/工作范围。
    const auto explicitRows = propertyFieldsFor(ws, *target);
    std::set<std::string> keys;
    for (const auto& r : explicitRows) { keys.insert(r.fieldKey); }
    EXPECT_TRUE(keys.count("type") == 1 && keys.count("axis") == 1
                && keys.count("origin") == 1 && keys.count("zero-offset") == 1
                && keys.count("bounds") == 1 && keys.count("working-range") == 1)
        << "关节字段面＝§9.7.1 行原文六项";
    for (const auto& r : explicitRows) {
        if (r.fieldKey == "axis" || r.fieldKey == "origin") {
            EXPECT_EQ(r.enablement, FieldEnablement::Editable)
                << "Explicit 权威下一等字段可编辑";
        }
        EXPECT_FALSE(r.fieldKey == "velocity" || r.fieldKey == "acceleration")
            << "速度/加速度不属关节 schema（MDL-16 归传动——§15 v0.4 ③c）";
    }

    // StandardDH 权威：轴/原点灰显只读（§7.2 派生只读——C-1 呈现半区）。
    ws.design.authority = AuthorityMode::StandardDH;
    const auto dhRows = propertyFieldsFor(ws, *target);
    for (const auto& r : dhRows) {
        if (r.fieldKey == "axis" || r.fieldKey == "origin") {
            EXPECT_EQ(r.enablement, FieldEnablement::ReadOnlyGrey)
                << "DH 权威下轴/原点派生只读（灰显非隐藏——值仍投影）";
            EXPECT_FALSE(r.valueText.empty()) << "灰显行的值仍可见";
        }
    }
}

/// 属性区：连杆物性三行带 ValueProvenance 来源徽标（用户/估算/导入——
/// core ProvenanceKind 直投）；几何引用行反映引用有无；未提供行呈现缺失
/// 占位（不伪造数值——ERR-01 四态纪律）。
TEST(PluginPanel, Properties_LinkProvenanceBadges_WP13T15_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-07"}, std::vector<std::string>{});

    // 模板草稿的连杆物性为 NotProvided（物性由估算/导入路径提供——T-MDL-1
    // 只种子关节参数与材料）：先断言缺失态呈现，再注入已提供值断言徽标。
    ModelingWorkingSet ws = makeSixAxisDraft();
    const auto target = resolveSelection(ws, ws.design.links[0].objectId);
    ASSERT_TRUE(target.has_value());

    const auto rows = propertyFieldsFor(ws, *target);
    std::set<std::string> keys;
    for (const auto& r : rows) { keys.insert(r.fieldKey); }
    EXPECT_TRUE(keys.count("mass") == 1 && keys.count("center-of-mass") == 1
                && keys.count("inertia") == 1 && keys.count("visual-geometry") == 1
                && keys.count("collision-geometry") == 1);

    // 缺失态：质量行显示"未提供"占位、无徽标（不伪造数值与来源）。
    for (const auto& r : rows) {
        if (r.fieldKey == "mass") {
            EXPECT_EQ(r.valueText, "未提供");
            EXPECT_FALSE(r.provenance.has_value());
        }
    }

    // 已提供态：物性注入（估算来源——GeometricEstimate 徽标直投）。
    ws.design.links[0].body.mass = core::SourcedValue<double>::provided(
        12.5, core::ValueProvenance::make(
                  core::ProvenanceKind::GeometricEstimate, std::nullopt,
                  std::nullopt, std::string("mdl-property-formula/1")));
    const auto providedRows = propertyFieldsFor(ws, *target);
    for (const auto& r : providedRows) {
        if (r.fieldKey == "mass") {
            EXPECT_EQ(r.valueText, "12.5") << "确定性文本化（6 位裁尾零）";
            ASSERT_TRUE(r.provenance.has_value());
            EXPECT_EQ(*r.provenance, core::ProvenanceKind::GeometricEstimate)
                << "来源徽标＝ValueProvenance 直投（估算）";
        }
    }
}

/// 属性区：工具/场景/传动投影（§4.4 安装接口＋TCP 列表；§4.5 世界位姿
/// ＋角色；§4.7 比率/摩擦/力矩行——"选中只显示相关属性"的部件面）。
TEST(PluginPanel, Properties_ToolSceneDrivetrainProjection_WP13T15_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-07", "MDL-13", "MDL-15"},
                  std::vector<std::string>{});

    ModelingWorkingSet ws = makeSixAxisDraft();

    // 工具夹具：tcpList 两行（key 唯一——defaultTcp 引用目标）。
    ToolDefinition tool;
    tool.objectId = fixedOid(0xA0);
    tool.localName = "gripper";
    TcpEntry tcp0;
    tcp0.key = "tcp-center";
    tcp0.displayName = "中心 TCP";
    TcpEntry tcp1;
    tcp1.key = "tcp-flange";
    tcp1.displayName = "法兰 TCP";
    tool.tcpList = {tcp0, tcp1};
    ws.toolObjects.push_back(tool);

    // 场景夹具：世界系固连位姿＋角色。
    SceneObject scene;
    scene.objectId = fixedOid(0xB0);
    scene.localName = "table";
    scene.role = SceneObjectRole::EnvironmentObject;
    ws.sceneObjects.push_back(scene);

    // 传动夹具：逐关节比率两条（与关节序对应）。
    DrivetrainDesign drivetrain;
    drivetrain.objectId = fixedOid(0xC0);
    drivetrain.ratioPerJoint = {
        core::SourcedValue<double>::provided(
            1.0, core::ValueProvenance::make(core::ProvenanceKind::UserProvided)),
        core::SourcedValue<double>::provided(
            2.0, core::ValueProvenance::make(core::ProvenanceKind::UserProvided))};
    ws.drivetrainObject = drivetrain;

    // 工具投影：安装接口行＋逐 TCP 行（键＝"tcp:<key>"——引用目标呈现）。
    const auto toolRows = propertyFieldsFor(ws, *resolveSelection(ws, tool.objectId));
    ASSERT_TRUE(toolRows.size() >= 3U);
    EXPECT_EQ(toolRows[0].fieldKey, "mount-interface");
    EXPECT_TRUE(toolRows[1].fieldKey == "tcp:tcp-center"
                && toolRows[2].fieldKey == "tcp:tcp-flange");

    // 场景投影：世界位姿（世界系固连——§4.5 注）＋角色行。
    const auto sceneRows = propertyFieldsFor(ws, *resolveSelection(ws, scene.objectId));
    ASSERT_EQ(sceneRows.size(), 2U);
    EXPECT_EQ(sceneRows[0].fieldKey, "world-pose");
    EXPECT_EQ(sceneRows[1].fieldKey, "role");

    // 传动投影：比率/摩擦/力矩三行（§4.7——值面直投）。
    const auto dtRows = propertyFieldsFor(ws, *resolveSelection(ws, drivetrain.objectId));
    ASSERT_EQ(dtRows.size(), 3U);
    EXPECT_EQ(dtRows[0].fieldKey, "ratio-per-joint");
    EXPECT_EQ(dtRows[1].fieldKey, "friction-per-joint");
    EXPECT_EQ(dtRows[2].fieldKey, "torque-limits-per-joint");
}

/// 就绪诊断条：L0~L11 分层结果＋三组计数＋逐项定位跳转（subject→树锚；
/// 无主体行不伪造定位）。
TEST(PluginPanel, ReadinessBar_LayersCountsAndJumpTargets_WP13T15_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-06", "MDL-07"},
                  std::vector<std::string>{});

    const auto report = makeSampleReport();
    const auto bar = projectReadinessBar(report);

    // 层结果行：12 行（下标＝层号）——L0~L11 分层结果保序直投。
    ASSERT_EQ(bar.layerResults.size(), 12U);
    EXPECT_EQ(bar.layerResults[0].layer, ReadinessLayer::L0Structure);
    EXPECT_EQ(bar.layerResults[11].layer, ReadinessLayer::L11CompileRequestable);

    // 三组计数：1/1/1＋note 1（报告面直投——判定权威在 checker）。
    EXPECT_EQ(bar.counts.blockers, 1U);
    EXPECT_EQ(bar.counts.warnings, 1U);
    EXPECT_EQ(bar.counts.confirmables, 1U);
    EXPECT_EQ(bar.counts.notes, 1U);

    // 逐项行组序：blocking→warning→confirmable→note（组内报告稳定序）。
    ASSERT_EQ(bar.items.size(), 4U);
    EXPECT_EQ(bar.items[0].severity, "blocking");
    EXPECT_EQ(bar.items[1].severity, "warning");
    EXPECT_EQ(bar.items[2].severity, "confirmable");
    EXPECT_EQ(bar.items[3].severity, "note");

    // 逐项定位跳转：blocker 的 subject＝树锚（同键关联）；无主体行＝无跳转。
    ASSERT_TRUE(bar.items[0].jumpTarget.has_value());
    EXPECT_TRUE(*bar.items[0].jumpTarget == fixedOid(1));
    EXPECT_FALSE(bar.items[1].jumpTarget.has_value());
    EXPECT_TRUE(bar.items[3].summary.find("DataInsufficient") != std::string::npos)
        << "note 行呈现报告原文（零业务文案加工）";
}

/// 预览页：仅基于已应用修订（D-MDL-10——编辑态即时 XML 预览不提供）。
/// 行为面：空修订＝空页；内容按行拆分；工作集不可作预览源（类型强制——
/// buildPreviewPage 只收 AppliedRevisionView，ModelingWorkingSet 无重载，
/// 编辑态数据在类型层面进不了预览构建函数）。
TEST(PluginPanel, Preview_AppliedRevisionOnly_DMdl10_WP13T15_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-20", "UX-02"},
                  std::vector<std::string>{});

    // 空修订＝空页（不伪造内容）。
    AppliedRevisionView empty;
    EXPECT_TRUE(buildPreviewPage(empty).empty());

    // 有修订：摘要按行拆分（WC/DWC XML 外供预览素材——MDL-20 外供内容）。
    AppliedRevisionView applied;
    applied.revision = core::RevisionId::fromCanonical(
        "rev-00000000000000000000000000000abc");
    applied.summaryText = "<workcell>\n  <device name=\"demo\"/>\n</workcell>";
    const auto lines = buildPreviewPage(applied);
    ASSERT_EQ(lines.size(), 3U);
    EXPECT_EQ(lines[0], "<workcell>");
    EXPECT_EQ(lines[2], "</workcell>");
}

// =====================================================================
// ACC3——界面数据流抽测（卡 §9.7.2：模型层可测三条 L-1/L-2/L-3＋L-8 数据面）
// =====================================================================

/// L-1 正向：树/View3D 拾取→会话选中态零修订（工作集字节不变＋零变更
/// 记录——KIN-06/AT-04 会话操作零修订）＋幂等选中。
TEST(PluginPanel, L1_SelectionSessionStateZeroRevision_WP13T15_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-07", "KIN-06"},
                  std::vector<std::string>{"AT-04"});

    ModelingWorkingSet ws = makeSixAxisDraft();
    const ModelingWorkingSet before = ws;  // 值快照（零修订断言的对照面）

    PanelSelectionState selection;
    const auto anchor = ws.design.joints[0].objectId;

    // 选中：会话态变化（true），工作集零触碰（值相等＝字节不变）。
    EXPECT_TRUE(selection.select(anchor));
    EXPECT_TRUE(selection.selected().has_value());
    EXPECT_TRUE(*selection.selected() == anchor);
    EXPECT_EQ(ws, before) << "选中只写会话态——零修订（工作集字节不变）";
    EXPECT_TRUE(ws.changes.empty()) << "零变更记录（无编辑差值——AT-04）";

    // 幂等：重复选中同一锚不重复触发刷新。
    EXPECT_FALSE(selection.select(anchor));
    // 清除选中（点击空白）：会话态回空。
    EXPECT_TRUE(selection.select(std::nullopt));
    EXPECT_FALSE(selection.selected().has_value());
}

/// L-1 反向：属性区/诊断条跳转→树滚动＋三维高亮目标（LocateTarget 同锚
/// 双产出）；无效锚不定位不高亮（不伪造）。
TEST(PluginPanel, L1_ReverseLocateScrollAndHighlight_WP13T15_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-07", "KIN-06"},
                  std::vector<std::string>{"AT-04"});

    PanelSelectionState selection;
    std::vector<LocateTarget> located;
    selection.setLocateSink([&located](const LocateTarget& t) { located.push_back(t); });

    const auto oid = fixedOid(0x42);
    const auto target = selection.locate(oid);
    ASSERT_TRUE(target.has_value());
    EXPECT_TRUE(target->scrollToNode == oid) << "树滚动目标＝同一锚";
    EXPECT_TRUE(target->highlightObject == oid) << "三维高亮目标＝同一锚";
    ASSERT_EQ(located.size(), 1U) << "定位事件经 sink 即发（UI 线程同步）";
    EXPECT_TRUE(located[0] == *target);

    // 全零保留值＝无效锚：不产出目标、不触发 sink（不伪造定位）。
    located.clear();
    const auto invalid = selection.locate(core::ObjectId{});
    EXPECT_FALSE(invalid.has_value());
    EXPECT_TRUE(located.empty());
}

/// L-2 接受分支：applyEdit 接受→树/属性区/就绪条增量刷新信号＋
/// notifySessionDirty（PM-04/PM-11）＋变更记录恰一条。
TEST(PluginPanel, L2_EditAcceptedIncrementalRefreshAndDirty_WP13T15_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-07", "UX-05"},
                  std::vector<std::string>{});

    ModelingWorkingSet ws = makeSixAxisDraft();
    RecordingEditSink sink;

    const auto outcome =
        submitJointFieldEdit(ws, sink, 0, JointEditField::ZeroOffset, 0.5);
    EXPECT_EQ(outcome, EditSubmitOutcome::Applied);

    // 增量刷新信号：定位路径精确到关节（树/属性区/就绪条裁剪刷新）。
    ASSERT_EQ(sink.appliedPaths.size(), 1U);
    EXPECT_EQ(sink.appliedPaths[0], "joints[0]");
    // 脏通知：恰一次（标题 `*` 标记——PM-04/PM-11）。
    EXPECT_EQ(sink.dirtyCount, 1);
    // 域效果：权威值已更新＋变更记录恰一条（append-only）。
    EXPECT_DOUBLE_EQ(ws.design.joints[0].zeroOffset, 0.5);
    ASSERT_EQ(ws.changes.size(), 1U);
}

/// L-2 拒绝分支：就地比较型错误＋保留原值不弹模态（UX-03/05/07）——
/// 工作集字节不变、错误经 sink 即时回传（无任何模态路径）。
TEST(PluginPanel, L2_EditRejectedInlineErrorValuePreserved_WP13T15_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-07", "UX-03", "UX-07"},
                  std::vector<std::string>{});

    ModelingWorkingSet ws = makeSixAxisDraft();
    const ModelingWorkingSet before = ws;
    RecordingEditSink sink;

    // 非法限位（qmin≥qmax）——域函数裁决拒绝（面板零判定）。
    const auto outcome = submitJointFieldEdit(ws, sink, 0, JointEditField::Bounds,
                                              JointLimits{1.0, -1.0});
    EXPECT_EQ(outcome, EditSubmitOutcome::Rejected);

    // 就地错误：码 token＋定位细节可呈现（比较型原因面）。
    ASSERT_EQ(sink.rejections.size(), 1U);
    EXPECT_EQ(sink.rejections[0].codeToken, "limit-interval-invalid");
    EXPECT_FALSE(sink.rejections[0].detail.empty());
    // 无刷新无脏通知（无变更发生）；零模态（sink 无阻塞路径——结构保证）。
    EXPECT_TRUE(sink.appliedPaths.empty());
    EXPECT_EQ(sink.dirtyCount, 0);
    // 保留原值：工作集字节不变（值相等＝域函数强保证的呈现面证据）。
    EXPECT_EQ(ws, before);
}

/// L-3 应用确认流：ConfirmableFinding 三要素经 ui CommandInteractionBridge
/// marshal 至 UI 线程（SA-15/P-PR-7）——比较型三要素在对话装配值中可
/// 观察，凭据组装随决议回传（桥的模型层机制面——UI-T13 落位面复用）。
TEST(PluginPanel, L3_ConfirmableThreeElementsViaBridgeMarshal_WP13T15_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"SA-15", "MDL-06"},
                  std::vector<std::string>{});

    // 比较型三要素夹具（行程上限——V-16 同型：实际/阈值/单位）。
    core::ComparativeFields comparison;
    comparison.actual.quantity = core::SourcedValue<double>::provided(
        42.0, core::ValueProvenance::make(core::ProvenanceKind::UserProvided));
    comparison.actual.unit = core::UnitToken::find("N*m").value();
    comparison.expected.quantity = core::SourcedValue<double>::provided(
        40.0, core::ValueProvenance::make(core::ProvenanceKind::UserProvided));
    comparison.expected.unit = core::UnitToken::find("N*m").value();
    core::DiagnosticRecord record = core::DiagnosticRecord::make(
        core::DiagCode(kMdl06TravelLimit), fixedOid(0x7),
        std::string("j0"), std::nullopt, "关节力矩超限",
        "实际力矩 42 N·m 超过阈值 40 N·m", "确认继续或修正输入", comparison);
    const core::ConfirmableFinding finding = core::ConfirmableFinding::make(record);

    // 确认对话呈现器替身（UI 线程捕获装配值——对话三要素断言锚）。
    struct CapturingPresenter final : ui::IUiConfirmDialogPresenter {
        std::vector<ui::ConfirmDialogData> seen;
        std::atomic<bool> resolved{false};
        ui::ConfirmDialogResolution showConfirmDialog(
            const ui::ConfirmDialogData& data) override
        {
            seen.push_back(data);
            ui::ConfirmDialogResolution r;
            r.outcome = ui::ConfirmDialogOutcome::Confirmed;
            resolved.store(true);
            return r;
        }
        void noteInputChanged() override {}
    };
    auto presenter = std::make_shared<CapturingPresenter>();

    ui::CommandInteractionBridge::Deps deps;
    deps.presenter = presenter;
    deps.principalProvider = [] { return std::string("tester"); };
    ui::CommandInteractionBridge bridge(deps);

    // 命令执行线程发起 requestConfirmations（同步回调——§9.2 线程模型）；
    // 主线程（测试 UI 线程）泵送事件直至桥的 Marshal 投递完成并决议。
    std::optional<std::vector<core::ConfirmationCredential>> credentials;
    std::thread commandThread([&] {
        credentials = bridge.requestConfirmations({finding});
    });
    const int spinGuard = 20000;
    for (int i = 0; i < spinGuard && !presenter->resolved.load(); ++i) {
        QCoreApplication::processEvents();  // 主线程泵送——Marshal 事件投递
        std::this_thread::yield();
    }
    commandThread.join();

    // marshal 至 UI 线程实证：呈现器在主线程收到恰一次对话。
    ASSERT_EQ(presenter->seen.size(), 1U);
    const auto& dialog = presenter->seen[0];
    ASSERT_EQ(dialog.items.size(), 1U);
    // 三要素齐备（SA-15 比较型：实际/阈值/单位——UX-03 同显锚）。
    const auto& cmp = *dialog.items[0].finding.record.comparison;
    ASSERT_TRUE(cmp.actual.quantity.state() == core::FieldState::Provided);
    ASSERT_TRUE(cmp.expected.quantity.state() == core::FieldState::Provided);
    EXPECT_DOUBLE_EQ(cmp.actual.quantity.value(), 42.0);
    EXPECT_DOUBLE_EQ(cmp.expected.quantity.value(), 40.0);
    EXPECT_TRUE(cmp.actual.unit == core::UnitToken::find("N*m").value()) << "单位要素（N·m）";
    // 决议回传：凭据向量产出（principal＝会话缓存——P-UI-4）。
    ASSERT_TRUE(credentials.has_value());
    ASSERT_EQ(credentials->size(), 1U);
    EXPECT_EQ((*credentials)[0].principal, "tester");
}

/// L-8 数据面：质心编辑内联二选一（平行轴迁移/覆盖完整张量＋迁移预览
/// 数值；未选择→CentroidEditUnresolved 就地拒绝；带选择重提→接受）。
/// GUI 呈现登记为 V-30 用例（本目标不启动）。
TEST(PluginPanel, L8_CentroidInlineChoiceModelData_WP13T15_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-05", "UX-07"},
                  std::vector<std::string>{});

    ModelingWorkingSet ws = makeSixAxisDraft();
    // 前置"已估算连杆"：物性三值Provided（V-17 前置——估算值来源标记）。
    BodyData& body = ws.design.links[0].body;
    body.mass = core::SourcedValue<double>::provided(
        10.0, core::ValueProvenance::make(core::ProvenanceKind::GeometricEstimate));
    body.centerOfMass = core::SourcedValue<rw::math::Vector3D<double>>::provided(
        rw::math::Vector3D<double>(0.0, 0.0, 0.1),
        core::ValueProvenance::make(core::ProvenanceKind::GeometricEstimate));
    body.inertia = core::SourcedValue<InertiaTensor>::provided(
        InertiaTensor{},
        core::ValueProvenance::make(core::ProvenanceKind::GeometricEstimate));

    const rw::math::Vector3D<double> newCom(0.0, 0.0, 0.2);  // 新质心（m，连杆系）

    // 内联预览数值：迁移分支可得（域 dry-run 产出——插件零推算）。
    const auto preview = centroidChoicePreview(body, newCom, std::nullopt);
    EXPECT_FALSE(preview.migratedText.empty())
        << "迁移预览＝计算库产出（平行轴定理——面板零计算逻辑）";
    EXPECT_TRUE(preview.overwrittenText.empty())
        << "无候选张量时覆盖分支预览为空（不伪造数值）";

    // 分支 (c) 未选择：域拒绝 CentroidEditUnresolved→就地出口＋原值保留。
    RecordingEditSink sink;
    const auto unresolved =
        submitCentroidEdit(body, sink, newCom, std::nullopt, std::nullopt);
    EXPECT_EQ(unresolved, CentroidSubmitOutcome::Unresolved);
    ASSERT_EQ(sink.rejections.size(), 1U);
    EXPECT_EQ(sink.rejections[0].codeToken, "CentroidEditUnresolved");
    EXPECT_DOUBLE_EQ(body.centerOfMass.value()[2], 0.1) << "原值保留";

    // 分支 (a) 迁移重提：接受＋刷新＋脏通知（选择随域留痕——V-17）。
    RecordingEditSink sinkA;
    const auto migrated = submitCentroidEdit(body, sinkA, newCom,
                                             CentroidEditResolution::MigrateInertia,
                                             std::nullopt);
    EXPECT_EQ(migrated, CentroidSubmitOutcome::Applied);
    EXPECT_EQ(sinkA.dirtyCount, 1);
    EXPECT_DOUBLE_EQ(body.centerOfMass.value()[2], 0.2);
}

// =====================================================================
// ACC4——域命令登记（卡 §9.7.3 表逐行；ui.md §10.9/§11.2）
// =====================================================================

/// 十条 CommandId 点分小写、逐行与卡 §9.7.3 表一致、无重复（export-package
/// 与 import-package 两条独立命令不合并）。
TEST(PluginPanel, Commands_TenDottedCommandIds_WP13T15_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-07"}, std::vector<std::string>{});

    const auto cmds = modelingDomainCommands();
    ASSERT_EQ(cmds.size(), 10U) << "卡 §9.7.3 表恰十条";

    // 逐行对照（行序＝卡表行序——登记序确定性）。
    const char* expected[10] = {
        "modeling.new-from-template", "modeling.import-urdf",
        "modeling.import-xacro", "modeling.switch-authority",
        "modeling.estimate-properties", "modeling.generate-placeholder-geometry",
        "modeling.diff-baseline", "modeling.export-package",
        "modeling.import-package", "modeling.reset-home-zero",
    };
    std::set<std::string> ids;
    for (std::size_t i = 0; i < cmds.size(); ++i) {
        EXPECT_EQ(cmds[i].id, expected[i]) << "第 " << i << " 行与卡表不一致";
        ids.insert(cmds[i].id);
    }
    EXPECT_EQ(ids.size(), 10U) << "无重复 id（点分小写全局唯一——§7.2）";

    // 点分小写词形：点分段＋段内 [a-z0-9-]（卡 §9.7.3 十条全小写——段内
    // 连字符如 reset-home-zero；ui 词表段字符集 [A-Za-z0-9-]，词表全小写）。
    const std::regex dotted("^[a-z0-9-]+(\\.[a-z0-9-]+)+$");
    for (const auto& c : cmds) {
        EXPECT_TRUE(std::regex_match(c.id, dotted)) << "点分小写词形: " << c.id;
    }
}

/// readOnlyAllowed 逐行按卡 §9.7.3 表；ownerUnit＝白名单 token。
TEST(PluginPanel, Commands_ReadOnlyAllowedPerTable_WP13T15_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-07", "PM-07"},
                  std::vector<std::string>{});

    const auto cmds = modelingDomainCommands();
    // 卡表值（id→readOnlyAllowed）。
    const std::set<std::string> readOnlyTrue = {
        "modeling.diff-baseline", "modeling.export-package",
        "modeling.reset-home-zero",
    };
    for (const auto& c : cmds) {
        EXPECT_EQ(c.ownerUnit, "modeling") << "白名单 token（§11.1）";
        const bool expectTrue = readOnlyTrue.count(c.id) > 0;
        EXPECT_EQ(c.readOnlyAllowed, expectTrue)
            << "readOnlyAllowed 卡表值: " << c.id;
    }
}

/// 快捷键纪律（SA-16）：十条命令默认零绑定——快捷键一律经 ui
/// HotkeyBindingTable（用户级），插件不私占全局。
TEST(PluginPanel, Commands_NoDefaultGlobalShortcut_SA16_WP13T15_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"SA-16"}, std::vector<std::string>{});

    for (const auto& c : modelingDomainCommands()) {
        EXPECT_FALSE(c.defaultShortcut.has_value())
            << "默认无键（面板可达兜底——UX-13）: " << c.id;
        EXPECT_TRUE(c.bindable)
            << "可经 HotkeyBindingTable 用户级绑定（SA-16 唯一绑定路径）";
    }
}

/// 命名空间分离：ui CommandId 词表（点分）与 project commandType 词表
/// （无点）两套命名空间不混用（卡 §9.7.3 括注——交叉断言）。
TEST(PluginPanel, Commands_NamespaceSeparationFromProjectTokens_WP13T15_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-07", "P-PR-9"},
                  std::vector<std::string>{});

    const auto cmds = modelingDomainCommands();
    const auto projectTokens = modelingProjectCommandTokens();

    // project token 无点语法（project §4.4.4 ^[a-z0-9-]{3,64}）。
    const std::regex noDot("^[a-z0-9-]{3,64}$");
    for (const auto& t : projectTokens) {
        EXPECT_TRUE(std::regex_match(t, noDot)) << "无点词形: " << t;
    }
    // 词表零交叉：任一 CommandId 不出现在 commandType 清单、反之亦然。
    std::set<std::string> cmdIds;
    for (const auto& c : cmds) { cmdIds.insert(c.id); }
    std::set<std::string> tokens(projectTokens.begin(), projectTokens.end());
    for (const auto& c : cmds) {
        EXPECT_EQ(tokens.count(c.id), 0U) << "CommandId 不得混入 commandType: " << c.id;
    }
    for (const auto& t : projectTokens) {
        EXPECT_EQ(cmdIds.count(t), 0U) << "commandType 不得混入 CommandId: " << t;
    }
    // 权威直用：project token 与 CommandHandlers.hpp 冻结常量逐一同串
    // （不私写第二字面量）。
    EXPECT_EQ(tokens.count(std::string(kCmdApplyRobotDesign)), 1U);
}

/// 只读门控 L-7：writable=false→一切可编辑行降级灰显；DH 派生灰显行不
/// 因可写性复活（§7.2×L-7 正交）；浏览/预览面不在门控范围。
TEST(PluginPanel, Commands_ReadOnlyDisablesEditControls_L7_WP13T15_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-07", "PM-07"},
                  std::vector<std::string>{});

    std::vector<PropertyFieldRow> rows;
    PropertyFieldRow editable;
    editable.fieldKey = "zero-offset";
    editable.enablement = FieldEnablement::Editable;
    PropertyFieldRow derivedGrey;
    derivedGrey.fieldKey = "axis";
    derivedGrey.enablement = FieldEnablement::ReadOnlyGrey;  // DH 派生只读
    rows = {editable, derivedGrey};

    // 可写会话：门控零干预。
    const auto writableRows = applyReadOnlyGate(rows, true);
    ASSERT_EQ(writableRows.size(), 2U);
    EXPECT_EQ(writableRows[0].enablement, FieldEnablement::Editable);
    EXPECT_EQ(writableRows[1].enablement, FieldEnablement::ReadOnlyGrey);

    // 只读会话：可编辑行降级灰显；已灰显行不复活（两个灰显源正交）。
    const auto readonlyRows = applyReadOnlyGate(rows, false);
    ASSERT_EQ(readonlyRows.size(), 2U);
    EXPECT_EQ(readonlyRows[0].enablement, FieldEnablement::ReadOnlyGrey)
        << "L-7：编辑控件禁用（灰显）";
    EXPECT_EQ(readonlyRows[1].enablement, FieldEnablement::ReadOnlyGrey)
        << "派生只读不受会话可写性影响";
    // 键与值零变化（只变使能——同序同键）。
    EXPECT_EQ(readonlyRows[0].fieldKey, "zero-offset");
}

/// 装配描述符形状（ui.md §10.9 承载——pluginId/阶段/能力）＋域就绪投影
/// （§11.2 readonlyProjections 数据面——汇聚不判定）。
TEST(PluginPanel, Registration_DescriptorShapeAndReadinessProjection_WP13T15_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-07", "UX-09"},
                  std::vector<std::string>{});

    const auto rec = modelingPanelRegistration();
    EXPECT_EQ(rec.pluginId, "modeling") << "静态白名单 token（ui.md §11.1）";
    EXPECT_EQ(rec.stage, ui::StageId::Modeling) << "StageId=modeling（卡 §9.7.1）";
    EXPECT_TRUE(rec.capabilities.providesStagePanel);
    EXPECT_TRUE(rec.capabilities.providesReadonlyProjection);
    EXPECT_TRUE(rec.capabilities.registersCommands);
    EXPECT_FALSE(rec.advanced) << "主面板（非 UX-04 高级面板）";

    // 就绪投影三态映射（直投——判定权威在 checker）。
    auto report = makeSampleReport();
    report.status = ReadinessStatus::Ready;
    auto proj = modelingReadinessProjection(report);
    ASSERT_EQ(proj.size(), 1U);
    EXPECT_EQ(proj[0].domainKey, "modeling");
    EXPECT_EQ(proj[0].verdict, core::EngineeringStatus::Feasible);
    EXPECT_TRUE(proj[0].inputComplete);

    report.status = ReadinessStatus::NotReady;
    proj = modelingReadinessProjection(report);
    EXPECT_EQ(proj[0].verdict, core::EngineeringStatus::DataInsufficient);
    EXPECT_FALSE(proj[0].inputComplete);
    // 缺项键：非 blocking note 派生（UX-02——键与层 token，零哈希）。
    ASSERT_FALSE(proj[0].missingItemKeys.empty());
    EXPECT_NE(proj[0].missingItemKeys[0].find("missing."), std::string::npos);
}

// =====================================================================
// ACC5——刷新与真值纪律（卡 §9.7.4；L-4）
// =====================================================================

/// 事件驱动刷新（L-4）：⑤通知→重载基线（provider 现取——零缓存）→
/// 未应用编辑保序重演（域函数逐条裁决）→全面板刷新恰一次。
TEST(PluginPanel, Refresh_EventDrivenReplayInOrder_L4_WP13T15_ACC5)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-07", "PM-04"},
                  std::vector<std::string>{});

    ModelingWorkingSet ws = makeSixAxisDraft();
    int refreshCount = 0;

    PanelRefreshCoordinator coordinator;
    // provider 现取权威工作集引用（重演直接落位——L5 装配层的会话单例；
    // 协调器零持有——引用仅调用期使用）。
    coordinator.setSinks(
        [&ws]() -> ModelingWorkingSet& { return ws; },
        [&refreshCount]() { ++refreshCount; });

    // 未应用编辑两条（发生序＝入队序）：零位 0.5→0.9（同关节同字段——
    // 保序重演的顺序语义断言锚：终值必须是后一条）。
    PendingEdit e1;
    e1.jointIndex = 0;
    e1.field = JointEditField::ZeroOffset;
    e1.value = 0.5;
    PendingEdit e2;
    e2.jointIndex = 0;
    e2.field = JointEditField::ZeroOffset;
    e2.value = 0.9;
    coordinator.recordPending(e1);
    coordinator.recordPending(e2);

    const auto outcome = coordinator.onRevisionEvent(std::nullopt);
    EXPECT_EQ(outcome, ReplayOutcome::Replayed);
    EXPECT_FALSE(coordinator.manualInterventionRequired());
    // 全面板刷新恰一次（事件驱动——无轮询无重复触发）。
    EXPECT_EQ(refreshCount, 1);
    // 保序重演实证：终值＝最后一条编辑（0.9 非 0.5——顺序语义确定）。
    EXPECT_DOUBLE_EQ(ws.design.joints[0].zeroOffset, 0.9);
    // 重演不改队列（编辑意图由用户处置——discardPendingFrom/clear）。
    EXPECT_EQ(coordinator.pending().size(), 2U);
}

/// 重演失败＝手工处置（L-4"不静默丢弃"）：失败编辑与其后编辑保留在队列，
/// 手工处置标记置位，全面板刷新仍触发（横幅可见）。
TEST(PluginPanel, Refresh_ReplayFailureManualIntervention_WP13T15_ACC5)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-07", "UX-03"},
                  std::vector<std::string>{});

    ModelingWorkingSet ws = makeSixAxisDraft();
    int refreshCount = 0;

    PanelRefreshCoordinator coordinator;
    coordinator.setSinks(
        [&ws]() -> ModelingWorkingSet& { return ws; },
        [&refreshCount]() { ++refreshCount; });

    // 编辑 1：零位 0.5（新基线上仍合法）；编辑 2：对同一关节的非法限位
    // （qmin≥qmax——域必拒）。保序重演应在第 2 条停下。
    PendingEdit e1;
    e1.jointIndex = 0;
    e1.field = JointEditField::ZeroOffset;
    e1.value = 0.5;
    PendingEdit e2;
    e2.jointIndex = 0;
    e2.field = JointEditField::Bounds;
    e2.value = JointLimits{1.0, -1.0};
    coordinator.recordPending(e1);
    coordinator.recordPending(e2);
    ASSERT_EQ(coordinator.pending().size(), 2U);

    const auto outcome = coordinator.onRevisionEvent(std::nullopt);
    EXPECT_EQ(outcome, ReplayOutcome::BlockedAtEdit);
    ASSERT_TRUE(coordinator.blockedAtIndex().has_value());
    EXPECT_EQ(*coordinator.blockedAtIndex(), 1U) << "停在第一条被拒编辑";
    EXPECT_TRUE(coordinator.manualInterventionRequired()) << "手工处置标记置位";
    // 不静默丢弃：队列原样保留（失败条＋其后条——用户可见可处置）。
    EXPECT_EQ(coordinator.pending().size(), 2U);
    // 失败态同样刷新（横幅可见——L-4 提示手工处置的呈现前提）。
    EXPECT_EQ(refreshCount, 1);
    // 域接受的前缀编辑即真实编辑（重演落在权威工作集——e1 已落位 0.5；
    // 被拒的 e2 未触碰限位——域拒绝强保证）。
    EXPECT_DOUBLE_EQ(ws.design.joints[0].zeroOffset, 0.5);

    // 手工处置：用户放弃全部未应用编辑→标记复位可重演。
    coordinator.clearPending();
    EXPECT_TRUE(coordinator.pending().empty());
}

/// 真值纪律：投影为纯函数（同输入同输出）；输入变更后重投影立即反映
/// （面板零权威数据缓存——防 UI 副本成为第二真值）。
TEST(PluginPanel, Model_NoAuthoritativeDataCache_WP13T15_ACC5)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-07", "NFR-COR-02"},
                  std::vector<std::string>{});

    ModelingWorkingSet ws = makeSixAxisDraft();
    const auto target = resolveSelection(ws, ws.design.joints[0].objectId);
    ASSERT_TRUE(target.has_value());

    // 纯函数半区：同输入重复投影逐行相等（确定性——NFR-COR-02）。
    const auto first = propertyFieldsFor(ws, *target);
    const auto second = propertyFieldsFor(ws, *target);
    EXPECT_EQ(first, second);

    // 无缓存半区：工作集变更后重投影立即反映新值（零陈旧副本）。
    ws.design.joints[0].zeroOffset = 0.75;
    const auto after = propertyFieldsFor(ws, *target);
    ASSERT_EQ(after.size(), first.size());
    for (std::size_t i = 0; i < after.size(); ++i) {
        if (after[i].fieldKey == "zero-offset") {
            EXPECT_NE(after[i].valueText, first[i].valueText)
                << "重投影即时反映权威值变更（零缓存）";
        }
    }
    // 树投影同判据（五区同纪律）。
    const auto tree1 = buildStructureTree(ws);
    const auto tree2 = buildStructureTree(ws);
    EXPECT_EQ(tree1, tree2);
}

/// 线程纪律（§3.4）：编辑/刷新面仅 UI 线程——守卫构造线程绑定，跨线程
/// 进入即 fail-fast（调用方契约违约——异常上抛，不静默容错）。
TEST(PluginPanel, Thread_UiThreadOnlyConstraint_WP13T15_ACC5)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-07"}, std::vector<std::string>{});

    PanelUiThreadGuard guard;
    EXPECT_TRUE(guard.onUiThread()) << "构造线程＝UI 线程（面板装配语义）";
    guard.assertOnUiThread();  // 同线程断言通过（不抛）。

    // 外来线程：违约即抛（fail-fast——AGENTS 错误语义）。
    std::atomic<bool> threw{false};
    std::thread foreign([&] {
        try {
            guard.assertOnUiThread();
        } catch (const std::exception&) {
            threw.store(true);
        }
    });
    foreign.join();
    EXPECT_TRUE(threw) << "跨线程访问编辑面＝契约违约（§3.4 fail-fast）";
}
