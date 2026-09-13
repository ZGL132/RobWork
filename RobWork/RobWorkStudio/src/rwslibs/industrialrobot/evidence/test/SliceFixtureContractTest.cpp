/**
 * @file   SliceFixtureContractTest.cpp
 * @brief  切片契约夹具数据集用例组（EV-T11 产物，跨单元契约面）——
 *         testdata/golden/ev-slice-fixture 数据集经 testkit 装载/校验全链
 *         （lint 面），并以数据集为事实面驱动切片身份契约
 *         （SnapshotBuilder→SliceBuilder→SliceCodec 双形态编码→
 *         parse(encode(x))==x 往返；EV-ID-1 的数据集载体）。
 *
 * 设计依据：
 *   - units/evidence.md §11（"契约夹具数据集〔切片序列化样本〕按 testkit
 *     manifest schema 登记（contract-fixture 类）"、EV-ID-1 行
 *     "SnapshotCodec/SliceCodec 往返 parse(encode(x))==x"）、§12 EV-T11 行
 *     （产物＝"切片契约夹具数据集（_contract_test）"；验证方式＝§11 矩阵
 *     逐条）、§13 testkit 行（evidence 须提供"切片/快照契约夹具数据集"）
 *   - units/testkit.md §4.2.2（manifest 字段表——装载器全量校验）、§4.5
 *     （size＋SHA-256 完整性）、§10.2 evidence 行（数据集清单/ContractCheck
 *     ——本文件消费 GoldenDataset/ToleranceProfile/JsonLite）
 *   - 需求 CON-04（契约版本进身份）、CON-05（切片内容身份＝缓存键与失效
 *     判据）、NFR-COR-02（同内容恒同编码/同身份——跨实现互证的依据）、
 *     NFR-COR-01（解析算例为独立正确性依据首选——expected 由生成脚本
 *     独立重实现，见 manifest.parameters.independence）
 *   - 任务契约 tasks/foundation/EV-T11.json acceptance 4（数据集交付：
 *     testdata/golden/ev-slice-fixture/——lint 通过并随 CI 跑通）
 *
 * 数据集形态（manifest.parameters 详注）：inputs/slice.json＝切片语义描述
 * （单一事实来源）；expected/slice-identity.json＝三身份（snapshotId/
 * sliceId/inputBaselineId）＋SliceCodec 双形态规范编码 hex——由生成脚本
 * 按编码布局规则**独立重实现**产出，与产品 C++ 编码器双实现互证：任何
 * 一侧漂移（codec 版本变更/字节序/长度前缀/presence 规则）都会让本文件
 * 显性失败（这正是字节级契约夹具的防线价值——格式变更必须携数据集
 * 重审，testkit.md §4.7）。
 *
 * 两模式说明：本文件只消费 testkit（core＋标准库）与 testdata 资产，零
 * 框架符号——集成/冒烟两模式均编译进契约测试目标（与 RT-T12
 * GoldenDataLintTest 同款取舍：数据资产校验要求两模式口径一致）。
 */

#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/evidence/Dependency.hpp>
#include <sdurws/ird/evidence/Slice.hpp>
#include <sdurws/ird/evidence/Snapshot.hpp>
#include <sdurws/ird/testkit/Dataset.hpp>
#include <sdurws/ird/testkit/JsonLite.hpp>
#include <sdurws/ird/testkit/TestPaths.hpp>
#include <sdurws/ird/testkit/ToleranceProfile.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace {

using namespace sdurws::ird::evidence;
namespace core = sdurws::ird::core;
namespace tk = sdurws::ird::testkit;

/// 数据集引用（目录名＝datasetId；版本目录＝语义化版本）。
constexpr const char* kDatasetId = "ev-slice-fixture";
constexpr const char* kDatasetVersion = "1.0.0";

// =====================================================================
// 辅助：十六进制/字节转换与 JSON 字段读取
// =====================================================================

/// 字节序列 → 小写十六进制串（规范编码对照的文本形态——expected 侧同形）。
std::string bytesToHex(const std::vector<std::uint8_t>& bytes)
{
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (const std::uint8_t b : bytes) {
        out.push_back(kHex[b >> 4]);
        out.push_back(kHex[b & 0x0F]);
    }
    return out;
}

