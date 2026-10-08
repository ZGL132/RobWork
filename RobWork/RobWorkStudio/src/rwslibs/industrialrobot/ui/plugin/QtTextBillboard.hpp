/**
 * @file   QtTextBillboard.hpp
 * @brief  Qt 纹理化三维文本 billboard（UI-T77——F-556 的文本替代承载；
 *         会话预览标签与发布模型骨架标签共用的渲染基建件）。
 *
 * 设计依据：
 *   - findings F-556（工位标记标签中文乱码——rwlibs::opengl::RenderText
 *     用 GLUT 位图字体渲染，freeglut bitmap 仅 ASCII 字形集无 CJK，
 *     UTF-8 工位名按字节映射拉丁字形即乱码；框架级限制非产品缺陷，
 *     三选一裁决选方案②Qt 纹理化文本替代件）；
 *   - 挂接先例：框架 RenderText 的 billboard 手法（相机世界变换→挂接
 *     帧局部系四角）与 HostView3DPreviewBackend 的自绘 Render 范式。
 *
 * 背景说明：本件是 ui 单元内部实现件（plugin 私有头——不经 include/
 * 公共面外溢）；消费面＝需求会话预览标签（HostView3DPreviewBackend）
 * 与发布模型骨架标签（HostPresentationAdapters——F-550）。两处同源
 * 单件，杜绝第二文本承载实现漂移。
 *
 * 线程约束：仅 UI 线程（RobWorkStudio 场景渲染与 Qt 事件循环同线程
 * ——QImage 离屏排版无跨线程约束；进程级纹理缓存单线程访问）。
 */

#ifndef IRD_UI_PLUGIN_QTTEXTBILLBOARD_HPP
#define IRD_UI_PLUGIN_QTTEXTBILLBOARD_HPP

#include <string>

#include <rw/graphics/Render.hpp>
#include <rw/kinematics/Frame.hpp>
#include <rw/math/Transform3D.hpp>
#include <rw/math/Vector3D.hpp>

namespace rw {
namespace graphics {
class DrawableNode;
class SceneCamera;
}  // namespace graphics
}  // namespace rw

namespace sdurws {
namespace ird {
namespace ui {

/**
 * @brief Qt 纹理化文本 billboard 值件（UTF-8 文本→GL 纹理→面向相机
 *        四边形；F-556 的 CJK 字形承载）。
 *
 * 渲染管线：QImage(RGBA8888)＋QPainter 离屏排版（QFont 默认应用字体
 * ——Windows 下 CJK 字形经系统字体回退保障；QString::fromStdString
 * 对 UTF-8 原生解码）→ glTexImage2D 上传单级纹理 → billboard。
 *
 * 纹理生命周期（登记性取舍）：纹理按「文本＋字色」键入进程级缓存——
 * 命中复用不上传不删除；GL 纹理句柄跨上下文无效，故记录创建上下文、
 * 检出更换即整缓存作废重建（RobWorkStudio 视图上下文进程内稳定，重建
 * 仅发生在视图重建拍）；上下文销毁时其纹理由驱动回收，进程级缓存不
 * 主动 glDeleteTextures（避免 Render 析构时上下文非当前的删除陷阱）。
 *
 * 约束：drawBillboard 须在挂接帧的 GL 局部坐标系内调用（场景 Render
 * 契约）；世界尺寸固定（高 0.06 m——远距缩小是已知取舍，自适应尺寸
 * 随文本渲染基建任务演进〔WP-10-T05〕）。
 */
class QtTextBillboard
{
  public:
    /// 构造（文本为 UTF-8 字面——工位/帧名原值；字色经 UiTheme 词表
    /// kMarkerLabelHex——工程蓝单一供色点，本件无第二色值源）。
    explicit QtTextBillboard(std::string text);

    /// 文本世界高（m——工位尺度标签；与工位轴长 0.25 m 同尺度族）。
    static constexpr double kTextHeightM = 0.06;

    /// 已上传纹理（缓存值——id 在创建上下文内有效，尺寸供 billboard
    /// 纵横比展开）。
    struct TextTexture
    {
        unsigned int id;  ///< GL 纹理名（GLuint——避免本头引 GL 聚合面）
        int w;            ///< 图像宽（px）
        int h;            ///< 图像高（px）
    };

    /**
     * @brief 在当前 GL 局部系的 @p centerLocal 处绘制面向相机的文本牌。
     *
     * @param centerLocal [in] 文本牌中心（**挂接帧局部系**，m）
     * @param wTframe     [in] 挂接帧世界位姿（billboard 朝向计算输入——
     *                    挂 WORLD 根帧时传恒等变换即可）
     * @param info        [in] 渲染信息（相机/状态来源；相机缺位＝跳过）
     * @param alpha       [in] drawable 透明度（场景渲染排序语义）
     */
    void drawBillboard(const rw::math::Vector3D<double>& centerLocal,
                       const rw::math::Transform3D<double>& wTframe,
                       const rw::graphics::DrawableNode::RenderInfo& info,
                       double alpha) const;

  private:
    /**
     * @brief Qt 离屏排版＋GL 纹理上传（懒建——首次 draw 时执行；须在
     *        GL 上下文当前线程＝UI 线程调用）。
     */
    static TextTexture rasterizeAndUpload(const std::string& text);

    std::string m_text;  ///< 标签文本（UTF-8——构造冻结）
};

/**
 * @brief 挂帧文本标签渲染（F-556：挂帧指示器形态的标签承载，替代框架
 *        RenderText〔GLUT 位图字体无 CJK——乱码根因〕）。
 *
 * 挂任意帧（frameName 解析产物），标签画在帧原点上方固定偏移处、面向
 * 相机；帧存活由场景树保证（rw::core::Ptr 非 owning 包装——框架
 * RenderText 构造同款先例）。
 */
class QtTextBillboardRender final : public rw::graphics::Render
{
  public:
    /// 构造（frame 为挂接帧——场景树持存活；文本 UTF-8 原值）。
    QtTextBillboardRender(std::string text, rw::core::Ptr<rw::kinematics::Frame> frame);

    void draw(const rw::graphics::DrawableNode::RenderInfo& info,
              rw::graphics::DrawableNode::DrawType type,
              double alpha) const override;

  private:
    /// 标签相对帧原点的上方偏移（m——工位尺度；RenderText 旧视觉同位）。
    static constexpr double kLabelLiftM = 0.30;

    QtTextBillboard m_label;       ///< 文本 billboard（UTF-8——构造冻结）
    rw::core::Ptr<rw::kinematics::Frame> m_frame;  ///< 挂接帧（非 owning）
};

}  // namespace ui
}  // namespace ird
}  // namespace sdurws

#endif  // IRD_UI_PLUGIN_QTTEXTBILLBOARD_HPP
