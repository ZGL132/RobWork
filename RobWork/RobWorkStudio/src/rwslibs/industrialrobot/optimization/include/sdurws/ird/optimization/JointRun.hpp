/**
 * @file   JointRun.hpp
 * @brief  StageD 联合运行编排面（OPT-03/06 D 部分——§8.5 外层两级批量编排
 *         ＋§9.2 完整并行评估与检查点恢复＋§9.2 Paused 态消费〔WP-21-T07〕）：
 *         八项指标全量计算（D-only 五项经层证据投影接缝）、确定性候选分批、
 *         并行评估与稳定合并、批边界检查点中间状态（域自编 codec，magic
 *         IRDOPTC1 检查点域）与续跑统计不重复（AT-35）、批边界暂停/继续
 *         （暂停确认边界＝批边界检查点；继续经 resumeFrom 不重复统计——
 *         V12-06）。任务 WP-21-T04；暂停/继续增量任务 WP-21-T07。
 *
 * 设计依据：
 *   - units/optimization.md §6.3（R2 联合管线——内层六层执行序＋联合硬
 *     约束汇总，执行件归 JointStrategy.hpp JointLayeredChecker〔WP-21-T03
 *     交付〕，本编排只做批量推进）、§7.1 行 4~8（D-only 五项指标的唯一
 *     数据来源口径：节拍＝trj 层 TimeParameterization.totalDurationS、
 *     器件成本/质量＝selection 层事实、正机械功＝dyn 层
 *     PowerEnergySummary.positiveEnergyJ、驱动裕量＝selection 裕量事实）、
 *     §7.2（缺失即"—"不显示零；非有限→候选评估失败不静默转 0——
 *     NFR-COR-03）、§7.4（缺失指标候选不参与支配比较）、§8.3 第 11 步
 *     （多候选批结果按 CandidateId 合并去重）、§8.4（Quick 保守筛选规则
 *     ——基线恒占席＋稳定序补位；两批记录效力边界）、§8.5（外层两级批量
 *     编排——T03 落位登记注⑥「归 T04」面）、§9.2（并行分片——R2 由
 *     execution 池化 worker 并行执行候选批，worker 内并行线程数来自
 *     config.opt 入身份；检查点——域中间状态〔已完成候选批索引/待评估
 *     队列/当前代计数/审计计数器〕编码为 intermediateStateRef，域自编
 *     codec magic IRDOPTC1，checkpointGranularity=Batch，
 *     completedUnits/totalUnits＝候选批计数〔续跑统计基点——继续不重复
 *     统计〕；恢复语义——恢复后新 AttemptId，不兼容检查点必须拒绝〔P-EX-9
     *     宁可重算不可错续〕，检查点恢复不等于优化成功）、§9.3（候选完成
 *     状态与统计——completedUnits/totalUnits 候选批计数从检查点累计，
 *     恢复不重复统计 AT-35）、§4.3/DOPT-10（检查点 codec magic IRDOPTC1
 *     ——与 config.opt canonical 同名异域：config.opt 字节流 vs 检查点
 *     存储格式，EvaluatorPorts.hpp 编码布局注同源登记）
 *   - 需求 OPT-03（联合硬约束——各域 Must 违例 ⇒ Infeasible 逐域逐约束
 *     记录）、OPT-06 D 部分（完整并行评估与检查点恢复）、NFR-COR-02
 *     （同输入同版本同配置同线程数同种子 ⇒ 等价候选集合与稳定排序——
 *     并行合并与线程数无关；并行归约满足附录 D 第 8 项相对 1×10⁻¹² 逐
 *     元素——WP-23-T07 基准达标不归本卡，本编排以确定性合并保证容差下
 *     的等价性）、AT-35（并行与检查点恢复统计不重复）
 *   - 任务契约 tasks/foundation/WP-21-T04.json acceptance（轨迹/动力/器件
 *     联合硬约束＋完整并行评估与检查点恢复；并行与检查点恢复统计不重复
 *     AT-35；八项指标全量可算用例通过）
 *
 * ★ 落位范围口径（诚实登记，防扩大）：§14.2 行「WP-21-T04 → §6.3/§9.2」
 *   ——本任务交付四件事：①StageD 联合约束清单执行件（Constraint.hpp
 *   表尾追加八值＋stageDConstraintPlan——另一提交）；②八项指标全量可算
 *   （D-only 五项经 JointMetricFacts 投影接缝——P-OPT-2 裁决前由调用方/
 *   测试供给，同 T05 StaticMetricFacts 形态）；③完整并行评估（候选批跨
 *   线程分片＋按批序稳定合并——进程内编排面；execution 池化 worker 进程
 *   面的派发治理〔maxWorkers/资源预算〕归 execution，本编排消费其配置语
 *   义不复制进程治理——N8/N13）；④检查点域中间状态 codec＋恢复消费
 *   （续跑统计不重复 AT-35；检查点**磁盘写出/装载归 execution
 *   ICheckpointCoordinator**——本域只产中间状态字节、消费恢复规格，
 *   N8）。**不落**（后续批次，如实登记）：鲁棒性三模式复核（WP-21-T05
 *   ——判定阈值两槽位待所有者补充确认，本批零判定结论零阈值数值）、
 *   误淘汰审计（WP-21-T08）、门面评估键缓存消费的
 *   完整命中链（结构性依赖：门面 sliceId 由四条 upstream.* 内层切片身份
 *   构成〔§8.5〕，六层完成前不存在——缓存查找先于评估在联合门面键上
 *   结构不可能；命中面留待装配批次随 CON-04 写入双门槛链落位，本编排
 *   审计计数位保留、如实为零查找不伪造命中）。
 *
 * ★ WP-21-T07 增量落位（2026-10-14——Paused 态消费，§9.2/AT-35/V12-06）：
 *   上列 T04"不落"清单中的「暂停/继续 Paused 态消费」由本批兑现——
 *   ①IPauseSignal 暂停查询接缝（deps 注入，nullptr＝暂停面未装配、编排
 *   永不暂停——R1 面零影响）；②批边界暂停消费（批间查询同取消位次，
 *   取消优先：取消与暂停同时请求时取消胜出——§7.4 协作取消强于暂停）；
 *   ③暂停确认边界＝暂停生效批边界产出的检查点状态（checkpointStates
 *   末份——宿主写出并确认后任务态 Paused 归 execution T10，本编排产出
 *   RunPhase::Paused）；④继续＝resumeFrom 重入（既有 T04 恢复机制零改
 *   ——继续不重复统计 AT-35 由审计基点＋批索引跳过承载）；⑤不改变输入
 *   快照（I-OPT-4：恢复绑定原 request——snapshot/slice/config 不入检查
 *   点、不入恢复面）。任务侧 requestPause/requestResume 编排（能力门控/
 *   暂停中取消）落 execution TaskController——同批交付（WP-21-T07 单元
 *   ＝optimization＋execution）。
 *
 * 背景说明（第一读者须知——三件事）：
 *   ① **编排与执行分离**（N8/N13 同源）：候选批的进程间派发、worker 生
 *      命周期、资源治理归 execution；本编排在进程内把候选分批、并行调
 *      度线程（每线程持各自 JointLayeredChecker——卡 §6.7 threadSafety=
 *      SingleThread「线程并行由 execution worker 编排掌控，多 worker 各
 *      持实例」的编排侧兑现）、按批序稳定合并。并行度＝
 *      config.parallel.threadCount（入 config.opt canonical 进 sliceId
 *      ——KIN-13 同型「改线程数＝新身份＝重算」）。
 *   ② **确定性合并**（NFR-COR-02/AT-35 前提）：候选批划分由 config 确定
 *      （与线程数无关的固定批容量），批内候选按生成序串行推进，批间结果
 *      按批序合并——并行执行只改变墙钟耗时，绝不改变记录顺序/集合内容/
 *      审计计数；同请求同 deps ⇒ 与串行逐字节同结论面（等价集合＋稳定
 *      排序；附录 D 第 8 项容差内的归约等价由"逐候选独立评估＋序位合并"
 *      的零浮点重排结构直接保证）。
 *   ③ **检查点只产状态、不碰磁盘**（N8）：批边界产出的 JointCheckpoint
 *      State 是**域中间状态值**——宿主（execution 侧派发编排）负责把它
 *      编入 CheckpointBatchFile 经 ICheckpointCoordinator 写出（磁盘编址
 *      ／manifest 发布／摘要全归 execution/project）；恢复侧宿主经
 *      judgeCheckpointCompatibility 判定通过后把中间状态字节交回本编排
 *      （resumeFrom）——已完成的候选批不重评、审计计数自检查点基点累计
 *      （AT-35：恢复不重复统计）；检查点恢复≠优化成功——恢复后仍走完整
 *      接纳与汇总（§9.2）。
 *
 * 线程约束：computeJointMetrics/encode/decodeJointCheckpointState 纯函数
 *   可重入；JointRunOrchestrator 实例非线程安全——run() 仅编排线程串行
 *   调用（run 内部按 threadCount 派生 worker 线程并 join）；deps 依赖面
 *   在并行路径下必须并发只读安全（EvaluatorRegistry/EvidenceProfile
 *   Registry 查询共享锁已保证；IJointLayerSliceProvider 与
 *   IJointMetricProjector 的实现方负责并发安全——生产适配器为无状态纯
 *   投影，测试替身需自持互斥）。IEvaluationContext 与 IPauseSignal 在并行
 *   路径下被多 worker 并发查询 cancellationRequested/pauseRequested——
 *   实现方须并发安全（生产＝execution 提供的原子取消/暂停旗标）。
 * 确定性：同（request, deps 行为）⇒ 同结论面（候选集/状态/幸存集/Pareto
 *   排序/审计计数——与 threadCount 无关；检查点状态序列同批界同值——
 *   NFR-COR-02）。
 */

