#include "StructureOptimizationJson.hpp"
#include "StructureOptimizationDocument.hpp"

#include "KinematicBaselineSnapshot.hpp"

#include "StructureOptimizationObjectiveProfile.hpp"
#include "StructureOptimizationTypes.hpp"

#include <rwslibs/robotanalysiscore/RobotAnalysisJson.hpp>
#include <rwslibs/robotanalysiscore/RequirementExecutionJson.hpp>
#include <rwslibs/robotmodelbuilder/RobotModelSpecJson.hpp>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCryptographicHash>

#include <rw/math/Rotation3D.hpp>

#include <cmath>
#include <algorithm>
#include <functional>
#include <limits>
#include <set>
#include <stdexcept>

namespace rws {

// =============================================================================
//  内部辅助函数
// =============================================================================

// JSON has no NaN or infinity literal.  Keep the policy explicit instead of
// relying on Qt's implementation-specific coercion: every non-finite value is
// persisted as JSON null.  Result metrics add a companion availability field
// at their call site so null never silently means a numeric zero.
static QJsonValue jsonFiniteNumber(double value)
{
    return std::isfinite(value) ? QJsonValue(value) : QJsonValue(QJsonValue::Null);
}

static void appendFiniteNumber(QJsonArray& array, double value)
{
    array.append(jsonFiniteNumber(value));
}

// Problem configuration numbers are inputs, rather than optional evaluation
// metrics. Reject null, strings, and non-finite values instead of allowing Qt's
// toDouble() coercion to silently turn malformed data into zero.
static bool readFiniteNumber(const QJsonObject& object, const char* key,
                             double fallback, double& output, std::string* error)
{
    const QLatin1String field(key);
    if (!object.contains(field)) {
        output = fallback;
        return true;
    }
    const QJsonValue value = object.value(field);
    if (!value.isDouble() || !std::isfinite(value.toDouble())) {
        if (error != nullptr)
            *error = "Field '" + std::string(key) + "' must be a finite JSON number.";
        return false;
    }
    output = value.toDouble();
    return true;
}

static bool readInteger(const QJsonObject& object, const char* key, int fallback,
                        int& output, std::string* error)
{
    const QLatin1String field(key);
    if (!object.contains(field)) {
        output = fallback;
        return true;
    }
    const QJsonValue value = object.value(field);
    const double number = value.toDouble(std::numeric_limits<double>::quiet_NaN());
    if (!value.isDouble() || !std::isfinite(number) || std::floor(number) != number ||
        number < static_cast<double>(std::numeric_limits<int>::min()) ||
        number > static_cast<double>(std::numeric_limits<int>::max())) {
        if (error != nullptr)
            *error = "Field '" + std::string(key) + "' must be a finite JSON integer.";
        return false;
    }
    output = static_cast<int>(number);
    return true;
}

static bool readUnsignedInteger(const QJsonObject& object, const char* key,
                                unsigned int fallback, unsigned int& output,
                                std::string* error)
{
    const QLatin1String field(key);
    if (!object.contains(field)) {
        output = fallback;
        return true;
    }
    const QJsonValue value = object.value(field);
    const double number = value.toDouble(std::numeric_limits<double>::quiet_NaN());
    if (!value.isDouble() || !std::isfinite(number) || std::floor(number) != number ||
        number < 0.0 || number > static_cast<double>(std::numeric_limits<unsigned int>::max())) {
        if (error != nullptr)
            *error = "Field '" + std::string(key) + "' must be a non-negative JSON integer.";
        return false;
    }
    output = static_cast<unsigned int>(number);
    return true;
}

static bool readBoolean(const QJsonObject& object, const char* key, bool fallback,
                        bool& output, std::string* error)
{
    const QLatin1String field(key);
    if (!object.contains(field)) {
        output = fallback;
        return true;
    }
    const QJsonValue value = object.value(field);
    if (!value.isBool()) {
        if (error != nullptr)
            *error = "Field '" + std::string(key) + "' must be a JSON boolean.";
        return false;
    }
    output = value.toBool();
    return true;
}

static bool isKnownProblemKey(const QString& key)
{
    static const char* const knownKeys[] = {
        "schemaVersion", "type", "context", "tasks",
        "engineeringRequirementProvenance", "requirementExecution",
        "frozenScenarioSnapshot", "variables", "constraints", "weights",
        "objectives", "metricConstraints", "evaluationConfig", "runConfig", "canonicalModelShadow"
    };
    for (const char* knownKey : knownKeys)
        if (key == QLatin1String(knownKey)) return true;
    return false;
}

// Preserve both the explicit extensions envelope and legacy top-level unknown
// fields.  A conflict is rejected because an extension must never override a
// field that has defined optimizer semantics.
static bool readProblemExtensions(const QJsonObject& object, QJsonObject& extensions,
                                  std::string* error)
{
    extensions = QJsonObject();
    if (object.contains("extensions")) {
        if (!object.value("extensions").isObject()) {
            if (error != nullptr) *error = "Structure optimization extensions must be an object.";
            return false;
        }
        const QJsonObject explicitExtensions = object.value("extensions").toObject();
        for (const QString& key : explicitExtensions.keys()) {
            if (isKnownProblemKey(key) || key == QLatin1String("extensions")) {
                if (error != nullptr)
                    *error = "Structure optimization extensions cannot override known field '" +
                             key.toStdString() + "'.";
                return false;
            }
            extensions.insert(key, explicitExtensions.value(key));
        }
    }
    for (const QString& key : object.keys()) {
        if (key == QLatin1String("extensions") || isKnownProblemKey(key)) continue;
        if (extensions.contains(key)) {
            if (error != nullptr)
                *error = "Structure optimization top-level extension field '" +
                         key.toStdString() + "' conflicts with explicit extensions.";
            return false;
        }
        extensions.insert(key, object.value(key));
    }
    return true;
}

static void writeProblemExtensions(QJsonObject& object, const QJsonObject& extensions)
{
    QJsonObject filtered;
    for (const QString& key : extensions.keys()) {
        if (!isKnownProblemKey(key) && key != QLatin1String("extensions"))
            filtered.insert(key, extensions.value(key));
    }
    if (!filtered.isEmpty()) object["extensions"] = filtered;
}

// 把设计变量种类枚举映射为稳定的 JSON 字符串。以文本而非数值序列化，
// 保证枚举值顺序调整或新增种类后仍能与旧项目文件兼容。
static QJsonObject variableKindToJson(StructureVariableKind kind)
{
    switch (kind) {
        case StructureVariableKind::JointPositionX:  return {{"kind", "JointPositionX"}};
        case StructureVariableKind::JointPositionY:  return {{"kind", "JointPositionY"}};
        case StructureVariableKind::JointPositionZ:  return {{"kind", "JointPositionZ"}};
        case StructureVariableKind::JointRotationRoll:  return {{"kind", "JointRotationRoll"}};
        case StructureVariableKind::JointRotationPitch: return {{"kind", "JointRotationPitch"}};
        case StructureVariableKind::JointRotationYaw:   return {{"kind", "JointRotationYaw"}};
        case StructureVariableKind::DhA:   return {{"kind", "DhA"}};
        case StructureVariableKind::DhD:   return {{"kind", "DhD"}};
        case StructureVariableKind::BaseHeight:  return {{"kind", "BaseHeight"}};
        case StructureVariableKind::TcpOffsetX:  return {{"kind", "TcpOffsetX"}};
        case StructureVariableKind::TcpOffsetY:  return {{"kind", "TcpOffsetY"}};
        case StructureVariableKind::TcpOffsetZ:  return {{"kind", "TcpOffsetZ"}};
        case StructureVariableKind::LinkRadius:  return {{"kind", "LinkRadius"}};
    case StructureVariableKind::LinkWidth:   return {{"kind", "LinkWidth"}};
    case StructureVariableKind::LinkHeight:  return {{"kind", "LinkHeight"}};
    case StructureVariableKind::LinkDimensionX: return {{"kind", "LinkDimensionX"}};
    case StructureVariableKind::LinkDimensionY: return {{"kind", "LinkDimensionY"}};
    case StructureVariableKind::LinkDimensionZ: return {{"kind", "LinkDimensionZ"}};
    }
    return {{"kind", "Unknown"}};
}

// 逆过程：按字符串恢复变量种类；ok 为可选的解析成功标志，遇到未知字符串时
// 置 false，让上层显式报错而不是静默回退到默认种类。
static StructureVariableKind variableKindFromJson(const QJsonObject& obj, bool* ok = nullptr)
{
    if (ok) *ok = true;
    if (!obj.contains("kind")) return StructureVariableKind::JointPositionX;
    const QString k = obj["kind"].toString();
    if (k == "JointPositionX")     return StructureVariableKind::JointPositionX;
    if (k == "JointPositionY")     return StructureVariableKind::JointPositionY;
    if (k == "JointPositionZ")     return StructureVariableKind::JointPositionZ;
    if (k == "JointRotationRoll")  return StructureVariableKind::JointRotationRoll;
    if (k == "JointRotationPitch") return StructureVariableKind::JointRotationPitch;
    if (k == "JointRotationYaw")   return StructureVariableKind::JointRotationYaw;
    if (k == "DhA")                return StructureVariableKind::DhA;
    if (k == "DhD")                return StructureVariableKind::DhD;
    if (k == "BaseHeight")         return StructureVariableKind::BaseHeight;
    if (k == "TcpOffsetX")         return StructureVariableKind::TcpOffsetX;
    if (k == "TcpOffsetY")         return StructureVariableKind::TcpOffsetY;
    if (k == "TcpOffsetZ")         return StructureVariableKind::TcpOffsetZ;
    if (k == "LinkRadius")         return StructureVariableKind::LinkRadius;
    if (k == "LinkWidth")          return StructureVariableKind::LinkWidth;
    if (k == "LinkHeight")         return StructureVariableKind::LinkHeight;
    if (k == "LinkDimensionX")     return StructureVariableKind::LinkDimensionX;
    if (k == "LinkDimensionY")     return StructureVariableKind::LinkDimensionY;
    if (k == "LinkDimensionZ")     return StructureVariableKind::LinkDimensionZ;
    if (ok) *ok = false;
    return StructureVariableKind::JointPositionX;
}

// 设计变量定义域（连续/整数/离散）到字符串，用于持久化。
static QString variableDomainToString(DesignVariableDomain domain)
{
    switch (domain) {
    case DesignVariableDomain::Continuous: return "Continuous";
    case DesignVariableDomain::Integer: return "Integer";
    case DesignVariableDomain::Discrete: return "Discrete";
    }
    return "Continuous";
}

// 逆映射：字段缺失按旧版本的 Continuous 默认值处理；未知字符串由调用者拒绝。
static DesignVariableDomain variableDomainFromString(const QString& value, bool* ok = nullptr)
{
    if (ok) *ok = true;
    if (value.isEmpty() || value == "Continuous") return DesignVariableDomain::Continuous;
    if (value == "Integer") return DesignVariableDomain::Integer;
    if (value == "Discrete") return DesignVariableDomain::Discrete;
    if (ok) *ok = false;
    return DesignVariableDomain::Continuous;
}

// 优化方向（最小化/最大化）到字符串。
static QString directionToString(OptimizationDirection direction)
{
    return direction == OptimizationDirection::Minimize ? "Minimize" : "Maximize";
}

// 逆映射：字段缺失按旧版本的 Maximize 默认值处理；未知字符串由调用者拒绝。
static OptimizationDirection directionFromString(const QString& value, bool* ok = nullptr)
{
    if (ok) *ok = true;
    if (value.isEmpty() || value == "Maximize") return OptimizationDirection::Maximize;
    if (value == "Minimize") return OptimizationDirection::Minimize;
    if (ok) *ok = false;
    return OptimizationDirection::Maximize;
}

// 指标比较运算符到字符串。
static QString comparisonToString(ComparisonOperator comparison)
{
    switch (comparison) {
    case ComparisonOperator::LessThanOrEqual: return "LessThanOrEqual";
    case ComparisonOperator::GreaterThanOrEqual: return "GreaterThanOrEqual";
    case ComparisonOperator::Equal: return "Equal";
    }
    return "GreaterThanOrEqual";
}

// 逆映射：字段缺失按旧版本的 GreaterThanOrEqual 默认值处理；未知字符串由调用者拒绝。
static ComparisonOperator comparisonFromString(const QString& value, bool* ok = nullptr)
{
    if (ok) *ok = true;
    if (value.isEmpty() || value == "GreaterThanOrEqual")
        return ComparisonOperator::GreaterThanOrEqual;
    if (value == "LessThanOrEqual") return ComparisonOperator::LessThanOrEqual;
    if (value == "Equal") return ComparisonOperator::Equal;
    if (ok) *ok = false;
    return ComparisonOperator::GreaterThanOrEqual;
}

// 结构约束种类到字符串。
static QJsonObject constraintKindToJson(StructureConstraintKind kind)
{
    switch (kind) {
        case StructureConstraintKind::ModelValid:               return {{"kind", "ModelValid"}};
        case StructureConstraintKind::RequiredTaskReachable:    return {{"kind", "RequiredTaskReachable"}};
        case StructureConstraintKind::RequiredTaskCollisionFree: return {{"kind", "RequiredTaskCollisionFree"}};
        case StructureConstraintKind::MinimumJointMargin:       return {{"kind", "MinimumJointMargin"}};
        case StructureConstraintKind::MaximumTotalLength:       return {{"kind", "MaximumTotalLength"}};
        case StructureConstraintKind::MaximumBaseHeight:        return {{"kind", "MaximumBaseHeight"}};
        case StructureConstraintKind::MaximumCrossSection:      return {{"kind", "MaximumCrossSection"}};
        case StructureConstraintKind::MaximumLinkSlenderness:   return {{"kind", "MaximumLinkSlenderness"}};
        case StructureConstraintKind::MinimumWorkspaceCoverage:  return {{"kind", "MinimumWorkspaceCoverage"}};
    }
    return {{"kind", "Unknown"}};
}

// 逆映射：未知字符串返回 ModelValid 并置 ok=false，避免静默接受错误类型。
static StructureConstraintKind constraintKindFromJson(const QJsonObject& obj, bool* ok = nullptr)
{
    if (ok) *ok = true;
    if (!obj.contains("kind")) return StructureConstraintKind::ModelValid;
    const QString k = obj["kind"].toString();
    if (k == "ModelValid")               return StructureConstraintKind::ModelValid;
    if (k == "RequiredTaskReachable")    return StructureConstraintKind::RequiredTaskReachable;
    if (k == "RequiredTaskCollisionFree") return StructureConstraintKind::RequiredTaskCollisionFree;
    if (k == "MinimumJointMargin")       return StructureConstraintKind::MinimumJointMargin;
    if (k == "MaximumTotalLength")       return StructureConstraintKind::MaximumTotalLength;
    if (k == "MaximumBaseHeight")        return StructureConstraintKind::MaximumBaseHeight;
    if (k == "MaximumCrossSection")      return StructureConstraintKind::MaximumCrossSection;
    if (k == "MaximumLinkSlenderness")   return StructureConstraintKind::MaximumLinkSlenderness;
    if (k == "MinimumWorkspaceCoverage") return StructureConstraintKind::MinimumWorkspaceCoverage;
    if (ok) *ok = false;
    return StructureConstraintKind::ModelValid;
}

// 候选解状态到字符串。
static QJsonObject candidateStatusToJson(StructureCandidateStatus s)
{
    switch (s) {
        case StructureCandidateStatus::Pending:    return {{"status", "Pending"}};
        case StructureCandidateStatus::Feasible:   return {{"status", "Feasible"}};
        case StructureCandidateStatus::Infeasible: return {{"status", "Infeasible"}};
        case StructureCandidateStatus::Failed:     return {{"status", "Failed"}};
        case StructureCandidateStatus::Canceled:   return {{"status", "Canceled"}};
    }
    return {{"status", "Unknown"}};
}

// 逆映射：缺少 status 仍按旧结果文件的 Pending 默认值处理；未知字符串必须由
// 调用方根据 ok 拒绝，不能把未来状态伪装成 Pending。
static StructureCandidateStatus candidateStatusFromJson(const QJsonObject& obj, bool* ok = nullptr)
{
    if (ok) *ok = true;
    if (!obj.contains("status")) return StructureCandidateStatus::Pending;
    const QString s = obj["status"].toString();
    if (s == "Pending")    return StructureCandidateStatus::Pending;
    if (s == "Feasible")   return StructureCandidateStatus::Feasible;
    if (s == "Infeasible") return StructureCandidateStatus::Infeasible;
    if (s == "Failed")     return StructureCandidateStatus::Failed;
    if (s == "Canceled")   return StructureCandidateStatus::Canceled;
    if (ok) *ok = false;
    return StructureCandidateStatus::Pending;
}

// =============================================================================
//  TaskPoint -> QJsonObject
// =============================================================================

static const char* taskPointTypeToString(TaskPointType type)
{
    switch (type) {
    case TaskPointType::Pick: return "Pick";
    case TaskPointType::Place: return "Place";
    case TaskPointType::Weld: return "Weld";
    case TaskPointType::Glue: return "Glue";
    case TaskPointType::Inspect: return "Inspect";
    case TaskPointType::Screw: return "Screw";
    case TaskPointType::Custom: return "Custom";
    case TaskPointType::Generic: default: return "Generic";
    }
}

static bool taskPointTypeFromString(const QString& value, TaskPointType& type)
{
    if (value == "Generic") type = TaskPointType::Generic;
    else if (value == "Pick") type = TaskPointType::Pick;
    else if (value == "Place") type = TaskPointType::Place;
    else if (value == "Weld") type = TaskPointType::Weld;
    else if (value == "Glue") type = TaskPointType::Glue;
    else if (value == "Inspect") type = TaskPointType::Inspect;
    else if (value == "Screw") type = TaskPointType::Screw;
    else if (value == "Custom") type = TaskPointType::Custom;
    else return false;
    return true;
}

// 任务点序列化：位置与姿态（RPY 角度）以数值数组保存，便于与旧版 RobWork
// 项目中的位姿语义对应，也避免浮点精度在文本往返中丢失。
static QJsonObject taskPointToJson(const TaskPoint& pt)
{
    QJsonObject obj;
    obj["id"]       = QString::fromStdString(pt.id);
    obj["name"]     = QString::fromStdString(pt.name);
    obj["type"]     = taskPointTypeToString(pt.type);
    obj["refFrame"] = QString::fromStdString(pt.refFrame);
    obj["tcpFrame"] = QString::fromStdString(pt.tcpFrame);
    QJsonArray pos;
    appendFiniteNumber(pos, pt.position[0]);
    appendFiniteNumber(pos, pt.position[1]);
    appendFiniteNumber(pos, pt.position[2]);
    obj["position"] = pos;
    QJsonArray rpy;
    appendFiniteNumber(rpy, pt.rpyDeg[0]);
    appendFiniteNumber(rpy, pt.rpyDeg[1]);
    appendFiniteNumber(rpy, pt.rpyDeg[2]);
    obj["rpyDeg"] = rpy;
    QJsonObject tolerance;
    tolerance["positionMeters"] = jsonFiniteNumber(pt.tolerance.positionMeters);
    tolerance["orientationDeg"] = jsonFiniteNumber(pt.tolerance.orientationDeg);
    tolerance["allowToolRollFree"] = pt.tolerance.allowToolRollFree;
    obj["tolerance"] = tolerance;
    obj["enabled"] = pt.enabled;
    obj["weight"]  = jsonFiniteNumber(pt.weight);
    obj["note"]    = QString::fromStdString(pt.note);
    return obj;
}

static bool readFiniteArray3(const QJsonObject& object, const char* key,
                             std::array<double, 3>& output, std::string* error)
{
    if (!object.contains(QLatin1String(key)))
        return true;
    const QJsonValue value = object.value(QLatin1String(key));
    if (!value.isArray() || value.toArray().size() != 3) {
        if (error != nullptr)
            *error = "Field '" + std::string(key) + "' must be an array of 3 finite numbers.";
        return false;
    }
    const QJsonArray array = value.toArray();
    for (int i = 0; i < 3; ++i) {
        const QJsonValue item = array.at(i);
        if (!item.isDouble() || !std::isfinite(item.toDouble())) {
            if (error != nullptr)
                *error = "Field '" + std::string(key) + "' must contain only finite numbers.";
            return false;
        }
        output[static_cast<std::size_t>(i)] = item.toDouble();
    }
    return true;
}

// 逆过程：缺失字段保持旧版本默认值；若字段存在则必须严格满足类型和有限数契约。
static bool taskPointFromJson(const QJsonObject& obj, TaskPoint& pt, std::string* error)
{
    pt.id       = obj["id"].toString().toStdString();
    pt.name     = obj["name"].toString().toStdString();
    if (obj.contains("type")) {
        if (!obj.value("type").isString() ||
            !taskPointTypeFromString(obj.value("type").toString(), pt.type)) {
            if (error != nullptr) *error = "Unknown TaskPointType value.";
            return false;
        }
    }
    pt.refFrame = obj["refFrame"].toString().toStdString();
    pt.tcpFrame = obj["tcpFrame"].toString().toStdString();
    if (!readFiniteArray3(obj, "position", pt.position, error) ||
        !readFiniteArray3(obj, "rpyDeg", pt.rpyDeg, error))
        return false;
    if (obj.contains("tolerance")) {
        if (!obj.value("tolerance").isObject()) {
            if (error != nullptr) *error = "TaskPoint tolerance must be an object.";
            return false;
        }
        const QJsonObject tolerance = obj.value("tolerance").toObject();
        if (!readFiniteNumber(tolerance, "positionMeters", pt.tolerance.positionMeters,
                              pt.tolerance.positionMeters, error) ||
            !readFiniteNumber(tolerance, "orientationDeg", pt.tolerance.orientationDeg,
                              pt.tolerance.orientationDeg, error))
            return false;
        if (tolerance.contains("allowToolRollFree")) {
            if (!tolerance.value("allowToolRollFree").isBool()) {
                if (error != nullptr) *error = "TaskPoint tolerance allowToolRollFree must be bool.";
                return false;
            }
            pt.tolerance.allowToolRollFree = tolerance.value("allowToolRollFree").toBool();
        }
    }
    if (!readBoolean(obj, "enabled", true, pt.enabled, error) ||
        !readFiniteNumber(obj, "weight", pt.weight, pt.weight, error))
        return false;
    pt.note = obj["note"].toString().toStdString();
    return true;
}

// =============================================================================
//  variable / constraint / weight / evalConfig / runConfig -> QJsonObject
// =============================================================================

// 设计变量序列化：除范围/步长外还保存离散选项与偏好信息，
// 保证优化参数在项目重载后完全可恢复。
static QJsonObject designVariableToJson(const StructureDesignVariable& var)
{
    QJsonObject obj;
    obj["id"]             = QString::fromStdString(var.id);
    obj["label"]          = QString::fromStdString(var.label);
    obj["targetName"]     = QString::fromStdString(var.targetName);
    obj["unit"]           = QString::fromStdString(var.unit);
    obj["kind"]           = variableKindToJson(var.kind)["kind"].toString();
    obj["currentValue"]   = jsonFiniteNumber(var.currentValue);
    obj["minimum"]        = jsonFiniteNumber(var.minimum);
    obj["maximum"]        = jsonFiniteNumber(var.maximum);
    obj["step"]           = jsonFiniteNumber(var.step);
    obj["preferredValue"] = jsonFiniteNumber(var.preferredValue);
    obj["preferenceWeight"] = jsonFiniteNumber(var.preferenceWeight);
    obj["enabled"]        = var.enabled;
    obj["syncAssociatedGeometry"] = var.syncAssociatedGeometry;
    obj["domain"] = variableDomainToString(var.domainDefinition.domain);
    QJsonArray discreteOptions;
    for (const std::string& option : var.domainDefinition.discreteOptions)
        discreteOptions.append(QString::fromStdString(option));
    obj["discreteOptions"] = discreteOptions;
    return obj;
}

// 逆过程：discreteOptions 缺省为空数组，不改变既有默认值。
static bool designVariableFromJson(const QJsonObject& obj, StructureDesignVariable& var,
                                   std::string* error)
{
    var.id             = obj["id"].toString().toStdString();
    var.label          = obj["label"].toString().toStdString();
    var.targetName     = obj["targetName"].toString().toStdString();
    var.unit           = obj["unit"].toString().toStdString();
    bool kindOk = true;
    var.kind           = variableKindFromJson(obj, &kindOk);
    if (!kindOk) {
        if (error != nullptr)
            *error = "Unknown StructureVariableKind value: " +
                     obj.value("kind").toString().toStdString();
        return false;
    }
    if (!readFiniteNumber(obj, "currentValue", var.currentValue, var.currentValue, error) ||
        !readFiniteNumber(obj, "minimum", var.minimum, var.minimum, error) ||
        !readFiniteNumber(obj, "maximum", var.maximum, var.maximum, error) ||
        !readFiniteNumber(obj, "step", var.step, var.step, error) ||
        !readFiniteNumber(obj, "preferredValue", var.preferredValue,
                          var.preferredValue, error) ||
        !readFiniteNumber(obj, "preferenceWeight", var.preferenceWeight,
                          var.preferenceWeight, error))
        return false;
    if (!readBoolean(obj, "enabled", true, var.enabled, error) ||
        !readBoolean(obj, "syncAssociatedGeometry", false,
                     var.syncAssociatedGeometry, error))
        return false;
    bool domainOk = true;
    var.domainDefinition.domain = variableDomainFromString(obj["domain"].toString(), &domainOk);
    if (!domainOk) {
        if (error != nullptr)
            *error = "Unknown DesignVariableDomain value: " +
                     obj.value("domain").toString().toStdString();
        return false;
    }
    for (const QJsonValue& option : obj["discreteOptions"].toArray()) {
        if (option.isString()) {
            var.domainDefinition.discreteOptions.push_back(option.toString().toStdString());
        }
        else if (option.isDouble() && std::isfinite(option.toDouble())) {
            // Legacy JSON sometimes encoded discrete numbers as JSON numbers.
            var.domainDefinition.discreteOptions.push_back(
                QString::number(option.toDouble(), 'g', 17).toStdString());
        }
        else {
            if (error != nullptr)
                *error = "Discrete option must be a string or finite JSON number.";
            return false;
        }
    }
    return true;
}

// 结构约束序列化：含主/次阈值、启用状态与硬/软属性。
static QJsonObject constraintToJson(const StructureConstraint& con)
{
    QJsonObject obj;
    obj["id"]                = QString::fromStdString(con.id);
    obj["label"]             = QString::fromStdString(con.label);
    obj["targetName"]        = QString::fromStdString(con.targetName);
    obj["kind"]              = constraintKindToJson(con.kind)["kind"].toString();
    obj["threshold"]         = jsonFiniteNumber(con.threshold);
    obj["secondaryThreshold"] = jsonFiniteNumber(con.secondaryThreshold);
    obj["enabled"]           = con.enabled;
    obj["hard"]              = con.hard;
    return obj;
}

// 逆过程：enabled/hard 缺省取 true，与历史版本默认保持一致。
static bool constraintFromJson(const QJsonObject& obj, StructureConstraint& con,
                               std::string* error)
{
    con.id                = obj["id"].toString().toStdString();
    con.label             = obj["label"].toString().toStdString();
    con.targetName        = obj["targetName"].toString().toStdString();
    bool kindOk = true;
    con.kind              = constraintKindFromJson(obj, &kindOk);
    if (!kindOk) {
        if (error != nullptr)
            *error = "Unknown StructureConstraintKind value: " +
                     obj.value("kind").toString().toStdString();
        return false;
    }
    if (!readFiniteNumber(obj, "threshold", con.threshold, con.threshold, error) ||
        !readFiniteNumber(obj, "secondaryThreshold", con.secondaryThreshold,
                          con.secondaryThreshold, error) ||
        !readBoolean(obj, "enabled", con.enabled, con.enabled, error) ||
        !readBoolean(obj, "hard", con.hard, con.hard, error))
        return false;
    return true;
}

// 多目标权重序列化。weights 仅用于旧版兼容，新版本以 objectives 为准。
static QJsonObject weightsToJson(const StructureOptimizationWeights& w)
{
    QJsonObject obj;
    obj["reachability"  ] = jsonFiniteNumber(w.reachability);
    obj["manipulability"] = jsonFiniteNumber(w.manipulability);
    obj["jointMargin"   ] = jsonFiniteNumber(w.jointMargin);
    obj["collision"     ] = jsonFiniteNumber(w.collision);
    obj["compactness"   ] = jsonFiniteNumber(w.compactness);
    obj["preference"    ] = jsonFiniteNumber(w.preference);
    return obj;
}

// 逆过程：逐项读取并给出历史默认值；缺字段时不会重置其他权重。
static bool weightsFromJson(const QJsonObject& obj, StructureOptimizationWeights& w,
                            std::string* error)
{
    return readFiniteNumber(obj, "reachability", 0.35, w.reachability, error) &&
           readFiniteNumber(obj, "manipulability", 0.20, w.manipulability, error) &&
           readFiniteNumber(obj, "jointMargin", 0.15, w.jointMargin, error) &&
           readFiniteNumber(obj, "collision", 0.15, w.collision, error) &&
           readFiniteNumber(obj, "compactness", 0.10, w.compactness, error) &&
           readFiniteNumber(obj, "preference", 0.05, w.preference, error);
}

// 通用指标目标序列化：方向、归一化区间与权重一并保存。
static QJsonObject objectiveToJson(const ObjectiveTerm& objective)
{
    QJsonObject obj;
    obj["metricId"] = QString::fromStdString(objective.metricId);
    obj["direction"] = directionToString(objective.direction);
    QJsonObject normalization;
    normalization["good"] = jsonFiniteNumber(objective.normalization.good);
    normalization["bad"] = jsonFiniteNumber(objective.normalization.bad);
    normalization["clamp"] = objective.normalization.clamp;
    obj["normalization"] = normalization;
    obj["weight"] = jsonFiniteNumber(objective.weight);
    obj["enabled"] = objective.enabled;
    return obj;
}

// 逆过程：归一化缺省 good=1.0/bad=0.0 并启用 clamp，保证可复现评分语义。
static bool objectiveFromJson(const QJsonObject& obj, ObjectiveTerm& objective,
                              std::string* error)
{
    objective.metricId = obj["metricId"].toString().toStdString();
    bool directionOk = true;
    objective.direction = directionFromString(obj["direction"].toString(), &directionOk);
    if (!directionOk) {
        if (error != nullptr)
            *error = "Unknown OptimizationDirection value: " +
                     obj.value("direction").toString().toStdString();
        return false;
    }
    QJsonObject normalization;
    if (obj.contains("normalization")) {
        if (!obj.value("normalization").isObject()) {
            if (error != nullptr) *error = "Objective normalization must be an object.";
            return false;
        }
        normalization = obj.value("normalization").toObject();
    }
    if (!readFiniteNumber(normalization, "good", 1.0, objective.normalization.good, error) ||
        !readFiniteNumber(normalization, "bad", 0.0, objective.normalization.bad, error) ||
        !readBoolean(normalization, "clamp", true, objective.normalization.clamp, error) ||
        !readFiniteNumber(obj, "weight", objective.weight, objective.weight, error) ||
        !readBoolean(obj, "enabled", true, objective.enabled, error))
        return false;
    return true;
}

// 通用指标硬/软约束序列化。
static QJsonObject metricConstraintToJson(const ConstraintRule& constraint)
{
    QJsonObject obj;
    obj["metricId"] = QString::fromStdString(constraint.metricId);
    obj["comparison"] = comparisonToString(constraint.comparison);
    obj["threshold"] = jsonFiniteNumber(constraint.threshold);
    obj["hard"] = constraint.hard;
    obj["enabled"] = constraint.enabled;
    return obj;
}

// 逆过程：hard/enabled 缺省为 true。
static bool metricConstraintFromJson(const QJsonObject& obj, ConstraintRule& constraint,
                                     std::string* error)
{
    constraint.metricId = obj["metricId"].toString().toStdString();
    bool comparisonOk = true;
    constraint.comparison = comparisonFromString(obj["comparison"].toString(), &comparisonOk);
    if (!comparisonOk) {
        if (error != nullptr)
            *error = "Unknown ComparisonOperator value: " +
                     obj.value("comparison").toString().toStdString();
        return false;
    }
    if (!readFiniteNumber(obj, "threshold", constraint.threshold, constraint.threshold, error) ||
        !readBoolean(obj, "hard", true, constraint.hard, error) ||
        !readBoolean(obj, "enabled", true, constraint.enabled, error))
        return false;
    return true;
}

// 评估配置序列化：粗评/精评采样参数、碰撞开关与覆盖盒集合。
// 内部用局部 lambda 复用工作空间采样与覆盖盒的编码逻辑。
static QJsonObject evalConfigToJson(const StructureEvaluationConfig& cfg)
{
    QJsonObject obj;
    obj["checkCollision"] = cfg.checkCollision;
    obj["evaluatorId"] = QString::fromStdString(cfg.evaluatorId);
    obj["evaluatorVersion"] = QString::fromStdString(cfg.evaluatorVersion);
    const auto workspaceToJson = [](const WorkspaceSamplingConfig& sampling) {
        QJsonObject value;
        value["mode"] = static_cast<int>(sampling.mode);
        value["sampleCount"] = sampling.sampleCount;
        value["gridStepsPerJoint"] = sampling.gridStepsPerJoint;
        value["checkCollision"] = sampling.checkCollision;
        value["randomSeed"] = static_cast<qint64>(sampling.randomSeed);
        return value;
    };
    obj["quickWorkspace"] = workspaceToJson(cfg.quickWorkspace);
    obj["verifiedWorkspace"] = workspaceToJson(cfg.verifiedWorkspace);

    const auto coverageToJson = [](const WorkspaceCoverageBox& box) {
        QJsonObject coverage;
        coverage["id"] = QString::fromStdString(box.id);
        coverage["referenceFrame"] = QString::fromStdString(box.referenceFrame);
        coverage["enabled"] = box.enabled;
        QJsonArray minimum;
        QJsonArray maximum;
        QJsonArray cells;
        for (std::size_t i = 0; i < box.minimum.size(); ++i) {
            appendFiniteNumber(minimum, box.minimum[i]);
            appendFiniteNumber(maximum, box.maximum[i]);
            cells.append(box.cells[i]);
        }
        coverage["minimum"] = minimum;
        coverage["maximum"] = maximum;
        coverage["cells"] = cells;
        return coverage;
    };
    // 保留 coverageBox 以读取旧版本项目，同时额外写入 coverageBoxes，使冻结需求中的
    // 多个独立工作空间区域在保存后不会退化为一个全局盒。
    obj["coverageBox"] = coverageToJson(cfg.coverageBox);
    QJsonArray coverageBoxes;
    for (const WorkspaceCoverageBox& box : cfg.coverageBoxes)
        coverageBoxes.append(coverageToJson(box));
    obj["coverageBoxes"] = coverageBoxes;
    return obj;
}

// 逆过程：各子对象均为"缺失即保留默认"，因此旧项目文件可以直接打开。
static bool evalConfigFromJson(const QJsonObject& obj, StructureEvaluationConfig& cfg,
                               std::string* error)
{
    if (!readBoolean(obj, "checkCollision", true, cfg.checkCollision, error))
        return false;
    if (obj.contains("evaluatorId") && !obj.value("evaluatorId").isString()) {
        if (error != nullptr) *error = "Field 'evaluatorId' must be a JSON string.";
        return false;
    }
    if (obj.contains("evaluatorVersion") && !obj.value("evaluatorVersion").isString()) {
        if (error != nullptr) *error = "Field 'evaluatorVersion' must be a JSON string.";
        return false;
    }
    cfg.evaluatorId = obj["evaluatorId"].toString(
        QString::fromStdString(cfg.evaluatorId)).toStdString();
    cfg.evaluatorVersion = obj["evaluatorVersion"].toString(
        QString::fromStdString(cfg.evaluatorVersion)).toStdString();
    const auto workspaceFromJson = [](const QJsonObject& value,
                                       WorkspaceSamplingConfig& sampling,
                                       std::string* parseError) {
        if (value.isEmpty())
            return true;
        if (value.contains("mode")) {
            const QJsonValue modeValue = value.value("mode");
            const int mode = modeValue.toInt(-1);
            if (!modeValue.isDouble() ||
                (mode != static_cast<int>(WorkspaceSamplingMode::RandomUniform) &&
                 mode != static_cast<int>(WorkspaceSamplingMode::Grid))) {
                if (parseError != nullptr)
                    *parseError = "Unknown WorkspaceSamplingMode value: " +
                                  std::to_string(mode);
                return false;
            }
            sampling.mode = static_cast<WorkspaceSamplingMode>(mode);
        }
        if (!readInteger(value, "sampleCount", sampling.sampleCount,
                         sampling.sampleCount, parseError) ||
            !readInteger(value, "gridStepsPerJoint", sampling.gridStepsPerJoint,
                         sampling.gridStepsPerJoint, parseError) ||
            !readBoolean(value, "checkCollision", sampling.checkCollision,
                         sampling.checkCollision, parseError))
            return false;
        if (!readUnsignedInteger(value, "randomSeed", sampling.randomSeed,
                                 sampling.randomSeed, parseError)) {
            return false;
        }
        return true;
    };
    if (!workspaceFromJson(obj["quickWorkspace"].toObject(), cfg.quickWorkspace, error) ||
        !workspaceFromJson(obj["verifiedWorkspace"].toObject(), cfg.verifiedWorkspace, error))
        return false;

    const auto coverageFromJson = [](const QJsonObject& coverage, WorkspaceCoverageBox& box,
                                     std::string* parseError) {
        if (coverage.isEmpty()) return true;
        if (coverage.contains("id") && !coverage.value("id").isString()) {
            if (parseError != nullptr) *parseError = "Coverage box id must be a string.";
            return false;
        }
        if (coverage.contains("referenceFrame") && !coverage.value("referenceFrame").isString()) {
            if (parseError != nullptr) *parseError = "Coverage box referenceFrame must be a string.";
            return false;
        }
        if (!readBoolean(coverage, "enabled", box.enabled, box.enabled, parseError))
            return false;
        box.id = coverage["id"].toString(QString::fromStdString(box.id)).toStdString();
        box.referenceFrame = coverage["referenceFrame"].toString(
            QString::fromStdString(box.referenceFrame)).toStdString();
        box.enabled = coverage["enabled"].toBool(box.enabled);
        const QJsonArray minimum = coverage["minimum"].toArray();
        const QJsonArray maximum = coverage["maximum"].toArray();
        const QJsonArray cells = coverage["cells"].toArray();
        if ((!minimum.isEmpty() && minimum.size() != 3) ||
            (!maximum.isEmpty() && maximum.size() != 3) ||
            (!cells.isEmpty() && cells.size() != 3)) {
            if (parseError != nullptr) *parseError = "Coverage box arrays must contain three values.";
            return false;
        }
        for (int i = 0; i < 3; ++i) {
            if (!minimum.isEmpty()) {
                QJsonObject wrapper;
                wrapper["value"] = minimum[i];
                if (!readFiniteNumber(wrapper, "value", box.minimum[static_cast<std::size_t>(i)],
                                      box.minimum[static_cast<std::size_t>(i)], parseError))
                    return false;
            }
            if (!maximum.isEmpty()) {
                QJsonObject wrapper;
                wrapper["value"] = maximum[i];
                if (!readFiniteNumber(wrapper, "value", box.maximum[static_cast<std::size_t>(i)],
                                      box.maximum[static_cast<std::size_t>(i)], parseError))
                    return false;
            }
            if (!cells.isEmpty()) {
                QJsonObject wrapper;
                wrapper["value"] = cells[i];
                int cell = box.cells[static_cast<std::size_t>(i)];
                if (!readInteger(wrapper, "value", cell, cell, parseError)) return false;
                box.cells[static_cast<std::size_t>(i)] = cell;
            }
        }
        return true;
    };
    if (obj.contains("coverageBox") && !obj.value("coverageBox").isObject()) {
        if (error != nullptr) *error = "Field 'coverageBox' must be an object.";
        return false;
    }
    if (!coverageFromJson(obj["coverageBox"].toObject(), cfg.coverageBox, error)) return false;
    cfg.coverageBoxes.clear();
    for (const QJsonValue& value : obj["coverageBoxes"].toArray()) {
        if (!value.isObject()) {
            if (error != nullptr) *error = "Coverage boxes must contain objects.";
            return false;
        }
        WorkspaceCoverageBox box;
        if (!coverageFromJson(value.toObject(), box, error)) return false;
        cfg.coverageBoxes.push_back(box);
    }
    return true;
}

// 优化运行配置序列化。
static QJsonObject runConfigToJson(const StructureOptimizationRunConfig& run)
{
    QJsonObject obj;
    obj["strategy"]              = static_cast<int>(run.strategy);
    obj["candidateCount"]        = run.candidateCount;
    obj["eliteCount"]            = run.eliteCount;
    obj["localEliteCount"]       = run.localEliteCount;
    obj["finalVerificationCount"] = run.finalVerificationCount;
    obj["maxLocalSweeps"]        = run.maxLocalSweeps;
    obj["gridSteps"]             = run.gridSteps;
    obj["randomSeed"]            = static_cast<qint64>(run.randomSeed);
    return obj;
}

// 逆过程：默认值与历史版本一致，保证旧项目重载后搜索行为不变。
static bool runConfigFromJson(const QJsonObject& obj, StructureOptimizationRunConfig& run,
                              std::string* error)
{
    if (obj.contains("strategy")) {
        const QJsonValue strategyValue = obj.value("strategy");
        const int strategy = strategyValue.toInt(-1);
        if (!strategyValue.isDouble() ||
            (strategy != static_cast<int>(StructureStrategyKind::Random) &&
             strategy != static_cast<int>(StructureStrategyKind::Grid) &&
             strategy != static_cast<int>(StructureStrategyKind::Hybrid))) {
            if (error != nullptr)
                *error = "Unknown StructureStrategyKind value: " + std::to_string(strategy);
            return false;
        }
        run.strategy = static_cast<StructureStrategyKind>(strategy);
    }
    if (!readInteger(obj, "candidateCount", 300, run.candidateCount, error) ||
        !readInteger(obj, "eliteCount", 20, run.eliteCount, error) ||
        !readInteger(obj, "localEliteCount", 5, run.localEliteCount, error) ||
        !readInteger(obj, "finalVerificationCount", 3, run.finalVerificationCount, error) ||
        !readInteger(obj, "maxLocalSweeps", 20, run.maxLocalSweeps, error) ||
        !readInteger(obj, "gridSteps", 3, run.gridSteps, error))
        return false;
    unsigned int seed = 1;
    if (!readUnsignedInteger(obj, "randomSeed", seed, seed, error)) {
        return false;
    }
    run.randomSeed = seed;
    return true;
}

// =============================================================================
//  Sensitivity JSON
// =============================================================================

// 单变量灵敏度入口序列化。
static QJsonObject sensitivityEntryToJson(const StructureSensitivityEntry& e)
{
    QJsonObject obj;
    obj["variableId"]       = QString::fromStdString(e.variableId);
    obj["delta"]            = jsonFiniteNumber(e.delta);
    obj["perturbedValue"]   = jsonFiniteNumber(e.perturbedValue);
    obj["scoreDrop"]        = jsonFiniteNumber(e.scoreDrop);
    obj["feasible"]         = e.feasible;
    QJsonArray vc;
    for (const auto& c : e.violatedConstraints)
        vc.append(QString::fromStdString(c));
    obj["violatedConstraints"] = vc;
    return obj;
}

// 灵敏度分析整体结果序列化：条目列表、汇总得分、关键变量与鲁棒性等级。
static QJsonObject sensitivityResultToJson(const StructureSensitivityResult& sr)
{
    QJsonObject obj;
    QJsonArray arr;
    for (const auto& e : sr.entries)
        arr.append(sensitivityEntryToJson(e));
    obj["entries"]           = arr;
    obj["maximumScoreDrop"]  = jsonFiniteNumber(sr.maximumScoreDrop);
    obj["meanScoreDrop"]     = jsonFiniteNumber(sr.meanScoreDrop);
    QJsonArray cids;
    for (const auto& id : sr.criticalVariableIds)
        cids.append(QString::fromStdString(id));
    obj["criticalVariableIds"] = cids;
    obj["robustnessGrade"]   = QString::fromStdString(sr.robustnessGrade);
    return obj;
}

static QString canonicalFrameTypeToString(CanonicalFrameType type)
{
    switch (type) {
    case CanonicalFrameType::Base: return "Base";
    case CanonicalFrameType::Link: return "Link";
    case CanonicalFrameType::Fixed: return "Fixed";
    case CanonicalFrameType::Flange: return "Flange";
    case CanonicalFrameType::Tool: return "Tool";
    case CanonicalFrameType::Auxiliary: return "Auxiliary";
    }
    return "Unknown";
}

static bool canonicalFrameTypeFromString(const QString& value, CanonicalFrameType& type)
{
    if (value == "Base") { type = CanonicalFrameType::Base; return true; }
    if (value == "Link") { type = CanonicalFrameType::Link; return true; }
    if (value == "Fixed") { type = CanonicalFrameType::Fixed; return true; }
    if (value == "Flange") { type = CanonicalFrameType::Flange; return true; }
    if (value == "Tool") { type = CanonicalFrameType::Tool; return true; }
    if (value == "Auxiliary") { type = CanonicalFrameType::Auxiliary; return true; }
    return false;
}

static QString canonicalJointTypeToString(CanonicalJointType type)
{
    switch (type) {
    case CanonicalJointType::Revolute: return "Revolute";
    case CanonicalJointType::Prismatic: return "Prismatic";
    case CanonicalJointType::Fixed: return "Fixed";
    }
    return "Unknown";
}

static bool canonicalJointTypeFromString(const QString& value, CanonicalJointType& type)
{
    if (value == "Revolute") { type = CanonicalJointType::Revolute; return true; }
    if (value == "Prismatic") { type = CanonicalJointType::Prismatic; return true; }
    if (value == "Fixed") { type = CanonicalJointType::Fixed; return true; }
    return false;
}

static QString canonicalCoordinateUnitToString(CanonicalCoordinateUnit unit)
{
    return unit == CanonicalCoordinateUnit::Radians ? "Radians" : "Metres";
}

static bool canonicalCoordinateUnitFromString(const QString& value,
                                               CanonicalCoordinateUnit& unit)
{
    if (value == "Radians") { unit = CanonicalCoordinateUnit::Radians; return true; }
    if (value == "Metres") { unit = CanonicalCoordinateUnit::Metres; return true; }
    return false;
}

static bool finiteJsonNumber(const QJsonValue& value, double& number)
{
    if (!value.isDouble()) return false;
    number = value.toDouble();
    return std::isfinite(number);
}

static QJsonArray canonicalVectorToJson(const rw::math::Vector3D<>& vector)
{
    QJsonArray result;
    appendFiniteNumber(result, vector(0));
    appendFiniteNumber(result, vector(1));
    appendFiniteNumber(result, vector(2));
    return result;
}

static bool canonicalVectorFromJson(const QJsonValue& value, rw::math::Vector3D<>& vector)
{
    if (!value.isArray()) return false;
    const QJsonArray array = value.toArray();
    if (array.size() != 3) return false;
    double x = 0.0, y = 0.0, z = 0.0;
    if (!finiteJsonNumber(array.at(0), x) || !finiteJsonNumber(array.at(1), y) ||
        !finiteJsonNumber(array.at(2), z)) return false;
    vector = rw::math::Vector3D<>(x, y, z);
    return true;
}

static QJsonObject canonicalTransformToJson(const rw::math::Transform3D<>& transform)
{
    QJsonObject result;
    result["position"] = canonicalVectorToJson(transform.P());
    QJsonArray rotation;
    for (std::size_t row = 0; row < 3; ++row) {
        QJsonArray values;
        for (std::size_t column = 0; column < 3; ++column)
            appendFiniteNumber(values, transform.R()(row, column));
        rotation.append(values);
    }
    result["rotation"] = rotation;
    return result;
}

static bool canonicalTransformFromJson(const QJsonValue& value,
                                       rw::math::Transform3D<>& transform)
{
    if (!value.isObject()) return false;
    const QJsonObject object = value.toObject();
    rw::math::Vector3D<> position;
    if (!canonicalVectorFromJson(object.value("position"), position) ||
        !object.value("rotation").isArray()) return false;
    const QJsonArray rows = object.value("rotation").toArray();
    if (rows.size() != 3) return false;
    double matrix[3][3];
    for (std::size_t row = 0; row < 3; ++row) {
        if (!rows.at(static_cast< int >(row)).isArray()) return false;
        const QJsonArray columns = rows.at(static_cast< int >(row)).toArray();
        if (columns.size() != 3) return false;
        for (std::size_t column = 0; column < 3; ++column)
            if (!finiteJsonNumber(columns.at(static_cast< int >(column)), matrix[row][column]))
                return false;
    }
    transform = rw::math::Transform3D<>(
        position, rw::math::Rotation3D<>(matrix[0][0], matrix[0][1], matrix[0][2],
                                         matrix[1][0], matrix[1][1], matrix[1][2],
                                         matrix[2][0], matrix[2][1], matrix[2][2]));
    return transform.R().isProperRotation(1e-9);
}

static QJsonObject canonicalLimitsToJson(const CanonicalJointLimits& limits)
{
    QJsonObject result;
    result["enabled"] = limits.enabled;
    result["lower"] = jsonFiniteNumber(limits.lower);
    result["upper"] = jsonFiniteNumber(limits.upper);
    result["unit"] = canonicalCoordinateUnitToString(limits.unit);
    result["coordinateConvention"] =
        QString::fromStdString(jointCoordinateConventionToString(limits.coordinateConvention));
    return result;
}

static bool canonicalLimitsFromJson(const QJsonValue& value, CanonicalJointLimits& limits)
{
    if (!value.isObject()) return false;
    const QJsonObject object = value.toObject();
    if (!object.value("enabled").isBool() ||
        !finiteJsonNumber(object.value("lower"), limits.lower) ||
        !finiteJsonNumber(object.value("upper"), limits.upper) ||
        !canonicalCoordinateUnitFromString(object.value("unit").toString(), limits.unit) ||
        !jointCoordinateConventionFromString(
            object.value("coordinateConvention").toString().toStdString(),
            limits.coordinateConvention))
        return false;
    limits.enabled = object.value("enabled").toBool();
    return true;
}

static QJsonArray canonicalStringArrayToJson(const std::vector< std::string >& values)
{
    QJsonArray result;
    for (const std::string& value : values)
        result.append(QString::fromStdString(value));
    return result;
}

static bool canonicalStringArrayFromJson(const QJsonValue& value,
                                         std::vector< std::string >& values)
{
    if (!value.isArray()) return false;
    values.clear();
    for (const QJsonValue& element : value.toArray()) {
        if (!element.isString()) return false;
        values.push_back(element.toString().toStdString());
    }
    return true;
}

static QJsonObject canonicalModelToJson(const CanonicalKinematicModel& model)
{
    QJsonObject result;
    result["schemaVersion"] = model.schemaVersion;
    result["modelId"] = QString::fromStdString(model.modelId);
    result["sourceFingerprint"] = QString::fromStdString(model.sourceFingerprint);
    result["environmentFingerprint"] = QString::fromStdString(model.environmentFingerprint);
    result["rootFrameId"] = QString::fromStdString(model.rootFrameId);
    result["baseFrameId"] = QString::fromStdString(model.baseFrameId);
    result["activeDeviceChainId"] = QString::fromStdString(model.activeDeviceChainId);
    QJsonArray frames;
    for (const FrameNode& frame : model.frames) {
        QJsonObject object;
        object["id"] = QString::fromStdString(frame.id);
        object["name"] = QString::fromStdString(frame.name);
        object["type"] = canonicalFrameTypeToString(frame.type);
        object["sourceObjectId"] = QString::fromStdString(frame.sourceObjectId);
        frames.append(object);
    }
    result["frames"] = frames;
    QJsonArray joints;
    for (const JointEdge& joint : model.joints) {
        QJsonObject object;
        object["id"] = QString::fromStdString(joint.id);
        object["name"] = QString::fromStdString(joint.name);
        object["type"] = canonicalJointTypeToString(joint.type);
        object["parentFrameId"] = QString::fromStdString(joint.parentFrameId);
        object["childFrameId"] = QString::fromStdString(joint.childFrameId);
        object["parentToJointZero"] = canonicalTransformToJson(joint.parentToJointZero);
        object["motionAxisInJoint"] = canonicalVectorToJson(joint.motionAxisInJoint);
        object["jointMotionToChild"] = canonicalTransformToJson(joint.jointMotionToChild);
        object["zeroPositionOffset"] = jsonFiniteNumber(joint.zeroPositionOffset);
        object["physicalLimits"] = canonicalLimitsToJson(joint.physicalLimits);
        object["operationalLimits"] = canonicalLimitsToJson(joint.operationalLimits);
        object["dofId"] = QString::fromStdString(joint.dofId);
        object["sourceObjectId"] = QString::fromStdString(joint.sourceObjectId);
        joints.append(object);
    }
    result["joints"] = joints;
    QJsonArray dofs;
    for (const DofDefinition& dof : model.dofs) {
        QJsonObject object;
        object["id"] = QString::fromStdString(dof.id);
        object["jointId"] = QString::fromStdString(dof.jointId);
        object["qIndex"] = static_cast< qint64 >(dof.qIndex);
        object["type"] = canonicalJointTypeToString(dof.type);
        object["unit"] = canonicalCoordinateUnitToString(dof.unit);
        dofs.append(object);
    }
    result["dofs"] = dofs;
    QJsonArray chains;
    for (const DeviceChain& chain : model.deviceChains) {
        QJsonObject object;
        object["id"] = QString::fromStdString(chain.id);
        object["rootFrameId"] = QString::fromStdString(chain.rootFrameId);
        object["tipFrameId"] = QString::fromStdString(chain.tipFrameId);
        object["orderedJointIds"] = canonicalStringArrayToJson(chain.orderedJointIds);
        object["orderedDofIds"] = canonicalStringArrayToJson(chain.orderedDofIds);
        chains.append(object);
    }
    result["deviceChains"] = chains;
    QJsonArray bindings;
    for (const ToolBinding& binding : model.toolBindings) {
        QJsonObject object;
        object["id"] = QString::fromStdString(binding.id);
        object["flangeFrameId"] = QString::fromStdString(binding.flangeFrameId);
        object["tcpFrameId"] = QString::fromStdString(binding.tcpFrameId);
        object["flangeToTcp"] = canonicalTransformToJson(binding.flangeToTcp);
        object["geometryBindingIds"] = canonicalStringArrayToJson(binding.geometryBindingIds);
        object["collisionBindingIds"] = canonicalStringArrayToJson(binding.collisionBindingIds);
        bindings.append(object);
    }
    result["toolBindings"] = bindings;
    return result;
}

static bool canonicalModelFromJson(const QJsonValue& value, CanonicalKinematicModel& model)
{
    if (!value.isObject()) return false;
    const QJsonObject object = value.toObject();
    if (!object.value("schemaVersion").isDouble() || !object.value("modelId").isString() ||
        !object.value("sourceFingerprint").isString() ||
        !object.value("environmentFingerprint").isString() ||
        !object.value("rootFrameId").isString() || !object.value("baseFrameId").isString() ||
        !object.value("activeDeviceChainId").isString() || !object.value("frames").isArray() ||
        !object.value("joints").isArray() || !object.value("dofs").isArray() ||
        !object.value("deviceChains").isArray() || !object.value("toolBindings").isArray())
        return false;
    model = CanonicalKinematicModel();
    model.schemaVersion = object.value("schemaVersion").toInt();
    model.modelId = object.value("modelId").toString().toStdString();
    model.sourceFingerprint = object.value("sourceFingerprint").toString().toStdString();
    model.environmentFingerprint = object.value("environmentFingerprint").toString().toStdString();
    model.rootFrameId = object.value("rootFrameId").toString().toStdString();
    model.baseFrameId = object.value("baseFrameId").toString().toStdString();
    model.activeDeviceChainId = object.value("activeDeviceChainId").toString().toStdString();
    for (const QJsonValue& value : object.value("frames").toArray()) {
        if (!value.isObject()) return false;
        const QJsonObject frame = value.toObject();
        FrameNode parsed;
        if (!frame.value("id").isString() || !frame.value("name").isString() ||
            !frame.value("sourceObjectId").isString() ||
            !canonicalFrameTypeFromString(frame.value("type").toString(), parsed.type)) return false;
        parsed.id = frame.value("id").toString().toStdString();
        parsed.name = frame.value("name").toString().toStdString();
        parsed.sourceObjectId = frame.value("sourceObjectId").toString().toStdString();
        model.frames.push_back(parsed);
    }
    for (const QJsonValue& value : object.value("joints").toArray()) {
        if (!value.isObject()) return false;
        const QJsonObject joint = value.toObject();
        JointEdge parsed;
        if (!joint.value("id").isString() || !joint.value("name").isString() ||
            !joint.value("parentFrameId").isString() || !joint.value("childFrameId").isString() ||
            !joint.value("dofId").isString() || !joint.value("sourceObjectId").isString() ||
            !canonicalJointTypeFromString(joint.value("type").toString(), parsed.type) ||
            !canonicalTransformFromJson(joint.value("parentToJointZero"), parsed.parentToJointZero) ||
            !canonicalVectorFromJson(joint.value("motionAxisInJoint"), parsed.motionAxisInJoint) ||
            !canonicalTransformFromJson(joint.value("jointMotionToChild"), parsed.jointMotionToChild) ||
            !finiteJsonNumber(joint.value("zeroPositionOffset"), parsed.zeroPositionOffset) ||
            !canonicalLimitsFromJson(joint.value("physicalLimits"), parsed.physicalLimits) ||
            !canonicalLimitsFromJson(joint.value("operationalLimits"), parsed.operationalLimits)) return false;
        parsed.id = joint.value("id").toString().toStdString();
        parsed.name = joint.value("name").toString().toStdString();
        parsed.parentFrameId = joint.value("parentFrameId").toString().toStdString();
        parsed.childFrameId = joint.value("childFrameId").toString().toStdString();
        parsed.dofId = joint.value("dofId").toString().toStdString();
        parsed.sourceObjectId = joint.value("sourceObjectId").toString().toStdString();
        model.joints.push_back(parsed);
    }
    for (const QJsonValue& value : object.value("dofs").toArray()) {
        if (!value.isObject()) return false;
        const QJsonObject dof = value.toObject();
        DofDefinition parsed;
        if (!dof.value("id").isString() || !dof.value("jointId").isString() ||
            !dof.value("qIndex").isDouble() || dof.value("qIndex").toDouble() < 0.0 ||
            std::floor(dof.value("qIndex").toDouble()) != dof.value("qIndex").toDouble() ||
            !canonicalJointTypeFromString(dof.value("type").toString(), parsed.type) ||
            !canonicalCoordinateUnitFromString(dof.value("unit").toString(), parsed.unit)) return false;
        parsed.id = dof.value("id").toString().toStdString();
        parsed.jointId = dof.value("jointId").toString().toStdString();
        parsed.qIndex = static_cast< std::size_t >(dof.value("qIndex").toDouble());
        model.dofs.push_back(parsed);
    }
    for (const QJsonValue& value : object.value("deviceChains").toArray()) {
        if (!value.isObject()) return false;
        const QJsonObject chain = value.toObject();
        DeviceChain parsed;
        if (!chain.value("id").isString() || !chain.value("rootFrameId").isString() ||
            !chain.value("tipFrameId").isString() ||
            !canonicalStringArrayFromJson(chain.value("orderedJointIds"), parsed.orderedJointIds) ||
            !canonicalStringArrayFromJson(chain.value("orderedDofIds"), parsed.orderedDofIds)) return false;
        parsed.id = chain.value("id").toString().toStdString();
        parsed.rootFrameId = chain.value("rootFrameId").toString().toStdString();
        parsed.tipFrameId = chain.value("tipFrameId").toString().toStdString();
        model.deviceChains.push_back(parsed);
    }
    for (const QJsonValue& value : object.value("toolBindings").toArray()) {
        if (!value.isObject()) return false;
        const QJsonObject binding = value.toObject();
        ToolBinding parsed;
        if (!binding.value("id").isString() || !binding.value("flangeFrameId").isString() ||
            !binding.value("tcpFrameId").isString() ||
            !canonicalTransformFromJson(binding.value("flangeToTcp"), parsed.flangeToTcp) ||
            !canonicalStringArrayFromJson(binding.value("geometryBindingIds"), parsed.geometryBindingIds) ||
            !canonicalStringArrayFromJson(binding.value("collisionBindingIds"), parsed.collisionBindingIds)) return false;
        parsed.id = binding.value("id").toString().toStdString();
        parsed.flangeFrameId = binding.value("flangeFrameId").toString().toStdString();
        parsed.tcpFrameId = binding.value("tcpFrameId").toString().toStdString();
        model.toolBindings.push_back(parsed);
    }
    return CanonicalKinematicModelValidator::validate(model).valid;
}

static QJsonObject canonicalSnapshotToJson(const KinematicBaselineSnapshot& snapshot)
{
    QJsonObject result;
    result["schemaVersion"] = snapshot.schemaVersion;
    result["fingerprintAlgorithmId"] = QString::fromStdString(snapshot.fingerprintAlgorithmId);
    result["serializationVersion"] = QString::fromStdString(snapshot.serializationVersion);
    result["modelFingerprint"] = QString::fromStdString(snapshot.modelFingerprint);
    result["environmentFingerprint"] = QString::fromStdString(snapshot.environmentFingerprint);
    result["toolFingerprint"] = QString::fromStdString(snapshot.toolFingerprint);
    result["model"] = canonicalModelToJson(snapshot.model);
    return result;
}

static bool canonicalSnapshotFromJson(const QJsonValue& value, KinematicBaselineSnapshot& snapshot)
{
    if (!value.isObject()) return false;
    const QJsonObject object = value.toObject();
    if (!object.value("schemaVersion").isDouble() ||
        !object.value("fingerprintAlgorithmId").isString() ||
        !object.value("serializationVersion").isString() ||
        !object.value("modelFingerprint").isString() ||
        !object.value("environmentFingerprint").isString() ||
        !object.value("toolFingerprint").isString() ||
        !canonicalModelFromJson(object.value("model"), snapshot.model)) return false;
    snapshot.schemaVersion = object.value("schemaVersion").toInt();
    snapshot.fingerprintAlgorithmId = object.value("fingerprintAlgorithmId").toString().toStdString();
    snapshot.serializationVersion = object.value("serializationVersion").toString().toStdString();
    snapshot.modelFingerprint = object.value("modelFingerprint").toString().toStdString();
    snapshot.environmentFingerprint = object.value("environmentFingerprint").toString().toStdString();
    snapshot.toolFingerprint = object.value("toolFingerprint").toString().toStdString();
    return true;
}

// =============================================================================
//  公有的 problemToJson / problemFromJson
// =============================================================================

// 问题全量序列化：上下文、任务、变量、约束、目标/权重、评估与运行配置，
// 外加冻结需求工件提供的执行契约、审计溯源与场景快照，保证优化可完整重建。
std::string StructureOptimizationJson::problemToJson(
    const StructureOptimizationProblem& problem)
{
    QJsonObject root;
    root["schemaVersion"] = SchemaVersion;
    root["type"]          = "StructureOptimizationProblem";

    // context — 复用 RobotAnalysisJson
    const std::string ctxJson = RobotAnalysisJson::toJson(problem.context);
    QJsonDocument ctxDoc = QJsonDocument::fromJson(QString::fromStdString(ctxJson).toUtf8());
    if (!ctxDoc.isNull())
        root["context"] = ctxDoc.object();

    // tasks
    QJsonArray tasksArr;
    for (const auto& t : problem.tasks) {
        QJsonObject tObj = taskPointToJson(t.point);
        tObj["required"] = t.required;
        tasksArr.append(tObj);
    }
    root["tasks"] = tasksArr;

    // 不复制需求插件的编辑态模型；只持久化已冻结的执行契约和审计身份，使结构优化
    // 重载后仍能运行同一组公共 evaluator，并让完整契约参与候选缓存键计算。
    if (!problem.requirementProvenance.requirementFingerprint.empty() ||
        !problem.requirementProvenance.executionFingerprint.empty() ||
        !problem.requirementProvenance.workcellFingerprint.empty() ||
        !problem.requirementProvenance.compilerVersion.empty()) {
        QJsonObject provenance;
        provenance["requirementFingerprint"] =
            QString::fromStdString(problem.requirementProvenance.requirementFingerprint);
        provenance["executionFingerprint"] =
            QString::fromStdString(problem.requirementProvenance.executionFingerprint);
        provenance["workcellFingerprint"] =
            QString::fromStdString(problem.requirementProvenance.workcellFingerprint);
        provenance["environmentFingerprint"] =
            QString::fromStdString(problem.requirementProvenance.environmentFingerprint);
        provenance["compilerVersion"] =
            QString::fromStdString(problem.requirementProvenance.compilerVersion);
        // 冻结时间是需求工件的一部分而非项目创建时间，因此需与三类指纹一起保存。
        provenance["frozenAt"] = QString::fromStdString(problem.requirementProvenance.frozenAt);
        root["engineeringRequirementProvenance"] = provenance;
    }
    if (!problem.requirementExecution.provenance.requirementFingerprint.empty() ||
        !problem.requirementExecution.tasks.empty() ||
        !problem.requirementExecution.workspaceRegions.empty()) {
        root["requirementExecution"] =
            RequirementExecutionJson::toObject(problem.requirementExecution);
    }
    if (problem.scenarioSnapshot.available()) {
        QJsonObject scenario;
        scenario["schemaVersion"] = problem.scenarioSnapshot.schemaVersion;
        scenario["sourceWorkCellPath"] = QString::fromStdString(problem.scenarioSnapshot.sourceWorkCellPath);
        scenario["sourceFileFingerprint"] = QString::fromStdString(problem.scenarioSnapshot.sourceFileFingerprint);
        scenario["snapshotFingerprint"] = QString::fromStdString(problem.scenarioSnapshot.snapshotFingerprint);
        scenario["deviceName"] = QString::fromStdString(problem.scenarioSnapshot.deviceName);
        scenario["environmentFingerprint"] = QString::fromStdString(problem.scenarioSnapshot.environmentFingerprint);
        scenario["stateFingerprint"] = QString::fromStdString(problem.scenarioSnapshot.stateFingerprint);
        scenario["sceneSpec"] = RobotModelSpecJson::toObject(problem.scenarioSnapshot.sceneSpec);
        root["frozenScenarioSnapshot"] = scenario;
    }
    if (problem.canonicalModelShadow.hasSnapshot()) {
        QJsonObject shadow;
        switch (problem.canonicalModelShadow.status) {
        case CanonicalModelShadowStatus::Current: shadow["status"] = "Current"; break;
        case CanonicalModelShadowStatus::Stale: shadow["status"] = "Stale"; break;
        case CanonicalModelShadowStatus::Invalid: shadow["status"] = "Invalid"; break;
        case CanonicalModelShadowStatus::CanonicalModelMissing:
            shadow["status"] = "CanonicalModelMissing";
            break;
        }
        shadow["snapshot"] = canonicalSnapshotToJson(*problem.canonicalModelShadow.snapshot);
        root["canonicalModelShadow"] = shadow;
    }

    // variables
    QJsonArray varsArr;
    for (const auto& v : problem.variables)
        varsArr.append(designVariableToJson(v));
    root["variables"] = varsArr;

    // constraints
    QJsonArray consArr;
    for (const auto& c : problem.constraints)
        consArr.append(constraintToJson(c));
    root["constraints"] = consArr;

    // The legacy weights remain for compatibility, while objectives are the
    // canonical v2 representation.
    root["weights"] = weightsToJson(problem.weights);
    // 双写兼容：weights 保留给旧版读取；若新项目没有显式 objectives，
    // 则从 weights 推导旧式目标，使新旧格式都能被下游一致消费。
    const std::vector<ObjectiveTerm> objectives = problem.objectives.empty()
        ? StructureOptimizationObjectiveProfile::legacyObjectives(problem.weights)
        : problem.objectives;
    QJsonArray objectivesArr;
    for (const ObjectiveTerm& objective : objectives)
        objectivesArr.append(objectiveToJson(objective));
    root["objectives"] = objectivesArr;

    QJsonArray metricConstraintsArr;
    for (const ConstraintRule& constraint : problem.metricConstraints)
        metricConstraintsArr.append(metricConstraintToJson(constraint));
    root["metricConstraints"] = metricConstraintsArr;

    // evaluationConfig
    root["evaluationConfig"] = evalConfigToJson(problem.evaluation);

    // runConfig
    root["runConfig"] = runConfigToJson(problem.run);

    writeProblemExtensions(root, problem.extensions);

    QJsonDocument doc(root);
    return doc.toJson(QJsonDocument::Indented).toStdString();
}

// 问题反序列化：对 schemaVersion 做兼容校验，解析失败时通过 error 输出原因并
// 返回 false，避免上层拿着半填充的问题对象继续优化产生错误结果。
bool StructureOptimizationJson::problemFromJson(
    const std::string& json, StructureOptimizationProblem& problem,
    std::string* error)
{
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(
        QString::fromStdString(json).toUtf8(), &parseError);

    if (doc.isNull()) {
        if (error) *error = "JSON parse error: " + parseError.errorString().toStdString();
        return false;
    }

    if (!doc.isObject()) {
        if (error) *error = "JSON root is not an object";
        return false;
    }

    QJsonObject root = doc.object();

    QJsonObject extensions;
    if (!readProblemExtensions(root, extensions, error)) return false;

    // schemaVersion
    int sv = 0;
    if (!readInteger(root, "schemaVersion", 0, sv, error))
        return false;
    if (sv != 1 && sv != SchemaVersion) {
        if (error) *error = "Unsupported schema version: " + std::to_string(sv);
        return false;
    }

    // context
    if (root.contains("context")) {
        QJsonObject ctxObj = root["context"].toObject();
        QJsonDocument ctxDoc(ctxObj);
        const std::string ctxJson(ctxDoc.toJson(QJsonDocument::Compact).toStdString());
        std::string ctxErr;
        if (!RobotAnalysisJson::fromJson(ctxJson, problem.context, &ctxErr)) {
            if (error) *error = "Failed to parse context: " + ctxErr;
            return false;
        }
    }

    // tasks
    problem.tasks.clear();
    if (root.contains("tasks") && !root.value("tasks").isArray()) {
        if (error != nullptr) *error = "Field 'tasks' must be an array.";
        return false;
    }
    QJsonArray tasksArr = root["tasks"].toArray();
    for (const auto& val : tasksArr) {
        if (!val.isObject()) {
            if (error != nullptr) *error = "Task entries must be objects.";
            return false;
        }
        QJsonObject tObj = val.toObject();
        OptimizationTaskPoint tp;
        if (!taskPointFromJson(tObj, tp.point, error))
            return false;
        if (!readBoolean(tObj, "required", true, tp.required, error))
            return false;
        problem.tasks.push_back(tp);
    }

    // 此字段在 P2 后才出现。使用可选读取保持旧版结构优化项目能够直接打开，
    // 同时让新项目在报告、导出和复核时保留冻结工程需求的审计链。
    problem.requirementProvenance = EngineeringRequirementProvenance();
    if (root.contains("engineeringRequirementProvenance")) {
        const QJsonObject provenance = root["engineeringRequirementProvenance"].toObject();
        problem.requirementProvenance.requirementFingerprint =
            provenance["requirementFingerprint"].toString().toStdString();
        problem.requirementProvenance.executionFingerprint =
            provenance["executionFingerprint"].toString().toStdString();
        problem.requirementProvenance.workcellFingerprint =
            provenance["workcellFingerprint"].toString().toStdString();
        problem.requirementProvenance.environmentFingerprint =
            provenance["environmentFingerprint"].toString().toStdString();
        problem.requirementProvenance.compilerVersion =
            provenance["compilerVersion"].toString().toStdString();
        // 旧项目没有该字段时保持为空，仍可按旧格式打开；新项目则完整保留审计时间线。
        problem.requirementProvenance.frozenAt = provenance["frozenAt"].toString().toStdString();
    }
    // 解析冻结执行契约：它是 Verified 阶段评价的直接输入。契约对象无效时视为
    // 致命错误返回，保证优化不会在缺少已验证需求时静默运行。
    problem.requirementExecution = RequirementExecutionSet();
    if (root.contains("requirementExecution")) {
        if (!root["requirementExecution"].isObject() ||
            !RequirementExecutionJson::fromObject(
                root["requirementExecution"].toObject(), problem.requirementExecution, error)) {
            if (error != nullptr && error->empty())
                *error = "Failed to parse frozen requirement execution contract.";
            return false;
        }
    }
    // 解析冻结场景快照：用于候选模型工厂重建工装与碰撞环境。
    // 旧项目没有该字段时保持空快照，仍可按 WORLD 任务正常运行。
    problem.scenarioSnapshot = StructureOptimizationScenarioSnapshot();
    if (root.contains("frozenScenarioSnapshot")) {
        const QJsonObject scenario = root["frozenScenarioSnapshot"].toObject();
        problem.scenarioSnapshot.schemaVersion = scenario["schemaVersion"].toInt();
        problem.scenarioSnapshot.sourceWorkCellPath = scenario["sourceWorkCellPath"].toString().toStdString();
        problem.scenarioSnapshot.sourceFileFingerprint = scenario["sourceFileFingerprint"].toString().toStdString();
        problem.scenarioSnapshot.snapshotFingerprint = scenario["snapshotFingerprint"].toString().toStdString();
        problem.scenarioSnapshot.deviceName = scenario["deviceName"].toString().toStdString();
        problem.scenarioSnapshot.environmentFingerprint = scenario["environmentFingerprint"].toString().toStdString();
        problem.scenarioSnapshot.stateFingerprint = scenario["stateFingerprint"].toString().toStdString();
        if (!scenario["sceneSpec"].isObject() ||
            !RobotModelSpecJson::fromObject(scenario["sceneSpec"].toObject(),
                                             problem.scenarioSnapshot.sceneSpec, error)) {
            if (error != nullptr) *error = "Failed to parse frozen scenario snapshot.";
            return false;
        }
    }

    // variables
    problem.variables.clear();
    if (root.contains("variables") && !root.value("variables").isArray()) {
        if (error != nullptr) *error = "Field 'variables' must be an array.";
        return false;
    }
    QJsonArray varsArr = root["variables"].toArray();
    for (const auto& val : varsArr) {
        if (!val.isObject()) {
            if (error != nullptr) *error = "Variable entries must be objects.";
            return false;
        }
        StructureDesignVariable variable;
        if (!designVariableFromJson(val.toObject(), variable, error)) return false;
        problem.variables.push_back(variable);
    }
    problem.canonicalModelShadow = CanonicalModelShadow();
    if (root.contains("canonicalModelShadow")) {
        if (!root["canonicalModelShadow"].isObject()) {
            if (error != nullptr) *error = "Canonical model shadow must be an object.";
            return false;
        }
        const QJsonObject shadow = root["canonicalModelShadow"].toObject();
        const QString status = shadow["status"].toString();
        if (status == "Current")
            problem.canonicalModelShadow.status = CanonicalModelShadowStatus::Current;
        else if (status == "Stale")
            problem.canonicalModelShadow.status = CanonicalModelShadowStatus::Stale;
        else if (status == "Invalid")
            problem.canonicalModelShadow.status = CanonicalModelShadowStatus::Invalid;
        else if (status == "CanonicalModelMissing")
            problem.canonicalModelShadow.status = CanonicalModelShadowStatus::CanonicalModelMissing;
        else {
            if (error != nullptr) *error = "Unknown canonical model shadow status: " +
                                           status.toStdString();
            return false;
        }
        KinematicBaselineSnapshot snapshot;
        if (!canonicalSnapshotFromJson(shadow.value("snapshot"), snapshot)) {
            if (error != nullptr) *error = "Invalid canonical model shadow snapshot.";
            return false;
        }
        problem.canonicalModelShadow.snapshot =
            std::make_shared< KinematicBaselineSnapshot >(snapshot);
    }

    // constraints
    problem.constraints.clear();
    if (root.contains("constraints") && !root.value("constraints").isArray()) {
        if (error != nullptr) *error = "Field 'constraints' must be an array.";
        return false;
    }
    QJsonArray consArr = root["constraints"].toArray();
    for (const auto& val : consArr) {
        if (!val.isObject()) {
            if (error != nullptr) *error = "Constraint entries must be objects.";
            return false;
        }
        StructureConstraint constraint;
        if (!constraintFromJson(val.toObject(), constraint, error)) return false;
        problem.constraints.push_back(constraint);
    }

    // weights
    if (root.contains("weights")) {
        if (!root.value("weights").isObject() ||
            !weightsFromJson(root.value("weights").toObject(), problem.weights, error))
            return false;
    }

    // objectives 反序列化：优先读取新字段；缺失时回退为按旧 weights 推导的目标。
    problem.objectives.clear();
    if (root.contains("objectives")) {
        if (!root.value("objectives").isArray()) {
            if (error != nullptr) *error = "Field 'objectives' must be an array.";
            return false;
        }
        for (const QJsonValue& value : root["objectives"].toArray()) {
            if (!value.isObject()) {
                if (error != nullptr) *error = "Objective entries must be objects.";
                return false;
            }
            ObjectiveTerm objective;
            if (!objectiveFromJson(value.toObject(), objective, error)) return false;
            problem.objectives.push_back(objective);
        }
    }
    else {
        problem.objectives = StructureOptimizationObjectiveProfile::legacyObjectives(problem.weights);
    }

    problem.metricConstraints.clear();
    if (root.contains("metricConstraints") && !root.value("metricConstraints").isArray()) {
        if (error != nullptr) *error = "Field 'metricConstraints' must be an array.";
        return false;
    }
    for (const QJsonValue& value : root["metricConstraints"].toArray()) {
        if (!value.isObject()) {
            if (error != nullptr) *error = "Metric constraint entries must be objects.";
            return false;
        }
        ConstraintRule constraint;
        if (!metricConstraintFromJson(value.toObject(), constraint, error)) return false;
        problem.metricConstraints.push_back(constraint);
    }

    // evaluationConfig
    if (root.contains("evaluationConfig")) {
        if (!root.value("evaluationConfig").isObject() ||
            !evalConfigFromJson(root.value("evaluationConfig").toObject(), problem.evaluation, error))
            return false;
    }

    // runConfig
    if (root.contains("runConfig")) {
        if (!root.value("runConfig").isObject() ||
            !runConfigFromJson(root.value("runConfig").toObject(), problem.run, error))
            return false;
    }

    problem.extensions = extensions;

    return true;
}

// 优化结果序列化：内嵌完整问题快照、候选摘要、诊断与灵敏度数据，
// 使结果文件可独立用于复核与导出，无需同时持有原始项目文件。
std::string StructureOptimizationJson::resultToJson(
    const StructureOptimizationProblem& problem,
    const StructureOptimizationResult& result)
{
    QJsonObject root;
    root["schemaVersion"] = SchemaVersion;
    root["type"]          = "StructureOptimizationResult";

    // 嵌入问题快照
    const std::string probJson = problemToJson(problem);
    QJsonDocument probDoc = QJsonDocument::fromJson(
        QString::fromStdString(probJson).toUtf8());
    if (!probDoc.isNull())
        root["problem"] = probDoc.object();

    // 结果字段
    root["startedAt"]             = QString::fromStdString(result.startedAt);
    root["completedAt"]           = QString::fromStdString(result.completedAt);
    root["canceled"]              = result.canceled;
    root["baselineCandidateIndex"] = result.baselineCandidateIndex;
    root["bestCandidateIndex"]    = result.bestCandidateIndex;
    QJsonObject baselineAudit;
    baselineAudit["index"] = result.baselineAudit.index;
    baselineAudit["candidateFingerprint"] =
        QString::fromStdString(result.baselineAudit.candidateFingerprint);
    baselineAudit["modelFingerprint"] =
        QString::fromStdString(result.baselineAudit.modelFingerprint);
    baselineAudit["environmentFingerprint"] =
        QString::fromStdString(result.baselineAudit.environmentFingerprint);
    baselineAudit["toolFingerprint"] =
        QString::fromStdString(result.baselineAudit.toolFingerprint);
    baselineAudit["planFingerprint"] =
        QString::fromStdString(result.baselineAudit.planFingerprint);
    root["baselineAudit"] = baselineAudit;

    // candidates — 摘要
    QJsonArray candArr;
    for (const auto& c : result.candidates) {
        QJsonObject cObj;
        cObj["index"]       = c.index;
        cObj["status"]      = candidateStatusToJson(c.status)["status"].toString();
        cObj["feasible"]    = c.feasible;
        cObj["totalScore"]  = jsonFiniteNumber(c.totalScore);
        if (!std::isfinite(c.totalScore))
            cObj["totalScoreAvailability"] = "Unavailable";
        QJsonArray vals;
        for (double v : c.values)
            appendFiniteNumber(vals, v);
        cObj["values"] = vals;
        candArr.append(cObj);
    }
    root["candidates"] = candArr;

    // diagnostics
    QJsonObject diag;
    diag["generatedCandidates"]   = result.diagnostics.generatedCandidates;
    diag["evaluatedCandidates"]   = result.diagnostics.evaluatedCandidates;
    diag["cacheHits"]            = result.diagnostics.cacheHits;
    diag["quickEvaluatedCandidates"] = result.diagnostics.quickEvaluatedCandidates;
    diag["verifiedEliteCandidates"] = result.diagnostics.verifiedEliteCandidates;
    diag["finalVerifiedCandidates"] = result.diagnostics.finalVerifiedCandidates;
    diag["sensitivityEvaluations"] = result.diagnostics.sensitivityEvaluations;
    diag["totalSeconds"]         = jsonFiniteNumber(result.diagnostics.totalSeconds);
    diag["modelBuildSeconds"]    = jsonFiniteNumber(result.diagnostics.modelBuildSeconds);
    diag["kinematicEvaluationSeconds"] =
        jsonFiniteNumber(result.diagnostics.kinematicEvaluationSeconds);
    diag["workspaceEvaluationSeconds"] =
        jsonFiniteNumber(result.diagnostics.workspaceEvaluationSeconds);
    root["diagnostics"] = diag;

    // sensitivity
    root["sensitivity"] = sensitivityResultToJson(result.sensitivity);

    // warnings
    QJsonArray warnArr;
    for (const auto& w : result.warnings) {
        QJsonObject wObj;
        wObj["code"]     = QString::fromStdString(w.code);
        wObj["message"]  = QString::fromStdString(w.message);
        warnArr.append(wObj);
    }
    root["warnings"] = warnArr;

    QJsonDocument doc(root);
    return doc.toJson(QJsonDocument::Indented).toStdString();
}

// =============================================================================
//  S60 当前权威 JSON Envelope
// =============================================================================

namespace {

// 当前文档的 canonical 单位始终是 SI。旧问题对象仍可能来自 UI，带有 mm/cm/deg
// 等显示单位，因此这里只在写当前 Envelope 时做一次明确的边界转换；运行时对象
// 本身不会被修改，也不会把显示单位重新写回 canonical 文档。无法解释的单位不允
// 许进入当前文档，否则会伪造米/弧度语义。
static bool isAngleVariable(const QString& kind)
{
    return kind == QLatin1String("JointRotationRoll") ||
           kind == QLatin1String("JointRotationPitch") ||
           kind == QLatin1String("JointRotationYaw");
}

// 只接受与变量种类同族、且含义明确的单位；返回 false 表示写出门必须拒绝。
static bool siFactor(const QString& sourceUnit, bool angle, double* factor)
{
    const QString unit = sourceUnit.trimmed().toLower();
    if (angle) {
        if (unit == "rad") { *factor = 1.0; return true; }
        if (unit == "deg" || unit == "degree" || unit == "degrees") {
            *factor = 3.141592653589793238462643383279502884 / 180.0;
            return true;
        }
        return false;
    }
    if (unit == "m") { *factor = 1.0; return true; }
    if (unit == "mm") { *factor = 1.0e-3; return true; }
    if (unit == "cm") { *factor = 1.0e-2; return true; }
    if (unit == "um" || unit == "µm") { *factor = 1.0e-6; return true; }
    return false;
}

static void convertVariableToSi(QJsonObject& variable)
{
    const QString kind = variable.value("kind").toString();
    const bool angle = isAngleVariable(kind);
    double factor = 0.0;
    if (!siFactor(variable.value("unit").toString(), angle, &factor)) {
        throw std::invalid_argument(
            "StructureOptimizationJson: cannot interpret unit '" +
            variable.value("unit").toString().toStdString() +
            "' for kind '" + kind.toStdString() + "' as a canonical SI quantity.");
    }
    variable["unit"] = angle ? QStringLiteral("rad") : QStringLiteral("m");
    const char* const numericFields[] = {
        "currentValue", "minimum", "maximum", "step", "preferredValue"
    };
    for (const char* field : numericFields) {
        const QJsonValue value = variable.value(QLatin1String(field));
        if (value.isDouble()) variable[QLatin1String(field)] = jsonFiniteNumber(value.toDouble() * factor);
    }
    const QJsonArray oldOptions = variable.value("discreteOptions").toArray();
    if (!oldOptions.isEmpty()) {
        QJsonArray options;
        for (const QJsonValue& option : oldOptions) {
            bool ok = false;
            const double number = option.isDouble()
                ? option.toDouble()
                : option.toString().toDouble(&ok);
            if (option.isDouble())
                ok = std::isfinite(number);
            options.append(ok ? jsonFiniteNumber(number * factor) : option);
        }
        variable["discreteOptions"] = options;
    }
}

// Qt 的 QJsonObject 保留插入顺序，而对象字段插入顺序不应改变文档身份。此递归
// 编码器按 key 排序后再计算 SHA-256，数组顺序仍然保留，因为变量/任务的顺序是
// 设计空间的语义组成部分。
static QByteArray canonicalJsonValue(const QJsonValue& value)
{
    if (value.isObject()) {
        const QJsonObject object = value.toObject();
        QStringList keys = object.keys();
        std::sort(keys.begin(), keys.end());
        QByteArray result("{");
        bool first = true;
        for (const QString& key : keys) {
            if (!first) result.append(',');
            first = false;
            const QByteArray encodedKey = QJsonDocument(QJsonObject{{key, key}})
                .toJson(QJsonDocument::Compact);
            const int colon = encodedKey.indexOf(':');
            result.append(encodedKey.left(colon));
            result.append(':');
            result.append(canonicalJsonValue(object.value(key)));
        }
        result.append('}');
        return result;
    }
    if (value.isArray()) {
        QByteArray result("[");
        const QJsonArray array = value.toArray();
        for (int i = 0; i < array.size(); ++i) {
            if (i != 0) result.append(',');
            result.append(canonicalJsonValue(array.at(i)));
        }
        result.append(']');
        return result;
    }
    if (value.isNull()) return QByteArray("null");
    if (value.isBool()) return value.toBool() ? QByteArray("true") : QByteArray("false");
    if (value.isDouble()) return QByteArray::number(value.toDouble(), 'g', 17);
    if (value.isString()) {
        const QByteArray encoded = QJsonDocument(QJsonArray{value})
            .toJson(QJsonDocument::Compact);
        return encoded.mid(1, encoded.size() - 2);
    }
    return QByteArray("null");
}

static bool requireSection(const QJsonObject& root, const char* name, int version,
                           std::string* error)
{
    const QJsonValue sectionValue = root.value(QLatin1String(name));
    if (!sectionValue.isObject() || sectionValue.toObject().value("schemaVersion").toInt() != version) {
        if (error != nullptr)
            *error = std::string("Current Envelope missing or unsupported section: ") + name;
        return false;
    }
    return true;
}

// 当前 Envelope 的根字段与旧 problem 文档不同，因此不能直接复用旧 reader 的
// known-key 表。未知根字段统一进入 extensions；已声明的扩展若与协议字段冲突，
// 立即拒绝，避免未来字段覆盖当前语义。
static bool readCurrentEnvelopeExtensions(const QJsonObject& root,
                                          QJsonObject& extensions,
                                          std::string* error)
{
    auto isKnownRootKey = [](const QString& key) {
        static const char* const keys[] = {
            "type", "schemaVersion", "designSpace", "plan", "objectives",
            "constraints", "config", "extensions"
        };
        for (const char* known : keys)
            if (key == QLatin1String(known)) return true;
        return false;
    };

    extensions = QJsonObject();
    if (root.contains("extensions")) {
        if (!root.value("extensions").isObject()) {
            if (error != nullptr) *error = "Current Envelope extensions must be an object.";
            return false;
        }
        const QJsonObject explicitExtensions = root.value("extensions").toObject();
        for (const QString& key : explicitExtensions.keys()) {
            if (isKnownRootKey(key)) {
                if (error != nullptr)
                    *error = "Current Envelope extensions cannot override root field '" +
                             key.toStdString() + "'.";
                return false;
            }
            extensions.insert(key, explicitExtensions.value(key));
        }
    }
    for (const QString& key : root.keys()) {
        if (isKnownRootKey(key)) continue;
        if (extensions.contains(key)) {
            if (error != nullptr)
                *error = "Current Envelope root extension field '" + key.toStdString() +
                         "' conflicts with explicit extensions.";
            return false;
        }
        extensions.insert(key, root.value(key));
    }
    return true;
}

// Binding 是持久化层与运行时适配器之间唯一稳定的连接点。这里只接受纯数据：
// id/adapterId/version 必须存在且类型正确，且同一变量不能出现两个绑定。
static bool validateCurrentBindings(const QJsonObject& designSpace, std::string* error)
{
    const QJsonArray variableArray = designSpace.value("variables").toArray();
    const QJsonArray bindingArray = designSpace.value("bindings").toArray();
    std::set<QString> variableIds;
    for (const QJsonValue& value : variableArray) {
        if (!value.isObject()) {
            if (error != nullptr) *error = "Current Envelope designSpace variable must be an object.";
            return false;
        }
        const QString id = value.toObject().value("id").toString();
        if (id.isEmpty() || variableIds.find(id) != variableIds.end()) {
            if (error != nullptr) *error = "Current Envelope variable ids must be unique and non-empty.";
            return false;
        }
        variableIds.insert(id);
    }
    std::set<QString> bindingIds;
    for (const QJsonValue& value : bindingArray) {
        if (!value.isObject()) {
            if (error != nullptr) *error = "Current Envelope binding must be an object.";
            return false;
        }
        const QJsonObject binding = value.toObject();
        const QJsonValue idValue = binding.value("id");
        const QJsonValue adapterValue = binding.value("adapterId");
        const QJsonValue versionValue = binding.value("version");
        const QString id = idValue.toString();
        const double version = versionValue.toDouble(-1.0);
        if (!idValue.isString() || id.isEmpty() || !adapterValue.isString() ||
            adapterValue.toString().isEmpty() || !versionValue.isDouble() ||
            !std::isfinite(version) || version <= 0.0 || std::floor(version) != version ||
            bindingIds.find(id) != bindingIds.end()) {
            if (error != nullptr)
                *error = "Current Envelope binding requires unique id, adapterId and positive version.";
            return false;
        }
        if (variableIds.find(id) == variableIds.end()) {
            if (error != nullptr)
                *error = "Current Envelope binding references an unknown variable id '" +
                         id.toStdString() + "'.";
            return false;
        }
        const QJsonObject variable = [&variableArray, &id]() {
            for (const QJsonValue& value : variableArray)
                if (value.toObject().value("id").toString() == id) return value.toObject();
            return QJsonObject();
        }();
        const QString expectedUnit = isAngleVariable(variable.value("kind").toString())
                                         ? QStringLiteral("rad")
                                         : QStringLiteral("m");
        if (variable.value("unit").toString() != expectedUnit ||
            (binding.contains("unit") && binding.value("unit").toString() != expectedUnit)) {
            if (error != nullptr)
                *error = "Current Envelope designSpace values must use SI units for binding '" +
                         id.toStdString() + "'.";
            return false;
        }
        bindingIds.insert(id);
    }
    return true;
}

} // namespace

std::string StructureOptimizationJson::currentEnvelopeToJson(
    const StructureOptimizationProblem& problem)
{
    const QJsonDocument legacyDocument = QJsonDocument::fromJson(
        QByteArray::fromStdString(problemToJson(problem)));
    const QJsonObject legacy = legacyDocument.object();

    QJsonObject root;
    root["type"] = "StructureOptimizationDocument";
    root["schemaVersion"] = StructureOptimizationDocument::SchemaVersion;

    QJsonObject designSpace;
    designSpace["schemaVersion"] = StructureOptimizationDocument::DesignSpaceSchemaVersion;
    designSpace["context"] = legacy.value("context");
    QJsonArray variables;
    for (const QJsonValue& value : legacy.value("variables").toArray()) {
        QJsonObject variable = value.toObject();
        convertVariableToSi(variable);
        variables.append(variable);
    }
    designSpace["variables"] = variables;
    QJsonArray bindings;
    for (const QJsonValue& value : variables) {
        const QJsonObject variable = value.toObject();
        QJsonObject binding;
        binding["id"] = variable.value("id");
        binding["adapterId"] = "structure.legacy-variable";
        binding["version"] = StructureOptimizationDocument::BindingSchemaVersion;
        binding["targetName"] = variable.value("targetName");
        binding["kind"] = variable.value("kind");
        binding["unit"] = variable.value("unit");
        bindings.append(binding);
    }
    designSpace["bindings"] = bindings;
    root["designSpace"] = designSpace;

    QJsonObject plan;
    plan["schemaVersion"] = StructureOptimizationDocument::PlanSchemaVersion;
    plan["tasks"] = legacy.value("tasks");
    root["plan"] = plan;

    QJsonObject objectives;
    objectives["schemaVersion"] = StructureOptimizationDocument::ObjectivesSchemaVersion;
    objectives["items"] = legacy.value("objectives");
    root["objectives"] = objectives;

    QJsonObject constraints;
    constraints["schemaVersion"] = StructureOptimizationDocument::ConstraintsSchemaVersion;
    constraints["structural"] = legacy.value("constraints");
    constraints["metric"] = legacy.value("metricConstraints");
    root["constraints"] = constraints;

    QJsonObject config;
    config["schemaVersion"] = StructureOptimizationDocument::ConfigSchemaVersion;
    config["weights"] = legacy.value("weights");
    config["evaluation"] = legacy.value("evaluationConfig");
    config["run"] = legacy.value("runConfig");
    for (const char* key : {"engineeringRequirementProvenance", "requirementExecution",
                            "frozenScenarioSnapshot", "canonicalModelShadow"}) {
        if (legacy.contains(QLatin1String(key))) config[QLatin1String(key)] = legacy.value(QLatin1String(key));
    }
    root["config"] = config;
    if (legacy.value("extensions").isObject()) root["extensions"] = legacy.value("extensions");

    return QJsonDocument(root).toJson(QJsonDocument::Indented).toStdString();
}

bool StructureOptimizationJson::currentEnvelopeFromJson(
    const std::string& json, StructureOptimizationProblem& problem, std::string* error)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(
        QByteArray::fromStdString(json), &parseError);
    if (!document.isObject()) {
        if (error != nullptr) *error = "Current Envelope JSON root is not an object.";
        return false;
    }
    const QJsonObject root = document.object();
    if (root.value("type").toString() != "StructureOptimizationDocument" ||
        root.value("schemaVersion").toInt() != StructureOptimizationDocument::SchemaVersion) {
        if (error != nullptr) *error = "Unsupported current StructureOptimizationDocument schema.";
        return false;
    }
    if (!requireSection(root, "designSpace", StructureOptimizationDocument::DesignSpaceSchemaVersion, error) ||
        !requireSection(root, "plan", StructureOptimizationDocument::PlanSchemaVersion, error) ||
        !requireSection(root, "objectives", StructureOptimizationDocument::ObjectivesSchemaVersion, error) ||
        !requireSection(root, "constraints", StructureOptimizationDocument::ConstraintsSchemaVersion, error) ||
        !requireSection(root, "config", StructureOptimizationDocument::ConfigSchemaVersion, error))
        return false;

