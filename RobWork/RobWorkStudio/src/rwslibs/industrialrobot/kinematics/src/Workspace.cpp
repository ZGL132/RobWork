/**
 * @file   Workspace.cpp
 * @brief  工作空间探索式采样与指标可视化浏览（KIN-09/R2）的实现翻译
 *         单元——呈现词表 token、步长规范化、探索式计划身份、装配校验、
 *         逐样本评估计算核（T05/T06 同构）、点云渲染数据组装/投影/
 *         图例/导出值行与可视化点会话回写出口。
 *
 * 设计依据：
 *   - units/kinematics.md §7.3（R2 边界——探索式结果不进 Must 判定/
 *     必验覆盖；复用 §7.2 采样黄金设施与解集设施）、§7.2（采样/逐样本
 *     评估/取消/进度/检查点语义的复用面）、§8.3/§8.4（执行协作与
 *     确定性/并行口径）、§10.2（V-11/22 取消口径、V-27 设施失败值面）、
 *     §9.6（探索式通道不产新诊断码——素材走状态/原因文本）
 *   - REQUIREMENTS KIN-09/AT-25/KIN-06/AT-04/UX-08/ARC-05/V13-01
 *     （语义见 Workspace.hpp 文件头注——本 TU 逐函数落值）
 *   - 任务契约 tasks/foundation/WP-15-T14.json acceptance 1~6
 *
 * 背景说明（实现结构对照）：逐样本评估管线与 Evaluators.cpp 的
 * runRegionCoverageComputation（T06 落位）同构——分批/取消探针/并行
 * 分片/检查点 watermark/进度上报的代码形态保持同款（读代码时可对照），
 * 差异面全部登记在 Workspace.hpp 文件头"KIN-09 与 KIN-04 的分立"注：
 * 单区域、无对账、零覆盖率、解级指标投影（bestSolution）。两管线刻意
 * 不共享代码（各自封闭）——探索式通道的演化方向（呈现增强）与正式
 * 通道的演化方向（证据治理）不同，强行抽象会把 R2 边界模糊成运行时
 * 开关（违反"结构性保证优于纪律约定"的本卡总则）。
 *
 * KIN-P5（2026-10-16，审查条目 KIN-09 语义闭合——§14.6 v0.27）：采样
 * 模式三值词表（CartesianRegion 保持旧径行为；JointSpaceRandom/
 * JointSpaceGrid 为需求权威语义"关节空间随机/网格采样→FK"的落点，
 * 生成器见 generateJointSpaceConfigurations 注）＋点云/导出的模式来源
 * 标注（渲染契约 v2）＋计划身份 canonical v2（模式与关节定义入身份）。
 * 需求 KIN-09 原文"随机均匀/关节网格采样（样本数/种子/网格步数可配）"
 * 的关节空间语义自此由本 TU 直接承载（旧"区域盒采样→逐样本 IK"单径
 * 的落点偏差，§14.6 v0.20 登记，随本迁移闭合）。
 *
 * 线程安全：全部自由函数无共享可变状态；runWorkspaceSampling 的并行
 * 分片各线程只写自己的结果槽位（全序槽位写入——T05/T06 同款）。
 * 确定性：canonical 编码纯位组装；合并按全序；文本格式化用
 * snprintf("%.17g")（最短往返的超集——跨进程逐位一致，NFR-COR-02）。
 */

#include <sdurws/ird/kinematics/Workspace.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <exception>
#include <stdexcept>
#include <thread>
#include <utility>

#include <sdurws/ird/kinematics/Ik.hpp>      // makeInitialValues/evaluationIntervals/IkSolver/
                                             //   kIkSolverContractVersion（区域逐样本求解＋
                                             //   关节模式指标投影的版本溯源）
#include <sdurws/ird/kinematics/Fk.hpp>      // FkEvaluator/PoseMetrics（KIN-P5/关节模式
                                             //   逐构型 FK——KIN-01 权威实现消费）
#include <sdurws/ird/kinematics/Sampling.hpp>  // generateSampleSet/sampleSetIdentity（§7.2 黄金设施）
#include <sdurws/ird/runtime/Description.hpp>   // runtime::CanonicalModel/JointType（自由度统计）

#include "CanonicalCodec.hpp"  // detail::putU32/putU64/putF64/putBytes（私有编码原语——R-2）

