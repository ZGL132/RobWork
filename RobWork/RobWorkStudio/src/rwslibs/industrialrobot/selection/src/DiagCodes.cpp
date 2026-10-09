/**
 * @file   DiagCodes.cpp
 * @brief  selection 稳定诊断码登记表的实现——SEL-* 全表 54 码登记行清单
 *         （逐码出处与语义登记；T02 批 17 码＋T06 批表尾追加 28 码＋T09
 *         批表尾追加 9 码）与 ReasonToken→稳定码唯一映射函数。
 *
 * 设计依据：
 *   - units/selection.md §2.2（移动关节范围外纪律）、§5.3（目录业务校验
 *     清单——逐行触发条件与比较型字段要求）、§6.2（默认禁止外推——不
 *     自动使用最近点）、§6.3（曲线校验表——拒绝优于排序掩盖）、§9.3
 *     （组合校核清单——兼容/轴映射/身份一致性；各码登记值与处置口径的
 *     唯一权威）、§10.3（淘汰原因词表——SEL-* 稳定码建议值随 WP-19-T06
 *     注册；token→码映射的唯一实现点在本文件）、§1.3（SEL-* 建议值；
 *     前缀已在 diagnostics §4.5 在册）
 *   - units/core.md §4.8（DiagCode 句法——core 仅承载；本清单码值经
 *     DiagCodesTest 与 FeasibleSetContractTest 以 core::DiagnosticRecord
 *     句法权威校验）
 *   - 先例：drivetrain/src/DiagCodes.cpp（依赖白名单无 diagnostics 编译
 *     边的登记表物化同款形态——kinematics/src/DiagCodes.cpp 的注册函数
 *     形态在此不适用，差异论证见 DiagCodes.hpp 文件头注）
 *   - 任务契约 tasks/foundation/WP-19-T02.json acceptance 2、
 *     tasks/foundation/WP-19-T06.json acceptance 1（逐项淘汰原因含
 *     thresholdSource 与稳定诊断引用）
 *
 * 确定性（NFR-COR-02）：清单序＝登记契约序（T02 批＝卡面章节序 §2.2 →
 * §5.3 → §6.2 → §6.3 → §9.3；T06 批表尾追加，批内序＝ReasonToken 词表
 * 组序）；每次调用返回同序同值新清单（登记行为纯值聚合）；码值经
 * DiagCodes.hpp 常量引用（唯一书写点）。
 */

#include <sdurws/ird/selection/DiagCodes.hpp>

#include <string>

