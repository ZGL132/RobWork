#ifndef RWS_STRUCTUREOPTIMIZATION_STRUCTUREDESIGNMUTATOR_HPP
#define RWS_STRUCTUREOPTIMIZATION_STRUCTUREDESIGNMUTATOR_HPP

#include "StructureOptimizationTypes.hpp"
#include <rwslibs/robotmodelbuilder/RobotModelSpec.hpp>

namespace rws {

/**
 * @brief 结构设计变量突变/应用结果结构体。
 *
 * 用于封装将一组设计变量数值应用到基线机器人模型规格（RobotModelSpec）后的输出结果，
 * 包含突变是否成功、突变生成的新模型规格以及过程中产生的警告或错误信息。
 */
struct StructureMutationResult {
    bool ok = false;                         //!< 突变应用是否成功 (true: 成功且合法; false: 参数越界/校验失败)
    RobotModelSpec spec;                     //!< 突变修改后的新机器人模型规格对象 (若 ok 为 false 则可能无效)
    std::vector<AnalysisWarning> warnings;   //!< 变异与同步过程中产生的警告及错误列表
};

/**
 * @brief 结构设计变异器类。
 *
 * 负责将优化算法或采样器生成的连续/离散设计变量数值向量映射回机器人模型数据结构（RobotModelSpec）中。
 * 核心逻辑包括：
 *  1. 变量数值与上下界的合法性校验（防 NaN、Inf 和超限）；
 *  2. 运动学建模模式互斥校验（禁止混用 DH 参数与 Transform 参数）；
 *  3. 修改对应的关节平移/旋转、DH 参数、基座高度、TCP 偏移量及连杆几何尺寸（半径/宽高）；
 *  4. 自动正反向同步 Transform 变换与 DH 参数视图；
 *  5. 重新计算并同步连杆几何体与碰撞模型网格。
 */
class StructureDesignMutator {
  public:
    /**
     * @brief 将给定的设计变量数值向量应用到基线机器人模型规格上，生成变异后的新模型。
     *
     * @param baseline 基线机器人模型规格（原始未变异的模型定义，保持只读）
     * @param variables 当前优化问题定义的设计变量配置列表（定义了变量类型、目标名称、边界等）
     * @param values 待施加的具体设计变量数值向量（数量和顺序必须与 variables 严格一致）
     * @return StructureMutationResult 突变执行结果，包含突变后的模型规格 spec 及状态标志 ok
     */
    static StructureMutationResult apply(
        const RobotModelSpec& baseline,
        const std::vector<StructureDesignVariable>& variables,
        const std::vector<double>& values);
};

} // namespace rws
#endif // RWS_STRUCTUREOPTIMIZATION_STRUCTUREDESIGNMUTATOR_HPP