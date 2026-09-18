/**
 * @file   CacheCoordinator.hpp
 * @brief  执行缓存协调器（ExecutionCacheCoordinator）——结果缓存与运行时
 *         模型缓存的存储治理（查找/写入/淘汰/预算）与命中请求的调度边界
 *         （§8.2 四类区分＋§10.7 接口的公共契约面）。
 *
 * 设计依据：
 *   - units/execution.md §8.2（四类复用严格区分；治理规则表：缓存查找/
 *     命中前检查/失效/写入/并发命中/内容冲突/淘汰/磁盘不足/版本升级）、
 *     §10.7（IExecutionCacheCoordinator 接口原文）、§3.1（组成行：
 *     CacheCoordinator.hpp 承载本头全部类型＋ICompileCacheJudge 编译缓存
 *     判定注入）、§3.3（ICompileCacheJudge 注入边界——P-EX-3 处置件）
 *   - 需求 CON-04（缓存按契约判定；部分/失败结果不作正式命中）、CON-05
 *     （切片内容身份驱动——缓存键绑定 sliceId/inputBaselineId）、OPT-06
 *     （Quick/Verified/缓存/检查点平台机制）、TASK-02（执行侧保证——仅
 *     finalize 产物入缓存）、§8.1 表 1（模式效力——不隐式升降级）
 *   - 任务契约 tasks/foundation/EX-T08.json acceptance 3~5（EX-CCH-1/2、
 *     判定逻辑零复制、OPT-06 键绑定＋D-10 同键并发合并）
 *
 * 背景说明（为什么"判定"不在本单元而"存储"在本单元，PA-1 权威唯一）：
 *   "这份已完成的计算能否直接当作本次请求的答案"是证据效力语义，权威
 *   判定式归 evidence judgeCacheHit（三档：FullHit/DiagnosticOnly/
 *   Incompatible，六条件全量收集）；"这份编译产物对该键可否复用"是编译
 *   语义，权威判定式归 runtime judgeCompileCacheCompatibility（三态：
 *   FullReuse/WorkCellOnlyReuse/Incompatible）。runtime 不是 execution 的
 *   登记依赖边（ARCH §3.5，P-EX-3），故编译判定经 ICompileCacheJudge
 *   注入消费；evidence 是登记边，判定函数直接调用。本单元只做**存储、
 *   查找、淘汰、写入门槛与调度边界**——判定逻辑零复制（契约 acceptance 4
 *   的结构性要求：本头与实现中不存在任何一行模式/契约/身份比较的判定
 *   代码，全部经上述两个判定单点）。
 *
 * 调度边界（§8.2 缓存查找行的落点，EX-CCH-1 的"不派发伪装"）：
 *   lookup 的结论只有三种调度含义——①FullHit→短路径（不派发 worker，
 *   以缓存产物构造结果引用；命中也是一个可追溯运行）；②DiagnosticOnly/
 *   Incompatible→正常派发（部分/旧版结果**永不**替代派发）；③
 *   WaitForInFlightRun→登记等待同一运行（D-10 同键并发合并——不重复
 *   派发）。"命中≠当前结果"（CON-04）：FullHit 只声明"对该键可复用"，
 *   当前性归 evidence computeCurrentness 另行计算——本头不携带、不猜测
 *   当前性字段（结构性表达：CacheLookup 无当前性语义的任何成员）。
 *
 * 线程约束：lookup/storeResult/storeModelCache/noteDispatchStarted/
 *   noteRunFinished 按调度线程域约定调用（§10.8"内部转调度串行"）；
 *   stats/setEvictionPolicy 并发安全（内部互斥保护——§10.8"stats 并发
 *   只读""查询不被写阻塞"；全部临界区仅内存操作）。
 */

#ifndef SDURWS_IRD_EXECUTION_CACHECOORDINATOR_HPP
#define SDURWS_IRD_EXECUTION_CACHECOORDINATOR_HPP

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <sdurws/ird/core/Digest.hpp>        // ContentIdentity（身份键面）
#include <sdurws/ird/core/Evaluation.hpp>    // EvaluationMode/TaskState（词表归 core）
#include <sdurws/ird/core/Identity.hpp>      // RunId（命中规格的运行引用）
#include <sdurws/ird/evidence/Compatibility.hpp>  // CachedResultSummary/CacheHitQuery/
                                                 // CacheHitResult/judgeCacheHit（零复制消费）