/// 十六进制串 → 字节序列（expected 编码的载入形态；奇数长/非法字符显性
/// 失败——数据资产损坏不得静默降级）。
std::vector<std::uint8_t> hexToBytes(const std::string& hex)
{
    EXPECT_EQ(hex.size() % 2, 0u) << "hex 长度须为偶数: " << hex;
    std::vector<std::uint8_t> out;
    out.reserve(hex.size() / 2);
    for (std::size_t i = 0; i + 1 < hex.size(); i += 2) {
        const auto nibble = [](char c) -> int {
            if (c >= '0' && c <= '9') { return c - '0'; }
            if (c >= 'a' && c <= 'f') { return c - 'a' + 10; }
            ADD_FAILURE() << "hex 含非法字符: " << c;
            return 0;
        };
        out.push_back(static_cast<std::uint8_t>((nibble(hex[i]) << 4)
                                                | nibble(hex[i + 1])));
    }
    return out;
}

/// JSON 字符串字段读取（缺字段/非串＝数据集非法级——显性失败）。
std::string requireString(const tk::JsonValue& object, const std::string& key)
{
    const tk::JsonValue* v = object.find(key);
    EXPECT_NE(v, nullptr) << "缺少字段: " << key;
    EXPECT_TRUE(v == nullptr || v->isString()) << "字段须为字符串: " << key;
    return v == nullptr ? std::string{} : v->text;
}

/// JSON 对象字段读取（同上）。
const tk::JsonValue* requireObject(const tk::JsonValue& object, const std::string& key)
{
    const tk::JsonValue* v = object.find(key);
    EXPECT_NE(v, nullptr) << "缺少字段: " << key;
    EXPECT_TRUE(v == nullptr || v->isObject()) << "字段须为对象: " << key;
    return v;
}

/// JSON 数组字段读取（同上）。
const tk::JsonValue* requireArray(const tk::JsonValue& object, const std::string& key)
{
    const tk::JsonValue* v = object.find(key);
    EXPECT_NE(v, nullptr) << "缺少字段: " << key;
    EXPECT_TRUE(v == nullptr || v->isArray()) << "字段须为数组: " << key;
    return v;
}

// =====================================================================
// 数据集 → 冻结快照/切片的重建（语义描述 → builder 冻结——单一事实源）
// =====================================================================

/// 修订闭包事实来源替身（EV-REG-3 同源声明见本文件末"替身边界"）：对
/// inputs 描述内的 (oid,cv) 回答 true——快照组装协议归 EV-T03 用例，
/// 本文件只按数据集描述组装。
class FixtureClosureSource : public IRevisionClosureSource {
public:
    bool objectInRevision(core::RevisionId, core::ObjectId, core::ContentVersion) const override
    {
        return true;
    }
};

/**
 * @brief 单条依赖条目的 JSON → DependencyEntry 转换（payload 按 kind 对应
 *        的 alternative——数据集 schema 见 inputs/slice.json 的 face 注）。
 *
 * 本夹具实现三类的解析面（object/configuration/policy——三形态覆盖面）；
 * 其余 kind 不在本数据集 schema（未知 kind 显性失败——描述与实现漂移
 * 即暴露）。
 */
