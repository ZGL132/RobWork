/**
 * @file   EvaluatorPorts.cpp
 * @brief  优化批量编排与缓存/种子消费面实现（WP-20-T06）——config.opt
 *         校验与 canonical 序列化（"IRDOPTC1"）、确定性候选生成（splitmix64
 *         自持 PRNG＋拉丁超立方）、Quick/Verified 两级批量编排与缓存命中
 *         消费（判定唯一经 evidence judgeCacheHit——由 execution 协调器
 *         消费，本文件零判定零存储复制）。
 *
 * 设计依据：units/optimization.md §4.3/§4.4/§4.5/§8.2/§8.3/§8.4（与头文件
 * 逐条对应）；实现口径登记单元卡 §8 增量修订（DTB §5.4）。
 *
 * 确定性来源（I-OPT-3/NFR-COR-02 全链）：①种子 ≥1 校验（0 非法不静默替换
 * ——validateConfiguration）；②随机性唯一来自自持 splitmix64（黄金常数
 * ——不依赖 STL/libc，跨平台可重放）；③候选构造经 makeCandidatePatch
 * （量化 round-half-even 对齐＋bindingId 字典序——T03 单点）；④筛选与
 * 前沿排序消费 T05 buildFront 稳定序三键（rank→目标序→CandidateId）。
 * 缓存命中只改变审计计数（性能面），不改变结论面（候选集合/状态/排序）。
 */

#include <sdurws/ird/optimization/EvaluatorPorts.hpp>

#include <sdurws/ird/core/Digest.hpp>            // core::ContentDigester——
                                                  //  config.opt 内容身份计算单点（CR-02）
#include <sdurws/ird/evidence/Dependency.hpp>    // evidence::DependencyEntry/Kind/
                                                  //  Configuration/Environment 载荷——切片条目
#include <sdurws/ird/optimization/DiagCodes.hpp> // OPT-* 码值常量（唯一书写点）
#include <sdurws/ird/optimization/Types.hpp>     // toToken(RunPhase) 等

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace sdurws::ird::optimization {
namespace {

// =====================================================================
// canonical 字节编码辅助（小端定宽——§5.2 canonical 浮点/整型纪律）
// =====================================================================

/// 追加 u16（小端）。
void appendU16(std::vector<std::uint8_t>& out, std::uint16_t v)
{
    out.push_back(static_cast<std::uint8_t>(v & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFU));
}

/// 追加 u32（小端）。
void appendU32(std::vector<std::uint8_t>& out, std::uint32_t v)
{
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFFU));
    }
}

/// 追加 u64（小端）。
void appendU64(std::vector<std::uint8_t>& out, std::uint64_t v)
{
    for (int i = 0; i < 8; ++i) {
        out.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFFU));
    }
}

/// 追加 f64（IEEE 754 位模式小端——canonical 浮点纪律：位模式非数值格式，
/// 同值同位（含 -0.0 与 0.0 的位差异——绑定边界不存在 -0.0 语义面））。
void appendF64(std::vector<std::uint8_t>& out, double v)
{
    std::uint64_t bits = 0;
    static_assert(sizeof(bits) == sizeof(v), "double 必须为 64 位（IEEE 754）");
    std::memcpy(&bits, &v, sizeof(bits));
    appendU64(out, bits);
}

/// 追加长度前缀字节串（u16 长度＋UTF-8 字节，不含终止符）。
void appendLenBytes16(std::vector<std::uint8_t>& out, std::string_view s)
{
    appendU16(out, static_cast<std::uint16_t>(s.size()));
    out.insert(out.end(), s.begin(), s.end());
}

/// 追加长度前缀字节串（u32 长度——策略 token 段用）。
void appendLenBytes32(std::vector<std::uint8_t>& out, std::string_view s)
{
    appendU32(out, static_cast<std::uint32_t>(s.size()));
    out.insert(out.end(), s.begin(), s.end());
}

// =====================================================================
// 自持 splitmix64 PRNG（确定性唯一随机源——I-OPT-3/NFR-COR-02）
// =====================================================================

/**
 * @brief splitmix64 自持伪随机数发生器状态。
 *
 * 为什么自持而不使用 std::mt19937 等：标准库发生器的算法/种子播种语义
 * 允许实现差异（跨平台/跨标准库版本不保证同输出）——确定性承诺要求
 * "同种子 ⇒ 同候选集合"跨平台成立（WP-23-T09 复现基准的前置）。splitmix64
 * 算法与黄金常数（0x9E3779B97F4A7C15 / 0xBF58476D1CE4E5B9 /
 * 0x94D049BB133111EB）为公开规范（Steele 等，FastSplittableRandom），无
 * 工程自由度；全部运算为无符号 64 位整型（溢出行为由无符号环绕定义——
 * 确定性）。
 */
struct SplitMix64 {
    std::uint64_t state = 0;  ///< 内部状态（初值＝种子，≥1 已校验）

    /// 产出下一个 64 位输出（状态推进＋三轮黄金常数混合）。
    std::uint64_t nextU64()
    {
        state += 0x9E3779B97F4A7C15ULL;  // 黄金比例常数——状态推进
        std::uint64_t z = state;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;  // 混合第 1 轮
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;  // 混合第 2 轮
        return z ^ (z >> 31);                          // 混合第 3 轮（输出雪崩）
    }

    /// 产出 [0,1) 均匀双精度（64 位输出右移 11 位——53 位尾数无损映射；
    /// 乘数 2⁻⁵³ 为精确二次幂，无舍入自由度）。
    double nextUnit()
    {
        return static_cast<double>(nextU64() >> 11) * (1.0 / 9007199254740992.0);
    }
};

// =====================================================================
// 内部辅助
// =====================================================================

