/**
 * @file   Ptp.cpp
 * @brief  关节空间 PTP 规划的实现翻译单元——逐轴线性插值、构型选择三键
 *         全序与 planPtpSegment 执行序（WP-16-T04 批）。
 *
 * 设计依据：
 *   - units/trajectory.md §7.3（q(s)=(1-s)·q_a+s·q_b 逐轴线性；单轴差为
 *     零→恒值；全轴差为零→零长段合法）、§7.4（限位凸组合性质；限速不在
 *     几何阶段判定）、§7.5（延续性优先→裕量优先→稳定编号兜底）、§7.6
 *     （失败语义表——PTP 侧素材归类与语义列）、§15.1（前置/非法示例）
 *   - 需求 TRJ-01/TRJ-06、NFR-COR-02/03；附录 D C7 运行校验量纲（越限
 *     素材的单位标注——角度 rad、位置 m 依关节类型逐轴）
 *   - 任务契约 tasks/foundation/WP-16-T04.json（acceptance 1——V-01/02/03
 *     解析算例面）
 *
 * 确定性（NFR-COR-02）：选择序为全序（三键逐级收紧）；插值与路径长度
 * 为逐元素标量运算（无并行归约）；所有失败文案含实际值定位。
 */

#include <sdurws/ird/trajectory/Ptp.hpp>

#include <sdurws/ird/core/Units.hpp>        // core::QuantityKind（越限素材单位口径）
#include <sdurws/ird/trajectory/DiagCodes.hpp>
#include <sdurws/ird/trajectory/Errors.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace sdurws::ird::trajectory {

namespace {

/// 有限性判定（NaN 与 ±inf 均拒绝——NFR-COR-03）。
bool isFiniteDouble(double v)
{
    return v == v && v - v == 0.0;
}

/// 关节轴的量纲与单位（附录 D C7 运行校验口径：逐轴按关节类型定纲——
/// 转动 rad、移动 m；本域不持有关节类型视图，以区间上下界的数值尺度
/// 无法可靠区分，故单位标注按调用方声明。本实现以 Angle 口径标注素材
/// 单位并在 cause 文案中写明逐轴量纲约定——V-03 素材的单位字段义务由
/// core::ComparativeFields 承载，量纲歧义在 cause 中显式说明，不私设
/// 第二套判定）。
core::UnitToken axisUnitToken()
{
    // rad token（core 注册表编译期冻结——find 失败属 core 注册表缺陷，
    // 该断言在 core Units 测试已钉住；此处取值失败即环境缺陷 fail-fast）。
    auto token = core::UnitToken::find("rad");
    return *token;
}

}  // namespace

// =====================================================================
// 关节空间插值（§7.3）
// =====================================================================

rw::math::Q interpolateJointLinear(const rw::math::Q& qa, const rw::math::Q& qb, double s)
{
    // 前置 1：维度一致（调用方契约违约——fail-fast，不静默截断）。
    if (qa.size() != qb.size()) {
        throw TrajectoryError("trajectory/ptp/interpolate-dim",
                              "插值两端维度不一致（qa=" + std::to_string(qa.size())
                                  + "，qb=" + std::to_string(qb.size()) + "）");
    }
    // 前置 2：行程参数在 [0,1]（越界外推属几何语义破坏——§7.3 定义域）。
    if (!(s >= 0.0 && s <= 1.0)) {
        throw TrajectoryError("trajectory/ptp/interpolate-range",
                              "行程参数 s 须 ∈[0,1]，实际 " + std::to_string(s));
    }

    // 逐轴凸组合：(1-s)·qa[i] + s·qb[i]。s=0/1 时浮点下精确回到端点
    // （(1-0)·qa+0·qb == qa 逐分量精确——端点还原性质，V-01 断言依托）。
    rw::math::Q out(qa.size());
    for (std::size_t i = 0; i < static_cast<std::size_t>(qa.size()); ++i) {
        out[i] = (1.0 - s) * qa[i] + s * qb[i];
    }
    return out;
}

