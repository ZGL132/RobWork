/**
 * @file   FeasibleSet.cpp
 * @brief  可行集与淘汰原因输出的实现——FeasibleSetBuilder（§10.2 分层
 *         资格判定＋§10.4 稳定排序＋EVI-02 覆盖复核）、
 *         RejectionReasonProvider（§10.3 词表唯一实现点——diagRef 回填）、
 *         SEL-06 指标计算（§17.3 裕量口径）与 sel 域 Profile 实例化
 *         （EVI-01 表 4 选型行）。
 *
 * 设计依据：
 *   - units/selection.md §10.1～§10.4（类型/空集语义/词表/稳定排序）、
 *     §9.4（EVI-02 多工况资格——任一工况数据不足整体 DataInsufficient
 *     不漏验）、§14.0（通用约定——错误两分法）、§14.6（接口签名）、
 *     §17.3（裕量＝工作点对能力的余量，含来源工况）、§13.7（reporting
 *     消费面）；REQUIREMENTS §8.1 表 4（选型行必需/建议证据项——Profile
 *     逐行实例化的内容权威）
 *   - 需求 SEL-06（可行组合＋裕量/质量/成本/来源＋逐项淘汰原因（含实际
 *     值与阈值））、SEL-05（上游组合级记录——唯一事实源）、EVI-01/EVI-02、
 *     ERR-01（比较型字段＋稳定诊断引用）、NFR-COR-01/02（确定性）、
 *     NFR-COR-03（非有限拒绝）
 *   - 任务契约 tasks/foundation/WP-19-T06.json（acceptance 1～3）
 *
 * 确定性（NFR-COR-02）：全部输出序由 stable_sort 与固定排序键决定——
 *   不依赖哈希表遍历序（查找用 map 只影响查询路径，不影响输出序）、与
 *   线程数无关；同值稳定次序＝输入序。
 */

#include <sdurws/ird/selection/FeasibleSet.hpp>

