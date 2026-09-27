/**
 * @file   KinPanelModel.cpp
 * @brief  四面板投影行集构建的实现（KinPanelModel.hpp 全部落点）。
 *
 * 设计依据：KinPanelModel.hpp 文件头（本 TU 是其全部函数的实现落点）；
 * 本 TU 零排序/零比较/零阈值判断——一切数值语义由域设施定形（acceptance 2）。
 */

#include "KinPanelModel.hpp"

#include <cmath>
#include <stdexcept>
#include <utility>

#include "KinPanelUnits.hpp"  // formatKinQuantityText（显示投影唯一出口）

namespace sdurws {
namespace ird {
namespace kinematics {
namespace {

/// 便捷装配：命名数值行（SI 值＋单位 token → 投影文本一行）。
KinNamedValueRow makeRow(std::string key, std::string label, double siValue,
                         std::string unitToken,
                         const std::optional<DisplayUnitProjection>& units)
{
    KinNamedValueRow row;
    row.key = std::move(key);
    row.label = std::move(label);
    row.siValue = siValue;
    row.unitToken = unitToken;
    row.displayText = formatKinQuantityText(siValue, row.unitToken, units);
    return row;
}

/// 碰撞标记词（三态直投——KIN-05：未评价绝不呈现为"无碰撞"）。
std::string collisionTokenOf(const CollisionStatus& cs)
{
    if (!cs.evaluated) {
        return "未评价";  // 证据缺失语义——如实呈现
    }
    return cs.inCollision ? "碰撞" : "无";
}

}  // namespace

std::vector<KinNamedValueRow> poseMetricRows(
    const PoseMetrics& metrics,
    const std::optional<DisplayUnitProjection>& displayUnits)
{
    // 行构建序固定（头注 1~4——呈现稳定序，NFR-COR-02）。全部数值来自
    // IFkEvaluator::evaluate 产出值（域设施），此处零计算只投影。
    std::vector<KinNamedValueRow> rows;

    // ---- 1. TCP 位姿（位置三轴行＋姿态旋转矩阵对角元摘要行）：位置为
    //      长度量纲（m 投影）；姿态以方向余弦对角元直投（无量纲——呈现
    //      层零三角换算，RPY 分解属计算逻辑不落插件）。----
    rows.push_back(makeRow("tcp-x", "TCP X", metrics.tcpInBase.P()[0], "m",
                           displayUnits));
    rows.push_back(makeRow("tcp-y", "TCP Y", metrics.tcpInBase.P()[1], "m",
                           displayUnits));
    rows.push_back(makeRow("tcp-z", "TCP Z", metrics.tcpInBase.P()[2], "m",
                           displayUnits));
    rows.push_back(makeRow("tcp-rot-xx", "姿态 R[0][0]",
                           metrics.tcpInBase.R()(0, 0), "1", displayUnits));
    rows.push_back(makeRow("tcp-rot-yy", "姿态 R[1][1]",
                           metrics.tcpInBase.R()(1, 1), "1", displayUnits));
    rows.push_back(makeRow("tcp-rot-zz", "姿态 R[2][2]",
                           metrics.tcpInBase.R()(2, 2), "1", displayUnits));

    // ---- 2. 无量纲指标（条件数/可操作度/最小裕量）----
    rows.push_back(makeRow("cond", "条件数", metrics.conditionNumber, "1",
                           displayUnits));
    rows.push_back(makeRow("manip", "可操作度", metrics.manipulability, "1",
                           displayUnits));
    rows.push_back(makeRow("min-margin", "最小关节裕量", metrics.minimumJointMargin,
                           "1", displayUnits));

    // ---- 3. 奇异值逐条（sigma-1..n——链序；无量纲）----
    for (std::size_t i = 0; i < metrics.singularValues.size(); ++i) {
        rows.push_back(makeRow("sigma-" + std::to_string(i + 1),
                               "奇异值 σ" + std::to_string(i + 1),
                               metrics.singularValues[i], "1", displayUnits));
    }

    // ---- 4. 关节裕量逐关节（margin-j1..n——链序；无量纲归一化比）----
    for (std::size_t i = 0; i < metrics.jointMargins.size(); ++i) {
        rows.push_back(makeRow("margin-j" + std::to_string(i + 1),
                               "关节裕量 J" + std::to_string(i + 1),
                               metrics.jointMargins[i], "1", displayUnits));
    }
    return rows;
}

std::vector<KinSolutionRow> solutionRowsFor(const IKinematicSolutionSet& set,
                                            bool usableOnly,
                                            const std::optional<DisplayUnitProjection>& displayUnits)
{
    // 排序权威＝set.sorted()（构造时四键稳定排序——本函数零自排）；
    // 筛选权威＝规范谓词工厂产出的谓词逐解调用（NFR-MNT-04——比较式
    // 唯一在 SolutionSet.hpp，此处只调用不实现）。
    const SolutionSetView& ordered = set.sorted();
    const SolutionPredicate usable = usableOnly ? usableSolutionPredicate()
                                                : SolutionPredicate{};

    std::vector<KinSolutionRow> rows;
    rows.reserve(ordered.size());
    for (std::size_t i = 0; i < ordered.size(); ++i) {
        const KinematicSolution& s = ordered[i];
        // 视图序上逐解过域谓词——保留者即"可用解"子序列（不重排）。
        if (usable && !usable(s)) {
            continue;
        }

        KinSolutionRow row;
        row.rank = i;  // rank＝视图稳定序位置（筛选不改变序语义）
        row.signature = s.signature;
        // q 文本与残差列经投影出口（reprojectSolutionColumns——单位换算
        // 唯一通道）；裕量/条件数为无量纲直投。
        const auto cols =
            reprojectSolutionColumns(s.q, s.positionResidual, displayUnits);
        row.qText = cols.first;
        row.positionResidualText = cols.second;
        row.minJointMarginText =
            formatKinQuantityText(s.minimumJointMargin, "1", displayUnits);
        row.conditionNumberText =
            formatKinQuantityText(s.conditionNumber, "1", displayUnits);
        row.collisionToken = collisionTokenOf(s.collisionStatus);
        rows.push_back(std::move(row));
    }
    return rows;
}

std::vector<KinNamedValueRow> solutionStatisticsRows(const IKinematicSolutionSet& set)
{
    // 四计数直投（IkSolutionSetStatistics 结构序——确定性拼装）。
    const IkSolutionSetStatistics st = set.statistics();
    std::vector<KinNamedValueRow> rows;
    auto countRow = [&rows](std::string key, std::string label, std::uint64_t v) {
        KinNamedValueRow row;
        row.key = std::move(key);
        row.label = std::move(label);
        row.siValue = static_cast<double>(v);  // 计数（无量纲）——双承载便于投影
        row.unitToken = "1";
        row.displayText = std::to_string(v);   // 计数直显（不投影小数）
        rows.push_back(std::move(row));
    };
    countRow("stat-raw", "初值展开数", st.rawCount);
    countRow("stat-converged", "收敛数", st.convergedCount);
    countRow("stat-deduped", "去重后解数", st.dedupedCount);
    countRow("stat-filtered", "硬过滤数", st.filteredCount);
    return rows;
}

std::vector<KinNamedValueRow> solutionInspectorRows(
    const IKinematicSolutionSet& set, const SolutionRef& ref,
    const std::optional<DisplayUnitProjection>& displayUnits)
{
    // 视图稳定序取解（SolutionRef.solutionIndex＝sorted() 下标）；越界＝
    // 空向量（不虚构行——陈旧指称的缺省面）。
    const SolutionSetView& ordered = set.sorted();
    if (ref.solutionIndex >= ordered.size()) {
        return {};
    }
    const KinematicSolution& s = ordered[ref.solutionIndex];

    std::vector<KinNamedValueRow> rows;
    // 健康摘要半区（裕量/条件数/残差——与解表同源零二口径）。
    rows.push_back(makeRow("insp-margin", "最小关节裕量", s.minimumJointMargin,
                           "1", displayUnits));
    rows.push_back(makeRow("insp-cond", "条件数", s.conditionNumber, "1",
                           displayUnits));
    rows.push_back(makeRow("insp-res-pos", "位置残差", s.positionResidual, "m",
                           displayUnits));
    rows.push_back(makeRow("insp-res-ori", "姿态残差", s.orientationResidual,
                           "rad", displayUnits));
    rows.push_back(makeRow("insp-manip", "可操作度", s.manipulability, "1",
                           displayUnits));
    // 关节半区（q 逐关节——rad|m 投影）与雅可比半区不在本行集（雅可比
    // 矩阵文本由 widget 表格直呈 PoseMetrics 产出——此处保持行集语义）。
    for (std::size_t i = 0; i < s.jointMargins.size(); ++i) {
        rows.push_back(makeRow("insp-margin-j" + std::to_string(i + 1),
                               "裕量 J" + std::to_string(i + 1),
                               s.jointMargins[i], "1", displayUnits));
    }
    // 碰撞半区（三态词直投——文本行）。
    KinNamedValueRow collision;
    collision.key = "insp-collision";
    collision.label = "碰撞";
    collision.siValue = 0.0;  // 文本行无数值语义
    collision.unitToken = "1";
    collision.displayText = collisionTokenOf(s.collisionStatus);
    rows.push_back(std::move(collision));
    return rows;
}

std::vector<KinCoverageRow> coverageRows(const CoverageResult& result)
{
    // 位置/姿态两轴分别成行（V-14 两口径不混合）；计数比直投文本——
    // 零浮点百分比换算（无比率字段是 CoverageResult 的设计决定，文件头注）。
    auto ratioText = [](const CoverageTotals& t, bool defined) {
        if (!defined) {
            return std::string("零样本——比率不存在");  // V-13：零样本不虚构 0%
        }
        return std::to_string(t.reached) + "/" + std::to_string(t.planned);
    };

    KinCoverageRow pos;
    pos.key = "coverage-position";
    pos.label = "位置覆盖（存在性口径）";
    pos.ratioText = ratioText(result.position, result.positionDefined);
    pos.zeroSamples = !result.positionDefined;
    pos.degraded = result.downgraded;
    KinCoverageRow ori;
    ori.key = "coverage-orientation";
    ori.label = "姿态覆盖（全局口径）";
    ori.ratioText = ratioText(result.orientation, result.orientationDefined);
    ori.zeroSamples = !result.orientationDefined;
    ori.degraded = result.downgraded;
    return {pos, ori};
}

std::vector<KinNamedValueRow> policySummaryRows(const std::string& collisionPolicyText,
                                                const std::string& thresholdPolicyText)
{
    // 只读策略摘要两行（UX-08——面板零碰撞开关/零阈值编辑字段；文本由
    // 宿主从 policy 投影，本函数只装行）。
    KinNamedValueRow collision;
    collision.key = "collision-policy";
    collision.label = "碰撞策略（只读）";
    collision.siValue = 0.0;
    collision.unitToken = "1";
    collision.displayText = collisionPolicyText;
    KinNamedValueRow threshold;
    threshold.key = "threshold-policy";
    threshold.label = "判定阈值（只读）";
    threshold.siValue = 0.0;
    threshold.unitToken = "1";
    threshold.displayText = thresholdPolicyText;
    return {collision, threshold};
}

std::vector<KinNamedValueRow> configChangeHintRows(const AnalysisConfigChangeHint& hint)
{
    // 纯提示行（L-K7——不自动重算：本函数零回调零触发，只把提示数据行化）。
    std::vector<KinNamedValueRow> rows;
    if (!hint.configChanged) {
        return rows;  // 未变化＝空提示（域设施已裁定）
    }
    for (const std::string_view key : hint.affectedEvaluationKeys) {
        KinNamedValueRow row;
        row.key = std::string("hint-") + std::string(key);
        row.label = "受影响评估（需重算）";
        row.siValue = 0.0;
        row.unitToken = "1";
        row.displayText = std::string(key);
        rows.push_back(std::move(row));
    }
    if (hint.sampleBaselineChanged) {
        KinNamedValueRow sample;
        sample.key = "hint-sample-baseline";
        sample.label = "样本基准变化（sampleSetIdentity 随变）";
        sample.siValue = 0.0;
        sample.unitToken = "1";
        sample.displayText = "region-cover 采样基准已变——覆盖结果需按新基准重算";
        rows.push_back(std::move(sample));
    }
    return rows;
}

}  // namespace kinematics
}  // namespace ird
}  // namespace sdurws