DependencyEntry entryFromJson(const tk::JsonValue& e)
{
    DependencyEntry entry;
    entry.key = requireString(e, "key");
    const std::string kind = requireString(e, "kind");
    // applied：JSON 布尔（JsonLite Kind::Bool——本数据集恒显式携带；
    // 缺字段/非布尔按 false 处理并在下方配对校验中显性暴露）。
    const tk::JsonValue* appliedFlag = e.find("applied");
    entry.applied = appliedFlag != nullptr && appliedFlag->isBool() && appliedFlag->boolean;
    const tk::JsonValue* reason = e.find("notAppliedReason");
    if (reason != nullptr && reason->isString()) {
        entry.notAppliedReason = reason->text;
    }

    if (kind == "object") {
        entry.kind = DependencyKind::Object;
        ObjectDependencyPayload p;
        p.objectId = core::ObjectId::fromCanonical(requireString(e, "objectId"));
        p.contentVersion
            = core::ContentVersion::fromCanonical(requireString(e, "contentVersion"));
        p.objectTypeToken = requireString(e, "objectTypeToken");
        entry.payload = p;
    } else if (kind == "configuration") {
        entry.kind = DependencyKind::Configuration;
        ConfigurationDependencyPayload p;
        p.configKindToken = requireString(e, "configKindToken");
        p.canonicalBytes = hexToBytes(requireString(e, "canonicalBytesHex"));
        p.contentIdentity
            = core::ContentIdentity::fromCanonical(requireString(e, "contentIdentity"));
        entry.payload = p;
    } else if (kind == "policy") {
        entry.kind = DependencyKind::Policy;
        PolicyDependencyPayload p;
        p.policyContentIdentity = core::ContentIdentity::fromCanonical(
            requireString(e, "policyContentIdentity"));
        entry.payload = p;
    } else {
        ADD_FAILURE() << "夹具数据集含未实现的依赖 kind: " << kind;
    }
    return entry;
}

/**
 * @brief 从数据集重建冻结快照（SnapshotBuilder——inputs.snapshot 段；
 *        重建即消费：对象条目经闭包校验、身份由 builder 计算）。
 */
AnalysisSnapshot buildSnapshotFromDataset(const tk::JsonValue& snapDesc)
{
    SnapshotBuilder b;
    const tk::JsonValue* identity = requireObject(snapDesc, "identity");
    const core::ProjectId project
        = core::ProjectId::fromCanonical(requireString(*identity, "project"));
    const core::BranchId branch
        = core::BranchId::fromCanonical(requireString(*identity, "branch"));
    const core::RevisionId revision
        = core::RevisionId::fromCanonical(requireString(*identity, "revision"));
    const tk::JsonValue* seq = identity->find("revisionSeq");
    const double revisionSeq = (seq != nullptr && seq->isNumber()) ? seq->number : 0.0;
    b.setIdentity(project, branch, revision, static_cast<std::uint64_t>(revisionSeq));

    b.setPolicyRef(PolicyRef{core::ContentIdentity::fromCanonical(
        requireString(snapDesc, "policyContentIdentity"))});
    b.setNameMapRef(NameMapRef{core::ContentIdentity::fromCanonical(
        requireString(snapDesc, "nameMapContentIdentity"))});

    const tk::JsonValue* repro = requireObject(snapDesc, "reproduction");
    ReproductionBlock r;
    r.productVersion = requireString(*repro, "productVersion");
    r.evidenceContractVersion = requireString(*repro, "evidenceContractVersion");
    if (const tk::JsonValue* codecs = requireArray(*repro, "codecVersions")) {
        for (const tk::JsonValue& v : codecs->items) {
            r.codecVersions.push_back(v.text);
        }
    }
    b.setReproduction(r);

    if (const tk::JsonValue* objects = requireArray(snapDesc, "objects")) {
        for (const tk::JsonValue& o : objects->items) {
            ObjectRefEntry entry;
            entry.objectId = core::ObjectId::fromCanonical(requireString(o, "objectId"));
            entry.contentVersion
                = core::ContentVersion::fromCanonical(requireString(o, "contentVersion"));
            entry.objectTypeToken = requireString(o, "objectTypeToken");
            // digest＝十六进制体（32 字节——ObjectRefEntry 一致性闸门的
            // 申报值；生成器侧已保证与 contentVersion.bytes 同源）。
            const std::string digestHex = requireString(o, "digestHex");
            std::array<std::uint8_t, 32> digest{};
            const std::vector<std::uint8_t> raw = hexToBytes(digestHex);
            // 非 void 函数不用 ASSERT（无法返回）——EXPECT＋守卫回退：数据
            // 损坏时以零摘要继续组装，后续身份断言必然失配并显性失败。
            EXPECT_EQ(raw.size(), 32u) << "digestHex 须为 32 字节";
            if (raw.size() == 32u) {
                std::copy(raw.begin(), raw.end(), digest.begin());
            }
            entry.digest = digest;
            b.addObjectRef(entry);
        }
    }

    FixtureClosureSource source;
    return b.build(source);
}

