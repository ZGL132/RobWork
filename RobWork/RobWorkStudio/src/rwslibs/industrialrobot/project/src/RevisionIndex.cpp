/**
 * @file   RevisionIndex.cpp
 * @brief  修订索引（RevisionIndex）实现——闭包双通道行走、发布边界不变量
 *         校验、D-5 底本拷贝、O-15 序号分配（设计语义见 RevisionIndex.hpp
 *         文件头，此处注释聚焦每段的"为什么"与算法步骤）。
 *
 * 设计依据：同 RevisionIndex.hpp 文件头（units/project.md §4.3/§4.5/§4.5.1/
 * §15.1 D-4/D-5、§6.1 S6、任务契约 tasks/foundation/PRJ-T06.json）。
 */

#include "RevisionIndex.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace sdurws::ird::project::revindex {
namespace {

// ---------------------------------------------------------------------
// 局部辅助：StoreError 的 detail 统一走 "project/revindex: <键>=<值>"
// 前缀（单元卡 §5.0 对 detail 的约定——机器可读、面向开发诊断；用户可见
// 文案归 diagnostics 供文案链路，P-PR-6）。稳定码由调用点从
// StoreErrorCode 枚举取用，此处只负责把 detail 拼装整齐。
// ---------------------------------------------------------------------

/// 拼装 "project/revindex: " 前缀的开发诊断明细（拼接风格与 codec 域一致）。
std::string revIndexDetail(const std::string& message)
{
    return "project/revindex: " + message;
}

/// 身份的规范文本（无效保留值给出可读标记——诊断里不能出现空串让人误判）。
std::string idText(const std::string& canonical)
{
    return canonical.empty() ? "<invalid>" : canonical;
}

/// 修订身份的规范文本（core 公共 API——P-PR-1：零本地格式化）。
std::string revText(const RevisionId& id)
{
    return idText(id.toCanonical());
}

/// 元数据引用键的 "oid/cv" 复合文本（诊断用；core 公共 API 拼接）。
std::string refText(const ObjectRefPair& ref)
{
    return idText(ref.objectId.toCanonical()) + "/" + idText(ref.contentVersion.toCanonical());
}

/**
 * @brief 两个 ObjectRefPair 是否同一元数据版本（键全等——编址即身份）。
 *
 * 独立小函数只为闭包/校验两处行走代码的可读性（"同键即同版本"是谱系
 * 行走的唯一相等判据——内容寻址下同键必同字节，§4.6）。
 */
bool sameRef(const ObjectRefPair& a, const ObjectRefPair& b) noexcept
{
    return a.objectId == b.objectId && a.contentVersion == b.contentVersion;
}

}  // namespace

// =====================================================================
// 装载：registerRevision / registerMetadata
// =====================================================================

