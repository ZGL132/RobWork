#ifndef RWS_STRUCTUREOPTIMIZATION_STRUCTUREWORKSPACECOVERAGE_HPP
#define RWS_STRUCTUREOPTIMIZATION_STRUCTUREWORKSPACECOVERAGE_HPP

#include "StructureOptimizationTypes.hpp"

#include <cstddef>
#include <vector>

namespace rws {

/**
 * @brief 工作空间包围盒覆盖率分析结果结构体。
 * 
 * 用于封装对特定空间包围盒（WorkspaceCoverageBox）执行采样覆盖分析后的最终统计指标。
 */
struct StructureWorkspaceCoverageResult
{
    double coverage = 0.0;            //!< 体素网格覆盖率，数值范围为 [0.0, 1.0] (即 0% ~ 100%)
    std::size_t occupiedCellCount = 0; //!< 被无碰撞且可达的 TCP 采样点成功占用的体素网格（Cell）数量
    std::size_t totalCellCount = 0;    //!< 包围盒内按指定划分粒度切分出的体素网格（Cell）总数量 (如 10x10x10 = 1000)
};

/**
 * @brief 工作空间覆盖率计算与分析工具类。
 * 
 * 该类提供静态分析方法，从运动学分析器生成的无碰撞可用工作空间采样点（WorkspaceSample）中，
 * 计算末端 TCP 扫掠点落入指定空间包围盒（WorkspaceCoverageBox）的网格占用情况及覆盖率。
 */
class StructureWorkspaceCoverage
{
  public:
    /**
     * @brief 对工作空间采样点进行体素剖分分析，返回完整的覆盖率统计结果。
     * 
     * 算法逻辑：
     *  1. 校验包围盒边界与 cells 划分参数的合法性；
     *  2. 遍历所有采样点，筛选出无碰撞且运动学状态正常的有效样本；
     *  3. 将有效样本的坐标映射到包围盒的 3D 网格单元中，计算落入的体素 Cell ID；
     *  4. 利用集合去重统计被占用的网格总数 occupiedCellCount；
     *  5. 计算并返回包含覆盖率比例的完整 StructureWorkspaceCoverageResult 对象。
     * 
     * @param samples 正运动学采样获得的工作空间样本点集合（已包含 TCP 坐标、碰撞状态等）
     * @param box 预定义的空间包围盒规格（定义了 3D 边界坐标、三轴划分网格数等）
     * @return StructureWorkspaceCoverageResult 包含覆盖率比例、被占用网格数与总网格数的结构体
     */
    static StructureWorkspaceCoverageResult analyze(
        const std::vector<WorkspaceSample>& samples,
        const WorkspaceCoverageBox& box);

    /**
     * @brief 便捷计算函数，仅直接返回覆盖率的浮点数值 [0.0, 1.0]。
     * 
     * 内部直接调用 analyze(samples, box).coverage 实现。
     * 
     * @param samples 正运动学采样获得的工作空间样本点集合
     * @param box 预定义的空间包围盒规格
     * @return double 空间网格覆盖率比例 [0.0, 1.0]
     */
    static double calculate(const std::vector<WorkspaceSample>& samples,
                            const WorkspaceCoverageBox& box);
};

} // namespace rws

#endif // RWS_STRUCTUREOPTIMIZATION_STRUCTUREWORKSPACECOVERAGE_HPP