/**
 * @file   SliceTest.cpp
 * @brief  切片用例组——EV-ID-1 身份确定性与往返、EV-ID-2 无浮点近似等价、
 *         canonical 浮点位模式与 NaN/±Inf 拒绝（§5.2 数值行）、CR-02 排除
 *         字段钉住（基准投影参与集）、CR-05 保留 Environment 条目（必填/
 *         值形态/进入 inputBaselineId/与 RT-T09 共享样例）、SliceBuilder
 *         冻结拒绝反例、SliceCodec 结构非法拒绝、EV-INV 前置（身份比较）。
 *
 * 设计依据：
 *   - units/evidence.md §4.2.2/§4.2.3（字段表＋冻结协议）、§5.1（双层身份
 *     ＋EV-ID-1/2 行）、§5.2（canonical 规则＋浮点纪律）、§11 矩阵
 *     （EV-ID-1/EV-ID-2/EV-INV 前置行）、§12 EV-T04 行（验证方式）
 *   - 跨单元红线 CR-02（traceability/foundation-api-diff.md：摘要唯一经
 *     core ContentDigester＋排除字段以 codec 单测钉住）、CR-05（切片
 *     Environment 保留 token 必填/值形态/基准类归属；联合契约测试与
 *     RT-T09 共享同一样例断言身份一致——evidence 不重算 CanonicalModel）
 *   - 需求 CON-04/05/06、KIN-13、NFR-COR-02/03；任务契约
 *     tasks/foundation/EV-T04.json（≙WP-05-T04）acceptance 1～3
 *
 * 组名说明：EV-SLICE/EV-SLICEC/EV-CR02/EV-CR05 为实现侧组名——§11 矩阵
 * 未设 EV-T04 专项组（EV-ERR/EV-DEP/EV-SNAP 同例），对照 §12 EV-T04 行
 * 验证方式"EV-ID-1/2、EV-INV 前置（身份比较）；canonical 往返＋浮点位
 * 模式＋NaN 拒绝用例通过"。
 *
 * 替身边界声明（EV-REG-3 同源纪律）：本文件的 FakeRevisionClosureSource
 * 为接口替身，仅验证 evidence 契约（快照组装的闭包校验调用），其返回
 * 内容不构成任何 project 侧实现正确性证明。
 *
 * CR-05 共享样例说明（acceptance 3 对齐锚点）：runtime 侧
 * runtime/test/SnapshotTest.cpp 的 Cr05ValueSupplyEnvironmentEntries_CR_05
 * 已断言"快照暴露值＝模型/映射/采集点计算值（值传递无重编码）＋样例确定
 * 性"。evidence 不重算 CanonicalModel（CR-05 裁决——本单元无模型重编码
 * 入口），故 evidence 侧的联合断言面为：以该样例的输出**形态**（模型身份
 * ＝cid-<64hex> 规范文本、基线串＝commit/tag＋选项摘要的无空白文本）作为
 * 固定样例值，断言"组装方供给值 → Environment 条目 → SliceCodec 编码 →
 * parse 还原"全链逐字节一致（evidence 对值零重编码），并钉住 CR-05 裁决
 * 的三条身份行为（改基线串→双身份均变；改求解配置→仅 sliceId 变；条目
 * 值透明传递）。runtime 侧样例值若漂移（夹具/编码变更），其测试与本文的
 * 形态闸门共同暴露（跨单元联锁）。
 */

#include <sdurws/ird/evidence/Dependency.hpp>
#include <sdurws/ird/evidence/Errors.hpp>
#include <sdurws/ird/evidence/Slice.hpp>
#include <sdurws/ird/evidence/Snapshot.hpp>

#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/core/Identity.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <string>
#include <variant>
#include <vector>

