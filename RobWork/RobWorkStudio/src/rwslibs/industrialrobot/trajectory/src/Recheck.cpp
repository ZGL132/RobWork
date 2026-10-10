/**
 * @file   Recheck.cpp
 * @brief  TRJ-04 复检协议的唯一实现翻译单元（§11.1 协议＋§15.6 管线；
 *         WP-16-T07 批——复检半区）。
 *
 * 设计依据（头文件 Recheck.hpp 的设计依据此处不重复；本注登记实现语义）：
 *   - **细分协议实现口径（DTB §5.4 实现补全登记，单元卡 §1.2 T07 注同步）**：
 *     ①初始子段集＝相邻必检参数之间的区间（必检参数＝段端点＋路点——
 *     R2①"端点复检（各段端点/路点）"的字面承载；端点必含同时满足 P-06
 *     行 9"段端点必含"）；②细分＝逐层二分：每层对全部未满足双上界的子段
 *     同时二分（广度优先——层账目与 P-06 行 7"最大层数（二分）"一致）；
 *     ③预算双上界：细分次数 ≤params.maxSubdivisionDepth 且总子段数（含
 *     初始）≤params.maxSubsegments——任一耗尽仍有子段不满足即 Budget-
 *     Exhausted（P-06 行 7/8 两值对单初始子段一致：2¹⁰=1024）；④"双上界
 *     同时满足"的取严语义：关节界（逐轴按轴型步长）与笛卡尔界（代表点集
 *     最大位移）任一不满足即继续细分——两界同时满足的子段才通过（TRJ-04
 *     R9 原文）；⑤预算耗尽分支不执行碰撞查询：验证覆盖已判不充分（R9
 *     "不得默认接受未充分验证段"），碰撞查询对不充分覆盖的采样集无意义，
 *     且结论已定——budgetExhausted 分支的结论在细分终止时即产出。
 *   - **policy 评估的三态映射（§10.3"finalized=false 不得采信"）**：
 *     Completed＋Applicable＋finalized＋零 Collision 发现 → 通过半区；
 *     Completed＋Applicable＋finalized＋含 Collision 发现 → Collision；
 *     Canceled → 顶层 Canceled（取消不是错误——UX-03；policy 侧取消＝
 *     调用方取消请求穿透，本域取消语义收尾）；其余一切（Failed/EmptyScope/
 *     DisabledByPolicy）→ DataInsufficient（EvidenceUnavailable——KIN-05：
 *     验证器缺失绝不视为无碰撞）。与 T06 reviewPathSequence 的差异（那里
 *     policy Canceled 归 DataInsufficient）：复检面有顶层取消态可精确区分
 *     ——规划器约束内部无法上抛取消只能保守；登记为两处语义差异的诚实
 *     说明（非矛盾——场景不同）。
 *   - 需求 TRJ-04/AT-06、NFR-COR-02（细分计划确定性——子段集合按区间
 *     升序的确定遍历序）、KIN-05（覆盖缺口显式化）。
 *
 * 集成模式条件源（Ptp.cpp 同款 gating）：Q 构造面消费 rw::math::Q 的库内
 * 虚析构符号、policy 会话 evaluate 消费框架符号——冒烟模式无框架库可链，
 * 不编译本 TU；Recheck.hpp 的值类型/inline 函数两模式皆可编译。
 */

#include <sdurws/ird/trajectory/Recheck.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

#include <sdurws/ird/policy/CollisionEvaluator.hpp>  // 会话完整类型（evaluate 消费）
#include <sdurws/ird/policy/CollisionQuery.hpp>      // 查询/输出类型（§6.2 唯一权威）
#include <sdurws/ird/policy/Contexts.hpp>            // IPolicyCallContext（取消/存活适配）
#include <sdurws/ird/trajectory/DiagCodes.hpp>       // TRJ-* 码常量（素材唯一书写点）
#include <sdurws/ird/trajectory/Errors.hpp>          // TrajectoryError（fail-fast 载体）

