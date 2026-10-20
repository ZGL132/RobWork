/**
 * @file   ImportTest.cpp
 * @brief  URDF 导入字段映射与链型判定用例组（MdlImport）——契约
 *         tasks/foundation/WP-13-T05.json acceptance 逐条具名自证：
 *           ACC1 接口落位/三分边界（mapWorkCellXml R1 边界、可重入无状态、
 *                mapXacroExpanded 来源留痕——§9.4.3/SA-14）
 *           ACC2 §6.3 字段映射表逐行＋四清单可观察（AT-15）＋默认补全不
 *                静默（NFR-COR-03）＋package:// 定位引导＋缺失资源
 *                IO-RES-MISSING/Recorded 草稿可携带（§6.7/V-08）
 *           ACC3 MDL-11 轴语义三分支（非零非 Z 轴/缺 axis +X 待确认/
 *                零与非有限轴仅报告不提交）
 *           ACC4 链型判定两维度（§6.4/AT-17）：分支报告＋显式选链＋辅助
 *                分支候选不构成拒绝／mimic·planar·floating 阻断不转
 *                FixedFrame／4·5 轴（与任何非六/七轴链）范围外结论＋诊断
 *                ＋类型保留（V12-01）／continuous 工程工作范围待确认
 *                〔WP-13-T19 增量：六/七轴含 prismatic 混合链随 MDL-12-S1
 *                放行——FullTemplateRange＋无 TEMPLATE-RANGE 诊断；4/5 轴
 *                反例仍阻断〕
 *           ACC5 确定性（同字节同选项同输出；诊断按源文件行序稳定排序
 *                ——NFR-COR-02）＋非法值原串保留（SourcedValue::invalid）
 *
 * 设计依据：units/modeling.md §6.1～§6.4/§6.7/§9.4.3/§9.5、units/io.md
 * §9.6/§10.5；需求 MDL-03/11/12/18/22、NFR-COR-01/02/03、CON-03。
 *
 * fixture 口径：全部 inline 字符串 URDF（V-06 黄金数据集驱动的全链用例随
 * WP-13-T16 收口——契约 note ④）；依赖树/快照按 io 值类型手工构造（模拟
 * io IResourceReader 产物——映射输入的装配面归向导域侧，本单元不读文件）。
 */

#include <sdurws/ird/modeling/Import.hpp>

#include <sdurws/ird/core/DiagData.hpp>             // DiagnosticRecord 字段断言
#include <sdurws/ird/io/IoDiagnostics.hpp>          // io::errorCodeToken——IO-RES-MISSING 码面
#include <sdurws/ird/modeling/Codec.hpp>            // draft 字节级确定性比对
#include <sdurws/ird/modeling/DiagCodes.hpp>        // MDL-IMPORT-* 码值常量
#include <sdurws/ird/modeling/RobotDesign.hpp>      // checkInvariants——值模型不变量核查（WP-13-T17）
#include <sdurws/ird/modeling/WorkCellReverseImport.hpp>  // extractWorkCellDevice——接口/引擎同源钉扎（WP-13-T17）
#include <sdurws/ird/runtime/Errors.hpp>            // runtime::Expected（encode 载体）
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯登记

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>       // std::cos/sin/M_PI——RPY deg→rad 断言（WP-13-T17）
#include <cstdint>
#include <string>
#include <vector>

using sdurws::ird::core::FieldState;
using sdurws::ird::core::ObjectId;
using sdurws::ird::core::ProvenanceKind;
namespace rwmath = rw::math;
using sdurws::ird::io::ResourceDependencyTree;
using sdurws::ird::io::ResourceEdge;
using sdurws::ird::io::ResourceEdgeKind;
using sdurws::ird::io::ResourceKind;
using sdurws::ird::io::ResourceNode;
using sdurws::ird::io::ResourceSnapshot;
using sdurws::ird::modeling::ImportErrorCode;
using sdurws::ird::modeling::ImportOptions;
using sdurws::ird::modeling::ImportOutcome;
using sdurws::ird::modeling::ImportReport;
using sdurws::ird::modeling::ImportSourceSpan;
using sdurws::ird::modeling::ImportUnsupportedItem;
using sdurws::ird::modeling::ModelImportMapper;
using sdurws::ird::modeling::ValidatedSource;
using sdurws::ird::modeling::XacroProvenance;
using sdurws::ird::modeling::kMdlImportBranchSelection;
using sdurws::ird::modeling::kMdlImportPendingConfirm;
using sdurws::ird::modeling::kMdlImportTemplateRange;
using sdurws::ird::modeling::kMdlImportUnsupportedJoint;
using sdurws::ird::modeling::kMdlImportZeroAxis;

namespace {

// =====================================================================
// fixture 设施（io 产物模拟——IResourceReader 四方法值的装配面）
// =====================================================================

/// 构造确定性摘要字节（模式填充——仅占位：映射器不校验摘要与内容一致，
/// 摘要真实性由 io 保证；测试关心的是摘要的传递与呈现）。
sdurws::ird::core::Digest256 makeDigest(std::uint8_t seed)
{
    sdurws::ird::core::Digest256 d{};
    for (std::size_t i = 0; i < d.size(); ++i) {
        d[i] = static_cast<std::uint8_t>(seed + i);
    }
    return d;
}

/// 构造一个"存在"的依赖树节点（digest=seed 派生）。
ResourceNode existingNode(const std::string& relPath, std::uint8_t seed)
{
    ResourceNode node;
    node.relPath = relPath;
    node.exists = true;
    ResourceSnapshot snapshot;
    snapshot.finalPath = std::string("Z:/fake/") + relPath;  // 追溯提示——映射器不以路径作身份
    snapshot.sizeBytes = 128;
    snapshot.mtimeUtc = 0;
    snapshot.contentDigest = makeDigest(seed);
    node.snapshot = snapshot;
    return node;
}

/// 构造一个"缺失"的依赖树节点（io §6.5 缺失叶——exists=false）。
ResourceNode missingNode(const std::string& relPath)
{
    ResourceNode node;
    node.relPath = relPath;
    node.exists = false;
    return node;
}

/// 组装 ValidatedSource：入口文档 model.urdf＋给定节点/边（io 产物形态）。
ValidatedSource makeSource(const std::string& urdf,
                           std::vector<ResourceNode> nodes = {},
                           std::vector<ResourceEdge> edges = {})
{
    ValidatedSource source;
    source.bytes.assign(urdf.begin(), urdf.end());
    source.entrySnapshot.finalPath = "Z:/fake/model.urdf";
    source.entrySnapshot.sizeBytes = source.bytes.size();
    source.entrySnapshot.contentDigest = makeDigest(0);
    ResourceDependencyTree tree;
    tree.rootRel = "model.urdf";
    tree.nodes.push_back(existingNode("model.urdf", 0));
    for (auto& node : nodes) { tree.nodes.push_back(std::move(node)); }
    tree.edges = std::move(edges);
    // io 稳定序（relPath 字典序）——映射器不依赖节点序，但保持产物形态真实。
    std::sort(tree.nodes.begin(), tree.nodes.end(),
              [](const ResourceNode& a, const ResourceNode& b) {
                  return a.relPath < b.relPath;
              });
    source.dependencyTree = std::move(tree);
    return source;
}

/// mesh 引用边（from 入口文档 → to 相对键）。
ResourceEdge meshEdge(const std::string& toRel)
{
    ResourceEdge edge;
    edge.fromRel = "model.urdf";
    edge.toRel = toRel;
    edge.kind = ResourceEdgeKind::Mesh;
    return edge;
}

// =====================================================================
// 完整样例（ACC2 主体 fixture——§6.3 映射表逐行覆盖面）
// =====================================================================

/**
 * 完整样例：两关节链（revolute＋continuous），覆盖：名称净化（"demo arm"
 * 含空格）/robot 级材质/gazebo 扩展/transmission/自碰撞对/缺失网格
 * （missing.dae）/存在网格（l1.stl）/非单位缩放/package:// URI/图元材质
 * 密度缺失/非 Z 轴（0 2 0）/effort→Peak 候选/velocity 无落点/continuous
 * NotApplicable/l2 缺 inertial。
 */
const char* kFullUrdf = R"(<?xml version="1.0"?>
<robot name="demo arm">
  <material name="gray"/>
  <gazebo><plugin name="fake"/></gazebo>
  <transmission><joint name="j1"/><mechanicalReduction>1</mechanicalReduction></transmission>
  <disable_collisions link1="base" link2="l1"/>
  <link name="base">
    <visual>
      <origin xyz="0 0 0" rpy="0 0 0"/>
      <geometry><mesh filename="meshes/missing.dae"/></geometry>
    </visual>
  </link>
  <link name="l1">
    <inertial>
      <origin xyz="0 0 0.1" rpy="0 0 0"/>
      <mass value="2.0"/>
      <inertia ixx="0.01" iyy="0.01" izz="0.01" ixy="0" ixz="0" iyz="0"/>
    </inertial>
    <visual>
      <geometry><mesh filename="meshes/l1.stl"/></geometry>
      <material name="steel_gray"/>
    </visual>
    <collision>
      <geometry><mesh filename="meshes/l1.stl" scale="2 1 1"/></geometry>
    </collision>
  </link>
  <link name="l2">
    <visual>
      <geometry><mesh filename="package://pkg/meshes/l2.stl"/></geometry>
      <material name="gray2"/>
    </visual>
  </link>
  <joint name="j1" type="revolute">
    <parent link="base"/>
    <child link="l1"/>
    <origin xyz="0 0 0.2" rpy="0 0 0"/>
    <axis xyz="0 2 0"/>
    <limit lower="-3.14" upper="3.14" effort="50" velocity="1.0"/>
  </joint>
  <joint name="j2" type="continuous">
    <parent link="l1"/>
    <child link="l2"/>
    <axis xyz="0 0 1"/>
  </joint>
</robot>
)";

/// 完整样例的依赖树：l1.stl 存在、missing.dae 缺失。
ValidatedSource makeFullSource()
{
    return makeSource(kFullUrdf,
                      {existingNode("meshes/l1.stl", 7), missingNode("meshes/missing.dae")},
                      {meshEdge("meshes/l1.stl"), meshEdge("meshes/missing.dae"),
                       meshEdge("package://pkg/meshes/l2.stl")});
}

// =====================================================================
// WorkCell 反向导入 fixture（WP-13-T17——MDL-18/AT-33 提取语义载体；
// 格式语义对照 RobWork 框架 XMLRWLoader 文法：Pos=m、RPY=deg、PosLimit
// Revolute 单位 deg/Prismatic 单位 m、Q=rad、CollisionSetup/场景/相机）
// =====================================================================

/**
 * 两关节 WorkCell 样例（提取语义逐项覆盖面）：链＝Fixed 基帧＋Revolute
 * （Pos/RPY/PosLimit(deg)/VelLimit/Drawable→Polytope 存在网格）＋
 * Prismatic（PosLimit(m)/CollisionModel→Polytope 缺失网格）；Q 首配置
 * （Home，对位两可动关节）＋备选构型（Alt）；设备级 CollisionSetup
 * 外部引用；WorkCell 级场景 Frame/Drawable/Camera。
 */
const char* kWorkCellTwoJoint = R"(<?xml version="1.0" encoding="UTF-8"?>
<WorkCell name="demo cell">
  <SerialDevice name="arm">
    <Frame name="base" type="Fixed">
      <Pos>0 0 0</Pos>
      <RPY>0 0 0</RPY>
    </Frame>
    <Joint name="j1" type="Revolute">
      <Pos>0 0 0.2</Pos>
      <RPY>0 0 90</RPY>
      <PosLimit min="-170" max="170"/>
      <VelLimit max="1.5"/>
      <Drawable name="l1mesh"><Polytope file="meshes/l1.stl"/></Drawable>
    </Joint>
    <Joint name="j2" type="Prismatic">
      <Pos>0 0 0.4</Pos>
      <PosLimit min="0" max="0.5"/>
      <CollisionModel name="l2mesh"><Polytope file="meshes/missing.stl"/></CollisionModel>
    </Joint>
    <Q name="Home"> 0.5 -0.1 </Q>
    <Q name="Alt"> 1 2 </Q>
    <CollisionSetup file="collision/setup.xml"/>
  </SerialDevice>
  <Frame name="table" type="Fixed"/>
  <Drawable name="scene-prop"><Polytope file="scene/table.stl"/></Drawable>
  <Camera name="cam"/>
</WorkCell>
)";

/// 两关节 WorkCell 的依赖树：l1.stl 存在、missing.stl 缺失。
ValidatedSource makeWorkCellSource(const std::string& xml = kWorkCellTwoJoint)
{
    return makeSource(xml,
                      {existingNode("meshes/l1.stl", 11), missingNode("meshes/missing.stl")},
                      {});
}

/// 两设备 WorkCell（目标设备选择——§6.4 同机制载体）。
const char* kWorkCellTwoDevices = R"(<?xml version="1.0"?>
<WorkCell name="twin cell">
  <SerialDevice name="arm-a">
    <Joint name="a1" type="Revolute">
      <PosLimit min="-90" max="90"/>
    </Joint>
  </SerialDevice>
  <SerialDevice name="arm-b">
    <Joint name="b1" type="Revolute">
      <PosLimit min="0" max="180"/>
    </Joint>
  </SerialDevice>
</WorkCell>
)";

/// 不可表达面 WorkCell：Spherical 多自由度关节＋Depend 从动关节＋
/// MovableFrame 自由帧——逐项清单载体（无草稿）。
const char* kWorkCellUnsupported = R"(<?xml version="1.0"?>
<WorkCell name="odd cell">
  <SerialDevice name="weird">
    <Joint name="s1" type="Spherical"/>
    <Joint name="d1" type="Revolute">
      <Depend on="s1" gain="1" offset="0"/>
    </Joint>
    <Frame name="f1" type="Movable"/>
  </SerialDevice>
</WorkCell>
)";

/// 在诊断清单中查找指定码的首条记录（无则返回 false）。
bool hasDiag(const std::vector<sdurws::ird::core::DiagnosticRecord>& diags,
             std::string_view code)
{
    for (const auto& d : diags) {
        if (d.code == code) { return true; }
    }
    return false;
}

}  // namespace

