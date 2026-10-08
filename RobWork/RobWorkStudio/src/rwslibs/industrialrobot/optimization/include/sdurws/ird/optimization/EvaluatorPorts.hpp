/**
 * @file   EvaluatorPorts.hpp
 * @brief  优化批量编排与缓存/种子消费面（O7/O15——§8/§9 的 WP-20-T06 交付
 *         子集）：config.opt 运行配置载荷与 canonical 序列化（缓存键要素）、
 *         确定性候选生成（种子消费点）、Quick/Verified 两级批量编排（保守
 *         淘汰→复核）、会话内缓存命中消费（判定唯一经 evidence
 *         judgeCacheHit、存储治理归 execution）。
 *
 * 设计依据：
 *   - units/optimization.md §4.3（config.opt 载荷 OptimizationConfiguration/
 *     OptimizationBudget/ParallelConfiguration——"schemaVersion 1；任何字段
 *     变化 ⇒ 新 sliceId ⇒ 缓存不命中 ⇒ 新运行"；canonical magic "IRDOPTC1"
 *     ＋u32 schemaVersion＋阶段/种子/线程/预算/策略 token 定序定宽编码＋
 *     目标集位图段＋变量绑定表按 bindingToken 字典序；"编码由 optimization
 *     实现，身份由 evidence/快照体系承载"）、§4.4 I-OPT-2（seed≥1，0 非法
 *     不静默替换）/I-OPT-3（同输入⇒等价候选集合与稳定排序）、§4.5（缓存
 *     失效映射——种子/线程/预算/策略进 config.opt；"缓存命中不代表结果
 *     Current——当前性由 evidence computeCurrentness 另行计算，不复制第二
 *     套兼容逻辑（N8）"）、§8.2（基线候选恒生成参与评估；候选数量受
 *     OptimizationBudget 约束；变量编码＝补丁 canonical 内容无第二编码层）、
 *     §8.3（评估编排第 4 步阶段能力检查/第 5 步失败取消语义/第 9 步淘汰
 *     原因/第 10 步稳定排序）、§8.4（Quick/Verified 两级与缓存——Quick
 *     保守淘汰 ScreenedOut＋screening-only 不进正式可行集；Verified 完整
 *     预算复核进可行集→Pareto；缓存键要素与命中判定唯一归 evidence
 *     judgeCacheHit；写入双门槛 CON-04；确定性承诺 NFR-COR-02）、§9.1
 *     （R1 形态——本编排不拥有 worker 调度，N8）、§15.0（并行与检查点不
 *     作为 B 期验收承诺）
 *   - 需求 OPT-06（B 子集：缓存与确定性种子）、NFR-COR-02（同种子同线程
 *     配置⇒等价候选集合与稳定排序——AT-09~11）、CON-04（部分/失败结果
 *     不得作为正式缓存命中；命中≠Current）、OPT-02（V12-02——传动比参数
 *     可编辑；采样域锁定集外）
 *   - 任务契约 tasks/foundation/WP-20-T06.json acceptance 1~3；knownPitfalls
 *     P-EV-8（Quick 产物在 Verified 判定中＝EvidenceItemStatus::Unverified
 *     ——实现承载词表；本编排以 screeningOnly 标记＋"Quick 记录绝不进入
 *     buildFront 输入"的类型学承载其语义，正式判定仍唯一归 evidence）
 *
 * ★ 落位范围口径（诚实登记，防扩大）：§14.2 行"§8 → EvaluatorPorts.hpp＋
 *   src（管线/策略/缓存消费）| WP-20-T06"——本任务交付：①config.opt 载荷
 *   ＋canonical＋校验（§4.3/§4.5 身份面）；②确定性候选生成（§8.2 首版
 *   策略 seeded-lhs 的 R1 面）；③Quick/Verified 两级批量编排（§8.4）＋
 *   会话缓存消费（编排排查找请求/消费命中结果/统计命中计数——存储治理
 *   归 execution IExecutionCacheCoordinator，本编排零存储零判定复制）。
 *   **不落**：ICandidateGenerator 扩展点接口（§12.2——OPT-10 属 D/R2，
 *   接口扩展随 WP-21-T06）；IOptimizationRunController（§9.1/§12.2——
 *   任务提交经 execution 的运行控制面随 WP-20-T07 Run.hpp）；Optimization-
 *   RunSpec（§4.3 尾部——Preflight 消费随 WP-20-T08；本批零消费者不预建，
 *   NFR-MNT-04）；OptimizationConfiguration 在卡面 §4.3 Draft 中标注落位
 *   Types.hpp，因 Objective.hpp/Variable.hpp 反向依赖 Types.hpp（include
 *   环）改落本头——偏差随单元卡增量修订登记（DTB §5.4）。
 *   接缝口径：候选→投影事实（限位查询/包络几何/连杆质量/工位裕量）经
 *   ICandidateProjector 注入（P-OPT-2 裁决前由调用方/测试供给，与 T04
 *   探针接缝/T05 StaticMetricFacts 投影同款形态）；生产适配器随 P-OPT-2
 *   候选物化通道落位（§16.3 R-OPT-1）。
 *
 * 背景说明（第一读者须知——三件事）：
 *   ① **判定与编排分离**（T04 头注②同源纪律）：候选 Feasible/Infeasible/
 *      DataInsufficient/EvaluationFailed 的最终判定唯一归 T04 管线侧
 *      evidence::aggregateVerdict（§7.5 判定流）；缓存命中判定唯一归
 *      evidence::judgeCacheHit（三档六原因）；当前性唯一归 evidence::
 *      computeCurrentness。本编排只做：组装、派发、筛选（Quick 保守淘汰
 *      是本域编排语义——ScreenedOut 是候选状态非工程判定）、记账。
 *   ② **缓存三步走**（§8.4 原文的编排侧机械化）：编排查找请求（每候选
 *      评估请求一个冻结切片——config.opt canonical 与补丁身份进切片条目
 *      ⇒ sliceId 含全部缓存键要素，mode/契约版本/Profile 身份作判定面四
 *      要素）→ 消费命中结果（FullHit＝短路径：会话内有同键记录则回放不
 *      重算；会话内无载荷如实重算——跨会话载荷装载归归档面 WP-20-T07）
 *      → 统计命中计数（审计——FullHit/DiagnosticOnly/Incompatible 分账）。
 *      失败/取消/部分结果由存储侧（execution 写入门槛）与判定侧（evidence
 *      outcome/manifest 条件）双层挡在正式命中之外（CON-04）。
 *   ③ **确定性三支柱**（I-OPT-3/NFR-COR-02）：种子 ≥1 且 0 非法不静默替换
 *      （validateConfiguration fail-fast）；随机性唯一来自自持 splitmix64
 *      （不依赖 STL/libc 实现差异——跨平台可重放）；同配置双跑产出等价
 *      候选集合（CandidateId 集合相等）与稳定排序（筛选与 Pareto 消费
 *      T05 buildFront 稳定序三键）。并行与检查点不落本编排（R1 threadCount
 *      恒 1——非 1 配置显式拒绝，不静默降级）。
 *
 * 线程约束：validateConfiguration/canonicalizeRunConfiguration/
 *   generateSeededLhsCandidates 纯函数可重入；TwoStageEvaluationOrchestrator
 *   实例持有会话记录底账（跨 run() 复用）——**非线程安全：仅编排线程串行
 *   调用 run()**（R1 单线程编排——卡 §6.7"线程并行由编排/多 worker 掌控"
 *   的编排侧，本批串行）。
 * 确定性：同 (request, deps 行为) ⇒ 同 TwoStageRunResult 结论面（候选集合/
 *   状态/排序/审计计数；缓存命中路径的审计计数会随命中与否变化——命中是
 *   性能面非结论面，I-OPT-3 承诺的是候选集合与稳定排序）。
 */

