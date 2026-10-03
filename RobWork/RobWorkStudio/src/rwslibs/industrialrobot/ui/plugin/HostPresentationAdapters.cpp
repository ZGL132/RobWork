/**
 * @file   HostPresentationAdapters.cpp
 * @brief  宿主呈现装配适配器族实现（契约头 HostPresentationAdapters.hpp；
 *         本文件零判定——三件适配器全部为既有契约的机械转发/包裹）。
 */

#include "HostPresentationAdapters.hpp"

#include <stdexcept>
#include <utility>

namespace sdurws {
namespace ird {
namespace ui {

// =====================================================================
// HostRuntimeNameMapPort
// =====================================================================

void HostRuntimeNameMapPort::bindPresentation(
    std::shared_ptr<const runtime::HostPresentationView> view)
{
    // 单点更新（三消费面共享同一实例——SelectionService/网关/需求域缝
    // 经插件成员同步升级；"替换即全链激活"的执行点）。
    m_view = std::move(view);
}

void HostRuntimeNameMapPort::clearPresentation()
{
    m_view.reset();  // 回诚实空二态（幂等——与 HostEmptyNameMapPort 同构）
}

std::optional<core::ObjectId> HostRuntimeNameMapPort::resolveObjectIdFromRuntimeName(
    const std::string& runtimeName) const
{
    if (m_view == nullptr) {
        return std::nullopt;  // 无呈现＝无映射（L3 反解失败分支——合法触发面）
    }
    // 转发呈现视图 NameMap（§7 权威实例——与计算侧同源，R-4 零拼装）。
    const auto resolved = m_view->resolveRuntimeName(runtimeName);
    if (!resolved.ok()) {
        return std::nullopt;  // UnknownObject＝映射中无该名（不猜测——ARC-04）
    }
    return resolved.get().objectId;
}

std::optional<std::string> HostRuntimeNameMapPort::resolveRuntimeName(
    const core::ObjectId& id) const
{
    if (m_view == nullptr) {
        return std::nullopt;  // 同上（L2 正向"未应用"分支）
    }
    const auto resolved = m_view->resolveObjectId(id);
    if (!resolved.ok()) {
        return std::nullopt;
    }
    return resolved.get().fullName;
}

// =====================================================================
// HostWorkCellPresentationObject
// =====================================================================

HostWorkCellPresentationObject::HostWorkCellPresentationObject(
    rws::RobWorkStudio* studio, runtime::HostPresentationViewHandle view)
    : m_studio(studio), m_view(std::move(view))
{
}

bool HostWorkCellPresentationObject::apply() const
{
    // 宿主单入口（框架公开 API——TreeView 与三维场景从同一载体刷新；
    // INV-B4 一致性的宿主半区）。WorkCell 即编译链产物本体（对象同一性
    // ——hostPresentationWorkCell 出口，零复制）。
    if (m_studio == nullptr || m_view == nullptr) {
        return false;  // 装配缺位（诚实失败——网关保留旧呈现）
    }
    const rw::models::WorkCell::Ptr workcell = m_view->hostPresentationWorkCell();
    if (workcell.isNull()) {
        return false;  // 快照无 WC（结构性不可达——hasWorkCell 恒 true）防御面
    }
    m_studio->setWorkCell(workcell);
    return true;
}

void HostWorkCellPresentationObject::remove() const
{
    // 空动作（语义见类注——宿主 WorkCell 清空归项目关闭编排；网关侧的
    // 残留收口经 onSceneCleared 已在位）。
}

// =====================================================================
// HostPresentationSource
// =====================================================================

HostPresentationSource::HostPresentationSource(Deps deps)
    : m_deps(std::move(deps))
{
    // 装配校验（fail-fast——禁构造无供数面的 source；studio 可空＝headless
    // 测试面（投影构造不需要宿主——挂接对象对空宿主诚实失败）。
    if (m_deps.snapshot == nullptr) {
        throw std::invalid_argument(
            "ui/hostpresentation: snapshot 缝必填非空（装配契约违约）");
    }
}

std::optional<PresentationViewProjection>
    HostPresentationSource::fetchPresentation(const PresentationEventFacts& facts)
{
    // ---- 完整构造前置：快照可得性（nullopt＝本会话尚无编译产物——桥按
    // construct-failed 处置，不虚构呈现）。
    std::shared_ptr<const runtime::RuntimeSnapshot> snapshot = m_deps.snapshot();
    if (snapshot == nullptr) {
        if (m_deps.devLog) {
            m_deps.devLog("presentation-source: 无已发布快照——本修订不可呈现");
        }
        return std::nullopt;
    }

    // ---- RT-T14 工厂（唯一构造入口——D10"同一构造规则"的类型层执行）。
    // appliedRevision 取事件事实（对账基准同源）；工厂对非法输入异常
    // 穿透（RuntimeError——装配缺陷不吞，桥不捕获）。
    const runtime::HostPresentationViewHandle view =
        runtime::createHostPresentationView(std::move(snapshot), facts.appliedRevision);

    // ---- 折叠投影（字段对应表——RuntimePublishBridge 头；hostPayload 为
    // 网关约定型包裹，宿主挂接适配在对象内）。
    PresentationViewProjection projection;
    projection.modelIdentity = view->modelIdentity();
    projection.appliedRevisionId = view->appliedRevisionId();
    projection.presentationIdentity = view->presentationIdentity();
    projection.objectExists = [view](const core::ObjectId& oid) {
        // 存在性查询＝视图 NameMap 反解成功（呈现视图 NameMap 同源——
        // INV-B4"选中存在性经 ObjectId 校验"的判定输入）。
        return view->resolveObjectId(oid).ok();
    };
    // hostPayload：shared_ptr<const void> 形态保证 deleter 随原始句柄存活
    // ——ui 拷贝投影即持有适配器与呈现视图存活期（桥契约注释同款）。
    auto object = std::make_shared<HostWorkCellPresentationObject>(m_deps.studio, view);
    projection.hostPayload = std::shared_ptr<const void>(object);

    if (m_deps.devLog) {
        m_deps.devLog("presentation-source: 投影构造完成（修订 "
                      + facts.appliedRevision.toCanonical() + "）");
    }
    return projection;
}

}  // namespace ui
}  // namespace ird
}  // namespace sdurws