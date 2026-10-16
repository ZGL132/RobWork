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
#include <string_view>
#include <vector>

#include <sdurws/ird/kinematics/AnalysisConfig.hpp>  // DisplayUnitProjection（KIN-12 投影句柄）
#include <sdurws/ird/runtime/Description.hpp>        // runtime::JointType（A05 逐轴量纲——关节类型词表）
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
 * 非有限值三分类（KIN-P3/审查 A03——显示层的非法值纪律）：
 *   - **合法无穷（+∞）**：域设施契约内的合法产出（Fk.hpp：奇异条件数
 *     ＝+∞ 且以 isFinite 标记承载"不静默截断"；连续关节无工作范围的
 *     关节裕量＝+∞——KinTypes.hpp）→ 原样返回 infinityText 语义词
 *     （调用方按行语义供给，如条件数行"奇异/∞"、裕量行"无界"），**禁
 *     止截断成有限数字**（截断即伪造数据——比抛异常更严重的失实）；
 *   - **非法 NaN／−∞**：上游计算违约（这些量的值域不含 NaN/−∞）→ 抛
 *     std::invalid_argument fail-fast（NFR-COR-03——不渲染伪数据）；
 *   - **未知/不适用**：不落在本函数（行集层的承载形态是文本行——如
 *     碰撞"未评价"三态词、覆盖"零样本——比率不存在"，见 KinPanelModel
 *     的同案行），数值行只承载"有限值或合法 +∞"两类。
 *
 * 数值格式：6 位有效数字 general（std::to_chars，locale 无关——NFR-COR-02；
 * 与 ui formatFieldValueText 同精度口径）。
 *
 * @param siValue        [in] SI 真值（有限值或合法 +∞——NaN/−∞ 属上游
 *                       计算违约，抛 std::invalid_argument）
 * @param unitToken      [in] SI 单位 token（"m"/"rad"/"1"/……）
 * @param displayUnits   [in] 显示单位投影（nullopt＝SI 直显制式）
 * @param infinityText   [in] siValue＝+∞ 时的呈现语义词（调用方按行语义
 *                       供给："奇异/∞"/"无界"；缺省泛化 "∞"；原样返回
 *                       不带单位后缀——无穷无量纲可比，投影无意义）
 * @return "300 mm"/"0.3 m"/"500"/"奇异/∞" 形态显示文本
 *
 * @throws std::invalid_argument siValue 为 NaN 或 −∞（上游计算违约），
 *         或 unitToken 需投影但非 "m"/"rad"（投影句柄只覆盖 R1 两量纲
 *         ——调用方错选即装配错误）
 *
 * 纯函数；线程安全；确定性。
 */
std::string formatKinQuantityText(double siValue, const std::string& unitToken,
                                  const std::optional<DisplayUnitProjection>& displayUnits,
                                  std::string_view infinityText = "∞");

/**
 * @brief 单关节分量显示投影（KIN-P3/审查 A05——逐轴按其**天然量纲**选
 *        择长度/角度换算的唯一判定点）。
 *
 * 背景（A05 根因）：混合链（转动＋移动）的关节向量各分量量纲不同
 * （转动 rad／移动 m，§5.1 q_authoritative 口径），修复前显示层对整条
 * 向量统一按角度投影（reprojectSolutionColumns 旧实现）——移动轴
 * 0.1 m 在 mm 制式下被误显示 5.72958（deg 数值）。double 数组本身不
 * 携带量纲信息，逐轴类型必须由调用方从模型关节表传下（禁止从数值
 * 大小猜测——装配层契约）。
 *
 * @param siValue      [in] 该轴 SI 真值（转动 rad／移动 m；须有限——
 *                     关节向量分量按求解器契约恒有限，非有限属上游违约）
 * @param jointType    [in] 该轴关节类型（模型关节表逐轴值——链序与 q
 *                     对齐；Fixed 不消耗 q，传入即装配错误）
 * @param displayUnits [in] 显示单位投影（nullopt＝SI 直显——恒等返回）
 * @return 显示数值（移动轴经长度投影、转动/连续轴经角度投影；nullopt
 *         制式＝SI 原值）
 *
 * @throws std::invalid_argument siValue 非有限，或 jointType 为 Fixed
 *         （Fixed 关节不消耗 q——出现在 dof 类型表即调用方装配错误）
 *
 * 纯函数；线程安全；确定性。
 */
double projectJointAxisToDisplay(double siValue, runtime::JointType jointType,
                                 const std::optional<DisplayUnitProjection>& displayUnits);

