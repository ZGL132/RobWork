/**
 * @file   ErrorsTest.cpp
 * @brief  modeling 错误契约用例组（MdlErrors）——ModelingErrorCode 全表
 *         token、ModelingError 值语义与域错误→稳定诊断码映射数据
 *         （任务契约 WP-13-T02 acceptance 4 的 Errors.hpp 行具名自证）。
 *
 * 设计依据：
 *   - units/modeling.md §3.3（Errors.hpp 行）、§9.5 尾段（域错误→码映射
 *     纪律："产码唯一经 IDiagnosticFactory::create（码已注册校验），禁
 *     字符串拼码"）、§9.4.1/§9.4.2/§9.4.5（枚举值出处接口）
 *   - 任务契约 tasks/foundation/WP-13-T02.json acceptance 4
 */

#include <sdurws/ird/modeling/Errors.hpp>

#include <sdurws/ird/modeling/DiagCodes.hpp>          // 映射码与注册清单的同源交叉核对
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯登记（testkit §7.3）

#include <gtest/gtest.h>

#include <algorithm>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using sdurws::ird::modeling::ModelingError;
using sdurws::ird::modeling::ModelingErrorCode;
using sdurws::ird::modeling::modelingDiagCode;
using sdurws::ird::modeling::modelingErrorCodeToken;

namespace {

/// 全表 12 值（枚举声明序——Errors.hpp §9.4 首次出现序；新增表尾追加值
/// 时本清单同步扩展，计数断言暴露遗漏）。
constexpr ModelingErrorCode kAllCodes[] = {
    ModelingErrorCode::DuplicateObjectId,
    ModelingErrorCode::RefProtected,
    ModelingErrorCode::AuthorityViolation,
    ModelingErrorCode::UnitIllegal,
    ModelingErrorCode::NotFinite,
    ModelingErrorCode::CentroidEditUnresolved,
    ModelingErrorCode::BatchPartial,
    ModelingErrorCode::TemplateDisabled,
    ModelingErrorCode::IllegalName,
    ModelingErrorCode::RefMissing,
    ModelingErrorCode::DhExpandFailed,
    ModelingErrorCode::SchemaVersionUnsupported,
};

}  // namespace

/**
 * 错误码 token 全表机械比对：每值 token 非空、全表互异（判别码的最低
 * 契约——同串即不可判）；枚举值总数恰 12（与 kAllCodes 清单一致——
 * 防枚举扩表后清单漏更的静默漂移）。
 */
TEST(MdlErrors, TokenTableCompleteAndDistinct_WP13T02_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"ERR-01"},
                  std::vector<std::string>{});

    std::set<std::string_view> tokens;
    for (const ModelingErrorCode code : kAllCodes) {
        const std::string_view token = modelingErrorCodeToken(code);
        EXPECT_FALSE(token.empty()) << "枚举值 " << static_cast<int>(code)
                                    << " 的 token 为空（转发表漏登记）";
        tokens.insert(token);
    }
    EXPECT_EQ(tokens.size(), std::size(kAllCodes))
        << "token 存在同串（判别码互异性破坏）或枚举/清单计数失同步";
    EXPECT_EQ(tokens.size(), 12U) << "枚举全表应恰 12 值（§9.4 收编口径；"
                                     "表尾追加时同步扩展本清单）";
}

/**
 * token 确定性与代表性值锚定：同码重复取 token 结果一致（NFR-COR-02）；
 * 三个代表性值与 §9.4/V 矩阵行文成员名原文一致（RefProtected＝V-04 行、
 * AuthorityViolation＝V-14 行、SchemaVersionUnsupported＝§9.4.5 @错误 行）。
 */
TEST(MdlErrors, TokenDeterministicAndAnchoredToCard_WP13T02_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"ERR-01", "NFR-COR-02"},
                  std::vector<std::string>{});

    EXPECT_EQ(modelingErrorCodeToken(ModelingErrorCode::RefProtected),
              "RefProtected");
    EXPECT_EQ(modelingErrorCodeToken(ModelingErrorCode::AuthorityViolation),
              "AuthorityViolation");
    EXPECT_EQ(modelingErrorCodeToken(ModelingErrorCode::SchemaVersionUnsupported),
              "SchemaVersionUnsupported");
    // 确定性：重复调用同串（编译期固定表的回归锚）。
    for (const ModelingErrorCode code : kAllCodes) {
        EXPECT_EQ(modelingErrorCodeToken(code), modelingErrorCodeToken(code));
    }
}

/**
 * ModelingError 值语义（io::IoError 同款形态）：params 保序（构造序＝
 * 遍历序——NFR-COR-01/02 确定性要求）、detail 随值携带；默认构造仅为
 * 聚合占位（code＝首值），业务代码显式携带语义码（文件头注纪律）。
 */
