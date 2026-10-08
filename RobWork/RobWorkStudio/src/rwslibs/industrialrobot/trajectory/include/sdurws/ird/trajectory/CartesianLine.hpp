/**
 * @file   CartesianLine.hpp
 * @brief  笛卡尔直线段规划（§8）——接近/撤离段几何构造（§8.2 轴向解析＋
 *         位置线性＋姿态最短弧 slerp）、直线采样（§8.3 cartesianSampleStep
 *         等步长、端点必含）、沿途 IK 连续性检查（TRJ-02 核心——经
 *         IKinematicsComputePort ③端口消费，本域零 IK 实现）与奇异邻域
 *         warning 素材（§8.5；WP-16-T05 批）。
 *
 * 设计依据：
 *   - units/trajectory.md §8.1（R1 范围——仅笛卡尔直线接近/撤离段；圆弧
 *     等其他几何不在承诺面）、§8.2（几何构造——轴向解析/起点生成/姿态
 *     插值口径）、§8.3（直线采样与 IK 端口消费——采样计划/多初值硬过滤
 *     去重稳定排序透传/分支连续性链式判定/证据素材字段）、§8.4（采样
 *     失败语义——初值策略扩充→段失败定位→搜索未果口径，不判不可行）、
 *     §8.5（奇异邻域——阈值唯一来源 policy conditionNumberWarning，
 *     nullopt＝检查显式不适用；命中产 warning 素材不阻断）、§8.6（碰撞
 *     归 §11 复检协议——本批不消费 policy）
 *   - §15.2（PlanCartesianLine 接口基线——请求/输出/前置/错误/合法与
 *     非法示例；"实现任务允许按 DTB §5.4 微调并登记偏差"）、§15.0（错误
 *     二分/取消/确定性/线程/零副作用）、§15.4（③端口消费纪律）
 *   - §7.5（首采样点分支取 PTP 构型选择规则延续——choosePtpCandidate
 *     复用）、§13.3（trj.cartesian-ik-continuity 证据项——适用条件＝路径
 *     含笛卡尔段）、§14.4（TRJ-BRANCH-JUMP/TRJ-SINGULAR-NEIGHBORHOOD/
 *     TRJ-NO-PATH 素材口径）
 *   - 需求 TRJ-02（"支持笛卡尔直线接近/撤离段，并对沿途 IK 连续性进行
 *     检查"）、TRJ-06（分支跳变/奇异邻域给出具体段落与原因）、REQ-02
 *     （approach 沿轴负向趋近/retract 沿轴正向离开——方向语义归
 *     requirements，本域只做几何解释）、NFR-COR-01/02（解析对照与确定性）
 *   - 任务契约 tasks/foundation/WP-16-T05.json（acceptance 1/2——直线
 *     接近/撤离段＋沿途 IK 连续性模型测试；端口消费不直链 kinematics）
 *
 * 背景说明（几何与失败语义的域内定位）：
 *   - 本头只产出**几何**（段＋逐采样连续性记录）；限速/时间归 §12
 *     （WP-16-T08 唯一判定点），碰撞归 §11 复检协议（WP-16-T07，policy
 *     会话消费面）——本批零 policy 消费，段约束仅随段携带（零副本，ARC-05）。
 *   - 失败语义（§8.3/§8.4）：分支断裂→BranchJump 终态＋TRJ-BRANCH-JUMP
 *     素材（附采样点 s 与两端解）；采样点无可用解→先初值策略扩充（端口
 *     重试语义由适配层承担，本域以更大初值数重试一次）→仍失败→
 *     SampleUnreachable 终态＋TRJ-NO-PATH 素材（附 s/已试初值数/过滤原
 *     因分布）——**搜索未果口径，不判不可行**（REQUIREMENTS §8.1 C5/C8；
 *     判定归 evidence aggregateVerdict，本域不越权——N7）。
 *   - 确定性：采样计划由 (distanceM, cartesianSampleStep) 解析决定；解序
 *     由端口稳定排序透传；分支跟踪的比较谓词全序（逐轴偏差和最小→
 *     stableIndex 升序兜底）——同输入恒同输出（NFR-COR-02）。
 *
 * 线程安全：全部纯函数/值类型，无共享可变状态（§15.0）。端口指针由调用
 * 方持有，本域不接管生命周期。
 */

#ifndef IRD_TRAJECTORY_CARTESIANLINE_HPP
#define IRD_TRAJECTORY_CARTESIANLINE_HPP

