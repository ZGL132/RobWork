/**
 * @file   Constraint.hpp
 * @brief  静态硬约束编排（O4/O7 管线执行件）——R1 约束词表与执行清单、
 *         阶段锁约束解析（OPT-STAGE-LOCKED）、约束事实/淘汰原因记录、
 *         限位/碰撞探针接缝（policy 唯一实现消费）与静态硬约束先行管线
 *         （WP-20-T04）。
 *
 * 设计依据：
 *   - units/optimization.md §6.1（约束分类 13 类词表——R1 静态子集）、
 *     §6.2（R1 约束执行顺序 OPT-B 管线——本文件管线执行序的唯一权威：
 *     输入前置→阶段锁→变量锁定→补丁合法→候选编译→拓扑→关节限位→Must
 *     工位可达→Must 区域覆盖→静态碰撞，"硬约束先行：任何指标计算前完成
 *     2～7 项硬约束；硬约束失败的候选不进入可行集〔AT-09〕"）、§6.5
 *     （阶段锁诊断的呈现边界——运行启动阻塞非候选淘汰）、§5.7（阶段锁
 *     全表）、§8.3 第 7~9 步（约束事实/指标事实/淘汰原因的记录面）、
 *     §7.5（候选分类判定流——消费 evidence aggregateVerdict 五级决策表，
 *     P-EV-3 字面顺序引用其现状）、§6.7（内部消费链：kin.task-points-batch
 *     ＋kin.region-coverage＋policy IJointLimitEvaluator；"静态碰撞只消费
 *     policy 的唯一实现〔④端口；SampleSet 查询形态；不传阈值/模式参数
 *     ——R-POL-5〕；不在 optimization 复制碰撞算法〔N4〕"）
 *   - 需求 OPT-03（硬约束先行；阶段 B 仅静态硬约束，联合约束归 OPT-D）、
 *     EVI-01（正式通过判定五条件——evidence 唯一，本管线只消费）、
 *     EVI-02（必验工况全覆盖——漏验不出正式通过，AT-09 反例）、AT-19
 *     （碰撞三入口一致——唯一实现在 policy，本单元零碰撞算法）
 *   - 需求 §8.1 C5/C8（搜索未找到有效解/构型碰撞不构成任务级不可行——
 *     数值搜索未果判 DataInsufficient，构型级碰撞只做硬过滤记录）
 *   - evidence 公共契约（消费不复制——DOPT-1）：EvaluatorRegistry（③端口
 *     find/create/evaluate）、VerdictInput/aggregateVerdict（五级判定）、
 *     checkFormalPassEligibility（正式通过资格——§7.2 五条件）
 *   - 诊断码：kOptStageLocked/kOptInputInvalid/kOptPatchIllegal/kOptVarLocked/
 *     kOptCandidateCompileFailed/kOptTopologyRejected/kOptEvaluatorMissing
 *     （DiagCodes.hpp 常量——§6.6 登记表）
 *   - 任务契约 tasks/foundation/WP-20-T04.json acceptance 1~4
 *
 * 背景说明（本头在优化链路中的位置——第一读者须知）：
 *   WP-20-T04 的交付面是"约束编排"：把一个候选补丁沿 §6.2 固定顺序推过
 *   全部静态硬约束，产出逐约束事实（§8.3 第 7 步）与候选分类（§7.5 判定
 *   流）。三个边界必须分清：
 *   ① **计算与编排分离**——关节限位经 policy IJointLimitEvaluator（探针
 *      接缝投影）、Must 可达/区域覆盖经 ③端口评估器（evidence 注册表
 *      find→create→evaluate）、碰撞经 policy 唯一实现的会话（探针接缝）；
 *      本单元零限位/可达/覆盖/碰撞算法（R-1/N4）。
 *   ② **判定与编排分离**——候选是否不可行/数据不足/完整的最终判定唯一归
 *      evidence::aggregateVerdict（五级决策表，P-EV-3 字面顺序）与
 *      checkFormalPassEligibility（正式通过资格五条件）；本管线只把约束
 *      执行事实组装为 VerdictInput 并映射结果到候选状态（PA-1/DOPT-1：
 *      不复制第二套判定）。
 *   ③ **启动阻塞与候选淘汰分离**——阶段锁（StageB 引用联合约束/请求
 *      StageD 管线）是运行启动阻塞（抛 OptimizationError，OPT-STAGE-LOCKED，
 *      §6.5：不静默降级、不呈现为候选淘汰）；候选级失败走结构化返回
 *      （CandidateEvaluationRecord，Infeasible/EvaluationFailed——§7.5）。
 *
 * ★ 落位范围口径（诚实登记，防扩大）：§14.3 行"§6 → Constraint.hpp/
 *   Preflight.hpp＋src＋码表注册 | WP-20-T04/T08/T02"——本任务交付
 *   Constraint.hpp＋src/Constraint.cpp（约束编排＋OPT-STAGE-LOCKED 消费）；
 *   Preflight.hpp/IOptimizationPreflightService（§6.4 检查面 20 项＋
 *   allowances 五元组）随 WP-20-T08；IEvaluationPipeline 接口本体随
 *   WP-20-T06（§3.1 布局表 EvaluatorPorts.hpp 行——本任务的管线执行件
 *   StaticHardConstraintPipeline 是其 §12.2 语义的 T04 子集形态，T06 落
 *   批量编排时按既有形态收敛）；三项静态指标计算（§7，管线 §6.2 第 11 步
 *   之后）随 WP-20-T05——本管线不产指标事实。
 *   接缝口径：候选编译（ICandidateCompiler）与限位/碰撞探针（IJointLimitProbe/
 *   IStaticCollisionProbe）是 policy/runtime 唯一实现的域侧薄投影接缝——
 *   生产适配器＝对唯一实现调用的机械翻译（评估阈值唯一来源 policy 策略集，
 *   R-POL-5 结构防覆盖），落位随 P-OPT-2 候选物化通道裁决（§16.3 R-OPT-1）；
 *   裁决前测试以可控替身注入（kinematics IIkSolver 注入口/O-37 同款形态）。
 *
 * 线程约束：ConstraintSpec/ConstraintFact/RejectionReason/探针值类型全部
 *   纯值（可并发拷贝）；OptimizationConstraintProvider 只读纯函数面（可并
 *   发）；StaticHardConstraintPipeline 构造后只读（deps 全为非 owning 只读
 *   指针），evaluate 单线程调用（R1 单候选批单线程——卡 §6.7 threadSafety
 *   行"线程并行由编排/多 worker 掌控"，T06 编排面负责）。
 * 确定性：同输入同 deps ⇒ 同 CandidateEvaluationRecord（NFR-COR-02——
 *   约束事实按 §6.2 顺序、判定消费 evidence 纯函数面、无随机源）。
 */

