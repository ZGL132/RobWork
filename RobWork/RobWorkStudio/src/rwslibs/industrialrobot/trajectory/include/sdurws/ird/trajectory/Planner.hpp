/**
 * @file   Planner.hpp
 * @brief  RobWork 规划器适配面（§15.5 IPathPlannerAdapter）与避障重规划
 *         编排（§10.4，TRJ-03"调用 RobWork 规划器完成避障路径搜索"承载
 *         面；WP-16-T06 批）。
 *
 * 设计依据：
 *   - units/trajectory.md §10.1（归属与装配边界——"trajectory 计算库
 *     PRIVATE 链接 sdurw_pathplanners 消费规划算法；WorkCell/Device/State
 *     实例只能来自 runtime RuntimeSnapshot 只读视图；碰撞判定只能经 policy
 *     会话；名称经⑥端口"）、§10.2（规划器选型与参数——PlannerSelection
 *     family 词表"当前登记拟实现值，落位时在黄金算例中锁定"、参数表
 *     "显式键值"）、§10.3（与 policy 的交接——QConstraint 适配器"纯适配
 *     零策略逻辑"、SingleState/PathSequence 两查询、finalized=false 不得
 *     采信、阈值零参数化、取消/预算传递）、§10.4（避障重规划流程图——
 *     直连候选碰撞→避障搜索→候选逐条 PathSequence 复核→碰撞淘汰并重规划
 *     →不判任务不可行→稳定选择序采纳）
 *   - §15.5（IPathPlannerAdapter 接口基线——形态"适配面（trajectory 拥
 *     有）；实现 PRIVATE 消费 sdurw_pathplanners；WorkCell/State 一律来自
 *     注入的 WorkCellConstView/makeState"；签名 search(PlannerSearchRequest)；
 *     输出"候选路径列表（各自 policy 复核状态）"；确定性"种子化规划器＋
 *     稳定候选选择序"；错误"PlannerError→TRJ 域素材＋RT-ROBWORK-ERROR
 *     转译登记（runtime translateRobWorkError 同款不吞异常）"；非法示例
 *     "直接构造 rw::proximity 检测器注入规划器（R-5 违规）"）
 *   - §15.0（通用约定——错误二分/取消/确定性/线程/副作用）、§7.6（PTP
 *     失败语义表——"端点无碰撞但直连路径采样碰撞→触发避障搜索或淘汰该
 *     直连候选；全部候选路径碰撞/规划器搜索未果→SearchExhaustedRecord→
 *     DataInsufficient 素材，不判不可行"）
 *   - 需求 TRJ-03（P0：调用 RobWork 规划器完成避障路径搜索，规划器与参数
 *     可配置）、REQUIREMENTS §8.1 C5/C8 v1.16 修订（碰撞判定的固有作用域
 *     ＝构型/路径级——某条候选路径碰撞＝该路径被淘汰并触发重规划，均不
 *     直接上升为任务不可行；"初始候选路径碰撞、重规划成功"是 AT-06 明文
 *     反例）、NFR-COR-02（确定性）、R-5（零 sdurw_proximity——碰撞唯一
 *     经 policy；sdurw_pathplanners 为 DTB §4.6 登记给本单元的 L1 基线库）
 *   - 先例：kinematics/Collision.hpp（WP-15-T07 真实④端口半区——
 *     PolicyCollisionSessionAdapter 每查询直穿会话/失败三态映射/零本地判
 *     定副本形态）、policy/CollisionEvaluator.hpp（会话构建期半区与
 *     evaluate 冻结签名）
 *   - 任务契约 tasks/foundation/WP-16-T06.json（acceptance 1——规划器调
 *     用接入基准；acceptance 2——候选路径碰撞＝淘汰并触发重规划、不判任
 *     务不可行；P-TRJ-2 裁决前按卡内登记口径实现）
 *
 * 背景说明（本头在轨迹规划链路中的位置——第一读者须知）：
 *   序列展开（Sequence.hpp）与 PTP 几何（Ptp.hpp）产出的是"构型对"——
 *   两端构型之间的关节空间直连路径可能穿过障碍。TRJ-03 要求的避障能力分
 *   两层（§10.4 流程图）：
 *     ① IPathPlannerAdapter：RobWork 规划器（sdurw_pathplanners）的适配
 *       面——把"起终点构型＋碰撞约束（经 policy 会话 SingleState 适配）"
 *       交给真实规划算法，收回候选路径并逐条做 policy PathSequence 复核；
 *     ② planPtpWithObstacleAvoidance：§10.4 编排——先做直连候选筛查（一
 *       次 PathSequence 查询），无碰撞直接采纳（最快路径，不进搜索）；有
 *       碰撞则记录直连淘汰（C8 证据素材）并转入适配器搜索，按稳定选择序
 *       采纳首条通过候选；全部候选淘汰/预算耗尽→SearchExhaustedRecord→
 *       DataInsufficient 素材——**本域不判任务不可行**（N7：任务级判定
 *       归 evidence aggregateVerdict）。
 *
 *   P-TRJ-2 口径（§10.1/§21.2）：DTB §4.6 L1 基线表明文登记
 *   sdurw_pathplanners→trajectory（规划算法面），与 ARCH §5.1 概括措辞
 *   的张力已登记待架构裁决；裁决前本实现按卡内登记口径执行——规划器内部
 *   零 proximity 触碰（D-TRJ-3：碰撞约束经 policy 会话适配），R-5 红线
 *   不动（产品面零 sdurw_proximity，契约测试静态扫描钉住）。
 *
 * 头文件依赖纪律（冒烟模式安全——KinematicsPort.hpp 同款最小面）：
 *   本头对 rw 只消费 rw::math::Q（header-only）；对 policy/runtime 只做
 *   前向声明（会话与视图以指针/shared_ptr 形态持有——shared_ptr 对不完
 *   整类型合法）。完整类型（policy::CollisionEvaluationSession、
 *   runtime::WorkCellConstView）仅在实现翻译单元（src/Planner.cpp）与
 *   集成模式测试 TU 内 include——src/Planner.cpp 为集成模式条件源（消费
 *   rw 非模板符号：QConstraint/RRTPlanner/WorkCell——与 Ptp.cpp 同因同
 *   gating，冒烟模式不编译）。
 *
 * 线程安全：全部请求/结果值为纯值聚合（并发只读）；算法对象单线程使用
 * （§15.0——每 worker 每任务一实例）。种子化说明（R-TRJ-1）：RobWork
 * 规划器的随机性经 rw::math::Math::seed（进程级全局随机源）约束，故
 * search 要求单线程调用环境（并发 search 的全局种子互扰属装配面线程
 * 约束——评估器 threadSafety=SingleThread 语义覆盖）；同种子同输入两次
 * search 产等价候选集合（测试黄金算例钉扎）。
 */

