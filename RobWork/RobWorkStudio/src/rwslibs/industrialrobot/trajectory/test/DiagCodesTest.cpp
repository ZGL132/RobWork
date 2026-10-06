/**
 * @file   DiagCodesTest.cpp
 * @brief  trajectory 稳定诊断码工厂用例组（TrjDiagCodes）——TRJ-* 码
 *         描述符登记值（§14.4 登记表逐字段）、真实 StableCodeRegistry
 *         注册行为与全表清单纪律（任务契约 WP-16-T03 acceptance 3
 *         "TRJ-* 域诊断码按 units/trajectory.md 登记表在装配期注册"的
 *         实现侧自证面）。
 *
 * 设计依据：
 *   - units/trajectory.md §14.4（拟注册清单 v1 设计基线——12 行：码/
 *     严重度/比较型/用途四列；"码值权威归 StableCodeRegistry，本表为
 *     拟注册清单——WP-16-T03/T09 注册时按协议提交"）、§4.2（布局表
 *     DiagCodes.hpp 行——T03 落位）
 *   - units/diagnostics.md §4.3（分类词表）、§4.4（动作族映射）、§4.5
 *     （注册协议——句法/前缀-所有权/键唯一/paramSchema/字段不变量）、
 *     §9.1（StableCodeRegistry 行为冻结表）
 *   - 收编确认锚点（如实声明，kinematics/dynamics DiagCodesTest.cpp
 *     同款）：TRJ-* 码进入 diagnostics 全局装配（L5 装配清单调用
 *     registerTrajectoryCodes）属装配侧动作——本任务产出注册面并以真实
 *     注册表验证，不在实现侧私自扩编 diagnostics 单元文件（allowedFiles
 *     红线）；TRJ 前缀已在注册表前缀-所有权表在册（kPrefixOwners
 *     {"TRJ","trajectory"} 行）——注册无前缀阻塞
 *   - 任务契约 tasks/foundation/WP-16-T03.json acceptance 3
 */

#include <sdurws/ird/trajectory/DiagCodes.hpp>

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
using sdurws::ird::trajectory::registerTrajectoryCodes;
using sdurws::ird::trajectory::trajectoryCodeDescriptors;

