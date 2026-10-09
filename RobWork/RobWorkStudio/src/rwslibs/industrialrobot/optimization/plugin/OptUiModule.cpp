/**
 * @file   OptUiModule.cpp
 * @brief  optimization 插件界面模块的实现翻译单元——§11.2 三方法的落点
 *         （ASM-PLUG 收口批：P-OPT-10 消账形态的模块半区实现）。
 *
 * 设计依据：
 *   - units/ui.md §11.2（三方法冻结签名）、§6.5（DomainReadinessItem
 *     汇聚值——verdict 词表 core 直用、inputComplete/missingItemKeys/
 *     hasActiveTask 事实透传）、§10.9（线程行——onShellReady 装配线程、
 *     投影/草稿 UI 线程）；
 *   - units/optimization.md §16.3 P-OPT-10（宿主注册端口消费义务——
 *     ASM-PLUG 收口批兑现）；§10.2（候选应用两步组合经宿主编排——草稿
 *     恒空语义锚）；
 *   - plugin/OptPanelModel.hpp（L-O1 纯透传流——本模块投影的唯一数据
 *     面，零第二真值）；
 *   - 先例：dynamics/plugin/DynUiModule.cpp 与 selection/plugin/
 *     SelUiModule.cpp（ASM-PLUG 同构三件套）。
 *
 * 包含面说明（诚实登记）：buildDraftCommand 返回值 std::optional 携带
 *   ui.md §11.2 冻结接口的 project 信封类型——`return std::nullopt` 的
 *   构造/析构路径要求该元素完整类型（optional 成员实例化），故本 TU 自
 *   含 project 公共头（R-2 公共面合规——四先例 modeling/requirements/
 *   kinematics/workflow 的 UiModule 实现同款包含面；optimization→project
 *   为卡 §3.2 九条登记边之一，公共头直用）。该包含仅服务于接口签名实
 *   例化：**零 project 业务访问**（不调用任何 project 实现符号——插件
 *   纪律由契约测试词表扫描钉住，本 TU 在其具名豁免清单——豁免面＝该
 *   头包含行，非红线松动）。
 */

#include "OptUiModule.hpp"

// 同单元私有头（R-2 不跨单元——本 TU 编入 optimization 插件目标自身）。
#include "OptPanelModel.hpp"   // OptReadinessRow/readinessProjection（L-O1
                               //   纯透传流——投影唯一数据面）
#include "OptPanelModule.hpp"  // OptPanelModule（完整类型——session 访问）

#include <sdurws/ird/project/CommandService.hpp>  // project::CommandEnvelope
                                                  //   完整类型（§11.2 返回值
                                                  //   optional 构造/析构实例化
                                                  //   要求——见文件头包含面说
                                                  //   明；零业务访问）

namespace sdurws::ird::optimization {

OptUiModule::OptUiModule(OptPanelModule* module)
    : m_module(module)
    // 面板模块指针由装配激活路径保证非空（门面构造即持模块——见类注）。
{
}

OptUiModule::~OptUiModule() = default;

void OptUiModule::onShellReady(ui::IWorkbenchShell& shell)
{
    // §11.2"注册回调后初始化"——只持壳引用（只读消费面）。域命令的注册
    // 经装配描述符在装配期一次完成（§10.9"装配期一次"——本域 R1 命令词
    // 表未随卡面登记，命令面缺席＝既登记的诚实缺席），本回调零重复注
    // 册；面板内容初始化由面板工厂与 refreshFromSession 通道承载（T10
    // 落位形态）——不在此重复（零第二初始化路径，NFR-MNT-03）。
    m_shell = &shell;
}

std::vector<ui::DomainReadinessItem> OptUiModule::readonlyProjections() const
{
    // 第一步：模型层 L-O1 纯透传流现取行（会话事实唯一归属
    // OptPanelModule::session——零判定零缓存，防第二真值）。
    const OptReadinessRow row = readinessProjection(m_module->session);

    // 第二步：冻结形状五字段一一对应翻译（ui.md §6.5 DomainReadinessItem
    // 冻结形状——domainKey/verdict/inputComplete/missingItemKeys/
    // hasActiveTask 逐一拷贝，零增删；"字段同构无编译期校验"的缺口随
    // 真实编译边建立而解除——字段名/类型漂移即编译错误，ASM-PLUG 收口
    // 批）。本域行扩展呈现位（运行编排状态/取消已请求/正式导出可用）
    // 不经 §6.5 汇聚——ui 冻结形状无承载，不私扩 ui 类型（SA-12），面
    // 板半区继续承载（T10 形态不变）。
    ui::DomainReadinessItem item;
    item.domainKey = row.domainKey;          // 域注册键（恒 "optimization"）
    item.verdict = row.verdict;              // 最近正式判定（core 词表直用）
    item.inputComplete = row.inputComplete;  // 就绪校验结论（UX-10 素材）
    item.missingItemKeys = row.missingItemKeys;  // 缺项文案键清单
    item.hasActiveTask = row.hasActiveTask;  // 在途任务事实（"计算中"素材）
    return {std::move(item)};
}

std::optional<project::CommandEnvelope> OptUiModule::buildDraftCommand(
    const std::string& /*moduleId*/)
{
    // optimization 无草稿（候选应用是两步组合经宿主编排——Applier 组装
    // 命令对→①命令端口→project 命令服务，卡 §10.2；不经 §8.5 草稿流）：
    // §8.5"无可应用变更返回 nullopt（不产生空修订）"的语义落点——恒
    // nullopt 是语义实现而非占位。零 project 访问：本函数不构造任何信封
    // （完整类型包含仅服务 optional 实例化——文件头注）。
    return std::nullopt;
}

}  // namespace sdurws::ird::optimization
