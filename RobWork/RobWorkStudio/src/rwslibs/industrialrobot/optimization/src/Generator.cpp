/**
 * @file   Generator.cpp
 * @brief  搜索策略扩展点实现（Generator.hpp 的实现 TU）——seeded-lhs 薄
 *         适配器（委托 WP-20-T06 单点生成函数）、baseline-perturb 单因子
 *         ±Δ 邻域枚举、内置词表聚合与分派单点。
 *
 * 设计依据：见 Generator.hpp 文件头（§8.2/§8.5/§12.2/附录 C 表①——本
 * TU 不重复登记，只补实现级细节注释）。
 *
 * 线程约束：全部纯函数/无状态——可重入（头文件契约行）。
 * 确定性：seeded-lhs 路径随机性唯一来自 splitmix64（被委托函数内自持）；
 *   baseline-perturb 路径零随机项（确定性轮转调度）。
 */

#include <sdurws/ird/optimization/Generator.hpp>

#include <algorithm>
#include <cmath>
#include <utility>

#include <sdurws/ird/optimization/DiagCodes.hpp>  // kOptInputInvalid——稳定诊断码
#include <sdurws/ird/optimization/Types.hpp>      // OptimizationError——域异常
#include <sdurws/ird/optimization/Variable.hpp>   // VariableBinding/VariableKind
                                                  //  ——扰动维度判定面

