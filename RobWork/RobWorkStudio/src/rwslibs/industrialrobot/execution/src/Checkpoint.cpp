/**
 * @file   Checkpoint.cpp
 * @brief  检查点协调器实现——写出编排（摘要计算→归档端口三段）、恢复调度
 *         （完整性校验→evidence 兼容判定→拒绝转译）与会话索引（§8.1/§10.6）。
 *
 * 设计依据（详见头文件 Checkpoint.hpp 文件头）：
 *   - units/execution.md §8.1（写出流程图/损坏/不兼容/恢复失败保留/清理
 *     失败不破坏已提交检查点）、§10.6（接口契约逐方法前置后置）
 *   - 任务契约 tasks/foundation/EX-T08.json acceptance 1（EX-CKP-1~3）、
 *     acceptance 2（P-EX-9：旧版本不迁移）
 *
 * 实现要点（与设计条目的一一对应）：
 *   1. 写出＝批次校验→逐文件摘要＋复合摘要（computePayloadDigest 单点）
 *      →归档端口 begin→writeBatch→finalize（manifest 发布＝"完整"）→
 *      会话索引登记。端口环境类失败→abandon 结束责任＋ArchiveFailed
 *      （批次残留保留——不删除；§8.1"失败不破坏已提交检查点"）。
 *   2. 恢复＝索引查找→磁盘只读装载批次（目录取登记 archiveDir 原值——
 *      A8 不重新推导）→逐文件核对（损坏→EX-CHECKPOINT-CORRUPT＋会话内
 *      废弃标记——只读登记，不删盘）→evidence judgeCheckpointCompatibility
 *      （判定单点零复制；拒绝→EX-CHECKPOINT-INCOMPATIBLE＋reasons 原样
 *      透传——P-EX-9 不迁移）→resumable=false→NotResumable 拒绝
 *      （EX-CKP-3：原检查点完整保留）。
 *   3. 全部拒绝路径零磁盘写（结构性保证"原文件保留"——文件头声明）。
 *
 * 线程模型：writeCheckpoint/restore/discard 仅调度线程（P-PR-4 域——
 *   归档端口调用遵守单侧冻结）；listCompatible/indexedCount 并发只读，
 *   经 m_indexMutex 保护（临界区仅内存操作，无 I/O 无回调，无死锁面）。
 */

#include <sdurws/ird/execution/Checkpoint.hpp>

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <system_error>
#include <utility>

namespace sdurws::ird::execution {

namespace {

// ---------------------------------------------------------------------
// 诊断辅助（Admission.cpp admissionDiag 同款形态——稳定码＋开发双通道）
// ---------------------------------------------------------------------

/// 稳定码诊断（瞬时开发诊断承载——subject 空，core §4.8 同款；context
/// 固定 "execution/checkpoint" 供日志检索）。
core::DiagnosticRecord checkpointDiag(const char* stableCode, std::string cause,
                                      std::string action)
{
    return core::DiagnosticRecord::make(stableCode, std::nullopt, std::nullopt,
                                        std::nullopt, "execution/checkpoint",
                                        std::move(cause), std::move(action));
}

/// 摘要非零判定（Digest256 为 std::array 裸别名，无成员函数——core 的
/// "全零＝空保留值"纪律的本地机械实现；两处消费：记录校验/测试辅助同款）。
bool nonZeroDigest(const core::Digest256& d) noexcept
{
    for (const std::uint8_t b : d) {
        if (b != 0) {
            return true;
        }
    }
    return false;
}

/// 单文件的 SHA-256（内容寻址引用与损坏定位共用——core ContentDigester
/// 单点，§8.1 payloadDigest 行"经 core ContentDigester"）。
core::Digest256 fileDigest(const std::vector<std::uint8_t>& bytes)
{
    core::ContentDigester digester;
    // 空文件也走同一摘要路径（update(nullptr,0) 契约允许——空文件是合法
    // 工件，project ArchiveItem 注同口径）。
    digester.update(bytes.empty() ? nullptr : bytes.data(), bytes.size());
    return digester.finalize();
}

/// system_clock 时刻→ISO-8601 UTC 文本（RunManifest.finalizedAtUtc 契约
/// "ISO-8601 UTC 文本"——project finalize 只透传不重算，格式责任在调用方；
/// Admission.cpp iso8601Utc 同款实现，两域各自自持不共享私有件）。
std::string iso8601Utc(std::chrono::system_clock::time_point tp)
{
    const std::time_t t = std::chrono::system_clock::to_time_t(tp);
    std::tm tmValue{};
#ifdef _WIN32
    gmtime_s(&tmValue, &t);   // MSVC 安全版（线程局部分解）
#else
    gmtime_r(&t, &tmValue);
#endif
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ",
                  tmValue.tm_year + 1900, tmValue.tm_mon + 1, tmValue.tm_mday,
                  tmValue.tm_hour, tmValue.tm_min, tmValue.tm_sec);
    return buf;
}

}  // namespace

