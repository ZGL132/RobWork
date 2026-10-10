/**
 * @file   LinearDrive.hpp
 * @brief  直线传动选型层（selection 单元）——直线轴类型化广义量工作点
 *         事实（drivetrain §16.2 扩展端口的消费承载）与四类直线传动器件
 *         目录模板/能力曲线的选型消费声明（卡 §17.2——WP-19-T12/SEL-09-S1）。
 *
 * 设计依据：
 *   - units/selection.md §17.2（SEL-09-S1 直线传动 R2 选型层：能力曲线
 *     schema〔横坐标速度/载荷 m/s、N；纵坐标力/功率 N、W〕；线性位移/
 *     速度/加速度/力单位进字段字典；drivetrain 扩展端口——selection 只
 *     消费不自实现直线映射）、§7.2/§10.3/§10.4（筛选纪律/原因词表/稳定
 *     排序——直线通道复用同一套纪律）、§14.0（错误二分与通用约定）、
 *     §14.4（IHardConstraintSelector 接口面——screenLinearDrives 经既有
 *     筛选器接口消费）、§14.5 注（R2 扩展直线传动组合——本任务不落位，
 *     组合构造直线轴通道仍为预留）
 *   - units/drivetrain.md §16.2（扩展点设计预留：JointKind::Prismatic
 *     通道的移动广义量——位移 m、速度 m/s、加速度 m/s²、力 N；直线传动
 *     映射公式族待 R2 需求细化，drivetrain 只提供扩展点）
 *   - 需求 SEL-09-S1（直线传动目录模板与映射——选型层；可行与不可行
 *     样例；不改变六/七轴全旋转链现有行为——V12-03 收窄）、SEL-06
 *     （逐项淘汰原因含实际值/阈值/原因）、SEL-02（能力曲线插值禁外推
 *     ——不放宽）
 *   - 任务契约 tasks/foundation/WP-19-T12.json（acceptance 1～3）
 *
 * ★ 唯一映射纪律（本头最重要的边界——红线机器可断言）：
 *   直线传动的工作点映射（旋转→直线的传动常数换算、直线电机的推力
 *   常数出力等公式族）**唯一实现归属 drivetrain**（drivetrain 卡 §16.2
 *   扩展点；DYN-04 同款唯一映射纪律在直线侧的延伸）。本头与整个
 *   selection 产品面：①零 drivetrain include（P-SEL-2——③端口不落编译
 *   边）；②零直线映射公式（任何"旋转量→直线量"的换算表达式不得在
 *   selection 书写——契约测试 LinearDriveContractTest 词表扫描锁定）。
 *   selection 的直线工作点唯一来源＝消费 drivetrain 扩展端口的产出
 *   （类型化广义量事实，值传递）。
 *
 * ★ 提议契约承载形态（P-SEL-1 同款纪律——登记单元卡 §19.3 T12）：
 *   drivetrain 卡 §16.2 扩展端口为 R2 设计预留（接口形状，非 R1 实现）
 *   ——本头以 selection 域内值类型 LinearAxisWorkpointFacts 承载该端口
 *   产出在 selection 侧的消费面（单方提议契约 v1）；由组装方（L5 装配/
 *   测试）从③端口上游产出提取填充；drivetrain 扩展端口落位后按其卡
 *   收编（字段名/分组语义不变的前提下对齐）。本头不自建映射、不伪造
 *   工作点（字段未供给＝数据缺口，与旋转侧 AxisWorkpointFacts 同款
 *   "不把缺证据当零负载"纪律，卡 §7.2/§11.2）。
 *
 * ★ 分期边界（V12-03 收窄的执行面）：本头交付的是**选型层**资格事实
 *   （目录模板导入校验＋直线通道硬筛选）——MDL-12-S1（产品链正式计算
 *   放开）仍为 R2 未启用：①既有旋转筛选器/组合校核对移动关节轴的
 *   "范围外"阻断零变化（不放宽——SEL-09 R1 语义）；②4/5 轴阻断不受
 *   影响（modeling 链型入口，MDL-12-S1 前不放开）；③组合构造的直线轴
 *   通道为卡 §17.2 预留，本任务不落位（直线筛选是单轴×单器件的资格
 *   事实面，不产生轴×组合的 DeviceCombination）。
 *
 * 线程安全：本头全部类型为纯值类型＋无状态纯函数（可重入——卡 §14.10）。
 */

#ifndef IRD_SELECTION_LINEARDRIVE_HPP
#define IRD_SELECTION_LINEARDRIVE_HPP

#include <optional>
#include <string>

#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/selection/CatalogTypes.hpp>   // CaseId（WP-19-T12 上移至此——
                                                   //   强语义 ID 别名区）/目录类型；
                                                   //   零 Screening.hpp include——
                                                   //   消除循环（Screening.hpp 反向
                                                   //   include 本头取事实类型）

