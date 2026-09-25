/**
 * @file   DiagCodes.hpp
 * @brief  requirements 稳定诊断码工厂——REQ-* 码的 CodeDescriptor 登记数据
 *         与注册函数（§9.6 注册纪律：只登记有消费者条目，不预建）。
 *
 * 设计依据：
 *   - units/requirements.md §9.6（REQ- 稳定诊断码登记表——14 码/任务列/
 *     逐码语义；注册纪律原文："REQ- 前缀；ownerUnit=requirements，装配期
 *     注册，不预建无消费者条目"）、§3.3（公共头表 DiagCodes.hpp 行——T02）、
 *     §14.4（新增语义登记第 4 项——"REQ-* 稳定码 14 项（§9.6，diagnostics
 *     注册纪律内随任务注册）"）
 *   - units/diagnostics.md §4.5（注册协议——CodeDescriptor 字段约束与
 *     注册期验证）、§4.5 前缀-所有权表（REQ→requirements——已随
 *     diagnostics/src/DiagCodes.cpp 前缀表阶段 A 全量落位）、§8.8（单元
 *     域错误→诊断码分工——映射数据在 requirements/Errors.hpp，产码唯一
 *     经工厂）
 *   - 先例：modeling/DiagCodes.hpp（modelingCodeDescriptors＋
 *     registerModelingCodes 同款形态——单元产出描述符清单，注册权威仍在
 *     diagnostics）、io/IoDiagnostics.hpp（更早先例）
 *   - 需求 ERR-01（诊断码稳定可追溯）、NFR-MNT-03（码/文案单一权威）
 *   - 任务契约 tasks/foundation/WP-14-T02.json acceptance 3（"REQ- 稳定码
 *     清单（§9.6 表中 T02 行 REQ-SCHEMA-UNSUPPORTED 先注册，其余码随消费
 *     者任务注册、不预建无消费者条目）"）
 *
 * 背景说明（码值权威链——为什么 requirements 只"产出注册数据"不"收编"）：
 * 稳定码的注册表唯一权威＝diagnostics::StableCodeRegistry（PA-1/
 * NFR-MNT-03）；REQ-* 码值的登记权威＝units/requirements.md §9.6 表（卡面
 * 逐码给出级别/语义）。本头把 §9.6 表中**当前任务行**的登记值物化为
 * CodeDescriptor（逐字段可追溯到卡面），装配期由 L5 经
 * registerRequirementCodes 注册——本实现不改动 diagnostics 单元任何文件，
 * 不私定任何卡面之外的码值。
 *
 * 阶段纪律（acceptance 3"其余码随消费者任务注册、不预建无消费者条目"的
 * 执行口径）：§9.6 表 15 行按"任务"列分批注册——T02 批登记 1 行
 * （REQ-SCHEMA-UNSUPPORTED）；WP-14-T04 批登记 T04 行 4 码（导入族）；
 * WP-14-T05 批登记 T05 行 6 码＋表尾增登 1 码（就绪族：REF-MISSING/
 * SEQ-CYCLE/POSE-ILLEGAL/NO-REQUIRED-CASE/PLAN-DEGENERATE/
 * INPUT-INCOMPLETE，消费者＝Readiness.cpp/CommandHandlers.cpp；增登
 * PLAN-MISSING 承载 §8.1 R6"Warning（零计划）"分支——modeling
 * WP-13-T10 实现期增登先例）；WP-14-T06 批登记 T06 行 1 码（捕获族
 * REQ-CAPTURE-STATE-STALE，消费者＝Capture.cpp STALE 对账步）。其余
 * 2 行（T07 派生族两码）的登记随各自任务落位，届时在本头工厂清单
 * **表尾追加**对应描述符并同步单元卡——不提前预建（modeling §9.5
 * 分批纪律的 REQ 侧同款执行）。
 *
 * P-REQ-8 处置锚点（契约 knownPitfalls）：本头消费的 diagnostics
 * CodeDescriptor/IDiagnosticRegistry 契约为 Draft 未冻结——按当周
 * diagnostics 卡现状消费（DiagCodes.hpp 已落位形态）；冻结后如签名漂移
 * 按卡 R-REQ-1 增量同步；五边构建形态不受影响（契约 note ⑤）。
 *
 * 线程安全：requirementCodeDescriptors 纯函数；registerRequirementCodes
 * 只转发 registry.registerCode（注册期单线程约定随 diagnostics 装配语义）。
 * 确定性：清单序＝§9.6 表行序（确定性序；manifest 排序由注册表侧承担
 * ——同注册集同摘要，NFR-COR-02）。
 */

#ifndef IRD_REQUIREMENTS_DIAGCODES_HPP
#define IRD_REQUIREMENTS_DIAGCODES_HPP

#include <string_view>
#include <vector>

#include <sdurws/ird/diagnostics/DiagCodes.hpp>  // diagnostics::CodeDescriptor/IDiagnosticRegistry——注册协议（§4.5）

