/**
 * @file   DiagCodesTest.cpp
 * @brief  dynamics 稳定诊断码工厂用例组（DynDiagCodes）——DYN-* 码
 *         描述符登记值（§9.4 登记表逐字段）、真实 StableCodeRegistry
 *         注册行为与全表清单纪律（任务契约 WP-17-T02 acceptance 2
 *         "DYN-* 域诊断码按 units/dynamics.md 登记表装配期注册"的实现
 *         侧自证面）。
 *
 * 设计依据：
 *   - units/dynamics.md §9.4（拟注册清单设计基线——v1 15 行＋WP-17-T05 增行 16：码/严重度/
 *     比较型/用途四列；"码值权威归 StableCodeRegistry，本表为拟注册清单
 *     ——随 WP-17-T02/T03 提交"）、§3.2（DiagCodes.hpp 行——T02 落位）
 *   - units/diagnostics.md §4.3（分类词表）、§4.4（动作族映射）、§4.5
 *     （注册协议——句法/前缀-所有权/键唯一/paramSchema/字段不变量）、
 *     §9.1（StableCodeRegistry 行为冻结表）
 *   - 收编确认锚点（如实声明，kinematics/DiagCodesTest.cpp 同款）：
 *     DYN-* 码进入 diagnostics 全局装配（L5 装配清单调用
 *     registerDynamicsCodes）属装配侧动作——本任务产出注册面并以真实
 *     注册表验证，不在实现侧私自扩编 diagnostics 单元文件（allowedFiles
 *     红线）；DYN 前缀已在注册表前缀-所有权表在册（kPrefixOwners
 *     {"DYN","dynamics"} 行）——注册无前缀阻塞
 *   - 任务契约 tasks/foundation/WP-17-T02.json acceptance 2
 */

#include <sdurws/ird/dynamics/DiagCodes.hpp>

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
using sdurws::ird::dynamics::dynamicsCodeDescriptors;
using sdurws::ird::dynamics::registerDynamicsCodes;

