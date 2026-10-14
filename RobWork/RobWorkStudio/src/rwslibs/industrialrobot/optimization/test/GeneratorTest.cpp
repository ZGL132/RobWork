/**
 * @file   GeneratorTest.cpp
 * @brief  搜索策略扩展点模型测试（OptGenerators 组）——词表钉扎、seeded-lhs
 *         适配零漂移、取消/代序边界、baseline-perturb 单因子 ±Δ 邻域枚举
 *         逐面（基线恒首/步长与比例承载/锁定排除/界与正性跳过/预算与耗尽/
 *         确定性与比例敏感/参数冻结档位）——任务契约 WP-21-T06
 *         acceptance（OPT-10 接口扩展；不预建空算法占位）的模型面。
 *
 * 设计依据：
 *   - units/optimization.md §8.2（搜索策略接口扩展点——首版词表两策略；
 *     基线候选恒生成；生成器受 OptimizationBudget 约束；锁定集外扰动，
 *     步长/比例入 config）、§12.2（ICandidateGenerator 签名——strategyId()＋
 *     generateNextBatch；批间取消＝空批＋cancelled；确定性——种子/预算/
 *     编码入 config.opt canonical）、§12.3（随机性仅来自显式种子）
 *   - REQUIREMENTS 附录 C《P-04 冻结数值表》表①（扰动比例默认 0.01＝
 *     ±1%×行程、放宽上界 0.04＝4× 默认封顶——冻结采样面值；判定阈值两
 *     槽位维持待定，本套件不涉判定面）
 *   - 需求 OPT-10（D/R2）、NFR-COR-02（同输入同种子 ⇒ 等价候选集合）、
 *     NFR-COR-03（0/越档位不静默修正）、OPT-02（锁定集＝扰动排除集）、
 *     I-MDL-11（传动比 >0 值域——正性跳过面）
 *
 * 测试形态（模型测试＝直调计算库，NFR-MNT-01）：零外部依赖（无评估器
 * 注册表/无③端口——生成器是纯函数面）；绑定 token 全部匹配 §5.3 词表
 * 形态（补丁构造的词表匹配前提）；补丁逐字节比较经 canonicalize 单点。
 */

#include <sdurws/ird/optimization/Generator.hpp>

#include <sdurws/ird/optimization/CandidatePatch.hpp>
#include <sdurws/ird/optimization/DiagCodes.hpp>
#include <sdurws/ird/optimization/EvaluatorPorts.hpp>
#include <sdurws/ird/optimization/Objective.hpp>
#include <sdurws/ird/optimization/Types.hpp>
#include <sdurws/ird/optimization/Variable.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

using namespace sdurws::ird;
using optimization::BaselinePerturbGenerator;
using optimization::BaselinePerturbParams;
using optimization::CandidatePatch;
using optimization::GenerationBatch;
using optimization::GenerationContext;
using optimization::ICandidateGenerator;
using optimization::OptimizationConfiguration;
using optimization::OptimizationStage;
using optimization::SeededLhsGenerator;
using optimization::VariableBinding;
using optimization::VariableKind;

