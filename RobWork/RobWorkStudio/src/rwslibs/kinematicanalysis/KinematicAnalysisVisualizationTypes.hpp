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
    bool hasQ = false;
    std::vector< double > q;
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
