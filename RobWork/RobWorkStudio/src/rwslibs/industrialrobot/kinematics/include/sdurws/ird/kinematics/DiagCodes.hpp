/**
 * @file   DiagCodes.hpp
 * @brief  kinematics 稳定诊断码工厂——KIN-* 码的 CodeDescriptor 登记数据
 *         （§9.6 全表 16 码——T02 登记 15 码、T05 随消费任务表尾追加 1 码）
 *         与装配注册函数。
 *
 * 设计依据：
 *   - units/kinematics.md §9.6（KIN- 稳定诊断码登记表——16 码/级别/语义/
 *     任务列；表头注册纪律原文："ownerUnit=kinematics，装配期注册，不预
 *     建无消费者条目"）、§3.3（公共头布局表 Errors.hpp/DiagCodes.hpp 行
 *     ——T02）、§5.5（失败分类与诊断——"比较型诊断带实际值/期望值/单位
 *     （残差 m·rad、裕量比无量纲）"）、§12 交接清单（"diagnostics 收到：
 *     KIN-* CodeDescriptor 清单（§9.6）＋域错误→码映射"）
 *   - units/diagnostics.md §4.3（DiagnosticCategory 15 值词表——P-DIAG-3
 *     实现承载）、§4.4（分类—严重—动作族矩阵——retryable 机械映射依据）、
 *     §4.5（注册协议——句法/前缀-所有权/键唯一/paramSchema/字段不变量；
 *     "不私造参数名"展开登记纪律）、§4.5 前缀-所有权表（KIN→kinematics
 *     ——已随 diagnostics/src/DiagCodes.cpp 前缀表阶段 A 全量落位）
 *   - 先例：requirements/DiagCodes.hpp（WP-14-T02 同款形态——单元产出
 *     描述符清单，注册权威仍在 diagnostics）、modeling/DiagCodes.hpp
 *   - 需求 ERR-01（诊断码稳定可追溯）、NFR-MNT-03（码/文案单一权威）
 *   - 任务契约 tasks/foundation/WP-15-T02.json acceptance 4（"§9.6 全表
 *     15 个 KIN- 稳定码清单登记（注册义务随各消费任务展开——装配期注册，
 *     不预建无消费者条目）"）
 *
 * 背景说明（码值权威链——为什么 kinematics 只"产出注册数据"不"收编"）：
 * 稳定码的注册表唯一权威＝diagnostics::StableCodeRegistry（PA-1/
 * NFR-MNT-03）；KIN-* 码值的登记权威＝units/kinematics.md §9.6 表（卡面
 * 逐码给出级别/语义）。本头把 §9.6 **全表**的登记值物化为
 * CodeDescriptor（逐字段可追溯到卡面与 diagnostics 词表），装配期由 L5
 * 经 registerKinematicsCodes 注册——本实现不改动 diagnostics 单元任何
 * 文件，不私定任何卡面之外的码值。表行随消费任务表尾追加（T05 追加
 * 行 16 KIN-POINT-REF-DANGLING——悬空引用 InputInvalid 素材的产码面），
 * 既有行不重排（枚举/清单序进入确定性契约）。
 *
 * 阶段纪律（T02 acceptance 4 括号内三个短语的执行口径；行序纪律延用于
 * 后续追加行）：
 *   1. "全表 15 个稳定码清单登记"（T02 时点口径；T05 起全表 16 行——
 *      本头工厂清单恒恰含 §9.6 全表当前行数
 *      （与 requirements T02"仅登记任务列含 T02 的行"的差异来自各自契约
 *      acceptance 的明文差异：kinematics acceptance 4 要求全表清单）。
 *      清单序＝§9.6 表行序（确定性序）。
 *   2. "注册义务随各消费任务展开——装配期注册"——生产者接口（§9.2 各
 *      接口的产码路径）随 T03~T10 逐个落地；诊断**实例**的产出（经
 *      diagnostics 工厂）只能消费已注册码，注册动作本身发生在装配期
 *      （L5 装配清单调用 registerKinematicsCodes），开发期不向任何全局
 *      注册表注册。
 *   3. "不预建无消费者条目"——本实现不改动 diagnostics 单元的装配清单
 *      （allowedFiles 红线）；KIN-* 码进入 diagnostics 全局装配属
 *      diagnostics 所有者的治理动作（requirements T02 同款收编确认口径）。
 *
 * P-KIN-7 处置锚点（契约 knownPitfalls）：本头消费的 diagnostics
 * CodeDescriptor/StableCodeRegistry 契约为 Draft 未冻结——按当周
 * diagnostics 卡现状消费（DiagCodes.hpp 已落位形态）；冻结后如签名漂移
 * 按 §14.3 P-KIN-7 增量同步；六边构建形态不受影响（契约 note）。
 *
 * 线程安全：kinematicsCodeDescriptors 纯函数；registerKinematicsCodes
 * 只转发 registry.registerCode（注册期单线程约定随 diagnostics 装配语义）。
 * 确定性：清单序＝§9.6 表行序（确定性序；manifest 排序由注册表侧承担
 * ——同注册集同摘要，NFR-COR-02）。
 */

