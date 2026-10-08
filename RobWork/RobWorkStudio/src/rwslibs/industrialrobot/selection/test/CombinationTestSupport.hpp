/**
 * @file   CombinationTestSupport.hpp
 * @brief  组合校核测试支撑（selection 单元测试私有头——不跨单元暴露）：
 *         两轴黄金目录（电机 M-A/M-B＋减速器 G-10/G-20＋兼容表）、黄金
 *         工作点事实、映射批事实与组合集构造。
 *
 * 设计依据：
 *   - units/selection.md §9（组合校核）、§14.5（组合构造）、§11.3（惯量比
 *     未判定）、§15.2 V5 组（组合校核与可行集故障注入矩阵）
 *   - 需求 SEL-05（经共享映射校核——电机侧黄金值为映射口径解析值：理想
 *     虚务口径 τ_m＝c·τ_joint、ω_m＝ω_joint/c〔drivetrain 卡 §6.2 口径①/
 *     位置映射〕——黄金联动的 selection 侧消费面）、NFR-COR-01
 *   - 任务契约 tasks/foundation/WP-19-T05.json acceptance 1
 *
 * 黄金值口径：全部为一位小数以内的解析算例（附录 D 精神）——例如
 * K1/J1 电机侧峰值转矩＝c×τ_joint＝0.1×100＝10.0 N·m、峰值转速＝
 * ω_joint/c＝10/0.1＝100.0 rad/s；判定边界由 T04 黄金表（每维"刚好
 * 满足/不足"）背书，本文件只钉组合装配与组合级维度。
 *
 * 线程安全：纯值构造（无共享状态）。
 */

#ifndef IRD_SELECTION_COMBINATIONTESTSUPPORT_HPP
#define IRD_SELECTION_COMBINATIONTESTSUPPORT_HPP

#include <string>
#include <vector>

#include <sdurws/ird/selection/CatalogTypes.hpp>
#include <sdurws/ird/selection/Combination.hpp>
#include <sdurws/ird/selection/Screening.hpp>

