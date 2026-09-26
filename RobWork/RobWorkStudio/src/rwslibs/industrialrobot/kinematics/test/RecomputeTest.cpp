/**
 * @file   RecomputeTest.cpp
 * @brief  批量复算请求构造用例组（KinRecompute；WP-15-T09）——"按原
 *         slice 重新提交 execution 新 run"的构造正确性（KIN-08/§7.4/
 *         CON-02/05 消费语义）。
 *
 * 设计依据：
 *   - units/kinematics.md §7.4（批量复算＝按原 slice 重新提交任务
 *     （execution 新 run）——不复用会话残留，结果当前性重新判定）、
 *     §6.3 D-KIN-4（referenceQ 显式化——禁止隐式读会话姿态）
 *   - REQUIREMENTS KIN-08（批量复算）、CON-02（当前性正交——请求不
 *     预持当前性结论）、CON-05（切片内容寻址——工程未变重推导同一切
 *     片身份）、NFR-COR-03（非法输入拒绝）
 *   - 任务契约 tasks/foundation/WP-15-T09.json acceptance 3（本单元保证
 *     重提交请求构造正确性；提交动作与触发方归 execution/插件侧——
 *     本组只验证构造面）
 */

#include <sdurws/ird/kinematics/Recompute.hpp>

#include <sdurws/ird/kinematics/Evidence.hpp>  // kTaskPointsBatchEvaluationKey——评估键词表
#include <sdurws/ird/kinematics/Ik.hpp>        // kTaskPointIkEvaluationKey——评估键词表

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯登记

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>
#include <vector>

using namespace sdurws::ird::kinematics;

namespace core = sdurws::ird::core;

namespace {

// =====================================================================
// 测试夹具：原结果身份与当前快照绑定（固定值——fromCanonical 严格解析）
// =====================================================================

/// 固定内容身份（"cid-"＋64 hex——尾字节序号区分）。
core::ContentIdentity makeCid(unsigned char last)
{
    std::string text = "cid-";
    for (int i = 0; i < 63; ++i) {
        text += '0';
    }
    text += std::string{"0123456789abcdef"}[last % 16];
    return core::ContentIdentity::fromCanonical(text);
}

/// 原结果的请求身份（sliceId＝原切片锚；snapshotId＝原 run 快照——
/// 复算场景下它属于历史，当前绑定另给）。
IkRequestIdentity originalIdentity()
{
    IkRequestIdentity id;
    id.snapshotId = makeCid(0x1);  // 原 run 快照（历史锚）
    id.sliceId = makeCid(0x2);     // 原切片（复算任务闭包定义来源）
    id.configDigest = makeCid(0x3);
    id.mode = core::EvaluationMode::Quick;
    id.seed = 7U;
    id.referenceQ = {0.1, 0.2, 0.3};
    return id;
}

}  // namespace

// ---------------------------------------------------------------------
// ACC3-1：原任务定义逐字段携带（按原 slice——同键同模式同种子同参考构型）
// ---------------------------------------------------------------------

TEST(KinRecompute, CarriesOriginalTaskDefinition_WP15T09_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-08", "CON-05"},
                  std::vector<std::string>{});

    const IkRequestIdentity origin = originalIdentity();
    const KinRecomputeRequest req = buildKinRecomputeRequest(
        origin, kTaskPointIkEvaluationKey, makeCid(0x9));

    // 携带面逐字段核对（构造正确性的断言主体——§7.4"按原 slice 重新
    // 提交"：复算同一评估定义）。
    EXPECT_EQ(req.sliceId, origin.sliceId) << "原切片身份为语义锚";
    EXPECT_EQ(req.evaluationKey, kTaskPointIkEvaluationKey) << "同键复算";
    EXPECT_EQ(req.mode, origin.mode);
    EXPECT_EQ(req.seed, origin.seed);
    EXPECT_EQ(req.referenceQ, origin.referenceQ)
        << "referenceQ 自原身份显式携带（D-KIN-4——不读会话）";
}

// ---------------------------------------------------------------------
// ACC3-2：当前快照绑定直传＋当前性不继承（CON-02 正交）
// ---------------------------------------------------------------------

