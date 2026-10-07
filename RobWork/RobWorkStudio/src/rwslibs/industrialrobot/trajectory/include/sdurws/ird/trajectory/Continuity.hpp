/**
 * @file   Continuity.hpp
 * @brief  路径连接与连续性检查（§9）——段边界几何连续（位置/姿态，附录
 *         D C7 运行校验量纲）＋ trj.cartesian-ik-continuity 证据项适用性
 *         双路判定（§8.1 C2 口径：含笛卡尔段→适用；纯关节路径→显式标记
 *         不适用不计缺失；WP-16-T05 批）。
 *
 * 设计依据：
 *   - units/trajectory.md §9.1（连续性维度表——位置/姿态连续＝段边界两端
 *     TCP 差 ≤ 容差，"必检（段构造保证；检查作为守卫）"；逐元素比较经
 *     core::closeWithin；关节分支连续＝段内逐采样（笛卡尔段必检，由
 *     planCartesianLine 执行并记录——本头不重复）；时间维归 §12.5
 *     （WP-16-T08——本批 TimeParameterization 未落位，只做几何维））、
 *     §9.2（合法/非法连接与诊断示例——TRJ-CONTINUITY-BROKEN 素材字段：
 *     段序号、维度、实际差/容差比较型；"纯关节序列被请求笛卡尔连续性
 *     证据→不适用（非非法）→NotApplicable（原因=无笛卡尔段）"）、
 *     §9.3（检查执行点——几何连续在段构建与连接时即时检查）
 *   - §15.3（CheckContinuity 接口基线——"几何维随时可用；时间维需时间
 *     化结果"；输出"逐边界逐维度判定、违规清单、纯关节路径的笛卡尔项
 *     NotApplicable 标记"；前置"段序号连续；容差来源已解析"）
 *   - §13.3（trj.cartesian-ik-continuity 适用条件＝路径含笛卡尔段）、
 *     §14.4（TRJ-CONTINUITY-BROKEN——error，比较型：实际差/容差/单位）
 *   - REQUIREMENTS §8.1 表 4 C2（"必需证据项按其固有适用条件生效——条件
 *     不满足时显式标记'不适用'，不计缺失（例：纯关节空间路径不含笛卡尔
 *     段，TRJ-02 段内 IK 连续性检查不适用）"）、附录 D C4（通用比较公式
 *     |a−b| ≤ ε_rel·|ref|＋ε_abs）与 C7 运行校验量纲（长度 1×10⁻¹² m、
 *     角度 1×10⁻¹² rad）
 *   - 需求 TRJ-02（沿途 IK 连续性检查的证据面）、TRJ-05 侧的几何半区
 *     （时间维随 T08）、TRJ-06（连续性破坏给出具体段落与原因）
 *   - 任务契约 tasks/foundation/WP-16-T05.json（acceptance 1——适用条件
 *     双路用例：含笛卡尔段→检查；纯关节路径→标记不适用）
 *
 * 背景说明（本批几何维的边界——如实登记）：
 *   - 段边界 TCP 位姿仅在路点携带 target（CartesianLine 段路点）时直接
 *     可得；JointLinear 段路点只携带 q（其 TCP 位姿是 FK 派生观察——
 *     §7.6"纯关节路径不得伪装成笛卡尔路径"）。故边界两侧任一侧无
 *     target 时，该边界的笛卡尔连续维度显式标记 NotApplicable（无笛卡尔
 *     表达可比较——不是通过也不是缺失），与 C2"显式标记不适用"同口径。
 *   - 关节分支连续（§9.1 表"笛卡尔段必检"）的执行点在段内逐采样
 *     （planCartesianLine 的 IkContinuityRecord）——本头只做**段间边界**
 *     几何守卫，不复算段内检查（单一判定点，防双口径）。
 *   - 时间维（速度/加速度连续——TRJ-05 R1 验收重点）需要
 *     TimeParameterization（§15.3"时间维需时间化结果"），随 WP-16-T08
 *     落位后增列重载——本批接口不含时间维（不预建占位，NFR-MNT-04）。
 *
 * 线程安全：纯函数/值类型（§15.3"纯函数、可重入"）。确定性：同输入同
 * 判定（比较经 core::closeWithin 逐元素——NFR-COR-01/02）。
 */

#ifndef IRD_TRAJECTORY_CONTINUITY_HPP
#define IRD_TRAJECTORY_CONTINUITY_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <sdurws/ird/trajectory/TrjTypes.hpp>

