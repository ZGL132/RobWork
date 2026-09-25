/**
 * @file   OrientationResolutionTest.cpp
 * @brief  姿态规则解析用例组（ReqOrientationResolution）——五规则解析
 *         成功与 resolution 留痕（契约 WP-14-T06 acceptance 1——V-03/
 *         AT-23）、解析失败可定位与浅校验边界（acceptance 2——§8.1
 *         专项声明）、确定性（NFR-COR-01）。
 *
 * 设计依据：units/requirements.md §5.3（五规则表＋隔离声明）、§8.1
 * （浅校验边界——仅查闭包 objectRefs 元数据）、§9.6（REF-MISSING/
 * POSE-ILLEGAL 两码的解析侧复用）、§10.2 V-03（故障注入矩阵行）。
 */

#include <sdurws/ird/requirements/OrientationResolution.hpp>

#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/project/PersistenceFormat.hpp>
#include <sdurws/ird/requirements/RequirementTypes.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <vector>

using namespace sdurws::ird;
using namespace sdurws::ird::requirements;

/// core 命名空间别名（ObjectId 直写面）。
namespace core = sdurws::ird::core;

namespace {

/// 测试用条目锚（objectId 固定规范文本——断言可精确比对）。
OrientationRuleAnchor makeAnchor()
{
    return OrientationRuleAnchor{
        core::ObjectId::fromCanonical("obj-00000000000000000000000000000a01"),
        "搬运工位-1"};
}

/// 闭包元数据登记（ObjectRef 元数据四字段——**无任何 modeling 对象字节**
/// ：本文件全程不构造 modeling 对象，浅校验边界的结构性自证，§8.1）。
void addRef(CheckContext& ctx, const core::ObjectId& oid, std::string token)
{
    project::ObjectRef ref;
    ref.objectId = oid;
    ref.contentVersion.bytes[0] = static_cast<std::uint8_t>(ctx.closureRefs.size() + 1U);
    ref.objectTypeToken = std::move(token);
    ref.digest256 = std::string(64, '0');  // 占位摘要（本单元从不复核其真实性）
    ctx.closureRefs.push_back(std::move(ref));
}

/// 固定身份（规范文本构造——跨用例可复现）。
core::ObjectId oidOf(const char* canonical)
{
    return core::ObjectId::fromCanonical(canonical);
}

}  // namespace

// =====================================================================
// acceptance 1——五规则解析成功与 resolution 留痕（V-03/AT-23）
// =====================================================================

/**
 * 五规则各建样例解析成功（ACC1——DTB 完成条件"规则解析成功……可定位"
 * 的正例半边）：每规则的解析产出与 resolution 来源逐字段断言——Fixed
 * 留痕参数字面（rad/Z-Y-X）、AlignFrame/AlignGeometryNormal 留痕核对
 * 通过的目标 ObjectId＋特征语义、PointAtTarget 产出单位方向、
 * ToolRollFree 留痕滚转区间（rad）。
 */
