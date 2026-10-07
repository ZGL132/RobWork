/**
 * @file   TrjTypes.hpp
 * @brief  轨迹数据模型（WP-16-T04 面）——路点/段/约束/段计划的身份与
 *         几何值类型＋评估身份组＋失败段素材记录（TRJ-01 承载面）。
 *
 * 设计依据：
 *   - units/trajectory.md §6.1（数据形态六分类——本头落"离散路径"与
 *     "最终轨迹"的 T04 子集）、§6.2（核心类型设计——签名＝设计基线；
 *     实现任务允许按 DTB §5.4 微调并登记偏差）、§6.3（字段级义务清单
 *     ——身份/来源/约束/所有权逐项）、§6.4（与 runtime/policy/kinematics
 *     的类型边界——q 用 rw::math::Q、位姿用 rw::math::Transform3D，
 *     不包装不重定义）、§6.7（失败/取消/部分结果的表达）
 *   - §5.4（任务序列展开算法——SegmentPlan 展开产物的字段集合来源）、
 *     §5.6（运行身份要素——TrjSequenceIdentity 的语义出处）、§7.5
 *     （构型候选解的选择输入——PtpCandidate 三键）、§7.6（失败语义
 *     ——FailedSegmentRecord 的 reasonToken 归属）、§14.1.6（phase token
 *     词表——失败阶段定位）
 *   - 需求 TRJ-01（关节空间点到点路径＋任务点有序作业序列）、TRJ-06
 *     （失败段定位——具体段落与原因）、ARC-04（来源任务点对象 ID 可
 *     追溯——NFR-COR-04）、ARC-05（限值零私有副本——SegmentConstraint
 *     只存"引用＋缩放系数"）
 *   - 任务契约 tasks/foundation/WP-16-T04.json（acceptance 1/2——TRJ-01
 *     模型与身份绑定面）
 *
 * 背景说明（T04 批的落地面与演进边界——如实登记，防"占位"误读）：
 *   - 本头落 **T04 消费的类型全集**：段几何（TrajectorySegment/Waypoint/
 *     SegmentConstraint）、PTP 候选解（PtpCandidate）、评估身份组
 *     （TrjSequenceIdentity）、失败段素材（FailedSegmentRecord）与最终
 *     轨迹对象（Trajectory）的 T04 结构。
 *   - §6.2 基线中时间参数化（TimedSample/TimeParameterization）与质量
 *     指标（TrajectoryQuality/RecheckBudgetUsage）是 WP-16-T08 的消费面
 *     ——按 NFR-MNT-04（不预建占位）本批**不含**这两个类型，Trajectory
 *     的对应字段随 T08 增列（结构演进＝单元卡 §21.5 增量登记；本头注释
 *     即登记锚点）。
 *   - SegmentSpaceType 词表两值一次落全（封闭词表——§6.2 原文；Cartesian-
 *     Line 值由 WP-16-T05 的笛卡尔段规划产出消费，本批仅作展开产物的
 *     空间类型标记，不产生其几何）。
 *   - 坐标语义（§6.4/§6.3）：笛卡尔路点 target 为**基座系 {B}** 的 TCP
 *     位姿（T_world_base 之后的设备链上表达——MDL-22 单一不变量，AT-37
 *     契约；世界系表达只在派生观察层换算）。关节向量 q 一律**权威角**
 *     （q_authoritative = q_zeroOffset + q_rw，runtime.md §4.3.3；逐自由度
 *     SI：转动 rad／移动 m）。
 *   - 所有权/生命周期（§6.3）：全部值语义、深拷贝安全；域中间态生命周期
 *     ≤单次 evaluate 调用；Trajectory 构造后按不可变对待（无 setter——
 *     冻结纪律由调用方构造期一次性组装保证）。线程安全：并发只读安全；
 *     算法对象单线程使用（§15.0）。
 */

#ifndef IRD_TRAJECTORY_TRJTYPES_HPP
#define IRD_TRAJECTORY_TRJTYPES_HPP

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <rw/math/Q.hpp>
#include <rw/math/Transform3D.hpp>

#include <sdurws/ird/core/DiagData.hpp>    // core::ComparativeFields（比较型素材三要素——UX-03）
#include <sdurws/ird/core/Digest.hpp>      // core::ContentIdentity（快照/切片/策略身份）
#include <sdurws/ird/core/Identity.hpp>    // core::ObjectId（对象引用——ARC-04）
#include <sdurws/ird/trajectory/DiagCodes.hpp>  // TRJ-* 稳定码常量（失败素材 reasonToken 唯一书写点）