namespace {

// =====================================================================
// 构造辅助（绑定 token 全部匹配 §5.3 词表形态——补丁构造的词表匹配前提）
// =====================================================================

/// 连续绑定（DH a，m——§5.3 #2 权威字段；bounds/默认值可调）。
VariableBinding makeContinuousBinding(const std::string& id, double lower, double upper,
                                      double defaultValue)
{
    VariableBinding b;
    b.bindingId = id;
    b.kind = VariableKind::Continuous;
    b.unit = core::UnitToken::find("m").value();
    b.lowerBound = lower;
    b.upperBound = upper;
    b.step = 0.0;  // Continuous 恒 0（卡 §5.2 原文）
    b.defaultValue = defaultValue;
    b.authorized = true;
    b.locked = false;
    b.authorityFieldPath = "robot-design/joints[2]/dh/a";
    return b;
}

/// 量化绑定（DH a 词表条目的量化使用形态——词表 Continuous 允许绑定声明
/// Quantized（step>0 网格化），T03 实现口径④的类型系统承载）。
VariableBinding makeQuantizedBinding(const std::string& id, double lower, double upper,
                                     double step, double defaultValue)
{
    VariableBinding b = makeContinuousBinding(id, lower, upper, defaultValue);
    b.kind = VariableKind::Quantized;
    b.step = step;
    return b;
}

/// 已校验基线配置（三项静态目标＋指定绑定集/预算/策略）。
OptimizationConfiguration makeConfig(std::vector<VariableBinding> variables,
                                     std::uint32_t maxCandidates = 8)
{
    OptimizationConfiguration c;
    c.stage = OptimizationStage::StageB;
    c.seed = 11;
    c.budget.maxCandidates = maxCandidates;
    c.budget.maxVerifiedCandidates = 2;
    c.strategyId = std::string(optimization::kStrategySeededLhs);
    c.objectives = optimization::defaultObjectives(OptimizationStage::StageB);
    c.variables = std::move(variables);
    optimization::validateConfiguration(c);
    return c;
}

/// 把配置装进生成上下文（代序 0、无聚焦提示、无取消——逐用例覆盖）。
GenerationContext makeCtx(const OptimizationConfiguration& config)
{
    GenerationContext ctx;
    ctx.config = config;
    ctx.generationIndex = 0;
    ctx.cancellationRequested = false;
    return ctx;
}

/// 补丁逐字节规范化字节（比较经 canonical 单点——与身份同源编码层）。
std::vector<std::uint8_t> canonicalBytes(const CandidatePatch& patch)
{
    return optimization::canonicalize(patch);
}

/// 在批内查找"作用于指定绑定的非基线候选"值集合（单因子补丁——每候选恰
/// 一项；返回该项标量值）。单因子形态违例以 EXPECT 就地上报并跳过该候选
/// （非 void 函数不能用 ASSERT——gtest 断言宏的 return 语义约束）。
std::vector<double> valuesForDim(const GenerationBatch& batch, const std::string& bindingId)
{
    std::vector<double> values;
    for (std::size_t i = 1; i < batch.candidates.size(); ++i) {  // 跳过基线
        const auto& items = batch.candidates[i].items;
        EXPECT_EQ(items.size(), 1U) << "baseline-perturb 候选须为单因子补丁";
        if (items.size() == 1U && items.front().bindingId == bindingId) {
            values.push_back(items.front().scalarValue);
        }
    }
    return values;
}

}  // namespace

// =====================================================================
// 词表与接口形态（OPT-10 扩展点——首版词表＝已实现策略）
// =====================================================================

/**
 * OptGenerators.VocabularyPinnedExactlyImplementedStrategies
 * 依据：卡 §8.2 表（两行）＋NFR-MNT-04（不预建空占位）。
 * 验证：①baseline-perturb token 逐字钉扎（§8.2 表第二行原文）；②内置词表
 * 恰两 token、序＝卡面表行序；③词表即 validateConfiguration 接受集（两
 * token 配置均过校验）——词表与校验同源（无漂移面）。
 */
TEST(OptGenerators, VocabularyPinnedExactlyImplementedStrategies)
{
    // ① token 原文钉扎（§8.2 表第二行——值一经交付不得改动）。
    EXPECT_EQ(std::string(optimization::kStrategyBaselinePerturb),
              "opt.strategy.baseline-perturb");

    // ② 词表聚合面：恰两已实现策略，序＝卡面表行序。
    const auto& vocab = optimization::builtInStrategyIds();
    ASSERT_EQ(vocab.size(), 2U);
    EXPECT_EQ(vocab[0], optimization::kStrategySeededLhs);
    EXPECT_EQ(vocab[1], optimization::kStrategyBaselinePerturb);

    // ③ 词表＝校验接受集（同源——两 token 配置均通过 validateConfiguration）。
    for (const auto& token : vocab) {
        OptimizationConfiguration c = makeConfig({});
        c.strategyId = std::string(token);
        EXPECT_NO_THROW(optimization::validateConfiguration(c))
            << "词表 token 须被配置校验接受: " << token;
    }
}

