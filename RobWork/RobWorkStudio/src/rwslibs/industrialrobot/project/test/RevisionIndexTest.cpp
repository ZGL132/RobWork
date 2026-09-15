/**
 * @file   RevisionIndexTest.cpp
 * @brief  修订索引（RevisionIndex）用例组——§4.5.1 分支走查七步（PRJ-TX-5
 *         模型部分）、元数据不变量注入（PRJ-TX-12）、修订单调序号分配
 *         （O-15）、D-5 底本拷贝更新与陷阱处置自证（P-PR-1/P-PR-8）。
 *
 * 设计依据：
 *   - units/project.md §4.5.1（走查七步表——本文件 Walkthrough 组的逐步
 *     断言基准，含结论①～④）、§4.5（闭包双通道伪码、INV-M2/M3 不变量、
 *     三种"修订指针"表）、§4.4.3（INV-M1）、§4.4.8（稳定码
 *     branch-metadata-regression——发布边界拒绝的码值）、§6.1（S6 内分配
 *     ＝权威 HEAD seq+1）、§15.1 D-4/D-5/D-06、§11 PRJ-TX-5/PRJ-TX-12 行；
 *   - 需求 ARC-01（修订历史可追溯——seq 单调/闭包完整）、PM-12（分支
 *     baseRevisionId 引用不复制对象）、PA-2（不可变历史——D-5 机制化）、
 *     AT-29（分支走查）；
 *   - 任务契约 tasks/foundation/PRJ-T06.json acceptance 1～4（用例名后缀
 *     标注对应条目序号；dtb 行＝WP-04-T06）。
 *
 * 陷阱处置自证（acceptance 4）：
 *   - P-PR-1：身份类型消费面仅 core 公共契约（Identity.hpp/Digest.hpp 的
 *     fromCanonical/toCanonical——CoreContract 组钉住"索引存取的身份一律
 *     core 规范文本、零本地第二格式化"）；include 面红线由
 *     BuildRedLineTest/LinkageContractTest 持续扫描（PRJ-T01 既有）。
 *   - P-PR-8：分支 label 创建时一次写入——Derive 组钉住"derive 不存在
 *     改名路径"，PublishValidation 组以 label 漂移注入钉住"发布边界拒绝
 *     改名"（双重防线）。
 */

#include "RevisionIndex.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/project/StoreTypes.hpp>

using sdurws::ird::core::BranchId;
using sdurws::ird::core::ContentVersion;
using sdurws::ird::core::ObjectId;
using sdurws::ird::core::RevisionId;
using sdurws::ird::project::BranchRecord;
using sdurws::ird::project::ObjectRefPair;
using sdurws::ird::project::ProjectMetadataRecord;
using sdurws::ird::project::RevisionManifest;
using sdurws::ird::project::StoreError;
using sdurws::ird::project::StoreErrorCode;
namespace revindex = sdurws::ird::project::revindex;
using revindex::CommittedClosure;
using revindex::MetadataDelta;
using revindex::RevisionIndex;
using revindex::TipUpdate;

