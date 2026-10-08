/**
 * @file   HostView3DPreviewBackend.cpp
 * @brief  需求域三维会话预览渲染后端实现（契约头 HostView3DPreviewBackend
 *         .hpp——WorkCellScene 原语编排＋两组自绘 Render）。
 *
 * GL 纪律：固定管线即时模式（glBegin/glEnd——框架 RenderFrame 同款先例；
 * 头 rwlibs/opengl/glext_win32.hpp 的框架聚合面——ui_plugin 链接面既有）。
 * 着色词表：Good 绿/Weak 黄/Failed 红（spec §2.4——判定归域侧，此处只
 * 分色）；中性格线＝灰（无评估结果的诚实空态——不虚构判定）。
 */

#include "HostView3DPreviewBackend.hpp"

#include <sdurws/ird/ui/UiTheme.hpp>  // 采样状态色词表（UI-T65——F-495 统一供色单点）

#include <rw/graphics/DrawableNode.hpp>
#include <rw/graphics/Render.hpp>
#include <rw/graphics/SceneViewer.hpp>  // setWorldNode（世界节点重同步——F-550②）
#include <rw/graphics/WorkCellScene.hpp>
#include <rw/kinematics/Frame.hpp>
#include <rwlibs/opengl/DrawableUtil.hpp>
#include <rwlibs/opengl/RenderText.hpp>  // 框架文本渲染（UI-T52——工位标签；F-490② 消账）
#include <rwlibs/opengl/rwgl.hpp>  // GL 聚合头（框架 RenderFrame 同款——平台 glext 由其内部处理）

#include <rws/RobWorkStudio.hpp>
#include <rws/RWStudioView3D.hpp>  // getView→getSceneViewer（F-550② 世界节点重同步）

#include <cstdio>

using namespace rw::graphics;
using namespace rw::kinematics;
using namespace rw::math;

namespace sdurws {
namespace ird {
namespace ui {
namespace {

/// 预览组节点名前缀（clear 的删除面——draw/clear 共用常量，防漂移）。
constexpr char kPreviewPrefix[] = "ird-req-preview-";

/// 逐点着色分色（判定已由域侧完成——本函数只做词表到 GL 颜色的映射；
/// 色值经 UiTheme palette 词表供色——UI-T65 消硬编码，F-495 统一供色）。
void applyCellColor(View3DCellState state)
{
    switch (state) {
    case View3DCellState::Good:
        glColor3fv(ui::palette::kSampleGoodGl);  // 达标＝绿（词表）
        break;
    case View3DCellState::Weak:
        glColor3fv(ui::palette::kSampleWeakGl);  // 临界＝黄（词表）
        break;
    case View3DCellState::Failed:
        glColor3fv(ui::palette::kSampleFailedGl);  // 不达标＝红（词表）
        break;
    case View3DCellState::NotSampled:
        glColor3fv(ui::palette::kSampleNotSampledGl);  // 未采样＝灰（UI-T65）
        break;
    }
}

/// 区域框 tint 分色（UI-T65——覆盖率映射档；None/缺省＝区域框既有辨识
/// 蓝（UI-T33 以来的原值 (0.25, 0.45, 1.0)——UI-T65 返工恢复并经
/// palette::kRegionTintDefaultGl 词表供色，本文件零硬编码色值；返工前
/// 误为 kPrimary 同值且字面量硬编码，acc/ui-t65/1 阻断 B/E2 消账）。
/// 返回色三元组供 glColor4f 消费。
std::array<float, 3> tintColor(std::optional<View3DTint> tint)
{
    if (!tint.has_value() || *tint == View3DTint::None) {
        return {ui::palette::kRegionTintDefaultGl[0],
                ui::palette::kRegionTintDefaultGl[1],
                ui::palette::kRegionTintDefaultGl[2]};  // 缺省蓝（词表单点）
    }
    switch (*tint) {
    case View3DTint::Good:
        return {ui::palette::kSampleGoodGl[0], ui::palette::kSampleGoodGl[1],
                ui::palette::kSampleGoodGl[2]};  // 达标档＝绿
    case View3DTint::Weak:
        return {ui::palette::kSampleWeakGl[0], ui::palette::kSampleWeakGl[1],
                ui::palette::kSampleWeakGl[2]};  // 未达档＝黄
    case View3DTint::Failed:
        return {ui::palette::kSampleFailedGl[0], ui::palette::kSampleFailedGl[1],
                ui::palette::kSampleFailedGl[2]};  // 零达标档＝红
    }
    // 不可达分支（switch 全覆盖——防御面）：同缺省蓝词表值。
    return {ui::palette::kRegionTintDefaultGl[0],
            ui::palette::kRegionTintDefaultGl[1],
            ui::palette::kRegionTintDefaultGl[2]};
}

/**
 * @brief 区域边界框线框渲染（12 棱——八角点拓扑直投；蓝色——旧版
 *        WorkspaceRegionSceneVisualizer 同色系，选中预览的辨识色）。
 */
class RegionOutlineRender final : public Render
{
  public:
    /// 构造持框值（tint 一并冻结——UI-T65 覆盖率映射档随整组替换刷新）。
    explicit RegionOutlineRender(const View3DBoxOutline& box)
        : m_corners(box.corners), m_tint(box.tint)
    {
    }

