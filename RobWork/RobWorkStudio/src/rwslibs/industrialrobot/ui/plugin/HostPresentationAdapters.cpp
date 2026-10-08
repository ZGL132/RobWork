/**
 * @file   HostPresentationAdapters.cpp
 * @brief  宿主呈现装配适配器族实现（契约头 HostPresentationAdapters.hpp）。
 *
 * 本文件两块职责：
 *   1. 三件适配器＝既有契约的机械转发/包裹（UI-T46——零判定）；
 *   2. 发布模型骨架三维呈现（UI-T77——F-550 修复增量）：挂接对象
 *      apply() 在宿主单入口 setWorkCell 之后，把编译产物 WC 的帧树/
 *      SerialDevice 以「坐标轴阵＋连杆链线＋关键帧标签」呈现进宿主三
 *      维——发布后模型在三维可见（此前三维仅网格地面＝F-550 实录）。
 *      边界：本呈现是**骨架示意**（帧结构/运动链可视化），真实网格
 *      几何渲染（visual/collision mesh 资源链）仍归 WP-10-T05 阶段 B
 *      （模板模型 resourceManifest 为空——mesh 数据链与骨架呈现互不
 *      依赖，边界登记 findings F-550）。
 */

#include "HostPresentationAdapters.hpp"

#include "QtTextBillboard.hpp"  // Qt 纹理化文本 billboard（F-556——骨架标签承载，共享件）

#include <sdurws/ird/ui/UiTheme.hpp>  // 标签/坐标轴色词表（UI-T77 单一供色点）

#include <rw/graphics/DrawableNode.hpp>
#include <rw/graphics/Render.hpp>
#include <rw/graphics/WorkCellScene.hpp>
#include <rw/kinematics/Frame.hpp>
#include <rw/kinematics/Kinematics.hpp>
#include <rw/models/Device.hpp>
#include <rw/models/WorkCell.hpp>
#include <rwlibs/opengl/rwgl.hpp>  // GL 聚合头（HostView3DPreviewBackend 同款先例）

#include <algorithm>
#include <stdexcept>
#include <utility>
#include <vector>

// 渲染件命名空间引入（骨架呈现三件——DeviceChainRender/WorkCellScene/
// Kinematics 消费面；与 HostView3DPreviewBackend 同款 using 纪律）。
using namespace rw::graphics;
using namespace rw::kinematics;
using namespace rw::math;

namespace sdurws {
namespace ird {
namespace ui {

// =====================================================================
// 发布模型骨架三维呈现（UI-T77——F-550 修复增量）
// =====================================================================

namespace {

/// 骨架组节点名前缀（与预览组 ird-req-preview-、会话预览锚
/// ird-session-preview 同族的 ird 前缀约定——诊断可辨识；发布 WC 的
/// 帧名全局唯一，前缀＋帧名不冲突）。
constexpr char kSkeletonPrefix[] = "ird-model-skeleton-";

/**
 * @brief Device 连杆链渲染（UI-T77——F-550：基座→各关节→末端的世界系
 *        连线，随 State **实时跟随**——Joint 转动时链形即时更新）。
 *
 * 为什么实时取位姿而不是预计算：addLines 挂帧只能承载静态线段（挂接
 * 帧局部系固定），关节运动会让预计算连线失真；Render::draw 在场景每
 * 帧渲染时执行，持帧指针按 info._state 现算 worldT——RobWork 场景对
 * 挂帧 drawable 的 State 同步机制（WorkCellScene::updateSceneGraph）
 * 之外，Render 内部自查 State 是运动跟随的标准通道。
 *
 * 帧存活：链内 Frame* 由发布 WC 帧树持有；drawable 挂接在 WC 的场景
 * 节点上，setWorkCell 重建场景节点树时旧 WC 节点整体摘除、本 Render
 * 随之释放（所有权与被挂 WC 严格同寿——无悬空窗口）。
 */
class DeviceChainRender final : public Render
{
  public:
    /// 构造（链＝基座→…→末端的有序帧序列——非 owning，见类注存活）。
    explicit DeviceChainRender(std::vector<rw::kinematics::Frame*> chain)
        : m_chain(std::move(chain))
    {
    }

    void draw(const DrawableNode::RenderInfo& info,
              DrawableNode::DrawType type, double alpha) const override
    {
        (void)type;
        if (m_chain.size() < 2 || info._state == nullptr) {
            return;  // 单帧链无线可画／无状态面（装配残态防御；_state 为
                     // 裸指针形态——空判用指针比较而非 Ptr::isNull）
        }
        const rw::kinematics::State& state = *info._state;
        glLineWidth(2.0f);
        // 连杆链线＝工程蓝（UiTheme 词表——主色，模型骨架的辨识色；
        // 与区域框缺省蓝同值系——空间结构色语义族）。
        glColor4f(ui::palette::kMarkerLabelGl[0], ui::palette::kMarkerLabelGl[1],
                  ui::palette::kMarkerLabelGl[2], static_cast<float>(alpha));
        glBegin(GL_LINES);
        for (std::size_t i = 0; i + 1 < m_chain.size(); ++i) {
            // 逐段世界系连线（相邻帧原点——关节运动下链形实时跟随）。
            const rw::math::Transform3D<double> ti =
                rw::kinematics::Kinematics::worldTframe(m_chain[i], state);
            const rw::math::Transform3D<double> tj = rw::kinematics::Kinematics::
                worldTframe(m_chain[i + 1], state);
            glVertex3d(ti.P()[0], ti.P()[1], ti.P()[2]);
            glVertex3d(tj.P()[0], tj.P()[1], tj.P()[2]);
        }
        glEnd();
        glLineWidth(1.0f);
    }

