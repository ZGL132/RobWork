/**
 * @file   PoseReach.cpp
 * @brief  独立位姿可达性分析（KIN-11）的实现翻译单元——计划预览、
 *         确定性目标生成（复用 Sampling 黄金设施）、方向覆盖计算唯一
 *         实现点、逐目标评估执行核（分批/协作取消/检查点/进度/并行
 *         分片——T05/T06 批评估执行管线同构）、分析载荷 canonical
 *         编码与 kin-pose-reachability 评估器形态。
 *
 * 设计依据：
 *   - units/kinematics.md §7.3（R2 边界——结果不进 Must 判定/必验覆盖；
 *     复用 §7.2 方向集生成器与解集设施）、§7.2（黄金设施——方向集/
 *     roll 均分/位姿组装/状态词表映射口径）、§11 T16 行、§8.3/§8.4
 *     （检查点/确定性/并行）、§9.1（错误分轨——装配 fail-fast/评估期
 *     结构化诊断/取消不是结局）
 *   - REQUIREMENTS KIN-11（位置列表×方向采样×工具滚转采样、计划预览、
 *     后台运行、协作取消、方向覆盖率报告）、KIN-05（碰撞证据缺失语义
 *     ——口径同源面）、C5/C8（搜索未果不构成不可行证明——映射约束）
 *   - 任务契约 tasks/foundation/WP-15-T16.json acceptance 1~4
 *
 * 实现口径（语义决策逐处登记，修改走设计变更并同步卡面 §14.6 v0.20）：
 *   - 目标枚举序黄金锁定：位置 i 最慢×方向 j 居中×roll r 最快——
 *     targetIndex ＝ (i·D＋j)·R＋r（§7.2 位姿样本生成序同构）；
 *   - 方向聚合口径：∃roll Reached 即该方向可达（存在性——KIN-04 位置
 *     覆盖同款）；五态聚合优先级与混合态保守归并见 computePoseReach-
 *     Coverage 注；
 *   - 位置样本评估的"姿态无约束"落值本通道不存在——全部目标都是
 *     完整位姿目标（KIN-11 语义：方向×roll 就是姿态约束本身）；
 *   - 零证据行（R2 边界的结构表达——PoseReach.hpp 文件头边界 2）。
 *
 * 线程安全：全部为纯函数/无跨调用状态（评估器实例无共享可变状态）；
 * 确定性：位模式直写/无环境熵；并行分片结果按全序槽位写回——同输入
 * 同线程数逐位一致（NFR-COR-01/02）。
 */

#include <sdurws/ird/kinematics/PoseReach.hpp>

#include <sdurws/ird/core/DiagData.hpp>  // DiagnosticRecord::make（C-3 校验工厂）
#include <sdurws/ird/core/Digest.hpp>    // ContentDigester（SHA-256 唯一算法面——CR-02）
#include <sdurws/ird/kinematics/DiagCodes.hpp>  // kKin* 码值常量（唯一书写点——禁拼码）
#include <sdurws/ird/kinematics/Ik.hpp>  // IkSolver（内置默认求解器）/makeInitialValues/
                                         //   evaluationIntervals（黄金设施复用）

#include "CanonicalCodec.hpp"  // detail::putU32/putU64/putF64/putBytes（定宽小端原语——私有头）

#include <algorithm>
#include <atomic>
#include <cmath>
#include <exception>
#include <functional>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace sdurws::ird::kinematics {