#ifndef IRD_TRAJECTORY_PLANNER_HPP
#define IRD_TRAJECTORY_PLANNER_HPP

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <rw/math/Q.hpp>

#include <sdurws/ird/core/Identity.hpp>       // core::ObjectId（碰撞对象对——NFR-COR-05）
#include <sdurws/ird/trajectory/TrjTypes.hpp>  // SegmentConstraint/FailedSegmentRecord/
                                               //   CancelSignal/TrajectorySegment（域内值面）

namespace sdurws::ird {

namespace policy {
/// 前向声明（本头仅以 shared_ptr 形态持有碰撞会话——完整类型在实现 TU
/// include policy/CollisionEvaluator.hpp；头文件依赖纪律见文件头注）。
class CollisionEvaluationSession;
}  // namespace policy

namespace runtime {
/// 前向声明（本头仅以裸指针形态借持 WorkCell 只读视图——零自建 WorkCell
/// 红线的类型承载；完整类型 runtime/Adapter.hpp 在实现 TU include）。
class WorkCellConstView;
}  // namespace runtime

namespace trajectory {

// =====================================================================
// 词表与参数键（§10.2"逐键登记"——唯一书写点；编解码/校验/测试共用）
// =====================================================================

/// 规划器 family 词表当前唯一实现值（§10.2——"以 sdurw_pathplanners 实际
/// 可用算法为准，落位时在黄金算例中锁定"：RRT-Connect（双向启发式 RRT，
/// rwlibs::pathplanners::RRTPlanner::RRTConnect）经本批黄金算例锁定为
/// 首个登记选型；扩展走本卡增量修订＋实现＋黄金算例同批（§6.6 词表纪律；
/// 不可种子化 family 不入选——§15.5/R-TRJ-1））。
inline constexpr char kTrjPlannerFamilyRrtConnect[] = "rrt-connect";

/// 规划器参数键：RRT 扩展步长（逐轴欧氏度量下的一次树扩展距离；转动
/// rad／移动 m 逐轴混合计量；>0 有限——PlannerSearchRequest 校验拒绝
/// 非法值）。§10.2"扩展参数"的落键。
inline constexpr char kTrjPlannerParamExtend[] = "extend";

/// 规划器参数键：边离散检查分辨率（QEdgeConstraint 的直线插值检查步距；
/// 同上量纲；>0 有限）。§10.2"步长"的落键；同时决定直连筛查与候选复核
/// 的采样密度（采样计划随 plannerSelection.params 进 canonical 身份——
/// §5.6"采样计划进入身份"）。
inline constexpr char kTrjPlannerParamEdgeResolution[] = "edge-resolution";

/// 规划器参数键：候选路径规划轮数 K（正整数 ≥1——§10.4"规划器产出候选
/// 路径 k=1..K"；每轮独立调用规划器，K 与逐条结果入证据）。§10.2"时间
/// 预算"之外的第二重搜索上界（时间预算先到即停）。
inline constexpr char kTrjPlannerParamCandidateAttempts[] = "candidate-attempts";

// =====================================================================
// 候选路径复核（§10.3 PathSequence 通道的域内结论——三态、无第四态）
// =====================================================================

/**
 * @brief 候选路径 policy 复核结论（§10.4"候选 k PathSequence 复核"的域内
 *        三态词表——封闭，无第四态）。
 */
enum class CandidateReviewStatus : std::uint8_t {
    /// 全部样本无碰撞且复核终态（finalized=true、Applicable）——可采纳。
    Passed,
    /// 复核检出碰撞发现——该候选淘汰并触发重规划（C8：仅淘汰该路径，
    /// 不上升为任务结论）；collisionRecords 携带对象对与 pathParameter。
    Collision,
    /// 复核不可采信（finalized=false 的 Failed/Canceled、作用域为空、
    /// 策略禁用被误调用）——按 KIN-05 口径转数据不足素材：该候选既不
    /// 采纳也不判碰撞淘汰（§10.3"finalized=false 不得采信"）。
    DataInsufficient,
};

/**
 * @brief 单条碰撞淘汰记录（§10.4"淘汰候选 k（记录：对象对、pathParameter）"
 *        的域内载体——C8 重规划触发证据素材的最小单元）。
 *
 * 字段直映 policy::CollisionFinding 的 Collision 种类发现（SafetyMargin-
 * Violation 间距不足为"非碰撞"——policy §7.4，不构成淘汰依据，不进本
 * 记录；间距语义归复检/证据面——WP-16-T07）。对象对为 policy 规范序
 * （字节字典序 A<B——NFR-COR-05）；全部值语义纯结构；线程安全。
 */
struct CandidateCollisionRecord {
    /// 规范序第一端（碰撞对象对；ObjectId——R-4 零名称直存）。
    core::ObjectId objectA;
    /// 规范序第二端。
    core::ObjectId objectB;
    /// 采样位置（复核样本下标，0 基；无量纲计数）。
    std::size_t sampleIndex = 0;
    /// 段内定位参数 s∈[0,1]（PathSequence 查询 findings 必填——TRJ-04
    /// 同款段内定位；本域采样协议保证等长携带）。
    std::optional<double> pathParameter;