/// 激活目标槽位是否全部有值（§7.4"缺失指标候选不参与支配比较"的编排侧
/// 判定——激活集来自 config.objectives）。
bool activeMetricsComplete(const StaticMetricResult& metrics, const ObjectiveSet& objectives)
{
    for (const auto& entry : objectives.entries) {
        if (!metrics.valueOf(entry.metricId).has_value()) {
            return false;
        }
    }
    return true;
}

/// 由指标事实组装八槽 FeasibleCandidate（槽序＝MetricId 枚举序——buildFront
/// @pre 的输入契约；调用方保证激活槽位完整——编排层已把缺失者判
/// DataInsufficient，此处 @pre 抛即实现缺陷红）。
FeasibleCandidate toFeasibleCandidate(const TwoStageRunRecord& record)
{
    FeasibleCandidate fc;
    fc.candidateId = record.candidateId;
    fc.isBaseline = record.isBaseline;
    fc.metricValues.reserve(kMetricCount);
    for (int i = 0; i < static_cast<int>(kMetricCount); ++i) {
        fc.metricValues.push_back(record.metrics.valueOf(static_cast<MetricId>(i)));
    }
    return fc;
}

/// Quick 筛选淘汰原因（预算线——§8.4"保守淘汰（ScreenedOut，含原因）"）。
RejectionReason makeQuickScreenRejection(OptimizationStage stage)
{
    RejectionReason r;
    r.stage = stage;
    r.sourceId = "opt.pipeline.quick-screen";   // 筛选步骤定位（编排层锚）
    r.reasonToken = std::string(kRejectQuickScreenedBudget);
    r.subject = {};                              // 集合级淘汰——无单对象定位
    r.mode = core::EvaluationMode::Quick;        // 发生模式＝Quick（§8.3 第 9 步）
    return r;
}

}  // namespace

// =====================================================================
// 配置校验与 canonical（§4.3/§4.4 I-OPT-2/§4.5）
// =====================================================================

void validateConfiguration(const OptimizationConfiguration& config)
{
    // 检查序固定（头文件注 ①~⑩——确定性首错）；全部不静默修正
    // （NFR-COR-03）。每条消息携带违例字段定位与合法域（ERR-01 定位纪律）。

    // ① schema 版本：0＝非法（版本从 1 起——schemaVersion 进 canonical）。
    if (config.schemaVersion == 0) {
        throw OptimizationError(kOptInputInvalid,
                                "OPT-INPUT-INVALID: config.schemaVersion 为 0（合法域 ≥1）"
                                "——schema 版本进 config.opt canonical，0 值不可编址");
    }
    // ② 种子：I-OPT-2 原文——seed≥1，0 非法不做静默替换（NFR-COR-03）。
    //    静默替换（如 0→1）会让两次不同意图的运行共享身份（sliceId）——
    //    缓存错误命中的直接来源，故 fail-fast。
    if (config.seed == 0) {
        throw OptimizationError(kOptInputInvalid,
                                "OPT-INPUT-INVALID: config.seed 为 0（I-OPT-2：种子合法域 ≥1，"
                                "0 非法且不做静默替换——NFR-COR-03）");
    }
    // ③④ 并行声明：R1 恒 1（§15.0"并行与检查点不作为 B 期验收承诺"）。
    //    接受 >1 而按串行执行＝静默降级（配置语义与行为不符），故显式
    //    拒绝并指向 OPT-D 落位（WP-21）。
    if (config.parallel.threadCount != 1U) {
        throw OptimizationError(kOptInputInvalid,
                                "OPT-INPUT-INVALID: config.parallel.threadCount="
                                    + std::to_string(config.parallel.threadCount)
                                    + "（阶段 B 编排恒串行——并行与检查点不作 B 期承诺"
                                      "〔REQUIREMENTS §15.0〕；非 1 值属 OPT-D/R2 预支语义，"
                                      "随 WP-21 落位，不静默降级）");
    }
    if (config.parallel.maxInFlightBatches != 1U) {
        throw OptimizationError(kOptInputInvalid,
                                "OPT-INPUT-INVALID: config.parallel.maxInFlightBatches="
                                    + std::to_string(config.parallel.maxInFlightBatches)
                                    + "（R1 恒 1——在途批派发治理归 execution/OPT-D）");
    }
    // ⑤ 预算下界：至少基线候选（§8.2 基线恒生成参与评估）。
    if (config.budget.maxCandidates < 1U) {
        throw OptimizationError(kOptInputInvalid,
                                "OPT-INPUT-INVALID: budget.maxCandidates 为 0（至少容纳基线"
                                "候选——§8.2 基线候选恒生成）");
    }
    // ⑥ 幸存集上界：≥1 且不得超过总预算（§8.4 V1"Quick 幸存集
    //    ≤maxVerifiedCandidates"的配置面）。
    if (config.budget.maxVerifiedCandidates < 1U) {
        throw OptimizationError(kOptInputInvalid,
                                "OPT-INPUT-INVALID: budget.maxVerifiedCandidates 为 0"
                                "（Verified 复核至少容纳基线席）");
    }
    if (config.budget.maxVerifiedCandidates > config.budget.maxCandidates) {
        throw OptimizationError(kOptInputInvalid,
                                "OPT-INPUT-INVALID: budget.maxVerifiedCandidates("
                                    + std::to_string(config.budget.maxVerifiedCandidates)
                                    + ") > maxCandidates("
                                    + std::to_string(config.budget.maxCandidates)
                                    + ")（幸存集上界不得超过候选总预算）");
    }
    // ⑦ 生成代数：R1 固定单批（多代搜索归 R2 策略扩展——WP-21-T06）。
    if (config.budget.maxGenerations != 1U) {
        throw OptimizationError(kOptInputInvalid,
                                "OPT-INPUT-INVALID: budget.maxGenerations="
                                    + std::to_string(config.budget.maxGenerations)
                                    + "（R1 固定 1——多代搜索属 OPT-D/R2 策略扩展）");
    }
    // ⑧ 墙钟：≥1（R1 不强制执行——NFR-PERF-06 基准口径归 WP-23-T08；
    //    0 值无配置意义）。
    if (config.budget.maxWallClockS < 1U) {
        throw OptimizationError(kOptInputInvalid,
                                "OPT-INPUT-INVALID: budget.maxWallClockS 为 0（合法域 ≥1 秒；"
                                "R1 编排不强制执行墙钟——如实登记）");
    }
    // ⑨ 策略 token：只接受已实现词表（kStrategySeededLhs——未登记 token
    //    拒绝；扩展点随 WP-21-T06 表尾追加）。
    if (config.strategyId != std::string(kStrategySeededLhs)) {
        throw OptimizationError(kOptInputInvalid,
                                "OPT-INPUT-INVALID: strategyId=\"" + config.strategyId
                                    + "\" 未登记（已实现策略词表：opt.strategy.seeded-lhs——"
                                      "搜索策略接口扩展随 WP-21-T06/OPT-10）");
    }
    // ⑩ 目标集：非空＋无重复＋容差合法（与 makeObjectiveSet 同面——目标集
    //    是 Quick 筛选与 Pareto 的支配基础，空集无比较基础）。
    if (config.objectives.entries.empty()) {
        throw OptimizationError(kOptInputInvalid,
                                "OPT-INPUT-INVALID: objectives 为空（激活目标集无比较基础——"
                                "§7.3/§7.4）");
    }
    for (std::size_t i = 0; i < config.objectives.entries.size(); ++i) {
        for (std::size_t j = i + 1; j < config.objectives.entries.size(); ++j) {
            if (config.objectives.entries[i].metricId
                == config.objectives.entries[j].metricId) {
                throw OptimizationError(kOptInputInvalid,
                                        "OPT-INPUT-INVALID: objectives 存在重复 MetricId（下标 "
                                            + std::to_string(i) + " 与 "
                                            + std::to_string(j) + "）——支配判定与排序键"
                                            "无法确定");
            }
        }
        const auto& t = config.objectives.entries[i].tolerance;
        // 容差两分量非负且有限（core::Tolerance 同源校验——负/NaN/Inf 让
        // 支配判定失去序语义；字段名以 core::Tolerance 实际成员为准）。
        const double rel = t.relative;
        const double absT = t.absolute;
        if (!std::isfinite(rel) || !std::isfinite(absT) || rel < 0.0 || absT < 0.0) {
            throw OptimizationError(kOptInputInvalid,
                                    "OPT-INPUT-INVALID: objectives 容差非法（下标 "
                                        + std::to_string(i)
                                        + "——ε_rel/ε_abs 须非负且有限）");
        }
    }
}