namespace sdurws::ird::kinematics {
namespace {

// =====================================================================
// 内部工具（文件局部——不进公共面）
// =====================================================================

// ★ KIN-P2（审查 A02）：旧"位置样本姿态自由容差落值 π"
// （kWorkspacePoseFreeTolerance）随任务维度语义移除——位置存在性样本
// 现以 IkRequest.taskSpace=PositionOnly（3 维任务子空间）表达姿态自由，
// 姿态项不再进目标函数（π 容差方案下姿态仍参与 6 维迭代，位置可达解
// 会被姿态项牵制——A02 根因）；Evaluators.cpp kPoseFreeTolerance 同批
// 移除。

/**
 * @brief 内置默认求解器（生产路径——query.solver 为空时的求解实现）。
 *
 * 无状态服务（IkSolver 契约——§9.2"实现无可变状态"）；每次调用返回
 * 同型静态实例（函数级 static——初始化线程安全，C++11 魔术静态）。
 * 测试路径经 query.solver 注入替身，不经过此处。
 */
const IkSolver& workspaceDefaultSolver()
{
    static const IkSolver kSolver{};
    return kSolver;
}

/**
 * @brief 求解设施失败阶段的人读短语（素材文本——DataInsufficient 的
 *        reason 组装用；三值与 IkFacilityFailureStage 一一对应）。
 */
std::string_view facilityStageText(IkFacilityFailureStage stage) noexcept
{
    switch (stage) {
    case IkFacilityFailureStage::InitialFkEvaluation:
        return "初值起点 FK 首评失败";
    case IkFacilityFailureStage::IterationFkEvaluation:
        return "迭代内 FK 再评失败";
    case IkFacilityFailureStage::ResidualRecheck:
        return "残差复验 FK 复算失败";
    }
    return "未知阶段";
}

/// 追加 ASCII 文本字节（canonical 布局的文本字段——长度前置由调用方
/// 负责；与 detail::putBytes 同族的本 TU 便捷封装）。
void putText(std::vector<std::uint8_t>& out, const std::string& text)
{
    detail::putU64(out, static_cast<std::uint64_t>(text.size()));
    detail::putBytes(out, text.data(), text.size());
}

/// 追加 f64 三元组（Vector3D 的 canonical 形态——x/y/z 链序定宽小端）。
void putVector3(std::vector<std::uint8_t>& out, const rw::math::Vector3D<double>& v)
{
    detail::putF64(out, v[0]);
    detail::putF64(out, v[1]);
    detail::putF64(out, v[2]);
}

/// 设备自由度统计（可动关节链序——validateRegionCoverageQuery 同口径；
/// 与 T06 校验面的语义单一性由"同一模型同一计数"的确定性保证，代码
/// 各自封闭的理由见 Workspace.cpp 文件头注）。
std::size_t deviceDof(const IKinRuntimeView& view)
{
    std::size_t dof = 0;
    for (const auto& j : view.model().chain().joints) {
        if (j.type != runtime::JointType::Fixed) {
            ++dof;
        }
    }
    return dof;
}

/// 数值的导出文本（%.17g——double 的最短往返超集，跨进程逐位一致；
/// +∞/−∞/NaN 由 C 库格式化为 "inf"/"-inf"/"nan"（MSVC 与 libm 一致
/// ——词表封闭无引号，CSV 安全）。NaN 在上游即被拒绝（NFR-COR-03），
/// 此处仅为防御完整。
std::string exportNumber(double v)
{
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%.17g", v);
    return std::string(buf);
}

// =====================================================================
// 关节空间确定性采样生成（KIN-P5/KIN-09 语义闭合——KIN-09 权威语义
// "随机均匀/关节网格采样"的生成落点；区域通道 §7.2 设施不复用于此：
// 采样域是关节空间不是笛卡尔区域盒）
// =====================================================================

/// splitmix64（确定性伪随机序列源——§3.4；Ik.cpp/Sampling.cpp 同款
/// 自持副本：算法公开定版、跨平台位级一致，TU 局部先例）。
std::uint64_t splitmix64Local(std::uint64_t& state)
{
    state += 0x9E3779B97F4A7C15ull;
    std::uint64_t z = state;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

/// u64 → [0,1) 均匀归一化（53 位有效——双精度无偏下界；与 Ik.cpp 同式）。
double unitFromU64Local(std::uint64_t v)
{
    return static_cast<double>(v >> 11) * (1.0 / 9007199254740992.0);  // 2^53
}

/// (seed, 序号) 二元组的完整 64 位流种子派生（KIN-P5/审查 A06 修复——
/// 与 Ik.cpp streamSeedFor 同算法同形态的自持副本；派生规则与"为什么
/// 不用 seed·2³²＋k"的说明见 Ik.cpp 同名函数注，此处不重复）。
std::uint64_t streamSeedForLocal(std::uint64_t seed, std::uint64_t index)
{
    std::uint64_t s = seed;
    const std::uint64_t a = splitmix64Local(s);
    std::uint64_t t = index ^ 0x9E3779B97F4A7C15ull;
    const std::uint64_t b = splitmix64Local(t);
    std::uint64_t u = a ^ b;
    return splitmix64Local(u);
}

/// 无分析限位轴的采样名义区间（[−π,π] rad——§6.1"无工作范围 continuous"
/// 的采样落值；与 Ik.cpp 初值生成同源口径，登记随卡 §14.6 v0.4）。
constexpr double kJointNominalLower = -3.14159265358979323846;
constexpr double kJointNominalUpper = 3.14159265358979323846;

/// 单轴采样区间（bounded 用真实区间，否则名义区间——Ik.cpp axisRange
/// 同款；§6.1 三态唯一语义的复制点，两处消费面共享同一登记）。
void jointAxisRange(const JointInterval& itv, double& lo, double& hi)
{
    if (itv.bounded) {
        lo = itv.lower;
        hi = itv.upper;
    } else {
        lo = kJointNominalLower;
        hi = kJointNominalUpper;
    }
}

/**
 * @brief 关节空间确定性构型采样（runWorkspaceSampling 步骤 1 的关节
 *        模式分支——KIN-09 权威语义"随机均匀/关节网格采样"的生成器）。
 *
 * 生成规则（确定性——同 (模式, 参数, 种子, 区间) 同集同序；黄金序
 * 登记随卡 §14.6 v0.27）：
 *   - JointSpaceRandom：第 k 个样本（0 起）状态＝(seed, k) 完整 64 位
 *     混合（streamSeedForLocal——A06 同源：旧式 "seed·2³²＋k" 丢弃
 *     seed 高 32 位），逐轴 u∈[0,1) 由 splitmix64 连续派生（轴 0→n−1），
 *     q_i＝lo_i＋u·(hi_i−lo_i)（评价区间内均匀撒点——"随机均匀"语义；
 *     无工作范围 continuous 轴取名义区间 [−π,π]）；
 *   - JointSpaceGrid：逐轴等分取点的**笛卡尔积**全网格（与初值策略
 *     JointGrid 的对角线取点不同构——分工见 Ik.hpp 该策略注：初值
 *     策略受 count 参数约束取对角点，本生成器按"网格步数"语义逐轴
 *     独立展开）。第 i 轴取 jointGridCounts[i] 个点：counts=1 → 区间
 *     中点；counts≥2 → lo＋k/(counts−1)·(hi−lo)（k=0..counts−1，
 *     **含两端点**——限位边界构型入网格）。枚举序＝轴 0 最慢、末轴
 *     最快（index＝∏ 后缀积展开——与区域 Grid"x 最慢 z 最快"同款
 *     黄金序形态）。
 *
 * @param query  [in] 探索式采样查询（模式与关节采样定义消费；合法域
 *               已由 validateWorkspaceSamplingQuery 保证）
 * @param seed   [in] 确定性种子（config.regionBudget.seed——KIN-13
 *               权威面；Random 消费、Grid 不消费）
 * @param intervals [in] 逐自由度评价区间（evaluationIntervals 产出——
 *                   采样域；长度＝设备自由度）
 * @return 构型集（生成序；每条 rad|m 链序）
 *
 * 纯函数；线程安全；确定性。
 */
std::vector<std::vector<double>> generateJointSpaceConfigurations(
    const WorkspaceSamplingQuery& query,
    std::uint64_t seed,
    const std::vector<JointInterval>& intervals)
{
    std::vector<std::vector<double>> out;
    if (query.mode == WorkspaceSamplingMode::JointSpaceRandom) {
        // 随机均匀：count 个构型，逐轴连续派生（§3.4 随机性唯一来源）。
        out.reserve(query.jointRandomCount);
        for (std::uint32_t k = 0; k < query.jointRandomCount; ++k) {
            std::uint64_t state = streamSeedForLocal(seed, k);
            std::vector<double> q;
            q.reserve(intervals.size());
            for (const JointInterval& itv : intervals) {
                double lo = 0.0;
                double hi = 0.0;
                jointAxisRange(itv, lo, hi);
                q.push_back(lo + unitFromU64Local(splitmix64Local(state)) * (hi - lo));
            }
            out.push_back(std::move(q));
        }
        return out;
    }

    // 全网格：逐轴取点表（轴序——每轴 counts[i] 个点，含两端）。
    std::vector<std::vector<double>> axisPoints(query.jointGridCounts.size());
    for (std::size_t i = 0; i < query.jointGridCounts.size(); ++i) {
        double lo = 0.0;
        double hi = 0.0;
        jointAxisRange(intervals[i], lo, hi);
        const std::uint32_t n = query.jointGridCounts[i];
        std::vector<double>& pts = axisPoints[i];
        pts.reserve(n);
        if (n == 1U) {
            pts.push_back(lo + (hi - lo) / 2.0);  // 单点＝区间中点
        } else {
            for (std::uint32_t k = 0; k < n; ++k) {
                pts.push_back(lo + (static_cast<double>(k)
                                    / static_cast<double>(n - 1U)) * (hi - lo));
            }
        }
    }
    // 笛卡尔积展开（轴 0 最慢、末轴最快——黄金序；总样本数 ≤ 上限由
    // 校验面保证，此处 size_t 累进安全）。
    std::size_t total = 1U;
    for (const std::vector<double>& pts : axisPoints) {
        total *= pts.size();
    }
    out.reserve(total);
    std::vector<std::size_t> idx(axisPoints.size(), 0U);
    for (std::size_t row = 0; row < total; ++row) {
        std::vector<double> q(axisPoints.size());
        for (std::size_t i = 0; i < axisPoints.size(); ++i) {
            q[i] = axisPoints[i][idx[i]];
        }
        out.push_back(std::move(q));
        // 末轴最快进位（odometer——从最后一位起逢满回卷）。
        for (std::size_t i = axisPoints.size(); i-- > 0;) {
            ++idx[i];
            if (idx[i] < axisPoints[i].size()) {
                break;
            }
            idx[i] = 0;
        }
    }
    return out;
}

}  // namespace

// =====================================================================
// 呈现词表 token（Workspace.hpp 三组枚举的稳定键——唯一书写点）
// =====================================================================

std::string_view workspaceColorModeToken(WorkspaceColorMode mode) noexcept
{
    switch (mode) {
    case WorkspaceColorMode::ByState:
        return "by-state";
    case WorkspaceColorMode::ByManipulability:
        return "by-manipulability";
    case WorkspaceColorMode::ByConditionNumber:
        return "by-condition-number";
    case WorkspaceColorMode::ByMinJointMargin:
        return "by-min-joint-margin";
    case WorkspaceColorMode::ByPositionResidual:
        return "by-position-residual";
    case WorkspaceColorMode::ByOrientationResidual:
        return "by-orientation-residual";
    case WorkspaceColorMode::ByCollision:
        return "by-collision";
    }
    return "by-state";
}

std::string_view workspaceProjectionPlaneToken(WorkspaceProjectionPlane plane) noexcept
{
    switch (plane) {
    case WorkspaceProjectionPlane::XY:
        return "xy";
    case WorkspaceProjectionPlane::XZ:
        return "xz";
    case WorkspaceProjectionPlane::YZ:
        return "yz";
    }
    return "xy";
}

std::string_view workspaceRenderStyleToken(WorkspaceRenderStyle style) noexcept
{
    switch (style) {
    case WorkspaceRenderStyle::Scatter:
        return "scatter";
    case WorkspaceRenderStyle::Envelope:
        return "envelope";
    }
    return "scatter";
}

std::string_view workspaceSamplingModeToken(WorkspaceSamplingMode mode) noexcept
{
    // 三值词表（KIN-P5/KIN-09——采样模式来源标注的稳定键；点云/导出
    // 列与测试的解析锚）。越界值保守缺省为 cartesian-region（词表首值
    // ——与枚举缺省构造一致，不虚构类别）。
    switch (mode) {
    case WorkspaceSamplingMode::CartesianRegion:
        return "cartesian-region";
    case WorkspaceSamplingMode::JointSpaceRandom:
        return "joint-random";
    case WorkspaceSamplingMode::JointSpaceGrid:
        return "joint-grid";
    }
    return "cartesian-region";
}

std::string_view workspaceCollisionToken(std::uint8_t collisionClass) noexcept
{
    // 三值词表：0=已检查无碰撞、1=碰撞（含样本级碰撞过滤事实）、
    // 2=未评价。越界值保守缺省为"未评价"——绝不把未知呈现为"无碰撞"
    // （KIN-05 保守读法的呈现侧延伸）。
    switch (collisionClass) {
    case 0U:
        return "collision-free";
    case 1U:
        return "in-collision";
    case 2U:
        return "not-evaluated";
    default:
        return "not-evaluated";
    }
}

// =====================================================================
// gridCountsFromSpacing——"网格步数"规范化（D-REQ-2 同款语义唯一实现点）
// =====================================================================

std::array<std::uint32_t, 3>
gridCountsFromSpacing(const rw::math::Vector3D<double>& boxSize,
                      const rw::math::Vector3D<double>& spacing)
{
    // 良构校验（fail-fast 不钳制——NFR-COR-03）：盒尺寸与步长各分量
    // 须有限且 >0。步长非正意味着"无限细分"——语义无意义，拒绝而非
    // 截断；盒尺寸非正即 RegionBox 契约违约。
    for (int i = 0; i < 3; ++i) {
        if (!std::isfinite(boxSize[i]) || boxSize[i] <= 0.0) {
            throw std::invalid_argument(
                "gridCountsFromSpacing 非法：区域盒尺寸分量（单位 m）须有限且>0，"
                "实际 " + exportNumber(boxSize[i]));
        }
        if (!std::isfinite(spacing[i]) || spacing[i] <= 0.0) {
            throw std::invalid_argument(
                "gridCountsFromSpacing 非法：网格步长分量（单位 m）须有限且>0，"
                "实际 " + exportNumber(spacing[i]));
        }
    }

    // 步长→计数：counts[i] = floor(size[i]/spacing[i]) + 1。
    // ＋1 的来源：体心规则下 k∈{0..counts-1} 的格心坐标
    // coord(k) = min + (k+0.5)·(size/counts)，counts 个格心恰好覆盖
    // [min+半格, max−半格]——与 D-REQ-2"floor(size/spacing)+1"的
    // requirements 构建期规范化语义逐字一致（同商同余则同计数——两
    // 通道样本集可比对的前提）。size < spacing 时 floor=0 → 计数 1
    // （单格心——区间太小容不下一个整步时仍给一个中心样本）。
    std::array<std::uint32_t, 3> counts{1U, 1U, 1U};
    for (int i = 0; i < 3; ++i) {
        const double raw = std::floor(boxSize[i] / spacing[i]) + 1.0;
        // 溢出防御（良构输入下 raw ≤ size/min_spacing，实际不可达；
        // 防御性钳到 u32 上界并注明——计数域有限）。
        const double capped = std::min(raw, 4.294967295e9);
        counts[static_cast<std::size_t>(i)] = static_cast<std::uint32_t>(capped);
    }
    return counts;
}

// =====================================================================
// workspaceSamplingPlan——探索式计划组装＋内容身份（会话内溯源面）
// =====================================================================

// （计划内容身份的 canonical 编码——声明的实现落点）
// canonical 字节布局（codec 版本 2；登记随卡 §14.6 v0.20（v1）/§14.6
// v0.27（v2——KIN-P5/KIN-09 模式字段扩展））：
//   "IRDWSP1"（8 字节 ASCII magic）
//   ‖ u32 codecVersion = 2（小端；v1→v2 随采样模式扩展升版——同查询
//     面在两代布局下身份不同，回显/重跑核对不跨代假借）
//   ‖ u8  samplingMode（0=CartesianRegion、1=JointSpaceRandom、
//     2=JointSpaceGrid——v2 新增首字段：模式是采样定义的第一决定量）
//   ‖ f64×3 box.center（x/y/z 链序，小端 IEEE754 位模式）
//   ‖ f64×3 box.size
//   ‖ u8  position.method（0=Grid、1=Random）
//   ‖ u32×3 position.gridCounts
//   ‖ u32 position.randomCount
//   ‖ u32 jointRandomCount（v2 新增——关节空间随机样本数）
//   ‖ u32 jointGridCountCount ‖ u32×N jointGridCounts（v2 新增——
//     逐自由度网格取点数，长度前置防拼接歧义）
//   ‖ u32 orientation.directionSamples
//   ‖ u32 orientation.rollSamples
//   ‖ len‖bytes orientation.methodToken（u64 长度前置——putText）
// 注：seed 不入计划内容身份（种子是预算参数——sampleSetIdentity 的
// 第二输入，§7.2 公式同构分面；同计划不同种子→同样本定义不同样本集
// ——身份分层与 KIN-04 一致）。methodToken 是 requirements 侧字面
// 投影（Sampling.hpp 注）——探索式恒空串，仍入字节（字段完备性）。
// 关节空间模式下 box/position/orientation 为查询回显（生成面不消费）
// ——仍入字节：身份是"查询采样定义全量"的摘要，回显字段不消费≠不
// 属于定义。

namespace {
// 计划内容身份的编码实现（文件局部——公共面只暴露 workspaceSamplingPlan；
// 单独成函数便于注释锚定字节布局）。
core::ContentIdentity workspacePlanContentIdentityImpl(const SamplingPlan& plan,
                                                       const WorkspaceSamplingQuery& query)
{
    std::vector<std::uint8_t> bytes;
    bytes.reserve(8 + 4 + 1 + 48 + 1 + 12 + 4 + 4 + 8 + 4 + 16);
    detail::putBytes(bytes, kWorkspacePlanMagic, 8);
    detail::putU32(bytes, 2U);  // codecVersion——布局演进即新版本（v2 见编码注）
    // 采样模式（v2 首字段——0/1/2 词表值直写）。
    bytes.push_back(static_cast<std::uint8_t>(query.mode));
    putVector3(bytes, plan.box.center);
    putVector3(bytes, plan.box.size);
    bytes.push_back(plan.position.method
                        == PositionSamplingDefinition::Method::Grid
                        ? 0U
                        : 1U);
    for (const std::uint32_t c : plan.position.gridCounts) {
        detail::putU32(bytes, c);
    }
    detail::putU32(bytes, plan.position.randomCount);
    // 关节空间采样定义（v2 新增——模式与其参数同入身份，防"同计划
    // 身份、不同关节定义"的跨定义假借）。
    detail::putU32(bytes, query.jointRandomCount);
    detail::putU32(bytes, static_cast<std::uint32_t>(query.jointGridCounts.size()));
    for (const std::uint32_t c : query.jointGridCounts) {
        detail::putU32(bytes, c);
    }
    detail::putU32(bytes, plan.orientation.directionSamples);
    detail::putU32(bytes, plan.orientation.rollSamples);
    putText(bytes, plan.orientation.methodToken);

    // 摘要唯一经 core::ContentDigester（CR-02——SHA-256 全仓唯一实现点）。
    core::ContentDigester digester;
    digester.update(bytes.data(), bytes.size());
    core::ContentIdentity identity;
    identity.bytes = digester.finalize();
    return identity;
}
}  // namespace

SamplingPlan workspaceSamplingPlan(const WorkspaceSamplingQuery& query)
{
    SamplingPlan plan;
    // regionObjectId＝全零保留值（探索式无 req 区域对象——"未绑定"
    // 语义；R-4：不拼任何名称）。
    plan.regionObjectId = core::ObjectId{};
    plan.box = query.region;
    plan.position = query.position;
    plan.orientation = query.orientation;
    // demands＝默认值（探索式零要求值——碰撞检查在场性只由
    // collisionSession 注入决定，V13-01：不另存第二布尔）。
    // mirrorOf＝空（无镜像派生——探索式单区域）。
    // 计划内容身份＝参数 canonical 的 SHA-256（同查询同身份——确定性；
    // 会话内溯源回显面，绝不对账快照凭据——R2 边界）。v2 编码消费
    // query 的模式与关节采样定义（见编码注）。
    plan.planContentIdentity = workspacePlanContentIdentityImpl(plan, query);
    return plan;
}

// =====================================================================
// validateWorkspaceSamplingQuery——装配校验（fail-fast 轨）
// =====================================================================

void validateWorkspaceSamplingQuery(const IKinRuntimeView& view,
                                    const WorkspaceSamplingQuery& query)
{
    // ---- 1. 求解配置合法域（KIN-13 权威校验点——validateAnalysis-
    // Configuration 复用：seed=0 非法不静默替换（I-KIN-4）、容差/迭代/
    // 计数下界、线程数下界。文案由该函数给出（含首错字段/实际值）。
    // 三模式共同前置——种子是区域随机/关节随机/初值三处随机性的唯一
    // 来源。----
    validateAnalysisConfiguration(query.config);

    // ---- 2. 采样模式词表值域防御（uint8 位型强转可造出词表外值——
    // NFR-COR-03 拒绝不静默归约）。----
    switch (query.mode) {
    case WorkspaceSamplingMode::CartesianRegion:
    case WorkspaceSamplingMode::JointSpaceRandom:
    case WorkspaceSamplingMode::JointSpaceGrid:
        break;
    default:
        throw std::invalid_argument(
            "工作空间采样查询非法：采样模式枚举值越界（三值词表，实际 "
            + std::to_string(static_cast<unsigned>(query.mode)) + "）");
    }

    // ---- 3. CartesianRegion 面：区域盒＋位置/姿态采样定义（关节空间
    // 模式不消费这三者——校验越界会强迫调用方填无意义的区域值，故按
    // 模式分派）。----
    if (query.mode == WorkspaceSamplingMode::CartesianRegion) {
        for (int i = 0; i < 3; ++i) {
            if (!std::isfinite(query.region.size[i]) || query.region.size[i] <= 0.0) {
                throw std::invalid_argument(
                    "工作空间采样查询非法：区域盒尺寸分量（基座系 {B}，单位 m）"
                    "须有限且>0，实际 " + exportNumber(query.region.size[i]));
            }
            if (!std::isfinite(query.region.center[i])) {
                throw std::invalid_argument(
                    "工作空间采样查询非法：区域盒中心分量（基座系 {B}，单位 m）"
                    "非有限——" + exportNumber(query.region.center[i]));
            }
        }

        // 位置采样定义良构（计数 0 合法——零样本场景与 §7.2 口径
        // 一致：运行产物为空集而非错误；枚举值域防御）。
        switch (query.position.method) {
        case PositionSamplingDefinition::Method::Grid:
        case PositionSamplingDefinition::Method::Random:
            break;
        default:
            throw std::invalid_argument(
                "工作空间采样查询非法：位置采样方法枚举值越界（两值词表）");
        }

        // 姿态采样定义（directionSamples/rollSamples ≥1——requirements
        // 词表原文的探索式同轨；≥1 使位姿样本数随位置样本数同零/同非零，
        // 零样本判定单看位置侧）。
        if (query.orientation.directionSamples == 0U
            || query.orientation.rollSamples == 0U) {
            throw std::invalid_argument(
                "工作空间采样查询非法：姿态采样方向数/roll 数须 ≥1");
        }
    }

    // ---- 4. JointSpaceRandom 面：jointRandomCount 无下界（0＝零样本
    // 合法——与区域通道零样本口径一致）。----

    // ---- 5. JointSpaceGrid 面：逐自由度计数良构＋笛卡尔积预算闸
    // （组合爆炸防护——fail-fast 拒绝而非静默截断，NFR-COR-03）。----
    if (query.mode == WorkspaceSamplingMode::JointSpaceGrid) {
        const std::size_t dof = deviceDof(view);
        if (query.jointGridCounts.size() != dof) {
            throw std::invalid_argument(
                "工作空间采样查询非法：jointGridCounts 维度 "
                + std::to_string(query.jointGridCounts.size())
                + " != 设备自由度 " + std::to_string(dof));
        }
        for (std::size_t k = 0; k < query.jointGridCounts.size(); ++k) {
            if (query.jointGridCounts[k] == 0U) {
                throw std::invalid_argument(
                    "工作空间采样查询非法：jointGridCounts[" + std::to_string(k)
                    + "]=0（每轴取点数须 ≥1；0 意味该轴无样本——网格语义"
                    "下应缩减轴或减计数，而非零格）");
            }
        }
        // 笛卡尔积（u64 累乘防 u32 溢出——每轴 ≤2³²−1 的良构输入下
        // 乘积仍可能超 2³²）。
        std::uint64_t product = 1U;
        for (const std::uint32_t c : query.jointGridCounts) {
            product *= c;
        }
        if (product > kJointGridMaxTotalSamples) {
            throw std::invalid_argument(
                "工作空间采样查询非法：jointGridCounts 笛卡尔积 "
                + std::to_string(product) + " 超过总样本数上限 "
                + std::to_string(kJointGridMaxTotalSamples)
                + "（组合爆炸防护——请降低逐轴取点数；拒绝而非静默截断）");
        }
    }

    // ---- 6. referenceQ 维度与有限性（D-KIN-4——三模式共同消费：
    // 区域模式的排序参考/初值伴随、关节模式的排序伴随）。----
    if (query.referenceQ.size() != deviceDof(view)) {
        throw std::invalid_argument(
            "工作空间采样查询非法：referenceQ 维度 "
            + std::to_string(query.referenceQ.size()) + " != 设备自由度 "
            + std::to_string(deviceDof(view)));
    }
    for (std::size_t k = 0; k < query.referenceQ.size(); ++k) {
        if (!std::isfinite(query.referenceQ[k])) {
            throw std::invalid_argument(
                "工作空间采样查询非法：referenceQ[" + std::to_string(k)
                + "] 非有限（rad|m；NFR-COR-03：不置零）");
        }
    }

    // ---- 7. 批大小下界。----
    if (query.maxBatchSize == 0U) {
        throw std::invalid_argument("工作空间采样查询非法：maxBatchSize 须 ≥1");
    }

    // ---- 8. TCP 在场性不在此拒（运行期 KIN-NO-TCP 素材轨——T03/T06
    // 两分口径：模型侧缺失≠调用方违约；见函数注）。
}

// =====================================================================
// runWorkspaceSampling——探索式采样计算核（T05/T06 同构管线）
// =====================================================================

WorkspaceSampleRun runWorkspaceSampling(const IKinRuntimeView& view,
                                        const WorkspaceSamplingQuery& query,
                                        evidence::IEvaluationContext& context)
{
    // ---- 装配校验（直调面与面板入口共用同一 fail-fast 面——防御直调
    // 绕过校验；NFR-COR-03）。----
    validateWorkspaceSamplingQuery(view, query);

    WorkspaceSampleRun run;
    run.mode = query.mode;  // 采样模式回显（点云/导出标注取值源）

    // ---- 步骤 1：确定性采样计划与样本集（按模式分派——KIN-P5/
    // KIN-09 关节空间采样语义闭合）。----
    run.plan = workspaceSamplingPlan(query);
    RegionSamplingBudget budget;
    budget.seed = query.config.regionBudget.seed;     // 种子权威面＝AnalysisConfiguration
    budget.threadCount = query.config.regionBudget.threadCount;
    run.budget = budget;
    // 样本集身份（§7.2 公式复用——会话内重跑核对键；R2 边界：绝不对账
    // 快照凭据、不冻结）。
    run.sampleSetIdentity =
        kinematics::sampleSetIdentity(run.plan.planContentIdentity, budget);

    // 关节空间模式：确定性构型采样（KIN-09 权威语义——"关节空间随机/
    // 网格采样"，样本＝权威关节向量；生成黄金序见 generateJointSpace-
    // Configurations 注）。区域模式走 §7.2 generateSampleSet（行为保持）。
    const std::vector<JointInterval> intervals = evaluationIntervals(view);
    if (query.mode != WorkspaceSamplingMode::CartesianRegion) {
        run.jointConfigurations = generateJointSpaceConfigurations(
            query, budget.seed, intervals);
        // ---- 零样本早退（随机 count=0 / 网格积=0 校验面已防 0 轴——
        // 随机 0 合法；空产物合法：零样本不是错误（I-REQ-6））。----
        if (run.jointConfigurations.empty()) {
            return run;
        }
    } else {
        run.samples = generateSampleSet({run.plan}, budget);

        // ---- 零样本早退（位置计数乘积=0——空产物合法：零样本不是错误
        // （I-REQ-6），completed/notRun 计数守恒式在空集上自然成立）。----
        if (run.samples.samples.empty()) {
            return run;
        }
    }

    // ---- 步骤 2：评估环境（一次提取——确定性来源全部固定；初值种子
    // 与采样种子同源＝config.regionBudget.seed，§3.4 单一种子纪律）。
    // 区域模式＝IK 求解器＋多初值集；关节模式＝FkEvaluator（KIN-01
    // 权威实现——§9.2 纯函数服务，无状态可静态共享）。----
    const std::vector<std::vector<double>> initialValues = makeInitialValues(
        query.config.initialStrategy, query.config.initialValuesCount,
        query.config.regionBudget.seed, intervals, query.referenceQ);
    const IIkSolver& solver =
        query.solver != nullptr ? *query.solver : workspaceDefaultSolver();
    const FkEvaluator fkEvaluator;  // 关节模式消费（无状态——§3.4）

    // 样本总数（两模式统一批处理游标——区域＝样本记录数、关节＝构型数）。
    const std::size_t sampleSlotCount = query.mode == WorkspaceSamplingMode::CartesianRegion
        ? run.samples.samples.size()
        : run.jointConfigurations.size();

    // 结果槽预置（全序槽位——NotRun 初值；并行分片各线程只写各自槽位。
    // 区域模式：采样点位置先于评估填入——NotRun 样本的 tcpPosition 同样
    // 有值（见 WorkspaceSampleResult 注）。关节模式：采样构型先于评估
    // 填入——NotRun 样本的 jointConfiguration 同样有值；tcpPosition 待
    // FK 产出（NotRun 无语义——点云值域对面按模式排除，见
    // buildWorkspacePointCloud 注）。
    run.results.assign(sampleSlotCount, WorkspaceSampleResult{});
    for (std::size_t i = 0; i < run.results.size(); ++i) {
        run.results[i].sampleIndex = static_cast<std::uint64_t>(i);
        if (query.mode == WorkspaceSamplingMode::CartesianRegion) {
            run.results[i].tcpPosition = run.samples.samples[i].position;
        } else {
            run.results[i].jointConfiguration = run.jointConfigurations[i];
        }
    }

    // 取消传播旗标（批内共享——任一样本观测到取消即停止批内派发；原子
    // 松散序足够——只做"是否停止"的保守判定，T05/T06 同款取舍）。
    std::atomic<bool> batchCancelled{false};

    const std::uint64_t sampleTotal = static_cast<std::uint64_t>(sampleSlotCount);
    const std::uint64_t batchCount =
        (sampleTotal + query.maxBatchSize - 1U) / query.maxBatchSize;
    std::uint64_t completedBatches = 0;

    // 单样本求解闭包（批内并行时各线程只写自己的结果槽——§8.4；两模式
    // 分派——区域＝逐样本 IK、关节＝逐构型 FK）。
    auto evaluateAt = [&](std::size_t slot) {
        // 批内样本起点取消检查（批间"每批至少一次"之外的自适应粒度）。
        if (batchCancelled.load(std::memory_order_relaxed)
            || context.cancellationRequested()) {
            batchCancelled.store(true, std::memory_order_relaxed);
            return;
        }

        // ---- 关节空间模式（KIN-P5/KIN-09 权威语义）：逐构型 FK 产点。
        // 构型可达由 FK 全序保证（KIN-01 对任意有限 q 可计算）——不存在
        // IK 的"搜索未果"形态；FK 值面错误（NoTcp 等模型侧事实）＝
        // DataInsufficient（绝不虚构可达）。FK 不可取消（§9.2"可预测
        // <1 s 内联"——取消粒度仍由批间/样本起点查询承载）。----
        if (query.mode != WorkspaceSamplingMode::CartesianRegion) {
            WorkspaceSampleResult result;
            result.sampleIndex = static_cast<std::uint64_t>(slot);
            result.tcpPosition = run.results[slot].tcpPosition;  // NotRun 占位继承
            result.jointConfiguration = run.jointConfigurations[slot];

            const kinematics::Expected<kinematics::PoseMetrics> fk =
                fkEvaluator.evaluate(view, query.tcp, run.jointConfigurations[slot]);
            if (!fk.ok()) {
                // FK 值面错误（模型侧事实——NoTcp/IllegalQ 等，§9.6 两分
                // 口径）：样本评估未完成→DataInsufficient，原因携带稳定
                // 码 token＋detail 素材（诊断链文本，不直接呈现）；绝不
                // 输出不可行（C5/C8 同源纪律）。
                result.state = SampleState::DataInsufficient;
                result.reason = std::string("FK 评估失败（模型侧事实——")
                    + std::string(kinematicsErrorCodeToken(fk.error().code))
                    + "，该样本证据缺失非任务级结论）：" + fk.error().detail;
                run.results[slot] = std::move(result);
                return;
            }
            result.state = SampleState::Reached;
            // 点云坐标＝FK 的 TCP 位置（基座系 {B}，m——构造性可达点）。
            result.tcpPosition = fk.get().tcpInBase.P();

            // bestSolution＝PoseMetrics 的解级投影（KinematicSolution 值
            // 面复用——着色六标量与回写 q 的唯一来源与区域模式同构）：
            // 残差恒 0（FK 无目标误差——"构造性可达"与"搜索收敛"的
            // 语义边界，呈现侧按采样模式解释）；碰撞状态恒未评价（碰撞
            // 评估不属 FK——KIN-05：绝不解读为无碰撞）。
            KinematicSolution projected;
            projected.q = run.jointConfigurations[slot];
            projected.positionResidual = 0.0;      // m（FK 构造性可达——无目标误差）
            projected.orientationResidual = 0.0;   // rad（同上）
            projected.jointMargins = fk.get().jointMargins;
            projected.minimumJointMargin = fk.get().minimumJointMargin;
            projected.manipulability = fk.get().manipulability;
            projected.conditionNumber = fk.get().conditionNumber;
            projected.iterations = 0U;             // 计数（无迭代——直接计算）
            projected.signature =
                configurationSignature(projected.q);  // 记录键（I-KIN-3 同源）
            projected.solverContractVersion = kIkSolverContractVersion;  // 指标产出版本溯源
            result.bestSolution = std::move(projected);
            run.results[slot] = std::move(result);
            return;
        }

        const SampleRecord& sample = run.samples.samples[slot];
        WorkspaceSampleResult result;
        result.sampleIndex = sample.sampleIndex;
        result.tcpPosition = sample.position;

        // 逐样本 IK 请求（与 T06 逐样本面同构）：位置样本＝位置存在性
        // IK——KIN-P2/A02 起 taskSpace=PositionOnly（3 维任务子空间：
        // 姿态误差不进目标函数，收敛/复验只查位置残差；旧"姿态容差 π"
        // 方案已废——它仍让姿态项参与 6 维迭代，位置可达解被牵制）；
        // 位姿样本＝完整位姿 IK（taskSpace 默认 FullPose）。
        IkRequest ikRequest;
        if (sample.kind == SampleKind::Position) {
            ikRequest.targetInBase = rw::math::Transform3D<double>(
                sample.position,
                rw::math::Rotation3D<double>(1, 0, 0, 0, 1, 0, 0, 0, 1));
            // 仅位置任务（A02）——姿态自由由任务维度表达，不由容差放宽
            // 表达；orientationTolerance 仍携带配置值（请求合法域校验面
            // ——不参与本任务收敛判定）。
            ikRequest.taskSpace = IkTaskSpace::PositionOnly;
            ikRequest.orientationTolerance =
                query.config.orientationResidualTolerance;
        } else {
            ikRequest.targetInBase = sample.pose;
            ikRequest.orientationTolerance = query.config.orientationResidualTolerance;
        }
        ikRequest.positionTolerance = query.config.positionResidualTolerance;
        ikRequest.modelView = &view;
        ikRequest.tcp = query.tcp;
        ikRequest.initialValues = initialValues;
        ikRequest.iterationLimit = query.config.iterationLimit;
        ikRequest.intervals = intervals;
        ikRequest.dedupThresholdPerAxis = query.config.ikDedupThresholdPerAxis;
        // 碰撞会话注入（V13-01——统一策略：会话在场即启用，零布尔；
        // 空＝检查不在范围，由求解器标记 collisionNotEvaluated）。
        ikRequest.collisionSession = query.collisionSession;
        ikRequest.referenceQ = query.referenceQ;
        // 对象绑定：探索式无 req 点/工况——保留值（"未绑定"语义）。
        ikRequest.targetRef = IkTargetRef{};
        // 身份面字段（形态满足——探索式不入切片身份：快照/切片/配置
        // 摘要为零值保留，模式取 Quick——估算性质的词表承载，EVI-01
        // "Quick 仅筛选/排序依据"的通道语义一致）。
        ikRequest.requestIdentity.mode = core::EvaluationMode::Quick;
        ikRequest.requestIdentity.seed = query.config.regionBudget.seed;
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
            // 取消不是结局（§9.2）——槽保持 NotRun 初值，中止批内后续
            // 派发（收尾段统一补 reason）。
            batchCancelled.store(true, std::memory_order_relaxed);
            return;
        }
        if (outcome.facilityFailure.has_value()) {
            // 求解设施失败值面（V-27 同族——非终局形态）：样本求解未
            // 完成→DataInsufficient（证据缺失族——绝不输出不可行）；
            // reason 携带失败阶段与底层细节。批内剩余样本照常派发
            // （样本级设施失败按样本处置——T06 同口径）。
            result.state = SampleState::DataInsufficient;
            result.reason = std::string("求解设施失败（")
                + std::string(facilityStageText(outcome.facilityFailure->stage))
                + "——该样本证据缺失，非任务级结论）："
                + outcome.facilityFailure->detail;
            run.results[slot] = std::move(result);
            return;
        }

        // 结局→样本状态映射（§7.2 五值词表同源口径）：1/4→Reached、
        // 5→Unreachable（解析界限确定性证明）、2/3→DataInsufficient
        // （C5/C8——绝不输出不可行）。
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

        // 碰撞要求面（探索式无 demands 要求值——KIN-05 的"要求＋无
        // 会话"判定不适用；证据缺失只来自求解器标记：会话不在场
        // （collisionNotEvaluated）或会话在场而评价未完成
        // （collisionEvidenceMissing）——都绝不解读为无碰撞）。标记
        // 直投样本级事实（着色 ByCollision 的"未评价"类）。
        result.collisionNotEvaluated =
            outcome.collisionNotEvaluated || outcome.collisionEvidenceMissing;

        // 样本级碰撞过滤事实（PartialCollision＝有解被硬过滤③碰撞
        // 过滤——着色 ByCollision 的"碰撞"类取值源之一）。
        result.collisionFiltered =
            outcome.outcomeKind == IkOutcomeKind::PartialCollision;

        // Reached 样本提取最佳解（稳定排序首位——§6.3 四键序的
        // solutions[0]；着色六标量与回写 q 的唯一来源。探索式不携带
        // 全解集——点云浏览是"每采样点一个代表解"的估算语义）。
        if (result.state == SampleState::Reached
            && !outcome.solutionSet.solutions.empty()) {
            result.bestSolution = outcome.solutionSet.solutions.front();
        }
        result.outcomeKind = outcome.outcomeKind;
        run.results[slot] = std::move(result);
    };