#ifndef SDURWS_IRD_OPTIMIZATION_CONSTRAINT_HPP
#define SDURWS_IRD_OPTIMIZATION_CONSTRAINT_HPP

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <sdurws/ird/core/Evaluation.hpp>       // core::EvaluationMode/EngineeringStatus——
                                                // 评估模式与工程判定词表（evidence 汇总输出）
#include <sdurws/ird/core/Identity.hpp>         // core::TaskIdentity——评估请求任务绑定
#include <sdurws/ird/core/DiagData.hpp>         // core::DiagnosticRecord——域诊断透传载体
#include <sdurws/ird/evidence/Evidence.hpp>     // evidence::CaseCoverageMatrix/
                                                // RegionCoverageEvidence/EvidenceManifest/
                                                // DeterministicInfeasibilityProof——汇总事实面
#include <sdurws/ird/evidence/Evaluator.hpp>    // evidence::EvaluatorRegistry（③端口）/
                                                // IEvaluationContext——评估器注册表与调用约定
#include <sdurws/ird/evidence/Slice.hpp>        // evidence::InputSlice——冻结输入切片
#include <sdurws/ird/evidence/Verdict.hpp>      // evidence::VerdictTrace/VerdictResult/
                                                // IProducerRegistryView/IProfileRegistryView——
                                                // 判定消费面（aggregateVerdict 参数）
#include <sdurws/ird/optimization/CandidatePatch.hpp>  // CandidatePatch/CandidateId——被评估候选
#include <sdurws/ird/optimization/Types.hpp>    // OptimizationStage/CandidateStatus/
                                                // OptimizationError——阶段与候选状态词表
#include <sdurws/ird/optimization/Variable.hpp> // VariableBinding——研究绑定集（锁定检查输入）

namespace sdurws::ird::optimization {

// =====================================================================
// ③端口评估键（消费 kinematics 域评估器的注册键——键值权威＝kinematics
// 单元卡 §4.3 注册键与其 Evidence.hpp 常量。本单元禁 include kinematics
// 头〔R-1 业务互链禁止〕，故以字面常量承载；契约测试以真 kinematics
// 评估器注册进 evidence::EvaluatorRegistry 后按本常量 find 命中钉住——
// 键漂移即红。注意评估键词形＝kebab（evidence isValidEvaluationKey，
// 不含点）；卡面点形键 "kin.task-points-batch" 是文档书写形态，其随附
// 同步偏差已登记于 kinematics 卡 §14.6 v0.3）
// =====================================================================

/// kin.task-points-batch 的注册键（Must 工位可达——§6.1 表第 5 行）。
inline constexpr std::string_view kKinTaskPointsBatchKey = "kin-task-points-batch";
/// kin.region-coverage 的注册键（Must 区域覆盖——§6.1 表第 6 行）。
inline constexpr std::string_view kKinRegionCoverageKey = "kin-region-coverage";

// =====================================================================
// R1 约束词表（卡 §6.1 表 1~7 行的 OPT-B 执行子集；枚举值序＝§6.2 管线
// 执行序——顺序即登记契约，值一经交付不得改动/插入）
// =====================================================================

/**
 * @brief R1 静态硬约束词表（§6.2 管线执行项——枚举序即执行序）。
 *
 * 词表边界（防扩大）：仅含 OPT-B 静态子集十项；轨迹/动力学/drivetrain
 * 性能/器件联合约束（§6.1 表第 8~11 行，阶段 D）**不落枚举值**——不预建
 * 占位（NFR-MNT-04；WP-21-T02 增量修订时表尾追加）。用户硬约束/软目标
 * （§6.1 表第 12/13 行）无需求承载，永不落表。
 */
enum class ConstraintId {
    InputPrecondition,       ///< 输入前置（快照/版本/身份一致——§6.1 #1；管线内为
                             ///  汇总事实面在场检查，完整 Preflight 归 WP-20-T08）
    StageCapability,         ///< 阶段能力（阶段锁——§5.7/§6.5；违例＝启动阻塞非候选淘汰）
    VariableLock,            ///< 变量边界和锁定（§6.1 #2 的锁定面——§5.4/OPT-VAR-LOCKED）
    PatchValidity,           ///< 补丁合法性（§5.5——OPT-PATCH-ILLEGAL；I-OPT-9 不截断）
    CandidateCompile,        ///< 候选编译（§6.2 第 5 步——OPT-CANDIDATE-COMPILE-FAILED
                             ///  透传 RT-* 码；编译链归 runtime，P-OPT-2 裁决前接缝）
    Topology,                ///< 拓扑/模型合法性（§6.2 第 6 步——补丁结构不变式＋基线
                             ///  链型启用范围；"补丁不改拓扑 ⇒ 候选拓扑＝基线拓扑"）
    JointLimit,              ///< 关节限位（§6.1 #4——policy IJointLimitEvaluator 值域/区间）
    MustStationReachability, ///< Must 工位可达（§6.1 #5——kin.task-points-batch，KIN-03）
    MustRegionCoverage,      ///< Must 区域覆盖（§6.1 #6——kin.region-coverage，KIN-04）
    StaticCollision,         ///< 静态碰撞（§6.1 #7——policy 唯一实现 SampleSet；C8 构型级
                             ///  过滤记录不上升任务不可行）
};

/**
 * @brief 约束稳定 token（"opt.constraint.<slug>"——约束事实/淘汰原因/
 *        导出的确定性书写；slug 与枚举值一一对应，冻结）。
 * @param id [in] 约束枚举值
 * @return 稳定 token 视图（编译期字面量，生命周期静态）
 */
std::string_view toToken(ConstraintId id) noexcept;

/**
 * @brief R2 联合约束家族（§6.1 表第 8~11 行的**引用词表**——只承载
 *        "研究定义引用了哪个联合约束族"这一事实，不是可执行约束）。
 *
 * 为什么独立成枚举而不落 ConstraintId：阶段 B 引用联合约束的处理是
 * **启动拒绝**（OPT-STAGE-LOCKED，§5.7/§6.5——不静默降级），被引用项
 * 根本不会进入执行清单；为其落可执行枚举值即预建占位（NFR-MNT-04）。
 * WP-21-T02 冻结联合管线时，各家族对应的可执行约束随其卡面表尾追加
 * ConstraintId 枚举值，本枚举保持引用词表不变。
 */
enum class JointConstraintFamily {
    Trajectory,            ///< 轨迹约束族（trj-sequence-plan——阶段 D）
    Dynamics,              ///< 动力学约束族（dyn-rnea-analysis——阶段 D）
    DrivetrainPerformance, ///< drivetrain 性能约束族（dt.mapping——阶段 D；注意
                           ///  与 drivetrain.ratio **变量**的 StageB 放行不冲突
                           ///  ——V12-02/DOPT-15：参数可编辑≠性能可评估）
    DeviceJoint,           ///< 器件联合约束族（sel.combination-check——阶段 D）
};

/**
 * @brief 联合约束家族稳定 token（"opt.constraint.trajectory"/
 *        "opt.constraint.dynamics"/"opt.constraint.drivetrain-performance"/
 *        "opt.constraint.device-joint"）——研究定义约束激活集的书写词表。
 * @param f [in] 家族枚举值
 * @return 稳定 token 视图（编译期字面量）
 */
std::string_view toToken(JointConstraintFamily f) noexcept;

/**
 * @brief 约束执行清单条目（§12.2 ConstraintSpec——编排的最小执行单元）。
 *
 * 值语义；线程安全：纯值。
 */
struct ConstraintSpec {
    ConstraintId constraintId = ConstraintId::InputPrecondition; ///< 约束（词表值）
    /// 依据评估键/依据标识（§8.3 第 7 步"依据评估键"字段）：
    /// ③端口消费项填注册键（kKinTaskPointsBatchKey/kKinRegionCoverageKey）；
    /// ④端口/本地检查项填依据标识（如 "policy.collision-session"）或空串
    /// （纯本地结构检查）。本字段是事实溯源锚，非路由键——管线路由按
    /// constraintId 执行。
    std::string evaluationKey;

