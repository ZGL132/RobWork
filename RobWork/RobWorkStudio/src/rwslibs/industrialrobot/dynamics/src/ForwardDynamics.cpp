/**
 * @file   ForwardDynamics.cpp
 * @brief  正动力学响应一致性检查器实现（units/dynamics.md §6）——同源
 *         τ_ref 产出、runtime 受控引擎会话（RobWorkSim ODE 后端）推进、
 *         采样保持、负载变体会话重建、误差四元组度量、发散/异常检测与
 *         四态判定。
 *
 * 设计依据（契约面见同名公共头 ForwardDynamics.hpp 文件头）：
 *   - units/dynamics.md §6.1（时序图①→⑤——本实现按步逐步对应）、§6.2
 *     （输入与前置表）、§6.3（必须明确六条）、§10.2（契约表）、§9.4
 *     （DYN-FD-* 码）、§4.6（样本级处置——非有限不伪造）
 *   - 需求 DYN-05（明确控制输入/初始状态/步长/容差的 RobWorkSim 正动力学
 *     场景——响应一致性检查与异常检测）
 *   - GAP-FD-BUILD-01 提案执行（A3 裁决 2026-10-26）——WP-09/REUSE-T53：
 *     ODE 引擎承接面（runtime::FdEngineSession，模型构造权归 runtime），
 *     旧自持核（质量阵提取/高斯消元/RK4 四阶段）随本批删除
 *   - 任务契约 tasks/REUSE-T53.json
 *
 * 算法总览（符号约定全链统一，单位见逐处注释）：
 *   - τ_ref（§6.1 ①）：参考段经公共 InverseDynamicsEvaluator::evaluate
 *     整段产出（同一冻结模型/重力/负载/工况——⑤共源的结构性强制）；
 *   - 引擎会话（§6.1 ②③）：runtime::buildFdEngineSession 以 CanonicalModel
 *     私有物化副本装配 ODE 引擎——请求 gravityBase（基座系，m/s²）由会话
 *     经 R_world_base 投影为世界系重力（与 τ_ref 同一重力语义）；控制输入
 *     τ_ctrl 按采样保持（区间内常量）；子步长 h＝min(maxStepS, 剩余时长)
 *     （子步不跨参考样本边界——采样保持区间完整性）；
 *   - 负载变体（§5.4）：变体切换只发生在参考样本区间边界——挂载集变化时
 *     以当前 (q,q̇) 重建会话（法兰组合替代值按 §5.4 同规则合成，工具/负载
 *     提取数学与逆动力学 extractEndComponent/composeFlangeVariant 同源——
 *     本 TU 自持同式镜像，合并归后续卫生批登记，REUSE-T52 镜像先例）；
 *   - 异常检测（§6.1 ④）：逐子步检查状态非有限或量级超发散检出界；会话
 *     物化/推进失败（含移动关节链与物性能力缺失的引擎侧稳定诊断）→
 *     DYN-FD-NUMERIC-ANOMALY 值面（不抛穿，四态契约闭合）。
 */

#include <sdurws/ird/dynamics/ForwardDynamics.hpp>

#include <sdurws/ird/dynamics/DiagCodes.hpp>  // DYN-* 码值常量（唯一书写点——产码共用）
#include <sdurws/ird/core/Provenance.hpp>     // core::SourcedValue/ProvenanceKind（比较型素材）
#include <sdurws/ird/core/Units.hpp>          // core::UnitToken（比较型单位三要素）
#include <sdurws/ird/runtime/FdEngineSession.hpp>  // runtime 受控引擎会话（WP-09 承接面）

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
#include <utility>

