/**
 * @file   JsonTest.cpp
 * @brief  JSON 读写器用例组（IoJson）——V07 SchemaUnknownFuture（未知
 *         字段拒/Preserve 透传/未来版本只读拒绝＋升级指引数据/重复键与
 *         缺必填定位）、V08 NaNInfLimits（NaN/Inf/1e999 拒、超长字符串/
 *         深嵌套/文档预算三要素）、canonical 字节一致与内容摘要（core
 *         ContentDigester 唯一算法）、版本门与语法/编码拒绝、注册表契约。
 *
 * 设计依据：
 *   - units/io.md §11.2 IO-V07/V08 行（本文件用例一一对应）、§5.9.1~
 *     §5.9.4（被测语义）、§9.5（接口契约）、§9.12（错误码面）
 *   - 需求 REQ-12/OPT-12（JSON 工件通道）、PM-06（未来版本只读拒绝＋
 *     升级指引数据——判定与升级器归 project，io 提供格式探测数据）、
 *     NFR-DEP-04（schema 演进走版本升级）、NFR-SEC-02（JSON 预算）、
 *     SA-12/NFR-MNT-03（SHA-256 唯一摘要算法）
 *   - 任务契约 tasks/foundation/IO-T04.json acceptance 1~3
 *
 * 断言纪律（AGENTS.md §2.7）：每个用例中文注明验证的需求/验收条目；
 * 测试以真实构造字节/真实文件为替身载体，失败如实失败不伪造。
 */

#include <sdurws/ird/io/Json.hpp>

#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/io/Budget.hpp>
#include <sdurws/ird/io/IoDiagnostics.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

using sdurws::ird::core::ContentDigester;
using sdurws::ird::io::BudgetDimension;
using sdurws::ird::io::BudgetScopeId;
using sdurws::ird::io::BudgetSpec;
using sdurws::ird::io::IBudgetGuardPtr;
using sdurws::ird::io::IoErrorCode;
using sdurws::ird::io::IoResult;
using sdurws::ird::io::IoString;
using sdurws::ird::io::JsonDocument;
using sdurws::ird::io::JsonProfile;
using sdurws::ird::io::JsonProfileRegistry;
using sdurws::ird::io::JsonProperty;
using sdurws::ird::io::JsonReadOptions;
using sdurws::ird::io::JsonShape;
using sdurws::ird::io::JsonValue;
using sdurws::ird::io::JsonValueType;
using sdurws::ird::io::JsonWriteOptions;
using sdurws::ird::io::canonicalizeJson;
using sdurws::ird::io::digestCanonicalJson;
using sdurws::ird::io::errorCodeToken;
using sdurws::ird::io::makeBudgetGuard;
using sdurws::ird::io::makeJsonWriter;
using sdurws::ird::io::makeStructuredDataReader;

namespace {

// =====================================================================
// 测试助手
// =====================================================================

/// 从 IoError params 取键值（三要素/定位参数断言用——CsvTest 同款）。
std::string paramOf(const sdurws::ird::io::IoError& e, const char* key)
{
    for (const auto& kv : e.params) {
        if (kv.first == key) {
            return kv.second;
        }
    }
    return {};
}

/// 写二进制文件（真实文件替身——parse 文件路径用例的载体）。
void writeFileBytes(const fs::path& path, const std::string& bytes)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(out.is_open()) << path.string();
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    ASSERT_FALSE(out.fail()) << "测试文件写入失败：" << path.string();
}

/// 读全文件字节（写出生效/字节一致断言用）。
std::string readFileBytes(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        ADD_FAILURE() << "无法打开被测文件：" << path.string();
        return {};
    }
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

/// 常用测试 profile：根对象，键 a（必填整数）、name（选填字符串）、
/// ext（Preserve 扩展块——前向兼容透传，§5.9.2 未知字段行）、
/// angle（必填数值，范围 [0,360]，单位 deg——比较型 RANGE 面）。
JsonProfile makeTestProfile()
{
    JsonProfile p;
    p.profileId = "ird-test/1";
    p.supportedVersions = {1};

    // 版本字段在结构契约中显式声明（版本判定先行，但键仍属根结构——
    // 未声明键的未知键策略对其一视同仁，§5.9.2）。
    JsonProperty svProp;
    svProp.key = "schemaVersion";
    svProp.required = true;
    auto svShape = std::make_shared<JsonShape>();
    svShape->type = JsonValueType::Integer;
    svProp.shape = svShape;

    JsonProperty angleProp;
    angleProp.key = "angle";
    angleProp.required = true;
    auto angleShape = std::make_shared<JsonShape>();
    angleShape->type = JsonValueType::Number;
    angleShape->hasRange = true;
    angleShape->rangeMin = 0.0;
    angleShape->rangeMax = 360.0;
    angleShape->rangeUnit = "deg";
    angleProp.shape = angleShape;

    JsonProperty aProp;
    aProp.key = "a";
    aProp.required = true;
    auto intShape = std::make_shared<JsonShape>();
    intShape->type = JsonValueType::Integer;
    aProp.shape = intShape;

    JsonProperty nameProp;
    nameProp.key = "name";
    auto strShape = std::make_shared<JsonShape>();
    strShape->type = JsonValueType::String;
    nameProp.shape = strShape;

    JsonProperty extProp;
    extProp.key = "ext";
    extProp.preserve = true;   // Preserve 子树：任意结构透传（扩展块）

    p.rootShape.type = JsonValueType::Object;
    p.rootShape.properties = {svProp, angleProp, aProp, nameProp, extProp};
    p.unknownKeyPolicy = sdurws::ird::io::JsonUnknownKeyPolicy::Reject;
    return p;
}