namespace sdurws::ird::requirements {

// =====================================================================
// REQ-* 码值常量（唯一书写点——DiagCodes.cpp 描述符工厂与 T03+ 消费者
// 产码共用同一常量，禁字符串拼码/第二处字面量；modeling §9.5 尾段产码
// 纪律的 REQ 侧同款）
// =====================================================================

/// §9.6 T02/T03 行：REQ-SCHEMA-UNSUPPORTED（error——对象 schema 主版本
/// 超出支持→升级程序/重新编辑；NFR-DEP-04 稳定拒绝面。Errors.hpp 映射码
/// requirementDiagCode 与此同源同串）。
inline constexpr std::string_view kReqSchemaUnsupported = "REQ-SCHEMA-UNSUPPORTED";

/// §9.6 T04 行：REQ-IMPORT-ROW-ERROR（error——CSV/JSON 行级错误，定位
/// 三要素＝行号/列名/原文；§7.3 行级错误与 §9.5 mapCsv/mapJson 的产出面）。
/// 生产者＝Import.cpp（WP-14-T04 落位）；context 承载三要素文本。
inline constexpr std::string_view kReqImportRowError = "REQ-IMPORT-ROW-ERROR";

/// §9.6 T04 行：REQ-IMPORT-DUPLICATE-ID（error——导入重复 id/name；
/// §7.3 行级错误清单"重复 id/重复 name"分支的稳定码）。
inline constexpr std::string_view kReqImportDuplicateId = "REQ-IMPORT-DUPLICATE-ID";

/// §9.6 T04 行：REQ-IMPORT-UNIT-ILLEGAL（error——单位声明无法换算/列缺失；
/// §7.3 单位声明校验与必填列结构级拒绝（该文件不可导入）的稳定码）。
inline constexpr std::string_view kReqImportUnitIllegal = "REQ-IMPORT-UNIT-ILLEGAL";

/// §9.6 T04 行：REQ-IMPORT-FRAME-UNKNOWN（warning——Frame 引用浅悬空，
/// 可保留为待解析；§7.3"Frame 引用悬空→警告＋条目保留"的稳定码，跨闭包
/// 半区核对归 T05 就绪层 R2）。
inline constexpr std::string_view kReqImportFrameUnknown = "REQ-IMPORT-FRAME-UNKNOWN";

// =====================================================================
// §9.6 T05 行六码（WP-14-T05 落位——就绪校验族；消费者＝Readiness.cpp
// 产码面＋CommandHandlers.cpp 候选态重估拒绝面，WP-14-T05 同任务）。
// 逐码语义＝§9.6 表行原文；层归属映射见 Readiness.hpp 文件头。
// =====================================================================

/// §9.6 T05 行：REQ-READY-REF-MISSING（error——引用悬空/token 不匹配，
/// §8.1 R1/R8；R0 根引用表/R2 槽-种类/R4 绑定悬空按"引用悬空"语义就近
/// 承载同码，层标注区分——Readiness.hpp 文件头映射表）。
inline constexpr std::string_view kReqReadyRefMissing = "REQ-READY-REF-MISSING";

/// §9.6 T05 行：REQ-READY-SEQ-CYCLE（error——任务顺序成环/重复键，
/// §8.1 R7；悬空前驱名按层-码对齐原则以本码承载，cause 区分）。
inline constexpr std::string_view kReqReadySeqCycle = "REQ-READY-SEQ-CYCLE";

/// §9.6 T05 行：REQ-READY-POSE-ILLEGAL（error——位姿/容差/约束分量非法，
/// §8.1 R3；I-REQ-5 值面的就绪层定位码）。
inline constexpr std::string_view kReqReadyPoseIllegal = "REQ-READY-POSE-ILLEGAL";

/// §9.6 T05 行：REQ-READY-NO-REQUIRED-CASE（warning——无启用必验工况，
/// §8.1 R5；正式拦截归 evidence P-EV-7，本码为预告登记面）。
inline constexpr std::string_view kReqReadyNoRequiredCase = "REQ-READY-NO-REQUIRED-CASE";

/// §9.6 T05 行：REQ-READY-PLAN-DEGENERATE（error——区域退化/计划-区域
/// 失配，§8.1 R6 Blocking 分支；I-REQ-6 盒/覆盖率/采样参数与计划对应性）。
inline constexpr std::string_view kReqReadyPlanDegenerate = "REQ-READY-PLAN-DEGENERATE";

/// §9.6 T05 行：REQ-READY-INPUT-INCOMPLETE（error——启用 Must 条目非法
/// 汇总，ReadinessSummary.valid=false 投影面；命令 prepare 拒绝时随逐项
/// 定位诊断附一条汇总诊断——CommandHandlers.cpp 产码）。
inline constexpr std::string_view kReqReadyInputIncomplete = "REQ-READY-INPUT-INCOMPLETE";

/// §9.6 T05 行表尾增登（实现期增登先例——modeling WP-13-T10 同款）：
/// REQ-READY-PLAN-MISSING（warning——区域已定义而计划集为空，§8.1 R6
/// "Warning（零计划）"分支的原六码无 warning 级承载面；正式判定归评估
/// ——KIN-04 零样本→DataInsufficient，本码为预告登记面）。消费者＝
/// Readiness.cpp R6 层。
inline constexpr std::string_view kReqReadyPlanMissing = "REQ-READY-PLAN-MISSING";

/// §9.6 T06 行（WP-14-T06 落位——捕获族）：REQ-CAPTURE-STATE-STALE
/// （warning——TCP 捕获时会话状态与当前快照不一致，REQ-10 确认流；
/// 卡 §9.8 L-R7 行"会话过期→REQ-CAPTURE-STATE-STALE"、§14.2 R-REQ-5
/// 缓解登记）。消费者＝Capture.cpp STALE 对账步（L-R7 专用——L-R6
/// 拾取无会话修订键，不扩面）。paramSchema 两键 session-revision/
/// draft-baseline＝错位两侧值对照。
inline constexpr std::string_view kReqCaptureStateStale = "REQ-CAPTURE-STATE-STALE";

// =====================================================================
// REQ-* 稳定诊断码描述符清单（§9.6 已到任务行的物化；分批纪律见文件头注）
// =====================================================================

/**
 * @brief 产出 requirements 已到注册任务行的 REQ-* 稳定码描述符全集
 *        （当前 13 项：§9.6 表 T02/T03 行 1 码＋T04 行 4 码＋T05 行 6 码
 *        ＋表尾增登 PLAN-MISSING 一码＋T06 行 CAPTURE-STATE-STALE 一码
 *        ——分批纪律表尾追加；其余 2 行随各自消费者任务 T07 在本清单
 *        表尾追加——分批注册纪律，不预建无消费者条目）。
 *
 * 逐字段登记口径（全部可追溯到卡面/diagnostics 卡，modeling
 * modelingCodeDescriptors 同款自证结构）：
 *   - 码值文本＝§9.6 表"码"列连字符串原样（不私定码值——P-IO-6 同款
 *     纪律；域错误面 RequirementErrorCode::SchemaVersionUnsupported 的
 *     映射码即本串，Errors.hpp requirementDiagCode 与此同源）。
 *   - ownerUnit＝"requirements"（§4.5 前缀-所有权表 REQ→requirements；
 *     首段"REQ"与所有者声明域一致——注册期校验可通过）。
 *   - 分类/严重＝"error"级别；分类取 FormatOrVersion（diagnostics.md
 *     §4.3 词表：format-or-version——旧格式/未来版本/schema/契约不兼容；
 *     对象 schema 主版本超出支持正是该族定义的实例，modeling 同义码
 *     MDL-READINESS-SCHEMA-UNSUPPORTED 同款投影）。
 *   - titleKey/detailKey＝命名约定 diag.<code-lower>.title/.detail
 *     （P-DIAG-9 键/值分离：键体系冻结，文案值归 ui/文案资源——本设施
 *     不携带任何用户可见文案）。
 *   - paramSchema＝["object-type","schema-version","supported-major"]（§4.5
 *     受限 JSON 形命名参数清单，参数名词形 ^[a-z0-9]+(-[a-z0-9]+)*$：哪类
 *     需求对象/其实际 schema 主版本/本程序支持的最高主版本——三要素定位
 *     升级动作；必填字段显式声明）。
 *   - confirmable＝false（schema 超版不是"知情放行"语义，必须升级/重
 *     编辑，无可确认放行分支——modeling 同义码同值）。
 *   - retryable＝UserRetry（diagnostics RetryKind 动作族映射：建议动作
 *     "升级程序/重新编辑"＝fix-input 族——用户采取措施后可重试）。
 *   - userVisible/reportable/historical＝true（非 Dev 码——§4.5 注册期
 *     验证对 Dev 码的强制 false 不适用；schema 超版须进入用户目录/报告/
 *     历史以支撑"升级后重开"的追溯）。
 *   - registryVersion＝1（首次登记）；deprecated＝false、supersededBy＝
 *     nullopt（§4.5.1 tombstone 仅废弃时置位）。
 *
 * @return 描述符清单（顺序＝§9.6 表行序——确定性；每次调用返回新值）
 */
std::vector<diagnostics::CodeDescriptor> requirementCodeDescriptors();

/**
 * @brief 将 requirements 已到任务行的 REQ-* 码全量注册进稳定码注册表
 *        （§9.6 表头"装配期注册，ownerUnit=requirements"的执行面）。
 *
 * 前置：registry 未 seal、不含同码冲突登记（重复注册→注册表抛
 * DiagnosticsError(DuplicateCode)——不捕获不吞，装配期 fail-fast）。
 * 后置：本头清单内全部码可被 registry.find 命中；manifest 反映全表。
 *
 * @param registry [in,out] 目标注册表（调用方持有——L5 装配的
 *                 diagnostics::StableCodeRegistry 实例；本函数不接管）
 * @throws diagnostics::DiagnosticsError 注册期验证失败（码值冲突/字段
 *         非法/前缀-所有权不一致——描述符由本单元按卡面产出，出现即
 *         实现缺陷，fail-fast）
 */
void registerRequirementCodes(diagnostics::IDiagnosticRegistry& registry);

}  // namespace sdurws::ird::requirements

#endif  // IRD_REQUIREMENTS_DIAGCODES_HPP
