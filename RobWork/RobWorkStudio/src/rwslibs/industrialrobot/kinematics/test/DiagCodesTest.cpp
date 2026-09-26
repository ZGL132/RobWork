/**
 * @file   DiagCodesTest.cpp
 * @brief  kinematics 稳定诊断码工厂用例组（KinDiagCodes）——KIN-* 码
 *         描述符登记值（§9.6 全表逐字段）、真实 StableCodeRegistry 注册
 *         行为与全表清单纪律（任务契约 WP-15-T02 acceptance 4 的
 *         DiagCodes.hpp 行具名自证；knownPitfalls P-KIN-7 的实现侧处置
 *         自证）。
 *
 * 设计依据：
 *   - units/kinematics.md §9.6（全表 15 行——码/级别/语义/任务列；表头
 *     "装配期注册，不预建无消费者条目"）、§3.3（DiagCodes.hpp 行——T02）
 *   - units/diagnostics.md §4.3（分类词表）、§4.4（动作族映射）、§4.5
 *     （注册协议——句法/前缀-所有权/键唯一/paramSchema/字段不变量）、
 *     §9.1（StableCodeRegistry 行为冻结表）
 *   - 收编确认锚点（如实声明，requirements/DiagCodesTest.cpp 同款）：
 *     KIN-* 码进入 diagnostics 全局装配（L5 装配清单）属 diagnostics
 *     所有者的治理动作——本任务产出注册面并以真实注册表验证，不在实现
 *     侧私自扩编 diagnostics 单元文件（allowedFiles 红线）
 *   - 任务契约 tasks/foundation/WP-15-T02.json acceptance 4
 */

#include <sdurws/ird/kinematics/DiagCodes.hpp>

#include <sdurws/ird/diagnostics/DiagCodes.hpp>
#include <sdurws/ird/diagnostics/Errors.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯登记（testkit §7.3）

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

using sdurws::ird::diagnostics::CodeDescriptor;
using sdurws::ird::diagnostics::DiagnosticCategory;
using sdurws::ird::diagnostics::DiagnosticSeverity;
using sdurws::ird::diagnostics::DiagnosticsError;
using sdurws::ird::diagnostics::RetryKind;
using sdurws::ird::diagnostics::StableCodeRegistry;
using sdurws::ird::kinematics::kinematicsCodeDescriptors;
using sdurws::ird::kinematics::registerKinematicsCodes;