namespace sdurws::ird::selection {

// =====================================================================
// 直线轴类型化广义量工作点事实（drivetrain §16.2 扩展端口消费承载——
// 提议契约 v1，P-SEL-1 同款纪律）
// =====================================================================

/**
 * @brief 单直线轴×单工况的类型化广义量工作点事实（drivetrain 卡 §16.2
 *        "移动广义量：位移 m、速度 m/s、加速度 m/s²、力 N"四量在
 *        selection 侧的消费承载）。
 *
 * 四量与单位（物理量注释单位制式纪律——AGENTS §2.5；卡 §4.4 单位表
 * "R2 移动关节扩展"行：位移 m/速度 m/s/加速度 m/s²/力 N）：
 *   - 力（force*）：筛选消费——推力两档维度（RMS/峰值）与功率维度的
 *     事实来源；
 *   - 速度（linearSpeedPeak）：筛选消费——直线速度维度；
 *   - 功率（power*）：筛选消费——直线功率维度（功率是映射产出之一，
 *     与力/速度同为扩展端口产出的独立量——selection 不自算 F×v）；
 *   - 位移（displacementPeak）与加速度（accelerationPeak）：**承载不
 *     判定**——四量齐备承载是扩展端口消费面的完整性要求（drivetrain
 *     卡 §16.2 词面），行程利用/加速度能力等判定维度随 R2 需求细化，
 *     当前不伪造判定（不判定≠默认通过——缺维度不产生原因也不产生
 *     缺口，见字段注）。
 *
 * 全部 optional 数值字段：nullopt＝该量未供给（对应判定维度数据缺口
 * ——与旋转侧 AxisWorkpointFacts 同款语义）；present 值必须有限
 * （NaN/±Inf＝调用方契约违约 fail-fast——卡 §14.0 错误二分）。
 *
 * ★ 零自实现红线：本结构的字段是**映射产出的事实承载**——字段值只能
 *   来自 drivetrain 扩展端口（组装方从③端口上游提取填充）；selection
 *   内任何"由其他字段推导本结构字段"的计算都是映射第二实现（红线，
 *   契约测试词表扫描锁定）。
 */
struct LinearAxisWorkpointFacts {
    core::ObjectId jointId;   ///< 轴对象 ID（modeling 项目对象——稳定身份；
                              ///   移动关节轴的定位面）
    CaseId caseId;            ///< 工况 ID（同轴多工况分组——逐工况独立判定）

    // ---- 力（单位 N——直线推力；筛选消费）----
    std::optional<double> forceRms;   ///< 工作点推力 RMS，单位 N
                                      ///   （连续推力维度的事实来源）
    std::optional<double> forcePeak;  ///< 工作点峰值推力，单位 N
                                      ///   （峰值推力维度的事实来源）

    // ---- 速度（单位 m/s——直线速度；筛选消费）----
    std::optional<double> linearSpeedPeak; ///< 工作点峰值线速度，单位 m/s
                                           ///   （直线速度维度＋推力-速度曲线
                                           ///   查询点的事实来源）

    // ---- 功率（单位 W；筛选消费）----
    std::optional<double> powerPeak;  ///< 工作点峰值功率，单位 W（直线功率
                                      ///   维度峰值侧的事实来源）
    std::optional<double> powerRms;   ///< 工作点 RMS 功率，单位 W（直线功率
                                      ///   维度连续侧的事实来源）

    // ---- 位移（单位 m）与加速度（单位 m/s²）——承载不判定 ----
    std::optional<double> displacementPeak;   ///< 工作点峰值位移（行程位置），
                                              ///   单位 m——四量完整性承载；
                                              ///   行程判定维度随 R2 细化，
                                              ///   当前不据此产生原因/缺口
    std::optional<double> accelerationPeak;   ///< 工作点峰值加速度，单位
                                              ///   m/s²——四量完整性承载；
                                              ///   加速度能力维度随 R2 细化，
                                              ///   当前不据此产生原因/缺口

    // ---- 工作点定位（淘汰原因携带——卡 §10.3）----
    double atTime = 0.0;      ///< 峰值工作点时间，单位 s（原因定位）
    std::string segmentId;    ///< 轨迹段 ID（原因定位；无段上下文为空串）

    bool operator==(const LinearAxisWorkpointFacts& o) const
    {
        return jointId == o.jointId && caseId == o.caseId
            && forceRms == o.forceRms && forcePeak == o.forcePeak
            && linearSpeedPeak == o.linearSpeedPeak
            && powerPeak == o.powerPeak && powerRms == o.powerRms
            && displacementPeak == o.displacementPeak
            && accelerationPeak == o.accelerationPeak
            && atTime == o.atTime && segmentId == o.segmentId;
    }
    bool operator!=(const LinearAxisWorkpointFacts& o) const { return !(*this == o); }
};

}  // namespace sdurws::ird::selection

#endif  // IRD_SELECTION_LINEARDRIVE_HPP
