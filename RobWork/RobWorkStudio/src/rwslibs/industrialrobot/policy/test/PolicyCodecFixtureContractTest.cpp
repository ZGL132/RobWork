/**
 * @file   PolicyCodecFixtureContractTest.cpp
 * @brief  解析/编码契约夹具数据集消费用例组（POL-T11）——
 *         testdata/golden/pol-policy-codec-fixture（contract-fixture 类）
 *         的全链装载＋双实现互证：canonical 编码逐字节对照、内容身份
 *         独立摘要对照、排序/显示单位无关性（POL-ID-2/POL-ID-3 的数据集
 *         载体）与完整性复核。
 *
 * 设计依据：
 *   - units/policy.md §11 矩阵尾注："解析/编码契约夹具数据集按 testkit
 *     manifest schema 登记（contract-fixture 类）"；§12 POL-T11 行
 *     （数据集交付面 testdata/golden/pol-*——本套件为消费端）；
 *   - units/testkit.md §4.2（manifest 五类/schema/完整性 §4.5）、§5.1
 *     （GoldenDataset::load 装载链：解析→schema→integrity→交叉校验）、
 *     §10.2 policy 行（各域黄金数据集＋生成脚本——域自负数据集责任）；
 *   - 需求 CON-05（内容身份＝缓存/切片失效判据——身份必须可由独立实现
 *     复算）、NFR-COR-01（独立实现为正确性依据首选——本夹具以"独立
 *     重实现"通道服务之）、NFR-COR-02（确定性）。
 *
 * 双实现互证口径（ev-slice-fixture 先例同款）：
 *   expected/identity.json 由 generate/make_pol_policy_fixture.mjs 按
 *   §5.3 编码布局规则**独立重实现**产出（JS 侧不复用任何产品代码）——
 *   产品 C++ 编码器（PolicyCodec）与 JS 参考编码器任一侧漂移即本套件
 *   显性失败。本文件是消费侧：装载→重建 RawPolicyInput→编码/解析→
 *   与 expected 逐字节/逐串对照。
 *
 * 模式约束：本文件纯 core＋policy 无框架依赖（PolicyInput/PolicyParsing
 * 双模式无条件编译）——冒烟＋集成两模式编译运行（数据集随 CI 跑通的
 * 双模式通道）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/policy/PolicyInput.hpp>
#include <sdurws/ird/policy/PolicyParsing.hpp>
#include <sdurws/ird/policy/PolicySet.hpp>

#include <sdurws/ird/testkit/Dataset.hpp>
#include <sdurws/ird/testkit/JsonLite.hpp>
#include <sdurws/ird/testkit/TestPaths.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace sdurws::ird::policy;

/// core/testkit 契约类型短别名（CollisionEvaluationTest 同款可读性别名）。
namespace core = sdurws::ird::core;
namespace tk = sdurws::ird::testkit;

namespace {

// =====================================================================
// 数据集定位与小工具（装载与字节/文本互转）。
// =====================================================================

/// 数据集引用（datasetId/版本与目录结构一致——testkit §4.2.2 关联约束）。
constexpr char kDatasetId[] = "pol-policy-codec-fixture";
constexpr char kDatasetVersion[] = "1.0.0";

/// 读文本文件（数据集 inputs/expected 的读取入口——二进制安全的整读）。
std::string readTextFile(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};   // 调用侧以"空串＝读失败"处理（用例内显式断言失败消息）
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

/// 字节→小写十六进制（编码对照的文本形态——与 expected JSON 串比较）。
std::string bytesToHex(const std::vector<std::uint8_t>& bytes)
{
    static const char* kDigits = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (const std::uint8_t b : bytes) {
        out.push_back(kDigits[b >> 4]);
        out.push_back(kDigits[b & 0x0F]);
    }
    return out;
}

/// 十六进制→字节（expected 串的还原；非法长度/字符＝数据集缺陷，返回空）。
std::vector<std::uint8_t> hexToBytes(const std::string& hex)
{
    const auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') { return c - '0'; }
        if (c >= 'a' && c <= 'f') { return c - 'a' + 10; }
        if (c >= 'A' && c <= 'F') { return c - 'A' + 10; }
        return -1;
    };
    std::vector<std::uint8_t> out;
    if (hex.size() % 2 != 0) {
        return out;
    }
    out.reserve(hex.size() / 2);
    for (std::size_t i = 0; i < hex.size(); i += 2) {
        const int hi = nibble(hex[i]);
        const int lo = nibble(hex[i + 1]);
        if (hi < 0 || lo < 0) {
            return {};
        }
        out.push_back(static_cast<std::uint8_t>((hi << 4) | lo));
    }
    return out;
}

/// 16 字节身份的十六进制解析（policyObjectHex/对象集合条目的还原入口）。
core::ObjectId objectIdFromHex(const std::string& hex)
{
    core::ObjectId id;
    const std::vector<std::uint8_t> bytes = hexToBytes(hex);
    if (bytes.size() == id.bytes.size()) {
        std::copy(bytes.begin(), bytes.end(), id.bytes.begin());
    }
    return id;
}

// ---- JSON 读取辅助（缺键/类型错＝数据集缺陷——测试显性失败） ----

const tk::JsonValue* requireObject(const tk::JsonValue& o, const std::string& key)
{
    const tk::JsonValue* v = o.find(key);
    EXPECT_NE(v, nullptr) << "数据集缺对象字段: " << key;
    EXPECT_TRUE(v == nullptr || v->isObject()) << "字段类型非对象: " << key;
    return v;
}

const tk::JsonValue* requireArray(const tk::JsonValue& o, const std::string& key)
{
    const tk::JsonValue* v = o.find(key);
    EXPECT_NE(v, nullptr) << "数据集缺数组字段: " << key;
    EXPECT_TRUE(v == nullptr || v->isArray()) << "字段类型非数组: " << key;
    return v;
}

std::string requireString(const tk::JsonValue& o, const std::string& key)
{
    const tk::JsonValue* v = o.find(key);
    EXPECT_NE(v, nullptr) << "数据集缺字符串字段: " << key;
    EXPECT_TRUE(v == nullptr || v->isString()) << "字段类型非字符串: " << key;
    return v == nullptr ? std::string{} : v->text;
}

double requireNumber(const tk::JsonValue& o, const std::string& key)
{
    const tk::JsonValue* v = o.find(key);
    EXPECT_NE(v, nullptr) << "数据集缺数值字段: " << key;
    EXPECT_TRUE(v == nullptr || v->isNumber()) << "字段类型非数值: " << key;
    return v == nullptr ? 0.0 : v->number;
}

bool requireBool(const tk::JsonValue& o, const std::string& key)
{
    const tk::JsonValue* v = o.find(key);
    EXPECT_NE(v, nullptr) << "数据集缺布尔字段: " << key;
    EXPECT_TRUE(v == nullptr || v->isBool()) << "字段类型非布尔: " << key;
    return v != nullptr && v->boolean;
}

// ---- 词表映射（JSON token→枚举；token 词表与解析面 §4.3 同源冻结） ----

/// 碰撞域 token→枚举（Self/Environment/Tool/Scene——§4.3 词表）。
CollisionDomain domainFromToken(const std::string& token)
{
    if (token == "Self") { return CollisionDomain::Self; }
    if (token == "Environment") { return CollisionDomain::Environment; }
    if (token == "Tool") { return CollisionDomain::Tool; }
    if (token == "Scene") { return CollisionDomain::Scene; }
    ADD_FAILURE() << "未知碰撞域 token: " << token;
    return CollisionDomain::Self;
}

/// 规则级别 token→枚举（Must/Should）。
PolicyRuleLevel levelFromToken(const std::string& token)
{
    if (token == "Should") { return PolicyRuleLevel::Should; }
    EXPECT_EQ(token, "Must") << "未知规则级别 token: " << token;
    return PolicyRuleLevel::Must;
}

/// 来源类别 token→枚举（Template/Imported/UserEdited/SystemDefault——§4.2）。
PolicyOriginKind originKindFromToken(const std::string& token)
{
    if (token == "Template") { return PolicyOriginKind::Template; }
    if (token == "Imported") { return PolicyOriginKind::Imported; }
    if (token == "SystemDefault") { return PolicyOriginKind::SystemDefault; }
    EXPECT_EQ(token, "UserEdited") << "未知来源类别 token: " << token;
    return PolicyOriginKind::UserEdited;
}

/// 作用域目标 JSON→ScopeTarget（kind 决定载荷字段——§4.3 联合契约）。
ScopeTarget scopeTargetFromJson(const tk::JsonValue& o)
{
    const std::string kind = requireString(o, "kind");
    if (kind == "Object") {
        return ScopeTarget::makeObject(objectIdFromHex(requireString(o, "objectHex")));
    }
    if (kind == "Role") {
        return ScopeTarget::makeRole(requireString(o, "roleToken"));
    }
    EXPECT_EQ(kind, "Group") << "未知作用域目标类别: " << kind;
    return ScopeTarget::makeGroup(requireString(o, "groupName"));
}

/// 对规则 JSON→PairRule（无序对按 JSON 承载序——规范化归编码器/解析器）。
PairRule pairRuleFromJson(const tk::JsonValue& o)
{
    const tk::JsonValue* first = requireObject(o, "first");
    const tk::JsonValue* second = requireObject(o, "second");
    if (first == nullptr || second == nullptr) {
        // 结构缺陷占位——让上层已触发的 EXPECT 失败保持可见（不抛出进程）。
        return PairRule::make(ScopeTarget::makeRole("RobotLink"),
                              ScopeTarget::makeRole("Tool"),
                              PolicyRuleLevel::Must, "invalid-fixture-entry");
    }
    return PairRule::make(scopeTargetFromJson(*first), scopeTargetFromJson(*second),
                          levelFromToken(requireString(o, "level")),
                          requireString(o, "reason"));
}

/// 阈值槽位 JSON→RawThresholdInput（null/缺失＝未设置——presence 语义）。
std::optional<RawThresholdInput> optionalThresholdFromJson(const tk::JsonValue& o,
                                                           const std::string& key)
{
    const tk::JsonValue* v = o.find(key);
    if (v == nullptr || v->isNull()) {
        return std::nullopt;
    }
    return RawThresholdInput{requireNumber(*v, "value"), requireString(*v, "unit")};
}

/**
 * @brief 修订闭包替身（IPolicyValidationContext 的纯 core 测试实现——
 *        本文件双模式编译，不引入 testdouble 头〔其 rw include 面仅限
 *        集成模式〕；应答表来自夹具 inputs 的 closure 节）。
 *
 * 为什么在消费测试里自带最小实现：解析⑤需要闭包应答（对象存在/角色/
 * 组定义），夹具的发布路径必须通过真实解析管线——独立重实现的身份
 * 对照才有"已发布语义"的效力。应答在同键下恒定（§9.7 确定性前提）。
 */