namespace {

using namespace sdurws::ird::evidence;
namespace core = sdurws::ird::core;

// =====================================================================
// 测试夹具（纯值辅助——无共享状态，用例间独立；固定身份常量保证
// "同内容"断言跨用例可复现——身份只由内容决定，不用随机 ObjectId）
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

/// 固定对象身份（"obj-"+32 个十六进制字符——Id128 规范文本；suffix 用于
/// 区分闭包内多个对象）。
core::ObjectId fixedObjectId(const char* suffixHex32)
{
    return core::ObjectId::fromCanonical(std::string{"obj-"} + suffixHex32);
}

/// 固定项目/分支/修订身份（快照身份三元组——值任意、只须词形合法）。
core::ProjectId fixedProject()
{
    return core::ProjectId::fromCanonical("prj-11111111111111111111111111111111");
}

core::BranchId fixedBranch()
{
    return core::BranchId::fromCanonical("brn-22222222222222222222222222222222");
}

core::RevisionId fixedRevision()
{
    return core::RevisionId::fromCanonical("rev-33333333333333333333333333333333");
}

/// 对字节向量做 SHA-256（core ContentDigester——构造"摘要与字节自洽"的
/// 配置内容身份：Configuration 载荷的身份承诺语义）。
core::ContentIdentity cidOfBytes(const std::vector<std::uint8_t>& bytes)
{
    core::ContentDigester digester;
    digester.update(bytes.data(), bytes.size());
    core::ContentIdentity id;
    id.bytes = digester.finalize();
    return id;
}

/**
 * @brief 修订闭包替身：只认锚定修订＋预登记 (oid,cv) 集合（SnapshotTest
 *        同款最小替身——切片组装只消费已冻结快照，本替身服务于快照
 *        组装段；替身边界声明见文件头）。
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

private:
    core::RevisionId m_revision;           ///< 锚定修订
    std::vector<ObjectRefEntry> m_entries; ///< 该修订的对象清单（替身数据）
};

/// 构造合法复现块（必填标量非空——快照组装的必填项）。
ReproductionBlock validReproduction()
{
    ReproductionBlock r;
    r.productVersion = "industrialrobot-designer 0.1.0";
    r.evidenceContractVersion = "evidence-contract/1";
    r.codecVersions = {"snapshot-codec/1", "slice-codec/1"};
    return r;
}

// =====================================================================
// 冻结快照夹具（切片构建的来源——标准闭包 2 对象，身份固定可复现）
// =====================================================================

/// 标准快照闭包条目（digest 与 contentVersion 自洽——I-2 一致性闸门；
/// 身份/字节固定——"同内容"断言跨用例可复现）。
ObjectRefEntry closureEntry(const char* oidHex32, const std::vector<std::uint8_t>& bytes)
{
    ObjectRefEntry e;
    e.objectId = fixedObjectId(oidHex32);
    core::ContentDigester digester;
    digester.update(bytes.data(), bytes.size());
    e.contentVersion.bytes = digester.finalize();
    e.objectTypeToken = "robot-design";
    e.digest = e.contentVersion.bytes;
    return e;
}

/// 组装一份冻结快照：2 对象闭包＋策略＋名称映射＋复现块（§4.1.2 必填项
/// 的最小合法集——切片条目从闭包取 Object 载荷）。
AnalysisSnapshot makeFrozenSnapshot()
{
    std::vector<ObjectRefEntry> closure;
    closure.push_back(closureEntry("aaaa0000000000000000000000000000", {0xA0u, 0x01u}));
    closure.push_back(closureEntry("bbbb0000000000000000000000000000", {0xB0u, 0x02u}));

    SnapshotBuilder b;
    b.setIdentity(fixedProject(), fixedBranch(), fixedRevision(), 7);
    for (const ObjectRefEntry& e : closure) {
        b.addObjectRef(e);
    }
    PolicyRef policy;
    policy.policyContentIdentity = cid(kHexA);
    b.setPolicyRef(policy);
    NameMapRef nameMap;
    nameMap.nameMapContentIdentity = cid(kHexB);
    b.setNameMapRef(nameMap);
    b.setReproduction(validReproduction());

    FakeRevisionClosureSource source(fixedRevision(), closure);
    return b.build(source);
}

// =====================================================================
// 标准切片条目集（§4.2.1 七类 Kind 的覆盖性组合＋CR-05 双保留条目；
// 添加序刻意打乱——builder 规范化排序的断言前提）
// =====================================================================

/// CR-05 共享样例·模型身份值（cid-<64hex> 规范文本——对齐锚点语义见文件头）。
const char* kCr05ModelIdentityText =
    "cid-1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef";
/// CR-05 共享样例·基线串（commit/tag＋选项摘要形态——无空白）。
const char* kCr05BaselineText = "31b81845-msvc2022-x64";

/// Environment 条目构造辅助（key＝消费角色键、token/value＝CR-05 要素）。
DependencyEntry envEntry(const std::string& key, std::string token, std::string value)
{
    DependencyEntry e;
    e.key = key;
    e.kind = DependencyKind::Environment;
    EnvironmentDependencyPayload p;
    p.token = std::move(token);
    p.valueToken = std::move(value);
    e.payload = std::move(p);
    return e;
}

/// Configuration 载荷构造：tag 字节＋浮点容差的 IEEE754 位模式（§5.2
/// 数值行的域侧生产示例——evidence 提供 canonicalF64 原语，域 schema 语义
/// 不进 evidence）。contentIdentity＝对字节承诺（与快照 ConfigEntry 同源）。
DependencyEntry configEntry(const std::string& key, double tolerance)
{
    DependencyEntry e;
    e.key = key;
    e.kind = DependencyKind::Configuration;
    ConfigurationDependencyPayload p;
    p.configKindToken = "kin.ik-config";
    p.canonicalBytes.push_back(0x54u);  // 'T'——示例域 tag（容差字段）。
    const auto bits = SliceCodec::canonicalF64(tolerance);
    p.canonicalBytes.insert(p.canonicalBytes.end(), bits.begin(), bits.end());
    p.contentIdentity = cidOfBytes(p.canonicalBytes);
    e.payload = std::move(p);
    return e;
}

/// 条目集变体选择器（"仅改 X"用例的构造面——每变体只改一处）。
struct SliceVariant {
    std::vector<DependencyEntry> entries;      ///< 条目集（添加序刻意打乱）
    bool consumesCanonicalModel = true;        ///< CR-05 必填开关（默认消费）
};

/// 标准条目集：Object＋Configuration＋Policy＋NameMap＋SampleSet＋
/// Environment 四条（产品/编译器版本＋CR-05 双保留条目）。Object 载荷取
/// 来源快照闭包对象 0 的真实 (oid,cv)——子集校验要求 (oid,cv) 与快照
/// 承诺精确一致（闭包条目的 cv 是其字节摘要，非任意常量）。
SliceVariant standardVariant(const AnalysisSnapshot& snapshot)
{
    SliceVariant v;
    // —— 闭包对象 0（模型类角色键；载荷与快照闭包精确一致）——
    DependencyEntry obj;
    obj.key = "model.robot-design";
    obj.kind = DependencyKind::Object;
    ObjectDependencyPayload op;
    op.objectId = snapshot.objectClosure[0].objectId;
    op.contentVersion = snapshot.objectClosure[0].contentVersion;
    op.objectTypeToken = "robot-design";
    obj.payload = std::move(op);
    v.entries.push_back(obj);
    // —— 求解配置（容差 1e-6——EV-ID-2 的"仅改配置"基准）——
    v.entries.push_back(configEntry("solver-config", 1e-6));
    // —— 策略（CON-06：进切片）——
    DependencyEntry policy;
    policy.key = "engineering-policy";
    policy.kind = DependencyKind::Policy;
    PolicyDependencyPayload pp;
    pp.policyContentIdentity = cid(kHexA);
    policy.payload = std::move(pp);
    v.entries.push_back(policy);
    // —— 名称映射（CON-06）——
    DependencyEntry nameMap;
    nameMap.key = "runtime-names";
    nameMap.kind = DependencyKind::NameMap;
    NameMapDependencyPayload np;
    np.nameMapContentIdentity = cid(kHexB);
    nameMap.payload = std::move(np);
    v.entries.push_back(nameMap);
    // —— 冻结样本集（§4.1.4）——
    DependencyEntry sampleSet;
    sampleSet.key = "coverage-samples";
    sampleSet.kind = DependencyKind::SampleSet;
    SampleSetDependencyPayload sp;
    sp.regionObjectId = fixedObjectId("cccc0000000000000000000000000000");
    sp.sampleSetIdentity = cid(kHexC);
    sampleSet.payload = std::move(sp);
    v.entries.push_back(sampleSet);
    // —— Environment 版本要素（非基准版本——D-1 排除面）——
    v.entries.push_back(envEntry("env-product", "product-version", "product/0.1.0"));
    v.entries.push_back(
        envEntry("env-compiler", "compiler-contract-version", "compiler-contract/1"));
    // —— CR-05 双保留条目（值传递样例——文件头对齐锚点说明）——
    v.entries.push_back(envEntry("env-model-identity", std::string{kEnvRuntimeModelIdentity},
                                 kCr05ModelIdentityText));
    v.entries.push_back(envEntry("env-robwork-baseline", std::string{kEnvRuntimeRobworkBaseline},
                                 kCr05BaselineText));
    return v;
}

/// 由变体组装切片（snapshot 提供来源身份与闭包事实）。
InputSlice buildSlice(const AnalysisSnapshot& snapshot, const SliceVariant& variant,
                      const std::string& evaluationKey = "kin-batch-ik",
                      std::uint32_t contractVersion = 1)
{
    SliceBuilder b;
    b.setEvaluation(evaluationKey, contractVersion);
    // 按逆序录入——builder 规范化排序后身份与添加序无关（EV-ID-1 前提）。
    for (auto it = variant.entries.rbegin(); it != variant.entries.rend(); ++it) {
        b.addEntry(*it);
    }
    b.setConsumesCanonicalModel(variant.consumesCanonicalModel);
    return b.build(snapshot);
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

/** 同内容切片构建两次（条目添加序不同）→ 双身份逐字节相等＋编码恒同。 */
TEST(EvidenceSliceIdentity, SameContentDeterministicId_EV_ID_1)
{
    const AnalysisSnapshot snapshot = makeFrozenSnapshot();
    const SliceVariant variant = standardVariant(snapshot);

    // 两次构建（buildSlice 内部已按逆序录入，再以正序录入对照——添加序
    // 敏感面被 builder 规范化排序消除，NFR-COR-02）。
    const InputSlice a = buildSlice(snapshot, variant);
    SliceBuilder forward;
    forward.setEvaluation("kin-batch-ik", 1);
    for (const DependencyEntry& e : variant.entries) {
        forward.addEntry(e);
    }
    forward.setConsumesCanonicalModel(true);
    const InputSlice b = forward.build(snapshot);

    // 双身份逐字节相等（§5.1：同内容必得同身份；sliceId/inputBaselineId
    // 都是编码的导出值——字节决定）。
    EXPECT_TRUE(a.sliceId == b.sliceId) << "sliceId 漂移（同内容不同身份）";
    EXPECT_TRUE(a.inputBaselineId == b.inputBaselineId) << "inputBaselineId 漂移";
    // 身份相等 ⇒ 编码字节相等（字节等值是身份的唯一等价关系——§5.1）。
    EXPECT_EQ(SliceCodec::encodeFull(a), SliceCodec::encodeFull(b));
    // 双身份互不替代（§5.1 身份纪律：四类身份互不转换——值不同是常态，
    // 投影比全编码少条目）。
    EXPECT_FALSE(a.sliceId == a.inputBaselineId)
        << "sliceId 与 inputBaselineId 撞值（投影面＝全编码面，D-1 排除面失效）";
}

/** EV-ID-1 往返：parse(encodeFull(x))==x——全字段＋双身份恒等。 */
TEST(EvidenceSliceIdentity, CodecRoundTrip_EV_ID_1)
{
    const AnalysisSnapshot snapshot = makeFrozenSnapshot();
    const InputSlice slice = buildSlice(snapshot, standardVariant(snapshot));

    const std::vector<std::uint8_t> encoding = SliceCodec::encodeFull(slice);
    const InputSlice parsed = SliceCodec::parse(encoding);
    // 往返全字段恒等（§11 EV-ID-1 观测点原文"parse(encode(x))==x"）。
    EXPECT_TRUE(parsed == slice);
    // 解析侧身份为重算值（D-2：身份是编码的函数——解析结果必与内容自洽）。
    EXPECT_TRUE(parsed.sliceId == slice.sliceId);
    EXPECT_TRUE(parsed.inputBaselineId == slice.inputBaselineId);
    // 再编码字节恒同（往返稳定——二次编码不引入漂移）。
    EXPECT_EQ(SliceCodec::encodeFull(parsed), encoding);
}

/** CR-02 摘要单点：sliceId/inputBaselineId 恰为 core ContentDigester 对
 *  对应形态编码的 SHA-256（不存在第二摘要实现——手工重算必须逐字节相等）。 */
TEST(EvidenceSliceIdentity, DigestViaCoreContentDigesterOnly_CR_02)
{
    const AnalysisSnapshot snapshot = makeFrozenSnapshot();
    const InputSlice slice = buildSlice(snapshot, standardVariant(snapshot));

    // 手工重算 sliceId（full 形态编码 → ContentDigester）。
    const std::vector<std::uint8_t> full = SliceCodec::encodeFull(slice);
    core::ContentDigester d1;
    d1.update(full.data(), full.size());
    core::ContentIdentity expectSliceId;
    expectSliceId.bytes = d1.finalize();
    EXPECT_TRUE(slice.sliceId == expectSliceId)
        << "sliceId 偏离 ContentDigester(full)——出现第二摘要路径（CR-02 违约）";

    // 手工重算 inputBaselineId（baseline-projection 形态编码 → 同一摘要器）。
    const std::vector<std::uint8_t> proj = SliceCodec::encodeBaselineProjection(slice);
    core::ContentDigester d2;
    d2.update(proj.data(), proj.size());
    core::ContentIdentity expectBaselineId;
    expectBaselineId.bytes = d2.finalize();
    EXPECT_TRUE(slice.inputBaselineId == expectBaselineId)
        << "inputBaselineId 偏离 ContentDigester(projection)（CR-02 违约）";
}

// =====================================================================
// EV-ID-2 无浮点近似等价（§5.1 数值容差行——acceptance 1）
// =====================================================================

/** 两配置容差相差 1e-15（数值上"近似相等"）→ sliceId 必不等：位模式承载
 *  ＋字节等值身份共同保证"近似相等禁止作身份等价关系"（EV-ID-2 观测点；
 *  缓存/当前性判定不命中的消费面归 EV-T08/EV-T09，身份面在此钉住）。 */
TEST(EvidenceSliceIdentity, NoFloatApproximateEquivalence_EV_ID_2)
{
    const AnalysisSnapshot snapshot = makeFrozenSnapshot();

    // 基准配置容差 1e-6；对照＝1e-6＋1e-15（double 可分辨的近似差——
    // 1e-6 的相邻可表示数间距远小于 1e-15，两值位模式必然不同）。
    const InputSlice base = buildSlice(snapshot, standardVariant(snapshot));
    SliceVariant changed = standardVariant(snapshot);
    for (DependencyEntry& e : changed.entries) {
        if (e.kind == DependencyKind::Configuration) {
            e.payload = configEntry(e.key, 1e-6 + 1e-15).payload;
        }
    }
    const InputSlice altered = buildSlice(snapshot, changed);

    // 配置身份不同（canonical 字节位模式不同 → 内容身份不同）——按 key
    // 显式查找（buildSlice 产出已按 (kind,key) 排序，不用位置下标）。
    const auto configIdentityOf = [](const InputSlice& s) -> core::ContentIdentity {
        for (const DependencyEntry& e : s.entries) {
            if (e.kind == DependencyKind::Configuration) {
                return std::get<ConfigurationDependencyPayload>(e.payload).contentIdentity;
            }
        }
        return core::ContentIdentity{};  // 零保留值——后续断言必不相等即暴露。
    };
    EXPECT_FALSE(configIdentityOf(base) == configIdentityOf(altered))
        << "1e-15 容差差被近似抹平（位模式承载失效）";
    // sliceId 不等（KIN-13：配置进缓存身份——近似等价不作身份键）。
    EXPECT_FALSE(base.sliceId == altered.sliceId)
        << "近似相等的配置得到同一切片身份（EV-ID-2 反例命中）";
    // "缓存/当前性判定不命中"的消费观测点归 EV-T08/EV-T09（computeCurrentness
    // /judgeCacheHit 消费 sliceId 字节等值）——身份面在此钉住。
}

// =====================================================================
// canonical 浮点：位模式往返＋NaN/±Inf 拒绝（§5.2 数值行——acceptance 1）
// =====================================================================

/** 位模式大端序已知向量＋round-trip 精确（含 -0.0/最小规格化数/1e-15 差）。 */
TEST(EvidenceSliceFloat, CanonicalF64BitPatternRoundTrip)
{
    // 已知向量：1.0 = 0x3FF0000000000000（大端字节序逐字节钉住——§5.2
    // "大端"规则的浮点面）。
    const auto one = SliceCodec::canonicalF64(1.0);
    const std::uint8_t expectOne[8] = {0x3Fu, 0xF0u, 0x00u, 0x00u,
                                       0x00u, 0x00u, 0x00u, 0x00u};
    for (int i = 0; i < 8; ++i) {
        ASSERT_EQ(one[static_cast<std::size_t>(i)], expectOne[i]) << "字节 " << i;
    }
    EXPECT_EQ(SliceCodec::parseCanonicalF64(one.data()), 1.0);

    // round-trip 位级精确：有限值集合逐个还原后位型相等（含 -0.0——
    // 位型 0x8000…0 与 +0.0 不同，位模式承载保留该差异进身份）。
    const double samples[] = {0.0, -0.0, 1.5, -2.25, 1e-6, 1e-15, 3.141592653589793,
                              1e300, 5e-324, 1.7976931348623157e308};
    for (const double v : samples) {
        const auto bits = SliceCodec::canonicalF64(v);
        const double back = SliceCodec::parseCanonicalF64(bits.data());
        // 位级比较（memcmp 语义——不用 ==，避免 -0.0==0.0 的数值等值掩盖
        // 位型差异）。
        EXPECT_EQ(std::memcmp(bits.data(), SliceCodec::canonicalF64(back).data(), 8), 0)
            << "round-trip 位型漂移";
    }
    // 0.0 与 -0.0 位模式不同（字节不同 ⇒ 身份不同——EV-ID-2 的符号零面）。
    EXPECT_NE(SliceCodec::canonicalF64(0.0), SliceCodec::canonicalF64(-0.0));
    // 1e-15 差异 → 位模式不同（EV-ID-2 的最小可分辨差面）。
    EXPECT_NE(SliceCodec::canonicalF64(1e-6), SliceCodec::canonicalF64(1e-6 + 1e-15));
}

/** NaN/±Inf 编码入口拒绝（§5.2 原文——抛 EvidenceError）；接收端非有限
 *  位型同样拒绝（双端纪律——runtime RT-Codec f64 同款）。 */
TEST(EvidenceSliceFloat, CanonicalF64NaNInfRejected)
{
    // 编码入口：三类非有限值全部拒绝（EvidenceError 码面统一）。
    expectEvidenceThrow([] { (void)SliceCodec::canonicalF64(std::numeric_limits<double>::quiet_NaN()); },
                        EvidenceErrorCode::SliceIncomplete, "非有限");
    expectEvidenceThrow([] { (void)SliceCodec::canonicalF64(std::numeric_limits<double>::infinity()); },
                        EvidenceErrorCode::SliceIncomplete);
    expectEvidenceThrow([] { (void)SliceCodec::canonicalF64(-std::numeric_limits<double>::infinity()); },
                        EvidenceErrorCode::SliceIncomplete);
    // 接收端：手工构造的 NaN 位型（0x7FF8000000000000 大端）在 parse 端
    // 拒绝——传输损坏/手改字节不静默进入身份计算（NFR-COR-03）。
    const std::uint8_t nanBits[8] = {0x7Fu, 0xF8u, 0x00u, 0x00u,
                                     0x00u, 0x00u, 0x00u, 0x00u};
    expectEvidenceThrow([&] { (void)SliceCodec::parseCanonicalF64(nanBits); },
                        EvidenceErrorCode::SliceIncomplete);
}

// =====================================================================
// CR-02 排除字段钉住（基准投影参与集——acceptance 2）
// =====================================================================

/** 排除面：仅改求解类输入（Configuration/Policy/非基准 Environment）→
 *  sliceId 变而 inputBaselineId 不变；参与面：改 Object/SampleSet/NameMap/
 *  CR-05 双条目 → 双身份均变（foundation-api-diff.md CR-02"排除字段……
 *  实现期以各自 codec 单测钉住"＋CR-05 裁决第 2 点）。 */
TEST(EvidenceSliceBaseline, Cr02ExcludedAndIncludedFieldsPinned_CR_02_CR_05)
{
    const AnalysisSnapshot snapshot = makeFrozenSnapshot();
    const InputSlice base = buildSlice(snapshot, standardVariant(snapshot));

    // 辅助：对基准条目集应用单点变换后重建切片。
    const auto rebuild = [&snapshot](std::function<void(SliceVariant&)> mutate) {
        SliceVariant v = standardVariant(snapshot);
        mutate(v);
        return buildSlice(snapshot, v);
    };

    // ---- 排除面 1：仅改 Configuration（求解类——KIN-13/D-04：进 sliceId
    // 不进 inputBaselineId；§4.2.4"改求解预算复评"的身份语义）。
    {
        const InputSlice s = rebuild([](SliceVariant& v) {
            for (DependencyEntry& e : v.entries) {
                if (e.kind == DependencyKind::Configuration) {
                    e.payload = configEntry(e.key, 5e-3).payload;  // 换容差。
                }
            }
        });
        EXPECT_FALSE(base.sliceId == s.sliceId) << "配置变 sliceId 不变（缓存键失效面破坏）";
        EXPECT_TRUE(base.inputBaselineId == s.inputBaselineId)
            << "配置进入 inputBaselineId（D-1 排除面违约——C5 复评语义破坏）";
    }
    // ---- 排除面 2：仅改 Policy（消费机制——D-1 口径：不在参与清单）。
    {
        const InputSlice s = rebuild([](SliceVariant& v) {
            for (DependencyEntry& e : v.entries) {
                if (e.kind == DependencyKind::Policy) {
                    std::get<PolicyDependencyPayload>(e.payload).policyContentIdentity
                        = cid(kHexC);
                }
            }
        });
        EXPECT_FALSE(base.sliceId == s.sliceId);
        EXPECT_TRUE(base.inputBaselineId == s.inputBaselineId)
            << "Policy 进入 inputBaselineId（§4.2.2'仅模型/需求/工况集/冻结样本集"
            "条目参与'被违反）";
    }
    // ---- 排除面 3：仅改非基准 Environment（product-version——§5.1 排除语
    // "Environment 中的非基准版本"）。
    {
        const InputSlice s = rebuild([](SliceVariant& v) {
            for (DependencyEntry& e : v.entries) {
                if (e.kind == DependencyKind::Environment
                    && std::get<EnvironmentDependencyPayload>(e.payload).token
                           == "product-version") {
                    std::get<EnvironmentDependencyPayload>(e.payload).valueToken
                        = "product/0.2.0";
                }
            }
        });
        EXPECT_FALSE(base.sliceId == s.sliceId) << "Environment 要素变 sliceId 不变";
        EXPECT_TRUE(base.inputBaselineId == s.inputBaselineId)
            << "非基准版本进入 inputBaselineId（§5.1 排除语违约）";
    }
    // ---- 参与面 1：仅改 Object（模型类角色——Object 条目整体换为闭包内
    // 另一对象的 (oid,cv)——子集校验要求成对精确一致）。
    {
        const InputSlice s = rebuild([&snapshot](SliceVariant& v) {
            for (DependencyEntry& e : v.entries) {
                if (e.kind == DependencyKind::Object) {
                    std::get<ObjectDependencyPayload>(e.payload).objectId
                        = snapshot.objectClosure[1].objectId;
                    std::get<ObjectDependencyPayload>(e.payload).contentVersion
                        = snapshot.objectClosure[1].contentVersion;
                }
            }
        });
        EXPECT_FALSE(base.sliceId == s.sliceId);
        EXPECT_FALSE(base.inputBaselineId == s.inputBaselineId)
            << "Object 变 inputBaselineId 不变（基准参与面缺失——EVI-02 拦截失效）";
    }
    // ---- 参与面 2：仅改 SampleSet（冻结样本基准——EV-COV-4 的身份面）。
    {
        const InputSlice s = rebuild([](SliceVariant& v) {
            for (DependencyEntry& e : v.entries) {
                if (e.kind == DependencyKind::SampleSet) {
                    std::get<SampleSetDependencyPayload>(e.payload).sampleSetIdentity
                        = cid(kHexA);
                }
            }
        });
        EXPECT_FALSE(base.sliceId == s.sliceId);
        EXPECT_FALSE(base.inputBaselineId == s.inputBaselineId)
            << "SampleSet 变 inputBaselineId 不变（采样预算复评拦截失效）";
    }
    // ---- 参与面 3：仅改 NameMap（CON-06 基准面）。
    {
        const InputSlice s = rebuild([](SliceVariant& v) {
            for (DependencyEntry& e : v.entries) {
                if (e.kind == DependencyKind::NameMap) {
                    std::get<NameMapDependencyPayload>(e.payload).nameMapContentIdentity
                        = cid(kHexC);
                }
            }
        });
        EXPECT_FALSE(base.sliceId == s.sliceId);
        EXPECT_FALSE(base.inputBaselineId == s.inputBaselineId)
            << "NameMap 变 inputBaselineId 不变";
    }
    // ---- 参与面 4（CR-05）：仅改基线串 → 双身份均变（CR-05 裁决联合
    // 断言 2——跨基线比较被 EVI-02 拦截的身份前提）。
    {
        const InputSlice s = rebuild([](SliceVariant& v) {
            for (DependencyEntry& e : v.entries) {
                if (e.kind == DependencyKind::Environment
                    && std::get<EnvironmentDependencyPayload>(e.payload).token
                           == kEnvRuntimeRobworkBaseline) {
                    std::get<EnvironmentDependencyPayload>(e.payload).valueToken
                        = "31b81845-clang17-x64";
                }
            }
        });
        EXPECT_FALSE(base.sliceId == s.sliceId);
        EXPECT_FALSE(base.inputBaselineId == s.inputBaselineId)
            << "基线串变 inputBaselineId 不变（CR-05 裁决第 2 点违约）";
    }
}

// =====================================================================
// CR-05 必填校验与值形态闸门（acceptance 3）
// =====================================================================

/** 必填：消费 CanonicalModel 的评估缺 runtime.model-identity 或
 *  runtime.robwork-baseline 任一 → build 拒绝；双条目齐 → 通过；不消费
 *  （开关 false）→ 不强制（谁必填由组装方按评估器声明置位——evidence
 *  不域判）。 */
TEST(EvidenceSliceCr05, MandatoryEnvironmentEntries_CR_05)
{
    const AnalysisSnapshot snapshot = makeFrozenSnapshot();

    // 缺 runtime.model-identity → 拒绝（detail 含保留 token——就地定位）。
    {
        SliceVariant v = standardVariant(snapshot);
        v.entries.erase(std::remove_if(v.entries.begin(), v.entries.end(),
                                       [](const DependencyEntry& e) {
                                           return e.kind == DependencyKind::Environment
                                               && std::get<EnvironmentDependencyPayload>(
                                                      e.payload).token
                                                      == kEnvRuntimeModelIdentity;
                                       }),
                        v.entries.end());
        expectEvidenceThrow([&] { (void)buildSlice(snapshot, v); },
                            EvidenceErrorCode::SliceIncomplete, "runtime.model-identity");
    }
    // 缺 runtime.robwork-baseline → 拒绝。
    {
        SliceVariant v = standardVariant(snapshot);
        v.entries.erase(std::remove_if(v.entries.begin(), v.entries.end(),
                                       [](const DependencyEntry& e) {
                                           return e.kind == DependencyKind::Environment
                                               && std::get<EnvironmentDependencyPayload>(
                                                      e.payload).token
                                                      == kEnvRuntimeRobworkBaseline;
                                       }),
                        v.entries.end());
        expectEvidenceThrow([&] { (void)buildSlice(snapshot, v); },
                            EvidenceErrorCode::SliceIncomplete, "runtime.robwork-baseline");
    }
    // 双条目齐 → 通过（标准变体即此形态——双身份均为非零计算值）。
    {
        const InputSlice s = buildSlice(snapshot, standardVariant(snapshot));
        EXPECT_TRUE(s.sliceId.isValid());
        EXPECT_TRUE(s.inputBaselineId.isValid());
    }
    // 开关 false（不消费 CanonicalModel）→ 缺失不强制（non-CM 评估如
    // 纯几何查询——条目可选）。
    {
        SliceVariant v = standardVariant(snapshot);
        v.entries.erase(std::remove_if(v.entries.begin(), v.entries.end(),
                                       [](const DependencyEntry& e) {
                                           return e.kind == DependencyKind::Environment
                                               && (std::get<EnvironmentDependencyPayload>(
                                                       e.payload).token
                                                       == kEnvRuntimeModelIdentity
                                                   || std::get<EnvironmentDependencyPayload>(
                                                          e.payload).token
                                                       == kEnvRuntimeRobworkBaseline);
                                       }),
                        v.entries.end());
        v.consumesCanonicalModel = false;
        const InputSlice s = buildSlice(snapshot, v);
        EXPECT_TRUE(s.sliceId.isValid());
    }
    // 同一保留 token 重复条目（不同 key 承载）→ 拒绝（身份贡献歧义）。
    {
        SliceVariant v = standardVariant(snapshot);
        v.entries.push_back(envEntry("env-model-identity-dup",
                                     std::string{kEnvRuntimeModelIdentity},
                                     kCr05ModelIdentityText));
        expectEvidenceThrow([&] { (void)buildSlice(snapshot, v); },
                            EvidenceErrorCode::SliceIncomplete, "重复");
    }
}

/** 值形态闸门：保留 token 条目的值形态恒久校验（不论必填开关）——
 *  model-identity 须为 cid-<64hex> 规范文本；baseline 须非空无空白
 *  （D-3 口径——跨单元身份可比的前提）。 */
TEST(EvidenceSliceCr05, ValueShapeGate_CR_05)
{
    const AnalysisSnapshot snapshot = makeFrozenSnapshot();

    // model-identity 值非规范文本（非 cid- 形态）→ 拒绝。
    {
        SliceVariant v = standardVariant(snapshot);
        for (DependencyEntry& e : v.entries) {
            if (e.kind == DependencyKind::Environment
                && std::get<EnvironmentDependencyPayload>(e.payload).token
                       == kEnvRuntimeModelIdentity) {
                std::get<EnvironmentDependencyPayload>(e.payload).valueToken
                    = "not-a-cid-text";
            }
        }
        expectEvidenceThrow([&] { (void)buildSlice(snapshot, v); },
                            EvidenceErrorCode::SliceIncomplete, "cid-");
    }
    // baseline 值含空白 → 拒绝（RobWorkBaselineVersion 契约——RT-T09
    // 断言 4 同款"基线文本含空白字符（契约禁止）"）。
    {
        SliceVariant v = standardVariant(snapshot);
        for (DependencyEntry& e : v.entries) {
            if (e.kind == DependencyKind::Environment
                && std::get<EnvironmentDependencyPayload>(e.payload).token
                       == kEnvRuntimeRobworkBaseline) {
                std::get<EnvironmentDependencyPayload>(e.payload).valueToken
                    = "31b81845 msvc2022";  // 内含空格。
            }
        }
        expectEvidenceThrow([&] { (void)buildSlice(snapshot, v); },
                            EvidenceErrorCode::SliceIncomplete, "空白");
    }
}

/** CR-05 共享样例值传递＋联合断言（acceptance 3：与 RT-T09 共享同一样例
 *  断言身份一致，evidence 不重算 CanonicalModel——对齐锚点语义见文件头）：
 *  ①供给值 → 条目 → 编码 → parse 还原全链逐字节一致（零重编码）；
 *  ②仅改求解类 Configuration → sliceId 变而 inputBaselineId 不变（CR-05
 *  裁决联合断言 3）。 */
TEST(EvidenceSliceCr05, SharedSampleValuePassThrough_RT_T09)
{
    const AnalysisSnapshot snapshot = makeFrozenSnapshot();
    const InputSlice slice = buildSlice(snapshot, standardVariant(snapshot));

    // ①全链逐字节一致：样例值在切片条目中原样在在。
    const DependencyEntry* modelEntry = nullptr;
    const DependencyEntry* baselineEntry = nullptr;
    for (const DependencyEntry& e : slice.entries) {
        if (e.kind == DependencyKind::Environment) {
            const auto& p = std::get<EnvironmentDependencyPayload>(e.payload);
            if (p.token == kEnvRuntimeModelIdentity) {
                modelEntry = &e;
            } else if (p.token == kEnvRuntimeRobworkBaseline) {
                baselineEntry = &e;
            }
        }
    }
    ASSERT_NE(modelEntry, nullptr);
    ASSERT_NE(baselineEntry, nullptr);
    // 供给值 → 条目值：逐字节一致（evidence 不重写/不归一/不重编码——
    // CR-05"值由组装方值传递录入"的切片侧承诺）。
    EXPECT_EQ(std::get<EnvironmentDependencyPayload>(modelEntry->payload).valueToken,
              kCr05ModelIdentityText);
    EXPECT_EQ(std::get<EnvironmentDependencyPayload>(baselineEntry->payload).valueToken,
              kCr05BaselineText);
    // 条目值 → 编码 → parse 还原：仍逐字节一致（编码透明——形态闸门外的
    // 任何改写都会在此暴露）。
    const InputSlice parsed = SliceCodec::parse(SliceCodec::encodeFull(slice));
    for (const DependencyEntry& e : parsed.entries) {
        if (e.kind == DependencyKind::Environment) {
            const auto& p = std::get<EnvironmentDependencyPayload>(e.payload);
            if (p.token == kEnvRuntimeModelIdentity) {
                EXPECT_EQ(p.valueToken, kCr05ModelIdentityText);
            } else if (p.token == kEnvRuntimeRobworkBaseline) {
                EXPECT_EQ(p.valueToken, kCr05BaselineText);
            }
        }
    }
    // 样例值形态自证（与 RT-T09 输出形态对齐——cid-<64hex> 可解析回身份）。
    EXPECT_TRUE(core::ContentIdentity::tryFromCanonical(kCr05ModelIdentityText).has_value());

    // ②仅改求解类 Configuration → sliceId 变、inputBaselineId 不变
    // （CR-05 联合断言 3——与 runtime 侧共享样例下的同构断言）。
    SliceVariant changed = standardVariant(snapshot);
    for (DependencyEntry& e : changed.entries) {
        if (e.kind == DependencyKind::Configuration) {
            e.payload = configEntry(e.key, 2e-6).payload;
        }
    }
    const InputSlice altered = buildSlice(snapshot, changed);
    EXPECT_FALSE(slice.sliceId == altered.sliceId);
    EXPECT_TRUE(slice.inputBaselineId == altered.inputBaselineId)
        << "改求解配置影响 inputBaselineId（CR-05 联合断言 3 违约）";
}

// =====================================================================
// SliceBuilder 冻结拒绝反例（§4.2.3② 校验面——EV-SLICE）
// =====================================================================

/** builder 反例组：评估键语法/未冻结快照/空条目集/(kind,key) 重复/
 *  Object 闭包子集（错对象＋同对象不同版本）/applied 配对矛盾/载荷错配
 *  ——全部 SliceIncomplete 且 detail 可定位。 */
TEST(EvidenceSliceBuilder, FreezeRejections_EV_SLICE)
{
    const AnalysisSnapshot snapshot = makeFrozenSnapshot();

    // 评估键语法非法（大写字母——[a-z][a-z0-9-]{1,63} 词表外）。
    expectEvidenceThrow(
        [&] { (void)buildSlice(snapshot, standardVariant(snapshot), "Kin-Batch-IK"); },
        EvidenceErrorCode::SliceIncomplete, "evaluationKey");
    // 评估键含点（依赖键词形≠评估键词形——词形差异的负例）。
    expectEvidenceThrow(
        [&] { (void)buildSlice(snapshot, standardVariant(snapshot), "kin.batch-ik"); },
        EvidenceErrorCode::SliceIncomplete);

    // 未冻结快照（snapshotId 为零保留值——切片只能从冻结快照构建）。
    {
        AnalysisSnapshot unfrozen;  // 默认构造＝未过 builder。
        expectEvidenceThrow([&] { (void)buildSlice(unfrozen, standardVariant(snapshot)); },
                            EvidenceErrorCode::SliceIncomplete, "冻结");
    }

    // 空条目集（§4.2.2 entries ≥1——无依赖的评估没有失效语义）。
    {
        SliceBuilder b;
        b.setEvaluation("kin-batch-ik", 1);
        expectEvidenceThrow([&] { (void)b.build(snapshot); },
                            EvidenceErrorCode::SliceIncomplete, "空");
    }

    // (kind,key) 重复（稳定存储要求唯一）。
    {
        SliceVariant v = standardVariant(snapshot);
        v.entries.push_back(v.entries[0]);  // 复制 Object 条目。
        expectEvidenceThrow([&] { (void)buildSlice(snapshot, v); },
                            EvidenceErrorCode::SliceIncomplete, "重复");
    }

    // Object 条目不在快照闭包内（错对象——子集校验，防漏声明错配）。
    {
        SliceVariant v = standardVariant(snapshot);
        std::get<ObjectDependencyPayload>(v.entries[0].payload).objectId
            = fixedObjectId("ffff0000000000000000000000000000");  // 闭包外 oid。
        expectEvidenceThrow([&] { (void)buildSlice(snapshot, v); },
                            EvidenceErrorCode::SliceIncomplete, "闭包");
    }
    // Object 条目同对象但版本与快照承诺不符（内容错配——(oid,cv) 精确匹配）。
    {
        SliceVariant v = standardVariant(snapshot);
        std::get<ObjectDependencyPayload>(v.entries[0].payload).contentVersion
            = cv(kHexC);  // 闭包中该对象的 cv 是字节摘要（kHexC 必不相同）。
        expectEvidenceThrow([&] { (void)buildSlice(snapshot, v); },
                            EvidenceErrorCode::SliceIncomplete, "闭包");
    }

    // applied/notAppliedReason 配对矛盾（未适用却无原因——presence 语义；
    // 载荷取闭包对象 1 真实 (oid,cv)——排除子集校验干扰，单点验配对面）。
    {
        SliceVariant v = standardVariant(snapshot);
        DependencyEntry e;
        e.key = "collision-models";
        e.kind = DependencyKind::Object;
        ObjectDependencyPayload p;
        p.objectId = snapshot.objectClosure[1].objectId;
        p.contentVersion = snapshot.objectClosure[1].contentVersion;
        p.objectTypeToken = "collision-model";
        e.payload = std::move(p);
        e.applied = false;
        e.notAppliedReason.reset();  // false ∧ 无原因＝矛盾。
        v.entries.push_back(e);
        expectEvidenceThrow([&] { (void)buildSlice(snapshot, v); },
                            EvidenceErrorCode::SliceIncomplete, "notAppliedReason");
    }

    // 载荷与 kind 错配（Environment kind 携带 Policy 载荷——variant 下标
    // 不符，冻结期必须拒绝：错配载荷进 canonical 编码会产生错误身份）。
    {
        SliceVariant v = standardVariant(snapshot);
        DependencyEntry e = envEntry("env-broken", "x-token", "x-value");
        e.payload = PolicyDependencyPayload{cid(kHexA)};  // 载荷错配。
        v.entries.push_back(e);
        expectEvidenceThrow([&] { (void)buildSlice(snapshot, v); },
                            EvidenceErrorCode::SliceIncomplete);
    }
}

/** 未适用条件条目保留在切片且其条件输入仍进身份（§4.2.1 D-10："未适用的
 *  已声明条件依赖保留在切片中（带 notAppliedReason），其条件输入仍在身份
 *  里"——EV-INV 前置·身份比较）。 */
TEST(EvidenceSliceBuilder, NotAppliedEntryStaysInIdentity_EV_INV)
{
    const AnalysisSnapshot snapshot = makeFrozenSnapshot();

    // 变体 A：条件条目未适用（applied=false＋原因在在）——碰撞几何对象
    // 条目（闭包对象 1），域场景替身＝策略禁用碰撞（条件真假解析归请求方，
    // evidence 承载解析结果——§4.2.3②）。载荷取闭包真实 (oid,cv)——
    // 子集校验对未适用条目同样生效（条目在切片中即承诺其内容版本）。
    SliceVariant disabled = standardVariant(snapshot);
    {
        DependencyEntry e;
        e.key = "collision-models";
        e.kind = DependencyKind::Object;
        ObjectDependencyPayload p;
        p.objectId = snapshot.objectClosure[1].objectId;
        p.contentVersion = snapshot.objectClosure[1].contentVersion;
        p.objectTypeToken = "collision-model";
        e.payload = std::move(p);
        e.applied = false;
        e.notAppliedReason = "策略未启用碰撞";
        disabled.entries.push_back(e);
    }
    const InputSlice a = buildSlice(snapshot, disabled);
    EXPECT_TRUE(a.sliceId.isValid());

    // 变体 B：同条目翻转为已适用（条件翻转为真——域场景：策略启用碰撞）。
    SliceVariant enabled = disabled;
    for (DependencyEntry& e : enabled.entries) {
        if (e.key == "collision-models") {
            e.applied = true;
            e.notAppliedReason.reset();
        }
    }
    const InputSlice b = buildSlice(snapshot, enabled);

    // 条目解析结果翻转 → sliceId 变（applied/notAppliedReason 在编码内；
    // 条件翻转必触发重解析——D-10 的身份面）。
    EXPECT_FALSE(a.sliceId == b.sliceId);
}

// =====================================================================
// SliceCodec 结构非法拒绝（EV-SLICEC——传输损坏/手改字节不静默）
// =====================================================================

/** 结构反例组：magic 错/版本错/形态错（投影编码不可 parse）/截断/尾随
 *  字节——全部 SliceIncomplete。 */
TEST(EvidenceSliceCodec, ParseRejectsStructuralGarbage_EV_SLICEC)
{
    const AnalysisSnapshot snapshot = makeFrozenSnapshot();
    const InputSlice slice = buildSlice(snapshot, standardVariant(snapshot));
    const std::vector<std::uint8_t> good = SliceCodec::encodeFull(slice);

    // magic 错（异单元编码——SnapshotCodec 的 IRDSNAP1 在此拦截）。
    {
        std::vector<std::uint8_t> bad = good;
        bad[0] = 'I'; bad[1] = 'R'; bad[2] = 'D'; bad[3] = 'S';
        bad[4] = 'N'; bad[5] = 'A'; bad[6] = 'P'; bad[7] = '1';
        expectEvidenceThrow([&] { (void)SliceCodec::parse(bad); },
                            EvidenceErrorCode::SliceIncomplete, "magic");
    }
    // 版本错（未知版本——升版协商失败）。
    {
        std::vector<std::uint8_t> bad = good;
        bad[8] = 0x7Fu;
        expectEvidenceThrow([&] { (void)SliceCodec::parse(bad); },
                            EvidenceErrorCode::SliceIncomplete, "版本");
    }
    // 形态错（baseline-projection 编码不是往返载体——parse 拒绝）。
    {
        const std::vector<std::uint8_t> proj = SliceCodec::encodeBaselineProjection(slice);
        EXPECT_EQ(proj[9], 0x01u);
        expectEvidenceThrow([&] { (void)SliceCodec::parse(proj); },
                            EvidenceErrorCode::SliceIncomplete, "形态");
    }
    // 截断（砍掉最后 3 字节——长度前缀承诺被破坏）。
    {
        std::vector<std::uint8_t> bad(good.begin(), good.end() - 3);
        expectEvidenceThrow([&] { (void)SliceCodec::parse(bad); },
                            EvidenceErrorCode::SliceIncomplete, "截断");
    }
    // 尾随字节（拼接损坏/形态混淆）。
    {
        std::vector<std::uint8_t> bad = good;
        bad.push_back(0x00u);
        expectEvidenceThrow([&] { (void)SliceCodec::parse(bad); },
                            EvidenceErrorCode::SliceIncomplete, "尾随");
    }
    // 投影面正向核对：投影编码比 full 编码短（排除面真实存在——D-1），
    // 且投影不含评估键（"仅基准"——evaluationKey/契约版本不进投影）。
    {
        const std::vector<std::uint8_t> proj = SliceCodec::encodeBaselineProjection(slice);
        EXPECT_LT(proj.size(), good.size());
        const std::string projText(proj.begin(), proj.end());
        EXPECT_EQ(projText.find("kin-batch-ik"), std::string::npos)
            << "评估键进入基准投影（D-4 分层违约）";
    }
}

}  // namespace
