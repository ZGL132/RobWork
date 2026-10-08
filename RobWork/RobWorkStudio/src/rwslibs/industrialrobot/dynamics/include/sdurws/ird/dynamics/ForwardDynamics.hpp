/**
 * @file   ForwardDynamics.hpp
 * @brief  正动力学响应一致性检查（units/dynamics.md §6/§10.2，DYN-05）——
 *         同源逆动力学力矩回放、确定性 RK4 积分、误差四元组度量与异常
 *         检测（发散/数值异常），建议证据项 dyn.forward-dynamics-consistency
 *         的产出器。
 *
 * 设计依据：
 *   - units/dynamics.md §6 全章（§6.1 正/逆一致性时序图、§6.2 输入与前置、
 *     §6.3 必须明确六条）、§10.2（IForwardDynamicsValidator 契约表）、
 *     §4.2（config.dyn.forwardCheck 六字段）、§4.4（DynamicsValidity.
 *     ForwardCheckState 四态——本头 outcome.state 的枚举来源）、§9.4
 *     （DYN-FD-* 码）
 *   - 需求 DYN-05（使用明确控制输入、初始状态、步长和容差的 RobWorkSim
 *     正动力学场景进行响应一致性检查和异常检测）
 *   - 决策 D-DYN-2（RNEA 本域自实现、输入取自编译产物——本头复用同一
 *     评估器公共接口，⑤共源强制的结构性实现）、D-DYN-3（重力投影唯一
 *     消费 gravityBase，零二次旋转）
 *   - 任务契约 tasks/foundation/WP-17-T05.json（acceptance：响应一致性
 *     检查与异常检测用例通过）
 *
 * ★ 正动力学引擎选型登记（P-DYN-7 落位锁定，2026-10-08——实测结论，详见
 *   单元卡 §1.2 T05 实现登记注与 §15.2 P-DYN-7 行）：
 *   本域正动力学引擎＝**同源 RNEA 质量阵/偏置力提取＋固定步长四阶龙格—
 *   库塔（RK4）积分**（integratorToken 词表锁定值 kForwardIntegratorToken
 *   ＝"rk4-fixed"）。RobWorkSim 物理引擎（rwsim::simulator::PhysicsEngine
 *   工厂形态）经实测不落位，四条依据：
 *   1. 本仓库集成构建缓存 ODE_DIR-NOTFOUND（ODE 引擎依赖未启用，源码树
 *      rwsimlibs/ode 不参与构建）；Bullet 引擎（sdurwsim_bullet）为
 *      RW_ADD_PLUGIN 动态插件注册机制（BtPlugin.cpp 实测），L2 计算库
 *      （进程内评估器、零 Qt 零插件加载器）无合法接入点，直连引擎实现类
 *      （BtSimulator）＝依赖 rwsimlibs 私有实现头，非 rwsim 公共契约；
 *   2. D-DYN-2/CM-0"不自建第二套关节链"：rwsim 引擎消费 DynamicWorkCell
 *      形态模型，工况条件负载（payloads/events）需向引擎场景注入负载体
 *      （运行中 addBody）——rwsim 公共 API 无此消费先例，注入即自建第二
 *      套链；本头经公共 evaluate 接口消费，τ_ref 与正向仿真天然同变体
 *      （§6.1 ⑤共源强制——非法示例"用另一工况的负载做仿真"在本接口面
 *      结构上不可表达）；
 *   3. §10.2 确定性行"同配置同输入→等价误差序列（积分确定）"：固定步长
 *      RK4 为纯确定性数值路径；迭代约束求解器（Bullet LCP）存在容差内
 *      漂移，须按"黄金算例声明对照容差"放宽——与 R1 确定性口径冲突；
 *   4. 冒烟模式纪律（DTB §5.1 双模式；T03 确立的零外联符号面）：rwsim
 *      引擎符号为框架 .cpp 外联，冒烟配置树无框架库可链——本实现全链
 *      零外联符号（只消费已冒烟验证的公共头），双模式同源可构建。
 *   "RobWorkSim 正动力学场景"的承接口径与 DYN-01 的 T03 验收先例同款：
 *   场景本体由 RobWorkSim 刚体模型的编译产物（CanonicalModel——质量/质
 *   心/惯量/轴几何/摩擦/工具偏置全部经它读取）构造，四要素（控制输入/
 *   初始状态/步长/容差）全部显式入 ForwardCheckSettings 与请求。
 *
 * ★ 契约形态微调（诚实登记，DTB §5.4 精神——单元卡 §1.2/§10.2 同步登记）：
 *   1. §10.2 的 IForwardDynamicsValidator 抽象接口落位为域内具体类
 *      ForwardDynamicsValidator（T03 IInverseDynamicsEvaluator 先例同款
 *      ——evidence IEngineeringEvaluator 适配与注册面随 WP-17-T10 装配
 *      冻结）；
 *   2. request 为域内 DTO（ForwardCheckRequest）——参考段激励样本数组
 *      ＋forwardCheck 配置（§4.2 的 forwardCheck 六字段随本头承载；
 *      DynConfig.hpp 的 DynamicsAnalysisConfiguration 全量配置面与
 *      canonical 编码随 WP-17-T10 装配落位，本任务不预建占位）；
 *   3. τ_ref 由本检查器内部经公共 InverseDynamicsEvaluator::evaluate
 *      产出（§6.1 时序图第①步的检查器内化）——调用方不可注入外部
 *      τ_ref（防止"τ_ref 与仿真不同源"的共源违约，§10.2 非法示例）；
 *   4. §10.2 outcome 的 tOfMaxErr 单字段增列为 tOfMaxErrQ/tOfMaxErrQd
 *      两位（§6.1 ④"各最大误差发生时间"的逐量承载）；payload 扩展块
 *      （误差序列）不进本结构——DYN-08 回放数据面归 T08。
 *
 * 背景说明（一致性检查的方法论边界，§6.3.1/§6.3.5）：
 *   正/逆两路在本实现中共享同一 RNEA 内核（经公共接口）——一致性结论
 *   的发现力集中在"积分收敛性＋采样保持＋变体切换时序"的数值链路，而
 *   非"两套独立模型实现的对拍"。这是 P-DYN-7 实测选型的已知代价，如实
 *   登记于单元卡；模型参数错误（质量/惯量错值）的检出归建模/编译链门禁
 *   （MDL-06）与黄金算例（§11.2 V-01 解析对照），不是本检查的职责。
 *   正动力学失败（Failed）不能直接判定模型无效（§6.3.5）——Failed 只进
 *   建议证据项 Invalid＋warning 诊断。
 *
 * 线程安全：评估器实例无状态（每任务一实例——卡 §10.0）；validate 内部
 *   全部局部值，输入只读；仿真状态为栈/局部值（无共享可变状态——§6.3.6
 *   "不触碰共享 RuntimeSnapshot"在本接口面结构成立：CanonicalModel 为
 *   const 只读引用，零 State 句柄）。确定性：同（输入字节＋配置）→等价
 *   产出（固定步长 RK4 纯确定；无随机源/时钟/locale 依赖——§10.0）。
 */

