/**
 * @file   SnapshotTest.cpp
 * @brief  快照用例组——EV-ID-1 身份确定性/EV-ID-3 展示无关性、builder 全部
 *         不变量（EV-SNAP：修订闭包防混入/CON-06/工况唯一/外部资源状态机/
 *         采样计划/复现块）、RequiredCaseSet 冻结承载（EV-CASET，O-14 保守
 *         字面）、SnapshotCodec 双形态（EV-SNAPC：同身份/④b 完整性/结构
 *         非法拒绝）。
 *
 * 设计依据：
 *   - units/evidence.md §4.1（AnalysisSnapshot 全节）、§4.1.5（组装协议①～⑤）、
 *     §5.1（身份产生与比较）、§5.2（canonical 编码规则）、§11 反例矩阵
 *     （EV-ID-1/EV-ID-3 行）、§12 EV-T03 行（验证方式）
 *   - 需求 CON-01/03/05/06、NFR-COR-02/03；任务契约 tasks/foundation/
 *     EV-T03.json（≙WP-05-T03）acceptance 1～3：
 *     ①EV-ID-1/3 用例通过；②修订闭包拒绝（混入反例）用例通过；
 *     ③RequiredCaseSet 冻结承载（caseId 唯一/enabled＋mandatory 标记）
 *     用例通过——O-14 未决，保守字面：evidence 只承载标记不私裁 schema
 *     （标记 schema 归 requirements 卡 WP-14-T01）
 *
 * 组名说明：EV-SNAP/EV-CASET/EV-SNAPC 为实现侧组名——§11 矩阵未设 EV-T03
 * 专项组（EV-ERR/EV-DEP 同例），对照 §12 EV-T03 行验证方式"EV-ID-1/3；
 * 修订闭包拒绝（混入反例）用例；全部快照不变量用例通过"。
 *
 * 替身边界声明（EV-REG-3 同源纪律）：本文件的 FakeRevisionClosureSource/
 * FakeObjectBytesSource 为接口替身，仅验证 evidence 契约（闭包校验调用、
 * 字节物化完整性），其返回内容不构成任何 project 侧实现正确性证明。
 *
 * 内容/组装分离说明：EV-ID-1/3 的"同内容"断言要求两次组装消费**同一份**
 * 对象身份（ObjectId 入身份——身份只由内容决定），因此样本内容（闭包/
 * 工况/配置等，含随机生成的 ObjectId）只生成一次，"组装"以其为输入可
 * 重复执行（插入序/序号可变）。
 */

#include <sdurws/ird/evidence/Errors.hpp>
#include <sdurws/ird/evidence/Snapshot.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace {

using namespace sdurws::ird::evidence;
namespace core = sdurws::ird::core;

// =====================================================================
// 测试夹具（纯值辅助——无共享状态，用例间独立）
// =====================================================================

/// 64 个 'a' 的十六进制串（合法身份文本——'a' 在 [0-9a-f] 内）。
const char* kHexA = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
/// 64 个 'b'（与 kHexA 区分的第二身份值）。
const char* kHexB = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
/// 64 个 'c'（第三身份值）。
const char* kHexC = "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc";

/// 合法内容身份（cid- 规范文本解析——core Digest.hpp 工厂）。
core::ContentIdentity cid(const char* hex64)
{
    return core::ContentIdentity::fromCanonical(std::string{"cid-"} + hex64);
}

/// 合法内容版本（cv- 规范文本解析）。
core::ContentVersion cv(const char* hex64)
{
    return core::ContentVersion::fromCanonical(std::string{"cv-"} + hex64);
}

/// 固定项目身份（"prj-"+32 个 '1'——Id128 规范文本）。
core::ProjectId fixedProject()
{
    return core::ProjectId::fromCanonical("prj-11111111111111111111111111111111");
}

/// 固定分支身份。
core::BranchId fixedBranch()
{
    return core::BranchId::fromCanonical("brn-22222222222222222222222222222222");
}

/// 固定修订身份（锚定修订 A）。
core::RevisionId fixedRevision()
{
    return core::RevisionId::fromCanonical("rev-33333333333333333333333333333333");
}

/// 另一修订身份（锚定修订 B——锚定读取反例用）。
core::RevisionId otherRevision()
{
    return core::RevisionId::fromCanonical("rev-44444444444444444444444444444444");
}

/// 对字节向量做 SHA-256（core ContentDigester——构造"摘要与字节自洽"的
/// ContentVersion：④b 校验要求物化字节的重算摘要等于 contentVersion）。
core::Digest256 digestOf(const std::vector<std::uint8_t>& bytes)
{
    core::ContentDigester digester;
    digester.update(bytes.data(), bytes.size());
    return digester.finalize();
}

/// 由字节构造自洽的内容版本（重算摘要——非任意文本）。
core::ContentVersion cvOfBytes(const std::vector<std::uint8_t>& bytes)
{
    core::ContentVersion v;
    v.bytes = digestOf(bytes);
    return v;
}

/// 构造合法对象闭包条目（digest 与 contentVersion 自洽——I-2 一致性闸门）。
ObjectRefEntry validObjectRef(const std::vector<std::uint8_t>& bytes)
{
    ObjectRefEntry e;
    e.objectId = core::ObjectId::generate();
    e.contentVersion = cvOfBytes(bytes);
    e.objectTypeToken = "robot-design";
    e.digest = e.contentVersion.bytes;
    return e;
}

/// 构造合法复现块（必填标量非空；可选字段缺席——最常用形态）。
ReproductionBlock validReproduction()
{
    ReproductionBlock r;
    r.productVersion = "industrialrobot-designer 0.1.0";
    r.evidenceContractVersion = "evidence-contract/1";
    r.codecVersions = {"snapshot-codec/1", "slice-codec/1"};
    return r;
}

/// 构造合法配置引用。
ConfigEntry validConfig(const std::string& token)
{
    ConfigEntry c;
    c.configKindToken = token;
    c.canonicalBytes = {0x01u, 0x02u, 0x03u};
    c.contentIdentity = cid(kHexB);
    return c;
}

/// 构造合法采样计划（样本数默认非零；零样本用例单独覆盖）。
SamplingPlanRef validSamplingPlan()
{
    SamplingPlanRef p;
    p.regionObjectId = core::ObjectId::generate();
    p.planContentIdentity = cid(kHexB);
    p.plannedPositionSamples = 10;   // 计划位置样本数（计数——KIN-04 分母来源）
    p.plannedPoseSamples = 5;        // 计划位姿样本数
    p.sampleSetIdentity = cid(kHexC);
    return p;
}

