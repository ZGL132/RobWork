/**
 * @file   Screening.hpp
 * @brief  电机/减速器硬筛选（selection 单元）——筛选条件、工作点事实、
 *         淘汰原因词表（ReasonToken 封闭词表）、FeasibilityRecord 输出与
 *         IHardConstraintSelector/HardConstraintSelector（卡 §7/§8/§10/
 *         §14.4 设计基线）。
 *
 * 设计依据：
 *   - units/selection.md §7（电机硬筛选流程与纪律）、§8（减速器硬筛选
 *     流程与纪律）、§10.1（FeasibilityRecord/RejectionReason 类型基线）、
 *     §10.2（分层与短路边界——候选能力筛选不短路）、§10.3（淘汰原因
 *     词表）、§10.4（稳定排序）、§14.4（IHardConstraintSelector 接口
 *     签名）、§14.9（ICancellation 复用既有形态）、§14.0（通用约定：
 *     调用方错误 fail-fast vs 数据/环境类返回诊断）
 *   - 需求 SEL-03（按连续/峰值转矩、转速、功率、过载持续时间、工作制、
 *     电压、温度降额、制动/保持和安全系数筛选电机）、SEL-04（按额定/
 *     峰值转矩、允许输入转速、速比、效率、回程间隙、寿命、安装方向和
 *     允许外载荷筛选减速器）、SEL-06（逐项淘汰原因含实际值与阈值——
 *     ERR-01 比较型字段）、SEL-07（硬能力判定不受优选品牌影响——本头
 *     无品牌/偏好输入面，优选过滤随 WP-19-T07 走独立 token 分轨）
 *   - 任务契约 tasks/foundation/WP-19-T04.json（acceptance 1 全维度
 *     覆盖＋acceptance 2 可行/不可行黄金表＋acceptance 3 门禁与留痕）
 *
 * ★ 边界（PA-1 权威唯一——本筛选器不做什么）：
 *   1. 不调用传动映射（§9.1"selection 不得重新计算"）：电机侧工作点
 *      （motor* 字段）由调用方经 AxisWorkpointFacts 值传递供给——R1 两段
 *      管线（§9.2）中该供给发生在组合校核段（dt.mapping 批结果）；唯一
 *      例外是减速器输入转速维度的 ω_m＝ω_joint/c 换算（§8.2 明文允许的
 *      候选传动参数换算——"不自算 ω_m＝ω_j/c 之外的任何映射量"）。
 *   2. 不判工程不可行（§10.2）：全淘汰不构成任务级确定性不可行——正式
 *      判定权在 evidence 汇总；本头只输出逐候选资格事实。
 *   3. 惯量比维度不在本头（O-11/P-POL-3 阈值归属未裁决——卡 §11.3：
 *      未裁决期间该维度输出"未判定"且不是 §7/§8 硬筛选维度；组合校核
 *      阶段〔WP-19-T05〕才消费，且不内嵌任何阈值数字）。
 *   4. 组合构造/组合校核评估器已随 WP-19-T05 落位（Combination.hpp——
 *      §14.5/§9）；可行集汇总/淘汰原因的独立供给接口已随 WP-19-T06
 *      落位（FeasibleSet.hpp——§14.6）；本头的 RejectionReason/
 *      FeasibilityRecord 是 §10.1 基线类型的候选级承载，供 T05/T06
 *      复用（同一类型，不分叉）。
 *   5. 移动关节范围外阻断已随 WP-19-T08 落位（SEL-09——卡 §2.2 R1
 *      纪律/D-SEL-15）：AxisWorkpointFacts.jointKind 声明轴关节类型，
 *      Prismatic 轴在本筛选器输出"范围外"记录（VerdictKind::
 *      DataInsufficient＋SEL-INPUT-AXIS-OUT-OF-SCOPE 数据缺口），不执行
 *      §7/§8 任何旋转传动维度判定——不静默套用旋转传动、不伪造电机
 *      工作点、不升级整机不可行（判定权在 evidence 汇总）。
 *
 * 线程安全：HardConstraintSelector 无状态纯函数对象（可重入——卡
 * §14.10"筛选/曲线/组合构造（纯函数）可重入"）；全部输入由调用方持有，
 * 输出按值返回（卡 §14.0 所有权约定）。
 */

#ifndef IRD_SELECTION_SCREENING_HPP
#define IRD_SELECTION_SCREENING_HPP

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/evidence/Evaluator.hpp>   // evidence::IEvaluationContext——取消查询
                                               //   既有形态（卡 §14.9"已有形态则复用"）
#include <sdurws/ird/selection/CatalogTypes.hpp>
#include <sdurws/ird/selection/LinearDrive.hpp> // LinearAxisWorkpointFacts——直线轴
                                               //   类型化广义量工作点事实（WP-19-T12
                                               //   接口扩展消费类型；drivetrain §16.2
                                               //   扩展端口消费承载——提议契约 v1）