#include <sdurws/ird/selection/DiagCodes.hpp>  // reasonTokenDiagCode——diagRef 回填唯一映射

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace sdurws::ird::selection {
namespace {

// ---------------------------------------------------------------------
// 通用小工具（与 Screening.cpp/CombinationCheck.cpp 同语义——各 TU 自持
// 最小实现，避免新增跨 TU 私有头；NFR-MNT-04 不建无边界价值设施）
// ---------------------------------------------------------------------

/// 有限性判定（NaN/±Inf 一并拒绝——NFR-COR-03"非有限不静默通过"）。
bool isFiniteNumber(double v) noexcept
{
    return std::isfinite(v);
}

/// 非有限即抛（调用方契约违约 fail-fast；what 用于异常文案定位）。
void requireFinite(double v, const char* what)
{
    if (!isFiniteNumber(v)) {
        throw std::invalid_argument(std::string("可行集/淘汰原因：") + what
                                    + " 非有限（NaN/±Inf——调用方契约违约）");
    }
}

/// verdict 的稳定排序位次（§10.4 排序键①"可行/不可行"的三态展开序：
/// 可行最优 → 数据不足居中〔数据不足不是淘汰——分轨纪律〕→ 淘汰最后；
/// 注意与 VerdictKind 枚举声明序不同——显式映射，不依赖枚举数值）。
int verdictRank(VerdictKind v) noexcept
{
    switch (v) {
    case VerdictKind::Feasible:
        return 0;
    case VerdictKind::DataInsufficient:
        return 1;
    case VerdictKind::Rejected:
        return 2;
    }
    return 3;  // 防御分支（枚举封闭——不可达）。
}

/// 组合实体排序键（§10.4 排序键②~⑤的级联比较——轴序→候选稳定 ID→
/// 目录版本→组合键；全部为字典序级联，确定性，NFR-COR-02）。
///
/// ②轴序＝轴表逐元素 jointId 规范文本字典序级联（关节串联序——与上游
///   模型一致，组合轴表排列序即权威轴序）；长度不同时短者在前（词法序）。
/// ③候选稳定 ID＝逐轴 (motorModelId, gearboxModelId) 字典序级联
///   （modelId 字节序——std::string 字典序，卡 §10.4"modelId 字节序"）。
/// ④目录版本＝(catalogId, version) 字典序。
/// ⑤组合键＝64 字符小写 hex 的字典序（内容寻址键——唯一性兜底，保证
///   全序无歧义，⑦输入序由 stable_sort 保留同键子项执行序）。
bool combinationLess(const DeviceCombination& a, const DeviceCombination& b)
{
    // ②轴序＋③候选稳定 ID：逐轴级联比较（任一维度分出即返回）。
    const std::size_t n = std::min(a.axes.size(), b.axes.size());
    for (std::size_t i = 0; i < n; ++i) {
        const std::string ja = a.axes[i].jointId.toCanonical();
        const std::string jb = b.axes[i].jointId.toCanonical();
        if (ja != jb) {
            return ja < jb;  // ②轴序。
        }
        if (a.axes[i].motorModelId != b.axes[i].motorModelId) {
            return a.axes[i].motorModelId < b.axes[i].motorModelId;  // ③电机 ID。
        }
        if (a.axes[i].gearboxModelId != b.axes[i].gearboxModelId) {
            return a.axes[i].gearboxModelId < b.axes[i].gearboxModelId;  // ③减速器 ID。
        }
    }
    if (a.axes.size() != b.axes.size()) {
        return a.axes.size() < b.axes.size();  // ②轴序长度兜底（词法序）。
    }
    // ④目录版本：(catalogId, version) 字典序级联。
    if (a.catalog.catalogId != b.catalog.catalogId) {
        return a.catalog.catalogId < b.catalog.catalogId;
    }
    if (a.catalog.version != b.catalog.version) {
        return a.catalog.version < b.catalog.version;
    }
    // ⑤组合键：内容寻址键字典序（全序兜底——同键组合在构造面已去重，
    // 此处理论不可达，保留以保全序语义完整）。
    return a.id < b.id;
}

}  // namespace

// =====================================================================
// 可行集组装器（§10.2/§10.4/§9.4——实现规则见 FeasibleSet.hpp 类注三步）
// =====================================================================

SelectionRunResult FeasibleSetBuilder::build(
    const std::vector<FeasibilityRecord>& records,
    const std::vector<CaseCoverageEntry>& coverage,
    const IdentityBlock& identity,
    const std::vector<FeasibleSetEntry>& entries) const
{
    // ---- 第 1 步：对位校验（调用方契约违约 fail-fast——校验边界快速
    // 拒绝，卡 §10.2 短路边界只允许致命输入错误短路）。契约版本为 0＝
    // 未登记非法（T05 映射批版本同款语义——身份块是结果追溯面的锚）。
    if (identity.contractVersion == 0) {
        throw std::invalid_argument(
            "可行集组装：identity.contractVersion 为 0（未登记非法——调用方契约违约）");
    }
    std::unordered_map<std::string, const FeasibilityRecord*> recordById;
    recordById.reserve(records.size());
    for (const FeasibilityRecord& rec : records) {
        // 分层纪律（卡 §10.2"轴/组合/整机三层不混淆"）：组装器只接受
        // 组合级记录——轴级候选记录（T04 screenMotors/screenGearboxes
        // 产出）的汇总已在组合校核段完成（T05 轴级原因并入组合级），
        // 混入即组装前提破坏。
        if (rec.deviceKind != DeviceKind::Combination) {
            throw std::invalid_argument(
                "可行集组装：输入记录含非组合级类别（deviceKind=="
                + std::to_string(static_cast<int>(rec.deviceKind))
                + "——轴级记录须先经组合校核段汇总，卡 §10.2 分层纪律）");
        }
        if (!recordById.emplace(rec.id, &rec).second) {
            throw std::invalid_argument("可行集组装：记录键重复（" + rec.id
                                        + "——组合键唯一性前提破坏）");
        }
    }
    std::unordered_map<std::string, const FeasibleSetEntry*> entryById;
    entryById.reserve(entries.size());
    for (const FeasibleSetEntry& entry : entries) {
        if (!entryById.emplace(entry.combination.id, &entry).second) {
            throw std::invalid_argument("可行集组装：组合键重复（"
                                        + entry.combination.id + "）");
        }
    }
    // 每条记录必须引用已知组合（记录有而实体无＝无法承载可行集构成——
    // 组装前提破坏；反向（实体有而记录无）是数据类，见第 2 步合成缺口）。
    for (const FeasibilityRecord& rec : records) {
        if (entryById.count(rec.id) == 0) {
            throw std::invalid_argument("可行集组装：记录引用未知组合（"
                                        + rec.id + "——entries 缺同键组合实体）");
        }
    }

    // ---- 第 2 步：逐组合资格判定（EVI-02 不漏验）＋合成缺口记录。
    // 覆盖格索引：组合键 → 该组合的格集（按 coverage 输入序收集——格集
    // 顺序仅用于"存在性/全通过"聚合，不进入输出序）。
    std::unordered_map<std::string, std::vector<const CaseCoverageEntry*>> cellsByCombo;
    for (const CaseCoverageEntry& cell : coverage) {
        cellsByCombo[cell.combinationId].push_back(&cell);
    }

    std::vector<FeasibilityRecord> mergedRecords;
    mergedRecords.reserve(entries.size());
    for (const FeasibleSetEntry& entry : entries) {
        const auto recIt = recordById.find(entry.combination.id);
        if (recIt == recordById.end()) {
            // 数据类：无校核记录的组合（取消截断前缀/组装方漏供给——
            // §13.4"取消不发布完整可行集"）→ 合成 DataInsufficient 记录
            // （DataGap 显式标记，不伪造可行——EVI-02"不得漏验"）。
            FeasibilityRecord synth;
            synth.id = entry.combination.id;
            synth.deviceKind = DeviceKind::Combination;
            synth.candidateModelId = ModelId{};      // 组合级无单候选（T05 约定）。
            synth.axisId = core::ObjectId{};         // 组合级无单轴（全零诚实标记）。
            synth.catalog = identity.catalog;        // 追溯面＝结果身份块目录。
            synth.inputSliceId = identity.inputSliceId;
            synth.mappingId = identity.mappingSliceId;
            DataGap gap;
            gap.dimension = "feasible-set-input";
            gap.detail = "组装输入缺少该组合的校核记录（截断/未供给——不进可行集）";
            gap.caseId = CaseId{};                   // 组合级缺口与单工况无关。
            synth.gaps.push_back(std::move(gap));
            synth.verdict = VerdictKind::DataInsufficient;
            mergedRecords.push_back(std::move(synth));
            continue;
        }

        // 记录存在：先拷贝（diagRef 回填在本记录副本上执行——不改调用方
        // 输入），再按 EVI-02 复核覆盖完整性。
        FeasibilityRecord merged = *recIt->second;
        if (merged.verdict == VerdictKind::Feasible) {
            // 组合校核判可行 → 组装面独立复核覆盖矩阵（EVI-02"组合仅在
            // 全部启用必验工况通过后进入可行集；任一工况数据不足整体
            // DataInsufficient 不漏验"——防线语义：记录与格矛盾时以格
            // 为准，更严者胜）。
            const auto cellsIt = cellsByCombo.find(entry.combination.id);
            if (cellsIt == cellsByCombo.end() || cellsIt->second.empty()) {
                // 无覆盖素材＝无法证明全工况通过＝漏验风险 → 降级
                // DataInsufficient（不伪造通过——§7.2 纪律）。
                merged.verdict = VerdictKind::DataInsufficient;
                DataGap gap;
                gap.dimension = "coverage-missing";
                gap.detail = "记录判可行但资格矩阵无覆盖格（EVI-02 无法确认——降级不漏验）";
                gap.caseId = CaseId{};
                merged.gaps.push_back(std::move(gap));
            } else {
                for (const CaseCoverageEntry* cell : cellsIt->second) {
                    if (cell->verdict != VerdictKind::Feasible) {
                        // 任一格非 Pass（Fail/DataInsufficient）→ 整体
                        // 降级 DataInsufficient：不伪造原因明细（原因归
                        // 上游组合校核段产出——组装器不编造），以缺口
                        // 标记矛盾事实并阻断可行（EVI-02 红线）。
                        merged.verdict = VerdictKind::DataInsufficient;
                        DataGap gap;
                        gap.dimension = "coverage-inconsistent";
                        gap.detail = "记录判可行但工况 «" + cell->caseId
                                     + "» 覆盖格非 Pass（记录与格矛盾——以格为准降级）";
                        gap.axisId = cell->axisId;
                        gap.caseId = cell->caseId;
                        merged.gaps.push_back(std::move(gap));
                        break;  // 首个非 Pass 格已足够降级（缺口明细不再罗列——上游记录为准）。
                    }
                }
            }
        }
        mergedRecords.push_back(std::move(merged));
    }

    // ---- 第 3 步：输出装配。records 按 §10.4 排序键级联稳定排序
    // （①verdict 位次 → ②~⑤组合排序键 → ⑦输入序）；逐条原因的空
    // diagRef 回填稳定码引用（reasonTokenDiagCode——T06 批码已注册登记
    // 表；输出满足卡 §10.3"每条原因包含全部字段"）。
    std::vector<std::pair<const DeviceCombination*, FeasibilityRecord*>> ordered;
    ordered.reserve(mergedRecords.size());
    for (FeasibilityRecord& rec : mergedRecords) {
        ordered.emplace_back(&entryById.find(rec.id)->second->combination, &rec);
    }
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const auto& a, const auto& b) {
                         // ①可行/不足/淘汰（三态位次——见 verdictRank）。
                         const int ra = verdictRank(a.second->verdict);
                         const int rb = verdictRank(b.second->verdict);
                         if (ra != rb) {
                             return ra < rb;
                         }
                         // ②轴序→③候选 ID→④目录版本→⑤组合键。
                         return combinationLess(*a.first, *b.first);
                     });

    SelectionRunResult result;
    result.set.records.reserve(ordered.size());
    std::unordered_map<std::string, const FeasibleCombinationMetrics*> metricsById;
    for (const FeasibleSetEntry& entry : entries) {
        if (entry.metrics.has_value()) {
            metricsById.emplace(entry.combination.id, &*entry.metrics);
        }
    }
    for (const auto& item : ordered) {
        FeasibilityRecord& rec = *item.second;
        // diagRef 回填（词表→稳定码唯一映射——DiagCodes.hpp；已带引用的
        // 原因不覆盖——上游显式登记的引用权威更高）。
        for (RejectionReason& reason : rec.reasons) {
            if (!reason.diagRef.has_value()) {
                const std::string_view code = reasonTokenDiagCode(reason.token);
                if (!code.empty()) {
                    reason.diagRef = std::string(code);
                }
            }
        }
        result.set.records.push_back(rec);
        if (rec.verdict == VerdictKind::Feasible) {
            result.set.feasible.push_back(*item.first);  // 可行组合实体（构成面）。
        }
        // SEL-06 指标素材按 records 排序序对位输出（消费面读取序一致）。
        const auto mIt = metricsById.find(rec.id);
        if (mIt != metricsById.end()) {
            FeasibleCombinationMetrics m = *mIt->second;
            m.combinationId = rec.id;  // 键与记录对位（防御同源——组装方供给键应一致）。
            result.metrics.push_back(std::move(m));
        }
    }

    // 覆盖矩阵素材与身份/完整性原样透传（组装器不加工——卡 §14.6"覆盖
    // 矩阵素材"的职责止于承载）。
    result.coverage = coverage;
    result.identity = identity;
    result.completeness = CompletenessKind::Complete;
    // completeness 语义：组装器输入的记录/覆盖已含分轨缺口（合成记录均
    // 为 DataInsufficient）——可行集素材面本身恒为"结构完整"（缺失已在
    // 记录层标记）；批级完整性（映射批 Partial）由上游 SelectionCheckResult
    // 携带、经证据汇总消费，不在此重复承载（单一事实源——登记 §19.3 T06）。
    return result;
}