namespace {

/// units/trajectory.md §14.4 登记表的**应登记码面**（测试内自持字面清单
/// ——与实现清单机械比对，任一侧漂移即失败：失同步防线）。行序＝§14.4
/// 表行序（实现清单序＝确定性序的依据面）；卡面增码（随消费任务 T04+
/// 先走 §14.4 增量修订）时在实现清单与本清单表尾同步追加——既有行不
/// 重排。
const char* kSection144FullTable[] = {
    "TRJ-INPUT-INVALID",               // 行 1（输入非法——序列环/悬空引用/配置/模式组合）
    "TRJ-NO-PATH",                     // 行 2（无路径——IK 无解/规划失败段定位）
    "TRJ-BRANCH-JUMP",                 // 行 3（关节分支跳变——附采样点与两端解摘要）
    "TRJ-SINGULAR-NEIGHBORHOOD",       // 行 4（奇异邻域——比较型：条件数/阈值/1）
    "TRJ-LIMIT-EXCEEDED",              // 行 5（限值超标——比较型：实际/期望/单位）
    "TRJ-CONTINUITY-BROKEN",           // 行 6（连续性破坏——比较型：实际差/容差/单位）
    "TRJ-RECHECK-COLLISION",           // 行 7（平滑后重新碰撞——段定位＋对象对）
    "TRJ-RECHECK-BUDGET-EXHAUSTED",    // 行 8（细分预算耗尽——比较型：实际步长/预算）
    "TRJ-RECHECK-DATA-INSUFFICIENT",   // 行 9（复检验证器/碰撞证据缺失——KIN-05 口径）
    "TRJ-TIME-PARAM-FAILED",           // 行 10（时间参数化失败——含限值未定义）
    "TRJ-CYCLE-TIME-EXCEEDED",         // 行 11（目标节拍超标——比较型：实际/目标/s）
    "TRJ-EXPORT-FAILED",               // 行 12（导出失败——io 通道错误转译）
};

/// §14.4"严重度"列（与码清单同序——逐码登记值）：全表 9 error＋3 warning。
const bool kSection144IsError[] = {
    true,   // TRJ-INPUT-INVALID              error
    true,   // TRJ-NO-PATH                    error
    false,  // TRJ-BRANCH-JUMP                warning
    false,  // TRJ-SINGULAR-NEIGHBORHOOD      warning
    true,   // TRJ-LIMIT-EXCEEDED             error
    true,   // TRJ-CONTINUITY-BROKEN          error
    true,   // TRJ-RECHECK-COLLISION          error
    true,   // TRJ-RECHECK-BUDGET-EXHAUSTED   error
    true,   // TRJ-RECHECK-DATA-INSUFFICIENT  error
    true,   // TRJ-TIME-PARAM-FAILED          error
    false,  // TRJ-CYCLE-TIME-EXCEEDED        warning
    true,   // TRJ-EXPORT-FAILED              error
};

/// §14.4"比较型"列（与码清单同序——逐码登记值）：恰 5 码点名比较型。
const bool kSection144RequiresComparison[] = {
    false,  // INPUT-INVALID
    false,  // NO-PATH
    false,  // BRANCH-JUMP
    true,   // SINGULAR-NEIGHBORHOOD（条件数/阈值/1）
    true,   // LIMIT-EXCEEDED（实际/期望/单位）
    true,   // CONTINUITY-BROKEN（实际差/容差/单位）
    false,  // RECHECK-COLLISION
    true,   // RECHECK-BUDGET-EXHAUSTED（实际步长/预算）
    false,  // RECHECK-DATA-INSUFFICIENT
    false,  // TIME-PARAM-FAILED
    true,   // CYCLE-TIME-EXCEEDED（实际节拍/目标/s）
    false,  // EXPORT-FAILED
};

/// 逐码分类/重试族登记值（与码清单同序；落值锚点＝DiagCodes.cpp 逐码
/// 注释——本表是其机器核对面，任一侧漂移即失败）。
struct ExpectedCategoryRow {
    DiagnosticCategory category;
    RetryKind retryable;
};

const ExpectedCategoryRow kExpectedCategoryRows[] = {
    {DiagnosticCategory::InputInvalid,     RetryKind::UserRetry},  // INPUT-INVALID（fix-input）
    {DiagnosticCategory::ExecutionFailed,  RetryKind::UserRetry},  // NO-PATH（retry-task——输入合法求解未果）
    {DiagnosticCategory::DataInsufficient, RetryKind::UserRetry},  // BRANCH-JUMP（段级明细素材——KIN 残差同构）
    {DiagnosticCategory::PolicyDenied,     RetryKind::UserRetry},  // SINGULAR-NEIGHBORHOOD（阈值读 policy——adjust-policy）
    {DiagnosticCategory::PolicyDenied,     RetryKind::UserRetry},  // LIMIT-EXCEEDED（行程上限硬口径锚点——adjust-policy）
    {DiagnosticCategory::ExecutionFailed,  RetryKind::UserRetry},  // CONTINUITY-BROKEN（检查已执行但超容差——DYN 一致性同构）
    {DiagnosticCategory::PolicyDenied,     RetryKind::UserRetry},  // RECHECK-COLLISION（碰撞过滤锚点——adjust-policy）
    {DiagnosticCategory::DataInsufficient, RetryKind::UserRetry},  // RECHECK-BUDGET-EXHAUSTED（细分搜索预算用尽——supply-evidence）
    {DiagnosticCategory::DataInsufficient, RetryKind::UserRetry},  // RECHECK-DATA-INSUFFICIENT（KIN-05 口径——supply-evidence）
    {DiagnosticCategory::ExecutionFailed,  RetryKind::UserRetry},  // TIME-PARAM-FAILED（时间化未产出——retry-task）
    {DiagnosticCategory::PolicyDenied,     RetryKind::UserRetry},  // CYCLE-TIME-EXCEEDED（Should 策略阈值——adjust-policy）
    {DiagnosticCategory::ExecutionFailed,  RetryKind::UserRetry},  // EXPORT-FAILED（导出操作失败——retry-task）
};

}  // namespace

/**
 * 工厂清单范围＝§14.4 登记表全表（12 行——acceptance 3"按
 * units/trajectory.md 登记表"）：清单恰含全表且行序＝§14.4 表行序，
 * 多登（登记卡面之外的码值——私造码）或少登（漏登记——装配数据源
 * 缺口）均失败；码值文本与卡面"码"列原文逐字一致；无重复码（键唯一
 * ——注册协议前置面）；全表 TRJ- 前缀（前缀即所有权声明）。
 */