namespace {

// ---------------------------------------------------------------------
// 确定性身份构造（测试专用）：core 规范文本经 fromCanonical 解析——身份
// 的合法性由 core 严格解析保证（P-PR-1：测试侧同样不走本地格式化）。
// 固定字面值让走查断言失败时可直接对照 §4.5.1 表格读 debug。
// ---------------------------------------------------------------------

/// "<tag><全部 hex>" 定位填充：<tag>＋n 的十六进制右对齐到规定长度。
std::string paddedHex(const char* tag, std::uint64_t n, std::size_t totalHexDigits)
{
    char buffer[8];
    std::snprintf(buffer, sizeof(buffer), "%04llx",
                  static_cast<unsigned long long>(n & 0xffffull));
    // 低位 4 位来自 buffer，其余补 0——总数凑满 totalHexDigits。
    std::string digits(static_cast<std::size_t>(totalHexDigits) - 4, '0');
    digits += buffer;
    return std::string(tag) + digits;
}

/// 修订身份 rev-<32hex>（低 4 位编码 n；n≥1，全零＝core 保留值）。
RevisionId makeRev(std::uint64_t n)
{
    return RevisionId::fromCanonical(paddedHex("rev-", n, 32));
}

/// 分支身份 brn-<32hex>。
BranchId makeBrn(std::uint64_t n)
{
    return BranchId::fromCanonical(paddedHex("brn-", n, 32));
}

/// 对象身份 obj-<32hex>。
ObjectId makeOid(std::uint64_t n)
{
    return ObjectId::fromCanonical(paddedHex("obj-", n, 32));
}

/// 内容版本 cv-<64hex>（低 4 位编码 n；n≥1）。
ContentVersion makeCv(std::uint64_t n)
{
    return ContentVersion::fromCanonical(paddedHex("cv-", n, 64));
}

// ---------------------------------------------------------------------
// 走查场景常量（§4.5.1 表：main/A/B 三分支、r0～r5 五修订、M0～M4 五
// 元数据版本、X＝共享域内容对象）。序号从 1 起由分配器逐次产出。
// ---------------------------------------------------------------------

const BranchId kMain = makeBrn(1);
const BranchId kA = makeBrn(2);
const BranchId kB = makeBrn(3);

const RevisionId kR0 = makeRev(1);
const RevisionId kR1 = makeRev(2);
const RevisionId kR2 = makeRev(3);
const RevisionId kR3 = makeRev(4);
const RevisionId kR4 = makeRev(5);

/// 共享域内容对象（全部修订的 objectRefs 都引用它——引用共享，不复制）。
const ObjectId kContentOid = makeOid(30);
const ContentVersion kContentCv = makeCv(300);

/// 固定时间戳（走查断言不关心时刻；ISO-8601 UTC 文本，§4.4 约定）。
const char* kCreatedAt = "2026-09-16T00:00:00Z";

/// 对象引用（objectTypeToken/digest256 为透传字段，固定占位即可——
/// 索引不解释其内容）。
sdurws::ird::project::ObjectRef objectRef(const ObjectId& oid, const ContentVersion& cv)
{
    sdurws::ird::project::ObjectRef ref;
    ref.objectId = oid;
    ref.contentVersion = cv;
    ref.objectTypeToken = "TestObject";
    ref.digest256 = std::string(64, 'a');
    return ref;
}

/// 第 k 个元数据版本的对象引用键（每版本独立对象——不同内容不同 cv）。
ObjectRefPair metaRef(int k)
{
    return ObjectRefPair{makeOid(static_cast<std::uint64_t>(20 + k)),
                         makeCv(static_cast<std::uint64_t>(100 + k))};
}

/// 分支表条目构造（created 固定 kCreatedAt——创建事实字段）。
BranchRecord branchRecord(const BranchId& id, const char* label, const RevisionId& base,
                          const RevisionId& tip)
{
    BranchRecord record;
    record.branchId = id;
    record.label = label;
    record.baseRevisionId = base;
    record.tipRevisionId = tip;
    record.createdAtUtc = kCreatedAt;
    return record;
}

/// 元数据记录构造（显示名固定；分支表由调用方装配）。
ProjectMetadataRecord metadataRecord(const RevisionId& committedBy,
                                     const std::vector<BranchRecord>& branches)
{
    ProjectMetadataRecord record;
    record.committedBy = committedBy;
    record.projectDisplayName = "WalkthroughProject";
    record.primaryBranchId = kMain;
    record.branches = branches;
    return record;
}

/// 修订清单构造（objectRefs＝共享域对象＋本修订元数据对象——§4.4.2 ≥1
/// 且必含 metadataRef 指向的元数据对象）。
RevisionManifest manifestRecord(const RevisionId& id, std::uint64_t seq,
                                const std::optional<RevisionId>& parent,
                                const BranchId& branch, const ObjectRefPair& metadataRef)
{
    RevisionManifest manifest;
    manifest.revisionId = id;
    manifest.revisionSeq = seq;
    manifest.parentRevisionId = parent;
    manifest.branchId = branch;
    manifest.committedAtUtc = kCreatedAt;
    manifest.metadataRef = metadataRef;
    manifest.objectRefs.push_back(objectRef(kContentOid, kContentCv));
    manifest.objectRefs.push_back(objectRef(metadataRef.objectId, metadataRef.contentVersion));
    // introducedObjects 记录本修订新引入的对象（残留识别/浏览辅助）——
    // 这里只如实登记元数据对象；共享域对象由 r0 首引。
    manifest.introducedObjects.push_back(metadataRef.contentVersion);
    return manifest;
}

/// 断言可调用体抛出 BranchMetadataRegression（发布边界拒绝的统一稳定码，
/// §4.4.8；单次执行——不重复触发副作用）。
template <class Fn>
void expectRegression(Fn&& fn, const char* what)
{
    try {
        fn();
        ADD_FAILURE() << what << ": 预期 BranchMetadataRegression 未抛出";
    } catch (const StoreError& e) {
        EXPECT_TRUE(e.code() == StoreErrorCode::BranchMetadataRegression)
            << what << ": 稳定码不符，实得 " << static_cast<int>(e.code())
            << "，detail=" << e.what();
    }
}

/// 断言可调用体抛出 std::invalid_argument（调用方错误 fail-fast）。
template <class Fn>
void expectInvalidArgument(Fn&& fn, const char* what)
{
    try {
        fn();
        ADD_FAILURE() << what << ": 预期 std::invalid_argument 未抛出";
    } catch (const std::invalid_argument&) {
        // 预期路径
    }
}

/// 断言可调用体抛出 StoreCorrupt（数据侧损坏）。
template <class Fn>
void expectCorrupt(Fn&& fn, const char* what)
{
    try {
        fn();
        ADD_FAILURE() << what << ": 预期 StoreCorrupt 未抛出";
    } catch (const StoreError& e) {
        EXPECT_TRUE(e.code() == StoreErrorCode::StoreCorrupt)
            << what << ": 稳定码不符，实得 " << static_cast<int>(e.code())
            << "，detail=" << e.what();
    }
}

/// 闭包结果中的修订 id 集合（规范文本集合——断言包含/排除用）。
std::set<std::string> closureRevTexts(const CommittedClosure& closure)
{
    std::set<std::string> texts;
    for (const RevisionId& id : closure.revisions) {
        texts.insert(id.toCanonical());
    }
    return texts;
}

/// 走查状态的载体（索引＋各权威元数据＋注册键＋分配序号；出参填充）。
struct WalkthroughState {
    RevisionIndex index;
    std::vector<ProjectMetadataRecord> metadata;   // M0..M4
    std::vector<ObjectRefPair> refs;               // 对应注册键
    std::vector<std::uint64_t> seqs;               // r0..r4 的分配序号
};

/**
 * @brief 铺设"走查历史已提交到 r4"的最小索引状态（Walkthrough 组与部分
 *        注入组的公共前置——由 r0 逐铺到指定步；注入组随后从任意权威
 *        状态出发构造候选，避免用例间隐式耦合）。
 *
 * 出参形态（非返回值）：RevisionIndex 含互斥成员不可拷贝/移动，按值
 * 返回结构体依赖 NRVO（非保证）；出参引用让全部调用点零拷贝。
 * 字段语义：metadata/refs＝M0..M4 及其注册键；seqs＝r0..r4 的分配序号。
 */
void buildWalkthrough(WalkthroughState& st)
{
    // ---- 前置：初始提交 r0（main；M0{main→r0}；首版元数据无 supersedes）----
    st.seqs.push_back(st.index.allocateRevisionSeq());  // r0 → 1（§6.1：S6 内分配）
    st.refs.push_back(metaRef(0));
    st.metadata.push_back(metadataRecord(kR0, {branchRecord(kMain, "main", kR0, kR0)}));
    st.index.registerMetadata(st.refs[0], st.metadata[0]);
    // 首版元数据随项目创建产生（PRJ-T08 createNew 场景）——无权威前驱，
    // 不经过 validateMetadataPublish（头注契约：创建≠发布转移）。
    st.index.registerRevision(manifestRecord(kR0, st.seqs[0], std::nullopt, kMain, st.refs[0]));

    // ---- 步骤 1：CreateBranch A（base=r0）→ r1（parent=r0〔main tip〕；M1）----
    st.seqs.push_back(st.index.allocateRevisionSeq());  // r1 → 2
    st.refs.push_back(metaRef(1));
    st.metadata.push_back(revindex::RevisionIndex::deriveNextMetadata(
        st.metadata[0], st.refs[0],
        MetadataDelta{std::nullopt, {branchRecord(kA, "A", kR0, kR0)}}, kR1));
    st.index.registerRevision(manifestRecord(kR1, st.seqs[1], kR0, kMain, st.refs[1]));
    st.index.validateMetadataPublish(st.metadata[0], st.refs[0], st.metadata[1], st.refs[1], kR1);
    st.index.registerMetadata(st.refs[1], st.metadata[1]);

    // ---- 步骤 2：在 A 应用修改 → r2（parent=r0〔A.tip——分支 tip 维度〕；M2）----
    st.seqs.push_back(st.index.allocateRevisionSeq());  // r2 → 3
    st.refs.push_back(metaRef(2));
    st.metadata.push_back(revindex::RevisionIndex::deriveNextMetadata(
        st.metadata[1], st.refs[1],
        MetadataDelta{TipUpdate{kA, kR2}, {}}, kR2));
    st.index.registerRevision(manifestRecord(kR2, st.seqs[2], kR0, kA, st.refs[2]));
    st.index.validateMetadataPublish(st.metadata[1], st.refs[1], st.metadata[2], st.refs[2], kR2);
    st.index.registerMetadata(st.refs[2], st.metadata[2]);

    // ---- 步骤 3：CreateBranch B（base=r0，自 main）→ r3（parent=r0〔main tip〕；M3）----
    st.seqs.push_back(st.index.allocateRevisionSeq());  // r3 → 4
    st.refs.push_back(metaRef(3));
    st.metadata.push_back(revindex::RevisionIndex::deriveNextMetadata(
        st.metadata[2], st.refs[2],
        MetadataDelta{std::nullopt, {branchRecord(kB, "B", kR0, kR0)}}, kR3));
    st.index.registerRevision(manifestRecord(kR3, st.seqs[3], kR0, kA, st.refs[3]));
    st.index.validateMetadataPublish(st.metadata[2], st.refs[2], st.metadata[3], st.refs[3], kR3);
    st.index.registerMetadata(st.refs[3], st.metadata[3]);

    // ---- 步骤 4：切换 B（纯会话，零写入）——无任何索引动作 ----

    // ---- 步骤 5：在 B 应用修改 → r4（parent=r0〔B.tip〕；M4）----
    st.seqs.push_back(st.index.allocateRevisionSeq());  // r4 → 5
    st.refs.push_back(metaRef(4));
    st.metadata.push_back(revindex::RevisionIndex::deriveNextMetadata(
        st.metadata[3], st.refs[3],
        MetadataDelta{TipUpdate{kB, kR4}, {}}, kR4));
    st.index.registerRevision(manifestRecord(kR4, st.seqs[4], kR0, kB, st.refs[4]));
    st.index.validateMetadataPublish(st.metadata[3], st.refs[3], st.metadata[4], st.refs[4], kR4);
    st.index.registerMetadata(st.refs[4], st.metadata[4]);

    // ---- 步骤 6/7：返回 A、查询（纯读取）——在用例内断言 ----
}

}  // namespace

