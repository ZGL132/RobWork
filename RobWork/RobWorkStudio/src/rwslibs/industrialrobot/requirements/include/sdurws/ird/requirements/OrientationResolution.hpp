/**
 * @file   OrientationResolution.hpp
 * @brief  姿态与方向规则解析（§5.3 五规则——WP-14-T06）：把规则参数解析为
 *         确定性参考姿态/方向并记录 resolution 来源（kind＋目标 ObjectId＋
 *         解析值），失败（引用/参数错误）产出可定位诊断。
 *
 * 设计依据：
 *   - units/requirements.md §5.3（姿态与方向规则表＋隔离声明原文——本头
 *     即"规则'解析'＝把规则参数解析为确定性参考姿态/方向并记录来源
 *     resolution（kind＋目标 ObjectId＋解析值）"的落位；"求解失败的
 *     '失败'指引用/参数错误，不是 IK 失败——后者归 kinematics"）、
 *     §8.1（跨聚合浅引用边界专项声明——仅查闭包 objectRefs 元数据，
 *     不解码 modeling 对象字节）、§9.6（REQ-READY-REF-MISSING/
 *     REQ-READY-POSE-ILLEGAL 两码的解析侧复用——语义单源，NFR-MNT-04）、
 *     §11（WP-14-T06 行"姿态规则解析……＋UT（AT-23）"）、§2.5（旧
 *     GeometryFeatureResolver/姿态规则解析器 ♻ 承接——解析记录来源/
 *     失败可定位）
 *   - 需求 REQ-09（姿态规则五规则、应用时解析、解析失败给出可定位
 *     诊断）、AT-23（V-03 五规则样例——解析成功记录来源/失败可定位）、
 *     NFR-COR-01/02（确定性）、NFR-COR-03（不静默改写参数字面）
 *   - 任务契约 tasks/foundation/WP-14-T06.json acceptance 1/2
 *
 * 背景说明（解析的两层语义——第一读者须知）：
 *   "应用时解析"（REQ-09）分两层，本头只落**浅解析层**：
 *   ① 浅解析（本头，requirements 所有权）：规则参数 → 确定性参考姿态/
 *      方向 + resolution 来源留痕。Fixed 的参考姿态即参数字面（Z-Y-X
 *      欧拉，rad）；PointAtTarget 的参考方向＝目标单位向量（refFrame 系
 *      内归一化——纯参数运算，非坐标变换）；ToolRollFree 的参考值＝
 *      滚转自由区间（rad）；AlignFrame/AlignGeometryNormal 的参考目标＝
 *      经闭包浅核对的引用（目标 ObjectId＋特征语义），其数值姿态/法向
 *      依赖 modeling 对象内容，归评估时深度解析（§8.1 浅引用边界）。
 *   ② 深度解析（评估侧，本单元零动作）：目标参考系的实际姿态、场景
 *      对象几何的实际法向、与机器人基座的变换链——遵循 core §4.6 T_ab
 *      语义，矩阵计算归 runtime/下游（§5.3 隔离声明：本单元不重定义
 *      坐标变换、不重定义姿态求解）。
 *
 * 失败语义（acceptance 2 原文"目标悬空等引用/参数失败→可定位诊断回指
 * 需求条目（objectId＋name）"）：
 *   - 参数非法（非有限角/零向量目标/缺 feature/rollRange 逆序）→ 域
 *     错误值（validateOrientationRule 同源——构造边界单点）＋稳定码
 *     REQ-READY-POSE-ILLEGAL 定位诊断（R3 族——语义单源复用，不私设
 *     第二套码）；
 *   - 引用悬空/token 失配（AlignFrame.targetFrame、
 *     AlignGeometryNormal.targetSceneObject 的闭包浅核对）→ 稳定码
 *     REQ-READY-REF-MISSING 定位诊断（R1 族同源复用）。诊断 subject＝
 *     需求条目 ObjectId、localName＝条目名——"回指需求条目"的落实。
 *
 * 线程安全：本头全部实体为纯值/纯函数（无共享可变状态），并发只读安
 * 全、可重入（§3.4 总约定 1）。
 * 确定性（NFR-COR-01/02）：校验序固定（参数面→引用面→产出装配）；方
 * 向归一化为 IEEE754 确定运算（乘加＋std::sqrt）；无环境/时钟/locale
 * 依赖；同输入必得同 resolution。
 */

