/**
 * @file   InverseDynamics.hpp
 * @brief  RNEA 逆动力学评估器（units/dynamics.md §5/§10.1）——逐样本递归
 *         牛顿—欧拉，含重力/连杆惯性/末端负载/外部力/黏性＋库仑摩擦全
 *         分项，关节空间精确含全部耦合项。
 *
 * 设计依据：
 *   - units/dynamics.md §5 全章（§5.1 算法定位与模型来源、§5.2 分项图、
 *     §5.3 输入覆盖清单、§5.4 负载事件时间线与模型变体、§5.5 摩擦符号
 *     约定与数据不足降级、§5.6 数值稳定性与失败语义）、§10.1
 *     （IInverseDynamicsEvaluator 契约表）、§4.4/§4.5/§4.6（数据模型/
 *     量纲/序列纪律）
 *   - 需求 DYN-01（RobWorkSim 刚体模型＋递归牛顿—欧拉）、DYN-02（重力/
 *     连杆惯性/末端负载/外部力/黏性＋库仑摩擦——摩擦链 MDL-16 缺失触发
 *     DYN-06 降级）、MDL-22（基座—世界变换单一消费——重力投影）、
 *     DYN-04（关节侧结果与候选传动无关——映射归 drivetrain）、MDL-16
 *     （摩擦参数输入链）
 *   - 决策 D-DYN-2（RNEA 本域自实现，输入取自编译产物，不自建第二套
 *     关节链）、D-DYN-3（重力投影唯一消费 gravityBase，全卡零二次旋转）、
 *     D-DYN-5（dynamics 不内嵌 drivetrain 映射调用）、D-DYN-7（摩擦
 *     sgn₀(0)=0）
 *   - 任务契约 tasks/foundation/WP-17-T03.json（三条 acceptance 的实现面）
 *
 * ★ 契约形态微调（诚实登记，DTB §5.4 精神——单元卡 §1.2/§12 同步登记）：
 *   1. §10.1 的 IInverseDynamicsEvaluator::evaluate(request, context) 中
 *      request 为域内 DTO（本头 InverseDynRequest），不直接吞 evidence
 *      EvaluationRequest（AnalysisSnapshot/InputSlice/objectClosure 的完整
 *      消费链随 WP-17-T10 装配面冻结）；本评估器消费"冻结后的纯数据面"
 *      ——CanonicalModel 只读引用＋基座系重力向量＋样本激励数组。
 *   2. 单工况粒度：一次 evaluate 产出一个工况的样本序列（InverseDynOutcome）；
 *      多工况循环与包络合并归 WP-17-T07 编排（§10.9"多工况编排不设独立
 *      接口"——逐工况循环由上层承担）。
 *   3. 上游轨迹经激励样本数组注入（ITrajectorySourcePort 的**数据面**）；
 *      归档读取适配器落位待裁决（P-DYN-1——L5 装配 vs 上游导出），本头
 *      不依赖任何 trajectory 类型（R-1 零业务域互链）。
 *
 * 背景说明（为什么接口只收"基座系重力向量"而不收 R_world_base）：
 *   重力进入 RNEA 的唯一形态是基座系投影 g_base＝R_world_baseᵀ·g_world
 *   （MDL-22/D-DYN-3；runtime 快照 gravityBase() 单点产出）。接口参数面
 *   只存在 g_base 向量、不存在旋转矩阵——"下游二次旋转"在本域**结构上
 *   不可表达**（AT-37/RT-BW-4 同类缺陷的接口级拦截）；倒挂/壁装工况的
 *   差异全部折叠在调用方传入的 g_base 里（其值来自同一编译变换），评估
 *   器对其来源零假设——测试以地面/倒挂两组投影值验证力矩反号。
 *
 * 线程安全：评估器实例无状态（每任务一实例——卡 §10.0）；evaluate 内部
 *   全部局部值，输入只读；并发安全＝多实例并行或实例外串行。确定性：同
 *   （输入字节＋契约版本）→等价输出（NFR-COR-02；纯数值递归，无环境/
 *   时钟/locale 依赖，无随机源——§10.0 随机种子行）。
 */