/**
 * @brief 修订闭包替身：只认锚定修订＋预登记 (oid,cv) 集合；记录查询次数
 *        （供"事实来源确被逐条消费"的断言）。
 */
class FakeRevisionClosureSource : public IRevisionClosureSource {
public:
    FakeRevisionClosureSource(core::RevisionId revision,
                              std::vector<ObjectRefEntry> entries)
        : m_revision(std::move(revision)), m_entries(std::move(entries))
    {
    }

    bool objectInRevision(core::RevisionId revision, core::ObjectId objectId,
                          core::ContentVersion contentVersion) const override
    {
        ++m_queries;
        // 锚定读取（§4.1.5①）：只对锚定修订回答——其他修订一律 false。
        if (!(revision == m_revision)) {
            return false;
        }
        for (const ObjectRefEntry& e : m_entries) {
            if (e.objectId == objectId && e.contentVersion == contentVersion) {
                return true;
            }
        }
        return false;
    }

    /// 事实来源被查询的次数（确定性——每闭包条目恰一次）。
    int queries() const { return m_queries; }

private:
    core::RevisionId m_revision;           ///< 锚定修订
    std::vector<ObjectRefEntry> m_entries; ///< 该修订的对象清单（替身数据）
    mutable int m_queries = 0;             ///< 查询计数（const 接口内递增）
};

/// 对象字节来源替身：按 (oid,cv) 精确匹配返回预置字节（IObjectBytesSource
/// 的最小可观测实现——当前用例经 codec 直接物化，保留以钉住接口可实现性）。
class FakeObjectBytesSource : public IObjectBytesSource {
public:
    void put(core::ObjectId oid, core::ContentVersion v, std::vector<std::uint8_t> bytes)
    {
        m_store[oid.toCanonical() + "|" + v.toCanonical()] = std::move(bytes);
    }

    std::optional<std::vector<std::uint8_t>>
    tryObjectBytes(core::ObjectId oid, core::ContentVersion v) const override
    {
        const auto it = m_store.find(oid.toCanonical() + "|" + v.toCanonical());
        if (it == m_store.end()) {
            return std::nullopt;
        }
        return it->second;
    }

private:
    std::map<std::string, std::vector<std::uint8_t>> m_store;
};

/// 组装器基底：身份三元组＋策略＋名称映射＋复现块（§4.1.2 必填项的最小
/// 合法集——各用例再按需叠加闭包/工况/配置等）。
SnapshotBuilder baseBuilder(std::uint64_t revisionSeq = 5)
{
    SnapshotBuilder b;
    b.setIdentity(fixedProject(), fixedBranch(), fixedRevision(), revisionSeq);
    PolicyRef policy;
    policy.policyContentIdentity = cid(kHexA);
    b.setPolicyRef(policy);
    NameMapRef nameMap;
    nameMap.nameMapContentIdentity = cid(kHexB);
    b.setNameMapRef(nameMap);
    b.setReproduction(validReproduction());
    return b;
}

// =====================================================================
// 样本内容/组装分离（"同内容"断言的前提——内容只生成一次）
// =====================================================================

/// 标准样本内容：3 对象闭包（字节自洽）＋1 配置＋2 工况（其一 enabled∧
/// mandatory）＋1 固化外部资源＋1 采样计划——多数用例的"合法基线"。
struct SampleContent {
    std::vector<std::vector<std::uint8_t>> byteBlobs; ///< 对象字节（与闭包平行）
    std::vector<ObjectRefEntry> closure;              ///< 闭包条目（字节→摘要自洽）
    std::vector<CaseEntry> cases;                     ///< 工况两条（标记组合覆盖）
    ConfigEntry config;                               ///< 分析配置引用
    ExternalResourceState resource;                   ///< 固化外部资源
    SamplingPlanRef plan;                             ///< 采样计划
};

/// 生成一份样本内容（ObjectId 随机一次——此后按同内容重复组装）。
SampleContent makeSampleContent()
{
    SampleContent c;
    c.byteBlobs.push_back({0xA0u, 0x01u, 0x10u});
    c.byteBlobs.push_back({0xB0u, 0x02u, 0x20u, 0x21u});
    c.byteBlobs.push_back({0xC0u, 0x03u});
    for (const auto& blob : c.byteBlobs) {
        c.closure.push_back(validObjectRef(blob));
    }

    CaseEntry c1;
    c1.caseId = core::ObjectId::generate();
    c1.label = "带载可达工况";
    c1.enabled = true;    // 启用＋必验——覆盖矩阵的核心消费组合
    c1.mandatory = true;
    CaseEntry c2;
    c2.caseId = core::ObjectId::generate();
    c2.label = "空载姿态工况";
    c2.enabled = false;   // 未启用——标记组合的对照侧
    c2.mandatory = false;
    c.cases.push_back(c1);
    c.cases.push_back(c2);

    c.config = validConfig("kin.ik-config");

    c.resource.resourceId = core::ObjectId::generate();
    c.resource.state = ExternalResourceStatus::Solidified;
    c.resource.solidifiedContentVersion = cv(kHexC);

    c.plan = validSamplingPlan();
    return c;
}

/// 以给定内容组装（reverseInsertion＝集合类成员逆序录入——插入序敏感面；
/// 冻结序由 builder 规范化，身份与插入序无关）。修订闭包替身按内容闭包登记。
AnalysisSnapshot assembleSample(const SampleContent& content, bool reverseInsertion,
                                std::uint64_t revisionSeq)
{
    SnapshotBuilder b = baseBuilder(revisionSeq);
    if (reverseInsertion) {
        for (auto it = content.closure.rbegin(); it != content.closure.rend(); ++it) {
            b.addObjectRef(*it);
        }
        for (auto it = content.cases.rbegin(); it != content.cases.rend(); ++it) {
            b.addCase(*it);
        }
    } else {
        for (const ObjectRefEntry& e : content.closure) {
            b.addObjectRef(e);
        }
        for (const CaseEntry& c : content.cases) {
            b.addCase(c);
        }
    }
    b.addConfiguration(content.config);
    b.addExternalResource(content.resource);
    b.addSamplingPlan(content.plan);

    FakeRevisionClosureSource source(fixedRevision(), content.closure);
    return b.build(source);
}

