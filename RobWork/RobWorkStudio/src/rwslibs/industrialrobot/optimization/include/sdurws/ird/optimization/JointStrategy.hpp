/**
 * @file   JointStrategy.hpp
 * @brief  分层联合策略（OPT-05——§8.5 外层—内层结构图的编排侧承载）：
 *         结构/传动外层探索（StageD 组合空间采样）＋轨迹/动力学校核与
 *         器件匹配内层反馈闭环（§6.3 R2 联合管线六层执行序）＋联合门面
 *         评估键 `opt-joint-full` 登记值与 `upstream.*` 依赖链切片身份面
 *         （P-OPT-9 消账——WP-21-T02 冻结的 §6.7 登记值落位）。
 *
 * 设计依据：
 *   - units/optimization.md §8.5（WP-21-T02 增量修订——分层联合策略实现
 *     口径落章：联合门面评估器与 opt-joint-full 评估键、upstream.* 依赖链
 *     四条、内层反馈闭环与边界、PA-7 无新机制核对锚）、§6.3（R2 约束执行
 *     顺序——六层调用序＋语义保证逐条）、§6.7（优化域评估器能力声明——
 *     opt-joint-full descriptor 登记值一次冻结）、§8.2（seeded-lhs 策略
 *     B/D 两阶段——"离散变量 R2 组合空间采样"；枚举变量组合枚举上限
 *     保护）、§5.6（R2 离散器件变量——值＝selection 组合空间合法成员；
 *     组合合法性归 sel.combination-check 内层判定，optimization 不自判）、
 *     §5.7（StageB 研究定义引用 R2 变量→OPT-STAGE-LOCKED；StageD 使用
 *     未注册轨迹/动力学/drivetrain/selection 评估器→OPT-EVALUATOR-MISSING）、
 *     §7.5（候选分类判定流——六种情形语义独立不互相冒充）、§8.3（评估
 *     编排第 4 步阶段能力检查/第 5 步失败取消语义/第 9 步淘汰原因）、
 *     §8.4（缓存三步走——门面评估键入缓存身份；mode-mismatch 双向
 *     Incompatible，D-13）
 *   - 需求 OPT-05（分层联合策略——结构/传动外层探索，轨迹动力学校核与
 *     器件匹配内层反馈）、OPT-03（联合硬约束——各域 Must 违例 ⇒ 候选
 *     Infeasible；逐域逐约束记录）
 *   - 任务契约 tasks/foundation/WP-21-T03.json acceptance；knownPitfalls
 *     P-OPT-9（联合评估键/upstream.* 依赖链——本批消账）＋O-37（runtime
 *     视图经宿主注入形态——候选物化通道裁决前的接缝纪律同 T04）
 *
 * ★ 落位范围口径（诚实登记，防扩大）：§14.2 行"WP-21-T03 → §8.5"——
 *   本任务交付三件事：①opt-joint-full 登记值（评估键/契约版本/payload
 *   kindToken/canonical magic 逐字一致——§6.7 冻结值）＋门面 descriptor
 *   构造器；②upstream.* 依赖链切片身份面（四条 UpstreamResult 条目进
 *   门面 sliceId——上游切片身份任一变更 ⇒ 门面 sliceId 变更 ⇒ evidence
 *   判 Incompatible 重算的机械前提，CON-04 当前性传播）；③内层反馈闭环
 *   （§6.3 六层执行序编排＋判定唯一归 evidence aggregateVerdict）＋
 *   StageD 外层组合空间采样。**不落**（T04 及后续批次）：完整并行评估
 *   与检查点恢复（§9.2——WP-21-T04）、联合硬约束全词表扩展执行件
 *   （WP-21-T04）、门面 evaluate() 的完整装配接线（候选物化通道 P-OPT-2
 *   裁决前的接缝形态见 IJointLayerSliceProvider；两半——门面身份面＋
 *   内层执行面——本批分别可测，装配闭环随 T04）、鲁棒性三模式
 *   （WP-21-T05——判定阈值两槽位待所有者补充确认，本批零判定结论）、
 *   搜索策略 ICandidateGenerator 接口扩展（WP-21-T06——本批 StageD
 *   生成器沿用 §8.2 已登记 seeded-lhs 机制的 R2 面，不新增接口族）。
 *
 * 背景说明（第一读者须知——三件事）：
 *   ① **判定与编排分离**（T04 头注②同源纪律）：候选 Feasible/Infeasible/
 *      DataInsufficient/EvaluationFailed 的最终判定唯一归 evidence::
 *      aggregateVerdict（§7.5 判定流）；本编排只做：六层按 §6.3 序派发、
 *      层事实收集（判定素材/搜索未果凭据/诊断透传）、联合汇总组装。
 *      评估器失败 ≠ 约束失败（LayerFailed → 候选 EvaluationFailed）；
 *      搜索未果 ≠ 确定性无解（searchRecord → DataInsufficient 素材，
 *      C5/C8）；两者绝不冒充 Infeasible（§7.5 区分表）。
 *   ② **上游身份即失效传播**（§8.5——CON-04）：四条 upstream.* 依赖把
 *      轨迹/动力学/drivetrain/selection 各层切片身份绑入门面 sliceId——
 *      任一层重算（切片身份变更）⇒ 门面 sliceId 变更 ⇒ evidence 按
 *      缓存判定判 Incompatible 重算（当前性传播消费，evidence §8.1）。
 *      本文件不复制任何失效判定（N8）——只保证"身份进切片"的机械前提。
 *   ③ **组合合法性零自判**（§5.6/N5）：StageD 外层采样的离散器件组合
 *      值域由调用方按 selection 契约声明（§8.5"组合空间经 selection
 *      契约"——管线①组合集提交面 config.dt-combo-set 的优化侧消费）；
 *      optimization 只做确定性采样与结构校验（引用文本非空），组合是否
 *      可行由内层 sel-combination-check 逐项判定（淘汰原因含
 *      thresholdSource——P-POL-3 未裁决维度输出"未判定"不整体阻断）。
 *
 * 线程约束：makeJointFacadeDescriptor/buildJointFacadeSlice/
 *   generateStageDJointCandidates 纯函数可重入；JointLayeredChecker 实例
 *   构造后只读、check 单线程调用（R2 编排口径——卡 §6.7 threadSafety=
 *   SingleThread：线程并行由 execution worker 编排掌控，多 worker 各持
 *   实例）。
 * 确定性：同（input, deps 行为）⇒ 同 JointLayeredCheckResult 结论面
 *   （层记录/判定素材/候选状态；取消前缀如实保留）；同 config＋组合集
 *   ⇒ 同候选批（外层采样 splitmix64 自持——跨平台可重放，NFR-COR-02）。
 */