void RevisionIndex::registerRevision(const RevisionManifest& manifest)
{
    // 构造防御（调用方错误 fail-fast）：无效身份/空引用集的清单不允许入
    // 索引——这种输入来自调用方构造错误而非磁盘数据（磁盘数据经 Codec
    // parse 已拒），按 AGENTS §3 归 fail-fast。
    if (!manifest.revisionId.isValid()) {
        throw std::invalid_argument(revIndexDetail("registerRevision: revisionId 无效（全零保留值）"));
    }
    if (manifest.objectRefs.empty()) {
        // §4.4.2 表：objectRefs ≥1（完整状态闭包必含元数据对象本身）。
        throw std::invalid_argument(revIndexDetail(
            "registerRevision: objectRefs 为空（§4.4.2 ≥1 约束） revision="
            + revText(manifest.revisionId)));
    }
    if (!manifest.metadataRef.objectId.isValid() || !manifest.metadataRef.contentVersion.isValid()) {
        throw std::invalid_argument(revIndexDetail(
            "registerRevision: metadataRef 身份无效 revision=" + revText(manifest.revisionId)));
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    // 幂等注册：同 id 同内容＝重复喂入（打开协议与恢复扫描的重放），静默
    // 成功；同 id 不同内容＝同一修订身份出现两个版本——存储损坏（数据侧
    // StoreCorrupt），不得静默合并掩盖分歧。
    const auto it = m_revisions.find(manifest.revisionId);
    if (it != m_revisions.end()) {
        if (!(it->second == manifest)) {
            throw StoreError(StoreErrorCode::StoreCorrupt,
                             revIndexDetail("registerRevision: 同一修订身份注册了不同内容（损坏） revision="
                                            + revText(manifest.revisionId)));
        }
        return;
    }
    // 深拷贝入索引：注册后调用方修改自己的清单副本不影响索引（索引内容
    // 一经注册不可变——PA-2"既有读者引用永不被覆盖"的模型层形态）。
    m_revisions.emplace(manifest.revisionId, manifest);
}

void RevisionIndex::registerMetadata(const ObjectRefPair& ref, const ProjectMetadataRecord& record)
{
    // 构造防御：引用键两身份、committedBy 必须有效；分支表非空（§4.4.3 ≥1）。
    if (!ref.objectId.isValid() || !ref.contentVersion.isValid()) {
        throw std::invalid_argument(revIndexDetail("registerMetadata: 引用键身份无效"));
    }
    if (!record.committedBy.isValid()) {
        throw std::invalid_argument(revIndexDetail(
            "registerMetadata: committedBy 无效 ref=" + refText(ref)));
    }
    if (record.branches.empty()) {
        throw std::invalid_argument(revIndexDetail(
            "registerMetadata: branches 为空（§4.4.3 ≥1 约束） ref=" + refText(ref)));
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    const auto it = m_metadata.find(ref);
    if (it != m_metadata.end()) {
        // 同键必同字节（内容寻址，§4.6）——内容不同即编址被破坏，数据侧拒绝。
        if (!(it->second == record)) {
            throw StoreError(StoreErrorCode::StoreCorrupt,
                             revIndexDetail("registerMetadata: 同一引用键注册了不同内容（损坏） ref="
                                            + refText(ref)));
        }
        return;
    }
    m_metadata.emplace(ref, record);
}

// =====================================================================
// 查询：tryRevision / tryMetadata / branchTip
// =====================================================================

std::optional<RevisionManifest> RevisionIndex::tryRevision(const RevisionId& id) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto it = m_revisions.find(id);
    if (it == m_revisions.end()) {
        return std::nullopt;
    }
    return it->second;  // 值语义深拷贝——PA-3：调用方与索引后续变化隔离
}

std::optional<ProjectMetadataRecord> RevisionIndex::tryMetadata(const ObjectRefPair& ref) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto it = m_metadata.find(ref);
    if (it == m_metadata.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::optional<RevisionId> RevisionIndex::branchTip(const ProjectMetadataRecord& metadata,
                                                   const BranchId& branchId)
{
    // 线性扫描而非建映射：分支表规模＝分支数（个位数量级），一次调用
    // O(n) 无意义优化；静态纯函数无共享状态，天然线程安全。
    for (const BranchRecord& branch : metadata.branches) {
        if (branch.branchId == branchId) {
            return branch.tipRevisionId;
        }
    }
    return std::nullopt;
}

// =====================================================================
// 序号分配：lastRevisionSeq / adoptHeadSeq / allocateRevisionSeq
// =====================================================================

std::uint64_t RevisionIndex::lastRevisionSeq() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_lastRevisionSeq;
}

void RevisionIndex::adoptHeadSeq(std::uint64_t headSeq)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    // 只升不降：水位单调是 O-15 的结构前提；同进程内二次 adopt（理论上
    // 不发生——每上下文一个索引）也不会把水位拉回。
    if (headSeq > m_lastRevisionSeq) {
        m_lastRevisionSeq = headSeq;
    }
}

