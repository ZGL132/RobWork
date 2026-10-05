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
#include <rw/graphics/WorkCellScene.hpp>
#include <rw/kinematics/Frame.hpp>
#include <rwlibs/opengl/DrawableUtil.hpp>
#include <rwlibs/opengl/RenderText.hpp>  // 框架文本渲染（UI-T52——工位标签；F-490② 消账）
#include <rwlibs/opengl/rwgl.hpp>  // GL 聚合头（框架 RenderFrame 同款——平台 glext 由其内部处理）

#include <rws/RobWorkStudio.hpp>

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

/// 区域框 tint 分色（UI-T65——覆盖率映射档；None/缺省＝缺省蓝保持——
/// 选中预览的辨识色不因无评估而改变）。返回色三元组供 glColor4f 消费。
std::array<float, 3> tintColor(std::optional<View3DTint> tint)
{
    if (!tint.has_value() || *tint == View3DTint::None) {
        return {0.12f, 0.35f, 0.66f};  // 缺省蓝（既有选中辨识色——原值保持）
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
    return {0.12f, 0.35f, 0.66f};  // 不可达分支（switch 全覆盖——防御面）
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
    // 场景与工作 cell 现取（studio 公开 API 直供——getView 中介零需要；
    // world 帧＝workcell 的世界帧——线框/格线挂接锚）。
    if (m_studio == nullptr) {
        return false;
    }
    WorkCellScene::Ptr scene = m_studio->getWorkCellScene();
    const rw::models::WorkCell::Ptr workcell = m_studio->getWorkCell();
    if (scene.isNull() || workcell.isNull()) {
        return false;
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
    int unresolved = 0;
    for (const View3DFrameMarker& marker : update.frameMarkers) {
        Frame* const frame = m_studio->getWorkCell() != nullptr
                                 ? m_studio->getWorkCell()->findFrame(marker.frameName)
                                 : nullptr;
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
