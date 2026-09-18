/**
 * @file   CacheCoordinator.cpp
 * @brief  执行缓存协调器实现——联合查找（evidence 判定＋注入编译判定）、
 *         双门槛写入（Completed＋finalize）、LRU 分账淘汰与 D-10 同键
 *         并发合并（§8.2 治理规则表/§10.7 接口）。
 *
 * 设计依据（详见头文件 CacheCoordinator.hpp 文件头）：
 *   - units/execution.md §8.2（缓存查找/命中前检查/写入/并发命中/内容
 *     冲突/淘汰各行的字面落点）、§10.7（接口契约）
 *   - 任务契约 tasks/foundation/EX-T08.json acceptance 3（EX-CCH-1/2）、
 *     acceptance 4（判定逻辑零复制）、acceptance 5（OPT-06 键绑定＋D-10）
 *
 * 判定逻辑零复制的实现面（acceptance 4 的可核查点）：
 *   - 结果缓存兼容性：唯一调用点＝lookup 内 evidence::judgeCacheHit——
 *     verdict/reasons 原样进入 CacheLookup.resultVerdict，无本地复判；
 *   - 模型缓存兼容性：唯一调用点＝lookup 内 m_compileJudge->judge——
 *     verdict/reasons 原样进入 modelVerdict/modelReasons，无本地复判；
 *   - 写入门槛（Completed＋Archived）是**登记事实检查**（读登记表镜像），
 *     不是兼容判定——权威镜像归接纳编排（PA-1）。
 */

#include <sdurws/ird/execution/CacheCoordinator.hpp>

#include <utility>

namespace sdurws::ird::execution {

// =====================================================================
// 构造与注入
// =====================================================================

ExecutionCacheCoordinator::ExecutionCacheCoordinator(IRunRegistry& registry,
                                                     IExecutionDiagnosticsSink& diagnostics,
                                                     UtcClockFn clock)
    : m_registry(registry)
    , m_diagnostics(diagnostics)
    , m_clock(std::move(clock))
{
}

void ExecutionCacheCoordinator::setCompileCacheJudge(const ICompileCacheJudge* judge) noexcept
{
    // 装配期注入（调度线程启动前——EX-T05 装配形态先例）；无需加锁
    // （lookup 消费前装配已完成；此处仍经总锁以支持运行期替换的防御性）。
    std::lock_guard<std::mutex> lock(m_mutex);
    m_compileJudge = judge;
}

// =====================================================================
// 编排内部维护面
// =====================================================================

void ExecutionCacheCoordinator::noteDispatchStarted(const CacheLookupQuery& query,
                                                    core::RunId run)
{
    // 主键面校验（调用方契约——同 lookup 的违约面）。
    if (!query.requestSliceId.isValid()) {
        throw ExecutionError(ExecutionErrorCode::InvalidState,
                             "execution/cache: 派发登记键无效（requestSliceId 为保留值）");
    }
    ResultCacheKey key{query.requestedMode, query.requestSliceId,
                       query.requestContractVersion, query.requestProfileIdentity};
    std::lock_guard<std::mutex> lock(m_mutex);
    // D-10：同键已有在途——保留首个（后续登记为等待该运行），开发通道
    // 留痕（不 fail-fast：并发到达序是合法竞争，处置＝合并而非拒绝）。
    const auto it = m_inFlight.find(key);
    if (it != m_inFlight.end()) {
        if (!(it->second == run)) {
            m_diagnostics.reportDev(
                "execution/cache",
                "D-10 同键并发派发登记：键已有在途 run=" + it->second.toCanonical()
                    + "，本次 run=" + run.toCanonical() + " 记为等待（不重复派发）");
        }
        return;
    }
    m_inFlight.emplace(std::move(key), run);
}

void ExecutionCacheCoordinator::noteRunFinished(core::RunId run)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    clearInFlightLocked(run);
}

