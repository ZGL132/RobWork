/**
 * @file   Planner.cpp
 * @brief  RobWork 规划器适配实现（§15.5 IPathPlannerAdapter 唯一产品实现
 *         ——sdurw_pathplanners 消费、policy 会话 QConstraint 适配、K 轮
 *         候选＋PathSequence 复核＋稳定选择序）与 §10.4 避障重规划编排
 *         （planPtpWithObstacleAvoidance——WP-16-T06 批）。
 *
 * 设计依据：
 *   - units/trajectory.md §10.3（与 policy 的交接——流程图：候选构型 q →
 *     QConstraint 适配器（纯适配零策略逻辑）→ CollisionQuery{SingleState}
 *     → 会话 evaluate → 无碰撞扩展/碰撞剪枝；候选路径完成 →
 *     CollisionQuery{PathSequence} → findings → 淘汰并触发重规划（C8）；
 *     finalized=false 不得采信；pathParameters 与 configurations 等长；
 *     阈值零参数化——CollisionQuery 不携带任何阈值/开关，R-POL-5）、
 *     §10.4（避障重规划流程图——直连碰撞→搜索→候选逐条复核→淘汰重规划
 *     →SearchExhaustedRecord→DataInsufficient 素材不判不可行；选择序
 *     "延续性→裕量→路径长度→候选序号"）、§10.1（WorkCell/Device/State
 *     只来自注入视图；碰撞判定只经 policy 会话——R-5/R-POL-2）
 *   - §15.5（接口基线——"实现 PRIVATE 消费 sdurw_pathplanners"；
 *     "PlannerError→TRJ 域素材＋RT-ROBWORK-ERROR 转译登记（runtime
 *     translateRobWorkError 同款不吞异常）"；"若实测某 planner 内部不可
 *     种子化→该 family 不入选"——本实现经 rw::math::Math::seed 全局种子
 *     化 RRT 随机源，黄金算例验证同种子等价候选集合）、§15.0（错误二分/
 *     取消/确定性/线程）
 *   - 需求 TRJ-03、REQUIREMENTS §8.1 C8（候选路径碰撞＝淘汰并触发重规划，
 *     不判任务不可行）、KIN-05（证据缺口绝不解读为无碰撞）、R-4/R-5
 *   - 先例：kinematics/src/Collision.cpp（WP-15-T07——PolicyCollision-
 *     SessionAdapter 每查询直穿会话、Completed/EmptyScope/Disabled/Canceled/
 *     Failed 三态映射同款）、policy/src/CollisionQuery.cpp（evaluate 执行序
 *     ——本文件的调用方契约）
 *   - 任务契约 tasks/foundation/WP-16-T06.json（acceptance 1/2/3）
 *
 * 线程安全：本翻译单元全部实体无共享可变状态（适配器实例单线程使用——
 * §15.0；rw::math::Math::seed 为进程级全局随机源，种子化时点在每次 search
 * 的规划器构造前，并发 search 的互斥归装配面线程约束——文件头注已登记）。
 * 确定性：同 (请求值, 种子) → 等价候选集合（Math::seed＋RRT 顺序执行＋
 * 稳定选择序全序——NFR-COR-02）。
 */

#include <sdurws/ird/trajectory/Planner.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

// ---- rw 基线（集成模式专属——本 TU 为集成条件源，冒烟模式不编译）----
#include <rw/core/Exception.hpp>
#include <rw/math/Math.hpp>
#include <rw/math/MetricFactory.hpp>
#include <rw/models/Device.hpp>
#include <rw/models/SerialDevice.hpp>
#include <rw/models/WorkCell.hpp>
#include <rw/pathplanning/PlannerConstraint.hpp>
#include <rw/pathplanning/QConstraint.hpp>
#include <rw/pathplanning/QEdgeConstraint.hpp>
#include <rw/pathplanning/QSampler.hpp>
#include <rw/pathplanning/QToQPlanner.hpp>
#include <rwlibs/pathplanners/rrt/RRTPlanner.hpp>
#include <rw/trajectory/Path.hpp>

// ---- 对端单元公共头（七条登记边内——runtime 视图＋policy 会话④端口）----
#include <sdurws/ird/policy/CollisionEvaluator.hpp>
#include <sdurws/ird/policy/CollisionQuery.hpp>
#include <sdurws/ird/policy/Contexts.hpp>
#include <sdurws/ird/runtime/Adapter.hpp>

// ---- 本单元 ----
#include <sdurws/ird/trajectory/DiagCodes.hpp>  // TRJ-* 码常量（reasonToken 唯一书写点）
#include <sdurws/ird/trajectory/Errors.hpp>     // TrajectoryError（fail-fast 载体）
#include <sdurws/ird/trajectory/Ptp.hpp>        // interpolateJointLinear（§7.3 直连
                                                //   采样与段几何的唯一实现点——
                                                //   "同语义同一实现点"纪律）

namespace sdurws::ird::trajectory {
namespace {

// =====================================================================
// 内部辅助——policy 调用上下文（§10.3"取消/预算/超时传递"的适配落点）
// =====================================================================

/**
 * @brief policy evaluate 的调用上下文（IPolicyCallContext 最小适配——
 *        恒存活＋取消委托）。
 *
 * 为什么恒存活：本域的会话与请求同生命周期（编排层每段规划持有会话
 * shared_ptr——适配器借用指针在 search 调用期内必然有效），"迟到调用"
 * 场景（快照废弃后评估）在评估器装配面被结构排除（每任务每实例——§6.3
 * 线程行）；因此 alive() 恒 true，迟到防护不在此重复实现（policy 侧
 * POL-LATE-1 的宿主语义由装配面承载）。
 *
 * 取消委托：cancellationRequested 逐次转发调用方的 CancelSignal（可空＝
 * 不可取消）——policy evaluate 在样本边界轮询该信号（§6.6 时序），使
 * 取消能穿透到 PathSequence 逐样本循环内部。
 */
class AdapterCallContext final : public policy::IPolicyCallContext {
public:
    /// 构造：绑定取消信号源（借用——调用期存活；可空＝不可取消）。
    explicit AdapterCallContext(const CancelSignal* cancel) : m_cancel(cancel) {}

    /// 委托调用方取消信号（policy §6.6 样本边界轮询的对端；双重判空——
    /// 指针非空且信号对象有目标：空 function＝"不可取消"而非"请求取消"
    /// ——std::bad_function_call 防线）。
    bool cancellationRequested() const override
    {
        return m_cancel != nullptr && static_cast<bool>(*m_cancel) && (*m_cancel)();
    }

