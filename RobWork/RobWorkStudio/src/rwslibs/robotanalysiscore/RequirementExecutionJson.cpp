#include "RequirementExecutionJson.hpp"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>

#include <cmath>
#include <set>

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

// —— 严格 JSON 字段类型校验辅助函数族 ——
// 执行契约是下游分析直接消费的审计数据：字段缺失或被换成其他 JSON 类型时，
// toDouble/toBool/toString 会静默返回默认值，掩盖损坏或篡改的工件。因此解析前先
// 用这些函数显式核对字段类型，类型不符即失败并回填包含字段名的可操作错误。
// 通用类型检查：字段存在且类型匹配才通过；未定义(缺失)也视为失败。
bool requireType(const QJsonObject& object, const char* key, QJsonValue::Type type,
                 std::string* error)
{
    const QJsonValue value = object.value(key);
    if (!value.isUndefined() && value.type() == type) return true;
    if (error != nullptr)
        *error = std::string("Requirement execution field '") + key + "' is missing or has the wrong type.";
    return false;
}

// 要求字段为字符串。
bool requireString(const QJsonObject& object, const char* key, std::string* error)
{
    return requireType(object, key, QJsonValue::String, error);
}

// 要求字段为数字(QJson 中整数也以 Double 类型存储)。
bool requireNumber(const QJsonObject& object, const char* key, std::string* error)
{
    return requireType(object, key, QJsonValue::Double, error);
}

// 要求字段为整数值：先确保是数字，再校验其有限且无小数部分。
bool requireInteger(const QJsonObject& object, const char* key, std::string* error)
{
    if (!requireNumber(object, key, error)) return false;
    const double value = object.value(key).toDouble();
    if (!std::isfinite(value) || std::floor(value) != value) {
        if (error != nullptr)
            *error = std::string("Requirement execution field '") + key + "' must be an integer.";
        return false;
    }
    return true;
}

// 要求字段为布尔值。
bool requireBool(const QJsonObject& object, const char* key, std::string* error)
{
    return requireType(object, key, QJsonValue::Bool, error);
}

// 要求字段为 JSON 数组。
bool requireArray(const QJsonObject& object, const char* key, std::string* error)
{
    return requireType(object, key, QJsonValue::Array, error);
}

// 要求字段为 JSON 对象。
bool requireObject(const QJsonObject& object, const char* key, std::string* error)
{
    return requireType(object, key, QJsonValue::Object, error);
}