#include <sdurws/ird/execution/Errors.hpp>   // ExecutionError/ExecutionErrorCode
#include <sdurws/ird/execution/Ports.hpp>    // IExecutionDiagnosticsSink（诊断注入）
#include <sdurws/ird/execution/RunRegistry.hpp>  // IRunRegistry（storeResult 的
                                                 // Completed＋finalize 门槛数据源——本单元内组件）

namespace sdurws::ird::execution {

// =====================================================================
// 编译缓存判定注入（§3.3 原文形态——P-EX-3 处置件；消费任务 EX-T08）
// =====================================================================

/**
 * @brief 编译缓存键视图（§3.3 CompileCacheKeyView 的承载——runtime
 *        CompileCacheKey 的复合键面在注入边界上的值传递形态）。
 *
 * 为什么只有复合键没有分量指纹表：runtime CompileCacheKey 另带 8 条分量
 * 指纹（judge 产出精确差异清单的依据）——那是 runtime 侧判定器的输入
 * 细节，跨注入边界只需"哪份产物"的键身份。注入适配器（L5）在边界内侧
 * 持全键调 runtime judge；execution 存储侧按复合键编址与淘汰。键无指纹
 * 时 runtime judge 按复合键给保守结论（runtime CacheKey.hpp 实现层增补
 * 注"judge 对无指纹键仍可按复合键给出保守结论"——行为契约，非本单元
 * 私定）。
 *
 * 键值语义（runtime §9.4 原文）：workCellKey 非零为合法键；全零键
 * （未 finalize/失败产物）**永不构成命中**——判定面由注入的 judge 拦截
 * （runtime 判定表"部分模型/失败产物"行），本单元不私判、照单存储。
 *
 * 值语义；线程安全：纯值。
 */
struct CompileCacheKeyView {
    /// WC 层复合键（runtime §9.1 公式单点计算的消费面副本）。
    core::ContentIdentity workCellKey;
    /// DWC 层复合键（nullopt＝SkippedNoPhysics/未请求——runtime D-04
    /// 分层键的可空语义原样承载）。
    std::optional<core::ContentIdentity> dynamicWorkCellKey;

    bool operator==(const CompileCacheKeyView& o) const noexcept
    {
        return workCellKey == o.workCellKey
            && dynamicWorkCellKey == o.dynamicWorkCellKey;
    }
    bool operator!=(const CompileCacheKeyView& o) const noexcept
    {
        return !(*this == o);
    }
    /// 字典序（WC 字节→DWC 有无→DWC 字节）——存储容器键用，无业务排序语义。
    bool operator<(const CompileCacheKeyView& o) const noexcept
    {
        if (workCellKey != o.workCellKey) {
            return workCellKey < o.workCellKey;
        }
        if (dynamicWorkCellKey.has_value() != o.dynamicWorkCellKey.has_value()) {
            return dynamicWorkCellKey.has_value()
                < o.dynamicWorkCellKey.has_value();
        }
        if (dynamicWorkCellKey.has_value()
            && *dynamicWorkCellKey != *o.dynamicWorkCellKey) {
            return *dynamicWorkCellKey < *o.dynamicWorkCellKey;
        }
        return false;
    }
};

/**
 * @brief 编译缓存判定结论（§10.7 ICompileCacheJudge 消费的词表——runtime
 *        §9.4 三态，值序与语义逐字对齐；execution 零判定逻辑，只承载）。
 */
enum class CompileCacheVerdict : std::uint8_t {
    FullReuse,          ///< 完整命中——WC/DWC 全对齐（策略/种子/线程不在键内）
    WorkCellOnlyReuse,  ///< 仅 WC 层可复用——不得作为完整命中上报（CON-04）
    Incompatible,       ///< 任一基础分量不等——旧编译器/基线/编码一律拒绝
};

/**
 * @brief 编译缓存判定注入口（§3.3 原文形态——L5 把 runtime
 *        judgeCompileCacheCompatibility 适配为本接口；判定单点在注入侧）。
 *
 * P-EX-3 处置（acceptance 4）：ARCH §3.5 未登记 execution→runtime 边
 * （SA-10 表外边＝构建失败）——本接口把"Preparing 段需要的编译缓存判定"
 * 与"判定能力的来源"（runtime）解耦；如架构侧补登边，适配器可原样退役
 * 为直连（接口形状不变，D-17）。
 *
 * 线程约束：仅调度线程调用（lookup 的模型缓存段）；实现为纯函数形态
 * （可重入——同一键对同结论）。
 */
class ICompileCacheJudge {
public:
    virtual ~ICompileCacheJudge() = default;

