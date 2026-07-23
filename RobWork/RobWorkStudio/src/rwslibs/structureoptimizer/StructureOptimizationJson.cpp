#include "StructureOptimizationJson.hpp"

#include "StructureOptimizationTypes.hpp"

#include <rwslibs/robotanalysiscore/RobotAnalysisJson.hpp>
#include <rwslibs/robotmodelbuilder/RobotModelSpecJson.hpp>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace rws {

// =============================================================================
//  内部辅助函数
// =============================================================================

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
    }
    return {{"kind", "Unknown"}};
}

static StructureVariableKind variableKindFromJson(const QJsonObject& obj, bool* ok = nullptr)
{
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
    if (ok) *ok = false;
    return StructureVariableKind::JointPositionX;
}

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

static StructureConstraintKind constraintKindFromJson(const QJsonObject& obj, bool* ok = nullptr)
{
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

static StructureCandidateStatus candidateStatusFromJson(const QJsonObject& obj)
{
    const QString s = obj["status"].toString();
    if (s == "Pending")    return StructureCandidateStatus::Pending;
    if (s == "Feasible")   return StructureCandidateStatus::Feasible;
    if (s == "Infeasible") return StructureCandidateStatus::Infeasible;
    if (s == "Failed")     return StructureCandidateStatus::Failed;
    if (s == "Canceled")   return StructureCandidateStatus::Canceled;
    return StructureCandidateStatus::Pending;
}

// =============================================================================
//  TaskPoint -> QJsonObject
// =============================================================================

static QJsonObject taskPointToJson(const TaskPoint& pt)
{
    QJsonObject obj;
    obj["id"]       = QString::fromStdString(pt.id);
    obj["name"]     = QString::fromStdString(pt.name);
    obj["refFrame"] = QString::fromStdString(pt.refFrame);
    obj["tcpFrame"] = QString::fromStdString(pt.tcpFrame);
    QJsonArray pos;
    pos.append(pt.position[0]);
    pos.append(pt.position[1]);
    pos.append(pt.position[2]);
    obj["position"] = pos;
    QJsonArray rpy;
    rpy.append(pt.rpyDeg[0]);
    rpy.append(pt.rpyDeg[1]);
    rpy.append(pt.rpyDeg[2]);
    obj["rpyDeg"] = rpy;
    obj["enabled"] = pt.enabled;
    obj["weight"]  = pt.weight;
    return obj;
}

static TaskPoint taskPointFromJson(const QJsonObject& obj)
{
    TaskPoint pt;
    pt.id       = obj["id"].toString().toStdString();
    pt.name     = obj["name"].toString().toStdString();
    pt.refFrame = obj["refFrame"].toString().toStdString();
    pt.tcpFrame = obj["tcpFrame"].toString().toStdString();
    QJsonArray pos = obj["position"].toArray();
    if (pos.size() >= 3) {
        pt.position[0] = pos[0].toDouble();
        pt.position[1] = pos[1].toDouble();
        pt.position[2] = pos[2].toDouble();
    }
    QJsonArray rpy = obj["rpyDeg"].toArray();
    if (rpy.size() >= 3) {
        pt.rpyDeg[0] = rpy[0].toDouble();
        pt.rpyDeg[1] = rpy[1].toDouble();
        pt.rpyDeg[2] = rpy[2].toDouble();
    }
    pt.enabled = obj["enabled"].toBool(true);
    pt.weight  = obj["weight"].toDouble(1.0);
    return pt;
}

// =============================================================================
//  variable / constraint / weight / evalConfig / runConfig -> QJsonObject
// =============================================================================

static QJsonObject designVariableToJson(const StructureDesignVariable& var)
{
    QJsonObject obj;
    obj["id"]             = QString::fromStdString(var.id);
    obj["label"]          = QString::fromStdString(var.label);
    obj["targetName"]     = QString::fromStdString(var.targetName);
    obj["unit"]           = QString::fromStdString(var.unit);
    obj["kind"]           = variableKindToJson(var.kind)["kind"].toString();
    obj["currentValue"]   = var.currentValue;
    obj["minimum"]        = var.minimum;
    obj["maximum"]        = var.maximum;
    obj["step"]           = var.step;
    obj["preferredValue"] = var.preferredValue;
    obj["preferenceWeight"] = var.preferenceWeight;
    obj["enabled"]        = var.enabled;
    obj["syncAssociatedGeometry"] = var.syncAssociatedGeometry;
    return obj;
}

static StructureDesignVariable designVariableFromJson(const QJsonObject& obj)
{
    StructureDesignVariable var;
    var.id             = obj["id"].toString().toStdString();
    var.label          = obj["label"].toString().toStdString();
    var.targetName     = obj["targetName"].toString().toStdString();
    var.unit           = obj["unit"].toString().toStdString();
    var.kind           = variableKindFromJson(obj);
    var.currentValue   = obj["currentValue"].toDouble();
    var.minimum        = obj["minimum"].toDouble();
    var.maximum        = obj["maximum"].toDouble();
    var.step           = obj["step"].toDouble(0.1);
    var.preferredValue = obj["preferredValue"].toDouble();
    var.preferenceWeight = obj["preferenceWeight"].toDouble();
    var.enabled        = obj["enabled"].toBool(true);
    var.syncAssociatedGeometry = obj["syncAssociatedGeometry"].toBool(false);
    return var;
}

static QJsonObject constraintToJson(const StructureConstraint& con)
{
    QJsonObject obj;
    obj["id"]                = QString::fromStdString(con.id);
    obj["label"]             = QString::fromStdString(con.label);
    obj["targetName"]        = QString::fromStdString(con.targetName);
    obj["kind"]              = constraintKindToJson(con.kind)["kind"].toString();
    obj["threshold"]         = con.threshold;
    obj["secondaryThreshold"] = con.secondaryThreshold;
    obj["enabled"]           = con.enabled;
    obj["hard"]              = con.hard;
    return obj;
}

static StructureConstraint constraintFromJson(const QJsonObject& obj)
{
    StructureConstraint con;
    con.id                = obj["id"].toString().toStdString();
    con.label             = obj["label"].toString().toStdString();
    con.targetName        = obj["targetName"].toString().toStdString();
    con.kind              = constraintKindFromJson(obj);
    con.threshold         = obj["threshold"].toDouble();
    con.secondaryThreshold = obj["secondaryThreshold"].toDouble();
    con.enabled           = obj["enabled"].toBool(true);
    con.hard              = obj["hard"].toBool(true);
    return con;
}

static QJsonObject weightsToJson(const StructureOptimizationWeights& w)
{
    QJsonObject obj;
    obj["reachability"  ] = w.reachability;
    obj["manipulability"] = w.manipulability;
    obj["jointMargin"   ] = w.jointMargin;
    obj["collision"     ] = w.collision;
    obj["compactness"   ] = w.compactness;
    obj["preference"    ] = w.preference;
    return obj;
}

static void weightsFromJson(const QJsonObject& obj, StructureOptimizationWeights& w)
{
    w.reachability   = obj["reachability"  ].toDouble(0.35);
    w.manipulability = obj["manipulability"].toDouble(0.20);
    w.jointMargin    = obj["jointMargin"   ].toDouble(0.15);
    w.collision      = obj["collision"     ].toDouble(0.15);
    w.compactness    = obj["compactness"   ].toDouble(0.10);
    w.preference     = obj["preference"    ].toDouble(0.05);
}

static QJsonObject evalConfigToJson(const StructureEvaluationConfig& cfg)
{
    QJsonObject obj;
    obj["checkCollision"] = cfg.checkCollision;
    // thresholds / workspace 暂简化
    return obj;
}

static void evalConfigFromJson(const QJsonObject& obj, StructureEvaluationConfig& cfg)
{
    cfg.checkCollision = obj["checkCollision"].toBool(true);
}

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
    obj["randomSeed"]            = static_cast<int>(run.randomSeed);
    return obj;
}