// =====================================================================
// Walkthrough 组——acceptance 1（PRJ-TX-5 模型部分：§4.5.1 七步走查；
// PM-12：baseRevisionId 不复制对象）。需求/AT：PM-12、AT-29、INV-M3。
// =====================================================================

/**
 * 用例：PRJ-TX-5 模型部分——走查七步逐表断言（acceptance 1）。
 * 前置：§4.5.1 场景（r0/main 起步）。操作：按表执行步骤 1～7。
 * 预期：每步发布边界放行；权威分支表逐字段符合表列；A/B 的 tip 在后续
 * 提交中不丢失/不回退（INV-M3）；HEAD 逐表推进。
 */
TEST(Walkthrough, SevenSteps_BranchTipsPreserved_Acc1)
{
    WalkthroughState st;
    buildWalkthrough(st);

    // ---- 步骤 1 断言：M1{main→r0；A(base r0, tip r0)}，HEAD=r1 ----
    EXPECT_EQ(st.metadata[1].branches.size(), 2u);
    EXPECT_EQ(revindex::RevisionIndex::branchTip(st.metadata[1], kMain), kR0);
    const std::optional<RevisionId> aTipAfterCreate = revindex::RevisionIndex::branchTip(st.metadata[1], kA);
    ASSERT_TRUE(aTipAfterCreate.has_value());
    EXPECT_EQ(*aTipAfterCreate, kR0);  // A 创建时 tip＝base＝r0（§4.5.1 表步骤 1）
    // A 的 base 记录创建起点 r0（PM-12：base 是引用不是拷贝——分支创建
    // 不产生对象复制，结构上本 API 就没有对象复制入口）。
    for (const BranchRecord& b : st.metadata[1].branches) {
        if (b.branchId == kA) {
            EXPECT_EQ(b.baseRevisionId, kR0);
            EXPECT_EQ(b.label, "A");  // P-PR-8：label 创建时一次写入
        }
    }

    // ---- 步骤 2 断言：M2{main→r0；A→r2}；r2 的 parent＝r0（A.tip）----
    EXPECT_EQ(revindex::RevisionIndex::branchTip(st.metadata[2], kA), kR2);
    EXPECT_EQ(revindex::RevisionIndex::branchTip(st.metadata[2], kMain), kR0);
    const std::optional<RevisionManifest> r2 = st.index.tryRevision(kR2);
    ASSERT_TRUE(r2.has_value());
    ASSERT_TRUE(r2->parentRevisionId.has_value());
    EXPECT_EQ(*r2->parentRevisionId, kR0);  // 分支 tip 维度的父修订（§4.5 表）

    // ---- 步骤 3 断言：M3{main→r0；A→r2；B(base r0, tip r0)}，HEAD=r3 ----
    EXPECT_EQ(revindex::RevisionIndex::branchTip(st.metadata[3], kMain), kR0);
    EXPECT_EQ(revindex::RevisionIndex::branchTip(st.metadata[3], kA), kR2);
    const std::optional<RevisionId> bTipAfterCreate = revindex::RevisionIndex::branchTip(st.metadata[3], kB);
    ASSERT_TRUE(bTipAfterCreate.has_value());
    EXPECT_EQ(*bTipAfterCreate, kR0);
    // B 自 main 创建（base=r0＝main.tip），A 的 tip 不受 CreateBranch 影响。
    for (const BranchRecord& b : st.metadata[3].branches) {
        if (b.branchId == kB) {
            EXPECT_EQ(b.baseRevisionId, kR0);
        }
        if (b.branchId == kA) {
            EXPECT_EQ(b.tipRevisionId, kR2);
        }
    }

    // ---- 步骤 4 断言：切换 B＝纯会话，零写入 ----
    // 不调用任何索引写 API；权威仍是 M3 的内容（三支完整）、也没有任何
    // 新版本因"切换"产生（历史恰好五个元数据版本 M0..M4，metaRef(5) 不
    // 存在——切换不产生修订/新元数据，§4.5"切换分支是否改变持久化状态：
    // 否"）。
    EXPECT_EQ(st.index.tryMetadata(st.refs[3])->branches.size(), 3u);
    EXPECT_FALSE(st.index.tryMetadata(metaRef(5)).has_value());

    // ---- 步骤 5 断言：M4 以权威 M3 为底本——A 的 tip r2 被完整携带 ----
    // （INV-M3 核心断言，§4.5.1 结论①：不因"r4 的父修订是 r0、r0 引用的
    // M0 无 A/B 分支"而丢失或回退分支表。）
    const ProjectMetadataRecord& m4 = st.metadata[4];
    ASSERT_EQ(m4.branches.size(), 3u);
    EXPECT_EQ(revindex::RevisionIndex::branchTip(m4, kMain), kR0);
    EXPECT_EQ(revindex::RevisionIndex::branchTip(m4, kA), kR2);   // 不丢失、不回退
    EXPECT_EQ(revindex::RevisionIndex::branchTip(m4, kB), kR4);   // 活动分支推进
    EXPECT_EQ(*st.index.tryRevision(kR4)->parentRevisionId, kR0); // r4.parent＝r0〔B.tip〕

    // ---- 步骤 6 断言：返回 A＝纯会话，权威仍是 M4 ----
    // （无索引动作；HEAD 引用的权威元数据版本不变——切换零写入。）

    // ---- 步骤 7 断言：查询 A 的最新＝自 HEAD 引用的 M4 读取 → r2 ----
    // （§4.5.1 结论②：一律取自权威版本，禁止经 r2 引用的 M2 重建分支表
    // ——M2 无 B，若用它即"读取旧修订回退整个项目分支表"。）
    const std::optional<RevisionId> aTip = revindex::RevisionIndex::branchTip(m4, kA);
    ASSERT_TRUE(aTip.has_value());
    EXPECT_EQ(*aTip, kR2);
    // 反证形态展示：M2（r2 自身引用的元数据）里查不到 B——这正是"用旧
    // 修订元数据读分支表"会发生分支丢失的原因（结论②禁止路径的形态）。
    EXPECT_FALSE(revindex::RevisionIndex::branchTip(st.metadata[2], kB).has_value());
}

/**
 * 用例：PM-12——分支记录 baseRevisionId 不复制对象（acceptance 1 后半）。
 * 前置：完整走查历史。操作：对照全部修订的引用集与 introducedObjects。
 * 预期：分支创建（r1/r3）只新增其元数据对象；共享域对象 X 被全部修订
 * 引用共享（引用复制≠对象复制——对象文件零新增）；引用宇宙＝X＋M0..M4。
 */