namespace {

/// units/dynamics.md §9.4 登记表的**应登记码面**（测试内自持字面清单——
/// 与实现清单机械比对，任一侧漂移即失败：失同步防线）。行序＝§9.4 表
/// 行序（实现清单序＝确定性序的依据面）；卡面增码（随消费任务 T03+
/// 先走 §9.4 增量修订）时在实现清单与本清单表尾同步追加——既有行不重排。
const char* kSection94FullTable[] = {
    "DYN-INPUT-INVALID",                      // 行 1（输入非法——①级素材）
    "DYN-UPSTREAM-TRAJECTORY-INCOMPATIBLE",   // 行 2（上游轨迹身份/模型不兼容）
    "DYN-TIME-PARAM-MISSING",                 // 行 3（轨迹缺时间参数）
    "DYN-SERIES-NON-MONOTONIC",               // 行 4（时间重复/倒退/零间隔）
    "DYN-SAMPLE-GAP",                         // 行 5（缺样本/采样间隙——比较型）
    "DYN-NON-FINITE",                         // 行 6（非有限输入/输出）
    "DYN-DIMENSION-MISMATCH",                 // 行 7（维度不匹配——比较型）
    "DYN-RNEA-FAILED",                        // 行 8（RNEA 计算失败）
    "DYN-FD-INITIAL-STATE-MISSING",           // 行 9（FD 初始状态缺失）
    "DYN-FD-DIVERGED",                        // 行 10（积分发散）
    "DYN-FD-CONSISTENCY-FAILED",              // 行 11（一致性超阈值——比较型）
    "DYN-PROPERTY-DOWNGRADED",                // 行 12（物性/负载估算降级）
    "DYN-FRICTION-MISSING",                   // 行 13（摩擦参数缺失）
    "DYN-CONDITION-REF-MISSING",              // 行 14（工况引用缺失/空工况集合）
    "DYN-COUPLING-GATE-REJECTED",             // 行 15（耦合链防御拒绝）
    "DYN-FD-NUMERIC-ANOMALY",                 // 行 16（WP-17-T05 增行——正动力学数值异常）
};

/// §9.4"严重度"列（与码清单同序——逐码登记值）：全表 9 error＋7 warning。
const bool kSection94IsError[] = {
    true,   // DYN-INPUT-INVALID                      error
    true,   // DYN-UPSTREAM-TRAJECTORY-INCOMPATIBLE   error
    true,   // DYN-TIME-PARAM-MISSING                 error
    true,   // DYN-SERIES-NON-MONOTONIC               error
    false,  // DYN-SAMPLE-GAP                         warning
    true,   // DYN-NON-FINITE                         error
    true,   // DYN-DIMENSION-MISMATCH                 error
    true,   // DYN-RNEA-FAILED                        error
    false,  // DYN-FD-INITIAL-STATE-MISSING           warning
    false,  // DYN-FD-DIVERGED                        warning
    false,  // DYN-FD-CONSISTENCY-FAILED              warning
    false,  // DYN-PROPERTY-DOWNGRADED                warning
    false,  // DYN-FRICTION-MISSING                   warning
    true,   // DYN-CONDITION-REF-MISSING              error
    true,   // DYN-COUPLING-GATE-REJECTED             error
    false,  // DYN-FD-NUMERIC-ANOMALY                 warning（行 16）
};

/// 逐码分类/重试族/比较型登记值（与码清单同序；落值锚点＝DiagCodes.cpp
/// 逐码注释——本表是其机器核对面，任一侧漂移即失败）。
struct ExpectedCategoryRow {
    DiagnosticCategory category;
    RetryKind retryable;
    bool requiresComparison;
};

const ExpectedCategoryRow kExpectedCategoryRows[] = {
    {DiagnosticCategory::InputInvalid,     RetryKind::UserRetry, false},  // INPUT-INVALID（fix-input）
    {DiagnosticCategory::InputInvalid,     RetryKind::UserRetry, false},  // UPSTREAM-TRAJECTORY-INCOMPATIBLE（fix-input）
    {DiagnosticCategory::InputInvalid,     RetryKind::UserRetry, false},  // TIME-PARAM-MISSING（fix-input）
    {DiagnosticCategory::InputInvalid,     RetryKind::UserRetry, false},  // SERIES-NON-MONOTONIC（fix-input）
    {DiagnosticCategory::DataInsufficient, RetryKind::UserRetry, true},   // SAMPLE-GAP（§9.4 比较型"缺口数/计划数/1"）
    {DiagnosticCategory::InputInvalid,     RetryKind::UserRetry, false},  // NON-FINITE（fix-input）
    {DiagnosticCategory::InputInvalid,     RetryKind::UserRetry, true},   // DIMENSION-MISMATCH（§9.4 比较型"实际/期望/1"）
    {DiagnosticCategory::ExecutionFailed,  RetryKind::UserRetry, false},  // RNEA-FAILED（retry-task——单工况失败可重算）
    {DiagnosticCategory::DataInsufficient, RetryKind::UserRetry, false},  // FD-INITIAL-STATE-MISSING（supply-evidence）
    {DiagnosticCategory::ExecutionFailed,  RetryKind::UserRetry, false},  // FD-DIVERGED（retry-task）
    {DiagnosticCategory::ExecutionFailed,  RetryKind::UserRetry, true},   // FD-CONSISTENCY-FAILED（§9.4 比较型"实际误差/阈值/单位"）
    {DiagnosticCategory::DataInsufficient, RetryKind::UserRetry, false},  // PROPERTY-DOWNGRADED（supply-evidence）
    {DiagnosticCategory::DataInsufficient, RetryKind::UserRetry, false},  // FRICTION-MISSING（supply-evidence）
    {DiagnosticCategory::InputInvalid,     RetryKind::UserRetry, false},  // CONDITION-REF-MISSING（fix-input）
    {DiagnosticCategory::FormatOrVersion,  RetryKind::UserRetry, false},  // COUPLING-GATE-REJECTED（跨版本快照 fix-input）
    {DiagnosticCategory::ExecutionFailed,  RetryKind::UserRetry, false},  // FD-NUMERIC-ANOMALY（行 16——数值异常 retry-task）
};

}  // namespace

/**
 * 工厂清单范围＝§9.4 登记表全表（16 行——acceptance 2"按
 * units/dynamics.md 登记表"）：清单恰含全表且行序＝§9.4 表行序，多登
 * （登记卡面之外的码值——私造码）或少登（漏登记——装配数据源缺口）
 * 均失败；码值文本与卡面"码"列原文逐字一致；无重复码（键唯一——注册
 * 协议前置面）；全表 DYN- 前缀（前缀即所有权声明）。
 */