#ifndef IRD_DYNAMICS_INVERSEDYNAMICS_HPP
#define IRD_DYNAMICS_INVERSEDYNAMICS_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sdurws/ird/core/DiagData.hpp>      // core::DiagnosticRecord（ERR-01 素材）
#include <sdurws/ird/core/Identity.hpp>      // core::ObjectId（工况/关节/负载对象身份）
#include <sdurws/ird/core/Provenance.hpp>    // core::SourcedValue（四态——负载物性承载）
#include <sdurws/ird/dynamics/DynTypes.hpp>  // DynamicsSample/DynamicsValidity/DynJointType
#include <sdurws/ird/dynamics/Errors.hpp>    // DynamicsError（evaluate 的 fail-fast 异常轨——契约面）
#include <sdurws/ird/evidence/Evaluator.hpp> // evidence::IEvaluationContext（取消/进度宿主上下文）
#include <sdurws/ird/runtime/CanonicalModel.hpp> // runtime::CanonicalModel（RNEA 输入权威——编译产物）

namespace sdurws::ird::dynamics {

// =====================================================================
// 契约版本与算法版本（§4.4 身份块分量——结果可追溯的最小承载；常量唯一
// 书写点，消费方不得另写字面量）。
// =====================================================================

/// 逆动力学评估契约版本（评估输入/输出结构变更＝递增——结果侧身份分量；
/// 单调递增，1＝WP-17-T03 初始面）。
inline constexpr std::uint32_t kInverseDynContractVersion = 1;

/// RNEA 算法实现版本 token（§4.4 DynamicsSeries.algorithmVersion 分量；
/// 算法语义变更〔公式/符号约定/降级规则〕＝换新 token——旧结果不与新
/// 结果混比）。
inline constexpr std::string_view kRneaAlgorithmVersion = "dyn-rnea/1";

// =====================================================================
// 评估输入（§5.3 输入覆盖清单的域内 DTO 形态）。
// =====================================================================

/**
 * @brief 轨迹激励单样本（上游 TimedSample 的动力学消费面——q/q̇/q̈ 三个
 *        逐关节列；§4.3 轨迹消费契约：只消费不重算）。
 *
 * 单位：t 秒；逐关节列的量纲随关节类型（转动 rad·rad/s·rad/s²／移动
 * m·m/s·m/s²）——列序＝链序（模型可动关节序，不含 Fixed）。
 * 值语义纯结构；线程安全。
 */
struct InverseDynSampleInput {
    double t = 0.0;                  ///< 样本时间，单位 s（时间轴唯一基准；必须严格递增）
    std::uint32_t segmentIndex = 0;  ///< 所在轨迹段序号（0 基——上游段结构直通，本域不解释）
    std::vector<double> q;           ///< 权威关节位置（rad 或 m——权威角 q_authoritative，runtime 口径）
    std::vector<double> qd;          ///< 关节速度（rad/s 或 m/s）
    std::vector<double> qdd;         ///< 关节加速度（rad/s² 或 m/s²）
};

/**
 * @brief 末端负载条目（§5.4 负载变体的承载单元——条件 payloads 的单件）。
 *
 * 物性四态语义（D-DYN-6）：mass 必填（Provided 且＞0——建模侧 MDL-06
 * 断言前置，此处防御复检）；com/inertia 可选——缺失时按保守估算评估
 * （com→安装点〔TCP〕；惯量→点质量模型〔零转动惯量〕），并强制标记
 * estimated（estimatedPayloadCount＋DYN-PROPERTY-DOWNGRADED 素材——
 * "低估惯性"提示，绝不包装为精确结论，DYN-06）。
 * 值语义纯结构；线程安全。
 */
struct EndEffectorPayload {
    core::ObjectId objectId;     ///< 负载对象稳定身份（EVI-02/素材定位——空 id 非法）
    core::SourcedValue<double> mass;                 ///< 质量，单位 kg（必填：Provided 且有限＞0）
    core::SourcedValue<rw::math::Vector3D<double>> centerOfMass; ///< 质心，TCP 系下表示，单位 m（可选）
    core::SourcedValue<rw::math::InertiaMatrix<double>> inertia; ///< 惯量张量，质心系下表示，单位 kg·m²（可选）
    bool initiallyMounted = true; ///< 是否初始挂载（§5.4"初始变体＝工具＋初始负载"——
                                  ///<  无事件操控时恒挂载；Grasp/Release 事件切换其挂载态）
};

/**
 * @brief 负载事件（§5.4 事件时间线单条——Grasp/Release 切换负载挂载态）。
 *
 * 时间定位语义（§5.4 保守边界规则）：事件绑定任务点→上游已折算为轨迹
 * 时刻 tEvent（单位 s）；事件落在采样间隙时，变体边界取事件时刻所在区间
 * 的**下一样本起生效**（保守：载荷提前挂载不晚于其后首样本）——实现为
 * "样本时刻 t ≥ tEvent 即生效"（事件恰落在样本上时该样本即生效，满足
 * "不晚于其后首样本"的保守上界）。
 * 值语义纯结构；线程安全。
 */
struct PayloadEvent {
    /// 事件种类（词表二值——Dwell 是轨迹驻留段语义，不是变体事件，§5.4）。
    enum class Kind { Grasp, Release };
    double tEvent = 0.0;      ///< 事件时刻，单位 s（任务点样本时刻；有限）
    Kind kind = Kind::Grasp;  ///< 夹取＝并入末端；释放＝移除
    std::uint32_t payloadIndex = 0; ///< 目标负载在 InverseDynRequest.payloads 的下标
};

/**
 * @brief 逆动力学评估请求（§10.1 request 的域内 DTO；§10.0 身份要求行：
 *        正式接口要求输入含完整身份块——本结构承载 T03 数据面可得的
 *        身份分量：工况对象身份＋模型只读引用〔其 contentIdentity 即
 *        快照模型身份的值源头〕；sliceId/上游轨迹 payload 身份随 T10
 *        装配面入请求——微调已登记）。
 *
 * 所有权：model 只读引用（调用方持有并保证 evaluate 期间存活——本域
 * 不接管、不修改、不重编译〔CM-0〕）；其余字段全部值语义。
 * 线程约束：单次 evaluate 内只读；跨线程并发＝各线程各持请求。
 */
struct InverseDynRequest {
    // —— 身份块（§10.0——缺身份拒绝评估）——
    core::ObjectId conditionId;      ///< 工况对象 ID（EVI-02 覆盖矩阵关联键；空 id＝调用方错误）
    core::ObjectId toolObjectId;     ///< 工具对象 ID（取自模型默认 TCP 项；无工具模型＝空 id）

