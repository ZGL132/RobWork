/**
 * @file   TrajUiModule.cpp
 * @brief  trajectory 插件界面模块的实现翻译单元——§11.2 三方法的
 *         trajectory 侧语义落点（WP-16-T12）。
 *
 * 设计依据：TrajUiModule.hpp 文件头（转接纪律与本域语义锚）。
 *
 * Qt/包含面说明（诚实登记）：本 TU 零 Qt 类消费（三方法均为值/引用面
 * ——投影行与命令信封无 Widgets 类型）；project 命令信封头为完整类型
 * 到达面（buildDraftCommand 返回 optional 的实例化点需要——纯头消费零
 * 链接符号；真实信封构造不发生在本域——恒 nullopt）。
 */

#include "TrajUiModule.hpp"

#include "TrajPanelModule.hpp" // TrajPanelModule（完整类型——session 访问）
#include "TrajPanelTypes.hpp"  // TrajModuleSessionState（投影字段的来源值面）

#include <sdurws/ird/trajectory/TrajectoryPluginAssembly.hpp> // kTrjDomainKey
                                                               //   （域注册键常量
                                                               //   ——assembly 公共
                                                               //   头唯一书写点）

#include <sdurws/ird/project/CommandService.hpp>  // project::CommandEnvelope
                                                  //   （完整类型到达面——§11.2
                                                  //   返回类型实例化点）

#include <utility>  // std::move（投影行转移构造）

namespace sdurws::ird::trajectory {

TrajUiModule::TrajUiModule(TrajPanelModule* module) : m_module(module)
{
    // 装配期程序违约面：模块指针由激活路径保证非空（工厂构造即建模块
    // ——unique_ptr 契约保证）；此处不加运行时检查——空指针解引用属
    // 装配程序缺陷而非用户可见态，契约由调用方承担（dynamics 同款口径）。
}

TrajUiModule::~TrajUiModule() = default;

void TrajUiModule::onShellReady(ui::IWorkbenchShell& shell)
{
    // 只持有壳引用（只读消费）——本域零订阅、零重复注册（语义见头注）。
    // 壳引用存活期由壳侧保证至模块拆除（§10.9 所有权行）。
    m_shell = &shell;
}

std::vector<ui::DomainReadinessItem> TrajUiModule::readonlyProjections() const
{
    // 第一步：会话态纯透传现取（会话事实唯一归属 TrajPanelModule::session
    // ——零判定零缓存，防第二真值；PA-1 权威唯一）。
    const TrajModuleSessionState& state = m_module->session;

    // 第二步：字段一一对应翻译（ui.md §6.5 DomainReadinessItem 冻结形
    // 状——domainKey/verdict/inputComplete/missingItemKeys/hasActiveTask
    // 五字段逐一拷贝，零增删；字段同构缺口由 ui 公共头在本编译单元的
    // 真实可见交编译器校验——字段名/类型漂移即编译错误）。
    ui::DomainReadinessItem item;
    item.domainKey = kTrjDomainKey;              // 域注册键（恒 "trajectory"——§6.5）
    item.verdict = state.verdict;                // 最近正式判定（core 词表直用）
    item.inputComplete = state.inputComplete;    // 就绪校验结论（REQ-06——UX-10）
    item.missingItemKeys = state.missingItemKeys; // 缺项文案键清单（UX-10）
    item.hasActiveTask = state.hasActiveTask;    // 在途任务事实（"计算中"素材）
    return {std::move(item)};
}

std::optional<project::CommandEnvelope> TrajUiModule::buildDraftCommand(
    const std::string& /*moduleId*/)
{
    // 恒 nullopt（§14.5.1 七命令修订列全部"无"——本域无草稿工作集；
    // §8.5"无可应用变更返回 nullopt，不产生空修订"）。域内外请求一致
    // ——零判定（参数不解读）。
    return std::nullopt;
}

}  // namespace sdurws::ird::trajectory