namespace {

// =====================================================================
// 装配期查询校验（调用方错误 fail-fast 轨——§9.1；NFR-COR-03；
// Evaluators.cpp 各 validate*Query 的同构面——查询字段不同、各自实现，
// 先例同款取舍）
// =====================================================================

/// 视图自由度清点（可动关节链序计数——Evaluators.cpp viewDof 同口径；
/// 单一语义两处实现以翻译单元解耦为界，先例同款）。
std::size_t viewDof(const IKinRuntimeView& view)
{
    std::size_t dof = 0;
    for (const auto& j : view.model().chain().joints) {
        if (j.type != runtime::JointType::Fixed) { ++dof; }
    }
    return dof;
}

/**
 * @brief 查询形状校验（无视图上下文的轻量面——planPoseReachability/
 *        generatePoseReachTargets 纯函数入口与执行面共用的前置校验；
 *        涉及设备自由度/TCP 解析的项在 validatePoseReachabilityQuery
 *        补全——分层原因：预览是纯计数计算，不应要求注入视图）。
 *
 * @throws std::invalid_argument 任一形状违约（NFR-COR-03：fail-fast
 *         不钳制不置零；消息携带违规项与量值——可定位诊断）
 */
void validateQueryShape(const PoseReachabilityQuery& query)
{
    // 位置列表：非空（独立分析无目标无意义——PoseReach.hpp 字段注的
    // 装配校验位阶决策）且逐条三分量有限（非有限位置拒绝入算——
    // NFR-COR-03 同源）。
    if (query.positions.empty()) {
        throw std::invalid_argument(
            "kin-pose-reachability 查询非法：位置列表为空（独立位姿可达性"
            "分析至少需要一个位置——KIN-11 语义；NFR-COR-03：装配期 "
            "fail-fast，不产空报告）");
    }
    for (std::size_t i = 0; i < query.positions.size(); ++i) {
        const rw::math::Vector3D<double>& p = query.positions[i];
        if (!std::isfinite(p[0]) || !std::isfinite(p[1]) || !std::isfinite(p[2])) {
            throw std::invalid_argument(
                "kin-pose-reachability 查询非法：positions[" + std::to_string(i)
                + "] 含非有限分量（NaN/Inf——单位 m、基座系 {B}；NFR-COR-03："
                  "不置零不入算）");
        }
    }
    // 方向/roll 采样计数：≥1（KIN-11 的采样语义——0 方向/0 roll 的
    // "分析"没有可达性判定意义；区别于 KIN-04 零样本的合法性——那里
    // 是 requirements 计划定义面，这里是分析工具输入面）。
    if (query.directionSamples < 1U) {
        throw std::invalid_argument(
            "kin-pose-reachability 查询非法：directionSamples=0（方向采样"
            "数须 ≥1——KIN-11 方向覆盖率分母；NFR-COR-03）");
    }
    if (query.rollSamples < 1U) {
        throw std::invalid_argument(
            "kin-pose-reachability 查询非法：rollSamples=0（工具滚转采样"
            "数须 ≥1——KIN-11 语义；NFR-COR-03）");
    }
    // 求解配置标量（T04/T05/T06 校验面同构——逐项列违规量值）。
    if (query.initialValuesCount < 1U) {
        throw std::invalid_argument(
            "kin-pose-reachability 查询非法：initialValuesCount=0（初值数"
            "须 ≥1；NFR-COR-03）");
    }
    if (query.iterationLimit < 1U) {
        throw std::invalid_argument(
            "kin-pose-reachability 查询非法：iterationLimit=0（迭代上限"
            "须 ≥1；NFR-COR-03）");
    }
    if (!std::isfinite(query.positionTolerance) || query.positionTolerance <= 0.0) {
        throw std::invalid_argument(
            "kin-pose-reachability 查询非法：positionTolerance 非有限或 "
            "≤0（单位 m——附录 D 第 1 项；NFR-COR-03）");
    }
    if (!std::isfinite(query.orientationTolerance)
        || query.orientationTolerance <= 0.0) {
        throw std::invalid_argument(
            "kin-pose-reachability 查询非法：orientationTolerance 非有限"
            "或 ≤0（单位 rad——附录 D 第 2 项；NFR-COR-03）");
    }
    if (!std::isfinite(query.dedupThresholdPerAxis)
        || query.dedupThresholdPerAxis <= 0.0) {
        throw std::invalid_argument(
            "kin-pose-reachability 查询非法：dedupThresholdPerAxis 非有限"
            "或 ≤0（rad|m 逐轴——附录 D 第 3 项；NFR-COR-03）");
    }
    // 执行协作参数（T06 同款校验）。
    if (query.maxBatchSize < 1U) {
        throw std::invalid_argument(
            "kin-pose-reachability 查询非法：maxBatchSize=0（批大小上限"
            "须 ≥1；NFR-COR-03）");
    }
    if (query.threadCount < 1U) {
        throw std::invalid_argument(
            "kin-pose-reachability 查询非法：threadCount=0（并行分片数"
            "须 ≥1；NFR-COR-03）");
    }
    // 种子纪律（I-KIN-4 同款——SeededRandom 序列源拒绝 0，不做 0→1
    // 静默替换；其余策略 seed=0 合法＝未用随机，IkRequestIdentity.seed
    // 语义同源）。
    if (query.initialStrategy == InitialValueStrategy::SeededRandom
        && query.seed == 0U) {
        throw std::invalid_argument(
            "kin-pose-reachability 查询非法：initialStrategy=SeededRandom "
            "而 seed=0（确定性种子 0 非法——I-KIN-4 拒绝，不做 0→1 静默"
            "替换；NFR-COR-03）");
    }
}

/**
 * @brief 查询全量校验（执行面/评估器构造器共用——形状校验＋设备自由
 *        度/TCP 键两项需视图上下文的检查）。
 *
 * @throws std::invalid_argument 任一违约（消息含 DOF 量值/TCP 键——
 *         可定位诊断；NFR-COR-03）
 */
void validatePoseReachabilityQuery(const IKinRuntimeView& view,
                                   const PoseReachabilityQuery& query)
{
    // 第一层：形状（复用轻量面——两入口同一规则，无第二套真值）。
    validateQueryShape(query);

    // 第二层：referenceQ 维度与有限性（D-KIN-4 显式输入的契约面——
    // 维度须＝设备自由度；Evaluators.cpp validateQuery 同款拒绝文案）。
    const std::size_t dof = viewDof(view);
    if (query.referenceQ.size() != dof) {
        throw std::invalid_argument(
            "kin-pose-reachability 查询非法：referenceQ 维度 "
            + std::to_string(query.referenceQ.size()) + " != 设备自由度 "
            + std::to_string(dof) + "（D-KIN-4 显式输入；NFR-COR-03："
            "装配期 fail-fast，不钳制不置零）");
    }
    for (std::size_t k = 0; k < query.referenceQ.size(); ++k) {
        if (!std::isfinite(query.referenceQ[k])) {
            throw std::invalid_argument(
                "kin-pose-reachability 查询非法：referenceQ[" + std::to_string(k)
                + "] 非有限（NaN/Inf——rad|m；NFR-COR-03：不置零）");
        }
    }

    // 第三层：TCP 键不命中（工具可解析时的选择键校验——T05/T06 同款：
    // 键非空须与工具 localName 精确相等（KinTypes.hpp TcpRef 解析规则
    // ——禁前缀/模糊匹配，R-4 禁拼串定位）；"无工具/引用悬空"不在
    // 此轨——那是评估期批量级 KIN-NO-TCP 零素材（两分口径），见
    // PoseReachabilityEvaluator 构造器）。
    if (!query.defaultTcp.tcpKey.empty()) {
        const runtime::CanonicalModel& model = view.model();
        const auto loc = model.findObject(query.defaultTcp.toolObject);
        if (loc.has_value() && loc->kind == runtime::CanonicalModel::ObjectKind::Tool) {
            const auto& tools = model.tools();
            bool keyHit = false;
            for (const auto& t : tools) {
                if (t.objectId == query.defaultTcp.toolObject
                    && t.localName == query.defaultTcp.tcpKey) {
                    keyHit = true;
                    break;
                }
            }
            if (!keyHit) {
                throw std::invalid_argument(
                    "kin-pose-reachability：tcpKey '" + query.defaultTcp.tcpKey
                    + "' 不命中工具的 canonical TCP（帧未解析——装配期 "
                    "fail-fast；KinTypes.hpp TcpRef 解析规则）");
            }
        }
    }
}

/// 基础诊断组装（码值经 DiagCodes.hpp 注册常量传入——禁字符串拼码；
/// subject 仅在语义对象存在时携带——不伪造。Evaluators.cpp makeKinDiag
/// 的同构自持副本：匿名命名空间工具不可跨翻译单元消费，先例同款取舍）。
core::DiagnosticRecord makeKinDiag(std::string_view code,
                                   std::optional<core::ObjectId> subject,
                                   const char* context,
                                   const std::string& cause,
                                   const char* recommendedAction)
{
    return core::DiagnosticRecord::make(std::string(code), std::move(subject),
                                        std::nullopt,   // localName 不伪造
                                        std::nullopt,   // runtimeName——⑥端口消费随名称面任务
                                        context, cause, recommendedAction);
}

// =====================================================================
// 内置默认求解器（Evaluators.cpp internalDefaultSolver 的同构面——
// IkSolver 无状态公共类，函数内 static 实例线程安全初始化；替身注入
// 面＝query.solver，生产路径＝本实例）
// =====================================================================

const IIkSolver& defaultPoseReachSolver()
{
    // 函数内 static——C++11 魔法静态初始化（线程安全）；IkSolver 无
    // 状态（求解参数全部随请求携带——Ik.hpp 类注），共享安全。
    static const IkSolver s_solver;
    return s_solver;
}

// =====================================================================
// 载荷编码小工具（Sampling.cpp 编码器的同构形态——绑定块共享布局；
// 各编码 TU 自持副本先例同款）
// =====================================================================

/// 追加定长 32 字节身份（ContentIdentity 的原始字节）。
void putIdentity(std::vector<std::uint8_t>& out, const core::ContentIdentity& id)
{
    detail::putBytes(out, id.bytes.data(), id.bytes.size());
}

/// 追加 CoverageTotals 六计数（T06 覆盖计数块的落值序——字段注序）。
void putCoverageTotals(std::vector<std::uint8_t>& out, const CoverageTotals& t)
{
    detail::putU64(out, t.planned);
    detail::putU64(out, t.reached);
    detail::putU64(out, t.unreachable);
    detail::putU64(out, t.dataInsufficient);
    detail::putU64(out, t.notRun);
    detail::putU64(out, t.notApplicable);
}

}  // namespace

