/**
 * @file   Ptp.hpp
 * @brief  关节空间 PTP 规划（§7）——构型选择规则（§7.5 三键全序）、逐轴
 *         线性插值（§7.3）、端点限位守卫（§7.4/V-03）与 planPtpSegment
 *         段规划出口（WP-16-T04 批；TRJ-01"关节空间点到点路径"承载面）。
 *
 * 设计依据：
 *   - units/trajectory.md §7.1（PTP 能力分界——几何＝关节空间逐轴线性，
 *     不提供笛卡尔连续性）、§7.2（输入与前置——起点/终点构型来源、零长
 *     段合法）、§7.3（逐轴线性＋多关节同步——同步系数 s 对各轴一致）、
 *     §7.4（限位凸组合性质；限速/限加速度不在几何阶段判定——§12 唯一
 *     判定点）、§7.5（构型与候选解选择——延续性优先→裕量优先→稳定
 *     编号兜底三规则）、§7.6（失败语义表——PTP 侧素材归类）
 *   - §15.1（PlanPtpSegment 接口基线——签名/输出/前置/非法示例；"本域
 *     内部接口实现任务允许按 DTB §5.4 微调并登记偏差"）
 *   - §15.0（错误类型/取消/确定性/线程/副作用通用约定）
 *   - 需求 TRJ-01（关节空间点到点路径）、TRJ-06（限制超标定位——V-03
 *     素材轨）、NFR-COR-02（确定性选择序）
 *   - 任务契约 tasks/foundation/WP-16-T04.json（acceptance 1——PTP 模型
 *     测试；黄金基准＝解析算例，V-01）
 *
 * 背景说明（几何与时间的解耦——本头为什么不做限速/时间）：§7.3 原文
 * "同步时间：时间参数化阶段（§12）以'最严关节'决定段时长，几何与时间
 * 解耦——PTP 段的几何不因限值改变"。本头只产出**几何**（段计划＋段
 * 路点）；限速/限加速度判定与节拍输出在 WP-16-T08 的时间参数化按附录
 * D 第 10 项容差执行——本域零双口径（§12.2 唯一判定点）。
 *
 * 碰撞边界：§7.6 的候选解碰撞过滤、必经状态碰撞素材与避障搜索属 policy
 * 会话消费面（WP-16-T06/T07）——本批 planPtpSegment 不消费 policy，
 * PtpStatus 词表本批仅三值（Ok/NoPath/Canceled），碰撞面两值随 T06 表尾
 * 追加（kinematics KinematicsErrorCode 表尾追加先例）。
 *
 * 线程安全：全部纯函数/值类型，无共享可变状态（§15.0——请求/结果值
 * 语义，无跨调用状态）。确定性：同输入同选择同几何（候选选择序 §7.5
 * 全序；浮点运算无归约顺序歧义）。
 */

#ifndef IRD_TRAJECTORY_PTP_HPP
#define IRD_TRAJECTORY_PTP_HPP

#include <cstdint>
#include <optional>
#include <vector>

#include <rw/math/Q.hpp>

#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/trajectory/TrjTypes.hpp>