#include <cstdint>
#include <optional>
#include <vector>

#include <rw/math/Q.hpp>
#include <rw/math/Transform3D.hpp>
#include <rw/math/Vector3D.hpp>

#include <sdurws/ird/core/DiagData.hpp>      // core::ComparativeFields（奇异 warning 比较型三要素）
#include <sdurws/ird/core/Identity.hpp>      // core::ObjectId（tcpRef/frameRef/sourceTaskPoint）
#include <sdurws/ird/trajectory/KinematicsPort.hpp>  // IKinematicsComputePort（③端口——R-1 合规消费）
#include <sdurws/ird/trajectory/Ptp.hpp>     // choosePtpCandidate（§7.5 首采样分支延续）
#include <sdurws/ird/trajectory/Sequence.hpp>  // SequenceSegmentAxis（ToolZ/ReferenceZ 词表镜像复用）
#include <sdurws/ird/trajectory/TrjTypes.hpp>

namespace sdurws::ird::trajectory {

// =====================================================================
// 笛卡尔直线插值（§8.2 姿态插值口径的唯一实现点——段几何/黄金算例共用）
// =====================================================================

/**
 * @brief 笛卡尔直线插值（§8.2——位置线性＋姿态最短弧 slerp）。
 *
 * 插值语义（黄金算例锁定口径——NFR-COR-01 解析对照）：
 *   - 位置：p(s) = (1-s)·p_a + s·p_b（逐分量凸组合；s=0/1 浮点下精确
 *     还原端点——V-04 断言依托）；
 *   - 姿态：四元数最短路径 slerp——把两端旋转各化为单位四元数 q_a/q_b，
 *     若点积 q_a·q_b < 0 则翻转 q_b（同旋转的等价四元数 ±q 成对，取
 *     点积非负一支保证走最短弧）；θ=acos(clamp(q_a·q_b,-1,1))；θ 小于
 *     极小角阈值时回退线性插值后归一化（避免 1/sinθ 数值发散）；否则
 *     q(s) = (sin((1-s)θ)·q_a + sin(sθ)·q_b)/sinθ。
 *   - 接近/撤离段两端姿态在 §8.2 起点生成下天然相同（纯平移生成），
 *     slerp 退化为恒等——但插值器按通用直线段实现（V-05 绕行姿态样例
 *     以显式四元数对照锁定 slerp 口径）。
 *
 * @param ta [in] 直线起点位姿（基座系 {B} TCP；平移 m）
 * @param tb [in] 直线终点位姿（基座系 {B} TCP；ta姿态/tb姿态均可逆——
 *            奇异旋转矩阵属调用方数据违约，构造点 fail-fast）
 * @param s  [in] 行程参数 ∈[0,1]（0＝ta，1＝tb；弧长比例——位置沿直线
 *            匀比、姿态沿最短弧匀角速；越界属调用方违约）
 *
 * @return 插值位姿（基座系 {B}）
 *
 * @throws TrajectoryError token "trajectory/cartesian/interpolate-"：
 *         s 越出 [0,1]（调用方契约违约——fail-fast）
 *
 * 纯函数；线程安全；确定性（无归约顺序歧义——NFR-COR-02）。
 */
rw::math::Transform3D<double> interpolateCartesianLine(
    const rw::math::Transform3D<double>& ta,
    const rw::math::Transform3D<double>& tb,
    double s);

// =====================================================================
// 采样计划（§8.3"按 cartesianSampleStep 等步长采样（端点必含）"）
// =====================================================================

/**
 * @brief 计算笛卡尔直线采样计划（s 参数序列——确定性唯一实现点）。
 *
 * 口径（§8.3）：段长 L（m）按步长上限 step（m）等分——分段数
 * n = max(1, ceil(L/step))，样本 s_i = i/n（i=0..n，共 n+1 个，端点 0/1
 * 必含）；实际相邻样本弧长 L/n ≤ step 恒成立。采样计划参数（步长/样本
 * 数）进身份（§5.6/§8.3——config.trj.cartesianSampleStep 已在
 * planConfigDigest 内）。
 *
 * @param lengthM [in] 段长（m；>0 有限——0 长笛卡尔段无 IK 语义，非法）
 * @param stepM   [in] 采样步长上限（m；>0 有限——config.trj 投影）
 *
 * @return 样本 s 参数升序序列（首 0 末 1；相邻间隔恒等——等步长）
 *
 * @throws TrajectoryError token "trajectory/cartesian/sample-plan-"：
 *         lengthM/stepM 非有限或 ≤0（调用方契约违约——fail-fast）
 *
 * 纯函数；线程安全；确定性。
 */
std::vector<double> cartesianSamplePlan(double lengthM, double stepM);

// =====================================================================
// 连续性记录与奇异邻域素材（§8.3 证据绑定／§8.5 warning——素材轨值类型）
// =====================================================================

/**
 * @brief 单个采样点的 IK 连续性记录（§8.3 证据绑定字段——"逐采样解索
 *        引、逐轴偏差、判定"；trj.cartesian-ik-continuity 证据项素材的
 *        逐样本行）。
 *
 * 值语义纯结构；线程安全（并发只读）。
 */
struct IkContinuitySampleRecord {
    /// 采样参数 s∈[0,1]（cartesianSamplePlan 序——段内定位键）。
    double s = 0.0;
    /// 该采样点通过硬过滤的解集大小（≥0；解集空时连续性行不存在——
    /// 段以 SampleUnreachable 终止，故本记录恒在非空解集下产生）。
    std::uint32_t solutionCount = 0;
    /// 当前分支解在解集内的下标（解集视图序；§8.3"跟踪当前分支"）。
    std::uint32_t branchSolutionIndex = 0;
    /// 与前一采样点分支解的逐轴最大偏差（转动 rad／移动 m 逐轴混合计量
    /// ——与 pathLengthJoint 同口径；首样本恒 0）。
    double maxAxisDelta = 0.0;
    /// 分支连续判定（首样本恒 true——分支起点；其余＝逐轴 |Δq| 全部
    /// ≤ ikContinuityThreshold）。
    bool branchContinuous = true;
};

/**
 * @brief 全段 IK 连续性记录（§8.3——逐采样记录＋判定参数快照；Ok/
 *        BranchJump 终态均携带（跳变前样本记录完整，可回放定位））。
 *
 * 消费面：trj.cartesian-ik-continuity 证据项素材装配（评估器面——
 * §13.4 EvidenceItem）；本域只产素材不判证据等级（§13.2——质量/判定
 * 归 evidence）。值语义纯结构；线程安全。
 */
struct IkContinuityRecord {
    /// 逐采样记录（cartesianSamplePlan 序；全部 branchContinuous==true
    /// ⇔ allContinuous）。
    std::vector<IkContinuitySampleRecord> samples;
    /// 判定阈值快照（逐轴上界；rad|m——config.trj.ikContinuityThreshold，
    /// 进身份）。
    double threshold = 0.0;
    /// 实际相邻样本弧长上限（m＝L/n ≤ cartesianSampleStep——采样计划
    /// 身份要素的落地值）。
    double sampleStepActualM = 0.0;
    /// 全链连续判定（true＝逐采样分支连续记录完整且全部连续——§15.2
    /// 后置"Ok 时逐采样分支连续记录完整"）。
    bool allContinuous = false;
};

/**
 * @brief 奇异邻域 warning 素材（§8.5——TRJ-SINGULAR-NEIGHBORHOOD，
 *        warning 级不阻断段规划；TRJ-06"给出具体段落与原因"的奇异侧
 *        定位载体）。
 *
 * 素材轨（非异常非失败）：命中只记录不改变段规划结局（§8.5 原文"不
 * 阻断段规划——奇异邻域是工程警告非硬失败"）；若后续导致 IK 发散/
 * 跳变，由 §8.3/§8.4 的失败语义接管。值语义纯结构；线程安全。
 */
struct SingularNeighborhoodWarning {
    /// 采样参数 s∈[0,1]（命中点定位——§8.5"附 s"）。
    double s = 0.0;
    /// 条件数实测值（无量纲，单位 1；+∞＝奇异——非有限时
    /// conditionNumberIsFinite==false，不截断）。
    double conditionNumber = 0.0;
    /// 条件数有限性标记（false＝实测 +∞——D-KIN-2 透传）。
    bool conditionNumberIsFinite = true;
    /// 判定阈值（无量纲，单位 1；policy conditionNumberWarning 投影）。
    double threshold = 0.0;
    /// 中文原因（ERR-01 字段——含 s、条件数、阈值）。
    std::string cause;
    /// 中文建议动作（ERR-01 字段）。
    std::string recommendedAction;
    /// 比较型三要素（实际条件数/阈值/单位 1——UX-03；码表 requires-
    /// Comparison 前件的本素材侧承载）。
    std::optional<core::ComparativeFields> comparison;
};

// =====================================================================
// 段规划请求/结果（§15.2 PlanCartesianLine 的 T05 落地面）
// =====================================================================

/**
 * @brief 笛卡尔直线段角色（approach/retract 两站段封闭词表——§8.2；
 *        方向语义：approach 沿轴负向趋近、retract 沿轴正向离开——方向
 *        语义权威归 requirements §5.1，本域按此口径做几何解释）。
 */
enum class CartesianLineRole : std::uint8_t {
    /// 接近段——从任务点沿轴负向回退 distanceM 生成起点（§8.2）。
    Approach,
    /// 撤离段——从任务点沿轴正向外推 distanceM 生成终点（§8.2）。
    Retract,
};

/**
 * @brief 笛卡尔直线段规划请求（§15.2 Request 的 T05 字段面——值语义）。
 *
 * 合法域（fail-fast——调用方契约违约，§15.2 非法示例）：
 *   - workPose 各分量有限、旋转矩阵正交可逆（构造点数值核验）；
 *   - distanceM 有限且 >0（§15.2 非法示例原文"distanceM≤0（输入非法）"
 *     ——requirements 启用校验已保证，到域仍复验）；
 *   - axis==ReferenceZ 时 referenceAxisDirectionInBase 必填且为基座系
 *     单位向量（|v|≈1，偏差 ≤1×10⁻⁹——frameRef Z 轴解析产物；"axis
 *     对象未在闭包（输入非法）"的请求面落点＝该方向缺失）；axis==ToolZ
 *     时必须为空（轴向由 workPose 旋转部域内解析——防双源不一致）；
 *   - ikContinuityThreshold 有限且 >0（rad|m）；conditionNumberWarning
 *     有值时须有限且 >0（无量纲 1）；
 *   - branchSeedQ 全分量有限（构型游标——首采样分支延续参照）且维度
 *     ≥1（自由度链）；lowerBoundQ/upperBoundQ 同维度、全分量有限且逐轴
 *     lower<upper（评价区间投影——§5.3）；
 *   - planningSeed ≠0（无量纲——I-KIN-4 同款拒绝）；
 *   - kinPort 非空（§15.4 前置"端口已在装配期注入（缺失→装配失败，
 *     不运行）"）；tcpRef 合法对象身份。
 *
 * 单位/坐标系：workPose 为基座系 {B} 的 TCP 目标位姿（T_base_tcp；
 * 平移 m）；distanceM 单位 m；thresholds 见上；branchSeedQ 权威角
 * （rad|m）。值语义纯结构。
 */
struct CartesianLineRequest {
    /// 任务点 TCP 目标位姿——基座系 {B}（approach 的段终点＝retract 的
    /// 段起点；§8.2 输入 T_end 语义）。
    rw::math::Transform3D<double> workPose;
    /// 进退轴词表（SequenceSegmentAxis——ToolZ/ReferenceZ；ToolZ 由域内
    /// 经 workPose 旋转部解析，ReferenceZ 由调用方注入解析产物——§8.2）。
    SequenceSegmentAxis axis = SequenceSegmentAxis::ToolZ;
    /// ReferenceZ 轴向解析产物——frameRef Z 轴在基座系的单位向量（仅
    /// ReferenceZ 有语义且必填；ToolZ 时必须为空——§8.2 轴向解析的
    /// 单源纪律）。
    std::optional<rw::math::Vector3D<double>> referenceAxisDirectionInBase;
    /// 段长（m；>0 有限——TaskSegment.distanceM 投影）。
    double distanceM = 0.0;
    /// 段角色（approach/retract——CartesianLineRole 注）。
    CartesianLineRole role = CartesianLineRole::Approach;