// =====================================================================
// planPoseReachability——计划预览（AT-26）
// =====================================================================

PoseReachabilityPlan planPoseReachability(const PoseReachabilityQuery& query)
{
    // 形状校验（预览也不产非法数字——违例 fail-fast 与执行面同一规则；
    // 设备自由度/TCP 项不在此层——预览是纯计数计算，见校验函数分层注）。
    validateQueryShape(query);

    // 乘积展开（u64 防溢出——位置数 u64×两 u32 计数的晋升序保证 64 位
    // 乘法；KIN-11 三元组展开的计数语义）。
    PoseReachabilityPlan plan;
    plan.positionCount = static_cast<std::uint64_t>(query.positions.size());
    plan.directionCount = query.directionSamples;
    plan.rollCount = query.rollSamples;
    plan.ikTargetCount =
        plan.positionCount * static_cast<std::uint64_t>(plan.directionCount)
        * static_cast<std::uint64_t>(plan.rollCount);
    return plan;
}

// =====================================================================
// generatePoseReachTargets——确定性目标生成（黄金设施复用）
// =====================================================================

PoseReachTargetSet generatePoseReachTargets(const PoseReachabilityQuery& query)
{
    // 形状校验（同预览面——生成与预览同一规则，无第二套真值）。
    validateQueryShape(query);

    // 黄金设施三连消费（零自有采样数学——§7.2 D-KIN-6 黄金锁定的
    // 唯一复用通道；任何黄金漂移即时可见于目标几何）：
    //   方向集＝generateDirections（斐波那契螺旋）＋roll 序列＝
    //   generateRolls（体心均分）＋位姿组装＝poseFromDirectionRoll。
    const std::vector<rw::math::Vector3D<double>> directions =
        generateDirections(query.directionSamples);
    const std::vector<double> rolls = generateRolls(query.rollSamples);

    PoseReachTargetSet out;
    const PoseReachabilityPlan plan = planPoseReachability(query);
    out.plannedTargets = plan.ikTargetCount;
    out.targets.reserve(out.plannedTargets);

    // 枚举序黄金锁定：位置 i 最慢×方向 j 居中×roll r 最快——
    // targetIndex ＝ (i·D＋j)·R＋r（§7.2 位姿样本生成序同构；登记随卡
    // §14.6 v0.20）。逐目标位姿＝poseFromDirectionRoll(d_j, roll_r, p_i)
    // （黄金组装——T_base_tcp 目标值，基座系 {B}）。
    std::uint64_t index = 0;
    for (std::size_t i = 0; i < query.positions.size(); ++i) {
        for (std::uint32_t j = 0; j < query.directionSamples; ++j) {
            for (std::uint32_t r = 0; r < query.rollSamples; ++r) {
                PoseReachTarget t;
                t.targetIndex = index;
                t.positionIndex = static_cast<std::uint32_t>(i);
                t.directionIndex = j;
                t.rollIndex = r;
                t.position = query.positions[i];
                t.direction = directions[j];
                t.roll = rolls[r];
                t.pose = poseFromDirectionRoll(directions[j], rolls[r],
                                               query.positions[i]);
                out.targets.push_back(std::move(t));
                ++index;
            }
        }
    }

    // 内部不变量自检（目标数≠分母计数＝实现缺陷——不静默，fail-fast）。
    if (out.targets.size() != out.plannedTargets) {
        throw std::logic_error(
            "kin-pose-reachability：目标生成自检失败（目标数≠分母计数——"
            "内部缺陷，fail-fast）");
    }
    return out;
}

// =====================================================================
// computePoseReachCoverage——方向覆盖计算（唯一实现点）
// =====================================================================

