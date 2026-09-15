/**
 * @file   PersistenceFormatTest.cpp
 * @brief  磁盘格式契约与 canonical 编解码用例组（PRJ-T04）——round-trip
 *         全类型、非法输入拒绝、版本兼容、externalRefs、CR-02 摘要边界。
 *
 * 设计依据：
 *   - units/project.md §4.4（字段级契约）、§4.8（canonical 编码契约——
 *     ASCII/固定字段序/无浮点/parse(dump(x))==x/次版本兼容）、§8.11
 *     （format-legacy/schema-future＋升级指引数据——PM-06/NFR-DEP-04）、
 *     §4.4.5（externalRefs——PM-01/NFR-SEC-01/NFR-REL-04）、§4.6（D-10
 *     对象负载摘要边界）；
 *   - 需求 CON-01、NFR-DEP-04、PM-06、NFR-COR-02（确定性）、NFR-SEC-01、
 *     NFR-REL-04；
 *   - 任务契约 tasks/foundation/PRJ-T04.json acceptance 1～4（每用例的
 *     trace 见 ird-test-report.json——gtest 用例名带追溯字段）。
 *
 * 消费面说明：测试经 src/Codec.hpp 私有头驱动编解码器（同单元测试目标
 * 以相对路径包含——PRJ-T02 先例，R-2 允许形态；产品 include 根不暴露）。
 */

#include "Codec.hpp"

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using namespace sdurws::ird::project;
namespace codec = sdurws::ird::project::codec;
// core 命名空间别名：摘要边界用例直接消费 core::ContentDigester（CR-02
// 对照直算）——测试 TU 在全局命名空间，需显式别名方可限定。
namespace core = sdurws::ird::core;

// =====================================================================
// 测试常量：合法 core 规范文本（32 小写 hex）与 64 小写 hex 摘要。
// 值本身无语义（身份由 core generate 分配；测试用固定文本以便黄金字节
// 断言与失败消息定位——确定性 NFR-COR-02 精神）。
// =====================================================================

constexpr const char* kProjText  = "prj-0123456789abcdef0123456789abcdef";
constexpr const char* kBrnText   = "brn-0123456789abcdef0123456789abcdef";
constexpr const char* kRevText   = "rev-0123456789abcdef0123456789abcdef";
constexpr const char* kRev2Text  = "rev-ffffffffffffffffffffffffffffffff";
constexpr const char* kObjText   = "obj-0123456789abcdef0123456789abcdef";
constexpr const char* kCvText    = "cv-00112233445566778899aabbccddeeff"
                                   "00112233445566778899aabbccddeeff";
constexpr const char* kHex64Text = "00112233445566778899aabbccddeeff"
                                   "00112233445566778899aabbccddeeff";

/// 合法 ObjectId 样本（由 core 规范文本解析——消费面走 core 契约）。
ObjectId objId() { return ObjectId::fromCanonical(kObjText); }
/// 合法 ContentVersion 样本。
ContentVersion cv() { return ContentVersion::fromCanonical(kCvText); }

// ---------------------------------------------------------------------
// 样本工厂：各类型"全字段有效"基线（round-trip 测试的公共起点）。
// ---------------------------------------------------------------------

ProjectStaticIdentity makeStaticIdentity()
{
    ProjectStaticIdentity v;
    v.formatId = kFormatId;
    v.schemaVersion = kSchemaVersionCurrent;
    v.projectId = ProjectId::fromCanonical(kProjText);
    v.createdAtUtc = "2026-09-15T08:30:45.123Z";
    v.createdWithToolVersion = "RobWorkStudio-1.0";
    return v;
}

HeadRecord makeHeadRecord()
{
    HeadRecord v;
    v.formatId = kFormatId;
    v.schemaVersion = kSchemaVersionCurrent;
    v.projectId = ProjectId::fromCanonical(kProjText);
    v.revisionId = RevisionId::fromCanonical(kRevText);
    v.revisionSeq = 7;
    v.branchId = BranchId::fromCanonical(kBrnText);
    v.manifestDigest = kHex64Text;
    return v;
}

RevisionManifest makeManifest(bool withOptionals = true)
{
    RevisionManifest v;
    v.revisionId = RevisionId::fromCanonical(kRevText);
    v.revisionSeq = 7;
    if (withOptionals) {
        v.parentRevisionId = RevisionId::fromCanonical(kRev2Text);
    }
    v.branchId = BranchId::fromCanonical(kBrnText);
    v.committedAtUtc = "2026-09-15T08:30:45.123Z";
    v.metadataRef = ObjectRefPair{objId(), cv()};
    ObjectRef r;
    r.objectId = objId();
    r.contentVersion = cv();
    r.objectTypeToken = "RobotDesign";
    r.digest256 = kHex64Text;
    v.objectRefs.push_back(r);
    if (withOptionals) {
        v.introducedObjects.push_back(cv());
    }
    return v;
}

ProjectMetadataRecord makeMetadata()
{
    ProjectMetadataRecord v;
    v.schemaVersion = kSchemaVersionCurrent;
    v.committedBy = RevisionId::fromCanonical(kRevText);
    v.supersedes = ObjectRefPair{objId(), cv()};
    // 中文显示名：钉住非 ASCII 经 \uXXXX 转义、parse 还原的 round-trip
    // 保真（§4.8 ASCII 磁盘格式；PM-11 显示名可为用户语言）。
    v.projectDisplayName = "机械臂方案A";
    v.primaryBranchId = BranchId::fromCanonical(kBrnText);
    BranchRecord b;
    b.branchId = BranchId::fromCanonical(kBrnText);
    b.label = "main";
    b.baseRevisionId = RevisionId::fromCanonical(kRevText);
    b.tipRevisionId = RevisionId::fromCanonical(kRevText);
    b.createdAtUtc = "2026-09-15T08:30:45.123Z";
    v.branches.push_back(b);
    v.schemeLabels.emplace("scheme-a", "默认方案");
    return v;
}

// 注：正向样本的 commandType 使用不含 '.' 的 token（如 "create-branch"）——
// §4.4.4 冻结语法 ^[a-z0-9-]{3,64} 不含点，而 §6.5 内置命令族示例写作
// "project.create-branch"（含点）——两处矛盾已按 DTB §5.4 登记
// （units/project.md §15.3 P-PR-9），本任务按字段级契约（§4.4.4）执行，
// 不在代码中私自扩词表裁决。

