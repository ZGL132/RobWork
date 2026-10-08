/**
 * @file   Run.hpp
 * @brief  优化运行结果（OptimizationRunResult）聚合面——运行身份、任务映射、
 *         候选/指标/Pareto/审计的归档载体与"正式导出"资格位（任务 WP-20-T07）。
 *
 * 设计依据：
 *   - units/optimization.md §4.2（六层身份表——运行身份 OptimizationRunId：
 *     "本卡，规范文本 opt-run-<32hex>，Id128 语义；**不进入** core TaskIdentity
 *     五元组；不新造 core Id128 tag（core 冻结 tag 集不动），文本前缀
 *     opt-run- 仅为本域日志/导出可读性"；"OptimizationRunResult.tasks[]
 *     登记映射"）、§4.4 I-OPT-6（历史归档：运行完成后 OptimizationRunResult
 *     与候选评估结果随任务归档至原修订 results/<run-id>/；归档后不可修改；
 *     旧结果由 evidence 判定 Current/Superseded，不删除不改写）、§4.5
 *     （缓存命中不代表结果 Current——当前性唯一归 evidence computeCurrentness，
 *     本聚合不复制第二套判定，N8/N9）、§7.5（候选状态三轴正交）、§10.6
 *     （历史优化结果与当前性——"已中断"呈现不伪装完整结果，NFR-REL-03：
 *     RunPhase=Interrupted ⇒ allowFormalExport=false）、§11.2（ResultEnvelope
 *     与三态正交——"OptimizationRunResult（聚合：候选/指标/Pareto/审计）"）
 *   - 需求 OPT-08（候选归属于 OptimizationRunResult，不产生项目修订——
 *     本类型即候选的**归属容器**：候选是运行结果的成员值，不是项目修订的
 *     对象写入；采用经 Applier.hpp 组合命令另行组装）、CON-02（历史结果
 *     保留为原快照历史证据）、TASK-02（取消/失败/中断结果不得进入正式
 *     可行集——allowFormalExport 位的语义来源）、NFR-COR-02（同输入同
 *     结论面——聚合本身零计算，只做投影与一致性校验）
 *   - 任务契约 tasks/foundation/WP-20-T07.json（acceptance 1："候选归属
 *     OptimizationRunResult，不产生项目修订；预览候选不得修改基线"）
 *
 * ★ 落位范围口径（诚实登记，防扩大——卡 §3.1 布局表行"Run.hpp ←
 *   OptimizationRunResult/运行身份/审计计数"）：本头交付①运行身份类型；
 *   ②运行结果聚合值类型（候选记录/Pareto/审计计数的**归属容器**——记
 *   录类型复用 WP-20-T06 EvaluatorPorts.hpp 既有 TwoStageRunRecord 等，
 *   零第二定义）；③自两级编排产出组装聚合的工厂 assembleRunResult（一
 *   致性校验＋资格位推导）。**不落**：IOptimizationRunController（卡
 *   §12.2——运行意图与结果适配的接口面；R1 编排已由 T06
 *   TwoStageEvaluationOrchestrator 承载、任务提交经 execution 的运行控制
 *   通道随 WP-21 R2——本批零新增消费者，NFR-MNT-04 不预建）；归档写盘
 *   （IResultArchivePort begin/writeBatch/finalize 消费——卡 §14.1 把 §11
 *   归 WP-20-T09 导出/归档面，本头只承载 ArchivePhase 状态词表）；当前性
 *   判定（evidence computeCurrentness 的消费投影归 T09 导出面——OPT-VER-
 *   148 观测点随其任务落位，本批不含）。
 *
 * 背景说明（候选为什么归属运行结果而不是项目——OPT-08 的数据面含义）：
 *   优化候选＝"基线＋补丁"的评估事实，属**研究证据**而非项目权威对象。
 *   候选进项目只有一条路——采用守卫（Applier.hpp）组装两步命令在新方案
 *   分支上产生新修订；除此之外候选永远只存在于 OptimizationRunResult 与
 *   其归档副本中，绝不作为 ObjectWrite 出现（N1 红线）。本类型全部为值
 *   语义成员，聚合后即视为**不可变历史**（I-OPT-6：归档后不可修改；C++
 *   语言层面不强制 const——不可变性是契约纪律，由消费方遵守，归档执行
 *   面 T09 同口径）。
 *
 * 线程约束：全部纯值类型/纯函数；assembleRunResult 可重入。聚合实例的
 *   跨线程共享由调用方保证发布安全（构造后不再修改——同 core 值类型纪律）。
 */