// =====================================================================
// 淘汰原因构造器（§10.3 词表唯一实现点——make 回填 token/diagRef）
// =====================================================================

RejectionReason RejectionReasonProvider::make(ReasonToken token,
                                              const ReasonContext& ctx) const
{
    // 数值字段非有限＝调用方契约违约 fail-fast（NFR-COR-03——比较型
    // 字段的"实际/要求/时刻"是 ERR-01 追溯面，NaN 会使比较语义失效）。
    requireFinite(ctx.atTime, "atTime");
    requireFinite(ctx.actual, "actual");
    requireFinite(ctx.required, "required");

    RejectionReason r;
    r.token = token;
    r.candidateModelId = ctx.candidateModelId;
    r.axisId = ctx.axisId;
    r.caseId = ctx.caseId;
    r.atTime = ctx.atTime;
    r.segmentId = ctx.segmentId;
    r.actual = ctx.actual;
    r.required = ctx.required;
    r.unit = ctx.unit;
    r.thresholdSource = ctx.thresholdSource;
    r.actualText = ctx.actualText;
    r.requiredText = ctx.requiredText;
    r.catalog = ctx.catalog;
    r.inputSliceId = ctx.inputSliceId;
    r.mappingId = ctx.mappingId;
    r.suggestion = ctx.suggestion;
    // SEL-* 稳定码引用回填（§10.3"建议值随 WP-19-T06 注册"——词表→码
    // 唯一映射 reasonTokenDiagCode；token 为封闭枚举恒命中，空串分支
    // 仅为越界防御——不产 diagRef 即不伪造码值）。
    const std::string_view code = reasonTokenDiagCode(token);
    if (!code.empty()) {
        r.diagRef = std::string(code);
    }
    return r;
}