#ifndef IRD_DYNAMICS_FORWARDDYNAMICS_HPP
#define IRD_DYNAMICS_FORWARDDYNAMICS_HPP

#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sdurws/ird/core/DiagData.hpp>      // core::DiagnosticRecord（ERR-01 素材）
#include <sdurws/ird/core/Identity.hpp>      // core::ObjectId（工况对象身份）
#include <sdurws/ird/dynamics/DynTypes.hpp>  // DynamicsValidity（ForwardCheckState 四态）
#include <sdurws/ird/dynamics/Errors.hpp>    // DynamicsError（调用方错误 fail-fast 异常轨）
#include <sdurws/ird/dynamics/InverseDynamics.hpp>  // InverseDynSampleInput/
                                                    //   EndEffectorPayload/PayloadEvent
                                                    //   （参考段数据面——同源 DTO）
#include <sdurws/ird/evidence/Evaluator.hpp> // evidence::IEvaluationContext（取消/进度宿主）
#include <sdurws/ird/runtime/CanonicalModel.hpp> // runtime::CanonicalModel（输入权威）

namespace sdurws::ird::dynamics {

// =====================================================================
// 版本 token（§4.4 身份块分量——常量唯一书写点，消费方禁另写字面量）。
// =====================================================================

/// 正动力学积分器封闭词表的当前锁定值（§4.2 forwardCheck.integratorToken
/// ——P-DYN-7 落位锁定，2026-10-08；扩展新积分器＝先单元卡 §4.2 增量修订
/// 词表、再追加常量——既有值不重排，NFR-COR-02 确定性序）。
inline constexpr std::string_view kForwardIntegratorToken = "rk4-fixed";

/// 正动力学一致性检查的算法实现版本 token（§4.4 身份分量——算法语义变更
/// 〔积分器/误差度量/判定规则〕＝换新 token，旧结果不与新结果混比）。
inline constexpr std::string_view kForwardCheckAlgorithmVersion = "dyn-forward-check/1";

// =====================================================================
// forwardCheck 分析配置（§4.2 表 forwardCheck.* 六字段的域内 DTO——
// microadjustment 2：DynConfig.hpp 全量配置面随 T10，见文件头登记）。
// =====================================================================

/**
 * @brief 正动力学一致性检查配置（§4.2 forwardCheck.* 原文语义；DYN-05
 *        "控制输入/初始状态/步长/容差明确"的配置面——控制输入与初始状态
 *        来自参考段请求，步长与容差在此）。
 *
 * 产品运行校验只依赖本配置的分析容差（toleranceQ/toleranceQd——附录 D
 * C7"必须有默认值或配置来源"的配置来源侧）；黄金算例的测试对照容差由
 * 测试文件另声明（两类容差分离，C7 口径——§6.3.3）。
 * 值语义纯结构；线程安全。
 */
struct ForwardCheckSettings {
    /**
     * @brief 检查开关（§4.2 mode——Quick 默认 Skip〔筛选语义〕、Verified
     *        默认 Standard；本结构不预设需求侧默认，初值 Skip 为"未配置
     *        ＝不执行"的保守缺省，调用方显式给 Standard 才执行仿真）。
     */
    enum class Mode { Skip, Standard };

