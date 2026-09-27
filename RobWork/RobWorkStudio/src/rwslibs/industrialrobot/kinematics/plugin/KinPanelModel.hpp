/**
 * @file   KinPanelModel.hpp
 * @brief  四面板投影行集构建（零 Qt 呈现模型）——位姿指标/任务点/覆盖/
 *         结果面板的"域设施产出→呈现行"唯一搬运面（零计算逻辑）。
 *
 * 设计依据：
 *   - units/kinematics.md §9.8（面板组成表四行：位姿指标面板/任务点验证
 *     面板/区域覆盖面板/结果与可视化面板；消费契约列——IFkEvaluator/
 *     IKinematicSolutionSet/结果投影）；
 *   - acceptance 2（面板不缓存权威结果、阈值/排序/筛选全部消费域设施
 *     ——本头全部函数对解集只调 sorted()/statistics()/worstBy()/规范谓词
 *     工厂，对渲染数据只调 Render.hpp 组装点，零自设比较与排序）；
 *   - modeling 先例 PanelModel 同构（五区投影的零 Qt 呈现模型——WP-13-T15）。
 *
 * 背景说明（"搬运面"的含义）：本头全部函数都是纯函数——输入域值
 * （PoseMetrics/解集视图/覆盖结果），输出行集值；一切数值语义（排序、
 * 最差项、阈值、状态词）在调用前已由域设施定形，这里只做"取字段→投影
 * 文本→装行"。任何一处需要"比较/排序/阈值判断"才能实现的功能都不属于
 * 本头（那会构成第二实现点——NFR-MNT-04）。
 *
 * 线程约束：全部纯函数可重入；实际仅在 UI 线程刷新路径调用（§3.4）。
 */

#ifndef IRD_KINEMATICS_PLUGIN_KINPANELMODEL_HPP
#define IRD_KINEMATICS_PLUGIN_KINPANELMODEL_HPP

#include <optional>
#include <string>
#include <vector>

#include <sdurws/ird/kinematics/Coverage.hpp>     // CoverageResult（覆盖率结果值）
#include <sdurws/ird/kinematics/Fk.hpp>           // PoseMetrics（位姿指标值）
#include <sdurws/ird/kinematics/Render.hpp>       // 渲染数据/选中联动（KIN-07 组装点）
#include <sdurws/ird/kinematics/SolutionSet.hpp>  // IKinematicSolutionSet（解集视图）
#include "KinPanelTypes.hpp"                       // 行集值（同目录插件私有头）

