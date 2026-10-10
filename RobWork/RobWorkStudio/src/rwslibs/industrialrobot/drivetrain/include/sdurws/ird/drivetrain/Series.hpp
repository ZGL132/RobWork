/**
 * @file   Series.hpp
 * @brief  传动映射序列与电机侧工作点值类型（units/drivetrain.md §10/§11/
 *         §12.1）——关节侧输入序列视图 JointSeriesView（P-DT-6 提议契约的
 *         本卡消费面）、电机侧映射序列 MotorSeries、工作点统计
 *         MotorOperatingPoint（§11.1 字段表）、四象限统计（§10.6）、能量
 *         分项（§10.5）、反射惯量结果（§9）与映射总输出
 *         DriveTrainMappingOutput。
 *
 * 设计依据：
 *   - units/drivetrain.md §6.2（R1 映射定义）、§8.3（功率符号分类）、
 *     §9.2/§9.5（反射惯量与惯量比）、§10.2～§10.7（效率/功率/能量/四象限/
 *     数据不足）、§11.1（MotorOperatingPoint 字段表——selection 消费口径
 *     ＝reporting 展示口径）、§12.1（DynamicsJointSeries 提议 DTO——P-DT-6）
 *   - 需求 DYN-04（电机侧工作点 τ/ω/P、η±、反射惯量 J·i²、惯量比、能量
 *     分项与四象限工作制统计）、DYN-03（峰值窗口径/RMS 完整循环口径的
 *     电机侧镜像）、ERR-01（不适用字段显式标记）
 *   - 任务契约 tasks/foundation/WP-18-T03.json（acceptance 1 输出面）
 *
 * 口径声明（不得混用——卡 §8.2 四口径在本结构的字段落位）：
 *   - pJoint＝τ_joint·q̇（关节侧机械功率，逐元素——§8.3；本卡按 τ·q̇ 逐
 *     元素派生，不消费上游独立 P_joint 字段【P-DT-6 对齐面，登记单元卡
 *     §18.3】，保证虚功口径自洽）；
 *   - tauIdeal＝c·τ_joint（理想虚功映射——口径①，无损耗无转子项）；
 *   - pTransmission＝f_dir(pJoint)（效率折算后的**传动**功率——口径③，
 *     P_joint＞0 除以 η⁺、＜0 乘以 η⁻、＝0 为零【效率不适用】）；
 *   - pRotor＝J_rotor·θ̈·θ̇（转子功率项——§10.3，分项报告不混写）；
 *   - tauMotor＝tauIdeal＋J_rotor·θ̈（M-12 全量口径——口径④）；
 *   - pMotor＝pTransmission＋pRotor（电机侧总机械功率——§10.3 冻结口径）。
 *   E_loss（传动箱损耗）按 ∫(pTransmission−pJoint)dt 计（恒＞0——两方向
 *   折算均消耗；卡 §10.5 公式记 P_motor−P_joint，与 §10.3 含转子项口径
 *   联立时该差不恒为正，按"传动箱损耗"物理语义与"＞0 恒成立"约束取
 *   传动功率差——单元卡 §18.3 口径澄清登记）。
 *
 * 错误语义：本文件全部为纯值类型（无校验入口）；数据类降级（效率缺失/
 *   转子缺失/负载惯量缺失/循环不完整）以 optional/CompletenessState/
 *   missingItems 显式承载（ERR-01 不适用标记），由映射核心填充——不伪造
 *   数值（NFR-COR-03）。
 * 线程安全：纯值类型；输出对象按值返回（卡 §13.9）。
 */

#ifndef IRD_DRIVETRAIN_SERIES_HPP
#define IRD_DRIVETRAIN_SERIES_HPP

#include <sdurws/ird/core/DiagData.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/drivetrain/MappingTypes.hpp>

#include <optional>
#include <string>
#include <vector>