/// 建注册表＋注册测试 profile（返回共享注册表——reader 工厂注入形态）。
std::shared_ptr<const JsonProfileRegistry> makeRegistryWithTestProfile()
{
    auto reg = std::make_shared<JsonProfileRegistry>();
    reg->registerProfile(makeTestProfile());
    return reg;
}

/// 用例自持临时目录（CsvTest 同款形态）。
class IoJsonTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        m_dir = fs::temp_directory_path() / "ird_wp11_t05_json"
                / (std::string(info->name()) + "_" + std::to_string(std::random_device{}()));
        std::error_code ec;
        fs::remove_all(m_dir, ec);
        fs::create_directories(m_dir, ec);
        ASSERT_FALSE(ec) << "临时目录创建失败";
    }

    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(m_dir, ec);
        EXPECT_FALSE(ec) << "临时目录清理失败";
    }

    fs::path m_dir;   ///< 用例临时根
};

} // namespace

// =====================================================================
// V07 SchemaUnknownFuture（acceptance 1——NFR-DEP-04、PM-06、REQ-12）
// =====================================================================

/**
 * 未知字段拒（Preserve 名单外）：未声明键触发 IO-FORMAT-JSON-UNKNOWN，
 * params 定位 path/row/column（V07 观测点"错误码＋JSON 路径＋行列区间"
 * ——NFR-DEP-04"schema 演进必须走版本升级，不允许静默吞字段"）。
 */
TEST_F(IoJsonTest, SchemaUnknownKeyRejectedWithLocation)
{
    auto reader = makeStructuredDataReader(makeRegistryWithTestProfile());
    JsonReadOptions opt;
    const IoString profileId = "ird-test/1";
    opt.profileId = &profileId;
    // "bogus" 未在 profile 声明——位于第 3 行第 5 列（含缩进的字节列）。
    const IoResult<JsonDocument> r =
        reader->parseBytes("{\n  \"schemaVersion\": 1,\n  \"bogus\": true\n}", opt, nullptr, nullptr);
    ASSERT_FALSE(r) << "未知字段必须拒绝（不允许静默吞——NFR-DEP-04）";
    EXPECT_EQ(r.error.code, IoErrorCode::FormatJsonUnknown);
    EXPECT_EQ(paramOf(r.error, "path"), "$.bogus") << "JSON 路径定位";
    EXPECT_EQ(paramOf(r.error, "row"), "3") << "行列区间（首行首列）";
    EXPECT_FALSE(paramOf(r.error, "column").empty());
}

/**
 * Preserve 名单内透传：扩展块子树带任意未声明结构仍通过（§5.9.2 未知
 * 字段行"profile 可对指定子树声明 Preserve——前向兼容的扩展块"）。
 */
TEST_F(IoJsonTest, PreserveSubtreePassedThrough)
{
    auto reader = makeStructuredDataReader(makeRegistryWithTestProfile());
    JsonReadOptions opt;
    const IoString profileId = "ird-test/1";
    opt.profileId = &profileId;
    // ext 子树内含任意未来字段（futureKey/嵌套对象）——透传不校验。
    const char* doc =
        "{\"schemaVersion\":1,\"a\":7,\"angle\":90,\"ext\":{\"futureKey\":1,"
        "\"nested\":{\"whatever\":[1,2,3]}}}";
    const IoResult<JsonDocument> r = reader->parseBytes(doc, opt, nullptr, nullptr);
    ASSERT_TRUE(r) << "Preserve 扩展块必须透传：" << r.error.detail;
    EXPECT_EQ(r.value.root.findMember("ext")->findMember("futureKey")->integerValue, 1);
}

/**
 * schemaVersion=999 未来版本只读拒绝＋升级指引数据（PM-06 执行侧支撑
 * ——params 携带文件版本/支持区间；detail 注明升级器归 project）。
 */
TEST_F(IoJsonTest, SchemaVersion999FutureRejectWithUpgradeData)
{
    auto reader = makeStructuredDataReader(makeRegistryWithTestProfile());
    JsonReadOptions opt;
    const IoString profileId = "ird-test/1";
    opt.profileId = &profileId;
    const IoResult<JsonDocument> r =
        reader->parseBytes("{\"schemaVersion\":999,\"a\":1,\"angle\":0}", opt, nullptr, nullptr);
    ASSERT_FALSE(r) << "未来版本必须只读拒绝（PM-06）";
    EXPECT_EQ(r.error.code, IoErrorCode::FormatJsonVersionFuture);
    EXPECT_EQ(paramOf(r.error, "file-version"), "999") << "升级指引数据：文件版本";
    EXPECT_EQ(paramOf(r.error, "supported"), "1..1") << "升级指引数据：支持区间";
    EXPECT_NE(r.error.detail.find("project"), std::string::npos)
        << "升级指引数据：升级器归属（project）注记";
    // 失败＝无部分 DOM 外泄（§9.5 后置——value 默认构造）。
    EXPECT_TRUE(r.value.root.isNull());
}

/**
 * 旧版本拒绝（< 支持下限 → IO-FORMAT-VERSION-LEGACY——§5.9.2 同口径）。
 */
TEST_F(IoJsonTest, SchemaVersionLegacyReject)
{
    auto reader = makeStructuredDataReader(makeRegistryWithTestProfile());
    JsonReadOptions opt;
    const IoString profileId = "ird-test/1";
    opt.profileId = &profileId;
    const IoResult<JsonDocument> r =
        reader->parseBytes("{\"schemaVersion\":0,\"a\":1,\"angle\":0}", opt, nullptr, nullptr);
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error.code, IoErrorCode::FormatJsonVersionLegacy);
    EXPECT_EQ(paramOf(r.error, "file-version"), "0");
}