    bool operator==(const CandidateCollisionRecord& o) const
    {
        return objectA == o.objectA && objectB == o.objectB
            && sampleIndex == o.sampleIndex && pathParameter == o.pathParameter;
    }
    bool operator!=(const CandidateCollisionRecord& o) const { return !(*this == o); }
};

/**
 * @brief 单条候选路径（§15.5 输出"候选路径列表（各自 policy 复核状态）"
 *        的元素——离散构型序列＋复核结论）。
 *
 * configurations 是规划器产出的离散路点序列（首点＝起点构型、末点＝终点
 * 构型——适配器保证；中间点为规划器引入的绕行路点，对应数据模型
 * WaypointKind::Via——非必经状态，C8 语义）。pathParameters 与
 * configurations 等长（§10.3 约定——PathSequence 查询必须携带；均匀
 * s=i/(n-1)，n 为样本数；n==1 时恒 {0.0}）。全部值语义纯结构；线程安全。
 */
struct PathCandidate {
    /// 规划器产出序号（0 基；轮次序——选择序最终键与证据回放的稳定锚）。
    std::uint32_t candidateIndex = 0;
    /// 离散构型序列（权威关节向量，转动 rad／移动 m；≥2 点）。
    std::vector<rw::math::Q> configurations;
    /// 逐样本路径参数（与 configurations 等长；s∈[0,1]——§10.3 约定）。
    std::vector<double> pathParameters;
    /// policy 复核结论（CandidateReviewStatus 三态）。
    CandidateReviewStatus reviewStatus = CandidateReviewStatus::DataInsufficient;
    /// 碰撞淘汰记录（仅 Collision 态非空；对象对＋sampleIndex＋
    /// pathParameter——C8 淘汰证据）。
    std::vector<CandidateCollisionRecord> collisionRecords;
};

/**
 * @brief 搜索未果记录（§7.6"SearchExhaustedRecord（预算、已试候选数、
 *        逐条过滤记录）"的域内载体——DataInsufficient 素材的数据面）。
 *
 * 语义边界（诚实登记）：本记录**不构成任务级不可行证明**（REQUIREMENTS
 * §8.1 C5/C8 v1.16——"数值搜索未找到有效解（含全部已找到的解因碰撞被
 * 过滤）不构成不可行证明"）；它随 EvaluationOutput 进证据素材，任务级
 * DataInsufficient 判定归 evidence aggregateVerdict（N7 边界）。
 * 全部值语义纯结构；线程安全。
 */
struct SearchExhaustedRecord {
    /// 实际消耗的时间预算（s；墙钟——多轮规划共享总预算的如实账目）。
    double consumedBudgetS = 0.0;
    /// 已试候选数（已完成的规划轮数——含产出路径与未产出轮次）。
    std::uint32_t attemptedCandidates = 0;
    /// 逐条过滤记录（候选碰撞淘汰明细——含直连候选？不含：直连候选的
    /// 淘汰记录由编排层 directCollision 单独承载〔§10.4 流程图第一分支〕，
    /// 本列表只收避障搜索产出的候选）。
    std::vector<CandidateCollisionRecord> candidateRecords;
};

// =====================================================================
// §15.5 规划器适配——请求/结果与注入接口
// =====================================================================

/**
 * @brief 规划器搜索结局（§15.5 status 词表四值——封闭）。
 */
enum class PlanSearchStatus : std::uint8_t {
    /// 至少一条候选通过 policy PathSequence 复核（adoptedIndex 有值——
    /// 稳定选择序首条通过者）。
    Found,
    /// 预算/轮数耗尽且无通过候选——SearchExhaustedRecord 必填（素材轨，
    /// 不判不可行）。
    BudgetExhausted,
    /// 取消观测命中（轮边界轮询）——**取消不是错误**（UX-03）：零错误
    /// 素材，已完成的候选仍随结果携带（非终态复核结论照实标注）。
    Canceled,
    /// 规划器/环境异常（rw 异常转译——§15.5"RT-ROBWORK-ERROR 转译登记，
    /// 不吞异常"）：failure 素材必填（cause 携带转译后的异常详情）。
    PlannerError,
};

/**
 * @brief 规划器搜索请求（§15.5 提议签名的字段面——全部值语义；前置违约
 *        一律 fail-fast，token "trajectory/planner/..."）。
 *
 * 前置（§15.5 形态行与 §15.0 纪律的逐项落点）：
 *   - fromQ/toQ 同维度且全分量有限、落在 [lowerBoundQ, upperBoundQ]
 *     评价区间内（§15.1 PTP 前置"起终点构型在评价区间内"的同源守卫——
 *     越限属调用方契约违约，本层不重复 IK/限位语义）；
 *   - workCell 非空（runtime RuntimeSnapshot 只读视图——§10.1"零自建"；
 *     设备经 deviceRuntimeName 自视图解析，名称为⑥端口解析产物的透传，
 *     本域零拼接/剥离——R-4）；解析未命中＝调用方违约（fail-fast）；
 *   - session 非空（policy 唯一碰撞权威——R-5；本请求无"无碰撞检查"
 *     形态：跳过碰撞检查的编排语义在 planPtpWithObstacleAvoidance 的
 *     constraint.collisionCheckApplicable 承载，适配器恒要求会话）；
 *   - plannerFamilyToken ∈ 词表（当前唯一值 kTrjPlannerFamilyRrtConnect；
 *     词表外＝TRJ-INPUT-INVALID 性质的调用方配置错误——fail-fast）；
 *   - plannerParams 键 ⊆ 白名单三键（kTrjPlannerParam*；未知键拒绝——
 *     §10.2"参数表为显式键值"的封闭性；键值可解析且在合法域）；
 *   - timeBudgetS 有限且 >0（s——§10.3"规划器时间预算＝显式配置"）；
 *   - seed >0（0 非法——I-KIN-4 同款拒绝，NFR-COR-02 种子纪律）。
 *
 * 单位/坐标系：构型均为权威关节向量（q_authoritative = q_zeroOffset +
 * q_rw 的换算在装配面/视图侧单点完成——§6.4，本层零二次换算）；无笛卡尔
 * 量。值语义纯结构；线程安全（并发只读——借用指针在调用期存活即可）。
 */
struct PlannerSearchRequest {
    /// 起点构型（权威角 rad|m）。
    rw::math::Q fromQ;
    /// 终点构型（权威角 rad|m；与 fromQ 同维度）。
    rw::math::Q toQ;
    /// 评价区间下界（逐轴 rad|m——端点守卫与选择序裕量键的判定基准；
    /// 与 fromQ 同维度且逐轴 lower<upper）。
    rw::math::Q lowerBoundQ;
    /// 评价区间上界（逐轴 rad|m）。
    rw::math::Q upperBoundQ;
    /// runtime 快照只读视图（借持——调用期存活；零自建 WorkCell 红线，
    /// §10.1；collisionCheckApplicable 语义之外的空指针恒为调用方违约）。
    const runtime::WorkCellConstView* workCell = nullptr;
    /// 设备运行时名（⑥端口解析产物透传——视图 findDevice 的查询键；
    /// 非空；本域不拼接/剥离——R-4）。
    std::string deviceRuntimeName;
    /// policy 碰撞会话（共享只读——④端口唯一碰撞权威；非空）。
    std::shared_ptr<const policy::CollisionEvaluationSession> session;
    /// 规划器选型 family token（词表见 kTrjPlannerFamilyRrtConnect 注）。
    std::string plannerFamilyToken;
    /// 规划器参数键值表（白名单三键——见 kTrjPlannerParam* 注；值均为
    /// UTF-8 串，解析与合法域校验在本层执行）。
    std::map<std::string, std::string> plannerParams;
    /// 时间预算（s；>0 有限——多轮候选共享，先到先停）。
    double timeBudgetS = 5.0;
    /// 规划种子（无量纲；>0——rw::math::Math::seed 的实参，§15.5 种子化
    /// 纪律；同种子同输入→等价候选集合，黄金算例钉扎）。
    std::uint64_t seed = 0;
    /// 取消观测（可空＝不可取消；轮边界轮询——§15.1 取消纪律）。
    CancelSignal cancel;
};

/**
 * @brief 规划器搜索结果（§15.5 输出词表——候选路径列表＋各自复核状态＋
 *        素材轨；值语义纯结构；线程安全）。
 *
 * 状态—字段的联动（适配器保证）：
 *   - Found：candidates 非空且至少一条 reviewStatus==Passed；adoptedIndex
 *     指向稳定选择序最优的通过候选（选择序定义见 makeRobWorkPathPlanner-
 *     Adapter 实现注——延续性→裕量→路径长度→候选序号，§10.4）；其余
 *     候选为 Collision/DataInsufficient（逐条结果入证据——§10.4"K 与
 *     逐条结果入证据"）；
 *   - BudgetExhausted：exhausted 必填、adoptedIndex 为空、failure 为空
 *     （搜索未果不是错误——素材轨）；
 *   - Canceled：failure 恒空（取消不是错误——UX-03）、exhausted 可携带
 *     已消耗账目、candidates 为已完成部分；
 *   - PlannerError：failure 必填（reasonToken 取 kTrjNoPath——§14.4 行 2
 *     "§10 规划失败的段级定位素材"；cause 含 rw 异常转译详情）。
 */
struct PlanSearchResult {
    /// 搜索结局（PlanSearchStatus 四态）。
    PlanSearchStatus status = PlanSearchStatus::BudgetExhausted;
    /// 候选路径列表（规划产出序——candidateIndex 升序；逐条复核结论
    /// 如实标注，含被淘汰与不可采信者——§10.4 证据义务）。
    std::vector<PathCandidate> candidates;
    /// 被采纳候选的下标（candidates 向量序；仅 Found 有值——稳定选择序
    /// 首条通过者，选择序定义见 makePathPlannerAdapter 实现注）。
    std::optional<std::size_t> adoptedIndex;
    /// 搜索未果记录（BudgetExhausted 时必填；Found/Canceled 可为空）。
    std::optional<SearchExhaustedRecord> exhausted;
    /// 失败定位素材（PlannerError 时必填；Canceled 恒空——UX-03）。
    std::optional<FailedSegmentRecord> failure;
};

/**
 * @brief RobWork 规划器适配接口（§15.5 IPathPlannerAdapter——trajectory
 *        拥有的适配面）。
 *
 * 契约要点（§15.5 逐行）：
 *   - 实现消费 sdurw_pathplanners（P-TRJ-2 口径），规划器内部零 proximity
 *     触碰——碰撞约束经 policy 会话 SingleState 适配（D-TRJ-3）；
 *   - WorkCell/Device/State 一律来自注入的只读视图（零自建——§10.1）；
 *   - 确定性：种子化规划器＋稳定候选选择序（同种子等价候选集合——R-TRJ-1
 *     逐 family 验证；不可种子化 family 不入选）；
 *   - 错误：实现不得让 rw 异常逃逸（转译为 PlannerError＋素材——不吞、
 *     不崩、不伪造）；调用方前置违约以 TrajectoryError fail-fast；
 *   - 无跨调用可变状态（请求/结果值语义；每次 search 独立构造规划器——
 *     种子化时点在构造前，确定性保证的结构基础）。
 *
 * 线程语义：单线程使用（§15.0；全局随机源的种子化约束——见文件头注）。
 */
class IPathPlannerAdapter {
public:
    virtual ~IPathPlannerAdapter() = default;