TEST(ReqOrientationResolution, FiveRulesResolveWithResolutionProvenance_WP14T06_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-09"}, std::vector<std::string>{"AT-23"});

    // 闭包元数据：一个 robot-design（Frame 对齐目标）＋一个 scene-object
    // （几何法向目标）——仅元数据，无对象字节（浅校验边界自证）。
    const core::ObjectId frameOid = oidOf("obj-00000000000000000000000000000b01");
    const core::ObjectId sceneOid = oidOf("obj-00000000000000000000000000000b02");
    CheckContext closure;
    addRef(closure, frameOid, "robot-design");
    addRef(closure, sceneOid, "scene-object");
    const OrientationRuleAnchor anchor = makeAnchor();

    // ---- Fixed：解析产出＝参数字面（Z-Y-X 欧拉 rad；不归一等效角）。
    OrientationRule fixed;
    fixed.kind = OrientationRuleKind::Fixed;
    fixed.fixedRpy = rw::math::Vector3D<double>(0.1, 0.2, 0.3);  // rad
    {
        const OrientationResolution r = resolveOrientationRule(fixed, anchor, closure);
        ASSERT_TRUE(r.ok);
        EXPECT_EQ(r.kind, OrientationRuleKind::Fixed);
        ASSERT_TRUE(r.referenceRpy.has_value());
        EXPECT_DOUBLE_EQ(r.referenceRpy->operator[](0), 0.1);
        EXPECT_DOUBLE_EQ(r.referenceRpy->operator[](1), 0.2);
        EXPECT_DOUBLE_EQ(r.referenceRpy->operator[](2), 0.3);
        EXPECT_FALSE(r.targetObjectId.has_value());  // 无引用目标
    }

    // ---- AlignFrame：留痕核对通过的目标 ObjectId（参考姿态数值归评估
    // ——本层零数值产出，隔离声明）。
    OrientationRule alignFrame;
    alignFrame.kind = OrientationRuleKind::AlignFrame;
    alignFrame.targetFrame.kind = RequirementRefKind::ModelFrame;
    alignFrame.targetFrame.objectId = frameOid;
    {
        const OrientationResolution r = resolveOrientationRule(alignFrame, anchor, closure);
        ASSERT_TRUE(r.ok);
        EXPECT_EQ(r.kind, OrientationRuleKind::AlignFrame);
        ASSERT_TRUE(r.targetObjectId.has_value());
        EXPECT_EQ(*r.targetObjectId, frameOid);
        EXPECT_FALSE(r.referenceRpy.has_value());     // 数值姿态不在此层
        EXPECT_FALSE(r.referenceDirection.has_value());
    }

    // ---- AlignGeometryNormal：留痕目标 ObjectId＋特征＋取反标志。
    OrientationRule alignNormal;
    alignNormal.kind = OrientationRuleKind::AlignGeometryNormal;
    alignNormal.targetSceneObject = sceneOid;
    alignNormal.feature = OrientationFeature::FramePlaneNormal;
    alignNormal.invertNormal = true;
    {
        const OrientationResolution r = resolveOrientationRule(alignNormal, anchor, closure);
        ASSERT_TRUE(r.ok);
        EXPECT_EQ(r.kind, OrientationRuleKind::AlignGeometryNormal);
        ASSERT_TRUE(r.targetObjectId.has_value());
        EXPECT_EQ(*r.targetObjectId, sceneOid);
        ASSERT_TRUE(r.feature.has_value());
        EXPECT_EQ(*r.feature, OrientationFeature::FramePlaneNormal);
        EXPECT_TRUE(r.invertNormal);
    }

    // ---- PointAtTarget：产出单位方向（refFrame 系，无量纲）——目标
    // (3,0,4) m 范数 5 → (0.6,0,0.8)。
    OrientationRule pointAt;
    pointAt.kind = OrientationRuleKind::PointAtTarget;
    pointAt.targetPoint = rw::math::Vector3D<double>(3.0, 0.0, 4.0);  // m
    {
        const OrientationResolution r = resolveOrientationRule(pointAt, anchor, closure);
        ASSERT_TRUE(r.ok);
        EXPECT_EQ(r.kind, OrientationRuleKind::PointAtTarget);
        ASSERT_TRUE(r.referenceDirection.has_value());
        EXPECT_DOUBLE_EQ(r.referenceDirection->operator[](0), 0.6);
        EXPECT_DOUBLE_EQ(r.referenceDirection->operator[](1), 0.0);
        EXPECT_DOUBLE_EQ(r.referenceDirection->operator[](2), 0.8);
    }

    // ---- ToolRollFree：留痕滚转自由区间（rad，默认 [−π,π]）。
    OrientationRule rollFree;
    rollFree.kind = OrientationRuleKind::ToolRollFree;  // 缺省 rollRange＝[−π,π]
    {
        const OrientationResolution r = resolveOrientationRule(rollFree, anchor, closure);
        ASSERT_TRUE(r.ok);
        EXPECT_EQ(r.kind, OrientationRuleKind::ToolRollFree);
        ASSERT_TRUE(r.rollSpan.has_value());
        EXPECT_DOUBLE_EQ(r.rollSpan->min, -3.14159265358979323846);
        EXPECT_DOUBLE_EQ(r.rollSpan->max, 3.14159265358979323846);
    }
}