static void runConfigFromJson(const QJsonObject& obj, StructureOptimizationRunConfig& run)
{
    run.strategy               = static_cast<StructureStrategyKind>(obj["strategy"].toInt(2));
    run.candidateCount         = obj["candidateCount"].toInt(300);
    run.eliteCount             = obj["eliteCount"].toInt(20);
    run.localEliteCount        = obj["localEliteCount"].toInt(5);
    run.finalVerificationCount = obj["finalVerificationCount"].toInt(3);
    run.maxLocalSweeps         = obj["maxLocalSweeps"].toInt(20);
    run.gridSteps              = obj["gridSteps"].toInt(3);
    run.randomSeed             = static_cast<unsigned int>(obj["randomSeed"].toInt(1));
}

// =============================================================================
//  Sensitivity JSON
// =============================================================================

static QJsonObject sensitivityEntryToJson(const StructureSensitivityEntry& e)
{
    QJsonObject obj;
    obj["variableId"]       = QString::fromStdString(e.variableId);
    obj["delta"]            = e.delta;
    obj["perturbedValue"]   = e.perturbedValue;
    obj["scoreDrop"]        = e.scoreDrop;
    obj["feasible"]         = e.feasible;
    QJsonArray vc;
    for (const auto& c : e.violatedConstraints)
        vc.append(QString::fromStdString(c));
    obj["violatedConstraints"] = vc;
    return obj;
}