namespace sdurws::ird::trajectory {

// =====================================================================
// 域常量（唯一书写点——算法/测试/后续任务共用，禁第二处字面量）
// =====================================================================

/// 本域评估键（§14.2.1 评估器 descriptor 依赖声明原文 "trj-sequence-plan"
/// ——kebab 形，evidence 词形闸门内；评估器注册归 WP-16-T10，常量先落
/// ——身份组与素材消费面 T04 起引用）。
inline constexpr char kTrjEvaluationKey[] = "trj-sequence-plan";

/// 本域评估器输入/输出契约版本（§6.2 Trajectory.evaluatorContractVersion
/// "=descriptor.contractVersion"；一经交付不回退——CON-04 同款纪律）。
inline constexpr std::uint32_t kTrjEvaluatorContractVersion = 1U;

/// 轨迹算法版本 token（§6.2 algorithmVersion"实现版本变更→新身份→结果
/// 失效"；T04 批落位值——PTP/序列展开算法的版本标识，后续任务改动算法
/// 语义时必须递进并在单元卡登记）。
inline constexpr char kTrjAlgorithmVersion[] = "trj-alg-1";

/// 失败阶段 token：PTP 规划（§14.1.6 phase 词表首值——本批唯一产生面；
/// 后续 plan-line/plan-avoid/smooth/recheck/time-param 随 T05~T08 各自
/// 落位时增列常量，词表原文见卡 §14.1.6）。
inline constexpr char kPhasePlanPtp[] = "plan-ptp";

// =====================================================================
// 取消观测（§15.0 通用约定——一切长计算接口经注入取消观测）
// =====================================================================

/**
 * @brief 取消观测信号（域内最小取消面——§15.0 取消行的可注入形态）。
 *
 * 契约：可调用对象返回 true 表示"已请求取消"；空 std::function＝本次
 * 调用不可取消（合法缺省——单元测试/短计算场景）。评估路径的适配落点
 * ＝IEvaluationContext::cancellationRequested（evidence 卡 §9.3 调用
 * 契约）由评估器装配层注入（WP-16-T10），本域不消费任何 execution/
 * evidence 头文件实现取消（保持域内算法的依赖最小面）。
 *
 * 轮询纪律（§15.1）：在算法的段级/候选级循环边界轮询——不做逐元素
 * 轮询（开销不可控），不丢失取消请求（每轮必查）。
 */
using CancelSignal = std::function<bool()>;

// =====================================================================
// 路点与段（§6.2 设计基线的 T04 子集——逐字段出处见各注）
// =====================================================================

/**
 * @brief 轨迹段空间类型（§6.2 封闭词表两值；扩展＝需求变更＋本卡修订，
 *        本卡不预留空值——TRJ-08 词表纪律）。
 */
enum class SegmentSpaceType : std::uint8_t {
    /// 关节空间线性段（§7——q(s)=(1-s)·q_a+s·q_b 逐轴线性；T04 产生面）。
    JointLinear,
    /// 笛卡尔直线段（§8——TCP 直线平移＋姿态插值；WP-16-T05 产生面，
    /// 本批仅作序列展开产物的空间类型标记）。
    CartesianLine,
};

/**
 * @brief 路点种类（§6.2——必经状态标记供证明素材引用；Via＝规划器/平滑
 *        引入的中间点）。
 */
enum class WaypointKind : std::uint8_t {
    Start,      ///< 序列起点（startStateRef 解析构型——§5.6 必经状态 1）
    TaskPoint,  ///< 任务点构型（选定 IK 解——§5.6 必经状态 3）
    Via,        ///< 中间路点（规划器/平滑引入——非必经状态，C8 候选语义）
    Dwell,      ///< 驻留（时间轴常值段的路点表达——§5.4 步骤 3）
    End,        ///< 序列终点（§5.6 必经状态 2）
};

/**
 * @brief 单个路点（§6.2 Waypoint 行——关节空间或笛卡尔空间二选一）。
 *
 * 不变量（构造纪律，validateTrajectorySegment 逐点校验）：
 *   - q 与 target 至多一个有值（同一路点不得双域同时有效——§6.2 原文）；
 *   - kind==TaskPoint 时 sourceTaskPoint 必填（可追溯性 NFR-COR-04）；
 *   - kind==Dwell 时 dwellDurationS 必填且 >0（其余 kind 不携带）。
 *
 * 单位/坐标系（AGENTS §2.5）：q 为权威关节向量（转动 rad／移动 m）；
 * target 为基座系 {B} 的 TCP 目标位姿（T_world_base 之后，非世界系）；
 * dwellDurationS 单位 s。值语义纯结构；线程安全（并发只读）。
 */
struct Waypoint {
    /// 路点种类（必经状态语义见枚举注）。
    WaypointKind kind = WaypointKind::Via;
    /// 所属段序号（0 基，全轨迹单调——§6.3"诊断/复检/动画共用同一定位键"）。
    std::uint32_t segmentIndex = 0;
    /// 关节路点：权威关节向量（rad|m；nullopt＝非关节路点）。
    std::optional<rw::math::Q> q;
    /// 笛卡尔路点：TCP 目标位姿，基座系 {B}（nullopt＝非笛卡尔路点；
    /// WP-16-T05 产生面）。
    std::optional<rw::math::Transform3D<double>> target;
    /// 来源任务点对象（kind==TaskPoint 必填；可追溯性 NFR-COR-04）。
    std::optional<core::ObjectId> sourceTaskPoint;
    /// 驻留时长，单位 s（kind==Dwell 必填且 >0；其余 kind 恒 nullopt）。
    std::optional<double> dwellDurationS;