/**
 * 版本字段缺失/类型错各归其码（§5.9.2：缺省＝VERSION-MISSING、类型错
 * ＝VERSION-TYPE）；版本判定先于一切 schema 校验——未知字段＋未来版本
 * 并存时报 FUTURE 而非 UNKNOWN（顺序硬约束的直接证据）。
 */
TEST_F(IoJsonTest, VersionGateMissingTypeAndPrecedesSchema)
{
    auto reader = makeStructuredDataReader(makeRegistryWithTestProfile());
    JsonReadOptions opt;
    const IoString profileId = "ird-test/1";
    opt.profileId = &profileId;
    // 缺失：非对象根与对象无版本键两种形态（§5.9.2 版本字段行）。
    IoResult<JsonDocument> r = reader->parseBytes("[1,2]", opt, nullptr, nullptr);
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error.code, IoErrorCode::FormatJsonVersionMissing);
    r = reader->parseBytes("{\"a\":1}", opt, nullptr, nullptr);
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error.code, IoErrorCode::FormatJsonVersionMissing);
    // 类型错：schemaVersion 为字符串。
    r = reader->parseBytes("{\"schemaVersion\":\"1\",\"a\":1}", opt, nullptr, nullptr);
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error.code, IoErrorCode::FormatJsonVersionType);
    // 顺序：未知字段＋未来版本并存 → FUTURE（版本判定在前——§5.9.2）。
    r = reader->parseBytes("{\"schemaVersion\":999,\"a\":1,\"unknownKey\":true}", opt, nullptr,
                           nullptr);
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error.code, IoErrorCode::FormatJsonVersionFuture)
        << "版本判定必须先于 schema 校验";
}

/**
 * 重复键拒绝＋定位（§5.9.1 重复键行：同层同名键第二次出现即拒——静默
 * 覆盖即数据丢失；V07"重复键定位"观测点）。嵌套层同层语义（外层同名
 * 不算重复）。
 */
TEST_F(IoJsonTest, DuplicateKeyRejectedWithLocationAndNestingScoping)
{
    auto reader = makeStructuredDataReader(nullptr);   // 仅语法/安全层（无 profile）
    JsonReadOptions opt;   // profileId=null——manifest 等内部件口径
    // 顶层重复：第二个 "a" 在第 1 行第 8 字节列起（{=1、"a"=2..4、:=5、
    // 1=6、,=7、第二个 "a" 首字节=8）。
    IoResult<JsonDocument> r = reader->parseBytes("{\"a\":1,\"a\":2}", opt, nullptr, nullptr);
    ASSERT_FALSE(r) << "重复键必须拒绝（不后者覆盖——§5.9.1）";
    EXPECT_EQ(r.error.code, IoErrorCode::FormatJsonDupKey);
    EXPECT_EQ(paramOf(r.error, "path"), "$") << "定位到所属对象路径";
    EXPECT_EQ(paramOf(r.error, "row"), "1");
    EXPECT_EQ(paramOf(r.error, "column"), "8") << "第二次出现的键 token 列定位";
    // 嵌套层重复：路径 $.o（同层判定）；外层 "k" 出现两次与内层无关。
    r = reader->parseBytes("{\"o\":{\"x\":1,\"x\":2}}", opt, nullptr, nullptr);
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error.code, IoErrorCode::FormatJsonDupKey);
    EXPECT_EQ(paramOf(r.error, "path"), "$.o");
    // \u 转义不改变键身份："\u0061" 与 "a" 是同名键（解码后比对）。
    r = reader->parseBytes("{\"a\":1,\"\\u0061\":2}", opt, nullptr, nullptr);
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error.code, IoErrorCode::FormatJsonDupKey) << "键身份按解码后内容判定";
}

/**
 * 缺必填定位（IO-FORMAT-JSON-REQUIRED，定位到对象＋键名——io 不注入
 * 默认值，§5.9.2 必填/默认值行）。
 */
TEST_F(IoJsonTest, RequiredMissingLocatedAndNoDefaultInjection)
{
    auto reader = makeStructuredDataReader(makeRegistryWithTestProfile());
    JsonReadOptions opt;
    const IoString profileId = "ird-test/1";
    opt.profileId = &profileId;
    // 缺必填 "a"（angle 在）——REQUIRED＋key 参数。
    const IoResult<JsonDocument> r =
        reader->parseBytes("{\"schemaVersion\":1,\"angle\":10}", opt, nullptr, nullptr);
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error.code, IoErrorCode::FormatJsonRequired);
    EXPECT_EQ(paramOf(r.error, "key"), "a") << "缺失键名定位";
    EXPECT_EQ(paramOf(r.error, "path"), "$") << "定位到所属对象";
}

/**
 * 类型不符与范围越界（§5.9.2 类型/范围行）：TYPE 带 expected/actual；
 * RANGE 比较型三要素 actual/limit/unit（比较型：实际/期望/单位）。
 */
