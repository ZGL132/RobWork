/**
 * @file   ContinuousProcess.cpp
 * @brief  连续工艺段边界行为校验的唯一实现翻译单元（TRJ-08-S3——
 *         WP-16-T16；段间速度连续断言＋驻留/不停驻切换明确断言）。
 *
 * 设计依据（头文件 ContinuousProcess.hpp 的设计依据此处不重复；本注登
 * 记实现补全——DTB §5.4 偏差/补全登记，单元卡 §22 同批）：
 *   - **两侧速度极限的一阶回推估计（实现补全）**：TRJ-08-S3 行只给语
 *     义"段间速度连续"，测量的离散化口径由本实现登记。ITimeCurve 在结
 *     点取左段极限（无右极限入口）；内点导数直接对照含 qdd·δ 一阶偏差
 *     （rad/s——qdd 常规量级下远大于判定容差，不可用）。本实现以一阶
 *     泰勒回推承载两侧极限估计：
 *       qd⁻ ≈ qd(t−δ) + qdd(t−δ)·δ   （左极限——内点解析值回推一阶项）
 *       qd⁺ ≈ qd(t+δ) − qdd(t+δ)·δ   （右极限——同上）
 *     qd/qdd 均为该内点的样条解析值（无差分误差），回推残差为二阶项
 *     O(jerk·δ²)。δ 的取值纪律：
 *       δ = min(neighborDeltaS, t_b, totalDurationS − t_b)
 *     （双侧余量收缩——邻域恒在 ITimeCurve 求值域 [0, totalDurationS]
 *     内；t_b 合法域已排除端点〔前置校验——严格内〕，故 δ >0 恒成立）。
 *     neighborDeltaS 为请求面显式携带（本域零自设默认——黄金算例声明
 *     所用值与残差量级声明见 test/ContinuousProcessTest.cpp 头注）；邻
 *     域内点落在相邻结点之间的"段内性"由调用方保证（δ 应小于边界到相
 *     邻结点的距离——结点时刻不是 ITimeCurve 契约的一部分，本域不可校
 *     验，文档化义务登记）。
 *   - **速度连续判定（协议出处）**：两侧估计的逐元素差经无穷范数对附
 *     录 D 第 10 项容差判定：
 *       ‖qd⁺ − qd⁻‖_∞ ≤ ε_rel·‖qd⁻‖_∞ + ε_abs
 *     ε_rel＝kTimeParamLimitRelativeTolerance（1×10⁻⁹ 相对——唯一书写
 *     点复用，禁第二处字面量）；ε_abs＝core C7 角速度量纲默认 1×10⁻⁹
 *     经 core::runtimeAbsoluteTolerance(AngularVelocity) 转写（D-06 单
 *     点；关节域取 Angular 族——TimeParam.cpp 同款口径，转动/移动关
 *     节混合链两族 C7 默认同值使结论不变）。相对分量以左估计范数为基
 *     准（连接起点；逐轴 |ref_i| ≤ ‖ref‖_∞，范数基准比逐元素基准更严
 *     ——方向安全不漏检）。违约素材期望值＝同一公式右端（判定容差本
 *     身——比较型三要素的"期望"）。
 *   - **切换语义判定（expectedSwitch 分派——零空分支）**：断言维＝速
 *     度（验收标准原文口径；加速度半区不入本面——见头文件注"断言维"
 *     段）：
 *       · Continuous（不停驻）：min(‖qd⁻‖_∞, ‖qd⁺‖_∞) ≥ minContinuous-
 *         Speed（关节速度无穷范数——混合关节链的范数计量口径＝逐轴取
 *         绝对值最大者，rad/s|m/s 逐轴混合计量声明随该字段）；低于阈值
 *         ＝**隐式停驻**（声称连续切换实际发生停顿——连续工艺段语义违
 *         约，TRJ-08-S3"切换明确"的反例形态之一）。素材实际值取两侧范
 *         数的较小者（取严侧——任一侧停驻即停驻）。
 *       · Dwell（驻留）：max(‖qd⁻‖_∞, ‖qd⁺‖_∞) ≤ ε_abs(角速度)（§12.1
 *         "驻留结点两侧导数为零"的速度半区断言面——§9.2 行 5"驻留段
 *         后首样本速度非零"反例族的边界侧承接；C7 ε_abs 是数值容差而
 *         非工艺阈值——零速判定的量纲诚实转写）。回推残差 O(jerk·δ²)
 *         入此判定的测量精度声明（黄金算例：真实驻留产物在驻留边界处
 *         qdd=0〔结点二阶导恒 0 纪律〕且 jerk 有限，δ 取 1×10⁻⁷ s 时
 *         残差 ≪ ε_abs——测试头注声明）。
 *   - **诊断码复用（零新增码）**：全部违例素材 reasonToken＝TRJ-CONTI-
 *     NUITY-BROKEN（§14.4 行 6——"位置/姿态/速度/加速度连续性破坏"的
 *     边界行为语义本体：速度不连续＝§9.2 行 4 反例族、驻留切换非零＝
 *     行 5 反例族、隐式停驻＝行 5 的"切换不明确"对偶形态——声称不停
 *     驻的边界实际停驻；T14/T15 批"超限/连续性维度复用既有码"先例同
 *     款，码表 12 码零扩保持；码描述符 requiresComparison=true 与比较
 *     型素材三要素匹配）。phaseToken 新增常量 kPhaseBoundaryCheck＝
 *     "boundary-check"（§14.1.6 词表表尾追加——T15 批 kPhaseProcessSpeed-
 *     Check 先例同款：素材阶段定位需要本校验维度的独立词形；DTB §5.4
 *     增量登记）。
 *   - **素材组装序（确定性）**：violationRecords 序＝边界序×维度序（速
 *     度连续 → 切换语义〔Continuous 隐式停驻 / Dwell 零速〕）——每边
 *     界每维度至多一条素材；不重排不合并；boundaryObservations 序＝请
 *     求 boundaries 序原样（逐边界观测与素材一一可对账）。
 *   - 需求 TRJ-08-S3、TRJ-06（比较型素材轨）、NFR-COR-01/02/03、ARC-05
 *     （见 ContinuousProcess.hpp）；错误二分见 Errors.hpp。
 *
 * 集成模式条件源（TimeParam.cpp/ProcessSpeed.cpp 同款 gating）：本 TU 经
 * ITimeCurve::sampleAt 消费 rw::math::Q 的构造/析构符号（Q 虚析构外联
 * 于框架库 sdurw_math）——冒烟模式无框架库可链，不编译本 TU；Continu-
 * ousProcess.hpp 的值类型/接口声明两模式皆可编译。
 */

