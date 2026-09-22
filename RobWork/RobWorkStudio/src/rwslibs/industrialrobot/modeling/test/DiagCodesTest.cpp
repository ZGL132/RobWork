/**
 * @file   DiagCodesTest.cpp
 * @brief  modeling 稳定诊断码工厂用例组（MdlDiagCodes）——MDL-* 码描述符
 *         登记值（§9.5 行逐字段）、真实 StableCodeRegistry 注册行为与
 *         分批注册纪律（任务契约 WP-13-T02 acceptance 4 的 DiagCodes.hpp
 *         行具名自证；knownPitfalls P-MDL-8 的实现侧处置自证）。
 *
 * 设计依据：
 *   - units/modeling.md §9.5（MDL-READINESS-SCHEMA-UNSUPPORTED 行——
 *     类别/级别/confirmable/语义与"只登记有消费者条目，不预建"纪律）、
 *     §3.3（DiagCodes.hpp 行——T02）
 *   - units/diagnostics.md §4.5（注册协议——句法/前缀-所有权/键唯一/
 *     paramSchema 注册期验证）、§9.1（StableCodeRegistry 行为冻结表）
 *   - 收编确认锚点（如实声明，io/IoDiagnosticsTest.cpp 同款）：MDL-* 码
 *     进入 diagnostics 内置装配清单（§4.6 收编）属 diagnostics 所有者的
 *     治理动作——本任务产出注册面并以真实注册表验证，治理侧收编确认随
 *     验收请求提请，不在实现侧私自扩编 diagnostics 单元文件（allowedFiles
 *     红线）
 *   - 任务契约 tasks/foundation/WP-13-T02.json acceptance 4
 */

#include <sdurws/ird/modeling/DiagCodes.hpp>

#include <sdurws/ird/diagnostics/DiagCodes.hpp>
#include <sdurws/ird/diagnostics/Errors.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯登记（testkit §7.3）

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

using sdurws::ird::diagnostics::CodeDescriptor;
using sdurws::ird::diagnostics::CodeTableManifest;
using sdurws::ird::diagnostics::DiagnosticCategory;
using sdurws::ird::diagnostics::DiagnosticSeverity;
using sdurws::ird::diagnostics::DiagnosticsError;
using sdurws::ird::diagnostics::RetryKind;
using sdurws::ird::diagnostics::StableCodeRegistry;
using sdurws::ird::diagnostics::isValidDiagCodeSyntax;
using sdurws::ird::modeling::modelingCodeDescriptors;
using sdurws::ird::modeling::registerModelingCodes;

namespace {

/// §9.5 任务列含 T02 的行的**应登记码面**（测试内自持字面清单——与实现
/// 清单机械比对，任一侧漂移即失败：失同步防线）。当前恰 1 行；T03+ 任务
/// 落位时在实现清单与本清单表尾同步追加（分批纪律——其余 18 行不得提前
/// 出现，见 StagedScope 用例）。
const char* kT02Section95Codes[] = {
    "MDL-READINESS-SCHEMA-UNSUPPORTED",
};

}  // namespace

/**
 * 工厂清单分批封闭性（acceptance 4——"按 §9.5 注册纪律只登记有消费者
 * 条目，不预建"）：清单恰含 §9.5 任务列含 T02 的行（当前 1 行）——
 * 其余 18 行（T05/T08/T09/T13/T18 任务列）提前出现即"预建"违约；逐码
 * 等于卡面字面清单（不私定码值）。
 */
TEST(MdlDiagCodes, FactoryScopeIsStagedT02Rows_WP13T02_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"ERR-01", "MDL-06"},
                  std::vector<std::string>{});

    const auto descriptors = modelingCodeDescriptors();
    ASSERT_EQ(descriptors.size(), std::size(kT02Section95Codes))
        << "工厂清单应恰含 §9.5 T02 任务行（分批纪律：其余行随各自任务"
           "登记——不预建）";
    for (std::size_t i = 0; i < descriptors.size(); ++i) {
        EXPECT_EQ(descriptors[i].code, std::string(kT02Section95Codes[i]))
            << "清单序 " << i << " 与 §9.5 卡面字面不符（不私定码值）";
    }
}

/**
 * 描述符登记值逐字段锚定（§9.5 行＋diagnostics §4.5 字段约束）：码句法、
 * 前缀-所有权（MDL→modeling）、分类/严重、文案键命名约定、paramSchema
 * 三键、confirmable=false、retryable=UserRetry、非 Dev 码三项、版本/废弃
 * 状态。
 */
