/**
 * @file   TxEngine.cpp
 * @brief  事务引擎（TxEngine）实现——七步提交协议编排、.staging 暂存与
 *         恢复扫描。
 *
 * 设计依据：见 TxEngine.hpp 文件头（units/project.md §7 全节、§4.1/§4.5、
 * 需求 NFR-REL-01/PM-08、任务契约 PRJ-T07.json）。本实现文件只做一件事：
 * 把 §7.1 逐步表的每一步落到 win32 原语（AtomicFile/IFileOps——PRJ-T02
 * 产出）、对象库读校验（ObjectStore——PRJ-T05）与修订索引语义
 * （RevisionIndex——PRJ-T06）之上；任何一步失败不回滚已就位内容（§7.5
 * 崩溃一致性：失败残留处于闭包之外，可识别、对查询不可见、无害）。
 */

#include "TxEngine.hpp"

#include "Codec.hpp"
#include "win32/AtomicFile.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <map>
#include <random>
#include <set>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace fs = std::filesystem;

namespace sdurws::ird::project::tx {

namespace {

// ---------------------------------------------------------------------
// 错误映射与字节读写辅助
// ---------------------------------------------------------------------

/// 写路径 Win32 原生错误码 → 稳定码（与 ObjectStore PRJ-T05 登记口径①
/// 同表：磁盘满→DiskFull、拒绝访问→AccessDenied、其余→WriteRejected；
/// §7.1 第 1 步"write-rejected/disk-full"、第 2/4 步同口径。目录创建与
/// 删除的 error_code.value() 在 Windows（system 类别）同为 OS 原生码，
/// 共用本表——分类逻辑单点，不在目录/文件两处重复）。
StoreError osToStoreError(unsigned long osError, const std::string& what,
                          const std::string& path)
{
    constexpr unsigned long kDiskFull = 70;        // ERROR_DISK_FULL
    constexpr unsigned long kQuotaExceeded = 395;  // ERROR_DISK_QUOTA_EXCEEDED
    constexpr unsigned long kAccessDenied = 5;     // ERROR_ACCESS_DENIED
    const std::string detail = "project/tx-engine: " + what
        + " path=" + path + " osError=" + std::to_string(osError);
    if (osError == kDiskFull || osError == kQuotaExceeded) {
        return StoreError(StoreErrorCode::DiskFull, detail);
    }
    if (osError == kAccessDenied) {
        return StoreError(StoreErrorCode::AccessDenied, detail);
    }
    return StoreError(StoreErrorCode::WriteRejected, detail);
}

/// ContentVersion → 64 hex 文件名形态（剥离 "cv-" tag——与 ObjectStore
/// 同源同序，本地零第二格式化；失真属 core 契约破坏，logic_error fail-fast
/// 优先于产生错误编址）。
std::string cvHex(const core::ContentVersion& cv)
{
    const std::string canonical = cv.toCanonical();
    if (canonical.size() != 67 || canonical.compare(0, 3, "cv-") != 0) {
        throw std::logic_error("project/tx-engine: core ContentVersion "
                               "canonical contract violated");
    }
    return canonical.substr(3);
}

/// 二进制整读文件；文件不存在返回 nullopt（读失败的其他形态抛环境错误
/// ——读路径无法证明内容时不得伪装成"读到空"，与 ObjectStore 同纪律）。
std::optional<std::string> readAllBytes(const fs::path& file)
{
    std::error_code ec;
    if (!fs::exists(file, ec) || ec) {
        return std::nullopt;  // 不存在＝调用方语义分流（验证步按损坏处理）
    }
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        throw StoreError(StoreErrorCode::AccessDenied,
                         "project/tx-engine: open-failed path=" + file.string());
    }
    std::string bytes{std::istreambuf_iterator<char>(in),
                      std::istreambuf_iterator<char>()};
    return bytes;
}

/**
 * @brief 第 3 步单文件验证：重读＋size＋SHA-256 与期望值比对
 *        （§7.1 第 3 步"size＋SHA-256 全量校验"的逐文件体）。
 *
 * @param file      [in] 暂存文件全路径（诊断定位用）
 * @param expected  [in] 暂存时的原始字节（内存副本）
 * @param expectCv  [in] 期望内容版本（对象文件＝编址名一致性；清单/命令
 *                  ＝暂存时对内存字节实算的摘要——同一起源）
 * @throws StoreError StoreCorrupt（文件消失/size 不符/摘要不符——含路径
 *         定位；§7.1 第 3 步"校验不符→Failed(store-corrupt)"）
 */
void verifyStagedFile(const fs::path& file, const std::string& expected,
                      const core::ContentVersion& expectCv)
{
    // 读回（读不到＝暂存文件在闸门后消失——按损坏拒绝，不伪装成空）。
    const auto readBack = readAllBytes(file);
    if (!readBack) {
        throw StoreError(StoreErrorCode::StoreCorrupt,
                         "project/tx-engine: staged-file-missing path="
                             + file.string());
    }
    // size 半边：读得字节数必须与内存副本一致（截断/多余尾巴都在此暴露
    // ——部分写/介质异常的可观测面）。
    if (readBack->size() != expected.size()) {
        throw StoreError(StoreErrorCode::StoreCorrupt,
                         "project/tx-engine: staged-size-mismatch path="
                             + file.string() + " expected="
                             + std::to_string(expected.size()) + " got="
                             + std::to_string(readBack->size()));
    }
    // 摘要半边：只经 core ContentDigester（CR-02 唯一哈希入口）——对读回
    // 字节实算并与期望内容版本比对。"对象字节→ContentVersion 名一致性"
    // ＝对象文件的期望 cv 即其编址名；清单/命令的期望＝内存同源字节的
    // 摘要。写后篡改/位翻转在本步被拒（F3）。
    const core::ContentVersion actual = codec::contentVersionOf(*readBack);
    if (!(actual == expectCv)) {
        throw StoreError(StoreErrorCode::StoreCorrupt,
                         "project/tx-engine: staged-digest-mismatch path="
                             + file.string());
    }
}

/// 用户级诊断出口（sink 可空＝丢弃——§5.0 装配口径；StoreLock 先例）。
void emitReport(IDiagnosticsSink* sink, const core::DiagnosticRecord& rec)
{
    if (sink != nullptr) {
        sink->report(rec);
    }
}

/**
 * @brief 构造 PRJ-RECOVERY-IGNORED-UNCOMMITTED 记录（码值＝diagnostics.md
 *        §4.6 收编清单；项目级事件，subject=∅——PRJ-LOCK-HELD 同口径）。
 *        文案权威归 diagnostics/ui，此处文本是诊断载荷数据。
 */
core::DiagnosticRecord makeIgnoredUncommittedRecord(
    const std::vector<std::string>& txIds)
{
    std::string names;
    for (const std::string& id : txIds) {
        if (!names.empty()) {
            names += ",";
        }
        names += id;
    }
    return core::DiagnosticRecord::make(
        std::string{"PRJ-RECOVERY-IGNORED-UNCOMMITTED"},
        std::nullopt,  // subject：项目级事件（§8.2/§8.5 记法＝∅）
        std::nullopt, std::nullopt,
        "打开项目时发现 " + std::to_string(txIds.size())
            + " 个未提交事务的暂存残留（" + names + "），已按未提交内容"
            "忽略——不影响已提交历史",
        "上次会话的事务在提交点（HEAD 切换）之前中断（崩溃/失败），"
        ".staging 暂存目录残留（PM-08：启动扫描忽略＋恢复诊断）",
        "无需处理：残留内容不属于任何已提交修订；如需排查中断原因，"
        "可联系支持人员并保留 .staging 现场目录");
}

/**
 * @brief 构造 PRJ-STORE-CORRUPT 记录（码值＝diagnostics.md §4.6；定位
 *        细节写入 cause——§7.4④"失败＝store-corrupt（定位到文件）"）。
 */
core::DiagnosticRecord makeStoreCorruptRecord(const std::string& detail)
{
    return core::DiagnosticRecord::make(
        std::string{"PRJ-STORE-CORRUPT"},
        std::nullopt,  // subject：项目级事件（同上口径）
        std::nullopt, std::nullopt,
        "项目存储完整性校验失败：HEAD 引用的已提交内容存在缺失或损坏",
        detail,
        "从最近的有效备份恢复项目，或联系支持人员；不要手工修改 "
        "objects/revisions 目录（内容寻址校验会拒绝不一致的改动）");
}

/// 元数据引用键的去重比较子（ObjectRefPair 只契约 operator==——std::set
/// 需要严格弱序：按 oid/cv 的 core 规范文本字典序，仅用于扫描期去重，
/// 语义面不进持久化格式）。
struct MetadataRefLess
{
    bool operator()(const ObjectRefPair& a, const ObjectRefPair& b) const
    {
        if (!(a.objectId == b.objectId)) {
            return a.objectId.toCanonical() < b.objectId.toCanonical();
        }
        return a.contentVersion.toCanonical()
            < b.contentVersion.toCanonical();
    }
};

}  // namespace