    /// 恒存活（会话与请求同生命周期——见类注）。
    bool alive() const override { return true; }

private:
    /// 取消信号源（借用；不接管所有权——AGENTS §2.5 所有权标注）。
    const CancelSignal* m_cancel;
};

// =====================================================================
// 内部辅助——QConstraint 适配器（§10.3 流程图第二框：纯适配零策略逻辑）
// =====================================================================

/**
 * @brief policy 会话 → RobWork 规划器约束的适配器（rw::pathplanning::
 *        QConstraint 子类——§10.3"QConstraint 适配器（trajectory 持有，
 *        纯适配零策略逻辑）"的唯一实现点；文件私有）。
 *
 * 适配语义（每查询直穿会话——kinematics PolicyCollisionSessionAdapter
 * 同款纪律，AT-19 三入口一致）：
 *   1. 每个构型查询构造一次 CollisionQuery{SingleState, 1 构型}，立即
 *      调用会话 evaluate——零本地判定副本、零记忆化（R-POL-5：查询不
 *      携带任何阈值/开关——安全间距唯一来源＝会话绑定的策略集）；
 *   2. 仅 Collision 种类发现构成"碰撞"（SafetyMarginViolation 间距不足
 *      是"非碰撞"——policy §7.4 决策表；间距语义归复检/证据面）；
 *   3. 复核不可采信（Completed 但作用域空/策略禁用、Canceled、Failed——
 *      即非"Completed＋Applicable"的一切形态）→ **保守返回"碰撞"**：
 *      KIN-05 铁律的规划侧形态——证据缺口绝不解读为可通行；规划器把该
 *      构型当障碍绕行或整体失败，结果落在 BudgetExhausted→DataInsufficient
 *      素材轨，绝不产出未经验证的"无碰撞"路径。不可采信的出现次数记入
 *      untrustedObserved（观测面——契约测试断言直穿/不采信语义）。
 */
class PolicySessionQConstraint final : public rw::pathplanning::QConstraint {
public:
    /// 构造：绑定碰撞会话（共享只读）与取消信号源（借用）。
    PolicySessionQConstraint(
        std::shared_ptr<const policy::CollisionEvaluationSession> session,
        const CancelSignal* cancel)
        : m_session(std::move(session)), m_ctx(cancel)
    {
    }

    /// 规划期不可采信评估的出现次数（观测面——非判定输入；单线程访问）。
    mutable std::size_t untrustedObserved = 0;

    /// @brief 日志句柄注入（QConstraint 纯虚的空实现——本适配器零日志
    /// 面适配：诊断产出归 policy 会话与评估器组装面，规划约束层不重复
    /// 记录；NFR-MNT-03 单一权威）。
    void doSetLog(rw::core::Log::Ptr) override {}