namespace sdurws { namespace ird { namespace dynamics {

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

// =====================================================================
// 末端合成（引擎会话法兰替代值的 §5.4 变体承载——与逆动力学提取/合成
// 数学期同源镜像；见文件头"负载变体"段与 REUSE-T52 镜像先例）。
// =====================================================================

/**
 * @brief 末端组成件（工具/负载在法兰系的静参数——每变体合成一次）。
 *
 * 所有量在**法兰系**（末连杆系）表示：comF 为质心在法兰系位置（单位 m，
 * 已含"质心相对 TCP 的偏置经 T_flange_tcp 旋转"）；inertiaC 为对质心、
 * 法兰系姿态的惯量张量（单位 kg·m²）。与 InverseDynamics.cpp 的
 * EndComponent 同构（物理值面——估算标记/诊断定位不进引擎会话）。
 */
struct FdEndComponent {
    double mass = 0.0;                          ///< 质量，单位 kg
    rw::math::Vector3D<double> comF{};          ///< 质心，法兰系下表示，单位 m
    rw::math::InertiaMatrix<double> inertiaC{}; ///< 对质心惯量（法兰系姿态），kg·m²
};

/**
 * @brief 末端组成件提取（§5.4 物理语义的同源镜像——估算标记面不复制）。
 *
 * 物理规则（与 extractEndComponent 逐式同源，D-DYN-6）：com 缺失→取安装
 * 点（TCP 在法兰系位置）；inertia 缺失→点质量模型（零张量）；值已提供→
 * TCP 姿态旋转到法兰系（R·I·Rᵀ）。质量必填（Provided 且有限＞0）——
 * 违约即调用方错误（τ_ref 主调用的同款防线先行，此处为会话侧防御）。
 *
 * @param massF          [in] 质量（必填）
 * @param comF           [in] 质心（TCP 系，可缺失）
 * @param inertiaF       [in] 惯量（质心系、TCP 姿态，可缺失）
 * @param tcpPosInFlange [in] TCP 安装点在法兰系位置，单位 m
 * @param tcpRotToFlange [in] TCP 系→法兰系旋转
 * @return 法兰系组成件（物理值面）
 *
 * @throws DynamicsError mass 非法（调用方错误——防御轨；正常路径被 τ_ref
 *         主调用的提取防线先行拦截，此处不可达防御）
 */
FdEndComponent extractFdEndComponent(
    const core::SourcedValue<double>& massF,
    const core::SourcedValue<rw::math::Vector3D<double>>& comF,
    const core::SourcedValue<rw::math::InertiaMatrix<double>>& inertiaF,
    const rw::math::Vector3D<double>& tcpPosInFlange,
    const rw::math::Rotation3D<double>& tcpRotToFlange)
{
    const std::optional<double> massOpt = massF.tryValue();
    if (!massOpt.has_value() || !std::isfinite(*massOpt) || *massOpt <= 0.0) {
        throw DynamicsError("input-invalid", "末端组成件质量必填且须为有限正值（kg）"
                                             "——引擎会话变体合成防御");
    }
    FdEndComponent c;
    c.mass = *massOpt;
    if (const std::optional<rw::math::Vector3D<double>> com = comF.tryValue()) {
        c.comF = tcpPosInFlange + tcpRotToFlange * *com;  // TCP 偏置→法兰系（m）
    } else {
        c.comF = tcpPosInFlange;  // 缺失→安装点（D-DYN-6 保守估算的物理面）
    }
    if (const std::optional<rw::math::InertiaMatrix<double>> inertia = inertiaF.tryValue()) {
        c.inertiaC = tcpRotToFlange * *inertia * rw::math::inverse(tcpRotToFlange);
    } else {
        c.inertiaC = rw::math::InertiaMatrix<double>();  // 零张量（点质量——显式化）
    }
    return c;
}

/**
 * @brief 把工具与挂载负载合成进末连杆（法兰）物性替代值。
 *
 * 合成规则（与 composeFlangeVariant 逐式同源——全部在法兰系，单位 kg/m/
 * kg·m²）：总质量 M＝法兰基线＋Σ组件；总质心＝质量加权一阶矩；对总质心
 * 的合成惯量＝Σ[I_k＋m_k·(|d_k|²E−d_k d_kᵀ)]（平行轴定理，d_k 为各体质心
 * 到总质心的位移）。★ 合成基项＝末连杆规范物性（权威法兰系）——绝不上次
 * 合成值（变体重复合成会重复累加）。
 *
 * 边界：M ≤ 0（全部物性缺失的极端形态）——质心/惯量取零（力矩贡献为零、
 * 数值安全，与逆动力学同式）；物性缺失链在会话物化时已被 S7 门控拒绝
 * （DwcCompileFailed→DYN-FD-NUMERIC-ANOMALY 值面），此分支为镜像自证。
 *
 * @param flangeMass    [in] 末连杆基线质量，单位 kg
 * @param flangeCom     [in] 末连杆基线质心，法兰系下表示，单位 m
 * @param flangeInertia [in] 末连杆基线对质心惯量（法兰系姿态），kg·m²
 * @param mounted       [in] 当前挂载的组成件（工具＋负载——法兰系静参数）
 * @param outOverride   [out] 引擎会话法兰替代值（末连杆系表示）
 */
void composeFlangeOverride(double flangeMass, const rw::math::Vector3D<double>& flangeCom,
                           const rw::math::InertiaMatrix<double>& flangeInertia,
                           const std::vector<FdEndComponent>& mounted,
                           runtime::FdFlangeOverride& outOverride)
{
    double totalMass = flangeMass;
    rw::math::Vector3D<double> firstMoment = flangeMass * flangeCom;  // 一阶矩，kg·m
    for (const FdEndComponent& p : mounted) {
        totalMass += p.mass;
        firstMoment += p.mass * p.comF;
    }
    rw::math::Vector3D<double> compositeCom{};
    rw::math::InertiaMatrix<double> compositeInertia = rw::math::InertiaMatrix<double>();
    if (totalMass > 0.0) {
        compositeCom = firstMoment / totalMass;  // 质心（法兰系，m）
        // 平行轴定理逐项累加（确定性求和序：基线→组件依挂载序）。
        auto addParallelAxis = [&compositeInertia, &compositeCom](
                                   const rw::math::InertiaMatrix<double>& inertia,
                                   const rw::math::Vector3D<double>& comF, double mass) {
            const rw::math::Vector3D<double> d = comF - compositeCom;  // m
            const double dd = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
            for (int r = 0; r < 3; ++r) {
                for (int c = 0; c < 3; ++c) {
                    const double dOuter = d[r] * d[c];
                    const double correction =
                        ((r == c) ? dd : 0.0) - dOuter;  // δrc·|d|² − d_r·d_c
                    compositeInertia(r, c) += inertia(r, c) + mass * correction;
                }
            }
        };
        addParallelAxis(flangeInertia, flangeCom, flangeMass);
        for (const FdEndComponent& p : mounted) {
            addParallelAxis(p.inertiaC, p.comF, p.mass);
        }
    }
    outOverride.mass = totalMass;              // kg
    outOverride.masscenter = compositeCom;     // m（末连杆系）
    outOverride.inertia = compositeInertia;    // kg·m²（对质心、末连杆系姿态）
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
        // 封闭词表（§4.2——当前唯一锁定值 kForwardIntegratorToken；历史值
        // "rk4-fixed" 已随自持核退役，接受它＝以旧身份冒充新引擎，拒绝）。
        throw DynamicsError("input-invalid",
                            "forwardCheck.integratorToken 不在封闭词表（实际 '"
                                + request.forwardCheck.integratorToken + "'，词表值 '"
                                + std::string{kForwardIntegratorToken}
                                + "'）——拒绝检查");
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
    // F-644 前置防线：样本 q/qd 长度一致性（调用方契约违约——fail-fast）。
    // 为什么必须在入口拦截：第 1 步的初始状态非有限扫描（下方）在 τ_ref
    // 主调用（其内部有 DYN-DIMENSION-MISMATCH 全量防线）**之前**执行——
    // 若 q/qd 长度不一致，该扫描以 q 的长度索引 qd 会越界读取（未定义
    // 行为）。q 与 qd 是同一样本同一自由度向量的位置/速度列，长度不一致
    // 属组装错误，按既有 input-invalid 异常面拒绝，消息说明自由度不匹配；
    // qdd 及各向量对模型自由度 n 的匹配性由 τ_ref 主调用既有防线承载
    // （此处不重复——前置只补"本函数先于主调用触碰数据"的缺口）。
    for (std::size_t k = 0; k < request.samples.size(); ++k) {
        const InverseDynSampleInput& s = request.samples[k];
        if (s.q.size() != s.qd.size()) {
            throw DynamicsError("input-invalid",
                                "样本状态向量长度不一致——自由度不匹配（样本序 "
                                    + std::to_string(k) + "：q 长度 "
                                    + std::to_string(s.q.size()) + "，qd 长度 "
                                    + std::to_string(s.qd.size()) + "，调用方契约违约）");
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
        // F-644 纵深防御：q 与 qd 各按**自身**长度索引（入口一致性防线之上
        // 的保险——若防线将来被移除，本扫描也不得越界读取）。
        const InverseDynSampleInput& s0 = request.samples.front();
        bool initialNonFinite = false;
        for (std::size_t i = 0; i < s0.q.size(); ++i) {
            if (!std::isfinite(s0.q[i])) {
                initialNonFinite = true;
                break;
            }
        }
        for (std::size_t i = 0; i < s0.qd.size(); ++i) {
            if (!std::isfinite(s0.qd[i])) {
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
    // §4.6）。F-581：RNEA 数值失败时逆动力学按 §5.6 只产**失败关节**的行
    // 并**自该样本起截断后续产出**——总行数可小于 nSample×n。按时刻 t 匹配
    // 的游标消费：本样本行不足 n 行、或产出耗尽、或行态非 Ok＝该样本 τ_ref
    // 不可信（不参与仿真/比较，样本级处置 §4.6——诊断已由 evaluate 透传，
    // 此处不重复产码）。
    const std::size_t n = rneaRequest.samples.front().q.size();  // 可动关节数
    struct SampleTorque {
        std::vector<double> tauTotal;          ///< 逐关节全量 τ_ref（N·m 或 N）
        SampleNumericState numericState;       ///< 该样本数值状态（非 Ok 不参与仿真/比较）
        double t;                              ///< 样本时刻，单位 s（透传自请求）
    };
    std::vector<SampleTorque> tauRef;
    tauRef.reserve(nSample);
    std::size_t rowCursor = 0;  // 产出行游标（跨样本单调推进——产出行序即请求样本序）
    for (std::size_t j = 0; j < nSample; ++j) {
        SampleTorque st;
        st.t = rneaRequest.samples[j].t;
        st.numericState = SampleNumericState::Ok;
        st.tauTotal.assign(n, 0.0);
        std::size_t rowsTaken = 0;
        while (rowsTaken < n && rowCursor < rneaOut.samples.size()
               && rneaOut.samples[rowCursor].t == st.t) {
            // 行填装按消费序写入（产出行本样本内即 jointIndex 升序；行态
            // 非 Ok 时整个样本标记不可信——tauTotal 数值不再被消费）。
            const DynamicsSample& row = rneaOut.samples[rowCursor];
            st.tauTotal[rowsTaken] = row.tauTotal;
            if (row.numericState != SampleNumericState::Ok) {
                st.numericState = row.numericState;
            }
            ++rowsTaken;
            ++rowCursor;
        }
        if (rowsTaken < n && st.numericState == SampleNumericState::Ok) {
            // 行数不足却无任何非 Ok 行标记（产出与请求失步——防御面）：
            // 同样按不可信处置，不把缺行当零力矩消费。
            st.numericState = SampleNumericState::NonFiniteOutput;
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

    // ---- 第 3～5 步：正向仿真＋误差度量＋异常检测（时序图②③④——
    //      runtime 受控引擎会话承载）----
    // 仿真状态 y=(q,q̇)——初始状态取参考段起点（§6.2；第 1 步已验证有限）。
    std::vector<double> q = request.samples.front().q;    // 权威角（rad 或 m）
    std::vector<double> qd = request.samples.front().qd;  // 关节速度（rad/s 或 m/s）

    // ---- 变体合成前置（§5.4 法兰静参数的会话侧提取——工具＋负载）：
    //      TCP 安装几何与末连杆基线自模型提取一次（只读面）。
    const runtime::RobotChain& chain = request.model->chain();
    const runtime::CanonicalLink& flangeLink = chain.links.back();
    const double flangeMass = flangeLink.mass.tryValue().value_or(0.0);  // kg（缺失→0：会话侧门控拒绝）
    const rw::math::Vector3D<double> flangeCom =
        flangeLink.centerOfMass.tryValue().value_or(rw::math::Vector3D<double>());  // m
    const rw::math::InertiaMatrix<double> flangeInertia =
        flangeLink.inertia.tryValue().value_or(rw::math::InertiaMatrix<double>());  // kg·m²

    rw::math::Vector3D<double> tcpPosInFlange{};   // TCP 安装点，法兰系，m（无工具＝法兰原点）
    rw::math::Rotation3D<double> tcpRotToFlange(1, 0, 0, 0, 1, 0, 0, 0, 1);  // TCP→法兰（无工具＝恒等）
    bool hasTool = false;
    FdEndComponent toolComponent;
    if (request.model->defaultTcpIndex().has_value()) {
        const runtime::CanonicalTool& tool =
            request.model->tools().at(*request.model->defaultTcpIndex());
        tcpPosInFlange = tool.tcpOffset.P();
        tcpRotToFlange = tool.tcpOffset.R();
        toolComponent = extractFdEndComponent(tool.mass, tool.centerOfMass, tool.inertia,
                                              tcpPosInFlange, tcpRotToFlange);
        hasTool = true;
    }

    // 挂载集计算（§5.4 事件时间线的区间起点口径——"样本时刻 t ≥ tEvent
    // 生效"，与 τ_ref 侧 evaluate 同规则——两侧同变体的结构性强制）。
    auto computeMounted = [&](double t, std::vector<bool>& mounted) {
        mounted.assign(request.payloads.size(), false);
        for (std::size_t p = 0; p < request.payloads.size(); ++p) {
            bool isMounted = request.payloads[p].initiallyMounted;
            for (const PayloadEvent& e : request.events) {
                if (e.payloadIndex == p && t >= e.tEvent) {
                    isMounted = (e.kind == PayloadEvent::Kind::Grasp);
                }
            }
            mounted[p] = isMounted;
        }
    };
    auto buildComponents = [&](const std::vector<bool>& mounted,
                               std::vector<FdEndComponent>& parts) {
        parts.clear();
        if (hasTool) { parts.push_back(toolComponent); }
        for (std::size_t p = 0; p < request.payloads.size(); ++p) {
            if (!mounted[p]) { continue; }
            const EndEffectorPayload& payload = request.payloads[p];
            // 负载 com/惯量在 TCP 系表达（§5.4——与逆动力学提取同基准）；
            // 非法质量经 extractFdEndComponent 防御（τ_ref 主调用已先行拦截）。
            parts.push_back(extractFdEndComponent(payload.mass, payload.centerOfMass,
                                                  payload.inertia, tcpPosInFlange,
                                                  tcpRotToFlange));
        }
    };

    // 数值异常/发散的终止面（记录首因——诊断素材只产一条；先于会话助手
    // 声明——ensureSession 按引用捕获写入）。
    bool diverged = false;       ///< 发散检出（DYN-FD-DIVERGED）
    bool singular = false;       ///< 会话物化/推进失败（DYN-FD-NUMERIC-ANOMALY）
    std::string anomalyDetail;   ///< 异常定位文本（首因）

    // 会话重建助手：按当前 (q,q̇) 与挂载集物化引擎会话；任何失败（含移动
    // 关节链/物性能力缺失的 runtime 稳定诊断）映射 DYN-FD-NUMERIC-ANOMALY
    // 值面（四态契约闭合——不抛穿；detail 携带会话侧定位）。
    runtime::FdFlangeOverride flangeOverride;  // 复用缓冲（每变体重填）
    std::vector<bool> mountedNow;
    std::vector<bool> sessionMounted;
    bool hasSession = false;
    runtime::FdEngineSession session;
    auto ensureSession = [&](double t) {
        computeMounted(t, mountedNow);
        if (hasSession && mountedNow == sessionMounted) {
            return true;  // 变体未变——复用当前会话
        }
        std::vector<FdEndComponent> parts;
        buildComponents(mountedNow, parts);
        // 法兰替代面：有工具或有挂载负载时合成替代值（S7 工具体为静态惰性
        // 体——替代值是工具/负载进入引擎动力学的唯一通道）；纯基线变体传
        // 空指针＝S7 编译物性直用（与合成基线逐值等价，单一实现两形态）。
        const runtime::FdFlangeOverride* overridePtr = nullptr;
        if (hasTool || !parts.empty()) {
            composeFlangeOverride(flangeMass, flangeCom, flangeInertia, parts, flangeOverride);
            overridePtr = &flangeOverride;
        }
        try {
            session = runtime::buildFdEngineSession(*request.model, request.gravityBase, q, qd,
                                                    overridePtr);
        }
        catch (const std::exception& e) {
            // 会话物化失败＝引擎侧稳定诊断（runtime/* 码面）——映射既有
            // NUMERIC-ANOMALY 值面（全零物性链不可正解积分旧语义的延续面）。
            singular = true;
            anomalyDetail = std::string{"引擎会话物化失败（"} + e.what() + "）：t="
                            + std::to_string(t) + " s";
            return false;
        }
        sessionMounted = mountedNow;
        hasSession = true;
        return true;
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

    // 主循环：逐参考样本区间积分（区间 j 的控制输入＝tauRef[j] 常量——
    // 采样保持；子步不跨样本边界；变体切换＝区间边界会话重建）。
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

        // 变体检查/会话就绪（区间起点时刻——变体切换只发生在区间边界）。
        if (!ensureSession(tauRef[j].t)) {
            break;  // 物化失败——singular/anomalyDetail 已置
        }

        const double tBegin = tauRef[j].t;      // 区间起点（s）
        const double tEnd = tauRef[j + 1].t;    // 区间终点（s）
        session.setForceTargets(tauRef[j].tauTotal);  // 采样保持输入（N·m 或 N）
        double tCur = tBegin;                   // 当前仿真时刻（s）

        // 子步循环（h＝min(maxStepS, 剩余时长)——不跨参考样本边界）。
        while (tCur < tEnd) {
            const double h = std::min(request.forwardCheck.maxStepS, tEnd - tCur);
            // 数值防御：区间端点舍入残差（h 可能为 0——直接收口推进）。
            if (!(h > 0.0)) {
                tCur = tEnd;
                break;
            }
            try {
                session.step(h);  // 引擎推进（摩擦补偿/回写在会话内——状态随之推进）
            }
            catch (const std::exception& e) {
                singular = true;
                anomalyDetail = std::string{"引擎推进失败（"} + e.what() + "）：t="
                                + std::to_string(tCur) + " s";
                break;
            }
            tCur += h;
            ++out.integratorSteps;

            // 状态读回（设备 postUpdate 回写值——q/qd 随推进更新）。
            q = session.q();    // rad 或 m
            qd = session.qd();  // rad/s 或 m/s

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

    // 取消观察补偿：会话循环内 break 的 cancelled 位已置；此处统一出口。
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
            std::string{"检查模型质量/惯量参数与关节类型（全零物性链不可正解积分、"
                        "移动关节链超出 ODE 力控承载）后重试——正动力学失败不判定"
                        "模型无效（§6.3.5）"});
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

}}}  // namespace sdurws::ird::dynamics