#include <sdurws/ird/trajectory/ContinuousProcess.hpp>

#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <rw/math/Q.hpp>

#include <sdurws/ird/core/Compare.hpp>      // core::runtimeAbsoluteTolerance（C7 转写——D-06 单点）
#include <sdurws/ird/core/Provenance.hpp>   // core::SourcedValue/ValueProvenance（比较型素材值面）
#include <sdurws/ird/core/Units.hpp>        // core::QuantityKind/UnitToken（角速度量纲转写）
#include <sdurws/ird/trajectory/DiagCodes.hpp>  // TRJ-* 码常量（素材 reasonToken 唯一书写点）
#include <sdurws/ird/trajectory/Errors.hpp>     // TrajectoryError（fail-fast 载体）
#include <sdurws/ird/trajectory/TimeParam.hpp>  // kTimeParamLimitRelativeTolerance（附录 D 第 10 项——唯一书写点）

namespace sdurws::ird::trajectory {
namespace {

// 失败阶段 token：连续工艺边界校验（FailedSegmentRecord.phaseToken 产生
// 面——§14.1.6 phase 词表表尾追加，WP-16-T16 批；T15 批 kPhaseProcessSpeed-
// Check 先例同款：实现 TU 内部常量，词表增补的实现落位不外溢头文件，测
// 试以字面量断言值）。
//
// 词形契约：kebab 小写、与既有词表同形（plan-ptp/…/time-param/process-
// speed-check + boundary-check 增补——本校验维度独立成词）。
const char kPhaseBoundaryCheck[] = "boundary-check";

// =====================================================================
// C7 ε_abs 转写与单位 token（D-06 单点——TimeParam.cpp 同款口径）
// =====================================================================

/**
 * @brief 关节角速度判定的绝对容差（rad/s；core::runtimeAbsoluteTolerance
 *        的 AngularVelocity 族转写——C7 默认 1×10⁻⁹）。
 *
 * 量纲声明：关节速度逐轴按关节类型（转动 rad/s／移动 m/s）——混合关
 * 节链取 Angular 族转写（TimeParam.cpp 关节域同款口径；两族 C7 默认同
 * 值 1×10⁻⁹，结论不受选择影响）。转写对已声明量纲恒有值（core 侧 D-06
 * 表）——nullopt 属实现缺陷。
 */
double angularVelocityAbsTolerance()
{
    const std::optional<double> eps =
        core::runtimeAbsoluteTolerance(core::QuantityKind::AngularVelocity);
    if (!eps.has_value()) {
        throw TrajectoryError("trajectory/boundary/tolerance-missing",
                              "core C7 转写缺失 AngularVelocity 默认（实现"
                              "缺陷——D-06 表不完整）");
    }
    return *eps;
}

/**
 * @brief 取关节角速度单位 token（"rad/s"——core 单位注册表编译期冻结
 *        在册词形〔Units.cpp 量纲表 AngularVelocity 行〕；违例素材比较
 *        型的单位承载面。find 失败属 core 注册表缺陷，ProcessSpeed.cpp
 *        同款 fail-fast 口径）。
 */
core::UnitToken angularVelocityUnitToken()
{
    const auto token = core::UnitToken::find("rad/s");
    if (!token.has_value()) {
        throw TrajectoryError("trajectory/boundary/unit-token",
                              "core 单位注册表缺少 rad/s token（环境缺陷，"
                              "fail-fast——比较型素材单位口径不可用）");
    }
    return *token;
}

// =====================================================================
// 前置校验（调用方契约违约 fail-fast——校验序固定，NFR-COR-02）
// =====================================================================

/**
 * @brief 请求合法域全表校验（checkProcessBoundaries 步骤 1；非法即抛—
 *        —文案含首错定位与实际值）。
 *
 * 校验序（确定性——同一坏请求必报同一首错）：
 *   1. timeCurve 非空（无时间化产物无速度——§12.4 不伪造同源）；
 *   2. totalDurationS 有限且 >0（s）；
 *   3. neighborDeltaS 有限且 >0（s——邻域半步；本域零自设默认，调用方
 *      显式给出）；
 *   4. minContinuousSpeed 有限且 >0（rad/s|m/s——不停驻工艺阈值；零/
 *      负值＝无不停驻语义可断言，强制显式——T15 恒定带同款纪律）；
 *   5. 逐边界：tBoundaryS 有限且严格内 (0, totalDurationS)（轨迹端点
 *      不是"段间"边界——端点差分邻域单侧退化，无两侧对照语义）。
 *
 * 注：boundaries 为空是合法输入（空结果口径——头注），故第 5 步循环
 * 自然跳过。
 */
void validateRequest(const ProcessBoundaryCheckRequest& request)
{
    // 1. 时间化产物连续求值面（空 shared_ptr＝调用方以默认构造请求蒙混
    //    ——边界行为是时间导数量语义，无曲线即无校验对象）。
    if (request.timeCurve == nullptr) {
        throw TrajectoryError("trajectory/boundary/curve-missing",
                              "时间化产物连续求值面缺失（timeCurve 为空——"
                              "边界行为校验的输入必须是 T08 Ok 态产物）");
    }
    // 2. 总时长（邻域收缩与边界时刻校验的基准域；有限正——零长轨迹无
    //    校验意义）。
    if (!std::isfinite(request.totalDurationS)
        || !(request.totalDurationS > 0.0)) {
        throw TrajectoryError("trajectory/boundary/duration",
                              "轨迹总时长非法：totalDurationS="
                                  + std::to_string(request.totalDurationS)
                                  + " s（要求 >0 且有限）");
    }
    // 3. 邻域半步（s——两侧内点采样的距离尺度；零/负值无邻域语义）。
    if (!std::isfinite(request.neighborDeltaS)
        || !(request.neighborDeltaS > 0.0)) {
        throw TrajectoryError("trajectory/boundary/neighbor-delta",
                              "邻域半步非法：neighborDeltaS="
                                  + std::to_string(request.neighborDeltaS)
                                  + " s（要求 >0 且有限——调用方按结点间距"
                                    "与运动律 jerk 量级显式给出，本域零自"
                                    "设默认）");
    }
    // 4. 不停驻工艺阈值（rad/s|m/s——隐式停驻判定的期望值；零值使断言
    //    退化为"速度非负"（恒真），等于静默关闭切换语义断言——拒绝优
    //    于静默降级，NFR-COR-03）。
    if (!std::isfinite(request.minContinuousSpeed)
        || !(request.minContinuousSpeed > 0.0)) {
        throw TrajectoryError("trajectory/boundary/min-speed",
                              "不停驻阈值非法：minContinuousSpeed="
                                  + std::to_string(request.minContinuousSpeed)
                                  + "（要求 >0 且有限——工艺语义量，调用方"
                                    "显式给出，本域零自设默认）");
    }
    // 5. 逐边界合法域（时刻严格内；首错即抛——文案携带边界序号）。
    for (std::size_t idx = 0; idx < request.boundaries.size(); ++idx) {
        const ProcessBoundarySpan& span = request.boundaries[idx];
        // 时刻：有限、严格内 (0, totalDurationS)——端点是轨迹起讫不是
        // 段间交界（无两侧对照语义）；邻域收缩后 δ>0 的前置保证。
        if (!std::isfinite(span.tBoundaryS) || !(span.tBoundaryS > 0.0)
            || !(span.tBoundaryS < request.totalDurationS)) {
            throw TrajectoryError(
                "trajectory/boundary/time",
                "边界时刻非法：boundaries[" + std::to_string(idx)
                    + "] boundaryIndex=" + std::to_string(span.boundaryIndex)
                    + "，tBoundaryS=" + std::to_string(span.tBoundaryS)
                    + " s（要求有限且严格内 (0, totalDurationS="
                    + std::to_string(request.totalDurationS) + "）");
        }
    }
}

// =====================================================================
// 邻域内点采样与极限估计（一阶回推——测量口径见文件头注登记）
// =====================================================================

/**
 * @brief 关节向量的无穷范数（max|q_i|——逐轴绝对值最大者；rad/s|m/s
 *        逐轴混合计量——口径随字段声明）。
 */
double infinityNorm(const rw::math::Q& v)
{
    double peak = 0.0;
    for (std::size_t i = 0; i < static_cast<std::size_t>(v.size()); ++i) {
        const double a = std::abs(v[i]);
        if (a > peak) { peak = a; }
    }
    return peak;
}

/**
 * @brief 边界一侧的速度极限估计（一阶泰勒回推——文件头注"测量口径"
 *        段）。
 *
 * @param t        [in] 该侧内点时刻（s；t_b∓δ——段内点，解析导数无结
 *                 点歧义）
 * @param sign     [in] 回推方向：+1＝左极限（内点在 t_b−δ，回推 +
 *                 qdd·δ）；−1＝右极限（内点在 t_b+δ，回推 − qdd·δ）。
 *                 以符号参数统一两侧公式（qd⁻ = qd(t−δ)+qdd(t−δ)·δ、
 *                 qd⁺ = qd(t+δ)−qdd(t+δ)·δ——同一泰勒展开的两个方向）
 * @param delta    [in] 实际使用的邻域半步（s；＝min(neighborDeltaS, 到
 *                 时轴端点距离)——回推距离必须与内点采样距离一致，否则
 *                 一阶项回推残差失真）
 * @param request  [in] 校验请求（曲线来源）
 * @param speedOut [out] 该侧极限估计的无穷范数（rad/s|m/s）
 * @param limitQd  [out] 该侧极限估计的关节速度向量（rad/s——速度连续
 *                 判定的逐元素来源）
 */
void estimateSideLimit(double t, double sign, double delta,
                       const ProcessBoundaryCheckRequest& request,
                       double& speedOut, rw::math::Q& limitQd)
{
    // 内点采样（样条解析值——qd 一阶导、qdd 二阶导，rad/s、rad/s²）。
    const TimedSample sample = request.timeCurve->sampleAt(t);
    // 一阶回推：qd_est = qd ∓ qdd·δ（sign=+1 左极限加、−1 右极限减）。
    // 残差＝二阶项 O(jerk·δ²)（rad/s）——neighborDeltaS 的声明义务使残
    // 差远小于判定容差（黄金算例声明——测试头注）。
    limitQd = sample.qd;
    for (std::size_t i = 0; i < static_cast<std::size_t>(sample.qdd.size());
         ++i) {
        limitQd[i] -= sign * sample.qdd[i] * delta;
    }
    speedOut = infinityNorm(limitQd);
}

// =====================================================================
// 比较型素材构造辅助（UX-03 三要素——ProcessSpeed.cpp makeViolationRecord
// 同款形态，来源标记单点化）
// =====================================================================

/**
 * @brief 派生量的来源标记（本域对轨迹产物的派生测量量——ValueProvenance
 *        五类词表 DerivedReadOnly；methodTag 标注产生面）。
 */
inline core::ValueProvenance derivedProvenance(const char* methodTag)
{
    return core::ValueProvenance::make(core::ProvenanceKind::DerivedReadOnly,
                                       {}, {}, methodTag);
}

/**
 * @brief 组装单条边界行为违例素材（TRJ-CONTINUITY-BROKEN 比较型——实
 *        际/期望/单位＋边界定位 boundaryIndex/beforeSegmentIndex/subject；
 *        §9.2 反例族、TRJ-06"具体段落与原因"）。
 *
 * @param boundaryIndex [in] 违例边界序（请求 span 的定位键）
 * @param beforeSegIdx  [in] 前段序号（素材 segmentIndex 携带前段——边界
 *                      违例的"所在段落"取前段，§6.2 定位语义；pathParam
 *                      恒 1.0＝前段末点〔边界即前段终点，s∈[0,1] 归一
 *                      化参数的段内定位〕）
 * @param subject       [in] 素材 subject 投影（可空——透传）
 * @param cause         [in] 中文原因（ERR-01——违例事实＋实测值＋时刻）
 * @param action        [in] 中文建议动作（ERR-01）
 * @param actual        [in] 实测值（速度差/速度范数——量纲随 unit）
 * @param expected      [in] 期望值（判定容差/工艺阈值/零容差——量纲随
 *                      unit）
 * @param unit          [in] 单位 token（rad/s——在册词形）
 */
FailedSegmentRecord makeViolationRecord(std::uint32_t boundaryIndex,
                                        std::uint32_t beforeSegIdx,
                                        std::optional<core::ObjectId> subject,
                                        std::string cause, std::string action,
                                        double actual, double expected,
                                        const core::UnitToken& unit)
{
    FailedSegmentRecord rec;
    rec.segmentIndex = beforeSegIdx;
    rec.phaseToken = kPhaseBoundaryCheck;  // "boundary-check"（§14.1.6 表尾追加）
    rec.reasonToken = std::string(kTrjContinuityBroken);  // 连续性/边界行为破坏语义本体——零新增码
    // 边界＝前段末点：段内归一化参数 s=1.0（pathParameter 的段内定位
    // 语义——§6.2；越限端点 s=0/1 同款形态）。
    rec.pathParameter = 1.0;
    rec.subject = std::move(subject);
    rec.cause = std::move(cause);
    rec.recommendedAction = std::move(action);
    // 比较型三要素（UX-03）：rad/s 直值形态（core 注册表在册词形——量
    // 纲诚实；与 T14 jerk 超限素材的倍数形态不同：本素材的期望值是容
    // 差/阈值本身，直值承载即可，无需倍数换算）。
    core::ComparativeFields cmp;
    cmp.actual.quantity = core::SourcedValue<double>::provided(
        actual, derivedProvenance("boundary-check-measured"));
    cmp.actual.unit = unit;
    cmp.expected.quantity = core::SourcedValue<double>::provided(
        expected, derivedProvenance("boundary-check-spec"));
    cmp.expected.unit = unit;
    rec.comparison = std::move(cmp);
    return rec;
}

}  // namespace

// =====================================================================
// §19.1 S3 校验入口（执行序见 ContinuousProcess.hpp 函数注）
// =====================================================================

ProcessBoundaryCheckResult checkProcessBoundaries(
    const ProcessBoundaryCheckRequest& request)
{
    // ---- 步骤 1：前置校验（调用方契约违约 fail-fast——validateRequest）。
    validateRequest(request);

    // ---- 步骤 2：取消轮询（命中 → Canceled，零素材——UX-03；空边界
    // 的合法空结果在取消检查之后同样成立——零受检边界与取消同为零素
    // 材面，语义区分靠 status：未请求取消且无边界 → Ok）。
    if (request.cancel && request.cancel()) {
        ProcessBoundaryCheckResult result;
        result.status = ProcessBoundaryStatus::Canceled;
        return result;
    }

    // 空受检边界序列＝合法空结果（零断言零素材零观测——T09"空素材序
    // 列＝合法空结果"同口径；Ok 态空观测）。
    if (request.boundaries.empty()) {
        ProcessBoundaryCheckResult result;
        result.status = ProcessBoundaryStatus::Ok;
        return result;
    }

    // ---- 判定容差与单位（C7/D-06 单点转写——文件头注登记）。
    const double epsVel = angularVelocityAbsTolerance();
    const core::UnitToken velUnit = angularVelocityUnitToken();
    // 速度连续判定的相对容差：附录 D 第 10 项常量复用（唯一书写点——
    // TimeParam.hpp 定义，禁第二处字面量）。
    const double relTol = kTimeParamLimitRelativeTolerance;

    // ---- 步骤 3：逐受检边界断言（请求 boundaries 序——确定性；违例
    // 不中断后续边界——全量诊断面，素材序＝边界序×维度序）。
    ProcessBoundaryCheckResult result;
    bool anyViolation = false;
    for (const ProcessBoundarySpan& span : request.boundaries) {
        // 迭代边界取消轮询（§15.1——每轮必查）。
        if (request.cancel && request.cancel()) {
            ProcessBoundaryCheckResult canceled;
            canceled.status = ProcessBoundaryStatus::Canceled;
            return canceled;
        }
        // ---- 3a. 邻域半步收缩与两侧极限估计：δ＝min(邻域半步, 到时轴
        // 端点距离)——邻域恒在求值域内（前置校验保证 t_b 严格内，故
        // δ>0 恒成立；段内性〔δ 不跨相邻结点〕由调用方保证——文档化
        // 义务见文件头注）。两侧内点 t_b∓δ 解析采样＋一阶回推（残差
        // O(jerk·δ²)——单位 rad/s，rad/s²×s）。
        const double delta =
            std::min({request.neighborDeltaS, span.tBoundaryS,
                      request.totalDurationS - span.tBoundaryS});
        double speedBefore = 0.0;
        double speedAfter = 0.0;
        rw::math::Q qdBefore;
        rw::math::Q qdAfter;
        estimateSideLimit(span.tBoundaryS - delta, +1.0, delta, request,
                          speedBefore, qdBefore);  // 左极限（前侧——离开边界方向的内点回推）
        estimateSideLimit(span.tBoundaryS + delta, -1.0, delta, request,
                          speedAfter, qdAfter);   // 右极限（后侧——同口径）
        // 速度连续实测：两侧估计的逐元素差向量（rad/s）→ 无穷范数。
        const rw::math::Q velocityDelta = qdAfter - qdBefore;
        const double speedDelta = infinityNorm(velocityDelta);
        const double speedMin = std::min(speedBefore, speedAfter);

        // ---- 3b. 速度连续断言（两类切换统一——§9.1 速度连续维必检）：
        // ‖qd⁺−qd⁻‖_∞ ≤ ε_rel·‖qd⁻‖_∞ + ε_abs（C4 公式的范数形态——逐
        // 元素 |ref_i| ≤ ‖ref‖_∞，范数基准比逐元素基准更严，方向安全不
        // 漏检；文件头注登记）。false＝两侧速度不连续，§9.2 行 4 反例
        // 族。素材期望值＝同一公式右端（判定容差本身）。
        const bool speedContinuousOk =
            speedDelta <= relTol * speedBefore + epsVel;
        if (!speedContinuousOk) {
            anyViolation = true;
            result.violationRecords.push_back(makeViolationRecord(
                span.boundaryIndex, span.beforeSegmentIndex, span.subject,
                "段间速度不连续：边界 t=" + std::to_string(span.tBoundaryS)
                    + " s 两侧关节速度极限估计的逐元素差无穷范数 "
                    + std::to_string(speedDelta)
                    + " 超出附录 D 第 10 项容差（相对 1e-9＋C7 ε_abs="
                    + std::to_string(epsVel) + " rad/s——§9.1 速度连续维）",
                "登记缺陷并回归黄金算例（§12.6 行 2——结点两侧导数不连"
                "续属运动律实现缺陷）",
                speedDelta, relTol * speedBefore + epsVel, velUnit));
        }

        // ---- 3c. 切换语义断言（expectedSwitch 分派——零空分支；断言
        // 维＝速度〔验收标准原文口径〕；词表两值封闭，扩值＝需求变更＋
        // 本卡增量修订）。
        bool switchSemanticsOk = true;
        switch (span.expectedSwitch) {
        case BoundarySwitchKind::Continuous: {
            // 不停驻断言：两侧 ‖qd‖_∞ ≥ 工艺阈值（任一侧低于阈值＝隐
            // 式停驻——声称连续切换实际停顿，连续工艺段语义违约）。速
            // 度连续已由 3b 独立断言（违例各自产素材——维度正交）。
            if (speedMin < request.minContinuousSpeed) {
                switchSemanticsOk = false;
                anyViolation = true;
                result.violationRecords.push_back(makeViolationRecord(
                    span.boundaryIndex, span.beforeSegmentIndex, span.subject,
                    "隐式停驻：连续切换边界 t=" + std::to_string(span.tBoundaryS)
                        + " s 两侧关节速度峰值 " + std::to_string(speedMin)
                        + " 低于不停驻阈值 "
                        + std::to_string(request.minContinuousSpeed)
                        + "（声称不停驻的边界实际发生停顿——TRJ-08-S3 切"
                          "换明确违例）",
                    "将该边界改为驻留切换声明（显式驻留段）或调整轨迹时"
                    "间参数化使边界速度达到不停驻阈值后重新评估",
                    speedMin, request.minContinuousSpeed, velUnit));
            }
            break;
        }
        case BoundarySwitchKind::Dwell: {
            // 驻留断言：两侧零速（§12.1 驻留结点两侧速度为零的速度半区
            // 断言面——§9.2 行 5"驻留段后首样本速度非零"反例族的边界
            // 侧承接；C7 ε_abs 是数值容差——零速判定）。回推残差
            // O(jerk·δ²) 的测量精度声明随黄金算例（真实驻留产物在驻留
            // 边界 qdd=0、δ=1e-7 s 时残差 ≪ ε_abs——测试头注）。
            const double speedMax = std::max(speedBefore, speedAfter);
            if (speedMax > epsVel) {
                switchSemanticsOk = false;
                anyViolation = true;
                result.violationRecords.push_back(makeViolationRecord(
                    span.boundaryIndex, span.beforeSegmentIndex, span.subject,
                    "驻留切换速度非零：边界 t=" + std::to_string(span.tBoundaryS)
                        + " s 两侧关节速度峰值 " + std::to_string(speedMax)
                        + " rad/s 超出零速容差 " + std::to_string(epsVel)
                        + " rad/s（§12.1 驻留结点两侧导数为零——§9.2 驻留"
                          "段边界反例族）",
                    "登记缺陷并回归黄金算例（§12.6 行 2——驻留/边界导数"
                    "不连续属运动律实现缺陷）",
                    speedMax, epsVel, velUnit));
            }
            break;
        }
        }

        // ---- 3d. 边界观测入列（与 boundaries 同序——证据素材面）。
        ProcessBoundaryObservation observation;
        observation.boundaryIndex = span.boundaryIndex;
        observation.tBoundaryS = span.tBoundaryS;
        observation.speedBefore = speedBefore;
        observation.speedAfter = speedAfter;
        observation.speedDelta = speedDelta;
        observation.speedContinuousOk = speedContinuousOk;
        observation.switchSemanticsOk = switchSemanticsOk;
        observation.satisfied = speedContinuousOk && switchSemanticsOk;
        result.boundaryObservations.push_back(observation);
    }

    // ---- 步骤 4：结局组装（全部满足 → Ok；任一违例 → Violated）。
    result.status =
        anyViolation ? ProcessBoundaryStatus::Violated : ProcessBoundaryStatus::Ok;
    return result;
}

}  // namespace sdurws::ird::trajectory
