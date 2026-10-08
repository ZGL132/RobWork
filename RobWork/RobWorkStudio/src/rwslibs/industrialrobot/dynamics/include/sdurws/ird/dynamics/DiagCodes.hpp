/**
 * @file   DiagCodes.hpp
 * @brief  dynamics 稳定诊断码工厂——DYN-* 码的 CodeDescriptor 登记数据
 *         （units/dynamics.md §9.4 拟注册清单全表 16 码）与装配注册函数
 *         （WP-17-T02 构建落位随批登记）。
 *
 * 设计依据：
 *   - units/dynamics.md §9.4（diagnostics 协作——DYN-* 拟注册清单设计
 *     基线 v1 15 码＋WP-17-T05 增量修订表尾增行 1 码＝全表 16 码：码/严重
 *     度/比较型/用途四列；"码值权威归 StableCodeRegistry，本表为拟注册
 *     清单——随 WP-17-T02/T03/T05 提交"；§5.6（数值稳定性与失败
 *     语义——DYN-RNEA-FAILED/DYN-COUPLING-GATE-REJECTED 的触发条件）、
 *     §4.3（轨迹消费契约——DYN-UPSTREAM-TRAJECTORY-INCOMPATIBLE/
 *     DYN-TIME-PARAM-MISSING/DYN-SERIES-NON-MONOTONIC）、§4.6（序列纪律
 *     ——DYN-SAMPLE-GAP/非有限数）、§5.5（摩擦缺失降级——DYN-FRICTION-
 *     MISSING/DYN-PROPERTY-DOWNGRADED）、§6（正动力学一致性——DYN-FD-*
 *     三码＋DYN-FD-NUMERIC-ANOMALY）、§8.1（工况引用缺失——DYN-CONDITION-
 *     REF-MISSING））
 *   - units/diagnostics.md §4.3（DiagnosticCategory 15 值词表——P-DIAG-3
 *     实现承载）、§4.4（分类—严重—动作族矩阵——retryable 机械映射依据）、
 *     §4.5（注册协议——句法/前缀-所有权/键唯一/paramSchema/字段不变量；
 *     "不私造参数名"展开登记纪律）、§4.5 前缀-所有权表（DYN→dynamics
 *     ——diagnostics/src/DiagCodes.cpp kPrefixOwners 实测在册行
 *     {"DYN","dynamics"}，2026-10-06；注册无前缀阻塞）
 *   - 先例：kinematics/DiagCodes.hpp（WP-15-T02 同款形态——依赖白名单含
 *     diagnostics 编译边的单元产出 CodeDescriptor 清单＋注册函数；selection/
 *     drivetrain 的"纯登记表、注册归 L5 装配"形态在 dynamics 不适用——
 *     本卡 §3.2 CMake 行明文 PRIVATE 链 diagnostics，注册执行面可落本单元）
 *   - 需求 ERR-01（稳定诊断码＋比较型字段齐备）、DYN-06（数据不足降级）、
 *     NFR-MNT-03（码/文案单一权威）、NFR-COR-03（非有限数拒绝）
 *   - 任务契约 tasks/foundation/WP-17-T02.json acceptance 2（"DYN-* 域
 *     诊断码按 units/dynamics.md 登记表装配期注册"）
 *
 * ★ 登记范围口径（T02/T03 与 T05 的分界——诚实登记，防扩大）：本表物化
 *   的是卡面 §9.4 登记表**已具名**的 16 个具体码值（v1 全表 15 码＋T05
 *   表尾增行 DYN-FD-NUMERIC-ANOMALY——原为卡面 §6.1/§10.2 行文提及但
 *   v1 表未具名的码，随其消费任务 WP-17-T05 先走单元卡 §9.4 增量修订、
 *   再表尾追加——本头既有行不重排，清单序进入登记契约；后续增码同款
 *   纪律）。本头不预建、不占位任何登记表之外码（NFR-MNT-04 不建无边界
 *   价值包装器）。
 *
 * 背景说明（码值权威链）：稳定码的注册表唯一权威＝
 * diagnostics::StableCodeRegistry（PA-1/NFR-MNT-03）；DYN-* 码值的登记
 * 权威＝units/dynamics.md §9.4 表（卡面逐码给出严重度/比较型/用途）。
 * 本头把 §9.4 **全表**的登记值物化为 CodeDescriptor（逐字段可追溯到卡面
 * 与 diagnostics 词表），装配期由 L5 经 registerDynamicsCodes 注册——
 * 本实现不改动 diagnostics 单元任何文件，不私定任何卡面之外的码值。
 * 与 kinematics 的阶段纪律同款：诊断**实例**的产出（经 diagnostics 工厂）
 * 只能消费已注册码，生产者接口（§10 评估器/统计/证据装配各接口的产码
 * 路径）随 WP-17-T03~T10 逐个落地；注册动作本身发生在装配期，开发期不
 * 向任何全局注册表注册。
 *
 * 线程安全：dynamicsCodeDescriptors() 纯函数（可重入）；
 * registerDynamicsCodes 只转发 registry.registerCode（注册期单线程约定
 * 随 diagnostics 装配语义）。确定性：清单序＝§9.4 表行序（确定性序，
 * NFR-COR-02），后续任务追加码只允许表尾追加、既有行不重排。
 */

