/**
 * @file   DiagCodes.hpp
 * @brief  optimization 稳定诊断码工厂——OPT-* 码的 CodeDescriptor 登记
 *         数据（units/optimization.md §6.6 登记表全表 15 码）与装配注册
 *         函数（WP-20-T02 构建落位随批登记）。
 *
 * 设计依据：
 *   - units/optimization.md §6.6（优化域稳定诊断码登记表——OPT- 前缀
 *     ownerUnit=optimization，15 码：码/严重度/语义/备注四列；"前缀 OPT
 *     已在 diagnostics §4.5 业务域命名空间清单登记；本表为域码的设计
 *     登记，实现随 WP-20-T02 落位经 IDiagnosticRegistry::registerCode
 *     注册，并同步收编 diagnostics.md §4.6"）、§6.5（阶段锁诊断的呈现
 *     边界——运行启动阻塞非候选淘汰）、§5.7（阶段锁全表——STAGE-LOCKED
 *     的触发面）、§6.4（Preflight——BLOCKED/EVALUATOR-MISSING 的产出面）、
 *     §16.3 P-OPT-1（阶段锁码落位 OPT-STAGE-LOCKED——与需求文本引用名
 *     IRD-OPT-STAGE-LOCKED 的对应关系在卡内登记，前缀归一按 diagnostics
 *     §4.5"首段＝单元短前缀"硬约定）
 *   - units/diagnostics.md §4.3（DiagnosticCategory 15 值词表——P-DIAG-3
 *     实现承载）、§4.4（分类—严重—动作族矩阵——retryable 机械映射依据）、
 *     §4.5（注册协议——句法/前缀-所有权/键唯一/paramSchema/字段不变量；
 *     "不私造参数名"展开登记纪律）、§4.5 前缀-所有权表（OPT→optimization
 *     ——diagnostics/src/DiagCodes.cpp kPrefixOwners 实测在册行
 *     {"OPT","optimization"}，2026-10-07；注册无前缀阻塞）
 *   - 先例：kinematics/DiagCodes.hpp（WP-15-T02 同款形态——依赖白名单含
 *     diagnostics 编译边的单元产出 CodeDescriptor 清单＋注册函数；
 *     dynamics WP-17-T02/trajectory WP-16-T03 同因同款；selection/
 *     drivetrain 的"纯登记表、注册归 L5 装配"形态在本单元不适用——本卡
 *     §3.2 依赖白名单含 diagnostics 编译边，注册执行面可落本单元）
 *   - 需求 ERR-01（稳定诊断码＋比较型字段齐备）、OPT-03（阶段锁——
 *     STAGE-LOCKED 码的消费需求族）、OPT-11（Preflight——BLOCKED 码）、
 *     OPT-12（一站式导出——EXPORT-CONTRACT-STALE）、NFR-MNT-03（码/文案
 *     单一权威）
 *   - 任务契约 tasks/foundation/WP-20-T02.json acceptance 2（"OPT-* 15 码
 *     按 units/optimization.md §6.6 登记表装配期注册——diagnostics §4.5
 *     协议"）
 *
 * ★ 登记范围口径（T02 与后续任务的分界——诚实登记，防扩大）：本表物化的
 *   是卡面 §6.6 登记表**已具名**的 15 个具体码值（全表——无"拟注册待增"
 *   尾巴；WP-21 的 OPT-D 扩展如需增码，先走卡面 §6.6 增量修订、再表尾
 *   追加，本头既有行不重排——清单序进入登记契约）。本任务不预建、不占位
 *   （NFR-MNT-04 不建无边界价值包装器）。
 *
 * ★ P-OPT-1 落位口径（契约 knownPitfalls 点名项）：需求 §15.0/DTB WP-20-T04
 *   引用名为 IRD-OPT-STAGE-LOCKED，不符合 diagnostics §4.5"码值首段＝单元
 *   短前缀"的注册协议——按卡面 §6.6 登记落位为 OPT-STAGE-LOCKED，对应
 *   关系登记于卡面 §6.6 行注与 §16.3 P-OPT-1（不改需求语义；如需求侧
 *   另有裁决走需求变更）。本头常量 kOptStageLocked 即该落位码的唯一
 *   书写点，后续 WP-20-T04 阶段锁产码消费同一常量。
 *
 * 背景说明（码值权威链）：稳定码的注册表唯一权威＝
 * diagnostics::StableCodeRegistry（PA-1/NFR-MNT-03）；OPT-* 码值的登记
 * 权威＝units/optimization.md §6.6 表（卡面逐码给出严重度与语义）。本头
 * 把 §6.6 **全表**的登记值物化为 CodeDescriptor（逐字段可追溯到卡面与
 * diagnostics 词表；分类/重试族的落值锚点取卡面 §6.6"失败语义归类"段——
 * 该段把 15 码划为三组），装配期由 L5 经 registerOptimizationCodes 注册
 * ——本实现不改动 diagnostics 单元任何文件，不私定任何卡面之外的码值。
 * 与 kinematics/dynamics/trajectory 的阶段纪律同款：诊断**实例**的产出
 * （经 diagnostics 工厂）只能消费已注册码，生产者接口（§12 十接口与
 * opt-static-screen 评估器的产码路径）随 WP-20-T03~T09 逐个落地；注册
 * 动作本身发生在装配期，开发期不向任何全局注册表注册。
 *
 * 线程安全：optimizationCodeDescriptors() 纯函数（可重入）；
 * registerOptimizationCodes 只转发 registry.registerCode（注册期单线程
 * 约定随 diagnostics 装配语义）。确定性：清单序＝§6.6 表行序（确定性
 * 序，NFR-COR-02），后续任务追加码只允许表尾追加、既有行不重排。
 */