TEST_F(IoJsonTest, TypeErrorAndComparativeRange)
{
    auto reader = makeStructuredDataReader(makeRegistryWithTestProfile());
    JsonReadOptions opt;
    const IoString profileId = "ird-test/1";
    opt.profileId = &profileId;
    // 类型：a 声明 Integer，给 string。
    IoResult<JsonDocument> r =
        reader->parseBytes("{\"schemaVersion\":1,\"a\":\"text\",\"angle\":0}", opt, nullptr,
                           nullptr);
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error.code, IoErrorCode::FormatJsonType);
    EXPECT_EQ(paramOf(r.error, "path"), "$.a");
    EXPECT_EQ(paramOf(r.error, "expected"), "integer");
    EXPECT_EQ(paramOf(r.error, "actual"), "string");
    // 范围：angle 声明 [0,360]（deg），给 500——比较型三要素。
    r = reader->parseBytes("{\"schemaVersion\":1,\"a\":1,\"angle\":500}", opt, nullptr, nullptr);
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error.code, IoErrorCode::FormatJsonRange);
    EXPECT_EQ(paramOf(r.error, "path"), "$.angle");
    EXPECT_EQ(paramOf(r.error, "actual"), "500");
    EXPECT_EQ(paramOf(r.error, "limit"), "[0,360]");
    EXPECT_EQ(paramOf(r.error, "unit"), "deg");
}

/**
 * profileId 未注册＝IO-FORMAT-INTERNAL（§9.5 前置条件行"装配期防漏"
 * ——调用方装配缺陷走 Dev 级码，不进用户文案）。
 */
TEST_F(IoJsonTest, UnregisteredProfileIdMapsToInternal)
{
    auto reader = makeStructuredDataReader(makeRegistryWithTestProfile());
    JsonReadOptions opt;
    const IoString profileId = "not-registered/1";
    opt.profileId = &profileId;
    const IoResult<JsonDocument> r =
        reader->parseBytes("{\"schemaVersion\":1}", opt, nullptr, nullptr);
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error.code, IoErrorCode::FormatInternal);
}

// =====================================================================
// V08 NaNInfLimits（acceptance 2——NFR-SEC-02 预算/数值限制）
// =====================================================================

/**
 * NaN/Infinity/-Infinity 语法层拒绝（§5.9.1 NaN 行——非法 JSON，码面
 * IO-FORMAT-JSON-NUMBER；受限读写器不提供非有限数值承载形态）。
 */
TEST_F(IoJsonTest, NanInfLiteralsRejectedAsNumber)
{
    auto reader = makeStructuredDataReader(nullptr);
    JsonReadOptions opt;
    for (const char* doc : {"{\"x\":NaN}", "{\"x\":Infinity}", "{\"x\":-Infinity}"}) {
        const IoResult<JsonDocument> r = reader->parseBytes(doc, opt, nullptr, nullptr);
        ASSERT_FALSE(r) << doc;
        EXPECT_EQ(r.error.code, IoErrorCode::FormatJsonNumber) << doc;
        EXPECT_EQ(errorCodeToken(r.error.code), "IO-FORMAT-JSON-NUMBER");
    }
    // 写出侧同口径（§5.9.1 写出侧 isfinite 断言）：程序化构造的非有限
    // Real 拒绝写出（防 DOM 回渗——解析层不可能产生该节点）。
    JsonDocument d;
    d.root.type = JsonValue::Type::Object;
    JsonValue v;
    v.type = JsonValue::Type::Real;
    v.realValue = std::nan("");
    d.root.members.push_back({"x", {}, v});
    auto writer = makeJsonWriter();
    IoString out;
    const IoResult<void> wr =
        writer->write(sdurws::ird::io::JsonOutputTarget::memory(out), d, JsonWriteOptions{});
    ASSERT_FALSE(wr);
    EXPECT_EQ(wr.error.code, IoErrorCode::FormatJsonNumber) << "写出侧 isfinite 断言";
}

/**
 * 1e999 溢出拒绝（IO-FORMAT-JSON-NUMBER——§5.9.1 NaN 行"也拒 1e999 溢
 * 出"）；对照：下溢 1e-999 接受为 0（IEEE754 最近值语义——实现注）。
 */
TEST_F(IoJsonTest, NumberOverflowRejectedUnderflowAccepted)
{
    auto reader = makeStructuredDataReader(nullptr);
    JsonReadOptions opt;
    IoResult<JsonDocument> r = reader->parseBytes("{\"x\":1e999}", opt, nullptr, nullptr);
    ASSERT_FALSE(r) << "1e999 溢出必须拒绝（V08）";
    EXPECT_EQ(r.error.code, IoErrorCode::FormatJsonNumber);
    // 下溢：1e-999 → 0（接受，不拒绝）。
    r = reader->parseBytes("{\"x\":1e-999}", opt, nullptr, nullptr);
    ASSERT_TRUE(r) << "下溢按 IEEE754 最近值接受：" << r.error.detail;
    EXPECT_EQ(r.value.root.findMember("x")->realValue, 0.0);
    // 合法大数正常解析（double 范围内）。
    r = reader->parseBytes("{\"x\":1e308}", opt, nullptr, nullptr);
    ASSERT_TRUE(r);
    EXPECT_DOUBLE_EQ(r.value.root.findMember("x")->realValue, 1e308);
}

/**
 * 深嵌套触发 IO-SEC-BUDGET-JSON-DEPTH（§5.9.1 嵌套深度行——比较型三
 * 要素 actual/limit/unit=levels；默认限额 64 层——§4.5.1 表）。
 */
TEST_F(IoJsonTest, DeepNestingTriggersDepthBudget)
{
    auto reader = makeStructuredDataReader(nullptr);
    JsonReadOptions opt;
    // 100 层嵌套数组 > 默认 64——第 65 层递归前拒绝（先比后递归——栈安全）。
    std::string doc(100, '[');
    doc += std::string(100, ']');
    const IoResult<JsonDocument> r = reader->parseBytes(doc, opt, nullptr, nullptr);
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error.code, IoErrorCode::SecBudgetJsonDepth);
    EXPECT_EQ(paramOf(r.error, "actual"), "65") << "触发层＝限额+1（先检查后递归）";
    EXPECT_EQ(paramOf(r.error, "limit"), "64") << "§4.5.1 JsonDepth 默认值";
    EXPECT_EQ(paramOf(r.error, "unit"), "levels");
}

