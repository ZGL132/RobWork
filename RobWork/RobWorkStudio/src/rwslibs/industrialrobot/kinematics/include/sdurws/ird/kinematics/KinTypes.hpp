/**
 * @file   KinTypes.hpp
 * @brief  kinematics 值模型公共头——TcpRef（TCP 引用值）、
 *         IKinRuntimeView（runtime 只读模型视图的本单元最小消费接口，
 *         T03 批次）；五类结局枚举、KinematicSolution、
 *         ConfigurationSignature、SolutionSet 值模型（T04 批次，表尾追加）。
 *
 * 设计依据：
 *   - units/kinematics.md §3.3（公共头布局表 KinTypes.hpp 行——"值模型、
 *     结局枚举"，任务列 T03/T04：本头随 T03 落位首批类型，T04 落位时在
 *     表尾追加解集/结局枚举等值模型——不重排既有声明）、§5.2 输入行
 *     （"TCP 引用（tool-definition oid＋tcpKey→快照解析）"——TcpRef
 *     字段出处）、§4.2（RuntimeSnapshot 消费面——IKinRuntimeView 的
 *     消费语义出处）、§14.4 条 6（"IKinRuntimeView 最小消费接口——
 *     runtime 只读视图的本单元侧封装，不新增 runtime 语义"）
 *   - 治理裁决 O-37（DTB §4.2 已裁决 closed，2026-09-22）：运行时模型
 *     视图＝**宿主注入**形态——评估宿主构建绑定请求 RuntimeSnapshot 的
 *     只读模型视图、经评估器工厂闭包注入。本头即该裁决在 kinematics 侧
 *     的类型落点：本单元定义自有最小接口，宿主（L5 装配）以适配器实现
 *     之；对端 evidence.md §9 的注入点文字增补归 evidence 卡所有者
 *     （P-KIN-2 在途——本单元不依赖、不冻结该侧契约）。
 *   - 任务契约 tasks/foundation/WP-15-T03.json（acceptance 2/4 的类型面）
 *
 * 背景说明（为什么不是直接消费 runtime::IRuntimeModelView）：卡面 §9.2
 * IFkEvaluator.evaluate 的视图参数类型为 IKinRuntimeView——自有最小接口
 * 使 (a) 单元测试可在无完整 RuntimeSnapshot 的条件下以测试替身实现视图
 * （R-KIN-1"T03 前以测试替身先行"），(b) 本单元对 runtime 的依赖收敛到
 * §4.2 消费面真正用到的成员，runtime 契约后续演进时的对账面最小。
 * 宿主适配器（runtime::IRuntimeModelView → IKinRuntimeView）归 L5 装配
 * （evidence §3.3 D-07 注入先例同款分工——适配器十行级，非本单元交付物）。
 *
 * 线程安全：本头全部实体为纯值/纯接口（无共享可变状态）；实现类的线程
 * 约束随实现注释声明。
 */

#ifndef IRD_KINEMATICS_KINTYPES_HPP
#define IRD_KINEMATICS_KINTYPES_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <rw/math/Transform3D.hpp>

#include <sdurws/ird/core/Digest.hpp>        // ContentIdentity（快照/切片/配置内容身份）
#include <sdurws/ird/core/Evaluation.hpp>    // EvaluationMode（模式词表——入结果身份）
#include <sdurws/ird/core/Identity.hpp>      // ObjectId（工具对象稳定身份）
#include <sdurws/ird/runtime/CanonicalModel.hpp>  // runtime::CanonicalModel（只读模型真值）