#ifndef SDURWS_IRD_OPTIMIZATION_JOINT_RUN_HPP
#define SDURWS_IRD_OPTIMIZATION_JOINT_RUN_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sdurws/ird/core/Identity.hpp>         // core::ObjectId/ContentVersion/
                                                //  ContentIdentity/TaskIdentity——身份面
#include <sdurws/ird/evidence/Evidence.hpp>     // evidence::CaseCoverageMatrix/
                                                //  RegionCoverageEvidence——覆盖事实面
#include <sdurws/ird/evidence/Snapshot.hpp>     // evidence::AnalysisSnapshot——冻结快照
#include <sdurws/ird/evidence/Evaluator.hpp>    // evidence::IEvaluationContext——调用约定
#include <sdurws/ird/optimization/CandidatePatch.hpp>  // CandidatePatch/CandidateId
#include <sdurws/ird/optimization/EvaluatorPorts.hpp>  // OptimizationConfiguration/
                                                //  StaticMetricFacts/TwoStageRunRecord/
                                                //  ParetoFrontResult/TwoStageRunAuditCounts
                                                //  ——复用既有记录形态（消费者零新类型面）
#include <sdurws/ird/optimization/JointStrategy.hpp>   // JointLayeredChecker/
                                                //  JointLayeredCheckDeps/
                                                //  JointLayeredCheckInput/
                                                //  JointLayeredCheckResult/
                                                //  JointLayerRecord/StageDCombination
                                                //  ——T03 内层闭环（单候选执行件）