  private:
    std::vector<rw::kinematics::Frame*> m_chain;  ///< 连杆链（非 owning——WC 帧树持存活）
};

/**
 * @brief 把发布 WC 的模型骨架挂进宿主场景（apply 的呈现半区——
 *        setWorkCell 之后调用一次）。
 *
 * 呈现三层（自上而下控制标签密度，防标签噪声）：
 *   ①坐标轴阵：全部非 WORLD 根帧各挂小轴（0.12 m——帧结构可辨、可拾
 *     取载体；Virtual 渲染分组——不进物理呈现）；
 *   ②连杆链线：每台 SerialDevice 的基座→关节→末端链（实时跟随——
 *     DeviceChainRender），挂设备基座帧；
 *   ③关键帧标签：设备基座与末端（Qt 纹理化文本——F-556 承载，中文
 *     设备名可读）；场景对象帧只挂轴不挂标签（标签密度让位给工位
 *     标记层——ird-req-preview 组自有标签）。
 *
 * 生命周期：drawable 全部挂接在发布 WC 的场景节点上（帧的 GroupNode
 * ——addFrameAxis/addRender 的挂接语义），setWorkCell 换 WC 时随旧节
 * 点树整体释放（HostWorkCellPresentationObject::apply 是发布链唯一
 * 挂接点，替换即收口——零残留、零额外清理名单）。
 *
 * @param scene     [in] 宿主场景（apply 已确保 setWorkCell 完成）
 * @param workcell  [in] 编译产物 WC（只读借用——出口契约零结构写）
 */
void applyPublishedModelSkeleton(rw::graphics::WorkCellScene& scene,
                                 const rw::models::WorkCell& workcell)
{
    // ---- ②连杆链：先收集 device 链内帧与关键帧（基座/末端）集合
    // （①③的标签密度控制输入——链中段已有链线表达，不叠标签）。
    std::vector<rw::kinematics::Frame*> deviceChainFrames;
    std::vector<rw::kinematics::Frame*> deviceAnchorFrames;
    for (const rw::core::Ptr<rw::models::Device>& device :
         workcell.getDevices()) {
        if (device.isNull()) {
            continue;  // 空设备条目（WC 装配防御——跳过不崩）
        }
        // 链＝末端沿父链回溯至基座（单父树拓扑——JointDevice 的
        // base..end 严格一条链；反转恢复基座→末端序）。
        std::vector<rw::kinematics::Frame*> chain;
        for (rw::kinematics::Frame* f = device->getEnd(); f != nullptr;
             f = f->getParent()) {
            chain.push_back(f);
            if (f == device->getBase()) {
                break;  // 到基座为止（base 之外的父链——WORLD 等——不入链）
            }
        }
        if (chain.empty() || chain.back() != device->getBase()) {
            continue;  // 链断裂（末端不在基座子树——装配残态，跳过不虚构）
        }
        // 链内帧与锚点帧登记（std::find 的线性量＝链长——设备数个帧，
        // 微不足道）。
        for (rw::kinematics::Frame* f : chain) {
            deviceChainFrames.push_back(f);
        }
        deviceAnchorFrames.push_back(chain.front());  // 末端
        deviceAnchorFrames.push_back(chain.back());   // 基座

        // 链线 Render 挂基座帧（实时跟随——见 DeviceChainRender 类注）。
        const std::string chainName =
            std::string(kSkeletonPrefix) + "chain-" + device->getName();
        scene.addRender(
            chainName,
            rw::core::ownedPtr(
                new DeviceChainRender(std::vector<rw::kinematics::Frame*>(
                    chain.rbegin(), chain.rend()))),
            device->getBase());
    }

    // ---- ①③坐标轴阵＋关键帧标签：全帧遍历（WORLD 根帧＝参考系非
    // 模型元素，不呈现）。
    for (rw::kinematics::Frame* frame : workcell.getFrames()) {
        if (frame == nullptr || frame == workcell.getWorldFrame()) {
            continue;
        }
        const std::string axisName =
            std::string(kSkeletonPrefix) + "axis-" + frame->getName();
        scene.addFrameAxis(axisName, 0.12, frame);  // 小轴（工位标记 0.25 让位——层次区分）
        // 标签＝设备锚点帧（基座/末端——设备名中文可读，F-556 承载）；
        // 链中段（关节/连杆帧）与场景对象帧不挂（链线/轴已表达——防
        // 标签噪声，标签密度让位给工位标记层）。
        const bool isDeviceAnchor =
            std::find(deviceAnchorFrames.begin(), deviceAnchorFrames.end(),
                      frame) != deviceAnchorFrames.end();
        if (!isDeviceAnchor) {
            continue;  // 链中段/场景对象帧＝只挂轴
        }
        const std::string labelName =
            std::string(kSkeletonPrefix) + "label-" + frame->getName();
        scene.addRender(labelName,
                        rw::core::ownedPtr(new QtTextBillboardRender(
                            frame->getName(),
                            rw::core::Ptr<rw::kinematics::Frame>(frame))),
                        frame);
    }
}

}  // namespace

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
    // ---- 发布模型骨架三维呈现（UI-T77——F-550）：宿主单入口装入后把
    // 帧树/设备链呈现进三维（此前三维仅网格＝发布后模型不可见）。挂在
    // 发布 WC 的场景节点上——setWorkCell 换 WC 时随旧节点树整体收口
    // （见 applyPublishedModelSkeleton 类注生命周期），本对象 remove()
    // 的空动作语义不受影响。
    rw::graphics::WorkCellScene::Ptr scene = m_studio->getWorkCellScene();
    if (!scene.isNull()) {
        applyPublishedModelSkeleton(*scene, *workcell);
    }  // 场景缺位（headless 装配降级）＝WC 已装入、骨架缺呈现——诚实降级
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