TEST(KinRecompute, BindsCallerSuppliedCurrentSnapshot_WP15T09_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-08", "CON-02"},
                  std::vector<std::string>{});

    const IkRequestIdentity origin = originalIdentity();

    // 场景一：设计已变更——当前快照 ≠ 原 run 快照（重提交锚定当前状态，
    // 结果当前性由 evidence 重新判定；请求不携带任何当前性结论——类型
    // 面无此字段，结构性正交）。
    const core::ContentIdentity changed = makeCid(0x9);
    const KinRecomputeRequest afterChange =
        buildKinRecomputeRequest(origin, kTaskPointIkEvaluationKey, changed);
    EXPECT_EQ(afterChange.snapshotId, changed);
    EXPECT_NE(afterChange.snapshotId, origin.snapshotId)
        << "当前绑定不继承原 run 快照锚（当前性重新判定的输入面）";
    EXPECT_EQ(afterChange.sliceId, origin.sliceId) << "slice 锚不变（对照键）";

    // 场景二：工程未变——当前快照与原快照同内容身份（CON-05：内容寻址
    // 下"按原 slice"自动成立；重推导的切片同身份，evidence 判 Current）。
    const KinRecomputeRequest unchanged = buildKinRecomputeRequest(
        origin, kTaskPointIkEvaluationKey, origin.snapshotId);
    EXPECT_EQ(unchanged.snapshotId, origin.snapshotId);
    // 两个请求仅 snapshotId 一字段相异（逐成员等值的变体差异钉面）。
    EXPECT_NE(afterChange.snapshotId, unchanged.snapshotId);
    EXPECT_EQ(afterChange.sliceId, unchanged.sliceId);
    EXPECT_EQ(afterChange.evaluationKey, unchanged.evaluationKey);

    // 目标对象引用（单点复算的呈现面）显式直传。
    const core::ObjectId point = core::ObjectId::fromCanonical(
        "obj-0000000000000000000000000000000a");
    const KinRecomputeRequest withSubject = buildKinRecomputeRequest(
        origin, kTaskPointIkEvaluationKey, changed, point);
    ASSERT_TRUE(withSubject.subjectOid.has_value());
    EXPECT_EQ(*withSubject.subjectOid, point);
    // 缺省（整闭包复算——批量/区域场景）为 nullopt。
    EXPECT_FALSE(afterChange.subjectOid.has_value());
}

// ---------------------------------------------------------------------
// ACC3-3：确定性＋零结果/会话残留（新 run 语义——构造只消费显式入参）
// ---------------------------------------------------------------------

TEST(KinRecompute, DeterministicAndFreeOfResidue_WP15T09_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-08", "NFR-COR-02"},
                  std::vector<std::string>{});

    const IkRequestIdentity origin = originalIdentity();
    const core::ContentIdentity current = makeCid(0x9);

    // 同输入两次构造全等（纯函数——无隐藏状态，NFR-COR-02）。
    const KinRecomputeRequest a =
        buildKinRecomputeRequest(origin, kTaskPointIkEvaluationKey, current);
    const KinRecomputeRequest b =
        buildKinRecomputeRequest(origin, kTaskPointIkEvaluationKey, current);
    EXPECT_EQ(a, b);

    // 零会话残留：referenceQ 只随原身份变化——构造签名不存在会话形参
    // （结构性保证，此处行为面验证：原身份 referenceQ 变则请求变，
    // 原身份不变则请求不变——不存在第三输入源）。
    IkRequestIdentity moved = originalIdentity();
    moved.referenceQ = {9.0, 9.0, 9.0};  // 显式换参考构型（新评估请求语义）
    const KinRecomputeRequest c =
        buildKinRecomputeRequest(moved, kTaskPointIkEvaluationKey, current);
    EXPECT_NE(c.referenceQ, a.referenceQ);
    EXPECT_EQ(c.referenceQ, moved.referenceQ);
    EXPECT_EQ(c.sliceId, a.sliceId) << "其余携带面不受影响";

    // 零评估键残留：evaluationKey 完全取自入参（换键即换请求——不同
    // 评估器的复算请求不会串键）。
    const KinRecomputeRequest d = buildKinRecomputeRequest(
        origin, kTaskPointsBatchEvaluationKey, current);
    EXPECT_NE(d.evaluationKey, a.evaluationKey);
    EXPECT_EQ(d.sliceId, a.sliceId);
}

// ---------------------------------------------------------------------
// ACC3-4：调用方错误 fail-fast（空评估键/全零快照锚——NFR-COR-03）
// ---------------------------------------------------------------------

TEST(KinRecompute, FailsFastOnEmptyKeyOrZeroSnapshot_WP15T09_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-08", "NFR-COR-03"},
                  std::vector<std::string>{});

    const IkRequestIdentity origin = originalIdentity();

    // 空评估键：execution 接纳面无法路由——invalid_argument。
    EXPECT_THROW(buildKinRecomputeRequest(origin, "", makeCid(0x9)),
                 std::invalid_argument);

    // 全零快照锚（ContentIdentity 保留值）：切片重推导无锚——invalid_argument。
    const core::ContentIdentity zero;  // 全零＝保留值
    EXPECT_THROW(
        buildKinRecomputeRequest(origin, kTaskPointIkEvaluationKey, zero),
        std::invalid_argument);
}
