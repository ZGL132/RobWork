/**
 * @file   Errors.cpp
 * @brief  requirements 错误契约的实现——RequirementErrorCode 全表 token
 *         转发表与域错误→稳定诊断码映射数据（§9.3~§9.5 @错误 行的唯一
 *         映射点）。
 *
 * 设计依据：
 *   - units/requirements.md §9.3（loadBaseline @pre——解码失败→
 *     RequirementError(SchemaVersionUnsupported)）、§9.4（createPoint
 *     @错误 行）、§9.5（buildPlan @错误 行）、§9.6（REQ- 稳定码分批
 *     注册纪律）
 *   - 先例：modeling/src/Errors.cpp（modelingErrorCodeToken/
 *     modelingDiagCode 同款形态）
 *   - 任务契约 tasks/foundation/WP-14-T02.json acceptance 3
 *
 * 确定性（NFR-COR-02）：token 与映射均为编译期字面量表，switch 全枚举
 * 无 default——新增枚举值未登记表项时编译器告警暴露遗漏。
 *
 * 线程安全：全部纯函数（无共享可变状态），并发只读安全。
 */

#include <sdurws/ird/requirements/Errors.hpp>

#include <sdurws/ird/requirements/DiagCodes.hpp>  // kReqSchemaUnsupported——映射码与工厂登记同源同串（禁第二处字面量）

namespace sdurws::ird::requirements {

std::string_view requirementErrorCodeToken(RequirementErrorCode code) noexcept
{
    // token＝枚举成员名原文（§9.3~§9.5 @错误 行行文即以成员名指称错误）；
    // 与枚举声明序一一对应（登记簿纪律：两处失同步由测试全表机械比对
    // 暴露）。
    switch (code) {
    case RequirementErrorCode::SchemaVersionUnsupported:
        return "SchemaVersionUnsupported";
    case RequirementErrorCode::DuplicateName: return "DuplicateName";
    case RequirementErrorCode::IllegalTolerance: return "IllegalTolerance";
    case RequirementErrorCode::ZeroVectorTarget: return "ZeroVectorTarget";
    case RequirementErrorCode::AllDofFree: return "AllDofFree";
    case RequirementErrorCode::DegenerateRegion: return "DegenerateRegion";
    case RequirementErrorCode::NegativeCount: return "NegativeCount";
    case RequirementErrorCode::RegionNotBox: return "RegionNotBox";
    // WP-14-T03 表尾增列（units/requirements.md §14.6 v0.3——canonical
    // 编解码 decode 校验链的失败码，见 Errors.hpp 枚举注释）。
    case RequirementErrorCode::MalformedPayload: return "MalformedPayload";
    }
    // switch 已全枚举（无 default）；到达此处仅可能是未定义枚举值
    // （UB 防御面），返回空串不猜测。
    return {};
}

std::optional<std::string_view> requirementDiagCode(RequirementErrorCode code) noexcept
{
    // 映射阶段纪律（§9.6 分批注册）：只映射已到任务行的 REQ-* 码——
    // 当前仅 SchemaVersionUnsupported（§9.6 T02/T03 行 REQ-SCHEMA-
    // UNSUPPORTED：生产界面＝editor loadBaseline 解码失败＋codec decode，
    // 本任务同批登记工厂）；其余值的生产者接口随 T03/T04/T05 落地、其
    // §9.6 码行属 T04/T05 批次——映射随码行注册同批追加，不预建（nullopt＝
    // 调用方不得产诊断，错误经值面返回）。WP-14-T03 表尾增列的
    // MalformedPayload 无映射码行（字节面错误不产诊断——值面返回）。
    switch (code) {
    case RequirementErrorCode::SchemaVersionUnsupported:
        return kReqSchemaUnsupported;
    case RequirementErrorCode::DuplicateName:
    case RequirementErrorCode::IllegalTolerance:
    case RequirementErrorCode::ZeroVectorTarget:
    case RequirementErrorCode::AllDofFree:
    case RequirementErrorCode::DegenerateRegion:
    case RequirementErrorCode::NegativeCount:
    case RequirementErrorCode::RegionNotBox:
    case RequirementErrorCode::MalformedPayload:
        return std::nullopt;  // 暂无已登记映射码——分批纪律（文件头注）
    }
    return std::nullopt;
}

}  // namespace sdurws::ird::requirements