CommandRecord makeCommand(bool withOptionals = true)
{
    CommandRecord v;
    v.commandType = "create-branch";
    v.payloadFormatVersion = 2;
    v.payloadCanonical = "{\"branchName\":\"B\"}";
    if (withOptionals) {
        InverseCommand inv;
        inv.commandType = "delete-branch";
        inv.payloadFormatVersion = 1;
        inv.payloadCanonical = "{\"branchName\":\"B\"}";
        v.inverse = inv;
        ConfirmationRecord c;
        c.findingDigest = kHex64Text;
        c.policyContentId = "cid-00112233445566778899aabbccddeeff"
                            "00112233445566778899aabbccddeeff";
        c.commandDigest = kHex64Text;
        c.baseRevisionId = kRevText;
        c.credential.principal = "operator-zhang";
        c.credential.confirmedAtUtc = "2026-09-15T08:31:00.000Z";
        v.confirmations.push_back(c);
    }
    v.summary = "创建分支 B（含中文摘要）";
    return v;
}

DraftDocument makeDraft(bool withExternalRefs = true)
{
    DraftDocument v;
    v.schemaVersion = kSchemaVersionCurrent;
    v.projectId = ProjectId::fromCanonical(kProjText);
    v.branchId = BranchId::fromCanonical(kBrnText);
    v.moduleId = "robot-design";
    v.baseRevisionId = RevisionId::fromCanonical(kRevText);
    v.payload = "{\"mass\":{\"state\":\"provided\",\"value\":12.5}}";
    if (withExternalRefs) {
        ExternalRefRecord r;
        r.externalRefId = "ext-ref-1";
        r.absolutePath = "D:/assets/mesh.stl";
        r.contentHash256 = kHex64Text;
        r.sizeBytes = 1234567;
        r.recordedAtUtc = "2026-09-15T08:30:45.123Z";
        r.state = "recorded";
        v.externalRefs.push_back(r);
    }
    v.savedAtUtc = "2026-09-15T08:30:45.123Z";
    v.origin = DraftOrigin::Autosave;
    return v;
}

// ---------------------------------------------------------------------
// 断言辅助：期望语句抛出指定稳定码的 StoreError（错误码即稳定契约——
// 只认 code，不匹配 detail 文本；detail 另在升级指引用例中检查键值）。
// ---------------------------------------------------------------------

#define EXPECT_STORE_ERROR(stmt, expectedCode)                                        \
    do {                                                                              \
        bool thrown = false;                                                          \
        try {                                                                         \
            stmt;                                                                     \
        } catch (const StoreError& e) {                                               \
            thrown = true;                                                            \
            EXPECT_EQ(e.code(), expectedCode)                                         \
                << "detail: " << e.what();                                            \
        } catch (const std::exception& e) {                                           \
            ADD_FAILURE() << "抛出非 StoreError 异常: " << e.what();                  \
        }                                                                             \
        if (!thrown) { ADD_FAILURE() << "期望 StoreError 未抛出"; }                   \
    } while (false)

/// 断言文本全部为 ASCII（每字节 <0x80——§4.8 磁盘格式 ASCII）。
void expectPureAscii(const std::string& text, const char* what)
{
    for (const char c : text) {
        if (static_cast<unsigned char>(c) >= 0x80) {
            FAIL() << what << " 输出含非 ASCII 字节（§4.8 磁盘格式 ASCII）";
        }
    }
    SUCCEED();
}

}  // namespace

// =====================================================================
// acceptance 1：parse(dump(x))==x 全类型 round-trip（§4.4 五类型＋§4.2
// 静态标识）＋确定性（同值必同字节）＋ASCII 纯度＋固定字段序。
// =====================================================================

/** 静态标识 round-trip（§4.2 project.json）。 */
TEST(PersistenceFormatRoundTrip, StaticIdentity_ValuePreserved_CON01)
{
    const auto v = makeStaticIdentity();
    const std::string bytes = codec::dump(v);
    expectPureAscii(bytes, "ProjectStaticIdentity");
    const auto back = codec::parseStaticIdentity(bytes);
    EXPECT_EQ(back, v);
}

/** HEAD 记录 round-trip（§4.4.1）＋确定性：两次 dump 逐字节相同。 */
TEST(PersistenceFormatRoundTrip, HeadRecord_ValueAndDeterministicBytes_CON01_NFRCOR02)
{
    const auto v = makeHeadRecord();
    const std::string bytes1 = codec::dump(v);
    const std::string bytes2 = codec::dump(v);
    EXPECT_EQ(bytes1, bytes2) << "同值两次 dump 必须同字节（NFR-COR-02）";
    expectPureAscii(bytes1, "HeadRecord");
    const auto back = codec::parseHeadRecord(bytes1);
    EXPECT_EQ(back, v);
}

/** 修订清单 round-trip：可选字段（parent/introduced）存在与缺省两态。 */
TEST(PersistenceFormatRoundTrip, RevisionManifest_OptionalPresenceAndAbsence_CON01)
{
    const auto with = makeManifest(true);
    const auto backWith = codec::parseRevisionManifest(codec::dump(with));
    EXPECT_EQ(backWith, with);

    const auto without = makeManifest(false);
    const auto backWithout = codec::parseRevisionManifest(codec::dump(without));
    EXPECT_EQ(backWithout, without);
    // 缺省形态的 canonical 字节不含 parent/introduced 键（可选空值省略）。
    EXPECT_EQ(codec::dump(without).find("parentRevisionId"), std::string::npos);
    EXPECT_EQ(codec::dump(without).find("introducedObjects"), std::string::npos);
}

/** 元数据 round-trip：中文显示名 \uXXXX 转义还原＋schemeLabels 键序。 */
TEST(PersistenceFormatRoundTrip, MetadataRecord_ChineseEscapedRoundTrip_CON01_NFRCOR02)
{
    const auto v = makeMetadata();
    const std::string bytes = codec::dump(v);
    expectPureAscii(bytes, "ProjectMetadataRecord");
    // "机械"二字（U+673A U+68B0）的 \u 转义（小写 hex——canonical 字节面）。
    EXPECT_NE(bytes.find("\\u673a\\u68b0"), std::string::npos)
        << "非 ASCII 须以小写 \\uXXXX 转义出现: " << bytes;
    const auto back = codec::parseMetadataRecord(bytes);
    EXPECT_EQ(back, v);
    EXPECT_EQ(back.projectDisplayName, "机械臂方案A");
}

/** 命令留痕 round-trip：inverse/confirmations 存在与最小形态两态。 */
TEST(PersistenceFormatRoundTrip, CommandRecord_FullAndMinimal_CON01)
{
    const auto full = makeCommand(true);
    const auto backFull = codec::parseCommandRecord(codec::dump(full));
    EXPECT_EQ(backFull, full);

    const auto minimal = makeCommand(false);
    const auto backMinimal = codec::parseCommandRecord(codec::dump(minimal));
    EXPECT_EQ(backMinimal, minimal);
    EXPECT_EQ(codec::dump(minimal).find("\"inverse\""), std::string::npos);
    EXPECT_EQ(codec::dump(minimal).find("\"confirmations\""), std::string::npos);
}

