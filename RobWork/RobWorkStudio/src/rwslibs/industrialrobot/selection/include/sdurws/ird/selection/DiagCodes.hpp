/**
 * @file   DiagCodes.hpp
 * @brief  selection 稳定诊断码登记表——SEL-* 码值常量（45 码：T02 批
 *         17 码〔卡 §2.2 范围外 1 码＋§5.3 目录业务校验 8 码＋§6.2 插值
 *         外推 1 码＋§6.3 曲线校验 4 码〔其中 REF-DANGLING 与 §5.3 同码〕
 *         ＋§9.3 组合校核 3 码〔IDENTITY-MISMATCH 两行同码〕〕＋T06 批
 *         表尾追加 28 码〔§10.3 淘汰原因词表的逐 token 稳定码建议值〕）
 *         与登记清单函数、ReasonToken→稳定码唯一映射函数。
 *
 * 设计依据：
 *   - units/selection.md §2.2（R1 目标链含移动关节的纪律——"范围外"
 *     诊断 SEL-INPUT-AXIS-OUT-OF-SCOPE）、§5.3（目录业务校验清单——
 *     8 码逐行触发条件）、§6.2（默认禁止外推——EXTRAPOLATION-DENIED）、
 *     §6.3（曲线校验表——4 码）、§9.3（组合校核清单——兼容/轴映射/
 *     身份一致性 3 码）；§1.3（诊断码 SEL-* 为建议值——码值分配与合法
 *     性权威＝diagnostics StableCodeRegistry）
 *   - units/core.md §4.8（DiagCode 句法 ^[A-Z0-9]+(-[A-Z0-9]+)*$ 且 ≤64
 *     ——core 仅承载；CR-08 码值权威在 diagnostics StableCodeRegistry）
 *     ＋core/DiagData.hpp（core::DiagCode＝std::string token、
 *     DiagnosticRecord 产码承载）
 *   - units/diagnostics.md §4.5（命名空间约定：首段＝单元短前缀，业务域
 *     词表含 SEL——前缀已在册，无补登事项）＋diagnostics/src/
 *     DiagCodes.cpp kPrefixOwners（{"SEL","selection"} 行——前缀-所有权
 *     表实测在册，2026-10-06）
 *   - 需求 ERR-01（稳定诊断码＋比较型字段齐备）、NFR-MNT-03（码/文案
 *     单一权威）、NFR-COR-03（非有限/非法单位/引用缺失不静默通过——
 *     目录与曲线校验码的依据）、SEL-01～05/09（各码对应需求条目）
 *   - 任务契约 tasks/foundation/WP-19-T02.json acceptance 2（"SEL-* 域
 *     诊断码按 units/selection.md 登记表装配期注册"）
 *
 * ★ 登记范围口径（T02 与 T06 的分界——T06 落位后的闭合登记）：T02 批
 *   物化了卡面**已具名**的 17 个具体码值；§10.3 淘汰原因词表（ReasonToken
 *   封闭词表）的逐 token 稳定码映射（SEL-MOTOR- 与 SEL-GEARBOX- 前缀的
 *   候选淘汰族等）已随 WP-19-T06（可行集与淘汰原因输出）表尾追加 28 码
 *   ——码值构造规则见下方 T06 批常量区块头注，映射唯一实现点＝
 *   reasonTokenDiagCode()（词表 token→稳定码；FeasibleSet.hpp 的
 *   RejectionReasonProvider 与组装器回填 diagRef 共用）。
 *
 * 背景说明（码值权威链——为什么 selection 只产出"登记表"不产出"注册
 * 函数"）：稳定码的注册表唯一权威＝diagnostics::StableCodeRegistry
 * （PA-1/NFR-MNT-03），码值文本的合法性权威＝units/selection.md 各节
 * 登记值（建议值——卡 §1.3；T06 批 28 码为卡 §10.3"建议值随 T06 注册"
 * 的落位面，构造规则见 T06 批区块头注）。本头把登记值物化为
 * 码值常量（inline constexpr string_view——T03+ 产码路径与 L5 装配共用
 * 唯一书写点）与登记行清单（码值＋卡面出处＋语义原文）。装配期注册的
 * 执行面与 kinematics（registerKinematicsCodes）不同，差异来自依赖白
 * 名单的硬约束（诚实登记，非遗漏）：
 *   1. selection 依赖白名单仅 core＋evidence 编译边（卡 §3.2 边表——
 *      policy/io/project/diagnostics/ui/runtime/drivetrain 七单元
 *      全部列在"运行时注入/端口"列、明文"不落编译链接边"）——本头不可
 *      include diagnostics::CodeDescriptor/IDiagnosticRegistry，登记行
 *      以纯 std 类型承载（drivetrain WP-18-T02 同款形态先例）；
 *   2. SEL 前缀本身**已在册**（与 DT 前缀需 P-DT-7 补登不同）：卡 §1.3
 *      明文"SEL 前缀已在 diagnostics.md §4.5 业务域命名空间清单登记，
 *      无补登事项"，且注册表前缀-所有权表（kPrefixOwners）实测含
 *      {"SEL","selection"} 行——真实 StableCodeRegistry 注册不存在前缀
 *      阻塞，仅是执行面位置问题；
 *   3. 因此本单元的"装配期注册"落地为：登记表物化（本头/本实现）＋L5
 *      装配期的唯一数据源契约——装配清单在 selection 域接入时，按本
 *      登记表逐行构造 CodeDescriptor 注册进 StableCodeRegistry（码值/
 *      出处/语义三列即卡面登记值的完备承载；级别/分类/paramSchema 等
 *      diagnostics 词表字段的落值裁决随收编动作由 diagnostics 所有者按
 *      其卡 §4.3/§4.4 词表执行——卡面未逐码给级别列，本实现不私造，
 *      NFR-MNT-03 单一权威纪律）。
 *
 * 线程安全：selectionCodeEntries() 纯函数（可重入）；码值常量
 * constexpr（编译期常量，无共享可变状态）。确定性：清单序＝卡面章节序
 * （§2.2 → §5.3 → §6.2 → §6.3 → §9.3，同节按表行序——确定性序，
 * NFR-COR-02），T06 批 28 码表尾追加（批内序＝ReasonToken 词表组序），
 * 后续任务追加码只允许表尾追加、既有行不重排（清单序
 * 进入登记契约——kinematics §9.6 行序纪律同款）。
 */

