/**
 * @file   Tx2DiagnosticRecordContractTest.cpp
 * @brief  确认/断言拒绝面诊断契约用例组（跨单元契约面）——PRJ-TX-2 的
 *         观测点"诊断字段完整"经 testkit ContractCheck 机器校验落地，
 *         并消费 IRD_TEST_INFO/IRD_EXPECT_IDENTICAL（testkit 消费边界
 *         的用例化声明之一，任务契约 acceptance 2）。
 *
 * 设计依据：
 *   - units/project.md §11 PRJ-TX-2 行（硬断言失败／无 interaction 待确认
 *     集／interaction 拒绝／确认凭据绑定失效——四路均无修订；观测点＝
 *     "诊断字段完整（ContractCheck.checkDiagnosticRecord）"）、§5.3.3
 *     （确认回调三态）、§6.7（确认放行流与绑定复核）；
 *   - units/testkit.md §5.5（checkDiagnosticRecord 契约：code 句法/必填
 *     串非空/比较型三要素/NotApplicable 纪律——校验器不信任被校验方）、
 *     §7.3（IRD_TEST_INFO 需求追溯登记）、§5.3.3（IRD_EXPECT_IDENTICAL
 *     ——字符串精确相等断言走 testkit Check 通道，失败详情进
 *     ird-test-report.json 的 comparisons 字段）；
 *   - 需求 MDL-06（断言就地阻止）、SA-15（确认放行）、AT-01、ERR-01
 *     （诊断必填字段）、UX-03（比较型三要素齐备）。
 *
 * 与 CommandServiceTest.CommandConfirmTx2 组的关系（零重复）：该组（单元
 * 面）钉住四路的决策映射与零磁盘副作用；本组（契约面）只做"产出的诊断
 * 记录逐条过 ContractCheck 机器校验"——PRJ-T15 消费 testkit 断言设施的
 * 落地面。四路驱动逻辑刻意精简为最小前置。
 *
 * P-PR-1 处置声明：用例构造的 core 数据（ConfirmableFinding/
 * DiagnosticRecord/凭据）随 core v0.1 基线消费，core 冻结出 diff 后
 * 增量同步——不在测试内私改 core 语义。
 */

#include <sdurws/ird/testkit/ContractCheck.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include "StubHandlers.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

using sdurws::ird::core::ConfirmableFinding;
using sdurws::ird::core::DiagnosticRecord;
using sdurws::ird::core::QuantityKind;
using sdurws::ird::project::CommandEnvelope;
using sdurws::ird::project::CommandResult;
using sdurws::ird::project::stub::PerturbingProbe;
using sdurws::ird::project::stub::ServiceFixture;
using sdurws::ird::project::stub::StubInteraction;
// IRD_EXPECT_IDENTICAL 宏展开为未限定 checkIdentical（testkit §5.3.3 宏
// 语义——展开于消费方 TU）——using 声明使其可见。
using sdurws::ird::testkit::checkIdentical;
namespace tk = sdurws::ird::testkit;

/**
 * @brief 单条诊断记录过 ContractCheck 契约校验（ERR-01/UX-03 机器面）。
 *
 * allowTransient=true 的依据：测试域记录（TEST-* 码）不携带主体对象
 * （subject 空——域对象在测试载荷中不存在）；core §4.8 语义下无主体的
 * 记录属瞬时/测试域诊断，按 §5.5 选项语义放行 subject 检查，其余检查
 * （code 句法/必填串/比较型三要素/NotApplicable 纪律）全量生效。失败
 * 详情经 IRD_CHECK_REPORT_ 逐条进 gtest 输出与 ird-test-report.json。
 */
void expectRecordPassesContractCheck(const DiagnosticRecord& record)
{
    tk::DiagnosticCheckOptions options;
    options.allowTransient = true;
    IRD_CHECK_REPORT_(tk::checkDiagnosticRecord(record, options));
}

/// 一批诊断记录逐条过契约校验（失败详情经 IRD 宏进报告 comparisons）。
void expectAllPassContractCheck(const std::vector<DiagnosticRecord>& records)
{
    for (const auto& record : records) {
        expectRecordPassesContractCheck(record);
    }
}

/// 待确认命令信封（FindingsHandler 的 "test-confirm-required" v1）。
CommandEnvelope confirmEnvelope(const sdurws::ird::core::BranchId& branch,
                                const std::string& payload)
{
    CommandEnvelope envelope;
    envelope.branch = branch;
    envelope.commandType = "test-confirm-required";
    envelope.payloadFormatVersion = 1;
    envelope.payloadCanonical.assign(payload.begin(), payload.end());
    return envelope;
}

/// 硬断言命令信封（HardAssertFailHandler 的 "test-hard-assert-fail" v1）。
CommandEnvelope hardAssertEnvelope(const sdurws::ird::core::BranchId& branch)
{
    CommandEnvelope envelope;
    envelope.branch = branch;
    envelope.commandType = "test-hard-assert-fail";
    envelope.payloadFormatVersion = 1;
    envelope.payloadCanonical = {'b', 'a', 'd'};
    return envelope;
}

}  // namespace

// =====================================================================
// 用例组：Tx2DiagnosticContract（PRJ-TX-2 观测点——诊断字段完整）
// =====================================================================

