/**
 * @file   Comparison.cpp
 * @brief  方案比较视图编排实现（UX-13/V15-02；WP-22-T11）——
 *         纯函数组装核＋ISchemeComparisonController 端口组合编排。
 *
 * 设计依据：units/workflow.md §8.1/§8.2（比较视图编排——指标取数投影、
 * 基准一致性前置、diff 分组呈现）、§10.2（Draft 签名逐字——v1.2 偏差
 * 登记段）、§10.3（错误语义＝调用方 fail-fast；buildComparison const
 * 并发安全；比较数据会话态无 schema）；REQUIREMENTS.md UX-13/OPT-07/
 * EVI-02/RPT-04/MDL-08（M-5 分工——diff 计算归 modeling，本单元零 diff
 * 实现）、P-OPT-8（警告不虚构）；evidence.md §6.5（基准一致性检查公共
 * 契约——本单元只消费不重实现，PA-1）。
 *
 * 头文件已承载全部设计注释（字段级来源锚/错误语义/线程约束）——本文件
 * 注释聚焦实现步骤与算法语义（AGENTS.md §2.4：复杂逻辑逐步讲解）。
 */

#include <sdurws/ird/workflow/Comparison.hpp>

#include <cmath>      // std::isfinite——非有限值归"—"占位（不渲染 nan/inf）
#include <iomanip>    // std::setprecision——17 位有效数字确定性渲染
#include <locale>     // std::locale::classic——小数点/无千分位（NFR-COR-02）
#include <sstream>    // std::ostringstream——值文本渲染载体
#include <utility>    // std::move——行/块组装的转移语义

