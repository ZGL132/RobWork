#include "KinematicAnalysisPlotWidget.hpp"

#include <QEvent>
#include <QFontMetrics>
#include <QHelpEvent>
#include <QImage>
#include <QMouseEvent>
#include <QPainter>
#include <QToolTip>

#include <algorithm>
#include <cmath>
#include <limits>

using namespace rws;

namespace {

QString axisLabelX (VisualProjection projection)
{
    switch (projection) {
        case VisualProjection::XY:
        case VisualProjection::XZ: return QStringLiteral ("x");
        case VisualProjection::YZ: return QStringLiteral ("y");
    }
    return QStringLiteral ("x");
}

QString axisLabelY (VisualProjection projection)
{
    switch (projection) {
        case VisualProjection::XY: return QStringLiteral ("y");
        case VisualProjection::XZ: return QStringLiteral ("z");
        case VisualProjection::YZ: return QStringLiteral ("z");
    }
    return QStringLiteral ("y");
}

}    // namespace

KinematicAnalysisPlotWidget::KinematicAnalysisPlotWidget (QWidget* parent) :
    QWidget (parent)
{
    setMouseTracking (true);
    setMinimumSize (320, 220);
}

void KinematicAnalysisPlotWidget::setVisualData (const AnalysisVisualData& data)
{
    _data = data;
    update ();
}

void KinematicAnalysisPlotWidget::setProjection (VisualProjection projection)
{
    _projection = projection;
    update ();
}

void KinematicAnalysisPlotWidget::setShowLabels (bool show)
{
    _showLabels = show;
    update ();
}

void KinematicAnalysisPlotWidget::setStatusFilters (
    bool showPass, bool showWarning, bool showFail, bool showUnknown)
{
    _showPass = showPass;
    _showWarning = showWarning;
    _showFail = showFail;
    _showUnknown = showUnknown;
    update ();
}

void KinematicAnalysisPlotWidget::setShowGrid (bool show)
{
    _showGrid = show;
    update ();
}

void KinematicAnalysisPlotWidget::setShowLegend (bool show)
{
    _showLegend = show;
    update ();
}

void KinematicAnalysisPlotWidget::setPointRadius (double radius)
{
    _pointRadius = radius;
    update ();
}

void KinematicAnalysisPlotWidget::setRenderMode (VisualRenderMode mode)
{
    _renderMode = mode;
    update ();
}

QImage KinematicAnalysisPlotWidget::renderToImage (const QSize& size) const
{
    const QSize targetSize = size.isValid () ? size : QSize (1200, 800);
    QImage image (targetSize, QImage::Format_ARGB32_Premultiplied);
    image.fill (palette ().base ().color ());
    QPainter painter (&image);
    painter.setRenderHint (QPainter::Antialiasing, true);
    paintPlot (painter, QRect (QPoint (0, 0), targetSize));
    return image;
}

QSize KinematicAnalysisPlotWidget::minimumSizeHint () const
{
    return QSize (320, 220);
}

QSize KinematicAnalysisPlotWidget::sizeHint () const
{
    return QSize (720, 420);
}

QRectF KinematicAnalysisPlotWidget::plotRect () const
{
    return visualPlotArea (rect (), _showLegend);
}

bool KinematicAnalysisPlotWidget::pointVisible (const AnalysisVisualPoint& point) const
{
    switch (point.status) {
        case AnalysisStatus::Pass:    return _showPass;
        case AnalysisStatus::Warning: return _showWarning;
        case AnalysisStatus::Fail:    return _showFail;
        case AnalysisStatus::Unknown:
        default:                      return _showUnknown;
    }
}

AnalysisVisualFilters KinematicAnalysisPlotWidget::filters () const
{
    AnalysisVisualFilters f;
    f.showPass = _showPass;
    f.showWarning = _showWarning;
    f.showFail = _showFail;
    f.showUnknown = _showUnknown;
    return f;
}

