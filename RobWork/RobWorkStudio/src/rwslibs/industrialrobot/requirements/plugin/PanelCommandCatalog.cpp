/**
 * @file   PanelCommandCatalog.cpp
 * @brief  requirements 域命令登记目录实现——九条 CommandId 与装配值（卡
 *         §9.8 命令表）。
 *
 * 设计依据：units/requirements.md §9.8 命令表逐行（登记序＝表行序）、
 * ui.md §10.9/§11.1/§11.2、§6.5、§7.3（SA-16）；契约 WP-14-T08
 * acceptance 1/4/5。实现纪律：清单即数据（零逻辑）；行序确定性＝卡表行序；
 * 命令文案键按 ui §3.5 键约定派生（值解析归 UiText——键即契约）。
 */

#include "PanelCommandCatalog.hpp"

#include <sdurws/ird/requirements/CommandHandlers.hpp>  // kCmdApplyRequirement* token（project 侧无点词表——命名空间分离守卫的另一半）

namespace sdurws::ird::requirements {

// =====================================================================
// 九条域命令（行序＝卡 §9.8 命令表行序——登记序确定性）
// =====================================================================

std::vector<ui::CommandDescriptor> requirementsDomainCommands()
{
    // 行构造辅助：公共要素（ownerUnit/category/scope/bindable/无默认快捷键）
    // 在此集中；行内只写差异（id/readOnlyAllowed/menuPath）——清单的单一
    // 事实来源＝卡 §9.8 命令表，本函数是其逐一值化。
    auto make = [](const char* id, bool readOnlyAllowed, const char* menuPath) {
        ui::CommandDescriptor d;
        d.id = ui::CommandId(id);      // CommandId＝字符串强别名（点分小写——§7.2 句法校验归注册表）
        d.ownerUnit = "requirements";  // 白名单 token（§11.1 八 token 词表）
        d.titleKey = ui::TextKey(std::string("cmd.") + id + ".title");  // §3.5 键约定
        d.category = ui::CommandCategory::Stage;  // 阶段面命令族（§7.1）
        d.scope = ui::CommandScope::Project;      // 项目作用域域命令族（§9.8 全表）
        d.readOnlyAllowed = readOnlyAllowed;      // 卡表值——L-R12 只读门控的命令半区
        d.bindable = true;                        // 可经 HotkeyBindingTable 用户级绑定（SA-16）
        // defaultShortcut 缺省 nullopt＝默认零绑定（SA-16：插件不私占全局
        // 快捷键；需要绑定时经 ui HotkeyBindingTable 用户级配置——与任何
        // 既有绑定构造性无冲突，acceptance 5"无冲突登记"的结构保证）。
        d.menuPath = menuPath;                    // 面板/菜单分组（域命令区"需求"分组）
        return d;
    };

    return {
        // ① CSV 导入向导（L-R10——io 预检→映射→逐行错误→确认→草稿）。
        make("requirements.import-csv", false, "需求/导入"),
        // ② JSON 导入向导（与 CSV 两条独立命令——卡面分列）。
        make("requirements.import-json", false, "需求/导入"),
        // ③ JSON/CSV 副本导出（REQ-12：导出为副本不影响项目——只读可用）。
        make("requirements.export-copy", true, "需求/导出"),
        // ④ TCP 捕获（L-R7：会话读＋确认写草稿——只读会话也可捕获查看，
        //   写回经确认门且草稿写路径受 L-R12 行面门控）。
        make("requirements.capture-tcp", true, "需求/捕获"),
        // ⑤ 几何特征拾取态（L-R6——View3D 拾取入口；只读可用同④语义）。
        make("requirements.pick-feature", true, "需求/捕获"),
        // ⑥ 工位镜像（L-R9——写路径，readOnlyAllowed=false）。
        make("requirements.mirror-stations", false, "需求/派生"),
        // ⑦ 四类批量阵列（L-R9——写路径）。
        make("requirements.create-array", false, "需求/派生"),
        // ⑧ 工艺模板应用（L-R9——写路径）。
        make("requirements.apply-template", false, "需求/派生"),
        // ⑨ 按模板重生成（L-R9——写路径；冲突不静默覆盖）。
        make("requirements.regenerate-linked", false, "需求/派生"),
    };
}

std::vector<std::string> requirementsProjectCommandTokens()
{
    // project commandType 词表（无点语法——project §4.4.4 冻结语法；O-35
    // 裁决形态）：requirements 域已落位的命令处理器族 token（CommandHandlers
    // .hpp 冻结常量直用——不私写字面量，单一权威）。
    return {std::string(kCmdApplyRequirementSet), std::string(kCmdApplyRequirementImport)};
}

// =====================================================================
// 装配描述符
// =====================================================================

PanelRegistrationRecord requirementsPanelRegistration()
{
    PanelRegistrationRecord rec;
    rec.pluginId = "requirements";  // §11.1 静态白名单 token
    rec.titleKey = ui::TextKey("stage.requirements.title");  // 阶段面板标题键（§3.5）
    rec.stage = ui::StageId::Requirements;  // 挂位阶段（§9.8 面板表——需求阶段）
    rec.capabilities.providesStagePanel = true;          // 对象树＋工位/区域/工况/校验四面板
    rec.capabilities.providesReadonlyProjection = true;  // §6.5 汇聚源（就绪投影）
    rec.capabilities.registersCommands = true;           // 九条域命令（§9.8 命令表）
    rec.advanced = false;                                // 主面板（非 UX-04 高级面板）
    return rec;
}

// =====================================================================
// 域就绪投影
// =====================================================================

std::vector<ui::DomainReadinessItem> requirementsReadinessProjection(
    const RequirementReadinessReport& report)
{
    ui::DomainReadinessItem item;
    item.domainKey = "requirements";  // 域注册键（§6.5 词表）

    // inputComplete＝"无 Blocking 发现"的事实直投（hasBlocking 是报告自带的
    // 事实查询——Readiness.hpp 契约；该位的门控消费归 workflow/ui，本投影
    // 只供数——P-REQ-6 边界）。
    item.inputComplete = !report.hasBlocking();

    // verdict：零判定直投（§8.3 正交表——需求未就绪≠工程不可行；本单元
    // 不产出工程判定，"无 Blocking"不冒充"可行"→NotApplicable＝待评估域
    // 判定；有 Blocking＝输入不完整→DataInsufficient——①级门禁数据源
    // 语义，与 evidence ReadinessSummary.valid 同源事实）。
    item.verdict = report.hasBlocking() ? core::EngineeringStatus::DataInsufficient
                                        : core::EngineeringStatus::NotApplicable;

    // 缺项键：Warning 级发现的呈现键（"missing.<层 token>"——UX-02：键与
    // 局部名，零哈希/内部标识；Blocking 明细不在此重复——输入不完整由
    // inputComplete 位承载，明细全文在校验面板逐项行）。
    for (const DomainReadinessItem& d : report.items) {
        if (d.level == ReadinessFindingLevel::Warning) {
            item.missingItemKeys.push_back(
                ui::TextKey("missing." + std::string(readinessLayerToken(d.layer))));
        }
    }

    // hasActiveTask：requirements 域无在途任务呈现面（任务呈现归 ui——N-11）。
    item.hasActiveTask = false;

    return {item};
}

// =====================================================================
// L-R12 只读门控
// =====================================================================

std::vector<StationFieldRow> applyReadOnlyGate(std::vector<StationFieldRow> rows, bool writable)
{
    // 只读会话（writable=false）：可编辑行一律降级灰显（L-R12"编辑禁用"）；
    // 已灰显行不变（灰显不因可写性复活——只读会话与溯源字段是两个独立
    // 灰显源，正交）。命令半区（readOnlyAllowed=false 禁用）在命令目录
    // 逐条登记——本函数只管行面（单一登记点纪律）。
    if (!writable) {
        for (StationFieldRow& r : rows) {
            if (r.enablement == StationFieldEnablement::Editable) {
                r.enablement = StationFieldEnablement::ReadOnlyGrey;
            }
        }
    }
    return rows;
}

}  // namespace sdurws::ird::requirements
