#ifndef RWS_STRUCTUREOPTIMIZATION_STRUCTUREOPTIMIZATIONPROJECTADAPTER_HPP
#define RWS_STRUCTUREOPTIMIZATION_STRUCTUREOPTIMIZATIONPROJECTADAPTER_HPP

#include "StructureOptimizationTypes.hpp"

#include <QByteArray>
#include <QString>

namespace rws {

struct KinematicImportRequest;

/**
 * @brief 结构优化项目磁盘文件持久化适配器类。
 *
 * 负责结构优化项目工程文件 (通常为 .sop.json 格式) 的磁盘读取 (load)、写入 (save)
 * 以及标准 JSON 字节流序列化 (serialize)。
 *
 * 设计哲学：
 * 1. 强一致性路径：序列化逻辑集中收口于 serializeProject()，确保 UI 脏状态比较 (Dirty-State Check)
 *    与实际写盘使用相同的路径转换与 Schema 版本；
 * 2. 移植可携带性：保存时自动将绝对路径转换为相对于工程文件的相对路径，加载时依据 projectRoot 重新解析为绝对路径。
 */
class StructureOptimizationProjectAdapter
{
public:
    /**
     * @brief 从指定磁盘路径加载结构优化工程文件。
     *
     * 内部流程：
     *  1. 读取磁盘文件内容为 QJsonDocument；
     *  2. 校验文件 schemaVersion 版本号；
     *  3. 反序列化变量、任务点、约束、运行策略及历史候选解结果到 out 结构体中；
     *  4. 恢复用户上次在界面选中的候选解索引 selectedCandidateIndex。
     *
     * @param path 工程 JSON 文件的绝对路径
     * @param out [out] 用于接收反序列化还原后的结构优化问题定义结构体
     * @param selectedCandidateIndex [out] 可选输出参数，接收上次选中的候选解索引 (缺省 -1)
     * @param error [out] 可选输出参数，接收加载失败时的错误描述信息
     * @return true  加载并解析成功；
     * @return false 文件不存在、格式非法或 Schema 版本不匹配
     */
    static bool loadProject(const QString& path, StructureOptimizationProblem& out,
                            int* selectedCandidateIndex = nullptr,
                            QString* error = nullptr);

    /**
     * @brief 重载版本的 loadProject，显式指定工程根目录以解析外部 CAD/模型资源相对路径。
     *
     * 当工程架构采用托管工程 (Managed Project) 时，使用指定的 projectRoot 作为相对路径解析基准，
     * 确保模型输出目录、CAD 几何及快照资源的绝对路径能够被一致、无缝地还原。
     *
     * @param path 工程 JSON 文件的绝对路径
     * @param out [out] 接收还原后的结构优化问题对象
     * @param selectedCandidateIndex [out] 接收上次选中的候选解索引
     * @param error [out] 接收错误描述信息
     * @param projectRoot 显式指定的托管工程全局根目录路径
     * @return true 加载成功；false 加载失败
     */
    static bool loadProject(const QString& path, StructureOptimizationProblem& out,
                            int* selectedCandidateIndex, QString* error,
                            const QString& projectRoot);

    /** Loads a project and refreshes the optional canonical shadow from an explicit source. */
    static bool loadProject(const QString& path, const KinematicImportRequest& importRequest,
                            StructureOptimizationProblem& out,
                            int* selectedCandidateIndex = nullptr,
                            QString* error = nullptr);

    /**
     * @brief 将当前结构优化问题及界面选择状态保存写盘为工程文件。
     *
     * 内部会调用 serializeProject() 生成规范 JSON 文本，并安全写回磁盘文件（包含临时文件替换机制以防落盘中断破坏原文件）。
     *
     * @param path 目标工程 JSON 文件的保存路径
     * @param problem 待保存的结构优化问题定义对象 (只读)
     * @param selectedCandidateIndex 当前在界面表格中选中的候选解索引 (缺省 -1)
     * @param error [out] 可选输出参数，接收保存失败时的错误描述信息
     * @return true  保存成功；
     * @return false 磁盘写权限不足或序列化失败
     */
    static bool saveProject(const QString& path, const StructureOptimizationProblem& problem,
                            int selectedCandidateIndex = -1, QString* error = nullptr);

    /**
     * @brief 生成与 saveProject 完全一致的规范 JSON 字节流，用于项目 Provider 的脏状态快照比较。
     *
     * 核心设计哲学：
     * 单独公开序列化接口，而不是让 UI Widget 复制拼装逻辑！
     * 能够保证模型输出目录的相对路径转换、schemaVersion 版本标头与 UI 元数据在
     * “内存比较 (Is Dirty?)” 和 “实际写盘 (Save)” 两条路径中严格完全一致，彻底杜绝假脏状态误报。
     *
     * @param path 工程文件的磁盘基准路径 (用于计算相对路径转换)
     * @param problem 待序列化的结构优化问题对象
     * @param selectedCandidateIndex 当前选中的候选解索引
     * @param serialized [out] 接收生成的规范 JSON 字节流 (QByteArray)
     * @param error [out] 可选输出参数，接收序列化失败描述
     * @return true  序列化成功；
     * @return false 结构数据损坏导致序列化失败
     */
    static bool serializeProject(const QString& path, const StructureOptimizationProblem& problem,
                                 int selectedCandidateIndex, QByteArray& serialized,
                                 QString* error = nullptr);
};

} // namespace rws

#endif // RWS_STRUCTUREOPTIMIZATION_STRUCTUREOPTIMIZATIONPROJECTADAPTER_HPP
