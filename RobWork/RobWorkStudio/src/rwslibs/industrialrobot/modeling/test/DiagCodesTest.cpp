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
/// 清单机械比对，任一侧漂移即失败：失同步防线）。T03+ 任务落位时在实现
/// 清单与本清单表尾同步追加（分批纪律——其余行不得提前出现）。
const char* kT02Section95Codes[] = {
    "MDL-READINESS-SCHEMA-UNSUPPORTED",
};

/// §9.5 任务列含 T05 的行的应登记码面（WP-13-T05 登记——表行序＝工厂
/// 清单序；TEMPLATE-RANGE 为实现期增登行，单元卡 §14.6 v0.6 登记）。
const char* kT05Section95Codes[] = {
    "MDL-IMPORT-UNSUPPORTED-JOINT",
    "MDL-IMPORT-BRANCH-SELECTION",
    "MDL-IMPORT-ZERO-AXIS",
    "MDL-IMPORT-PENDING-CONFIRM",
    "MDL-IMPORT-TEMPLATE-RANGE",
};

/// §9.5 任务列含 T06 的行的应登记码面（WP-13-T06 实现期增登——单元卡
/// §14.6 v0.7 登记；Xacro 受控展开失败的语义定位面，io 护栏码透传面
/// 之外）。
const char* kT06Section95Codes[] = {
    "MDL-IMPORT-XACRO-UNRESOLVED",
};

/// §9.5 任务列含 T07 的行的应登记码面（WP-13-T07 实现期增登——单元卡
/// §14.6 v0.8 登记；§9.4.2"TemplateDisabled 附定位诊断"的码面落位；
/// 表行序追加于表尾——登记簿纪律不重排既有行）。
const char* kT07Section95Codes[] = {
    "MDL-TEMPLATE-DISABLED",
};

/// §9.5 任务列含 T08 的行的应登记码面（WP-13-T08 登记——单元卡 §14.6
/// v0.9；断言分域＋就绪校验＋行程确认族；含 v0.9 实现期增登行
/// MDL-READINESS-PHYSICS-MISSING——§9.3"物性缺失转 Warning 诊断随计划
/// 留痕"的码面落位；表行序追加于表尾——登记簿纪律不重排既有行）。
const char* kT08Section95Codes[] = {
    "MDL-06-TRAVEL-LIMIT",
    "MDL-ASSERT-MASS-NONPOSITIVE",
    "MDL-ASSERT-INERTIA-NOT-SPD",
    "MDL-ASSERT-INERTIA-TRIANGLE",
    "MDL-ASSERT-LIMIT-INTERVAL",
    "MDL-ASSERT-RANGE-NOT-FINITE",
    "MDL-READINESS-REF-MISSING",
    "MDL-READINESS-RESOURCE-STATE",
    "MDL-READINESS-PHYSICS-MISSING",
};

/// §9.5 任务列含 T09 的行的应登记码面（WP-13-T09 登记——单元卡 §14.6
/// v0.10；DH↔显式转换族三码；表行序追加于表尾——登记簿纪律不重排
/// 既有行）。
const char* kT09Section95Codes[] = {
    "MDL-DH-NOT-EXPRESSIBLE",
    "MDL-DH-APPROXIMATE",
    "MDL-DH-ANALYSIS-FAILED",
};

/// §9.5 任务列含 T10 的行的应登记码面（WP-13-T10 实现期增登——单元卡
/// §14.6 v0.12；引用保护＋defaultTcp 完整性两码：§4.8"被本域引用时移除
/// 被拒（I-MDL-9）"与 §4.3 defaultTcp 行"有 tools 时须已设置（KIN-14）"
/// 的码面落位；表行序追加于表尾——登记簿纪律不重排既有行）。
const char* kT10Section95Codes[] = {
    "MDL-REF-PROTECTED",
    "MDL-READINESS-DEFAULT-TCP-INCOMPLETE",
};

}  // namespace

