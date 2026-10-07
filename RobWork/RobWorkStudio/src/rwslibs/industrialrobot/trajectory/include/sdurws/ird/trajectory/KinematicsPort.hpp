/**
 * @file   KinematicsPort.hpp
 * @brief  kinematics IK/FK 注入端口（IKinematicsComputePort）——trajectory
 *         拥有的最小计算端口接口（§15.4；WP-16-T05 批落位）。
 *
 * 设计依据：
 *   - units/trajectory.md §15.4（接口基线——形态"trajectory 拥有的最小
 *     接口（KinematicsPort.hpp）；实现由装配面适配 kinematics IIkSolver/
 *     IFkEvaluator（P-KIN-2 宿主注入先例同构）；R-1 合规：本域零
 *     kinematics 编译依赖"；签名"solveIk(const IkPortRequest&)（目标位
 *     姿、初值策略/数量、容差、去重阈值、限位区间、碰撞会话引用、种子、
 *     取消）；evaluateFk(q)"；语义"完整沿用 kin.task-point-ik/kin.pose-
 *     metrics 契约（五类结局、硬过滤记录、稳定排序、统一尺度）——本端
 *     口为透传适配，不重解释"；错误"端口层错误 token trajectory/
 *     kin-port-*（适配失败/契约不匹配）"；合法示例"笛卡尔段逐采样调用
 *     solveIk（同种子同配置→等价解序）"）
 *   - §3.3（端口协作总图——"注入端口 IKinematicsComputePort ──(适配)──
 *     kinematics"）、§8.3（笛卡尔段逐采样 IK 端口消费——"trajectory 不
 *     实现 IK"）、§8.5（奇异邻域判定输入＝FK 端口的条件数/奇异值）
 *   - 需求 TRJ-02（沿途 IK 连续性检查的计算服务来源）、KIN-01~05（③端口
 *     IK 消费纪律）、R-1（业务域单元互链禁止——ARC §3.5/卡 §3.2）
 *   - 先例：kinematics IKinRuntimeView 宿主注入放行形态（P-KIN-2——卡
 *     §4.2"注入端口（§15.4）：IKinematicsComputePort/IPolicySessionPort
 *     的适配器实现属装配面（L5），本域只定义最小接口"）
 *   - 任务契约 tasks/foundation/WP-16-T05.json（acceptance 2——"IK 经③
 *     端口消费（IKinematicsComputePort 透传 kin.task-point-ik/kin.pose-
 *     metrics），不直链 kinematics（R-1）"）
 *
 * 背景说明（为什么是"镜像词表＋透传"而不是直接消费 kinematics 类型）：
 *   R-1 红线禁止 trajectory include kinematics 头（卡 §3.2 消费表"R-1：
 *   不直链 kinematics 库"）。而 TRJ-02 需要逐采样 IK 服务（非一次性评估
 *   结果），故按 D-TRJ-2 以"注入端口＋③评估器端口语义"承接：本头定义
 *   trajectory 自有的最小请求/结果值与纯虚接口，装配面（L5）提供适配器
 *   ——把 kin.task-point-ik 解集（四键稳定排序后的视图序）与 kin.pose-
 *   metrics 指标**逐字段投影**为本头的值类型。词表枚举为 kinematics 侧
 *   枚举的镜像（数值一致、语义权威归 kinematics 卡），适配器负责双侧
 *   枚举的机械映射——本域不重解释任何结局面语义（"透传适配，不重解释"
 *   ——§15.4 原文）。
 *
 * 落地面与演进边界（如实登记，防"占位"误读）：
 *   - 适配器实现落位＝P-TRJ-3 待裁决（§21.2——归 L5 装配还是 kinematics
 *     提供导出适配面）；本头只定义端口契约，不提供 kinematics 适配器
 *     （装配面职责）。
 *   - 碰撞会话引用：§15.4 请求字段面提及"碰撞会话引用"——该字段随
 *     WP-16-T06/T07 消费 policy 会话时表尾增列；本批请求无碰撞会话，
 *     解集的碰撞评价标记恒 evaluated=false（语义见 IkPortCollisionStatus
 *     ——**不得解读为无碰撞**，KIN-05 铁律）。
 *   - 本头无 .cpp：全部为纯虚接口与值类型（无域内实现逻辑）。
 *
 * 线程安全：值类型并发只读；端口实例的线程语义由装配面声明（评估器
 * threadSafety=SingleThread——每 worker 每任务一实例，§15.0）。
 */

#ifndef IRD_TRAJECTORY_KINEMATICSPORT_HPP
#define IRD_TRAJECTORY_KINEMATICSPORT_HPP