/** 草稿 round-trip：externalRefs 存在与缺省两态＋origin 枚举三值。 */
TEST(PersistenceFormatRoundTrip, DraftDocument_WithAndWithoutExternalRefs_CON01)
{
    const auto withRefs = makeDraft(true);
    const auto backWith = codec::parseDraftDocument(codec::dump(withRefs));
    EXPECT_EQ(backWith, withRefs);

    const auto noRefs = makeDraft(false);
    const auto backNo = codec::parseDraftDocument(codec::dump(noRefs));
    EXPECT_EQ(backNo, noRefs);
    EXPECT_EQ(codec::dump(noRefs).find("externalRefs"), std::string::npos);

    // origin 三值逐一 round-trip（token 冻结：autosave/manual/apply-retained）。
    for (const DraftOrigin o :
         {DraftOrigin::Autosave, DraftOrigin::Manual, DraftOrigin::ApplyRetained}) {
        auto d = makeDraft(false);
        d.origin = o;
        const auto back = codec::parseDraftDocument(codec::dump(d));
        EXPECT_EQ(back.origin, o);
        EXPECT_EQ(back, d);
    }
}

/** 黄金字节：HeadRecord 的精确 canonical 文本（钉死字段序与紧凑形态——
 *  防"语义等价但字节漂移"回归破坏摘要可复现性）。 */
TEST(PersistenceFormatRoundTrip, HeadRecord_GoldenBytes_FieldOrderFixed_CON01_NFRCOR02)
{
    const std::string golden =
        std::string("{\"formatId\":\"rwdesign\",\"schemaVersion\":10000")
        + ",\"projectId\":\"" + kProjText + "\""
        + ",\"revisionId\":\"" + kRevText + "\""
        + ",\"revisionSeq\":7"
        + ",\"branchId\":\"" + kBrnText + "\""
        + ",\"manifestDigest\":\"" + kHex64Text + "\"}";
    EXPECT_EQ(codec::dump(makeHeadRecord()), golden);
}

/** 全部六类型 dump 输出纯 ASCII（§4.8；含中文字段的类型逐一覆盖）。 */
TEST(PersistenceFormatRoundTrip, AllTypes_DumpOutputPureAscii_CON01)
{
    expectPureAscii(codec::dump(makeStaticIdentity()), "ProjectStaticIdentity");
    expectPureAscii(codec::dump(makeHeadRecord()), "HeadRecord");
    expectPureAscii(codec::dump(makeManifest(true)), "RevisionManifest");
    expectPureAscii(codec::dump(makeMetadata()), "ProjectMetadataRecord");
    expectPureAscii(codec::dump(makeCommand(true)), "CommandRecord");
    expectPureAscii(codec::dump(makeDraft(true)), "DraftDocument");
}

/** BMP 外字符（U+1F600）经代理对转义并 round-trip 还原。 */
TEST(PersistenceFormatRoundTrip, MetadataRecord_AstralPlaneChar_SurrogatePairRoundTrip_CON01)
{
    auto v = makeMetadata();
    v.projectDisplayName = "\xF0\x9F\x98\x80";  // U+1F600 的 UTF-8
    const std::string bytes = codec::dump(v);
    expectPureAscii(bytes, "ProjectMetadataRecord(astral)");
    // 代理对 \ud83d\ude00（小写）。
    EXPECT_NE(bytes.find("\\ud83d\\ude00"), std::string::npos) << bytes;
    const auto back = codec::parseMetadataRecord(bytes);
    EXPECT_EQ(back.projectDisplayName, v.projectDisplayName);
    EXPECT_EQ(back, v);
}

// =====================================================================
// acceptance 2（数据侧）：非法输入拒绝——截断/越界/语法/身份格式/未知
// 字段（§4.8/§4.4/PM-02 读校验精神：宁可拒绝不猜测）。
// =====================================================================

/** 截断拒绝：六类型各去掉收尾 1 字节（最浅截断——任何结构损坏的通式）。 */
TEST(PersistenceFormatReject, TruncatedInput_Rejected_AllTypes_PRJTX_CON01)
{
    EXPECT_STORE_ERROR(
        codec::parseStaticIdentity(codec::dump(makeStaticIdentity()).substr(0, 10)),
        StoreErrorCode::StoreCorrupt);
    EXPECT_STORE_ERROR(
        codec::parseHeadRecord(codec::dump(makeHeadRecord()).substr(0, 40)),
        StoreErrorCode::StoreCorrupt);
    EXPECT_STORE_ERROR(
        codec::parseRevisionManifest(codec::dump(makeManifest(true)).substr(0, 60)),
        StoreErrorCode::StoreCorrupt);
    EXPECT_STORE_ERROR(
        codec::parseMetadataRecord(codec::dump(makeMetadata()).substr(0, 60)),
        StoreErrorCode::StoreCorrupt);
    EXPECT_STORE_ERROR(
        codec::parseCommandRecord(codec::dump(makeCommand(true)).substr(0, 60)),
        StoreErrorCode::StoreCorrupt);
    EXPECT_STORE_ERROR(
        codec::parseDraftDocument(codec::dump(makeDraft(true)).substr(0, 60)),
        StoreErrorCode::StoreCorrupt);
}

/** 去掉收尾单字节（恰缺 '}'/'"'）同样拒绝——精确截断边界。 */
TEST(PersistenceFormatReject, TruncatedLastByte_Rejected_HeadRecord)
{
    std::string bytes = codec::dump(makeHeadRecord());
    bytes.pop_back();
    EXPECT_STORE_ERROR(codec::parseHeadRecord(bytes), StoreErrorCode::StoreCorrupt);
}

/** 垃圾输入与空输入拒绝。 */
TEST(PersistenceFormatReject, GarbageAndEmptyInput_Rejected)
{
    EXPECT_STORE_ERROR(codec::parseHeadRecord("not-json-at-all"),
                       StoreErrorCode::StoreCorrupt);
    EXPECT_STORE_ERROR(codec::parseHeadRecord(""), StoreErrorCode::StoreCorrupt);
    EXPECT_STORE_ERROR(codec::parseHeadRecord("[1,2,3]"),
                       StoreErrorCode::StoreCorrupt);  // 顶层必须为对象
}