// P8:委托到纯 helper,消除 duplicated bounds 逻辑。
QRectF KinematicAnalysisPlotWidget::projectedBounds () const
{
    const AnalysisVisualBounds bounds =
        projectedVisualBounds (_data, _projection, filters ());
    if (!bounds.valid)
        return QRectF (-1.0, -1.0, 2.0, 2.0);

    double minX = bounds.minX;
    double maxX = bounds.maxX;
    double minY = bounds.minY;
    double maxY = bounds.maxY;
    if (std::fabs (maxX - minX) < 1e-9) {
        minX -= 0.5;
        maxX += 0.5;
    }
    if (std::fabs (maxY - minY) < 1e-9) {
        minY -= 0.5;
        maxY += 0.5;
    }
    const double padX = (maxX - minX) * 0.08;
    const double padY = (maxY - minY) * 0.08;
    return QRectF (minX - padX, minY - padY,
                   (maxX - minX) + 2.0 * padX,
                   (maxY - minY) + 2.0 * padY);
}

QPointF KinematicAnalysisPlotWidget::mapToPlot (
    const AnalysisVisualPoint& point, const QRectF& rect,
    const QRectF& bounds) const
{
    const QPointF p = projectVisualPoint (point, _projection);
    const double x = rect.left () +
        (p.x () - bounds.left ()) / bounds.width () * rect.width ();
    const double y = rect.bottom () -
        (p.y () - bounds.top ()) / bounds.height () * rect.height ();
    return QPointF (x, y);
}

bool KinematicAnalysisPlotWidget::visualPointAt (
    const QPoint& pos, AnalysisVisualPoint* hitPoint) const
{
    const QRectF pr = plotRect ();
    const QRectF bounds = projectedBounds ();
    double bestDist = std::numeric_limits< double >::max ();
    bool found = false;
    AnalysisVisualPoint bestPoint;
    for (const AnalysisVisualPoint& point : _data.points) {
        if (!pointVisible (point))
            continue;
        const QPointF mapped = mapToPlot (point, pr, bounds);
        const double dx = mapped.x () - pos.x ();
        const double dy = mapped.y () - pos.y ();
        const double dist = std::sqrt (dx * dx + dy * dy);
        if (dist <= 9.0 && dist < bestDist) {
            bestDist = dist;
            bestPoint = point;
            found = true;
        }
    }
    if (found && hitPoint != nullptr)
        *hitPoint = bestPoint;
    return found;
}

QString KinematicAnalysisPlotWidget::pointTooltipAt (const QPoint& pos) const
{
    AnalysisVisualPoint point;
    if (!visualPointAt (pos, &point))
        return QString ();
    return point.tooltip;
}

void KinematicAnalysisPlotWidget::mousePressEvent (QMouseEvent* event)
{
    if (event != nullptr && event->button () == Qt::LeftButton) {
        AnalysisVisualPoint point;
        if (visualPointAt (event->pos (), &point)) {
            Q_EMIT visualPointClicked (point);
            event->accept ();
            return;
        }
    }
    QWidget::mousePressEvent (event);
}

bool KinematicAnalysisPlotWidget::event (QEvent* event)
{
    if (event->type () == QEvent::ToolTip) {
        QHelpEvent* help = static_cast< QHelpEvent* > (event);
        const QString tip = pointTooltipAt (help->pos ());
        if (!tip.isEmpty ()) {
            QToolTip::showText (help->globalPos (), tip, this);
            return true;
        }
        QToolTip::hideText ();
        event->ignore ();
        return true;
    }
    return QWidget::event (event);
}

// paintEvent:标准 QWidget 重绘入口。创建 QPainter 调 paintPlot(rect())。
void KinematicAnalysisPlotWidget::paintEvent (QPaintEvent*)
{
    QPainter painter (this);
    // 开启抗锯齿,让散点和文字边缘平滑(在大点距下尤为重要)
    painter.setRenderHint (QPainter::Antialiasing, true);
    // 用调色板基础色填充整 widget 背景(避免与 plotArea 之间的留白透出)
    painter.fillRect (rect (), palette ().base ());
    paintPlot (painter, rect ());
}