/**
 * 等效角不归一（ACC1——§5.3"等价姿态与内容身份"行：参数字面留痕，
 * NFR-COR-03 不改写用户输入）：φ 与 φ+2π 的 Fixed 解析产出保持各自
 * 字面（数值不等——归一即隐性改写，禁止）。
 */
TEST(ReqOrientationResolution, FixedLiteralNotNormalized_WP14T06_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-09", "NFR-COR-03"},
                  std::vector<std::string>{"AT-23"});

    CheckContext closure;  // 无引用目标——Fixed 不查闭包
    const OrientationRuleAnchor anchor = makeAnchor();

    OrientationRule a;
    a.kind = OrientationRuleKind::Fixed;
    a.fixedRpy = rw::math::Vector3D<double>(0.0, 0.0, 0.5);  // rad
    OrientationRule b;
    b.kind = OrientationRuleKind::Fixed;
    b.fixedRpy = rw::math::Vector3D<double>(0.0, 0.0, 0.5 + 6.283185307179586);  // rad（等效角）

    const OrientationResolution ra = resolveOrientationRule(a, anchor, closure);
    const OrientationResolution rb = resolveOrientationRule(b, anchor, closure);
    ASSERT_TRUE(ra.ok);
    ASSERT_TRUE(rb.ok);
    // 字面逐分量不等（yaw 差 2π——解析产出忠实保留，不归一）。
    ASSERT_TRUE(ra.referenceRpy.has_value());
    ASSERT_TRUE(rb.referenceRpy.has_value());
    EXPECT_NE(rb.referenceRpy->operator[](2), ra.referenceRpy->operator[](2));
}

/**
 * 解析确定性（ACC1——NFR-COR-01：同输入必得同 resolution）：同规则
 * 同闭包两次解析的产出逐字段一致（方向归一化的 IEEE754 确定运算）。
 */
TEST(ReqOrientationResolution, ResolutionIsDeterministic_WP14T06_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-01"}, std::vector<std::string>{"AT-23"});

    const core::ObjectId sceneOid = oidOf("obj-00000000000000000000000000000b02");
    CheckContext closure;
    addRef(closure, sceneOid, "scene-object");
    const OrientationRuleAnchor anchor = makeAnchor();

    OrientationRule rule;
    rule.kind = OrientationRuleKind::PointAtTarget;
    rule.targetPoint = rw::math::Vector3D<double>(1.0, -2.0, 3.0);  // m

    const OrientationResolution r1 = resolveOrientationRule(rule, anchor, closure);
    const OrientationResolution r2 = resolveOrientationRule(rule, anchor, closure);
    ASSERT_TRUE(r1.ok);
    ASSERT_TRUE(r2.ok);
    ASSERT_TRUE(r1.referenceDirection.has_value());
    ASSERT_TRUE(r2.referenceDirection.has_value());
    EXPECT_DOUBLE_EQ(r1.referenceDirection->operator[](0), r2.referenceDirection->operator[](0));
    EXPECT_DOUBLE_EQ(r1.referenceDirection->operator[](1), r2.referenceDirection->operator[](1));
    EXPECT_DOUBLE_EQ(r1.referenceDirection->operator[](2), r2.referenceDirection->operator[](2));
    EXPECT_EQ(r1.kind, r2.kind);
}

// =====================================================================
// acceptance 2——解析失败可定位＋浅校验边界
// =====================================================================

/**
 * 引用悬空/token 失配可定位（ACC2——DTB 完成条件"目标悬空等引用/
 * 参数失败→可定位诊断回指需求条目（objectId＋name）"）：AlignFrame
 * 目标不在闭包→REQ-READY-REF-MISSING，subject/localName 回指锚；目标
 * 在闭包但 token 失配→同码（两类违例统一定位面）；context 携
 * field/target/expected-token 三键（与就绪 R1 同键对齐）。
 */
