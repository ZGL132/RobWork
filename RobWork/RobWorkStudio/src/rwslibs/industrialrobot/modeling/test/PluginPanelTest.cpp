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
#include <QTemporaryDir>
#include <QFile>
#include "plugin/GeometryResourceFlow.hpp"
#include "plugin/ModelingDraftCodec.hpp" // B4b域帧须在独立/集成两模式都能真实往返。
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯登记（testkit §7.3）

#include <atomic>
#include <limits> // F10非有限值反例，必须走真实拒绝路径。
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
#include "plugin/RpyPresentation.hpp"  // REUSE-T33 oracle：rpyview::rotationToRpy（上游 RPY 反解）

#include <rw/math/RPY.hpp>  // REUSE-T33 oracle：上游 RPY 位级对照
#include <array>
#include <cmath>
#include "plugin/PanelRefresh.hpp"         // 刷新协调器/线程守卫（ACC5）
#include "plugin/PanelSelection.hpp"       // L-1 会话选中（ACC3）
#include <sdurws/ird/modeling/Codec.hpp>
#include "plugin/ModelingBodyEditor.hpp" // F10纯值编排，真实迁移与完整张量判定。
#include "plugin/ModelingSceneEditor.hpp" // F12纯值事务，错误时完整基线保持不变。
#include "plugin/ModelingToolEditor.hpp" // F08工具/默认TCP/几何候选完整事务。
#include "plugin/ModelingCommandFlows.hpp"  // customChainRowSemanticCheck（UI-T66——F-523① 录入辅助面）
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

/// 属性区复合值展开与枚举值中文呈现：Provided 态复合行展开为精确数值
/// （轴向三维／原点·安装接口·世界位姿六值／惯量六分量——不再落
/// "[值已提供]/[位姿已提供]"占位）；类型/权威模式/场景角色值＝中文呈现
/// 映射（英文 token 括注保留——机器判别仍以词表 token 为权威）。
TEST(PluginPanel, Properties_CompoundValuesExpandedAndChineseTokenDisplay)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-07", "UX-02"},
                  std::vector<std::string>{});

    ModelingWorkingSet ws = makeSixAxisDraft();
    auto rowOf = [](const ModelingWorkingSet& w, const core::ObjectId& oid,
                    const char* key) {
        const auto rows = propertyFieldsFor(w, *resolveSelection(w, oid));
        for (const auto& r : rows) {
            if (r.fieldKey == key) { return r; }
        }
        return PropertyFieldRow{};  // 未命中＝缺省行（下述断言即失败）
    };

    // 关节 0（表 T-MDL-1 种子：Revolute＋axis Z＋原点恒位姿）：复合 Provided
    // 态展开为精确数值，类型值＝中文呈现映射。
    const core::ObjectId& j0 = ws.design.joints[0].objectId;
    EXPECT_EQ(rowOf(ws, j0, "axis").valueText, "0, 0, 1")
        << "轴向 Provided 态展开为三维分量（不再落\"[值已提供]\"占位）";
    EXPECT_EQ(rowOf(ws, j0, "origin").valueText, "0, 0, 0, 0, 0, 0")
        << "原点 Provided 态展开为位姿六值（恒位姿种子）";
    EXPECT_EQ(rowOf(ws, j0, "type").valueText, "转动（Revolute）")
        << "类型值＝中文呈现映射（token 括注保留）";

    // 连杆惯量：注入已提供值后展开为六分量（字段序＝InertiaTensor 声明序）。
    InertiaTensor inertiaSeed;
    inertiaSeed.ixx = 1.5;
    inertiaSeed.iyy = 2.5;
    inertiaSeed.izz = 3.5;
    inertiaSeed.ixy = 0.1;
    ws.design.links[0].body.inertia =
        core::SourcedValue<InertiaTensor>::provided(
            inertiaSeed,
            core::ValueProvenance::make(core::ProvenanceKind::UserProvided));
    EXPECT_EQ(rowOf(ws, ws.design.links[0].objectId, "inertia").valueText,
              "1.5, 2.5, 3.5, 0.1, 0, 0")
        << "惯量 Provided 态展开为六分量";

    // 场景：世界位姿非恒等（平移 (1,2,3)＋绕 z 轴 90°）——RPY 反解确定性
    // （Rz(π/2)→roll=0/pitch=0/yaw=π/2，呈现专用逐元素反解）；角色值＝
    // 中文呈现映射。
    SceneObject scene;
    scene.objectId = fixedOid(0xB1);
    scene.localName = "table";
    scene.role = SceneObjectRole::EnvironmentObject;
    scene.worldPose = rw::math::Transform3D<double>(
        rw::math::Vector3D<double>(1.0, 2.0, 3.0),
        rw::math::Rotation3D<double>(0.0, -1.0, 0.0, 1.0, 0.0, 0.0,
                                     0.0, 0.0, 1.0));
    ws.sceneObjects.push_back(scene);
    EXPECT_EQ(rowOf(ws, scene.objectId, "world-pose").valueText,
              "1, 2, 3, 0, 0, 1.570796")
        << "世界位姿展开为六值（formatDeterministic 6 位裁尾零）";
    EXPECT_EQ(rowOf(ws, scene.objectId, "role").valueText,
              "环境物体（EnvironmentObject）")
        << "场景角色值＝中文呈现映射";

    // 工具：安装接口恒有值（Transform3D 值语义、非 SourcedValue）——恒等
    // 种子展开为六值零（此前硬编码"[位姿已提供]"占位）。
    ToolDefinition tool;
    tool.objectId = fixedOid(0xA1);
    tool.localName = "gripper";
    ws.toolObjects.push_back(tool);
    EXPECT_EQ(rowOf(ws, tool.objectId, "mount-interface").valueText,
              "0, 0, 0, 0, 0, 0")
        << "安装接口展开为位姿六值（不再落占位）";

    // 根：权威模式值中文映射两态各断言一次（模板草稿 rootObjectId 为空
    // ——夹具显式注入根身份以定位 ModelRoot 目标，与 resolveSelection
    // 闭包语义一致）。
    ws.rootObjectId = fixedOid(0xD0);
    EXPECT_EQ(rowOf(ws, *ws.rootObjectId, "authority").valueText,
              "显式（Explicit）");
    ws.design.authority = AuthorityMode::StandardDH;
    EXPECT_EQ(rowOf(ws, *ws.rootObjectId, "authority").valueText,
              "标准DH（StandardDH）");
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

/**
 * @brief NotProvided 就绪提示保留对象、字段、单位和修复建议（可靠设计的
 *        最小呈现契约——UI 可据此定位而无需猜测计算语义）。
 */
TEST(PluginPanel, ReadinessBar_NotProvidedCarriesFieldUnitAndAction_WP13T23)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-06", "MDL-16"},
                  std::vector<std::string>{});

    ModelReadinessReport report;
    ReadinessNote note;
    note.layer = ReadinessLayer::L9Drivetrain;
    note.subjectPath = "drivetrain.velocityAcceleration";
    note.summary = "速度/加速度未进入建模 schema";
    note.kind = ReadinessNoteKind::NotProvided;
    note.subject = fixedOid(0xC0);
    note.fieldPath = "joints[*].maxVelocity / joints[*].maxAcceleration";
    note.unitText = "rad·s⁻¹ / rad·s⁻²";
    note.recommendedAction = "在传动或策略模块补录";
    report.notes.push_back(note);

    const auto bar = projectReadinessBar(report);
    ASSERT_EQ(bar.items.size(), std::size_t{1});
    EXPECT_EQ(bar.items[0].severity, "not-provided");
    EXPECT_EQ(bar.counts.notProvided, std::size_t{1});
    ASSERT_TRUE(bar.items[0].jumpTarget.has_value());
    EXPECT_TRUE(*bar.items[0].jumpTarget == fixedOid(0xC0));
    EXPECT_EQ(bar.items[0].fieldPath, note.fieldPath);
    EXPECT_EQ(bar.items[0].unitText, note.unitText);
    EXPECT_EQ(bar.items[0].recommendedAction, note.recommendedAction);
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