    /// @brief 单构型碰撞判定（QConstraint 契约的实现点——适配语义见类注）。
    bool doInCollision(const rw::math::Q& q) const override
    {
        // 第一步：装配 SingleState 查询（kind 与样本数匹配契约——恰 1 个
        // 构型且 pathParameters 为空，policy §6.2；违约由 evaluate 入口
        // fail-fast——本适配器保证不触雷）。
        policy::CollisionQuery query;
        query.kind = policy::CollisionQueryKind::SingleState;
        query.configurations = {q};
        // stopAtFirstFinding＝筛选/淘汰场景（§6.2 字段语义——单构型查询
        // 的对逐对查询可在首个发现即停；输出仍带 coverage）。
        query.stopAtFirstFinding = true;

        // 第二步：直穿会话评估（会话构造后只读——并发只读安全由会话
        // backendQueryMutex 保证，policy §9.3；本适配器零额外同步）。
        const policy::CollisionEvaluation evaluation = m_session->evaluate(query, m_ctx);

        // 第三步：三态映射（仅"Completed＋Applicable＋finalized"可采信）。
        if (evaluation.status == policy::CollisionEvaluationStatus::Completed
            && evaluation.finalized
            && evaluation.applicability == policy::ScopeApplicability::Applicable) {
            for (const policy::CollisionFinding& finding : evaluation.findings) {
                // 仅 Collision 种类构成碰撞（MarginViolation 非碰撞——§7.4）。
                if (finding.kind == policy::CollisionFindingKind::Collision) {
                    return true;  // 碰撞→剪枝（§10.3 流程图"碰撞→剪枝"）
                }
            }
            return false;  // 无碰撞发现→扩展继续
        }
        // 不可采信（finalized=false / EmptyScope / DisabledByPolicy）——
        // 保守"碰撞"＋观测计数（KIN-05：绝不视为无碰撞）。
        ++untrustedObserved;
        return true;
    }

private:
    /// 碰撞会话（共享只读——④端口唯一碰撞权威）。
    std::shared_ptr<const policy::CollisionEvaluationSession> m_session;
    /// 调用上下文（取消委托——构造期绑定）。
    AdapterCallContext m_ctx;
};

// =====================================================================
// 内部辅助——选型解析与校验（§10.2 词表/参数白名单的唯一执行点）
// =====================================================================

/**
 * @brief 规划器选型的解析产物（白名单三键的强类型投影——校验通过后
 *        才存在；避免实现内散落字符串解析）。
 */
struct PlannerTuning {
    /// RRT 扩展步长（逐轴欧氏度量；rad|m——kTrjPlannerParamExtend）。
    double extend = 0.0;
    /// 边检查分辨率（rad|m——kTrjPlannerParamEdgeResolution；兼采样密度）。
    double resolution = 0.0;
    /// 候选规划轮数 K（≥1——kTrjPlannerParamCandidateAttempts）。
    std::uint32_t attempts = 0;
};

/**
 * @brief 解析并校验规划器选型（§10.2 词表＋参数白名单；违约抛
 *        TrajectoryError——调用方配置错误属 fail-fast 轨，token
 *        "trajectory/planner/selection-*"）。
 *
 * 校验序固定（确定性——同一坏选型必报同一首错，NFR-COR-02）：
 *   1. family token ∈ 词表（当前唯一值 rrt-connect）；
 *   2. 参数键 ⊆ 白名单三键（未知键拒绝——封闭词表）；
 *   3. 三键齐备且值合法（extend/resolution 有限 >0；attempts 为正整数
 *      文本）。
 *
 * 三键为何必填而非缺省：§5.5/§10.2——plannerSelection 进 canonical 身份
 * （config.trj 摘要），参数的任何缺省补全都会引入"配置外的隐式数值"
 * （D-TRJ-5 数值零自设纪律的规划器侧形态）；缺键＝配置不完整＝调用方
 * 违约，拒绝而不是猜。
 */
PlannerTuning parsePlannerSelection(const std::string& familyToken,
                                    const std::map<std::string, std::string>& params)
{
    // 1. family 词表（§10.2 封闭词表——词表外拒绝，不猜测）。
    if (familyToken != kTrjPlannerFamilyRrtConnect) {
        throw TrajectoryError("trajectory/planner/selection-family",
                              "规划器 family 不在登记词表（当前唯一实现值 "
                              "rrt-connect），实际值: " + familyToken);
    }
    // 2. 参数键白名单（未知键拒绝——键集合封闭，§10.2"逐键登记"）。
    for (const auto& entry : params) {
        if (entry.first != kTrjPlannerParamExtend
            && entry.first != kTrjPlannerParamEdgeResolution
            && entry.first != kTrjPlannerParamCandidateAttempts) {
            throw TrajectoryError("trajectory/planner/selection-param-key",
                                  "规划器参数键不在白名单（extend / edge-resolution / "
                                  "candidate-attempts），实际键: " + entry.first);
        }
    }
    // 3. 逐键取值与合法域（三键必备——缺键拒绝，不补默认）。
    PlannerTuning tuning;
    const auto readDouble = [&](const char* key) {
        const auto it = params.find(key);
        if (it == params.end()) {
            throw TrajectoryError("trajectory/planner/selection-param-missing",
                                  "规划器参数缺键（三键必填）: " + std::string{key});
        }
        try {
            const std::string& text = it->second;
            // std::stod 消费前缀——校验"整串可解析"（尾随非法字符拒绝）。
            std::size_t consumed = 0;
            const double value = std::stod(text, &consumed);
            if (consumed != text.size()) {
                throw std::invalid_argument("trailing");
            }
            return value;
        } catch (const std::exception&) {
            throw TrajectoryError("trajectory/planner/selection-param-value",
                                  "规划器参数值不是合法数值: " + std::string{key}
                                      + " = " + it->second);
        }
    };
    tuning.extend = readDouble(kTrjPlannerParamExtend);
    tuning.resolution = readDouble(kTrjPlannerParamEdgeResolution);
    if (!(tuning.extend > 0.0) || !std::isfinite(tuning.extend)) {
        throw TrajectoryError("trajectory/planner/selection-param-value",
                              "规划器扩展步长 extend 必须为有限正数（rad|m），实际: "
                                  + std::to_string(tuning.extend));
    }
    if (!(tuning.resolution > 0.0) || !std::isfinite(tuning.resolution)) {
        throw TrajectoryError("trajectory/planner/selection-param-value",
                              "规划器边检查分辨率 edge-resolution 必须为有限正数"
                              "（rad|m），实际: " + std::to_string(tuning.resolution));
    }
    const double attemptsRaw = readDouble(kTrjPlannerParamCandidateAttempts);
    if (!(attemptsRaw >= 1.0) || attemptsRaw != std::floor(attemptsRaw)
        || attemptsRaw > static_cast<double>(std::numeric_limits<std::uint32_t>::max())) {
        throw TrajectoryError("trajectory/planner/selection-param-value",
                              "规划器候选轮数 candidate-attempts 必须为正整数（≥1），"
                              "实际: " + std::to_string(attemptsRaw));
    }
    tuning.attempts = static_cast<std::uint32_t>(attemptsRaw);
    return tuning;
}

/**
 * @brief 构型对与评价区间的公共校验（适配器/编排层共用——同一违约同一
 *        首 token，保证两层错误面一致）。
 *
 * 校验序（确定性）：维度一致 → 区间逐轴 lower<upper → 端点全分量有限 →
 * 端点在区间内（§15.1 PTP 前置"起终点构型在评价区间内"的同源守卫——
 * 越限属调用方契约违约 fail-fast，本层不产 TRJ-LIMIT-EXCEEDED 素材：该
 * 素材的归口是 PTP 层端点守卫 §7.4/V-03，本层重复产码会破坏唯一判定点
 * 纪律）。
 */
void validateConfigPairAndBounds(const rw::math::Q& fromQ, const rw::math::Q& toQ,
                                 const rw::math::Q& lowerBoundQ,
                                 const rw::math::Q& upperBoundQ,
                                 const char* errorTokenPrefix)
{
    const std::size_t dof = fromQ.size();
    if (toQ.size() != dof || lowerBoundQ.size() != dof || upperBoundQ.size() != dof) {
        throw TrajectoryError(errorTokenPrefix, "起终点构型与评价区间维度不一致");
    }
    for (std::size_t i = 0; i < dof; ++i) {
        if (!(lowerBoundQ[i] < upperBoundQ[i])) {
            throw TrajectoryError(errorTokenPrefix,
                                  "评价区间逐轴必须 lower<upper，轴 " + std::to_string(i)
                                      + " 实际: [" + std::to_string(lowerBoundQ[i]) + ", "
                                      + std::to_string(upperBoundQ[i]) + "]");
        }
    }
    for (std::size_t i = 0; i < dof; ++i) {
        if (!std::isfinite(fromQ[i]) || !std::isfinite(toQ[i])) {
            throw TrajectoryError(errorTokenPrefix,
                                  "端点构型含非有限分量（rad|m），轴 "
                                      + std::to_string(i));
        }
        if (fromQ[i] < lowerBoundQ[i] || fromQ[i] > upperBoundQ[i]
            || toQ[i] < lowerBoundQ[i] || toQ[i] > upperBoundQ[i]) {
            throw TrajectoryError(errorTokenPrefix,
                                  "端点构型越出评价区间（调用方前置违约——§15.1），轴 "
                                      + std::to_string(i));
        }
    }
}

// =====================================================================
// 内部辅助——路径采样与 PathSequence 复核（§10.3 直连/候选共用通道）
// =====================================================================

/**
 * @brief 均匀采样直连路径（§10.4 流程图第一分支"直连候选路径采样"——
 *        采样协议：按 edge-resolution 等分关节空间欧氏长度，端点必含；
 *        采样计划参数（resolution）随 plannerSelection.params 进 canonical
 *        身份——§5.6"采样计划进入身份"）。
 *
 * @return 采样结果（configurations 与 pathParameters 等长；s∈[0,1] 均匀；
 *         至少 2 个样本——端点必含）
 */
struct SampledPath {
    std::vector<rw::math::Q> configurations;  ///< 采样构型序列（rad|m）
    std::vector<double> pathParameters;       ///< 逐样本 s∈[0,1]（等长）
};

SampledPath sampleStraightPath(const rw::math::Q& fromQ, const rw::math::Q& toQ,
                               double resolution)
{
    // 欧氏长度（逐轴混合计量 rad|m——与规划器度量同源口径，§6.4）。
    double length = 0.0;
    for (std::size_t i = 0; i < fromQ.size(); ++i) {
        const double delta = toQ[i] - fromQ[i];
        length += delta * delta;
    }
    length = std::sqrt(length);
    // 分段数＝ceil(长度/分辨率)，下限 1（零长/极短段至少 1 段——样本数
    // ≥2，端点必含）；上限防病态小分辨率把样本数撑爆（分辨率来自显式
    // 配置，调用方对采样规模负责；此处仅防溢出级灾难）。
    const double segmentCountReal = std::ceil(length / resolution);
    const auto segmentCount = static_cast<std::size_t>(
        std::min(std::max(segmentCountReal, 1.0), 1.0e6));
    SampledPath sampled;
    sampled.configurations.reserve(segmentCount + 1);
    sampled.pathParameters.reserve(segmentCount + 1);
    for (std::size_t i = 0; i <= segmentCount; ++i) {
        const double s = static_cast<double>(i) / static_cast<double>(segmentCount);
        // 关节空间逐轴线性插值（§7.3 同一几何语义——复用 interpolateJoint-
        // Linear 保证"同语义同一实现点"；s∈[0,1] 由构造保证不越界）。
        sampled.configurations.push_back(interpolateJointLinear(fromQ, toQ, s));
        sampled.pathParameters.push_back(s);
    }
    return sampled;
}

/**
 * @brief PathSequence 复核结论（域内三态——与 CandidateReviewStatus 对齐）。
 */
enum class ReviewOutcome { Passed, Collision, DataInsufficient };

/**
 * @brief 一次 PathSequence 复核（§10.3 流程图"候选路径完成→PathSequence
 *        评估"的执行点；直连筛查与避障候选复核共用同一会话通道——AT-19
 *        三入口一致）。
 *
 * 采样约定（§10.3"pathParameters 约定"逐条）：configurations 与
 * pathParameters 必须等长（调用方保证——违约在会话 evaluate 入口
 * fail-fast，属本域内部契约违约）；样本顺序含路径连续性语义；stopAt-
 * FirstFinding＝淘汰场景（首个发现即停，输出仍带 coverage）；阈值零
 * 参数化（查询不携带任何阈值/开关——R-POL-5）。
 *
 * 三态映射（§10.3"finalized=false 不得采信"）：
 *   - Completed＋Applicable＋finalized＋零 Collision 发现 → Passed
 *     （MarginViolation 不构成碰撞——§7.4，也不进淘汰记录）；
 *   - Completed＋Applicable＋finalized＋含 Collision 发现 → Collision
 *     （逐条转 CandidateCollisionRecord——对象对、sampleIndex、
 *     pathParameter；finalized=false 部分发现不会出现在本分支）；
 *   - 其余一切（Canceled/Failed/EmptyScope/DisabledByPolicy）→
 *     DataInsufficient（不采信、不判碰撞、不采纳——KIN-05）。
 */
struct ReviewResult {
    ReviewOutcome outcome = ReviewOutcome::DataInsufficient;
    std::vector<CandidateCollisionRecord> records;  ///< 碰撞淘汰明细（Collision 态非空）
};

ReviewResult reviewPathSequence(const policy::CollisionEvaluationSession& session,
                                const AdapterCallContext& ctx,
                                const std::vector<rw::math::Q>& configurations,
                                const std::vector<double>& pathParameters)
{
    // 查询装配（PathSequence 契约：构型非空＋pathParameters 等长；无
    // baseState＝场景基准状态——policy evaluate 侧取 WC 默认态）。
    policy::CollisionQuery query;
    query.kind = policy::CollisionQueryKind::PathSequence;
    query.configurations = configurations;
    query.pathParameters = pathParameters;
    query.stopAtFirstFinding = true;  // 淘汰场景——首个发现即停
    query.requestMinDistance = false; // 二值后端无距离能力（P-POL-11），不请求

    const policy::CollisionEvaluation evaluation = session.evaluate(query, ctx);

    ReviewResult result;
    // 只有"Completed＋Applicable＋finalized"是可采信终态（§6.3 状态机）。
    if (evaluation.status == policy::CollisionEvaluationStatus::Completed
        && evaluation.finalized
        && evaluation.applicability == policy::ScopeApplicability::Applicable) {
        bool collided = false;
        for (const policy::CollisionFinding& finding : evaluation.findings) {
            if (finding.kind != policy::CollisionFindingKind::Collision) {
                continue;  // MarginViolation＝间距不足非碰撞（§7.4）——不淘汰
            }
            collided = true;
            CandidateCollisionRecord record;
            record.objectA = finding.objectA;  // policy 规范序 A<B（NFR-COR-05）
            record.objectB = finding.objectB;
            record.sampleIndex = finding.sampleIndex;
            record.pathParameter = finding.pathParameter;  // PathSequence 必填（TRJ-04）
            result.records.push_back(std::move(record));
        }
        // coverage 完整性说明（§10.4"coverage 完整"）：段内样本的全对查询
        // 事实由 policy coverage 记录承载；几何缺口（pairsWithGeometry<
        // pairsInScope）policy 侧已以 POLICY-CLL-GEOMETRY-MISSING 显式化
        // （走 Failed→DataInsufficient 分支），此处不重复判定。
        result.outcome = collided ? ReviewOutcome::Collision : ReviewOutcome::Passed;
        return result;
    }
    result.outcome = ReviewOutcome::DataInsufficient;  // 不可采信——KIN-05
    return result;
}

// =====================================================================
// 内部辅助——稳定选择序四键（§10.4"延续性→裕量→路径长度→候选序号"）
// =====================================================================

/// 相邻路点逐轴最大 |Δq|（延续性键——步进越平缓越好，升序；rad|m）。
double maxAdjacentStep(const std::vector<rw::math::Q>& configurations)
{
    double maxStep = 0.0;
    for (std::size_t i = 1; i < configurations.size(); ++i) {
        for (std::size_t j = 0; j < configurations[i].size(); ++j) {
            maxStep = std::max(maxStep, std::abs(configurations[i][j] - configurations[i - 1][j]));
        }
    }
    return maxStep;
}

/// 关节空间路径长度 Σ|Δq|（路径长度键——升序；rad|m 逐轴混合计量）。
double pathJointLength(const std::vector<rw::math::Q>& configurations)
{
    double length = 0.0;
    for (std::size_t i = 1; i < configurations.size(); ++i) {
        for (std::size_t j = 0; j < configurations[i].size(); ++j) {
            length += std::abs(configurations[i][j] - configurations[i - 1][j]);
        }
    }
    return length;
}

/// 候选最小归一化关节裕量（裕量键——降序；有界轴 margin=2·min(q−lo, hi−q)/
/// (hi−lo)∈[0,1]，无界轴跳过；全无界＝+∞ 取最大——D-KIN-6 同源口径）。
double minNormalizedMargin(const std::vector<rw::math::Q>& configurations,
                           const rw::math::Q& lowerBoundQ, const rw::math::Q& upperBoundQ)
{
    double minimum = std::numeric_limits<double>::infinity();
    for (const rw::math::Q& q : configurations) {
        for (std::size_t j = 0; j < q.size(); ++j) {
            const double range = upperBoundQ[j] - lowerBoundQ[j];
            if (!(range > 0.0) || !std::isfinite(range)) {
                continue;  // 无界/退化轴不参与（全无界时 minimum 保持 +∞）
            }
            const double margin =
                2.0 * std::min(q[j] - lowerBoundQ[j], upperBoundQ[j] - q[j]) / range;
            minimum = std::min(minimum, margin);
        }
    }
    return minimum;
}

/**
 * @brief 候选选择序严格弱序（四键全序——见 makePathPlannerAdapter
 *        实现注；NFR-COR-02 全序可复现）。
 */
bool candidatePreferred(const PathCandidate& lhs, const PathCandidate& rhs,
                        const rw::math::Q& lowerBoundQ, const rw::math::Q& upperBoundQ)
{
    // 键 1 延续性：maxAdjacentStep 升序。
    const double stepL = maxAdjacentStep(lhs.configurations);
    const double stepR = maxAdjacentStep(rhs.configurations);
    if (stepL != stepR) { return stepL < stepR; }
    // 键 2 裕量：minNormalizedMargin 降序（+∞ 优先）。
    const double marginL = minNormalizedMargin(lhs.configurations, lowerBoundQ, upperBoundQ);
    const double marginR = minNormalizedMargin(rhs.configurations, lowerBoundQ, upperBoundQ);
    if (marginL != marginR) { return marginL > marginR; }
    // 键 3 路径长度：升序。
    const double lengthL = pathJointLength(lhs.configurations);
    const double lengthR = pathJointLength(rhs.configurations);
    if (lengthL != lengthR) { return lengthL < lengthR; }
    // 键 4 候选序号：升序兜底（全序保证）。
    return lhs.candidateIndex < rhs.candidateIndex;
}

// =====================================================================
// 内部辅助——失败素材构造（TRJ-06 定位载体；phase 恒 plan-avoid）
// =====================================================================

/**
 * @brief 构造避障失败素材（FailedSegmentRecord——ERR-01 字段齐备：中文
 *        原因与建议动作必填；reasonToken 恒取 DiagCodes 在册常量）。
 */
FailedSegmentRecord makeAvoidFailure(std::uint32_t segmentIndex,
                                     const std::string& reasonToken,
                                     const std::string& cause,
                                     const std::string& recommendedAction)
{
    FailedSegmentRecord record;
    record.segmentIndex = segmentIndex;
    record.phaseToken = kPhasePlanAvoid;  // §14.1.6 phase 词表（TrjTypes 常量）
    record.reasonToken = reasonToken;
    record.cause = cause;
    record.recommendedAction = recommendedAction;
    return record;
}

// =====================================================================
// §15.5 唯一产品实现——PathPlannerAdapterImpl（sdurw_pathplanners 消费）
// =====================================================================

/**
 * @brief IPathPlannerAdapter 唯一产品实现（RRT-Connect family——词表
 *        kTrjPlannerFamilyRrtConnect 的黄金算例锁定选型）。
 *
 * 每次/search 独立构造规划器（无跨调用状态——§15.5 契约；种子化时点在
 * 规划器构造前是确定性的结构基础）。规划器构造链（rw API 装配序，全部
 * 碰撞判定经 policy 适配约束——零 proximity 直链，D-TRJ-3）：
 *   policy 会话 → PolicySessionQConstraint（SingleState 适配）
 *     → 与设备 bounds 约束合并（QConstraint::makeMerged——构型与边都受
 *       限位+碰撞双重约束）
 *     → QEdgeConstraint::make（直线插值离散检查，分辨率＝edge-resolution）
 *     → PlannerConstraint::make（构型约束＋边约束）
 *     → QSampler::makeConstrained(makeUniform(设备 bounds))（约束采样器）
 *     → RRTPlanner::makeQToQPlanner（RRTConnect；度量＝逐轴欧氏；扩展步长
 *       ＝extend）
 *     → 逐轮 query(from, to, path, 剩余预算秒)。
 */
class PathPlannerAdapterImpl final : public IPathPlannerAdapter {
public:
    PlanSearchResult search(const PlannerSearchRequest& request) override
    {
        // ---------------------------------------------------------------
        // 第一步：前置校验（调用方契约违约 fail-fast——§15.5 前置逐项）。
        // ---------------------------------------------------------------
        validateConfigPairAndBounds(request.fromQ, request.toQ, request.lowerBoundQ,
                                    request.upperBoundQ,
                                    "trajectory/planner/config-pair");
        if (request.workCell == nullptr) {
            throw TrajectoryError("trajectory/planner/view-missing",
                                  "runtime 快照只读视图缺失（WorkCell/Device 零自建"
                                  "——§10.1，调用方契约违约）");
        }
        if (request.deviceRuntimeName.empty()) {
            throw TrajectoryError("trajectory/planner/device-name",
                                  "设备运行时名为空（⑥端口解析产物透传——R-4）");
        }
        if (request.session == nullptr) {
            throw TrajectoryError("trajectory/planner/session-missing",
                                  "policy 碰撞会话缺失（R-5 唯一碰撞权威——适配器"
                                  "恒要求会话，跳过检查的语义在编排层承载）");
        }
        if (!(request.timeBudgetS > 0.0) || !std::isfinite(request.timeBudgetS)) {
            throw TrajectoryError("trajectory/planner/time-budget",
                                  "时间预算必须为有限正数（s），实际: "
                                      + std::to_string(request.timeBudgetS));
        }
        if (request.seed == 0) {
            throw TrajectoryError("trajectory/planner/seed",
                                  "规划种子必须为正（0 非法——I-KIN-4 同款拒绝，"
                                  "NFR-COR-02 种子纪律）");
        }
        const PlannerTuning tuning =
            parsePlannerSelection(request.plannerFamilyToken, request.plannerParams);

        // ---------------------------------------------------------------
        // 第二步：种子化全局随机源（§15.5"种子化规划器"——rw 规划器的随机
        // 性经进程级 Math::seed 约束；uint64 种子截断为 unsigned 的映射是
        // 确定性的（同种子同截断），不破坏 NFR-COR-02；并发 search 的种
        // 子互扰归装配面线程约束——评估器 SingleThread）。
        // ---------------------------------------------------------------
        rw::math::Math::seed(static_cast<unsigned>(request.seed));

        // ---------------------------------------------------------------
        // 第三步：定位设备（只读视图查询——名称为⑥端口解析产物透传；
        // 未命中＝调用方违约 fail-fast，不猜测名称——R-4/ARC-04）。
        // ---------------------------------------------------------------
        const rw::core::Ptr<const rw::models::SerialDevice> device =
            request.workCell->findDevice(request.deviceRuntimeName);
        if (device.isNull()) {
            throw TrajectoryError("trajectory/planner/device-unresolved",
                                  "设备在快照只读视图中不可解析: "
                                      + request.deviceRuntimeName);
        }
        const std::size_t dof = device->getDOF();
        if (request.fromQ.size() != dof) {
            throw TrajectoryError("trajectory/planner/dof-mismatch",
                                  "构型维度与设备自由度不一致：构型 "
                                      + std::to_string(request.fromQ.size()) + "，设备 "
                                      + std::to_string(dof));
        }

        // ---------------------------------------------------------------
        // 第四步：构造碰撞约束/采样器/规划器（装配序见类注；规划器局部
        // 对象——每次 search 独立，无跨调用状态）。
        // ---------------------------------------------------------------
        const AdapterCallContext ctx(&request.cancel);
        // policy 会话适配约束（rw::core::ownedPtr 接管生命周期；合并约束
        // 持有其 Ptr——存活期覆盖规划器）。
        rw::core::Ptr<PolicySessionQConstraint> policyConstraint =
            rw::core::ownedPtr(new PolicySessionQConstraint(request.session, &request.cancel));
        // 设备 bounds 约束（编译产物权威限位——构型守卫与采样盒同源）。
        rw::core::Ptr<rw::pathplanning::QConstraint> boundsConstraint =
            rw::pathplanning::QConstraint::makeBounds(device->getBounds());
        // 合并（碰撞＋限位双约束——构型与边都受限）。
        rw::core::Ptr<rw::pathplanning::QConstraint> mergedConstraint =
            rw::pathplanning::QConstraint::makeMerged(policyConstraint, boundsConstraint);
        // 逐轴欧氏度量（rad|m 混合计量——与本域路径长度/选择序同口径）。
        const rw::math::QMetric::Ptr metric =
            rw::math::MetricFactory::makeEuclidean<rw::math::Q>();
        // 边约束（直线插值离散检查——分辨率＝edge-resolution，§10.2）。
        const rw::pathplanning::QEdgeConstraint::Ptr edge =
            rw::pathplanning::QEdgeConstraint::make(mergedConstraint, metric,
                                                    tuning.resolution);
        // 规划约束（构型＋边）。
        const rw::pathplanning::PlannerConstraint plannerConstraint =
            rw::pathplanning::PlannerConstraint::make(mergedConstraint, edge);
        // 约束采样器（均匀采样设备 bounds，再经合并约束过滤——采样失败
        // 返回空 Q，RRT 内部重试）。
        const rw::pathplanning::QSampler::Ptr sampler =
            rw::pathplanning::QSampler::makeConstrained(
                rw::pathplanning::QSampler::makeUniform(device->getBounds()),
                mergedConstraint);
        // RRT-Connect 规划器（黄金算例锁定选型——词表注释）。
        const rw::pathplanning::QToQPlanner::Ptr planner =
            rwlibs::pathplanners::RRTPlanner::makeQToQPlanner(
                plannerConstraint, sampler, metric, tuning.extend,
                rwlibs::pathplanners::RRTPlanner::RRTConnect);

        // ---------------------------------------------------------------
        // 第五步：K 轮候选循环（预算先到先停；取消轮边界轮询——§15.1）。
        // 每轮：query 产出一条候选路径→立即 PathSequence 复核→收集。
        // ---------------------------------------------------------------
        PlanSearchResult result;
        SearchExhaustedRecord exhaustedRecord;
        // 墙钟账目：start/deadline 双锚——预算判定用 deadline，消耗账目
        // 用 start（SearchExhaustedRecord.consumedBudgetS 的如实来源）。
        const auto startClock = std::chrono::steady_clock::now();
        const auto deadline = startClock
            + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                std::chrono::duration<double>(request.timeBudgetS));
        bool canceled = false;
        bool plannerError = false;
        std::string plannerErrorCause;

