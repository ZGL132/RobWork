#ifndef RWS_KINEMATICANALYSIS_KINEMATICANALYSISPLOTWIDGET_HPP
#define RWS_KINEMATICANALYSIS_KINEMATICANALYSISPLOTWIDGET_HPP

#include "KinematicAnalysisVisualizationTypes.hpp"

#include <QWidget>

namespace rws {

// =============================================================================
//  KinematicAnalysisPlotWidget:2D 散点图绘图控件
// =============================================================================
//
// 这是纯绘图控件,只负责"画"和"接收点击",不承担 RobWorkStudio 状态管理。
// 架构分层:
//   - 输入数据:AnalysisVisualData(setVisualData)
//   - 输出:visualPointClicked(point) 信号 → 由 KinematicAnalysisWidget 处理
//   - 配置:投影/状态过滤/网格/图例/点大小 setter
//   - 渲染:绘图被拆成 paintPlot → paintGrid → paintLegend 三个子函数
//
// 线程安全:仅在 UI 线程使用(所有 setter/渲染都是同一线程调用)。
class KinematicAnalysisPlotWidget : public QWidget
{
    Q_OBJECT

    // =================================================================
    //  信号
    // =================================================================
    //
    // visualPointClicked:左键命中可见点时发射,point 是被命中的可视化点副本。
    // 注:经过点击检测(visualPointAt),dist ≤ 9 像素才算命中。
    // 注:仅在 LeftButton 触发,其它按钮会忽略。
    Q_SIGNALS:
    void visualPointClicked (rws::AnalysisVisualPoint point);

  public:
    explicit KinematicAnalysisPlotWidget (QWidget* parent = nullptr);

    // ===================================================================
    //  数据与配置 setter
    // ===================================================================

    // 替换可视化数据(包含点集、标量模式、scalarMin/Max 范围)。
    // 调用后触发 update() 重绘。
    void setVisualData (const AnalysisVisualData& data);

    // 切换投影平面(XY/XZ/YZ)。
    void setProjection (VisualProjection projection);

    // 是否绘制点旁的 label 文本。
    void setShowLabels (bool show);

    // 设置状态过滤开关。每个状态对应一个 bool:
    //   showPass/showWarning/showFail/showUnknown
    // 关闭时该状态的所有点都不参与绘制和点击命中。
    void setStatusFilters (bool showPass, bool showWarning, bool showFail,
                           bool showUnknown);

    // 切换绘图区域内的网格 + 刻度数字。
    void setShowGrid (bool show);

    // 切换右侧/底部的图例(状态色块或标量色带)。
    void setShowLegend (bool show);

    // 散点半径(像素)。当前实现还会按数据规模自动缩放(> 1000 → ≤ 3.5;> 5000 → ≤ 2.5)。
    void setPointRadius (double radius);

    // ===================================================================
    //  渲染到 QImage(用于 Export PNG)
    // ===================================================================
    //
    // 模拟屏幕 paintEvent 的过程在指定 size 的 QImage 上执行 paintPlot。
    // 默认 size 为 1200×800,适合插入 PDF/报告。
    // 由于 paintPlot 接受 QRect 参数,这里传入完整 image 区域;
    // 因此 PNG 导出会按指定 size 正确布局,不会受 widget 当前尺寸影响。
    QImage renderToImage (const QSize& size = QSize ()) const;

    // ===================================================================
    //  QWidget 标准接口
    // ===================================================================

    QSize minimumSizeHint () const override;
    QSize sizeHint () const override;

  protected:
    // 标准 QPainter 重绘入口:创建 QPainter 调 paintPlot(rect())。
    void paintEvent (QPaintEvent* event) override;

    // 鼠标点击事件:左键时做点击命中测试,命中则发射 visualPointClicked(point)。
    // 不是 Plot 控件的职责去处理点击后的应用 — 由 Widget 拿到信号后处理。
    void mousePressEvent (QMouseEvent* event) override;

    // 重写 event 拦截 QEvent::ToolTip:命中可视化点时显示点 tooltip。
    // 其它事件透传给 QWidget::event。
    bool event (QEvent* event) override;

  private:
    // ===================================================================
    //  内部布局与命中检测
    // ===================================================================

    // 绘图可用矩形(扣除边距 + 图例)。
    QRectF plotRect () const;

    // 给定一个点,根据状态过滤决定是否参与绘制/命中。
    bool pointVisible (const AnalysisVisualPoint& point) const;

    // 把 _show* 系列布尔打包成 AnalysisVisualFilters,供纯 helper 使用。
    AnalysisVisualFilters filters () const;

    // 把数据点从数据坐标映射到绘图区屏幕坐标(等距投影 + 缩放)。
    QPointF mapToPlot (const AnalysisVisualPoint& point, const QRectF& rect,
                       const QRectF& bounds) const;

    // 计算可见点的投影边界(供 paintPlot 自动 fit)。
    QRectF projectedBounds () const;

    // 在 pos 处做点击命中检测,返回 true 表示命中,并把命中的点写入 hitPoint。
    bool visualPointAt (const QPoint& pos, AnalysisVisualPoint* hitPoint) const;

    // 返回 pos 处点的 tooltip 文本(若未命中则返回空字符串)。
    QString pointTooltipAt (const QPoint& pos) const;

    // ===================================================================
    //  渲染流水线(供 paintEvent 和 renderToImage 共享)
    // ===================================================================

    // 主绘图入口:按"边框 → 网格 → 点 → 图例 → 轴标签 → info 文本"顺序绘制。
    void paintPlot (QPainter& painter, const QRect& area) const;

    // 绘制 5 等分刻度虚线网格 + 数字 tick 标签。
    void paintGrid (QPainter& painter, const QRectF& plotArea,
                    const QRectF& bounds) const;

    // 绘制图例:状态色块 / Collision 图例 / 标量色带,根据 scalarMode 切换。
    void paintLegend (QPainter& painter, const QRectF& legendArea) const;

    // ===================================================================
    //  私有状态
    // ===================================================================

    AnalysisVisualData _data;                       // 当前绘制的数据
    VisualProjection _projection = VisualProjection::XY;  // 当前投影平面
    bool _showLabels  = false;                      // 是否显示 label
    bool _showPass    = true;                       // 显示 Pass 点
    bool _showWarning = true;                       // 显示 Warning 点
    bool _showFail    = true;                       // 显示 Fail 点
    bool _showUnknown = true;                       // 显示 Unknown 点
    bool _showGrid    = true;                       // 显示网格 + 刻度
    bool _showLegend  = true;                       // 显示图例/标量色带
    double _pointRadius = 4.5;                      // 散点半径(像素)
};

}    // namespace rws

#endif    // RWS_KINEMATICANALYSIS_KINEMATICANALYSISPLOTWIDGET_HPP