/// 抛错断言辅助：fn 必须抛 EvidenceError 且 code==expected；what() 前缀为
/// token（"evidence/..."——Errors.hpp 消息约定），needle 非空时还须包含。
void expectEvidenceThrow(const std::function<void()>& fn, EvidenceErrorCode expected,
                         const char* needle = nullptr)
{
    try {
        fn();
        FAIL() << "预期抛出 EvidenceError，但未抛";
    } catch (const EvidenceError& e) {
        EXPECT_EQ(e.code(), expected);
        EXPECT_NE(std::string(e.what()).find(token(expected)), std::string::npos)
            << "what() 缺少稳定 token 前缀: " << e.what();
        if (needle != nullptr) {
            EXPECT_NE(std::string(e.what()).find(needle), std::string::npos)
                << "what() 缺少定位信息: " << e.what();
        }
    }
}

// =====================================================================
// EV-ID-1 身份确定性（CON-05/NFR-COR-02——acceptance 1）
// =====================================================================

/** 同内容构建两次（插入序不同）→ snapshotId 逐字节相等；编码逐字节相等。 */
TEST(EvidenceSnapshotIdentity, SameContentDeterministicId_EV_ID_1)
{
    // 同一份内容（同一批 ObjectId/工况/配置）——正序与逆序各组装一次。
    const SampleContent content = makeSampleContent();
    const AnalysisSnapshot first = assembleSample(content, false, 5);
    const AnalysisSnapshot second = assembleSample(content, true, 5);

    ASSERT_TRUE(first.snapshotId.isValid()) << "snapshotId 不得为零（合法实例）";
    EXPECT_EQ(first.snapshotId, second.snapshotId)
        << "同内容不同插入序的身份必须相等（builder 冻结序规范化）";
    // 编码层面同样逐字节相等（身份＝编码摘要，编码相等是身份相等的直接依据）。
    EXPECT_EQ(SnapshotCodec::encodeRefsOnly(first), SnapshotCodec::encodeRefsOnly(second));
}

/** SnapshotCodec 往返：parse(encode(x))==x（revisionSeq 不在编码内——见断言）。 */
TEST(EvidenceSnapshotIdentity, CodecRoundTrip_EV_ID_1)
{
    const SampleContent content = makeSampleContent();
    const AnalysisSnapshot original = assembleSample(content, false, 5);

    const std::vector<std::uint8_t> encoding = SnapshotCodec::encodeRefsOnly(original);
    const AnalysisSnapshot parsed = SnapshotCodec::parseRefsOnly(encoding);

    // revisionSeq 不参与内容身份（§4.1.2 原文——I-1）：编码不含该字段，
    // parse 恢复为默认 0；其余全部字段必须逐成员相等。
    ASSERT_EQ(original.revisionSeq, 5u) << "样本前置：原快照序号非零";
    EXPECT_EQ(parsed.revisionSeq, 0u)
        << "revisionSeq 不在编码内（不参与内容身份——§4.1.2）";
    AnalysisSnapshot expected = original;
    expected.revisionSeq = 0;
    EXPECT_EQ(parsed, expected) << "往返等值（除 revisionSeq 外逐成员相等）";

    // 身份往返一致：parse 侧重算的 snapshotId 与原快照相等（身份由编码决定）。
    EXPECT_EQ(parsed.snapshotId, original.snapshotId);
    // 再编码稳定性：parse 产物重编码必须得到同字节（冻结序往返不变）。
    EXPECT_EQ(SnapshotCodec::encodeRefsOnly(parsed), encoding);
}

// =====================================================================
// EV-ID-3 展示无关性（CON-05/AT-05——acceptance 1）
// =====================================================================

/** 改项目显示名/重开项目（新会话同内容）→ snapshotId 不变、无展示噪声入身份。 */
TEST(EvidenceSnapshotIdentity, DisplayAndSessionIndependence_EV_ID_3)
{
    // 场景建模（§11 EV-ID-3 行）：同一内容两次正式评估——第一次在会话 A
    // （项目显示名"设计 V1"），第二次在重开的新会话 B（显示名改为
    // "设计 V1-复评"）。显示名是项目元数据的展示投影，**不是**快照字段、
    // 不经过 builder 任何入口——"改显示名"对组装的唯一可观测影响＝无。
    // revisionSeq 的变化（重开后 project 重新分配的序号投影）同样不得
    // 进入身份（§4.1.2："仅展示/排序，不参与内容身份语义判断"——I-1）。
    const std::string displayNameSessionA = "设计 V1";
    const std::string displayNameSessionB = "设计 V1-复评";
    ASSERT_NE(displayNameSessionA, displayNameSessionB);

    const SampleContent content = makeSampleContent();
    const AnalysisSnapshot sessionA = assembleSample(content, false, 5);
    const AnalysisSnapshot sessionB = assembleSample(content, true, 9);

    EXPECT_EQ(sessionA.snapshotId, sessionB.snapshotId)
        << "显示名/会话/序号差异不得改变快照身份（CON-05：身份只由内容决定）";
    // 反向对照：序号确实不同而身份相同——"不参与"是机械保证而非巧合。
    EXPECT_EQ(sessionA.revisionSeq, 5u);
    EXPECT_EQ(sessionB.revisionSeq, 9u);
    // （displayName 变量未流入任何组装入口——这正是用例的观测点：若未来
    //  有人把展示名/序号编入身份，本用例的 id 相等断言即失败。）
}

// =====================================================================
// EV-SNAP builder 不变量（§4.1.5④a 即时验证——acceptance 2 及非法实例清单）
// =====================================================================

/** 合法构建：冻结凭据/身份非零、闭包规范化有序、值语义拷贝等值、事实来源被逐条消费。 */
TEST(EvidenceSnapshotBuilder, ValidBuildFreezesCanonicalSnapshot_EV_SNAP)
{
    // 逆序插入两个对象（冻结后必须升序——规范化排序，I-3）。
    const std::vector<std::uint8_t> blob1 = {0x11u, 0x22u};
    const std::vector<std::uint8_t> blob2 = {0x33u};
    const ObjectRefEntry e1 = validObjectRef(blob1);
    const ObjectRefEntry e2 = validObjectRef(blob2);

    SnapshotBuilder b = baseBuilder(7);
    b.addObjectRef(e2);
    b.addObjectRef(e1);
    FakeRevisionClosureSource source(fixedRevision(), {e1, e2});

    const AnalysisSnapshot snap = b.build(source);

    ASSERT_TRUE(snap.snapshotId.isValid());
    ASSERT_TRUE(snap.caseSet.requiredCaseSetId.isValid());
    // 冻结序：objectId 字节升序（插入序为 e2, e1——冻结后必须反转）。
    ASSERT_EQ(snap.objectClosure.size(), static_cast<std::size_t>(2));
    EXPECT_TRUE(snap.objectClosure[0].objectId < snap.objectClosure[1].objectId)
        << "闭包必须按 objectId 字典序冻结（NFR-COR-02/I-3）";
    // 值语义：拷贝等值（深拷贝——无共享可变状态）。
    const AnalysisSnapshot copy = snap;
    EXPECT_EQ(copy, snap);
    // 防混入校验确实逐条查询了事实来源（协议①的机械化证据）。
    EXPECT_EQ(source.queries(), 2);
    EXPECT_EQ(snap.revisionSeq, 7u) << "序号透传承载（不参与身份但保留在实例上）";
}

