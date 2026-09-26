/**
 * @file   PanelValidationModel.hpp
 * @brief  校验面板呈现模型（零 Qt）——R0~R9 分层结果计数、逐项定位跳转
 *         与预览/正式语义说明（卡 §9.8 面板表第 5 行）。
 *
 * 设计依据：
 *   - units/requirements.md §9.8（面板组成表第 5 行——"R0~R9 分层结果
 *     （Blocking/Warning 计数＋逐项定位跳转）；预览/正式语义说明（REQ-06）"；
 *     消费契约＝§8＋IPluginUiModule::readonlyProjections()＋IDiagnosticSink）、
 *     §8.1（就绪校验分层表——层/检查/级别权威）、§7.5（预览与正式分离
 *     ——REQ-06：预览零修订零正式证据；正式＝命令 prepare 现场重估）、
 *     §12（DomainReadinessItem 三字段呈现数据——P-REQ-6 边界：报告不含
 *     门控动作语义）
 *   - 需求 REQ-06（Must/Should 分级与预览分离）、UX-06（诊断定位跳转）；
 *     任务契约 tasks/foundation/WP-14-T08.json acceptance 1/2
 *
 * ★ P-REQ-6 边界（契约 knownPitfalls）：就绪 Blocking 的**门控拦截归
 *   workflow/ui**——本面板对三级（Blocking/Warning/NotApplicable）仅做
 *   呈现：计数行/逐项行/语义说明行都是**数据事实**，没有任何"能否进入
 *   某阶段"的判定字段（Readiness.hpp DomainReadinessItem 同源边界——
 *   供数不做主，D-REQ-11）。
 *
 * ★ "ui 侧仅呈现拒绝诊断，不本地复判"（契约 acceptance 2——L-R3 的
 *   P-REQ-6 边界半区）：本模型不做任何就绪重估——报告值唯一来源是
 *   IRequirementReadinessChecker::check 的产出（调用方注入：编辑器即时
 *   预检或命令拒绝回执的诊断投影），面板只是报告→行卡的投影器。
 *
 * 线程约束：全部函数纯函数，可重入；报告值仅 UI 线程产生（§3.4）。
 * 确定性：层行恒 10 行（R0~R9 定序）；逐项行保报告稳定序（不二次排序
 * ——单一排序权威纪律）。
 */

#ifndef IRD_REQUIREMENTS_PLUGIN_PANELVALIDATIONMODEL_HPP
#define IRD_REQUIREMENTS_PLUGIN_PANELVALIDATIONMODEL_HPP

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <sdurws/ird/core/Identity.hpp>  // core::ObjectId（定位跳转锚）
#include <sdurws/ird/requirements/Readiness.hpp>  // RequirementReadinessReport/DomainReadinessItem/分层词表

namespace sdurws::ird::requirements {

// =====================================================================
// 分层结果行与逐项行（卡 §9.8 第 5 行的行载体）
// =====================================================================

/**
 * @brief 一层的校验结果行（"R0~R9 分层结果"的承载——层号定序恒 10 行）。
 */
struct ValidationLayerRow {
    std::string layerToken;  ///< 层号 token（"R0".."R9"——readinessLayerToken 直投）
    std::string title;       ///< 层名（§8.1 表"检查"列——固定中文词表）
    std::size_t blocking = 0;    ///< 该层 Blocking 发现数
    std::size_t warning = 0;     ///< 该层 Warning 发现数
    std::size_t notApplicable = 0;  ///< 该层 NotApplicable 显式标记数

