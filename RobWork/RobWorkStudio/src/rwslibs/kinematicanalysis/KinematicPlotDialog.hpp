// =============================================================================
//  KinematicPlotDialog:运动学分析结果的图表展示对话框
// =============================================================================
//
// 该对话框是"图表视图"的顶层容器,由 KinematicAnalysisWidget 管理。
// 它以无模式(modeless)窗口展示运动学分析结果的可视化图表,实际绘图工作
// 全部委托给内部持有的 KinematicAnalysisPlotWidget 控件完成。
//
// 职责:
//   - 提供投影(Projection)/标量模式(Scalar mode)/渲染模式(Render mode)
//     三个下拉框,让用户切换图表的观察维度;
//   - 提供 Fit(自动缩放)与 Export PNG(导出位图)按钮;
//   - 把用户操作以信号形式转发给 KinematicAnalysisWidget 处理,同时接收
//     KinematicAnalysisWidget 回写的显示状态(setDisplayState 单向同步)。
//
// 数据流:
//   KinematicAnalysisWidget --setVisualData/setDisplayState--> 本对话框
//   用户交互(下拉框/按钮) --*Requested 信号--> KinematicAnalysisWidget
//
// 线程安全:仅在 UI 线程使用,所有 setter/信号都在同一线程触发。
#ifndef RWS_KINEMATICANALYSIS_KINEMICPLOTDIALOG_HPP
#define RWS_KINEMATICANALYSIS_KINEMICPLOTDIALOG_HPP

#include "KinematicAnalysisVisualizationTypes.hpp"

#include <QDialog>

class QComboBox;

namespace rws {

class KinematicAnalysisPlotWidget;

// 无模式(modeless)视图,负责展示 KinematicAnalysisWidget 拥有的可视化数据。
// 与模态对话框不同,它不阻塞主窗口:用户可一边调整分析参数一边观察图表;
// 关闭后自动析构(WA_DeleteOnClose),调用方无需管理其生命周期。
//! Modeless view for the visualization owned by KinematicAnalysisWidget.
class KinematicPlotDialog : public QDialog
{
    Q_OBJECT

  public:
    explicit KinematicPlotDialog (QWidget* parent = nullptr);

    // 返回内部绘图控件指针(所有权仍归本对话框)。
    // 用途:外部需要直接操作绘图控件时使用,例如连接
    // KinematicAnalysisPlotWidget::visualPointClicked 信号以响应图表点选。
    KinematicAnalysisPlotWidget* plotWidget () const;
    // 返回最近一次 setVisualData 设置的可视化数据(只读引用)。
    // 该数据与绘图控件共享同一份内容,供统计/摘要等只读访问。
    const AnalysisVisualData& visualData () const;
    // 返回当前投影平面(XY/XZ/YZ),由 setDisplayState 同步维护。
    VisualProjection projection () const;
    // 返回当前网格显示开关,由 setDisplayState 同步维护。
    bool showGrid () const;

    // 整体替换图表数据(点集 + 标量模式 + 标量范围),并转发给绘图控件。
    // 数据为值语义复制:调用方传入的临时对象在本函数返回后可安全销毁。
    void setVisualData (const AnalysisVisualData& data);
    // 一次性同步完整显示状态:投影/标量模式/渲染模式/状态过滤/标签/
    // 网格/图例/点半径/长度单位。
    // 通常由 KinematicAnalysisWidget 在显示状态变化后调用,把外部状态
    // 镜像到本对话框内的下拉框与绘图控件。
    // 实现上用 QSignalBlocker 屏蔽下拉框信号,避免"外部回写"被误判为
    // 用户交互而反向发射 *Requested 信号,形成同步反馈循环。
    void setDisplayState (VisualProjection projection, VisualScalarMode scalarMode,
                          VisualRenderMode renderMode, const AnalysisVisualFilters& filters,
                          bool showLabels, bool showGrid, bool showLegend,
                          double pointRadius, KinematicLengthUnit lengthUnit);

  Q_SIGNALS:
    // 用户切换投影下拉框时发射,携带新投影枚举值,由 Widget 真正更新显示。
    void projectionRequested (rws::VisualProjection projection);
    // 用户切换标量模式下拉框时发射(状态/可操作度/条件数/覆盖率等)。
    void scalarModeRequested (rws::VisualScalarMode scalarMode);
    // 用户切换渲染模式下拉框时发射(散点/包络)。
    void renderModeRequested (rws::VisualRenderMode renderMode);
    // 用户点击 Fit 按钮时发射,请求绘图自动缩放以适合当前数据。
    void fitRequested ();
    // 用户点击 Export PNG 按钮时发射,请求把当前图表导出为 PNG 位图。
    void exportPngRequested ();

  private:
    // 填充投影下拉框:遍历 XY/XZ/YZ,以"可读文本 + 枚举整数值"作为 item。
    void addProjectionItems ();
    // 填充标量模式下拉框:遍历全部 VisualScalarMode 枚举。
    void addScalarModeItems ();
    // 填充渲染模式下拉框:散点(Scatter)/包络(Envelope)。
    void addRenderModeItems ();

    // 内部绘图控件:本对话框的所有绘图职责都委托给它,所有权随对话框析构。
    KinematicAnalysisPlotWidget* _plot;
    // 投影平面下拉框(XY/XZ/YZ)。
    QComboBox* _projectionCombo;
    // 标量模式下拉框(状态/可操作度/条件数/关节裕度/误差/碰撞/覆盖率)。
    QComboBox* _scalarModeCombo;
    // 渲染模式下拉框(散点/包络)。
    QComboBox* _renderModeCombo;
    // 最近一次通过 setVisualData 设置的图表数据副本(值语义,非指针共享)。
    AnalysisVisualData _data;
    // 当前投影平面,默认 XY;由 setDisplayState 同步维护。
    VisualProjection _projection = VisualProjection::XY;
    // 当前网格显示开关,默认显示;由 setDisplayState 同步维护。
    bool _showGrid = true;
};

}    // namespace rws

#endif    // RWS_KINEMATICANALYSIS_KINEMICPLOTDIALOG_HPP