// paintPlot:核心绘图入口,被 paintEvent 和 renderToImage 共享。
// 渲染顺序(由下到上):
//   1) plotArea 边框 → 2) 网格 + 刻度数字 → 3) 散点 → 4) 图例 →
//   5) 右下 info 文本 → 6) 轴标签(底部/旋转 90° 左侧) →
//   7) 可见点为 0 时画 "No visual data" 提示。
void KinematicAnalysisPlotWidget::paintPlot (QPainter& painter, const QRect& area) const
{
    // 0. 包络(Envelope)模式 — 直接绘制技术图纸,跳过散点渲染
    if (_renderMode == VisualRenderMode::Envelope) {
        paintEnvelope (painter, area);
        return;
    }

    // 1. plotArea:从 area 扣除边距 + 右侧图例空间
    const int reservedLegendWidth = visualLegendWidth (_showLegend, area);
    const QRectF pr = visualPlotArea (area, _showLegend);
    painter.setPen (QPen (palette ().mid ().color (), 1));
    painter.drawRect (pr);

    // 当前可见点投影后的 2D 边界(供 paintGrid 取刻度值)
    const QRectF bounds = projectedBounds ();

    // 2. 网格 + 刻度
    paintGrid (painter, pr, bounds);

    // 3. 按密度自动缩小点半径,避免大量样本重叠
    //    注意:用户 _pointRadius 是"上限",密度大时只缩不放大
    double radius = _pointRadius;
    if (_data.points.size () > 5000)
        radius = std::min (radius, 2.5);
    else if (_data.points.size () > 1000)
        radius = std::min (radius, 3.5);

    int visibleCount = 0;
    // 4. 散点:按状态过滤后,逐点画椭圆;inCollision 用深红描边强调
    for (const AnalysisVisualPoint& point : _data.points) {
        if (!pointVisible (point))
            continue;
        ++visibleCount;
        const QPointF mapped = mapToPlot (point, pr, bounds);
        const QColor color = visualColorForPoint (point, _data);
        // 碰撞点用深红粗描边 + 自身色填充,警示;非碰撞用色深 130
        painter.setPen (point.inCollision ?
            QPen (QColor (120, 20, 20), 2) :
            QPen (color.darker (130), 1));
        painter.setBrush (color);
        painter.drawEllipse (mapped, radius, radius);
        // 标签:点右侧 + 7px 偏移,避免遮挡点中心
        if (_showLabels && !point.label.isEmpty ()) {
            painter.setPen (palette ().text ().color ());
            painter.drawText (QPointF (mapped.x () + 7, mapped.y () - 5),
                              point.label);
        }
    }

    // 5. 图例(右侧/底部)— 数据源和图例助手决定渲染什么
    if (visualLegendVisible (_showLegend, area)) {
        const QRectF legendArea (
            pr.right () + 8,
            pr.top () + 4,
            reservedLegendWidth - 12,
            pr.height () - 8);
        paintLegend (painter, legendArea);
    }

    // 6. plotArea 顶部右侧显示 summary info
    painter.setPen (palette ().mid ().color ());
    painter.drawText (QRectF (pr.left (), 2, pr.width (), 18),
                      Qt::AlignRight | Qt::AlignVCenter,
                      QStringLiteral ("%1 point(s), %2, %3")
                          .arg (visibleCount)
                          .arg (visualProjectionText (_projection))
                          .arg (visualScalarModeText (_data.scalarMode)));

    // 7. 轴标签:底部 X + 旋转 90° 的左侧 Y
    painter.setPen (palette ().text ().color ());
    painter.drawText (QRectF (pr.left (), pr.bottom () + 8, pr.width (), 20),
                      Qt::AlignCenter,
                      QStringLiteral ("%1 (m)").arg (axisLabelX (_projection)));
    painter.drawText (QRectF (0, pr.bottom () + 8, pr.left () - 4, 20),
                      Qt::AlignCenter,
                      QStringLiteral ("%1 (m)").arg (axisLabelY (_projection)));

    // 8. 空数据提示
    if (visibleCount == 0) {
        painter.setPen (palette ().mid ().color ());
        painter.drawText (pr, Qt::AlignCenter,
                          QStringLiteral ("No visual data"));
    }
}