// =====================================================================
// ACC1：接口落位与三分边界（§9.4.3/SA-14/MDL-18 R1）
// =====================================================================

/**
 * mapWorkCellXml 通道实现（acceptance 1——WP-13-T17 阶段 D：R1 边界响应
 * 退役，WP13T05 版 R1 用例〔NotImplemented＋workcell-channel-r2〕随通道
 * 实现更新为"真实提取"验证——MDL-18 R2 有损提取＋提取报告）：无 R1 残留
 * 面＋报告完整产出；草稿/报告细节见下方 WP-13-T17 用例组（MdlWorkCell
 * Import——ImportTest 内联会随本组迁移，本用例只钉 R1 退役事实）。
 */
TEST(MdlImport, MapWorkCellXml_ChannelImplementedR1Retired_WP13T17_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-18"},
                  std::vector<std::string>{"AT-33"});

    const ModelImportMapper mapper;
    std::vector<sdurws::ird::core::DiagnosticRecord> diags;
    const ImportOutcome outcome = mapper.mapWorkCellXml(makeSource(kWorkCellTwoJoint),
                                                        ImportOptions{}, diags);
    // 通道实现：合法 WorkCell→草稿产出（R1 的 NotImplemented 不再出现）。
    ASSERT_TRUE(outcome.draft.has_value()) << "R2 通道应产出草稿";
    EXPECT_FALSE(outcome.error.has_value()) << "合法文档无阻断错误";
    for (const ImportUnsupportedItem& item : outcome.report.unsupported) {
        EXPECT_NE(item.kind, "workcell-channel-r2")
            << "R1 引导词表已退役——不再产出";
    }
}

/**
 * 可重入无状态（acceptance 1/5——卡 §3.4 总约定 1）：同实例重复调用与
 * 两实例调用产出逐字段相等结果（无隐藏会话态）。
 */
TEST(MdlImport, Mapper_ReentrantStateless_WP13T05_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-01"},
                  std::vector<std::string>{});

    const ModelImportMapper mapperA;
    const ModelImportMapper mapperB;
    std::vector<sdurws::ird::core::DiagnosticRecord> diagsFirst;
    std::vector<sdurws::ird::core::DiagnosticRecord> diagsSecond;
    std::vector<sdurws::ird::core::DiagnosticRecord> diagsThird;
    const ImportOutcome first = mapperA.mapUrdf(makeFullSource(), ImportOptions{}, diagsFirst);
    const ImportOutcome second =
        mapperA.mapUrdf(makeFullSource(), ImportOptions{}, diagsSecond);
    const ImportOutcome third = mapperB.mapUrdf(makeFullSource(), ImportOptions{}, diagsThird);
    EXPECT_TRUE(first == second) << "同实例重复调用应逐字段一致";
    EXPECT_TRUE(first == third) << "跨实例调用应逐字段一致（无共享可变状态）";
    ASSERT_EQ(diagsFirst.size(), diagsSecond.size());
    for (std::size_t i = 0; i < diagsFirst.size(); ++i) {
        EXPECT_TRUE(diagsFirst[i] == diagsSecond[i]) << "诊断第 " << i << " 条不一致";
    }
    ASSERT_EQ(diagsFirst.size(), diagsThird.size());
    for (std::size_t i = 0; i < diagsFirst.size(); ++i) {
        EXPECT_TRUE(diagsFirst[i] == diagsThird[i]) << "跨实例诊断第 " << i << " 条不一致";
    }
}

/**
 * mapXacroExpanded 来源留痕（acceptance 1——mapXacroExpanded 同边界＋
 * 来源记录随草稿/报告；§6.5）：原始 .xacro 以 Recorded 进 resourceManifest
 * ＋资源状态表，substitutions 逐条入映射清单留痕；来源摘要缺失＝调用方
 * 契约违约→SourceInconsistent 无草稿。
 */
TEST(MdlImport, MapXacroExpanded_ProvenanceRecorded_WP13T05_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-19"},
                  std::vector<std::string>{});

    const ModelImportMapper mapper;
    // 简单单链（无资源面干扰）：base→j→l。
    const char* urdf = R"(<?xml version="1.0"?>
<robot name="x">
  <link name="base"/>
  <link name="l"/>
  <joint name="j" type="revolute">
    <parent link="base"/>
    <child link="l"/>
    <axis xyz="0 0 1"/>
    <limit lower="0" upper="1"/>
  </joint>
</robot>
)";
    XacroProvenance provenance;
    provenance.sourceDigest = makeDigest(9);
    provenance.sourceAbsPath = "Z:/fake/model.xacro";
    provenance.substitutions = {{"arm_length", "0.8"}, {"joints", "6"}};
    std::vector<sdurws::ird::core::DiagnosticRecord> diags;
    const ImportOutcome outcome =
        mapper.mapXacroExpanded(makeSource(urdf), provenance, ImportOptions{}, diags);
    ASSERT_TRUE(outcome.draft.has_value());
    // 留痕 1：草稿 resourceManifest 含 xacro-source（Recorded＋externalRecord）。
    bool found = false;
    for (const auto& ref : outcome.draft->resourceManifest) {
        if (ref.resourceId == "xacro-source") {
            found = true;
            EXPECT_EQ(ref.state, sdurws::ird::modeling::ResourceState::Recorded);
            ASSERT_TRUE(ref.externalRecord.has_value());
            EXPECT_EQ(ref.externalRecord->absPath, "Z:/fake/model.xacro");
            EXPECT_TRUE(ref.contentDigest == provenance.sourceDigest);
        }
    }
    EXPECT_TRUE(found) << "原始 .xacro 应以 Recorded 资源留痕（§6.5）";
    // 留痕 2：资源状态表行＋逐条替换入映射清单。
    bool rowFound = false;
    for (const auto& row : outcome.report.resources) {
        if (row.resourceId == "xacro-source") {
            rowFound = true;
            EXPECT_EQ(row.state, "recorded");
        }
    }
    EXPECT_TRUE(rowFound);
    int substitutionNotes = 0;
    for (const auto& mapped : outcome.report.mapped) {
        if (mapped.sourcePath.rfind("xacro-substitution/", 0) == 0) {
            ++substitutionNotes;
        }
    }
    EXPECT_EQ(substitutionNotes, 2) << "substitutions 逐条留痕（§6.5）";

    // 契约违约面：零摘要→SourceInconsistent 无草稿。
    XacroProvenance broken;
    std::vector<sdurws::ird::core::DiagnosticRecord> diags2;
    const ImportOutcome failed =
        mapper.mapXacroExpanded(makeSource(urdf), broken, ImportOptions{}, diags2);
    ASSERT_TRUE(failed.error.has_value());
    EXPECT_EQ(failed.error->code, ImportErrorCode::SourceInconsistent);
    EXPECT_FALSE(failed.draft.has_value());
}

// =====================================================================
// ACC2：§6.3 字段映射表逐行＋四清单可观察（MDL-03/AT-15/NFR-COR-03）
// =====================================================================

/**
 * 完整样例字段映射（acceptance 2——"字段映射/默认补全/忽略/不支持四
 * 清单逐项可观察"）：结构面（链长/类型）＋逐字段值＋来源标记＋清单条目。
 */
TEST(MdlImport, FullUrdf_FieldMappingRowsObservable_WP13T05_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-03"},
                  std::vector<std::string>{"AT-15"});

    const ModelImportMapper mapper;
    std::vector<sdurws::ird::core::DiagnosticRecord> diags;
    const ImportOutcome outcome = mapper.mapUrdf(makeFullSource(), ImportOptions{}, diags);
    ASSERT_TRUE(outcome.draft.has_value()) << "完整样例应产出草稿";
    const auto& draft = *outcome.draft;
    const auto& report = outcome.report;

    // —— <robot name>：displayName 保留原文；根候选净化入默认补全清单。
    EXPECT_EQ(draft.displayName, "demo arm");
    bool rootSanitized = false;
    for (const auto& item : report.defaults) {
        if (item.field == "root-local-name-candidate") {
            rootSanitized = true;
            EXPECT_EQ(item.appliedValue, "demo_arm");
        }
    }
    EXPECT_TRUE(rootSanitized) << "名称净化必须入默认补全清单（NFR-COR-03 不静默）";

    // —— 链结构（I-MDL-1）：2 关节 3 连杆；类型四类识别。
    ASSERT_EQ(draft.joints.size(), 2U);
    ASSERT_EQ(draft.links.size(), 3U);
    EXPECT_EQ(draft.joints[0].type, sdurws::ird::modeling::JointType::Revolute);
    EXPECT_EQ(draft.joints[1].type, sdurws::ird::modeling::JointType::Continuous);

    // —— j1 轴（0,2,0）→归一化 (0,1,0)（非 Z 轴可接受——MDL-11）。
    ASSERT_EQ(draft.joints[0].axis.state(), FieldState::Provided);
    EXPECT_DOUBLE_EQ(draft.joints[0].axis.value()[0], 0.0);
    EXPECT_DOUBLE_EQ(draft.joints[0].axis.value()[1], 1.0);
    EXPECT_DOUBLE_EQ(draft.joints[0].axis.value()[2], 0.0);
    EXPECT_EQ(draft.joints[0].axis.provenance().kind, ProvenanceKind::ImportMapped);

    // —— j1 origin（xyz 0 0 0.2——T_parent_joint）。
    ASSERT_EQ(draft.joints[0].origin.state(), FieldState::Provided);
    EXPECT_DOUBLE_EQ(draft.joints[0].origin.value().d()[2], 0.2);

    // —— j1 bounds（-3.14, 3.14 rad）。
    ASSERT_EQ(draft.joints[0].bounds.state(), FieldState::Provided);
    EXPECT_DOUBLE_EQ(draft.joints[0].bounds.value().first, -3.14);
    EXPECT_DOUBLE_EQ(draft.joints[0].bounds.value().second, 3.14);

    // —— j2 continuous：bounds NotApplicable（类型保留——I-MDL-4）。
    EXPECT_EQ(draft.joints[1].bounds.state(), FieldState::NotApplicable);

    // —— l1 物性（mass/com/inertia Provided＋ImportMapped）。
    ASSERT_EQ(draft.links[1].body.mass.state(), FieldState::Provided);
    EXPECT_DOUBLE_EQ(draft.links[1].body.mass.value(), 2.0);
    ASSERT_EQ(draft.links[1].body.centerOfMass.state(), FieldState::Provided);
    EXPECT_DOUBLE_EQ(draft.links[1].body.centerOfMass.value()[2], 0.1);
    ASSERT_EQ(draft.links[1].body.inertia.state(), FieldState::Provided);
    EXPECT_DOUBLE_EQ(draft.links[1].body.inertia.value().ixx, 0.01);

    // —— l1 材质：materialId＋密度 NotProvided（估算不可用——报告登记）。
    ASSERT_TRUE(draft.links[1].body.material.has_value());
    EXPECT_EQ(draft.links[1].body.material->materialId, "steel_gray");
    EXPECT_EQ(draft.links[1].body.material->density.state(), FieldState::NotProvided);

    // —— basePlacement：ground（MDL-22）＋默认补全清单含 ImportMapped 来源。
    EXPECT_EQ(draft.basePlacement.preset,
              sdurws::ird::runtime::InstallationPresetToken::Ground);
    bool basePlacementDefaulted = false;
    for (const auto& item : report.defaults) {
        if (item.field == "basePlacement") {
            basePlacementDefaulted = true;
            EXPECT_NE(item.appliedValue.find("ground"), std::string::npos);
            EXPECT_NE(item.reason.find("import-mapped"), std::string::npos);
        }
    }
    EXPECT_TRUE(basePlacementDefaulted) << "basePlacement 默认地面必须入清单";

    // —— 资源清单：l1.stl（recorded）＋missing.dae（Recorded 携带）。
    ASSERT_EQ(draft.resourceManifest.size(), 2U);
    bool l1Found = false;
    bool missingFound = false;
    for (const auto& ref : draft.resourceManifest) {
        if (ref.resourceId == "meshes/l1.stl") {
            l1Found = true;
            EXPECT_EQ(ref.state, sdurws::ird::modeling::ResourceState::Recorded);
            ASSERT_TRUE(ref.externalRecord.has_value());
            EXPECT_TRUE(ref.contentDigest == makeDigest(7));
        }
        if (ref.resourceId == "meshes/missing.dae") {
            missingFound = true;
            EXPECT_EQ(ref.state, sdurws::ird::modeling::ResourceState::Recorded);
            ASSERT_TRUE(ref.externalRecord.has_value()) << "Recorded 态必带外部记录（I-MDL-10）";
        }
    }
    EXPECT_TRUE(l1Found);
    EXPECT_TRUE(missingFound) << "缺失文件以 Recorded 草稿可携带（§6.7/V-08）";

    // —— 四清单可观察性（每类至少一条＋关键条目命中）。
    EXPECT_FALSE(report.mapped.empty());
    EXPECT_FALSE(report.defaults.empty());
    bool gazeboIgnored = false;
    bool materialIgnored = false;
    for (const auto& item : report.ignored) {
        if (item.element == "gazebo") { gazeboIgnored = true; }
        if (item.element.rfind("material", 0) == 0) { materialIgnored = true; }
    }
    EXPECT_TRUE(gazeboIgnored) << "<gazebo> 外来扩展入忽略清单";
    EXPECT_TRUE(materialIgnored) << "robot 级材质定义入忽略清单";
    bool transmissionUnsupported = false;
    bool scaleUnsupported = false;
    bool rosUriUnsupported = false;
    for (const auto& item : report.unsupported) {
        if (item.kind == "transmission") { transmissionUnsupported = true; }
        if (item.kind == "mesh-scale") {
            scaleUnsupported = true;
            EXPECT_NE(item.subject.find("scale=2 1 1"), std::string::npos);
        }
        if (item.kind == "ros-uri") {
            rosUriUnsupported = true;
            EXPECT_NE(item.guidance.find("相对路径"), std::string::npos)
                << "package:// 必须引导改相对路径";
            EXPECT_GT(item.span.line, 0U) << "定位面：span 行号可观察";
        }
    }
    EXPECT_TRUE(transmissionUnsupported);
    // F01（MDL-01）：<limit velocity> 已真实映射 maxVelocity——映射清单可观察
    // （旧"joint-velocity-limit 无落点"不支持面随 schema 落位消亡）。
    bool velocityMapped = false;
    for (const auto& m : report.mapped) {
        if (m.targetField.find(".maxVelocity") != std::string::npos) {
            velocityMapped = true;
        }
    }
    EXPECT_TRUE(velocityMapped) << "velocity 应映射 joints[i].maxVelocity（F01）";
    EXPECT_TRUE(scaleUnsupported);
    EXPECT_TRUE(rosUriUnsupported);

    // —— effort→torqueLimit Peak 候选＋自碰撞候选（P-MDL-3 报告面）。
    ASSERT_EQ(report.drivetrainCandidates.size(), 1U);
    EXPECT_EQ(report.drivetrainCandidates[0].jointName, "j1");
    EXPECT_NE(report.drivetrainCandidates[0].valueText.find("50"), std::string::npos);
    ASSERT_EQ(report.selfCollisionCandidates.size(), 1U);
    EXPECT_EQ(report.selfCollisionCandidates[0].link1, "base");
    EXPECT_EQ(report.selfCollisionCandidates[0].link2, "l1");
    // P-MDL-3：不写策略对象、不入编码语义——草稿连杆零自碰撞提示。
    for (const auto& link : draft.links) {
        EXPECT_FALSE(link.selfCollisionHints.has_value())
            << "自碰撞配置只经报告候选承载（P-MDL-3）";
    }

    // —— 缺失资源事实面：IO-RES-MISSING 诊断＋资源状态表 missing 行。
    EXPECT_TRUE(hasDiag(diags, sdurws::ird::io::errorCodeToken(
                                     sdurws::ird::io::IoErrorCode::ResMissing)));
    bool missingRow = false;
    for (const auto& row : report.resources) {
        if (row.resourceId == "meshes/missing.dae") {
            missingRow = true;
            EXPECT_EQ(row.state, "missing");
            EXPECT_TRUE(row.digestHex.empty());
        }
    }
    EXPECT_TRUE(missingRow);

    // —— 缺失/待确认之外的软面不阻断：submittable 保持 true（V-08 应用可过）。
    EXPECT_TRUE(report.submittable);
    EXPECT_FALSE(outcome.error.has_value()) << "完整样例无具名阻断条件";
}