TEST(ReqOrientationResolution, DanglingOrMismatchedTargetLocatableDiag_WP14T06_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-09"}, std::vector<std::string>{"AT-23"});

    const core::ObjectId frameOid = oidOf("obj-00000000000000000000000000000b01");
    const core::ObjectId strangerOid = oidOf("obj-00000000000000000000000000000b09");
    CheckContext closure;
    // 注意：frameOid 以 scene-object token 登记（故意失配——期望
    // robot-design）；strangerOid 完全不在闭包（悬空）。
    addRef(closure, frameOid, "scene-object");
    const OrientationRuleAnchor anchor = makeAnchor();

    // 悬空：目标不在闭包。
    OrientationRule dangling;
    dangling.kind = OrientationRuleKind::AlignFrame;
    dangling.targetFrame.kind = RequirementRefKind::ModelFrame;
    dangling.targetFrame.objectId = strangerOid;
    {
        const OrientationResolution r = resolveOrientationRule(dangling, anchor, closure);
        ASSERT_FALSE(r.ok);
        EXPECT_EQ(r.diag.code, "REQ-READY-REF-MISSING");
        ASSERT_TRUE(r.diag.subject.has_value());
        EXPECT_EQ(*r.diag.subject, anchor.entryId);       // 回指条目 id
        ASSERT_TRUE(r.diag.localName.has_value());
        EXPECT_EQ(*r.diag.localName, anchor.entryName);   // 回指条目名
        // context 三键（field/target/expected-token——定位面键序固定）。
        EXPECT_NE(r.diag.context.find("field=targetFrame"), std::string::npos);
        EXPECT_NE(r.diag.context.find("target=" + strangerOid.toCanonical()),
                  std::string::npos);
        EXPECT_NE(r.diag.context.find("expected-token=robot-design"), std::string::npos);
        // 值面：错误码可判（引用悬空的就近族承载——机器判别看稳定码）。
        EXPECT_EQ(r.error.code, RequirementErrorCode::IllegalTolerance);
    }

    // token 失配：目标在闭包但登记类型与期望不符。
    OrientationRule mismatched;
    mismatched.kind = OrientationRuleKind::AlignFrame;
    mismatched.targetFrame.kind = RequirementRefKind::ModelFrame;
    mismatched.targetFrame.objectId = frameOid;
    {
        const OrientationResolution r = resolveOrientationRule(mismatched, anchor, closure);
        ASSERT_FALSE(r.ok);
        EXPECT_EQ(r.diag.code, "REQ-READY-REF-MISSING");
        EXPECT_NE(r.diag.context.find("expected-token=robot-design"), std::string::npos);
        EXPECT_NE(r.diag.cause.find("token 不匹配"), std::string::npos);
    }
}

/**
 * 非法参数构造边界拒绝＋REQ-READY-POSE-ILLEGAL（ACC2——V-03 反例半边：
 * 零向量目标/rollRange 逆序/缺 feature/非有限角四反例——错误值面与
 * 稳定码定位面双轨断言；subject/localName 回指条目）。
 */