class FixtureValidationContext final : public IPolicyValidationContext {
public:
    /// objectExists/objectRole 应答表（键＝对象身份）。
    std::map<core::ObjectId, std::string> roles;
    /// groupDefined 应答表（显式组名→是否已定义）。
    std::map<std::string, bool> groups;

    bool objectExists(core::ObjectId object) const override
    {
        return roles.find(object) != roles.end();
    }

    std::optional<std::string> objectRole(core::ObjectId object) const override
    {
        const auto it = roles.find(object);
        return it == roles.end() ? std::nullopt : std::optional<std::string>(it->second);
    }

    bool groupDefined(std::string_view groupName) const override
    {
        const auto it = groups.find(std::string{groupName});
        return it != groups.end() && it->second;
    }
};

/**
 * @brief 从数据集 inputs JSON 重建 RawPolicyInput 与闭包替身（消费侧装配
 *        ——与 generate/make_pol_policy_fixture.mjs 的 JS 侧装配同源同语义）。
 *
 * @param root      [in] inputs/<变体>.json 的解析根
 * @param closure   [out] 闭包应答表（inputs 的 closure 节——解析⑤通过的前提）
 * @return 重建的原始策略输入（承载序＝JSON 书写序——规范化归编码器）
 */
RawPolicyInput policyInputFromJson(const tk::JsonValue& root,
                                   FixtureValidationContext& closure)
{
    RawPolicyInput in;
    in.schemaVersion = static_cast<std::uint32_t>(requireNumber(root, "schemaVersion"));
    in.policyObject = objectIdFromHex(requireString(root, "policyObjectHex"));

    // ---- 碰撞规则原始输入（§4.3 对偶——逐字段对应） ----
    const tk::JsonValue* collision = requireObject(root, "collision");
    if (collision != nullptr) {
        in.collision.enabled = requireBool(*collision, "enabled");
        if (const tk::JsonValue* domains = requireArray(*collision, "enabledDomains")) {
            for (const auto& d : domains->items) {
                in.collision.enabledDomains.push_back(domainFromToken(d.text));
            }
        }
        in.collision.safetyClearance = optionalThresholdFromJson(*collision, "safetyClearance");
        in.collision.excludeAdjacentLinksByDefault =
            requireBool(*collision, "excludeAdjacentLinksByDefault");
        if (const tk::JsonValue* rules = requireArray(*collision, "mandatoryPairs")) {
            for (const auto& r : rules->items) {
                in.collision.mandatoryPairs.push_back(pairRuleFromJson(r));
            }
        }
        if (const tk::JsonValue* rules = requireArray(*collision, "excludedPairs")) {
            for (const auto& r : rules->items) {
                in.collision.excludedPairs.push_back(pairRuleFromJson(r));
            }
        }
    }

    // ---- 关节限位/行程阈值原始输入（§4.4 对偶——槽位可缺失） ----
    const tk::JsonValue* joints = requireObject(root, "jointThresholds");
    if (joints != nullptr) {
        in.jointThresholds.nearLimitRatio =
            optionalThresholdFromJson(*joints, "nearLimitRatio");
        in.jointThresholds.conditionNumberWarning =
            optionalThresholdFromJson(*joints, "conditionNumberWarning");
        in.jointThresholds.finiteRotationTravelLimit =
            optionalThresholdFromJson(*joints, "finiteRotationTravelLimit");
        in.jointThresholds.travelLimitCheckEnabled =
            requireBool(*joints, "travelLimitCheckEnabled");
    }

    // ---- 适用范围（集合按 JSON 承载序——规范化归编码器） ----
    const tk::JsonValue* applicability = requireObject(root, "applicability");
    if (applicability != nullptr) {
        if (const tk::JsonValue* modes = requireArray(*applicability, "modes")) {
            for (const auto& m : modes->items) {
                if (m.text == "Preview") {
                    in.applicability.modes.push_back(core::EvaluationMode::Preview);
                }
                else if (m.text == "Quick") {
                    in.applicability.modes.push_back(core::EvaluationMode::Quick);
                }
                else if (m.text == "Verified") {
                    in.applicability.modes.push_back(core::EvaluationMode::Verified);
                }
                else {
                    ADD_FAILURE() << "未知模式 token: " << m.text;
                }
            }
        }
        const auto idList = [applicability](const std::string& key) {
            std::vector<core::ObjectId> out;
            if (const tk::JsonValue* arr = requireArray(*applicability, key)) {
                for (const auto& e : arr->items) {
                    out.push_back(objectIdFromHex(e.text));
                }
            }
            return out;
        };
        in.applicability.modelObjects = idList("modelObjects");
        in.applicability.taskObjects = idList("taskObjects");
        in.applicability.caseObjects = idList("caseObjects");
    }

    // ---- 数值契约锚与管理审计块（锚入身份；origin/注记不入身份） ----
    in.numericContractAnchor = requireString(root, "numericContractAnchor");
    const tk::JsonValue* origin = requireObject(root, "origin");
    if (origin != nullptr) {
        in.origin.kind = originKindFromToken(requireString(*origin, "kind"));
        if (const tk::JsonValue* src = origin->find("sourceObjectHex");
            src != nullptr && src->isString()) {
            in.origin.sourceObject = objectIdFromHex(src->text);
        }
        if (const tk::JsonValue* note = origin->find("note"); note != nullptr && note->isString()) {
            in.origin.note = note->text;
        }
    }
    if (const tk::JsonValue* compat = root.find("compatibilityNotes");
        compat != nullptr && compat->isString()) {
        in.compatibilityNotes = compat->text;
    }

    // ---- 闭包应答表（inputs 的 closure 节：对象→角色 token＋组定义） ----
    if (const tk::JsonValue* closureJson = root.find("closure");
        closureJson != nullptr && closureJson->isObject()) {
        if (const tk::JsonValue* objects = requireArray(*closureJson, "objects")) {
            for (const auto& e : objects->items) {
                closure.roles[objectIdFromHex(requireString(e, "objectHex"))] =
                    requireString(e, "role");
            }
        }
        if (const tk::JsonValue* groupMap = closureJson->find("groups");
            groupMap != nullptr && groupMap->isObject()) {
            for (const auto& [name, defined] : groupMap->members) {
                closure.groups[name] = defined.isBool() && defined.boolean;
            }
        }
    }
    return in;
}

