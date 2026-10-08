/**
 * @file   ConstraintContractTest.cpp
 * @brief  静态硬约束编排契约用例组（OptConstraintContract）——③端口装配
 *         契约（Profile 先行＋真注册表注册期校验＋管线消费路径）、评估键
 *         词形/字面契约、AT-19 唯一实现消费面（本单元零碰撞算法源码扫描）
 *         与 P-EV-3 汇总顺序引用契约——任务契约 WP-20-T04 acceptance 1/4。
 *
 * 设计依据：
 *   - units/optimization.md §6.7（内部消费链——"依赖键闭包在 descriptor.
 *     inputs 声明，注册期校验"；"Profile 先于评估器注册——evidence §13
 *     顺序约束"）、§6.2（三入口一致 AT-19——静态碰撞只消费 policy 唯一
 *     实现；不在 optimization 复制碰撞算法〔N4〕）、§7.5（跨域汇总顺序
 *     ＝P-EV-3 open 引用 evidence 现状字面顺序）、§3.2（contract_test＝
 *     "跨单元契约（与 evidence/execution/project/kinematics/testkit 联合）"）
 *   - 需求 AT-19（碰撞三入口一致）、OPT-03（约束计算消费③④端口）
 *   - 先例：trajectory/dynamics 契约测试的对端面钉住形态
 */

#include <sdurws/ird/optimization/Constraint.hpp>

#include <sdurws/ird/evidence/Evaluator.hpp>
#include <sdurws/ird/evidence/Slice.hpp>
#include <sdurws/ird/optimization/DiagCodes.hpp>
#include <sdurws/ird/optimization/Types.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace sdurws::ird;
using optimization::ConstraintId;
using optimization::OptimizationConstraintProvider;
using optimization::OptimizationError;
using optimization::OptimizationStage;

namespace {

/// 测试工作树内 optimization 源码文件读取（源码扫描用——IRD_OPTIMIZATION_
/// UNIT_ROOT 由 CMake 注入，值内以单元名作子目录前缀，同 BuildRedLineTest；
/// 路径拼接经 std::filesystem——正斜杠＋".." 的字符串直拼在 MSVC ifstream
/// 下不可靠，fs::path 规范化后跨平台正确）。
std::string readUnitSource(const std::string& relativePath)
{
    const std::filesystem::path fullPath
        = std::filesystem::path{IRD_OPTIMIZATION_UNIT_ROOT} / relativePath;
    std::ifstream in(fullPath, std::ios::binary);
    if (!in) {
        ADD_FAILURE() << "源码文件不可读: " << fullPath.string();
        return {};
    }
    return std::string(std::istreambuf_iterator<char>(in),
                       std::istreambuf_iterator<char>());
}

/// 注册 "kin" v1.0 空 Profile（与模型测试同构——契约面独立自持）。
void registerKinProfile(evidence::EvidenceProfileRegistry& profiles)
{
    evidence::RequiredEvidenceProfile profile;
    profile.profileId = "kin";
    profile.version = "1.0";
    profiles.registerProfile(profile);
}

}  // namespace

// =====================================================================
// ③端口装配契约（acceptance 1——约束计算消费③端口）
// =====================================================================