    bool operator==(const ValidationLayerRow& o) const
    {
        return layerToken == o.layerToken && title == o.title && blocking == o.blocking
            && warning == o.warning && notApplicable == o.notApplicable;
    }
    bool operator!=(const ValidationLayerRow& o) const { return !(*this == o); }
};

/**
 * @brief 一条校验发现的逐项行（"逐项定位跳转"的载体——UX-06）。
 *
 * jumpTarget＝诊断 subject 的条目锚（DiagnosticRecord.subjectObjectId→树
 * 节点 ObjectId 同键关联）；无 subject 的发现（如 R5 空必验集合级警告）
 * 无跳转目标＝nullopt（呈现为不可点击行——不伪造定位）。
 */
struct ValidationItemRow {
    std::string levelToken;  ///< 级别 token（"Blocking"/"Warning"/"NotApplicable"——直投）
    std::string layerToken;  ///< 所属层 token（"R0".."R9"——定位面）
    std::string code;        ///< 稳定诊断码（REQ-READY-* 等——§9.6 词表直投）
    std::string summary;     ///< 人读一行（诊断记录消息——呈现层不加工业务文案）
    std::optional<core::ObjectId> jumpTarget;  ///< 跳转目标（树节点锚；无定位＝nullopt）

    bool operator==(const ValidationItemRow& o) const
    {
        return levelToken == o.levelToken && layerToken == o.layerToken && code == o.code
            && summary == o.summary && jumpTarget == o.jumpTarget;
    }
    bool operator!=(const ValidationItemRow& o) const { return !(*this == o); }
};

/**
 * @brief 校验面板投影（报告→行卡的纯投影器——零重估零判定）。
 *
 * 字段面：
 *   - layers：恒 10 行（下标 0..9 ↔ R0..R9——§8.1 层序；未检查到的层
 *     计数为 0——短路语义的如实呈现，不伪造"已检查"）；
 *   - items：逐项行（保报告稳定序——items 已按 R0→R9 短路序产出，
 *     Readiness.hpp 排序契约；本投影保序展开，不二次排序）；
 *   - previewNote/formalNote：预览/正式语义说明行（REQ-06——固定文案，
 *     见 kValidationPreviewNote/kValidationFormalNote）；
 *   - blockingCount/warningCount：总数（计数行素材——呈现级汇总）。
 */
struct ValidationPanelProjection {
    std::vector<ValidationLayerRow> layers;  ///< 分层结果行（恒 10 行——R0~R9）
    std::vector<ValidationItemRow> items;    ///< 逐项行（报告稳定序；逐项定位跳转）
    std::size_t blockingCount = 0;           ///< Blocking 总数（呈现级汇总）
    std::size_t warningCount = 0;            ///< Warning 总数
    std::string previewNote;                 ///< 预览语义说明（REQ-06——kValidationPreviewNote）
    std::string formalNote;                  ///< 正式语义说明（kValidationFormalNote）

    bool operator==(const ValidationPanelProjection& o) const
    {
        return layers == o.layers && items == o.items && blockingCount == o.blockingCount
            && warningCount == o.warningCount && previewNote == o.previewNote
            && formalNote == o.formalNote;
    }
    bool operator!=(const ValidationPanelProjection& o) const { return !(*this == o); }
};

/// @brief 预览语义说明（REQ-06/§7.5：编辑器即时预检——零修订、零正式证据、
/// 预览输入无内容身份承诺；固定文案，EVI-01 表 1 预览行原文语义）。
std::string_view kValidationPreviewNote() noexcept;

/// @brief 正式语义说明（REQ-06：正式判定＝命令 prepare 现场重估——就绪
/// Blocking 拦截归 prepare/project（P-REQ-6），面板仅呈现拒绝诊断）。
std::string_view kValidationFormalNote() noexcept;

/**
 * @brief 投影校验面板（报告→行卡；@param report 为 IRequirementReadiness
 *        Checker::check 的产出——本函数不调用 check，不重估）。
 *
 * @param report [in] 就绪报告（域校验器产出——判定权威，零加工）
 * @return 面板投影（层行恒 10 行；逐项行保序；确定性）
 *
 * 纯函数；确定性；不抛。
 */
ValidationPanelProjection projectValidationPanel(const RequirementReadinessReport& report);

}  // namespace sdurws::ird::requirements

#endif  // IRD_REQUIREMENTS_PLUGIN_PANELVALIDATIONMODEL_HPP