namespace sdurws {
namespace ird {
namespace kinematics {

// =====================================================================
// 位姿指标面板（承接旧 Diagnose 页——§9.8 面板表行 1）
// =====================================================================

/**
 * @brief 位姿指标行集构建（FK 位姿/Jacobian 汇总/奇异值/条件数/可操作度/
 *        关节裕量的逐项一行投影）。
 *
 * 行构建序（固定——呈现稳定序，NFR-COR-02）：
 *   1. tcp-x/tcp-y/tcp-z/tcp-rx/…（TCP 位姿——位置 m/姿态 rad 投影）；
 *   2. cond/manipulability/min-margin（无量纲指标）；
 *   3. singular values 逐条（sigma-1..n——无量纲）；
 *   4. joint margins 逐关节（margin-j1..n——无量纲，链序）。
 *
 * @param metrics      [in] 位姿指标（IFkEvaluator::evaluate 产出——域设施）
 * @param displayUnits [in] 显示单位投影（nullopt＝SI 直显）
 * @return 指标行集（每次调用现建——面板零缓存）
 *
 * @throws std::invalid_argument 指标含非有限值（上游违约——fail-fast）
 *
 * 纯函数；线程安全；确定性。
 */
std::vector<KinNamedValueRow> poseMetricRows(
    const PoseMetrics& metrics,
    const std::optional<DisplayUnitProjection>& displayUnits);

// =====================================================================
// 结果与可视化面板（承接旧 Visualization/Report 页——§9.8 面板表行 4）
// =====================================================================

/**
 * @brief 解表行集构建（§9.8 结果面板"结果筛选/最差项排序"的消费面）。
 *
 * 筛选/排序的域设施分工（acceptance 2 静态可断言）：
 *   - 排序：set.sorted()（构造时四键稳定排序——零自排）；
 *   - 筛选：usableSolutionPredicate()（规范谓词工厂——零第二处比较式）；
 *   - 行内数值：KinematicSolution 字段直投＋显示投影。
 *
 * @param set          [in] 会话/归档解集视图（调用方保证存活至返回）
 * @param usableOnly   [in] true＝仅"可用解"（碰撞证据完备且无碰撞——
 *                     usableSolutionPredicate 语义）；false＝全部 sorted() 解
 * @param displayUnits [in] 显示单位投影
 * @return 解行集（sorted() 序子序列——rank 保持视图序；零缓存）
 *
 * @throws std::invalid_argument 解指标含 NaN（同上游契约）
 *
 * 纯函数；线程安全；确定性。
 */
std::vector<KinSolutionRow> solutionRowsFor(const IKinematicSolutionSet& set,
                                            bool usableOnly,
                                            const std::optional<DisplayUnitProjection>& displayUnits);

/**
 * @brief 解统计摘要行（统计面——IkSolutionSetStatistics 四计数直投；
 *        "3 解/2 过滤"形态的检查器摘要行）。
 *
 * @param set [in] 解集视图
 * @return 摘要文本（确定性拼装——行序＝计数结构序）
 *
 * 纯函数；线程安全；确定性。
 */
std::vector<KinNamedValueRow> solutionStatisticsRows(const IKinematicSolutionSet& set);

/**
 * @brief 选中解的检查器联动数据（L-K1 选中联动的检查器半区——健康摘要/
 *        关节/雅可比投影）。
 *
 * 数据流（§9.8 L-K1）：解表选中→SolutionRef→视图稳定序取解→检查器行集；
 * 三维高亮半区＝渲染数据（assembleSolutionSetRenderData 产出，由
 * refreshRenderData 单独供给 View3D 契约——本函数只做检查器行）。
 *
 * @param set          [in] 解集视图
 * @param ref          [in] 选中指称（SolutionRef——视图序下标）
 * @param displayUnits [in] 显示单位投影
 * @return 检查器行集（解不存在＝空向量——不虚构行）
 *
 * @throws std::invalid_argument 解指标含 NaN
 *
 * 纯函数；线程安全；确定性。
 */
std::vector<KinNamedValueRow> solutionInspectorRows(
    const IKinematicSolutionSet& set, const SolutionRef& ref,
    const std::optional<DisplayUnitProjection>& displayUnits);

// =====================================================================
// 区域覆盖面板（§9.8 面板表行 3）
// =====================================================================

/**
 * @brief 覆盖率结果行集构建（位置/姿态分别呈现＋降级/零样本标识——
 *        CoverageResult 值直投）。
 *
 * @param result [in] 覆盖率计算结果（computeCoverage 域设施产出——经
 *               evaluation 通道或测试直供；此处零自算）
 * @return 覆盖行集（位置/姿态两行——ratioText 为计数比直投文本）
 *
 * 纯函数；线程安全；确定性。
 */
std::vector<KinCoverageRow> coverageRows(const CoverageResult& result);

// =====================================================================
// 求解配置高级面板（§9.8 面板表行 5——UX-04）
// =====================================================================

/**
 * @brief 配置策略摘要行（高级面板的只读策略区——"无碰撞开关、无判定
 *        阈值：只读策略摘要跳转——UX-08"的承载行）。
 *
 * 行语义（acceptance 4 的静态断言锚）：
 *   - "collision-policy"行：碰撞策略启用事实的只读摘要（来源 policy——
 *     面板零开关字段，值文本由调用方供给的策略事实直投）；
 *   - "threshold-policy"行：判定阈值只读摘要（阈值唯一来源 policy
 *     JointThresholds——面板零阈值编辑字段，P-POL-2）。
 *
 * @param collisionPolicyText [in] 碰撞策略摘要文本（宿主从 policy 投影）
 * @param thresholdPolicyText [in] 判定阈值摘要文本（宿主从 policy 投影）
 * @return 两行只读摘要行（fixedKey——编辑器按 key 识别不生成编辑行）
 *
 * 纯函数；线程安全；确定性。
 */
std::vector<KinNamedValueRow> policySummaryRows(const std::string& collisionPolicyText,
                                                const std::string& thresholdPolicyText);

/**
 * @brief 配置编辑依赖提示行（L-K7——analyzeConfigurationChange 产出的
 *        提示数据的呈现行化；不触发重算——纯提示）。
 *
 * @param hint [in] 配置变更依赖提示（域设施 AnalysisConfig.hpp 产出）
 * @return 提示行集（configChanged 时受影响评估键逐条一行；未变化＝空）
 *
 * 纯函数；线程安全；确定性。
 */
std::vector<KinNamedValueRow> configChangeHintRows(const AnalysisConfigChangeHint& hint);

}  // namespace kinematics
}  // namespace ird
}  // namespace sdurws

#endif  // IRD_KINEMATICS_PLUGIN_KINPANELMODEL_HPP