#include <sdurws/ird/optimization/Objective.hpp>       // StaticMetricResult——八项载体
#include <sdurws/ird/optimization/Types.hpp>    // OptimizationStage/CandidateStatus/
                                                //  RunPhase/OptimizationError——词表与异常

namespace sdurws::ird::optimization {

// =====================================================================
// StageD 联合指标事实投影（§7.1 行 4~8 数据来源的唯一接缝——P-OPT-2
// 裁决前由调用方/测试供给，同 T05 StaticMetricFacts 形态）
// =====================================================================

/**
 * @brief D-only 五项指标的候选事实投影（§7.1 行 4~8「数据来源（唯一口径）」
 *        列的编排侧承载——联合层证据到指标计算的投影缝）。
 *
 * 为什么是投影而非直读域载荷：五项指标的数据来源分属轨迹/动力学/
 * selection 三域评估器输出（域 canonical 载荷，evidence 视为不透明字节
 * ——R-1 禁止本单元 include 业务域头解析其结构）；提取语义归各域登记的
 * 载荷编码，optimization 只消费"投影后的 SI 数值事实"。生产适配器＝层
 * 证据载荷解码的机械翻译（随 P-OPT-2 候选物化通道落位），裁决前由调用
 * 方/测试供给（T05 同款接缝纪律）。
 *
 * 缺失语义（§7.1「缺失语义」列逐行）：任一事实 nullopt ⇒ 该指标"—"
 * （SourceMissing——D 无时间参数化/无可行组合/成本项缺失/降级标记），
 * 绝不输出 0 冒充（NFR-COR-03）；数值非有限 ⇒ NonFiniteInput（§7.2：
 * 指标值非有限→该候选评估失败处理——编排侧映射候选状态，计算面只如实
 * 标记）。
 *
 * 值语义；线程安全：纯值。
 */
struct JointMetricFacts {
    /// 节拍（s——trj 层 payload trj.sequence-plan.v1 的
    /// TimeParameterization.totalDurationS 投影，含驻留；§7.1 行 4）。
    std::optional<double> cycleTimeS{};
    /// 器件成本（目录货币单位随字段字典——sel 层 SEL-06 可行组合成本事实
    /// 投影；§7.1 行 5；货币口径未冻结归 P-OPT-5，投影只携数值）。
    std::optional<double> deviceCost{};
    /// 器件质量（kg——sel 层 MotorCatalogEntry/GearboxCatalogEntry.mass
    /// 合成投影；§7.1 行 6）。
    std::optional<double> deviceMassKg{};
    /// 关节侧正机械功（J——dyn 层 payload dyn.rnea-analysis.v1 的
    /// PowerEnergySummary.positiveEnergyJ 投影，E⁺＝∫max(P,0)dt 完整循环
    /// 含驻留；§7.1 行 7）。
    std::optional<double> jointPositiveWorkJ{};
    /// 最小驱动裕量（无量纲——sel 层工作点对能力余量事实投影；§7.1 行 8；
    /// 比值定义未冻结归 P-OPT-5，投影只携数值）。
    std::optional<double> minDriveMargin{};
};

/**
 * @brief 单候选联合事实投影（静态三项＋D-only 五项——八项全量的输入面）。
 *
 * 值语义；线程安全：纯值。
 */
struct JointCandidateFacts {
    StaticMetricFacts staticFacts;  ///< 前三项事实（§7.1 行 1~3——T05 形态）
    JointMetricFacts jointFacts;    ///< D-only 五项事实（§7.1 行 4~8——见上）
};

/**
 * @brief 候选联合事实投影接口（§8.3 编排第 3 步投影缝的 StageD 面——
 *        P-OPT-2 裁决前由测试以脚本化替身注入；生产适配器＝候选物化通道
 *        ＋层证据载荷解码的机械投影）。
 *
 * 实现契约：同补丁重复调用输出逐字段相等（投影纯函数——NFR-COR-02）；
 * 并行路径下实现必须并发安全（多 worker 线程同时调用——见文件头线程
 * 约束）。
 */
class IJointMetricProjector {
public:
    virtual ~IJointMetricProjector() = default;

