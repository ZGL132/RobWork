/**
 * @file   ImportWizardFlow.cpp
 * @brief  导入向导域侧流实现——L-R10 五步的模型半区编排。
 *
 * 设计依据：units/requirements.md §9.8（L-R10）、§7.3/§7.4（导入映射与
 * 部分成功）；契约 WP-14-T08 acceptance 4。实现纪律：映射/换算/校验全部
 * 经 IRequirementImporter 域函数（零复制——NFR-MNT-04）；步骤推进即显式
 * 动作（无自动跳步）。
 */

#include "ImportWizardFlow.hpp"

#include <stdexcept>
#include <utility>

namespace sdurws::ird::requirements {

void ImportWizardFlow::setSourceTable(const io::RawTable& table)
{
    // ①接收 io 解析产物（文件面归 io/装配层——本流零文件 IO，PA-1）。
    m_table = table;
    m_hasTable = true;
    m_step = ImportWizardStep::SourceSelected;
}

ImportMappingView ImportWizardFlow::prepareMapping(const ImportUnitOptions& units)
{
    // 步骤前置：源未设置＝调用方步骤违约（fail-fast——不静默用空表）。
    if (!m_hasTable) {
        throw std::invalid_argument("导入向导：源表未设置即请求映射（步骤违约）");
    }

    ImportMappingView view;
    // 表头自动识别（域函数——canonical 优先、别名次之、同字段首列胜出；
    // 无表头违约由域函数 fail-fast 透传——装配层预检漏项）。
    const AutoDetectResult detected = autoDetectMapping(m_table, m_importer.fieldDictionary());
    view.mapping = detected.mapping;
    view.unrecognizedColumns = std::move(detected.unrecognizedColumns);
    // 单位换算预览（域函数——与 mapCsv 同一声明校验/换算入口：预览值＝
    // 落库值，NFR-MNT-04；换算唯一经 core Units——REQ-05 单位归一 SI）。
    view.unitPreview =
        previewUnitConversion(m_table, view.mapping, units, m_importer.fieldDictionary());
    m_step = ImportWizardStep::Mapped;
    return view;
}

ImportOutcome ImportWizardFlow::executeMapping(const FieldMapping& mapping,
                                               const ImportUnitOptions& units)
{
    // 步骤前置：源未设置＝步骤违约。
    if (!m_hasTable) {
        throw std::invalid_argument("导入向导：源表未设置即执行映射（步骤违约）");
    }
    // 行级映射（域函数——部分成功语义：正确行保留、错误行逐条定位到列
    // 与原文，AT-02；列级/结构级诊断追加进累积清单）。
    const std::size_t before = m_diags.size();
    m_outcome = m_importer.mapCsv(m_table, mapping, units, m_diags);
    (void)before;  // 诊断只增不清（累积呈现面——调用方按需取全量）
    m_step = ImportWizardStep::Previewed;
    return m_outcome;
}

std::size_t ImportWizardFlow::confirmAndApplyToDraft(IRequirementEditor& editor)
{
    // 步骤前置：未执行映射即确认＝步骤违约（不静默把空产出当确认）。
    if (m_step != ImportWizardStep::Previewed && m_step != ImportWizardStep::Confirmed) {
        throw std::invalid_argument("导入向导：未执行映射即确认（步骤违约）");
    }
    m_step = ImportWizardStep::Confirmed;

    // 逐条入草稿（正确行条目——objectId 恒全零待命令 prepare 分配，O-36；
    // 编辑器域校验链裁决：重名/非法条目被拒不入——拒绝差额由调用方据
    // outcome().rowErrors 与返回值呈现，本流不吞不静默）。
    std::size_t applied = 0;
    for (const TaskPoint& entry : m_outcome.entries) {
        if (editor.applyEdit(entry).accepted) {
            applied += 1;
        }
    }
    m_step = ImportWizardStep::Applied;
    return applied;
}

void ImportWizardFlow::reset()
{
    // 全部会话态归零（重新导入——RawTable 值清空＋步骤回①空态）。
    m_table = io::RawTable{};
    m_hasTable = false;
    m_outcome = ImportOutcome{};
    m_diags.clear();
    m_step = ImportWizardStep::SourceSelected;
}

ExportCopyView exportCopyPrompt(const IRequirementImporter& importer,
                                const RequirementWorkingSet& ws, ExportFormat format,
                                const ExportTarget& target)
{
    // 导出唯一经域函数（L-R11 消费契约——AtomicFile 原子就位/失败旧文件
    // 完好的强保证在域内；本函数零导出逻辑，只装配呈现文案）。
    const ExportOutcome outcome = importer.exportCopy(ws, format, target);
    ExportCopyView view;
    view.ok = outcome.ok;
    if (outcome.ok) {
        // 成功提示路径（L-R11"成功提示路径"——canonical 路径＋字节数）。
        view.prompt = "已导出副本：" + target.filePath.string() + "（"
                    + std::to_string(outcome.bytesWritten) + " 字节）——不影响项目";
    } else {
        // 失败提示旧文件完好（L-R11"失败旧文件完好提示"——REQ-12 语义；
        // detail 为开发级细节，用户可见文案经 diagnostics 脱敏——此处按
        // 呈现面直投，脱敏链在诊断通道）。
        view.prompt = "导出失败，原文件保持完好：" + outcome.error;
    }
    return view;
}

}  // namespace sdurws::ird::requirements
