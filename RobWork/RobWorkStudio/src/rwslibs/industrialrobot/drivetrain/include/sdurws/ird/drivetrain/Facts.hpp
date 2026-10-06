/**
 * @file   Facts.hpp
 * @brief  传动事实 DTO 与提取接口（units/drivetrain.md §12.3/§13.6）——
 *         DriveTrainEvidenceFacts（映射事实＋输入身份＋诊断引用＋完整性
 *         信息，只含事实）与 IDriveTrainEvidenceFactsProvider。
 *
 * 设计依据：
 *   - units/drivetrain.md §12.3（与 evidence 正交表——drivetrain 只提供
 *     Facts DTO；evidence 拥有证据等级/ResultEnvelope/当前性/正式判定）、
 *     §13.6（接口签名；D-DT-10：命名 FactsProvider 而非 EvidenceBuilder
 *     ——"Builder"暗示拥有证据组装权，与 evidence 所有权边界冲突）
 *   - 需求 EVI-01/EVI-02（评估模式与证据汇总消费全局唯一契约——映射评估
 *     声明自己的 RequiredEvidenceProfile；缺失输入→DataInsufficient 素材，
 *     不伪造完整证据）、NFR-COR-04（结果绑定输入身份——证据可追溯）
 *   - 任务契约 tasks/foundation/WP-18-T03.json（acceptance 1——工作点
 *     输出的消费面）
 *
 * 所有权边界（★本头最关键的设计约束）：Facts DTO **不携带**任何证据等级、
 * 当前性状态、工程判定或 ResultEnvelope——那些归 evidence/消费域（PA-1
 * 权威唯一）。drivetrain 产出的是"有什么事实、缺什么输入、身份是什么"；
 * "可不可信、过不过、是否当前"由消费域判定。
 *
 * 线程安全：纯值类型；Facts 由调用方持有。
 */

#ifndef IRD_DRIVETRAIN_FACTS_HPP
#define IRD_DRIVETRAIN_FACTS_HPP

#include <sdurws/ird/core/DiagData.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/drivetrain/MappingTypes.hpp>
#include <sdurws/ird/drivetrain/Series.hpp>

#include <string>
#include <vector>

namespace sdurws::ird::drivetrain {

/**
 * @brief 传动事实 DTO（§12.3 Facts 行——canonical 形态供证据消费）。
 *
 * 字段面＝卡 §12.3"DriveTrainEvidenceFacts"行的机械化：映射结果摘要
 * （逐轴工作点的数值事实引用）、输入身份块、工况覆盖素材、缺失清单、
 * 诊断引用（按码值＋主体——完整诊断记录经映射输出 diagnostics 通道，
 * 此处只携带引用面防 DTO 膨胀）。估算来源标记贯穿（§10.7——证据/报告
 * 保留限定语 RPT-05 的素材面）。
 *
 * ★ 不承载：证据等级、当前性、工程判定、包络（evidence/消费域所有）；
 *   限位/超限结论（本卡不判——P-DT-2/§11.3）；惯量比阈值（数值事实而已）。
 */
struct DriveTrainEvidenceFacts {
    // ---- 身份块（NFR-COR-04 可追溯面——§5.5 身份链的承载）。
    DriveTrainIdentity identity{};            ///< 传动配置身份（矩阵内容身份的载体）
    core::ContentIdentity upstreamSliceId{};  ///< 上游切片身份（§5.6 输入身份分离）
    std::uint32_t algorithmVersion = 0;       ///< 映射算法版本
    std::uint32_t contractVersion = 0;        ///< 输入/输出契约版本（CON-04）
    core::ObjectId caseId{};                  ///< 本批事实的工况

    // ---- 完整性与缺失（DataInsufficient 素材——判定归 evidence）。
    CompletenessState completeness = CompletenessState::Complete; ///< 完整性状态
    std::vector<std::string> missingItems{};  ///< 缺失清单（轴前缀定位——§13.8 形态）
    bool estimatedSource = false;             ///< 含估算来源（RPT-05 限定语素材）

    // ---- 工况覆盖素材（EVI-02——必验工况覆盖矩阵的本域半区：本批覆盖了
    // 哪个工况；跨工况汇总归 evidence）。
    core::ObjectId caseCovered{};             ///< 已覆盖工况（与 caseId 同值——命名区分语义角色）

    // ---- 映射事实摘要（逐轴一行——数值事实的轻量引用面；完整数值经
    // payload/工作点对象消费，DTO 只作证据登记用）。
    struct AxisFact {
        core::ObjectId axisId{};        ///< 电机轴身份
        std::size_t jointIndex = 0;     ///< 对应关节下标
        double tauRms = 0.0;            ///< 转矩 RMS（N·m；完整循环含驻留）
        double omegaRms = 0.0;          ///< 速度 RMS（rad/s）
        double reflectedInertia = 0.0;  ///< 反射惯量（kg·m²，关节轴系）
        bool efficiencyApplied = false; ///< 效率是否可用（false＝该轴功率/能量降级）
        bool operator==(const AxisFact& o) const noexcept
        {
            return axisId == o.axisId && jointIndex == o.jointIndex && tauRms == o.tauRms
                && omegaRms == o.omegaRms && reflectedInertia == o.reflectedInertia
                && efficiencyApplied == o.efficiencyApplied;
        }
        bool operator!=(const AxisFact& o) const noexcept { return !(*this == o); }
    };
    std::vector<AxisFact> axes{};         ///< 逐轴事实（下标＝电机轴序）
    std::size_t diagnosticCount = 0;      ///< 诊断引用计数（完整记录在映射输出 diagnostics）
    std::vector<std::string> diagnosticCodes{}; ///< 诊断码引用面（稳定码值列表，按产出序）

    bool operator==(const DriveTrainEvidenceFacts& o) const
    {
        return identity == o.identity && upstreamSliceId == o.upstreamSliceId
            && algorithmVersion == o.algorithmVersion && contractVersion == o.contractVersion
            && caseId == o.caseId && completeness == o.completeness
            && missingItems == o.missingItems && estimatedSource == o.estimatedSource
            && caseCovered == o.caseCovered && axes == o.axes
            && diagnosticCount == o.diagnosticCount && diagnosticCodes == o.diagnosticCodes;
    }
    bool operator!=(const DriveTrainEvidenceFacts& o) const { return !(*this == o); }
};

/**
 * @brief 事实 DTO 提取接口（§13.6——从映射输出提取 canonical 事实面；
 *        替代旧提示词 IDriveTrainEvidenceBuilder——D-DT-10）。
 *
 * 本接口【不拥有】RequiredEvidenceProfile、证据等级、ResultEnvelope、
 * 结果当前性、正式工程判定、项目归档（全部归 evidence/接纳层）。
 */
struct IDriveTrainEvidenceFactsProvider {
    virtual ~IDriveTrainEvidenceFactsProvider() = default;

    /**
     * @brief 从映射输出提取事实 DTO（纯函数——零副作用，可重入）。
     *
     * @param output [in] 映射总输出（本函数只读）
     * @return 事实 DTO（身份块＋覆盖素材＋缺失清单＋诊断引用；canonical 形态）
     */
    virtual DriveTrainEvidenceFacts facts(const DriveTrainMappingOutput& output) const = 0;
};

}  // namespace sdurws::ird::drivetrain

#endif  // IRD_DRIVETRAIN_FACTS_HPP