#ifndef SDURWS_IRD_OPTIMIZATION_JOINT_STRATEGY_HPP
#define SDURWS_IRD_OPTIMIZATION_JOINT_STRATEGY_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <sdurws/ird/core/DiagData.hpp>     // core::DiagnosticRecord——域诊断透传载体
#include <sdurws/ird/core/Evaluation.hpp>   // core::EvaluationMode/EngineeringStatus——
                                              //  评估模式与工程判定词表
#include <sdurws/ird/core/Identity.hpp>     // core::ObjectId/ContentIdentity/
                                              //  TaskIdentity/RunId——身份面
#include <sdurws/ird/evidence/Evidence.hpp> // evidence::CaseCoverageMatrix/
                                              //  RegionCoverageEvidence/EvidenceManifest/
                                              //  DeterministicInfeasibilityProof/
                                              //  SearchExhaustedRecord/
                                              //  IProducerRegistryView——汇总事实面
#include <sdurws/ird/evidence/Evaluator.hpp>  // evidence::EvaluatorDescriptor/
                                              //  EvaluatorRegistry/IEngineeringEvaluator/
                                              //  IEvaluationContext——③端口与调用约定
#include <sdurws/ird/evidence/Slice.hpp>    // evidence::InputSlice/SliceBuilder/
                                              //  DependencyEntry——冻结切片（缓存键）
#include <sdurws/ird/evidence/Snapshot.hpp> // evidence::AnalysisSnapshot——冻结快照
#include <sdurws/ird/evidence/Verdict.hpp>  // evidence::DomainVerdictInputs/
                                              //  IProfileRegistryView——判定面
#include <sdurws/ird/optimization/CandidatePatch.hpp>  // CandidatePatch/CandidateId——
                                                        //  候选描述与身份链
#include <sdurws/ird/optimization/Constraint.hpp>      // CandidateEvaluationRecord/
                                                        //  RejectionReason——评估记录形态
#include <sdurws/ird/optimization/EvaluatorPorts.hpp>  // OptimizationConfiguration——
                                                        //  运行配置载荷（T06 落位头；
                                                        //  validateConfiguration 校验面）
#include <sdurws/ird/optimization/Types.hpp>           // OptimizationStage/
                                                        //  OptimizationError/诊断码——域异常面
#include <sdurws/ird/optimization/Variable.hpp>        // VariableBinding——研究绑定集