    /**
     * @brief 投影候选的八项指标事实（静态三项＋D-only 五项）。
     *
     * @param patch    [in] 候选补丁（调用方持有，调用期间有效）
     * @param bindings [in] 研究绑定集（补丁语义来源）
     * @return 投影事实（值语义）
     */
    virtual JointCandidateFacts project(const CandidatePatch& patch,
                                        const std::vector<VariableBinding>& bindings) const = 0;
};

/**
 * @brief 八项指标全量计算（§7.1 全表——WP-21-T04 落位的 StageD 面；
 *        纯函数）。
 *
 * 计算规则（逐指标——缺失即"—"，语义与 computeStaticMetrics 同源）：
 *   1~3. 尺寸包络/结构质量/最小关节裕量：委托 computeStaticMetrics
 *        （T05 单点零漂移——同一实现同一缺失语义）；
 *   4~8. 节拍/器件成本/器件质量/正机械功/最小驱动裕量：
 *        - stage==StageB → 保持 StageNotComputable（§7.1「阶段可算」列
 *          ——StageB 恒"—"，不估算不预建）；
 *        - stage==StageD → 事实有值且有限 ⇒ 值；事实有值但非有限 ⇒
 *          NonFiniteInput（§7.2 不静默转 0——候选级后果由编排侧映射）；
 *          事实缺失（nullopt）⇒ SourceMissing（"—"——§7.1 缺失语义列，
 *          绝不输出 0 冒充）。
 *
 * 与 computeStaticMetrics 的关系：本函数是其 StageD 全量扩展——前三项
 * 复用、后五项补算；StaticMetricResult.partialMarginDataInsufficient
 * 素材标志原样透传（§7.1 行 3 编排联动面，PA-1 候选状态判定仍归管线）。
 *
 * @param stage [in] 优化阶段（StageB ⇒ 后五项恒 StageNotComputable）
 * @param facts [in] 候选联合事实投影（调用方持有；调用期间有效）
 * @return 八项全量结果（metrics 按 MetricId 枚举序——候选表八列固定序）
 *
 * 确定性：同 (stage, facts) ⇒ 同结果（纯函数，NFR-COR-02）。
 * 线程安全：纯函数（可并发）。
 */
StaticMetricResult computeJointMetrics(OptimizationStage stage,
                                       const JointCandidateFacts& facts);

// =====================================================================
// 检查点域中间状态（§9.2/DOPT-10——域自编 codec，magic IRDOPTC1 检查点
// 域；Batch 粒度；续跑统计基点）
// =====================================================================

/**
 * @brief 联合检查点中间状态 codec 格式版本（v1；codec 变更 ⇒ 新版本号
 *        ⇒ judgeCheckpointCompatibility 判 Incompatible 拒绝恢复——P-EX-9
 *        「旧版本检查点不迁移，宁可重算不可错续」；本域不私设迁移）。
 */
inline constexpr std::uint32_t kJointCheckpointCodecVersion = 1U;

/**
 * @brief 联合运行检查点域中间状态（§9.2「优化域中间状态」四要素的字段
 *        化——已完成候选批索引/待评估队列/当前代计数/审计计数器）。
 *
 * 生命周期：编排器在**每个候选批完成边界**产出一份（批边界＝暂停确认
 * 边界素材——AT-35/V12-06）；宿主编码为 CheckpointBatchFile 字节经
 * execution 写出；恢复时解码回本结构（resumeFrom）。批索引语义：候选批
 * 是确定性候选序列的固定容量划分（批容量见 JointRunOrchestrator 类注）
 * ——同 config ⇒ 同划分，批索引跨运行稳定（恢复跳过的判据）。
 *
 * 值语义；线程安全：纯值（构造后按只读对待）。
 */
struct JointCheckpointState {
    std::uint32_t codecVersion = kJointCheckpointCodecVersion; ///< codec 格式版本
                                                                ///  （进字节头——恢复兼容第三条件）
    core::EvaluationMode phase                                 ///< 已推进到的批相
        = core::EvaluationMode::Quick;                         ///  （Quick=筛选批相/
                                                                ///  Verified=复核批相——
                                                                ///  恢复自该相继续）
    std::vector<std::uint32_t> completedBatchIndices;  ///< 已完成候选批索引
                                                        ///  （升序——恢复跳过判据）
    std::vector<std::uint32_t> pendingCandidateIndices; ///< 待评估候选下标
                                                        ///  （生成序去重后的候选序列
                                                        ///  下标，升序——§9.2 待评估队列）
    std::uint32_t generation = 0;              ///< 当前代计数（§9.2——单代编排恒 0
                                                ///  ＋1 完成；R2 多代搜索未落位，
                                                ///  字段按卡面四要素保留）
    std::uint64_t completedUnits = 0;          ///< 已完成候选批计数（CheckpointRecord
                                                ///  .completedUnits 的域侧基点——
                                                ///  §9.2「候选批计数，继续不重复统计」）
    std::uint64_t totalUnits = 0;              ///< 候选批总数（同上 totalUnits）
    TwoStageRunAuditCounts audit{};            ///< 审计计数器快照（§9.2 四要素之四
                                                ///  ——恢复自基点累计，AT-35 不重复）