    bool operator==(const Waypoint& o) const
    {
        return kind == o.kind && segmentIndex == o.segmentIndex && q == o.q
            && target == o.target && sourceTaskPoint == o.sourceTaskPoint
            && dwellDurationS == o.dwellDurationS;
    }
    bool operator!=(const Waypoint& o) const { return !(*this == o); }
};

/**
 * @brief 段级约束（§6.2——限值只做"引用＋缩放系数"，绝不复制阈值；
 *        ARC-05：不持有影响计算的私有副本）。
 *
 * 真值来源（§5.3）：关节速度/加速度限值＝CanonicalJoint.maxVelocity/
 * maxAcceleration（SourcedValue<double>，SI：rad/s、rad/s²；未显式设值
 * ＝+inf）；采样步长与缩放系数＝config.trj（PlanConfig.hpp）。本结构
 * 零阈值副本——限值校验唯一判定点在时间参数化（§7.4/§12.2，WP-16-T08）。
 *
 * 合法域：limitsScaleFactor ∈ (0,1]（有限）；cartesianSampleStep >0 且
 * 有限（单位 m）。值语义纯结构；线程安全。
 */
struct SegmentConstraint {
    /// 限值使用比例（无量纲，(0,1]；1.0＝全限值——§5.5 limitsScaleFactor）。
    double limitsScaleFactor = 1.0;
    /// 笛卡尔段采样步长上限（m；来自 config.trj.cartesianSampleStep——
    /// 关节段不消费该值但仍随约束携带以保持约束完整）。
    double cartesianSampleStep = 0.05;
    /// 碰撞检查适用性（policy 启用且参与几何非空时 true——§6.2 原文；
    /// 本批恒由调用方装配面给定，trajectory 不解析 policy）。
    bool collisionCheckApplicable = false;

