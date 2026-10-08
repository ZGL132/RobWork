/**
 * @file   Replay.cpp
 * @brief  DYN-08 数据面实现——曲线联动投影／回放数据构建／插值查表／
 *         峰值定位查询（Replay.hpp 三投影器类的执行体）。
 *
 * 设计依据：见 Replay.hpp 文件头（§9.5 三命令数据面＋§4.3/§4.6 纪律＋
 *   §10.0 错误两轨）。本实现 TU 的口径要点（与头文件契约一一对应）：
 *   1. 全部产出为数据重组——值＝样本行原字段直拷或相邻两样本线性混合；
 *      无 RNEA 递推、无积分、无统计聚合（峰值数值不重算——locate 只做
 *      结构对账后按行序取行）；
 *   2. 行序防御校验（t 非降＋同刻度关节行唯一）＝构建器产物不可能形态
 *      的 fail-fast 暴露面（调用方错误轨——DynamicsError，不发稳定码）；
 *   3. 查询空态（空集/越界/无峰关节）一律 std::nullopt 显式返回（
 *      NFR-COR-03 不伪造数值）。
 *
 * 线程安全：全部方法纯函数（输入只读、零副作用——零写盘零修订）。
 * 确定性：遍历序＝序列行序、双指针对齐按 jointIndex 升序（NFR-COR-02）。
 */

#include <sdurws/ird/dynamics/Replay.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

