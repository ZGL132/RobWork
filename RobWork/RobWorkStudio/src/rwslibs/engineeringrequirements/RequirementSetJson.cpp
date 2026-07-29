#include "RequirementSetJson.hpp"

#include <QJsonArray>
#include <QJsonDocument>

#include <cmath>

namespace rws {
namespace {

template <std::size_t N>
QJsonArray writeArray(const std::array<double, N>& values)
{
    QJsonArray output;
    for (double value : values)
        output.append(value);
    return output;
}

template <std::size_t N>
bool readArray(const QJsonObject& object, const char* key, std::array<double, N>& values,
               std::string* error)
{
    const QJsonArray array = object.value(key).toArray();
    if (array.size() != static_cast<int>(N)) {
        if (error != nullptr)
            *error = std::string(key) + " must contain " + std::to_string(N) + " values.";
        return false;
    }
    for (std::size_t index = 0; index < N; ++index) {
        const double value = array.at(static_cast<int>(index)).toDouble();
        if (!std::isfinite(value)) {
            if (error != nullptr)
                *error = std::string(key) + " contains a non-finite value.";
            return false;
        }
        values[index] = value;
    }
    return true;
}

QJsonObject writePoseTask(const PoseTask& task)
{
    QJsonObject object;
    object["id"] = QString::fromStdString(task.id);
    object["name"] = QString::fromStdString(task.name);
    object["processType"] = toString(task.processType);
    object["level"] = toString(task.level);
    object["source"] = toString(task.source);
    object["refFrame"] = QString::fromStdString(task.refFrame);
    object["tcpFrame"] = QString::fromStdString(task.tcpFrame);
    object["position"] = writeArray(task.position);
    object["rpyDeg"] = writeArray(task.rpyDeg);
    object["positionToleranceMeters"] = task.tolerance.positionMeters;
    object["orientationToleranceDeg"] = task.tolerance.orientationDeg;
    object["allowToolRollFree"] = task.tolerance.allowToolRollFree;
    QJsonObject geometryFeature;
    geometryFeature["type"] = toString(task.geometryFeature.type);
    geometryFeature["frameName"] = QString::fromStdString(task.geometryFeature.frameName);
    geometryFeature["objectName"] = QString::fromStdString(task.geometryFeature.objectName);
    geometryFeature["geometryName"] = QString::fromStdString(task.geometryFeature.geometryName);
    object["geometryFeature"] = geometryFeature;
    QJsonObject generation;
    generation["generatorId"] = QString::fromStdString(task.generation.generatorId);
    generation["instanceId"] = QString::fromStdString(task.generation.instanceId);
    generation["linked"] = task.generation.linked;
    QJsonArray generationParameters;
    for (const GenerationParameter& parameter : task.generation.parameters) {
        QJsonObject value;
        value["key"] = QString::fromStdString(parameter.key);
        value["value"] = QString::fromStdString(parameter.value);
        generationParameters.append(value);
    }
    generation["parameters"] = generationParameters;
    object["generation"] = generation;
    QJsonObject orientation;
    orientation["mode"] = toString(task.orientation.mode);
    orientation["targetFrame"] = QString::fromStdString(task.orientation.targetFrame);
    orientation["targetGeometry"] = QString::fromStdString(task.orientation.targetGeometry);
    orientation["targetPoint"] = QString::fromStdString(task.orientation.targetPoint);
    orientation["invertNormal"] = task.orientation.invertNormal;
    orientation["allowToolRollFree"] = task.orientation.allowToolRollFree;
    orientation["rollMinimumDeg"] = task.orientation.rollMinimumDeg;
    orientation["rollMaximumDeg"] = task.orientation.rollMaximumDeg;
    object["orientation"] = orientation;
    const auto writePathRule = [] (const ApproachRetractRule& rule) {
        QJsonObject value;
        value["enabled"] = rule.enabled;
        value["axis"] = toString(rule.axis);
        value["distanceMeters"] = rule.distanceMeters;
        value["collisionFreeRequired"] = rule.collisionFreeRequired;
        return value;
    };
    object["approach"] = writePathRule(task.approach);
    object["retract"] = writePathRule(task.retract);
    QJsonObject validation;
    validation["collisionFreeRequired"] = task.validation.collisionFreeRequired;
    validation["minimumJointMargin"] = task.validation.minimumJointMargin;
    validation["minimumManipulability"] = task.validation.minimumManipulability;
    object["validation"] = validation;
    object["confidence"] = task.confidence;
    object["note"] = QString::fromStdString(task.note);
    return object;
}

bool readPoseTask(const QJsonObject& object, PoseTask& task, std::string* error)
{
    task.id = object.value("id").toString().toStdString();
    task.name = object.value("name").toString().toStdString();
    if (!processTypeFromString(object.value("processType").toString("Generic").toStdString(), task.processType)) {
        if (error != nullptr) *error = "KeyStation.processType is invalid.";
        return false;
    }
    if (!requirementLevelFromString(object.value("level").toString("Must").toStdString(), task.level)) {
        if (error != nullptr) *error = "PoseTask.level is invalid.";
        return false;
    }
    if (!poseTaskSourceFromString(object.value("source").toString("Manual").toStdString(), task.source)) {
        if (error != nullptr) *error = "PoseTask.source is invalid.";
        return false;
    }
    task.refFrame = object.value("refFrame").toString("WORLD").toStdString();
    task.tcpFrame = object.value("tcpFrame").toString().toStdString();
    if (!readArray(object, "position", task.position, error) ||
        !readArray(object, "rpyDeg", task.rpyDeg, error))
        return false;
    task.tolerance.positionMeters = object.value("positionToleranceMeters").toDouble(0.001);
    task.tolerance.orientationDeg = object.value("orientationToleranceDeg").toDouble(1.0);
    task.tolerance.allowToolRollFree = object.value("allowToolRollFree").toBool(false);
    const QJsonObject geometryFeature = object.value("geometryFeature").toObject();
    if (!geometryFeatureTypeFromString(geometryFeature.value("type").toString("None").toStdString(), task.geometryFeature.type)) {
        if (error != nullptr) *error = "KeyStation.geometryFeature.type is invalid.";
        return false;
    }
    task.geometryFeature.frameName = geometryFeature.value("frameName").toString().toStdString();
    task.geometryFeature.objectName = geometryFeature.value("objectName").toString().toStdString();
    task.geometryFeature.geometryName = geometryFeature.value("geometryName").toString().toStdString();
    const QJsonObject generation = object.value("generation").toObject();
    task.generation.generatorId = generation.value("generatorId").toString().toStdString();
    task.generation.instanceId = generation.value("instanceId").toString().toStdString();
    task.generation.linked = generation.value("linked").toBool(false);
    task.generation.parameters.clear();
    const QJsonArray generationParameters = generation.value("parameters").toArray();
    for (const QJsonValue& value : generationParameters) {
        const QJsonObject parameter = value.toObject();
        const std::string key = parameter.value("key").toString().toStdString();
        if (key.empty()) {
            if (error != nullptr) *error = "KeyStation.generation parameter key cannot be empty.";
            return false;
        }
        task.generation.parameters.push_back({key, parameter.value("value").toString().toStdString()});
    }
    const QJsonObject orientation = object.value("orientation").toObject();
    if (!orientationModeFromString(orientation.value("mode").toString("Fixed").toStdString(), task.orientation.mode)) {
        if (error != nullptr) *error = "KeyStation.orientation.mode is invalid.";
        return false;
    }
    task.orientation.targetFrame = orientation.value("targetFrame").toString().toStdString();
    task.orientation.targetGeometry = orientation.value("targetGeometry").toString().toStdString();
    task.orientation.targetPoint = orientation.value("targetPoint").toString().toStdString();
    task.orientation.invertNormal = orientation.value("invertNormal").toBool(false);
    task.orientation.allowToolRollFree = orientation.value("allowToolRollFree").toBool(task.tolerance.allowToolRollFree);
    task.orientation.rollMinimumDeg = orientation.value("rollMinimumDeg").toDouble(-180.0);
    task.orientation.rollMaximumDeg = orientation.value("rollMaximumDeg").toDouble(180.0);
    const auto readPathRule = [error] (const QJsonObject& value, ApproachRetractRule& rule) {
        rule.enabled = value.value("enabled").toBool(false);
        if (!offsetAxisFromString(value.value("axis").toString("ToolZ").toStdString(), rule.axis)) {
            if (error != nullptr) *error = "KeyStation approach/retract axis is invalid.";
            return false;
        }
        rule.distanceMeters = value.value("distanceMeters").toDouble(0.0);
        rule.collisionFreeRequired = value.value("collisionFreeRequired").toBool(true);
        return std::isfinite(rule.distanceMeters);
    };
    if (!readPathRule(object.value("approach").toObject(), task.approach) ||
        !readPathRule(object.value("retract").toObject(), task.retract))
        return false;
    const QJsonObject validation = object.value("validation").toObject();
    task.validation.collisionFreeRequired = validation.value("collisionFreeRequired").toBool(true);
    task.validation.minimumJointMargin = validation.value("minimumJointMargin").toDouble(0.0);
    task.validation.minimumManipulability = validation.value("minimumManipulability").toDouble(0.0);
    task.confidence = object.value("confidence").toDouble(1.0);
    task.note = object.value("note").toString().toStdString();
    return std::isfinite(task.tolerance.positionMeters) && std::isfinite(task.tolerance.orientationDeg) &&
           std::isfinite(task.orientation.rollMinimumDeg) && std::isfinite(task.orientation.rollMaximumDeg) &&
           std::isfinite(task.validation.minimumJointMargin) && std::isfinite(task.validation.minimumManipulability) &&
           std::isfinite(task.confidence);
}

QJsonObject writeBoxRegion(const BoxRegion& region)
{
    QJsonObject object;
    object["id"] = QString::fromStdString(region.id);
    object["name"] = QString::fromStdString(region.name);
    object["level"] = toString(region.level);
    object["refFrame"] = QString::fromStdString(region.refFrame);
    object["center"] = writeArray(region.center);
    object["size"] = writeArray(region.size);
    object["minimumCoverage"] = region.minimumCoverage;
    object["samplesPerAxis"] = region.samplesPerAxis;
    return object;
}

bool readBoxRegion(const QJsonObject& object, BoxRegion& region, std::string* error)
{
    region.id = object.value("id").toString().toStdString();
    region.name = object.value("name").toString().toStdString();
    if (!requirementLevelFromString(object.value("level").toString("Must").toStdString(), region.level)) {
        if (error != nullptr) *error = "BoxRegion.level is invalid.";
        return false;
    }
    region.refFrame = object.value("refFrame").toString("WORLD").toStdString();
    if (!readArray(object, "center", region.center, error) ||
        !readArray(object, "size", region.size, error))
        return false;
    region.minimumCoverage = object.value("minimumCoverage").toDouble(0.8);
    region.samplesPerAxis = object.value("samplesPerAxis").toInt(5);
    return std::isfinite(region.minimumCoverage);
}

} // namespace

const char* toString(RequirementLevel value)
{
    switch (value) {
        case RequirementLevel::Must: return "Must";
        case RequirementLevel::Should: return "Should";
        case RequirementLevel::Info: return "Info";
    }
    return "Must";
}

const char* toString(PoseTaskSource value)
{
    switch (value) {
        case PoseTaskSource::Manual: return "Manual";
        case PoseTaskSource::CapturedTcp: return "CapturedTcp";
        case PoseTaskSource::FrameOffset: return "FrameOffset";
        case PoseTaskSource::GeometryFeature: return "GeometryFeature";
        case PoseTaskSource::Template: return "Template";
        case PoseTaskSource::Imported: return "Imported";
    }
    return "Manual";
}

bool requirementLevelFromString(const std::string& text, RequirementLevel& value)
{
    if (text == "Must") { value = RequirementLevel::Must; return true; }
    if (text == "Should") { value = RequirementLevel::Should; return true; }
    if (text == "Info") { value = RequirementLevel::Info; return true; }
    return false;
}

bool poseTaskSourceFromString(const std::string& text, PoseTaskSource& value)
{
    if (text == "Manual") { value = PoseTaskSource::Manual; return true; }
    if (text == "CapturedTcp") { value = PoseTaskSource::CapturedTcp; return true; }
    if (text == "FrameOffset") { value = PoseTaskSource::FrameOffset; return true; }
    if (text == "GeometryFeature") { value = PoseTaskSource::GeometryFeature; return true; }
    if (text == "Template") { value = PoseTaskSource::Template; return true; }
    if (text == "Imported") { value = PoseTaskSource::Imported; return true; }
    return false;
}

const char* toString(ProcessType value)
{
    switch (value) {
        case ProcessType::Generic: return "Generic"; case ProcessType::Pick: return "Pick";
        case ProcessType::Place: return "Place"; case ProcessType::MachineLoad: return "MachineLoad";
        case ProcessType::MachineUnload: return "MachineUnload"; case ProcessType::Inspect: return "Inspect";
        case ProcessType::WeldStart: return "WeldStart"; case ProcessType::WeldEnd: return "WeldEnd";
        case ProcessType::ToolChange: return "ToolChange"; case ProcessType::SafeStandby: return "SafeStandby";
        case ProcessType::Handover: return "Handover";
    }
    return "Generic";
}
const char* toString(OrientationMode value)
{
    switch (value) {
        case OrientationMode::Fixed: return "Fixed"; case OrientationMode::AlignFrame: return "AlignFrame";
        case OrientationMode::AlignGeometryNormal: return "AlignGeometryNormal"; case OrientationMode::PointAtTarget: return "PointAtTarget";
    }
    return "Fixed";
}
const char* toString(OffsetAxis value) { return value == OffsetAxis::ReferenceZ ? "ReferenceZ" : "ToolZ"; }
const char* toString(GeometryFeatureType value)
{
    switch (value) {
        case GeometryFeatureType::None: return "None";
        case GeometryFeatureType::FrameOrigin: return "FrameOrigin";
        case GeometryFeatureType::FramePlaneNormal: return "FramePlaneNormal";
    }
    return "None";
}
bool processTypeFromString(const std::string& text, ProcessType& value)
{
    for (int item = static_cast<int>(ProcessType::Generic); item <= static_cast<int>(ProcessType::Handover); ++item) {
        const ProcessType candidate = static_cast<ProcessType>(item);
        if (text == toString(candidate)) { value = candidate; return true; }
    }
    return false;
}
bool orientationModeFromString(const std::string& text, OrientationMode& value)
{
    for (int item = static_cast<int>(OrientationMode::Fixed); item <= static_cast<int>(OrientationMode::PointAtTarget); ++item) {
        const OrientationMode candidate = static_cast<OrientationMode>(item);
        if (text == toString(candidate)) { value = candidate; return true; }
    }
    return false;
}
bool offsetAxisFromString(const std::string& text, OffsetAxis& value)
{
    if (text == "ToolZ") { value = OffsetAxis::ToolZ; return true; }
    if (text == "ReferenceZ") { value = OffsetAxis::ReferenceZ; return true; }
    return false;
}
bool geometryFeatureTypeFromString(const std::string& text, GeometryFeatureType& value)
{
    if (text == "None") { value = GeometryFeatureType::None; return true; }
    if (text == "FrameOrigin") { value = GeometryFeatureType::FrameOrigin; return true; }
    if (text == "FramePlaneNormal") { value = GeometryFeatureType::FramePlaneNormal; return true; }
    return false;
}

QJsonObject RequirementSetJson::toObject(const RequirementSet& requirements)
{
    QJsonObject object;
    object["schemaVersion"] = requirements.schemaVersion;
    object["name"] = QString::fromStdString(requirements.name);
    object["version"] = requirements.version;
    object["frozen"] = requirements.frozen;
    QJsonObject binding;
    binding["sourcePath"] = QString::fromStdString(requirements.modelBinding.sourcePath);
    binding["robotModelFingerprint"] = QString::fromStdString(requirements.modelBinding.robotModelFingerprint);
    binding["robotName"] = QString::fromStdString(requirements.modelBinding.robotName);
    object["modelBinding"] = binding;
    QJsonArray poses;
    for (const PoseTask& task : requirements.poseTasks)
        poses.append(writePoseTask(task));
    object["poseTasks"] = poses;
    QJsonArray regions;
    for (const BoxRegion& region : requirements.boxRegions)
        regions.append(writeBoxRegion(region));
    object["boxRegions"] = regions;
    return object;
}

bool RequirementSetJson::fromObject(const QJsonObject& object, RequirementSet& requirements,
                                    std::string* error)
{
    RequirementSet parsed;
    parsed.schemaVersion = object.value("schemaVersion").toInt(1);
    parsed.name = object.value("name").toString().toStdString();
    parsed.version = object.value("version").toInt(1);
    parsed.frozen = object.value("frozen").toBool(false);
    const QJsonObject binding = object.value("modelBinding").toObject();
    parsed.modelBinding.sourcePath = binding.value("sourcePath").toString().toStdString();
    parsed.modelBinding.robotModelFingerprint = binding.value("robotModelFingerprint").toString().toStdString();
    parsed.modelBinding.robotName = binding.value("robotName").toString().toStdString();
    for (const QJsonValue& value : object.value("poseTasks").toArray()) {
        PoseTask task;
        if (!readPoseTask(value.toObject(), task, error)) return false;
        parsed.poseTasks.push_back(task);
    }
    for (const QJsonValue& value : object.value("boxRegions").toArray()) {
        BoxRegion region;
        if (!readBoxRegion(value.toObject(), region, error)) return false;
        parsed.boxRegions.push_back(region);
    }
    requirements = parsed;
    return true;
}

std::string RequirementSetJson::toJson(const RequirementSet& requirements)
{
    return QJsonDocument(toObject(requirements)).toJson(QJsonDocument::Compact).toStdString();
}

bool RequirementSetJson::fromJson(const std::string& json, RequirementSet& requirements,
                                  std::string* error)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(QByteArray::fromStdString(json), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error != nullptr) *error = "Requirement JSON is not an object: " + parseError.errorString().toStdString();
        return false;
    }
    return fromObject(document.object(), requirements, error);
}

} // namespace rws