/** 混入反例（acceptance 2）：对象引用不属于锚定修订 → builder 拒绝。 */
TEST(EvidenceSnapshotBuilder, RevisionClosureRejectsForeignObject_EV_SNAP)
{
    const SampleContent content = makeSampleContent();
    SnapshotBuilder b = baseBuilder();
    for (const ObjectRefEntry& e : content.closure) {
        b.addObjectRef(e);   // 前几条属于锚定修订
    }
    // 混入对象：新逻辑对象、内容版本自洽，但 (oid,cv) 不在锚定修订清单内
    // → 必须拒绝（§4.1.5①——混入其他修订数据是 CON-01 的根本破坏）。
    const std::vector<std::uint8_t> foreignBlob = {0xDEu, 0xADu, 0xBEu, 0xEFu};
    const ObjectRefEntry foreign = validObjectRef(foreignBlob);
    b.addObjectRef(foreign);

    FakeRevisionClosureSource source(fixedRevision(), content.closure);
    expectEvidenceThrow(
        [&b, &source] { b.build(source); }, EvidenceErrorCode::SnapshotIncomplete,
        "不属于锚定修订");
    // 事实来源被查询（拒绝发生在对事实来源的校验上，而非字段预检）。
    EXPECT_GE(source.queries(), 1);
}

/** 锚定读取（§4.1.5①）：同一对象在修订 A 合法、组装锚定修订 B 时必须拒绝。 */
TEST(EvidenceSnapshotBuilder, ClosureCheckedAgainstAnchoredRevision_EV_SNAP)
{
    const std::vector<std::uint8_t> blob = {0x50u, 0x60u};
    const ObjectRefEntry e = validObjectRef(blob);
    // 闭包替身锚定修订 A；组装却锚定修订 B——对象在"当前锚"下不属于清单。
    FakeRevisionClosureSource sourceA(fixedRevision(), {e});
    SnapshotBuilder b = baseBuilder();
    b.setIdentity(fixedProject(), fixedBranch(), otherRevision(), 1);
    b.addObjectRef(e);

    expectEvidenceThrow(
        [&b, &sourceA] { b.build(sourceA); }, EvidenceErrorCode::SnapshotIncomplete);
    // 对照：锚定修订 A 时同一条目合法（拒绝确由锚定失配触发）。
    SnapshotBuilder b2 = baseBuilder();
    b2.addObjectRef(e);
    const AnalysisSnapshot snap = b2.build(sourceA);
    EXPECT_TRUE(snap.snapshotId.isValid());
}

/** CON-06：policyRef/nameMapRef 内容身份为空（或未设定）→ builder 拒绝。 */
TEST(EvidenceSnapshotBuilder, Con06RequiresPolicyAndNameMap_EV_SNAP)
{
    const std::vector<std::uint8_t> blob = {0x01u};
    const ObjectRefEntry e = validObjectRef(blob);
    FakeRevisionClosureSource source(fixedRevision(), {e});

    // 反例 1：策略身份为零值。
    {
        SnapshotBuilder b = baseBuilder();
        PolicyRef bad;
        bad.policyContentIdentity = core::ContentIdentity{};   // 全零＝空（CON-06 违反）
        b.setPolicyRef(bad);
        b.addObjectRef(e);
        expectEvidenceThrow([&b, &source] { b.build(source); },
                            EvidenceErrorCode::SnapshotIncomplete, "policyRef");
    }
    // 反例 2：名称映射未设定（组装不完整——独立 builder 只设策略）。
    {
        SnapshotBuilder b;
        b.setIdentity(fixedProject(), fixedBranch(), fixedRevision(), 1);
        PolicyRef policy;
        policy.policyContentIdentity = cid(kHexA);
        b.setPolicyRef(policy);
        b.addObjectRef(e);
        expectEvidenceThrow([&b, &source] { b.build(source); },
                            EvidenceErrorCode::SnapshotIncomplete, "nameMapRef");
    }
}

/** 对象闭包不变量：空闭包/零版本/digest 不一致/重复 objectId 逐类拒绝。 */
TEST(EvidenceSnapshotBuilder, ObjectClosureInvariants_EV_SNAP)
{
    FakeRevisionClosureSource emptySource(fixedRevision(), {});
    // 反例 1：空闭包（§4.1.2"是（≥1）"）。
    {
        SnapshotBuilder b = baseBuilder();
        expectEvidenceThrow([&b, &emptySource] { b.build(emptySource); },
                            EvidenceErrorCode::SnapshotIncomplete, "为空");
    }
    // 反例 2：contentVersion 为零保留值。
    {
        const std::vector<std::uint8_t> blob = {0x02u};
        ObjectRefEntry e = validObjectRef(blob);
        e.contentVersion = core::ContentVersion{};
        FakeRevisionClosureSource source(fixedRevision(), {e});
        SnapshotBuilder b = baseBuilder();
        b.addObjectRef(e);
        expectEvidenceThrow([&b, &source] { b.build(source); },
                            EvidenceErrorCode::SnapshotIncomplete);
    }
    // 反例 3：digest 与 contentVersion 不一致（I-2 一致性闸门——错填摘要）。
    {
        const std::vector<std::uint8_t> blob = {0x03u};
        ObjectRefEntry e = validObjectRef(blob);
        e.digest = cv(kHexC).bytes;   // 与 contentVersion 无关的另一摘要
        FakeRevisionClosureSource source(fixedRevision(), {e});
        SnapshotBuilder b = baseBuilder();
        b.addObjectRef(e);
        expectEvidenceThrow([&b, &source] { b.build(source); },
                            EvidenceErrorCode::SnapshotIncomplete, "digest");
    }
    // 反例 4：同一 objectId 两条引用（I-3 唯一性——歧义闭包）。
    {
        const std::vector<std::uint8_t> blob = {0x04u};
        ObjectRefEntry e = validObjectRef(blob);
        ObjectRefEntry dup = e;   // 同 (oid,cv,digest) 重复录入
        FakeRevisionClosureSource source(fixedRevision(), {e});
        SnapshotBuilder b = baseBuilder();
        b.addObjectRef(e);
        b.addObjectRef(dup);
        expectEvidenceThrow([&b, &source] { b.build(source); },
                            EvidenceErrorCode::SnapshotIncomplete, "重复");
    }
}

