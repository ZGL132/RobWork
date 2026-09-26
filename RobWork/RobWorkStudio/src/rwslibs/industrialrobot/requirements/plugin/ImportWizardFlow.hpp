/**
 * @file   ImportWizardFlow.hpp
 * @brief  导入向导域侧页数据流（零 Qt）——L-R10：io 预检→表头映射＋单位
 *         预览→逐行错误清单→确认→草稿（卡 §9.8 界面逻辑表 L-R10 行）。
 *
 * 设计依据：
 *   - units/requirements.md §9.8 界面逻辑表 L-R10（"导入向导域侧页：文件
 *     选择（io 预检）→表头映射＋单位预览→逐行错误清单（正确行可折叠
 *     预览）→确认→草稿→draft.apply"；消费契约＝§7.3；AT-02）、§7.3
 *     （CSV 坐标表导入——字段字典冻结/自动识别/单位声明与预览/行级部分
 *     成功）、§7.4（JSON 导入）
 *   - 需求 REQ-05（CSV 导入——部分成功/错误定位/单位归一 SI）、AT-02；
 *     任务契约 tasks/foundation/WP-14-T08.json acceptance 4
 *
 * 背景说明（域侧页＝向导的**模型半区**，GUI 呈现归 widget 层）：
 *   - "io 预检"半区：文件枚举/读取/解析归 io 与装配层（PA-1；插件零文件
 *     IO）——本模型第一步接收 io 已解析产物（RawTable 值；经 Import.hpp
 *     公共头的 io 值类型传染可达，插件不直接 include io 头）；
 *   - "表头映射＋单位预览"：autoDetectMapping/previewUnitConversion 域
 *     函数现算（映射与换算零复制——NFR-MNT-04；换算唯一经 core Units）；
 *   - "逐行错误清单"：mapCsv/mapJson 产出的 ImportOutcome 直投（部分成功
 *     语义——正确行保留、错误行定位到列与原文，AT-02）；
 *   - "确认→草稿"：确认后把正确行条目逐条经编辑器 applyEdit 入草稿
 *     （ImportOutcome.entries——objectId 恒全零待命令 prepare 分配，O-36）。
 *   步骤推进是**显式动作**（每步一步），无自动跳步；回退＝重新 set 源。
 *
 * 线程约束：仅 UI 线程访问（§3.4——向导会话态）。确定性：步骤推进即调
 * 用序；同输入同产出（域函数确定性——NFR-COR-01/02）。
 */

#ifndef IRD_REQUIREMENTS_PLUGIN_IMPORTWIZARDFLOW_HPP
#define IRD_REQUIREMENTS_PLUGIN_IMPORTWIZARDFLOW_HPP

#include <string>
#include <vector>

#include <sdurws/ird/requirements/Import.hpp>  // IRequirementImporter/ImportOutcome/FieldMapping/ImportUnitOptions（域映射唯一入口）
#include <sdurws/ird/requirements/Editor.hpp>  // IRequirementEditor（草稿唯一写目标）

namespace sdurws::ird::requirements {

// =====================================================================
// 向导步骤与逐步数据（L-R10 五步的模型承载）
// =====================================================================

/**
 * @brief 向导步骤词表（L-R10 行原文五段——步骤推进的判别枚举；非存储
 *        状态机的第二份迁移规则——推进只经显式动作方法）。
 */
enum class ImportWizardStep : std::uint8_t {
    SourceSelected,  ///< ①源已选（io 预检产物已接收——RawTable 在手）
    Mapped,          ///< ②表头已映射（自动/手动＋单位声明已定）
    Previewed,       ///< ③映射执行完毕（逐行错误清单在手——正确行可折叠预览）
    Confirmed,       ///< ④用户已确认（entries 待入草稿）
    Applied,         ///< ⑤草稿写入完毕（正确行条目已 applyEdit）
};

/**
 * @brief 表头映射步骤的呈现数据（自动识别＋单位预览的合并承载——②页面
 *        的行模型）。
 */
struct ImportMappingView {
    FieldMapping mapping{};                        ///< 字段→列映射（自动识别产出；可手动改）
    std::vector<std::size_t> unrecognizedColumns;  ///< 未识别列号（0 起升序——进忽略清单）
    std::vector<UnitPreviewEntry> unitPreview;     ///< 单位换算预览（已映射长度/角度列——"1000（mm）→1（m）"）