namespace sdurws::ird::drivetrain {

// =====================================================================
// 关节侧输入序列（卡 §12.1 提议契约的本卡消费面——P-DT-6）
// =====================================================================

/**
 * @brief 关节侧单样本（单时刻采样——上游 dynamics 序列的逐元素承载）。
 *
 * 单位（卡 §5.4 单位表）：t 秒（s）；q rad；q̇ rad/s；q̈ rad/s²；
 * τ_joint N·m（类型化广义力的转动侧——移动关节在 R1 已被阻断面拒绝，
 * 不存在 N 侧样本）。样本值必须全部有限（NFR-COR-03——非有限即拒绝，
 * 映射入口 fail-fast）。
 */
struct JointDriveSample {
    double t = 0.0;     ///< 采样时刻（s；序列内严格单调递增——统计面前提）
    double q = 0.0;     ///< 关节位置（rad）
    double qd = 0.0;    ///< 关节速度（rad/s）
    double qdd = 0.0;   ///< 关节加速度（rad/s²）
    double tauJoint = 0.0; ///< 关节侧广义力矩（N·m；含摩擦/重力——RNEA 权威，本卡不叠加）
    std::string segmentId{}; ///< 所在轨迹段 ID（上游携带、原样保持——§12.1"轨迹段/工况分组原样保持"；峰值来源定位面）

    bool operator==(const JointDriveSample& o) const noexcept
    {
        return t == o.t && q == o.q && qd == o.qd && qdd == o.qdd
            && tauJoint == o.tauJoint && segmentId == o.segmentId;
    }
    bool operator!=(const JointDriveSample& o) const noexcept { return !(*this == o); }
};

/**
 * @brief 关节侧输入序列视图（单工况——多工况按视图逐次评估，不混算）。
 *
 * 卡 §12.1 处理规则落位：采样逐时间戳对齐（本卡不重采样、不插值——上游
 * 序列即对齐基准）；轨迹段/工况分组原样保持（segmentId 逐样本携带）。
 * P-DT-6：字段名以本卡提议契约为准（jointIds/caseId/samples），dynamics
 * 卡产出时如更名按注册清单同步；上游独立 P_joint 字段暂不消费（本卡按
 * τ·q̇ 逐元素派生关节侧功率——虚功口径自洽，偏差登记单元卡 §18.3）。
 *
 * 线程安全：纯值；调用方持有（evaluate 期间不修改——卡 §13.1 @param 契约）。
 */
struct JointSeriesView {
    std::vector<core::ObjectId> jointIds{}; ///< 关节对象 ID（串联序；与模型 jointAxes 一致）
    core::ObjectId caseId{};                ///< 所属工况 ID（批次边界＝工况边界——卡 §12.4）
    std::vector<JointDriveSample> samples{}; ///< 逐时刻样本（t 严格递增；等长语义由结构保证）
    /// 上游结果切片身份（dynamics 关节侧序列来源——进结果身份链，§5.5）。
    core::ContentIdentity upstreamSliceId{};

    bool operator==(const JointSeriesView& o) const
    {
        return jointIds == o.jointIds && caseId == o.caseId
            && samples == o.samples && upstreamSliceId == o.upstreamSliceId;
    }
    bool operator!=(const JointSeriesView& o) const { return !(*this == o); }
};

// =====================================================================
// 电机侧映射序列（§6.2 逐样本逐轴输出）
// =====================================================================

/**
 * @brief 四象限归属（§10.6 表——ω＝θ̇、P＝τ_motor·θ̇ 的符号组合）。
 *
 * 卡 §10.6：Q1 正转电动（ω＞0 且 P＞0）；Q2 正转再生（ω＞0 且 P＜0）；
 * Q3 反转电动（ω＜0 且 P＜0）；Q4 反转再生（ω＜0 且 P＞0）；零速/零功率
 * （ω＝0 或 P＝0）＝驻留/保持——单独计数，不计入象限能量。符号判定为
 * 精确判据（浮点严格比较，无阈值——§10.2 同源纪律）。
 */
enum class Quadrant : std::uint8_t {
    Q1,        ///< 正转电动（ω＞0，P＞0）
    Q2,        ///< 正转再生（ω＞0，P＜0）
    Q3,        ///< 反转电动（ω＜0，P＜0）
    Q4,        ///< 反转再生（ω＜0，P＞0）
    ZeroDwell, ///< 零速/零功率（驻留/保持——时间占比单独统计）
};

/**
 * @brief 电机侧单样本（单轴单时刻——§6.2 映射表的全列输出）。
 *
 * 字段口径见文件头"口径声明"；效率折算方向 efficiencyApplicable＝false
 * 表示该样本 P_joint＝0（精确零）——效率不适用（ERR-01 显式标记，不伪造
 * 数值；pTransmission＝0，卡 §10.2），且该样本不计入能量分项与四象限
 * 统计的有效样本集合（§8.3/§10.6）。
 */
struct MotorDriveSample {
    double t = 0.0;        ///< 采样时刻（s；与输入样本同源对齐）
    double theta = 0.0;        ///< 电机位置（rad；含零位偏置 θ_off——只影响绝对位置）
    double thetaDot = 0.0;     ///< 电机速度（rad/s）
    double thetaDDot = 0.0;    ///< 电机加速度（rad/s²）
    double tauIdeal = 0.0;     ///< 理想映射电机力矩 c·τ_joint（N·m——口径①）
    double tauMotor = 0.0;     ///< 含转子项电机力矩 tauIdeal＋J_rotor·θ̈（N·m——M-12 口径④）
    double pJoint = 0.0;       ///< 关节侧机械功率 τ_joint·q̇（W——逐元素）
    double pTransmission = 0.0;///< 效率折算后传动功率 f_dir(pJoint)（W——口径③）
    double pRotor = 0.0;       ///< 转子功率项 J_rotor·θ̈·θ̇（W——§10.3 分项）
    double pMotor = 0.0;       ///< 电机侧总机械功率 pTransmission＋pRotor（W——§10.3）
    bool efficiencyApplicable = false; ///< 效率是否适用（false＝P_joint 精确零——显式标记）
    Quadrant quadrant = Quadrant::ZeroDwell; ///< 四象限归属（§10.6；零功率样本恒 ZeroDwell）

