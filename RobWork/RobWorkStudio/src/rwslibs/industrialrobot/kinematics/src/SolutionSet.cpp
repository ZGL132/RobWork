/**
 * @file   SolutionSet.cpp
 * @brief  解集视图实现——§6.3 四键稳定排序的唯一实现与
 *         KinematicSolutionSet 的构造排序/筛选/统计/最差项。
 *
 * 设计依据（契约面见 SolutionSet.hpp 文件头）：units/kinematics.md §6.2/
 * §6.3/§9.2/§9.4；任务契约 WP-15-T04 acceptance 1/3。
 *
 * 确定性：排序比较全序无容差、稳定键兜底（浮点距离单遍平方和——定序
 * 固定，无并行归约）；同输入重复排序逐位一致（NFR-COR-01/02——V-04）。
 */

#include <sdurws/ird/kinematics/SolutionSet.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace sdurws::ird::kinematics {

namespace {

/// ‖q−referenceQ‖₂（§6.3 第 3 键——"当前距离"；单遍平方和，定序固定）。
double distanceToReference(const std::vector<double>& q,
                           const std::vector<double>& referenceQ)
{
    const std::size_t n = std::min(q.size(), referenceQ.size());
    double acc = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double d = q[i] - referenceQ[i];
        acc += d * d;
    }
    return std::sqrt(acc);
}

}  // namespace

// =====================================================================
// sortSolutions——四键稳定排序（§6.3 唯一实现点；键序见头注）
// =====================================================================

void sortSolutions(std::vector<KinematicSolution>& solutions,
                   const std::vector<double>& referenceQ)
{
    // 解与距离配对后排序（避免比较器内重复开方，也避免比较期取下标——
    // stable_sort 会移动元素，缓冲区下标与预计算表在比较期不对齐）。
    struct Keyed {
        KinematicSolution solution;
        double distance;
    };
    std::vector<Keyed> keyed;
    keyed.reserve(solutions.size());
    for (KinematicSolution& s : solutions) {
        const double d = distanceToReference(s.q, referenceQ);
        keyed.push_back(Keyed{std::move(s), d});
    }
    solutions.clear();

    // std::stable_sort：相等键序保持原相对次序（第 4 键 sourceInitIndex
    // 在比较器内显式兜底——总序；稳定算法保证同输入同次序，NFR-COR-02）。
    std::stable_sort(
        keyed.begin(), keyed.end(),
        [](const Keyed& a, const Keyed& b) {
            // 键 1：minimumJointMargin 降序（裕量大的优先；全序比较——
            // 无容差，§6.3 原文；+∞ 为最大值参与全序）。
            if (a.solution.minimumJointMargin != b.solution.minimumJointMargin) {
                return a.solution.minimumJointMargin > b.solution.minimumJointMargin;
            }
            // 键 2：manipulability 降序。
            if (a.solution.manipulability != b.solution.manipulability) {
                return a.solution.manipulability > b.solution.manipulability;
            }
            // 键 3：‖q−referenceQ‖₂ 升序（D-KIN-4——referenceQ 显式输入）。
            if (a.distance != b.distance) {
                return a.distance < b.distance;
            }
            // 键 4：sourceInitIndex 升序兜底（初值序——总序、与线程/分片
            // 无关）。
            return a.solution.sourceInitIndex < b.solution.sourceInitIndex;
        });

    // 就地写回（排序键全部取自解自身＋预计算距离——写回不改变次序语义）。
    solutions.reserve(keyed.size());
    for (Keyed& k : keyed) {
        solutions.push_back(std::move(k.solution));
    }
}

// =====================================================================
// deduplicateSolutions——逐轴容差成对比较（§6.1/I-KIN-3 唯一实现点）
// =====================================================================

std::vector<KinematicSolution> deduplicateSolutions(
    const std::vector<KinematicSolution>& solutions, double thresholdPerAxis)
{
    std::vector<KinematicSolution> out;
    out.reserve(solutions.size());
    for (const KinematicSolution& cand : solutions) {
        bool duplicate = false;
        for (const KinematicSolution& accepted : out) {
            if (accepted.q.size() != cand.q.size()) {
                continue;  // 维度不符＝不同构型（防御面——正常输入恒等长）
            }
            // 逐轴阈值（rad|m）——**任一轴**超阈即视为不同构型；全部轴
            // |Δq_i| ≤ 阈值才合并（per-axis 语义，非合模长/范数——
            // 附录 D 第 3 项"C1"口径；无跨周取模——§6.1）。
            bool sameConfig = true;
            for (std::size_t i = 0; i < cand.q.size(); ++i) {
                if (std::fabs(accepted.q[i] - cand.q[i]) > thresholdPerAxis) {
                    sameConfig = false;
                    break;
                }
            }
            if (sameConfig) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            out.push_back(cand);  // 输入序先到先留——组内代表＝最小 init 序
        }
    }
    return out;
}