namespace {

/// §9.6 全表 15 行的**应登记码面**（测试内自持字面清单——与实现清单机械
/// 比对，任一侧漂移即失败：失同步防线）。行序＝§9.6 表行序（实现清单序
/// ＝确定性序的依据面）；卡面增行（随消费任务）时在实现清单与本清单
/// 表尾同步追加并走单元卡增量修订。
const char* kSection96FullTable[] = {
    "KIN-NO-DEVICE",
    "KIN-NO-TCP",
    "KIN-TARGET-ILLEGAL",
    "KIN-RESIDUAL-EXCEEDED",
    "KIN-JOINT-LIMIT-VIOLATED",
    "KIN-NEAR-LIMIT",
    "KIN-NEAR-SINGULAR",
    "KIN-COLLISION-FILTERED",
    "KIN-COLLISION-UNAVAILABLE",
    "KIN-SEARCH-EXHAUSTED",
    "KIN-SOLVER-INTERNAL",
    "KIN-CONFIG-ILLEGAL",
    "KIN-SAMPLE-IDENTITY-MISMATCH",
    "KIN-COVERAGE-ZERO-SAMPLES",
    "KIN-RESULT-INCOMPLETE",
};

/// §9.6"级别"列（与码清单同序——逐码登记值）：error×4＋warning×11。
const bool kSection96IsError[] = {
    true,   // KIN-NO-DEVICE            error
    true,   // KIN-NO-TCP               error
    true,   // KIN-TARGET-ILLEGAL       error
    false,  // KIN-RESIDUAL-EXCEEDED    warning
    false,  // KIN-JOINT-LIMIT-VIOLATED warning
    false,  // KIN-NEAR-LIMIT           warning
    false,  // KIN-NEAR-SINGULAR        warning
    false,  // KIN-COLLISION-FILTERED   warning
    false,  // KIN-COLLISION-UNAVAILABLE warning
    false,  // KIN-SEARCH-EXHAUSTED     warning
    true,   // KIN-SOLVER-INTERNAL      error
    true,   // KIN-CONFIG-ILLEGAL       error
    true,   // KIN-SAMPLE-IDENTITY-MISMATCH error
    false,  // KIN-COVERAGE-ZERO-SAMPLES warning
    false,  // KIN-RESULT-INCOMPLETE    warning
};

/// 逐码分类/重试族登记值（与码清单同序；落值锚点＝DiagCodes.cpp 逐码
/// 注释——本表是其机器核对面，任一侧漂移即失败）。
struct ExpectedCategoryRow {
    DiagnosticCategory category;
    RetryKind retryable;
    bool requiresComparison;
};

const ExpectedCategoryRow kExpectedCategoryRows[] = {
    {DiagnosticCategory::InputInvalid,      RetryKind::UserRetry, false},  // NO-DEVICE（fix-input）
    {DiagnosticCategory::InputInvalid,      RetryKind::UserRetry, false},  // NO-TCP（fix-input）
    {DiagnosticCategory::InputInvalid,      RetryKind::UserRetry, false},  // TARGET-ILLEGAL（fix-input）
    {DiagnosticCategory::DataInsufficient,  RetryKind::UserRetry, true},   // RESIDUAL-EXCEEDED（§9.6 比较型原文）
    {DiagnosticCategory::PolicyDenied,      RetryKind::UserRetry, false},  // JOINT-LIMIT-VIOLATED（§4.3 行程上限锚点）
    {DiagnosticCategory::PolicyDenied,      RetryKind::UserRetry, true},   // NEAR-LIMIT（§5.5 裕量比比较型）
    {DiagnosticCategory::PolicyDenied,      RetryKind::UserRetry, false},  // NEAR-SINGULAR（阈值读 policy）
    {DiagnosticCategory::PolicyDenied,      RetryKind::UserRetry, false},  // COLLISION-FILTERED（§4.3 碰撞过滤锚点）
    {DiagnosticCategory::DataInsufficient,  RetryKind::UserRetry, false},  // COLLISION-UNAVAILABLE（§4.3 KIN-05 口径锚点）
    {DiagnosticCategory::DataInsufficient,  RetryKind::UserRetry, false},  // SEARCH-EXHAUSTED（C5 锚点）
    {DiagnosticCategory::Internal,          RetryKind::Never,     false},  // SOLVER-INTERNAL（report-bug 族）
    {DiagnosticCategory::InputInvalid,      RetryKind::UserRetry, false},  // CONFIG-ILLEGAL（I-KIN-4 输入面）
    {DiagnosticCategory::FormatOrVersion,   RetryKind::UserRetry, false},  // SAMPLE-IDENTITY-MISMATCH（契约不兼容族）
    {DiagnosticCategory::DataInsufficient,  RetryKind::UserRetry, false},  // COVERAGE-ZERO-SAMPLES（§7.2 零样本）
    {DiagnosticCategory::ExecutionFailed,   RetryKind::UserRetry, false},  // RESULT-INCOMPLETE（批次执行轴）
};

}  // namespace

/**
 * 工厂清单范围＝§9.6 全表 15 行（acceptance 4——"全表 15 个 KIN- 稳定码
 * 清单登记"）：清单恰含 15 码且行序＝§9.6 表行序，多登（登记卡面之外
 * 的码值）或少登（漏登记）均失败；码值文本与卡面"码"列原文逐字一致。
 */
TEST(KinDiagCodes, FactoryScopeIsFullTable96_WP15T02_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"ERR-01"},
                  std::vector<std::string>{});

    const auto descriptors = kinematicsCodeDescriptors();
    ASSERT_EQ(descriptors.size(), std::size(kSection96FullTable))
        << "工厂清单规模＝§9.6 全表行数（acceptance 4：全表 15 码清单登记）";

    // 行序＝§9.6 表行序（确定性序——逐位比对，不查表不排序）。
    for (std::size_t i = 0; i < descriptors.size(); ++i) {
        EXPECT_EQ(descriptors[i].code, kSection96FullTable[i])
            << "§9.6 行序/码值失同步 @ 行 " << (i + 1);
    }
}

