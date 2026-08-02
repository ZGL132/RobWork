#include "FrozenRequirementProjectImportService.hpp"

#include "EngineeringRequirementArtifactAdapter.hpp"
#include "StructureOptimizationProjectFactory.hpp"

#include <rwslibs/engineeringrequirements/RequirementFreezer.hpp>
#include <rwslibs/engineeringrequirements/RequirementSetJson.hpp>
#include <rwslibs/robotmodelbuilder/RobotModelProjectPaths.hpp>
#include <rwslibs/robotmodelbuilder/RobotModelSpecJson.hpp>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>

#include <algorithm>

namespace rws {
namespace {

bool setError(std::string* error, const std::string& message)
{
    if (error != nullptr)
        *error = message;
    return false;
}

QString resolveModelPath(const QString& requirementPath, const std::string& bindingPath)
{
    // 冻结工件中的 sourcePath 由需求插件记录。绝对路径保持原样；相对路径严格相对于
    // 需求文件，而非当前进程工作目录，从而避免从不同插件或命令行启动程序时解析到不同模型。
    const QString modelPath = QString::fromStdString(bindingPath).trimmed();
    if (QFileInfo(modelPath).isAbsolute())
        return QDir::cleanPath(modelPath);
    return QDir(QFileInfo(requirementPath).absolutePath()).absoluteFilePath(modelPath);
}

QString commonDirectory(const QString& first, const QString& second)
{
    const QString left = QDir::fromNativeSeparators(QFileInfo(first).absoluteFilePath());
    const QString right = QDir::fromNativeSeparators(QFileInfo(second).absoluteFilePath());
    const QStringList leftParts = left.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    const QStringList rightParts = right.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    QStringList commonParts;
    const int count = std::min(leftParts.size(), rightParts.size());
    for (int index = 0; index < count; ++index) {
        if (leftParts[index].compare(rightParts[index], Qt::CaseInsensitive) != 0)
            break;
        commonParts.push_back(leftParts[index]);
    }
    if (commonParts.size() <= 1)
        return QFileInfo(first).absolutePath();
    QString common = commonParts.join(QLatin1Char('/'));
    if (left.startsWith(QLatin1Char('/')))
        common.prepend(QLatin1Char('/'));
    return QDir::cleanPath(common);
}

bool hasRelativeGeometryPath(const RobotModelSpec& model)
{
    for (const DrawableSpec& drawable : model.drawables) {
        const QString path = QString::fromStdString(drawable.filePath).trimmed();
        if (!path.isEmpty() && QFileInfo(path).isRelative())
            return true;
    }
    for (const CollisionModelSpec& collision : model.collisionModels) {
        const QString path = QString::fromStdString(collision.filePath).trimmed();
        if (!path.isEmpty() && QFileInfo(path).isRelative())
            return true;
    }
    for (const SceneGeometrySpec& geometry : model.sceneGeometries) {
        const QString path = QString::fromStdString(geometry.file).trimmed();
        if (!path.isEmpty() && QFileInfo(path).isRelative())
            return true;
    }
    return false;
}

} // namespace

bool FrozenRequirementProjectImportService::createProblem(
    const QString& requirementPath,
    const rw::models::WorkCell& workcell,
    const rw::kinematics::State& state,
    StructureOptimizationProblem& problem,
    FrozenRequirementValidationResult* validation,
    std::string* error)
{
    return createProblem(requirementPath, workcell, state, problem, validation, error, QString());
}

bool FrozenRequirementProjectImportService::createProblem(
    const QString& requirementPath,
    const rw::models::WorkCell& workcell,
    const rw::kinematics::State& state,
    StructureOptimizationProblem& problem,
    FrozenRequirementValidationResult* validation,
    std::string* error,
    const QString& artifactBaseDirectory)
{
    const QFileInfo requirementInfo(requirementPath);
    if (requirementPath.trimmed().isEmpty() || !requirementInfo.isFile())
        return setError(error, "Engineering requirement file does not exist.");

    QFile requirementFile(requirementInfo.absoluteFilePath());
    if (!requirementFile.open(QIODevice::ReadOnly | QIODevice::Text))
        return setError(error, "Engineering requirement file cannot be opened: " +
                              requirementFile.errorString().toStdString());

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(requirementFile.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return setError(error, "Engineering requirement JSON is invalid: " +
                              parseError.errorString().toStdString());

    // 同时解析编辑态 RequirementSet，防止手工拼接的顶层 JSON 绕开基本 schema 校验；真正
    // 交给下游的仍然只会是 frozenArtifact，不会读取这份可编辑数据来生成任务。
    RequirementSet editableRequirements;
    std::string parseMessage;
    if (!RequirementSetJson::fromObject(document.object(), editableRequirements, &parseMessage))
        return setError(error, "Engineering requirement set is invalid: " + parseMessage);

    const QJsonValue artifactValue = document.object().value("frozenArtifact");
    if (!artifactValue.isObject())
        return setError(error, "Engineering requirement file has no valid frozenArtifact.");

    FrozenRequirementArtifact artifact;
    if (!FrozenRequirementArtifactJson::fromObject(artifactValue.toObject(), artifact, &parseMessage))
        return setError(error, "Frozen engineering requirement artifact is invalid: " + parseMessage);
    if (!artifact.compiled.frozen)
        return setError(error, "Frozen engineering requirement artifact is not marked frozen.");

    // 顶层绑定和冻结绑定必须描述同一模型。导入时先阻止两者不一致，才能避免用户编辑态
    // 文件已改绑模型、但 artifact 仍指向旧模型而产生难以追溯的优化结论。
    if (!editableRequirements.modelBinding.robotModelFingerprint.empty() &&
        editableRequirements.modelBinding.robotModelFingerprint !=
            artifact.modelBinding.robotModelFingerprint)
        return setError(error, "Requirement set model binding differs from frozenArtifact.");

    const QString modelPath = resolveModelPath(requirementInfo.absoluteFilePath(),
                                               artifact.modelBinding.sourcePath);
    if (artifact.modelBinding.sourcePath.empty() || !QFileInfo(modelPath).isFile())
        return setError(error, "Frozen engineering requirement model snapshot does not exist.");

    const QString resolvedArtifactBaseDirectory = artifactBaseDirectory.trimmed().isEmpty()
        ? commonDirectory(requirementInfo.absolutePath(), QFileInfo(modelPath).absolutePath())
        : QFileInfo(artifactBaseDirectory).absoluteFilePath();
    FrozenRequirementValidationResult scenarioValidation;
    if (!RequirementFreezer::validateScenario(
            artifact, workcell, state, &scenarioValidation, &parseMessage,
            resolvedArtifactBaseDirectory.toStdString()))
        return setError(error, parseMessage);

    QFile modelFile(modelPath);
    if (!modelFile.open(QIODevice::ReadOnly | QIODevice::Text))
        return setError(error, "Robot model snapshot cannot be opened: " +
                              modelFile.errorString().toStdString());

    RobotModelSpec storedModel;
    if (!RobotModelSpecJson::fromJson(modelFile.readAll().toStdString(), storedModel, &parseMessage))
        return setError(error, "Robot model snapshot is invalid: " + parseMessage);

    RobotModelSpec model = storedModel;
    if (!artifactBaseDirectory.trimmed().isEmpty() && hasRelativeGeometryPath(storedModel)) {
        QString pathError;
        if (!RobotModelProjectPaths::resolveManaged(
                storedModel, resolvedArtifactBaseDirectory, model, &pathError)) {
            return setError(error, "Managed robot model paths are invalid: " +
                                      pathError.toStdString());
        }
    }

    StructureOptimizationProblem created;
    if (!StructureOptimizationProjectFactory::create(model, modelPath, created, &parseMessage))
        return setError(error, "Structure optimization project creation failed: " + parseMessage);
    if (!EngineeringRequirementArtifactAdapter::apply(artifact, created, &parseMessage))
        return setError(error, "Frozen engineering requirements cannot be applied: " + parseMessage);
    created.scenarioSnapshot.baseDirectory = resolvedArtifactBaseDirectory.toStdString();

    problem = created;
    if (validation != nullptr)
        *validation = scenarioValidation;
    if (error != nullptr)
        error->clear();
    return true;
}

} // namespace rws
