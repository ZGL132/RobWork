/**
 * @file   Playback.cpp
 * @brief  轨迹会话域面三件的实现（WP-16-T10）——动画播放查表（相邻样本
 *         线性插值，UI 线程安全）、曲线只读投影、路点局部重规划编排
 *         （§14.5.1 local-replan；worker 纯函数编排）。
 *
 * 设计依据：
 *   - units/trajectory.md §14.5.1/§14.5.2（三会话域面的语义出处——见
 *     Playback.hpp 文件头注）、§16.2（零修订与零计算红线）、§11.1/§11.2/
 *     §11.3（局部重规划的强制边与回退语义）、§15.8（请求面字段口径）
 *   - 算法纪律：查表二分＋线性插值（无样条求值——§16.1"UI 线程只做投
 *     影、插值查表"）；局部重规划零新算法——smooth/recheckSegment/
 *     timeParameterize 三纯函数的编排（T07/T08 产物即输入形态）
 *
 * 集成模式条件源（Q 构造面——Ptp.cpp 同款 gating）：本 TU 构造/拷贝
 * rw::math::Q 对象（插值帧/样本表副本/段表产物），其完整对象构造/析构
 * 符号在框架库 sdurw_math——冒烟模式无框架库可链，链接面必然未定义；
 * 冒烟口径仍覆盖 Commands/Export 的双模式面（见 CMakeLists 注释）。
 *
 * 线程安全：表构造后不可变（并发只读）；localReplan 纯函数单线程使用。
 * 确定性：二分/插值/处置序全确定（NFR-COR-02）。
 */

#include <sdurws/ird/trajectory/Playback.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace sdurws::ird::trajectory {

namespace {

/// 域错误便捷构造（调用方契约违约 fail-fast 轨——§15.0 错误二分第一类）。
TrajectoryError domainError(const std::string& subToken, const std::string& message)
{
    return TrajectoryError("trajectory/" + subToken, message);
}

/// payload 完整性共同校验（查表/曲线投影共用——违约序固定，NFR-COR-02）。
/// 校验：样本非空／时刻非递减／端点样本在位（首 0 末 total，位级——T08
/// 采样计划"事件对齐点〔全部结点〕必含"的承接）／维度一致／全值有限。
void validatePayloadCommon(const TimeParameterization& timeParam)
{
    const std::vector<TimedSample>& samples = timeParam.samples;
    if (samples.empty()) {
        throw domainError("curve/payload-empty",
                          "归档 payload 样本表为空——无动画/曲线语义（§6.2 不变量）");
    }
    for (std::size_t i = 1; i < samples.size(); ++i) {
        if (samples[i].t < samples[i - 1].t) {
            throw domainError("payload-time-order",
                              "样本时刻非递减（下标 " + std::to_string(i) + " 处 t="
                                  + std::to_string(samples[i].t) + " < 前值 "
                                  + std::to_string(samples[i - 1].t) + "）——payload 损坏");
        }
    }
    // 端点样本在位：T08 采样计划保证首末结点（t=0 与 t=total）必为样本；
    // 位级比较（payload 由同一求值器产出，两端点时间同源——不放宽为容差
    // 判定，防掩盖数据损坏）。
    if (samples.front().t != 0.0) {
        throw domainError("payload-missing-start",
                          "首样本 t=" + std::to_string(samples.front().t)
                              + " ≠ 0——缺起点事件对齐样本（§6.2 采样不变量）");
    }
    if (samples.back().t != timeParam.totalDurationS) {
        throw domainError("payload-missing-end",
                          "末样本 t=" + std::to_string(samples.back().t)
                              + " ≠ totalDurationS=" + std::to_string(timeParam.totalDurationS)
                              + "——缺终点事件对齐样本（§6.2 采样不变量）");
    }
    // 维度一致性：全部样本同一关节维数（变维＝payload 混装损坏）。
    const std::size_t dim = samples.front().q.size();
    for (const TimedSample& s : samples) {
        if (s.q.size() != dim || s.qd.size() != dim || s.qdd.size() != dim) {
            throw domainError("payload-dim",
                              "样本 q/qd/qdd 维度不一致（期望 " + std::to_string(dim)
                                  + "）——payload 损坏");
        }
    }
    // 全值有限：非有限分量静默进入查表/曲线会沿投影链污染呈现与导出
    // （NFR-COR-03 拒绝不静默）。
    for (const TimedSample& s : samples) {
        for (std::size_t j = 0; j < dim; ++j) {
            if (!std::isfinite(s.q[j]) || !std::isfinite(s.qd[j])
                || !std::isfinite(s.qdd[j])) {
                throw domainError("payload-non-finite",
                                  "样本含非有限分量（下标 " + std::to_string(j)
                                      + "，t=" + std::to_string(s.t) + "）——payload 损坏");
            }
        }
    }
}

}  // namespace