#ifndef SDURWS_IRD_OPTIMIZATION_RUN_HPP
#define SDURWS_IRD_OPTIMIZATION_RUN_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sdurws/ird/core/Evaluation.hpp>   // core::EvaluationMode——任务模式
                                            //  （Quick/Verified；Preview 不入运行映射）
#include <sdurws/ird/core/Identity.hpp>     // core::ProjectId/BranchId/RevisionId/
                                            //  ObjectId/ContentVersion/TaskIdentity——
                                            //  输入身份面与任务五元组
#include <sdurws/ird/core/Digest.hpp>       // core::ContentIdentity——快照身份
#include <sdurws/ird/optimization/EvaluatorPorts.hpp>  // OptimizationConfiguration/
                                            //  TwoStageRunRecord/TwoStageRunResult/
                                            //  TwoStageRunAuditCounts——聚合成员类型
                                            //  （T06 既有契约，零第二定义）
#include <sdurws/ird/optimization/Pareto.hpp>  // ParetoFrontResult——非支配集
                                            //  （T05 既有契约）
#include <sdurws/ird/optimization/Types.hpp>   // RunPhase/OptimizationError——
                                            //  运行状态词表与域异常

namespace sdurws::ird::optimization {

// =====================================================================
// 运行身份（卡 §4.2 身份表"运行身份"行——本域生成、本域自有格式）
// =====================================================================

/**
 * @brief 优化运行身份 OptimizationRunId（规范文本 "opt-run-<32 个小写
 *        十六进制>"——128 位随机身份，Id128 语义）。
 *
 * 为什么不用 core::RunId（"run-"）：core 的 run- 是 **execution 派发的
 * 评估运行**身份（core.md §4.1 类型表"分配者＝execution"），一次优化
 * 运行下挂 1..N 个 execution 任务、各自持有 execution 生成的 run- 身份
 * （§4.2/§4.6）——两层运行身份不同粒度，混用会造成"运行"一词双权威。
 * 同理**不新造** core Id128 tag（core 冻结 tag 集不动——CandidateId 先例
 * 同源口径）：本类型是本域自有值类型，规范文本前缀 opt-run- 仅为本域
 * 日志/导出可读性，不参与 core 解析面。
 *
 * 生成时机：运行启动时由 optimization 生成（§4.2 身份表"生成者：
 * optimization（运行启动时）"）。保留值纪律与 core 同源：全零＝空/
 * 未设置，isValid() 恒 false；generate() 保证非零。
 *
 * 线程安全：generate() 使用 thread_local 引擎（多线程并发生成安全）；
 * 其余纯值操作。
 */
struct OptimizationRunId {
    /// 128 位原始字节；全零＝空（保留值纪律——同 core §4.1 U-1）。
    /// 字节序＝规范文本序（generate 与 parse 使用同一字节序，往返一致）。
    std::array<std::uint8_t, 16> bytes{};

    /// 生成非零随机新值（thread_local 引擎——core generate 同款形态；
    /// 零则重取，保留值不可出现）。线程安全。
    static OptimizationRunId generate();

    /// 严格解析 "opt-run-<32 小写 hex>"：前缀逐字符匹配、长度恰 32、
    /// 字符集仅 [0-9a-f]（大写拒绝）、无前后缀/空白；违约抛
    /// OptimizationError(kOptInputInvalid)（调用方契约违约 fail-fast——
    /// 本域异常轨；不用 core::CoreError，错误归属本域）。
    static OptimizationRunId fromCanonical(std::string_view text);

    /// try 轨解析：失败返回 nullopt 不抛（io/导出的容错收集场景）。
    static std::optional<OptimizationRunId> tryFromCanonical(
        std::string_view text) noexcept;

    /// 规范文本 "opt-run-<32 小写 hex>"（与 parse 构成 parse(format(x))==x
    /// 往返——导出/日志的确定性书写）。
    [[nodiscard]] std::string toCanonical() const;