#ifndef IRD_KINEMATICS_DIAGCODES_HPP
#define IRD_KINEMATICS_DIAGCODES_HPP

#include <string_view>
#include <vector>

#include <sdurws/ird/diagnostics/DiagCodes.hpp>  // diagnostics::CodeDescriptor/IDiagnosticRegistry——注册协议（§4.5）

namespace sdurws::ird::kinematics {

// =====================================================================
// KIN-* 码值常量（唯一书写点——DiagCodes.cpp 描述符工厂与 T03+ 消费者
// 产码共用同一常量，禁字符串拼码/第二处字面量；modeling/requirements
// 码值常量纪律的 KIN 侧同款）
// =====================================================================

/// §9.6 行 1：KIN-NO-DEVICE（error——无可用设备；§9.2 域错误 NoDevice
/// 的同义诊断登记面）。
inline constexpr std::string_view kKinNoDevice = "KIN-NO-DEVICE";
/// §9.6 行 2：KIN-NO-TCP（error——TCP 未配置/悬空；§9.2 域错误 NoTcp
/// 的同义诊断登记面）。
inline constexpr std::string_view kKinNoTcp = "KIN-NO-TCP";
/// §9.6 行 3：KIN-TARGET-ILLEGAL（error——目标位姿/容差非法）。
inline constexpr std::string_view kKinTargetIllegal = "KIN-TARGET-ILLEGAL";
/// §9.6 行 4：KIN-RESIDUAL-EXCEEDED（warning——FK 验算残差超容差；
/// 比较型：实际/期望/单位）。
inline constexpr std::string_view kKinResidualExceeded = "KIN-RESIDUAL-EXCEEDED";
/// §9.6 行 5：KIN-JOINT-LIMIT-VIOLATED（warning——解超限位）。
inline constexpr std::string_view kKinJointLimitViolated = "KIN-JOINT-LIMIT-VIOLATED";
/// §9.6 行 6：KIN-NEAR-LIMIT（warning——接近限位；阈值读 policy）。
inline constexpr std::string_view kKinNearLimit = "KIN-NEAR-LIMIT";
/// §9.6 行 7：KIN-NEAR-SINGULAR（warning——条件数恶化/可操作度低；
/// 阈值读 policy）。
inline constexpr std::string_view kKinNearSingular = "KIN-NEAR-SINGULAR";
/// §9.6 行 8：KIN-COLLISION-FILTERED（warning——该解因碰撞被过滤，
/// 构型级）。
inline constexpr std::string_view kKinCollisionFiltered = "KIN-COLLISION-FILTERED";
/// §9.6 行 9：KIN-COLLISION-UNAVAILABLE（warning——碰撞检测器缺失/策略
/// 未启用→证据缺失；KIN-05）。
inline constexpr std::string_view kKinCollisionUnavailable = "KIN-COLLISION-UNAVAILABLE";
/// §9.6 行 10：KIN-SEARCH-EXHAUSTED（warning——搜索未果；DataInsufficient
/// 素材）。
inline constexpr std::string_view kKinSearchExhausted = "KIN-SEARCH-EXHAUSTED";
/// §9.6 行 11：KIN-SOLVER-INTERNAL（error——求解器内部错误；fail-fast
/// 轨道）。
inline constexpr std::string_view kKinSolverInternal = "KIN-SOLVER-INTERNAL";
/// §9.6 行 12：KIN-CONFIG-ILLEGAL（error——求解配置非法；I-KIN-4）。
inline constexpr std::string_view kKinConfigIllegal = "KIN-CONFIG-ILLEGAL";
/// §9.6 行 13：KIN-SAMPLE-IDENTITY-MISMATCH（error——样本集身份与快照
/// 不一致）。
inline constexpr std::string_view kKinSampleIdentityMismatch = "KIN-SAMPLE-IDENTITY-MISMATCH";
/// §9.6 行 14：KIN-COVERAGE-ZERO-SAMPLES（warning——零样本→覆盖率不
/// 定义；DataInsufficient 素材）。
inline constexpr std::string_view kKinCoverageZeroSamples = "KIN-COVERAGE-ZERO-SAMPLES";
/// §9.6 行 15：KIN-RESULT-INCOMPLETE（warning——批次不完整；NotRun 清单）。
inline constexpr std::string_view kKinResultIncomplete = "KIN-RESULT-INCOMPLETE";
/// §9.6 行 16：KIN-POINT-REF-DANGLING（error——任务点引用悬空：appliesTo
/// 显式清单引用点集外对象→该 (悬空点，工况) 工作项产 InputInvalid 素材
/// ——V-12；行随 WP-15-T05 消费任务表尾追加，登记随卡 §9.6/§14.6 v0.5）。
inline constexpr std::string_view kKinPointRefDangling = "KIN-POINT-REF-DANGLING";

// =====================================================================
// KIN-* 稳定诊断码描述符清单（§9.6 全表的物化——T02 时 15 行、T05 起
// 16 行；阶段纪律见文件头注）
// =====================================================================

/**
 * @brief 产出 kinematics §9.6 全表（当前 16 码）的稳定码描述符全集
 *        （T02 契约 acceptance 4"全表 15 个 KIN- 稳定码清单登记"的执行
 *        面；T05 起随消费任务表尾追加，清单恒与卡面全表同长）。
 *
 * 逐字段登记口径（全部可追溯到卡面/diagnostics 卡；逐码分类与动作族的
 * 落值依据在 DiagCodes.cpp 逐码注释——diagnostics 卡"逐码落值依据，实现
 * ＝DiagCodes.cpp 内置表逐码注释"同款登记形态）：
 *   - 码值文本＝§9.6 表"码"列连字符串原样（不私定码值——P-IO-6 同款
 *     纪律；上方 16 个常量即唯一书写点）。
 *   - ownerUnit＝"kinematics"（§4.5 前缀-所有权表 KIN→kinematics；首段
 *     "KIN"与所有者声明域一致——注册期校验可通过）。
 *   - severity＝§9.6 表"级别"列逐行原值（T05 起 7 error＋9 warning——注册表
 *     对 Dev 强制三项 false，本表无 Dev 码；用户级两档与 §4.4 默认矩阵
 *     的用户级分界一致）。
 *   - category＝diagnostics §4.3 词表 15 值内落值（P-DIAG-3 不新增词表
 *     值），逐码锚点：语义命中"输入/配置非法"→input-invalid；命中
 *     "策略评估否决（碰撞过滤、行程上限硬口径等——域内事实）"→
 *     policy-denied；命中"DataInsufficient 轴（搜索未果 C5）"→
 *     data-insufficient；命中"schema/契约不兼容"→format-or-version；
 *     命中"防御性检查失败/不变量违反"→internal；命中"outcome=Failed
 *     轴"→execution-failed。
 *   - retryable＝§4.4 分类动作族机械映射（含重试语义的动作族 fix-input/
 *     adjust-policy/supply-evidence/retry-task 等→UserRetry；report-bug
 *     族→Never——KIN-SOLVER-INTERNAL 唯一 Never）。
 *   - paramSchema＝"[]"（无参数显式声明——diagnostics 卡展开登记纪律
 *     "各单元产码路径落地时按需增量登记并升 registryVersion，不私造
 *     参数名"；比较型三要素走 requiresComparison/实例 comparison 面，
 *     不占参数名）。
 *   - confirmable＝false（16 码均无"策略校验超限待用户显式确认"语义
 *     ——SA-15 确认流属 policy/modeling 域码；故 requiresComparison 的
 *     强制前件不触发）。requiresComparison 仅两码 true：KIN-RESIDUAL-
 *     EXCEEDED（§9.6"比较型：实际/期望/单位"原文）与 KIN-NEAR-LIMIT
 *     （§5.5 比较型举例"裕量比无量纲"点名）。
 *   - titleKey/detailKey＝命名约定 diag.<code-lower>.title/.detail
 *     （P-DIAG-9 键/值分离：键体系冻结，文案值归 ui/文案资源——注册期
 *     键形校验强制此约定）。
 *   - userVisible/reportable/historical＝true（全部为用户级码——§4.5
 *     仅对 Dev 码强制三项 false）。
 *   - registryVersion＝1（首次登记）；deprecated＝false、supersededBy＝
 *     nullopt（§4.5.1 tombstone 仅废弃时置位）。
 *
 * @return 描述符清单（顺序＝§9.6 表行序——确定性；每次调用返回新值）
 */
std::vector<diagnostics::CodeDescriptor> kinematicsCodeDescriptors();

/**
 * @brief 将 §9.6 全表（当前 16 码）注册进稳定码注册表（§9.6 表头"装配
 *        期注册，ownerUnit=kinematics"的执行面——L5 装配清单的调用入口）。
 *
 * 前置：registry 未 seal、不含同码/同文案键冲突登记（重复注册→注册表抛
 * DiagnosticsError(DuplicateCode)——不捕获不吞，装配期 fail-fast）。
 * 后置：本头清单内全部 16 码可被 registry.find 命中；manifest 反映全表。
 *
 * @param registry [in,out] 目标注册表（调用方持有——L5 装配的
 *                 diagnostics::StableCodeRegistry 实例；本函数不接管）
 * @throws diagnostics::DiagnosticsError 注册期验证失败（码值冲突/字段
 *         非法/前缀-所有权不一致/键形偏离——描述符由本单元按卡面产出，
 *         出现即实现缺陷，fail-fast）
 */
void registerKinematicsCodes(diagnostics::IDiagnosticRegistry& registry);

}  // namespace sdurws::ird::kinematics

#endif  // IRD_KINEMATICS_DIAGCODES_HPP