namespace sdurws::ird::trajectory {

// =====================================================================
// 关节空间插值（§7.3 几何路径的唯一实现点——段几何/复检/动画共用）
// =====================================================================

/**
 * @brief 关节空间逐轴线性插值（§7.3——q(s) = (1-s)·q_a + s·q_b）。
 *
 * 同步语义（§7.3"保证全部关节同时到达"）：同步系数 s 对各轴一致——
 * s 是"关节空间弧长参数"的归一化行程比例，不是时间；时间律（各轴同时
 * 到达的时刻映射）在 §12 时间参数化按最严关节统一给出。
 *
 * @param qa  [in] 起点构型（权威关节向量，转动 rad／移动 m；qb 同维度）
 * @param qb  [in] 终点构型（同上）
 * @param s   [in] 行程参数，∈[0,1]（0＝qa，1＝qb；越界属调用方违约）
 *
 * @return 插值构型（逐轴 (1-s)·qa[i]+s·qb[i]；单轴差为零时该轴恒值——
 *         §7.3 边界）
 *
 * @throws TrajectoryError token "trajectory/ptp/interpolate-"：两向量
 *         维度不一致，或 s 越出 [0,1]（调用方契约违约——fail-fast）
 *
 * 纯函数；线程安全；确定性（同输入同输出位级一致——无归约歧义）。
 */
rw::math::Q interpolateJointLinear(const rw::math::Q& qa, const rw::math::Q& qb, double s);

// =====================================================================
// 构型选择（§7.5 三规则——确定性全序；单点实现）
// =====================================================================

/**
 * @brief 按确定性规则从候选解集中选择 PTP 终点构型（§7.5 全文实现）。
 *
 * 三规则（原文序，逐级收紧直至全序）：
 *   1. **延续性优先**：与起点构型逐轴 |Δq| 全部 ≤ ikContinuityThreshold
 *      的候选构成优先集——避免无谓的构型跳变；
 *   2. **裕量优先**：在当前集（延续集非空取延续集，否则取全集）内按
 *      minimumJointMargin **降序**排（与 kinematics 稳定排序第一键一致；
 *      +∞ 裕量参与全序取最大——全无界链的合法形态）；
 *   3. **稳定编号兜底**：裕量精确相等并列时取 stableIndex **升序**最小者
 *      （四键排序的最终键，保证全序；精确相等判定＝浮点位级相等——裕量
 *      来自同一 D-KIN-6 归一化计算的 f64 值，位级判定稳定可复现）。
 *
 * 规则 1 与规则 2 的组合是本实现的确定性补全（§7.5 原文规则 2 只写了
 * "无延续候选时"的分支——延续候选多于一个时同样按 2/3 键在延续集内
 * 排序，保证任何输入下唯一解；无语义冲突，登记于单元卡 §21.5）。
 *
 * @param startQ                [in] 起点构型（权威角 rad|m——延续性判定
 *                              的参照点；**不得取会话姿态**，§5.3/KIN-06）
 * @param candidates            [in] 终点候选解集（§7.5 输入三键齐备；
 *                              非空——空集属调用方前置违约）
 * @param ikContinuityThreshold [in] 延续阈值（逐轴上界；转动 rad／移动
 *                              m；>0 有限——config.trj 投影）
 *
 * @return 被选候选的下标（输入向量序；选择结果与过程由调用方按 §7.5
 *         第 4 条记录入轨迹身份与证据——planPtpSegment 已代为记录）
 *
 * @throws TrajectoryError token "trajectory/ptp/choose-"：候选集为空
 *         （§15.1 非法示例——前置违约 fail-fast）、候选 q 维度不一致、
 *         裕量 NaN（投影面数据违约）或阈值非法（≤0/非有限）
 *
 * 纯函数；线程安全；确定性（同输入恒同选择——NFR-COR-02）。
 */
std::size_t choosePtpCandidate(const rw::math::Q& startQ,
                               const std::vector<PtpCandidate>& candidates,
                               double ikContinuityThreshold);

// =====================================================================
// PTP 段规划（§15.1 PlanPtpSegment 的 T04 落地面）
// =====================================================================

/**
 * @brief PTP 段规划结局（§15.1 status 词表的 T04 子集——碰撞面两值
 *        SearchExhausted/MandatoryStateCollisionMaterial 随 WP-16-T06/T07
 *        消费 policy 会话时表尾追加；封闭词表演进＝单元卡登记）。
 */
enum class PtpStatus : std::uint8_t {
    /// 规划成功——段几何确定（waypoints 含端点；§15.1 后置）。
    Ok,
    /// 无路径（本批产生面＝端点越限位守卫拒绝——TRJ-LIMIT-EXCEEDED 素材；
    /// IK 无解素材在序列展开层产出——§7.6 行 5）。
    NoPath,
    /// 取消（取消观测命中——**取消不是错误**（UX-03）：零错误素材，
    /// 调用方以 Canceled 语义收尾，§6.7）。
    Canceled,
};

/**
 * @brief PTP 段规划请求（§15.1 PtpRequest 的 T04 字段面——全部值语义）。
 *
 * 前置（§15.1 原文）与合法域：
 *   - candidates 非空；每候选 q 与 startQ 同维度且全分量有限；
 *   - lowerBoundQ/upperBoundQ 与 startQ 同维度且逐轴 lower<upper
 *     （评价区间视图——§5.3 CanonicalJoint.bounds 投影，宿主注入；
 *     限值真值仍归模型，本结构零副本语义的区间快照仅作守卫判定）；
 *   - ikContinuityThreshold 有限且 >0（rad|m 逐轴上界）；
 *   - constraint 合法（limitsScaleFactor∈(0,1] 等——SegmentConstraint 注）；
 *   - tcpRef 为合法对象身份（§6.3"tcpRef 必填（对象 ID）"）；
 *   - startKind/endKind ∈ {Start, TaskPoint, End}（PTP 段端点不是 Via/
 *     Dwell——后者由序列展开/时间化产出）。
 *
 * 单位/坐标系：全部关节向量＝权威角（转动 rad／移动 m）；无笛卡尔量。
 */
struct PtpRequest {
    /// 起点构型（§7.2——上一段终点构型或 startStateRef 解析构型；禁止
    /// 会话姿态，§5.3/KIN-06）。
    rw::math::Q startQ;
    /// 评价区间下界（逐轴；rad|m——§7.4 端点限位守卫的判定基准）。
    rw::math::Q lowerBoundQ;
    /// 评价区间上界（逐轴；rad|m）。
    rw::math::Q upperBoundQ;
    /// 终点候选解集（§7.5 三键；KinematicsPort 投影注入——PtpCandidate 注）。
    std::vector<PtpCandidate> candidates;
    /// 构型延续阈值（rad|m 逐轴上界；config.trj.ikContinuityThreshold）。
    double ikContinuityThreshold = 1e-6;
    /// 段级约束（限值引用＋缩放系数＋采样步长——SegmentConstraint）。
    SegmentConstraint constraint;
    /// TCP/工具对象引用（ARC-04 对象 ID；必填合法——§6.3）。
    core::ObjectId tcpRef;
    /// 参考系对象（World 时 nullopt——不伪造默认，§6.3）。
    std::optional<core::ObjectId> frameRef;
    /// 来源任务点（站间转移段＝到达站；nullopt＝序列级工具段——§6.2；
    /// 终点路点 kind==TaskPoint 时必填合法——§6.3 路点可追溯性）。
    std::optional<core::ObjectId> sourceTaskPoint;
    /// 起点路点的来源任务点（仅 startKind==TaskPoint 有语义——站间转移段
    /// 的起点是上一站任务点构型，其路点级来源＝上一站对象；§6.2
    /// Waypoint.sourceTaskPoint"TaskPoint kind 必填"的起点侧承载。
    /// startKind==TaskPoint 时必填合法，其余形态必须为空）。
    std::optional<core::ObjectId> startSourceTaskPoint;
    /// 起点路点种类（Start＝序列首段；TaskPoint＝站间转移）。
    WaypointKind startKind = WaypointKind::Start;
    /// 终点路点种类（TaskPoint＝到达任务点；End＝收尾段）。
    WaypointKind endKind = WaypointKind::TaskPoint;
    /// 段序号（0 基；写入段与路点的定位键）。
    std::uint32_t segmentIndex = 0;
    /// 取消观测（可空＝不可取消；在候选选择前与几何完成后轮询——§15.1
    /// "规划循环边界轮询"）。
    CancelSignal cancel;
};

/**
 * @brief PTP 段规划结果（§15.1 SegmentPlanResult 的 T04 字段面）。
 *
 * status==Ok 时 segment 填满且过 validateTrajectorySegment；status==
 * NoPath 时 failure 必填（可定位素材——§15.1 后置"失败时
 * FailedSegmentRecord 可定位"）；status==Canceled 时 failure 必为空
 * （取消不是错误——UX-03，零错误素材）。selectedCandidateIndex 仅 Ok
 * 时有值（§7.5 第 4 条——选择过程与结果记录入身份与证据，review 可
 * 回放；值＝请求 candidates 向量序下标）。
 *
 * 诊断边界：本结构承载域内素材（FailedSegmentRecord）；DiagnosticRecord
 * 实例（TRJ-* 码经 IDiagnosticFactory）归评估器组装面（WP-16-T09/T10）
 * ——§14.4"诊断构造：经 IDiagnosticFactory.create"。
 */
struct PtpPlanResult {
    /// 规划结局（PtpStatus 词表——碰撞面两值随 T06 追加）。
    PtpStatus status = PtpStatus::Canceled;
    /// 规划产出的段（Ok 时有效且过结构校验；其余状态无段）。
    TrajectorySegment segment;
    /// 被选候选下标（请求 candidates 向量序；仅 Ok 时有值——§7.5 第 4 条）。
    std::optional<std::size_t> selectedCandidateIndex;
    /// 失败定位素材（NoPath 时必填；Canceled 时恒空——UX-03）。
    std::optional<FailedSegmentRecord> failure;
};

/**
 * @brief 规划一个关节空间 PTP 段（§7/§15.1——TRJ-01 点到点路径核心）。
 *
 * 执行序（每步语义见行内注释；任何一步失败即按 §7.6 归类返回）：
 *   1. 前置校验（调用方契约违约 → TrajectoryError fail-fast——维度/
 *      空候选集/阈值/约束非法/tcpRef 缺失）；
 *   2. 取消轮询（命中 → Canceled，零素材）；
 *   3. 端点限位守卫（§7.4——IK 硬过滤已保证，检查作为守卫；越限 →
 *      NoPath＋TRJ-LIMIT-EXCEEDED 素材，比较型字段含实际越限值/区间界/
 *      单位 rad|m——V-03 观测点）；
 *   4. 构型选择（§7.5 三键全序——choosePtpCandidate）；
 *   5. 几何构造（JointLinear 段：两路点端点＋逐轴线性语义；pathLength-
 *      Joint＝Σ|Δq|；TCP 路径长度为 FK 派生观察本批不计算——恒 0 不冒充）；
 *   6. 结构守卫（validateTrajectorySegment——段构造保证的自证）。
 *
 * 限速/限加速度**不在本函数判定**（§7.4——§12.2 唯一判定点，WP-16-T08）；
 * 碰撞不在本函数判定（§7.6——policy 会话消费面，WP-16-T06/T07）。
 *
 * @param request [in] 规划请求（PtpRequest 前置见其注）
 * @return 规划结果（PtpPlanResult 三态；值语义）
 *
 * @throws TrajectoryError 调用方契约违约（token "trajectory/ptp/..."——
 *         维度/空候选/非法阈值/非法约束等；见各校验点）
 *
 * 纯函数；无副作用/零修订/零写盘（§15.0）；线程安全；确定性。
 */
PtpPlanResult planPtpSegment(const PtpRequest& request);

}  // namespace sdurws::ird::trajectory

#endif  // IRD_TRAJECTORY_PTP_HPP
