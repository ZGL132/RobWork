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

#include <rw/math/RPY.hpp>  // RPY（★ KIN-R2 任务4——完整姿态呈现的库设施直调；RobWork 权威欧拉角实现，零自写三角分解）

#include "KinPanelUnits.hpp"  // formatKinQuantityText（显示投影唯一出口）

namespace sdurws {
namespace ird {
namespace kinematics {
namespace {

/// 便捷装配：命名数值行（SI 值＋单位 token → 投影文本一行）。
/// infinityText＝该行遇 +∞ 时的呈现语义词（KIN-P3/审查 A03；★ F-902
/// 按行收紧——调用方只对**域契约允许无穷的行**供给词：条件数行"奇异/∞"
/// （Fk.hpp:108-112 奇异→+∞）、裕量行"无界"（KinTypes.hpp:272-273 连续
/// 无工作范围＝+∞/全无界＝+∞）；缺省 nullopt＝该量域契约不含无穷
/// （位置/旋转矩阵元/奇异值/可操作度/残差——Fk.hpp PoseMetrics 字段
/// 规则），域外 +∞ 走"非法值(+∞)"显式标记轨，不再静默显示泛化"∞"）。
KinNamedValueRow makeRow(std::string key, std::string label, double siValue,
                         std::string unitToken,
                         const std::optional<DisplayUnitProjection>& units,
                         std::optional<std::string_view> infinityText
                         = std::nullopt)
{
    KinNamedValueRow row;
    row.key = std::move(key);
    row.label = std::move(label);
    row.siValue = siValue;
    row.unitToken = unitToken;
    row.displayText = formatKinQuantityText(siValue, row.unitToken, units,
                                            infinityText);
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
    // 行构建序固定（头注 1~5——呈现稳定序，NFR-COR-02）。全部数值来自
    // IFkEvaluator::evaluate 产出值（域设施），此处零业务计算只投影。
    std::vector<KinNamedValueRow> rows;

    // ---- 1. TCP 位置（双参考系——★ KIN-R2 任务4/审查 §4 坐标系标识）：
    //      基座系 {B}（T_base_tcp）与世界系 {W}（T_world_tcp——经 view.
    //      worldToBase() 唯一组合，Fk.hpp 契约）各三轴行。位置为长度量纲
    //      （m 投影）；两参考系均域外无穷收紧（F-902 白名单缺省——位置
    //      契约恒有限）。
    rows.push_back(makeRow("tcp-x", "TCP X（基座系）",
                           metrics.tcpInBase.P()[0], "m", displayUnits));
    rows.push_back(makeRow("tcp-y", "TCP Y（基座系）",
                           metrics.tcpInBase.P()[1], "m", displayUnits));
    rows.push_back(makeRow("tcp-z", "TCP Z（基座系）",
                           metrics.tcpInBase.P()[2], "m", displayUnits));
    rows.push_back(makeRow("tcp-world-x", "TCP X（世界系）",
                           metrics.tcpInWorld.P()[0], "m", displayUnits));
    rows.push_back(makeRow("tcp-world-y", "TCP Y（世界系）",
                           metrics.tcpInWorld.P()[1], "m", displayUnits));
    rows.push_back(makeRow("tcp-world-z", "TCP Z（世界系）",
                           metrics.tcpInWorld.P()[2], "m", displayUnits));

    // ---- 2. 完整姿态呈现（★ KIN-R2 任务4/审查 §4——修复前仅旋转矩阵
    //      三个对角元，不能完整表达三维姿态）：
    //      2a. RPY 三行（rad——角度量纲投影；换算经 rw::math::RPY 库
    //          设施（RobWork 权威欧拉角实现），本层零自写三角分解——
    //          旧决策"RPY 分解不落插件"随审查 §4 完整姿态要求修订为
    //          "库设施直调、零自写三角"（呈现投影，不入任何计算身份，
    //          随卡 §14.6 增量修订登记）。worldTref→基座系的 RPY 语义
    //          即"基座系中看 TCP 姿态"。
    //      2b. 旋转矩阵 9 元逐行只读（无量纲，∈[-1,1]——正交阵契约；
    //          域外无穷收紧同 F-902）。RPY 在万向锁邻域的库侧 epsilon
    //          特判不改变矩阵元的直投真值——矩阵行是姿态的第一性呈现，
    //          RPY 行是工程可读投影，两者互补不互替。
    const rw::math::RPY<double> rpy(metrics.tcpInBase.R());
    rows.push_back(makeRow("tcp-rpy-roll", "姿态 RPY-Roll（基座系）",
                           rpy[0], "rad", displayUnits));
    rows.push_back(makeRow("tcp-rpy-pitch", "姿态 RPY-Pitch（基座系）",
                           rpy[1], "rad", displayUnits));
    rows.push_back(makeRow("tcp-rpy-yaw", "姿态 RPY-Yaw（基座系）",
                           rpy[2], "rad", displayUnits));
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            rows.push_back(makeRow(
                "tcp-rot-r" + std::to_string(r) + "c" + std::to_string(c),
                "姿态 R[" + std::to_string(r) + "][" + std::to_string(c)
                    + "]（基座系）",
                metrics.tcpInBase.R()(r, c), "1", displayUnits));
        }
    }