    Mode mode = Mode::Skip;        ///< Skip＝不适用（NotApplicable，显式非缺失——
                                   ///   §6.3.4）；Standard＝执行检查
    std::string integratorToken;   ///< 积分器标识（封闭词表——当前唯一合法值
                                   ///   kForwardIntegratorToken；进身份）
    double maxStepS = 0.0;         ///< 最大积分步长，单位 s（DYN-05"步长明确"）；
                                   ///   必须 >0 且有限。实际子步长＝min(maxStepS,
                                   ///   至下一参考样本的剩余时长)——子步不跨参考
                                   ///   样本边界（采样保持区间完整性）
    double relTolerance = 0.0;     ///< 积分相对容差（DYN-05"容差明确"；进身份），
                                   ///   无量纲，必须 >0 且有限。★ 固定步长 RK4
                                   ///   无自适应容差消费点——本字段为记录性
                                   ///   身份/审计分量（不进数值路径），如实登记；
                                   ///   积分器换自适应实现时接手语义
    double toleranceQ = 0.0;       ///< 一致性判定的位置误差阈值（分析配置来源——
                                   ///   附录 D C7）；单位随关节类型（rad 或 m，
                                   ///   §4.5——同型链单一单位，混合链逐元素
                                   ///   比较见 validate 前置说明）；必须 >0 且有限
    double toleranceQd = 0.0;      ///< 速度误差阈值；单位随关节类型（rad/s 或
                                   ///   m/s）；必须 >0 且有限
};

// =====================================================================
// 检查请求（§10.2 request 的域内 DTO——microadjustment 2/3，见文件头）。
// =====================================================================

/**
 * @brief 正动力学一致性检查请求（参考段＋配置——§10.2 @param[in] request
 *        "参考段（来自逆动力学同源序列）＋forwardCheck 配置"）。
 *
 * 参考段以**激励样本数组**形态注入（q/q̇/q̈ 逐样本，T03 数据面同款 DTO）
 * ——τ_ref 在 validate 内部由同一评估器对同一模型/重力/负载/工况产出
 * （§6.1 ①），调用方无法也不需要提供力矩序列（文件头微调 3）。
 *
 * 身份与所有权（§10.0）：model 只读引用（调用方持有并保证 validate 期间
 * 存活——本域不接管、不修改、不重编译〔CM-0〕）；conditionId 必须有效
 * （EVI-02 关联键）；其余字段全部值语义。线程约束：单次 validate 内只读。
 */
struct ForwardCheckRequest {
    // —— 身份块（§10.0——缺身份拒绝正式评估）——
    core::ObjectId conditionId;      ///< 工况对象 ID（EVI-02 覆盖矩阵关联键；
                                     ///   空 id＝调用方错误 fail-fast）