// =====================================================================
// SEL-06 指标计算（§17.3 裕量口径——见 FeasibleSet.hpp 函数注六维度表）
// =====================================================================

namespace {

/// 单维度裕量的构造与最小值维护（严格小于才替换——同值保留先见者，
/// 遍历序＝轴序→工况序→固定维度序，确定性）。
void considerMargin(std::optional<MarginFact>& current, MarginFact candidate)
{
    if (!current.has_value() || candidate.margin < current->margin) {
        current = std::move(candidate);
    }
}

/// 快照电机查找（CombinationCheck.cpp 同名辅助的本地最小实现——按
/// modelId 线性查找；快照规模 R1 有限，查找在裕量计算 O(轴×工况×维)×
/// O(候选数) 内可接受；未找到返回 nullptr——调用方按维度跳过处理）。
const MotorCatalogEntry* findMotorLocal(const CatalogPackageSnapshot& s,
                                        const ModelId& id)
{
    for (const MotorCatalogEntry& m : s.motors) {
        if (m.modelId == id) {
            return &m;
        }
    }
    return nullptr;
}

/// 快照减速器查找（同上）。
const GearboxCatalogEntry* findGearboxLocal(const CatalogPackageSnapshot& s,
                                            const ModelId& id)
{
    for (const GearboxCatalogEntry& g : s.gearboxes) {
        if (g.modelId == id) {
            return &g;
        }
    }
    return nullptr;
}

}  // namespace