    for (std::uint64_t b = 0; b < batchCount; ++b) {
        // 批间协作取消查询（每批至少一次——V-22 本单元侧；ARCH §4.4
        // 的 2 s 停止派发界由本查询点＋批内探针承载——EX-T03 协议）。
        if (context.cancellationRequested()) {
            batchCancelled.store(true, std::memory_order_relaxed);
        }
        if (batchCancelled.load(std::memory_order_relaxed)) {
            break;  // 停止派发新批——剩余样本保持 NotRun（无伪完成）
        }

        // 本批的连续槽区间（§8.4——并行分片＝样本全序的连续区间；两
        // 模式统一按槽位游标——区域＝样本记录、关节＝构型）。
        const std::size_t begin =
            static_cast<std::size_t>(b) * query.maxBatchSize;
        const std::size_t end = std::min<std::size_t>(
            begin + query.maxBatchSize, sampleSlotCount);

        // 分片执行（threadCount=1 内联单线程——逐位一致；>1 时 T 个
        // 连续分片并行，各线程只写各自槽位，join 后按分片序重抛首个
        // 异常——确定性失败面；T05/T06 同款）。
        const std::size_t threads = std::min<std::size_t>(
            query.config.regionBudget.threadCount, end - begin);
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

        // ---- 批完成：检查点 watermark（样本批粒度——IBatchCheckpointSink
        // 复用，EX-T08 检查点治理的消费面；P-KIN-7 最小端口）＋进度上报
        // （批粒度，phase=kWorkspaceSamplingPhase——EX-T05 事件分发由
        // 宿主上下文承接）。----
        ++completedBatches;
        if (query.checkpointSink != nullptr) {
            query.checkpointSink->batchWatermark(completedBatches, batchCount);
        }
        const std::uint64_t percent = sampleTotal == 0U
            ? 100U
            : std::min<std::uint64_t>(100U, (end * 100U) / sampleTotal);
        context.reportProgress(static_cast<std::uint8_t>(percent),
                               kWorkspaceSamplingPhase);
    }

