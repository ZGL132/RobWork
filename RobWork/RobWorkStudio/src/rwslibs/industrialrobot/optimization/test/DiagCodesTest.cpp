/**
 * @file   DiagCodesTest.cpp
 * @brief  optimization 稳定诊断码工厂用例组（OptDiagCodes）——OPT-* 码
 *         描述符登记值（§6.6 登记表逐字段）、真实 StableCodeRegistry
 *         注册行为与全表清单纪律（任务契约 WP-20-T02 acceptance 2
 *         "OPT-* 15 码按 units/optimization.md §6.6 登记表装配期注册"
 *         的实现侧自证面）。
 *
 * 设计依据：
 *   - units/optimization.md §6.6（登记表 v1 设计基线——15 行：码/severity/
 *     语义/备注四列＋"失败语义归类"段三组划分；"前缀 OPT 已在 diagnostics
 *     §4.5 业务域命名空间清单登记；本表为域码的设计登记，实现随
 *     WP-20-T02 落位经 IDiagnosticRegistry::registerCode 注册"）、§16.3
 *     P-OPT-1（阶段锁码落位 OPT-STAGE-LOCKED——需求引用名 IRD-OPT-STAGE-
 *     LOCKED 的前缀归一形态，对应关系登记于卡内）
 *   - units/diagnostics.md §4.3（分类词表）、§4.4（动作族映射）、§4.5
 *     （注册协议——句法/前缀-所有权/键唯一/paramSchema/字段不变量）、
 *     §9.1（StableCodeRegistry 行为冻结表）
 *   - 收编确认锚点（如实声明，kinematics/dynamics/trajectory
 *     DiagCodesTest.cpp 同款）：OPT-* 码进入 diagnostics 全局装配（L5
 *     装配清单调用 registerOptimizationCodes）属装配侧动作——本任务产出
 *     注册面并以真实注册表验证，不在实现侧私自扩编 diagnostics 单元文件
 *     （allowedFiles 红线）；OPT 前缀已在注册表前缀-所有权表在册
 *     （kPrefixOwners {"OPT","optimization"} 行，2026-10-07 实测）——注册
 *     无前缀阻塞
 *   - 先例：trajectory/test/DiagCodesTest.cpp（WP-16-T03 同款五用例形态
 *     ——全表范围/逐字段/分类映射/真实注册/重复拒绝）
 *   - 任务契约 tasks/foundation/WP-20-T02.json acceptance 2
 */

#include <sdurws/ird/optimization/DiagCodes.hpp>

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
using sdurws::ird::optimization::registerOptimizationCodes;
using sdurws::ird::optimization::optimizationCodeDescriptors;