PoseReachCoverageOutcome computePoseReachCoverage(
    const PoseReachTargetSet& set, const PoseReachTargetResultSet& results)
{
    // 双射核查前置（分母完整性核查键 targetIndex——缺项/重复/越界即
    // logic_error；T06 computeCoverage 同款防线）。
    if (results.results.size() != set.targets.size()) {
        throw std::logic_error(
            "kin-pose-reachability：结果集规模与目标集不一致（双射破坏"
            "——分母完整性核查失败，fail-fast）");
    }
    {
        // 逐槽对齐核查：结果序与目标序同下标同 targetIndex（执行核按
        // 全序槽位写回的结构前提——乱序/缺项组装即缺陷）。
        for (std::size_t k = 0; k < set.targets.size(); ++k) {
            if (results.results[k].targetIndex != set.targets[k].targetIndex) {
                throw std::logic_error(
                    "kin-pose-reachability：结果集与目标集 targetIndex 不对齐"
                    "（槽位 " + std::to_string(k) + "——双射破坏，fail-fast）");
            }
        }
    }

    // 逐位置聚合预备（位置列表下标域——目标集内实际出现的 position-
    // Index 上界＝最大值＋1；覆盖表按 0..max 连续展开，空位置区间自然
    // 为零计数行——分母纪律：每位置行 plannedDirections＝D 恒定，无
    // 目标的位置行只在组装违例时出现，守恒核查会拦截）。
    std::uint32_t positionCount = 0;
    std::uint32_t directionCount = 0;
    std::uint32_t rollCount = 0;
    for (const PoseReachTarget& t : set.targets) {
        positionCount = std::max(positionCount, t.positionIndex + 1U);
        directionCount = std::max(directionCount, t.directionIndex + 1U);
        rollCount = std::max(rollCount, t.rollIndex + 1U);
    }

    PoseReachCoverageOutcome out;
    out.perPosition.assign(positionCount, PosePositionCoverage{});
    for (std::uint32_t p = 0; p < positionCount; ++p) {
        out.perPosition[p].positionIndex = p;
        // 方向分母＝D（禁止按评估结果剔除——R8 同款纪律；逐方向聚合
        // 计数五态初始化为零）。
        out.perPosition[p].plannedDirections = directionCount;
        // roll 粒度明细分母＝D×R（同上纪律）。
        out.perPosition[p].poses.planned =
            static_cast<std::uint64_t>(directionCount)
            * static_cast<std::uint64_t>(rollCount);
    }

    // 逐方向中间聚合（每 (位置,方向) 对的 roll 状态小集合——五态聚合
    // 优先级的判定素材；向量规模＝位置数×方向数，计划级即可承受）。
    struct DirectionAggregate {
        std::uint64_t reached = 0;
        std::uint64_t unreachable = 0;
        std::uint64_t dataInsufficient = 0;
        std::uint64_t notRun = 0;
    };
    std::vector<DirectionAggregate> aggregates(
        static_cast<std::size_t>(positionCount) * directionCount);

    // 第一遍：逐目标归轴累计（目标粒度计数——状态表是唯一真值来源）。
    for (std::size_t k = 0; k < results.results.size(); ++k) {
        const PoseReachTargetResult& r = results.results[k];
        const PoseReachTarget& t = set.targets[k];
        PosePositionCoverage& pos = out.perPosition[t.positionIndex];
        DirectionAggregate& agg =
            aggregates[static_cast<std::size_t>(t.positionIndex) * directionCount
                       + t.directionIndex];

        // 目标粒度（roll 粒度明细）归轴——五值词表逐值累计。
        switch (r.state) {
        case SampleState::Reached:
            ++pos.poses.reached;
            ++agg.reached;
            break;
        case SampleState::Unreachable:
            ++pos.poses.unreachable;
            ++agg.unreachable;
            break;
        case SampleState::DataInsufficient:
            ++pos.poses.dataInsufficient;
            ++agg.dataInsufficient;
            break;
        case SampleState::NotApplicable:
            // 词表完备性保留值——本通道生成面不产（PoseReach.hpp 文件头
            // 注）；出现即组装缺陷，不静默。
            throw std::logic_error(
                "kin-pose-reachability：状态表出现 NotApplicable（生成面"
                "不产此态——组装缺陷，fail-fast）");
        case SampleState::NotRun:
            ++pos.poses.notRun;
            ++agg.notRun;
            break;
        }
    }

    // 第二遍：逐方向五态聚合（优先级规则见 PosePositionCoverage 注；
    // 逐值保守——绝不伪造确定性结论）。
    for (std::uint32_t p = 0; p < positionCount; ++p) {
        PosePositionCoverage& pos = out.perPosition[p];
        for (std::uint32_t d = 0; d < directionCount; ++d) {
            const DirectionAggregate& agg =
                aggregates[static_cast<std::size_t>(p) * directionCount + d];
            if (agg.reached > 0U) {
                // 规则 1：∃roll Reached——存在性口径的分子方向。
                ++pos.reachedDirections;
            } else if (agg.dataInsufficient == 0U && agg.notRun == 0U
                       && agg.unreachable > 0U) {
                // 规则 2：全部 roll ∈ {Unreachable}（由前三项为零隐含
                // ——方向下 D 个 roll 全部是解析界限确定性证明）→方向
                // 确定性不可达（C2③ 唯一确定性路径的方向级聚合）。
                ++pos.unreachableDirections;
            } else if (agg.notRun > 0U) {
                // 规则 3：∃NotRun——未完成优先（取消后方向判定不完整，
                // 重跑可消；不以局部素材下 DataInsufficient 结论——
                // NotRun＋DataInsufficient 混合归 notRun 的保守取舍，
                // 登记随卡 §14.6 v0.20）。
                ++pos.notRunDirections;
            } else {
                // 规则 4：∃DataInsufficient（含 Unreachable＋Data-
                // Insufficient 混合——无分子凭据且非全证明，保守归数据
                // 不足；C5/C8——搜索未果绝不升格为不可行）。
                ++pos.dataInsufficientDirections;
            }
        }
        // 逐位置守恒式自检（方向五态和＝分母 D——违例即实现缺陷）。
        if (pos.plannedDirections
            != pos.reachedDirections + pos.unreachableDirections
                + pos.dataInsufficientDirections + pos.notRunDirections) {
            throw std::logic_error(
                "kin-pose-reachability：方向覆盖守恒式破坏（位置 "
                + std::to_string(p) + "——实现缺陷，fail-fast）");
        }
        // 逐位置 roll 粒度守恒式自检（六计数和＝分母 D×R）。
        if (pos.poses.planned
            != pos.poses.reached + pos.poses.unreachable
                + pos.poses.dataInsufficient + pos.poses.notRun
                + pos.poses.notApplicable) {
            throw std::logic_error(
                "kin-pose-reachability：目标粒度守恒式破坏（位置 "
                + std::to_string(p) + "——实现缺陷，fail-fast）");
        }
        // 标记传播（字段注规则——素材面标记，判定归消费侧）。
        pos.downgraded = pos.dataInsufficientDirections > 0U
            || pos.poses.dataInsufficient > 0U;
        pos.incomplete = pos.notRunDirections > 0U;
    }

    // 全局汇总（目标粒度合计＝逐位置之和；标记＝任一位置命中）。
    for (const PosePositionCoverage& pos : out.perPosition) {
        out.report.targets.planned += pos.poses.planned;
        out.report.targets.reached += pos.poses.reached;
        out.report.targets.unreachable += pos.poses.unreachable;
        out.report.targets.dataInsufficient += pos.poses.dataInsufficient;
        out.report.targets.notRun += pos.poses.notRun;
        out.report.targets.notApplicable += pos.poses.notApplicable;
        out.report.downgraded = out.report.downgraded || pos.downgraded;
        out.report.incomplete = out.report.incomplete || pos.incomplete;
    }
    // 全局守恒式自检（分母＝目标集计划计数——预览/生成/报告三面一致
    // 的最终核查点；违例即实现/组装缺陷）。
    if (out.report.targets.planned != set.plannedTargets) {
        throw std::logic_error(
            "kin-pose-reachability：全局覆盖分母≠计划目标数（预览/生成/"
            "报告三面一致破坏——实现缺陷，fail-fast）");
    }
    return out;
}

// =====================================================================
// runPoseReachabilityComputation——位姿可达计算核心（纯函数服务）
// =====================================================================