    /**
     * @brief 判定两份编译缓存键的复用关系（§3.3 原文签名；判定规则全在
     *        注入侧实现——本单元零复制）。
     *
     * @param requested [in] 当前请求的键（非空引用）
     * @param cached    [in] 缓存侧的键（存储登记值；非空引用）
     * @return 判定结论（三态——判定不对称：requested/cached 互换可改变
     *         DWC 层结论方向，runtime §9.4 有向语义）
     */
    virtual CompileCacheVerdict judge(const CompileCacheKeyView& requested,
                                      const CompileCacheKeyView& cached) const = 0;
};

// =====================================================================
// 结果缓存键与条目（§8.2 编址"sliceId＋模式＋契约版本＋Profile 身份"）
// =====================================================================

/**
 * @brief 正式结果缓存的复合编址键（§8.2 完整结果缓存行："按 sliceId＋
 *        模式＋契约版本＋Profile 身份编址"的字面落点）。
 *
 * 键绑定纪律（OPT-06/CON-05，acceptance 5）：sliceId 是内容身份驱动的主
 * 键面（策略/种子/线程/样本/环境版本已含于其中——evidence D-01/D-11，
 * 无主动失效的结构前提）；inputBaselineId 随条目携带（记录面核对——
 * evidence CachedResultSummary 注："不参与 FullHit 判定，仅随摘要携带"），
 * 存储于 CacheEntrySummary 而不入复合键（判定权威在 evidence，键面若私
 * 加即越界复判）。
 *
 * 值语义；线程安全：纯值。
 */
struct ResultCacheKey {
    core::EvaluationMode mode = core::EvaluationMode::Quick; ///< 评估模式（表 1 效力面）
    core::ContentIdentity sliceId;                           ///< 切片内容身份（主键面）
    std::uint32_t contractVersion = 0;                       ///< 评估器契约版本
    core::ContentIdentity profileIdentity;                   ///< 证据 Profile 内容身份

