/**
 * @file   SectionCrossUnitContractTest.cpp
 * @brief  reporting 章节契约的跨单元消费自证——SectionRequest 内嵌
 *         evidence::ResultEnvelope 与 core 身份类型（P-RPT-9 v0.1 Draft
 *         基线的类型级钉死）＋章节内容消费 ReportModel 冻结视图（v0.6 偏差
 *         登记的类型级兑现）＋RPT-SCOPE-INSUFFICIENT 码面与 diagnostics
 *         StableCodeRegistry 收编一致性（RPT-T04 acceptance 4/5/6）。
 *
 * 设计依据：
 *   - units/reporting.md §9.2（SectionRequest.results 契约"evidence::
 *     ResultEnvelope 只读值"、§3.2 消费清单行）、§5.4（RPT-SCOPE-
 *     INSUFFICIENT 诊断挂接——码值权威归 diagnostics）、§3.1/§4.3.4
 *     （条目冻结视图族暂载 ReportModel.hpp——RPT-T04 消费不重复定义）
 *   - 任务契约 tasks/foundation/RPT-T04.json acceptance 4（诊断挂接）、
 *     5（结构逐字段）、6（P-RPT-5/P-RPT-9 陷阱处置）
 *
 * 测试形态（先例：IdentityCrossUnitContractTest——类型级 static_assert
 * ＋最小值面往返；夹具数据仅验证结构契约，envelope 载荷语义归 evidence
 * 单元测试——RP-STATE-4 替身边界声明同源）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/core/DiagData.hpp>
#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/diagnostics/DiagCodes.hpp>
#include <sdurws/ird/evidence/Envelope.hpp>
#include <sdurws/ird/reporting/Errors.hpp>
#include <sdurws/ird/reporting/ReportModel.hpp>
#include <sdurws/ird/reporting/SectionProvider.hpp>
#include <sdurws/ird/reporting/Sections.hpp>

#include <string>
#include <type_traits>
#include <vector>

namespace {

using namespace sdurws::ird::reporting;
namespace co = sdurws::ird::core;
namespace ev = sdurws::ird::evidence;
namespace dg = sdurws::ird::diagnostics;

// =====================================================================
// acceptance 6（P-RPT-9）：上游契约消费基线的类型级钉死
// =====================================================================

/**
 * 验证 acceptance 6（P-RPT-9 处置）：SectionRequest 以**值类型**内嵌上游
 * 公共契约——evidence::ResultEnvelope（§9.2 原文"本章节相关的结果子集"）
 * 与 core 身份类型（RevisionId/ContentIdentity）。类型恒等断言常驻自证：
 * 上游冻结出 diff（类型改名/形状变化）时本文件编译失配报警，按 DTB §5.4
 * 增量同步留痕——不静默跟随、不私改上游。
 */
TEST(ReportingSectionContract, RequestEmbedsUpstreamValueTypes_RPT04_ACC6)
{
    // 证据侧：结果子集元素类型＝evidence::ResultEnvelope 本身（零本地
    // 包装/零别名换轨——消费面即上游类型面）。
    static_assert(std::is_same_v<decltype(SectionRequest::results)::value_type,
                                 ev::ResultEnvelope>,
                  "SectionRequest.results 必须以 evidence::ResultEnvelope 值承载");
    // core 侧：锚定修订与统一快照的身份类型面。
    static_assert(std::is_same_v<decltype(SectionRequest::revision), co::RevisionId>,
                  "SectionRequest.revision 必须为 core::RevisionId");
    static_assert(
        std::is_same_v<decltype(SectionRequest::snapshotId)::value_type,
                       co::ContentIdentity>,
        "SectionRequest.snapshotId 必须以 core::ContentIdentity 承载");

    // 值面往返：envelope 值可置入请求并整体拷贝/比较（§9.2"只读、值拷贝"
    // 的类型级可满足性；夹具 envelope 为默认构造值——仅结构契约验证，
    // 载荷语义归 evidence 单元测试，RP-STATE-4 边界声明）。
    SectionRequest request;
    request.revision = co::RevisionId{};
    request.results.push_back(ev::ResultEnvelope{});
    SectionRequest copy = request;
    EXPECT_EQ(copy, request);
}

