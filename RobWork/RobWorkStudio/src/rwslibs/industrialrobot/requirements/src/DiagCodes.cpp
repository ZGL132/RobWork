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
 * 背景说明：清单当前 13 项（§9.6 任务列 T02/T03 行 1 码＋T04 行 4 码
 * ＋T05 行 6 码＋表尾增登 PLAN-MISSING 一码——WP-14-T05 就绪族落位；
 * T06 行 CAPTURE-STATE-STALE 一码——WP-14-T06 捕获族落位；分批注册
 * 纪律（"不预建无消费者条目"）的执行口径见头文件 DiagCodes.hpp
 * 文件头注；其余 2 行（T07 派生族）随各自任务在**本清单表尾追加**
 * （表尾追加＝登记簿纪律，不重排既有项）。
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

    // =================================================================
    // 以下 §9.6 T04 行 4 码（导入族——WP-14-T04 落位时表尾追加；消费者
    // ＝Import.cpp 的 mapCsv/mapJson 产码面）。共同登记口径：
    //   - 分类＝InputInvalid（diagnostics §4.3 词表"输入非法（REQ-06/
    //     MDL-06 硬断言）"族——导入源数据非法正是该族定义的实例，与
    //     schema 超版的 FormatOrVersion 族相区分）；
    //   - ownerUnit="requirements"、registryVersion=1、deprecated=false、
    //     supersededBy=nullopt（同 T02 行口径，不赘述）；
    //   - 文案键＝P-DIAG-9 命名约定 diag.<code-lower>.title/.detail。
    // =================================================================

    // ---- §9.6 T04 行：REQ-IMPORT-ROW-ERROR（error）----
    // CSV/JSON 行级错误（数值非法/level 非法/enabled 非法/词表外枚举等
    // ——§7.3 行级错误清单的兜底码）；定位三要素（行号/列名/原文）经
    // DiagnosticRecord.context 承载（Import.cpp makeRowError 单点拼装）。
    // row=0 哨兵＝文档级（非单行）错误（JSON 整档解析失败——io 通道无
    // 部分成功，§5.9.4 IO-D10）。
    diagnostics::CodeDescriptor rowErr;
    rowErr.code = std::string(kReqImportRowError);
    rowErr.ownerUnit = "requirements";
    rowErr.category = diagnostics::DiagnosticCategory::InputInvalid;
    rowErr.severity = diagnostics::DiagnosticSeverity::Error;  // §9.6"级别"列：error
    rowErr.titleKey = "diag.req-import-row-error.title";
    rowErr.detailKey = "diag.req-import-row-error.detail";
    // paramSchema 四键：行号（0＝文档级哨兵）/列名/原文/原因——AT-02
    // "错误定位到列与原文"的登记面（受限 JSON 形命名参数清单）。
    rowErr.paramSchema = R"(["row","column","raw","reason"])";
    rowErr.confirmable = false;          // 数据错误无"知情放行"分支（修正后重导）
    rowErr.requiresComparison = false;   // 非比较型三要素码
    rowErr.retryable = diagnostics::RetryKind::UserRetry;  // 修正输入后重试
    rowErr.userVisible = true;
    rowErr.reportable = true;
    rowErr.historical = true;

    // ---- §9.6 T04 行：REQ-IMPORT-DUPLICATE-ID（error）----
    // 导入重复 id/name（§7.3"重复 id/重复 name→逐行错误"分支；同一码
    // 承载两去重键——§9.6 行语义"导入重复 id/name"原文口径）。
    diagnostics::CodeDescriptor dupId;
    dupId.code = std::string(kReqImportDuplicateId);
    dupId.ownerUnit = "requirements";
    dupId.category = diagnostics::DiagnosticCategory::InputInvalid;
    dupId.severity = diagnostics::DiagnosticSeverity::Error;
    dupId.titleKey = "diag.req-import-duplicate-id.title";
    dupId.detailKey = "diag.req-import-duplicate-id.detail";
    // paramSchema 四键：所在行/重复键的列（id 或 name）/重复值原文/首次
    // 出现行——去重修正的完整定位面。
    dupId.paramSchema = R"(["row","column","value","first-row"])";
    dupId.confirmable = false;
    dupId.requiresComparison = false;
    dupId.retryable = diagnostics::RetryKind::UserRetry;
    dupId.userVisible = true;
    dupId.reportable = true;
    dupId.historical = true;

    // ---- §9.6 T04 行：REQ-IMPORT-UNIT-ILLEGAL（error）----
    // 单位声明无法换算（token 词表外/量纲与列不符——core UnitToken 注册
    // 表是唯一换算权威，NFR-COR-03 不静默转 0）/必填列缺失（结构级拒绝
    // ——该文件不可导入，§7.3"必填列缺失"行；§9.6 行语义"单位声明无法
    // 换算/列缺失"两分支同码承载）。
    diagnostics::CodeDescriptor unitIll;
    unitIll.code = std::string(kReqImportUnitIllegal);
    unitIll.ownerUnit = "requirements";
    unitIll.category = diagnostics::DiagnosticCategory::InputInvalid;
    unitIll.severity = diagnostics::DiagnosticSeverity::Error;
    unitIll.titleKey = "diag.req-import-unit-illegal.title";
    unitIll.detailKey = "diag.req-import-unit-illegal.detail";
    // paramSchema 三键：列名/声明单位 token（结构级缺列分支为 "none"）/
    // 原因（unregistered|dimension-mismatch|missing-required-column）。
    unitIll.paramSchema = R"(["column","unit","reason"])";
    unitIll.confirmable = false;
    unitIll.requiresComparison = false;
    unitIll.retryable = diagnostics::RetryKind::UserRetry;
    unitIll.userVisible = true;
    unitIll.reportable = true;
    unitIll.historical = true;

    // ---- §9.6 T04 行：REQ-IMPORT-FRAME-UNKNOWN（warning）----
    // Frame 引用浅悬空（ref_frame 值指向导入方无法核验的模型坐标系/场景
    // 对象——本单元拿不到修订闭包，§8.1 浅校验边界；条目**保留待解析**，
    // 跨闭包半区核对归 T05 就绪层 R2。级别 warning＝§9.6 表原文，不阻断
    // 导入）。
    diagnostics::CodeDescriptor frameUnk;
    frameUnk.code = std::string(kReqImportFrameUnknown);
    frameUnk.ownerUnit = "requirements";
    frameUnk.category = diagnostics::DiagnosticCategory::InputInvalid;
    frameUnk.severity = diagnostics::DiagnosticSeverity::Warning;  // §9.6"级别"列：warning
    frameUnk.titleKey = "diag.req-import-frame-unknown.title";
    frameUnk.detailKey = "diag.req-import-frame-unknown.detail";
    // paramSchema 三键：行号/列名/引用原文（待解析目标——闭包核对阶段的
    // 回查键）。
    frameUnk.paramSchema = R"(["row","column","raw"])";
    frameUnk.confirmable = false;
    frameUnk.requiresComparison = false;
    frameUnk.retryable = diagnostics::RetryKind::UserRetry;
    frameUnk.userVisible = true;
    frameUnk.reportable = true;
    frameUnk.historical = true;

    // =================================================================
    // 以下 §9.6 T05 行 6 码＋表尾增登 1 码（就绪校验族——WP-14-T05 落位
    // 时表尾追加；消费者＝Readiness.cpp 分层产码面＋CommandHandlers.cpp
    // 候选态重估拒绝面）。共同登记口径：
    //   - error 级分类＝InputInvalid（diagnostics §4.3 词表"输入非法"
    //     族——需求输入不满足就绪断言正是该族定义的实例；warning 级
    //     同分类但级别 Warning——预告登记面，不阻断应用）；
    //   - ownerUnit="requirements"、registryVersion=1、deprecated=false、
    //     supersededBy=nullopt（同 T02/T04 行口径，不赘述）；
    //   - 文案键＝P-DIAG-9 命名约定 diag.<code-lower>.title/.detail。
    // =================================================================

    // ---- §9.6 T05 行：REQ-READY-REF-MISSING（error）----
    // 引用悬空/token 不匹配（§8.1 R1/R8；R0 根引用表/R2 槽-种类/R4 绑定
    // 悬空按"引用悬空"语义就近承载同码，层标注区分——Readiness.hpp 文件
    // 头映射表）。定位三要素：subject=条目/引用目标 ObjectId、localName=
    // 条目名、context 携 field/target——逐项定位面。
    diagnostics::CodeDescriptor refMissing;
    refMissing.code = std::string(kReqReadyRefMissing);
    refMissing.ownerUnit = "requirements";
    refMissing.category = diagnostics::DiagnosticCategory::InputInvalid;
    refMissing.severity = diagnostics::DiagnosticSeverity::Error;  // §9.6"级别"列：error
    refMissing.titleKey = "diag.req-ready-ref-missing.title";
    refMissing.detailKey = "diag.req-ready-ref-missing.detail";
    // paramSchema 三键：引用字段名/引用目标对象（缺省 "-"=结构违约面）/
    // 期望对象类型 token——悬空/失配两类违例的统一定位面。
    refMissing.paramSchema = R"(["field","target","expected-token"])";
    refMissing.confirmable = false;         // 数据错误无"知情放行"分支
    refMissing.requiresComparison = false;  // 非比较型三要素码
    refMissing.retryable = diagnostics::RetryKind::UserRetry;  // 修正引用后重新校验
    refMissing.userVisible = true;
    refMissing.reportable = true;
    refMissing.historical = true;

    // ---- §9.6 T05 行：REQ-READY-SEQ-CYCLE（error）----
    // 任务顺序成环/重复键（§8.1 R7；悬空前驱名按层-码对齐原则以本码
    // 承载、cause 区分）。定位：localName=涉事条目名/顺序键，context 携
    // kind（duplicate/dangling/cycle）与 key。
    diagnostics::CodeDescriptor seqCycle;
    seqCycle.code = std::string(kReqReadySeqCycle);
    seqCycle.ownerUnit = "requirements";
    seqCycle.category = diagnostics::DiagnosticCategory::InputInvalid;
    seqCycle.severity = diagnostics::DiagnosticSeverity::Error;
    seqCycle.titleKey = "diag.req-ready-seq-cycle.title";
    seqCycle.detailKey = "diag.req-ready-seq-cycle.detail";
    // paramSchema 两键：违例种类（duplicate|dangling|cycle）/涉事顺序键。
    seqCycle.paramSchema = R"(["kind","key"])";
    seqCycle.confirmable = false;
    seqCycle.requiresComparison = false;
    seqCycle.retryable = diagnostics::RetryKind::UserRetry;
    seqCycle.userVisible = true;
    seqCycle.reportable = true;
    seqCycle.historical = true;

    // ---- §9.6 T05 行：REQ-READY-POSE-ILLEGAL（error）----
    // 位姿/容差/约束分量非法（§8.1 R3；I-REQ-5 值面的就绪层定位码——
    // 值面拒绝在构造边界，本码承载"已入工作集的非法位姿事实"的定位）。
    diagnostics::CodeDescriptor poseIllegal;
    poseIllegal.code = std::string(kReqReadyPoseIllegal);
    poseIllegal.ownerUnit = "requirements";
    poseIllegal.category = diagnostics::DiagnosticCategory::InputInvalid;
    poseIllegal.severity = diagnostics::DiagnosticSeverity::Error;
    poseIllegal.titleKey = "diag.req-ready-pose-illegal.title";
    poseIllegal.detailKey = "diag.req-ready-pose-illegal.detail";
    // paramSchema 单键：违例字段名（pose/tolerance/<segment>.distanceM——
    // Errors 值面 params "field" 的就绪侧同键对齐）。
    poseIllegal.paramSchema = R"(["field"])";
    poseIllegal.confirmable = false;
    poseIllegal.requiresComparison = false;
    poseIllegal.retryable = diagnostics::RetryKind::UserRetry;
    poseIllegal.userVisible = true;
    poseIllegal.reportable = true;
    poseIllegal.historical = true;

    // ---- §9.6 T05 行：REQ-READY-NO-REQUIRED-CASE（warning）----
    // 无启用必验工况（§8.1 R5——Warning 不阻断应用；正式拦截归 evidence
    // P-EV-7 ④级，本码为"数据不足预告"登记面）。
    diagnostics::CodeDescriptor noRequiredCase;
    noRequiredCase.code = std::string(kReqReadyNoRequiredCase);
    noRequiredCase.ownerUnit = "requirements";
    noRequiredCase.category = diagnostics::DiagnosticCategory::InputInvalid;
    noRequiredCase.severity = diagnostics::DiagnosticSeverity::Warning;  // §9.6"级别"列：warning
    noRequiredCase.titleKey = "diag.req-ready-no-required-case.title";
    noRequiredCase.detailKey = "diag.req-ready-no-required-case.detail";
    // paramSchema 两键：工况总数/必验计数（恒 0——清单形状自描述）。
    noRequiredCase.paramSchema = R"(["conditions","required"])";
    noRequiredCase.confirmable = false;
    noRequiredCase.requiresComparison = false;
    noRequiredCase.retryable = diagnostics::RetryKind::UserRetry;
    noRequiredCase.userVisible = true;
    noRequiredCase.reportable = true;
    noRequiredCase.historical = true;

    // ---- §9.6 T05 行：REQ-READY-PLAN-DEGENERATE（error）----
    // 区域退化/计划-区域失配（§8.1 R6 Blocking 分支——I-REQ-6 盒/覆盖率/
    // 采样参数与计划对应性违例）。
    diagnostics::CodeDescriptor planDegenerate;
    planDegenerate.code = std::string(kReqReadyPlanDegenerate);
    planDegenerate.ownerUnit = "requirements";
    planDegenerate.category = diagnostics::DiagnosticCategory::InputInvalid;
    planDegenerate.severity = diagnostics::DiagnosticSeverity::Error;
    planDegenerate.titleKey = "diag.req-ready-plan-degenerate.title";
    planDegenerate.detailKey = "diag.req-ready-plan-degenerate.detail";
    // paramSchema 两键：违例字段（box/coverageTargets/positionSampling/
    // orientationSampling/regionRef）/关联区域或计划目标（缺省 "-"）。
    planDegenerate.paramSchema = R"(["field","target"])";
    planDegenerate.confirmable = false;
    planDegenerate.requiresComparison = false;
    planDegenerate.retryable = diagnostics::RetryKind::UserRetry;
    planDegenerate.userVisible = true;
    planDegenerate.reportable = true;
    planDegenerate.historical = true;

    // ---- §9.6 T05 行：REQ-READY-INPUT-INCOMPLETE（error）----
    // 启用 Must 条目非法汇总（ReadinessSummary.valid=false 投影面）——
    // 命令 prepare 候选态重估拒绝时随逐项定位诊断附一条汇总诊断
    // （CommandHandlers.cpp 产码；ReadinessSummary 本体是纯数据投影，
    // 不携诊断——汇总码是其在人读面的承载）。
    diagnostics::CodeDescriptor inputIncomplete;
    inputIncomplete.code = std::string(kReqReadyInputIncomplete);
    inputIncomplete.ownerUnit = "requirements";
    inputIncomplete.category = diagnostics::DiagnosticCategory::InputInvalid;
    inputIncomplete.severity = diagnostics::DiagnosticSeverity::Error;
    inputIncomplete.titleKey = "diag.req-ready-input-incomplete.title";
    inputIncomplete.detailKey = "diag.req-ready-input-incomplete.detail";
    // paramSchema 两键：非法启用 Must 条目计数/清单（ObjectId 规范文本
    // 分号拼接——全量不抽样，NFR-MNT-04 口径）。
    inputIncomplete.paramSchema = R"(["invalid-count","invalid-items"])";
    inputIncomplete.confirmable = false;
    inputIncomplete.requiresComparison = false;
    inputIncomplete.retryable = diagnostics::RetryKind::UserRetry;
    inputIncomplete.userVisible = true;
    inputIncomplete.reportable = true;
    inputIncomplete.historical = true;

    // ---- §9.6 T05 行表尾增登：REQ-READY-PLAN-MISSING（warning）----
    // 区域已定义而计划集为空（§8.1 R6"Warning（零计划）"分支的原六码
    // 无 warning 级承载面——实现期增登先例，modeling WP-13-T10 同款；
    // 正式判定归评估/KIN-04 零样本→DataInsufficient，本码为预告登记面）。
    diagnostics::CodeDescriptor planMissing;
    planMissing.code = std::string(kReqReadyPlanMissing);
    planMissing.ownerUnit = "requirements";
    planMissing.category = diagnostics::DiagnosticCategory::InputInvalid;
    planMissing.severity = diagnostics::DiagnosticSeverity::Warning;  // 预告级（增登依据见头注）
    planMissing.titleKey = "diag.req-ready-plan-missing.title";
    planMissing.detailKey = "diag.req-ready-plan-missing.detail";
    // paramSchema 两键：区域计数/计划计数（0——预告的形状自描述）。
    planMissing.paramSchema = R"(["regions","plans"])";
    planMissing.confirmable = false;
    planMissing.requiresComparison = false;
    planMissing.retryable = diagnostics::RetryKind::UserRetry;
    planMissing.userVisible = true;
    planMissing.reportable = true;
    planMissing.historical = true;

    // =================================================================
    // 以下 §9.6 T06 行 1 码（捕获族——WP-14-T06 落位时表尾追加；消费者
    // ＝Capture.cpp STALE 对账步）。登记口径同 T05 行 warning 级：
    // 分类 ResourceMissing（diagnostics §4.3 词表"外部源 Missing/Changed/
    // 资源越界"族——会话基线相对草稿基线已 Changed 正是该族实例）；
    // ownerUnit="requirements"、registryVersion=1、deprecated=false、
    // supersededBy=nullopt；文案键＝P-DIAG-9 命名约定。
    // =================================================================

    // ---- §9.6 T06 行：REQ-CAPTURE-STATE-STALE（warning）----
    // TCP 捕获时会话状态与当前快照不一致（REQ-10 确认流——卡 §9.8
    // L-R7 行"会话过期→REQ-CAPTURE-STATE-STALE"；§14.2 R-REQ-5 缓解
    // 登记：确认流＋本码＋捕获值恒带来源方法标记）。warning 级＝登记
    // 不阻断（确认门已过＝知情写回；重捕可消除——UserRetry）。
    diagnostics::CodeDescriptor captureStale;
    captureStale.code = std::string(kReqCaptureStateStale);
    captureStale.ownerUnit = "requirements";
    captureStale.category = diagnostics::DiagnosticCategory::ResourceMissing;
    captureStale.severity = diagnostics::DiagnosticSeverity::Warning;  // §9.6"级别"列：warning
    captureStale.titleKey = "diag.req-capture-state-stale.title";
    captureStale.detailKey = "diag.req-capture-state-stale.detail";
    // paramSchema 两键：捕获时会话基线修订 id/草稿基线修订 id（错位
    // 两侧值对照——知情确认与重捕的判定面；"<none>"＝空基线哨兵）。
    captureStale.paramSchema = R"(["session-revision","draft-baseline"])";
    captureStale.confirmable = false;         // 警告登记面——非 SA-15 确认请求（确认流在草稿写回门）
    captureStale.requiresComparison = false;  // 非比较型三要素码（两侧值在 paramSchema 对照）
    captureStale.retryable = diagnostics::RetryKind::UserRetry;  // 重新捕获后可消除
    captureStale.userVisible = true;
    captureStale.reportable = true;
    captureStale.historical = true;

    // 清单序＝§9.6 表行序（T02/T03 行→T04 行→T05 行→增登行→T06 行
    // 表尾）——确定性序。
    return {d, rowErr, dupId, unitIll, frameUnk, refMissing, seqCycle, poseIllegal,
            noRequiredCase, planDegenerate, inputIncomplete, planMissing,
            captureStale};
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
