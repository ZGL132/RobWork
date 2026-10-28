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

/// F-800 复现（缺陷修复批A FIX-T1——audit/round2-20261010 一轮条目 #24）：
/// 六轴重置前 defaultTcp 已设（指向工具 T 的某 tcp 键）→重置清空 toolRefs
/// 时 defaultTcp 若不同拍清除，即残留"指向已不在引用表中的工具"的悬挂
/// 引用（I-MDL-9 defaultTcp∈toolRefs 被静默破坏，且悬挂候选可随 prepare
/// 入库）——期望与 toolRefs/sceneRefs/poseSetRef 同进退同拍清除。
TEST(StructureEditTest, ResetToSixAxis_ClearsDefaultTcp_WithChainRefs)
{
    ModelingWorkingSet ws = makeSeedWorkset();
    // defaultTcp 指向工具 T（工具本体在部件视图保留，此处只造引用面事实
    // ——I-MDL-9 的不变量对象是根对象内的引用表一致性）。
    const core::ObjectId toolOid = core::ObjectId::generate();
    ws.design.toolRefs.push_back(toolOid);
    ws.design.defaultTcp = TcpRef{toolOid, std::string("tcp-1")};

    const std::optional<StructureEditError> err = resetToSixAxis(ws);
    ASSERT_FALSE(err.has_value());
    ASSERT_EQ(ws.design.joints.size(), 6u);
    EXPECT_TRUE(ws.design.toolRefs.empty());
    // F-800 修复面：defaultTcp 随链引用清空——悬挂引用不可入库。
    EXPECT_FALSE(ws.design.defaultTcp.has_value());
}

}  // namespace sdurws::ird::modeling::test
namespace sdurws::ird::modeling::test {
/** @brief F10：删后新增不复用历史ID，add/remove/add始终满足全局身份不变量。 */
TEST(StructureEditTest, RepeatedInsertAfterDeletionPreservesUniqueIdentity)
{
    auto ws=makeSeedWorkset();
    ASSERT_FALSE(addJointAt(ws,0));
    const auto allocated=ws.design.joints[1].objectId;
    ASSERT_FALSE(removeJointAt(ws,1));
    ASSERT_FALSE(addJointAt(ws,0));
    EXPECT_NE(ws.design.joints[1].objectId,allocated);
    EXPECT_TRUE(checkInvariants(ws.design).empty());
}
/** @brief F06：以对象绑定置换命名位姿和逐关节传动值，来源一起随动。 */
TEST(StructureEditTest, ReorderPermutesNamedPosesAndJointValues)
{
    auto ws=makeSeedWorkset();
    PoseSet poses;poses.objectId=core::ObjectId::generate();
    poses.entries={{"homeConfiguration",{.1,.2},""},{"saved",{.3,.4},""}};
    ws.poseSetObject=poses;ws.design.poseSetRef=poses.objectId;
    DrivetrainDesign drive;drive.objectId=core::ObjectId::generate();
    const auto prov=core::ValueProvenance::make(core::ProvenanceKind::UserProvided);
    drive.ratioPerJoint={core::SourcedValue<double>::provided(11.,prov),core::SourcedValue<double>::provided(22.,prov)};
    drive.frictionPerJoint.resize(2);drive.frictionPerJoint[0].bias=core::SourcedValue<double>::provided(3.,prov);
    drive.torqueLimitsPerJoint.resize(2);drive.torqueLimitsPerJoint[0].rated=core::SourcedValue<double>::provided(5.,prov);
    drive.coupling=CouplingDesign{2,2,{1.,2.,3.,4.},0,1,1.};
    ws.drivetrainObject=drive;ws.design.drivetrainRef=drive.objectId;
    ASSERT_FALSE(reorderJoint(ws,0,true));
    EXPECT_DOUBLE_EQ(ws.poseSetObject->entries[0].jointConfiguration[1],.1);
    EXPECT_DOUBLE_EQ(ws.poseSetObject->entries[1].jointConfiguration[1],.3);
    EXPECT_EQ(ws.drivetrainObject->ratioPerJoint[1],drive.ratioPerJoint[0]);
    EXPECT_EQ(ws.drivetrainObject->frictionPerJoint[1],drive.frictionPerJoint[0]);
    EXPECT_EQ(ws.drivetrainObject->torqueLimitsPerJoint[1],drive.torqueLimitsPerJoint[0]);
    EXPECT_EQ(ws.drivetrainObject->coupling->c,(std::vector<double>{3.,4.,1.,2.}));
}
}

namespace sdurws::ird::modeling::test {
/** @brief F06：关联维度错误和无法表达的耦合窗口均在副本阶段拒绝，原值逐项不变。 */
TEST(StructureEditTest, InvalidAssociatedValuesRejectReorderAtomically)
{
    auto ws=makeSeedWorkset();PoseSet poses;poses.objectId=core::ObjectId::generate();
    poses.entries={{"bad",{.1},""}};ws.poseSetObject=poses;ws.design.poseSetRef=poses.objectId;
    const auto before=ws;EXPECT_TRUE(reorderJoint(ws,0,true));EXPECT_EQ(ws,before);
    ws.poseSetObject.reset();ws.design.poseSetRef.reset();ASSERT_FALSE(addJointAt(ws,1));
    DrivetrainDesign drive;drive.objectId=core::ObjectId::generate();
    drive.coupling=CouplingDesign{2,2,{1,0,0,1},0,1,1};ws.drivetrainObject=drive;ws.design.drivetrainRef=drive.objectId;
    const auto coupled=ws;EXPECT_TRUE(reorderJoint(ws,1,true));EXPECT_EQ(ws,coupled);
}
/** @brief F10：当前跨部件占用也排除，整链重建历史不因再次重建释放。 */
TEST(StructureEditTest, ResetRetainsIdentityHistory)
{
    auto ws=makeSeedWorkset();ASSERT_FALSE(resetToSixAxis(ws));const auto first=ws.design;
    ASSERT_FALSE(resetToSixAxis(ws));
    for(const auto& j:first.joints){for(const auto& current:ws.design.joints){EXPECT_NE(j.objectId,current.objectId);}}
    for(const auto& l:first.links){for(const auto& current:ws.design.links){EXPECT_NE(l.objectId,current.objectId);}}
}
}