namespace {

/// units/optimization.md §6.6 登记表的**应登记码面**（测试内自持字面清单
/// ——与实现清单机械比对，任一侧漂移即失败：失同步防线）。行序＝§6.6
/// 表行序（实现清单序＝确定性序的依据面）；卡面增码（随 WP-21 OPT-D 扩展
/// 先走 §6.6 增量修订）时在实现清单与本清单表尾同步追加——既有行不重排。
const char* kSection66FullTable[] = {
    "OPT-STAGE-LOCKED",                 // 行 1（阶段锁——P-OPT-1 落位码）
    "OPT-INPUT-INVALID",                // 行 2（研究定义/配置非法）
    "OPT-VAR-LOCKED",                   // 行 3（补丁触及锁定/未授权变量）
    "OPT-VAR-UNBINDABLE",               // 行 4（绑定到当前基线不可绑定字段）
    "OPT-PATCH-ILLEGAL",                // 行 5（补丁非法——未知/重复/越界/序列化失败）
    "OPT-CANDIDATE-COMPILE-FAILED",     // 行 6（候选编译失败——透传 RT-*）
    "OPT-TOPOLOGY-REJECTED",            // 行 7（拓扑/链型不被当前启用范围支持）
    "OPT-EVALUATOR-MISSING",            // 行 8（阶段必需评估器未注册/契约版本不符）
    "OPT-PREFLIGHT-BLOCKED",            // 行 9（Preflight 存在阻塞项）
    "OPT-METRIC-NOT-COMPUTABLE",        // 行 10（指标不可算——显示"—"）
    "OPT-COMBO-OUT-OF-SCOPE",           // 行 11（离散器件/直线传动/耦合链超范围）
    "OPT-ROBUSTNESS-PROTOCOL-MISSING",  // 行 12（P-04 未冻结）
    "OPT-SEARCH-EMPTY",                 // 行 13（搜索未产生合法候选——非不可行）
    "OPT-EXPORT-CONTRACT-STALE",        // 行 14（导出契约过期——阻断正式导出）
    "OPT-APPLY-PLAN-INVALID",           // 行 15（候选应用组装非法）
};

/// §6.6"severity"列（与码清单同序——逐码登记值）：全表 11 error＋4 warning。
const bool kSection66IsError[] = {
    true,   // OPT-STAGE-LOCKED                 error
    true,   // OPT-INPUT-INVALID                error
    true,   // OPT-VAR-LOCKED                   error
    false,  // OPT-VAR-UNBINDABLE               warning
    true,   // OPT-PATCH-ILLEGAL                error
    true,   // OPT-CANDIDATE-COMPILE-FAILED     error
    true,   // OPT-TOPOLOGY-REJECTED            error
    true,   // OPT-EVALUATOR-MISSING            error
    false,  // OPT-PREFLIGHT-BLOCKED            warning
    false,  // OPT-METRIC-NOT-COMPUTABLE        warning
    true,   // OPT-COMBO-OUT-OF-SCOPE           error
    true,   // OPT-ROBUSTNESS-PROTOCOL-MISSING  error
    false,  // OPT-SEARCH-EMPTY                 warning
    true,   // OPT-EXPORT-CONTRACT-STALE        error
    true,   // OPT-APPLY-PLAN-INVALID           error
};

/// 逐码分类/重试族登记值（与码清单同序；落值锚点＝卡面 §6.6"失败语义
/// 归类"段三组划分＋DiagCodes.cpp 逐码注释——本表是其机器核对面，任一侧
/// 漂移即失败）。
struct ExpectedCategoryRow {
    DiagnosticCategory category;
    RetryKind retryable;
};

const ExpectedCategoryRow kExpectedCategoryRows[] = {
    {DiagnosticCategory::InputInvalid,     RetryKind::UserRetry},  // STAGE-LOCKED（修正研究定义后重新 Preflight——fix-input）
    {DiagnosticCategory::InputInvalid,     RetryKind::UserRetry},  // INPUT-INVALID（绑定互斥/预算非法/种子 0——fix-input）
    {DiagnosticCategory::InputInvalid,     RetryKind::UserRetry},  // VAR-LOCKED（显式授权后可行——fix-input）
    {DiagnosticCategory::DataInsufficient, RetryKind::UserRetry},  // VAR-UNBINDABLE（基线事实素材缺口——supply-evidence）
    {DiagnosticCategory::InputInvalid,     RetryKind::UserRetry},  // PATCH-ILLEGAL（修正补丁值——fix-input）
    {DiagnosticCategory::ExecutionFailed,  RetryKind::UserRetry},  // CANDIDATE-COMPILE-FAILED（环境面透传 cause——retry-task）
    {DiagnosticCategory::InputInvalid,     RetryKind::UserRetry},  // TOPOLOGY-REJECTED（换链型或等 R2 前置——fix-input）
    {DiagnosticCategory::FormatOrVersion,  RetryKind::UserRetry},  // EVALUATOR-MISSING（未注册/契约版本不符——契约面）
    {DiagnosticCategory::DataInsufficient, RetryKind::UserRetry},  // PREFLIGHT-BLOCKED（数据面目录条目——fix-input）
    {DiagnosticCategory::DataInsufficient, RetryKind::UserRetry},  // METRIC-NOT-COMPUTABLE（显示"—"不判不可行——supply-evidence）
    {DiagnosticCategory::InputInvalid,     RetryKind::UserRetry},  // COMBO-OUT-OF-SCOPE（移除范围外绑定——fix-input）
    {DiagnosticCategory::FormatOrVersion,  RetryKind::UserRetry},  // ROBUSTNESS-PROTOCOL-MISSING（P-04 冻结后启用——契约面）
    {DiagnosticCategory::DataInsufficient, RetryKind::UserRetry},  // SEARCH-EMPTY（数据不足语义非不可行——retry-task）
    {DiagnosticCategory::FormatOrVersion,  RetryKind::UserRetry},  // EXPORT-CONTRACT-STALE（契约版本过期——refresh 后重导出）
    {DiagnosticCategory::InputInvalid,     RetryKind::UserRetry},  // APPLY-PLAN-INVALID（修正应用来源——fix-input）
};

}  // namespace

