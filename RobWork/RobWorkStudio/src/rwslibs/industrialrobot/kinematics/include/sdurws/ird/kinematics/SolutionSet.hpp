/**
 * @file   SolutionSet.hpp
 * @brief  解集视图（§6.2）——稳定排序/筛选/统计/最差项的**唯一定义点**
 *         （NFR-MNT-04：UI/报告不各自排序）与四键稳定排序的共享实现。
 *
 * 设计依据：
 *   - units/kinematics.md §3.3（公共头布局表 SolutionSet.hpp 行——
 *     "IKinematicSolutionSet（去重/排序/统计视图）"，任务 T04）、§6.2
 *     （视图契约原文——sorted/filtered/statistics/worstBy 四操作）、
 *     §6.3（稳定排序四键＋浮点全序比较无容差＋referenceQ 显式化
 *     D-KIN-4）、§9.2（@pre"以已求解 SolutionSet 构造（值持有）；构造
 *     时完成一次稳定排序"）、§9.4（IKinematicSolutionSet——插件会话/
 *     归档读取期持有，UI 线程只读消费）
 *   - REQUIREMENTS KIN-02（解集稳定排序）、KIN-08（最差排序的规范来源）、
 *     NFR-COR-01/02（同输入重复排序结果逐位一致——V-04）
 *   - 任务契约 tasks/foundation/WP-15-T04.json acceptance 1/3
 *
 * 背景说明（为什么排序实现是自由函数）：求解器管线（Ik.cpp）与解集视图
 * （构造时排序）消费**同一个**四键排序语义——共享一份实现（sortSolutions）
 * 保证"求解产物次序"与"视图重排次序"永不漂移；视图本身不再暴露改序入口
 * （不可变操作——sorted() 返回构造时排好的序列引用）。
 *
 * 线程安全：KinematicSolutionSet 构造后不可变——并发只读安全（§9.4
 * "UI 线程只读消费"）；sortSolutions 纯函数可重入。确定性：浮点全序
 * 比较（无容差）＋稳定键兜底——同输入重复排序逐位一致（§6.3 原文）。
 */

#ifndef IRD_KINEMATICS_SOLUTIONSET_HPP
#define IRD_KINEMATICS_SOLUTIONSET_HPP

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include <sdurws/ird/kinematics/KinTypes.hpp>  // 解集值模型（T04 批次）

namespace sdurws::ird::kinematics {

// =====================================================================
// 四键稳定排序（§6.3 的唯一实现点——求解器管线与视图共用）
// =====================================================================

/**
 * @brief 按 §6.3 四键对解集就地稳定排序（KIN-02 明文键序）。
 *
 * 键序（自左向右字典序；浮点比较为**全序比较——无容差**，§6.3 原文）：
 *   1) minimumJointMargin 降序（裕量大的优先）；
 *   2) manipulability 降序（可操作度高优先）；
 *   3) ‖q−referenceQ‖₂ 升序（"当前距离"——referenceQ 是显式评估输入，
 *      D-KIN-4，禁止隐式读会话姿态）；
 *   4) sourceInitIndex 升序兜底（去重保留组内代表＝该组 sourceInitIndex
 *      最小者——总序、与线程/分片无关）。
 *
 * 已排序集合上幂等（稳定排序性质——V-04"两次输出逐位比对"的前提）。
 *
 * @param solutions   [in,out] 就地排序的解集（求解器产物或视图副本）
 * @param referenceQ  [in] 排序参考构型（rad／m；维度须与 q 一致——
 *                    调用方契约，不符时距离项无定义，本函数不校验）
 *
 * 纯函数（除就地重排入参）；线程安全（无共享状态）；确定性（同输入同
 * 次序——NFR-COR-01/02）。
 */
void sortSolutions(std::vector<KinematicSolution>& solutions,
                   const std::vector<double>& referenceQ);

/**
 * @brief 解集去重（§5.3/§6.1 的唯一实现点——求解器管线与视图面共享）。
 *
 * 语义（§6.1/I-KIN-3）：关节空间**逐轴阈值**成对容差比较（|Δq_i| ≤
 * thresholdPerAxis 全轴成立＝同一构型）；continuous 关节按工作范围值
 * **直接比较、无跨周取模**（附录 D 第 3 项/C1）；去重对象是构型而非
 * 位姿——同位姿异构型均保留；ConfigurationSignature 仅作记录键、不
 * 参与去重判定。保留规则：按输入序先到先留（求解器以初值序传入——
 * 组内代表＝sourceInitIndex 最小者）。
 *
 * @param solutions        [in] 待去重解集（已通过硬过滤；输入序即保留
 *                         优先序）
 * @param thresholdPerAxis [in] 逐轴去重阈值（rad|m；>0——调用方契约）
 * @return 去重后的新解集（保持输入序的子序列；调用方所有）
 *
 * 纯函数；线程安全；确定性（同输入同结果——比较为逐轴标量比较，
 * 无浮点聚合）。
 */
std::vector<KinematicSolution> deduplicateSolutions(
    const std::vector<KinematicSolution>& solutions, double thresholdPerAxis);

// =====================================================================
// 视图词表：SolutionRef／SolutionSetView／SolutionPredicate／WorstMetric
// =====================================================================

/**
 * @brief 解的稳定指称（视图内下标——sorted() 序上的位置；值语义）。
 */
struct SolutionRef {
    /// sorted() 序上的下标（计数，无量纲）。
    std::size_t solutionIndex = 0;

