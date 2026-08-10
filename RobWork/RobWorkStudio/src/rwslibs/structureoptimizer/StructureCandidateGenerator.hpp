#ifndef RWS_STRUCTUREOPTIMIZATION_STRUCTURECANDIDATEGENERATOR_HPP
#define RWS_STRUCTUREOPTIMIZATION_STRUCTURECANDIDATEGENERATOR_HPP

#include "StructureOptimizationTypes.hpp"

namespace rws {

/**
 * @brief 结构优化候选解采样生成器类。
 *
 * 该类提供纯静态方法，负责在由设计变量（variables）构成的多维空间中，
 * 根据指定的搜索策略（随机、拉丁超立方、网格）生成候选解样本池（Candidate Pool），
 * 并提供变量搜索步长量化截断功能。
 */
class StructureCandidateGenerator {
  public:
    /**
     * @brief 均匀随机采样生成候选解列表。
     * 
     * 在每个已启用的设计变量的上下界 [minimum, maximum] 范围内进行独立均匀随机抽样，
     * 并对生成的连续数值按变量设定的搜索步长（step）进行量化截断。
     *
     * @param variables 设计变量配置列表
     * @param count 期望生成的候选解数量
     * @param seed 随机数发生器种子（用于保证实验结果可复现）
     * @return std::vector<std::vector<double>> 生成的候选解矩阵（二维数组），
     *         其中外层向量大小为 count，内层向量大小与 variables 保持一致。
     */
    static std::vector<std::vector<double>> randomUniform(
        const std::vector<StructureDesignVariable>& variables,
        int count, unsigned int seed);

    /**
     * @brief 拉丁超立方采样（Latin Hypercube Sampling, LHS）生成候选解列表。
     * 
     * 一种高维空间均匀分层抽样策略。将每个设计变量的取值范围等分为 count 个分层区间，
     * 在每个区间内进行随机抽样，随后对各变量的分层样本顺序进行随机打乱（Permute），
     * 能够有效消除变量间随机相关性，保证高维空间下样本点的均匀分布。
     *
     * @param variables 设计变量配置列表
     * @param count 期望生成的候选解数量（即分层数量）
     * @param seed 随机数发生器种子
     * @return std::vector<std::vector<double>> 高维分布均匀的候选解矩阵
     */
    static std::vector<std::vector<double>> latinHypercube(
        const std::vector<StructureDesignVariable>& variables,
        int count, unsigned int seed);

    /**
     * @brief 网格遍历采样（Grid Sweep）生成候选解列表。
     * 
     * 按“里程表”累加的方式对所有已启用的设计变量在其上下界范围内进行全网格遍历组合。
     * 当变量数量较多时组合数会剧烈增加，可以通过 maximumCount 限制生成的最大候选解数量。
     *
     * @param variables 设计变量配置列表
     * @param stepsPerVariable 每个设计变量在网格遍历时的划分步数（网格密度）
     * @param maximumCount 允许生成的最大候选解数量上限
     * @return std::vector<std::vector<double>> 网格遍历生成的候选解矩阵
     */
    static std::vector<std::vector<double>> grid(
        const std::vector<StructureDesignVariable>& variables,
        int stepsPerVariable, int maximumCount);

    /**
     * @brief 对连续变量值按照设计变量设定的搜索步长（step）进行量化截断。
     * 
     * 量化公式：q = minimum + round((value - minimum) / step) * step
     * 确保生成的变量数值符合工程实际加工或安装的最小步长限制，并将其钳位在 [minimum, maximum] 区间内。
     *
     * @param value 待量化的连续采样数值
     * @param variable 对应的设计变量配置结构体（包含 minimum, maximum, step 等）
     * @return double 量化及边界截断后的合法变量数值
     */
    static double quantize(double value, const StructureDesignVariable& variable);

  private:
    /**
     * @brief 内部线性同余伪随机数发生器（LCG）。
     * 
     * 基于给定的无符号整数状态生成 [0.0, 1.0) 范围内的 double 浮点数，
     * 并同步更新状态值，用于保证跨平台伪随机序列的一致性。
     *
     * @param state [in, out] 伪随机数发生器的当前状态（种子）
     * @return double 生成的 [0.0, 1.0) 均匀分布随机浮点数
     */
    static double randomDouble(unsigned int& state);
};

} // namespace rws
#endif // RWS_STRUCTUREOPTIMIZATION_STRUCTURECANDIDATEGENERATOR_HPP