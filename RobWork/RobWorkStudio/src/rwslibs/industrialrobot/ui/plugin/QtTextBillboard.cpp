/**
 * @file   QtTextBillboard.cpp
 * @brief  Qt 纹理化三维文本 billboard 实现（契约头 QtTextBillboard.hpp
 *         ——F-556 的 CJK 字形承载；离屏排版/GL 纹理/billboard 三段）。
 *
 * GL 纪律：固定管线即时模式＋GL 1.1 标准纹理入口（rwlibs/opengl/rwgl
 * .hpp 框架聚合面——ui_plugin 链接面既有）；混合状态自管进出配对，不
 * 外泄状态给后续 drawable。
 */

#include "QtTextBillboard.hpp"

#include <sdurws/ird/ui/UiTheme.hpp>  // 标签字色词表（kMarkerLabelHex——UI-T77 单一供色点）

#include <QColor>          // 字色（词表 hex → Qt 值）
#include <QFont>           // 离屏排版字体（应用默认字体——CJK 字形来源）
#include <QFontMetrics>    // 文本像素度量（纹理尺寸）
#include <QImage>          // 离屏文本位图（RGBA8888——GL 纹理上传源）
#include <QOpenGLContext>  // 当前 GL 上下文（纹理缓存失效防御——上下文更换）
#include <QPainter>        // 离屏排版（UTF-8/CJK 原生字形）

#include <rw/graphics/DrawableNode.hpp>
#include <rw/graphics/SceneCamera.hpp>  // 相机世界位姿（billboard 朝向）
#include <rwlibs/opengl/rwgl.hpp>       // GL 聚合头（框架 RenderFrame 同款）

#include <algorithm>
#include <map>
#include <utility>

using namespace rw::graphics;
using namespace rw::kinematics;
using namespace rw::math;

namespace sdurws {
namespace ird {
namespace ui {

QtTextBillboard::QtTextBillboard(std::string text)
    : m_text(std::move(text))
{
}

QtTextBillboard::TextTexture
QtTextBillboard::rasterizeAndUpload(const std::string& text)
{
    // ---- Qt 离屏排版：QFont 默认应用字体（像素字号——DPI 无关，纹理
    // 尺寸稳定）；UTF-8 → QString 原生解码，CJK 字形经系统字体回退
    // （Windows 下"Microsoft YaHei"族）——GLUT 位图字体做不到的这一步
    // 就是 F-556 乱码的消解点。图像留 8/4 px 内边距防字形裁边。
    QFont font;
    font.setPixelSize(16);
    const QString qs = QString::fromStdString(text);
    const QFontMetrics fm(font);
    TextTexture tex{0, std::max(1, fm.horizontalAdvance(qs) + 8),
                    std::max(1, fm.height() + 4)};
    QImage img(tex.w, tex.h, QImage::Format_RGBA8888);
    img.fill(Qt::transparent);
    QPainter painter(&img);
    painter.setFont(font);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QColor(ui::palette::kMarkerLabelHex));  // 词表字色（工程蓝）
    painter.drawText(img.rect(), Qt::AlignCenter, qs);
    painter.end();