TEST(MdlDiagCodes, DescriptorFieldsMatchSection95Row_WP13T02_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"ERR-01", "NFR-MNT-03"},
                  std::vector<std::string>{});

    const auto descriptors = modelingCodeDescriptors();
    ASSERT_FALSE(descriptors.empty());
    for (const auto& d : descriptors) {
        // 句法（§4.5：^[A-Z0-9]+(-[A-Z0-9]+)*$ 且 ≤64）。
        EXPECT_TRUE(isValidDiagCodeSyntax(d.code)) << "码句法非法: " << d.code;
        // 前缀-所有权（§4.5 前缀表 MDL→modeling——注册期校验的同判据）。
        EXPECT_EQ(d.ownerUnit, "modeling");
        EXPECT_EQ(d.code.rfind("MDL-", 0), 0U)
            << "首段前缀必须与 ownerUnit 声明域一致（MDL→modeling）";
        // §9.5 行"类别/级别"列："校验/error"→FormatOrVersion/Error
        // （对象 schema 主版本超版＝diagnostics §4.3 format-or-version 族）。
        EXPECT_EQ(d.category, DiagnosticCategory::FormatOrVersion);
        EXPECT_EQ(d.severity, DiagnosticSeverity::Error);
        // 文案键命名约定（P-DIAG-9：diag.<code-lower>.title/.detail）。
        std::string lower;
        lower.reserve(d.code.size());
        std::transform(d.code.begin(), d.code.end(), std::back_inserter(lower),
                       [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        EXPECT_EQ(d.titleKey, "diag." + lower + ".title");
        EXPECT_EQ(d.detailKey, "diag." + lower + ".detail");
        // paramSchema 必填三键（object-type/schema-version/supported-major
        // ——§4.5 受限 JSON 形命名参数清单，词形 ^[a-z0-9]+(-[a-z0-9]+)*$；
        // 与 Errors.hpp 映射行同源）。
        EXPECT_NE(d.paramSchema.find("\"object-type\""), std::string::npos);
        EXPECT_NE(d.paramSchema.find("\"schema-version\""), std::string::npos);
        EXPECT_NE(d.paramSchema.find("\"supported-major\""), std::string::npos);
        // §9.5 行"confirmable"列 false（超版无放行分支）；非比较型。
        EXPECT_FALSE(d.confirmable);
        EXPECT_FALSE(d.requiresComparison);
        // 建议动作"升级程序/重新编辑"＝fix-input 族→UserRetry。
        EXPECT_EQ(d.retryable, RetryKind::UserRetry);
        // 非 Dev 码：可见/可报告/可入历史三项全 true（§4.5 Dev 强制 false
        // 的逆面）；首次登记版本、未废弃、无迁移目标。
        EXPECT_TRUE(d.userVisible);
        EXPECT_TRUE(d.reportable);
        EXPECT_TRUE(d.historical);
        EXPECT_EQ(d.registryVersion, 1U);
        EXPECT_FALSE(d.deprecated);
        EXPECT_FALSE(d.supersededBy.has_value());
    }
}

/**
 * 真实注册表注册行为（§9.1 行为冻结表；P-MDL-8 处置自证——以当周
 * diagnostics 落位形态消费）：注册后逐码 find 命中、registeredCodes
 * ("modeling") 含全表；manifest 两次计算稳定（DT-REG-1 同款观测面）。
 */
TEST(MdlDiagCodes, RegistersIntoStableCodeRegistry_WP13T02_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"ERR-01", "NFR-MNT-03"},
                  std::vector<std::string>{});

    StableCodeRegistry registry;
    ASSERT_NO_THROW(registerModelingCodes(registry))
        << "按卡面产出的描述符必须全部通过注册期验证（§4.5）——出现即"
           "实现缺陷";

    const auto descriptors = modelingCodeDescriptors();
    for (const auto& d : descriptors) {
        const CodeDescriptor* found = registry.find(d.code);
        ASSERT_NE(found, nullptr) << "注册后应可 find 命中: " << d.code;
        EXPECT_EQ(*found, d) << "find 返回的描述符应与登记值逐字段一致";
    }
    const auto owned = registry.registeredCodes("modeling");
    ASSERT_EQ(owned.size(), descriptors.size());
    for (const auto& d : descriptors) {
        EXPECT_NE(std::find(owned.begin(), owned.end(), d.code), owned.end())
            << "registeredCodes(modeling) 应含 " << d.code;
    }
    // manifest 稳定（同注册集同摘要——NFR-COR-02）。
    const CodeTableManifest m1 = registry.manifest();
    const CodeTableManifest m2 = registry.manifest();
    EXPECT_EQ(m1, m2);
    EXPECT_EQ(m1.entries.size(), descriptors.size());
}

/**
 * 重复注册拒绝（NFR-MNT-03 边界拒绝——装配期 fail-fast，不静默覆盖）：
 * 同码二次注册→DiagnosticsError(DuplicateCode)；未 seal 前装配自检合法
 * （查询路径可用——diagnostics §9.1 实现口径）。
 */
TEST(MdlDiagCodes, DuplicateRegistrationRejected_WP13T02_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-MNT-03"},
                  std::vector<std::string>{});

    StableCodeRegistry registry;
    registerModelingCodes(registry);
    for (const auto& d : modelingCodeDescriptors()) {
        EXPECT_THROW(
            {
                try {
                    registry.registerCode(d);
                    FAIL() << "重复注册应抛 DiagnosticsError: " << d.code;
                } catch (const DiagnosticsError& e) {
                    // 码面核对：重复注册归 DuplicateCode（§9.1）——捕获
                    // 后断言码值再隐式重抛（EXPECT_THROW 语义）。
                    EXPECT_EQ(e.code(),
                              sdurws::ird::diagnostics::DiagnosticsErrorCode::DuplicateCode);
                    throw;
                }
            },
            DiagnosticsError);
    }
}