/** 身份三元组零值拒绝（revision 是唯一对象解析锚——§4.1.2 行 1）。 */
TEST(EvidenceSnapshotBuilder, IdentityTripleRequired_EV_SNAP)
{
    SnapshotBuilder b;
    b.setIdentity(core::ProjectId{}, fixedBranch(), fixedRevision(), 1);   // 零项目
    expectEvidenceThrow(
        [&b] {
            FakeRevisionClosureSource source(fixedRevision(), {});
            b.build(source);
        },
        EvidenceErrorCode::SnapshotIncomplete, "身份三元组");

    SnapshotBuilder b2;
    b2.setIdentity(fixedProject(), fixedBranch(), core::RevisionId{}, 1);   // 零修订
    expectEvidenceThrow(
        [&b2] {
            FakeRevisionClosureSource source(fixedRevision(), {});
            b2.build(source);
        },
        EvidenceErrorCode::SnapshotIncomplete, "身份三元组");
}

/** 配置引用不变量：空字节/零身份/重复 configKindToken 逐类拒绝。 */
TEST(EvidenceSnapshotBuilder, ConfigEntryInvariants_EV_SNAP)
{
    const std::vector<std::uint8_t> blob = {0x0Au};
    const ObjectRefEntry e = validObjectRef(blob);
    FakeRevisionClosureSource source(fixedRevision(), {e});

    // 反例 1：canonical 字节为空（CON-06 非空纪律同款——空配置＝占位条目）。
    {
        ConfigEntry c = validConfig("kin.ik-config");
        c.canonicalBytes.clear();
        SnapshotBuilder b = baseBuilder();
        b.addObjectRef(e);
        b.addConfiguration(c);
        expectEvidenceThrow([&b, &source] { b.build(source); },
                            EvidenceErrorCode::SnapshotIncomplete);
    }
    // 反例 2：内容身份零值。
    {
        ConfigEntry c = validConfig("kin.ik-config");
        c.contentIdentity = core::ContentIdentity{};
        SnapshotBuilder b = baseBuilder();
        b.addObjectRef(e);
        b.addConfiguration(c);
        expectEvidenceThrow([&b, &source] { b.build(source); },
                            EvidenceErrorCode::SnapshotIncomplete);
    }
    // 反例 3：同一种类配置两条引用（I-3 唯一性）。
    {
        SnapshotBuilder b = baseBuilder();
        b.addObjectRef(e);
        b.addConfiguration(validConfig("kin.ik-config"));
        b.addConfiguration(validConfig("kin.ik-config"));
        expectEvidenceThrow([&b, &source] { b.build(source); },
                            EvidenceErrorCode::SnapshotIncomplete, "重复");
    }
}

/** 外部资源状态机（CON-03）：Solidified 必带固化版本、Recorded 必不带、resourceId 唯一。 */
TEST(EvidenceSnapshotBuilder, ExternalResourceStateMachine_EV_SNAP)
{
    const std::vector<std::uint8_t> blob = {0x0Bu};
    const ObjectRefEntry e = validObjectRef(blob);
    FakeRevisionClosureSource source(fixedRevision(), {e});

    // 反例 1：Solidified 无固化版本（无版本承诺的"固化"不是固化）。
    {
        ExternalResourceState res;
        res.resourceId = core::ObjectId::generate();
        res.state = ExternalResourceStatus::Solidified;
        SnapshotBuilder b = baseBuilder();
        b.addObjectRef(e);
        b.addExternalResource(res);
        expectEvidenceThrow([&b, &source] { b.build(source); },
                            EvidenceErrorCode::SnapshotIncomplete, "Solidified");
    }
    // 反例 2：Recorded 携带固化版本（presence 语义——噪声即矛盾）。
    {
        ExternalResourceState res;
        res.resourceId = core::ObjectId::generate();
        res.state = ExternalResourceStatus::Recorded;
        res.solidifiedContentVersion = cv(kHexC);
        SnapshotBuilder b = baseBuilder();
        b.addObjectRef(e);
        b.addExternalResource(res);
        expectEvidenceThrow([&b, &source] { b.build(source); },
                            EvidenceErrorCode::SnapshotIncomplete, "Recorded");
    }
    // 反例 3：同资源两条状态记录（自相矛盾——I-3 唯一性）。
    {
        ExternalResourceState res;
        res.resourceId = core::ObjectId::generate();
        res.state = ExternalResourceStatus::Recorded;
        SnapshotBuilder b = baseBuilder();
        b.addObjectRef(e);
        b.addExternalResource(res);
        b.addExternalResource(res);
        expectEvidenceThrow([&b, &source] { b.build(source); },
                            EvidenceErrorCode::SnapshotIncomplete, "重复");
    }
    // 正例：Recorded 无版本、Solidified 带非零版本——两种状态均合法入快照。
    {
        ExternalResourceState recorded;
        recorded.resourceId = core::ObjectId::generate();
        recorded.state = ExternalResourceStatus::Recorded;
        ExternalResourceState solidified;
        solidified.resourceId = core::ObjectId::generate();
        solidified.state = ExternalResourceStatus::Solidified;
        solidified.solidifiedContentVersion = cv(kHexA);
        SnapshotBuilder b = baseBuilder();
        b.addObjectRef(e);
        b.addExternalResource(recorded);
        b.addExternalResource(solidified);
        const AnalysisSnapshot snap = b.build(source);
        ASSERT_EQ(snap.externalResources.size(), static_cast<std::size_t>(2));
    }
}