// =====================================================================
// CheckpointRecord——结构有效性
// =====================================================================

bool CheckpointRecord::isValid() const noexcept
{
    // 主键（§8.1 checkpointId 行：seq≥1、run 非零）。
    if (!checkpointId.isValid()) {
        return false;
    }
    // 五元组（§8.1 task 行：有效性同 core TaskIdentity）。
    if (!task.isValid()) {
        return false;
    }
    // 四身份非零（§8.1 snapshotId/sliceId/policyIdentity/nameMapIdentity 行）。
    if (!snapshotId.isValid() || !sliceId.isValid() || !policyIdentity.isValid()
        || !nameMapIdentity.isValid()) {
        return false;
    }
    // 评估键词形（§8.1 evaluatorKey 行"已注册键"的执行侧可校验半区——
    // 词形闸门单点归 evidence isValidEvaluationKey；注册核对归派发编排）。
    if (!evidence::isValidEvaluationKey(evaluatorKey)) {
        return false;
    }
    // 版本/复现要素边界（§8.1 各行合法列：格式版本≥1、threadCount≥1、
    // completed≤total；契约版本与 randomSeed 不设下界——0 是合法承载值
    // 〔无随机性〕，evidence 判定面按相等条件消费）。
    if (checkpointFormatVersion < 1 || threadCount < 1
        || completedUnits > totalUnits) {
        return false;
    }
    // 批次载荷引用面：本函数**不校验**——载荷面（intermediateStateRef/
    // payloadDigest）是写出编排从实际批次派生的产物而非调用方申报事实
    // （§8.1 两行的"所有权＝execution 写出/project 编址"），请求内预填值
    // 一律被覆盖；补全后的完整记录在 writeCheckpoint 内另行强校验
    // （引用非空＋复合摘要非零——构造保证，防御性保留）。
    return true;
}

// =====================================================================
// 复合摘要与核对（写/读两侧唯一计算点）
// =====================================================================