TEST(OptConstraintContract, Port3ConsumptionPathThroughRealRegistry_WP20T04_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-03"}, std::vector<std::string>{"AT-19"});

    // 契约 1：优化侧评估键常量满足 evidence 评估键词形闸门（kebab、无点
    // ——isValidEvaluationKey；依赖声明与注册期校验共用此词形）。
    const std::string reachKey(optimization::kKinTaskPointsBatchKey);
    const std::string coverageKey(optimization::kKinRegionCoverageKey);
    EXPECT_TRUE(evidence::isValidEvaluationKey(reachKey));
    EXPECT_TRUE(evidence::isValidEvaluationKey(coverageKey));

    // 契约 2（字面钉扎——R-1 禁 include kinematics 头，跨单元键一致性以
    // 字面契约承载；权威＝kinematics 单元卡 §4.3 注册键与其 Evidence.hpp
    // 常量 kTaskPointsBatchEvaluationKey/kRegionCoverageEvaluationKey 的
    // 唯一书写点。kinematics 侧键漂移由其自身契约测试与集成链路拦截，
    // 本断言钉住 optimization 侧书写不漂移）：
    EXPECT_EQ(reachKey, "kin-task-points-batch");
    EXPECT_EQ(coverageKey, "kin-region-coverage");

    // 契约 3（Profile 先行——evidence §13 接入顺序约束的真注册表面）：
    // Profile 未注册时评估器注册被注册期校验拒绝；先行注册后同 descriptor
    // 注册成功且按优化侧常量键可 find 命中——生产装配顺序与管线消费路径
    // 的契约面（替身工厂承载计算主体——descriptor 校验是真实路径）。
    evidence::EvidenceProfileRegistry profiles;
    evidence::EvaluatorRegistry registry(profiles);
    auto makeFactory = [&reachKey]() {
        // 工厂闭包捕获描述符（真实注册面——校验消费 descriptor() 值）。
        class Factory final : public evidence::IEvaluatorFactory {
        public:
            explicit Factory(evidence::EvaluatorDescriptor d) : m_d(std::move(d)) {}
            const evidence::EvaluatorDescriptor& descriptor() const override
            {
                return m_d;
            }
            std::unique_ptr<evidence::IEngineeringEvaluator> create() const override
            {
                return nullptr;  // 本契约不消费实例——注册面验证即可
            }

        private:
            evidence::EvaluatorDescriptor m_d;
        };
        evidence::EvaluatorDescriptor d;
        d.key = reachKey;
        d.contractVersion = 1U;
        d.profile.profileId = "kin";
        d.profile.version = "1.0";
        d.supportedModes = {core::EvaluationMode::Quick, core::EvaluationMode::Verified};
        d.stateless = true;
        d.threadSafety = evidence::ThreadSafety::FullyThreadSafe;
        return std::make_unique<Factory>(std::move(d));
    };
    // 3a：Profile 未注册 → 注册期校验拒绝（ProfileUnresolvable 归
    // EvaluatorDescriptorInvalid 码面——§9.2"注册时必须已可解析"）。
    bool threw = false;
    try {
        registry.registerEvaluator(makeFactory(), {});
    } catch (const evidence::EvidenceError&) {
        threw = true;
    }
    EXPECT_TRUE(threw) << "Profile 未注册时评估器注册必须被拒（§13 顺序约束）";
    // 3b：Profile 先行注册 → 同 descriptor 注册成功＋常量键可 find 命中。
    registerKinProfile(profiles);
    registry.registerEvaluator(makeFactory(), {});
    EXPECT_NE(registry.find(reachKey), nullptr)
        << "③端口消费路径：优化侧常量键必须命中真注册表";
}

// =====================================================================
// 阶段锁契约半区（acceptance 2——两阶段口径分离的约束面复证）
// =====================================================================

TEST(OptConstraintContract, StageBJointFamilyRejectionIsLaunchBlockNotCandidateRejection_WP20T04_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-03"}, std::vector<std::string>{});

    // §6.5 呈现边界契约：阶段锁拒绝以**异常**承载（运行启动阻塞），不是
    // 返回带淘汰原因的清单/记录——接口签名的异常轨即契约形态（调用方
    // 不会把启动阻塞误读为候选淘汰）。
    const OptimizationConstraintProvider provider;
    for (const auto familyToken :
         {std::string("opt.constraint.trajectory"), std::string("opt.constraint.dynamics"),
          std::string("opt.constraint.drivetrain-performance"),
          std::string("opt.constraint.device-joint")}) {
        bool threw = false;
        try {
            (void)provider.resolveConstraintPlan(OptimizationStage::StageB, {familyToken});
        } catch (const OptimizationError& e) {
            threw = true;
            EXPECT_EQ(e.stableCode(), std::string(optimization::kOptStageLocked));
        }
        EXPECT_TRUE(threw) << familyToken << " 必须以异常轨拒绝（§6.5）";
    }
    // 家族 token 与 R1 约束 token 词表不相交（引用词表≠可执行词表——
    // 不预建占位 NFR-MNT-04 的词表面）。
    const auto plan = optimization::stageBConstraintPlan();
    for (const auto& spec : plan) {
        EXPECT_NE(optimization::toToken(spec.constraintId), "opt.constraint.trajectory");
        EXPECT_NE(optimization::toToken(spec.constraintId), "opt.constraint.dynamics");
        EXPECT_NE(optimization::toToken(spec.constraintId),
                  "opt.constraint.device-joint");
    }
}