/** 采样计划不变量：身份字段非零；零样本数合法（§6.6 零样本场景）；同区域唯一。 */
TEST(EvidenceSnapshotBuilder, SamplingPlanInvariants_EV_SNAP)
{
    const std::vector<std::uint8_t> blob = {0x0Cu};
    const ObjectRefEntry e = validObjectRef(blob);
    FakeRevisionClosureSource source(fixedRevision(), {e});

    // 反例 1：sampleSetIdentity 零值（无冻结凭据——KIN-04 分母来源失效）。
    {
        SamplingPlanRef p = validSamplingPlan();
        p.sampleSetIdentity = core::ContentIdentity{};
        SnapshotBuilder b = baseBuilder();
        b.addObjectRef(e);
        b.addSamplingPlan(p);
        expectEvidenceThrow([&b, &source] { b.build(source); },
                            EvidenceErrorCode::SnapshotIncomplete);
    }
    // 正例：计划样本数 0/0 合法（零样本场景——§4.1.4"0 合法"原文）。
    {
        SamplingPlanRef p = validSamplingPlan();
        p.plannedPositionSamples = 0;
        p.plannedPoseSamples = 0;
        SnapshotBuilder b = baseBuilder();
        b.addObjectRef(e);
        b.addSamplingPlan(p);
        const AnalysisSnapshot snap = b.build(source);
        ASSERT_EQ(snap.samplingPlans.size(), static_cast<std::size_t>(1));
        EXPECT_EQ(snap.samplingPlans[0].plannedPositionSamples, 0u);
        EXPECT_EQ(snap.samplingPlans[0].plannedPoseSamples, 0u);
    }
    // 反例 2：同一区域两条计划（I-3 唯一性）。
    {
        SamplingPlanRef p = validSamplingPlan();
        SnapshotBuilder b = baseBuilder();
        b.addObjectRef(e);
        b.addSamplingPlan(p);
        b.addSamplingPlan(p);
        expectEvidenceThrow([&b, &source] { b.build(source); },
                            EvidenceErrorCode::SnapshotIncomplete, "重复");
    }
}

/** 复现块完整性：必填标量非空、可选字段 presence 语义、未设定拒绝。 */
TEST(EvidenceSnapshotBuilder, ReproductionCompleteness_EV_SNAP)
{
    const std::vector<std::uint8_t> blob = {0x0Du};
    const ObjectRefEntry e = validObjectRef(blob);
    FakeRevisionClosureSource source(fixedRevision(), {e});

    // 反例 1：productVersion 为空（复现第一要素缺失——"复现块完整"违约）。
    {
        ReproductionBlock r = validReproduction();
        r.productVersion.clear();
        SnapshotBuilder b = baseBuilder();
        b.addObjectRef(e);
        b.setReproduction(r);
        expectEvidenceThrow([&b, &source] { b.build(source); },
                            EvidenceErrorCode::SnapshotIncomplete, "productVersion");
    }
    // 反例 2：可选字段"有值空串"（presence 语义——§5.2 噪声即矛盾）。
    {
        ReproductionBlock r = validReproduction();
        r.compilerContractVersion = std::string{};
        SnapshotBuilder b = baseBuilder();
        b.addObjectRef(e);
        b.setReproduction(r);
        expectEvidenceThrow([&b, &source] { b.build(source); },
                            EvidenceErrorCode::SnapshotIncomplete,
                            "compilerContractVersion");
    }
    // 反例 3：未设定复现块（§4.1.2 必填）。
    {
        SnapshotBuilder b;
        b.setIdentity(fixedProject(), fixedBranch(), fixedRevision(), 1);
        PolicyRef policy;
        policy.policyContentIdentity = cid(kHexA);
        b.setPolicyRef(policy);
        NameMapRef nameMap;
        nameMap.nameMapContentIdentity = cid(kHexB);
        b.setNameMapRef(nameMap);
        b.addObjectRef(e);
        expectEvidenceThrow([&b, &source] { b.build(source); },
                            EvidenceErrorCode::SnapshotIncomplete, "复现块");
    }
    // 正例：可选字段双缺席（不强制编译器契约/碰撞后端版本——谁必填由评估
    // 器依赖声明表达，evidence 不域判）＋codec 版本族合法元素。
    {
        SnapshotBuilder b = baseBuilder();
        b.addObjectRef(e);
        const AnalysisSnapshot snap = b.build(source);
        EXPECT_FALSE(snap.reproduction.compilerContractVersion.has_value());
        EXPECT_FALSE(snap.reproduction.collisionBackendVersion.has_value());
        EXPECT_EQ(snap.reproduction.codecVersions.size(), static_cast<std::size_t>(2));
    }
}

// =====================================================================
// EV-CASET RequiredCaseSet 冻结承载（§4.1.3——acceptance 3，O-14 保守字面）
// =====================================================================

/** enabled/mandatory/label 字面承载：evidence 只承载标记、原样保留不解释。 */
TEST(EvidenceSnapshotCaseSet, MarkersCarriedVerbatim_O14_EV_CASESET)
{
    // O-14/P-EV-9 保守字面（契约 acceptance 3 编入）：标记的权威 schema 归
    // requirements（WP-14-T01）——evidence 不解读、不派生、不改写标记。
    // 构造"反直觉但字面合法"的标记组合（enabled=false ∧ mandatory=true）
    // 验证承载不越权纠偏；空 label 同样原样承载（设计未禁止）。
    const std::vector<std::uint8_t> blob = {0xE0u};
    const ObjectRefEntry ref = validObjectRef(blob);

    CaseEntry odd;   // 反直觉组合：未启用但标记必验——字面承载，不私裁
    odd.caseId = core::ObjectId::generate();
    odd.label = "";
    odd.enabled = false;
    odd.mandatory = true;
    CaseEntry normal;
    normal.caseId = core::ObjectId::generate();
    normal.label = "标准工况";
    normal.enabled = true;
    normal.mandatory = true;

    SnapshotBuilder b = baseBuilder();
    b.addObjectRef(ref);
    b.addCase(odd);
    b.addCase(normal);
    FakeRevisionClosureSource source(fixedRevision(), {ref});
    const AnalysisSnapshot snap = b.build(source);

    ASSERT_EQ(snap.caseSet.entries.size(), static_cast<std::size_t>(2));
    // 字面核对（冻结序＝caseId 序——与录入序无关，按 caseId 定位）：
    const CaseEntry* storedOdd = nullptr;
    const CaseEntry* storedNormal = nullptr;
    for (const CaseEntry& e : snap.caseSet.entries) {
        if (e.caseId == odd.caseId) {
            storedOdd = &e;
        }
        if (e.caseId == normal.caseId) {
            storedNormal = &e;
        }
    }
    ASSERT_NE(storedOdd, nullptr);
    ASSERT_NE(storedNormal, nullptr);
    EXPECT_FALSE(storedOdd->enabled) << "未启用标记原样承载（不私裁纠偏）";
    EXPECT_TRUE(storedOdd->mandatory) << "必验标记原样承载（O-14 保守字面）";
    EXPECT_EQ(storedOdd->label, "") << "空 label 合法承载（设计未禁止）";
    EXPECT_TRUE(storedNormal->enabled);
    EXPECT_TRUE(storedNormal->mandatory);
    EXPECT_EQ(storedNormal->label, "标准工况");
    // 冻结凭据非零（两条工况的规范摘要在场——§6.6 覆盖矩阵的核对基准）。
    EXPECT_TRUE(snap.caseSet.requiredCaseSetId.isValid());
}