/**
 * 验证 acceptance 5（v0.6 偏差登记的兑现）：SectionContent 消费 ReportModel
 * 冻结视图族——条目类型＝SectionEntryView（"SectionEntry ≙ 冻结视图"零
 * 转换）、状态/渲染提示＝ReportModel 枚举本身（本头零重复定义——单一
 * 权威）、章节诊断＝core::DiagnosticRecord（码面权威在 diagnostics）。
 */
TEST(ReportingSectionContract, ContentConsumesFrozenViewTypes_RPT04_ACC5)
{
    // 条目冻结视图（ReportModel.hpp v0.6 暂载——SectionProvider.hpp 消费）。
    static_assert(std::is_same_v<decltype(SectionContent::entries)::value_type,
                                 SectionEntryView>,
                  "SectionContent.entries 必须以 ReportModel 冻结视图承载（零重复定义）");
    static_assert(std::is_same_v<decltype(SectionContent::missingItems)::value_type,
                                 MissingItemView>,
                  "SectionContent.missingItems 必须以 ReportModel 冻结视图承载");
    // 状态/版式枚举：与 ReportModel 侧同一类型（两处声明同名枚举＝双权威
    // ——类型恒等断言排除该漂移面）。
    static_assert(std::is_same_v<decltype(SectionContent::status), SectionStatus>,
                  "SectionContent.status 必须为 ReportModel 的 SectionStatus 本身");
    static_assert(std::is_same_v<decltype(SectionContent::renderHint), RenderHint>,
                  "SectionContent.renderHint 必须为 ReportModel 的 RenderHint 本身");
    // 章节诊断承载 core 公共诊断记录（P-RPT-9 基线：core.md v0.1 §4.8）。
    static_assert(
        std::is_same_v<decltype(SectionContent::diagnostics)::value_type,
                       co::DiagnosticRecord>,
        "SectionContent.diagnostics 必须以 core::DiagnosticRecord 承载");
}

// =====================================================================
// acceptance 4：RPT-SCOPE-INSUFFICIENT 码面与收编表一致性
// =====================================================================

/**
 * 验证 acceptance 4：ScopeInsufficient 诊断挂接使用的稳定码在 diagnostics
 * StableCodeRegistry 收编表中可查且登记一致（ownerUnit=reporting、严重
 * 级别 Warning——"降级建议而非失败事实"的码面语义；P-RPT-8"引用登记非
 * 二次定义"的注册表侧交叉自证）。
 */
TEST(ReportingSectionContract, ScopeInsufficientCodeRegisteredInDiagnostics_RPT04_ACC4)
{
    // 装配内置码表（87 码全量——注册期验证即收编表自洽证明；Identity
    // 契约测试已有全清单交叉用例，此处钉本任务消费的具体码面）。
    dg::StableCodeRegistry registry;
    ASSERT_NO_THROW(dg::registerBuiltinCodes(registry));

    const auto* desc = registry.find(diagcodes::kScopeInsufficient);
    ASSERT_NE(desc, nullptr) << "RPT-SCOPE-INSUFFICIENT 未入收编表";
    EXPECT_EQ(desc->code, std::string{diagcodes::kScopeInsufficient});
    EXPECT_EQ(desc->ownerUnit, "reporting");
    EXPECT_EQ(desc->severity, dg::DiagnosticSeverity::Warning)
        << "ScopeInsufficient 为降级建议（Warning）——拒绝本体走异常/判定码面";
    EXPECT_FALSE(desc->deprecated);

    // 诊断工厂产出的记录码＝登记码（挂接面闭合——makeScopeInsufficient
    // Diagnostic 不会产生第二个码面）。
    const co::DiagnosticRecord rec = makeScopeInsufficientDiagnostic({"trajectory-cycle"});
    EXPECT_EQ(rec.code, std::string{diagcodes::kScopeInsufficient});
}

}  // namespace