/**
 * OptGenerators.SeededLhsAdapterByteIdenticalToFreeFunction
 * 依据：WP-21-T06 落位口径（seeded-lhs 薄适配器——WP-20-T06 生成单点零漂移）。
 * 验证：SeededLhsGenerator 批与既有自由函数批逐候选逐字节同 canonical 同
 * label——接口化不改变既有黄金行为（同种子确定性回归面）。
 */
TEST(OptGenerators, SeededLhsAdapterByteIdenticalToFreeFunction)
{
    const auto config = makeConfig({makeContinuousBinding("mdl.joint[2].dh.a", 0.2, 0.8, 0.5),
                                    makeQuantizedBinding("mdl.joint[3].dh.a", 0.1, 0.9, 0.02, 0.5)});

    SeededLhsGenerator gen;
    EXPECT_EQ(gen.strategyId(), std::string(optimization::kStrategySeededLhs));
    const GenerationBatch batch = gen.generateNextBatch(makeCtx(config));
    const auto direct = optimization::generateSeededLhsCandidates(config);

    ASSERT_EQ(batch.candidates.size(), direct.size());
    ASSERT_FALSE(batch.candidates.empty());
    for (std::size_t i = 0; i < direct.size(); ++i) {
        EXPECT_EQ(batch.candidates[i].label, direct[i].label);
        EXPECT_EQ(canonicalBytes(batch.candidates[i]), canonicalBytes(direct[i]))
            << "适配器逐字节漂移于自由函数（候选下标 " << i << "）";
    }
}

/**
 * OptGenerators.SingleBatchStrategiesExhaustedAndGenerationIndex
 * 依据：§12.2/@post（生成纯函数式推进）＋WP-21-T06 落位口径（单批策略——
 * 多代推进编排归 §8.5/WP-21-T03，不伪造"再来一代"语义）。
 * 验证：两内置策略第 0 代 exhausted=true；generationIndex>0 ⇒ 空批＋
 * exhausted（不产出部分批、不重复基线）。
 */
TEST(OptGenerators, SingleBatchStrategiesExhaustedAndGenerationIndex)
{
    const auto config = makeConfig({makeContinuousBinding("mdl.joint[2].dh.a", 0.2, 0.8, 0.5)});

    SeededLhsGenerator seeded;
    const GenerationBatch first = seeded.generateNextBatch(makeCtx(config));
    EXPECT_TRUE(first.exhausted);
    EXPECT_FALSE(first.cancelled);
    EXPECT_FALSE(first.candidates.empty());

    BaselinePerturbGenerator perturb;
    const GenerationBatch pfirst = perturb.generateNextBatch(makeCtx(config));
    EXPECT_TRUE(pfirst.exhausted);

    // 代序 >0：空批＋exhausted（两策略一致）。
    GenerationContext next = makeCtx(config);
    next.generationIndex = 1;
    const GenerationBatch seededNext = seeded.generateNextBatch(next);
    EXPECT_TRUE(seededNext.candidates.empty());
    EXPECT_TRUE(seededNext.exhausted);
    EXPECT_FALSE(seededNext.cancelled);
    const GenerationBatch perturbNext = perturb.generateNextBatch(next);
    EXPECT_TRUE(perturbNext.candidates.empty());
    EXPECT_TRUE(perturbNext.exhausted);
}

/**
 * OptGenerators.CancellationReturnsEmptyCancelledBatch
 * 依据：§12.2 @cancellation（返回空批＋cancelled 标记——TASK-01 同型）。
 * 验证：两内置策略在取消位下返回空批＋cancelled＝true，且不产出部分批。
 */