    /// 非全零（保留值恒 false）。
    [[nodiscard]] bool isValid() const noexcept;

    /// 字节精确相等（附录 D 第 12 项：无容差）。
    bool operator==(const OptimizationRunId& o) const noexcept
    {
        return bytes == o.bytes;
    }
    bool operator!=(const OptimizationRunId& o) const noexcept
    {
        return !(*this == o);
    }
    /// 字节字典序（容器键用；无业务排序语义——同 core Id128 纪律）。
    bool operator<(const OptimizationRunId& o) const noexcept
    {
        return bytes < o.bytes;
    }
};

// =====================================================================
// 运行下挂任务映射（卡 §4.2/§4.6——tasks[] 登记面）
// =====================================================================

/**
 * @brief 运行下挂 execution 任务映射条目（OptimizationRunResult.tasks[]
 *        的元素——卡 §4.2 身份表运行身份行"运行下挂 1..N 个 execution
 *        任务（§9.1），每个任务各自持有 execution 生成的 TaskId/core::RunId；
 *        OptimizationRunResult.tasks[] 登记映射"）。
 *
 * 背景：R1（OPT-B）两级编排为进程内编排（T06——零 worker 派发），任务
 * 五元组由调用方按 execution 提交事实登记；R1 进程内直跑可无任务映射
 * （空集——审计口径如实为零任务，不伪造 execution 登记面）。R2（OPT-D）
 * 经 ITaskScheduler 提交后由运行控制器回填（WP-21）。
 *
 * 线程安全：纯值。
 */
struct RunTaskRecord {
    /// 任务身份五元组（TASK-03——project/branch/revision/run/attempt）；
    /// execution 生成的 TaskId 的承载类型即 core::TaskIdentity。
    core::TaskIdentity task{};
    /// 该任务的评估模式（Quick 筛选批/Verified 复核批——§9.1"Quick 筛选
    /// 与 Verified 复核各为一个任务"；Preview 不作为独立运行任务登记）。
    core::EvaluationMode mode = core::EvaluationMode::Quick;
};

// =====================================================================
// 归档阶段词表（卡 §12.2 Export @pre"运行已归档（archivePhase==Archived）"
// ——运行结果生命周期的归档轴；T09 导出面消费）
// =====================================================================

/**
 * @brief 运行结果归档阶段（§4.4 I-OPT-6"运行完成后随任务归档至原修订
 *        results/<run-id>/"的承载词表）。
 *
 * 两值一次冻结（封闭词表——后续只消费不扩表）：运行结果要么尚在会话内
 * （未归档——不可作正式导出源），要么已完成归档（原修订 results/ 下、
 * 不可变历史）。归档执行（IResultArchivePort 写盘序列）归 execution 触发
 * ＋T09 导出/归档面编排——本词表只承载状态，不承载执行。
 */
enum class ArchivePhase {
    Pending,    ///< 未归档（会话内结果——正式导出阻断，T09 @pre 消费）
    Archived,   ///< 已归档（原修订 results/<run-id>/——不可变历史，CON-02）
};

/**
 * @brief 归档阶段稳定 token（"pending"/"archived"）——导出/审计的确定性
 *        书写（同 Types.hpp 各词表 toToken 纪律）。
 * @param p [in] 归档阶段枚举值
 * @return 稳定 token 视图（编译期字面量，生命周期静态）
 */
std::string_view toToken(ArchivePhase p) noexcept;

// =====================================================================
// 运行结果聚合（卡 §11.2"OptimizationRunResult（聚合：候选/指标/Pareto/
// 审计）"——候选归属容器，OPT-08 数据面）
// =====================================================================

/**
 * @brief 优化运行结果聚合（一次运行的归档载体与候选归属容器）。
 *
 * 成员全部为值语义（T06 记录/T05 Pareto/审计计数的投影，零第二定义）；
 * 聚合经 assembleRunResult 工厂产出（一致性校验单点），构造后按 I-OPT-6
 * 纪律视为不可变历史（修改＝违约——归档副本只增不改，PA-2/CON-02）。
 *
 * 字段分组：
 *   - 身份面：runId（本域）＋project/branch/revision/snapshotId（输入
 *     身份——§4.2 输入身份行：运行绑定原修订，迟到结果绝不写入新项目）；
 *   - 基线锚：baselineRoot/baselineCv（候选身份公式的基线输入——§4.2
 *     候选身份行；Applier 归属核对的立即可用输入，避免重查快照）；
 *   - 配置面：config（config.opt 载荷——canonical 进 sliceId，复现键）；
 *   - 状态面：runPhase（终态——Completed/Canceled 为 R1 编排产出集）＋
 *     archivePhase（归档轴）＋allowFormalExport（正式导出资格位）；
 *   - 任务映射：tasks（execution 登记面——R1 进程内可空）；
 *   - 结论面：candidates（Quick＋Verified 记录合并，保持编排序）＋
 *     pareto（非支配集——仅 Verified-Feasible 输入）＋audit（审计计数）。
 *
 * 线程安全：纯值（构造后只读——I-OPT-6 不可变纪律）。
 */
struct OptimizationRunResult {
    // ---- 身份面 ----
    OptimizationRunId runId{};                 ///< 运行身份（本域生成；须 isValid）
    core::ProjectId project{};                 ///< 运行绑定项目（迟到拒绝核对面之一）
    core::BranchId branch{};                   ///< 运行输入所在分支
    core::RevisionId revision{};               ///< 运行输入修订（结果归属修订＝
                                               ///  基线修订——§10.3 第 3 项核对锚）
    core::ContentIdentity snapshotId{};        ///< 冻结输入快照身份（§4.1；须非保留值）

