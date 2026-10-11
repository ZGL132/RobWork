/**
 * @file   ProcessSpeed.cpp
 * @brief  工艺速度约束校验的唯一实现翻译单元（TRJ-08-S2——WP-16-T15；
 *         指定段 TCP 速度受限/恒定的测量、断言与超限诊断）。
 *
 * 设计依据（头文件 ProcessSpeed.hpp 的设计依据此处不重复；本注登记实
 * 现补全——DTB §5.4 偏差/补全登记，单元卡 §22 同批）：
 *   - **TCP 速度的测量口径（实现补全）**：TRJ-08-S2 行只给语义"指定段
 *     TCP 速度受限或恒定"，测量的离散化口径由本实现登记——
 *       v(t) ≈ ‖P(t+δ) − P(t−δ)‖ / (2δ)（中心差分，δ 为差分半步）
 *     P(·) 为该时刻的 TCP 位置（基座系 {B} 平移部，m——经
 *     IKinematicsComputePort::evaluateFk 消费 kin.pose-metrics 语义；
 *     R-1：本域零 kinematics 编译依赖）。δ 取
 *       δ = min(kFiniteDiffDeltaS, t, totalDurationS − t)
 *     （双侧余量收缩——邻域恒在 ITimeCurve 求值域 [0, totalDurationS]
 *     内；t 恰在端点 0/totalDurationS 时双侧余量为 0，退化为单侧差分
 *     ‖P(t+δ) − P(t)‖/δ 或 ‖P(t) − P(t−δ)‖/δ）。中心差分截断误差
 *     O(δ²)（单侧 O(δ)）——kFiniteDiffDeltaS 为实现参数（非协议数值；
 *     与黄金算例的解析对照精度见 test/ProcessSpeedTest.cpp 头注声明）。
 *     峰值检测网格＝采样计划（sampleStepS 均匀划分＋端点必含）——网格
 *     峰值是真实峰值的下界近似（欠估），判定在"同一采样计划"下自洽
 *     （检测与复验同口径——§6.3"采样计划入身份"精神；TimeParam.cpp
 *     峰值统计口径同源登记）。
 *   - **判定公式（协议出处）**：
 *       Limited 达标：peak ≤ limit·(1 + 1×10⁻⁹) + ε_abs
 *         ——附录 D 第 10 项"相对 1×10⁻⁹（固定）"的单向应用（TimeParam.
 *         cpp withinLimit 同公式单向化登记同源：欠估是合法达标态，双向
 *         一致性判定不适用于上界校验）；相对分量直接引用
 *         kTimeParamLimitRelativeTolerance 常量（唯一书写点——禁第二处
 *         字面量）；ε_abs＝core::runtimeAbsoluteTolerance(LinearVelocity)
 *         转写（C7 运行校验默认 1×10⁻⁹——D-06 单点，本域零自设数值。
 *         TCP 线速度取 LinearVelocity 族——量纲诚实；与 TimeParam.cpp
 *         取 Angular 族的选择各自贴量纲，两族 C7 默认同值使结论不变，
 *         测试钉扎）。
 *       Constant 达标：全部采样点 v_i ∈ [v_set·(1−band) − ε_abs,
 *         v_set·(1+band) + ε_abs]（band＝请求面显式相对半宽——附录 D
 *         无恒定带默认条目，"无默认且无配置来源的量不得引入产品侧相对
 *         校验"，故强制调用方给出；ε_abs 为浮点吸收下限，工程容差量级
 *         由 band 决定）。违例素材取**首个越差点**（最小采样时刻——确
 *         定性定位，NFR-COR-02），比较型三要素＝实测速度/设定值/单位
 *         m/s（直值形态——core 单位注册表 "m/s" 词形在册，与 jerk 超限
 *         素材的倍数形态不同：m/s 是已注册 SI 词形，直值承载量纲诚实）。
 *   - **诊断码复用（零新增码）**：违例素材 reasonToken＝TRJ-LIMIT-
 *     EXCEEDED（§14.4 行 5——"速度/加速度/限位超标"的"限制超标"语义
 *     本体；T14 批 jerk 超限同码复用先例——工艺速度超限/越带同为"限
 *     制超标"家族，码表 12 码零扩保持；码描述符 requiresComparison=
 *     true 与比较型素材三要素匹配）。phaseToken 新增常量
 *     kPhaseProcessSpeedCheck＝"process-speed-check"（§14.1.6 词表表演
 *     追加——T04 批 plan-sequence 表外增补先例同款：素材阶段定位需要
 *     本校验维度的独立词形；DTB §5.4 增量登记）。
 *   - **素材组装序（确定性）**：violationRecords 序＝span 序×（Limited
 *     每段至多一条峰值素材 / Constant 每段至多一条首越差素材）——素材
 *     不重排、不合并，FailureLocate 输入序=输出序纪律同源（§14.4）；
 *     segmentObservations 序＝请求 spans 序原样（逐段观测与素材一一可
 *     对账）。
 *   - **端口层分流（§15.4 先例）**：FkPortReply.status==PortError → 原
 *     errorToken/errorMessage 显性抛 TrajectoryError（不转素材：素材轨
 *     只承载用户数据的合法失败结局，基础设施故障必须显性失败——Carte-
 *     sianLine.cpp 同款分流）；==Canceled → 校验以 Canceled 收尾（取消
 *     不是错误，UX-03——零错误素材）。
 *   - 需求 TRJ-08-S2、TRJ-06（比较型素材轨）、NFR-COR-01/02/03、ARC-05
 *     （见 ProcessSpeed.hpp）；错误二分见 Errors.hpp。
 *
 * 集成模式条件源（TimeParam.cpp 同款 gating）：本 TU 经 ITimeCurve::
 * sampleAt 消费 rw::math::Q 的构造/析构符号（Q 虚析构外联于框架库
 * sdurw_math）——冒烟模式无框架库可链，不编译本 TU；ProcessSpeed.hpp
 * 的值类型/接口声明两模式皆可编译。
 */