TEST(OptGenerators, CancellationReturnsEmptyCancelledBatch)
{
    const auto config = makeConfig({makeContinuousBinding("mdl.joint[2].dh.a", 0.2, 0.8, 0.5)});
    GenerationContext ctx = makeCtx(config);
    ctx.cancellationRequested = true;

    const GenerationBatch seeded = SeededLhsGenerator{}.generateNextBatch(ctx);
    EXPECT_TRUE(seeded.cancelled);
    EXPECT_TRUE(seeded.candidates.empty());

    const GenerationBatch perturbed = BaselinePerturbGenerator{}.generateNextBatch(ctx);
    EXPECT_TRUE(perturbed.cancelled);
    EXPECT_TRUE(perturbed.candidates.empty());
}

// =====================================================================
// baseline-perturb 邻域枚举逐面（§8.2 表第二行）
// =====================================================================

/**
 * OptGenerators.PerturbBaselineFirstAndSingleFactorSchedule
 * 依据：§8.2（基线候选恒生成——OPT-11）＋本批落位算法（OFAT 单因子 ±Δ
 * 确定性轮转：第 0 轮正向、第 1 轮负向）。
 * 验证：①批首恒为基线空补丁（label "baseline"）；②预算充足时每维度的
 * ±Δ 双向各出现一次（调度覆盖完整）；③每个非基线候选恰含一个绑定项
 * （单因子）；④调度序＝维度字典序轮转（确定性）。
 */
TEST(OptGenerators, PerturbBaselineFirstAndSingleFactorSchedule)
{
    const auto config = makeConfig(
        {makeContinuousBinding("mdl.joint[2].dh.a", 0.2, 0.8, 0.5),   // 维度 A
         makeQuantizedBinding("mdl.joint[3].dh.a", 0.1, 0.9, 0.02, 0.5)});  // 维度 B
    const GenerationBatch batch
        = BaselinePerturbGenerator{}.generateNextBatch(makeCtx(config));

    // ① 基线恒首（空补丁＋label——空补丁有确定身份）。
    ASSERT_FALSE(batch.candidates.empty());
    EXPECT_TRUE(batch.candidates.front().items.empty());
    EXPECT_EQ(batch.candidates.front().label, "baseline");

    // ② 预算 8（n=7）> 邻域 4：全邻域枚举——批大小 1+4。
    ASSERT_EQ(batch.candidates.size(), 5U);
    EXPECT_TRUE(batch.exhausted);

    // ③④ 调度序与单因子：A+、B+、A−、B−（维度字典序轮转——A<B 字典序）。
    EXPECT_EQ(batch.candidates[1].items.size(), 1U);
    EXPECT_EQ(batch.candidates[1].items.front().bindingId, "mdl.joint[2].dh.a");
    EXPECT_EQ(batch.candidates[2].items.front().bindingId, "mdl.joint[3].dh.a");
    EXPECT_EQ(batch.candidates[3].items.front().bindingId, "mdl.joint[2].dh.a");
    EXPECT_EQ(batch.candidates[4].items.front().bindingId, "mdl.joint[3].dh.a");
}

/**
 * OptGenerators.PerturbMagnitudeStepForQuantizedRatioForContinuous
 * 依据：§8.2 表第二行"步长/比例入 config"＋附录 C 表①（默认 ±1%×行程；
 * 比例默认 0.01、上界 0.04 冻结档位）。
 * 验证：①量化维度 Δ＝绑定 step（网格邻点 default±step 逐字）；②连续
 * 维度 Δ＝perturbRatio×行程（默认参数 0.01×0.6＝0.006）；③自定义比例
 * 0.03 生效（Δ＝0.018）——配置面真实参与生成。
 */