/// 装载契约夹具数据集（GoldenDataset 全链——schema/完整性/交叉校验）。
tk::GoldenDataset loadFixture()
{
    return tk::GoldenDataset::load({kDatasetId, kDatasetVersion});
}

/// 读 expected/identity.json（独立期望——JS 参考实现的产出）。
tk::JsonValue loadExpectedIdentity(const tk::GoldenDataset& dataset)
{
    const std::string text =
        readTextFile(dataset.resolveExpected("expected/identity.json"));
    EXPECT_FALSE(text.empty()) << "expected/identity.json 不可读";
    return tk::parseJson(text);
}

/// 发布解析便捷入口（返回已发布策略；失败即测试显性失败——夹具必须可发布）。
EngineeringPolicySet publishOrDie(const RawPolicyInput& in,
                                  const FixtureValidationContext& closure)
{
    const PolicyParseResult res = resolvePolicy(in, closure);
    if (!res.policy.has_value()) {
        ADD_FAILURE() << "夹具策略应可发布，首条诊断: "
                      << (res.diagnostics.empty() ? std::string{"-"}
                                                  : res.diagnostics.front().code);
        // 不可达——make 一个最小对象避免编译器对空返回路径的告警。
        return *resolvePolicy(in, closure).policy;
    }
    return *res.policy;
}

}  // namespace