/**
 * 超长字符串触发 IO-SEC-BUDGET-JSON-STRING（§5.9.1 大字符串行——
 * actual/limit/unit=chars；默认 16 Mi 码点——§4.5.1 表）。
 */
TEST_F(IoJsonTest, OversizedStringTriggersStringBudget)
{
    auto reader = makeStructuredDataReader(nullptr);
    JsonReadOptions opt;
    // 16 Mi＋1 个 ASCII 字符的字符串（默认限额 16 Mi 码点——越界 1 码点
    // 即拒；测试内存峰值约 16 MiB×2，可接受）。
    const std::uint64_t limit = 16u * 1024 * 1024;
    std::string doc = "{\"x\":\"";
    doc.append(static_cast<std::size_t>(limit + 1), 'a');
    doc += "\"}";
    const IoResult<JsonDocument> r = reader->parseBytes(doc, opt, nullptr, nullptr);
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error.code, IoErrorCode::SecBudgetJsonString);
    EXPECT_EQ(paramOf(r.error, "limit"), std::to_string(limit));
    EXPECT_EQ(paramOf(r.error, "unit"), "chars");
}

/**
 * 文档大小预算经 BudgetGuard 记账通道触发（§5.9.1 文档大小行——
 * IO-SEC-BUDGET-JSON 三要素；调用方 tighten 的 scope 经等价增补的
 * budgetScope 参数传导——记账按调用方规格生效）。
 */
TEST_F(IoJsonTest, DocBytesBudgetChargedThroughGuard)
{
    auto reader = makeStructuredDataReader(nullptr);
    JsonReadOptions opt;
    auto guard = makeBudgetGuard();
    ASSERT_NE(guard, nullptr);
    // 收紧 JsonDocBytes 到 16 字节：18 字节文档超限（外部 scope——等价
    // 增补的语义落点：调用方规格约束读取会话，§4.5.2"只能收紧"）。
    auto spec = BudgetSpec::productDefault();
    spec.tighten(BudgetDimension::JsonDocBytes, 16);
    const auto scope = guard->openScope(spec);
    ASSERT_TRUE(scope);
    IoResult<JsonDocument> r =
        reader->parseBytes("{\"x\":1,\"y\":223344}", opt, guard.get(), nullptr, scope.value);
    ASSERT_FALSE(r) << "超预算文档必须被拒（记账通道——非旁路比较）";
    EXPECT_EQ(r.error.code, IoErrorCode::SecBudgetJson);
    EXPECT_EQ(paramOf(r.error, "actual"), "18") << "比较型三要素：实际字节数";
    EXPECT_EQ(paramOf(r.error, "limit"), "16") << "比较型三要素：生效限额（调用方 tighten）";
    EXPECT_EQ(paramOf(r.error, "unit"), "bytes");
    // 对照：预算内文档经同一 scope 通过。
    const IoResult<JsonDocument> ok =
        reader->parseBytes("{\"x\":1}", opt, guard.get(), nullptr, scope.value);
    ASSERT_TRUE(ok) << "预算内文档放行：" << ok.error.detail;
}

/**
 * 超范围整数按原文 String 透传＋报告计数（§5.9.1 数值行——不静默截断；
 * "＋提示"面＝report.outOfRangeIntegers）。
 */
TEST_F(IoJsonTest, OutOfRangeIntegerPassthroughWithStringAndHint)
{
    auto reader = makeStructuredDataReader(nullptr);
    JsonReadOptions opt;
    const char* big = "123456789012345678901234567890";   // > 2^63-1
    const IoResult<JsonDocument> r =
        reader->parseBytes(std::string("{\"x\":") + big + "}", opt, nullptr, nullptr);
    ASSERT_TRUE(r) << "超范围整数透传不是错误（§5.9.1 数值行）：" << r.error.detail;
    const JsonValue* x = r.value.root.findMember("x");
    EXPECT_TRUE(x->isString()) << "按原文保留为 String 透传";
    EXPECT_EQ(x->stringValue, big) << "原文逐字节保留";
    EXPECT_EQ(r.value.report.outOfRangeIntegers, 1u) << "提示计数";
}

// =====================================================================
// 编码与语法层（§5.9.1 编码行——仅 UTF-8；语法违例→表尾追加码 SYNTAX）
// =====================================================================

/**
 * 编码门：UTF-8 BOM 容忍剥离（report.bomStripped）；UTF-16 BOM 与非法
 * UTF-8 序列稳定拒绝（IO-FORMAT-JSON-ENCODING＋行列定位——不猜转码）。
 */
TEST_F(IoJsonTest, EncodingGateBomStripAndStableReject)
{
    auto reader = makeStructuredDataReader(nullptr);
    JsonReadOptions opt;
    // BOM 剥离（容忍）。
    IoResult<JsonDocument> r = reader->parseBytes("\xEF\xBB\xBF{\"x\":1}", opt, nullptr, nullptr);
    ASSERT_TRUE(r) << "UTF-8 BOM 容忍剥离：" << r.error.detail;
    EXPECT_TRUE(r.value.report.bomStripped);
    // UTF-16LE BOM 拒绝（仅 UTF-8——与 CSV 的 UTF-16 通道不同）。
    r = reader->parseBytes(std::string("\xFF\xFE") + "{\"x\":1}", opt, nullptr, nullptr);
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error.code, IoErrorCode::FormatJsonEncoding);
    // 非法 UTF-8 序列（0xE9 悬空首字节）拒绝＋行列定位。
    r = reader->parseBytes("{\"x\":\"\xE9\"}", opt, nullptr, nullptr);
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error.code, IoErrorCode::FormatJsonEncoding);
    EXPECT_EQ(paramOf(r.error, "row"), "1");
    EXPECT_FALSE(paramOf(r.error, "column").empty());
}

