// =============================================================================
//  KinematicPlotDialog 的实现
// =============================================================================
//
// 构造函数完成四件事:
//   1) 创建绘图控件与三个下拉框(投影/标量模式/渲染模式);
//   2) 向三个下拉框填充对应枚举项;
//   3) 组装布局:顶部控制栏(下拉框 + Fit/Export 按钮)+ 中部绘图区;
//   4) 连接信号:下拉框切换/按钮点击 -> 对应 *Requested 信号。
//
// 说明:
//   - 三个下拉框以枚举整数值作为 itemData,切换时用
//     currentData().toInt() 还原枚举,再经 static_cast 强转发射信号;
//   - 外部通过 setDisplayState 同步状态时,用 QSignalBlocker 屏蔽
//     下拉框的 currentIndexChanged,避免形成"同步 <-> 信号"反馈循环。
#include "KinematicPlotDialog.hpp"

#include "KinematicAnalysisPlotWidget.hpp"

#include <QComboBox>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace rws {

// 构造函数:创建绘图控件与三个下拉框,填充枚举项,组装布局并连接信号。
// 窗口标志说明:
//   - Qt::Window:使对话框成为独立的顶层窗口;
//   - Qt::WindowStaysOnTopHint:保持在其它窗口之上,便于边调参边看图;
//   - WA_DeleteOnClose:关闭时自动析构,调用方不应手动 delete 对话框指针。
KinematicPlotDialog::KinematicPlotDialog (QWidget* parent) :
    QDialog (parent),
    _plot (new KinematicAnalysisPlotWidget (this)),
    _projectionCombo (new QComboBox (this)),
    _scalarModeCombo (new QComboBox (this)),
    _renderModeCombo (new QComboBox (this))
{
    setObjectName (QStringLiteral ("kinematicPlotDialog"));
    setWindowTitle (tr("Kinematic Plot"));
    setWindowFlags (Qt::Window | Qt::WindowStaysOnTopHint);
    setAttribute (Qt::WA_DeleteOnClose, true);
    resize (800, 600);

    addProjectionItems ();
    addScalarModeItems ();
    addRenderModeItems ();
    _projectionCombo->setObjectName (QStringLiteral ("plotProjectionCombo"));
    _scalarModeCombo->setObjectName (QStringLiteral ("plotScalarModeCombo"));
    _renderModeCombo->setObjectName (QStringLiteral ("plotRenderModeCombo"));

    QVBoxLayout* layout = new QVBoxLayout (this);
    QGridLayout* controls = new QGridLayout ();
    controls->addWidget (new QLabel (tr("Projection"), this), 0, 0);
    controls->addWidget (_projectionCombo, 0, 1);
    controls->addWidget (new QLabel (tr("Scalar mode"), this), 0, 2);
    controls->addWidget (_scalarModeCombo, 0, 3);
    controls->addWidget (new QLabel (tr("Render mode"), this), 1, 0);
    controls->addWidget (_renderModeCombo, 1, 1);

    QPushButton* fitButton = new QPushButton (tr("Fit"), this);
    fitButton->setObjectName (QStringLiteral ("plotFitButton"));
    QPushButton* exportButton = new QPushButton (tr("Export PNG"), this);
    exportButton->setObjectName (QStringLiteral ("plotExportPngButton"));
    controls->addWidget (fitButton, 1, 2);
    controls->addWidget (exportButton, 1, 3);
    controls->setColumnStretch (1, 1);
    controls->setColumnStretch (3, 1);
    controls->setColumnStretch (5, 1);
    layout->addLayout (controls);
    _plot->setSizePolicy (QSizePolicy::Expanding, QSizePolicy::Expanding);
    layout->addWidget (_plot, 1);

    // 三个下拉框的切换信号 -> 各自 *Requested 信号。
    // 使用显式函数指针取重载地址,避免 currentIndexChanged(int) 与
    // currentIndexChanged(const QString&) 两个重载的歧义;lambda 内部把
    // itemData(枚举整数值)还原并强转为枚举后发射。
    connect (_projectionCombo, static_cast< void (QComboBox::*) (int) > (
                 &QComboBox::currentIndexChanged), this, [this] (int) {
        Q_EMIT projectionRequested (static_cast< VisualProjection > (
            _projectionCombo->currentData ().toInt ()));
    });
    connect (_scalarModeCombo, static_cast< void (QComboBox::*) (int) > (
                 &QComboBox::currentIndexChanged), this, [this] (int) {
        Q_EMIT scalarModeRequested (static_cast< VisualScalarMode > (
            _scalarModeCombo->currentData ().toInt ()));
    });
    connect (_renderModeCombo, static_cast< void (QComboBox::*) (int) > (
                 &QComboBox::currentIndexChanged), this, [this] (int) {
        Q_EMIT renderModeRequested (static_cast< VisualRenderMode > (
            _renderModeCombo->currentData ().toInt ()));
    });
    // 按钮点击 -> Fit / Export PNG 请求信号。
    connect (fitButton, &QPushButton::clicked, this, &KinematicPlotDialog::fitRequested);
    connect (exportButton, &QPushButton::clicked, this,
             &KinematicPlotDialog::exportPngRequested);
}