    /// TCP/工具对象引用（ARC-04 对象 ID；必填合法——§6.3）。
    core::ObjectId tcpRef;
    /// 参考系对象（World 时 nullopt——不伪造默认，§6.3；ReferenceZ 的
    /// 解析对象身份随段携带供追溯）。
    std::optional<core::ObjectId> frameRef;
    /// 来源任务点（approach/retract 归属站——§6.2；任务点端路点的
    /// sourceTaskPoint 取本值，可追溯性 NFR-COR-04）。
    std::optional<core::ObjectId> sourceTaskPoint;
    /// 段级约束（限值引用＋缩放系数＋采样步长——SegmentConstraint 零
    /// 副本纪律；cartesianSampleStep 即采样计划步长上限来源）。
    SegmentConstraint constraint;
    /// 段序号（0 基；写入段与路点的定位键）。
    std::uint32_t segmentIndex = 0;

    /// 相邻采样点分支连续判定阈值（逐轴上界；rad|m——config.trj.
    /// ikContinuityThreshold 投影；§8.3/§9.1 同一来源）。
    double ikContinuityThreshold = 1e-6;
    /// 奇异邻域条件数阈值（无量纲 1；policy conditionNumberWarning 只读
    /// 投影——§8.5 阈值唯一来源；nullopt＝policy 未启用→检查显式不适用，
    /// 不计缺失，P-POL-2 语义承接）。
    std::optional<double> conditionNumberWarning;