TEST(Walkthrough, BranchBaseRevisionReferencesWithoutCopyingObjects_Acc1)
{
    WalkthroughState st;
    buildWalkthrough(st);

    // 全部修订的引用宇宙＝共享域对象 X＋五个元数据对象（六个编址键）。
    std::set<std::string> universe;
    for (const RevisionId& id : {kR0, kR1, kR2, kR3, kR4}) {
        const RevisionManifest manifest = *st.index.tryRevision(id);
        for (const sdurws::ird::project::ObjectRef& ref : manifest.objectRefs) {
            universe.insert(ref.objectId.toCanonical() + "/" + ref.contentVersion.toCanonical());
        }
    }
    EXPECT_EQ(universe.size(), 6u);  // X＋M0～M4——分支创建没有复制任何对象

    // 每个修订都引用共享域对象 X（对象被多修订共享，不复制——§4.3 图）。
    for (const RevisionId& id : {kR0, kR1, kR2, kR3, kR4}) {
        const RevisionManifest manifest = *st.index.tryRevision(id);
        bool referencesContent = false;
        for (const sdurws::ird::project::ObjectRef& ref : manifest.objectRefs) {
            if (ref.objectId == kContentOid && ref.contentVersion == kContentCv) {
                referencesContent = true;
            }
        }
        EXPECT_TRUE(referencesContent) << "修订 " << id.toCanonical() << " 未共享引用域对象";
    }

    // 分支创建修订（r1/r3）的 introducedObjects 只含各自的元数据对象——
    // 分支记录（baseRevisionId 等）只是元数据表条目，不携带对象副本。
    const RevisionManifest r1 = *st.index.tryRevision(kR1);
    ASSERT_EQ(r1.introducedObjects.size(), 1u);
    EXPECT_EQ(r1.introducedObjects[0], st.refs[1].contentVersion);
    const RevisionManifest r3 = *st.index.tryRevision(kR3);
    ASSERT_EQ(r3.introducedObjects.size(), 1u);
    EXPECT_EQ(r3.introducedObjects[0], st.refs[3].contentVersion);
}

// =====================================================================
// Closure 组——acceptance 2 前半（PRJ-TX-12：提交闭包双通道 D-4，孤岛
// 判定不出现；§4.5.1 结论③④）。需求/AT：PM-12、自审 A-6。
// =====================================================================

/**
 * 用例：PRJ-TX-12——闭包双通道覆盖全部已提交修订（acceptance 2）。
 * 前置：完整走查历史（HEAD=r4）。操作：closure(r4)。
 * 预期：{r0..r4} 全部可达（无孤岛）；其中 r1/r2/r3 从 r4 沿 parent 链
 * **不可达**（它们的 parent 都是 r0）——只经元数据谱系通道（M4→M3→r3、
 * M3→M2→r2、M2→M1→r1）到达，D-4 双通道缺一不可的正面证明。
 */
TEST(Closure, DualChannelCoversAllCommittedRevisions_NoIsland_Acc2)
{
    WalkthroughState st;
    buildWalkthrough(st);

    const CommittedClosure closure = st.index.committedClosure(kR4);
    const std::set<std::string> reached = closureRevTexts(closure);

    // 全部五修订在闭包内——孤岛判定不出现（§4.5.1 结论③）。
    EXPECT_EQ(reached.size(), 5u);
    EXPECT_TRUE(reached.count(kR0.toCanonical()) == 1);
    EXPECT_TRUE(reached.count(kR1.toCanonical()) == 1);  // CreateBranch 修订——谱系通道可达
    EXPECT_TRUE(reached.count(kR2.toCanonical()) == 1);  // A 上的历史提交——同上
    EXPECT_TRUE(reached.count(kR3.toCanonical()) == 1);  // CreateBranch B——同上
    EXPECT_TRUE(reached.count(kR4.toCanonical()) == 1);

    // 单通道（parent 链）从 r4 只能到达 {r4, r0}——补集恰为"仅谱系可达"
    // 的 r1/r2/r3，双通道必要性的直接数值证据。
    std::size_t parentChainOnly = 0;
    std::optional<RevisionId> cursor = kR4;
    while (cursor.has_value()) {
        ++parentChainOnly;
        cursor = st.index.tryRevision(*cursor)->parentRevisionId;
    }
    EXPECT_EQ(parentChainOnly, 2u);

    // 谱系通道覆盖的元数据版本＝M0..M4 全部（确定性排序下 5 个引用键）。
    EXPECT_EQ(closure.metadata.size(), 5u);
}

/**
 * 用例：§4.5.1 结论④——发布后、切 HEAD 前崩溃的残留判定（acceptance 2
 * 的残留半边）。前置：历史提交到 r3（HEAD=r3），磁盘上已有 r4 残留修订
 * （已发布未提交——注册进索引模拟磁盘扫描装载）。操作：closure(r3)。
 * 预期：闭包={r0..r3}，r4 在闭包外（→调用方按未提交残留忽略＋恢复诊断，
 * 不把 r4 当作已提交历史——自审项 A-6；M3 权威性不受影响）。
 */
TEST(Closure, ResidueOutsideClosure_AfterPublishBeforeHeadSwitch_Acc2)
{
    WalkthroughState st;
    buildWalkthrough(st);
    // 回退到"崩溃时刻"的视角：把 HEAD 视为 r3（r4 已注册＝磁盘有目录，
    // 但 HEAD 未切换——本索引不持 HEAD，HEAD 归调用方 HeadRecord）。
    // r4_res 语义＝同一磁盘状态，从 HEAD=r3 计算闭包。
    const RevisionId r4Residue = kR4;

    const CommittedClosure closure = st.index.committedClosure(kR3);
    const std::set<std::string> reached = closureRevTexts(closure);

    EXPECT_EQ(reached.size(), 4u);  // r0..r3
    EXPECT_TRUE(reached.count(r4Residue.toCanonical()) == 0) << "发布未提交的 r4 不得进入闭包";
    // 谱系通道也不把 r4 带进来：M4 在"崩溃时刻"尚未成为权威（HEAD 仍指
    // M3），闭包自 r3 起走 M3→M2→M1→M0，M4 不在链上。
    EXPECT_EQ(closure.metadata.size(), 4u);
}

/**
 * 用例：闭包行走对悬挂引用的数据侧拒绝（StoreCorrupt——调用方声称已装
 * 载全部磁盘内容，缺失即损坏；静默截断会把 committed 历史误报为残留）。
 */