    bool operator==(const MotorDriveSample& o) const noexcept
    {
        return t == o.t && theta == o.theta && thetaDot == o.thetaDot
            && thetaDDot == o.thetaDDot && tauIdeal == o.tauIdeal
            && tauMotor == o.tauMotor && pJoint == o.pJoint
            && pTransmission == o.pTransmission && pRotor == o.pRotor
            && pMotor == o.pMotor && efficiencyApplicable == o.efficiencyApplicable
            && quadrant == o.quadrant;
    }
    bool operator!=(const MotorDriveSample& o) const noexcept { return !(*this == o); }
};

/**
 * @brief 单电机轴的映射序列（逐时刻输出序列＋轴身份）。
 */
struct MotorSeries {
    core::ObjectId axisId{};               ///< 电机轴对象 ID（模型 motorAxes[k].motorId）
    std::size_t jointIndex = 0;            ///< 对应关节轴下标
    std::vector<MotorDriveSample> samples{}; ///< 逐时刻电机侧样本（与输入序列逐时刻对齐）

    bool operator==(const MotorSeries& o) const
    {
        return axisId == o.axisId && jointIndex == o.jointIndex && samples == o.samples;
    }
    bool operator!=(const MotorSeries& o) const { return !(*this == o); }
};

// =====================================================================
// 峰值/能量/四象限统计值类型（§10.4～§10.6、§11.1）
// =====================================================================

/**
 * @brief 峰值记录（DYN-03 峰值窗口径的电机侧镜像——卡 §10.4）。
 *
 * 必须携带发生时刻、所在轨迹段、所属工况（"峰值不带来源即非法输出"
 * ——卡 §11.2 构造边界；本结构由统计器填充，时刻/段/工况不可省）。
 */
struct PeakRecord {
    bool present = false;      ///< 是否存在（样本集为空/全部零值时 false——不伪造）
    double value = 0.0;        ///< 峰值（量纲由使用处字段标注：N·m/rad/s/W）
    double t = 0.0;            ///< 发生时刻（s——完整循环区间内）
    std::string segmentId{};   ///< 所在轨迹段 ID（上游序列携带——可定位）
    core::ObjectId caseId{};   ///< 所属工况（多工况不混取——§11.2）

    bool operator==(const PeakRecord& o) const
    {
        return present == o.present && value == o.value && t == o.t
            && segmentId == o.segmentId && caseId == o.caseId;
    }
    bool operator!=(const PeakRecord& o) const { return !(*this == o); }
};

/**
 * @brief 能量分项（§10.5——逐轴、逐工况；梯形积分，非均匀采样按逐对区间
 *        积分，缺样本区间不外推）。
 *
 * 口径见文件头"口径声明"；单位全部为焦耳（J）。eLoss 为传动箱损耗
 * （∫(pTransmission−pJoint)dt，恒＞0）；eRegen 为机械再生功（不声明为可
 * 回馈电能——§8.3 限定语在报告侧保留）。
 */
struct EnergyBreakdown {
    double eMotor = 0.0;  ///< 电机侧总机械能量 ∫pMotor dt（J）
    double eJoint = 0.0;  ///< 关节侧机械能量 ∫pJoint dt（J）
    double eLoss = 0.0;   ///< 传动箱损耗 ∫(pTransmission−pJoint) dt（J；恒＞0）
    double eRegen = 0.0;  ///< 再生功 ∫max(0,−pJoint) dt（J；机械口径）
    double ePos = 0.0;    ///< 正功 ∫max(0, pJoint) dt（J）
    double eRotor = 0.0;  ///< 转子动能往返 ∫pRotor dt（J；往返净额≈0，分项呈现）

