#include "RequirementSetJson.hpp"

#include <QJsonArray>
#include <QJsonDocument>

#include <algorithm>
#include <cmath>
#include <limits>

namespace rws {
namespace {

bool isKnownKey(const QString& key, std::initializer_list<const char*> knownKeys)
{
    for (const char* known : knownKeys)
        if (key == QLatin1String(known)) return true;
    return false;
}

bool readExtensions(const QJsonObject& object,
                    std::initializer_list<const char*> knownKeys,
                    QJsonObject& extensions, std::string* error)
{
    extensions = QJsonObject();
    if (object.contains("extensions")) {
        if (!object.value("extensions").isObject()) {
            if (error != nullptr) *error = "extensions must be an object.";
            return false;
        }
        const QJsonObject explicitExtensions = object.value("extensions").toObject();
        for (const QString& key : explicitExtensions.keys()) {
            if (isKnownKey(key, knownKeys) || key == QLatin1String("extensions")) {
                if (error != nullptr)
                    *error = "extensions cannot override known field '" + key.toStdString() + "'.";
                return false;
            }
            extensions.insert(key, explicitExtensions.value(key));
        }
    }
    for (const QString& key : object.keys()) {
        if (key == QLatin1String("extensions") || isKnownKey(key, knownKeys)) continue;
        if (extensions.contains(key)) {
            if (error != nullptr)
                *error = "top-level extension field '" + key.toStdString() +
                    "' conflicts with explicit extensions.";
            return false;
        }
        extensions.insert(key, object.value(key));
    }
    return true;
}

void writeExtensions(QJsonObject& object, const QJsonObject& extensions,
                     std::initializer_list<const char*> knownKeys)
{
    QJsonObject filtered;
    for (const QString& key : extensions.keys()) {
        if (!isKnownKey(key, knownKeys) && key != QLatin1String("extensions"))
            filtered.insert(key, extensions.value(key));
    }
    if (!filtered.isEmpty()) object["extensions"] = filtered;
}

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
    const QJsonValue raw = object.value(key);
    if (!raw.isArray()) {
        if (error != nullptr)
            *error = std::string(key) + " must be an array.";
        return false;
    }
    const QJsonArray array = raw.toArray();
    if (array.size() != static_cast<int>(N)) {
        if (error != nullptr)
            *error = std::string(key) + " must contain " + std::to_string(N) + " values.";
        return false;
    }
    for (std::size_t index = 0; index < N; ++index) {
        const QJsonValue rawValue = array.at(static_cast<int>(index));
        if (!rawValue.isDouble()) {
            if (error != nullptr)
                *error = std::string(key) + " values must be numbers.";
            return false;
        }
        const double value = rawValue.toDouble();
        if (!std::isfinite(value)) {
            if (error != nullptr)
                *error = std::string(key) + " contains a non-finite value.";
            return false;
        }
        values[index] = value;
    }
    return true;
}

bool requireString(const QJsonObject& object, const char* key, std::string& value,
                   const std::string& defaultValue, std::string* error)
{
    const QJsonValue raw = object.value(key);
    if (raw.isUndefined()) {
        value = defaultValue;
        return true;
    }
    if (!raw.isString()) {
        if (error != nullptr) *error = std::string(key) + " must be a string.";
        return false;
    }
    value = raw.toString().toStdString();
    return true;
}

bool requireNumber(const QJsonObject& object, const char* key, double& value,
                   double defaultValue, std::string* error)
{
    const QJsonValue raw = object.value(key);
    if (raw.isUndefined()) {
        value = defaultValue;
        return true;
    }
    if (!raw.isDouble() || !std::isfinite(raw.toDouble())) {
        if (error != nullptr) *error = std::string(key) + " must be a finite number.";
        return false;
    }
    value = raw.toDouble();
    return true;
}

bool requireInteger(const QJsonObject& object, const char* key, int& value,
                    int defaultValue, std::string* error)
{
    double number = 0.0;
    if (!requireNumber(object, key, number, static_cast<double>(defaultValue), error))
        return false;
    if (object.contains(key) && std::floor(number) != number) {
        if (error != nullptr) *error = std::string(key) + " must be an integer.";
        return false;
    }
    if (number < static_cast<double>(std::numeric_limits<int>::min()) ||
        number > static_cast<double>(std::numeric_limits<int>::max())) {
        if (error != nullptr) *error = std::string(key) + " is outside the integer range.";
        return false;
    }
    value = static_cast<int>(number);
    return true;
}

bool requireBool(const QJsonObject& object, const char* key, bool& value,
                 bool defaultValue, std::string* error)
{
    const QJsonValue raw = object.value(key);
    if (raw.isUndefined()) {
        value = defaultValue;
        return true;
    }
    if (!raw.isBool()) {
        if (error != nullptr) *error = std::string(key) + " must be a boolean.";
        return false;
    }
    value = raw.toBool();
    return true;
}

bool requireObject(const QJsonObject& object, const char* key, QJsonObject& value,
                   std::string* error)
{
    const QJsonValue raw = object.value(key);
    if (raw.isUndefined()) {
        value = QJsonObject();
        return true;
    }
    if (!raw.isObject()) {
        if (error != nullptr) *error = std::string(key) + " must be an object.";
        return false;
    }
    value = raw.toObject();
    return true;
}

bool requireArray(const QJsonObject& object, const char* key, QJsonArray& value,
                  std::string* error)
{
    const QJsonValue raw = object.value(key);
    if (raw.isUndefined()) {
        value = QJsonArray();
        return true;
    }
    if (!raw.isArray()) {
        if (error != nullptr) *error = std::string(key) + " must be an array.";
        return false;
    }
    value = raw.toArray();
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
    // 导入溯源独立于模板/阵列溯源：一个工位既可能来自外部文件，也可能随后被阵列复制。
    QJsonObject importProvenance;
    importProvenance["sourcePath"] = QString::fromStdString(task.importProvenance.sourcePath);
    importProvenance["recordNumber"] = task.importProvenance.recordNumber;
    object["importProvenance"] = importProvenance;
    QJsonObject orientation;
    orientation["mode"] = toString(task.orientation.mode);
    orientation["targetFrame"] = QString::fromStdString(task.orientation.targetFrame);
    orientation["targetGeometry"] = QString::fromStdString(task.orientation.targetGeometry);
    orientation["targetPoint"] = QString::fromStdString(task.orientation.targetPoint);
    // 解析证据通常只在冻结快照中非空。统一通过需求 JSON 读写，保证冻结工件重载后
    // 不会丢失“代表姿态来自哪条规则与哪个目标”的可审计依据。
    orientation["resolutionEvidence"] = QString::fromStdString(task.orientation.resolutionEvidence);
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
    writeExtensions(object, task.extensions,
                    {"id", "name", "processType", "level", "source", "refFrame", "tcpFrame",
                     "position", "rpyDeg", "positionToleranceMeters", "orientationToleranceDeg",
                     "allowToolRollFree", "geometryFeature", "generation", "importProvenance",
                     "orientation", "approach", "retract", "validation", "confidence", "note"});
    return object;
}

bool readPoseTask(const QJsonObject& object, PoseTask& task, std::string* error)
{
    if (!readExtensions(object,
                        {"id", "name", "processType", "level", "source", "refFrame", "tcpFrame",
                         "position", "rpyDeg", "positionToleranceMeters", "orientationToleranceDeg",
                         "allowToolRollFree", "geometryFeature", "generation", "importProvenance",
                         "orientation", "approach", "retract", "validation", "confidence", "note"},
                        task.extensions, error)) return false;
    std::string text;
    if (!requireString(object, "id", task.id, "", error) ||
        !requireString(object, "name", task.name, "", error) ||
        !requireString(object, "processType", text, "Generic", error)) return false;
    if (!processTypeFromString(text, task.processType)) {
        if (error != nullptr) *error = "KeyStation.processType is invalid.";
        return false;
    }
    if (!requireString(object, "level", text, "Must", error)) return false;
    if (!requirementLevelFromString(text, task.level)) {
        if (error != nullptr) *error = "PoseTask.level is invalid.";
        return false;
    }
    if (!requireString(object, "source", text, "Manual", error)) return false;
    if (!poseTaskSourceFromString(text, task.source)) {
        if (error != nullptr) *error = "PoseTask.source is invalid.";
        return false;
    }
    if (!requireString(object, "refFrame", task.refFrame, "WORLD", error) ||
        !requireString(object, "tcpFrame", task.tcpFrame, "", error)) return false;
    if (!readArray(object, "position", task.position, error) ||
        !readArray(object, "rpyDeg", task.rpyDeg, error))
        return false;
    if (!requireNumber(object, "positionToleranceMeters", task.tolerance.positionMeters, 0.001, error) ||
        !requireNumber(object, "orientationToleranceDeg", task.tolerance.orientationDeg, 1.0, error) ||
        !requireBool(object, "allowToolRollFree", task.tolerance.allowToolRollFree, false, error)) return false;
    QJsonObject geometryFeature;
    if (!requireObject(object, "geometryFeature", geometryFeature, error)) return false;
    if (!requireString(geometryFeature, "type", text, "None", error)) return false;
    if (!geometryFeatureTypeFromString(text, task.geometryFeature.type)) {
        if (error != nullptr) *error = "KeyStation.geometryFeature.type is invalid.";
        return false;
    }
    if (!requireString(geometryFeature, "frameName", task.geometryFeature.frameName, "", error) ||
        !requireString(geometryFeature, "objectName", task.geometryFeature.objectName, "", error) ||
        !requireString(geometryFeature, "geometryName", task.geometryFeature.geometryName, "", error)) return false;
    QJsonObject generation;
    if (!requireObject(object, "generation", generation, error) ||
        !requireString(generation, "generatorId", task.generation.generatorId, "", error) ||
        !requireString(generation, "instanceId", task.generation.instanceId, "", error) ||
        !requireBool(generation, "linked", task.generation.linked, false, error)) return false;
    task.generation.parameters.clear();
    QJsonArray generationParameters;
    if (!requireArray(generation, "parameters", generationParameters, error)) return false;
    for (const QJsonValue& value : generationParameters) {
        if (!value.isObject()) {
            if (error != nullptr) *error = "generation.parameters entries must be objects.";
            return false;
        }
        const QJsonObject parameter = value.toObject();
        std::string key;
        std::string parameterValue;
        if (!requireString(parameter, "key", key, "", error) ||
            !requireString(parameter, "value", parameterValue, "", error)) return false;
        if (key.empty()) {
            if (error != nullptr) *error = "KeyStation.generation parameter key cannot be empty.";
            return false;
        }
        task.generation.parameters.push_back({key, parameterValue});
    }
    QJsonObject importProvenance;
    if (!requireObject(object, "importProvenance", importProvenance, error) ||
        !requireString(importProvenance, "sourcePath", task.importProvenance.sourcePath, "", error) ||
        !requireInteger(importProvenance, "recordNumber", task.importProvenance.recordNumber, 0, error)) return false;
    if (task.importProvenance.recordNumber < 0) {
        if (error != nullptr) *error = "KeyStation.importProvenance.recordNumber cannot be negative.";
        return false;
    }
    QJsonObject orientation;
    if (!requireObject(object, "orientation", orientation, error) ||
        !requireString(orientation, "mode", text, "Fixed", error)) return false;
    if (!orientationModeFromString(text, task.orientation.mode)) {
        if (error != nullptr) *error = "KeyStation.orientation.mode is invalid.";
        return false;
    }
    if (!requireString(orientation, "targetFrame", task.orientation.targetFrame, "", error) ||
        !requireString(orientation, "targetGeometry", task.orientation.targetGeometry, "", error) ||
        !requireString(orientation, "targetPoint", task.orientation.targetPoint, "", error) ||
        !requireString(orientation, "resolutionEvidence", task.orientation.resolutionEvidence, "", error) ||
        !requireBool(orientation, "invertNormal", task.orientation.invertNormal, false, error) ||
        !requireBool(orientation, "allowToolRollFree", task.orientation.allowToolRollFree,
                     task.tolerance.allowToolRollFree, error) ||
        !requireNumber(orientation, "rollMinimumDeg", task.orientation.rollMinimumDeg, -180.0, error) ||
        !requireNumber(orientation, "rollMaximumDeg", task.orientation.rollMaximumDeg, 180.0, error)) return false;
    const auto readPathRule = [error] (const QJsonObject& value, ApproachRetractRule& rule) {
        std::string axis;
        if (!requireBool(value, "enabled", rule.enabled, false, error) ||
            !requireString(value, "axis", axis, "ToolZ", error)) return false;
        if (!offsetAxisFromString(axis, rule.axis)) {
            if (error != nullptr) *error = "KeyStation approach/retract axis is invalid.";
            return false;
        }
        if (!requireNumber(value, "distanceMeters", rule.distanceMeters, 0.0, error) ||
            !requireBool(value, "collisionFreeRequired", rule.collisionFreeRequired, true, error)) return false;
        return std::isfinite(rule.distanceMeters);
    };
    QJsonObject approach;
    QJsonObject retract;
    if (!requireObject(object, "approach", approach, error) ||
        !requireObject(object, "retract", retract, error) ||
        !readPathRule(approach, task.approach) || !readPathRule(retract, task.retract))
        return false;
    QJsonObject validation;
    if (!requireObject(object, "validation", validation, error) ||
        !requireBool(validation, "collisionFreeRequired", task.validation.collisionFreeRequired, true, error) ||
        !requireNumber(validation, "minimumJointMargin", task.validation.minimumJointMargin, 0.0, error) ||
        !requireNumber(validation, "minimumManipulability", task.validation.minimumManipulability, 0.0, error) ||
        !requireNumber(object, "confidence", task.confidence, 1.0, error) ||
        !requireString(object, "note", task.note, "", error)) return false;
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
    object["sampleSpacingMeters"] = writeArray(region.sampleSpacingMeters);
    object["tcpFrame"] = QString::fromStdString(region.tcpFrame);
    object["orientationMode"] = toString(region.orientationMode);
    object["orientationTargetFrame"] = QString::fromStdString(region.orientationTargetFrame);
    object["orientationTargetGeometry"] = QString::fromStdString(region.orientationTargetGeometry);
    object["orientationTargetPoint"] = QString::fromStdString(region.orientationTargetPoint);
    object["fixedRpyDeg"] = writeArray(region.fixedRpyDeg);
    object["directionSamples"] = region.directionSamples;
    object["rollSamples"] = region.rollSamples;
    object["minimumOrientationCoverage"] = region.minimumOrientationCoverage;
    object["minimumVerificationStage"] =
        region.minimumVerificationStage == RequirementVerificationStage::Verified ? "Verified" : "Quick";
    object["collisionFreeRequired"] = region.collisionFreeRequired;
    object["positionToleranceMeters"] = region.positionToleranceMeters;
    object["orientationToleranceDeg"] = region.orientationToleranceDeg;
    object["minimumJointMargin"] = region.minimumJointMargin;
    object["minimumManipulability"] = region.minimumManipulability;
    writeExtensions(object, region.extensions,
                    {"id", "name", "level", "refFrame", "center", "size", "minimumCoverage",
                     "sampleSpacingMeters", "tcpFrame", "orientationMode", "orientationTargetFrame",
                     "orientationTargetGeometry", "orientationTargetPoint", "fixedRpyDeg",
                     "directionSamples", "rollSamples", "minimumOrientationCoverage",
                     "minimumVerificationStage", "collisionFreeRequired", "positionToleranceMeters",
                     "orientationToleranceDeg", "minimumJointMargin", "minimumManipulability"});
    return object;
}

bool readBoxRegion(const QJsonObject& object, BoxRegion& region, std::string* error)
{
    if (!readExtensions(object,
                        {"id", "name", "level", "refFrame", "center", "size", "minimumCoverage",
                         "sampleSpacingMeters", "samplesPerAxis", "tcpFrame", "orientationMode", "orientationTargetFrame",
                         "orientationTargetGeometry", "orientationTargetPoint", "fixedRpyDeg",
                         "directionSamples", "rollSamples", "minimumOrientationCoverage",
                         "minimumVerificationStage", "collisionFreeRequired", "positionToleranceMeters",
                         "orientationToleranceDeg", "minimumJointMargin", "minimumManipulability"},
                        region.extensions, error)) return false;
    std::string text;
    if (!requireString(object, "id", region.id, "", error) ||
        !requireString(object, "name", region.name, "", error) ||
        !requireString(object, "level", text, "Must", error)) return false;
    if (!requirementLevelFromString(text, region.level)) {
        if (error != nullptr) *error = "BoxRegion.level is invalid.";
        return false;
    }
    if (!requireString(object, "refFrame", region.refFrame, "WORLD", error)) return false;
    if (!readArray(object, "center", region.center, error) ||
        !readArray(object, "size", region.size, error))
        return false;
    if (!requireNumber(object, "minimumCoverage", region.minimumCoverage, 0.8, error) ||
        !requireString(object, "tcpFrame", region.tcpFrame, "", error) ||
        !requireString(object, "orientationMode", text, "Fixed", error)) return false;
    if (object.contains("sampleSpacingMeters")) {
        if (!readArray(object, "sampleSpacingMeters", region.sampleSpacingMeters, error)) return false;
    } else if (object.contains("samplesPerAxis")) {
        int legacySamples = 5;
        if (!requireInteger(object, "samplesPerAxis", legacySamples, 5, error)) return false;
        for (std::size_t axis = 0; axis < region.size.size(); ++axis)
            region.sampleSpacingMeters[axis] = region.size[axis] /
                static_cast<double>(std::max(legacySamples - 1, 1));
    }
    if (!orientationModeFromString(text, region.orientationMode)) {
        if (error != nullptr) *error = "BoxRegion.orientationMode is invalid.";
        return false;
    }
    if (!requireString(object, "orientationTargetFrame", region.orientationTargetFrame, "", error) ||
        !requireString(object, "orientationTargetGeometry", region.orientationTargetGeometry, "", error) ||
        !requireString(object, "orientationTargetPoint", region.orientationTargetPoint, "", error)) return false;
    if (object.contains("fixedRpyDeg") && !readArray(object, "fixedRpyDeg", region.fixedRpyDeg, error)) return false;
    if (!requireInteger(object, "directionSamples", region.directionSamples, 1, error) ||
        !requireInteger(object, "rollSamples", region.rollSamples, 1, error) ||
        !requireNumber(object, "minimumOrientationCoverage", region.minimumOrientationCoverage, 0.0, error) ||
        !requireString(object, "minimumVerificationStage", text, "Verified", error)) return false;
    const QString stage = QString::fromStdString(text);
    if (stage == "Quick") region.minimumVerificationStage = RequirementVerificationStage::Quick;
    else if (stage == "Verified") region.minimumVerificationStage = RequirementVerificationStage::Verified;
    else {
        if (error != nullptr) *error = "BoxRegion.minimumVerificationStage is invalid.";
        return false;
    }
    if (!requireBool(object, "collisionFreeRequired", region.collisionFreeRequired, true, error) ||
        !requireNumber(object, "positionToleranceMeters", region.positionToleranceMeters, 0.001, error) ||
        !requireNumber(object, "orientationToleranceDeg", region.orientationToleranceDeg, 1.0, error) ||
        !requireNumber(object, "minimumJointMargin", region.minimumJointMargin, 0.0, error) ||
        !requireNumber(object, "minimumManipulability", region.minimumManipulability, 0.0, error)) return false;
    return std::isfinite(region.minimumCoverage) &&
           std::isfinite(region.minimumOrientationCoverage) &&
           std::isfinite(region.positionToleranceMeters) &&
           std::isfinite(region.orientationToleranceDeg) &&
           std::isfinite(region.minimumJointMargin) &&
           std::isfinite(region.minimumManipulability);
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
    writeExtensions(object, requirements.extensions,
                    {"schemaVersion", "name", "version", "frozen", "modelBinding", "poseTasks", "boxRegions"});
    return object;
}

bool RequirementSetJson::fromObject(const QJsonObject& object, RequirementSet& requirements,
                                    std::string* error)
{
    RequirementSet parsed;
    if (!readExtensions(object,
                        {"schemaVersion", "name", "version", "frozen", "modelBinding", "poseTasks", "boxRegions"},
                        parsed.extensions, error)) return false;
    if (!requireInteger(object, "schemaVersion", parsed.schemaVersion, 1, error) ||
        !requireString(object, "name", parsed.name, "", error) ||
        !requireInteger(object, "version", parsed.version, 1, error) ||
        !requireBool(object, "frozen", parsed.frozen, false, error)) return false;
    QJsonObject binding;
    if (!requireObject(object, "modelBinding", binding, error) ||
        !requireString(binding, "sourcePath", parsed.modelBinding.sourcePath, "", error) ||
        !requireString(binding, "robotModelFingerprint", parsed.modelBinding.robotModelFingerprint, "", error) ||
        !requireString(binding, "robotName", parsed.modelBinding.robotName, "", error)) return false;
    QJsonArray poseValues;
    QJsonArray regionValues;
    if (!requireArray(object, "poseTasks", poseValues, error) ||
        !requireArray(object, "boxRegions", regionValues, error)) return false;
    for (const QJsonValue& value : poseValues) {
        if (!value.isObject()) {
            if (error != nullptr) *error = "poseTasks entries must be objects.";
            return false;
        }
        PoseTask task;
        if (!readPoseTask(value.toObject(), task, error)) return false;
        parsed.poseTasks.push_back(task);
    }
    for (const QJsonValue& value : regionValues) {
        if (!value.isObject()) {
            if (error != nullptr) *error = "boxRegions entries must be objects.";
            return false;
        }
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
