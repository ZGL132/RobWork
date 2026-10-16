/**
 * @file   Envelope.cpp
 * @brief  近似外包络估算（KIN-10）实现——射线边界二分搜索（内锚点指数
 *         探测＋对分收敛）、位置存在性探针（§7.2 存在性口径）、协作
 *         取消传播与结果组装。算法语义与"近似、非精确"的三个数学根源
 *         见 Envelope.hpp 文件头注（本实现注只写落值细节）。
 *
 * 设计依据：
 *   - units/kinematics.md §7.3（KIN-10 边界——估算不作 Must 判定依据）、
 *     §7.2（方向集黄金设施 generateDirections 复用；存在性口径）、
 *     §8.3（取消——协作探针；响应粒度）、§8.4（确定性——seed 唯一
 *     随机来源）、§9.1（非异常出口＋fail-fast 两轨）、§3.4（SI；纯
 *     函数服务）
 *   - REQUIREMENTS KIN-10/AT-26（原文见 Envelope.hpp 文件头注）
 *   - 任务契约 tasks/foundation/WP-15-T15.json acceptance 1~4
 *
 * 线程安全：纯函数、无共享可变状态（§3.4）；单线程顺序执行。
 * 确定性：探针种子＝query.seed 的 splitmix64 派生（混入全局探测序号
 * ——方向序/锚点序/二分序的单调计数），同输入同初值序列（NFR-COR-01）。
 */

#include <sdurws/ird/kinematics/Envelope.hpp>

#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <rw/math/Transform3D.hpp>

#include <sdurws/ird/kinematics/Bounds.hpp>     // computeAnalyticReachBound（外括号）
#include <sdurws/ird/kinematics/Sampling.hpp>   // generateDirections（§7.2 黄金设施）

