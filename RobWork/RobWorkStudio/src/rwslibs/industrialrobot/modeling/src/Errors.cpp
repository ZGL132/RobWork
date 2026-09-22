/**
 * @file   Errors.cpp
 * @brief  modeling 错误契约的实现——错误码 token 转发表与域错误→稳定
 *         诊断码映射数据。
 *
 * 设计依据：
 *   - units/modeling.md §3.3（Errors.hpp 行）、§9.5 尾段（域错误→码映射
 *     纪律与"禁字符串拼码"）、§9.4.1/§9.4.2/§9.4.5（枚举值出处接口）
 *   - 任务契约 tasks/foundation/WP-13-T02.json acceptance 4
 *
 * 背景说明：本 TU 承载两张**编译期固定表**——
 *   ①modelingErrorCodeToken：枚举→token 转发表（switch 全枚举，与
 *     Errors.hpp 枚举声明失同步时由测试全表机械比对暴露）；
 *   ②modelingDiagCode：域错误→已登记稳定码映射。映射阶段纪律（文件头
 *     注）：当前仅 SchemaVersionUnsupported→MDL-READINESS-SCHEMA-
 *     UNSUPPORTED（§9.5 T02/T03 行——与 DiagCodes.hpp 工厂登记同源同
 *     串，测试交叉核对）；其余值 nullopt＝暂无已登记映射，映射随其
 *     生产者任务在 §9.5 纪律内登记，本任务不私定码值、不预建映射行。
 *
 * 确定性（NFR-COR-02）：全部产出为编译期固定字面量，同码同串/同映射。
 */

#include <sdurws/ird/modeling/Errors.hpp>

namespace sdurws::ird::modeling {

std::string_view modelingErrorCodeToken(ModelingErrorCode code) noexcept
{
    // token 转发表：switch 全枚举、无 default（漏表项时编译器告警暴露）。
    // token＝§9.4/V 矩阵行文指称错误的成员名原文串（如 V-04"RefProtected
    // 错误码"）；分支顺序＝枚举声明序（§9.4 首次出现序）。
    switch (code) {
    case ModelingErrorCode::DuplicateObjectId:
        return "DuplicateObjectId";
    case ModelingErrorCode::RefProtected:
        return "RefProtected";
    case ModelingErrorCode::AuthorityViolation:
        return "AuthorityViolation";
    case ModelingErrorCode::UnitIllegal:
        return "UnitIllegal";
    case ModelingErrorCode::NotFinite:
        return "NotFinite";
    case ModelingErrorCode::CentroidEditUnresolved:
        return "CentroidEditUnresolved";
    case ModelingErrorCode::BatchPartial:
        return "BatchPartial";
    case ModelingErrorCode::TemplateDisabled:
        return "TemplateDisabled";
    case ModelingErrorCode::IllegalName:
        return "IllegalName";
    case ModelingErrorCode::RefMissing:
        return "RefMissing";
    case ModelingErrorCode::DhExpandFailed:
        return "DhExpandFailed";
    case ModelingErrorCode::SchemaVersionUnsupported:
        return "SchemaVersionUnsupported";
    case ModelingErrorCode::MalformedPayload:
        return "MalformedPayload";
    }
    // 全枚举已覆盖，不达此处（ARC-04：不猜测——漏表项由编译器拦截，
    // 不设静默兜底串）。
    return "DuplicateObjectId";
}

std::optional<std::string_view> modelingDiagCode(ModelingErrorCode code) noexcept
{
    // 映射数据（§9.5 尾段"每值登记映射码"的当前已登记行）。仅一行：
    // SchemaVersionUnsupported→MDL-READINESS-SCHEMA-UNSUPPORTED（§9.5
    // T02/T03 行——码串与 DiagCodes.hpp 工厂登记同源；该域错误的语义
    // 与码行语义逐字对应"对象 schema 主版本超出本程序支持"）。
    //
    // 其余 11 值显式 nullopt（不是遗漏）：①"无独立码时复用校验码族"
    // （§9.5 括注——如 AuthorityViolation）的具体复用属产出点语义裁决，
    // 随生产者任务登记；②生产者接口未落位（T03+）的值，映射随 §9.5
    // 对应码行注册同批落地。nullopt 时调用方不得产诊断（产码唯一经
    // IDiagnosticFactory::create 且码须已注册——禁字符串拼码）。
    switch (code) {
    case ModelingErrorCode::SchemaVersionUnsupported:
        return std::optional<std::string_view>{"MDL-READINESS-SCHEMA-UNSUPPORTED"};
    case ModelingErrorCode::DuplicateObjectId:
    case ModelingErrorCode::RefProtected:
    case ModelingErrorCode::AuthorityViolation:
    case ModelingErrorCode::UnitIllegal:
    case ModelingErrorCode::NotFinite:
    case ModelingErrorCode::CentroidEditUnresolved:
    case ModelingErrorCode::BatchPartial:
    case ModelingErrorCode::TemplateDisabled:
    case ModelingErrorCode::IllegalName:
    case ModelingErrorCode::RefMissing:
    case ModelingErrorCode::DhExpandFailed:
    case ModelingErrorCode::MalformedPayload:
        // 暂无已登记映射码（文件头/Errors.hpp 注：阶段纪律——不私定、
        // 不预建；错误经值面返回，不落诊断）。MalformedPayload 属解码
        // 数据错误面，语义随 §9.5 对应码行（如有）注册时登记映射。
        return std::nullopt;
    }
    // 全枚举已覆盖，不达此处。
    return std::nullopt;
}

}  // namespace sdurws::ird::modeling