namespace sdurws {
namespace ird {
namespace workflow {

namespace {

// =====================================================================
// 内部辅助：当前性投影的逐字段比较（ui::CurrentnessProjection 未定义
// operator==——TitleFacts 同款口径，字段集与对端投影一致）
// =====================================================================

/// @brief 逐字段比较当前性投影（status/不可判定原因/逐条失效原因三元组）。
bool currentnessEqual(const ui::CurrentnessProjection& a,
                      const ui::CurrentnessProjection& b)
{
    // 快速路径：状态与不可判定原因先比（可选值 presence 语义——nullopt
    // 与"有值"是不同事实，optional== 已承载）。
    if (a.status != b.status
        || a.unevaluableCause != b.unevaluableCause
        || a.reasons.size() != b.reasons.size()) {
        return false;
    }
    // 逐条失效原因（Reason 三字段 dependencyKey/kindToken/detail 按序
    // 比对——evidence 侧 (kind,key) 字典序由对端保证，此处直比序位）。
    for (std::size_t i = 0; i < a.reasons.size(); ++i) {
        if (a.reasons[i].dependencyKey != b.reasons[i].dependencyKey
            || a.reasons[i].kindToken != b.reasons[i].kindToken
            || a.reasons[i].detail != b.reasons[i].detail) {
            return false;
        }
    }
    return true;
}

// =====================================================================
// 内部辅助：diff 分组 token 词表（modeling 稳定 token 词形——呈现分拣
// 键；词形权威归 modeling modelDiffGroupToken，此处为消费侧登记位）
// =====================================================================

/// @brief 分组 token 判别（返回组分箱序 0=structure/1=parameters/2=properties；
///        -1＝未知 token——对端违约信号）。
int diffGroupIndex(const std::string& group)
{
    if (group == "structure") {
        return 0;
    }
    if (group == "parameters") {
        return 1;
    }
    if (group == "properties") {
        return 2;
    }
    return -1;
}

}  // namespace

// =====================================================================
// 值类型相等运算（头文件仅声明——字段多的类型落本文件保持头整洁）
// =====================================================================

bool SchemeMetricFacts::operator==(const SchemeMetricFacts& o) const
{
    if (!(branch == o.branch && available == o.available && label == o.label
          && metrics.size() == o.metrics.size()
          && baseline == o.baseline)) {
        return false;
    }
    // 指标列逐格比较（五字段全等——对齐键与取数半区一体）。
    for (std::size_t i = 0; i < metrics.size(); ++i) {
        const Metric& a = metrics[i];
        const Metric& b = o.metrics[i];
        if (a.metricKey != b.metricKey || a.labelKey != b.labelKey
            || a.unitToken != b.unitToken || a.value != b.value
            || a.stale != b.stale) {
            return false;
        }
    }
    // 当前性投影逐字段（辅助函数——零重算，纯值比较）。
    return currentnessEqual(currentness, o.currentness);
}

bool SchemeDiffEntry::operator==(const SchemeDiffEntry& o) const
{
    return group == o.group && kind == o.kind && objectId == o.objectId
        && subjectPath == o.subjectPath && field == o.field
        && valueChanged == o.valueChanged
        && provenanceChanged == o.provenanceChanged
        && baselineText == o.baselineText && candidateText == o.candidateText;
}

bool ComparisonMetricRow::operator==(const ComparisonMetricRow& o) const
{
    return metricKey == o.metricKey && labelKey == o.labelKey
        && unitToken == o.unitToken && cells == o.cells && differs == o.differs;
}

bool ComparisonDiffBlock::operator==(const ComparisonDiffBlock& o) const
{
    return baseline == o.baseline && candidate == o.candidate
        && baselineLabel == o.baselineLabel
        && candidateLabel == o.candidateLabel && structure == o.structure
        && parameters == o.parameters && properties == o.properties
        && warnings == o.warnings;
}

bool ComparisonViewData::operator==(const ComparisonViewData& o) const
{
    return schemeLabels == o.schemeLabels && metricRows == o.metricRows
        && diffs == o.diffs;
}

// =====================================================================
// 纯函数组装核
// =====================================================================

std::string renderMetricValueText(double v)
{
    // 非有限值（NaN/±Inf）归"—"占位：非有限值不是可呈现的工程量，
    // 绝不渲染 "nan"/"inf" 字样（OPT-07 不可算面同口径——不伪造数值）。
    if (!std::isfinite(v)) {
        return kMetricUnavailableDisplay;
    }
    // 17 位有效数字＋classic locale：IEEE754 double 的十进制往返安全位数
    // （同值必同串——NFR-COR-02 跨平台字节稳定；小数点恒 '.'、无千分位）。
    std::ostringstream os;
    os.imbue(std::locale::classic());
    os << std::setprecision(17) << v;
    return os.str();
}

std::string comparisonBaselineDimensionKey(
    evidence::BaselineDifferenceDimension dimension)
{
    // 维度 → 键尾 token（kebab 词形，与枚举声明序一一对应；值归 ui 文案表
    // ——本函数只产键不产文案，UX-02 键/值半区分工）。switch 全枚举无
    // default：evidence 新增维度漏登记时编译器告警暴露（消费侧同步义务）。
    switch (dimension) {
    case evidence::BaselineDifferenceDimension::Project:
        return "comparison.baseline-mismatch.dim.project";
    case evidence::BaselineDifferenceDimension::Branch:
        return "comparison.baseline-mismatch.dim.branch";
    case evidence::BaselineDifferenceDimension::InputBaseline:
        return "comparison.baseline-mismatch.dim.input-baseline";
    case evidence::BaselineDifferenceDimension::RequiredCaseSet:
        return "comparison.baseline-mismatch.dim.required-case-set";
    case evidence::BaselineDifferenceDimension::SampleSets:
        return "comparison.baseline-mismatch.dim.sample-sets";
    }
    // 词表外值（理论上七值封闭枚举不可构造——防御性返回空串不伪造键，
    // adviceTitleKey 同款未知 token 口径；不抛——键构造是呈现辅助面）。
    return {};
}

std::vector<ComparisonMetricRow> assembleMetricRows(
    const std::vector<SchemeMetricFacts>& facts)
{
    // ---- 第 1 步：前置校验（调用方/上游组装违约 fail-fast）----
    // 空集在本单元不可能由 buildComparison 产生（方案数已校验 ≥2）——
    // 直调组装核的调用方同样受 @pre 约束，违约即 fail-fast 不带病组装。
    if (facts.empty()) {
        throw WorkflowError("assembleMetricRows: facts 为空（方案数 <2）");
    }
    for (const SchemeMetricFacts& f : facts) {
        // @pre"方案分支存在且可读"（§10.2 Draft）的编排面兑现：端口把
        // "分支不存在/归档结果不可读"折叠为 available=false，此处转
        // fail-fast——静默跳过会产出缺列高亮表（比崩溃更危险的错误呈现）。
        if (!f.available) {
            throw WorkflowError(
                "assembleMetricRows: 方案分支不可用（" + f.branch.toCanonical()
                + "）——@pre 方案分支存在且可读违约");
        }
    }

    // ---- 第 2 步：指标列同构校验（以首方案列为基准）----
    // 高亮表是"行=指标、列=方案"的矩阵：各方案指标列必须同序同键，否则
    // 单元格会错位到别的指标行（对端契约违约——fail-fast，绝不猜列）。
    const std::vector<SchemeMetricFacts::Metric>& firstColumn = facts.front().metrics;
    for (std::size_t s = 1; s < facts.size(); ++s) {
        const std::vector<SchemeMetricFacts::Metric>& col = facts[s].metrics;
        if (col.size() != firstColumn.size()) {
            throw WorkflowError(
                "assembleMetricRows: 方案指标列长度不同构（方案 "
                + facts[s].branch.toCanonical() + " 列数 " +
                std::to_string(col.size()) + " ≠ 基准 " +
                std::to_string(firstColumn.size()) + "）");
        }
        for (std::size_t m = 0; m < col.size(); ++m) {
            if (col[m].metricKey != firstColumn[m].metricKey) {
                throw WorkflowError(
                    "assembleMetricRows: 方案指标列不同构（方案 "
                    + facts[s].branch.toCanonical() + " 第 " +
                    std::to_string(m) + " 列键 " + col[m].metricKey
                    + " ≠ 基准 " + firstColumn[m].metricKey + "）");
            }
        }
    }

    // ---- 第 3 步：逐指标组装行（行序＝端口列序——不重排）----
    std::vector<ComparisonMetricRow> rows;
    rows.reserve(firstColumn.size());
    for (std::size_t m = 0; m < firstColumn.size(); ++m) {
        ComparisonMetricRow row;
        // 列头三件套自首方案透传（对端权威——零加工：指标词表/单位/口径
        // 归对端，本单元不解释 §8.1 红线）。
        row.metricKey = firstColumn[m].metricKey;
        row.labelKey = firstColumn[m].labelKey;
        row.unitToken = firstColumn[m].unitToken;

        // 差异高亮判定素材：可算值收集（nullopt 不参与——"'—'语义
        // null≠0"，不可算绝不当作 0 参与比较）。
        std::vector<double> computable;
        computable.reserve(facts.size());

        row.cells.reserve(facts.size());
        for (const SchemeMetricFacts& f : facts) {
            ComparisonMetricRow::Cell cell;
            cell.value = f.metrics[m].value;
            if (cell.value.has_value()) {
                // 可算→确定性渲染（17 位有效数字 classic locale）；非有限
                // 值由渲染函数归"—"（不可算同占位）。
                cell.displayText = renderMetricValueText(*cell.value);
                computable.push_back(*cell.value);
            } else {
                // 不可算→OPT-07 占位符（kMetricUnavailableDisplay——
                // displayText 与 value 的 presence 严格一致）。
                cell.displayText = kMetricUnavailableDisplay;
            }
            // 指标级过期限定（端口从当前性投影折叠——透传零重算）。
            cell.stale = f.metrics[m].stale;
            row.cells.push_back(std::move(cell));
        }

        // 高亮位：≥2 个可算值且不全精确相等（零容差——无任何容差口径
        // 登记，不发明阈值 N9/I-WF-3；浮点精确比较在同基准同域数据上
        // 是合法判定——EVI-02 已挡掉跨基准比较）。单可算值（其余全不
        // 可算）不高亮：没有可比对偶，"差异"无从谈起。
        bool differs = false;
        if (computable.size() >= 2) {
            const double& head = computable.front();
            for (std::size_t i = 1; i < computable.size(); ++i) {
                if (computable[i] != head) {  // 零容差精确比较（有意为之）
                    differs = true;
                    break;
                }
            }
        }
        row.differs = differs;
        rows.push_back(std::move(row));
    }
    return rows;
}

ComparisonDiffBlock assembleDiffBlock(const SchemeDiffFacts& facts,
                                      const core::BranchId& baseline,
                                      const core::BranchId& candidate,
                                      const std::string& baselineLabel,
                                      const std::string& candidateLabel)
{
    ComparisonDiffBlock block;
    // 块头透传（呈现素材——身份与显示名零加工）。
    block.baseline = baseline;
    block.candidate = candidate;
    block.baselineLabel = baselineLabel;
    block.candidateLabel = candidateLabel;

    if (!facts.available) {
        // ---- 通道不可用（环境面）——诚实降级 ----
        // 三组保持空表（不虚构任何条目）＋追加降级警告：呈现面据此告知
        // 用户"差异面来源缺失、该组差异可能不全"（ITitleFactPort"环境
        // 失败折叠为安全缺省不炸宿主"同款纪律——比较视图是可重试的
        // 呈现面，不允许单次端口失败炸宿主会话）。
        block.warnings.push_back(kComparisonWarningDiffSourceUnavailable);
        return block;
    }

    // ---- 分组分拣：按 modeling 稳定 token 归三组（呈现组织——不解释
    // 字段含义、不增删条目、不重排组内序；排序契约归 modeling diff 实现）。
    for (const SchemeDiffEntry& entry : facts.entries) {
        const int idx = diffGroupIndex(entry.group);
        // 未知 token＝对端契约违约（modeling 词表三值封闭）——fail-fast：
        // 静默丢条目会让用户看到"无差异"的假象，比崩溃更危险。
        if (idx < 0) {
            throw WorkflowError(
                "assembleDiffBlock: 未知 diff 分组 token（" + entry.group
                + "）——对端契约违约");
        }
        switch (idx) {
        case 0:
            block.structure.push_back(entry);
            break;
        case 1:
            block.parameters.push_back(entry);
            break;
        case 2:
            block.properties.push_back(entry);
            break;
        default:
            // 不可达（idx ∈ {0,1,2}）——防御性保持封闭，零吞错。
            throw WorkflowError("assembleDiffBlock: 分组分箱越界（内部缺陷）");
        }
    }

    // ---- 警告透传（P-OPT-8 通道）：零加工零吞——条目集仍逐条来自端口
    // 产出，绝不因警告补造条目（"范围外给警告不虚构差异"）。
    block.warnings.insert(block.warnings.end(), facts.warnings.begin(),
                          facts.warnings.end());
    return block;
}

// =====================================================================
// O7 编排服务（端口组合实现）
// =====================================================================

SchemeComparisonController::SchemeComparisonController(
    const ISchemeMetricPort& metricPort,
    const IComparisonDiffPort& diffPort) noexcept
    : m_metricPort(&metricPort), m_diffPort(&diffPort)
{
}

ComparisonOutcome SchemeComparisonController::buildComparison(
    const std::vector<core::BranchId>& schemes) const
{
    // ---- 编排第 1 步：前置校验（调用方错误 fail-fast——§10.2 Draft
    // @pre"2~4 方案"的编排面兑现；越界抛 WorkflowError 不产出半成品）。
    if (schemes.size() < kMinComparisonSchemes
        || schemes.size() > kMaxComparisonSchemes) {
        throw WorkflowError(
            "buildComparison: 方案数越界（" + std::to_string(schemes.size())
            + "）——UX-13 要求 2~4 个方案");
    }
    // 重复分支拒绝：同一分支自比无比较语义（差集恒空），且会让 EVI-02
    // 检查退化为平凡通过——调用方清单有重复即属组装缺陷，fail-fast。
    for (std::size_t i = 0; i < schemes.size(); ++i) {
        for (std::size_t j = i + 1; j < schemes.size(); ++j) {
            if (schemes[i] == schemes[j]) {
                throw WorkflowError(
                    "buildComparison: 重复方案分支（"
                    + schemes[i].toCanonical() + "）");
            }
        }
    }

    // ---- 编排第 2 步：指标投影取数（§8.2 数据流"八项指标取数（投影，
    // 只读）"——每次调用现取快照，无缓存；端口把分支 label/指标列/基准
    // 身份/当前性投影一次折叠）。
    const std::vector<SchemeMetricFacts> facts = m_metricPort->collect(schemes);

    // ---- 编排第 3 步：可用性检查（@pre"方案分支存在且可读"——快照与
    // 入参同序同长由端口契约保证；长度复核防对端实现违约，fail-fast）。
    if (facts.size() != schemes.size()) {
        throw WorkflowError(
            "buildComparison: 指标端口快照长度与方案数不符（"
            + std::to_string(facts.size()) + " ≠ "
            + std::to_string(schemes.size()) + "）——端口契约违约");
    }
    // 可用性复核**先于**基准一致性检查：分支不可用属调用方契约违约
    // （fail-fast 异常轨），基准不一致属业务分支（返回值拒绝轨）——两轨
    // 不可混叠（不可用快照的 baseline 是无意义值，拿去做一致性判定会把
    // 调用方错误伪装成"基准不一致"拒绝，错误语义失真）。
    for (const SchemeMetricFacts& f : facts) {
        if (!f.available) {
            throw WorkflowError(
                "buildComparison: 方案分支不可用（" + f.branch.toCanonical()
                + "）——@pre 方案分支存在且可读违约");
        }
    }

    // ---- 编排第 4 步：基准一致性检查（EVI-02/RPT-04 前置——§8.2 数据流
    // "不一致→拒绝比较＋原因提示"；检查实现归 evidence 公共契约
    // checkComparisonBaselinesConsistent，本单元零重实现 PA-1）。
    std::vector<evidence::ComparisonBaseline> baselines;
    baselines.reserve(facts.size());
    for (const SchemeMetricFacts& f : facts) {
        baselines.push_back(f.baseline);
    }
    const evidence::BaselineConsistencyResult consistency =
        evidence::checkComparisonBaselinesConsistent(baselines);
    if (!consistency.consistent) {
        // 拒绝不是错误：基准不一致是可修复的业务分支——返回 Rejected
        // （原因键＋建议动作键恒填充；逐维度明细键一一对应；dimensions
        // 透传零加工——本单元零基准判定语义）。**不产出任何比较数据**
        // （acceptance 2：不产出混基准比较）。
        ComparisonRejection rejection;
        rejection.dimensions = consistency.differingDimensions;
        rejection.reasonKey = kComparisonBaselineMismatchKey;
        rejection.actionKey = kComparisonBaselineMismatchActionKey;
        rejection.detailKeys.reserve(rejection.dimensions.size());
        for (evidence::BaselineDifferenceDimension d : rejection.dimensions) {
            rejection.detailKeys.push_back(comparisonBaselineDimensionKey(d));
        }
        ComparisonOutcome outcome;
        outcome.accepted = false;
        outcome.rejection = std::move(rejection);
        return outcome;
    }

    // ---- 编排第 5 步：指标行组装（高亮判定＋"—"渲染——含列同构复核
    // 与 available 复核，见 assembleMetricRows）。
    ComparisonViewData view;
    view.metricRows = assembleMetricRows(facts);

    // 方案显示名列（列头呈现素材——label 透传）。
    view.schemeLabels.reserve(facts.size());
    for (const SchemeMetricFacts& f : facts) {
        view.schemeLabels.push_back(f.label);
    }

    // ---- 编排第 6 步：diff 取数与组装（§8.2 数据流"Model Diff（modeling
    // 数据实体）"——基线＝首方案〔编排决定〕，其余逐对成块；n-1 块恒产出，
    // 空差集＝空块仍入列，呈现面渲染"无差异"而不是缺块）。
    view.diffs.reserve(schemes.size() - 1);
    for (std::size_t i = 1; i < schemes.size(); ++i) {
        // 端口调用（现取——两分支的 diff 由 L5 桥接 modeling 服务产出；
        // P-OPT-8 警告与通道可用性由端口面承载）。
        const SchemeDiffFacts diffFacts =
            m_diffPort->diff(schemes.front(), schemes[i]);
        // 分组分拣＋警告透传（纯函数——未知 token/通道不可用语义见该函数）。
        view.diffs.push_back(assembleDiffBlock(
            diffFacts, schemes.front(), schemes[i], facts.front().label,
            facts[i].label));
    }

    // ---- 编排第 7 步：返回接受态（view 与 rejection 互斥——Outcome
    // 契约由测试钉住）。
    ComparisonOutcome outcome;
    outcome.accepted = true;
    outcome.view = std::move(view);
    return outcome;
}

}  // namespace workflow
}  // namespace ird
}  // namespace sdurws