    bool operator==(const SegmentConstraint& o) const noexcept
    {
        return limitsScaleFactor == o.limitsScaleFactor
            && cartesianSampleStep == o.cartesianSampleStep
            && collisionCheckApplicable == o.collisionCheckApplicable;
    }
    bool operator!=(const SegmentConstraint& o) const noexcept { return !(*this == o); }
};

/**
 * @brief 单个轨迹段（§6.2 TrajectorySegment 行——几何路径＋段级约束与
 *        身份；时间参数化产物另存（T08），本结构只含几何与约束——§5.4
 *        "展开产物不含时间"）。
 *
 * 不变量（validateTrajectorySegment 校验）：waypoints 至少 2 个（含段
 * 端点——§6.2）；JointLinear 段全部路点携带 q、CartesianLine 段全部
 * 路点携带 target；Waypoint.segmentIndex==本段 segmentIndex。
 *
 * 单位：pathLengthJoint 为各轴 |Δq| 之和（转动 rad／移动 m 逐轴混合
 * 计量——§6.2 原文口径；逐关节分解见 TrajectoryQuality.jointPathLengths
 * ——T08）；pathLengthTcp 单位 m（仅 CartesianLine 段由 T05 填写；
 * JointLinear 段的 TCP 路径长度是 FK 派生观察——本批不消费 FK 端口，
 * 恒 0.0 且语义为"未计算"，不冒充笛卡尔证据——§7.6"纯关节路径不得
 * 伪装成笛卡尔路径"）。值语义纯结构；线程安全。
 */
struct TrajectorySegment {
    /// 段序号（0 基，全轨迹单调连续——§6.3 定位键）。
    std::uint32_t segmentIndex = 0;
    /// 空间类型（封闭词表——SegmentSpaceType 注）。
    SegmentSpaceType spaceType = SegmentSpaceType::JointLinear;
    /// 段内路点（含段端点；至少 2 个——§6.2 不变量）。
    std::vector<Waypoint> waypoints;
    /// TCP/工具对象引用（ARC-04 对象 ID；禁止运行时名称直存——§6.2 原文）。
    core::ObjectId tcpRef;
    /// 参考系对象（TaskPoint.refFrame 解析产物；World 时 nullopt——不伪造
    /// 默认，§6.3）。
    std::optional<core::ObjectId> frameRef;
    /// 段级约束（引用＋缩放系数——SegmentConstraint 注）。
    SegmentConstraint constraint;
    /// 来源任务点（approach/work/retract 归属站；站间转移段＝到达站——
    /// §7.2 该段服务于到达该站；序列级工具段无来源时 nullopt）。
    std::optional<core::ObjectId> sourceTaskPoint;
    /// 关节空间路径长度（各轴 |Δq| 之和；rad|m 逐轴混合计量——见结构注）。
    double pathLengthJoint = 0.0;
    /// TCP 参考点路径长度（m；派生观察——T05 起由笛卡尔段填写；本批恒 0）。
    double pathLengthTcp = 0.0;

    bool operator==(const TrajectorySegment& o) const
    {
        return segmentIndex == o.segmentIndex && spaceType == o.spaceType
            && waypoints == o.waypoints && tcpRef == o.tcpRef
            && frameRef == o.frameRef && constraint == o.constraint
            && sourceTaskPoint == o.sourceTaskPoint
            && pathLengthJoint == o.pathLengthJoint
            && pathLengthTcp == o.pathLengthTcp;
    }
    bool operator!=(const TrajectorySegment& o) const { return !(*this == o); }
};

// =====================================================================
// PTP 候选解（§7.5 构型选择规则的输入三键——域内最小值类型）
// =====================================================================

/**
 * @brief PTP 终点的单个候选构型（§7.5 选择规则输入；域内最小值类型）。
 *
 * 为什么不直接消费 kinematics 的解集类型：R-1 红线（业务域单元互链禁止
 * ——卡 §3.2）禁止 trajectory include kinematics 头；本结构按 O-37 裁决
 * 同款纪律（kinematics Evidence.hpp 批量通道先例——"宿主把上游对象解析
 * 为投影值后经工厂闭包注入，单元侧只消费投影值"）只携带 §7.5 三键所需
 * 的最小字段。注入路径：WP-16-T05 落位的 KinematicsPort 适配层把
 * kin.task-point-ik 解集（四键稳定排序后的视图序）投影为本结构序列。
 *
 * 字段语义（与 kinematics KinematicSolution 同源同单位）：
 *   - q：权威关节向量（转动 rad／移动 m；链序）；
 *   - stableIndex：解集稳定排序序上的下标（计数，无量纲——§7.5 规则 3
 *     "稳定编号兜底"的键；kinematics 侧＝sorted() 视图序 solutionIndex）；
 *   - minimumJointMargin：有界关节最小裕量（无量纲归一化比，D-KIN-6；
 *     全无界关节＝+∞——参与全序取最大）。
 *
 * 合法域：q 全分量有限；stableIndex 无约束（调用方投影面保证非负）；
 * minimumJointMargin 为有限非负或 +∞（NaN＝投影面数据违约——消费点
 * fail-fast）。值语义纯结构；线程安全。
 */
struct PtpCandidate {
    /// 权威关节向量（rad|m）。
    rw::math::Q q;
    /// 解集稳定排序下标（无量纲计数——§7.5 规则 3 键）。
    std::uint32_t stableIndex = 0;
    /// 有界关节最小裕量（无量纲归一化比；+∞＝全无界）。
    double minimumJointMargin = 0.0;

