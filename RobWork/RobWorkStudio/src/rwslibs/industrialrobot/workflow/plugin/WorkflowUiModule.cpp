/**
 * @file   WorkflowUiModule.cpp
 * @brief  WorkflowUiModule 三方法的实现翻译单元（WP-22-T02 最小可注册面）。
 *
 * 设计依据：
 *   - units/ui.md §11.2（三方法契约原文——各方法体的"诚实空态"语义逐条
 *     对应卡文允许的返回值）、§10.9（壳引用存活期契约）
 *   - units/workflow.md §2.3/§2.4（O1 门控规则所有权 vs N1 领域就绪计算
 *     非所有权——readonlyProjections 恒空的结构性依据）
 *   - 任务契约 tasks/foundation/WP-22-T02.json（acceptance 1——_plugin
 *     目标最小可注册实现，零业务计算逻辑）
 *
 * 线程模型：各方法按接口契约的调用线程执行（见头文件）；本实现零共享态
 * （仅指针赋值/常量返回），无需加锁。
 */

#include "WorkflowUiModule.hpp"

namespace sdurws::ird::workflow {

void WorkflowUiModule::onShellReady(ui::IWorkbenchShell& shell)
{
    // 最小语义：仅登记壳引用（非 owning——§10.9 存活期由壳侧保证）。
    // 七阶段导航订阅、恢复横幅接线等初始化随 WP-22-T03/T09 批次在本
    // 回调内扩展（T02 不预建空订阅——NFR-MNT-04 无接口预建）。
    m_shell = &shell;
}

std::vector<ui::DomainReadinessItem> WorkflowUiModule::readonlyProjections() const
{
    // 编排单元恒无域就绪行（§6.5 投影由各评估域自报——workflow 是汇聚
    // 结果的门控消费方，不是数据源；§11.2"可空向量＝域无投影内容"）。
    return {};
}

std::optional<project::CommandEnvelope> WorkflowUiModule::buildDraftCommand(
    const std::string& /*moduleId*/)
{
    // 编排单元恒无可应用草稿（头文件方法注两种情形的语义区分：域外请求
    // 与"编排单元不持工作集"均无信封可产——§8.5"不产生空修订"）。
    // 参数 moduleId 当前不参与分支（两种情形返回值相同，声明处以注释名
    // 弃用）；后续批次若 workflow 出现自有会话命令编排再启用。
    return std::nullopt;
}

}  // namespace sdurws::ird::workflow