#ifndef SDURWS_IRD_OPTIMIZATION_EVALUATOR_PORTS_HPP
#define SDURWS_IRD_OPTIMIZATION_EVALUATOR_PORTS_HPP

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sdurws/ird/core/Evaluation.hpp>      // core::EvaluationMode——模式效力面
#include <sdurws/ird/core/Identity.hpp>        // core::ObjectId/ContentVersion/
                                                //  TaskIdentity——候选身份公式与请求绑定
#include <sdurws/ird/evidence/Envelope.hpp>    // evidence::EvidenceProfileRef——
                                                //  Profile 三元组（缓存判定第四要素）
#include <sdurws/ird/evidence/Evaluator.hpp>   // evidence::IEvaluationContext——
                                                //  批间协作取消查询（§12.2 取消粒度）
#include <sdurws/ird/evidence/Slice.hpp>       // evidence::InputSlice——冻结切片（缓存键）
#include <sdurws/ird/evidence/Snapshot.hpp>    // evidence::AnalysisSnapshot——冻结快照
#include <sdurws/ird/execution/CacheCoordinator.hpp>  // execution::IExecutionCacheCoordinator
                                                //  ——会话缓存查找/消费（存储治理归 execution，
                                                //  §8.4；判定面 evidence judgeCacheHit 由其消费）
#include <sdurws/ird/optimization/CandidatePatch.hpp>  // CandidatePatch/CandidateId——候选描述
#include <sdurws/ird/optimization/Constraint.hpp>      // StaticHardConstraintPipeline/Input/
                                                //  CandidateEvaluationRecord——单候选评估执行件
#include <sdurws/ird/optimization/Objective.hpp>       // ObjectiveSet/StaticMetricFacts/
                                                //  StaticMetricResult——目标与指标事实
#include <sdurws/ird/optimization/Pareto.hpp>          // IParetoFrontBuilder/
                                                //  ParetoFrontResult/FeasibleCandidate——筛选与前沿
#include <sdurws/ird/optimization/Types.hpp>           // OptimizationStage/RandomSeed/
                                                //  CandidateStatus/RunPhase/OptimizationError
#include <sdurws/ird/optimization/Variable.hpp>        // VariableBinding——研究绑定集