/**
 * 描述符逐字段＝§9.6 行登记值（acceptance 4——登记值可追溯到卡面）：
 * 级别列 15 码逐位核对（error×4＋warning×11）；ownerUnit、文案键命名
 * 约定（diag.<code-lower>.title/.detail——注册期键形校验的预演核对）、
 * paramSchema 全表"[]"（不私造参数名）、确认/可见性/登记版本全表同值。
 */
TEST(KinDiagCodes, DescriptorFieldsMatchSection96Rows_WP15T02_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"ERR-01", "NFR-MNT-03"},
                  std::vector<std::string>{});

    const auto descriptors = kinematicsCodeDescriptors();
    ASSERT_EQ(descriptors.size(), std::size(kSection96FullTable));
    ASSERT_EQ(std::size(kSection96IsError), std::size(kSection96FullTable));

    for (std::size_t i = 0; i < descriptors.size(); ++i) {
        const CodeDescriptor& d = descriptors[i];
        const std::string code = kSection96FullTable[i];

        // 级别列：§9.6"error"→Error、"warning"→Warning（逐位机械核对）。
        if (kSection96IsError[i]) {
            EXPECT_EQ(d.severity, DiagnosticSeverity::Error) << code;
        } else {
            EXPECT_EQ(d.severity, DiagnosticSeverity::Warning) << code;
        }

        // 所有权与键约定（P-DIAG-9 命名约定——小写连字符派生）。
        EXPECT_EQ(d.ownerUnit, "kinematics") << code;
        std::string lower = code;
        for (char& ch : lower) {
            if (ch >= 'A' && ch <= 'Z') { ch = static_cast<char>(ch - 'A' + 'a'); }
        }
        EXPECT_EQ(d.titleKey, "diag." + lower + ".title") << code;
        EXPECT_EQ(d.detailKey, "diag." + lower + ".detail") << code;

        // paramSchema 全表"[]"（diagnostics 展开登记纪律——不私造参数名，
        // 参数面随各消费者任务按需登记并升 registryVersion）。
        EXPECT_EQ(d.paramSchema, "[]") << code;

        // 确认流/登记版本/废弃状态（全表同值——逐码注释共字段口径）。
        EXPECT_FALSE(d.confirmable) << code;
        EXPECT_EQ(d.registryVersion, 1U) << code;
        EXPECT_FALSE(d.deprecated) << code;
        EXPECT_FALSE(d.supersededBy.has_value()) << code;

        // 可见性三项（用户级码全 true——§4.5 仅 Dev 码强制 false）。
        EXPECT_TRUE(d.userVisible) << code;
        EXPECT_TRUE(d.reportable) << code;
        EXPECT_TRUE(d.historical) << code;
    }
}

/**
 * 比较型标记与逐码分类/重试族（acceptance 4——§5.5"比较型诊断带实际值/
 * 期望值/单位"与 diagnostics §4.3/§4.4 落值的机器核对面）：requires-
 * Comparison 恰两码 true（RESIDUAL-EXCEEDED——§9.6"比较型"原文；
 * NEAR-LIMIT——§5.5"裕量比无量纲"点名）；SA-15 不变量（confirmable⇒
 * requiresComparison）全表成立；15 行分类/重试族与 DiagCodes.cpp 逐码
 * 注释一一对应。
 */