/// 十二条 CommandId 点分小写、逐行与卡 §9.7.3 表一致、无重复（export-package
/// 与 import-package 两条独立命令不合并；export-workcell-xml 为 UI-T56
/// 表尾追加——WC/DWC XML 外供导出；import-workcell 为 WP-13-T25/F06
/// 表尾追加——外部 WorkCell 反向导入）。
TEST(PluginPanel, Commands_TenDottedCommandIds_WP13T15_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-07"}, std::vector<std::string>{});

    const auto cmds = modelingDomainCommands();
    ASSERT_EQ(cmds.size(), 12U) << "卡 §9.7.3 表恰十二条（UI-T56＋WP-13-T25/F06 表尾追加）";

    // 逐行对照（行序＝卡表行序——登记序确定性）。
    const char* expected[12] = {
        "modeling.new-from-template", "modeling.import-urdf",
        "modeling.import-xacro", "modeling.switch-authority",
        "modeling.estimate-properties", "modeling.generate-placeholder-geometry",
        "modeling.diff-baseline", "modeling.export-package",
        "modeling.import-package", "modeling.reset-home-zero",
        "modeling.export-workcell-xml", "modeling.import-workcell",
    };
    std::set<std::string> ids;
    for (std::size_t i = 0; i < cmds.size(); ++i) {
        EXPECT_EQ(cmds[i].id, expected[i]) << "第 " << i << " 行与卡表不一致";
        ids.insert(cmds[i].id);
    }
    EXPECT_EQ(ids.size(), 12U) << "无重复 id（点分小写全局唯一——§7.2）";

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
        "modeling.reset-home-zero", "modeling.export-workcell-xml",
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

// =====================================================================
// UI-T66——分组重演（L-4 的组粒度推广：编辑页一次分组提交＝一个重演组，
// 组内保序整组落位/整组手工处置；属性行单字段轨 groupId=0 兼容回归）
// =====================================================================

/// 同组多条编辑组内保序落位＋跨组继续（分组重演的正向语义面）。
TEST(PluginPanel, Refresh_GroupedReplayWholeGroupsInOrder_UI_T66)
{
    IRD_TEST_INFO(std::vector<std::string>{"L-4", "MDL-07"},
                  std::vector<std::string>{"UI-T66-ACC2"});

    ModelingWorkingSet ws = makeSixAxisDraft();
    int refreshCount = 0;
    PanelRefreshCoordinator coordinator;
    coordinator.setSinks([&ws]() -> ModelingWorkingSet& { return ws; },
                         [&refreshCount]() { ++refreshCount; });

    // 组 7（模拟编辑页一轮分组提交：零位＋限位两字段同组）＋组 8（独立
    // 零位编辑）——组间以 groupId 变化为界，重演跨组继续。
    PendingEdit g1a;
    g1a.jointIndex = 0;
    g1a.field = JointEditField::ZeroOffset;
    g1a.value = 0.5;
    g1a.groupId = 7;
    PendingEdit g1b;
    g1b.jointIndex = 0;
    g1b.field = JointEditField::Bounds;
    g1b.value = JointLimits{-2.0, 2.0};
    g1b.groupId = 7;
    PendingEdit g2;
    g2.jointIndex = 1;
    g2.field = JointEditField::ZeroOffset;
    g2.value = -0.25;
    g2.groupId = 8;
    coordinator.recordPending(g1a);
    coordinator.recordPending(g1b);
    coordinator.recordPending(g2);

    const auto outcome = coordinator.onRevisionEvent(std::nullopt);
    EXPECT_EQ(outcome, ReplayOutcome::Replayed);
    EXPECT_FALSE(coordinator.manualInterventionRequired());
    EXPECT_EQ(refreshCount, 1);  // 全面板刷新恰一次（组粒度不改事件语义）
    // 组内保序落位：两条编辑都生效（同组原子呈现——工作集含组 7 全部）。
    EXPECT_DOUBLE_EQ(ws.design.joints[0].zeroOffset, 0.5);
    ASSERT_TRUE(ws.design.joints[0].bounds.tryValue().has_value());
    EXPECT_DOUBLE_EQ(ws.design.joints[0].bounds.tryValue()->second, 2.0);
    // 跨组继续：组 8 亦落位（失败即停语义只在被拒时生效）。
    EXPECT_DOUBLE_EQ(ws.design.joints[1].zeroOffset, -0.25);
    // 队列原样保留（编辑意图由用户处置）。
    EXPECT_EQ(coordinator.pending().size(), 3U);
}

/// 组内中途被拒＝整组手工处置（blockedAt 指组首；后续组保留；不半组
/// 静默——组粒度的"不静默丢弃"）。
TEST(PluginPanel, Refresh_GroupReplayBlockedKeepsWholeGroup_UI_T66)
{
    IRD_TEST_INFO(std::vector<std::string>{"L-4", "ERR-01"},
                  std::vector<std::string>{"UI-T66-ACC2"});

    ModelingWorkingSet ws = makeSixAxisDraft();
    int refreshCount = 0;
    PanelRefreshCoordinator coordinator;
    coordinator.setSinks([&ws]() -> ModelingWorkingSet& { return ws; },
                         [&refreshCount]() { ++refreshCount; });

    // 组 1：e1 零位合法（组内先落位）→ e2 类型改 Continuous（六轴夹具
    // 限位 Provided——I-MDL-4 组合约束域拒 TypeBoundsConflict）。组 2：
    // 合法零位编辑（被拒组之后的现场保留载体）。
    PendingEdit g1a;
    g1a.jointIndex = 0;
    g1a.field = JointEditField::ZeroOffset;
    g1a.value = 0.5;
    g1a.groupId = 1;
    PendingEdit g1b;
    g1b.jointIndex = 0;
    g1b.field = JointEditField::Type;
    g1b.value = JointEditValue{JointType::Continuous};
    g1b.groupId = 1;
    PendingEdit g2;
    g2.jointIndex = 1;
    g2.field = JointEditField::ZeroOffset;
    g2.value = -0.25;
    g2.groupId = 2;
    coordinator.recordPending(g1a);
    coordinator.recordPending(g1b);
    coordinator.recordPending(g2);

    const auto outcome = coordinator.onRevisionEvent(std::nullopt);
    EXPECT_EQ(outcome, ReplayOutcome::BlockedAtEdit);
    ASSERT_TRUE(coordinator.blockedAtIndex().has_value());
    // blockedAt＝失败组组首（整组手工处置的锚——非被拒条下标 1）。
    EXPECT_EQ(*coordinator.blockedAtIndex(), 0U)
        << "整组语义：blockedAt 指组首而非被拒条";
    EXPECT_TRUE(coordinator.manualInterventionRequired());
    EXPECT_EQ(refreshCount, 1);  // 失败态同样刷新（横幅可见）
    // 队列现场全保留（被拒组两条＋后续组——不半组静默不丢弃）。
    ASSERT_EQ(coordinator.pending().size(), 3U);
    EXPECT_EQ(coordinator.pending()[0].groupId, 1U);
    // 组内前缀已落位（重演落权威工作集——e1 零位 0.5 已生效；被拒类型
    // 未触碰——域拒绝强保证，关节类型保持 Revolute）。
    EXPECT_DOUBLE_EQ(ws.design.joints[0].zeroOffset, 0.5);
    EXPECT_TRUE(ws.design.joints[0].type == JointType::Revolute);
    // 后续组未重演（失败即停——跨组语义与单字段轨一致）。
    EXPECT_DOUBLE_EQ(ws.design.joints[1].zeroOffset, 0.0);
}

/// 类型编辑单条重演（groupId=0 独立单条轨——类型枚举行编辑入队后随
/// 修订事件落位；Revolute→Prismatic 带限位＝I-MDL-4 合法组合轨）。
TEST(PluginPanel, Refresh_TypeEditReplaySingleTrack_UI_T66)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-06", "I-MDL-4"},
                  std::vector<std::string>{"UI-T66-ACC1"});

    ModelingWorkingSet ws = makeSixAxisDraft();
    int refreshCount = 0;
    PanelRefreshCoordinator coordinator;
    coordinator.setSinks([&ws]() -> ModelingWorkingSet& { return ws; },
                         [&refreshCount]() { ++refreshCount; });

    PendingEdit e;
    e.jointIndex = 0;
    e.field = JointEditField::Type;
    e.value = JointEditValue{JointType::Prismatic};  // 限位 Provided＋workingRange 缺——合法
    e.groupId = 0;  // 独立单条（类型下拉轨——单编辑动作）
    coordinator.recordPending(e);

    const auto outcome = coordinator.onRevisionEvent(std::nullopt);
    EXPECT_EQ(outcome, ReplayOutcome::Replayed);
    EXPECT_TRUE(ws.design.joints[0].type == JointType::Prismatic)
        << "类型编辑经域分支落位（I-MDL-4 组合约束通过）";
    EXPECT_EQ(refreshCount, 1);
}