void ExecutionCacheCoordinator::storeModelCache(const CompileCacheKeyView& key,
                                                std::vector<std::uint8_t> bytes)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto now = m_clock ? m_clock() : std::chrono::system_clock::now();
    const auto it = m_modelCache.find(key);
    if (it != m_modelCache.end()) {
        if (it->second.bytes.size() == bytes.size()) {
            // 同键同长按幂等刷新处理（内容寻址键同键即同内容——阶段 A
            // 不做逐字节比对，键即内容身份，CON-05；刷新 LRU 即命中语义）。
            it->second.lastHitAtUtc = now;
            return;
        }
        // 内容冲突：保留既有＋开发诊断（不覆盖——§8.2 内容冲突行，
        // project D-14 同精神；同键不同长在内容寻址下属异常输入）。
        m_diagnostics.reportDev(
            "execution/cache",
            "模型缓存同键异载荷冲突（保留既有不覆盖）bytes-old="
                + std::to_string(it->second.bytes.size())
                + " bytes-new=" + std::to_string(bytes.size()));
        return;
    }
    ModelCacheEntry entry;
    entry.bytes = std::move(bytes);
    entry.cachedAtUtc = now;
    entry.lastHitAtUtc = now;
    m_modelCache.emplace(std::move(key), std::move(entry));
    // 入账后执行淘汰（模型账户字节预算——§8.2 淘汰行）。
    evictLocked();
}

void ExecutionCacheCoordinator::storePartialDiagnostic(CacheEntrySummary entry)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto now = m_clock ? m_clock() : std::chrono::system_clock::now();
    entry.cachedAtUtc = now;
    entry.lastHitAtUtc = now;
    // 键从摘要面重建（mode/slice/contract/profile——与正式账户同编址，
    // 账户隔离靠分容器达成：诊断条目永不入 m_resultCache，EX-CCH-2）。
    const core::ContentIdentity sliceKey = entry.summary.sliceId;
    const ResultCacheKey key{entry.summary.mode, entry.summary.sliceId,
                             entry.summary.evaluatorContractVersion,
                             entry.summary.profileContentIdentity};
    // 同键幂等覆盖（部分批次重投递——诊断账户无"只增"义务，短周期保留）。
    m_diagnosticCache[sliceKey].insert_or_assign(key, std::move(entry));
    // 条目数上限淘汰（诊断账户短周期保留策略——§8.2 部分诊断缓存行）。
    evictLocked();
}

// =====================================================================
// lookup——联合查找（evidence 判定＋注入编译判定＋D-10 合并）
// =====================================================================