namespace sdurws::ird::optimization {

// =====================================================================
// 联合门面登记常量（§6.7 表 opt-joint-full 行——WP-21-T02 冻结登记值；
// OPT-VER-253"键名/契约/payload/magic 与登记值逐字一致"的唯一书写点）
// =====================================================================

/**
 * @brief 联合门面评估键（§6.7 登记值 "opt-joint-full"——kebab 词形满足
 *        isValidEvaluationKey；StageD 联合管线的聚合登记面）。
 */
inline constexpr std::string_view kOptJointFullKey = "opt-joint-full";

/**
 * @brief 联合门面输入/输出契约版本（§6.7 登记值 1；进 sliceId——CON-04）。
 */
inline constexpr std::uint32_t kOptJointFullContractVersion = 1U;

/**
 * @brief 联合门面输出载荷域 token（DomainPayload.kindToken；§6.7 登记值
 *        "opt.joint-full.v1"——词表归本域门面 descriptor 声明）。
 */
inline constexpr std::string_view kOptJointFullPayloadToken = "opt.joint-full.v1";

/**
 * @brief 联合门面 canonical 魔数（§6.7 登记值 "IRDOPTJ1"——门面域编码
 *        起始 8 字节；与 config.opt "IRDOPTC1"/补丁 "IRDOPTP1" 分域）。
 */
inline constexpr std::string_view kOptJointFullCanonicalMagic = "IRDOPTJ1";

// ---- upstream.* 依赖链 token（§6.7 登记值逐字——§8.5"取 upstream.<…>
// 形态"；依赖键词形允许点号（[a-z][a-z0-9.-]{2,63}），与评估键词形不同）----

/// 轨迹层上游依赖 token（§6.7 登记值；载荷 upstreamKey＝kInnerTrjPlanKey）。
inline constexpr std::string_view kUpstreamTrjPlanToken = "upstream.trj-sequence-plan";
/// 动力学层上游依赖 token（§6.7 登记值；载荷 upstreamKey＝kInnerDynRneaKey）。
inline constexpr std::string_view kUpstreamDynRneaToken = "upstream.dyn-rnea-analysis";
/// drivetrain 映射层上游依赖 token（§6.7 登记值；载荷 upstreamKey＝
/// kInnerDtMappingKey）。
inline constexpr std::string_view kUpstreamDtMappingToken = "upstream.dt.mapping";
/// selection 器件匹配层上游依赖 token（§6.7 登记值；载荷 upstreamKey＝
/// kInnerSelCombinationKey）。
inline constexpr std::string_view kUpstreamSelCombinationToken
    = "upstream.sel.combination-check";

// ---- 内层消费评估键（消费侧拼写常量——登记权与权威归各域单元卡；此处
// 只声明 ③端口 find() 的查找键。trj/dyn 与各域登记值逐字一致；dt/sel 为
// 各域已登记的 kebab 落位偏差〔drivetrain kMappingEvaluationKey＝
// "dt-mapping"、selection kCombinationCheckEvaluationKey＝
// "sel-combination-check"——卡面 "dt.mapping"/"sel.combination-check" 记
// 法与 evidence 评估键词形闸门冲突的偏差先例〕）----

/// 轨迹序列规划评估键（trajectory kTrjEvaluationKey 登记值）。
inline constexpr std::string_view kInnerTrjPlanKey = "trj-sequence-plan";
/// 动力学 RNEA 分析评估键（dynamics §9.1.1/O12 登记值）。
inline constexpr std::string_view kInnerDynRneaKey = "dyn-rnea-analysis";
/// drivetrain 工作点映射评估键（drivetrain kMappingEvaluationKey 落位值）。
inline constexpr std::string_view kInnerDtMappingKey = "dt-mapping";
/// selection 组合校核评估键（selection kCombinationCheckEvaluationKey 落位值）。
inline constexpr std::string_view kInnerSelCombinationKey = "sel-combination-check";

// ---- 内层管线步骤定位 token（§6.3 R2 联合管线调用序——JointLayerRecord
// .layerToken 的域词表；联合记录/审计/淘汰定位的稳定书写）----

inline constexpr std::string_view kJointLayerTrjQuick = "joint-layer.trj-quick";
inline constexpr std::string_view kJointLayerTrjVerified = "joint-layer.trj-verified";
inline constexpr std::string_view kJointLayerDynQuick = "joint-layer.dyn-quick";
inline constexpr std::string_view kJointLayerDynVerified = "joint-layer.dyn-verified";
inline constexpr std::string_view kJointLayerDtMapping = "joint-layer.dt-mapping";
inline constexpr std::string_view kJointLayerSelCombination = "joint-layer.sel-combination";

/**
 * @brief 内层编排追加的淘汰原因 token（评估器失败→候选 EvaluationFailed
 *        的层定位记录；§8.3 第 5 步"附域诊断 cause 链"的编排侧承载——
 *        域诊断原样透传 layer diagnostics，本 token 只作淘汰原因定位词）。
 *        WP-21-T03 表尾追加（单元卡增量修订登记——域内淘汰原因词表，
 *        非新架构机制，PA-7）。
 */
inline constexpr std::string_view kRejectJointLayerFailed = "opt.reject.joint-layer-failed";

// =====================================================================
// 联合门面 descriptor（§6.7 表 opt-joint-full 行——注册面构造器）
// =====================================================================

/**
 * @brief 构造联合门面评估器描述符（字段落值＝§6.7 登记值逐字；注册期
 *        校验由 evidence requireValidEvaluatorRegistration 承载——Profile
 *        "opt" 先于本 descriptor 注册，evidence §13 顺序约束）。
 *
 * 字段落值（逐字段出处——§6.7 表行原文）：
 *   - key＝kOptJointFullKey（"opt-joint-full"）；
 *   - contractVersion＝kOptJointFullContractVersion（1）；
 *   - inputs（八条）：
 *       {"config.opt", Configuration, Required}（stage-d canonical——§4.3）
 *       {"policy.resolved", Policy, Required}
 *       {"namemap", NameMap, Required}
 *       {"catalog.lock", Object, Required}（目录锁定版本引用——SEL-08/
 *         Preflight #12；键拼写对齐 selection kCatalogLockKey 同语义条目）
 *       {kUpstreamTrjPlanToken, UpstreamResult, Required}
 *       {kUpstreamDynRneaToken, UpstreamResult, Required}
 *       {kUpstreamDtMappingToken, UpstreamResult, Required}
 *       {kUpstreamSelCombinationToken, UpstreamResult, Required}
 *     （依赖必需性词表四形态 Object/Configuration/Policy/NameMap/
 *     UpstreamResult——evidence §4.2.1 既定协议，非新机制）；
 *   - profile＝{profileId:"opt", version:"1.0", contentIdentity:保留值}
 *     （域不可申报——evidence §9.5；kOptProfileId 既有常量同源）；
 *   - supportedModes＝{Quick, Verified}（D-13 双向不隐式升降级，与
 *     opt-static-screen 缓存互斥同型——§8.4）；
 *   - stateless＝true；threadSafety＝SingleThread（§6.7——线程并行由
 *     execution worker 编排掌控）。
 *
 * @return 描述符（值语义；registration 前经注册期校验——本函数不做校验，
 *         校验单点在 EvaluatorRegistry::registerEvaluator）
 *
 * 纯函数；线程安全（可重入）。
 */
evidence::EvaluatorDescriptor makeJointFacadeDescriptor();

// =====================================================================
// 上游引用与门面切片（upstream.* 依赖链——CON-04 失效传播的机械前提）
// =====================================================================

/**
 * @brief 单条上游结果引用（evidence §4.2.1 UpstreamResultRef 语法同源的
 *        编排侧承载——门面切片条目载荷，含 dependencyToken 定位）。
 *
 * 值语义；线程安全：纯值。
 */
struct JointUpstreamRef {
    std::string dependencyToken;              ///< 门面依赖键（upstream.* 四词表
                                              ///  之一——§6.7 登记值）
    std::string upstreamKey;                  ///< 内层评估键（trj-sequence-plan 等——
                                              ///  isValidDependencyKey 词形）
    core::ContentIdentity upstreamSliceId;    ///< 上游层评估切片身份（进门面
                                              ///  sliceId——CON-04）
    std::optional<core::RunId> upstreamRunId; ///< 上游运行身份（可选——跨运行
                                              ///  依赖的当前性语义，evidence §8.1）
};

/**
 * @brief 冻结联合门面评估请求切片（§8.5——门面是联合管线的聚合登记面；
 *        切片身份把 config.opt＋候选补丁身份＋四条上游层切片身份全部绑入
 *        ⇒ "上游切片身份任一变更 ⇒ 门面 sliceId 变更"的机械前提）。
 *
 * 切片条目（六条，构建序固定——SliceBuilder 冻结期 (kind,key) 字典序
 * 规范化，条目序不影响 sliceId）：
 *   1. "opt.config"（Configuration）＝config.opt canonical 字节＋内容身份
 *      （§4.3/§4.5 缓存键要素——同 T06 buildCandidateSlice 条目 1）；
 *   2. "opt.candidate-patch"（Environment）＝补丁身份 canonical 文本
 *      （§4.5——同 T06 条目 2）；
 *   3~6. 四条 UpstreamResult：key＝upstream.* token、载荷
 *      {upstreamKey, upstreamSliceId, upstreamRunId?}（evidence §4.2.1
 *      既有载荷类型 UpstreamResultDependencyPayload——零新编码层，N8）。
 *
 * 门面评估键 opt-joint-full＋契约版本 1 进 sliceId（setEvaluation——缓存
 * 判定第一/三要素）；来源快照身份由 builder 绑定（切片 ⊆ 快照）。
 *
 * @param snapshot  [in] 冻结快照（builder.build 的对账基准——切片身份为
 *                  builder 计算值，非申报值）
 * @param config    [in] 已校验运行配置（本函数防御复验——违例同
 *                  validateConfiguration 拒绝面）
 * @param patch     [in] 候选补丁（身份进切片条目）
 * @param upstreams [in] 上游引用集（恰好四条、dependencyToken 互异且都属
 *                  §6.7 四词表——违例抛 std::invalid_argument；调用方契约
 *                  违约 fail-fast。四层全 Completed 才可组装门面切片——
 *                  调用序由 JointLayeredChecker 保证）
 * @return 冻结切片（sliceId 为 builder 计算值——门面缓存身份）
 *
 * @throws OptimizationError(kOptInputInvalid) config 非法（同校验面）；
 *         std::invalid_argument upstreams 结构违约（条数≠4/token 重复/
 *         词表外 token/sliceId 保留值）
 *
 * 纯函数；线程安全（可重入）。
 */
evidence::InputSlice buildJointFacadeSlice(const evidence::AnalysisSnapshot& snapshot,
                                           const OptimizationConfiguration& config,
                                           const CandidatePatch& patch,
                                           const std::vector<JointUpstreamRef>& upstreams);

// =====================================================================
// 内层分层闭环（§8.5 内层校核——§6.3 六层执行序的编排侧实现）
// =====================================================================

/**
 * @brief 单层执行结局（§7.5 语义独立的编排侧承载——三种非完成结局绝不
 *        互相冒充：评估器异常/取消/超时＝LayerFailed（候选 Evaluation-
 *        Failed）；搜索未果＝SearchExhausted（候选 DataInsufficient 凭
 *        C5/C8）；两者都不是约束违例、都不产出"不可行"）。
 */
enum class JointLayerOutcome {
    Completed,      ///< 层评估正常完成（产出判定素材＋上游引用）
    LayerFailed,    ///< 层评估失败（EvidenceError——环境轨，I-C2）
    SearchExhausted ///< 搜索未果（SearchExhaustedRecord 在场——C5/C8 凭据）
};

/**
 * @brief 单层执行记录（§6.3 调用序一层的完整事实——逐域逐约束联合记录
 *        的分域承载；审计与淘汰定位的观测面）。
 *
 * 值语义；线程安全：纯值。
 */
struct JointLayerRecord {
    std::string layerToken;    ///< 管线步骤定位 token（kJointLayer* 六词表
                               ///  之一——§6.3 调用序锚）
    std::string evaluationKey; ///< 内层评估键（kInner* 四词表之一）
    core::EvaluationMode mode = core::EvaluationMode::Quick; ///< 本层请求模式
                              ///  （显式请求——D-13 不隐式升降级；轨迹/动力学
                              ///  两层各含 Quick 与 Verified 两条记录）
    JointLayerOutcome outcome = JointLayerOutcome::Completed; ///< 层结局
    std::optional<JointUpstreamRef> upstreamRef; ///< Completed 层产出（下游
                              ///  层与门面切片的引用锚；非 Completed 为空）
    evidence::DomainVerdictInputs verdictInputs; ///< 层判定素材（域产出 Must/
                              ///  Should 违例——联合硬约束汇总的逐域分账；
                              ///  PA-1 违例语义归域）
    std::vector<evidence::EvidenceItem> evidenceItems; ///< 层证据产出（域清单
                              ///  项原样聚合——联合证据清单 items 的层并集
                              ///  来源；presence 纪律由汇总层核对）
    std::optional<evidence::SearchExhaustedRecord> searchRecord; ///< 搜索未果
                              ///  凭据（SearchExhausted 结局——DataInsufficient
                              ///  素材，不判不可行）
    std::vector<core::DiagnosticRecord> diagnostics; ///< 域诊断透传（selection
                              ///  不吞上游诊断同款纪律——本编排零吞错）
};

/**
 * @brief 层评估请求切片供给接缝（③端口装配缝——P-OPT-2/O-37 裁决前的
 *        形态：每层评估请求的冻结切片由装配侧供给〔生产＝按各域 descriptor
 *        inputs 组装的适配器，随 T04 ③端口接线落位〕，测试以脚本化替身
 *        注入。与 T04 探针接缝/T05 指标投影/T06 投影缝同款四件套形态）。
 *
 * 为什么是接缝而非管线自建：各内层评估器 descriptor.inputs 的条目面
 * （catalog.lock 对象/dyn.joint-series 上游/config.* 条目等）依赖跨域
 * 载荷布线与快照事实投影，其装配语义归 L5 装配期（evidence §13 注册时
 * 序）；联合编排只消费"已冻结的层切片"——切片怎么组是装配的领域
 * （R-1：optimization 不实现 selection/drivetrain 的切片构造语义）。
 *
 * 实现契约：同（layerToken, evaluationKey, patch, bindings, upstreams）
 * 重复调用输出逐字段相等（切片冻结纯函数——NFR-COR-02）。
 */
class IJointLayerSliceProvider {
public:
    virtual ~IJointLayerSliceProvider() = default;