/// 声明表单语义即时校验纯函数（F-523① 录入辅助面——两规则与域同口径；
/// 域拒绝面兜底语义不变，本函数只前置提示）。
TEST(PluginPanel, CustomChainRowSemanticCheck_TableDriven_UI_T66)
{
    IRD_TEST_INFO(std::vector<std::string>{"F-523", "I-MDL-4"},
                  std::vector<std::string>{"UI-T66-ACC3"});

    // 限位逆序/相等＝拒（I-MDL-4）。
    const auto reversed = customChainRowSemanticCheck(0.0, 0.0, 1.0, 0.5, 0.2);
    ASSERT_TRUE(reversed.has_value());
    EXPECT_NE(reversed->find("I-MDL-4"), std::string::npos);
    const auto equal = customChainRowSemanticCheck(0.0, 0.0, 1.0, 0.5, 0.5);
    ASSERT_TRUE(equal.has_value());
    EXPECT_NE(equal->find("I-MDL-4"), std::string::npos);
    // 轴零向量＝拒（I-MDL-6）。
    const auto zeroAxis = customChainRowSemanticCheck(0.0, 0.0, 0.0, -1.0, 1.0);
    ASSERT_TRUE(zeroAxis.has_value());
    EXPECT_NE(zeroAxis->find("I-MDL-6"), std::string::npos);
    // 合法行＝通过（T-MDL-1 J1 种子形态——轴 z＋对称限位）。
    EXPECT_EQ(customChainRowSemanticCheck(0.0, 0.0, 1.0, -1.0, 1.0), std::nullopt);
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

// ---------------------------------------------------------------------
// WP-24-T03b 收口的模块级用例迁移登记（WP-24-T03 验证修复）：原在本文件
// 尾段的 T03B 三用例（DomainReadinessSourceParity/SessionDetachAndRevision
// Advance/PolicyNameContextHonestEmptyPath——构造 ModelingUiModule，其
// policy 行程评估器符号仅集成模式可链）迁至 test/PluginModuleT03BTest.cpp
// （TARGET sdurw_kinematics 集成 gating——与 ReadinessTest/CommandHandlers
// Test 同款先例）。迁移不改任何断言语义；详见该文件头 gating 说明。
// ---------------------------------------------------------------------

// =====================================================================
// UI-T41 批次C：DH 投影参数行（C3）＋几何引用行形状徽标与悬空检测（C1/C2）
// =====================================================================

/// C3：dhDerived 在位时关节属性区追加四参数只读行（§7.2"DH 态权威可编辑"
/// 的呈现半区——面板就地提交轨未开，恒只读灰显＋DerivedReadOnly 徽标；
/// 无投影值＝零 DH 行——MDL-07 相关属性按值在否）。
TEST(PluginPanel, JointDhProjectionRows_ReadOnlyWithDerivedBadge_UI_T41C)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-07"}, std::vector<std::string>{});

    ModelingWorkingSet ws = makeSixAxisDraft();
    const auto target = resolveSelection(ws, ws.design.joints[0].objectId);
    ASSERT_TRUE(target.has_value());

    // 前置：模板种子无 DH 投影值——零 DH 行（不虚构）。
    auto countDhKeys = [](const std::vector<PropertyFieldRow>& rows) {
        std::size_t n = 0;
        for (const auto& r : rows) {
            if (r.fieldKey.rfind("dh-", 0) == 0) { ++n; }
        }
        return n;
    };
    ASSERT_EQ(countDhKeys(propertyFieldsFor(ws, *target)), std::size_t{0})
        << "种子草稿不应带 DH 投影行（夹具前置）";

    // 注入投影值（显式→DH 判定的产物形态——批次A 权威切换流写入面）。
    ws.design.joints[0].dhDerived = DhParameters{0.1, 0.2, 0.3, 1.5708};
    const auto rows = propertyFieldsFor(ws, *target);
    EXPECT_EQ(countDhKeys(rows), std::size_t{4}) << "四参数行齐全（α/a/d/θ）";
    std::set<std::string> dhKeys;
    for (const auto& r : rows) {
        if (r.fieldKey.rfind("dh-", 0) != 0) { continue; }
        dhKeys.insert(r.fieldKey);
        EXPECT_EQ(r.enablement, FieldEnablement::ReadOnlyGrey)
            << "DH 投影行恒只读灰显（写入口＝权威切换流）";
        ASSERT_TRUE(r.provenance.has_value());
        EXPECT_EQ(*r.provenance, core::ProvenanceKind::DerivedReadOnly)
            << "DH 投影行来源徽标＝派生只读";
    }
    EXPECT_TRUE(dhKeys.count("dh-alpha") && dhKeys.count("dh-a")
                && dhKeys.count("dh-d") && dhKeys.count("dh-theta"));
}

/// C1/C2：几何引用行＝形状类别徽标（Primitive/Mesh——GeometryKind 直投）
/// ＋引用完整性（清单无对应条目＝⚠ 悬空警示——"让悬空可见"的呈现半区；
/// 占位原语的清单登记受 schema 边界，悬空如实呈现不伪造解析）。
TEST(PluginPanel, GeometryRows_ShapeBadgeAndDanglingDetection_UI_T41C)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-07"}, std::vector<std::string>{});

    ModelingWorkingSet ws = makeSixAxisDraft();
    ws.design.links[0].visual = GeometryRef{};
    ws.design.links[0].visual->resourceRefId = "dangling-mesh";
    ws.design.links[0].visual->kind = GeometryKind::Mesh;

    auto rowOf = [&ws](const char* key) {
        const auto target = resolveSelection(ws, ws.design.links[0].objectId);
        const auto rows = propertyFieldsFor(ws, *target);
        for (const auto& r : rows) {
            if (r.fieldKey == key) { return r; }
        }
        return PropertyFieldRow{};  // 未命中＝缺省行（下述断言即失败）
    };

    // 悬空引用（清单空）：⚠ 前缀＋形状徽标＋说明。
    PropertyFieldRow visual = rowOf("visual-geometry");
    EXPECT_TRUE(visual.valueText.find("\xe2\x9a\xa0") != std::string::npos)  // "⚠"
        << "悬空引用未带警示前缀";
    EXPECT_TRUE(visual.valueText.find("dangling-mesh") != std::string::npos);
    EXPECT_TRUE(visual.valueText.find("未入资源清单") != std::string::npos);

    // 清单登记同键条目后：悬空消失（可解析形态＝"类别 · 键"）。
    ResourceRef entry;
    entry.resourceId = "dangling-mesh";
    entry.externalRecord = ExternalResourceRecord{};
    entry.externalRecord->absPath = "D:/demo/dangling-mesh.stl";
    ws.design.resourceManifest.push_back(std::move(entry));
    visual = rowOf("visual-geometry");
    EXPECT_TRUE(visual.valueText.find("\xe2\x9a\xa0") == std::string::npos)
        << "可解析引用不应带警示";
    EXPECT_TRUE(visual.valueText.find("dangling-mesh") != std::string::npos);
    EXPECT_TRUE(visual.valueText.find(
        std::string(geometryKindToken(GeometryKind::Mesh))) != std::string::npos)
        << "形状类别徽标缺失（C2）";
}