TEST(Closure, DanglingReferencesRejectedAsCorrupt_Acc2)
{
    // (a) parent 悬挂：清单引用未注册父修订。
    {
        RevisionIndex index;
        index.registerMetadata(metaRef(0),
                               metadataRecord(makeRev(9), {branchRecord(kMain, "main", makeRev(9), makeRev(9))}));
        RevisionManifest orphan = manifestRecord(kR0, 1, makeRev(9), kMain, metaRef(0));
        index.registerRevision(orphan);
        expectCorrupt([&] { index.committedClosure(kR0); }, "parent 悬挂");
    }
    // (b) metadataRef 悬挂：清单指向未注册元数据。
    {
        RevisionIndex index;
        index.registerRevision(manifestRecord(kR0, 1, std::nullopt, kMain, metaRef(0)));
        expectCorrupt([&] { index.committedClosure(kR0); }, "metadataRef 悬挂");
    }
    // (c) supersedes 悬挂：谱系链指向未注册元数据版本。
    {
        RevisionIndex index;
        ProjectMetadataRecord m0 = metadataRecord(kR0, {branchRecord(kMain, "main", kR0, kR0)});
        m0.supersedes = metaRef(7);  // 未注册的前驱
        index.registerMetadata(metaRef(0), m0);
        index.registerRevision(manifestRecord(kR0, 1, std::nullopt, kMain, metaRef(0)));
        expectCorrupt([&] { index.committedClosure(kR0); }, "supersedes 悬挂");
    }
    // (d) committedBy 悬挂：元数据的提交修订未注册。
    {
        RevisionIndex index;
        index.registerMetadata(metaRef(0), metadataRecord(makeRev(9),
                                                          {branchRecord(kMain, "main", kR0, kR0)}));
        index.registerRevision(manifestRecord(kR0, 1, std::nullopt, kMain, metaRef(0)));
        expectCorrupt([&] { index.committedClosure(kR0); }, "committedBy 悬挂");
    }
    // (e) 起点未注册 / 起点无效（调用方错误 fail-fast）。
    {
        RevisionIndex index;
        expectCorrupt([&] { index.committedClosure(kR0); }, "起点未注册");
        expectInvalidArgument([&] { index.committedClosure(RevisionId{}); }, "起点全零");
    }
}

/**
 * 用例：闭包结果确定性（NFR-COR-02）——同输入两次计算逐元素相等；
 * revisions 按 (seq, id) 升序、metadata 按规范文本升序。
 */
TEST(Closure, DeterministicOrdering_Acc2)
{
    WalkthroughState st;
    buildWalkthrough(st);

    const CommittedClosure a = st.index.committedClosure(kR4);
    const CommittedClosure b = st.index.committedClosure(kR4);
    ASSERT_EQ(a.revisions.size(), b.revisions.size());
    for (std::size_t i = 0; i < a.revisions.size(); ++i) {
        EXPECT_EQ(a.revisions[i], b.revisions[i]);
    }
    ASSERT_EQ(a.metadata.size(), b.metadata.size());
    for (std::size_t i = 0; i < a.metadata.size(); ++i) {
        EXPECT_TRUE(a.metadata[i] == b.metadata[i]);
    }
    // seq 升序（r0..r4 的分配序号 1..5）。
    for (std::size_t i = 1; i < a.revisions.size(); ++i) {
        const std::uint64_t prev = st.index.tryRevision(a.revisions[i - 1])->revisionSeq;
        const std::uint64_t cur = st.index.tryRevision(a.revisions[i])->revisionSeq;
        EXPECT_LT(prev, cur);
    }
}

// =====================================================================
// PublishValidation 组——acceptance 2 后半（PRJ-TX-12：谱系成环/分支表
// 回退注入在发布边界拒绝，branch-metadata-regression）＋INV-M1。
// 需求/AT：PM-12、自审 A-6/A-7。
// =====================================================================

/**
 * 用例：INV-M3 超集约束——以父修订元数据（M0）重建的候选被拒绝。
 * 这是 D-5"绝不经父修订元数据重建"的机械证明（acceptance 3 反证半边）：
 * 若步骤 5 的提交误以 r4 的父修订 r0 所引用的 M0 为底本，候选丢失 A/B
 * 分支（§4.5.1 结论①反例），发布边界必须拒绝。
 */
TEST(PublishValidation, ParentMetadataRebuildRejected_Acc2)
{
    WalkthroughState st;
    buildWalkthrough(st);

    // 病态候选：分支表内容按"以 M0 为底本重建"的形态丢失 A/B（§4.5.1
    // 结论①反例），但 supersedes 仍按 D-5 收口到权威 M3——把拒绝点隔离在
    // INV-M3 超集约束上（谱系锚点错误是另一条独立检查，见
    // CommittedByAndLineageAnchorRejected 用例）。
    ProjectMetadataRecord stale = st.metadata[0];  // {main→r0}——无 A/B
    stale.committedBy = kR4;
    stale.supersedes = st.refs[3];
    const ObjectRefPair staleRef = metaRef(90);

    expectRegression([&] {
        st.index.validateMetadataPublish(st.metadata[3], st.refs[3], stale, staleRef, kR4);
    }, "以父修订元数据重建的候选");
}

/**
 * 用例：INV-M3 不回退——既有分支 tip 被拨回旧修订的候选被拒绝。
 * （候选把 A 的 tip 从 r2 拨回 r0——即使该候选其余部分健康。）
 */
TEST(PublishValidation, TipRegressionRejected_Acc2)
{
    WalkthroughState st;
    buildWalkthrough(st);

    ProjectMetadataRecord regressed = st.metadata[3];
    for (BranchRecord& b : regressed.branches) {
        if (b.branchId == kA) {
            b.tipRevisionId = kR0;  // A：r2 → r0（回退）
        }
    }
    regressed.committedBy = kR4;
    regressed.supersedes = st.refs[3];
    const ObjectRefPair regressedRef = metaRef(91);

    expectRegression([&] {
        st.index.validateMetadataPublish(st.metadata[3], st.refs[3], regressed, regressedRef, kR4);
    }, "A 的 tip 回退到 r0");
}

/**
 * 用例：INV-M3——非新修订所属分支的 tip 被变更的候选被拒绝
 * （tip 变更只允许发生在新修订所属分支上，且推进到新修订）。
 */
TEST(PublishValidation, ForeignBranchTipChangeRejected_Acc2)
{
    WalkthroughState st;
    buildWalkthrough(st);

    ProjectMetadataRecord foreign = st.metadata[3];
    for (BranchRecord& b : foreign.branches) {
        if (b.branchId == kMain) {
            b.tipRevisionId = kR4;  // main 的 tip 被拨到 r4（r4 属于 B，非 main）
        }
    }
    foreign.committedBy = kR4;
    foreign.supersedes = st.refs[3];
    const ObjectRefPair foreignRef = metaRef(92);

    expectRegression([&] {
        st.index.validateMetadataPublish(st.metadata[3], st.refs[3], foreign, foreignRef, kR4);
    }, "main 的 tip 被非法变更为 r4");
}

/**
 * 用例：P-PR-8——既有分支 label 被改名的候选在发布边界拒绝
 * （acceptance 4：label 创建时一次写入、不可改名；PM 明确不做分支改名）。
 */
TEST(PublishValidation, LabelRenameRejected_Acc2_PPR8)
{
    WalkthroughState st;
    buildWalkthrough(st);

    ProjectMetadataRecord renamed = st.metadata[3];
    for (BranchRecord& b : renamed.branches) {
        if (b.branchId == kA) {
            b.label = "A-renamed";  // 改名——P-PR-8 禁止
        }
    }
    renamed.committedBy = kR4;
    renamed.supersedes = st.refs[3];
    const ObjectRefPair renamedRef = metaRef(93);

    expectRegression([&] {
        st.index.validateMetadataPublish(st.metadata[3], st.refs[3], renamed, renamedRef, kR4);
    }, "分支 label 改名注入");
}

/**
 * 用例：INV-M3 不可变条目——baseRevisionId 被改写的候选被拒绝
 * （§4.5"三种修订指针"表：base 创建时记录后不变）。
 */
