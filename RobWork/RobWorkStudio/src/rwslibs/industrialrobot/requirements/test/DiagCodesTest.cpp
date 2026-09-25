/**
 * @file   DiagCodesTest.cpp
 * @brief  requirements 稳定诊断码工厂用例组（ReqDiagCodes）——REQ-* 码
 *         描述符登记值（§9.6 行逐字段）、真实 StableCodeRegistry 注册
 *         行为与分批注册纪律（任务契约 WP-14-T02 acceptance 3 的
 *         DiagCodes.hpp 行具名自证；knownPitfalls P-REQ-8 的实现侧处置
 *         自证）。
 *
 * 设计依据：
 *   - units/requirements.md §9.6（REQ-SCHEMA-UNSUPPORTED 行——"error／
 *     对象 schema 主版本超出支持"与"不预建无消费者条目"纪律）、§3.3
 *     （DiagCodes.hpp 行——T02）
 *   - units/diagnostics.md §4.5（注册协议——句法/前缀-所有权/键唯一/
 *     paramSchema 注册期验证）、§9.1（StableCodeRegistry 行为冻结表）
 *   - 收编确认锚点（如实声明，modeling/DiagCodesTest.cpp 同款）：REQ-*
 *     码进入 diagnostics 全局装配（L5 装配清单）属 diagnostics 所有者的
 *     治理动作——本任务产出注册面并以真实注册表验证，不在实现侧私自
 *     扩编 diagnostics 单元文件（allowedFiles 红线）
 *   - 任务契约 tasks/foundation/WP-14-T02.json acceptance 3
 */

#include <sdurws/ird/requirements/DiagCodes.hpp>

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
using sdurws::ird::requirements::kReqImportDuplicateId;
using sdurws::ird::requirements::kReqImportFrameUnknown;
using sdurws::ird::requirements::kReqImportRowError;
using sdurws::ird::requirements::kReqImportUnitIllegal;
using sdurws::ird::requirements::kReqReadyInputIncomplete;
using sdurws::ird::requirements::kReqReadyNoRequiredCase;
using sdurws::ird::requirements::kReqReadyPlanDegenerate;
using sdurws::ird::requirements::kReqReadyPlanMissing;
using sdurws::ird::requirements::kReqReadyPoseIllegal;
using sdurws::ird::requirements::kReqReadyRefMissing;
using sdurws::ird::requirements::kReqReadySeqCycle;
using sdurws::ird::requirements::kReqSchemaUnsupported;
using sdurws::ird::requirements::registerRequirementCodes;
using sdurws::ird::requirements::requirementCodeDescriptors;

namespace {

/// §9.6 已到任务行的**应登记码面**（测试内自持字面清单——与实现清单机械
/// 比对，任一侧漂移即失败：失同步防线）。分批纪律：T02 批 1 码＋T04 批
/// 4 码（WP-14-T04 落位随附同步，1→5）＋T05 批 6 码＋表尾增登 1 码
/// （WP-14-T05 就绪族落位随附同步，5→12——合法登记随附同步先例；增登
/// PLAN-MISSING 承载 §8.1 R6"Warning（零计划）"分支，modeling
/// WP-13-T10 实现期增登同款）；其余 3 行（T06/T07）不得提前出现，随
/// 各自任务在本清单表尾追加。
const char* kStagedSection96Codes[] = {
    "REQ-SCHEMA-UNSUPPORTED",     // §9.6 T02/T03 行
    "REQ-IMPORT-ROW-ERROR",       // §9.6 T04 行（导入族，WP-14-T04 登记）
    "REQ-IMPORT-DUPLICATE-ID",    // §9.6 T04 行
    "REQ-IMPORT-UNIT-ILLEGAL",    // §9.6 T04 行
    "REQ-IMPORT-FRAME-UNKNOWN",   // §9.6 T04 行
    "REQ-READY-REF-MISSING",      // §9.6 T05 行（就绪族，WP-14-T05 登记）
    "REQ-READY-SEQ-CYCLE",        // §9.6 T05 行
    "REQ-READY-POSE-ILLEGAL",     // §9.6 T05 行
    "REQ-READY-NO-REQUIRED-CASE", // §9.6 T05 行
    "REQ-READY-PLAN-DEGENERATE",  // §9.6 T05 行
    "REQ-READY-INPUT-INCOMPLETE", // §9.6 T05 行
    "REQ-READY-PLAN-MISSING",     // §9.6 T05 行表尾增登（实现期增登）
};

}  // namespace

