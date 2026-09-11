/**
 * @file   ContractCheck.hpp
 * @brief  契约通用断言——诊断记录/比较型三要素/任务身份/身份唯一性/文件完整性/
 *         包络组合泛型谓词（TK-T07；经模板/访问器消费 core 类型）。
 *
 * 设计依据：
 *   - units/testkit.md §5.5（签名逐项）、§4.8（core::DiagnosticRecord 语义——
 *     ERR-01/UX-03/SA-15）、§8 TK-CTR
 *   - 职责边界：testkit 不依赖 evidence 等产品单元——evidence 契约测试在自己的
 *     _contract_test 目标内将其类型喂入泛型断言（checkEnvelopeCombination）
 *   - 任务契约 tasks/foundation/TK-T07.json（≙WP-02-T07）
 *
 * 失败承载：复用 CheckResult（TK-T05 交付）——契约校验失败计入 failures，
 * failures[i].fieldPath＝违规描述（如 "code: 句法非法"、"subject: 稳定项缺失"）；
 * 数值字段不适用（置 0）。零 gtest（D-06）。
 * 线程安全：纯函数。
 */

#ifndef SDURWS_IRD_TESTKIT_CONTRACTCHECK_HPP
#define SDURWS_IRD_TESTKIT_CONTRACTCHECK_HPP

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <sdurws/ird/core/DiagData.hpp>
#include <sdurws/ird/core/Evaluation.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/testkit/Check.hpp>

namespace sdurws::ird::testkit {

/// 诊断记录校验选项（§5.5：allowTransient 时 subject 可空——瞬时开发诊断）。
struct DiagnosticCheckOptions {
    bool allowTransient = false;   ///< true＝允许 subject 缺失（瞬时诊断）
};

/**
 * @brief 诊断记录契约校验（ERR-01/UX-03）。
 *
 * 校验项：code 句法；稳定诊断项 subject 必填（opt.allowTransient 时可空）；
 * context/cause/recommendedAction 非空；比较型条目存在且三要素完整
 * （actual/expected/unit 已注册）；NotApplicable 值不得伪造数值
 * （SourcedValue 状态检查）。
 */
CheckResult checkDiagnosticRecord(const core::DiagnosticRecord& r,
                                  DiagnosticCheckOptions opt);

/**
 * @brief 比较型三要素校验：actual/expected 的 unit 量纲须与 expectedKind 一致。
 */
CheckResult checkComparativeFields(const core::ComparativeFields& f,
                                   core::QuantityKind expectedKind);

/// 任务身份五元组全 isValid（TASK-03）。
CheckResult checkTaskIdentity(const core::TaskIdentity& id);

/// 身份唯一性：重复项列入 failures（fieldPath＝重复的规范文本）。
CheckResult checkStableIdsUnique(const std::vector<core::ObjectId>& ids);

/// 文件完整性：SHA-256（core::ContentDigester）对照期望十六进制串。
CheckResult checkFileIntegrity(const std::filesystem::path& file,
                               std::string_view sha256Expected);

/**
 * @brief 结果包络合法组合的通用谓词（§8.1 表 3——evidence 侧提供访问器）。
 *
 * @tparam EnvelopeT evidence 侧结果包络类型（含 outcome/engineeringStatus 字段）
 * @tparam Accessors 访问器集合（静态成员）：outcome(e)、engineeringStatus(e)、
 *                   hasEvidenceList(e)、hasMissingItemsList(e)、hasFormalConclusion(e)
 *
 * §8.1 表 3 规则：outcome∈{Canceled,Failed,Interrupted} ⇒ engineeringStatus==
 * NotApplicable 且无正式结论字段；Completed ⇒ payload 含证据清单；
 * DataInsufficient ⇒ 缺失项全量清单存在。
 */
template <class EnvelopeT, class Accessors>
CheckResult checkEnvelopeCombination(const EnvelopeT& e, Accessors a)
{
    CheckResult result;
    const auto outcome = a.outcome(e);
    const auto status = a.engineeringStatus(e);
    const bool terminalNonComplete = outcome == core::TaskOutcome::Canceled
                                  || outcome == core::TaskOutcome::Failed
                                  || outcome == core::TaskOutcome::Interrupted;

    if (terminalNonComplete && status != core::EngineeringStatus::NotApplicable) {
        result.failures.push_back([] {
            CompareDetail d;
            d.fieldPath = "engineeringStatus: 取消/失败/中断 ⇒ NotApplicable（表 3）";
            return d;
        }());
    }
    if (outcome == core::TaskOutcome::Completed && !a.hasEvidenceList(e)) {
        result.failures.push_back([] {
            CompareDetail d;
            d.fieldPath = "evidenceList: Completed ⇒ 须含证据清单（表 3）";
            return d;
        }());
    }
    if (status == core::EngineeringStatus::DataInsufficient
        && !a.hasMissingItemsList(e)) {
        result.failures.push_back([] {
            CompareDetail d;
            d.fieldPath = "missingItems: DataInsufficient ⇒ 缺失项全量清单存在（表 3）";
            return d;
        }());
    }
    if (!result.failures.empty()) { result.passed = false; }
    return result;
}

}  // namespace sdurws::ird::testkit

#endif  // SDURWS_IRD_TESTKIT_CONTRACTCHECK_HPP