TEST(KinDiagCodes, CategoryAndRetryMappingMatchesAnchors_WP15T02_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"ERR-01"},
                  std::vector<std::string>{});

    const auto descriptors = kinematicsCodeDescriptors();
    ASSERT_EQ(descriptors.size(), std::size(kExpectedCategoryRows));
    ASSERT_EQ(descriptors.size(), std::size(kSection96FullTable));

    for (std::size_t i = 0; i < descriptors.size(); ++i) {
        const CodeDescriptor& d = descriptors[i];
        const std::string code = kSection96FullTable[i];

        // SA-15 注册表侧强化（confirmable⇒requiresComparison）——全表
        // confirmable=false，不变量恒成立。
        EXPECT_TRUE(!d.confirmable || d.requiresComparison) << code;

        // 比较型标记恰两码 true（卡面点名锚点——从严执行）。
        EXPECT_EQ(d.requiresComparison, kExpectedCategoryRows[i].requiresComparison)
            << code << " 比较型标记失同步";

        // 分类/重试族逐码核对（DiagCodes.cpp 逐码注释的机器面）。
        EXPECT_EQ(d.category, kExpectedCategoryRows[i].category)
            << code << " 分类落值失同步";
        EXPECT_EQ(d.retryable, kExpectedCategoryRows[i].retryable)
            << code << " 重试族失同步";
    }

    // SOLVER-INTERNAL 唯一 Never（report-bug 族——机械映射的例外面钉住）。
    const auto it = std::find_if(descriptors.begin(), descriptors.end(),
                                 [](const CodeDescriptor& d) {
                                     return d.code == "KIN-SOLVER-INTERNAL";
                                 });
    ASSERT_NE(it, descriptors.end());
    EXPECT_EQ(it->retryable, RetryKind::Never);
}

/**
 * 真实注册表注册成功且可查询（acceptance 4——P-KIN-7 处置自证：消费
 * diagnostics Draft 契约按当周现状，以真实 StableCodeRegistry 验证全部
 * 通过）：注册后全表 15 码 find 命中且 ownerUnit 归属正确、
 * registeredCodes("kinematics") 反映全表、seal 后 manifest 含全表。
 */
TEST(KinDiagCodes, RegistersIntoStableCodeRegistry_WP15T02_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"ERR-01"},
                  std::vector<std::string>{});

    StableCodeRegistry registry;
    registerKinematicsCodes(registry);

    // 全表 find 命中（查询非抛）且归属正确。
    for (const char* code : kSection96FullTable) {
        const CodeDescriptor* found = registry.find(code);
        ASSERT_NE(found, nullptr) << "注册后码必须可 find 命中: " << code;
        EXPECT_EQ(found->ownerUnit, "kinematics") << code;
    }

    // ownerUnit 反查反映全表（字典序由注册表侧承担——15 码无缺漏）。
    const auto codes = registry.registeredCodes("kinematics");
    ASSERT_EQ(codes.size(), std::size(kSection96FullTable));
    for (const char* code : kSection96FullTable) {
        EXPECT_NE(std::find(codes.begin(), codes.end(), code), codes.end())
            << "registeredCodes 缺码: " << code;
    }

    // manifest 含全表（跨进程一致性面——同注册集同摘要）。
    const auto manifest = registry.manifest();
    ASSERT_EQ(manifest.entries.size(), std::size(kSection96FullTable));
    for (const char* code : kSection96FullTable) {
        const auto it = std::find_if(manifest.entries.begin(), manifest.entries.end(),
                                     [&](const CodeDescriptor& d) {
                                         return d.code == code;
                                     });
        EXPECT_TRUE(it != manifest.entries.end()) << "manifest 缺码: " << code;
    }
}

/**
 * 重复注册被拒（acceptance 4——NFR-MNT-03 码值唯一权威：同码二次注册
 * 抛 DiagnosticsError 不吞——装配期 fail-fast）。
 */
TEST(KinDiagCodes, DuplicateRegistrationRejected_WP15T02_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-MNT-03"},
                  std::vector<std::string>{});

    StableCodeRegistry registry;
    registerKinematicsCodes(registry);
    // 全表首码再注册（首版描述符）→DuplicateCode（diagnostics §9.1 行为
    // 冻结表）——不捕获类型细节，只断言"抛 DiagnosticsError"即实现契约
    // 达成。
    const auto descriptors = kinematicsCodeDescriptors();
    EXPECT_THROW(registry.registerCode(descriptors[0]), DiagnosticsError);
}