namespace sdurws::ird::selection {

// =====================================================================
// 域内 ID 别名（落位细化——卡 §10.1 写 core::StableId/core::CaseId，
// core 未落位该二类型：ModelId 沿用 T03 承载；CaseId 以 std::string
// 强语义别名落位，dynamics 侧收编后不改语义。登记于单元卡 §19.3 T04 ①；
// ★ WP-19-T12 上移：CaseId 别名定义移至 CatalogTypes.hpp"强语义 ID 别名"
// 区——直线传动工作点事实类型（LinearDrive.hpp）与本头共用该别名，上移
// 消除两头循环 include，本头经 CatalogTypes.hpp 继续可见，语义零变化）
// =====================================================================

// =====================================================================
// §10.3 淘汰原因词表（ReasonToken 封闭词表的物化）
// =====================================================================

/**
 * @brief 淘汰原因 token（卡 §10.3 封闭词表——枚举序＝词表序＝稳定排序
 *        键首位，卡 §10.4"淘汰原因稳定排序：reasonToken 词表序 → …"；
 *        词表冻结后追加只允许表尾，既有枚举项不重排/不删除）。
 *
 * 词表文本经 reasonTokenText() 取得（唯一映射点——禁止在筛选实现里
 * 第二处书写 token 字符串字面量）。SEL-* 稳定诊断码映射（diagRef 回填）
 * 随 WP-19-T06 注册（DiagCodes.hpp 头注"登记范围口径"——T04 不预建，
 * diagRef 全程 nullopt）。
 */
enum class ReasonToken {
    // ---- 电机能力组（卡 §10.3 行 1；11 token——SEL-03 维度失败）----
    TorqueContinuousInsufficient,   ///< 连续转矩不足（工作点 τ_rms ＞ 额定连续转矩）
    TorquePeakInsufficient,         ///< 峰值转矩不足（工作点 τ_peak ＞ 峰值转矩）
    SpeedInsufficient,              ///< 转速不足（ω_peak ＞ 最高转速 或 ω_rms ＞ 额定转速）
    PowerInsufficient,              ///< 功率不足（P_peak/P_rms ＞ 功率能力）
    OverloadTimeInsufficient,       ///< 过载持续时间不足（峰值段时长 ＞ 目录过载持续时间）
    DutyMismatch,                   ///< 工作制不匹配（需求工作制 ∉ 目录工作制）
    VoltageMismatch,                ///< 电压不匹配（需求电压 ≠ 目录额定电压容差内）
    ThermalDeratingInsufficient,    ///< 温度降额后复判不足（折减能力 ＜ 工作点）
    BrakeInsufficient,              ///< 制动能力不足（保持需求 ＞ 制动转矩）
    HoldingInsufficient,            ///< 保持能力不足（保持需求 ＞ 保持能力）
    SafetyFactorInsufficient,       ///< 安全系数复判不足（工作点×SF ＞ 能力值）

    // ---- 减速器能力组（卡 §10.3 行 2；9 token——SEL-04 维度失败）----
    GearboxRatedTorqueInsufficient, ///< 减速器额定输出转矩不足（关节 τ_rms ＞ 额定输出转矩）
    GearboxPeakTorqueInsufficient,  ///< 减速器峰值输出转矩不足（关节 τ_peak ＞ 峰值输出转矩）
    InputSpeedExceeded,             ///< 输入转速超限（ω_m_peak ＞ 允许输入转速）
    RatioMismatch,                  ///< 速比不匹配（候选速比 ∉ 该轴允许传动比范围）
    EfficiencyInsufficient,         ///< 效率不足（目录效率 ＜ 筛选条件最低效率）
    BacklashExceeded,               ///< 回程间隙超限（目录回隙 ＞ 筛选条件上限）
    LifeInsufficient,               ///< 寿命不足（目录额定寿命 ＜ 筛选条件要求）
    MountingIncompatible,           ///< 安装不兼容（接口/安装方向 vs 关节安装关系——
                                    ///   电机与减速器共用 token，落位细化登记 T04 ③）
    ExternalLoadExceeded,           ///< 允许外载荷超限（实际外载荷 ＞ 力臂核算后允许值）

    // ---- 组合/一致性组（卡 §10.3 行 3；6 token——WP-19-T05 消费）----
    ComboIncompatible,              ///< 电机—减速器组合不兼容（兼容表无记录）
    AxisMappingIncomplete,          ///< 轴映射不完整（漏轴）
    InertiaRatioPolicyUnsettled,    ///< 惯量比策略未裁决（O-11——显式未判定，非淘汰）
    IdentityMismatch,               ///< 身份不一致（目录/映射版本错配——SEL-IDENTITY-MISMATCH）
    CatalogVersionIncompatible,     ///< 目录版本不兼容
    MappingVersionIncompatible,     ///< 映射版本不兼容

    // ---- 上游/数据组（卡 §10.3 行 4；5 token——上游缺口/计算失败分轨）----
    DynamicsMissing,                ///< dynamics 结果缺失
    DrivetrainMissing,              ///< drivetrain 映射缺失
    CaseCoverageGap,                ///< 工况覆盖缺口
    InputInvalid,                   ///< 输入非法（校验边界拒绝类）
    ComputeFailed,                  ///< 计算失败（与候选淘汰分开，不进淘汰统计）

    // ---- 边界/偏好组（卡 §10.3 行 5；3 token）----
    AxisOutOfScope,                 ///< 轴范围外（R1 移动关节——WP-19-T08 消费）
    R2CapabilityDisabled,           ///< R2 能力未启用
    UserPreferenceFiltered,         ///< 用户优选过滤（与硬能力失败分离——SEL-07 分轨）

