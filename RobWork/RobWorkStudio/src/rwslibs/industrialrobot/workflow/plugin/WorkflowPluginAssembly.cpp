/**
 * @file   WorkflowPluginAssembly.cpp
 * @brief  workflow 插件装配门面的实现翻译单元——descriptor 现产与模块
 *         创建的唯一装配点（WP-22-T02 最小可注册形态）。
 *
 * 设计依据：
 *   - units/ui.md §10.9（PluginUiDescriptor/PluginCapabilities 字段语义）、
 *     §11.1（白名单八 token——"workflow" 在册）
 *   - units/workflow.md §3.2（_plugin 目标落位口径——T02 最小可注册）
 *   - 先例：requirements/plugin/RequirementsPluginAssembly.cpp（O-45 门面
 *     实现形态——descriptor 现产、模块 unique_ptr 交装配产物结构持有）
 *
 * 线程模型：全部函数装配线程（UI 线程）调用——见门面头文件注。
 */

#include <sdurws/ird/workflow/WorkflowPluginAssembly.hpp>

#include <utility>

#include <sdurws/ird/ui/IPluginUiModule.hpp>  // ui::IPluginUiModule 完整类型（析构/移动实现所需——声明面此前为前向声明）

#include "WorkflowUiModule.hpp"  // WorkflowUiModule（同单元插件私有头——模块具体类型，R-2 同单元内消费不跨单元）

namespace sdurws::ird::workflow {

// ---- 特殊成员函数（完整类型在此 TU 可见——门面头文件注的落点）----------

WorkflowPluginAssembly::WorkflowPluginAssembly() = default;
WorkflowPluginAssembly::~WorkflowPluginAssembly() = default;
WorkflowPluginAssembly::WorkflowPluginAssembly(
    WorkflowPluginAssembly&&) noexcept = default;
WorkflowPluginAssembly&
WorkflowPluginAssembly::operator=(WorkflowPluginAssembly&&) noexcept = default;

WorkflowPluginAssembly createWorkflowPluginAssembly()
{
    // 第一步：创建模块实例（具体类型在此闭包内可见——出线后以接口面
    // unique_ptr 持有，宿主装配层零插件私有头依赖——R-2/O-31 门面纪律）。
    WorkflowPluginAssembly assembly;
    assembly.module = std::make_unique<WorkflowUiModule>();

    // 第二步：现产装配描述符（字段值与门面头文件工厂注逐条对应；§10.9
    // 校验序的前置——pluginId/titleKey/stages 均非空即满足合法性三查）。
    assembly.descriptor.pluginId = "workflow";  // 白名单第 8 token（§11.1）
    assembly.descriptor.titleKey =
        "plugin.workflow.title";  // ui.md §3.5 键族（UI-T10 冻结八键）
    // stages＝七阶段全表（UX-12 冻结序＝StageId 枚举序——门控所有权 O1
    // 覆盖全部阶段；StageId 词表归 ui，本单元零新增枚举——SA-12/D-WF-4）。
    assembly.descriptor.stages = {
        ui::StageId::Modeling,      // 建模（七阶段第 1）
        ui::StageId::Requirements,  // 需求（第 2）
        ui::StageId::Kinematics,    // 运动学（第 3）
        ui::StageId::TrajectoryDynamics,  // 轨迹/动力学（第 4——聚合阶段）
        ui::StageId::Selection,     // 选型（第 5）
        ui::StageId::Optimization,  // 优化（第 6）
        ui::StageId::Reporting,     // 报告（第 7）
    };
    // 能力声明三 false＋commands/panels 空＝T02 最小可注册形态（§10.9
    // "描述性，非判定性"；缺省值即 false/空——显式写出以自文档化，后续
    // 批次按需置位：T04+ 面板置 providesStagePanel、T12 命令置
    // registersCommands、T09 横幅/标题栏投影不占此三能力位）。
    assembly.descriptor.capabilities.providesStagePanel = false;
    assembly.descriptor.capabilities.providesReadonlyProjection = false;
    assembly.descriptor.capabilities.registersCommands = false;
    // commands/panels 保持空向量（缺省构造——T12/T04+ 增列点见上注）。

    return assembly;
}

}  // namespace sdurws::ird::workflow