// =====================================================================
// 构造与路径派生
// =====================================================================

TxEngine::TxEngine(win32::IFileOps* fileOps,
                   std::filesystem::path projectDir,
                   objstore::ObjectStore* objects,
                   revindex::RevisionIndex* index,
                   core::IDomainEventBus* eventBus,
                   IDiagnosticsSink* sink)
    : m_fileOps(fileOps),
      m_projectDir(std::move(projectDir)),
      m_objects(objects),
      m_index(index),
      m_eventBus(eventBus),
      m_sink(sink)
{
    // 装配契约 fail-fast（调用方错误）：三个必需部件任一为空＝装配违约，
    // 在构造期拒绝而不是在首个事务中途崩溃（fail-fast 原则——错误暴露
    // 点尽量靠近错误源）。eventBus/sink 可空（语义见头文件）。
    if (m_fileOps == nullptr || m_objects == nullptr || m_index == nullptr) {
        throw std::invalid_argument(
            "project/tx-engine: fileOps/objects/index 注入不得为空"
            "（装配契约——非 owning 指针，生存期由持有方保证）");
    }
}

std::filesystem::path TxEngine::stagingRoot() const
{
    return m_projectDir / ".staging";
}

std::filesystem::path TxEngine::objectsRoot() const
{
    return m_projectDir / "objects";
}

std::filesystem::path TxEngine::revisionsRoot() const
{
    return m_projectDir / "revisions";
}

std::filesystem::path TxEngine::revisionDir(const RevisionId& id) const
{
    // 目录名＝core 规范文本（"rev-<32hex>"，含 tag——§4.1 命名规则；
    // 与 core 格式化同源，不经本地第二套拼接，P-PR-1 消费基线）。
    return revisionsRoot() / id.toCanonical();
}

std::filesystem::path TxEngine::objectFile(const core::ObjectId& oid,
                                           const core::ContentVersion& cv) const
{
    // 与 ObjectStore::objectFile 同构：objects/<oid 规范文本>/<cv hex 64>
    // （§4.1 命名规则"无 tag"）。两处编址派生都锚定 core toCanonical，
    // 不存在漂移空间。
    return objectsRoot() / oid.toCanonical() / cvHex(cv);
}

std::string TxEngine::generateTxId()
{
    // §4.1 .staging 行："<tx-id>＝进程内单调＋随机后缀"。单调序号防同
    // 进程内重复；随机后缀防跨进程/跨会话残留目录撞名（残留会被恢复
    // 扫描忽略——撞名虽无害，但会让 ignoredStagingTxs 语义混淆）。随机
    // 源＝random_device 播种的 mt19937_64（线程局部——generateTxId 可
    // 并发进入而不共享 RNG 状态；引擎本身仍要求 commit 串行化）。
    const std::uint64_t seq =
        m_txSeq.fetch_add(1, std::memory_order_relaxed);
    thread_local std::mt19937_64 rng{std::random_device{}()};
    char hex[17];
    for (int i = 0; i < 2; ++i) {
        const std::uint64_t v = rng();
        static const char* kDigits = "0123456789abcdef";
        for (int j = 0; j < 8; ++j) {
            hex[i * 8 + j] = kDigits[(v >> (j * 4)) & 0xF];
        }
    }
    hex[16] = '\0';
    return "tx-" + std::to_string(seq) + "-" + std::string{hex, 16};
}

// =====================================================================
// 七步提交协议（commit 的线性编排；各步注释标注 §7.1 步号）
// =====================================================================