// 读取固定长度 double 数组：先确认字段是数组且长度恰为 N，再逐元素校验类型
// 与有限性。非数字元素会在此被拒绝，而不是被 toDouble 静默转为 0。
template <std::size_t N>
bool readArray(const QJsonObject& object, const char* key,
               std::array<double, N>& values, std::string* error)
{
    if (!requireArray(object, key, error)) return false;
    const QJsonArray array = object.value(key).toArray();
    if (array.size() != static_cast<int>(N)) {
        if (error != nullptr) *error = std::string(key) + " must contain " +
            std::to_string(N) + " values.";
        return false;
    }
    for (std::size_t index = 0; index < N; ++index) {
        const QJsonValue element = array.at(static_cast<int>(index));
        // 显式类型校验：只接受 Double，拒绝字符串/布尔等被 toDouble 吞掉的错误类型。
        if (!element.isDouble()) {
            if (error != nullptr)
                *error = std::string(key) + " contains a value with the wrong type.";
            return false;
        }
        const double value = element.toDouble();
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
                          std::string* error);

// 序列化执行态条目溯源：sourceId/sourceKind 为字符串，关联诊断写入 diagnostics 数组。
QJsonObject provenanceToObject(const RequirementItemProvenance& value)
{
    QJsonObject object;
    object["sourceId"] = QString::fromStdString(value.sourceId);
    object["sourceKind"] = QString::fromStdString(value.sourceKind);
    QJsonArray diagnostics;
    for (const auto& diagnostic : value.diagnostics)
        diagnostics.append(diagnosticToObject(diagnostic));
    object["diagnostics"] = diagnostics;
    return object;
}

// 反序列化执行态条目溯源：sourceId/sourceKind 必须为字符串，diagnostics 必须为数组
// 且每个元素都是对象；任一结构不符即拒绝。
bool provenanceFromObject(const QJsonObject& object, RequirementItemProvenance& value,
                          std::string* error)
{
    if (!requireString(object, "sourceId", error) ||
        !requireString(object, "sourceKind", error) ||
        !object.contains("diagnostics") || !object.value("diagnostics").isArray()) {
        if (error != nullptr && error->empty())
            *error = "Requirement item provenance is incomplete.";
        return false;
    }
    value.sourceId = object.value("sourceId").toString().toStdString();
    value.sourceKind = object.value("sourceKind").toString().toStdString();
    for (const QJsonValue& item : object.value("diagnostics").toArray()) {
        if (!item.isObject()) {
            if (error != nullptr) *error = "Requirement item provenance diagnostic must be an object.";
            return false;
        }
        RequirementExecutionDiagnostic diagnostic;
        if (!diagnosticFromObject(item.toObject(), diagnostic, error)) return false;
        value.diagnostics.push_back(diagnostic);
    }
    return true;
}

bool diagnosticFromObject(const QJsonObject& object,
                          RequirementExecutionDiagnostic& value,
                          std::string* error)
{
    // 诊断的所有文本字段必须显式存在且为字符串；severity 枚举值必须在合法集合内。
    for (const char* key : {"code", "severity", "requirementId", "field", "message", "source"})
        if (!requireString(object, key, error)) return false;
    value.code = object.value("code").toString().toStdString();
    value.requirementId = object.value("requirementId").toString().toStdString();
    value.field = object.value("field").toString().toStdString();
    value.message = object.value("message").toString().toStdString();
    value.source = object.value("source").toString().toStdString();
    if (!severityFromString(object.value("severity").toString(), value.severity)) {
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
    // 逐工位序列化条目级溯源(来源 + 关联诊断)。
    object["provenance"] = provenanceToObject(value.provenance);
    writeExtensions(object, value.extensions,
                    {"id", "name", "level", "compileState", "processType", "excludedReason",
                     "refFrame", "tcpFrame", "position", "rpyDeg", "positionToleranceMeters",
                     "orientationToleranceDeg", "allowToolRollFree", "orientationMode",
                     "orientationTargetFrame", "orientationTargetGeometry", "orientationTargetPoint",
                     "invertNormal", "rollMinimumDeg", "rollMaximumDeg", "collisionFreeRequired",
                     "minimumJointMargin", "minimumManipulability", "resolutionEvidence", "approach",
                     "retract", "pathValidationPending", "provenance", "diagnostics"});
}

bool validateExecutionTask(const RequirementExecutionTask& task, std::string* error);
bool validateExecutionRegion(const RequirementExecutionRegion& region, std::string* error);

bool readCommon(const QJsonObject& object, RequirementExecutionTask& value, std::string* error)
{
    if (!readExtensions(object,
                        {"id", "name", "level", "compileState", "processType", "excludedReason",
                         "refFrame", "tcpFrame", "position", "rpyDeg", "positionToleranceMeters",
                         "orientationToleranceDeg", "allowToolRollFree", "orientationMode",
                         "orientationTargetFrame", "orientationTargetGeometry", "orientationTargetPoint",
                         "invertNormal", "rollMinimumDeg", "rollMaximumDeg", "collisionFreeRequired",
                         "minimumJointMargin", "minimumManipulability", "resolutionEvidence", "approach",
                         "retract", "pathValidationPending", "provenance", "diagnostics"},
                        value.extensions, error)) return false;
    // 字符串字段：id/name/excludedReason/refFrame/tcpFrame 及枚举、朝向目标、证据等。
    for (const char* key : {"id", "name", "excludedReason", "refFrame", "tcpFrame",
                            "level", "compileState", "processType", "orientationMode",
                            "orientationTargetFrame", "orientationTargetGeometry",
                            "orientationTargetPoint", "resolutionEvidence"})
        if (!requireString(object, key, error)) return false;
    // 数值字段：容差、翻滚范围、关节裕量、可操作度等。
    for (const char* key : {"positionToleranceMeters", "orientationToleranceDeg",
                            "rollMinimumDeg", "rollMaximumDeg", "minimumJointMargin",
                            "minimumManipulability"})
        if (!requireNumber(object, key, error)) return false;
    // 布尔字段：工具翻滚自由、法向翻转、碰撞自由要求、路径待验证。
    for (const char* key : {"allowToolRollFree", "invertNormal", "collisionFreeRequired",
                            "pathValidationPending"})
        if (!requireBool(object, key, error)) return false;
    // 嵌套的接近/撤离路径规则必须是对象。
    if (!requireObject(object, "approach", error) || !requireObject(object, "retract", error))
        return false;
    // 条目级溯源必须存在且通过严格反序列化。
    if (!requireObject(object, "provenance", error) ||
        !provenanceFromObject(object.value("provenance").toObject(), value.provenance, error))
        return false;
    value.id = object.value("id").toString().toStdString();
    value.name = object.value("name").toString().toStdString();
    value.excludedReason = object.value("excludedReason").toString().toStdString();
    value.refFrame = object.value("refFrame").toString("WORLD").toStdString();
    value.tcpFrame = object.value("tcpFrame").toString().toStdString();
    // 枚举字段必须落在合法取值集合内(不再允许缺省回填后悄悄通过)。
    if (!levelFromString(object.value("level").toString(), value.level) ||
        !compileStateFromString(object.value("compileState").toString(), value.compileState) ||
        !processTypeFromString(object.value("processType").toString(), value.processType) ||
        !orientationFromString(object.value("orientationMode").toString(), value.orientationMode)) {
        if (error != nullptr) *error = "Requirement execution task enum is invalid.";
        return false;
    }
    if (!readArray(object, "position", value.position, error) ||
        !readArray(object, "rpyDeg", value.rpyDeg, error)) return false;
    // 数值字段严格读取：字段已由上面 require* 保证类型，这里不再回填默认值。
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
    // 路径规则严格读取：四个字段全部必须显式存在且类型正确；axis 枚举必须合法。
    const auto readPath = [&] (const QJsonObject& path, RequirementExecutionPathRule& rule) {
        if (!requireBool(path, "enabled", error) || !requireString(path, "axis", error) ||
            !requireNumber(path, "distanceMeters", error) ||
            !requireBool(path, "collisionFreeRequired", error)) return false;
        rule.enabled = path.value("enabled").toBool(false);
        if (!offsetAxisFromString(path.value("axis").toString(), rule.axis)) {
            if (error != nullptr) *error = "Requirement execution path axis is invalid.";
            return false;
        }
        rule.distanceMeters = path.value("distanceMeters").toDouble();
        rule.collisionFreeRequired = path.value("collisionFreeRequired").toBool();
        return true;
    };
    if (!readPath(object.value("approach").toObject(), value.approach) ||
        !readPath(object.value("retract").toObject(), value.retract)) return false;
    value.pathValidationPending = object.value("pathValidationPending").toBool();
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
    // 身份是最具可操作性的结构诊断：先于契约其余字段检查 id，避免畸形记录因后续
    // 字段缺失而无法定位到具体是哪个需求条目非法。
    if (!requireString(object, "id", error)) return false;
    if (object.value("id").toString().trimmed().isEmpty()) {
        if (error != nullptr) *error = "Requirement execution task id is required.";
        return false;
    }
    if (!readCommon(object, value, error)) return false;
    // 先校验身份与标量契约，再检查嵌套诊断：当多个字段同时畸形时，调用方收到最
    // 可操作的错误(例如空的工位 id)，而不是被嵌套结构的错误淹没。
    if (!validateExecutionTask(value, error)) return false;
    // 诊断数组必须存在且元素为对象。
    if (!object.contains("diagnostics") || !object.value("diagnostics").isArray()) {
        if (error != nullptr) *error = "Requirement execution task diagnostics must be an array.";
        return false;
    }
    for (const auto& item : object.value("diagnostics").toArray()) {
        if (!item.isObject()) {
            if (error != nullptr) *error = "Requirement execution task diagnostic must be an object.";
            return false;
        }
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
    // 逐覆盖盒序列化条目级溯源。
    object["provenance"] = provenanceToObject(value.provenance);
    QJsonArray diagnostics;
    for (const auto& item : value.diagnostics) diagnostics.append(diagnosticToObject(item));
    object["diagnostics"] = diagnostics;
    writeExtensions(object, value.extensions,
                    {"id", "name", "level", "compileState", "excludedReason", "refFrame", "tcpFrame",
                     "center", "size", "minimumCoverage", "samplesPerAxis", "orientationMode",
                     "orientationTargetFrame", "orientationTargetGeometry", "orientationTargetPoint",
                     "fixedRpyDeg", "directionSamples", "rollSamples", "minimumOrientationCoverage",
                     "minimumVerificationStage", "collisionFreeRequired", "positionToleranceMeters",
                     "orientationToleranceDeg", "minimumJointMargin", "minimumManipulability",
                     "provenance", "diagnostics"});
    return object;
}

bool regionFromObject(const QJsonObject& object, RequirementExecutionRegion& value, std::string* error)
{
    if (!readExtensions(object,
                        {"id", "name", "level", "compileState", "excludedReason", "refFrame", "tcpFrame",
                         "center", "size", "minimumCoverage", "samplesPerAxis", "orientationMode",
                         "orientationTargetFrame", "orientationTargetGeometry", "orientationTargetPoint",
                         "fixedRpyDeg", "directionSamples", "rollSamples", "minimumOrientationCoverage",
                         "minimumVerificationStage", "collisionFreeRequired", "positionToleranceMeters",
                         "orientationToleranceDeg", "minimumJointMargin", "minimumManipulability",
                         "provenance", "diagnostics"},
                        value.extensions, error)) return false;
    // 字符串字段：id/name/excludedReason/refFrame/tcpFrame 及枚举、朝向目标、验证阶段。
    for (const char* key : {"id", "name", "excludedReason", "refFrame", "tcpFrame",
                            "level", "compileState", "orientationMode",
                            "orientationTargetFrame", "orientationTargetGeometry",
                            "orientationTargetPoint", "minimumVerificationStage"})
        if (!requireString(object, key, error)) return false;
    // 覆盖率/朝向覆盖率/容差等数值字段。
    for (const char* key : {"minimumCoverage", "minimumOrientationCoverage",
                            "positionToleranceMeters", "orientationToleranceDeg",
                            "minimumJointMargin", "minimumManipulability"})
        if (!requireNumber(object, key, error)) return false;
    // 采样计数必须是整数(网格/方向/翻滚样本数)。
    for (const char* key : {"samplesPerAxis", "directionSamples", "rollSamples"})
        if (!requireInteger(object, key, error)) return false;
    // 固定数组与布尔字段。
    if (!requireArray(object, "center", error) || !requireArray(object, "size", error) ||
        !requireArray(object, "fixedRpyDeg", error) ||
        !requireBool(object, "collisionFreeRequired", error)) return false;
    // 条目级溯源必须存在且通过严格反序列化。
    if (!requireObject(object, "provenance", error) ||
        !provenanceFromObject(object.value("provenance").toObject(), value.provenance, error))
        return false;
    value.id = object.value("id").toString().toStdString();
    value.name = object.value("name").toString().toStdString();
    value.excludedReason = object.value("excludedReason").toString().toStdString();
    value.refFrame = object.value("refFrame").toString("WORLD").toStdString();
    value.tcpFrame = object.value("tcpFrame").toString().toStdString();
    // 枚举字段必须在合法取值集合内。
    if (!levelFromString(object.value("level").toString(), value.level) ||
        !compileStateFromString(object.value("compileState").toString(), value.compileState) ||
        !orientationFromString(object.value("orientationMode").toString(), value.orientationMode) ||
        !stageFromString(object.value("minimumVerificationStage").toString(), value.minimumVerificationStage)) {
        if (error != nullptr) *error = "Requirement execution region enum is invalid.";
        return false;
    }
    if (!readArray(object, "center", value.center, error) ||
        !readArray(object, "size", value.size, error) ||
        !readArray(object, "fixedRpyDeg", value.fixedRpyDeg, error)) return false;
    // 数值严格读取：类型已由 require* 保证，不再回填默认值。
    value.minimumCoverage = object.value("minimumCoverage").toDouble();
    value.samplesPerAxis = object.value("samplesPerAxis").toInt();
    value.orientationTargetFrame = object.value("orientationTargetFrame").toString().toStdString();
    value.orientationTargetGeometry = object.value("orientationTargetGeometry").toString().toStdString();
    value.orientationTargetPoint = object.value("orientationTargetPoint").toString().toStdString();
    value.directionSamples = object.value("directionSamples").toInt();
    value.rollSamples = object.value("rollSamples").toInt();
    value.minimumOrientationCoverage = object.value("minimumOrientationCoverage").toDouble();
    value.collisionFreeRequired = object.value("collisionFreeRequired").toBool();
    value.positionToleranceMeters = object.value("positionToleranceMeters").toDouble();
    value.orientationToleranceDeg = object.value("orientationToleranceDeg").toDouble();
    value.minimumJointMargin = object.value("minimumJointMargin").toDouble();
    value.minimumManipulability = object.value("minimumManipulability").toDouble();
    // 先做语义校验(枚举合法性)，再校验取值范围(覆盖率、采样下限与上限、有限性)。
    if (!validateExecutionRegion(value, error)) return false;
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
    if (!object.contains("diagnostics") || !object.value("diagnostics").isArray()) {
        if (error != nullptr) *error = "Requirement execution region diagnostics must be an array.";
        return false;
    }
    for (const auto& item : object.value("diagnostics").toArray()) {
        if (!item.isObject()) {
            if (error != nullptr) *error = "Requirement execution region diagnostic must be an object.";
            return false;
        }
        RequirementExecutionDiagnostic diagnostic;
        if (!diagnosticFromObject(item.toObject(), diagnostic, error)) return false;
        value.diagnostics.push_back(diagnostic);
    }
    return true;
}

// 校验单条路径规则：axis 必须是合法枚举(ToolZ/ReferenceZ)，距离有限且非负。
bool validatePathRule(const RequirementExecutionPathRule& rule, std::string* error)
{
    if (rule.axis != RequirementExecutionOffsetAxis::ToolZ &&
        rule.axis != RequirementExecutionOffsetAxis::ReferenceZ) {
        if (error != nullptr) *error = "Requirement execution path axis is invalid.";
        return false;
    }
    if (!std::isfinite(rule.distanceMeters) || rule.distanceMeters < 0.0) {
        if (error != nullptr) *error = "Requirement execution path distance must be finite and non-negative.";
        return false;
    }
    return true;
}

// 语义校验执行契约工位：除必填字段外，逐项核对枚举取值(level/compileState/
// orientationMode/processType)、容差与限值(有限且非负、翻滚区间单调)以及朝向
// 目标完整性。非法值一律拒绝，防止枚举静态转换产生的越界值流入下游。
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
    // 枚举合法性：只接受已知枚举值。
    const bool validLevel = task.level == RequirementExecutionLevel::Must ||
        task.level == RequirementExecutionLevel::Should || task.level == RequirementExecutionLevel::Info;
    const bool validCompileState = task.compileState == RequirementExecutionCompileState::Included ||
        task.compileState == RequirementExecutionCompileState::Excluded ||
        task.compileState == RequirementExecutionCompileState::Invalid;
    const bool validOrientation = task.orientationMode == RequirementExecutionOrientationMode::Fixed ||
        task.orientationMode == RequirementExecutionOrientationMode::AlignFrame ||
        task.orientationMode == RequirementExecutionOrientationMode::AlignGeometryNormal ||
        task.orientationMode == RequirementExecutionOrientationMode::PointAtTarget;
    // processType 用整数区间判定，覆盖连续枚举的合法范围。
    const bool validProcessType = static_cast<int>(task.processType) >=
        static_cast<int>(RequirementExecutionProcessType::Generic) &&
        static_cast<int>(task.processType) <=
        static_cast<int>(RequirementExecutionProcessType::Handover);
    if (!validLevel || !validCompileState || !validOrientation || !validProcessType) {
        if (error != nullptr) *error = "Requirement execution task enum is invalid.";
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

// 语义校验执行契约工作区覆盖盒：必填字段、枚举合法性、几何/覆盖率/采样上下限
// (含 MaxExecutionWorkspace* 安全上限)与朝向目标完整性。拒绝任何非法值，防止
// 畸形或篡改的覆盖盒在采样分析中造成无界计算。
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
    // 枚举合法性：level/compileState/orientationMode/minimumVerificationStage 均须合法。
    const bool validLevel = region.level == RequirementExecutionLevel::Must ||
        region.level == RequirementExecutionLevel::Should || region.level == RequirementExecutionLevel::Info;
    const bool validCompileState = region.compileState == RequirementExecutionCompileState::Included ||
        region.compileState == RequirementExecutionCompileState::Excluded ||
        region.compileState == RequirementExecutionCompileState::Invalid;
    const bool validOrientation = region.orientationMode == RequirementExecutionOrientationMode::Fixed ||
        region.orientationMode == RequirementExecutionOrientationMode::AlignFrame ||
        region.orientationMode == RequirementExecutionOrientationMode::AlignGeometryNormal ||
        region.orientationMode == RequirementExecutionOrientationMode::PointAtTarget;
    const bool validStage = region.minimumVerificationStage == RequirementExecutionStage::Quick ||
        region.minimumVerificationStage == RequirementExecutionStage::Verified;
    if (!validLevel || !validCompileState || !validOrientation || !validStage) {
        if (error != nullptr) *error = "Requirement execution region enum is invalid.";
        return false;
    }
    // 几何/数值范围：尺寸各分量必须为正，覆盖率在 [0,1]，采样计数有下限(Verified
    // 阶段至少 2)与安全上限(MaxExecutionWorkspace*)，其余阈值有限且非负。
    if (!finiteArray(region.center) || !finiteArray(region.size) || !finiteArray(region.fixedRpyDeg) ||
        !std::isfinite(region.size[0]) || !std::isfinite(region.size[1]) ||
        !std::isfinite(region.size[2]) || region.size[0] <= 0.0 || region.size[1] <= 0.0 ||
        region.size[2] <= 0.0 || !std::isfinite(region.minimumCoverage) ||
        region.minimumCoverage < 0.0 || region.minimumCoverage > 1.0 ||
        region.samplesPerAxis < (region.minimumVerificationStage == RequirementExecutionStage::Verified ? 2 : 1) ||
        region.samplesPerAxis > MaxExecutionWorkspaceSamplesPerAxis ||
        region.directionSamples < 1 ||
        region.directionSamples > MaxExecutionWorkspaceDirectionSamples ||
        region.rollSamples < 1 || region.rollSamples > MaxExecutionWorkspaceRollSamples ||
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
    writeExtensions(object, value.extensions,
                    {"type", "schemaVersion", "provenance", "tasks", "workspaceRegions", "diagnostics"});
    return object;
}

bool RequirementExecutionJson::fromObject(const QJsonObject& object,
                                          RequirementExecutionSet& value,
                                          std::string* error)
{
    if (error != nullptr) error->clear();
    // 顶层类型必须显式存在且等于 RequirementExecutionSet(拒绝缺省回填)。
    if (!requireString(object, "type", error) ||
        object.value("type").toString() != "RequirementExecutionSet") {
        if (error != nullptr) *error = "Requirement execution JSON type is invalid.";
        return false;
    }
    RequirementExecutionSet parsed;
    if (!readExtensions(object,
                        {"type", "schemaVersion", "provenance", "tasks", "workspaceRegions", "diagnostics"},
                        parsed.extensions, error)) return false;
    // schemaVersion 必须显式为整数且等于 1。
    if (!requireInteger(object, "schemaVersion", error)) return false;
    parsed.schemaVersion = object.value("schemaVersion").toInt();
    if (parsed.schemaVersion != 1) {
        if (error != nullptr) *error = "Requirement execution schemaVersion is unsupported; expected 1.";
        return false;
    }
    // provenance 必须为对象，且七个来源成员全部显式存在并是字符串。
    if (!object.value("provenance").isObject()) {
        if (error != nullptr) *error = "Requirement execution JSON provenance must be an object.";
        return false;
    }
    const QJsonObject provenance = object.value("provenance").toObject();
    for (const char* key : {"requirementFingerprint", "robotModelFingerprint",
                            "workcellFingerprint", "environmentFingerprint",
                            "compilerVersion", "frozenAt", "sourcePath"}) {
        if (!requireString(provenance, key, error)) {
            if (error != nullptr && error->find("provenance") == std::string::npos)
                *error = "Requirement execution provenance member '" + std::string(key) + "' is missing or has the wrong type.";
            return false;
        }
    }
    // 三个顶层数组(tasks/workspaceRegions/diagnostics)必须显式存在，缺一即拒绝。
    for (const char* key : {"tasks", "workspaceRegions", "diagnostics"}) {
        if (!object.contains(key) || !object.value(key).isArray()) {
            if (error != nullptr) *error = std::string("Requirement execution JSON field is missing or not an array: ") + key;
            return false;
        }
    }
    parsed.provenance.requirementFingerprint = provenance.value("requirementFingerprint").toString().toStdString();
    parsed.provenance.robotModelFingerprint = provenance.value("robotModelFingerprint").toString().toStdString();
    parsed.provenance.workcellFingerprint = provenance.value("workcellFingerprint").toString().toStdString();
    parsed.provenance.environmentFingerprint = provenance.value("environmentFingerprint").toString().toStdString();
    parsed.provenance.compilerVersion = provenance.value("compilerVersion").toString().toStdString();
    parsed.provenance.frozenAt = provenance.value("frozenAt").toString().toStdString();
    parsed.provenance.sourcePath = provenance.value("sourcePath").toString().toStdString();
    // 数组元素逐个解析：每个元素必须为对象，且经各自严格的 fromObject 校验。
    for (const auto& item : object.value("tasks").toArray()) {
        if (!item.isObject()) {
            if (error != nullptr) *error = "Requirement execution task must be an object.";
            return false;
        }
        RequirementExecutionTask task;
        if (!taskFromObject(item.toObject(), task, error)) return false;
        parsed.tasks.push_back(task);
    }
    for (const auto& item : object.value("workspaceRegions").toArray()) {
        if (!item.isObject()) {
            if (error != nullptr) *error = "Requirement execution workspace region must be an object.";
            return false;
        }
        RequirementExecutionRegion region;
        if (!regionFromObject(item.toObject(), region, error)) return false;
        parsed.workspaceRegions.push_back(region);
    }
    for (const auto& item : object.value("diagnostics").toArray()) {
        if (!item.isObject()) {
            if (error != nullptr) *error = "Requirement execution diagnostic must be an object.";
            return false;
        }
        RequirementExecutionDiagnostic diagnostic;
        if (!diagnosticFromObject(item.toObject(), diagnostic, error)) return false;
        parsed.diagnostics.push_back(diagnostic);
    }
    // 整集合校验：覆盖工位/覆盖盒的语义校验与 id 唯一性检查。
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
    // 区分两种失败：JSON 语法错误(报告解析器错误)与根节点不是对象(明确说明结构要求)，
    // 让调用方无需猜测失败原因。
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error != nullptr) {
            *error = parseError.error != QJsonParseError::NoError
                ? parseError.errorString().toStdString()
                : "Requirement execution JSON root must be an object.";
        }
        return false;
    }
    return fromObject(document.object(), value, error);
}

} // namespace rws