    /**
     * @brief 结构有效性（写出前置校验——机械化：版本≥1、批索引升序无
     *        重复、待评估队列升序无重复、completedUnits≤totalUnits）。
     */
    bool isValid() const noexcept;
};

/**
 * @brief 编码检查点域中间状态（§9.2 域自编 codec——确定性字节序列）。
 *
 * 编码布局（全多字节量小端；确定性 NFR-COR-02）：
 *   [0..7]   magic "IRDOPTC1"（8 字节 ASCII——DOPT-10 登记魔数；与
 *            config.opt canonical 同名异域〔检查点存储格式 vs config
 *            字节流〕，域由消费面区分）
 *   [8..11]  u32 codecVersion
 *   [12..12] u8 phase（core::EvaluationMode 枚举值——Quick/Verified）
 *   [13..16] u32 generation
 *   [17..24] u64 completedUnits ＋ [25..32] u64 totalUnits
 *   [33..36] u32 completedBatchIndices 数 N ＋ N×u32（升序原样）
 *   随后     u32 pendingCandidateIndices 数 M ＋ M×u32（升序原样）
 *   随后     审计段（TwoStageRunAuditCounts 九字段依次 u32——与结构字段
 *            序一致，冻结）
 *
 * @param state [in] 中间状态（调用方保证 isValid——违例抛，调用方契约
 *              违约 fail-fast）
 * @return codec 字节（确定性——同状态任意成员录入序同字节）
 *
 * @throws OptimizationError(kOptInputInvalid) state 结构非法
 *
 * 纯函数；线程安全（可重入）。
 */
std::vector<std::uint8_t>
encodeJointCheckpointState(const JointCheckpointState& state);

/**
 * @brief 解码检查点域中间状态（编码的唯一逆——恢复面消费）。
 *
 * 解码失败语义（§9.2 恢复语义的域侧承载）：magic/版本/截断/尾随字节
 * 任一不符 ⇒ 抛 OptimizationError(kOptInputInvalid)——**绝不静默截断或
 * 部分采纳**；宿主侧把该异常映射为恢复拒绝（EX-CHECKPOINT-CORRUPT/
 * INCOMPATIBLE 轨，原检查点保留——P-EX-9 宁重算不错续）。注意：磁盘级
 * 损坏（字节翻转）由 execution 装载期摘要核对先拦（EX-CKP-1）——本解码
 * 只面对摘要相符但语义不可读的载荷（ codec 版本漂移等）。
 *
 * @param bytes [in] codec 字节（空/截断/魔数不符/版本不支持 ⇒ 抛）
 * @return 中间状态（逐字段还原）
 *
 * @throws OptimizationError(kOptInputInvalid) 载荷不可读（含版本超出
 *         支持区间——kJointCheckpointCodecVersion 之外一律拒绝，不迁移）
 *
 * 纯函数；线程安全（可重入）。
 */
JointCheckpointState
decodeJointCheckpointState(const std::vector<std::uint8_t>& bytes);

// =====================================================================
// StageD 联合两级批量编排（§8.5 外层两级批量编排——T04 落位面）
// =====================================================================

/**
 * @brief 联合运行审计计数（TwoStageRunAuditCounts 的 StageD 扩展——
 *        §8.6 审计 CSV/Profile 证据的编排侧记账面＋§9.2 检查点统计基点）。
 *
 * 值语义；线程安全：纯值（编排器单线程累计——worker 线程不碰审计）。
 */
struct JointRunAuditCounts {
    TwoStageRunAuditCounts base{};        ///< R1 同款九计数（生成/评估/筛选/
                                            ///  缓存分账——口径与 T06 一致）
    std::uint32_t candidatesDeduped = 0;  ///< 候选去重数（§8.3 第 11 步——
                                            ///  同 CandidateId 保留首次）
    std::uint32_t batchesCompleted = 0;   ///< 已完成候选批数（本次 run 内累计
                                            ///  ——含 resume 前缀完成量之外的增量）
    std::uint32_t batchesTotal = 0;       ///< 候选批总数（Quick＋Verified 两相合计
                                            ///  ——CheckpointRecord.totalUnits 基点）
    std::uint32_t checkpointStatesEmitted = 0; ///< 批边界检查点状态产出数
                                            ///  （批边界＝暂停确认边界素材——V12-06）
    std::uint32_t resumedBatchesSkipped = 0;   ///< 恢复跳过的已完成批数
                                            ///  （AT-35 观测点——续跑不重评）
};

/**
 * @brief 单候选联合运行记录（TwoStageRunRecord 的 StageD 扩展载体——
 *        基础面复用 T06 记录形态〔消费者零新类型〕＋联合面增量）。
 *
 * 值语义；线程安全：纯值。
 */
struct JointRunRecord {
    TwoStageRunRecord base;                 ///< 基础面（candidateId/sliceId〔＝门面
                                            ///  切片身份〕/status/metrics/screeningOnly/
                                            ///  formalPassEligible…——T06 消费面兼容）
    std::vector<JointLayerRecord> layers;   ///< 内层六层执行记录（§6.3 序——
                                            ///  逐域逐约束联合记录的分账；
                                            ///  OPT-VER-209 观测面；提前终止时含
                                            ///  已执行层前缀）
    bool allLayersCompleted = false;        ///< 六层全 Completed（门面身份有效前提）
    core::ContentIdentity facadeSliceId{};  ///< 门面切片身份（六层全 Completed 时＝
                                            ///  buildJointFacadeSlice 计算值——
                                            ///  base.sliceId 同值；提前终止为保留值）
};

/**
 * @brief 联合两级编排请求（一次 run() 的冻结输入面——I-OPT-1）。
 *
 * 生命周期：调用方持有并保证 run() 调用期间存活（值语义拷贝入编排）。
 * 值语义；线程安全：纯值（编排实例串行消费）。
 */
struct JointRunRequest {
    OptimizationConfiguration config;    ///< 已校验运行配置（stage 恒 StageD——
                                          ///  并行面解锁后经 validateConfiguration；
                                          ///  threadCount 入 config.opt 进身份）
    core::ObjectId baselineRoot;         ///< 基线根对象身份（CandidateId 公式输入；
                                          ///  须非保留值）
    core::ContentVersion baselineCv;     ///< 基线根对象内容版本（同上）
    evidence::AnalysisSnapshot snapshot; ///< 冻结快照（层评估与汇总输入）
    core::TaskIdentity task;             ///< 评估请求任务五元组（③端口绑定面；
                                          ///  恢复新 Attempt 时由宿主 +1 后传入
                                          ///  ——恢复语义 §9.2）
    evidence::CaseCoverageMatrix coverage;   ///< Verified 批覆盖矩阵（EVI-02 分母
                                          ///  ——全必验工况；漏验不出正式通过）
    std::vector<evidence::RegionCoverageEvidence> regionCoverages; ///< 区域覆盖
                                          ///  证据形状（KIN-04 R8；空表合法）
    /// StageD 离散组合空间声明（§8.5 外层——selection 契约供给；可空＝
    /// 无离散组合维。生成唯一经 generateStageDJointCandidates 单点——
    /// 本编排零生成策略复制）。
    std::vector<StageDCombination> combinations;