namespace sdurws::ird::dynamics {

namespace {

// =====================================================================
// 共享助手：序列行序防御校验与 Ok 关节表推导（两投影器与峰值定位共源
// ——单一实现点，口径漂移不可能在类间发生）。
// =====================================================================

/**
 * @brief 校验序列行序纪律并返回含 Ok 行关节的升序表（三投影器共用的
 *        第一步——DynamicsSeriesBuilder 产物的不可能形态在此 fail-fast）。
 *
 * 校验规则（Replay.hpp 契约）：行序 t 非降；同刻度内 jointIndex 严格
 * 升（等价于 (t, jointIndex) 无重复行对——重复即取值歧义，显示面不可
 * 接受，见 Replay.hpp projectCurves 注释的诚实登记）。
 *
 * @param series      [in] 待校验序列（只读）
 * @param okJointList [out] 含 Ok 行关节的升序去重表（jointIndex 升序——
 *                    computePeaks 行序解读锚）
 *
 * @throws DynamicsError t 倒退（token "series-non-monotonic"——同
 *         SeriesBuilder 口径）或同刻度关节行重复/乱序（token
 *         "series-row-duplicate"）
 */
void validateRowOrderAndCollectJoints(const DynamicsSeries& series,
                                      std::vector<std::uint32_t>& okJointList)
{
    okJointList.clear();

    // ---- 逐行扫描：时间单调非降＋同刻度关节唯一升序＋Ok 关节收集 ----
    // 逐行比较前行（序列行序＝构建器稳定排序产物；防御面只对"不可能
    // 形态"开火，不重排修复——§4.6 评估器不排序修复的同口径精神）。
    bool havePrev = false;             // 是否已有前行（首行免比较）
    double prevT = 0.0;                // 前行时间，单位 s
    std::uint32_t prevJoint = 0;       // 前行关节序号（同刻度比较用）
    for (const DynamicsSample& row : series.samples) {
        if (havePrev) {
            // 时间倒退＝DYN-SERIES-NON-MONOTONIC 语义——构建器 addSample
            // 已 fail-fast 同类输入，此处拦截"绕过构建器的手工序列"。
            if (row.t < prevT) {
                throw DynamicsError(
                    "series-non-monotonic",
                    "回放/曲线投影输入序列时间倒退：t=" + std::to_string(row.t)
                        + " s < 前行 t=" + std::to_string(prevT) + " s（构建器产物"
                        "不可能形态——上游违约，拒绝投影）");
            }
            // 同刻度内关节行必须严格升序且唯一（重复行对＝构建器 finalize
            // 标记形态——投影无诊断通道，fail-fast 暴露；乱序同理）。
            if (row.t == prevT && row.jointIndex <= prevJoint) {
                throw DynamicsError(
                    "series-row-duplicate",
                    "回放/曲线投影输入序列同刻度关节行重复或乱序：t=" + std::to_string(row.t)
                        + " s，jointIndex=" + std::to_string(row.jointIndex)
                        + "（前行 jointIndex=" + std::to_string(prevJoint)
                        + "）——帧内关节取值歧义，拒绝投影");
            }
        }
        // Ok 关节收集（行序按 (t, jointIndex) 排列——同关节行按时刻交错
        // 出现，不能按"相邻行去重"；逐行查重插入，扫描毕统一升序排序）。
        if (row.numericState == SampleNumericState::Ok
            && std::find(okJointList.begin(), okJointList.end(), row.jointIndex)
                   == okJointList.end()) {
            okJointList.push_back(row.jointIndex);
        }
        havePrev = true;
        prevT = row.t;
        prevJoint = row.jointIndex;
    }
    // 升序排序（computePeaks 行序解读锚——(jointIndex 升序, token 序) 的
    // 关节轴；行序交错收集后此处归位，NFR-COR-02 确定序）。
    std::sort(okJointList.begin(), okJointList.end());
}

/**
 * @brief 单关节五量状态构造（样本行→ReplayJointState 的字段直拷——
 *        投影零重算口径的最小执行点）。
 *
 * @param row [in] 源样本行（必须 Ok 行——调用方保证）
 * @return 关节状态（五量＝行内原字段；单位/量纲见 ReplayJointState 注释）
 */
ReplayJointState stateFromRow(const DynamicsSample& row)
{
    ReplayJointState s;
    s.jointIndex = row.jointIndex;
    s.jointObjectId = row.jointObjectId;
    s.jointType = row.jointType;
    s.q = row.q;                       // rad 或 m（按 jointType）
    s.qd = row.qd;                     // rad/s 或 m/s
    s.qdd = row.qdd;                   // rad/s² 或 m/s²
    s.generalizedForce = row.tauTotal; // N·m 或 N——曲线/回放的 τ 通道取总
                                       //   广义力（τ_total 与曲线联动"τ
                                       //   曲线"同源——DYN-03 输出口径）
    s.mechanicalPower = row.mechanicalPower; // W
    return s;
}

}  // namespace

// =====================================================================
// 曲线联动投影器（show-curves 数据面）。
// =====================================================================

CurveProjection DynamicsCurveProjector::projectCurves(const DynamicsSeries& series) const
{
    // ---- 第 1 步：行序防御校验＋Ok 关节表推导（共享第一步）----
    std::vector<std::uint32_t> okJoints;
    validateRowOrderAndCollectJoints(series, okJoints);

    // ---- 第 2 步：逐关节通道缓冲初始化（jointIndex 升序处理——
    //      NFR-COR-02 稳定序）----
    CurveProjection out;
    out.conditionId = series.conditionId;         // 工况透传
    out.sourceSeriesId = series.contentIdentity;  // 来源序列内容身份（追溯键）
    out.completeness = series.validity.completeness; // 完整性透传（UI 标注用）
    if (okJoints.empty()) {
        return out;  // Empty 显式语义：无 Ok 行→joints 空——不伪造曲线
                     // （NFR-COR-03；conditionId/身份块仍完整——可寻址）
    }
    out.joints.resize(okJoints.size());

    // ---- 第 3 步：单遍扫描，Ok 行进通道、非 Ok 行计数（第二形态纪律：
    //      值＝行内原字段直拷，零重算零平滑零插值——§4.3 同口径）----
    // 行序按 (t, jointIndex) 排列——同关节行按时刻交错出现，逐行按
    // jointIndex 在 Ok 关节表（升序）中定位行下标（二分——关节数即链
    // 长，小表；零推进状态，交错行序安全）。
    for (const DynamicsSample& row : series.samples) {
        const auto pos = std::lower_bound(okJoints.begin(), okJoints.end(),
                                          row.jointIndex);
        if (pos == okJoints.end() || *pos != row.jointIndex) {
            continue;  // 非 Ok 关节的行（全非 Ok 关节——不在投影面）
        }
        JointCurves& jc = out.joints[static_cast<std::size_t>(pos - okJoints.begin())];
        if (jc.t.empty()) {
            // 该关节首个被接受的行：携带关节元数据（同关节行恒同值——
            // 构建器/评估器契约；取首 Ok 行值）。
            jc.jointIndex = row.jointIndex;
            jc.jointObjectId = row.jointObjectId;
            jc.jointType = row.jointType;
        }
        if (row.numericState != SampleNumericState::Ok) {
            ++jc.nonOkCount;  // 非 Ok 行剔除（NFR-COR-03）——计数如实供
                              //   UI 标注数据缺失，行值绝不进曲线
            continue;
        }
        jc.t.push_back(row.t);                       // s
        jc.q.push_back(row.q);                       // rad 或 m
        jc.qd.push_back(row.qd);                     // rad/s 或 m/s
        jc.qdd.push_back(row.qdd);                   // rad/s² 或 m/s²
        jc.generalizedForce.push_back(row.tauTotal); // N·m 或 N
        jc.mechanicalPower.push_back(row.mechanicalPower); // W
    }
    return out;
}

// =====================================================================
// 回放投影器（replay-at 数据面：构建＋插值查表）。
// =====================================================================

ReplayData DynamicsReplayProjector::buildReplayData(const DynamicsSeries& series) const
{
    // ---- 第 1 步：行序防御校验＋Ok 关节表推导（共享第一步）----
    std::vector<std::uint32_t> okJoints;
    validateRowOrderAndCollectJoints(series, okJoints);

    ReplayData out;
    out.conditionId = series.conditionId;
    out.sourceSeriesId = series.contentIdentity;
    out.completeness = series.validity.completeness;
    out.jointCount = okJoints.size();
    if (okJoints.empty()) {
        return out;  // Empty 显式语义：frames 空——无 Ok 行不伪造帧
    }

    // ---- 第 2 步：按 t 分组建帧（行序已校验：同刻度 Ok 行连续且关节
    //      升序——顺序收集即帧内关节升序，零重排）----
    bool haveCurFrame = false;      // 是否正在组帧（首帧惰性开启）
    for (const DynamicsSample& row : series.samples) {
        if (row.numericState != SampleNumericState::Ok) {
            continue;  // 非 Ok 行不进帧（NFR-COR-03）——帧完整性由
                       //   completeAllJoints 标注缺失
        }
        if (!haveCurFrame || out.frames.back().t != row.t) {
            // 新时刻→新帧（t 严格递增由行序校验保证——同刻度行已在上帧
            // 收完，重复/倒退在校验步已 fail-fast）。
            ReplayFrame frame;
            frame.t = row.t;                    // s
            frame.segmentIndex = row.segmentIndex; // 段结构直通（回放定位键）
            frame.completeAllJoints = false;    // 收行后统一回填
            out.frames.push_back(std::move(frame));
            haveCurFrame = true;
        }
        out.frames.back().joints.push_back(stateFromRow(row));
    }

    // ---- 第 3 步：帧完整性回填（帧行数≠全 Ok 关节数＝该时刻存在非 Ok
    //      行/缺行——帧仍交付，缺失如实标注：不截断伪造，§4.6 精神）----
    for (ReplayFrame& frame : out.frames) {
        frame.completeAllJoints = frame.joints.size() == okJoints.size();
    }
    return out;
}

std::optional<ReplaySample> DynamicsReplayProjector::sampleAt(const ReplayData& data,
                                                              double t) const
{
    // ---- 第 1 步：查询时刻有限性（调用方错误 fail-fast——NaN/±Inf 无
    //      查询意义，NFR-COR-03 不静默）----
    if (!std::isfinite(t)) {
        throw DynamicsError(
            "replay-time-invalid",
            "回放查询时刻非有限：t=" + std::to_string(t)
                + " s（NaN/±Inf 不构成合法时刻——调用方错误）");
    }

    // ---- 第 2 步：空集空态（合法空态——不伪造帧）----
    if (data.frames.empty()) {
        return std::nullopt;
    }

    const ReplayFrame& first = data.frames.front();
    const ReplayFrame& last = data.frames.back();

    // ---- 第 3 步：越界空态（不外推——§4.3"不平滑、不插值外推"纪律：
    //      越界时刻的数据不存在，呈现侧应停在端点而非虚构运动）----
    if (t < first.t || t > last.t) {
        return std::nullopt;
    }

    // ---- 第 4 步：二分定位区间（upper_bound：首个 t帧 > 查询 t 的帧——
    //      查询 t 所在区间为 [idx-1, idx]；t==末帧时 upper_bound 返回
    //      end()，但 t==last.t 已在下方精确命中分支处理）----
    // 谓词签名＝comp(value, element)——value（查询时刻）在前、元素（帧）
    // 在后：query < f.t 为 true 的首帧即首个大于查询时刻的帧。
    const auto frameAfterQuery = [](double query, const ReplayFrame& f) {
        return query < f.t;
    };
    const auto it = std::upper_bound(data.frames.begin(), data.frames.end(), t,
                                     frameAfterQuery);

    // ---- 第 5 步：精确命中检查（upper_bound 前一帧时刻==查询 t 即帧点
    //      ——原值直拷，exact=true）----
    if (it != data.frames.begin()) {
        const ReplayFrame& candidate = *(it - 1);
        if (candidate.t == t) {
            ReplaySample hit;
            hit.exact = true;
            hit.t = t;
            hit.segmentIndex = candidate.segmentIndex; // 帧段号直通
            hit.joints = candidate.joints;             // 帧原值拷贝（零混合）
            hit.completeAllJoints = candidate.completeAllJoints;
            return hit;
        }
    }

    // ---- 第 6 步：区间线性插值（exact=false——显示投影语义：插值点仅
    //      是相邻样本的线性过渡呈现，τ·q̇ 恒等式与能量积分只在样本点
    //      成立，插值值不得回流统计/评估）----
    // it 必非 begin()（t ≥ first.t 已保证）且必非 end()（t < last.t——
    // t==last.t 已走精确命中），区间两端均存在。
    const ReplayFrame& left = *(it - 1);
    const ReplayFrame& right = *it;

    // 插值系数 α∈[0,1)：分母 tR−tL>0 由帧 t 严格递增保证（构建步纪律）。
    // 防御：若出现相等（构造违约）拒绝插值——不产生除零。
    if (!(right.t > left.t)) {
        throw DynamicsError(
            "series-non-monotonic",
            "回放数据帧时间非严格递增：tL=" + std::to_string(left.t)
                + " s，tR=" + std::to_string(right.t) + " s（buildReplayData "
                "产物不可能形态——数据集违约）");
    }
    const double alpha = (t - left.t) / (right.t - left.t); // 无量纲 ∈ (0,1)

    ReplaySample out;
    out.exact = false;
    out.t = t;
    // 段归属取左帧：区间 [tL,tR) 归左段（保守边界——同 §5.4"样本时刻
    // t≥tEvent 生效"的左闭精神；exact 分支为帧段号直通）。
    out.segmentIndex = left.segmentIndex;

    // ---- 第 7 步：逐关节双指针对齐插值（两帧 joints 均 jointIndex 升序
    //      ——合并遍历零重排；共有关节线性混合，单侧独有关节取单侧值
    //      并置不完整位——无对侧数据源，不跨帧虚构）----
    out.completeAllJoints = true;
    std::size_t i = 0;  // 左帧关节游标
    std::size_t j = 0;  // 右帧关节游标
    while (i < left.joints.size() || j < right.joints.size()) {
        if (j >= right.joints.size()
            || (i < left.joints.size() && left.joints[i].jointIndex < right.joints[j].jointIndex)) {
            // 仅左帧有该关节：取左值（右无数据源——单侧值直拷）。
            out.joints.push_back(left.joints[i]);
            out.completeAllJoints = false;
            ++i;
        } else if (i >= left.joints.size()
                   || right.joints[j].jointIndex < left.joints[i].jointIndex) {
            // 仅右帧有该关节：取右值（左无数据源）。
            out.joints.push_back(right.joints[j]);
            out.completeAllJoints = false;
            ++j;
        } else {
            // 两帧共有关节：五量线性混合 q_mix=(1−α)·qL+α·qR（量纲同源）。
            const ReplayJointState& l = left.joints[i];
            const ReplayJointState& r = right.joints[j];
            ReplayJointState mixed;
            mixed.jointIndex = l.jointIndex;
            mixed.jointObjectId = l.jointObjectId; // 同关节恒同 id（取左）
            mixed.jointType = l.jointType;         // 同关节恒同类型
            mixed.q = (1.0 - alpha) * l.q + alpha * r.q;                             // rad|m
            mixed.qd = (1.0 - alpha) * l.qd + alpha * r.qd;                          // rad/s|m/s
            mixed.qdd = (1.0 - alpha) * l.qdd + alpha * r.qdd;                       // rad/s²|m/s²
            mixed.generalizedForce = (1.0 - alpha) * l.generalizedForce
                                     + alpha * r.generalizedForce;                   // N·m|N
            mixed.mechanicalPower = (1.0 - alpha) * l.mechanicalPower
                                    + alpha * r.mechanicalPower;                     // W
            out.joints.push_back(std::move(mixed));
            ++i;
            ++j;
        }
    }
    return out;
}

// =====================================================================
// 峰值定位器（locate-peak 数据面——消费 T04 computePeaks 行集）。
// =====================================================================

std::optional<PeakRecord> DynamicsPeakLocator::locate(const DynamicsSeries& series,
                                                      const std::vector<PeakRecord>& peaks,
                                                      std::uint32_t jointIndex,
                                                      int tokenIndex) const
{
    // ---- 第 1 步：token 词表校验（越界＝调用方错误 fail-fast——Envelope.
    //      hpp token 表为封闭词表：0 力矩正/1 力矩反/2 速度/3 加速度/
    //      4 功率正/5 功率反）----
    if (tokenIndex < 0 || tokenIndex >= kPeakTokenCount) {
        throw DynamicsError(
            "peak-token-out-of-range",
            "峰值定位 token 序号越界：tokenIndex=" + std::to_string(tokenIndex)
                + "（词表 [0, " + std::to_string(kPeakTokenCount) + ")）");
    }

    // ---- 第 2 步：Ok 关节表推导（行序解读锚——共享校验第一步；这里也
    //      隐含对 series 行序的防御校验）----
    std::vector<std::uint32_t> okJoints;
    validateRowOrderAndCollectJoints(series, okJoints);
    if (okJoints.empty()) {
        return std::nullopt;  // 空序列/无 Ok 行→无峰值可定位（合法空态）
    }

    // ---- 第 3 步：目标关节不在 Ok 关节表→无峰值（合法空态——显式
    //      未找到，不伪造零值记录）----
    const auto pos = std::find(okJoints.begin(), okJoints.end(), jointIndex);
    if (pos == okJoints.end()) {
        return std::nullopt;
    }

    // ---- 第 4 步：行集结构对账（行数＝Ok 关节数×每关节行数——不等＝
    //      行集与序列不同源/被截断，定位将错位：fail-fast 暴露，比静默
    //      错位安全）----
    const std::size_t expectedRows = okJoints.size() * kPeaksPerJoint;
    if (peaks.size() != expectedRows) {
        throw DynamicsError(
            "peaks-series-mismatch",
            "峰值行集与序列结构不同源：peaks.size()=" + std::to_string(peaks.size())
                + "，期望=" + std::to_string(expectedRows) + "（Ok 关节数 "
                + std::to_string(okJoints.size()) + "×每关节 "
                + std::to_string(kPeaksPerJoint) + " 行——行集须为 "
                "computePeaks(series) 产物）");
    }

    // ---- 第 5 步：按行序定位（computePeaks 行序＝(jointIndex 升序, 每
    //      关节 token 序)——位次×每关节行数＋token 序号即行下标）----
    const std::size_t jointRank = static_cast<std::size_t>(pos - okJoints.begin());
    return peaks[jointRank * kPeaksPerJoint + static_cast<std::size_t>(tokenIndex)];
}

}  // namespace sdurws::ird::dynamics
