/**
 * @file   Errors.cpp
 * @brief  runtime 错误契约实现——token/稳定注册码/分类三张固定全表＋
 *         RuntimeError 消息拼装。
 *
 * 设计依据：
 *   - units/runtime.md §3.4（错误类型——枚举全表与 token 注释列的原文契约）、
 *     §10.11（与 diagnostics：枚举↔稳定码关系 v0.3 冻结——12 个 1:1＋
 *     2 个 fail-fast token＋2 个事件码）、§5.2/§5.6（错误产生段归属与
 *     分类语义）、§8.4（robwork-error 事件转译路径）
 *   - 需求 NFR-COR-02/03（确定性：同码同串；不静默：前置违约 fail-fast）
 *   - 任务契约 tasks/foundation/RT-T02.json（acceptance 1——错误码全表
 *     逐 token 用例的载体）
 *
 * 实现说明（确定性来源，NFR-COR-02）：三张表均为 switch 全枚举、无 default
 * 分支——新增枚举值而未登记表项时，switch 全枚举告警（MSVC C4061/C4062，
 * /W4 及以上可见）会暴露遗漏；无论告警级别如何，测试侧另有全表用例逐值
 * 钉住（强制防线，ErrorsTest RT-ERR-1）。所有返回值为静态存储期字面量，
 * 无堆分配、无 locale 依赖——跨平台跨进程逐字节一致。
 */

#include <sdurws/ird/runtime/Errors.hpp>

#include <utility>