PoseReachabilityComputation runPoseReachabilityComputation(
    const IKinRuntimeView& view, const PoseReachabilityQuery& query,
    const evidence::EvaluationRequest& request, evidence::IEvaluationContext& context)
{
    // 装配校验（直调面与评估器构造器同款 fail-fast 面——防御直调绕过
    // 构造器；NFR-COR-03）。
    validatePoseReachabilityQuery(view, query);

    PoseReachabilityComputation out;
    out.snapshotId = request.snapshot.snapshotId;
    out.sliceId = request.slice.sliceId;
    out.configDigest = query.configDigest;
    out.mode = request.mode;
    out.seed = query.seed;
    out.referenceQ = query.referenceQ;
    out.task = request.task;

    // ---- 计划预览＋确定性目标生成（预览/生成/报告三面同源——分母
    // 一致由 computePoseReachCoverage 全局守恒式终检）。----
    out.plan = planPoseReachability(query);
    out.targets = generatePoseReachTargets(query);

    // ---- 评估环境（逐目标共享——同一快照视图/评价区间/初值集：确定性
    // 来源全部固定；初值种子＝query.seed（SeededRandom 序列源——§3.4
    // 单一种子纪律；黄金方向集/roll 不消费种子）。----
    const std::vector<JointInterval> intervals = evaluationIntervals(view);
    const std::vector<std::vector<double>> initialValues = makeInitialValues(
        query.initialStrategy, query.initialValuesCount, query.seed, intervals,
        query.referenceQ);
    const IIkSolver& solver =
        query.solver != nullptr ? *query.solver : defaultPoseReachSolver();

    // ---- 逐目标评估（分批＋协作取消＋检查点＋进度＋并行分片——T06
    // 同构：结果槽按全序下标写回，合并＝按全序归并）。----
    const std::uint64_t targetTotal =
        static_cast<std::uint64_t>(out.targets.targets.size());
    const std::uint64_t batchCount =
        (targetTotal + query.maxBatchSize - 1U) / query.maxBatchSize;

    // 结果槽预置（全序槽位——NotRun 初值；并行分片各线程只写各自槽位，
    // 无交叉写入；取消路径的 NotRun 即此初值的自然保留）。
    out.results.results.assign(out.targets.targets.size(), PoseReachTargetResult{});
    for (std::size_t i = 0; i < out.results.results.size(); ++i) {
        out.results.results[i].targetIndex = out.targets.targets[i].targetIndex;
    }

    // 取消传播旗标（批内共享——任一目标观测到取消即停止批内派发；原子
    // 松散序足够：只做"是否停止"的保守判定，T05/T06 同款取舍）。
    std::atomic<bool> batchCancelled{false};
    std::uint64_t completedBatches = 0;

    // 单目标求解闭包（批内并行时各线程只写自己的结果槽——§8.4）。
    auto evaluateAt = [&](std::size_t slot) {
        // 批内目标起点取消检查（批间"每批至少一次"之外的自适应粒度）。
        if (batchCancelled.load(std::memory_order_relaxed)
            || context.cancellationRequested()) {
            batchCancelled.store(true, std::memory_order_relaxed);
            return;
        }
        const PoseReachTarget& target = out.targets.targets[slot];
        PoseReachTargetResult result;
        result.targetIndex = target.targetIndex;

        // 逐目标 IK 请求（全部目标＝完整位姿目标——方向×roll 就是姿态
        // 约束本身，本通道无"姿态无约束"位置目标形态）。
        IkRequest ikRequest;
        ikRequest.targetInBase = target.pose;
        ikRequest.positionTolerance = query.positionTolerance;
        ikRequest.orientationTolerance = query.orientationTolerance;
        ikRequest.modelView = &view;
        ikRequest.tcp = query.defaultTcp;
        ikRequest.initialValues = initialValues;
        ikRequest.iterationLimit = query.iterationLimit;
        ikRequest.intervals = intervals;
        ikRequest.dedupThresholdPerAxis = query.dedupThresholdPerAxis;
        ikRequest.collisionSession = query.collisionSession;
        ikRequest.referenceQ = query.referenceQ;
        // 对象绑定：本通道无区域/任务点对象（位置列表直给——文件头
        // 边界 1）——targetRef 保留缺省值（"纯服务直调面的合法缺省"，
        // IkTargetRef 注）。
        ikRequest.requestIdentity.snapshotId = request.snapshot.snapshotId;
        ikRequest.requestIdentity.sliceId = request.slice.sliceId;
        ikRequest.requestIdentity.configDigest = query.configDigest;
        ikRequest.requestIdentity.mode = request.mode;
        ikRequest.requestIdentity.seed = query.seed;
        ikRequest.requestIdentity.referenceQ = query.referenceQ;
        ikRequest.cancellationProbe = [&context, &batchCancelled]() {
            if (context.cancellationRequested()) {
                batchCancelled.store(true, std::memory_order_relaxed);
                return true;
            }
            return false;
        };

        const IkOutcome outcome = solver.solve(ikRequest);
        if (outcome.cancelled) {
            // 取消不是结局（§9.2）——槽保持 NotRun 初值，中止批内后续派发。
            batchCancelled.store(true, std::memory_order_relaxed);
            return;
        }
        if (outcome.facilityFailure.has_value()) {
            // 求解设施失败值面（F-746③——非终局形态）：目标求解未完成
            // →DataInsufficient（证据缺失族——保留分母不计分子，绝不
            // 输出不可行，C5/C8 同口径）；reason 携带失败阶段与底层
            // 细节。批内剩余目标照常派发（目标级设施失败按目标处置；
            // 降级面由 computePoseReachCoverage 的标记传播承载）。
            result.state = SampleState::DataInsufficient;
            result.reason = std::string("求解设施失败（该目标证据缺失，非任务"
                                        "级结论）：")
                + outcome.facilityFailure->detail;
            out.results.results[slot] = std::move(result);
            return;
        }

        // 结局→目标状态映射（§7.2 词表映射口径——与 T06 逐条同源）：
        // 1/4→Reached（存在性凭据）；5→Unreachable（解析界限确定性
        // 证明）；2/3→DataInsufficient（C5/C8——绝不输出不可行）。
        switch (outcome.outcomeKind) {
        case IkOutcomeKind::SolutionsFound:
        case IkOutcomeKind::PartialCollision:
            result.state = SampleState::Reached;
            break;
        case IkOutcomeKind::AnalyticBoundExceeded:
            result.state = SampleState::Unreachable;
            break;
        case IkOutcomeKind::MultiInitNoConvergence:
        case IkOutcomeKind::AllCandidatesFiltered:
            result.state = SampleState::DataInsufficient;
            break;
        }
        // 碰撞评价缺失（会话在场但构型级评价未完成——设施异常/策略侧
        // 应答不可判）：该目标即使有解也不能作"无碰撞可达"凭据（KIN-05
        // 口径）→降为 DataInsufficient＋collisionNotEvaluated 标记
        // （T06 设施臂同款；诊断在评估器面按目标计数产
        // KIN-COLLISION-UNAVAILABLE）。
        if (outcome.collisionEvidenceMissing) {
            result.state = SampleState::DataInsufficient;
            result.collisionNotEvaluated = true;
            result.reason =
                "碰撞要求在场而构型碰撞评价未完成（设施异常/策略侧应答不可判"
                "——KIN-05：该目标证据缺失，绝不视为无碰撞）";
            out.results.results[slot] = std::move(result);
            return;
        }
        result.outcomeKind = outcome.outcomeKind;
        // 碰撞未评价标记（会话不在场＝策略未启用碰撞——检查不在范围，
        // 正常态不携带原因文本；"标记面≠缺陷面"口径与 T06 同源）。
        result.collisionNotEvaluated = outcome.collisionNotEvaluated;
        out.results.results[slot] = std::move(result);
    };

    for (std::uint64_t b = 0; b < batchCount; ++b) {
        // 批间协作取消查询（每批至少一次——EX-T03 语义的执行核落点；
        // ARCH §4.4 的 2 s 停止派发界由本查询点＋批内探针承载）。
        if (context.cancellationRequested()) {
            batchCancelled.store(true, std::memory_order_relaxed);
        }
        if (batchCancelled.load(std::memory_order_relaxed)) {
            break;  // 停止派发新批——剩余目标保持 NotRun（无伪完成）
        }

        // 本批的连续槽区间（§8.4——并行分片＝目标全序的连续区间）。
        const std::size_t begin =
            static_cast<std::size_t>(b) * query.maxBatchSize;
        const std::size_t end = std::min<std::size_t>(
            begin + query.maxBatchSize, out.targets.targets.size());

        // 分片执行（threadCount=1 内联单线程——逐位一致；>1 时 T 个连续
        // 分片并行，各线程只写各自槽位，join 后按分片序重抛首个异常——
        // 确定性失败面；T05/T06 同款）。
        const std::size_t threads =
            std::min<std::size_t>(query.threadCount, end - begin);
        if (threads <= 1) {
            for (std::size_t slot = begin; slot < end; ++slot) {
                evaluateAt(slot);
                if (batchCancelled.load(std::memory_order_relaxed)) {
                    break;  // 批内中止——剩余槽走 NotRun
                }
            }
        } else {
            std::vector<std::exception_ptr> shardErrors(threads);
            auto shardWorker = [&](std::size_t t) {
                try {
                    // 第 t 片＝[begin + t·n/T, begin + (t+1)·n/T)——连续
                    // 区间；余数摊入末片。
                    const std::size_t n = end - begin;
                    const std::size_t lo = begin + t * n / threads;
                    const std::size_t hi = begin + (t + 1) * n / threads;
                    for (std::size_t slot = lo; slot < hi; ++slot) {
                        if (batchCancelled.load(std::memory_order_relaxed)) {
                            return;
                        }
                        evaluateAt(slot);
                    }
                } catch (...) {
                    shardErrors[t] = std::current_exception();
                }
            };
            std::vector<std::thread> workers;
            workers.reserve(threads - 1);
            for (std::size_t t = 1; t < threads; ++t) {
                workers.emplace_back(shardWorker, t);
            }
            shardWorker(0);  // 主线程跑第 0 片——避免空等
            for (std::thread& w : workers) {
                w.join();
            }
            // 异常按分片序重抛首个（确定性失败面——NFR-COR-02）。
            for (std::size_t t = 0; t < threads; ++t) {
                if (shardErrors[t] != nullptr) {
                    std::rethrow_exception(shardErrors[t]);
                }
            }
        }

        if (batchCancelled.load(std::memory_order_relaxed)) {
            break;  // 批内取消/失败中断——本批剩余与后续批不推进 watermark
        }

        // ---- 批完成：检查点 watermark（目标批粒度——§8.3 落点；
        // P-KIN-7 最小端口复用）＋进度上报（phase="solve-pose-target"，
        // EX-T05 语义）。----
        ++completedBatches;
        if (query.checkpointSink != nullptr) {
            query.checkpointSink->batchWatermark(completedBatches, batchCount);
        }
        const std::uint64_t percent = targetTotal == 0U
            ? 100U
            : std::min<std::uint64_t>(100U, (end * 100U) / targetTotal);
        context.reportProgress(static_cast<std::uint8_t>(percent),
                               kPoseReachabilityPhase);
    }

    // ---- 取消/中断收尾：未评估目标如实 NotRun（§7.2"取消→partial→
    // 不产正式覆盖结论"同口径；以"槽仍处于 NotRun 初值且无原因文本"
    // 判定未派发——已评估目标的终态如实保留，不回写）。----
    for (PoseReachTargetResult& r : out.results.results) {
        if (r.state == SampleState::NotRun && r.reason.empty()) {
            r.reason =
                "未运行：协作取消/失败中止后未派发（NotRun 如实标记——已完成"
                "目标批 " + std::to_string(completedBatches) + "/"
                + std::to_string(batchCount) + "，watermark 保留可续）";
        }
    }

    // ---- 方向覆盖计算（唯一实现点委托——双射核查/守恒式内置）。----
    const PoseReachCoverageOutcome coverage =
        computePoseReachCoverage(out.targets, out.results);
    out.perPosition = std::move(coverage.perPosition);
    out.report = std::move(coverage.report);

    // 计算核心到此为止（分析素材组装唯一在评估器 evaluate——测试直调
    // 本函数断言目标生成/逐目标/取消语义，NFR-MNT-01）。
    return out;
}

