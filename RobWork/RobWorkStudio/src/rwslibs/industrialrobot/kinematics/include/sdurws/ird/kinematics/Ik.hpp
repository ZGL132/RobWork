/**
 * @file   Ik.hpp
 * @brief  多初值 IK 求解（KIN-02）——IkRequest/IkOutcome 值、IIkSolver
 *         接口与无状态实现 IkSolver、初值策略生成器、碰撞会话最小消费
 *         端口、解析界限检查。
 *
 * 设计依据：
 *   - units/kinematics.md §3.3（公共头布局表 Ik.hpp 行——"IIkSolver＋
 *     IkRequest/IkOutcome（KIN-02）"，任务 T04）、§5.3（IK 求解契约——
 *     请求字段表＋求解流程时序：初值生成→逐初值数值迭代（含阻尼最小
 *     二乘；周期查询取消令牌）→硬过滤→去重→稳定排序）、§5.4（五类结局
 *     判定铁律）、§5.5（失败分类——KIN-TARGET-ILLEGAL 等错误 fail-fast
 *     轨）、§5.6（结果绑定）、§6.1（关节角周期归一化——R1 不做跨周
 *     归一化）、§6.3（稳定排序四键＋referenceQ 显式化 D-KIN-4）、§9.2
 *     （IIkSolver 接口契约原文）、§3.4（数值恒 SI；随机性唯一来源＝
 *     seed 派生确定性序列）、§8.3（取消——每 N 次迭代查询一次）
 *   - REQUIREMENTS KIN-02（多初值 IK、解集去重排序、同位姿异构型）、
 *     NFR-COR-01/02（确定性）、NFR-COR-03（非法输入拒绝）、AT-03（去重
 *     反例）、§8.1 搜索未果口径 C5/C8（EVI-01/EVI-02 表达）
 *   - 治理裁决 O-37（宿主注入形态——碰撞会话句柄同款消费端口先例：
 *     IKinRuntimeView，见 KinTypes.hpp 文件头）；P-KIN-7（policy 会话
 *     契约 Draft——本头以自有最小端口承载，真实④端口会话组装已随 T07
 *     落位（Collision.hpp））
 *   - 任务契约 tasks/foundation/WP-15-T04.json acceptance 1/2/3/4
 *
 * 背景说明（生产者身份常量为何落在本头）：结局 5 的证明素材必须绑定
 * producer 评估键＋契约版本（evidence validateProof 第③查——"已注册且
 * 契约版本相符"），而素材由求解器在 solve() 内组装，故键常量与契约版本
 * 的唯一书写点在本头；Evaluators.hpp 的 descriptor 构造消费同一常量
 * （禁第二处字面量）。§8.4"求解器版本入评估器 descriptor.contractVersion
 * 与 payload"——求解器算法契约版本与评估器契约版本是**同一个值**
 * （kIkSolverContractVersion），升级即新切片身份（CON-04）。
 *
 * 线程安全：IkSolver 无状态可重入（§3.4 纯函数服务）；初值生成与界限
 * 计算为纯函数。确定性：同 request（含 seed/referenceQ）→同输出（同
 * 线程数逐位——本实现单线程计算，§9.2 确定性行）。
 */

#ifndef IRD_KINEMATICS_IK_HPP
#define IRD_KINEMATICS_IK_HPP

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <rw/math/Transform3D.hpp>

#include <sdurws/ird/kinematics/Bounds.hpp>     // AnalyticBoundMaterial（结局 5 素材）
#include <sdurws/ird/kinematics/KinTypes.hpp>   // 值模型（T04 批次）＋TcpRef/IKinRuntimeView
#include <sdurws/ird/runtime/Description.hpp>   // detail::identityTransform3D()——恒等初值
                                                //   （Transform3D 默认构造走外联符号——
                                                //   冒烟模式不可链接，Fk.hpp 同款规避）

