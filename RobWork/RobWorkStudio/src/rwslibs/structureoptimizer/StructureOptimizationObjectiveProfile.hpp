#ifndef RWS_STRUCTUREOPTIMIZATION_STRUCTUREOPTIMIZATIONOBJECTIVEPROFILE_HPP
#define RWS_STRUCTUREOPTIMIZATION_STRUCTUREOPTIMIZATIONOBJECTIVEPROFILE_HPP

#include "StructureOptimizationTypes.hpp"

namespace rws {

/**
 * @brief 结构优化多目标配置 Profile 工具类。
 * 
 * 作用：处理优化算法在多目标打分时，新版动态目标列表 (std::vector<ObjectiveTerm>) 
 * 与旧版固定四项权重结构体 (StructureOptimizationWeights) 之间的向下兼容与自动转换。
 * 
 * 保证了优化引擎在升级到动态目标架构后，依然能无缝运行旧版保存的优化项目文件。
 */
class StructureOptimizationObjectiveProfile
{
public:
    /**
     * @brief 将旧版的四项权重结构体转换为新版的 ObjectiveTerm 列表。
     * 
     * 将旧版的四个固定权重字段（可达性、可操作度、紧凑度、工程偏好）打包转换为 4 个标准 ObjectiveTerm：
     *  1. "kinematics.reachability.weighted" (权重 = weights.reachability)
     *  2. "kinematics.manipulability.p10"   (权重 = weights.manipulability)
     *  3. "geometry.compactness"            (权重 = weights.compactness)
     *  4. "structure.preference"            (权重 = weights.engineeringPreference)
     * 
     * @param weights 旧版结构优化权重结构体
     * @return std::vector<ObjectiveTerm> 转换后的新版目标项向量
     */
    static std::vector<ObjectiveTerm> legacyObjectives(
        const StructureOptimizationWeights& weights);

    /**
     * @brief 智能解析并获取当前优化问题中实际生效的目标项列表。
     * 
     * 策略模式与兼容逻辑：
     *  - 若 problem.objectives 已包含用户配置的新版目标项，则直接返回 problem.objectives；
     *  - 若 problem.objectives 为空，则自动调用 legacyObjectives(problem.weights) 
     *    将旧版权重包装转换为标准目标列表后返回，确保打分器 (StructureObjectiveScorer) 永远拿到统一的新版接口。
     * 
     * @param problem 当前结构优化问题定义只读引用
     * @return const std::vector<ObjectiveTerm>& 实际生效的目标项列表引用
     */
    static const std::vector<ObjectiveTerm>& effectiveObjectives(
        const StructureOptimizationProblem& problem);

    /**
     * @brief 判断给定的目标项列表是否为旧版导出的四项目标 Profile 架构。
     * 
     * 检查 objectives 列表的长度及项 ID 是否完全符合旧版 4 项标量配置，
     * 供 UI 界面决定是展示简化的 4 项权重调节滑动条，还是展示高级的动态目标列表。
     * 
     * @param objectives 待判定的目标项列表
     * @return true  属于旧版 Profile 结构；
     * @return false 包含自定义或扩展的目标项，属于新版 Profile 结构
     */
    static bool isLegacyProfile(const std::vector<ObjectiveTerm>& objectives);
};

} // namespace rws

#endif // RWS_STRUCTUREOPTIMIZATION_STRUCTUREOPTIMIZATIONOBJECTIVEPROFILE_HPP