/** @brief MDL-05/V-17：独立闭式迁移与完整覆盖，保持未改字段及来源。 */
TEST(ModelingManualBody, ExplicitMigrationAndCompleteOverride_MDL05_V17)
{
    IRD_TEST_INFO("MDL-05",{},std::nullopt);
    BodyData body;const auto source=core::ValueProvenance::make(core::ProvenanceKind::GeometricEstimate);
    body.mass=core::SourcedValue<double>::provided(2,source);
    body.centerOfMass=core::SourcedValue<rw::math::Vector3D<double>>::provided({0,0,0},source);
    body.inertia=core::SourcedValue<InertiaTensor>::provided({3,4,5,0.1,0.2,0.3},source);
    const auto before=body;std::string error;ManualBodyEdit edit;
    auto out=evaluateManualBodyEdit(body,edit,error);ASSERT_TRUE(out);EXPECT_EQ(out->body,body);
    edit.mass=4;out=evaluateManualBodyEdit(body,edit,error);ASSERT_TRUE(out);
    EXPECT_DOUBLE_EQ(out->body.mass.value(),4);EXPECT_EQ(out->body.inertia,body.inertia);
    EXPECT_EQ(out->body.centerOfMass,body.centerOfMass);
    edit.centerOfMass=rw::math::Vector3D<double>{1,2,3};
    EXPECT_FALSE(evaluateManualBodyEdit(body,edit,error));EXPECT_FALSE(error.empty());
    edit.resolution=CentroidEditResolution::MigrateInertia;
    out=evaluateManualBodyEdit(body,edit,error);ASSERT_TRUE(out)<<error;
    // 质量4kg为用户整体物性覆盖；COM安装平移另按C01保持原张量。
    const auto& t=out->body.inertia.value();
    // C01：刚体整体平移，关于其实际COM的张量不膨胀。
    EXPECT_DOUBLE_EQ(t.ixx,3);EXPECT_DOUBLE_EQ(t.iyy,4);EXPECT_DOUBLE_EQ(t.izz,5);
    EXPECT_DOUBLE_EQ(t.ixy,0.1);EXPECT_DOUBLE_EQ(t.ixz,0.2);EXPECT_DOUBLE_EQ(t.iyz,0.3);
    EXPECT_EQ(out->body.inertia.provenance(),source);
    EXPECT_EQ(out->body.centerOfMass.provenance().kind,core::ProvenanceKind::UserProvided);
    EXPECT_NE(out->summary.find("migrate-inertia"),std::string::npos);
    edit.resolution=CentroidEditResolution::OverwriteInertia;
    EXPECT_FALSE(evaluateManualBodyEdit(body,edit,error));
    edit.inertia=InertiaTensor{5,6,7,0.4,0.5,0.6};
    out=evaluateManualBodyEdit(body,edit,error);ASSERT_TRUE(out)<<error;
    EXPECT_EQ(out->body.inertia.value(),*edit.inertia);
    EXPECT_EQ(out->body.inertia.provenance().kind,core::ProvenanceKind::UserProvided);
    EXPECT_NE(out->summary.find("overwrite-inertia"),std::string::npos);EXPECT_EQ(body,before);
    // 完整六分量与来源必须经真实Codec保持，不能只验证窗口内存中的值。
    const RobotDesignTemplateFactory factory;std::vector<core::DiagnosticRecord> diags;
    auto draft=factory.createDraft(TemplateId{kTemplateIdGeneric6R},
        runtime::InstallationPresetToken::Ground,"body-codec",diags).get();
    draft.design.links.back().body=out->body;
    const RobotDesignCodec codec;const auto bytes=codec.encode(ObjectVariant{draft.design},kCurrentFormatVersion);
    ASSERT_TRUE(bytes.ok());const auto decoded=codec.decode(bytes.get(),kCurrentFormatVersion);
    ASSERT_TRUE(decoded.ok());EXPECT_EQ(std::get<RobotDesign>(decoded.get()),draft.design);

}

/** @brief MDL-05/06：缺基准、未决议、非有限、负质量和非法完整张量均拒绝。 */
TEST(ModelingManualBody, MissingBaselineAndInvalidInputsRemainAtomic_MDL05_V17)
{
    IRD_TEST_INFO("MDL-05",{},std::nullopt);
    BodyData body;std::string error;ManualBodyEdit edit;
    edit.mass=2;edit.centerOfMass=rw::math::Vector3D<double>{1,2,3};
    edit.resolution=CentroidEditResolution::MigrateInertia;
    EXPECT_FALSE(evaluateManualBodyEdit(body,edit,error));EXPECT_EQ(body,BodyData{});
    edit.resolution=CentroidEditResolution::OverwriteInertia;edit.inertia=InertiaTensor{3,4,5,0.1,0.2,0.3};
    auto out=evaluateManualBodyEdit(body,edit,error);ASSERT_TRUE(out)<<error;
    EXPECT_DOUBLE_EQ(out->body.mass.value(),2);EXPECT_DOUBLE_EQ(out->body.centerOfMass.value()[2],3);
    const auto full=out->body;
    for(const double mass:{-1.0,0.0,std::numeric_limits<double>::infinity()}){
        ManualBodyEdit bad;bad.mass=mass;EXPECT_FALSE(evaluateManualBodyEdit(full,bad,error));
    }
    for(const auto tensor:{InertiaTensor{-1,2,2,0,0,0},InertiaTensor{1,1,8,0,0,0},
                          InertiaTensor{1,1,1,5,0,0}}){
        ManualBodyEdit bad;bad.inertia=tensor;EXPECT_FALSE(evaluateManualBodyEdit(full,bad,error));
    }
    ManualBodyEdit bad;bad.centerOfMass=rw::math::Vector3D<double>{0,std::numeric_limits<double>::quiet_NaN(),0};
    EXPECT_FALSE(evaluateManualBodyEdit(full,bad,error));EXPECT_EQ(body,BodyData{});
}