        for (std::uint32_t round = 0; round < tuning.attempts; ++round) {
            // 取消轮询（轮边界——命中立即停止；取消不是错误，零错误素材）。
            if (request.cancel && request.cancel()) {
                canceled = true;
                break;
            }
            // 剩余预算（多轮共享总预算——先到先停，§10.3 预算传递）。
            const double remainingS =
                std::chrono::duration<double>(deadline - std::chrono::steady_clock::now())
                    .count();
            if (!(remainingS > 0.0)) {
                break;  // 预算耗尽——按搜索未果收尾
            }
            // 一轮规划（rw 异常就地转译——不吞、不逃逸，§15.5 错误行）。
            rw::trajectory::QPath plannedPath;
            bool found = false;
            try {
                found = planner->query(request.fromQ, request.toQ, plannedPath, remainingS);
            } catch (const rw::core::Exception& e) {
                // RT-ROBWORK-ERROR 转译登记（runtime translateRobWorkError
                // 同款不吞异常——本域归 TRJ 素材轨，原文入 cause）。
                plannerError = true;
                plannerErrorCause = e.getMessage().getText();
                break;
            } catch (const std::exception& e) {
                plannerError = true;
                plannerErrorCause = e.what();
                break;
            }
            exhaustedRecord.attemptedCandidates = round + 1;

            if (!found || plannedPath.size() < 2) {
                continue;  // 本轮未产出（预算内未找到/退化输出）——下一轮
            }
            // 防御核对：首末点必须与请求端点一致（规划器契约；不一致的
            // 输出按本轮无效丢弃——不进入复核，证据账目照实）。
            const double endpointsEps = tuning.resolution;  // 分辨率量级容差
            const rw::math::Q frontError = plannedPath.front() - request.fromQ;
            const rw::math::Q backError = plannedPath.back() - request.toQ;
            bool endpointsMatch = frontError.norm2() <= endpointsEps
                                  && backError.norm2() <= endpointsEps;
            if (!endpointsMatch) {
                continue;  // 丢弃本轮输出（鲁棒性守卫——不计为候选）
            }

            // 候选复核采样协议：规划器输出路点全量（密度由 extend/edge-
            // resolution 间接决定——进身份）；pathParameters 均匀 s=i/(n-1)
            // （§10.3 等长约定）。
            const std::size_t sampleCount = plannedPath.size();
            std::vector<double> candidateParams(sampleCount);
            for (std::size_t i = 0; i < sampleCount; ++i) {
                candidateParams[i] = sampleCount == 1
                                         ? 0.0
                                         : static_cast<double>(i)
                                               / static_cast<double>(sampleCount - 1);
            }
            const ReviewResult review = reviewPathSequence(
                *request.session, ctx, plannedPath, candidateParams);

            // 候选入册（逐条结果入证据——§10.4"K 与逐条结果入证据"）。
            PathCandidate candidate;
            candidate.candidateIndex = static_cast<std::uint32_t>(result.candidates.size());
            candidate.configurations = plannedPath;
            candidate.pathParameters = candidateParams;
            switch (review.outcome) {
            case ReviewOutcome::Passed:
                candidate.reviewStatus = CandidateReviewStatus::Passed;
                break;
            case ReviewOutcome::Collision:
                candidate.reviewStatus = CandidateReviewStatus::Collision;
                candidate.collisionRecords = review.records;
                exhaustedRecord.candidateRecords.insert(
                    exhaustedRecord.candidateRecords.end(), review.records.begin(),
                    review.records.end());
                break;
            case ReviewOutcome::DataInsufficient:
                candidate.reviewStatus = CandidateReviewStatus::DataInsufficient;
                break;
            }
            result.candidates.push_back(std::move(candidate));
        }
        exhaustedRecord.consumedBudgetS =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - startClock)
                .count();