    // —— 模型与重力（§5.1/§5.3——编译产物只读消费）——
    const runtime::CanonicalModel* model = nullptr; ///< 快照规范模型（唯一输入权威——
                                                    ///<  质量/质心/惯量/轴几何/摩擦/工具偏置全部经它读取；
                                                    ///<  空＝调用方错误 fail-fast）
    double gravityBase[3] = {0.0, 0.0, -9.81};      ///< 基座系重力加速度，单位 m/s²——
                                                    ///<  调用方自快照 gravityBase() 取（R_world_baseᵀ·g_world
                                                    ///<  编译投影单点）；本域零二次旋转（文件头背景说明）。
                                                    ///<  分量必须全部有限。

    // —— 激励（§4.3——上游轨迹的冻结数据面）——
    std::vector<InverseDynSampleInput> samples; ///< 轨迹激励样本（t 严格递增；逐关节列维度
                                                ///<  ＝模型可动关节数——违例 fail-fast）

    // —— 负载变体（§5.4——可选；空＝纯工具链）——
    std::vector<EndEffectorPayload> payloads;   ///< 负载池（条件 payloads；空＝无负载）
    std::vector<PayloadEvent> events;           ///< 夹取/释放事件时间线（空＝恒初始变体）
};

// =====================================================================
// 评估产出（§10.1 返回承载的域内形态）。
// =====================================================================

/**
 * @brief 单工况逆动力学评估产出（OperatingConditionResult 的样本/有效性/
 *        诊断面——峰值/RMS/功率能量统计与序列身份冻结随 T04 落位，本结构
 *        为其直接上游；微调已登记）。
 *
 * 样本排序：按 (t 升序, jointIndex 升序)（§4.6——逐时刻逐关节行序固定，
 * NFR-COR-02）。部分结果纪律：观测到取消或 RNEA 计算失败时序列保留至
 * 故障点（validity.completeness=Partial、cancelled/故障诊断如实标注），
 * 不截断伪造、不补 0（§4.6）。
 * 值语义；线程安全：纯值。
 */
struct InverseDynOutcome {
    core::ObjectId conditionId;                  ///< 工况对象 ID（透传自请求）
    std::vector<DynamicsSample> samples;         ///< 逐样本逐关节行（§4.6 排序纪律见上）
    DynamicsValidity validity;                   ///< 完整性/数值质量/来源摘要（§4.4）
    std::vector<core::DiagnosticRecord> diagnostics; ///< 诊断素材（DYN-* 码——缺降级/
                                                 ///<  估算/非有限/RNEA 失败定位；环境错误轨）
    bool cancelled = false;                      ///< 观测到宿主取消（§9.1.5——取消非错误，
                                                 ///<  UX-03；true 时样本为部分产出）
};

// =====================================================================
// 评估器（§10.1 IInverseDynamicsEvaluator 的具体实现载体——域内具体类；
// evidence IEngineeringEvaluator 适配与注册面随 WP-17-T10 装配冻结）。
// =====================================================================

/**
 * @brief RNEA 逆动力学评估器（§5 递归牛顿—欧拉的本域唯一实现入口）。
 *
 * 职责（一次 evaluate 的执行序——与单元卡 §5 分项图逐步对应）：
 *   1. 前置校验（§10.1 前置行的防御面：模型/工况/维度/时间/物性——调用
 *      方错误即 DynamicsError fail-fast，见 Errors.hpp 触发面清单）；
 *   2. 模型提取：从 CanonicalModel 读取链参数（关节类型/轴/origin/零位
 *      偏置/连杆物性/摩擦三元组/工具偏置）为内部纯数值形态——零 rw 外联
 *      符号（冒烟 header-only 纪律，同 runtime RT-T03 先例）；摩擦四态
 *      检查即 MDL-16 输入链闭环（任一 NotProvided→frictionMissing＋
 *      DYN-FRICTION-MISSING 素材，分项按 0 计入但证据不包装精确——§5.5）；
 *   3. 逐样本：负载变体切换（事件保守边界）→末端体合成（工具＋已挂负载，
 *      平行轴定理）→共享几何递推→RNEA 四通道（重力/惯性/科氏/全量）→
 *      摩擦叠加→功率/能量累积→样本行组装；
 *   4. 逐样本轮询取消（§9.3"长评估必须周期性查询"——每样本一次，成本
 *      一次虚调用）；观测到取消即返回部分产出（cancelled=true，非错误）。
 *
 * 分项拆分口径（§5.2 恒等式的计算来源）：tauTotal 取全量通道；tauGravity/
 * tauInertia/tauCoriolisCentrifugal 分别取 (q̇=0,q̈=0)/(g=0,q̇=0)/(g=0,
 * q̈=0) 三通道——动力学 M(q)q̈＋C(q,q̇)q̇＋G(q) 对 q̈ 线性、对 q̇ 二次、
 * 重力线性，无交叉项，故三通道之和恒等于全量通道（黄金算例 V-03 校验）；
 * 同一样本四通道共享一次几何递推（成本 4×RNEA 力学段、1×几何段）。
 */
class InverseDynamicsEvaluator {
public:
    InverseDynamicsEvaluator() = default;