static QJsonObject sensitivityResultToJson(const StructureSensitivityResult& sr)
{
    QJsonObject obj;
    QJsonArray arr;
    for (const auto& e : sr.entries)
        arr.append(sensitivityEntryToJson(e));
    obj["entries"]           = arr;
    obj["maximumScoreDrop"]  = sr.maximumScoreDrop;
    obj["meanScoreDrop"]     = sr.meanScoreDrop;
    QJsonArray cids;
    for (const auto& id : sr.criticalVariableIds)
        cids.append(QString::fromStdString(id));
    obj["criticalVariableIds"] = cids;
    obj["robustnessGrade"]   = QString::fromStdString(sr.robustnessGrade);
    return obj;
}

// =============================================================================
//  公有的 problemToJson / problemFromJson
// =============================================================================

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

    // weights
    root["weights"] = weightsToJson(problem.weights);

    // evaluationConfig
    root["evaluationConfig"] = evalConfigToJson(problem.evaluation);

    // runConfig
    root["runConfig"] = runConfigToJson(problem.run);

    QJsonDocument doc(root);
    return doc.toJson(QJsonDocument::Indented).toStdString();
}

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

    // schemaVersion
    const int sv = root["schemaVersion"].toInt();
    if (sv != SchemaVersion) {
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
    QJsonArray tasksArr = root["tasks"].toArray();
    for (const auto& val : tasksArr) {
        QJsonObject tObj = val.toObject();
        OptimizationTaskPoint tp;
        tp.point   = taskPointFromJson(tObj);
        tp.required = tObj["required"].toBool(true);
        problem.tasks.push_back(tp);
    }

    // variables
    problem.variables.clear();
    QJsonArray varsArr = root["variables"].toArray();
    for (const auto& val : varsArr)
        problem.variables.push_back(designVariableFromJson(val.toObject()));

    // constraints
    problem.constraints.clear();
    QJsonArray consArr = root["constraints"].toArray();
    for (const auto& val : consArr)
        problem.constraints.push_back(constraintFromJson(val.toObject()));

    // weights
    if (root.contains("weights"))
        weightsFromJson(root["weights"].toObject(), problem.weights);

    // evaluationConfig
    if (root.contains("evaluationConfig"))
        evalConfigFromJson(root["evaluationConfig"].toObject(), problem.evaluation);

    // runConfig
    if (root.contains("runConfig"))
        runConfigFromJson(root["runConfig"].toObject(), problem.run);

    return true;
}

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

    // candidates — 摘要
    QJsonArray candArr;
    for (const auto& c : result.candidates) {
        QJsonObject cObj;
        cObj["index"]       = c.index;
        cObj["status"]      = candidateStatusToJson(c.status)["status"].toString();
        cObj["feasible"]    = c.feasible;
        cObj["totalScore"]  = c.totalScore;
        QJsonArray vals;
        for (double v : c.values)
            vals.append(v);
        cObj["values"] = vals;
        candArr.append(cObj);
    }
    root["candidates"] = candArr;

    // diagnostics
    QJsonObject diag;
    diag["generatedCandidates"]   = result.diagnostics.generatedCandidates;
    diag["evaluatedCandidates"]   = result.diagnostics.evaluatedCandidates;
    diag["cacheHits"]            = result.diagnostics.cacheHits;
    diag["totalSeconds"]         = result.diagnostics.totalSeconds;
    diag["modelBuildSeconds"]    = result.diagnostics.modelBuildSeconds;
    diag["kinematicEvaluationSeconds"] = result.diagnostics.kinematicEvaluationSeconds;
    diag["workspaceEvaluationSeconds"] = result.diagnostics.workspaceEvaluationSeconds;
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

} // namespace rws