    // ---- 收尾：取消判定＋NotRun 样本 reason 补齐（partial 如实——
    // "取消→未完成样本 NotRun，不产完整结果"＝acceptance 2/V-11/22
    // 口径；以"槽仍处于 NotRun 初值且无原因文本"判定未派发——已评估
    // 样本的终态如实保留，不回写）。----
    std::uint64_t notRunCount = 0;
    for (WorkspaceSampleResult& r : run.results) {
        if (r.state == SampleState::NotRun) {
            ++notRunCount;
            if (r.reason.empty()) {
                r.reason = batchCancelled.load(std::memory_order_relaxed)
                    ? "协作取消：该样本未派发求解（partial——重跑同查询"
                      "同种子可得同一样本集续评）"
                    : "样本未派发（内部调度未覆盖——实现缺陷疑点，如实标记）";
            }
        }
    }
    run.cancelled = batchCancelled.load(std::memory_order_relaxed);
    run.notRunCount = notRunCount;
    run.completedCount = sampleTotal - notRunCount;

    // ---- 完整性自检（违例 logic_error——内部缺陷不静默）：双射对齐
    // （results 与样本定义面按 sampleIndex 一一对应——区域＝samples、
    // 关节＝jointConfigurations 槽位）＋计数守恒（completedCount＋
    // notRunCount＝样本总数）。----
    if (run.results.size() != sampleSlotCount) {
        throw std::logic_error(
            "工作空间采样：结果集与样本集数量不一致（双射破坏——实现缺陷）");
    }
    if (query.mode == WorkspaceSamplingMode::CartesianRegion) {
        for (std::size_t i = 0; i < run.results.size(); ++i) {
            if (run.results[i].sampleIndex != run.samples.samples[i].sampleIndex) {
                throw std::logic_error(
                    "工作空间采样：结果集与样本集 sampleIndex 未对齐（双射破坏"
                    "——实现缺陷）");
            }
        }
    } else {
        for (std::size_t i = 0; i < run.results.size(); ++i) {
            if (run.results[i].sampleIndex != static_cast<std::uint64_t>(i)) {
                throw std::logic_error(
                    "工作空间采样：结果集与构型集 sampleIndex 未对齐（双射"
                    "破坏——实现缺陷）");
            }
        }
    }
    if (run.completedCount + run.notRunCount != sampleTotal) {
        throw std::logic_error(
            "工作空间采样：完成/未运行计数守恒破坏（实现缺陷）");
    }

