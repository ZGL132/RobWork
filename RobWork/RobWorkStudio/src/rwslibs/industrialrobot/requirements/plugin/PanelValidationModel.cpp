/**
 * @file   PanelValidationModel.cpp
 * @brief  校验面板呈现模型实现——报告→行卡的纯投影（R0~R9 计数/逐项/
 *         语义说明）。
 *
 * 设计依据：units/requirements.md §9.8（面板表第 5 行）、§8.1（分层表）、
 * §7.5/REQ-06（预览/正式分离）；契约 WP-14-T08 acceptance 1/2（P-REQ-6
 * 边界：仅呈现，不本地复判）。
 */

#include "PanelValidationModel.hpp"

#include <utility>

namespace sdurws::ird::requirements {

std::string_view kValidationPreviewNote() noexcept
{
    // 固定文案（REQ-06/§7.5 预览行语义——零修订零正式证据零身份承诺）；
    // 静态存储期字面量——呈现层不再加工。
    return "预览：本页结果是编辑器即时预检（域校验器纯函数），零修订、"
           "零正式证据，不产生内容身份承诺。";
}

std::string_view kValidationFormalNote() noexcept
{
    // 固定文案（REQ-06/§9.1：正式判定在命令 prepare 现场重估——P-REQ-6：
    // 拦截动作归 prepare/project，本面板仅呈现拒绝诊断）。
    return "正式：应用时由命令 prepare 按同一校验器现场重估，Blocking 将"
           "拒绝应用并逐项定位；本面板只呈现结果，不做阶段门控。";
}

namespace {

// 层名固定词表（§8.1 表"检查"列的中文缩略——下标序＝R0..R9；呈现文案的
// 确定性来源）。
const char* layerTitle(std::size_t layerIndex)
{
    static const char* kTitles[10] = {
        "结构完整", "引用完整", "坐标系有效", "位姿合法", "工况绑定完整",
        "必验范围明确", "采样计划有效", "任务顺序无环", "工具/模型引用", "可生成输入切片",
    };
    return kTitles[layerIndex];
}

}  // namespace

ValidationPanelProjection projectValidationPanel(const RequirementReadinessReport& report)
{
    ValidationPanelProjection proj;

    // ---- 分层结果行（恒 10 行——R0~R9 定序；逐层按级别计数：单遍扫描
    //      报告 items，按 item.diag 所属层归桶。层归属轴唯一来源＝发现值
    //      自身的 layer 字段——单一事实来源，不私写码→层第二映射）。----
    proj.layers.reserve(10);
    for (std::size_t i = 0; i < 10; ++i) {
        ValidationLayerRow row;
        row.layerToken = std::string(readinessLayerToken(static_cast<ReadinessCheckLayer>(i)));
        row.title = layerTitle(i);
        proj.layers.push_back(std::move(row));
    }
    for (const DomainReadinessItem& item : report.items) {
        const std::size_t idx = static_cast<std::size_t>(item.layer);
        switch (item.level) {
        case ReadinessFindingLevel::Blocking:
            proj.layers[idx].blocking += 1;
            proj.blockingCount += 1;
            break;
        case ReadinessFindingLevel::Warning:
            proj.layers[idx].warning += 1;
            proj.warningCount += 1;
            break;
        case ReadinessFindingLevel::NotApplicable:
            proj.layers[idx].notApplicable += 1;
            break;
        }
    }

    // ---- 逐项行（保报告稳定序——items 已按 R0→R9 短路序产出，Readiness
    //      排序契约；本投影保序展开不二次排序——单一排序权威纪律）。----
    proj.items.reserve(report.items.size());
    for (const DomainReadinessItem& item : report.items) {
        ValidationItemRow row;
        row.levelToken = std::string(readinessFindingLevelToken(item.level));
        row.layerToken = std::string(readinessLayerToken(item.layer));
        // 稳定码与定位（诊断记录三要素直投——码文本/原因零加工；subject
        // 有值且有效→跳转锚，无效/缺失→不可点击行，不伪造定位）。
        row.code = item.diag.code;
        row.summary = item.diag.cause;  // 人读一行＝诊断原因（呈现层不加工）
        if (item.diag.subject.has_value() && item.diag.subject.value().isValid()) {
            row.jumpTarget = item.diag.subject.value();
        }
        proj.items.push_back(std::move(row));
    }

    // ---- 语义说明行（REQ-06 预览/正式两行——固定文案直投）。
    proj.previewNote = std::string(kValidationPreviewNote());
    proj.formalNote = std::string(kValidationFormalNote());
    return proj;
}

}  // namespace sdurws::ird::requirements