// =====================================================================
// encodePoseReachabilityPayloadCanonical——载荷编码（布局见头文件注）
// =====================================================================

std::vector<std::uint8_t> encodePoseReachabilityPayloadCanonical(
    const PoseReachabilityComputation& computation, const core::TaskIdentity& task)
{
    std::vector<std::uint8_t> out;
    const char magic[] = "IRDPR01";
    out.insert(out.end(), magic, magic + 7);
    detail::putU32(out, 1U);  // codecVersion

    // 标记块（两标记——报告语义面的字节承载；u8 布尔，T06 同款宽度）。
    out.push_back(computation.report.incomplete ? 1U : 0U);
    out.push_back(computation.report.downgraded ? 1U : 0U);

    // 绑定块（§5.6 六要素——T04/T05/T06 布局同构；mode u8 词表值）。
    putIdentity(out, computation.snapshotId);
    putIdentity(out, computation.sliceId);
    putIdentity(out, computation.configDigest);
    out.push_back(static_cast<std::uint8_t>(computation.mode));
    detail::putU64(out, computation.seed);
    detail::putU32(out, static_cast<std::uint32_t>(computation.referenceQ.size()));
    for (const double x : computation.referenceQ) {
        detail::putF64(out, x);
    }
    // 任务五元组（4×16B 强类型 id＋attempt u64——T04/T05/T06 绑定块同
    // 布局；attempt 为 u64 序号非 Id128——core TaskIdentity 字段形态）。
    detail::putBytes(out, task.project.bytes.data(), task.project.bytes.size());
    detail::putBytes(out, task.branch.bytes.data(), task.branch.bytes.size());
    detail::putBytes(out, task.revision.bytes.data(), task.revision.bytes.size());
    detail::putBytes(out, task.run.bytes.data(), task.run.bytes.size());
    detail::putU64(out, task.attempt.value);
    // 评估键（长度前缀串——词形 kebab，禁 NUL 由常量保证）＋契约版本。
    detail::putU32(out,
                   static_cast<std::uint32_t>(std::char_traits<char>::length(
                       kPoseReachabilityEvaluationKey)));
    detail::putBytes(out, kPoseReachabilityEvaluationKey,
                     std::char_traits<char>::length(kPoseReachabilityEvaluationKey));
    detail::putU32(out, kPoseReachabilityContractVersion);

    // 计划块（预览四计数——AT-26 的载荷承载面）。
    detail::putU64(out, computation.plan.positionCount);
    detail::putU32(out, computation.plan.directionCount);
    detail::putU32(out, computation.plan.rollCount);
    detail::putU64(out, computation.plan.ikTargetCount);

    // 全局汇总块（目标六计数——V-14"分子分母计数"口径的本通道对应物）。
    putCoverageTotals(out, computation.report.targets);

    // 逐位置块（方向五计数＋roll 粒度六计数＋两标记）。
    detail::putU32(out, static_cast<std::uint32_t>(computation.perPosition.size()));
    for (const PosePositionCoverage& pos : computation.perPosition) {
        detail::putU32(out, pos.positionIndex);
        detail::putU64(out, pos.plannedDirections);
        detail::putU64(out, pos.reachedDirections);
        detail::putU64(out, pos.unreachableDirections);
        detail::putU64(out, pos.dataInsufficientDirections);
        detail::putU64(out, pos.notRunDirections);
        putCoverageTotals(out, pos.poses);
        out.push_back(pos.downgraded ? 1U : 0U);
        out.push_back(pos.incomplete ? 1U : 0U);
    }

    // 逐目标状态表（报告明细层——目标几何不入载荷，见头文件注）。
    detail::putU32(out,
                   static_cast<std::uint32_t>(computation.results.results.size()));
    for (const PoseReachTargetResult& r : computation.results.results) {
        detail::putU64(out, r.targetIndex);
        // 三元组下标（positionIndex/directionIndex/rollIndex——状态表
        // 自携带分组键，消费侧免回查目标几何）。
        std::uint32_t pi = 0;
        std::uint32_t di = 0;
        std::uint32_t ri = 0;
        // 对齐回查（按 targetIndex 找目标——双射保证命中；乱序组装即
        // 缺陷，fail-fast 不静默）。
        bool found = false;
        for (const PoseReachTarget& t : computation.targets.targets) {
            if (t.targetIndex == r.targetIndex) {
                pi = t.positionIndex;
                di = t.directionIndex;
                ri = t.rollIndex;
                found = true;
                break;
            }
        }
        if (!found) {
            throw std::logic_error(
                "kin-pose-reachability：载荷编码对齐失败（结果 targetIndex 无"
                "对应目标——双射破坏，fail-fast）");
        }
        detail::putU32(out, pi);
        detail::putU32(out, di);
        detail::putU32(out, ri);
        out.push_back(static_cast<std::uint8_t>(r.state));
        if (r.outcomeKind.has_value()) {
            out.push_back(1U);
            out.push_back(static_cast<std::uint8_t>(*r.outcomeKind));
        } else {
            out.push_back(0U);
        }
        out.push_back(r.collisionNotEvaluated ? 1U : 0U);
        detail::putU32(out, static_cast<std::uint32_t>(r.reason.size()));
        detail::putBytes(out, r.reason.data(), r.reason.size());
    }
    return out;
}

