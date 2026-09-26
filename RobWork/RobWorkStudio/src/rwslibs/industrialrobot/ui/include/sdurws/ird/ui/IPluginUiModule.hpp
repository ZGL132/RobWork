/**
 * @file   IPluginUiModule.hpp
 * @brief  插件界面模块接口（IPluginUiModule）——各业务域插件界面目标实现
 *         的统一契约（ui.md §11.2 冻结形状的代码落点）。
 *
 * 设计依据：
 *   - units/ui.md §11.2（三方法冻结签名：onShellReady／readonlyProjections/
 *     buildDraftCommand）、§10.9（面板工厂经 PluginUiDescriptor::panels
 *     提供——本接口不携带面板创建面）、§11.1（ui 只经公共端口消费业务
 *     投影——本头即该公共端口的模块半区）；
 *   - 需求 SA-01（静态白名单装配）、ARC-02（插件界面之间不互取指针——
 *     协作只经本接口与命令注册表）、UX-14（装配报告进关于页）；
 *   - 落位任务：WP-24-T03 首版装配（owner 指示 2026-09-26 提前启动）——
 *     P-MDL-8 消账载体（modeling 侧 ModelingUiModule 原为按本卡冻结文本
 *     的"逐方法同形实现"，本头落位后切换为继承，语义零变化）。
 *
 * 背景说明（为什么这个接口属于 ui 单元）：业务域插件界面（modeling/
 * requirements/…的 _plugin 目标）彼此不依赖（R-1），它们与工作台壳的
 * 全部协作只经两类公共端口——本接口（模块→壳方向的初始化与投影供给）
 * 与 IPluginUiRegistrar（装配层→注册表方向的登记）。接口归 ui 冻结，
 * 业务单元只实现不定义——PA-1"命令入口权威归 ui"的接口面落点。
 *
 * 包含面纪律：本头**零 project include**（NoCrossUnitInclude_O31_UI_BUILD
 * 常驻扫描——ui 头对 project/evidence/execution/policy/runtime 零包含零
 * 链接）；CommandEnvelope 以前向声明承载（声明面不需要完整类型；实现/
 * 消费侧 TU 自含 project/CommandService.hpp——建模→project 登记边）。
 * IWorkbenchShell 亦为前向声明（onShellReady 引用参数不需完整类型；且
 * IWorkbenchShell→UiPorts→AboutDialog 包含链若经本头回引即成包含环——
 * 断环纪律）。消费/实现侧需要完整类型时自含相应头。
 *
 * 线程模型：onShellReady 在装配线程调用（与 registrar 装配同期）；
 *   readonlyProjections／buildDraftCommand 只允许 UI 线程调用（实现侧
 *   自行断言——违规＝实现内 fail-fast，ui 侧不加锁）。
 */
#ifndef SDURWS_IRD_UI_IPLUGINUIMODULE_HPP
#define SDURWS_IRD_UI_IPLUGINUIMODULE_HPP

#include <optional>
#include <string>
#include <vector>

#include <sdurws/ird/ui/UiTypes.hpp>           // ui::DomainReadinessItem（§6.5 汇聚值面）

namespace project {
struct CommandEnvelope;  // 前向声明（buildDraftCommand 返回值元素——见文件头包含面纪律）
}

namespace sdurws {
namespace ird {
namespace ui {

class IWorkbenchShell;  // 前向声明（onShellReady 引用参数——断环见文件头包含面纪律）

/**
 * @brief 插件界面模块（各业务域 _plugin 目标实现的统一契约——ui 冻结）。
 *
 * 生命周期/所有权：实例归插件装配层（L5/宿主插件）创建与持有；经
 * IPluginUiRegistrar::registerPluginUi 登记后 registrar 只持弱引用＋id
 * 索引（§10.9 所有权行）——模块存活至壳拆除（§10.9 前置条件），无注销
 * （静态白名单 SA-01）。
 */
class IPluginUiModule {
public:
    virtual ~IPluginUiModule() = default;

    /**
     * @brief 壳就绪回调（§11.2"注册回调后初始化〔订阅/面板内容〕"）。
     *
     * 调用时序：registerPluginUi 成功后、mainWindow 显示前（§10.9 装配
     * 时序②③之间）；实现侧据此完成订阅与面板内容初始化。域命令登记
     * 不在本回调（经装配描述符在装配期一次完成——§10.9"装配期一次"）。
     *
     * @param shell [in] 工作台壳门面（非 owning——存活期由壳侧保证）
     */
    virtual void onShellReady(IWorkbenchShell& shell) = 0;

    /**
     * @brief 只读业务投影（§6.5 汇聚源——StageStatusModel 汇聚的域行）。
     *
     * 纯查询（零副作用）；返回值语义由各域自解释（输入不完整＝缺省行，
     * 不伪造可行性——UX-02/ERR-01 同源纪律）。
     *
     * @return 域就绪投影行（可空向量＝域无投影内容）
     */
    virtual std::vector<DomainReadinessItem> readonlyProjections() const = 0;

    /**
     * @brief 草稿应用命令组装（§8.5 应用时命令组装的域侧半区——L-3
     *        draft.apply 流的域侧入口）。
     *
     * 语义（§8.5）：draft.apply 执行时壳侧逐模块征询；有可应用草稿的
     * 模块返回域命令信封（project commandType 词表），无可应用内容返回
     * nullopt（不产生空修订）。信封的合法性由 project 命令管线校验
     * （本接口零判定——域侧组装只做确定性编码）。
     *
     * @param moduleId [in] 域注册键（白名单 token，如 "modeling"）
     * @return 域命令信封；nullopt＝域外请求／无可应用变更
     */
    virtual std::optional<project::CommandEnvelope> buildDraftCommand(
        const std::string& moduleId) = 0;
};

}  // namespace ui
}  // namespace ird
}  // namespace sdurws

#endif  // SDURWS_IRD_UI_IPLUGINUIMODULE_HPP
