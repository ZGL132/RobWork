/**
 * @file   ForwardDynamics.cpp
 * @brief  正动力学响应一致性检查器实现（units/dynamics.md §6）——同源
 *         τ_ref 产出、单样本 RNEA 质量阵/偏置力提取、固定步长 RK4 积分、
 *         误差四元组度量、发散/数值异常检测与四态判定。
 *
 * 设计依据（契约面见同名公共头 ForwardDynamics.hpp 文件头）：
 *   - units/dynamics.md §6.1（时序图①→⑤——本实现按步逐步对应）、§6.2
 *     （输入与前置表）、§6.3（必须明确六条）、§10.2（契约表）、§9.4
 *     （DYN-FD-* 码）、§4.6（样本级处置——非有限不伪造）
 *   - 需求 DYN-05（明确控制输入/初始状态/步长/容差的正动力学场景——响应
 *     一致性检查与异常检测）
 *   - 任务契约 tasks/foundation/WP-17-T05.json
 *
 * 实现要点（为什么零 rw 外联符号）：
 *   全部数值路径（RK4/高斯消元/误差统计）为自持 double 逐元素算术；模型
 *   消费只经公共 InverseDynamicsEvaluator::evaluate（其内部已按 T03 冒烟
 *   纪律实现）——本 TU 无任何 rw/rwsim 头包含，双模式构建同源可链（公共
 *   头文件头"引擎选型登记"第 4 条）。
 *
 * 算法总览（符号约定全链统一，单位见逐处注释）：
 *   - 状态 y=(q,q̇)，右端 f(q,q̇)=M(q)⁻¹·(τ_ctrl − h_f(q,q̇))：
 *     h_f(q,q̇)＝G(q)＋C(q,q̇)q̇＋τ_fric(q̇)＝单样本 evaluate(q,q̇,q̈=0) 的
 *     tauTotal（全量通道在 q̈=0 时恰为三者之和——T03 分项拆分口径）；
 *     M(q) 第 i 列＝单样本 evaluate(q, q̇=0, q̈=e_i) 的 tauInertia（惯性
 *     通道＝(g=0,q̇=0) 输入组合下的 M(q)·q̈，纯惯量不含摩擦/重力——T03
 *     "惯性通道"注释原文），n 列共 n 次调用；
 *   - 采样保持（§6.1 ②）：区间 [t_j, t_{j+1}) 内控制输入 τ_ctrl＝τ_ref(t_j)
 *     常量；M/h 提取调用一律传区间起点时刻 t_j——evaluate 内部事件时间线
 *     按"样本时刻 t ≥ tEvent 生效"推进变体（§5.4 保守边界），区间起点时刻
 *     调用使正动力学侧变体与 τ_ref(t_j) 严格同变体（⑤共源强制的结构性
 *     实现——变体切换只发生在参考样本区间边界，积分子步内恒定）；
 *   - 固定步长 RK4：子步长 h＝min(maxStepS, t_{j+1} − t_current)（子步不跨
 *     参考样本边界——采样保持区间完整性）；经典四显式 stage，对不超过 4
 *     阶的多项式轨迹精确（黄金解析算例的构造依据——匀加速激励在质量阵
 *     常数区间内 τ_ref 恒定，仿真逐位重现解析解）；
 *   - 线性求解：带部分主元的高斯消元（n×n，n 为可动关节数，典型 ≤7——
 *     O(n³) 每次消元可忽略）；主元下界＝1e-14×‖M‖∞（数值线性代数的常规
 *     相对奇异判据——实现常量，黄金算例锁定；全零质量阵由 ‖M‖∞=0 先行
 *     拦截）。
 */

#include <sdurws/ird/dynamics/ForwardDynamics.hpp>

#include <sdurws/ird/dynamics/DiagCodes.hpp>  // DYN-* 码值常量（唯一书写点——产码共用）
#include <sdurws/ird/core/Provenance.hpp>     // core::SourcedValue/ProvenanceKind（比较型素材）
#include <sdurws/ird/core/Units.hpp>          // core::UnitToken（比较型单位三要素）

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
#include <utility>