    /**
     * @brief 供给一层评估的冻结请求切片。
     *
     * @param layerToken   [in] 管线步骤定位 token（kJointLayer* 词表）
     * @param evaluationKey [in] 内层评估键（kInner* 词表）
     * @param patch        [in] 候选补丁（候选相关条目的语义来源）
     * @param bindings     [in] 研究绑定集（补丁语义来源）
     * @param upstreams    [in] 已完成层的上游引用（层间依赖——如 sel 层
     *                     切片需消费 dt 层产出身份；首层为空集）
     * @return 冻结切片（evaluationKey/契约版本须与层评估键一致——派发
     *         违约由内层评估器切片核对拦截）
     */
    virtual evidence::InputSlice layerSlice(const std::string& layerToken,
                                            const std::string& evaluationKey,
                                            const CandidatePatch& patch,
                                            const std::vector<VariableBinding>& bindings,
                                            const std::vector<JointUpstreamRef>& upstreams) const = 0;
};

/**
 * @brief 联合分层闭环依赖注入面（构造注入——L5 装配形态；全部非 owning
 *        只读指针，调用方保证 check() 期间存活）。
 *
 * 装配校验（构造期）：evaluators/layerSlices/producers/profiles 必须非空
 * （缺失即构造抛 std::invalid_argument——装配违约 fail-fast）。
 */
struct JointLayeredCheckDeps {
    const evidence::EvaluatorRegistry* evaluators = nullptr; ///< ③端口注册表
                                              ///  （find 内层键→create→evaluate——§6.7）
    const IJointLayerSliceProvider* layerSlices = nullptr; ///< 层切片供给缝
                                              ///  （P-OPT-2 裁决前替身面）
    const evidence::IProducerRegistryView* producers = nullptr; ///< aggregateVerdict
                                              ///  的产生者注册表投影（D-09）
    const evidence::IProfileRegistryView* profiles = nullptr;   ///< Profile 注册表
                                              ///  投影（清单绑定面）
};

/**
 * @brief 联合分层闭环请求（单候选一个内层闭环生命周期——§12.2
 *        CandidateEvaluationRequest 的联合承载形态）。
 *
 * 生命周期：调用方持有并保证 check() 调用期间存活；snapshot/coverage 为
 * 冻结不可变值（I-OPT-1 输入冻结）。
 * 值语义；线程安全：纯值（各调用持各自输入）。
 */
struct JointLayeredCheckInput {
    // ---- 候选描述 ----
    CandidateId candidateId;               ///< 候选身份（内容寻址——§4.2）
    bool isBaseline = false;               ///< 基线候选标记（空补丁——§8.2）
    CandidatePatch patch;                  ///< 候选补丁（锁定/合法性已由生成
                                            ///  侧 makeCandidatePatch 保证）
    std::vector<VariableBinding> bindings; ///< 研究绑定集（补丁语义来源）
    OptimizationConfiguration config;      ///< 已校验运行配置（门面切片的
                                            ///  config.opt canonical 来源——
                                            ///  §4.5 缓存键要素；本编排防御
                                            ///  复验，违例同校验面拒绝）
    core::EvaluationMode jointMode = core::EvaluationMode::Verified; ///< 联合
                              ///  请求模式（门面 supportedModes 两值之一；
                              ///  决定 dt/sel 两层的请求模式与汇总门禁面——
                              ///  Quick＝筛选语义/Verified＝正式比较面；
                              ///  轨迹/动力学两层各自 Quick→Verified 两级
                              ///  恒两级执行——§8.5 内层结构图固定序）
    // ---- ③端口评估请求绑定面 ----
    evidence::AnalysisSnapshot snapshot;   ///< 冻结快照（层评估与 aggregateVerdict）
    core::TaskIdentity task;               ///< 评估请求任务五元组（③端口绑定面）
    // ---- 汇总事实面（编排方冻结供给——与 T04 StaticHardConstraintInput
    //      同款声明性输入；形状/对账校验在 aggregateVerdict 内部）----
    evidence::CaseCoverageMatrix coverage; ///< 必验工况覆盖矩阵（EVI-02 分母）
    std::vector<evidence::RegionCoverageEvidence> regionCoverages; ///< 区域覆盖
                              ///  证据形状（KIN-04 R8；空表合法）
    std::optional<evidence::DeterministicInfeasibilityProof> proof; ///< 外来
                              ///  任务级不可行证明（如有——③级素材，D-09）
};

/**
 * @brief 联合分层闭环结果（§6.3 全序执行事实——层记录＋联合判定＋门面
 *        切片身份三面显式分离）。
 *
 * 值语义；线程安全：纯值。
 */
struct JointLayeredCheckResult {
    CandidateId candidateId = {};          ///< 候选身份（关联键）
    bool isBaseline = false;               ///< 基线候选标记
    core::ContentIdentity facadeSliceId = {}; ///< 门面切片身份（六层全
                              ///  Completed 时为 buildJointFacadeSlice 计算值
                              ///  ——门面缓存身份；提前终止路径为保留值，
                              ///  如实登记不作门面身份）
    bool canceled = false;                 ///< 协作取消标记（层间观测到取消
                              ///  即停止派发——已执行层前缀如实保留，
                              ///  TASK-01；未取消恒 false）
    bool allLayersCompleted = false;       ///< 六层全 Completed（联合判定与
                              ///  门面身份有效的前提——提前终止恒 false）
    CandidateEvaluationRecord record;      ///< 候选评估记录（§7.5 判定流映射
                              ///  ＋联合淘汰原因——复用 T04 记录形态，消费者
                              ///  零新类型；提前终止路径含已执行层事实）
    std::vector<JointLayerRecord> layers;  ///< 层执行记录（§6.3 调用序——
                              ///  逐域逐约束联合记录的分域分账）
};

/**
 * @brief 分层联合闭环检查器（§8.5 内层校核的编排侧实现——§6.3 六层执行
 *        序＋§7.5 判定流映射＋§8.5 门面切片身份绑定）。
 *
 * check() 执行序（固定——确定性）：
 *   1. 阶段能力检查（构造期 stage 定型；StageB 请求即抛 OPT-STAGE-LOCKED
 *      ——§5.7 运行启动阻塞，不是候选淘汰；§6.5 呈现边界同 T04）；
 *   2. 候选身份校验（保留值不可评估——调用方契约违约 fail-fast）；
 *   3. **六层执行**（§6.3 序，层定义见 kJointLayer* 词表）：
 *        trj Quick → trj Verified → dyn Quick → dyn Verified →
 *        dt.mapping（Verified）→ sel.combination-check（Verified）
 *      每层：
 *        a. ctx.cancellationRequested() 查询（层间协作取消——观测到即
 *           停止派发，canceled=true＋已执行层前缀保留）；
 *        b. ③端口 find(层评估键)→create()（未知键抛 kOptEvaluatorMissing
 *           ——装配不一致，§5.7 Preflight #7 同源码面 fail-fast）；
 *        c. 阶段能力检查（层请求 mode ∈ descriptor.supportedModes——§8.3
 *           第 4 步；违例抛 kOptStageLocked，D-13 不隐式升降级）；
 *        d. 层切片＝deps.layerSlices->layerSlice(...)（装配缝——派发违约
 *           由内层评估器切片核对拦截）；
 *        e. evaluate({task, mode, snapshot, slice, {}})：
 *           - 抛 EvidenceError（含取消派生异常）→ 层 LayerFailed → 候选
 *             EvaluationFailed＋kRejectJointLayerFailed 淘汰原因（sourceId
 *             定位层）＋域诊断透传——**后续层停止派发**（下游层缺上游
 *             事实，§6.3 线性序）；
 *           - searchRecord 在场 → 层 SearchExhausted → 候选 DataInsuffi-
 *             cient（C5/C8 凭 searchRecord——不判不可行）＋停止派发；
 *           - 正常 → 层 Completed（verdictInputs/searchRecord 空/诊断/
 *             upstreamRef{token,key,sliceId}），联合判定素材累积；
 *   4. **门面切片身份**（六层全 Completed）：buildJointFacadeSlice（四条
 *      upstream.* ＝trj Verified/dyn Verified/dt/sel 四层 sliceId）——
 *      上游身份进 sliceId（CON-04 失效传播机械前提）；
 *   5. **联合硬约束汇总**（§6.3 第 8 步——判定唯一归 evidence）：组装
 *      VerdictInput{outcome=Completed, mode=jointMode, 素材=六层
 *      verdictInputs 并集＋coverage/regionCoverages/manifest/proof} →
 *      aggregateVerdict → 候选状态映射（EngineeringInfeasible→Infeasible
 *      ＋逐违例淘汰原因〔sourceId＝层评估键＋违例项 id——逐域逐约束，
 *      O10〕；DataInsufficient→DataInsufficient；Feasible→Feasible）＋
 *      checkFormalPassEligibility（漏验/空必验集绝不输出正式通过——
 *      EVI-02/P-EV-7）。判定汇总诊断透传。
 *
 * 判定唯一性（N8/DOPT-1 同源）：本编排不复制任何判定逻辑——五级决策表
 * 全部消费 evidence 纯函数面；层 verdictInputs 是域产出（PA-1），编排只
 * 并集透传（不覆盖前序层事实——§6.3 域隔离：每层只写自己的分账）。
 *
 * 确定性：同（input, deps 行为）⇒ 同结果（层序固定/状态映射消费 evidence
 * 纯函数面/无随机源——NFR-COR-02）。取消为层间协作粒度（§12.2）。
 * 线程约束：实例构造后只读；check 单线程调用（卡 §6.7 SingleThread——
 * 线程并行由 execution worker 编排掌控）。
 */
class JointLayeredChecker {
public:
    /**
     * @brief 构造（阶段定型＋依赖注入）。
     *
     * @param stage [in] 编排阶段（本编排只实现 StageD；StageB 构造不拒、
     *              check 即抛——与 T04 管线的阶段锁同一码面）
     * @param deps  [in] 依赖注入面（指针在 check() 期间存活）
     *
     * @throws std::invalid_argument deps 必填项为空（装配违约 fail-fast）
     */
    JointLayeredChecker(OptimizationStage stage, JointLayeredCheckDeps deps);