/**
 * 工厂清单分批封闭性（acceptance 4——"按 §9.5 注册纪律只登记有消费者
 * 条目，不预建"）：清单恰含 §9.5 任务列含 T02/T05/T06/T07/T08/T09/T10
 * 的行——其余行（T13/T18 任务列）提前出现即"预建"违约；逐码等于卡面
 * 字面清单（不私定码值），清单序＝§9.5 表行序。分期登记随任务推进
 * 表尾追加（T05/T06/T07/T08/T09/T10 历次登记同款推进口径；T10 行钉住
 * 断言随 WP-13-T10 合法登记同步——T09 attempt 1 B-1 返工先例）。
 */
TEST(MdlDiagCodes, FactoryScopeIsStagedRows_WP13T10)
{
    IRD_TEST_INFO(std::vector<std::string>{"ERR-01", "MDL-06"},
                  std::vector<std::string>{});

    const auto descriptors = modelingCodeDescriptors();
    const std::size_t expectedCount = std::size(kT02Section95Codes)
                                      + std::size(kT05Section95Codes)
                                      + std::size(kT06Section95Codes)
                                      + std::size(kT07Section95Codes)
                                      + std::size(kT08Section95Codes)
                                      + std::size(kT09Section95Codes)
                                      + std::size(kT10Section95Codes);
    ASSERT_EQ(descriptors.size(), expectedCount)
        << "工厂清单应恰含 §9.5 T02/T05/T06/T07/T08/T09/T10 任务行（分批纪律：其余行随"
           "各自任务登记——不预建）";
    // 清单序＝§9.5 表行序（实现期增登行表尾追加）：T02 行在前，
    // T05/T06/T07/T08/T09/T10 行按登记序随后。
    for (std::size_t i = 0; i < std::size(kT02Section95Codes); ++i) {
        EXPECT_EQ(descriptors[i].code, std::string(kT02Section95Codes[i]))
            << "清单序 " << i << " 与 §9.5 卡面字面不符（不私定码值）";
    }
    std::size_t offset = std::size(kT02Section95Codes);
    for (std::size_t i = 0; i < std::size(kT05Section95Codes); ++i) {
        EXPECT_EQ(descriptors[offset + i].code,
                  std::string(kT05Section95Codes[i]))
            << "T05 清单序 " << i << " 与 §9.5 卡面字面不符（不私定码值）";
    }
    offset += std::size(kT05Section95Codes);
    for (std::size_t i = 0; i < std::size(kT06Section95Codes); ++i) {
        EXPECT_EQ(descriptors[offset + i].code,
                  std::string(kT06Section95Codes[i]))
            << "T06 清单序 " << i << " 与 §9.5 卡面字面不符（不私定码值）";
    }
    offset += std::size(kT06Section95Codes);
    for (std::size_t i = 0; i < std::size(kT07Section95Codes); ++i) {
        EXPECT_EQ(descriptors[offset + i].code,
                  std::string(kT07Section95Codes[i]))
            << "T07 清单序 " << i << " 与 §9.5 卡面字面不符（不私定码值）";
    }
    offset += std::size(kT07Section95Codes);
    for (std::size_t i = 0; i < std::size(kT08Section95Codes); ++i) {
        EXPECT_EQ(descriptors[offset + i].code,
                  std::string(kT08Section95Codes[i]))
            << "T08 清单序 " << i << " 与 §9.5 卡面字面不符（不私定码值）";
    }
    offset += std::size(kT08Section95Codes);
    for (std::size_t i = 0; i < std::size(kT09Section95Codes); ++i) {
        EXPECT_EQ(descriptors[offset + i].code,
                  std::string(kT09Section95Codes[i]))
            << "T09 清单序 " << i << " 与 §9.5 卡面字面不符（不私定码值）";
    }
    offset += std::size(kT09Section95Codes);
    for (std::size_t i = 0; i < std::size(kT10Section95Codes); ++i) {
        EXPECT_EQ(descriptors[offset + i].code,
                  std::string(kT10Section95Codes[i]))
            << "T10 清单序 " << i << " 与 §9.5 卡面字面不符（不私定码值）";
    }
}

/**
 * 描述符登记值逐字段锚定（§9.5 行＋diagnostics §4.5 字段约束）：公共面
 * （句法/前缀-所有权/文案键命名约定/非 Dev 三项/版本状态）逐码核查；
 * 卡面差异面（分类/级别/paramSchema/可重试性）逐码断言——T02 行
 * "校验/error→FormatOrVersion＋三键"，T05 行"导入/error|warning→
 * InputInvalid＋各自键集"（§9.5 行文逐列对照）。
 */