namespace sdurws::ird::selection {

std::vector<DiagnosticEntry> selectionCodeEntries()
{
    // 清单序＝卡面章节序（登记契约序——追加只允许表尾）；逐码注释给出
    // 卡面出处与语义登记原文（触发条件/处置口径）。
    return {
        // ---- §2.2：移动关节轴范围外 ----
        // R1 只支持旋转传动（电机＋减速器）；目标链含移动关节→该轴输出
        // "范围外"诊断并按 DataInsufficient 语义处理。四不纪律：不静默
        // 套用旋转传动、不静默把移动关节转成旋转关节、不伪造电机工作点、
        // 不将该结果直接升级为整机工程不可行（判定权在 evidence 汇总
        // ——D-SEL-15）。
        {kSelInputAxisOutOfScope,
         "units/selection.md §2.2",
         "移动关节轴范围外：DataInsufficient 语义，不套用旋转传动、不伪造工作点、不升级整机不可行"},

        // ---- §5.3 行 1：字段字典不完整 ----
        // manifest 声明列与 CSV 表头不一致（多列/缺列）——逐列定位拒绝；
        // 目录 schema 的结构级错误在校验边界快速拒绝（整批拒绝＋逐项
        // 定位，卡 §10.2 短路边界）。
        {kSelCatalogSchemaMismatch,
         "units/selection.md §5.3",
         "字段字典与表头不一致：多列/缺列逐列定位，schema 级整批拒绝"},

        // ---- §5.3 行 2：单位非法 ----
        // 单位词表＋量纲检查不通过——未知单位拒绝、不猜测；比较型诊断
        // （实际/期望/单位，ERR-01）；单位换算唯一实现归 core（卡 §2.1
        // NFR-MNT-03 承接行），本域比较一律在 SI 域。
        {kSelCatalogUnitInvalid,
         "units/selection.md §5.3",
         "单位非法：未知单位拒绝不猜测；比较型（实际/期望/单位），比较在 SI 域"},

        // ---- §5.3 行 3：必填字段缺失 ----
        // 逐字段定位；与"允许缺失字段显式入 missing 清单"分轨（后者不
        // 伪造数值、按 Partial 状态降级——卡 §4.1 MotorCatalogEntry.missing）。
        {kSelCatalogFieldMissing,
         "units/selection.md §5.3",
         "必填字段缺失：逐字段定位；允许缺失字段另入 missing 清单，不伪造"},

        // ---- §5.3 行 4a：重复型号 ----
        // 同稳定 ID 的多行重复型号条目——候选身份唯一性前提破坏；注意
        // 显示名重复但 ID 不同＝合法（验证矩阵 V1 登记行），不落本码。
        {kSelCatalogDuplicateModel,
         "units/selection.md §5.3",
         "同稳定 ID 重复型号条目：候选身份唯一性破坏（显示名重复 ID 不同＝合法，不落本码）"},

        // ---- §5.3 行 4b：稳定 ID 重复 ----
        // (catalogId, version, modelId) 三元组唯一确定条目实例（卡 §4.2）
        // ——modelId 主键重复使兼容关系、淘汰原因定位全部失锚。
        {kSelCatalogDuplicateId,
         "units/selection.md §5.3",
         "稳定 ID 重复：三元组身份前提失效，主键唯一性拒绝"},

        // ---- §5.3 行 5：数值范围非法 ----
        // 转矩>0 N·m、效率∈(0,1] 无量纲、速比>0、寿命>0 等；非有限数
        // （NaN/±Inf）同路径——NFR-COR-03"非有限不静默通过"；比较型：
        // 实际值/期望范围/单位。
        {kSelCatalogRangeInvalid,
         "units/selection.md §5.3",
         "数值范围非法（转矩>0、效率∈(0,1]、速比>0 等；非有限同路径）：比较型（实际/期望范围/单位）"},

        // ---- §5.3 行 6＝§6.3 曲线缺失行：引用语义悬空 ----
        // curve_ref 指向曲线不存在或 owner 不匹配、compatibility 引用
        // 双方型号不存在——文件层存在性由 io 先行校验（io §7.8），语义
        // 层（owner 匹配）归本卡；§6.3"曲线缺失"行同码（条目引用了不
        // 存在的 curve_id）。
        {kSelCatalogRefDangling,
         "units/selection.md §5.3/§6.3",
         "引用语义悬空（curve_ref 无曲线或 owner 不匹配、compatibility 引用缺失；曲线缺失同码）"},

        // ---- §5.3 行 7：兼容关系冲突 ----
        // 同型号对多行且 mount_kind 安装关系矛盾——组合兼容判定（§9.3
        // 无记录即不兼容）的前提被破坏，导入期拒绝。
        {kSelCatalogCompatConflict,
         "units/selection.md §5.3",
         "兼容关系冲突：同型号对多行且 mount_kind 矛盾，导入期拒绝"},

        // ---- §6.2：默认禁止外推 ----
        // 查询点落在 [x_min, x_max] 闭区间外即拒绝——不自动使用最近点、
        // 不静默外推；比较型（实际输入点/有效区间/单位）。插值失败≠候选
        // 能力不足：本码属数据不足类标记，与能力不足类淘汰原因分轨
        // （卡 §6.2 分轨纪律）。
        {kSelCurveExtrapolationDenied,
         "units/selection.md §6.2",
         "默认禁止外推：区间外查询拒绝，不用最近点、不静默外推；数据不足类与能力不足分轨"},

        // ---- §6.3 行 1：采样点无序 ----
        // 未按 x 升序提交即拒绝——构造入口不代排序（排序会掩盖目录错误，
        // 要求目录修正——卡 §6.3 明示）；升序是分段线性插值的结构前提。
        {kSelCurveUnordered,
         "units/selection.md §6.3",
         "曲线采样点无序：拒绝，构造入口不代排序（排序会掩盖目录错误）"},

        // ---- §6.3 行 2：重复横坐标 ----
        // 同 x 不同 y 的点对——插值在该横坐标处语义歧义，导入期拒绝。
        {kSelCurveDupX,
         "units/selection.md §6.3",
         "重复横坐标（同 x 不同 y）：插值语义歧义，导入期拒绝"},

        // ---- §6.3 行 3：非有限点 ----
        // 曲线点含 NaN/±Inf 任一——NFR-COR-03 精确判据"非有限即拒绝"，
        // 不静默丢弃坏点（丢点会改变曲线区间与插值结果）。
        {kSelCurveNonfinite,
         "units/selection.md §6.3",
         "曲线点非有限（NaN/±Inf）：拒绝，不静默丢弃坏点"},

        // ---- §6.3 行 4：区间不合法 ----
        // x_max ≤ x_min 或单点曲线声明为曲线——分段线性插值区间前提
        // 破坏；单点能力值应走"固定额定值"显式口径（卡 §6.4——不得用
        // 额定值伪造缺失的能力曲线）。
        {kSelCurveIntervalInvalid,
         "units/selection.md §6.3",
         "区间不合法（x_max≤x_min 或单点曲线）：单点能力值走固定额定值口径，不伪造曲线"},

        // ---- §9.3 行 1：组合不兼容 ----
        // compatibility 表无该（motor, gearbox）型号对记录即不兼容——
        // 零行语义＝包内无预声明兼容对（卡 §5.2）；组合级淘汰原因
        // （§10.2 分层：轴级→组合级→整机素材）。
        {kSelComboIncompatible,
         "units/selection.md §9.3",
         "组合不兼容：无兼容记录即不兼容（零行＝无预声明兼容对）；组合级淘汰原因"},

        // ---- §9.3 行 2：轴映射不完整 ----
        // 每轴恰一组合的前提破坏（漏轴）——组合身份 DeviceCombinationId
        // 的轴序×器件完备性要求（卡 §9.5），不允许多轴/缺轴兜底。
        {kSelComboAxisMappingIncomplete,
         "units/selection.md §9.3",
         "轴映射不完整：每轴恰一组合，漏轴即拒绝（不缺轴兜底）"},

        // ---- §9.3 行 11/12：身份不一致 ----
        // 组合校核所用目录版本≠映射批候选参数来源版本，或 drivetrain
        // 映射 Facts 契约/算法版本与切片声明不一致——拒绝评估（AT-38
        // dynamics/drivetrain/selection 三方同口径；不以版本不符数据
        // 继续计算）。
        {kSelIdentityMismatch,
         "units/selection.md §9.3",
         "身份不一致（目录版本/映射契约版本与切片声明不符）：拒绝评估，AT-38 三方同口径"},

        // =============================================================
        // T06 批（WP-19-T06）：§10.3 淘汰原因词表的逐 token 稳定码建议值
        // ——28 码表尾追加；批内序＝ReasonToken 词表组序。逐码出处统一
        // 登记卡 §10.3（词表行）；语义列登记该 token 的判定语义与分轨
        // 口径（ERR-01 比较型字段齐备由 RejectionReason 承载——码是
        // 引用面，不是第二事实源）。
        // =============================================================

        // ---- 电机能力组（词表行 1；11 码）----
        {kSelMotorTorqueContinuousInsufficient,
         "units/selection.md §10.3/§7.1",
         "电机连续转矩不足：工作点 τ_rms＞额定连续转矩——逐项淘汰原因（SEL-03 维度）"},
        {kSelMotorTorquePeakInsufficient,
         "units/selection.md §10.3/§7.1",
         "电机峰值转矩不足：工作点 τ_peak＞峰值转矩——逐项淘汰原因（SEL-03 维度）"},
        {kSelMotorSpeedInsufficient,
         "units/selection.md §10.3/§7.1",
         "电机转速不足：ω_peak＞最高转速或 ω_rms＞额定转速——逐项淘汰原因（SEL-03 维度）"},
        {kSelMotorPowerInsufficient,
         "units/selection.md §10.3/§7.1",
         "电机功率不足：工作点功率＞功率能力——逐项淘汰原因（SEL-03 维度）"},
        {kSelMotorOverloadTimeInsufficient,
         "units/selection.md §10.3/§7.1",
         "电机过载持续时间不足：峰值段时长＞目录允许时长——逐项淘汰原因（触发式维度）"},
        {kSelMotorDutyMismatch,
         "units/selection.md §10.3/§7.1",
         "电机工作制不匹配：需求工作制∉目录工作制——逐项淘汰原因"},
        {kSelMotorVoltageMismatch,
         "units/selection.md §10.3/§7.1",
         "电机电压不匹配：需求电压与目录额定电压超出容差——逐项淘汰原因"},
        {kSelMotorThermalDeratingInsufficient,
         "units/selection.md §10.3/§7.1",
         "电机温度降额复判不足：折减后能力＜工作点——独立 token 与原始转矩维度分轨并行"},
        {kSelMotorBrakeInsufficient,
         "units/selection.md §10.3/§7.1",
         "电机制动能力不足：保持需求＞制动转矩——逐项淘汰原因"},
        {kSelMotorHoldingInsufficient,
         "units/selection.md §10.3/§7.1",
         "电机保持能力不足：保持需求＞保持能力——逐项淘汰原因"},
        {kSelMotorSafetyFactorInsufficient,
         "units/selection.md §10.3/§7.1",
         "电机安全系数复判不足：工作点×SF＞能力值——逐项淘汰原因（复判维度）"},

        // ---- 减速器能力组（词表行 2；9 码）----
        {kSelGearboxRatedTorqueInsufficient,
         "units/selection.md §10.3/§8.1",
         "减速器额定输出转矩不足：关节 τ_rms＞额定输出转矩——逐项淘汰原因（SEL-04 维度）"},
        {kSelGearboxPeakTorqueInsufficient,
         "units/selection.md §10.3/§8.1",
         "减速器峰值输出转矩不足：关节 τ_peak＞峰值输出转矩——逐项淘汰原因（SEL-04 维度）"},
        {kSelGearboxInputSpeedExceeded,
         "units/selection.md §10.3/§8.1",
         "减速器输入转速超限：ω_m_peak＞允许输入转速——逐项淘汰原因（SEL-04 维度）"},
        {kSelGearboxRatioMismatch,
         "units/selection.md §10.3/§8.1",
         "速比不匹配：候选速比∉该轴允许传动比范围——逐项淘汰原因"},
        {kSelGearboxEfficiencyInsufficient,
         "units/selection.md §10.3/§8.1",
         "减速器效率不足：目录效率＜筛选条件最低效率——逐项淘汰原因"},
        {kSelGearboxBacklashExceeded,
         "units/selection.md §10.3/§8.1",
         "回程间隙超限：目录回隙＞筛选条件上限——逐项淘汰原因"},
        {kSelGearboxLifeInsufficient,
         "units/selection.md §10.3/§8.1",
         "减速器寿命不足：目录额定寿命＜筛选条件要求——逐项淘汰原因"},
        {kSelMountingIncompatible,
         "units/selection.md §10.3/§7.1/§8.1",
         "安装不兼容（电机/减速器共用 token——T04 落位细化 ③）：接口/安装方向与关节安装关系不符，失败分轨独立记因"},
        {kSelGearboxExternalLoadExceeded,
         "units/selection.md §10.3/§8.1",
         "允许外载荷超限：实际外载荷＞力臂核算后允许值——逐项淘汰原因"},

        // ---- 组合/一致性组新增码（词表行 3；其余 5 token 复用 T02 批码）----
        {kSelInertiaRatioPolicyUnsettled,
         "units/selection.md §10.3/§11.3",
         "惯量比策略未裁决（O-11/P-POL-3）：显式未判定标记——非淘汰码，不进入淘汰统计，Verified 不整体阻断"},

        // ---- 上游/数据组（词表行 4；5 码——与候选淘汰分轨）----
        {kSelDynamicsMissing,
         "units/selection.md §10.3/§10.2",
         "dynamics 结果缺失：上游缺失按数据不足分轨——不伪装候选淘汰、不默认零负载"},
        {kSelDrivetrainMissing,
         "units/selection.md §10.3/§10.2",
         "drivetrain 映射缺失/失败：上游诊断透传——映射失败≠器件能力不足，不自动判整机不可行"},
        {kSelCaseCoverageGap,
         "units/selection.md §10.3/§9.4",
         "工况覆盖缺口：EVI-02 素材面——任一必验工况数据不足组合整体 DataInsufficient，不漏验"},
        {kSelInputInvalid,
         "units/selection.md §10.3/§10.2",
         "输入非法：校验边界快速拒绝类——与候选能力淘汰分轨（不留下笼统不可行）"},
        {kSelComputeFailed,
         "units/selection.md §10.3",
         "计算失败：与候选淘汰分开、不进入淘汰原因统计（卡 §10.3 词表登记）"},

        // ---- 边界/偏好组（词表行 5；2 码——AxisOutOfScope 复用 T02 批码）----
        {kSelR2CapabilityDisabled,
         "units/selection.md §10.3/§17.2",
         "R2 能力未启用：直线传动等 R2 扩展在阶段 D 前显式拒绝——不静默套用旋转传动"},
        {kSelUserPreferenceFiltered,
         "units/selection.md §10.3/§10.2",
         "用户优选过滤：偏好呈现结果、非工程结论——与硬能力淘汰分开（SEL-07 分轨）"},

        // ---- T09 批（WP-19-T09）：§12 器件回填的拒绝/定位族（9 码）----
        // 批内序＝§12.1 S3 判定序（载荷结构→载荷版本→域输入→目录/安装
        // →数据缺失→数值范围→锁定引用→合成断言）；组装期与 prepare 期
        // 共用同码（同一拒绝语义不分阶段私设第二码——NFR-MNT-03）。
        {kSelBackfillPayloadMalformed,
         "units/selection.md §12.1/§12.3",
         "回填载荷结构非法：magic/截断/残余/非法标志——解码面拒绝，不产出半成品"},
        {kSelBackfillPayloadVersionUnsupported,
         "units/selection.md §12.3",
         "回填载荷版本不受理：NFR-DEP-04 拒绝不猜测——升级指引面"},
        {kSelBackfillInputInvalid,
         "units/selection.md §12.1/§12.2",
         "回填域输入非法：参考系词表外/轴身份保留值/同轴重复/空轴表——组装与 prepare 共用同码"},
        {kSelBackfillUnknownDevice,
         "units/selection.md §12.1",
         "型号不在目录快照主表：电机或减速器查找落空——候选存在判定的组装侧"},
        {kSelBackfillMountMismatch,
         "units/selection.md §12.2",
         "安装关系与兼容表不一致：§12.2 纪律 4——记录 mountKind 须与目录兼容记录一致"},
        {kSelBackfillDataInsufficient,
         "units/selection.md §12.4",
         "壳体物性缺失：合成不可得→整体失败零修订（P-SEL-6 保守口径——不允许部分回填）"},
        {kSelBackfillRangeInvalid,
         "units/selection.md §12.4",
         "回填数值范围非法：传动比/质量/非有限物性——NFR-COR-03 不静默置零（I-MDL-11 同口径）"},
        {kSelBackfillLockRefMismatch,
         "units/selection.md §12.1/§12.2",
         "目录锁定引用与基线闭包不一致：lockObject/lockVersion 失配——引用完整性破坏"},
        {kSelBackfillSynthesisAssertFailed,
         "units/selection.md §12.4",
         "合成物性断言失败：MDL-06 断言①～③同语义任一违约——回填失败零修订（硬断言轨）"},
    };
}

// =====================================================================
// ReasonToken → 稳定码唯一映射（DiagCodes.hpp 声明的唯一实现——全表
// 34 token；复用码分支引用既有常量、新码分支引用 T06 批常量，禁第二处
// 字面量）。分支序＝ReasonToken 枚举序＝词表序（与 switch 可读序一致；
// 映射值与 selectionCodeEntries 登记行同源——两处皆引用同一常量，失同
// 步由 FeasibleSetContractTest 全遍历＋DiagCodesTest 机械比对双面钉住）。
// =====================================================================

std::string_view reasonTokenDiagCode(ReasonToken token)
{
    switch (token) {
    // ---- 电机能力组（11 token→SEL-MOTOR- 族）----
    case ReasonToken::TorqueContinuousInsufficient:
        return kSelMotorTorqueContinuousInsufficient;
    case ReasonToken::TorquePeakInsufficient:
        return kSelMotorTorquePeakInsufficient;
    case ReasonToken::SpeedInsufficient:
        return kSelMotorSpeedInsufficient;
    case ReasonToken::PowerInsufficient:
        return kSelMotorPowerInsufficient;
    case ReasonToken::OverloadTimeInsufficient:
        return kSelMotorOverloadTimeInsufficient;
    case ReasonToken::DutyMismatch:
        return kSelMotorDutyMismatch;
    case ReasonToken::VoltageMismatch:
        return kSelMotorVoltageMismatch;
    case ReasonToken::ThermalDeratingInsufficient:
        return kSelMotorThermalDeratingInsufficient;
    case ReasonToken::BrakeInsufficient:
        return kSelMotorBrakeInsufficient;
    case ReasonToken::HoldingInsufficient:
        return kSelMotorHoldingInsufficient;
    case ReasonToken::SafetyFactorInsufficient:
        return kSelMotorSafetyFactorInsufficient;

    // ---- 减速器能力组（9 token→SEL-GEARBOX- 族＋共用安装码）----
    case ReasonToken::GearboxRatedTorqueInsufficient:
        return kSelGearboxRatedTorqueInsufficient;
    case ReasonToken::GearboxPeakTorqueInsufficient:
        return kSelGearboxPeakTorqueInsufficient;
    case ReasonToken::InputSpeedExceeded:
        return kSelGearboxInputSpeedExceeded;
    case ReasonToken::RatioMismatch:
        return kSelGearboxRatioMismatch;
    case ReasonToken::EfficiencyInsufficient:
        return kSelGearboxEfficiencyInsufficient;
    case ReasonToken::BacklashExceeded:
        return kSelGearboxBacklashExceeded;
    case ReasonToken::LifeInsufficient:
        return kSelGearboxLifeInsufficient;
    case ReasonToken::MountingIncompatible:
        return kSelMountingIncompatible;
    case ReasonToken::ExternalLoadExceeded:
        return kSelGearboxExternalLoadExceeded;

    // ---- 组合/一致性组（6 token：3 复用 T02 批码＋1 复用同码＋2 新码；
    //      CatalogVersionIncompatible/MappingVersionIncompatible 按卡
    //      §9.3 行 10/11 与 IdentityMismatch 同码——SEL-IDENTITY-MISMATCH）----
    case ReasonToken::ComboIncompatible:
        return kSelComboIncompatible;
    case ReasonToken::AxisMappingIncomplete:
        return kSelComboAxisMappingIncomplete;
    case ReasonToken::InertiaRatioPolicyUnsettled:
        return kSelInertiaRatioPolicyUnsettled;
    case ReasonToken::IdentityMismatch:
        return kSelIdentityMismatch;
    case ReasonToken::CatalogVersionIncompatible:
        return kSelIdentityMismatch;
    case ReasonToken::MappingVersionIncompatible:
        return kSelIdentityMismatch;

    // ---- 上游/数据组（5 token→新码；与候选淘汰分轨）----
    case ReasonToken::DynamicsMissing:
        return kSelDynamicsMissing;
    case ReasonToken::DrivetrainMissing:
        return kSelDrivetrainMissing;
    case ReasonToken::CaseCoverageGap:
        return kSelCaseCoverageGap;
    case ReasonToken::InputInvalid:
        return kSelInputInvalid;
    case ReasonToken::ComputeFailed:
        return kSelComputeFailed;

    // ---- 边界/偏好组（3 token：1 复用 T02 批码＋2 新码）----
    case ReasonToken::AxisOutOfScope:
        return kSelInputAxisOutOfScope;
    case ReasonToken::R2CapabilityDisabled:
        return kSelR2CapabilityDisabled;
    case ReasonToken::UserPreferenceFiltered:
        return kSelUserPreferenceFiltered;
    }
    // 词表外整数值（防御分支——不抛不私造）：返回空串，调用方以空串判
    // "无码可引"，diagRef 保持 nullopt（NFR-MNT-03 不伪造码值）。
    return std::string_view{};
}

}  // namespace sdurws::ird::selection