namespace sdurws::ird::trajectory {
namespace {

// =====================================================================
// policy 调用上下文（Planner.cpp AdapterCallContext 同款最小适配——
// 恒存活＋取消委托；复检面独立持有，不跨 TU 复用文件私有设施）
// =====================================================================

/**
 * @brief policy evaluate 的调用上下文（IPolicyCallContext 最小适配——
 *        恒存活＋取消转发）。
 *
 * 恒存活的依据与 Planner.cpp 同款：本域的会话与请求同生命周期（复检请求
 * 持有会话 shared_ptr——调用期必然有效），迟到调用场景在评估器装配面被
 * 结构排除（每任务每实例——policy §6.3 线程行）。取消逐次转发调用方的
 * CancelSignal——policy evaluate 在样本边界轮询（§6.6 时序），取消可穿透
 * 到 PathSequence 逐样本循环内部。
 */
class RecheckCallContext final : public policy::IPolicyCallContext {
public:
    /// 构造：绑定取消信号源（借用——调用期存活；可空＝不可取消）。
    explicit RecheckCallContext(const CancelSignal* cancel) : m_cancel(cancel) {}

    /// 委托调用方取消信号（双重判空——空 function＝"不可取消"而非"请求
    /// 取消"，std::bad_function_call 防线）。
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
// 子段（细分协议的工作单元——值面，确定性遍历）
// =====================================================================

/**
 * @brief 一个细分子段（参数区间 [low, high]⊆[0,1]）。
 *
 * 遍历序契约：子段集合按 (low, high) 字典序升序维护——细分计划确定性的
 * 结构基础（同一几何+同一参数必得同序采样集，NFR-COR-02）。
 */
struct SubSegment {
    /// 段内参数下界（∈[0,1]，无量纲）。
    double low = 0.0;
    /// 段内参数上界（∈[0,1]；low<high——零长子段非法）。
    double high = 0.0;