/** @brief F12/MDL-15/AT-28：固定帧增改删原子更新根引用与对象，世界位姿不被重置。 */
TEST(ModelingSceneTransactions, CreateUpdateRemoveAndCodec_MDL15_AT28)
{
    IRD_TEST_INFO("MDL-15",{"AT-28"},std::nullopt);
    const auto before=makeSixAxisDraft();
    SceneEditRequest request;request.target=core::ObjectId::generate();request.localName="fixture";
    std::string error;
    const auto added=evaluateSceneEdit(before,request,{},error);ASSERT_TRUE(added)<<error;
    ASSERT_EQ(added->sceneObjects.size(),1U);ASSERT_EQ(added->design.sceneRefs.size(),1U);
    EXPECT_EQ(added->design.sceneRefs.front(),request.target);
    EXPECT_EQ(added->sceneObjects.front().objectId,request.target);EXPECT_FALSE(added->sceneObjects.front().geometry);
    EXPECT_EQ(added->changes.size(),before.changes.size()+1);
    auto positioned=*added;
    ASSERT_FALSE(applyScenePoseEdit(positioned,0,PartPoseEditValue{1,2,3,0.1,0.2,0.3}));
    request.action=SceneEditAction::Update;request.localName="fixture_renamed";
    request.role=SceneObjectRole::Workpiece;
    const auto updated=evaluateSceneEdit(positioned,request,{},error);ASSERT_TRUE(updated)<<error;
    EXPECT_EQ(updated->sceneObjects.front().worldPose,positioned.sceneObjects.front().worldPose);
    EXPECT_EQ(updated->sceneObjects.front().role,SceneObjectRole::Workpiece);
    EXPECT_EQ(updated->sceneObjects.front().localName,"fixture_renamed");
    // 实际独立对象编码/解码，不把纯结构相等当成持久化兼容。
    const RobotDesignCodec codec;
    const auto bytes=codec.encode(ObjectVariant{updated->sceneObjects.front()},kCurrentFormatVersion);
    ASSERT_TRUE(bytes.ok());const auto decoded=codec.decode(bytes.get(),kCurrentFormatVersion);
    ASSERT_TRUE(decoded.ok());EXPECT_EQ(std::get<SceneObject>(decoded.get()),updated->sceneObjects.front());
    const auto unchanged=evaluateSceneEdit(*updated,request,{},error);ASSERT_TRUE(unchanged);
    EXPECT_EQ(*unchanged,*updated)<<"恒等更新不追加摘要或历史";
    request.action=SceneEditAction::Remove;
    EXPECT_FALSE(evaluateSceneEdit(*updated,request,{},error));EXPECT_FALSE(error.empty());
    request.confirmRemove=true;
    const auto removed=evaluateSceneEdit(*updated,request,{},error);ASSERT_TRUE(removed)<<error;
    EXPECT_TRUE(removed->sceneObjects.empty());EXPECT_TRUE(removed->design.sceneRefs.empty());
    EXPECT_EQ(before.sceneObjects.size(),0U)<<"初始输入始终只读";
}
/** @brief F12：非法输入/身份/资源拒绝；共享资源不因删除或摘除而丢失。 */
TEST(ModelingSceneTransactions, RejectAndSharedResource_MDL15_AT28)
{
    IRD_TEST_INFO("MDL-15",{"AT-28"},std::nullopt);
    auto before=makeSixAxisDraft();ResourceRef resource;resource.resourceId="shared";
    resource.externalRecord=ExternalResourceRecord{"C:/fixture.stl",resource.contentDigest};
    before.design.resourceManifest.push_back(resource);
    SceneEditRequest request;request.target=core::ObjectId::generate();request.localName="obstacle";
    request.source=SceneGeometrySource::Existing;request.resourceOrPath="shared";
    std::string error;const auto added=evaluateSceneEdit(before,request,{},error);ASSERT_TRUE(added)<<error;
    ASSERT_TRUE(added->sceneObjects.front().geometry);
    EXPECT_EQ(added->sceneObjects.front().geometry->resourceRefId,"shared");
    EXPECT_EQ(added->design.resourceManifest,before.design.resourceManifest);
    const auto baseline=*added;
    for(const auto& name:std::vector<std::string>{"",before.design.links.front().localName,"bad\nname","obstacle"}){
        request.target=core::ObjectId::generate();request.localName=name;
        EXPECT_FALSE(evaluateSceneEdit(baseline,request,{},error))<<name;
        EXPECT_EQ(*added,baseline);
    }
    request.action=SceneEditAction::Update;request.target=baseline.sceneObjects.front().objectId;
    request.localName="obstacle";request.resourceOrPath="missing";
    EXPECT_FALSE(evaluateSceneEdit(baseline,request,{},error));
    request.source=SceneGeometrySource::External;request.resourceOrPath="missing.stl";
    EXPECT_FALSE(evaluateSceneEdit(baseline,request,{},error));
    request.source=SceneGeometrySource::None;
    const auto detached=evaluateSceneEdit(baseline,request,{},error);ASSERT_TRUE(detached)<<error;
    EXPECT_FALSE(detached->sceneObjects.front().geometry);EXPECT_EQ(detached->design.resourceManifest,before.design.resourceManifest);
    request.action=SceneEditAction::Remove;request.confirmRemove=true;
    const auto removed=evaluateSceneEdit(baseline,request,{},error);ASSERT_TRUE(removed);
    EXPECT_EQ(removed->design.resourceManifest,before.design.resourceManifest);
    request.target=core::ObjectId::generate();EXPECT_FALSE(evaluateSceneEdit(baseline,request,{},error));
}

/** @brief F12/MDL-15：真实磁盘/安全io快照登记、重复确认与缺文件拒绝，不伪造摘要。 */
TEST(ModelingSceneTransactions, ExternalDiskGeometryAndFailureAtomicity_MDL15_AT28)
{
    IRD_TEST_INFO("MDL-15",{"AT-28"},std::nullopt);
    QTemporaryDir dir;ASSERT_TRUE(dir.isValid());const auto path=dir.filePath(QStringLiteral("障碍物.stl"));
    QFile file(path);ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    const QByteArray mesh="solid obstacle\nfacet normal 0 0 1\nouter loop\nvertex 0 0 0\nvertex 1 0 0\nvertex 0 1 0\nendloop\nendfacet\nendsolid obstacle\n";
    ASSERT_EQ(file.write(mesh),mesh.size());file.close();
    const auto before=makeSixAxisDraft();SceneEditRequest request;
    request.target=core::ObjectId::generate();request.localName="obstacle";
    request.source=SceneGeometrySource::External;request.resourceOrPath=path.toStdString();
    const auto probe=makeIoResourceProbe();std::string error;
    const auto added=evaluateSceneEdit(before,request,probe,error);ASSERT_TRUE(added)<<error;
    ASSERT_EQ(added->design.resourceManifest.size(),before.design.resourceManifest.size()+1);
    const auto& resource=added->design.resourceManifest.back();EXPECT_EQ(resource.state,ResourceState::Recorded);
    ASSERT_TRUE(resource.externalRecord);EXPECT_EQ(resource.contentDigest,resource.externalRecord->recordedDigest);
    EXPECT_NE(resource.contentDigest,core::Digest256{});ASSERT_TRUE(added->sceneObjects.front().geometry);
    EXPECT_EQ(resource.resourceId,added->sceneObjects.front().geometry->resourceRefId);
    request.action=SceneEditAction::Update;
    const auto same=evaluateSceneEdit(*added,request,probe,error);ASSERT_TRUE(same)<<error;EXPECT_EQ(*same,*added);
    request.resourceOrPath=dir.filePath("missing.stl").toStdString();
    EXPECT_FALSE(evaluateSceneEdit(*added,request,probe,error));EXPECT_FALSE(error.empty());
    QFile text(dir.filePath("not-geometry.urdf"));ASSERT_TRUE(text.open(QIODevice::WriteOnly));
    ASSERT_GT(text.write("<robot name=\"not-mesh\"/>"),0);text.close();
    request.resourceOrPath=text.fileName().toStdString();
    EXPECT_FALSE(evaluateSceneEdit(*added,request,probe,error));
    EXPECT_EQ(added->design.resourceManifest.back(),resource);EXPECT_TRUE(before.sceneObjects.empty());
}