/**
 * 工厂清单范围＝§9.6 已到任务行（acceptance 3——分批注册纪律）：清单
 * 恰含 T02 行 1 码＋T04 行 4 码＋T05 行 6 码＋增登 1 码，多登（预建无
 * 消费者条目）或少登（漏登记）均失败；码值文本与卡面"码"列原文逐字
 * 一致（T05 批同步：WP-14-T05 acceptance 1 六码具名断言＋增登一码）。
 */
TEST(ReqDiagCodes, FactoryScopeIsStagedRows_WP14T02T04T05)
{
    IRD_TEST_INFO(std::vector<std::string>{"ERR-01"},
                  std::vector<std::string>{});

    const auto descriptors = requirementCodeDescriptors();
    ASSERT_EQ(descriptors.size(), std::size(kStagedSection96Codes))
        << "工厂清单规模＝§9.6 已到任务行数（分批纪律：不预建/不漏登）";

    for (const auto* expected : kStagedSection96Codes) {
        const auto it = std::find_if(descriptors.begin(), descriptors.end(),
                                     [&](const CodeDescriptor& d) {
                                         return d.code == expected;
                                     });
        ASSERT_TRUE(it != descriptors.end())
            << "§9.6 已到任务行缺失: " << expected;
    }
    // 码值常量与卡面原文同串（唯一书写点——Errors.cpp 映射与 Readiness/
    // Import 产码共用）。
    EXPECT_EQ(kReqSchemaUnsupported, "REQ-SCHEMA-UNSUPPORTED");
    EXPECT_EQ(kReqImportRowError, "REQ-IMPORT-ROW-ERROR");
    EXPECT_EQ(kReqImportDuplicateId, "REQ-IMPORT-DUPLICATE-ID");
    EXPECT_EQ(kReqImportUnitIllegal, "REQ-IMPORT-UNIT-ILLEGAL");
    EXPECT_EQ(kReqImportFrameUnknown, "REQ-IMPORT-FRAME-UNKNOWN");
    EXPECT_EQ(kReqReadyRefMissing, "REQ-READY-REF-MISSING");
    EXPECT_EQ(kReqReadySeqCycle, "REQ-READY-SEQ-CYCLE");
    EXPECT_EQ(kReqReadyPoseIllegal, "REQ-READY-POSE-ILLEGAL");
    EXPECT_EQ(kReqReadyNoRequiredCase, "REQ-READY-NO-REQUIRED-CASE");
    EXPECT_EQ(kReqReadyPlanDegenerate, "REQ-READY-PLAN-DEGENERATE");
    EXPECT_EQ(kReqReadyInputIncomplete, "REQ-READY-INPUT-INCOMPLETE");
    EXPECT_EQ(kReqReadyPlanMissing, "REQ-READY-PLAN-MISSING");
}

/**
 * 描述符逐字段＝§9.6 行登记值（acceptance 3——登记值可追溯到卡面）：
 * ownerUnit/分类/级别/文案键/paramSchema/confirmable/retryable/可见性
 * 三项逐字段核对（modeling 同义码同款投影——同一语义事件两域登记）。
 */