TEST(OptGenerators, PerturbMagnitudeStepForQuantizedRatioForContinuous)
{
    const auto config = makeConfig(
        {makeContinuousBinding("mdl.joint[2].dh.a", 0.2, 0.8, 0.5),        // 行程 0.6 m
         makeQuantizedBinding("mdl.joint[3].dh.a", 0.1, 0.9, 0.02, 0.5)});  // step 0.02
    const GenerationBatch batch
        = BaselinePerturbGenerator{}.generateNextBatch(makeCtx(config));

    // ① 量化维度：default±step＝0.52/0.48（Δ＝step——"步长入 config"）。
    const auto quantValues = valuesForDim(batch, "mdl.joint[3].dh.a");
    ASSERT_EQ(quantValues.size(), 2U);
    EXPECT_DOUBLE_EQ(quantValues[0], 0.52);  // +Δ（第 0 轮正向）
    EXPECT_DOUBLE_EQ(quantValues[1], 0.48);  // −Δ（第 1 轮负向）

    // ② 连续维度（默认比例 0.01）：0.5±0.01×0.6＝0.506/0.494。
    const auto contValues = valuesForDim(batch, "mdl.joint[2].dh.a");
    ASSERT_EQ(contValues.size(), 2U);
    EXPECT_DOUBLE_EQ(contValues[0], 0.506);
    EXPECT_DOUBLE_EQ(contValues[1], 0.494);

    // ③ 自定义比例 0.03：0.5±0.03×0.6＝0.518/0.482（配置面生效）。
    BaselinePerturbParams custom;
    custom.perturbRatio = 0.03;
    const GenerationBatch customBatch = BaselinePerturbGenerator{custom}.generateNextBatch(
        makeCtx(config));
    const auto customValues = valuesForDim(customBatch, "mdl.joint[2].dh.a");
    ASSERT_EQ(customValues.size(), 2U);
    EXPECT_DOUBLE_EQ(customValues[0], 0.518);
    EXPECT_DOUBLE_EQ(customValues[1], 0.482);
}

/**
 * OptGenerators.PerturbExcludesLockedUnauthorizedAndEnumeration
 * 依据：§5.4/OPT-02（锁定/未授权＝扰动排除集）＋§8.6 模式③同款排除口径
 * （枚举无连续邻域语义——±Δ 无定义，其组合空间归 seeded-lhs 均匀采样面）。
 * 验证：四绑定（连续授权/连续锁定/连续未授权/枚举授权）下，全部补丁项
 * 只触及连续授权维度——排除集零泄漏。
 */
TEST(OptGenerators, PerturbExcludesLockedUnauthorizedAndEnumeration)
{
    VariableBinding locked = makeContinuousBinding("mdl.joint[2].dh.a", 0.2, 0.8, 0.5);
    locked.locked = true;  // 锁定（§5.4——补丁出现即拒绝，生成域排除）
    VariableBinding unauthorized
        = makeContinuousBinding("mdl.joint[3].dh.a", 0.1, 0.9, 0.5);
    unauthorized.authorized = false;  // 未授权（改型默认锁定语义面）

    VariableBinding enumeration;  // §5.3 #8 材料（枚举——值域＝modeling 键集）
    enumeration.bindingId = "mdl.link[2].material";
    enumeration.kind = VariableKind::Enumeration;
    enumeration.enumValues = {"steel", "aluminum", "cast-iron", "titanium-alloy",
                              "engineering-plastic"};
    enumeration.defaultValueIndex = 0;
    enumeration.authorityFieldPath = "robot-design/links[2]/body/material";

    const auto config = makeConfig({std::move(locked), std::move(unauthorized),
                                    std::move(enumeration),
                                    makeContinuousBinding("mdl.joint[4].dh.a", 0.2, 0.8, 0.5)});
    const GenerationBatch batch
        = BaselinePerturbGenerator{}.generateNextBatch(makeCtx(config));

    // 排除集零泄漏：非基线候选全部只触及 mdl.joint[4].dh.a（2 邻域——
    // 唯一合格维度的 ±Δ）。
    ASSERT_EQ(batch.candidates.size(), 3U);
    for (std::size_t i = 1; i < batch.candidates.size(); ++i) {
        ASSERT_EQ(batch.candidates[i].items.size(), 1U);
        EXPECT_EQ(batch.candidates[i].items.front().bindingId, "mdl.joint[4].dh.a");
    }
}