// =====================================================================
// 动画播放查表
// =====================================================================

TrajectoryPlaybackTable::TrajectoryPlaybackTable(
    const TimeParameterization& timeParam, const std::vector<double>& segmentDurationsS)
{
    // 公共完整性校验（违约 fail-fast——见辅助函数注）。
    validatePayloadCommon(timeParam);

    // 深拷贝样本表（值语义——UI 表可安全脱离原 payload 存活）。
    m_samples = timeParam.samples;
    m_totalDurationS = timeParam.totalDurationS;

    // 段归投影（可选）：累积边界 m_segmentCum[j]＝前 j 段时长之和；
    // Σ 与 totalDurationS 的自洽校验（§12.4"逐段对齐"——投影与 payload
    // 不同源即拒绝混装，防段归错位被静默接受）。
    if (!segmentDurationsS.empty()) {
        m_segmentCum.assign(segmentDurationsS.size() + 1, 0.0);
        for (std::size_t i = 0; i < segmentDurationsS.size(); ++i) {
            if (!(segmentDurationsS[i] > 0.0) || !std::isfinite(segmentDurationsS[i])) {
                throw domainError("payload-segment-duration",
                                  "段时长投影含非正值（下标 " + std::to_string(i)
                                      + "，值 " + std::to_string(segmentDurationsS[i])
                                      + " s）——投影与 payload 不同源");
            }
            m_segmentCum[i + 1] = m_segmentCum[i] + segmentDurationsS[i];
        }
        // 自洽判定（R3 审计 A2 修复，2026-10-15）：原实现按位级 != 拒绝，
        // 但生产面 TimeParam 对含内部结点（≥3 结点）的运动段按 L1 比例
        // 分摊结点时刻、总时长取结点累加——Σ fl(D·f_k) 与 D 位级不等的
        // 概率约三成，合法产物被诬"payload 不同源"fail-fast（回放动画
        // 无法构建、带段映射导出被拒）。改用相对容差判定： mismatch 容限
        // =1e-9·max(1, totalDurationS)（无量纲相对口径，远小于任何真实
        // "不同源"错装的段时长偏差——不同源错装至少差一个段时长量级），
        // 仅拦截真正的不同源混装。
        const double sumDiff = std::abs(m_segmentCum.back() - m_totalDurationS);
        const double sumTol = 1e-9 * std::max(1.0, std::abs(m_totalDurationS));
        if (!(sumDiff <= sumTol)) {
            throw domainError("payload-segment-sum",
                              "Σ段时长=" + std::to_string(m_segmentCum.back())
                                  + " ≠ totalDurationS=" + std::to_string(m_totalDurationS)
                                  + "（|差|=" + std::to_string(sumDiff)
                                  + " 超容差 " + std::to_string(sumTol)
                                  + "）——段归投影与 payload 不同源（§12.4 自洽）");
        }
    }
}