std::vector<std::uint8_t> canonicalizeRunConfiguration(const OptimizationConfiguration& config)
{
    // 防御复验（绕过 validateConfiguration 直调本函数也拦——最后防线）。
    validateConfiguration(config);

    std::vector<std::uint8_t> out;
    out.reserve(256);

    // [0..7] magic "IRDOPTC1"（卡 §4.3 指定魔数——ASCII 逐字节）。
    const std::string_view magic = "IRDOPTC1";
    out.insert(out.end(), magic.begin(), magic.end());

    // [8..11] u32 schemaVersion。
    appendU32(out, config.schemaVersion);

    // [12..19] 阶段 token 定宽 8 字节（左对齐补 NUL——定序定宽编码）。
    const std::string_view stageToken = toToken(config.stage);
    std::array<std::uint8_t, 8> stageField {};
    std::copy(stageToken.begin(), stageToken.end(), stageField.begin());
    out.insert(out.end(), stageField.begin(), stageField.end());

    // [20..27] u64 seed（确定性来源进身份——I-OPT-3）。
    appendU64(out, config.seed);

    // [28..35] 并行声明（R1 恒 1——值进身份，KIN-13 同型）。
    appendU32(out, config.parallel.threadCount);
    appendU32(out, config.parallel.maxInFlightBatches);

    // [36..55] 预算四字段（候选/复核/代数/墙钟——预算变化＝新输入，§4.5）。
    appendU32(out, config.budget.maxCandidates);
    appendU32(out, config.budget.maxVerifiedCandidates);
    appendU32(out, config.budget.maxGenerations);
    appendU64(out, config.budget.maxWallClockS);

    // [56..] 策略 token（u32 长度前缀＋字节——定序编码）。
    appendLenBytes32(out, config.strategyId);

    // 目标段：复用 T05 canonicalizeObjectiveSegment（位图＋每激活目标
    // ε_rel/ε_abs——声明序无关，同目标集同字节）。
    const auto objectiveSegment = canonicalizeObjectiveSegment(config.objectives);
    out.insert(out.end(), objectiveSegment.begin(), objectiveSegment.end());

    // 变量绑定表：按 bindingId 字典序排序后编码（§4.3 原文"按 bindingToken
    // 字典序"——同配置任意录入序同字节，确定性 ①）。
    std::vector<const VariableBinding*> sorted;
    sorted.reserve(config.variables.size());
    for (const auto& b : config.variables) {
        sorted.push_back(&b);
    }
    std::sort(sorted.begin(), sorted.end(),
              [](const VariableBinding* a, const VariableBinding* b) {
                  return a->bindingId < b->bindingId;
              });
    appendU32(out, static_cast<std::uint32_t>(sorted.size()));
    for (const VariableBinding* b : sorted) {
        appendLenBytes16(out, b->bindingId);
        out.push_back(static_cast<std::uint8_t>(b->kind));
        out.push_back(b->locked ? 1U : 0U);
        out.push_back(b->authorized ? 1U : 0U);
        appendF64(out, b->lowerBound);
        appendF64(out, b->upperBound);
        appendF64(out, b->step);
        appendF64(out, b->defaultValue);
        appendU32(out, b->defaultValueIndex);
        appendU32(out, static_cast<std::uint32_t>(b->enumValues.size()));
        for (const auto& ev : b->enumValues) {
            appendLenBytes16(out, ev);
        }
        // 绑定单位编码：枚举/离散绑定允许默认无效句柄（值非物理量——
        // Variable.hpp 契约），编码为空串；物理量绑定编码注册表冻结 token。
        appendLenBytes16(out, b->unit.isValid() ? b->unit.symbol() : std::string_view{});
        appendLenBytes16(out, b->authorityFieldPath);
        appendLenBytes16(out, b->diagSubject);
    }

    return out;
}