        // ---------------------------------------------------------------
        // 第六步：结局归类（四态——PlanSearchResult 状态—字段联动契约）。
        // ---------------------------------------------------------------
        if (plannerError) {
            result.status = PlanSearchStatus::PlannerError;
            result.failure = makeAvoidFailure(
                0xFFFFFFFFu, std::string{kTrjNoPath},
                "避障搜索中规划器异常（rw 异常转译登记）: " + plannerErrorCause,
                "检查场景几何与策略配置后重试；若持续出现请登记诊断");
            return result;
        }
        if (canceled) {
            result.status = PlanSearchStatus::Canceled;  // 取消不是错误（UX-03）
            if (!result.candidates.empty()) {
                result.exhausted = exhaustedRecord;  // 已消耗账目如实携带
            }
            return result;
        }
        // 稳定选择序：在通过候选中取最优（四键全序——选择序实现注）。
        std::optional<std::size_t> adopted;
        for (std::size_t i = 0; i < result.candidates.size(); ++i) {
            if (result.candidates[i].reviewStatus != CandidateReviewStatus::Passed) {
                continue;
            }
            if (!adopted.has_value()
                || candidatePreferred(result.candidates[i], result.candidates[*adopted],
                                      request.lowerBoundQ, request.upperBoundQ)) {
                adopted = i;
            }
        }
        if (adopted.has_value()) {
            result.status = PlanSearchStatus::Found;
            result.adoptedIndex = adopted;
            return result;
        }
        // 无通过候选——搜索未果（素材轨，不判不可行）。
        result.status = PlanSearchStatus::BudgetExhausted;
        result.exhausted = exhaustedRecord;
        return result;
    }
};

}  // namespace