    bool operator==(const ResultCacheKey& o) const noexcept
    {
        return mode == o.mode && sliceId == o.sliceId
            && contractVersion == o.contractVersion
            && profileIdentity == o.profileIdentity;
    }
    bool operator!=(const ResultCacheKey& o) const noexcept { return !(*this == o); }
    /// 字典序（模式→sliceId→契约版本→Profile）——存储容器键与确定性
    /// 淘汰排序用，无业务排序语义。
    bool operator<(const ResultCacheKey& o) const noexcept
    {
        if (mode != o.mode) {
            return mode < o.mode;
        }
        if (sliceId != o.sliceId) {
            return sliceId < o.sliceId;
        }
        if (contractVersion != o.contractVersion) {
            return contractVersion < o.contractVersion;
        }
        return profileIdentity < o.profileIdentity;
    }
};

/**
 * @brief 缓存条目摘要（§3.1 CacheEntrySummary 的承载——evidence 判定面
 *        七字段＋存储治理面四字段的配对）。
 *
 * 判定面直接复用 evidence::CachedResultSummary（零第二套摘要结构——判定
 * 输入与条目登记同源，杜绝"存储摘要≠判定摘要"的漂移面）。
 *
 * 值语义；线程安全：纯值。
 */
struct CacheEntrySummary {
    /// 产生该条目的运行（FullHit 命中规格——短路径以它构造结果引用；
    /// §8.2 缓存查找行"缓存命中也是一个可追溯运行"的关联键）。
    core::RunId run{};
    /// evidence 判定面七字段（mode/outcome/manifestFinalized/sliceId/
    /// contractVersion/profileIdentity/inputBaselineId——单点复用）。
    evidence::CachedResultSummary summary;
    /// 条目载荷字节（预算分账面：模型缓存条目＝物化字节实测值；结果缓存
    /// 条目＝0——阶段 A 载荷驻留 project 归档目录，命中返回 run 引用由
    /// 查询端口装载，本索引只记账身份；部分诊断条目＝批次字节实测值）。
    std::uint64_t payloadBytes = 0;
    /// 登记时刻（UTC——注入时钟；LRU 初值）。
    std::chrono::system_clock::time_point cachedAtUtc{};
    /// 最近命中时刻（UTC——LRU 淘汰序的键；命中即刷新）。
    std::chrono::system_clock::time_point lastHitAtUtc{};
};

// =====================================================================
// 查找（§10.7 lookup 两结构）
// =====================================================================

/**
 * @brief 缓存查找查询（§10.7 lookup 入参的承载——结果缓存判定面四要素
 *        ＋OPT-06 绑定携带＋模型缓存联合查询键）。
 *
 * 值语义；线程约束：仅调度线程构造传递。
 */
struct CacheLookupQuery {
    // ---- 结果缓存判定面（evidence CacheHitQuery 四要素同名同义）----
    core::EvaluationMode requestedMode = core::EvaluationMode::Quick; ///< 请求模式（不升降级，D-13）
    core::ContentIdentity requestSliceId;                             ///< 请求切片身份
    std::uint32_t requestContractVersion = 0;                         ///< 请求契约版本
    core::ContentIdentity requestProfileIdentity;                     ///< 请求 Profile 身份
    /// 输入基准身份（OPT-06 键绑定携带——记录面核对；不参与判定，evidence
    /// CachedResultSummary 注原文口径）。
    std::optional<core::ContentIdentity> inputBaselineId;
    /// 模型缓存联合查询键（nullopt＝本次查询不查模型缓存——§10.7"联合
    /// 查询"的可选半区；Preparing 段编排按需携带）。
    std::optional<CompileCacheKeyView> requestedModelKey;
};

/**
 * @brief 缓存查找结果（§10.7 CacheLookup 的承载——调度边界的唯一出口）。
 *
 * 消费纪律（§8.2 缓存查找行的机械化）：
 *   - guidance==ShortPath：FullHit——不派发 worker，以 hitEntry 构造短
 *     路径结果引用；命中≠当前结果（CacheLookup 无当前性字段——结构性
 *     表达，当前性归 evidence 另判）；
 *   - guidance==DispatchNormal：正常派发（DiagnosticOnly/Incompatible/
 *     未命中——部分/旧版结果永不替代派发，"不派发伪装"的边界）；
 *   - guidance==WaitForInFlightRun：同键已有在途运行（D-10）——登记为
 *     等待该运行（共享完成事件），不重复派发；inFlightRun 即等待目标。
 *
 * 值语义；线程约束：仅调度线程消费。
 */
struct CacheLookup {
    /// 调度边界结论（三态互斥——见结构注释消费纪律）。
    enum class Guidance {
        ShortPath,          ///< FullHit 短路径（不派发）
        DispatchNormal,     ///< 正常派发（未命中/诊断性/不兼容）
        WaitForInFlightRun, ///< 等待同键在途运行（D-10 合并）
    };