std::vector<FeasibleCombinationMetrics> computeFeasibleCombinationMetrics(
    const CombinationCheckCoreInput& input,
    const std::vector<CombinationCheckOutcome>& outcomes)
{
    // ---- 调用方契约校验（fail-fast——与 checkCombinations 同款前提）。
    if (input.snapshot == nullptr) {
        throw std::invalid_argument("SEL-06 指标计算：快照指针为空（调用方契约违约）");
    }
    const CatalogPackageSnapshot& snapshot = *input.snapshot;
    std::unordered_map<std::string, const CombinationCheckOutcome*> outcomeById;
    outcomeById.reserve(outcomes.size());
    for (const CombinationCheckOutcome& o : outcomes) {
        if (!outcomeById.emplace(o.record.id, &o).second) {
            throw std::invalid_argument("SEL-06 指标计算：产出记录键重复（"
                                        + o.record.id + "）");
        }
    }

    // ---- 工况序（轴事实首现序——与组合校核段 caseOrder 同口径；确定性）。
    std::vector<CaseId> caseOrder;
    {
        std::unordered_map<std::string, int> seen;
        for (const AxisWorkpointFacts& f : input.axisFacts) {
            if (seen.emplace(f.caseId, 0).second) {
                caseOrder.push_back(f.caseId);
            }
        }
    }

    std::vector<FeasibleCombinationMetrics> result;
    result.reserve(outcomes.size());
    for (const CombinationCheckOutcome& outcome : outcomes) {
        FeasibleCombinationMetrics m;
        m.combinationId = outcome.record.id;
        m.totalMass = outcome.totalMass;  // 镜像 T05 核算（单一来源——§9.3 ⑨）。
        // cost：v1 目录无成本字段——恒 nullopt（不伪造，T05 落位细化 ⑧）。
        m.cost = std::nullopt;

        // 定位组合轴表（映射批组合指派表——轴序权威；缺表＝无轴可遍历，
        // minMargin 保持 nullopt——如实输出无指标）。
        const MappingCombinationFact* combo = nullptr;
        for (const MappingCombinationFact& c : input.mappingBatch.combinations) {
            if (c.combinationId == outcome.record.id) {
                combo = &c;
                break;
            }
        }
        if (combo != nullptr) {
            // 遍历序：轴（组合轴表序）→ 工况（首现序）→ 维度（固定六维
            // 序——与 FeasibleSet.hpp 函数注登记顺序一致）。全部维度取
            // margin 最小者（§17.3"最小驱动裕量……含来源工况"）。
            for (const AxisDeviceAssignment& axis : combo->axes) {
                // 候选能力值（目录必填字段——快照缺失候选＝无能力值，
                // 维度跳过；与组合校核的 candidate-missing 缺口分轨——
                // 指标计算不产缺口，缺失如实表现为无该维裕量）。
                const MotorCatalogEntry* motor = findMotorLocal(snapshot, axis.motorModelId);
                const GearboxCatalogEntry* gearbox
                    = findGearboxLocal(snapshot, axis.gearboxModelId);
                for (const CaseId& caseId : caseOrder) {
                    // 关节侧事实（与组合无关——DYN-03 口径，轴×工况查）。
                    const AxisWorkpointFacts* joint = nullptr;
                    for (const AxisWorkpointFacts& f : input.axisFacts) {
                        if (f.jointId == axis.jointId && f.caseId == caseId) {
                            joint = &f;
                            break;
                        }
                    }
                    // 电机侧映射事实（组合口径权威——组合×轴×工况三键查）。
                    const MappingAxisFact* mfact = nullptr;
                    for (const MappingAxisFact& f : input.mappingBatch.axes) {
                        if (f.combinationId == combo->combinationId
                            && f.jointId == axis.jointId && f.caseId == caseId) {
                            mfact = &f;
                            break;
                        }
                    }
                    // 六维度逐项计算（工作点与能力值都可得才计算——任一
                    // 缺失跳过该维，不伪造零工作点/零能力；margin＝
                    // (capability−|actual|)/capability，负值如实保留）。
                    if (motor != nullptr && mfact != nullptr
                        && mfact->motorTorqueRms.has_value() && motor->ratedTorque > 0.0) {
                        MarginFact fact;
                        fact.dimension = "motor-torque-continuous";
                        fact.axisId = axis.jointId;
                        fact.caseId = caseId;
                        fact.actual = *mfact->motorTorqueRms;
                        fact.capability = motor->ratedTorque;
                        fact.margin = (fact.capability - std::fabs(fact.actual))
                                      / fact.capability;
                        fact.unit = "N*m";
                        considerMargin(m.minMargin, std::move(fact));
                    }
                    if (motor != nullptr && mfact != nullptr
                        && mfact->motorTorquePeak.has_value() && motor->peakTorque > 0.0) {
                        MarginFact fact;
                        fact.dimension = "motor-torque-peak";
                        fact.axisId = axis.jointId;
                        fact.caseId = caseId;
                        fact.actual = *mfact->motorTorquePeak;
                        fact.capability = motor->peakTorque;
                        fact.margin = (fact.capability - std::fabs(fact.actual))
                                      / fact.capability;
                        fact.unit = "N*m";
                        considerMargin(m.minMargin, std::move(fact));
                    }
                    if (motor != nullptr && mfact != nullptr
                        && mfact->motorSpeedPeak.has_value() && motor->maxSpeed > 0.0) {
                        MarginFact fact;
                        fact.dimension = "motor-speed-peak";
                        fact.axisId = axis.jointId;
                        fact.caseId = caseId;
                        fact.actual = *mfact->motorSpeedPeak;
                        fact.capability = motor->maxSpeed;
                        fact.margin = (fact.capability - std::fabs(fact.actual))
                                      / fact.capability;
                        fact.unit = "rad/s";
                        considerMargin(m.minMargin, std::move(fact));
                    }
                    if (motor != nullptr && mfact != nullptr
                        && mfact->motorPowerPeak.has_value() && motor->ratedPower > 0.0) {
                        MarginFact fact;
                        fact.dimension = "motor-power-peak";
                        fact.axisId = axis.jointId;
                        fact.caseId = caseId;
                        fact.actual = *mfact->motorPowerPeak;
                        fact.capability = motor->ratedPower;
                        fact.margin = (fact.capability - std::fabs(fact.actual))
                                      / fact.capability;
                        fact.unit = "W";
                        considerMargin(m.minMargin, std::move(fact));
                    }
                    if (gearbox != nullptr && joint != nullptr
                        && joint->jointTorqueRms.has_value()
                        && gearbox->ratedOutputTorque > 0.0) {
                        MarginFact fact;
                        fact.dimension = "gearbox-rated-torque";
                        fact.axisId = axis.jointId;
                        fact.caseId = caseId;
                        fact.actual = *joint->jointTorqueRms;
                        fact.capability = gearbox->ratedOutputTorque;
                        fact.margin = (fact.capability - std::fabs(fact.actual))
                                      / fact.capability;
                        fact.unit = "N*m";
                        considerMargin(m.minMargin, std::move(fact));
                    }
                    if (gearbox != nullptr && joint != nullptr
                        && joint->jointTorquePeak.has_value()
                        && gearbox->peakOutputTorque > 0.0) {
                        MarginFact fact;
                        fact.dimension = "gearbox-peak-torque";
                        fact.axisId = axis.jointId;
                        fact.caseId = caseId;
                        fact.actual = *joint->jointTorquePeak;
                        fact.capability = gearbox->peakOutputTorque;
                        fact.margin = (fact.capability - std::fabs(fact.actual))
                                      / fact.capability;
                        fact.unit = "N*m";
                        considerMargin(m.minMargin, std::move(fact));
                    }
                }
            }
        }
        result.push_back(std::move(m));
    }
    return result;
}