/**
 * 语法违例归 IO-FORMAT-JSON-SYNTAX（表尾追加码——DTB §5.4 单元卡增量
 * 修订补登的 §9.12 缺口；数值字面量语法非法仍归 NUMBER——码面分工）。
 */
TEST_F(IoJsonTest, SyntaxViolationsUseAppendedSyntaxCode)
{
    auto reader = makeStructuredDataReader(nullptr);
    JsonReadOptions opt;
    // 截断文档/缺失分隔符/尾随内容/未知记号——结构类语法违例。
    for (const char* doc : {"{\"x\":1", "{\"x\" 1}", "{\"x\":1} garbage", "#", "{\"x\":tru}"}) {
        const IoResult<JsonDocument> r = reader->parseBytes(doc, opt, nullptr, nullptr);
        ASSERT_FALSE(r) << doc;
        EXPECT_EQ(r.error.code, IoErrorCode::FormatJsonSyntax) << doc;
        EXPECT_EQ(errorCodeToken(r.error.code), "IO-FORMAT-JSON-SYNTAX") << doc;
    }
    // 数值字面量语法非法仍归 NUMBER（§5.9.1 数值行——前导零/孤立 '.'）。
    for (const char* doc : {"{\"x\":01}", "{\"x\":.5}", "{\"x\":1.}"}) {
        const IoResult<JsonDocument> r = reader->parseBytes(doc, opt, nullptr, nullptr);
        ASSERT_FALSE(r) << doc;
        EXPECT_EQ(r.error.code, IoErrorCode::FormatJsonNumber) << doc;
    }
}

/**
 * 取消检查点：置位令牌在解析窗口命中 → IO-CANCELLED（状态非错误，
 * UX-03——§9.5 取消行为行）。
 */
TEST_F(IoJsonTest, CancelTokenReturnsStateNotError)
{
    auto reader = makeStructuredDataReader(nullptr);
    JsonReadOptions opt;
    // 恒取消令牌（测试替身——幂等真值，§9.0 契约）。
    struct Cancelled : sdurws::ird::io::IoCancelToken {
        bool isCancelled() const override { return true; }
    } token;
    const IoResult<JsonDocument> r = reader->parseBytes("{\"x\":1}", opt, nullptr, &token);
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error.code, IoErrorCode::Cancelled) << "取消＝状态（不落诊断——UX-03）";
}

// =====================================================================
// canonical 字节一致与内容身份（acceptance 3——§5.9.3、SA-12/NFR-MNT-03）
// =====================================================================

/**
 * 同语义 JSON 同字节（§5.9.3 canonical 编码——键序/空白/数值书写差异
 * 全部规范化）：两份语义相同、写法不同的文档 canonical 字节与摘要全等。
 */
TEST_F(IoJsonTest, CanonicalSameSemanticsSameBytes)
{
    auto reader = makeStructuredDataReader(nullptr);
    JsonReadOptions opt;
    // 差异面：键序（b/a 互换）、空白、数值书写（1 vs 1.0、5e-1 vs 0.5
    // ——数值语义相同）。
    const IoResult<JsonDocument> d1 =
        reader->parseBytes("{\n  \"b\" : 2,\n  \"a\" : 1\n}", opt, nullptr, nullptr);
    const IoResult<JsonDocument> d2 =
        reader->parseBytes("{\"a\":1.0,\"b\":2,\"c\":5e-1}", opt, nullptr, nullptr);
    const IoResult<JsonDocument> d3 =
        reader->parseBytes("{\"a\":1,\"b\":2,\"c\":0.5}", opt, nullptr, nullptr);
    ASSERT_TRUE(d1);
    ASSERT_TRUE(d2);
    ASSERT_TRUE(d3);
    const JsonWriteOptions wopt;   // 无 profile——字典序
    const IoResult<IoString> c1 = canonicalizeJson(d1.value, wopt);
    const IoResult<IoString> c2 = canonicalizeJson(d2.value, wopt);
    const IoResult<IoString> c3 = canonicalizeJson(d3.value, wopt);
    ASSERT_TRUE(c1);
    ASSERT_TRUE(c2);
    ASSERT_TRUE(c3);
    // d1 缺 c 键——补齐后三份同语义（重新解析文档全集）。
    const IoResult<JsonDocument> d1b =
        reader->parseBytes("{\"b\":2,\"a\":1,\"c\":5E-1}", opt, nullptr, nullptr);
    ASSERT_TRUE(d1b);
    const IoResult<IoString> c1b = canonicalizeJson(d1b.value, wopt);
    ASSERT_TRUE(c1b);
    EXPECT_EQ(c1b.value, c2.value) << "同语义文档 canonical 字节相同";
    EXPECT_EQ(c2.value, c3.value) << "数值书写差异规范化（5e-1 与 0.5 同字节）";
    // 内容身份同字节同摘要（§5.9.3——内容身份对 canonical 字节计算）。
    const IoResult<sdurws::ird::core::Digest256> g1 = digestCanonicalJson(d1b.value, wopt);
    const IoResult<sdurws::ird::core::Digest256> g2 = digestCanonicalJson(d2.value, wopt);
    ASSERT_TRUE(g1);
    ASSERT_TRUE(g2);
    EXPECT_EQ(g1.value, g2.value) << "同语义同摘要";
}