TEST(MdlDiagCodes, DescriptorFieldsMatchSection95Row_WP13T02_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"ERR-01", "NFR-MNT-03"},
                  std::vector<std::string>{});

    const auto descriptors = modelingCodeDescriptors();
    ASSERT_FALSE(descriptors.empty());
    const auto lowerOf = [](const std::string& code) {
        std::string lower;
        lower.reserve(code.size());
        std::transform(code.begin(), code.end(), std::back_inserter(lower),
                       [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        return lower;
    };
    for (const auto& d : descriptors) {
        // 句法（§4.5：^[A-Z0-9]+(-[A-Z0-9]+)*$ 且 ≤64）。
        EXPECT_TRUE(isValidDiagCodeSyntax(d.code)) << "码句法非法: " << d.code;
        // 前缀-所有权（§4.5 前缀表 MDL→modeling——注册期校验的同判据）。
        EXPECT_EQ(d.ownerUnit, "modeling");
        EXPECT_EQ(d.code.rfind("MDL-", 0), 0U)
            << "首段前缀必须与 ownerUnit 声明域一致（MDL→modeling）";
        // 文案键命名约定（P-DIAG-9：diag.<code-lower>.title/.detail）。
        const std::string lower = lowerOf(d.code);
        EXPECT_EQ(d.titleKey, "diag." + lower + ".title");
        EXPECT_EQ(d.detailKey, "diag." + lower + ".detail");
        // 非 Dev 码：可见/可报告/可入历史三项全 true（§4.5 Dev 强制 false
        // 的逆面）；首次登记版本、未废弃、无迁移目标、非比较型。
        EXPECT_TRUE(d.userVisible);
        EXPECT_TRUE(d.reportable);
        EXPECT_TRUE(d.historical);
        EXPECT_EQ(d.registryVersion, 1U);
        EXPECT_FALSE(d.deprecated);
        EXPECT_FALSE(d.supersededBy.has_value());
        // requiresComparison／confirmable 逐码断言（差异面——见各分支：
        // T08 行 TRAVEL-LIMIT 为 confirmable＋比较型强制；断言族为
        // 比较型强制但不可确认；其余非比较型）。
        // ---- 卡面差异面（§9.5 行逐列）----
        if (d.code == "MDL-READINESS-SCHEMA-UNSUPPORTED") {
            EXPECT_FALSE(d.confirmable);
            EXPECT_FALSE(d.requiresComparison);
            // "校验/error"→FormatOrVersion/Error；paramSchema 三键；UserRetry。
            EXPECT_EQ(d.category, DiagnosticCategory::FormatOrVersion);
            EXPECT_EQ(d.severity, DiagnosticSeverity::Error);
            EXPECT_NE(d.paramSchema.find("\"object-type\""), std::string::npos);
            EXPECT_NE(d.paramSchema.find("\"schema-version\""), std::string::npos);
            EXPECT_NE(d.paramSchema.find("\"supported-major\""), std::string::npos);
            EXPECT_EQ(d.retryable, RetryKind::UserRetry);
        } else if (d.code == "MDL-IMPORT-UNSUPPORTED-JOINT") {
            // "导入/error"→InputInvalid/Error（源文件关节类型不可表达——
            // 输入非法族）；paramSchema [joint-name, source-type]；UserRetry。
            EXPECT_EQ(d.category, DiagnosticCategory::InputInvalid);
            EXPECT_EQ(d.severity, DiagnosticSeverity::Error);
            EXPECT_NE(d.paramSchema.find("\"joint-name\""), std::string::npos);
            EXPECT_NE(d.paramSchema.find("\"source-type\""), std::string::npos);
            EXPECT_EQ(d.retryable, RetryKind::UserRetry);
        } else if (d.code == "MDL-IMPORT-BRANCH-SELECTION") {
            // "导入/info"→InputInvalid/Info（输入不足以唯一定义模型——需
            // 用户选链）；paramSchema [branch-count, branch-roots]；UserRetry。
            EXPECT_EQ(d.category, DiagnosticCategory::InputInvalid);
            EXPECT_EQ(d.severity, DiagnosticSeverity::Info);
            EXPECT_NE(d.paramSchema.find("\"branch-count\""), std::string::npos);
            EXPECT_NE(d.paramSchema.find("\"branch-roots\""), std::string::npos);
            EXPECT_EQ(d.retryable, RetryKind::UserRetry);
        } else if (d.code == "MDL-IMPORT-ZERO-AXIS") {
            // "导入/error"→InputInvalid/Error（零轴/非有限轴）；paramSchema
            // [joint-name, axis-raw]；UserRetry（"修正源文件"）。
            EXPECT_EQ(d.category, DiagnosticCategory::InputInvalid);
            EXPECT_EQ(d.severity, DiagnosticSeverity::Error);
            EXPECT_NE(d.paramSchema.find("\"joint-name\""), std::string::npos);
            EXPECT_NE(d.paramSchema.find("\"axis-raw\""), std::string::npos);
            EXPECT_EQ(d.retryable, RetryKind::UserRetry);
        } else if (d.code == "MDL-IMPORT-PENDING-CONFIRM") {
            // "导入/Warning"→InputInvalid/Warning（输入不完整待逐条确认）；
            // paramSchema [item-kind, subject]；UserRetry（"逐条确认"）。
            EXPECT_EQ(d.category, DiagnosticCategory::InputInvalid);
            EXPECT_EQ(d.severity, DiagnosticSeverity::Warning);
            EXPECT_NE(d.paramSchema.find("\"item-kind\""), std::string::npos);
            EXPECT_NE(d.paramSchema.find("\"subject\""), std::string::npos);
            EXPECT_EQ(d.retryable, RetryKind::UserRetry);
        } else if (d.code == "MDL-IMPORT-TEMPLATE-RANGE") {
            // 增登行"导入/info"→InfeasibilityProof/Info（能力边界结论——
            // 有效工程结论而非错误）；paramSchema [movable-axes,
            // prismatic-present]；Never（结论呈现类）。
            EXPECT_EQ(d.category, DiagnosticCategory::InfeasibilityProof);
            EXPECT_EQ(d.severity, DiagnosticSeverity::Info);
            EXPECT_NE(d.paramSchema.find("\"movable-axes\""), std::string::npos);
            EXPECT_NE(d.paramSchema.find("\"prismatic-present\""), std::string::npos);
            EXPECT_EQ(d.retryable, RetryKind::Never);
        } else if (d.code == "MDL-IMPORT-XACRO-UNRESOLVED") {
            // 增登行"导入/error"→InputInvalid/Error（Xacro 受控展开语义
            // 失败——未定义宏/参数等，源输入非法族）；paramSchema
            // [item-kind, symbol]；UserRetry（"改写源文件"）。
            EXPECT_EQ(d.category, DiagnosticCategory::InputInvalid);
            EXPECT_EQ(d.severity, DiagnosticSeverity::Error);
            EXPECT_NE(d.paramSchema.find("\"item-kind\""), std::string::npos);
            EXPECT_NE(d.paramSchema.find("\"symbol\""), std::string::npos);
            EXPECT_EQ(d.retryable, RetryKind::UserRetry);
        } else if (d.code == "MDL-TEMPLATE-DISABLED") {
            // 增登行"模板/info"→InfeasibilityProof/Info（模板存在且已登记，
            // 仅 P-03 冻结门未过——有效工程结论而非错误）；paramSchema
            // [template-id, freeze-gate]；UserRetry（"选择已启用模板"）。
            EXPECT_EQ(d.category, DiagnosticCategory::InfeasibilityProof);
            EXPECT_EQ(d.severity, DiagnosticSeverity::Info);
            EXPECT_NE(d.paramSchema.find("\"template-id\""), std::string::npos);
            EXPECT_NE(d.paramSchema.find("\"freeze-gate\""), std::string::npos);
            EXPECT_EQ(d.retryable, RetryKind::UserRetry);
        } else if (d.code == "MDL-06-TRAVEL-LIMIT") {
            // T08 行"比较型/Warning"→Confirmable/Warning（策略校验超限待
            // 显式确认——SA-15/MDL-06④）；paramSchema "[]"（定位走
            // subject＋comparison）；UserRetry（"确认放行或改行程"）。
            EXPECT_EQ(d.category, DiagnosticCategory::Confirmable);
            EXPECT_EQ(d.severity, DiagnosticSeverity::Warning);
            EXPECT_TRUE(d.confirmable);
            EXPECT_TRUE(d.requiresComparison);   // 可确认必为比较型（C-1 强化）
            EXPECT_EQ(d.paramSchema, "[]");
            EXPECT_EQ(d.retryable, RetryKind::UserRetry);
        } else if (d.code == "MDL-ASSERT-MASS-NONPOSITIVE"
                   || d.code == "MDL-ASSERT-INERTIA-NOT-SPD"
                   || d.code == "MDL-ASSERT-INERTIA-TRIANGLE"
                   || d.code == "MDL-ASSERT-LIMIT-INTERVAL") {
            // T08 断言族"断言/error"→InputInvalid/Error（MDL-06 硬断言
            // 就地阻止——无放行分支）；paramSchema "[]"；比较型强制
            //（三要素齐备——精确定位）。
            EXPECT_EQ(d.category, DiagnosticCategory::InputInvalid);
            EXPECT_EQ(d.severity, DiagnosticSeverity::Error);
            EXPECT_FALSE(d.confirmable);
            EXPECT_TRUE(d.requiresComparison);
            EXPECT_EQ(d.paramSchema, "[]");
            EXPECT_EQ(d.retryable, RetryKind::UserRetry);
        } else if (d.code == "MDL-ASSERT-RANGE-NOT-FINITE") {
            // 未确认分支无值可比→requiresComparison=false（ERR-01 不伪造
            // 数值）；paramSchema "[]"；UserRetry（"确认范围"）。
            EXPECT_EQ(d.category, DiagnosticCategory::InputInvalid);
            EXPECT_EQ(d.severity, DiagnosticSeverity::Error);
            EXPECT_FALSE(d.confirmable);
            EXPECT_FALSE(d.requiresComparison);
            EXPECT_EQ(d.paramSchema, "[]");
            EXPECT_EQ(d.retryable, RetryKind::UserRetry);
        } else if (d.code == "MDL-READINESS-REF-MISSING") {
            // T08 行"校验/error"→ResourceMissing/Error（闭包内引用缺失面）。
            EXPECT_EQ(d.category, DiagnosticCategory::ResourceMissing);
            EXPECT_EQ(d.severity, DiagnosticSeverity::Error);
            EXPECT_FALSE(d.confirmable);
            EXPECT_FALSE(d.requiresComparison);
            EXPECT_EQ(d.paramSchema, "[]");
            EXPECT_EQ(d.retryable, RetryKind::UserRetry);
        } else if (d.code == "MDL-READINESS-RESOURCE-STATE") {
            // T08 行"校验/Warning"→ResourceMissing/Warning（未固化提示——
            // 不阻断应用，CON-03 固化激励）。
            EXPECT_EQ(d.category, DiagnosticCategory::ResourceMissing);
            EXPECT_EQ(d.severity, DiagnosticSeverity::Warning);
            EXPECT_FALSE(d.confirmable);
            EXPECT_FALSE(d.requiresComparison);
            EXPECT_EQ(d.paramSchema, "[]");
            EXPECT_EQ(d.retryable, RetryKind::UserRetry);
        } else if (d.code == "MDL-READINESS-PHYSICS-MISSING") {
            // 增登行"校验/Warning"→DataInsufficient/Warning（V15-01 缺失
            // 降级预告——缺失无值可比，不伪造数值）。
            EXPECT_EQ(d.category, DiagnosticCategory::DataInsufficient);
            EXPECT_EQ(d.severity, DiagnosticSeverity::Warning);
            EXPECT_FALSE(d.confirmable);
            EXPECT_FALSE(d.requiresComparison);
            EXPECT_EQ(d.paramSchema, "[]");
            EXPECT_EQ(d.retryable, RetryKind::UserRetry);
        } else if (d.code == "MDL-DH-NOT-EXPRESSIBLE") {
            // T09 行"转换/error"→InfeasibilityProof/Error（结构前提终判
            // ——有效工程结论而非输入错误，TEMPLATE-RANGE 同判例）；
            // paramSchema "[]"；UserRetry（"改链结构后重试"）。
            EXPECT_EQ(d.category, DiagnosticCategory::InfeasibilityProof);
            EXPECT_EQ(d.severity, DiagnosticSeverity::Error);
            EXPECT_FALSE(d.confirmable);
            EXPECT_FALSE(d.requiresComparison);
            EXPECT_EQ(d.paramSchema, "[]");
            EXPECT_EQ(d.retryable, RetryKind::UserRetry);
        } else if (d.code == "MDL-DH-APPROXIMATE") {
            // T09 行"转换/Warning"→InfeasibilityProof/Warning（收敛的数值
            // 事实＋权威资格否定——C-4 不可确认放行）；paramSchema "[]"；
            // 比较型强制（最坏轴角偏差 vs 第 5 项上界）；UserRetry。
            EXPECT_EQ(d.category, DiagnosticCategory::InfeasibilityProof);
            EXPECT_EQ(d.severity, DiagnosticSeverity::Warning);
            EXPECT_FALSE(d.confirmable);
            EXPECT_TRUE(d.requiresComparison);
            EXPECT_EQ(d.paramSchema, "[]");
            EXPECT_EQ(d.retryable, RetryKind::UserRetry);
        } else if (d.code == "MDL-DH-ANALYSIS-FAILED") {
            // T09 行"转换/error"→ExecutionFailed/Error（求解器数值失败
            // ——执行失败轴 TASK-02，不构成语义结论）；paramSchema "[]"；
            // 非比较型（无数值比对值——NFR-COR-03）；UserRetry（"调整后
            // 重试"）。
            EXPECT_EQ(d.category, DiagnosticCategory::ExecutionFailed);
            EXPECT_EQ(d.severity, DiagnosticSeverity::Error);
            EXPECT_FALSE(d.confirmable);
            EXPECT_FALSE(d.requiresComparison);
            EXPECT_EQ(d.paramSchema, "[]");
            EXPECT_EQ(d.retryable, RetryKind::UserRetry);
        } else if (d.code == "MDL-REF-PROTECTED") {
            // T10 增登行"校验/error"→InputInvalid/Error（移除请求在当前
            // 引用状态下不可执行——V-04 引用保护，I-MDL-9）；paramSchema
            // [object-id, reference-holder]；比较型强制（引用计数
            // actual=1/expected=0——V-04"比较型定位"）；UserRetry
            //（"先解除引用再移除"）。
            EXPECT_EQ(d.category, DiagnosticCategory::InputInvalid);
            EXPECT_EQ(d.severity, DiagnosticSeverity::Error);
            EXPECT_FALSE(d.confirmable);
            EXPECT_TRUE(d.requiresComparison);
            EXPECT_NE(d.paramSchema.find("\"object-id\""), std::string::npos);
            EXPECT_NE(d.paramSchema.find("\"reference-holder\""), std::string::npos);
            EXPECT_EQ(d.retryable, RetryKind::UserRetry);
        } else if (d.code == "MDL-READINESS-DEFAULT-TCP-INCOMPLETE") {
            // T10 增登行"校验/error"→ResourceMissing/Error（defaultTcp
            // 引用完整性族——REF-MISSING 同分类口径，I-MDL-9/KIN-14/L7）；
            // paramSchema [tcp-key, tool-id]；非比较型（缺失/悬空无值可比
            // ——NFR-COR-03）；UserRetry（"修复 defaultTcp 或 TCP 表"）。
            EXPECT_EQ(d.category, DiagnosticCategory::ResourceMissing);
            EXPECT_EQ(d.severity, DiagnosticSeverity::Error);
            EXPECT_FALSE(d.confirmable);
            EXPECT_FALSE(d.requiresComparison);
            EXPECT_NE(d.paramSchema.find("\"tcp-key\""), std::string::npos);
            EXPECT_NE(d.paramSchema.find("\"tool-id\""), std::string::npos);
            EXPECT_EQ(d.retryable, RetryKind::UserRetry);
        } else {
            FAIL() << "未登记的码面出现（分批纪律——不预建）: " << d.code;
        }
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