/** 非 ASCII 原始字节拒绝（GBK 误写/编码错乱防线——磁盘格式 ASCII）。 */
TEST(PersistenceFormatReject, NonAsciiRawBytes_Rejected)
{
    // {"projectDisplayName":"机械"} 的 UTF-8 原样字节（未转义）。
    const std::string raw = "{\"schemaVersion\":10000,\"committedBy\":\""
        + std::string(kRevText)
        + "\",\"projectDisplayName\":\"\xe6\x9c\xba\xe6\xa2\xb0\""
          ",\"primaryBranchId\":\""
        + std::string(kBrnText) + "\",\"branches\":[{\"branchId\":\""
        + std::string(kBrnText)
        + "\",\"label\":\"m\",\"baseRevisionId\":\"" + std::string(kRevText)
        + "\",\"tipRevisionId\":\"" + std::string(kRevText)
        + "\",\"createdAtUtc\":\"2026-09-15T00:00:00Z\"}]}";
    EXPECT_STORE_ERROR(codec::parseMetadataRecord(raw), StoreErrorCode::StoreCorrupt);
}

/** 尾随内容拒绝（截断/拼接现场证据）。 */
TEST(PersistenceFormatReject, TrailingContent_Rejected)
{
    std::string bytes = codec::dump(makeHeadRecord());
    bytes += 'x';
    EXPECT_STORE_ERROR(codec::parseHeadRecord(bytes), StoreErrorCode::StoreCorrupt);
}

/** 非 canonical 空白拒绝（严格 canonical：带空白的字节流＝外部产物）。 */
TEST(PersistenceFormatReject, WhitespaceInDocument_Rejected)
{
    const std::string spaced = "{ \"formatId\" : \"rwdesign\" }";
    EXPECT_STORE_ERROR(codec::parseStaticIdentity(spaced), StoreErrorCode::StoreCorrupt);
}

/** 缺必填字段拒绝（HeadRecord 七字段逐一）。 */
TEST(PersistenceFormatReject, HeadRecord_MissingEachField_Rejected)
{
    // 逐字段从黄金文本中删除后重建——以成员级操作构造缺字段文档。
    const auto base = makeHeadRecord();
    const std::string full = codec::dump(base);
    for (const char* field :
         {"formatId", "schemaVersion", "projectId", "revisionId", "revisionSeq",
          "branchId", "manifestDigest"}) {
        // 简化构造：逐个字段剔除（定位 "key": 值段）。用解析层直接验证：
        // 以完整文本删除该键值对——按 '"key"' 查找并移除到下一个 ','或'}'。
        const std::string needle = std::string("\"") + field + "\":";
        const auto pos = full.find(needle);
        ASSERT_NE(pos, std::string::npos) << field;
        auto valueEnd = full.find_first_of(",}", pos + needle.size());
        ASSERT_NE(valueEnd, std::string::npos) << field;
        std::string removed = full.substr(0, pos) + full.substr(valueEnd + 1);
        // 删除后若以 '{' 直接跟 '}'，上层结构判定先失败——同样为拒绝。
        // （SCOPED_TRACE 供失败时定位字段——EXPECT_STORE_ERROR 宏为 do/while
        // 形态，不支持 gtest 流后缀。）
        SCOPED_TRACE(std::string("删除字段 ") + field + " 后应被拒绝");
        EXPECT_STORE_ERROR(codec::parseHeadRecord(removed),
                           StoreErrorCode::StoreCorrupt);
    }
}

/** 未知字段在当前版本拒绝（§4.8：当前版本合法文档不含未知字段）。 */
TEST(PersistenceFormatReject, UnknownField_Rejected_AtCurrentVersion)
{
    std::string bytes = codec::dump(makeHeadRecord());
    bytes.insert(bytes.size() - 1, ",\"suspiciousExtra\":1");
    EXPECT_STORE_ERROR(codec::parseHeadRecord(bytes), StoreErrorCode::StoreCorrupt);
}

/** 类型错位拒绝：字符串字段给整数、整数字段给字符串、对象字段给数组。 */
TEST(PersistenceFormatReject, TypeMismatch_Rejected)
{
    EXPECT_STORE_ERROR(codec::parseHeadRecord("{\"formatId\":\"rwdesign\","
                                              "\"schemaVersion\":\"10000\"}"),
                       StoreErrorCode::StoreCorrupt);
    EXPECT_STORE_ERROR(codec::parseHeadRecord("{\"formatId\":\"rwdesign\","
                                              "\"schemaVersion\":10000,"
                                              "\"projectId\":123}"),
                       StoreErrorCode::StoreCorrupt);
    EXPECT_STORE_ERROR(
        codec::parseRevisionManifest("{\"revisionId\":\"" + std::string(kRevText)
                                     + "\",\"revisionSeq\":1,\"branchId\":\""
                                     + std::string(kBrnText)
                                     + "\",\"committedAtUtc\":\"t\",\"metadataRef\":[]"
                                       ",\"objectRefs\":[{\"objectId\":\""
                                     + std::string(kObjText) + "\",\"contentVersion\":\""
                                     + std::string(kCvText)
                                     + "\",\"objectTypeToken\":\"t\",\"digest256\":\""
                                     + std::string(kHex64Text) + "\"}]}"),
        StoreErrorCode::StoreCorrupt);  // metadataRef 给了数组
}

/** 浮点/指数字面量拒绝（§4.8 无浮点——不允许 1.5/1e3 存在）。 */
TEST(PersistenceFormatReject, FloatAndExponentLiterals_Rejected)
{
    EXPECT_STORE_ERROR(codec::parseHeadRecord("{\"formatId\":\"rwdesign\","
                                              "\"schemaVersion\":10000.5}"),
                       StoreErrorCode::StoreCorrupt);
    EXPECT_STORE_ERROR(codec::parseHeadRecord("{\"formatId\":\"rwdesign\","
                                              "\"schemaVersion\":1e4}"),
                       StoreErrorCode::StoreCorrupt);
}

/** true/false/null 字面量拒绝（契约无布尔/空字段——缺失即可选）。 */
TEST(PersistenceFormatReject, BooleanAndNullLiterals_Rejected)
{
    EXPECT_STORE_ERROR(codec::parseHeadRecord("{\"formatId\":\"rwdesign\","
                                              "\"schemaVersion\":true}"),
                       StoreErrorCode::StoreCorrupt);
    EXPECT_STORE_ERROR(codec::parseHeadRecord("{\"formatId\":null,"
                                              "\"schemaVersion\":10000}"),
                       StoreErrorCode::StoreCorrupt);
}