TEST(ReqDiagCodes, DescriptorFieldsMatchSection96Row_WP14T02_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"ERR-01", "NFR-MNT-03"},
                  std::vector<std::string>{});

    const auto descriptors = requirementCodeDescriptors();
    ASSERT_EQ(descriptors.size(), 12U);
    const CodeDescriptor& d = descriptors[0];

    // 码值/所有权：§9.6 表"码"列原文＋前缀-所有权表 REQ→requirements。
    EXPECT_EQ(d.code, "REQ-SCHEMA-UNSUPPORTED");
    EXPECT_EQ(d.ownerUnit, "requirements");
    // 分类/级别："error"→Error；分类＝FormatOrVersion（schema/版本不兼容
    // 族——diagnostics §4.3 词表；modeling 同义码同款投影）。
    EXPECT_EQ(d.category, DiagnosticCategory::FormatOrVersion);
    EXPECT_EQ(d.severity, DiagnosticSeverity::Error);
    // 文案键（P-DIAG-9 命名约定 diag.<code-lower>.title/.detail）。
    EXPECT_EQ(d.titleKey, "diag.req-schema-unsupported.title");
    EXPECT_EQ(d.detailKey, "diag.req-schema-unsupported.detail");
    // paramSchema 三键（哪类需求对象/实际主版本/支持的最高主版本——
    // 与 Errors.hpp 同码 params 键面对齐）。
    EXPECT_EQ(d.paramSchema, R"(["object-type","schema-version","supported-major"])");
    // 确认/比较/重试：无放行分支；建议动作"升级程序/重新编辑"＝fix-input。
    EXPECT_FALSE(d.confirmable);
    EXPECT_FALSE(d.requiresComparison);
    EXPECT_EQ(d.retryable, RetryKind::UserRetry);
    // 可见性三项（非 Dev 码全 true——进用户目录/报告/历史）。
    EXPECT_TRUE(d.userVisible);
    EXPECT_TRUE(d.reportable);
    EXPECT_TRUE(d.historical);
    // 登记版本/废弃状态。
    EXPECT_EQ(d.registryVersion, 1U);
    EXPECT_FALSE(d.deprecated);
    EXPECT_FALSE(d.supersededBy.has_value());
}

/**
 * 真实注册表注册成功且可查询（acceptance 3——P-REQ-8 处置自证：消费
 * diagnostics Draft 契约按当周现状，以真实 StableCodeRegistry 验证全部
 * 通过）：注册后 find 命中且 ownerUnit 归属正确、registeredCodes("requirements")
 * 反映全清单、seal 后 manifest 含该码。
 */
TEST(ReqDiagCodes, RegistersIntoStableCodeRegistry_WP14T02_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"ERR-01"},
                  std::vector<std::string>{});

    StableCodeRegistry registry;
    registerRequirementCodes(registry);

    // find 命中（查询非抛）且归属正确。
    const CodeDescriptor* found = registry.find("REQ-SCHEMA-UNSUPPORTED");
    ASSERT_NE(found, nullptr) << "注册后码必须可 find 命中";
    EXPECT_EQ(found->ownerUnit, "requirements");
    EXPECT_EQ(found->category, DiagnosticCategory::FormatOrVersion);

    // ownerUnit 反查反映全清单（字典序由注册表侧承担；规模随 T04 批
    // 1→5、T05 批 5→12）。
    const auto codes = registry.registeredCodes("requirements");
    ASSERT_EQ(codes.size(), 12U);

    // manifest 含该码（跨进程一致性面——同注册集同摘要）。
    const auto manifest = registry.manifest();
    const auto it = std::find_if(manifest.entries.begin(), manifest.entries.end(),
                                 [](const CodeDescriptor& d) {
                                     return d.code == "REQ-SCHEMA-UNSUPPORTED";
                                 });
    EXPECT_TRUE(it != manifest.entries.end());
}

/**
 * 重复注册被拒（acceptance 3——NFR-MNT-03 码值唯一权威：同码二次注册
 * 抛 DiagnosticsError 不吞——装配期 fail-fast）。
 */
TEST(ReqDiagCodes, DuplicateRegistrationRejected_WP14T02_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-MNT-03"},
                  std::vector<std::string>{});

    StableCodeRegistry registry;
    registerRequirementCodes(registry);
    // 同码再注册（首版描述符）→DuplicateCode（diagnostics §9.1 行为冻结
    // 表）——不捕获类型细节，只断言"抛 DiagnosticsError"即实现契约达成。
    const auto descriptors = requirementCodeDescriptors();
    EXPECT_THROW(registry.registerCode(descriptors[0]), DiagnosticsError);
}