// =====================================================================
// 构型选择（§7.5 三键全序）
// =====================================================================

std::size_t choosePtpCandidate(const rw::math::Q& startQ,
                               const std::vector<PtpCandidate>& candidates,
                               double ikContinuityThreshold)
{
    // 前置：候选集非空（§15.1 非法示例原文——"终点解集为空却调用（前置
    // 违约→fail-fast）"）。
    if (candidates.empty()) {
        throw TrajectoryError("trajectory/ptp/choose-empty",
                              "PTP 终点候选解集为空（前置违约——§15.1；"
                              "用户数据的无解结局应在序列展开层以 TRJ-NO-PATH "
                              "素材表达，不进入本接口）");
    }
    // 前置：阈值合法（rad|m 逐轴上界——§5.5/§7.5）。
    if (!isFiniteDouble(ikContinuityThreshold) || ikContinuityThreshold <= 0.0) {
        throw TrajectoryError("trajectory/ptp/choose-threshold",
                              "ikContinuityThreshold 须有限且 >0（rad|m），实际 "
                                  + std::to_string(ikContinuityThreshold));
    }

    // 逐候选合法性核验（维度一致＋分量有限＋裕量非 NaN——投影面数据
    // 违约 fail-fast；裕量 +∞ 合法：全无界关节链，参与全序取最大）。
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        const PtpCandidate& c = candidates[i];
        if (static_cast<std::size_t>(c.q.size()) != static_cast<std::size_t>(startQ.size())) {
            throw TrajectoryError("trajectory/ptp/choose-dim",
                                  "候选解维度与起点不一致（候选 " + std::to_string(i)
                                      + "：q=" + std::to_string(c.q.size())
                                      + "，startQ=" + std::to_string(startQ.size())
                                      + "）");
        }
        for (std::size_t j = 0; j < static_cast<std::size_t>(c.q.size()); ++j) {
            if (!isFiniteDouble(c.q[j])) {
                throw TrajectoryError("trajectory/ptp/choose-nonfinite",
                                      "候选解含非有限关节分量（候选 "
                                          + std::to_string(i) + "，轴 "
                                          + std::to_string(j) + "）");
            }
        }
        if (c.minimumJointMargin != c.minimumJointMargin) {  // NaN 判定
            throw TrajectoryError("trajectory/ptp/choose-margin-nan",
                                  "候选解 minimumJointMargin 为 NaN（候选 "
                                      + std::to_string(i) + "——投影面数据违约）");
        }
    }

    // 规则 1（延续性优先）：与起点逐轴 |Δq| 全部 ≤ 阈值的候选构成优先集
    // ——避免无谓的构型跳变（§7.5 原文；逐轴上界判定，任一轴超阈值即
    // 不延续）。
    std::vector<std::size_t> pool;   // 当前选择池（延续集或全集）
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        const rw::math::Q& q = candidates[i].q;
        bool continuous = true;
        for (std::size_t j = 0; j < static_cast<std::size_t>(q.size()); ++j) {
            if (std::fabs(q[j] - startQ[j]) > ikContinuityThreshold) {
                continuous = false;
                break;
            }
        }
        if (continuous) {
            pool.push_back(i);
        }
    }
    if (pool.empty()) {
        // 无延续候选→选择池回退为全集（规则 2 前提分支——§7.5 原文
        // "无延续候选时，按 minimumJointMargin 降序"）。
        pool.resize(candidates.size());
        for (std::size_t i = 0; i < pool.size(); ++i) {
            pool[i] = i;
        }
    }

    // 规则 2＋3（裕量降序→稳定编号升序）：在池内线性扫描取最优——
    // 比较谓词 (margin 更大) 优先，margin 精确相等时 stableIndex 更小者
    // 优先；并列判定＝浮点位级相等（裕量来自同一 D-KIN-6 归一化计算的
    // f64 值，位级判定稳定可复现——NFR-COR-02）。
    std::size_t best = pool.front();
    for (const std::size_t idx : pool) {
        const PtpCandidate& c = candidates[idx];
        const PtpCandidate& b = candidates[best];
        const bool better = c.minimumJointMargin > b.minimumJointMargin
                         || (c.minimumJointMargin == b.minimumJointMargin
                             && c.stableIndex < b.stableIndex);
        if (better) {
            best = idx;
        }
    }
    return best;
}