namespace sdurws::ird::kinematics {

// =====================================================================
// 域常量（唯一书写点——求解器/评估器 descriptor/证明素材/测试共用）
// =====================================================================

/// 评估键（evidence 词形闸门内 kebab 形态——卡面 §4.3 键名
/// "kin.task-point-ik" 的实现形态，偏差登记随卡 §14.6 v0.4；T03
/// kin-pose-metrics 同款先例。DependencyKey 才允许点）。
inline constexpr char kTaskPointIkEvaluationKey[] = "kin-task-point-ik";

/// 求解器算法契约版本＝评估器契约版本（§8.4 单一值；进 descriptor、
/// 解的 solverContractVersion 与证明素材 producerContractVersion；
/// 升级即新切片身份——CON-04，一经交付不回退）。
inline constexpr std::uint32_t kIkSolverContractVersion = 1U;

/// 阻尼最小二乘的阻尼系数 λ（无量纲设计默认——Δq＝Jᵀ(JJᵀ＋λ²I₆)⁻¹e；
/// λ²＝1e-6 在米级工作半径下近伪逆、近奇异处抑振荡；随黄金数据集锁定
/// ——T13，修改走设计变更；登记随卡 §14.6 v0.4）。
inline constexpr double kIkDampingLambda = 1e-3;

/// 取消探针查询周期（每 N 次迭代查询一次——§8.3"每批/每 N 次迭代查询
/// 一次"的求解器内落值；响应粒度黄金锁定随 T13）。
inline constexpr std::uint32_t kIkCancellationProbeInterval = 16U;

// =====================================================================
// InitialValueStrategy——初值策略（§5.3 请求字段表；词表＝§4.4 枚举）
// =====================================================================

/**
 * @brief 初值集生成策略（§5.3"初值集（按策略生成）"三值；与
 *        AnalysisConfiguration.initialStrategy 词表同源——T10）。
 */
enum class InitialValueStrategy : std::uint8_t {
    /// 单初值＝显式参考构型（referenceQ——排序与初值同源显式输入）。
    ReferenceQ,
    /// seed 派生确定性伪随机序列（逐关节在评价区间内均匀采样——同 seed
    /// 同序列，§3.4 随机性唯一来源）。
    SeededRandom,
    /// 限位空间均分（沿评价区间对角线取 count 个等分点——count=1 取
    /// 区间中点；不做自由度组合展开，登记随卡 §14.6 v0.4、黄金锁定 T13）。
    JointGrid,
};

/**
 * @brief 单自由度的关节评价区间（§6.1 三态：有界关节取 bounds、
 *        continuous 取工作范围（MDL-12）、continuous 无工作范围＝无
 *        分析限位（bounded=false——限位过滤跳过该轴））。
 *
 * 单位：转动 rad／移动 m。值语义纯结构；线程安全。
 */
struct JointInterval {
    /// 区间下界（bounded=true 时有效）。
    double lower = 0.0;
    /// 区间上界（bounded=true 时有效）。
    double upper = 0.0;
    /// 是否有分析限位（false＝continuous 无工作范围——采样以 [−π,π]
    /// 为名义区间、限位评价跳过；§6.1 归一化语义变更走需求变更）。
    bool bounded = false;
};

/**
 * @brief 从模型视图提取逐自由度评价区间（§6.1 三态的唯一实现点——
 *        初值生成与限位过滤同源，防两套真值漂移）。
 *
 * @param view [in] 只读模型视图（调用方持有；调用期间存活）
 * @return 逐可动关节区间（链序——与 q 向量下标一一对应）
 *
 * 纯函数；线程安全；确定性（同模型同区间）。
 */
std::vector<JointInterval> evaluationIntervals(const IKinRuntimeView& view);

/**
 * @brief 生成初值集（§5.3"初值集（按策略生成）"的唯一实现点；确定性）。
 *
 * 逐策略语义（登记随卡 §14.6 v0.4；黄金锁定 T13）：
 *   - ReferenceQ：返回 {referenceQ}（1 个初值——count 被忽略）；
 *   - SeededRandom：count 个初值；第 k 个初值逐轴取
 *     lo_i＋u·(hi_i−lo_i)，u∈[0,1) 由 splitmix64(seed·2³²＋k) 归一化
 *     派生（确定性序列——同 seed 同矩阵；无工作范围 continuous 轴的
 *     采样名义区间取 [−π,π]，rad）；
 *   - JointGrid：count 个初值沿区间对角线均分：t_k＝k/(count−1)
 *     （count=1 → t=0.5），q_i＝lo_i＋t_k·(hi_i−lo_i)；bounded=false
 *     轴取名义区间 [−π,π] 的同比例点。
 *
 * @param strategy   [in] 初值策略（三值之一）
 * @param count      [in] 初值数量（≥1；ReferenceQ 忽略）
 * @param seed       [in] 确定性种子（SeededRandom 的序列源；0 非法——
 *                   I-KIN-4 拒绝不做 0→1 静默替换，此处按契约前置由
 *                   调用方保证，违例输出无定义）
 * @param intervals  [in] 逐自由度评价区间（evaluationIntervals 产出；
 *                   长度须与 referenceQ 自由度一致——调用方契约）
 * @param referenceQ [in] 参考构型（rad／m；ReferenceQ 的初值源）
 * @return 初值集（初值序确定性——稳定排序 sourceInitIndex 的序源）
 *
 * 纯函数；线程安全；确定性（同输入同矩阵——NFR-COR-01）。
 */
std::vector<std::vector<double>> makeInitialValues(InitialValueStrategy strategy,
                                                   std::uint32_t count,
                                                   std::uint64_t seed,
                                                   const std::vector<JointInterval>& intervals,
                                                   const std::vector<double>& referenceQ);

// =====================================================================
// IKinCollisionSession——碰撞会话最小消费端口（④端口句柄的域侧投影）
// =====================================================================

/**
 * @brief 构型碰撞评价状态三值词表（WP-15-T07 表尾追加——§8.1 取消/失败行
 *        的值承载；登记随卡 §14.6 v0.7）。
 *
 * 语义（KIN-05 铁律：三种状态中只有 Evaluated 才允许解读"无碰撞"）：
 *   - Evaluated：④端口会话完成构型级判定（inCollision/objectIdPairs 有效）；
 *   - EvidenceMissing：证据缺失——策略侧应答不可判（作用域为空/策略禁用
 *     被误调用/会话返回取消非终态），绝不解读为无碰撞；
 *   - FacilityFailed：碰撞设施异常（检测后端异常/上下文失效），该解标记
 *     DataInsufficient 素材＋诊断、不中断其余候选（§8.1 取消/失败行）。
 */
enum class IkCollisionEvaluationState : std::uint8_t {
    /// ④端口会话完成构型级判定（判定值有效）。
    Evaluated,
    /// 证据缺失（策略侧应答不可判——KIN-05：绝不视为无碰撞）。
    EvidenceMissing,
    /// 碰撞设施异常（该样本/解 DataInsufficient 素材——不中断整批）。
    FacilityFailed,
};

/**
 * @brief 单构型碰撞查询结果（§6.1 collisionStatus 的判定来源）。
 *
 * objectIdPairs 成对展平（[a1,b1,a2,b2,…]——碰撞对象对；仅
 * inCollision=true 时非空）。值语义纯结构；线程安全。
 *
 * ★ T07 追加纪律：state/statusDetail 两成员为 WP-15-T07 表尾追加（真实
 * ④端口会话的失败/缺失语义承载——T04 端口的"仅判定值"形态不足以表达
 * §8.1"设施异常→该解 DataInsufficient"轨）；聚合初始化只写前两成员的
 * 既有调用点保持合法（追加成员带默认值——T03/T04 既有测试替身零改动）。
 */
struct IkCollisionVerdict {
    /// 该构型是否碰撞（构型级判定——仅过滤该解，不下任务结论，C8）。
    bool inCollision = false;
    /// 碰撞对象对（成对展平——ObjectId，来自 policy 会话判定明细）。
    std::vector<core::ObjectId> objectIdPairs;
    /// 评价状态（T07 追加——仅 Evaluated 态允许"无碰撞"解读，KIN-05）。
    IkCollisionEvaluationState state = IkCollisionEvaluationState::Evaluated;
    /// 非评价态的原因素材（EvidenceMissing/FacilityFailed 时非空——进
    /// 诊断 cause；Evaluated 态为空串）。内部诊断链文本——不得未经
    /// diagnostics 脱敏直接呈现。
    std::string statusDetail;
};

/**
 * @brief 碰撞会话最小消费端口（§5.3 请求字段"碰撞会话句柄（policy 启用
 *        时）"的域侧类型落点）。
 *
 * 语义边界（IKinRuntimeView 同款纪律——不新增对端语义）：本接口只投影
 * 本单元实际消费的成员（构型级二值判定＋对象对明细）；真实④端口会话
 * （policy::CollisionEvaluationSession，§9.3 冻结签名）由适配器实现本
 * 接口注入——适配归 L5/碰撞接入任务 T07（P-KIN-7 处置：真实会话契约
 * Draft 未冻结，T04 以本最小端口＋测试替身先行，§10.1"可控求解器与
 * 碰撞测试替身"口径）。R-POL-2 不受影响：碰撞**实现**唯一归 policy，
 * 本单元零 proximity 消费、零本地判定副本（V-25 静态扫描面）。
 *
 * 生命周期与线程：会话由请求装配方（评估器宿主）持有并保证 solve()
 * 期间存活（非 owning 指针进 IkRequest）；实现方保证并发只读安全
 * （§9.4 CollisionSession 行——每评估构建、工作线程使用）。
 */
class IKinCollisionSession {
public:
    virtual ~IKinCollisionSession() = default;