    bool operator==(const EnergyBreakdown& o) const noexcept
    {
        return eMotor == o.eMotor && eJoint == o.eJoint && eLoss == o.eLoss
            && eRegen == o.eRegen && ePos == o.ePos && eRotor == o.eRotor;
    }
    bool operator!=(const EnergyBreakdown& o) const noexcept { return !(*this == o); }
};

/**
 * @brief 单象限统计（§10.6——时间占比、能量、峰值）。
 *
 * 时间占比分母＝完整循环时长 T（§10.4 口径）；energy 只累计该象限样本的
 * 关节侧机械功净额（∫pJoint dt 限该象限样本——象限能量分项）。零速/零
 * 功率（ZeroDwell）不计入象限能量（卡 §10.6 表行 5）。
 */
struct QuadrantStats {
    double timeShare = 0.0; ///< 时间占比（无量纲 [0,1]；分母＝循环时长 T）
    double energy = 0.0;    ///< 该象限 ∫pJoint dt（J；ZeroDwell 恒 0）
    std::size_t sampleCount = 0; ///< 该象限样本数（含 ZeroDwell——驻留计数面）

    bool operator==(const QuadrantStats& o) const noexcept
    {
        return timeShare == o.timeShare && energy == o.energy && sampleCount == o.sampleCount;
    }
    bool operator!=(const QuadrantStats& o) const noexcept { return !(*this == o); }
};

/**
 * @brief 完整性状态（§11.1 quality 字段——Complete/Partial/Estimated 三态）。
 *
 * 卡 §11.1："Complete / Partial（附缺失清单）/ Estimated（附来源）"。
 * Estimated 与 Partial 可叠加（估算来源＋缺失并存）——以
 * estimatedSource 标记单独承载，quality 只取 Complete/Partial 两值＋
 * estimatedSource 有值＝含估算限定语。
 */
enum class CompletenessState : std::uint8_t {
    Complete, ///< 完整（无缺失输入）
    Partial,  ///< 部分（缺失清单非空——功率/能量/四象限等降级面如实列出）
};

// =====================================================================
// 反射惯量与效率结果（§9/§10——步骤接口的输出承载）
// =====================================================================

/**
 * @brief 单轴反射惯量与惯量比结果（§9.2/§9.5——数值事实，无阈值判定）。
 *
 * jReflectedJointSide＝J_rotor/c²（kg·m²，关节轴系——DYN-04 记法 J·i²，
 * i＝1/c 换算）。inertiaRatio＝c²·J_load@joint/J_rotor（无量纲；
 * P-DT-2：阈值判定归 selection，本卡只输出数值）。
 * windowProjected（R2，WP-18-T05）：窗口轴的对角视图是**投影值**——完整
 * 反射惯量矩阵含交叉惯量项（§9.3），单轴比值为投影值须附本限定标记
 * （§9.5"窗口内存在交叉项、单轴比值为投影值"）；自由轴恒 false。
 */
struct ReflectedInertiaAxis {
    std::size_t jointIndex = 0;      ///< 对应关节轴下标
    double jReflectedJointSide = 0.0;///< 反射惯量（kg·m²，关节轴系对角视图）
    /// 惯量比（无量纲；nullopt＝负载折算惯量缺失→不适用——§10.7 显式标记）。
    std::optional<double> inertiaRatio{};
    bool windowProjected = false;    ///< R2 窗口轴投影限定（§9.5；R1 恒 false）