/**
 * 缺失 <inertial>（acceptance 2——"缺 inertial→NotProvided 走降级"入默认
 * 补全清单；V-15 第④例建模面）：body 三字段 NotProvided 不断言（DYN-06）。
 */
TEST(MdlImport, MissingInertial_NotProvidedInDefaults_WP13T05_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-03", "MDL-06"},
                  std::vector<std::string>{"AT-15"});

    const ModelImportMapper mapper;
    const char* urdf = R"(<?xml version="1.0"?>
<robot name="n">
  <link name="base"/>
  <link name="l"/>
  <joint name="j" type="revolute">
    <parent link="base"/>
    <child link="l"/>
    <axis xyz="0 0 1"/>
    <limit lower="0" upper="1"/>
  </joint>
</robot>
)";
    std::vector<sdurws::ird::core::DiagnosticRecord> diags;
    const ImportOutcome outcome = mapper.mapUrdf(makeSource(urdf), ImportOptions{}, diags);
    ASSERT_TRUE(outcome.draft.has_value());
    const auto& body = outcome.draft->links[1].body;
    EXPECT_EQ(body.mass.state(), FieldState::NotProvided);
    EXPECT_EQ(body.centerOfMass.state(), FieldState::NotProvided);
    EXPECT_EQ(body.inertia.state(), FieldState::NotProvided);
    bool defaulted = false;
    for (const auto& item : outcome.report.defaults) {
        if (item.field.rfind("links[1].body", 0) == 0
            && item.appliedValue.find("not-provided") != std::string::npos) {
            defaulted = true;
        }
    }
    EXPECT_TRUE(defaulted) << "缺 inertial 的降级必须入默认补全清单";
    EXPECT_TRUE(outcome.report.submittable) << "物性缺失走降级（不阻断——V-15 第④例）";
}

/**
 * 回归（F-570，P0——audit/unit-code-review-20261009）：<origin> 属性部分
 * 缺省曾被整条误判数值非法（缺失属性取空串喂 parseVec3Stable→false→
 * originIllegal→valueIllegal→submittable=false）——而 URDF 规范规定
 * xyz/rpy 均**可选**、缺省＝零向量/零旋转，只写 xyz 不写 rpy 是真实
 * URDF 文件的常见写法，常规导入即不可提交。修复后：缺失属性补零缺省
 * （映射照常 Provided）＋默认补全清单可追溯（NFR-COR-03）。
 *
 * 验证：①关节 origin 只带 xyz→Planned 面（origin Provided：z=0.2＋恒等
 * 旋转）、defaults 含 "rpy=0 0 0" 条目；②inertial origin 只带 rpy→质心
 * Provided 零向量、defaults 含 "xyz=0 0 0" 条目；③可提交性恢复 true。
 */
TEST(MdlImport, PartialOriginAttributes_DefaultZeroNotIllegal_F570)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-03", "MDL-06"},
                  std::vector<std::string>{"AT-15"});

    const ModelImportMapper mapper;
    const char* urdf = R"(<?xml version="1.0"?>
<robot name="n">
  <link name="base">
    <inertial>
      <origin rpy="0 0 0.5"/>
      <mass value="2.0"/>
      <inertia ixx="0.01" iyy="0.01" izz="0.01" ixy="0" ixz="0" iyz="0"/>
    </inertial>
  </link>
  <link name="l"/>
  <joint name="j" type="revolute">
    <parent link="base"/>
    <child link="l"/>
    <origin xyz="0 0 0.2"/>
    <axis xyz="0 0 1"/>
    <limit lower="0" upper="1"/>
  </joint>
</robot>
)";
    std::vector<sdurws::ird::core::DiagnosticRecord> diags;
    const ImportOutcome outcome = mapper.mapUrdf(makeSource(urdf), ImportOptions{}, diags);
    ASSERT_TRUE(outcome.draft.has_value());

    // ① 关节 origin：xyz 解析保留、rpy 缺省＝零旋转（恒等阵）。
    const auto jOrigin = outcome.draft->joints[0].origin.tryValue();
    ASSERT_TRUE(jOrigin.has_value()) << "部分缺省 origin 不得再走 invalid 面";
    const auto jT = static_cast<rw::math::Transform3D<double>>(*jOrigin);
    EXPECT_EQ(jT.P()[2], 0.2);  // xyz="0 0 0.2" 的 z 分量（m）
    const rw::math::Rotation3D<double> identity(
        1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0);
    EXPECT_TRUE(jT.R() == identity) << "rpy 缺省＝零旋转";

    // ② inertial origin：xyz 缺省＝质心零向量（连杆系）、rpy 保留给定值
    //    （惯量旋转按 0 0 0.5 执行——语义不受 xyz 缺省影响）。
    const auto com = outcome.draft->links[0].body.centerOfMass.tryValue();
    ASSERT_TRUE(com.has_value());
    EXPECT_EQ((*com)[0], 0.0);
    EXPECT_EQ((*com)[1], 0.0);
    EXPECT_EQ((*com)[2], 0.0);

    // ③ 默认补全清单可追溯：两处缺省各入一条（NFR-COR-03 不静默）。
    bool jointRpyDefaulted = false;
    bool inertialXyzDefaulted = false;
    for (const auto& item : outcome.report.defaults) {
        if (item.field == "joints[0].origin"
            && item.appliedValue.find("rpy=0 0 0") != std::string::npos) {
            jointRpyDefaulted = true;
        }
        if (item.field == "links[0].body.centerOfMass"
            && item.appliedValue.find("xyz=0 0 0") != std::string::npos) {
            inertialXyzDefaulted = true;
        }
    }
    EXPECT_TRUE(jointRpyDefaulted) << "关节 origin rpy 缺省必须入补全清单";
    EXPECT_TRUE(inertialXyzDefaulted) << "inertial origin xyz 缺省必须入补全清单";

    // ④ 可提交性恢复：部分缺省不再是 valueIllegal（修复前 false）。
    EXPECT_TRUE(outcome.report.submittable)
        << "URDF 规范合法的部分缺省 origin 不得阻断提交";
}

/**
 * 回归（F-570）反面：属性**存在但语法非法**仍走原非法值面——缺省补零
 * 不构成对非法值的静默改写（NFR-COR-03/ERR-01 边界：容错不等于改值）。
 */
TEST(MdlImport, IllegalOriginTextStillRejected_F570)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-03"},
                  std::vector<std::string>{});

    const ModelImportMapper mapper;
    const char* urdf = R"(<?xml version="1.0"?>
<robot name="n">
  <link name="base"/>
  <link name="l"/>
  <joint name="j" type="revolute">
    <parent link="base"/>
    <child link="l"/>
    <origin xyz="abc" rpy="0 0 0"/>
    <axis xyz="0 0 1"/>
    <limit lower="0" upper="1"/>
  </joint>
</robot>
)";
    std::vector<sdurws::ird::core::DiagnosticRecord> diags;
    const ImportOutcome outcome = mapper.mapUrdf(makeSource(urdf), ImportOptions{}, diags);
    ASSERT_TRUE(outcome.draft.has_value());
    // 非法原串保留（invalid 态）＋错误项＋不可提交（原面不变）。
    EXPECT_FALSE(outcome.draft->joints[0].origin.tryValue().has_value());
    EXPECT_EQ(outcome.draft->joints[0].origin.state(), FieldState::Invalid);
    bool hasOriginError = false;
    for (const auto& item : outcome.report.errors) {
        if (item.kind == "value-illegal"
            && item.subject.find("joint 'j'.origin") != std::string::npos) {
            hasOriginError = true;
        }
    }
    EXPECT_TRUE(hasOriginError);
    EXPECT_FALSE(outcome.report.submittable);
}

/**
 * 回归（F-633，P1——audit r2a 批次）：URDF origin 数值写 "nan" 字面量曾被
 * 放行——parseDoubleStable 底层 std::from_chars 接受 "nan"/"inf"（其契约
 * 明言"有限性由调用方按字段语义判定"），而 origin 路径没有判定点，NaN
 * 位姿以 Provided＋submittable=true 落草稿（下游旋转矩阵/位姿组合被 NaN
 * 污染且零报告面）。修复后与同文件轴路径 F-587 有限性判定同族：任一分量
 * 非有限→originIllegal→value-illegal 错误项（原串保留）＋submittable=false。
 *
 * 用例 ①：关节 origin xyz="nan 0 0"（rpy 合法）——必须走 Invalid 面。
 */
TEST(MdlImport, NonFiniteOriginXyzNan_Rejected_F633)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-03"},
                  std::vector<std::string>{});

    const ModelImportMapper mapper;
    const char* urdf = R"(<?xml version="1.0"?>
<robot name="n">
  <link name="base"/>
  <link name="l"/>
  <joint name="j" type="revolute">
    <parent link="base"/>
    <child link="l"/>
    <origin xyz="nan 0 0" rpy="0 0 0"/>
    <axis xyz="0 0 1"/>
    <limit lower="0" upper="1"/>
  </joint>
</robot>
)";
    std::vector<sdurws::ird::core::DiagnosticRecord> diags;
    const ImportOutcome outcome = mapper.mapUrdf(makeSource(urdf), ImportOptions{}, diags);
    ASSERT_TRUE(outcome.draft.has_value());
    // "nan" 语法可解析（from_chars 接受该拼写）但非有限——必须走 Invalid
    // 面（修复前 Provided 带 NaN 值）。
    EXPECT_FALSE(outcome.draft->joints[0].origin.tryValue().has_value());
    EXPECT_EQ(outcome.draft->joints[0].origin.state(), FieldState::Invalid);
    // 错误详情携原串（originRaw＝xyz/rpy 属性原文拼接——报告可定位源文件
    // 写法，NFR-COR-03 非法值不静默改写）。
    bool hasOriginError = false;
    for (const auto& item : outcome.report.errors) {
        if (item.kind == "value-illegal"
            && item.subject.find("joint 'j'.origin") != std::string::npos
            && item.detail.find("nan") != std::string::npos) {
            hasOriginError = true;
        }
    }
    EXPECT_TRUE(hasOriginError) << "错误详情必须保留 nan 原串";
    EXPECT_FALSE(outcome.report.submittable);
}

/**
 * 回归（F-633）用例 ②：origin 只写 rpy="inf 0 1"——一属性缺省（xyz 按
 * URDF 语义补零，F-570 面）＋另一属性非有限→整体拒绝。两项既有语义一次
 * 锁定：缺省补零不因非有限复检失效（缺省分支不触碰 ok），非有限判定不因
 * 另一属性缺省而短路放行。
 */
TEST(MdlImport, NonFiniteOriginRpyInf_Rejected_F633)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-03"},
                  std::vector<std::string>{});

    const ModelImportMapper mapper;
    const char* urdf = R"(<?xml version="1.0"?>
<robot name="n">
  <link name="base"/>
  <link name="l"/>
  <joint name="j" type="revolute">
    <parent link="base"/>
    <child link="l"/>
    <origin rpy="inf 0 1"/>
    <axis xyz="0 0 1"/>
    <limit lower="0" upper="1"/>
  </joint>
</robot>
)";
    std::vector<sdurws::ird::core::DiagnosticRecord> diags;
    const ImportOutcome outcome = mapper.mapUrdf(makeSource(urdf), ImportOptions{}, diags);
    ASSERT_TRUE(outcome.draft.has_value());
    // xyz 缺省补零不挽救 rpy 的 inf——整体 originIllegal（一缺一非法≠放行）。
    EXPECT_FALSE(outcome.draft->joints[0].origin.tryValue().has_value());
    EXPECT_EQ(outcome.draft->joints[0].origin.state(), FieldState::Invalid);
    bool hasOriginError = false;
    for (const auto& item : outcome.report.errors) {
        if (item.kind == "value-illegal"
            && item.subject.find("joint 'j'.origin") != std::string::npos
            && item.detail.find("inf") != std::string::npos) {
            hasOriginError = true;
        }
    }
    EXPECT_TRUE(hasOriginError) << "错误详情必须保留 inf 原串";
    EXPECT_FALSE(outcome.report.submittable);
}