#ifndef IRD_OPTIMIZATION_DIAGCODES_HPP
#define IRD_OPTIMIZATION_DIAGCODES_HPP

#include <string_view>
#include <vector>

#include <sdurws/ird/diagnostics/DiagCodes.hpp>  // diagnostics::CodeDescriptor/IDiagnosticRegistry——注册协议（§4.5）

namespace sdurws::ird::optimization {

// =====================================================================
// OPT-* 码值常量（唯一书写点——DiagCodes.cpp 描述符工厂与 WP-20-T03+
// 消费者产码共用同一常量，禁字符串拼码/第二处字面量；kinematics/
// dynamics/trajectory 码值常量纪律的 OPT 侧同款）
// =====================================================================

/// §6.6 行 1：OPT-STAGE-LOCKED（error——阶段锁：激活当前阶段不支持的
/// 能力〔变量/约束/目标/暂停/检查点〕；P-OPT-1 落位码——需求引用名
/// IRD-OPT-STAGE-LOCKED 的前缀归一形态，对应关系登记于卡面 §6.6/§16.3）。
inline constexpr std::string_view kOptStageLocked = "OPT-STAGE-LOCKED";
/// §6.6 行 2：OPT-INPUT-INVALID（error——研究定义/配置非法：绑定互斥
/// 〔I-OPT-7〕/预算非法/种子 0〔I-OPT-2〕等——fail-fast 面）。
inline constexpr std::string_view kOptInputInvalid = "OPT-INPUT-INVALID";
/// §6.6 行 3：OPT-VAR-LOCKED（error——补丁触及锁定/未授权变量；比较型
/// 定位：绑定 token＋对象定位——§5.4 改型授权与锁定）。
inline constexpr std::string_view kOptVarLocked = "OPT-VAR-LOCKED";
/// §6.6 行 4：OPT-VAR-UNBINDABLE（warning——变量绑定到当前基线不可绑定
/// 字段（Mesh 基线截面、Explicit 基线 DH 等）——§5.3 变量表绑定前提）。
inline constexpr std::string_view kOptVarUnbindable = "OPT-VAR-UNBINDABLE";
/// §6.6 行 5：OPT-PATCH-ILLEGAL（error——补丁非法：未知绑定/重复/越界/
/// 序列化失败——§5.5 补丁构造边界；I-OPT-9 不静默截断）。
inline constexpr std::string_view kOptPatchIllegal = "OPT-PATCH-ILLEGAL";
/// §6.6 行 6：OPT-CANDIDATE-COMPILE-FAILED（error——候选编译失败，透传
/// RT-* 编译链码（cause 链保留）——§6.2 R1 管线候选编译步）。
inline constexpr std::string_view kOptCandidateCompileFailed
    = "OPT-CANDIDATE-COMPILE-FAILED";
/// §6.6 行 7：OPT-TOPOLOGY-REJECTED（error——拓扑/链型不被当前启用范围
/// 支持（§2.2 红线 7：R1 首版不启用的链型维持阻断））。
inline constexpr std::string_view kOptTopologyRejected = "OPT-TOPOLOGY-REJECTED";
/// §6.6 行 8：OPT-EVALUATOR-MISSING（error——阶段必需评估器未注册或契约
/// 版本不符——§6.4 Preflight 检查项 #7、§5.7 StageD 行）。
inline constexpr std::string_view kOptEvaluatorMissing = "OPT-EVALUATOR-MISSING";
/// §6.6 行 9：OPT-PREFLIGHT-BLOCKED（warning——Preflight 存在阻塞项（逐项
/// 定位见报告）——§6.4 检查面汇总；warning 级目录条目呈现）。
inline constexpr std::string_view kOptPreflightBlocked = "OPT-PREFLIGHT-BLOCKED";
/// §6.6 行 10：OPT-METRIC-NOT-COMPUTABLE（warning——指标不可算（显示
/// "—"；不参与 Pareto；不判不可行）——§7.2 缺失语义、DOPT-13）。
inline constexpr std::string_view kOptMetricNotComputable
    = "OPT-METRIC-NOT-COMPUTABLE";
/// §6.6 行 11：OPT-COMBO-OUT-OF-SCOPE（error——离散器件/直线传动/耦合链
/// 超出当前启用范围——§5.6、SEL-09 范围外口径的 R1 侧拒绝始点）。
inline constexpr std::string_view kOptComboOutOfScope = "OPT-COMBO-OUT-OF-SCOPE";
/// §6.6 行 12：OPT-ROBUSTNESS-PROTOCOL-MISSING（error——P-04 未冻结，
/// 鲁棒性/灵敏度复核不可启用——OPT-09、O-28、附录 C 硬前置）。
inline constexpr std::string_view kOptRobustnessProtocolMissing
    = "OPT-ROBUSTNESS-PROTOCOL-MISSING";
/// §6.6 行 13：OPT-SEARCH-EMPTY（warning——搜索未产生合法候选（数据不足
/// 语义，非任务不可行）——§8.2 幸存集为空、OPT-VER-120）。
inline constexpr std::string_view kOptSearchEmpty = "OPT-SEARCH-EMPTY";
/// §6.6 行 14：OPT-EXPORT-CONTRACT-STALE（error——导出契约/证据契约版本
/// 过期，阻断正式导出——§11.4、OPT-12 版本三元组校验）。
inline constexpr std::string_view kOptExportContractStale
    = "OPT-EXPORT-CONTRACT-STALE";
/// §6.6 行 15：OPT-APPLY-PLAN-INVALID（error——候选应用组装非法（基线
/// 过期/身份不符/补丁与结果不对应）——§10.3 组装前置校验清单）。
inline constexpr std::string_view kOptApplyPlanInvalid = "OPT-APPLY-PLAN-INVALID";

// =====================================================================
// OPT-* 稳定诊断码描述符清单（§6.6 登记表的物化——装配期注册进
// diagnostics StableCodeRegistry 的数据源；见文件头注"码值权威链"段）
// =====================================================================

/**
 * @brief 产出 optimization §6.6 登记表全表（15 码）的稳定码描述符全集
 *        （契约 acceptance 2"OPT-* 15 码按 units/optimization.md §6.6
 *        登记表装配期注册"的执行面；后续任务增码只允许表尾追加，清单
 *        恒与卡面 §6.6 全表同长）。
 *
 * 逐字段登记口径（全部可追溯到卡面/diagnostics 词表；逐码分类与动作族
 * 的落值依据在 DiagCodes.cpp 逐码注释——diagnostics 卡"逐码落值依据，
 * 实现＝DiagCodes.cpp 内置表逐码注释"同款登记形态）：
 *   - 码值文本＝§6.6 表"码"列连字符串原样（不私定码值；上方 15 个常量
 *     即唯一书写点）。
 *   - ownerUnit＝"optimization"（§4.5 前缀-所有权表 OPT→optimization；
 *     首段"OPT"与所有者声明域一致——注册期校验可通过）。
 *   - severity＝§6.6 表"severity"列逐行原值（全表 11 error＋4 warning——
 *     注册表对 Dev 强制三项 false，本表无 Dev 码）。
 *   - category＝diagnostics §4.3 词表 15 值内落值（P-DIAG-3 不新增词表
 *     值），落值依据＝卡面 §6.6"失败语义归类"段三组划分，逐码锚点见
 *     DiagCodes.cpp 调用点注释。
 *   - retryable＝§4.4 分类动作族机械映射（fix-input/supply-evidence/
 *     retry-task/adjust-policy 族→UserRetry；本表无 report-bug/none 族
 *     码——§6.6 归类段三组均为"调用方可修复/可供给/可重算"面）。
 *   - paramSchema＝"[]"（无参数显式声明——diagnostics 卡展开登记纪律
 *     "各单元产码路径落地时按需增量登记并升 registryVersion，不私造
 *     参数名"；比较型定位〔如 VAR-LOCKED 的"绑定 token＋对象定位"〕走
 *     诊断实例 subject/context 面，不占参数名）。
 *   - requiresComparison＝全表 false：§6.6 表四列（码/严重度/语义/备注）
 *     **无"比较型"列**、备注列无任何"（实际/要求/单位）"三元组点名——
 *     与 trajectory 卡 §14.4 显式设比较型列不同（TRJ 表恰五码 true），
 *     本表零码置 true（从严执行：卡面未点名即不登记比较语义——私造
 *     比较型会触发工厂侧实例 comparison 三要素强制，而 §6.6 各码的定位
 *     面走 subject/context 非数值三元组）。confirmable＝false 随之成立
 *     （SA-15 不变量 confirmable⇒requiresComparison；15 码均无"策略校验
 *     超限待用户显式确认"语义——SA-15 确认流属 policy/modeling 域码，
 *     阶段锁/Preflight 阻塞是运行启动拒绝面非确认面）。
 *   - titleKey/detailKey＝命名约定 diag.<code-lower>.title/.detail
 *     （P-DIAG-9 键/值分离：键体系冻结，文案值归 ui/文案资源——注册期
 *     键形校验强制此约定）。
 *   - userVisible/reportable/historical＝true（全部为用户级码——§4.5
 *     仅对 Dev 码强制三项 false）。
 *   - registryVersion＝1（首次登记）；deprecated＝false、supersededBy＝
 *     nullopt（§4.5.1 tombstone 仅废弃时置位）。
 *
 * @return 描述符清单（顺序＝§6.6 表行序——确定性；每次调用返回新值）
 */
std::vector<diagnostics::CodeDescriptor> optimizationCodeDescriptors();

/**
 * @brief 将 §6.6 登记表全表（15 码）注册进稳定码注册表（卡 §6.6 注册
 *        协议——"实现随 WP-20-T02 落位经 IDiagnosticRegistry::registerCode
 *        注册"的执行面——L5 装配清单的调用入口；本函数不改动 diagnostics
 *        单元文件，注册权威仍在 StableCodeRegistry）。
 *
 * 前置：registry 未 seal、不含同码/同文案键冲突登记（重复注册→注册表抛
 * DiagnosticsError(DuplicateCode)——不捕获不吞，装配期 fail-fast）。
 * 后置：本头清单内全部 15 码可被 registry.find 命中；manifest 反映全表。
 *
 * @param registry [in,out] 目标注册表（调用方持有——L5 装配的
 *                 diagnostics::StableCodeRegistry 实例；本函数不接管）
 * @throws diagnostics::DiagnosticsError 注册期验证失败（码值冲突/字段
 *         非法/前缀-所有权不一致/键形偏离——描述符由本单元按卡面产出，
 *         出现即实现缺陷，fail-fast）
 */
void registerOptimizationCodes(diagnostics::IDiagnosticRegistry& registry);

}  // namespace sdurws::ird::optimization

#endif  // IRD_OPTIMIZATION_DIAGCODES_HPP