namespace sdurws::ird::kinematics {

// =====================================================================
// TcpRef——TCP 引用（§5.2 输入行"tool-definition oid＋tcpKey→快照解析"）
// =====================================================================

/**
 * @brief TCP 引用值：指名快照内的一个工具定义对象及其 TCP。
 *
 * 解析规则（§5.2/§9.6 消费语义的 T03 实现口径，随单元卡 §14.6 登记）：
 *   - toolObject 经快照模型的对象索引（CanonicalModel::findObject）解析到
 *     CanonicalTool——解析失败（无工具/引用悬空/指名对象非工具）＝
 *     KinematicsError{NoTcp}（§9.6 KIN-NO-TCP"TCP 未配置/悬空"两分语义）；
 *   - tcpKey 为 TCP 选择键：**空串＝canonical TCP**（规范模型每工具恰一个
 *     tcpOffset——runtime §4.3.4 单 TCP 面，与 KIN-14 默认 TCP 单值同源）；
 *     非空时必须与该工具的 canonical TCP 身份 token（＝工具 localName——
 *     canonical TCP 以其所属工具命名）精确相等，否则＝
 *     KinematicsError{FrameUnresolved}（§9.2 @错误 行"tcpRef 指名的 TCP 帧
 *     不存在"——多 TCP 工具是 modeling 侧语义扩展，canonical 面就绪前
 *     键不命中即帧未解析，不做前缀/模糊匹配，R-4 禁拼串定位）。
 *
 * 值语义纯结构；线程安全（并发只读安全）。确定性：同引用同解析结果
 * （解析只依赖快照内容——NFR-COR-01/02）。
 */
struct TcpRef {
    /// 工具定义对象稳定身份（tool-definition oid——§5.2 输入行；快照内
    /// 引用，跨 snapshot 使用属调用方契约违约——§9.2"非法调用"行）。
    core::ObjectId toolObject;
    /// TCP 选择键（空串＝canonical TCP；非空须与工具 localName 精确相等
    /// ——解析规则见结构体注）。单位/量纲：无（身份 token）。
    std::string tcpKey;

    /// 精确等值（逐成员；身份按 core 强类型等值、键按字节串等值——
    /// 无容差，身份面比较禁用数值容差）。
    bool operator==(const TcpRef& o) const
    {
        return toolObject == o.toolObject && tcpKey == o.tcpKey;
    }
    bool operator!=(const TcpRef& o) const { return !(*this == o); }
};

// =====================================================================
// IKinRuntimeView——runtime 只读模型视图的最小消费接口（§4.2/§14.4-6；
// O-37 宿主注入形态的 kinematics 侧类型落点）
// =====================================================================

/**
 * @brief 运行时模型视图的本单元最小消费接口（§4.2 消费面在 T03 的投影）。
 *
 * 语义边界（§14.4 条 6 原文口径）：**不新增 runtime 语义**——本接口只把
 * §4.2 消费面表中本单元实际消费的成员以自有虚接口收拢，每个成员的语义
 * 与 runtime 侧对应访问器逐字一致：
 *   - model()：规范模型只读真值（关节类型/轴/origin/zeroOffset/限位/
 *     工作范围、工具/TCP、T_world_base 唯一来源、gravityWorld）——约束：
 *     只读；不缓存跨请求（§4.2"每请求重建视图"）。
 *   - worldToBase()：T_world_base（世界系→基座系读法 core §4.6——
 *     p_world＝T_world_base·p_base）；基座—世界唯一读取点（M-11/AT-37：
 *     本单元禁止第二套基座变换代数，tcpInWorld 只经该值组合）。
 *
 * 生命周期与线程（§9.4 表 IKinRuntimeView 行）：每请求构建（宿主持有）、
 * 请求结束失效；构建于宿主、使用于工作线程（**只读**）——实现方须保证
 * evaluate() 调用期间视图存活且并发只读安全；评估器不接管所有权
 * （§9.2 @所有权 行"view 由调用方持有"）。
 *
 * 扩展纪律：后续任务（T04 IK 等）需要更多消费面成员时在本接口**表尾
 * 追加**纯虚成员并同步登记单元卡（不重排既有成员——虚表序进入二进制
 * 契约）；T03 的最小面＝上列两成员（位姿指标无重力相关量，gravityBase()
 * 随首个重力消费任务追加——§4.5"重力相关量仅经 gravityBase"的约束在
 * 消费出现时生效）。
 */
class IKinRuntimeView {
public:
    virtual ~IKinRuntimeView() = default;

    /// 规范模型只读真值（§4.2 消费面 model() 行——语义同
    /// runtime::IRuntimeModelView::model()；返回引用所指对象须在视图
    /// 存活期内稳定）。
    virtual const runtime::CanonicalModel& model() const = 0;