    // —— 模型与重力（与逆动力学同源——§6.1 ⑤）——
    const runtime::CanonicalModel* model = nullptr; ///< 快照规范模型（唯一输入权威；
                                                    ///   空＝调用方错误 fail-fast）
    double gravityBase[3] = {0.0, 0.0, -9.81};      ///< 基座系重力加速度，单位 m/s²——
                                                    ///   与逆动力学参考段同源（同一快照
                                                    ///   gravityBase() 投影值；分量必须
                                                    ///   全部有限；本域零二次旋转 D-DYN-3）

    // —— 参考段（§6.1 ①——逆动力学同源激励；≥2 样本才构成仿真区间）——
    std::vector<InverseDynSampleInput> samples; ///< 参考段激励样本（t 严格递增、
                                                ///   逐关节维度＝模型可动关节数；
                                                ///   违例 fail-fast——同逆动力学
                                                ///   前置；样本数 <2 或初始状态
                                                ///   非有限→NotRun＋素材，§6.2）

    // —— 负载变体（与逆动力学同源——§6.1 ⑤共源强制；空＝纯工具链）——
    std::vector<EndEffectorPayload> payloads; ///< 负载池（同 InverseDynRequest 语义）
    std::vector<PayloadEvent> events;         ///< 夹取/释放事件时间线（变体切换
                                              ///   在仿真与 τ_ref 两路同步生效——
                                              ///   采样保持区间边界）