/** caseId 唯一（§4.1.2 非法实例原文）：重复工况 id → builder 拒绝。 */
TEST(EvidenceSnapshotCaseSet, DuplicateCaseIdRejected_EV_CASESET)
{
    const std::vector<std::uint8_t> blob = {0xE1u};
    const ObjectRefEntry ref = validObjectRef(blob);
    FakeRevisionClosureSource source(fixedRevision(), {ref});

    SnapshotBuilder b = baseBuilder();
    b.addObjectRef(ref);
    CaseEntry c1;
    c1.caseId = core::ObjectId::generate();
    c1.label = "工况甲";
    c1.enabled = true;
    CaseEntry c2 = c1;   // 同 caseId（label 同）——重复
    b.addCase(c1);
    b.addCase(c2);

    expectEvidenceThrow(
        [&b, &source] { b.build(source); }, EvidenceErrorCode::SnapshotIncomplete,
        "重复工况 id");
}

/** 空工况集合法（P-EV-7 场景入口）：空集构建通过且凭据为确定值。 */
TEST(EvidenceSnapshotCaseSet, EmptyCaseSetAllowed_EV_CASESET)
{
    // P-EV-7（保守处置在汇总判定层——覆盖平凡完备→DataInsufficient，
    // 不在组装层拒绝）：空集可冻结；其凭据同样由规范编码计算（确定性——
    // 同为"空集"的两次组装凭据相等）。
    const SampleContent content = makeSampleContent();
    SnapshotBuilder b = baseBuilder();
    for (const ObjectRefEntry& e : content.closure) {
        b.addObjectRef(e);
    }
    FakeRevisionClosureSource source(fixedRevision(), content.closure);
    const AnalysisSnapshot snap = b.build(source);

    EXPECT_TRUE(snap.caseSet.entries.empty());
    EXPECT_TRUE(snap.caseSet.requiredCaseSetId.isValid());

    // 确定性：另一份内容同样空集 → 凭据相等（空集凭据是确定值，非随机）。
    const SampleContent content2 = makeSampleContent();
    SnapshotBuilder b2 = baseBuilder();
    for (const ObjectRefEntry& e : content2.closure) {
        b2.addObjectRef(e);
    }
    FakeRevisionClosureSource source2(fixedRevision(), content2.closure);
    const AnalysisSnapshot snap2 = b2.build(source2);
    EXPECT_EQ(snap.caseSet.requiredCaseSetId, snap2.caseSet.requiredCaseSetId);
}

/** 冻结凭据敏感性与规范化：entries 任一字面变化 → requiredCaseSetId 变化；
 *  同内容不同插入序 → 凭据不变（确定性）。 */
TEST(EvidenceSnapshotCaseSet, RequiredCaseSetIdSensitivity_EV_CASESET)
{
    // 基线：两条工况。
    CaseEntry c1;
    c1.caseId = core::ObjectId::generate();
    c1.label = "工况一";
    c1.enabled = true;
    c1.mandatory = true;
    CaseEntry c2;
    c2.caseId = core::ObjectId::generate();
    c2.label = "工况二";
    c2.enabled = true;
    c2.mandatory = false;

    // 辅助：以给定工况集构建并取凭据（闭包/其余字段为固定最小合法集）。
    const auto buildWith = [](std::vector<CaseEntry> cases, bool reverse) {
        SnapshotBuilder b = baseBuilder();
        const std::vector<std::uint8_t> blob = {0xE2u};
        const ObjectRefEntry ref = validObjectRef(blob);
        b.addObjectRef(ref);
        if (reverse) {
            for (auto it = cases.rbegin(); it != cases.rend(); ++it) {
                b.addCase(*it);
            }
        } else {
            for (const CaseEntry& c : cases) {
                b.addCase(c);
            }
        }
        FakeRevisionClosureSource source(fixedRevision(), {ref});
        return b.build(source).caseSet.requiredCaseSetId;
    };

    const core::ContentIdentity baseId = buildWith({c1, c2}, false);
    ASSERT_TRUE(baseId.isValid());

    // 同内容逆序录入 → 凭据不变（规范化摘要与插入序无关，I-4）。
    EXPECT_EQ(buildWith({c1, c2}, true), baseId);

    // mandatory 字面翻转 → 凭据变化（冻结凭据对标记敏感——覆盖矩阵核对
    // 的前提：标记被篡改必被发现）。
    {
        CaseEntry mutated = c2;
        mutated.mandatory = true;
        EXPECT_NE(buildWith({c1, mutated}, false), baseId);
    }
    // label 变化 → 凭据变化（label 入规范编码——文案也是工况承载的一部分）。
    {
        CaseEntry mutated = c2;
        mutated.label = "工况二（改）";
        EXPECT_NE(buildWith({c1, mutated}, false), baseId);
    }
    // 增删条目 → 凭据变化（覆盖范围变化必改凭据）。
    EXPECT_NE(buildWith({c1}, false), baseId);
}

// =====================================================================
// EV-SNAPC SnapshotCodec 双形态（§4.1.5⑤/④b、§5.2）
// =====================================================================

/** 双形态同身份（D-01）：materialized 编码解析出的 snapshotId 与 refs-only 相等。 */
TEST(EvidenceSnapshotCodec, MaterializedSameIdentity_EV_SNAPC)
{
    const SampleContent content = makeSampleContent();
    const AnalysisSnapshot snapshot = assembleSample(content, false, 5);

    // 物化子集：仅闭包第 0、2 条（"物化仅限被评估切片"——允许部分物化）。
    SnapshotCodec::MaterializedPayloads payloads;
    payloads.emplace(content.closure[0].objectId, content.byteBlobs[0]);
    payloads.emplace(content.closure[2].objectId, content.byteBlobs[2]);

    const std::vector<std::uint8_t> refsOnly
        = SnapshotCodec::encodeRefsOnly(snapshot);
    const std::vector<std::uint8_t> materialized
        = SnapshotCodec::encodeMaterialized(snapshot, payloads);

    // 身份恒取 refs-only（§4.1.5⑤：载荷字节已由 contentVersion 承诺）。
    const SnapshotCodec::MaterializedSnapshot parsed
        = SnapshotCodec::parseMaterialized(materialized);
    EXPECT_EQ(parsed.snapshot.snapshotId, snapshot.snapshotId)
        << "双形态 snapshotId 必须相同（D-01）";
    // 快照内容往返等值（除 revisionSeq——I-1 同 refs-only 契约）。
    AnalysisSnapshot expected = snapshot;
    expected.revisionSeq = 0;
    EXPECT_EQ(parsed.snapshot, expected);
    // 载荷逐字节归还（worker 侧物化数据可用）。
    ASSERT_EQ(parsed.payloads.size(), static_cast<std::size_t>(2));
    EXPECT_EQ(parsed.payloads.at(content.closure[0].objectId), content.byteBlobs[0]);
    EXPECT_EQ(parsed.payloads.at(content.closure[2].objectId), content.byteBlobs[2]);
    // 两形态字节可区分（形态字节不同——形态互混由 ParseRejectsStructural
    // Garbage 的反例 3 拒绝）；同身份断言已在上方经 parseMaterialized 成立。
    EXPECT_NE(materialized, refsOnly) << "两形态字节不同（形态字节可区分）";
}