std::uint64_t RevisionIndex::allocateRevisionSeq()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    // 调用即消费：本方法必须在事务提交决策点之后调用（§6.1 S6 内分配＝
    // 权威 HEAD 的 seq+1）；失败路径不调用即不消费。一旦分配，进程内不
    // 回收——跨会话断点归 adoptHeadSeq（见头注"崩溃残留序号可重合"）。
    return ++m_lastRevisionSeq;
}

// =====================================================================
// D-5：deriveNextMetadata——权威元数据底本拷贝更新
// =====================================================================

ProjectMetadataRecord RevisionIndex::deriveNextMetadata(
    const ProjectMetadataRecord& authoritative,
    const ObjectRefPair& authoritativeRef,
    const MetadataDelta& delta,
    const RevisionId& committedBy)
{
    // 构造防御（调用方错误 fail-fast）——以下输入违约全部是代码构造错误，
    // 不存在合法磁盘来源，按 AGENTS §3 归 fail-fast 而非稳定码。
    if (!authoritativeRef.objectId.isValid() || !authoritativeRef.contentVersion.isValid()) {
        throw std::invalid_argument(revIndexDetail("deriveNextMetadata: authoritativeRef 身份无效"));
    }
    if (!committedBy.isValid()) {
        throw std::invalid_argument(revIndexDetail("deriveNextMetadata: committedBy 无效（全零保留值）"));
    }
    if (delta.tipUpdate) {
        if (!delta.tipUpdate->branchId.isValid() || !delta.tipUpdate->newTip.isValid()) {
            throw std::invalid_argument(revIndexDetail("deriveNextMetadata: tipUpdate 身份无效"));
        }
    }

    // 第 1 步：底本＝权威版本的深拷贝。分支表/显示名/主分支/schemeLabels
    // 全部原样保留——既有条目的 label/base/createdAt 逐字带过（P-PR-8：
    // label 一次写入，本函数不存在任何改名路径）。
    ProjectMetadataRecord next = authoritative;
    // 第 2/3 步：committedBy＝本次新修订；supersedes 收口到权威版本（D-5
    // 谱系链——发布校验按同值核对，见 validateMetadataPublish 第 5 步）。
    next.committedBy = committedBy;
    next.supersedes = authoritativeRef;

    // 第 4 步：活动分支 tip 更新（线性查找——分支表个位数量级，同
    // branchTip 的取舍）。
    if (delta.tipUpdate) {
        bool found = false;
        for (BranchRecord& branch : next.branches) {
            if (branch.branchId == delta.tipUpdate->branchId) {
                branch.tipRevisionId = delta.tipUpdate->newTip;
                found = true;
                break;
            }
        }
        if (!found) {
            // §6.1 S1 已保证"branch 存在于权威元数据"——此处再遇不存在即
            // 调用方构造违约。
            throw std::invalid_argument(revIndexDetail(
                "deriveNextMetadata: tipUpdate.branchId 不在权威分支表 branch="
                + idText(delta.tipUpdate->branchId.toCanonical())));
        }
    }

    // 第 5 步：显式声明的分支表新增条目（CreateBranch）。重复判定＝branchId
    // 不得与底本既有条目或本批次已追加的条目重复（INV-M1a 的构造面）——
    // branchTip 查的是 next，而 next 已含先前迭代追加的条目，一次查找即
    // 覆盖两类重复。
    for (const BranchRecord& added : delta.addedBranches) {
        if (!added.branchId.isValid() || !added.baseRevisionId.isValid()
            || !added.tipRevisionId.isValid()) {
            throw std::invalid_argument(revIndexDetail("deriveNextMetadata: addedBranches 含无效身份"));
        }
        if (branchTip(next, added.branchId).has_value()) {
            throw std::invalid_argument(revIndexDetail(
                "deriveNextMetadata: 新增分支与既有分支表/本批次重复 branch="
                + idText(added.branchId.toCanonical())));
        }
        next.branches.push_back(added);
    }

    return next;
}