/** @brief F12：正式编译桥的名称约束和非几何资源反例必须在确认时拒绝。 */
TEST(ModelingSceneTransactions, RejectIllegalNameAndSourceKinds_MDL15_AT28)
{
    IRD_TEST_INFO("MDL-15",{"AT-28"},std::nullopt);
    auto before=makeSixAxisDraft();SceneEditRequest request;request.target=core::ObjectId::generate();
    std::string error;
    for(const auto& name:std::vector<std::string>{"with space","slash/name","name:bad","中文名称"}){
        request.localName=name;EXPECT_FALSE(evaluateSceneEdit(before,request,{},error))<<name;
    }
    request.localName="fixture";request.source=SceneGeometrySource::Existing;request.resourceOrPath="source-xml";
    ResourceRef ref;ref.resourceId="source-xml";ref.externalRecord=ExternalResourceRecord{"C:/source.xacro",ref.contentDigest};
    before.design.resourceManifest.push_back(ref);
    EXPECT_FALSE(evaluateSceneEdit(before,request,{},error))<<"来源XML不是可选几何";
    request.source=SceneGeometrySource::None;request.role=SceneObjectRole::RobotLink;
    EXPECT_FALSE(evaluateSceneEdit(before,request,{},error));
    // 环境管理器不能通过删除绕过角色边界；导入/旧数据中的机器人角色须交对应编辑入口。
    SceneObject robotPart;robotPart.objectId=request.target;robotPart.localName="robot_part";
    robotPart.role=SceneObjectRole::RobotLink;before.sceneObjects.push_back(robotPart);
    before.design.sceneRefs.push_back(robotPart.objectId);
    request.action=SceneEditAction::Remove;request.confirmRemove=true;
    EXPECT_FALSE(evaluateSceneEdit(before,request,{},error))<<"删除也须遵循环境/工件角色边界";
}
/** @brief F08/MDL-13：新增/改名/删除的根引用和默认TCP必须原子一致，更新保留安装/TCP/物性。 */
TEST(ModelingToolTransactions, CrudDefaultAndPreserve_MDL13_AT01)
{
    IRD_TEST_INFO("MDL-13",{"AT-01"},std::nullopt);
    const auto before=makeSixAxisDraft();ToolEditRequest r;r.target=core::ObjectId::generate();r.localName="gripper";
    std::string error;EXPECT_FALSE(evaluateToolEdit(before,r,{},error))<<"首工具必须显式选择默认";
    r.makeDefault=true;const auto created=evaluateToolEdit(before,r,{},error);ASSERT_TRUE(created)<<error;
    ASSERT_EQ(created->toolObjects.size(),1U);ASSERT_TRUE(created->design.defaultTcp);
    EXPECT_EQ(created->design.toolRefs,std::vector<core::ObjectId>{r.target});
    EXPECT_EQ(created->design.defaultTcp->toolOid,r.target);EXPECT_EQ(created->design.defaultTcp->tcpKey,"tcp");
    auto baseline=*created;auto& tool=baseline.toolObjects.front();tool.mountInterface.P()[0]=0.125;
    tool.tcpList.front().offset.P()[2]=0.3;TcpEntry extra;extra.key="probe";extra.offset.P()[1]=0.2;tool.tcpList.push_back(extra);
    tool.body.mass=core::SourcedValue<double>::provided(2,core::ValueProvenance::make(core::ProvenanceKind::ImportMapped));
    r.action=ToolEditAction::Update;r.localName="gripper_renamed";r.displayName="焊接夹具";
    const auto updated=evaluateToolEdit(baseline,r,{},error);ASSERT_TRUE(updated)<<error;
    auto expected=baseline.toolObjects.front();expected.localName=r.localName;expected.displayName=r.displayName;
    EXPECT_EQ(updated->toolObjects.front(),expected);
    const auto same=evaluateToolEdit(*updated,r,{},error);ASSERT_TRUE(same);EXPECT_EQ(*same,*updated);
    r.makeDefault=false;EXPECT_FALSE(evaluateToolEdit(*updated,r,{},error));
    r.action=ToolEditAction::Remove;EXPECT_FALSE(evaluateToolEdit(*updated,r,{},error));
    r.confirmRemove=true;const auto removed=evaluateToolEdit(*updated,r,{},error);ASSERT_TRUE(removed)<<error;
    EXPECT_TRUE(removed->toolObjects.empty());EXPECT_TRUE(removed->design.toolRefs.empty());EXPECT_FALSE(removed->design.defaultTcp);
    EXPECT_TRUE(before.toolObjects.empty())<<"输入完整基线从未被修改";
}

/** @brief F08：默认工具有其他候选时禁止猜测替代，显式改默认后才允许删除旧工具。 */
TEST(ModelingToolTransactions, MultiToolDefaultProtection_MDL13_AT28)
{
    IRD_TEST_INFO("MDL-13",{"AT-28"},std::nullopt);
    ToolEditRequest r;r.target=core::ObjectId::generate();r.localName="first";r.makeDefault=true;std::string error;
    const auto first=evaluateToolEdit(makeSixAxisDraft(),r,{},error);ASSERT_TRUE(first);
    const auto firstId=r.target;r.target=core::ObjectId::generate();r.localName="second";r.makeDefault=false;
    const auto two=evaluateToolEdit(*first,r,{},error);ASSERT_TRUE(two)<<error;
    EXPECT_EQ(two->design.defaultTcp->toolOid,firstId);
    ToolEditRequest remove;remove.action=ToolEditAction::Remove;remove.target=firstId;remove.confirmRemove=true;
    EXPECT_FALSE(evaluateToolEdit(*two,remove,{},error));EXPECT_TRUE(error.find("默认TCP")!=std::string::npos);
    r.action=ToolEditAction::Update;r.makeDefault=true;const auto newDefault=evaluateToolEdit(*two,r,{},error);ASSERT_TRUE(newDefault);
    EXPECT_EQ(newDefault->design.defaultTcp->toolOid,r.target);
    const auto removed=evaluateToolEdit(*newDefault,remove,{},error);ASSERT_TRUE(removed)<<error;
    ASSERT_EQ(removed->toolObjects.size(),1U);EXPECT_EQ(removed->toolObjects.front().objectId,r.target);
}

/** @brief F08：非法名称、重复身份、失效引用及错误资源均拒绝，不把来源XML当网格。 */
TEST(ModelingToolTransactions, InvalidIdentityNamesReferencesAndResources_MDL13_AT01)
{
    IRD_TEST_INFO("MDL-13",{"AT-01"},std::nullopt);
    auto before=makeSixAxisDraft();ToolEditRequest r;r.localName="tool";r.makeDefault=true;std::string error;
    EXPECT_FALSE(evaluateToolEdit(before,r,{},error));r.target=before.design.joints.front().objectId;
    EXPECT_FALSE(evaluateToolEdit(before,r,{},error));r.target=core::ObjectId::generate();
    for(const auto& name:std::vector<std::string>{"","with space","a/b",before.design.joints.front().localName}){
        r.localName=name;EXPECT_FALSE(evaluateToolEdit(before,r,{},error))<<name;
    }
    r.localName="valid";r.source=ManagedGeometrySource::Existing;r.resourceOrPath="missing";
    EXPECT_FALSE(evaluateToolEdit(before,r,{},error));
    ResourceRef source;source.resourceId="source";source.externalRecord=ExternalResourceRecord{"C:/source.urdf",source.contentDigest};
    before.design.resourceManifest.push_back(source);r.resourceOrPath="source";EXPECT_FALSE(evaluateToolEdit(before,r,{},error));
    r.source=ManagedGeometrySource::External;r.resourceOrPath="C:/missing.stl";
    EXPECT_FALSE(evaluateToolEdit(before,r,{},error));
    r.source=ManagedGeometrySource::None;const auto added=evaluateToolEdit(before,r,{},error);ASSERT_TRUE(added);
    auto broken=*added;broken.design.toolRefs.push_back(r.target);r.action=ToolEditAction::Update;
    EXPECT_FALSE(evaluateToolEdit(broken,r,{},error));
}