    bool operator==(const ConstraintSpec& o) const
    {
        return constraintId == o.constraintId && evaluationKey == o.evaluationKey;
    }
    bool operator!=(const ConstraintSpec& o) const { return !(*this == o); }
};

/**
 * @brief R1 内置约束执行清单（§6.2 全序——纯函数，同调用恒同值）。
 *
 * 清单内容（顺序即执行序）：InputPrecondition → StageCapability →
 * VariableLock → PatchValidity → CandidateCompile → Topology → JointLimit
 * → MustStationReachability(kin-task-points-batch) →
 * MustRegionCoverage(kin-region-coverage) → StaticCollision
 * (policy.collision-session)。
 *
 * @return §6.2 顺序的十项清单（每次调用返回新值——确定性 NFR-COR-02）
 *
 * 纯函数；线程安全（可重入）。
 */
std::vector<ConstraintSpec> stageBConstraintPlan();

// =====================================================================
// 约束编排服务（§12.2 签名——OPT-03 的编排描述面；执行归管线）
// =====================================================================

/**
 * @brief 约束编排描述接口（§12.2 原文签名——阶段→约束集解析；阶段锁
 *        校验在此）。
 *
 * 错误语义（§12.3/§6.5）：阶段不支持约束 → OptimizationError
 * (OPT-STAGE-LOCKED)——**运行启动阻塞**，调用方（研究定义校验/Preflight
 * 面）呈现为阻塞横幅＋缺项清单（UX-10 同型），绝不静默降级、绝不呈现为
 * 候选淘汰或空结果（§6.5 呈现边界）。
 */
class IOptimizationConstraintProvider {
public:
    virtual ~IOptimizationConstraintProvider() = default;

    /**
     * @brief 返回阶段对应的约束执行清单（§6.1/§6.2/§6.3 顺序）。
     *
     * StageB → stageBConstraintPlan()（§6.2 R1 全序）；
     * StageD → 抛 OPT-STAGE-LOCKED——联合约束清单随 WP-21-T02 增量修订
     * 登记（P-04 冻结＋联合评估器落位为前置，卡 §13.2 表头），本任务不
     * 预建（DOPT-4 同口径：OPT-D 联合评估器不预登记）。
     *
     * @param stage [in] 优化阶段
     * @return 该阶段的约束执行清单（§6.2/§6.3 顺序；每次调用返回新值）
     *
     * @throws OptimizationError(kOptStageLocked) 阶段约束体系未启用/未登记
     *         （StageD——P-04 未冻结且联合清单未随 WP-21-T02 登记）
     *
     * 线程安全：只读纯函数面（可并发）。
     */
    virtual std::vector<ConstraintSpec>
    constraintsFor(OptimizationStage stage) const = 0;