    /**
     * @brief 执行单候选内层分层闭环（§6.3 六层序——类注释）。
     *
     * @param input [in] 冻结请求（调用方持有，调用期间有效）
     * @param ctx   [in] 宿主调用上下文（层间协作取消查询）
     * @return 闭环结果（候选级失败走结构化返回，绝不因层失败/约束违例抛
     *         异常——§7.5；启动阻塞面才抛）
     *
     * @throws OptimizationError(kOptStageLocked) 阶段锁（StageB 请求——
     *         §5.7 运行启动阻塞）
     * @throws OptimizationError(kOptEvaluatorMissing) ③端口必需评估器未
     *         注册（装配不一致——§5.7 StageD 使用未注册评估器行）
     * @throws OptimizationError(kOptStageLocked) 层请求模式不在层评估器
     *         supportedModes（阶段能力冲突——§8.3 第 4 步）
     * @throws std::invalid_argument input 结构违约（candidateId 保留值）
     *
     * 确定性：同输入同 deps ⇒ 同结果（NFR-COR-02）。
     */
    JointLayeredCheckResult check(const JointLayeredCheckInput& input,
                                  evidence::IEvaluationContext& ctx) const;

private:
    OptimizationStage m_stage;       ///< 编排阶段（构造定型）
    JointLayeredCheckDeps m_deps;    ///< 依赖注入面（构造后只读）
};

// =====================================================================
// StageD 外层探索（§8.5 外层——结构/传动探索的组合空间采样 R2 面）
// =====================================================================

/**
 * @brief 一条离散器件组合声明（§8.5 外层"离散器件组合（R2）"的提交面
 *        ——组合空间由调用方按 selection 契约供给〔管线①组合集
 *        config.dt-combo-set 的优化侧消费〕；optimization 零组合合法性
 *        自判——N5，可行性归内层 sel-combination-check）。
 *
 * 值语义；线程安全：纯值。
 */
struct StageDCombination {
    /// 组合内逐离散绑定赋值：(bindingId, discreteRef) 对——bindingId 须
    /// 对应 config.variables 中未锁定且授权的 DiscreteDevice 绑定；
    /// discreteRef＝离散器件规范引用（§5.6——modelId/DeviceCombinationId
    /// ＋目录身份的规范文本；非空）。对内 bindingId 不得重复。
    std::vector<std::pair<std::string, std::string>> bindings;
};

/**
 * @brief StageD 联合候选批生成（§8.5 外层探索的 R2 面——确定性种子采样：
 *        连续/量化/枚举维沿用 §8.2 seeded-lhs 机制口径＋离散器件组合维
 *        "组合空间经 selection 契约"声明值域；基线候选恒生成）。
 *
 * 算法（逐步——确定性 I-OPT-3/NFR-COR-02；随机性唯一来自自持 PRNG）：
 *   1. 防御复验：config 校验（同 validateConfiguration）＋stage==StageD
 *      （StageB 请求抛 OPT-STAGE-LOCKED——R2 采样面不服务 R1）；组合集
 *      内 bindingId 重复/引用绑定不存在/类别非 DiscreteDevice/绑定锁定
 *      或未授权/引用文本空——任一违例抛 OPT-INPUT-INVALID（结构校验；
 *      语义合法性不在此列——N5）；
 *   2. 采样维度集：config.variables 中未锁定且授权的 Continuous/Quantized/
 *      Enumeration 绑定（bindingId 字典序——与 T06 同款确定性维度序）
 *      ＋离散组合维（组合集非空时一个维度）；
 *   3. 非基线候选数 n = budget.maxCandidates − 1；
 *   4. 自持 splitmix64 PRNG（初值＝seed——与 T06 同款黄金常数与均匀数
 *      变换；跨平台可重放）；
 *   5. 连续/量化/枚举维：与 T06 同款拉丁超立方层置换（每维独立 n 层
 *      Fisher-Yates 洗牌）；层内取值公式与 T06 逐字同源（连续层内均匀
 *      点/量化采样域收缩 [lower+step, upper−step] 后对齐/枚举均匀取
 *      下标）；
 *   6. 离散组合维：组合集先按声明序规范化（逐组合按 bindingId 字典序
 *      排序；组合间按 (首 bindingId, 引用文本) 字典序排序——确定性组合
 *      序），候选 i 经组合维层置换取组合下标（对组合数取模——组合枚举
 *      上限保护：空间上界＝声明组合数，无枚举爆炸面）；逐组合的
 *      (bindingId, discreteRef) 对产出补丁离散项；
 *   7. 每候选经 makeCandidatePatch 构造（补丁 canonical 唯一编码层——
 *      排序/规范化/量化对齐全复用；构造抛出按调用方错误 fail-fast
 *      传播，不跳过不吞）；
 *   8. 返回批：首元素恒为基线空补丁（label "baseline"），随后 n 个采样
 *      候选（label "cand-<i>"——label 不参与身份，I-OPT-8）。
 *
 * 确定性：同 config＋同组合集 ⇒ 同批（逐字节同补丁序列——PRNG 自持＋
 * 层置换确定性＋组合序规范化＋浮点运算 IEEE 确定）。
 * 组合集为空：离散组合维缺席，批退化为连续/量化/枚举采样候选（基线＋
 * n 个）——研究无组合空间时预算自然退化，如实不伪造多样性。
 *
 * @param config       [in] 已校验配置（本函数防御复验）
 * @param combinations [in] 声明组合空间（§8.5 外层——selection 契约供给；
 *                      可空＝无离散组合维）
 * @return 候选补丁批（首元素基线；长度 = min(maxCandidates, 1+n)）
 *
 * @throws OptimizationError(kOptStageLocked) StageB 请求（阶段锁）；
 *         kOptInputInvalid 配置/组合集结构非法；kOptPatchIllegal 补丁
 *         构造拒绝（研究定义与词表值域矛盾——fail-fast）
 *
 * 纯函数；线程安全（可重入——无共享状态）。
 */
std::vector<CandidatePatch> generateStageDJointCandidates(
    const OptimizationConfiguration& config,
    const std::vector<StageDCombination>& combinations);

}  // namespace sdurws::ird::optimization

#endif  // SDURWS_IRD_OPTIMIZATION_JOINT_STRATEGY_HPP