    /// 分支延续种子构型（rad|m）——首采样点分支按 PTP 构型选择规则延续
    /// 的参照点（§8.3"首采样点分支取 PTP 构型选择规则延续"；语义＝展开
    /// 游标：最近一个已解析构型——approach 段为该站前 PTP 的起点游标、
    /// retract 段为任务点构型；禁止取会话姿态，§5.3）。
    rw::math::Q branchSeedQ;

    /// 评价区间下界（逐轴；rad|m——CanonicalJoint.bounds 投影，经 IK 端
    /// 口请求透传供限位硬过滤②；与 branchSeedQ 同维度、逐轴 lower<upper）。
    rw::math::Q lowerBoundQ;
    /// 评价区间上界（逐轴；rad|m——与 lowerBoundQ 同维度）。
    rw::math::Q upperBoundQ;

    /// 规划种子（无量纲；≠0——0 非法，I-KIN-4 同款拒绝；透传 IK 端口
    /// 请求的确定性种子——NFR-COR-02 复现要素；域运行种子
    /// TrjSequenceIdentity.planningSeed 的段级投影）。
    std::uint64_t planningSeed = 1;

    /// IK/FK 注入端口（③端口——非空；调用方持有，本域不接管所有权）。
    IKinematicsComputePort* kinPort = nullptr;