TEST(MdlErrors, ErrorValueSemanticsOrderedParams_WP13T02_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-01"},
                  std::vector<std::string>{});

    // 显式构造：码＋有序参数＋细节（SchemaVersionUnsupported 的三键形态
    // 与 DiagCodes.hpp 同码 paramSchema 对齐——参数名词形小写连字符）。
    const ModelingError err{ModelingErrorCode::SchemaVersionUnsupported,
                            {{"object-type", "robot-design"},
                             {"schema-version", "2"},
                             {"supported-major", "1"}},
                            "对象 schema 主版本超出支持（测试样例）"};
    EXPECT_EQ(err.code, ModelingErrorCode::SchemaVersionUnsupported);
    ASSERT_EQ(err.params.size(), 3U);
    EXPECT_EQ(err.params[0].first, "object-type");
    EXPECT_EQ(err.params[1].first, "schema-version");
    EXPECT_EQ(err.params[2].first, "supported-major");
    EXPECT_FALSE(err.detail.empty());

    // 缺省构造＝聚合占位（首值），不承载业务语义（文件头注）。
    ModelingError placeholder{};
    EXPECT_EQ(placeholder.code, ModelingErrorCode::DuplicateObjectId);
    EXPECT_TRUE(placeholder.params.empty());
    EXPECT_TRUE(placeholder.detail.empty());
}

/**
 * 域错误→稳定诊断码映射数据（§9.5 尾段）：当前已登记两行
 * SchemaVersionUnsupported→"MDL-READINESS-SCHEMA-UNSUPPORTED"（§9.5
 * T02/T03 行）＋TemplateDisabled→"MDL-TEMPLATE-DISABLED"（§9.5 T07 行，
 * WP-13-T07 实现期增登随生产者接口 createDraft 落位同批登记）——映射串
 * 与 DiagCodes.hpp 注册清单逐字节同源（交叉核对，禁两处各写各的字面量
 * 而无对账）；其余 10 值 nullopt＝暂无已登记映射（阶段纪律：不私定码值，
 * 映射随生产者任务在 §9.5 纪律内登记；nullopt 时调用方不得产诊断——
 * 错误经值面返回）。
 */
TEST(MdlErrors, DiagCodeMappingStagedRows_WP13T02_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"ERR-01", "NFR-MNT-03"},
                  std::vector<std::string>{});

    // 已登记行：映射串与注册清单同源（从 modelingCodeDescriptors 取该码
    // 比对——两处字面量失同步即刻暴露）。T07 行同口径（WP-13-T07 同批
    // 追加——Errors.cpp 映射与 DiagCodes.hpp kMdlTemplateDisabled 同源）。
    const auto descriptors = sdurws::ird::modeling::modelingCodeDescriptors();
    const auto assertMappedAndRegistered = [&](const ModelingErrorCode code,
                                               const char* expected) {
        const auto mapped = modelingDiagCode(code);
        ASSERT_TRUE(mapped.has_value())
            << modelingErrorCodeToken(code) << " 的映射码是 §9.5 已登记行的"
                                       "登记义务";
        EXPECT_EQ(*mapped, expected) << "映射串漂移（同源纪律）";
        bool foundInRegistryList = false;
        for (const auto& d : descriptors) {
            if (d.code == *mapped) {
                foundInRegistryList = true;
            }
        }
        EXPECT_TRUE(foundInRegistryList)
            << "映射码 " << *mapped
            << " 必须同时出现在 DiagCodes.hpp 注册清单（同源对账——禁字符串拼码面）";
    };
    assertMappedAndRegistered(ModelingErrorCode::SchemaVersionUnsupported,
                              "MDL-READINESS-SCHEMA-UNSUPPORTED");
    assertMappedAndRegistered(ModelingErrorCode::TemplateDisabled,
                              "MDL-TEMPLATE-DISABLED");

    // 暂缓行：其余 10 值 nullopt（阶段纪律的封闭性——多登记了未到任务
    // 行的映射也在此暴露；IllegalName 的生产者 createDraft 已落位但 §9.5
    // 尚无其独立码行——维持 nullopt，不私定码值凑数）。
    for (const ModelingErrorCode code : kAllCodes) {
        if (code == ModelingErrorCode::SchemaVersionUnsupported
            || code == ModelingErrorCode::TemplateDisabled) {
            continue;
        }
        EXPECT_FALSE(modelingDiagCode(code).has_value())
            << "枚举值 " << modelingErrorCodeToken(code)
            << " 尚未到 §9.5 登记任务行——映射应为 nullopt（不预建）";
    }
}