    Guidance guidance = Guidance::DispatchNormal;
    /// 结果缓存判定原样输出（evidence judgeCacheHit 的 verdict＋reasons
    /// ——零复制零改写；词表与检查序归 evidence §8.2。WaitForInFlightRun
    /// 时为默认值——合并结论优先于判定结论）。
    evidence::CacheHitResult resultVerdict;
    /// FullHit 命中条目（ShortPath 时非空——run 引用＋判定面摘要＋
    /// inputBaselineId 绑定核对面）。
    std::optional<CacheEntrySummary> hitEntry;
    /// 模型缓存查询结论（五值——见枚举注；判定语义归注入 judge）。
    enum class ModelVerdict : std::uint8_t {
        NotQueried,        ///< 查询未携带模型键（requestedModelKey==nullopt）
        Miss,              ///< 携带键但无登记条目——需编译（查找面缺失，非
                           ///< 兼容性结论——兼容判定只在两键并存时发生）
        FullReuse,         ///< 注入判定：完整复用
        WorkCellOnlyReuse, ///< 注入判定：仅 WC 层复用（不得作完整命中上报）
        Incompatible,      ///< 注入判定：不兼容（含判定器缺席的保守拒绝——
                           ///< fail-closed，见 ExecutionCacheCoordinator 注）
    };
    ModelVerdict modelVerdict = ModelVerdict::NotQueried;
    /// 模型缓存差异清单（注入 judge 的 reasons 原样透传——词表归 runtime
    /// §9.4；Miss/NotQueried 时为空）。
    std::vector<std::string> modelReasons;
    /// 同键在途运行（WaitForInFlightRun 时非空——D-10 等待目标，共享其
    /// 完成事件）。
    std::optional<core::RunId> inFlightRun;
};

// =====================================================================
// 淘汰策略与统计（§10.7 setEvictionPolicy/stats）
// =====================================================================

/**
 * @brief 淘汰策略（§8.2 淘汰行"LRU＋总字节预算（默认 2 GiB，可配，
 *        实现参数）；模型缓存与结果缓存分账"的承载）。
 *
 * 2 GiB 默认值＝§8.2 原文实现参数（非上游需求值）；分账＝两账户各自
 * 独立预算、独立 LRU 序。部分诊断缓存为条目数上限（短周期保留策略——
 * §8.2 部分诊断缓存行"保留策略（短周期，实现参数）"；字节级治理随阶段
 * B 真实载荷接入再扩展，避免无消费者的参数预建）。
 *
 * 值语义；线程安全：纯值。
 */
struct EvictionPolicy {
    /// 结果缓存账户字节预算（默认 2 GiB＝2×1024³ 字节；实现参数）。
    std::uint64_t maxResultCacheBytes = 2ull * 1024ull * 1024ull * 1024ull;
    /// 模型缓存账户字节预算（默认 2 GiB；实现参数——模型缓存与结果缓存
    /// 分账，互不挤占）。
    std::uint64_t maxModelCacheBytes = 2ull * 1024ull * 1024ull * 1024ull;
    /// 部分诊断缓存条目数上限（短周期保留——超出按 LRU 淘汰；实现参数）。
    std::size_t maxDiagnosticEntries = 64;
};

/**
 * @brief 缓存统计（§10.7 stats 的承载——观测面，不参与判定）。
 *
 * 值语义；线程安全：纯值（快照语义——返回后即与内部状态解耦）。
 */
struct CacheStats {
    std::size_t resultEntries = 0;      ///< 正式结果条目数
    std::uint64_t resultBytes = 0;      ///< 正式结果账户字节（载荷记账）
    std::size_t modelEntries = 0;       ///< 模型缓存条目数
    std::uint64_t modelBytes = 0;       ///< 模型缓存账户字节
    std::size_t diagnosticEntries = 0;  ///< 部分诊断条目数
    std::uint64_t evictedEntries = 0;   ///< 累计淘汰条目数（三账户合计）
    std::size_t inFlightRuns = 0;       ///< 同键并发合并的在途运行数（D-10）
};

// =====================================================================
// IExecutionCacheCoordinator（§10.7 接口原文）
// =====================================================================

/**
 * @brief 执行缓存协调器接口（§10.7 原文四方法）。
 *
 * 生命周期：实现随调度器装配创建（L5 持有）；归档式载荷（envelope 产物
 * 本体）驻留 project 存储域——本接口只治理身份索引与预算（§8.2 完整
 * 结果缓存行的编址语义）。
 */
class IExecutionCacheCoordinator {
public:
    virtual ~IExecutionCacheCoordinator() = default;

    /**
     * @brief 派发前查找（§10.7 原文：结果缓存（evidence judgeCacheHit）
     *        ＋模型缓存（ICompileCacheJudge 注入）联合查询）。
     *
     * 后置：FullHit→返回命中规格（调度器走短路径）；否则 Incompatible/
     * DiagnosticOnly＋reasons；同键在途→WaitForInFlightRun（D-10）。
     *
     * @param query [in] 查询（判定面四要素＋可选绑定/模型键）
     * @return 查找结果（guidance 三态——见 CacheLookup 消费纪律）
     *
     * @throws ExecutionError(InvalidState) 调用方契约违约：requestSliceId
     *         为保留值（无身份的查询没有判定意义）
     */
    virtual CacheLookup lookup(const CacheLookupQuery& query) = 0;