TEST(DynDiagCodes, FactoryScopeIsSection94FullTable_WP17T02_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"ERR-01", "NFR-MNT-03"},
                  std::vector<std::string>{"DYN-01", "DYN-05", "DYN-06"});

    const auto descriptors = dynamicsCodeDescriptors();
    ASSERT_EQ(descriptors.size(), std::size(kSection94FullTable))
        << "工厂清单规模＝§9.4 登记表行数（acceptance 2——清单＝卡面全表）";

    // 行序＝§9.4 表行序（确定性序——逐位比对，不查表不排序）。
    for (std::size_t i = 0; i < descriptors.size(); ++i) {
        EXPECT_EQ(descriptors[i].code, kSection94FullTable[i])
            << "§9.4 行序/码值失同步 @ 行 " << (i + 1);
    }

    // 无重复：键唯一是 diagnostics 注册协议（§4.5）的前置——重复码在
    // 登记表层面即暴露，不等注册期才失败。
    for (std::size_t i = 0; i < descriptors.size(); ++i) {
        for (std::size_t j = i + 1; j < descriptors.size(); ++j) {
            EXPECT_NE(descriptors[i].code, descriptors[j].code)
                << "重复码值（键唯一前置——§4.5）: " << descriptors[i].code;
        }
    }

    // 全表 DYN- 前缀：前缀即所有权声明——dynamics 登记表不得混入他单元
    // 前缀（前缀-所有权表按 DYN→dynamics 校验）。
    for (const auto& d : descriptors) {
        EXPECT_EQ(d.code.substr(0, 4), std::string_view{"DYN-"})
            << "非 DYN- 前缀码混入登记表: " << d.code;
    }
}

/**
 * 描述符逐字段＝§9.4 行登记值（acceptance 2——登记值可追溯到卡面）：
 * 严重度列 16 码逐位核对（error×9＋warning×7）；ownerUnit、文案键命名
 * 约定（diag.<code-lower>.title/.detail——注册期键形校验的预演核对）、
 * paramSchema 全表"[]"（不私造参数名）、确认/可见性/登记版本全表同值。
 */
TEST(DynDiagCodes, DescriptorFieldsMatchSection94Rows_WP17T02_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"ERR-01", "NFR-MNT-03"},
                  std::vector<std::string>{});

    const auto descriptors = dynamicsCodeDescriptors();
    ASSERT_EQ(descriptors.size(), std::size(kSection94FullTable));
    ASSERT_EQ(std::size(kSection94IsError), std::size(kSection94FullTable));

    for (std::size_t i = 0; i < descriptors.size(); ++i) {
        const CodeDescriptor& d = descriptors[i];
        const std::string code = kSection94FullTable[i];

        // 严重度列：§9.4"error"→Error、"warning"→Warning（逐位机械核对）。
        if (kSection94IsError[i]) {
            EXPECT_EQ(d.severity, DiagnosticSeverity::Error) << code;
        } else {
            EXPECT_EQ(d.severity, DiagnosticSeverity::Warning) << code;
        }

        // 所有权与键约定（P-DIAG-9 命名约定——小写连字符派生）。
        EXPECT_EQ(d.ownerUnit, "dynamics") << code;
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
 * 比较型标记与逐码分类/重试族（acceptance 2——§9.4"比较型"列与
 * diagnostics §4.3/§4.4 落值的机器核对面）：requiresComparison 恰三码
 * true（SAMPLE-GAP/DIMENSION-MISMATCH/FD-CONSISTENCY-FAILED——§9.4
 * "比较型"列点名）；SA-15 不变量（confirmable⇒requiresComparison）全表
 * 成立；16 行分类/重试族与 DiagCodes.cpp 逐码注释一一对应。
 */
TEST(DynDiagCodes, CategoryAndRetryMappingMatchesAnchors_WP17T02_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"ERR-01", "DYN-06"},
                  std::vector<std::string>{});

    const auto descriptors = dynamicsCodeDescriptors();
    ASSERT_EQ(descriptors.size(), std::size(kExpectedCategoryRows));
    ASSERT_EQ(descriptors.size(), std::size(kSection94FullTable));

    std::size_t comparisonCount = 0;
    for (std::size_t i = 0; i < descriptors.size(); ++i) {
        const CodeDescriptor& d = descriptors[i];
        const std::string code = kSection94FullTable[i];

        // SA-15 注册表侧强化（confirmable⇒requiresComparison）——全表
        // confirmable=false，不变量恒成立。
        EXPECT_TRUE(!d.confirmable || d.requiresComparison) << code;

        // 比较型标记恰三码 true（卡面点名锚点——从严执行）。
        EXPECT_EQ(d.requiresComparison, kExpectedCategoryRows[i].requiresComparison)
            << code << " 比较型标记失同步";
        if (d.requiresComparison) { ++comparisonCount; }

        // 分类/重试族逐码核对（DiagCodes.cpp 逐码注释的机器面）。
        EXPECT_EQ(d.category, kExpectedCategoryRows[i].category)
            << code << " 分类落值失同步";
        EXPECT_EQ(d.retryable, kExpectedCategoryRows[i].retryable)
            << code << " 重试族失同步";
    }
    // 比较型总数钉死（§9.4"比较型"列恰三行"是"——多出即私造比较语义）。
    EXPECT_EQ(comparisonCount, 3U)
        << "§9.4 比较型列恰三码（SAMPLE-GAP/DIMENSION-MISMATCH/"
           "FD-CONSISTENCY-FAILED）";
}