// =====================================================================
// makePoseReachabilityDescriptor——依赖声明与形态（R2 增补行）
// =====================================================================

evidence::EvaluatorDescriptor makePoseReachabilityDescriptor()
{
    evidence::EvaluatorDescriptor d;
    d.key = kPoseReachabilityEvaluationKey;
    d.contractVersion = kPoseReachabilityContractVersion;

    // 依赖声明五条（KIN-11 分析的真实消费面——文件头边界 1/边界 4：
    // 无 req.regions/req.sampling-plans/req.conditions——位置列表直给、
    // 无工况要求值；collision-models 一条 Conditional——条件依赖语义：
    // 碰撞启用状态只读自 policy，策略未启用碰撞时该键不进切片（V13-01），
    // resolutionNote 登记条件语义，T06 同款）。
    d.inputs = {
        {"model.robot-design", evidence::DependencyKind::Object,
         evidence::DependencyRequiredness::Required, std::nullopt,
         "规范机器人链对象（Object 闭包内 robot-design——链/关节/限位真值）"},
        {"tcp", evidence::DependencyKind::Object,
         evidence::DependencyRequiredness::Required, std::nullopt,
         "工具定义对象（Object→tool-definition——TCP 偏置真值，AT-05① 失效面）"},
        {"policy.resolved", evidence::DependencyKind::Policy,
         evidence::DependencyRequiredness::Required, std::nullopt,
         "已解析工程策略内容身份（CON-06——策略变更全列失效；碰撞启用"
         "状态的只读源，V13-01）"},
        {"config.ik", evidence::DependencyKind::Configuration,
         evidence::DependencyRequiredness::Required, std::nullopt,
         "分析求解配置（KIN-13 canonical——入 sliceId；位姿可达分析消费"
         "初值/迭代/容差/去重面）"},
        {"collision-models", evidence::DependencyKind::Object,
         evidence::DependencyRequiredness::Conditional, std::nullopt,
         "碰撞模型集合对象（策略启用碰撞时进切片——V13-01 条件依赖；"
         "本单元不设碰撞布尔开关）"},
    };

    // Profile 声明引用：域 id 词表 "kin"（evidence isDomainProfileId）；
    // contentIdentity 置零值＝域不可申报（evidence §9.5/R-3）。
    d.profile.profileId = "kin";
    d.profile.version = "1";
    d.profile.contentIdentity = core::ContentIdentity{};

    // 模式集：{Quick} 单值（§7.3"Quick/估算性质，AT-25/26"原文的模式面
    // 表达——Verified 不支持＝探索式结果不进正式证据链（R2 边界）；
    // 文件头边界 3。登记随卡 §14.6 v0.20）。
    d.supportedModes = {core::EvaluationMode::Quick};

    // 无跨调用状态（实例可共享）＋完全线程安全（逐目标并行分片要求
    // 可重入——§9.2 头注同款）。
    d.stateless = true;
    d.threadSafety = evidence::ThreadSafety::FullyThreadSafe;
    return d;
}

// =====================================================================
// PoseReachabilityEvaluator——评估器实现（类注见 PoseReach.hpp）
// =====================================================================