    /// 取消观测（可空＝不可取消；入口＋逐采样循环边界轮询——§15.1）。
    CancelSignal cancel;
};

/**
 * @brief 笛卡尔直线段规划结局（§15.2 status 词表的 T05 落地面）。
 *
 * 词表与 §15.2 输出行的映射（诚实登记）：Ok／BranchJump／SampleUnreach-
 * able／Canceled 四终态；"SingularNeighborhood(s)"为伴随 warning 素材
 * （不阻断——singularWarnings 字段非空即呈现，非独立终态）；"Data-
 * InsufficientMaterial"不设终态——SampleUnreachable 的素材即搜索未果
 * 口径素材（附 s/已试初值数/过滤原因分布），DataInsufficient 判定归
 * evidence aggregateVerdict（§8.1 表 2 ④；本域不越权——N7）。
 */
enum class CartesianLineStatus : std::uint8_t {
    /// 规划成功——段几何确定＋逐采样分支连续记录完整（§15.2 后置；
    /// singularWarnings 可能非空——warning 不改变结局）。
    Ok,
    /// 分支跳变——段失败定位（TRJ-BRANCH-JUMP 素材，附采样点 s 与两端
    /// 解摘要；跳变前样本的连续性记录完整回带）。
    BranchJump,
    /// 采样点不可达——端点或中途采样点解集空（初值扩充后仍失败）；
    /// TRJ-NO-PATH 素材（搜索未果口径——不判不可行，§8.4）。
    SampleUnreachable,
    /// 取消（取消观测命中——非错误，零素材零诊断；UX-03）。
    Canceled,
};

/**
 * @brief 笛卡尔直线段规划结果（§15.2 SegmentPlanResult 的 T05 字段面）。
 *
 * status==Ok 时 segment 有效且过 validateTrajectorySegment、continuity
 * .allContinuous==true；status==BranchJump/SampleUnreachable 时 failure
 * 必填（可定位素材——pathParameter=s）且 segment 无值；status==
 * Canceled 时 failure 必为空（取消不是错误——UX-03）。
 *
 * 诊断边界：本结构承载域内素材（FailedSegmentRecord/SingularNeighborhood-
 * Warning）；DiagnosticRecord 实例（TRJ-* 码经 IDiagnosticFactory、评估
 * 路径 snapshotId/sliceId 必填）归评估器组装面（WP-16-T09/T10）——§14.4。
 */
struct CartesianLinePlanResult {
    /// 规划结局（CartesianLineStatus——四态）。
    CartesianLineStatus status = CartesianLineStatus::Canceled;
    /// 规划产出的段（Ok 时有效且过结构校验；其余状态无段）。
    TrajectorySegment segment;
    /// 失败定位素材（BranchJump/SampleUnreachable 时必填；Canceled 恒空）。
    std::optional<FailedSegmentRecord> failure;
    /// 全段 IK 连续性记录（Ok/BranchJump 时有效——§8.3 证据绑定素材；
    /// SampleUnreachable/Canceled 时无完整记录）。
    std::optional<IkContinuityRecord> continuity;
    /// 奇异邻域 warning 素材（§8.5——可能非空且不改变结局；逐采样命中
    /// 逐条记录，采样序）。
    std::vector<SingularNeighborhoodWarning> singularWarnings;
};

// =====================================================================
// 段规划入口（§8 几何构造→采样→IK 端口消费→连续性/奇异判定）
// =====================================================================

/**
 * @brief 规划一个笛卡尔直线接近/撤离段并执行沿途 IK 连续性检查
 *        （§8/§15.2——TRJ-02 核心）。
 *
 * 执行序（每步语义见行内注释；任何一步失败即按 §8.3/§8.4 归类返回）：
 *   1. 前置校验（调用方契约违约 → TrajectoryError fail-fast——§15.2
 *      非法示例两例：distanceM≤0、axis 解析产物缺失）；
 *   2. 取消轮询（命中 → Canceled，零素材）；
 *   3. 几何构造（§8.2：轴向解析→起点/终点生成——approach 回退/retract
 *      外推）；
 *   4. 采样计划（§8.3：cartesianSamplePlan——端点必含）；
 *   5. 逐采样 IK 端口消费（§8.3：solveIk 透传——解集空→初值策略扩充
 *      重试一次→仍空→SampleUnreachable 终态，素材附 s/已试初值数/
 *      过滤原因分布，不判不可行）；
 *   6. 分支跟踪（§8.3：首采样 choosePtpCandidate 延续（branchSeedQ），
 *      其后逐采样在解集中找与当前分支逐轴 |Δq|≤threshold 的延续解——
 *      找到→更新当前分支（偏差和最小→stableIndex 升序确定性消解）；
 *      找不到→BranchJump 终态＋TRJ-BRANCH-JUMP 素材（附 s 与两端解））；
 *   7. 奇异邻域判定（§8.5：conditionNumberWarning 有值时逐采样经
 *      evaluateFk 取条件数→命中产 warning 素材不阻断；nullopt→检查
 *      NotApplicable 不调用 FK——§8.3 FK 消费以奇异检查为唯一目的）；
 *   8. 几何产物组装（CartesianLine 段：端点路点 kind 按角色——任务点
 *      端 TaskPoint 带 sourceTaskPoint、生成端 Via；pathLengthTcp=
 *      distanceM；pathLengthJoint 不填——笛卡尔段无关节路径长度语义，
 *      恒 0 且语义"未计算"，不冒充）；
 *   9. 结构守卫（validateTrajectorySegment——段构造保证的自证）。
 *
 * 限速/时间不在本函数判定（§12 唯一判定点——WP-16-T08）；碰撞不在本
 * 函数判定（§8.6——§11 复检协议，WP-16-T07）。
 *
 * @param request [in] 规划请求（合法域见 CartesianLineRequest 注）
 * @return 规划结果（CartesianLinePlanResult 四态；值语义）
 *
 * @throws TrajectoryError 调用方契约违约（token "trajectory/cartesian/
 *         ..."）；端口层 PortError 以 "trajectory/kin-port-*" token 透传
 *         抛出（环境故障显性失败——不吞错，§15.0/§15.4）
 *
 * 纯函数；无副作用/零修订/零写盘（§15.0）；线程安全；确定性（同输入同
 * 采样计划＋端口稳定解序＋全序分支比较——NFR-COR-02）。
 */
CartesianLinePlanResult planCartesianLine(const CartesianLineRequest& request);

}  // namespace sdurws::ird::trajectory

#endif  // IRD_TRAJECTORY_CARTESIANLINE_HPP
