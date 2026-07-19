#include "KinematicAnalysisVisualizationTypes.hpp"

#include <QStringList>

#include <algorithm>
#include <cmath>
#include <limits>

using namespace rws;

namespace {

QString statusTextLocal (AnalysisStatus status)
{
    switch (status) {
        case AnalysisStatus::Pass:    return QStringLiteral ("Pass");
        case AnalysisStatus::Warning: return QStringLiteral ("Warning");
        case AnalysisStatus::Fail:    return QStringLiteral ("Fail");
        case AnalysisStatus::Unknown:
        default:                      return QStringLiteral ("Unknown");
    }
}

double statusScalar (AnalysisStatus status)
{
    switch (status) {
        case AnalysisStatus::Pass:    return 1.0;
        case AnalysisStatus::Warning: return 0.5;
        case AnalysisStatus::Fail:    return 0.0;
        case AnalysisStatus::Unknown:
        default:                      return std::numeric_limits< double >::quiet_NaN ();
    }
}

bool finiteScalar (double value, double* out)
{
    if (!std::isfinite (value))
        return false;
    if (out != nullptr)
        *out = value;
    return true;
}

const KinematicIkSolution* bestUsableSolution (
    const KinematicIkAnalysisResult& ik)
{
    const KinematicIkSolution* best = nullptr;
    for (const KinematicIkSolution& solution : ik.solutions) {
        if (solution.status == AnalysisStatus::Fail || solution.inCollision)
            continue;
        if (best == nullptr || solution.score < best->score)
            best = &solution;
    }
    return best;
}

bool hasCollisionCandidate (const KinematicIkAnalysisResult& ik)
{
    for (const KinematicIkSolution& solution : ik.solutions) {
        if (solution.inCollision)
            return true;
    }
    return false;
}

QString failureReasonsText (const std::vector< KinematicFailureReason >& reasons)
{
    if (reasons.empty ())
        return QStringLiteral ("-");
    QStringList parts;
    for (KinematicFailureReason reason : reasons)
        parts << QString::fromLatin1 (toString (reason));
    return parts.join (QStringLiteral (", "));
}

bool taskScalar (const TaskPointReachabilityResult& result,
                 VisualScalarMode mode, double* value)
{
    const KinematicIkSolution* best = bestUsableSolution (result.ik);
    switch (mode) {
        case VisualScalarMode::Status:
            return finiteScalar (statusScalar (result.status), value);
        case VisualScalarMode::Manipulability:
            return best != nullptr && finiteScalar (best->manipulability, value);
        case VisualScalarMode::Condition:
            return best != nullptr && finiteScalar (best->conditionNumber, value);
        case VisualScalarMode::MinJointMargin:
            return best != nullptr && finiteScalar (best->minJointLimitMargin, value);
        case VisualScalarMode::PositionError:
            return best != nullptr && finiteScalar (best->positionErrorMeters, value);
        case VisualScalarMode::OrientationError:
            return best != nullptr && finiteScalar (best->orientationErrorDeg, value);
        case VisualScalarMode::Collision:
            if (value != nullptr)
                *value = (best != nullptr ? best->inCollision :
                          hasCollisionCandidate (result.ik)) ? 1.0 : 0.0;
            return true;
        case VisualScalarMode::Coverage:
        default:
            return false;
    }
}

bool workspaceScalar (const WorkspaceSample& sample,
                      VisualScalarMode mode, double* value)
{
    switch (mode) {
        case VisualScalarMode::Status:
            return finiteScalar (statusScalar (sample.status), value);
        case VisualScalarMode::Manipulability:
            return finiteScalar (sample.manipulability, value);
        case VisualScalarMode::Condition:
            return finiteScalar (sample.conditionNumber, value);
        case VisualScalarMode::MinJointMargin:
            return finiteScalar (sample.minJointLimitMargin, value);
        case VisualScalarMode::Collision:
            if (value != nullptr)
                *value = sample.inCollision ? 1.0 : 0.0;
            return true;
        case VisualScalarMode::PositionError:
        case VisualScalarMode::OrientationError:
        case VisualScalarMode::Coverage:
        default:
            return false;
    }
}

bool poseScalar (const PoseReachabilitySample& sample,
                 VisualScalarMode mode, double* value)
{
    switch (mode) {
        case VisualScalarMode::Status:
            return finiteScalar (statusScalar (sample.status), value);
        case VisualScalarMode::Coverage:
            return finiteScalar (sample.coverage, value);
        case VisualScalarMode::Manipulability:
        case VisualScalarMode::Condition:
        case VisualScalarMode::MinJointMargin:
        case VisualScalarMode::PositionError:
        case VisualScalarMode::OrientationError:
        case VisualScalarMode::Collision:
        default:
            return false;
    }
}

void updateRange (AnalysisVisualData& data)
{
    bool first = true;
    for (const AnalysisVisualPoint& point : data.points) {
        if (!point.hasFiniteScalar)
            continue;
        if (first) {
            data.scalarMin = point.scalar;
            data.scalarMax = point.scalar;
            first = false;
        }
        else {
            data.scalarMin = std::min (data.scalarMin, point.scalar);
            data.scalarMax = std::max (data.scalarMax, point.scalar);
        }
    }
    data.hasFiniteScalar = !first;
}

double normalizedScalar (const AnalysisVisualPoint& point,
                         const AnalysisVisualData& data)
{
    if (!point.hasFiniteScalar || !data.hasFiniteScalar)
        return 0.5;
    const double span = data.scalarMax - data.scalarMin;
    if (!std::isfinite (span) || std::fabs (span) < 1e-12)
        return 0.5;
    const double t = (point.scalar - data.scalarMin) / span;
    return std::max (0.0, std::min (1.0, t));
}

}    // namespace