PlaybackFrame TrajectoryPlaybackTable::sampleAt(double t) const
{
    // 步骤②：合法域判定。NaN 会被比较链自然拒绝（NaN 与任何值的比较均为
    // false——单条件同时拦 NaN 与双侧越界）；不钳制不静默（NFR-COR-03），
    // UI 进度条应在调用前钳位到播放区间。
    if (!(t >= 0.0 && t <= m_totalDurationS)) {
        throw domainError("playback/t-out-of-range",
                          "播放时刻 t=" + std::to_string(t) + " 越出 [0, "
                              + std::to_string(m_totalDurationS) + "] s——调用方违约");
    }

    // 步骤③：样本二分定位。upper_bound 给出"时刻严格大于 t"的首样本下标
    // idx；命中精确样本时 idx-1 即该时刻的最后一个样本（时刻非递减下同
    // 时刻并列时取后者的确定性约定——同 t 恒同帧，NFR-COR-02）。
    const std::size_t n = m_samples.size();
    const auto it = std::upper_bound(
        m_samples.begin(), m_samples.end(), t,
        [](double value, const TimedSample& s) { return value < s.t; });
    const std::size_t idx = static_cast<std::size_t>(it - m_samples.begin());

    PlaybackFrame frame;
    frame.t = t;

    // 步骤④：精确样本命中（t 与样本时刻位级相等）→样本原值返回（零插
    // 值漂移——确定性回放锚；播放暂停/关键帧对齐走本分支）。
    if (idx > 0 && m_samples[idx - 1].t == t) {
        const TimedSample& s = m_samples[idx - 1];
        frame.q = s.q;
        frame.qd = s.qd;
        frame.qdd = s.qdd;
        frame.atSample = true;
    } else {
        // 步骤⑤：区间 [idx-1, idx] 线性插值。idx>=1 恒成立（t>=0=首样本
        // 时刻且非精确命中→t 在首样本之后）；idx<n 恒成立（t<=total=末
        // 样本时刻且非精确命中→t 在末样本之前）。分母>0：区间两端时刻
        // 相等只可能 t 命中其中之一（精确分支已拦截）。
        const TimedSample& a = m_samples[idx - 1];
        const TimedSample& b = m_samples[idx];
        const double alpha = (t - a.t) / (b.t - a.t);
        const std::size_t dim = a.q.size();
        // rw::math::Q(n) 单参构造不零初始化（Eigen 动态向量默认构造语义
        // ——T08 实现陷阱登记⑨同源）：三向量逐分量全量赋值，无读前写。
        frame.q = rw::math::Q(static_cast<int>(dim));
        frame.qd = rw::math::Q(static_cast<int>(dim));
        frame.qdd = rw::math::Q(static_cast<int>(dim));
        for (std::size_t j = 0; j < dim; ++j) {
            frame.q[j] = a.q[j] + alpha * (b.q[j] - a.q[j]);
            frame.qd[j] = a.qd[j] + alpha * (b.qd[j] - a.qd[j]);
            frame.qdd[j] = a.qdd[j] + alpha * (b.qdd[j] - a.qdd[j]);
        }
        frame.atSample = false;
    }

    // 步骤⑥：段归投影（提供时长投影时）——半开区间 [cum_j, cum_{j+1})，
    // t==total 落末段；线性扫描（段数为个位量级，二分无收益）。
    if (!m_segmentCum.empty()) {
        const std::size_t segCount = m_segmentCum.size() - 1;
        std::size_t seg = segCount - 1;  // 默认末段（覆盖 t==total）
        for (std::size_t j = 0; j + 1 < m_segmentCum.size(); ++j) {
            if (t >= m_segmentCum[j] && t < m_segmentCum[j + 1]) {
                seg = j;
                break;
            }
        }
        frame.segmentIndex = static_cast<std::uint32_t>(seg);
    }
    return frame;
}

// =====================================================================
// 曲线只读投影
// =====================================================================