/**
 * OptGenerators.PerturbSkipsInvalidDirectionsDeterministically
 * 依据：本批落位算法第 7 步（合法性预检——越界方向不是邻点，跳过是采样
 * 设计非静默纠错）。
 * 验证：默认值贴上界的维度只产 −Δ 邻点、贴下界的维度只产 +Δ 邻点（无
 * 抛出、无越界候选）；调度把跳过方向让位给后续 (维度,方向) 对（收集数
 * ＝合法邻点数）。
 */
TEST(OptGenerators, PerturbSkipsInvalidDirectionsDeterministically)
{
    // A：默认 0.79 贴近上界 0.8（+Δ=0.796 仍界内——行程 0.6、Δ=0.006）；
    // 取默认 0.796 使 +Δ=0.802>0.8 越界 → 只产 −Δ。
    const auto upperPinned = makeConfig(
        {makeContinuousBinding("mdl.joint[2].dh.a", 0.2, 0.8, 0.796)});
    const GenerationBatch upperBatch
        = BaselinePerturbGenerator{}.generateNextBatch(makeCtx(upperPinned));
    ASSERT_EQ(upperBatch.candidates.size(), 2U);  // 基线＋唯一合法邻点
    EXPECT_DOUBLE_EQ(upperBatch.candidates[1].items.front().scalarValue, 0.79);
    EXPECT_EQ(upperBatch.candidates[1].items.front().bindingId, "mdl.joint[2].dh.a");

    // B：默认 0.204 贴近下界 0.2（−Δ=0.198<0.2 越界 → 只产 +Δ）。
    const auto lowerPinned = makeConfig(
        {makeContinuousBinding("mdl.joint[2].dh.a", 0.2, 0.8, 0.204)});
    const GenerationBatch lowerBatch
        = BaselinePerturbGenerator{}.generateNextBatch(makeCtx(lowerPinned));
    ASSERT_EQ(lowerBatch.candidates.size(), 2U);
    EXPECT_DOUBLE_EQ(lowerBatch.candidates[1].items.front().scalarValue, 0.21);
}

/**
 * OptGenerators.PerturbBudgetCapAndExhaustedFlag
 * 依据：§8.2（生成器受 OptimizationBudget 约束）＋本批落位算法第 9 步
 * （邻域总量 2m<n 时批短于预算，exhausted=true）。
 * 验证：①预算小于邻域：批＝预算上限（基线＋n），调度序仍确定性（前 n
 * 个 (维度,方向) 对）；②邻域小于预算：批＝邻域总量，exhausted=true。
 */
TEST(OptGenerators, PerturbBudgetCapAndExhaustedFlag)
{
    const auto dims = [] {
        return std::vector<VariableBinding>{
            makeContinuousBinding("mdl.joint[2].dh.a", 0.2, 0.8, 0.5),
            makeQuantizedBinding("mdl.joint[3].dh.a", 0.1, 0.9, 0.02, 0.5)};
    }();

    // ① 预算 3（n=2）：批＝基线＋2（调度前两对——A+、B+），exhausted=true
    //    （单批策略恒 exhausted——预算收满同样无更多候选）。
    const auto capped = makeConfig(dims, 3);
    const GenerationBatch cappedBatch
        = BaselinePerturbGenerator{}.generateNextBatch(makeCtx(capped));
    ASSERT_EQ(cappedBatch.candidates.size(), 3U);
    EXPECT_EQ(cappedBatch.candidates[1].items.front().bindingId, "mdl.joint[2].dh.a");
    EXPECT_EQ(cappedBatch.candidates[2].items.front().bindingId, "mdl.joint[3].dh.a");
    EXPECT_TRUE(cappedBatch.exhausted);

    // ② 预算 256（n=255）> 邻域 4：批＝5（基线＋2m 全邻域），exhausted=true。
    const auto generous = makeConfig(dims, 256);
    const GenerationBatch generousBatch
        = BaselinePerturbGenerator{}.generateNextBatch(makeCtx(generous));
    ASSERT_EQ(generousBatch.candidates.size(), 5U);
    EXPECT_TRUE(generousBatch.exhausted);
}

