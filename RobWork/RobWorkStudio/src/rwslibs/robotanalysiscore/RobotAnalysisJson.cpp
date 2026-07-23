#include "RobotAnalysisJson.hpp"

#include <rwslibs/robotmodelbuilder/RobotModelSpecJson.hpp>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QString>

namespace {
QString qs (const std::string& value)
{
    return QString::fromStdString (value);
}

std::string ss (const QString& value)
{
    return value.toStdString ();
}

const char* toString (rws::AnalysisStatus status)
{
    switch (status) {
        case rws::AnalysisStatus::Pass:    return "Pass";
        case rws::AnalysisStatus::Warning: return "Warning";
        case rws::AnalysisStatus::Fail:    return "Fail";
        case rws::AnalysisStatus::Unknown:
        default:                           return "Unknown";
    }
}

rws::AnalysisStatus analysisStatusFromString (const QString& status)
{
    if (status == "Pass")
        return rws::AnalysisStatus::Pass;
    if (status == "Warning")
        return rws::AnalysisStatus::Warning;
    if (status == "Fail")
        return rws::AnalysisStatus::Fail;
    return rws::AnalysisStatus::Unknown;
}

const char* toString (rws::TaskPointType type)
{
    switch (type) {
        case rws::TaskPointType::Pick:    return "Pick";
        case rws::TaskPointType::Place:   return "Place";
        case rws::TaskPointType::Weld:    return "Weld";
        case rws::TaskPointType::Glue:    return "Glue";
        case rws::TaskPointType::Inspect: return "Inspect";
        case rws::TaskPointType::Screw:   return "Screw";
        case rws::TaskPointType::Custom:  return "Custom";
        case rws::TaskPointType::Generic:
        default:                          return "Generic";
    }
}

rws::TaskPointType taskPointTypeFromString (const QString& type)
{
    if (type == "Pick")
        return rws::TaskPointType::Pick;
    if (type == "Place")
        return rws::TaskPointType::Place;
    if (type == "Weld")
        return rws::TaskPointType::Weld;
    if (type == "Glue")
        return rws::TaskPointType::Glue;
    if (type == "Inspect")
        return rws::TaskPointType::Inspect;
    if (type == "Screw")
        return rws::TaskPointType::Screw;
    if (type == "Custom")
        return rws::TaskPointType::Custom;
    return rws::TaskPointType::Generic;
}

QJsonArray array3 (const std::array< double, 3 >& values)
{
    QJsonArray array;
    array.append (values[0]);
    array.append (values[1]);
    array.append (values[2]);
    return array;
}

QJsonArray array6 (const std::array< double, 6 >& values)
{
    QJsonArray array;
    for (const double value : values) {
        array.append (value);
    }
    return array;
}

std::array< double, 3 > readArray3 (const QJsonObject& object, const char* key,
                                    const std::array< double, 3 >& fallback)
{
    const QJsonArray array = object.value (key).toArray ();
    if (array.size () != 3)
        return fallback;
    return {{array.at (0).toDouble (), array.at (1).toDouble (), array.at (2).toDouble ()}};
}

std::array< double, 6 > readArray6 (const QJsonObject& object, const char* key,
                                    const std::array< double, 6 >& fallback)
{
    const QJsonArray array = object.value (key).toArray ();
    if (array.size () != 6)
        return fallback;
    return {{array.at (0).toDouble (), array.at (1).toDouble (), array.at (2).toDouble (),
             array.at (3).toDouble (), array.at (4).toDouble (), array.at (5).toDouble ()}};
}

QJsonObject poseToleranceToObject (const rws::PoseTolerance& tolerance)
{
    QJsonObject object;
    object["positionMeters"]    = tolerance.positionMeters;
    object["orientationDeg"]    = tolerance.orientationDeg;
    object["allowToolRollFree"] = tolerance.allowToolRollFree;
    return object;
}

rws::PoseTolerance poseToleranceFromObject (const QJsonObject& object)
{
    rws::PoseTolerance tolerance;
    tolerance.positionMeters    = object.value ("positionMeters").toDouble (tolerance.positionMeters);
    tolerance.orientationDeg    = object.value ("orientationDeg").toDouble (tolerance.orientationDeg);
    tolerance.allowToolRollFree = object.value ("allowToolRollFree").toBool (tolerance.allowToolRollFree);
    return tolerance;
}

QJsonObject taskPointToObject (const rws::TaskPoint& point)
{
    QJsonObject object;
    object["id"]        = qs (point.id);
    object["name"]      = qs (point.name);
    object["type"]      = toString (point.type);
    object["refFrame"]  = qs (point.refFrame);
    object["tcpFrame"]  = qs (point.tcpFrame);
    object["position"]  = array3 (point.position);
    object["rpyDeg"]    = array3 (point.rpyDeg);
    object["tolerance"] = poseToleranceToObject (point.tolerance);
    object["weight"]    = point.weight;
    object["enabled"]   = point.enabled;
    object["note"]      = qs (point.note);
    return object;
}

rws::TaskPoint taskPointFromObject (const QJsonObject& object)
{
    rws::TaskPoint point;
    point.id        = ss (object.value ("id").toString ());
    point.name      = ss (object.value ("name").toString ());
    point.type      = taskPointTypeFromString (object.value ("type").toString ());
    point.refFrame  = ss (object.value ("refFrame").toString (qs (point.refFrame)));
    point.tcpFrame  = ss (object.value ("tcpFrame").toString (qs (point.tcpFrame)));
    point.position  = readArray3 (object, "position", point.position);
    point.rpyDeg    = readArray3 (object, "rpyDeg", point.rpyDeg);
    point.tolerance = poseToleranceFromObject (object.value ("tolerance").toObject ());
    point.weight    = object.value ("weight").toDouble (point.weight);
    point.enabled   = object.value ("enabled").toBool (point.enabled);
    point.note      = ss (object.value ("note").toString ());
    return point;
}

QJsonObject payloadToObject (const rws::PayloadSpec& payload)
{
    QJsonObject object;
    object["name"]    = qs (payload.name);
    object["mass"]    = payload.mass;
    object["cog"]     = array3 (payload.cog);
    object["inertia"] = array6 (payload.inertia);
    return object;
}

rws::PayloadSpec payloadFromObject (const QJsonObject& object)
{
    rws::PayloadSpec payload;
    payload.name    = ss (object.value ("name").toString (qs (payload.name)));
    payload.mass    = object.value ("mass").toDouble (payload.mass);
    payload.cog     = readArray3 (object, "cog", payload.cog);
    payload.inertia = readArray6 (object, "inertia", payload.inertia);
    return payload;
}

QJsonObject warningToObject (const rws::AnalysisWarning& warning)
{
    QJsonObject object;
    object["code"]     = qs (warning.code);
    object["message"]  = qs (warning.message);
    object["source"]   = qs (warning.source);
    object["severity"] = toString (warning.severity);
    return object;
}

rws::AnalysisWarning warningFromObject (const QJsonObject& object)
{
    rws::AnalysisWarning warning;
    warning.code     = ss (object.value ("code").toString ());
    warning.message  = ss (object.value ("message").toString ());
    warning.source   = ss (object.value ("source").toString ());
    warning.severity = analysisStatusFromString (object.value ("severity").toString ());
    return warning;
}

QJsonArray warningsToArray (const std::vector< rws::AnalysisWarning >& warnings)
{
    QJsonArray array;
    for (const rws::AnalysisWarning& warning : warnings) {
        array.append (warningToObject (warning));
    }
    return array;
}

std::vector< rws::AnalysisWarning > warningsFromArray (const QJsonArray& array)
{
    std::vector< rws::AnalysisWarning > warnings;
    for (const QJsonValue& value : array) {
        warnings.push_back (warningFromObject (value.toObject ()));
    }
    return warnings;
}

QJsonObject metricToObject (const rws::MetricValue& metric)
{
    QJsonObject object;
    object["name"]  = qs (metric.name);
    object["value"] = metric.value;
    object["unit"]  = qs (metric.unit);
    return object;
}

rws::MetricValue metricFromObject (const QJsonObject& object)
{
    rws::MetricValue metric;
    metric.name  = ss (object.value ("name").toString ());
    metric.value = object.value ("value").toDouble (metric.value);
    metric.unit  = ss (object.value ("unit").toString ());
    return metric;
}

QJsonArray metricsToArray (const std::vector< rws::MetricValue >& metrics)
{
    QJsonArray array;
    for (const rws::MetricValue& metric : metrics) {
        array.append (metricToObject (metric));
    }
    return array;
}

std::vector< rws::MetricValue > metricsFromArray (const QJsonArray& array)
{
    std::vector< rws::MetricValue > metrics;
    for (const QJsonValue& value : array) {
        metrics.push_back (metricFromObject (value.toObject ()));
    }
    return metrics;
}

QJsonObject jointSummaryToObject (const rws::JointAnalysisSummary& joint)
{
    QJsonObject object;
    object["jointName"] = qs (joint.jointName);
    object["status"]    = toString (joint.status);
    object["metrics"]   = metricsToArray (joint.metrics);
    object["warnings"]  = warningsToArray (joint.warnings);
    return object;
}

rws::JointAnalysisSummary jointSummaryFromObject (const QJsonObject& object)
{
    rws::JointAnalysisSummary joint;
    joint.jointName = ss (object.value ("jointName").toString ());
    joint.status    = analysisStatusFromString (object.value ("status").toString ());
    joint.metrics   = metricsFromArray (object.value ("metrics").toArray ());
    joint.warnings  = warningsFromArray (object.value ("warnings").toArray ());
    return joint;
}

QJsonObject rootObject (const char* type, const QJsonObject& data)
{
    QJsonObject root;
    root["schema"] = "RobotAnalysisCore";
    root["type"]   = type;
    root["data"]   = data;
    return root;
}

std::string writeRoot (const QJsonObject& root)
{
    return ss (QString::fromUtf8 (QJsonDocument (root).toJson (QJsonDocument::Compact)));
}

bool parseRoot (const std::string& json, QJsonObject& data, std::string* error)
{
    QJsonParseError parseError;
    const QJsonDocument document =
        QJsonDocument::fromJson (QByteArray::fromStdString (json), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject ()) {
        if (error)
            *error = ss (parseError.errorString ());
        return false;
    }

    const QJsonObject root = document.object ();
    data                   = root.value ("data").toObject ();
    if (data.isEmpty () && !root.contains ("data")) {
        if (error)
            *error = "JSON root does not contain a data object.";
        return false;
    }
    if (error)
        error->clear ();
    return true;
}
}    // namespace