namespace sdurws::ird::trajectory {

// =====================================================================
// 容差（§9.1 容差来源合法性声明——"本卡不发明其他数值"：引用附录 D C7）
// =====================================================================

/**
 * @brief 段边界几何连续容差（附录 D C7 运行校验量纲的运行时默认）。
 *
 * 来源合法性（§9.1 容差来源声明原文）：位置 ε_abs＝1×10⁻¹² m、姿态
 * ε_abs＝1×10⁻¹² rad——附录 D C7 运行校验默认值（非本卡发明）；比较
 * 一律经 core::closeWithin 逐元素（C4 公式 |a−b| ≤ ε_rel·|ref|＋ε_abs，
 * ε_rel 取 0——纯绝对容差，C7 运行校验口径）。
 *
 * 值语义纯结构；线程安全。
 */
struct GeometricContinuityTolerances {
    /// 位置连续 ε_abs（m；附录 D C7 长度 1×10⁻¹²）。
    double positionAbsM = 1e-12;
    /// 姿态连续 ε_abs（rad；附录 D C7 角度 1×10⁻¹²）。
    double orientationAbsRad = 1e-12;
};

// =====================================================================
// 边界判定行与报告（§15.3 ContinuityReport 的 T05 字段面）
// =====================================================================

/**
 * @brief 单个段边界的几何连续判定行（边界 i＝段 i 末路点与段 i+1 首路
 *        点之间；§15.3"逐边界逐维度判定"）。
 *
 * 维度语义：cartesianApplicable==false（任一侧路点无 target——纯关节
 * 边界）时 positionOk/orientationOk 均无值（NotApplicable——非通过非缺
 * 失，§9.2 行 6 口径）；==true 时两值必有且为逐维度判定。
 *
 * 值语义纯结构；线程安全。
 */
struct BoundaryContinuityEntry {
    /// 边界序（0 基——段 i 与段 i+1 之间＝边界 i）。
    std::uint32_t boundaryIndex = 0;
    /// 前段序号（0 基）。
    std::uint32_t beforeSegmentIndex = 0;
    /// 后段序号（0 基）。
    std::uint32_t afterSegmentIndex = 0;
    /// 笛卡尔维度适用性（false＝任一侧路点无 target——纯关节边界，
    /// 笛卡尔维度 NotApplicable）。
    bool cartesianApplicable = false;
    /// 位置连续判定（仅 cartesianApplicable 时有值；true＝段边界两端
    /// TCP 位置逐分量差在容差内——§9.1）。
    std::optional<bool> positionOk;
    /// 姿态连续判定（仅 cartesianApplicable 时有值；true＝相对旋转角
    /// 在容差内——最短弧口径，rad）。
    std::optional<bool> orientationOk;
    /// 实测位置差（m；两位置向量的最大逐分量绝对差——比较型素材的
    /// "实际值"来源；NotApplicable 时无值）。
    std::optional<double> positionDeltaM;
    /// 实测姿态差（rad；相对旋转的最短弧角——同上）。
    std::optional<double> orientationDeltaRad;
};

/**
 * @brief 几何连续性检查报告（§15.3 ContinuityReport 的 T05 字段面）。
 *
 * 消费面：违规清单（violations）是 TRJ-CONTINUITY-BROKEN 素材轨——评估
 * 器组装诊断与证据时消费（本域不自行判定"轨迹可行"，§9.3/N7）；逐边界
 * 判定行入证据素材（可回放）。
 *
 * 值语义纯结构；线程安全。
 */
struct ContinuityReport {
    /// 逐边界判定行（段数 ≥2 时非空；恰 n−1 行——n 为段数）。
    std::vector<BoundaryContinuityEntry> boundaries;
    /// 违规素材清单（§9.2——TRJ-CONTINUITY-BROKEN：段序号/维度/实际差/
    /// 容差比较型；无违规时为空）。
    std::vector<FailedSegmentRecord> violations;
};

// =====================================================================
// C2 适用性双路（§13.3 trj.cartesian-ik-continuity 适用条件）
// =====================================================================

/**
 * @brief trj.cartesian-ik-continuity 证据项的适用性判定值（§8.1 C2
 *        双路——适用/不适用）。
 *
 * 适用条件（§13.3 原文）："适用条件＝路径含笛卡尔段；纯关节路径→
 * NotApplicable（不计缺失，C2 例）"。值语义纯结构；线程安全。
 */
struct CartesianIkContinuityApplicability {
    /// true＝路径含笛卡尔段（检查适用——逐段连续性记录由
    /// planCartesianLine 产出）；false＝纯关节路径（显式不适用）。
    bool applicable = false;
    /// 不适用原因（中文，ERR-01 口径；applicable==true 时为空——
    /// "原因=无笛卡尔段"即 §9.2 行 6 承载）。
    std::string notApplicableReason;
};

/**
 * @brief 判定 trj.cartesian-ik-continuity 证据项对给定路径的适用性
 *        （§8.1 C2/§13.3——适用条件双路判定的唯一实现点）。
 *
 * 判定规则（§13.3 原文口径）：任一段 spaceType==CartesianLine → 适用；
 * 否则（含空段序列）→ 不适用，原因"路径不含笛卡尔段（纯关节路径——
 * 表 4 C2 例，显式标记不计缺失）"。本函数只读 spaceType 字段做纯分类
 * （不校验段结构——结构守卫归 checkGeometricContinuity/段构造面；两
 * 函数职责单一，防分类面被结构错误噪声淹没）。
 *
 * @param segments [in] 有序段序列（可空——空序列＝无笛卡尔段，不适用）
 *
 * @return 适用性判定（CartesianIkContinuityApplicability——双路）
 *
 * 纯函数；线程安全；确定性。
 */
CartesianIkContinuityApplicability cartesianIkContinuityApplicability(
    const std::vector<TrajectorySegment>& segments);

// =====================================================================
// 段边界几何连续检查（§9.1 位置/姿态维——守卫面）
// =====================================================================

/**
 * @brief 检查段边界几何连续性（§9.1 位置/姿态维＋§15.3 几何维——段
 *        构造保证的运行期守卫）。
 *
 * 执行序（每步语义见行内注释）：
 *   1. 前置校验（fail-fast——调用方契约违约）：段序号 0 基连续（§15.3
 *      前置原文）；容差有限且 >0（"容差来源已解析"的合法域面）；逐段
 *      结构不变量（validateTrajectorySegment——双域纪律的守卫复用）；
 *   2. 逐边界（i=0..n−2）：前段末路点 × 后段首路点——
 *      a. 任一侧无 target → 边界笛卡尔维度 NotApplicable（cartesian-
 *         Applicable=false，两判定无值——纯关节边界，§9.2 行 6 口径）；
 *      b. 两侧均 target → 位置逐分量差（m，取最大逐分量绝对差）与
 *         姿态最短弧角（rad）分别按附录 D C7 容差判定（core::closeWithin，
 *         零参考退化 ε_abs）；判定的"参考值"＝后段边界值（连接目标——
 *         位置逐分量以 b 为 ref；姿态角差的 ref=0 退化为 ε_abs）；
 *   3. 违规素材（§9.2：TRJ-CONTINUITY-BROKEN——段序号、维度、实际差/
 *      容差比较型三要素，单位 m/rad 逐维度）——每个违规维度一条素材，
 *      boundaryIndex/前后段序号入 cause 文案可定位。
 *
 * 分支连续（§9.1"笛卡尔段必检"）不在本函数：其执行点＝段内逐采样
 * （planCartesianLine 的 IkContinuityRecord）——单一判定点（文件头注）。
 * 时间维（速度/加速度）随 WP-16-T08 增列重载（本批不预建）。
 *
 * @param segments      [in] 有序段序列（段数 ≥2 才有边界；0/1 段返回空
 *                      报告——无边界即无违规，非错误）
 * @param tolerances    [in] 容差（附录 D C7 量纲——缺省值即运行校验默认；
 *                      调用方可传更严值，不得放宽——C7 口径）
 *
 * @return 报告（逐边界判定行＋违规素材；无违规时 violations 为空）
 *
 * @throws TrajectoryError token "trajectory/continuity/..."：段序号不连
 *         续/容差非法/段结构不变量违约（调用方契约违约——fail-fast）
 *
 * 纯函数；无副作用；线程安全；确定性（NFR-COR-02）。
 */
ContinuityReport checkGeometricContinuity(
    const std::vector<TrajectorySegment>& segments,
    const GeometricContinuityTolerances& tolerances);

}  // namespace sdurws::ird::trajectory

#endif  // IRD_TRAJECTORY_CONTINUITY_HPP