#ifndef IRD_DYNAMICS_DIAGCODES_HPP
#define IRD_DYNAMICS_DIAGCODES_HPP

#include <string_view>
#include <vector>

#include <sdurws/ird/diagnostics/DiagCodes.hpp>  // diagnostics::CodeDescriptor/IDiagnosticRegistry——注册协议（§4.5）

namespace sdurws::ird::dynamics {

// =====================================================================
// DYN-* 码值常量（唯一书写点——DiagCodes.cpp 描述符工厂与 WP-17-T03+
// 消费者产码共用同一常量，禁字符串拼码/第二处字面量；kinematics/
// modeling/requirements 码值常量纪律的 DYN 侧同款）
// =====================================================================

/// §9.4 行 1：DYN-INPUT-INVALID（error——输入非法：跨修订绑定/维度
/// 不匹配/配置非法/模式组合非法——评估级素材①"输入非法"；§4.1 冻结
/// 纪律"跨修订引用→拒绝"的产码面）。
inline constexpr std::string_view kDynInputInvalid = "DYN-INPUT-INVALID";
/// §9.4 行 2：DYN-UPSTREAM-TRAJECTORY-INCOMPATIBLE（error——上游轨迹
/// 身份/模型不兼容：轨迹 payload 绑定的 model.robot-design cv≠本快照
/// 闭包内 cv（§4.3 兼容性校验第 2 条）——拒绝评估，不以不兼容数据继续）。
inline constexpr std::string_view kDynUpstreamTrajectoryIncompatible
    = "DYN-UPSTREAM-TRAJECTORY-INCOMPATIBLE";
/// §9.4 行 3：DYN-TIME-PARAM-MISSING（error——轨迹缺时间参数：TimeParameterization
/// 不存在或 totalDurationS≤0（§4.3 第 2 条）——拒绝正式评估，不伪造
/// 节拍/功率/能量（§4.6"无时间参数"行））。
inline constexpr std::string_view kDynTimeParamMissing = "DYN-TIME-PARAM-MISSING";
/// §9.4 行 4：DYN-SERIES-NON-MONOTONIC（error——轨迹时间重复/倒退/零
/// 间隔（§4.3 第 2 条）：评估器不排序修复、不插值抹平——拒绝正式评估）。
inline constexpr std::string_view kDynSeriesNonMonotonic = "DYN-SERIES-NON-MONOTONIC";
/// §9.4 行 5：DYN-SAMPLE-GAP（warning，比较型——缺口数/计划数/1：缺样本/
/// 采样间隙，附首个缺口 t 与段（§4.6）；不插值补齐，Partial 不阻断该工况
/// 其余统计——逐段标注覆盖区间）。
inline constexpr std::string_view kDynSampleGap = "DYN-SAMPLE-GAP";
/// §9.4 行 6：DYN-NON-FINITE（error——非有限输入/输出，附样本定位：
/// NFR-COR-03"非有限数拒绝"，不静默转 0——该样本 NonFiniteInput/
/// NonFiniteOutput/Overflow 标记）。
inline constexpr std::string_view kDynNonFinite = "DYN-NON-FINITE";
/// §9.4 行 7：DYN-DIMENSION-MISMATCH（error，比较型——实际/期望/1：轨迹
/// 关节数≠模型关节数（§5.6 输入维度不匹配行）——评估终止）。
inline constexpr std::string_view kDynDimensionMismatch = "DYN-DIMENSION-MISMATCH";
/// §9.4 行 8：DYN-RNEA-FAILED（error——RNEA 计算失败，附 t/段/关节定位
/// （§5.6）：单工况失败不自动推出其他工况失败——其余工况继续）。
inline constexpr std::string_view kDynRneaFailed = "DYN-RNEA-FAILED";
/// §9.4 行 9：DYN-FD-INITIAL-STATE-MISSING（warning——正动力学初始状态
/// 缺失：建议证据项 dyn.forward-dynamics-consistency＝NotRun（§6.2）——
/// 建议项缺失不阻断，不伪造 Passed）。
inline constexpr std::string_view kDynFdInitialStateMissing = "DYN-FD-INITIAL-STATE-MISSING";
/// §9.4 行 10：DYN-FD-DIVERGED（warning——积分发散（DYN-05 异常检测本义，
/// §6.1 ④）：Failed 只进建议证据项 Invalid＋warning 诊断——正动力学失败
/// 不能直接判定模型无效（§6.3.5））。
inline constexpr std::string_view kDynFdDiverged = "DYN-FD-DIVERGED";
/// §9.4 行 11：DYN-FD-CONSISTENCY-FAILED（warning，比较型——实际误差/
/// 阈值/单位：正逆动力学一致性超阈值（§6.1 ④）；容差＝config.dyn.
/// forwardCheck 分析配置来源，测试对照容差另由黄金算例声明——附录 D C7
/// 两类容差分离）。
inline constexpr std::string_view kDynFdConsistencyFailed = "DYN-FD-CONSISTENCY-FAILED";
/// §9.4 行 12：DYN-PROPERTY-DOWNGRADED（warning——物性/负载估算降级
/// （DYN-06；附来源计数）：估算物性不得自动升级为精确动力学证据（§5.5
/// 三层来源第 2 层））。
inline constexpr std::string_view kDynPropertyDowngraded = "DYN-PROPERTY-DOWNGRADED";
/// §9.4 行 13：DYN-FRICTION-MISSING（warning——摩擦参数缺失（MDL-16 M-8
/// 闭环→DataInsufficient 素材）：分项值按 0 计入但样本/序列标记
/// frictionMissing——数值继续、证据不包装精确（§5.5 三层来源第 1 层））。
inline constexpr std::string_view kDynFrictionMissing = "DYN-FRICTION-MISSING";
/// §9.4 行 14：DYN-CONDITION-REF-MISSING（error——工况引用缺失/空工况
/// 集合（§8.1）：可定位诊断＋保守处置——快照身份/覆盖矩阵缺失先行）。
inline constexpr std::string_view kDynConditionRefMissing = "DYN-CONDITION-REF-MISSING";
/// §9.4 行 15：DYN-COUPLING-GATE-REJECTED（error——耦合链防御拒绝
/// （§5.6）：快照携带 R2 才会出现的耦合结构标识而 MDL-21 未启用（跨版本
/// 快照）时拒绝评估——绝不静默按独立关节链计算、绝不对角化/准静态替代
/// ——DYN-04/M-12 红线的 dynamics 侧兜底）。
inline constexpr std::string_view kDynCouplingGateRejected = "DYN-COUPLING-GATE-REJECTED";
/// §9.4 行 16（WP-17-T05 增行——表尾追加，既有行不重排）：DYN-FD-NUMERIC-
/// ANOMALY（warning——正动力学数值异常：质量阵奇异/病态致线性求解失败
/// （刚性/全零物性链——§6.1 ④"数值异常（刚性/溢出）"分支；其引擎选型与
/// 异常判据随 WP-17-T05 落位实测锁定——P-DYN-7）；Failed 只进建议证据项
/// Invalid＋warning 诊断，不判定模型无效（§6.3.5）。本码原为卡面 §6.1/
/// §10.2 行文提及但 §9.4 v1 表未具名的码——按 DiagCodes.hpp T02 登记范围
/// 口径"先走单元卡 §9.4 增量修订、再表尾追加"执行，T05 批随单元卡 v0.5
/// 同步增行）。
inline constexpr std::string_view kDynFdNumericAnomaly = "DYN-FD-NUMERIC-ANOMALY";

// =====================================================================
// DYN-* 稳定诊断码描述符清单（§9.4 拟注册清单的物化——装配期注册进
// diagnostics StableCodeRegistry 的数据源；见文件头注"码值权威链"段）
// =====================================================================

/**
 * @brief 产出 dynamics §9.4 登记表全表（16 码）的稳定码描述符全集
 *        （契约 acceptance 2"DYN-* 域诊断码按 units/dynamics.md 登记表
 *        装配期注册"的执行面；后续消费任务增码只允许表尾追加，清单恒与
 *        卡面 §9.4 全表同长）。
 *
 * 逐字段登记口径（全部可追溯到卡面/diagnostics 卡；逐码分类与动作族的
 * 落值依据在 DiagCodes.cpp 逐码注释——diagnostics 卡"逐码落值依据，
 * 实现＝DiagCodes.cpp 内置表逐码注释"同款登记形态）：
 *   - 码值文本＝§9.4 表"码"列连字符串原样（不私定码值——P-IO-6 同款
 *     纪律；上方 16 个常量即唯一书写点）。
 *   - ownerUnit＝"dynamics"（§4.5 前缀-所有权表 DYN→dynamics；首段
 *     "DYN"与所有者声明域一致——注册期校验可通过）。
 *   - severity＝§9.4 表"严重度"列逐行原值（全表 9 error＋7 warning——
 *     注册表对 Dev 强制三项 false，本表无 Dev 码）。
 *   - category＝diagnostics §4.3 词表 15 值内落值（P-DIAG-3 不新增词表
 *     值），逐码锚点：命中"输入/配置非法"→input-invalid；命中
 *     "DataInsufficient 轴（缺样本/摩擦缺失/估算降级/建议项缺失）"→
 *     data-insufficient；命中"outcome=Failed 轴（RNEA 失败/积分发散/
 *     一致性超限/数值异常）"→execution-failed；命中"schema/契约不兼容
 *     （跨版本快照耦合结构）"→format-or-version。
 *   - retryable＝§4.4 分类动作族机械映射（fix-input/supply-evidence/
 *     retry-task 族→UserRetry；本表无 report-bug 族码——RNEA 失败定位
 *     齐备后按重试语义归类 retry-task，与 kinematics SOLVER-INTERNAL 的
 *     Never 差异在于卡面 §5.6 明文"单工况失败不推出其他工况失败"的
 *     可重试边界）。
 *   - paramSchema＝"[]"（无参数显式声明——diagnostics 卡展开登记纪律
 *     "各单元产码路径落地时按需增量登记并升 registryVersion，不私造
 *     参数名"；比较型三要素走 requiresComparison/实例 comparison 面，
 *     不占参数名）。
 *   - confirmable＝false（16 码均无"策略校验超限待用户显式确认"语义
 *     ——SA-15 确认流属 policy/modeling 域码；故 requiresComparison 的
 *     强制前件不触发）。requiresComparison 仅三码 true——§9.4"比较型"
 *     列点名：DYN-SAMPLE-GAP（缺口数/计划数/1）、DYN-DIMENSION-MISMATCH
 *     （实际/期望/1）、DYN-FD-CONSISTENCY-FAILED（实际误差/阈值/单位）。
 *   - titleKey/detailKey＝命名约定 diag.<code-lower>.title/.detail
 *     （P-DIAG-9 键/值分离：键体系冻结，文案值归 ui/文案资源——注册期
 *     键形校验强制此约定）。
 *   - userVisible/reportable/historical＝true（全部为用户级码——§4.5
 *     仅对 Dev 码强制三项 false）。
 *   - registryVersion＝1（首次登记）；deprecated＝false、supersededBy＝
 *     nullopt（§4.5.1 tombstone 仅废弃时置位）。
 *
 * @return 描述符清单（顺序＝§9.4 表行序——确定性；每次调用返回新值）
 */
std::vector<diagnostics::CodeDescriptor> dynamicsCodeDescriptors();

/**
 * @brief 将 §9.4 登记表全表（16 码）注册进稳定码注册表（卡 §9.4"注册
 *        协议：装配期 ownerUnit="dynamics" 注册"的执行面——L5 装配清单
 *        的调用入口；本函数不改动 diagnostics 单元文件，注册权威仍在
 *        StableCodeRegistry）。
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
void registerDynamicsCodes(diagnostics::IDiagnosticRegistry& registry);

}  // namespace sdurws::ird::dynamics

#endif  // IRD_DYNAMICS_DIAGCODES_HPP