// =====================================================================
// 确定性候选生成（§8.2 seeded-lhs）
// =====================================================================

std::vector<CandidatePatch>
generateSeededLhsCandidates(const OptimizationConfiguration& config)
{
    // 第 1 步：防御复验（种子 0 等在此拦截——生成器是种子的第一消费者）。
    validateConfiguration(config);

    // 第 2 步：采样维度集＝未锁定且授权的连续/量化/枚举绑定（§8.2 改型
    // "锁定集外扰动"；DiscreteDevice 属 R2 组合空间采样——不进 R1），
    // bindingId 字典序（确定性维度序）。
    std::vector<const VariableBinding*> dims;
    dims.reserve(config.variables.size());
    for (const auto& b : config.variables) {
        if (b.locked || !b.authorized) {
            continue;  // 锁定/未授权维度不采样（§5.4——出现在补丁即拒绝）
        }
        if (b.kind == VariableKind::Continuous || b.kind == VariableKind::Quantized
            || b.kind == VariableKind::Enumeration) {
            dims.push_back(&b);
        }
    }
    std::sort(dims.begin(), dims.end(),
              [](const VariableBinding* a, const VariableBinding* b) {
                  return a->bindingId < b->bindingId;
              });

    // 第 3 步：非基线候选数 n（预算上限——§8.2"生成器受 OptimizationBudget
    // 约束"；maxCandidates≥1 已校验，无下溢）。
    const std::uint32_t n = config.budget.maxCandidates - 1U;

    std::vector<CandidatePatch> patches;
    patches.reserve(static_cast<std::size_t>(n) + 1U);

    // 第 8 步（前置落位）：基线候选恒为批首元素（§8.2——基线总是生成并
    // 参与评估；空补丁有确定身份；label 不参与身份）。
    CandidatePatch baseline;
    baseline.label = "baseline";
    patches.push_back(std::move(baseline));

    if (n == 0U) {
        return patches;  // 预算＝仅基线——合法退化
    }

    // 第 4 步：自持 splitmix64（状态＝种子——同种子同输出，跨平台确定）。
    SplitMix64 rng;
    rng.state = config.seed;

    // 第 5 步：每维度独立的 n 层层序置换（Fisher-Yates，PRNG 驱动——
    // 拉丁超立方正交性：每维度 n 个候选各占一层，空间填充）。
    std::vector<std::vector<std::uint32_t>> perms(dims.size());
    for (std::size_t d = 0; d < dims.size(); ++d) {
        auto& perm = perms[d];
        perm.resize(n);
        for (std::uint32_t i = 0; i < n; ++i) {
            perm[i] = i;  // 恒等层序初值
        }
        // Fisher-Yates 自后向前洗牌（i>n 下界防护：n==0 时循环体不执行）。
        for (std::uint32_t i = n; i-- > 1U;) {
            // j ∈ [0, i]（模减偏置存在但量级 ≤2⁻³²相对——采样均匀性足够，
            // 确定性不受影响；精确无偏混洗非需求面）。
            const std::uint32_t j = static_cast<std::uint32_t>(rng.nextU64() % (i + 1U));
            std::swap(perm[i], perm[j]);
        }
    }

    // 第 6~7 步：逐候选逐维度取值并经 makeCandidatePatch 构造（补丁
    // canonical 唯一编码层——量化对齐/排序/规范化复用 T03 单点）。
    for (std::uint32_t i = 0; i < n; ++i) {
        std::vector<PatchItem> items;
        items.reserve(dims.size());
        for (std::size_t d = 0; d < dims.size(); ++d) {
            const VariableBinding& b = *dims[d];
            const std::uint32_t layer = perms[d][i];
            const double u = rng.nextUnit();  // 层内均匀位置 [0,1)
            PatchItem item;
            item.bindingId = b.bindingId;
            if (b.kind == VariableKind::Enumeration) {
                // 枚举维度：PRNG 均匀取封闭值域下标（clamp 末位防 u≈1 时
                // 越界——确定性；组合上限保护＝值域即词表封闭集）。
                const std::size_t domain = b.enumValues.size();
                if (domain == 0) {
                    // 空值域枚举绑定属研究定义非法（validateBindings 面）——
                    // 防御式拒绝，不静默跳过（fail-fast）。
                    throw OptimizationError(
                        kOptInputInvalid,
                        "OPT-INPUT-INVALID: 绑定 " + b.bindingId
                            + " 为空值域枚举（enumValues 空）——研究定义非法");
                }
                auto idx = static_cast<std::size_t>(
                    static_cast<double>(domain) * u);
                if (idx >= domain) {
                    idx = domain - 1U;  // u 上界钳制（u<1 时理论不可达——防御）
                }
                item.enumIndex = static_cast<std::uint32_t>(idx);
            } else if (b.kind == VariableKind::Quantized) {
                // 量化维度：采样域收缩为 [lower+step, upper−step]（对齐偏差
                // ≤step/2 ⇒ 对齐后必在界内——补丁构造器 round-half-even 后
                // 验界不拒绝）；收缩域为空（区间窄于两步）时退化为绑定
                // 默认值（采样设计的确定性退化——值仍是显式默认，非静默
                // 纠错）。
                const double lo = b.lowerBound + b.step;
                const double hi = b.upperBound - b.step;
                if (lo <= hi) {
                    item.scalarValue = lo + (static_cast<double>(layer) + u)
                            / static_cast<double>(n) * (hi - lo);
                } else {
                    item.scalarValue = b.defaultValue;
                }
            } else {
                // 连续维度：层 [layer/n,(layer+1)/n) 映射到 [lower,upper]
                // 层内均匀点（u<1 ⇒ 严格小于上界端点，含端点安全）。
                item.scalarValue
                    = b.lowerBound
                    + (static_cast<double>(layer) + u) / static_cast<double>(n)
                          * (b.upperBound - b.lowerBound);
            }
            items.push_back(std::move(item));
        }
        // 第 7 步：补丁构造（锁定拒绝不可能——采样域已排除；构造抛＝研究
        // 定义与词表值域矛盾〔如正性维度配负区间〕——fail-fast 传播）。
        CandidatePatch patch = makeCandidatePatch(
            config.variables, config.stage, items, "cand-" + std::to_string(i));
        patches.push_back(std::move(patch));
    }

    return patches;
}

