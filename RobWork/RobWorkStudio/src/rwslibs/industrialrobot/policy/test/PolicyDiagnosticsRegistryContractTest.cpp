/**
 * @file PolicyDiagnosticsRegistryContractTest.cpp
 * @brief POL-T13/ERR-01：policy 全码表对正式 diagnostics 注册表与工厂的契约回归。
 * 产品库不增加反向依赖；本测试复用真实注册和创建路径，防建议码落不到宿主。
 */
#include <gtest/gtest.h>
#include <sdurws/ird/diagnostics/DiagCodes.hpp>
#include <sdurws/ird/diagnostics/Factory.hpp>
#include <sdurws/ird/policy/Diagnostics.hpp>

namespace {
/** @brief 测试固定时钟；纯值、只读、可并发调用，不影响生产时间来源。 */
class FixedPolicyClock final : public sdurws::ird::diagnostics::IClock
{
  public:
    /** @brief 返回固定 UTC epoch，确保条目比较不依赖执行时间。 */
    std::chrono::system_clock::time_point nowUtc () const override { return {}; }
};
}    // namespace

/** @brief POL-T13：逐个生产码必须有正式描述符并可创建条目；缺一个即失败。 */
TEST (PolicyDiagnosticsRegistryContract, EveryEmittedCodeIsAccepted_POL_T13)
{
    using namespace sdurws::ird;
    diagnostics::StableCodeRegistry registry;
    diagnostics::registerBuiltinCodes (registry);
    registry.seal ();
    FixedPolicyClock clock;
    diagnostics::DiagnosticsFactory factory (registry, clock);
    factory.seal ();
    diagnostics::DiagContext context;
    context.sourceUnit      = "policy";
    context.sourceInterface = "policy.review";
    for (const auto code : policy::policyDiagCodes ()) {
        SCOPED_TRACE (std::string (code));
        const auto* descriptor = registry.find (code);
        EXPECT_NE (descriptor, nullptr);
        const auto record = policy::PolicyDiagnostics{}.make (
            code, core::ObjectId::generate (), "策略回归", "验证正式码表契约", "修正输入后重评");
        EXPECT_NO_THROW ({
            const auto entry = factory.create (record, context);
            EXPECT_EQ (entry.record.code, code);
        });
    }
    // 使用正式既有码拼写；枚举名保留，历史错误拼写不再作为新记录输出。
    EXPECT_EQ (policy::policyDiagCode (policy::PolicyDiagCode::JntEngineeringRangeInvalid),
               "POLICY-ENGINEERING-RANGE-INVALID");
}