    bool operator<(const SubSegment& o) const
    {
        return low != o.low ? low < o.low : high < o.high;
    }
};

// =====================================================================
// 校验（请求前置——fail-fast 面）
// =====================================================================

/**
 * @brief 请求前置校验（RecheckRequest 注的逐项落点；违约抛 TrajectoryError
 *        ——调用方契约违约 fail-fast，token "trajectory/recheck/..."）。
 *
 * 校验序固定（确定性——同一坏请求必报同一首错，NFR-COR-02）：
 *   1. path/sampler 非空；2. session 非空；3. params 过 validate；
 *   4. 必检参数 ≥2 个、严格升序、首 0.0 末 1.0（位级——端点必含）；
 *   5. jointTypes 与几何维度一致（s=0 采样）；6. 限位区间同维度且逐轴
 *   lower<upper。
 */
void validateRecheckRequest(const RecheckRequest& request)
{
    if (request.path == nullptr) {
        throw TrajectoryError("trajectory/recheck/path",
                              "被检段几何缺失（path 为空——平滑产物或折线求值器必填）");
    }
    if (request.sampler == nullptr) {
        throw TrajectoryError("trajectory/recheck/sampler",
                              "代表点集采样器缺失（P-06 行 9 生成规则的装配面物化"
                              "必填——R9 笛卡尔步长的度量对象）");
    }
    if (request.session == nullptr) {
        throw TrajectoryError("trajectory/recheck/session",
                              "policy 碰撞会话缺失（复检协议＝碰撞验证协议——唯一"
                              "碰撞权威必填，R-5）");
    }
    validateRecheckParameters(request.params);

    const std::vector<double>& mp = request.mandatoryParameters;
    if (mp.size() < 2) {
        throw TrajectoryError("trajectory/recheck/mandatory",
                              "必检参数至少 2 个（段两端点必含），实际: "
                                  + std::to_string(mp.size()));
    }
    if (mp.front() != 0.0 || mp.back() != 1.0) {
        throw TrajectoryError("trajectory/recheck/mandatory",
                              "必检参数必须以 0.0 起、1.0 止（段端点必含——R2①/"
                              "P-06 行 9），实际首尾: " + std::to_string(mp.front())
                                  + " / " + std::to_string(mp.back()));
    }
    if (!std::isfinite(mp.front()) || !std::isfinite(mp.back())) {
        throw TrajectoryError("trajectory/recheck/mandatory",
                              "必检参数含非有限值（端点）");
    }
    for (std::size_t i = 1; i < mp.size(); ++i) {
        if (!(mp[i] > mp[i - 1])) {
            throw TrajectoryError("trajectory/recheck/mandatory",
                                  "必检参数必须严格升序，位置 " + std::to_string(i)
                                      + " 实际: " + std::to_string(mp[i - 1]) + " → "
                                      + std::to_string(mp[i]));
        }
        if (!std::isfinite(mp[i])) {
            throw TrajectoryError("trajectory/recheck/mandatory",
                                  "必检参数含非有限值，位置 " + std::to_string(i));
        }
    }

    // 维度一致性（以几何 s=0 采样为基准——类型表/限位区间逐一对齐）。
    const rw::math::Q startQ = request.path->sampleAt(0.0);
    const std::size_t dof = startQ.size();
    if (request.jointTypes.size() != dof) {
        throw TrajectoryError("trajectory/recheck/joint-types",
                              "关节类型表维度与被检几何不一致（几何 " + std::to_string(dof)
                                  + " 轴，类型表 " + std::to_string(request.jointTypes.size())
                                  + " 轴）");
    }
    if (request.lowerBoundQ.size() != dof || request.upperBoundQ.size() != dof) {
        throw TrajectoryError("trajectory/recheck/bounds",
                              "限位区间维度与被检几何不一致（几何 " + std::to_string(dof)
                                  + " 轴）");
    }
    for (std::size_t j = 0; j < dof; ++j) {
        if (!(request.lowerBoundQ[j] < request.upperBoundQ[j])) {
            throw TrajectoryError("trajectory/recheck/bounds",
                                  "限位区间逐轴必须 lower<upper，轴 " + std::to_string(j)
                                      + " 实际: [" + std::to_string(request.lowerBoundQ[j])
                                      + ", " + std::to_string(request.upperBoundQ[j]) + "]");
        }
    }
}

// =====================================================================
// 失败素材构造（TRJ-06 定位载体；phase 恒 kPhaseRecheck——本批词表第五值）
// =====================================================================

/**
 * @brief 构造复检结论级素材（ERR-01 字段齐备：中文原因与建议动作必填；
 *        reasonToken 恒取 DiagCodes 在册常量）。
 */
FailedSegmentRecord makeRecheckFailure(std::uint32_t segmentIndex,
                                       const std::string& reasonToken,
                                       const std::string& cause,
                                       const std::string& recommendedAction)
{
    FailedSegmentRecord record;
    record.segmentIndex = segmentIndex;
    record.phaseToken = kPhaseRecheck;
    record.reasonToken = reasonToken;
    record.cause = cause;
    record.recommendedAction = recommendedAction;
    return record;
}

// =====================================================================
// 细分协议核心——子段度量与双上界判定（R9）
// =====================================================================

/**
 * @brief 单子段的双上界度量结果（一次评估的实测面）。
 */
struct SegmentMeasure {
    /// 关节维逐轴最大原始位移（rad|m 逐轴混合计量——|Δq_j| 的最大值；判
    /// 定按轴型分别取限，账目取原始最大）。
    double jointMaxRaw = 0.0;
    /// 关节界是否满足（逐轴 |Δq_j| ≤ 对应轴型步长——P-06 行 1/2）。
    bool jointOk = false;
    /// 笛卡尔维代表点集最大位移（m——R9 度量口径）。
    double cartesianMax = 0.0;
    /// 笛卡尔界是否满足（≤ 生效笛卡尔步长——P-06 行 5）。
    bool cartesianOk = false;
    /// 双上界同时满足（取严者生效——TRJ-04 R9 原文）。
    bool bothOk() const { return jointOk && cartesianOk; }
};

/**
 * @brief 度量单子段（细分协议的原子判定——双上界取严）。
 *
 * 度量协议（确定性——同一子段同输入必得同判定，NFR-COR-02）：
 *   1. 取两端构型（被检几何求值——s=low/high）；
 *   2. 关节界：逐轴 |Δq_j| 与对应轴型步长比较（P-06 行 1/2——旋转 rad／
 *      移动 m）；记录逐轴最大原始位移（账目面）；
 *   3. 笛卡尔界：两端构型各求代表点集（采样器——P-06 行 9 规则的装配面
 *      物化），逐点欧氏距离的最大值（点数漂移＝装配面契约违约——
 *      fail-fast，义务面见采样器接口注）。
 */
SegmentMeasure measureSubSegment(const RecheckRequest& request,
                                 const SubSegment& span)
{
    SegmentMeasure m;
    const rw::math::Q qa = request.path->sampleAt(span.low);
    const rw::math::Q qb = request.path->sampleAt(span.high);

    // 第 2 步：关节界（逐轴按轴型取步长——P-06 行 1/2 分类）。
    m.jointOk = true;
    for (std::size_t j = 0; j < qa.size(); ++j) {
        const double step = std::abs(qb[j] - qa[j]);  // 转动轴 rad ｜ 移动轴 m
        m.jointMaxRaw = std::max(m.jointMaxRaw, step);
        const double limit = (request.jointTypes[j] == JointTypeKind::Revolute)
                                 ? request.params.maxJointStepRevolute
                                 : request.params.maxJointStepPrismatic;
        if (step > limit) {
            m.jointOk = false;  // 该轴超步长——关节界不满足（扫完取账目）
        }
    }

    // 第 3 步：笛卡尔界（代表点集最大位移——R9；TCP＋全部参与碰撞验证
    // 几何的代表点，基座系 {B}）。
    const std::vector<rw::math::Vector3D<double>> pointsA = request.sampler->sample(qa);
    const std::vector<rw::math::Vector3D<double>> pointsB = request.sampler->sample(qb);
    if (pointsA.empty() || pointsA.size() != pointsB.size()) {
        // 点集空/点数漂移＝装配面契约违约（采样器接口注的义务面）——
        // fail-fast 而非判 DataInsufficient：程序缺陷不是用户数据结局。
        throw TrajectoryError("trajectory/recheck/sampler-contract",
                              "代表点集采样器契约违约（点集空或逐构型点数漂移"
                              "——等长契约见采样器接口注），点数: "
                                  + std::to_string(pointsA.size()) + " vs "
                                  + std::to_string(pointsB.size()));
    }
    m.cartesianOk = true;
    for (std::size_t p = 0; p < pointsA.size(); ++p) {
        const double disp = (pointsB[p] - pointsA[p]).norm2();  // m——逐点位移
        m.cartesianMax = std::max(m.cartesianMax, disp);
        if (disp > request.params.maxCartesianStep) {
            m.cartesianOk = false;  // 超笛卡尔步长（扫完取账目）
        }
    }
    return m;
}

/**
 * @brief 轴型对应的比较型素材单位 token（rad 对旋转轴、m 对移动轴——
 *        core 注册表编译期冻结；find 失败属环境缺陷 fail-fast）。
 */
core::UnitToken axisUnitToken(JointTypeKind kind)
{
    const char* name = (kind == JointTypeKind::Revolute) ? "rad" : "m";
    auto token = core::UnitToken::find(name);
    if (!token.has_value()) {
        throw TrajectoryError("trajectory/recheck/unit-token",
                              "core 单位注册表缺少 token（环境缺陷，fail-fast——"
                              "比较型素材单位口径不可用）: "
                                  + std::string{name});
    }
    return *token;
}

}  // namespace

// =====================================================================
// §15.6 复检管线主流程
// =====================================================================

RecheckOutcome recheckSegment(const RecheckRequest& request)
{
    // ---- 第 1 步：前置校验（调用方契约违约 fail-fast；含 params 校验
    // ——R-POL-5 越顶步长在此拒绝）。
    validateRecheckRequest(request);

    RecheckOutcome outcome;
    // 预算账目的引用值面（ARC-05"引用值非副本"——params 透传登记）。
    outcome.budget.subdivisionBudget = request.params.maxSubdivisionDepth;

    // ---- 第 2 步：取消轮询（入口——命中即 Canceled，零素材，UX-03）。
    if (request.cancel && request.cancel()) {
        outcome.status = RecheckStatus::Canceled;
        return outcome;
    }

    // ---- 第 3 步：初始子段集（相邻必检参数之间的区间——端点/路点必含；
    // 天然按区间升序）。
    std::vector<SubSegment> leaves;
    leaves.reserve(request.mandatoryParameters.size() - 1);
    for (std::size_t i = 0; i + 1 < request.mandatoryParameters.size(); ++i) {
        leaves.push_back(SubSegment{request.mandatoryParameters[i],
                                    request.mandatoryParameters[i + 1]});
    }

    // 步长账目（"实际达到的最大步长"——R9 语义＝细分停止时刻**最后一层
    // 评估**的实测最严值，不含已被细分取代的粗子段；settled* 为已完成层
    // 的落地值——取消退出时取其上一层）。
    double settledJointStep = 0.0;
    double settledCartesianStep = 0.0;
    std::uint32_t subsegmentsExamined = 0;
    std::uint32_t depthUsed = 0;  // 已执行的二分次数（退出时的 layer 值）
    bool budgetExhausted = false;
    bool subdivisionPassed = false;

    // ---- 第 4 步：细分层循环（广度优先——每层对全部未满足子段同时二分；
    // 层数账目与 P-06 行 7"最大层数（二分）"一致）。
    for (std::uint32_t layer = 0;; ++layer) {
        depthUsed = layer;  // 第 layer 层评估前已执行 layer 次二分
        // 取消轮询（层边界——§15.1 轮询纪律）。
        if (request.cancel && request.cancel()) {
            outcome.status = RecheckStatus::Canceled;
            // 已执行部分的真实账目（UX-03——取消不是错误但账目如实；步长
            // 取上一层完整评估的落地值——当前层未开始度量）。
            outcome.budget.subdivisionDepthUsed = depthUsed;
            outcome.budget.subsegmentsExamined = subsegmentsExamined;
            return outcome;
        }

        // 第 4a 步：评估当前层全部子段（逐段双上界度量——确定遍历序；
        // 层内实测最严值在该层结束时落地为 settled*——账目语义见上注）。
        std::vector<SubSegment> unsatisfied;
        double layerWorstJoint = 0.0;
        double layerWorstCartesian = 0.0;
        for (const SubSegment& span : leaves) {
            const SegmentMeasure m = measureSubSegment(request, span);
            ++subsegmentsExamined;
            layerWorstJoint = std::max(layerWorstJoint, m.jointMaxRaw);
            layerWorstCartesian = std::max(layerWorstCartesian, m.cartesianMax);
            if (!m.bothOk()) {
                unsatisfied.push_back(span);  // 未满足——候选细分对象
            }
        }
        settledJointStep = layerWorstJoint;
        settledCartesianStep = layerWorstCartesian;

        // 第 4b 步：全满足 → 细分终止（双上界同时满足才终止——R9）。
        if (unsatisfied.empty()) {
            subdivisionPassed = true;
            break;
        }

        // 第 4c 步：预算判定（层数/子段数双上界——任一耗尽即停，不默认
        // 接受未充分验证段）。
        if (layer >= request.params.maxSubdivisionDepth) {
            budgetExhausted = true;  // 层数预算耗尽（P-06 行 7）
            break;
        }
        // 二分一层后的子段总数 = 通过数 + 2×未满足数（含初始子段的总量账
        // 目——P-06 行 8"单段最大子段数"）；超出即子段预算耗尽。
        const std::uint64_t totalAfterSplit =
            static_cast<std::uint64_t>(leaves.size() - unsatisfied.size())
            + 2ULL * static_cast<std::uint64_t>(unsatisfied.size());
        if (totalAfterSplit > static_cast<std::uint64_t>(request.params.maxSubsegments)) {
            budgetExhausted = true;  // 子段预算耗尽（P-06 行 8）
            break;
        }

        // 第 4d 步：细分一层——未满足子段二分（区间中点切分），通过子段
        // 原样保留；整体保持区间升序（确定遍历序——NFR-COR-02）。
        std::vector<SubSegment> next;
        next.reserve(leaves.size() + unsatisfied.size());
        for (const SubSegment& span : leaves) {
            const bool isUnsatisfied =
                std::binary_search(unsatisfied.begin(), unsatisfied.end(), span);
            if (isUnsatisfied) {
                const double mid = 0.5 * (span.low + span.high);
                next.push_back(SubSegment{span.low, mid});
                next.push_back(SubSegment{mid, span.high});
            } else {
                next.push_back(span);
            }
        }
        std::sort(next.begin(), next.end());
        leaves.swap(next);
    }

    // 预算账目（实际值面——Provided；来源标记 DerivedReadOnly＋方法短标
    // 记——步长是本域对被检几何的派生对照量，非用户直输入）。
    const auto provenance = core::ValueProvenance::make(
        core::ProvenanceKind::DerivedReadOnly, {}, {}, "recheck-budget");
    outcome.budget.actualMaxJointStep =
        core::SourcedValue<double>::provided(settledJointStep, provenance);
    outcome.budget.actualMaxCartesianStep =
        core::SourcedValue<double>::provided(settledCartesianStep, provenance);
    outcome.budget.subdivisionDepthUsed = depthUsed;
    outcome.budget.subsegmentsExamined = subsegmentsExamined;

    // ---- 第 4e 步：预算耗尽分支——DataInsufficient（R9：附实际最大步长
    // 与预算占用；不执行碰撞查询——验证覆盖已判不充分，见文件头注⑤）。
    if (budgetExhausted) {
        outcome.status = RecheckStatus::Completed;
        outcome.conclusion = RecheckConclusion::DataInsufficient;
        outcome.dataInsufficientReason = RecheckDataInsufficientReason::BudgetExhausted;
        outcome.budget.budgetExhausted = true;
        // 结论级素材（TRJ-RECHECK-BUDGET-EXHAUSTED——比较型：实际步长/预算；
        // R9 原文"附实际达到的最大步长与预算占用"）。
        const core::UnitToken radToken = axisUnitToken(JointTypeKind::Revolute);
        FailedSegmentRecord failure = makeRecheckFailure(
            request.segmentIndex,
            std::string(kTrjRecheckBudgetExhausted),
            "复检细分预算耗尽：段内仍有子段不满足关节/笛卡尔双上界（实测最严"
            "关节步长 " + std::to_string(settledJointStep) + " rad|m，最严笛卡尔"
            "代表点位移 " + std::to_string(settledCartesianStep)
            + " m），预算 " + std::to_string(depthUsed) + " 层二分/"
            + std::to_string(subsegmentsExamined) + " 子段检查已用尽",
            "不得默认接受未充分验证段（TRJ-04 R9）；调整工程策略预算或放宽"
            "平滑后重试，段级判定交证据汇总");
        core::ComparativeFields comparison;
        comparison.actual.quantity =
            core::SourcedValue<double>::provided(settledJointStep, provenance);
        comparison.actual.unit = radToken;
        comparison.expected.quantity = core::SourcedValue<double>::provided(
            request.params.maxJointStepRevolute, provenance);
        comparison.expected.unit = radToken;
        failure.comparison = comparison;
        outcome.failure = std::move(failure);
        return outcome;
    }

    // ---- 第 5 步：最终采样集（细分终止后的叶子区间端点并集——有序去重；
    // 含段端点 0/1＝必检参数首尾，P-06 行 9"段端点必含"）。
    std::vector<double> sampleParameters;
    sampleParameters.reserve(leaves.size() + 1);
    for (const SubSegment& span : leaves) {
        sampleParameters.push_back(span.low);  // 相邻子段共享端点——只收 low
    }
    sampleParameters.push_back(1.0);  // 末子段上界＝段末点

    // ---- 第 6 步：关节限位复检（逐采样逐轴——平滑可能越出原路径包络，
    // §11.3；违例全量收集——TRJ-06 定位输出不因首个短路）。
    for (std::size_t i = 0; i < sampleParameters.size(); ++i) {
        const double s = sampleParameters[i];
        const rw::math::Q q = request.path->sampleAt(s);
        for (std::size_t j = 0; j < q.size(); ++j) {
            const bool below = q[j] < request.lowerBoundQ[j];
            const bool above = q[j] > request.upperBoundQ[j];
            if (!below && !above) {
                continue;  // 该轴在区间内——下一样本
            }
            // 限位违例记录（TRJ-LIMIT-EXCEEDED——比较型三要素：实际构型/
            // 限界/单位；segmentIndex 取请求值，s 可定位）。
            RecheckLimitViolationRecord violation;
            violation.axis = j;
            violation.axisKind = request.jointTypes[j];
            violation.sampleIndex = i;
            violation.pathParameter = s;
            violation.actualQ = q[j];
            violation.upper = above;
            violation.boundQ = above ? request.upperBoundQ[j] : request.lowerBoundQ[j];
            FailedSegmentRecord& record = violation.record;
            record.segmentIndex = request.segmentIndex;
            record.phaseToken = kPhaseRecheck;
            record.reasonToken = std::string(kTrjLimitExceeded);
            record.pathParameter = s;
            record.cause = std::string(above ? "复检限位越上界" : "复检限位越下界")
                + "（轴 " + std::to_string(j) + "，采样 s=" + std::to_string(s)
                + "，实际 " + std::to_string(q[j]) + "，限界 "
                + std::to_string(violation.boundQ) + "）";
            record.recommendedAction =
                "平滑产物越出原路径包络——淘汰该平滑结果并回退到候选路径，"
                "重平滑或重规划";
            core::ComparativeFields comparison;
            const auto recordProvenance = core::ValueProvenance::make(
                core::ProvenanceKind::DerivedReadOnly, {}, {}, "recheck-limit");
            comparison.actual.quantity =
                core::SourcedValue<double>::provided(q[j], recordProvenance);
            comparison.actual.unit = axisUnitToken(request.jointTypes[j]);
            comparison.expected.quantity =
                core::SourcedValue<double>::provided(violation.boundQ, recordProvenance);
            comparison.expected.unit = axisUnitToken(request.jointTypes[j]);
            record.comparison = comparison;
            outcome.limitViolations.push_back(std::move(violation));
        }
    }

    // ---- 第 7 步：碰撞复检（最终采样集一次 policy PathSequence——样本
    // 由本域按细分协议生成、policy 不生成采样；pathParameters 等长携带
    // ——§10.3 约定；阈值零参数化——R-POL-5，间距唯一来源＝会话策略）。
    policy::CollisionQuery query;
    query.kind = policy::CollisionQueryKind::PathSequence;
    query.configurations.reserve(sampleParameters.size());
    for (const double s : sampleParameters) {
        query.configurations.push_back(request.path->sampleAt(s));
    }
    query.pathParameters = sampleParameters;
    query.stopAtFirstFinding = true;   // 淘汰场景——首个发现即停（输出仍带 coverage）
    query.requestMinDistance = false;  // 二值后端无距离能力（P-POL-11），不请求

    RecheckCallContext ctx(&request.cancel);
    const policy::CollisionEvaluation evaluation =
        request.session->evaluate(query, ctx);

    // policy 侧取消命中（样本边界轮询穿透）——顶层取消收尾（UX-03：零
    // 错误素材；账目已填——取消不是错误但已执行部分如实）。
    if (evaluation.status == policy::CollisionEvaluationStatus::Canceled) {
        outcome.status = RecheckStatus::Canceled;
        return outcome;
    }

    // coverage 投影（policy §6.2 PairCoverageRecord 数值子集——KIN-05
    // 口径；语义权威归 policy，投影不改语义）。
    outcome.coverage.pairsInScope = evaluation.coverage.pairsInScope;
    outcome.coverage.pairsWithGeometry = evaluation.coverage.pairsWithGeometry;
    outcome.coverage.pairsEvaluated = evaluation.coverage.pairsEvaluated;
    outcome.coverage.pairsExcludedByRule = evaluation.coverage.pairsExcludedByRule;

    // 三态映射（只有"Completed＋Applicable＋finalized"可采信——§6.3 状态
    // 机；其余一律不采信——KIN-05）。
    const bool trustworthy =
        evaluation.status == policy::CollisionEvaluationStatus::Completed
        && evaluation.finalized
        && evaluation.applicability == policy::ScopeApplicability::Applicable;
    if (!trustworthy) {
        // 验证器/碰撞证据缺失（Failed/EmptyScope/DisabledByPolicy）——
        // DataInsufficient（KIN-05：不得视为无碰撞；复检不足不能默认为
        // 通过——TRJ-04④/R9）。
        outcome.status = RecheckStatus::Completed;
        outcome.conclusion = RecheckConclusion::DataInsufficient;
        outcome.dataInsufficientReason =
            RecheckDataInsufficientReason::EvidenceUnavailable;
        outcome.failure = makeRecheckFailure(
            request.segmentIndex,
            std::string(kTrjRecheckDataInsufficient),
            "复检碰撞验证不可采信（policy 评估 status="
                + std::to_string(static_cast<int>(evaluation.status))
                + " finalized=" + (evaluation.finalized ? "true" : "false")
                + " applicability="
                + std::to_string(static_cast<int>(evaluation.applicability))
                + "——验证器或碰撞证据缺失）",
            "不得视为无碰撞；补齐碰撞检测器/策略作用域后重试，段级判定交"
            "证据汇总");
        return outcome;
    }

    // 碰撞发现转检出记录（仅 Collision 种类——MarginViolation 间距不足为
    // "非碰撞"（policy §7.4），间距语义归会话策略（ARC-05），不构成淘汰
    // 依据；stopAtFirstFinding=true 时至多一条）。
    for (const policy::CollisionFinding& finding : evaluation.findings) {
        if (finding.kind != policy::CollisionFindingKind::Collision) {
            continue;  // 间距不足——非碰撞，不淘汰（§7.4）
        }
        RecheckFindingRecord record;
        record.objectA = finding.objectA;  // policy 规范序 A<B（NFR-COR-05）
        record.objectB = finding.objectB;
        record.sampleIndex = finding.sampleIndex;
        record.pathParameter = finding.pathParameter;  // PathSequence 必填
        outcome.collisionRecords.push_back(std::move(record));
    }

    // ---- 第 8 步：结论归集（三态唯一——§15.6"无第四态"；检出违例（碰
    // 撞或限位）同属淘汰分支——§11.1 分支二）。
    outcome.status = RecheckStatus::Completed;
    if (!outcome.collisionRecords.empty()) {
        outcome.conclusion = RecheckConclusion::Collision;
        const RecheckFindingRecord& first = outcome.collisionRecords.front();
        outcome.failure = makeRecheckFailure(
            request.segmentIndex,
            std::string(kTrjRecheckCollision),
            "平滑后复检检出碰撞：对象对于采样 s="
                + (first.pathParameter.has_value()
                       ? std::to_string(*first.pathParameter)
                       : std::string{"?"})
                + " 相交（端点无碰撞不能豁免段内样本——AT-06 段内碰撞反例"
                  "语义）",
            "淘汰该平滑结果并回退到候选路径；重平滑或重规划（TRJ-03），"
            "不静默放行未复检的平滑路径");
    } else if (!outcome.limitViolations.empty()) {
        outcome.conclusion = RecheckConclusion::Collision;
        const RecheckLimitViolationRecord& first = outcome.limitViolations.front();
        outcome.failure = makeRecheckFailure(
            request.segmentIndex,
            std::string(kTrjLimitExceeded),
            "平滑后复检检出关节限位违例：轴 " + std::to_string(first.axis)
                + " 于采样 s=" + std::to_string(first.pathParameter) + " 实际 "
                + std::to_string(first.actualQ) + " 越出限界 "
                + std::to_string(first.boundQ) + "（rad|m）",
            "淘汰该平滑结果并回退到候选路径，重平滑或重规划（§11.3——平滑"
            "可能越出原路径包络）");
    } else {
        outcome.conclusion = RecheckConclusion::Passed;  // 全样本已检且无碰撞/限位违例
    }
    return outcome;
}

}  // namespace sdurws::ird::trajectory
