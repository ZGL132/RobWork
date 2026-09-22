/**
 * @file   DiagCodes.hpp
 * @brief  modeling 稳定诊断码工厂——MDL-* 码的 CodeDescriptor 登记数据与
 *         注册函数（§9.5 注册纪律：只登记有消费者条目，不预建）。
 *
 * 设计依据：
 *   - units/modeling.md §9.5（MDL- 稳定诊断码登记表——19 码/任务列/逐码
 *     语义；注册纪律原文："码值 ^[A-Z0-9]+(-[A-Z0-9]+)*$ ≤64；一经注册
 *     并进入持久化产物不改义不改拼；不预建无消费者条目——下表每码随
 *     对应任务（§11）注册并附带 CodeDescriptor"）、§3.3（公共头表
 *     DiagCodes.hpp 行——T02）、§14.4（新增语义登记第 4 项）
 *   - units/diagnostics.md §4.5（注册协议——CodeDescriptor 字段约束与
 *     注册期验证）、§4.5 前缀-所有权表（MDL→modeling——已随
 *     diagnostics/src/DiagCodes.cpp 前缀表落位）、§8.8（单元域错误→
 *     诊断码分工——映射数据在 modeling/Errors.hpp，产码唯一经工厂）
 *   - 先例：io/IoDiagnostics.hpp（ioCodeDescriptors＋registerIoCodeTable
 *     同款形态——单元产出描述符清单，注册权威仍在 diagnostics）
 *   - 需求 ERR-01（诊断码稳定可追溯）、NFR-MNT-03（码/文案单一权威）
 *   - 任务契约 tasks/foundation/WP-13-T02.json acceptance 4（公共头落位
 *     ——DiagCodes.hpp 行）＋knownPitfalls P-MDL-8 处置
 *
 * 背景说明（码值权威链——为什么 modeling 只"产出注册数据"不"收编"）：
 * 稳定码的注册表唯一权威＝diagnostics::StableCodeRegistry（PA-1/
 * NFR-MNT-03）；MDL-* 码值的登记权威＝units/modeling.md §9.5 表（卡面
 * 逐码给出类别/级别/confirmable/语义）。本头把 §9.5 表中**当前任务行**
 * 的登记值物化为 CodeDescriptor（逐字段可追溯到卡面），装配期由 L5 经
 * registerModelingCodes 注册——本实现不改动 diagnostics 单元任何文件，
 * 不私定任何卡面之外的码值。
 *
 * 阶段纪律（acceptance 4"只登记有消费者条目，不预建"的执行口径）：
 * §9.5 表 19 行按"任务"列分批注册——本任务（WP-13-T02）只登记任务列
 * 含 T02 的行，当前恰为 1 行：MDL-READINESS-SCHEMA-UNSUPPORTED（T02/T03
 * ——对象 schema 主版本门，与 ObjectTypes.hpp 的对象类型登记同批消费）。
 * 其余 18 行的登记随各自任务落位（T05 导入族/T08 断言与就绪族/T09 转换
 * 族/T13 包族/T18 R2 耦合族），届时在本头工厂清单**表尾追加**对应
 * 描述符并同步单元卡——不提前预建（WP-13-T08 契约注记"MDL-21-COUPLING-
 * STAGE-LOCKED 码登记行随 T18（R2）不预建"即本纪律的契约侧先例）。
 *
 * P-MDL-8 处置锚点（契约 knownPitfalls）：本头消费的 diagnostics
 * CodeDescriptor/IDiagnosticRegistry 契约为 Draft-Structured 未冻结——
 * 按当周 diagnostics 卡现状消费（DiagCodes.hpp v 落位形态），冻结后如
 * 签名漂移按卡 R-MDL-1 增量同步；六边构建形态不受影响（契约注记③）。
 *
 * 线程安全：modelingCodeDescriptors 纯函数；registerModelingCodes 只转发
 * registry.registerCode（注册期单线程约定随 diagnostics 装配语义）。
 * 确定性：清单序＝§9.5 表行序（确定性序；manifest 排序由注册表侧承担
 * ——同注册集同摘要，NFR-COR-02）。
 */