    // ---- 直线传动能力组（卡 §10.3 表尾追加组——WP-19-T12/§17.2
    //      SEL-09-S1；4 token——四类直线器件共用能力维度）----
    LinearForceContinuousInsufficient, ///< 直线连续推力不足（工作点推力 RMS
                                       ///  ＞目录额定推力 rated_force_n）
    LinearForcePeakInsufficient,       ///< 直线峰值推力不足（工作点峰值推力
                                       ///  ＞目录峰值推力 peak_force_n 或推力-
                                       ///  速度曲线插值上限——禁外推分轨见 §6.2）
    LinearSpeedInsufficient,           ///< 直线速度不足（工作点峰值线速度
                                       ///  ＞目录最高线速度 max_speed_ms，m/s）
    LinearPowerInsufficient,           ///< 直线功率不足（工作点峰值/RMS 功率
                                       ///  ＞目录额定功率 rated_power_w，W）
};

/// 词表全表行数（封闭词表的规模冻结——遍历上界；追加 token 时同步更新。
/// T12 批表尾追加 4：34→38——WP-19-T12；既有 34 项枚举值零变化）。
inline constexpr int kReasonTokenCount = 38;

/**
 * @brief ReasonToken → 词表文本（卡 §10.3 token 字符串的唯一映射点）。
 *
 * @param token [in] 淘汰原因 token
 * @return 词表文本（卡 §10.3 登记值，如 "torque-continuous-insufficient"）；
 *         token 越界（词表外整数值）返回 "unknown-reason-token"（不抛——
 *         词表文本用于呈现/序列化，越界属防御分支）
 *
 * @note 纯函数；确定性。词表序（枚举序）与文本的对应关系由词表测试
 *       全表钉住（contract_test 词表封闭性用例）。
 */
std::string_view reasonTokenText(ReasonToken token);

// =====================================================================
// §10.1 可行性判定类型（候选级承载——落位细化 T04 ①）
// =====================================================================

/// 判定结论（卡 §10.2 空集语义的三态收敛——汇总规则见单元卡 §19.3 T04 ⑦：
/// reasons 非空 → Rejected；否则 gaps 非空 → DataInsufficient；否则 Feasible）。
enum class VerdictKind {
    Feasible,           ///< 可行（全部已判定维度通过且无数据缺口）
    Rejected,           ///< 淘汰（至少一条硬淘汰原因——逐项原因齐备）
    DataInsufficient,   ///< 数据不足（无淘汰原因但存在缺口——不默认通过，§7.2）
};

/// 候选器件类别（FeasibilityRecord 侧标记——电机/减速器筛选共用记录类型）。
/// Combination 为 WP-19-T05 表尾追加值（封闭枚举追加只允许表尾，既有项
/// 不重排/不删除——词表纪律；组合级记录复用本类型，T05 落位细化登记）。
enum class DeviceKind {
    Motor,      ///< 电机候选（screenMotors 产出）
    Gearbox,    ///< 减速器候选（screenGearboxes 产出）
    Combination, ///< 器件组合（组合校核产出——WP-19-T05；组合级记录的类别标记）
    LinearDrive ///< 直线传动器件候选（screenLinearDrives 产出——WP-19-T12/
                ///  SEL-09-S1 选型层；表尾追加：既有三值零变化）
};

/**
 * @brief 数据缺口（卡 §7.2"缺工作点数据的维度标数据不足"与 §10.2 空集
 *        语义表"数据不足"行的承载——与淘汰原因分轨：缺口不是淘汰，
 *        不伪造数值，不默认通过）。
 *
 * 卡面 §10.1 只引用 DataGap 未给字段——本结构为 T04 落位定义（登记
 * 单元卡 §19.3 T04 ①），字段以"定位＋语义＋关联码"最小集承载。
 */
struct DataGap {
    std::string dimension;   ///< 缺口维度名（筛选维度标识，如 "power-curve"）
    std::string detail;      ///< 中文缺口说明（缺什么数据、为何需要——呈现素材）
    core::ObjectId axisId;   ///< 轴对象 ID（多轴批量时定位）
    CaseId caseId;           ///< 工况 ID（可空串——维度级缺口与工况无关时）
    std::string diagCode;    ///< 关联稳定码（如曲线外推拒绝 SEL-CURVE-EXTRAPOLATION-DENIED；
                             ///   无关联码时为空——不私造码值，NFR-MNT-03）