    /**
     * @brief 研究定义约束激活集解析（§4.5"约束配置（激活集）→ config.opt"
     *        的解析面——StageB 引用联合约束即拒的执行点，OPT-VER-109~111）。
     *
     * 解析规则（检查序固定——确定性首错）：
     *   ① requested 中出现联合约束族 token（toToken(JointConstraintFamily)
     *     四值任一）而 stage==StageB → 抛 OPT-STAGE-LOCKED（不静默降级、
     *     不从清单中剔除——§6.5：拒绝即终止该研究启动）；
     *   ② requested 出现未知 token → 抛 OPT-INPUT-INVALID（调用方书写
     *     错误，fail-fast——与"登记了但阶段不支持"〔①〕语义分立，防
     *     AT-09 同型的语义混同）；
     *   ③ 其余（R1 词表内 token 的任意子集，含空表＝缺省全启用）→ 返回
     *     对应 R1 约束按 §6.2 顺序排列的子清单。
     *
     * @param stage            [in] 优化阶段
     * @param requestedTokens  [in] 研究定义引用的约束 token 集（R1 词表
     *                         ＋联合家族词表；顺序无关——输出恒按 §6.2 序）
     * @return 解析后的执行清单（§6.2 顺序）
     *
     * @throws OptimizationError(kOptStageLocked) StageB 引用联合约束族
     * @throws OptimizationError(kOptInputInvalid) 未知约束 token
     *
     * 线程安全：只读纯函数面（可并发）。
     */
    virtual std::vector<ConstraintSpec>
    resolveConstraintPlan(OptimizationStage stage,
                          const std::vector<std::string>& requestedTokens) const = 0;
};

/**
 * @brief 约束编排唯一产品实现（本卡 §12.2 O4 面；只读纯函数——实例
 *        无状态，进程级共享安全）。
 */
class OptimizationConstraintProvider final : public IOptimizationConstraintProvider {
public:
    /// @copydoc IOptimizationConstraintProvider::constraintsFor
    std::vector<ConstraintSpec>
    constraintsFor(OptimizationStage stage) const override;