// =====================================================================
// 规范筛选谓词（§6.2"可用解/含碰撞诊断解"——T09；比较集中实现，
// NFR-MNT-04。语义与 KIN-05 依据见 SolutionSet.hpp 工厂注，登记随卡
// §14.6 v0.9）
// =====================================================================

SolutionPredicate usableSolutionPredicate()
{
    // 无捕获 lambda→可转换为裸函数指针，纯函数可重入（§3.4 线程总约定）。
    // 判据两条件缺一不可：evaluated==true（碰撞证据在场——KIN-05"绝不
    // 解读为无碰撞"的保守面：未评价解可用性未证，不入可用集）且
    // inCollision==false（正面无碰撞判定）。
    return [](const KinematicSolution& s) {
        return s.collisionStatus.evaluated && !s.collisionStatus.inCollision;
    };
}

SolutionPredicate collisionDiagnosticPredicate()
{
    // 碰撞诊断标记＝inCollision==true（对象对明细随 collisionStatus）。
    // 求解器直接产出的解集上恒空集（碰撞解在求解期移入 filteredRecords
    // ——§6.1 生产端不变式）；本谓词服务于合并诊断呈现与语义演进——
    // 比较式唯一在此，UI/报告不得自写第二份（NFR-MNT-04）。
    return [](const KinematicSolution& s) {
        return s.collisionStatus.inCollision;
    };
}

// =====================================================================
// KinematicSolutionSet——构造排序＋四操作（§6.2/§9.2）
// =====================================================================

KinematicSolutionSet::KinematicSolutionSet(IkSolutionSet set)
    : m_statistics(set.statistics), m_sorted(std::move(set.solutions))
{
    // 构造时完成一次稳定排序（§9.2 @pre 原文——唯一排序时机；不可变
    // 视图此后不再改序）。
    sortSolutions(m_sorted, set.requestIdentity.referenceQ);
}

const SolutionSetView& KinematicSolutionSet::sorted() const
{
    return m_sorted;
}

SolutionSetView KinematicSolutionSet::filtered(const SolutionPredicate& p) const
{
    SolutionSetView out;
    out.reserve(m_sorted.size());
    for (const KinematicSolution& s : m_sorted) {
        // 保持 sorted() 序的子序列（稳定性由遍历序保证）；空谓词＝全保留。
        if (!p || p(s)) {
            out.push_back(s);
        }
    }
    return out;
}

IkSolutionSetStatistics KinematicSolutionSet::statistics() const
{
    return m_statistics;
}

std::optional<SolutionRef> KinematicSolutionSet::worstBy(WorstMetric m) const
{
    if (m_sorted.empty()) {
        return std::nullopt;  // 空集无最差项（§6.2——nullopt 语义）。
    }

    // 线性扫描取"第一个最差值"（sorted 序上稳定——同值不跳位；
    // 逐度量极值方向：最小裕量/最大条件数/最大残差，§6.2 原文）。
    std::optional<SolutionRef> worst;
    double best = 0.0;
    for (std::size_t i = 0; i < m_sorted.size(); ++i) {
        const KinematicSolution& s = m_sorted[i];
        double value = 0.0;
        bool better = false;
        switch (m) {
        case WorstMetric::MinimumJointMargin:
            value = s.minimumJointMargin;
            better = !worst.has_value() || value < best;
            break;
        case WorstMetric::ConditionNumber:
            value = s.conditionNumber;
            better = !worst.has_value() || value > best;
            break;
        case WorstMetric::PositionResidual:
            value = s.positionResidual;
            better = !worst.has_value() || value > best;
            break;
        }
        if (better) {
            best = value;
            worst = SolutionRef{i};
        }
    }
    return worst;
}

}  // namespace sdurws::ird::kinematics
