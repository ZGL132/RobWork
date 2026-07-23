#ifndef RWS_STRUCTUREOPTIMIZATION_STRUCTURECANDIDATEEXPORTER_HPP
#define RWS_STRUCTUREOPTIMIZATION_STRUCTURECANDIDATEEXPORTER_HPP

#include "StructureOptimizationTypes.hpp"
#include <QString>
#include <QStringList>

namespace rws {

//! @brief 候选模型导出器。
//!
//! 将指定的候选解转换为一组 WorkCell / Device / Collision 文件，
//! 并复制到目标目录。
class StructureCandidateExporter {
  public:
    //! @brief 导出指定候选解的完整机器人模型至目标目录。
    //! @param problem  优化问题 (包含 RobotDesignContext / 基线模型规格)。
    //! @param candidate  待导出的候选解 (其 values 将被 DesignMutator 应用)。
    //! @param targetDirectory  导出目标路径。
    //! @param errors  输出参数，收集导出过程中的错误消息。
    //! @return true 表示成功导出全部文件。
    static bool exportModel(const StructureOptimizationProblem& problem,
                            const StructureCandidateResult& candidate,
                            const QString& targetDirectory,
                            QStringList& errors);
};

} // namespace rws
#endif // RWS_STRUCTUREOPTIMIZATION_STRUCTURECANDIDATEEXPORTER_HPP
