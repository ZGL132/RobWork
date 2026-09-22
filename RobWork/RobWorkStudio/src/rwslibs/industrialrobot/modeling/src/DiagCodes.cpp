/**
 * @file   DiagCodes.cpp
 * @brief  modeling 稳定诊断码工厂的实现——§9.5 已到任务行描述符清单与
 *         注册函数。
 *
 * 设计依据：
 *   - units/modeling.md §9.5（MDL-READINESS-SCHEMA-UNSUPPORTED 行——
 *     "校验/error、confirmable false、对象 schema 主版本超出本程序支持
 *     →升级程序/重新编辑"）
 *   - units/diagnostics.md §4.5（CodeDescriptor 字段约束；titleKey/
 *     detailKey 命名约定；paramSchema 受限 JSON 形命名参数清单）
 *   - 先例：io/src/IoDiagnostics.cpp（ioCodeDescriptors 逐字段登记口径）
 *   - 任务契约 tasks/foundation/WP-13-T02.json acceptance 4
 *
 * 背景说明：清单当前 6 项（§9.5 任务列含 T02 的行＋WP-13-T05 登记的
 * T05 行五码）——分批注册纪律（"不预建无消费者条目"）的执行口径见
 * 头文件 DiagCodes.hpp 文件头注；其余行随各自任务在**本清单表尾追加**
 * （表尾追加＝登记簿纪律，不重排既有项）。
 *
 * 确定性（NFR-COR-02）：清单序＝§9.5 表行序；每次调用返回同序同值
 * 新清单（描述符为纯值聚合）。
 */

#include <sdurws/ird/modeling/DiagCodes.hpp>