    /// 恢复基点（nullopt＝冷启动；有值＝自检查点续跑——已完成批不重评、
    /// 审计自基点累计〔AT-35〕）。兼容判定归宿主侧 judgeCheckpoint
    /// Compatibility（本编排信任入参为已判定可恢复状态——恢复语义 §9.2
    /// 的编排内消费点）。
    std::optional<JointCheckpointState> resumeFrom{};
};

/**
 * @brief 联合两级编排运行结果（§8.4 产出面 StageD 形态）。
 *
 * 值语义；线程安全：纯值。
 */
struct JointRunResult {
    RunPhase runPhase = RunPhase::Draft;  ///< 终态运行状态（Completed/Canceled/
                                           ///  Paused〔WP-21-T07——批边界检查点
                                           ///  挂起〕；Failed/Interrupted 以异常
                                           ///  传播/宿主处置，本编排不产出）
    bool runCompleted = false;            ///< 正常完成（取消/暂停时 false——已回传
                                           ///  批保留，TASK-01；检查点恢复≠成功：
                                           ///  恢复后仍走完整接纳汇总，§9.2；
                                           ///  Paused 时续跑凭据＝checkpointStates
                                           ///  末份——暂停确认边界，V12-06）
    bool searchEmpty = false;             ///< 搜索空标记（可行集空——OPT-SEARCH-
                                           ///  EMPTY warning，非任务不可行）
    std::string searchEmptyToken;         ///< 搜索空稳定 token（非搜索空为空串）
    /// Quick 批记录（screeningOnly 恒 true——绝不进入 pareto 输入的类型学
    /// 屏障：pareto 只从 verifiedRecords 组装）
    std::vector<JointRunRecord> quickRecords;
    /// Verified 批记录（正式面——幸存集复核产物）
    std::vector<JointRunRecord> verifiedRecords;
    /// Pareto 非支配筛选结果（仅 Verified-Feasible 且激活指标完整候选；
    /// 可行集/非支配集双标记——§7.4；空＝搜索空）
    ParetoFrontResult pareto;
    JointRunAuditCounts audit{};              ///< 审计计数（含检查点统计基点）
    /// 批边界检查点状态序列（每完成一批追加一份——终态为全量快照；宿主
    /// 取**末份**编入 CheckpointBatchFile 写出〔只留最新检查点语义由宿主
    /// 决定，本序列保留全量对账面——暂停确认边界的逐批凭据〕）。
    std::vector<JointCheckpointState> checkpointStates;
};

/**
 * @brief 暂停信号查询接口（§9.2 Paused 态消费的编排侧接缝——WP-21-T07）。
 *
 * 为什么是本域接口而不是复用 evidence::IEvaluationContext：取消查询归
 * evidence 评估调用约定（§9.1 既有面，跨单元冻结），而暂停是 execution
 * 任务控制语义（§7.2 requestPause→批边界确认）在优化编排侧的消费点——
 * 经依赖注入进入本编排（同 IJointMetricProjector/IParetoFrontBuilder 接
 * 缝纪律），不改动 evidence 冻结接口（跨单元零触碰）。
 *
 * 语义契约：pauseRequested() 一经置位**不得回退**（同 execution
 * ICancelSignal 纪律——复位语义未定义，自造复位会造成批边界漏判）；查询
 * 须无阻塞（批边界高频调用）；生产实现＝execution 侧暂停意图旗标（任务
 * Running＋requestPause 受理后置位——TaskController 编排），测试以脚本化
 * 替身注入。
 *
 * 取消与暂停同时请求：**取消优先**（§7.4 协作取消的终态语义强于挂起——
 * 编排在批边界先查取消、后查暂停；两者皆真按取消处置，暂停意图随取消
 * 终态一并消解）。
 */
class IPauseSignal {
public:
    virtual ~IPauseSignal() = default;

