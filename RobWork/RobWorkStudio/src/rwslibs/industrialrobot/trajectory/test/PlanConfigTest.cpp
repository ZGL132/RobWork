/**
 * @file   PlanConfigTest.cpp
 * @brief  轨迹求解配置用例组（TrjPlanConfig）——§5.5 TrajectoryPlan-
 *         Configuration 的合法域校验、canonical v1 编解码往返、确定性
 *         （同配置同字节/参数表字典序）、digest 身份面与 ConfigEntry
 *         条目（任务契约 WP-16-T04 acceptance 2——身份绑定面）。
 *
 * 设计依据：
 *   - units/trajectory.md §5.5（九字段约束表、canonical 编码纪律 magic
 *     IRDCFGTR1、"进 sliceId、不进 inputBaselineId"）、§6.2（身份块
 *     planConfigDigest 消费）
 *   - 需求 NFR-COR-01/02/03（确定性；同配置同字节；非法拒绝）、I-KIN-4
 *     同款（seed=0 拒绝不静默替换）、AT-27（求解配置分层）
 *   - 先例：kinematics test/AnalysisConfigTest.cpp（WP-15-T10 同款用例
 *     形态——校验逐项/往返/确定性/身份敏感四组）
 *   - 任务契约 tasks/foundation/WP-16-T04.json acceptance 2
 */

#include <sdurws/ird/trajectory/PlanConfig.hpp>

#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>
#include <sdurws/ird/trajectory/Errors.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

using sdurws::ird::core::ContentDigester;
using sdurws::ird::core::ContentIdentity;
using sdurws::ird::core::ObjectId;
using sdurws::ird::trajectory::DwellPolicy;
using sdurws::ird::trajectory::StartStateKind;
using sdurws::ird::trajectory::TrajectoryPlanConfigCodec;
using sdurws::ird::trajectory::TrajectoryPlanConfiguration;
using sdurws::ird::trajectory::TrajectoryError;
using sdurws::ird::trajectory::kConfigTrjKindToken;
using sdurws::ird::trajectory::kTimeParamMethodQuinticSplineC2;
using sdurws::ird::trajectory::kTrajectoryPlanConfigMagic;
using sdurws::ird::trajectory::kTrajectoryPlanConfigMagicSize;
using sdurws::ird::trajectory::makeTrajectoryPlanConfigRefEntry;
using sdurws::ird::trajectory::trajectoryPlanConfigurationDigest;
using sdurws::ird::trajectory::validateTrajectoryPlanConfiguration;

namespace {

/**
 * @brief 构造一份全字段"合法基线"配置（各字段均取 §5.5 约束内的代表
 *        值——种子/步长/阈值/容差/缩放/驻留策略全部显式；测试在各用例
 *        中以拷贝微调单字段构造反例，保证"一次只违约一处"的定位清晰）。
 */
TrajectoryPlanConfiguration makeValidConfig()
{
    TrajectoryPlanConfiguration c;
    c.startStateKind = StartStateKind::Home;             // Home＝零附件形态
    c.plannerFamilyToken = "rrt-connect";                // 实现选型 token（词表校验归 T06）
    c.plannerParams = {{"goal-bias", "0.05"}, {"timeout-s", "2.0"}};  // 两参数
    c.planningSeed = 20261007ULL;                        // 非零（I-KIN-4）
    c.cartesianSampleStep = 0.01;                        // m
    c.ikContinuityThreshold = 1e-6;                      // rad|m 逐轴
    c.smoothToleranceJoint = 1e-3;                       // rad
    c.smoothToleranceTcp = 1e-3;                         // m
    c.timeParamMethod = kTimeParamMethodQuinticSplineC2; // v1 唯一词表值
    c.limitsScaleFactor = 0.8;                           // (0,1]
    c.dwellPolicy = DwellPolicy::HonorEvents;            // Verified 口径
    return c;
}

}  // namespace

// =====================================================================
// 合法域校验（逐项反例——NFR-COR-03 拒绝不钳制；校验序首错即抛）
// =====================================================================

/** 全字段合法基线必须通过校验（正例锚——反例组的对照面）。 */
TEST(TrjPlanConfig, ValidBaselinePasses_WP16T04_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-01"}, std::vector<std::string>{});
    EXPECT_NO_THROW(validateTrajectoryPlanConfiguration(makeValidConfig()));
}

/** seed=0 非法（I-KIN-4 同款——拒绝、不做 0→1 静默替换）。 */
TEST(TrjPlanConfig, ZeroSeedRejected_WP16T04_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-02"}, std::vector<std::string>{});
    auto c = makeValidConfig();
    c.planningSeed = 0U;
    EXPECT_THROW(validateTrajectoryPlanConfiguration(c), TrajectoryError);
}