namespace sdurws::ird::optimization {

// =====================================================================
// 内置策略一：seeded-lhs 适配器（§8.2 表第一行）
// =====================================================================

std::string SeededLhsGenerator::strategyId() const
{
    // token 常量原址在 EvaluatorPorts.hpp（WP-20-T06 落位点零迁移——
    // canonical 黄金钉扎与既有测试引用不动）。
    return std::string(kStrategySeededLhs);
}

GenerationBatch SeededLhsGenerator::generateNextBatch(const GenerationContext& ctx)
{
    // 取消位先行（§12.2 @cancellation——空批＋cancelled，不产出部分批；
    // 生成批是不可分的确定性整体）。
    if (ctx.cancellationRequested) {
        GenerationBatch batch;
        batch.cancelled = true;
        return batch;
    }
    // 单批策略代序语义：第 0 代产出全部邻域/采样候选；>0 代已无候选
    // （多代推进消费归 §8.5/WP-21-T03——本批不伪造"再来一代"的语义）。
    if (ctx.generationIndex > 0U) {
        GenerationBatch batch;
        batch.exhausted = true;
        return batch;
    }
    // 候选批逐字节委托 WP-20-T06 生成函数（单一实现点——splitmix64/层置换/
    // 采样域收缩/基线恒首全部在其内，本类零算法复制零行为漂移）。
    GenerationBatch batch;
    batch.candidates = generateSeededLhsCandidates(ctx.config);
    batch.exhausted = true;  // 全批一次产出——单批策略
    return batch;
}

// =====================================================================
// 内置策略二：baseline-perturb 邻域扰动（§8.2 表第二行）
// =====================================================================

void validateBaselinePerturbParams(const BaselinePerturbParams& params)
{
    // 检查序固定（确定性首错）；全部不静默修正（NFR-COR-03）。消息含实际
    // 值与合法域（ERR-01 比较型定位纪律）。
    // ① 有限性：NaN/Inf 无配置意义（浮点比较失去序语义）。
    if (!std::isfinite(params.perturbRatio)) {
        throw OptimizationError(
            kOptInputInvalid,
            "OPT-INPUT-INVALID: baseline-perturb perturbRatio 非有限（实际值 "
                + std::to_string(params.perturbRatio)
                + "）——扰动比例须为有限数");
    }
    // ② 下界：0＝无扰动（批退化为基线自身重复——无搜索语义），负值方向
    //    语义颠倒；合法域自正数起。
    if (params.perturbRatio <= 0.0) {
        throw OptimizationError(
            kOptInputInvalid,
            "OPT-INPUT-INVALID: baseline-perturb perturbRatio="
                + std::to_string(params.perturbRatio)
                + "（合法域 (0, 0.04]——0 无扰动语义，负值非法）");
    }
    // ③ 上界：4× 默认封顶（0.04——REQUIREMENTS 附录 C《P-04 冻结数值表》
    //    表①"工程策略可放宽上界 ±4%（4×默认封顶）"冻结档位；超档位配置
    //    不静默接受）。
    if (params.perturbRatio > kBaselinePerturbMaxRatio) {
        throw OptimizationError(
            kOptInputInvalid,
            "OPT-INPUT-INVALID: baseline-perturb perturbRatio="
                + std::to_string(params.perturbRatio) + "（合法域 (0, 0.04]——"
                "上界为附录 C 表①冻结放宽档位 4×默认封顶）");
    }
}

BaselinePerturbGenerator::BaselinePerturbGenerator(BaselinePerturbParams params)
    : m_params(params)
{
}

std::string BaselinePerturbGenerator::strategyId() const
{
    return std::string(kStrategyBaselinePerturb);
}

GenerationBatch BaselinePerturbGenerator::generateNextBatch(const GenerationContext& ctx)
{
    // 第 1 步：防御复验（配置面＋策略参数面——生成器是两者共同的消费入口；
    // 绕过 validateConfiguration 直调本接口也拦截——最后防线，与
    // generateSeededLhsCandidates/canonicalizeRunConfiguration 同款纪律）。
    validateConfiguration(ctx.config);
    validateBaselinePerturbParams(m_params);

    // 第 2 步：取消位（空批＋cancelled——§12.2 取消语义）。
    if (ctx.cancellationRequested) {
        GenerationBatch batch;
        batch.cancelled = true;
        return batch;
    }

    // 第 3 步：代序（单批策略——邻域在第 0 代完整枚举）。
    if (ctx.generationIndex > 0U) {
        GenerationBatch batch;
        batch.exhausted = true;
        return batch;
    }

    // 第 4 步：扰动维度集＝未锁定且授权的连续/量化绑定（§8.2"锁定集外
    // 扰动"/OPT-02——锁定绑定出现在补丁即拒绝，生成域预先排除）；枚举/
    // 离散器件维度不在扰动域（无连续邻域语义——±Δ 无定义；登记于头文件
    // 类注）；bindingId 字典序＝确定性维度序。
    std::vector<const VariableBinding*> dims;
    dims.reserve(ctx.config.variables.size());
    for (const auto& b : ctx.config.variables) {
        if (b.locked || !b.authorized) {
            continue;  // 锁定/未授权维度不扰动（§5.4——出现在补丁即拒绝）
        }
        if (b.kind == VariableKind::Continuous || b.kind == VariableKind::Quantized) {
            dims.push_back(&b);
        }
    }
    std::sort(dims.begin(), dims.end(),
              [](const VariableBinding* a, const VariableBinding* b) {
                  return a->bindingId < b->bindingId;
              });

    GenerationBatch batch;

    // 第 5 步：基线候选恒为批首元素（§8.2"基线候选……总是生成并参与
    // 评估"/OPT-11——空补丁有确定身份，label 不参与身份 I-OPT-8）。
    CandidatePatch baseline;
    baseline.label = "baseline";
    batch.candidates.push_back(std::move(baseline));

    // 第 6 步：预算与邻域规模。非基线候选上限 n＝maxCandidates−1（§8.2
    // "生成器受 OptimizationBudget 约束"；maxCandidates≥1 已校验无下溢）；
    // 邻域总量＝2m（m 维各 ±Δ 一点）。n==0（预算仅基线）或 m==0（无连续/
    // 量化授权维度）均为合法退化——批＝仅基线，exhausted 如实置位。
    const std::uint32_t n = ctx.config.budget.maxCandidates - 1U;
    const std::size_t m = dims.size();
    if (n == 0U || m == 0U) {
        batch.exhausted = true;
        return batch;
    }

    // 第 7~8 步：OFAT ±Δ 确定性轮转调度——第 i 个邻域候选取维度 v＝i mod m、
    // 方向 sign＝第 0 轮（i/m==0）正向、第 1 轮负向；调度上限 2m（全部
    // (维度,方向) 对）。逐点做合法性预检（有限/界内/正性词表位），不合法
    // 方向跳过不生成（邻域枚举语义即"合法邻点"——跳过是采样设计而非静默
    // 纠错，登记于头文件算法第 7 步）；收集满 n 个或调度耗尽即止。
    std::uint32_t collected = 0;
    for (std::uint32_t i = 0; i < 2U * m && collected < n; ++i) {
        const VariableBinding& b = *dims[i % m];
        const int sign = (i < m) ? 1 : -1;  // 第 0 轮 +Δ、第 1 轮 −Δ（轮转序）

        // 单步扰动幅度 Δ_v（§8.2 表第二行"步长/比例入 config"）：
        //   Quantized→绑定 step（网格邻点——步长字面承载于 config.opt
        //   variables 内）；Continuous→perturbRatio×行程（比例承载于装配期
        //   配置面 BaselinePerturbParams，默认 0.01＝附录 C 表①冻结值）。
        // step 语义由 VariableBinding 契约保证：Quantized 恒 >0（Continuous
        // 恒 0 不进本分支）；量化绑定的防御位（step≤0）属研究定义非法，
        // 交由补丁构造/绑定校验面拒绝，此处不重复判定（单一职责）。
        const double delta = (b.kind == VariableKind::Quantized)
                                 ? b.step
                                 : m_params.perturbRatio * (b.upperBound - b.lowerBound);
        const double value = b.defaultValue + sign * delta;

        // 合法性预检（三查——任一不过即跳过该方向）：
        //   a) 有限：defaultValue/界值异常（NaN/Inf）时扰动值无配置语义；
        //   b) 界内：[lowerBound, upperBound] 闭区间（越界方向不是邻点）；
        //   c) 正性词表位：valueMustBePositive（I-MDL-11 传动比等——需求
        //      原文值域硬约束）维度候选值须 >0。该位定义在**词表条目**上
        //      （VariableDefinition——绑定结构无此字段），经 matchDefinition
        //      查表；未知绑定查不到条目时不预判正性（补丁构造随后按研究
        //      定义非法 fail-fast——两道防线各司其职）。
        const bool finite = std::isfinite(value);
        const bool inBounds = value >= b.lowerBound && value <= b.upperBound;
        const VariableDefinition* def
            = matchDefinition(b.bindingId, ctx.config.stage);
        const bool positive = def == nullptr || !def->valueMustBePositive
                              || value > 0.0;
        if (!finite || !inBounds || !positive) {
            continue;  // 该方向无合法邻点——跳过（采样设计，非静默纠错）
        }

        // 第 8 步：单项补丁构造（OFAT——候选恰含一个绑定项；补丁 canonical
        // 唯一编码层，量化对齐/排序/规范化复用 T03 单点）。构造抛出（如
        // defaultValue 本身越界/正性矛盾——研究定义非法）按调用方错误
        // fail-fast 传播，不跳过不吞（与 seeded-lhs 生成期口径一致）。
        PatchItem item;
        item.bindingId = b.bindingId;
        item.scalarValue = value;
        CandidatePatch patch = makeCandidatePatch(
            ctx.config.variables, ctx.config.stage, {item},
            "perturb-" + std::to_string(collected));
        batch.candidates.push_back(std::move(patch));
        ++collected;
    }

    // 第 9 步：耗尽位——收集数不足预算即邻域已全部枚举（2m 对耗尽），
    // 调用方无须再请求下一代（单批策略恒 exhausted；预算恰好收满时同样
    // 置位——邻域没有更多候选）。
    batch.exhausted = true;
    return batch;
}

// =====================================================================
// 内置词表聚合与分派单点
// =====================================================================

const std::vector<std::string_view>& builtInStrategyIds()
{
    // 词表序＝卡面 §8.2 表行序（确定性）；内部静态表——进程生存期只读共享。
    // 每个 token 都有真实实现类（上方两个 generateNextBatch 实现）——契约
    // 测试逐 token 经 generateWithBuiltInStrategy 解析钉住（NFR-MNT-04
    // "不预建空占位"的机器可断言面）。
    static const std::vector<std::string_view> kVocabulary {
        kStrategySeededLhs,
        kStrategyBaselinePerturb,
    };
    return kVocabulary;
}

GenerationBatch generateWithBuiltInStrategy(const std::string& strategyId,
                                            const GenerationContext& ctx,
                                            const BaselinePerturbParams& perturbParams)
{
    // 词表核对（与 validateConfiguration 检查⑨同源——同一 builtInStrategyIds
    // 单点）：未登记 token 在此同样拒绝（直调本函数绕过配置校验的防线），
    // 消息含已实现词表全文（ERR-01 定位纪律）。
    const auto& vocab = builtInStrategyIds();
    const bool registered = std::find(vocab.begin(), vocab.end(), strategyId)
                            != vocab.end();
    if (!registered) {
        std::string vocabText;
        for (const auto& v : vocab) {
            if (!vocabText.empty()) {
                vocabText += "／";
            }
            vocabText += std::string(v);
        }
        throw OptimizationError(
            kOptInputInvalid,
            "OPT-INPUT-INVALID: strategyId=\"" + strategyId + "\" 未登记（已实现"
            "策略词表：" + vocabText
                + "——新策略按 OPT-10 扩展点注册：实现 ICandidateGenerator＋词表"
                  "登记随单元卡增量修订留痕）");
    }

    // 分派（token 逐一比对——词表规模小，直排比对即确定性唯一路径）。
    if (strategyId == std::string(kStrategySeededLhs)) {
        return SeededLhsGenerator{}.generateNextBatch(ctx);
    }
    // baseline-perturb：携带调用方策略配置面（缺省＝冻结比例 0.01）。
    return BaselinePerturbGenerator{perturbParams}.generateNextBatch(ctx);
}

}  // namespace sdurws::ird::optimization
