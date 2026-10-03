/**
 * @file   HostView3DGateway.cpp
 * @brief  宿主三维网关实现——上行拾取/呈现出口/TCP 源的事件转接与对称
 *         清理（UI-T45；头注为契约，本文件零判定——链路语义全部在既有
 *         端口/域侧。下行高亮由既有 HostHighlightOutlet 承载，见头注
 *         范围勘误）。
 */

#include "HostView3DGateway.hpp"

#include <stdexcept>

namespace sdurws {
namespace ird {
namespace ui {

HostView3DGateway::HostView3DGateway(Deps deps)
    : m_deps(std::move(deps))
{
    // 装配校验（fail-fast——禁止构造违反拾取承诺的网关；scene/TCP 两缝
    // 可空＝呈现/TCP 诚实降级的显式形态，不在此列）。
    if (m_deps.pickFrame == nullptr || m_deps.selection == nullptr
        || m_deps.nameMap == nullptr) {
        throw std::invalid_argument(
            "ui/hostview3d: pickFrame/selection/nameMap 必填非空"
            "（装配契约违约——scene/TCP 两缝可空除外）");
    }
}

// =====================================================================
// 上行拾取链（Ctrl+双击语义等价承接）
// =====================================================================

bool HostView3DGateway::handleViewDoubleClick(const QPoint& pos)
{
    // 链路五拍：拾取→帧名→反解→域分发→选中（View3DPick 来源）。任何
    // 一拍失败＝诚实返回 false（调用方不过滤事件——放行框架默认处理）。
    rw::kinematics::Frame* frame = m_deps.pickFrame(pos.x(), pos.y());
    if (frame == nullptr) {
        return false;  // 未命中（空白处双击——常态，非错误）
    }
    const std::string runtimeName = frame->getName();
    const std::optional<core::ObjectId> oid =
        m_deps.nameMap->resolveObjectIdFromRuntimeName(runtimeName);
    if (!oid.has_value()) {
        return false;  // L3 分支②语义——非业务对象：零选中变化、零伪造
    }
    // 域分发先行（未命中本域＝域内诚实 false——两域至少其一命中即有
    // 产出；分发出口缺省＝该域不参与，装配缺席的诚实形态）。
    bool dispatched = false;
    if (m_deps.dispatchToModeling) {
        dispatched = m_deps.dispatchToModeling(*oid) || dispatched;
    }
    if (m_deps.dispatchToRequirements) {
        dispatched = m_deps.dispatchToRequirements(*oid) || dispatched;
    }
    // 选中汇聚（View3DPick 来源——selectBusiness 后 L2 高亮判定经既有
    // HostHighlightOutlet 回环，链路闭环零第二状态源）。
    m_deps.selection->selectBusiness({*oid}, SelectionSource::View3DPick);
    return true;
}

// =====================================================================
// 下行呈现出口（IUiPresentationOutlet——事务第三步宿主半区）
// =====================================================================

PresentationApplyReport HostView3DGateway::applyPresentation(
    const PresentationViewProjection& view)
{
    PresentationApplyReport report;
    // 视图完整性前置（桥的身份对账之后的双保险——不完整视图不进挂接）。
    if (!view.isComplete()) {
        report.ok = false;
        report.failureToken = "ui-t45.incomplete-view";
        report.failureDetail = "呈现视图不完整（身份/存在性查询缺项）";
        return report;  // 旧呈现保持原状（桥保留旧当前呈现语义）
    }
    // hostPayload 取回（本类型本解释——禁止跨适配器解释的契约边界）。
    // void 载荷别名构造＋static 下转：类型契约由载荷提供方保证（装配
    // 纪律——void 起点无 dynamic 可言，误型载荷属装配缺陷非运行态）。
    const auto object = std::shared_ptr<const HostPresentationObject>(
        view.hostPayload,
        static_cast<const HostPresentationObject*>(view.hostPayload.get()));
    if (object == nullptr) {
        report.ok = false;
        report.failureToken = "ui-t45.payload-null";
        report.failureDetail = "hostPayload 为空（无可应用的呈现对象）";
        return report;
    }
    // 原子替换＝新呈现先挂接（失败＝旧呈现保持原状并如实报告——"失败
    // 保持原状"的事务语义），成功后整组摘除旧呈现（切换窗口内新旧短暂
    // 并存——观察面在返回后才见新态，原子性对消费者成立）。
    if (!object->apply()) {
        report.ok = false;
        report.failureToken = "ui-t45.attach-failed";
        report.failureDetail = "呈现对象挂接失败（对象自报）";
        return report;
    }
    releasePresentation();
    m_attachedPresentation = object;
    report.ok = true;
    return report;
}

void HostView3DGateway::releasePresentation()
{
    if (m_attachedPresentation != nullptr) {
        // 整组摘除（场景绑定在对象侧——网关零场景依赖；尽力而为，桥对
        // 失败仅 Dev 留痕的既有语义）。
        m_attachedPresentation->remove();
        m_attachedPresentation.reset();
    }
}

// =====================================================================
// TCP 会话态数据源（需求域捕获流解除降级）
// =====================================================================

std::optional<rw::math::Transform3D<>>
HostView3DGateway::currentTcpPose(const std::string& deviceName)
{
    // 双缝缺省/双取失败＝诚实降级（nullopt——调用方呈现失败原因）。
    if (!m_deps.resolveTcpFrame || !m_deps.currentState) {
        return std::nullopt;
    }
    const rw::kinematics::Frame* tcpFrame = m_deps.resolveTcpFrame(deviceName);
    const rw::kinematics::State* state = m_deps.currentState();
    if (tcpFrame == nullptr || state == nullptr) {
        return std::nullopt;
    }
    // TCP 世界系位姿（worldTframe——宿主会话态只读，KIN-06：零修订、
    // 不写设计模型；设备 base 即模型根布置时与 base→TCP 一致）。
    const rw::core::Ptr<const rw::kinematics::Frame> frameView(tcpFrame);
    return rw::kinematics::Kinematics::worldTframe(frameView, *state);
}

// =====================================================================
// 会话/场景拍（宿主生命周期同步）
// =====================================================================

void HostView3DGateway::onSceneCleared()
{
    // 场景清除拍残留清理（呈现——幂等；高亮归既有 outlet 的选中流收口）。
    releasePresentation();
}

}  // namespace ui
}  // namespace ird
}  // namespace sdurws