#ifndef IRD_MODELING_DIAGCODES_HPP
#define IRD_MODELING_DIAGCODES_HPP

#include <string_view>
#include <vector>

#include <sdurws/ird/diagnostics/DiagCodes.hpp>  // diagnostics::CodeDescriptor/IDiagnosticRegistry——注册协议（§4.5）

namespace sdurws::ird::modeling {

// =====================================================================
// MDL-* 码值常量（唯一书写点——DiagCodes.cpp 描述符工厂与 Import.cpp
// 产码共用同一常量，禁字符串拼码/第二处字面量，§9.5 尾段产码纪律）
// =====================================================================

/// §9.5 T05 行：MDL-IMPORT-BRANCH-SELECTION（导入/info——多可动分支文件
/// →显式选择主链；辅助分支不构成拒绝）。
inline constexpr std::string_view kMdlImportBranchSelection = "MDL-IMPORT-BRANCH-SELECTION";

/// §9.5 T05 行：MDL-IMPORT-TEMPLATE-RANGE（导入/info——所选主链 4/5 轴或
/// 含 prismatic，超出首版产品模板范围→草稿兼容编辑、模板创建与正式计算
/// 阻断；类型保留不降级。本行为 WP-13-T05 实现期增登——卡 §6.4"诊断
/// 超出首版产品模板范围"语义的码面落位，§14.6 v0.6 登记）。
inline constexpr std::string_view kMdlImportTemplateRange = "MDL-IMPORT-TEMPLATE-RANGE";

/// §9.5 T02/T03 行：MDL-READINESS-SCHEMA-UNSUPPORTED（校验/error——对象
/// schema 主版本超出本程序支持→升级程序/重新编辑；Errors.hpp 映射码
/// modelingDiagCode 与此同源同串）。
inline constexpr std::string_view kMdlReadinessSchemaUnsupported = "MDL-READINESS-SCHEMA-UNSUPPORTED";

/// §9.5 T05 行：MDL-IMPORT-UNSUPPORTED-JOINT（导入/error——mimic/planar/
/// floating/闭环在所选主链→移除或改拓扑，不得绕过；V-10）。
inline constexpr std::string_view kMdlImportUnsupportedJoint = "MDL-IMPORT-UNSUPPORTED-JOINT";

/// §9.5 T05 行：MDL-IMPORT-ZERO-AXIS（导入/error——零轴/非有限轴关节→
/// 修正源文件；该草稿不得提交，MDL-11）。
inline constexpr std::string_view kMdlImportZeroAxis = "MDL-IMPORT-ZERO-AXIS";

/// §9.5 T05/T06 行：MDL-IMPORT-PENDING-CONFIRM（导入/Warning——待确认项
/// 未决（缺 axis 默认 +X/缺限位/工作范围）→逐条确认）。
inline constexpr std::string_view kMdlImportPendingConfirm = "MDL-IMPORT-PENDING-CONFIRM";

/// §9.5 T06 行：MDL-IMPORT-XACRO-UNRESOLVED（导入/error——Xacro 受控展开
/// 失败的语义定位面：未定义宏/未定义参数/缺参/签名外属性/不支持构造/
/// 宏重定义，逐条携带宏名或参数名＋源行列。本行为 WP-13-T06 实现期增登
/// ——卡 §6.5"展开失败→可定位诊断（宏名/行列）"的码面落位（io 护栏码
/// 只覆盖循环/缺失/预算三族，P-MDL-4 口径下 io 公共面不扩，语义面归
/// modeling），§14.6 v0.7 登记）。
inline constexpr std::string_view kMdlImportXacroUnresolved = "MDL-IMPORT-XACRO-UNRESOLVED";

// =====================================================================
// MDL-* 稳定诊断码描述符清单（§9.5 已到任务行的物化；分批纪律见文件头注）
// =====================================================================

/**
 * @brief 产出 modeling 已到注册任务行的 MDL-* 稳定码描述符全集（当前
 *        7 项：MDL-READINESS-SCHEMA-UNSUPPORTED——§9.5 T02/T03 行；外加
 *        WP-13-T05 登记的 T05 行 MDL-IMPORT-{UNSUPPORTED-JOINT,
 *        BRANCH-SELECTION,ZERO-AXIS,PENDING-CONFIRM,TEMPLATE-RANGE}
 *        五码；外加 WP-13-T06 实现期增登的 T06 行 MDL-IMPORT-XACRO-
 *        UNRESOLVED——清单序＝§9.5 表行序；TEMPLATE-RANGE 与 XACRO-
 *        UNRESOLVED 为实现期增登行，§14.6 v0.6/v0.7 登记）。
 *
 * 逐字段登记口径（全部可追溯到卡面/diagnostics 卡，io ioCodeDescriptors
 * 同款自证结构）：
 *   - 码值文本＝§9.5 表"码"列连字符串原样（不私定码值——P-IO-6 同款
 *     纪律；域错误面 ModelingErrorCode::SchemaVersionUnsupported 的映射
 *     码即本串，Errors.hpp modelingDiagCode 与此同源）。
 *   - ownerUnit＝"modeling"（§4.5 前缀-所有权表 MDL→modeling；首段
 *     "MDL"与所有者声明域一致——注册期校验可通过）。
 *   - 分类/严重＝§9.5 行"类别/级别"列："校验/error"→语义归
 *     FormatOrVersion（diagnostics.md §4.3 词表：format-or-version——旧
 *     格式/未来版本/schema/契约不兼容；对象 schema 主版本超出支持正是
 *     该族定义的实例）/Error。
 *   - titleKey/detailKey＝命名约定 diag.<code-lower>.title/.detail
 *     （P-DIAG-9 键/值分离：键体系冻结，文案值归 ui/文案资源——本设施
 *     不携带任何用户可见文案）。
 *   - paramSchema＝["object-type","schema-version","supported-major"]（§4.5
 *     受限 JSON 形命名参数清单，参数名词形 ^[a-z0-9]+(-[a-z0-9]+)*$：哪类
 *     对象/其实际 schema 主版本/本程序支持的最高主版本——三要素定位升级
 *     动作；必填字段显式声明）。
 *   - confirmable＝false（§9.5 行"confirmable"列 false——schema 超版
 *     不是"知情放行"语义，必须升级/重编辑，无可确认放行分支）。
 *   - retryable＝UserRetry（diagnostics RetryKind 动作族映射：建议动作
 *     "升级程序/重新编辑"＝fix-input 族——用户采取措施后可重试）。
 *   - userVisible/reportable/historical＝true（非 Dev 码——§4.5 注册期
 *     验证对 Dev 码的强制 false 不适用；schema 超版须进入用户目录/报告/
 *     历史以支撑"升级后重开"的追溯）。
 *   - registryVersion＝1（首次登记）；deprecated＝false、supersededBy＝
 *     nullopt（§4.5.1 tombstone 仅废弃时置位）。
 *
 * @return 描述符清单（顺序＝§9.5 表行序——确定性；每次调用返回新值）
 */
std::vector<diagnostics::CodeDescriptor> modelingCodeDescriptors();

/**
 * @brief 将 modeling 已到任务行的 MDL-* 码全量注册进稳定码注册表
 *        （§9.5"装配期经 IDiagnosticRegistry::registerCode 注册，
 *        ownerUnit=modeling"的执行面）。
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
void registerModelingCodes(diagnostics::IDiagnosticRegistry& registry);

}  // namespace sdurws::ird::modeling

#endif  // IRD_MODELING_DIAGCODES_HPP