CacheLookup ExecutionCacheCoordinator::lookup(const CacheLookupQuery& query)
{
    // ---- 调用方契约校验（fail-fast）：无切片身份的查询没有判定意义
    //      （CON-05 内容身份驱动——保留值身份连"未命中"都不可表达）。
    if (!query.requestSliceId.isValid()) {
        throw ExecutionError(ExecutionErrorCode::InvalidState,
                             "execution/cache: 查找键无效（requestSliceId 为保留值）");
    }

    CacheLookup out;
    const ResultCacheKey key{query.requestedMode, query.requestSliceId,
                             query.requestContractVersion, query.requestProfileIdentity};
    const auto now = m_clock ? m_clock() : std::chrono::system_clock::now();

    // ---- 段 1：D-10 同键在途合并（先于一切判定——合并是调度事实，
    //      判定结论此时无关紧要；等待者共享首个运行的完成事件）。
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto it = m_inFlight.find(key);
        if (it != m_inFlight.end()) {
            out.guidance = CacheLookup::Guidance::WaitForInFlightRun;
            out.inFlightRun = it->second;
            return out;
        }
    }

    // ---- 段 2：正式结果缓存判定（§8.2 缓存查找行："以 (requestedMode,
    //      requestSliceId, contractVersion, profileIdentity) 组查询→
    //      evidence 判定"）。候选集＝同 sliceId 的全部条目（身份面失配
    //      〔mode-mismatch 等〕只有在候选被送进判定器时才可观测——精确
    //      键查表会把失配吞成 miss）；判定调用在锁外（evidence 纯函数）。
    evidence::CacheHitQuery judgeRequest;
    judgeRequest.requestedMode = query.requestedMode;
    judgeRequest.requestSliceId = query.requestSliceId;
    judgeRequest.requestContractVersion = query.requestContractVersion;
    judgeRequest.requestProfileIdentity = query.requestProfileIdentity;

    std::vector<CacheEntrySummary> candidates;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto outer = m_resultCache.find(query.requestSliceId);
        if (outer != m_resultCache.end()) {
            candidates.reserve(outer->second.size());
            for (const auto& [k, entry] : outer->second) {
                (void)k;
                candidates.push_back(entry);
            }
        }
    }
    // 键序迭代＝内层复合键升序（确定性）；FullHit 优先、其次首个
    // DiagnosticOnly、再次首个 Incompatible（同查询同结论，NFR-COR-02）。
    std::optional<CacheEntrySummary> hitCandidate;
    std::optional<evidence::CacheHitResult> bestMiss;
    for (CacheEntrySummary& candidate : candidates) {
        const evidence::CacheHitResult verdict
            = evidence::judgeCacheHit(judgeRequest, candidate.summary);
        if (verdict.verdict == evidence::CacheHitResult::FullHit) {
            // FullHit→短路径：不派发 worker，以命中规格构造结果引用
            // （§8.2 缓存查找行）；命中≠当前结果——CacheLookup 无当前性
            // 字段（结构性表达，当前性归 evidence computeCurrentness）。
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                // LRU 刷新（lastHitAtUtc——淘汰序键；锁内仅内存写）。
                const auto outer = m_resultCache.find(query.requestSliceId);
                if (outer != m_resultCache.end()) {
                    const ResultCacheKey hitKey{
                        candidate.summary.mode, candidate.summary.sliceId,
                        candidate.summary.evaluatorContractVersion,
                        candidate.summary.profileContentIdentity};
                    const auto inner = outer->second.find(hitKey);
                    if (inner != outer->second.end()) {
                        inner->second.lastHitAtUtc = now;
                    }
                }
            }
            hitCandidate = std::move(candidate);
            break;
        }
        if (!bestMiss.has_value()
            || (verdict.verdict == evidence::CacheHitResult::DiagnosticOnly
                && bestMiss->verdict != evidence::CacheHitResult::DiagnosticOnly)) {
            // 首个失配结论，或"诊断性优于不兼容"的升级（判定输出原样
            // 保留——reasons 词表/检查序归 evidence，零复制零改写）。
            bestMiss = verdict;
        }
    }
    if (hitCandidate.has_value()) {
        out.guidance = CacheLookup::Guidance::ShortPath;
        out.hitEntry = std::move(hitCandidate);
        out.resultVerdict = evidence::CacheHitResult{};
        out.resultVerdict.verdict = evidence::CacheHitResult::FullHit;
        return out;   // 模型缓存面对短路径无意义（结果复用已决定不派发）
    }

    // ---- 段 3：部分诊断缓存（仅当正式未命中——DiagnosticOnly 可读但
    //      永不替代派发，guidance 恒 DispatchNormal，EX-CCH-2 查找面：
    //      "部分批次仅 DiagnosticOnly"）。
    if (!bestMiss.has_value()
        || bestMiss->verdict != evidence::CacheHitResult::DiagnosticOnly) {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto outer = m_diagnosticCache.find(query.requestSliceId);
        if (outer != m_diagnosticCache.end()) {
            for (const auto& [k, entry] : outer->second) {
                (void)k;
                const evidence::CacheHitResult verdict
                    = evidence::judgeCacheHit(judgeRequest, entry.summary);
                if (verdict.verdict == evidence::CacheHitResult::DiagnosticOnly) {
                    bestMiss = verdict;
                    break;
                }
            }
        }
    }
    // guidance 恒 DispatchNormal——诊断性/不兼容/无候选一视同仁（§8.2：
    // "DiagnosticOnly/Incompatible→正常派发"）；resultVerdict＝判定器
    // 原样输出（无任何候选时为默认值——未命中即派发，原因面对调度无义）。
    if (bestMiss.has_value()) {
        out.resultVerdict = *bestMiss;
    }
    out.guidance = CacheLookup::Guidance::DispatchNormal;

    // ---- 段 4：模型缓存联合查询（§10.7——注入 judge 单点；判定调用在
    //      锁外，条目读取在锁内）。
    if (!query.requestedModelKey.has_value()) {
        out.modelVerdict = CacheLookup::ModelVerdict::NotQueried;
        return out;
    }
    std::optional<CompileCacheKeyView> cachedModelKey;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto it = m_modelCache.find(*query.requestedModelKey);
        if (it != m_modelCache.end()) {
            // 键相等查表＝存储编址命中——兼容判定仍必须经注入 judge
            // （键相等≠复用语义：runtime §9.4 判定表含 DWC 有向语义）；
            // 刷新 LRU 后把登记键交给判定（值拷贝出锁）。
            it->second.lastHitAtUtc = now;
            cachedModelKey = it->first;
        }
    }
    if (!cachedModelKey.has_value()) {
        out.modelVerdict = CacheLookup::ModelVerdict::Miss;   // 无条目→需编译
        return out;
    }
    if (m_compileJudge == nullptr) {
        // 校验面缺失 fail-closed：无判定器不得声称任何复用（EX-T05 提交
        // 验证同口径）；一次性开发留痕（避免每次查找重复告警）。
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_judgeAbsenceReported) {
            m_judgeAbsenceReported = true;
            m_diagnostics.reportDev(
                "execution/cache",
                "ICompileCacheJudge 未注入——模型缓存面按 Incompatible 保守拒绝");
        }
        out.modelVerdict = CacheLookup::ModelVerdict::Incompatible;
        out.modelReasons.push_back("judge-unavailable");
        return out;
    }
    // 判定单点消费（P-EX-3：判定规则在注入侧——本单元零复制；verdict/
    // reasons 原样透传，含 WorkCellOnlyReuse"不得作为完整命中上报"的
    // 消费义务——本面只报告，复用裁决由 Preparing 编排按三态处置）。
    const CompileCacheVerdict modelVerdict
        = m_compileJudge->judge(*query.requestedModelKey, *cachedModelKey);
    switch (modelVerdict) {
    case CompileCacheVerdict::FullReuse:
        out.modelVerdict = CacheLookup::ModelVerdict::FullReuse;
        break;
    case CompileCacheVerdict::WorkCellOnlyReuse:
        out.modelVerdict = CacheLookup::ModelVerdict::WorkCellOnlyReuse;
        break;
    case CompileCacheVerdict::Incompatible:
        out.modelVerdict = CacheLookup::ModelVerdict::Incompatible;
        break;
    }
    return out;
}