    // ---- GL 纹理上传（GL 1.1 标准入口；RGBA8888 内存布局与
    // GL_RGBA/GL_UNSIGNED_BYTE 直配——无字节序换算；NPOT 尺寸在现代
    // GL 兼容面直上，LINEAR 单级过滤——标签文本无 mipmap 需求）。
    glGenTextures(1, &tex.id);
    glBindTexture(GL_TEXTURE_2D, tex.id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tex.w, tex.h, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, img.bits());
    return tex;
}

void QtTextBillboard::drawBillboard(
    const Vector3D<double>& centerLocal,
    const Transform3D<double>& wTframe,
    const DrawableNode::RenderInfo& info, double alpha) const
{
    // ①GL 上下文现取（纹理上传/绑定的前提；无上下文＝离屏快照等
    // 装配态——文本层诚实跳过，不虚构呈现）。
    QOpenGLContext* const ctx = QOpenGLContext::currentContext();
    if (ctx == nullptr) {
        return;
    }
    // ②相机现取（billboard 朝向来源；无相机信息＝无法面向视口——同上）。
    if (info._cam.isNull()) {
        return;
    }
    // ③纹理缓存（进程级——键＝文本＋字色；上下文更换＝旧句柄全失效，
    // 整缓存作废重建，见契约头生命周期说明。仅 UI 线程访问——§3.4）。
    static std::map<std::pair<std::string, QRgb>, TextTexture> s_cache;
    static QOpenGLContext* s_texContext = nullptr;
    if (s_texContext != ctx) {
        s_cache.clear();
        s_texContext = ctx;
    }
    const QRgb labelRgba = QColor(ui::palette::kMarkerLabelHex).rgba();
    const auto key = std::make_pair(m_text, labelRgba);
    auto it = s_cache.find(key);
    if (it == s_cache.end()) {
        it = s_cache.emplace(key, rasterizeAndUpload(m_text)).first;
    }
    const TextTexture& tex = it->second;

    // ④billboard 四角（局部系）：相机右/上向量经挂接帧逆变换投回局部
    // 系（框架 RenderText 同款手法——wTf⁻¹·wTc 的旋转取列 0/列 1），
    // 中心对 @p centerLocal、世界尺寸按图像纵横比展开。
    const Transform3D<double> wTc = info._cam->getTransform();
    const Rotation3D<double> fTcR = inverse(wTframe).R() * wTc.R();
    const Vector3D<double> right(fTcR(0, 0), fTcR(1, 0), fTcR(2, 0));
    const Vector3D<double> up(fTcR(0, 1), fTcR(1, 1), fTcR(2, 1));
    const double halfH = 0.5 * kTextHeightM;
    const double halfW =
        halfH * static_cast<double>(tex.w) / static_cast<double>(tex.h);
    const Vector3D<double> tl = centerLocal - right * halfW + up * halfH;
    const Vector3D<double> tr = centerLocal + right * halfW + up * halfH;
    const Vector3D<double> br = centerLocal + right * halfW - up * halfH;
    const Vector3D<double> bl = centerLocal - right * halfW - up * halfH;

    // ⑤纹理四边形（混合状态自管——进出配对；v 坐标 0＝QImage 顶行
    // 〔行序直上 GL——glTexImage2D 自 v=0 装载首行〕）。
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBindTexture(GL_TEXTURE_2D, tex.id);
    glColor4f(1.0f, 1.0f, 1.0f, static_cast<float>(alpha));
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f);
    glVertex3d(tl[0], tl[1], tl[2]);
    glTexCoord2f(1.0f, 0.0f);
    glVertex3d(tr[0], tr[1], tr[2]);
    glTexCoord2f(1.0f, 1.0f);
    glVertex3d(br[0], br[1], br[2]);
    glTexCoord2f(0.0f, 1.0f);
    glVertex3d(bl[0], bl[1], bl[2]);
    glEnd();
    glDisable(GL_BLEND);
    glDisable(GL_TEXTURE_2D);
}

// =====================================================================
// QtTextBillboardRender——挂帧文本标签（F-556 替代 RenderText 的场景件）
// =====================================================================

QtTextBillboardRender::QtTextBillboardRender(std::string text,
                                             rw::core::Ptr<Frame> frame)
    : m_label(std::move(text)), m_frame(frame)
{
}

void QtTextBillboardRender::draw(const DrawableNode::RenderInfo& info,
                                 DrawableNode::DrawType type,
                                 double alpha) const
{
    (void)type;
    // 帧/状态缺位＝场景树装配残态（防御面——不渲染不崩；RenderInfo
    // 的 _state 为裸指针形态——空判用指针比较而非 Ptr::isNull）。
    if (m_frame.isNull() || info._state == nullptr) {
        return;
    }
    // 挂接帧世界位姿（billboard 朝向计算输入——随 State 实时取）。
    const Transform3D<double> wTframe = m_frame->wTf(*info._state);
    // 标签中心＝帧原点上方（与轴指示器的视觉层次分离——轴在原点、名在
    // 上方；偏移与点值标记标签同族）。
    m_label.drawBillboard(Vector3D<double>(0.0, 0.0, kLabelLiftM), wTframe,
                          info, alpha);
}

}  // namespace ui
}  // namespace ird
}  // namespace sdurws