    /// @brief 值相等（UnitPreviewEntry 无 operator==——逐字段比较；
    ///        siSample 为 optional<double> 精确比较——预览值面无容差语义）。
    bool operator==(const ImportMappingView& o) const
    {
        if (!(mapping.columnOf == o.mapping.columnOf)
            || unrecognizedColumns != o.unrecognizedColumns
            || unitPreview.size() != o.unitPreview.size()) {
            return false;
        }
        for (std::size_t i = 0; i < unitPreview.size(); ++i) {
            const UnitPreviewEntry& a = unitPreview[i];
            const UnitPreviewEntry& b = o.unitPreview[i];
            if (a.field != b.field || a.declaredUnit != b.declaredUnit
                || a.rawSample != b.rawSample || a.siSample != b.siSample
                || a.unitUsable != b.unitUsable) {
                return false;
            }
        }
        return true;
    }
    bool operator!=(const ImportMappingView& o) const { return !(*this == o); }
};

/**
 * @brief 导入向导域侧流（L-R10 的模型半区——步骤推进即数据流）。
 *
 * 生命周期：由导入命令处理器/面板创建（UI 线程会话对象）；一个实例对应
 * 一次导入会话（重导＝新实例或 reset）。不持有文件句柄（io 预检半区在
 * 装配层）。
 */
class ImportWizardFlow {
public:
    /**
     * @brief 构造（UI 线程——§3.4）。
     * @param importer [in] 需求导入器（域映射唯一入口；非 owning——调用方
     *                 保证存活期覆盖本流）
     */
    explicit ImportWizardFlow(const IRequirementImporter& importer) : m_importer(importer) {}

    ImportWizardFlow(const ImportWizardFlow&) = delete;
    ImportWizardFlow& operator=(const ImportWizardFlow&) = delete;

    // ---- ①源选择（io 预检半区在装配层——本方法接收解析产物）------------

    /**
     * @brief 接收 io 已解析表（L-R10 第①步的模型半区——文件枚举/读取/
     *        解析归 io 与装配层，PA-1；本流从"表在手"起步）。
     *
     * @param table [in] io 已解析 CSV 表（须有表头——无表头属装配层预检
     *              漏项，autoDetectMapping 域函数 fail-fast 兜底）
     *
     * @throws std::invalid_argument 透传 autoDetectMapping 的无表头违约
     *               （装配层预检漏项——fail-fast）
     */
    void setSourceTable(const io::RawTable& table);

    // ---- ②表头映射＋单位预览 -------------------------------------------

    /**
     * @brief 执行表头自动识别＋单位换算预览（L-R10 第②步——映射与换算
     *        经域函数现算，零复制）。
     *
     * @param units [in] 单位声明（缺省 m/rad——ImportUnitOptions::defaults）
     * @return 映射步骤呈现数据（映射＋未识别列＋单位预览）
     *
     * @throws std::invalid_argument 源未设置即调用（步骤违约——fail-fast）
     *
     * 纯计算（域函数）；确定性。
     */
    ImportMappingView prepareMapping(const ImportUnitOptions& units);

    // ---- ③执行映射（逐行错误清单）--------------------------------------

    /**
     * @brief 以当前映射执行导入映射（L-R10 第③步——行级部分成功：正确行
     *        保留、错误行定位到列与原文，AT-02）。
     *
     * @param mapping [in] 字段映射（prepareMapping 产出或手动改映射）
     * @param units   [in] 单位声明（与②一致——落库前经 core 唯一换算归一
     *                SI，REQ-05）
     * @return 导入产出（status 三分语义——Rejected 时 entries 恒空；结构级
     *         诊断经 m_lastDiags 累积，可经 lastDiagnostics() 取用）
     *
     * @throws std::invalid_argument 源未设置／mapping 列号越界（调用方
     *               契约违约——域函数 fail-fast 透传）
     */
    ImportOutcome executeMapping(const FieldMapping& mapping, const ImportUnitOptions& units);

