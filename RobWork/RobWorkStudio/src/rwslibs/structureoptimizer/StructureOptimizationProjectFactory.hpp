#ifndef RWS_STRUCTUREOPTIMIZATION_STRUCTUREOPTIMIZATIONPROJECTFACTORY_HPP
#define RWS_STRUCTUREOPTIMIZATION_STRUCTUREOPTIMIZATIONPROJECTFACTORY_HPP

#include "StructureOptimizationTypes.hpp"

#include <QString>

#include <string>

namespace rws {

/**
 * @brief 结构优化项目初始化工厂类。
 * 
 * 负责根据传入的机器人模型规格 (RobotModelSpec) 创建并初始化一个全新的、
 * 结构完整且具备默认运行策略与设计变量的结构优化问题对象 (StructureOptimizationProblem)。
 * 
 * 设计哲学：
 * 1. 一键初始化：自动从输入模型中提取可变参数作为设计变量，填充默认的多目标权重与算法参数；
 * 2. 数据溯源绑定：支持绑定磁盘源模型路径 (sourceModelPath)，为后续的模型陈旧度检查 (RobotModelStalenessChecker) 建立审计跟踪链。
 */
class StructureOptimizationProjectFactory
{
  public:
    /**
     * @brief 从给定的机器人模型规格 (RobotModelSpec) 快照创建全新的结构优化项目。
     * 
     * 内部流程：
     *  1. 将 spec 赋值保存为 problem.context.modelSpec 作为优化的基线模型 (Baseline)；
     *  2. 自动扫描 spec 中的 transformJoints 和 drawables，生成推荐的优化设计变量 (variables)；
     *  3. 初始化默认的混合优化策略 (HybridStrategy)、运行参数 (candidateCount, eliteCount)；
     *  4. 填充默认的多目标打分权重配置 (ObjectiveTerms)；
     *  5. 若过程无异常，返回 true；若 spec 无效则写入 error 描述并返回 false。
     * 
     * @param spec 机器人模型规格结构体（包含关节变换与几何体尺寸）
     * @param problem [out] 用于接收创建并初始化完成的结构优化问题对象
     * @param error [out] 可选的输出错误描述信息指针
     * @return true  初始化项目成功；
     * @return false 模型规格为空或提取设计变量失败
     */
    static bool create(const RobotModelSpec& spec,
                       StructureOptimizationProblem& problem,
                       std::string* error = nullptr);

    /**
     * @brief 从机器人模型规格快照并绑定磁盘源文件路径 (sourceModelPath) 创建结构优化项目。
     * 
     * 重载版本的 create 函数。除了执行基础的项目初始化外，还会将 sourceModelPath 
     * 记录进 problem.context.sourceModelPath 中，从而使得后续的 RobotModelStalenessChecker 
     * 能够实时监控磁盘源文件与项目内快照的一致性状态。
     * 
     * @param spec 机器人模型规格结构体
     * @param sourceModelPath 当前模型对应的磁盘源文件路径 (如 "models/ur10.json")
     * @param problem [out] 用于接收初始化完成的结构优化问题对象
     * @param error [out] 可选的输出错误描述信息指针
     * @return true  初始化并绑定源文件成功；
     * @return false 初始化失败
     */
    static bool create(const RobotModelSpec& spec, const QString& sourceModelPath,
                       StructureOptimizationProblem& problem, std::string* error = nullptr);
};

} // namespace rws

#endif // RWS_STRUCTUREOPTIMIZATION_STRUCTUREOPTIMIZATIONPROJECTFACTORY_HPP