    /// T_world_base（§4.2 消费面 worldToBase() 行——世界系→基座系读法
    /// core §4.6；平移单位 m、旋转无量纲正交阵；前置：快照编译产物已过
    /// 合法域校验）。
    virtual rw::math::Transform3D<double> worldToBase() const = 0;
};

// =====================================================================
// 以下为 T04 批次表尾追加（WP-15-T04——§3.3 布局表 KinTypes.hpp 行任务列
// T03/T04 的 T04 半区：五类结局枚举＋解集值模型；既有 T03 声明不重排，
// 虚表序/枚举数值进入二进制契约的追加纪律见本头 IKinRuntimeView 注）。
// =====================================================================

// =====================================================================
// IkOutcomeKind——IK 五类结局枚举（§5.4 判定表；枚举值＝表行号）
// =====================================================================

/**
 * @brief IK 求解的五类结局（§5.4 铁律表逐行；枚举数值＝表行号 1~5）。
 *
 * 铁律（§5.4 原文，D-KIN-7）：1↔2/3 互斥；4 是 1 的子形态；2/3/4 任何
 * 组合都**不得升级为任务级确定性不可行**——本枚举**故意不含任何"任务级
 * 不可行"值**（不可行结论的唯一来源＝结局 5 的证明素材经 evidence
 * validateProof 校验成立，本单元只产素材不裁定）。
 *
 * 取消不是结局：取消令牌触发时产出携带 cancelled=true 的结果对象、
 * 无终局字段（§9.2 @取消 行），不占用本枚举值。
 */
enum class IkOutcomeKind : std::uint8_t {
    /// 行 1：去重后存在 ≥1 个通过全部硬过滤的解（可行性素材——判定仍归
    /// evidence，本单元不下结论）。
    SolutionsFound = 1,
    /// 行 2：全部初值迭代至上限未收敛（零候选）——必附搜索未果记录
    /// （预算/初值数/迭代统计）→ DataInsufficient 素材；**不得输出不可行**。
    MultiInitNoConvergence = 2,
    /// 行 3：有收敛候选但全部被限位/残差/碰撞过滤——必附搜索未果记录＋
    /// 逐解过滤记录（原因/对象对）→ DataInsufficient 素材；**不得输出
    /// 不可行**（含"全部因碰撞被过滤"——构型级碰撞仅过滤该解，C8）。
    AllCandidatesFiltered = 3,
    /// 行 4：部分解碰撞、其余有效（1 的子形态——碰撞解经过滤记录保留
    /// 诊断价值；任务结论不受单解影响）。
    PartialCollision = 4,
    /// 行 5：目标位置超出解析工作半径上界（唯一允许产出证明素材的路径；
    /// 素材附推导输入与 expectedSliceId 绑定——成立与否由 evidence
    /// validateProof 校验，本单元不裁定）。
    AnalyticBoundExceeded = 5,
};

// =====================================================================
// SolutionFilterReason——解被硬过滤的原因（构型级记录；§5.3 硬过滤三段）
// =====================================================================

/**
 * @brief 单个收敛候选被硬过滤的原因（§5.3 硬过滤顺序固定①残差复验→
 *        ②限位→③碰撞；记录取**首个命中的阶段**——顺序即语义，登记于
 *        单元卡 §14.6 v0.4）。
 *
 * 与 evidence::SearchFilterReason（§6.3 三值）逐值对应：
 * ResidualRecheck→Residual、JointLimit→JointLimit、Collision→Collision
 * （映射在评估器组装搜索未果记录时执行，本枚举不依赖 evidence 头）。
 */
enum class SolutionFilterReason : std::uint8_t {
    /// 阶段①：FK 复算残差超两容差之一（比较型量：位置 m／姿态 rad）。
    ResidualRecheck,
    /// 阶段②：解超关节限位（有界关节出 bounds／continuous 出工作范围
    /// ——§6.1 直接比较无跨周取模）。
    JointLimit,
    /// 阶段③：构型级碰撞（policy 会话判定；仅过滤该解，不下任务结论）。
    Collision,
};

// =====================================================================
// ConfigurationSignature——构型身份的精确编码（记录键；I-KIN-3）
// =====================================================================

/**
 * @brief 计算构型的 canonical 签名（§6.1：q 的全精度定宽编码——**构型
 *        身份**，仅作记录/统计键）。
 *
 * I-KIN-3 语义边界（§6.1 原文）：签名**不用于去重**——去重是求解期成对
 * 容差比较（逐轴阈值）；两个相差 1e-9 rad 的解签名不同但去重等价，反之
 * 去重等价也不蕴含签名相等（容差比较非哈希等价）。契约测试锁定该两向
 * 语义（V-04 组）。
 *
 * 编码布局（codec 版本 1）：magic "IRDSIG01"（8 字节 ASCII）＋自由度
 * u32（小端）＋逐自由度 f64（IEEE754 位模式小端 8 字节，含 ±0 的位级
 * 区分——全精度，无舍入）。同构型同签名、跨进程逐字节一致（NFR-COR-02
 * ——纯位模式拷贝，无格式化）。
 *
 * @param q [in] 权威关节向量（rad／m；须全部分量有限——调用方契约，
 *            非有限输入的签名无定义，本函数不校验不改写）
 * @return 小写十六进制串（长度 16＋16·q.size() 字符；确定性）
 *
 * 纯函数；线程安全；不抛。
 */
std::string configurationSignature(const std::vector<double>& q);

// =====================================================================
// CollisionStatus——解的碰撞评价状态（§6.1 collisionStatus 三字段）
// =====================================================================

/**
 * @brief 单个解的碰撞评价状态（§6.1 KinematicSolution.collisionStatus）。
 *
 * 语义（§8.1/KIN-05）：evaluated=false＝策略未启用碰撞或缺检测器——
 * **证据缺失**，绝不解读为"无碰撞"（调用方不得把 evaluated=false 且
 * inCollision=false 当作可行凭据）；objectIdPairs 仅在 inCollision=true
 * 时非空。值语义纯结构；线程安全（并发只读）。
 */
struct CollisionStatus {
    /// 是否完成了碰撞评价（会话在场且调用成功）。
    bool evaluated = false;
    /// 碰撞判定（仅 evaluated=true 时有意义）。
    bool inCollision = false;
    /// 碰撞对象对（成对展平 [a1,b1,a2,b2,…]——ObjectId；仅 inCollision=
    /// true 时非空；身份来自 policy 会话判定明细，本单元不拼装名称）。
    std::vector<core::ObjectId> objectIdPairs;
};

// =====================================================================
// KinematicSolution——单个可行解（§6.1 值模型逐字段）
// =====================================================================

/**
 * @brief 一个通过全部硬过滤的可行构型（§6.1 值模型行；值语义，随
 *        payload 归档）。
 *
 * 全部物理量逐项单位（AGENTS §2.5）：q 为权威关节向量（逐自由度 SI：
 * 转动 rad／移动 m，§5.1 q_authoritative 口径）；残差为位置 m／姿态
 * rad；jointMargins 与 minimumJointMargin 为无量纲归一化比（D-KIN-6）；
 * manipulability/conditionNumber 为雅可比导出无量纲量（D-KIN-2）。
 *
 * 确定性：同请求同初值序 → 同解同字节（浮点布局含于 canonical 编码
 * ——NFR-COR-01）。线程安全（并发只读）。
 */
struct KinematicSolution {
    /// 权威关节向量（rad／m；链序）。
    std::vector<double> q;
    /// 收敛/复验残差：位置（m）、姿态（rad）——均 ≤ 请求两容差。
    double positionResidual = 0.0;
    double orientationResidual = 0.0;
    /// 逐自由度归一化关节裕量（无量纲；连续无工作范围关节＝+∞）。
    std::vector<double> jointMargins;
    /// 有界关节最小裕量（无量纲；全无界＝+∞）。
    double minimumJointMargin = 0.0;
    /// 可操作度 w＝√det(J·Jᵀ)（D-KIN-2）。
    double manipulability = 0.0;
    /// 条件数 σmax/σmin（D-KIN-2）。
    double conditionNumber = 0.0;
    /// 碰撞评价状态（evaluated=false＝未启用碰撞——证据缺失语义）。
    CollisionStatus collisionStatus;
    /// 产生本解的初值下标（初值集序——稳定排序第 4 键兜底）。
    std::uint32_t sourceInitIndex = 0;
    /// 本初值的迭代次数（计数，无量纲——搜索未果迭代统计素材）。
    std::uint32_t iterations = 0;
    /// 构型签名（记录键——I-KIN-3：不用于去重，语义见函数注）。
    std::string signature;
    /// 求解器算法契约版本（kIkSolverContractVersion——§8.4 升级即切片失效）。
    std::uint32_t solverContractVersion = 0;
};

// =====================================================================
// 解集值模型：TargetRef／RequestIdentity／FilteredSolutionRecord／
// IkSearchRecord／Statistics／IkSolutionSet（§6.1 SolutionSet 行展开）
// =====================================================================

/**
 * @brief 结果与对象的双重绑定面（§6.1 targetRef{pointOid, conditionId?}）。
 *
 * pointOid＝任务点对象身份（req.points 闭包内）；conditionId＝工况对象
 * 身份（req.conditions 闭包内；单点求解可空）。身份语义与 evidence
 * CaseId 同源（core::ObjectId），本头不依赖 evidence。
 */
struct IkTargetRef {
    /// 任务点对象身份（保留值＝未绑定——纯服务直调面的合法缺省）。
    core::ObjectId pointOid;
    /// 工况对象身份（可空——批量通道 T05 必填）。
    std::optional<core::ObjectId> conditionId;
};

/**
 * @brief 结果身份（§6.1 requestIdentity{snapshotId, sliceId, configDigest,
 *        mode, seed}＋referenceQ——D-KIN-4 显式化）。
 *
 * 身份纪律：**构型身份 ≠ 结果身份**——同构型在不同请求/模式下是不同
 * 结果条目；referenceQ 显式入身份（§6.3"身份含之"、禁止隐式读会话姿态
 * ——会话姿态只可作 UI 填充默认值，KIN-06/AT-04）。referenceQ 入身份的
 * 形态＝逐自由度全精度 f64 值列（canonical 编码位模式直写——卡面 §6.1
 * 要点列未穷尽字段，随卡 §14.6 v0.4 登记）。
 *
 * sliceId 的本体类型＝core::ContentIdentity（evidence::SliceId 的语义
 * 别名同型——本头不依赖 evidence 头，语义注释在此登记）。
 */
struct IkRequestIdentity {
    /// 来源快照内容身份（EvaluationRequest.snapshot.snapshotId）。
    core::ContentIdentity snapshotId;
    /// 冻结输入切片身份（EvaluationRequest.slice.sliceId——CON-04）。
    core::ContentIdentity sliceId;
    /// 求解配置摘要（config.ik canonical 摘要——T10 落位前允许零值）。
    core::ContentIdentity configDigest;
    /// 评估模式（Quick 结果仅筛选/排序依据——EVI-01；入身份）。
    core::EvaluationMode mode = core::EvaluationMode::Verified;
    /// 确定性随机种子（初值策略 SeededRandom 的序列源；0＝未用随机策略）。
    std::uint64_t seed = 0;
    /// 排序参考构型（rad／m；D-KIN-4——显式评估输入，默认值的落点在
    /// 请求装配层，本单元不读任何会话状态）。
    std::vector<double> referenceQ;
};

/**
 * @brief 单条被硬过滤解的记录（§6.1 filteredRecords 元素——"为什么少了
 *        解"的诊断价值承载；不计入解集排序序列、不进入可行素材）。
 *
 * 与 evidence::FilteredSolution（§6.3）的字段对应：signature→
 * solutionRef（非空＋无 NUL 的编码安全下限满足——十六进制串）、reason→
 * filterReason（三值映射见 SolutionFilterReason 注）。其余字段为构型级
 * 指标（原因/对象对/指标——§6.1 原文），供 UI 检查器与报告解释。
 */
struct FilteredSolutionRecord {
    /// 被过滤构型（rad／m——构型级记录，不下结论）。
    std::vector<double> q;
    /// 过滤原因（首个命中的硬过滤阶段——顺序固定①②③）。
    SolutionFilterReason reason = SolutionFilterReason::ResidualRecheck;
    /// 复验残差（m／rad——阶段①量；其余阶段为命中时的当次值）。
    double positionResidual = 0.0;
    double orientationResidual = 0.0;
    /// 构型级指标（无量纲——与 KinematicSolution 同口径）。
    double minimumJointMargin = 0.0;
    double manipulability = 0.0;
    /// 碰撞对象对（成对展平；仅 Collision 原因时非空）。
    std::vector<core::ObjectId> objectIdPairs;
    /// 产生该候选的初值下标。
    std::uint32_t sourceInitIndex = 0;
    /// 该候选的迭代次数。
    std::uint32_t iterations = 0;
    /// 构型签名（记录键——映射 evidence FilteredSolution.solutionRef）。
    std::string signature;
};

/**
 * @brief 搜索未果记录（§6.1 searchRecord／§8.2 表行——结局 2/3 必附）。
 *
 * 内容四要素（§8.2 原文"预算、初值数、迭代统计、逐解过滤记录"）：前三
 * 在本结构；逐解过滤记录在 IkSolutionSet::filteredRecords（同批交付）。
 * 下游语义（§8.1 搜索未果口径 C5/C8）：→DataInsufficient 素材，**不得
 * 输出不可行结论**；换初值/扩预算后按同一冻结输入复评可翻转（V-07）。
 * 映射 evidence::SearchExhaustedRecord（searchBudgetUsed/
 * initialGuessesTried/filteredSolutions）在评估器组装面执行。
 */
struct IkSearchRecord {
    /// 已用搜索预算＝全部初值的迭代次数之和（计数，无量纲）。
    std::uint64_t searchBudgetUsed = 0;
    /// 已试初值数（计数——"扩大初值"复评的对照基线）。
    std::uint64_t initialGuessesTried = 0;
    /// 逐初值迭代次数（初值集序——迭代统计明细）。
    std::vector<std::uint32_t> iterationsPerInit;
};

/**
 * @brief 解集统计（§6.1 statistics 四计数；均 u64）。
 *
 * 口径（§14.6 v0.4 登记）：rawCount＝初值数；convergedCount＝通过阶段①
 * 残差复验的候选数；dedupedCount＝去重后（＝最终 solutions 数）；
 * filteredCount＝filteredRecords 数（硬过滤移除，不含去重合并）。
 */
struct IkSolutionSetStatistics {
    std::uint64_t rawCount = 0;
    std::uint64_t convergedCount = 0;
    std::uint64_t dedupedCount = 0;
    std::uint64_t filteredCount = 0;
};

/**
 * @brief IK 解集（§6.1 SolutionSet 值模型行——结果身份＋稳定排序解集＋
 *        过滤/搜索记录＋统计）。
 *
 * 不变式（求解器产出保证）：solutions 已按 §6.3 四键稳定排序（去重保留
 * 组内代表＝sourceInitIndex 最小者）；filteredRecords 与 solutions 无交
 * （构型级互斥——被过滤解不入排序序列）；结局 2/3 时 searchRecord 必填
 * 且 solutions 为空。
 *
 * 排序/筛选/统计的**消费面**唯一定义点＝IKinematicSolutionSet 视图
 * （SolutionSet.hpp——NFR-MNT-04：UI/报告不各自排序）。值语义；线程
 * 安全（并发只读）。
 */
struct IkSolutionSet {
    /// 对象绑定（§5.6 与快照双重绑定——归档后可溯源）。
    IkTargetRef targetRef;
    /// 结果身份（含 referenceQ——D-KIN-4）。
    IkRequestIdentity requestIdentity;
    /// 可行解（稳定排序；§6.3 四键）。
    std::vector<KinematicSolution> solutions;
    /// 被硬过滤解记录（诊断价值；不入排序序列）。
    std::vector<FilteredSolutionRecord> filteredRecords;
    /// 搜索未果记录（结局 2/3 必附）。
    std::optional<IkSearchRecord> searchRecord;
    /// 四计数统计。
    IkSolutionSetStatistics statistics;
};

}  // namespace sdurws::ird::kinematics

#endif  // IRD_KINEMATICS_KINTYPES_HPP
