/**
 * @file   Bounds.hpp
 * @brief  解析工作半径界限（§3.3 布局表 T04 行）——连杆长度和上界的
 *         推导、推导输入格式化与 AnalyticBoundExceeded 证明素材组装。
 *
 * 设计依据：
 *   - units/kinematics.md §3.3（公共头布局表 Bounds.hpp 行——"解析工作
 *     半径界限（不可行证明素材）"，任务 T04；契约 note 登记的落点偏差
 *     "卡 §11 T03 行提及 Bounds 归 §3.3 布局表 T04"已随 v0.3 消歧）、
 *     §5.4 结局 5 行（AnalyticBoundExceeded——证明**素材**非裁定）、
 *     §8.2（DeterministicInfeasibilityProof 素材——"解析界限（工作半径
 *     推导输入＋目标距离）"）、§5.5（KIN-TARGET-ILLEGAL 语义锚）
 *   - evidence 冻结契约 Evidence.hpp：DeterministicInfeasibilityProof/
 *     ProofCategory/kCoverageClaimAllAlternatives/validateProof 的素材面
 *     字段（§6.3——本头只组装素材，校验归 evidence）
 *   - 任务契约 tasks/foundation/WP-15-T04.json acceptance 4（"解析工作
 *     半径界限：目标超界产 AnalyticBoundExceeded 证明素材（附推导输入、
 *     expectedSliceId 绑定）——仅素材不裁定"）
 *
 * 背景说明（为什么上界取"逐段平移贡献之和"）：对链式 FK，TCP 位置是
 * 逐段平移向量经旋转复合后的和；旋转不改变向量模长，三角形不等式给出
 * |p_tcp(q)| ≤ Σ（逐段平移模长的最大值），与关节角取值无关——静态可
 * 验证且**覆盖全部可能解**（§5.4 结局 5"静态可验证、覆盖全部可能解"
 * 的数学根据）。逐段贡献＝每个关节 origin 的平移模长（Fixed 关节亦有）
 * ＋每个移动关节的行程贡献 max(|qmin|,|qmax|)＋TCP 偏置平移模长。
 * 上界只约束**位置半径**，不含姿态可达性（§5.5 前置声明——素材的适用
 * 前提写入 proof.preconditions）。
 *
 * "仅素材不裁定"纪律：目标超界时本单元产出
 * evidence::DeterministicInfeasibilityProof（category=AnalyticBound）
 * 候选并绑定 expectedSliceId（D-09 可校验）；证明成立与否由 evidence
 * validateProof 逐字段校验后才能进入任务级判定（§8.1——kinematics 只
 * 产素材，aggregateVerdict 归 evidence）。
 *
 * 线程安全：全部实体为纯值/纯函数（无共享可变状态）；并发只读安全。
 * 确定性：推导输入的文本格式化用定点 "%.17g"（C 默认 locale——进程不调
 * setlocale，登记为确定性来源；NFR-COR-01/02：同输入同字节）。
 */

#ifndef IRD_KINEMATICS_BOUNDS_HPP
#define IRD_KINEMATICS_BOUNDS_HPP

#include <string>
#include <vector>

#include <rw/math/Transform3D.hpp>

#include <sdurws/ird/evidence/Evidence.hpp>     // DeterministicInfeasibilityProof（素材承载——§6.3）
#include <sdurws/ird/kinematics/Fk.hpp>         // Expected（非抛出查询轨）
#include <sdurws/ird/kinematics/KinTypes.hpp>   // TcpRef/IKinRuntimeView/IkRequestIdentity/IkTargetRef