AnalysisVisualData rws::visualDataFromTaskPointResults (
    const std::vector< TaskPointReachabilityResult >& results,
    VisualScalarMode scalarMode)
{
    AnalysisVisualData data;
    data.scalarMode = scalarMode;
    data.points.reserve (results.size ());
    for (std::size_t i = 0; i < results.size (); ++i) {
        const TaskPointReachabilityResult& result = results[i];
        AnalysisVisualPoint point;
        point.position = result.taskPoint.position;
        point.status = result.status;
        point.inCollision = hasCollisionCandidate (result.ik);
        point.source = VisualPointSource::TaskPoint;
        point.sourceIndex = static_cast< int > (i);
        point.label = !result.taskPoint.id.empty () ?
            QString::fromStdString (result.taskPoint.id) :
            (!result.taskPoint.name.empty () ?
                 QString::fromStdString (result.taskPoint.name) :
                 QStringLiteral ("TP%1").arg (static_cast< int > (i + 1)));
        point.hasFiniteScalar = taskScalar (result, scalarMode, &point.scalar);
        {
            const KinematicIkSolution* best = bestUsableSolution (result.ik);
            QString extra;
            if (best != nullptr) {
                point.hasQ = !best->q.empty ();
                point.q = best->q;
                extra = QStringLiteral (
                    "\nBest Q index: %1\nManipulability: %2\nCondition: %3\n"
                    "Position error: %4 m\nOrientation error: %5 deg")
                    .arg (point.sourceIndex)
                    .arg (QString::number (best->manipulability, 'g', 6))
                    .arg (QString::number (best->conditionNumber, 'g', 6))
                    .arg (QString::number (best->positionErrorMeters, 'g', 6))
                    .arg (QString::number (best->orientationErrorDeg, 'g', 6));
                extra += QStringLiteral ("\nReplay Q: %1")
                    .arg (point.hasQ ? QStringLiteral ("Yes") : QStringLiteral ("No"));
            }
            point.tooltip = QStringLiteral (
                "Task point: %1\nStatus: %2\nReason: %3\n"
                "Position: %4, %5, %6 m\nScalar: %7 = %8%9")
                .arg (point.label)
                .arg (statusTextLocal (result.status))
                .arg (failureReasonsText (result.failureReasons))
                .arg (QString::number (point.position[0], 'g', 6))
                .arg (QString::number (point.position[1], 'g', 6))
                .arg (QString::number (point.position[2], 'g', 6))
                .arg (visualScalarModeText (scalarMode))
                .arg (point.hasFiniteScalar ? QString::number (point.scalar, 'g', 6)
                                            : QStringLiteral ("-"))
                .arg (extra);
        }
        data.points.push_back (point);
    }
    updateRange (data);
    return data;
}