/**
 * canonical 确定性＋摘要算法唯一（SA-12/NFR-MNT-03）：同 DOM 两次写出
 * 字节相同；digestCanonicalJson 恒等于对 canonical 字节手工经 core
 * ContentDigester 计算的摘要——摘要算法唯一性（SHA-256）的通道证据。
 */
TEST_F(IoJsonTest, CanonicalDeterministicAndDigestViaCoreDigester)
{
    auto reader = makeStructuredDataReader(nullptr);
    JsonReadOptions opt;
    const IoResult<JsonDocument> d =
        reader->parseBytes("{\"k\":[1,2.5,\"s中文\",true,null],\"m\":{\"z\":0,\"y\":-1}}", opt,
                           nullptr, nullptr);
    ASSERT_TRUE(d);
    const JsonWriteOptions wopt;
    const IoResult<IoString> c1 = canonicalizeJson(d.value, wopt);
    const IoResult<IoString> c2 = canonicalizeJson(d.value, wopt);
    ASSERT_TRUE(c1);
    ASSERT_TRUE(c2);
    EXPECT_EQ(c1.value, c2.value) << "同 DOM 两次 canonical 字节相同（NFR-COR-01）";
    // 版面钉住：2 空格缩进＋LF（§5.9.3 canonical 编码行）。
    EXPECT_NE(c1.value.find("\n  \""), std::string::npos) << "2 空格缩进";
    EXPECT_EQ(c1.value.find("\r"), std::string::npos) << "LF 行尾（无 CR）";
    // 摘要＝core ContentDigester（SHA-256 唯一算法——无第二摘要实现）。
    const IoResult<sdurws::ird::core::Digest256> g = digestCanonicalJson(d.value, wopt);
    ASSERT_TRUE(g);
    ContentDigester manual;   // 手工对同一 canonical 字节复算
    manual.update(c1.value.data(), c1.value.size());
    EXPECT_EQ(g.value, manual.finalize()) << "JSON 通道摘要唯一入口＝core ContentDigester";
}

/**
 * 键序来源二态（§5.9.3 写行）：profile 声明序在前；无 profile 按字典序。
 */
TEST_F(IoJsonTest, CanonicalKeyOrderProfileDeclaredVsLexicographic)
{
    auto reader = makeStructuredDataReader(nullptr);
    JsonReadOptions opt;
    const IoResult<JsonDocument> d =
        reader->parseBytes("{\"bb\":1,\"aa\":2,\"cc\":3}", opt, nullptr, nullptr);
    ASSERT_TRUE(d);
    // 无 profile：字典序 aa < bb < cc。
    const IoResult<IoString> lexi = canonicalizeJson(d.value, JsonWriteOptions{});
    ASSERT_TRUE(lexi);
    EXPECT_LT(lexi.value.find("\"aa\""), lexi.value.find("\"bb\""));
    EXPECT_LT(lexi.value.find("\"bb\""), lexi.value.find("\"cc\""));
    // 有 profile：声明序 cc、aa、bb（声明表外键文件序跟后——本例无）。
    JsonProfile p;
    p.profileId = "order/1";
    p.supportedVersions = {1};
    JsonProperty p1;
    p1.key = "cc";
    JsonProperty p2;
    p2.key = "aa";
    JsonProperty p3;
    p3.key = "bb";
    p.rootShape.type = JsonValueType::Object;
    p.rootShape.properties = {p1, p2, p3};
    JsonWriteOptions wopt;
    wopt.profile = &p;
    const IoResult<IoString> decl = canonicalizeJson(d.value, wopt);
    ASSERT_TRUE(decl);
    EXPECT_LT(decl.value.find("\"cc\""), decl.value.find("\"aa\"")) << "声明序优先";
    EXPECT_LT(decl.value.find("\"aa\""), decl.value.find("\"bb\""));
}

/**
 * 写出器通道：文件目标原子替换（§9.5 副作用行"原子输出"——成功＝目
 * 标就位；替换后内容可再解析且 DOM 语义一致——写读 roundtrip）。
 */
TEST_F(IoJsonTest, WriterFileTargetAtomicReplaceAndRoundtrip)
{
    auto reader = makeStructuredDataReader(nullptr);
    JsonReadOptions opt;
    const IoResult<JsonDocument> d =
        reader->parseBytes("{\"msg\":\"中文✓\",\"n\":-3.5,\"arr\":[1,2]}", opt, nullptr, nullptr);
    ASSERT_TRUE(d);
    const fs::path target = m_dir / "out.json";
    // 预置旧内容（替换语义——先前输出被完整替换）。
    writeFileBytes(target, "OLD-CONTENT");
    auto writer = makeJsonWriter();
    const IoResult<void> wr = writer->write(sdurws::ird::io::JsonOutputTarget::file(target),
                                            d.value, JsonWriteOptions{});
    ASSERT_TRUE(wr) << "文件写出失败：" << wr.error.detail;
    const std::string bytes = readFileBytes(target);
    EXPECT_EQ(bytes.find("OLD-CONTENT"), std::string::npos) << "旧内容被原子替换";
    // roundtrip：再解析 → 语义一致（数值/字符串/数组逐字段）。
    const IoResult<JsonDocument> back = reader->parseBytes(bytes, opt, nullptr, nullptr);
    ASSERT_TRUE(back);
    EXPECT_EQ(back.value.root.findMember("msg")->stringValue, "中文✓");
    EXPECT_DOUBLE_EQ(back.value.root.findMember("n")->realValue, -3.5);
    EXPECT_EQ(back.value.root.findMember("arr")->items.size(), 2u);
    // 暂存零残留（§4.6 失败恢复语义的常态面——成功后无 .ird-json-tmp）。
    EXPECT_FALSE(fs::exists(fs::path(target.string() + ".ird-json-tmp")));
}