    /**
     * @brief 评价单个构型的碰撞状态（构型级；§8.1④端口语义）。
     *
     * @param q [in] 权威关节向量（rad／m；链序——与 IK 请求同序）
     * @return 判定值（inCollision＋对象对明细）
     *
     * 纯查询（不修改会话状态）；实现方保证确定性（同会话同 q 同判定
     * ——§8.4④端口"三入口一致"的本单元侧前提）。
     */
    virtual IkCollisionVerdict evaluate(const std::vector<double>& q) const = 0;
};

// =====================================================================
// IkRequest——求解请求（§5.3 请求字段表逐项；§9.2 solve() 唯一入参）
// =====================================================================

/**
 * @brief 多初值 IK 求解请求（§5.3 字段表；值语义，调用方组装）。
 *
 * 所有权与生命周期：modelView/collisionSession 为**非 owning** 裸指针
 * ——调用方持有并保证 solve() 调用期间存活（§9.4 视图/会话行）；其余
 * 字段全部值持有。cancellationProbe 为取消探针回调（可空＝不可取消；
 * 实现在工作线程内被调用——须线程安全或干脆不可变）。
 *
 * 输入义务（§5.1）：modelView 与 intervals 必须取自**同一快照**（限位
 * 评价与 FK 复算同源，防两套真值）；referenceQ 显式携带（D-KIN-4——
 * 禁止隐式读会话姿态）。
 *
 * 全部物理量单位：目标位姿平移 m／旋转 rad（基座系 {B}——§5.1 解析后
 * 的 TCP 目标）；两容差 m／rad；去重阈值 rad|m 逐轴；referenceQ/
 * initialValues/initialValue 内向量逐自由度 rad|m。
 */
struct IkRequest {
    /// 目标位姿（基座系 {B} 的 TCP 目标——T_base_tcp 目标值；§5.3 字段 1）。
    rw::math::Transform3D<double> targetInBase = runtime::detail::identityTransform3D();
    /// 位置残差容差（m；默认 1e-6——附录 D 第 1 项）。
    double positionTolerance = 1e-6;
    /// 姿态残差容差（rad；默认 1e-6——附录 D 第 2 项）。
    double orientationTolerance = 1e-6;
    /// 模型视图（非 owning——FK 复算与限位评价的唯一真值来源；空＝
    /// 调用方契约违约）。
    const IKinRuntimeView* modelView = nullptr;
    /// TCP 引用（快照内解析——KinTypes.hpp 解析规则；§5.3 字段表"目标
    /// 位姿（经 §5.1 解析的 base 系 TCP 目标）"的解析面）。
    TcpRef tcp;
    /// 初值集（已按策略物化——makeInitialValues 产出或调用方自备；
    /// 非空且逐项维度/有限性受验）。
    std::vector<std::vector<double>> initialValues;
    /// 单初值迭代上限（计数；≥1——§5.3 字段表"迭代上限"）。
    std::uint32_t iterationLimit = 0;
    /// 逐自由度关节评价区间（来自快照，含 continuous 工作范围——§5.3
    /// 字段表；evaluationIntervals(view) 产出，与 modelView 同快照）。
    std::vector<JointInterval> intervals;
    /// 去重阈值（rad|m 逐轴；默认 1e-6——附录 D 第 3 项/C1）。
    double dedupThresholdPerAxis = 1e-6;
    /// 碰撞会话句柄（非 owning；空＝策略未启用碰撞——硬过滤③跳过并
    /// 标记 collisionNotEvaluated，绝不解读为无碰撞）。
    const IKinCollisionSession* collisionSession = nullptr;
    /// 取消探针（可空＝不可取消；返回 true 即取消——§9.2"@取消 返回
    /// cancelled=true 载荷（无终局字段）——取消不是结局"）。
    std::function<bool()> cancellationProbe;
    /// 排序参考构型（rad／m；§6.3 第 3 键的距离中心——显式评估输入）。
    std::vector<double> referenceQ;
    /// 对象绑定（pointOid/conditionId——结果溯源；§5.6）。
    IkTargetRef targetRef;
    /// 结果身份（snapshotId/sliceId/configDigest/mode/seed/referenceQ
    /// ——§5.6 绑定五元组载体；评估器由 EvaluationRequest 填充）。
    IkRequestIdentity requestIdentity;
};

// =====================================================================
// IkOutcome——求解结果（§5.3 流程产出＋§5.4 结局）
// =====================================================================

/**
 * @brief 多初值 IK 求解结果（§9.2 solve() 唯一返回值）。
 *
 * 解读规则（§5.4 铁律＋§9.2 契约）：
 *   - cancelled=true：取消不是结局——outcomeKind/solutionSet 无终局
 *     语义（调用方不得解读；proofMaterial 必空）；
 *   - outcomeKind=SolutionsFound/PartialCollision：solutionSet.solutions
 *     非空且已按 §6.3 稳定排序；
 *   - outcomeKind=MultiInitNoConvergence/AllCandidatesFiltered：
 *     solutions 为空、searchRecord 必填（＋filteredRecords 逐解过滤
 *     记录）→DataInsufficient 素材；**不得输出不可行结论**；
 *   - outcomeKind=AnalyticBoundExceeded：solutions 为空、proofMaterial
 *     必填（仅素材——裁定归 evidence validateProof）；
 *   - collisionNotEvaluated=true：策略未启用碰撞，硬过滤③跳过——解的
 *     collisionStatus.evaluated=false（证据缺失，不解读为无碰撞）；
 *   - collisionEvidenceMissing=true（T07 追加）：会话在场但存在构型级
 *     评价未完成（设施异常/策略侧应答不可判）——同上证据缺失语义；
 *     诊断面（KIN-COLLISION-UNAVAILABLE）由评估器/组装器据两标记产出。
 */
struct IkOutcome {
    /// 取消标记（true＝取消返回——无终局字段，§9.2）。
    bool cancelled = false;
    /// 五类结局（§5.4；cancelled=true 时无语义）。
    IkOutcomeKind outcomeKind = IkOutcomeKind::SolutionsFound;
    /// 解集（排序解＋过滤记录＋搜索未果记录＋统计＋双重绑定）。
    IkSolutionSet solutionSet;
    /// 碰撞未评价标记（硬过滤③跳过——证据缺失语义，KIN-05 口径）。
    bool collisionNotEvaluated = false;
    /// 碰撞评价缺失标记（WP-15-T07 表尾追加——会话在场但构型评价未完成：
    /// 设施异常/策略侧应答不可判；该解保留、collisionStatus.evaluated=
    /// false，绝不解读为无碰撞，KIN-05；不中断其余候选——§8.1 取消/失败
    /// 行。与 collisionNotEvaluated 的分工：后者＝会话不在场（策略未启用/
    /// 接线不可用），前者＝会话在场而评价未完成——两者都是证据缺失素材，
    /// 诊断码同为 KIN-COLLISION-UNAVAILABLE（口径登记随卡 §14.6 v0.7）。
    bool collisionEvidenceMissing = false;
    /// 解析界限证明素材（仅结局 5 非空——本单元只产素材不裁定）。
    std::optional<AnalyticBoundMaterial> proofMaterial;
    /// 求解器算法契约版本（kIkSolverContractVersion——§8.4）。
    std::uint32_t solverContractVersion = kIkSolverContractVersion;
};

// =====================================================================
// IIkSolver——多初值 IK 求解接口（§9.2 原文契约）
// =====================================================================

/**
 * @brief 多初值 IK 求解接口（KIN-02；§9.2 七个必需接口之二）。
 *
 * 契约（§9.2 行内联原文的展开）：
 *   - @pre request.target 已解析到 base 系；collisionSession 由④端口
 *     创建（策略启用时）；modelView/intervals 同快照（§5.1）。
 *   - @post SolutionsFound（含 PartialCollision）时 solutions 非空且已
 *     按 §6.3 稳定排序；结局 2/3 必附搜索未果记录；结局 5 仅产证明
 *     素材（不裁定）。
 *   - @取消 返回 IkOutcome{cancelled=true}（无终局字段）——取消不是
 *     结局。
 *   - @确定性 同 request（含 seed/referenceQ）→同输出（同线程数逐位、
 *     异线程数等价——本实现单线程，逐位）。
 *   - 合法＝④端口会话句柄（或显式未启用）；非法＝自建碰撞实现（编译
 *     不可达——端口接口唯一）。
 *
 * 调用方错误 fail-fast 轨（§9.1"调用方错误 fail-fast"；§5.5 错误组——
 * KIN-TARGET-ILLEGAL／KIN-NO-TCP／FrameUnresolved 语义锚，违例抛
 * std::invalid_argument，不进入结果对象）：
 *   - modelView 空指针／链空（视图契约违约）；
 *   - 目标位姿含非有限分量／容差非有限或 ≤0（KIN-TARGET-ILLEGAL——
 *     "目标非法：非有限/容差非法"）；
 *   - TCP 未配置/悬空/键不命中（KinTypes 规则——KIN-NO-TCP 两分/
 *     FrameUnresolved）；
 *   - referenceQ／初值集维度或有限性违例、initialValues 空集、
 *     iterationLimit=0、去重阈值非有限或 ≤0、intervals 维度/有序性
 *     违例（请求组装错误——同轨拒绝，不钳制不置零，NFR-COR-03）。
 */
class IIkSolver {
public:
    virtual ~IIkSolver() = default;