CommitResult TxEngine::commit(const CommitPlan& plan,
                              const HeadRecord& currentHead,
                              const ProjectMetadataRecord& authoritative,
                              const ObjectRefPair& authoritativeRef)
{
    // =================================================================
    // 内存段（零磁盘副作用）：计划校验 → 身份/序号分配 → D-5 派生 →
    // S6 注册＋发布边界校验 → 清单装配。全部可能失败的操作先于一切
    // 发布动作（"顺序即原子性"的 D-09 前置精神：任何 here-doc 失败
    // 都不留下磁盘痕迹）。
    // =================================================================

    // ---- 调用方契约校验（fail-fast）：字段级违约在装配点暴露 ----
    if (!plan.revisionId.isValid()) {
        throw std::invalid_argument(
            "project/tx-engine: commit requires valid plan.revisionId"
            "（修订身份由调用方生成——单一分配点口径）");
    }
    if (!plan.branchId.isValid()) {
        throw std::invalid_argument(
            "project/tx-engine: commit requires valid branchId");
    }
    // tipUpdate 一致性提前校验：更新的 tip 必须恰为本计划的新修订
    // （validateMetadataPublish 亦有同判据——分支_metadata-regression；
    // 此处以 invalid_argument 前置给出更精确的调用方错误归类，二道
    // 防线保留防绕过）。
    if (plan.metadataDelta.tipUpdate.has_value()
        && !(plan.metadataDelta.tipUpdate->newTip == plan.revisionId)) {
        throw std::invalid_argument(
            "project/tx-engine: tipUpdate.newTip 必须等于 plan.revisionId"
            "（每次提交的活动分支 tip 恰推进到本次新修订——§4.5）");
    }
    if (plan.committedAtUtc.empty()) {
        throw std::invalid_argument(
            "project/tx-engine: commit requires committedAtUtc（ISO-8601 "
            "UTC——时钟供给归调用方）");
    }
    if (plan.command.commandType.empty()) {
        throw std::invalid_argument(
            "project/tx-engine: commit requires commandType（命令留痕无"
            "token＝历史浏览无法呈现——§4.4.4）");
    }
    if (!currentHead.projectId.isValid()) {
        throw std::invalid_argument(
            "project/tx-engine: currentHead.projectId 无效（须为磁盘 HEAD "
            "的真实内容——readHead() 产出）");
    }
    if (plan.parentRevisionId.has_value()
        && !plan.parentRevisionId->isValid()) {
        throw std::invalid_argument(
            "project/tx-engine: parentRevisionId 携带全零保留值（无父修订"
            "应传 nullopt 而非空身份——§4.4.2）");
    }
    for (const PlannedObject& obj : plan.newObjects) {
        // 域对象身份与类型 token 的合法性：全零 oid＝保留值不是身份；
        // 空 token 会让修订清单的 objectRefs 违反 §4.4.2 非空约束。
        if (!obj.oid.isValid() || obj.objectTypeToken.empty()) {
            throw std::invalid_argument(
                "project/tx-engine: newObjects 含无效 oid 或空 "
                "objectTypeToken（oid 分配归 HandlerContext——§6.5）");
        }
    }

    // ---- 第 1 步（内存半边）：tx-id 与序号分配（§7.1 第 1 步"生成 tx-id；
    //      ……；分配 RevisionId/seq（权威 HEAD seq+1）"）。修订身份由计划
    //      带入（单一分配点＝命令服务 S6 计划装配，见 CommitPlan 登记），
    //      本步骤消费该身份并分配序号。O-15 口径：序号在 S6（本事务）内
    //      分配、失败不回收（进程内水位只进不退）；"S6 决策点"＝进入本
    //      事务（S1～S5 命令阶段全部通过后），因此此处分配符合"决策点之后
    //      调用"的时序契约。事务中途失败会留下已消费的水位——跨会话由
    //      adoptHeadSeq 断点恢复，与崩溃残留序号同语义（§4.5.1 结论④：
    //      不同在闭包即无歧义）。
    const std::string txId = generateTxId();
    const RevisionId& newRevId = plan.revisionId;
    const std::uint64_t revSeq = m_index->allocateRevisionSeq();

    // ---- D-5 派生：以权威元数据为底本拷贝出候选（deriveNextMetadata 是
    //      INV-M3 的机制化唯一入口——增量引用未知分支等在此 invalid_argument，
    //      零磁盘副作用）。candidate 与计划分离＝PRJ-T06 的 S6 时序契约
    //      （"提交时由事务引擎 derive＋validate"）。
    const ProjectMetadataRecord candidate = revindex::RevisionIndex::
        deriveNextMetadata(authoritative, authoritativeRef,
                           plan.metadataDelta, newRevId);

    // ---- 元数据对象身份与编址：oid 由引擎生成（§13.2：对象分配落地点
    //      归 project——元数据对象是 project 自有对象，不经 HandlerContext）；
    //      负载＝候选元数据的 canonical 字节；cv＝CR-02 唯一哈希入口。
    const core::ObjectId metadataOid = core::ObjectId::generate();
    const std::string metadataPayload = codec::dump(candidate);
    const core::ContentVersion metadataCv =
        codec::contentVersionOf(metadataPayload);
    const std::string metadataDigestHex = cvHex(metadataCv);

    // ---- 域对象编址预计算（内容寻址：文件名即摘要，写入前必须已知目标
    //      ——与 ObjectStore::publishObject 第 1 步同纪律）。
    struct StagedObject
    {
        core::ObjectId oid;
        core::ContentVersion cv;
        std::string payload;  ///< canonical 字节的稳定副本（验证步比对基准）
    };
    std::vector<StagedObject> staged;
    staged.reserve(plan.newObjects.size() + 1);
    for (const PlannedObject& obj : plan.newObjects) {
        StagedObject s;
        s.oid = obj.oid;
        s.payload.assign(obj.payload.begin(), obj.payload.end());
        s.cv = codec::contentVersionOf(
            std::string_view{reinterpret_cast<const char*>(s.payload.data()),
                             s.payload.size()});
        staged.push_back(std::move(s));
    }

    // ---- 修订清单装配（§4.4.2）：objectRefs＝完整引用集（≥1，含元数据
    //      对象——元数据在前、域对象按计划序）；introducedObjects＝本次新
    //      就位对象（含元数据——残留识别/浏览辅助，§4.4.2 表）。
    RevisionManifest manifest;
    manifest.revisionId = newRevId;
    manifest.revisionSeq = revSeq;
    manifest.parentRevisionId = plan.parentRevisionId;
    manifest.branchId = plan.branchId;
    manifest.committedAtUtc = plan.committedAtUtc;
    manifest.metadataRef = ObjectRefPair{metadataOid, metadataCv};
    {
        ObjectRef metaRef;
        metaRef.objectId = metadataOid;
        metaRef.contentVersion = metadataCv;
        metaRef.objectTypeToken = kMetadataTypeToken;
        metaRef.digest256 = metadataDigestHex;
        manifest.objectRefs.push_back(metaRef);
        manifest.introducedObjects.push_back(metadataCv);
    }
    for (std::size_t i = 0; i < staged.size(); ++i) {
        const StagedObject& s = staged[i];
        ObjectRef ref;
        ref.objectId = s.oid;
        ref.contentVersion = s.cv;
        // 按索引对齐取计划里的类型 token（staged 与 plan.newObjects 同序
        // 同长——上方装配循环保序）。
        ref.objectTypeToken = plan.newObjects[i].objectTypeToken;
        ref.digest256 = cvHex(s.cv);
        manifest.objectRefs.push_back(ref);
        manifest.introducedObjects.push_back(s.cv);
    }
    const std::string manifestBytes = codec::dump(manifest);
    // HEAD.manifestDigest 的取值（§4.4.1"指向修订 manifest 的字节摘要"）：
    // cv 的 hex 形态（SHA-256 hex 无 tag）——同一字节序列后续在第 3/5 步
    // 重算比对，切换后自校验因此可复现（CR-02：摘要只经 core ContentDigester）。
    const core::ContentVersion manifestCv =
        codec::contentVersionOf(manifestBytes);
    const std::string manifestDigestHex = cvHex(manifestCv);

    // ---- 命令留痕字节（§4.4.4；project 原样持久化——D-10）。
    const std::string commandBytes = codec::dump(plan.command);
    const core::ContentVersion commandCv =
        codec::contentVersionOf(commandBytes);

    // ---- S6 注册＋发布边界校验（PRJ-T06 时序契约：先注册新修订后校验
    //      ——祖先链/谱系单调判定以新修订已注册为前提）。二者任一失败
    //      （BranchMetadataRegression/喂入失真的 invalid_argument）都发生在
    //      暂存目录创建之前——零磁盘副作用。注意：校验失败时新修订已注册
    //      进会话索引且无撤销入口——该修订从任何 HEAD 闭包都不可达（无
    //      parent 指向它），对查询不可见，属无害的内存残迹；此时调用方
    //      已处于不变量违约的编程错误态（fail-fast 语义优先）。
    m_index->registerRevision(manifest);
    const ObjectRefPair candidateRef{metadataOid, metadataCv};
    m_index->validateMetadataPublish(authoritative, authoritativeRef,
                                     candidate, candidateRef, newRevId);
    // ---- S6 时序第三步"切换权威"：候选通过发布边界校验后注册为新权威
    // 元数据版本——后续闭包行走（含本次新修订的 metadataRef）因此可达
    // （validate 契约要求 candidateRef 此前未注册，故注册必须在校验之后）。
    m_index->registerMetadata(candidateRef, candidate);

    // =================================================================
    // 第 1 步（磁盘半边）：建 .staging/<tx-id>/（§7.1 第 1 步"建 .staging/
    // <tx-id>/"）。失败→环境码；残留形态＝无或仅空目录（启动清理兜底，
    // 这里尽力移除空壳——失败忽略，恢复扫描可识别空目录）。
    // =================================================================
    const fs::path txDir = stagingRoot() / txId;
    {
        const win32::FileResult cr = m_fileOps->createDirectories(txDir.wstring());
        if (!cr.ok) {
            // 尽力清理空壳目录（创建失败的目录通常不存在；存在则是半建
            // 状态——removeTree 幂等，错误码吸收不做二次报错：本失败的
            // 主因错误才是稳定诊断载体）。
            std::error_code rmEc;
            fs::remove_all(txDir, rmEc);
            throw osToStoreError(cr.osError, "create-staging-dir",
                                 txDir.string());
        }
    }
    // tmp 子目录随事务建立（D-09：编译/非事务临时产物限 .staging/tmp——
    // 事务期的临时落点即本目录；§7.1 第 7 步"删除 .staging/<tx-id>/（含
    // tmp）"的清理对象在准备期即存在）。
    {
        const win32::FileResult cr =
            m_fileOps->createDirectories((txDir / "tmp").wstring());
        if (!cr.ok) {
            std::error_code rmEc;
            fs::remove_all(txDir, rmEc);
            throw osToStoreError(cr.osError, "create-staging-tmp",
                                 (txDir / "tmp").string());
        }
    }

    // =================================================================
    // 第 2 步：暂存（§7.1"对象字节、manifest.json、command.json、新
    // ProjectMetadata 全部写入 .staging/<tx-id>/"）。每文件走 writeThrough
    // ＝FILE_FLAG_WRITE_THROUGH＋FlushFileBuffers 闸门（§7.2 行 1——写入
    // 持久性保证）。暂存布局镜像目标树（objects/<oid>/<cv>、revision/
    // manifest.json、revision/command.json），发布步 rename 时路径语义
    // 一目了然。任何写失败：已写部分**有意残留**（§7.1 第 2 步"残留部分
    // 文件于 .staging"——恢复扫描忽略＋诊断，PM-08），直接上抛不清理。
    // =================================================================
    win32::AtomicFile file{m_fileOps};  // 注入接缝（F2 注入边界在此链路）
    {
        // 元数据对象先行暂存（发布序＝对象先行；元数据对象是清单的首个
        // 引用者）。
        const fs::path stagedMeta =
            txDir / "objects" / metadataOid.toCanonical() / cvHex(metadataCv);
        const win32::FileResult dirCr = m_fileOps->createDirectories(
            (txDir / "objects" / metadataOid.toCanonical()).wstring());
        if (!dirCr.ok) {
            throw osToStoreError(dirCr.osError, "stage-mkdir",
                                 stagedMeta.string());
        }
        const win32::FileResult wr = file.writeThrough(
            stagedMeta.wstring(), metadataPayload.data(),
            metadataPayload.size());
        if (!wr.ok) {
            throw osToStoreError(wr.osError, "stage-write",
                                 stagedMeta.string());
        }
    }
    for (const StagedObject& s : staged) {
        const fs::path stagedPath =
            txDir / "objects" / s.oid.toCanonical() / cvHex(s.cv);
        const win32::FileResult dirCr = m_fileOps->createDirectories(
            (txDir / "objects" / s.oid.toCanonical()).wstring());
        if (!dirCr.ok) {
            throw osToStoreError(dirCr.osError, "stage-mkdir",
                                 stagedPath.string());
        }
        const char* data =
            s.payload.empty() ? nullptr : s.payload.data();
        const win32::FileResult wr = file.writeThrough(
            stagedPath.wstring(), data, s.payload.size());
        if (!wr.ok) {
            throw osToStoreError(wr.osError, "stage-write",
                                 stagedPath.string());
        }
    }
    // 修订目录两文件（command 先于 manifest 暂存只是书写序；发布步保证
    // manifest 最后就位）。
    const fs::path stagedRevisionDir = txDir / "revision";
    {
        const win32::FileResult dirCr =
            m_fileOps->createDirectories(stagedRevisionDir.wstring());
        if (!dirCr.ok) {
            throw osToStoreError(dirCr.osError, "stage-mkdir",
                                 stagedRevisionDir.string());
        }
    }
    const fs::path stagedManifest = stagedRevisionDir / "manifest.json";
    const fs::path stagedCommand = stagedRevisionDir / "command.json";
    {
        const win32::FileResult wr =
            file.writeThrough(stagedCommand.wstring(), commandBytes.data(),
                              commandBytes.size());
        if (!wr.ok) {
            throw osToStoreError(wr.osError, "stage-write",
                                 stagedCommand.string());
        }
    }
    {
        const win32::FileResult wr =
            file.writeThrough(stagedManifest.wstring(), manifestBytes.data(),
                              manifestBytes.size());
        if (!wr.ok) {
            throw osToStoreError(wr.osError, "stage-write",
                                 stagedManifest.string());
        }
    }

    // =================================================================
    // 第 3 步：验证（§7.1"重读暂存文件：size＋SHA-256 全量校验（含'对象
    // 字节→ContentVersion 名一致性'）"）。失败→StoreCorrupt＋开发诊断
    // （磁盘/内存错误怀疑——§7.1 第 3 步失败列）；残留同第 2 步。
    // =================================================================
    {
        const fs::path stagedMetaPath = txDir / "objects"
            / metadataOid.toCanonical() / cvHex(metadataCv);
        try {
            verifyStagedFile(stagedMetaPath, metadataPayload, metadataCv);
            for (const StagedObject& s : staged) {
                verifyStagedFile(
                    txDir / "objects" / s.oid.toCanonical() / cvHex(s.cv),
                    s.payload, s.cv);
            }
            verifyStagedFile(stagedCommand, commandBytes, commandCv);
            verifyStagedFile(stagedManifest, manifestBytes, manifestCv);
        } catch (const StoreError& e) {
            reportDev(std::string{"verify-failed（暂存校验不符——磁盘/内存"
                                  "错误怀疑，F3 边界） detail="}
                      + e.what());
            throw;  // StoreCorrupt（含路径定位）原样上抛
        }
    }

    // =================================================================
    // 第 4 步：内容发布（§7.1"`.staging` 内容就位正式路径……只增不改，
    // 部分就位不破坏旧版本（HEAD 仍指旧内容）"）。顺序＝对象先行、修订
    // 清单最后（"清单是内容的引用者，反向会出现'清单在而对象缺'的中间
    // 可见态"；command.json 属修订目录内容，先于 manifest 就位）。任一
    // rename 失败→环境码；已就位项保留（悬挂，不回滚——闭包外，§7.3）。
    // =================================================================
    {
        // 对象品类：元数据对象→域对象（计划序）。共享判定（目标已存在）
        // 复用 ObjectStore 读回校验通道——摘要不符抛 StoreCorrupt，与
        // publishObject 的共享语义同源同码（实现口径登记见头文件）。
        const fs::path stagedMetaPath =
            txDir / "objects" / metadataOid.toCanonical() / cvHex(metadataCv);
        publishOneObject(stagedMetaPath, metadataOid, metadataCv);
        for (const StagedObject& s : staged) {
            publishOneObject(
                txDir / "objects" / s.oid.toCanonical() / cvHex(s.cv),
                s.oid, s.cv);
        }
        // 修订目录：先建目录，command.json 次之，manifest.json 最后
        // （§7.1"对象先行、修订清单最后"）。
        const fs::path targetRevDir = revisionDir(newRevId);
        const win32::FileResult dirCr =
            m_fileOps->createDirectories(targetRevDir.wstring());
        if (!dirCr.ok) {
            throw osToStoreError(dirCr.osError, "publish-mkdir",
                                 targetRevDir.string());
        }
        const win32::FileResult cmdPub = m_fileOps->publishNew(
            stagedCommand.wstring(),
            (targetRevDir / "command.json").wstring());
        if (!cmdPub.ok) {
            throw osToStoreError(cmdPub.osError, "publish-command",
                                 (targetRevDir / "command.json").string());
        }
        const win32::FileResult manPub = m_fileOps->publishNew(
            stagedManifest.wstring(),
            (targetRevDir / "manifest.json").wstring());
        if (!manPub.ok) {
            throw osToStoreError(manPub.osError, "publish-manifest",
                                 (targetRevDir / "manifest.json").string());
        }
    }

    // =================================================================
    // 第 5 步：提交指针切换（§7.1"写 HEAD.new（完整 HeadRecord）→ flush →
    // MoveFileExW(HEAD.new→HEAD, REPLACE_EXISTING|WRITE_THROUGH)——唯一
    // 提交点；切换后重读 HEAD 自校验 manifestDigest"）。
    // =================================================================
    HeadRecord newHead;
    newHead.schemaVersion = currentHead.schemaVersion;
    newHead.projectId = currentHead.projectId;
    newHead.revisionId = newRevId;
    newHead.revisionSeq = revSeq;
    newHead.branchId = plan.branchId;
    newHead.manifestDigest = manifestDigestHex;
    const fs::path stagedHead = txDir / "head.new";
    {
        const std::string headBytes = codec::dump(newHead);
        const win32::FileResult wr =
            file.writeThrough(stagedHead.wstring(), headBytes.data(),
                              headBytes.size());
        if (!wr.ok) {
            throw osToStoreError(wr.osError, "stage-head",
                                 stagedHead.string());
        }
    }
    {
        const fs::path headPath = m_projectDir / "HEAD";
        // replaceExisting＝MoveFileExW(REPLACE_EXISTING|WRITE_THROUGH)（§7.2
        // 行 3）。本调用成功即"唯一提交点"成立——此后本次修订视为已提交，
        // 后续任何失败（第 6/7 步）不回滚（D-18）。失败→环境码；内容已
        // 发布但 HEAD 未变＝第 4 步残留语义（闭包外可识别）。
        const win32::FileResult rep =
            m_fileOps->replaceExisting(stagedHead.wstring(),
                                       headPath.wstring());
        if (!rep.ok) {
            throw osToStoreError(rep.osError, "head-switch", headPath.string());
        }
        // 自校验（§7.1 第 5 步"切换后重读 HEAD 自校验 manifestDigest"）：
        // 重读+重解与内存事实逐字段比对。失败＝提交点内容异常的极端态
        // （读失败/内容不符）——D-18 不回滚：抛 StoreCorrupt 硬错误，恢复
        // 扫描④以磁盘 HEAD 实际内容为准兜底（§7.1"崩溃于切换前后以 HEAD
        // 实际内容为准（字节级判定）"）。
        const auto headBytes = readAllBytes(headPath);
        bool selfCheckOk = headBytes.has_value();
        if (selfCheckOk) {
            try {
                selfCheckOk = (codec::parseHeadRecord(*headBytes) == newHead);
            } catch (const StoreError&) {
                selfCheckOk = false;  // 重读内容解析失败＝自校验不过
            }
        }
        if (!selfCheckOk) {
            reportDev("head-selfcheck-failed（HEAD 切换后重读不符——提交点"
                      "异常，恢复扫描④兜底） rev=" + newRevId.toCanonical());
            throw StoreError(StoreErrorCode::StoreCorrupt,
                             "project/tx-engine: head-selfcheck-failed path="
                                 + (m_projectDir / "HEAD").string());
        }
    }

    // =================================================================
    // 第 6 步：事件发布（§7.1"bus.publish(RevisionCommitted→
    // DependencyInvalidated)（FIFO；失败重试一次＋开发诊断，不回滚——D-18）"）。
    // 总线可空＝跳过（§5.1 装配口径）。失败不影响提交事实。
    // =================================================================
    if (m_eventBus != nullptr) {
        core::RevisionCommittedPayload committed;
        committed.project = currentHead.projectId;
        committed.branch = plan.branchId;
        committed.revision = newRevId;
        committed.parent = plan.parentRevisionId;
        publishEventWithRetry(core::DomainEvent::make(committed),
                              "revision-committed");
        // FIFO：失效通知严格在提交事件之后发布（订阅方先刷新视图再失效
        // 依赖——§7.1 第 6 步"RevisionCommitted→DependencyInvalidated"序）。
        core::DependencyInvalidatedPayload invalidated;
        invalidated.project = currentHead.projectId;
        invalidated.branch = plan.branchId;
        invalidated.revision = newRevId;
        publishEventWithRetry(core::DomainEvent::make(invalidated),
                              "dependency-invalidated");
    }

    // =================================================================
    // 第 7 步：清理（§7.1"删除 .staging/<tx-id>/（含 tmp）；清理失败仅
    // 开发诊断"——已提交状态不受影响，残留不参与任何读取路径）。
    // =================================================================
    {
        const win32::FileResult rm = m_fileOps->removeTree(txDir.wstring());
        if (!rm.ok) {
            reportDev("cleanup-failed（.staging 清理失败——已提交状态不受"
                      "影响，残留由下次启动扫描处置，F7 边界） dir="
                          + txDir.string() + " osError="
                          + std::to_string(rm.osError));
        }
    }

    // ---- 提交结果（供命令服务组装返回值/事件消费，PRJ-T10） ----
    CommitResult result;
    result.revisionId = newRevId;
    result.revisionSeq = revSeq;
    result.metadataRef = candidateRef;
    result.manifestDigest = manifestDigestHex;
    result.newHead = newHead;
    return result;
}