/**
 * 物理合法性错误项（acceptance 2——"已提供但 m≤0/非 SPD→导入报告错误项
 * （应用将被断言阻断）"）：m=0→mass-nonpositive；零张量→inertia-not-spd；
 * diag(10,1,1)→inertia-triangle；三者皆置 submittable=false。
 */
TEST(MdlImport, PhysicalIllegality_ReportErrorItems_WP13T05_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-03", "MDL-06"},
                  std::vector<std::string>{});

    const ModelImportMapper mapper;
    const char* urdf = R"(<?xml version="1.0"?>
<robot name="p">
  <link name="base"/>
  <link name="l">
    <inertial>
      <mass value="0"/>
      <inertia ixx="10" iyy="1" izz="1" ixy="0" ixz="0" iyz="0"/>
    </inertial>
  </link>
  <joint name="j" type="revolute">
    <parent link="base"/>
    <child link="l"/>
    <axis xyz="0 0 1"/>
    <limit lower="0" upper="1"/>
  </joint>
</robot>
)";
    std::vector<sdurws::ird::core::DiagnosticRecord> diags;
    const ImportOutcome outcome = mapper.mapUrdf(makeSource(urdf), ImportOptions{}, diags);
    ASSERT_TRUE(outcome.draft.has_value()) << "错误项是预提示面——草稿仍产出";
    bool massBad = false;
    bool triangleBad = false;
    for (const auto& item : outcome.report.errors) {
        if (item.kind == "mass-nonpositive") { massBad = true; }
        if (item.kind == "inertia-triangle") { triangleBad = true; }
    }
    EXPECT_TRUE(massBad) << "m≤0→mass-nonpositive 错误项";
    EXPECT_TRUE(triangleBad) << "λmax>λmid+λmin→inertia-triangle 错误项（diag(10,1,1)）";
    EXPECT_FALSE(outcome.report.submittable) << "物理违例面→不可提交修订";
}

/**
 * 名称冲突与非法数值保留（acceptance 2/5——NameConflict 应用边界前拦截
 * （I-MDL-2）；非法值 invalid 态保留原串，NFR-COR-03 不静默转 0）。
 */
TEST(MdlImport, NameConflictAndIllegalValue_Preserved_WP13T05_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-03"},
                  std::vector<std::string>{});

    const ModelImportMapper mapper;
    // 两个不同源名净化后同串（"a b"/"a?b"→"a_b"）＋质量语法非法。为什么
    // 不用字面同名：重名会破坏名称索引的结构解析（SourceInconsistent 抢先
    // 失败）——净化冲突是 NameConflict 的本征形态（I-MDL-2 判据＝草稿内
    // 最终 localName）。
    const char* urdf = R"(<?xml version="1.0"?>
<robot name="c">
  <link name="a b"/>
  <link name="a?b"/>
  <link name="l">
    <inertial>
      <mass value="abc"/>
    </inertial>
  </link>
  <joint name="j1" type="fixed">
    <parent link="a b"/>
    <child link="a?b"/>
  </joint>
  <joint name="j" type="revolute">
    <parent link="a?b"/>
    <child link="l"/>
    <axis xyz="0 0 1"/>
    <limit lower="0" upper="1"/>
  </joint>
</robot>
)";
    std::vector<sdurws::ird::core::DiagnosticRecord> diags;
    const ImportOutcome outcome = mapper.mapUrdf(makeSource(urdf), ImportOptions{}, diags);
    ASSERT_TRUE(outcome.error.has_value());
    EXPECT_EQ(outcome.error->code, ImportErrorCode::NameConflict);
    EXPECT_TRUE(outcome.draft.has_value()) << "名称冲突：草稿仍产出（呈现/编辑用）";
    EXPECT_FALSE(outcome.report.submittable);
    bool conflictItem = false;
    for (const auto& item : outcome.report.errors) {
        if (item.kind == "name-conflict") { conflictItem = true; }
    }
    EXPECT_TRUE(conflictItem);
    // 非法质量：invalid 态保留原串（NFR-COR-03）。
    ASSERT_EQ(outcome.draft->links[2].body.mass.state(), FieldState::Invalid);
    EXPECT_EQ(outcome.draft->links[2].body.mass.invalidRawInput(), "abc");
}

// =====================================================================
// ACC3：MDL-11 轴语义三分支
// =====================================================================

/**
 * 缺 <axis>（acceptance 3——"缺失 axis→按 URDF 语义取局部 +X 并入待确认
 * 草稿清单"）：+X Provided（ImportMapped）＋待确认条目＋PENDING-CONFIRM
 * 警告诊断；submittable 保持 true（确认后可应用）。
 */
TEST(MdlImport, MissingAxis_DefaultPlusXPendingConfirm_WP13T05_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-11"},
                  std::vector<std::string>{});

    const ModelImportMapper mapper;
    const char* urdf = R"(<?xml version="1.0"?>
<robot name="m">
  <link name="a"/>
  <link name="b"/>
  <joint name="j" type="revolute">
    <parent link="a"/>
    <child link="b"/>
    <limit lower="0" upper="1"/>
  </joint>
</robot>
)";
    std::vector<sdurws::ird::core::DiagnosticRecord> diags;
    const ImportOutcome outcome = mapper.mapUrdf(makeSource(urdf), ImportOptions{}, diags);
    ASSERT_TRUE(outcome.draft.has_value());
    ASSERT_EQ(outcome.draft->joints[0].axis.state(), FieldState::Provided);
    EXPECT_DOUBLE_EQ(outcome.draft->joints[0].axis.value()[0], 1.0);
    EXPECT_DOUBLE_EQ(outcome.draft->joints[0].axis.value()[1], 0.0);
    EXPECT_DOUBLE_EQ(outcome.draft->joints[0].axis.value()[2], 0.0);
    ASSERT_EQ(outcome.report.pendingConfirms.size(), 1U);
    EXPECT_EQ(outcome.report.pendingConfirms[0].kind, "axis-default-plus-x");
    EXPECT_TRUE(hasDiag(diags, kMdlImportPendingConfirm));
    EXPECT_TRUE(outcome.report.submittable) << "待确认项不阻断（逐条确认流）";
    EXPECT_FALSE(outcome.error.has_value());
}

/**
 * 零轴（acceptance 3——"零轴/非有限轴→仅报告＋该关节标记 Invalid＋包含
 * 该类关节的草稿不得提交修订"）：关节状态 invalid＋ZERO-AXIS 诊断＋
 * ZeroAxisReported＋轴 invalid 态保留原串；草稿仍产出（仅报告不静默删）。
 */
TEST(MdlImport, ZeroAxis_ReportOnlyInvalidNotSubmittable_WP13T05_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-11"},
                  std::vector<std::string>{});

    const ModelImportMapper mapper;
    const char* urdf = R"(<?xml version="1.0"?>
<robot name="z">
  <link name="a"/>
  <link name="b"/>
  <joint name="j" type="revolute">
    <parent link="a"/>
    <child link="b"/>
    <axis xyz="0 0 0"/>
    <limit lower="0" upper="1"/>
  </joint>
</robot>
)";
    std::vector<sdurws::ird::core::DiagnosticRecord> diags;
    const ImportOutcome outcome = mapper.mapUrdf(makeSource(urdf), ImportOptions{}, diags);
    ASSERT_TRUE(outcome.draft.has_value()) << "零轴仅报告——草稿仍产出";
    ASSERT_EQ(outcome.draft->joints[0].axis.state(), FieldState::Invalid);
    EXPECT_EQ(outcome.draft->joints[0].axis.invalidRawInput(), "0 0 0");
    ASSERT_EQ(outcome.report.jointStatuses.size(), 1U);
    EXPECT_EQ(outcome.report.jointStatuses[0].status, "invalid");
    EXPECT_NE(outcome.report.jointStatuses[0].reason.find("0 0 0"), std::string::npos)
        << "轴原文可观察（诊断定位面）";
    EXPECT_TRUE(hasDiag(diags, kMdlImportZeroAxis));
    ASSERT_TRUE(outcome.error.has_value());
    EXPECT_EQ(outcome.error->code, ImportErrorCode::ZeroAxisReported);
    EXPECT_FALSE(outcome.report.submittable) << "含零轴关节的草稿不得提交修订";
}

/**
 * 非有限轴（acceptance 3——同零轴口径）：NaN 分量→Invalid＋ZERO-AXIS
 * 诊断＋不可提交。
 */
TEST(MdlImport, NonFiniteAxis_ReportOnly_WP13T05_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-11"},
                  std::vector<std::string>{});

    const ModelImportMapper mapper;
    const char* urdf = R"(<?xml version="1.0"?>
<robot name="nf">
  <link name="a"/>
  <link name="b"/>
  <joint name="j" type="prismatic">
    <parent link="a"/>
    <child link="b"/>
    <axis xyz="0 NaN 0"/>
    <limit lower="0" upper="1"/>
  </joint>
</robot>
)";
    std::vector<sdurws::ird::core::DiagnosticRecord> diags;
    const ImportOutcome outcome = mapper.mapUrdf(makeSource(urdf), ImportOptions{}, diags);
    ASSERT_TRUE(outcome.draft.has_value());
    EXPECT_EQ(outcome.draft->joints[0].axis.state(), FieldState::Invalid);
    EXPECT_TRUE(hasDiag(diags, kMdlImportZeroAxis));
    ASSERT_TRUE(outcome.error.has_value());
    EXPECT_EQ(outcome.error->code, ImportErrorCode::ZeroAxisReported);
    EXPECT_FALSE(outcome.report.submittable);
}

// =====================================================================
// 阻断型关节面（维度二前提——mimic/不可表达类型；V-10/M-6 的关节级面）
// =====================================================================

/**
 * mimic 关节（acceptance 4 关节级——"识别＋报告＋阻断；类型保留不转
 * FixedFrame"）：类型保留 Revolute＋unsupported 条目＋UNSUPPORTED-JOINT
 * 诊断＋MimicBlocked＋不可提交。
 */
TEST(MdlImport, MimicJoint_TypePreservedBlocked_WP13T05_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-12"},
                  std::vector<std::string>{"AT-17"});

    const ModelImportMapper mapper;
    const char* urdf = R"(<?xml version="1.0"?>
<robot name="mm">
  <link name="a"/>
  <link name="b"/>
  <joint name="j" type="revolute">
    <parent link="a"/>
    <child link="b"/>
    <axis xyz="0 0 1"/>
    <limit lower="-1" upper="1"/>
    <mimic joint="other" multiplier="2"/>
  </joint>
</robot>
)";
    std::vector<sdurws::ird::core::DiagnosticRecord> diags;
    const ImportOutcome outcome = mapper.mapUrdf(makeSource(urdf), ImportOptions{}, diags);
    ASSERT_TRUE(outcome.draft.has_value()) << "识别＋报告：草稿产出（类型保留）";
    EXPECT_EQ(outcome.draft->joints[0].type, sdurws::ird::modeling::JointType::Revolute)
        << "不得转 FixedFrame（M-6）";
    ASSERT_EQ(outcome.report.unsupported.size(), 1U);
    EXPECT_EQ(outcome.report.unsupported[0].kind, "mimic-joint");
    EXPECT_TRUE(hasDiag(diags, kMdlImportUnsupportedJoint));
    ASSERT_TRUE(outcome.error.has_value());
    EXPECT_EQ(outcome.error->code, ImportErrorCode::MimicBlocked);
    EXPECT_FALSE(outcome.report.submittable) << "含 mimic 的草稿不可提交（V-10）";
}

/**
 * 不可表达关节类型（acceptance 4——planar/floating：无草稿产出，不得转
 * FixedFrame）：UnsupportedJointType＋UNSUPPORTED-JOINT 诊断＋报告条目。
 */
TEST(MdlImport, PlanarJoint_UnsupportedNoDraft_WP13T05_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-12"},
                  std::vector<std::string>{"AT-17"});

    const ModelImportMapper mapper;
    const char* urdf = R"(<?xml version="1.0"?>
<robot name="pl">
  <link name="a"/>
  <link name="b"/>
  <joint name="j" type="planar">
    <parent link="a"/>
    <child link="b"/>
    <axis xyz="0 0 1"/>
  </joint>
</robot>
)";
    std::vector<sdurws::ird::core::DiagnosticRecord> diags;
    const ImportOutcome outcome = mapper.mapUrdf(makeSource(urdf), ImportOptions{}, diags);
    EXPECT_FALSE(outcome.draft.has_value()) << "不可表达类型无草稿（无半成品）";
    ASSERT_TRUE(outcome.error.has_value());
    EXPECT_EQ(outcome.error->code, ImportErrorCode::UnsupportedJointType);
    EXPECT_EQ(outcome.error->params[0].first, "joint-name");
    EXPECT_EQ(outcome.error->params[0].second, "j");
    EXPECT_EQ(outcome.error->params[1].second, "planar");
    ASSERT_EQ(outcome.report.unsupported.size(), 1U);
    EXPECT_EQ(outcome.report.unsupported[0].kind, "joint-type");
    EXPECT_TRUE(hasDiag(diags, kMdlImportUnsupportedJoint));
}

/**
 * 多可动分支未选链（acceptance 4 维度一前半——"分支报告（对象与原因可
 * 观察）＋用户显式选择主链——未经用户显式选择不排除可动分支"）：
 * MultiBranchNeedsSelection＋无草稿＋分支根清单可观察（report.branches
 * 条目随链型判定提交增列——本用例钉住 error params 面）。
 */
TEST(MdlImport, MultiBranch_NoSelectionRejected_WP13T05_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-12"},
                  std::vector<std::string>{"AT-17"});

    const ModelImportMapper mapper;
    const char* urdf = R"(<?xml version="1.0"?>
<robot name="mb">
  <link name="root"/>
  <link name="la"/>
  <link name="lb"/>
  <joint name="ja" type="revolute">
    <parent link="root"/>
    <child link="la"/>
    <axis xyz="0 0 1"/>
    <limit lower="0" upper="1"/>
  </joint>
  <joint name="jb" type="revolute">
    <parent link="root"/>
    <child link="lb"/>
    <axis xyz="0 0 1"/>
    <limit lower="0" upper="1"/>
  </joint>