PoseReachabilityEvaluator::PoseReachabilityEvaluator(const IKinRuntimeView* view,
                                                     PoseReachabilityQuery query)
    : m_view(view), m_query(std::move(query)),
      m_descriptor(makePoseReachabilityDescriptor())
{
    // 装配期 fail-fast（类注错误分轨）：空视图＝装配违约；查询非法＝
    // 调用方错误——两轨都不进入评估输出面（NFR-COR-03）。
    if (m_view == nullptr) {
        throw std::invalid_argument(
            "kin-pose-reachability：注入视图为空指针（宿主注入契约违约——O-37）");
    }
    validatePoseReachabilityQuery(*m_view, m_query);

    // 工具侧缺失（未配置/悬空）保留到评估期＝批量级 KIN-NO-TCP 零素材
    // （T05/T06 两分口径同源——键不命中已在 validatePoseReachabilityQuery
    // 拒绝）。
    const runtime::CanonicalModel& model = m_view->model();
    if (model.tools().empty()) {
        KinematicsError err;
        err.code = KinematicsErrorCode::NoTcp;
        err.detail = "TCP 未配置：快照模型无工具（KIN-NO-TCP 素材——§9.6）";
        m_setupError = std::move(err);
    } else {
        const auto loc = model.findObject(m_query.defaultTcp.toolObject);
        if (!loc.has_value()
            || loc->kind != runtime::CanonicalModel::ObjectKind::Tool) {
            KinematicsError err;
            err.code = KinematicsErrorCode::NoTcp;
            err.detail = "TCP 引用悬空：toolObject 未解析到快照工具"
                         "（KIN-NO-TCP 素材——§9.6）";
            m_setupError = std::move(err);
        }
    }
}

const evidence::EvaluatorDescriptor& PoseReachabilityEvaluator::descriptor() const
{
    return m_descriptor;
}

evidence::EvaluationOutput PoseReachabilityEvaluator::evaluate(
    const evidence::EvaluationRequest& request,
    evidence::IEvaluationContext& context)
{
    evidence::EvaluationOutput out;

    // ---- 批量级零素材轨：工具侧缺失（KIN-NO-TCP——T03/T05/T06 两分
    // 口径：模型侧缺失≠工程不可行，零 payload 零证据行，判定留消费侧）。----
    if (m_setupError.has_value()) {
        out.diagnostics.push_back(makeKinDiag(
            kKinNoTcp, std::nullopt, "kin-pose-reachability 位姿可达性分析",
            m_setupError->detail,
            "配置工具 TCP 或修正 tcpRef 引用后重提位姿可达性分析"));
        return out;
    }

    // ---- 纯计算核（计划预览→目标生成→逐目标评估（取消/检查点/进度
    // 在核内）→方向覆盖计算）。----
    PoseReachabilityComputation computation =
        runPoseReachabilityComputation(*m_view, m_query, request, context);

    // ---- 素材组装（payload canonical＋诊断——**零证据行**：R2 边界
    // 的结构表达，见 PoseReach.hpp 文件头边界 2；摘要唯一经
    // core::ContentDigester，CR-02）。----
    const std::vector<std::uint8_t> payloadBytes =
        encodePoseReachabilityPayloadCanonical(computation, request.task);
    core::ContentDigester digester;
    digester.update(payloadBytes.data(), payloadBytes.size());
    core::ContentIdentity payloadDigest;
    payloadDigest.bytes = digester.finalize();
    out.payload = evidence::DomainPayload{kPoseReachabilityPayloadToken, payloadBytes,
                                          payloadDigest};

    // 诊断面（不完整/碰撞评价缺失——两码均 §9.6 在册行，产码面随本任务；
    // 不新增码值——禁私定新码凑数纪律）。
    if (computation.report.incomplete) {
        out.diagnostics.push_back(makeKinDiag(
            kKinResultIncomplete, std::nullopt,
            "kin-pose-reachability 位姿可达性分析",
            "逐目标评估不完整：NotRun " + std::to_string(computation.report.targets.notRun)
                + " 目标（协作取消/失败中止——partial 不产正式覆盖结论，"
                  "重跑同一 query；§7.2 同口径）",
            "恢复任务（新 attempt 自 watermark 续跑）后按同一 query 复评"));
    }
    // 碰撞证据缺失诊断（KIN-COLLISION-UNAVAILABLE——会话在场面：存在
    // 构型级碰撞评价未完成的目标（设施异常/策略侧应答不可判）；计数
    // 口径＝状态表中携带原因文本的 collisionNotEvaluated 目标（"标记
    // 面≠缺陷面"——会话不在场的正常态标记无原因文本，不在此计数，
    // T06 同源口径）。
    {
        std::uint64_t collisionMissing = 0;
        for (const PoseReachTargetResult& r : computation.results.results) {
            if (r.collisionNotEvaluated && !r.reason.empty()) {
                ++collisionMissing;
            }
        }
        if (collisionMissing > 0) {
            out.diagnostics.push_back(makeKinDiag(
                kKinCollisionUnavailable, std::nullopt,
                "kin-pose-reachability 位姿可达性分析",
                "会话在场的目标中 " + std::to_string(collisionMissing)
                    + " 个目标碰撞评价未完成（设施异常/策略侧应答不可判——"
                      "证据缺失素材，绝不视为无碰撞；KIN-05）",
                "接入碰撞检测器或修正策略碰撞规则后按同一 query 复评"));
        }
    }

    // 携带模式敏感性标记（Quick 的 screening-only 语义在 mode 字段——
    // 载荷绑定块已携带；效力门禁归消费侧，§8.4）。
    return out;
}

// =====================================================================
// PoseReachabilityEvaluatorFactory——宿主注入工厂（闭包捕获；create
// 无参——O-37 裁决形态，WorkspaceSamplerFactory 同构）
// =====================================================================

PoseReachabilityEvaluatorFactory::PoseReachabilityEvaluatorFactory(
    const IKinRuntimeView* view, PoseReachabilityQuery query)
    : m_view(view), m_query(std::move(query)),
      m_descriptor(makePoseReachabilityDescriptor())
{
    // 与评估器同一装配校验面（工厂是宿主的注入入口——违约在装配期
    // 暴露，不迟至 create()）。
    if (m_view == nullptr) {
        throw std::invalid_argument(
            "kin-pose-reachability 工厂：注入视图为空指针（宿主注入契约违约"
            "——O-37）");
    }
    validatePoseReachabilityQuery(*m_view, m_query);
}

const evidence::EvaluatorDescriptor& PoseReachabilityEvaluatorFactory::descriptor()
    const
{
    return m_descriptor;
}

std::unique_ptr<evidence::IEngineeringEvaluator>
PoseReachabilityEvaluatorFactory::create() const
{
    // 无参签名（O-37 裁决——注册表兼容）；视图/求解器/检查点/碰撞会话
    // 指针经闭包传递——注入语义的唯一通道（生命周期约束见查询值注）。
    return std::make_unique<PoseReachabilityEvaluator>(m_view, m_query);
}

}  // namespace sdurws::ird::kinematics
