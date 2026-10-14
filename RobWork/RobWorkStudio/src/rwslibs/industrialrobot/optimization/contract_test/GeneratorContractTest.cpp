/**
 * @file   GeneratorContractTest.cpp
 * @brief  搜索策略扩展点契约用例组（OptGeneratorContract）——OPT-10 接口
 *         扩展的契约面：词表零空占位逐 token 可解析直证（NFR-MNT-04 的
 *         机器可断言面）、策略切换经 config.opt 引用生效且进身份（§4.5
 *         失效映射）、未知 token 校验/分派双面同拒——任务契约 WP-21-T06
 *         acceptance（接口扩展评审通过；不预建空算法占位）。
 *
 * 设计依据：
 *   - units/optimization.md §8.2（搜索策略接口扩展点——扩展方式＝"注册新
 *     ICandidateGenerator 实现（strategyId 登记＋config.opt 引用）"；首版
 *     词表两策略；"不预建无语义的空算法占位"）、§4.5（策略进 config.opt
 *     ⇒ token/字节变化 ⇒ sliceId 变 ⇒ 缓存失效）、§12.1（ICandidateGenerator
 *     ——OPT-10 明确要求接口扩展）、§15.1 OPT-10 行（验证＝202 策略面＋
 *     接口评审——本文件即接口评审的机器可断言半区）
 *   - 需求 OPT-10（D/R2）、NFR-MNT-04（不预建空占位）、NFR-COR-03（未登记
 *     token 不静默接受）、CON-04/§4.5（策略是缓存键要素）
 *
 * 测试形态（契约测试＝跨单元联合面）：本组契约的消费对端＝config.opt
 * canonical 身份链（core::ContentDigester 经 canonicalizeRunConfiguration
 * 单点）——策略 token 进 canonical 字节的核对与 evidence 侧切片身份的
 * 生产路径同源（编排查找请求的 Configuration 条目即本 canonical 字节，
 * EvaluatorPortsContractTest 已钉其身份链）；本组补"策略引用面"半区。
 */

#include <sdurws/ird/optimization/Generator.hpp>

#include <sdurws/ird/optimization/CandidatePatch.hpp>
#include <sdurws/ird/optimization/DiagCodes.hpp>
#include <sdurws/ird/optimization/EvaluatorPorts.hpp>
#include <sdurws/ird/optimization/Objective.hpp>
#include <sdurws/ird/optimization/Types.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

using namespace sdurws::ird;
using optimization::GenerationBatch;
using optimization::GenerationContext;
using optimization::OptimizationConfiguration;
using optimization::OptimizationStage;

namespace {

/// 最小合法配置（空变量集——两策略在空维度集下都合法退化为基线批；
/// 目标集取 StageB 默认三项——validateConfiguration 检查⑩前提）。
OptimizationConfiguration makeMinimalConfig(const std::string& strategyId)
{
    OptimizationConfiguration c;
    c.stage = OptimizationStage::StageB;
    c.seed = 3;
    c.strategyId = strategyId;
    c.objectives = optimization::defaultObjectives(OptimizationStage::StageB);
    c.variables = {};
    optimization::validateConfiguration(c);
    return c;
}

/// 空维度生成上下文（词表解析直证用——批形态断言只看基线在场）。
GenerationContext makeCtx(const OptimizationConfiguration& config)
{
    GenerationContext ctx;
    ctx.config = config;
    ctx.generationIndex = 0;
    ctx.cancellationRequested = false;
    return ctx;
}

}  // namespace

// =====================================================================
// OPT-10 接口扩展契约面
// =====================================================================

/**
 * OptGeneratorContract.RegisteredVocabularyHasNoEmptyPlaceholder
 * 依据：卡 §8.2"不预建无语义的空算法占位"＋NFR-MNT-04——词表只登记已
 * 实现策略的机器可断言半区（WP-21-T06 acceptance 明文禁止项）。
 * 验证：builtInStrategyIds() 每个 token 经 generateWithBuiltInStrategy
 * 可解析出真实现（零抛出）并产出非空批、批首为基线空补丁——"登记而
 * 不可解析/不可产出"即空占位违约，本用例直接翻红。
 */
TEST(OptGeneratorContract, RegisteredVocabularyHasNoEmptyPlaceholder)
{
    const auto& vocab = optimization::builtInStrategyIds();
    ASSERT_GE(vocab.size(), 1U) << "首版词表至少含 seeded-lhs（§8.2 表）";

    for (const auto& token : vocab) {
        const auto config = makeMinimalConfig(std::string(token));
        GenerationBatch batch;
        EXPECT_NO_THROW(batch = optimization::generateWithBuiltInStrategy(
                            std::string(token), makeCtx(config)))
            << "词表 token 必须可解析出真实现（空占位违约）: " << token;
        EXPECT_FALSE(batch.candidates.empty())
            << "token 解析后必须产出含基线的候选批（NFR-MNT-04）: " << token;
        if (!batch.candidates.empty()) {
            EXPECT_TRUE(batch.candidates.front().items.empty())
                << "批首必须为基线空补丁（§8.2 基线恒生成）: " << token;
        }
    }

    // 词表含首版两策略（§8.2 表行——接口扩展批的登记完整性）。
    const bool hasSeededLhs
        = std::find(vocab.begin(), vocab.end(), optimization::kStrategySeededLhs)
          != vocab.end();
    const bool hasBaselinePerturb
        = std::find(vocab.begin(), vocab.end(), optimization::kStrategyBaselinePerturb)
          != vocab.end();
    EXPECT_TRUE(hasSeededLhs);
    EXPECT_TRUE(hasBaselinePerturb);
}

