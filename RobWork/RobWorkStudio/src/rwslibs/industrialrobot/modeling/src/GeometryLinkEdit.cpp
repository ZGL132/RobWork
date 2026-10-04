/**
 * @file   GeometryLinkEdit.cpp
 * @brief  几何资源引用编辑原语实现（契约头 GeometryLinkEdit.hpp——§6.7
 *         Recorded 登记面＋引用挂换摘；本文件零 I/O 零 UI——io 交互经
 *         注入缝，呈现归 plugin 层）。
 */

#include <sdurws/ird/modeling/GeometryLinkEdit.hpp>

#include <cmath>
#include <stdexcept>
#include <utility>

namespace sdurws {
namespace ird {
namespace modeling {

namespace {

/// 资源登记批次键空间（临时句柄/清单键派生——与模板/结构编辑路径互斥）。
constexpr char kGeometryDraftKey[] = "ird/modeling/geometry-draft/";

/// 变更记录追加（三原语共用——MDL-09 摘要载体）。
void recordChange(ModelingWorkingSet& ws, std::string subject, std::string summary)
{
    ModelingChangeRecord record;
    record.subject = std::move(subject);
    record.summary = std::move(summary);
    ws.changes.push_back(std::move(record));
}

/// 连杆下标校验（越界＝IndexOutOfRange——拒绝路径零触碰前置面）。
std::optional<GeometryLinkError>
    checkLinkIndex(const ModelingWorkingSet& ws, std::size_t linkIndex)
{
    if (linkIndex >= ws.design.links.size()) {
        return GeometryLinkError{
            GeometryLinkErrorCode::IndexOutOfRange,
            "连杆下标 " + std::to_string(linkIndex) + " 越界（链上连杆 "
                + std::to_string(ws.design.links.size()) + " 根）"};
    }
    return std::nullopt;
}

/// 槽位引用存取（读写共用路由——两槽词表单一出口）。
const std::optional<GeometryRef>&
    slotRef(const LinkEntry& link, GeometrySlot slot)
{
    return slot == GeometrySlot::Visual ? link.visual : link.collision;
}
std::optional<GeometryRef>& slotRef(LinkEntry& link, GeometrySlot slot)
{
    return slot == GeometrySlot::Visual ? link.visual : link.collision;
}

/// 槽位稳定名（摘要/诊断定位用）。
std::string_view slotName(GeometrySlot slot)
{
    return slot == GeometrySlot::Visual ? "visual" : "collision";
}

}  // namespace

// =====================================================================
// 错误码 token
// =====================================================================

std::string_view geometryLinkErrorCodeToken(GeometryLinkErrorCode code) noexcept
{
    switch (code) {
    case GeometryLinkErrorCode::IoRejected: return "io-rejected";
    case GeometryLinkErrorCode::UnsupportedKind: return "unsupported-kind";
    case GeometryLinkErrorCode::DuplicateResource: return "duplicate-resource";
    case GeometryLinkErrorCode::NoSuchResource: return "no-such-resource";
    case GeometryLinkErrorCode::IndexOutOfRange: return "index-out-of-range";
    }
    return "unknown";
}

// =====================================================================
// ① 资源选择器（登记＋挂接）
// =====================================================================

std::optional<GeometryLinkError>
    attachExternalGeometry(ModelingWorkingSet& ws, std::size_t linkIndex,
                           GeometrySlot slot, const std::string& absPath,
                           const ResourceProbeFn& probe)
{
    // 装配校验（探测缝必填——零 I/O 纪律的结构执行面）。
    if (probe == nullptr) {
        throw std::invalid_argument(
            "modeling/geometry-link: probe 缝必填非空（装配契约违约——"
            "P-MDL-8 零直读纪律）");
    }
    // 拒绝面前置：连杆下标（工作集零触碰面）。
    if (const std::optional<GeometryLinkError> bad = checkLinkIndex(ws, linkIndex);
        bad.has_value()) {
        return bad;
    }

    // io 探测（SafePath＋预算＋识别＋摘要——治理全在端口内；失败＝诚实
    // 拒绝，detail 直投 io 侧定位文本——ERR-01）。
    std::string errText;
    const std::optional<ResourceProbeResult> probed = probe(absPath, errText);
    if (!probed.has_value()) {
        return GeometryLinkError{GeometryLinkErrorCode::IoRejected, errText};
    }
    // 格式族校验（io 识别成功≠几何可用——stl/obj/dae 之外的登记族如
    // URDF/纹理＝UnsupportedKind 诚实拒绝，§2.5 行 6 边界）。
    if (!probed->isMeshFamily) {
        return GeometryLinkError{
            GeometryLinkErrorCode::UnsupportedKind,
            "文件格式不在几何承载族（stl/obj/dae）——路径 " + absPath
                + "；请选择网格文件"};
    }

    // 清单键确定性生成（"res-<序>"顺序扫描第一个未占用值——同工作集态
    // 同键；DuplicateResource 防御面在扫描语义下结构性不可达，保留形态
    // 对称）。
    std::string resourceId;
    for (std::size_t n = 1;; ++n) {
        const std::string candidate = "res-" + std::to_string(n);
        bool taken = false;
        for (const ResourceRef& r : ws.design.resourceManifest) {
            if (r.resourceId == candidate) { taken = true; break; }
        }
        if (!taken) { resourceId = candidate; break; }
    }

    // 登记 Recorded（§4.3 resourceManifest 行——digest＝身份要素＋
    // externalRecord 必填；对象字节零资源本体——CON-03）。
    ResourceRef entry;
    entry.resourceId = resourceId;
    entry.contentDigest = probed->contentDigest;
    entry.state = ResourceState::Recorded;
    ExternalResourceRecord external;
    external.absPath = probed->absPath;
    external.recordedDigest = probed->contentDigest;
    entry.externalRecord = std::move(external);
    ws.design.resourceManifest.push_back(std::move(entry));

    // 挂接（替换语义：槽已有引用时被覆盖——旧清单条目不级联删除，其他
    // 消费者可能仍引用；失引用悬空由 §8.2 L6 检测呈现）。
    LinkEntry& link = ws.design.links[linkIndex];
    const std::string replacedId =
        slotRef(link, slot).has_value() ? slotRef(link, slot)->resourceRefId : "";
    GeometryRef ref;
    ref.resourceRefId = resourceId;
    ref.kind = GeometryKind::Mesh;  // 外部网格文件（原语类别——Primitive 归图元面）
    slotRef(link, slot) = std::move(ref);

    recordChange(ws,
                 "links[" + std::to_string(linkIndex) + "]." + std::string(slotName(slot)),
                 "挂接几何资源 " + resourceId + "（" + absPath + "，Recorded）→"
                     + std::string(slotName(slot)) + "；"
                     + (replacedId.empty() ? std::string("原槽为空")
                                           : "替换原引用 " + replacedId)
                     + "；结构变更后运动学及下游结果需重算（MDL-09）");
    return std::nullopt;
}

// =====================================================================
// ② 摘除（幂等）
// =====================================================================

std::optional<GeometryLinkError>
    detachGeometry(ModelingWorkingSet& ws, std::size_t linkIndex, GeometrySlot slot)
{
    // 拒绝面前置（越界——零触碰）。
    if (const std::optional<GeometryLinkError> bad = checkLinkIndex(ws, linkIndex);
        bad.has_value()) {
        return bad;
    }
    LinkEntry& link = ws.design.links[linkIndex];
    std::optional<GeometryRef>& ref = slotRef(link, slot);
    if (!ref.has_value()) {
        return std::nullopt;  // 本无引用＝恒等动作（幂等——零摘要零错误）
    }
    const std::string removedId = ref->resourceRefId;
    ref.reset();
    // 清单条目保留（可能被其他槽/对象引用——不级联删除；失引用悬空由
    // L6 检测呈现——"让悬空可见"）。
    recordChange(ws,
                 "links[" + std::to_string(linkIndex) + "]." + std::string(slotName(slot)),
                 "摘除几何引用 " + removedId + "（清单条目保留——共享引用不级联删除；"
                     "失引用条目由就绪校验呈现）；运动学及下游结果需重算（MDL-09）");
    return std::nullopt;
}

// =====================================================================
// ③ 局部变换编辑
// =====================================================================

std::optional<GeometryLinkError>
    editGeometryLocalTransform(ModelingWorkingSet& ws, std::size_t linkIndex,
                               GeometrySlot slot, double x, double y, double z,
                               double roll, double pitch, double yaw)
{
    // 拒绝面：越界→未挂引用→非有限值（全部先于工作集变更——I-MDL-3
    // "非法值不静默置 0"同款纪律）。
    if (const std::optional<GeometryLinkError> bad = checkLinkIndex(ws, linkIndex);
        bad.has_value()) {
        return bad;
    }
    LinkEntry& link = ws.design.links[linkIndex];
    std::optional<GeometryRef>& ref = slotRef(link, slot);
    if (!ref.has_value()) {
        return GeometryLinkError{
            GeometryLinkErrorCode::NoSuchResource,
            std::string(slotName(slot)) + " 槽未挂接几何引用——请先挂接或选择"
                                         "已挂接的槽位"};
    }
    const double components[6] = {x, y, z, roll, pitch, yaw};
    for (int i = 0; i < 6; ++i) {
        if (!std::isfinite(components[i])) {
            // 词表外值错误：局部错误码不私扩（表尾追加纪律）——以 IoRejected
            // 承载值非法面（detail 定位分量；io 词表外但诚实拒绝语义同族）。
            return GeometryLinkError{
                GeometryLinkErrorCode::IoRejected,
                "输入含非有限值（NaN/Inf）——分量序号 " + std::to_string(i)};
        }
    }

    // 变更（局部变换＝引用字段——T_link_geom m/rad；值语义直投。旋转先
    // 逐元素恒等构造〔冒烟 header-only 纪律——禁 Rotation3D::identity()
    // 外联符号，Template.cpp 同款〕再写入 RPY 矩阵）。
    ref->localTransform = rw::math::Transform3D<double>(
        rw::math::Vector3D<double>(x, y, z),
        rw::math::Rotation3D<double>(1.0, 0.0, 0.0,
                                     0.0, 1.0, 0.0,
                                     0.0, 0.0, 1.0));
    // RPY → 旋转矩阵（Rz(yaw)·Ry(pitch)·Rx(roll)——同模型 URDF 侧约定；
    // 逐元素构造零外联——冒烟纪律）。
    const double cr = std::cos(roll), sr = std::sin(roll);
    const double cp = std::cos(pitch), sp = std::sin(pitch);
    const double cy = std::cos(yaw), sy = std::sin(yaw);
    rw::math::Rotation3D<double>& R = ref->localTransform.R();
    R(0, 0) = cy * cp;                 R(0, 1) = cy * sp * sr - sy * cr; R(0, 2) = cy * sp * cr + sy * sr;
    R(1, 0) = sy * cp;                 R(1, 1) = sy * sp * sr + cy * cr; R(1, 2) = sy * sp * cr - cy * sr;
    R(2, 0) = -sp;                     R(2, 1) = cp * sr;                R(2, 2) = cp * cr;

    recordChange(ws,
                 "links[" + std::to_string(linkIndex) + "]." + std::string(slotName(slot))
                     + ".localTransform",
                 "编辑几何局部变换（平移 m／姿态 rad）——引用 " + ref->resourceRefId);
    return std::nullopt;
}

}  // namespace modeling
}  // namespace ird
}  // namespace sdurws