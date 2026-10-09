/**
 * @file   DynUiModule.cpp
 * @brief  dynamics 插件界面模块的实现翻译单元——§11.2 三方法的落点
 *         （ASM-PLUG 收口批：P-DYN-8 消账形态的模块半区实现）。
 *
 * 设计依据：
 *   - units/ui.md §11.2（三方法冻结签名）、§6.5（DomainReadinessItem
 *     汇聚值——verdict 词表 core 直用、inputComplete/missingItemKeys/
 *     hasActiveTask 事实透传）、§10.9（线程行——onShellReady 装配线程、
 *     投影/草稿 UI 线程）；
 *   - units/dynamics.md §9.5（投影 domainKey="dynamics"；五命令零修订
 *     ——草稿恒空语义锚）；
 *   - plugin/DynPanelModel.hpp（L-D1 纯透传流——本模块投影的唯一数据
 *     面，零第二真值）；
 *   - 先例：workflow/plugin/WorkflowUiModule.cpp（三方法最小实现形态）。
 *
 * 包含面说明（诚实登记）：buildDraftCommand 返回值 std::optional 携带
 *   ui.md §11.2 冻结接口的 project 信封类型——`return std::nullopt` 的
 *   构造/析构路径要求该元素完整类型（optional 成员实例化），故本 TU 自
 *   含 project 公共头（R-2 公共面合规——四先例 modeling/requirements/
 *   kinematics/workflow 的 UiModule 实现同款包含面）。该包含仅服务于
 *   接口签名实例化：**零 project 业务访问**（不调用任何 project 实现
 *   符号——插件纪律由契约测试词表扫描钉住，本 TU 在其具名豁免清单——
 *   豁免面＝该头包含行，非红线松动）。
 */

#include "DynUiModule.hpp"

// 同单元私有头（R-2 不跨单元——本 TU 编入 dynamics 插件目标自身）。
#include "DynPanelModel.hpp"  // DynReadinessRow/readinessProjection（L-D1
                              //   纯透传流——投影唯一数据面）
#include "DynPanelModule.hpp" // DynPanelModule（完整类型——session 访问）

#include <sdurws/ird/project/CommandService.hpp>  // project::CommandEnvelope
                                                  //   完整类型（§11.2 返回值
                                                  //   optional 构造/析构实例化
                                                  //   要求——见文件头包含面说
                                                  //   明；零业务访问）

namespace sdurws::ird::dynamics {

DynUiModule::DynUiModule(DynPanelModule* module)
    : m_module(module)
    // 面板模块指针由装配激活路径保证非空（门面构造即持模块——见类注）。
{
}

DynUiModule::~DynUiModule() = default;

void DynUiModule::onShellReady(ui::IWorkbenchShell& shell)
{
    // §11.2"注册回调后初始化"——只持壳引用（只读消费面）。域命令的注册
    // 经装配描述符在装配期一次完成（§10.9"装配期一次"），本回调零重复
    // 注册；面板内容初始化由面板工厂与 refreshFromSession 通道承载
    // （§9.5 落位形态）——不在此重复（零第二初始化路径，NFR-MNT-03）。
    m_shell = &shell;
}

std::vector<ui::DomainReadinessItem> DynUiModule::readonlyProjections() const
{
    // 第一步：模型层 L-D1 纯透传流现取行（会话事实唯一归属
    // DynPanelModule::session——零判定零缓存，防第二真值）。
    const DynReadinessRow row = readinessProjection(m_module->session);

    // 第二步：字段一一对应翻译（ui.md §6.5 DomainReadinessItem 冻结形
    // 状——domainKey/verdict/inputComplete/missingItemKeys/hasActiveTask
    // 五字段逐一拷贝，零增删；"字段同构无编译期校验"的缺口随真实编译
    // 边建立而解除——字段名/类型漂移即编译错误，ASM-PLUG 收口批）。
    ui::DomainReadinessItem item;
    item.domainKey = row.domainKey;          // 域注册键（恒 "dynamics"）
    item.verdict = row.verdict;              // 最近正式判定（core 词表直用）
    item.inputComplete = row.inputComplete;  // 就绪校验结论（REQ-06）
    item.missingItemKeys = row.missingItemKeys;  // 缺项文案键清单（UX-10）
    item.hasActiveTask = row.hasActiveTask;  // 在途任务事实（"计算中"素材）
    return {std::move(item)};
}

std::optional<project::CommandEnvelope> DynUiModule::buildDraftCommand(
    const std::string& /*moduleId*/)
{
    // dynamics 无草稿（§9.5 UI 协作表"修订"列全部"无"——五命令族全部
    // 为零修订会话命令，本域不持有任何草稿工作集）：§8.5"无可应用变更
    // 返回 nullopt（不产生空修订）"的语义落点——恒 nullopt 是语义实现
    // 而非占位（域内 moduleId 同样无可应用草稿）。零 project 访问：本
    // 函数不构造任何信封（完整类型包含仅服务 optional 实例化——文件头注）。
    return std::nullopt;
}

}  // namespace sdurws::ird::dynamics
