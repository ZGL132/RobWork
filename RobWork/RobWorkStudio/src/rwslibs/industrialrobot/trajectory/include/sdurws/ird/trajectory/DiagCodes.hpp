/**
 * @file   DiagCodes.hpp
 * @brief  trajectory 稳定诊断码工厂——TRJ-* 码的 CodeDescriptor 登记数据
 *         （units/trajectory.md §14.4 拟注册清单全表 12 码）与装配注册
 *         函数（WP-16-T03 构建落位随批登记）。
 *
 * 设计依据：
 *   - units/trajectory.md §14.4（diagnostics 协作——TRJ-* 拟注册清单 v1
 *     设计基线 12 码：码/严重度/比较型/用途四列；"码值权威归
 *     StableCodeRegistry，本表为拟注册清单——WP-16-T03/T09 注册时按协议
 *     提交，注册期校验/重复拒绝/manifest 握手均走 diagnostics 设施"）、
 *     §14.5.1（领域命令零修订——TRJ-07 的会话命令族；导出失败的转译面）、
 *     §12.6（超限与失败定位——时间侧产码路径）、§11.3（复检执行要点
 *     ——预算耗尽/验证器缺失两码的触发条件）、§9.2（连续性破坏诊断
 *     示例）
 *   - units/diagnostics.md §4.3（DiagnosticCategory 15 值词表——P-DIAG-3
 *     实现承载）、§4.4（分类—严重—动作族矩阵——retryable 机械映射依据）、
 *     §4.5（注册协议——句法/前缀-所有权/键唯一/paramSchema/字段不变量；
 *     "不私造参数名"展开登记纪律）、§4.5 前缀-所有权表（TRJ→trajectory
 *     ——diagnostics/src/DiagCodes.cpp kPrefixOwners 实测在册行
 *     {"TRJ","trajectory"}，2026-10-06；注册无前缀阻塞）
 *   - 先例：kinematics/DiagCodes.hpp（WP-15-T02 同款形态——依赖白名单含
 *     diagnostics 编译边的单元产出 CodeDescriptor 清单＋注册函数；
 *     dynamics WP-17-T02 同因同款；selection/drivetrain 的"纯登记表、
 *     注册归 L5 装配"形态在 trajectory 不适用——本卡 §3.2 接口依赖含
 *     diagnostics 公共头，注册执行面可落本单元）
 *   - 需求 ERR-01（稳定诊断码＋比较型字段齐备）、TRJ-06（失败/超限定位
 *     ——本表的消费需求族）、NFR-MNT-03（码/文案单一权威）
 *   - 任务契约 tasks/foundation/WP-16-T03.json acceptance 3（"TRJ-* 域
 *     诊断码按 units/trajectory.md 登记表在装配期注册——diagnostics
 *     §4.5 协议"）
 *
 * ★ 登记范围口径（T03 与 T09 的分界——诚实登记，防扩大）：本表物化的是
 *   卡面 §14.4 登记表**已具名**的 12 个具体码值。T09（失败段诊断定位）
 *   落位时若需增码，先走本卡 §14.4 增量修订、再表尾追加（本头既有行不
 *   重排——清单序进入登记契约）。本任务不预建、不占位（NFR-MNT-04 不建
 *   无边界价值包装器）。
 *
 * 背景说明（码值权威链）：稳定码的注册表唯一权威＝
 * diagnostics::StableCodeRegistry（PA-1/NFR-MNT-03）；TRJ-* 码值的登记
 * 权威＝units/trajectory.md §14.4 表（卡面逐码给出严重度/比较型/用途）。
 * 本头把 §14.4 **全表**的登记值物化为 CodeDescriptor（逐字段可追溯到卡
 * 面与 diagnostics 词表），装配期由 L5 经 registerTrajectoryCodes 注册
 * ——本实现不改动 diagnostics 单元任何文件，不私定任何卡面之外的码值。
 * 与 kinematics/dynamics 的阶段纪律同款：诊断**实例**的产出（经
 * diagnostics 工厂）只能消费已注册码，生产者接口（§15 各接口与评估器
 * 的产码路径）随 WP-16-T04~T10 逐个落地；注册动作本身发生在装配期，
 * 开发期不向任何全局注册表注册。
 *
 * 线程安全：trajectoryCodeDescriptors() 纯函数（可重入）；
 * registerTrajectoryCodes 只转发 registry.registerCode（注册期单线程
 * 约定随 diagnostics 装配语义）。确定性：清单序＝§14.4 表行序（确定性
 * 序，NFR-COR-02），后续任务追加码只允许表尾追加、既有行不重排。
 */

#ifndef IRD_TRAJECTORY_DIAGCODES_HPP
#define IRD_TRAJECTORY_DIAGCODES_HPP

#include <string_view>
#include <vector>