// =====================================================================
// PTP 段规划（执行序见 Ptp.hpp 函数注）
// =====================================================================

PtpPlanResult planPtpSegment(const PtpRequest& request)
{
    // ---- 第 1 步：前置校验（调用方契约违约 → fail-fast，§15.1）----
    const std::size_t dof = static_cast<std::size_t>(request.startQ.size());
    if (dof == 0U) {
        throw TrajectoryError("trajectory/ptp/empty-q",
                              "起点构型维度为 0（无自由度链无 PTP 语义）");
    }
    for (std::size_t i = 0; i < dof; ++i) {
        if (!isFiniteDouble(request.startQ[i])) {
            throw TrajectoryError("trajectory/ptp/nonfinite-start",
                                  "起点构型含非有限分量（轴 " + std::to_string(i) + "）");
        }
    }
    if (static_cast<std::size_t>(request.lowerBoundQ.size()) != dof
        || static_cast<std::size_t>(request.upperBoundQ.size()) != dof) {
        throw TrajectoryError("trajectory/ptp/bounds-dim",
                              "评价区间维度与起点不一致（lower="
                                  + std::to_string(request.lowerBoundQ.size())
                                  + "，upper="
                                  + std::to_string(request.upperBoundQ.size())
                                  + "，startQ=" + std::to_string(dof) + "）");
    }
    if (request.candidates.empty()) {
        throw TrajectoryError("trajectory/ptp/empty-candidates",
                              "终点候选解集为空却调用 PTP 规划（前置违约——"
                              "§15.1 非法示例；无解结局由序列展开层产素材）");
    }
    if (!request.tcpRef.isValid()) {
        throw TrajectoryError("trajectory/ptp/tcp-ref",
                              "tcpRef 必须为合法对象身份（§6.3——禁止名称直存，"
                              "ARC-04）");
    }
    if (request.startKind == WaypointKind::Via || request.startKind == WaypointKind::Dwell
        || request.endKind == WaypointKind::Via || request.endKind == WaypointKind::Dwell) {
        throw TrajectoryError("trajectory/ptp/waypoint-kind",
                              "PTP 段端点种类不得为 Via/Dwell（Via 由规划器/平滑"
                              "引入、Dwell 由驻留映射产出——调用方违约）");
    }
    // 任务点路点的可追溯性前置（§6.3"TaskPoint kind 必填 sourceTaskPoint"
    // 的请求侧联动——终点/起点各自绑定其来源站，缺失即构造不出合法段）。
    if (request.endKind == WaypointKind::TaskPoint
        && (!request.sourceTaskPoint.has_value() || !request.sourceTaskPoint->isValid())) {
        throw TrajectoryError("trajectory/ptp/task-point-source",
                              "endKind==TaskPoint 时 sourceTaskPoint 必须为合法"
                              "对象身份（§6.3——来源任务点可追溯）");
    }
    if (request.startKind == WaypointKind::TaskPoint
        && (!request.startSourceTaskPoint.has_value()
            || !request.startSourceTaskPoint->isValid())) {
        throw TrajectoryError("trajectory/ptp/start-source",
                              "startKind==TaskPoint 时 startSourceTaskPoint 必须"
                              "为合法对象身份（起点路点来源＝上一站——§6.2）");
    }
    if (request.startKind != WaypointKind::TaskPoint
        && request.startSourceTaskPoint.has_value()) {
        throw TrajectoryError("trajectory/ptp/start-source-dangling",
                              "startKind 非 TaskPoint 时 startSourceTaskPoint 必须为空"
                              "（不得携带悬空来源——身份面同语义同字节）");
    }
    if (!(request.constraint.limitsScaleFactor > 0.0
          && request.constraint.limitsScaleFactor <= 1.0)) {
        throw TrajectoryError("trajectory/ptp/constraint-scale",
                              "段约束 limitsScaleFactor 须 ∈(0,1]，实际 "
                                  + std::to_string(request.constraint.limitsScaleFactor));
    }

    // ---- 第 2 步：取消轮询（§15.1"规划循环边界"——入口一次；取消不是
    // 错误：零素材零诊断，UX-03/§6.7）。
    if (request.cancel && request.cancel()) {
        PtpPlanResult out;
        out.status = PtpStatus::Canceled;
        return out;
    }

    // ---- 第 3 步：端点限位守卫（§7.4——IK 硬过滤已保证端点在评价区间
    // 内，此处检查作为守卫；越限＝用户数据面失败 → NoPath＋
    // TRJ-LIMIT-EXCEEDED 素材（V-03 观测点），比较型字段含实际/期望/单位）。
    const auto checkEndpoint = [&](const rw::math::Q& q, double s,
                                   const char* which) -> std::optional<FailedSegmentRecord> {
        for (std::size_t i = 0; i < dof; ++i) {
            const double lo = request.lowerBoundQ[i];
            const double hi = request.upperBoundQ[i];
            if (!(lo < hi)) {
                throw TrajectoryError("trajectory/ptp/bounds-order",
                                      "评价区间上下界须逐轴 lower<upper（轴 "
                                          + std::to_string(i) + "）");
            }
            if (q[i] < lo || q[i] > hi) {
                // 越限素材：reasonToken=TRJ-LIMIT-EXCEEDED（DiagCodes 常量
                // ——禁字符串拼码）；比较型三要素＝实际关节值/越限侧界值/
                // 单位（rad——逐轴按关节类型定纲的口径见 axisUnitToken 注）。
                FailedSegmentRecord rec;
                rec.segmentIndex = request.segmentIndex;
                rec.phaseToken = kPhasePlanPtp;
                rec.reasonToken = std::string(kTrjLimitExceeded);
                rec.pathParameter = s;  // 端点 s=0/1——段内可定位
                rec.cause = std::string(which) + "构型越出评价区间（轴 "
                              + std::to_string(i) + "：实际 "
                              + std::to_string(q[i]) + "，区间 [" + std::to_string(lo)
                              + ", " + std::to_string(hi) + "]，单位 rad（移动关节 m））";
                rec.recommendedAction =
                    "检查该构型来源（startStateRef 解析或 IK 硬过滤视图）——"
                    "越限构型不得进入 PTP 几何；修正限位视图或替换构型后重评";
                core::ComparativeFields cmp;
                // 来源标记：DerivedReadOnly＋方法短标记——越限判定值是本域
                // 从限位视图派生的对照量（非用户直输入；ValueProvenance
                // 五类词表中 DerivedReadOnly 为派生只读承载）。
                const auto provenance = core::ValueProvenance::make(
                    core::ProvenanceKind::DerivedReadOnly, {}, {}, "ptp-endpoint");
                cmp.actual.quantity = core::SourcedValue<double>::provided(q[i], provenance);
                cmp.actual.unit = axisUnitToken();
                const double bound = (q[i] < lo) ? lo : hi;
                cmp.expected.quantity = core::SourcedValue<double>::provided(bound, provenance);
                cmp.expected.unit = axisUnitToken();
                rec.comparison = cmp;
                return rec;
            }
        }
        return std::nullopt;
    };

    if (auto rec = checkEndpoint(request.startQ, 0.0, "起点")) {
        PtpPlanResult out;
        out.status = PtpStatus::NoPath;
        out.failure = std::move(rec);
        return out;
    }

    // ---- 第 4 步：构型选择（§7.5 三键全序——单点实现 choosePtpCandidate；
    // 其内部前置已含候选集非空/维度/有限性校验）。
    const std::size_t chosen = choosePtpCandidate(request.startQ, request.candidates,
                                                  request.ikContinuityThreshold);
    const rw::math::Q& endQ = request.candidates[chosen].q;

    // 终点限位守卫（同起点口径——守卫面覆盖两端）。
    if (auto rec = checkEndpoint(endQ, 1.0, "终点")) {
        PtpPlanResult out;
        out.status = PtpStatus::NoPath;
        out.failure = std::move(rec);
        return out;
    }

    // 取消轮询（候选选择与几何构造之间——§15.1 循环边界纪律）。
    if (request.cancel && request.cancel()) {
        PtpPlanResult out;
        out.status = PtpStatus::Canceled;
        return out;
    }

    // ---- 第 5 步：几何构造（JointLinear 段——两路点端点承载逐轴线性
    // 语义 q(s)=(1-s)·q_a+s·q_b，插值语义由 interpolateJointLinear 单点
    // 实现供复检/动画查表共用，§16.1 播放=纯插值查表）。
    TrajectorySegment segment;
    segment.segmentIndex = request.segmentIndex;
    segment.spaceType = SegmentSpaceType::JointLinear;
    segment.tcpRef = request.tcpRef;
    segment.frameRef = request.frameRef;
    segment.constraint = request.constraint;
    segment.sourceTaskPoint = request.sourceTaskPoint;

    Waypoint wpStart;
    wpStart.kind = request.startKind;
    wpStart.segmentIndex = request.segmentIndex;
    wpStart.q = request.startQ;
    if (request.startKind == WaypointKind::TaskPoint) {
        // 起点路点（上一站任务点构型）的来源＝上一站对象（前置校验已保证
        // 有值——§6.3 路点可追溯性在起点侧的对称落点）。
        wpStart.sourceTaskPoint = request.startSourceTaskPoint;
    }

    Waypoint wpEnd;
    wpEnd.kind = request.endKind;
    wpEnd.segmentIndex = request.segmentIndex;
    wpEnd.q = endQ;
    if (request.endKind == WaypointKind::TaskPoint) {
        wpEnd.sourceTaskPoint = request.sourceTaskPoint;  // 可追溯性（NFR-COR-04）
    }

    segment.waypoints.push_back(std::move(wpStart));
    segment.waypoints.push_back(std::move(wpEnd));

    // 关节空间路径长度＝各轴 |Δq| 之和（§6.2——rad|m 逐轴混合计量口径，
    // 见 TrajectorySegment 结构注）；零长段（全轴差为零）恒 0——§7.2
    // 零长 PTP 段合法。
    double pathLen = 0.0;
    for (std::size_t i = 0; i < dof; ++i) {
        pathLen += std::fabs(endQ[i] - request.startQ[i]);
    }
    segment.pathLengthJoint = pathLen;
    // pathLengthTcp：TCP 路径长度是 FK 派生观察（§6.2"仅 JointLinear 段
    // 填写并标记 derived"——填写依赖 FK 端口消费，归 WP-16-T05/T08 落位
    // 后填充）；本批恒 0 且语义＝未计算，不冒充笛卡尔证据（§7.6）。

    // ---- 第 6 步：结构守卫（段构造保证的自证——§9.1"检查作为守卫"
    // 同款；失败即实现缺陷 fail-fast）。
    validateTrajectorySegment(segment);

    PtpPlanResult out;
    out.status = PtpStatus::Ok;
    out.segment = std::move(segment);
    out.selectedCandidateIndex = chosen;
    return out;
}

}  // namespace sdurws::ird::trajectory