// =====================================================================
// storeResult——双门槛写入（Completed＋finalize；EX-CCH-2 的执行侧落点）
// =====================================================================

void ExecutionCacheCoordinator::storeResult(core::RunId run)
{
    // ---- 调用方契约校验：登记表是运行事实的唯一镜像（PA-1）——未登记
    //      运行的缓存登记＝编排违约，fail-fast。
    const std::optional<RunRegistration> record = m_registry.tryRun(run);
    if (!record.has_value()) {
        throw ExecutionError(ExecutionErrorCode::InvalidState,
                             "execution/cache: storeResult 目标运行未登记 run="
                                 + run.toCanonical());
    }

    // ---- 双门槛（§8.2 写入行原文："仅 outcome==Completed 且归档 finalize
    //      成功后登记缓存条目"；失败/取消/中断/归档失败一律不登记——
    //      EX-CCH-2：不登记缓存条目，仅入部分诊断缓存或不入）。两镜像均
    //      由接纳编排维护（RunRegistry noteAdmissionCompleted/
    //      noteArchivePhase）——本面只读镜像不私推。
    if (record->currentState != core::TaskState::Completed
        || record->archivePhase != ArchivePhase::Archived) {
        // 拒绝是**设计的正常路径**而非错误（CON-04/TASK-02 执行侧门槛——
        // 失败/取消结果不入正式缓存）：走开发通道留痕，不发稳定码（缓存
        // 是优化非正确性要件，§8.2"磁盘不足→跳过缓存写入"同款保守定位）。
        m_diagnostics.reportDev(
            "execution/cache",
            "缓存登记拒绝（仅 finalize 产物入缓存）run=" + run.toCanonical()
                + " state=" + std::to_string(static_cast<int>(record->currentState))
                + " archivePhase="
                + std::to_string(static_cast<int>(record->archivePhase)));
        return;
    }

    // ---- 摘要组装（判定面七字段全部取自登记事实——同源不复制语义；
    //      inputBaselineId 为 OPT-06 绑定携带的记录面）。
    // 防御分支：接纳路径必持 evaluation 面（registerRun 校验非空）——
    // 此处为空说明镜像被绕过，按门槛失败处置（不登记不猜值，NFR-COR-03）。
    if (!record->resourceContext.evaluation
        || !record->resourceContext.evaluation->manifestProfile.contentIdentity
                .isValid()) {
        // 镜像被绕过属异常装配——开发通道告警（不登记不猜值，NFR-COR-03）。
        m_diagnostics.reportDev(
            "execution/cache",
            "缓存登记拒绝（登记事实面缺失/Profile 身份为保留值）run="
                + run.toCanonical());
        return;
    }
    CacheEntrySummary entry;
    entry.run = run;
    entry.summary.mode = record->mode;
    entry.summary.outcome = core::TaskOutcome::Completed;
    entry.summary.manifestFinalized = true;   // Archived＝manifest 已发布（D-13）
    entry.summary.sliceId = record->sliceId;
    entry.summary.evaluatorContractVersion = record->contractVersion;
    // Profile 内容身份：登记事实面（派发期自评估器注册清单查得——EX-T04
    // RunEvaluationMaterials.manifestProfile 的 contentIdentity 字段；接纳
    // 路径必有 evaluation 面）。
    entry.summary.profileContentIdentity
        = record->resourceContext.evaluation->manifestProfile.contentIdentity;
    if (record->inputBaselineId.isValid()) {
        entry.summary.inputBaselineId = record->inputBaselineId;
    }
    // 载荷记账：结果条目 payloadBytes=0——载荷驻留 project 归档目录
    // （run 引用装载），头文件 CacheEntrySummary 注同口径。

    const core::ContentIdentity sliceKey = entry.summary.sliceId;
    const ResultCacheKey key{entry.summary.mode, entry.summary.sliceId,
                             entry.summary.evaluatorContractVersion,
                             entry.summary.profileContentIdentity};
    const auto now = m_clock ? m_clock() : std::chrono::system_clock::now();
    entry.cachedAtUtc = now;
    entry.lastHitAtUtc = now;

    std::lock_guard<std::mutex> lock(m_mutex);
    // 内容冲突：同键已有条目→保留既有＋开发诊断（不覆盖——§8.2 内容冲突
    // 行；内容寻址键下同键即同内容，重复登记多为重跑，首个为可追溯原作）。
    auto& account = m_resultCache[sliceKey];
    if (account.count(key) != 0) {
        m_diagnostics.reportDev(
            "execution/cache",
            "缓存同键冲突（保留既有不覆盖）run=" + run.toCanonical() + " slice="
                + key.sliceId.toCanonical());
    } else {
        account.emplace(key, std::move(entry));
        evictLocked();
    }
    // ---- D-10 解除：该运行名下的在途键全部出清（等待者在下一次查找时
    //      命中本条目——共享完成事件闭环）。
    clearInFlightLocked(run);
}