    /**
     * @brief 执行避障路径搜索（§15.5 签名——K 轮候选＋逐条 PathSequence
     *        复核＋稳定选择序采纳）。
     *
     * @param request [in] 搜索请求（PlannerSearchRequest 前置见其注；
     *                违约抛 TrajectoryError——fail-fast）
     * @return 搜索结果（PlanSearchResult 四态；候选逐条复核结论如实）
     *
     * 确定性：同 (请求值, 种子) → 等价候选集合（NFR-COR-02；黄金算例
     * 锁定）。无副作用/零修订/零写盘（§15.0）。
     */
    virtual PlanSearchResult search(const PlannerSearchRequest& request) = 0;
};

/**
 * @brief 唯一产品实现的唯一构造入口（§15.5 形态行的落地面——实现 PRIVATE
 *        消费 sdurw_pathplanners；与 policy makeRobWorkCollisionEvaluator
 *        同款"唯一构造入口"装配纪律）。
 *
 * 实现要点（语义权威＝本注与实现内注释）：
 *   - QConstraint 适配（§10.3）：policy 会话 SingleState 查询的纯适配——
 *     每次构型查询直穿会话 evaluate（零本地判定副本/零缓存——AT-19 三入
 *     口一致）；仅 Collision 种类发现构成"碰撞"（MarginViolation 为非
 *     碰撞——policy §7.4）；**复核不可采信（finalized=false/作用域空/
 *     策略禁用）时保守返回"碰撞"**——宁可让规划器绕行/失败（结果落在
 *     BudgetExhausted→DataInsufficient 素材），绝不把证据缺口当作可通行
 *     （KIN-05 铁律的规划侧形态）；
 *   - 稳定候选选择序（§10.4"延续性→裕量→路径长度→候选序号"在候选路径
 *     层面的实现化，全序可复现 NFR-COR-02）：
 *       1. 延续性：相邻路点逐轴最大 |Δq| 升序（步进越平缓构型延续性越
 *          好——§7.5 规则 1"逐轴 |Δq|≤阈值"语义在路径层面的推广）；
 *       2. 裕量：候选全部构型全部有界关节的归一化裕量最小值降序（归一化
 *          比∈[0,1]＝2·min(q−lo, hi−q)/(hi−lo)；全无界＝+∞ 取最大——
 *          与 kinematics D-KIN-6 同源口径）；
 *       3. 路径长度：关节空间路径长度 Σ|Δq| 升序；
 *       4. 候选序号：candidateIndex 升序兜底（全序保证）。
 *
 * @return 适配器实例（调用方持有 unique_ptr；无跨调用状态）
 *
 * 异常安全：构造仅分配成员——不抛（std::bad_alloc 除外，进程内异常轨）。
 */
std::unique_ptr<IPathPlannerAdapter> makePathPlannerAdapter();

// =====================================================================
// §10.4 避障重规划编排——直连筛查→避障搜索→采纳/搜索未果
// =====================================================================

/**
 * @brief 避障段规划结局（§10.4 流程图的域内四态词表——封闭）。
 *
 * 刻意**不含任何"任务不可行"值**（C8 作用域——REQUIREMENTS §8.1 v1.16：
 * 候选路径碰撞仅淘汰该路径；搜索未果归 DataInsufficient 素材，任务级
 * 判定归 evidence——N7 边界的词表化承载）。
 */
enum class AvoidanceStatus : std::uint8_t {
    /// 已采纳路径（直连或避障候选）——segment 有效。
    Ok,
    /// 搜索未果（直连碰撞＋全部候选淘汰/预算耗尽）——searchRecord 与
    /// failure 素材必填（TRJ-NO-PATH 定位），**不判不可行**。
    SearchExhausted,
    /// 规划器/环境异常（转译自适配器 PlannerError——failure 必填）。
    PlannerError,
    /// 取消（任何轮询边界命中）——零错误素材（UX-03）。
    Canceled,
};

/**
 * @brief 避障段规划请求（§10.4 编排的输入面——构型对已经 PTP 层选定，
 *        本层只负责"两端构型之间走哪条路径"）。
 *
 * 与 PtpRequest（Ptp.hpp）的关系：PTP 层完成构型选择（§7.5 三键）与端点
 * 限位守卫后，若直连路径碰撞（§7.6 行 3）则把选定构型对交到本层触发避障
 * 搜索——两层的输入面同源（同一评价区间投影/段约束/身份字段），本请求
 * 不重复 PTP 的候选解集语义。
 *
 * 前置（fail-fast，token "trajectory/avoid/..."）：
 *   - startQ/endQ 同维度、全分量有限、在评价区间内；
 *   - constraint 合法（SegmentConstraint 合法域）；
 *   - tcpRef 为合法对象身份（§6.3 tcpRef 必填）；
 *   - startKind/endKind ∈ {Start, TaskPoint, End}（与 PtpRequest 同款
 *     端点词表——Via/Dwell 由本层/时间化产出，不收外部请求）；
 *   - endKind==TaskPoint ⇒ sourceTaskPoint 必填合法；startKind==TaskPoint
 *     ⇒ startSourceTaskPoint 必填合法；其余形态两者必须为空（Waypoint
 *     可追溯性不变量的请求侧投影——§6.2/NFR-COR-04）；
 *   - collisionCheckApplicable==true 时 policySession/workCell 非空且
 *     selection/预算/种子满足 PlannerSearchRequest 同款合法域（直连筛查
 *     与搜索共用同一校验序）；==false 时本层**跳过全部碰撞消费**直接
 *     采纳直连路径（"策略未启用碰撞→碰撞检查不在范围"——V13-01 同款
 *     唯一开关语义，非降级；constraint.collisionCheckApplicable=false
 *     随段标记自明）。
 *
 * 单位/坐标系：构型为权威关节向量（rad|m）；无笛卡尔量。值语义纯结构。
 */
struct AvoidancePlanRequest {
    /// 段起点构型（权威角 rad|m——PTP 层选定）。
    rw::math::Q startQ;
    /// 段终点构型（权威角 rad|m）。
    rw::math::Q endQ;
    /// 评价区间下界（逐轴 rad|m）。
    rw::math::Q lowerBoundQ;
    /// 评价区间上界（逐轴 rad|m）。
    rw::math::Q upperBoundQ;
    /// 段级约束（限值引用＋采样步长＋碰撞检查适用性——SegmentConstraint；
    /// collisionCheckApplicable 是本层是否消费 policy 的唯一开关）。
    SegmentConstraint constraint;
    /// TCP/工具对象引用（ARC-04 对象 ID——段身份字段，透传产物段）。
    core::ObjectId tcpRef;
    /// 参考系对象（World 时 nullopt——不伪造默认，§6.3）。
    std::optional<core::ObjectId> frameRef;
    /// 来源任务点（approach/work/retract 归属站；endKind==TaskPoint 时
    /// 必填合法——Waypoint 可追溯性不变量 NFR-COR-04；nullopt＝序列级
    /// 工具段〔endKind==End〕）。
    std::optional<core::ObjectId> sourceTaskPoint;
    /// 起点路点的来源任务点（站间转移段的起点是上一站任务点构型——
    /// startKind==TaskPoint 时必填合法；其余形态必须为空——与 PtpRequest
    /// 同款起点侧承载，§6.2 Waypoint.sourceTaskPoint 不变量）。
    std::optional<core::ObjectId> startSourceTaskPoint;
    /// 起点路点种类（Start/TaskPoint——词表见前置）。
    WaypointKind startKind = WaypointKind::Start;
    /// 终点路点种类（TaskPoint/End）。
    WaypointKind endKind = WaypointKind::TaskPoint;
    /// 段序号（0 基——段与路点的定位键）。
    std::uint32_t segmentIndex = 0;
    /// 规划器选型 family token（词表同 PlannerSearchRequest）。
    std::string plannerFamilyToken;
    /// 规划器参数键值表（白名单三键——同 PlannerSearchRequest）。
    std::map<std::string, std::string> plannerParams;
    /// 时间预算（s；>0 有限——直连筛查＋搜索共享）。
    double timeBudgetS = 5.0;
    /// 规划种子（无量纲；>0）。
    std::uint64_t seed = 0;
    /// policy 碰撞会话（collisionCheckApplicable==true 时必填——唯一碰撞
    /// 权威；false 时必须为空（防"带会话却跳过检查"的双口径——调用方
    /// 一致性违约 fail-fast））。
    std::shared_ptr<const policy::CollisionEvaluationSession> policySession;
    /// runtime 快照只读视图（collisionCheckApplicable==true 时必填——
    /// 零自建 WorkCell；false 时必须为空——同上一致性纪律）。
    const runtime::WorkCellConstView* workCell = nullptr;
    /// 设备运行时名（⑥端口解析产物透传——R-4；collisionCheckApplicable==
    /// true 时非空）。
    std::string deviceRuntimeName;
    /// 取消观测（可空＝不可取消；筛查前/候选边界轮询）。
    CancelSignal cancel;
};

/**
 * @brief 避障段规划结果（§10.4 流程图的产物面——段几何＋逐分支证据素材；
 *        值语义纯结构；线程安全）。
 */
struct AvoidancePlanResult {
    /// 规划结局（AvoidanceStatus 四态）。
    AvoidanceStatus status = AvoidanceStatus::Canceled;
    /// 产出段（Ok 时有效且过 validateTrajectorySegment——JointLinear 段，
    /// waypoints＝起点＋(避障候选的中间 Via 点)＋终点；其余状态无段）。
    TrajectorySegment segment;
    /// 直连路径被采纳（true＝未经避障搜索——直连筛查通过或碰撞检查不
    /// 适用；false＝采纳的是避障候选）。
    bool directPathAdopted = false;
    /// 被采纳避障候选的下标（适配器 candidates 向量序；仅 Ok 且
    /// !directPathAdopted 有值——证据回放锚）。
    std::optional<std::size_t> adoptedCandidateIndex;
    /// 直连候选碰撞淘汰记录（§10.4 流程图第一分支——"初始候选路径碰撞、
    /// 重规划成功"AT-06 反例的证据素材；仅 !directPathAdopted 时必填）。
    std::optional<CandidateCollisionRecord> directCollision;
    /// 搜索未果记录（SearchExhausted 时必填——预算/已试候选数/逐条淘汰
    /// 记录；Found 时若适配器报告了预算账目也可携带，非必填）。
    std::optional<SearchExhaustedRecord> searchRecord;
    /// 失败定位素材（SearchExhausted/PlannerError 时必填——phase 恒
    /// kPhasePlanAvoid；Canceled 恒空——UX-03）。
    std::optional<FailedSegmentRecord> failure;
};

/**
 * @brief 规划一段避障路径（§10.4 流程图的唯一执行点——TRJ-03 编排面）。
 *
 * 执行序（每步语义见行内注释；素材一律不判任务不可行）：
 *   1. 前置校验（调用方契约违约 → TrajectoryError fail-fast——维度/
 *      区间/约束/词表/参数键/预算/种子/会话一致性）；
 *   2. 取消轮询（命中 → Canceled，零素材）；
 *   3. 碰撞检查不适用（constraint.collisionCheckApplicable==false）→
 *      直连采纳（无碰撞检查义务——段约束标记自明）；
 *   4. 直连候选筛查：按 edge-resolution 均匀采样直连路径，一次 policy
 *      PathSequence 查询——无碰撞 → 直连采纳（最快路径，不进搜索）；
 *      检出碰撞 → 记录直连淘汰（C8 素材）进入避障搜索；复核不可采信
 *      （finalized=false 等）→ 按取消/数据不足归类（不当作碰撞淘汰——
 *      KIN-05）；
 *   5. 避障搜索：adapter.search（K 轮候选＋逐条 PathSequence 复核＋稳定
 *      选择序）——Found → 采纳候选构造段（中间点为 Via 路点）；
 *      BudgetExhausted → SearchExhausted＋素材；PlannerError → 转译；
 *      Canceled → 透传（零素材）；
 *   6. 结构守卫（validateTrajectorySegment——段构造保证的自证）。
 *
 * 限速/限加速度不在本函数判定（§12.2 唯一判定点——WP-16-T08）；本函数
 * 不判任务可行性（N7——素材经证据面汇总）。
 *
 * @param request [in] 规划请求（AvoidancePlanRequest 前置见其注）
 * @param adapter [in] 规划器适配器（借用——调用期存活；仅步骤 5 消费；
 *                碰撞检查不适用路径不触碰适配器）
 * @return 规划结果（AvoidancePlanResult 四态）
 *
 * @throws TrajectoryError 调用方契约违约（token "trajectory/avoid/..."）
 *
 * 纯函数（零副作用/零修订/零写盘）；线程安全（单线程使用）；确定性
 * （采样计划/选择序/种子化搜索全序——NFR-COR-02）。
 */
AvoidancePlanResult planPtpWithObstacleAvoidance(const AvoidancePlanRequest& request,
                                                 IPathPlannerAdapter& adapter);

}  // namespace trajectory
}  // namespace sdurws::ird

#endif  // IRD_TRAJECTORY_PLANNER_HPP