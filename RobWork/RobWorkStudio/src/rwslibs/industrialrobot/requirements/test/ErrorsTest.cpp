/**
 * @file   ErrorsTest.cpp
 * @brief  requirements 错误契约用例组（ReqErrors）——RequirementErrorCode
 *         全表 token、RequirementError 值语义与域错误→稳定码映射的分批
 *         纪律（任务契约 WP-14-T02 acceptance 3 的 Errors.hpp 行具名
 *         自证）。
 *
 * 设计依据：
 *   - units/requirements.md §9.3~§9.5（@错误 行收编面——8 值的出处）、
 *     §9.6（映射分批纪律——仅 SchemaVersionUnsupported 有已登记映射码）、
 *     §9.2（非异常出口——值面承载）
 *   - 先例：modeling/test/ErrorsTest.cpp（token 全表机械比对＋值语义＋
 *     映射分批的同款形态）
 *   - 任务契约 tasks/foundation/WP-14-T02.json acceptance 3
 */

#include <sdurws/ird/requirements/Errors.hpp>

#include <sdurws/ird/requirements/DiagCodes.hpp>  // kReqSchemaUnsupported——映射码同源交叉核对用（DiagCodesTest 与本用例双面钉住同串）
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯登记（testkit §7.3）

#include <gtest/gtest.h>

#include <set>
#include <string>
#include <string_view>
#include <vector>

using namespace std::string_view_literals;
using sdurws::ird::requirements::RequirementError;
using sdurws::ird::requirements::RequirementErrorCode;

namespace {

/// 全表 8 值清单（§9.3~§9.5 @错误 行原文序——测试内自持，与实现 token 表
/// 机械比对：任一侧漂移即失败）。
const std::pair<RequirementErrorCode, std::string_view> kErrorCodeTable[] = {
    {RequirementErrorCode::SchemaVersionUnsupported, "SchemaVersionUnsupported"sv},
    {RequirementErrorCode::DuplicateName, "DuplicateName"sv},
    {RequirementErrorCode::IllegalTolerance, "IllegalTolerance"sv},
    {RequirementErrorCode::ZeroVectorTarget, "ZeroVectorTarget"sv},
    {RequirementErrorCode::AllDofFree, "AllDofFree"sv},
    {RequirementErrorCode::DegenerateRegion, "DegenerateRegion"sv},
    {RequirementErrorCode::NegativeCount, "NegativeCount"sv},
    {RequirementErrorCode::RegionNotBox, "RegionNotBox"sv},
};

}  // namespace

/**
 * 错误码全表 token 完整且互异（acceptance 3——token 是错误面的判别串，
 * 与枚举成员一一对应）：全表 8 值逐一核对＋token 两两互异（集合去重
 * 计数＝8）＋表规模哨兵。
 */
TEST(ReqErrors, TokenTableCompleteAndDistinct_WP14T02_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"ERR-01"},
                  std::vector<std::string>{});

    ASSERT_EQ(std::size(kErrorCodeTable), 8U)
        << "RequirementErrorCode 应恰 8 值（§9.3~§9.5 @错误 行收编）";

    std::set<std::string_view> tokens;
    for (const auto& [code, token] : kErrorCodeTable) {
        EXPECT_EQ(sdurws::ird::requirements::requirementErrorCodeToken(code), token)
            << "token 漂移: " << token;
        tokens.insert(token);
    }
    EXPECT_EQ(tokens.size(), 8U) << "token 必须两两互异（判别串前提）";
}

/**
 * token 确定性且锚定卡面（acceptance 3——NFR-COR-02 同码同串）：重复
 * 调用同串（静态存储期字面量——两次取值指针相等是字面量折叠的实现侧
 * 证据，语义断言为值相等）；@错误 行出处逐值锚定（§9.3 解码 1 值／
 * §9.4 createPoint 4 值／§9.5 buildPlan 3 值）。
 */