// =====================================================================
// 用例组 1：数据集装载（testkit manifest schema 全链——"lint 通过"的
// 消费侧承载；testdata_lint 工具全量扫描是独立第二通道）。
// =====================================================================

/**
 * POL-T11 数据集交付（acceptance 3）：contract-fixture 类数据集经
 * GoldenDataset::load 全链装载零异常——schema 句法（datasetId/版本目录
 * 一致、coveredRequirements 需求 ID 句法、units/frames 必填）、完整性
 * 申报（§4.5 覆盖 inputs+expected+generate）、字段交叉校验一次通过。
 */
TEST(PolPolicyCodecFixture, LoadsThroughFullManifestChain)
{
    // 全链装载：解析→schema→integrity（逐文件 SHA-256＋大小）→交叉校验；
    // 任何失败抛 TestKitError(dataset-invalid)——此处让它显性失败。
    const tk::GoldenDataset dataset = loadFixture();

    // 类别与追溯面核对（contract-fixture 类＋需求/AT 锚——登记语义）。
    const tk::DatasetManifest& manifest = dataset.manifest();
    EXPECT_EQ(manifest.kind, tk::DatasetKind::ContractFixture);
    EXPECT_EQ(manifest.datasetId, kDatasetId);
    EXPECT_FALSE(manifest.coveredRequirements.empty());
    // 完整性条目覆盖 inputs+expected+generate 三面（≥3——§4.5 申报面）。
    EXPECT_GE(manifest.integrity.size(), 3u);
}