// paintGrid:5 等分刻度虚线网格 + 数字标签。
// 数字格式 'g' 4(自动选择 fixed/scientific),保留 4 位有效数字。
// 字体自动缩小但不低于 7pt,保证可读性。
void KinematicAnalysisPlotWidget::paintGrid (
    QPainter& painter, const QRectF& plotArea, const QRectF& bounds) const
{
    if (!_showGrid)
        return;

    painter.save ();
    // 网格虚线:mid 色 lighten 130(更浅),1 像素 Qt::DotLine
    QPen gridPen (palette ().mid ().color ().lighter (130), 1, Qt::DotLine);
    painter.setPen (gridPen);

    // 刻度字体:小一号但 ≥ 7pt
    QFont tickFont = painter.font ();
    tickFont.setPointSize (std::max (7, tickFont.pointSize () - 1));
    painter.setFont (tickFont);
    painter.setPen (palette ().text ().color ());

    // 5 等分刻度(0, 0.25, 0.5, 0.75, 1)
    for (int i = 0; i <= 4; ++i) {
        const double ratio = static_cast< double > (i) / 4.0;

        // 垂直网格线 + 底部 X 刻度
        {
            const double x = plotArea.left () + ratio * plotArea.width ();
            painter.setPen (gridPen);
            painter.drawLine (QPointF (x, plotArea.top ()),
                              QPointF (x, plotArea.bottom ()));
            // 把屏幕坐标反算回数据坐标(bounds),打印实际值
            const double dataX = bounds.left () + ratio * bounds.width ();
            painter.setPen (palette ().text ().color ());
            // 数字位置:plotArea 下方 28px,居中,40 像素宽
            painter.drawText (QRectF (x - 20, plotArea.bottom () + 28, 40, 16),
                              Qt::AlignCenter,
                              QString::number (dataX, 'g', 4));
        }

        // 水平网格线 + 左侧 Y 刻度
        {
            // 注意 Y 方向反算:bounds.top() + ratio × height 表示从顶向下的距离
            const double y = plotArea.bottom () - ratio * plotArea.height ();
            painter.setPen (gridPen);
            painter.drawLine (QPointF (plotArea.left (), y),
                              QPointF (plotArea.right (), y));
            const double dataY = bounds.top () + ratio * bounds.height ();
            painter.setPen (palette ().text ().color ());
            // 数字位置:plotArea 左侧 42px,垂直居中
            painter.drawText (QRectF (plotArea.left () - 42, y - 8, 38, 16),
                              Qt::AlignRight | Qt::AlignVCenter,
                              QString::number (dataY, 'g', 4));
        }
    }

    painter.restore ();
}

// paintLegend:状态色块图例或标量色带。绘制位置由 paintPlot 传入的 legendArea 决定。
void KinematicAnalysisPlotWidget::paintLegend (
    QPainter& painter, const QRectF& legendArea) const
{
    painter.save ();

    const double legendLeft = legendArea.left ();
    const double legendTop = legendArea.top ();

    if (_data.scalarMode == VisualScalarMode::Status) {
        // 状态图例:Pass / Warning / Fail / Unknown
        struct Swatch { QString label; AnalysisStatus status; };
        const Swatch swatches[] = {
            {QStringLiteral ("Pass"), AnalysisStatus::Pass},
            {QStringLiteral ("Warning"), AnalysisStatus::Warning},
            {QStringLiteral ("Fail"), AnalysisStatus::Fail},
            {QStringLiteral ("Unknown"), AnalysisStatus::Unknown},
        };
        const double swatchSize = 10.0;
        double y = legendTop;
        QFont smallFont = painter.font ();
        smallFont.setPointSize (std::max (7, smallFont.pointSize () - 1));
        painter.setFont (smallFont);

        for (const Swatch& sw : swatches) {
            AnalysisVisualPoint pt;
            pt.status = sw.status;
            const QColor c = visualColorForPoint (pt, _data);
            painter.setPen (Qt::NoPen);
            painter.setBrush (c);
            painter.drawRect (QRectF (legendLeft, y, swatchSize, swatchSize));

            painter.setPen (palette ().text ().color ());
            painter.drawText (QPointF (legendLeft + swatchSize + 4, y + swatchSize - 1),
                              sw.label);
            y += swatchSize + 3;
        }
    }
    else if (_data.scalarMode == VisualScalarMode::Collision) {
        // Collision 图例:Collision / Free
        const double swatchSize = 10.0;
        double y = legendTop;
        QFont smallFont = painter.font ();
        smallFont.setPointSize (std::max (7, smallFont.pointSize () - 1));
        painter.setFont (smallFont);

        for (const auto& pair : {std::make_pair (1.0, QStringLiteral ("Collision")),
                                 std::make_pair (0.0, QStringLiteral ("Free"))}) {
            AnalysisVisualPoint pt;
            pt.scalar = pair.first;
            pt.hasFiniteScalar = true;
            AnalysisVisualData dummy;
            dummy.scalarMode = VisualScalarMode::Collision;
            dummy.hasFiniteScalar = true;
            dummy.scalarMin = 0.0;
            dummy.scalarMax = 1.0;
            const QColor c = visualColorForPoint (pt, dummy);
            painter.setPen (Qt::NoPen);
            painter.setBrush (c);
            painter.drawRect (QRectF (legendLeft, y, swatchSize, swatchSize));

            painter.setPen (palette ().text ().color ());
            painter.drawText (QPointF (legendLeft + swatchSize + 4, y + swatchSize - 1),
                              pair.second);
            y += swatchSize + 3;
        }
    }
    else if (_data.hasFiniteScalar) {
        // 标量色带:min → max 渐变条
        const double rampWidth = 14.0;
        const double rampHeight = 60.0;
        const QRectF rampRect (legendLeft, legendTop, rampWidth, rampHeight);
        for (int py = 0; py < static_cast< int > (rampHeight); ++py) {
            const double t = 1.0 - static_cast< double > (py) / rampHeight;
            AnalysisVisualPoint pt;
            pt.scalar = _data.scalarMin + t * (_data.scalarMax - _data.scalarMin);
            pt.hasFiniteScalar = true;
            const QColor c = visualColorForPoint (pt, _data);
            painter.setPen (c);
            painter.drawLine (QPointF (legendLeft, legendTop + py),
                              QPointF (legendLeft + rampWidth, legendTop + py));
        }
        painter.setPen (QPen (palette ().mid ().color (), 1));
        painter.setBrush (Qt::NoBrush);
        painter.drawRect (rampRect);

        painter.setPen (palette ().text ().color ());
        QFont smallFont = painter.font ();
        smallFont.setPointSize (std::max (7, smallFont.pointSize () - 1));
        painter.setFont (smallFont);
        painter.drawText (QPointF (legendLeft + rampWidth + 4, legendTop + rampHeight),
                          QString::number (_data.scalarMin, 'g', 4));
        painter.drawText (QPointF (legendLeft + rampWidth + 4, legendTop + 6),
                          QString::number (_data.scalarMax, 'g', 4));
    }

    painter.restore ();
}
#include "KinematicAnalysisEnvelope.hpp"

