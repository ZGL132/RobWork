/**
 * @file   RequirementsCommandFlows.hpp
 * @brief  需求域九条命令的 UI 流程编排（UI-T32 C 批次——对话框/表单/
 *         文件选择与域纯函数的装配层；域逻辑零重写，全部经
 *         ITemplateArrayService/IRequirementImporter 既有纯函数）。
 *
 * 设计依据：
 *   - 任务契约 tasks/foundation/UI-T32.json（九命令 assembled 流程——
 *     实施序组1：导出副本/模板/阵列/镜像/重生成＋解除关联）；
 *   - units/requirements.md §7.1（模板六类）/§7.2（镜像/四类阵列）/
 *     §7.4（导出原子写）/§9.5（重生成/解除）；
 *   - 面板命令按钮→setCommandSubmit 出口→宿主 handler→门面
 *     executeDomainCommand 的唯一路由（PA-1——本文件是被调用端）。
 *
 * 线程模型：全部 UI 线程（对话框模态＋编辑器同步提交）。
 * 反馈面：成功/失败经返回的中文摘要（面板状态行呈现——UX-02）。
 */

#ifndef IRD_REQUIREMENTS_PLUGIN_REQUIREMENTSCOMMANDFLOWS_HPP
#define IRD_REQUIREMENTS_PLUGIN_REQUIREMENTSCOMMANDFLOWS_HPP

#include <optional>
#include <QString>
#include <QStringList>
#include <string>

#include <sdurws/ird/requirements/TemplateArray.hpp>  // TemplateParams/ArrayKind（表单应答值面）
#include <sdurws/ird/requirements/Import.hpp> // WP-14-T12：私有映射对话框使用原字段/单位类型。
#include <sdurws/ird/requirements/RequirementsPluginAssembly.hpp>  // RequirementsView3DSeams（UI-T33——门面消费面同单元）

namespace sdurws::ird::requirements {

class RequirementsPanelWidget;
class IRequirementEditor;
class IRequirementEditSink;

/**
 * @brief 命令对话框宿主（UI-T32 返工测试缝——attempt1 fail B-1 的修复面）。
 *
 * 动机：九命令流程的交互全部是模态对话框（QFileDialog/QInputDialog/
 * QMessageBox/参数表单 QDialog::exec）——gui 测试进程会被模态阻塞，
 * 契约 acceptance 的具名用例无落位路径。本抽象把**交互应答**与**流程
 * 编排**分离：生产装配用 QtDialogHost（真实模态——行为与直调 Qt 完全
 * 一致）；测试注入预置应答替身（gui 用例自动化）。
 *
 * 值面约定：所有方法返回 nullopt/false＝用户取消（流程静默终止——
 * 非失败）；表单方法就地修改入参参数并返回是否确认。
 */
struct CommandDialogHost {
    virtual ~CommandDialogHost() = default;

    /// 导出目标路径（QFileDialog::getSaveFileName 应答面）。
    virtual std::optional<QString> saveFilePath(const QString& title,
                                                const QString& defaultPath,
                                                const QString& filter) = 0;
    /// 导入源路径（QFileDialog::getOpenFileName 应答面）。
    virtual std::optional<QString> openFilePath(const QString& title,
                                                const QString& filter) = 0;
    /// 条目选择（QInputDialog::getItem 应答面——返回 items 下标）。
    virtual std::optional<int> chooseItem(const QString& title,
                                          const QString& label,
                                          const QStringList& items) = 0;

    /// 模板参数表单（六类下拉＋数值行＋计数预览——生产为 QDialog）。
    /// @param kind [in,out] 初始/确认的类别；取消时保持原值，禁止由参数猜测类别。
    /// @param params [in,out] 进入＝表单预填（域黄金默认）；确认＝用户改后值。
    virtual bool editTemplateParams(TemplateKind& kind, TemplateParams& params) = 0;

    /// 阵列参数表单（构型选择在 chooseItem——本面承载数值参数）。
    /// @param count [in,out] 生成数量；@param spacing [in,out] 间距/半径。
    virtual bool editArrayParams(int& count, double& spacing) = 0;

    /**
     * @brief 暂存人工列映射及单位，显示域函数SI换算预览；取消不改入参。
     * @param table [in] 已解析CSV，仅调用期借用；表头必须存在。
     * @param mapping [in,out] 自动映射起点，确认才覆盖。
     * @param units [in,out] 每字段单位声明，确认才覆盖，落库仍经core转换。
     * @return true确认，false取消；仅UI线程，用户错误就地呈现。
     */
    virtual bool editImportMapping(const io::RawTable& table, FieldMapping& mapping,
                                    ImportUnitOptions& units) = 0;

    /// 导入预览确认（计数＋全量行级错误的可滚动纯文本；取消不写草稿）。
    virtual bool confirmImport(const QString& summaryText) = 0;

    /// 重生成/解除双动作选择（QMessageBox 三钮应答面）。
    enum class RegenerateAction { Cancel, Regenerate, Detach };
    virtual RegenerateAction chooseRegenerateAction() = 0;
};

/// 生产宿主（真实 Qt 模态——flows 的缺省装配；单例无状态）。
CommandDialogHost& qtDialogHost();

/**
 * @brief 执行一条需求域命令的 UI 流程（九命令统一入口——测试缝重载）。
 *
 * @param host [in] 对话框宿主（生产＝qtDialogHost()；测试＝预置替身）
 * 其余参数与返回值同无 host 重载。
 */
bool executeRequirementCommand(const std::string& commandId,
                               RequirementsPanelWidget& panel,
                               IRequirementEditor& editor,
                               IRequirementEditSink& sink,
                               CommandDialogHost& host);

/// 生产装配重载（host＝qtDialogHost()——面板 executeDomainCommand 消费）。
bool executeRequirementCommand(const std::string& commandId,
                               RequirementsPanelWidget& panel,
                               IRequirementEditor& editor,
                               IRequirementEditSink& sink);

/**
 * @brief 同 host 重载（带三维视图缝——UI-T33：requirements.capture-tcp/
 *        pick-feature 随缝在位解除降级，走域捕获服务真实现；view3d 为
 *        空指针＝两命令保持诚实降级原文，其余命令零影响）。
 *
 * @param view3d [in] 三维视图缝（门面 RequirementsView3DSeams——宿主
 *               网关真数据源；可为空指针）
 */
bool executeRequirementCommand(const std::string& commandId,
                               RequirementsPanelWidget& panel,
                               IRequirementEditor& editor,
                               IRequirementEditSink& sink,
                               CommandDialogHost& host,
                               const RequirementsView3DSeams* view3d);

}  // namespace sdurws::ird::requirements

#endif  // IRD_REQUIREMENTS_PLUGIN_REQUIREMENTSCOMMANDFLOWS_HPP