namespace sdurws::ird::modeling {

std::vector<diagnostics::CodeDescriptor> modelingCodeDescriptors()
{
    // 清单序＝§9.5 表行序（确定性序）；每码的码值经 DiagCodes.hpp 常量
    // 引用（唯一书写点——与 Import.cpp 产码共用，禁字符串拼码）。

    // ---- §9.5 T02/T03 行：MDL-READINESS-SCHEMA-UNSUPPORTED ----
    // 对象 schema 主版本超出本程序支持（→升级程序/重新编辑）。逐字段
    // 取值依据见头文件 modelingCodeDescriptors() 注释（分类 FormatOr
    // Version＝diagnostics §4.3 词表"format-or-version——旧格式/未来
    // 版本/schema/契约不兼容"族；paramSchema 三键与 Errors.hpp
    // modelingDiagCode 映射码行同源对齐）。
    diagnostics::CodeDescriptor d;
    d.code = std::string(kMdlReadinessSchemaUnsupported);  // §9.5 表"码"列原文（不私定码值）
    d.ownerUnit = "modeling";                     // §4.5 前缀-所有权表：MDL→modeling
    d.category = diagnostics::DiagnosticCategory::FormatOrVersion;
    d.severity = diagnostics::DiagnosticSeverity::Error;  // §9.5"级别"列：error
    d.titleKey = "diag.mdl-readiness-schema-unsupported.title";   // P-DIAG-9 命名约定
    d.detailKey = "diag.mdl-readiness-schema-unsupported.detail"; // （键/值分离，值归文案资源）
    d.paramSchema = R"(["object-type","schema-version","supported-major"])";
    //        （参数名词形＝diagnostics 注册期 ^[a-z0-9]+(-[a-z0-9]+)*$——
    //          小写连字符；三键语义：哪类对象/实际 schema 主版本/本程序
    //          支持的最高主版本）
    d.confirmable = false;  // §9.5"confirmable"列：false（超版必须升级/重编辑，无放行分支）
    d.requiresComparison = false;  // 非"比较型三要素"码（confirmable=false 同款；§4.5 confirmable⇒requiresComparison 的逆不成立）
    d.retryable = diagnostics::RetryKind::UserRetry;  // 建议动作"升级程序/重新编辑"＝fix-input 族
    d.userVisible = true;    // 非 Dev 码（§4.5 Dev 强制三项 false 不适用）
    d.reportable = true;     // 进报告（升级追溯面）
    d.historical = true;     // 进项目历史（§4.5 持久化契约面）
    d.registryVersion = 1;   // 首次登记
    d.deprecated = false;    // 未废弃（tombstone 仅 §4.5.1 废弃流程置位）
    // d.supersededBy 保持 nullopt（无迁移目标——optional 缺省即空）。

    // ---- §9.5 T05 行：MDL-IMPORT-UNSUPPORTED-JOINT（WP-13-T05 登记）----
    // mimic/planar/floating/闭环在所选主链→移除或改拓扑（不得绕过——
    // 不得转 FixedFrame、不得经选链绕开所选主链上的不支持关节；V-10/M-6）。
    // 分类 InputInvalid＝diagnostics §4.3 词表"输入非法"族：源文件的关节
    // 类型在本软件 R1 表达范围之外（导入映射的 MDL-12 阻断语义）；级别
    // error（§9.5"导入/error"）。
    diagnostics::CodeDescriptor unsupportedJoint;
    unsupportedJoint.code = std::string(kMdlImportUnsupportedJoint);
    unsupportedJoint.ownerUnit = "modeling";
    unsupportedJoint.category = diagnostics::DiagnosticCategory::InputInvalid;
    unsupportedJoint.severity = diagnostics::DiagnosticSeverity::Error;   // §9.5"导入/error"
    unsupportedJoint.titleKey = "diag.mdl-import-unsupported-joint.title";
    unsupportedJoint.detailKey = "diag.mdl-import-unsupported-joint.detail";
    unsupportedJoint.paramSchema = R"(["joint-name","source-type"])";
    //        （joint-name＝关节 localName；source-type＝URDF type 属性原文）
    unsupportedJoint.confirmable = false;          // §9.5 行：false（阻断无可确认放行分支——不得绕过）
    unsupportedJoint.requiresComparison = false;   // 非比较型
    unsupportedJoint.retryable = diagnostics::RetryKind::UserRetry;  // "移除或改拓扑"＝fix-input 族
    unsupportedJoint.userVisible = true;
    unsupportedJoint.reportable = true;
    unsupportedJoint.historical = true;
    unsupportedJoint.registryVersion = 1;
    unsupportedJoint.deprecated = false;
    // supersededBy 保持 nullopt。

    // ---- §9.5 T05 行：MDL-IMPORT-BRANCH-SELECTION（WP-13-T05 登记）----
    // 多可动分支文件→显式选择主链（分支报告可观察——对象与原因；辅助
    // 分支转场景/环境候选或忽略、不构成拒绝——§6.4 维度一）。分类
    // InputInvalid（输入不足以唯一定义模型——需用户补全决策；词表无
    // "待决策"独立类）；级别 info（§9.5"导入/info"——非错误、正常交互面）。
    diagnostics::CodeDescriptor branchSelection;
    branchSelection.code = std::string(kMdlImportBranchSelection);
    branchSelection.ownerUnit = "modeling";
    branchSelection.category = diagnostics::DiagnosticCategory::InputInvalid;
    branchSelection.severity = diagnostics::DiagnosticSeverity::Info;   // §9.5"导入/info"
    branchSelection.titleKey = "diag.mdl-import-branch-selection.title";
    branchSelection.detailKey = "diag.mdl-import-branch-selection.detail";
    branchSelection.paramSchema = R"(["branch-count","branch-roots"])";
    //        （branch-count＝候选分支数；branch-roots＝分支根连杆名逗号清单）
    branchSelection.confirmable = false;          // §9.5 行：false（选链经向导交互，非确认放行）
    branchSelection.requiresComparison = false;
    branchSelection.retryable = diagnostics::RetryKind::UserRetry;  // "显式选择主链"＝fix-input 族
    branchSelection.userVisible = true;
    branchSelection.reportable = true;
    branchSelection.historical = true;
    branchSelection.registryVersion = 1;
    branchSelection.deprecated = false;
    // supersededBy 保持 nullopt。

    // ---- §9.5 T05 行：MDL-IMPORT-ZERO-AXIS（WP-13-T05 登记）----
    // 零轴/非有限轴关节→修正源文件（该关节标记 Invalid，含该类关节的
    // 草稿不得提交修订——MDL-11 后半；仅报告不静默修正，NFR-COR-03）。
    // 分类 InputInvalid（源文件值非法）；级别 error。
    diagnostics::CodeDescriptor zeroAxis;
    zeroAxis.code = std::string(kMdlImportZeroAxis);
    zeroAxis.ownerUnit = "modeling";
    zeroAxis.category = diagnostics::DiagnosticCategory::InputInvalid;
    zeroAxis.severity = diagnostics::DiagnosticSeverity::Error;   // §9.5"导入/error"
    zeroAxis.titleKey = "diag.mdl-import-zero-axis.title";
    zeroAxis.detailKey = "diag.mdl-import-zero-axis.detail";
    zeroAxis.paramSchema = R"(["joint-name","axis-raw"])";
    //        （joint-name＝关节 localName；axis-raw＝轴属性原文——非法值
    //          保留原文不静默改写，MDL-06）
    zeroAxis.confirmable = false;          // §9.5 行：false（不可确认放行——必须修源）
    zeroAxis.requiresComparison = false;
    zeroAxis.retryable = diagnostics::RetryKind::UserRetry;  // "修正源文件"＝fix-input 族
    zeroAxis.userVisible = true;
    zeroAxis.reportable = true;
    zeroAxis.historical = true;
    zeroAxis.registryVersion = 1;
    zeroAxis.deprecated = false;
    // supersededBy 保持 nullopt。

    // ---- §9.5 T05/T06 行：MDL-IMPORT-PENDING-CONFIRM（WP-13-T05 登记）----
    // 待确认项未决（缺 axis 默认 +X/缺限位/工作范围）→逐条确认。缺省值
    // 已按卡面语义补全（可观察——不静默），但正式应用前须用户逐条确认
    // （待确认草稿项决议——§6.4）。分类 InputInvalid（输入不完整——词表
    // 无独立"待确认"类：Confirmable 保留给策略 SA-15 确认放行族，本码
    // 非策略语义）；级别 Warning（§9.5"导入/Warning"——不阻断草稿保存）。
    diagnostics::CodeDescriptor pendingConfirm;
    pendingConfirm.code = std::string(kMdlImportPendingConfirm);
    pendingConfirm.ownerUnit = "modeling";
    pendingConfirm.category = diagnostics::DiagnosticCategory::InputInvalid;
    pendingConfirm.severity = diagnostics::DiagnosticSeverity::Warning;   // §9.5"导入/Warning"
    pendingConfirm.titleKey = "diag.mdl-import-pending-confirm.title";
    pendingConfirm.detailKey = "diag.mdl-import-pending-confirm.detail";
    pendingConfirm.paramSchema = R"(["item-kind","subject"])";
    //        （item-kind＝待确认类别 axis-default-plus-x|limit-missing|
    //          working-range-unconfirmed；subject＝关节/条目定位）
    pendingConfirm.confirmable = false;          // §9.5 行：false（逐条确认经待确认草稿项决议流，非本诊断的确认放行）
    pendingConfirm.requiresComparison = false;
    pendingConfirm.retryable = diagnostics::RetryKind::UserRetry;  // "逐条确认"＝fix-input 族
    pendingConfirm.userVisible = true;
    pendingConfirm.reportable = true;
    pendingConfirm.historical = true;
    pendingConfirm.registryVersion = 1;
    pendingConfirm.deprecated = false;
    // supersededBy 保持 nullopt。

    // ---- §9.5 T05 行：MDL-IMPORT-TEMPLATE-RANGE（WP-13-T05 实现期增登，
    // §14.6 v0.6 登记——卡 §6.4 能力矩阵第二行"诊断'超出首版产品模板
    // 范围'"的码面落位）----
    // 所选主链 4/5 轴或含 prismatic→导入识别与草稿兼容编辑通过，模板创建
    // 入口与正式计算/报告阻断（类型保留不降级——V12-01；R1；MDL-12-S1
    // 启用后仅六/七轴含 prismatic 放开）。分类 InfeasibilityProof＝
    // diagnostics §4.3 词表"有效工程结论而非错误"族：能力边界结论（链可
    // 识别可编辑，仅模板/正式计算面阻断）；级别 info（§9.5 增登行
    // "导入/info"）。
    diagnostics::CodeDescriptor templateRange;
    templateRange.code = std::string(kMdlImportTemplateRange);
    templateRange.ownerUnit = "modeling";
    templateRange.category = diagnostics::DiagnosticCategory::InfeasibilityProof;
    templateRange.severity = diagnostics::DiagnosticSeverity::Info;   // 增登行"导入/info"
    templateRange.titleKey = "diag.mdl-import-template-range.title";
    templateRange.detailKey = "diag.mdl-import-template-range.detail";
    templateRange.paramSchema = R"(["movable-axes","prismatic-present"])";
    //        （movable-axes＝主链可动关节数；prismatic-present＝true/false）
    templateRange.confirmable = false;          // 结论呈现（无可确认放行语义——阻断面在模板/计算入口）
    templateRange.requiresComparison = false;
    templateRange.retryable = diagnostics::RetryKind::Never;  // 结论呈现类（草稿可编辑——无"重试"动作）
    templateRange.userVisible = true;
    templateRange.reportable = true;
    templateRange.historical = true;
    templateRange.registryVersion = 1;
    templateRange.deprecated = false;
    // supersededBy 保持 nullopt。

    return {d, unsupportedJoint, branchSelection, zeroAxis, pendingConfirm,
            templateRange};
}

void registerModelingCodes(diagnostics::IDiagnosticRegistry& registry)
{
    // 逐条转发注册：注册期验证（句法/前缀-所有权/键唯一/paramSchema）
    // 全部在注册表内执行；任何一条失败即抛 DiagnosticsError——装配期
    // fail-fast，不静默跳过（NFR-MNT-03 边界拒绝；io registerIoCodeTable
    // 同款语义）。
    for (const auto& descriptor : modelingCodeDescriptors()) {
        registry.registerCode(descriptor);
    }
}

}  // namespace sdurws::ird::modeling
