#ifndef RWS_KINEMATICANALYSIS_KINEMATICANALYSISVISUALIZATIONTYPES_HPP
#define RWS_KINEMATICANALYSIS_KINEMATICANALYSISVISUALIZATIONTYPES_HPP

#include "KinematicAnalysisTypes.hpp"

#include <QColor>
#include <QPointF>
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
};

struct AnalysisVisualData
{
    std::vector< AnalysisVisualPoint > points;
    VisualScalarMode scalarMode = VisualScalarMode::Status;
    bool hasFiniteScalar = false;
    double scalarMin = 0.0;
    double scalarMax = 0.0;
};

AnalysisVisualData visualDataFromTaskPointResults (
    const std::vector< TaskPointReachabilityResult >& results,
    VisualScalarMode scalarMode);

AnalysisVisualData visualDataFromWorkspaceSamples (
    const std::vector< WorkspaceSample >& samples,
    VisualScalarMode scalarMode);

AnalysisVisualData visualDataFromPoseReachabilitySamples (
    const std::vector< PoseReachabilitySample >& samples,
    VisualScalarMode scalarMode);

QPointF projectVisualPoint (const AnalysisVisualPoint& point,
                            VisualProjection projection);

QString visualScalarModeText (VisualScalarMode mode);
QString visualProjectionText (VisualProjection projection);
QColor visualColorForPoint (const AnalysisVisualPoint& point,
                            const AnalysisVisualData& data);

}    // namespace rws

#endif    // RWS_KINEMATICANALYSIS_KINEMATICANALYSISVISUALIZATIONTYPES_HPP