    bool operator==(const DataGap& o) const
    {
        return dimension == o.dimension && detail == o.detail && axisId == o.axisId
            && caseId == o.caseId && diagCode == o.diagCode;
    }
    bool operator!=(const DataGap& o) const { return !(*this == o); }
};

/**
 * @brief 移动关节"范围外"数据缺口构造（WP-19-T08——SEL-09/D-SEL-15：
 *        目标链含移动关节时该轴输出明确"范围外"诊断，DataInsufficient
 *        语义，不静默套用旋转传动）。
 *
 * 唯一书写点：dimension 词面（"axis-out-of-scope"，与 §10.3 词表
 * ReasonToken::AxisOutOfScope 文本一致——同一语义在"原因词表面"与
 * "数据缺口面"分轨使用同一定位词）与中文 detail 文本在本函数唯一
 * 书写；筛选器（screenMotors/screenGearboxes）与组合校核
 * （checkCombinations）共用，禁第二处字面量（NFR-MNT-03 单一权威）。
 *
 * @param axisId   [in] 范围外轴的对象 ID（定位面——多轴链中区分哪根轴
 *                 范围外）
 * @param diagCode [in] 关联稳定码（调用方传 DiagCodes.hpp 的
 *                 kSelInputAxisOutOfScope 常量；以参数注入而非本头直接
 *                 include DiagCodes.hpp——DiagCodes.hpp 反向 include 本头
 *                 取 ReasonToken，直接包含会形成循环依赖）
 * @return 缺口记录（caseId 为空——轴级边界事实与工况无关，DataGap
 *         caseId 空串语义）
 *
 * @note 纯函数；确定性。
 */
inline DataGap makeAxisOutOfScopeGap(const core::ObjectId& axisId, std::string diagCode)
{
    DataGap g;
    g.dimension = "axis-out-of-scope";  // 定位词＝词表 token 文本（§10.3）
    g.detail = "目标链该轴为移动关节（prismatic）——R1 选型只支持旋转传动"
               "（SEL-09 范围外：不静默套用旋转传动、不伪造电机工作点；"
               "SEL-09-S1/MDL-12-S1 启用前维持阻断，DataInsufficient 语义，"
               "不升级整机不可行——判定权在 evidence 汇总）";
    g.axisId = axisId;
    g.caseId = CaseId{};  // 与工况无关——空串（轴级边界事实）
    g.diagCode = std::move(diagCode);
    return g;
}

/**
 * @brief 逐项淘汰原因（卡 §10.1 RejectionReason 基线——ERR-01 比较型
 *        字段齐备：每条原因携带实际值/要求值/单位/阈值来源，review 与
 *        报告层不需要回查目录即可理解淘汰依据，卡 §10.3"每条原因包含
 *        全部字段"）。
 *
 * 落位细化（登记单元卡 §19.3 T04 ①）：卡面 candidateId（core::ObjectId）
 * 以 ModelId 承载——候选是目录型号（目录域身份＝modelId，无项目对象
 * ID）；core::ObjectId 保留给 axisId（轴是 modeling 项目对象）。
 */
struct RejectionReason {
    ReasonToken token = ReasonToken::InputInvalid; ///< 淘汰原因 token（§10.3 封闭词表）
    ModelId candidateModelId;   ///< 候选型号稳定 ID（候选定位——目录域身份）
    core::ObjectId axisId;      ///< 轴对象 ID（多轴批量时定位）
    CaseId caseId;              ///< 工况 ID（可空串——维度与特定工况无关时空）
    double atTime = 0.0;        ///< 工作点时间，单位 s（可适用时；不适用时 0）
    std::string segmentId;      ///< 轨迹段 ID（可适用时；不适用时空）
    double actual = 0.0;        ///< 实际值（SI 域——卡 §4.4 单位表；文本类比较
                                ///   如工作制/安装方向时本值无意义，入 actualText）
    double required = 0.0;      ///< 要求值（SI 域；阈值/边界值——"阈值"侧）
    std::string unit;           ///< 单位 token（core Units 词表，如 "N*m"/"rad/s"；
                                ///   文本类比较为空串）
    std::string thresholdSource; ///< 阈值来源（目录列名/筛选条件条目/映射事实——
                                 ///   卡 §10.3"阈值来源"登记；人读定位字符串）
    std::string actualText;     ///< 实际侧文本（文本类比较承载——工作制词表值/
                                ///   安装方向词表值/电压等数值比较时为空）
    std::string requiredText;   ///< 要求侧文本（同上——对称承载，ERR-01 可追溯）
    CatalogIdentity catalog;    ///< 目录版本（判定所用快照版本——追溯）
    core::ContentIdentity inputSliceId; ///< 输入切片身份（评估器路径由 T05 回填；
                                        ///   T04 硬筛选直调路径＝全零 ContentIdentity
                                        ///   〔无 evidence 切片——诚实标记，不伪造〕）
    core::ContentIdentity mappingId;    ///< 映射身份（可适用时——电机侧工作点来源
                                        ///   映射批；T04 直调路径＝全零）
    std::string suggestion;     ///< 建议动作（ERR-01——如"更换更大额定转矩型号"）
    std::optional<std::string> diagRef; ///< 稳定诊断引用（SEL-*——随 WP-19-T06
                                        ///   注册后回填；T04 恒 nullopt，
                                        ///   DiagCodes.hpp 头注登记口径）

    bool operator==(const RejectionReason& o) const;
    bool operator!=(const RejectionReason& o) const { return !(*this == o); }
};

/**
 * @brief 单候选×单轴的可行性记录（卡 §10.1 FeasibilityRecord 基线＋
 *        §7.1/§8.1 流程图"④/⑤汇总 → FeasibilityRecord"输出）。
 *
 * 记录粒度＝（候选型号 × 轴）：同轴多工况的全部维度原因合并进同一记录
 * （EVI-02 精神——候选在任一必验工况失败即不可用，原因按工况定位）。
 * 组合级/整机级汇总归 WP-19-T05/T06（§10.2 分层——轴/组合/整机三层
 * 不混淆，本结构只承载轴层候选资格）。
 *
 * 落位细化（登记单元卡 §19.3 T04 ①）：
 *   - 卡面 DeviceCombinationId id：T04 逐候选路径的记录键＝
 *     "<modelId>|<jointId 规范文本>"（候选×轴资格键）；组合键构造随
 *     WP-19-T05 落位后，组合级记录复用本结构、id 承载组合键；
 *   - 新增 deviceKind/candidateModelId/axisId/catalog/mappingId 字段：
 *     候选级身份与追溯面（报告层定位候选所需——卡 §10.3"候选对象定位"）。
 */
struct FeasibilityRecord {
    std::string id;             ///< 记录键（T04＝"<modelId>|<jointId>"；组合键随 T05）
    DeviceKind deviceKind = DeviceKind::Motor; ///< 候选类别
    ModelId candidateModelId;   ///< 候选型号稳定 ID（候选定位）
    core::ObjectId axisId;      ///< 轴对象 ID（资格所属轴）
    VerdictKind verdict = VerdictKind::DataInsufficient; ///< 判定结论（汇总规则见头注）
    CatalogIdentity catalog;    ///< 候选所属目录版本（追溯）
    core::ContentIdentity inputSliceId; ///< 输入切片身份（T04 直调路径全零——同 RejectionReason）
    core::ContentIdentity mappingId;    ///< 映射身份（电机侧工作点来源；T04 直调路径全零）
    std::vector<RejectionReason> reasons; ///< 全部独立淘汰原因（稳定排序——§10.4：
                                          ///   token 词表序 → 工况 ID → 时刻）
    std::vector<DataGap> gaps;            ///< 全部数据缺口（产生序＝维度执行序——确定性）