// 返回绘图控件指针。控件所有权归本对话框,外部只读访问,
// 典型用途是连接 visualPointClicked 信号以响应图表点选。
KinematicAnalysisPlotWidget* KinematicPlotDialog::plotWidget () const
{
    return _plot;
}

// 返回最近一次 setVisualData 存储的数据副本(只读)。
const AnalysisVisualData& KinematicPlotDialog::visualData () const
{
    return _data;
}

// 返回当前投影平面(由 setDisplayState 最近一次传入值维护)。
VisualProjection KinematicPlotDialog::projection () const
{
    return _projection;
}

// 返回当前网格显示开关(由 setDisplayState 最近一次传入值维护)。
bool KinematicPlotDialog::showGrid () const
{
    return _showGrid;
}

// 整体替换图表数据:先复制到成员 _data,再转发给绘图控件重绘。
// 值语义复制,调用方与对话框互不共享数据所有权。
void KinematicPlotDialog::setVisualData (const AnalysisVisualData& data)
{
    _data = data;
    _plot->setVisualData (_data);
}

// 一次性同步完整显示状态到下拉框与绘图控件。
// 关键实现细节:先对三个下拉框构造 QSignalBlocker,屏蔽其
// currentIndexChanged 信号——否则 setCurrentIndex 会把"外部状态回写"
// 误判为用户交互,进而发射 *Requested 信号,与外部形成反馈循环。
// 其余参数(过滤/标签/网格/图例/点半径/长度单位)直接转发给绘图控件。
void KinematicPlotDialog::setDisplayState (
    VisualProjection projection, VisualScalarMode scalarMode, VisualRenderMode renderMode,
    const AnalysisVisualFilters& filters, bool showLabels, bool showGrid, bool showLegend,
    double pointRadius, KinematicLengthUnit lengthUnit)
{
    _projection = projection;
    _showGrid = showGrid;
    const QSignalBlocker projectionBlocker (_projectionCombo);
    const QSignalBlocker scalarBlocker (_scalarModeCombo);
    const QSignalBlocker renderBlocker (_renderModeCombo);
    _projectionCombo->setCurrentIndex (_projectionCombo->findData (static_cast<int> (projection)));
    _scalarModeCombo->setCurrentIndex (_scalarModeCombo->findData (static_cast<int> (scalarMode)));
    _renderModeCombo->setCurrentIndex (_renderModeCombo->findData (static_cast<int> (renderMode)));
    _plot->setProjection (projection);
    _plot->setStatusFilters (filters.showPass, filters.showWarning, filters.showFail,
                             filters.showUnknown);
    _plot->setShowLabels (showLabels);
    _plot->setShowGrid (showGrid);
    _plot->setShowLegend (showLegend);
    _plot->setPointRadius (pointRadius);
    _plot->setRenderMode (renderMode);
    _plot->setLengthUnit (lengthUnit);
}

// 填充投影下拉框:遍历 XY/XZ/YZ 三种投影,
// 使用 visualProjectionText 可读文本 + 枚举整数值作为 item data。
void KinematicPlotDialog::addProjectionItems ()
{
    for (VisualProjection projection : {VisualProjection::XY, VisualProjection::XZ,
                                        VisualProjection::YZ}) {
        _projectionCombo->addItem (visualProjectionText (projection),
                                   static_cast<int> (projection));
    }
}

// 填充标量模式下拉框:遍历全部 VisualScalarMode(状态/可操作度/条件数/
// 最小关节裕度/位置误差/姿态误差/碰撞/覆盖率)。
// 注意:某标量模式是否支持取决于当前数据源类型,支持性判断由上层 Widget
// 负责,此处仅提供完整的可选项。
void KinematicPlotDialog::addScalarModeItems ()
{
    for (VisualScalarMode mode : {VisualScalarMode::Status, VisualScalarMode::Manipulability,
                                  VisualScalarMode::Condition, VisualScalarMode::MinJointMargin,
                                  VisualScalarMode::PositionError,
                                  VisualScalarMode::OrientationError,
                                  VisualScalarMode::Collision, VisualScalarMode::Coverage}) {
        _scalarModeCombo->addItem (visualScalarModeText (mode), static_cast<int> (mode));
    }
}

// 填充渲染模式下拉框:散点(Scatter)与包络(Envelope)两种渲染模式。
void KinematicPlotDialog::addRenderModeItems ()
{
    for (VisualRenderMode mode : {VisualRenderMode::Scatter, VisualRenderMode::Envelope}) {
        _renderModeCombo->addItem (visualRenderModeText (mode), static_cast<int> (mode));
    }
}

}    // namespace rws