namespace sdurws::ird::dynamics {

namespace {

// =====================================================================
// 实现常量（黄金算例锁定面——测试对照口径与产品判据分离，附录 D C7）。
// =====================================================================

/**
 * 发散检出量级（异常检测判据——非工程判定阈值）：状态分量 |q| 或 |q̇| 超
 * 此界即判积分发散（DYN-FD-DIVERGED）。取 1e10：正常机械臂动力学量级远低
 * （限位 rad、速度 rad/s 的工程范围 <1e4），而双精度饱和（~1e308）前必经
 * 此界——检出确定且不误报。黄金发散算例按此界构造（测试文件引用）。
 */
constexpr double kForwardDivergenceMagnitude = 1e10;

/**
 * 质量阵线性求解的相对奇异判据：消元主元 |p| ≤ 1e-14×‖M‖∞ 即判求解失败
 * （DYN-FD-NUMERIC-ANOMALY）。取 1e-14＝双精度部分主元消元的常规相对下界
 * （物理一致模型的 M 对称正定，正常路径主元远离此界；全零物性链的 M≡0
 * 由 ‖M‖∞=0 先行拦截）。
 */
constexpr double kSingularPivotRelative = 1e-14;

// =====================================================================
// 单样本 RNEA 调用（M/h 提取的公共接口消费面——共源强制的实现载体）。
// =====================================================================

/**
 * @brief 单样本逆动力学调用（一次 evaluate 消费——返回逐关节全量与惯性
 *        通道力矩）。
 *
 * 变体一致性：t 传区间起点时刻——evaluate 内部按事件时间线把 tEvent≤t 的
 * 全部事件生效（§5.4 保守边界），与 τ_ref(t) 严格同变体（文件头算法总览）。
 *
 * @param base      [in] 请求基座（身份/模型/重力/负载/事件——拷贝后改样本列）
 * @param t         [in] 样本时刻，单位 s（区间起点——变体生效时刻）
 * @param q         [in] 关节位置（rad 或 m——权威角；长度 n）
 * @param qd        [in] 关节速度（rad/s 或 m/s；长度 n）
 * @param qdd       [in] 关节加速度（rad/s² 或 m/s²；长度 n）
 * @param evaluator [in] 逆动力学评估器（无状态——调用边界）
 * @param context   [in] 宿主上下文（evaluate 内部样本级取消轮询用）
 * @param tauTotal  [out] 全量通道 τ（N·m 或 N，按关节类型；长度 n）
 * @param tauInertia[out] 惯性通道 M(q)·q̈（同上量纲；长度 n——M 列提取用）
 * @param cancelled [out] 观测到宿主取消置 true（evaluate 单样本取消——
 *                  空产出返回）
 *
 * @throws DynamicsError 透传 evaluate 的调用方错误（物性复检等——与 τ_ref
 *         主调用同模型，理论不可达；防御性传播不吞错）
 */
void callRneaSample(const InverseDynRequest& base, double t,
                    const std::vector<double>& q, const std::vector<double>& qd,
                    const std::vector<double>& qdd,
                    const InverseDynamicsEvaluator& evaluator,
                    evidence::IEvaluationContext& context,
                    std::vector<double>& tauTotal, std::vector<double>& tauInertia,
                    bool& cancelled)
{
    // 请求基座拷贝：身份/模型引用/重力/负载池/事件时间线原样透传（共源），
    // 仅样本列替换为本调用构造的单样本（segmentIndex=0——内部调用无段语义）。
    InverseDynRequest req = base;
    InverseDynSampleInput s;
    s.t = t;            // 单位 s
    s.segmentIndex = 0; // 内部提取调用不携带上游段结构
    s.q = q;
    s.qd = qd;
    s.qdd = qdd;
    req.samples.clear();
    req.samples.push_back(std::move(s));

    const InverseDynOutcome out = evaluator.evaluate(req, context);
    cancelled = out.cancelled;
    if (cancelled) { return; }  // 取消＝空产出（调用方检查 cancelled 位）

    // 行提取：单样本产出按 (t, jointIndex 升序) 排序（evaluate §4.6 纪律），
    // 前 n 行即本样本逐关节行；tauTotal/tauInertia 逐行搬运。
    const std::size_t n = q.size();
    tauTotal.assign(n, 0.0);
    tauInertia.assign(n, 0.0);
    for (std::size_t i = 0; i < n; ++i) {
        tauTotal[i] = out.samples.at(i).tauTotal;
        tauInertia[i] = out.samples.at(i).tauInertia;
    }
}

/**
 * @brief 解 n×n 线性方程组 M·x = rhs（带部分主元的高斯消元）。
 *
 * @param a   [in,out] 系数矩阵（行主序 a[i*n+j]，单位随方程语义——本域为
 *            质量阵 kg·m²/kg；函数内被消元破坏，调用方传副本）
 * @param b   [in,out] 右端项（长度 n；函数内随消元变换）
 * @param x   [out] 解向量（长度 n——q̈，rad/s² 或 m/s²）
 * @return true＝求解成功；false＝奇异/病态（‖M‖∞=0 或主元越相对下界——
 *         调用方据此产 DYN-FD-NUMERIC-ANOMALY）
 *
 * 确定性：固定循环序＋部分主元取首个最大（严格 > 比较——同输入同主元行）。
 */
bool solveLinearSystem(std::vector<double>& a, std::vector<double>& b,
                       std::vector<double>& x)
{
    const std::size_t n = b.size();
    // ‖M‖∞（行和最大绝对值——缩放基准）：全零矩阵立即判奇异（全零物性链
    // 的物理语义＝无惯量可积分，属数值异常而非除零崩溃）。
    double scale = 0.0;
    for (std::size_t k = 0; k < n * n; ++k) {
        scale = std::max(scale, std::abs(a[k]));
    }
    if (!(scale > 0.0)) { return false; }
    const double pivotFloor = scale * kSingularPivotRelative;  // 相对主元下界

    // 前向消元（逐列部分主元——数值稳定性）。
    for (std::size_t col = 0; col < n; ++col) {
        // 选主元：col 列 |a[r][col]| 最大的行 r≥col（严格 > 取首个最大——
        // 确定性，NFR-COR-02）。
        std::size_t pivotRow = col;
        double pivotMag = std::abs(a[col * n + col]);
        for (std::size_t r = col + 1; r < n; ++r) {
            const double mag = std::abs(a[r * n + col]);
            if (mag > pivotMag) {
                pivotMag = mag;
                pivotRow = r;
            }
        }
        if (!(pivotMag > pivotFloor)) { return false; }  // 奇异/病态
        if (pivotRow != col) {
            // 行交换（消元准备——行主序逐元素换）。
            for (std::size_t c = 0; c < n; ++c) {
                std::swap(a[col * n + c], a[pivotRow * n + c]);
            }
            std::swap(b[col], b[pivotRow]);
        }
        // 消去下方各行（第 col 列）。
        for (std::size_t r = col + 1; r < n; ++r) {
            const double factor = a[r * n + col] / a[col * n + col];
            a[r * n + col] = 0.0;
            for (std::size_t c = col + 1; c < n; ++c) {
                a[r * n + c] -= factor * a[col * n + c];
            }
            b[r] -= factor * b[col];
        }
    }
    // 回代（上三角求解——最后一行向前）。
    x.assign(n, 0.0);
    for (std::size_t ii = n; ii-- > 0;) {
        const std::size_t r = ii;
        double s = b[r];
        for (std::size_t c = r + 1; c < n; ++c) {
            s -= a[r * n + c] * x[c];
        }
        x[r] = s / a[r * n + r];
    }
    return true;
}

/// 向量全部分量有限（NFR-COR-03 判定原语——长度 n 的逐元素检查）。
bool allFinite(const std::vector<double>& v)
{
    for (const double x : v) {
        if (!std::isfinite(x)) { return false; }
    }
    return true;
}

/// 任一分量绝对值超发散检出界（kForwardDivergenceMagnitude——异常检测）。
bool exceedsDivergenceBound(const std::vector<double>& v)
{
    for (const double x : v) {
        if (std::abs(x) > kForwardDivergenceMagnitude) { return true; }
    }
    return false;
}

}  // namespace

// =====================================================================
// 检查主入口（公共头 ForwardDynamicsValidator::validate 的实现）。
// =====================================================================

ForwardCheckOutcome ForwardDynamicsValidator::validate(
    const ForwardCheckRequest& request, evidence::IEvaluationContext& context) const
{
    ForwardCheckOutcome out;  // 全字段默认值见公共头（state=NotRun、误差 NaN）
    const double kNaN = std::numeric_limits<double>::quiet_NaN();

    // ---- 第 0 步：mode=Skip 短路（§6.3.4——显式 NotApplicable，非缺失；
    //      其余配置字段不参与校验：Skip 语义下未配置是合法形态）----
    if (request.forwardCheck.mode == ForwardCheckSettings::Mode::Skip) {
        out.state = DynamicsValidity::ForwardCheckState::NotApplicable;
        return out;
    }

    // ---- 配置校验（Standard 模式——调用方错误 fail-fast；DYN-05 四要素
    //      之"步长/容差明确"的合法性面；配置非法不发稳定码——§10.0 错误
    //      类型行"输入/配置非法"归调用方错误轨）----
    if (request.forwardCheck.integratorToken != kForwardIntegratorToken) {
        // 封闭词表（§4.2——当前唯一锁定值 kForwardIntegratorToken）。
        throw DynamicsError("input-invalid",
                            "forwardCheck.integratorToken 不在封闭词表（实际 '"
                                + request.forwardCheck.integratorToken + "'，词表值 '"
                                + std::string{kForwardIntegratorToken} + "'）——拒绝检查");
    }
    if (!std::isfinite(request.forwardCheck.maxStepS)
        || request.forwardCheck.maxStepS <= 0.0) {
        throw DynamicsError("input-invalid",
                            "forwardCheck.maxStepS 必须为有限正值（实际 "
                                + std::to_string(request.forwardCheck.maxStepS)
                                + " s）——DYN-05 步长明确");
    }
    if (!std::isfinite(request.forwardCheck.relTolerance)
        || request.forwardCheck.relTolerance <= 0.0) {
        throw DynamicsError("input-invalid",
                            "forwardCheck.relTolerance 必须为有限正值（实际 "
                                + std::to_string(request.forwardCheck.relTolerance)
                                + "，无量纲）——DYN-05 容差明确");
    }
    if (!std::isfinite(request.forwardCheck.toleranceQ)
        || request.forwardCheck.toleranceQ <= 0.0) {
        throw DynamicsError("input-invalid",
                            "forwardCheck.toleranceQ 必须为有限正值（实际 "
                                + std::to_string(request.forwardCheck.toleranceQ)
                                + "，rad 或 m）——一致性判定阈值");
    }
    if (!std::isfinite(request.forwardCheck.toleranceQd)
        || request.forwardCheck.toleranceQd <= 0.0) {
        throw DynamicsError("input-invalid",
                            "forwardCheck.toleranceQd 必须为有限正值（实际 "
                                + std::to_string(request.forwardCheck.toleranceQd)
                                + "，rad/s 或 m/s）——一致性判定阈值");
    }

    // ---- 请求身份/模型/重力校验（同逆动力学前置语义——fail-fast；
    //      主调用 evaluate 内有同款防御，此处前置使错误序确定）----
    if (request.model == nullptr) {
        throw DynamicsError("input-invalid", "模型指针为空——拒绝检查");
    }
    if (!request.conditionId.isValid()) {
        throw DynamicsError("input-invalid", "工况对象 ID 为空——拒绝检查");
    }
    if (!std::isfinite(request.gravityBase[0]) || !std::isfinite(request.gravityBase[1])
        || !std::isfinite(request.gravityBase[2])) {
        throw DynamicsError("input-invalid", "基座系重力加速度含非有限分量——拒绝检查");
    }
    // 负载池/事件引用完整性（同逆动力学——事件越界/时刻非有限是调用方错误）。
    for (std::size_t e = 0; e < request.events.size(); ++e) {
        if (request.events[e].payloadIndex >= request.payloads.size()) {
            throw DynamicsError("input-invalid",
                                "负载事件引用越界（事件序 " + std::to_string(e)
                                    + "，payloadIndex="
                                    + std::to_string(request.events[e].payloadIndex)
                                    + "，池大小="
                                    + std::to_string(request.payloads.size()) + "）");
        }
        if (!std::isfinite(request.events[e].tEvent)) {
            throw DynamicsError("input-invalid",
                                "负载事件时刻非有限（事件序 " + std::to_string(e) + "）");
        }
    }
    for (const EndEffectorPayload& p : request.payloads) {
        if (!p.objectId.isValid()) {
            throw DynamicsError("input-invalid", "负载对象 ID 为空——素材不可定位");
        }
    }

    // ---- 第 1 步：数据充足性（§6.2——初始状态缺失→NotRun＋素材，建议项
    //      语义不阻断、不伪造 Passed；样本数 <2＝无仿真区间＝参考段无可用
    //      状态序列，同码承载）----
    const std::size_t nSample = request.samples.size();
    if (nSample < 2) {
        core::DiagnosticRecord rec = core::DiagnosticRecord::make(
            core::DiagCode{std::string{kDynFdInitialStateMissing}}, request.conditionId,
            std::optional<std::string>{}, std::optional<std::string>{},
            std::string{"dynamics 正动力学一致性检查"},
            std::string{"参考段样本数 " + std::to_string(nSample)
                        + " <2——无仿真区间，初始状态序列缺失（§6.2）——检查 NotRun，"
                          "建议证据项缺失不阻断、不伪造 Passed"},
            std::string{"提供含初始状态与至少两个样本的参考段后重试"});
        out.diagnostics.push_back(std::move(rec));
        out.state = DynamicsValidity::ForwardCheckState::NotRun;
        return out;
    }
    {
        // 初始状态非有限（q₀/q̇₀ 任一分量 NaN/Inf）——不可积分起点，NotRun。
        const InverseDynSampleInput& s0 = request.samples.front();
        bool initialNonFinite = false;
        for (std::size_t i = 0; i < s0.q.size(); ++i) {
            if (!std::isfinite(s0.q[i]) || !std::isfinite(s0.qd[i])) {
                initialNonFinite = true;
                break;
            }
        }
        if (initialNonFinite) {
            core::DiagnosticRecord rec = core::DiagnosticRecord::make(
                core::DiagCode{std::string{kDynFdInitialStateMissing}}, request.conditionId,
                std::optional<std::string>{}, std::optional<std::string>{},
                std::string{"dynamics 正动力学一致性检查"},
                std::string{"参考段初始状态 (q₀,q̇₀) 含非有限分量（首个样本）——不可积分"
                            "起点（NFR-COR-03），检查 NotRun，不伪造 Passed"},
                std::string{"修复上游轨迹初始样本后重试"});
            out.diagnostics.push_back(std::move(rec));
            out.state = DynamicsValidity::ForwardCheckState::NotRun;
            return out;
        }
    }

    // ---- 第 2 步：τ_ref 产出（时序图①——公共 evaluate 整段调用；同一冻结
    //      模型/重力/负载/工况，⑤共源的结构性强制）----
    InverseDynRequest rneaRequest;
    rneaRequest.conditionId = request.conditionId;
    rneaRequest.toolObjectId = core::ObjectId{};  // 身份透传位——工具 id 由评估器
                                                  //   自模型读取（提取链），请求位保持空 id
    rneaRequest.model = request.model;
    rneaRequest.gravityBase[0] = request.gravityBase[0];
    rneaRequest.gravityBase[1] = request.gravityBase[1];
    rneaRequest.gravityBase[2] = request.gravityBase[2];
    rneaRequest.samples = request.samples;
    rneaRequest.payloads = request.payloads;
    rneaRequest.events = request.events;

    const InverseDynamicsEvaluator evaluator;  // 无状态调用边界（每任务一实例约定）
    const InverseDynOutcome rneaOut = evaluator.evaluate(rneaRequest, context);

    // τ_ref 侧降级/非有限诊断透传（共源事实——一致性检查的素材完整性）。
    out.diagnostics.insert(out.diagnostics.end(), rneaOut.diagnostics.begin(),
                           rneaOut.diagnostics.end());

    if (rneaOut.cancelled) {
        // τ_ref 阶段被取消——检查未执行（非错误，UX-03）。
        out.cancelled = true;
        out.state = DynamicsValidity::ForwardCheckState::NotRun;
        return out;
    }

    // τ_ref 逐时刻提取（evaluate 产出按 (t 升序, jointIndex 升序) 排序——
    // §4.6；行指针移动法：每 n 行为一时刻）。
    const std::size_t n = rneaRequest.samples.front().q.size();  // 可动关节数
    struct SampleTorque {
        std::vector<double> tauTotal;          ///< 逐关节全量 τ_ref（N·m 或 N）
        SampleNumericState numericState;       ///< 该样本数值状态（非 Ok 不参与仿真/比较）
        double t;                              ///< 样本时刻，单位 s（透传自请求）
    };
    std::vector<SampleTorque> tauRef;
    tauRef.reserve(nSample);
    for (std::size_t j = 0; j < nSample; ++j) {
        SampleTorque st;
        st.t = rneaRequest.samples[j].t;
        st.numericState = SampleNumericState::Ok;
        st.tauTotal.assign(n, 0.0);
        for (std::size_t i = 0; i < n; ++i) {
            const DynamicsSample& row = rneaOut.samples.at(j * n + i);
            st.tauTotal[i] = row.tauTotal;
            if (row.numericState != SampleNumericState::Ok) {
                // 非有限输入/输出样本：τ_ref 行不可信——该样本不参与仿真/比较
                // （样本级处置，§4.6；诊断已由 evaluate 透传 DYN-NON-FINITE/
                // DYN-RNEA-FAILED——此处不重复产码）。
                st.numericState = row.numericState;
            }
        }
        tauRef.push_back(std::move(st));
    }

    // τ_ref 故障点截断：首个非 Ok 样本起，后续样本不可仿真（控制输入缺失/
    // 参考状态缺失）——故障点后区间不执行（部分数据语义见文件尾判定段）。
    std::size_t usableSamples = nSample;  // 可用前缀长度（首个故障样本下标）
    for (std::size_t j = 0; j < nSample; ++j) {
        if (tauRef[j].numericState != SampleNumericState::Ok) {
            usableSamples = j + 1;  // 故障样本自身保留（其行已透传素材），其后截断
            break;
        }
    }
    const bool rneaComplete = (usableSamples == nSample);

    // ---- 第 3～5 步：正向仿真＋误差度量＋异常检测（时序图②③④）----
    // 仿真状态 y=(q,q̇)——初始状态取参考段起点（§6.2；第 1 步已验证有限）。
    std::vector<double> q = request.samples.front().q;    // 权威角（rad 或 m）
    std::vector<double> qd = request.samples.front().qd;  // 关节速度（rad/s 或 m/s）

    // RNEA 请求基座（M/h 单样本调用的公共底座——身份/模型/重力/负载/事件）。
    InverseDynRequest extractBase;
    extractBase.conditionId = request.conditionId;
    extractBase.toolObjectId = core::ObjectId{};
    extractBase.model = request.model;
    extractBase.gravityBase[0] = request.gravityBase[0];
    extractBase.gravityBase[1] = request.gravityBase[1];
    extractBase.gravityBase[2] = request.gravityBase[2];
    extractBase.payloads = request.payloads;
    extractBase.events = request.events;

    // 动力学右端 f(q,q̇)＝M(q)⁻¹·(τ_ctrl − h_f(q,q̇)) 的单次求值。
    // 返回 false＝质量阵奇异/病态（数值异常——调用方产 DYN-FD-NUMERIC-
    // ANOMALY 并终止仿真）。
    const std::vector<double> zeroVec(n, 0.0);
    std::vector<double> tauTotalBuf, tauInertiaBuf;  // 单样本调用输出缓冲
    std::vector<double> hVec(n, 0.0);                // h_f(q,q̇)（N·m 或 N）
    std::vector<double> massFlat(n * n, 0.0);        // 质量阵（行主序，kg·m²/ kg）
    std::vector<double> rhs(n, 0.0);                 // τ_ctrl − h_f（N·m 或 N）
    std::vector<double> qdd(n, 0.0);                 // 解向量（rad/s² 或 m/s²）
    auto evalAcceleration = [&](const std::vector<double>& qv,
                                const std::vector<double>& qdv, double tIntervalStart,
                                const std::vector<double>& tauCtrl, bool& cancelledOut,
                                bool& singularOut) {
        cancelledOut = false;
        singularOut = false;
        // h_f 提取：单样本 evaluate(q, q̇, q̈=0) 的全量通道＝G＋C·q̇＋摩擦
        // （q̈=0 时惯性通道为零——全量恰为偏置力＋摩擦之和）。
        callRneaSample(extractBase, tIntervalStart, qv, qdv, zeroVec, evaluator,
                       context, tauTotalBuf, tauInertiaBuf, cancelledOut);
        if (cancelledOut) { return false; }
        hVec = tauTotalBuf;

        // M 逐列提取：惯性通道 (g=0, q̇=0, q̈=e_i)＝M·e_i（纯惯量——不叠
        // 摩擦/重力；T03"惯性通道"口径）。n 列 n 次调用。
        for (std::size_t i = 0; i < n; ++i) {
            std::vector<double> e = zeroVec;
            e[i] = 1.0;  // 单位向量（第 i 列激励——rad/s² 或 m/s² 的 1 单位）
            callRneaSample(extractBase, tIntervalStart, qv, zeroVec, e, evaluator,
                           context, tauTotalBuf, tauInertiaBuf, cancelledOut);
            if (cancelledOut) { return false; }
            for (std::size_t r = 0; r < n; ++r) {
                massFlat[r * n + i] = tauInertiaBuf[r];  // 行主序 M(r,i)
            }
        }

        // rhs＝τ_ctrl − h_f（控制输入减偏置力——正动力学方程右端）。
        for (std::size_t i = 0; i < n; ++i) {
            rhs[i] = tauCtrl[i] - hVec[i];
        }
        // 求解 M·q̈ = rhs（副本消元——massFlat 可复用）。
        std::vector<double> a = massFlat;
        std::vector<double> b = rhs;
        const bool solved = solveLinearSystem(a, b, qdd);
        singularOut = !solved;
        return solved;
    };

    // 误差统计累加器（§6.1 ④——逐元素 max 与全网格平方和）。
    double sumSqQ = 0.0;     ///< e_q 全网格平方和（rad² 或 m²）
    double sumSqQd = 0.0;    ///< e_qd 全网格平方和（(rad/s)² 或 (m/s)²）
    double maxErrQv = 0.0;   ///< max|e_q|（rad 或 m）
    double maxErrQdv = 0.0;  ///< max|e_qd|（rad/s 或 m/s）
    double tMaxQ = kNaN;     ///< max|e_q| 首达时刻（s）
    double tMaxQd = kNaN;    ///< max|e_qd| 首达时刻（s）
    std::size_t compared = 0;      ///< 参与比较的样本时刻数
    std::size_t intervalCount = 0; ///< 预期比较区间数（故障/取消前的完整区间）
    {
        // 预期区间数＝可用样本数−1（区间 j 连接样本 j 与 j+1）。
        intervalCount = (usableSamples >= 2) ? (usableSamples - 1) : 0;
    }

    // 数值异常/发散的终止面（记录首因——诊断素材只产一条）。
    bool diverged = false;       ///< 发散检出（DYN-FD-DIVERGED）
    bool singular = false;       ///< 质量阵求解失败（DYN-FD-NUMERIC-ANOMALY）
    std::string anomalyDetail;   ///< 异常定位文本（首因）

    // 主循环：逐参考样本区间积分（区间 j 的控制输入＝tauRef[j] 常量——
    // 采样保持；子步不跨样本边界）。
    for (std::size_t j = 0; j + 1 < usableSamples; ++j) {
        // 取消轮询（§9.3 周期性查询——每区间一次；取消非错误 UX-03）。
        if (context.cancellationRequested()) {
            out.cancelled = true;
            break;
        }
        context.reportProgress(
            static_cast<std::uint8_t>(std::min<std::size_t>(
                100, (j * 100) / std::max<std::size_t>(intervalCount, 1))),
            std::string_view{"dyn-forward-interval"});

        // 区间 j 左端样本 τ_ref 非有限（控制输入缺失）——仿真终止（部分
        // 数据语义：已完成区间数据保留判定，见文件尾判定段）。
        if (tauRef[j].numericState != SampleNumericState::Ok) {
            break;
        }

        const double tBegin = tauRef[j].t;      // 区间起点（s）
        const double tEnd = tauRef[j + 1].t;    // 区间终点（s）
        const std::vector<double>& tauCtrl = tauRef[j].tauTotal;  // 采样保持输入
        double tCur = tBegin;                   // 当前仿真时刻（s）

        // RK4 子步循环（h＝min(maxStepS, 剩余时长)——不跨参考样本边界）。
        while (tCur < tEnd) {
            const double h = std::min(request.forwardCheck.maxStepS, tEnd - tCur);
            // 数值防御：区间端点舍入残差（h 可能为 0——直接收口推进）。
            if (!(h > 0.0)) {
                tCur = tEnd;
                break;
            }

            // ---- RK4 四 stage（经典显式格式；全部 stage 共用区间起点
            //      时刻 tBegin 提取变体——采样保持语义见文件头）----
            bool cancelledNow = false;
            bool singularNow = false;

            // k1 ＝ f(y_j)
            std::vector<double> k1q = qd;  // q̇（状态一阶分量恒为速度）
            if (!evalAcceleration(q, qd, tBegin, tauCtrl, cancelledNow, singularNow)) {
                if (cancelledNow) { out.cancelled = true; break; }
                singular = true;
                anomalyDetail = "质量矩阵线性求解失败（奇异或病态）：t="
                    + std::to_string(tCur) + " s，RK4 stage 1";
                break;
            }
            const std::vector<double> k1qd = qdd;

            // k2 ＝ f(y_j + h/2·k1)
            std::vector<double> q2(n), qd2(n);
            for (std::size_t i = 0; i < n; ++i) {
                q2[i] = q[i] + 0.5 * h * k1q[i];
                qd2[i] = qd[i] + 0.5 * h * k1qd[i];
            }
            if (!evalAcceleration(q2, qd2, tBegin, tauCtrl, cancelledNow, singularNow)) {
                if (cancelledNow) { out.cancelled = true; break; }
                singular = true;
                anomalyDetail = "质量矩阵线性求解失败（奇异或病态）：t="
                    + std::to_string(tCur) + " s，RK4 stage 2";
                break;
            }
            const std::vector<double> k2q = qd2;
            const std::vector<double> k2qd = qdd;

            // k3 ＝ f(y_j + h/2·k2)
            for (std::size_t i = 0; i < n; ++i) {
                q2[i] = q[i] + 0.5 * h * k2q[i];
                qd2[i] = qd[i] + 0.5 * h * k2qd[i];
            }
            if (!evalAcceleration(q2, qd2, tBegin, tauCtrl, cancelledNow, singularNow)) {
                if (cancelledNow) { out.cancelled = true; break; }
                singular = true;
                anomalyDetail = "质量矩阵线性求解失败（奇异或病态）：t="
                    + std::to_string(tCur) + " s，RK4 stage 3";
                break;
            }
            const std::vector<double> k3q = qd2;
            const std::vector<double> k3qd = qdd;

            // k4 ＝ f(y_j + h·k3)
            for (std::size_t i = 0; i < n; ++i) {
                q2[i] = q[i] + h * k3q[i];
                qd2[i] = qd[i] + h * k3qd[i];
            }
            if (!evalAcceleration(q2, qd2, tBegin, tauCtrl, cancelledNow, singularNow)) {
                if (cancelledNow) { out.cancelled = true; break; }
                singular = true;
                anomalyDetail = "质量矩阵线性求解失败（奇异或病态）：t="
                    + std::to_string(tCur) + " s，RK4 stage 4";
                break;
            }
            const std::vector<double> k4q = qd2;
            const std::vector<double> k4qd = qdd;

            // 状态推进（加权组合 1/6·(k1+2k2+2k3+k4)——RK4 标准格式）。
            for (std::size_t i = 0; i < n; ++i) {
                q[i] += (h / 6.0)
                        * (k1q[i] + 2.0 * k2q[i] + 2.0 * k3q[i] + k4q[i]);
                qd[i] += (h / 6.0)
                         * (k1qd[i] + 2.0 * k2qd[i] + 2.0 * k3qd[i] + k4qd[i]);
            }
            tCur += h;
            ++out.integratorSteps;

            // 异常检测（DYN-05 本义——每子步检查，首因即停）：
            //   非有限（NFR-COR-03 拒绝语义）或量级超检出界→发散。
            if (!allFinite(q) || !allFinite(qd)) {
                diverged = true;
                anomalyDetail = "积分状态非有限（q/q̇ 含 NaN 或 Inf）：t="
                    + std::to_string(tCur) + " s——积分发散（§6.1 ④异常检测）";
                break;
            }
            if (exceedsDivergenceBound(q) || exceedsDivergenceBound(qd)) {
                diverged = true;
                anomalyDetail = "积分状态量级超发散检出界（|·|>"
                    + std::to_string(kForwardDivergenceMagnitude) + "）：t="
                    + std::to_string(tCur) + " s——积分发散（§6.1 ④异常检测）";
                break;
            }
        }
        if (out.cancelled || diverged || singular) {
            break;  // 终止面：取消/发散/奇异——停止仿真（数据保留至故障点）
        }

        // ---- 区间完成：与参考段末端样本逐元素比较（时序图④）----
        // 末端样本自身非有限（参考状态缺失）→ 该样本不参与比较（样本级
        // 处置；其 τ_ref 行的素材已透传）。
        if (tauRef[j + 1].numericState != SampleNumericState::Ok) {
            continue;
        }
        const InverseDynSampleInput& ref = request.samples[j + 1];
        for (std::size_t i = 0; i < n; ++i) {
            const double eq = q[i] - ref.q[i];      // e_q（rad 或 m）
            const double eqd = qd[i] - ref.qd[i];   // e_qd（rad/s 或 m/s）
            sumSqQ += eq * eq;
            sumSqQd += eqd * eqd;
            const double aq = std::abs(eq);
            const double aqd = std::abs(eqd);
            if (aq > maxErrQv) {
                maxErrQv = aq;
                tMaxQ = tEnd;  // 首达时刻（严格 >——同值取时间轴首个，确定性）
            }
            if (aqd > maxErrQdv) {
                maxErrQdv = aqd;
                tMaxQd = tEnd;
            }
        }
        ++compared;
    }

    // 取消观察补偿：RK4 内层 break 的 cancelled 位已置；此处统一出口。
    if (out.cancelled) {
        out.state = DynamicsValidity::ForwardCheckState::NotRun;
        return out;
    }

    // ---- 异常检出→Failed（§6.1 ④——发散/数值异常；失败不判模型无效，
    //      §6.3.5 建议证据项语义）----
    // 误差回填助手（部分数据也如实回填——可审计；compared==0 时误差字段
    // 保持 NaN＝未比较的显式无效，不伪造 0）。
    auto fillErrorFields = [&]() {
        if (compared == 0) { return; }
        const double denom = static_cast<double>(compared * n);
        out.maxErrQ = maxErrQv;
        out.rmsErrQ = std::sqrt(sumSqQ / denom);
        out.maxErrQd = maxErrQdv;
        out.rmsErrQd = std::sqrt(sumSqQd / denom);
        out.tOfMaxErrQ = tMaxQ;
        out.tOfMaxErrQd = tMaxQd;
        out.comparedSamples = compared;
    };

    if (diverged) {
        out.numericAnomaly = anomalyDetail;
        out.state = DynamicsValidity::ForwardCheckState::Failed;
        core::DiagnosticRecord rec = core::DiagnosticRecord::make(
            core::DiagCode{std::string{kDynFdDiverged}}, request.conditionId,
            std::optional<std::string>{}, std::optional<std::string>{},
            std::string{"dynamics 正动力学一致性检查"}, anomalyDetail,
            std::string{"减小 maxStepS 或检查激励/负载工况后重试——正动力学失败不判定"
                        "模型无效（§6.3.5）"});
        out.diagnostics.push_back(std::move(rec));
        fillErrorFields();
        return out;
    }
    if (singular) {
        out.numericAnomaly = anomalyDetail;
        out.state = DynamicsValidity::ForwardCheckState::Failed;
        core::DiagnosticRecord rec = core::DiagnosticRecord::make(
            core::DiagCode{std::string{kDynFdNumericAnomaly}}, request.conditionId,
            std::optional<std::string>{}, std::optional<std::string>{},
            std::string{"dynamics 正动力学一致性检查"}, anomalyDetail,
            std::string{"检查模型质量/惯量参数（全零物性链不可正解积分）后重试——"
                        "正动力学失败不判定模型无效（§6.3.5）"});
        out.diagnostics.push_back(std::move(rec));
        fillErrorFields();
        return out;
    }

    // ---- 判定出口（时序图④）----
    // 全程完整性：τ_ref 侧截断（rneaComplete=false）或区间左端非有限 break
    // 使 compared < 预期区间数时，比较数据不完整——有超阈证据随时可判
    // Failed（有据失败成立）；否则判 Passed 必须全程完成（部分区间的通过
    // 不代表全程一致——不伪造，§4.6 精神）。
    if (maxErrQv > request.forwardCheck.toleranceQ
        || maxErrQdv > request.forwardCheck.toleranceQd) {
        // 超阈值：Failed＋DYN-FD-CONSISTENCY-FAILED（比较型素材——§9.4
        // "实际误差/阈值/单位"；链内单位同型时填单位三要素，混合链
        // cause 文本承载＋单位句柄显式无效，见公共头类注释单位口径）。
        out.state = DynamicsValidity::ForwardCheckState::Failed;

        // 单位判定：全转动/连续→rad（rad/s）；全移动→m（m/s）；混合→无效句柄。
        bool allRotary = true;
        bool allPrismatic = true;
        const runtime::RobotChain& chain = request.model->chain();
        for (const runtime::CanonicalJoint& cj : chain.joints) {
            if (cj.type == runtime::JointType::Prismatic) { allRotary = false; }
            else { allPrismatic = false; }
        }
        const bool allRotation = allRotary;  // 全转动/连续链（含 Continuous）

        // 比较型素材（实际 max 误差 / 阈值 / 单位——位置与速度各一条）。
        // 误差值的来源语义＝DerivedReadOnly（检查器派生实测值——非用户输入、
        // 非估算：ProvenanceKind 词表最贴切项）。
        auto addComparison = [&](bool isVelocity) {
            const double actual = isVelocity ? maxErrQdv : maxErrQv;
            const double threshold = isVelocity ? request.forwardCheck.toleranceQd
                                                : request.forwardCheck.toleranceQ;
            // 单位 token：同型链取注册单位；混合链＝无效句柄（isValid()==false
            // ——"不适用"的显式标记，core UnitToken 词表无混合单位，不伪造）。
            std::optional<core::UnitToken> unit;
            if (allRotation) {
                unit = isVelocity ? core::UnitToken::find("rad/s")
                                  : core::UnitToken::find("rad");
            } else if (allPrismatic) {
                unit = isVelocity ? core::UnitToken::find("m/s")
                                  : core::UnitToken::find("m");
            }
            const std::string unitText = unit.has_value()
                                             ? std::string{unit->symbol()}
                                             : std::string{"mixed(rad/m)"};
            // 比较型三要素（actual/expected 两侧＋单位——DiagData 契约）。
            core::ComparativeFields cmp;
            const core::ValueProvenance derived =
                core::ValueProvenance::make(core::ProvenanceKind::DerivedReadOnly);
            cmp.actual.quantity = core::SourcedValue<double>::provided(actual, derived);
            cmp.actual.unit = unit.value_or(core::UnitToken{});
            cmp.expected.quantity =
                core::SourcedValue<double>::provided(threshold, derived);
            cmp.expected.unit = unit.value_or(core::UnitToken{});

            core::DiagnosticRecord rec = core::DiagnosticRecord::make(
                core::DiagCode{std::string{kDynFdConsistencyFailed}}, request.conditionId,
                std::optional<std::string>{}, std::optional<std::string>{},
                std::string{"dynamics 正动力学一致性检查"},
                std::string{isVelocity ? "速度误差超阈值" : "位置误差超阈值"}
                    + std::string{"：实际 max|e"} + (isVelocity ? "qd" : "q")
                    + std::string{"|="} + std::to_string(actual) + " > 阈值 "
                    + std::to_string(threshold) + "（单位 " + unitText
                    + "；容差来源＝config.dyn.forwardCheck 分析配置——附录 D C7）",
                std::string{"增大分析容差、减小 maxStepS 或核对模型/负载同源性后重试——"
                            "正动力学失败不判定模型无效（§6.3.5）"},
                std::optional<core::ComparativeFields>{std::move(cmp)});
            out.diagnostics.push_back(std::move(rec));
        };
        if (maxErrQv > request.forwardCheck.toleranceQ) { addComparison(false); }
        if (maxErrQdv > request.forwardCheck.toleranceQd) { addComparison(true); }

        // 误差回填（部分数据也如实回填）。
        fillErrorFields();
        return out;
    }

    // 未超阈：Passed 仅在全程完成时成立；数据不完整→NotRun（不伪造）。
    if (!rneaComplete || compared < intervalCount) {
        out.state = DynamicsValidity::ForwardCheckState::NotRun;
        return out;
    }

    // Passed：误差四元组与首达时刻回填（判定已用同一数据——字段供证据项
    // 消费）。
    out.state = DynamicsValidity::ForwardCheckState::Passed;
    fillErrorFields();
    return out;
}

}  // namespace sdurws::ird::dynamics