// =============================================================================
//  包络(Envelope)渲染
// =============================================================================

QRectF KinematicAnalysisPlotWidget::envelopeBounds () const
{
    const AnalysisEnvelopeData& envelope = _data.envelope;
    if (!envelope.valid)
        return QRectF (-1.0, -1.0, 2.0, 2.0);
    const double padX = std::max (0.05, envelope.width * 0.12);
    const double padY = std::max (0.05, envelope.height * 0.12);
    return QRectF (envelope.minX - padX,
                   envelope.minY - padY,
                   envelope.width + 2.0 * padX,
                   envelope.height + 2.0 * padY);
}

QPointF KinematicAnalysisPlotWidget::mapEnvelopePoint (
    const QPointF& point, const QRectF& rect, const QRectF& bounds) const
{
    const double x = rect.left () +
        (point.x () - bounds.left ()) / bounds.width () * rect.width ();
    const double y = rect.bottom () -
        (point.y () - bounds.top ()) / bounds.height () * rect.height ();
    return QPointF (x, y);
}

void KinematicAnalysisPlotWidget::paintDimensionLine (
    QPainter& painter,
    const QPointF& a,
    const QPointF& b,
    const QString& text) const
{
    painter.drawLine (a, b);
    const QPointF mid ((a.x () + b.x ()) * 0.5, (a.y () + b.y ()) * 0.5);
    painter.drawText (QRectF (mid.x () - 45, mid.y () - 18, 90, 18),
                      Qt::AlignCenter,
                      text);
}

