#include "RequirementCompiler.hpp"

#include "RequirementSetJson.hpp"

#include <QCryptographicHash>

#include <cmath>
#include <set>

namespace rws {
namespace {

bool finiteArray(const std::array<double, 3>& values)
{
    return std::isfinite(values[0]) && std::isfinite(values[1]) && std::isfinite(values[2]);
}

void addDiagnostic(std::vector<RequirementDiagnostic>& diagnostics, const std::string& requirementId,
                   RequirementLevel level, const std::string& message)
{
    RequirementDiagnostic diagnostic;
    diagnostic.requirementId = requirementId;
    diagnostic.level = level;
    diagnostic.message = message;
    diagnostic.blocking = level == RequirementLevel::Must;
    diagnostics.push_back(diagnostic);
}

bool hasDiagnosticFor(const std::vector<RequirementDiagnostic>& diagnostics, const std::string& requirementId)
{
    for (const RequirementDiagnostic& diagnostic : diagnostics) {
        if (diagnostic.requirementId == requirementId)
            return true;
    }
    return false;
}

} // namespace

std::vector<RequirementDiagnostic> RequirementCompiler::validateDetailed(const RequirementSet& requirements)
{
    std::vector<RequirementDiagnostic> diagnostics;
    if (requirements.modelBinding.robotModelFingerprint.empty())
        addDiagnostic(diagnostics, std::string(), RequirementLevel::Must, "A robot model fingerprint is required.");
    std::set<std::string> ids;
    for (const PoseTask& task : requirements.poseTasks) {
        if (task.id.empty()) addDiagnostic(diagnostics, task.id, task.level, "Key station id is required.");
        else if (!ids.insert(task.id).second) addDiagnostic(diagnostics, task.id, task.level, "Duplicate requirement id: " + task.id);
        if (task.name.empty()) addDiagnostic(diagnostics, task.id, task.level, "Key station name is required: " + task.id);
        if (task.refFrame.empty()) addDiagnostic(diagnostics, task.id, task.level, "Key station reference frame is required: " + task.id);
        if (task.tcpFrame.empty()) addDiagnostic(diagnostics, task.id, task.level, "Key station TCP frame is required: " + task.id);
        if (task.source == PoseTaskSource::GeometryFeature &&
            (task.geometryFeature.type == GeometryFeatureType::None || task.geometryFeature.frameName.empty()))
            addDiagnostic(diagnostics, task.id, task.level, "Key station geometry feature frame is required: " + task.id);
        if (task.orientation.mode == OrientationMode::AlignFrame && task.orientation.targetFrame.empty())
            addDiagnostic(diagnostics, task.id, task.level, "Key station alignment target frame is required: " + task.id);
        if (task.orientation.mode == OrientationMode::PointAtTarget && task.orientation.targetFrame.empty() && task.orientation.targetPoint.empty())
            addDiagnostic(diagnostics, task.id, task.level, "Key station pointing target is required: " + task.id);
        if (!finiteArray(task.position) || !finiteArray(task.rpyDeg) ||
            !std::isfinite(task.tolerance.positionMeters) || !std::isfinite(task.tolerance.orientationDeg) ||
            task.tolerance.positionMeters < 0.0 || task.tolerance.orientationDeg < 0.0)
            addDiagnostic(diagnostics, task.id, task.level, "Key station contains invalid pose or tolerance values: " + task.id);
        if (!std::isfinite(task.orientation.rollMinimumDeg) || !std::isfinite(task.orientation.rollMaximumDeg) ||
            task.orientation.rollMinimumDeg > task.orientation.rollMaximumDeg)
            addDiagnostic(diagnostics, task.id, task.level, "Key station contains invalid roll limits: " + task.id);
        if ((task.approach.enabled && (!std::isfinite(task.approach.distanceMeters) || task.approach.distanceMeters < 0.0)) ||
            (task.retract.enabled && (!std::isfinite(task.retract.distanceMeters) || task.retract.distanceMeters < 0.0)))
            addDiagnostic(diagnostics, task.id, task.level, "Key station approach or retract distance must be non-negative: " + task.id);
        if (!std::isfinite(task.validation.minimumJointMargin) || !std::isfinite(task.validation.minimumManipulability) ||
            task.validation.minimumJointMargin < 0.0 || task.validation.minimumManipulability < 0.0)
            addDiagnostic(diagnostics, task.id, task.level, "Key station validation policy contains invalid values: " + task.id);
        if (!std::isfinite(task.confidence) || task.confidence < 0.0 || task.confidence > 1.0)
            addDiagnostic(diagnostics, task.id, task.level, "Key station confidence must be within [0, 1]: " + task.id);
    }
    for (const BoxRegion& region : requirements.boxRegions) {
        if (region.id.empty()) addDiagnostic(diagnostics, region.id, region.level, "Box region id is required.");
        else if (!ids.insert(region.id).second) addDiagnostic(diagnostics, region.id, region.level, "Duplicate requirement id: " + region.id);
        if (region.refFrame.empty()) addDiagnostic(diagnostics, region.id, region.level, "Box region reference frame is required: " + region.id);
        if (!finiteArray(region.center) || !finiteArray(region.size) ||
            region.size[0] <= 0.0 || region.size[1] <= 0.0 || region.size[2] <= 0.0 ||
            !std::isfinite(region.minimumCoverage) || region.minimumCoverage < 0.0 ||
            region.minimumCoverage > 1.0 || region.samplesPerAxis < 2)
            addDiagnostic(diagnostics, region.id, region.level, "Box region contains invalid values: " + region.id);
    }
    return diagnostics;
}

std::vector<std::string> RequirementCompiler::validate(const RequirementSet& requirements)
{
    std::vector<std::string> messages;
    for (const RequirementDiagnostic& diagnostic : validateDetailed(requirements))
        messages.push_back(diagnostic.message);
    return messages;
}

std::string RequirementCompiler::fingerprint(const RequirementSet& requirements)
{
    RequirementSet canonical = requirements;
    canonical.frozen = false;
    const QByteArray bytes = QByteArray::fromStdString(RequirementSetJson::toJson(canonical));
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex().toStdString();
}

bool RequirementCompiler::compile(const RequirementSet& requirements, CompiledRequirementSet& compiled,
                                  std::string* error)
{
    const std::vector<RequirementDiagnostic> diagnostics = validateDetailed(requirements);
    for (const RequirementDiagnostic& diagnostic : diagnostics) {
        if (diagnostic.blocking) {
            if (error != nullptr) *error = diagnostic.message;
            return false;
        }
    }
    CompiledRequirementSet result;
    result.frozen = true;
    result.modelBinding = requirements.modelBinding;
    result.requirementFingerprint = fingerprint(requirements);
    result.diagnostics = diagnostics;
    for (const PoseTask& task : requirements.poseTasks) {
        if (task.level == RequirementLevel::Info || hasDiagnosticFor(diagnostics, task.id)) continue;
        CompiledPoseTask item;
        item.id = task.id; item.name = task.name; item.level = task.level;
        item.refFrame = task.refFrame; item.tcpFrame = task.tcpFrame;
        item.position = task.position; item.rpyDeg = task.rpyDeg; item.tolerance = task.tolerance;
        item.processType = task.processType;
        item.geometryFeature = task.geometryFeature;
        item.orientation = task.orientation;
        item.validation = task.validation;
        item.pathValidationPending = task.approach.enabled || task.retract.enabled;
        result.poseTasks.push_back(item);
    }
    for (const BoxRegion& region : requirements.boxRegions) {
        if (region.level == RequirementLevel::Info || hasDiagnosticFor(diagnostics, region.id)) continue;
        WorkspaceDemandRegion item;
        item.id = region.id; item.name = region.name; item.level = region.level;
        item.refFrame = region.refFrame; item.center = region.center; item.size = region.size;
        item.minimumCoverage = region.minimumCoverage; item.samplesPerAxis = region.samplesPerAxis;
        result.workspaceRegions.push_back(item);
    }
    compiled = result;
    if (error != nullptr) error->clear();
    return true;
}

} // namespace rws