// =====================================================================
// 发布边界校验：validateMetadataPublish（INV-M1/M2/M3）
// =====================================================================

void RevisionIndex::validateMetadataPublish(const ProjectMetadataRecord& authoritative,
                                            const ObjectRefPair& authoritativeRef,
                                            const ProjectMetadataRecord& candidate,
                                            const ObjectRefPair& candidateRef,
                                            const RevisionId& newRevisionId) const
{
    // 抛 BranchMetadataRegression 的局部捷径：不变量违约统一走稳定码
    // branch-metadata-regression（§4.4.8"防御性，§4.5 不变量"），detail 带
    // invariant 键供开发诊断定位是哪条不变量在哪个字段拒绝。
    auto reject = [](const std::string& invariant, const std::string& message) {
        throw StoreError(StoreErrorCode::BranchMetadataRegression,
                         revIndexDetail("invariant=" + invariant + " " + message));
    };

    // ---- 时序契约核查（调用方错误 fail-fast——先注册后校验，见头注）----
    // 新修订必须已注册：不回退判定要沿它的 parent 链走、谱系单调判定要
    // 读它的 seq。未注册＝调用方没按 §6.1 S6 时序喂入。
    std::uint64_t newRevisionSeq = 0;
    BranchId newRevisionBranch{};
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto it = m_revisions.find(newRevisionId);
        if (it == m_revisions.end()) {
            throw std::invalid_argument(revIndexDetail(
                "validateMetadataPublish: newRevisionId 未注册（须先 registerRevision） revision="
                + revText(newRevisionId)));
        }
        // 锁内拷贝本检查所需字段（seq/所属分支）——后续检查段按值使用，
        // 不跨锁持有容器内部指针（节点虽然地址稳定，值拷贝让数据流更直白）。
        newRevisionSeq = it->second.revisionSeq;
        newRevisionBranch = it->second.branchId;
        // 权威版本必须已注册且与传入记录全等：校验全程以注册内容为谱系
        // 行走的地面真值；传入的 authoritative 只是调用方持有的副本，二者
        // 不一致＝喂入失真。
        const auto authIt = m_metadata.find(authoritativeRef);
        if (authIt == m_metadata.end()) {
            throw std::invalid_argument(revIndexDetail(
                "validateMetadataPublish: authoritativeRef 未注册 ref=" + refText(authoritativeRef)));
        }
        if (!(authIt->second == authoritative)) {
            throw std::invalid_argument(revIndexDetail(
                "validateMetadataPublish: authoritativeRef 注册内容与传入记录不一致 ref="
                + refText(authoritativeRef)));
        }
    }
    if (candidateRef == authoritativeRef) {
        // 同键即同字节同版本（内容寻址，§4.6）——"新版本"与权威完全相同
        // ＝没有新元数据可发布，病态调用按不变量拒绝。
        reject("no-new-version",
               "candidateRef 与 authoritativeRef 同编址（同内容＝无新版本）");
    }
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_metadata.find(candidateRef) != m_metadata.end()) {
            throw std::invalid_argument(revIndexDetail(
                "validateMetadataPublish: candidateRef 已注册（同内容重复发布） ref="
                + refText(candidateRef)));
        }
    }

    // ---- 构造面防御（无效身份＝调用方构造错误，fail-fast）----
    if (!candidate.committedBy.isValid()) {
        throw std::invalid_argument(revIndexDetail("validateMetadataPublish: candidate.committedBy 无效"));
    }
    if (!candidateRef.objectId.isValid() || !candidateRef.contentVersion.isValid()) {
        throw std::invalid_argument(revIndexDetail("validateMetadataPublish: candidateRef 身份无效"));
    }
    for (const BranchRecord& branch : candidate.branches) {
        if (!branch.branchId.isValid() || !branch.baseRevisionId.isValid()
            || !branch.tipRevisionId.isValid()) {
            throw std::invalid_argument(revIndexDetail(
                "validateMetadataPublish: 候选分支表条目含无效身份 label=" + branch.label));
        }
    }

    // ---- INV-M1a：分支表非空且 branchId 唯一（§4.4.3）----
    if (candidate.branches.empty()) {
        reject("INV-M1", "候选分支表为空（§4.4.3 ≥1 约束）");
    }
    {
        std::unordered_set<std::string> seen;
        seen.reserve(candidate.branches.size() * 2);
        for (const BranchRecord& branch : candidate.branches) {
            if (!seen.insert(branch.branchId.toCanonical()).second) {
                reject("INV-M1",
                       "候选分支表 branchId 重复 branch=" + branch.branchId.toCanonical());
            }
        }
    }

    // ---- INV-M1b：每个 tip 指向已注册修订（§4.4.3"已存在修订"）----
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (const BranchRecord& branch : candidate.branches) {
            if (m_revisions.find(branch.tipRevisionId) == m_revisions.end()) {
                reject("INV-M1",
                       "tipRevisionId 指向不存在修订 branch=" + branch.branchId.toCanonical()
                           + " tip=" + revText(branch.tipRevisionId));
            }
        }
    }

    // ---- committedBy 绑定：候选元数据必须由本次新修订提交（§4.4.3）----
    if (!(candidate.committedBy == newRevisionId)) {
        reject("committedBy",
               "candidate.committedBy 必须为本次新修订 committedBy="
                   + revText(candidate.committedBy)
                   + " newRevision=" + revText(newRevisionId));
    }

    // ---- D-5 链：候选 supersedes 必须恰为权威版本（谱系收口）----
    if (!candidate.supersedes.has_value()) {
        reject("D-5", "候选 supersedes 缺失（谱系断裂——必须以权威版本为前驱）");
    } else if (!sameRef(*candidate.supersedes, authoritativeRef)) {
        reject("D-5",
               "候选 supersedes 未指向权威版本 supersedes=" + refText(*candidate.supersedes)
                   + " authoritative=" + refText(authoritativeRef));
    }

    // ---- INV-M3：超集＋不可变条目＋tip 不回退 ----
    // 以权威分支表建 id→条目索引（个位数量级，直接线性；先建候选侧映射
    // 便于逐权威条目核对）。
    {
        std::unordered_map<std::string, const BranchRecord*> candidateById;
        candidateById.reserve(candidate.branches.size() * 2);
        for (const BranchRecord& branch : candidate.branches) {
            candidateById.emplace(branch.branchId.toCanonical(), &branch);
        }
        for (const BranchRecord& authBranch : authoritative.branches) {
            const auto it = candidateById.find(authBranch.branchId.toCanonical());
            if (it == candidateById.end()) {
                // 分支丢失＝§4.5.1 结论①的反例（以父修订元数据 M0 重建时
                // A/B 分支整体消失）——INV-M3 超集约束拒绝。
                reject("INV-M3",
                       "候选分支表丢失既有分支 branch=" + authBranch.branchId.toCanonical());
            }
            const BranchRecord& candBranch = *it->second;
            // 不可变条目核查：label（P-PR-8 一次写入）、base（创建时记录
            // 后不变，§4.5 三种"修订指针"表）、createdAt（同属创建事实）
            // 三者被改动＝回退防御拒绝。
            if (candBranch.label != authBranch.label) {
                reject("P-PR-8",
                       "既有分支 label 被改名 branch=" + authBranch.branchId.toCanonical()
                           + "（label 创建时一次写入，不做分支改名）");
            }
            if (!(candBranch.baseRevisionId == authBranch.baseRevisionId)) {
                reject("INV-M3",
                       "既有分支 baseRevisionId 被改写 branch=" + authBranch.branchId.toCanonical());
            }
            if (candBranch.createdAtUtc != authBranch.createdAtUtc) {
                reject("INV-M3",
                       "既有分支 createdAtUtc 被改写 branch=" + authBranch.branchId.toCanonical());
            }
            // tip 不回退：未变＝本提交未触及该分支，通过；变了＝必须"恰
            // 为新修订＋该分支即新修订所属分支＋旧 tip 是新修订的真祖先"
            // ——三者共同排除"回退到旧修订"与"平移到无关修订"。
            if (!(candBranch.tipRevisionId == authBranch.tipRevisionId)) {
                if (!(candBranch.tipRevisionId == newRevisionId)) {
                    reject("INV-M3",
                           "既有分支 tip 未推进到新修订 branch=" + authBranch.branchId.toCanonical()
                               + " tip=" + revText(candBranch.tipRevisionId));
                }
                if (!(newRevisionBranch == authBranch.branchId)) {
                    reject("INV-M3",
                           "非新修订所属分支的 tip 被变更 branch=" + authBranch.branchId.toCanonical());
                }
                std::lock_guard<std::mutex> lock(m_mutex);
                if (!isProperAncestorLocked(authBranch.tipRevisionId, newRevisionId)) {
                    reject("INV-M3",
                           "既有分支 tip 回退（旧 tip 非新修订的祖先） branch="
                               + authBranch.branchId.toCanonical()
                               + " oldTip=" + revText(authBranch.tipRevisionId)
                               + " newTip=" + revText(newRevisionId));
                }
            }
        }
        // 新增条目（不在权威分支表＝本次显式声明的分支表变更，如
        // CreateBranch）：base 必须指向已注册修订——分支自既有修订分叉，
        // 悬挂 base 的分支表同样构成 INV-M1"指向不存在修订"。
        for (const BranchRecord& candBranch : candidate.branches) {
            bool isNew = true;
            for (const BranchRecord& authBranch : authoritative.branches) {
                if (authBranch.branchId == candBranch.branchId) {
                    isNew = false;
                    break;
                }
            }
            if (!isNew) {
                continue;
            }
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_revisions.find(candBranch.baseRevisionId) == m_revisions.end()) {
                reject("INV-M1",
                       "新增分支 baseRevisionId 指向不存在修订 branch="
                           + candBranch.branchId.toCanonical()
                           + " base=" + revText(candBranch.baseRevisionId));
            }
        }
    }

    // ---- INV-M2：谱系无环＋版本单调（自 candidateRef 起，经注册记录行走）----
    //
    // 行走序列：candidate（committedBy＝新修订）→ authoritativeRef → 其
    // supersedes → … 直至首版（supersedes 缺失）。两件事逐趟检查：
    //   无环——下一跳引用键出现在已访问集即成环（含 candidate.supersedes
    //   自指 candidateRef 的自环、经已注册记录回指的间接环）；
    //   单调——committedBy 修订的 seq 沿行走方向严格递减（越旧的元数据由
    //   越旧的修订提交）。悬挂下一跳（未注册）＝喂入不完整，按数据侧
    //   StoreCorrupt 拒（校验前提是磁盘内容已全部装载）。
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::unordered_set<std::string> visited;
        visited.reserve(16);
        visited.insert(refText(candidateRef));  // 自环判据的种子：回到候选自身即环

        std::uint64_t previousSeq = newRevisionSeq;
        ObjectRefPair cursor = authoritativeRef;
        while (true) {
            if (visited.count(refText(cursor)) != 0) {
                reject("INV-M2",
                       "元数据谱系成环 cursor=" + refText(cursor));
            }
            visited.insert(refText(cursor));

            const auto metaIt = m_metadata.find(cursor);
            if (metaIt == m_metadata.end()) {
                throw StoreError(StoreErrorCode::StoreCorrupt,
                                 revIndexDetail("谱系引用悬挂（未注册） ref=" + refText(cursor)));
            }
            const ProjectMetadataRecord& record = metaIt->second;
            const auto revIt = m_revisions.find(record.committedBy);
            if (revIt == m_revisions.end()) {
                throw StoreError(StoreErrorCode::StoreCorrupt,
                                 revIndexDetail("谱系 committedBy 修订悬挂（未注册） committedBy="
                                                + revText(record.committedBy)));
            }
            // 版本单调（INV-M2）：当前记录的提交修订必须严格旧于谱系中它
            // 的后继（后继 seq 初值＝新修订的 seq，逐跳下移）。相等也不可能
            // 合法出现——每次提交恰好一个新修订＋至多一个新元数据版本
            // （§6.1 S6）。
            if (revIt->second.revisionSeq >= previousSeq) {
                reject("INV-M2",
                       "谱系版本单调违约 committedBy=" + revText(record.committedBy)
                           + " seq=" + std::to_string(revIt->second.revisionSeq)
                           + " 后继 seq=" + std::to_string(previousSeq));
            }
            previousSeq = revIt->second.revisionSeq;

            if (!record.supersedes.has_value()) {
                break;  // 首版元数据——谱系走完，无环且单调
            }
            cursor = *record.supersedes;
        }
    }
}