    /**
     * @brief 五类结局求解（§5.4 铁律；过滤/去重/排序内部完成）。
     *
     * @param request [in] 求解请求（值语义读入；调用方持有——本函数不
     *                     修改、不存储引用）
     * @return 求解结果（解读规则见 IkOutcome 注；不抛——除非调用方
     *         契约违约，见类注 fail-fast 轨）
     *
     * 纯函数；线程安全（无共享可变状态）；长运算——周期查询取消探针
     * （kIkCancellationProbeInterval 迭代一次＋每个初值起止各一次）。
     */
    virtual IkOutcome solve(const IkRequest& request) const = 0;
};

// =====================================================================
// IkSolver——无状态实现（§3.1 计算库"IK"落点）
// =====================================================================

/**
 * @brief IIkSolver 的无状态实现（纯函数服务——§3.4 总约定）。
 *
 * 生命周期：无成员状态，可静态/栈构造共享使用（§9.4"无状态建议共享"）。
 * 实现要点（详见 src/Ik.cpp 逐步注释；流程＝§5.3 时序图）：
 *   0. 请求校验（fail-fast 轨——类注 IIkSolver 错误面）；
 *   1. 解析界限检查（结局 5——静态、覆盖全部可能解；仅产素材）；
 *   2. 逐初值阻尼最小二乘迭代（FkEvaluator 单一计算点复算 FK；
 *      λ=kIkDampingLambda；周期查询取消探针）；
 *   3. 硬过滤（顺序固定①残差复验②限位③碰撞——未启用跳过＋标记）；
 *   4. 去重（关节空间逐轴容差成对比较——构型而非位姿；保留组内
 *      sourceInitIndex 最小者）；
 *   5. 稳定排序（§6.3 四键）＋统计与结局判定（§5.4）。
 */
class IkSolver final : public IIkSolver {
public:
    /// 无状态——默认构造即可用（求解参数全部随请求携带）。
    IkSolver() = default;

    IkOutcome solve(const IkRequest& request) const override;
};

}  // namespace sdurws::ird::kinematics

#endif  // IRD_KINEMATICS_IK_HPP
