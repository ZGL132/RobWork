#ifndef RWS_STRUCTUREOPTIMIZATION_STRUCTUREOPTIMIZATIONPROJECTADAPTER_HPP
#define RWS_STRUCTUREOPTIMIZATION_STRUCTUREOPTIMIZATIONPROJECTADAPTER_HPP

#include "StructureOptimizationTypes.hpp"

#include <QByteArray>
#include <QString>

namespace rws {

class StructureOptimizationProjectAdapter
{
public:
    static bool loadProject(const QString& path, StructureOptimizationProblem& out,
                            int* selectedCandidateIndex = nullptr,
                            QString* error = nullptr,
                            const QString& projectRoot = QString());
    static bool saveProject(const QString& path, const StructureOptimizationProblem& problem,
                            int selectedCandidateIndex = -1, QString* error = nullptr);

    /**
     * @brief 生成与 saveProject 完全一致的规范 JSON，用于项目 Provider 的脏状态快照。
     *
     * 单独公开序列化而不是让 Widget 复制拼装规则，可保证模型输出目录的相对路径转换、
     * schemaVersion 与 UI 元数据在“比较”和“实际写盘”两条路径中严格一致。
     */
    static bool serializeProject(const QString& path, const StructureOptimizationProblem& problem,
                                 int selectedCandidateIndex, QByteArray& serialized,
                                 QString* error = nullptr);
};

} // namespace rws

#endif // RWS_STRUCTUREOPTIMIZATION_STRUCTUREOPTIMIZATIONPROJECTADAPTER_HPP
