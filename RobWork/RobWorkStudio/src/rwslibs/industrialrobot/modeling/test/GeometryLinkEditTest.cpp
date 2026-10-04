/**
 * @file   GeometryLinkEditTest.cpp
 * @brief  几何资源引用编辑原语的单元测试（UI-T48——§6.7 Recorded 登记面
 *         ＋引用挂换摘＋拒绝路径；io 交互经替身缝——原语零 I/O 的结构
 *         验证面）。
 *
 * 测试锚（对齐契约 tasks/foundation/UI-T48.json acceptance 1~3）：
 *   - ACC1 资源选择器（登记 Recorded＋摘要身份＋挂接/替换语义）；
 *   - ACC2 引用编辑三态（挂/换/摘＋摘除幂等＋清单不级联删除）；
 *   - ACC3 拒绝路径（io 拒绝/格式族外/越界/非有限值——工作集字节不变，
 *     ERR-01）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/modeling/GeometryLinkEdit.hpp>
#include <sdurws/ird/modeling/Template.hpp>

#include "../src/DraftIdentity.hpp"  // makeSeedLink（单元内私有头——PropertyEstimationTest 同款先例）

namespace sdurws::ird::modeling::test {

namespace {

/// 夹具：1 关节＋2 连杆的种子工作集（无几何引用基线）。
ModelingWorkingSet makeSeedWorkset()
{
    ModelingWorkingSet ws;
    const core::ValueProvenance provenance =
        core::ValueProvenance::make(core::ProvenanceKind::UserProvided);
    ws.design.displayName = "几何测试链";
    JointEntry joint;
    joint.objectId = core::ObjectId::generate();
    joint.localName = "j1";
    joint.type = JointType::Revolute;
    joint.axis = core::SourcedValue<rw::math::Vector3D<double>>::provided(
        rw::math::Vector3D<double>(0.0, 0.0, 1.0), provenance);
    joint.origin = core::SourcedValue<JointPose>::provided(JointPose{}, provenance);
    joint.bounds = core::SourcedValue<JointLimits>::provided(
        JointLimits{-1.0, 1.0}, provenance);
    ws.design.joints.push_back(std::move(joint));
    ws.design.links.push_back(
        makeSeedLink("ut/geo/link/0", "base", provenance));
    ws.design.links.push_back(
        makeSeedLink("ut/geo/link/1", "l1", provenance));
    return ws;
}

/// 探测缝替身（脚本化应答——按路径返回摘要/格式族或错误文本）。
struct FakeProbe {
    core::Digest256 digest{};        ///< 应答摘要（同路径同摘要——确定性面）
    bool meshFamily = true;          ///< 应答格式族判定
    std::string errorText;           ///< 非空＝应答失败（io 拒绝面）
    int calls = 0;                   ///< 调用计数（零 I/O 契约的旁证）

    ResourceProbeFn fn()
    {
        return [this](const std::string& path, std::string& errText)
            -> std::optional<ResourceProbeResult> {
            ++calls;
            if (!errorText.empty()) {
                errText = errorText;
                return std::nullopt;
            }
            // 摘要由路径字节派生（确定性——同路径同摘要）。
            core::ContentDigester d;
            const std::string seed = "probe:" + path;
            d.update(seed.data(), seed.size());
            ResourceProbeResult r;
            r.contentDigest = d.finalize();
            r.absPath = path;
            r.isMeshFamily = meshFamily;
            return r;
        };
    }
};

}  // namespace

// =====================================================================
// ACC1——资源选择器（登记＋挂接）
// =====================================================================

/// 挂接空槽：清单 +1（Recorded＋externalRecord 必填）＋槽引用落位＋
/// 摘要一条（MDL-09"重算"提示随记录）。
TEST(GeometryLinkEditTest, AttachExternalGeometry_RegistersAndAttaches)
{
    ModelingWorkingSet ws = makeSeedWorkset();
    FakeProbe probe;

    const std::optional<GeometryLinkError> err = attachExternalGeometry(
        ws, 1, GeometrySlot::Visual, "D:/assets/link1.stl", probe.fn());
    ASSERT_FALSE(err.has_value()) << "挂接被拒：" << err->detail;

    // 清单登记面（CON-03——只存引用与摘要；本体不入对象字节）。
    ASSERT_EQ(ws.design.resourceManifest.size(), 1u);
    const ResourceRef& entry = ws.design.resourceManifest.front();
    EXPECT_EQ(entry.resourceId, "res-1");
    EXPECT_EQ(entry.state, ResourceState::Recorded);
    ASSERT_TRUE(entry.externalRecord.has_value());
    EXPECT_EQ(entry.externalRecord->absPath, "D:/assets/link1.stl");
    EXPECT_TRUE(entry.contentDigest == entry.externalRecord->recordedDigest)
        << "登记摘要与身份摘要不一致（§4.3 digest＝身份要素）";

    // 槽挂接面（Mesh 类别＋引用键指向清单）。
    ASSERT_TRUE(ws.design.links[1].visual.has_value());
    EXPECT_EQ(ws.design.links[1].visual->resourceRefId, "res-1");
    EXPECT_EQ(ws.design.links[1].visual->kind, GeometryKind::Mesh);
    // 摘要记录（MDL-09 提示语义随记录）。
    ASSERT_EQ(ws.changes.size(), 1u);
    EXPECT_NE(ws.changes.front().summary.find("重算"), std::string::npos);
}

/// 挂接已占用槽＝替换语义（新条目登记＋旧引用被覆盖；旧清单条目保留
/// ——共享引用不级联删除）。
TEST(GeometryLinkEditTest, AttachExternalGeometry_ReplacesOccupiedSlot)
{
    ModelingWorkingSet ws = makeSeedWorkset();
    FakeProbe probe;
    ASSERT_FALSE(attachExternalGeometry(ws, 1, GeometrySlot::Visual,
                                        "D:/a/old.stl", probe.fn())
                     .has_value());
    const std::size_t manifestBefore = ws.design.resourceManifest.size();

    ASSERT_FALSE(attachExternalGeometry(ws, 1, GeometrySlot::Visual,
                                        "D:/a/new.stl", probe.fn())
                     .has_value());
    // 新条目登记（res-2）＋旧条目保留在清单。
    ASSERT_EQ(ws.design.resourceManifest.size(), manifestBefore + 1);
    EXPECT_EQ(ws.design.resourceManifest.back().resourceId, "res-2");
    EXPECT_EQ(ws.design.resourceManifest.front().resourceId, "res-1")
        << "旧清单条目被级联删除（共享引用语义失守）";
    // 槽引用指向新条目。
    ASSERT_TRUE(ws.design.links[1].visual.has_value());
    EXPECT_EQ(ws.design.links[1].visual->resourceRefId, "res-2");
}

// =====================================================================
// ACC2——引用编辑三态（摘除＋幂等）
// =====================================================================

TEST(GeometryLinkEditTest, DetachGeometry_RemovesRef_KeepsManifestEntry)
{
    ModelingWorkingSet ws = makeSeedWorkset();
    FakeProbe probe;
    ASSERT_FALSE(attachExternalGeometry(ws, 1, GeometrySlot::Collision,
                                        "D:/a/col.stl", probe.fn())
                     .has_value());

    const std::optional<GeometryLinkError> err =
        detachGeometry(ws, 1, GeometrySlot::Collision);
    ASSERT_FALSE(err.has_value());
    EXPECT_FALSE(ws.design.links[1].collision.has_value()) << "引用未摘除";
    EXPECT_EQ(ws.design.resourceManifest.size(), 1u)
        << "清单条目被级联删除（防误伤共享资源语义失守）";
}

/// 摘除空槽＝幂等恒等（零错误零摘要）。
TEST(GeometryLinkEditTest, DetachGeometry_OnEmptySlot_IsIdempotent)
{
    ModelingWorkingSet ws = makeSeedWorkset();
    ASSERT_FALSE(detachGeometry(ws, 1, GeometrySlot::Visual).has_value());
    EXPECT_TRUE(ws.changes.empty()) << "空摘产生了变更摘要（幻影脏化）";
}

/// 局部变换编辑（m/rad 直投＋未挂引用拒绝）。
TEST(GeometryLinkEditTest, EditGeometryLocalTransform_WritesAndRejectsUnset)
{
    ModelingWorkingSet ws = makeSeedWorkset();
    FakeProbe probe;
    ASSERT_FALSE(attachExternalGeometry(ws, 1, GeometrySlot::Visual,
                                        "D:/a/v.stl", probe.fn())
                     .has_value());

    const std::optional<GeometryLinkError> err = editGeometryLocalTransform(
        ws, 1, GeometrySlot::Visual, 0.1, 0.2, 0.3, 0.0, 1.5707963267948966, 0.0);
    ASSERT_FALSE(err.has_value());
    const GeometryRef& ref = *ws.design.links[1].visual;
    EXPECT_DOUBLE_EQ(ref.localTransform.P()[0], 0.1);
    EXPECT_NEAR(ref.localTransform.R()(2, 0), -1.0, 1e-12) << "pitch=π/2 的 R(2,0) 应为 -1";

    const std::optional<GeometryLinkError> bad = editGeometryLocalTransform(
        ws, 0, GeometrySlot::Visual, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0);
    ASSERT_TRUE(bad.has_value());
    EXPECT_EQ(bad->code, GeometryLinkErrorCode::NoSuchResource)
        << "未挂槽编辑未拒绝";
}

// =====================================================================
// ACC3——拒绝路径（工作集字节不变——ERR-01）
// =====================================================================

TEST(GeometryLinkEditTest, AttachExternalGeometry_RejectPaths_KeepWorksetIntact)
{
    ModelingWorkingSet ws = makeSeedWorkset();
    const ModelingWorkingSet before = ws;

    // ①io 拒绝（预算超限/逃逸路径——替身模拟，detail 直投）。
    FakeProbe ioFail;
    ioFail.errorText = "IO-SEC-BUDGET-FILE limit=10485760 actual=52428800 unit=bytes";
    const auto r1 = attachExternalGeometry(ws, 1, GeometrySlot::Visual,
                                           "D:/a/huge.stl", ioFail.fn());
    ASSERT_TRUE(r1.has_value());
    EXPECT_EQ(r1->code, GeometryLinkErrorCode::IoRejected);
    EXPECT_NE(r1->detail.find("IO-SEC-BUDGET-FILE"), std::string::npos)
        << "io 定位文本未透传（ERR-01 诚实呈现失守）";

    // ②格式族外（io 识别成功但非几何承载——如 URDF/纹理）。
    FakeProbe wrongKind;
    wrongKind.meshFamily = false;
    const auto r2 = attachExternalGeometry(ws, 1, GeometrySlot::Visual,
                                           "D:/a/robot.urdf", wrongKind.fn());
    ASSERT_TRUE(r2.has_value());
    EXPECT_EQ(r2->code, GeometryLinkErrorCode::UnsupportedKind);

    // ③越界下标。
    const auto r3 = attachExternalGeometry(ws, 9, GeometrySlot::Visual,
                                           "D:/a/x.stl", FakeProbe{}.fn());
    ASSERT_TRUE(r3.has_value());
    EXPECT_EQ(r3->code, GeometryLinkErrorCode::IndexOutOfRange);

    // 拒绝路径工作集字节不变（三连拒后与基线等值——UX-03 域内强保证）。
    EXPECT_TRUE(ws == before) << "拒绝路径改动了工作集";
    EXPECT_EQ(ws.design.resourceManifest.size(), 0u);
}

/// 探测缝缺位＝装配违约 fail-fast（P-MDL-8 零直读纪律的结构面）。
TEST(GeometryLinkEditTest, AttachExternalGeometry_NullProbe_FailsFast)
{
    ModelingWorkingSet ws = makeSeedWorkset();
    EXPECT_THROW((void)(attachExternalGeometry(
                     ws, 1, GeometrySlot::Visual, "D:/a/x.stl", nullptr)),
                 std::invalid_argument);
}

// =====================================================================
// UI-T49——视觉→碰撞复制辅助（§5.2 几何生成辅助②；G6 独立性＋G7 复制）
// =====================================================================

/// 复制基本语义：同 resourceRefId＋localTransform/kind 初值随复制＋清单
/// 零改动＋摘要携带来源标记 "collision-copy"（GeometricEstimate 族）。
TEST(GeometryLinkEditTest, CopyVisualToCollision_ReplicatesRefAndRecordsProvenance)
{
    ModelingWorkingSet ws = makeSeedWorkset();
    FakeProbe probe;
    ASSERT_FALSE(attachExternalGeometry(ws, 1, GeometrySlot::Visual,
                                        "D:/a/v.stl", probe.fn())
                     .has_value());
    // 造非恒等初值（复制应整体搬运——非仅引用键）。
    ASSERT_FALSE(editGeometryLocalTransform(ws, 1, GeometrySlot::Visual,
                                            0.1, 0.2, 0.3, 0.0, 0.0, 0.5)
                     .has_value());
    const GeometryRef visualSnapshot = *ws.design.links[1].visual;

    const std::optional<GeometryLinkError> err =
        copyVisualToCollision(ws, 1);
    ASSERT_FALSE(err.has_value());
    ASSERT_TRUE(ws.design.links[1].collision.has_value());
    const GeometryRef& copied = *ws.design.links[1].collision;
    EXPECT_EQ(copied.resourceRefId, visualSnapshot.resourceRefId)
        << "同资源复制失守（复制不得新登清单条目）";
    EXPECT_TRUE(copied.localTransform == visualSnapshot.localTransform)
        << "localTransform 初值未随复制";
    EXPECT_EQ(copied.kind, visualSnapshot.kind) << "kind 未随复制";
    EXPECT_EQ(ws.design.resourceManifest.size(), 1u)
        << "复制产生了清单新条目（应为同资源引用共享）";
    // 摘要留痕：来源标记 methodTag "collision-copy"（GeometryRef schema
    // 无来源字段——标记只在呈现链）。本用例前置两动作各落一条摘要
    // （挂接＋局部变换），复制摘要为末条。
    ASSERT_EQ(ws.changes.size(), 3u);
    EXPECT_EQ(ws.changes.back().subject, "links[1].collision");
    EXPECT_NE(ws.changes.back().summary.find("collision-copy"), std::string::npos)
        << "摘要未携带辅助来源标记";
}

/// visual 未设＝诚实拒绝（ERR-01——非静默产出空引用；工作集字节不变）。
TEST(GeometryLinkEditTest, CopyVisualToCollision_VisualNotSet_Rejected)
{
    ModelingWorkingSet ws = makeSeedWorkset();
    const ModelingWorkingSet before = ws;
    const std::optional<GeometryLinkError> err = copyVisualToCollision(ws, 1);
    ASSERT_TRUE(err.has_value());
    EXPECT_EQ(err->code, GeometryLinkErrorCode::VisualNotSet);
    EXPECT_EQ(geometryLinkErrorCodeToken(err->code), "visual-not-set");
    EXPECT_FALSE(err->detail.empty()) << "诚实拒绝缺原因文本";
    EXPECT_TRUE(before == ws) << "拒绝路径工作集被改动";
}

/// collision 已有引用＝须显式确认覆盖（estimate 覆盖确认同款纪律——
/// 不确认拒绝且工作集不变；确认后覆盖为新引用，旧清单条目保留）。
TEST(GeometryLinkEditTest, CopyVisualToCollision_OccupiedRequiresExplicitConfirm)
{
    ModelingWorkingSet ws = makeSeedWorkset();
    FakeProbe probe;
    ASSERT_FALSE(attachExternalGeometry(ws, 1, GeometrySlot::Visual,
                                        "D:/a/v.stl", probe.fn())
                     .has_value());
    ASSERT_FALSE(attachExternalGeometry(ws, 1, GeometrySlot::Collision,
                                        "D:/a/old.stl", probe.fn())
                     .has_value());
    const std::string oldCollisionRef =
        ws.design.links[1].collision->resourceRefId;

    const std::optional<GeometryLinkError> unconfirmed =
        copyVisualToCollision(ws, 1, false);
    ASSERT_TRUE(unconfirmed.has_value());
    EXPECT_EQ(unconfirmed->code, GeometryLinkErrorCode::CollisionOccupied);
    EXPECT_EQ(ws.design.links[1].collision->resourceRefId, oldCollisionRef)
        << "未确认覆盖已改写 collision（静默覆盖纪律失守）";
    EXPECT_EQ(ws.changes.size(), 2u) << "拒绝路径产生了幻影摘要";

    const std::optional<GeometryLinkError> confirmed =
        copyVisualToCollision(ws, 1, true);
    ASSERT_FALSE(confirmed.has_value());
    EXPECT_EQ(ws.design.links[1].collision->resourceRefId,
              ws.design.links[1].visual->resourceRefId)
        << "确认覆盖后未复制视觉引用";
    EXPECT_EQ(ws.design.resourceManifest.size(), 2u)
        << "被覆盖旧引用的清单条目被级联删除（失引用条目保留语义失守）";
}

/// 两槽独立性（acceptance 1 核心断言）：复制后摘除 visual，collision 仍
/// 持有引用；改 collision 位姿，visual 位姿不动——collision 编辑零 visual
/// 改写。
TEST(GeometryLinkEditTest, CopyVisualToCollision_SlotsIndependentAfterCopy)
{
    ModelingWorkingSet ws = makeSeedWorkset();
    FakeProbe probe;
    ASSERT_FALSE(attachExternalGeometry(ws, 1, GeometrySlot::Visual,
                                        "D:/a/v.stl", probe.fn())
                     .has_value());
    ASSERT_FALSE(copyVisualToCollision(ws, 1).has_value());
    const GeometryRef visualSnapshot = *ws.design.links[1].visual;

    // ①摘除 visual——collision 引用独立存活（槽位摘除只动本槽）。
    ASSERT_FALSE(detachGeometry(ws, 1, GeometrySlot::Visual).has_value());
    EXPECT_FALSE(ws.design.links[1].visual.has_value());
    ASSERT_TRUE(ws.design.links[1].collision.has_value())
        << "摘除 visual 连带清了 collision（两槽独立性失守）";
    EXPECT_EQ(ws.design.resourceManifest.size(), 1u)
        << "清单条目应保留（collision 仍在引用）";

    // ②恢复 visual 并改 collision 位姿——visual 位姿不动。
    ASSERT_FALSE(attachExternalGeometry(ws, 1, GeometrySlot::Visual,
                                        "D:/a/v.stl", probe.fn())
                     .has_value());
    ASSERT_FALSE(editGeometryLocalTransform(ws, 1, GeometrySlot::Collision,
                                            1.0, 0.0, 0.0, 0.0, 0.0, 0.0)
                     .has_value());
    EXPECT_TRUE(ws.design.links[1].visual->localTransform
                == visualSnapshot.localTransform)
        << "collision 位姿编辑改写了 visual（零 visual 改写断言失守）";
}

/// 连杆下标越界＝拒绝（复用挂换摘同款前置面）。
TEST(GeometryLinkEditTest, CopyVisualToCollision_IndexOutOfRange_Rejected)
{
    ModelingWorkingSet ws = makeSeedWorkset();
    const std::optional<GeometryLinkError> err = copyVisualToCollision(ws, 9);
    ASSERT_TRUE(err.has_value());
    EXPECT_EQ(err->code, GeometryLinkErrorCode::IndexOutOfRange);
}

}  // namespace sdurws::ird::modeling::test