    // —— 配置（§4.2——见 ForwardCheckSettings）——
    ForwardCheckSettings forwardCheck; ///< forwardCheck 分析配置（mode/integratorToken/
                                       ///   maxStepS/relTolerance/toleranceQ/toleranceQd）
};

// =====================================================================
// 检查产出（§10.2 ForwardCheckOutcome 的域内形态——microadjustment 4）。
// =====================================================================

/**
 * @brief 正动力学一致性检查产出（§10.2 契约字段＋诊断/取消承载）。
 *
 * 四态语义（DynamicsValidity::ForwardCheckState——§6.1 ④判定表逐行）：
 *   - Passed：max|e_q|≤toleranceQ ∧ max|e_qd|≤toleranceQd（响应一致）；
 *   - Failed：超阈值（＋DYN-FD-CONSISTENCY-FAILED 比较型素材）／积分发散
 *     （＋DYN-FD-DIVERGED）／数值异常（＋DYN-FD-NUMERIC-ANOMALY）——失败
 *     不能判定模型无效（§6.3.5，建议证据项语义）；
 *   - NotRun：数据不足（样本数 <2／初始状态非有限——DYN-FD-INITIAL-STATE-
 *     MISSING 素材，不伪造 Passed，§6.2）或取消/τ_ref 侧未完成（cancelled
 *     位）；
 *   - NotApplicable：mode=Skip（显式不适用，非缺失——§6.3.4）。
 *
 * 误差字段在未执行比较时为 NaN 位模式（显式无效——不伪造 0，NFR-COR-03；
 * §4.6 无效语义同款）。值语义纯结构；线程安全。
 */
struct ForwardCheckOutcome {
    DynamicsValidity::ForwardCheckState state =          ///< 检查状态（四态——类注释）
        DynamicsValidity::ForwardCheckState::NotRun;
    std::size_t comparedSamples = 0;  ///< 参与误差比较的样本时刻数（首样本与参考
                                      ///   重合误差恒 0 不计入——实际区间数＝
                                      ///   样本数−1 中完成仿真的部分）
    double maxErrQ = std::numeric_limits<double>::quiet_NaN();  ///< max|e_q|——逐元素
                                      ///   位置误差最大绝对值（rad 或 m，随关节类型；
                                      ///   未比较＝NaN）
    double rmsErrQ = std::numeric_limits<double>::quiet_NaN();  ///< RMS(e_q)——全网格
                                      ///   （样本×关节）均方根（rad 或 m；未比较＝NaN）
    double maxErrQd = std::numeric_limits<double>::quiet_NaN(); ///< max|e_qd|（rad/s
                                      ///   或 m/s；未比较＝NaN）
    double rmsErrQd = std::numeric_limits<double>::quiet_NaN(); ///< RMS(e_qd)（rad/s
                                      ///   或 m/s；未比较＝NaN）
    double tOfMaxErrQ = std::numeric_limits<double>::quiet_NaN(); ///< max|e_q| 首个
                                      ///   达峰样本时刻，单位 s（§6.1 ④"发生时间"；
                                      ///   未比较＝NaN）
    double tOfMaxErrQd = std::numeric_limits<double>::quiet_NaN();///< max|e_qd| 首个
                                      ///   达峰样本时刻，单位 s（未比较＝NaN）
    std::optional<std::string> numericAnomaly; ///< 数值异常描述（Failed＋异常检出时
                                      ///   非空——发散量级/质量阵奇异定位；正常
                                      ///   路径＝nullopt；§10.2 numericAnomaly?）
    std::vector<core::DiagnosticRecord> diagnostics; ///< 诊断素材（DYN-FD-*＋τ_ref
                                      ///   侧透传素材——共源降级事实；环境错误轨）
    bool cancelled = false;           ///< 观测到宿主取消（§9.1.5——取消非错误 UX-03；
                                      ///   true 时 state=NotRun、误差为部分数据）
    std::uint32_t integratorSteps = 0; ///< 实际积分子步数（审计面——采样保持区间数
                                      ///   ×区间内子步数之和；取消/失败时为已执行数）
};

// =====================================================================
// 检查器（§10.2 IForwardDynamicsValidator 的具体实现载体——microadjustment 1）。
// =====================================================================

/**
 * @brief 正动力学响应一致性检查器（§6 时序图的本域实现——建议证据项
 *        dyn.forward-dynamics-consistency 的产出器）。
 *
 * 职责（一次 validate 的执行序——与 §6.1 时序图逐步对应）：
 *   0. 请求级前置校验（调用方错误 fail-fast——见 validate @throws）；
 *      mode=Skip 在配置校验前短路→NotApplicable（§6.3.4 显式不适用）；
 *   1. 数据充足性：样本数 <2 或初始状态 (q₀,q̇₀) 非有限→NotRun＋
 *      DYN-FD-INITIAL-STATE-MISSING 素材（§6.2——建议项缺失不阻断、不伪造）；
 *   2. τ_ref 产出（时序图①）：参考段经公共 InverseDynamicsEvaluator::
 *      evaluate 逐样本求力矩（同一冻结模型/重力/负载/工况——⑤共源的结构
 *      性强制）；τ_ref 侧的降级/非有限诊断透传至 outcome.diagnostics；
 *   3. 正向仿真（时序图②③）：初始状态 (q₀,q̇₀)＝参考段起点；控制输入
 *      τ_ref 按采样保持（区间 [t_j,t_{j+1}) 内取 τ_ref(t_j) 常量）；固定
 *      步长 RK4（子步不跨参考样本边界）；动力学右端＝M(q)⁻¹·(τ_ctrl −
 *      h_f(q,q̇))，其中 h_f（重力＋科氏＋摩擦）与 M 逐列提取均经公共
 *      evaluate 单样本调用（变体一致性由 evaluate 内部事件时间线在区间
 *      起点时刻生效保证——与 τ_ref 同变体）；
 *   4. 误差度量（时序图④）：每个参考样本时刻与 (q_ref,q̇_ref) 逐元素比较，
 *      累计 max/RMS 与首达时刻；中间样本激励非有限→该样本不参与统计＋
 *      DYN-NON-FINITE 素材（样本级处置，§4.6——不阻断检查）；
 *   5. 异常检测（DYN-05 本义）：积分状态非有限或量级超发散检出界→Failed＋
 *      DYN-FD-DIVERGED；质量阵线性求解失败（奇异/病态）→Failed＋
 *      DYN-FD-NUMERIC-ANOMALY；
 *   6. 判定（时序图④出口）：max|e_q|≤toleranceQ ∧ max|e_qd|≤toleranceQd
 *      →Passed，否则 Failed＋DYN-FD-CONSISTENCY-FAILED（比较型素材）。
 *
 * 发散检出界：状态向量任一分量 |·|>kForwardDivergenceMagnitude（常量 1e10，
 * 单位 rad/m、rad/s、m/s——异常检测判据而非工程判定阈值，值随黄金算例
 * 声明）即判发散（持续积分下双精度饱和前必经此界，检出确定）。
 *
 * 取消语义：每参考样本区间轮询一次宿主取消（§9.3 周期性查询）；观测到
 * 取消即返回（state=NotRun＋cancelled=true，已算误差保留可审计，零错误
 * 诊断——取消非错误，UX-03）。
 *
 * 单位口径（§4.5）：误差量纲随关节类型（转动 rad、移动 m）——max/RMS 为
 * 全向量逐元素合并值；链内关节单位同型时比较型诊断带单位三要素，混合链
 * （转动＋移动）合并值单位不单一，比较型素材以 cause 文本承载（不伪造
 * 单一单位 token——core UnitToken 词表无混合单位）。
 */
class ForwardDynamicsValidator {
public:
    ForwardDynamicsValidator() = default;