</robot>
)";
    std::vector<sdurws::ird::core::DiagnosticRecord> diags;
    const ImportOutcome outcome = mapper.mapUrdf(makeSource(urdf), ImportOptions{}, diags);
    EXPECT_FALSE(outcome.draft.has_value()) << "未经显式选链不排除可动分支——无草稿";
    ASSERT_TRUE(outcome.error.has_value());
    EXPECT_EQ(outcome.error->code, ImportErrorCode::MultiBranchNeedsSelection);
    // 分支对象可观察：branch-roots/branch-count/split-link 三要素。
    bool rootsFound = false;
    bool countFound = false;
    bool splitFound = false;
    for (const auto& param : outcome.error->params) {
        if (param.first == "branch-roots") {
            rootsFound = true;
            EXPECT_EQ(param.second, "la,lb");
        }
        if (param.first == "branch-count") { countFound = true; }
        if (param.first == "split-link") {
            splitFound = true;
            EXPECT_EQ(param.second, "root");
        }
    }
    EXPECT_TRUE(rootsFound);
    EXPECT_TRUE(countFound);
    EXPECT_TRUE(splitFound);
    EXPECT_FALSE(outcome.report.submittable);
    // 分支报告（§6.4 维度一——对象与原因可观察）：两候选分支均为
    // pending-selection 处置＋BRANCH-SELECTION info 诊断。
    ASSERT_EQ(outcome.report.branches.size(), 2U);
    EXPECT_EQ(outcome.report.branches[0].branchRoot, "la");
    EXPECT_EQ(outcome.report.branches[0].disposition, "pending-selection");
    EXPECT_EQ(outcome.report.branches[0].splitLink, "root");
    EXPECT_EQ(outcome.report.branches[1].branchRoot, "lb");
    EXPECT_TRUE(hasDiag(diags, kMdlImportBranchSelection));
}

// =====================================================================
// ACC4：链型判定两维度（选链放行/辅助分支候选/能力矩阵/类型保留）
// =====================================================================

namespace {

/**
 * 构造 N 轴全旋转串联链 fixture（base→j1..jN→leaf；每关节 revolute＋
 * 限位＋axis 0 0 1——六/七轴模板形态）。
 */
std::string makeRevoluteChainUrdf(int axes)
{
    std::string urdf = "<?xml version=\"1.0\"?>\n<robot name=\"chain\">\n";
    urdf += "  <link name=\"base\"/>\n";
    for (int i = 0; i < axes; ++i) {
        urdf += "  <link name=\"l" + std::to_string(i + 1) + "\"/>\n";
    }
    for (int i = 0; i < axes; ++i) {
        const std::string parent = (i == 0) ? "base" : ("l" + std::to_string(i));
        urdf += "  <joint name=\"j" + std::to_string(i + 1)
                + "\" type=\"revolute\">\n"
                  "    <parent link=\"" + parent + "\"/>\n"
                  "    <child link=\"l" + std::to_string(i + 1) + "\"/>\n"
                  "    <axis xyz=\"0 0 1\"/>\n"
                  "    <limit lower=\"-1\" upper=\"1\"/>\n"
                  "  </joint>\n";
    }
    urdf += "</robot>\n";
    return urdf;
}

}  // namespace

/**
 * 六轴全旋转主链（acceptance 4 维度二——能力矩阵第一行）：FullTemplate
 * Range、可动轴数 6、不含 prismatic、无 TEMPLATE-RANGE 诊断。
 */
TEST(MdlImport, SixRevoluteChain_FullTemplateRange_WP13T05_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-12"},
                  std::vector<std::string>{"AT-17"});

    const ModelImportMapper mapper;
    std::vector<sdurws::ird::core::DiagnosticRecord> diags;
    const ImportOutcome outcome =
        mapper.mapUrdf(makeSource(makeRevoluteChainUrdf(6)), ImportOptions{}, diags);
    ASSERT_TRUE(outcome.draft.has_value());
    EXPECT_EQ(outcome.report.chainCapability.kind,
              sdurws::ird::modeling::ChainCapabilityKind::FullTemplateRange);
    EXPECT_EQ(outcome.report.chainCapability.movableAxes, 6U);
    EXPECT_FALSE(outcome.report.chainCapability.containsPrismatic);
    EXPECT_FALSE(hasDiag(diags, kMdlImportTemplateRange)) << "全能力链无范围诊断";
    EXPECT_TRUE(outcome.report.submittable);
    EXPECT_FALSE(outcome.error.has_value());
}

/**
 * 七轴全旋转主链（acceptance 4 维度二）：同为 FullTemplateRange（P-03
 * 七轴模板登记不启用不影响导入识别能力面——能力矩阵行原文"六/七轴"）。
 */
TEST(MdlImport, SevenRevoluteChain_FullTemplateRange_WP13T05_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-12"},
                  std::vector<std::string>{});

    const ModelImportMapper mapper;
    std::vector<sdurws::ird::core::DiagnosticRecord> diags;
    const ImportOutcome outcome =
        mapper.mapUrdf(makeSource(makeRevoluteChainUrdf(7)), ImportOptions{}, diags);
    ASSERT_TRUE(outcome.draft.has_value());
    EXPECT_EQ(outcome.report.chainCapability.kind,
              sdurws::ird::modeling::ChainCapabilityKind::FullTemplateRange);
    EXPECT_EQ(outcome.report.chainCapability.movableAxes, 7U);
}

/**
 * 4/5 轴主链（acceptance 4——"4/5 轴与含 prismatic 主链→正式计算/报告
 * 阻断＋诊断（草稿兼容编辑可）"）：BeyondTemplateRange＋TEMPLATE-RANGE
 * 诊断＋草稿产出可提交（兼容编辑）＋类型保留。
 */
TEST(MdlImport, FiveAxes_BeyondRange_DraftEditable_WP13T05_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-12"},
                  std::vector<std::string>{"AT-17"});

    const ModelImportMapper mapper;
    std::vector<sdurws::ird::core::DiagnosticRecord> diags;
    const ImportOutcome outcome =
        mapper.mapUrdf(makeSource(makeRevoluteChainUrdf(5)), ImportOptions{}, diags);
    ASSERT_TRUE(outcome.draft.has_value()) << "范围外链仍产草稿（兼容编辑）";
    EXPECT_EQ(outcome.report.chainCapability.kind,
              sdurws::ird::modeling::ChainCapabilityKind::BeyondTemplateRange);
    EXPECT_EQ(outcome.report.chainCapability.movableAxes, 5U);
    EXPECT_NE(outcome.report.chainCapability.reason.find("4/5"), std::string::npos);
    EXPECT_TRUE(hasDiag(diags, kMdlImportTemplateRange));
    EXPECT_TRUE(outcome.report.submittable) << "草稿兼容编辑——不置不可提交";
    EXPECT_FALSE(outcome.error.has_value());
    for (const auto& joint : outcome.draft->joints) {
        EXPECT_EQ(joint.type, sdurws::ird::modeling::JointType::Revolute)
            << "类型保留（不静默降级——V12-01）";
    }
}

/**
 * 六轴含 prismatic 混合链（MDL-12-S1——WP-13-T19 启用面）：五转一移
 * （6 可动轴）→FullTemplateRange（正式计算/报告放开）＋无 TEMPLATE-RANGE
 * 诊断＋prismatic 类型原样入草稿（类型保留 V12-01——本用例自 WP-13-T05
 * ACC4 的 R1 阻断口径随 MDL-12-S1 语义演进重钉，翻转面有据）。
 */
TEST(MdlImport, PrismaticInChain_HybridFullRange_WP13T19_AT36)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-12-S1"},
                  std::vector<std::string>{"AT-36", "AT-17"});

    const ModelImportMapper mapper;
    // 六关节链：j1..j5 revolute＋j6 prismatic（限位单位 m）——在五转链
    // 尾部追加（去掉生成器的闭合标签再拼装）。
    const std::string base = makeRevoluteChainUrdf(5);
    const std::size_t tail = base.rfind("</robot>");
    ASSERT_NE(tail, std::string::npos);
    const std::string urdf =
        base.substr(0, tail)
        + "  <link name=\"l6\"/>\n"
          "  <joint name=\"j6\" type=\"prismatic\">\n"
          "    <parent link=\"l5\"/>\n    <child link=\"l6\"/>\n"
          "    <axis xyz=\"0 0 1\"/>\n"
          "    <limit lower=\"0\" upper=\"0.5\"/>\n"
          "  </joint>\n</robot>\n";
    std::vector<sdurws::ird::core::DiagnosticRecord> diags;
    const ImportOutcome outcome = mapper.mapUrdf(makeSource(urdf), ImportOptions{}, diags);
    ASSERT_TRUE(outcome.draft.has_value());
    ASSERT_EQ(outcome.draft->joints.size(), 6U);
    EXPECT_EQ(outcome.draft->joints[5].type,
              sdurws::ird::modeling::JointType::Prismatic) << "类型保留 V12-01";
    ASSERT_EQ(outcome.draft->joints[5].bounds.state(), FieldState::Provided);
    EXPECT_DOUBLE_EQ(outcome.draft->joints[5].bounds.value().second, 0.5)
        << "prismatic 限位单位 m";
    EXPECT_EQ(outcome.report.chainCapability.kind,
              sdurws::ird::modeling::ChainCapabilityKind::FullTemplateRange)
        << "MDL-12-S1：六轴含 prismatic 放开（正式计算/报告）";
    EXPECT_EQ(outcome.report.chainCapability.movableAxes, 6U);
    EXPECT_TRUE(outcome.report.chainCapability.containsPrismatic);
    EXPECT_NE(outcome.report.chainCapability.reason.find("prismatic"), std::string::npos);
    EXPECT_FALSE(hasDiag(diags, kMdlImportTemplateRange))
        << "放开链不再产 TEMPLATE-RANGE 诊断";
    EXPECT_TRUE(outcome.report.submittable);
}

/**
 * 七轴含 prismatic 混合链（MDL-12-S1——"仅限六/七轴"的七轴半边）：六转
 * 一移（7 可动轴）→FullTemplateRange＋无 TEMPLATE-RANGE 诊断。
 */
TEST(MdlImport, SevenAxesWithPrismatic_HybridFullRange_WP13T19_AT36)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-12-S1"},
                  std::vector<std::string>{"AT-36"});

    const ModelImportMapper mapper;
    const std::string base = makeRevoluteChainUrdf(6);
    const std::size_t tail = base.rfind("</robot>");
    ASSERT_NE(tail, std::string::npos);
    const std::string urdf =
        base.substr(0, tail)
        + "  <link name=\"l7\"/>\n"
          "  <joint name=\"j7\" type=\"prismatic\">\n"
          "    <parent link=\"l6\"/>\n    <child link=\"l7\"/>\n"
          "    <axis xyz=\"0 0 1\"/>\n"
          "    <limit lower=\"-0.2\" upper=\"0.2\"/>\n"
          "  </joint>\n</robot>\n";
    std::vector<sdurws::ird::core::DiagnosticRecord> diags;
    const ImportOutcome outcome = mapper.mapUrdf(makeSource(urdf), ImportOptions{}, diags);
    ASSERT_TRUE(outcome.draft.has_value());
    ASSERT_EQ(outcome.draft->joints.size(), 7U);
    EXPECT_EQ(outcome.report.chainCapability.kind,
              sdurws::ird::modeling::ChainCapabilityKind::FullTemplateRange);
    EXPECT_EQ(outcome.report.chainCapability.movableAxes, 7U);
    EXPECT_TRUE(outcome.report.chainCapability.containsPrismatic);
    EXPECT_FALSE(hasDiag(diags, kMdlImportTemplateRange));
}

/**
 * 4 轴含 prismatic 反例（MDL-12-S1 禁止项——"4/5 轴不因本子项放开"，
 * DTB §2.14 行）：三转一移（4 可动轴）→仍 BeyondTemplateRange＋
 * TEMPLATE-RANGE 诊断＋reason 携带 prismatic 与不放开语义＋类型保留。
 */
TEST(MdlImport, FourAxesWithPrismatic_StillBlocked_WP13T19_AT36)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-12-S1"},
                  std::vector<std::string>{"AT-36", "AT-17"});

    const ModelImportMapper mapper;
    const std::string base = makeRevoluteChainUrdf(3);
    const std::size_t tail = base.rfind("</robot>");
    ASSERT_NE(tail, std::string::npos);
    const std::string urdf =
        base.substr(0, tail)
        + "  <link name=\"l4\"/>\n"
          "  <joint name=\"j4\" type=\"prismatic\">\n"
          "    <parent link=\"l3\"/>\n    <child link=\"l4\"/>\n"
          "    <axis xyz=\"0 0 1\"/>\n"
          "    <limit lower=\"0\" upper=\"0.4\"/>\n"
          "  </joint>\n</robot>\n";
    std::vector<sdurws::ird::core::DiagnosticRecord> diags;
    const ImportOutcome outcome = mapper.mapUrdf(makeSource(urdf), ImportOptions{}, diags);
    ASSERT_TRUE(outcome.draft.has_value()) << "范围外链仍产草稿（兼容编辑）";
    EXPECT_EQ(outcome.draft->joints[3].type,
              sdurws::ird::modeling::JointType::Prismatic) << "类型保留 V12-01";
    EXPECT_EQ(outcome.report.chainCapability.kind,
              sdurws::ird::modeling::ChainCapabilityKind::BeyondTemplateRange)
        << "4 轴不因 MDL-12-S1 放开（仅六/七轴）";
    EXPECT_EQ(outcome.report.chainCapability.movableAxes, 4U);
    EXPECT_TRUE(outcome.report.chainCapability.containsPrismatic);
    EXPECT_NE(outcome.report.chainCapability.reason.find("prismatic"), std::string::npos);
    EXPECT_TRUE(hasDiag(diags, kMdlImportTemplateRange));
    EXPECT_TRUE(outcome.report.submittable) << "草稿兼容编辑——不置不可提交";
}

/**
 * 判定函数参数化钉（§12.2 预留 enablement token 的落位形态）：同一条
 * 六轴含 prismatic 链在 R1Locked 下仍范围外（v0.49 前历史口径——阶段值
 * 保留可复验），R2Enabled 下放开；4/5 轴在两个 stage 下同判范围外
 * （禁止项不随参数化翻转）。
 */