/**
 * 完整性复核（testkit §4.5 的显式再现）：对 manifest 申报的每个条目
 * 重算 SHA-256 与大小——数据在仓库流转中的任何漂移在此暴露（完整性
 * 是"黄金"二字的前提，NFR-COR-01）。
 */
TEST(PolPolicyCodecFixture, IntegrityDigestsMatchCommittedFiles)
{
    const tk::GoldenDataset dataset = loadFixture();
    for (const auto& entry : dataset.manifest().integrity) {
        // 按条目路径读原文：版本目录＝goldenDataRoot()/golden/<id>/<version>
        // （GoldenDataset 只暴露 inputs/expected 清单成员的解析器——generate
        // 脚本等其余完整性条目经数据根拼装，TkTestPaths 唯一入口口径不变）。
        const std::filesystem::path file =
            tk::goldenDataRoot() / "golden" / kDatasetId / kDatasetVersion
            / entry.path;
        ASSERT_TRUE(std::filesystem::exists(file))
            << "完整性条目文件缺失: " << entry.path;
        const std::string text = readTextFile(file);
        // SHA-256 重算经 core ContentDigester（CR-02 同源摘要原语——生成
        // 侧 manifest 申报用的是同一算法，逐字节对照才有意义）。
        core::ContentDigester digester;
        digester.update(text.data(), text.size());
        const core::Digest256 digest = digester.finalize();
        EXPECT_EQ(bytesToHex(std::vector<std::uint8_t>(digest.begin(), digest.end())),
                  entry.sha256Hex)
            << "完整性摘要不符: " << entry.path;
        EXPECT_EQ(text.size(), entry.sizeBytes) << "完整性大小不符: " << entry.path;
    }
}