#ifndef IRD_SELECTION_DIAGCODES_HPP
#define IRD_SELECTION_DIAGCODES_HPP

#include <string_view>
#include <vector>

#include <sdurws/ird/selection/Screening.hpp>  // ReasonToken——词表→稳定码映射的键面

namespace sdurws::ird::selection {

// =====================================================================
// SEL-* 码值常量（唯一书写点——DiagCodes.cpp 登记清单与 WP-19-T03+
// 产码路径共用同一常量，禁字符串拼码/第二处字面量；modeling/requirements/
// kinematics/drivetrain 码值常量纪律的 SEL 侧同款）。
// 清单序＝卡面章节序（§2.2 → §5.3 → §6.2 → §6.3 → §9.3）——登记契约序，
// 追加只允许表尾（见文件头注"确定性"段）。
// =====================================================================

// ---- §2.2 R1 范围纪律（目标链含移动关节时）----

/// §2.2：SEL-INPUT-AXIS-OUT-OF-SCOPE（移动关节轴范围外——R1 只支持
/// 旋转传动〔电机＋减速器〕；该轴输出"范围外"诊断并按 DataInsufficient
/// 语义处理；不静默套用旋转传动、不静默把移动关节转成旋转关节、不伪造
/// 电机工作点、不将该结果直接升级为整机工程不可行——判定权在 evidence）。
inline constexpr std::string_view kSelInputAxisOutOfScope = "SEL-INPUT-AXIS-OUT-OF-SCOPE";

// ---- §5.3 目录业务校验（导入期；逐项可定位到文件/行/列）----

/// §5.3 行 1：SEL-CATALOG-SCHEMA-MISMATCH（字段字典不完整——manifest
/// 声明列与 CSV 表头不一致；多列/缺列逐列定位；未知 formatVersion 的
/// 拒绝另按卡 §5.2 走升级指引面）。
inline constexpr std::string_view kSelCatalogSchemaMismatch = "SEL-CATALOG-SCHEMA-MISMATCH";
/// §5.3 行 2：SEL-CATALOG-UNIT-INVALID（单位非法——单位词表＋量纲检查
/// 不通过；未知单位拒绝不猜测；比较型：实际/期望/单位——ERR-01；
/// 换算唯一经 core Units，本域不自换算）。
inline constexpr std::string_view kSelCatalogUnitInvalid = "SEL-CATALOG-UNIT-INVALID";
/// §5.3 行 3：SEL-CATALOG-FIELD-MISSING（必填字段缺失——逐字段定位；
/// 需求允许缺失的字段显式入条目 missing 清单、不伪造数值——ERR-01，
/// 与本码分轨）。
inline constexpr std::string_view kSelCatalogFieldMissing = "SEL-CATALOG-FIELD-MISSING";
/// §5.3 行 4：SEL-CATALOG-DUPLICATE-MODEL（同稳定 ID 的重复型号条目——
/// 同 modelId 多行；显示名重复但 ID 不同＝合法〔卡 §5.3 验证矩阵登记〕，
/// 不落本码）。
inline constexpr std::string_view kSelCatalogDuplicateModel = "SEL-CATALOG-DUPLICATE-MODEL";
/// §5.3 行 4：SEL-CATALOG-DUPLICATE-ID（稳定 ID 重复——型号主表主键
/// 唯一性破坏；(catalogId, version, modelId) 三元组身份前提失效）。
inline constexpr std::string_view kSelCatalogDuplicateId = "SEL-CATALOG-DUPLICATE-ID";
/// §5.3 行 5：SEL-CATALOG-RANGE-INVALID（数值范围非法——转矩>0 N·m、
/// 效率∈(0,1] 无量纲、速比>0、寿命>0 等；非有限数〔NaN/±Inf〕同路径
/// ——NFR-COR-03；比较型：实际值/期望范围/单位）。
inline constexpr std::string_view kSelCatalogRangeInvalid = "SEL-CATALOG-RANGE-INVALID";
/// §5.3 行 6（§6.3 曲线缺失行同码）：SEL-CATALOG-REF-DANGLING（文件间
/// 引用语义悬空——curve_ref 指向的曲线不存在或 owner 不匹配、
/// compatibility 引用的电机/减速器型号不存在；文件层存在性由 io 先行，
/// 语义层〔owner 匹配〕归本卡——卡 §5.3 双层校验分工）。
inline constexpr std::string_view kSelCatalogRefDangling = "SEL-CATALOG-REF-DANGLING";
/// §5.3 行 7：SEL-CATALOG-COMPAT-CONFLICT（兼容关系冲突——同型号对
/// 多行且 mount_kind 安装关系矛盾）。
inline constexpr std::string_view kSelCatalogCompatConflict = "SEL-CATALOG-COMPAT-CONFLICT";

// ---- §6.2 插值与外推边界 ----

/// §6.2：SEL-CURVE-EXTRAPOLATION-DENIED（默认禁止外推——查询点落在
/// 曲线 [x_min, x_max] 闭区间之外即拒绝；不自动使用最近点、不静默外推；
/// 比较型：实际输入点/有效区间/单位。插值失败≠候选能力不足——按数据
/// 不足类标记分轨，卡 §6.2）。
inline constexpr std::string_view kSelCurveExtrapolationDenied = "SEL-CURVE-EXTRAPOLATION-DENIED";

// ---- §6.3 曲线校验（导入期）----

/// §6.3 行 1：SEL-CURVE-UNORDERED（采样点无序——未按 x 升序提交即拒绝；
/// 构造入口**不代排序**——排序会掩盖目录错误，要求目录修正）。
inline constexpr std::string_view kSelCurveUnordered = "SEL-CURVE-UNORDERED";
/// §6.3 行 2：SEL-CURVE-DUP-X（重复横坐标——同 x 不同 y 的点对存在，
/// 插值语义歧义）。
inline constexpr std::string_view kSelCurveDupX = "SEL-CURVE-DUP-X";
/// §6.3 行 3：SEL-CURVE-NONFINITE（曲线点非有限——NaN/±Inf 任一点；
/// NFR-COR-03"非有限即拒绝"）。
inline constexpr std::string_view kSelCurveNonfinite = "SEL-CURVE-NONFINITE";
/// §6.3 行 4：SEL-CURVE-INTERVAL-INVALID（区间不合法——x_max ≤ x_min、
/// 或单点曲线声明为曲线；单点能力值应走"固定额定值"口径而非曲线形态，
/// 卡 §6.4）。
inline constexpr std::string_view kSelCurveIntervalInvalid = "SEL-CURVE-INTERVAL-INVALID";

// ---- §9.3 组合校核（SEL-05 全覆盖清单）----

/// §9.3 行 1：SEL-COMBO-INCOMPATIBLE（电机—减速器组合不兼容——
/// compatibility 表无该型号对记录即不兼容；零行语义＝包内无预声明
/// 兼容对，卡 §5.2）。
inline constexpr std::string_view kSelComboIncompatible = "SEL-COMBO-INCOMPATIBLE";
/// §9.3 行 2：SEL-COMBO-AXIS-MAPPING-INCOMPLETE（轴映射不完整——
/// 每轴恰一组合的前提破坏；漏轴即本码，不多轴组合兜底）。
inline constexpr std::string_view kSelComboAxisMappingIncomplete = "SEL-COMBO-AXIS-MAPPING-INCOMPLETE";
/// §9.3 行 11/12：SEL-IDENTITY-MISMATCH（身份不一致——组合校核所用
/// 目录版本≠映射批候选参数来源版本，或 drivetrain 映射 Facts 的契约/
/// 算法版本与切片声明不一致；拒绝评估，不以版本不符的数据继续计算
/// ——AT-38 三方同口径纪律）。
inline constexpr std::string_view kSelIdentityMismatch = "SEL-IDENTITY-MISMATCH";

// =====================================================================
// T06 批（WP-19-T06）：§10.3 淘汰原因词表的逐 token 稳定码建议值——
// 28 码表尾追加（登记行见 selectionCodeEntries() 表尾 T06 区块）。
//
// 码值构造规则（登记于单元卡 §19.3 T06 落位细化）：电机维度 token 一律
// SEL-MOTOR-<维度 kebab 大写>、减速器维度 token 一律 SEL-GEARBOX-<…>；
// 电机/减速器共用的安装 token 取无器件前缀的 SEL-MOUNTING-INCOMPATIBLE
// （卡 §10.3 词表中 mounting-incompatible 位于减速器组但无 gearbox- 前缀
// ——T04 落位细化 ③ 已登记其为共用 token，码值同样共用）；已有卡面
// 具名码的 token 复用既有常量不新造同义码（ComboIncompatible/
// AxisMappingIncomplete/IdentityMismatch 三 token 及目录/映射版本两
// token→SEL-IDENTITY-MISMATCH〔卡 §9.3 行 10/11 同码〕、AxisOutOfScope→
// SEL-INPUT-AXIS-OUT-OF-SCOPE〔§2.2〕）；其余 token 按语义命名。全部码值
// 经 core §4.8 句法（^[A-Z0-9]+(-[A-Z0-9]+)*$ 且 ≤64）由 DiagCodesTest
// 与 FeasibleSetContractTest 双面校验。
//
// 清单序纪律不变：T06 批 28 行全部位于既有 17 行之后（表尾追加），批内
// 序＝ReasonToken 词表组序（电机→减速器→组合/一致性→上游/数据→边界/
// 偏好），既有行不重排。
// =====================================================================

// ---- 电机能力组（§10.3 行 1——11 token→11 码；SEL-MOTOR- 族）----

/// torque-continuous-insufficient：连续转矩不足（工作点 τ_rms ＞ 额定连续转矩）。
inline constexpr std::string_view kSelMotorTorqueContinuousInsufficient =
    "SEL-MOTOR-TORQUE-CONTINUOUS-INSUFFICIENT";
/// torque-peak-insufficient：峰值转矩不足（工作点 τ_peak ＞ 峰值转矩）。
inline constexpr std::string_view kSelMotorTorquePeakInsufficient =
    "SEL-MOTOR-TORQUE-PEAK-INSUFFICIENT";
/// speed-insufficient：转速不足（ω_peak ＞ 最高转速 或 ω_rms ＞ 额定转速）。
inline constexpr std::string_view kSelMotorSpeedInsufficient =
    "SEL-MOTOR-SPEED-INSUFFICIENT";
/// power-insufficient：功率不足（P_peak/P_rms ＞ 功率能力）。
inline constexpr std::string_view kSelMotorPowerInsufficient =
    "SEL-MOTOR-POWER-INSUFFICIENT";
/// overload-time-insufficient：过载持续时间不足（峰值段时长 ＞ 目录允许时长）。
inline constexpr std::string_view kSelMotorOverloadTimeInsufficient =
    "SEL-MOTOR-OVERLOAD-TIME-INSUFFICIENT";
/// duty-mismatch：工作制不匹配（需求工作制 ∉ 目录工作制）。
inline constexpr std::string_view kSelMotorDutyMismatch = "SEL-MOTOR-DUTY-MISMATCH";
/// voltage-mismatch：电压不匹配（需求电压 ≠ 目录额定电压容差内）。
inline constexpr std::string_view kSelMotorVoltageMismatch = "SEL-MOTOR-VOLTAGE-MISMATCH";
/// thermal-derating-insufficient：温度降额复判不足（折减能力 ＜ 工作点）。
inline constexpr std::string_view kSelMotorThermalDeratingInsufficient =
    "SEL-MOTOR-THERMAL-DERATING-INSUFFICIENT";
/// brake-insufficient：制动能力不足（保持需求 ＞ 制动转矩）。
inline constexpr std::string_view kSelMotorBrakeInsufficient =
    "SEL-MOTOR-BRAKE-INSUFFICIENT";
/// holding-insufficient：保持能力不足（保持需求 ＞ 保持能力）。
inline constexpr std::string_view kSelMotorHoldingInsufficient =
    "SEL-MOTOR-HOLDING-INSUFFICIENT";
/// safety-factor-insufficient：安全系数复判不足（工作点×SF ＞ 能力值）。
inline constexpr std::string_view kSelMotorSafetyFactorInsufficient =
    "SEL-MOTOR-SAFETY-FACTOR-INSUFFICIENT";

// ---- 减速器能力组（§10.3 行 2——9 token→9 码；SEL-GEARBOX- 族＋共用
//      安装码 SEL-MOUNTING-INCOMPATIBLE）----

/// gearbox-rated-torque-insufficient：减速器额定输出转矩不足（关节 τ_rms ＞ 额定）。
inline constexpr std::string_view kSelGearboxRatedTorqueInsufficient =
    "SEL-GEARBOX-RATED-TORQUE-INSUFFICIENT";
/// gearbox-peak-torque-insufficient：减速器峰值输出转矩不足（关节 τ_peak ＞ 峰值）。
inline constexpr std::string_view kSelGearboxPeakTorqueInsufficient =
    "SEL-GEARBOX-PEAK-TORQUE-INSUFFICIENT";
/// input-speed-exceeded：输入转速超限（ω_m_peak ＞ 允许输入转速）。
inline constexpr std::string_view kSelGearboxInputSpeedExceeded =
    "SEL-GEARBOX-INPUT-SPEED-EXCEEDED";
/// ratio-mismatch：速比不匹配（候选速比 ∉ 该轴允许传动比范围）。
inline constexpr std::string_view kSelGearboxRatioMismatch =
    "SEL-GEARBOX-RATIO-MISMATCH";
/// efficiency-insufficient：效率不足（目录效率 ＜ 筛选条件最低效率）。
inline constexpr std::string_view kSelGearboxEfficiencyInsufficient =
    "SEL-GEARBOX-EFFICIENCY-INSUFFICIENT";
/// backlash-exceeded：回程间隙超限（目录回隙 ＞ 筛选条件上限）。
inline constexpr std::string_view kSelGearboxBacklashExceeded =
    "SEL-GEARBOX-BACKLASH-EXCEEDED";
/// life-insufficient：寿命不足（目录额定寿命 ＜ 筛选条件要求）。
inline constexpr std::string_view kSelGearboxLifeInsufficient =
    "SEL-GEARBOX-LIFE-INSUFFICIENT";
/// mounting-incompatible：安装不兼容（电机/减速器共用 token——T04 落位
/// 细化 ③；码值同用无器件前缀形态）。
inline constexpr std::string_view kSelMountingIncompatible =
    "SEL-MOUNTING-INCOMPATIBLE";
/// external-load-exceeded：允许外载荷超限（实际外载荷 ＞ 力臂核算后允许值）。
inline constexpr std::string_view kSelGearboxExternalLoadExceeded =
    "SEL-GEARBOX-EXTERNAL-LOAD-EXCEEDED";

// ---- 组合/一致性组新增码（§10.3 行 3——InertiaRatioPolicyUnsettled
//      一码；其余 5 token 复用上方 §9.3 三码）----

/// inertia-ratio-policy-unsettled：惯量比策略未裁决（O-11——显式"未判定"
/// 标记的稳定引用面；非淘汰码——呈现/追溯用，不进入淘汰统计）。
inline constexpr std::string_view kSelInertiaRatioPolicyUnsettled =
    "SEL-INERTIA-RATIO-POLICY-UNSETTLED";

// ---- 上游/数据组（§10.3 行 4——5 token→5 码；与候选淘汰分轨）----

/// dynamics-missing：dynamics 结果缺失（上游缺失——数据不足分轨）。
inline constexpr std::string_view kSelDynamicsMissing = "SEL-DYNAMICS-MISSING";
/// drivetrain-missing：drivetrain 映射缺失/失败（上游失败透传面——
/// 映射失败≠器件能力不足，卡 §10.2 空集语义表）。
inline constexpr std::string_view kSelDrivetrainMissing = "SEL-DRIVETRAIN-MISSING";
/// case-coverage-gap：工况覆盖缺口（EVI-02 素材面——DataInsufficient 语义）。
inline constexpr std::string_view kSelCaseCoverageGap = "SEL-CASE-COVERAGE-GAP";
/// input-invalid：输入非法（校验边界拒绝类——与候选能力淘汰分轨）。
inline constexpr std::string_view kSelInputInvalid = "SEL-INPUT-INVALID";
/// compute-failed：计算失败（与候选淘汰分开、不进入淘汰原因统计——§10.3）。
inline constexpr std::string_view kSelComputeFailed = "SEL-COMPUTE-FAILED";

// ---- 边界/偏好组（§10.3 行 5——R2CapabilityDisabled/UserPreferenceFiltered
//      两码；AxisOutOfScope 复用上方 §2.2 码）----

/// r2-capability-disabled：R2 能力未启用（直线传动等——阶段 D 前显式拒绝）。
inline constexpr std::string_view kSelR2CapabilityDisabled =
    "SEL-R2-CAPABILITY-DISABLED";
/// user-preference-filtered：用户优选过滤（偏好呈现结果——与硬能力失败
/// 分离，非工程结论，卡 §10.2 空集语义表）。
inline constexpr std::string_view kSelUserPreferenceFiltered =
    "SEL-USER-PREFERENCE-FILTERED";

// =====================================================================
// ReasonToken → 稳定码唯一映射（§10.3"SEL-* 稳定码建议值随 WP-19-T06
// 注册"的执行点——词表 34 token 全表映射；RejectionReasonProvider.make
// 回填 diagRef 与 FeasibleSetBuilder 输出回填共用本函数，禁第二处映射）
// =====================================================================

/**
 * @brief 淘汰原因 token → SEL-* 稳定码（唯一映射点——见上方区块头注）。
 *
 * @param token [in] 淘汰原因 token（ReasonToken 封闭词表）
 * @return 稳定码文本（DiagCodes.hpp 常量——既有码复用不新造；T06 批新码
 *         按构造规则命名）；token 越界（词表外整数值）返回空串（防御分支
 *         ——调用方以空串判"无码可引"，diagRef 保持 nullopt，不私造码值）
 *
 * @note 纯函数；确定性（NFR-COR-02）。全表 34 token 的映射封闭性由
 *       FeasibleSetContractTest 全遍历钉住（逐 token 非空＋句法权威校验）。
 */
std::string_view reasonTokenDiagCode(ReasonToken token);

// =====================================================================
// SEL-* 登记行与全表清单（units/selection.md 登记值的物化——L5 装配期
// 注册进 diagnostics StableCodeRegistry 的数据源；见文件头注"码值权威
// 链"段）
// =====================================================================

/**
 * @brief 单码登记行（卡面登记值的三列承载——纯值聚合，零外部依赖）。
 *
 * 字段取舍口径（诚实边界）：selection.md 的 SEL-* 登记值散布于 §2.2/
 * §5.3/§6.2/§6.3/§9.3 各节，卡面给出并经本表物化的只有三列——码值、
 * 出处（章节锚点）、语义（卡面原文压缩）。diagnostics 词表字段（级别/
 * 分类/paramSchema/重试族等）卡面未逐码给出，本表不承载（不私造登记值
 * ——NFR-MNT-03 单一权威；落值裁决随 L5 装配收编动作由 diagnostics
 * 所有者按其卡 §4.3/§4.4 词表执行）。
 */
struct DiagnosticEntry {
    std::string_view code;          ///< 码值（上方常量——唯一书写点引用）
    std::string_view sourceClause;  ///< 卡面登记出处（units/selection.md §x.y 锚点）
    std::string_view semantics;     ///< 卡面语义（触发条件/处置口径的一句话登记）
};

/**
 * @brief 产出 SEL-* 全表（45 码＝T02 批 17＋T06 批表尾追加 28）的登记行
 *        清单（units/selection.md 登记表的物化——装配期注册进 diagnostics
 *        StableCodeRegistry 的数据源；WP-19-T03+ 产码路径的码值语义对照面）。
 *
 * 清单序＝卡面章节序（§2.2 一码 → §5.3 表行序八码〔REF-DANGLING 兼并
 * §6.3 曲线缺失行〕→ §6.2 一码 → §6.3 表行序四码 → §9.3 表行序三码
 * 〔IDENTITY-MISMATCH 兼并行 12〕→ T06 批 28 码〔批内序＝ReasonToken
 * 词表组序：电机 11→减速器 9→组合/一致性 1→上游/数据 5→边界/偏好 2〕
 * ——确定性序，NFR-COR-02）；每次调用
 * 返回同序同值新清单（纯值聚合）。追加纪律：后续任务新增码只允许表尾
 * 追加并走单元卡增量修订（kinematics §9.6 行序纪律同款）——既有行
 * 不重排。
 *
 * @return 登记行清单（顺序＝登记契约序；调用方持有）
 */
std::vector<DiagnosticEntry> selectionCodeEntries();

}  // namespace sdurws::ird::selection

#endif  // IRD_SELECTION_DIAGCODES_HPP