/**
 * OptGeneratorContract.StrategySwitchGoesThroughConfigOptReferenceAndIdentity
 * 依据：§8.2（扩展方式＝config.opt 引用——策略 token 是配置面引用键）＋
 * §4.5（策略进 config.opt canonical ⇒ 进 sliceId ⇒ 缓存失效映射）。
 * 验证：①strategyId=baseline-perturb 的配置通过 validateConfiguration 且
 * canonical 序列化成功（策略引用面真实生效——不接受而拒绝即接口扩展未
 * 落地）；②同配置仅切换 strategyId ⇒ canonical 字节不同（策略是身份要素
 * ——缓存键随策略切换失效）；③canonical 字节内含策略 token 字节（引用
 * 被编码进身份）。
 */
TEST(OptGeneratorContract, StrategySwitchGoesThroughConfigOptReferenceAndIdentity)
{
    const auto lhs = makeMinimalConfig(std::string(optimization::kStrategySeededLhs));
    const auto perturb = makeMinimalConfig(std::string(optimization::kStrategyBaselinePerturb));

    // ① 校验与序列化面：baseline-perturb 引用被 config.opt 链真实接受。
    EXPECT_NO_THROW(optimization::validateConfiguration(perturb));
    std::vector<std::uint8_t> perturbCanonical;
    EXPECT_NO_THROW(perturbCanonical = optimization::canonicalizeRunConfiguration(perturb));
    const auto lhsCanonical = optimization::canonicalizeRunConfiguration(lhs);
    ASSERT_FALSE(perturbCanonical.empty());

    // ② 策略切换 ⇒ canonical 字节不同（策略进身份——§4.5 缓存失效前提；
    //    两 token 长度不同〔23 vs 29 字节〕，长度前缀编码下总长随之不同
    //    ——只断言字节序列不等，不断言等长）。
    EXPECT_NE(lhsCanonical, perturbCanonical);

    // ③ token 字节在 canonical 内（引用被编码——长度前缀＋UTF-8 字节）。
    const std::string token(optimization::kStrategyBaselinePerturb);
    const auto it = std::search(perturbCanonical.begin(), perturbCanonical.end(),
                                token.begin(), token.end());
    EXPECT_NE(it, perturbCanonical.end())
        << "策略 token 未出现在 config.opt canonical 字节中";
}

/**
 * OptGeneratorContract.UnknownStrategyRejectedByValidationAndDispatch
 * 依据：§8.2（词表只登记已实现策略）＋NFR-COR-03（未登记 token 不静默
 * 接受——接受而按缺省策略执行即静默错配）。
 * 验证：未登记 token 在校验面（validateConfiguration 检查⑨）与分派面
 * （generateWithBuiltInStrategy）双面同拒、同稳定码（OPT-INPUT-VALID）、
 * 消息均含已实现词表全文（ERR-01 定位纪律）——两面对同一词表单点。
 */
TEST(OptGeneratorContract, UnknownStrategyRejectedByValidationAndDispatch)
{
    // 校验面拒绝。
    auto config = makeMinimalConfig(std::string(optimization::kStrategySeededLhs));
    config.strategyId = "opt.strategy.not-registered";
    try {
        optimization::validateConfiguration(config);
        FAIL() << "未登记 token 未被校验面拒绝";
    } catch (const optimization::OptimizationError& e) {
        EXPECT_EQ(e.stableCode(), std::string(optimization::kOptInputInvalid));
        EXPECT_NE(std::string(e.what()).find("opt.strategy.baseline-perturb"),
                  std::string::npos)
            << "校验拒绝消息应含已实现词表全文: " << e.what();
    }

    // 分派面拒绝（绕过校验直调的第二道防线——同词表同码）。
    const auto valid = makeMinimalConfig(std::string(optimization::kStrategySeededLhs));
    try {
        optimization::generateWithBuiltInStrategy("opt.strategy.not-registered",
                                                  makeCtx(valid));
        FAIL() << "未登记 token 未被分派面拒绝";
    } catch (const optimization::OptimizationError& e) {
        EXPECT_EQ(e.stableCode(), std::string(optimization::kOptInputInvalid));
        EXPECT_NE(std::string(e.what()).find("opt.strategy.seeded-lhs"),
                  std::string::npos);
    }
}
