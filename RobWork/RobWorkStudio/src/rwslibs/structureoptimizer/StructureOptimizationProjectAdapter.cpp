#include "StructureOptimizationProjectAdapter.hpp"

#include "CanonicalModelShadowService.hpp"
#include "StructureOptimizationJson.hpp"
#include "StructureOptimizationValidation.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace rws {

namespace {

const int ProjectSchemaVersion = 1;

bool hasInvalidContext(const StructureOptimizationProblem& problem)
{
    std::string reason;
    return !StructureOptimizationValidation::hasCompleteModel(
        problem.context.modelSpec, &reason);
}

void setError(QString* error, const QString& value)
{
    if (error != nullptr)
        *error = value;
}

StructureOptimizationProblem portableProblem(const QString& projectPath,
                                             const StructureOptimizationProblem& problem)
{
    StructureOptimizationProblem portable = problem;
    const QString saveDirectory = QString::fromStdString(portable.context.modelSpec.saveDirectory);
    if (saveDirectory.isEmpty())
        return portable;

    // 内存中的模型目录可为绝对路径，方便编辑器与导出服务直接访问。仅当输出目录
    // 位于项目目录内部时才相对化：项目外模型的绝对路径属于模型快照指纹的一部分，
    // 强行改写会破坏既有溯源校验并把“外部依赖”伪装为项目内资源。
    const QDir projectDirectory(QFileInfo(projectPath).absolutePath());
    const QFileInfo directoryInfo(saveDirectory);
    const QString absoluteDirectory = directoryInfo.isRelative() ?
        projectDirectory.absoluteFilePath(saveDirectory) : directoryInfo.absoluteFilePath();
    const QString relativeDirectory = projectDirectory.relativeFilePath(absoluteDirectory);
    if (relativeDirectory == "." ||
        (!relativeDirectory.startsWith("../") && relativeDirectory != "..")) {
        portable.context.modelSpec.saveDirectory =
            QDir::fromNativeSeparators(relativeDirectory).toStdString();
    }
    return portable;
}

} // namespace

bool StructureOptimizationProjectAdapter::loadProject(
    const QString& path, StructureOptimizationProblem& out, int* selectedCandidateIndex,
    QString* error)
{
    return loadProject(path, out, selectedCandidateIndex, error, QString());
}

bool StructureOptimizationProjectAdapter::loadProject(
    const QString& path, StructureOptimizationProblem& out, int* selectedCandidateIndex,
    QString* error, const QString& projectRoot)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(error, "StructureOptimization.Project.OpenFailed: " + file.errorString());
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (!document.isObject()) {
        setError(error, "StructureOptimization.Project.InvalidJson: " + parseError.errorString());
        return false;
    }

    const QJsonObject root = document.object();
    if (root.value("type").toString() != "StructureOptimizationProject" ||
        root.value("schemaVersion").toInt() != ProjectSchemaVersion) {
        setError(error, "StructureOptimization.Project.UnsupportedSchema");
        return false;
    }
    if (!root.value("problem").isObject()) {
        setError(error, "StructureOptimization.Project.MissingProblem");
        return false;
    }

    const QJsonDocument problemDocument(root.value("problem").toObject());
    std::string parseMessage;
    StructureOptimizationProblem loaded;
    if (!StructureOptimizationJson::problemFromJson(
            problemDocument.toJson(QJsonDocument::Compact).toStdString(), loaded,
            &parseMessage)) {
        setError(error, "StructureOptimization.Project.InvalidProblem: " +
                            QString::fromStdString(parseMessage));
        return false;
    }
    if (hasInvalidContext(loaded)) {
        setError(error, "StructureOptimization.Context.Invalid: complete RobotModelSpec is required.");
        return false;
    }

    const QString modelDirectory = QString::fromStdString(loaded.context.modelSpec.saveDirectory);
    if (!modelDirectory.isEmpty() && QFileInfo(modelDirectory).isRelative()) {
        loaded.context.modelSpec.saveDirectory = QDir(QFileInfo(path).absolutePath())
                                                    .absoluteFilePath(modelDirectory)
                                                    .toStdString();
    }
    loaded.scenarioSnapshot.baseDirectory =
        QFileInfo(projectRoot.isEmpty() ? QFileInfo(path).absolutePath() : projectRoot)
            .absoluteFilePath()
            .toStdString();

    if (selectedCandidateIndex != nullptr)
        *selectedCandidateIndex = root.value("ui").toObject().value("selectedCandidateIndex").toInt(-1);
    out = loaded;
    if (error != nullptr)
        error->clear();
    return true;
}

bool StructureOptimizationProjectAdapter::loadProject(
    const QString& path, const KinematicImportRequest& importRequest,
    StructureOptimizationProblem& out, int* selectedCandidateIndex, QString* error)
{
    StructureOptimizationProblem loaded;
    if (!loadProject(path, loaded, selectedCandidateIndex, error))
        return false;

    const KinematicImportResult imported = KinematicModelImporter::import(importRequest);
    if (!imported.ok) {
        setError(error, "StructureOptimization.Project.CanonicalSourceInvalid");
        return false;
    }
    loaded.canonicalModelShadow.status = CanonicalModelShadowService::assess(
        loaded.canonicalModelShadow, imported.model);
    out = std::move(loaded);
    if (error != nullptr) error->clear();
    return true;
}

// 保存优化项目：先序列化为规范 JSON 字节，再以 QSaveFile 原子写入目标路径。
// 项目 Provider 的 saveProjectDocument 也经由本函数写暂存文件，保证与手动导出
// 使用完全一致的序列化规则。
bool StructureOptimizationProjectAdapter::saveProject(
    const QString& path, const StructureOptimizationProblem& problem, int selectedCandidateIndex,
    QString* error)
{
    QByteArray serialized;
    if (!serializeProject(path, problem, selectedCandidateIndex, serialized, error))
        return false;

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        setError(error, "StructureOptimization.Project.OpenFailed: " + file.errorString());
        return false;
    }
    if (file.write(serialized) < 0 || !file.commit()) {
        setError(error, "StructureOptimization.Project.SaveFailed: " + file.errorString());
        return false;
    }
    if (error != nullptr)
        error->clear();
    return true;
}

bool StructureOptimizationProjectAdapter::serializeProject(
    const QString& path, const StructureOptimizationProblem& problem, int selectedCandidateIndex,
    QByteArray& serialized, QString* error)
{
    if (hasInvalidContext(problem)) {
        setError(error, "StructureOptimization.Context.Invalid: complete RobotModelSpec is required.");
        return false;
    }

    // 使用便携副本进行 JSON 编码，绝不反向修改调用者正在编辑的绝对路径上下文。
    const StructureOptimizationProblem portable = portableProblem(path, problem);
    const QJsonDocument problemDocument = QJsonDocument::fromJson(
        QByteArray::fromStdString(StructureOptimizationJson::problemToJson(portable)));
    if (!problemDocument.isObject()) {
        setError(error, "StructureOptimization.Project.SerializeFailed");
        return false;
    }

    QJsonObject root;
    root["schemaVersion"] = ProjectSchemaVersion;
    root["type"] = "StructureOptimizationProject";
    root["problem"] = problemDocument.object();
    QJsonObject ui;
    ui["selectedCandidateIndex"] = selectedCandidateIndex;
    root["ui"] = ui;
    serialized = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (error != nullptr)
        error->clear();
    return true;
}

} // namespace rws
