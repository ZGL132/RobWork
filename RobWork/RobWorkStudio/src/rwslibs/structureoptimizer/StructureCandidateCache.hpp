#ifndef RWS_STRUCTUREOPTIMIZATION_STRUCTURECANDIDATECACHE_HPP
#define RWS_STRUCTUREOPTIMIZATION_STRUCTURECANDIDATECACHE_HPP

#include "StructureOptimizationTypes.hpp"
#include <map>

namespace rws {

/**
 * @brief 候选解评估结果哈希缓存类。
 *
 * 该类负责在优化计算过程中，将已经评估过的设计变量数值组合及其对应的评估结果（StructureCandidateResult）缓存起来。
 * 当算法生成重复或在量化步长上极度接近的候选解参数时，通过哈希 Key 直接检索缓存，
 * 避免重复执行耗时的模型编译、运动学求解与碰撞检测，从而显著提升寻优效率。
 */
class StructureCandidateCache {
  public:
    /**
     * @brief 将候选解评估结果写入缓存。
     *
     * 根据优化问题定义、设计变量数值向量以及当前的评估阶段（Quick/Verified）生成唯一的复合 Key，
     * 并将计算好的评估结果存入内部的 std::map 容器中。
     *
     * @param problem 当前结构优化问题定义（只读，用于提取哈希所需的配置与阈值）
     * @param values 待缓存候选解的设计变量数值向量
     * @param stage 评估阶段（Quick 粗评 / Verified 精评）
     * @param result 评估完成的候选解结果对象
     */
    void put(const StructureOptimizationProblem& problem,
             const std::vector<double>& values,
             StructureEvaluationStage stage,
             const StructureCandidateResult& result);

    /**
     * @brief 在缓存中检索是否存在对应参数和评估阶段的计算结果。
     *
     * 根据输入参数生成 Key 并查询缓存。若命中缓存，将历史评估结果写回 out 参数 result，
     * 自动累加命中计数器 _hits，并返回 true；若未命中则返回 false。
     *
     * @param problem 当前结构优化问题定义
     * @param values 待查询的设计变量数值向量
     * @param stage 评估阶段（Quick / Verified）
     * @param result [out] 检索成功时用于接收缓存结果的引用参数
     * @return true 缓存命中（成功复用历史计算结果）
     * @return false 未命中（需要重新执行模型编译与运动学评估）
     */
    bool find(const StructureOptimizationProblem& problem,
              const std::vector<double>& values,
              StructureEvaluationStage stage,
              StructureCandidateResult& result) const;

    /**
     * @brief 清空当前缓存的全部内容并重置缓存命中计数器。
     */
    void clear();

    /**
     * @brief 获取累计的缓存命中次数。
     * @return std::size_t 缓存成功命中的总次数（可用于运行诊断与性能分析）
     */
    std::size_t hitCount() const { return _hits; }

    /**
     * @brief 获取当前缓存中保存的唯一候选解 Key-Value 项数量。
     * @return std::size_t 缓存条目总数
     */
    std::size_t size() const { return _cache.size(); }

  private:
    /**
     * @brief 候选解缓存复合键结构体。
     *
     * 用于在 std::map 中唯一标识一个候选解的评估上下文。
     * 只有当设计变量量子化网格值、机器人模型、任务环境、评估器以及所有判定阈值/采样配置完全相同时，
     * 生成的 Key 才会被认定为相同。
     */
    struct Key {
        std::vector<long long> quantizedValues; //!< 各启用设计变量按 step 量化截断后的长整型步长网格值数组
        std::size_t modelHash = 0;             //!< 基线机器人模型规格（RobotModelSpec）JSON 的哈希值
        std::size_t taskEnvironmentHash = 0;   //!< 任务点与设计上下文（RobotDesignContext）的哈希值
        std::size_t evaluatorHash = 0;         //!< 绑定的评估器 ID 与版本号字符串（evaluatorId@version）的哈希值
        std::size_t configurationHash = 0;     //!< 包含运动学阈值、粗/精采样配置、覆盖盒等所有影响评判结果的综合哈希值
        StructureEvaluationStage stage = StructureEvaluationStage::Quick; //!< 评估精度阶段（Quick 与 Verified 阶段互相隔离不混用）

        /**
         * @brief 重载 < 运算符，用于 std::map 内部的红黑树节点排序与严格弱序比较。
         * @param rhs 比较的另一个 Key 对象
         * @return true 当前 Key 小于 rhs
         * @return false 当前 Key 大于等于 rhs
         */
        bool operator<(const Key& rhs) const;
    };

    /**
     * @brief 根据输入问题、设计变量值以及评估阶段生成缓存 Key。
     *
     * @param problem 优化问题对象（从中提取阈值、采样参数等生成综合哈希）
     * @param values 原始连续变量数值向量
     * @param stage 当前评估阶段
     * @return Key 生成的内部复合键对象
     */
    Key makeKey(const StructureOptimizationProblem& problem,
                const std::vector<double>& values,
                StructureEvaluationStage stage) const;

    mutable std::size_t _hits = 0;                           //!< 缓存命中统计计数器（在 find 方法中可变递增）
    std::map<Key, StructureCandidateResult> _cache;          //!< 保存 Key 到候选解评估结果映射的标准红黑树容器
};

} // namespace rws
#endif // RWS_STRUCTUREOPTIMIZATION_STRUCTURECANDIDATECACHE_HPP