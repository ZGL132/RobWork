/**
 * @file   KinPanelCommandCatalog.cpp
 * @brief  域命令登记目录与装配登记记录的实现（KinPanelCommandCatalog.hpp
 *         全部落点）。
 *
 * 设计依据：KinPanelCommandCatalog.hpp 文件头（本 TU 是其全部函数的实现
 * 落点）；命令要素权威＝units/kinematics.md §9.8 域命令登记清单表。
 */

#include "KinPanelCommandCatalog.hpp"

#include <sdurws/ird/kinematics/Commands.hpp>  // kFacadedModelingCommand（project 词表分离守卫值）

namespace sdurws {
namespace ird {
namespace kinematics {
namespace {

/// 命令 id 词表（§9.8 表行序——八条；点分小写，ui CommandId 词表）。
constexpr const char* kCommandIds[] = {
    "kinematics.analyze-pose",         // 行 1：当前位姿指标（会话级）
    "kinematics.solve-ik",             // 行 2：单点 IK（会话级）
    "kinematics.validate-task-points", // 行 3：批量验证提交（写 results）
    "kinematics.evaluate-coverage",    // 行 4：区域覆盖提交（写 results）
    "kinematics.set-default-tcp",      // 行 5：设默认 TCP（KIN-14——新修订）
    "kinematics.set-default-device",   // 行 5 同行：设默认设备（KIN-14）
    "kinematics.export-results",       // 行 6：JSON/CSV 副本导出
    "kinematics.reset-session-pose",   // 行 7：复位 Home（会话）
};

/// readOnlyAllowed 逐行值（与 kCommandIds 同序——§9.8 表第 3 列权威）。
constexpr bool kCommandReadOnlyAllowed[] = {
    true,   // analyze-pose：会话级
    true,   // solve-ik：会话级
    false,  // validate-task-points：写 results——正式
    false,  // evaluate-coverage：写 results——正式
    false,  // set-default-tcp：新修订
    false,  // set-default-device：新修订
    true,   // export-results：副本导出（零修订）
    true,   // reset-session-pose：会话
};

/// 作用域逐行值（会话级三行→Session；其余→Project——登记要素见头注）。
/// 与 kCommandIds 同序。
constexpr ui::CommandScope kCommandScopes[] = {
    ui::CommandScope::Session,  // analyze-pose
    ui::CommandScope::Session,  // solve-ik
    ui::CommandScope::Project,  // validate-task-points
    ui::CommandScope::Project,  // evaluate-coverage
    ui::CommandScope::Project,  // set-default-tcp
    ui::CommandScope::Project,  // set-default-device
    ui::CommandScope::Project,  // export-results
    ui::CommandScope::Session,  // reset-session-pose
};

static_assert(sizeof(kCommandReadOnlyAllowed) / sizeof(bool)
                  == sizeof(kCommandIds) / sizeof(const char*),
              "§9.8 命令表 readOnlyAllowed 行数与 id 行数必须一致");
static_assert(sizeof(kCommandScopes) / sizeof(ui::CommandScope)
                  == sizeof(kCommandIds) / sizeof(const char*),
              "§9.8 命令表作用域行数与 id 行数必须一致");

}  // namespace

std::vector<ui::CommandDescriptor> kinematicsDomainCommands()
{
    std::vector<ui::CommandDescriptor> out;
    out.reserve(sizeof(kCommandIds) / sizeof(const char*));
    for (std::size_t i = 0; i < sizeof(kCommandIds) / sizeof(const char*); ++i) {
        ui::CommandDescriptor d;
        d.id = kCommandIds[i];
        // ownerUnit＝白名单 token（§7.2 注册协议第 1 步校验面——§11.1 词表）。
        d.ownerUnit = "kinematics";
        // 文案键按 §3.5 约定 "cmd.<id>.title"（值归 ui 文案资源——UX-02）。
        d.titleKey = std::string("cmd.") + kCommandIds[i] + ".title";
        // 阶段面命令族（modeling 先例同案——§7.1 分类词表 Stage 位）。
        d.category = ui::CommandCategory::Stage;
        d.scope = kCommandScopes[i];
        d.readOnlyAllowed = kCommandReadOnlyAllowed[i];
        // SA-16：默认快捷键全空＋bindable=true——需要绑定时经 ui
        // HotkeyBindingTable 用户级配置，插件不私占全局快捷键。
        d.bindable = true;
        d.defaultShortcut = std::nullopt;
        // 菜单分组（§7.1 MenuPath——阶段菜单"阶段/运动学"）。
        d.menuPath = "阶段/运动学";
        out.push_back(std::move(d));
    }
    return out;
}

std::vector<std::string> kinematicsProjectCommandTokens()
{
    // kinematics 域唯一的 project 侧 token（Commands.hpp 冻结常量——设默认
    // 门面组装的 modeling 命令；域命令 id 与 project commandType 两套命名
    // 空间不得交叉）。
    return {std::string(kFacadedModelingCommand)};
}

bool isKinWriteCommand(const std::string& commandId)
{
    // 写类命令族＝§9.8 表 readOnlyAllowed=false 的四 id——以禁用清单逐字
    // 比对（与 kinematicsDomainCommands 的登记值同源同义；此处独立成表是
    // 为了给"未登记命令"一个确定的非写判定面）。
    return commandId == "kinematics.validate-task-points"
        || commandId == "kinematics.evaluate-coverage"
        || commandId == "kinematics.set-default-tcp"
        || commandId == "kinematics.set-default-device";
}

KinPanelRegistration kinematicsPanelRegistration()
{
    KinPanelRegistration reg;
    // 面板五行（§9.8 面板表行序——确定性登记序；行 1~4 主面板，行 5
    // 求解配置 advanced=true——UX-04 高级参数收拢）。
    reg.panels = {
        {"pose-metrics", "stage.kinematics.panel.pose-metrics.title", false},
        {"task-points", "stage.kinematics.panel.task-points.title", false},
        {"region-coverage", "stage.kinematics.panel.region-coverage.title", false},
        {"results-view", "stage.kinematics.panel.results-view.title", false},
        {"solver-config", "stage.kinematics.panel.solver-config.title", true},
    };
    return reg;
}

std::vector<ui::DomainReadinessItem> kinematicsReadinessProjection(
    bool inputComplete, bool hasActiveTask)
{
    // 单元素投影（§6.5 汇聚源——零判定直投；缺省行＝DataInsufficient，
    // 不伪造可行性——UX-02/ERR-01）。
    ui::DomainReadinessItem item;
    item.domainKey = "kinematics";
    item.verdict = core::EngineeringStatus::DataInsufficient;
    item.inputComplete = inputComplete;
    // 缺项键词形 "missing.<layer-token>"（UX-02：界面只见键与局部名，零
    // 哈希）——输入不完整时至少携带任务点切片缺项锚。
    if (!inputComplete) {
        item.missingItemKeys.push_back("missing.task-points-slice");
    }
    item.hasActiveTask = hasActiveTask;
    return {item};
}

bool kinCommandEnabledInSession(const std::string& commandId, bool writable)
{
    // 只读门控（§9.8 L-K11）：写类命令在只读会话禁用；只读类命令恒可用
    // （会话级 FK 预览/单位切换/副本导出/复位 Home 不受影响）。未登记 id
    // 恒 false（不虚构可达性）。
    bool known = false;
    for (const char* id : kCommandIds) {
        if (commandId == id) {
            known = true;
            break;
        }
    }
    if (!known) {
        return false;
    }
    if (isKinWriteCommand(commandId)) {
        return writable;
    }
    return true;
}

}  // namespace kinematics
}  // namespace ird
}  // namespace sdurws