namespace sdurws::ird::optimization {

// =====================================================================
// 搜索策略词表与契约版本（卡 §8.2/§6.7）
// =====================================================================

/**
 * @brief 首版搜索策略 token（卡 §8.2 表"opt.strategy.seeded-lhs"——确定性
 *        种子拉丁超立方采样，连续/量化变量空间填充；B/D 两阶段）。进
 *        config.opt canonical（策略 token 变化 ⇒ sliceId 变 ⇒ 缓存失效，
 *        §4.5 表）。
 *
 * 词表边界（防扩大）：本批只登记**已实现**的策略（NFR-MNT-04 不预建占位
 * ——空算法占位被 §8.2 明文禁止）。§8.2 表第二行 "opt.strategy.baseline-
 * perturb"（改型邻域扰动）登记于卡面但实现随 WP-21-T06 搜索策略接口扩展
 * （OPT-10 属 D/R2）——届时随其任务表尾追加本词表并扩展 validateConfiguration
 * 接受集（单元卡增量修订留痕）。
 */
inline constexpr std::string_view kStrategySeededLhs = "opt.strategy.seeded-lhs";

/**
 * @brief 优化域聚合评估器契约版本（§6.7 表 opt-static-screen——"契约版本
 *        1"明文；本编排组装的评估请求切片以该键＋版本编址，进 sliceId 与
 *        缓存判定第三要素。与 T05 MetricDefinition.requiredContractVersion
 *        同源——单一权威此处，词表处只读引用）。
 */
inline constexpr std::uint32_t kOptStaticScreenContractVersion = 1U;

// =====================================================================
// config.opt 运行配置载荷（卡 §4.3 原文契约——缓存键要素与确定性来源）
// =====================================================================

/**
 * @brief 优化域预算（卡 §4.3——候选数/复核数/生成代数/墙钟；内存与 worker
 *        资源预算归 execution ResourceBudget，本类型不复制资源治理字段——
 *        N8 非所有权）。
 *
 * 默认值＝卡面 §4.3 原文（maxCandidates 256／maxVerifiedCandidates 64／
 * maxGenerations 1／maxWallClockS 8×3600 s）。
 *
 * R1 承诺边界：maxGenerations 固定 1（R1 单批生成——多代搜索归 R2 策略
 * 扩展）；maxWallClockS 仅作配置承载，R1 编排**不强制执行**墙钟（NFR-
 * PERF-06 基准口径归 WP-23-T08——如实登记，不伪造超时能力）。
 *
 * 值语义；线程安全：纯值。
 */
struct OptimizationBudget {
    std::uint32_t maxCandidates = 256;        ///< 候选总数上限（含基线；≥1）
    std::uint32_t maxVerifiedCandidates = 64; ///< Verified 复核上限（≥1 且
                                              ///  ≤maxCandidates——§8.4 幸存集上界）
    std::uint32_t maxGenerations = 1;         ///< 生成代数上限（R1 固定 1；
                                              ///  R2 搜索策略可用——WP-21-T06）
    std::uint64_t maxWallClockS = 8 * 3600;   ///< 墙钟上限（s——NFR-PERF-06
                                              ///  基准口径；R1 不强制执行，≥1）
};

/**
 * @brief 并行配置声明（卡 §4.3——R1 threadCount 固定 1，仅入身份不做并行
 *        承诺；R2 由 execution 编排）。
 *
 * 为什么 R1 拒绝非 1 值：§15.0 明文"并行与检查点不作为 B 期验收承诺（归
 * OPT-D）"——若接受 threadCount>1 而按串行执行即**静默降级**（配置语义与
 * 实际行为不符，NFR-COR-03 同源纪律），故 validateConfiguration 显式拒绝
 * （OPT-INPUT-INVALID，消息指向 OPT-D 落位）。字段本身保留：值进 canonical
 * （KIN-13 同型——线程数是身份要素），OPT-D 启用并行时格式位已就位。
 *
 * 值语义；线程安全：纯值。
 */
struct ParallelConfiguration {
    std::uint32_t threadCount = 1;        ///< ≥1；R1 恒 1（进 config.opt
                                          ///  canonical——KIN-13 同型"改线程
                                          ///  数＝新身份＝重算"）
    std::uint32_t maxInFlightBatches = 1; ///< 在途批上限（R1 恒 1；R2 由
                                          ///  execution 派发治理——D-10 合并）
};

/**
 * @brief 优化分析配置（config.opt；卡 §4.3——canonical 定序序列化后作为
 *        Configuration 切片条目进入 sliceId）。schemaVersion 1；任何字段
 *        变化 ⇒ 新 sliceId ⇒ 缓存不命中 ⇒ 新运行（§4.5/§4.6）。
 *
 * 字段面＝§4.3 原文：阶段/种子/并行/预算/策略/目标集/变量绑定。**不含**
 * 碰撞开关（V13-01：碰撞启用只读引用快照内已解析策略——不在 config）；
 * **不含**判定阈值（归 policy——NFR-MNT-07 单一权威）。
 *
 * 构造纪律：经 validateConfiguration 校验后方可交编排（fail-fast 面——
 * 种子 0/线程数非 1/预算矛盾/未登记策略都在校验拦截）；本结构无 invariant
 * 构造器（聚合值类型——Snapshot/InputSlice 同款纪律），校验入口单点见
 * validateConfiguration。
 *
 * 值语义；线程安全：纯值。
 */
struct OptimizationConfiguration {
    std::uint32_t schemaVersion = 1;              ///< config schema 版本（进 canonical）
    OptimizationStage stage = OptimizationStage::StageB; ///< 优化阶段（变量/约束/目标面作用域）
    RandomSeed seed = 1;                          ///< 随机种子（I-OPT-2：≥1；
                                                  ///  0 非法不静默替换——NFR-COR-03）
    ParallelConfiguration parallel {};            ///< 并行声明（R1 恒 1——见结构注）
    OptimizationBudget budget {};                 ///< 候选/复核/代数/墙钟预算
    std::string strategyId = std::string(kStrategySeededLhs); ///< 搜索策略 token
                                                  ///  （§8.2 首版＝seeded-lhs）
    ObjectiveSet objectives {};                   ///< 激活目标＋方向＋支配容差
                                                  ///  （§7；Pareto 筛选与前沿同源）
    std::vector<VariableBinding> variables {};    ///< 变量绑定＋锁定/授权状态
                                                  ///  （§5.2；生成采样域＝未锁定
                                                  ///  且授权者；进 canonical 绑定表）
};

/**
 * @brief 校验运行配置（fail-fast 轨——调用方错误抛 OptimizationError）。
 *
 * 校验规则（检查序固定——确定性首错；全部不静默修正——NFR-COR-03）：
 *   ① schemaVersion ≥1（0＝非法版本）；
 *   ② seed ≥1（I-OPT-2：0 非法——**不做静默替换**；消息明示合法域）；
 *   ③ parallel.threadCount ==1（R1：并行不作 B 期承诺——§15.0；非 1 显式
 *      拒绝不静默降级，落位指向 WP-21/OPT-D）；
 *   ④ parallel.maxInFlightBatches ==1（R1 恒 1——同③口径）；
 *   ⑤ budget.maxCandidates ≥1（至少基线候选——§8.2 基线恒生成）；
 *   ⑥ budget.maxVerifiedCandidates ≥1 且 ≤maxCandidates（幸存集上界不得
 *      超过总预算——§8.4 V1）；
 *   ⑦ budget.maxGenerations ==1（R1 固定单批——多代归 R2）；
 *   ⑧ budget.maxWallClockS ≥1（0＝无意义配置；R1 不强制执行——见类型注）；
 *   ⑨ strategyId ∈ 已实现策略词表（当前仅 kStrategySeededLhs——未登记
 *      token 拒绝；扩展点随 WP-21-T06）；
 *   ⑩ objectives 非空、MetricId 无重复、容差两分量非负且有限（同
 *      makeObjectiveSet 校验面——目标集是筛选与 Pareto 的支配基础，空集
 *      无比较基础）。
 *
 * @param config [in] 待校验配置
 * @throws OptimizationError(kOptInputInvalid) ①~⑩任一违例（消息含违例
 *         字段定位与合法域说明——比较型定位纪律 ERR-01）
 *
 * 纯函数；线程安全（可重入）。
 */
void validateConfiguration(const OptimizationConfiguration& config);

/**
 * @brief config.opt canonical 字节（卡 §4.3 原文格式——"magic "IRDOPTC1"＋
 *        u32 schemaVersion＋阶段/种子/线程/预算/策略 token 定序定宽编码＋
 *        目标集（按 MetricId 枚举序位图＋每目标容差三元组）＋变量绑定表
 *        （按 bindingToken 字典序）"）。
 *
 * 编码布局（codecVersion 语义由 schemaVersion 承载；全部多字节量小端）：
 *   [0..7]   magic "IRDOPTC1"（8 字节 ASCII——卡面 §4.3 指定魔数；与检查点
 *            codec 登记同名〔§14.4〕——两者是不同编码域〔config.opt 字节流
 *            vs 检查点存储格式〕，同名不构成身份混淆）
 *   [8..11]  u32 schemaVersion（小端）
 *   [12..19] 阶段 token 定宽 8 字节（toToken(stage) 左对齐补 NUL——定序
 *            定宽编码；"stage-b\0\0" / "stage-d\0\0"）
 *   [20..27] u64 seed（小端）
 *   [28..31] u32 parallel.threadCount
 *   [32..35] u32 parallel.maxInFlightBatches
 *   [36..39] u32 budget.maxCandidates
 *   [40..43] u32 budget.maxVerifiedCandidates
 *   [44..47] u32 budget.maxGenerations
 *   [48..55] u64 budget.maxWallClockS
 *   [56..59] u32 策略 token 字节长 L ＋ [60..60+L) token UTF-8 字节
 *   随后     目标段＝canonicalizeObjectiveSegment(objectives)（T05 编码——
 *            位图＋每激活目标 ε_rel/ε_abs，声明序无关）
 *   随后     变量绑定表（**编码前按 bindingId 字典序排序**——§4.3 原文
 *            "按 bindingToken 字典序"；同配置任意录入序同字节）：
 *              u32 绑定数 N
 *              每绑定：
 *                u16 bindingId 字节长＋字节
 *                u8  kind（VariableKind 枚举值）
 *                u8  locked（0/1）
 *                u8  authorized（0/1）
 *                f64 lowerBound / upperBound / step / defaultValue
 *                    （IEEE 754 位模式小端——canonical 浮点纪律同 §5.2）
 *                u32 defaultValueIndex
 *                u32 枚举值数 E ＋ 每枚举 u16 字节长＋字节
 *                u16 unitSymbol 字节长＋字节（词表 SI 符号——绑定语义面；
 *                    非显示单位，I-OPT-8 排除的是显示换算不入身份）
 *                u16 authorityFieldPath 字节长＋字节（权威字段定位——绑定
 *                    语义的物化目标，随绑定进身份）
 *                u16 diagSubject 字节长＋字节（关联对象定位——对象引用是
 *                    绑定语义的一部分〔快照闭包核对输入〕）
 *
 * 消费面：编排器把它作为 Configuration 切片条目（key "opt.config"）冻结
 * 进每候选评估切片 ⇒ 字节进 sliceId ⇒ §4.5 失效映射全表自动成立（种子/
 * 线程/预算/策略/目标/变量任一变化 ⇒ sliceId 变 ⇒ 缓存不命中）。内容身份
 * 由消费方经 core::ContentDigester 计算（"编码由 optimization 实现，身份
 * 由 evidence/快照体系承载"——§4.3 三层分工；本函数只产字节）。
 *
 * @param config [in] 运行配置（先经 validateConfiguration——本函数防御性
 *               复验，违例同样抛）
 * @return canonical 字节（确定性——同配置任意成员录入序同字节）
 *
 * @throws OptimizationError(kOptInputInvalid) 同 validateConfiguration 拒绝面
 *
 * 纯函数；线程安全（可重入）。
 */
std::vector<std::uint8_t> canonicalizeRunConfiguration(const OptimizationConfiguration& config);

// =====================================================================
// 确定性候选生成（§8.2 首版策略 seeded-lhs——种子消费点）
// =====================================================================

/**
 * @brief 确定性候选批生成（卡 §8.2 策略 "opt.strategy.seeded-lhs" 的 R1
 *        面——种子驱动拉丁超立方采样＋基线候选恒生成）。
 *
 * 算法（逐步——确定性 I-OPT-3/NFR-COR-02；随机性唯一来自自持 PRNG）：
 *   1. 防御复验 config（同 validateConfiguration——种子 0 等在此拦截）。
 *   2. 采样维度集：config.variables 中**未锁定且授权**（§8.2 改型"锁定集
 *      外扰动"；锁定绑定不可出现在补丁——§5.4）且值类别为
 *      Continuous/Quantized/Enumeration 的绑定，按 bindingId 字典序排列
 *      （确定性维度序）。DiscreteDevice 维度属 R2（StageD——不进 R1 采样，
 *      §5.6/§8.2 组合空间采样归 WP-21）。离散维度集可以为空——此时批＝
 *      基线＋同值候选（补丁恒空——全与基线同身份，去重后单候选；研究
 *      无采样维度时预算自然退化，如实不伪造多样性）。
 *   3. 非基线候选数 n = budget.maxCandidates − 1。
 *   4. 自持 splitmix64 PRNG：状态初值＝seed（≥1 已校验）；每次
 *      state += 0x9E3779B97F4A7C15 后经三轮混合（常数＝splitmix64 规范
 *      黄金常数——Not a Wiki 建议，无工程自由度）。[0,1) 均匀数由 64 位
 *      输出右移 11 位乘 2⁻⁵³（53 位尾数——双精度无损）。
 *   5. 拉丁超立方核心：每维度独立持一个 n 层的层序置换（Fisher-Yates 洗
 *      牌，PRNG 驱动）——候选 i 在维度 d 取层 perm_d[i]，保证每维度 n 个
 *      候选各占一层（空间填充正交性——§8.2"全局空间填充（探索优先）"）。
 *   6. 逐候选逐维度取值：
 *      - Enumeration：PRNG 均匀取 enumValues 下标（clamp 到值域末位——
 *        确定性；枚举组合上限保护＝值域即词表封闭集，无爆炸面）；
 *      - Continuous：层内均匀点 value = lower + (layer + u)/n × (upper−lower)
 *        （u∈[0,1)——层 [layer/n,(layer+1)/n) 映射到值域，含端点安全性由
 *        u<1 保证）；
 *      - Quantized：采样域收缩为 [lower+step, upper−step] 后同连续公式
 *        （保证补丁构造器 round-half-even 对齐后必在 [lower,upper] 界内
 *        ——对齐偏差 ≤step/2；收缩域为空〔区间窄于两步〕时退化为
 *        defaultValue——确定性退化，注释登记非静默纠错：退化发生在采样
 *        设计层，值仍是显式绑定默认值）；
 *   7. 每候选经 makeCandidatePatch 构造（补丁 canonical 唯一编码层——
 *      §8.2"编码即补丁 canonical 内容，无第二编码层"；量化对齐/排序/
 *      规范化全部复用 T03 实现）。构造抛出（如 valueMustBePositive 维度
 *      配了含非正值域——研究定义与词表值域矛盾）按调用方错误 fail-fast
 *      传播，不跳过不吞（候选级失败语义只属评估期，生成期非法＝研究
 *      定义非法）。
 *   8. 返回批：首元素恒为基线空补丁（§8.2"基线候选……总是生成并参与
 *      评估"，label "baseline"），随后 n 个采样候选（label "cand-<i>"——
 *      label 不参与身份，I-OPT-8）。
 *
 * 确定性：同 config ⇒ 同批（逐字节同补丁序列——PRNG 自持＋层置换确定性
 * ＋浮点运算 IEEE 确定）。等价候选集合的观测点＝编排层 CandidateId 集合
 * （同基线同补丁必同 CandidateId——跨运行稳定，CandidatePatch.hpp）。
 *
 * @param config [in] 已校验配置（本函数防御复验；变量集从 config.variables
 *               取——单一来源）
 * @return 候选补丁批（首元素基线；长度 = min(maxCandidates, 1+n)——n 由
 *         预算决定；绑定无采样维度时长度的有效去重规模＝1）
 *
 * @throws OptimizationError(kOptInputInvalid) 配置非法（同校验面）；
 *         kOptPatchIllegal 候选补丁构造拒绝（研究定义与词表值域矛盾——
 *         fail-fast，见第 7 步）
 *
 * 纯函数；线程安全（可重入——无共享状态）。
 */
std::vector<CandidatePatch> generateSeededLhsCandidates(const OptimizationConfiguration& config);

// =====================================================================
// 候选投影接缝（P-OPT-2 裁决前形态——与 T04 探针/T05 指标投影同款）
// =====================================================================

/**
 * @brief 单候选评估投影（T04 管线输入与 T05 指标事实的候选相关部分——
 *        P-OPT-2 裁决前由调用方/测试供给）。
 *
 * 值语义；线程安全：纯值。
 */
struct CandidateProjection {
    /// 基线链型在 R1 启用范围内（T04 StaticHardConstraintInput 同名投影——
    /// 拓扑约束事实入口）。
    bool baselineChainInEnabledScope = true;
    /// 候选关节限位表＋构型样本（T04 同名投影——限位约束查询；关节角
    /// 单位 rad（转动）/m（移动））。
    JointLimitProbeRequest jointLimits;
    /// 三项静态指标的候选事实投影（T05 StaticMetricFacts——包络几何
    /// 〔基座系，m〕/连杆质量〔kg〕/Must 工位裕量〔D-KIN-2 归一化，
    /// 无量纲〕；缺项语义同 T05）。
    StaticMetricFacts metricFacts;
};

/**
 * @brief 候选投影接口（§8.3 编排第 3 步的候选→事实映射缝——P-OPT-2 裁决
 *        前由测试以脚本化替身注入；生产适配器＝候选物化通道落位后的
 *        机械投影，随 P-OPT-2/WP-20-T08 装配）。
 *
 * 实现契约：同补丁重复调用输出逐字段相等（投影纯函数——NFR-COR-02；
 * 计数用 mutable 承载）。
 */
class ICandidateProjector {
public:
    virtual ~ICandidateProjector() = default;