std::vector<TrajectoryCurveSeries> projectTrajectoryCurves(
    const TimeParameterization& timeParam, TrajectoryCurveChannel channel)
{
    // payload 完整性校验（与查表同源同序——见辅助函数注）。
    validatePayloadCommon(timeParam);

    // 逐关节抽取通道列（jointIndex 升序稳定——NFR-COR-02）；非重采样：
    // 时刻轴与样本表逐一对应（投影语义——§14.5.1"读归档 payload 投影"）。
    const std::vector<TimedSample>& samples = timeParam.samples;
    const std::size_t dim = samples.front().q.size();
    std::vector<TrajectoryCurveSeries> series;
    series.reserve(dim);
    for (std::size_t j = 0; j < dim; ++j) {
        TrajectoryCurveSeries one;
        one.jointIndex = static_cast<std::uint32_t>(j);
        one.timesS.reserve(samples.size());
        one.values.reserve(samples.size());
        for (const TimedSample& s : samples) {
            one.timesS.push_back(s.t);
            switch (channel) {
                // 通道取值只做列选——零数值变换（单位随通道，呈现层换算
                // 归 ui——KIN-12 显示单位语义）。
                case TrajectoryCurveChannel::Position:
                    one.values.push_back(s.q[j]);
                    break;
                case TrajectoryCurveChannel::Velocity:
                    one.values.push_back(s.qd[j]);
                    break;
                case TrajectoryCurveChannel::Acceleration:
                    one.values.push_back(s.qdd[j]);
                    break;
            }
        }
        series.push_back(std::move(one));
    }
    return series;
}

// =====================================================================
// 路点局部重规划
// =====================================================================