    bool operator==(const SolutionRef& o) const noexcept
    {
        return solutionIndex == o.solutionIndex;
    }
    bool operator!=(const SolutionRef& o) const noexcept { return !(*this == o); }
};

/// 解集视图（§6.2 filtered 返回形态——不可变操作返回新视图，值语义）。
using SolutionSetView = std::vector<KinematicSolution>;

/// 筛选谓词（§6.2"筛选谓词由调用方给出但比较实现在此"——谓词只表达
/// 业务条件，排序/统计一致性由视图保证）。
using SolutionPredicate = std::function<bool(const KinematicSolution&)>;

/**
 * @brief 最差项查询的度量词表（§6.2 worstBy——KIN-08 最差排序的规范
 *        来源三值）。
 */
enum class WorstMetric : std::uint8_t {
    /// 裕量最小者最差（minimumJointMargin 升序取首——无量纲比）。
    MinimumJointMargin,
    /// 条件数最大者最差（conditionNumber 降序取首——无量纲；+∞ 参与全序）。
    ConditionNumber,
    /// 残差最大者最差（positionResidual 降序取首——m；同值按 sorted 序
    /// 取先者——稳定语义，登记随卡 §14.6 v0.4）。
    PositionResidual,
};

// =====================================================================
// IKinematicSolutionSet——解集只读视图接口（§6.2 原文契约）
// =====================================================================

/**
 * @brief 解集只读视图（§6.2——稳定排序/筛选/统计的唯一定义点）。
 *
 * 生命周期与线程（§9.4）：插件会话/归档读取期持有；UI 线程只读消费。
 * 实现构造后不可变（值持有解集副本）——并发只读安全；本接口不提供任何
 * 改序/改值入口（不可变视图——PA-2 精神在消费面的落点）。
 */
class IKinematicSolutionSet {
public:
    virtual ~IKinematicSolutionSet() = default;

    /**
     * @brief KIN-02 稳定排序结果（构造时完成一次排序——§9.2 @pre）。
     * @return 构造时排好的解集序列引用（视图存活期内稳定；已排序集合上
     *         幂等——重复获取同一次序）。
     */
    virtual const SolutionSetView& sorted() const = 0;

    /**
     * @brief 按谓词筛选（可用解/含碰撞诊断解——谓词语义由调用方表达）。
     * @param p [in] 筛选谓词（逐解调用；空谓词行为＝全保留）
     * @return 通过谓词的解的新视图（保持 sorted() 序——子序列；调用方所有）
     */
    virtual SolutionSetView filtered(const SolutionPredicate& p) const = 0;

    /**
     * @brief 统计（§6.1 四计数——构造值持有的 statistics 字段）。
     */
    virtual IkSolutionSetStatistics statistics() const = 0;

    /**
     * @brief 最差项查询（KIN-08 规范来源：裕量最小/条件数最大/残差最大）。
     * @param m [in] 度量词表（三值）
     * @return 最差解的指称（sorted() 序上**第一个**达到最差值的解——稳定
     *         语义；空集返回 nullopt）
     */
    virtual std::optional<SolutionRef> worstBy(WorstMetric m) const = 0;
};

// =====================================================================
// KinematicSolutionSet——值持有实现（§9.2"@pre 以已求解 SolutionSet 构造"）
// =====================================================================

/**
 * @brief IKinematicSolutionSet 的值持有实现（构造时完成一次稳定排序）。
 *
 * 生命周期：值语义（拷贝/移动均安全——成员全为值）；构造入参为
 * IkSolutionSet 按值持有副本。线程安全：构造后不可变——并发只读安全。
 * 确定性：排序经 sortSolutions 唯一实现点（同输入同次序）。
 */
class KinematicSolutionSet final : public IKinematicSolutionSet {
public:
    /**
     * @brief 以已求解解集构造（值持有；构造时完成一次稳定排序）。
     *
     * @param set [in] 求解产物（IkSolver::solve 的 solutionSet；调用方
     *                 值传入——本视图持副本，不回写求解器）
     */
    explicit KinematicSolutionSet(IkSolutionSet set);

    const SolutionSetView& sorted() const override;
    SolutionSetView filtered(const SolutionPredicate& p) const override;
    IkSolutionSetStatistics statistics() const override;
    std::optional<SolutionRef> worstBy(WorstMetric m) const override;

private:
    IkSolutionSetStatistics m_statistics;  ///< 构造值持有的统计面
    SolutionSetView m_sorted;              ///< 构造时完成的一次稳定排序结果
};

}  // namespace sdurws::ird::kinematics

#endif  // IRD_KINEMATICS_SOLUTIONSET_HPP