    void draw(const DrawableNode::RenderInfo& info,
              DrawableNode::DrawType type, double alpha) const override
    {
        (void)info;
        (void)type;
        (void)alpha;
        // 十二棱（八角点拓扑：底面四棱＋顶面四棱＋四立柱——角序与
        // requirements 侧 regionPreviewGeometry 同款，零重排直投）。
        static const int kEdges[12][2] = {
            {0, 1}, {1, 2}, {2, 3}, {3, 0},  // 底面
            {4, 5}, {5, 6}, {6, 7}, {7, 4},  // 顶面
            {0, 4}, {1, 5}, {2, 6}, {3, 7},  // 立柱
        };
        glLineWidth(2.0f);
        // 框色＝tint 分色（UI-T65——覆盖率映射档；缺省蓝＝既有选中辨识
        // 色语义保持）。
        const std::array<float, 3> color = tintColor(m_tint);
        glColor4f(color[0], color[1], color[2], static_cast<float>(alpha));
        glBegin(GL_LINES);
        for (const auto& e : kEdges) {
            const Vector3D<double>& a = m_corners[e[0]];
            const Vector3D<double>& b = m_corners[e[1]];
            glVertex3d(a[0], a[1], a[2]);
            glVertex3d(b[0], b[1], b[2]);
        }
        glEnd();
        glLineWidth(1.0f);
    }

  private:
    std::array<Vector3D<double>, 8> m_corners;  ///< 盒八角点（世界系——构造冻结）
    std::optional<View3DTint> m_tint;  ///< 覆盖率映射档（UI-T65——构造冻结）
};

/**
 * @brief 采样格渲染（格线＋逐点着色——cellStates 空＝中性格线诚实空态）。
 */
class SampleGridRender final : public Render
{
  public:
    SampleGridRender(const View3DSampleGrid& grid)
        : m_samples(grid.samples),
          m_states(grid.cellStates),
          m_gridLines(grid.gridLines)
    {}

    void draw(const DrawableNode::RenderInfo& info,
              DrawableNode::DrawType type, double alpha) const override
    {
        (void)info;
        (void)type;
        // 格线骨架（UI-T52 字面化——采样格的线段呈现：灰细线与着色点并
        // 存〔格线＝格结构、点＝样本锚——两层视觉语义分离〕；空＝无格）。
        if (!m_gridLines.empty()) {
            glLineWidth(1.0f);
            glColor4f(0.55f, 0.55f, 0.60f, static_cast<float>(alpha));  // 格线灰
            glBegin(GL_LINES);
            for (const auto& seg : m_gridLines) {
                glVertex3d(seg.first[0], seg.first[1], seg.first[2]);
                glVertex3d(seg.second[0], seg.second[1], seg.second[2]);
            }
            glEnd();
        }
        // 采样点渲染（点阵着色——评估就位＝逐点分色；空＝中性灰诚实空态）。
        glPointSize(5.0f);
        glBegin(GL_POINTS);
        for (std::size_t i = 0; i < m_samples.size(); ++i) {
            glColor4f(0.6f, 0.6f, 0.6f, static_cast<float>(alpha));  // 中性灰
            if (i < m_states.size()) {
                applyCellColor(m_states[i]);  // 评估就位＝逐点分色
            }
            const Vector3D<double>& p = m_samples[i];
            glVertex3d(p[0], p[1], p[2]);
        }
        glEnd();
        glPointSize(1.0f);
    }