void KinematicAnalysisPlotWidget::paintEnvelope (
    QPainter& painter, const QRect& area) const
{
    painter.setRenderHint (QPainter::Antialiasing, true);
    const QFontMetrics fm = painter.fontMetrics ();

    // ---- 边距预算(基于当前字体) ----
    const double topMargin    = static_cast< double > (fm.height () + 6);   // 标题行
    const double bottomMargin = static_cast< double > (fm.height () * 3 + 20); // 水平尺寸线 + 说明
    const double rightMargin  = static_cast< double > (fm.horizontalAdvance (QStringLiteral ("00000 mm")) + 20); // 垂直尺寸线
    const double leftMargin   = 12.0;

    const double plotX = area.x () + leftMargin;
    const double plotY = area.y () + topMargin;
    const double plotW = static_cast< double > (area.width ()) - leftMargin - rightMargin;
    const double plotH = static_cast< double > (area.height ()) - topMargin - bottomMargin;

    // 区域太小无法绘图
    if (plotW < 50.0 || plotH < 50.0) {
        painter.setPen (palette ().mid ().color ());
        painter.drawText (QRectF (area), Qt::AlignCenter,
                          QStringLiteral ("Area too small for envelope"));
        return;
    }

    const QRectF pr (plotX, plotY, plotW, plotH);
    painter.setPen (QPen (palette ().mid ().color (), 1));
    painter.drawRect (pr);

    const AnalysisEnvelopeData& envelope = _data.envelope;
    if (!envelope.valid) {
        painter.setPen (palette ().mid ().color ());
        painter.drawText (pr, Qt::AlignCenter,
                          QStringLiteral ("No approximate outer envelope data"));
        return;
    }

    const QRectF bounds = envelopeBounds ();
    QPolygonF polygon;
    for (const QPointF& point : envelope.boundary)
        polygon << mapEnvelopePoint (point, pr, bounds);

    painter.setPen (QPen (QColor (30, 30, 30), 1.2));
    painter.setBrush (QColor (230, 231, 233));
    painter.drawPolygon (polygon);

    const QPointF origin = mapEnvelopePoint (envelope.origin, pr, bounds);
    painter.setPen (QPen (QColor (70, 70, 70), 1, Qt::DashLine));
    painter.drawLine (QPointF (pr.left (), origin.y ()), QPointF (pr.right (), origin.y ()));
    painter.drawLine (QPointF (origin.x (), pr.top ()), QPointF (origin.x (), pr.bottom ()));

    painter.setPen (QPen (QColor (180, 180, 180), 1, Qt::DashLine));
    painter.setBrush (Qt::NoBrush);
    painter.drawEllipse (origin, pr.width () * 0.18, pr.height () * 0.18);

    painter.setPen (QPen (QColor (20, 20, 20), 1));
    painter.setBrush (QColor (20, 20, 20));
    painter.drawEllipse (origin, 3.0, 3.0);

    const double scale = 1000.0;
    const QString unit = QStringLiteral ("mm");

    // 底部:水平尺寸线(在 plotArea 下方,但仍在 area 内)
    const double dimY = area.y () + static_cast< double > (area.height ()) - bottomMargin + 4.0;
    paintDimensionLine (
        painter,
        QPointF (plotX, dimY),
        QPointF (plotX + plotW, dimY),
        QStringLiteral ("%1 %2").arg (envelope.width * scale, 0, 'f', 0).arg (unit));

    // 右侧:垂直尺寸线(在 plotArea 右侧,但仍在 area 内)
    const double dimX = plotX + plotW + 8.0;
    const double dimCenterY = plotY + plotH * 0.5;
    painter.save ();
    painter.translate (dimX, dimCenterY);
    painter.rotate (-90.0);
    const QString heightText = QStringLiteral ("%1 %2")
        .arg (envelope.height * scale, 0, 'f', 0).arg (unit);
    painter.drawText (QRectF (-static_cast< double > (plotH) * 0.5, -12.0,
                               plotH, 24.0),
                      Qt::AlignCenter, heightText);
    painter.restore ();

    // 顶部:标题(在 plotArea 上方,但仍在 area 内)
    painter.drawText (QRectF (plotX, area.y () + 2, plotW, topMargin - 4),
                      Qt::AlignRight | Qt::AlignVCenter,
                      QStringLiteral ("Approximate outer envelope, %1, Rmax %2 %3")
                          .arg (visualProjectionText (envelope.projection))
                          .arg (envelope.maxRadius * scale, 0, 'f', 0)
                          .arg (unit));

    // 底部:视图说明(在水平尺寸线下方)
    painter.setPen (palette ().text ().color ());
    const QString caption = envelope.projection == VisualProjection::XY ?
        QStringLiteral ("Approximate outer envelope, top view — not exact reachability") :
        QStringLiteral ("Approximate outer envelope, side view — not exact reachability");
    const double captionY = area.y () + static_cast< double > (area.height ()) - bottomMargin + fm.height () + 14.0;
    painter.drawText (QRectF (plotX, captionY, plotW, 20.0),
                      Qt::AlignCenter, caption);
}
