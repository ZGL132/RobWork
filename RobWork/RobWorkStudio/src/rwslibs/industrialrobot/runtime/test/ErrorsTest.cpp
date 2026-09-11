/**
 * @file   ErrorsTest.cpp
 * @brief  错误码全表用例组（RT-ERR）——RuntimeErrorCode 全枚举逐 token：
 *         稳定 token/稳定注册码/错误分类/取消与契约违约判别＋RuntimeError
 *         异常轨＋名称解析两错误值类型。
 *
 * 设计依据：
 *   - units/runtime.md §3.4（枚举全表与 token 注释列——期望值逐项抄录自
 *     该表原文）、§10.11（枚举↔稳定码 v0.3 冻结关系——12 个 1:1＋
 *     2 个 fail-fast token＋2 个事件码）、§5.2/§5.6（产生段与分类归属）、
 *     §7.4（RuntimeResolveError/RuntimeNameError 形态）
 *   - 需求 NFR-COR-03（不静默）；任务契约 tasks/foundation/RT-T02.json
 *     （acceptance 1：错误码全表用例通过——稳定诊断码/错误分类逐 token）
 */

#include <sdurws/ird/runtime/Errors.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

namespace {

using namespace sdurws::ird::runtime;
using namespace sdurws::ird::core;
// 限定符别名：using namespace 只引入成员名，不使 "core::" 限定形式可见
// ——core 强类型（ObjectId 等）在用例中按限定形式书写，别名缺一不可。
namespace core = sdurws::ird::core;

// =====================================================================
// 期望全表：逐值抄录自 units/runtime.md §3.4（token 列）与 §10.11 v0.3
// （注册码冻结关系）。手写期望而非由被测函数反推——防"实现即期望"的
// 恒真用例（验收协议：锚定先行）。
// =====================================================================

/// 单行期望：码值＋三张表答案＋两个判别谓词答案。
struct CodeExpectation {
    RuntimeErrorCode code;              ///< 被测枚举值
    const char* token;                  ///< §3.4 token 注释列原文
    const char* registryCode;           ///< §10.11 注册码（""＝不发稳定码）
    RuntimeErrorCategory category;      ///< §5.2/§5.6 分类归属
    bool isCancellation;                ///< 是否取消（仅 Cancelled）
    bool isContractViolation;           ///< 是否调用方契约违约（仅 Unknown/ContextReleased）
};

/// 全 15 值期望表（顺序＝§3.4 枚举声明顺序——数值稳定性一并钉住）。
const std::vector<CodeExpectation>& expectations()
{
    static const std::vector<CodeExpectation> table = {
        // ---------------- 12 个 1:1 稳定码（§10.11） ----------------
        {RuntimeErrorCode::InputInvalid,          "runtime/input-invalid",          "RT-INPUT-INVALID",          RuntimeErrorCategory::Input,       false, false},
        {RuntimeErrorCode::UnitMismatch,          "runtime/unit-mismatch",          "RT-UNIT-MISMATCH",          RuntimeErrorCategory::Input,       false, false},
        {RuntimeErrorCode::StructureInvalid,      "runtime/structure-invalid",      "RT-STRUCTURE-INVALID",      RuntimeErrorCategory::Input,       false, false},
        {RuntimeErrorCode::ResourceMissing,       "runtime/resource-missing",       "RT-RESOURCE-MISSING",       RuntimeErrorCategory::Resource,    false, false},
        {RuntimeErrorCode::ResourceChanged,       "runtime/resource-changed",       "RT-RESOURCE-CHANGED",       RuntimeErrorCategory::Resource,    false, false},
        {RuntimeErrorCode::ResourceBudget,        "runtime/resource-budget",        "RT-RESOURCE-BUDGET",        RuntimeErrorCategory::Resource,    false, false},
        {RuntimeErrorCode::WorkCellCompileFailed, "runtime/wc-compile-failed",      "RT-WC-COMPILE-FAILED",      RuntimeErrorCategory::Compile,     false, false},
        {RuntimeErrorCode::DwcCompileFailed,      "runtime/dwc-compile-failed",     "RT-DWC-COMPILE-FAILED",     RuntimeErrorCategory::Compile,     false, false},
        {RuntimeErrorCode::NameConflict,          "runtime/name-conflict",          "RT-NAME-CONFLICT",          RuntimeErrorCategory::Name,        false, false},
        {RuntimeErrorCode::BaseWorldInconsistent, "runtime/base-world-inconsistent", "RT-BASE-WORLD-INCONSISTENT", RuntimeErrorCategory::Consistency, false, false},
        {RuntimeErrorCode::CacheIncompatible,     "runtime/cache-incompatible",     "RT-CACHE-INCOMPATIBLE",     RuntimeErrorCategory::Cache,       false, false},
        {RuntimeErrorCode::Cancelled,             "runtime/cancelled",              "RT-CANCELLED",              RuntimeErrorCategory::Cancelled,   true,  false},
        // ---------------- 2 个 fail-fast token（不发稳定码） ----------------
        {RuntimeErrorCode::UnknownObject,         "runtime/unknown-object",         "",                          RuntimeErrorCategory::Context,     false, true},
        {RuntimeErrorCode::ContextReleased,       "runtime/context-released",       "",                          RuntimeErrorCategory::Context,     false, true},
        // ---------------- 1 个事件码路径（§8.4，不发异常携带稳定码） ----------------
        {RuntimeErrorCode::RobWorkError,          "runtime/robwork-error",          "",                          RuntimeErrorCategory::Baseline,    false, false},
    };
    return table;
}

}  // namespace