namespace sdurws::ird::runtime {

namespace {

/**
 * @brief 拼装 RuntimeError 的 what() 消息（前缀约定强制点）。
 *
 * 约定（§3.4）：what() ＝ "<token>[: <detail>]"——token 前缀由本函数统一
 * 拼装，构造路径无裸消息入口，保证异常消息可按 "runtime/..." 稳定检索；
 * detail 为空时恰为 token（无尾随冒号空格，避免噪音字符）。
 *
 * @param code   [in] 稳定错误码
 * @param detail [in] 开发诊断细节（就地定位信息；可为空）
 * @return 完整 what() 消息（供 std::runtime_error 基类拷贝持有）
 */
std::string makeWhat(RuntimeErrorCode code, std::string&& detail)
{
    // 第一步：token 前缀（全表非空——token() 契约）。
    std::string what(token(code));
    // 第二步：有细节才追加 ": "（空细节消息恰为 token，无噪音）。
    if (!detail.empty()) {
        what += ": ";
        what += detail;
    }
    return what;
}

}  // namespace

std::string_view token(RuntimeErrorCode code) noexcept
{
    // token 表：与 §3.4 枚举注释列逐字一致（稳定 token——持久化于诊断/报告，
    // 一经交付不得改写）。全 15 值非空（"runtime/..." 前缀统一）。
    switch (code) {
    case RuntimeErrorCode::InputInvalid:          return "runtime/input-invalid";
    case RuntimeErrorCode::UnitMismatch:          return "runtime/unit-mismatch";
    case RuntimeErrorCode::StructureInvalid:      return "runtime/structure-invalid";
    case RuntimeErrorCode::ResourceMissing:       return "runtime/resource-missing";
    case RuntimeErrorCode::ResourceChanged:       return "runtime/resource-changed";
    case RuntimeErrorCode::ResourceBudget:        return "runtime/resource-budget";
    case RuntimeErrorCode::WorkCellCompileFailed: return "runtime/wc-compile-failed";
    case RuntimeErrorCode::DwcCompileFailed:      return "runtime/dwc-compile-failed";
    case RuntimeErrorCode::NameConflict:          return "runtime/name-conflict";
    case RuntimeErrorCode::BaseWorldInconsistent: return "runtime/base-world-inconsistent";
    case RuntimeErrorCode::CacheIncompatible:     return "runtime/cache-incompatible";
    case RuntimeErrorCode::Cancelled:             return "runtime/cancelled";
    case RuntimeErrorCode::UnknownObject:         return "runtime/unknown-object";
    case RuntimeErrorCode::ContextReleased:       return "runtime/context-released";
    case RuntimeErrorCode::RobWorkError:          return "runtime/robwork-error";
    }
    // 不可达：switch 已覆盖全枚举（无 default——遗漏新值时编译器告警）。
    // 防御性返回空串（调用方以 empty 判异常值，测试保证不触达）。
    return {};
}

std::string_view registryCode(RuntimeErrorCode code) noexcept
{
    // 稳定注册码表：§10.11 v0.3 冻结关系（码值权威＝diagnostics
    // StableCodeRegistry，runtime 只产出建议码面）。
    // 空串三值的冻结依据（§10.11 原文）：
    //   - UnknownObject/ContextReleased＝调用方契约违约（fail-fast 轨，
    //     token 仅供日志，不发稳定码）；
    //   - RobWorkError＝诊断事件码路径（RT-ROBWORK-ERROR 经 §8.4
    //     translateRobWorkError 转译产出，非 RuntimeError 异常携带码）。
    switch (code) {
    case RuntimeErrorCode::InputInvalid:          return "RT-INPUT-INVALID";
    case RuntimeErrorCode::UnitMismatch:          return "RT-UNIT-MISMATCH";
    case RuntimeErrorCode::StructureInvalid:      return "RT-STRUCTURE-INVALID";
    case RuntimeErrorCode::ResourceMissing:       return "RT-RESOURCE-MISSING";
    case RuntimeErrorCode::ResourceChanged:       return "RT-RESOURCE-CHANGED";
    case RuntimeErrorCode::ResourceBudget:        return "RT-RESOURCE-BUDGET";
    case RuntimeErrorCode::WorkCellCompileFailed: return "RT-WC-COMPILE-FAILED";
    case RuntimeErrorCode::DwcCompileFailed:      return "RT-DWC-COMPILE-FAILED";
    case RuntimeErrorCode::NameConflict:          return "RT-NAME-CONFLICT";
    case RuntimeErrorCode::BaseWorldInconsistent: return "RT-BASE-WORLD-INCONSISTENT";
    case RuntimeErrorCode::CacheIncompatible:     return "RT-CACHE-INCOMPATIBLE";
    case RuntimeErrorCode::Cancelled:             return "RT-CANCELLED";
    case RuntimeErrorCode::UnknownObject:         return {};   // fail-fast token，不发稳定码
    case RuntimeErrorCode::ContextReleased:       return {};   // fail-fast token，不发稳定码
    case RuntimeErrorCode::RobWorkError:          return {};   // 事件码 RT-ROBWORK-ERROR 走诊断转译路径
    }
    return {};
}

RuntimeErrorCategory category(RuntimeErrorCode code) noexcept
{
    // 分类表：按 §5.2 十段表的产生段归属＋§5.6 正交关系归类（详见
    // Errors.hpp 中 RuntimeErrorCategory 各值注释）。逐值一一对应，
    // 全表用例（ErrorsTest）钉住——错误归属可定位的码面基础。
    switch (code) {
    case RuntimeErrorCode::InputInvalid:          return RuntimeErrorCategory::Input;
    case RuntimeErrorCode::UnitMismatch:          return RuntimeErrorCategory::Input;
    case RuntimeErrorCode::StructureInvalid:      return RuntimeErrorCategory::Input;
    case RuntimeErrorCode::ResourceMissing:       return RuntimeErrorCategory::Resource;
    case RuntimeErrorCode::ResourceChanged:       return RuntimeErrorCategory::Resource;
    case RuntimeErrorCode::ResourceBudget:        return RuntimeErrorCategory::Resource;
    case RuntimeErrorCode::WorkCellCompileFailed: return RuntimeErrorCategory::Compile;
    case RuntimeErrorCode::DwcCompileFailed:      return RuntimeErrorCategory::Compile;
    case RuntimeErrorCode::NameConflict:          return RuntimeErrorCategory::Name;
    case RuntimeErrorCode::BaseWorldInconsistent: return RuntimeErrorCategory::Consistency;
    case RuntimeErrorCode::CacheIncompatible:     return RuntimeErrorCategory::Cache;
    case RuntimeErrorCode::Cancelled:             return RuntimeErrorCategory::Cancelled;
    case RuntimeErrorCode::UnknownObject:         return RuntimeErrorCategory::Context;
    case RuntimeErrorCode::ContextReleased:       return RuntimeErrorCategory::Context;
    case RuntimeErrorCode::RobWorkError:          return RuntimeErrorCategory::Baseline;
    }
    return RuntimeErrorCategory::Baseline;   // 不可达（同上防御性收敛值）
}

bool isCancellation(RuntimeErrorCode code) noexcept
{
    // 仅 Cancelled 是取消（正常控制流，UX-03：取消诊断非 error 级）；
    // 其余 14 值（含全部错误码）恒 false——防把失败误判为取消。
    return code == RuntimeErrorCode::Cancelled;
}

bool isContractViolation(RuntimeErrorCode code) noexcept
{
    // 冻结清单（§10.11 v0.3）：仅 UnknownObject/ContextReleased 属调用方
    // 契约违约（fail-fast 轨）。注意二者仍保有 token（日志可定位），
    // 只是走异常路径而非稳定码登记路径。
    return code == RuntimeErrorCode::UnknownObject
        || code == RuntimeErrorCode::ContextReleased;
}

RuntimeError::RuntimeError(RuntimeErrorCode code, std::string detail)
    : std::runtime_error(makeWhat(code, std::move(detail)))
    , m_code(code)
{
}

}  // namespace sdurws::ird::runtime