/** 整数越界拒绝：uint64 序号溢出/uint32 版本溢出/int32 schema 溢出/负序号。 */
TEST(PersistenceFormatReject, IntegerOverflowAndNegative_Rejected)
{
    // revisionSeq = 2^64（超 uint64 上界一位）。
    std::string bytes = codec::dump(makeHeadRecord());
    bytes.replace(bytes.find("\"revisionSeq\":7"), std::string("\"revisionSeq\":7").size(),
                  "\"revisionSeq\":18446744073709551616");
    EXPECT_STORE_ERROR(codec::parseHeadRecord(bytes), StoreErrorCode::StoreCorrupt);

    // payloadFormatVersion = 2^32（超 uint32）。
    const std::string cmd = "{\"commandType\":\"project.create-branch\","
                            "\"payloadFormatVersion\":4294967296,"
                            "\"payloadCanonical\":\"{}\",\"summary\":\"s\"}";
    EXPECT_STORE_ERROR(codec::parseCommandRecord(cmd), StoreErrorCode::StoreCorrupt);

    // schemaVersion = 2^31（超 int32）。
    EXPECT_STORE_ERROR(codec::parseHeadRecord("{\"formatId\":\"rwdesign\","
                                              "\"schemaVersion\":2147483648}"),
                       StoreErrorCode::StoreCorrupt);

    // revisionSeq 负值。
    bytes = codec::dump(makeHeadRecord());
    bytes.replace(bytes.find("\"revisionSeq\":7"), std::string("\"revisionSeq\":7").size(),
                  "\"revisionSeq\":-1");
    EXPECT_STORE_ERROR(codec::parseHeadRecord(bytes), StoreErrorCode::StoreCorrupt);
}

/** 前导零拒绝（非 canonical：同值第二字节形态）。 */
TEST(PersistenceFormatReject, LeadingZeroInteger_Rejected)
{
    std::string bytes = codec::dump(makeHeadRecord());
    bytes.replace(bytes.find("\"revisionSeq\":7"), std::string("\"revisionSeq\":7").size(),
                  "\"revisionSeq\":007");
    EXPECT_STORE_ERROR(codec::parseHeadRecord(bytes), StoreErrorCode::StoreCorrupt);
}

/** 重复键拒绝（canonical 不存在两义性成员）。 */
TEST(PersistenceFormatReject, DuplicateKey_Rejected)
{
    EXPECT_STORE_ERROR(codec::parseHeadRecord("{\"formatId\":\"rwdesign\","
                                              "\"formatId\":\"rwdesign\","
                                              "\"schemaVersion\":10000}"),
                       StoreErrorCode::StoreCorrupt);
}

/** core 身份字段坏规范文本拒绝：tag 错/长度错/大写 hex/缺 tag（§4.4
 *  "身份字段一律 core 规范文本"——消费 core v0.1 解析契约）。 */
TEST(PersistenceFormatReject, BadCanonicalIdentity_Rejected_PPR1Baseline)
{
    // projectId 用了 rev- tag（强类型防误用——core §4.1）。
    EXPECT_STORE_ERROR(codec::parseHeadRecord("{\"formatId\":\"rwdesign\","
                                              "\"schemaVersion\":10000,\"projectId\":\""
                                                  + std::string(kRevText) + "\"}"),
                       StoreErrorCode::StoreCorrupt);
    // revisionId 长度不足（31 hex）。
    EXPECT_STORE_ERROR(codec::parseHeadRecord("{\"formatId\":\"rwdesign\","
                                              "\"schemaVersion\":10000,\"projectId\":\""
                                                  + std::string(kProjText)
                                                  + "\",\"revisionId\":\"rev-0123\"}"),
                       StoreErrorCode::StoreCorrupt);
    // contentVersion 缺 cv- tag（对象文件名为无 tag 64hex，磁盘记录字段必须
    // 带 tag——§4.1/§4.4 区分）。
    EXPECT_STORE_ERROR(
        codec::parseRevisionManifest("{\"revisionId\":\"" + std::string(kRevText)
                                     + "\",\"revisionSeq\":1,\"branchId\":\""
                                     + std::string(kBrnText)
                                     + "\",\"committedAtUtc\":\"t\",\"metadataRef\":"
                                       "{\"objectId\":\""
                                     + std::string(kObjText) + "\",\"contentVersion\":\""
                                     + std::string(kHex64Text) + "\"},\"objectRefs\":["
                                       "{\"objectId\":\""
                                     + std::string(kObjText) + "\",\"contentVersion\":\""
                                     + std::string(kCvText)
                                     + "\",\"objectTypeToken\":\"t\",\"digest256\":\""
                                     + std::string(kHex64Text) + "\"}]}"),
        StoreErrorCode::StoreCorrupt);
    // manifestDigest 大写 hex 拒绝（64 位小写十六进制口径）。
    std::string upper(kHex64Text);
    upper[0] = 'A';
    EXPECT_STORE_ERROR(codec::parseHeadRecord("{\"formatId\":\"rwdesign\","
                                              "\"schemaVersion\":10000,\"projectId\":\""
                                                  + std::string(kProjText)
                                                  + "\",\"revisionId\":\""
                                                  + std::string(kRevText)
                                                  + "\",\"revisionSeq\":1,\"branchId\":\""
                                                  + std::string(kBrnText)
                                                  + "\",\"manifestDigest\":\"" + upper
                                                  + "\"}"),
                       StoreErrorCode::StoreCorrupt);
}

/** commandType 冻结语法 ^[a-z0-9-]{3,64} 拒绝（§4.4.4）。
 *  反例取不含 '.' 的形态（点是否属于合法 token 是 §4.4.4 vs §6.5 的
 *  待裁决冲突 P-PR-9——本用例只钉无争议的违例面）。 */
TEST(PersistenceFormatReject, CommandTypeSyntaxViolation_Rejected)
{
    const auto cmdWith = [](const char* type) {
        return std::string("{\"commandType\":\"") + type
            + "\",\"payloadFormatVersion\":1,\"payloadCanonical\":\"{}\","
              "\"summary\":\"s\"}";
    };
    EXPECT_STORE_ERROR(codec::parseCommandRecord(cmdWith("ab").c_str()),
                       StoreErrorCode::StoreCorrupt);  // 太短（<3）
    EXPECT_STORE_ERROR(codec::parseCommandRecord(cmdWith("CreateBranch").c_str()),
                       StoreErrorCode::StoreCorrupt);  // 大写
    EXPECT_STORE_ERROR(codec::parseCommandRecord(cmdWith("project_create").c_str()),
                       StoreErrorCode::StoreCorrupt);  // 下划线
}

/** origin 非冻结三值拒绝（§4.4.5）。 */
TEST(PersistenceFormatReject, UnknownOrigin_Rejected)
{
    std::string bytes = codec::dump(makeDraft(false));
    bytes.replace(bytes.find("\"origin\":\"autosave\""),
                  std::string("\"origin\":\"autosave\"").size(),
                  "\"origin\":\"sneaky\"");
    EXPECT_STORE_ERROR(codec::parseDraftDocument(bytes), StoreErrorCode::StoreCorrupt);
}