  private:
    std::vector<Vector3D<double>> m_samples;  ///< 采样点（世界系——构造冻结）
    std::vector<View3DCellState> m_states;    ///< 着色态（空＝中性——诚实空态）
    std::vector<std::pair<Vector3D<double>, Vector3D<double>>> m_gridLines;  ///< 格线段（UI-T52——构造冻结）
};

}  // namespace

HostView3DPreviewBackend::HostView3DPreviewBackend(rws::RobWorkStudio* studio)
    : m_studio(studio)
{}

bool HostView3DPreviewBackend::draw(const View3DPreviewUpdate& update)
{
    // 场景现取（studio 公开 API 直供——getView 中介零需要）。
    if (m_studio == nullptr) {
        return false;
    }
    WorkCellScene::Ptr scene = m_studio->getWorkCellScene();
    if (scene.isNull()) {
        return false;
    }
    // 挂接锚 WorkCell 三级现取（F-550② 修复——会话预览是会话编辑面
    // 〔PM-11/协议头注〕，草稿期（首应用前）就必须可见；此前实现把
    // "宿主已有发布 WorkCell"当前置，新项目增工位/区域后三维恒空白）：
    //   ①宿主 WorkCell（发布链 setWorkCell 后——常态路径）；
    //   ②场景当前 WorkCell（宿主装载，或上次装入的会话预览锚——复用）；
    //   ③新建会话预览锚装入场景（ensureSessionPreviewAnchor——仅场景
    //     层，studio 模型层零触碰）。
    // "无锚不画"是框架硬约束（场景挂接 API 在场景无 WorkCell 时抛异常
    // ——WorkCellScene::addDrawable 防御），修复让锚恒在位而非放弃渲染。
    rw::models::WorkCell::Ptr workcell = m_studio->getWorkCell();
    if (workcell.isNull()) {
        workcell = ensureSessionPreviewAnchor(*scene);
    }
    if (workcell.isNull()) {
        return false;  // 锚不可得（视图缺位等装配降级——诚实失败）
    }
    Frame* const world = workcell->getWorldFrame();
    if (world == nullptr) {
        return false;
    }

    // ①清旧组（节点名清单逐个删——原子替换语义的"先清"半区）。
    clear();

    // ②工位标记：findFrame 挂 FrameAxis（坐标轴随帧动——会话/示教移动
    // 零重投；帧不可解析＝跳过并追加失败定位到标签——失败可见面）＋
    // 标签 RenderText（UI-T52——F-490② 消账：工位名随帧浮动文本；
    // RenderText 构造期持帧——同帧同组随动）。
    // 帧解析锚＝上方现取的挂接锚 WC（F-550②——发布前＝会话预览锚，
    // "WORLD" 别名经 WorkCell::findFrame 特判命中其根帧；发布后＝编译
    // 产物 WC，同一别名命中发布根帧——两态查找语义一致）。
    int unresolved = 0;
    for (const View3DFrameMarker& marker : update.frameMarkers) {
        Frame* const frame = workcell->findFrame(marker.frameName);
        if (frame == nullptr) {
            ++unresolved;  // 失败可见面在投影方摘要——此处计数不虚构坐标轴
            continue;
        }
        const rw::core::Ptr<Frame> framePtr(frame);  // 非 owning 包装（场景树持有帧存活）
        const std::string axisName =
            std::string(kPreviewPrefix) + "marker-" + marker.label;
        scene->addFrameAxis(axisName, 0.25, frame);  // 轴长 0.25 m（工位尺度）
        m_nodeNames.push_back(axisName);
        // 标签（框架 RenderText——文本直投；标签名独立命名空间防与轴同名
        // 冲突——removeDrawable 按名逐个删互不干扰）。
        const std::string labelName =
            std::string(kPreviewPrefix) + "label-" + marker.label;
        scene->addRender(labelName,
                         rw::core::ownedPtr(
                             new rwlibs::opengl::RenderText(marker.label, framePtr)),
                         frame);
        m_nodeNames.push_back(labelName);
    }

    // ③区域边界框（世界系八角点 12 棱线框——辨识蓝）。
    if (update.boxOutline.has_value()) {
        const std::string nodeName = std::string(kPreviewPrefix) + "box";
        scene->addRender(nodeName,
                         rw::core::ownedPtr(new RegionOutlineRender(*update.boxOutline)),
                         world);
        m_nodeNames.push_back(nodeName);
    }

    // ④采样格（点阵着色——Good 绿/Weak 黄/Failed 红；空＝中性灰）。
    if (update.sampleGrid.has_value()) {
        const std::string nodeName = std::string(kPreviewPrefix) + "grid";
        scene->addRender(nodeName,
                         rw::core::ownedPtr(new SampleGridRender(*update.sampleGrid)),
                         world);
        m_nodeNames.push_back(nodeName);
    }

    // 失败可见面追加到摘要（协议值不可变——摘要由投影方承载；此处经
    // 节点名留痕：未解析帧计数以独立占位节点名可查——Dev 面诊断锚）。
    if (unresolved > 0) {
        const std::string nodeName = std::string(kPreviewPrefix) + "unresolved-"
                                     + std::to_string(unresolved);
        m_nodeNames.push_back(nodeName);  // 名单留痕（无渲染体——诊断锚）
    }
    return true;
}

rw::core::Ptr<rw::models::WorkCell>
HostView3DPreviewBackend::ensureSessionPreviewAnchor(WorkCellScene& scene)
{
    // ①场景已挂 WorkCell＝原样复用（宿主装载/发布链/上次锚——幂等；
    // 重复 setWorkCell 会无谓重建场景节点树并触发查看器世界节点再同步，
    // 见下②注）。成员同步持有当前场景锚（可读性——所有权本身由场景
    // 共享属主保证）。
    if (!scene.getWorkCell().isNull()) {
        m_sessionAnchor = scene.getWorkCell();
        return m_sessionAnchor;
    }
    // ②新建会话预览锚 WC：仅含框架根帧的空 WorkCell（名＝ird 前缀族
    // 约定——诊断可辨识；WC 名不入任何身份/缓存键，runtime §8.2 同口
    // 径）。模型层隔离：studio->getWorkCell() 保持空——发布链"WorkCell
    // 即编译产物本体"（HostPresentationAdapters INV-B4 宿主单入口）与
    // TreeView 等订阅 studio 工作单元事件的宿主组件，对本锚零感知零干扰。
    // 查看器先行校验（不半装）：世界节点重同步是装入的必要半区——查看
    // 器缺位（headless 等装配降级）＝装了也渲染不出，如实返回空交调用
    // 方按"锚不可得"处理；实践中 scene 可取得则查看器必在位（同一
    // RWStudioView3D 持有），本分支为防御面。
    if (m_studio->getView() == nullptr
        || m_studio->getView()->getSceneViewer() == nullptr) {
        return nullptr;
    }
    m_sessionAnchor = rw::core::ownedPtr(
        new rw::models::WorkCell("ird-session-preview"));
    scene.setWorkCell(m_sessionAnchor);
    // 世界节点重同步（框架硬契约）：WorkCellScene::setWorkCell 会重建
    // 场景世界节点（旧节点从根摘除、新节点按锚 WC 帧树生成），而查看器
    // 缓存的是旧节点引用——框架自身路径（RWStudioView3D::setWorkCell/
    // clear 尾段）都在装 WC 后补 setWorldNode，本直连场景的装配路径必须
    // 同款补挂，否则新节点树不进渲染。
    m_studio->getView()->getSceneViewer()->setWorldNode(scene.getWorldNode());
    return m_sessionAnchor;
}

void HostView3DPreviewBackend::clear()
{
    if (m_studio == nullptr) {
        return;
    }
    WorkCellScene::Ptr scene = m_studio->getWorkCellScene();
    if (scene.isNull()) {
        m_nodeNames.clear();
        return;
    }
    for (const std::string& name : m_nodeNames) {
        scene->removeDrawable(name);  // 幂等（不存在＝false，无副作用）
    }
    m_nodeNames.clear();
}

}  // namespace ui
}  // namespace ird
}  // namespace sdurws
