#ifndef RWS_KINEMATICANALYSIS_KINEMATICANALYSISPLOTWIDGET_HPP
#define RWS_KINEMATICANALYSIS_KINEMATICANALYSISPLOTWIDGET_HPP

#include "KinematicAnalysisVisualizationTypes.hpp"

#include <QWidget>

namespace rws {

class KinematicAnalysisPlotWidget : public QWidget
{
    Q_OBJECT

  Q_SIGNALS:
    void visualPointClicked (rws::AnalysisVisualPoint point);

  public:
    explicit KinematicAnalysisPlotWidget (QWidget* parent = nullptr);

    void setVisualData (const AnalysisVisualData& data);
    void setProjection (VisualProjection projection);
    void setShowLabels (bool show);
    void setStatusFilters (bool showPass, bool showWarning, bool showFail,
                           bool showUnknown);
    void setShowGrid (bool show);
    void setShowLegend (bool show);
    void setPointRadius (double radius);

    QImage renderToImage (const QSize& size = QSize ()) const;

    QSize minimumSizeHint () const override;
    QSize sizeHint () const override;

  protected:
    void paintEvent (QPaintEvent* event) override;
    void mousePressEvent (QMouseEvent* event) override;
    bool event (QEvent* event) override;

  private:
    QRectF plotRect () const;
    bool pointVisible (const AnalysisVisualPoint& point) const;
    AnalysisVisualFilters filters () const;
    QPointF mapToPlot (const AnalysisVisualPoint& point, const QRectF& rect,
                       const QRectF& bounds) const;
    QRectF projectedBounds () const;
    bool visualPointAt (const QPoint& pos, AnalysisVisualPoint* hitPoint) const;
    QString pointTooltipAt (const QPoint& pos) const;
    void paintPlot (QPainter& painter, const QRect& area) const;
    void paintGrid (QPainter& painter, const QRectF& plotArea,
                    const QRectF& bounds) const;
    bool shouldPaintLegend (const QRect& area) const;
    int legendWidth (const QRect& area) const;
    void paintLegend (QPainter& painter, const QRectF& legendArea) const;

    AnalysisVisualData _data;
    VisualProjection _projection = VisualProjection::XY;
    bool _showLabels = false;
    bool _showPass = true;
    bool _showWarning = true;
    bool _showFail = true;
    bool _showUnknown = true;
    bool _showGrid = true;
    bool _showLegend = true;
    double _pointRadius = 4.5;
};

}    // namespace rws

#endif    // RWS_KINEMATICANALYSIS_KINEMATICANALYSISPLOTWIDGET_HPP