TEST(TrjDiagCodes, FactoryScopeIsSection144FullTable_WP16T03_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"ERR-01", "NFR-MNT-03"},
                  std::vector<std::string>{"TRJ-06"});

    const auto descriptors = trajectoryCodeDescriptors();
    ASSERT_EQ(descriptors.size(), std::size(kSection144FullTable))
        << "工厂清单规模＝§14.4 登记表行数（acceptance 3——清单＝卡面全表）";

    // 行序＝§14.4 表行序（确定性序——逐位比对，不查表不排序）。
    for (std::size_t i = 0; i < descriptors.size(); ++i) {
        EXPECT_EQ(descriptors[i].code, kSection144FullTable[i])
            << "§14.4 行序/码值失同步 @ 行 " << (i + 1);
    }

    // 无重复：键唯一是 diagnostics 注册协议（§4.5）的前置——重复码在
    // 登记表层面即暴露，不等注册期才失败。
    for (std::size_t i = 0; i < descriptors.size(); ++i) {
        for (std::size_t j = i + 1; j < descriptors.size(); ++j) {
            EXPECT_NE(descriptors[i].code, descriptors[j].code)
                << "重复码值（键唯一前置——§4.5）: " << descriptors[i].code;
        }
    }

    // 全表 TRJ- 前缀：前缀即所有权声明——trajectory 登记表不得混入他
    // 单元前缀（前缀-所有权表按 TRJ→trajectory 校验）。
    for (const auto& d : descriptors) {
        EXPECT_EQ(d.code.substr(0, 4), std::string_view{"TRJ-"})
            << "非 TRJ- 前缀码混入登记表: " << d.code;
    }
}

/**
 * 描述符逐字段＝§14.4 行登记值（acceptance 3——登记值可追溯到卡面）：
 * 严重度列与比较型列 12 码逐位核对（error×9＋warning×3；比较型恰 5）；
 * ownerUnit、文案键命名约定（diag.<code-lower>.title/.detail——注册期
 * 键形校验的预演核对）、paramSchema 全表"[]"（不私造参数名）、确认/
 * 可见性/登记版本全表同值。
 */
TEST(TrjDiagCodes, DescriptorFieldsMatchSection144Rows_WP16T03_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"ERR-01", "NFR-MNT-03"},
                  std::vector<std::string>{});

    const auto descriptors = trajectoryCodeDescriptors();
    ASSERT_EQ(descriptors.size(), std::size(kSection144FullTable));
    ASSERT_EQ(std::size(kSection144IsError), std::size(kSection144FullTable));
    ASSERT_EQ(std::size(kSection144RequiresComparison),
              std::size(kSection144FullTable));

    for (std::size_t i = 0; i < descriptors.size(); ++i) {
        const CodeDescriptor& d = descriptors[i];
        const std::string code = kSection144FullTable[i];

        // 严重度列：§14.4"error"→Error、"warning"→Warning（逐位机械核对）。
        if (kSection144IsError[i]) {
            EXPECT_EQ(d.severity, DiagnosticSeverity::Error) << code;
        } else {
            EXPECT_EQ(d.severity, DiagnosticSeverity::Warning) << code;
        }

        // 所有权与键约定（P-DIAG-9 命名约定——小写连字符派生）。
        EXPECT_EQ(d.ownerUnit, "trajectory") << code;
        std::string lower = code;
        for (char& ch : lower) {
            if (ch >= 'A' && ch <= 'Z') { ch = static_cast<char>(ch - 'A' + 'a'); }
        }
        EXPECT_EQ(d.titleKey, "diag." + lower + ".title") << code;
        EXPECT_EQ(d.detailKey, "diag." + lower + ".detail") << code;

        // paramSchema 全表"[]"（diagnostics 展开登记纪律——不私造参数名，
        // 参数面随各消费者任务按需登记并升 registryVersion）。
        EXPECT_EQ(d.paramSchema, "[]") << code;

        // 确认流/登记版本/废弃状态（全表同值——逐码注释共字段口径；
        // R1 轨迹域无可确认诊断产生点——卡 §14.4 末条）。
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
 * 比较型标记与逐码分类/重试族（acceptance 3——§14.4"比较型"列与
 * diagnostics §4.3/§4.4 落值的机器核对面）：requiresComparison 恰五码
 * true（SINGULAR-NEIGHBORHOOD/LIMIT-EXCEEDED/CONTINUITY-BROKEN/
 * RECHECK-BUDGET-EXHAUSTED/CYCLE-TIME-EXCEEDED——§14.4"比较型"列点名）；
 * SA-15 不变量（confirmable⇒requiresComparison）全表成立；12 行分类/
 * 重试族与 DiagCodes.cpp 逐码注释一一对应。
 */
TEST(TrjDiagCodes, CategoryAndRetryMappingMatchesAnchors_WP16T03_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"ERR-01", "TRJ-06"},
                  std::vector<std::string>{});

    const auto descriptors = trajectoryCodeDescriptors();
    ASSERT_EQ(descriptors.size(), std::size(kExpectedCategoryRows));
    ASSERT_EQ(descriptors.size(), std::size(kSection144FullTable));

    std::size_t comparisonCount = 0;
    for (std::size_t i = 0; i < descriptors.size(); ++i) {
        const CodeDescriptor& d = descriptors[i];
        const std::string code = kSection144FullTable[i];

        // SA-15 注册表侧强化（confirmable⇒requiresComparison）——全表
        // confirmable=false，不变量恒成立。
        EXPECT_TRUE(!d.confirmable || d.requiresComparison) << code;

        // 比较型标记逐位核对（卡面点名锚点——从严执行）。
        EXPECT_EQ(d.requiresComparison, kSection144RequiresComparison[i])
            << code << " 比较型标记失同步";
        if (d.requiresComparison) { ++comparisonCount; }

        // 分类/重试族逐码核对（DiagCodes.cpp 逐码注释的机器面）。
        EXPECT_EQ(d.category, kExpectedCategoryRows[i].category)
            << code << " 分类落值失同步";
        EXPECT_EQ(d.retryable, kExpectedCategoryRows[i].retryable)
            << code << " 重试族失同步";
    }
    // 比较型总数钉死（§14.4"比较型"列恰五行"是"——多出即私造比较语义）。
    EXPECT_EQ(comparisonCount, 5U)
        << "§14.4 比较型列恰五码（SINGULAR-NEIGHBORHOOD/LIMIT-EXCEEDED/"
           "CONTINUITY-BROKEN/RECHECK-BUDGET-EXHAUSTED/CYCLE-TIME-EXCEEDED）";
}