    /**
     * @brief 投影候选的评估事实（限位查询＋指标事实）。
     *
     * @param patch    [in] 候选补丁（调用方持有，调用期间有效）
     * @param bindings [in] 研究绑定集（补丁语义来源）
     * @return 投影事实（值语义）
     */
    virtual CandidateProjection project(const CandidatePatch& patch,
                                        const std::vector<VariableBinding>& bindings) const = 0;
};

// =====================================================================
// 两级编排请求/记录/结果（§8.4 的数据面）
// =====================================================================

/**
 * @brief 两级批量编排请求（一次 run() 的冻结输入面——I-OPT-1 输入冻结）。
 *
 * 生命周期：调用方持有并保证 run() 调用期间存活（值语义拷贝入编排）。
 * 值语义；线程安全：纯值（编排实例串行消费）。
 */
struct TwoStageRunRequest {
    OptimizationConfiguration config;      ///< 已校验运行配置（变量/种子/预算/
                                            ///  策略/目标——canonical 与采样单一来源）
    core::ObjectId baselineRoot;           ///< 基线根对象身份（CandidateId 公式
                                            ///  输入——§4.2；须非保留值）
    core::ContentVersion baselineCv;       ///< 基线根对象内容版本（同上；须非保留值）
    evidence::AnalysisSnapshot snapshot;   ///< 冻结快照（切片冻结与 T04 管线输入）
    core::TaskIdentity task;               ///< 评估请求任务五元组（T04 ③端口绑定面）
    evidence::EvidenceProfileRef profile;  ///< Profile 三元组（manifest 组装＋缓存
                                            ///  判定第四要素——profileContentIdentity；
                                            ///  contentIdentity 须非保留值）
    /// Verified 批覆盖矩阵投影（全必验工况执行——EVI-02 覆盖完备；漏验
    /// ⇒ ②级门禁 ⇒ 不出正式通过，AT-09 反例）。requiredCaseSetId 须等于
    /// 快照冻结值（T04 管线清单绑定对账）。
    evidence::CaseCoverageMatrix coverage;
    /// Quick 批覆盖矩阵投影（Quick 口径——低预算执行子集；结构合法即可，
    /// 覆盖不完备在 Quick 层不阻断筛选〔screening-only——EVI-01 表 1〕）。
    evidence::CaseCoverageMatrix quickCoverage;
    /// 区域覆盖证据形状（T04 同名透传——KIN-04 R8；空表合法）。
    std::vector<evidence::RegionCoverageEvidence> regionCoverages;
};

/**
 * @brief 单候选两级编排记录（§8.4 记录面——Quick 批与 Verified 批共用
 *        形态，以 mode/screeningOnly 区分效力）。
 *
 * 值语义；线程安全：纯值。
 */
struct TwoStageRunRecord {
    CandidateId candidateId = {};              ///< 候选身份（去重/合并/排序锚）
    core::ContentIdentity sliceId = {};        ///< 本候选评估切片身份（缓存键主面
                                                                ///  ——审计追溯与缓存登记的对账锚；
                                                                ///  编排器冻结切片的计算值）
    bool isBaseline = false;                   ///< 基线候选标记（空补丁——§8.2）
    CandidatePatch patch = {};                 ///< 候选补丁（审计与差异预览素材）
    core::EvaluationMode mode = core::EvaluationMode::Quick; ///< 产生模式
                                                                ///  （Quick/Verified）
    bool screeningOnly = false;                ///< **screening-only 标记**（Quick 批
                                                                ///  恒 true）：本记录不支撑正式
                                                                ///  结论——不进正式可行集、不进
                                                                ///  Pareto 输入（编排类型学屏障；
                                                                ///  正式资格另由 formalPassEligible
                                                                ///  经 evidence 五条件判——Quick
                                                                ///  记录因 mode-not-verified 恒 false，
                                                                ///  P-EV-8 实现承载词表）
    CandidateStatus status = CandidateStatus::Pending; ///< 候选状态（§7.5 判定流映射；
                                                                ///  ScreenedOut 仅由 Quick 筛选产出）
    core::EngineeringStatus engineeringStatus                 ///< 工程判定（透传 evidence
        = core::EngineeringStatus::NotApplicable;             ///  aggregateVerdict 输出）
    bool formalPassEligible = false;           ///< 正式通过资格（evidence 五条件——
                                                                ///  Quick 记录恒 false）
    std::vector<std::string> formalPassUnmetConditions; ///< 未满足资格条件
                                                                ///  （evidence 稳定 token）
    CandidateEvaluationRecord evaluation;      ///< T04 管线评估记录（约束事实/
                                                                ///  淘汰原因/判定追溯——缓存命中
                                                                ///  回放时为首次评估的原始记录）
    StaticMetricResult metrics;                ///< T05 八项指标事实（枚举序——
                                                                ///  缓存命中回放时同源）
    bool cacheHit = false;                     ///< 本记录产生自缓存 FullHit 短路
                                                                ///  径（审计——命中≠Current，两套
                                                                ///  语义不混同：本标记只记账复用，
                                                                ///  当前性归 evidence computeCurrentness
                                                                ///  另行计算——§4.5）
    std::vector<RejectionReason> extraRejections; ///< 编排层追加的淘汰原因
                                                                ///  （Quick 筛选预算线——
                                                                ///  kRejectQuickScreenedBudget；
                                                                ///  管线淘汰原因在 evaluation 内）
};

/**
 * @brief 两级编排审计计数（§8.6/§11.4 审计 CSV 的 R1 记账面——"审计计数
 *        入审计 CSV 与 Profile 证据"的编排侧来源；导出落盘随 WP-20-T09）。
 *
 * 值语义；线程安全：纯值。
 */
struct TwoStageRunAuditCounts {
    std::uint32_t candidatesGenerated = 0; ///< 生成候选数（含基线）
    std::uint32_t quickEvaluated = 0;      ///< Quick 批实际评估数（命中回放不计）
    std::uint32_t quickScreenedOut = 0;    ///< Quick 保守淘汰数（OPT-VER-127 观测点）
    std::uint32_t verifiedEvaluated = 0;   ///< Verified 批实际评估数（命中回放不计）
    std::uint32_t cacheLookups = 0;        ///< 缓存查找总数
    std::uint32_t cacheFullHits = 0;       ///< FullHit 短路径数（OPT-VER-137 观测点——
                                            ///  命中计数；命中≠Current）
    std::uint32_t cacheDiagnosticOnly = 0; ///< 诊断性读取数（CON-04——部分/失败/
                                            ///  未封账结果不作正式命中，只可读诊断）
    std::uint32_t cacheIncompatible = 0;   ///< 不兼容拒绝数（OPT-VER-138——含
                                            ///  mode-mismatch 双向〔D-13〕/slice/
                                            ///  contract/profile 失配）
    std::uint32_t cacheReplayUnavailable = 0; ///< FullHit 但会话内无载荷回放的
                                            ///  次数（跨会话条目——如实重算，
                                            ///  载荷装载归 WP-20-T07 归档面）
};

/**
 * @brief 两级编排运行结果（§8.4 产出面——Quick 批记录、Verified 批记录与
 *        Pareto 前沿三集合显式分离，效力边界由结构承载）。
 *
 * 值语义；线程安全：纯值。
 */
struct TwoStageRunResult {
    RunPhase runPhase = RunPhase::Draft;    ///< 终态运行状态（Completed/Canceled——
                                             ///  Failed/Interrupted 本编排不产出，
                                             ///  环境级失败以异常传播）
    bool runCompleted = false;              ///< 正常完成（取消时 false——已回传
                                             ///  批保留，TASK-01 协作取消语义）
    bool searchEmpty = false;               ///< 搜索空标记（可行集为空——
                                             ///  OPT-SEARCH-EMPTY warning 语义，
                                             ///  非任务不可行；OPT-VER-120）
    std::string searchEmptyToken;           ///< 搜索空稳定 token（＝kOptSearchEmpty；
                                             ///  非搜索空为空串）
    /// Quick 批记录（screeningOnly 恒 true——**绝不进入 pareto 输入**的
    /// 类型学：pareto 只从 verifiedRecords 组装）
    std::vector<TwoStageRunRecord> quickRecords;
    /// Verified 批记录（正式面——幸存集复核产物）
    std::vector<TwoStageRunRecord> verifiedRecords;
    /// Pareto 非支配筛选结果（仅 Verified-Feasible 且激活指标完整候选；
    /// 可行集/非支配集双标记——§7.4；空＝搜索空）
    ParetoFrontResult pareto;
    TwoStageRunAuditCounts audit;           ///< 审计计数（§8.6 记账面）
};

// =====================================================================
// 两级批量编排器（§8.4——Quick 保守淘汰→Verified 复核＋缓存消费）
// =====================================================================

/**
 * @brief 编排依赖注入面（构造注入——L5 装配形态；全部非 owning 只读
 *        指针，调用方保证 run() 期间存活）。
 *
 * 装配校验（构造期）：pipeline/projector/paretoBuilder 必须非空（缺失即
 * 构造抛 std::invalid_argument——装配违约 fail-fast）；cache 可空（无缓存
 * 会话——全部候选如实重算，审计计数如实为零查找；不伪造命中）。
 */
struct TwoStageOrchestratorDeps {
    const StaticHardConstraintPipeline* pipeline = nullptr; ///< T04 单候选管线
                                             ///  （§6.2 十步执行序——mode 差异经 input.mode）
    const ICandidateProjector* projector = nullptr; ///< 候选投影缝（P-OPT-2 替身面）
    const IParetoFrontBuilder* paretoBuilder = nullptr; ///< T05 Pareto（Quick 筛选
                                             ///  与 Verified 前沿同源——稳定排序单点）
    execution::IExecutionCacheCoordinator* cache = nullptr; ///< 会话缓存协调器
                                             ///  （可空＝无缓存会话；命中判定唯一经
                                             ///  evidence judgeCacheHit——由协调器消费，
                                             ///  本编排零判定复制，§8.4/N8）
};

/**
 * @brief Quick/Verified 两级批量编排器（§8.4 流程图的编排侧实现）。
 *
 * run() 执行序（固定——确定性）：
 *   1. 请求校验（validateConfiguration＋目标集非空＋身份面非保留值——
 *      调用方契约违约 fail-fast）；
 *   2. **Quick 批**（mode=Quick）：generateSeededLhsCandidates 生成候选批
 *      （基线恒在）→ 逐候选：冻结切片（config.opt canonical＋补丁身份进
 *      条目 ⇒ sliceId 含全部缓存键要素）→ 投影 → 缓存查找（FullHit 且
 *      会话有同键记录＝短路径回放不重算；否则经 T04 管线评估＋T05 指标
 *      计算）→ 记录（screeningOnly=true）→ 会话底账登记。每候选前查询
 *      ctx.cancellationRequested()（批边界协作取消——true 即停止派发，
 *      产出 runPhase=Canceled、runCompleted=false、已回传批保留）；
 *   3. **Quick 保守筛选**（§8.4 表"淘汰权"行）：Quick 批中 status==Feasible
 *      且激活目标槽位完整的候选，经 paretoBuilder.buildFront 取稳定序，
 *      按**基线优先占席＋稳定序补位**选出至多 maxVerifiedCandidates 个
 *      幸存者（基线 Quick-Feasible 时恒占一席——OPT-11"基线评估作比较
 *      基准"的编排侧保守化：比较基准不得被预算线筛掉；其余名额按稳定序
 *      取非基线候选）。未入选的 Feasible 候选 → ScreenedOut＋淘汰原因
 *      （kRejectQuickScreenedBudget——"预算线"原因记录，OPT-VER-127）；
 *      Infeasible/DataInsufficient/EvaluationFailed 候选**保持自有状态**
 *      不冒充 ScreenedOut（§7.5 区分表——状态词不同）。激活目标槽位缺失
 *      的 Feasible 候选在评估记账时已判 DataInsufficient（§7.4"缺失指标
 *      候选不参与支配比较"的编排侧执行点——候选状态映射见 run 内实现注）；
 *   4. 幸存集为空 → searchEmpty（OPT-VER-120：runPhase=Completed＋
 *      searchEmpty=true＋token——**非任务不可行**，正常完成）；
 *   5. **Verified 复核批**（mode=Verified）：幸存集逐候选同第 2 步（覆盖
 *      投影换 coverage 全必验集——EVI-02；screeningOnly=false）；批间取消
 *      同第 2 步；
 *   6. **可行集→Pareto**：Verified 批中 status==Feasible 且激活指标完整
 *      的候选组装 FeasibleCandidate → buildFront → result.pareto；前沿空
 *      ⇒ 同第 4 步搜索空语义；runPhase=Completed。
 *
 * 为什么 Quick 记录不可能混进 Pareto：pareto 输入只从 verifiedRecords
 * 组装（类型学屏障）＋Quick 记录 formalPassEligible 恒 false（evidence
 * mode-not-verified——checkFormalPassEligibility 第一条件）＋screeningOnly
 * 标记（审计呈现）——三层承载"Quick 不得单独支撑正式通过"（EVI-01 表 1；
 * P-EV-8 词表的编排侧消费）。
 *
 * 确定性：同 (request, deps 行为) ⇒ 同结论面（候选集合/状态/幸存集/
 * pareto 排序——缓存命中只改变审计计数与评估耗时，不改变结论面；
 * OPT-VER-130 的承载）。R1 取消为协作批边界粒度（§12.2 取消行）。
 * 线程约束：实例非线程安全——仅编排线程串行调用 run()（会话底账跨
 * run() 复用＝会话内缓存命中的回放底账；跨 TwoStageEvaluationOrchestrator
 * 实例不共享）。
 */
class TwoStageEvaluationOrchestrator {
public:
    /**
     * @brief 构造（阶段定型＋依赖注入）。
     *
     * @param stage [in] 编排阶段（本批 StageB 管线；StageD 构造不拒——
     *              联合管线未实现的事实由 T04 管线 evaluate 的阶段锁拒绝，
     *              与 constraintsFor 同一码面）
     * @param deps  [in] 依赖注入面（指针在 run() 期间存活）
     *
     * @throws std::invalid_argument pipeline/projector/paretoBuilder 为空
     *         （装配违约 fail-fast；cache 可空）
     */
    TwoStageEvaluationOrchestrator(OptimizationStage stage, TwoStageOrchestratorDeps deps);