    bool operator==(const FeasibilityRecord& o) const;
    bool operator!=(const FeasibilityRecord& o) const { return !(*this == o); }
};

// =====================================================================
// 筛选输入：筛选条件与工作点事实（卡 §14.4 criteria/axisFacts 参数的
// 类型承载——卡面未给签名，T04 落位定义，登记单元卡 §19.3 T04 ②/⑧）
// =====================================================================

/// 允许速比范围（闭区间 [minRatio, maxRatio]；无量纲——候选速比落域外即
/// ratio-mismatch）。两端值须有限且 minRatio ≤ maxRatio（构造侧调用方契约）。
struct RatioRange {
    double minRatio = 0.0;  ///< 范围下界（含）
    double maxRatio = 0.0;  ///< 范围上界（含）

    bool operator==(const RatioRange& o) const
    {
        return minRatio == o.minRatio && maxRatio == o.maxRatio;
    }
};

/**
 * @brief 筛选条件（卡 §14.4 criteria——"安全系数/电压/环境温度/工作制
 *        需求/速比范围等，进入切片身份"）。
 *
 * ★ 条件缺失 ≠ 数据缺失（分界纪律，登记单元卡 §19.3 T04 ⑧）：筛选条件
 *   是用户输入——某条件未配置（optional 为空/文本为空）时**对应维度不
 *   适用**（跳过，无原因无缺口）；而目录条目侧或工作点侧缺数据时维度
 *   **数据不足**（DataGap，不默认通过——§7.2）。两者的判定轨完全分开。
 *
 * ★ 切片身份：本结构全字段进入 config.sel-screening Configuration 条目
 *   （卡 §7.2"筛选条件作为 Configuration 条目进入切片身份（改变条件→
 *   重算）"）——条目化编码归组合校核段（WP-19-T05）；本结构是域内承载。
 */
struct ScreeningCriteria {
    /// 要求安全系数（无量纲；≥1 且有限——调用方契约违约即 fail-fast；
    /// 1.0＝不启用安全系数维度，>1.0 时对已提供的 τ/ω/P 逐项 ×SF 复判，
    /// 卡 §7.1"安全系数：τ/ω/P × 要求安全系数后复判"）。
    double safetyFactor = 1.0;

    /// 需求工作制（卡 §4.1 dutyClass 词表值，如 "S1"；空串＝工作制维度不适用）。
    std::string requiredDutyClass;

    /// 需求电压，单位 V（nullopt＝电压维度不适用；存在时目录额定电压
    /// 缺失 → 数据不足）。
    std::optional<double> requiredVoltage;

    /// 电压匹配相对容差（无量纲，≥0 且有限；0＝精确相等——附录 D C4
    /// 公式 closeWithin 的 relative 分量：|V_rated−V_req| ≤ tol·|V_req|）。
    double voltageRelativeTolerance = 0.0;

    /// 环境温度（v1 无量纲档位值——°C 语义，T03 落位细化 ③；nullopt＝
    /// 温度降额维度不适用）。
    std::optional<double> ambientTemp;

    /// 回程间隙上限，单位 rad（SI 域比较——卡 §4.4；nullopt＝回隙维度不适用）。
    std::optional<double> maxBacklash;

    /// 要求额定寿命（v1 冻结口径＝循环数，无量纲 "1"——T03 落位细化 ③；
    /// nullopt＝寿命维度不适用）。
    std::optional<double> requiredLife;

    /// 最低效率要求（无量纲，∈(0,1]；nullopt＝效率维度不适用）。
    std::optional<double> minEfficiency;

    /// 该轴允许传动比范围（闭区间；nullopt＝速比维度不适用——卡 §8.1 ③
    /// "候选速比 ∈ 该轴允许传动比范围（来自筛选条件/传动配置意图）"；
    /// R1 为全轴统一条件面，逐轴范围随上游传动配置意图扩展）。
    std::optional<RatioRange> ratioRange;

    bool operator==(const ScreeningCriteria& o) const;
    bool operator!=(const ScreeningCriteria& o) const { return !(*this == o); }
};

/**
 * @brief 关节安装关系要求（卡 §7.1 ②/§8.1 ②"MountSpec vs 关节安装
 *        关节安装关系"的关节侧承载——来自 modeling 装配链，值传递）。
 *
 * 词表值逐字符精确匹配（与字段字典登记的词表一致性由导入期保证）；
 * 空串＝该项不限（维度子项不适用）。
 */
struct JointMountRequirement {
    std::string flangeKind;   ///< 要求法兰接口词表值（空＝不限）
    std::string shaftKind;    ///< 要求轴伸接口词表值（空＝不限）
    std::string orientation;  ///< 要求安装方向词表值（减速器 mounting_orientation
                              ///   判定消费；电机无该子项——空＝不限）