/**
 * @brief 单关节分量的"数值＋单位"显示文本（widget 关节表单元格的唯一
 *        格式化点——projectJointAxisToDisplay 的文本半区）。
 *
 * @param siValue      [in] 该轴 SI 真值（同 projectJointAxisToDisplay）
 * @param jointType    [in] 该轴关节类型（同上）
 * @param displayUnits [in] 显示单位投影（nullopt＝SI 直显——单位后缀
 *                     用 SI token "m"/"rad" 原文）
 * @return "100 mm"/"5.72958 deg"/"0.1 m" 形态显示文本（6 位有效数字
 *         general——与 formatKinQuantityText 同精度口径）
 *
 * @throws std::invalid_argument 同 projectJointAxisToDisplay
 *
 * 纯函数；线程安全；确定性。
 */
std::string formatJointAxisText(double siValue, runtime::JointType jointType,
                                const std::optional<DisplayUnitProjection>& displayUnits);

/**
 * @brief 位姿指标行集重投影（L-K8——以新显示制式整表重建显示文本；
 *        SI 真值逐行原样保留）。
 *
 * 合法 +∞ 行（KIN-P3/审查 A03）语义：+∞ 的呈现语义词（"奇异/∞"/"无
 * 界"）在行构建期按行语义选定、与单位制式无关——重投影原样保留该行
 * displayText（幂等），仅有限行随新制式重换算；NaN/−∞ 行仍经 format
 * 的 fail-fast 拒绝面（不因重投影放大通过面）。
 *
 * @param rows         [in] 现有行集（siValue/unitToken 为权威输入）
 * @param displayUnits [in] 新显示单位投影（nullopt＝SI 直显）
 * @return 重建后的行集（行序/键/标签/siValue 逐位不变——有限行
 *         displayText 随制式变化；调用方所有）
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
 * 逐轴量纲（KIN-P3/审查 A05）：q 的逐分量投影经 projectJointAxisToDisplay
 * ——转动/连续轴走角度投影、移动轴走长度投影；逐轴类型由调用方从模型
 * 关节表提取（KinPanelModel::dofJointTypes）后传入，本函数不做任何"从
 * 数值猜量纲"的启发式（A05 根因即旧实现的全轴角度假设）。文本形态为
 * 裸数值逗号串（"5.72958, 100"——单位语义由列头/关节表承载，逗号串内
 * 不重复后缀；逐轴单位文本面见 formatJointAxisText）。
 *
 * @param qSi           [in] 关节向量 SI 真值（rad|m 链序——解对象 q 原值）
 * @param dofJointTypes [in] 逐自由度关节类型表（链序与 qSi 对齐——Fixed
 *                      不消耗 q 故不在表内；维度与 qSi 不一致＝调用方
 *                      装配错误）
 * @param residualSiM   [in] 位置残差（单位 m；无量纲列不参与投影）
 * @param displayUnits  [in] 显示单位投影（nullopt＝SI 直显）
 * @return 重投影后的（qText, positionResidualText）二元组
 *
 * @throws std::invalid_argument qSi 与 dofJointTypes 维度不一致、输入含
 *         非有限分量，或类型表含 Fixed（同 projectJointAxisToDisplay）
 *
 * 纯函数；线程安全；确定性。
 */
std::pair<std::string, std::string> reprojectSolutionColumns(
    const std::vector<double>& qSi, const std::vector<runtime::JointType>& dofJointTypes,
    double residualSiM, const std::optional<DisplayUnitProjection>& displayUnits);

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

/**
 * @brief 显示制式数值→SI 真值（提交面逆换算——projectLength 的逆方向；
 *        KIN-P1 审查条目 A07 修复的换算半区）。
 *
 * 背景（A07 根因与修复口径）：目标输入框承载的是**显示制式**数值（行
 * 标签随当前制式标注单位）；提交给会话/求解器的必须是 SI 真值
 * （IkRequest.targetInBase＝基座系 m，§5.3）。修复前 widget 把显示值
 * 直读后用 projectLength（SI→显示的正向读侧投影）再放大——换算方向
 * 反转：mm 制式输入 0.1 被提交为 100 m。本函数是"显示→SI"的唯一
 * 换算点：value_si＝value_display×siFactor（core Units 因子表——
 * mm→0.001、cm→0.01、m→1；与 core Quantity::make 同一算术口径，
 * 非第二换算实现）。
 *
 * @param displayValue [in] 显示制式数值（单位＝displayUnits 所选长度
 *                     单位；nullopt＝SI 直显制式，原值返回）
 * @param displayUnits [in] 当前显示单位投影（KIN-12 投影句柄）
 * @return SI 真值（单位 m）；确定性纯函数（同输入同输出——NFR-COR-02）
 *
 * 非有限输入原样传递（不在此拒绝——拒绝面在调用方交互层：解析后以
 * std::isfinite 就地反馈并保留原值，NFR-COR-03）。
 *
 * 纯函数；线程安全；确定性。
 */
double resolveKinDisplayToSi(double displayValue,
                             const std::optional<DisplayUnitProjection>& displayUnits);

}  // namespace kinematics
}  // namespace ird
}  // namespace sdurws

#endif  // IRD_KINEMATICS_PLUGIN_KINPANELUNITS_HPP