/// 从数据集重建冻结切片（SliceBuilder——inputs.entries 段；快照为上文
/// 重建结果，Object 条目经子集校验）。
InputSlice buildSliceFromDataset(const tk::JsonValue& sliceDesc,
                                 const AnalysisSnapshot& snapshot)
{
    SliceBuilder b;
    b.setEvaluation(requireString(sliceDesc, "evaluationKey"),
                    static_cast<std::uint32_t>(sliceDesc.find("evaluatorContractVersion")
                                                   != nullptr
                                           && sliceDesc.find("evaluatorContractVersion")
                                                  ->isNumber()
                                           ? sliceDesc.find("evaluatorContractVersion")
                                                 ->number
                                           : 0.0));
    if (const tk::JsonValue* entries = requireArray(sliceDesc, "entries")) {
        for (const tk::JsonValue& e : entries->items) {
            b.addEntry(entryFromJson(e));
        }
    }
    return b.build(snapshot);
}

// =====================================================================
// 装载与契约断言（lint 面＋契约面）
// =====================================================================

/** 数据集 lint 面装载断言（GoldenDataset::load 全链：解析→schema→
 *  完整性〔SHA-256＋size〕→交叉校验——任一失败抛 dataset-invalid，
 *  本用例判失败；"lint 通过并随 CI 跑通"的用例承载）。 */
TEST(SliceFixtureContract, DatasetLoadsAndPassesManifest_EV_T11_ACC4)
{
    const tk::GoldenDataset ds = [] {
        tk::GoldenDataset d;
        EXPECT_NO_THROW(d = tk::GoldenDataset::load({kDatasetId, kDatasetVersion}))
            << "数据集装载失败（数据资产缺陷——§7.2 dataset-invalid）";
        return d;
    }();

    // manifest 关键字段观测（装载通过但内容漂移的防复发钉住）。
    EXPECT_EQ(ds.manifest().datasetId, "ev-slice-fixture");
    EXPECT_EQ(ds.manifest().version, "1.0.0");
    EXPECT_EQ(ds.manifest().kind, tk::DatasetKind::ContractFixture)
        << "切片契约夹具须登记为 contract-fixture（§11 测试设施行）";
    EXPECT_EQ(ds.manifest().toleranceProfileId, "ev-slice");
    EXPECT_EQ(ds.manifest().toleranceProfileVersion, "1.0.0");
    EXPECT_FALSE(ds.manifest().coveredRequirements.empty());
    // 输入/期望面可解析（装载器交叉校验的复述钉住）。
    EXPECT_NO_THROW((void)ds.resolveInput("inputs/slice.json"));
    EXPECT_NO_THROW((void)ds.resolveExpected("expected/slice-identity.json"));
}

/** 容差档案 ev-slice 装载与零宽条目解析（§4.3.1——字节级等值数据集的
 *  档案化表达；宽于零＝伪造近似等值，EV-ID-2 反例面的档案侧钉住）。 */
TEST(SliceFixtureContract, ToleranceProfileEvSliceLoadsAndResolvesZeroTolerance)
{
    const tk::ToleranceProfile profile = tk::ToleranceProfile::load(
        tk::toleranceProfileDir("ev-slice") / "v1.0.0.json");
    EXPECT_EQ(profile.profileId, "ev-slice");
    EXPECT_EQ(profile.version, "1.0.0");
    EXPECT_FALSE(profile.basis.empty()) << "档案须带依据声明（§4.3.1）";
    ASSERT_GE(profile.entries.size(), 1u);

    // 零宽容差条目（字节级等值语义——absolute 0）。
    const tk::ToleranceEntry& entry = profile.resolve("slice.payload.config-bytes");
    EXPECT_EQ(entry.tolerance.relative, 0.0);
    EXPECT_EQ(entry.tolerance.absolute, 0.0);
    ASSERT_TRUE(entry.allowedMax.has_value());
    EXPECT_EQ(entry.allowedMax->absolute, 0.0);
    EXPECT_EQ(entry.source, tk::ToleranceSource::DatasetDeclared);
}