// =====================================================================
// 提交闭包：committedClosure（D-4 双通道）
// =====================================================================

CommittedClosure RevisionIndex::committedClosure(const RevisionId& headRevisionId) const
{
    if (!headRevisionId.isValid()) {
        throw std::invalid_argument(revIndexDetail("committedClosure: headRevisionId 无效（全零保留值）"));
    }

    // 访问集＋显式工作栈（迭代化——修订链深可达数万，递归有栈溢出风险；
    // §4.5 伪码的 visit_rev/visit_meta 互递归在此展开为两段式工作项）。
    std::lock_guard<std::mutex> lock(m_mutex);

    std::vector<RevisionId> revOrder;   // 访问序（仅用于结果排序前收集）
    std::vector<ObjectRefPair> metaOrder;
    std::unordered_set<std::string> revSeen;
    std::unordered_set<std::string> metaSeen;
    revSeen.reserve(64);
    metaSeen.reserve(16);

    // 修订栈：起点先入栈；栈空即闭包完毕。
    std::vector<RevisionId> revStack{headRevisionId};

    while (!revStack.empty()) {
        // ---- 通道一：visit_rev(r)——标记 r，沿 parent 上行 ----
        const RevisionId revId = revStack.back();
        revStack.pop_back();
        if (!revSeen.insert(revText(revId)).second) {
            continue;  // 已访问＝合流（DAG 菱形），去重即终止保证
        }
        const auto revIt = m_revisions.find(revId);
        if (revIt == m_revisions.end()) {
            // 行走遇未注册修订＝悬挂引用：调用方声称已装载全部磁盘清单，
            // 缺失即损坏（数据侧 StoreCorrupt）——不得静默截断闭包，否则
            // 残留判定会把 committed 历史误报为未提交。
            throw StoreError(StoreErrorCode::StoreCorrupt,
                             revIndexDetail("闭包行走遇未注册修订 revision=" + revText(revId)));
        }
        const RevisionManifest& manifest = revIt->second;
        revOrder.push_back(revId);
        if (manifest.parentRevisionId.has_value()) {
            revStack.push_back(*manifest.parentRevisionId);
        }

        // ---- 通道二：visit_meta(r.metadataRef)——谱系行走 ----
        ObjectRefPair metaCursor = manifest.metadataRef;
        while (true) {
            if (!metaSeen.insert(refText(metaCursor)).second) {
                break;  // 已访问的元数据版本——其谱系已走完
            }
            const auto metaIt = m_metadata.find(metaCursor);
            if (metaIt == m_metadata.end()) {
                throw StoreError(StoreErrorCode::StoreCorrupt,
                                 revIndexDetail("闭包行走遇未注册元数据 ref=" + refText(metaCursor)));
            }
            const ProjectMetadataRecord& record = metaIt->second;
            metaOrder.push_back(metaCursor);
            // 谱系回连（D-4 的第二通道关键步）：把 committedBy 修订作为
            // 修订继续访问——其 parent 链随主循环上行（§4.5.1 结论③
            // "M4→M3(committedBy r3)→r3→r0"的行走形态）。若无此步，
            // CreateBranch 修订（无子修订、非 tip）将成为孤岛被误判。
            if (!record.committedBy.isValid()) {
                throw StoreError(StoreErrorCode::StoreCorrupt,
                                 revIndexDetail("闭包行走遇无效 committedBy ref=" + refText(metaCursor)));
            }
            revStack.push_back(record.committedBy);
            if (!record.supersedes.has_value()) {
                break;  // 首版元数据——谱系走完
            }
            metaCursor = *record.supersedes;
        }
    }

    // ---- 确定性排序（NFR-COR-02）：同输入必得同输出序 ----
    // revisions 按 (seq 升序, id 规范文本升序)；metadata 按 (oid, cv) 规范
    // 文本升序。seq 从索引内清单读取（调用点已保证存在）。
    CommittedClosure closure;
    closure.revisions.reserve(revOrder.size());
    for (const RevisionId& id : revOrder) {
        closure.revisions.push_back(id);
    }
    std::sort(closure.revisions.begin(), closure.revisions.end(),
              [this](const RevisionId& a, const RevisionId& b) {
                  // 已在锁内（本方法持锁全程）——直接查表读 seq。
                  const auto ia = m_revisions.find(a);
                  const auto ib = m_revisions.find(b);
                  const std::uint64_t sa = ia->second.revisionSeq;
                  const std::uint64_t sb = ib->second.revisionSeq;
                  if (sa != sb) {
                      return sa < sb;
                  }
                  return a.toCanonical() < b.toCanonical();
              });
    closure.metadata = std::move(metaOrder);
    std::sort(closure.metadata.begin(), closure.metadata.end(),
              [](const ObjectRefPair& a, const ObjectRefPair& b) {
                  const std::string ka = a.objectId.toCanonical() + "/" + a.contentVersion.toCanonical();
                  const std::string kb = b.objectId.toCanonical() + "/" + b.contentVersion.toCanonical();
                  return ka < kb;
              });
    return closure;
}