// =====================================================================
// 注册表契约（C-6 注册协议——装配期注册、重复/非法装配 fail-fast）
// =====================================================================

/**
 * 注册表装配契约：重复注册抛 logic_error、空 id/空版本表/非升序版本表
 * 抛 invalid_argument（JsonProfileRegistry 注——装配期编程错误 fail-fast，
 * BudgetSpec 同款口径）。
 */
TEST_F(IoJsonTest, RegistryContractFailFastOnAssemblyErrors)
{
    JsonProfileRegistry reg;
    reg.registerProfile(makeTestProfile());
    // 重复 id。
    EXPECT_THROW(reg.registerProfile(makeTestProfile()), std::logic_error);
    // 空 id。
    JsonProfile emptyId = makeTestProfile();
    emptyId.profileId.clear();
    EXPECT_THROW(reg.registerProfile(std::move(emptyId)), std::invalid_argument);
    // 空版本表。
    JsonProfile noVer = makeTestProfile();
    noVer.profileId = "other/1";
    noVer.supportedVersions.clear();
    EXPECT_THROW(reg.registerProfile(std::move(noVer)), std::invalid_argument);
    // 非升序版本表。
    JsonProfile unsorted = makeTestProfile();
    unsorted.profileId = "other/2";
    unsorted.supportedVersions = {2, 1};
    EXPECT_THROW(reg.registerProfile(std::move(unsorted)), std::invalid_argument);
    EXPECT_EQ(reg.size(), 1u) << "全部非法注册未入库";
}

/**
 * 版本表多版本集：{1,2} 在册版本放行、界内空洞按 FUTURE（JsonProfile
 * 类注——未登记的中间版本语义不猜测）。
 */
TEST_F(IoJsonTest, VersionSetMembershipAndHoleAsFuture)
{
    JsonProfile p = makeTestProfile();
    p.profileId = "multi/1";
    p.supportedVersions = {1, 3};   // 2＝界内空洞
    auto reg = std::make_shared<JsonProfileRegistry>();
    reg->registerProfile(std::move(p));
    auto reader = makeStructuredDataReader(reg);
    JsonReadOptions opt;
    const IoString profileId = "multi/1";
    opt.profileId = &profileId;
    // 在册 3 → 通过。
    IoResult<JsonDocument> r =
        reader->parseBytes("{\"schemaVersion\":3,\"a\":1,\"angle\":0}", opt, nullptr, nullptr);
    ASSERT_TRUE(r) << "在册版本放行：" << r.error.detail;
    // 界内空洞 2 → FUTURE（拒绝＋指引数据）。
    r = reader->parseBytes("{\"schemaVersion\":2,\"a\":1,\"angle\":0}", opt, nullptr, nullptr);
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error.code, IoErrorCode::FormatJsonVersionFuture);
    EXPECT_EQ(paramOf(r.error, "file-version"), "2");
    EXPECT_EQ(paramOf(r.error, "supported"), "1..3");
}

/**
 * parse 文件路径：真实文件读取＋RES-NOT-FOUND 环境错误映射（§9.5 错误
 * 类型行 IO-RES-*；stat 预检——§5.9.1 文档大小行"先 stat"）。
 */
TEST_F(IoJsonTest, ParseFileReadsRealFileAndMapsMissing)
{
    auto reader = makeStructuredDataReader(nullptr);
    JsonReadOptions opt;
    // 真实文件。
    const fs::path f = m_dir / "doc.json";
    writeFileBytes(f, "{\"x\":42}");
    const IoResult<JsonDocument> r = reader->parse(f, opt, nullptr, nullptr);
    ASSERT_TRUE(r) << r.error.detail;
    EXPECT_EQ(r.value.root.findMember("x")->integerValue, 42);
    // 不存在路径。
    const IoResult<JsonDocument> missing = reader->parse(m_dir / "nope.json", opt, nullptr, nullptr);
    ASSERT_FALSE(missing);
    EXPECT_EQ(missing.error.code, IoErrorCode::ResNotFound);
}

/**
 * 转义与 Unicode 往返（RFC 8259 string 语义——\" \\ \n \t \uXXXX 含
 * 代理对/中文直通；悬空代理拒绝——不可交换文本不予承载）。
 */
TEST_F(IoJsonTest, StringEscapesRoundtripAndSurrogatePolicy)
{
    auto reader = makeStructuredDataReader(nullptr);
    JsonReadOptions opt;
    // 转义解码：\uXXXX（含中文）、代理对（𝄞 U+1D11E）、短转义。
    const IoResult<JsonDocument> r = reader->parseBytes(
        "{\"e\":\"a\\nb\\tc\\\"d\\\\e\\u4E2D\\uD834\\uDD1E\"}", opt, nullptr, nullptr);
    ASSERT_TRUE(r) << r.error.detail;
    const JsonValue* e = r.value.root.findMember("e");
    EXPECT_EQ(e->stringValue, "a\nb\tc\"d\\e中\xF0\x9D\x84\x9E") << "转义/代理对解码正确";
    // 悬空高代理拒绝（语法层——不可交换文本）。
    const IoResult<JsonDocument> lone =
        reader->parseBytes("{\"e\":\"\\uD834\"}", opt, nullptr, nullptr);
    ASSERT_FALSE(lone);
    EXPECT_EQ(lone.error.code, IoErrorCode::FormatJsonSyntax);
}