/**
 * §9.6 T04 行 4 码（导入族）的登记值逐字段核对（WP-14-T04 落位随附
 * 同步——登记值可追溯到卡面：三 error 码＋一 warning 码、分类全为
 * InputInvalid、所有权/文案键/paramSchema 键面与 Import.cpp 产码处的
 * context 载荷对齐）。
 */
TEST(ReqDiagCodes, T04ImportDescriptorsMatchSection96Rows_WP14T04)
{
    IRD_TEST_INFO(std::vector<std::string>{"ERR-01", "REQ-05"},
                  std::vector<std::string>{"AT-02"});

    const auto descriptors = requirementCodeDescriptors();
    ASSERT_EQ(descriptors.size(), 12U);

    // 按码查找（清单序无关的核对入口）。
    const auto findDesc = [&](std::string_view code) -> const CodeDescriptor& {
        const auto it = std::find_if(descriptors.begin(), descriptors.end(),
                                     [&](const CodeDescriptor& d) {
                                         return d.code == code;
                                     });
        EXPECT_TRUE(it != descriptors.end()) << "缺失登记: " << code;
        return it != descriptors.end() ? *it : descriptors.front();
    };

    // 三 error 码：REQ-IMPORT-ROW-ERROR / DUPLICATE-ID / UNIT-ILLEGAL。
    for (const auto* code : {"REQ-IMPORT-ROW-ERROR", "REQ-IMPORT-DUPLICATE-ID",
                             "REQ-IMPORT-UNIT-ILLEGAL"}) {
        const CodeDescriptor& d = findDesc(code);
        EXPECT_EQ(d.ownerUnit, "requirements") << code;
        EXPECT_EQ(d.category, DiagnosticCategory::InputInvalid) << code;
        EXPECT_EQ(d.severity, DiagnosticSeverity::Error) << code;
        EXPECT_FALSE(d.confirmable) << code;
        EXPECT_EQ(d.retryable, RetryKind::UserRetry) << code;
        EXPECT_TRUE(d.userVisible && d.reportable && d.historical) << code;
        EXPECT_EQ(d.registryVersion, 1U) << code;
    }
    // warning 码：REQ-IMPORT-FRAME-UNKNOWN（§9.6"级别"列原文——Frame 悬空
    // 可保留待解析，不阻断导入）。
    const CodeDescriptor& w = findDesc("REQ-IMPORT-FRAME-UNKNOWN");
    EXPECT_EQ(w.ownerUnit, "requirements");
    EXPECT_EQ(w.category, DiagnosticCategory::InputInvalid);
    EXPECT_EQ(w.severity, DiagnosticSeverity::Warning);
    EXPECT_EQ(w.titleKey, "diag.req-import-frame-unknown.title");
    EXPECT_EQ(w.detailKey, "diag.req-import-frame-unknown.detail");
    EXPECT_EQ(w.paramSchema, R"(["row","column","raw"])");
    EXPECT_FALSE(w.confirmable);
    EXPECT_EQ(w.retryable, RetryKind::UserRetry);
}

/**
 * §9.6 T05 行 6 码＋表尾增登 1 码（就绪族）的登记值逐字段核对（WP-14-T05
 * 落位随附同步——契约 acceptance 1 六码具名断言＋增登码登记依据；级别
 * 面＝五 error＋两 warning，所有权/文案键/paramSchema 键面与 Readiness.
 * cpp/CommandHandlers.cpp 产码处的 context 载荷对齐）。
 */