void TxEngine::publishOneObject(const fs::path& stagedPath,
                                const core::ObjectId& oid,
                                const core::ContentVersion& cv)
{
    const fs::path target = objectFile(oid, cv);
    const std::string targetName = target.string();

    // 确保对象目录存在（objects/<oid>/；§7.1 第 4 步发布在项目根内——
    // 对象区目录已在 §4.1 白名单，创建失败按环境码上抛）。
    const win32::FileResult dirCr =
        m_fileOps->createDirectories((objectsRoot() / oid.toCanonical()).wstring());
    if (!dirCr.ok) {
        throw osToStoreError(dirCr.osError, "publish-mkdir", targetName);
    }

    // 只增发布：publishNew 目标不存在→rename；有界重试只覆盖"EXISTS→
    // 消失"竞态（与 ObjectStore::publishObject 同口径——其余失败一律按
    // 原语义传播）。
    for (int attempt = 0; attempt < 2; ++attempt) {
        std::error_code existsEc;
        if (!fs::exists(target, existsEc) || existsEc) {
            // 目标不存在：publishNew 就位（F4 注入点——kth rename 失败在
            // 此暴露；已就位的前序对象保留＝悬挂语义，不回滚）。
            const win32::FileResult pub =
                m_fileOps->publishNew(stagedPath.wstring(), target.wstring());
            if (pub.ok) {
                return;  // 就位成功
            }
            if (pub.osError == ERROR_ALREADY_EXISTS && attempt == 0) {
                continue;  // 竞态：目标刚出现——下一轮走共享判定
            }
            throw osToStoreError(pub.osError, "publish-rename", targetName);
        }
        // 目标已存在（同内容已发布＝共享去重）：读回既有对象校验——
        // ObjectStore::object 对"摘要与编址不符"抛 StoreCorrupt（数据侧，
        // §4.6 口径），一致则返回字节＝共享成立（磁盘零写入）。暂存件
        // 不在此删除——第 7 步整目录清理统一处置，少一个失败面。
        m_objects->object(oid, cv);
        return;
    }
    // 不可达防御：2 次尝试均落入竞态窗口——按写失败传播（协议有界性）。
    throw StoreError(StoreErrorCode::WriteRejected,
                     "project/tx-engine: publish-retry-exhausted path="
                         + targetName);
}