    const QJsonObject designSpace = root.value("designSpace").toObject();
    const QJsonObject plan = root.value("plan").toObject();
    const QJsonObject objectives = root.value("objectives").toObject();
    const QJsonObject constraints = root.value("constraints").toObject();
    const QJsonObject config = root.value("config").toObject();
    if (!designSpace.value("context").isObject() || !designSpace.value("variables").isArray() ||
        !designSpace.value("bindings").isArray() || !plan.value("tasks").isArray() ||
        !objectives.value("items").isArray() || !constraints.value("structural").isArray() ||
        !constraints.value("metric").isArray()) {
        if (error != nullptr) *error = "Current Envelope canonical section has invalid shape.";
        return false;
    }
    if (!validateCurrentBindings(designSpace, error)) return false;

    QJsonObject legacy;
    legacy["schemaVersion"] = SchemaVersion;
    legacy["type"] = "StructureOptimizationProblem";
    legacy["context"] = designSpace.value("context");
    legacy["variables"] = designSpace.value("variables");
    legacy["tasks"] = plan.value("tasks");
    legacy["objectives"] = objectives.value("items");
    legacy["constraints"] = constraints.value("structural");
    legacy["metricConstraints"] = constraints.value("metric");
    legacy["weights"] = config.value("weights");
    legacy["evaluationConfig"] = config.value("evaluation");
    legacy["runConfig"] = config.value("run");
    for (const char* key : {"engineeringRequirementProvenance", "requirementExecution",
                            "frozenScenarioSnapshot", "canonicalModelShadow"}) {
        if (config.contains(QLatin1String(key))) legacy[QLatin1String(key)] = config.value(QLatin1String(key));
    }
    QJsonObject extensions;
    if (!readCurrentEnvelopeExtensions(root, extensions, error)) return false;
    if (!extensions.isEmpty()) legacy["extensions"] = extensions;

    StructureOptimizationProblem parsed;
    std::string parseMessage;
    if (!problemFromJson(QJsonDocument(legacy).toJson(QJsonDocument::Compact).toStdString(),
                         parsed, &parseMessage)) {
        if (error != nullptr) *error = "Current Envelope problem validation failed: " + parseMessage;
        return false;
    }
    problem = std::move(parsed);
    if (error != nullptr) error->clear();
    return true;
}

std::string StructureOptimizationJson::currentEnvelopeFingerprint(
    const StructureOptimizationProblem& problem)
{
    return currentEnvelopeFingerprint(currentEnvelopeToJson(problem));
}

std::string StructureOptimizationJson::currentEnvelopeFingerprint(const std::string& json)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(QByteArray::fromStdString(json), &parseError);
    if (!document.isObject()) return std::string();
    return QCryptographicHash::hash(canonicalJsonValue(document.object()), QCryptographicHash::Sha256)
        .toHex().toStdString();
}

} // namespace rws