/** objectRefs 空数组与 branches 空数组拒绝（§4.4.2/§4.4.3 ≥1 约束）。 */
TEST(PersistenceFormatReject, EmptyObjectRefsAndBranches_Rejected)
{
    const std::string manifestMinimal =
        std::string("{\"revisionId\":\"") + kRevText + "\",\"revisionSeq\":1"
        + ",\"branchId\":\"" + kBrnText + "\",\"committedAtUtc\":\"t\""
        + ",\"metadataRef\":{\"objectId\":\"" + kObjText + "\",\"contentVersion\":\""
        + kCvText + "\"},\"objectRefs\":[]}";
    EXPECT_STORE_ERROR(codec::parseRevisionManifest(manifestMinimal),
                       StoreErrorCode::StoreCorrupt);

    const std::string metaMinimal =
        std::string("{\"schemaVersion\":10000,\"committedBy\":\"") + kRevText
        + "\",\"projectDisplayName\":\"n\",\"primaryBranchId\":\"" + kBrnText
        + "\",\"branches\":[]}";
    EXPECT_STORE_ERROR(codec::parseMetadataRecord(metaMinimal),
                       StoreErrorCode::StoreCorrupt);
}

/** token 类字段空值拒绝（moduleId/absolutePath——§4.4.5 归属三元组/记录 id）。 */
TEST(PersistenceFormatReject, EmptyTokenField_Rejected)
{
    std::string bytes = codec::dump(makeDraft(true));
    bytes.replace(bytes.find("\"moduleId\":\"robot-design\""),
                  std::string("\"moduleId\":\"robot-design\"").size(),
                  "\"moduleId\":\"\"");
    EXPECT_STORE_ERROR(codec::parseDraftDocument(bytes), StoreErrorCode::StoreCorrupt);
}

/** 嵌套对象的未知字段同样拒绝（子对象不在容忍模式时）。 */
TEST(PersistenceFormatReject, UnknownFieldInNestedObject_Rejected)
{
    const std::string manifest =
        std::string("{\"revisionId\":\"") + kRevText + "\",\"revisionSeq\":1"
        + ",\"branchId\":\"" + kBrnText + "\",\"committedAtUtc\":\"t\""
        + ",\"metadataRef\":{\"objectId\":\"" + kObjText + "\",\"contentVersion\":\""
        + kCvText + "\",\"extra\":1},\"objectRefs\":[{\"objectId\":\"" + kObjText
        + "\",\"contentVersion\":\"" + kCvText
        + "\",\"objectTypeToken\":\"t\",\"digest256\":\"" + kHex64Text + "\"}]}";
    EXPECT_STORE_ERROR(codec::parseRevisionManifest(manifest),
                       StoreErrorCode::StoreCorrupt);
}

// =====================================================================
// acceptance 2（版本侧，NFR-DEP-04/PM-06/§8.11）：未知 schemaVersion——
// 主版本破坏性拒绝＋升级指引数据；次版本追加可选字段兼容。
// =====================================================================

/** 未来主版本拒绝：SchemaFuture＋升级指引数据三键（document/supported/
 *  upgrade——§8.11 行 2/PM-06 数据侧；升级器本体归阶段 B）。 */
TEST(PersistenceFormatVersioning, FutureMajorVersion_Rejected_WithUpgradeGuidance_NFRDEP04)
{
    std::string bytes = codec::dump(makeHeadRecord());
    bytes.replace(bytes.find("\"schemaVersion\":10000"),
                  std::string("\"schemaVersion\":10000").size(),
                  "\"schemaVersion\":20000");
    bool thrown = false;
    try {
        codec::parseHeadRecord(bytes);
        FAIL() << "期望 SchemaFuture 未抛出";
    } catch (const StoreError& e) {
        thrown = true;
        EXPECT_EQ(e.code(), StoreErrorCode::SchemaFuture);
        // 升级指引数据（机器可读键值——PM-06"当前版本、项目版本、升级工具
        // 入口"三要素）。
        EXPECT_NE(std::string(e.what()).find("document=20000"), std::string::npos)
            << e.what();
        EXPECT_NE(std::string(e.what()).find("supported=10000"), std::string::npos)
            << e.what();
        EXPECT_NE(std::string(e.what()).find("upgrade="), std::string::npos)
            << e.what();
    }
    ASSERT_TRUE(thrown);
}

/** 旧主版本拒绝：FormatLegacy（§8.11 行 1——稳定只读拒绝）。 */
TEST(PersistenceFormatVersioning, LegacyMajorVersion_Rejected_FormatLegacy_NFRDEP04)
{
    // 0.9 与 0（重构前/非法旧版本）均比当前支持的主版本更旧。
    for (const char* legacyVer : {"9000", "0"}) {
        SCOPED_TRACE(std::string("schemaVersion=") + legacyVer);
        std::string bytes = codec::dump(makeHeadRecord());
        bytes.replace(bytes.find("\"schemaVersion\":10000"),
                      std::string("\"schemaVersion\":10000").size(),
                      (std::string("\"schemaVersion\":") + legacyVer).c_str());
        EXPECT_STORE_ERROR(codec::parseHeadRecord(bytes),
                           StoreErrorCode::FormatLegacy);
    }
    // 负版本同样走 legacy（非法版本不可能比当前更新）。
    std::string bytes = codec::dump(makeHeadRecord());
    bytes.replace(bytes.find("\"schemaVersion\":10000"),
                  std::string("\"schemaVersion\":10000").size(),
                  "\"schemaVersion\":-5");
    EXPECT_STORE_ERROR(codec::parseHeadRecord(bytes), StoreErrorCode::FormatLegacy);
}

/** formatId 不符＝魔数不符＝旧格式拒绝（§8.11 行 1：.rwproj 等旧格式）。 */
TEST(PersistenceFormatVersioning, FormatIdMismatch_Rejected_FormatLegacy)
{
    std::string bytes = codec::dump(makeHeadRecord());
    bytes.replace(bytes.find("\"formatId\":\"rwdesign\""),
                  std::string("\"formatId\":\"rwdesign\"").size(),
                  "\"formatId\":\"rwproj\"");
    EXPECT_STORE_ERROR(codec::parseHeadRecord(bytes), StoreErrorCode::FormatLegacy);
}

/** 次版本兼容（§4.8 稳定性）：1.1 文档的未知可选字段（顶层＋嵌套）被
 *  容忍跳过，已知字段语义不变；1.0 文档的未知字段仍拒绝（对照）。 */