    /**
     * @brief 执行一次两级编排（Quick 筛选→Verified 复核→Pareto）。
     *
     * @param request [in] 冻结请求（调用方持有，调用期间有效）
     * @param ctx     [in] 宿主调用上下文（批边界协作取消查询）
     * @return 运行结果（结论面确定；取消时部分批保留）
     *
     * @throws OptimizationError(kOptInputInvalid) 配置/目标集/身份面非法；
     *         kOptPatchIllegal 候选补丁构造拒绝（研究定义非法——生成期
     *         fail-fast）；kOptStageLocked/kOptEvaluatorMissing 经 T04 管线
     *         传播（启动阻塞面）；std::invalid_argument 请求结构违约
     *         （baselineRoot/baselineCv/profile.contentIdentity 保留值）
     */
    TwoStageRunResult run(const TwoStageRunRequest& request, evidence::IEvaluationContext& ctx);

private:
    OptimizationStage m_stage;        ///< 编排阶段（构造定型）
    TwoStageOrchestratorDeps m_deps;  ///< 依赖注入面（构造后只读）
    /// 会话记录底账（sliceId → 记录——缓存 FullHit 短路径的进程内回放
    /// 载荷；R1 会话内命中承诺的承载面，跨 run() 复用、实例私有）。
    std::map<core::ContentIdentity, TwoStageRunRecord> m_sessionRecords;

    // ---- 锁-free 私有步骤（run() 内单线程顺序调用）----
    /// 冻结单候选评估切片（config.opt canonical＋补丁身份进条目——缓存键
    /// 要素进 sliceId；evaluationKey＝kOptStaticScreenKey＋契约版本 1）。
    evidence::InputSlice buildCandidateSlice(const evidence::AnalysisSnapshot& snapshot,
                                             const OptimizationConfiguration& config,
                                             const CandidatePatch& patch) const;
    /// 评估单候选（缓存查找→命中回放/管线评估＋指标记账；mode 决定效力面；
    /// ctx 供 T04 管线③端口调用约定消费）。
    TwoStageRunRecord evaluateCandidate(core::EvaluationMode mode, bool screeningOnly,
                                        const TwoStageRunRequest& request,
                                        const CandidatePatch& patch,
                                        const evidence::InputSlice& slice,
                                        const CandidateProjection& projection,
                                        const evidence::CaseCoverageMatrix& coverage,
                                        evidence::IEvaluationContext& ctx,
                                        TwoStageRunAuditCounts& audit);
};

}  // namespace sdurws::ird::optimization

#endif  // SDURWS_IRD_OPTIMIZATION_EVALUATOR_PORTS_HPP