    bool operator==(const JointMountRequirement& o) const
    {
        return flangeKind == o.flangeKind && shaftKind == o.shaftKind
            && orientation == o.orientation;
    }
};

/**
 * @brief 外载荷事实（卡 §8.1 ④"负载/工具对输出轴的外载荷"——来自
 *        modeling 装配链，值传递；nullopt 整体缺失＝该轴未声明外载荷，
 *        外载荷维度不适用——不默认零载荷，§11.2 纪律）。
 */
struct ExternalLoadFacts {
    double radial = 0.0;    ///< 实际径向力，单位 N（≥0 且有限）
    double axial = 0.0;     ///< 实际轴向力，单位 N（≥0 且有限）
    double distance = 0.0;  ///< 外载荷作用点到输出轴肩的距离，单位 m（≥0 且有限；
                            ///   0＝作用点未标注——力臂核算按不折减执行）

    bool operator==(const ExternalLoadFacts& o) const
    {
        return radial == o.radial && axial == o.axial && distance == o.distance;
    }
};

/**
 * @brief 关节类型（卡 §2.2 R1 范围纪律与 §17.1 交接行"modeling/runtime
 *        → selection：关节类型；旋转/移动能力"的轴侧承载——WP-19-T08
 *        落位，登记单元卡 §19.3 T08 ①）。
 *
 * 承载纪律：关节类型的权威判定与链型支持矩阵归 modeling（MDL-12——
 * R1 正式链限全旋转主链；本枚举只是上游值传递的事实声明，selection
 * 不自判关节类型）。词表与 modeling 关节类型词面（revolute/continuous/
 * prismatic）对齐：continuous 关节经 modeling MDL-12 工程工作范围确认后
 * 进入正式链，在选型侧属旋转传动——与本枚举 Revolute 同成员（类型细分
 * 不在本枚举承载——selection 消费的是"旋转传动可用与否"这一维度）。
 */
enum class JointKind {
    Revolute,   ///< 旋转关节（revolute/continuous——R1 旋转传动适用；枚举值 0
                ///  兼作默认值——见 AxisWorkpointFacts.jointKind 注）
    Prismatic,  ///< 移动关节（prismatic——R1 选型范围外：SEL-09 阻断，
                ///  输出 SEL-INPUT-AXIS-OUT-OF-SCOPE 数据缺口）
};

/**
 * @brief 单轴×单工况的工作点事实（卡 §14.4 axisFacts"关节侧事实
 *        （dynamics 上游：峰值/RMS/工况分组）"的 v1 承载——登记单元卡
 *        §19.3 T04 ②：dynamics 卡未产出（R-SEL-1），本结构为 selection
 *        单方提议契约形态，dynamics 落位后按其卡收编）。
 *
 * 字段分两组（§9.1 数据流纪律）：
 *   - 关节侧（joint*）：dynamics 关节侧结果（DYN-03 口径）——减速器
 *     筛选维度直接消费；
 *   - 电机侧（motor*）：传动映射工作点事实（dt.mapping 批结果）——
 *     电机筛选维度消费。R1 两段管线中电机侧事实在组合校核段才存在；
 *     本筛选器是纯函数，两段皆可直调（调用方供给什么就判什么——
 *     不供给的量产生数据缺口，不伪造零值）。
 *
 * 全部 optional 数值字段：nullopt＝该量未供给（对应维度数据不足）；
 * present 值必须有限（NaN/±Inf＝调用方契约违约 fail-fast）。
 *
 * ★ jointKind 与工作点字段的关系（WP-19-T08——SEL-09）：移动关节轴的
 *   旋转工作点物理不存在（直线轴无 τ/ω 口径的旋转量）——组装方对
 *   Prismatic 轴的 joint* 与 motor* 各字段应全部保持 nullopt（不伪造
 *   工作点，卡 §2.2"不得伪造电机工作点"）；筛选器对该轴不消费任何
 *   工作点字段（先于全部维度判定输出范围外记录）。同轴多条 facts 的
 *   jointKind 必须一致（轴类型是轴级属性——矛盾属调用方契约违约
 *   fail-fast）。
 */
struct AxisWorkpointFacts {
    core::ObjectId jointId;   ///< 轴对象 ID（modeling 项目对象——稳定身份）
    CaseId caseId;            ///< 工况 ID（同轴多工况分组——逐工况独立判定）
    /// 关节类型（轴级属性——默认 Revolute：R1 产品管线中 modeling
    /// MDL-12 已在链型入口阻断混合链，能流转到选型的轴事实缺省即全
    /// 旋转链成员；显式 Prismatic 是上游的范围外信号——WP-19-T08。
    /// 默认值使 T04~T07 既有构造零破坏；dynamics 收编时按其卡对齐）。
    JointKind jointKind = JointKind::Revolute;

    // ---- 关节侧（减速器筛选消费——DYN-03 口径值传递）----
    std::optional<double> jointTorqueRms;   ///< 关节侧 RMS 转矩，单位 N·m
    std::optional<double> jointTorquePeak;  ///< 关节侧峰值转矩，单位 N·m
    std::optional<double> jointSpeedPeak;   ///< 关节侧峰值角速度，单位 rad/s