    /// 可拷贝可移动（无状态——实例仅是调用边界；每任务一实例为使用约定）。
    InverseDynamicsEvaluator(const InverseDynamicsEvaluator&) = default;
    InverseDynamicsEvaluator& operator=(const InverseDynamicsEvaluator&) = default;

    /**
     * @brief 执行单工况 RNEA 评估（§10.1 evaluate——前置/后置/失败语义见
     *        类注释与 Errors.hpp）。
     *
     * @param request [in] 评估请求（调用方持有，调用期间存活；本函数只读）
     * @param context [in] 宿主上下文（取消/进度——evidence §9.3；进度按
     *                样本进度上报，取消按样本轮询）
     * @return 单工况产出（样本行＋有效性＋诊断素材；取消/数值失败时为
     *         部分产出——字段注释）
     *
     * @throws DynamicsError 调用方契约违约（模型空/链非法/工况 id 空/维度
     *         不匹配/时间非严格递增/物性防御复检失败——fail-fast，不产出
     *         半成品；详见 Errors.hpp 触发面清单）
     *
     * 确定性：同 request 同输出（逐位——纯数值递归；无环境依赖）。
     * 复杂度：O(样本数 × 关节数)（每样本 1 次几何＋4 次力学递推）。
     */
    InverseDynOutcome evaluate(const InverseDynRequest& request,
                               evidence::IEvaluationContext& context) const;

    // 复杂递推的实现集中于 InverseDynamics.cpp（私有实现头不外置——本头
    // 即唯一公共契约面；src 内匿名命名空间承载 Vec3/Mat3 纯数值助手）。
};

}  // namespace sdurws::ird::dynamics

#endif  // IRD_DYNAMICS_INVERSEDYNAMICS_HPP
