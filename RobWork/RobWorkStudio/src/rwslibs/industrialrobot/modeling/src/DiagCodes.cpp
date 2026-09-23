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
 * 背景说明：清单当前 17 项（§9.5 任务列含 T02 的行＋WP-13-T05 登记的
 * T05 行五码＋WP-13-T06 实现期增登的 T06 行一码＋WP-13-T07 实现期增登
 * 的 T07 行一码＋WP-13-T08 登记的 T08 行九码——八条卡面行＋一条 v0.9
 * 实现期增登行 MDL-READINESS-PHYSICS-MISSING）——分批注册纪律（"不预建
 * 无消费者条目"）的执行口径见
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

    // ---- §9.5 T06 行：MDL-IMPORT-XACRO-UNRESOLVED（WP-13-T06 实现期
    // 增登，§14.6 v0.7 登记——卡 §6.5"展开失败/依赖缺失→可定位诊断
    // （宏名/行列）"的语义面码面落位；io 护栏码（IO-FORMAT-XML-CYCLE/
    // IO-RES-MISSING/IO-SEC-BUDGET-*）透传覆盖循环/缺失/预算三族，未定义
    // 宏/参数等语义失败按 P-MDL-4"护栏 io／语义 modeling"口径归 modeling
    // 码面）----
    // Xacro 受控展开失败：未定义宏/未定义参数/缺参/签名外属性/不支持
    // 构造/宏重定义，逐条携带宏名或参数名＋源行列（MDL-19/AT-31）。分类
    // InputInvalid＝diagnostics §4.3 词表"输入非法"族：源文件的展开语义
    // 在受控子集之外；级别 error（§9.5 增登行"导入/error"——展开失败无
    // 草稿产出，阻断面）。
    diagnostics::CodeDescriptor xacroUnresolved;
    xacroUnresolved.code = std::string(kMdlImportXacroUnresolved);
    xacroUnresolved.ownerUnit = "modeling";
    xacroUnresolved.category = diagnostics::DiagnosticCategory::InputInvalid;
    xacroUnresolved.severity = diagnostics::DiagnosticSeverity::Error;   // 增登行"导入/error"
    xacroUnresolved.titleKey = "diag.mdl-import-xacro-unresolved.title";
    xacroUnresolved.detailKey = "diag.mdl-import-xacro-unresolved.detail";
    xacroUnresolved.paramSchema = R"(["item-kind","symbol"])";
    //        （item-kind＝undefined-macro|undefined-param|missing-param|
    //          unknown-attr|unsupported-construct|unsupported-expression|
    //          missing-attribute|macro-redefinition；symbol＝宏名/参数名/
    //          构造名——行列由诊断 context 定位段承载，不入 paramSchema）
    xacroUnresolved.confirmable = false;          // §9.5 行：false（修源文件——无放行分支）
    xacroUnresolved.requiresComparison = false;
    xacroUnresolved.retryable = diagnostics::RetryKind::UserRetry;  // "改写源文件"＝fix-input 族
    xacroUnresolved.userVisible = true;
    xacroUnresolved.reportable = true;
    xacroUnresolved.historical = true;
    xacroUnresolved.registryVersion = 1;
    xacroUnresolved.deprecated = false;
    // supersededBy 保持 nullopt。

    // ---- §9.5 T07 行：MDL-TEMPLATE-DISABLED（WP-13-T07 实现期增登，
    // §14.6 v0.8 登记——卡 §9.4.2"@错误 TemplateDisabled|IllegalName（附
    // 定位诊断）"的码面落位；表行序追加于表尾——登记簿纪律不重排既有行）----
    // 模板登记未启用：P-03 七轴模板工程数值未冻结（DTB §4.3 O-27 处置
    // 口径"仅登记不启用；模板启用前冻结"），listTemplates 返回
    // enabled=false、createDraft 拒绝并附本码提示诊断（不静默替换为六轴
    // ——V-02/AT-20 向导语义建模侧）。分类 InfeasibilityProof＝diagnostics
    // §4.3 词表"有效工程结论而非错误"族：模板存在且已登记，仅冻结门未过
    // （工程结论，非输入错误）；级别 info（§9.5 增登行"模板/info"——提示
    // 而非错误）。
    diagnostics::CodeDescriptor templateDisabled;
    templateDisabled.code = std::string(kMdlTemplateDisabled);
    templateDisabled.ownerUnit = "modeling";
    templateDisabled.category = diagnostics::DiagnosticCategory::InfeasibilityProof;
    templateDisabled.severity = diagnostics::DiagnosticSeverity::Info;   // 增登行"模板/info"
    templateDisabled.titleKey = "diag.mdl-template-disabled.title";
    templateDisabled.detailKey = "diag.mdl-template-disabled.detail";
    templateDisabled.paramSchema = R"(["template-id","freeze-gate"])";
    //        （template-id＝模板登记 id（如 generic-7r）；freeze-gate＝冻结
    //          前置编号（P-03）——启用条件可定位到 DTB §4.3 待冻结前置行）
    templateDisabled.confirmable = false;          // §9.5 行：false（数值冻结非会话内可放行——无确认分支）
    templateDisabled.requiresComparison = false;   // 非比较型
    templateDisabled.retryable = diagnostics::RetryKind::UserRetry;  // "选择已启用模板"＝fix-input 族
    templateDisabled.userVisible = true;           // 向导创建入口提示（AT-20 建模侧）
    templateDisabled.reportable = true;
    templateDisabled.historical = true;
    templateDisabled.registryVersion = 1;          // 首次登记
    templateDisabled.deprecated = false;
    // supersededBy 保持 nullopt。

    // ---- §9.5 T08 行八码（WP-13-T08 登记，§14.6 v0.9——断言分域＋就绪
    // 校验＋行程确认族；表行序追加于表尾——登记簿纪律不重排既有行）----
    // T08 行码共用口径：category/severity 逐码取 §9.5"类别/级别"列的
    // diagnostics §4.3 词表投影；paramSchema 统一为"[]"（无参数）——T08
    // 产码点（AssertionSuite/就绪校验器）的记录把定位放 subject＋localName、
    // 三要素放 comparison、人读文案放 context/cause/recommendedAction，
    // 不经 DiagContext params 通道，故按实际数据面登记空参数表。

    // ---- MDL-06-TRAVEL-LIMIT（比较型/Warning，confirmable=true）----
    // 有限限位旋转关节行程超策略阈值（T＞L 才超限——T＝L 不超限，边界
    // 含于合规侧；阈值唯一来源 policy JointThresholds.finiteRotationTravelLimit
    // 默认 4π——本地无第二常量，ARC-05/NFR-MNT-07）→确认放行或改行程
    // （MDL-06④/SA-15/M-10）。分类 Confirmable＝diagnostics §4.3 词表
    // "策略校验超限待用户显式确认"族（枚举注释原文即引 MDL-06④——逐字
    // 对应本码语义）。
    diagnostics::CodeDescriptor travelLimit;
    travelLimit.code = std::string(kMdl06TravelLimit);
    travelLimit.ownerUnit = "modeling";
    travelLimit.category = diagnostics::DiagnosticCategory::Confirmable;
    travelLimit.severity = diagnostics::DiagnosticSeverity::Warning;  // §9.5"比较型/Warning"
    travelLimit.titleKey = "diag.mdl-06-travel-limit.title";
    travelLimit.detailKey = "diag.mdl-06-travel-limit.detail";
    travelLimit.paramSchema = "[]";
    travelLimit.confirmable = true;               // §9.5 行：true（SA-15 确认放行流唯一入口）
    travelLimit.requiresComparison = true;        // 可确认必为比较型（注册期校验强化）
    travelLimit.retryable = diagnostics::RetryKind::UserRetry;  // "确认放行或改行程"＝confirm-or-fix 族
    travelLimit.userVisible = true;
    travelLimit.reportable = true;
    travelLimit.historical = true;                // 确认凭据随修订留痕（PM-12-S1 可浏览）
    travelLimit.registryVersion = 1;
    travelLimit.deprecated = false;
    // supersededBy 保持 nullopt。

    // ---- MDL-ASSERT-MASS-NONPOSITIVE（断言/error）----
    // 已提供质量 m≤0→修正质量或清空为缺失（断言①；缺失 NotProvided 不
    // 触发——走 DataInsufficient 降级，V15-01）。分类 InputInvalid＝
    // diagnostics §4.3 词表"输入非法"族（枚举注释原文即引"REQ-06/MDL-06
    // 硬断言"）。
    diagnostics::CodeDescriptor massNonpositive;
    massNonpositive.code = std::string(kMdlAssertMassNonpositive);
    massNonpositive.ownerUnit = "modeling";
    massNonpositive.category = diagnostics::DiagnosticCategory::InputInvalid;
    massNonpositive.severity = diagnostics::DiagnosticSeverity::Error;   // §9.5"断言/error"
    massNonpositive.titleKey = "diag.mdl-assert-mass-nonpositive.title";
    massNonpositive.detailKey = "diag.mdl-assert-mass-nonpositive.detail";
    massNonpositive.paramSchema = "[]";
    massNonpositive.confirmable = false;          // 硬断言无放行分支（就地阻止）
    massNonpositive.requiresComparison = true;    // actual=m、expected=0（kg）
    massNonpositive.retryable = diagnostics::RetryKind::UserRetry;  // "修正质量或清空"＝fix-input 族
    massNonpositive.userVisible = true;
    massNonpositive.reportable = true;
    massNonpositive.historical = true;
    massNonpositive.registryVersion = 1;
    massNonpositive.deprecated = false;
    // supersededBy 保持 nullopt。

    // ---- MDL-ASSERT-INERTIA-NOT-SPD（断言/error）----
    // 惯量非对称正定（对称化后最小特征值≤0）→修正张量（断言②；六分量
    // 表示结构性对称——对称违例仅可能来自估算合成，其输出自检先行拒收，
    // 故本码拦 SPD 半段）。分类 InputInvalid（同上——MDL-06 硬断言族）。
    diagnostics::CodeDescriptor inertiaNotSpd;
    inertiaNotSpd.code = std::string(kMdlAssertInertiaNotSpd);
    inertiaNotSpd.ownerUnit = "modeling";
    inertiaNotSpd.category = diagnostics::DiagnosticCategory::InputInvalid;
    inertiaNotSpd.severity = diagnostics::DiagnosticSeverity::Error;   // §9.5"断言/error"
    inertiaNotSpd.titleKey = "diag.mdl-assert-inertia-not-spd.title";
    inertiaNotSpd.detailKey = "diag.mdl-assert-inertia-not-spd.detail";
    inertiaNotSpd.paramSchema = "[]";
    inertiaNotSpd.confirmable = false;
    inertiaNotSpd.requiresComparison = true;      // actual=λmin、expected=0（kg·m²）
    inertiaNotSpd.retryable = diagnostics::RetryKind::UserRetry;  // "修正张量"＝fix-input 族
    inertiaNotSpd.userVisible = true;
    inertiaNotSpd.reportable = true;
    inertiaNotSpd.historical = true;
    inertiaNotSpd.registryVersion = 1;
    inertiaNotSpd.deprecated = false;
    // supersededBy 保持 nullopt。

    // ---- MDL-ASSERT-INERTIA-TRIANGLE（断言/error）----
    // 惯性椭球三角不等式不满足（λmax＞λmid＋λmin，严格比较——解析特征值
    // 无浮点放宽）→修正张量（断言③）。分类 InputInvalid（同上）。
    diagnostics::CodeDescriptor inertiaTriangle;
    inertiaTriangle.code = std::string(kMdlAssertInertiaTriangle);
    inertiaTriangle.ownerUnit = "modeling";
    inertiaTriangle.category = diagnostics::DiagnosticCategory::InputInvalid;
    inertiaTriangle.severity = diagnostics::DiagnosticSeverity::Error;   // §9.5"断言/error"
    inertiaTriangle.titleKey = "diag.mdl-assert-inertia-triangle.title";
    inertiaTriangle.detailKey = "diag.mdl-assert-inertia-triangle.detail";
    inertiaTriangle.paramSchema = "[]";
    inertiaTriangle.confirmable = false;
    inertiaTriangle.requiresComparison = true;    // actual=λmax、expected=λmid＋λmin（kg·m²）
    inertiaTriangle.retryable = diagnostics::RetryKind::UserRetry;  // "修正张量"＝fix-input 族
    inertiaTriangle.userVisible = true;
    inertiaTriangle.reportable = true;
    inertiaTriangle.historical = true;
    inertiaTriangle.registryVersion = 1;
    inertiaTriangle.deprecated = false;
    // supersededBy 保持 nullopt。

    // ---- MDL-ASSERT-LIMIT-INTERVAL（断言/error）----
    // qmin≥qmax（有限限位可动关节；单位随类型 rad/m——comparison 逐记录
    // 携带）→修正限位（断言④前半）。分类 InputInvalid（同上）。
    diagnostics::CodeDescriptor limitInterval;
    limitInterval.code = std::string(kMdlAssertLimitInterval);
    limitInterval.ownerUnit = "modeling";
    limitInterval.category = diagnostics::DiagnosticCategory::InputInvalid;
    limitInterval.severity = diagnostics::DiagnosticSeverity::Error;   // §9.5"断言/error"
    limitInterval.titleKey = "diag.mdl-assert-limit-interval.title";
    limitInterval.detailKey = "diag.mdl-assert-limit-interval.detail";
    limitInterval.paramSchema = "[]";
    limitInterval.confirmable = false;
    limitInterval.requiresComparison = true;      // actual=qmin、expected=qmax（rad/m）
    limitInterval.retryable = diagnostics::RetryKind::UserRetry;  // "修正限位"＝fix-input 族
    limitInterval.userVisible = true;
    limitInterval.reportable = true;
    limitInterval.historical = true;
    limitInterval.registryVersion = 1;
    limitInterval.deprecated = false;
    // supersededBy 保持 nullopt。

    // ---- MDL-ASSERT-RANGE-NOT-FINITE（断言/error）----
    // continuous 工程工作范围未确认（NotProvided）或非有限区间（端点非
    // 有限/min≥max）→确认范围（断言④后半＋MDL-12；已确认有限范围则
    // 豁免本断言与限位断言——continuous 无 bounds）。requiresComparison=
    // false：未确认分支无值可比（"不伪造数值"——ERR-01 不适用显式承载），
    // 已提供非法分支的 comparison 由记录自愿携带。分类 InputInvalid（同上）。
    diagnostics::CodeDescriptor rangeNotFinite;
    rangeNotFinite.code = std::string(kMdlAssertRangeNotFinite);
    rangeNotFinite.ownerUnit = "modeling";
    rangeNotFinite.category = diagnostics::DiagnosticCategory::InputInvalid;
    rangeNotFinite.severity = diagnostics::DiagnosticSeverity::Error;   // §9.5"断言/error"
    rangeNotFinite.titleKey = "diag.mdl-assert-range-not-finite.title";
    rangeNotFinite.detailKey = "diag.mdl-assert-range-not-finite.detail";
    rangeNotFinite.paramSchema = "[]";
    rangeNotFinite.confirmable = false;           // 未确认范围须确认（编辑面），非本诊断放行
    rangeNotFinite.requiresComparison = false;    // 未确认分支无值可比（见上）
    rangeNotFinite.retryable = diagnostics::RetryKind::UserRetry;  // "确认范围"＝fix-input 族
    rangeNotFinite.userVisible = true;
    rangeNotFinite.reportable = true;
    rangeNotFinite.historical = true;
    rangeNotFinite.registryVersion = 1;
    rangeNotFinite.deprecated = false;
    // supersededBy 保持 nullopt。

    // ---- MDL-READINESS-REF-MISSING（校验/error）----
    // 引用对象不在闭包/token 不匹配（I-MDL-9 闭包半段——值模型层无闭包
    // 上下文，§4.10 范围注记归本码承载）→修复引用。分类 ResourceMissing
    // ＝diagnostics §4.3 词表"外部源 Missing"族的闭包内引用面。
    diagnostics::CodeDescriptor refMissing;
    refMissing.code = std::string(kMdlReadinessRefMissing);
    refMissing.ownerUnit = "modeling";
    refMissing.category = diagnostics::DiagnosticCategory::ResourceMissing;
    refMissing.severity = diagnostics::DiagnosticSeverity::Error;   // §9.5"校验/error"
    refMissing.titleKey = "diag.mdl-readiness-ref-missing.title";
    refMissing.detailKey = "diag.mdl-readiness-ref-missing.detail";
    refMissing.paramSchema = "[]";
    refMissing.confirmable = false;
    refMissing.requiresComparison = false;
    refMissing.retryable = diagnostics::RetryKind::UserRetry;  // "修复引用"＝fix-input 族
    refMissing.userVisible = true;
    refMissing.reportable = true;
    refMissing.historical = true;
    refMissing.registryVersion = 1;
    refMissing.deprecated = false;
    // supersededBy 保持 nullopt。

    // ---- MDL-READINESS-RESOURCE-STATE（校验/Warning）----
    // Recorded 资源缺失/变化或未固化→重关联/固化（CON-03 固化激励；缺失/
    // 变化探测归 io 护栏与 runtime 编译复核——本码在就绪层承载状态面事
    // 实，不阻断应用）。分类 ResourceMissing（同上族）；级别 Warning。
    diagnostics::CodeDescriptor resourceState;
    resourceState.code = std::string(kMdlReadinessResourceState);
    resourceState.ownerUnit = "modeling";
    resourceState.category = diagnostics::DiagnosticCategory::ResourceMissing;
    resourceState.severity = diagnostics::DiagnosticSeverity::Warning;  // §9.5"校验/Warning"
    resourceState.titleKey = "diag.mdl-readiness-resource-state.title";
    resourceState.detailKey = "diag.mdl-readiness-resource-state.detail";
    resourceState.paramSchema = "[]";
    resourceState.confirmable = false;
    resourceState.requiresComparison = false;
    resourceState.retryable = diagnostics::RetryKind::UserRetry;  // "重关联/固化"＝fix-input 族
    resourceState.userVisible = true;
    resourceState.reportable = true;
    resourceState.historical = true;
    resourceState.registryVersion = 1;
    resourceState.deprecated = false;
    // supersededBy 保持 nullopt。

    // ---- MDL-READINESS-PHYSICS-MISSING（校验/Warning，v0.9 实现期增登）----
    // 物性缺失（质量/惯量 NotProvided）→DataInsufficient 降级预告（V15-01
    // ——缺失不触发硬断言，不伪造数值）；建议"补全物性或接受降级"。
    // §9.3"物性缺失→不阻断，转 Warning 诊断随计划留痕"与 §8.2 L5 Warning
    // 行的码面落位——原表缺行，沿实现期增登先例登记（§14.6 v0.9）。分类
    // DataInsufficient＝diagnostics §4.3 词表"engineeringStatus=
    // DataInsufficient 轴"族的就绪层预告面。
    diagnostics::CodeDescriptor physicsMissing;
    physicsMissing.code = std::string(kMdlReadinessPhysicsMissing);
    physicsMissing.ownerUnit = "modeling";
    physicsMissing.category = diagnostics::DiagnosticCategory::DataInsufficient;
    physicsMissing.severity = diagnostics::DiagnosticSeverity::Warning;  // §8.2 L5"Warning（缺失）"
    physicsMissing.titleKey = "diag.mdl-readiness-physics-missing.title";
    physicsMissing.detailKey = "diag.mdl-readiness-physics-missing.detail";
    physicsMissing.paramSchema = "[]";
    physicsMissing.confirmable = false;
    physicsMissing.requiresComparison = false;    // 缺失无值可比（不伪造数值——ERR-01）
    physicsMissing.retryable = diagnostics::RetryKind::UserRetry;  // "补全物性或接受降级"＝fix-input 族
    physicsMissing.userVisible = true;
    physicsMissing.reportable = true;
    physicsMissing.historical = true;
    physicsMissing.registryVersion = 1;
    physicsMissing.deprecated = false;
    // supersededBy 保持 nullopt。

    return {d, unsupportedJoint, branchSelection, zeroAxis, pendingConfirm,
            templateRange, xacroUnresolved, templateDisabled,
            travelLimit, massNonpositive, inertiaNotSpd, inertiaTriangle,
            limitInterval, rangeNotFinite, refMissing, resourceState,
            physicsMissing};
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