    return run;
}

// =====================================================================
// 点云渲染数据（buildWorkspacePointCloud/project/legend/export）
// =====================================================================

WorkspacePointCloudRenderData
buildWorkspacePointCloud(const WorkspaceSampleRun& run)
{
    WorkspacePointCloudRenderData data;
    data.contractVersion = kWorkspaceRenderContractVersion;
    data.points.reserve(run.results.size());

    // 逐 results 序投影（sampleIndex 升序——runWorkspaceSampling 的
    // 双射保证＝点云全序；确定性序）。采样模式标注（KIN-P5/KIN-09）＝
    // run.mode 词表值直投——每点携带来源模式（"点从哪来"的强制溯源列）。
    const std::uint8_t modeValue = static_cast<std::uint8_t>(run.mode);

    // 值域计入面（逐轴实测 min/max——视图缩放依据）：区域模式全样本
    // 计入（NotRun 样本的 tcpPosition 也是真实采样点——区域定义在评估
    // 前已定）；关节模式只计入已评估样本（tcpPosition＝FK 产出，NotRun
    // 槽位的占位原点不属点云事实——计入会把值域拉到原点、误导视图
    // 缩放；KIN-P5/KIN-09 关节模式语义）。
    const bool includeNotRunPosition =
        run.mode == WorkspaceSamplingMode::CartesianRegion;

    bool first = true;
    for (const WorkspaceSampleResult& r : run.results) {
        if (!includeNotRunPosition && r.state == SampleState::NotRun) {
            continue;  // 关节模式 NotRun——无位置事实，不入点云与值域
        }
        WorkspacePointCloudPoint p;
        p.sampleIndex = r.sampleIndex;
        p.position = r.tcpPosition;
        p.samplingMode = modeValue;
        p.state = r.state;
        p.collisionFiltered = r.collisionFiltered;

        // 碰撞三态类别映射（KIN-05 保守读法的呈现面）：未评价优先
        // （绝不把未评价呈现为无碰撞）→ 有碰撞（解级碰撞状态或样本级
        // 碰撞过滤事实）→ 无碰撞（仅当碰撞检查在场时 0 才表示"已检查
        // 无碰撞"）。
        // R3 审计 A7 修复（2026-10-15）：原实现的兜底 else 分支把"无
        // collisionNotEvaluated 标记的一切样本"都归入无碰撞（0）——但
        // 该标记只在会话缺席/证据缺失时由求解器置位；NotRun（取消后未
        // 派发，槽保持预置初值）、设施失败轨 DataInsufficient、搜索未果
        // （结局 2/3）与解析界限（结局 5）等"无任何构型经过碰撞检查"
        // 的样本同样没有该标记，全部落进 else 被误标 collision-free 并
        // 写入导出 CSV——直接违反本映射自身注释与 Workspace.hpp 的
        // "绝不把未知呈现为无碰撞"契约。改为按"评价凭据"分派：仅当样
        // 本携带已评价解（bestSolution 在场——会话在场时解必经碰撞检
        // 查）才允许 0；无凭据样本（无解/未运行/设施失败）一律保守归
        // 未评价（2）。
        if (r.collisionNotEvaluated) {
            p.collisionClass = 2U;
        } else if (r.collisionFiltered
                   || (r.bestSolution.has_value()
                       && r.bestSolution->collisionStatus.inCollision)) {
            p.collisionClass = 1U;
        } else if (r.bestSolution.has_value()) {
            p.collisionClass = 0U;  // 有解＝已评价凭据——0 才表示已检查无碰撞
        } else {
            p.collisionClass = 2U;  // 无凭据（未运行/无解/设施失败）——保守未评价
        }

        // 解级标量六元（bestSolution 直投——零二次换算；无解样本占位 0
        // ＋hasSolution=false——呈现侧按状态轴降级渲染，绝不把占位 0
        // 当数据）。
        if (r.bestSolution.has_value()) {
            p.hasSolution = true;
            p.manipulability = r.bestSolution->manipulability;
            p.conditionNumber = r.bestSolution->conditionNumber;
            p.minimumJointMargin = r.bestSolution->minimumJointMargin;
            p.positionResidual = r.bestSolution->positionResidual;
            p.orientationResidual = r.bestSolution->orientationResidual;
        }
        data.points.push_back(p);

        // 位置值域（逐轴实测 min/max——视图缩放依据；空点云不进入）。
        if (first) {
            data.positionMin = p.position;
            data.positionMax = p.position;
            first = false;
        } else {
            for (int axis = 0; axis < 3; ++axis) {
                data.positionMin[axis] = std::min(data.positionMin[axis], p.position[axis]);
                data.positionMax[axis] = std::max(data.positionMax[axis], p.position[axis]);
            }
        }
    }
    return data;
}

std::vector<std::pair<double, double>>
projectWorkspacePoints(const WorkspacePointCloudRenderData& data,
                       WorkspaceProjectionPlane plane)
{
    // 舍轴投影（唯一实现点）：XY 保留 (x,y)、XZ 保留 (x,z)、YZ 保留
    // (y,z)——坐标恒基座系 {B}，单位 m。
    std::vector<std::pair<double, double>> out;
    out.reserve(data.points.size());
    for (const WorkspacePointCloudPoint& p : data.points) {
        switch (plane) {
        case WorkspaceProjectionPlane::XY:
            out.emplace_back(p.position[0], p.position[1]);
            break;
        case WorkspaceProjectionPlane::XZ:
            out.emplace_back(p.position[0], p.position[2]);
            break;
        case WorkspaceProjectionPlane::YZ:
            out.emplace_back(p.position[1], p.position[2]);
            break;
        }
    }
    return out;
}

WorkspaceColorLegend workspaceLegendFor(const WorkspacePointCloudRenderData& data,
                                        WorkspaceColorMode mode)
{
    WorkspaceColorLegend legend;
    legend.mode = mode;

    // 类别维度（ByState/ByCollision）——稳定词表序逐条；无值域。
    if (mode == WorkspaceColorMode::ByState) {
        // SampleState 五值词表序（枚举声明序——黄金锁定序）。
        const std::pair<SampleState, std::string_view> kStates[]{
            {SampleState::Reached, "reached"},
            {SampleState::Unreachable, "unreachable"},
            {SampleState::DataInsufficient, "data-insufficient"},
            {SampleState::NotApplicable, "not-applicable"},
            {SampleState::NotRun, "not-run"},
        };
        for (const auto& kv : kStates) {
            WorkspaceColorLegend::Entry e;
            e.categoryToken = kv.second;
            legend.entries.push_back(std::move(e));
        }
        return legend;
    }
    if (mode == WorkspaceColorMode::ByCollision) {
        const std::pair<std::uint8_t, std::string_view> kClasses[]{
            {0U, "collision-free"},
            {1U, "in-collision"},
            {2U, "not-evaluated"},
        };
        for (const auto& kv : kClasses) {
            WorkspaceColorLegend::Entry e;
            e.categoryToken = kv.second;
            legend.entries.push_back(std::move(e));
        }
        return legend;
    }

    // 标量维度——单条图例＋实测值域（全部 hasSolution 点的 min/max；
    // 无有效点→hasRange=false——呈现"无数据"不虚构值域；+∞ 参与
    // 值域如实承载不截断）。
    bool hasAny = false;
    double lo = 0.0;
    double hi = 0.0;
    for (const WorkspacePointCloudPoint& p : data.points) {
        if (!p.hasSolution) {
            continue;
        }
        // 维度取值（六标量选择器——与 token 词表一一对应）。
        double v = 0.0;
        switch (mode) {
        case WorkspaceColorMode::ByManipulability:
            v = p.manipulability;
            break;
        case WorkspaceColorMode::ByConditionNumber:
            v = p.conditionNumber;
            break;
        case WorkspaceColorMode::ByMinJointMargin:
            v = p.minimumJointMargin;
            break;
        case WorkspaceColorMode::ByPositionResidual:
            v = p.positionResidual;
            break;
        case WorkspaceColorMode::ByOrientationResidual:
            v = p.orientationResidual;
            break;
        default:
            continue;  // 类别维度已在前处理——防御不可达
        }
        if (!hasAny) {
            lo = v;
            hi = v;
            hasAny = true;
        } else {
            lo = std::min(lo, v);
            hi = std::max(hi, v);
        }
    }
    WorkspaceColorLegend::Entry e;
    e.minValue = lo;
    e.maxValue = hi;
    legend.entries.push_back(std::move(e));
    legend.hasRange = hasAny;
    return legend;
}

std::string workspacePointCloudExportCsv(const WorkspacePointCloudRenderData& data)
{
    // 表头行（契约版本 2 的解析锚——字段序＝行注释布局；全字段无逗号
    // /引号字符，CSV 无需引号包裹。v2 新增 sampling-mode 列——采样模式
    // 来源标注，KIN-P5/KIN-09 语义闭合）。
    std::string out =
        "sample-index,x-m,y-m,z-m,sampling-mode,state,collision,has-solution,"
        "manipulability,condition-number,min-joint-margin,position-residual-m,"
        "orientation-residual-rad\n";
    for (const WorkspacePointCloudPoint& p : data.points) {
        out += std::to_string(p.sampleIndex);
        out += ',';
        out += exportNumber(p.position[0]);
        out += ',';
        out += exportNumber(p.position[1]);
        out += ',';
        out += exportNumber(p.position[2]);
        out += ',';
        // 采样模式 token（三值词表稳定键——KIN-P5/KIN-09）。
        out += workspaceSamplingModeToken(
            static_cast<WorkspaceSamplingMode>(p.samplingMode));
        out += ',';
        // 状态/碰撞类别 token（词表稳定键——呈现侧与测试的解析锚）。
        switch (p.state) {
        case SampleState::Reached:
            out += "reached";
            break;
        case SampleState::Unreachable:
            out += "unreachable";
            break;
        case SampleState::DataInsufficient:
            out += "data-insufficient";
            break;
        case SampleState::NotApplicable:
            out += "not-applicable";
            break;
        case SampleState::NotRun:
            out += "not-run";
            break;
        }
        out += ',';
        out += workspaceCollisionToken(p.collisionClass);
        out += ',';
        out += p.hasSolution ? "1" : "0";
        out += ',';
        out += exportNumber(p.manipulability);
        out += ',';
        out += exportNumber(p.conditionNumber);
        out += ',';
        out += exportNumber(p.minimumJointMargin);
        out += ',';
        out += exportNumber(p.positionResidual);
        out += ',';
        out += exportNumber(p.orientationResidual);
        out += '\n';
    }
    return out;
}

// =====================================================================
// 可视化点会话回写（KIN-06——RenderPointWriteback 出口）
// =====================================================================

RenderPointWriteback workspacePointWriteback(const WorkspaceSampleRun& run,
                                             std::uint64_t sampleIndex)
{
    // 对齐定位（sampleIndex＝全局序——双射对齐键；越界＝调用方契约
    // 违约，fail-fast）。
    auto it = std::find_if(
        run.results.begin(), run.results.end(),
        [sampleIndex](const WorkspaceSampleResult& r) {
            return r.sampleIndex == sampleIndex;
        });
    if (it == run.results.end()) {
        throw std::invalid_argument(
            "workspacePointWriteback 非法：sampleIndex 无对应样本（越界或"
            "不属于本运行——调用方契约违约）");
    }
    if (!it->bestSolution.has_value()) {
        // 无解点无可回写构型——UI 层应禁用无解点回写入口；此处 fail-fast
        // 而非空值（误用即契约违约——NFR-COR-03）。
        throw std::invalid_argument(
            "workspacePointWriteback 非法：该采样点无最佳解（状态 "
            "非 Reached——无可回写构型；UI 层应禁用无解点回写入口）");
    }
    // Render.hpp writebackOf 复用（KIN-06 出口唯一形态——结构上只有
    // 关节向量＋构型签名；消费口 writebackToSessionPose＝KinSessionPose
    // 唯一写点：零修订/零失效/不入缓存身份）。
    return writebackOf(*it->bestSolution);
}

}  // namespace sdurws::ird::kinematics