namespace sdurws::ird::selection::testsupport {

// =====================================================================
// 黄金目录条目（cat-sel v5——每用例独立副本，防互染）
// =====================================================================

/// 黄金目录身份（组合级/快照级共用——一致性维度的不一致用例以副本改值）。
inline CatalogIdentity goldenCatalog()
{
    CatalogIdentity c;
    c.catalogId = "cat-sel";
    c.version = "v5";
    c.source = "组合校核黄金目录";
    // contentIdentity 由快照装配口径回填（评估器路径经
    // computePackageContentIdentity 计算后锁定；直调路径零值亦可——
    // 身份一致性按 CatalogIdentity 全字段比较，两侧同源即一致）。
    return c;
}

/// 黄金电机 M-A：基准可行型号（全维度通过——黄金组合 K1/K2 的电机面）。
inline MotorCatalogEntry makeMotorA()
{
    MotorCatalogEntry m;
    m.modelId = "M-A";
    m.vendor = "Sel";
    m.displayName = "黄金电机 A";
    m.catalog = goldenCatalog();
    m.ratedTorque = 5.0;                  // 额定连续转矩，N·m
    m.peakTorque = 15.0;                  // 峰值转矩，N·m
    m.ratedSpeed = 150.0;                 // 额定转速，rad/s
    m.maxSpeed = 300.0;                   // 最高转速，rad/s
    m.ratedPower = 1500.0;                // 额定功率，W
    m.overload = OverloadSpec{15.0, 10.0}; // 过载 15 N·m 允许 10 s（触发式维窗口）
    m.dutyClass = "S1";
    m.rotorInertia = 0.01;                // 转子惯量，kg·m²（映射输入——组合校核不自算）
    m.mass = 3.0;                         // 质量，kg
    m.mounting = MountSpec{"flangeA", "shaftB"};
    m.status = ValidationStatus::Valid;
    return m;
}

/// 黄金电机 M-B：小机座（连续/峰值转矩不足的淘汰面——K3 组合 J1 轴）。
inline MotorCatalogEntry makeMotorB()
{
    MotorCatalogEntry m;
    m.modelId = "M-B";
    m.vendor = "Sel";
    m.displayName = "小机座电机 B";
    m.catalog = goldenCatalog();
    m.ratedTorque = 2.0;                  // N·m（J1 工作点 4.0 → 连续转矩不足）
    m.peakTorque = 8.0;                   // N·m（J1 工作点 10.0 → 峰值不足）
    m.ratedSpeed = 150.0;                 // rad/s
    m.maxSpeed = 300.0;                   // rad/s
    m.ratedPower = 800.0;                 // W
    m.overload = OverloadSpec{8.0, 10.0}; // s
    m.dutyClass = "S1";
    m.rotorInertia = 0.02;                // kg·m²
    m.mass = 4.0;                         // kg
    m.mounting = MountSpec{"flangeA", "shaftB"};
    m.status = ValidationStatus::Valid;
    return m;
}

/// 黄金减速器 G-10：n＝10（c＝0.1——K1/K3 组合面）。
inline GearboxCatalogEntry makeGearbox10()
{
    GearboxCatalogEntry g;
    g.modelId = "G-10";
    g.vendor = "Sel";
    g.displayName = "黄金减速器 10:1";
    g.catalog = goldenCatalog();
    g.ratedOutputTorque = 200.0;          // 额定输出转矩，N·m（输出轴系）
    g.peakOutputTorque = 400.0;           // 峰值输出转矩，N·m
    g.maxInputSpeed = 300.0;              // 允许输入转速，rad/s
    g.ratio = 10.0;                       // 速比 n:1（无量纲；c＝1/n＝0.1）
    g.efficiency = 0.9;                   // 效率（无量纲）
    g.backlash = 0.001;                   // 回程间隙，rad
    g.mountingOrientation = "flangeA";
    g.mass = 2.0;                         // kg
    g.mounting = MountSpec{"flangeA", "shaftB"};
    g.status = ValidationStatus::Valid;
    return g;
}

/// 黄金减速器 G-20：n＝50（c＝0.02——K2 组合面；输入转速超限的淘汰面）。
inline GearboxCatalogEntry makeGearbox20()
{
    GearboxCatalogEntry g;
    g.modelId = "G-20";
    g.vendor = "Sel";
    g.displayName = "黄金减速器 50:1";
    g.catalog = goldenCatalog();
    g.ratedOutputTorque = 100.0;          // N·m
    g.peakOutputTorque = 200.0;           // N·m
    g.maxInputSpeed = 300.0;              // rad/s（K2/J1 映射事实 500 → 超限）
    g.ratio = 50.0;                       // n:1（c＝0.02）
    g.efficiency = 0.8;
    g.backlash = 0.002;                   // rad
    g.mountingOrientation = "flangeA";
    g.mass = 1.5;                         // kg
    g.mounting = MountSpec{"flangeA", "shaftB"};
    g.status = ValidationStatus::Valid;
    return g;
}

/// 兼容表： (M-A,G-10)／(M-A,G-20)／(M-B,G-10)——(M-B,G-20) 无记录
/// （构造过滤面：B+G20 对不生成组合；校核对的双保险面）。
inline CompatibilityTable goldenCompat()
{
    return {
        CompatibilityRecord{"M-A", "G-10", "flangeA"},
        CompatibilityRecord{"M-A", "G-20", "flangeA"},
        CompatibilityRecord{"M-B", "G-10", "flangeA"},
    };
}

/// 黄金快照（四表装配——条目序＝modelId 升序，与 T03 装配序一致；
/// contentIdentity 由调用方按需回填（评估器路径须计算后锁定））。
inline CatalogPackageSnapshot goldenSnapshot()
{
    CatalogPackageSnapshot s;
    s.manifest.formatVersion = kCatalogFormatVersion;
    s.manifest.identity = goldenCatalog();
    s.motors = {makeMotorA(), makeMotorB()};      // modelId 升序
    s.gearboxes = {makeGearbox10(), makeGearbox20()}; // modelId 升序
    s.compatibility = goldenCompat();
    return s;
}

// =====================================================================
// 黄金轴与工作点事实
// =====================================================================

/// 黄金轴对象 ID（每用例固定生成两根——J1/J2；确定性由调用方持有副本）。
struct GoldenAxes {
    core::ObjectId j1;  ///< 轴 1（大负载轴——淘汰维度的定位面）
    core::ObjectId j2;  ///< 轴 2（轻载轴——全维通过面）
};

inline GoldenAxes makeGoldenAxes()
{
    return GoldenAxes{core::ObjectId::generate(), core::ObjectId::generate()};
}

/// 黄金三组合夹具（builder 产出——组合键/序与校核输入同源；各测试文件
/// 共用的展开口径：J1 候选 M-A/M-B×G-10/G-20，J2 候选 M-A×G-10/G-20）。
struct GoldenCombos {
    std::vector<DeviceCombination> combos;  ///< 黄金组合集（builder 展开序）
    GoldenAxes axes;                        ///< J1/J2
};

/// 关节侧工作点（DYN-03 口径——与组合无关；黄金值：J1 τ_peak=100/
/// τ_rms=40/ω_peak=10；J2 减载四分之一）。
inline AxisWorkpointFacts makeJointFacts(const core::ObjectId& axis,
                                         const CaseId& caseId, bool heavy)
{
    AxisWorkpointFacts f;
    f.jointId = axis;
    f.caseId = caseId;
    f.jointTorquePeak = heavy ? 100.0 : 20.0;  // N·m（关节侧——减速器维度消费）
    f.jointTorqueRms = heavy ? 40.0 : 10.0;    // N·m
    f.jointSpeedPeak = heavy ? 10.0 : 5.0;     // rad/s
    // 电机侧字段在组合校核路径以映射批为权威（P-SEL-1 消费面边界）——
    // 本支撑不填充（组装方无须填充的形态自证）。
    f.atTime = 1.5;        // s
    f.segmentId = "seg-A";
    return f;
}

/// 电机侧映射事实（dt.mapping 批口径——组合 c 决定的解析值）。
/// 黄金口径：τ_m_peak＝c×τ_joint_peak；τ_m_rms＝c×τ_joint_rms；
/// ω_m_peak＝ω_joint_peak/c；ω_m_rms＝ω_joint_rms×0.8（轻载曲线简化）。
inline MappingAxisFact makeMappingFact(const DeviceCombinationId& comboId,
                                       const core::ObjectId& axis,
                                       const CaseId& caseId, double c,
                                       bool heavy, double inertiaRatio,
                                       double reflected)
{
    MappingAxisFact a;
    a.combinationId = comboId;
    a.jointId = axis;
    a.caseId = caseId;
    const double tauPeak = (heavy ? 100.0 : 20.0) * c;   // N·m（理想虚务口径解析值）
    const double tauRms = (heavy ? 40.0 : 10.0) * c;     // N·m
    a.motorTorquePeak = tauPeak;
    a.motorTorqueRms = tauRms;
    a.motorSpeedPeak = (heavy ? 10.0 : 5.0) / c;         // rad/s
    a.motorSpeedRms = ((heavy ? 10.0 : 5.0) / c) * 0.8;  // rad/s
    a.motorPowerPeak = tauPeak * a.motorSpeedPeak.value(); // W（解析：τ·ω）
    a.motorPowerRms = tauRms * a.motorSpeedRms.value();    // W
    a.peakDuration = heavy ? 2.0 : 1.0;                  // s（过载时间窗口内）
    a.peakAtTime = 1.5;                                   // s
    a.peakSegmentId = "seg-A";
    a.inertiaRatio = inertiaRatio;       // 无量纲（映射数值事实——测试给定）
    a.reflectedInertia = reflected;      // kg·m²（关节轴系——参考面）
    a.efficiencyApplied = true;
    return a;
}

/// 黄金筛选条件（默认全维度不启用——仅转矩/转速/功率/过载由工作点驱动）。
inline ScreeningCriteria goldenCriteria()
{
    ScreeningCriteria c;
    c.safetyFactor = 1.0;  // 不加严
    return c;
}

/// 按轴面判别选组合（builder 展开序是字典序而非语义序——测试以轴×候选
/// 内容定位目标组合，防序位漂移脆弱断言）。
/// @param combos  [in] 黄金组合集（build 产物）
/// @param j1Motor/j1Gearbox/j2Motor/j2Gearbox [in] 目标组合的逐轴指派
inline const DeviceCombination* findCombo(const std::vector<DeviceCombination>& combos,
                                          const char* j1Motor, const char* j1Gearbox,
                                          const char* j2Motor, const char* j2Gearbox)
{
    for (const DeviceCombination& c : combos) {
        if (c.axes.size() == 2 && c.axes[0].motorModelId == j1Motor
            && c.axes[0].gearboxModelId == j1Gearbox
            && c.axes[1].motorModelId == j2Motor
            && c.axes[1].gearboxModelId == j2Gearbox) {
            return &c;
        }
    }
    return nullptr;
}

/// 按组合键取校核产出（输出序＝映射批 combinations 表序——以键定位，
/// 防序位漂移脆弱断言；未找到返回 nullptr）。
inline const CombinationCheckOutcome* findOutcome(
    const std::vector<CombinationCheckOutcome>& outcomes,
    const DeviceCombinationId& comboId)
{
    for (const CombinationCheckOutcome& o : outcomes) {
        if (o.record.id == comboId) {
            return &o;
        }
    }
    return nullptr;
}

/// 黄金映射批（三组合 case-A 全量事实——组合键由调用方传入以保键同源）。
inline MappingBatchFacts goldenMappingBatch(const std::vector<DeviceCombination>& combos,
                                            const GoldenAxes& axes)
{
    MappingBatchFacts batch;
    batch.mappingContractVersion = 1;   // 与 drivetrain kMappingContractVersion 同值口径
    batch.mappingAlgorithmVersion = 1;
    batch.upstreamSliceId = core::ContentIdentity{};  // 直调路径全零（诚实标记）
    batch.completeness = CompletenessKind::Complete;
    for (const DeviceCombination& combo : combos) {
        MappingCombinationFact cf;
        cf.combinationId = combo.id;
        cf.catalog = combo.catalog;
        cf.axes = combo.axes;
        batch.combinations.push_back(cf);
    }
    for (const DeviceCombination& combo : combos) {
        // 组合面判别：含 G-20 的轴 c＝0.02；含 M-B 的轴用 B 的惯量口径。
        for (const AxisDeviceAssignment& axis : combo.axes) {
            const bool heavy = axis.jointId == axes.j1;
            const bool isG20 = axis.gearboxModelId == "G-20";
            const double c = isG20 ? 0.02 : 0.1;  // c＝1/n（解析值）
            const double ratio = isG20 ? 75.0 : 3.0;  // 惯量比黄金值（测试给定）
            const double reflected = isG20 ? 25.0 : (axis.motorModelId == "M-B" ? 2.0 : 1.0);
            batch.axes.push_back(makeMappingFact(combo.id, axis.jointId, "case-A",
                                                 c, heavy, ratio, reflected));
        }
    }
    return batch;
}

}  // namespace sdurws::ird::selection::testsupport

#endif  // IRD_SELECTION_COMBINATIONTESTSUPPORT_HPP