    /// @copydoc IOptimizationConstraintProvider::resolveConstraintPlan
    std::vector<ConstraintSpec>
    resolveConstraintPlan(OptimizationStage stage,
                          const std::vector<std::string>& requestedTokens) const override;
};

// =====================================================================
// 约束事实与淘汰原因（§8.3 第 7/9 步——候选评估证据的编排素材；
// Profile 项 opt.hard-constraint-record 的数据面）
// =====================================================================

/**
 * @brief 单约束执行判定（§8.3 第 7 步 verdict 四值——满足/违例/不适用/
 *        数据不足）。
 *
 * 语义锚：Violated＝Must 级约束事实不成立（候选不可行素材）；Satisfied＝
 * 约束执行完成且无 Must 违例（Should 级警告不改变本值——进 shouldViolations
 * 素材）；NotApplicable＝约束对本次评估不在范围（如策略未启用碰撞——
 * V13-01 无布尔开关，会话不在场即未启用）；DataInsufficient＝约束计算
 * 不可靠（搜索未果/评估不完整——④级数据不足素材，绝不读作"通过"）。
 */
enum class ConstraintVerdict { Satisfied, Violated, NotApplicable, DataInsufficient };

/**
 * @brief 约束判定稳定 token（"satisfied"/"violated"/"not-applicable"/
 *        "data-insufficient"）——约束事实表/导出的确定性书写。
 * @param v [in] 判定枚举值
 * @return 稳定 token 视图（编译期字面量）
 */
std::string_view toToken(ConstraintVerdict v) noexcept;

/**
 * @brief 单约束执行事实（§8.3 第 7 步记录面——{constraintId, verdict,
 *        比较型数值（实际/要求/单位），依据评估键}的承载）。
 *
 * 比较型三要素（actualText/requiredText/unitSymbol）为**人读文本形态**
 * （ERR-01 逐项定位纪律——对象/上下文/原因/建议动作中的"上下文"层）；
 * 数值比较判定已由供给方（policy 发现/kin 评估器 verdictInputs）完成，
 * 本结构只如实记录，不做二次判定（PA-1）。
 *
 * 值语义；线程安全：纯值。
 */
struct ConstraintFact {
    ConstraintId constraintId = ConstraintId::InputPrecondition; ///< 约束（词表值）
    ConstraintVerdict verdict = ConstraintVerdict::Satisfied;    ///< 执行判定（四值）
    std::string evaluationKey;   ///< 依据评估键/依据标识（同 ConstraintSpec 注）
    std::string subject;         ///< 对象定位（绑定 token/关节/工况——ERR-01 定位面；
                                 ///  集合级事实为空串）
    std::string actualText;      ///< 比较型·实际（人读文本；非比较型为空）
    std::string requiredText;    ///< 比较型·要求（人读文本；非比较型为空）
    std::string unitSymbol;      ///< 比较型·单位符号（"rad"/"m"/"1"；非比较型为空）
    std::string detail;          ///< 中文说明（业务含义＋失败会走到哪——AGENTS §2.4）
};

/**
 * @brief 淘汰原因稳定 token（§8.3 第 9 步 reasonToken 词表——唯一书写点；
 *        值一经交付不得改动。词表边界：仅"淘汰"类原因——DataInsufficient/
 *        EvaluationFailed 是候选状态而非淘汰原因（§7.5 区分表：状态词
 *        不同，不互相冒充），不落 token）。
 */
/// 硬约束违例淘汰（Must 违例/拓扑拒绝——依据约束 token 定位）。
inline constexpr std::string_view kRejectHardConstraintViolated
    = "opt.reject.hard-constraint-violated";
/// 候选编译失败淘汰（§6.2 第 5 步；sourceId 携带透传的 RT-* 稳定码）。
inline constexpr std::string_view kRejectCandidateCompileFailed
    = "opt.reject.candidate-compile-failed";
/// Quick 保守淘汰·预算线（§8.4——Quick 批内目标序劣于幸存预算线的候选；
/// WP-20-T06 表尾追加。注意语义边界：只有 Quick-Feasible 候选才可能因
/// 预算线被筛为 ScreenedOut——Infeasible/DataInsufficient/EvaluationFailed
/// 保持其自有状态不冒充 ScreenedOut（§7.5 区分表——状态词不同）；Quick
/// 失败也不自动转确定性不可行（EvaluationFailed 独立，§8.4 表失败语义行））。
inline constexpr std::string_view kRejectQuickScreenedBudget
    = "opt.reject.quick-screen-budget";

/**
 * @brief 淘汰原因记录（§8.3 第 9 步——{阶段, 约束/评估器 ID, 原因 token,
 *        定位, 模式}；O10 逐候选逐约束记录）。
 *
 * 值语义；线程安全：纯值。
 */
struct RejectionReason {
    OptimizationStage stage = OptimizationStage::StageB; ///< 发生阶段
    std::string sourceId;      ///< 约束 token/评估键/编译器标识（定位依据）
    std::string reasonToken;   ///< 稳定 token（上方 kReject* 词表）
    std::string subject;       ///< 对象定位（绑定 token/关节——ERR-01）
    core::EvaluationMode mode = core::EvaluationMode::Verified; ///< 发生模式
                                                                ///  （Quick/Verified）
};

// =====================================================================
// 探针接缝（policy 唯一实现的域侧薄投影——O-37 裁决形态：生产适配器
// 在装配侧闭包，管线经接口消费、测试以可控替身注入）
// =====================================================================

/**
 * @brief 关节限位查询投影条目（policy::JointLimitSpec 的域侧机械投影
 *        ——字段一一同源，注释锚 policy 卡 §9.4）。
 *
 * 为什么投影而不直接消费 policy::JointLimitQuery：policy 的限位/碰撞
 * 评估实现翻译单元（JointLimits.cpp/CollisionQuery.cpp）仅在集成模式
 * 编译（policy 卡 §12 POL-T06 gating——冒烟模式无框架库可链），本单元
 * 产品面若直接调用其符号会在冒烟链接失败；以纯值投影接缝隔离符号依赖，
 * 两模式构建口径成立（runtime 冒烟分支只保证 rw 模板头路径）。投影是
 * 记录形态的一一同源承载（非算法复制——N4/R-POL-2 红线不涉）。
 *
 * 值语义；线程安全：纯值。
 */
struct JointLimitSpecRecord {
    std::string jointSubject;  ///< 关节对象 id 规范文本（"obj-<32hex>"——输出定位键，
                               ///  policy JointLimitSpec.jointObject 的文本形态）
    std::string localName;     ///< 局部名（显示辅助——不作身份，CON-06）
    std::optional<double> qmin; ///< 有限限位下端（SI rad（转动）/m（移动）——
                                ///  与 qmax 同有同无；continuous 关节无值）
    std::optional<double> qmax; ///< 有限限位上端（SI rad|m——同上）
    bool isContinuous = false; ///< continuous 类型（多圈关节——工程范围必填，
                               ///  行程上限检查豁免）
    /// continuous 必填有限工程工作范围（SI rad，first<second 双端有限）。
    std::optional<std::pair<double, double>> engineeringRange;
};

/**
 * @brief 关节限位探针查询（policy::JointLimitQuery 的域侧机械投影）。
 *
 * 值语义；线程安全：纯值。
 */
struct JointLimitProbeRequest {
    std::vector<JointLimitSpecRecord> joints;         ///< 候选关节限位表（jointSubject 唯一）
    /// 逐构型关节位置（SI rad|m；内层与 joints 等长同序；≥1 构型——
    /// 候选 bounds 值域/区间有效性的样本面）。
    std::vector<std::vector<double>> configurations;
};

/**
 * @brief 关节限位发现投影条目（policy::JointLimitFinding 的域侧机械投影
 *        ——比较型三要素逐字段同源，锚 policy 卡 §9.4 发现种类表）。
 *
 * 值语义；线程安全：纯值。
 */
struct JointLimitFindingRecord {
    std::string jointSubject; ///< 关节对象 id 规范文本（输出定位键）
    /// 发现种类 token（"interval-invalid"/"engineering-range-invalid"/
    /// "travel-limit-exceeded"/"near-limit"/"limit-violated"——policy
    /// JointLimitFindingKind 五值的稳定书写）。
    std::string kindToken;
    /// 规则级别 token（"must"/"should"——policy PolicyRuleLevel 投影；
    /// near-limit 恒 should，其余恒 must——policy §9.4 级别口径）。
    std::string levelToken;
    double actualValue = 0.0;    ///< 比较三要素·实际（单位随 kind——rad/无量纲）
    double thresholdValue = 0.0; ///< 比较三要素·阈值/期望值（同上）
    std::string unitSymbol;      ///< 单位符号（"rad"/"1"——比较型三要素第三元）
};

/**
 * @brief 关节限位探针结果（policy::JointLimitEvaluation 的域侧投影——
 *        终态性契约同源：completed==false＝评估级失败〔名称不可解析/
 *        位置非有限——环境面，候选 EvaluationFailed 素材〕，findings 为
 *        policy 输出稳定序的原样投影）。
 *
 * 值语义；线程安全：纯值。
 */
struct JointLimitProbeResult {
    bool completed = false;                          ///< policy 评估 Completed（终态）
    std::vector<JointLimitFindingRecord> findings;   ///< 发现投影（稳定序保持）
};

/**
 * @brief 关节限位探针接口（§6.2 第 7 步消费缝——生产适配器＝
 *        policy::JointLimitEvaluator::evaluate(query, policy, names) 的
 *        薄投影转发，阈值唯一来源装配绑定的 EngineeringPolicySet〔R-POL-5：
 *        接缝签名无阈值参数——结构上不可逐查询改判〕；装配随 P-OPT-2
 *        候选物化通道就绪，测试以可控替身注入）。
 *
 * 实现契约：同请求重复调用输出逐字段相等（policy 评估纯函数——NFR-COR-02）；
 * 实现不得缓存查询间状态（评估器无状态口径）——方法 const（评估无状态，
 * 并发只读可重入，policy IJointLimitEvaluator::evaluate 同款签名纪律）。
 */
class IJointLimitProbe {
public:
    virtual ~IJointLimitProbe() = default;