TEST(MdlImport, HybridChainStage_Parameterization_R1KeepsBlocked_R2Unlocks_WP13T19)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-12-S1"},
                  std::vector<std::string>{"AT-36"});

    using sdurws::ird::modeling::ChainCapabilityKind;
    using sdurws::ird::modeling::HybridChainStage;
    using sdurws::ird::modeling::judgeChainCapability;
    // 六轴含 prismatic（5R＋1P）。
    std::vector<sdurws::ird::modeling::JointType> hybrid(
        5, sdurws::ird::modeling::JointType::Revolute);
    hybrid.push_back(sdurws::ird::modeling::JointType::Prismatic);
    const auto r1 = judgeChainCapability(hybrid, HybridChainStage::R1Locked);
    EXPECT_EQ(r1.kind, ChainCapabilityKind::BeyondTemplateRange)
        << "R1Locked：prismatic 一票范围外（历史口径）";
    EXPECT_NE(r1.reason.find("R1"), std::string::npos) << "reason 如实区分 R1 成因";
    const auto r2 = judgeChainCapability(hybrid, HybridChainStage::R2Enabled);
    EXPECT_EQ(r2.kind, ChainCapabilityKind::FullTemplateRange)
        << "R2Enabled：MDL-12-S1 放开";
    EXPECT_EQ(r2.movableAxes, r1.movableAxes) << "轴数事实与 stage 无关";
    EXPECT_TRUE(r2.containsPrismatic);
    // 4/5 轴反例在两个 stage 下同判（禁止项）。
    const std::vector<sdurws::ird::modeling::JointType> fiveAxis(
        5, sdurws::ird::modeling::JointType::Revolute);
    EXPECT_EQ(judgeChainCapability(fiveAxis, HybridChainStage::R1Locked).kind,
              ChainCapabilityKind::BeyondTemplateRange);
    EXPECT_EQ(judgeChainCapability(fiveAxis, HybridChainStage::R2Enabled).kind,
              ChainCapabilityKind::BeyondTemplateRange)
        << "4/5 轴不因 R2Enabled 放开（REQUIREMENTS MDL-12-S1）";
    // 全旋转六轴在两个 stage 下同判全能力（stage 只作用于含 prismatic 族）。
    const std::vector<sdurws::ird::modeling::JointType> sixRevolute(
        6, sdurws::ird::modeling::JointType::Revolute);
    EXPECT_EQ(judgeChainCapability(sixRevolute, HybridChainStage::R1Locked).kind,
              ChainCapabilityKind::FullTemplateRange);
    EXPECT_EQ(judgeChainCapability(sixRevolute, HybridChainStage::R2Enabled).kind,
              ChainCapabilityKind::FullTemplateRange);
}

/**
 * 多可动分支显式选链（acceptance 4 维度一——"辅助分支转场景/环境候选
 * 或忽略项、不构成拒绝"）：选择含 prismatic 的分支→主链能力按所选链
 * 判定（prismatic 保留）＋选中/辅助分支报告条目＋辅助分支内容入忽略。
 */
TEST(MdlImport, MultiBranch_WithSelection_AuxCandidatesNotRejected_WP13T05_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-12"},
                  std::vector<std::string>{"AT-17"});

    const ModelImportMapper mapper;
    const char* urdf = R"(<?xml version="1.0"?>
<robot name="mb2">
  <link name="root"/>
  <link name="la"/>
  <link name="lb"/>
  <joint name="ja" type="revolute">
    <parent link="root"/>
    <child link="la"/>
    <axis xyz="0 0 1"/>
    <limit lower="0" upper="1"/>
  </joint>
  <joint name="jb" type="prismatic">
    <parent link="root"/>
    <child link="lb"/>
    <axis xyz="0 0 1"/>
    <limit lower="0" upper="0.4"/>
  </joint>
</robot>
)";
    ImportOptions options;
    options.selectedMainBranch = "lb";  // 用户显式选择（含 prismatic 的分支）
    std::vector<sdurws::ird::core::DiagnosticRecord> diags;
    const ImportOutcome outcome = mapper.mapUrdf(makeSource(urdf), options, diags);
    ASSERT_TRUE(outcome.draft.has_value()) << "显式选链后放行（辅助分支不构成拒绝）";
    ASSERT_EQ(outcome.draft->joints.size(), 1U);
    EXPECT_EQ(outcome.draft->joints[0].type,
              sdurws::ird::modeling::JointType::Prismatic) << "所选链类型保留";
    EXPECT_EQ(outcome.draft->joints[0].localName, "jb");
    // 分支报告：lb＝selected、la＝aux-candidate。
    ASSERT_EQ(outcome.report.branches.size(), 2U);
    bool selectedFound = false;
    bool auxFound = false;
    for (const auto& branch : outcome.report.branches) {
        if (branch.branchRoot == "lb" && branch.disposition == "selected") {
            selectedFound = true;
        }
        if (branch.branchRoot == "la" && branch.disposition == "aux-candidate") {
            auxFound = true;
            EXPECT_NE(branch.reason.find("不构成拒绝"), std::string::npos);
        }
    }
    EXPECT_TRUE(selectedFound);
    EXPECT_TRUE(auxFound);
    // 能力结论按所选链判定：1 可动轴＋prismatic→范围外。
    EXPECT_EQ(outcome.report.chainCapability.kind,
              sdurws::ird::modeling::ChainCapabilityKind::BeyondTemplateRange);
    EXPECT_TRUE(outcome.report.chainCapability.containsPrismatic);
    EXPECT_TRUE(hasDiag(diags, kMdlImportTemplateRange));
    // 辅助分支内容不在草稿（la/ja 不映射——可观察于忽略清单）。
    EXPECT_NE(outcome.draft->links.size(), 0U);
    bool auxLinkInDraft = false;
    for (const auto& link : outcome.draft->links) {
        if (link.localName == "la") { auxLinkInDraft = true; }
    }
    EXPECT_FALSE(auxLinkInDraft) << "辅助分支不进草稿（候选承载）";
    bool auxLinkIgnored = false;
    for (const auto& item : outcome.report.ignored) {
        if (item.element.find("la") != std::string::npos) { auxLinkIgnored = true; }
    }
    EXPECT_TRUE(auxLinkIgnored);
}

/**
 * 选链残留分裂（ImportOptions 语义——每次调用解析一层分裂）：选中分支
 * 内部再分裂→返回新一轮候选清单（向导循环），不静默任选。
 */
TEST(MdlImport, DeepSplit_SelectionResidualBranches_WP13T05_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-12"},
                  std::vector<std::string>{});

    const ModelImportMapper mapper;
    const char* urdf = R"(<?xml version="1.0"?>
<robot name="ds">
  <link name="root"/>
  <link name="la"/>
  <link name="mid"/>
  <link name="lc"/>
  <link name="ld"/>
  <joint name="ja" type="revolute">
    <parent link="root"/>
    <child link="la"/>
    <axis xyz="0 0 1"/>
    <limit lower="0" upper="1"/>
  </joint>
  <joint name="jm" type="revolute">
    <parent link="root"/>
    <child link="mid"/>
    <axis xyz="0 0 1"/>
    <limit lower="0" upper="1"/>
  </joint>
  <joint name="jc" type="revolute">
    <parent link="mid"/>
    <child link="lc"/>
    <axis xyz="0 0 1"/>
    <limit lower="0" upper="1"/>
  </joint>
  <joint name="jd" type="revolute">
    <parent link="mid"/>
    <child link="ld"/>
    <axis xyz="0 0 1"/>
    <limit lower="0" upper="1"/>
  </joint>
</robot>
)";
    ImportOptions options;
    options.selectedMainBranch = "mid";  // 第一层分裂选 mid（其内部再分裂）
    std::vector<sdurws::ird::core::DiagnosticRecord> diags;
    const ImportOutcome outcome = mapper.mapUrdf(makeSource(urdf), options, diags);
    EXPECT_FALSE(outcome.draft.has_value()) << "残留分裂未决——不静默任选";
    ASSERT_TRUE(outcome.error.has_value());
    EXPECT_EQ(outcome.error->code, ImportErrorCode::MultiBranchNeedsSelection);
    bool residualRoots = false;
    for (const auto& param : outcome.error->params) {
        if (param.first == "branch-roots") {
            residualRoots = true;
            EXPECT_EQ(param.second, "lc,ld") << "新一轮候选＝选中分支内部的分裂";
        }
        if (param.first == "split-link") {
            EXPECT_EQ(param.second, "mid");
        }
    }
    EXPECT_TRUE(residualRoots);
}

/**
 * continuous 工程工作范围（acceptance 4/§6.4——确认流导入期登记面）：
 * workingRange 待确认条目＋NotApplicable bounds（类型保留）＋待确认诊断；
 * 确认值不回写权威 bounds（workingRange 字段由确认流落位——本头只登记
 * 未决事实）。
 */
TEST(MdlImport, Continuous_WorkingRangePendingConfirm_WP13T05_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-12"},
                  std::vector<std::string>{});

    const ModelImportMapper mapper;
    const char* urdf = R"(<?xml version="1.0"?>
<robot name="cw">
  <link name="a"/>
  <link name="b"/>
  <joint name="j" type="continuous">
    <parent link="a"/>
    <child link="b"/>
    <axis xyz="0 0 1"/>
  </joint>
</robot>
)";
    std::vector<sdurws::ird::core::DiagnosticRecord> diags;
    const ImportOutcome outcome = mapper.mapUrdf(makeSource(urdf), ImportOptions{}, diags);
    ASSERT_TRUE(outcome.draft.has_value());
    EXPECT_EQ(outcome.draft->joints[0].bounds.state(), FieldState::NotApplicable);
    bool pendingRange = false;
    for (const auto& pending : outcome.report.pendingConfirms) {
        if (pending.kind == "working-range-unconfirmed") {
            pendingRange = true;
            EXPECT_NE(pending.question.find("不回写权威 bounds"), std::string::npos);
        }
    }
    EXPECT_TRUE(pendingRange);
    EXPECT_TRUE(hasDiag(diags, kMdlImportPendingConfirm));
    EXPECT_TRUE(outcome.report.submittable) << "待确认项不阻断（逐条确认流）";
}

// =====================================================================
// ACC5：确定性（NFR-COR-02）与诊断稳定排序
// =====================================================================

/**
 * 确定性（acceptance 5——"同输入字节＋同 options→同输出字节"）：两次
 * 映射 outcome 逐字段相等＋草稿 canonical 编码逐字节一致（Codec 位级
 * 确定性序列化——§4.8）。
 */
TEST(MdlImport, Determinism_SameBytesSameOptionsSameOutput_WP13T05_ACC5)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-02"},
                  std::vector<std::string>{"AT-15"});

    const ModelImportMapper mapper;
    std::vector<sdurws::ird::core::DiagnosticRecord> diagsA;
    std::vector<sdurws::ird::core::DiagnosticRecord> diagsB;
    const ImportOutcome a = mapper.mapUrdf(makeFullSource(), ImportOptions{}, diagsA);
    const ImportOutcome b = mapper.mapUrdf(makeFullSource(), ImportOptions{}, diagsB);
    ASSERT_TRUE(a.draft.has_value());
    ASSERT_TRUE(b.draft.has_value());
    EXPECT_TRUE(a == b) << "同输入字节＋同 options→同输出（ImportOutcome ==）";

    // 字节级：草稿 canonical 编码逐字节一致（§4.8 序列化登记——位级确定）。
    const sdurws::ird::modeling::RobotDesignCodec codec;
    const auto encodedA =
        codec.encode(sdurws::ird::modeling::ObjectVariant(*a.draft),
                     sdurws::ird::modeling::kCurrentFormatVersion);
    const auto encodedB =
        codec.encode(sdurws::ird::modeling::ObjectVariant(*b.draft),
                     sdurws::ird::modeling::kCurrentFormatVersion);
    ASSERT_TRUE(encodedA.ok());
    ASSERT_TRUE(encodedB.ok());
    ASSERT_EQ(encodedA.get().size(), encodedB.get().size());
    EXPECT_TRUE(std::equal(encodedA.get().begin(), encodedA.get().end(),
                           encodedB.get().begin()))
        << "草稿 canonical 编码应逐字节一致";
}

/**
 * 诊断按源文件行序稳定排序（acceptance 5——§9.4.3 确定性行）：产出序与
 * 行序相反的 fixture（关节待确认诊断先产出、行号靠后；缺失网格诊断后
 * 产出、行号靠前）→排序后行序在前者优先。
 */
TEST(MdlImport, Diags_SortedBySourceLineOrder_WP13T05_ACC5)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-02"},
                  std::vector<std::string>{});

    const ModelImportMapper mapper;
    // 缺失网格（第 5 行的 <mesh>）先于缺 axis 关节（第 9 行）出现，但
    // 产出序相反（关节循环先于连杆循环）——排序必须翻转。
    const char* urdf = R"(<?xml version="1.0"?>
<robot name="s">
  <link name="a">
    <visual>
      <geometry><mesh filename="meshes/missing.dae"/></geometry>
    </visual>
  </link>
  <link name="b"/>
  <joint name="j" type="revolute">
    <parent link="a"/>
    <child link="b"/>
    <limit lower="0" upper="1"/>
  </joint>
</robot>
)";
    const ValidatedSource source =
        makeSource(urdf, {missingNode("meshes/missing.dae")},
                   {meshEdge("meshes/missing.dae")});
    std::vector<sdurws::ird::core::DiagnosticRecord> diags;
    const ImportOutcome outcome = mapper.mapUrdf(source, ImportOptions{}, diags);
    ASSERT_TRUE(outcome.draft.has_value());
    ASSERT_EQ(diags.size(), 3U)
        << "本 fixture 恰三条诊断（能力结论＋缺 mesh＋缺 axis）";
    const std::string missingCode(
        sdurws::ird::io::errorCodeToken(sdurws::ird::io::IoErrorCode::ResMissing));
    // 行序断言：第 2 行的能力结论（robot 元素 span）→第 5 行的
    // IO-RES-MISSING→第 9 行的缺 axis 待确认（产出序与行序不同）。
    EXPECT_EQ(diags[0].code,
              std::string(sdurws::ird::modeling::kMdlImportTemplateRange))
        << "单关节链超出模板范围——能力结论诊断定位在 robot 元素行（第 2 行）";
    EXPECT_EQ(diags[1].code, missingCode) << "行号靠前的资源事实应排在前";
    EXPECT_EQ(diags[2].code, std::string(kMdlImportPendingConfirm));
    EXPECT_NE(diags[1].context.find(":5:"), std::string::npos)
        << "诊断上下文应携带源定位（file:line:col）";
    EXPECT_NE(diags[2].context.find(":9:"), std::string::npos)
        << "缺 axis 待确认诊断应定位在关节元素行";
}