    // ---- 基线锚（候选身份公式输入——§4.2）----
    core::ObjectId baselineRoot{};             ///< 基线根对象身份（RobotDesign 根；
                                               ///  须非保留值）
    core::ContentVersion baselineCv{};         ///< 基线根对象内容版本（须非保留值）

    // ---- 配置面 ----
    OptimizationConfiguration config{};        ///< config.opt 载荷（canonical 进
                                               ///  sliceId——复现与失效判据，§4.5）

    // ---- 状态面 ----
    RunPhase runPhase = RunPhase::Draft;       ///< 终态运行状态（R1 产出集＝
                                               ///  Completed/Canceled——Types.hpp 词表注）
    ArchivePhase archivePhase = ArchivePhase::Pending; ///< 归档阶段（I-OPT-6；
                                               ///  T09 导出 @pre 消费）
    bool allowFormalExport = false;            ///< 正式导出资格位（TASK-02/§10.6：
                                               ///  仅 Completed 为 true；取消/失败/
                                               ///  中断恒 false——"已中断"不伪装完整
                                               ///  结果，NFR-REL-03）

    // ---- 任务映射 ----
    std::vector<RunTaskRecord> tasks{};        ///< execution 任务登记（§4.6；
                                               ///  R1 进程内编排如实为空集）