// =====================================================================
// 两级批量编排器（§8.4）
// =====================================================================

TwoStageEvaluationOrchestrator::TwoStageEvaluationOrchestrator(OptimizationStage stage,
                                                               TwoStageOrchestratorDeps deps)
    : m_stage(stage), m_deps(deps)
{
    // 装配校验（必填项缺失＝装配违约 fail-fast；cache 可空＝无缓存会话）。
    if (m_deps.pipeline == nullptr || m_deps.projector == nullptr
        || m_deps.paretoBuilder == nullptr) {
        throw std::invalid_argument(
            "optimization/orchestrator: pipeline/projector/paretoBuilder 为必填依赖"
            "（装配违约——fail-fast）；cache 可空（无缓存会话）");
    }
}

evidence::InputSlice
TwoStageEvaluationOrchestrator::buildCandidateSlice(const evidence::AnalysisSnapshot& snapshot,
                                                    const OptimizationConfiguration& config,
                                                    const CandidatePatch& patch) const
{
    // 每候选评估请求一个冻结切片（§4.2.2——评估请求切片；SliceBuilder 唯一
    // 合法生产者——冻结协议与身份计算复用 evidence 单点，不手填身份）。
    evidence::SliceBuilder builder;
    // 评估面：优化域聚合评估键＋契约版本 1（§6.7——键与版本进 sliceId，
    // 即缓存判定第一/三要素的身份承载）。
    builder.setEvaluation(std::string(kOptStaticScreenKey), kOptStaticScreenContractVersion);

    // 条目 1：config.opt canonical（Configuration 条目——§4.5 缓存键要素
    // "config.opt 进 sliceId"的落点：种子/线程/预算/策略/目标/变量全部
    // 在字节内）。内容身份经 core::ContentDigester 单点计算（CR-02——
    // "编码由 optimization 实现，身份由 evidence/快照体系承载"，§4.3）。
    const auto canonical = canonicalizeRunConfiguration(config);
    core::ContentDigester digester;
    digester.update(canonical.data(), canonical.size());
    evidence::DependencyEntry configEntry;
    configEntry.key = "opt.config";
    configEntry.kind = evidence::DependencyKind::Configuration;
    evidence::ConfigurationDependencyPayload configPayload;
    configPayload.configKindToken = "opt.config";
    configPayload.canonicalBytes = canonical;
    configPayload.contentIdentity.bytes = digester.finalize();
    configEntry.payload = configPayload;
    builder.addEntry(std::move(configEntry));

    // 条目 2：补丁身份（Environment 条目——§4.5 缓存键要素"CandidatePatch
    // 身份（评估请求切片条目）"）。同补丁同 config ⇒ 同 sliceId（缓存
    // 命中前提）；任一变化 ⇒ sliceId 变（失效前提）。token 词形满足
    // isValidDependencyKey（[a-z][a-z0-9.-]{2,63}）。
    evidence::DependencyEntry patchEntry;
    patchEntry.key = "opt.candidate-patch";
    patchEntry.kind = evidence::DependencyKind::Environment;
    evidence::EnvironmentDependencyPayload patchPayload;
    patchPayload.token = "opt.candidate-patch";
    patchPayload.valueToken = patchIdentity(patch).toCanonical();
    patchEntry.payload = patchPayload;
    builder.addEntry(std::move(patchEntry));

    // 冻结：build() 执行全部校验（条目语法/结构/非 Object 条目无需子集
    // 校验）并计算双层身份（sliceId/inputBaselineId 均为计算值）。
    return builder.build(snapshot);
}