    // ---- 电机侧（电机筛选消费——映射工作点事实；组合校核段供给）----
    std::optional<double> motorTorqueRms;   ///< 电机侧 RMS 转矩，单位 N·m
    std::optional<double> motorTorquePeak;  ///< 电机侧峰值转矩，单位 N·m
    std::optional<double> motorSpeedPeak;   ///< 电机侧峰值角速度，单位 rad/s
    std::optional<double> motorSpeedRms;    ///< 电机侧 RMS 角速度，单位 rad/s
    std::optional<double> motorPowerPeak;   ///< 电机侧峰值功率，单位 W
    std::optional<double> motorPowerRms;    ///< 电机侧 RMS 功率，单位 W
    std::optional<double> peakDuration;     ///< 峰值段持续时长，单位 s（过载
                                            ///   持续时间维度消费——卡 §7.1）

    // ---- 工况需求与轴侧事实 ----
    /// 保持工况需求保持转矩，单位 N·m（nullopt＝无保持工况需求→制动/
    /// 保持维度不适用——卡 §7.1"无保持工况需求则不适用"）。
    std::optional<double> requiredHoldingTorque;
    /// 外载荷事实（nullopt＝该轴未声明外载荷→外载荷维度不适用）。
    std::optional<ExternalLoadFacts> externalLoad;
    /// 关节安装关系要求（nullopt＝不限→安装维度不适用）。
    std::optional<JointMountRequirement> mountRequirement;

    double atTime = 0.0;      ///< 峰值工作点时间，单位 s（淘汰原因定位——§10.3）
    std::string segmentId;    ///< 轨迹段 ID（淘汰原因定位；无段上下文为空串）

    bool operator==(const AxisWorkpointFacts& o) const;
    bool operator!=(const AxisWorkpointFacts& o) const { return !(*this == o); }
};

// =====================================================================
// §14.4 IHardConstraintSelector（电机/减速器硬筛选——接口与唯一产品实现）
// =====================================================================

/**
 * @brief 硬筛选编排接口（卡 §14.4 设计基线——签名逐注承载）。
 *
 * 纯计算；不调用映射（电机侧维度的工作点由调用方供给——见文件头注
 * 边界 1）；逐维度独立判定、全量原因保留、稳定排序（卡 §14.4 @brief 注）。
 */
class IHardConstraintSelector {
public:
    virtual ~IHardConstraintSelector() = default;

    /**
     * @brief 电机硬筛选（卡 §7 全维度——SEL-03）。
     *
     * 对快照内每个候选电机 × facts 中每根唯一轴产出一条 FeasibilityRecord
     * （记录数＝唯一轴数 × 候选电机数；候选遍历序＝快照电机序〔modelId
     * 升序——装配保证〕，轴序＝facts 首现序——确定性，NFR-COR-02）。
     *
     * @param snapshot  [in] 目录快照（调用方持有；不可变共享）
     * @param axisFacts [in] 关节侧＋电机侧工作点事实（每条＝单轴单工况；
     *                  同轴多工况以多条并列——逐工况独立判定后合并记录）
     * @param criteria  [in] 筛选条件（进入切片身份——见 ScreeningCriteria 注）
     * @param ctx       [in] 取消查询（可空 nullptr；批次边界＝候选条目边界，
     *                  卡 §14.4"维度批次边界查询"——观测到取消即停止处理
     *                  剩余候选，返回已完成记录〔截断语义——调用方以记录数
     *                  对 候选数×轴数 感知截断；登记单元卡 §19.3 T04 ④〕）
     * @return 逐候选×逐轴 FeasibilityRecord（含全部独立原因与数据缺口）。
     *             移动关节轴（jointKind==Prismatic）的每条记录为
     *             "范围外"形态（WP-19-T08——SEL-09/D-SEL-15）：verdict＝
     *             DataInsufficient、reasons 恒空、gaps 恰含一条
     *             dimension="axis-out-of-scope" 的缺口（diagCode＝
     *             SEL-INPUT-AXIS-OUT-OF-SCOPE）——该轴不执行 §7 任何
     *             旋转传动维度判定（不静默套用旋转传动；工作点字段即使
     *             供给也不消费——不伪造电机工作点），记录数不变量
     *             （候选数×轴数）保持
     *
     * @throws std::invalid_argument 致命输入错误（调用方契约违约 fail-fast
     *         ——卡 §14.4 @throws 注：筛选条件非有限/安全系数＜1/工作点
     *         数值非有限/同轴多条 facts 的 jointKind 矛盾〔轴类型是轴级
     *         属性——既旋转又移动属物理矛盾〕；候选能力筛选不短路原则
     *         不受影响——短路仅允许在校验边界的致命输入错误，卡 §10.2）
     *
     * @note 纯函数；同输入恒同输出（NFR-COR-01/02）；可重入。
     */
    virtual std::vector<FeasibilityRecord> screenMotors(
        const CatalogPackageSnapshot& snapshot,
        const std::vector<AxisWorkpointFacts>& axisFacts,
        const ScreeningCriteria& criteria,
        const evidence::IEvaluationContext* ctx) const = 0;

    /**
     * @brief 减速器硬筛选（卡 §8 全维度——SEL-04；签名/遍历序/取消语义
     *        同 screenMotors；移动关节轴的范围外记录形态同 screenMotors
     *        ——WP-19-T08：该轴不执行 §8 任何旋转传动维度判定，速比
     *        换算 ω_m＝ω_joint/c 亦不适用〔移动关节轴无旋转速比语义〕）。
     *
     * 输入转速维度：ω_m_peak 优先取映射事实 motorSpeedPeak（"ω_m 来自
     * 映射工作点"）；映射事实未供给时以 ω_m＝ω_joint_peak/ratio 换算
     * （§8.2 明文允许的唯一自算——候选传动参数换算）。
     *
     * @throws std::invalid_argument 同 screenMotors（含同轴 jointKind
     *         矛盾 fail-fast）。
     * @note 纯函数；确定性；可重入。
     */
    virtual std::vector<FeasibilityRecord> screenGearboxes(
        const CatalogPackageSnapshot& snapshot,
        const std::vector<AxisWorkpointFacts>& axisFacts,
        const ScreeningCriteria& criteria,
        const evidence::IEvaluationContext* ctx) const = 0;