    // ---- 结论面 ----
    /// 全部候选评估记录（Quick 批在前、Verified 批在后——保持编排产出序；
    /// screeningOnly 标记区分效力，Quick 记录绝不支撑正式结论——§8.4）。
    std::vector<TwoStageRunRecord> candidates{};
    ParetoFrontResult pareto{};                ///< 非支配集（§7.4——Verified-Feasible
                                               ///  输入；搜索空为空结果＋searchEmpty 语义
                                               ///  由 candidates/audit 侧呈现）
    TwoStageRunAuditCounts audit{};            ///< 审计计数（§8.6——与重放一致，
                                               ///  AT-34；T09 审计 CSV 的数据源）
};

/**
 * @brief 搜索空标记的读取（OPT-VER-120 观测点：runPhase==Completed 但
 *        Pareto 空＝"搜索未找到有效候选"——warning 语义，**非任务不可行**）。
 *
 * 独立读取函数而非成员位：空 Pareto 在"正常完成"与"搜索空"两种语义下
 * 同形，搜索空的判定唯一归 T06 编排（TwoStageRunResult.searchEmpty——
 * 编排语义不复制）；本函数为聚合侧的便捷投影（audit 面交叉核对）。
 *
 * @param result [in] 运行结果聚合
 * @return true＝Completed 且可行集为空（搜索空呈现条件成立）
 *
 * 纯函数；线程安全（可重入）。
 */
bool isSearchEmpty(const OptimizationRunResult& result) noexcept;

// =====================================================================
// 聚合工厂（一致性校验单点——T06 编排产出 → 归档载体）
// =====================================================================

/**
 * @brief 由两级编排产出组装运行结果聚合（assemble——组装＋校验＋资格位
 *        推导；不做任何评估/判定计算——判定唯一归 T04/T05/evidence）。
 *
 * 执行序（固定——确定性）：
 *   1. 身份面校验：runId/baselineRoot/baselineCv/snapshotId 须非保留值、
 *      project/branch/revision 须 isValid——任一违约抛
 *      OptimizationError(kOptInputInvalid)（调用方契约违约 fail-fast）；
 *   2. 编排终态校验：orchestrated.runPhase ∈ {Completed, Canceled}（T06
 *      编排产出集——Failed/Interrupted 以异常传播不落聚合，Types.hpp
 *      RunPhase 注）；越集抛 kOptInputInvalid（消息含实际值 token）；
 *   3. 资格位推导：allowFormalExport = (runPhase == Completed)——取消
 *      结果不具正式资格（TASK-02；"已中断不伪装完整"同源纪律，§10.6）；
 *   4. 投影：candidates = quickRecords ++ verifiedRecords（保持序）；
 *      pareto/audit 逐字段透传（零改写——T06 已是权威产出）；
 *   5. 任务映射/归档阶段按调用方登记透传（R1 进程内编排 tasks 常为空、
 *      archivePhase 恒 Pending——归档登记随 T09/execution 面）。
 *
 * @param runId        [in] 运行身份（本域生成；须 isValid）
 * @param identity     [in] 输入身份四元组（project/branch/revision/
 *                     snapshotId——运行绑定原修订）
 * @param baselineRoot [in] 基线根对象身份（候选身份公式输入；须非保留值）
 * @param baselineCv   [in] 基线根对象内容版本（须非保留值）
 * @param config       [in] 运行配置（T06 编排消费的同一 config——复现键）
 * @param orchestrated [in] 两级编排产出（T06 run() 的返回值——权威结论面）
 * @param tasks        [in] execution 任务登记（R1 进程内可传空集）
 * @param archivePhase [in] 归档阶段（组装时点恒 Pending——归档登记随
 *                     T09/execution 面；参数化以备恢复装载历史结果的
 *                     R2 场景）
 * @return 运行结果聚合（构造后按 I-OPT-6 只读纪律使用）
 *
 * @throws OptimizationError(kOptInputInvalid) 身份面保留值/编排终态越集
 *         （消息含定位——比较型定位纪律 ERR-01）
 *
 * 纯函数；线程安全（可重入）。
 */
OptimizationRunResult assembleRunResult(
    const OptimizationRunId& runId,
    const core::ProjectId& project,
    const core::BranchId& branch,
    const core::RevisionId& revision,
    const core::ContentIdentity& snapshotId,
    const core::ObjectId& baselineRoot,
    const core::ContentVersion& baselineCv,
    const OptimizationConfiguration& config,
    const TwoStageRunResult& orchestrated,
    std::vector<RunTaskRecord> tasks = {},
    ArchivePhase archivePhase = ArchivePhase::Pending);

}  // namespace sdurws::ird::optimization

// std::hash 特化（容器键——FNV-1a 128 字节序口径与 core Id128 同源；
// 置于 sdurws::ird::optimization 命名空间——core IrdCoreIdHash 同款先例，
// 不入 std 自定义模板）。
namespace std {

template <>
struct hash<sdurws::ird::optimization::OptimizationRunId> {
    std::size_t operator()(
        const sdurws::ird::optimization::OptimizationRunId& id) const noexcept
    {
        // FNV-1a 128 位的 (hi,lo) 两半异或合并——core Identity.hpp 的
        // IrdCoreIdHash 同款口径（散列用，非密码学承诺）。
        std::uint64_t hi = 0xcbf29ce484222325ULL;
        std::uint64_t lo = 0x9e3779b97f4a7c15ULL;
        for (std::uint8_t b : id.bytes) {
            hi ^= b;
            hi *= 0x100000001b3ULL;
            lo ^= b;
            lo *= 0x100000001b3ULL;
        }
        return static_cast<std::size_t>(hi ^ lo);
    }
};

}  // namespace std

#endif  // SDURWS_IRD_OPTIMIZATION_RUN_HPP