void TxEngine::publishEventWithRetry(const core::DomainEvent& event,
                                     const char* what)
{
    // 失败重试一次（§7.1 第 6 步"失败重试一次＋开发诊断"）：开发诊断
    // 伴随每一次失败观测发出（首败即诊断——运维在重试成功时也能看到
    // 抖动痕迹；重试再失败的诊断注明 D-18 不回滚）。两次都失败则放弃
    // ——绝不抛出：事件非数据源（core D-09），订阅方可经②端口重读自愈，
    // 提交事实不受影响（D-18）。
    for (int attempt = 0; attempt < 2; ++attempt) {
        try {
            m_eventBus->publish(event);
            return;
        } catch (const std::exception& e) {
            reportDev(std::string{"event-publish-failed（"}
                      + (attempt == 0
                             ? "首败——按协议重试一次"
                             : "重试后仍失败——不回滚已提交修订，D-18")
                      + "） event=" + what + " error=" + e.what());
        }
    }
}

// =====================================================================
// 恢复扫描（§7.3/§7.4①④⑤）
// =====================================================================

TxRecoveryScan TxEngine::scanForRecovery()
{
    TxRecoveryScan scan;

    // ---- ① `.staging` 残留扫描（§7.4①"忽略未提交内容＋恢复诊断"）。
    // tx 目录＝未提交事务：记入清单＋用户级诊断，**忽略不删**（现场保留
    // ——§4.1 .staging 行"启动扫描忽略"口径）；`.staging/tmp`＝启动清理
    // （整树删除——§4.1 tmp 行"启动清理"，D-09 临时物不逸出该目录）。
    std::error_code ec;
    const fs::path stRoot = stagingRoot();
    if (fs::exists(stRoot, ec) && !ec) {
        std::vector<std::string> ignored;
        for (fs::directory_iterator it(stRoot, ec), end; it != end && !ec;
             it.increment(ec)) {
            if (ec) {
                break;  // 迭代中途中错：停止扫描（已收集结果仍有效）
            }
            const std::string name = it->path().filename().string();
            std::error_code typeEc;
            if (!it->is_directory(typeEc) || typeEc) {
                continue;  // 非目录条目不属于事务残留（开发观测面之外——
                           // 暂存区只应有目录条目，异常文件留给人工排查）
            }
            if (name == "tmp") {
                // tmp 清理（§4.1"启动清理"）：删除失败仅开发诊断——
                // 临时物残留无害（不参与任何读取路径）。
                const win32::FileResult rm =
                    m_fileOps->removeTree(it->path().wstring());
                if (!rm.ok) {
                    reportDev("tmp-cleanup-failed（.staging/tmp 启动清理"
                              "失败——残留无害） osError="
                                  + std::to_string(rm.osError));
                }
                continue;
            }
            ignored.push_back(name);
        }
        // 确定性排序（NFR-COR-02：directory_iterator 顺序未指定——报告
        // 必须可复现，验收可逐元素比对）。
        std::sort(ignored.begin(), ignored.end());
        scan.ignoredStagingTxs = std::move(ignored);
        if (!scan.ignoredStagingTxs.empty()) {
            // 用户级恢复诊断（PRJ-RECOVERY-IGNORED-UNCOMMITTED——码值
            // ＝diagnostics.md §4.6 收编清单，project 不私造码）。
            emitReport(m_sink, makeIgnoredUncommittedRecord(
                                   scan.ignoredStagingTxs));
        }
    }

    // ---- ④ HEAD 读取：缺失/损坏＝CorruptStoreDetected（§4.1 HEAD 行）
    // ——报告位 false＋PRJ-STORE-CORRUPT（不抛：一次扫描产出完整清单）。
    const fs::path headPath = m_projectDir / "HEAD";
    const auto headBytes = readAllBytes(headPath);
    if (!headBytes) {
        reportDev("recovery-head-missing（HEAD 缺失——无法校验闭包）");
        emitReport(m_sink, makeStoreCorruptRecord(
                               "project/tx-engine: HEAD 文件缺失 path="
                                   + headPath.string()));
        return scan;  // headIntegrityVerified=false；①的发现已随行带回
    }
    HeadRecord head;
    try {
        head = codec::parseHeadRecord(*headBytes);
    } catch (const StoreError& e) {
        reportDev(std::string{"recovery-head-unreadable（HEAD 解析拒绝） "}
                  + e.what());
        emitReport(m_sink, makeStoreCorruptRecord(
                               "project/tx-engine: HEAD 解析失败 detail="
                                   + std::string(e.what())));
        return scan;
    }

    // ---- 装载磁盘修订清单入**临时索引**（不污染会话索引——§7.3：闭包
    // 外修订对查询不可见的机制＝装载纪律只认闭包成员；扫描自持局部视图）。
    // 损坏分流纪律（§7.4④的边界）：清单/元数据缺失或损坏只对**闭包成员**
    // 构成 store-corrupt；闭包外目录的残缺＝未提交残留（§7.3"闭包外修订
    // 目录与对象计入残留清单"——发布中途失败的修订目录可能无清单，F4
    // 边界）。装载期失败先挂账，待闭包计算成功后统一归类。
    revindex::RevisionIndex loader;
    // 磁盘侧修订目录集（dirName→已注册的修订 id；清单完好者才入集）。
    std::map<std::string, RevisionId> onDisk;
    // 挂账的残缺目录（{目录名解析出的身份, 定位细节}）——闭包行走成功
    // 即证明闭包成员清单全部完好，挂账目录必在闭包外＝残留。
    std::vector<std::pair<RevisionId, std::string>> brokenDirs;
    std::set<ObjectRefPair, MetadataRefLess> metadataRefs;  // 去重待装载集
    // 元数据装载失败挂账（ref→细节）——仅闭包成员引用的失败置损坏，
    // 残留清单引用的失败仅开发诊断。
    std::map<ObjectRefPair, std::string, MetadataRefLess> failedMetaRefs;
    bool broken = false;  // ④完整性结论（仅闭包相关失败置位）
    std::string brokenDetail;

    {
        std::error_code dirEc;
        const fs::path revRoot = revisionsRoot();
        const bool revRootOk = fs::exists(revRoot, dirEc) && !dirEc;
        if (revRootOk) {
            for (fs::directory_iterator it(revRoot, dirEc), end;
                 it != end && !dirEc; it.increment(dirEc)) {
                if (dirEc) {
                    break;
                }
                std::error_code isDirEc;
                if (!it->is_directory(isDirEc) || isDirEc) {
                    continue;  // 非目录条目：不在修订命名规则内，跳过
                }
                const std::string dirName = it->path().filename().string();
                const auto rid = RevisionId::tryFromCanonical(dirName);
                if (!rid) {
                    // 目录名非 rev- 规范文本：不合规条目（开发观测）——
                    // 不计入磁盘集也不计入闭包（残缺命名不可能被 HEAD
                    // 引用；§4.5 闭包规则只认身份可达性）。
                    reportDev("recovery-malformed-revision-dir（目录名非"
                              "规范文本，跳过） dir=" + dirName);
                    continue;
                }
                const auto bytes =
                    readAllBytes(it->path() / "manifest.json");
                if (!bytes) {
                    // 清单缺失：挂账待闭包归类（残留或损坏）。
                    brokenDirs.emplace_back(
                        *rid, "project/tx-engine: manifest 缺失 path="
                                  + (it->path() / "manifest.json").string());
                    reportDev("recovery-manifest-missing path="
                              + (it->path() / "manifest.json").string());
                    continue;
                }
                try {
                    const RevisionManifest manifest =
                        codec::parseRevisionManifest(*bytes);
                    loader.registerRevision(manifest);
                    onDisk[dirName] = manifest.revisionId;
                    metadataRefs.insert(manifest.metadataRef);
                } catch (const StoreError& e) {
                    // 清单字节非法（截断/字段违约/版本拒绝）：同缺失挂账。
                    brokenDirs.emplace_back(
                        *rid, "project/tx-engine: manifest 解析失败 path="
                            + (it->path() / "manifest.json").string()
                            + " detail=" + std::string(e.what()));
                    reportDev("recovery-manifest-corrupt path="
                              + (it->path() / "manifest.json").string()
                              + " detail=" + std::string(e.what()));
                }
            }
        }
    }

    // ---- 元数据记录装载（闭包通道二需要 supersedes/committedBy 链）。
    for (const ObjectRefPair& ref : metadataRefs) {
        try {
            // 经 ObjectStore 读回校验通道：缺失/摘要不符＝StoreCorrupt
            //（§4.6 读校验语义；闭包内引用缺失按数据侧拒绝——§5.2 表）。
            const auto payloadBytes = m_objects->object(ref.objectId,
                                                        ref.contentVersion);
            const std::string payload{
                reinterpret_cast<const char*>(payloadBytes.data()),
                payloadBytes.size()};
            const ProjectMetadataRecord record =
                codec::parseMetadataRecord(payload);
            loader.registerMetadata(ref, record);
        } catch (const StoreError& e) {
            // 挂账不置损：是否构成闭包损坏待闭包归类（仅闭包成员引用的
            // 失败置位——残留清单引用悬空只做开发诊断）。
            failedMetaRefs.emplace(
                ref, "project/tx-engine: metadata 装载失败 oid="
                    + ref.objectId.toCanonical() + " cv="
                    + ref.contentVersion.toCanonical() + " detail="
                    + std::string(e.what()));
        }
    }

    // ---- 闭包计算（§7.4④"校验 HEAD 引用闭包完整性"）：起点缺失/悬挂
    // 引用＝损坏（RevisionIndex 契约）——捕获置位，不中断扫描。
    bool closureOk = false;
    revindex::CommittedClosure closure;
    try {
        closure = loader.committedClosure(head.revisionId);
        closureOk = true;
    } catch (const StoreError& e) {
        broken = true;
        brokenDetail = "project/tx-engine: 闭包行走失败 detail="
            + std::string(e.what());
        reportDev(std::string{"recovery-closure-failed "} + e.what());
    }

    if (closureOk) {
        // ---- 闭包行走成功 ⇒ 闭包成员的清单全部完好；挂账残缺目录必在
        // 闭包外＝未提交残留（§7.3；F4 边界的"有目录无清单"形态）。
        for (const auto& [rid, detail] : brokenDirs) {
            scan.uncommittedRevisions.push_back(rid);
            reportDev("recovery-uncommitted-broken-dir（闭包外目录清单残缺"
                      "——按残留归类） detail=" + detail);
        }

        // ---- §7.3 识别：磁盘修订目录集 ＼ 闭包集 ＝ 已发布但未提交
        //（对查询/历史浏览不可见——装载纪律只认闭包成员；这里产出其
        // 清单供恢复诊断与 PRJ-TX-9 断言）。
        std::set<std::string> inClosure;
        for (const RevisionId& id : closure.revisions) {
            inClosure.insert(id.toCanonical());
        }
        for (const auto& [dirName, rid] : onDisk) {
            if (inClosure.count(dirName) == 0) {
                scan.uncommittedRevisions.push_back(rid);
            }
        }
        // 残留清单统一排序（map/挂账序均按规范文本有序，排序收敛两种
        // 来源——NFR-COR-02 确定性）。
        std::sort(scan.uncommittedRevisions.begin(),
                  scan.uncommittedRevisions.end());

        // ---- ④ 后半：闭包内全部对象引用做 size＋SHA-256 校验（§7.4④
        // "校验 HEAD 引用闭包完整性（对象/清单哈希）"——全量档）。清单
        // 哈希半边＝装载期 parse 拒绝（上文），对象半边在此。
        std::vector<objstore::ObjectKey> references;
        references.reserve(closure.revisions.size());
        for (const RevisionId& rid : closure.revisions) {
            const auto manifest = loader.tryRevision(rid);
            if (!manifest) {
                continue;  // 不可达防御（闭包成员必已注册）
            }
            // 闭包成员引用的元数据若在挂账集中＝闭包损坏（定位细节随
            // 用户诊断 cause 上报——§7.4④"定位到文件"）。
            const auto failedMeta = failedMetaRefs.find(manifest->metadataRef);
            if (failedMeta != failedMetaRefs.end()) {
                broken = true;
                brokenDetail = failedMeta->second;
                reportDev("recovery-closure-metadata-failed "
                          + failedMeta->second);
            }
            for (const ObjectRef& ref : manifest->objectRefs) {
                references.push_back(objstore::ObjectKey{ref.objectId,
                                                         ref.contentVersion});
            }
        }
        const objstore::ObjectScanReport objReport =
            m_objects->scanObjects(references, true);
        if (!objReport.missingReferenced.empty()
            || !objReport.corruptReferenced.empty()) {
            broken = true;
            brokenDetail = "project/tx-engine: 闭包对象完整性失败 missing="
                + std::to_string(objReport.missingReferenced.size())
                + " corrupt="
                + std::to_string(objReport.corruptReferenced.size());
            reportDev("recovery-object-integrity-failed missing="
                      + std::to_string(objReport.missingReferenced.size())
                      + " corrupt="
                      + std::to_string(objReport.corruptReferenced.size()));
        }
        // ---- ⑤ 悬挂对象计数（只读不删——GC 范围外，§4.1 objects 行）。
        scan.danglingObjectCount = objReport.danglingOnDisk.size();
        if (scan.danglingObjectCount > 0) {
            reportDev("recovery-dangling-objects count="
                      + std::to_string(scan.danglingObjectCount)
                      + "（闭包外对象——只读计数，不删除）");
        }
    } else {
        // 闭包不可计算：挂账元数据失败无法归类为"闭包内/外"——全部按
        // 开发诊断交底（损坏结论已由闭包失败承载，用户诊断单发不重复）。
        for (const auto& [ref, detail] : failedMetaRefs) {
            reportDev("recovery-metadata-load-failed " + detail);
        }
    }

    if (broken) {
        scan.headIntegrityVerified = false;
        // 用户级损坏诊断（PRJ-STORE-CORRUPT；定位细节随 record.cause）。
        emitReport(m_sink, makeStoreCorruptRecord(brokenDetail));
    } else {
        scan.headIntegrityVerified = true;
    }
    return scan;
}

HeadRecord TxEngine::readHead() const
{
    const auto bytes = readAllBytes(m_projectDir / "HEAD");
    if (!bytes) {
        // §4.1 HEAD 行"损坏/缺失＝CorruptStoreDetected（PM-02 读校验）"
        // ——缺失与损坏同落 store-corrupt（§4.4.8 token），不伪装成
        // "空项目"（新建项目走 createNew，不以缺 HEAD 形态存在）。
        throw StoreError(StoreErrorCode::StoreCorrupt,
                         "project/tx-engine: head-missing path="
                             + (m_projectDir / "HEAD").string());
    }
    // 解析拒绝（format-legacy/schema-future/store-corrupt）原样传播
    // ——版本判定语义归 Codec（§8.11），本层不重复实现。
    return codec::parseHeadRecord(*bytes);
}

// =====================================================================
// 诊断出口
// =====================================================================

void TxEngine::reportDev(const std::string& message) const
{
    if (m_sink != nullptr) {
        // 通道 token 约定前缀 "project/"（§5.0——diagnostics 侧分流/脱敏
        // 路由依据）。
        m_sink->reportDev("project/tx-engine", message);
    }
}

}  // namespace sdurws::ird::project::tx