/** RT-ERR-1 全表逐 token：15 值逐一核对 token/注册码/分类/两判别（acceptance 1）。 */
TEST(RuntimeErrorsFullTable, AllCodesPerToken_RT_ERR_NFR_COR_03)
{
    // 前置锚定：枚举值个数与顺序钉死——15 值、RobWorkError 序号 14。
    // 防止后续任务静默插入/重排枚举值（数值进入二进制契约面）。
    ASSERT_EQ(static_cast<int>(RuntimeErrorCode::RobWorkError), 14)
        << "枚举值个数/顺序漂移——§3.4 全表契约与持久化 token 稳定性被破坏";
    ASSERT_EQ(expectations().size(), static_cast<std::size_t>(15));

    for (const auto& e : expectations()) {
        SCOPED_TRACE(std::string{"码值: "} + e.token);
        // token：非空、统一前缀、与冻结原文逐字一致。
        EXPECT_FALSE(token(e.code).empty()) << "token 不得为空（全函数契约）";
        EXPECT_EQ(token(e.code).substr(0, 8), "runtime/") << "token 前缀约定（§3.4）";
        EXPECT_EQ(token(e.code), std::string_view{e.token});
        // 注册码：12 个 1:1 逐字一致；3 个空串（2 fail-fast＋1 事件路径，§10.11）。
        EXPECT_EQ(registryCode(e.code), std::string_view{e.registryCode});
        // 分类：§5.2/§5.6 归属逐值一致。
        EXPECT_EQ(category(e.code), e.category);
        // 判别谓词：取消仅 Cancelled；契约违约仅 UnknownObject/ContextReleased。
        EXPECT_EQ(isCancellation(e.code), e.isCancellation);
        EXPECT_EQ(isContractViolation(e.code), e.isContractViolation);
    }
}

/** RT-ERR-2 异常轨：消息前缀约定＋code() 访问器＋按基类捕获（§3.4）。 */
TEST(RuntimeErrorsThrow, MessagePrefixAndCodeAccessor_RT_ERR)
{
    // 有细节：what() ＝ "<token>: <detail>"——前缀由构造强制拼装。
    const RuntimeError withDetail{RuntimeErrorCode::NameConflict, "IRB6700.joint_3 与 joint_3 冲突"};
    EXPECT_STREQ(withDetail.what(), "runtime/name-conflict: IRB6700.joint_3 与 joint_3 冲突");
    EXPECT_EQ(withDetail.code(), RuntimeErrorCode::NameConflict);

    // 无细节：what() 恰为 token（无尾随冒号空格）。
    const RuntimeError noDetail{RuntimeErrorCode::ResourceBudget};
    EXPECT_STREQ(noDetail.what(), "runtime/resource-budget");
    EXPECT_EQ(noDetail.code(), RuntimeErrorCode::ResourceBudget);

    // 异常轨按基类捕获（std::runtime_error/std::exception 双层兼容——
    // 调用方可以既有异常体系接住 runtime 失败）。
    try {
        throw RuntimeError{RuntimeErrorCode::BaseWorldInconsistent, "S9 检查失败"};
    } catch (const std::runtime_error& ex) {
        EXPECT_STREQ(ex.what(), "runtime/base-world-inconsistent: S9 检查失败");
    }
    try {
        throw RuntimeError{RuntimeErrorCode::WorkCellCompileFailed, "stage=S6"};
    } catch (const std::exception& ex) {
        EXPECT_STREQ(ex.what(), "runtime/wc-compile-failed: stage=S6");
    }
}

/** RT-ERR-3 解析错误载荷：字段语义与原名回显（§7.4/RT-NM-3 错误含回显）。 */
TEST(RuntimeErrorsPayload, ResolveAndNameErrorCarryEcho_RT_ERR)
{
    // 名称解析失败：UnknownObject＋detail＋requestedName 原名回显
    // （§7.4 接口属性表——"UnknownObject（含原名回显）"）。
    const RuntimeResolveError resolve{RuntimeErrorCode::UnknownObject,
                                      "空名/未命中同码",
                                      "IRB6700.joint_99"};
    EXPECT_EQ(resolve.code, RuntimeErrorCode::UnknownObject);
    EXPECT_EQ(resolve.requestedName, "IRB6700.joint_99");

    // 反解失败：requestedId 为 core 强类型（名称/身份混用在类型层不可能
    // ——RT-NM-7 纪律的载荷面体现）。
    core::ObjectId id{};
    id.bytes[0] = 0xAB;   // 非零即有效（保留值纪律：全零＝空）
    const RuntimeNameError name{RuntimeErrorCode::UnknownObject, "id 不在本快照映射", id};
    EXPECT_EQ(name.code, RuntimeErrorCode::UnknownObject);
    EXPECT_EQ(name.requestedId, id);
    EXPECT_TRUE(name.requestedId.isValid());
}
