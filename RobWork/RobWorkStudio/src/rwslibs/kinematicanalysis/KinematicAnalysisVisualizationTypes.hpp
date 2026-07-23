#ifndef RWS_KINEMATICANALYSIS_KINEMATICANALYSISVISUALIZATIONTYPES_HPP
#define RWS_KINEMATICANALYSIS_KINEMATICANALYSISVISUALIZATIONTYPES_HPP

#include "KinematicAnalysisTypes.hpp"

#include <QColor>
#include <QPointF>
#include <QRect>
#include <QRectF>
#include <QSizeF>
#include <QString>

#include <array>
#include <vector>

namespace rws {

enum class VisualPointSource
{
    TaskPoint,
    Workspace,
    PoseReachability
};

enum class VisualScalarMode
{
    Status,
    Manipulability,
    Condition,
    MinJointMargin,
    PositionError,
    OrientationError,
    Collision,
    Coverage
};

enum class VisualProjection
{
    XY,
    XZ,
    YZ
};

enum class VisualRenderMode
{
    Scatter,
    Envelope
};

struct AnalysisEnvelopeData
{
    bool valid = false;
    VisualProjection projection = VisualProjection::XY;
    std::vector< QPointF > boundary;
    QPointF origin = QPointF (0.0, 0.0);
    double minX = 0.0;
    double maxX = 0.0;
    double minY = 0.0;
    double maxY = 0.0;
    double width = 0.0;
    double height = 0.0;
    double maxRadius = 0.0;
};

struct EnvelopePlotLayout
{
    bool valid = false;
    QRectF plotRect;
    QRectF titleRect;
    QPointF widthLineStart;
    QPointF widthLineEnd;
    QRectF widthLabelRect;
    QPointF heightLineStart;
    QPointF heightLineEnd;
    QRectF heightLabelRect;
    QRectF captionRect;
};

struct AnalysisVisualPoint
{
    std::array< double, 3 > position = {{0.0, 0.0, 0.0}};
    AnalysisStatus status = AnalysisStatus::Unknown;
    double scalar = 0.0;
    bool hasFiniteScalar = false;
    bool inCollision = false;
    QString label;
    QString tooltip;
    VisualPointSource source = VisualPointSource::TaskPoint;
    int sourceIndex = -1;
    bool hasQ = false;
    std::vector< double > q;
};

struct AnalysisVisualData
{
    std::vector< AnalysisVisualPoint > points;
    VisualScalarMode scalarMode = VisualScalarMode::Status;
    VisualRenderMode renderMode = VisualRenderMode::Scatter;
    AnalysisEnvelopeData envelope;
    bool hasFiniteScalar = false;
    double scalarMin = 0.0;
    double scalarMax = 0.0;
};

// 从三种数据源转换为可视化点(包含标量计算和 tooltip)。
//   - visualDataFromTaskPointResults       :任务点可达性 → 可视化点
//   - visualDataFromWorkspaceSamples       :工作空间采样 → 可视化点
//   - visualDataFromPoseReachabilitySamples:位姿可达性 → 可视化点
// 每种转换都会:
//   1) 从各 source 的对应字段填充 AnalysisVisualPoint(位置/状态/标量/碰撞等);
//   2) 填充 label 和 tooltip(包含位置、标量值、Replay Q 信息等);
//   3) 对于 TaskPoint 和 PoseReachability,填充 representative Q(replay)。
AnalysisVisualData visualDataFromTaskPointResults (
    const std::vector< TaskPointReachabilityResult >& results,
    VisualScalarMode scalarMode);

AnalysisVisualData visualDataFromWorkspaceSamples (
    const std::vector< WorkspaceSample >& samples,
    VisualScalarMode scalarMode);

AnalysisVisualData visualDataFromPoseReachabilitySamples (
    const std::vector< PoseReachabilitySample >& samples,
    VisualScalarMode scalarMode);

// 3D 点按选定平面投影为 2D 屏幕坐标(纯坐标转换,无状态依赖)。
//   XY 投影 → (x, y);
//   XZ 投影 → (x, z);
//   YZ 投影 → (y, z)。
QPointF projectVisualPoint (const AnalysisVisualPoint& point,
                            VisualProjection projection);

// 图例 / 着色区可见性辅助(供绘图控件布局):
//   visualLegendVisible :在给定宽度下是否显示图例(< 480 像素隐藏);
//   visualLegendWidth   :图例在 widget 中的固定宽度(像素);
//   visualPlotArea      :从 widget 区域扣除图例/边距后,绘图可用矩形。
bool visualLegendVisible (bool showLegend, const QRect& area);
int visualLegendWidth (bool showLegend, const QRect& area);
QRectF visualPlotArea (const QRect& area, bool showLegend);

// 枚举 → 可读字符串(用于轴标签、图例标题、tooltip 等)。
QString visualScalarModeText (VisualScalarMode mode);
QString visualProjectionText (VisualProjection projection);
QString visualRenderModeText (VisualRenderMode mode);
void updateEnvelopeDimensions (AnalysisEnvelopeData& envelope);
EnvelopePlotLayout computeEnvelopePlotLayout (
    const QRect& area,
    const QSizeF& titleSize,
    const QSizeF& widthLabelSize,
    const QSizeF& heightLabelSize,
    const QSizeF& captionSize);

// 根据 scalarMode 和 data 范围返回该点的颜色(Status/Collision 离散,其它连续)。
//   Status/Coverage :按 status/collision 直接取固定调色板;
//   连续标量         :用 (scalar - scalarMin) / (scalarMax - scalarMin) 归一化
//                     后插入到 [冷-暖] 渐变(蓝→绿→红)。
// 输入 scalar 非有限或 hasFiniteScalar == false 时,使用中位色(0.5)。
QColor visualColorForPoint (const AnalysisVisualPoint& point,
                            const AnalysisVisualData& data);

// 枚举 → 可读字符串(用于轴标签、图例标题、tooltip 等)。
QString visualScalarModeText (VisualScalarMode mode);
QString visualProjectionText (VisualProjection projection);
QColor visualColorForPoint (const AnalysisVisualPoint& point,
                            const AnalysisVisualData& data);

// ---- New helper structs and helpers in this phase ----

struct AnalysisVisualStatusSummary
{
    std::size_t totalCount = 0;
    std::size_t visibleCount = 0;
    std::size_t passCount = 0;
    std::size_t warningCount = 0;
    std::size_t failCount = 0;
    std::size_t unknownCount = 0;
    std::size_t collisionCount = 0;
};

struct AnalysisVisualBounds
{
    bool valid = false;
    double minX = 0.0;
    double maxX = 0.0;
    double minY = 0.0;
    double maxY = 0.0;
};

struct AnalysisVisualFilters
{
    bool showPass = true;
    bool showWarning = true;
    bool showFail = true;
    bool showUnknown = true;
};

QString visualPointSourceText (VisualPointSource source);
std::vector< VisualScalarMode > supportedVisualScalarModes (
    VisualPointSource source);
bool visualScalarModeSupported (VisualPointSource source,
                                VisualScalarMode mode);
VisualScalarMode defaultVisualScalarModeForSource (
    VisualPointSource source);
AnalysisVisualStatusSummary summarizeVisualData (
    const AnalysisVisualData& data,
    const AnalysisVisualFilters& filters);
AnalysisVisualBounds projectedVisualBounds (
    const AnalysisVisualData& data,
    VisualProjection projection,
    const AnalysisVisualFilters& filters);

}    // namespace rws

#endif    // RWS_KINEMATICANALYSIS_KINEMATICANALYSISVISUALIZATIONTYPES_HPP