#ifndef IRD_REQUIREMENTS_ORIENTATIONRESOLUTION_HPP
#define IRD_REQUIREMENTS_ORIENTATIONRESOLUTION_HPP

#include <optional>
#include <string>

#include <rw/math/Vector3D.hpp>  // rw::math::Vector3D<double>——欧拉角/方向（SI：rad/无量纲）

#include <sdurws/ird/core/DiagData.hpp>                  // DiagnosticRecord——可定位诊断载体
#include <sdurws/ird/requirements/Errors.hpp>            // RequirementError——域错误值面
#include <sdurws/ird/requirements/Readiness.hpp>         // CheckContext——闭包 objectRefs 元数据（浅校验数据面）
#include <sdurws/ird/requirements/RequirementTypes.hpp>  // OrientationRule/RollRange——规则值模型

namespace sdurws::ird::requirements {

// =====================================================================
// 解析锚与解析产出（resolution 留痕值——值语义，调用方所有）
// =====================================================================

/**
 * @brief 解析锚——被解析规则所属的需求条目（acceptance 2"可定位诊断
 *        回指需求条目（objectId＋name）"的承载）。
 *
 * 生命周期：纯值；调用方从工作集条目（TaskPoint/WorkRegion）填充——
 * entryId 即条目 ObjectId（O-36 模型内锚，诊断 subject），entryName 即
 * 条目语义名（诊断 localName——D-REQ-6 名称是语义字段）。
 */
struct OrientationRuleAnchor {
    core::ObjectId entryId;    ///< 需求条目 ObjectId（诊断 subject——回指锚）
    std::string entryName;     ///< 需求条目名（诊断 localName——工程用语定位面）
};

/**
 * @brief 姿态规则解析产出（§5.3"记录来源 resolution（kind＋目标
 *        ObjectId＋解析值）"的值承载——acceptance 1"解析产出确定性
 *        参考姿态/方向并记录 resolution 来源"）。
 *
 * 字段有效性按 kind 分支（其余字段保持缺省——与 OrientationRule 的
 * "kind→有效载荷对照"同款纪律，浅解析产出镜像规则参数形状）：
 *   - Fixed：referenceRpy（rad，Z-Y-X 欧拉序，refFrame 系）——确定性
 *     参考姿态＝参数字面（NFR-COR-03：不做等效角归一，§5.3"等价姿态
 *     与内容身份"行）；
 *   - AlignFrame：targetObjectId（闭包浅核对通过的目标）——参考姿态
 *     ≡目标参考系姿态（数值归评估时深度解析，本层不产出数值）；
 *   - AlignGeometryNormal：targetObjectId＋feature＋invertNormal——
 *     参考方向＝目标特征法向语义（数值归评估；本层留痕取反标志）；
 *   - PointAtTarget：referenceDirection（单位向量、无量纲，refFrame
 *     系）＝targetPoint/‖targetPoint‖——确定性参考方向；
 *   - ToolRollFree：rollSpan（rad）＝规则参数的滚转自由区间——主方向
 *     由伴随规则给出时组合表达（§5.3 第五规则行；组合解释归评估）。
 *
 * 失败面：ok==false 时 error（域错误值——与构造边界同源）与 diag
 * （可定位稳定码诊断——subject/localName 回指条目）二者必有其一成立
 * （实际两者都填充——值面与呈现面分工：机器判别看 error，呈现/留痕看
 * diag）。
 *
 * 线程安全：纯值。确定性：同输入同产出（NFR-COR-01）。
 */
struct OrientationResolution {
    bool ok = false;                                        ///< true＝解析成功（解析值字段有效）
    OrientationRuleKind kind = OrientationRuleKind::Fixed;  ///< resolution 来源：规则种类（§5.3 kind 列）
    std::optional<core::ObjectId> targetObjectId;           ///< 目标对象（AlignFrame/AlignGeometryNormal；闭包浅核对通过的目标）
    std::optional<OrientationFeature> feature;              ///< 几何特征（仅 AlignGeometryNormal）
    bool invertNormal = false;                              ///< 法向取反标志（仅 AlignGeometryNormal）
    std::optional<rw::math::Vector3D<double>> referenceRpy;       ///< Fixed：Z-Y-X 欧拉（rad，refFrame 系）
    std::optional<rw::math::Vector3D<double>> referenceDirection; ///< PointAtTarget：单位方向（refFrame 系，无量纲）
    std::optional<RollRange> rollSpan;                            ///< ToolRollFree：滚转自由区间（rad，有序非空）
    RequirementError error{};                               ///< 失败面：首个违例域错误（validate* 同源值）
    core::DiagnosticRecord diag{};                          ///< 失败面：可定位诊断（REQ-READY-REF-MISSING / REQ-READY-POSE-ILLEGAL）
};

// =====================================================================
// 解析入口（§5.3 浅解析层唯一实现——纯函数）
// =====================================================================

/**
 * @brief 解析一条姿态规则为确定性参考姿态/方向并留痕 resolution 来源
 *        （§5.3 隔离声明的落位；深度解析归评估侧——本函数零坐标变换、
 *        零姿态求解）。
 *
 * 执行序（固定——确定性来源，NFR-COR-02；短路——首个失败即返回）：
 *   ①参数面：validateOrientationRule(rule)（构造边界单点复用——
 *     NFR-MNT-04；非有限角/零向量目标/缺 feature/rollRange 逆序在此
 *     拒绝）→ 失败：error＝该值；diag＝REQ-READY-POSE-ILLEGAL（context
 *     携 "field=<params.field>"——与就绪 R3 同键对齐）；
 *   ②引用面（仅引用型规则）：AlignFrame.targetFrame／
 *     AlignGeometryNormal.targetSceneObject 对 closure.closureRefs 做
 *     浅核对（目标存在＋objectTypeToken 与 expectedTargetToken 一致
 *     ——§8.1 专项声明：仅读元数据，不解码 modeling 对象字节）→
 *     失败：diag＝REQ-READY-REF-MISSING（context 携 field/target/
 *     expected-token——与就绪 R1 同键对齐）；error 以 IllegalTolerance
 *     承载（域错误表"引用悬空"的值面就近族——机器判别以稳定码为准，
 *     与 Readiness 值面同款取舍）；
 *   ③产出装配：按 kind 填充解析值（见 OrientationResolution 字段表）
 *     ——Fixed 原样留痕参数字面；PointAtTarget 归一化为单位向量
 *     （‖targetPoint‖ 的 IEEE754 确定运算）；引用型规则留痕核对通过
 *     的目标 ObjectId＋特征语义。
 *
 * @param rule    [in] 待解析姿态规则（§5.3 五规则；kind 与载荷匹配性
 *                由①核对）
 * @param anchor  [in] 所属需求条目锚（诊断 subject/localName 回指面）
 * @param closure [in] 修订闭包 objectRefs 元数据（§8.1 浅校验数据面；
 *                仅读 objectId 与 objectTypeToken 两字段）
 * @return 解析产出（ok＝解析值有效；!ok＝error＋diag 定位）
 *
 * @throws 无——一切失败经返回值面（§9.2 总则：值语义产出；anchor 的
 *         entryName 允许为空串——诊断 localName 届时以 optional 空承
 *         载，不伪造名称）
 *
 * 纯函数；线程安全；确定性。
 */
OrientationResolution resolveOrientationRule(const OrientationRule& rule,
                                             const OrientationRuleAnchor& anchor,
                                             const CheckContext& closure);

}  // namespace sdurws::ird::requirements

#endif  // IRD_REQUIREMENTS_ORIENTATIONRESOLUTION_HPP