namespace {

/// 前置校验（LocalReplanRequest 前置全表的执行点——违约 fail-fast；校验
/// 序固定＝同一坏请求必报同一首错，NFR-COR-02）。
void validateLocalReplanRequest(const LocalReplanRequest& request)
{
    // 区间与段表对齐：闭区间非空＋条目数一致＋segmentIndex 连续无重。
    if (request.firstSegmentIndex > request.lastSegmentIndex) {
        throw domainError("local-replan/range-empty",
                          "选中路段空区间（first=" + std::to_string(request.firstSegmentIndex)
                              + " > last=" + std::to_string(request.lastSegmentIndex) + "）");
    }
    const std::size_t expectedCount =
        static_cast<std::size_t>(request.lastSegmentIndex - request.firstSegmentIndex) + 1U;
    if (request.segments.size() != expectedCount) {
        throw domainError("local-replan/range-mismatch",
                          "段表条目数 " + std::to_string(request.segments.size())
                              + " ≠ 区间段数 " + std::to_string(expectedCount)
                              + "——选中路段与段表未逐段对齐");
    }
    for (std::size_t i = 0; i < request.segments.size(); ++i) {
        const std::uint32_t want = request.firstSegmentIndex + static_cast<std::uint32_t>(i);
        if (request.segments[i].segmentIndex != want) {
            throw domainError("local-replan/segment-order",
                              "段表第 " + std::to_string(i) + " 条 segmentIndex="
                                  + std::to_string(request.segments[i].segmentIndex)
                                  + " ≠ 期望 " + std::to_string(want));
        }
    }

    // 逐段形态：运动段/驻留段二分（dwellDurationS>0 判驻留）＋路点形态
    // ＋必经保护集合法域。
    bool hasMovement = false;
    std::size_t dim = 0;
    for (std::size_t i = 0; i < request.segments.size(); ++i) {
        const LocalReplanSegmentInput& seg = request.segments[i];
        if (seg.dwellDurationS > 0.0) {
            // 驻留段：路点与保护集必空（§5.4 步骤 3——常值段无几何路径）。
            if (!seg.waypoints.empty() || !seg.mandatoryWaypointIndices.empty()) {
                throw domainError("local-replan/dwell-shape",
                                  "驻留段（segmentIndex=" + std::to_string(seg.segmentIndex)
                                      + "）不得携带路点/保护集");
            }
            if (!std::isfinite(seg.dwellDurationS)) {
                throw domainError("local-replan/dwell-duration",
                                  "驻留时长非有限（segmentIndex="
                                      + std::to_string(seg.segmentIndex) + "）");
            }
            continue;
        }
        // 运动段：路点 ≥2、同维、全有限（冻结输入投影完整性）。
        hasMovement = true;
        if (seg.waypoints.size() < 2) {
            throw domainError("local-replan/waypoints",
                              "运动段（segmentIndex=" + std::to_string(seg.segmentIndex)
                                  + "）路点数 " + std::to_string(seg.waypoints.size())
                                  + " < 2（§6.2 不变量）");
        }
        if (dim == 0) {
            dim = seg.waypoints.front().size();
        }
        for (const rw::math::Q& q : seg.waypoints) {
            if (q.size() != dim) {
                throw domainError("local-replan/dim",
                                  "运动段（segmentIndex=" + std::to_string(seg.segmentIndex)
                                      + "）构型维度 " + std::to_string(q.size())
                                      + " ≠ 首段 " + std::to_string(dim));
            }
            for (std::size_t j = 0; j < dim; ++j) {
                if (!std::isfinite(q[j])) {
                    throw domainError("local-replan/non-finite",
                                      "运动段（segmentIndex="
                                          + std::to_string(seg.segmentIndex) + "）含非有限"
                                          "分量（轴 " + std::to_string(j) + "）");
                }
            }
        }
        // 必经保护集：下标合法、严格升序去重（简化保护集契约——§5.6）。
        for (std::size_t k = 0; k < seg.mandatoryWaypointIndices.size(); ++k) {
            const std::size_t idx = seg.mandatoryWaypointIndices[k];
            if (idx == 0 || idx + 1 == seg.waypoints.size()) {
                throw domainError("local-replan/mandatory-endpoint",
                                  "保护集含端点（segmentIndex="
                                      + std::to_string(seg.segmentIndex)
                                      + "）——端点恒保护无须列入");
            }
            if (idx >= seg.waypoints.size()) {
                throw domainError("local-replan/mandatory-oob",
                                  "保护集下标越界（segmentIndex="
                                      + std::to_string(seg.segmentIndex) + "，下标 "
                                      + std::to_string(idx) + "）");
            }
            if (k > 0 && idx <= seg.mandatoryWaypointIndices[k - 1]) {
                throw domainError("local-replan/mandatory-order",
                                  "保护集非严格升序（segmentIndex="
                                      + std::to_string(seg.segmentIndex) + "）");
            }
        }
    }
    if (!hasMovement) {
        throw domainError("local-replan/no-movement",
                          "选中路段全为驻留段——无运动几何可重规划");
    }

    // 维度投影一致性：jointTypes/限位区间/限值视图与构型维度等长；限位
    // 逐轴 lower<upper（限值视图值域语义由 timeParameterize 复验——本处
    // 只拦维度与区间形状，避免双重校验口径分叉）。
    if (request.jointTypes.size() != dim || request.lowerBoundQ.size() != dim
        || request.upperBoundQ.size() != dim || request.jointLimits.size() != dim) {
        throw domainError("local-replan/projection-dim",
                          "jointTypes/限位区间/限值视图维度与构型维度 "
                              + std::to_string(dim) + " 不一致");
    }
    for (std::size_t j = 0; j < dim; ++j) {
        if (!(request.lowerBoundQ[j] < request.upperBoundQ[j])) {
            throw domainError("local-replan/bounds",
                              "评价区间下界 ≥ 上界（轴 " + std::to_string(j) + "）");
        }
    }

    // 配置数值面（§5.5 投影——调用方自 config.trj 解析传入，本域零默认值）。
    if (!(request.limitsScaleFactor > 0.0) || request.limitsScaleFactor > 1.0
        || !std::isfinite(request.limitsScaleFactor)) {
        throw domainError("local-replan/scale",
                          "limitsScaleFactor 非法（须 ∈ (0,1]）");
    }
    if (!(request.smoothToleranceJoint > 0.0) || !std::isfinite(request.smoothToleranceJoint)
        || !(request.smoothToleranceTcp > 0.0) || !std::isfinite(request.smoothToleranceTcp)) {
        throw domainError("local-replan/tolerance",
                          "平滑双域容差须有限且 >0（§5.5 smoothTolerance）");
    }
    if (request.smoothMaxIterations < 1U) {
        throw domainError("local-replan/smooth-iterations",
                          "平滑收缩重试上限须 ≥1（§15.7 迭代上限）");
    }

    // 复检协议数值（P-06 来源引用——R-POL-5 同款拒绝非法/越顶；validate
    // 抛其自有 token，如实传播）。
    validateRecheckParameters(request.recheckParams);

    // 协议可执行前置（§11.2）：存在适用段则会话与采样器必须就位——调用
    // 方应改标不适用或提供协议设施，不允许"声明适用却不给协议"。
    const bool anyApplicable = std::any_of(
        request.segments.begin(), request.segments.end(),
        [](const LocalReplanSegmentInput& s) { return s.collisionCheckApplicable; });
    if (anyApplicable
        && (request.collisionSession == nullptr || request.representPointSampler == nullptr)) {
        throw domainError("local-replan/recheck-unavailable",
                          "存在碰撞适用段但复检协议设施缺席（会话/采样器为空）"
                          "——§11.2 复检入口前置");
    }

    // 时间化配置（§15.8 请求面字段口径——T08 返工 S-2 提示承接）。
    if (request.motionLawToken != kTimeParamMethodQuinticSplineC2) {
        throw domainError("local-replan/motion-law",
                          "运动律 token 非法（§12.7 封闭词表当前唯一值 "
                              "quintic-spline-c2）");
    }
    if (!(request.timeParamSampleStepS > 0.0) || !std::isfinite(request.timeParamSampleStepS)) {
        throw domainError("local-replan/sample-step",
                          "采样步长须有限且 >0 s（§15.8 sampleStepS）");
    }
    if (request.timeParamMaxIterations < 1U) {
        throw domainError("local-replan/time-iterations",
                          "时间化缩放迭代上限须 ≥1（§15.8 maxIterations）");
    }

    // 链连续性提前守卫：忽略驻留条目后，相邻运动段端点构型位级相等（平
    // 滑端点强制不变故链性保持——冻结输入本身损坏时提前拒绝，省去无效
    // 平滑计算；与 timeParameterize 前置同口径，先行给出本面 token）。
    const LocalReplanSegmentInput* prevMovement = nullptr;
    for (const LocalReplanSegmentInput& seg : request.segments) {
        if (seg.dwellDurationS > 0.0) {
            continue;
        }
        if (prevMovement != nullptr
            && !(prevMovement->waypoints.back() == seg.waypoints.front())) {
            throw domainError("local-replan/chain-break",
                              "相邻运动段端点构型位级不等（segmentIndex="
                                  + std::to_string(prevMovement->segmentIndex) + " → "
                                  + std::to_string(seg.segmentIndex)
                                  + "）——冻结输入链不连续");
        }
        prevMovement = &seg;
    }
}

/// 均匀结点参数（k/(n-1)，k=0..n-1——IPathGeometry 插值性契约"结点均匀
/// 归一化"的 s 值序列；n≥2）。
std::vector<double> uniformNodeParameters(std::size_t nodeCount)
{
    std::vector<double> params(static_cast<std::size_t>(nodeCount), 0.0);
    for (std::size_t k = 0; k < nodeCount; ++k) {
        params[k] = static_cast<double>(k) / static_cast<double>(nodeCount - 1U);
    }
    return params;
}

/// 结点参数去端点（interiorS——首尾 0/1 剔除，§15.8 interiorS 契约）。
std::vector<double> interiorOf(const std::vector<double>& params)
{
    std::vector<double> interior;
    if (params.size() >= 3) {
        interior.assign(params.begin() + 1, params.end() - 1);
    }
    return interior;
}

}  // namespace