// =====================================================================
// 策略与统计
// =====================================================================

void ExecutionCacheCoordinator::setEvictionPolicy(const EvictionPolicy& policy)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_policy = policy;
    evictLocked();   // 新策略立即生效（可能触发收缩——§10.7"预算/容量"）
}

CacheStats ExecutionCacheCoordinator::stats() const noexcept
{
    // 快照统计（§10.8"stats 并发只读"——锁内聚合，锁外返回值语义）。
    std::lock_guard<std::mutex> lock(m_mutex);
    CacheStats s;
    for (const auto& [slice, entries] : m_resultCache) {
        (void)slice;
        s.resultEntries += entries.size();
        for (const auto& [k, e] : entries) {
            (void)k;
            s.resultBytes += e.payloadBytes;
        }
    }
    s.modelEntries = m_modelCache.size();
    for (const auto& [k, e] : m_modelCache) {
        (void)k;
        s.modelBytes += e.bytes.size();
    }
    for (const auto& [slice, entries] : m_diagnosticCache) {
        (void)slice;
        s.diagnosticEntries += entries.size();
    }
    s.evictedEntries = m_evictedEntries;
    s.inFlightRuns = m_inFlight.size();
    return s;
}

// =====================================================================
// 锁内辅助——LRU 淘汰与在途出清
// =====================================================================