    bool operator==(const PtpCandidate& o) const
    {
        return q == o.q && stableIndex == o.stableIndex
            && minimumJointMargin == o.minimumJointMargin;
    }
    bool operator!=(const PtpCandidate& o) const { return !(*this == o); }
};

// =====================================================================
// 评估身份组（acceptance 2——snapshotId/sliceId/评估键六要素与 §7 一致）
// =====================================================================

/**
 * @brief 轨迹域运行身份组（§5.6 运行身份要素的值承载——一次轨迹评估
 *        的全部身份绑定，评估器/展开/PTP 各产出口径一致引用）。
 *
 * §5.6 原文口径："运行身份要素＝sliceId（已含：对象 cv、policy 内容身份、
 * nameMap 身份、config.trj、评估键＋契约版本、模式、环境 token）＋任务
 * 五元组＋seed/线程"。本结构承载其中的**域内值**面：
 *   - snapshotId：绑定快照（CON-01——一次评估一份快照，防混入多修订）；
 *   - sliceId：绑定切片（CON-05——缓存键/当前性判据；已含 config.trj
 *     摘要与评估键＋契约版本等七要素，见 evidence 卡 §4.2.3）；
 *   - policyContentIdentity：已解析策略身份（CON-06——快照 policyRef
 *     透传，本域不解析策略内容）；
 *   - evaluationKey＋contractVersion：评估键与契约版本（进 sliceId 的
 *     词表要素——kTrjEvaluationKey/kTrjEvaluatorContractVersion）；
 *   - planConfigDigest：config.trj 摘要（§5.5——canonical 编码后
 *     SHA-256；进 sliceId、不进 inputBaselineId，D-04 同款）；
 *   - planningSeed：规划种子（确定性复现要素——NFR-COR-02；0 非法，
 *     I-KIN-4 同款拒绝）；
 *   - algorithmVersion：轨迹算法版本 token（kTrjAlgorithmVersion——算法
 *     语义变更→新身份→旧结果失效）。
 *
 * 任务五元组（TaskIdentity）与模式/环境 token 由 evaluation 请求面携带
 * （core::Evaluation.hpp / evidence 卡），不在本组重复承载——身份语义
 * 单点，避免双权威。全部字段构造期一次冻结（按不可变值对待）。
 * 值语义纯结构；线程安全（并发只读）。
 */
struct TrjSequenceIdentity {
    /// 绑定快照内容身份（CON-01；全零＝未绑定——保留值，仅中间态合法）。
    core::ContentIdentity snapshotId;
    /// 绑定切片内容身份（CON-05——缓存键/当前性判据）。
    core::ContentIdentity sliceId;
    /// 已解析策略内容身份（CON-06 透传）。
    core::ContentIdentity policyContentIdentity;
    /// 评估键（恒 kTrjEvaluationKey——本域唯一评估键）。
    std::string evaluationKey{kTrjEvaluationKey};
    /// 评估器契约版本（恒 kTrjEvaluatorContractVersion）。
    std::uint32_t contractVersion = kTrjEvaluatorContractVersion;
    /// config.trj 摘要（§5.5；PlanConfig.hpp trajectoryPlanConfigurationDigest）。
    core::ContentIdentity planConfigDigest;
    /// 规划种子（无纲量；>0——0 非法，I-KIN-4 同款）。
    std::uint64_t planningSeed = 0;
    /// 轨迹算法版本 token（恒 kTrjAlgorithmVersion——构造期快照）。
    std::string algorithmVersion{kTrjAlgorithmVersion};

