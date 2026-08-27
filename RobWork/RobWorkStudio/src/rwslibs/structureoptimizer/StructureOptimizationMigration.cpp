#include "StructureOptimizationMigration.hpp"

#include "StructureOptimizationDocument.hpp"
#include "StructureOptimizationJson.hpp"
#include "StructureOptimizationUiLogic.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

namespace rws {

namespace {

void setError(std::string* error, const std::string& message)
{
    if (error != nullptr)
        *error = message;
}

bool isCurrentDocument(const QJsonObject& root)
{
    return root.value(QStringLiteral("type")).toString() ==
               QStringLiteral("StructureOptimizationDocument") &&
           root.value(QStringLiteral("schemaVersion")).toInt() ==
               StructureOptimizationDocument::SchemaVersion;
}

} // namespace

bool StructureOptimizationMigration::migrate(const std::string& json,
                                             StructureOptimizationMigrationResult& result,
                                             std::string* error)
{
    result = StructureOptimizationMigrationResult();
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(
        QByteArray::fromStdString(json), &parseError);
    if (!document.isObject()) {
        setError(error, "Structure optimization document is not a JSON object: " +
                          parseError.errorString().toStdString());
        return false;
    }

    const QJsonObject root = document.object();
    if (isCurrentDocument(root)) {
        result.source = StructureOptimizationMigrationSource::Current;
        if (!StructureOptimizationJson::currentEnvelopeFromJson(json, result.problem, error))
            return false;
        // M3 统一加载边界：Current 信封同样应用存量维度 kind 修正。
        StructureOptimizationUiLogic::migrateLegacyDrawableDimensionKinds(
            result.problem.variables);
        result.currentJson = StructureOptimizationJson::currentEnvelopeToJson(result.problem);
        result.dirty = false;
        if (error != nullptr)
            error->clear();
        return true;
    }

    // 旧格式只允许从输入读一次；保留完整副本供审计，但不让 runtime 再读取它。
    result.source = StructureOptimizationMigrationSource::Legacy;
    if (!root.contains(QStringLiteral("schemaVersion")) ||
        !root.contains(QStringLiteral("type"))) {
        setError(error, "Legacy structure optimization document is missing type or schemaVersion.");
        return false;
    }

    StructureOptimizationProblem problem;
    std::string parseMessage;
    if (!StructureOptimizationJson::problemFromJson(json, problem, &parseMessage)) {
        setError(error, "Legacy structure optimization document is invalid: " + parseMessage);
        return false;
    }

    for (StructureDesignVariable& variable : problem.variables) {
        // 空 targetName 表示旧项目没有稳定绑定；迁移后必须显式禁用，不能猜测目标。
        if (variable.targetName.empty() && variable.enabled) {
            variable.enabled = false;
            result.diagnostics.push_back("MIGRATION_UNBOUND_VARIABLE_DISABLED:" + variable.id);
        }
    }

    QJsonObject legacyExtension;
    legacyExtension.insert(QStringLiteral("document"), root);
    problem.extensions.insert(QStringLiteral("legacy"), legacyExtension);
    // M3 统一加载边界：旧格式迁移后同样应用存量维度 kind 修正。
    StructureOptimizationUiLogic::migrateLegacyDrawableDimensionKinds(
        problem.variables);
    result.problem = problem;
    result.currentJson = StructureOptimizationJson::currentEnvelopeToJson(result.problem);
    result.dirty = true;
    result.diagnostics.push_back("MIGRATION_LEGACY_TO_CURRENT");
    if (error != nullptr)
        error->clear();
    return true;
}

} // namespace rws