TEST(PersistenceFormatVersioning, NextMinorVersion_UnknownOptionalFields_Tolerated_NFRDEP04)
{
    // 1.1 草稿：顶层未知字段＋externalRefs 元素内未知字段。
    std::string bytes = codec::dump(makeDraft(true));
    bytes.replace(bytes.find("\"schemaVersion\":10000"),
                  std::string("\"schemaVersion\":10000").size(),
                  "\"schemaVersion\":10001");
    // 顶层注入未知字段。
    bytes.insert(bytes.size() - 1, ",\"futureTopLevelField\":123");
    // externalRefs 元素注入未知字段（值用整数——自有格式无布尔字面量，
    // 词法层即拒绝 true/false，见 BooleanAndNullLiterals_Rejected）。
    bytes.replace(bytes.find("\"state\":\"recorded\""),
                  std::string("\"state\":\"recorded\"").size(),
                  "\"state\":\"recorded\",\"futureRefField\":123");
    const auto back = codec::parseDraftDocument(bytes);
    EXPECT_EQ(back.schemaVersion, 10001);
    EXPECT_EQ(back.projectId, makeDraft(true).projectId);
    EXPECT_EQ(back.moduleId, "robot-design");
    EXPECT_EQ(back.externalRefs.size(), 1u);
    EXPECT_EQ(back.externalRefs[0].state, "recorded");
    // 兼容语义边界：未知字段不被保留——降级读仅用于只读展示，重 dump 丢弃
    // 未知字段（写路径永远写当前版本——Codec.hpp 头注纪律）。
    EXPECT_EQ(codec::dump(back).find("futureTopLevelField"), std::string::npos);

    // 对照：同文档标 1.0（当前版本）时未知字段拒绝。
    std::string same10 = codec::dump(makeDraft(true));
    same10.insert(same10.size() - 1, ",\"futureTopLevelField\":123");
    EXPECT_STORE_ERROR(codec::parseDraftDocument(same10), StoreErrorCode::StoreCorrupt);
}

/** 1.1 文档在元数据嵌套 branches[] 元素内的未知字段同样容忍。 */
TEST(PersistenceFormatVersioning, NextMinorVersion_NestedBranchFields_Tolerated)
{
    std::string bytes = codec::dump(makeMetadata());
    bytes.replace(bytes.find("\"schemaVersion\":10000"),
                  std::string("\"schemaVersion\":10000").size(),
                  "\"schemaVersion\":10002");
    bytes.replace(bytes.find("\"label\":\"main\""),
                  std::string("\"label\":\"main\"").size(),
                  "\"label\":\"main\",\"futureBranchField\":\"x\"");
    const auto back = codec::parseMetadataRecord(bytes);
    ASSERT_EQ(back.branches.size(), 1u);
    EXPECT_EQ(back.branches[0].label, "main");
    EXPECT_EQ(back, [&] {
        auto m = makeMetadata();
        m.schemaVersion = 10002;
        return m;
    }());
}

// =====================================================================
// acceptance 3：externalRefs 记录结构（§4.4.5——PM-01 存储侧数据契约：
// 路径＋内容哈希＝NFR-REL-04 检测数据源）。
// =====================================================================

/** 六字段 round-trip＋uint64 大值 sizeBytes（大文件不截断）。 */
TEST(ExternalRefRecords, SixFieldsRoundTrip_LargeSizeBytes_NFRREL04)
{
    auto d = makeDraft(false);
    ExternalRefRecord r;
    r.externalRefId = "ext-ref-42";
    r.absolutePath = "D:/assets/very long path/mesh facility.stl";
    r.contentHash256 = kHex64Text;
    r.sizeBytes = 18446744073709551615ULL;  // uint64 上界——round-trip 无损
    r.recordedAtUtc = "2026-09-15T08:30:45.123Z";
    r.state = "recorded";
    d.externalRefs.push_back(r);
    const auto back = codec::parseDraftDocument(codec::dump(d));
    ASSERT_EQ(back.externalRefs.size(), 1u);
    EXPECT_EQ(back.externalRefs[0], r);
    EXPECT_EQ(back.externalRefs[0].sizeBytes, 18446744073709551615ULL);
    EXPECT_EQ(back, d);
}

/** 多条记录保序（登记序＝canonical 序——数组元素序是数据本身）。 */
TEST(ExternalRefRecords, MultipleRecords_KeepOrder)
{
    auto d = makeDraft(false);
    for (int i = 0; i < 3; ++i) {
        ExternalRefRecord r;
        r.externalRefId = "ext-ref-" + std::to_string(i);
        r.absolutePath = "D:/assets/file" + std::to_string(i) + ".stl";
        r.contentHash256 = kHex64Text;
        r.sizeBytes = static_cast<std::uint64_t>(i);
        r.recordedAtUtc = "2026-09-15T00:00:00Z";
        r.state = "recorded";
        d.externalRefs.push_back(r);
    }
    const auto back = codec::parseDraftDocument(codec::dump(d));
    ASSERT_EQ(back.externalRefs.size(), 3u);
    for (int i = 0; i < 3; ++i) {
        EXPECT_EQ(back.externalRefs[static_cast<std::size_t>(i)].externalRefId,
                  "ext-ref-" + std::to_string(i));
    }
}

/** 记录字段非法拒绝：contentHash256 非 64hex＋absolutePath 空值。 */
TEST(ExternalRefRecords, InvalidHashAndEmptyPath_Rejected)
{
    auto d = makeDraft(false);
    ExternalRefRecord r;
    r.externalRefId = "ext-ref-1";
    r.absolutePath = "D:/assets/mesh.stl";
    r.contentHash256 = "deadbeef";  // 非 64 hex
    r.sizeBytes = 1;
    r.recordedAtUtc = "2026-09-15T00:00:00Z";
    r.state = "recorded";
    d.externalRefs.push_back(r);
    EXPECT_STORE_ERROR(codec::parseDraftDocument(codec::dump(d)),
                       StoreErrorCode::StoreCorrupt);

    auto d2 = makeDraft(false);
    d2.externalRefs.push_back(r);
    d2.externalRefs[0].contentHash256 = kHex64Text;
    d2.externalRefs[0].absolutePath = "";  // 路径空＝无登记语义
    EXPECT_STORE_ERROR(codec::parseDraftDocument(codec::dump(d2)),
                       StoreErrorCode::StoreCorrupt);
}

// =====================================================================
// acceptance 4：CR-02/D-10 摘要边界——对象负载摘要只经 core ContentDigester
// （contentVersionOf 唯一入口）；编码器对 digest 字段零计算（透传）。
// =====================================================================