/**
 * OptGenerators.PerturbEmptyEligibleDimsBaselineOnly
 * 依据：本批落位算法第 6 步（m==0 合法退化——如实不伪造多样性）。
 * 验证：全部绑定锁定时批＝仅基线（1 个），exhausted=true，零抛出。
 */
TEST(OptGenerators, PerturbEmptyEligibleDimsBaselineOnly)
{
    VariableBinding locked = makeContinuousBinding("mdl.joint[2].dh.a", 0.2, 0.8, 0.5);
    locked.locked = true;
    const auto config = makeConfig({std::move(locked)});
    const GenerationBatch batch
        = BaselinePerturbGenerator{}.generateNextBatch(makeCtx(config));
    ASSERT_EQ(batch.candidates.size(), 1U);
    EXPECT_TRUE(batch.candidates.front().items.empty());
    EXPECT_EQ(batch.candidates.front().label, "baseline");
    EXPECT_TRUE(batch.exhausted);
}

/**
 * OptGenerators.PerturbDeterminismAndRatioSensitivity
 * 依据：NFR-COR-02（同输入 ⇒ 等价候选集合）＋头文件确定性契约（同 (ctx,
 * 构造参数) ⇒ 同批；本策略零随机自由度——种子变化改变身份不改变批）。
 * 验证：①同 config 同参数双跑逐字节同批；②不同比例双跑批不同（配置面
 * 参与生成的反证面）；③换种子（seed 11→12）批逐字节不变（无随机项）。
 */
TEST(OptGenerators, PerturbDeterminismAndRatioSensitivity)
{
    const auto config = makeConfig(
        {makeContinuousBinding("mdl.joint[2].dh.a", 0.2, 0.8, 0.5),
         makeQuantizedBinding("mdl.joint[3].dh.a", 0.1, 0.9, 0.02, 0.5)});

    // ① 同 config 同参数：逐字节同批。
    const GenerationBatch a = BaselinePerturbGenerator{}.generateNextBatch(makeCtx(config));
    const GenerationBatch b = BaselinePerturbGenerator{}.generateNextBatch(makeCtx(config));
    ASSERT_EQ(a.candidates.size(), b.candidates.size());
    for (std::size_t i = 0; i < a.candidates.size(); ++i) {
        EXPECT_EQ(canonicalBytes(a.candidates[i]), canonicalBytes(b.candidates[i]));
    }

    // ③ 种子变化（身份面）不改变批（确定性邻域枚举无随机项）。
    auto reseeded = config;
    reseeded.seed = 12;
    optimization::validateConfiguration(reseeded);
    const GenerationBatch c = BaselinePerturbGenerator{}.generateNextBatch(makeCtx(reseeded));
    ASSERT_EQ(c.candidates.size(), a.candidates.size());
    for (std::size_t i = 0; i < a.candidates.size(); ++i) {
        EXPECT_EQ(canonicalBytes(c.candidates[i]), canonicalBytes(a.candidates[i]));
    }

    // ② 比例变化：批不同（反证——参数真实参与生成，非常量折叠）。
    BaselinePerturbParams wider;
    wider.perturbRatio = 0.04;  // 冻结放宽上界档位（合法）
    const GenerationBatch d = BaselinePerturbGenerator{wider}.generateNextBatch(makeCtx(config));
    ASSERT_EQ(d.candidates.size(), a.candidates.size());
    bool anyDifferent = false;
    for (std::size_t i = 0; i < a.candidates.size(); ++i) {
        if (canonicalBytes(d.candidates[i]) != canonicalBytes(a.candidates[i])) {
            anyDifferent = true;
        }
    }
    EXPECT_TRUE(anyDifferent) << "比例变化未改变任何候选——配置面失效";
}