/** TaskPoint 形态缺对象身份→拒绝（§5.5 startStateRef 三选一的附件纪律）。 */
TEST(TrjPlanConfig, TaskPointWithoutOidRejected_WP16T04_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-01"}, std::vector<std::string>{});
    auto c = makeValidConfig();
    c.startStateKind = StartStateKind::TaskPoint;
    c.startTaskPoint = ObjectId{};  // 全零保留值＝未设置
    EXPECT_THROW(validateTrajectoryPlanConfiguration(c), TrajectoryError);
}

/** 非 TaskPoint 形态携带悬空附件→拒绝（同语义同字节——身份面纪律）。 */
TEST(TrjPlanConfig, HomeWithDanglingOidRejected_WP16T04_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-01"}, std::vector<std::string>{});
    auto c = makeValidConfig();
    c.startStateKind = StartStateKind::Home;
    c.startTaskPoint = ObjectId::generate();  // 非 TaskPoint 不得携带
    EXPECT_THROW(validateTrajectoryPlanConfiguration(c), TrajectoryError);
}

/** 正浮点域三处（步长/阈值/平滑容差）任一 ≤0 或非有限→拒绝。 */
TEST(TrjPlanConfig, PositiveFloatDomainsRejected_WP16T04_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-03"}, std::vector<std::string>{});
    auto a = makeValidConfig();
    a.cartesianSampleStep = 0.0;  // m——0 无采样语义
    EXPECT_THROW(validateTrajectoryPlanConfiguration(a), TrajectoryError);

    auto b = makeValidConfig();
    b.ikContinuityThreshold = -1e-6;  // rad|m——负阈值无意义
    EXPECT_THROW(validateTrajectoryPlanConfiguration(b), TrajectoryError);

    auto c = makeValidConfig();
    c.smoothToleranceJoint = std::numeric_limits<double>::quiet_NaN();  // rad——非有限
    EXPECT_THROW(validateTrajectoryPlanConfiguration(c), TrajectoryError);
}

/** limitsScaleFactor ∉(0,1]→拒绝（保守缩放域——0 与 >1 均违约）。 */
TEST(TrjPlanConfig, LimitsScaleDomainRejected_WP16T04_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-05"}, std::vector<std::string>{});
    auto a = makeValidConfig();
    a.limitsScaleFactor = 0.0;
    EXPECT_THROW(validateTrajectoryPlanConfiguration(a), TrajectoryError);
    auto b = makeValidConfig();
    b.limitsScaleFactor = 1.5;  // 放大限值违反保守缩放语义
    EXPECT_THROW(validateTrajectoryPlanConfiguration(b), TrajectoryError);
}

/** 运动律词表外 token→拒绝（§5.5 封闭词表 v1——唯一合法值）。 */
TEST(TrjPlanConfig, TimeParamMethodWordlistRejected_WP16T04_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-08"}, std::vector<std::string>{});
    auto c = makeValidConfig();
    c.timeParamMethod = "septic-spline-c4";  // 词表外——扩展走卡面增量修订
    EXPECT_THROW(validateTrajectoryPlanConfiguration(c), TrajectoryError);
}

/** 规划器家族空 token／参数表 NUL→拒绝（canonical 串安全下限）。 */
TEST(TrjPlanConfig, PlannerStringSafetyRejected_WP16T04_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-03"}, std::vector<std::string>{});
    auto a = makeValidConfig();
    a.plannerFamilyToken.clear();  // 空 token 无身份意义
    EXPECT_THROW(validateTrajectoryPlanConfiguration(a), TrajectoryError);

    auto b = makeValidConfig();
    // 键含 NUL——带长度构造（字面量在首个 NUL 截断，须显式长度才含 NUL）。
    b.plannerParams[std::string("bad\0key", 7)] = "v";
    EXPECT_THROW(validateTrajectoryPlanConfiguration(b), TrajectoryError);
}

// =====================================================================
// canonical 编解码（v1 布局——往返/确定性/字典序/拒绝面）
// =====================================================================

/** encode→decode 往返还原全字段（解码产物与原配置逐字段相等）。 */
TEST(TrjPlanConfig, EncodeDecodeRoundtrip_WP16T04_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-01"}, std::vector<std::string>{});
    const auto config = makeValidConfig();
    const auto bytes = TrajectoryPlanConfigCodec{}.encode(config);
    // 布局下界核验：纯标量段 78 字节＋三个变长段——至少 90 字节。
    ASSERT_GE(bytes.size(), 90U);
    const auto decoded = TrajectoryPlanConfigCodec{}.decode(bytes);
    EXPECT_EQ(decoded, config);
}

/** 确定性：同配置两次编码得同字节（NFR-COR-01/02——纯位组装）。 */
TEST(TrjPlanConfig, EncodeDeterministic_WP16T04_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-02"}, std::vector<std::string>{});
    const auto bytes1 = TrajectoryPlanConfigCodec{}.encode(makeValidConfig());
    const auto bytes2 = TrajectoryPlanConfigCodec{}.encode(makeValidConfig());
    ASSERT_EQ(bytes1.size(), bytes2.size());
    EXPECT_TRUE(bytes1 == bytes2);
}