/**
 * 工厂清单范围＝§6.6 登记表全表（15 行——acceptance 2"按
 * units/optimization.md §6.6 登记表"）：清单恰含全表且行序＝§6.6 表行序，
 * 多登（登记卡面之外的码值——私造码）或少登（漏登记——装配数据源
 * 缺口）均失败；码值文本与卡面"码"列原文逐字一致；无重复码（键唯一
 * ——注册协议前置面）；全表 OPT- 前缀（前缀即所有权声明）。
 */
TEST(OptDiagCodes, FactoryScopeIsSection66FullTable_WP20T02_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"ERR-01", "NFR-MNT-03"},
                  std::vector<std::string>{"OPT-03"});

    const auto descriptors = optimizationCodeDescriptors();
    ASSERT_EQ(descriptors.size(), std::size(kSection66FullTable))
        << "工厂清单规模＝§6.6 登记表行数（acceptance 2——清单＝卡面全表 15 码）";

    // 行序＝§6.6 表行序（确定性序——逐位比对，不查表不排序）。
    for (std::size_t i = 0; i < descriptors.size(); ++i) {
        EXPECT_EQ(descriptors[i].code, kSection66FullTable[i])
            << "§6.6 行序/码值失同步 @ 行 " << (i + 1);
    }

    // 无重复：键唯一是 diagnostics 注册协议（§4.5）的前置——重复码在
    // 登记表层面即暴露，不等注册期才失败。
    for (std::size_t i = 0; i < descriptors.size(); ++i) {
        for (std::size_t j = i + 1; j < descriptors.size(); ++j) {
            EXPECT_NE(descriptors[i].code, descriptors[j].code)
                << "重复码值（键唯一前置——§4.5）: " << descriptors[i].code;
        }
    }

    // 全表 OPT- 前缀：前缀即所有权声明——optimization 登记表不得混入他
    // 单元前缀（前缀-所有权表按 OPT→optimization 校验）。
    for (const auto& d : descriptors) {
        EXPECT_EQ(d.code.substr(0, 4), std::string_view{"OPT-"})
            << "非 OPT- 前缀码混入登记表: " << d.code;
    }
}

/**
 * P-OPT-1 落位口径（契约 knownPitfalls 点名项）：阶段锁码落位
 * OPT-STAGE-LOCKED（diagnostics §4.5"首段＝单元短前缀"硬约定），与需求
 * 文本引用名 IRD-OPT-STAGE-LOCKED 的对应关系登记于卡面 §6.6/§16.3——
 * 本用例钉住落位码文本与登记位置（§6.6 表行 1），防止后续任务以需求
 * 引用名直接拼码（那样会被注册表前缀校验拒绝：IRD- 前缀不在所有权表）。
 */
TEST(OptDiagCodes, StageLockedCodeFallsAtOptPrefix_WP20T02_POPT1)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-03"},
                  std::vector<std::string>{});

    const auto descriptors = optimizationCodeDescriptors();
    ASSERT_FALSE(descriptors.empty());
    // §6.6 表行 1 即阶段锁码（清单序＝表行序——首个描述符必为 STAGE-LOCKED）。
    EXPECT_EQ(descriptors[0].code, "OPT-STAGE-LOCKED")
        << "阶段锁码落位文本必须＝OPT-STAGE-LOCKED（P-OPT-1——前缀归一；"
           "需求引用名 IRD-OPT-STAGE-LOCKED 的对应关系登记于卡面 §6.6/§16.3）";
    EXPECT_EQ(descriptors[0].severity, DiagnosticSeverity::Error)
        << "阶段锁是运行启动阻塞（§6.5——非候选淘汰、不静默降级）";
}