    /**
     * @brief 执行候选关节限位评估（§6.2 第 7 步——候选 bounds 值域/区间
     *        有效；行程上限策略校验的消费面——SA-15 确认流归命令边界，
     *        管线只记录发现不执行放行）。
     *
     * @param request [in] 查询投影（调用方持有；调用期间有效）
     * @return 评估结果投影（completed==false 时 findings 为已产生部分）
     */
    virtual JointLimitProbeResult
    probeJointLimits(const JointLimitProbeRequest& request) const = 0;
};

/**
 * @brief 静态碰撞构型级发现投影（policy::CollisionFinding 的 SampleSet
 *        域侧最小投影——构型级过滤记录所需的最小事实面）。
 *
 * 值语义；线程安全：纯值。
 */
struct StaticCollisionSampleFinding {
    std::size_t sampleIndex = 0; ///< 采样位置（configurations 下标，0 基）
    bool inCollision = false;    ///< 后端二值判定（相交/接触——policy §7.4；
                                 ///  间距不足〔非碰撞〕不在静态硬过滤范围，
                                 ///  适配器如实不产出本类条目）
    std::string pairText;        ///< 对象对定位文本（规范序 "objA|objB"——NFR-COR-05
                                 ///  对象 ID 对的可读形态，溯源辅助不作身份）
};

/**
 * @brief 静态碰撞探针结果（policy::CollisionEvaluation 的 SampleSet 域侧
 *        最小投影——终态性契约同源：completed==false＝评估失败/取消
 *        〔finalized==false——非终态输出不入正式证据，CON-04〕）。
 *
 * 值语义；线程安全：纯值。
 */
struct StaticCollisionProbeResult {
    bool completed = false; ///< 评估 Completed（终态——policy §6.3 状态机）
    std::vector<StaticCollisionSampleFinding> findings; ///< 构型级发现（policy §6.4 稳定序）
};

/**
 * @brief 静态碰撞探针接口（§6.2 第 10 步消费缝——生产适配器＝policy
 *        CollisionEvaluationSession::evaluate 的 SampleSet 形态薄转发：
 *        CollisionQuery{kind=SampleSet, configurations, pathParameters 空}，
 *        **不传阈值/模式参数**（R-POL-5——阈值唯一来源会话绑定策略集）；
 *        唯一实现归 policy（R-POL-2/N4——本单元零碰撞算法），装配随
 *        P-OPT-2 候选编译产物（WorkCell 场景）就绪；策略未启用碰撞时
 *        探针指针为空（V13-01：无布尔开关——会话不在场即未启用）。
 *
 * 构型参数用关节坐标投影（std::vector<double>，rad|m 逐轴——policy §6.6
 * setState 语义）而非 rw::math::Q 值：rw 头的 boost 序列化 include 链在
 * 独立冒烟模式不可达（框架链才有 boost），rw 值到 Q 的机械转换归生产
 * 适配器（Q 支持迭代器构造——一次翻译，零语义）。
 *
 * AT-19 三入口一致：kin 批量/覆盖入口与本入口共享同一 policy 会话形态
 * （SampleSet/SingleState 只是查询种类——同一实现、同一输出类型），一致性
 * 由 policy §6.4 输出逐字节一致契约与装配侧 manifest 摘要保障（CON-06）。
 */
class IStaticCollisionProbe {
public:
    virtual ~IStaticCollisionProbe() = default;

    /**
     * @brief 以 SampleSet 查询形态执行静态碰撞评估（§6.2 第 10 步）。
     *
     * @param configurations [in] 候选构型样本集（每样本＝关节坐标序列，
     *                       rad|m 逐轴——维度＝设备自由度；无序独立样本）
     * @return 评估结果投影（completed==false 时 findings 为已产生部分）
     */
    virtual StaticCollisionProbeResult probeSampleSet(
        const std::vector<std::vector<double>>& configurations) const = 0;
};

// =====================================================================
// 候选编译接缝（§6.2 第 5 步——P-OPT-2 裁决前接缝承载）
// =====================================================================

/**
 * @brief 候选编译结果（§6.2 第 5 步——同一编译链的事实投影）。
 *
 * 值语义；线程安全：纯值。
 */
struct CandidateCompileResult {
    bool ok = false;         ///< 编译成功（候选模型可评估）
    std::string stableCode;  ///< 失败时透传的 RT-* 稳定码（如 "RT-WC-COMPILE-FAILED"
                             ///  ——cause 链首环；成功恒空串；OPT-CANDIDATE-
                             ///  COMPILE-FAILED 是编排侧包装码，不占本字段）
    std::string detail;      ///< 中文说明（失败语义＋定位；成功为空）
};

/**
 * @brief 候选编译接口（§6.2 第 5 步消费缝——生产适配器＝runtime 编译链
 *        调用（补丁覆盖视图 CandidateDesignOverlay→候选对象字节→编译产物
 *        ），落位随 P-OPT-2 候选物化通道裁决（卡 §16.3 R-OPT-1：依赖
 *        modeling/runtime 侧接口增量未裁决）；裁决前测试以可控替身注入
 *        （成功/失败两轨）。管线不自行实现任何编译（"同一编译链"红线
 *        ——§6.2 第 5 步括注）。
 */
class ICandidateCompiler {
public:
    virtual ~ICandidateCompiler() = default;