AnalysisVisualData rws::visualDataFromWorkspaceSamples (
    const std::vector< WorkspaceSample >& samples,
    VisualScalarMode scalarMode)
{
    AnalysisVisualData data;
    data.scalarMode = scalarMode;
    data.points.reserve (samples.size ());
    for (std::size_t i = 0; i < samples.size (); ++i) {
        const WorkspaceSample& sample = samples[i];
        AnalysisVisualPoint point;
        point.position = sample.tcpPosition;
        point.status = sample.status;
        point.inCollision = sample.inCollision;
        point.source = VisualPointSource::Workspace;
        point.sourceIndex = static_cast< int > (i);
        point.hasQ = !sample.q.empty ();
        point.q = sample.q;
        point.label = QStringLiteral ("W%1").arg (static_cast< int > (i));
        point.hasFiniteScalar = workspaceScalar (sample, scalarMode, &point.scalar);
        point.tooltip = QStringLiteral (
            "Workspace sample: %1\nStatus: %2\n"
            "TCP: %3, %4, %5 m\nScalar: %6 = %7\n"
            "Manipulability: %8\nCondition: %9\nMin margin: %10\nCollision: %11\n"
            "Replay Q: %12")
            .arg (point.sourceIndex)
            .arg (statusTextLocal (sample.status))
            .arg (QString::number (point.position[0], 'g', 6))
            .arg (QString::number (point.position[1], 'g', 6))
            .arg (QString::number (point.position[2], 'g', 6))
            .arg (visualScalarModeText (scalarMode))
            .arg (point.hasFiniteScalar ? QString::number (point.scalar, 'g', 6)
                                        : QStringLiteral ("-"))
            .arg (QString::number (sample.manipulability, 'g', 6))
            .arg (QString::number (sample.conditionNumber, 'g', 6))
            .arg (QString::number (sample.minJointLimitMargin, 'g', 6))
            .arg (sample.inCollision ? QStringLiteral ("Yes") : QStringLiteral ("No"))
            .arg (point.hasQ ? QStringLiteral ("Yes") : QStringLiteral ("No"));
        data.points.push_back (point);
    }
    updateRange (data);
    return data;
}

AnalysisVisualData rws::visualDataFromPoseReachabilitySamples (
    const std::vector< PoseReachabilitySample >& samples,
    VisualScalarMode scalarMode)
{
    AnalysisVisualData data;
    data.scalarMode = scalarMode;
    data.points.reserve (samples.size ());
    for (std::size_t i = 0; i < samples.size (); ++i) {
        const PoseReachabilitySample& sample = samples[i];
        AnalysisVisualPoint point;
        point.position = sample.position;
        point.status = sample.status;
        point.source = VisualPointSource::PoseReachability;
        point.sourceIndex = static_cast< int > (i);
        point.hasQ = sample.hasRepresentativeQ && !sample.representativeQ.empty ();
        point.q = point.hasQ ? sample.representativeQ : std::vector< double > ();
        point.label = QStringLiteral ("P%1").arg (static_cast< int > (i));
        point.hasFiniteScalar = poseScalar (sample, scalarMode, &point.scalar);
        const QString reprText = sample.hasRepresentativeQ ?
            QStringLiteral ("\nReplay Q: Yes\nRepresentative direction: %1\nRepresentative roll: %2")
                .arg (sample.representativeDirectionIndex)
                .arg (sample.representativeRollIndex) :
            QStringLiteral ("\nReplay Q: No");
        point.tooltip = QStringLiteral (
            "Pose reachability: %1\nStatus: %2\n"
            "Position: %3, %4, %5 m\nScalar: %6 = %7\n"
            "Reachable: %8 / %9%10")
            .arg (point.sourceIndex)
            .arg (statusTextLocal (sample.status))
            .arg (QString::number (point.position[0], 'g', 6))
            .arg (QString::number (point.position[1], 'g', 6))
            .arg (QString::number (point.position[2], 'g', 6))
            .arg (visualScalarModeText (scalarMode))
            .arg (point.hasFiniteScalar ? QString::number (point.scalar, 'g', 6)
                                        : QStringLiteral ("-"))
            .arg (sample.reachableDirections)
            .arg (sample.sampledDirections)
            .arg (reprText);
        data.points.push_back (point);
    }
    updateRange (data);
    return data;
}