LocalReplanOutcome localReplan(const LocalReplanRequest& request)
{
    // 步骤 1：前置校验（fail-fast——见辅助函数注）。
    validateLocalReplanRequest(request);

    // 步骤 2：取消轮询（命中→Canceled 零素材——UX-03）。
    if (request.cancel && request.cancel()) {
        LocalReplanOutcome canceled;
        canceled.frozenIdentity = request.frozenIdentity;
        canceled.rangeFirstSegmentIndex = request.firstSegmentIndex;
        canceled.rangeLastSegmentIndex = request.lastSegmentIndex;
        return canceled;
    }

    LocalReplanOutcome outcome;
    outcome.frozenIdentity = request.frozenIdentity;
    outcome.rangeFirstSegmentIndex = request.firstSegmentIndex;
    outcome.rangeLastSegmentIndex = request.lastSegmentIndex;

    // 步骤 3：逐段处置（区间序——固定处置序，NFR-COR-02）。
    std::vector<TimeParamSegment> timeSegs;
    timeSegs.reserve(request.segments.size());
    for (const LocalReplanSegmentInput& seg : request.segments) {
        if (request.cancel && request.cancel()) {
            // 取消：已完成前缀报告保留、零产物（UX-03——取消非错误）。
            outcome.status = LocalReplanStatus::Canceled;
            return outcome;
        }

        LocalReplanSegmentReport report;
        report.segmentIndex = seg.segmentIndex;

        // 3a. 驻留段：无平滑/复检语义——直接进时间化段表（§9.1 常值段）。
        if (seg.dwellDurationS > 0.0) {
            report.smoothStatus = SmoothStatus::NoChange;  // 占位值——语义由 skip 标注承载
            report.recheckSkipped = true;
            report.recheckSkipReason = "dwell-segment";
            report.adoptedNodeCount = 0;
            outcome.segmentReports.push_back(std::move(report));

            TimeParamSegment ts;
            ts.segmentIndex = seg.segmentIndex;
            ts.dwellDurationS = seg.dwellDurationS;
            ts.sourceTaskPoint = seg.sourceTaskPoint;
            timeSegs.push_back(std::move(ts));
            continue;
        }

        // 3b. 运动段平滑（§15.7——简化＋样条重拟合；双域容差/迭代上限/
        // FK 派生维可选消费均自请求投影直传）。
        SmoothRequest smoothReq;
        smoothReq.waypoints = seg.waypoints;
        smoothReq.mandatoryIndices = seg.mandatoryWaypointIndices;
        smoothReq.smoothToleranceJoint = request.smoothToleranceJoint;
        smoothReq.smoothToleranceTcp = request.smoothToleranceTcp;
        smoothReq.maxIterations = request.smoothMaxIterations;
        smoothReq.fkPort = request.fkPort;
        smoothReq.cancel = request.cancel;
        const SmoothOutcome smoothOut = smooth(smoothReq);
        report.smoothStatus = smoothOut.status;
        report.deviation = smoothOut.deviation;

        if (smoothOut.status == SmoothStatus::Canceled) {
            outcome.status = LocalReplanStatus::Canceled;
            return outcome;
        }

        // 最终几何与结点（Smoothed＝样条产物；其余状态＝原折线——§11.4
        // "偏差超容差→该次平滑作废"保留原路径语义）。
        std::shared_ptr<const IPathGeometry> adoptedGeometry;
        std::vector<double> adoptedParams;
        if (smoothOut.status == SmoothStatus::Smoothed) {
            adoptedGeometry = smoothOut.geometry;
            adoptedParams = uniformNodeParameters(smoothOut.smoothPath.size());
            report.geometryChanged = true;
            report.adoptedNodeCount = smoothOut.smoothPath.size();
        } else {
            adoptedGeometry = makeLinearJointPathGeometry(seg.waypoints);
            adoptedParams = uniformNodeParameters(seg.waypoints.size());
            report.geometryChanged = false;
            report.adoptedNodeCount = seg.waypoints.size();
        }

        // 3c. 复检强制边（§11.1——只要几何变化就重新复检；§11.2 范围口径
        // ——复检面向 collisionCheckApplicable 段）。
        if (smoothOut.status == SmoothStatus::Smoothed) {
            if (seg.collisionCheckApplicable) {
                RecheckRequest recheckReq;
                recheckReq.path = adoptedGeometry.get();
                // 必检参数＝保留结点均匀 s 序列（首 0 末 1——R2①/P-06 行 9
                // "段端点必含"；插值性契约保证 s_k 处取结点值）。
                recheckReq.mandatoryParameters = adoptedParams;
                recheckReq.jointTypes = request.jointTypes;
                recheckReq.lowerBoundQ = request.lowerBoundQ;
                recheckReq.upperBoundQ = request.upperBoundQ;
                recheckReq.params = request.recheckParams;
                recheckReq.sampler = request.representPointSampler;
                recheckReq.session = request.collisionSession;
                recheckReq.segmentIndex = seg.segmentIndex;
                recheckReq.cancel = request.cancel;
                RecheckOutcome recheckOut = recheckSegment(recheckReq);
                if (recheckOut.status == RecheckStatus::Canceled) {
                    outcome.status = LocalReplanStatus::Canceled;
                    return outcome;
                }
                report.recheckExecuted = true;
                report.recheck = std::move(recheckOut);
                if (report.recheck->conclusion != RecheckConclusion::Passed) {
                    // 分支二：检出违例/覆盖不足→淘汰该平滑结果，回退原折
                    // 线（R-TRJ-3"保留未平滑候选路径"；原几何已由原运行复
                    // 检验证——零变化沿用其结论，不再二次复检）。不静默：
                    // 淘汰事实经 smoothRejectedFallback＋recheck 结论如实
                    // 披露。
                    adoptedGeometry = makeLinearJointPathGeometry(seg.waypoints);
                    adoptedParams = uniformNodeParameters(seg.waypoints.size());
                    report.smoothRejectedFallback = true;
                    report.geometryChanged = false;
                    report.adoptedNodeCount = seg.waypoints.size();
                    outcome.anySmoothFallback = true;
                }
            } else {
                // 无碰撞验证义务段——跳过复检并显式标注（§11.2 范围口径；
                // 安全由线性凸组合性质〔§7.4〕与时间化限值校验〔§12.2〕承
                // 担，不冒充"已复检"）。
                report.recheckSkipped = true;
                report.recheckSkipReason = "collision-not-applicable";
            }
        } else {
            // 零变化/作废/放弃——几何未变，沿用既有复检结论（§11.1"零变
            // 化零复检"）。
            report.recheckSkipped = true;
            report.recheckSkipReason = "no-geometry-change";
        }

        outcome.segmentReports.push_back(std::move(report));

        // 步骤 4 前半：时间化段表条目（最终采纳几何＋内部结点 s 参数去端
        // 点——§15.8 interiorS 契约）。
        TimeParamSegment ts;
        ts.segmentIndex = seg.segmentIndex;
        ts.geometry = adoptedGeometry;
        ts.interiorS = interiorOf(adoptedParams);
        ts.sourceTaskPoint = seg.sourceTaskPoint;
        timeSegs.push_back(std::move(ts));
    }

    // 步骤 4 后半：时间参数化（§15.8 请求面字段直传——失败/取消语义随其
    // 四态词表，本编排不另设顶层失败态）。
    TimeParamRequest timeReq;
    timeReq.segments = std::move(timeSegs);
    timeReq.jointLimits = request.jointLimits;
    timeReq.limitsScaleFactor = request.limitsScaleFactor;
    timeReq.motionLawToken = request.motionLawToken;
    timeReq.sampleStepS = request.timeParamSampleStepS;
    timeReq.maxIterations = request.timeParamMaxIterations;
    timeReq.cancel = request.cancel;
    outcome.rangeTimeParam = timeParameterize(timeReq);
    if (outcome.rangeTimeParam.status == TimeParamStatus::Canceled) {
        outcome.status = LocalReplanStatus::Canceled;
        return outcome;
    }

    // 步骤 5：组装 Ok 结局（"新 RunId，新归档结果"由装配层接纳时分配
    // ——本编排零写盘零项目触碰，交付新结果记录的域内容）。
    outcome.status = LocalReplanStatus::Ok;
    return outcome;
}

}  // namespace sdurws::ird::trajectory