TwoStageRunRecord
TwoStageEvaluationOrchestrator::evaluateCandidate(core::EvaluationMode mode, bool screeningOnly,
                                                  const TwoStageRunRequest& request,
                                                  const CandidatePatch& patch,
                                                  const evidence::InputSlice& slice,
                                                  const CandidateProjection& projection,
                                                  const evidence::CaseCoverageMatrix& coverage,
                                                  evidence::IEvaluationContext& ctx,
                                                  TwoStageRunAuditCounts& audit)
{
    // 候选身份（§4.2 公式——CandidateIdOf 经 CandidatePatch 链：同基线同
    // 补丁必同身份，跨运行稳定、天然去重）。
    const CandidateId candidateId
        = candidateIdOf(request.baselineRoot, request.baselineCv, patch);
    const bool isBaseline = patch.items.empty();  // 空补丁＝基线候选（§8.2/§5.5 ③）

    // ---- 缓存查找（§8.4 编排三步之①②：编排查找请求→消费命中结果）----
    // 判定唯一经 evidence judgeCacheHit（由 execution 协调器消费——本编排
    // 零判定复制，N8）；cache 为空＝无缓存会话（全部如实重算，不伪造命中）。
    if (m_deps.cache != nullptr) {
        execution::CacheLookupQuery query;
        query.requestedMode = mode;                          // 判定第一要素（D-13 不升降级）
        query.requestSliceId = slice.sliceId;                // 判定第二要素（切片身份）
        query.requestContractVersion = slice.evaluatorContractVersion; // 第三要素
        query.requestProfileIdentity = request.profile.contentIdentity; // 第四要素
        query.inputBaselineId = slice.inputBaselineId;       // OPT-06 键绑定携带（记录面）
        const execution::CacheLookup look = m_deps.cache->lookup(query);
        audit.cacheLookups += 1;

        if (look.guidance == execution::CacheLookup::Guidance::ShortPath) {
            // FullHit 短路径：会话底账有同键记录 ⇒ 回放不重算（进程内载荷；
            // 跨会话条目的载荷装载归 WP-20-T07 归档面——如实重算并记账，
            // 不伪造回放）。WaitForInFlightRun 形态 R1 不可达（单线程串行、
            // 无并发派发——若出现按 miss 同轨推进，注释登记）。
            audit.cacheFullHits += 1;
            // 回放查找按 (sliceId, mode) 复合键（F-640）：同候选 Quick/
            // Verified 两批共用 sliceId，底账键必须带 mode 才能取回与本
            // 请求同效力面的记录——单键查找在此会取出另一批的记录（例：
            // 上一轮 Verified 批登记的记录覆盖 Quick 记录后，Quick 回放
            // 拿到 screeningOnly=false 的记录混入 quickRecords）。键含
            // mode 后无需再核对记录字段：登记（下方）与查找共用同一 mode
            // 参数，结构性一致。
            const auto it
                = m_sessionRecords.find({slice.sliceId, mode});
            if (it != m_sessionRecords.end()) {
                TwoStageRunRecord replayed = it->second;  // 回放＝首次评估的原始记录
                replayed.cacheHit = true;                  // 复用记账（命中≠Current——
                                                           //  当前性归 evidence 另判，§4.5）
                return replayed;
            }
            audit.cacheReplayUnavailable += 1;
        } else if (look.resultVerdict.verdict
                   == evidence::CacheHitResult::Verdict::DiagnosticOnly) {
            // 诊断性读取（CON-04——部分/失败/未封账结果可读诊断不作正式
            // 命中）：正常评估，分账观测（OPT-VER-137/CON-04 观测点）。
            audit.cacheDiagnosticOnly += 1;
        } else {
            // 不兼容拒绝（D-13 双向 mode-mismatch/slice/contract/profile
            // 失配——OPT-VER-129/138 观测点）：正常评估（无隐式升降级）。
            audit.cacheIncompatible += 1;
        }
    }

    // ---- T04 管线评估（§6.2 十步——mode 差异经 input.mode，效力门禁在
    //      汇总层：Quick 记录 formalPassEligible 恒 false〔mode-not-verified〕）。
    StaticHardConstraintInput input;
    input.candidateId = candidateId;
    input.isBaseline = isBaseline;
    input.patch = patch;
    input.bindings = request.config.variables;
    input.mode = mode;
    input.baselineChainInEnabledScope = projection.baselineChainInEnabledScope;
    input.jointLimits = projection.jointLimits;
    input.snapshot = request.snapshot;
    input.slice = slice;
    input.task = request.task;
    input.caseSubset = {};  // 空＝不做工况分批（R1 单批全量——§8.3 第 5 步
                            //  "部分结果跨批汇总"归 R2 分批派发）
    input.coverage = coverage;
    input.regionCoverages = request.regionCoverages;
    input.manifest.profileId = request.profile.profileId;
    input.manifest.profileVersion = request.profile.version;
    input.manifest.profileContentIdentity = request.profile.contentIdentity; // 四元组绑定
    input.manifest.snapshotId = request.snapshot.snapshotId;
    input.manifest.sliceId = slice.sliceId;  // 清单绑定三元组对账（逐候选切片）

    CandidateEvaluationRecord eval = m_deps.pipeline->evaluate(input, ctx);

    // ---- T05 指标事实（§8.3 第 8 步——八项全量；投影事实来自候选投影缝，
    //      P-OPT-2 裁决前由调用方/测试供给）。
    const StaticMetricResult metrics = computeStaticMetrics(m_stage, projection.metricFacts);

    TwoStageRunRecord record;
    record.candidateId = candidateId;
    record.sliceId = slice.sliceId;  // 缓存键主面（审计追溯/缓存登记对账锚）
    record.isBaseline = isBaseline;
    record.patch = patch;
    record.mode = mode;
    record.screeningOnly = screeningOnly;
    record.status = eval.status;
    record.engineeringStatus = eval.engineeringStatus;
    record.formalPassEligible = eval.formalPassEligible;
    record.formalPassUnmetConditions = eval.formalPassUnmetConditions;
    record.evaluation = std::move(eval);
    record.metrics = metrics;

    // ---- 激活目标槽位缺失的 Feasible 候选 → DataInsufficient（§7.4"缺失
    //      指标候选不参与支配比较；已在管线侧排除"的编排侧执行点——T05
    //      头注④预告的落位：T04 管线不识指标，该映射在管线＋指标＋编排的
    //      组合面执行。语义仍是 evidence 判定的机械延伸：候选不完整 ⇒ 不
    //      可进 Pareto ⇒ DataInsufficient——绝不冒充 Infeasible，ERR-01
    //      "指标缺失不是失败"；partialMargin 素材已由 computeStaticMetrics
    //      承载，此处只做状态映射）。
    if (record.status == CandidateStatus::Feasible
        && !activeMetricsComplete(record.metrics, request.config.objectives)) {
        record.status = CandidateStatus::DataInsufficient;
        record.evaluation.status = CandidateStatus::DataInsufficient;  // 记录内双字段一致
    }

    // ---- 会话底账登记（缓存 FullHit 短路径的进程内回放载荷——同键后续
    //      请求命中时从此处取"首次评估的原始记录"，不重算）。键＝
    //      (sliceId, mode) 复合键（F-640——见成员注与回放查找处注）：同
    //      sliceId 的 Quick/Verified 两批记录并存不互相覆盖，回放永远取
    //      同 mode 记录。
    m_sessionRecords[{slice.sliceId, mode}] = record;
    return record;
}