// =====================================================================
// AT-19 唯一实现消费面（acceptance 1——零碰撞算法源码扫描）
// =====================================================================

TEST(OptConstraintContract, NoCollisionAlgorithmInUnitSources_WP20T04_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-03"}, std::vector<std::string>{"AT-19"});

    // AT-19/N4/R-POL-2 契约：碰撞判定唯一实现归 policy——本单元源码零
    // 碰撞算法（无 proximity 后端、无几何相交/距离计算、无第二套发现
    // 语义）。扫描域＝本批产品面（Constraint.hpp/Constraint.cpp——三入口
    // 中 OPT 入口的实现文件）。
    const std::string header
        = readUnitSource("optimization/include/sdurws/ird/optimization/Constraint.hpp");
    const std::string impl = readUnitSource("optimization/src/Constraint.cpp");
    ASSERT_FALSE(header.empty()) << "Constraint.hpp 不可读——扫描前置失败";
    ASSERT_FALSE(impl.empty()) << "Constraint.cpp 不可读——扫描前置失败";
    // 禁止字面（命中即实现越界——碰撞算法私制的代码级信号）：
    for (const auto* banned : {"rw::proximity", "CollisionStrategy", "DistanceStrategy",
                               "inCollision(", "distance("}) {
        EXPECT_EQ(header.find(banned), std::string::npos)
            << "Constraint.hpp 出现碰撞算法字面 " << banned << "（N4/R-POL-2）";
        EXPECT_EQ(impl.find(banned), std::string::npos)
            << "Constraint.cpp 出现碰撞算法字面 " << banned << "（N4/R-POL-2）";
    }
    // 消费面在位：探针接口与 SampleSet 语义字面必须存在（唯一实现消费
    // 契约的正向锚——零算法≠零消费）。
    EXPECT_NE(header.find("IStaticCollisionProbe"), std::string::npos);
    EXPECT_NE(header.find("probeSampleSet"), std::string::npos);
    EXPECT_NE(impl.find("probeSampleSet"), std::string::npos);
}

// =====================================================================
// P-EV-3 汇总顺序引用契约（acceptance 4——跨域汇总顺序引用 evidence 现状）
// =====================================================================

TEST(OptConstraintContract, VerdictTraceShapeReferencesEvidenceCurrentForm_WP20T04_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"EVI-01"}, std::vector<std::string>{});

    // P-EV-3 open（引用现状字面顺序）：候选判定流的跨域汇总顺序＝evidence
    // aggregateVerdict 六级决策表的字面顺序（③命中即输出、④/⑤不再评估）
    // ——optimization 不复制判定逻辑，只消费其 trace（六槽完整决策路径）
    // 并透传到候选记录。本契约钉住：词表六值存在且顺序固定（evidence
    // 侧实现漂移即由其自身 EV-VER 用例拦截；本断言钉住消费侧引用面不漂移）。
    EXPECT_EQ(static_cast<int>(evidence::VerdictLevel::OutcomePrecheck), 0);
    EXPECT_EQ(static_cast<int>(evidence::VerdictLevel::InputReadiness), 1);
    EXPECT_EQ(static_cast<int>(evidence::VerdictLevel::CommonEvidenceGate), 2);
    EXPECT_EQ(static_cast<int>(evidence::VerdictLevel::InfeasibilityProof), 3);
    EXPECT_EQ(static_cast<int>(evidence::VerdictLevel::MissingEvidence), 4);
    EXPECT_EQ(static_cast<int>(evidence::VerdictLevel::EngineeringJudgement), 5);
    // 资格检查词表（§7.2 五条件 token——漏验不出正式通过的观测面词形）：
    // evidence 承载，optimization 消费侧只透传不翻译（AT-09 反例用例在
    // 模型测试以字符串比对钉住 "coverage-incomplete" 词形）。
    EXPECT_EQ(static_cast<int>(evidence::CaseExecutionStatus::Executed), 0);
}