/**
 * OptGenerators.PerturbParamsFrozenRatioBounds
 * 依据：附录 C 表①（默认 0.01、放宽上界 0.04＝4× 封顶——冻结档位）＋
 * NFR-COR-03（不静默修正）。
 * 验证：0.01/0.04 接受；0（无扰动语义）/负值/0.05（超冻结档位）/NaN 全部
 * kOptInputInvalid 拒绝（校验在生成入口统一执行——轻量构造允许后配置）。
 */
TEST(OptGenerators, PerturbParamsFrozenRatioBounds)
{
    const auto config = makeConfig({makeContinuousBinding("mdl.joint[2].dh.a", 0.2, 0.8, 0.5)});
    const auto ctx = makeCtx(config);

    // 合法档位：默认 0.01 与放宽上界 0.04。
    EXPECT_NO_THROW(BaselinePerturbGenerator{}.generateNextBatch(ctx));
    const BaselinePerturbParams atCap{optimization::kBaselinePerturbMaxRatio};
    EXPECT_NO_THROW(BaselinePerturbGenerator{atCap}.generateNextBatch(ctx));

    // 非法档位：0／负值／超上界 0.05／NaN——全部拒绝且稳定码正确。
    const std::vector<double> illegal = {0.0, -0.01, 0.05,
                                         std::numeric_limits<double>::quiet_NaN()};
    for (const double ratio : illegal) {
        BaselinePerturbParams bad;
        bad.perturbRatio = ratio;
        try {
            BaselinePerturbGenerator{bad}.generateNextBatch(ctx);
            FAIL() << "perturbRatio=" << ratio << " 未被拒绝";
        } catch (const optimization::OptimizationError& e) {
            EXPECT_EQ(e.stableCode(), std::string(optimization::kOptInputInvalid));
        }
    }
}

/**
 * OptGenerators.BuiltInDispatchResolvesEachTokenAndRejectsUnknown
 * 依据：§8.2（扩展点＝注册新 ICandidateGenerator 实现——strategyId 登记＋
 * config.opt 引用）＋NFR-MNT-04（词表零空占位——每个登记 token 可解析出
 * 真实现并产出含基线的批）。
 * 验证：①generateWithBuiltInStrategy 对两 token 均可解析（零抛出）且产出
 * 基线批；②未知 token 拒绝（kOptInputInvalid＋消息含已实现词表全文）。
 */
TEST(OptGenerators, BuiltInDispatchResolvesEachTokenAndRejectsUnknown)
{
    const auto config = makeConfig({makeContinuousBinding("mdl.joint[2].dh.a", 0.2, 0.8, 0.5)});
    const auto ctx = makeCtx(config);

    // ① 逐 token 解析直证（词表完备性——"登记而不可解析"即空占位违约）。
    for (const auto& token : optimization::builtInStrategyIds()) {
        const GenerationBatch batch = optimization::generateWithBuiltInStrategy(
            std::string(token), ctx);
        ASSERT_FALSE(batch.candidates.empty()) << "token 解析后未产出批: " << token;
        EXPECT_TRUE(batch.candidates.front().items.empty())
            << "token 批首须为基线空补丁: " << token;
    }

    // ② 未知 token：校验与分派双面同拒（消息含词表全文——ERR-01 定位）。
    try {
        optimization::generateWithBuiltInStrategy("opt.strategy.unknown", ctx);
        FAIL() << "未知 token 未被分派面拒绝";
    } catch (const optimization::OptimizationError& e) {
        EXPECT_EQ(e.stableCode(), std::string(optimization::kOptInputInvalid));
        EXPECT_NE(std::string(e.what()).find("opt.strategy.seeded-lhs"),
                  std::string::npos)
            << "拒绝消息应含已实现词表全文: " << e.what();
        EXPECT_NE(std::string(e.what()).find("opt.strategy.baseline-perturb"),
                  std::string::npos);
    }
}