/** contentVersionOf 与 core::ContentDigester 直算一致（同一哈希路径），
 *  并以 FIPS 180-2 已知向量 "abc" 双重钉住算法正确性。 */
TEST(ContentDigestBoundary, ContentVersionOf_MatchesCoreDigester_CR02_D10)
{
    const std::string payload = "abc";
    // 直算（不经 Codec）。
    core::ContentDigester direct;
    direct.update(payload.data(), payload.size());
    core::ContentVersion expected;
    expected.bytes = direct.finalize();
    // Codec 唯一入口。
    const auto viaCodec = codec::contentVersionOf(payload);
    EXPECT_EQ(viaCodec, expected);
    // FIPS 180-2 已知向量：SHA-256("abc")。
    EXPECT_EQ(viaCodec.toCanonical(),
              "cv-ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

/** 确定性与区分度：同字节同版本；异字节异版本；空字节序列合法。 */
TEST(ContentDigestBoundary, ContentVersionOf_DeterministicDistinctAndEmpty_CR02)
{
    EXPECT_EQ(codec::contentVersionOf("payload-A"), codec::contentVersionOf("payload-A"));
    EXPECT_NE(codec::contentVersionOf("payload-A"), codec::contentVersionOf("payload-B"));
    // 空负载（0 字节）合法——SHA-256 空串摘要（FIPS 已知向量）。
    EXPECT_EQ(codec::contentVersionOf("").toCanonical(),
              "cv-e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    // 分块等价性（core 纯函数性——与调用方式无关；注意 "load-A" 为 6 字节）。
    core::ContentDigester chunked;
    chunked.update("pay", 3);
    chunked.update("load-A", 6);
    core::ContentVersion chunkedVersion;
    chunkedVersion.bytes = chunked.finalize();
    EXPECT_EQ(codec::contentVersionOf("payload-A"), chunkedVersion);
}

/** 域负载逐字节透传（D-10 不解释）：payload 含 SourcedValue 样例序列化
 *  文本，round-trip 逐字节相等；摘要只对收到字节计算。 */
TEST(ContentDigestBoundary, DomainPayload_TransparentBytePreservation_CR02_D10)
{
    auto d = makeDraft(false);
    // 模拟域 canonical 负载：含 SourcedValue 四态样例（域序列化约定归域
    // 登记——project 不解释其语义，只保字节）。
    d.payload = "{\"thickness\":{\"state\":\"provided\",\"value\":8.0,"
                "\"provenance\":{\"kind\":\"user-provided\"}},"
                "\"temperature\":{\"state\":\"not-provided\"},"
                "\"note\":\"\\u6e29\\u5ea6\"}";
    const auto back = codec::parseDraftDocument(codec::dump(d));
    EXPECT_EQ(back.payload, d.payload) << "域负载须逐字节保真";
    EXPECT_EQ(back, d);
    // 摘要＝对收到字节（负载原文）计算——与 Codec 其它字段无关。
    EXPECT_EQ(codec::contentVersionOf(back.payload),
              codec::contentVersionOf(d.payload));
}

/** 编码器零哈希路径（CR-02"不私设第二哈希路径"的反证式钉法）：全部
 *  digest 类字段填"非真实"摘要值，round-trip 原样保留——若编码器私设
 *  重算/校验路径，此用例即失败。 */
TEST(ContentDigestBoundary, DigestFields_PassedThroughNotRecomputed_CR02)
{
    // "非真实"但格式合法的 64hex（真实 manifest 摘要不等于这些字段值的
    // 任何哈希——重算必不相等）。
    const std::string fakeDigest = "ffffffffffffffffffffffffffffffff"
                                   "ffffffffffffffffffffffffffffffff";
    auto head = makeHeadRecord();
    head.manifestDigest = fakeDigest;
    EXPECT_EQ(codec::parseHeadRecord(codec::dump(head)).manifestDigest, fakeDigest);

    auto manifest = makeManifest(true);
    manifest.objectRefs[0].digest256 = fakeDigest;
    EXPECT_EQ(codec::parseRevisionManifest(codec::dump(manifest)).objectRefs[0].digest256,
              fakeDigest);

    auto draft = makeDraft(true);
    draft.externalRefs[0].contentHash256 = fakeDigest;
    EXPECT_EQ(
        codec::parseDraftDocument(codec::dump(draft)).externalRefs[0].contentHash256,
        fakeDigest);
}

/** dump 侧调用方契约违约 fail-fast：payload 含无效 UTF-8 字节序列 →
 *  std::invalid_argument（调用方错误——域 canonical 契约归 core §6.3；
 *  数据侧 StoreError 与调用方 fail-fast 的错误二分，AGENTS §3）。 */
TEST(ContentDigestBoundary, InvalidUtf8Payload_DumpRejected_FailFast)
{
    auto d = makeDraft(false);
    d.payload = "\xFF\xFE not utf8";  // 无效 UTF-8 首字节
    bool thrown = false;
    try {
        (void)codec::dump(d);
        FAIL() << "期望 std::invalid_argument 未抛出";
    } catch (const std::invalid_argument&) {
        thrown = true;
    } catch (...) {
        ADD_FAILURE() << "抛出了非 invalid_argument 异常（应 fail-fast 而非数据侧错误）";
    }
    ASSERT_TRUE(thrown);
}

// =====================================================================
// acceptance 4（P-PR-1 基线证据）：消费 core v0.1 契约的边界行为——
// 身份类型严格解析（消费正确性；core 本体行为由 CORE-T02 用例钉住）。
// =====================================================================

/** 消费面：core Id128/ContentVersion 的严格解析契约（tag/长度/字符集），
 *  project 全部身份字段经此入账——错误文本在解析边界即失败（防误用）。 */
TEST(CoreContractConsumption, IdStrictness_ConsumedAsBaseline_PPR1)
{
    EXPECT_TRUE(ProjectId::tryFromCanonical(kProjText).has_value());
    // tag 错误：rev- 文本喂 ProjectId 拒绝（强类型纪律——core §4.1）。
    EXPECT_FALSE(ProjectId::tryFromCanonical(kRevText).has_value());
    // 大写 hex 拒绝。
    std::string upper = kProjText;
    upper[4] = 'A';
    EXPECT_FALSE(ProjectId::tryFromCanonical(upper).has_value());
    // ContentVersion：cv- tag 必需。
    EXPECT_TRUE(ContentVersion::tryFromCanonical(kCvText).has_value());
    EXPECT_FALSE(ContentVersion::tryFromCanonical(kHex64Text).has_value());
    // parse(format(x))==x 往返（core §5.1 契约——project 编解码依赖的前提）。
    const auto id = ProjectId::fromCanonical(kProjText);
    EXPECT_EQ(id.toCanonical(), kProjText);
}