/**
 * 锚定：PRJ-TX-2 四路的观测点"诊断字段完整（ContractCheck.
 * checkDiagnosticRecord）"／ERR-01/UX-03——acceptance 1＋acceptance 2
 * （IRD_TEST_INFO/IRD_EXPECT_* 消费用的声明在案）。
 *
 * 前置：可写项目＋缺省处理器桩（ServiceFixture.registerDefaults）。
 * 操作：四路提交（硬断言／无交互待确认／交互拒绝／绑定复核失配），
 * 逐路收集回传诊断与 finding 底层记录并过机器校验。
 * 预期：四路全部 Rejected 无修订（§11 预期列）；路 1 诊断码
 * ＝TEST-HARD-ASSERT-FAILED（IRD_EXPECT_IDENTICAL——testkit Check 通道
 * 的用例化消费）；路 2/3 finding 底层记录为比较型且三要素齐备
 * （checkComparativeFields 按长度量纲复核——m 单位）；路 4 绑定失配经
 * 扰动探针注入且不产出用户级诊断（P-PR-6——不私造码）。
 */
TEST(Tx2DiagnosticContract, FourRejectionPaths_DiagnosticRecordsPassContractCheck)
{
    // 需求/AT 追溯登记（§7.3 IRD_TEST_INFO——listener 聚合入报告）。
    IRD_TEST_INFO(std::vector<std::string>{"MDL-06", "SA-15"},
                  std::vector<std::string>{"AT-01"});

    // ---- 路 1：硬断言失败（MDL-06 就地阻止面）----
    std::vector<DiagnosticRecord> allRecords;
    {
        ServiceFixture fx;
        fx.registerDefaults();
        // 无修订基线（r0＝createNew 的初始修订——"均无修订"的对照面）。
        const std::size_t revisionsBefore = fx.revisionDirCount();
        const CommandResult result
            = fx.commands().submit(hardAssertEnvelope(fx.primaryBranch()));
        ASSERT_TRUE(result.rejected());
        EXPECT_EQ(result.status.rejection,
                  sdurws::ird::project::CommandStatus::Rejection::
                      HardAssertFailed);
        ASSERT_GE(result.diagnostics.size(), 1u);
        // 稳定码精确相等（IRD_EXPECT_IDENTICAL——字符串精确匹配通道）。
        IRD_EXPECT_IDENTICAL("path1.hardAssert.code",
                             result.diagnostics[0].code,
                             "TEST-HARD-ASSERT-FAILED");
        allRecords.insert(allRecords.end(), result.diagnostics.begin(),
                          result.diagnostics.end());
        EXPECT_EQ(fx.revisionDirCount(), revisionsBefore);
    }

    // ---- 路 2：无 interaction＋待确认集（confirmations-unresolved）----
    {
        ServiceFixture fx;
        fx.registerDefaults();
        // 无修订基线（r0＝createNew 的初始修订——"均无修订"的对照面）。
        const std::size_t revisionsBefore = fx.revisionDirCount();
        const CommandResult result
            = fx.commands().submit(confirmEnvelope(fx.primaryBranch(), "tx2"));
        ASSERT_TRUE(result.rejected());
        EXPECT_EQ(result.status.rejection,
                  sdurws::ird::project::CommandStatus::Rejection::
                      ConfirmationsUnresolved);
        ASSERT_EQ(result.findings.size(), 1u);
        // finding 底层记录过契约校验＋比较型三要素按长度量纲复核。
        const ConfirmableFinding& finding = result.findings[0];
        expectRecordPassesContractCheck(finding.record);
        ASSERT_TRUE(finding.record.comparison.has_value());
        IRD_CHECK_REPORT_(tk::checkComparativeFields(
            *finding.record.comparison, QuantityKind::Length));
        allRecords.push_back(finding.record);
        EXPECT_EQ(fx.revisionDirCount(), revisionsBefore);
    }

    // ---- 路 3：interaction 拒绝（confirmations-rejected）----
    {
        ServiceFixture fx;
        fx.registerDefaults();
        // 无修订基线（r0＝createNew 的初始修订——"均无修订"的对照面）。
        const std::size_t revisionsBefore = fx.revisionDirCount();
        StubInteraction interaction;
        interaction.rejectAll = true;
        const CommandResult result = fx.commands().submit(
            confirmEnvelope(fx.primaryBranch(), "tx2"), &interaction);
        ASSERT_TRUE(result.rejected());
        EXPECT_EQ(result.status.rejection,
                  sdurws::ird::project::CommandStatus::Rejection::
                      ConfirmationsRejected);
        ASSERT_EQ(result.findings.size(), 1u);
        expectRecordPassesContractCheck(result.findings[0].record);
        allRecords.push_back(result.findings[0].record);
        EXPECT_EQ(fx.revisionDirCount(), revisionsBefore);
    }

    // ---- 路 4：确认凭据绑定失效（复核扰动注入——§6.7 实现口径④）----
    {
        ServiceFixture fx;
        fx.registerDefaults();
        // 无修订基线（r0＝createNew 的初始修订——"均无修订"的对照面）。
        const std::size_t revisionsBefore = fx.revisionDirCount();
        StubInteraction interaction;  // 回调正常确认——失配由复核面捕获
        PerturbingProbe probe;
        probe.member = PerturbingProbe::Member::CommandDigest;
        fx.impl().commandService().setConfirmationProbe(&probe);
        const CommandResult result = fx.commands().submit(
            confirmEnvelope(fx.primaryBranch(), "tx2"), &interaction);
        ASSERT_TRUE(result.rejected());
        EXPECT_EQ(result.status.rejection,
                  sdurws::ird::project::CommandStatus::Rejection::
                      ConfirmationsUnresolved);
        EXPECT_EQ(result.diagnostics.size(), 0u)
            << "绑定失配不得产出用户级诊断（P-PR-6——维度定位走开发通道）";
        EXPECT_TRUE(fx.sink.devContains("绑定复核失配"));
        EXPECT_EQ(fx.revisionDirCount(), revisionsBefore);
    }

    // ---- 汇总：四路收集到的全部记录逐条过机器校验（观测点总成）----
    ASSERT_GE(allRecords.size(), 3u);
    expectAllPassContractCheck(allRecords);
}