/**
 * 真实注册表注册成功且可查询（acceptance 3——"装配期注册"执行面的真实
 * 行为验证；kinematics P-KIN-7 处置同款）：注册后全表 12 码 find 命中且
 * ownerUnit 归属正确、registeredCodes("trajectory") 反映全表、manifest
 * 含全表。TRJ 前缀已在 kPrefixOwners 在册——注册无前缀阻塞（与 DT 前缀
 * 的 P-DT-7 收编路径不同，KIN/DYN 同款在册形态）。
 */
TEST(TrjDiagCodes, RegistersIntoStableCodeRegistry_WP16T03_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"ERR-01", "NFR-MNT-03"},
                  std::vector<std::string>{});

    StableCodeRegistry registry;
    registerTrajectoryCodes(registry);

    // 全表 find 命中（查询非抛）且归属正确。
    for (const char* code : kSection144FullTable) {
        const CodeDescriptor* found = registry.find(code);
        ASSERT_NE(found, nullptr) << "注册后码必须可 find 命中: " << code;
        EXPECT_EQ(found->ownerUnit, "trajectory") << code;
    }

    // ownerUnit 反查反映全表（字典序由注册表侧承担——12 码无缺漏）。
    const auto codes = registry.registeredCodes("trajectory");
    ASSERT_EQ(codes.size(), std::size(kSection144FullTable));
    for (const char* code : kSection144FullTable) {
        EXPECT_NE(std::find(codes.begin(), codes.end(), code), codes.end())
            << "registeredCodes 缺码: " << code;
    }

    // manifest 含全表（跨进程一致性面——同注册集同摘要）。
    const auto manifest = registry.manifest();
    ASSERT_EQ(manifest.entries.size(), std::size(kSection144FullTable));
    for (const char* code : kSection144FullTable) {
        const auto it = std::find_if(manifest.entries.begin(), manifest.entries.end(),
                                     [&](const CodeDescriptor& d) {
                                         return d.code == code;
                                     });
        EXPECT_TRUE(it != manifest.entries.end()) << "manifest 缺码: " << code;
    }
}

/**
 * 重复注册被拒（acceptance 3——NFR-MNT-03 码值唯一权威：同码二次注册
 * 抛 DiagnosticsError 不吞——装配期 fail-fast）。
 */
TEST(TrjDiagCodes, DuplicateRegistrationRejected_WP16T03_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-MNT-03"},
                  std::vector<std::string>{});

    StableCodeRegistry registry;
    registerTrajectoryCodes(registry);
    // 全表首码再注册（首版描述符）→DuplicateCode（diagnostics §9.1 行为
    // 冻结表）——不捕获类型细节，只断言"抛 DiagnosticsError"即实现契约
    // 达成。
    const auto descriptors = trajectoryCodeDescriptors();
    EXPECT_THROW(registry.registerCode(descriptors[0]), DiagnosticsError);
}
