#ifndef RWS_KINEMATICANALYSIS_KINEMATICANALYSISPLOTWIDGET_HPP
#define RWS_KINEMATICANALYSIS_KINEMATICANALYSISPLOTWIDGET_HPP

#include "KinematicAnalysisVisualizationTypes.hpp"

#include <QWidget>

namespace rws {

class KinematicAnalysisPlotWidget : public QWidget
{
  public:
    explicit KinematicAnalysisPlotWidget (QWidget* parent = nullptr);

    void setVisualData (const AnalysisVisualData& data);
    void setProjection (VisualProjection projection);
    void setShowLabels (bool show);
    void setStatusFilters (bool showPass, bool showWarning, bool showFail);

    QSize minimumSizeHint () const override;
    QSize sizeHint () const override;

  protected:
    void paintEvent (QPaintEvent* event) override;
    bool event (QEvent* event) override;

  private:
    QRectF plotRect () const;
    bool pointVisible (const AnalysisVisualPoint& point) const;
    QPointF mapToPlot (const AnalysisVisualPoint& point, const QRectF& rect,
                       const QRectF& bounds) const;
    QRectF projectedBounds () const;
    QString pointTooltipAt (const QPoint& pos) const;

    AnalysisVisualData _data;
    VisualProjection _projection = VisualProjection::XY;
    bool _showLabels = false;
    bool _showPass = true;
    bool _showWarning = true;
    bool _showFail = true;
};

}    // namespace rws

#endif    // RWS_KINEMATICANALYSIS_KINEMATICANALYSISPLOTWIDGET_HPP