#include <sdurws/ird/diagnostics/DiagCodes.hpp>  // diagnostics::CodeDescriptor/IDiagnosticRegistry——注册协议（§4.5）

namespace sdurws::ird::trajectory {

// =====================================================================
// TRJ-* 码值常量（唯一书写点——DiagCodes.cpp 描述符工厂与 WP-16-T04+
// 消费者产码共用同一常量，禁字符串拼码/第二处字面量；kinematics/
// dynamics/modeling 码值常量纪律的 TRJ 侧同款）
// =====================================================================

/// §14.4 行 1：TRJ-INPUT-INVALID（error——输入非法：序列环/悬空引用/
/// 配置非法/模式组合非法——对应汇总①级素材；§5.4 序列展开的环与悬空
/// 引用拒收面）。
inline constexpr std::string_view kTrjInputInvalid = "TRJ-INPUT-INVALID";
/// §14.4 行 2：TRJ-NO-PATH（error——无路径：IK 无解/规划失败段定位；
/// §7.5 候选解选择无果与 §10 规划失败的段级定位素材）。
inline constexpr std::string_view kTrjNoPath = "TRJ-NO-PATH";
/// §14.4 行 3：TRJ-BRANCH-JUMP（warning——关节分支跳变，附采样点 s 与
/// 两端解摘要；§8.3 直线采样 IK 分支连续性的跳变记录面）。
inline constexpr std::string_view kTrjBranchJump = "TRJ-BRANCH-JUMP";
/// §14.4 行 4：TRJ-SINGULAR-NEIGHBORHOOD（warning，比较型——条件数/阈值
/// /1）：奇异邻域（§8.5；阈值缺失→notApplicable 显式标记——不私设
/// 数值，D-TRJ-5 同源纪律）。
inline constexpr std::string_view kTrjSingularNeighborhood
    = "TRJ-SINGULAR-NEIGHBORHOOD";
/// §14.4 行 5：TRJ-LIMIT-EXCEEDED（error，比较型——实际/期望/单位）：
/// 速度/加速度/限位超标（TRJ-06 限制超标；§12.2 限值校验唯一判定点的
/// 产码面）。
inline constexpr std::string_view kTrjLimitExceeded = "TRJ-LIMIT-EXCEEDED";
/// §14.4 行 6：TRJ-CONTINUITY-BROKEN（error，比较型——实际差/容差/单位）：
/// 位置/姿态/速度/加速度连续性破坏（§9.2 诊断示例；§9.1"至少加速度
/// 连续"R1 验收锚点的破坏记录）。
inline constexpr std::string_view kTrjContinuityBroken = "TRJ-CONTINUITY-BROKEN";
/// §14.4 行 7：TRJ-RECHECK-COLLISION（error——平滑后重新碰撞，段定位＋
/// 对象对；§11.1 复检强制边"只要几何或采样发生变化就重新复检"的检出
/// 记录——不静默放行）。
inline constexpr std::string_view kTrjRecheckCollision = "TRJ-RECHECK-COLLISION";
/// §14.4 行 8：TRJ-RECHECK-BUDGET-EXHAUSTED（error，比较型——实际步长/
/// 预算）：细分预算耗尽（R9：附实际最大步长与预算占用——§11.3；段级
/// DataInsufficient 素材的定位码，不默认接受）。
inline constexpr std::string_view kTrjRecheckBudgetExhausted
    = "TRJ-RECHECK-BUDGET-EXHAUSTED";
/// §14.4 行 9：TRJ-RECHECK-DATA-INSUFFICIENT（error——验证器/碰撞证据
/// 缺失（KIN-05 口径）——§11.2；不得视为无碰撞，V-20 反例锚点）。
inline constexpr std::string_view kTrjRecheckDataInsufficient
    = "TRJ-RECHECK-DATA-INSUFFICIENT";
/// §14.4 行 10：TRJ-TIME-PARAM-FAILED（error——时间参数化失败，含限值
/// 未定义情形；§12.2/§12.6——节拍 NotProvided 不伪造，V-18 锚点）。
inline constexpr std::string_view kTrjTimeParamFailed = "TRJ-TIME-PARAM-FAILED";
/// §14.4 行 11：TRJ-CYCLE-TIME-EXCEEDED（warning，比较型——实际节拍/
/// 目标/s）：目标节拍超标（Should 口径——§12.4 节拍输出；不阻断的
/// 工程裕量素材）。
inline constexpr std::string_view kTrjCycleTimeExceeded
    = "TRJ-CYCLE-TIME-EXCEEDED";
/// §14.4 行 12：TRJ-EXPORT-FAILED（error——标准轨迹导出失败（io 通道
/// 错误转译；先前输出完整保留语义随 io——§15.13/§14.5.1 导出命令）。
inline constexpr std::string_view kTrjExportFailed = "TRJ-EXPORT-FAILED";

// =====================================================================
// TRJ-* 稳定诊断码描述符清单（§14.4 拟注册清单的物化——装配期注册进
// diagnostics StableCodeRegistry 的数据源；见文件头注"码值权威链"段）
// =====================================================================

/**
 * @brief 产出 trajectory §14.4 登记表全表（12 码）的稳定码描述符全集
 *        （契约 acceptance 3"TRJ-* 域诊断码按 units/trajectory.md 登记
 *        表在装配期注册"的执行面；后续消费任务增码只允许表尾追加，清单
 *        恒与卡面 §14.4 全表同长）。
 *
 * 逐字段登记口径（全部可追溯到卡面/diagnostics 卡；逐码分类与动作族的
 * 落值依据在 DiagCodes.cpp 逐码注释——diagnostics 卡"逐码落值依据，
 * 实现＝DiagCodes.cpp 内置表逐码注释"同款登记形态）：
 *   - 码值文本＝§14.4 表"码"列连字符串原样（不私定码值——P-IO-6 同款
 *     纪律；上方 12 个常量即唯一书写点）。
 *   - ownerUnit＝"trajectory"（§4.5 前缀-所有权表 TRJ→trajectory；首段
 *     "TRJ"与所有者声明域一致——注册期校验可通过）。
 *   - severity＝§14.4 表"严重度"列逐行原值（全表 9 error＋3 warning——
 *     注册表对 Dev 强制三项 false，本表无 Dev 码）。
 *   - category＝diagnostics §4.3 词表 15 值内落值（P-DIAG-3 不新增词表
 *     值），逐码锚点见 DiagCodes.cpp 调用点注释。
 *   - retryable＝§4.4 分类动作族机械映射（fix-input/supply-evidence/
 *     retry-task/adjust-policy 族→UserRetry；本表无 report-bug 族码）。
 *   - paramSchema＝"[]"（无参数显式声明——diagnostics 卡展开登记纪律
 *     "各单元产码路径落地时按需增量登记并升 registryVersion，不私造
 *     参数名"；比较型五要素走 requiresComparison/实例 comparison 面，
 *     不占参数名）。
 *   - confirmable＝false（12 码均无"策略校验超限待用户显式确认"语义
 *     ——SA-15 确认流属 policy/modeling 域码，R1 轨迹域无 Confirmable-
 *     Finding 产生点——卡 §14.4 末条明文；故 requiresComparison 的强制
 *     前件不触发）。requiresComparison 恰五码 true——§14.4"比较型"列
 *     点名：SINGULAR-NEIGHBORHOOD（条件数/阈值/1）、LIMIT-EXCEEDED
 *     （实际/期望/单位）、CONTINUITY-BROKEN（实际差/容差/单位）、
 *     RECHECK-BUDGET-EXHAUSTED（实际步长/预算）、CYCLE-TIME-EXCEEDED
 *     （实际节拍/目标/s）。
 *   - titleKey/detailKey＝命名约定 diag.<code-lower>.title/.detail
 *     （P-DIAG-9 键/值分离：键体系冻结，文案值归 ui/文案资源——注册期
 *     键形校验强制此约定）。
 *   - userVisible/reportable/historical＝true（全部为用户级码——§4.5
 *     仅对 Dev 码强制三项 false）。
 *   - registryVersion＝1（首次登记）；deprecated＝false、supersededBy＝
 *     nullopt（§4.5.1 tombstone 仅废弃时置位）。
 *
 * @return 描述符清单（顺序＝§14.4 表行序——确定性；每次调用返回新值）
 */
std::vector<diagnostics::CodeDescriptor> trajectoryCodeDescriptors();

/**
 * @brief 将 §14.4 登记表全表（12 码）注册进稳定码注册表（卡 §14.4 注册
 *        协议——"装配期以 ownerUnit='trajectory' 注册 TRJ-* CodeDescriptor"
 *        的执行面——L5 装配清单的调用入口；本函数不改动 diagnostics 单元
 *        文件，注册权威仍在 StableCodeRegistry）。
 *
 * 前置：registry 未 seal、不含同码/同文案键冲突登记（重复注册→注册表抛
 * DiagnosticsError(DuplicateCode)——不捕获不吞，装配期 fail-fast）。
 * 后置：本头清单内全部 12 码可被 registry.find 命中；manifest 反映全表。
 *
 * @param registry [in,out] 目标注册表（调用方持有——L5 装配的
 *                 diagnostics::StableCodeRegistry 实例；本函数不接管）
 * @throws diagnostics::DiagnosticsError 注册期验证失败（码值冲突/字段
 *         非法/前缀-所有权不一致/键形偏离——描述符由本单元按卡面产出，
 *         出现即实现缺陷，fail-fast）
 */
void registerTrajectoryCodes(diagnostics::IDiagnosticRegistry& registry);

}  // namespace sdurws::ird::trajectory

#endif  // IRD_TRAJECTORY_DIAGCODES_HPP