void ExecutionCacheCoordinator::evictLocked()
{
    // ---- 模型账户：字节预算（§8.2"LRU＋总字节预算……分账"）。
    std::uint64_t modelBytes = 0;
    for (const auto& [k, e] : m_modelCache) {
        modelBytes += e.bytes.size();
    }
    while (modelBytes > m_policy.maxModelCacheBytes && !m_modelCache.empty()) {
        // LRU 选择：lastHitAtUtc 最小→cachedAtUtc 最小→键字典序（三级
        // 字典序平局裁决——同状态同序，确定性 NFR-COR-02）。
        auto victim = m_modelCache.begin();
        for (auto it = m_modelCache.begin(); it != m_modelCache.end(); ++it) {
            const auto& v = victim->second;
            const auto& c = it->second;
            if (c.lastHitAtUtc < v.lastHitAtUtc
                || (c.lastHitAtUtc == v.lastHitAtUtc
                    && (c.cachedAtUtc < v.cachedAtUtc
                        || (c.cachedAtUtc == v.cachedAtUtc
                            && it->first < victim->first)))) {
                victim = it;
            }
        }
        modelBytes -= victim->second.bytes.size();
        m_modelCache.erase(victim);
        ++m_evictedEntries;
    }

    // ---- 结果账户：字节预算（阶段 A 载荷记账为 0——预算机制在位，
    //      载荷驻留归档目录的形态下不触发；阶段 B 真实载荷接入即生效）。
    //      两层级结构：展开为 (外层 slice 键, 内层复合键迭代器) 平面视图
    //      后按三级字典序选 victim。
    struct ResultRef {
        std::map<core::ContentIdentity,
                 std::map<ResultCacheKey, CacheEntrySummary>>::iterator outer;
        std::map<ResultCacheKey, CacheEntrySummary>::iterator inner;
    };
    std::vector<ResultRef> resultRefs;
    std::uint64_t resultBytes = 0;
    for (auto outer = m_resultCache.begin(); outer != m_resultCache.end(); ++outer) {
        for (auto inner = outer->second.begin(); inner != outer->second.end(); ++inner) {
            resultBytes += inner->second.payloadBytes;
            resultRefs.push_back(ResultRef{outer, inner});
        }
    }
    while (resultBytes > m_policy.maxResultCacheBytes && !resultRefs.empty()) {
        auto victimIt = resultRefs.begin();
        for (auto it = resultRefs.begin(); it != resultRefs.end(); ++it) {
            const auto& v = victimIt->inner->second;
            const auto& c = it->inner->second;
            if (c.lastHitAtUtc < v.lastHitAtUtc
                || (c.lastHitAtUtc == v.lastHitAtUtc
                    && (c.cachedAtUtc < v.cachedAtUtc
                        || (c.cachedAtUtc == v.cachedAtUtc
                            && it->inner->first < victimIt->inner->first)))) {
                victimIt = it;
            }
        }
        resultBytes -= victimIt->inner->second.payloadBytes;
        victimIt->outer->second.erase(victimIt->inner);
        if (victimIt->outer->second.empty()) {
            m_resultCache.erase(victimIt->outer);   // 空切片桶同步出清
        }
        resultRefs.erase(victimIt);
        ++m_evictedEntries;
    }

    // ---- 诊断账户：条目数上限（短周期保留——§8.2 部分诊断缓存行）。
    //      平面视图同上（选 victim 的三级字典序与结果账户一致）。
    struct DiagRef {
        std::map<core::ContentIdentity,
                 std::map<ResultCacheKey, CacheEntrySummary>>::iterator outer;
        std::map<ResultCacheKey, CacheEntrySummary>::iterator inner;
    };
    std::vector<DiagRef> diagRefs;
    for (auto outer = m_diagnosticCache.begin(); outer != m_diagnosticCache.end(); ++outer) {
        for (auto inner = outer->second.begin(); inner != outer->second.end(); ++inner) {
            diagRefs.push_back(DiagRef{outer, inner});
        }
    }
    while (diagRefs.size() > m_policy.maxDiagnosticEntries) {
        auto victimIt = diagRefs.begin();
        for (auto it = diagRefs.begin(); it != diagRefs.end(); ++it) {
            const auto& v = victimIt->inner->second;
            const auto& c = it->inner->second;
            if (c.lastHitAtUtc < v.lastHitAtUtc
                || (c.lastHitAtUtc == v.lastHitAtUtc
                    && (c.cachedAtUtc < v.cachedAtUtc
                        || (c.cachedAtUtc == v.cachedAtUtc
                            && it->inner->first < victimIt->inner->first)))) {
                victimIt = it;
            }
        }
        victimIt->outer->second.erase(victimIt->inner);
        if (victimIt->outer->second.empty()) {
            m_diagnosticCache.erase(victimIt->outer);
        }
        diagRefs.erase(victimIt);
        ++m_evictedEntries;
    }
}

void ExecutionCacheCoordinator::clearInFlightLocked(core::RunId run)
{
    // 出清该运行名下的全部在途键（D-10 解除——通常恰一条；同运行多键的
    // 编排形态下全部出清，等待者下次查找回正式路径）。
    for (auto it = m_inFlight.begin(); it != m_inFlight.end();) {
        if (it->second == run) {
            it = m_inFlight.erase(it);
        } else {
            ++it;
        }
    }
}

}  // namespace sdurws::ird::execution
