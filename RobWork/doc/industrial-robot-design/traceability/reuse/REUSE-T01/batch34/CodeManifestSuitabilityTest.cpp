/**
 * @file CodeManifestSuitabilityTest.cpp
 * @brief REUSE-T01/B34：以独立固定字节oracle核清单摘要，不重写通用编码器。
 *
 * 依据DiagCodes.cpp的IRDDCM2字段顺序；期望来自oracle.py的struct/hashlib，
 * 不能用产品ContentDigester生成期望后与自己比较。对象限本测试线程。
 */
#include <gtest/gtest.h>
#include <sdurws/ird/diagnostics/DiagCodes.hpp>
#include <sdurws/ird/diagnostics/Errors.hpp>
#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/core/DiagData.hpp>

namespace {
namespace diag = sdurws::ird::diagnostics;
namespace core = sdurws::ird::core;

/** @brief 固定完整描述符；version [无参数]取0x01020304以覆盖u32四字节序。 */
diag::CodeDescriptor fixedDescriptor()
{
    diag::CodeDescriptor d;
    d.code = "RT-AUDIT";
    d.ownerUnit = "runtime";
    d.category = diag::DiagnosticCategory::InputInvalid;
    d.severity = diag::DiagnosticSeverity::Error;
    d.titleKey = "diag.rt-audit.title";
    d.detailKey = "diag.rt-audit.detail";
    d.paramSchema = "[]";
    d.confirmable = false;
    d.requiresComparison = false;
    d.retryable = diag::RetryKind::UserRetry;
    d.userVisible = d.reportable = d.historical = true;
    d.registryVersion = 0x01020304u;
    d.deprecated = false;
    d.level = core::DiagnosticLevel::Error;
    return d;
}

/** 空清单只有magic，也必须有确定摘要；不等价全零占位身份。 */
TEST(CodeManifestAudit, EmptyMatchesIndependentHashlib_DT_REG_1)
{
    diag::StableCodeRegistry registry;
    EXPECT_EQ(registry.manifest().digest,
              core::ContentIdentity::fromCanonical("cid-6c3ac4c235162eac1d655584e02ccb1fb291a8b537691e9b61e8243333adc0e4"));
}

/** 非空及tombstone两个布局都对照固定oracle，原顺序稳定测试继续复用。 */
TEST(CodeManifestAudit, FixedDescriptorAndTombstoneMatchIndependentHashlib_DT_REG_1)
{
    diag::StableCodeRegistry registry;
    registry.registerCode(fixedDescriptor());
    EXPECT_EQ(registry.manifest().digest,
              core::ContentIdentity::fromCanonical("cid-e896f4a9fda629b86c567b3c8a65614dcdd9c220e3bd1f1f21924773ff78e80e"));
    registry.deprecate("RT-AUDIT", "RT-NEXT");
    EXPECT_EQ(registry.manifest().digest,
              core::ContentIdentity::fromCanonical("cid-6ff05abc63e4eea1977c93a6063fcdd87ed089dfe39d2bed582700c652ad7d8e"));
    ASSERT_EQ(registry.registeredCodes("runtime").size(), 1u);
    ASSERT_NE(registry.find("RT-AUDIT"), nullptr);
    EXPECT_TRUE(registry.find("RT-AUDIT")->deprecated);
}

/** seal后必须拒绝写操作；不把std::map或上游PropertyMap当作码权威。 */
TEST(CodeManifestAudit, SealPreservesExistingLookupAndRejectsWrites_DT_REG_2)
{
    diag::StableCodeRegistry registry;
    registry.registerCode(fixedDescriptor());
    registry.seal();
    ASSERT_NE(registry.find("RT-AUDIT"), nullptr);
    EXPECT_THROW(registry.registerCode(fixedDescriptor()), diag::DiagnosticsError);
    EXPECT_THROW(registry.deprecate("RT-AUDIT", "RT-NEXT"), diag::DiagnosticsError);
    EXPECT_EQ(registry.find("runtime_error: invalid"), nullptr);
}

/** 两处同文法不是两种业务语义：核公开入口等价后才提出既有判定复用。 */
TEST(CodeManifestAudit, CoreAndRegistryShareCodeSyntaxForAsciiInputs)
{
    const auto check = [](const std::string& code) {
        bool coreAccepted = true;
        try {
            (void)core::DiagnosticRecord::make(code, {}, {}, {}, "c", "c", "a");
        } catch (const core::CoreError&) {
            coreAccepted = false;
        }
        ASSERT_EQ(coreAccepted, diag::isValidDiagCodeSyntax(code)) << code;
    };
    check("");
    check(std::string(64, 'A'));
    check(std::string(65, 'A'));
    for (int first = 0; first < 128; ++first) {
        check(std::string(1, static_cast<char>(first)));
        for (int second = 0; second < 128; ++second) {
            check(std::string{static_cast<char>(first), static_cast<char>(second)});
        }
    }
}
} // namespace