    /**
     * @brief 归档 finalize 成功后登记（§10.7 原文：仅 Completed；失败/
     *     取消不登记——CON-04 执行侧门槛，EX-CCH-2 的判定数据源）。
     *
     * 双门槛（§8.2 写入行"仅 outcome==Completed 且归档 finalize 成功后
     * 登记缓存条目"）：①登记表镜像 currentState==Completed；②archive
     * Phase==Archived（manifest 已发布）。任一不满足→不登记＋开发诊断
     * （结构化拒绝不走异常——可预期分支，TASK-02 的执行侧落点）。
     *
     * @param run [in] 已终结运行的标识（未登记＝调用方契约违约）
     *
     * @throws ExecutionError(InvalidState) run 未登记（登记表是 Completed/
     *         归档阶段的唯一权威镜像——私查不存在的运行＝编排违约）
     */
    virtual void storeResult(core::RunId run) = 0;

    /// 预算/容量（§10.7 原文——实现参数；并发安全）。
    virtual void setEvictionPolicy(const EvictionPolicy& policy) = 0;

    /// 统计快照（§10.7 原文——noexcept 并发只读）。
    virtual CacheStats stats() const noexcept = 0;
};

// =====================================================================
// ExecutionCacheCoordinator——实现本体（§8.2/§10.7）
// =====================================================================

/**
 * @brief 执行缓存协调器实现（IExecutionCacheCoordinator 本体＋编排内部
 *        维护面）。
 *
 * 内部维护面（§10.7 原文四方法之外，编排编排所需——RunRegistry noteXxx
 * 先例，登记单元卡 §15.4）：
 *   - setCompileCacheJudge：注入编译判定器（可空＝模型缓存面按
 *     Incompatible 保守拒绝＋一次性开发留痕——校验面缺失 fail-closed，
 *     不静默放行复用，EX-T05 提交验证同口径）；
 *   - noteDispatchStarted：派发登记同键在途（D-10 合并的数据源——首个
 *     执行、后续等待）；
 *   - noteRunFinished：运行终结解除在途（完成→条目已可命中；失败/取消
 *     →后续查找回到正常派发——等待者不悬挂）；
 *   - storeModelCache：编译产物登记（字节入模型账户预算）；
 *   - storePartialDiagnostic：部分/失败/取消批次登记（仅诊断账户——
 *     永不作正式命中，EX-CCH-2 的存储半区；判定面仍由 evidence 出）。
 *
 * 判定逻辑零复制声明（acceptance 4）：本实现的全部兼容性结论来自
 * evidence judgeCacheHit（结果缓存）与注入 ICompileCacheJudge（模型缓存）
 * 两个判定单点——实现内不存在模式/契约/身份的比较判定代码（键相等性
 * 查表除外——那是存储编址，不是兼容判定）。
 *
 * 确定性来源：淘汰序＝lastHitAtUtc→cachedAtUtc→键字典序（三级字典序，
 * 同状态同序）；冲突处置＝保留既有＋开发诊断（不覆盖——§8.2 内容冲突
 * 行）。
 */
class ExecutionCacheCoordinator final : public IExecutionCacheCoordinator {
public:
    /// UTC 墙钟注入形态（cachedAtUtc/lastHitAtUtc；空＝system_clock::now
    /// ——测试注入固定时钟保证 LRU 序确定性）。
    using UtcClockFn = std::function<std::chrono::system_clock::time_point()>;

    /**
     * @brief 构造（登记表与诊断 sink 必备——非所有权引用，调用方保证
     *        存活期覆盖）。
     *
     * @param registry    [in] 运行登记表（storeResult 的 Completed＋
     *                    finalize 门槛数据源——两镜像由接纳编排维护）
     * @param diagnostics [in] 诊断 sink（非所有权）
     * @param clock       [in] UTC 时钟（空＝system_clock::now）
     */
    ExecutionCacheCoordinator(IRunRegistry& registry,
                              IExecutionDiagnosticsSink& diagnostics,
                              UtcClockFn clock = nullptr);

    // ---- 编排内部维护面（构造/装配期与调度线程域调用）----

    /// 注入编译缓存判定器（空指针＝无判定能力——模型缓存面按 Incompatible
    /// 保守拒绝＋一次性开发留痕）。
    void setCompileCacheJudge(const ICompileCacheJudge* judge) noexcept;