/**
 * 惯量参考系旋转（§6.3 惯量行——inertial origin rpy≠0 时张量旋到连杆系，
 * M-2 参考姿态）：R=Rz(90°) 下 diag(1,4,1) → ixx'=4、iyy'=1（x/y 对换）。
 */
TEST(MdlImport, InertialRpy_RotatesInertiaToLinkFrame_WP13T05_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-03"},
                  std::vector<std::string>{});

    const ModelImportMapper mapper;
    const char* urdf = R"(<?xml version="1.0"?>
<robot name="r">
  <link name="base"/>
  <link name="l">
    <inertial>
      <origin xyz="0 0 0" rpy="0 0 1.5707963267948966"/>
      <mass value="1"/>
      <inertia ixx="1" iyy="4" izz="1" ixy="0" ixz="0" iyz="0"/>
    </inertial>
  </link>
  <joint name="j" type="revolute">
    <parent link="base"/>
    <child link="l"/>
    <axis xyz="0 0 1"/>
    <limit lower="0" upper="1"/>
  </joint>
</robot>
)";
    std::vector<sdurws::ird::core::DiagnosticRecord> diags;
    const ImportOutcome outcome = mapper.mapUrdf(makeSource(urdf), ImportOptions{}, diags);
    ASSERT_TRUE(outcome.draft.has_value());
    ASSERT_EQ(outcome.draft->links[1].body.inertia.state(), FieldState::Provided);
    // Rz(90°)·diag(1,4,1)·Rz(90°)ᵀ = diag(4,1,1)（x/y 主项对换——M-2 连杆系
    // 参考姿态的导入面）。解析正交变换下双精度误差 ≤1e-12 相对量级。
    EXPECT_NEAR(outcome.draft->links[1].body.inertia.value().ixx, 4.0, 1e-12);
    EXPECT_NEAR(outcome.draft->links[1].body.inertia.value().iyy, 1.0, 1e-12);
    EXPECT_NEAR(outcome.draft->links[1].body.inertia.value().izz, 1.0, 1e-12);
}

// =====================================================================
// F-587 回归（audit/unit-code-review-20261009）
// =====================================================================

/**
 * 回归（F-587，P1）：axis 分量平方和**上溢**（‖axis‖²≈1e400 超 double 上
 * 限→+Inf）——原实现"normSq > 0"判不住 Inf，x/Inf＝0 后把 (0,0,0) 以
 * Provided 零轴静默落盘（注释声称"溢出/下溢同样落入非有限/零判"仅下
 * 溢方向成立）。修复后与零轴/非有限同口径：Invalid 面保留原串＋
 * ZERO-AXIS 诊断＋不可提交。
 */
TEST(MdlImport, AxisNormSqOverflow_ReportInvalidNotSubmittable_F587)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-03"},
                  std::vector<std::string>{});

    const ModelImportMapper mapper;
    const char* urdf = R"(<?xml version="1.0"?>
<robot name="n">
  <link name="base"/>
  <link name="l"/>
  <joint name="j" type="revolute">
    <parent link="base"/>
    <child link="l"/>
    <axis xyz="1e200 0 0"/>
    <limit lower="0" upper="1"/>
  </joint>
</robot>
)";
    std::vector<sdurws::ird::core::DiagnosticRecord> diags;
    const ImportOutcome outcome = mapper.mapUrdf(makeSource(urdf), ImportOptions{}, diags);
    ASSERT_TRUE(outcome.draft.has_value()) << "上溢轴仅报告——草稿仍产出";
    ASSERT_EQ(outcome.draft->joints[0].axis.state(), FieldState::Invalid);
    EXPECT_EQ(outcome.draft->joints[0].axis.invalidRawInput(), "1e200 0 0")
        << "轴原串保留（NFR-COR-03 不静默转零）";
    EXPECT_TRUE(hasDiag(diags, kMdlImportZeroAxis));
    ASSERT_TRUE(outcome.error.has_value());
    EXPECT_EQ(outcome.error->code, ImportErrorCode::ZeroAxisReported);
    EXPECT_FALSE(outcome.report.submittable) << "量级上溢轴不得静默成零轴提交";
}

// =====================================================================
// WP-13-T17：WorkCell 反向导入通道（MDL-18/AT-33，R2/阶段 D——有损提取
// ＋提取报告；units/modeling.md §6.6/§9.4.9；tasks/foundation/
// WP-13-T17.json acceptance 逐条具名自证）
// =====================================================================

namespace {

/// 在 WorkCell 通道报告中按 kind 查找 unsupported 条目（无则返回 nullptr）。
const ImportUnsupportedItem* findUnsupported(const ImportReport& report,
                                             std::string_view kind,
                                             std::string_view subjectPart = {})
{
    for (const ImportUnsupportedItem& item : report.unsupported) {
        if (item.kind == kind
            && (subjectPart.empty()
                || item.subject.find(subjectPart) != std::string::npos)) {
            return &item;
        }
    }
    return nullptr;
}

}  // namespace

/**
 * AT-33 主体（acceptance 1——从 .wc.xml 创建草稿模型＋提取报告）：两关节
 * WorkCell 的提取语义逐项验证——关节类型（Revolute/Prismatic/Fixed）、
 * 限位（Revolute 源 deg→rad；Prismatic 源 m）、位姿（Pos m＋RPY 源 deg
 * →rad）、轴（RobWork Joint 固定局部 z 轴——报告注明来源）。
 */
TEST(MdlWorkCellImport, Extract_JointTypeLimitPoseAxis_WP13T17_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-18", "MDL-11"},
                  std::vector<std::string>{"AT-33"});

    const ModelImportMapper mapper;
    std::vector<sdurws::ird::core::DiagnosticRecord> diags;
    const ImportOutcome outcome =
        mapper.mapWorkCellXml(makeWorkCellSource(), ImportOptions{}, diags);

    // —— 草稿产出（通道实现——R1 边界已退役）。
    ASSERT_TRUE(outcome.draft.has_value()) << "两关节 WorkCell 应产出草稿";
    EXPECT_FALSE(outcome.error.has_value()) << "合法文档无阻断错误";
    EXPECT_TRUE(outcome.report.submittable) << "缺失网格不置不可提交（V-08）";
    EXPECT_EQ(outcome.draft->authority, sdurws::ird::modeling::AuthorityMode::Explicit);
    EXPECT_EQ(outcome.draft->displayName, "demo cell");

    // —— 链结构：Fixed 基帧＋Revolute＋Prismatic；links==joints+1（I-MDL-1）。
    ASSERT_EQ(outcome.draft->joints.size(), 3U);
    ASSERT_EQ(outcome.draft->links.size(), 4U);
    EXPECT_EQ(outcome.draft->joints[0].localName, "base");
    EXPECT_EQ(outcome.draft->joints[0].type,
              sdurws::ird::modeling::JointType::Fixed);
    EXPECT_EQ(outcome.draft->joints[1].localName, "j1");
    EXPECT_EQ(outcome.draft->joints[1].type,
              sdurws::ird::modeling::JointType::Revolute);
    EXPECT_EQ(outcome.draft->joints[2].localName, "j2");
    EXPECT_EQ(outcome.draft->joints[2].type,
              sdurws::ird::modeling::JointType::Prismatic);

    // —— j1 位姿（Pos m＋RPY 源 deg→rad：yaw 90°→π/2；π/2 的期望值以
    //     自定义常量表达——MSVC 的 M_PI 需 _USE_MATH_DEFINES，不引入）。
    namespace rwmath = rw::math;
    constexpr double kHalfPi = 3.14159265358979323846 / 2.0;
    ASSERT_EQ(outcome.draft->joints[1].origin.state(), FieldState::Provided);
    const rwmath::Transform3D<double> o1 = outcome.draft->joints[1].origin.value();
    EXPECT_DOUBLE_EQ(o1.P()[0], 0.0);
    EXPECT_DOUBLE_EQ(o1.P()[2], 0.2) << "Pos 单位 m——原样提取";
    EXPECT_NEAR(o1.R()(0, 0), std::cos(kHalfPi), 1e-12)
        << "RPY 源单位 deg 已换算 rad（90°→π/2）";
    EXPECT_NEAR(o1.R()(1, 0), std::sin(kHalfPi), 1e-12);

    // —— j1 轴：RobWork Joint 固定局部 z 轴（无源轴元素——报告注明）。
    ASSERT_EQ(outcome.draft->joints[1].axis.state(), FieldState::Provided);
    EXPECT_DOUBLE_EQ(outcome.draft->joints[1].axis.value()[0], 0.0);
    EXPECT_DOUBLE_EQ(outcome.draft->joints[1].axis.value()[1], 0.0);
    EXPECT_DOUBLE_EQ(outcome.draft->joints[1].axis.value()[2], 1.0);

    // —— j1 限位：PosLimit min=-170 max=170（源 deg→rad）。
    constexpr double kDegToRadTest = 3.14159265358979323846 / 180.0;
    ASSERT_EQ(outcome.draft->joints[1].bounds.state(), FieldState::Provided);
    EXPECT_NEAR(outcome.draft->joints[1].bounds.value().first,
                -170.0 * kDegToRadTest, 1e-12)
        << "Revolute 限位源单位 deg 已换算 rad（框架 convFactor 同口径）";
    EXPECT_NEAR(outcome.draft->joints[1].bounds.value().second,
                170.0 * kDegToRadTest, 1e-12);

    // —— j2 限位：Prismatic 源单位 m——原样（0, 0.5）。
    ASSERT_EQ(outcome.draft->joints[2].bounds.state(), FieldState::Provided);
    EXPECT_DOUBLE_EQ(outcome.draft->joints[2].bounds.value().first, 0.0);
    EXPECT_DOUBLE_EQ(outcome.draft->joints[2].bounds.value().second, 0.5)
        << "Prismatic 限位单位 m——不换算";

    // —— 提取报告逐项可观察（四清单面——mapped 覆盖类型/限位/位姿/轴）。
    bool mappedLimit = false;
    bool mappedOrigin = false;
    bool mappedAxis = false;
    for (const auto& m : outcome.report.mapped) {
        if (m.targetField.find(".bounds") != std::string::npos) { mappedLimit = true; }
        if (m.targetField.find(".origin") != std::string::npos) { mappedOrigin = true; }
        if (m.targetField.find(".axis") != std::string::npos) { mappedAxis = true; }
    }
    EXPECT_TRUE(mappedLimit) << "限位映射逐行可观察";
    EXPECT_TRUE(mappedOrigin) << "位姿映射逐行可观察";
    EXPECT_TRUE(mappedAxis) << "轴映射（来源注明）逐行可观察";
}

/**
 * Q 构型提取（acceptance 1——Q 构型→RobotDesign 草稿）：首命名构型对位
 * 可动关节→zeroOffset（权威零位＝源初始姿态）；备选构型未保留逐项列出。
 */
TEST(MdlWorkCellImport, Extract_QConfigToZeroOffsetResidualListed_WP13T17_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-18"},
                  std::vector<std::string>{"AT-33"});

    const ModelImportMapper mapper;
    std::vector<sdurws::ird::core::DiagnosticRecord> diags;
    const ImportOutcome outcome =
        mapper.mapWorkCellXml(makeWorkCellSource(), ImportOptions{}, diags);
    ASSERT_TRUE(outcome.draft.has_value());

    // —— 首配置 Home=[0.5, -0.1] 对位可动关节（j1/j2——Fixed 基帧不占位）。
    EXPECT_DOUBLE_EQ(outcome.draft->joints[1].zeroOffset, 0.5)
        << "Q 首配置→zeroOffset（rad；q_authoritative = q_zeroOffset + q_rw）";
    EXPECT_DOUBLE_EQ(outcome.draft->joints[2].zeroOffset, -0.1)
        << "Prismatic zeroOffset 单位 m——对位序＝可动关节文档序";
    EXPECT_DOUBLE_EQ(outcome.draft->joints[0].zeroOffset, 0.0)
        << "Fixed 基帧不占 Q 位——zeroOffset 维持 0";

    // —— 备选构型 Alt：未保留逐项列出（q-config-residual）。
    const ImportUnsupportedItem* residual =
        findUnsupported(outcome.report, "q-config-residual", "Alt");
    ASSERT_NE(residual, nullptr) << "备选构型必须逐项列出（AT-33 未保留面）";
}

/**
 * 几何/碰撞提取（acceptance 1/2——几何与资源状态表）：Drawable→visual、
 * CollisionModel→collision；Polytope→Mesh 资源 Recorded 登记（存在叶带
 * digest、缺失叶 IO-RES-MISSING 事实＋missing 状态——缺失≠不可行）。
 */
TEST(MdlWorkCellImport, Extract_GeometryResourceRecorded_WP13T17_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-18", "CON-03"},
                  std::vector<std::string>{"AT-33"});

    const ModelImportMapper mapper;
    std::vector<sdurws::ird::core::DiagnosticRecord> diags;
    const ImportOutcome outcome =
        mapper.mapWorkCellXml(makeWorkCellSource(), ImportOptions{}, diags);
    ASSERT_TRUE(outcome.draft.has_value());

    // —— links[i+1]＝链元素 i：j1 连杆（links[2]）visual＝l1.stl（存在叶
    //     ——Recorded 带 digest）。
    ASSERT_TRUE(outcome.draft->links[2].visual.has_value());
    EXPECT_EQ(outcome.draft->links[2].visual->resourceRefId, "meshes/l1.stl");

    // —— 缺失叶：资源清单照常登记（Recorded——§6.7 缺失≠不可行；GeometryRef
    //     亦产——缺失是资源事实非几何错误，草稿可携带）。
    ASSERT_TRUE(outcome.draft->links[3].collision.has_value())
        << "缺失网格仍产 GeometryRef（资源事实经 manifest/报告承载）";
    bool missingRegistered = false;
    for (const sdurws::ird::modeling::ResourceRef& ref : outcome.draft->resourceManifest) {
        if (ref.resourceId == "meshes/missing.stl") {
            missingRegistered = true;
            EXPECT_EQ(ref.state, sdurws::ird::modeling::ResourceState::Recorded);
        }
    }
    EXPECT_TRUE(missingRegistered) << "缺失网格仍登记 Recorded（V-08 草稿可携带）";

    // —— 资源状态表：两行（recorded/missing 各一）＋IO-RES-MISSING 诊断。
    ASSERT_EQ(outcome.report.resources.size(), 2U);
    EXPECT_EQ(outcome.report.resources[0].state, "recorded");
    EXPECT_FALSE(outcome.report.resources[0].digestHex.empty());
    EXPECT_EQ(outcome.report.resources[1].state, "missing");
    EXPECT_TRUE(outcome.report.resources[1].digestHex.empty());
    bool missingDiag = false;
    for (const auto& d : diags) {
        if (d.code == sdurws::ird::io::errorCodeToken(
                          sdurws::ird::io::IoErrorCode::ResMissing)) {
            missingDiag = true;
        }
    }
    EXPECT_TRUE(missingDiag) << "缺失叶事实诊断（IO-RES-MISSING——不置阻断）";
}

