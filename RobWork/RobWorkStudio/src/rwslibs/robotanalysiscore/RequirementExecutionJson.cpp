#include "RequirementExecutionJson.hpp"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>

#include <cmath>
#include <set>

namespace rws {
namespace {

template <std::size_t N>
QJsonArray writeArray(const std::array<double, N>& values)
{
    QJsonArray result;
    for (double value : values) result.append(value);
    return result;
}

template <std::size_t N>
bool finiteArray(const std::array<double, N>& values)
{
    for (double value : values)
        if (!std::isfinite(value)) return false;
    return true;
}

template <std::size_t N>
bool readArray(const QJsonObject& object, const char* key,
               std::array<double, N>& values, std::string* error)
{
    const QJsonArray array = object.value(key).toArray();
    if (array.size() != static_cast<int>(N)) {
        if (error != nullptr) *error = std::string(key) + " must contain " +
            std::to_string(N) + " values.";
        return false;
    }
    for (std::size_t index = 0; index < N; ++index) {
        const double value = array.at(static_cast<int>(index)).toDouble();
        if (!std::isfinite(value)) {
            if (error != nullptr) *error = std::string(key) + " contains a non-finite value.";
            return false;
        }
        values[index] = value;
    }
    return true;
}

const char* levelToString(RequirementExecutionLevel value)
{
    switch (value) {
    case RequirementExecutionLevel::Must: return "Must";
    case RequirementExecutionLevel::Should: return "Should";
    case RequirementExecutionLevel::Info: return "Info";
    }
    return "Must";
}

bool levelFromString(const QString& text, RequirementExecutionLevel& value)
{
    if (text == "Must") value = RequirementExecutionLevel::Must;
    else if (text == "Should") value = RequirementExecutionLevel::Should;
    else if (text == "Info") value = RequirementExecutionLevel::Info;
    else return false;
    return true;
}

const char* stageToString(RequirementExecutionStage value)
{
    return value == RequirementExecutionStage::Verified ? "Verified" : "Quick";
}

bool stageFromString(const QString& text, RequirementExecutionStage& value)
{
    if (text == "Quick") value = RequirementExecutionStage::Quick;
    else if (text == "Verified") value = RequirementExecutionStage::Verified;
    else return false;
    return true;
}

const char* compileStateToString(RequirementExecutionCompileState value)
{
    switch (value) {
    case RequirementExecutionCompileState::Included: return "Included";
    case RequirementExecutionCompileState::Excluded: return "Excluded";
    case RequirementExecutionCompileState::Invalid: return "Invalid";
    }
    return "Invalid";
}

bool compileStateFromString(const QString& text, RequirementExecutionCompileState& value)
{
    if (text == "Included") value = RequirementExecutionCompileState::Included;
    else if (text == "Excluded") value = RequirementExecutionCompileState::Excluded;
    else if (text == "Invalid") value = RequirementExecutionCompileState::Invalid;
    else return false;
    return true;
}

const char* severityToString(RequirementExecutionDiagnosticSeverity value)
{
    switch (value) {
    case RequirementExecutionDiagnosticSeverity::Info: return "Info";
    case RequirementExecutionDiagnosticSeverity::Warning: return "Warning";
    case RequirementExecutionDiagnosticSeverity::Error: return "Error";
    }
    return "Info";
}

bool severityFromString(const QString& text, RequirementExecutionDiagnosticSeverity& value)
{
    if (text == "Info") value = RequirementExecutionDiagnosticSeverity::Info;
    else if (text == "Warning") value = RequirementExecutionDiagnosticSeverity::Warning;
    else if (text == "Error") value = RequirementExecutionDiagnosticSeverity::Error;
    else return false;
    return true;
}

const char* orientationToString(RequirementExecutionOrientationMode value)
{
    switch (value) {
    case RequirementExecutionOrientationMode::Fixed: return "Fixed";
    case RequirementExecutionOrientationMode::AlignFrame: return "AlignFrame";
    case RequirementExecutionOrientationMode::AlignGeometryNormal: return "AlignGeometryNormal";
    case RequirementExecutionOrientationMode::PointAtTarget: return "PointAtTarget";
    }
    return "Fixed";
}

bool orientationFromString(const QString& text, RequirementExecutionOrientationMode& value)
{
    if (text == "Fixed") value = RequirementExecutionOrientationMode::Fixed;
    else if (text == "AlignFrame") value = RequirementExecutionOrientationMode::AlignFrame;
    else if (text == "AlignGeometryNormal") value = RequirementExecutionOrientationMode::AlignGeometryNormal;
    else if (text == "PointAtTarget") value = RequirementExecutionOrientationMode::PointAtTarget;
    else return false;
    return true;
}

const char* offsetAxisToString(RequirementExecutionOffsetAxis value)
{
    return value == RequirementExecutionOffsetAxis::ReferenceZ ? "ReferenceZ" : "ToolZ";
}

bool offsetAxisFromString(const QString& text, RequirementExecutionOffsetAxis& value)
{
    if (text == "ToolZ") value = RequirementExecutionOffsetAxis::ToolZ;
    else if (text == "ReferenceZ") value = RequirementExecutionOffsetAxis::ReferenceZ;
    else return false;
    return true;
}

const char* processTypeToString(RequirementExecutionProcessType value)
{
    switch (value) {
    case RequirementExecutionProcessType::Generic: return "Generic";
    case RequirementExecutionProcessType::Pick: return "Pick";
    case RequirementExecutionProcessType::Place: return "Place";
    case RequirementExecutionProcessType::MachineLoad: return "MachineLoad";
    case RequirementExecutionProcessType::MachineUnload: return "MachineUnload";
    case RequirementExecutionProcessType::Inspect: return "Inspect";
    case RequirementExecutionProcessType::WeldStart: return "WeldStart";
    case RequirementExecutionProcessType::WeldEnd: return "WeldEnd";
    case RequirementExecutionProcessType::ToolChange: return "ToolChange";
    case RequirementExecutionProcessType::SafeStandby: return "SafeStandby";
    case RequirementExecutionProcessType::Handover: return "Handover";
    }
    return "Generic";
}

bool processTypeFromString(const QString& text, RequirementExecutionProcessType& value)
{
    if (text == "Generic") value = RequirementExecutionProcessType::Generic;
    else if (text == "Pick") value = RequirementExecutionProcessType::Pick;
    else if (text == "Place") value = RequirementExecutionProcessType::Place;
    else if (text == "MachineLoad") value = RequirementExecutionProcessType::MachineLoad;
    else if (text == "MachineUnload") value = RequirementExecutionProcessType::MachineUnload;
    else if (text == "Inspect") value = RequirementExecutionProcessType::Inspect;
    else if (text == "WeldStart") value = RequirementExecutionProcessType::WeldStart;
    else if (text == "WeldEnd") value = RequirementExecutionProcessType::WeldEnd;
    else if (text == "ToolChange") value = RequirementExecutionProcessType::ToolChange;
    else if (text == "SafeStandby") value = RequirementExecutionProcessType::SafeStandby;
    else if (text == "Handover") value = RequirementExecutionProcessType::Handover;
    else return false;
    return true;
}

QJsonObject diagnosticToObject(const RequirementExecutionDiagnostic& value)
{
    QJsonObject object;
    object["code"] = QString::fromStdString(value.code);
    object["severity"] = severityToString(value.severity);
    object["requirementId"] = QString::fromStdString(value.requirementId);
    object["field"] = QString::fromStdString(value.field);
    object["message"] = QString::fromStdString(value.message);
    object["source"] = QString::fromStdString(value.source);
    return object;
}

bool diagnosticFromObject(const QJsonObject& object,
                          RequirementExecutionDiagnostic& value,
                          std::string* error)
{
    value.code = object.value("code").toString().toStdString();
    value.requirementId = object.value("requirementId").toString().toStdString();
    value.field = object.value("field").toString().toStdString();
    value.message = object.value("message").toString().toStdString();
    value.source = object.value("source").toString().toStdString();
    if (!severityFromString(object.value("severity").toString("Info"), value.severity)) {
        if (error != nullptr) *error = "Requirement execution diagnostic severity is invalid.";
        return false;
    }
    return true;
}

void writeCommon(const RequirementExecutionTask& value, QJsonObject& object)
{
    object["id"] = QString::fromStdString(value.id);
    object["name"] = QString::fromStdString(value.name);
    object["level"] = levelToString(value.level);
    object["compileState"] = compileStateToString(value.compileState);
    object["processType"] = processTypeToString(value.processType);
    object["excludedReason"] = QString::fromStdString(value.excludedReason);
    object["refFrame"] = QString::fromStdString(value.refFrame);
    object["tcpFrame"] = QString::fromStdString(value.tcpFrame);
    object["position"] = writeArray(value.position);
    object["rpyDeg"] = writeArray(value.rpyDeg);
    object["positionToleranceMeters"] = value.positionToleranceMeters;
    object["orientationToleranceDeg"] = value.orientationToleranceDeg;
    object["allowToolRollFree"] = value.allowToolRollFree;
    object["orientationMode"] = orientationToString(value.orientationMode);
    object["orientationTargetFrame"] = QString::fromStdString(value.orientationTargetFrame);
    object["orientationTargetGeometry"] = QString::fromStdString(value.orientationTargetGeometry);
    object["orientationTargetPoint"] = QString::fromStdString(value.orientationTargetPoint);
    object["invertNormal"] = value.invertNormal;
    object["rollMinimumDeg"] = value.rollMinimumDeg;
    object["rollMaximumDeg"] = value.rollMaximumDeg;
    object["collisionFreeRequired"] = value.collisionFreeRequired;
    object["minimumJointMargin"] = value.minimumJointMargin;
    object["minimumManipulability"] = value.minimumManipulability;
    object["resolutionEvidence"] = QString::fromStdString(value.resolutionEvidence);
    const auto writePath = [] (const RequirementExecutionPathRule& rule) {
        QJsonObject path;
        path["enabled"] = rule.enabled;
        path["axis"] = offsetAxisToString(rule.axis);
        path["distanceMeters"] = rule.distanceMeters;
        path["collisionFreeRequired"] = rule.collisionFreeRequired;
        return path;
    };
    object["approach"] = writePath(value.approach);
    object["retract"] = writePath(value.retract);
    object["pathValidationPending"] = value.pathValidationPending;
}

bool readCommon(const QJsonObject& object, RequirementExecutionTask& value, std::string* error)
{
    value.id = object.value("id").toString().toStdString();
    value.name = object.value("name").toString().toStdString();
    value.excludedReason = object.value("excludedReason").toString().toStdString();
    value.refFrame = object.value("refFrame").toString("WORLD").toStdString();
    value.tcpFrame = object.value("tcpFrame").toString().toStdString();
    if (!levelFromString(object.value("level").toString("Must"), value.level) ||
        !compileStateFromString(object.value("compileState").toString("Included"), value.compileState) ||
        !processTypeFromString(object.value("processType").toString("Generic"), value.processType) ||
        !orientationFromString(object.value("orientationMode").toString("Fixed"), value.orientationMode)) {
        if (error != nullptr) *error = "Requirement execution task enum is invalid.";
        return false;
    }
    if (!readArray(object, "position", value.position, error) ||
        !readArray(object, "rpyDeg", value.rpyDeg, error)) return false;
    value.positionToleranceMeters = object.value("positionToleranceMeters").toDouble(0.001);
    value.orientationToleranceDeg = object.value("orientationToleranceDeg").toDouble(1.0);
    value.allowToolRollFree = object.value("allowToolRollFree").toBool(false);
    value.orientationTargetFrame = object.value("orientationTargetFrame").toString().toStdString();
    value.orientationTargetGeometry = object.value("orientationTargetGeometry").toString().toStdString();
    value.orientationTargetPoint = object.value("orientationTargetPoint").toString().toStdString();
    value.invertNormal = object.value("invertNormal").toBool(false);
    value.rollMinimumDeg = object.value("rollMinimumDeg").toDouble(-180.0);
    value.rollMaximumDeg = object.value("rollMaximumDeg").toDouble(180.0);
    value.collisionFreeRequired = object.value("collisionFreeRequired").toBool(true);
    value.minimumJointMargin = object.value("minimumJointMargin").toDouble(0.0);
    value.minimumManipulability = object.value("minimumManipulability").toDouble(0.0);
    value.resolutionEvidence = object.value("resolutionEvidence").toString().toStdString();
    const auto readPath = [&] (const QJsonObject& path, RequirementExecutionPathRule& rule) {
        if (path.isEmpty()) return true;
        rule.enabled = path.value("enabled").toBool(false);
        if (!offsetAxisFromString(path.value("axis").toString("ToolZ"), rule.axis)) {
            if (error != nullptr) *error = "Requirement execution path axis is invalid.";
            return false;
        }
        rule.distanceMeters = path.value("distanceMeters").toDouble(0.0);
        rule.collisionFreeRequired = path.value("collisionFreeRequired").toBool(true);
        return true;
    };
    if (!readPath(object.value("approach").toObject(), value.approach) ||
        !readPath(object.value("retract").toObject(), value.retract)) return false;
    value.pathValidationPending = object.contains("pathValidationPending")
        ? object.value("pathValidationPending").toBool(false)
        : (value.approach.enabled || value.retract.enabled);
    value.pathValidationPending = value.pathValidationPending || value.approach.enabled || value.retract.enabled;
    if (!std::isfinite(value.positionToleranceMeters) || !std::isfinite(value.orientationToleranceDeg) ||
        !std::isfinite(value.rollMinimumDeg) || !std::isfinite(value.rollMaximumDeg) ||
        !std::isfinite(value.minimumJointMargin) || !std::isfinite(value.minimumManipulability) ||
        !std::isfinite(value.approach.distanceMeters) || !std::isfinite(value.retract.distanceMeters)) {
        if (error != nullptr) *error = "Requirement execution task contains a non-finite value.";
        return false;
    }
    return true;
}

QJsonObject taskToObject(const RequirementExecutionTask& value)
{
    QJsonObject object;
    writeCommon(value, object);
    QJsonArray diagnostics;
    for (const auto& item : value.diagnostics) diagnostics.append(diagnosticToObject(item));
    object["diagnostics"] = diagnostics;
    return object;
}

bool taskFromObject(const QJsonObject& object, RequirementExecutionTask& value, std::string* error)
{
    if (!readCommon(object, value, error)) return false;
    for (const auto& item : object.value("diagnostics").toArray()) {
        RequirementExecutionDiagnostic diagnostic;
        if (!diagnosticFromObject(item.toObject(), diagnostic, error)) return false;
        value.diagnostics.push_back(diagnostic);
    }
    return true;
}

QJsonObject regionToObject(const RequirementExecutionRegion& value)
{
    QJsonObject object;
    object["id"] = QString::fromStdString(value.id);
    object["name"] = QString::fromStdString(value.name);
    object["level"] = levelToString(value.level);
    object["compileState"] = compileStateToString(value.compileState);
    object["excludedReason"] = QString::fromStdString(value.excludedReason);
    object["refFrame"] = QString::fromStdString(value.refFrame);
    object["tcpFrame"] = QString::fromStdString(value.tcpFrame);
    object["center"] = writeArray(value.center);
    object["size"] = writeArray(value.size);
    object["minimumCoverage"] = value.minimumCoverage;
    object["samplesPerAxis"] = value.samplesPerAxis;
    object["orientationMode"] = orientationToString(value.orientationMode);
    object["orientationTargetFrame"] = QString::fromStdString(value.orientationTargetFrame);
    object["orientationTargetGeometry"] = QString::fromStdString(value.orientationTargetGeometry);
    object["orientationTargetPoint"] = QString::fromStdString(value.orientationTargetPoint);
    object["fixedRpyDeg"] = writeArray(value.fixedRpyDeg);
    object["directionSamples"] = value.directionSamples;
    object["rollSamples"] = value.rollSamples;
    object["minimumOrientationCoverage"] = value.minimumOrientationCoverage;
    object["minimumVerificationStage"] = stageToString(value.minimumVerificationStage);
    object["collisionFreeRequired"] = value.collisionFreeRequired;
    object["positionToleranceMeters"] = value.positionToleranceMeters;
    object["orientationToleranceDeg"] = value.orientationToleranceDeg;
    object["minimumJointMargin"] = value.minimumJointMargin;
    object["minimumManipulability"] = value.minimumManipulability;
    QJsonArray diagnostics;
    for (const auto& item : value.diagnostics) diagnostics.append(diagnosticToObject(item));
    object["diagnostics"] = diagnostics;
    return object;
}

bool regionFromObject(const QJsonObject& object, RequirementExecutionRegion& value, std::string* error)
{
    value.id = object.value("id").toString().toStdString();
    value.name = object.value("name").toString().toStdString();
    value.excludedReason = object.value("excludedReason").toString().toStdString();
    value.refFrame = object.value("refFrame").toString("WORLD").toStdString();
    value.tcpFrame = object.value("tcpFrame").toString().toStdString();
    if (!levelFromString(object.value("level").toString("Must"), value.level) ||
        !compileStateFromString(object.value("compileState").toString("Included"), value.compileState) ||
        !orientationFromString(object.value("orientationMode").toString("Fixed"), value.orientationMode) ||
        !stageFromString(object.value("minimumVerificationStage").toString("Verified"), value.minimumVerificationStage)) {
        if (error != nullptr) *error = "Requirement execution region enum is invalid.";
        return false;
    }
    if (!readArray(object, "center", value.center, error) ||
        !readArray(object, "size", value.size, error) ||
        !readArray(object, "fixedRpyDeg", value.fixedRpyDeg, error)) return false;
    value.minimumCoverage = object.value("minimumCoverage").toDouble(0.8);
    value.samplesPerAxis = object.value("samplesPerAxis").toInt(5);
    value.orientationTargetFrame = object.value("orientationTargetFrame").toString().toStdString();
    value.orientationTargetGeometry = object.value("orientationTargetGeometry").toString().toStdString();
    value.orientationTargetPoint = object.value("orientationTargetPoint").toString().toStdString();
    value.directionSamples = object.value("directionSamples").toInt(1);
    value.rollSamples = object.value("rollSamples").toInt(1);
    value.minimumOrientationCoverage = object.value("minimumOrientationCoverage").toDouble(0.0);
    value.collisionFreeRequired = object.value("collisionFreeRequired").toBool(true);
    value.positionToleranceMeters = object.value("positionToleranceMeters").toDouble(0.001);
    value.orientationToleranceDeg = object.value("orientationToleranceDeg").toDouble(1.0);
    value.minimumJointMargin = object.value("minimumJointMargin").toDouble(0.0);
    value.minimumManipulability = object.value("minimumManipulability").toDouble(0.0);
    if (!std::isfinite(value.minimumCoverage) || value.minimumCoverage < 0.0 || value.minimumCoverage > 1.0 ||
        value.samplesPerAxis < (value.minimumVerificationStage == RequirementExecutionStage::Verified ? 2 : 1) ||
        value.directionSamples < 1 || value.rollSamples < 1 ||
        !std::isfinite(value.minimumOrientationCoverage) || value.minimumOrientationCoverage < 0.0 ||
        value.minimumOrientationCoverage > 1.0 || !std::isfinite(value.positionToleranceMeters) ||
        !std::isfinite(value.orientationToleranceDeg) || !std::isfinite(value.minimumJointMargin) ||
        !std::isfinite(value.minimumManipulability)) {
        if (error != nullptr) *error = "Requirement execution region contains invalid values.";
        return false;
    }
    for (const auto& item : object.value("diagnostics").toArray()) {
        RequirementExecutionDiagnostic diagnostic;
        if (!diagnosticFromObject(item.toObject(), diagnostic, error)) return false;
        value.diagnostics.push_back(diagnostic);
    }
    return true;
}

bool validatePathRule(const RequirementExecutionPathRule& rule, std::string* error)
{
    if (!std::isfinite(rule.distanceMeters) || rule.distanceMeters < 0.0) {
        if (error != nullptr) *error = "Requirement execution path distance must be finite and non-negative.";
        return false;
    }
    return true;
}

bool validateExecutionTask(const RequirementExecutionTask& task, std::string* error)
{
    if (task.id.empty()) {
        if (error != nullptr) *error = "Requirement execution task id is required.";
        return false;
    }
    if (task.refFrame.empty() || task.tcpFrame.empty()) {
        if (error != nullptr) *error = "Requirement execution task refFrame and tcpFrame are required.";
        return false;
    }
    if (!finiteArray(task.position) || !finiteArray(task.rpyDeg) ||
        !std::isfinite(task.positionToleranceMeters) || task.positionToleranceMeters < 0.0 ||
        !std::isfinite(task.orientationToleranceDeg) || task.orientationToleranceDeg < 0.0 ||
        !std::isfinite(task.rollMinimumDeg) || !std::isfinite(task.rollMaximumDeg) ||
        task.rollMinimumDeg > task.rollMaximumDeg ||
        !std::isfinite(task.minimumJointMargin) || task.minimumJointMargin < 0.0 ||
        !std::isfinite(task.minimumManipulability) || task.minimumManipulability < 0.0) {
        if (error != nullptr) *error = "Requirement execution task contains invalid tolerances or limits.";
        return false;
    }
    if ((task.orientationMode == RequirementExecutionOrientationMode::AlignFrame &&
         task.orientationTargetFrame.empty()) ||
        (task.orientationMode == RequirementExecutionOrientationMode::AlignGeometryNormal &&
         (task.orientationTargetFrame.empty() || task.orientationTargetGeometry.empty())) ||
        (task.orientationMode == RequirementExecutionOrientationMode::PointAtTarget &&
         task.orientationTargetFrame.empty() && task.orientationTargetPoint.empty())) {
        if (error != nullptr) *error = "Requirement execution task orientation target is incomplete.";
        return false;
    }
    return validatePathRule(task.approach, error) && validatePathRule(task.retract, error);
}

bool validateExecutionRegion(const RequirementExecutionRegion& region, std::string* error)
{
    if (region.id.empty()) {
        if (error != nullptr) *error = "Requirement execution workspace region id is required.";
        return false;
    }
    if (region.refFrame.empty() || region.tcpFrame.empty()) {
        if (error != nullptr) *error = "Requirement execution workspace region refFrame and tcpFrame are required.";
        return false;
    }
    if (!finiteArray(region.center) || !finiteArray(region.size) || !finiteArray(region.fixedRpyDeg) ||
        !std::isfinite(region.size[0]) || !std::isfinite(region.size[1]) ||
        !std::isfinite(region.size[2]) || region.size[0] <= 0.0 || region.size[1] <= 0.0 ||
        region.size[2] <= 0.0 || !std::isfinite(region.minimumCoverage) ||
        region.minimumCoverage < 0.0 || region.minimumCoverage > 1.0 ||
        region.samplesPerAxis < (region.minimumVerificationStage == RequirementExecutionStage::Verified ? 2 : 1) ||
        region.directionSamples < 1 || region.rollSamples < 1 ||
        !std::isfinite(region.minimumOrientationCoverage) ||
        region.minimumOrientationCoverage < 0.0 || region.minimumOrientationCoverage > 1.0 ||
        !std::isfinite(region.positionToleranceMeters) || region.positionToleranceMeters < 0.0 ||
        !std::isfinite(region.orientationToleranceDeg) || region.orientationToleranceDeg < 0.0 ||
        !std::isfinite(region.minimumJointMargin) || region.minimumJointMargin < 0.0 ||
        !std::isfinite(region.minimumManipulability) || region.minimumManipulability < 0.0) {
        if (error != nullptr) *error = "Requirement execution workspace region contains invalid values.";
        return false;
    }
    if ((region.orientationMode == RequirementExecutionOrientationMode::AlignFrame &&
         region.orientationTargetFrame.empty()) ||
        (region.orientationMode == RequirementExecutionOrientationMode::AlignGeometryNormal &&
         (region.orientationTargetFrame.empty() || region.orientationTargetGeometry.empty())) ||
        (region.orientationMode == RequirementExecutionOrientationMode::PointAtTarget &&
         region.orientationTargetFrame.empty() && region.orientationTargetPoint.empty())) {
        if (error != nullptr) *error = "Requirement execution workspace region orientation target is incomplete.";
        return false;
    }
    return true;
}

} // namespace

QJsonObject RequirementExecutionJson::toObject(const RequirementExecutionSet& value)
{
    QJsonObject object;
    object["type"] = "RequirementExecutionSet";
    object["schemaVersion"] = value.schemaVersion;
    QJsonObject provenance;
    provenance["requirementFingerprint"] = QString::fromStdString(value.provenance.requirementFingerprint);
    provenance["robotModelFingerprint"] = QString::fromStdString(value.provenance.robotModelFingerprint);
    provenance["workcellFingerprint"] = QString::fromStdString(value.provenance.workcellFingerprint);
    provenance["environmentFingerprint"] = QString::fromStdString(value.provenance.environmentFingerprint);
    provenance["compilerVersion"] = QString::fromStdString(value.provenance.compilerVersion);
    provenance["frozenAt"] = QString::fromStdString(value.provenance.frozenAt);
    provenance["sourcePath"] = QString::fromStdString(value.provenance.sourcePath);
    object["provenance"] = provenance;
    QJsonArray tasks;
    for (const auto& item : value.tasks) tasks.append(taskToObject(item));
    object["tasks"] = tasks;
    QJsonArray regions;
    for (const auto& item : value.workspaceRegions) regions.append(regionToObject(item));
    object["workspaceRegions"] = regions;
    QJsonArray diagnostics;
    for (const auto& item : value.diagnostics) diagnostics.append(diagnosticToObject(item));
    object["diagnostics"] = diagnostics;
    return object;
}

bool RequirementExecutionJson::fromObject(const QJsonObject& object,
                                          RequirementExecutionSet& value,
                                          std::string* error)
{
    if (error != nullptr) error->clear();
    if (object.value("type").toString("RequirementExecutionSet") != "RequirementExecutionSet") {
        if (error != nullptr) *error = "Requirement execution JSON type is invalid.";
        return false;
    }
    RequirementExecutionSet parsed;
    parsed.schemaVersion = object.value("schemaVersion").toInt(1);
    if (parsed.schemaVersion != 1) {
        if (error != nullptr) *error = "Requirement execution schemaVersion is unsupported; expected 1.";
        return false;
    }
    const QJsonObject provenance = object.value("provenance").toObject();
    parsed.provenance.requirementFingerprint = provenance.value("requirementFingerprint").toString().toStdString();
    parsed.provenance.robotModelFingerprint = provenance.value("robotModelFingerprint").toString().toStdString();
    parsed.provenance.workcellFingerprint = provenance.value("workcellFingerprint").toString().toStdString();
    parsed.provenance.environmentFingerprint = provenance.value("environmentFingerprint").toString().toStdString();
    parsed.provenance.compilerVersion = provenance.value("compilerVersion").toString().toStdString();
    parsed.provenance.frozenAt = provenance.value("frozenAt").toString().toStdString();
    parsed.provenance.sourcePath = provenance.value("sourcePath").toString().toStdString();
    for (const auto& item : object.value("tasks").toArray()) {
        RequirementExecutionTask task;
        if (!taskFromObject(item.toObject(), task, error)) return false;
        parsed.tasks.push_back(task);
    }
    for (const auto& item : object.value("workspaceRegions").toArray()) {
        RequirementExecutionRegion region;
        if (!regionFromObject(item.toObject(), region, error)) return false;
        parsed.workspaceRegions.push_back(region);
    }
    for (const auto& item : object.value("diagnostics").toArray()) {
        RequirementExecutionDiagnostic diagnostic;
        if (!diagnosticFromObject(item.toObject(), diagnostic, error)) return false;
        parsed.diagnostics.push_back(diagnostic);
    }
    if (!RequirementExecutionJson::validate(parsed, error)) return false;
    value = parsed;
    if (error != nullptr) error->clear();
    return true;
}

bool RequirementExecutionJson::validate(const RequirementExecutionSet& value, std::string* error)
{
    if (error != nullptr) error->clear();
    if (value.schemaVersion != 1) {
        if (error != nullptr) *error = "Requirement execution schemaVersion is unsupported; expected 1.";
        return false;
    }
    std::set<std::string> ids;
    for (const RequirementExecutionTask& task : value.tasks) {
        if (!validateExecutionTask(task, error) || !ids.insert(task.id).second) {
            if (error != nullptr && error->empty()) *error = "Requirement execution contains duplicate requirement ids.";
            return false;
        }
    }
    for (const RequirementExecutionRegion& region : value.workspaceRegions) {
        if (!validateExecutionRegion(region, error) || !ids.insert(region.id).second) {
            if (error != nullptr && error->empty()) *error = "Requirement execution contains duplicate requirement ids.";
            return false;
        }
    }
    if (error != nullptr) error->clear();
    return true;
}

std::string RequirementExecutionJson::toJson(const RequirementExecutionSet& value)
{
    return QJsonDocument(toObject(value)).toJson(QJsonDocument::Compact).toStdString();
}

std::string RequirementExecutionJson::fingerprint(const RequirementExecutionSet& value)
{
    RequirementExecutionSet canonical = value;
    canonical.schemaVersion = 1;
    const QByteArray bytes = QJsonDocument(toObject(canonical)).toJson(QJsonDocument::Compact);
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex().toStdString();
}

bool RequirementExecutionJson::fromJson(const std::string& json,
                                        RequirementExecutionSet& value,
                                        std::string* error)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(QByteArray::fromStdString(json), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error != nullptr) *error = parseError.errorString().toStdString();
        return false;
    }
    return fromObject(document.object(), value, error);
}

} // namespace rws