// =====================================================================
// 私有：isProperAncestorLocked（INV-M3"不回退"的几何判据）
// =====================================================================

bool RevisionIndex::isProperAncestorLocked(const RevisionId& oldTip, const RevisionId& newTip) const
{
    // 沿 newTip 的 parent 链向上找 oldTip；找到即"真祖先"（深度 ≥1）。
    // 相等不算（调用方在变更分支里已排除相等情形）；访问集防环死循环
    // （环本应在发布边界被拒，这里只保证终止）。
    std::unordered_set<std::string> seen;
    RevisionId cursor = newTip;
    while (true) {
        if (!seen.insert(revText(cursor)).second) {
            return false;  // 环——未遇 oldTip，判否（终止保证）
        }
        const auto it = m_revisions.find(cursor);
        if (it == m_revisions.end()) {
            // 行走遇未注册修订＝悬挂引用（校验时序契约已保证 newTip 注册，
            // 中途缺失＝喂入不完整）——数据侧拒绝。
            throw StoreError(StoreErrorCode::StoreCorrupt,
                             revIndexDetail("祖先判定遇未注册修订 revision=" + revText(cursor)));
        }
        if (!it->second.parentRevisionId.has_value()) {
            return false;  // 链到首修订仍未遇 oldTip——非祖先
        }
        cursor = *it->second.parentRevisionId;
        if (cursor == oldTip) {
            return true;
        }
    }
}

}  // namespace sdurws::ird::project::revindex