    bool operator==(const ReflectedInertiaAxis& o) const
    {
        return jointIndex == o.jointIndex && jReflectedJointSide == o.jReflectedJointSide
            && inertiaRatio == o.inertiaRatio && windowProjected == o.windowProjected;
    }
    bool operator!=(const ReflectedInertiaAxis& o) const { return !(*this == o); }
};

/**
 * @brief 反射惯量评估结果（逐电机轴——§13.4 IReflectedInertiaEvaluator
 *        输出面）。
 *
 * jointSideFullMatrix（R2，WP-18-T05）：关节轴系**完整**反射惯量矩阵
 * J_ref＝(C⁻¹)ᵀ·diag(J_rotor)·C⁻¹（§9.3——对称正定，含交叉惯量项；
 * n×n，行列均按关节串联序）。§9.3 纪律：反射惯量不默认对角化——交叉项
 * 保留在完整矩阵中随结果归档（selection 只需单轴数值时消费 axes 对角
 * 视图，不因单轴消费丢弃交叉项）。R1 无耦合链无交叉项可丢——保持无值
 * （nullopt），对角视图即完整口径。
 */
struct ReflectedInertiaResult {
    std::vector<ReflectedInertiaAxis> axes{}; ///< 逐电机轴结果（下标＝电机轴序）
    /// R2 完整关节轴系反射惯量矩阵（kg·m²；§9.3——R1 恒 nullopt）。
    std::optional<RowMatrix> jointSideFullMatrix{};

    bool operator==(const ReflectedInertiaResult& o) const
    {
        return axes == o.axes && jointSideFullMatrix == o.jointSideFullMatrix;
    }
    bool operator!=(const ReflectedInertiaResult& o) const { return !(*this == o); }
};

// =====================================================================
// 电机工作点（§11.1 字段表——selection 消费口径＝reporting 展示口径）
// =====================================================================

/**
 * @brief 单电机轴工作点统计（§11.1 字段表的 R1 承载）。
 *
 * 字段取舍说明（诚实边界）：§11.1 表的 caseId/segmentId 逐条输出（多工况
 * 不合并成无来源最大值——§11.2）；constraintRefs（力矩限值等参考字段的
 * 对象引用）以 ratedTorqueSource 承载来源标记（本卡经归一化模型值传递
 * 消费限值字段，无独立对象引用可携带——§5.2 ratedTorque 字段注；完整
 * 引用面随 WP-19 组合场景落位）。identity 身份块由 DriveTrainMappingOutput
 * 统一携带（逐轴重复无信息量——卡 §11.1 identity 行的聚合承载）。
 *
 * ★ 本结构只输出数值事实：不判限位/不判超限（校验归消费域——卡 §6.2/
 *   §11.3）、惯量比不内嵌阈值（P-DT-2）。
 */
struct MotorOperatingPoint {
    core::ObjectId axisId{};    ///< 电机轴身份（§11.1 axisId）
    core::ObjectId jointId{};   ///< 对应关节（§11.1 jointId）
    std::size_t jointIndex = 0; ///< 对应关节下标（§11.1 jointIndex）

    PeakRecord tauPeakPos{};  ///< 最大驱动转矩（N·m；正峰值——不与负峰值混取绝对值）
    PeakRecord tauPeakNeg{};  ///< 最大再生转矩（N·m；负峰值按负值报告）
    PeakRecord omegaPeak{};   ///< 速度峰值 |θ̇| 最大（rad/s；value 报告带符号实测值）
    PeakRecord powerPeak{};   ///< 功率峰值 |P_motor| 最大（W；value 报告带符号实测值）

    double tauRms = 0.0;    ///< 转矩 RMS（N·m；完整循环含驻留——√(∫τ²dt/T)）
    double omegaRms = 0.0;  ///< 速度 RMS（rad/s；同上口径）
    /// 负载率 tauRms/ratedTorque（无量纲；参考值——nullopt＝额定值缺失→
    /// 不适用；§10.4"参考值呈现，硬筛选归 selection"）。
    std::optional<double> loadRatio{};

    /// 本次计算使用的 η⁺/η⁻（nullopt＝该轴效率缺失→功率/能量降级；
    /// §11.1"含来源标记"）。
    std::optional<EfficiencyModel> etaApplied{};

    double reflectedInertia = 0.0; ///< 反射惯量 J_rotor/c²（kg·m²，关节轴系对角视图）
    /// 惯量比（无量纲；nullopt＝负载折算惯量缺失→不适用；§9.5 数值事实）。
    std::optional<double> inertiaRatio{};

    QuadrantStats q1{}; ///< Q1 正转电动统计（§10.6）
    QuadrantStats q2{}; ///< Q2 正转再生统计
    QuadrantStats q3{}; ///< Q3 反转电动统计
    QuadrantStats q4{}; ///< Q4 反转再生统计
    QuadrantStats zeroDwell{}; ///< 零速/零功率（驻留/保持——时间占比，不计象限能量）