// =====================================================================
// 用例组 2：双实现互证（编码/身份——expected 由 JS 参考编码器独立产出）。
// =====================================================================

/**
 * POL-ID-1/POL-ID-2 的数据集载体：canonical 编码逐字节对照。
 *
 * JS 参考编码器按 §5.3 布局独立重实现（magic/大端/长度前缀/presence/
 * 位模式/规范序）——产品 PolicyCodec::encode 的输出与其逐字节一致；
 * decode 严格还原为规范化输入（往返载体语义）。任一侧布局漂移即失败。
 */
TEST(PolPolicyCodecFixture, CanonicalEncodingMatchesIndependentExpectation)
{
    const tk::GoldenDataset dataset = loadFixture();
    const tk::JsonValue expected = loadExpectedIdentity(dataset);

    // 重建规范变体的输入（inputs/policy-input.json＝已规范化承载序）。
    const std::string inputText =
        readTextFile(dataset.resolveInput("inputs/policy-input.json"));
    ASSERT_FALSE(inputText.empty()) << "inputs/policy-input.json 不可读";
    FixtureValidationContext closure;
    const RawPolicyInput input =
        policyInputFromJson(tk::parseJson(inputText), closure);

    // ---- full 形态逐字节对照（独立期望的还原→与产品编码比对） ----
    const std::vector<std::uint8_t> expectedBytes =
        hexToBytes(requireString(expected, "fullEncodingHex"));
    ASSERT_FALSE(expectedBytes.empty()) << "expected.fullEncodingHex 非法";
    EXPECT_EQ(bytesToHex(PolicyCodec::encode(input)),
              bytesToHex(expectedBytes))
        << "canonical 编码与独立参考实现不一致（§5.3 布局漂移）";

    // ---- 往返载体语义：decode（期望字节）==规范化输入（POL-ID-1） ----
    const RawPolicyInput decoded = PolicyCodec::decode(expectedBytes);
    EXPECT_EQ(decoded, input)
        << "decode(期望字节) 应逐字段还原规范化输入（往返保真）";
}

/**
 * CON-05 的数据集载体：内容身份与独立摘要对照。
 *
 * JS 侧按"语义闭包投影编码 → SHA-256"独立复算身份；产品侧经
 * resolvePolicy 七段管线（解析②归一→⑥投影摘要）发布——两条路径的
 * 身份必须逐串一致。同时显式复核投影编码字节（身份与投影不可独立漂移
 * ——CR-02 契约的观测面）。
 */
