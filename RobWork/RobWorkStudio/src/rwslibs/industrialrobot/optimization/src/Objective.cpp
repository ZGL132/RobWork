/**
 * @file   Objective.cpp
 * @brief  目标指标配置与八项指标分层实现（WP-20-T05）——词表物化、
 *         目标集构造校验、config.opt 目标段 canonical 序列化、默认激活
 *         分期、研究定义目标校验与三项静态指标计算。
 *
 * 设计依据：Objective.hpp 文件头（设计依据与三件事背景说明）；本文件只
 * 补实现层决策（编码布局/检查序/求和顺序等确定性约定），语义锚不重复。
 *
 * 确定性：canonical 段按 MetricId 枚举序定宽编码；计算全部为定点序纯
 * 函数——同输入同字节/同值（NFR-COR-02）。
 */

#include <sdurws/ird/optimization/Objective.hpp>

#include <cmath>
#include <cstring>
#include <stdexcept>

#include <sdurws/ird/optimization/DiagCodes.hpp>  // OPT-* 稳定码常量（kOptInputInvalid 等——
                                                  // 异常/问题条目的码值唯一书写点）

namespace sdurws::ird::optimization {

// 词表长度与 MetricId 枚举值域一致性（位图编码与槽位寻址的前提——词表
// 表尾追加时同步本断言与 kMetricCount，NFR-MNT-03 单点约束）。
static_assert(static_cast<std::size_t>(MetricId::MinDriveMargin) == kMetricCount - 1,
              "MetricId 枚举值域须与 kMetricCount（八项）一致");

// =====================================================================
// 局部辅助
// =====================================================================

namespace {

/// 双精度位模式小端写入（canonical 编码原语——与 CandidatePatch.cpp 的
/// putF64Le 同款平台无关形态：经 uint64 位模式移位逐字节低位在前写出，
/// 格式契约不依赖平台字节序，跨平台重放可校验；位模式拷贝不经数值转换
/// ——保留 IEEE 754 位形，NFR-COR-02）。
void putF64Le(std::vector<std::uint8_t>& out, double v)
{
    std::uint64_t bits = 0;
    static_assert(sizeof(bits) == sizeof(v), "double 必须 64 位（IEEE 754）");
    std::memcpy(&bits, &v, sizeof(bits));  // 位模式拷贝（非数值转换）
    for (int i = 0; i < 8; ++i) {
        out.push_back(static_cast<std::uint8_t>((bits >> (8 * i)) & 0xFFu));
    }
}

/// 容差两分量是否合法（≥0 且有限——makeObjectiveSet 与校验复验共用）。
bool toleranceValid(const core::Tolerance& t) noexcept
{
    return std::isfinite(t.relative) && std::isfinite(t.absolute)
        && t.relative >= 0.0 && t.absolute >= 0.0;
}

/// 目标集自身合法性检查（空/重复/容差非法）——返回首条中文违规说明；
/// 空串＝合法（makeObjectiveSet 与 validateObjectives 共用同一判定面，
/// 防两处口径漂移）。
std::string firstSetViolation(const ObjectiveSet& objectives)
{
    // ① 空集：无比较基础（支配判定在空目标下退化为"全互不支配"，
    // Pareto 无意义——研究定义配置错误，fail-fast/拒绝）。
    if (objectives.entries.empty()) {
        return "激活目标集为空——研究定义无比较基础（至少激活一项指标）";
    }
    // ② 重复 MetricId：同一指标两条目标语义未定义（支配判定与排序键
    // 均无法确定）。O(n²) 两两比对——目标集规模 ≤8，无性能顾虑。
    for (std::size_t i = 0; i < objectives.entries.size(); ++i) {
        for (std::size_t j = i + 1; j < objectives.entries.size(); ++j) {
            if (objectives.entries[i].metricId == objectives.entries[j].metricId) {
                return "激活目标重复："
                     + std::string(toToken(objectives.entries[i].metricId));
            }
        }
    }
    // ③ 容差合法性（防御性复验——makeObjectiveSet 已挡，防绕过构造入口
    // 直接聚合初始化 ObjectiveSet 的路径；负/非有限容差破坏序语义）。
    for (const auto& entry : objectives.entries) {
        if (!toleranceValid(entry.tolerance)) {
            return "支配容差非法（须 ≥0 且有限）："
                 + std::string(toToken(entry.metricId));
        }
    }
    return {};
}

/// 指标在 MetricId 枚举序中的位号（词表序＝枚举值——位图编码的 bit 位）。
std::size_t metricBit(MetricId id) noexcept
{
    return static_cast<std::size_t>(id);
}

}  // namespace

// =====================================================================
// 指标词表物化（卡 §7.1 表逐行——字段落值依据见 Objective.hpp 结构注）
// =====================================================================

std::string_view toToken(MetricDirection d) noexcept
{
    // 方向稳定 token（导出/候选表列头书写面——冻结）。
    switch (d) {
    case MetricDirection::Minimize:
        return "min";
    case MetricDirection::Maximize:
        return "max";
    }
    // 不可达分支（同 Types.cpp token 函数口径——防御式空串，不吞错）。
    return {};
}

MetricDirection metricDirectionOf(MetricId id) noexcept
{
    // 方向词表的实现单点（§7.2"方向在 MetricDefinition 词表冻结"——
    // metricDefinitions() 的 direction 列与 Pareto 支配/排序热路径共用
    // 本 switch，两处消费一个来源，防口径漂移）。
    switch (id) {
    case MetricId::Envelope:
    case MetricId::StructuralMass:
    case MetricId::CycleTime:
    case MetricId::DeviceCost:
    case MetricId::DeviceMass:
    case MetricId::JointPositiveWork:
        return MetricDirection::Minimize;  // 包络/质量/节拍/成本/器件质量/正机械功＝min
    case MetricId::MinJointMargin:
    case MetricId::MinDriveMargin:
        return MetricDirection::Maximize;  // 关节裕量/驱动裕量＝max
    }
    // 不可达分支（防御式——返回 min 不吞错语义：枚举封闭，新值表尾追加
    // 时必须同步本表）。
    return MetricDirection::Minimize;
}

std::vector<MetricDefinition> metricDefinitions()
{
    // 八项全量——顺序＝MetricId 枚举序（§7.1 表行序）；逐字段落值锚点：
    //   direction＝metricDirectionOf（词表方向实现单点——见其函数注）；
    //   unitToken/computableInStageB＝§7.1 表"单位/阶段可算"列逐行原值
    //   （包络单位 m 为暂定口径、成本单位空串为未冻结——P-OPT-5，见
    //   Objective.hpp MetricDefinition 注）；
    //   sourceKey＝§6.7（R1 三项＝opt-static-screen）＋§6.8 图 R2 消费链
    //   示意键（kebab 形；注册登记随 WP-21-T02——DOPT-4 不预登记）；
    //   requiredContractVersion：opt-static-screen＝1（§6.7 明文 contract
    //   Version 1）；R2 键＝0（版本未随卡面冻结——只查注册命中）。
    return {
        {MetricId::Envelope,          metricDirectionOf(MetricId::Envelope),          "m",  kOptStaticScreenKey,        1U, true},
        {MetricId::StructuralMass,    metricDirectionOf(MetricId::StructuralMass),    "kg", kOptStaticScreenKey,        1U, true},
        {MetricId::MinJointMargin,    metricDirectionOf(MetricId::MinJointMargin),    "1",  kOptStaticScreenKey,        1U, true},
        {MetricId::CycleTime,         metricDirectionOf(MetricId::CycleTime),         "s",  "trj-sequence-plan",        0U, false},
        {MetricId::DeviceCost,        metricDirectionOf(MetricId::DeviceCost),        "",   "sel-combination-check",    0U, false},
        {MetricId::DeviceMass,        metricDirectionOf(MetricId::DeviceMass),        "kg", "sel-combination-check",    0U, false},
        {MetricId::JointPositiveWork, metricDirectionOf(MetricId::JointPositiveWork), "J",  "dyn-rnea-analysis",        0U, false},
        {MetricId::MinDriveMargin,    metricDirectionOf(MetricId::MinDriveMargin),    "1",  "sel-combination-check",    0U, false},
    };
}

// =====================================================================
// 激活目标集（构造校验＋canonical 目标段）
// =====================================================================

ObjectiveSet makeObjectiveSet(const std::vector<ObjectiveEntry>& entries)
{
    // 先组集再校验（复用 validateObjectives 的同源判定面——口径不漂移）。
    ObjectiveSet set;
    set.entries = entries;
    const std::string violation = firstSetViolation(set);
    if (!violation.empty()) {
        // 调用方错误 fail-fast（§12.3）——OPT-INPUT-INVALID 码面。
        throw OptimizationError(kOptInputInvalid, "makeObjectiveSet: " + violation);
    }
    return set;
}

std::vector<std::uint8_t> canonicalizeObjectiveSegment(const ObjectiveSet& objectives)
{
    // 目标集自身合法性（同 makeObjectiveSet 判定面——序列化是身份来源，
    // 非法集绝不产字节——调用方错误 fail-fast）。
    const std::string violation = firstSetViolation(objectives);
    if (!violation.empty()) {
        throw OptimizationError(kOptInputInvalid,
                                "canonicalizeObjectiveSegment: " + violation);
    }

    // 第一步：位图字节（bit i＝MetricId 枚举值 i 被激活——卡 §4.3"按
    // MetricId 枚举序位图"；声明序不入位图，I-OPT-8 同源口径）。
    std::uint8_t bitmap = 0;
    for (const auto& entry : objectives.entries) {
        bitmap |= static_cast<std::uint8_t>(1U << metricBit(entry.metricId));
    }
    std::vector<std::uint8_t> out;
    out.reserve(1 + 17 * objectives.entries.size());
    out.push_back(bitmap);

    // 第二步：逐激活目标写容差对（**按 MetricId 枚举序**遍历，非声明序
    // ——同目标集不同声明序 ⇒ 同段字节；"每目标容差三元组"＝位图激活
    // 位＋ε_rel＋ε_abs，f64 小端——见 putF64Le 注）。
    for (std::size_t bit = 0; bit < kMetricCount; ++bit) {
        const auto id = static_cast<MetricId>(bit);
        for (const auto& entry : objectives.entries) {
            if (entry.metricId == id) {
                putF64Le(out, entry.tolerance.relative);
                putF64Le(out, entry.tolerance.absolute);
                break;  // 无重复（firstSetViolation 已保证）——命中即收。
            }
        }
    }
    return out;
}

// =====================================================================
// 目标校验（§7.3 四条件的静态子集——报告轨）
// =====================================================================

bool ObjectiveValidationReport::hasCode(std::string_view code) const noexcept
{
    // 逐条比对稳定码（发现问题即真——调用方定位用）。
    for (const auto& issue : issues) {
        if (issue.code == code) {
            return true;
        }
    }
    return false;
}

ObjectiveValidationReport validateObjectives(const ObjectiveSet& objectives,
                                             OptimizationStage stage,
                                             const evidence::EvaluatorRegistry* registry)
{
    ObjectiveValidationReport report;

    // ① 目标集整体合法性（空/重复/容差——与 makeObjectiveSet 同源判定面；
    // 整体性问题不带 metricToken）。
    const std::string setViolation = firstSetViolation(objectives);
    if (!setViolation.empty()) {
        ObjectiveValidationIssue issue;
        issue.code = std::string(kOptInputInvalid);
        issue.metricToken = {};
        issue.detail = setViolation;
        report.issues.push_back(std::move(issue));
        report.accepted = false;
        return report;  // 目标集非法时逐条目检查无意义——首错即返（确定性）。
    }

    // 指标词表（方向/来源键/契约版本/阶段可算性的唯一权威——词表物化）。
    const std::vector<MetricDefinition> defs = metricDefinitions();

    // ②③ 逐条目检查（声明序；同一条目先阶段锁后评估器注册——§7.3 表行序
    // ＝检查点优先序，错误不互斥时首错定位更靠前的条件）。
    for (const auto& entry : objectives.entries) {
        const MetricDefinition& def = defs[metricBit(entry.metricId)];

        // 条件 1·阶段允许（§7.3 行 1：StageB 仅 MetricId 1~3；StageD 全部
        // 八项）。违例＝OPT-STAGE-LOCKED——**不静默降级、不剔除**（§6.5：
        // 拒绝即终止该研究启动，呈现为阻塞横幅＋缺项清单）。
        if (stage == OptimizationStage::StageB && !def.computableInStageB) {
            ObjectiveValidationIssue issue;
            issue.code = std::string(kOptStageLocked);
            issue.metricToken = std::string(toToken(entry.metricId));
            issue.detail = "阶段 B 不支持激活该指标（OPT-D 专用）——研究定义"
                           "须移除该目标或切换阶段（不静默降级）";
            report.issues.push_back(std::move(issue));
            continue;  // 同条目不再查评估器（阶段已否——首错定位原则）。
        }

        // 条件 2·已注册合法评估器（§7.3 行 2：来源评估键在注册表中且契约
        // 版本匹配）。registry 为空指针＝注册表未装配——逐条目报缺失
        // （校验是报告轨，不抛）。
        if (registry == nullptr
            || registry->find(def.sourceKey) == nullptr
            || (def.requiredContractVersion != 0U
                && !registry->contractVersionMatches(
                    def.sourceKey, def.requiredContractVersion))) {
            ObjectiveValidationIssue issue;
            issue.code = std::string(kOptEvaluatorMissing);
            issue.metricToken = std::string(toToken(entry.metricId));
            issue.detail = "指标来源评估器未注册或契约版本不符（来源评估键 "
                         + std::string(def.sourceKey) + "）——检查评估器装配清单";
            report.issues.push_back(std::move(issue));
        }
    }

    report.accepted = report.issues.empty();
    return report;
}

ObjectiveSet defaultObjectives(OptimizationStage stage)
{
    // 默认激活分期（§7.3 默认激活行——REQUIREMENTS §15.0/RV-03/F-02 口径）：
    // StageB＝三项静态可算；StageD＝{节拍, 结构质量, 器件成本}。容差全零
    // （DOPT-5：默认零容差，不发明阈值——用户显式配置后才进容差支配）。
    ObjectiveSet set;
    if (stage == OptimizationStage::StageB) {
        set.entries = {
            {MetricId::Envelope,       core::Tolerance{}},
            {MetricId::StructuralMass, core::Tolerance{}},
            {MetricId::MinJointMargin, core::Tolerance{}},
        };
    } else {
        set.entries = {
            {MetricId::CycleTime,      core::Tolerance{}},
            {MetricId::StructuralMass, core::Tolerance{}},
            {MetricId::DeviceCost,     core::Tolerance{}},
        };
    }
    return set;
}

// =====================================================================
// 三项静态指标计算（投影接缝＋纯函数——语义锚见 Objective.hpp）
// =====================================================================

std::optional<double> StaticMetricResult::valueOf(MetricId id) const
{
    // metrics 按 MetricId 枚举序存储（computeStaticMetrics 契约）——按
    // 位号直接取槽位，再防越界（防御式：形状违约属调用方错误，fail-fast）。
    const std::size_t idx = metricBit(id);
    if (metrics.size() != kMetricCount || idx >= metrics.size()) {
        throw std::invalid_argument(
            "StaticMetricResult::valueOf: metrics 形状非法（须为八项全量）");
    }
    return metrics[idx].valueSi;
}

StaticMetricResult computeStaticMetrics(OptimizationStage stage,
                                        const StaticMetricFacts& facts)
{
    StaticMetricResult result;
    result.metrics.reserve(kMetricCount);

    // ---- 1. 尺寸包络（§7.1 行 1；暂定口径＝基座系 AABB 三向尺寸之和，
    //         m——P-OPT-5 登记中，文件头注③） ----
    {
        MetricComputation m;
        m.metricId = MetricId::Envelope;
        if (!facts.envelope.has_value()) {
            // 缺几何投影→"—"（数据面缺失——不显示 0、不判不可行）。
            m.gapToken = kMetricGapSourceMissing;
            m.detail = "缺候选几何投影——尺寸包络不可算（显示'—'）";
        } else {
            const EnvelopeFacts& e = *facts.envelope;
            // 三向尺寸（基座系，m）；先逐面校验（非有限/面序反转＝上游
            // 事实缺陷——NonFiniteInput 轨，不静默修正）。
            const double dx = e.xMax - e.xMin;
            const double dy = e.yMax - e.yMin;
            const double dz = e.zMax - e.zMin;
            const bool inputsFinite = std::isfinite(e.xMin) && std::isfinite(e.yMin)
                && std::isfinite(e.zMin) && std::isfinite(e.xMax)
                && std::isfinite(e.yMax) && std::isfinite(e.zMax);
            const bool boxOrdered = dx >= 0.0 && dy >= 0.0 && dz >= 0.0;
            if (!inputsFinite || !boxOrdered) {
                m.gapToken = kMetricGapNonFinite;
                m.detail = "包围盒坐标非有限或面序反转（上界须 ≥ 下界）——"
                           "上游几何事实缺陷，不静默修正";
            } else {
                // 暂定口径值：三向尺寸之和（m）。求和顺序 x→y→z 固定
                // （浮点求和的确定性约定——NFR-COR-02）。
                const double value = dx + dy + dz;
                if (std::isfinite(value)) {
                    m.valueSi = value;
                    m.detail = "暂定口径：基座系包围盒三向尺寸之和（m）——"
                               "P-OPT-5 登记中，裁决后随卡面 §7 切换";
                } else {
                    // 理论不可达（输入均有限且非负时三尺寸和必有限）——
                    // 纵深防御：溢出按非有限处理，不吞错。
                    m.gapToken = kMetricGapNonFinite;
                    m.detail = "包络计算溢出为非有限——纵深防御拦截";
                }
            }
        }
        result.metrics.push_back(std::move(m));
    }

    // ---- 2. 结构质量（§7.1 行 2；Σ 连杆 mass，kg） ----
    {
        MetricComputation m;
        m.metricId = MetricId::StructuralMass;
        if (facts.linkMasses.empty()) {
            // 无连杆事实——合成无对象→"—"。
            m.gapToken = kMetricGapSourceMissing;
            m.detail = "无连杆质量事实——结构质量不可算（显示'—'）";
        } else {
            // 逐连杆校验：任一 NotProvided→整体"—"（**不按 0 合成**——
            // NFR-COR-03/§7.1 行 2 缺失语义；求和顺序＝投影表序，固定）。
            double sum = 0.0;
            bool anyMissing = false;
            bool anyNonFinite = false;
            std::string missingSubject;
            std::string nonFiniteSubject;
            for (const auto& link : facts.linkMasses) {
                if (!link.massKg.has_value()) {
                    if (!anyMissing) {
                        anyMissing = true;
                        missingSubject = link.linkSubject;  // 首缺定位（ERR-01）
                    }
                    continue;
                }
                const double mass = *link.massKg;
                if (!std::isfinite(mass) || mass < 0.0) {
                    if (!anyNonFinite) {
                        anyNonFinite = true;
                        nonFiniteSubject = link.linkSubject;
                    }
                } else {
                    sum += mass;
                }
            }
            if (anyNonFinite) {
                m.gapToken = kMetricGapNonFinite;
                m.detail = "连杆质量非有限或为负（首见 " + nonFiniteSubject
                         + "）——物理不可能的上游事实缺陷，不静默修正";
            } else if (anyMissing) {
                m.gapToken = kMetricGapSourceMissing;
                m.detail = "连杆质量 NotProvided（首见 " + missingSubject
                         + "）——缺失不按 0 合成（NFR-COR-03），显示'—'";
            } else if (std::isfinite(sum)) {
                m.valueSi = sum;
                m.detail = "Σ 参与连杆 body.mass（kg；估算值带 ValueProvenance"
                           "标记——不包装为精确，DYN-06 同型）";
            } else {
                m.gapToken = kMetricGapNonFinite;
                m.detail = "质量合成溢出为非有限——纵深防御拦截";
            }
        }
        result.metrics.push_back(std::move(m));
    }

    // ---- 3. 最小关节裕量（§7.1 行 3；Must 工位裕量最小值——口径唯一归
    //         kinematics D-KIN-2，本单元零裕量算法） ----
    {
        MetricComputation m;
        m.metricId = MetricId::MinJointMargin;
        // 分类计数（"全部数据不足→'—'；部分不足→候选整体 DataInsufficient"
        // ——§7.1 行 3 缺失语义的两分支）。
        std::size_t presentCount = 0;   ///< 有裕量值的工位数
        std::size_t missingCount = 0;   ///< 数据不足（nullopt）的工位数
        bool anyNonFinite = false;      ///< 有值点中存在非有限
        double minValue = 0.0;          ///< 可算点最小值（presentCount>0 时有效）
        bool firstPresent = true;
        for (const auto& point : facts.pointMargins) {
            if (!point.minMargin.has_value()) {
                ++missingCount;
                continue;
            }
            const double margin = *point.minMargin;
            if (!std::isfinite(margin)) {
                anyNonFinite = true;
                continue;
            }
            if (firstPresent || margin < minValue) {
                minValue = margin;
                firstPresent = false;
            }
            ++presentCount;
        }
        if (anyNonFinite) {
            m.gapToken = kMetricGapNonFinite;
            m.detail = "工位裕量值非有限——上游评估事实缺陷，不静默修正";
        } else if (presentCount == 0 && missingCount == 0) {
            // 工位表空＝Must 点证据整体缺席→"—"（全部数据不足形态之一）。
            m.gapToken = kMetricGapSourceMissing;
            m.detail = "无 Must 工位裕量证据——最小关节裕量不可算（显示'—'）";
        } else if (presentCount == 0) {
            // 全部点数据不足→"—"（不升级候选状态——证据整体缺席的候选级
            // 判定归管线 aggregateVerdict，PA-1）。
            m.gapToken = kMetricGapSourceMissing;
            m.detail = "Must 工位全部数据不足——最小关节裕量显示'—'";
        } else if (missingCount > 0) {
            // 部分点数据不足→指标值不输出＋候选 DataInsufficient 素材标志
            // （§7.1 行 3"部分点数据不足→候选整体 DataInsufficient"——
            // 判定归管线，本头只产素材）。
            m.gapToken = kMetricGapPartialData;
            m.detail = "部分 Must 工位数据不足（缺 " + std::to_string(missingCount)
                     + "/" + std::to_string(presentCount + missingCount)
                     + "）——候选整体 DataInsufficient 素材，值不输出";
            result.partialMarginDataInsufficient = true;
        } else {
            m.valueSi = minValue;
            m.detail = "Must 工位 IK 解集关节限位裕量最小值（kin D-KIN-2 归一"
                       "化口径，无量纲）";
        }
        result.metrics.push_back(std::move(m));
    }

    // ---- 4~8. 五项 D-only 指标（§7.1 行 4~8——R1 恒"—"；不因空判动力
    //          学/器件不可行：缺失是数据面语义，绝不上升工程不可行） ----
    for (std::size_t bit = 3; bit < kMetricCount; ++bit) {
        MetricComputation m;
        m.metricId = static_cast<MetricId>(bit);
        if (stage == OptimizationStage::StageB) {
            // StageB 阶段不可算（§7.1"阶段可算"列——恒"—"，不估算冒充）。
            m.gapToken = kMetricGapStage;
            m.detail = "阶段 B 不可算（OPT-D 指标）——恒显示'—'，不参与"
                       " Pareto，不判动力学/器件不可行";
        } else {
            // StageD 可算性归 OPT-D 联合评估面（WP-21-T04）——本任务未
            // 实现，诚实登记缺失（不伪造可算、不预建输入占位）。
            m.gapToken = kMetricGapSourceMissing;
            m.detail = "OPT-D 联合评估面随 WP-21-T04 落位——本实现未承载该"
                       "指标计算输入";
        }
        result.metrics.push_back(std::move(m));
    }

    return result;
}

// =====================================================================
// 目标提供实现（全部委托自由函数——无状态，进程级共享安全）
// =====================================================================

std::vector<MetricDefinition>
OptimizationObjectiveProvider::metricDefinitions() const
{
    return optimization::metricDefinitions();
}

ObjectiveValidationReport OptimizationObjectiveProvider::validateObjectives(
    const ObjectiveSet& objectives, OptimizationStage stage,
    const evidence::EvaluatorRegistry* registry) const
{
    // 委托同自由函数（单一实现——接口与自由函数无第二套逻辑，NFR-MNT-04）。
    return optimization::validateObjectives(objectives, stage, registry);
}

}  // namespace sdurws::ird::optimization