core::Digest256 computePayloadDigest(const std::vector<CheckpointBatchFile>& files)
{
    // 复制为 (relPath, fileDigest) 视图后按 relPath 字典序排序——排序键
    // 固定（NFR-COR-02：同集合必得同复合摘要，与请求顺序无关）。
    std::vector<std::pair<std::string, core::Digest256>> ordered;
    ordered.reserve(files.size());
    for (const CheckpointBatchFile& f : files) {
        ordered.emplace_back(f.relPath, fileDigest(f.bytes));
    }
    std::sort(ordered.begin(), ordered.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    // 复合编码：relPath ‖ 0x00 ‖ 文件摘要（32B），逐文件追加进同一个
    // ContentDigester（编码约定见头文件声明注——分隔符防拼接歧义，以每
    // 文件摘要进复合使损坏可定位到文件粒度）。
    core::ContentDigester digester;
    static constexpr std::uint8_t kSeparator = 0x00;
    for (const auto& entry : ordered) {
        digester.update(entry.first.data(), entry.first.size());
        digester.update(&kSeparator, 1);
        digester.update(entry.second.data(), entry.second.size());
    }
    return digester.finalize();
}

std::string verifyCheckpointFiles(const std::vector<CheckpointBatchFile>& files,
                                  const CheckpointRecord& record)
{
    // 建立磁盘装载侧的 relPath→(digest,size) 查找表——记录引用逐条核对
    // （引用序＝写出声明序，装载序无关；缺失/多出/不符均判损坏）。
    std::map<std::string, std::pair<core::Digest256, std::uint64_t>> loaded;
    for (const CheckpointBatchFile& f : files) {
        loaded.emplace(f.relPath,
                       std::make_pair(fileDigest(f.bytes),
                                      static_cast<std::uint64_t>(f.bytes.size())));
    }

    // 逐条引用核对：文件缺失或摘要/字节数不符→返回该文件名（首个不符项
    // ——EX-CKP-1 诊断 cause 的损坏定位）。
    for (const CheckpointFileRef& ref : record.intermediateStateRef) {
        const auto it = loaded.find(ref.relPath);
        if (it == loaded.end()) {
            return ref.relPath;   // 磁盘缺文件＝不完整/被删——按损坏拒绝
        }
        if (it->second.first != ref.sha256
            || it->second.second != ref.sizeBytes) {
            return ref.relPath;   // 摘要或字节数不符＝内容被篡改
        }
    }
    // 磁盘侧多出的文件同样破坏"批次只增且完整"的声明面（防部分替换）。
    if (loaded.size() != record.intermediateStateRef.size()) {
        return std::string("<batch-file-set-mismatch>");
    }
    // 复合摘要终验（§8.1 payloadDigest 行"不符→Corrupt"的字面落点）。
    const core::Digest256 composite = computePayloadDigest(files);
    if (composite != record.payloadDigest) {
        return std::string("<payload-digest-mismatch>");
    }
    return {};   // 全部相符
}

// =====================================================================
// 构造
// =====================================================================

CheckpointCoordinator::CheckpointCoordinator(project::IResultArchivePort& archivePort,
                                             IExecutionDiagnosticsSink& diagnostics,
                                             UtcClockFn clock)
    : m_archivePort(archivePort)
    , m_diagnostics(diagnostics)
    , m_clock(std::move(clock))
{
}

// =====================================================================
// writeCheckpoint——批次校验→摘要→归档端口三段→索引登记
// =====================================================================

CheckpointWriteResult CheckpointCoordinator::writeCheckpoint(
    const CheckpointWriteRequest& request)
{
    // ---- 调用方契约校验（fail-fast——AGENTS §3 二分的"调用方错误"侧）----
    // ①主键与记录结构（isValid 覆盖 §8.1 合法列的结构面；载荷面不参与
    //   ——以实际批次重算为准，见下）。
    if (!request.record.isValid() || request.files.empty()
        || request.archiveDir.empty()) {
        throw ExecutionError(ExecutionErrorCode::InvalidState,
                             "execution/checkpoint: 写出请求结构无效（记录/批次/归档位置）");
    }
    // ②批次路径面（relPath 是磁盘编址与 manifest 完整性清单的键——空
    //   路径无法编址；白名单/保留名细节归归档端口裁决，这里只挡空值）。
    for (const CheckpointBatchFile& f : request.files) {
        if (f.relPath.empty()) {
            throw ExecutionError(ExecutionErrorCode::InvalidState,
                                 "execution/checkpoint: 批次文件 relPath 为空");
        }
    }
    // ②同 (run,seq) 重发布拒绝（正式检查点幂等可续、不可重写——§8.1
    //   "临时/在途可被同 seq 重写"只适用批次未发布的窗口，经归档端口
    //   只增幂等承接；manifest 已发布后再写＝编排协议错误）。
    {
        std::lock_guard<std::mutex> lock(m_indexMutex);
        if (m_index.count(request.record.checkpointId) != 0) {
            throw ExecutionError(
                ExecutionErrorCode::InvalidState,
                "execution/checkpoint: 同 (run,seq) 已有正式检查点，拒绝重发布 seq="
                    + std::to_string(request.record.checkpointId.sequence));
        }
    }

    // ---- 第一步：批次校验＋摘要计算（§10.6"批次校验"）----
    // 组装正式记录：载荷面（intermediateStateRef/payloadDigest）以实际
    // 批次重算——请求内预填值被覆盖（头文件声明的防伪造面）。信封版本与
    // 记录 schema 版本由本单元无条件打**当前**戳（§8.1 两字段的所有权列
    // "execution（信封）"；请求值不采信——旧版本记录只能来自装载历史
    // 存储，不由写出面伪造，P-EX-9 的"无迁移"由此成立：本实现写出的
    // 永远是当前版本，读取面的版本拒绝归 evidence 判定单点）。创建时刻
    // 经注入时钟（测试确定性——RunRegistry 同款纪律）。
    CheckpointRecord record = request.record;
    record.checkpointFormatVersion = kCheckpointRecordVersion;
    record.recordVersion = kCheckpointSchemaVersion;
    record.intermediateStateRef.clear();
    record.intermediateStateRef.reserve(request.files.size());
    for (const CheckpointBatchFile& f : request.files) {
        CheckpointFileRef ref;
        ref.relPath = f.relPath;
        ref.sha256 = fileDigest(f.bytes);
        ref.sizeBytes = static_cast<std::uint64_t>(f.bytes.size());
        record.intermediateStateRef.push_back(std::move(ref));
    }
    record.payloadDigest = computePayloadDigest(request.files);
    record.createdAtUtc = m_clock ? m_clock() : std::chrono::system_clock::now();
    // 重算后的记录必有效（批次非空＋摘要非零已由上方契约校验保证）——
    // 补全后的完整记录另行强校验载荷面（isValid 不覆盖的"引用非空＋
    // 复合摘要非零"两行，§8.1 字段表必填列）；失败说明上游字段被并发
    // 改动，不带病写盘。
    if (!record.isValid() || record.intermediateStateRef.empty()
        || !nonZeroDigest(record.payloadDigest)) {
        throw ExecutionError(ExecutionErrorCode::InvalidState,
                             "execution/checkpoint: 批次摘要计算后记录仍无效（内部不一致）");
    }

    // ---- 第二步：归档端口三段（begin→writeBatch→finalize；§8.1 写出
    //      位置规则——manifest 发布归 project，本单元零直接磁盘写）。
    //      端口异常语义：StoreError＝环境类（结构化 ArchiveFailed 返回）；
    //      std::invalid_argument＝调用方契约违约（原样传播 fail-fast）。
    project::ArchiveRequest archiveRequest;
    archiveRequest.task = record.task;
    archiveRequest.runDir = request.archiveDir;
    // runKind/evaluationKey 透传 manifest（project §4.4.7 token 口径——
    // 检查点运行用统一 kind 标记，恢复扫描可据此区分结果运行与检查点）。
    archiveRequest.runKind = "checkpoint";
    archiveRequest.evaluationKey = record.evaluatorKey;

    CheckpointWriteResult result;
    result.checkpointId = record.checkpointId;

    project::ArchiveSessionRef session;
    try {
        session = m_archivePort.begin(archiveRequest);
    } catch (const project::StoreError& e) {
        // 环境类失败（上下文关闭/只读/磁盘/冲突）——结构化 ArchiveFailed
        // ＋稳定码诊断（EX-ARCHIVE-FAILED 归档失败族——§3.4 码表），不抛
        // （可预期失败不走异常；§10.8 错误语义行）。无会话须清理。
        result.outcome = CheckpointWriteResult::Outcome::ArchiveFailed;
        result.diagnostics.push_back(checkpointDiag(
            "EX-ARCHIVE-FAILED",
            "检查点归档 begin 失败 seq="
                + std::to_string(record.checkpointId.sequence) + " detail=" + e.what(),
            "检查存储上下文写权限与磁盘状态后重试；已提交批次不受影响"));
        m_diagnostics.report(result.diagnostics.back());
        return result;
    }

    // 批次写入（project::ArchiveBatch 原样承载——relPath 白名单/幂等/
    // 冲突裁决归端口，本单元不重复实现；字节按值拷贝——请求为 const，
    // 大载荷的移动优化归上层编排的移动构造）。
    project::ArchiveBatch batch;
    batch.items.reserve(request.files.size());
    for (const CheckpointBatchFile& f : request.files) {
        project::ArchiveItem item;
        item.relPath = f.relPath;
        item.bytes = f.bytes;
        batch.items.push_back(std::move(item));
    }
    const project::ArchiveStatus writeStatus = m_archivePort.writeBatch(session, batch);
    if (!writeStatus.ok) {
        // 失败即 abandon（结束归档责任；批次残留保留为"未完成"——project
        // §10.1 不删除，§8.1"失败不破坏已提交检查点"由只增语义兑现）。
        m_archivePort.abandon(session, project::ArchiveEndReason::Failed);
        result.outcome = CheckpointWriteResult::Outcome::ArchiveFailed;
        result.diagnostics.push_back(checkpointDiag(
            "EX-ARCHIVE-FAILED",
            "检查点批次写入失败 seq=" + std::to_string(record.checkpointId.sequence),
            "检查磁盘状态后从更早检查点或全量重跑；批次残留保留为未完成"));
        m_diagnostics.report(result.diagnostics.back());
        return result;
    }

    // manifest 组装（§4.4.7 字段面：五元组＋批次完整性清单＋透传 token；
    // finalizedAtUtc 由本单元产出——project finalize 只透传不重算）。
    project::RunManifest manifest;
    manifest.taskIdentity = record.task;
    manifest.items.reserve(record.intermediateStateRef.size());
    for (const CheckpointFileRef& ref : record.intermediateStateRef) {
        project::RunManifestItem item;
        item.relPath = ref.relPath;
        // 摘要十六进制（小写 64 字符——RunManifestItem.sha256 契约面；
        // 逐字节两位小写十六进制，与 project 归档核对口径一致）。
        static constexpr char kHex[] = "0123456789abcdef";
        item.sha256.reserve(ref.sha256.size() * 2);
        for (const std::uint8_t b : ref.sha256) {
            item.sha256.push_back(kHex[b >> 4]);
            item.sha256.push_back(kHex[b & 0x0F]);
        }
        item.sizeBytes = ref.sizeBytes;
        manifest.items.push_back(std::move(item));
    }
    manifest.runKind = archiveRequest.runKind;
    manifest.evaluationKey = archiveRequest.evaluationKey;
    manifest.finalizedAtUtc = iso8601Utc(m_clock ? m_clock()
                                                 : std::chrono::system_clock::now());
    // manifestDigest 由 project 计算回填（ArchivePort finalize 契约——
    // 调用方该字段不参与）。
    const project::ArchiveStatus finalStatus = m_archivePort.finalize(session, manifest);
    if (!finalStatus.ok) {
        m_archivePort.abandon(session, project::ArchiveEndReason::Failed);
        result.outcome = CheckpointWriteResult::Outcome::ArchiveFailed;
        result.diagnostics.push_back(checkpointDiag(
            "EX-ARCHIVE-FAILED",
            "检查点 manifest 发布失败 seq=" + std::to_string(record.checkpointId.sequence),
            "检查磁盘状态后重试或从更早检查点恢复；批次残留保留为未完成"));
        m_diagnostics.report(result.diagnostics.back());
        return result;
    }

    // ---- 第三步：会话索引登记（正式检查点——恢复调度的查找基准；同一
    //      调度线程串行域内无并发重入，构造期已拒同主键重复落位）。
    {
        std::lock_guard<std::mutex> lock(m_indexMutex);
        m_index.emplace(record.checkpointId,
                        IndexedCheckpoint{std::move(record), request.archiveDir, false});
    }
    result.outcome = CheckpointWriteResult::Outcome::Published;
    return result;
}

// =====================================================================
// restore——完整性校验→evidence 判定→拒绝转译（全部拒绝零磁盘副作用）
// =====================================================================

CheckpointRestoreResult CheckpointCoordinator::restore(
    const CheckpointRestoreRequest& request)
{
    // ---- 调用方契约校验（fail-fast）。
    if (!request.checkpointId.isValid()) {
        throw ExecutionError(ExecutionErrorCode::InvalidState,
                             "execution/checkpoint: 恢复请求主键无效");
    }

    CheckpointRestoreResult result;
    CheckpointRecord record;   ///< 局部工作副本（只在 Restored 出口回填 result
                               ///< ——拒绝路径的结果不携带恢复规格，调用方
                               ///< 不可误用部分规格）。

    // ---- 第一步：索引查找（会话内——跨会话重建归 project 恢复扫描，
    //      §5.4"无第二账本"；未知主键＝结构化 UnknownCheckpoint 而非异常：
    //      恢复编排对"索引中没有"有合法处置路径——全量重跑）。
    std::filesystem::path archiveDir;
    {
        std::lock_guard<std::mutex> lock(m_indexMutex);
        const auto it = m_index.find(request.checkpointId);
        if (it == m_index.end()) {
            result.outcome = CheckpointRestoreResult::Outcome::UnknownCheckpoint;
            return result;
        }
        if (it->second.discarded) {
            // 已废弃（显式 discard 或先前损坏自动废弃）——拒绝且不再装载。
            result.outcome = CheckpointRestoreResult::Outcome::Discarded;
            return result;
        }
        // 记录与归档位置拷贝出临界区——后续磁盘 I/O 与判定不持锁
        // （锁内零 I/O 纪律）；装载目录取登记 archiveDir 原值（A8）。
        record = it->second.record;
        archiveDir = it->second.archiveDir;
    }

    // ---- 第二步：完整性校验（§8.1"损坏检查点：装载/恢复期摘要校验失败
    //      →EX-CHECKPOINT-CORRUPT＋诊断（定位文件）"）。磁盘只读装载——
    //      本面对磁盘的全部读动作，无任何写通道（EX-CKP-1/3"原文件保留"
    //      的结构保证）。
    std::vector<CheckpointBatchFile> loadedFiles;
    loadedFiles.reserve(record.intermediateStateRef.size());
    for (const CheckpointFileRef& ref : record.intermediateStateRef) {
        const std::filesystem::path filePath = archiveDir / ref.relPath;
        std::error_code ec;
        const std::uintmax_t size = std::filesystem::file_size(filePath, ec);
        if (ec) {
            // 装载失败（缺文件/不可读）＝完整性未过——同 Corrupt 路径
            // （诊断 cause 携带系统细节，定位面一致）。
            result.outcome = CheckpointRestoreResult::Outcome::Corrupt;
            result.diagnostics.push_back(checkpointDiag(
                "EX-CHECKPOINT-CORRUPT",
                "检查点批次文件不可读 seq="
                    + std::to_string(record.checkpointId.sequence) + " file="
                    + ref.relPath + " detail=" + ec.message(),
                "该检查点已废弃（原文件保留）；从更早检查点恢复或全量重跑"));
            m_diagnostics.report(result.diagnostics.back());
            markDiscarded(request.checkpointId);
            return result;
        }
        CheckpointBatchFile f;
        f.relPath = ref.relPath;
        f.bytes.resize(static_cast<std::size_t>(size));
        // 二进制只读装载（ifstream——读取失败按损坏处置，不重试不猜测：
        // 恢复面对"读不完整"只能保守，NFR-COR-03 不静默通过）。
        std::ifstream in(filePath, std::ios::binary);
        if (!in
            || !in.read(reinterpret_cast<char*>(f.bytes.data()),
                        static_cast<std::streamsize>(size))) {
            result.outcome = CheckpointRestoreResult::Outcome::Corrupt;
            result.diagnostics.push_back(checkpointDiag(
                "EX-CHECKPOINT-CORRUPT",
                "检查点批次文件读取失败 seq="
                    + std::to_string(record.checkpointId.sequence) + " file="
                    + ref.relPath,
                "该检查点已废弃（原文件保留）；从更早检查点恢复或全量重跑"));
            m_diagnostics.report(result.diagnostics.back());
            markDiscarded(request.checkpointId);
            return result;
        }
        loadedFiles.push_back(std::move(f));
    }
    // 逐文件核对＋复合摘要终验（verifyCheckpointFiles 单点——损坏定位）。
    const std::string corruptFile = verifyCheckpointFiles(loadedFiles, record);
    if (!corruptFile.empty()) {
        result.outcome = CheckpointRestoreResult::Outcome::Corrupt;
        result.diagnostics.push_back(checkpointDiag(
            "EX-CHECKPOINT-CORRUPT",
            "检查点完整性校验失败 seq="
                + std::to_string(record.checkpointId.sequence) + " 定位="
                + corruptFile,
            "该检查点已废弃（原文件保留不删除）；从更早检查点恢复或全量重跑"));
        m_diagnostics.report(result.diagnostics.back());
        // §8.1"该检查点废弃（不删除原文件——只读登记废弃标记）"：会话内
        // 内存标记，磁盘零副作用（EX-CKP-1"目录内容不变"由此保证）。
        markDiscarded(request.checkpointId);
        return result;
    }

    // ---- 第三步：evidence 兼容判定（判定单点零复制——acceptance 4；
    //      P-EX-9：区间外/任一失配一律 Incompatible 拒绝，不迁移）。
    evidence::CacheHitQuery judgeRequest;
    judgeRequest.requestSliceId = request.requestSliceId;
    judgeRequest.requestContractVersion = request.requestContractVersion;
    evidence::CheckpointSummary summary;
    summary.evaluatorKey = record.evaluatorKey;
    summary.sliceId = record.sliceId;
    summary.evaluatorContractVersion = record.evaluatorContractVersion;
    summary.checkpointFormatVersion = record.checkpointFormatVersion;
    // 完整性标记：第二步摘要核对已通过（true——判定式第五条件的执行侧
    // 校验半区，evidence 只消费标记不重算摘要——职责分界）。
    summary.integrityVerified = true;
    const evidence::CheckpointCompatibilityResult verdict
        = evidence::judgeCheckpointCompatibility(judgeRequest, summary);
    if (verdict.verdict == evidence::CheckpointCompatibilityResult::Incompatible) {
        // EX-CKP-2：EX-CHECKPOINT-INCOMPATIBLE＋reasons（token 词表归
        // evidence——原样透传，本单元零复制零改写；"旧版本不迁移"即此
        // 拒绝路径的保守方向，升级后长任务须重跑）。
        result.outcome = CheckpointRestoreResult::Outcome::Incompatible;
        result.reasons = verdict.reasons;
        std::string joined;
        for (const std::string& r : verdict.reasons) {
            if (!joined.empty()) {
                joined += ",";
            }
            joined += r;
        }
        result.diagnostics.push_back(checkpointDiag(
            "EX-CHECKPOINT-INCOMPATIBLE",
            "检查点兼容判定拒绝 seq="
                + std::to_string(record.checkpointId.sequence) + " reasons=["
                + joined + "]",
            "旧版本检查点不迁移（宁重算不可错续）；全量重跑或使用新任务"));
        m_diagnostics.report(result.diagnostics.back());
        // 清空记录——拒绝结果不携带恢复规格（调用方不可误用部分规格）。
        return result;
    }

    // ---- 第四步：域可续声明核对（§8.1 resumable 行"false 的检查点仅供
    //      诊断"——EX-CKP-3 构造面：恢复失败原检查点完整保留；不删盘、
    //      不降级、可再试或全量重跑）。
    if (!record.resumable) {
        result.outcome = CheckpointRestoreResult::Outcome::NotResumable;
        result.diagnostics.push_back(checkpointDiag(
            "EX-CHECKPOINT-INCOMPATIBLE",
            "检查点被域申报为不可续（resumable=false，仅供诊断）seq="
                + std::to_string(record.checkpointId.sequence),
            "从更早检查点恢复或全量重跑；原检查点完整保留"));
        m_diagnostics.reportDev("EX-CHECKPOINT-INCOMPATIBLE",
                                "restore 拒绝：域申报不可续（记录保留，不删盘）");
        return result;
    }

    // ---- 通过：返回恢复规格（记录即统计基点——新 Attempt 派发的"续跑
    //      统计累计，不重复"输入；检查点可恢复≠任务已成功，§8.1——恢复
    //      产物仍走完整接纳，不在本面包装）。
    result.record = std::move(record);
    result.outcome = CheckpointRestoreResult::Outcome::Restored;
    return result;
}

// =====================================================================
// listCompatible / discard / 观测面
// =====================================================================

std::vector<CheckpointCandidate> CheckpointCoordinator::listCompatible(
    const ResumeQuery& query) const
{
    std::vector<CheckpointCandidate> out;
    std::lock_guard<std::mutex> lock(m_indexMutex);
    // std::map 迭代即主键字典序——确定性排列（同查询同清单，NFR-COR-02）。
    for (const auto& entry : m_index) {
        const CheckpointId& id = entry.first;
        const IndexedCheckpoint& indexed = entry.second;
        // 废弃项不出候选（显式废弃或损坏自动废弃——恢复编排不得再选）。
        if (indexed.discarded) {
            continue;
        }
        // 运行过滤（nullopt＝跨运行查询——任务链找最近的编排面）。
        if (query.run.has_value() && !(*query.run == id.run)) {
            continue;
        }
        // 判定摘要（五字段——evidence CheckpointSummary 单点复用，零第
        // 二套摘要结构；完整性标记反映会话索引当前校验状态：Corrupt 者
        // 已被标废弃，不进本循环）。
        evidence::CheckpointSummary summary;
        summary.evaluatorKey = indexed.record.evaluatorKey;
        summary.sliceId = indexed.record.sliceId;
        summary.evaluatorContractVersion = indexed.record.evaluatorContractVersion;
        summary.checkpointFormatVersion = indexed.record.checkpointFormatVersion;
        summary.integrityVerified = true;
        // 兼容判定调用（与 restore 同一 evidence 单点——清单面提前过滤，
        // restore 的磁盘级重校验仍是权威，两级不互替）。
        evidence::CacheHitQuery judgeRequest;
        judgeRequest.requestSliceId = query.requestSliceId;
        judgeRequest.requestContractVersion = query.requestContractVersion;
        if (evidence::judgeCheckpointCompatibility(judgeRequest, summary).verdict
            == evidence::CheckpointCompatibilityResult::Resumeable) {
            CheckpointCandidate candidate;
            candidate.id = id;
            candidate.summary = std::move(summary);
            out.push_back(std::move(candidate));
        }
    }
    return out;
}

void CheckpointCoordinator::discard(CheckpointId id)
{
    if (!id.isValid()) {
        throw ExecutionError(ExecutionErrorCode::InvalidState,
                             "execution/checkpoint: 废弃请求主键无效");
    }
    markDiscarded(id);
}

std::size_t CheckpointCoordinator::indexedCount() const
{
    std::lock_guard<std::mutex> lock(m_indexMutex);
    return m_index.size();
}

// =====================================================================
// 私有辅助
// =====================================================================

void CheckpointCoordinator::markDiscarded(const CheckpointId& id)
{
    // §8.1 损坏检查点处置与 §10.6 discard 同一落点：只改会话内存标记，
    // 零磁盘副作用（头文件磁盘职责声明）。
    std::lock_guard<std::mutex> lock(m_indexMutex);
    const auto it = m_index.find(id);
    if (it != m_index.end()) {
        it->second.discarded = true;
    }
}

}  // namespace sdurws::ird::execution
