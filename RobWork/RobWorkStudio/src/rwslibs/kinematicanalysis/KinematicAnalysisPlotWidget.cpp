#include "KinematicAnalysisPlotWidget.hpp"

#include <QEvent>
#include <QFontMetrics>
#include <QHelpEvent>
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
    bool showPass, bool showWarning, bool showFail)
{
    _showPass = showPass;
    _showWarning = showWarning;
    _showFail = showFail;
    update ();
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
    return rect ().adjusted (48, 18, -18, -36);
}

bool KinematicAnalysisPlotWidget::pointVisible (const AnalysisVisualPoint& point) const
{
    switch (point.status) {
        case AnalysisStatus::Pass:    return _showPass;
        case AnalysisStatus::Warning: return _showWarning;
        case AnalysisStatus::Fail:    return _showFail;
        case AnalysisStatus::Unknown:
        default:                      return true;
    }
}

QRectF KinematicAnalysisPlotWidget::projectedBounds () const
{
    bool first = true;
    double minX = 0.0, minY = 0.0, maxX = 1.0, maxY = 1.0;
    for (const AnalysisVisualPoint& point : _data.points) {
        if (!pointVisible (point))
            continue;
        const QPointF p = projectVisualPoint (point, _projection);
        if (!std::isfinite (p.x ()) || !std::isfinite (p.y ()))
            continue;
        if (first) {
            minX = maxX = p.x ();
            minY = maxY = p.y ();
            first = false;
        }
        else {
            minX = std::min (minX, p.x ());
            maxX = std::max (maxX, p.x ());
            minY = std::min (minY, p.y ());
            maxY = std::max (maxY, p.y ());
        }
    }
    if (first)
        return QRectF (-1.0, -1.0, 2.0, 2.0);
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

QString KinematicAnalysisPlotWidget::pointTooltipAt (const QPoint& pos) const
{
    const QRectF pr = plotRect ();
    const QRectF bounds = projectedBounds ();
    double bestDist = std::numeric_limits< double >::max ();
    QString bestTip;
    for (const AnalysisVisualPoint& point : _data.points) {
        if (!pointVisible (point))
            continue;
        const QPointF mapped = mapToPlot (point, pr, bounds);
        const double dx = mapped.x () - pos.x ();
        const double dy = mapped.y () - pos.y ();
        const double dist = std::sqrt (dx * dx + dy * dy);
        if (dist <= 9.0 && dist < bestDist) {
            bestDist = dist;
            bestTip = point.tooltip;
        }
    }
    return bestTip;
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

void KinematicAnalysisPlotWidget::paintEvent (QPaintEvent*)
{
    QPainter painter (this);
    painter.setRenderHint (QPainter::Antialiasing, true);
    painter.fillRect (rect (), palette ().base ());

    const QRectF pr = plotRect ();
    painter.setPen (QPen (palette ().mid ().color (), 1));
    painter.drawRect (pr);

    const QRectF bounds = projectedBounds ();
    painter.setPen (palette ().text ().color ());
    painter.drawText (QRectF (pr.left (), pr.bottom () + 8, pr.width (), 20),
                      Qt::AlignCenter,
                      QStringLiteral ("%1 (m)").arg (axisLabelX (_projection)));
    painter.save ();
    painter.translate (12, pr.center ().y ());
    painter.rotate (-90);
    painter.drawText (QRectF (-pr.height () / 2.0, 0, pr.height (), 20),
                      Qt::AlignCenter,
                      QStringLiteral ("%1 (m)").arg (axisLabelY (_projection)));
    painter.restore ();

    int visibleCount = 0;
    for (const AnalysisVisualPoint& point : _data.points) {
        if (!pointVisible (point))
            continue;
        ++visibleCount;
        const QPointF mapped = mapToPlot (point, pr, bounds);
        const QColor color = visualColorForPoint (point, _data);
        painter.setPen (QPen (color.darker (130), 1));
        painter.setBrush (color);
        painter.drawEllipse (mapped, point.inCollision ? 5.5 : 4.5,
                             point.inCollision ? 5.5 : 4.5);
        if (_showLabels && !point.label.isEmpty ()) {
            painter.setPen (palette ().text ().color ());
            painter.drawText (QPointF (mapped.x () + 7, mapped.y () - 5),
                              point.label);
        }
    }

    painter.setPen (palette ().mid ().color ());
    painter.drawText (QRectF (pr.left (), 2, pr.width (), 18),
                      Qt::AlignRight | Qt::AlignVCenter,
                      QStringLiteral ("%1 point(s), %2, %3")
                          .arg (visibleCount)
                          .arg (visualProjectionText (_projection))
                          .arg (visualScalarModeText (_data.scalarMode)));

    if (visibleCount == 0) {
        painter.setPen (palette ().mid ().color ());
        painter.drawText (pr, Qt::AlignCenter,
                          QStringLiteral ("No visual data"));
    }
}
