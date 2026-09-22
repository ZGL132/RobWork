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
 *                FixedFrame／4·5 轴与含 prismatic 主链能力结论＋诊断＋
 *                类型保留（V12-01）／continuous 工程工作范围待确认
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
#include <sdurws/ird/runtime/Errors.hpp>            // runtime::Expected（encode 载体）
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯登记

#include <gtest/gtest.h>

#include <algorithm>
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
 * mapWorkCellXml R1 边界（acceptance 1——"后者 R1 返回 NotImplemented
 * 稳定诊断，MDL-18 R2 边界不留桩实现"）：NotImplemented 稳定错误＋无
 * 草稿＋通道引导条目；不产修订不落盘（无草稿即无落位面）。
 */
TEST(MdlImport, MapWorkCellXml_R1BoundaryNoStub_WP13T05_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-18"},
                  std::vector<std::string>{});

    const ModelImportMapper mapper;
    std::vector<sdurws::ird::core::DiagnosticRecord> diags;
    const ImportOutcome outcome = mapper.mapWorkCellXml(makeSource("<WorkCell/>"),
                                                        ImportOptions{}, diags);
    ASSERT_TRUE(outcome.error.has_value());
    EXPECT_EQ(outcome.error->code, ImportErrorCode::NotImplemented);
    EXPECT_FALSE(outcome.draft.has_value()) << "R1 不得产出草稿（不留桩实现）";
    ASSERT_EQ(outcome.report.unsupported.size(), 1U);
    EXPECT_EQ(outcome.report.unsupported[0].kind, "workcell-channel-r2");
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
    bool velocityUnsupported = false;
    bool scaleUnsupported = false;
    bool rosUriUnsupported = false;
    for (const auto& item : report.unsupported) {
        if (item.kind == "transmission") { transmissionUnsupported = true; }
        if (item.kind == "joint-velocity-limit") { velocityUnsupported = true; }
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
    EXPECT_TRUE(velocityUnsupported);
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
 * 含 prismatic 主链（acceptance 4——"含 prismatic 主链→阻断＋诊断；不得
 * 静默降级关节类型（类型保留 V12-01）"）：6 关节五转一移→Beyond（原因
 * 含 prismatic）＋prismatic 类型原样入草稿。
 */
TEST(MdlImport, PrismaticInChain_BeyondRange_TypePreserved_WP13T05_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-12"},
                  std::vector<std::string>{"AT-17"});

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
              sdurws::ird::modeling::ChainCapabilityKind::BeyondTemplateRange);
    EXPECT_TRUE(outcome.report.chainCapability.containsPrismatic);
    EXPECT_NE(outcome.report.chainCapability.reason.find("prismatic"), std::string::npos);
    EXPECT_TRUE(hasDiag(diags, kMdlImportTemplateRange));
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