    /// 当前是否已请求暂停（查询须无阻塞；置位后不回退——类注释）。
    virtual bool pauseRequested() const = 0;
};

/**
 * @brief 编排依赖注入面（构造注入——L5 装配形态；全部非 owning 指针，
 *        调用方保证 run() 期间存活）。
 *
 * 装配校验（构造期）：checkerDeps/projector/paretoBuilder 必须非空（缺失
 * 即构造抛 std::invalid_argument——装配违约 fail-fast）；pauseSignal 可空
 * ＝暂停面未装配（合法——编排永不暂停，R1 装配形态零影响）。
 *
 * 并发契约（并行路径——threadCount>1）：checkerDeps 所指四注册表/投影面
 * 必须并发只读安全（evidence 两注册表查询共享锁已保证；layerSlices 生产
 * 适配器为无状态纯投影——测试替身自持互斥）；ctx.cancellationRequested
 * 与 pauseSignal->pauseRequested() 被多 worker 并发查询，实现须并发安全。
 * 违反并发契约的数据竞争属调用方装配违约（本编排以"每 worker 独立
 * checker 实例＋deps 只读"约束自身侧）。
 */
struct JointRunOrchestratorDeps {
    const JointLayeredCheckDeps* checkerDeps = nullptr; ///< 内层闭环依赖面
                                             ///  （每 worker 据此各持一个
                                             ///  JointLayeredChecker 实例——
                                             ///  SingleThread 口径）
    const IJointMetricProjector* projector = nullptr; ///< 联合指标投影缝
                                             ///  （P-OPT-2 替身面）
    const IParetoFrontBuilder* paretoBuilder = nullptr; ///< Pareto 稳定序单点
                                             ///  （Quick 筛选与 Verified 前沿同源）
    const IPauseSignal* pauseSignal = nullptr; ///< 暂停信号查询缝（WP-21-T07；
                                             ///  nullptr＝暂停面未装配——编排
                                             ///  永不暂停，行为与既有批次一致）
};

/**
 * @brief StageD 联合两级批量编排器（§8.5 外层两级批量编排＋§9.2 并行/
 * 检查点规模化——OPT-03/06 D 部分的编排承载）。
 *
 * 批模型（确定性——批容量与线程数无关）：
 *   - 候选序列＝generateStageDJointCandidates(config, combinations) 产出
 *     （基线恒批首）经 CandidateId 去重（保留首次——§8.3 第 11 步）；
 *   - 批容量＝每相固定 kBatchCapacity=4（实现口径登记 DTB §5.4：卡面无
 *     批容量字段——取小常数使"多批"在合理预算下必然发生，检查点边界与
 *     并行分片的语义可观测；批容量只影响批界划分〔检查点粒度/派发粒度〕，
 *     结果面与批容量无关——结论面确定性不依赖该常数）；末批可短；
 *   - 并行度＝min(config.parallel.threadCount, 相内批数)；threadCount==1
 *     恒串行路径（同代码同序——并行只是派发形态差异）。
 *
 * run() 执行序（固定——确定性）：
 *   1. 请求校验（validateConfiguration——StageD 并行面解锁后的同一校验单
 *      点＋stage==StageD〔StageB 请求抛 OPT-STAGE-LOCKED——R1 面归两级
 *      编排，双编排同码面〕＋身份面非保留值——调用方契约违约 fail-fast）；
 *   2. 候选生成与去重（第 2 步单点＋§8.3 第 11 步；去重计数入审计）；
 *   3. Quick 批相（jointMode=Quick，screeningOnly）：
 *      - resumeFrom 在场且 phase==Quick ⇒ 已完成批索引内的候选整批跳过
 *        （不重评——AT-35；resumedBatchesSkipped 记账），审计计数器自
 *        检查点基点初始化（继续不重复统计）；
 *      - 逐批：批边界查询 ctx.cancellationRequested()（true ⇒ 停止派发，
 *        runPhase=Canceled＋已回传批保留〔TASK-01〕——**产出当前检查点
 *        状态**供宿主写出，续跑自此继续）；批边界其次查询暂停信号
 *        （pauseSignal 非空且 pauseRequested() ⇒ 同批界停止派发，
 *        runPhase=Paused＋该批界检查点状态即**暂停确认凭据**〔§9.2 暂停
 *        确认边界＝批边界检查点——宿主写出并确认后任务态转 Paused 归
 *        execution T10；取消优先于暂停——两者同时请求按取消处置，
 *        §7.4〕；单批运行无批间边界 ⇒ 暂停不生效、运行照常完成）；
 *        批内候选并行/串行推进（每
 *        候选＝联合闭环 check＋八项指标＋激活目标完整性映射＋会话底账
 *        登记）；批完成 ⇒ 产出检查点状态（checkpointStates 追加）；
 *   4. Quick 保守筛选（§8.4 规则原样：Quick-Feasible 且激活目标槽位完整
 *      经 buildFront 稳定序选席，基线 Quick-Feasible 恒占一席；未入选 ⇒
 *      ScreenedOut＋kRejectQuickScreenedBudget；非 Feasible 保持自有状态
 *      不冒充——§7.5）；
 *   5. 幸存集空 ⇒ searchEmpty（OPT-VER-120 口径——正常完成非任务不可行）；
 *   6. Verified 批相（jointMode=Verified，screeningOnly=false）：幸存集
 *      分批推进同第 3 步（resumeFrom.phase==Verified ⇒ 自检查点续跑）；
 *      恢复时 Quick 相记录不可用（检查点只载审计数与批索引，不含记录载
 *      荷——Phase==Verified 恢复要求调用方重走 Quick 相〔确定性同种子同
 *      批 ⇒ 同幸存集〕，已记审计基点防重评双计——如实登记的实现口径：
 *      Quick 相重评发生在"恢复到 Verified 相"的语义下被宿主侧按需触发，
 *      本编排 resumeFrom.phase==Verified 时同样推进 Quick 相但**跳过其
 *      检查点产出与审计累计**〔base 审计已含 Quick 相账目〕）；
 *   7. 可行集→Pareto（buildFront——同 T06 组装口径）；runPhase=Completed。
 *
 * 单候选推进（check 的候选级失败语义）：层失败 ⇒ EvaluationFailed、搜索
 * 未果 ⇒ DataInsufficient、Must 违例 ⇒ Infeasible（逐域逐约束淘汰原因
 * ——OPT-VER-209）——全部经 JointLayeredChecker 结构化返回，编排器零
 * 判定复制（PA-1/N8）；候选级失败绝不抛异常（启动阻塞面才抛）。
 *
 * 确定性：同 (request, deps 行为) ⇒ 同结论面（候选集/状态/幸存集/Pareto
 * 排序/审计计数——与 threadCount 无关〔OPT-VER-221/222 的承载〕；缓存
 * 命中位本编排如实为零查找〔见文件头不落项登记〕）。
 * 线程约束：实例非线程安全——run() 仅编排线程串行调用；worker 线程由
 *   run() 内部派生并 join（不外泄）。
 */
class JointRunOrchestrator {
public:
    /// 检查点批容量常数（类注「批模型」——固定小常数，结果面无关性由
    /// 测试钉扎〔不同批容量同结论面〕）。
    static constexpr std::uint32_t kBatchCapacity = 4;