/** @brief F08：真实中文磁盘网格沿io冻结摘要，替换保留局部位姿，删除保留共享资源，Codec完整往返。 */
TEST(ModelingToolTransactions, RealDiskResourceAndCodec_MDL13_AT28)
{
    IRD_TEST_INFO("MDL-13",{"AT-28"},std::nullopt);
    QTemporaryDir dir;ASSERT_TRUE(dir.isValid());QFile file(dir.filePath(QStringLiteral("夹具.stl")));
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    ASSERT_GT(file.write("solid tool\nfacet normal 0 0 1\nouter loop\nvertex 0 0 0\nvertex 1 0 0\nvertex 0 1 0\nendloop\nendfacet\nendsolid tool\n"),0);file.close();
    ToolEditRequest r;r.target=core::ObjectId::generate();r.localName="gripper";r.makeDefault=true;
    r.source=ManagedGeometrySource::External;r.resourceOrPath=file.fileName().toStdString();std::string error;const auto probe=makeIoResourceProbe();
    const auto added=evaluateToolEdit(makeSixAxisDraft(),r,probe,error);ASSERT_TRUE(added)<<error;
    ASSERT_TRUE(added->toolObjects.front().geometry);ASSERT_FALSE(added->design.resourceManifest.empty());
    const auto& resource=added->design.resourceManifest.back();EXPECT_NE(resource.contentDigest,core::Digest256{});
    ASSERT_TRUE(resource.externalRecord);EXPECT_EQ(resource.contentDigest,resource.externalRecord->recordedDigest);
    auto baseline=*added;baseline.toolObjects.front().geometry->localTransform.P()[0]=0.12;
    r.action=ToolEditAction::Update;const auto same=evaluateToolEdit(baseline,r,probe,error);ASSERT_TRUE(same)<<error;EXPECT_EQ(*same,baseline);
    r.source=ManagedGeometrySource::Existing;r.resourceOrPath=resource.resourceId;
    const auto existing=evaluateToolEdit(baseline,r,{},error);ASSERT_TRUE(existing);EXPECT_EQ(*existing,baseline);
    for(const auto& value:std::vector<ObjectVariant>{ObjectVariant(baseline.design),ObjectVariant(baseline.toolObjects.front())}){
        const RobotDesignCodec codec;const auto bytes=codec.encode(value,kCurrentFormatVersion);ASSERT_TRUE(bytes.ok());
        const auto decoded=codec.decode(bytes.get(),kCurrentFormatVersion);ASSERT_TRUE(decoded.ok());EXPECT_EQ(decoded.get(),value);
    }
    r.action=ToolEditAction::Remove;r.confirmRemove=true;const auto removed=evaluateToolEdit(baseline,r,{},error);ASSERT_TRUE(removed);
    EXPECT_EQ(removed->design.resourceManifest,baseline.design.resourceManifest);
}

/** @brief B4b/AT-21：真实中文磁盘UTF-8往返、完整工具/场景值及封装确定性。 */
TEST(ModelingDraftCodec, DiskRoundTripAndLegacyExactReferences_PM04_AT21)
{
    IRD_TEST_INFO("PM-04",{"AT-21"},std::nullopt);
    auto ws=makeSixAxisDraft();ws.rootObjectId=core::ObjectId::generate();
    ToolDefinition tool;tool.objectId=core::ObjectId::generate();tool.localName="draft-tool";
    TcpEntry tcp;tcp.key="tip";tcp.offset.P()[2]=0.234;tool.tcpList={tcp};
    ws.toolObjects={tool};ws.design.toolRefs={tool.objectId};ws.design.defaultTcp=TcpRef{tool.objectId,"tip"};
    SceneObject scene;scene.objectId=core::ObjectId::generate();scene.localName="draft-fixture";
    scene.worldPose.P()[0]=1.25;ws.sceneObjects={scene};ws.design.sceneRefs={scene.objectId};
    ws.changes.push_back({"tools","完整工具/场景未应用中文摘要"});const auto before=ws;
    const auto payload=encodeModelingDraft(ws);EXPECT_EQ(payload,encodeModelingDraft(ws));
    EXPECT_EQ(ws,before)<<"保存不得消费未应用草稿";
    QTemporaryDir dir;ASSERT_TRUE(dir.isValid());QFile file(dir.filePath(QStringLiteral("完整建模草稿.json")));
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));ASSERT_EQ(file.write(payload.data(),static_cast<qint64>(payload.size())),static_cast<qint64>(payload.size()));file.close();
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));const auto bytes=file.readAll();file.close();
    std::string error;const auto decoded=decodeModelingDraft(bytes.toStdString(),ModelingWorkingSet{},error);
    ASSERT_TRUE(decoded)<<error;EXPECT_EQ(*decoded,ws);
    const auto doc=QJsonDocument::fromJson(QByteArray::fromStdString(payload)).object();
    const auto legacy=doc.value("root").toString().toStdString();
    auto baseline=ws;baseline.changes.clear();baseline.design.displayName="旧基线";
    const auto old=decodeModelingDraft(legacy,baseline,error);ASSERT_TRUE(old)<<error;
    EXPECT_EQ(old->design,ws.design);EXPECT_EQ(old->toolObjects,ws.toolObjects);EXPECT_EQ(old->sceneObjects,ws.sceneObjects);
    EXPECT_FALSE(old->changes.empty())<<"旧格式内容不同仍须保留未应用事实";
    EXPECT_FALSE(decodeModelingDraft(legacy,ModelingWorkingSet{},error))<<"旧根缺部件不能猜造";
}

/** @brief B4b/AT-21：完整帧类型/版本/重复/缺引用/身份错位均原子拒绝，输入零变更。 */
TEST(ModelingDraftCodec, MalformedFramesRejectWithoutPartialValues_PM08_AT21)
{
    IRD_TEST_INFO("PM-08",{"AT-21"},std::nullopt);
    auto ws=makeSixAxisDraft();ToolEditRequest request;request.target=core::ObjectId::generate();
    request.localName="draft-tool";request.makeDefault=true;std::string error;
    const auto full=evaluateToolEdit(ws,request,{},error);ASSERT_TRUE(full)<<error;ws=*full;
    const auto payload=encodeModelingDraft(ws);const auto original=QJsonDocument::fromJson(QByteArray::fromStdString(payload)).object();
    for(int fault=0;fault<11;++fault){
        auto doc=original;
        if(fault==0){doc["version"]=2;}
        if(fault==1){doc["rootId"]="bad-id";}
        if(fault==2){auto values=doc["tools"].toArray();values.append(values.first());doc["tools"]=values;}
        if(fault==3){doc["tools"]=QJsonArray{};}
        if(fault==4){doc["root"]=doc["tools"].toArray().first();}
        if(fault==5){doc["poses"]="x0";}
        if(fault==6){doc.remove("changes");}
        if(fault==7){doc["rootId"]=QString::fromStdString(ws.toolObjects.front().objectId.toCanonical());}
        if(fault==8){doc["scenes"]=QJsonValue::Null;}
        if(fault==9){doc["changes"]=QJsonArray{QJsonObject{{"subject",true},{"summary","bad"}}};}
        auto input=QJsonDocument(doc).toJson(QJsonDocument::Compact).toStdString();
        if(fault==10){input+="trailing";}
        EXPECT_FALSE(decodeModelingDraft(input,ws,error))<<"fault="<<fault;EXPECT_FALSE(error.empty());EXPECT_EQ(ws,*full);
    }
    for(const std::string bad:{std::string{},std::string{"{"},std::string{"xx"},std::string{"0"}}){
        EXPECT_FALSE(decodeModelingDraft(bad,ws,error));EXPECT_FALSE(error.empty());
    }
}

