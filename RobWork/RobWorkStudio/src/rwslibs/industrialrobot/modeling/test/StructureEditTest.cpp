/**
 * @file   StructureEditTest.cpp
 * @brief  关节链结构编辑原语的单元测试（UI-T47——§5.2 v0.28 四操作词表
 *         的验收面：增/删/重排/六轴重置的正反例＋C4 参数保留＋计数守卫）。
 *
 * 测试锚（对齐契约 tasks/foundation/UI-T47.json acceptance）：
 *   - ACC1 关节新增（选中位插入/尾追加/越界拒绝/计数守卫/I-MDL-1）；
 *   - ACC2 关节删除（连带下游连杆/尾关节形态/零关节拒绝/中间删除＝重算
 *     提示非拒绝）；
 *   - ACC3 重排（相邻交换＋连杆随动/边界拒绝/C4 参数随身份保留）；
 *   - ACC4 六轴重置（整链重建 T-MDL-1/维度二守卫/呈现名与基座布置保留）；
 *   - 拒绝路径工作集字节不变（域内强保证——每拒绝例后比对工作集等值）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/modeling/StructureEdit.hpp>
#include <sdurws/ird/modeling/Template.hpp>  // sixAxisTemplateDefaults/createDraft（测试夹具建链）
#include "../src/DraftIdentity.hpp"  // makeSeedLink（单元内私有头——PropertyEstimationTest 同款先例）

namespace sdurws::ird::modeling::test {

namespace {

/// 夹具：custom-chain 种子工作集（§5.1——1 轴种子，供结构操作有"既有人
/// 权威参数"可保；与模板创建同构的合法基线）。
ModelingWorkingSet makeSeedWorkset()
{
    ModelingWorkingSet ws;
    const core::ValueProvenance provenance =
        core::ValueProvenance::make(core::ProvenanceKind::UserProvided);
    RobotDesign& design = ws.design;
    design.displayName = "测试链";
    design.authority = AuthorityMode::Explicit;

    // 2 关节＋3 连杆（I-MDL-1）——关节带用户权威参数（C4 的"既有参数"面）。
    for (std::size_t i = 0; i < 2; ++i) {
        JointEntry joint;
        joint.objectId = core::ObjectId::generate();
        joint.localName = "j" + std::to_string(i + 1);
        joint.type = JointType::Revolute;
        joint.axis = core::SourcedValue<rw::math::Vector3D<double>>::provided(
            rw::math::Vector3D<double>(0.0, 0.0, 1.0), provenance);
        joint.origin = core::SourcedValue<JointPose>::provided(
            JointPose{}, provenance);
        joint.zeroOffset = 0.25 * static_cast<double>(i + 1);  // 用户值（rad）
        joint.bounds = core::SourcedValue<JointLimits>::provided(
            JointLimits{-1.0, 1.0}, provenance);
        design.joints.push_back(std::move(joint));
    }
    design.links.push_back(
        makeSeedLink("ut/structure/link/0", "base", provenance));
    for (std::size_t i = 0; i < 2; ++i) {
        design.links.push_back(
            makeSeedLink("ut/structure/link/" + std::to_string(i + 1),
                         "l" + std::to_string(i + 1), provenance));
    }
    return ws;
}

/// 工作集字节等值（拒绝路径零副作用的比对面——ModelingWorkingSet::operator==）。
bool sameWorkset(const ModelingWorkingSet& a, const ModelingWorkingSet& b)
{
    return a == b;
}

}  // namespace

// =====================================================================
// ACC1——新增（选中位插入）
// =====================================================================

/// 中位插入：2 轴链 index=0 插入→3 关节 4 连杆；新关节在位（localName
/// 消歧 j3——j1/j2 被占）、新连杆插在 links[1]、下游连杆后移。
TEST(StructureEditTest, AddJointAt_InsertsJointAndLink_AtMiddle)
{
    ModelingWorkingSet ws = makeSeedWorkset();
    const std::size_t jointsBefore = ws.design.joints.size();

    const std::optional<StructureEditError> err = addJointAt(ws, 0);
    ASSERT_FALSE(err.has_value()) << "中位插入被拒：" << err->detail;

    ASSERT_EQ(ws.design.joints.size(), jointsBefore + 1);
    ASSERT_EQ(ws.design.links.size(), jointsBefore + 2);  // I-MDL-1
    // 新关节位于 index=1（joints[0] 之后）——设计默认种子面。
    EXPECT_EQ(ws.design.joints[1].localName, "j3")
        << "消歧命名应跳过已占用的 j1/j2";
    EXPECT_EQ(ws.design.joints[1].type, JointType::Revolute);
    ASSERT_TRUE(ws.design.joints[1].bounds.tryValue().has_value());
    // 链序连接关系：新连杆在 links[1]（下游位）。
    EXPECT_EQ(ws.design.links[1].localName, "l3");
    EXPECT_EQ(ws.design.links[2].localName, "l1") << "原下游连杆应后移";
    // 变更摘要一条（MDL-09 提示语义随记录）。
    ASSERT_EQ(ws.changes.size(), 1u);
    EXPECT_NE(ws.changes.front().summary.find("重算"), std::string::npos);
}

/// 尾追加：index==n 缺省形态。
TEST(StructureEditTest, AddJointAt_AppendsAtEnd)
{
    ModelingWorkingSet ws = makeSeedWorkset();
    ASSERT_FALSE(addJointAt(ws, ws.design.joints.size()).has_value());
    EXPECT_EQ(ws.design.joints.back().localName, "j3");
    EXPECT_EQ(ws.design.links.back().localName, "l3");
}

/// 越界拒绝＋零副作用。
TEST(StructureEditTest, AddJointAt_RejectsOutOfRange_WithoutMutation)
{
    ModelingWorkingSet ws = makeSeedWorkset();
    const ModelingWorkingSet before = ws;
    const std::optional<StructureEditError> err = addJointAt(ws, 3);
    ASSERT_TRUE(err.has_value());
    EXPECT_EQ(err->code, StructureEditErrorCode::IndexOutOfRange);
    EXPECT_TRUE(sameWorkset(ws, before)) << "拒绝路径工作集被改动（UX-03 违约）";
}

// =====================================================================
// ACC2——删除（连带下游连杆）
// =====================================================================

/// 中间删除：删 joints[0] 连带 links[1]；上游 base/l2 保留（§5.2 v0.28 ②
/// ——中间删除＝重算提示非拒绝）。
TEST(StructureEditTest, RemoveJointAt_RemovesDownstreamLink)
{
    ModelingWorkingSet ws = makeSeedWorkset();
    const std::string removedLink = ws.design.links[1].localName;

    const std::optional<StructureEditError> err = removeJointAt(ws, 0);
    ASSERT_FALSE(err.has_value());
    ASSERT_EQ(ws.design.joints.size(), 1u);
    ASSERT_EQ(ws.design.links.size(), 2u);  // I-MDL-1
    EXPECT_NE(ws.design.links[0].localName, removedLink)
        << "被删连杆仍在链上（连带移除失守）";
    EXPECT_EQ(ws.design.links.back().localName, "l2") << "下游连杆不应被连带";
}

/// 零关节拒绝（I-MDL-1）＋零副作用。
TEST(StructureEditTest, RemoveJointAt_RejectsDeletingLastJoint)
{
    ModelingWorkingSet ws = makeSeedWorkset();
    ASSERT_FALSE(removeJointAt(ws, 0).has_value());
    const ModelingWorkingSet before = ws;
    const std::optional<StructureEditError> err = removeJointAt(ws, 0);
    ASSERT_TRUE(err.has_value());
    EXPECT_EQ(err->code, StructureEditErrorCode::WouldDeleteLastJoint);
    EXPECT_TRUE(sameWorkset(ws, before));
}

/// 越界拒绝。
TEST(StructureEditTest, RemoveJointAt_RejectsOutOfRange)
{
    ModelingWorkingSet ws = makeSeedWorkset();
    const ModelingWorkingSet before = ws;
    const std::optional<StructureEditError> err = removeJointAt(ws, 2);
    ASSERT_TRUE(err.has_value());
    EXPECT_EQ(err->code, StructureEditErrorCode::IndexOutOfRange);
    EXPECT_TRUE(sameWorkset(ws, before));
}

// =====================================================================
// ACC3——重排（相邻交换＋C4 参数随身份保留）
// =====================================================================

/// 下移：joints[0]↔joints[1] 交换＋links[1]↔links[2] 随动；**C4 核心
/// 断言**：交换后各 ObjectId 的权威参数（zeroOffset/bounds）随身份走
/// ——数组序变了、身份与参数的绑定不变。
TEST(StructureEditTest, ReorderJoint_SwapsWithLink_FollowParamsByIdentity)
{
    ModelingWorkingSet ws = makeSeedWorkset();
    const core::ObjectId j1 = ws.design.joints[0].objectId;
    const core::ObjectId l1 = ws.design.links[1].objectId;
    const double j1Offset = ws.design.joints[0].zeroOffset;  // 0.25（用户值）

    const std::optional<StructureEditError> err = reorderJoint(ws, 0, /*down=*/true);
    ASSERT_FALSE(err.has_value());
    // 关节序交换：j1 现在在 index=1；其参数随身份走（C4）。
    EXPECT_EQ(ws.design.joints[1].objectId, j1);
    EXPECT_DOUBLE_EQ(ws.design.joints[1].zeroOffset, j1Offset)
        << "重排后权威参数未随身份保留（C4 失守）";
    EXPECT_EQ(ws.design.links[2].objectId, l1) << "连杆未随关节交换";
    // dhDerived 不冒充权威（D-MDL-5）：Explicit 态 dhDerived 保持缺省。
    EXPECT_FALSE(ws.design.joints[1].dhDerived.has_value());
}