// =====================================================================
// sel 域必需证据 Profile（EVI-01——REQUIREMENTS §8.1 表 4 选型行逐行
// 实例化；行文权威＝需求表 4 原文，本函数是登记实例，非第二内容源）
// =====================================================================

evidence::RequiredEvidenceProfile makeSelRequiredEvidenceProfile()
{
    // Profile 级：profileId/version 与 T05 评估器 descriptor.profile 绑定
    // 同值（kSelProfileId/kSelProfileVersion——同一 sel 域 Profile 实例；
    // 注册时序"Profile 注册在前、评估器注册在后"由 L5 装配执行，卡 §11.2/
    // evidence §13）。contentIdentity 零值＝域不可申报（注册时 evidence
    // 计算回填——§6.1/§9.5，T05 descriptor 同款保留值语义）。
    evidence::RequiredEvidenceProfile profile;
    profile.profileId = std::string(kSelProfileId);
    profile.version = std::string(kSelProfileVersion);

    // 必需项（表 4 选型行"必需证据项"四行逐行——序＝表 4 行文序；全部
    // Required 类、无适用条件〔恒适用——选型评估输入齐备时四产物恒可
    // 生成〕、substitutableByInfeasibility=false〔不存在"因不可行而无法
    // 生成"的成功产物——全淘汰亦有淘汰原因记录〕）。
    evidence::EvidenceProfileItem item;
    item.itemClass = evidence::EvidenceItemClass::Required;
    item.substitutableByInfeasibility = false;

    item.itemId = std::string(kSelProfileItemCatalogLock);
    item.description =
        "目录版本锁定标识（表 4 选型行必需项①——组合校核所用目录锁定版本，"
        "SelectionCheckResult.catalog 承载）";
    profile.required.push_back(item);

    item.itemId = std::string(kSelProfileItemMotorOpPoint);
    item.description =
        "每组合电机侧工作点：τ/ω/P 序列、效率、反射惯量、惯量比"
        "（表 4 选型行必需项②——DriveTrainMappingEvaluator 同一口径，"
        "③端口 dt.mapping 唯一映射产出）";
    profile.required.push_back(item);

    item.itemId = std::string(kSelProfileItemRejectionReasons);
    item.description =
        "逐项淘汰原因（表 4 选型行必需项③——含实际值与阈值，ERR-01 比较型"
        "字段＋阈值来源＋稳定诊断引用齐备）";
    profile.required.push_back(item);

    item.itemId = std::string(kSelProfileItemComboCompatibility);
    item.description =
        "组合兼容记录（表 4 选型行必需项④——电机—减速器兼容核对记录，"
        "无记录即不兼容）";
    profile.required.push_back(item);

    // 建议项（表 4 选型行"建议证据项"两行——缺失不阻断、单独标注；
    // Suggested 类、其余字段同纪律）。
    item.itemClass = evidence::EvidenceItemClass::Suggested;

    item.itemId = std::string(kSelProfileItemCostMassSummary);
    item.description =
        "成本/质量汇总（表 4 选型行建议项——SelectionRunResult.metrics；"
        "成本 v1 目录无字段显式缺失，不伪造）";
    profile.suggested.push_back(item);

    item.itemId = std::string(kSelProfileItemVendorAvailability);
    item.description =
        "优选品牌与供应状态（表 4 选型行建议项——SEL-07 优选过滤，与硬能力"
        "判定分轨）";
    profile.suggested.push_back(item);

    return profile;
}

}  // namespace sdurws::ird::selection