    // ---- 3. 无量纲指标（条件数/可操作度/最小裕量）----
    // 合法无穷的行级语义（KIN-P3/审查 A03；F-902 白名单按行收紧）：
    // 条件数 +∞＝σmin=0 的奇异状态（Fk.hpp 契约"奇异→+∞，不静默截断"）
    // ——呈现"奇异/∞"；裕量 +∞＝连续关节无工作范围的无界裕量
    // （KinTypes.hpp"全无界＝+∞"）——呈现"无界"。两者均原样呈现，
    // 禁止截断成有限数字。可操作度域契约恒有限（≥0）——白名单缺省。
    rows.push_back(makeRow("cond", "条件数", metrics.conditionNumber, "1",
                           displayUnits, "奇异/∞"));
    rows.push_back(makeRow("manip", "可操作度", metrics.manipulability, "1",
                           displayUnits));
    rows.push_back(makeRow("min-margin", "最小关节裕量", metrics.minimumJointMargin,
                           "1", displayUnits, "无界"));

    // ---- 4. 奇异值逐条（sigma-1..n——链序；无量纲）----
    for (std::size_t i = 0; i < metrics.singularValues.size(); ++i) {
        rows.push_back(makeRow("sigma-" + std::to_string(i + 1),
                               "奇异值 σ" + std::to_string(i + 1),
                               metrics.singularValues[i], "1", displayUnits));
    }

    // ---- 5. 关节裕量逐关节（margin-j1..n——链序；无量纲归一化比；+∞＝
    //      该关节连续且无工作范围——"无界"呈现，A03 同则）----
    for (std::size_t i = 0; i < metrics.jointMargins.size(); ++i) {
        rows.push_back(makeRow("margin-j" + std::to_string(i + 1),
                               "关节裕量 J" + std::to_string(i + 1),
                               metrics.jointMargins[i], "1", displayUnits,
                               "无界"));
    }
    return rows;
}

std::vector<runtime::JointType> dofJointTypes(const runtime::CanonicalModel& model)
{
    // 链序遍历＋Fixed 滤除（Fixed 不消耗 q——与求解器/FK/
    // assembleSessionSolveRequest 的 dof 口径一致）：产出与
    // q_authoritative 同维同序的类型表——显示投影逐轴量纲的唯一真值来源
    // （KIN-P3/审查 A05：double 数组不携带量纲，逐轴类型必须从模型关节
    // 表提取传递，禁止显示层猜测）。
    std::vector<runtime::JointType> types;
    types.reserve(model.chain().joints.size());
    for (const runtime::CanonicalJoint& j : model.chain().joints) {
        if (j.type != runtime::JointType::Fixed) {
            types.push_back(j.type);
        }
    }
    return types;
}

std::vector<KinSolutionRow> solutionRowsFor(
    const IKinematicSolutionSet& set, bool usableOnly,
    const std::vector<runtime::JointType>& dofJointTypes,
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
        // 唯一通道；q 逐轴按 dofJointTypes 量纲投影——KIN-P3/审查 A05：
        // 转动/连续轴角度、移动轴长度，零"全轴角度猜"）。
        const auto cols = reprojectSolutionColumns(s.q, dofJointTypes,
                                                   s.positionResidual,
                                                   displayUnits);
        row.qText = cols.first;
        row.positionResidualText = cols.second;
        // 裕量/条件数：无量纲直投；合法 +∞ 按行语义呈现（KIN-P3/审查
        // A03——解表列与指标行同则："无界"/"奇异/∞"，不截断不抛异常）。
        row.minJointMarginText =
            formatKinQuantityText(s.minimumJointMargin, "1", displayUnits,
                                  "无界");
        row.conditionNumberText =
            formatKinQuantityText(s.conditionNumber, "1", displayUnits,
                                  "奇异/∞");
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
    // 健康摘要半区（裕量/条件数/残差——与解表同源零二口径；合法 +∞ 按
    // A03 行语义词呈现："无界"/"奇异/∞"）。
    rows.push_back(makeRow("insp-margin", "最小关节裕量", s.minimumJointMargin,
                           "1", displayUnits, "无界"));
    rows.push_back(makeRow("insp-cond", "条件数", s.conditionNumber, "1",
                           displayUnits, "奇异/∞"));
    rows.push_back(makeRow("insp-res-pos", "位置残差", s.positionResidual, "m",
                           displayUnits));
    rows.push_back(makeRow("insp-res-ori", "姿态残差", s.orientationResidual,
                           "rad", displayUnits));
    rows.push_back(makeRow("insp-manip", "可操作度", s.manipulability, "1",
                           displayUnits));
    // 关节半区（q 逐关节——rad|m 投影）与雅可比半区不在本行集（雅可比
    // 矩阵文本由 widget 表格直呈 PoseMetrics 产出——此处保持行集语义）；
    // 逐关节裕量 +∞＝无界（A03 同则）。
    for (std::size_t i = 0; i < s.jointMargins.size(); ++i) {
        rows.push_back(makeRow("insp-margin-j" + std::to_string(i + 1),
                               "裕量 J" + std::to_string(i + 1),
                               s.jointMargins[i], "1", displayUnits, "无界"));
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