/** 切片身份契约（EV-ID-1 数据集载体）：按 inputs 语义描述经产品 builder
 *  重建快照/切片→三身份与双形态编码对照 expected 逐字节核对；SliceCodec
 *  parse(expected full) == 重建切片（parse(encode(x))==x 的数据集形态）。 */
TEST(SliceFixtureContract, RebuiltSliceMatchesExpectedIdentityAndEncoding_EV_ID_1)
{
    // ---- 数据集装载（lint 用例之外的全量面在此重入——用例自持） ----
    const tk::GoldenDataset ds = [] {
        tk::GoldenDataset d;
        EXPECT_NO_THROW(d = tk::GoldenDataset::load({kDatasetId, kDatasetVersion}));
        return d;
    }();
    ASSERT_EQ(ds.manifest().datasetId, kDatasetId);

    // ---- expected 三身份与双形态编码（独立重实现一侧的申报值） ----
    const std::string expectedText = [&ds] {
        std::ifstream in(ds.resolveExpected("expected/slice-identity.json"),
                         std::ios::binary);
        EXPECT_TRUE(static_cast<bool>(in)) << "无法读取 expected";
        std::ostringstream ss;
        ss << in.rdbuf();
        return ss.str();
    }();
    const tk::JsonValue expectedRoot = tk::parseJson(expectedText);
    const std::string expectedSnapshotId = requireString(expectedRoot, "snapshotId");
    const std::string expectedSliceId = requireString(expectedRoot, "sliceId");
    const std::string expectedBaselineId = requireString(expectedRoot, "inputBaselineId");
    const std::vector<std::uint8_t> expectedFull
        = hexToBytes(requireString(expectedRoot, "encodingFullHex"));
    const std::vector<std::uint8_t> expectedBaseline
        = hexToBytes(requireString(expectedRoot, "encodingBaselineProjectionHex"));

    // ---- inputs 语义描述 → 产品 builder 重建（消费侧实现） ----
    const std::string inputText = [&ds] {
        std::ifstream in(ds.resolveInput("inputs/slice.json"), std::ios::binary);
        EXPECT_TRUE(static_cast<bool>(in)) << "无法读取 inputs";
        std::ostringstream ss;
        ss << in.rdbuf();
        return ss.str();
    }();
    const tk::JsonValue inputRoot = tk::parseJson(inputText);
    const tk::JsonValue* snapDesc = requireObject(inputRoot, "snapshot");
    ASSERT_NE(snapDesc, nullptr);
    const AnalysisSnapshot snapshot = buildSnapshotFromDataset(*snapDesc);
    const InputSlice slice = buildSliceFromDataset(inputRoot, snapshot);

    // ---- 三身份逐字节对照（CON-05/CON-04/NFR-COR-02——双实现互证） ----
    EXPECT_EQ(slice.snapshotId.toCanonical(), expectedSnapshotId)
        << "快照身份与 expected 不符（SnapshotCodec 布局漂移或数据集失同步）";
    EXPECT_EQ(slice.sliceId.toCanonical(), expectedSliceId)
        << "切片身份与 expected 不符（SliceCodec full 布局漂移或数据集失同步）";
    EXPECT_EQ(slice.inputBaselineId.toCanonical(), expectedBaselineId)
        << "基准身份与 expected 不符（baseline-projection 漂移或数据集失同步）";

    // ---- 双形态规范编码逐字节对照（§5.2 canonical 规则的字节级钉住） ----
    EXPECT_EQ(bytesToHex(SliceCodec::encodeFull(slice)),
              bytesToHex(expectedFull));
    EXPECT_EQ(bytesToHex(SliceCodec::encodeBaselineProjection(slice)),
              bytesToHex(expectedBaseline));

    // ---- 往返（EV-ID-1 观测点：parse(encode(x))==x）----
    InputSlice parsed;
    EXPECT_NO_THROW(parsed = SliceCodec::parse(expectedFull));
    EXPECT_EQ(parsed, slice) << "parse(expected) 与重建切片不等——往返契约破坏";

    // 双层身份的分离面（D-04 复证：本夹具含排除类条目——baseline 编码短于
    // full；身份互不等值）。
    EXPECT_NE(expectedBaseline.size(), expectedFull.size());
    EXPECT_NE(slice.sliceId, slice.inputBaselineId);
}