// =====================================================================
// 唯一构造入口（§15.5 形态行——makePathPlannerAdapter）
// =====================================================================

std::unique_ptr<IPathPlannerAdapter> makePathPlannerAdapter()
{
    return std::make_unique<PathPlannerAdapterImpl>();
}

// =====================================================================
// §10.4 避障重规划编排（planPtpWithObstacleAvoidance）
// =====================================================================

AvoidancePlanResult planPtpWithObstacleAvoidance(const AvoidancePlanRequest& request,
                                                 IPathPlannerAdapter& adapter)
{
    // ---------------------------------------------------------------
    // 第一步：前置校验（fail-fast——AvoidancePlanRequest 前置逐项；选型
    // 复用适配器同款解析器，保证两层错误面一致）。
    // ---------------------------------------------------------------
    validateConfigPairAndBounds(request.startQ, request.endQ, request.lowerBoundQ,
                                request.upperBoundQ, "trajectory/avoid/config-pair");
    if (!(request.constraint.limitsScaleFactor > 0.0)
        || request.constraint.limitsScaleFactor > 1.0
        || !std::isfinite(request.constraint.limitsScaleFactor)) {
        throw TrajectoryError("trajectory/avoid/constraint",
                              "限值使用比例必须在 (0,1]，实际: "
                                  + std::to_string(request.constraint.limitsScaleFactor));
    }
    if (!(request.constraint.cartesianSampleStep > 0.0)
        || !std::isfinite(request.constraint.cartesianSampleStep)) {
        throw TrajectoryError("trajectory/avoid/constraint",
                              "采样步长必须为有限正数（m），实际: "
                                  + std::to_string(request.constraint.cartesianSampleStep));
    }
    if (!request.tcpRef.isValid()) {
        throw TrajectoryError("trajectory/avoid/tcp-ref",
                              "tcpRef 必须为合法对象身份（§6.3 必填）");
    }
    if (request.endKind == WaypointKind::TaskPoint && !request.sourceTaskPoint.has_value()) {
        throw TrajectoryError("trajectory/avoid/source-task-point",
                              "endKind==TaskPoint 时 sourceTaskPoint 必填（NFR-COR-04）");
    }
    if (request.endKind != WaypointKind::TaskPoint && request.sourceTaskPoint.has_value()) {
        throw TrajectoryError("trajectory/avoid/source-task-point",
                              "endKind 非 TaskPoint 时 sourceTaskPoint 必须为空（防悬空"
                              "附件——身份面同语义同字节纪律）");
    }
    if (request.startKind == WaypointKind::TaskPoint
        && !request.startSourceTaskPoint.has_value()) {
        throw TrajectoryError("trajectory/avoid/start-source-task-point",
                              "startKind==TaskPoint 时 startSourceTaskPoint 必填");
    }
    if (request.startKind != WaypointKind::TaskPoint
        && request.startSourceTaskPoint.has_value()) {
        throw TrajectoryError("trajectory/avoid/start-source-task-point",
                              "startKind 非 TaskPoint 时 startSourceTaskPoint 必须为空");
    }
    // 会话/视图一致性（collisionCheckApplicable 是唯一开关——带会话却
    // 跳过检查（或反之）的双口径在结构上禁止）。
    if (request.constraint.collisionCheckApplicable) {
        if (request.policySession == nullptr || request.workCell == nullptr
            || request.deviceRuntimeName.empty()) {
            throw TrajectoryError("trajectory/avoid/collision-wiring",
                                  "碰撞检查适用时 policy 会话/快照视图/设备名必须"
                                  "齐备（R-5 唯一碰撞权威）");
        }
    } else if (request.policySession != nullptr || request.workCell != nullptr) {
        throw TrajectoryError("trajectory/avoid/collision-wiring",
                              "碰撞检查不适用时不得携带会话/视图（防双口径——唯一"
                              "开关＝constraint.collisionCheckApplicable）");
    }
    if (!(request.timeBudgetS > 0.0) || !std::isfinite(request.timeBudgetS)) {
        throw TrajectoryError("trajectory/avoid/time-budget",
                              "时间预算必须为有限正数（s），实际: "
                                  + std::to_string(request.timeBudgetS));
    }
    if (request.seed == 0) {
        throw TrajectoryError("trajectory/avoid/seed",
                              "规划种子必须为正（0 非法——I-KIN-4 同款拒绝）");
    }
    const PlannerTuning tuning =
        parsePlannerSelection(request.plannerFamilyToken, request.plannerParams);

    AvoidancePlanResult outcome;
    outcome.segment.segmentIndex = request.segmentIndex;
    outcome.segment.spaceType = SegmentSpaceType::JointLinear;
    outcome.segment.tcpRef = request.tcpRef;
    outcome.segment.frameRef = request.frameRef;
    outcome.segment.constraint = request.constraint;
    outcome.segment.sourceTaskPoint = request.sourceTaskPoint;

    // ---------------------------------------------------------------
    // 第二步：取消轮询（任何计算前——取消不是错误，零素材）。
    // ---------------------------------------------------------------
    if (request.cancel && request.cancel()) {
        outcome.status = AvoidanceStatus::Canceled;
        return outcome;
    }

    // ---------------------------------------------------------------
    // 第三步：碰撞检查不适用 → 直连采纳（V13-01 同款唯一开关语义：策略
    // 未启用碰撞＝碰撞检查不在范围，非降级；段约束标记自明）。
    // ---------------------------------------------------------------
    if (!request.constraint.collisionCheckApplicable) {
        outcome.status = AvoidanceStatus::Ok;
        outcome.directPathAdopted = true;
        // 几何构造：两端点路点（JointLinear 逐轴线性语义——§7.3；中间无
        // Via 点）。
        Waypoint start;
        start.kind = request.startKind;
        start.segmentIndex = request.segmentIndex;
        start.q = request.startQ;
        start.sourceTaskPoint = request.startSourceTaskPoint;
        Waypoint end;
        end.kind = request.endKind;
        end.segmentIndex = request.segmentIndex;
        end.q = request.endQ;
        end.sourceTaskPoint = request.sourceTaskPoint;
        outcome.segment.waypoints = {start, end};
        double length = 0.0;
        for (std::size_t i = 0; i < request.startQ.size(); ++i) {
            length += std::abs(request.endQ[i] - request.startQ[i]);
        }
        outcome.segment.pathLengthJoint = length;
        outcome.segment.pathLengthTcp = 0.0;  // FK 派生观察不计算——恒 0 不冒充
        validateTrajectorySegment(outcome.segment);  // 段构造保证的自证
        return outcome;
    }

    // ---------------------------------------------------------------
    // 第四步：直连候选筛查（§10.4 流程图第一分支——按 edge-resolution
    // 均匀采样，一次 PathSequence 查询；与候选复核同一会话通道，AT-19）。
    // ---------------------------------------------------------------
    const AdapterCallContext ctx(&request.cancel);
    const SampledPath direct = sampleStraightPath(request.startQ, request.endQ,
                                                  tuning.resolution);
    const ReviewResult directReview =
        reviewPathSequence(*request.policySession, ctx, direct.configurations,
                           direct.pathParameters);
    if (directReview.outcome == ReviewOutcome::DataInsufficient) {
        // 直连复核不可采信（含取消到达——policy 样本边界取消）→ 不当作
        // 碰撞淘汰（KIN-05）；取消观测直接命中的情形归取消语义。
        if (request.cancel && request.cancel()) {
            outcome.status = AvoidanceStatus::Canceled;
            return outcome;
        }
        // 非取消的不可采信（Failed/EmptyScope/禁用）——归 PlannerError 面
        // （环境类失败素材；绝不当"无碰撞直连"采纳）。
        outcome.status = AvoidanceStatus::PlannerError;
        outcome.failure = makeAvoidFailure(
            request.segmentIndex, std::string{kTrjRecheckDataInsufficient},
            "直连路径筛查不可采信（碰撞评估未产生终态结论——证据缺失不当作"
            "无碰撞，KIN-05）",
            "检查策略启用状态与碰撞几何清单后重试");
        return outcome;
    }

    auto buildSegment = [&](const std::vector<rw::math::Q>& configurations) {
        // 段几何：起点＋中间 Via 点＋终点（避障候选的中间路点为规划器
        // 引入——WaypointKind::Via，非必经状态，C8 语义）。
        outcome.segment.waypoints.clear();
        Waypoint start;
        start.kind = request.startKind;
        start.segmentIndex = request.segmentIndex;
        start.q = request.startQ;
        start.sourceTaskPoint = request.startSourceTaskPoint;
        outcome.segment.waypoints.push_back(start);
        for (std::size_t i = 1; i + 1 < configurations.size(); ++i) {
            Waypoint via;
            via.kind = WaypointKind::Via;
            via.segmentIndex = request.segmentIndex;
            via.q = configurations[i];
            outcome.segment.waypoints.push_back(via);
        }
        Waypoint end;
        end.kind = request.endKind;
        end.segmentIndex = request.segmentIndex;
        end.q = configurations.back();
        end.sourceTaskPoint = request.sourceTaskPoint;
        outcome.segment.waypoints.push_back(end);
        double length = 0.0;
        for (std::size_t i = 1; i < configurations.size(); ++i) {
            for (std::size_t j = 0; j < configurations[i].size(); ++j) {
                length += std::abs(configurations[i][j] - configurations[i - 1][j]);
            }
        }
        outcome.segment.pathLengthJoint = length;
        outcome.segment.pathLengthTcp = 0.0;  // FK 派生观察不计算
    };

    if (directReview.outcome == ReviewOutcome::Passed) {
        // 直连无碰撞 → 采纳为该段路径（§10.4 流程图——不进搜索）。
        // 段几何只落两端点：JointLinear 段的几何＝逐轴线性（§7.3），筛查
        // 用的中间采样点只是碰撞检查的样本（不进段路点——共线冗余点会
        // 污染段结构语义与后续平滑/时间化的输入面）。
        outcome.status = AvoidanceStatus::Ok;
        outcome.directPathAdopted = true;
        buildSegment({direct.configurations.front(), direct.configurations.back()});
        validateTrajectorySegment(outcome.segment);
        return outcome;
    }

    // 直连检出碰撞 → 记录直连淘汰（C8 证据素材——"初始候选路径碰撞"是
    // AT-06 明文反例），进入避障搜索（重规划）。
    if (!directReview.records.empty()) {
        outcome.directCollision = directReview.records.front();  // 首发现（停机样本）
    }

    // ---------------------------------------------------------------
    // 第五步：避障搜索（K 轮候选＋逐条复核＋选择序——全部委托适配器；
    // 请求字段逐项投影——同源校验已在上面完成）。
    // ---------------------------------------------------------------
    PlannerSearchRequest searchRequest;
    searchRequest.fromQ = request.startQ;
    searchRequest.toQ = request.endQ;
    searchRequest.lowerBoundQ = request.lowerBoundQ;
    searchRequest.upperBoundQ = request.upperBoundQ;
    searchRequest.workCell = request.workCell;
    searchRequest.deviceRuntimeName = request.deviceRuntimeName;
    searchRequest.session = request.policySession;
    searchRequest.plannerFamilyToken = request.plannerFamilyToken;
    searchRequest.plannerParams = request.plannerParams;
    // 预算共享：直连筛查消耗未单独记账（两次会话查询，开销相对规划预算
    // 可忽略——账目口径＝搜索预算全归 search，如实登记）。
    searchRequest.timeBudgetS = request.timeBudgetS;
    searchRequest.seed = request.seed;
    searchRequest.cancel = request.cancel;
    const PlanSearchResult searchResult = adapter.search(searchRequest);

    switch (searchResult.status) {
    case PlanSearchStatus::Found: {
        const PathCandidate& adopted = searchResult.candidates[*searchResult.adoptedIndex];
        outcome.status = AvoidanceStatus::Ok;
        outcome.directPathAdopted = false;
        outcome.adoptedCandidateIndex = searchResult.adoptedIndex;
        buildSegment(adopted.configurations);
        validateTrajectorySegment(outcome.segment);  // 段构造保证的自证
        return outcome;
    }
    case PlanSearchStatus::BudgetExhausted:
        // 搜索未果 → SearchExhaustedRecord → DataInsufficient 素材——
        // **不判不可行**（§7.6 行 4/C8；任务级判定归 evidence）。
        outcome.status = AvoidanceStatus::SearchExhausted;
        outcome.searchRecord = searchResult.exhausted;
        outcome.failure = makeAvoidFailure(
            request.segmentIndex, std::string{kTrjNoPath},
            "避障搜索未果（直连路径碰撞且预算内全部候选淘汰——已试候选 "
                + std::to_string(searchResult.exhausted->attemptedCandidates)
                + " 轮；搜索未果不构成任务不可行证明）",
            "调整工况/障碍布局或放宽规划预算后重评；该段以数据不足素材进入"
            "证据汇总");
        return outcome;
    case PlanSearchStatus::PlannerError:
        outcome.status = AvoidanceStatus::PlannerError;
        outcome.failure = searchResult.failure;
        return outcome;
    case PlanSearchStatus::Canceled:
        outcome.status = AvoidanceStatus::Canceled;  // 取消透传——零错误素材
        return outcome;
    }
    // 词表封闭——不可达；fail-fast 兜底（实现缺陷显性化）。
    throw TrajectoryError("trajectory/avoid/status-exhausted",
                          "PlanSearchStatus 词表外取值（实现缺陷）");
}

}  // namespace sdurws::ird::trajectory