    EnergyBreakdown energy{}; ///< 能量分项（§10.5；降级时保持 0 并由 quality/缺失清单标注）

    core::ObjectId caseId{};  ///< 所属工况（多工况逐条输出——§11.1 caseId 行）
    std::string segmentId{};  ///< 峰值所在轨迹段冗余呈现（峰值来源可定位——§11.2）

    CompletenessState quality = CompletenessState::Complete; ///< 完整性状态
    std::vector<std::string> missingItems{}; ///< 缺失清单（Partial 必非空——如 "efficiency[j=0]"）
    bool estimatedSource = false;            ///< 含估算来源（η/转子/负载惯量为 Estimated→限定语）

    bool operator==(const MotorOperatingPoint& o) const
    {
        return axisId == o.axisId && jointId == o.jointId && jointIndex == o.jointIndex
            && tauPeakPos == o.tauPeakPos && tauPeakNeg == o.tauPeakNeg
            && omegaPeak == o.omegaPeak && powerPeak == o.powerPeak
            && tauRms == o.tauRms && omegaRms == o.omegaRms && loadRatio == o.loadRatio
            && etaApplied == o.etaApplied && reflectedInertia == o.reflectedInertia
            && inertiaRatio == o.inertiaRatio && q1 == o.q1 && q2 == o.q2
            && q3 == o.q3 && q4 == o.q4 && zeroDwell == o.zeroDwell
            && energy == o.energy && caseId == o.caseId && segmentId == o.segmentId
            && quality == o.quality && missingItems == o.missingItems
            && estimatedSource == o.estimatedSource;
    }
    bool operator!=(const MotorOperatingPoint& o) const { return !(*this == o); }
};

// =====================================================================
// 映射总输出（§13.1 evaluate 返回值）
// =====================================================================

/**
 * @brief 传动映射总输出（卡 §13.1——逐工况分组的电机侧序列＋工作点＋
 *        反射惯量＋诊断＋完整性）。
 *
 * 身份块：绑定 DriveTrainIdentity＋上游切片身份＋算法/契约版本（§6.2
 * "结果身份"行——消费方据此核对同一矩阵内容身份，§7.4-4）。
 * 阻断结果的表达：调用方错误（结构非法）以异常 fail-fast，不会产生
 * output；数据类降级（缺失输入）产出 output 但 completeness=Partial＋
 * 诊断列表非空——不伪装完整结果（卡 §6.3"阻断结果不伪装"同源纪律）。
 */
struct DriveTrainMappingOutput {
    std::vector<MotorSeries> motorSeries{};        ///< 电机侧序列（逐轴，下标＝电机轴序）
    std::vector<MotorOperatingPoint> points{};     ///< 工作点统计（逐轴，下标＝电机轴序）
    ReflectedInertiaResult inertia{};              ///< 反射惯量与惯量比（§9）
    std::vector<core::DiagnosticRecord> diagnostics{}; ///< 数据类诊断（DT-*；比较型含实际/期望/单位）
    CompletenessState completeness = CompletenessState::Complete; ///< 整体完整性
    std::vector<std::string> missingItems{};       ///< 缺失清单（轴前缀定位——§13.8 示例形态）
    DriveTrainIdentity identity{};                 ///< 传动配置身份（§5.5）
    core::ContentIdentity upstreamSliceId{};       ///< 上游切片身份（§5.6 输入身份分离面）
    std::uint32_t algorithmVersion = 0;            ///< 映射算法版本（快照冗余——追溯面）
    std::uint32_t contractVersion = 0;             ///< 契约版本（快照冗余——CON-04 面）
    core::ObjectId caseId{};                       ///< 本输出的工况（单工况一次评估）

    bool operator==(const DriveTrainMappingOutput& o) const
    {
        return motorSeries == o.motorSeries && points == o.points && inertia == o.inertia
            && diagnostics == o.diagnostics && completeness == o.completeness
            && missingItems == o.missingItems && identity == o.identity
            && upstreamSliceId == o.upstreamSliceId && algorithmVersion == o.algorithmVersion
            && contractVersion == o.contractVersion && caseId == o.caseId;
    }
    bool operator!=(const DriveTrainMappingOutput& o) const { return !(*this == o); }
};

}  // namespace sdurws::ird::drivetrain

#endif  // IRD_DRIVETRAIN_SERIES_HPP