/** 重建确定性（NFR-COR-02）：同描述二次重建必得同身份同编码（跨进程/
 *  跨会话的缓存键一致性的数据集级复证——EV-ID-1 的"同内容构建两次"）。 */
TEST(SliceFixtureContract, RebuildTwiceGivesIdenticalIdentity_EV_ID_1)
{
    const tk::GoldenDataset ds = [] {
        tk::GoldenDataset d;
        EXPECT_NO_THROW(d = tk::GoldenDataset::load({kDatasetId, kDatasetVersion}));
        return d;
    }();

    const auto loadInput = [&ds] {
        std::ifstream in(ds.resolveInput("inputs/slice.json"), std::ios::binary);
        EXPECT_TRUE(static_cast<bool>(in));
        std::ostringstream ss;
        ss << in.rdbuf();
        return tk::parseJson(ss.str());
    };
    const tk::JsonValue inputFirst = loadInput();
    const tk::JsonValue inputSecond = loadInput();

    const tk::JsonValue* snapFirst = requireObject(inputFirst, "snapshot");
    const tk::JsonValue* snapSecond = requireObject(inputSecond, "snapshot");
    ASSERT_NE(snapFirst, nullptr);
    ASSERT_NE(snapSecond, nullptr);
    const AnalysisSnapshot s1 = buildSnapshotFromDataset(*snapFirst);
    const AnalysisSnapshot s2 = buildSnapshotFromDataset(*snapSecond);
    const InputSlice slice1 = buildSliceFromDataset(inputFirst, s1);
    const InputSlice slice2 = buildSliceFromDataset(inputSecond, s2);

    EXPECT_EQ(s1.snapshotId, s2.snapshotId);
    EXPECT_EQ(slice1, slice2) << "同内容两次构建不等（EV-ID-1 确定性破坏）";
    EXPECT_EQ(SliceCodec::encodeFull(slice1), SliceCodec::encodeFull(slice2));
}

/** 载荷一致性交叉核对：Configuration.contentIdentity 必须＝
 *  SHA-256(canonicalBytes)（数据集自洽面——生成器复核的同源复证，摘要
 *  唯一经 core::ContentDigester，CR-02）。 */
TEST(SliceFixtureContract, ConfigPayloadIdentityMatchesContentDigest)
{
    const tk::GoldenDataset ds = [] {
        tk::GoldenDataset d;
        EXPECT_NO_THROW(d = tk::GoldenDataset::load({kDatasetId, kDatasetVersion}));
        return d;
    }();
    std::ifstream in(ds.resolveInput("inputs/slice.json"), std::ios::binary);
    ASSERT_TRUE(static_cast<bool>(in));
    std::ostringstream ss;
    ss << in.rdbuf();
    const tk::JsonValue inputRoot = tk::parseJson(ss.str());

    const tk::JsonValue* entries = requireArray(inputRoot, "entries");
    ASSERT_NE(entries, nullptr);
    bool checked = false;
    for (const tk::JsonValue& e : entries->items) {
        if (requireString(e, "kind") != "configuration") {
            continue;
        }
        const std::vector<std::uint8_t> bytes
            = hexToBytes(requireString(e, "canonicalBytesHex"));
        core::ContentDigester digester;
        digester.update(bytes.data(), bytes.size());
        const core::Digest256 digest = digester.finalize();
        const core::ContentIdentity declared
            = core::ContentIdentity::fromCanonical(requireString(e, "contentIdentity"));
        EXPECT_EQ(declared.bytes, digest)
            << "Configuration.contentIdentity ≠ SHA-256(canonicalBytes)（数据集自洽破坏）";
        checked = true;
    }
    EXPECT_TRUE(checked) << "数据集未含 configuration 条目（三形态覆盖面缺失）";
}

}  // namespace