#include <cstdint>
#include <string>
#include <vector>

#include <rw/math/Transform3D.hpp>

#include <sdurws/ird/trajectory/TrjTypes.hpp>  // CancelSignal（§15.0 取消观测）

namespace sdurws::ird::trajectory {

// =====================================================================
// 端口层常量（错误 token 前缀——唯一书写点）
// =====================================================================

/// 端口层错误 token 前缀（§15.4 错误行原文"端口层错误 token
/// trajectory/kin-port-*（适配失败/契约不匹配）"——适配器与消费面共用
/// 同一前缀常量，禁字符串拼前缀）。
inline constexpr char kKinPortErrorPrefix[] = "trajectory/kin-port-";

// =====================================================================
// kin.task-point-ik 契约词表镜像（数值与语义权威＝kinematics 卡；适配器
// 机械映射，本域不重解释——文件头注）
// =====================================================================

/**
 * @brief IK 求解五类结局的端口镜像（kinematics IkOutcomeKind §5.4 判定
 *        表；枚举数值＝表行号 1~5，与 kinematics 侧一致——适配器可静态
 *        断言锁定）。
 *
 * 语义摘引（权威原文归 kinematics 卡 §5.4/REQUIREMENTS §8.1 表 2——C5/C8）：
 *   - 1 SolutionsFound：去重后存在 ≥1 个通过全部硬过滤的解（可行性素材）；
 *   - 2 MultiInitNoConvergence：全部初值迭代至上限未收敛（零候选）→
 *     DataInsufficient 素材，不得输出不可行；
 *   - 3 AllCandidatesFiltered：有收敛候选但全部被限位/残差/碰撞过滤 →
 *     搜索未果口径 → DataInsufficient 素材，不得输出不可行（含全部因
 *     碰撞被过滤——构型级碰撞仅过滤该解，C8）；
 *   - 4 PartialCollision：部分解碰撞、其余有效（1 的子形态）；
 *   - 5 AnalyticBoundExceeded：目标位姿超出解析工作半径上界（唯一允许
 *     产出证明素材的路径）。
 *
 * 取消不是结局（kinematics §9.2 同款）：取消经 KinPortCallStatus::
 * Canceled 表达，不占用本枚举值。
 */
enum class IkPortOutcome : std::uint8_t {
    /// 行 1：存在 ≥1 个通过全部硬过滤的解。
    SolutionsFound = 1,
    /// 行 2：全部初值未收敛（零候选——搜索未果口径）。
    MultiInitNoConvergence = 2,
    /// 行 3：收敛候选全部被硬过滤（搜索未果口径——含全部因碰撞被过滤）。
    AllCandidatesFiltered = 3,
    /// 行 4：部分解碰撞、其余有效（1 的子形态）。
    PartialCollision = 4,
    /// 行 5：目标超出解析工作半径上界（解析界限素材路径）。
    AnalyticBoundExceeded = 5,
};

/**
 * @brief 单个收敛候选被硬过滤原因的端口镜像（kinematics
 *        SolutionFilterReason §5.3——硬过滤顺序固定①残差复验→②限位→
 *        ③碰撞，记录取首个命中的阶段）。
 */
enum class IkPortFilterReason : std::uint8_t {
    /// 阶段①：FK 复算残差超两容差之一（位置 m／姿态 rad）。
    ResidualRecheck,
    /// 阶段②：解超关节限位（有界关节出 bounds／continuous 出工作范围）。
    JointLimit,
    /// 阶段③：构型级碰撞（policy 会话判定；仅过滤该解，不下任务结论）。
    Collision,
};

/**
 * @brief 解集单解碰撞评价状态的端口镜像（kinematics CollisionStatus
 *        §6.1/KIN-05 语义）。
 *
 * ★ 铁律（KIN-05 原文）：evaluated=false＝策略未启用碰撞或缺检测器——
 * **证据缺失，绝不解读为"无碰撞"**（本批端口请求未携带碰撞会话——随
 * WP-16-T06/T07 增列前，适配器产出的碰撞状态恒 evaluated=false，消费方
 * 不得将其当作可行凭据）。值语义纯结构；线程安全。
 */
struct IkPortCollisionStatus {
    /// 是否完成了碰撞评价（policy 会话在场且调用成功）。
    bool evaluated = false;
    /// 碰撞判定（仅 evaluated=true 时有意义）。
    bool inCollision = false;
};

/**
 * @brief 端口单解（kin.task-point-ik 解集元素的投影——稳定排序视图序）。
 *
 * 与 PtpCandidate（TrjTypes.hpp）的字段同源同语义（q／stableIndex／
 * minimumJointMargin 三键——§7.5 选择规则的输入），适配器可直接逐字段
 * 映射。残差/雅可比等其余解字段不在投影内（笛卡尔段连续性检查只消费
 * 三键＋碰撞标记；投影最小化——O-37 纪律）。
 *
 * 单位：q 为权威关节向量（逐自由度 SI：转动 rad／移动 m；链序）；
 * stableIndex 无量纲计数；minimumJointMargin 无量纲归一化比（+∞＝全
 * 无界关节链——参与全序取最大）。值语义纯结构；线程安全。
 */
struct IkPortSolution {
    /// 权威关节向量（rad|m；链序）。
    std::vector<double> q;
    /// 解集稳定排序下标（kinematics 四键排序视图序——无量纲计数）。
    std::uint32_t stableIndex = 0;
    /// 有界关节最小裕量（无量纲归一化比，D-KIN-6；+∞＝全无界）。
    double minimumJointMargin = 0.0;
    /// 该解的碰撞评价状态（IkPortCollisionStatus——evaluated=false 铁律）。
    IkPortCollisionStatus collision;
};

/**
 * @brief 被硬过滤候选的逐解记录（kinematics 逐解过滤记录的投影——结局
 *        3/4 附带；§8.4"过滤原因分布"素材的数据来源）。
 *
 * 值语义纯结构；线程安全。q 为被过滤候选的权威关节向量（rad|m）。
 */
struct IkPortFilterRecord {
    /// 被过滤候选的构型（rad|m；链序）。
    std::vector<double> q;
    /// 首个命中的过滤阶段（①残差→②限位→③碰撞——顺序即语义）。
    IkPortFilterReason reason = IkPortFilterReason::ResidualRecheck;
};

// =====================================================================
// 端口请求/回复值（透传 kin.task-point-ik / kin.pose-metrics 请求面）
// =====================================================================

/**
 * @brief IK 求解请求（§15.4 提议签名的字段面——"目标位姿、初值策略/
 *        数量、容差、去重阈值、限位区间、种子、取消"；碰撞会话引用随
 *        WP-16-T06/T07 增列，见文件头注）。
 *
 * 全部字段为 kin.task-point-ik 请求语义的透传投影（适配器逐字段转发，
 * 本域不重解释）；字段的合法域判定归 kinematics 契约——端口消费面
 * （planCartesianLine）按 §15.2 前置校验后才构造本请求。
 *
 * 单位/坐标系：targetPose 为**基座系 {B}** 的 TCP 目标位姿（T_base_tcp，
 * core §4.6 读法；平移 m）；容差（位置 m／姿态 rad）；去重阈值与限位
 * 区间逐自由度（转动 rad／移动 m）；seed 无量纲（>0——0 非法，I-KIN-4
 * 同款，由调用方保证）。值语义纯结构；线程安全。
 */
struct IkPortRequest {
    /// TCP 目标位姿——基座系 {B}（m/rad；kin.task-point-ik 同语义）。
    rw::math::Transform3D<double> targetPose;
    /// 初值策略词表镜像（kinematics InitialValueStrategy 数值：0=
    /// ReferenceQ／1=SeededRandom／2=JointGrid；缺省 JointGrid＝无参考
    /// 构型场景的确定性策略——笛卡尔段逐采样无显式参考构型注入）。
    std::uint8_t initStrategy = 2;
    /// 初值数量上限（≥1；ReferenceQ 策略忽略——单初值）。
    std::uint32_t maxInitialValues = 8;
    /// 位置收敛容差（m；硬过滤①的判定基准之一——kinematics 侧评价）。
    double positionToleranceM = 1e-6;
    /// 姿态收敛容差（rad；硬过滤①的另一基准）。
    double orientationToleranceRad = 1e-6;
    /// 关节空间去重阈值（逐轴上界；rad|m——附录 D 第 3 项同源，KIN-02）。
    double dedupThreshold = 1e-6;
    /// 评价区间下界（逐自由度；rad|m——限位硬过滤②的区间投影；消费面
    /// 恒提供非空视图——§5.3 CanonicalJoint 投影义务）。
    std::vector<double> lowerBoundQ;
    /// 评价区间上界（逐自由度；rad|m——与 lowerBoundQ 同维度）。
    std::vector<double> upperBoundQ;
    /// 规划种子（无量纲；>0——透传 kinematics 随机性唯一来源，§3.4）。
    std::uint64_t seed = 1;
    /// 取消观测（可空＝不可取消；kinematics §9.2 取消令牌语义透传）。
    CancelSignal cancel;
};

/**
 * @brief solveIk 的业务结果（五类结局＋解集/过滤记录——透传不重解释）。
 *
 * 字段与结局的联动（kinematics §5.4 判定表语义；适配器保证，消费面按
 * 结局分支消费）：
 *   - SolutionsFound/PartialCollision：solutions 非空（稳定排序视图序）
 *     ——PartialCollision 的被过滤解另入 filtered；
 *   - MultiInitNoConvergence/AllCandidatesFiltered：solutions 为空、
 *     attemptedInitialValues＞0——AllCandidatesFiltered 时 filtered
 *     逐解记录完整（§8.4"过滤原因分布"素材来源）；
 *   - AnalyticBoundExceeded：solutions/filtered 均空（解析界限素材路径
 *     ——证明素材经评估器面装配，本端口只回结局）。
 *
 * 值语义纯结构；线程安全（并发只读）。
 */
struct IkPortResult {
    /// 五类结局（IkPortOutcome——数值＝kinematics 判定表行号）。
    IkPortOutcome outcome = IkPortOutcome::MultiInitNoConvergence;
    /// 通过全部硬过滤的解集（稳定排序视图序——§15.4"稳定排序"透传）。
    std::vector<IkPortSolution> solutions;
    /// 被硬过滤候选的逐解记录（结局 3/4 附；其余结局恒空）。
    std::vector<IkPortFilterRecord> filtered;
    /// 本次求解实际使用的初值数（≥0；§8.4 素材义务"已试初值数"来源）。
    std::uint32_t attemptedInitialValues = 0;
};

/**
 * @brief FK 位姿指标投影（kin.pose-metrics 语义子集——§8.5 奇异邻域
 *        判定面；§8.3"每采样点经 evaluateFk 取奇异值/条件数"）。
 *
 * 字段与 kinematics PoseMetrics 同源同语义（tcpInBase/singularValues/
 * conditionNumber/conditionNumberIsFinite——D-KIN-2）：奇异值**降序**；
 * conditionNumber＝σmax/σmin（无量纲），奇异（σmin=0 或除法非有限）时
 * ＝+∞ 并置 conditionNumberIsFinite=false——不静默截断。
 *
 * 单位：tcpInBase 为基座系 {B} 的 TCP 位姿（T_base_tcp；平移 m）；奇异
 * 值量纲随雅可比行列混合（比较用相对容差——附录 D 第 9 项）；条件数
 * 无量纲（单位 1）。值语义纯结构；线程安全。
 */
struct FkPortMetrics {
    /// TCP 位姿——基座系 {B}（m/rad；kin.pose-metrics 同语义）。
    rw::math::Transform3D<double> tcpInBase;
    /// 雅可比奇异值（降序；长度 min(6, n)——n＝设备自由度）。
    std::vector<double> singularValues;
    /// 条件数 σmax/σmin（无量纲，单位 1；奇异＝+∞——不静默截断）。
    double conditionNumber = 0.0;
    /// 条件数有限性标记（false＝conditionNumber 为 +∞/非有限——D-KIN-2）。
    bool conditionNumberIsFinite = true;
};

/**
 * @brief evaluateFk 的请求（§15.4 提议签名 evaluateFk(q) 的字段面——
 *        位姿指标计算只需权威关节向量；TCP/Frame 解析语义由适配器按
 *        装配闭包绑定，不在逐次请求内重复）。
 *
 * q 为权威关节向量（逐自由度 SI：转动 rad／移动 m；链序；全分量有限
 * ——kin.pose-metrics IllegalQ 判定语义归 kinematics 侧）。值语义。
 */
struct FkPortRequest {
    /// 权威关节向量（rad|m；链序）。
    std::vector<double> q;
    /// 取消观测（可空＝不可取消——kin.pose-metrics 为 <1 s 内联计算，
    /// 取消面为协议完备性保留；命中即 Canceled）。
    CancelSignal cancel;
};

// =====================================================================
// 端口调用的壳结局（端口层——区别于 IK 业务结局）
// =====================================================================

/**
 * @brief 端口单次调用的壳结局（§15.0 错误二分在端口层的落点）。
 *
 * 三态语义：
 *   - Ok：业务结果有效（IK 结局见 result／FK 指标见 metrics）；
 *   - PortError：端口层错误（适配失败/契约不匹配——环境类失败；
 *     errorToken 恒 kKinPortErrorPrefix 前缀；消费面按 §15.0 显性失败，
 *     不吞错不静默降级）；
 *   - Canceled：取消观测命中——**取消不是错误**（UX-03；零错误素材，
 *     调用方以取消语义收尾）。
 */
enum class KinPortCallStatus : std::uint8_t {
    /// 业务结果有效。
    Ok,
    /// 端口层错误（适配失败/契约不匹配——errorToken/errorMessage 必填）。
    PortError,
    /// 取消观测命中（非错误——UX-03）。
    Canceled,
};

/**
 * @brief solveIk 的端口回复（壳结局＋业务结果）。
 *
 * status==Ok 时 result 有效；status==PortError 时 errorToken（恒
 * kKinPortErrorPrefix 前缀）与 errorMessage（中文）必填、result 无效；
 * status==Canceled 时 result 无效且零素材（取消不是错误——UX-03）。
 * 值语义；线程安全（并发只读）。
 */
struct IkPortReply {
    /// 壳结局（KinPortCallStatus——三态）。
    KinPortCallStatus status = KinPortCallStatus::Canceled;
    /// 端口层错误 token（仅 PortError；"trajectory/kin-port-*" 前缀——
    /// 唯一书写点＝适配器，消费面按前缀常量判别）。
    std::string errorToken;
    /// 端口层错误中文文案（仅 PortError；说明适配失败点与实际值）。
    std::string errorMessage;
    /// IK 业务结果（仅 Ok 时有效）。
    IkPortResult result;
};

/**
 * @brief evaluateFk 的端口回复（壳结局＋位姿指标投影）。
 *
 * status 语义与 IkPortReply 同款（KinPortCallStatus——Ok/PortError/
 * Canceled 三态）。status==Ok 时 metrics 有效。值语义；线程安全。
 */
struct FkPortReply {
    /// 壳结局（KinPortCallStatus——三态）。
    KinPortCallStatus status = KinPortCallStatus::Canceled;
    /// 端口层错误 token（仅 PortError；"trajectory/kin-port-*" 前缀）。
    std::string errorToken;
    /// 端口层错误中文文案（仅 PortError）。
    std::string errorMessage;
    /// FK 位姿指标投影（仅 Ok 时有效）。
    FkPortMetrics metrics;
};

// =====================================================================
// 端口接口（§15.4——trajectory 拥有的最小接口；实现由装配面适配）
// =====================================================================

/**
 * @brief kinematics IK/FK 计算注入端口（§15.4 IKinematicsComputePort）。
 *
 * 契约要点（§15.4 逐行；适配器实现方义务）：
 *   - 语义透传：完整沿用 kin.task-point-ik／kin.pose-metrics 契约（五类
 *     结局、硬过滤记录、稳定排序、统一尺度）——不重解释、不二次过滤；
 *   - 确定性：同请求（含种子/配置）→ 等价解序（§15.4 合法示例"同种子
 *     同配置→等价解序"——NFR-COR-02 的端口侧承载）；
 *   - 前置：端口已在装配期注入（§15.4——缺失→装配失败，不运行；消费
 *     面对空指针 fail-fast）；
 *   - 错误：端口层失败以 PortError＋"trajectory/kin-port-*" token 返回
 *     （值面——不抛跨端口异常，适配器内部异常一律转译，不吞错）；
 *   - 所有权：实现由装配面持有并注入；消费面只持有裸指针视图、不接管
 *     生命周期（AGENTS §2.5 所有权标注）。
 *
 * 线程语义：由装配面声明（评估器 SingleThread——每 worker 每任务一
 * 实例，§15.0/§6.3）。
 */
class IKinematicsComputePort {
public:
    virtual ~IKinematicsComputePort() = default;