namespace rws {

std::string RobotAnalysisJson::toJson (const TaskPoint& point)
{
    return writeRoot (rootObject ("TaskPoint", taskPointToObject (point)));
}

std::string RobotAnalysisJson::toJson (const PayloadSpec& payload)
{
    return writeRoot (rootObject ("PayloadSpec", payloadToObject (payload)));
}

std::string RobotAnalysisJson::toJson (const RobotDesignContext& context)
{
    QJsonObject data;
    data["projectName"]               = qs (context.projectName);
    data["robotName"]                 = qs (context.robotName);
    data["sourceModelPath"]           = qs (context.sourceModelPath);
    data["sourceScenePath"]           = qs (context.sourceScenePath);
    data["sourceDynamicWorkCellPath"] = qs (context.sourceDynamicWorkCellPath);
    data["modelSpec"]                 = RobotModelSpecJson::toObject (context.modelSpec);
    data["modelSpecSchemaVersion"]    = RobotModelSpecJson::SchemaVersion;
    data["modelSpecRobotName"]        = qs (context.modelSpec.robotName);
    data["deviceName"]                = qs (context.deviceName);
    data["baseFrame"]                 = qs (context.baseFrame);
    data["tcpFrame"]                  = qs (context.tcpFrame);
    data["refFrame"]                  = qs (context.refFrame);
    data["payload"]                   = payloadToObject (context.payload);

    QJsonArray taskPoints;
    for (const TaskPoint& point : context.taskPoints) {
        taskPoints.append (taskPointToObject (point));
    }
    data["taskPoints"] = taskPoints;

    return writeRoot (rootObject ("RobotDesignContext", data));
}

std::string RobotAnalysisJson::toJson (const AnalysisResult& result)
{
    QJsonObject data;
    QJsonObject header;
    header["pluginName"]    = qs (result.header.pluginName);
    header["pluginVersion"] = qs (result.header.pluginVersion);
    header["robotName"]     = qs (result.header.robotName);
    header["createdAt"]     = qs (result.header.createdAt);
    data["header"]          = header;
    data["status"]          = toString (result.status);
    data["score"]           = result.score;

    QJsonArray joints;
    for (const JointAnalysisSummary& joint : result.jointSummaries) {
        joints.append (jointSummaryToObject (joint));
    }
    data["jointSummaries"] = joints;
    data["warnings"]       = warningsToArray (result.warnings);
    return writeRoot (rootObject ("AnalysisResult", data));
}

bool RobotAnalysisJson::fromJson (const std::string& json, TaskPoint& point, std::string* error)
{
    QJsonObject data;
    if (!parseRoot (json, data, error))
        return false;
    point = taskPointFromObject (data);
    return true;
}

bool RobotAnalysisJson::fromJson (const std::string& json, PayloadSpec& payload, std::string* error)
{
    QJsonObject data;
    if (!parseRoot (json, data, error))
        return false;
    payload = payloadFromObject (data);
    return true;
}

bool RobotAnalysisJson::fromJson (const std::string& json, RobotDesignContext& context,
                                  std::string* error)
{
    QJsonObject data;
    if (!parseRoot (json, data, error))
        return false;

    context.projectName               = ss (data.value ("projectName").toString ());
    context.robotName                 = ss (data.value ("robotName").toString ());
    context.sourceModelPath           = ss (data.value ("sourceModelPath").toString ());
    context.sourceScenePath           = ss (data.value ("sourceScenePath").toString ());
    context.sourceDynamicWorkCellPath = ss (data.value ("sourceDynamicWorkCellPath").toString ());

    if (data.contains ("modelSpec")) {
        std::string msError;
        if (!RobotModelSpecJson::fromObject (data.value ("modelSpec").toObject (),
                                              context.modelSpec, &msError)) {
            if (error)
                *error = "RobotDesignContext.modelSpec:" + msError;
            return false;
        }
    } else {
        context.modelSpec.robotName = ss (data.value ("modelSpecRobotName").toString ());
    }

    context.deviceName                = ss (data.value ("deviceName").toString ());
    context.baseFrame                 = ss (data.value ("baseFrame").toString (qs (context.baseFrame)));
    context.tcpFrame                  = ss (data.value ("tcpFrame").toString (qs (context.tcpFrame)));
    context.refFrame                  = ss (data.value ("refFrame").toString (qs (context.refFrame)));
    context.payload                   = payloadFromObject (data.value ("payload").toObject ());
    context.taskPoints.clear ();
    for (const QJsonValue& value : data.value ("taskPoints").toArray ()) {
        context.taskPoints.push_back (taskPointFromObject (value.toObject ()));
    }
    return true;
}

bool RobotAnalysisJson::fromJson (const std::string& json, AnalysisResult& result,
                                  std::string* error)
{
    QJsonObject data;
    if (!parseRoot (json, data, error))
        return false;

    const QJsonObject header       = data.value ("header").toObject ();
    result.header.pluginName       = ss (header.value ("pluginName").toString ());
    result.header.pluginVersion    = ss (header.value ("pluginVersion").toString ());
    result.header.robotName        = ss (header.value ("robotName").toString ());
    result.header.createdAt        = ss (header.value ("createdAt").toString ());
    result.status                  = analysisStatusFromString (data.value ("status").toString ());
    result.score                   = data.value ("score").toDouble (result.score);
    result.jointSummaries.clear ();
    for (const QJsonValue& value : data.value ("jointSummaries").toArray ()) {
        result.jointSummaries.push_back (jointSummaryFromObject (value.toObject ()));
    }
    result.warnings = warningsFromArray (data.value ("warnings").toArray ());
    return true;
}

}    // namespace rws