TEST(ReqOrientationResolution, IllegalParamsRejectedWithPoseIllegalDiag_WP14T06_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-09"}, std::vector<std::string>{"AT-23"});

    CheckContext closure;  // 反例均在参数面短路——无需闭包目标
    const OrientationRuleAnchor anchor = makeAnchor();

    // 反例①：PointAtTarget 零向量目标（ZeroVectorTarget 值面＋
    // POSE-ILLEGAL 稳定码）。
    {
        OrientationRule rule;
        rule.kind = OrientationRuleKind::PointAtTarget;
        rule.targetPoint = rw::math::Vector3D<double>(0.0, 0.0, 0.0);  // m（零向量）
        const OrientationResolution r = resolveOrientationRule(rule, anchor, closure);
        ASSERT_FALSE(r.ok);
        EXPECT_EQ(r.error.code, RequirementErrorCode::ZeroVectorTarget);
        EXPECT_EQ(r.diag.code, "REQ-READY-POSE-ILLEGAL");
        ASSERT_TRUE(r.diag.subject.has_value());
        EXPECT_EQ(*r.diag.subject, anchor.entryId);
        ASSERT_TRUE(r.diag.localName.has_value());
        EXPECT_EQ(*r.diag.localName, anchor.entryName);
    }
    // 反例②：ToolRollFree rollRange 逆序（min>=max——区间空/逆）。
    {
        OrientationRule rule;
        rule.kind = OrientationRuleKind::ToolRollFree;
        rule.rollRange = RollRange{1.0, -1.0};  // rad（逆序）
        const OrientationResolution r = resolveOrientationRule(rule, anchor, closure);
        ASSERT_FALSE(r.ok);
        EXPECT_EQ(r.diag.code, "REQ-READY-POSE-ILLEGAL");
        EXPECT_NE(r.diag.context.find("field=rollRange"), std::string::npos);
    }
    // 反例③：AlignGeometryNormal 缺 feature（特征必须显式——不猜缺省）。
    {
        OrientationRule rule;
        rule.kind = OrientationRuleKind::AlignGeometryNormal;
        rule.targetSceneObject = oidOf("obj-00000000000000000000000000000b02");
        rule.feature = std::nullopt;  // 缺 feature
        const OrientationResolution r = resolveOrientationRule(rule, anchor, closure);
        ASSERT_FALSE(r.ok);
        EXPECT_EQ(r.diag.code, "REQ-READY-POSE-ILLEGAL");
        EXPECT_NE(r.diag.context.find("field=feature"), std::string::npos);
    }
    // 反例④：Fixed 非有限角（NaN——参数字面不可解析）。
    {
        OrientationRule rule;
        rule.kind = OrientationRuleKind::Fixed;
        rule.fixedRpy = rw::math::Vector3D<double>(
            std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0);  // rad（NaN）
        const OrientationResolution r = resolveOrientationRule(rule, anchor, closure);
        ASSERT_FALSE(r.ok);
        EXPECT_EQ(r.diag.code, "REQ-READY-POSE-ILLEGAL");
        EXPECT_NE(r.diag.context.find("field=fixedRpy"), std::string::npos);
    }
}

/**
 * 浅校验边界（ACC2——§8.1 专项声明用例）：解析只消费闭包 objectRefs
 * **元数据**（objectId＋objectTypeToken），全程零 modeling 对象字节
 * ——本文件不构造任何 modeling 对象即完成引用型规则的成功解析与失配
 * 拒绝（结构性自证：若实现试图解码 modeling 内容，本用例在数据面即
 * 不可成立）。
 */
TEST(ReqOrientationResolution, ShallowBoundaryMetadataOnly_WP14T06_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-09", "REQ-10"},
                  std::vector<std::string>{"AT-23"});

    // 闭包仅含元数据（addRef 的 ObjectRef 四字段——digest 为占位串）。
    const core::ObjectId sceneOid = oidOf("obj-00000000000000000000000000000b02");
    CheckContext closure;
    addRef(closure, sceneOid, "scene-object");
    const OrientationRuleAnchor anchor = makeAnchor();

    // 特征法向规则解析成功——法向**数值**无从取得也不需要（归评估侧）。
    OrientationRule alignNormal;
    alignNormal.kind = OrientationRuleKind::AlignGeometryNormal;
    alignNormal.targetSceneObject = sceneOid;
    alignNormal.feature = OrientationFeature::FrameOrigin;
    const OrientationResolution ok =
        resolveOrientationRule(alignNormal, anchor, closure);
    ASSERT_TRUE(ok.ok);
    ASSERT_TRUE(ok.targetObjectId.has_value());
    EXPECT_EQ(*ok.targetObjectId, sceneOid);
    // 失配路径同样只依赖元数据（token 串比对）。
    CheckContext wrongToken;
    addRef(wrongToken, sceneOid, "robot-design");  // 登记类型错——scene-object 期望
    const OrientationResolution bad =
        resolveOrientationRule(alignNormal, anchor, wrongToken);
    ASSERT_FALSE(bad.ok);
    EXPECT_EQ(bad.diag.code, "REQ-READY-REF-MISSING");
}
