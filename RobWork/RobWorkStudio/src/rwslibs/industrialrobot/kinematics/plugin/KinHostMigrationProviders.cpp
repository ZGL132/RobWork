/**
 * @file   KinHostMigrationProviders.cpp
 * @brief  运动学域宿主迁移三接入面实现（KinHostMigrationProviders.hpp 的
 *         全部方法落点——编入 sdurws_ird_kinematics_plugin 目标）。
 *
 * 设计依据：见同目录 KinHostMigrationProviders.hpp 文件头（契约 acceptance
 *   1/4 v1.2——O-44 接入面 v1 诚实边界；ui 注册协议 UI-T21/T22 冻结形状；
 *   requirements 先例同构）。本注释不重复头注，只在各方法处标注行为要点
 *   与例外语义。
 */

#include "KinHostMigrationProviders.hpp"

#include <utility>

namespace sdurws {
namespace ird {
namespace kinematics {

// =====================================================================
// KinematicsTreeNodesProvider——结构接缝＋v1 恒空集
// =====================================================================

KinematicsTreeNodesProvider::KinematicsTreeNodesProvider(KinematicsSharedSurfaceDeps deps)
    : m_deps(std::move(deps))
{
}

std::string KinematicsTreeNodesProvider::domainKey() const
{
    // 三协议统一域键（IUiTreeNodesProvider/PropertyPagesProvider/装配层
    // 查重同一词表——常量字面跨调用稳定，requirements 先例同串纪律）。
    return "kinematics";
}

std::vector<ui::ProjectTreeNode> KinematicsTreeNodesProvider::treeNodes() const
{
    // v1 恒空集（O-44 诚实边界——头注类注完整事实链）：协议"可空＝合法
    // 常态"语义的直接兑现。注意这是**无条件**空集——本类不持有任何数据
    // 源注入面，"域侧恰好没有可入树对象"与"域侧有对象但身份模型缺位"
    // 两种场景在此不可区分也不必区分：v1 下两者都不供给（后者等所有者
    // 裁决对象身份模型，词表扩展走 B1-SPEC 增量修订）。
    return std::vector<ui::ProjectTreeNode>{};
}

// =====================================================================
// KinematicsPropertyPagesProvider——结构接缝＋v1 无应答面
// =====================================================================

KinematicsPropertyPagesProvider::KinematicsPropertyPagesProvider(
    KinematicsSharedSurfaceDeps deps)
    : m_deps(std::move(deps))
{
}

std::string KinematicsPropertyPagesProvider::domainKey() const
{
    return "kinematics";
}

std::optional<ui::CommonFieldsPage>
KinematicsPropertyPagesProvider::commonFieldsPage(const core::ObjectId&) const
{
    // v1 恒 nullopt（协议"非本域对象 → nullopt 合法二态"——检查器顺延
    // 询问下一注册者）。参数显式弃用：应答判定的前提"选中对象属于本域"
    // 在 v1 对任何 ObjectId 都不成立（本域无持有 ObjectId 的对象——O-44）。
    return std::nullopt;
}

std::vector<ui::ComplexPageEntry>
KinematicsPropertyPagesProvider::complexPageEntries(const core::ObjectId&) const
{
    // v1 恒空集（协议纪律：与 commonFieldsPage 的 nullopt 同答）。D6 的
    // 本域收口位＝域面板高级面板（KinematicsConfigPanel——WP-15-T10/T12
    // 既有落位），不经共享检查器展开——入口声明面为空不改变该收口。
    return std::vector<ui::ComplexPageEntry>{};
}

ui::ComplexPageActivationReport
KinematicsPropertyPagesProvider::activateComplexPage(const core::ObjectId&,
                                                     const std::string&,
                                                     QWidget*)
{
    // v1 恒诚实拒绝（头注类注：检查器编排路径在 v1 不可能路由到本类——
    // 激活编排只转调"仍被应答"的域；直接到达＝调用方违约，返回值轨拒绝
    // 不抛——域侧数据缺陷是可修复常态，与对端 requirements 的拒绝词表
    // 同源）。reason 取封闭词表 "activation-object-not-owned"（对象当前
    // 无本域应答——激活面不存在）。
    ui::ComplexPageActivationReport report;
    report.ok = false;
    report.reason = "activation-object-not-owned";
    report.hostedWidget = nullptr;
    return report;
}

// =====================================================================
// KinematicsSelectionAdapter——下行联动全功能＋上行 v1 诚实不上报
// =====================================================================

KinematicsSelectionAdapter::KinematicsSelectionAdapter(KinematicsSharedSurfaceDeps deps)
    : m_deps(std::move(deps))
{
}

void KinematicsSelectionAdapter::attach(ui::SelectionService& service)
{
    // 引用入参无空态（requirements 先例同型——存活期契约见头注类注）。
    detach();  // 幂等收口：重复 attach 先退订既有订阅
    m_service = &service;
    m_subscription = service.subscribe(*this);  // RAII 句柄——析构/detach 即退订
}

void KinematicsSelectionAdapter::detach() noexcept
{
    // 句柄析构即退订（RAII）——显式置空保证 detach 幂等（重复调用零触碰）。
    m_subscription.reset();
    m_service = nullptr;
}

void KinematicsSelectionAdapter::onSelectionChanged(const ui::SelectionChange& change)
{
    // 仅运行时对象选择（L3 反解失败暂态）→零触碰：业务选中集未变，此时
    // 清高亮会误伤"树选任务点→面板高亮"的既有呈现（SelectionChange 类
    // 注两态语义——requirements 先例同判）。
    if (change.runtimeOnly) {
        return;
    }

    // 呈现执行器缺失＝下行联动静默跳过（可空注入的显式声明语义——事件
    // 消费面照常，不视为错误）。
    if (!m_deps.panelHighlight) {
        return;
    }

    // 业务单选→驱动面板定位/高亮；多选/清空→对称清除（D5"当前选中对象"
    // 单数语义在域面板的落点——高亮锚是单点，多选无定位语义）。
    if (change.selectedObjectIds.size() == 1) {
        m_deps.panelHighlight(change.selectedObjectIds.front());
    } else {
        m_deps.panelHighlight(std::nullopt);
    }
}

bool KinematicsSelectionAdapter::reportView3DPick(const core::ObjectId&)
{
    // v1 恒 false（O-44 诚实边界——头注类注）：上报值必须是本域持有
    // ObjectId 的业务对象（selectBusiness 对无效身份 fail-fast，域侧先
    // 过滤），本域 v1 无此类对象——诚实不上报，零伪造零诊断。拾取未命中
    // 本域对象是常态而非错误（requirements 先例同语义注释）。
    return false;
}

}  // namespace kinematics
}  // namespace ird
}  // namespace sdurws