/**
 * 描述符逐字段＝§6.6 行登记值（acceptance 2——登记值可追溯到卡面）：
 * 严重度列 15 码逐位核对（error×11＋warning×4）；ownerUnit、文案键命名
 * 约定（diag.<code-lower>.title/.detail——注册期键形校验的预演核对）、
 * paramSchema 全表"[]"（不私造参数名）、确认/可见性/登记版本全表同值、
 * requiresComparison 全表 false（§6.6 表无比较型列——零码点名比较语义）。
 */
TEST(OptDiagCodes, DescriptorFieldsMatchSection66Rows_WP20T02_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"ERR-01", "NFR-MNT-03"},
                  std::vector<std::string>{});

    const auto descriptors = optimizationCodeDescriptors();
    ASSERT_EQ(descriptors.size(), std::size(kSection66FullTable));
    ASSERT_EQ(std::size(kSection66IsError), std::size(kSection66FullTable));

    for (std::size_t i = 0; i < descriptors.size(); ++i) {
        const CodeDescriptor& d = descriptors[i];
        const std::string code = kSection66FullTable[i];

        // 严重度列：§6.6"error"→Error、"warning"→Warning（逐位机械核对）。
        if (kSection66IsError[i]) {
            EXPECT_EQ(d.severity, DiagnosticSeverity::Error) << code;
        } else {
            EXPECT_EQ(d.severity, DiagnosticSeverity::Warning) << code;
        }

        // 所有权与键约定（P-DIAG-9 命名约定——小写连字符派生）。
        EXPECT_EQ(d.ownerUnit, "optimization") << code;
        std::string lower = code;
        for (char& ch : lower) {
            if (ch >= 'A' && ch <= 'Z') { ch = static_cast<char>(ch - 'A' + 'a'); }
        }
        EXPECT_EQ(d.titleKey, "diag." + lower + ".title") << code;
        EXPECT_EQ(d.detailKey, "diag." + lower + ".detail") << code;

        // paramSchema 全表"[]"（diagnostics 展开登记纪律——不私造参数名，
        // 参数面随各消费者任务按需登记并升 registryVersion）。
        EXPECT_EQ(d.paramSchema, "[]") << code;

        // 比较型全表 false（§6.6 表四列无比较型列——零码点名；私造比较
        // 语义会触发工厂侧实例 comparison 三要素强制）。
        EXPECT_FALSE(d.requiresComparison) << code;

        // 确认流/登记版本/废弃状态（全表同值——逐码注释共字段口径；
        // 阶段锁/Preflight 阻塞是运行启动拒绝面（§6.5）非 SA-15 确认面）。
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
 * 逐码分类/重试族（acceptance 2——卡面 §6.6"失败语义归类"段三组划分的
 * 机器核对面）：10 码第一组（input-invalid/format-or-version 二选一）、
 * 1 码 execution-failed（CANDIDATE-COMPILE-FAILED——全表唯一环境面）、
 * 4 码 data-insufficient（VAR-UNBINDABLE/PREFLIGHT-BLOCKED/
 * METRIC-NOT-COMPUTABLE/SEARCH-EMPTY）；SA-15 不变量（confirmable⇒
 * requiresComparison）全表成立；15 行分类/重试族与 DiagCodes.cpp 逐码
 * 注释一一对应；重试族全表 UserRetry（归类段三组均为可修复/可供给/
 * 可重算面——无 report-bug/none 族）。
 */
TEST(OptDiagCodes, CategoryAndRetryMappingMatchesAnchors_WP20T02_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"ERR-01"},
                  std::vector<std::string>{});

    const auto descriptors = optimizationCodeDescriptors();
    ASSERT_EQ(descriptors.size(), std::size(kExpectedCategoryRows));
    ASSERT_EQ(descriptors.size(), std::size(kSection66FullTable));

    std::size_t inputInvalidCount = 0;
    std::size_t formatOrVersionCount = 0;
    std::size_t executionFailedCount = 0;
    std::size_t dataInsufficientCount = 0;
    for (std::size_t i = 0; i < descriptors.size(); ++i) {
        const CodeDescriptor& d = descriptors[i];
        const std::string code = kSection66FullTable[i];

        // SA-15 注册表侧强化（confirmable⇒requiresComparison）——全表
        // confirmable=false，不变量恒成立。
        EXPECT_TRUE(!d.confirmable || d.requiresComparison) << code;

        // 分类/重试族逐码核对（DiagCodes.cpp 逐码注释的机器面）。
        EXPECT_EQ(d.category, kExpectedCategoryRows[i].category)
            << code << " 分类落值失同步";
        EXPECT_EQ(d.retryable, kExpectedCategoryRows[i].retryable)
            << code << " 重试族失同步";

        // 归类段三组计数器（与 §6.6 失败语义归类段的组划分对账）。
        switch (d.category) {
        case DiagnosticCategory::InputInvalid: ++inputInvalidCount; break;
        case DiagnosticCategory::FormatOrVersion: ++formatOrVersionCount; break;
        case DiagnosticCategory::ExecutionFailed: ++executionFailedCount; break;
        case DiagnosticCategory::DataInsufficient: ++dataInsufficientCount; break;
        default:
            ADD_FAILURE() << "分类落值超出 §6.6 归类段三组词面（§4.3 其余值"
                             "未被卡面点名）: " << code;
        }
    }
    // 组规模钉死（§6.6 归类段：第一组 10 码〔input-invalid/format-or-
    // version 二选一〕＋execution-failed 1 码＋data-insufficient 4 码
    // ——多出即私造分类语义）。
    EXPECT_EQ(inputInvalidCount + formatOrVersionCount, 10U)
        << "§6.6 第一组（调用方可修复/契约面）恰 10 码";
    EXPECT_EQ(executionFailedCount, 1U)
        << "§6.6 execution-failed 组恰 1 码（CANDIDATE-COMPILE-FAILED——透传 cause）";
    EXPECT_EQ(dataInsufficientCount, 4U)
        << "§6.6 data-insufficient 组恰 4 码（VAR-UNBINDABLE/PREFLIGHT-"
           "BLOCKED/METRIC-NOT-COMPUTABLE/SEARCH-EMPTY）";
}