    /// ③步骤产出（executeMapping 后有效——逐行错误清单与正确行条目）。
    const ImportOutcome& outcome() const noexcept { return m_outcome; }
    /// 累积诊断（列级/结构级/警告——REQ-IMPORT-UNIT-ILLEGAL 等；跨步骤累积）。
    const std::vector<core::DiagnosticRecord>& lastDiagnostics() const noexcept
    {
        return m_diags;
    }

    // ---- ④确认→⑤入草稿 -------------------------------------------------

    /**
     * @brief 确认并写入草稿（L-R10 第④⑤步——正确行条目逐条 applyEdit
     *        入编辑器工作集；entries 的 objectId 恒全零（O-36：待命令
     *        prepare 分配），编辑器照常接受全零临时句柄）。
     *
     * @param editor [in,out] 需求编辑器（草稿唯一写目标；仅 UI 线程）
     * @return 入草稿条目数（=outcome().entries 中被编辑器接受的条目数；
     *         拒绝条目（如重名）不入——调用方据 outcome().rowErrors 与
     *         返回值差额呈现，本流不吞不静默）
     *
     * @throws std::invalid_argument 未执行 executeMapping 即确认（步骤违约
     *               ——fail-fast）
     */
    std::size_t confirmAndApplyToDraft(IRequirementEditor& editor);

    /// 当前步骤（会话态只读投影——widget 据此驱动页面栈）。
    ImportWizardStep step() const noexcept { return m_step; }

    /// 重置（重新导入——全部会话态归零；步骤回①空态）。
    void reset();

private:
    const IRequirementImporter& m_importer;  ///< 需求导入器（非 owning——域映射唯一入口）
    ImportWizardStep m_step = ImportWizardStep::SourceSelected;  ///< 当前步骤（推进只经显式动作）
    io::RawTable m_table{};                  ///< ①接收的解析产物（io 值类型——值持有）
    bool m_hasTable = false;                 ///< 源是否已设置（步骤违约的判别位）
    ImportOutcome m_outcome{};               ///< ③导入产出（逐行错误清单＋正确行）
    std::vector<core::DiagnosticRecord> m_diags;  ///< 累积诊断（跨步骤）
};

// =====================================================================
// L-R11 JSON 副本导出（卡 §9.8 界面逻辑表 L-R11 行的呈现面）
// =====================================================================

/**
 * @brief 副本导出的用户提示（L-R11"exportCopy→成功提示路径／失败旧文件
 *        完好提示"的承载——导出执行与提示文案装配的唯一入口）。
 */
struct ExportCopyView {
    bool ok = false;      ///< 导出是否成功（ExportOutcome.ok 直投）
    std::string prompt;   ///< 用户提示（成功＝"已导出副本：<path>（N 字节）"；失败＝
                          ///< "导出失败，原文件保持完好：<detail>"——REQ-12 语义）

    bool operator==(const ExportCopyView& o) const
    {
        return ok == o.ok && prompt == o.prompt;
    }
    bool operator!=(const ExportCopyView& o) const { return !(*this == o); }
};

/**
 * @brief 执行副本导出并装配用户提示（L-R11——导出唯一经
 *        IRequirementImporter::exportCopy 域函数〔AtomicFile 原子就位，
 *        失败旧文件完好的强保证在域内〕；本函数只装配呈现文案）。
 *
 * @param importer [in] 需求导入器（域导出唯一入口；非 owning——调用期存活）
 * @param ws       [in] 需求工作集（只读——导出不改写）
 * @param format   [in] 导出格式（Csv/Json）
 * @param target   [in] 目标路径＋草稿标记（ExportTarget 语义——draft=true
 *                 的 CSV 请求被域值面拒绝）
 * @return 提示视图（ok＝ExportOutcome.ok；prompt 按两态装配——确定性）
 *
 * 纯函数（对 importer 的调用无副作用面——导出写目标文件，语义见域契约）；
 * 确定性；不抛（target.filePath 为空＝调用方契约违约，域函数 fail-fast
 * 透传）。
 */
ExportCopyView exportCopyPrompt(const IRequirementImporter& importer,
                                const RequirementWorkingSet& ws, ExportFormat format,
                                const ExportTarget& target);

}  // namespace sdurws::ird::requirements

#endif  // IRD_REQUIREMENTS_PLUGIN_IMPORTWIZARDFLOW_HPP