#include <sdurws/ird/trajectory/ProcessSpeed.hpp>

#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <rw/math/Q.hpp>
#include <rw/math/Vector3D.hpp>

#include <sdurws/ird/core/Compare.hpp>      // core::runtimeAbsoluteTolerance（C7 转写——D-06 单点）
#include <sdurws/ird/core/Provenance.hpp>   // core::SourcedValue/ValueProvenance（比较型素材值面）
#include <sdurws/ird/core/Units.hpp>        // core::QuantityKind/UnitToken（线速度量纲转写与 m/s token）
#include <sdurws/ird/trajectory/DiagCodes.hpp>  // TRJ-* 码常量（素材 reasonToken 唯一书写点）
#include <sdurws/ird/trajectory/Errors.hpp>     // TrajectoryError（fail-fast/显性失败载体）
#include <sdurws/ird/trajectory/KinematicsPort.hpp>  // IKinematicsComputePort/FkPortRequest（③端口完整类型）

namespace sdurws::ird::trajectory {
namespace {

// 失败阶段 token：工艺速度校验（FailedSegmentRecord.phaseToken 产生面——
// §14.1.6 phase 词表表尾追加，WP-16-T15 批；T04 批 plan-sequence 实现
// TU 内部常量先例同款〔Sequence.cpp kPhasePlanSequence——词表增补的实
// 现落位不外溢头文件，测试以字面量断言值〕）。
//
// 词形契约：kebab 小写、与六值词表同形（plan-ptp/plan-line/plan-avoid/
// smooth/recheck/time-param + plan-sequence 增补——本校验维度独立成词）。
const char kPhaseProcessSpeedCheck[] = "process-speed-check";


// =====================================================================
// 算法内部常量（实现参数——非协议数值，依据见文件头注登记）
// =====================================================================

/// TCP 速度差分的标称半步（s）：中心差分截断误差 O(δ²) 的实现参数——
/// 取 1×10⁻³ s 使黄金算例（解析曲线）的测量偏差远低于限值判定容差
/// （1×10⁻⁹ 相对）对应的量级；δ 随区间余量自适应收缩（见 tcpSpeedAt）。
/// 非协议数值（附录 D 无差分步条目）——测量口径入实现登记，采样计划
/// （sampleStepS）才是请求面身份字段。
inline constexpr double kFiniteDiffDeltaS = 1e-3;

// =====================================================================
// C7 ε_abs 转写与单位 token（D-06 单点——TimeParam.cpp 同款口径）
// =====================================================================

/**
 * @brief TCP 线速度判定的绝对容差（m/s；core::runtimeAbsoluteTolerance
 *        的 LinearVelocity 族转写——C7 默认 1×10⁻⁹）。
 *
 * 量纲声明：TCP 速度是任务空间线速度（基座系 {B} 下位姿平移部的时间
 * 导数模长）——取 LinearVelocity 族（与 TimeParam.cpp 关节域取 Angular
 * 族各自贴量纲；两族 C7 默认同值 1×10⁻⁹，结论不受选择影响，测试钉扎）。
 * 转写对已声明量纲恒有值（core 侧 D-06 表）——nullopt 属实现缺陷。
 */
double linearVelocityAbsTolerance()
{
    const std::optional<double> eps =
        core::runtimeAbsoluteTolerance(core::QuantityKind::LinearVelocity);
    if (!eps.has_value()) {
        throw TrajectoryError("trajectory/process-speed/tolerance-missing",
                              "core C7 转写缺失 LinearVelocity 默认（实现缺"
                              "陷——D-06 表不完整）");
    }
    return *eps;
}

/**
 * @brief 取线速度单位 token（"m/s"——core 单位注册表编译期冻结在册词
 *        形；违例素材比较型的单位承载面。find 失败属 core 注册表缺陷，
 *        Ptp.cpp/Smooth.cpp 同款 fail-fast 口径）。
 */
core::UnitToken linearVelocityUnitToken()
{
    const auto token = core::UnitToken::find("m/s");
    if (!token.has_value()) {
        throw TrajectoryError("trajectory/process-speed/unit-token",
                              "core 单位注册表缺少 m/s token（环境缺陷，"
                              "fail-fast——比较型素材单位口径不可用）");
    }
    return *token;
}

// =====================================================================
// 前置校验（调用方契约违约 fail-fast——校验序固定，NFR-COR-02）
// =====================================================================

/**
 * @brief 请求合法域全表校验（checkProcessSpeed 步骤 1；非法即抛——文
 *        案含首错定位与实际值）。
 *
 * 校验序（确定性——同一坏请求必报同一首错）：
 *   1. timeCurve 非空（无时间化产物无工艺速度——§12.4 不伪造同源）；
 *   2. totalDurationS 有限且 >0（s）；
 *   3. sampleStepS 有限且 >0（s）；
 *   4. spans 非空时 fkPort 非空（**未校验绝不冒充满足**——KIN-05 家
 *      族纪律的调用侧承载）；
 *   5. 逐 span：区间有限有序 [tStart,tEnd] ⊆ [0,totalDurationS] 且
 *      tStart<tEnd；约束值合法域（速度 >0 有限；Constant 带 ∈(0,1)；
 *      Limited 零带——非零即违约，NFR-COR-03 拒绝优于静默忽略）。
 *
 * 注：spans 为空是合法输入（空结果口径——头注），故 fkPort 空只在
 * spans 非空时违约（第 4 步在循环外单独判）。
 */
void validateRequest(const ProcessSpeedCheckRequest& request)
{
    // 1. 时间化产物连续求值面（空 shared_ptr＝调用方以默认构造请求蒙混
    //    ——工艺速度是时间导数量，无曲线即无校验语义）。
    if (request.timeCurve == nullptr) {
        throw TrajectoryError("trajectory/process-speed/curve-missing",
                              "时间化产物连续求值面缺失（timeCurve 为空——"
                              "工艺速度校验的输入必须是 T08 Ok 态产物）");
    }
    // 2. 总时长（区间校验的基准域；有限正——零长轨迹无校验意义）。
    if (!std::isfinite(request.totalDurationS)
        || !(request.totalDurationS > 0.0)) {
        throw TrajectoryError("trajectory/process-speed/duration",
                              "轨迹总时长非法：totalDurationS="
                                  + std::to_string(request.totalDurationS)
                                  + " s（要求 >0 且有限）");
    }
    // 3. 采样步长（s——采样计划的身份字段，非法值破坏确定性口径）。
    if (!std::isfinite(request.sampleStepS) || !(request.sampleStepS > 0.0)) {
        throw TrajectoryError("trajectory/process-speed/sample-step",
                              "sampleStepS 非法：实际 "
                                  + std::to_string(request.sampleStepS)
                                  + " s（要求 >0 且有限）");
    }
    // 4. FK 端口在位（spans 非空时强制——端口缺失即校验不可执行，缺省
    //    通过会让"未校验"冒充"满足"，KIN-05 家族纪律）。
    if (!request.spans.empty() && request.fkPort == nullptr) {
        throw TrajectoryError("trajectory/process-speed/port-missing",
                              "受检段非空但 FK 端口缺失（fkPort 为空——TCP"
                              " 速度消费必须经 kinematics ③端口，R-1；不得"
                              "静默跳过校验）");
    }
    // 5. 逐 span 合法域（区间/约束值；首错即抛——文案携带 span 序号）。
    for (std::size_t idx = 0; idx < request.spans.size(); ++idx) {
        const ProcessSpeedSegmentSpan& span = request.spans[idx];
        // 区间：有限、有序、落在轨迹时轴内（端点差分邻域以 totalDurationS
        // 为界收缩——越界区间没有可测量的邻域语义）。
        if (!std::isfinite(span.tStartS) || !std::isfinite(span.tEndS)
            || !(span.tStartS >= 0.0) || !(span.tEndS <= request.totalDurationS)
            || !(span.tStartS < span.tEndS)) {
            throw TrajectoryError(
                "trajectory/process-speed/span-range",
                "受检区间非法：spans[" + std::to_string(idx)
                    + "] segmentIndex=" + std::to_string(span.segmentIndex)
                    + "，[" + std::to_string(span.tStartS) + ", "
                    + std::to_string(span.tEndS) + "] s（要求有限且 0 ≤ "
                    "tStart < tEnd ≤ totalDurationS=" +
                    std::to_string(request.totalDurationS) + "）");
        }
        // 速度规格值：>0 且有限（0/负值无工程意义——"TCP 速度 ≤ 0"或
        // "恒定为 0"都不是工艺规格；NaN/±inf 拒绝）。
        if (!std::isfinite(span.constraint.tcpSpeedMps)
            || !(span.constraint.tcpSpeedMps > 0.0)) {
            throw TrajectoryError(
                "trajectory/process-speed/speed-value",
                "工艺速度规格值非法：spans[" + std::to_string(idx)
                    + "] tcpSpeedMps="
                    + std::to_string(span.constraint.tcpSpeedMps)
                    + " m/s（要求 >0 且有限）");
        }
        if (span.constraint.mode == ProcessSpeedMode::Constant) {
            // 恒定带：相对半宽 ∈(0,1)（强制显式——附录 D 无恒定带默认，
            // 本域零自设数值；带=0 等价于无限严判定无工程语义，带≥1 无
            // 约束语义）。
            if (!std::isfinite(span.constraint.constantBandRelative)
                || !(span.constraint.constantBandRelative > 0.0)
                || !(span.constraint.constantBandRelative < 1.0)) {
                throw TrajectoryError(
                    "trajectory/process-speed/constant-band",
                    "恒定带相对半宽非法：spans[" + std::to_string(idx)
                        + "] constantBandRelative="
                        + std::to_string(span.constraint.constantBandRelative)
                        + "（Constant 模式要求 ∈(0,1) 且有限——附录 D 无"
                          "默认条目，必须由调用方显式给出）");
            }
        } else {
            // Limited 模式不消费恒定带：携带非零值＝调用方契约违约
            // （静默忽略会使"约束写了什么"与"验了什么"脱钩——NFR-COR-
            // 03；T14 批 quintic 拒绝有限 jerkLimit 同款纪律）。
            if (span.constraint.constantBandRelative != 0.0) {
                throw TrajectoryError(
                    "trajectory/process-speed/band-not-consumed",
                    "Limited 模式不消费恒定带：spans["
                        + std::to_string(idx) + "] constantBandRelative="
                        + std::to_string(span.constraint.constantBandRelative)
                        + "（要求 0.0——恒定带仅 Constant 模式有效）");
            }
        }
    }
}

// =====================================================================
// TCP 位置与速度测量（FK 端口消费——R-1：经③端口镜像，零 kinematics 链）
// =====================================================================

/**
 * @brief 求时刻 t 的 TCP 位置（基座系 {B} 平移部，m）。
 *
 * 消费链：timeCurve.sampleAt(t) 取权威关节向量 q（TimedSample.q——Q
 * 链序）→ FK 端口 evaluateFk（kin.pose-metrics 语义透传）→
 * tcpInBase.P()（基座系 {B} 平移部——core §4.6 T_base_tcp 读法）。
 *
 * 端口层分流（§15.4 先例——CartesianLine 同款）：
 *   - PortError → 原 errorToken/errorMessage 显性抛 TrajectoryError
 *     （基础设施故障不转素材、不吞错不静默降级）；
 *   - Canceled → 返回 false（调用方以取消态收尾——取消不是错误）。
 *
 * @param t        [in] 时刻（s；∈[0, totalDurationS]——越界属调用方违
 *                 约，ITimeCurve 侧 fail-fast）
 * @param request  [in] 校验请求（端口/取消观测来源）
 * @param position [out] TCP 位置（基座系 {B}；m）
 * @return true＝测量成功；false＝取消观测命中（零素材收尾）
 *
 * @throws TrajectoryError 端口层故障（"trajectory/kin-port-*" token 透传）
 */
bool tcpPositionAt(double t, const ProcessSpeedCheckRequest& request,
                   rw::math::Vector3D<double>& position)
{
    // 该时刻的权威关节向量（q 链序——TimedSample.q；sampleAt 越界由
    // ITimeCurve 契约 fail-fast，本域采样计划保证 t 在域内）。
    const TimedSample sample = request.timeCurve->sampleAt(t);
    FkPortRequest fkReq;
    fkReq.q.reserve(static_cast<std::size_t>(sample.q.size()));
    for (std::size_t i = 0; i < static_cast<std::size_t>(sample.q.size());
         ++i) {
        fkReq.q.push_back(sample.q[i]);  // Q 逐轴 → 端口向量（rad|m 链序）
    }
    fkReq.cancel = request.cancel;
    const FkPortReply reply = request.fkPort->evaluateFk(fkReq);
    if (reply.status == KinPortCallStatus::PortError) {
        // 端口层错误＝环境故障（适配失败/契约不匹配）——按 §15.0 以原
        // token 显性抛出（不转素材：素材轨只承载用户数据的合法失败结局）。
        throw TrajectoryError(reply.errorToken, reply.errorMessage);
    }
    if (reply.status == KinPortCallStatus::Canceled) {
        return false;  // 取消观测命中（端口侧）——调用方以取消态收尾
    }
    position = reply.metrics.tcpInBase.P();
    return true;
}

/**
 * @brief 求时刻 t 的 TCP 线速度（m/s——基座系 {B} 下位姿平移部的时间
 *        导数模长的差分测量）。
 *
 * 差分口径（文件头注登记）：优先中心差分 ‖P(t+δ)−P(t−δ)‖/(2δ)（截断
 * O(δ²)）；t 在轨迹时轴端点（δ 双侧余量为 0）退化单侧差分（截断 O(δ)
 * ——端点速度测量精度低于内部点，属于测量口径的诚实登记，非判定缺陷：
 * 端点采样仍参与判定，黄金用例对端点口径独立锁定）。
 *
 * @param t       [in] 时刻（s；∈[0, totalDurationS]）
 * @param request [in] 校验请求（端口/曲线/取消来源）
 * @param speed   [out] TCP 线速度测量值（m/s；≥0）
 * @return true＝测量成功；false＝取消观测命中
 *
 * @throws TrajectoryError 端口层故障（透传）
 */
bool tcpSpeedAt(double t, const ProcessSpeedCheckRequest& request,
                double& speed)
{
    // 差分半步：标称值与双侧余量取严——邻域恒在求值域内（ITimeCurve 越
    // 界 fail-fast 的前置保证）。
    double delta = std::min(
        {kFiniteDiffDeltaS, t, request.totalDurationS - t});
    rw::math::Vector3D<double> pLo;
    rw::math::Vector3D<double> pHi;
    if (delta > 0.0) {
        // 中心差分（内部点——截断 O(δ²)）。
        if (!tcpPositionAt(t - delta, request, pLo)) { return false; }
        if (!tcpPositionAt(t + delta, request, pHi)) { return false; }
        speed = (pHi - pLo).norm2() / (2.0 * delta);
        return true;
    }
    // 双侧余量为 0＝t 恰在时轴端点——单侧差分（截断 O(δ)，端点口径的
    // 诚实登记见函数注）。
    if (t <= 0.0) {
        delta = std::min(kFiniteDiffDeltaS, request.totalDurationS);
        if (!tcpPositionAt(0.0, request, pLo)) { return false; }
        if (!tcpPositionAt(delta, request, pHi)) { return false; }
        speed = (pHi - pLo).norm2() / delta;
        return true;
    }
    delta = std::min(kFiniteDiffDeltaS, request.totalDurationS);
    if (!tcpPositionAt(request.totalDurationS - delta, request, pLo)) {
        return false;
    }
    if (!tcpPositionAt(request.totalDurationS, request, pHi)) { return false; }
    speed = (pHi - pLo).norm2() / delta;
    return true;
}

// =====================================================================
// 比较型素材构造辅助（UX-03 三要素——来源标记单点化）
// =====================================================================

/**
 * @brief 派生量的来源标记（本域对轨迹产物的派生测量量——ValueProvenance
 *        五类词表 DerivedReadOnly；methodTag 标注产生面——TimeParam.cpp
 *        derivedProvenance 同款纪律）。
 */
inline core::ValueProvenance derivedProvenance(const char* methodTag)
{
    return core::ValueProvenance::make(core::ProvenanceKind::DerivedReadOnly,
                                       {}, {}, methodTag);
}

/**
 * @brief 组装单条工艺速度违例素材（TRJ-LIMIT-EXCEEDED 比较型——实际/
 *        期望/单位 m/s 直值＋段定位 segmentIndex/pathParameter/subject；
 *        §14.4 行 5"限制超标"语义本体、TRJ-06"具体段落与原因"）。
 *
 * @param segmentIndex [in] 违例段序号（请求 span 的定位键）
 * @param pathParam    [in] 违例点段内归一化参数（无量纲 ∈[0,1]；Limited
 *                     ＝峰值所在采样点的 s，Constant＝首个越差采样点的 s）
 * @param subject      [in] 段来源任务点投影（可空——透传）
 * @param cause        [in] 中文原因（ERR-01——违例事实＋实测值）
 * @param action       [in] 中文建议动作（ERR-01）
 * @param actual       [in] 实测 TCP 速度（m/s——峰值或越差点值）
 * @param expected     [in] 期望规格（m/s——限值或设定值）
 * @param unit         [in] 单位 token（"m/s"——在册词形）
 */
FailedSegmentRecord makeViolationRecord(std::uint32_t segmentIndex,
                                        double pathParam,
                                        std::optional<core::ObjectId> subject,
                                        std::string cause,
                                        std::string action, double actual,
                                        double expected,
                                        const core::UnitToken& unit)
{
    FailedSegmentRecord rec;
    rec.segmentIndex = segmentIndex;
    rec.phaseToken = kPhaseProcessSpeedCheck;  // "process-speed-check"（§14.1.6 表尾追加）
    rec.reasonToken = std::string(kTrjLimitExceeded);  // 限制超标语义本体——零新增码
    rec.pathParameter = pathParam;
    rec.subject = std::move(subject);
    rec.cause = std::move(cause);
    rec.recommendedAction = std::move(action);
    // 比较型三要素（UX-03）：m/s 直值形态（core 注册表在册词形——与
    // jerk 超限素材的倍数形态不同，见文件头注登记）。
    core::ComparativeFields cmp;
    cmp.actual.quantity = core::SourcedValue<double>::provided(
        actual, derivedProvenance("process-speed-measured"));
    cmp.actual.unit = unit;
    cmp.expected.quantity = core::SourcedValue<double>::provided(
        expected, derivedProvenance("process-speed-spec"));
    cmp.expected.unit = unit;
    rec.comparison = std::move(cmp);
    return rec;
}

}  // namespace

// =====================================================================
// §19.1 S2 校验入口（执行序见 ProcessSpeed.hpp 函数注）
// =====================================================================

ProcessSpeedCheckResult checkProcessSpeed(const ProcessSpeedCheckRequest& request)
{
    // ---- 步骤 1：前置校验（调用方契约违约 fail-fast——validateRequest）。
    validateRequest(request);

    // ---- 步骤 2：取消轮询（命中 → Canceled，零素材——UX-03；空 spans
    // 的合法空结果在取消检查之后同样成立——零受检段与取消同为零素材面，
    // 语义区分靠 status：未请求取消且无段 → Ok）。
    if (request.cancel && request.cancel()) {
        ProcessSpeedCheckResult result;
        result.status = ProcessSpeedStatus::Canceled;
        return result;
    }

    // 空受检段序列＝合法空结果（零校验零素材零观测——T09"空素材序列＝
    // 合法空结果"同口径；Ok 态空观测）。
    if (request.spans.empty()) {
        ProcessSpeedCheckResult result;
        result.status = ProcessSpeedStatus::Ok;
        return result;
    }

    // ---- 判定容差与单位（C7/D-06 单点转写——文件头注登记）。
    const double epsAbs = linearVelocityAbsTolerance();
    const core::UnitToken mpsUnit = linearVelocityUnitToken();
    // 受限判定的相对容差：附录 D 第 10 项常量复用（唯一书写点——TimePa-
    // ram.hpp 定义，禁第二处字面量）。
    const double relTol = kTimeParamLimitRelativeTolerance;

    // ---- 步骤 3：逐受检段校验（请求 spans 序——确定性；违例不中断后
    // 续段——全量诊断面，素材序＝span 序×段内时刻序）。
    ProcessSpeedCheckResult result;
    bool anyViolation = false;
    for (const ProcessSpeedSegmentSpan& span : request.spans) {
        // 迭代边界取消轮询（§15.1——每轮必查）。
        if (request.cancel && request.cancel()) {
            ProcessSpeedCheckResult canceled;
            canceled.status = ProcessSpeedStatus::Canceled;
            return canceled;
        }
        // ---- 3a. 采样计划：端点必含＋均匀划分（步长 ≤ sampleStepS——
        // divisions=ceil(时长/步长)，n=divisions+1 个时刻；同计划下检测
        // 与复验同口径——§6.3 精神）。
        const double spanLength = span.tEndS - span.tStartS;
        const std::size_t divisions =
            std::max<std::size_t>(
                1, static_cast<std::size_t>(
                       std::ceil(spanLength / request.sampleStepS)));
        const std::size_t sampleCount = divisions + 1;
        // ---- 3b/3c. 逐采样测量与模式判定。
        //  段级状态：峰值（Limited）与峰值定位（argmax 伴随记录——素材
        //  的 pathParameter 来源）；Constant 的首个越差点（最小 t——确
        //  定性定位）。
        bool spanSatisfied = true;
        double peakSpeed = 0.0;
        double peakS = 0.0;  // 峰值所在采样点的段内归一化参数（[0,1]）
        std::optional<std::size_t> firstOutlierIdx;  // Constant 首越差采样下标
        std::optional<double> firstOutlierSpeed;     // 首越差点实测速度（m/s）
        for (std::size_t j = 0; j < sampleCount; ++j) {
            // 采样时刻（端点 j=0/sampleCount-1 位级等于 tStart/tEnd——
            // 均匀划分的端点封闭性）。
            const double t =
                span.tStartS
                + spanLength
                      * (static_cast<double>(j)
                         / static_cast<double>(divisions));
            // 循环边界取消轮询（FK 调用前——§15.1 不做逐元素轮询，
            // 采样粒度即轮询粒度）。
            if (request.cancel && request.cancel()) {
                ProcessSpeedCheckResult canceled;
                canceled.status = ProcessSpeedStatus::Canceled;
                return canceled;
            }
            double speed = 0.0;
            if (!tcpSpeedAt(t, request, speed)) {
                // 端口/曲线侧取消命中——取消不是错误（UX-03）：零错误素
                // 材、零违例结论、观测为空。
                ProcessSpeedCheckResult canceled;
                canceled.status = ProcessSpeedStatus::Canceled;
                return canceled;
            }
            // 段内归一化参数（超限素材 pathParameter——§6.2 定位语义）。
            const double sNorm = static_cast<double>(j)
                                 / static_cast<double>(divisions);
            // 峰值统计（两模式统一——观测面的段级事实）。
            if (speed > peakSpeed || j == 0) {
                // j==0 保证首样本入账（含零速起点段——统计语义完整）。
                peakSpeed = speed;
                peakS = sNorm;
            }
            if (span.constraint.mode == ProcessSpeedMode::Limited) {
                // 受限达标判定（附录 D 第 10 项单向应用——欠估是合法达
                // 标态；公式与 ε_abs 出处见文件头注登记）。
                const double limit = span.constraint.tcpSpeedMps;
                if (!(speed <= limit * (1.0 + relTol) + epsAbs)) {
                    spanSatisfied = false;
                }
            } else {
                // 恒定带判定：v ∈ [v_set·(1−band)−ε_abs,
                // v_set·(1+band)+ε_abs]（相对带＋浮点吸收下限——出处见
                // 文件头注）。首越差点即定位（最小 t——确定性）。
                const double vSet = span.constraint.tcpSpeedMps;
                const double band = span.constraint.constantBandRelative;
                const double lo = vSet * (1.0 - band) - epsAbs;
                const double hi = vSet * (1.0 + band) + epsAbs;
                if (!firstOutlierIdx.has_value()
                    && (speed < lo || speed > hi)) {
                    firstOutlierIdx = j;
                    firstOutlierSpeed = speed;
                    spanSatisfied = false;
                }
            }
        }
        // ---- 3c'. 违例素材组装（Limited＝峰值素材；Constant＝首越差素
        // 材——每段至多一条，定位要素齐备）。
        if (!spanSatisfied) {
            anyViolation = true;
            if (span.constraint.mode == ProcessSpeedMode::Limited) {
                const double limit = span.constraint.tcpSpeedMps;
                result.violationRecords.push_back(makeViolationRecord(
                    span.segmentIndex, peakS, span.subject,
                    "工艺速度超限：段 TCP 速度峰值 "
                        + std::to_string(peakSpeed)
                        + " m/s 超过工艺上限 " + std::to_string(limit)
                        + " m/s（TRJ-08-S2 受限约束）",
                    "调整该段工艺速度上限或轨迹时间参数化（降低关节限值使"
                    "用比例）后重新评估",
                    peakSpeed, limit, mpsUnit));
            } else {
                const double vSet = span.constraint.tcpSpeedMps;
                const double band = span.constraint.constantBandRelative;
                const double outlierS =
                    static_cast<double>(*firstOutlierIdx)
                    / static_cast<double>(divisions);
                const double lo = vSet * (1.0 - band);
                const double hi = vSet * (1.0 + band);
                result.violationRecords.push_back(makeViolationRecord(
                    span.segmentIndex, outlierS, span.subject,
                    "工艺速度非恒定：段 TCP 速度 "
                        + std::to_string(*firstOutlierSpeed)
                        + " m/s 越出恒定带 [" + std::to_string(lo) + ", "
                        + std::to_string(hi) + "] m/s（设定值 "
                        + std::to_string(vSet) + " m/s，带 ±"
                        + std::to_string(band * 100.0) + "%——TRJ-08-S2"
                          " 恒定约束）",
                    "该段轨迹需按恒定工艺速度重新规划（恒定性是轨迹形状"
                    "属性，时间缩放不改变）或放宽恒定带后重新评估",
                    *firstOutlierSpeed, vSet, mpsUnit));
            }
        }
        // ---- 3d. 段观测入列（与 spans 同序——节拍分解细化面）。
        ProcessSpeedSegmentObservation observation;
        observation.segmentIndex = span.segmentIndex;
        observation.tStartS = span.tStartS;
        observation.tEndS = span.tEndS;
        observation.peakTcpSpeedMps = peakSpeed;
        observation.satisfied = spanSatisfied;
        result.segmentObservations.push_back(observation);
    }

    // ---- 步骤 4：结局组装（全部满足 → Ok；任一违例 → Violated）。
    result.status =
        anyViolation ? ProcessSpeedStatus::Violated : ProcessSpeedStatus::Ok;
    return result;
}

}  // namespace sdurws::ird::trajectory
