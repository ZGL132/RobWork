#include "StructureCandidateExporter.hpp"

#include "StructureDesignMutator.hpp"
#include "CandidateModelFactory.hpp"

#include <rwslibs/robotmodelbuilder/RobotModelXmlWriter.hpp>

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace rws {

bool StructureCandidateExporter::exportModel(
    const StructureOptimizationProblem& problem,
    const StructureCandidateResult& candidate,
    const QString& targetDirectory,
    QStringList& errors)
{
    // ── 1. 应用设计变量突变 ────────────────────────────────────────────────
    const StructureMutationResult mutation = StructureDesignMutator::apply(
        problem.context.modelSpec,
        problem.variables,
        candidate.values);

    if (!mutation.ok) {
        errors << QString("DesignMutator::apply failed for candidate %1")
                      .arg(candidate.index);
        for (const auto& w : mutation.warnings)
            errors << QString::fromStdString("  Warning: [" + w.code + "] " + w.message);
        return false;
    }

    // ── 2. 确保目标目录存在 ────────────────────────────────────────────────
    QDir targetDir(targetDirectory);
    if (!targetDir.exists()) {
        if (!targetDir.mkpath(".")) {
            errors << QString("Failed to create target directory: %1").arg(targetDirectory);
            return false;
        }
    }

    // ── 3. 保存突变后的 RobotModelSpec 到目标目录 ──────────────────────────
    // 创建一个临时副本并设置保存目录为目标路径
    RobotModelSpec exportSpec = mutation.spec;
    exportSpec.saveDirectory = targetDir.absolutePath().toStdString();

    // 写入 XML 文件
    QStringList saveErrors;
    const bool writeOk = RobotModelXmlWriter::saveFiles(exportSpec, saveErrors);

    if (!writeOk) {
        errors << QString("RobotModelXmlWriter::saveFiles failed for candidate %1")
                      .arg(candidate.index);
        for (const auto& e : saveErrors)
            errors << "  " + e;
        return false;
    }

    // ── 4. 使用 CandidateModelFactory 构建模型并收集输出文件 ──────────────
    CandidateModelBuildRequest buildReq;
    buildReq.spec           = exportSpec;
    buildReq.deviceName     = problem.context.deviceName.empty()
                                  ? problem.context.robotName
                                  : problem.context.deviceName;
    buildReq.tcpFrame       = problem.context.tcpFrame;
    buildReq.checkCollision = problem.evaluation.checkCollision;

    CandidateModelFactory factory;
    CandidateModelBuildResult buildResult = factory.build(buildReq);

    if (!buildResult.ok) {
        errors << QString("CandidateModelFactory::build failed for candidate %1")
                      .arg(candidate.index);
        for (const auto& w : buildResult.warnings)
            errors << QString::fromStdString("  Warning: [" + w.code + "] " + w.message);
        return false;
    }

    // 如果工厂构建时创建了临时文件，将它们复制到目标目录
    if (buildResult.artifact.temporaryDirectory &&
        buildResult.artifact.temporaryDirectory->isValid())
    {
        const QString tmpPath = buildResult.artifact.temporaryDirectory->path();
        QDir tmpDir(tmpPath);
        const QStringList entries = tmpDir.entryList(QDir::Files | QDir::NoDotAndDotDot);
        for (const QString& entry : entries) {
            const QString srcPath = tmpDir.absoluteFilePath(entry);
            const QString dstPath = targetDir.absoluteFilePath(entry);
            // 如果目标已有同名文件则跳过
            if (QFile::exists(dstPath))
                continue;
            if (!QFile::copy(srcPath, dstPath))
                errors << QString("Failed to copy %1 to %2").arg(srcPath, dstPath);
        }
    }

    return errors.isEmpty();
}

} // namespace rws