TEST(PublishValidation, BaseRewriteRejected_Acc2)
{
    WalkthroughState st;
    buildWalkthrough(st);

    ProjectMetadataRecord rebase = st.metadata[3];
    for (BranchRecord& b : rebase.branches) {
        if (b.branchId == kA) {
            b.baseRevisionId = kR2;  // base 被改写
        }
    }
    rebase.committedBy = kR4;
    rebase.supersedes = st.refs[3];
    const ObjectRefPair rebaseRef = metaRef(94);

    expectRegression([&] {
        st.index.validateMetadataPublish(st.metadata[3], st.refs[3], rebase, rebaseRef, kR4);
    }, "分支 base 改写注入");
}

/**
 * 用例：INV-M1——branchId 重复 / tip 指向不存在修订的候选被拒绝
 * （§4.4.3 发布期校验）。
 */
TEST(PublishValidation, InvM1_DuplicateBranchAndUnknownTipRejected_Acc2)
{
    WalkthroughState st;
    buildWalkthrough(st);

    // (a) branchId 重复（两个 A）。
    {
        ProjectMetadataRecord dup = st.metadata[3];
        dup.branches.push_back(branchRecord(kA, "A2", kR0, kR2));
        dup.committedBy = kR4;
        dup.supersedes = st.refs[3];
        expectRegression([&] {
            st.index.validateMetadataPublish(st.metadata[3], st.refs[3], dup, metaRef(95), kR4);
        }, "branchId 重复");
    }
    // (b) tip 指向未注册修订（INV-M1b"必须指向已存在修订"）。
    {
        ProjectMetadataRecord ghost = st.metadata[3];
        for (BranchRecord& b : ghost.branches) {
            if (b.branchId == kB) {
                b.tipRevisionId = makeRev(999);  // 未注册
            }
        }
        ghost.committedBy = kR4;
        ghost.supersedes = st.refs[3];
        expectRegression([&] {
            st.index.validateMetadataPublish(st.metadata[3], st.refs[3], ghost, metaRef(96), kR4);
        }, "tip 指向不存在修订");
    }
    // (c) 新增分支的 base 指向未注册修订（分支自既有修订分叉——悬挂 base
    // 同样是"指向不存在修订"）。
    {
        ProjectMetadataRecord danglingBase = st.metadata[3];
        danglingBase.branches.push_back(branchRecord(makeBrn(9), "C", makeRev(998), makeRev(998)));
        danglingBase.committedBy = kR4;
        danglingBase.supersedes = st.refs[3];
        expectRegression([&] {
            st.index.validateMetadataPublish(st.metadata[3], st.refs[3], danglingBase, metaRef(99), kR4);
        }, "新增分支 base 悬挂");
    }
}

/**
 * 用例：committedBy 绑定＋D-5 链＋无新版本——候选元数据必须由本次新修订
 * 提交、supersedes 必须恰为权威版本、同编址候选＝无新版本。
 */
TEST(PublishValidation, CommittedByAndLineageAnchorRejected_Acc2)
{
    WalkthroughState st;
    buildWalkthrough(st);

    // (a) committedBy 指向旧修订（§4.4.3：提交本元数据版本的修订）。
    {
        ProjectMetadataRecord staleCommit = st.metadata[3];
        staleCommit.committedBy = kR3;  // 非 r4
        staleCommit.supersedes = st.refs[3];
        expectRegression([&] {
            st.index.validateMetadataPublish(st.metadata[3], st.refs[3], staleCommit, metaRef(97), kR4);
        }, "committedBy 非新修订");
    }
    // (b) supersedes 指向非权威版本（谱系断裂——D-5 链约束）。
    {
        ProjectMetadataRecord wrongParent = st.metadata[3];
        wrongParent.committedBy = kR4;
        wrongParent.supersedes = st.refs[1];  // 应为 refs[3]（M3）
        expectRegression([&] {
            st.index.validateMetadataPublish(st.metadata[3], st.refs[3], wrongParent, metaRef(98), kR4);
        }, "supersedes 未指向权威版本");
    }
    // (c) 候选与权威同编址（同内容＝没有新版本可发布）。
    {
        expectRegression([&] {
            st.index.validateMetadataPublish(st.metadata[3], st.refs[3], st.metadata[3], st.refs[3], kR4);
        }, "候选与权威同编址");
    }
}

/**
 * 用例：INV-M2 谱系成环注入——间接环（已注册记录回指候选键）在发布边界
 * 拒绝（acceptance 2"谱系成环注入在发布边界拒绝"）。
 */
TEST(PublishValidation, LineageCycleInjectionRejected_Acc2)
{
    // 注入构造：注册 M_a，其 supersedes 指向"候选的引用键"——磁盘损坏可
    // 产生此形态（M_a 的前驱指向一个尚未落位的版本）。候选以 M_a 为权威
    // 发布时，谱系行走 candidate→M_a→candidate 成环。
    RevisionIndex index;
    const ObjectRefPair candidateRef = metaRef(50);
    const ObjectRefPair aRef = metaRef(51);

    ProjectMetadataRecord mA = metadataRecord(kR3, {branchRecord(kMain, "main", kR0, kR3)});
    mA.supersedes = candidateRef;  // 回指候选——环
    index.registerMetadata(aRef, mA);
    // 谱系行走要求 committedBy 修订已注册：kR3（mA 的提交修订）与 kR4
    // （新修订，先注册——发布校验的时序契约）都须在索引中。
    index.registerRevision(manifestRecord(kR3, 3, kR0, kMain, aRef));
    index.registerRevision(manifestRecord(kR4, 6, kR3, kMain, candidateRef));

    ProjectMetadataRecord candidate = metadataRecord(kR4, {branchRecord(kMain, "main", kR0, kR4)});
    candidate.supersedes = aRef;

    expectRegression([&] {
        index.validateMetadataPublish(mA, aRef, candidate, candidateRef, kR4);
    }, "谱系间接环注入");
}

/**
 * 用例：INV-M2 版本单调注入——谱系链上"越旧的元数据由越旧的修订提交"
 * 被破坏的候选在发布边界拒绝（§4.4.3"supersedes 链全局无环、版本单调"）。
 */
TEST(PublishValidation, LineageMonotonicityInjectionRejected_Acc2)
{
    RevisionIndex index;
    // M_outer：committedBy r1（seq 1）——谱系更深处；M_inner：committedBy
    // r3（seq 3）——权威。行走 candidate(r4,seq4) → M_inner(r3,seq3) →
    // M_outer(r1,seq1)：本用例把顺序倒置——让权威的 supersedes 指向一个
    // committedBy **更新**的记录，行走应在中途拒绝。
    const ObjectRefPair outerRef = metaRef(60);
    const ObjectRefPair innerRef = metaRef(61);

    ProjectMetadataRecord mInner = metadataRecord(kR3, {branchRecord(kMain, "main", kR0, kR3)});
    mInner.supersedes = outerRef;
    ProjectMetadataRecord mOuter = metadataRecord(kR3, {branchRecord(kMain, "main", kR0, kR3)});
    // M_outer 的 committedBy 与 M_inner 同修订（seq 不严格递减）——单调违约。
    index.registerMetadata(outerRef, mOuter);
    index.registerMetadata(innerRef, mInner);
    index.registerRevision(manifestRecord(kR3, 3, kR0, kMain, innerRef));
    index.registerRevision(manifestRecord(kR4, 4, kR3, kMain, innerRef));  // 新修订先注册

    ProjectMetadataRecord candidate = metadataRecord(kR4, {branchRecord(kMain, "main", kR0, kR4)});
    candidate.supersedes = innerRef;

    expectRegression([&] {
        index.validateMetadataPublish(mInner, innerRef, candidate, metaRef(62), kR4);
    }, "谱系版本单调注入（两版本同 committedBy）");
}