TEST(ReqDiagCodes, T05ReadinessDescriptorsMatchSection96Rows_WP14T05)
{
    IRD_TEST_INFO(std::vector<std::string>{"ERR-01", "REQ-06"},
                  std::vector<std::string>{});

    const auto descriptors = requirementCodeDescriptors();
    ASSERT_EQ(descriptors.size(), 12U);

    const auto findDesc = [&](std::string_view code) -> const CodeDescriptor& {
        const auto it = std::find_if(descriptors.begin(), descriptors.end(),
                                     [&](const CodeDescriptor& d) {
                                         return d.code == code;
                                     });
        EXPECT_TRUE(it != descriptors.end()) << "缺失登记: " << code;
        return it != descriptors.end() ? *it : descriptors.front();
    };

    // 五 error 码（§9.6"级别"列 error）：REF-MISSING/SEQ-CYCLE/POSE-ILLEGAL
    // /PLAN-DEGENERATE/INPUT-INCOMPLETE——全量公共面（所有权/分类/可确认/
    // 重试/可见性/登记版本）逐码核对。
    for (const auto* code :
         {"REQ-READY-REF-MISSING", "REQ-READY-SEQ-CYCLE", "REQ-READY-POSE-ILLEGAL",
          "REQ-READY-PLAN-DEGENERATE", "REQ-READY-INPUT-INCOMPLETE"}) {
        const CodeDescriptor& d = findDesc(code);
        EXPECT_EQ(d.ownerUnit, "requirements") << code;
        EXPECT_EQ(d.category, DiagnosticCategory::InputInvalid) << code;
        EXPECT_EQ(d.severity, DiagnosticSeverity::Error) << code;
        EXPECT_FALSE(d.confirmable) << code;
        EXPECT_FALSE(d.requiresComparison) << code;
        EXPECT_EQ(d.retryable, RetryKind::UserRetry) << code;
        EXPECT_TRUE(d.userVisible && d.reportable && d.historical) << code;
        EXPECT_EQ(d.registryVersion, 1U) << code;
        EXPECT_FALSE(d.deprecated) << code;
        EXPECT_FALSE(d.supersededBy.has_value()) << code;
    }
    // error 码文案键/参数键面（与产码处 context 载荷对齐——P-DIAG-9）。
    const CodeDescriptor& ref = findDesc("REQ-READY-REF-MISSING");
    EXPECT_EQ(ref.titleKey, "diag.req-ready-ref-missing.title");
    EXPECT_EQ(ref.paramSchema, R"(["field","target","expected-token"])");
    const CodeDescriptor& seq = findDesc("REQ-READY-SEQ-CYCLE");
    EXPECT_EQ(seq.paramSchema, R"(["kind","key"])");
    const CodeDescriptor& pose = findDesc("REQ-READY-POSE-ILLEGAL");
    EXPECT_EQ(pose.paramSchema, R"(["field"])");
    const CodeDescriptor& deg = findDesc("REQ-READY-PLAN-DEGENERATE");
    EXPECT_EQ(deg.paramSchema, R"(["field","target"])");
    const CodeDescriptor& inc = findDesc("REQ-READY-INPUT-INCOMPLETE");
    EXPECT_EQ(inc.paramSchema, R"(["invalid-count","invalid-items"])");

    // 两 warning 码（§9.6"级别"列 warning——预告登记面，不阻断应用）：
    // NO-REQUIRED-CASE（T05 行）与 PLAN-MISSING（表尾增登）。
    for (const auto* code :
         {"REQ-READY-NO-REQUIRED-CASE", "REQ-READY-PLAN-MISSING"}) {
        const CodeDescriptor& d = findDesc(code);
        EXPECT_EQ(d.ownerUnit, "requirements") << code;
        EXPECT_EQ(d.category, DiagnosticCategory::InputInvalid) << code;
        EXPECT_EQ(d.severity, DiagnosticSeverity::Warning) << code;
        EXPECT_FALSE(d.confirmable) << code;
        EXPECT_EQ(d.retryable, RetryKind::UserRetry) << code;
        EXPECT_TRUE(d.userVisible && d.reportable && d.historical) << code;
        EXPECT_EQ(d.registryVersion, 1U) << code;
    }
    const CodeDescriptor& nrc = findDesc("REQ-READY-NO-REQUIRED-CASE");
    EXPECT_EQ(nrc.titleKey, "diag.req-ready-no-required-case.title");
    EXPECT_EQ(nrc.paramSchema, R"(["conditions","required"])");
    const CodeDescriptor& plm = findDesc("REQ-READY-PLAN-MISSING");
    EXPECT_EQ(plm.titleKey, "diag.req-ready-plan-missing.title");
    EXPECT_EQ(plm.paramSchema, R"(["regions","plans"])");
}
