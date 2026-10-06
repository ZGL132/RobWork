/**
 * @file   WorkflowPluginAssembly.hpp
 * @brief  workflow 插件装配门面——宿主装配层（WP-24 首版装配）消费
 *         workflow 插件的**唯一公共入口**（O-31 装配器单行适配面）。
 *
 * 设计依据：
 *   - units/ui.md §10.9（registerPluginUi 入参形状 PluginUiDescriptor；
 *     白名单 token；装配期一次）、§11.1（白名单八 token 词表——"workflow"
 *     为第 8 个占位 token，先行登记）、§11.2（IPluginUiModule 三方法）
 *   - units/workflow.md §3.2（sdurws_ird_workflow_plugin＝向导/首页/比较
 *     视图/确认对话框宿主面——宿主融合 SA-18 下经 IPluginUiRegistrar/
 *     宿主 Dock 形态装配；T02 批次为最小可注册形态）
 *   - 双先例同构：modeling/assembly/ModelingPluginAssembly.hpp（WP-24-T03
 *     落位形态）、requirements/assembly/RequirementsPluginAssembly.hpp
 *     （O-45 裁决补建缝——"宿主装配层只见本头与 ui 公共头，零插件私有头
 *     依赖（R-2）"）；kinematics/assembly/KinematicsPluginAssembly.hpp
 *     同构。workflow 落位即建门面（不待事后补缝——三先例的教训前移）。
 *
 * 落位形态说明：本头位于 workflow 单元的 `assembly/` 目录（plugin 目标的
 * PUBLIC include 面——插件界面目标的装配契约头，非产品 include/ 扫描域；
 * 与 plugin/ 同理在零 Qt 红线的文件域之外）。实现
 * （plugin/WorkflowPluginAssembly.cpp）编入 sdurws_ird_workflow_plugin 目标。
 *
 * 线程模型：createWorkflowPluginAssembly 在装配线程调用（UI 线程——模块
 * 构造绑定调用线程，§10.9 装配时序）。
 */

#ifndef IRD_WORKFLOW_ASSEMBLY_WORKFLOWPLUGINASSEMBLY_HPP
#define IRD_WORKFLOW_ASSEMBLY_WORKFLOWPLUGINASSEMBLY_HPP

#include <memory>

#include <sdurws/ird/ui/ICommandRegistry.hpp>    // ui::CommandDescriptor 完整类型（PluginUiDescriptor.commands 的
                                                 //   vector 成员析构在每个消费 TU 实例化——IPluginUiRegistrar.hpp 对其
                                                 //   仅前向声明，消费侧须自含完整类型；requirements 门面同款包含面）
#include <sdurws/ird/ui/IPluginUiRegistrar.hpp>  // ui::PluginUiDescriptor（§10.9 装配描述符——值成员需完整类型）

namespace sdurws {
namespace ird {
namespace ui {
class IPluginUiModule;  // 前向声明（module 成员——完整类型在 ui 公共头；
                        // 本头 unique_ptr 成员以不完整类型声明、析构在
                        // 实现 TU 完成——包含面最小化）
}  // namespace ui
}  // namespace ird
}  // namespace sdurws

namespace sdurws::ird::workflow {

/**
 * @brief workflow 插件装配产物（createWorkflowPluginAssembly 返回值）。
 *
 * 消费序（宿主装配层的标准用法——modeling/requirements/kinematics 三先例
 * 同构）：①取 module 与 descriptor；②registrar.registerPluginUi(descriptor,
 * *module)（§10.9 装配期一次；白名单含 "workflow"——§11.1 第 8 token）；
 * ③面板/命令装配随 WP-22-T04+ 批次落位后经 descriptor.panels/commands
 * 扩展（T02 三能力声明全 false、panels/commands 空——装配层按描述符
 * 现状装配即可，无需特判）。
 *
 * 所有权：module 由本结构 unique_ptr 持有。可移动（装配层转移持有）；
 * 不可拷贝（模块不可拷贝——§10.9 生命周期绑定）。
 */
struct WorkflowPluginAssembly {
    /// 插件界面模块（接口面——§11.2 三方法契约；实现＝WorkflowUiModule）。
    std::unique_ptr<ui::IPluginUiModule> module;
    /// 装配描述符（§10.9 形状直通——T02 现产值见工厂函数注）。
    ui::PluginUiDescriptor descriptor;

    /// 构造/析构/移动：析构与移动实现需 module 成员完整类型——落在实现
    /// TU（include 面最小化，requirements 门面同款形态）。
    WorkflowPluginAssembly();
    ~WorkflowPluginAssembly();
    WorkflowPluginAssembly(WorkflowPluginAssembly&&) noexcept;
    WorkflowPluginAssembly& operator=(WorkflowPluginAssembly&&) noexcept;
    WorkflowPluginAssembly(const WorkflowPluginAssembly&) = delete;
    WorkflowPluginAssembly& operator=(const WorkflowPluginAssembly&) = delete;
};

/**
 * @brief 创建 workflow 插件装配产物（每次调用全新实例——无缓存）。
 *
 * T02 批次 descriptor 装配规则（§10.9/§11.1 逐字段）：
 *   - pluginId = "workflow"（白名单第 8 token——§11.1 编译期词表）；
 *   - titleKey = "plugin.workflow.title"（ui.md §3.5 键族 plugin.<id>.title
 *     八键之一——UI-T10 冻结登记；用户见中文名，UX-02 零内部插件名）；
 *   - stages = 七阶段全表（StageId 七值按 UX-12 冻结序——workflow 编排
 *     面覆盖全部阶段：七阶段导航门控即本单元核心所有权 O1）；
 *   - capabilities 三能力全 false（providesStagePanel/providesReadonly-
 *     Projection/registersCommands——T02 无面板无投影无命令；§10.9
 *     "描述性，非判定性"；随 T04+ 批次按需置位）；
 *   - commands 空（最小命令集注册随 WP-22-T12——卡 §8.3）；
 *   - panels 空（向导/首页/比较视图宿主面随 WP-22-T04~T11——卡 §7/§8）。
 *
 * @return 装配产物（UI 线程构造——模块生命周期绑定调用线程）
 */
WorkflowPluginAssembly createWorkflowPluginAssembly();

}  // namespace sdurws::ird::workflow

#endif  // IRD_WORKFLOW_ASSEMBLY_WORKFLOWPLUGINASSEMBLY_HPP