/**
 * 用例：时序契约（调用方错误 fail-fast）——新修订未注册、权威未注册、
 * 权威注册内容与传入不一致、候选键已注册，均 std::invalid_argument。
 */
TEST(PublishValidation, CallerContractViolationsFailFast)
{
    WalkthroughState st;
    buildWalkthrough(st);

    ProjectMetadataRecord candidate = metadataRecord(kR4, st.metadata[3].branches);
    candidate.committedBy = kR4;
    candidate.supersedes = st.refs[3];

    // (a) 新修订未注册（走查历史里 r4 已注册；用一个未注册修订触发）。
    const RevisionId unregistered = makeRev(500);
    expectInvalidArgument([&] {
        st.index.validateMetadataPublish(st.metadata[3], st.refs[3], candidate, metaRef(70), unregistered);
    }, "新修订未注册");
    // (b) 权威引用键未注册。
    expectInvalidArgument([&] {
        st.index.validateMetadataPublish(st.metadata[3], metaRef(71), candidate, metaRef(70), kR4);
    }, "权威未注册");
    // (c) 权威注册内容与传入记录不一致。
    ProjectMetadataRecord distorted = st.metadata[3];
    distorted.projectDisplayName = "Distorted";
    expectInvalidArgument([&] {
        st.index.validateMetadataPublish(distorted, st.refs[3], candidate, metaRef(70), kR4);
    }, "权威内容失真");
    // (d) 候选键已注册（同内容重复发布）。
    expectInvalidArgument([&] {
        st.index.validateMetadataPublish(st.metadata[3], st.refs[3], st.metadata[4], st.refs[4], kR4);
    }, "候选键已注册");
}

// =====================================================================
// SeqAllocation 组——acceptance 3 前半（O-15：revisionSeq 单调分配）。
// 需求：ARC-01；设计：§6.1（S6 内分配＝权威 HEAD seq+1）、D-06（断点恢复）。
// =====================================================================

/**
 * 用例：分配严格单调递增（acceptance 3）；水位只升不降；HEAD 断点恢复
 * （D-06）后从 HEAD seq+1 继续。
 */
TEST(SeqAllocation, MonotonicAllocationAndHeadAdoption_Acc3)
{
    RevisionIndex index;

    // 空索引水位 0；分配从 1 起严格递增（§6.1：权威 HEAD 的 seq+1）。
    EXPECT_EQ(index.lastRevisionSeq(), 0u);
    EXPECT_EQ(index.allocateRevisionSeq(), 1u);
    EXPECT_EQ(index.allocateRevisionSeq(), 2u);
    EXPECT_EQ(index.allocateRevisionSeq(), 3u);
    EXPECT_EQ(index.lastRevisionSeq(), 3u);

    // 断点恢复：重开场景以 HEAD.revisionSeq 恢复水位（D-06），续分配接续。
    RevisionIndex reopened;
    reopened.adoptHeadSeq(41);
    EXPECT_EQ(reopened.lastRevisionSeq(), 41u);
    EXPECT_EQ(reopened.allocateRevisionSeq(), 42u);  // HEAD seq+1
    // 只升不降：更小的 headSeq 不得拉回水位（单调是 O-15 的结构前提）。
    reopened.adoptHeadSeq(7);
    EXPECT_EQ(reopened.lastRevisionSeq(), 42u);
    EXPECT_EQ(reopened.allocateRevisionSeq(), 43u);
}

// =====================================================================
// Derive 组——acceptance 3 后半（D-5：新元数据一律以 HEAD 引用权威版本
// 为底本拷贝更新——PA-2 不可变历史的机制化）。需求：PA-2、PM-12。
// =====================================================================

/**
 * 用例：底本拷贝的逐字保留（acceptance 3）——除显式声明外一切字段与
 * 权威版本全等：既有分支的 label/base/createdAt 原样（P-PR-8）、显示名/
 * 主分支/schemeLabels 原样；committedBy/supersedes/目标 tip 按声明更新。
 */
TEST(Derive, CopiesAuthoritativeVerbatim_UpdatesOnlyDeclared_Acc3)
{
    ProjectMetadataRecord m0 = metadataRecord(kR0, {branchRecord(kMain, "main", kR0, kR0)});
    m0.schemeLabels["stage"] = "draft";
    const ObjectRefPair m0Ref = metaRef(0);

    const ProjectMetadataRecord m1 = RevisionIndex::deriveNextMetadata(
        m0, m0Ref, MetadataDelta{std::nullopt, {branchRecord(kA, "A", kR0, kR0)}}, kR1);

    // 更新面：committedBy／supersedes／新增条目。
    EXPECT_EQ(m1.committedBy, kR1);
    ASSERT_TRUE(m1.supersedes.has_value());
    EXPECT_TRUE(*m1.supersedes == m0Ref);  // 谱系收口到权威版本（D-5）
    ASSERT_EQ(m1.branches.size(), 2u);
    EXPECT_EQ(m1.branches[1].branchId, kA);

    // 保留面：逐字拷贝（P-PR-8 label／base／createdAt；显示名；主分支；
    // schemeLabels）。
    EXPECT_EQ(m1.branches[0], m0.branches[0]);
    EXPECT_EQ(m1.projectDisplayName, m0.projectDisplayName);
    EXPECT_EQ(m1.primaryBranchId, m0.primaryBranchId);
    EXPECT_EQ(m1.schemaVersion, m0.schemaVersion);
    EXPECT_EQ(m1.schemeLabels, m0.schemeLabels);
}

/**
 * 用例：tip 更新只落在声明分支（§4.5"仅更新活动分支的 tipRevisionId"）；
 * 其余分支 tip 原样携带（INV-M3 的 derive 侧机制）。
 */
TEST(Derive, TipUpdateAppliesToDeclaredBranchOnly_Acc3)
{
    const std::vector<BranchRecord> baseBranches = {
        branchRecord(kMain, "main", kR0, kR0),
        branchRecord(kA, "A", kR0, kR2),
    };
    ProjectMetadataRecord m2 = metadataRecord(kR2, baseBranches);
    const ObjectRefPair m2Ref = metaRef(2);

    const ProjectMetadataRecord m3 = RevisionIndex::deriveNextMetadata(
        m2, m2Ref, MetadataDelta{std::nullopt, {branchRecord(kB, "B", kR0, kR0)}}, kR3);

    // B 新增（tip=base=r0）；main/A 逐字携带——A 的 r2 不丢（§4.5.1 步骤 3）。
    EXPECT_EQ(m3.branches[1], baseBranches[1]);
    EXPECT_EQ(RevisionIndex::branchTip(m3, kB), kR0);
    EXPECT_EQ(RevisionIndex::branchTip(m3, kA), kR2);

    // 步骤 5 形态：声明 tip 更新（B→r4）时 A 的 r2 仍被携带（INV-M3）。
    const ProjectMetadataRecord m4 = RevisionIndex::deriveNextMetadata(
        m3, m2Ref, MetadataDelta{TipUpdate{kB, kR4}, {}}, kR4);
    EXPECT_EQ(RevisionIndex::branchTip(m4, kB), kR4);
    EXPECT_EQ(RevisionIndex::branchTip(m4, kA), kR2);
    EXPECT_EQ(RevisionIndex::branchTip(m4, kMain), kR0);
}