    bool operator==(const TrjSequenceIdentity& o) const
    {
        return snapshotId == o.snapshotId && sliceId == o.sliceId
            && policyContentIdentity == o.policyContentIdentity
            && evaluationKey == o.evaluationKey
            && contractVersion == o.contractVersion
            && planConfigDigest == o.planConfigDigest
            && planningSeed == o.planningSeed
            && algorithmVersion == o.algorithmVersion;
    }
    bool operator!=(const TrjSequenceIdentity& o) const { return !(*this == o); }
};

// =====================================================================
// 失败段素材记录（§6.2 FailedSegmentRecord 行＋§6.7——TRJ-06 定位载体）
// =====================================================================

/**
 * @brief 失败段与局部诊断素材（§6.2 原文字段；TRJ-06"对无路径、分支
 *        跳变、奇异邻域和限制超标给出具体段落与原因"的域内承载）。
 *
 * 语义归类（AGENTS 错误语义／卡 §15.0）：本结构是**素材**（环境/用户
 * 数据失败的返回值面）——不是异常，也不直接构造 DiagnosticRecord；
 * 诊断实例的产出（经 IDiagnosticFactory、评估路径 snapshotId/sliceId
 * 必填）归评估器组装面（WP-16-T09/T10）。
 *
 * 字段义务（§6.2 原文）：失败段序号（全轨迹级失败＝0xFFFFFFFF＋phase
 * 说明）；reasonToken 恒取 DiagCodes.hpp 的 TRJ-* 码常量（禁字符串拼码
 * ——码值唯一书写点纪律）；比较型字段（超限类必填：实际值/期望值/单位
 * ——UX-03 三要素经 core::ComparativeFields）；cause/recommendedAction
 * 为中文（ERR-01 字段）。值语义纯结构；线程安全。
 */
struct FailedSegmentRecord {
    /// 失败段序号（0 基；全轨迹级失败＝0xFFFFFFFF 并在 phaseToken 说明）。
    std::uint32_t segmentIndex = 0xFFFFFFFFu;
    /// 失败阶段 token（本批产生面＝kPhasePlanPtp"plan-ptp"；§14.1.6 词表）。
    std::string phaseToken{kPhasePlanPtp};
    /// TRJ-* 稳定码建议（DiagCodes.hpp 常量——kTrjInputInvalid/kTrjNoPath/
    /// kTrjLimitExceeded 等；素材归属——evidence/aggregate 侧定级）。
    std::string reasonToken;
    /// 段内定位参数 s∈[0,1]（可定位到点时必填——如越限端点 s=0/1）。
    std::optional<double> pathParameter;
    /// 绑定对象（路点/段来源任务点/碰撞对象对之一；可空）。
    std::optional<core::ObjectId> subject;
    /// 中文原因（ERR-01 字段；必填——经 make 组装校验非空）。
    std::string cause;
    /// 中文建议动作（ERR-01 字段；必填）。
    std::string recommendedAction;
    /// 比较型字段（超限类素材必填：实际值/期望值/单位——UX-03）。
    std::optional<core::ComparativeFields> comparison;

    bool operator==(const FailedSegmentRecord& o) const
    {
        return segmentIndex == o.segmentIndex && phaseToken == o.phaseToken
            && reasonToken == o.reasonToken && pathParameter == o.pathParameter
            && subject == o.subject && cause == o.cause
            && recommendedAction == o.recommendedAction
            && comparison == o.comparison;
    }
    bool operator!=(const FailedSegmentRecord& o) const { return !(*this == o); }
};

// =====================================================================
// 段构造守卫（§9.1"段构造保证；检查作为守卫"同款精神——V-01 依托）
// =====================================================================

/**
 * @brief 校验轨迹段结构不变量（§6.2 不变量全表的执行面；非法即抛——
 *        fail-fast，段构造是产品代码内部契约而非用户数据评估）。
 *
 * 校验序固定（确定性——同一坏段必报同一首错，NFR-COR-02）：
 *   1. waypoints 数量 ≥2（§6.2"含段端点；至少 2 个"）；
 *   2. JointLinear 段全部路点携带 q 且不携带 target；CartesianLine 段
 *      全部路点携带 target 且不携带 q（双域不得同时有效/缺失）；
 *   3. 每个路点 Waypoint.segmentIndex==本段 segmentIndex；
 *   4. kind==TaskPoint 的路点 sourceTaskPoint 必填；
 *   5. kind==Dwell 的路点 dwellDurationS 必填且 >0（s）。
 *
 * @throws TrajectoryError token "trajectory/segment/..."，文案含首错
 *         路点下标与实际值（不钳制不静默——NFR-COR-03）
 *
 * 纯函数；线程安全；确定性。
 */
void validateTrajectorySegment(const TrajectorySegment& segment);

}  // namespace sdurws::ird::trajectory

#endif  // IRD_TRAJECTORY_TRJTYPES_HPP