/// WP-13-T25/F01（MDL-01）：关节属性行必须投影逐轴最大速度/最大加速度
/// （rad·s⁻¹/rad·s⁻² 随类型；可编辑——joint 一等权威字段；此前划界
/// "属传动对象不投影"随 schema 落位废止——§4.3-A 表行同步）。
TEST(PluginPanel, JointRowsIncludeVelocityAccelerationEditable_MDL01_WP13T25)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-01"}, std::vector<std::string>{});

    ModelingWorkingSet ws = makeSixAxisDraft();
    const auto target = resolveSelection(ws, ws.design.joints[0].objectId);
    ASSERT_TRUE(target.has_value());
    const auto rows = propertyFieldsFor(ws, *target);

    auto rowOf = [&rows](const char* key) {
        for (const auto& r : rows) {
            if (r.fieldKey == key) { return r; }
        }
        return PropertyFieldRow{};
    };
    const PropertyFieldRow velocity = rowOf("max-velocity");
    EXPECT_EQ(velocity.fieldKey, "max-velocity") << "关节行缺最大速度投影（F01）";
    EXPECT_EQ(velocity.enablement, FieldEnablement::Editable)
        << "速度为关节一等权威字段——可编辑行";
    const PropertyFieldRow acceleration = rowOf("max-acceleration");
    EXPECT_EQ(acceleration.fieldKey, "max-acceleration") << "关节行缺最大加速度投影（F01）";
    EXPECT_EQ(acceleration.enablement, FieldEnablement::Editable)
        << "加速度为关节一等权威字段——可编辑行";
}

/**
 * REUSE-T33 oracle（rpyview::rotationToRpy 换上游 RPY 反解——呈现层唯一
 * 实现的面）：①通支往返网格（含 yaw=0/roll=0/一般姿态）——正解合成
 * R＝Rz(y)Ry(p)Rx(r)（测试侧独立三角合成），反解输出与原 (r,p,y) 1e-12
 * 内一致，且与上游 rw::math::RPY(R) 换序逐位相等（直接消费的位级证据）；
 * ②精确 ±π/2 万向锁（字面矩阵）——roll=0、pitch=±π/2、yaw 字面值
 * （产品确定性约定保留——上游奇异支清零 z 角经取负映回），反解值重配
 * Rz·Ry·Rx 重建同旋转（1e-12）；③近奇异带（π/2−1e-6，上游 1e-5 带内）
 * ——规范化 roll=0 形式且往返仍正确（probe-r6 结论的产品面）。
 */
TEST(MdlPanelRpy, RotationToRpy_UpstreamReversedOrderAndGimbalRollZero_T33)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-02"},
                  std::vector<std::string>{});

    // 测试侧独立正解合成（不消费被测实现）：R＝Rz(yaw)·Ry(pitch)·Rx(roll)。
    auto composeRzyx = [](double roll, double pitch, double yaw) {
        const double cr = std::cos(roll), sr = std::sin(roll);
        const double cp = std::cos(pitch), sp = std::sin(pitch);
        const double cy = std::cos(yaw), sy = std::sin(yaw);
        return rw::math::Rotation3D<double>(
            cy * cp, cy * sp * sr - sy * cr, cy * sp * cr + sy * sr,
            sy * cp, sy * sp * sr + cy * cr, sy * sp * cr - cy * sr,
            -sp, cp * sr, cp * cr);
    };
    auto expectNearRot = [](const rw::math::Rotation3D<double>& a,
                            const rw::math::Rotation3D<double>& b) {
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c) {
                EXPECT_NEAR(a(r, c), b(r, c), 1e-12) << "(" << r << "," << c << ")";
            }
        }
    };

    // ①通支网格：一般 / yaw=0 / roll=0 / 负 pitch。
    for (const std::array<double, 3>& rpy
         : std::vector<std::array<double, 3>>{
             {0.3, -0.4, 0.7}, {0.0, 0.25, 0.0}, {1.1, 0.0, -0.6},
             {-2.0, 1.2, 0.45}}) {
        const rw::math::Rotation3D<double> R =
            composeRzyx(rpy[0], rpy[1], rpy[2]);
        const std::array<double, 3> back = rpyview::rotationToRpy(R);
        EXPECT_NEAR(back[0], rpy[0], 1e-12) << "roll 往返";
        EXPECT_NEAR(back[1], rpy[1], 1e-12) << "pitch 往返";
        EXPECT_NEAR(back[2], rpy[2], 1e-12) << "yaw 往返";
        // 直接消费上游的位级证据：反解＝上游 RPY(R) 换序（通支不触奇异支）。
        const rw::math::RPY<double> upstream(R);
        EXPECT_EQ(back[0], upstream[2]) << "roll＝上游 x 角（位级）";
        EXPECT_EQ(back[1], upstream[1]) << "pitch＝上游 y 角（位级）";
        EXPECT_EQ(back[2], upstream[0]) << "yaw＝上游 z 角（位级）";
    }

    // ②精确 +π/2 万向锁（字面矩阵——R20＝−1 精确）：
    // R＝Rz(y)Ry(π/2)Rx(r) 只依赖 (r−y)：取 y=0.7, r=0.3 → R01=sin(r−y)、
    // R11=cos(r−y)；期望 {0, π/2, −atan2(R01,R11)}＝{0, π/2, y−r}。
    {
        const double r = 0.3, y = 0.7;
        const double d = r - y;  // r−y＝−0.4
        const rw::math::Rotation3D<double> R(
            0.0, std::sin(d), std::cos(d),
            0.0, std::cos(d), -std::sin(d),
            -1.0, 0.0, 0.0);
        const std::array<double, 3> back = rpyview::rotationToRpy(R);
        EXPECT_EQ(back[0], 0.0) << "+π/2 万向锁：roll=0（产品确定性约定）";
        EXPECT_EQ(back[1], kPi / 2) << "pitch＝+π/2 精确";
        EXPECT_NEAR(back[2], y - r, 1e-15) << "yaw＝y−r（字面合成角）";
        // 重建同旋转：Rz(0)·Ry(π/2)·Rx(反解 roll) 与输入一致。
        expectNearRot(composeRzyx(back[0], back[1], back[2]), R);
    }
    // −π/2：R20＝+1 精确（Rz(y)Ry(−π/2)Rx(r) 同族字面矩阵）。
    {
        const double r = 0.3, y = 0.7;
        // Rz(y)Ry(−π/2)Rx(r) 字面展开（测试侧独立三角恒等式）。
        const double ssum = r + y;
        const rw::math::Rotation3D<double> R(
            0.0, -std::sin(ssum), -std::cos(ssum),
            0.0, std::cos(ssum), -std::sin(ssum),
            1.0, 0.0, 0.0);
        const std::array<double, 3> back = rpyview::rotationToRpy(R);
        EXPECT_EQ(back[0], 0.0) << "−π/2 万向锁：roll=0";
        EXPECT_EQ(back[1], -kPi / 2) << "pitch＝−π/2 精确";
        expectNearRot(composeRzyx(back[0], back[1], back[2]), R);
    }

    // ③近奇异带（pitch=π/2−1e-6，上游 1e-5 带内）：规范化 roll=0 形式，
    // 且反解值重配后重建同旋转（呈现等价分解——probe-r6 往返结论）。
    {
        const double pitch = kPi / 2 - 1e-6;
        const rw::math::Rotation3D<double> R = composeRzyx(0.3, pitch, 0.7);
        const std::array<double, 3> back = rpyview::rotationToRpy(R);
        EXPECT_EQ(back[0], 0.0) << "1e-5 带内规范化 roll=0（确定性呈现约定）";
        // 带内规范化代价如实登记：pitch 被规范为恰 π/2，重建与原旋转的
        // 元素差 O(带宽)＝≤2e-6（probe-r6 的旋转往返在带内即此量级——
        // 呈现层等价分解，非 1e-12 位级等价）。
        {
            const rw::math::Rotation3D<double> rebuilt =
                composeRzyx(back[0], back[1], back[2]);
            for (int r = 0; r < 3; ++r) {
                for (int c = 0; c < 3; ++c) {
                    EXPECT_NEAR(rebuilt(r, c), R(r, c), 2e-6)
                        << "带内重建 O(带宽)（" << r << "," << c << ")";
                }
            }
        }
    }
}