/** 参数表插入序无关：同集合不同插入序必得同字节（集合字典序纪律）。 */
TEST(TrjPlanConfig, ParamsDictionaryOrderCanonical_WP16T04_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-01"}, std::vector<std::string>{});
    auto a = makeValidConfig();
    a.plannerParams = {{"b-key", "2"}, {"a-key", "1"}};   // 逆字典序插入
    auto b = makeValidConfig();
    b.plannerParams = {{"a-key", "1"}, {"b-key", "2"}};   // 正字典序插入
    const auto bytesA = TrajectoryPlanConfigCodec{}.encode(a);
    const auto bytesB = TrajectoryPlanConfigCodec{}.encode(b);
    ASSERT_EQ(bytesA.size(), bytesB.size());
    EXPECT_TRUE(bytesA == bytesB) << "同参数集合不同插入序编码不同——字典序违约";
}

/** magic 头核验：编码以 IRDCFGTR1 开头（§5.5 指定 magic——9 字节）。 */
TEST(TrjPlanConfig, MagicHeader_WP16T04_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-01"}, std::vector<std::string>{});
    const auto bytes = TrajectoryPlanConfigCodec{}.encode(makeValidConfig());
    ASSERT_GE(bytes.size(), kTrajectoryPlanConfigMagicSize);
    for (std::size_t i = 0; i < kTrajectoryPlanConfigMagicSize; ++i) {
        EXPECT_EQ(bytes[i], static_cast<std::uint8_t>(kTrajectoryPlanConfigMagic[i]));
    }
}

/** decode 拒绝面：错 magic／错版本／截断／尾部多字节——四反例全拒。 */
TEST(TrjPlanConfig, DecodeRejections_WP16T04_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-03"}, std::vector<std::string>{});
    auto bytes = TrajectoryPlanConfigCodec{}.encode(makeValidConfig());

    auto badMagic = bytes;
    badMagic[3] = 'X';  // IRDCFGTR1 → IRDXFGTR1
    EXPECT_THROW(TrajectoryPlanConfigCodec{}.decode(badMagic), TrajectoryError);

    auto badVersion = bytes;
    badVersion[9] = 0x02;  // u32 小端 LSB：schemaVersion 1→2（向前不兼容）
    EXPECT_THROW(TrajectoryPlanConfigCodec{}.decode(badVersion), TrajectoryError);

    auto truncated = bytes;
    truncated.pop_back();  // 截断尾字节——变长段长度界失配
    EXPECT_THROW(TrajectoryPlanConfigCodec{}.decode(truncated), TrajectoryError);

    auto withTail = bytes;
    withTail.push_back(0x00);  // 尾部多一字节——布局封闭性违约
    EXPECT_THROW(TrajectoryPlanConfigCodec{}.decode(withTail), TrajectoryError);
}

// =====================================================================
// digest 身份面（§5.5——进 sliceId、不进 inputBaselineId；AT-27 分层）
// =====================================================================

/** digest＝对 canonical 字节的 SHA-256（同源一致性——ContentDigester 直算对照）。 */
TEST(TrjPlanConfig, DigestEqualsShaOverCanonicalBytes_WP16T04_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-01"}, std::vector<std::string>{});
    const auto config = makeValidConfig();
    const auto bytes = TrajectoryPlanConfigCodec{}.encode(config);
    ContentDigester digester;
    digester.update(bytes.data(), bytes.size());
    const ContentIdentity expected{digester.finalize()};
    EXPECT_TRUE(trajectoryPlanConfigurationDigest(config) == expected);
}

/** 身份敏感：任一身份域字段变化⇒digest 变化（修改即失效重算的数据基础）。 */
TEST(TrjPlanConfig, DigestSensitiveToFieldChange_WP16T04_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"AT-27", "CON-05"}, std::vector<std::string>{});
    const auto base = trajectoryPlanConfigurationDigest(makeValidConfig());

    auto changed = makeValidConfig();
    changed.cartesianSampleStep = 0.02;  // m——采样计划改变（进身份字段）
    EXPECT_FALSE(trajectoryPlanConfigurationDigest(changed) == base);

    auto changed2 = makeValidConfig();
    changed2.planningSeed = 99ULL;  // 种子改变（确定性复现要素进身份）
    EXPECT_FALSE(trajectoryPlanConfigurationDigest(changed2) == base);
}

/** 配置引用条目：kindToken=="config.trj"＋contentIdentity==digest 同源值。 */
TEST(TrjPlanConfig, RefEntryKindTokenAndIdentity_WP16T04_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"CON-05"}, std::vector<std::string>{});
    const auto config = makeValidConfig();
    const auto entry = makeTrajectoryPlanConfigRefEntry(config);
    EXPECT_EQ(entry.configKindToken, kConfigTrjKindToken);
    EXPECT_EQ(kConfigTrjKindToken, std::string("config.trj"));
    EXPECT_TRUE(entry.contentIdentity == trajectoryPlanConfigurationDigest(config));
    EXPECT_FALSE(entry.canonicalBytes.empty());
}