    /// 派发登记同键在途（D-10：首个执行、后续登记为等待该运行；同键重复
    /// 登记保留首个＋开发诊断——不重复派发的记账前提）。
    void noteDispatchStarted(const CacheLookupQuery& query, core::RunId run);

    /// 运行终结解除在途（任意终态——完成后续查命中、失败/取消后续查正常
    /// 派发；未登记在途的运行为 no-op——幂等）。
    void noteRunFinished(core::RunId run);

    /// 编译产物登记（模型账户；字节实测入预算；同键幂等刷新 LRU，异载荷
    /// 冲突保留既有＋开发诊断——D-14 同精神）。
    void storeModelCache(const CompileCacheKeyView& key,
                         std::vector<std::uint8_t> bytes);

    /// 部分/失败/取消批次登记（诊断账户——manifestFinalized=false＋
    /// outcome≠Completed 的摘要由调用方如实填报；判定面经 evidence 出
    /// DiagnosticOnly，永不正式命中，EX-CCH-2 存储半区）。
    void storePartialDiagnostic(CacheEntrySummary entry);

    // ---- IExecutionCacheCoordinator（§10.7 四方法）----
    CacheLookup lookup(const CacheLookupQuery& query) override;
    void storeResult(core::RunId run) override;
    void setEvictionPolicy(const EvictionPolicy& policy) override;
    CacheStats stats() const noexcept override;

private:
    /// 正式结果条目（外层＝sliceId 主键面——§8.2 缓存查找"以请求组查询
    /// →evidence 判定"的候选集索引：判定面失配〔如 mode-mismatch〕只有
    /// 在"同切片不同身份面"的候选被送进判定器时才可观测，精确键查表会
    /// 把失配吞成 miss；内层＝复合键去重，键序确定性）；LRU 面在摘要内。
    std::map<core::ContentIdentity, std::map<ResultCacheKey, CacheEntrySummary>>
        m_resultCache;
    /// 模型缓存条目（键→载荷字节＋LRU 面）。
    struct ModelCacheEntry {
        std::vector<std::uint8_t> bytes;
        std::chrono::system_clock::time_point cachedAtUtc{};
        std::chrono::system_clock::time_point lastHitAtUtc{};
    };
    std::map<CompileCacheKeyView, ModelCacheEntry> m_modelCache;
    /// 部分诊断条目（同编址的独立账户——永不正式命中，§8.2 四类区分；
    /// EX-CCH-2 的存储半区）。
    std::map<core::ContentIdentity, std::map<ResultCacheKey, CacheEntrySummary>>
        m_diagnosticCache;
    /// 同键在途（D-10——键→首个执行的运行）。
    std::map<ResultCacheKey, core::RunId> m_inFlight;
    /// 累计淘汰条目（stats 观测面）。
    std::uint64_t m_evictedEntries = 0;
    /// 当前策略（构造期默认＝EvictionPolicy 缺省值）。
    EvictionPolicy m_policy;
    /// 注入的编译判定器（可空——模型面 fail-closed）。
    const ICompileCacheJudge* m_compileJudge = nullptr;
    /// "判定器缺席"开发留痕的一次性标记（避免每次查找重复告警）。
    bool m_judgeAbsenceReported = false;
    IRunRegistry& m_registry;                     ///< 登记表（非所有权）
    IExecutionDiagnosticsSink& m_diagnostics;     ///< 诊断 sink（非所有权）
    UtcClockFn m_clock;                           ///< UTC 时钟（空＝now）
    /// 总互斥（全部映射的串行点——§10.8"查询不被写阻塞"与 stats 并发
    /// 只读的保证；临界区仅内存操作，判定函数调用在锁外〔纯函数无共享〕）。
    mutable std::mutex m_mutex;

    // ---- 锁内辅助（调用方已持 m_mutex）----
    /// 三账户统一 LRU 淘汰（账户满预算/满容量→逐出最久未用；三级字典序
    /// 平局裁决——确定性）。
    void evictLocked();
    /// 解除某运行名下的全部在途键（noteRunFinished/storeResult 共用）。
    void clearInFlightLocked(core::RunId run);
};

}  // namespace sdurws::ird::execution

#endif  // SDURWS_IRD_EXECUTION_CACHECOORDINATOR_HPP