/**
 * 真实注册表注册成功且可查询（acceptance 2——"装配期注册"执行面的真实
 * 行为验证；kinematics P-KIN-7 处置同款）：注册后全表 16 码 find 命中且
 * ownerUnit 归属正确、registeredCodes("dynamics") 反映全表、manifest 含
 * 全表。DYN 前缀已在 kPrefixOwners 在册——注册无前缀阻塞（与 DT 前缀
 * 的 P-DT-7 收编路径不同，kinematics KIN 同款在册形态）。
 */
TEST(DynDiagCodes, RegistersIntoStableCodeRegistry_WP17T02_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"ERR-01", "NFR-MNT-03"},
                  std::vector<std::string>{});

    StableCodeRegistry registry;
    registerDynamicsCodes(registry);

    // 全表 find 命中（查询非抛）且归属正确。
    for (const char* code : kSection94FullTable) {
        const CodeDescriptor* found = registry.find(code);
        ASSERT_NE(found, nullptr) << "注册后码必须可 find 命中: " << code;
        EXPECT_EQ(found->ownerUnit, "dynamics") << code;
    }

    // ownerUnit 反查反映全表（字典序由注册表侧承担——16 码无缺漏）。
    const auto codes = registry.registeredCodes("dynamics");
    ASSERT_EQ(codes.size(), std::size(kSection94FullTable));
    for (const char* code : kSection94FullTable) {
        EXPECT_NE(std::find(codes.begin(), codes.end(), code), codes.end())
            << "registeredCodes 缺码: " << code;
    }

    // manifest 含全表（跨进程一致性面——同注册集同摘要）。
    const auto manifest = registry.manifest();
    ASSERT_EQ(manifest.entries.size(), std::size(kSection94FullTable));
    for (const char* code : kSection94FullTable) {
        const auto it = std::find_if(manifest.entries.begin(), manifest.entries.end(),
                                     [&](const CodeDescriptor& d) {
                                         return d.code == code;
                                     });
        EXPECT_TRUE(it != manifest.entries.end()) << "manifest 缺码: " << code;
    }
}

/**
 * 重复注册被拒（acceptance 2——NFR-MNT-03 码值唯一权威：同码二次注册
 * 抛 DiagnosticsError 不吞——装配期 fail-fast）。
 */
TEST(DynDiagCodes, DuplicateRegistrationRejected_WP17T02_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-MNT-03"},
                  std::vector<std::string>{});

    StableCodeRegistry registry;
    registerDynamicsCodes(registry);
    // 全表首码再注册（首版描述符）→DuplicateCode（diagnostics §9.1 行为
    // 冻结表）——不捕获类型细节，只断言"抛 DiagnosticsError"即实现契约
    // 达成。
    const auto descriptors = dynamicsCodeDescriptors();
    EXPECT_THROW(registry.registerCode(descriptors[0]), DiagnosticsError);
}
