/**
 * @file   DiagCodes.cpp
 * @brief  requirements 稳定诊断码工厂的实现——§9.6 已到任务行描述符清单
 *         与注册函数。
 *
 * 设计依据：
 *   - units/requirements.md §9.6（REQ-SCHEMA-UNSUPPORTED 行——"error／
 *     对象 schema 主版本超出支持"，任务列 T02/T03）
 *   - units/diagnostics.md §4.5（CodeDescriptor 字段约束；titleKey/
 *     detailKey 命名约定；paramSchema 受限 JSON 形命名参数清单）
 *   - 先例：modeling/src/DiagCodes.cpp（同义码 MDL-READINESS-SCHEMA-
 *     UNSUPPORTED 的逐字段登记口径——分类 FormatOrVersion/paramSchema
 *     三键/retryable UserRetry 全部同构，两码是同一语义事件在两域的
 *     登记面）
 *   - 任务契约 tasks/foundation/WP-14-T02.json acceptance 3
 *
 * 背景说明：清单当前 1 项（§9.6 任务列含 T02 的行）——分批注册纪律
 * （"不预建无消费者条目"）的执行口径见头文件 DiagCodes.hpp 文件头注；
 * 其余 13 行随各自任务在**本清单表尾追加**（表尾追加＝登记簿纪律，不
 * 重排既有项）。
 *
 * 确定性（NFR-COR-02）：清单序＝§9.6 表行序；每次调用返回同序同值
 * 新清单（描述符为纯值聚合）。
 */

#include <sdurws/ird/requirements/DiagCodes.hpp>

namespace sdurws::ird::requirements {

std::vector<diagnostics::CodeDescriptor> requirementCodeDescriptors()
{
    // 清单序＝§9.6 表行序（确定性序）；码值经 DiagCodes.hpp 常量引用
    // （唯一书写点——与 Errors.cpp 映射、T03+ 消费者产码共用，禁字符串
    // 拼码）。

    // ---- §9.6 T02/T03 行：REQ-SCHEMA-UNSUPPORTED ----
    // 对象 schema 主版本超出本程序支持（→升级程序/重新编辑）。逐字段
    // 取值依据见头文件 requirementCodeDescriptors() 注释（分类 FormatOr
    // Version＝diagnostics §4.3 词表"format-or-version——旧格式/未来
    // 版本/schema/契约不兼容"族；paramSchema 三键与 Errors.hpp
    // requirementDiagCode 映射码行同源对齐；modeling 同义码
    // MDL-READINESS-SCHEMA-UNSUPPORTED 同款投影——同一语义事件的两域
    // 登记面）。
    diagnostics::CodeDescriptor d;
    d.code = std::string(kReqSchemaUnsupported);  // §9.6 表"码"列原文（不私定码值）
    d.ownerUnit = "requirements";                 // §4.5 前缀-所有权表：REQ→requirements
    d.category = diagnostics::DiagnosticCategory::FormatOrVersion;
    d.severity = diagnostics::DiagnosticSeverity::Error;  // §9.6"级别"列：error
    d.titleKey = "diag.req-schema-unsupported.title";     // P-DIAG-9 命名约定
    d.detailKey = "diag.req-schema-unsupported.detail";   // （键/值分离，值归文案资源）
    d.paramSchema = R"(["object-type","schema-version","supported-major"])";
    //        （参数名词形＝diagnostics 注册期 ^[a-z0-9]+(-[a-z0-9]+)*$——
    //          小写连字符；三键语义：哪类需求对象/实际 schema 主版本/
    //          本程序支持的最高主版本）
    d.confirmable = false;  // schema 超版必须升级/重编辑，无放行分支（modeling 同义码同值）
    d.requiresComparison = false;  // 非"比较型三要素"码（§4.5 confirmable⇒requiresComparison 的逆不成立）
    d.retryable = diagnostics::RetryKind::UserRetry;  // 建议动作"升级程序/重新编辑"＝fix-input 族
    d.userVisible = true;    // 非 Dev 码（§4.5 Dev 强制三项 false 不适用）
    d.reportable = true;     // 进报告（升级追溯面）
    d.historical = true;     // 进项目历史（§4.5 持久化契约面）
    d.registryVersion = 1;   // 首次登记
    d.deprecated = false;    // 未废弃（tombstone 仅 §4.5.1 废弃流程置位）
    // d.supersededBy 保持 nullopt（无迁移目标——optional 缺省即空）。

    return {d};
}

void registerRequirementCodes(diagnostics::IDiagnosticRegistry& registry)
{
    // 逐条转发注册：注册期验证（句法/前缀-所有权/键唯一/paramSchema）
    // 全部在注册表内执行；任何一条失败即抛 DiagnosticsError——装配期
    // fail-fast，不静默跳过（NFR-MNT-03 边界拒绝；modeling
    // registerModelingCodes 同款语义）。
    for (const auto& descriptor : requirementCodeDescriptors()) {
        registry.registerCode(descriptor);
    }
}

}  // namespace sdurws::ird::requirements