/** ④b 载荷级校验：摘要不符 → SnapshotIntegrity，物化（encode）与接收（parse）双端都拦。 */
TEST(EvidenceSnapshotCodec, MaterializedIntegrity_EV_SNAPC)
{
    const SampleContent content = makeSampleContent();
    const AnalysisSnapshot snapshot = assembleSample(content, false, 5);

    // 物化入口拦截（encodeMaterialized＝物化动作——§4.1.5④b"物化对象字节
    // 时重算 SHA-256 与 contentVersion 比对"）：提供摘要不符的字节。
    {
        SnapshotCodec::MaterializedPayloads bad;
        bad.emplace(content.closure[0].objectId,
                    std::vector<std::uint8_t>{0xFFu, 0xFFu});   // 摘要必不符
        expectEvidenceThrow(
            [&snapshot, &bad] { SnapshotCodec::encodeMaterialized(snapshot, bad); },
            EvidenceErrorCode::SnapshotIntegrity, "contentVersion");
    }
    // 接收端拦截（parseMaterialized＝worker 接收——传输损坏检测点，I-6）：
    // 取合法 materialized 编码，翻转载荷段最后一个字节（结构不变、内容变）。
    {
        SnapshotCodec::MaterializedPayloads payloads;
        payloads.emplace(content.closure[1].objectId, content.byteBlobs[1]);
        std::vector<std::uint8_t> encoding
            = SnapshotCodec::encodeMaterialized(snapshot, payloads);
        ASSERT_FALSE(encoding.empty());
        encoding.back() = static_cast<std::uint8_t>(encoding.back() ^ 0x01u);
        expectEvidenceThrow(
            [&encoding] { SnapshotCodec::parseMaterialized(encoding); },
            EvidenceErrorCode::SnapshotIntegrity);
    }
}

/** 物化闭包子集约束：闭包外对象的载荷 → SnapshotIncomplete（§4.1.5⑤）。 */
TEST(EvidenceSnapshotCodec, MaterializedClosureSubsetOnly_EV_SNAPC)
{
    const SampleContent content = makeSampleContent();
    const AnalysisSnapshot snapshot = assembleSample(content, false, 5);

    // 伪造"闭包外对象"的载荷：字节自洽（摘要一致）但对象不在闭包内——
    // 拒绝依据是闭包包含性而非摘要（与④b 区分——码面各异）。
    const std::vector<std::uint8_t> foreignBlob = {0x99u, 0x98u};
    const ObjectRefEntry foreign = validObjectRef(foreignBlob);
    SnapshotCodec::MaterializedPayloads bad;
    bad.emplace(foreign.objectId, foreignBlob);
    expectEvidenceThrow(
        [&snapshot, &bad] { SnapshotCodec::encodeMaterialized(snapshot, bad); },
        EvidenceErrorCode::SnapshotIncomplete, "闭包外");
}

/** 结构性非法拒绝：截断/坏 magic/形态互混/版本不符/尾随字节 → SnapshotIncomplete。 */
TEST(EvidenceSnapshotCodec, ParseRejectsStructuralGarbage_EV_SNAPC)
{
    const SampleContent content = makeSampleContent();
    const AnalysisSnapshot snapshot = assembleSample(content, false, 5);
    const std::vector<std::uint8_t> encoding = SnapshotCodec::encodeRefsOnly(snapshot);

    // 反例 1：截断（丢尾字节——布局必然不完整）。
    {
        std::vector<std::uint8_t> truncated(encoding.begin(), encoding.end() - 1);
        expectEvidenceThrow([&truncated] { SnapshotCodec::parseRefsOnly(truncated); },
                            EvidenceErrorCode::SnapshotIncomplete, "截断");
    }
    // 反例 2：magic 不符（非本 codec 产物）。
    {
        std::vector<std::uint8_t> bad = encoding;
        bad[0] = 'X';
        expectEvidenceThrow([&bad] { SnapshotCodec::parseRefsOnly(bad); },
                            EvidenceErrorCode::SnapshotIncomplete, "magic");
    }
    // 反例 3：形态互混——refs-only 字节喂 materialized 入口、反之亦然。
    {
        expectEvidenceThrow(
            [&encoding] { SnapshotCodec::parseMaterialized(encoding); },
            EvidenceErrorCode::SnapshotIncomplete, "形态");
        SnapshotCodec::MaterializedPayloads payloads;
        const std::vector<std::uint8_t> materialized
            = SnapshotCodec::encodeMaterialized(snapshot, payloads);
        expectEvidenceThrow(
            [&materialized] { SnapshotCodec::parseRefsOnly(materialized); },
            EvidenceErrorCode::SnapshotIncomplete, "形态");
    }
    // 反例 4：版本不符（编码版本字节被改——升版协商失败的机器判据）。
    {
        std::vector<std::uint8_t> bad = encoding;
        bad[8] = static_cast<std::uint8_t>(bad[8] + 1);
        expectEvidenceThrow([&bad] { SnapshotCodec::parseRefsOnly(bad); },
                            EvidenceErrorCode::SnapshotIncomplete, "版本");
    }
    // 反例 5：尾随多余字节（拼装错误/混入他版字节——静默截断会伪造身份）。
    {
        std::vector<std::uint8_t> padded = encoding;
        padded.push_back(0x00u);
        expectEvidenceThrow([&padded] { SnapshotCodec::parseRefsOnly(padded); },
                            EvidenceErrorCode::SnapshotIncomplete, "尾随");
    }
}

}  // namespace
