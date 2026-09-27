/**
 * @file   KinPanelUnits.hpp
 * @brief  面板显示单位投影辅助（L-K8 单位切换重投影的零 Qt 半区）——
 *         行集重投影与数值文本格式化的唯一出口（KIN-12）。
 *
 * 设计依据：
 *   - units/kinematics.md §9.8（L-K8：显示单位切换→全面板重投影：零修订/
 *     零重算/结果不动；KIN-12 显示单位纯投影）；§14.6 v0.10（T10 落位的
 *     DisplayUnitProjection——换算唯一经 core Units，本单元零换算算术）；
 *   - 任务契约 tasks/foundation/WP-15-T12.json acceptance 1（单位切换重
 *     投影用例）/4（单位对话框复用 T10 投影）。
 *
 * 背景说明（为什么重投影是"重建行"而非"改文本"）：行集（KinNamedValueRow
 * 等）的 displayText 在构建期一次投影成形；单位切换后由本头函数以新
 * DisplayUnitProjection **整表重建**——不逐格改写文本（避免半新半旧的
 * 混合呈现），重建输入恒为 SI 真值（行集携带 siValue），故切换零重算、
 * 结果不动（V-17 的呈现半区）。
 *
 * 线程约束：全部函数纯函数（无共享状态）——可重入；实际仅在 UI 线程的
 * 刷新路径调用（§3.4）。
 */

#ifndef IRD_KINEMATICS_PLUGIN_KINPANELUNITS_HPP
#define IRD_KINEMATICS_PLUGIN_KINPANELUNITS_HPP

#include <optional>
#include <string>
#include <vector>

#include <sdurws/ird/kinematics/AnalysisConfig.hpp>  // DisplayUnitProjection（KIN-12 投影句柄）
#include "KinPanelTypes.hpp"                          // 行集值（同目录插件私有头）

namespace sdurws {
namespace ird {
namespace kinematics {

/**
 * @brief SI 真值＋单位 token → 显示文本（"数值 单位"同显的唯一格式化点
 *        ——零 Qt 行集侧；ui 表单公共件 formatFieldValueText 的同源职责
 *        在零 Qt 行集面的对位落点）。
 *
 * 换算规则（按量纲分派，零第二实现点）：
 *   - 长度（"m"）与角度（"rad"）：displayUnits 在场→经 DisplayUnitProjection
 *     投影（core::convert 唯一换算入口）；nullopt→SI 直显；
 *   - 无量纲（"1"）：直显数值不带单位后缀（工程惯例——ui 同案）；
 *   - 其它 token：直显数值＋token 原文（裕量/条件数等无量纲比）。
 *
 * 数值格式：6 位有效数字 general（std::to_chars，locale 无关——NFR-COR-02；
 * 与 ui formatFieldValueText 同精度口径）。
 *
 * @param siValue      [in] SI 真值（须有限——非有限属上游计算违约，抛
 *                     std::invalid_argument，不渲染伪数据）
 * @param unitToken    [in] SI 单位 token（"m"/"rad"/"1"/……）
 * @param displayUnits [in] 显示单位投影（nullopt＝SI 直显制式）
 * @return "300 mm"/"0.3 m"/"500" 形态显示文本
 *
 * @throws std::invalid_argument siValue 非有限，或 unitToken 需投影但非
 *         "m"/"rad"（投影句柄只覆盖 R1 两量纲——调用方错选即装配错误）
 *
 * 纯函数；线程安全；确定性。
 */
std::string formatKinQuantityText(double siValue, const std::string& unitToken,
                                  const std::optional<DisplayUnitProjection>& displayUnits);

/**
 * @brief 位姿指标行集重投影（L-K8——以新显示制式整表重建显示文本；
 *        SI 真值逐行原样保留）。
 *
 * @param rows         [in] 现有行集（siValue/unitToken 为权威输入）
 * @param displayUnits [in] 新显示单位投影（nullopt＝SI 直显）
 * @return 重建后的行集（行序/键/标签/siValue 逐位不变——仅 displayText
 *         随制式变化；调用方所有）
 *
 * 纯函数；线程安全；确定性。
 */
std::vector<KinNamedValueRow> reprojectMetricRows(
    const std::vector<KinNamedValueRow>& rows,
    const std::optional<DisplayUnitProjection>& displayUnits);

/**
 * @brief 解行集重投影（L-K8 解表半区——q/残差列随制式重建；rank 序不变
 *        ——稳定排序权威在 IKinematicSolutionSet，本函数零重排）。
 *
 * 与 metricRows 不同，解行只携带已投影文本（原始解值在会话解集对象内
 * ——本函数不回读解集，重投影输入由调用方从解集重建行集，见
 * KinPanelModel::solutionRowsFor）。此处的重投影语义＝同制式下重查文本
 * 的便捷面（q 值向量逐分量换算）。
 *
 * @param qSi          [in] 关节向量 SI 真值（rad|m 链序——解对象 q 原值）
 * @param residualSiM  [in] 位置残差（单位 m；无量纲列不参与投影）
 * @param displayUnits [in] 显示单位投影（nullopt＝SI 直显）
 * @return 重投影后的（qText, positionResidualText）二元组
 *
 * @throws std::invalid_argument 输入含非有限值（同 formatKinQuantityText）
 *
 * 纯函数；线程安全；确定性。
 */
std::pair<std::string, std::string> reprojectSolutionColumns(
    const std::vector<double>& qSi, double residualSiM,
    const std::optional<DisplayUnitProjection>& displayUnits);

/**
 * @brief R1 显示单位制式切换的合法性判定与构造（L-K8 入口——单位切换
 *        控件的域侧半区）。
 *
 * 词表封闭（KIN-12 R1 子集）：长度仅 m/cm/mm、角度仅 rad/deg——R2 token
 * （inch/grad/turn）一律 nullopt（WP-15-T17 承接，不提前实现）。
 *
 * @param lengthSymbol [in] 长度单位 token 原文（区分大小写）
 * @param angleSymbol  [in] 角度单位 token 原文（区分大小写）
 * @return 投影句柄；token 不在 R1 子集＝nullopt（调用方回退现制式）
 *
 * 纯函数；线程安全；确定性。
 */
std::optional<DisplayUnitProjection> tryFindKinDisplayUnits(
    std::string_view lengthSymbol, std::string_view angleSymbol);

}  // namespace kinematics
}  // namespace ird
}  // namespace sdurws

#endif  // IRD_KINEMATICS_PLUGIN_KINPANELUNITS_HPP