/**
 * 用例：构造违约 fail-fast（调用方错误）——tip 更新指向不存在的分支、
 * 新增分支与既有/本批次重复、无效身份，一律 std::invalid_argument。
 */
TEST(Derive, ConstructorViolationsFailFast_Acc3)
{
    ProjectMetadataRecord m0 = metadataRecord(kR0, {branchRecord(kMain, "main", kR0, kR0)});
    const ObjectRefPair m0Ref = metaRef(0);

    // (a) tip 更新指向不存在的分支（S1 已校验分支存在——此处属构造违约）。
    expectInvalidArgument([&] {
        RevisionIndex::deriveNextMetadata(m0, m0Ref, MetadataDelta{TipUpdate{kA, kR1}, {}}, kR1);
    }, "tipUpdate 分支不存在");
    // (b) 新增分支与既有分支表重复（CreateBranch 到已存在分支）。
    expectInvalidArgument([&] {
        RevisionIndex::deriveNextMetadata(
            m0, m0Ref, MetadataDelta{std::nullopt, {branchRecord(kMain, "main2", kR0, kR0)}}, kR1);
    }, "新增分支与既有重复");
    // (c) 本批次内重复。
    expectInvalidArgument([&] {
        RevisionIndex::deriveNextMetadata(
            m0, m0Ref,
            MetadataDelta{std::nullopt,
                          {branchRecord(kA, "A", kR0, kR0), branchRecord(kA, "A-dup", kR0, kR0)}},
            kR1);
    }, "本批次分支重复");
    // (d) committedBy 无效（全零保留值）。
    expectInvalidArgument([&] {
        RevisionIndex::deriveNextMetadata(m0, m0Ref, MetadataDelta{}, RevisionId{});
    }, "committedBy 全零");
    // (e) 新增条目含无效身份。
    expectInvalidArgument([&] {
        RevisionIndex::deriveNextMetadata(
            m0, m0Ref, MetadataDelta{std::nullopt, {branchRecord(BranchId{}, "x", kR0, kR0)}}, kR1);
    }, "新增分支 branchId 全零");
}

// =====================================================================
// Registration 组——装载面防御（索引内容一经注册不可变——PA-2/PA-3）。
// =====================================================================

/**
 * 用例：注册幂等与冲突——同 id 同内容重复注册静默成功；同 id 不同内容
 * ＝存储损坏（StoreCorrupt）；调用方修改自己的副本不影响已注册内容
 * （深拷贝，PA-3）。
 */
TEST(Registration, IdempotentDuplicateConflictingAndImmutability)
{
    RevisionIndex index;
    const RevisionManifest manifest = manifestRecord(kR0, 1, std::nullopt, kMain, metaRef(0));

    // 同 id 同内容：幂等。
    index.registerRevision(manifest);
    index.registerRevision(manifest);
    EXPECT_TRUE(index.tryRevision(kR0).has_value());

    // 同 id 不同内容：数据侧冲突。
    RevisionManifest conflicting = manifest;
    conflicting.branchId = kB;
    expectCorrupt([&] { index.registerRevision(conflicting); }, "修订注册冲突");

    // 调用方副本修改不渗入索引（PA-3 值语义隔离）。
    RevisionManifest mutated = manifest;
    mutated.branchId = kB;
    EXPECT_EQ(index.tryRevision(kR0)->branchId, kMain);

    // 元数据同型三态。
    const ProjectMetadataRecord record = metadataRecord(kR0, {branchRecord(kMain, "main", kR0, kR0)});
    index.registerMetadata(metaRef(0), record);
    index.registerMetadata(metaRef(0), record);  // 幂等
    ProjectMetadataRecord conflictingMeta = record;
    conflictingMeta.projectDisplayName = "Other";
    expectCorrupt([&] { index.registerMetadata(metaRef(0), conflictingMeta); }, "元数据注册冲突");

    // 无效输入 fail-fast。
    expectInvalidArgument([&] { index.registerRevision(RevisionManifest{}); }, "修订清单全零身份");
    RevisionManifest emptyRefs = manifest;
    emptyRefs.objectRefs.clear();
    expectInvalidArgument([&] { index.registerRevision(emptyRefs); }, "objectRefs 为空");
    expectInvalidArgument([&] { index.registerMetadata(ObjectRefPair{}, record); }, "引用键全零");
    expectInvalidArgument([&] {
        index.registerMetadata(metaRef(1), metadataRecord(RevisionId{}, {}));
    }, "committedBy 全零＋分支表空");
}

// =====================================================================
// CoreContract 组——acceptance 4（P-PR-1 处置自证）：身份类型消费以
// core.md v0.1 为基线，索引存取的身份一律 core 规范文本，零本地第二
// 格式化。include 面红线由 BuildRedLineTest/LinkageContractTest 持续扫描
// （零 core 修改的构建图证据在 contract_test 目标）。
// =====================================================================

/**
 * 用例：core 身份契约往返——索引接受 core fromCanonical 解析的身份，
 * 返回时 toCanonical 与原文逐字相等（不存在第二套文本格式）；同语义
 * 身份（同文本解析）在索引中是同一条目。
 */
TEST(CoreContract, IdentityCanonicalRoundTripViaCoreApi_Acc4)
{
    RevisionIndex index;
    // 身份由 core 严格解析产生（P-PR-1 消费面）。
    const std::string revText = paddedHex("rev-", 7, 32);
    const RevisionId id = RevisionId::fromCanonical(revText);
    const ObjectRefPair ref = metaRef(0);

    index.registerMetadata(ref, metadataRecord(id, {branchRecord(kMain, "main", id, id)}));
    index.registerRevision(manifestRecord(id, 1, std::nullopt, kMain, ref));

    // 返回身份的规范文本与输入原文逐字相等——零本地第二格式化。
    const RevisionManifest stored = *index.tryRevision(id);
    EXPECT_EQ(stored.revisionId.toCanonical(), revText);
    // 同文本再解析＝同一索引条目（身份等值＝字节等值，core ARC-04）。
    EXPECT_TRUE(index.tryRevision(RevisionId::fromCanonical(revText)).has_value());
}

/**
 * 用例：branchTip 查询语义——分支存在返回 tip；分支不存在返回 nullopt
 * 不抛（§5.2 查询语义；§4.5.1 步骤 7 的读取形态）。
 */
TEST(CoreContract, BranchTipQuerySemantics)
{
    const ProjectMetadataRecord m0 = metadataRecord(kR0, {branchRecord(kMain, "main", kR0, kR0)});
    EXPECT_EQ(RevisionIndex::branchTip(m0, kMain), kR0);
    EXPECT_FALSE(RevisionIndex::branchTip(m0, kA).has_value());
}

/**
 * 用例：try 轨查询——未注册返回 nullopt 不抛（§4.7 只读查询契约）。
 */
TEST(CoreContract, TryQueriesReturnNulloptWhenAbsent)
{
    RevisionIndex index;
    EXPECT_FALSE(index.tryRevision(kR0).has_value());
    EXPECT_FALSE(index.tryMetadata(metaRef(0)).has_value());
    // 全零身份视为"未设置"→ nullopt（非错误）。
    EXPECT_FALSE(index.tryRevision(RevisionId{}).has_value());
}