TEST(PolPolicyCodecFixture, ContentIdentityMatchesIndependentDigest)
{
    const tk::GoldenDataset dataset = loadFixture();
    const tk::JsonValue expected = loadExpectedIdentity(dataset);

    const std::string inputText =
        readTextFile(dataset.resolveInput("inputs/policy-input.json"));
    ASSERT_FALSE(inputText.empty());
    FixtureValidationContext closure;
    const RawPolicyInput input = policyInputFromJson(tk::parseJson(inputText), closure);

    // 产品路径：解析管线发布的策略对象携带内容身份（CON-06——快照存该身份）。
    const EngineeringPolicySet published = publishOrDie(input, closure);
    EXPECT_EQ(bytesToHex(std::vector<std::uint8_t>(published.contentIdentity.bytes.begin(),
                                                   published.contentIdentity.bytes.end())),
              requireString(expected, "contentIdentityHex"))
        << "发布身份与独立摘要不一致（管线⑥投影/摘要漂移）";

    // 投影编码显式对照（身份的摘要输入——与 expected 的 projectionHex 一致）。
    const std::vector<std::uint8_t> projection = PolicyCodec::encodeSemanticProjection(
        published.schemaVersion, published.collision, published.jointThresholds,
        published.applicability, published.numericContractAnchor);
    EXPECT_EQ(bytesToHex(projection), requireString(expected, "semanticProjectionHex"))
        << "语义闭包投影与独立参考实现不一致";
}

/**
 * POL-ID-2/POL-ID-3 的数据集载体：排序打乱与显示单位切换不变身份。
 *
 * inputs/policy-input-variant.json 与规范变体同语义：规则清单承载序打乱、
 * 无序对两端互换、域集合乱序、安全间距以 mm 表述等值（10 mm ↔ 0.01 m，
 * core convert (v·factor)/factor 的 IEEE 逐位口径）——两条输入的编码
 * 字节与发布身份都必须与 expected 完全一致（显示单位/承载序不入身份，
 * UX-08/KIN-12 的数据级证据）。
 */
TEST(PolPolicyCodecFixture, ShuffledAndDisplayUnitVariantKeepsIdentity)
{
    const tk::GoldenDataset dataset = loadFixture();
    const tk::JsonValue expected = loadExpectedIdentity(dataset);

    const std::string variantText =
        readTextFile(dataset.resolveInput("inputs/policy-input-variant.json"));
    ASSERT_FALSE(variantText.empty()) << "inputs/policy-input-variant.json 不可读";
    FixtureValidationContext closureVariant;
    const RawPolicyInput variant =
        policyInputFromJson(tk::parseJson(variantText), closureVariant);

    // 语义闭包投影：变体与规范变体同字节（承载序/显示单位不入投影——
    // POL-ID-2/POL-ID-3 的机制面；full 形态对象字节按设计承载显示原文，
    // 等价面在语义投影与发布身份，不在对象字节）。
    const EngineeringPolicySet publishedVariant = publishOrDie(variant, closureVariant);
    const std::vector<std::uint8_t> projectionVariant = PolicyCodec::encodeSemanticProjection(
        publishedVariant.schemaVersion, publishedVariant.collision,
        publishedVariant.jointThresholds, publishedVariant.applicability,
        publishedVariant.numericContractAnchor);
    EXPECT_EQ(bytesToHex(projectionVariant),
              requireString(expected, "semanticProjectionHex"))
        << "变体的语义闭包投影应与规范变体逐字节一致（POL-ID-2/POL-ID-3）";

    // 发布身份：变体与规范变体同身份（内容编址的失效判定前提——CON-05）。
    EXPECT_EQ(bytesToHex(std::vector<std::uint8_t>(
                  publishedVariant.contentIdentity.bytes.begin(),
                  publishedVariant.contentIdentity.bytes.end())),
              requireString(expected, "contentIdentityHex"))
        << "变体发布身份应与规范变体一致（显示单位/承载序不入身份）";
    EXPECT_TRUE(publishedVariant.contentIdentity.isValid())
        << "发布身份必有效（发布门③）";

    // SI 真值相等显式断言（mm 输入归一后的安全间距＝独立期望登记的 SI 值）。
    const tk::JsonValue* si = expected.find("siValues");
    ASSERT_NE(si, nullptr) << "expected 缺 siValues 节";
    ASSERT_TRUE(publishedVariant.collision.safetyClearance.has_value());
    EXPECT_EQ(publishedVariant.collision.safetyClearance->siValue(),
              requireNumber(*si, "safetyClearanceSi"))
        << "mm 输入的 SI 归一值与独立期望不一致（core convert 逐位口径）";
}