/**
 * 真实注册表注册成功且可查询（acceptance 2——"装配期注册"执行面的真实
 * 行为验证；kinematics P-KIN-7 处置同款）：注册后全表 15 码 find 命中且
 * ownerUnit 归属正确、registeredCodes("optimization") 反映全表、manifest
 * 含全表。OPT 前缀已在 kPrefixOwners 在册——注册无前缀阻塞（与 DT 前缀
 * 的 P-DT-7 收编路径不同，KIN/TRJ/DYN 同款在册形态）。
 */
TEST(OptDiagCodes, RegistersIntoStableCodeRegistry_WP20T02_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"ERR-01", "NFR-MNT-03"},
                  std::vector<std::string>{});

    StableCodeRegistry registry;
    registerOptimizationCodes(registry);

    // 全表 find 命中（查询非抛）且归属正确。
    for (const char* code : kSection66FullTable) {
        const CodeDescriptor* found = registry.find(code);
        ASSERT_NE(found, nullptr) << "注册后码必须可 find 命中: " << code;
        EXPECT_EQ(found->ownerUnit, "optimization") << code;
    }

    // ownerUnit 反查反映全表（字典序由注册表侧承担——15 码无缺漏）。
    const auto codes = registry.registeredCodes("optimization");
    ASSERT_EQ(codes.size(), std::size(kSection66FullTable));
    for (const char* code : kSection66FullTable) {
        EXPECT_NE(std::find(codes.begin(), codes.end(), code), codes.end())
            << "registeredCodes 缺码: " << code;
    }

    // manifest 含全表（跨进程一致性面——同注册集同摘要）。
    const auto manifest = registry.manifest();
    ASSERT_EQ(manifest.entries.size(), std::size(kSection66FullTable));
    for (const char* code : kSection66FullTable) {
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
TEST(OptDiagCodes, DuplicateRegistrationRejected_WP20T02_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-MNT-03"},
                  std::vector<std::string>{});

    StableCodeRegistry registry;
    registerOptimizationCodes(registry);
    // 全表首码（＝P-OPT-1 落位码 OPT-STAGE-LOCKED）再注册（首版描述符）
    // →DuplicateCode（diagnostics §9.1 行为冻结表）——不捕获类型细节，
    // 只断言"抛 DiagnosticsError"即实现契约达成。
    const auto descriptors = optimizationCodeDescriptors();
    EXPECT_THROW(registry.registerCode(descriptors[0]), DiagnosticsError);
}