    /**
     * @brief 编译候选补丁为可评估模型（§6.2 第 5 步）。
     *
     * @param patch    [in] 候选补丁（已过锁定/合法性检查——管线执行序保证）
     * @param bindings [in] 研究绑定集（补丁语义来源）
     * @return 编译事实（ok==false 时 stableCode 携带 RT-* 透传码）
     */
    virtual CandidateCompileResult
    compile(const CandidatePatch& patch, const std::vector<VariableBinding>& bindings) const = 0;
};

// =====================================================================
// 管线输入/输出（T04 子集——指标事实随 WP-20-T05、批量编排随 WP-20-T06）
// =====================================================================

/**
 * @brief 静态硬约束管线输入（单候选一个评估生命周期——§12.2
 *        CandidateEvaluationRequest 的 T04 承载形态）。
 *
 * 生命周期：调用方持有并保证 evaluate() 调用期间存活；snapshot/slice 为
 * 冻结不可变值（I-OPT-1 输入冻结——运行开始后不得混用修订）。
 * 值语义；线程安全：纯值（各调用持各自输入）。
 */
struct StaticHardConstraintInput {
    // ---- 候选描述 ----
    CandidateId candidateId;                    ///< 候选身份（内容寻址——§4.2）
    bool isBaseline = false;                    ///< 基线候选标记（空补丁候选——§8.2）
    CandidatePatch patch;                       ///< 候选补丁（锁定/合法性检查输入）
    std::vector<VariableBinding> bindings;      ///< 研究绑定集（补丁语义来源）
    core::EvaluationMode mode = core::EvaluationMode::Verified; ///< 评估模式
                                                                ///  （Quick/Verified——效力门禁在汇总层）

    // ---- 基线/候选事实投影（P-OPT-2 裁决前由调用方/测试供给——§16.3 R-OPT-1）----
    /// 基线链型在 R1 启用范围内（快照事实投影——拓扑约束输入；卡 §6.2 第 6 步
    /// "链型能力以快照事实为准：进入优化阶段即已通过建模链型门"——完整
    /// Preflight #15 检查归 WP-20-T08，本字段是其管线半区的事实入口）。
    bool baselineChainInEnabledScope = true;
    JointLimitProbeRequest jointLimits;         ///< 候选关节限位表＋构型样本（rad|m——
                                                ///  关节限位约束的查询投影）

    // ---- ③端口评估编排输入 ----
    evidence::AnalysisSnapshot snapshot;        ///< 冻结快照（aggregateVerdict 的覆盖矩阵
                                                ///  分母/清单绑定/固化校验事实面）
    evidence::InputSlice slice;                 ///< 冻结输入切片（评估请求切片——③端口）
    core::TaskIdentity task;                    ///< 评估请求任务五元组（③端口绑定面）
    std::vector<evidence::CaseId> caseSubset;   ///< 本批工况子集（覆盖矩阵跨批汇总的
                                                ///  本批分母投影；空＝不做工况分批）