    /**
     * @brief 构造（阶段定型＋依赖注入）。
     *
     * @param stage [in] 编排阶段（本编排只实现 StageD；StageB 构造不拒、
     *              run 即抛——R1 面归 TwoStageEvaluationOrchestrator，双
     *              编排同一阶段锁码面）
     * @param deps  [in] 依赖注入面（指针在 run() 期间存活）
     *
     * @throws std::invalid_argument deps 必填项为空（装配违约 fail-fast）
     */
    JointRunOrchestrator(OptimizationStage stage, JointRunOrchestratorDeps deps);

    /**
     * @brief 执行一次联合两级编排（Quick 筛选→保守筛选→Verified 复核→
     *        Pareto；并行/恢复语义见类注）。
     *
     * @param request [in] 冻结请求（调用方持有，调用期间有效）
     * @param ctx     [in] 宿主调用上下文（批边界协作取消查询；并行路径下
     *                须并发安全——文件头线程约束）
     * @return 运行结果（结论面确定；取消时部分批保留＋当前检查点状态在
     *         checkpointStates 末位）
     *
     * @throws OptimizationError(kOptInputInvalid) 配置/身份面非法；
     *         kOptStageLocked StageB 请求／内层闭环阶段锁传播；
     *         kOptEvaluatorMissing 内层必需评估器未注册（装配不一致）；
     *         kOptPatchIllegal 候补构造拒绝（研究定义非法——生成期
     *         fail-fast）；std::invalid_argument 请求结构违约（保留值身份）
     */
    JointRunResult run(const JointRunRequest& request, evidence::IEvaluationContext& ctx);

private:
    OptimizationStage m_stage;         ///< 编排阶段（构造定型）
    JointRunOrchestratorDeps m_deps;   ///< 依赖注入面（构造后只读）

    // ---- run() 内部步骤（编排线程串行调用；worker 线程只跑单候选评估）----
    /// 单候选联合评估（check→指标→完整性映射→记录；线程约束：在 worker
    /// 线程调用——只触 deps 只读面与局部值，不碰编排器可变状态。checker
    /// 由调用方 worker 持有传入——每 worker 一个实例，SingleThread 口径）。
    JointRunRecord evaluateCandidate(const JointRunRequest& request,
                                     core::EvaluationMode jointMode,
                                     const CandidatePatch& patch,
                                     JointLayeredChecker& checker,
                                     evidence::IEvaluationContext& ctx) const;
};

}  // namespace sdurws::ird::optimization

#endif  // SDURWS_IRD_OPTIMIZATION_JOINT_RUN_HPP