TEST(ReqErrors, TokenDeterministicAndAnchoredToCard_WP14T02_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"ERR-01", "NFR-COR-02"},
                  std::vector<std::string>{});

    // 确定性：同码两次取 token 值相等（跨进程逐字节一致的码面）。
    const auto t1 = sdurws::ird::requirements::requirementErrorCodeToken(
        RequirementErrorCode::DuplicateName);
    const auto t2 = sdurws::ird::requirements::requirementErrorCodeToken(
        RequirementErrorCode::DuplicateName);
    EXPECT_EQ(t1, t2);
    EXPECT_EQ(t1, "DuplicateName"sv);

    // 卡面锚定抽查：§9.3 编辑器 @pre 行的解码失败值（首个映射码的
    // 生产界面）；§9.4/§9.5 的族首值——命名与卡面原文逐字一致。
    EXPECT_EQ(sdurws::ird::requirements::requirementErrorCodeToken(
                  RequirementErrorCode::SchemaVersionUnsupported),
              "SchemaVersionUnsupported"sv);
    EXPECT_EQ(sdurws::ird::requirements::requirementErrorCodeToken(
                  RequirementErrorCode::DegenerateRegion),
              "DegenerateRegion"sv);
}

/**
 * RequirementError 值语义：缺省构造＋params 保序（acceptance 3——§9.2
 * 非异常出口的值面；NFR-COR-01/02 params 构造序遍历）：聚合默认初始化
 * 占位语义、params 保序 vector 不经重排、detail 独立承载。
 */
TEST(ReqErrors, ErrorValueSemanticsOrderedParams_WP14T02_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-01", "NFR-COR-02"},
                  std::vector<std::string>{});

    // 缺省构造＝占位语义（枚举首值；业务代码应总是携带语义明确的码）。
    RequirementError def;
    EXPECT_EQ(def.code, RequirementErrorCode::SchemaVersionUnsupported);
    EXPECT_TRUE(def.params.empty());
    EXPECT_TRUE(def.detail.empty());

    // 构造带参错误：params 保序（先放键后遍历——顺序不得被重排）。
    RequirementError err;
    err.code = RequirementErrorCode::SchemaVersionUnsupported;
    err.params.emplace_back("object-type", "req-set");
    err.params.emplace_back("schema-version", "2");
    err.params.emplace_back("supported-major", "1");
    err.detail = "decode rejected future schema";
    ASSERT_EQ(err.params.size(), 3U);
    EXPECT_EQ(err.params[0].first, "object-type");
    EXPECT_EQ(err.params[1].first, "schema-version");
    EXPECT_EQ(err.params[2].first, "supported-major");
    EXPECT_EQ(err.detail, "decode rejected future schema");
}

/**
 * 域错误→稳定码映射分批纪律（acceptance 3——§9.6"不预建无消费者条目"）：
 * 仅 SchemaVersionUnsupported 映射到 REQ-SCHEMA-UNSUPPORTED（与 DiagCodes.hpp
 * 常量同源同串）；其余 7 值 nullopt（调用方不得产诊断——值面返回）。
 */
TEST(ReqErrors, DiagCodeMappingStagedRows_WP14T02_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"ERR-01"},
                  std::vector<std::string>{});

    // 已到任务行的映射：与 DiagCodes.hpp 码值常量同源同串（交叉核对——
    // 两处字面量失同步即刻暴露）。
    const auto mapped = sdurws::ird::requirements::requirementDiagCode(
        RequirementErrorCode::SchemaVersionUnsupported);
    ASSERT_TRUE(mapped.has_value());
    EXPECT_EQ(*mapped, sdurws::ird::requirements::kReqSchemaUnsupported);
    EXPECT_EQ(*mapped, "REQ-SCHEMA-UNSUPPORTED"sv);

    // 未到任务行的 7 值＝nullopt（分批纪律：映射随 §9.6 码行注册同批
    // 追加，不预建——禁私定码值凑数）。
    for (const auto& [code, token] : kErrorCodeTable) {
        if (code == RequirementErrorCode::SchemaVersionUnsupported) {
            continue;
        }
        EXPECT_FALSE(sdurws::ird::requirements::requirementDiagCode(code).has_value())
            << "未到任务行的码不得预建映射: " << token;
    }
}