/**
 * 碰撞设置/场景/备选构型未保留面＋有损报告逐项列出断言（acceptance 2
 * ——AT-33"不可表达/未保留内容逐项列出"字面：CollisionSetup 外部引用、
 * WorkCell 场景 Frame/Drawable、VelLimit、Camera 忽略面）。
 */
TEST(MdlWorkCellImport, Extract_LossyReportItemsListed_WP13T17_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-18", "MDL-03"},
                  std::vector<std::string>{"AT-33"});

    const ModelImportMapper mapper;
    std::vector<sdurws::ird::core::DiagnosticRecord> diags;
    const ImportOutcome outcome =
        mapper.mapWorkCellXml(makeWorkCellSource(), ImportOptions{}, diags);
    ASSERT_TRUE(outcome.draft.has_value());

    // —— 碰撞设置（外部文件引用——排除对未提取，未保留逐项列出）。
    ASSERT_NE(findUnsupported(outcome.report, "collision-setup-external"),
              nullptr) << "CollisionSetup 必须逐项列出（AT-33）";
    // —— 设备基帧安装（MDL-22 默认地面——未保留）。
    ASSERT_NE(findUnsupported(outcome.report, "base-placement"), nullptr);
    // —— VelLimit/AccLimit（F01/{1,1}：已映射 maxVelocity/maxAcceleration
    //     ——映射行可观察；旧"未保留"面随 schema 落位消亡，此断言随之
    //     改钉映射面）。
    {
        bool velMapped = false;
        for (const auto& m : outcome.report.mapped) {
            if (m.targetField.find(".maxVelocity") != std::string::npos
                && m.sourcePath.find("VelLimit") != std::string::npos) {
                velMapped = true;
            }
        }
        EXPECT_TRUE(velMapped) << "VelLimit 应映射 maxVelocity（F01）";
        // 链序＝[base(Fixed), j1, j2]（WorkCell 帧即关节——§6.6 连杆合成
        // 同源；j1＝joints[1]）。Fixed 基帧无可动限值（NotProvided 直投）。
        ASSERT_EQ(outcome.draft->joints.size(), std::size_t{3});
        ASSERT_EQ(outcome.draft->joints[0].maxVelocity.state(),
                  FieldState::NotProvided);
        ASSERT_EQ(outcome.draft->joints[1].maxVelocity.state(), FieldState::Provided);
        EXPECT_DOUBLE_EQ(outcome.draft->joints[1].maxVelocity.value(), 1.5);
    }
    // —— 场景内容（单设备草稿不携带）。
    ASSERT_NE(findUnsupported(outcome.report, "scene-object", "table"), nullptr);
    ASSERT_NE(findUnsupported(outcome.report, "scene-object", "scene-prop"), nullptr);
    // —— Camera：呈现面——ignored 清单。
    bool cameraIgnored = false;
    for (const auto& ig : outcome.report.ignored) {
        if (ig.element == "Camera") { cameraIgnored = true; }
    }
    EXPECT_TRUE(cameraIgnored) << "Camera 入忽略清单（报告不解释）";
    // —— 物性 NotProvided 默认清单（WorkCell 不承载惯性——逐连杆登记）＋
    //     basePlacement 缺省（MDL-22）为最低集；origin 缺省补零（本 fixture
    //     j2 无 RPY）会追加条目——因此用 GE 断言下界。
    EXPECT_GE(outcome.report.defaults.size(), outcome.draft->links.size() + 1U)
        << "逐连杆物性默认＋basePlacement——不静默（NFR-COR-03）";
}

/**
 * 不可表达清单（acceptance 2——MDL-10/12 同一判定规则）：Spherical 多
 * 自由度/MovableFrame 自由帧/Depend 从动——无草稿＋UnsupportedJointType
 * ＋逐项清单（joint-type/mimic-joint）＋不可提交。
 */
TEST(MdlWorkCellImport, Extract_UnsupportedTypesNoDraft_WP13T17_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-10", "MDL-12"},
                  std::vector<std::string>{"AT-33"});

    const ModelImportMapper mapper;
    std::vector<sdurws::ird::core::DiagnosticRecord> diags;
    const ImportOutcome outcome =
        mapper.mapWorkCellXml(makeSource(kWorkCellUnsupported), ImportOptions{}, diags);

    // —— 无草稿（不可表达——不得转 FixedFrame 绕过，M-6）。
    EXPECT_FALSE(outcome.draft.has_value()) << "不可表达关节无草稿产出";
    ASSERT_TRUE(outcome.error.has_value());
    EXPECT_EQ(outcome.error->code, ImportErrorCode::UnsupportedJointType);
    EXPECT_FALSE(outcome.report.submittable);

    // —— 逐项清单：Spherical（joint-type）＋Movable Frame（joint-type）。
    ASSERT_NE(findUnsupported(outcome.report, "joint-type", "s1"), nullptr);
    ASSERT_NE(findUnsupported(outcome.report, "joint-type", "f1"), nullptr);
    // —— Depend 从动＝mimic 同款（识别＋报告＋阻断——类型保留语义本用例
    //     无草稿可挂，仅清单）。
    ASSERT_NE(findUnsupported(outcome.report, "mimic-joint", "d1"), nullptr);
    EXPECT_TRUE(hasDiag(diags, kMdlImportUnsupportedJoint))
        << "MDL-IMPORT-UNSUPPORTED-JOINT 诊断（MDL-12 同一判定）";
}

/**
 * 目标设备选择（acceptance 1——§6.4 同机制）：多 SerialDevice 文件产
 * 分支报告；未选链拒绝（无草稿）；显式选链后提取所选设备。
 */
TEST(MdlWorkCellImport, Extract_MultiDeviceBranchSelection_WP13T17_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-18"},
                  std::vector<std::string>{"AT-33"});

    const ModelImportMapper mapper;
    std::vector<sdurws::ird::core::DiagnosticRecord> diags;

    // —— 未选链：MultiBranchNeedsSelection 无草稿＋分支报告逐项。
    const ImportOutcome unselected =
        mapper.mapWorkCellXml(makeSource(kWorkCellTwoDevices), ImportOptions{}, diags);
    EXPECT_FALSE(unselected.draft.has_value()) << "未选链不产草稿（§6.4 红线）";
    ASSERT_TRUE(unselected.error.has_value());
    EXPECT_EQ(unselected.error->code, ImportErrorCode::MultiBranchNeedsSelection);
    ASSERT_EQ(unselected.report.branches.size(), 2U);
    EXPECT_EQ(unselected.report.branches[0].branchRoot, "arm-a");
    EXPECT_EQ(unselected.report.branches[1].branchRoot, "arm-b");
    EXPECT_TRUE(hasDiag(diags, kMdlImportBranchSelection));

    // —— 显式选链 arm-b：提取该设备（aux 候选不构成拒绝）。
    ImportOptions options;
    options.selectedMainBranch = "arm-b";
    std::vector<sdurws::ird::core::DiagnosticRecord> diagsSelected;
    const ImportOutcome selected =
        mapper.mapWorkCellXml(makeSource(kWorkCellTwoDevices), options, diagsSelected);
    ASSERT_TRUE(selected.draft.has_value());
    EXPECT_EQ(selected.draft->joints[0].localName, "b1") << "提取所选设备链";
    ASSERT_EQ(selected.report.branches.size(), 2U);
    EXPECT_EQ(selected.report.branches[0].disposition, "aux-candidate");
    EXPECT_EQ(selected.report.branches[1].disposition, "selected");

    // —— 选链不存在：调用方契约违约（SourceInconsistent）。
    options.selectedMainBranch = "no-such-device";
    std::vector<sdurws::ird::core::DiagnosticRecord> diagsBad;
    const ImportOutcome bad =
        mapper.mapWorkCellXml(makeSource(kWorkCellTwoDevices), options, diagsBad);
    ASSERT_TRUE(bad.error.has_value());
    EXPECT_EQ(bad.error->code, ImportErrorCode::SourceInconsistent);
    EXPECT_FALSE(bad.draft.has_value());
}

/**
 * 草稿不产生修订（acceptance 3——PA-1 权威唯一：不经确认不产生修订，
 * 不经 project/**）：导入产物为纯值返回（无落盘/无命令路径）；对象身份
 * 全部为确定性临时句柄（同输入两次调用逐字节相等）；草稿通过值模型
 * 不变量核查（可提交草稿形态）。
 */
TEST(MdlWorkCellImport, DraftIsPureValueNoRevision_WP13T17_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-18"},
                  std::vector<std::string>{"AT-33"});

    const ModelImportMapper mapper;
    std::vector<sdurws::ird::core::DiagnosticRecord> diagsFirst;
    std::vector<sdurws::ird::core::DiagnosticRecord> diagsSecond;
    const ImportOutcome first =
        mapper.mapWorkCellXml(makeWorkCellSource(), ImportOptions{}, diagsFirst);
    const ImportOutcome second =
        mapper.mapWorkCellXml(makeWorkCellSource(), ImportOptions{}, diagsSecond);

    // —— 确定性（同输入→同输出）：草稿逐字段相等（含临时句柄同源——
    //     §6.7"tmp-<序号> 不入库"；真实 ObjectId 归命令 prepare 分配，
    //     PA-1——映射期无修订产生面）。
    ASSERT_TRUE(first.draft.has_value());
    ASSERT_TRUE(second.draft.has_value());
    EXPECT_EQ(first.report, second.report);
    for (std::size_t i = 0; i < first.draft->joints.size(); ++i) {
        EXPECT_EQ(first.draft->joints[i].objectId, second.draft->joints[i].objectId)
            << "确定性临时句柄——同输入同句柄（非随机）";
    }

    // —— 值模型合法性：可提交草稿通过 I-MDL-1~10 全量核查（不变量满足
    //     ＝草稿形态自证；修订/落盘语义不在值模型承载面——ImportOutcome
    //     为纯值聚合，无任何修订/存储句柄字段）。
    const auto violations = sdurws::ird::modeling::checkInvariants(*first.draft);
    EXPECT_TRUE(violations.empty()) << "可提交草稿应通过全量不变量核查";
}

/**
 * 主链能力结论＋诊断稳定排序（acceptance 1——MDL-12 同一判定复用：
 * judgeChainCapability 单一实现；诊断按源行序稳定排序——NFR-COR-02）。
 */
TEST(MdlWorkCellImport, CapabilityAndDiagOrder_WP13T17_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-12", "NFR-COR-02"},
                  std::vector<std::string>{"AT-33"});

    const ModelImportMapper mapper;
    std::vector<sdurws::ird::core::DiagnosticRecord> diags;
    const ImportOutcome outcome =
        mapper.mapWorkCellXml(makeWorkCellSource(), ImportOptions{}, diags);
    ASSERT_TRUE(outcome.draft.has_value());

    // —— 能力结论：1 revolute＋1 prismatic→BeyondTemplateRange（含
    //     prismatic——类型保留不降级，V12-01；R1 范围口径）。
    EXPECT_EQ(outcome.report.chainCapability.kind,
              sdurws::ird::modeling::ChainCapabilityKind::BeyondTemplateRange);
    EXPECT_EQ(outcome.report.chainCapability.movableAxes, 2U);
    EXPECT_TRUE(outcome.report.chainCapability.containsPrismatic);
    EXPECT_TRUE(hasDiag(diags, kMdlImportTemplateRange));

    // —— 诊断稳定排序（NFR-COR-02：按（行，列）非降序）。
    for (std::size_t i = 1; i < diags.size(); ++i) {
        const auto lineOf = [](const std::string& context) {
            // context 形如 "workcell-import@relFile:line:col"——解析行号。
            const std::size_t colon1 = context.rfind(':');
            const std::size_t colon2 = context.rfind(':', colon1 - 1);
            return std::stoi(context.substr(colon2 + 1));
        };
        EXPECT_LE(lineOf(diags[i - 1].context), lineOf(diags[i].context))
            << "诊断按源行序稳定排序";
    }
}

/**
 * 经接口消费的公共面钉扎（契约测试面——IModelImportMapper::mapWorkCellXml
 * 接口与 WorkCellReverseImport 提取引擎同源）：接口调用与直接调用引擎的
 * 产出逐字段相等（单一实现——无第二份提取逻辑）。
 */
TEST(MdlWorkCellImport, InterfaceDelegatesToEngine_WP13T17_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-18"},
                  std::vector<std::string>{"AT-33"});

    const ModelImportMapper mapper;
    std::vector<sdurws::ird::core::DiagnosticRecord> diagsViaInterface;
    std::vector<sdurws::ird::core::DiagnosticRecord> diagsViaEngine;
    const ImportOutcome viaInterface =
        mapper.mapWorkCellXml(makeWorkCellSource(), ImportOptions{}, diagsViaInterface);
    const ImportOutcome viaEngine =
        sdurws::ird::modeling::extractWorkCellDevice(makeWorkCellSource(),
                                                     ImportOptions{},
                                                     diagsViaEngine);
    EXPECT_EQ(viaInterface, viaEngine) << "接口＝引擎委托（单一实现）";
    EXPECT_EQ(diagsViaInterface, diagsViaEngine) << "诊断产出同源";
}