QPointF rws::projectVisualPoint (const AnalysisVisualPoint& point,
                                 VisualProjection projection)
{
    switch (projection) {
        case VisualProjection::XY:
            return QPointF (point.position[0], point.position[1]);
        case VisualProjection::XZ:
            return QPointF (point.position[0], point.position[2]);
        case VisualProjection::YZ:
            return QPointF (point.position[1], point.position[2]);
    }
    return QPointF (point.position[0], point.position[1]);
}

QString rws::visualScalarModeText (VisualScalarMode mode)
{
    switch (mode) {
        case VisualScalarMode::Status:           return QStringLiteral ("Status");
        case VisualScalarMode::Manipulability:  return QStringLiteral ("Manipulability");
        case VisualScalarMode::Condition:       return QStringLiteral ("Condition");
        case VisualScalarMode::MinJointMargin:  return QStringLiteral ("Min joint margin");
        case VisualScalarMode::PositionError:   return QStringLiteral ("Position error");
        case VisualScalarMode::OrientationError:return QStringLiteral ("Orientation error");
        case VisualScalarMode::Collision:       return QStringLiteral ("Collision");
        case VisualScalarMode::Coverage:        return QStringLiteral ("Coverage");
    }
    return QStringLiteral ("Status");
}

QString rws::visualProjectionText (VisualProjection projection)
{
    switch (projection) {
        case VisualProjection::XY: return QStringLiteral ("XY");
        case VisualProjection::XZ: return QStringLiteral ("XZ");
        case VisualProjection::YZ: return QStringLiteral ("YZ");
    }
    return QStringLiteral ("XY");
}

QColor rws::visualColorForPoint (const AnalysisVisualPoint& point,
                                 const AnalysisVisualData& data)
{
    if (data.scalarMode == VisualScalarMode::Status || !point.hasFiniteScalar) {
        switch (point.status) {
            case AnalysisStatus::Pass:    return QColor (42, 157, 143);
            case AnalysisStatus::Warning: return QColor (233, 196, 106);
            case AnalysisStatus::Fail:    return QColor (214, 64, 69);
            case AnalysisStatus::Unknown:
            default:                      return QColor (127, 127, 127);
        }
    }
    if (data.scalarMode == VisualScalarMode::Collision) {
        return point.scalar > 0.5 ? QColor (214, 64, 69) : QColor (42, 157, 143);
    }
    const double t = normalizedScalar (point, data);
    const int r = static_cast< int > (45 + t * 190);
    const int g = static_cast< int > (90 + (1.0 - std::fabs (t - 0.5) * 2.0) * 130);
    const int b = static_cast< int > (180 - t * 135);
    return QColor (r, g, b);
}

// ---- New helpers ----

QString rws::visualPointSourceText (VisualPointSource source)
{
    switch (source) {
        case VisualPointSource::TaskPoint:
            return QStringLiteral ("Task points");
        case VisualPointSource::Workspace:
            return QStringLiteral ("Workspace");
        case VisualPointSource::PoseReachability:
            return QStringLiteral ("Pose reachability");
    }
    return QStringLiteral ("Task points");
}

