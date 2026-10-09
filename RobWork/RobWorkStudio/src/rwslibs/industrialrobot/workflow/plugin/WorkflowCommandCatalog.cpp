/**
 * @file   WorkflowCommandCatalog.cpp
 * @brief  workflow 命令贡献目录实现（WP-22-T12）——描述符冻结行组装、
 *         处理器薄适配与注册装配（设计依据见 WorkflowCommandCatalog.hpp
 *         文件头；本翻译单元属 plugin 目标——Qt 允许面）。
 *
 * 实现口径：
 *   - 描述符行与 ui.md §7.1 冻结表逐列同值（id/titleKey/keywordKeys/
 *     category/scope/readOnlyAllowed/defaultShortcut/menuPath）——同一
 *     命令在壳层（ui WorkbenchContent kMinimalCommandRows）与贡献清单
 *     两处描述符同值，测试逐字段钉扎（两卡增量同步义务：ui 冻结表修订
 *     时本表同批修订，不一致＝实现偏差）；
 *   - 处理器只做适配（编排核调用＋outcome 折叠），分流逻辑零复制
 *     （NFR-MNT-03：分流唯一实现点在 src/Commands.cpp）。
 */

#include "WorkflowCommandCatalog.hpp"

#include <QKeySequence>
#include <QString>

#include <iterator>  // std::size——冻结行数组的编译期长度（C++17）