/// 边界拒绝：首上移/尾下移＋零副作用。
TEST(StructureEditTest, ReorderJoint_RejectsAtBoundary_WithoutMutation)
{
    ModelingWorkingSet ws = makeSeedWorkset();
    const ModelingWorkingSet before = ws;
    const auto up = reorderJoint(ws, 0, /*down=*/false);
    ASSERT_TRUE(up.has_value());
    EXPECT_EQ(up->code, StructureEditErrorCode::ReorderAtBoundary);
    EXPECT_TRUE(sameWorkset(ws, before));
    const auto down = reorderJoint(ws, 1, /*down=*/true);
    ASSERT_TRUE(down.has_value());
    EXPECT_EQ(down->code, StructureEditErrorCode::ReorderAtBoundary);
    EXPECT_TRUE(sameWorkset(ws, before));
}

// =====================================================================
// ACC4——六轴重置（整链重建）
// =====================================================================

/// 重置：2 轴链→6 轴 T-MDL-1 表值；呈现名/权威模式/基座布置保留；维度
/// 二判定通过（FullTemplateRange——出口守卫隐式验证）。
TEST(StructureEditTest, ResetToSixAxis_RebuildsChain_PreservesNonChainFace)
{
    ModelingWorkingSet ws = makeSeedWorkset();
    ws.design.basePlacement.preset = runtime::InstallationPresetToken::Wall;

    const std::optional<StructureEditError> err = resetToSixAxis(ws);
    ASSERT_FALSE(err.has_value());
    ASSERT_EQ(ws.design.joints.size(), 6u);
    ASSERT_EQ(ws.design.links.size(), 7u);
    // T-MDL-1 表值落位（J1 行——Revolute＋Z 轴＋±π）。
    EXPECT_EQ(ws.design.joints[0].type, JointType::Revolute);
    ASSERT_TRUE(ws.design.joints[0].bounds.tryValue().has_value());
    EXPECT_DOUBLE_EQ(ws.design.joints[0].bounds.tryValue()->first, -3.14159265358979323846);
    // 非链面保留：呈现名/权威/基座。
    EXPECT_EQ(ws.design.displayName, "测试链");
    EXPECT_EQ(ws.design.authority, AuthorityMode::Explicit);
    EXPECT_EQ(ws.design.basePlacement.preset, runtime::InstallationPresetToken::Wall);
    // 有损语义的显式形态：旧链引用表清空（工具/场景引用不悬挂）。
    EXPECT_TRUE(ws.design.toolRefs.empty());
    EXPECT_TRUE(ws.design.sceneRefs.empty());
}

}  // namespace sdurws::ird::modeling::test