/**
 * @file   SelUiModule.cpp
 * @brief  selection 插件界面模块的实现翻译单元——§11.2 三方法的落点
 *         （ASM-PLUG 收口批：P-SEL-10 消账形态的模块半区实现）。
 *
 * 设计依据：
 *   - units/ui.md §11.2（三方法冻结签名）、§6.5（DomainReadinessItem
 *     汇聚值——verdict 词表 core 直用、inputComplete/missingItemKeys/
 *     hasActiveTask 事实透传）、§10.9（线程行——onShellReady 装配线程、
 *     投影/草稿 UI 线程）；
 *   - units/selection.md §19.2/§19.5 P-SEL-10（宿主注册端口消费义务——
 *     ASM-PLUG 收口批兑现）；§12.1/§12.3（回填写语义唯一经①命令端口
 *     ——草稿恒空语义锚）；
 *   - plugin/SelPanelModel.hpp（L-S1 纯透传流——本模块投影的唯一数据
 *     面，零第二真值）；
 *   - 先例：dynamics/plugin/DynUiModule.cpp（ASM-PLUG 同构三件套）。
 *
 * 包含面说明（诚实登记）：buildDraftCommand 返回值 std::optional 携带
 *   ui.md §11.2 冻结接口的 project 信封类型——`return std::nullopt` 的
 *   构造/析构路径要求该元素完整类型（optional 成员实例化），故本 TU 自
 *   含 project 公共头（R-2 公共面合规——四先例 modeling/requirements/
 *   kinematics/workflow 的 UiModule 实现同款包含面；selection 计算库
 *   v0.9 已登记"⇢ project 公共头〔include 面，非链接边〕"同款口径——
 *   卡 §3.2 增行）。该包含仅服务于接口签名实例化：**零 project 业务访
 *   问**（不调用任何 project 实现符号——插件纪律由契约测试词表扫描钉
 *   住，本 TU 在其具名豁免清单——豁免面＝该头包含行，非红线松动）。
 */

#include "SelUiModule.hpp"

// 同单元私有头（R-2 不跨单元——本 TU 编入 selection 插件目标自身）。
#include "SelPanelModel.hpp"   // SelReadinessRow/readinessProjection（L-S1
                               //   纯透传流——投影唯一数据面）
#include "SelPanelModule.hpp"  // SelPanelModule（完整类型——session 访问）

#include <sdurws/ird/project/CommandService.hpp>  // project::CommandEnvelope
                                                  //   完整类型（§11.2 返回值
                                                  //   optional 构造/析构实例化
                                                  //   要求——见文件头包含面说
                                                  //   明；零业务访问）

namespace sdurws::ird::selection {

SelUiModule::SelUiModule(SelPanelModule* module)
    : m_module(module)
    // 面板模块指针由装配激活路径保证非空（门面构造即持模块——见类注）。
{
}

SelUiModule::~SelUiModule() = default;

void SelUiModule::onShellReady(ui::IWorkbenchShell& shell)
{
    // §11.2"注册回调后初始化"——只持壳引用（只读消费面）。域命令的注册
    // 经装配描述符在装配期一次完成（§10.9"装配期一次"），本回调零重复
    // 注册；面板内容初始化由面板工厂与 refreshFromSession 通道承载
    // （T10 落位形态）——不在此重复（零第二初始化路径，NFR-MNT-03）。
    m_shell = &shell;
}

std::vector<ui::DomainReadinessItem> SelUiModule::readonlyProjections() const
{
    // 第一步：模型层 L-S1 纯透传流现取行（会话事实唯一归属
    // SelPanelModule::session——零判定零缓存，防第二真值）。
    const SelReadinessRow row = readinessProjection(m_module->session);

    // 第二步：字段一一对应翻译（ui.md §6.5 DomainReadinessItem 冻结形
    // 状——domainKey/verdict/inputComplete/missingItemKeys/hasActiveTask
    // 五字段逐一拷贝，零增删；"字段同构无编译期校验"的缺口随真实编译
    // 边建立而解除——字段名/类型漂移即编译错误，ASM-PLUG 收口批）。
    ui::DomainReadinessItem item;
    item.domainKey = row.domainKey;          // 域注册键（恒 "selection"）
    item.verdict = row.verdict;              // 最近正式判定（core 词表直用）
    item.inputComplete = row.inputComplete;  // 就绪校验结论（REQ-06）
    item.missingItemKeys = row.missingItemKeys;  // 缺项文案键清单（UX-10）
    item.hasActiveTask = row.hasActiveTask;  // 在途任务事实（"计算中"素材）
    return {std::move(item)};
}

std::optional<project::CommandEnvelope> SelUiModule::buildDraftCommand(
    const std::string& /*moduleId*/)
{
    // selection 无草稿（写语义唯一经①命令端口——回填意图 token→宿主
    // 命令管线→project 命令服务产生恰一新修订，卡 §12.1/§12.3；不经
    // §8.5 draft.apply 草稿流）：§8.5"无可应用变更返回 nullopt（不产生
    // 空修订）"的语义落点——恒 nullopt 是语义实现而非占位。零 project
    // 访问：本函数不构造任何信封（完整类型包含仅服务 optional 实例化
    // ——文件头注）。
    return std::nullopt;
}

}  // namespace sdurws::ird::selection