namespace sdurws {
namespace ird {
namespace workflow {

namespace {

/// 冻结描述符行（编译期形态——ui.md §7.1 表行的本单元登记位；行序＝
/// 表行序，registrationOrder 的稳定排序锚 NFR-COR-02）。keywordKeys 以
/// nullptr 填充（不足两条的行）——与 ui 壳层同款承载。
struct FrozenCommandRow {
    const char* id;              ///< 命令 id（ui.md §7.1 冻结词形——段内大小写不强制）
    const char* keywordKeys[2];  ///< 关键字键（cmd.<id>.kw.<n>；不足 nullptr 填充）
    ui::CommandCategory category;///< 分类（面板二级分组轴——§7.1 词表）
    ui::CommandScope scope;      ///< 作用域（Session/Project/View——§7.1 词表）
    bool readOnlyAllowed;        ///< 只读会话是否可用（§7.6——false 且不可写→禁用）
    const char* defaultShortcut; ///< 默认键（nullptr＝无默认——面板可达 UX-13 兜底）
    const char* menuPath;        ///< 菜单与面板分组路径（"文件/新建"）
};

/// ui.md §7.1 冻结表逐行（14 条——最小集 10＋工业高频 4 id；归属列不
/// 入描述符——归属只决定处理器语义，描述符数据面两处同值）。
constexpr FrozenCommandRow kFrozenRows[] = {
    {"project.new",              {"cmd.project.new.kw.0", "cmd.project.new.kw.1"},              ui::CommandCategory::Project,   ui::CommandScope::Session, true,  "Ctrl+N",      "文件/新建"},
    {"project.open",             {"cmd.project.open.kw.0", nullptr},                            ui::CommandCategory::Project,   ui::CommandScope::Session, true,  "Ctrl+O",      "文件/打开"},
    {"draft.save",               {"cmd.draft.save.kw.0", nullptr},                              ui::CommandCategory::Edit,      ui::CommandScope::Project, false, "Ctrl+S",      "文件/保存草稿"},
    {"draft.apply",              {"cmd.draft.apply.kw.0", nullptr},                             ui::CommandCategory::Edit,      ui::CommandScope::Project, false, "Ctrl+Return", "文件/应用修改"},
    {"project.undo",             {"cmd.project.undo.kw.0", nullptr},                            ui::CommandCategory::Edit,      ui::CommandScope::Project, false, "Ctrl+Z",      "编辑/撤销"},
    {"project.redo",             {"cmd.project.redo.kw.0", nullptr},                            ui::CommandCategory::Edit,      ui::CommandScope::Project, false, "Ctrl+Y",      "编辑/重做"},
    {"scheme.switch",            {"cmd.scheme.switch.kw.0", nullptr},                           ui::CommandCategory::Stage,     ui::CommandScope::Project, false, nullptr,       "阶段/切换方案"},
    {"project.saveAs",           {"cmd.project.saveAs.kw.0", nullptr},                          ui::CommandCategory::Project,   ui::CommandScope::Project, false, nullptr,       "文件/项目另存为"},
    {"package.export",           {"cmd.package.export.kw.0", nullptr},                          ui::CommandCategory::Project,   ui::CommandScope::Project, false, nullptr,       "文件/导出评估包"},
    {"report.export",            {"cmd.report.export.kw.0", nullptr},                           ui::CommandCategory::Report,    ui::CommandScope::Project, true,  nullptr,       "文件/导出报告"},
    {"analysis.collisionCheck",  {"cmd.analysis.collisionCheck.kw.0", "cmd.analysis.collisionCheck.kw.1"}, ui::CommandCategory::Analysis, ui::CommandScope::Project, false, nullptr, "工具/碰撞检查"},
    {"view.displayMode",         {"cmd.view.displayMode.kw.0", nullptr},                        ui::CommandCategory::View,      ui::CommandScope::View,    true,  nullptr,       "视图/显示模式"},
    {"view.resetHome",           {"cmd.view.resetHome.kw.0", "cmd.view.resetHome.kw.1"},        ui::CommandCategory::View,      ui::CommandScope::View,    true,  nullptr,       "视图/复位到 home 位"},
    {"view.resetZero",           {"cmd.view.resetZero.kw.0", "cmd.view.resetZero.kw.1"},        ui::CommandCategory::View,      ui::CommandScope::View,    true,  nullptr,       "视图/复位到零位"},
};

/// 单行 → 描述符（titleKey 键形 "cmd.<id>.title" 由 id 唯一构造——
/// ui.md §3.5 键约定；iconKey/params 空＝阶段 A 不消费）。
ui::CommandDescriptor toDescriptor(const FrozenCommandRow& row)
{
    ui::CommandDescriptor desc;
    desc.id = row.id;
    desc.ownerUnit = "workflow";  // ui.md §11.1 静态白名单第 8 token——§7.2 owner 校验放行依据
    desc.titleKey = std::string("cmd.") + row.id + ".title";
    for (const char* keywordKey : row.keywordKeys) {
        if (keywordKey != nullptr) {
            desc.keywordKeys.emplace_back(keywordKey);  // nullptr 填充段跳过
        }
    }
    desc.category = row.category;
    desc.scope = row.scope;
    desc.readOnlyAllowed = row.readOnlyAllowed;
    desc.bindable = true;  // SA-16：全体可经 ui 快捷键表改绑——插件不私占
    if (row.defaultShortcut != nullptr) {
        // 默认绑定数据面（ui §7.1 冻结表默认键列）——真实键注册/冲突
        // 拒绝归 ui HotkeyBindingTable（SA-16 唯一注册点），本值仅供
        // 注册表呈现与快捷键表装配期 registerDefault 消费。
        desc.defaultShortcut = QKeySequence(QString::fromLatin1(row.defaultShortcut));
    }
    desc.menuPath = row.menuPath;
    return desc;
}

}  // namespace

std::vector<ui::CommandDescriptor> workflowCommandDescriptors()
{
    // 行序＝冻结表行序（每次现产、无缓存——确定性 NFR-COR-02）。
    std::vector<ui::CommandDescriptor> descriptors;
    descriptors.reserve(std::size(kFrozenRows));
    for (const FrozenCommandRow& row : kFrozenRows) {
        descriptors.push_back(toDescriptor(row));
    }
    return descriptors;
}

std::vector<ui::CommandDescriptor> WorkflowCommandContributor::contributedCommands() const
{
    return workflowCommandDescriptors();  // 无状态直通——Draft 签名逐字兑现
}

ui::CommandOutcome foldToCommandOutcome(const WorkflowCommandOutcome& outcome)
{
    ui::CommandOutcome folded;
    folded.accepted = outcome.accepted;
    // 空串＝无特别消息（nullopt）；非空＝文案键直通（值解析归 ui
    // UiText——UX-02 键值分工在本折叠点保持）。
    if (!outcome.messageKey.empty()) {
        folded.messageKey = outcome.messageKey;
    }
    return folded;
}

std::map<std::string, ui::ICommandRegistry::CommandHandler> makeWorkflowCommandHandlers(
    const WorkflowCommandPorts& ports)
{
    // 以命令 id 全集为行集（词表单点）逐条产处理器——每条处理器是薄
    // 适配：编排核调用＋折叠，零分流逻辑（分流唯一实现点在编排核）。
    // ports 按值捕获指针集（非 owning——存活期由装配层保证）。
    std::map<std::string, ui::ICommandRegistry::CommandHandler> handlers;
    for (const std::string& id : workflowCommandIds()) {
        handlers.emplace(id, [ports, id](const std::vector<ui::CommandParameter>&) {
            // 阶段 A 最小集全部无参（ui §7.1 ParameterSchema 恒空）——
            // 参数表不透传编排核（带参命令随首个带参命令任务增量冻结）。
            return foldToCommandOutcome(runWorkflowCommand(id, ports));
        });
    }
    return handlers;
}

std::vector<ui::RegistrationResult> registerWorkflowCommands(
    ui::ICommandRegistry& registry, const WorkflowCommandPorts& ports)
{
    const std::vector<ui::CommandDescriptor> descriptors = workflowCommandDescriptors();
    const auto handlers = makeWorkflowCommandHandlers(ports);

    // 逐条登记（行序＝冻结表序）；结果原样返回不吞——重复 id 的注册
    // 边界拒绝（ui §7.2 设施行为：DuplicateId＋UI-CMD-DUPLICATE）由
    // 调用方裁决（真实宿主融合形态同 id 壳层命令走覆写通道，不经本
    // 函数二次注册）。
    std::vector<ui::RegistrationResult> results;
    results.reserve(descriptors.size());
    for (const ui::CommandDescriptor& descriptor : descriptors) {
        const auto it = handlers.find(descriptor.id);
        results.push_back(registry.registerCommand(descriptor, it->second));
    }
    return results;
}

}  // namespace workflow
}  // namespace ird
}  // namespace sdurws