// =====================================================================
// run()——两级编排主流程（§8.4 流程图的编排侧逐步实现）
// =====================================================================

TwoStageRunResult TwoStageEvaluationOrchestrator::run(const TwoStageRunRequest& request,
                                                      evidence::IEvaluationContext& ctx)
{
    // ---- 第 1 步：请求校验（调用方契约违约 fail-fast——确定性首错）。
    validateConfiguration(request.config);
    if (!request.profile.contentIdentity.isValid()) {
        throw OptimizationError(kOptInputInvalid,
                                "OPT-INPUT-INVALID: profile.contentIdentity 为保留值"
                                "（Profile 内容身份是缓存判定第四要素与清单绑定面——"
                                "须为注册权威计算值）");
    }
    if (request.profile.profileId.empty() || request.profile.version.empty()) {
        throw OptimizationError(kOptInputInvalid,
                                "OPT-INPUT-INVALID: profile.profileId/version 为空"
                                "（Profile 三元组须完整——evidence 域词表）");
    }

    TwoStageRunResult result;

    // ---- 第 2 步：Quick 批（mode=Quick——screening-only 效力面，EVI-01
    //      表 1：Quick 不得产生等价于 Verified 的正式证据）。
    result.runPhase = RunPhase::QuickScreening;
    const std::vector<CandidatePatch> patches = generateSeededLhsCandidates(request.config);
    result.audit.candidatesGenerated = static_cast<std::uint32_t>(patches.size());

    for (const auto& patch : patches) {
        // 批边界协作取消（§12.2 取消行/TASK-01）：停止派发后续候选，
        // 已回传批保留——正常取消非错误（runCompleted=false 表达，UX-03）。
        if (ctx.cancellationRequested()) {
            result.runPhase = RunPhase::Canceled;
            result.runCompleted = false;
            return result;
        }
        const evidence::InputSlice slice
            = buildCandidateSlice(request.snapshot, request.config, patch);
        const CandidateProjection projection
            = m_deps.projector->project(patch, request.config.variables);
        TwoStageRunRecord record = evaluateCandidate(
            core::EvaluationMode::Quick, /*screeningOnly=*/true, request, patch, slice,
            projection, request.quickCoverage, ctx, result.audit);
        // 审计口径（G-1 对齐——前批验收登记义务）：quickEvaluated＝"Quick 批
        // 实际评估数（命中回放不计）"——缓存 FullHit 短路径回放的记录带
        // cacheHit 标记，复用的是首次评估的原始结论，未发生"实际评估"，
        // 不重复计入评估数（回放已单计入 cacheFullHits——两计数器各记各的
        // 账，审计 CSV 与重放对账时口径唯一，AT-34）。FullHit 但会话内无
        // 载荷（cacheReplayUnavailable）时记录真实走了管线，cacheHit=false，
        // 照常计入。
        if (!record.cacheHit) {
            result.audit.quickEvaluated += 1;
        }
        result.quickRecords.push_back(std::move(record));
    }

    // ---- 第 3 步：Quick 保守筛选（§8.4 表"淘汰权"行——OPT-VER-127）。
    // 筛选输入＝Quick 批中 Feasible 候选（激活指标缺失者已在评估记账时判
    // DataInsufficient——不参与支配比较，§7.4；Infeasible/DataInsufficient/
    // EvaluationFailed 保持自有状态不冒充 ScreenedOut，§7.5 区分表）。
    std::vector<FeasibleCandidate> quickFeasible;
    for (const auto& rec : result.quickRecords) {
        if (rec.status == CandidateStatus::Feasible) {
            quickFeasible.push_back(toFeasibleCandidate(rec));
        }
    }

    std::vector<CandidateId> survivorIds;
    if (!quickFeasible.empty()) {
        // 稳定序筛选（buildFront 三键序——rank→目标声明序→CandidateId；
        // 去重保首见）。同一 Pareto builder 服务 Quick 筛选与 Verified 前沿
        // ——排序单点（确定性消费面一致，OPT-VER-131 与线程无关）。
        const ParetoFrontResult front
            = m_deps.paretoBuilder->buildFront(quickFeasible, request.config.objectives);

        // 选席规则（实现口径，单元卡增量修订登记）：基线 Quick-Feasible 时
        // 恒占一席（OPT-11"基线评估作比较基准"的编排侧保守化——比较基准
        // 不得被预算线筛掉），其余名额按稳定序取非基线候选，总席位数
        // ≤maxVerifiedCandidates（§8.4 V1 上界刚性）。
        const std::uint32_t cap = request.config.budget.maxVerifiedCandidates;
        for (const auto& entry : front.entries) {
            if (entry.isBaseline) {
                survivorIds.push_back(entry.candidateId);
                break;  // 基线在去重集中至多一条（空补丁同身份天然去重）
            }
        }
        for (const auto& entry : front.entries) {
            if (static_cast<std::uint32_t>(survivorIds.size()) >= cap) {
                break;  // 预算线（§8.4"低预算"的 Verified 席位承载）
            }
            if (entry.isBaseline) {
                continue;  // 基线席已处理
            }
            const bool already = std::any_of(survivorIds.begin(), survivorIds.end(),
                                             [&](const CandidateId& id) {
                                                 return id == entry.candidateId;
                                             });
            if (!already) {
                survivorIds.push_back(entry.candidateId);
            }
        }

        // 未入选的 Quick-Feasible → ScreenedOut＋预算线淘汰原因（追加事实
        // 不覆盖管线记录——§8.3 第 9 步逐候选逐约束逐评估器记录）。
        for (auto& rec : result.quickRecords) {
            if (rec.status != CandidateStatus::Feasible) {
                continue;  // 非 Feasible 保持自有状态（不冒充 ScreenedOut）
            }
            const bool survivor = std::any_of(survivorIds.begin(), survivorIds.end(),
                                              [&](const CandidateId& id) {
                                                  return id == rec.candidateId;
                                              });
            if (!survivor) {
                rec.status = CandidateStatus::ScreenedOut;
                rec.evaluation.status = CandidateStatus::ScreenedOut;  // 双字段一致
                rec.extraRejections.push_back(makeQuickScreenRejection(m_stage));
                result.audit.quickScreenedOut += 1;
            }
        }
    }

    // ---- 第 4 步：幸存集为空 → 搜索空（OPT-VER-120——Completed＋
    //      OPT-SEARCH-EMPTY warning 语义，非任务不可行；不产 Pareto；
    //      §4.4 状态机"QuickScreening→Completed：幸存集为空"）。
    if (survivorIds.empty()) {
        result.runPhase = RunPhase::Completed;
        result.runCompleted = true;
        result.searchEmpty = true;
        result.searchEmptyToken = std::string(kOptSearchEmpty);
        return result;
    }

    // ---- 第 5 步：Verified 复核批（mode=Verified——完整预算：全必验工况
    //      覆盖投影 EVI-02；screeningOnly=false 正式效力面）。幸存集复核
    //      按 patches 生成序推进（确定性——同候选集合同评估序）。
    result.runPhase = RunPhase::VerifiedReview;
    std::size_t verifiedCursor = 0;  // 已派发幸存者游标（取消时部分批保留）
    for (const auto& patch : patches) {
        if (verifiedCursor >= survivorIds.size()) {
            break;  // 幸存集已全复核
        }
        if (ctx.cancellationRequested()) {
            // 批边界协作取消（同 Quick 批语义——TASK-01；§4.4
            // "VerifiedReview→Canceled"）。
            result.runPhase = RunPhase::Canceled;
            result.runCompleted = false;
            return result;
        }
        const evidence::InputSlice slice
            = buildCandidateSlice(request.snapshot, request.config, patch);
        const CandidateId candidateId
            = candidateIdOf(request.baselineRoot, request.baselineCv, patch);
        const bool survivor = std::any_of(survivorIds.begin(), survivorIds.end(),
                                          [&](const CandidateId& id) {
                                              return id == candidateId;
                                          });
        if (!survivor) {
            continue;  // 非幸存者不进复核批（Quick 淘汰终局——不自动升级，
                       //  §8.4"不自动升级"行）
        }
        ++verifiedCursor;
        const CandidateProjection projection
            = m_deps.projector->project(patch, request.config.variables);
        TwoStageRunRecord record = evaluateCandidate(
            core::EvaluationMode::Verified, /*screeningOnly=*/false, request, patch, slice,
            projection, request.coverage, ctx, result.audit);
        // 审计口径（G-1 对齐——与 Quick 批同款）：verifiedEvaluated＝"Verified
        // 批实际评估数（命中回放不计）"——同键复核请求命中会话底账时不重复
        // 计入（回放单计入 cacheFullHits；AT-34 审计对账口径唯一）。
        if (!record.cacheHit) {
            result.audit.verifiedEvaluated += 1;
        }
        result.verifiedRecords.push_back(std::move(record));
    }

    // ---- 第 6 步：可行集→Pareto（仅 Verified-Feasible 且激活指标完整
    //      候选——Quick 记录绝不进入本输入的类型学屏障，EVI-01/P-EV-8）。
    std::vector<FeasibleCandidate> verifiedFeasible;
    for (const auto& rec : result.verifiedRecords) {
        if (rec.status == CandidateStatus::Feasible) {
            verifiedFeasible.push_back(toFeasibleCandidate(rec));
        }
    }
    if (!verifiedFeasible.empty()) {
        result.pareto = m_deps.paretoBuilder->buildFront(verifiedFeasible,
                                                         request.config.objectives);
    }
    if (result.pareto.feasibleIds.empty()) {
        // 复核批无可行候选 → 搜索空（OPT-VER-120 语义——Completed 正常
        // 完成，非任务不可行）。
        result.searchEmpty = true;
        result.searchEmptyToken = std::string(kOptSearchEmpty);
    }
    result.runPhase = RunPhase::Completed;
    result.runCompleted = true;
    return result;
}

}  // namespace sdurws::ird::optimization