    // ---- 汇总事实面（编排方跨批汇总产物——aggregateVerdict 的声明性输入；
    //      形状/对账校验在 aggregateVerdict 内部对快照执行，错配→②/④级）----
    evidence::CaseCoverageMatrix coverage;      ///< 必验工况覆盖矩阵（EVI-02 分母事实
                                                ///  ——漏验→②级门禁命中→不出正式通过）
    std::vector<evidence::RegionCoverageEvidence> regionCoverages; ///< 区域覆盖证据形状
                                                ///  （KIN-04 R8——零样本/降级→④级）
    evidence::EvidenceManifest manifest;        ///< 证据清单（Profile 绑定三元组＋逐项状态）
    std::optional<evidence::DeterministicInfeasibilityProof> proof; ///< 外来任务级不可行
                                                ///  证明（如有——③级素材；有效性校验在
                                                ///  aggregateVerdict 内部，D-09）
};

/**
 * @brief 候选评估记录（§12.2 CandidateEvaluationRecord 的 T04 承载子集
 *        ——约束事实/淘汰原因/判定映射；指标事实随 WP-20-T05 增补）。
 *
 * 值语义；线程安全：纯值。
 */
struct CandidateEvaluationRecord {
    CandidateId candidateId;                    ///< 候选身份（结果关联键——去重/合并锚）
    bool isBaseline = false;                    ///< 基线候选标记
    CandidateStatus status = CandidateStatus::Pending; ///< 候选状态（§7.5 判定流映射）
    core::EngineeringStatus engineeringStatus
        = core::EngineeringStatus::NotApplicable; ///< 工程判定（aggregateVerdict 输出——
                                                  ///  evidence 唯一产出，本管线只透传）
    evidence::VerdictTrace verdictTrace;        ///< 判定追溯（固定六槽——P-EV-3 字面顺序
                                                ///  的可核查证据）
    bool formalPassEligible = false;            ///< 正式通过资格（§7.2 五条件——
                                                ///  checkFormalPassEligibility 消费面）
    std::vector<std::string> formalPassUnmetConditions; ///< 未满足资格条件
                                                ///  （evidence 稳定 token 词表——
                                                ///  "漏一个启用必验工况不得出正式
                                                ///  通过"的观测面，AT-09 反例）
    std::vector<ConstraintFact> constraintFacts;     ///< 逐约束事实（§6.2 顺序）
    std::vector<RejectionReason> rejections;         ///< 淘汰原因（仅 Infeasible/
                                                     ///  EvaluationFailed 时非空——
                                                     ///  DataInsufficient 不是淘汰）
    std::vector<core::DiagnosticRecord> diagnostics; ///< 域诊断透传（kin 评估器输出；
                                                     ///  管线自身不新造诊断记录——
                                                     ///  语义经 facts/rejections 承载）
};

/**
 * @brief 管线依赖注入面（构造注入——L5 装配形态；全部非 owning 只读
 *        指针，调用方保证 evaluate() 期间存活）。
 *
 * 装配校验（管线构造期）：evaluators/jointLimitProbe/compiler/producers/
 * profiles 必须非空（缺失即构造抛 std::invalid_argument——装配违约
 * fail-fast）；collisionProbe 可空（策略未启用碰撞——V13-01 语义位）。
 */
struct StaticHardConstraintDeps {
    const evidence::EvaluatorRegistry* evaluators = nullptr; ///< ③端口注册表（find
                                                    ///  kin-*→create→evaluate——§6.7 内部消费链）
    const IJointLimitProbe* jointLimitProbe = nullptr; ///< 限位探针（policy 唯一实现投影）
    IStaticCollisionProbe* collisionProbe = nullptr;   ///< 碰撞探针（可空＝策略未启用）
    const ICandidateCompiler* compiler = nullptr;      ///< 候选编译（runtime 编译链接缝）
    const evidence::IProducerRegistryView* producers = nullptr; ///< aggregateVerdict
                                                    ///  的产生者注册表投影（D-09 证明校验面）
    const evidence::IProfileRegistryView* profiles = nullptr;   ///< aggregateVerdict 的
                                                    ///  Profile 注册表投影（清单绑定量）
};

// =====================================================================
// 静态硬约束先行管线（§6.2 R1 执行件——O7 编排的 T04 子集）
// =====================================================================

/**
 * @brief R1 静态硬约束先行管线（§6.2 十步执行序＋§7.5 判定流映射）。
 *
 * 执行序（每次 evaluate，固定）：
 *   1. 阶段能力检查（构造期 stage 定型；StageD 请求即抛 OPT-STAGE-LOCKED
 *      ——运行启动阻塞，§6.5：不是候选淘汰）；
 *   2. 变量边界和锁定检查（复用 validatePatchItems——OPT-VAR-LOCKED/
 *      OPT-PATCH-ILLEGAL 诊断轨 → 候选 Infeasible＋逐项淘汰原因）；
 *   3. 候选编译（compiler→CandidateCompileResult；失败 → 候选
 *      EvaluationFailed＋OPT-CANDIDATE-COMPILE-FAILED 包装＋RT-* 透传）；
 *   4. 拓扑（补丁结构不变式已含于第 2 步词表校验；本步执行基线链型
 *      事实检查——范围外 → mustViolations 素材〔OPT-TOPOLOGY-REJECTED〕）；
 *   5. 关节限位（jointLimitProbe→发现投影：Must 级发现→mustViolations
 *      素材；near-limit→shouldViolations；评估级失败→EvaluationFailed）；
 *   6. Must 工位可达（③端口 kin-task-points-batch：verdictInputs 直接
 *      透传为判定素材；searchRecord→DataInsufficient 凭据〔C5/C8——不判
 *      不可行〕；EvidenceError→EvaluationFailed 附域诊断）；
 *   7. Must 区域覆盖（③端口 kin-region-coverage：同第 6 步消费口径；
 *      覆盖不足的 Must 违例由 kin 判定面产出——OPT-VER-116）；
 *   8. 静态碰撞（探针在场时 SampleSet 评估：构型级发现**只记录过滤**，
 *      不进 mustViolations——C8 作用域规则；探针失败→EvaluationFailed；
 *      探针缺席→NotApplicable）；
 *   9. 判定汇总（组装 evidence::VerdictInput→aggregateVerdict→候选状态
 *      映射：EngineeringInfeasible→Infeasible；DataInsufficient→
 *      DataInsufficient；Feasible→Feasible；另经 checkFormalPassEligibility
 *      记录正式通过资格——漏验/空必验集绝不输出正式通过〔EVI-02/P-EV-7〕）。
 *
 * 三项静态指标计算（§6.2 第 11 步）不在本管线（WP-20-T05）；Quick 保守
 * 淘汰（ScreenedOut）与批量编排/取消（批边界粒度）随 WP-20-T06——本管线
 * 的取消查询经 ctx.cancellationRequested() 在③端口评估器内部生效（kin
 * 评估器批间协作取消），管线自身步骤为轻量纯编排，不设额外取消点（如实
 * 登记：单候选管线调用无中间取消语义，取消粒度=评估器批边界）。
 *
 * 确定性：同输入同 deps ⇒ 同记录（约束事实按 §6.2 序；判定消费 evidence
 * 纯函数面；无随机源——NFR-COR-02）。
 * 线程约束：实例构造后只读；evaluate 单线程调用（R1——卡 §6.7）。
 */
class StaticHardConstraintPipeline {
public:
    /**
     * @brief 构造（阶段定型＋依赖注入）。
     *
     * @param stage [in] 管线阶段（T04 只实现 StageB；StageD 构造不拒——
     *              联合管线未实现的事实留待 WP-21 落位，evaluate 时阶段锁
     *              拒绝以保证与 constraintsFor 同一码面）——当前调用方
     *              传 StageB 之外的值属预支语义，evaluate 即抛。
     * @param deps  [in] 依赖注入面（全部指针在 evaluate 期间存活）
     *
     * @throws std::invalid_argument deps 必填项为空（装配违约 fail-fast）
     */
    StaticHardConstraintPipeline(OptimizationStage stage, StaticHardConstraintDeps deps);

    /**
     * @brief 评估单候选（§6.2 十步执行序——类注释）。
     *
     * @param input [in] 候选评估输入（调用方持有；调用期间有效）
     * @param ctx   [in] 宿主调用上下文（③端口评估器消费其取消查询/进度/
     *              对象读取——协作取消粒度=kin 评估器批边界）
     * @return 候选评估记录（状态映射契约见类注释；绝不因约束失败抛异常
     *         ——候选级失败走结构化返回〔§7.5〕；启动阻塞面才抛）
     *
     * @throws OptimizationError(kOptStageLocked) 阶段锁（StageD 请求——
     *         §6.5 运行启动阻塞，不是候选淘汰）
     * @throws OptimizationError(kOptEvaluatorMissing) ③端口必需评估器未注册
     *         （装配不一致——§6.4 #7 同源码面；fail-fast）
     * @throws std::invalid_argument input 结构非法（candidateId 保留值/
     *         snapshot 与 slice 身份错配——调用方契约违约）
     *
     * 确定性：同输入同 deps ⇒ 同记录（NFR-COR-02）。
     */
    CandidateEvaluationRecord evaluate(const StaticHardConstraintInput& input,
                                       evidence::IEvaluationContext& ctx) const;

private:
    OptimizationStage m_stage;          ///< 管线阶段（构造定型）
    StaticHardConstraintDeps m_deps;    ///< 依赖注入面（构造后只读）
};

}  // namespace sdurws::ird::optimization

#endif  // SDURWS_IRD_OPTIMIZATION_CONSTRAINT_HPP