    /// 可拷贝可移动（无状态——实例仅是调用边界；每任务一实例为使用约定）。
    ForwardDynamicsValidator(const ForwardDynamicsValidator&) = default;
    ForwardDynamicsValidator& operator=(const ForwardDynamicsValidator&) = default;

    /**
     * @brief 执行正动力学响应一致性检查（§10.2 validate——四态判定与异常
     *        检测，见类注释执行序）。
     *
     * @param request [in] 检查请求（调用方持有，调用期间存活；本函数只读）
     * @param context [in] 宿主上下文（取消/进度——evidence §9.3；进度按
     *                仿真区间进度上报，取消按区间轮询）
     * @return 检查产出（四态＋误差四元组＋首达时刻＋异常描述＋诊断素材；
     *         取消/数据不足为 NotRun 部分产出——字段注释）
     *
     * @throws DynamicsError 调用方契约违约（mode=Standard 时的配置非法：
     *         integratorToken 词表外／maxStepS≤0 或非有限／relTolerance≤0
     *         或非有限／toleranceQ≤0 或非有限／toleranceQd≤0 或非有限；
     *         模型空／工况 id 空／重力非有限／样本维度不匹配／时间非严格
     *         递增——fail-fast，不产出半成品）。mode=Skip 时其余配置字段
     *         不参与校验（短路 NotApplicable）。
     *
     * 确定性：同 request 同输出（逐位——固定步长 RK4＋纯数值递归）。
     * 复杂度：O(区间数 × 每区间子步数 × 4 stage × (n+1) 次单样本 RNEA 调用)，
     * n 为可动关节数；RNEA 每调用含一次模型提取（正确性/同源优先，性能
     * 基准归 WP-23——R-DYN-1）。
     */
    ForwardCheckOutcome validate(const ForwardCheckRequest& request,
                                 evidence::IEvaluationContext& context) const;
};

}  // namespace sdurws::ird::dynamics

#endif  // IRD_DYNAMICS_FORWARDDYNAMICS_HPP