    /**
     * @brief 直线传动器件硬筛选（卡 §17.2 SEL-09-S1 选型层——WP-19-T12
     *        表尾追加；四类直线器件共用能力维度）。
     *
     * 维度集（对快照 linearDrives 表逐候选执行；facts 未供给的量产生
     * 数据缺口——不伪造工作点，与旋转侧同款纪律）：
     *   ①连续推力：forceRms ≤ rated_force_n（单位 N）；
     *   ②峰值推力：forcePeak ≤ peak_force_n（单位 N）；
     *   ③峰值推力-曲线口径：目录声明推力-速度曲线（owner=linear-drive、
     *     横坐标 linear-speed、纵坐标 force）时以 linearSpeedPeak 为查询
     *     点插值力上限——曲线查询拒绝〔含区间外外推拒绝 SEL-CURVE-
     *     EXTRAPOLATION-DENIED〕＝数据缺口（禁外推不放宽——§6.2 插值
     *     失败≠候选能力不足的分轨语义零变化）；插值成功且 forcePeak 超
     *     曲线上限＝峰值推力不足（阈值来源＝曲线 ID）；
     *   ④直线速度：linearSpeedPeak ≤ max_speed_ms（单位 m/s）；
     *   ⑤直线功率：powerPeak/powerRms ≤ rated_power_w（单位 W）；
     *   ⑥安全系数复判：criteria.safetyFactor＞1 时力/速度/功率 ×SF 后
     *     复判（与旋转侧同款语义——卡 §7.1）。
     *
     * @param snapshot  [in] 目录快照（linearDrives 表＝候选集；v1 包恒空
     *                  ——返回空集，v1 行为零变化）
     * @param axisFacts [in] 直线轴工作点事实（每条＝单轴单工况；类型化
     *                  广义量——drivetrain §16.2 扩展端口消费承载，值传递）
     * @param criteria  [in] 筛选条件（复用 ScreeningCriteria——直线通道
     *                  消费 safetyFactor；其余条件维度与直线能力面正交，
     *                  不消费不产生原因）
     * @param ctx       [in] 取消查询（可空 nullptr；候选条目边界查询——
     *                  语义同 screenMotors）
     * @return 逐候选×逐轴 FeasibilityRecord（deviceKind＝LinearDrive；
     *         记录数＝唯一轴数 × 候选直线器件数；候选序＝快照 linearDrives
     *         序〔modelId 升序——装配保证〕、轴序＝facts 首现序——确定性；
     *         原因稳定排序同 §10.4）
     *
     * @throws std::invalid_argument 致命输入错误（事实数值非有限/同轴
     *         多条 facts 矛盾无〔直线 facts 无轴级类型字段〕——非有限
     *         校验同 screenMotors 口径；筛选条件非有限/安全系数＜1）
     *
     * @note 纯函数；同输入恒同输出（NFR-COR-01/02）；可重入。
     * @note 分期边界：本方法输出**选型层资格事实**——MDL-12-S1（产品链
     *       正式计算）仍 R2 未启用；不产生 DeviceCombination（组合构造
     *       直线轴通道为卡 §17.2 预留）；既有旋转通道与组合校核对移动
     *       关节轴的范围外阻断零变化（SEL-09 R1 口径不放宽）。
     */
    virtual std::vector<FeasibilityRecord> screenLinearDrives(
        const CatalogPackageSnapshot& snapshot,
        const std::vector<LinearAxisWorkpointFacts>& axisFacts,
        const ScreeningCriteria& criteria,
        const evidence::IEvaluationContext* ctx) const = 0;
};

/**
 * @brief 硬筛选器唯一产品实现（IHardConstraintSelector——§7/§8 全维度；
 *        判定细则与公式的逐维登记见单元卡 §19.3 T04 落位细化 ③~⑨）。
 *
 * 状态：无成员状态（纯函数对象——可重入，卡 §14.10）。
 */
class HardConstraintSelector final : public IHardConstraintSelector {
public:
    std::vector<FeasibilityRecord> screenMotors(
        const CatalogPackageSnapshot& snapshot,
        const std::vector<AxisWorkpointFacts>& axisFacts,
        const ScreeningCriteria& criteria,
        const evidence::IEvaluationContext* ctx) const override;

    std::vector<FeasibilityRecord> screenGearboxes(
        const CatalogPackageSnapshot& snapshot,
        const std::vector<AxisWorkpointFacts>& axisFacts,
        const ScreeningCriteria& criteria,
        const evidence::IEvaluationContext* ctx) const override;

    // 直线传动器件硬筛选（WP-19-T12——实现见 src/LinearDrive.cpp；维度集
    // 与分期边界见接口注）。
    std::vector<FeasibilityRecord> screenLinearDrives(
        const CatalogPackageSnapshot& snapshot,
        const std::vector<LinearAxisWorkpointFacts>& axisFacts,
        const ScreeningCriteria& criteria,
        const evidence::IEvaluationContext* ctx) const override;
};

}  // namespace sdurws::ird::selection

#endif  // IRD_SELECTION_SCREENING_HPP