std::vector< VisualScalarMode > rws::supportedVisualScalarModes (
    VisualPointSource source)
{
    switch (source) {
        case VisualPointSource::TaskPoint:
            return {
                VisualScalarMode::Status,
                VisualScalarMode::Manipulability,
                VisualScalarMode::Condition,
                VisualScalarMode::MinJointMargin,
                VisualScalarMode::PositionError,
                VisualScalarMode::OrientationError,
                VisualScalarMode::Collision
            };
        case VisualPointSource::Workspace:
            return {
                VisualScalarMode::Status,
                VisualScalarMode::Manipulability,
                VisualScalarMode::Condition,
                VisualScalarMode::MinJointMargin,
                VisualScalarMode::Collision
            };
        case VisualPointSource::PoseReachability:
            return {
                VisualScalarMode::Status,
                VisualScalarMode::Coverage
            };
    }
    return {VisualScalarMode::Status};
}

bool rws::visualScalarModeSupported (VisualPointSource source,
                                     VisualScalarMode mode)
{
    const std::vector< VisualScalarMode > modes =
        supportedVisualScalarModes (source);
    return std::find (modes.begin (), modes.end (), mode) != modes.end ();
}

VisualScalarMode rws::defaultVisualScalarModeForSource (
    VisualPointSource source)
{
    switch (source) {
        case VisualPointSource::TaskPoint:
            return VisualScalarMode::Status;
        case VisualPointSource::Workspace:
            return VisualScalarMode::Status;
        case VisualPointSource::PoseReachability:
            return VisualScalarMode::Coverage;
    }
    return VisualScalarMode::Status;
}

namespace {

bool visualPointPassesFilters (const AnalysisVisualPoint& point,
                               const AnalysisVisualFilters& filters)
{
    switch (point.status) {
        case AnalysisStatus::Pass:
            return filters.showPass;
        case AnalysisStatus::Warning:
            return filters.showWarning;
        case AnalysisStatus::Fail:
            return filters.showFail;
        case AnalysisStatus::Unknown:
        default:
            return filters.showUnknown;
    }
}

}    // namespace

AnalysisVisualStatusSummary rws::summarizeVisualData (
    const AnalysisVisualData& data,
    const AnalysisVisualFilters& filters)
{
    AnalysisVisualStatusSummary summary;
    summary.totalCount = data.points.size ();
    for (const AnalysisVisualPoint& point : data.points) {
        switch (point.status) {
            case AnalysisStatus::Pass:
                ++summary.passCount;
                break;
            case AnalysisStatus::Warning:
                ++summary.warningCount;
                break;
            case AnalysisStatus::Fail:
                ++summary.failCount;
                break;
            case AnalysisStatus::Unknown:
            default:
                ++summary.unknownCount;
                break;
        }
        if (point.inCollision)
            ++summary.collisionCount;
        if (visualPointPassesFilters (point, filters))
            ++summary.visibleCount;
    }
    return summary;
}

AnalysisVisualBounds rws::projectedVisualBounds (
    const AnalysisVisualData& data,
    VisualProjection projection,
    const AnalysisVisualFilters& filters)
{
    AnalysisVisualBounds bounds;
    for (const AnalysisVisualPoint& point : data.points) {
        if (!visualPointPassesFilters (point, filters))
            continue;
        const QPointF projected = projectVisualPoint (point, projection);
        if (!std::isfinite (projected.x ()) || !std::isfinite (projected.y ()))
            continue;
        if (!bounds.valid) {
            bounds.minX = bounds.maxX = projected.x ();
            bounds.minY = bounds.maxY = projected.y ();
            bounds.valid = true;
        }
        else {
            bounds.minX = std::min (bounds.minX, projected.x ());
            bounds.maxX = std::max (bounds.maxX, projected.x ());
            bounds.minY = std::min (bounds.minY, projected.y ());
            bounds.maxY = std::max (bounds.maxY, projected.y ());
        }
    }
    return bounds;
}