    /**
     * @brief 单点 IK 求解（kin.task-point-ik 服务语义透传）。
     *
     * @param request [in] 求解请求（IkPortRequest——目标位姿基座系、容差
     *                m/rad、限位 rad|m、种子；取消观测经请求携带）
     *
     * @return 回复（IkPortReply——Ok 时五类结局＋解集；PortError 时
     *         "trajectory/kin-port-*" token；Canceled 零素材）
     *
     * 确定性：同请求等价输出（种子/配置入请求身份——§15.4）；无副作用。
     */
    virtual IkPortReply solveIk(const IkPortRequest& request) = 0;

    /**
     * @brief 单点 FK 位姿指标（kin.pose-metrics 服务语义透传——§8.5
     *        奇异邻域判定的输入面）。
     *
     * @param request [in] 指标请求（FkPortRequest——权威关节向量 rad|m）
     *
     * @return 回复（FkPortReply——Ok 时条件数/奇异值投影；PortError/
     *         Canceled 语义同 solveIk）
     *
     * 确定性：同请求等价输出；纯计算零副作用。
     */
    virtual FkPortReply evaluateFk(const FkPortRequest& request) = 0;
};

}  // namespace sdurws::ird::trajectory

#endif  // IRD_TRAJECTORY_KINEMATICSPORT_HPP