namespace sdurws::ird::kinematics {

namespace {

// =====================================================================
// 局部常量与工具（TU 内——唯一书写点在头文件的禁止面之外的实现细节）
// =====================================================================

/// 内锚点指数探测的最小距离下限（m）：t_k＝Rup·2⁻ᵏ 衰减到该值以下即停
/// ——浮点下溢防护（Rup 极小时 2⁻ᵏ 可能次正规化）；该量级（<1e-12 m）
/// 远低于任何物理容差，判"无可达样本"不损失实际分辨率。
constexpr double kAnchorMinDistance = 1e-12;

/// splitmix64（确定性伪随机序列源——§3.4；Ik.cpp/Sampling.cpp 同款
/// 自持副本：算法公开定版、跨平台位级一致，TU 局部先例）。
std::uint64_t splitmix64(std::uint64_t& state)
{
    state += 0x9E3779B97F4A7C15ull;
    std::uint64_t z = state;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

/// 探针初值种子派生（确定性——同 (seed, probeSerial) 同初值集）：状态
/// ＝seed·2³²＋probeSerial（Ik.cpp 初值派生"seed·2³²＋k"同构形态——
/// probeSerial 是全计算单调计数，天然覆盖方向/锚点/二分三维序），再经
/// 一轮 splitmix64 去相关后作为该探针的 makeInitialValues 种子。
std::uint64_t probeSeedFor(std::uint64_t seed, std::uint64_t probeSerial)
{
    std::uint64_t state = seed * 0x100000000ull + probeSerial;
    return splitmix64(state);
}

/// 调用方契约违约 fail-fast（§9.1——std::invalid_argument，不进结果对象）。
[[noreturn]] void failCaller(const std::string& what)
{
    throw std::invalid_argument("kin-envelope：" + what);
}

// =====================================================================
// 位置存在性探针（§7.2 存在性口径的射线版——"存在 ≥1 有效解即达"）
// =====================================================================

/**
 * @brief 单探针判定词四值（射线搜索的内部判别词——不进结果值面）。
 */
enum class ProbeVerdict : std::uint8_t {
    /// 存在 ≥1 通过全部硬过滤的解（结局 1/4——存在性凭据，可达侧）。
    Reached,
    /// 未找到解（结局 2/3/5——不可达侧使用；注意：搜索未果≠确定性
    /// 不可达（C5/C8），此处只作二分的方向判定——近似根源 2）。
    NotFound,
    /// 协作取消（§9.2/D-KIN-9——非终局，立即停止全部后续探测）。
    Cancelled,
    /// 求解设施失败（F-746③ 同族非终局——视图面失效，停止全部）。
    Facility,
};

/**
 * @brief 在基座系 {B} 的 p＝t·d 处做一次位置存在性 IK 探测。
 *
 * 请求组装口径（与 Evaluators.cpp 区域覆盖通道"位置样本＝位置存在性
 * IK"逐项同源，KIN-P2/A02 任务维度语义）：目标姿态＝恒等（仅作 IK 请求
 * 形态满足——PositionOnly 任务不消费姿态误差）、taskSpace=PositionOnly
 * （3 维任务子空间——姿态项不进目标函数，收敛判据只查位置残差；姿态
 * 容差透传查询值仅作请求合法域载体）；碰撞会话恒空（KIN-10 无
 * 碰撞要求——纯运动学可达边界，EnvelopeQuery 结构体注）；取消探针
 * 原样透传（求解器内部 kIkCancellationProbeInterval 迭代粒度查询）。
 *
 * 计数纪律（守恒不变式的落点）：probeSerial/rayProbeCount 只在本函数
 * **实际发起 IK 调用**时推进——入口取消检查命中（未发起调用）两者皆
 * 不动；求解器内部取消/设施失败返回时调用已发起（计数照记，预算素材
 * 如实）。守恒式 ΣrayProbeCount＝probeSerial 由 computeApproximate-
 * Envelope 出口复核（违例 logic_error——实现缺陷不静默）。
 *
 * @param t          [in] 探测距离（m；≥0）
 * @param direction  [in] 单位方向（基座系 {B}，‖d‖=1）
 * @param view       [in] 只读模型视图
 * @param tcp        [in] TCP 引用
 * @param query      [in] 计算查询（求解配置面）
 * @param intervals  [in] 逐自由度评价区间（evaluationIntervals 预产）
 * @param probeSerial [in,out] 全局探针单调计数（初值种子派生输入；本
 *                     调用发起 IK 时递增）
 * @param rayProbeCount [in,out] 所属方向的探测调用计数（同上同步递增）
 * @param facilityOut  [out] 设施失败素材转载（可空＝调用方不收；判定为
 *                     Facility 时写入底层 outcome 的 IkFacilityFailure
 *                     原值〔stage/detail 逐字转载——素材不加工，诊断链
 *                     溯源保留求解器侧定位〕）
 * @return 探针判定（四值——枚举注）
 *
 * 纯函数（除两个计数器推进）；确定性（同序号同初值集——NFR-COR-01）。
 */
ProbeVerdict probeReachability(double t,
                               const rw::math::Vector3D<double>& direction,
                               const IKinRuntimeView& view,
                               const TcpRef& tcp,
                               const EnvelopeQuery& query,
                               const std::vector<JointInterval>& intervals,
                               std::uint64_t& probeSerial,
                               std::uint32_t& rayProbeCount,
                               IkFacilityFailure* facilityOut)
{
    // 探针级取消检查（响应粒度第一层——每探针至少一次，§8.3 落值；
    // 求解器内部的迭代级查询是第二层，经 cancellationProbe 透传生效）。
    // 命中即返回且**不计数**——零 IK 调用，预算素材不虚增。
    if (query.cancellationProbe && query.cancellationProbe()) {
        return ProbeVerdict::Cancelled;
    }

    // 初值集（确定性——本探针的序号派生种子；SeededRandom 下多初值
    // 提升边界附近〔雅可比趋奇异〕的收敛稳健性）。
    const std::vector<std::vector<double>> initialValues = makeInitialValues(
        query.initialStrategy, query.initialValuesCount,
        probeSeedFor(query.seed, probeSerial), intervals, query.referenceQ);
    ++probeSerial;    // 全局序号推进（与本次 IK 调用一一对应）
    ++rayProbeCount;  // 方向面计数同步（守恒式右端/左端同步推进）

    // IK 请求（位置存在性——KIN-P2/A02 起 taskSpace=PositionOnly：3 维
    // 任务子空间，姿态误差不进目标函数，收敛判据只查位置残差；姿态容差
    // 在该任务下不被消费，保持 IkRequest 默认合法值（1e-6 rad>0——请求
    // 合法域载体；EnvelopeQuery 投影面刻意不含姿态容差字段——"不预建无
    // 消费者条目"纪律，与旧 π 落值常量的裁剪同因）。旧"姿态无约束落值
    // π"机制废除——kEnvelopePoseFreeTolerance 已随本迁移移除，π 方案下
    // 姿态仍参与 6 维迭代会把位置可达探针点牵制出局〔审查 A02 同源〕。
    // 身份面回填调用方绑定）。
    IkRequest request;
    request.targetInBase = rw::math::Transform3D<double>(
        direction * t, rw::math::Rotation3D<double>(1, 0, 0, 0, 1, 0, 0, 0, 1));
    request.positionTolerance = query.positionTolerance;
    request.taskSpace = IkTaskSpace::PositionOnly;
    request.modelView = &view;
    request.tcp = tcp;
    request.initialValues = initialValues;
    request.iterationLimit = query.iterationLimit;
    request.intervals = intervals;
    request.dedupThresholdPerAxis = query.dedupThresholdPerAxis;
    request.collisionSession = nullptr;  // KIN-10 无碰撞要求（Query 注）——
                                         // 求解器按"未启用"跳过硬过滤③
    request.cancellationProbe = query.cancellationProbe;
    request.referenceQ = query.referenceQ;
    // 结果绑定（纯服务直调面——pointOid 保留值合法；身份 seed/referenceQ
    // 已由调用方经 query.identity 回填一致性由组装面保证）。
    request.requestIdentity = query.identity;
    request.requestIdentity.seed = query.seed;
    request.requestIdentity.referenceQ = query.referenceQ;

    const IkOutcome outcome = query.solver ? query.solver->solve(request)
                                           : IkSolver{}.solve(request);

    // 判读（按序——cancelled/设施失败优先，非终局形态上调）。
    if (outcome.cancelled) {
        return ProbeVerdict::Cancelled;
    }
    if (outcome.facilityFailure.has_value()) {
        // 素材转载（不加工——底层 stage/detail 逐字上交，诊断链溯源保留
        // 求解器侧定位；Sampling.cpp 设施失败 reason 转载 detail 同款）。
        if (facilityOut != nullptr) {
            *facilityOut = *outcome.facilityFailure;
        }
        return ProbeVerdict::Facility;
    }
    switch (outcome.outcomeKind) {
    case IkOutcomeKind::SolutionsFound:
    case IkOutcomeKind::PartialCollision:
        return ProbeVerdict::Reached;
    case IkOutcomeKind::MultiInitNoConvergence:
    case IkOutcomeKind::AllCandidatesFiltered:
    case IkOutcomeKind::AnalyticBoundExceeded:
        // 2/3＝搜索未果（近似根源 2——当"不可达侧"使用）；5＝解析界限
        // 超界，理论上仅在 ‖t·d‖>Rup 时出现，而本算法恒在 [0,Rup] 内
        // 探测——若出现（数值边界）同轨按"不可达侧"处置，注释如实。
        return ProbeVerdict::NotFound;
    }
    return ProbeVerdict::NotFound;  // 恒不可达（switch 全枚举兜底——编译器告警面）
}

}  // namespace

// =====================================================================
// computeApproximateEnvelope——主流程（头注算法四步的落值）
// =====================================================================

Expected<EnvelopeResult> computeApproximateEnvelope(const IKinRuntimeView& view,
                                                    const TcpRef& tcp,
                                                    const EnvelopeQuery& query)
{
    // ---- 第 0 步：装配校验（fail-fast 轨——调用方错误，不进结果对象；
    // 逐项独立可辨——消息定位字段，NFR-COR-03 拒绝不钳制）。----
    // 设备自由度（可动关节链序——validateRegionCoverageQuery 同口径）。
    std::size_t dof = 0;
    for (const auto& j : view.model().chain().joints) {
        if (j.type != runtime::JointType::Fixed) {
            ++dof;
        }
    }
    if (query.directionCount == 0U) {
        failCaller("query.directionCount=0（方向数须 ≥1——KIN-10 默认 "
                   "180）");
    }
    if (query.anchorProbesMax == 0U) {
        failCaller("query.anchorProbesMax=0（内锚点探测步数须 ≥1）");
    }
    if (query.bisectIterationsMax == 0U) {
        failCaller("query.bisectIterationsMax=0（二分迭代上限须 ≥1）");
    }
    if (query.seed == 0U) {
        failCaller("query.seed=0（I-KIN-4：拒绝，不做 0→1 静默替换——"
                   "NFR-COR-03；探针初值序列的唯一来源）");
    }
    if (query.initialValuesCount == 0U) {
        failCaller("query.initialValuesCount=0（初值数量须 ≥1）");
    }
    if (query.iterationLimit == 0U) {
        failCaller("query.iterationLimit=0（迭代上限须 ≥1）");
    }
    if (!std::isfinite(query.positionTolerance) || query.positionTolerance <= 0.0) {
        failCaller("query.positionTolerance 非法（须有限且>0，单位 m）");
    }
    if (!std::isfinite(query.dedupThresholdPerAxis)
        || query.dedupThresholdPerAxis <= 0.0) {
        failCaller("query.dedupThresholdPerAxis 非法（须有限且>0，rad|m "
                   "逐轴）");
    }
    if (query.referenceQ.size() != dof) {
        failCaller("query.referenceQ 维度 " + std::to_string(query.referenceQ.size())
                   + " != 设备自由度 " + std::to_string(dof)
                   + "（D-KIN-4 显式输入契约）");
    }
    for (std::size_t k = 0; k < query.referenceQ.size(); ++k) {
        if (!std::isfinite(query.referenceQ[k])) {
            failCaller("query.referenceQ[" + std::to_string(k)
                       + "] 非有限（NFR-COR-03：不置零）");
        }
    }

    // ---- 第 1 步：解析外括号（确定性上界——三角形不等式覆盖全部关节
    // 角；NoDevice/NoTcp/FrameUnresolved 走 Expected 值面，环境/快照
    // 事实不属调用方违约）。----
    const Expected<AnalyticReachBound> bound =
        computeAnalyticReachBound(view, tcp);
    if (!bound.ok()) {
        return Expected<EnvelopeResult>::err(bound.error());
    }
    const double reachUpper = bound.get().totalRadius;  // m（Rup——外括号）

    // ---- 结果骨架（恒近似标注——KIN-10/AT-26 的类型化落点；身份回填
    // 中 seed/referenceQ 由实现从 query 覆写——防调用方两处不一致）。----
    EnvelopeResult result;
    result.precision = EnvelopePrecision::Approximate;
    result.contractVersion = kEnvelopeContractVersion;
    result.directionCount = query.directionCount;
    result.reachUpperBound = reachUpper;
    result.identity = query.identity;
    result.identity.seed = query.seed;
    result.identity.referenceQ = query.referenceQ;

    // 方向集（§7.2 黄金设施复用——斐波那契螺旋，黄金锁定；与 seed 无关
    // ——方向离散是配置参数不是随机量）。
    const std::vector<rw::math::Vector3D<double>> directions =
        generateDirections(query.directionCount);

    // 逐自由度评价区间（evaluationIntervals 唯一实现点——初值生成与
    // 限位评价同源，防两套真值；每计算一次，逐探针复用）。
    const std::vector<JointInterval> intervals = evaluationIntervals(view);

    // 射线记录预置（方向生成序；NotRun 初值——取消/设施失败路径的
    // 未开始方向即此初值的自然保留，RegionCoverage 槽位预置同款）。
    result.rays.resize(query.directionCount);
    for (std::uint32_t j = 0; j < query.directionCount; ++j) {
        result.rays[j].directionIndex = j;
        result.rays[j].direction = directions[j];
    }

    // ---- 第 2 步：入口取消检查（取消已请求→零探测返回；全部方向
    // NotRun——D-KIN-9 取消不是结局，partial 如实标记）。----
    if (query.cancellationProbe && query.cancellationProbe()) {
        result.cancelled = true;
        for (EnvelopeRayRecord& ray : result.rays) {
            ray.reason = "协作取消（入口检查——零探测返回；重跑同 seed 可复算）";
        }
        return Expected<EnvelopeResult>::ok(std::move(result));
    }

    // ---- 第 3 步：逐方向射线边界（内锚点指数探测＋二分收敛——头注
    // 算法 2/3/4 步；单线程顺序执行，方向序＝生成序）。----
    std::uint64_t probeSerial = 0;   // 全局探针单调计数（种子派生输入）
    bool stopAll = false;            // 取消/设施失败后的全局停止旗标
    IkFacilityFailure facility;      // 设施失败素材（转载自底层求解 outcome）

    for (std::uint32_t j = 0; j < query.directionCount && !stopAll; ++j) {
        EnvelopeRayRecord& ray = result.rays[j];

        // ---- 3a. 内锚点探测（t_k＝Rup·2⁻ᵏ——环带工作空间的内空洞
        // 在此步跨越；k 从 1 起：t₀＝Rup 是解析已证不可达点，不探测）。
        // 步数耗尽未命中→近似空结论（rMax=0，Computed）——文件头注
        // 近似根源 2 的直接体现。----
        bool anchored = false;
        double lo = 0.0;  // 已证实可达的最远距离（m；锚点/二分推进）
        for (std::uint32_t k = 1; k <= query.anchorProbesMax; ++k) {
            const double t = reachUpper * std::ldexp(1.0, -static_cast<int>(k));
            if (t < kAnchorMinDistance) {
                break;  // 浮点下限防护——该量级低于任何物理分辨率
            }
            const ProbeVerdict verdict =
                probeReachability(t, directions[j], view, tcp, query,
                                  intervals, probeSerial, ray.probeCount,
                                  &facility);
            if (verdict == ProbeVerdict::Reached) {
                lo = t;
                anchored = true;
                break;
            }
            if (verdict == ProbeVerdict::Cancelled) {
                // 取消传播（D-KIN-9）：本方向标记后停止全部后续方向。
                ray.state = EnvelopeRayState::NotRun;
                ray.reason = "协作取消（内锚点探测中断）";
                result.cancelled = true;
                stopAll = true;
                break;
            }
            if (verdict == ProbeVerdict::Facility) {
                // 设施失败（F-746③ 同族非终局）：停止全部，剩余方向
                // NotRun 如实标记；facility 已由探针转载底层 outcome
                // 素材（stage/detail 原值——不加工）。
                ray.state = EnvelopeRayState::NotRun;
                ray.reason = "求解设施失败（内锚点探测中断——该方向证据缺失）";
                result.facilityFailure = facility;
                stopAll = true;
                break;
            }
            // NotFound：继续内缩探测（下一 k）。
        }

        if (stopAll) {
            break;
        }

        if (!anchored) {
            // 近似空结论：探测分辨率内未发现可达样本（含平面臂/受限
            // 锥等该方向天然不可达形态）——rMax=0、Computed、零额外
            // 探测。这不构成"该方向确定性不可达"的工程结论（近似根源
            // 2；§7.3 禁令随 precision/note 随行）。
            ray.state = EnvelopeRayState::Computed;
            ray.rMax = 0.0;
            continue;
        }

        // ---- 3b. 二分收敛（(lo, Rup] 上对分——mid 可达则外推 lo、
        // 不可达则内缩 hi；hi=Rup 初值是解析已证不可达点，免探测。
        // 终止＝区间宽 ≤ positionTolerance（物理分辨率早停）或迭代数
        // 耗尽（精度兜底）。rMax＝lo——"已证实可达的最远距离"下估计。----
        double hi = reachUpper;
        for (std::uint32_t it = 0;
             it < query.bisectIterationsMax && (hi - lo) > query.positionTolerance;
             ++it) {
            const double mid = lo + (hi - lo) / 2.0;  // 中点（不越界形态）
            const ProbeVerdict verdict =
                probeReachability(mid, directions[j], view, tcp, query,
                                  intervals, probeSerial, ray.probeCount,
                                  &facility);
            if (verdict == ProbeVerdict::Reached) {
                lo = mid;  // 可达侧外推
                continue;
            }
            if (verdict == ProbeVerdict::Cancelled) {
                ray.state = EnvelopeRayState::NotRun;
                ray.reason = "协作取消（二分中断）";
                result.cancelled = true;
                stopAll = true;
                break;
            }
            if (verdict == ProbeVerdict::Facility) {
                ray.state = EnvelopeRayState::NotRun;
                ray.reason = "求解设施失败（二分中断——该方向证据缺失）";
                result.facilityFailure = facility;
                stopAll = true;
                break;
            }
            hi = mid;  // 不可达侧内缩（含搜索未果——近似根源 2）
        }

        if (stopAll) {
            break;
        }

        // 方向完成：rMax＝已证实可达的最远距离（下估计——非精确边界，
        // 标注随 result.precision/kEnvelopeApproximationNote 随行）。
        ray.state = EnvelopeRayState::Computed;
        ray.rMax = lo;
    }

    // ---- 第 4 步：停止后的剩余方向收尾（取消/设施失败路径——未开始
    // 方向保持 NotRun 初值，reason 补非空文案——ERR-01）。----
    if (stopAll) {
        for (std::uint32_t j = 0; j < query.directionCount; ++j) {
            EnvelopeRayRecord& ray = result.rays[j];
            if (ray.state != EnvelopeRayState::NotRun) {
                continue;  // 已完成/已标记方向如实保留
            }
            if (ray.reason.empty()) {
                ray.reason = result.facilityFailure.has_value()
                                 ? "求解设施失败（未开始——partial 如实标记）"
                                 : "协作取消（未开始——partial 如实标记）";
            }
        }
    }

    // 探测总数（逐方向之和——守恒自检：实现缺陷即 logic_error，不静默）。
    for (const EnvelopeRayRecord& ray : result.rays) {
        result.probeCount += ray.probeCount;
    }
    if (result.probeCount != probeSerial) {
        throw std::logic_error(
            "kin-envelope：探测总数与探针序号计数不一致（实现缺陷——"
            "不静默）；probeCount=" + std::to_string(result.probeCount)
            + " serial=" + std::to_string(probeSerial));
    }

    return Expected<EnvelopeResult>::ok(std::move(result));
}

}  // namespace sdurws::ird::kinematics