namespace sdurws::ird::kinematics {

// =====================================================================
// 域常量（唯一书写点——证明素材与测试共用，禁第二处字面量）
// =====================================================================

/// 解析界限论断 token（evidence §6.3"域登记的具体论断 id"——词表归域；
/// 本单元登记值＝"位置距离超出连杆长度和上界"，登记随卡 §14.6 v0.4）。
inline constexpr char kReachBeyondLinkSumClaim[] = "kin.reach-beyond-link-sum";

// =====================================================================
// AnalyticReachBound——界限推导值（推导输入的值面）
// =====================================================================

/**
 * @brief 解析工作半径上界的推导输入与结果（§8.2"工作半径推导输入＋
 *        目标距离"的值面；全部平移量单位 m）。
 *
 * 值语义纯结构；线程安全（并发只读）。确定性：同 (view, tcp) 同值
 * （浮点运算定序固定——逐关节单遍累加）。
 */
struct AnalyticReachBound {
    /// 逐关节 origin 平移模长（m；链序含 Fixed 关节——T_acc 复合的逐段
    /// 贡献；长度＝链内关节总数）。
    std::vector<double> jointOriginNorms;
    /// 逐移动关节的行程贡献 max(|qmin|,|qmax|)（m；链序，仅 Prismatic
    /// 项非零——转动/连续关节运动不产生平移）。
    std::vector<double> prismaticStrokes;
    /// TCP 偏置平移模长（m——T_flange_tcp 的 |p|）。
    double tcpOffsetNorm = 0.0;
    /// 工作半径上界（m）＝ΣjointOriginNorms＋ΣprismaticStrokes＋
    /// tcpOffsetNorm——目标位置模长严格大于本值 ⇒ 位置不可达（对全部
    /// 关节角取值成立）。
    double totalRadius = 0.0;
};

/**
 * @brief 计算解析工作半径上界（§5.4 结局 5 的静态推导；纯函数）。
 *
 * @param view [in] 请求绑定的只读模型视图（调用方持有；调用期间存活）
 * @param tcp  [in] TCP 引用（快照内解析——KinTypes.hpp 解析规则；界限
 *             含 TCP 偏置贡献，故 TCP 必须可解析）
 *
 * @return 成功＝推导输入与上界；失败＝KinematicsError 值：
 *           - NoDevice：视图模型无可用设备链（§9.6 KIN-NO-DEVICE 素材）；
 *           - NoTcp：快照无工具或 tcpRef 悬空（§9.6 KIN-NO-TCP 两分）；
 *           - FrameUnresolved：tcpKey 不命中 canonical TCP（KinTypes 规则）。
 *
 * 纯函数；线程安全；确定性（同输入同字节）。
 */
Expected<AnalyticReachBound> computeAnalyticReachBound(const IKinRuntimeView& view,
                                                       const TcpRef& tcp);

/**
 * @brief 把推导输入格式化为确定文本（proof.boundExpression 的素材面——
 *        "界限表达（域登记）"的人类可读推导链）。
 *
 * 格式（逐段 "%.17g" 定点——C 默认 locale；确定性来源见文件头注）：
 * "‖p_target‖(m)=<d> > Σ(m)=<total>；推导：r_i=[...]; s_i=[...]; t=<t>
 * （三角形不等式上界——覆盖全部关节角取值；不含姿态可达性）"。
 *
 * @param bound         [in] 界限推导值（computeAnalyticReachBound 产出）
 * @param targetDistance [in] 目标位置模长 ‖p_target‖（m——基座系）
 * @return 确定文本（同输入同字节——NFR-COR-01）
 *
 * 纯函数；线程安全；不抛。
 */
std::string formatReachDerivation(const AnalyticReachBound& bound,
                                  double targetDistance);

// =====================================================================
// AnalyticBoundMaterial——结局 5 的证明素材（值面＋proof 候选）
// =====================================================================

/**
 * @brief AnalyticBoundExceeded 的完整素材包（§5.4 结局 5 产出列：
 *        DeterministicInfeasibilityProof 候选＋推导输入＋目标距离）。
 *
 * 生命周期/所有权：纯值；随 IkOutcome 交付。证明**素材**语义（§5.4/§8.1
 * ——仅素材不裁定）：proof 的存在不断言任务不可行；成立与否由 evidence
 * validateProof（producer 注册/契约版本/snapshotId/expectedSliceId 绑定/
 * 覆盖声明五查）校验后采信。
 *
 * 线程安全（并发只读）；确定性（同输入同 proof 字节面——格式化定点）。
 */
struct AnalyticBoundMaterial {
    /// 界限推导输入与上界（m）。
    AnalyticReachBound bound;
    /// 目标位置模长 ‖p_target‖（m——基座系；与上界比较的对象）。
    double targetDistance = 0.0;
    /// 证明素材候选（evidence §6.3——category=AnalyticBound；snapshotId/
    /// sliceId 绑定取自请求身份＝expectedSliceId 绑定；producer＝
    /// kin-task-point-ik 评估键＋契约版本）。
    evidence::DeterministicInfeasibilityProof proof;
};

/**
 * @brief 组装解析界限证明素材（目标位置超出上界时由求解器调用；纯函数）。
 *
 * @param bound          [in] 界限推导值（computeAnalyticReachBound 产出）
 * @param targetInBase   [in] 目标位姿（基座系 {B}——只消费其平移模长；
 *                            姿态不在本素材的界限前提内）
 * @param identity       [in] 请求身份（proof.snapshotId/sliceId 绑定源
 *                            ——expectedSliceId 绑定，D-09 可校验）
 * @param targetRef      [in] 任务点绑定（proof.subject＝pointOid）
 *
 * @return 素材包（proof 字段组装：category=AnalyticBound、claimToken=
 *         kReachBeyondLinkSumClaim、boundExpression=formatReachDerivation、
 *         preconditions/coverageClaim=§6.3 规范值、producer=评估键＋契约
 *         版本；零值身份照实写入——validateProof 的"非空绑定"查证归
 *         evidence，纯服务直调面允许未绑定身份）
 *
 * 纯函数；线程安全；确定性（同输入同字节）。
 */
AnalyticBoundMaterial makeAnalyticBoundMaterial(const AnalyticReachBound& bound,
                                                const rw::math::Transform3D<double>& targetInBase,
                                                const IkRequestIdentity& identity,
                                                const IkTargetRef& targetRef);

}  // namespace sdurws::ird::kinematics

#endif  // IRD_KINEMATICS_BOUNDS_HPP
