/**
 * @file   EnvelopeTest.cpp
 * @brief  近似外包络估算用例组（KinEnvelope，WP-15-T15 批）——射线边界
 *         Rmax（默认 180 方向、球壳黄金模型解析对照）、可取消（中途/
 *         入口/求解器内部三轨）、"近似、非精确"类型化标注（AT-26）、
 *         方向集黄金锁定与同种子重跑确定性（NFR-COR-01）、探针接线
 *         （位置存在性口径＋计数守恒）、设施失败非终局轨、fail-fast
 *         校验矩阵与 Expected 错误面。任务契约 WP-15-T15 acceptance
 *         1~4 逐条具名自证。
 *
 * 设计依据：
 *   - units/kinematics.md §7.3（KIN-10 边界——近似不作 Must 判定依据）、
 *     §7.2（方向集黄金设施 generateDirections；存在性口径）、§8.3
 *     （取消协作探针）、§8.4/§3.4（确定性——同种子重跑一致；seed 唯一
 *     随机来源）、§9.1（fail-fast 与 Expected 两轨）
 *   - REQUIREMENTS KIN-10（射线边界 Rmax 默认 180 方向、可取消、标注
 *     "近似、非精确"）、AT-26（外包络标注"近似非精确"）
 *   - 黄金几何：KinFkFixture 夹具＋本文件球壳三旋转发（解析外包络
 *     Rmax＝L1＋L2 独立推导——附录 D 第 9 项"独立参考实现对照"类别）
 *   - 任务契约 tasks/foundation/WP-15-T15.json（acceptance 1~4）
 *
 * 测试策略：真实 IkSolver 驱动数值路径（球壳模型解析值 1.8 m 对照）；
 * 可控求解器替身（CountingSolver——注入结局/取消/设施失败脚本并计数
 * ——接线断言面，RegionCoverageTest ScriptedSolver 同款先例）。估算
 * 预算说明：数值用例 positionTolerance 放宽至 1e-4 m（近似语义下的一
 * 档预算选择——Rmax 的二分分辨率随之放宽，仍满足断言容差 1e-3 m）。
 */

#include "KinFkFixture.hpp"

#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/kinematics/Bounds.hpp>
#include <sdurws/ird/kinematics/Envelope.hpp>
#include <sdurws/ird/kinematics/Ik.hpp>
#include <sdurws/ird/kinematics/Sampling.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace sdurws::ird::kinematics::testfixture;
namespace kin = sdurws::ird::kinematics;
using kin::EnvelopeQuery;
using kin::EnvelopeRayRecord;
using kin::EnvelopeRayState;
using kin::EnvelopeResult;

namespace {

// =====================================================================
// 黄金几何：球壳三旋转发（本文件自持——附录 D 第 9 项独立推导）
// =====================================================================
// 黄金几何：球壳三旋转发（本文件自持——附录 D 第 9 项独立推导）
// =====================================================================

/**
 * @brief 球壳工作空间的 3R 定位＋2R 球腕模型（5 自由度；L1＝1.1 m、
 *        L2＝0.7 m；腕部两关节 origin 在 TCP 点、后继偏置为零——
 *        位置与腕部角无关）。
 *
 * 解析工作空间（独立推导——定位三轴交于原点）：TCP 位置＝
 * Rz(q1)·Ry(q2)·[(1.1,0,0)＋Ry(q3)·(0.7,0,0)]——内括号向量在 xz 平面
 * 内取任意方向（q3 全周）、模长 ∈ [|L1−L2|, L1+L2]＝[0.4, 1.8]；Ry/Rz
 * 复合把该向量旋到任意空间方向 → 可达集＝球壳 {0.4 ≤ ‖p‖ ≤ 1.8}。
 * 逐方向射线边界（外包络真值）＝Rmax＝1.8 m（全部方向）。
 * 解析上界（Bounds 三角形不等式）＝0＋0＋1.1＋0.7＋0＝1.8 m——与真值
 * 相等（三轴原点无基座偏置、腕部零偏置、无移动关节）。
 *
 * 腕部解耦的设计依据（golden kin-coverage "planar-3r-centered" 同款
 * 模式——coverage-inputs.json model.note）：位置存在性探针的目标姿态
 * 恒定（恒等）而容差 π 无约束——若模型无腕部自由度，六维阻尼最小二乘
 * 的不可满足姿态分量会把迭代拉向姿态妥协点、位置残差停滞在远大于容差
 * 处（搜索未果）——包络全零。腕部两轴（Y＋Z，过 TCP、零偏置）使
 * (位置∈球壳 ∧ 姿态＝恒等) 六维误差恰可满足：q4≡−(q2+q3)、q5≡−q1
 * ——DLS 收敛到位置解，探针语义成立。
 *
 * 限位 ±10 rad（≈π＋2π 余量）的用意：DLS 迭代落点带 2πk 周期偏移
 * （腕部行程 q4＝−(q2+q3)±2πk、q5＝−q1±2πk），±π 级限位会把大量
 * 收敛解在硬过滤②限位处滤掉（实测 24 收敛解滤 20——测试伪影，非
 * 语义）——放宽限位消除过滤噪声，聚焦包络算法语义。真实模型限位
 * 语义的过滤路径由 IkTest/黄金套件承载，非本文件职责。
 */
rt::CanonicalModel sphericalShellModel()
{
    constexpr double L1 = 1.1;   // m
    constexpr double L2 = 0.7;   // m
    constexpr double kWideRange = 10.0;  // rad（π＋2π 余量——结构体注）
    std::vector<rt::CanonicalJoint> joints;
    joints.push_back(revolute("e1", rw::math::Vector3D<double>(0, 0, 1),
                              trans(0, 0, 0), -kWideRange, kWideRange));
    joints.push_back(revolute("e2", rw::math::Vector3D<double>(0, 1, 0),
                              trans(0, 0, 0), -kWideRange, kWideRange));
    joints.push_back(revolute("e3", rw::math::Vector3D<double>(0, 1, 0),
                              trans(L1, 0, 0), -kWideRange, kWideRange));
    // 腕部两轴（过 TCP 点——origin 距 TCP 零杠杆，位置不受 q4/q5 影响）。
    joints.push_back(revolute("e4", rw::math::Vector3D<double>(0, 1, 0),
                              trans(L2, 0, 0), -kWideRange, kWideRange));
    joints.push_back(revolute("e5", rw::math::Vector3D<double>(0, 0, 1),
                              trans(0, 0, 0), -kWideRange, kWideRange));
    return makeModel(joints, trans(0, 0, 0));
}

// =====================================================================
// 包络查询组装（球壳模型面——3 自由度）
// =====================================================================

EnvelopeQuery makeQuery(kin::IIkSolver* solver, std::uint32_t directions,
                        std::uint64_t seed = 20261013U)
{
    EnvelopeQuery q;
    q.referenceQ = {0.0, 0.0, 0.0, 0.0, 0.0};  // rad（5 自由度——D-KIN-4）
    q.initialValuesCount = 24U;  // 多初值降局部极小（黄金覆盖同款预算——
    q.iterationLimit = 400U;     // coverage-inputs 装配先例：24×400）
    q.positionTolerance = 1e-4;      // m（估算预算——文件头注说明）
    q.dedupThresholdPerAxis = 1e-6;  // rad（透传完整性——存在性判据不消费）
    q.directionCount = directions;
    q.seed = seed;
    q.solver = solver;
    return q;
}

/// TcpRef（夹具 canonical 工具——idFrom("kin-tool") 同源）。
kin::TcpRef shellTcp()
{
    kin::TcpRef tcp;
    tcp.toolObject = idFrom<core::ObjectId>("kin-tool");
    return tcp;
}

// =====================================================================
// 可控求解器替身（脚本结局/取消/设施失败；计数——守恒断言面；
// 不主动调用取消探针——计数确定可数）
// =====================================================================

class CountingSolver final : public kin::IIkSolver {
public:
    /// 请求记录（接线断言面——目标位置/两容差）。
    struct RecordedRequest {
        rw::math::Vector3D<double> targetPosition;  ///< 基座系 {B}，m
        double positionTolerance = 0.0;             ///< m
        double orientationTolerance = 0.0;          ///< rad
    };

    /// 结局脚本：按调用序取值（耗尽后重复末值）。
    /// reachFirst：前 n 次返回 Reached，其后返回 facilityFailure。
    /// cancelAfterN：前 n 次 Reached，其后返回 cancelled=true。
    enum class Script : std::uint8_t {
        ConstantKind,   ///< 恒返回注入结局（presence 不变式自补）
        ReachThenFacility,  ///< 第 1 次 Reached、其后设施失败
        ReachThenCancel,    ///< 第 1 次 Reached、其后取消
    };

    CountingSolver(Script script, kin::IkOutcomeKind kind = kin::IkOutcomeKind::SolutionsFound)
        : m_script(script), m_kind(kind)
    {
    }

    kin::IkOutcome solve(const kin::IkRequest& request) const override
    {
        RecordedRequest rec;
        rec.targetPosition = request.targetInBase.P();
        rec.positionTolerance = request.positionTolerance;
        rec.orientationTolerance = request.orientationTolerance;
        m_requests.push_back(rec);
        const std::size_t call = m_requests.size();

        kin::IkOutcome out;
        switch (m_script) {
        case Script::ConstantKind:
            out.outcomeKind = m_kind;
            break;
        case Script::ReachThenFacility:
            if (call == 1U) {
                out.outcomeKind = kin::IkOutcomeKind::SolutionsFound;
            } else {
                out.facilityFailure = kin::IkFacilityFailure{
                    kin::IkFacilityFailureStage::ResidualRecheck,
                    "替身注入的设施失败（ResidualRecheck——素材转载断言源）"};
                return out;  // 非终局形态——无结局语义
            }
            break;
        case Script::ReachThenCancel:
            if (call == 1U) {
                out.outcomeKind = kin::IkOutcomeKind::SolutionsFound;
            } else {
                out.cancelled = true;
                return out;  // 非终局——取消不是结局（§9.2）
            }
            break;
        }
        // 结局 presence 不变式（SolutionsFound 须非空解集——组装器同款）。
        if (out.outcomeKind == kin::IkOutcomeKind::SolutionsFound
            && out.solutionSet.solutions.empty()) {
            kin::KinematicSolution s;
            s.q = {0.1, -0.2, 0.3, 0.4, -0.5};  // rad（球壳 5 自由度）
            s.positionResidual = 1e-9;      // m
            s.orientationResidual = 1e-9;   // rad
            out.solutionSet.solutions.push_back(s);
        }
        return out;
    }

    const std::vector<RecordedRequest>& requests() const { return m_requests; }
    std::uint64_t solves() const { return m_requests.size(); }

private:
    Script m_script;
    kin::IkOutcomeKind m_kind;
    mutable std::vector<RecordedRequest> m_requests;
};

}  // namespace

// =====================================================================
// ACC1——射线边界 Rmax：默认 180 方向，球壳解析对照 1.8 m
// =====================================================================

TEST(KinEnvelope, Acc1_RayBoundaryDefault180SphericalShell_WP15T15)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-10"},
                  std::vector<std::string>{"AT-26"});

    const rt::CanonicalModel model = sphericalShellModel();
    TestView view(model);

    // 默认方向数（不覆写 query.directionCount——KIN-10 原文默认 180）。
    EnvelopeQuery query = makeQuery(nullptr, kin::kEnvelopeDirectionCountDefault);
    const kin::Expected<EnvelopeResult> result =
        kin::computeApproximateEnvelope(view, shellTcp(), query);
    ASSERT_TRUE(result.ok()) << "球壳模型计算不应走错误面";
    const EnvelopeResult& env = result.get();

    // 形状：方向数/记录列一一对应。
    ASSERT_EQ(env.directionCount, kin::kEnvelopeDirectionCountDefault);
    ASSERT_EQ(env.rays.size(), kin::kEnvelopeDirectionCountDefault);
    EXPECT_NEAR(env.reachUpperBound, 1.8, 1e-12)
        << "解析上界＝L1＋L2（m——推导见 sphericalShellModel 注；浮点累加"
           "序差异以 1e-12 容差断言）";

    // 逐方向：完成态与预算纪律；Rmax 断言分两层——
    //   a) 全方向恒 ≤ 解析上界 1.8 m（下估计纪律；NoSampleFound 态
    //      rMax 占位 0 同样满足）；
    //   b) 外包络主口径（max over rays）≥ 1.799 m——球壳真值 1.8 m 的
    //      1e-3 m 邻域（预算内 DLS 收敛精度，tol=1e-4 m）；
    //   c) 计数口径：≥160/180 方向 rMax ≥ 1.799 m——近极轴方向（斐波
    //      那芙螺旋 j≈0/179 处 dz→±1）在 Z 型基座下雅可比趋退化，DLS
    //      预算（4 初值×64 迭代）内可能不收敛→该方向按"搜索未果"记
    //      NoSampleFound 低置信态（KIN-P5/审查 A04 起"未找到"不再并入
    //      Computed 输出 rMax=0——本用例按两终态词表逐方向判读），这正是
    //      KIN-10"近似、非精确"的语义体现（近似根源 2：搜索未果不作
    //      不可行证明），用例如实按计数口径断言而非全方向强断言。
    const std::vector<rw::math::Vector3D<double>> goldenDirections =
        kin::generateDirections(kin::kEnvelopeDirectionCountDefault);
    std::uint32_t convergedRays = 0;
    double maxRmax = 0.0;
    for (std::uint32_t j = 0; j < env.rays.size(); ++j) {
        const EnvelopeRayRecord& ray = env.rays[j];
        ASSERT_TRUE(ray.state == EnvelopeRayState::Computed
                    || ray.state == EnvelopeRayState::NoSampleFound)
            << "方向 " << j << "：正常返回只有两终态";
        EXPECT_EQ(ray.directionIndex, j) << "方向序＝生成序（黄金锁定）";
        EXPECT_TRUE(ray.direction == goldenDirections[j])
            << "方向集复用 §7.2 黄金设施（方向 " << j << "）";
        EXPECT_LE(ray.rMax, 1.8) << "方向 " << j << "：下估计不超上界";
        EXPECT_GT(ray.probeCount, 0U) << "方向 " << j << "：预算素材非空";
        if (ray.state == EnvelopeRayState::Computed) {
            EXPECT_TRUE(ray.reason.empty()) << "Computed 态 reason 为空";
        } else {
            EXPECT_FALSE(ray.reason.empty())
                << "方向 " << j << "：NoSampleFound 必填原因";
        }
        if (ray.rMax >= 1.799) {
            ++convergedRays;
        }
        maxRmax = std::max(maxRmax, ray.rMax);
    }
    EXPECT_GE(maxRmax, 1.799) << "外包络主口径：最大射线边界逼近球壳真值";
    EXPECT_GE(convergedRays, 160U)
        << "收敛方向数=" << convergedRays << "/180（近极轴预算伪影——"
           "近似语义，非缺陷）";
    EXPECT_GT(env.probeCount, 0U);
    EXPECT_FALSE(env.cancelled);
    EXPECT_FALSE(env.facilityFailure.has_value());
}

// =====================================================================
// ACC1——可取消：中途取消（部分方向保留、未开始方向 NotRun）
// =====================================================================

TEST(KinEnvelope, Acc1_CancellableMidRun_WP15T15)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-10"},
                  std::vector<std::string>{"AT-26"});

    const rt::CanonicalModel model = sphericalShellModel();
    TestView view(model);

    // 探针：第 3 次查询起置真（首方向锚点/二分早期即中断——partial 面）。
    std::uint32_t calls = 0;
    EnvelopeQuery query = makeQuery(nullptr, 4U);
    query.cancellationProbe = [&calls]() { return ++calls > 3U; };

    const kin::Expected<EnvelopeResult> result =
        kin::computeApproximateEnvelope(view, shellTcp(), query);
    ASSERT_TRUE(result.ok());
    const EnvelopeResult& env = result.get();

    // 取消不是结局（D-KIN-9）：cancelled 载荷＋未开始方向 NotRun 非空
    // 原因（ERR-01）；已完成方向（若有）如实保留 Computed。
    EXPECT_TRUE(env.cancelled);
    EXPECT_FALSE(env.facilityFailure.has_value());
    ASSERT_EQ(env.rays.size(), 4U);
    bool sawNotRun = false;
    for (const EnvelopeRayRecord& ray : env.rays) {
        if (ray.state == EnvelopeRayState::NotRun) {
            sawNotRun = true;
            EXPECT_FALSE(ray.reason.empty()) << "NotRun 必填原因（方向 "
                                             << ray.directionIndex << "）";
        }
    }
    EXPECT_TRUE(sawNotRun) << "第 3 次探针即取消——至少尾随方向未开始";
}

// =====================================================================
// ACC1——可取消：入口取消（零探测返回——预算零消耗）
// =====================================================================

TEST(KinEnvelope, Acc1_CancellableAtEntry_WP15T15)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-10"},
                  std::vector<std::string>{"AT-26"});

    const rt::CanonicalModel model = sphericalShellModel();
    TestView view(model);

    EnvelopeQuery query = makeQuery(nullptr, 4U);
    query.cancellationProbe = []() { return true; };  // 入口即取消

    const kin::Expected<EnvelopeResult> result =
        kin::computeApproximateEnvelope(view, shellTcp(), query);
    ASSERT_TRUE(result.ok());
    const EnvelopeResult& env = result.get();

    EXPECT_TRUE(env.cancelled);
    EXPECT_EQ(env.probeCount, 0U) << "入口取消——零 IK 调用（预算零消耗）";
    ASSERT_EQ(env.rays.size(), 4U);
    for (const EnvelopeRayRecord& ray : env.rays) {
        EXPECT_EQ(ray.state, EnvelopeRayState::NotRun);
        EXPECT_FALSE(ray.reason.empty());
        EXPECT_EQ(ray.probeCount, 0U);
    }
}

// =====================================================================
// ACC1——取消经求解器内部探针返回（IkOutcome.cancelled 上调传播）
// =====================================================================

/**
 * @brief 脚本边界替身（KIN-P5 重构后取消/设施失败中断轨的载体——半径
 *        边界脚本保证方向需多次探测〔站点内扫＋二分〕，中断能落在方向
 *        进行中而非首个探针）。
 *
 * 行为：‖p‖ ≤ boundary 可达（SolutionsFound——presence 不变式自补），
 * 否则 MultiInitNoConvergence；cancelAfterN/facilityAfterN 分别在第 N
 * 次（1 起）调用起注入取消/设施失败（0＝不注入）。
 */
class BoundaryScriptSolver final : public kin::IIkSolver {
public:
    BoundaryScriptSolver(double boundary, std::uint32_t cancelAfterN,
                         std::uint32_t facilityAfterN)
        : m_boundary(boundary),
          m_cancelAfterN(cancelAfterN),
          m_facilityAfterN(facilityAfterN)
    {
    }

    kin::IkOutcome solve(const kin::IkRequest& request) const override
    {
        const std::uint64_t call = m_calls + 1;
        ++m_calls;

        kin::IkOutcome out;
        // 注入序：取消/设施失败优先（非终局形态——与真实求解器的
        // cancelled/facilityFailure 上调同构）。
        if (m_cancelAfterN != 0U && call >= m_cancelAfterN) {
            out.cancelled = true;
            return out;
        }
        if (m_facilityAfterN != 0U && call >= m_facilityAfterN) {
            out.facilityFailure = kin::IkFacilityFailure{
                kin::IkFacilityFailureStage::ResidualRecheck,
                "替身注入的设施失败（ResidualRecheck——素材转载断言源）"};
            return out;
        }
        if (request.targetInBase.P().norm2() <= m_boundary) {
            out.outcomeKind = kin::IkOutcomeKind::SolutionsFound;
            kin::KinematicSolution s;
            s.q = {0.0, 0.0, 0.0};
            out.solutionSet.solutions.push_back(s);
        } else {
            out.outcomeKind = kin::IkOutcomeKind::MultiInitNoConvergence;
            kin::IkSearchRecord record;
            out.solutionSet.searchRecord = record;
        }
        return out;
    }

    std::uint64_t calls() const { return m_calls; }

private:
    double m_boundary;                 ///< 可达半径边界（m，基座系 {B}）
    std::uint32_t m_cancelAfterN;      ///< 第 N 次调用起取消（0＝不注入）
    std::uint32_t m_facilityAfterN;    ///< 第 N 次调用起设施失败（0＝不注入）
    mutable std::uint64_t m_calls = 0; ///< solve 调用计数
};

TEST(KinEnvelope, Acc1_CancelPropagatedFromSolver_WP15T15)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-10"},
                  std::vector<std::string>{"AT-26"});

    const rt::CanonicalModel model = sphericalShellModel();
    TestView view(model);

    // 替身：半径边界 1.5 m（首方向需多站内扫＋多轮二分——中断落在方向
    // 进行中的二分阶段），第 4 次求解起返回 cancelled=true——包络层须按
    // 取消轨处置（非结局、停止全部、中断方向 NotRun 标记）。
    BoundaryScriptSolver solver(1.5, 4U, 0U);
    EnvelopeQuery query = makeQuery(&solver, 4U);
    const kin::Expected<EnvelopeResult> result =
        kin::computeApproximateEnvelope(view, shellTcp(), query);
    ASSERT_TRUE(result.ok());
    const EnvelopeResult& env = result.get();

    EXPECT_TRUE(env.cancelled);
    EXPECT_FALSE(env.facilityFailure.has_value());
    EXPECT_EQ(env.rays[0].state, EnvelopeRayState::NotRun)
        << "中断方向本身标记 NotRun（二分中断轨——KIN-P5 起首方向经站点"
           "内扫＋二分多探针，中断落在其进行中）";
    EXPECT_FALSE(env.rays[0].reason.empty());
    for (std::size_t j = 1; j < env.rays.size(); ++j) {
        EXPECT_EQ(env.rays[j].state, EnvelopeRayState::NotRun) << "方向 " << j;
        EXPECT_FALSE(env.rays[j].reason.empty());
    }
}

// =====================================================================
// ACC2——"近似、非精确"类型化标注（AT-26/§7.3 禁令随行）
// =====================================================================

TEST(KinEnvelope, Acc2_ApproximationMarking_WP15T15)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-10", "KIN-04"},
                  std::vector<std::string>{"AT-26"});

    const rt::CanonicalModel model = sphericalShellModel();
    TestView view(model);

    CountingSolver solver(CountingSolver::Script::ConstantKind);
    EnvelopeQuery query = makeQuery(&solver, 8U);
    query.identity.seed = 999U;  // 调用方误填——实现须以 query.seed 覆写
    const kin::Expected<EnvelopeResult> result =
        kin::computeApproximateEnvelope(view, shellTcp(), query);
    ASSERT_TRUE(result.ok());
    const EnvelopeResult& env = result.get();

    // 精度词表单值：恒 Approximate——"结果明确标注近似、非精确"的
    // 类型化落点（KIN-10 原文词）。
    EXPECT_EQ(env.precision, kin::EnvelopePrecision::Approximate);
    EXPECT_EQ(static_cast<std::uint8_t>(env.precision), 0U)
        << "单值词表（不存在'精确'形态——§7.3 边界的编译期承载）";

    // 固定文案随行：近似词＋非精确词＋两条 §7.3 禁令（不作 Must 判定
    // 依据、不替代样本全覆盖）。
    const std::string note(kin::kEnvelopeApproximationNote);
    EXPECT_NE(note.find("近似"), std::string::npos);
    EXPECT_NE(note.find("非精确"), std::string::npos);
    EXPECT_NE(note.find("Must"), std::string::npos);
    EXPECT_NE(note.find("样本全覆盖"), std::string::npos);

    // 结果身份回填：seed/referenceQ 由实现从 query 覆写（防两处漂移
    // ——调用方误填 identity.seed=999 被覆写为 query.seed）。
    EXPECT_EQ(env.identity.seed, query.seed);
    EXPECT_TRUE(env.identity.referenceQ == query.referenceQ);
    EXPECT_EQ(env.contractVersion, kin::kEnvelopeContractVersion);
}

// =====================================================================
// ACC3——方向集黄金锁定＋同种子重跑逐位一致（NFR-COR-01）
// =====================================================================

TEST(KinEnvelope, Acc3_DirectionSetGoldenAndRerunDeterminism_WP15T15)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-10"},
                  std::vector<std::string>{"NFR-COR-01", "NFR-COR-02"});

    const rt::CanonicalModel model = sphericalShellModel();
    TestView view(model);

    // 同种子重跑：结果逐字段相等（operator== 全字段——浮点逐位）。
    CountingSolver solverA(CountingSolver::Script::ConstantKind);
    EnvelopeQuery queryA = makeQuery(&solverA, 12U, 777U);
    const kin::Expected<EnvelopeResult> runA =
        kin::computeApproximateEnvelope(view, shellTcp(), queryA);
    CountingSolver solverB(CountingSolver::Script::ConstantKind);
    EnvelopeQuery queryB = makeQuery(&solverB, 12U, 777U);
    const kin::Expected<EnvelopeResult> runB =
        kin::computeApproximateEnvelope(view, shellTcp(), queryB);
    ASSERT_TRUE(runA.ok());
    ASSERT_TRUE(runB.ok());
    EXPECT_TRUE(runA.get() == runB.get())
        << "同 (view,tcp,query) 重跑逐位一致（NFR-COR-01）";

    // 方向集黄金锁定：逐方向与 §7.2 generateDirections 生成序全等
    // （几何值精确相等——同一黄金实现零漂移）。
    const std::vector<rw::math::Vector3D<double>> golden =
        kin::generateDirections(12U);
    ASSERT_EQ(runA.get().rays.size(), golden.size());
    for (std::size_t j = 0; j < golden.size(); ++j) {
        EXPECT_TRUE(runA.get().rays[j].direction == golden[j]) << "方向 " << j;
    }

    // 换种子：仍是合法结果（确定性来源切换——结果可不同，形状不变）。
    CountingSolver solverC(CountingSolver::Script::ConstantKind);
    EnvelopeQuery queryC = makeQuery(&solverC, 12U, 778U);
    const kin::Expected<EnvelopeResult> runC =
        kin::computeApproximateEnvelope(view, shellTcp(), queryC);
    ASSERT_TRUE(runC.ok());
    EXPECT_EQ(runC.get().directionCount, 12U);
    EXPECT_EQ(runC.get().rays.size(), 12U);
}

// =====================================================================
// 探针接线——位置存在性口径＋计数守恒＋脚本边界语义（替身面）
// =====================================================================

TEST(KinEnvelope, ProbeWiring_ExistenceCriterionAndBudgetConservation_WP15T15)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-10"},
                  std::vector<std::string>{"KIN-04"});

    const rt::CanonicalModel model = sphericalShellModel();
    TestView view(model);

    // 脚本边界替身：‖p‖ ≤ 1.5 m 可达、否则搜索未果（MultiInitNo-
    // Convergence——近似根源 2 的"当不可达侧使用"轨）。锚点 t=0.9 命中
    // 可达；二分边界收敛于 1.5 m。记录逐请求半径与任务空间模式（接线
    // 断言面——RegionCoverageTest ScriptedSolver 同款先例；KIN-P2/A02
    // 起存在性口径判别＝taskSpace，旧"姿态容差 π"判别随该机制移除）。
    class RadiusBoundarySolver final : public kin::IIkSolver {
    public:
        struct RecordedRequest {
            double probedRadius = 0.0;          ///< ‖p‖（m，基座系 {B}）
            double orientationTolerance = 0.0;  ///< rad（合法域载体——不参与判定）
            kin::IkTaskSpace taskSpace =
                kin::IkTaskSpace::FullPose;     ///< 任务维度（存在性口径判别）
        };

        mutable std::vector<RecordedRequest> requests;

        kin::IkOutcome solve(const kin::IkRequest& request) const override
        {
            const rw::math::Vector3D<double> p = request.targetInBase.P();
            RecordedRequest rec;
            rec.probedRadius = p.norm2();  // rw norm2＝欧氏 2-范数（m）
            rec.orientationTolerance = request.orientationTolerance;
            rec.taskSpace = request.taskSpace;
            requests.push_back(rec);

            kin::IkOutcome out;
            if (rec.probedRadius <= 1.5) {
                out.outcomeKind = kin::IkOutcomeKind::SolutionsFound;
                kin::KinematicSolution s;
                s.q = {0.0, 0.0, 0.0};
                out.solutionSet.solutions.push_back(s);
            } else {
                out.outcomeKind = kin::IkOutcomeKind::MultiInitNoConvergence;
                kin::IkSearchRecord recRecord;
                out.solutionSet.searchRecord = recRecord;
            }
            return out;
        }
    };
    RadiusBoundarySolver radiusSolver;

    EnvelopeQuery query = makeQuery(&radiusSolver, 4U);
    const kin::Expected<EnvelopeResult> result =
        kin::computeApproximateEnvelope(view, shellTcp(), query);
    ASSERT_TRUE(result.ok());
    const EnvelopeResult& env = result.get();

    // 边界语义：全部方向 rMax 收敛于脚本边界 1.5 m（下估计≤真值、
    // 二分分辨率 ≤ positionTolerance=1e-4——断言容差 1e-3 含预算裕量）。
    for (const EnvelopeRayRecord& ray : env.rays) {
        ASSERT_EQ(ray.state, EnvelopeRayState::Computed);
        EXPECT_LE(ray.rMax, 1.5) << "方向 " << ray.directionIndex;
        EXPECT_GE(ray.rMax, 1.5 - 1e-3) << "方向 " << ray.directionIndex;
    }

    // 存在性口径接线（KIN-P2/A02 迁移后）：全部探针＝位置存在性 IK
    // （taskSpace=PositionOnly——姿态项不进目标函数）；姿态容差为请求
    // 合法域载体（IkRequest 默认合法值，不参与本任务收敛判定——断言
    // >0 有限即可，恒 π 的旧钉随 kEnvelopePoseFreeTolerance 移除废除）。
    ASSERT_GT(radiusSolver.requests.size(), 0U);
    for (const auto& rec : radiusSolver.requests) {
        EXPECT_EQ(rec.taskSpace, kin::IkTaskSpace::PositionOnly)
            << "包络位置探针必须以仅位置任务维度提交（A02 迁移钉）";
        EXPECT_GT(rec.orientationTolerance, 0.0);
        EXPECT_TRUE(std::isfinite(rec.orientationTolerance));
    }
    // 探测点全部在解析上界内（hi 侧解析免探测——预算不越过 Rup；
    // 1e-9 m 裕量吸收单位向量模长的 1±ε 浮点噪声）。
    for (const auto& rec : radiusSolver.requests) {
        EXPECT_LE(rec.probedRadius, 1.8 + 1e-9)
            << "探测半径超出解析上界（预算越界）";
    }

    // 计数守恒：Σ逐方向 probeCount＝全局 probeCount＝替身 solve 次数。
    std::uint64_t raySum = 0;
    for (const EnvelopeRayRecord& ray : env.rays) {
        raySum += ray.probeCount;
    }
    EXPECT_EQ(raySum, env.probeCount);
    EXPECT_EQ(env.probeCount, radiusSolver.requests.size());
}

// =====================================================================
// KIN-P2（审查 A02 验收驳回补迁移）——包络位置探针的任务维度行为钉
// =====================================================================

/**
 * @brief 包络探针必须以 PositionOnly 任务维度发起位置存在性判定（行为
 *        级迁移钉——非仅字段透传）。
 *
 * 替身模拟"位置实心可达、姿态自由度不足"的链（审查 A02 形态）：仅当
 * 请求以 taskSpace=PositionOnly（位置存在性）提交时按 ‖p‖≤1.5 判可达；
 * FullPose 请求（姿态恒等目标）一律搜索未果——即姿态项牵制位置解的
 * 缺陷形态。包络必须以 PositionOnly 探针使 rMax 收敛到脚本边界 1.5 m；
 * 若实现回退旧"π 容差/FullPose"机制（迁移回退），全部探针搜索未果、
 * 锚点耗尽 → rMax=0——本用例红。修复前（2c163349 主提交漏迁移包络
 * 通道）即此红形态；这是探针级可观测差异的锚点——真实几何端到端的
 * rMax 差异在退化链上会被审查 A04 的锚点未命中机制（位置可达集为
 * 环/壳时内锚点全灭）掩盖，故以脚本边界承载行为差异（与 RadiusBoundary
 * 替身同族口径）。
 */
TEST(KinEnvelope, PositionProbeUsesTaskSpaceSemantics_KIN_P2)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-10", "KIN-02", "KIN-04"},
                  std::vector<std::string>{});

    const rt::CanonicalModel model = sphericalShellModel();
    TestView view(model);

    // 姿态受限链替身：PositionOnly（位置存在性）→ ‖p‖≤1.5 可达；
    // FullPose → 一律搜索未果（姿态自由度不足的 A02 缺陷形态模拟）。
    // 记录 taskSpace 供迁移面断言。
    class PositionOnlyBoundarySolver final : public kin::IIkSolver {
    public:
        mutable int positionOnlyCalls = 0;   ///< PositionOnly 请求计数
        mutable int fullPoseCalls = 0;       ///< FullPose 请求计数（期望 0）

        kin::IkOutcome solve(const kin::IkRequest& request) const override
        {
            const rw::math::Vector3D<double> p = request.targetInBase.P();
            kin::IkOutcome out;
            if (request.taskSpace == kin::IkTaskSpace::PositionOnly) {
                ++positionOnlyCalls;
                if (p.norm2() <= 1.5) {
                    out.outcomeKind = kin::IkOutcomeKind::SolutionsFound;
                    kin::KinematicSolution s;
                    s.q = {0.0, 0.0, 0.0};
                    out.solutionSet.solutions.push_back(s);
                    return out;
                }
            } else {
                ++fullPoseCalls;  // 迁移回退即非零——行为钉的失败面
            }
            out.outcomeKind = kin::IkOutcomeKind::MultiInitNoConvergence;
            kin::IkSearchRecord record;
            out.solutionSet.searchRecord = record;
            return out;
        }
    };
    PositionOnlyBoundarySolver solver;

    EnvelopeQuery query = makeQuery(&solver, 4U);
    const kin::Expected<EnvelopeResult> result =
        kin::computeApproximateEnvelope(view, shellTcp(), query);
    ASSERT_TRUE(result.ok());
    const EnvelopeResult& env = result.get();

    // 行为差异面：rMax 收敛于位置存在性边界 1.5 m（迁移回退→rMax=0）。
    ASSERT_EQ(env.rays.size(), 4U);
    for (const EnvelopeRayRecord& ray : env.rays) {
        ASSERT_EQ(ray.state, EnvelopeRayState::Computed) << "方向 "
                                                         << ray.directionIndex;
        EXPECT_LE(ray.rMax, 1.5) << "方向 " << ray.directionIndex;
        EXPECT_GE(ray.rMax, 1.5 - 1e-3)
            << "方向 " << ray.directionIndex
            << "（rMax=0＝位置存在性探针被姿态项牵制的迁移回退形态）";
    }
    // 迁移面：全部探针 PositionOnly、零 FullPose 泄漏。
    EXPECT_GT(solver.positionOnlyCalls, 0);
    EXPECT_EQ(solver.fullPoseCalls, 0)
        << "包络探针不得以完整位姿任务发起位置存在性判定（A02）";
}

// =====================================================================
// 设施失败——非终局轨（素材转载＋停止全部＋NotRun 标记）
// =====================================================================

TEST(KinEnvelope, FacilityFailureNonTerminalStopsAll_WP15T15)
{
    // 素材转载语义溯源 findings F-746③（设施失败转值面——非终局形态）。
    IRD_TEST_INFO(std::vector<std::string>{"KIN-10"}, std::vector<std::string>{});

    const rt::CanonicalModel model = sphericalShellModel();
    TestView view(model);

    // 替身：半径边界 1.5 m（首方向需多站内扫＋多轮二分），第 4 次求解
    // 起设施失败——包络层须按非终局轨处置（无结局、无工程结论、素材
    // 转载、剩余方向 NotRun）。
    BoundaryScriptSolver solver(1.5, 0U, 4U);
    EnvelopeQuery query = makeQuery(&solver, 4U);
    const kin::Expected<EnvelopeResult> result =
        kin::computeApproximateEnvelope(view, shellTcp(), query);
    ASSERT_TRUE(result.ok()) << "设施失败走值面——不抛、不走错误面";
    const EnvelopeResult& env = result.get();

    EXPECT_FALSE(env.cancelled) << "设施失败≠取消（两轨分立）";
    ASSERT_TRUE(env.facilityFailure.has_value());
    // 素材转载断言：stage/detail 逐字来自底层 outcome（不加工）。
    EXPECT_EQ(env.facilityFailure->stage,
              kin::IkFacilityFailureStage::ResidualRecheck);
    EXPECT_NE(env.facilityFailure->detail.find("替身注入的设施失败"),
              std::string::npos);

    ASSERT_EQ(env.rays.size(), 4U);
    EXPECT_EQ(env.rays[0].state, EnvelopeRayState::NotRun) << "中断方向本身";
    EXPECT_FALSE(env.rays[0].reason.empty());
    for (std::size_t j = 1; j < env.rays.size(); ++j) {
        EXPECT_EQ(env.rays[j].state, EnvelopeRayState::NotRun) << "方向 " << j;
        EXPECT_FALSE(env.rays[j].reason.empty()) << "NotRun 必填原因";
    }
}

// =====================================================================
// KIN-P5（审查 A04）——未找到可达样本＝NoSampleFound 低置信态
// （旧"近似空结论"Computed＋rMax=0 的确定结论形态废除）
// =====================================================================

TEST(KinEnvelope, NoSampleFoundLowConfidenceState_KIN_P5)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-10"},
                  std::vector<std::string>{"AT-26"});

    // 平面二连杆（KinFkFixture 黄金算例 A——工作空间在 z=0 平面）；
    // 斐波那契螺旋方向集（N=偶数）无精确赤道方向——全部方向 dz≠0，
    // 脚本求解器恒搜索未果（模拟数值搜索全灭——审查 A04 的"未找到"
    // 形态）→ 全部方向 NoSampleFound 低置信态。
    const rt::CanonicalModel model = twoLinkModel();
    TestView view(model);

    CountingSolver solver(CountingSolver::Script::ConstantKind,
                          kin::IkOutcomeKind::MultiInitNoConvergence);
    EnvelopeQuery query = makeQuery(&solver, 8U);
    query.referenceQ = {0.0, 0.0};  // rad（二连杆 2 自由度——D-KIN-4）
    const kin::Expected<EnvelopeResult> result =
        kin::computeApproximateEnvelope(view, shellTcp(), query);
    ASSERT_TRUE(result.ok());
    const EnvelopeResult& env = result.get();

    ASSERT_EQ(env.rays.size(), 8U);
    for (const EnvelopeRayRecord& ray : env.rays) {
        // 诚实性断言（审查 A04 验收锚）：站点全灭＝NoSampleFound——
        // 绝不回退为 Computed＋rMax=0 的确定结论（迁移回退即本用例红）。
        EXPECT_EQ(ray.state, EnvelopeRayState::NoSampleFound)
            << "方向 " << ray.directionIndex
            << "：未找到可达样本必须是 NoSampleFound 低置信态";
        EXPECT_DOUBLE_EQ(ray.rMax, 0.0) << "rMax 占位（本态无语义）";
        EXPECT_FALSE(ray.reason.empty())
            << "NoSampleFound 必填原因（低置信语义随文案显式携带）";
        EXPECT_NE(ray.reason.find("未找到可达样本"), std::string::npos)
            << "方向 " << ray.directionIndex << "：原因须明示'未找到'而非"
                                               "确定性不可达";
        EXPECT_NE(ray.reason.find("不构成"), std::string::npos)
            << "原因须显式声明不构成确定性不可达结论（C5/C8 措辞纪律）";
        // 预算素材：全部站点已探测（非零消耗）。
        EXPECT_GT(ray.probeCount, 0U) << "方向 " << ray.directionIndex;
    }
    EXPECT_FALSE(env.cancelled);
    EXPECT_FALSE(env.facilityFailure.has_value());
}

// =====================================================================
// KIN-P5（审查 A04 反例 1）——退化链上界点先行试探：圆形工作空间
// +X 最外可达 1 m 必须命中（修复前 Rmax=0 的漏估形态钉死）
// =====================================================================

TEST(KinEnvelope, DegenerateChainUpperBoundProbeFirst_KIN_P5)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-10", "KIN-02"},
                  std::vector<std::string>{});

    // 审查 A04 反例几何：六轴同轴退化链（coaxialChainModel——TCP 距轴
    // 1 m，解析工作空间＝xy 平面半径 1 m 的圆；+X 最外可达点 (1,0,0)
    // 由零位构型 q=0 精确到达）。旧算法两处叠加漏估：①上界点 (Rup=
    // 1 m) 被当作"已证不可达"免探测（错误表述——三角形不等式取等时
    // 上界点恰可达）；②内锚点 t=0.5 处初值 q=0 的雅可比秩 1（切向），
    // 径向目标一阶不可达→锚点全灭→rMax=0。新算法上界站先行试探，
    // 零位即精确命中→rMax=Rup 直接成立。
    const rt::CanonicalModel model = coaxialChainModel();
    TestView view(model);

    kin::TcpRef tcp;
    tcp.toolObject = model.tools().front().objectId;

    EnvelopeQuery query;  // 审查探针同款预算（ReferenceQ 单初值——
    query.referenceQ = std::vector<double>(6, 0.0);  // rad（6 自由度）
    query.initialStrategy = kin::InitialValueStrategy::ReferenceQ;
    query.initialValuesCount = 1U;
    query.iterationLimit = 64U;
    query.positionTolerance = 1e-6;      // m
    query.dedupThresholdPerAxis = 1e-6;  // rad
    query.directionCount = 1U;           // 单方向＝+X（generateDirections(1) 黄金退化）
    query.seed = 1U;
    query.solver = nullptr;  // 真实内置求解器（数值路径）

    const kin::Expected<EnvelopeResult> result =
        kin::computeApproximateEnvelope(view, tcp, query);
    ASSERT_TRUE(result.ok());
    const EnvelopeResult& env = result.get();
    ASSERT_EQ(env.rays.size(), 1U);

    EXPECT_NEAR(env.reachUpperBound, 1.0, 1e-12) << "解析上界＝TCP 偏置 1 m";
    EXPECT_EQ(env.rays[0].state, EnvelopeRayState::Computed)
        << "上界站命中——Computed 态（修复前：锚点全灭→rMax=0）";
    EXPECT_NEAR(env.rays[0].rMax, 1.0, 1e-4)
        << "审查 A04 反例：圆形工作空间 +X 最外可达 1 m 必须被估算命中";
    EXPECT_GT(env.rays[0].probeCount, 0U);
    EXPECT_TRUE(env.rays[0].reason.empty());
}

// =====================================================================
// KIN-P5（审查 A04 反例 2）——窄环带（不含上界点）站点扫描命中：
// 折叠二连杆可达带 [0.40, 0.474] m、Rup=1.1 m，修复前锚点（0.55/0.275/
// …）全灭输出 Rmax=0
// =====================================================================

TEST(KinEnvelope, NarrowBandAwayFromUpperBoundFound_KIN_P5)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-10"},
                  std::vector<std::string>{});

    // 本文件自持窄带几何：平面二连杆、j2 折叠限位 [π−0.5, π+0.5]（rad）
    // ——可达集 |p| ∈ [0.4, 0.4736]（解析：|p|²＝0.75²＋0.35²＋2·0.75·
    // 0.35·cos q2，cos q2 ∈ [−1, −cos 0.5=−0.87758]）；解析上界 Rup＝
    // 0.75＋0.35＝1.1 m（三角形不等式——**不在**可达带内）。旧几何
    // 锚点序列 t=0.55, 0.275, … 全部落在带外→rMax=0（A04 漏估）；新
    // 等差站点分辨率 1.1/32≈0.0344 m＜带宽 0.0736 m→j=13 站（0.4469）
    // 命中，二分收敛于带外缘 0.4736。
    constexpr double kL1 = 0.75;  // m
    constexpr double kL2 = 0.35;  // m
    constexpr double kFold = 0.5;  // rad（j2 折叠半宽——带外缘决定量）
    std::vector<rt::CanonicalJoint> joints;
    joints.push_back(revolute("b1", rw::math::Vector3D<double>(0, 0, 1),
                              trans(0, 0, 0), -2.97, 2.97));
    joints.push_back(revolute("b2", rw::math::Vector3D<double>(0, 0, 1),
                              trans(kL1, 0, 0), 3.14159265358979323846 - kFold,
                              3.14159265358979323846 + kFold));
    const rt::CanonicalModel model = makeModel(joints, trans(kL2, 0, 0));
    TestView view(model);

    kin::TcpRef tcp;
    tcp.toolObject = model.tools().front().objectId;

    EnvelopeQuery query;
    query.referenceQ = {0.0, 3.14159265358979323846};  // rad（折叠构型参考）
    query.initialStrategy = kin::InitialValueStrategy::SeededRandom;
    query.initialValuesCount = 24U;  // 多初值降局部极小（黄金覆盖同款预算）
    query.iterationLimit = 400U;
    query.positionTolerance = 1e-4;      // m（估算预算——文件头注说明）
    query.dedupThresholdPerAxis = 1e-6;  // rad
    query.directionCount = 1U;           // 单方向＝+X
    query.seed = 20261016U;
    query.solver = nullptr;

    const kin::Expected<EnvelopeResult> result =
        kin::computeApproximateEnvelope(view, tcp, query);
    ASSERT_TRUE(result.ok());
    const EnvelopeResult& env = result.get();
    ASSERT_EQ(env.rays.size(), 1U);

    const double kBandOuter =
        std::sqrt(kL1 * kL1 + kL2 * kL2
                  + 2.0 * kL1 * kL2 * (-std::cos(kFold)));  // m（0.4736）
    EXPECT_NEAR(env.reachUpperBound, 1.1, 1e-12) << "解析上界＝L1＋L2";
    EXPECT_EQ(env.rays[0].state, EnvelopeRayState::Computed)
        << "窄带被站点扫描命中——Computed 态（修复前：锚点全灭→rMax=0）";
    EXPECT_GE(env.rays[0].rMax, kBandOuter - 0.003)
        << "下估计须逼窄带外缘（下界）；实际 " << env.rays[0].rMax;
    EXPECT_LE(env.rays[0].rMax, kBandOuter + 1e-3)
        << "下估计不越带外缘（可达性凭据纪律——不允许超出脚本可达带）";
}

// =====================================================================
// KIN-P5（审查 A06）——包络探针初值种子的完整 64 位敏感：seed=1 与
// seed=2³²＋1 的同序号探针初值必须不同（修复前 probeSeedFor 在模 2⁶⁴
// 下两 seed 状态相同→初值序列相同）
// =====================================================================

TEST(KinEnvelope, ProbeSeedFull64BitSensitivity_A06_KIN_P5)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-10", "KIN-13", "NFR-COR-01"},
                  std::vector<std::string>{});

    // 初值记录替身：捕获每请求的初值矩阵（探针种子敏感性的直接观测面
    // ——包络把 makeInitialValues 的产物原样透传 IK 请求）。
    class InitRecordingSolver final : public kin::IIkSolver {
    public:
        mutable std::vector<std::vector<std::vector<double>>> seen;
        kin::IkOutcome solve(const kin::IkRequest& request) const override
        {
            seen.push_back(request.initialValues);
            kin::IkOutcome out;
            out.outcomeKind = kin::IkOutcomeKind::SolutionsFound;
            kin::KinematicSolution s;
            s.q = {0.0, 0.0, 0.0, 0.0, 0.0};
            out.solutionSet.solutions.push_back(s);
            return out;
        }
    };

    const rt::CanonicalModel model = sphericalShellModel();
    TestView view(model);

    // 同一查询面只差 seed 高 32 位（1 与 1＋2³²——审查 A06 反例对）：
    // 各跑单方向包络，首个探针（上界站）的初值矩阵必须不同。
    InitRecordingSolver solverLow;
    EnvelopeQuery queryLow = makeQuery(&solverLow, 1U, 1U);
    const kin::Expected<EnvelopeResult> runLow =
        kin::computeApproximateEnvelope(view, shellTcp(), queryLow);
    ASSERT_TRUE(runLow.ok());
    ASSERT_GT(solverLow.seen.size(), 0U);

    InitRecordingSolver solverHigh;
    EnvelopeQuery queryHigh = makeQuery(&solverHigh, 1U, 1U + (1ULL << 32));
    const kin::Expected<EnvelopeResult> runHigh =
        kin::computeApproximateEnvelope(view, shellTcp(), queryHigh);
    ASSERT_TRUE(runHigh.ok());
    ASSERT_GT(solverHigh.seen.size(), 0U);

    ASSERT_EQ(solverLow.seen[0].size(), solverHigh.seen[0].size());
    EXPECT_NE(solverLow.seen[0], solverHigh.seen[0])
        << "审查 A06：seed 高 32 位必须参与探针初值派生（修复前两 seed "
           "同序号探针初值相同——多样性来源被静默丢弃）";

    // 同种子重跑逐位一致（确定性不因混合派生破坏——NFR-COR-01）。
    InitRecordingSolver solverLow2;
    EnvelopeQuery queryLow2 = makeQuery(&solverLow2, 1U, 1U);
    const kin::Expected<EnvelopeResult> runLow2 =
        kin::computeApproximateEnvelope(view, shellTcp(), queryLow2);
    ASSERT_TRUE(runLow2.ok());
    EXPECT_EQ(solverLow.seen[0], solverLow2.seen[0])
        << "同 seed 同序号探针初值逐位一致（可复现）";
}

// =====================================================================
// fail-fast 校验矩阵（调用方契约违约——invalid_argument，NFR-COR-03）
// =====================================================================

TEST(KinEnvelope, FailFastValidationMatrix_WP15T15)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-10", "KIN-13"},
                  std::vector<std::string>{"NFR-COR-03"});

    const rt::CanonicalModel model = sphericalShellModel();
    TestView view(model);

    // 基线合法查询（逐项覆写成非法——每项独立可辨）。
    const auto expectReject = [&](EnvelopeQuery query, const char* what) {
        EXPECT_THROW(kin::computeApproximateEnvelope(view, shellTcp(), query),
                     std::invalid_argument)
            << "应拒绝：" << what;
    };

    {
        EnvelopeQuery q = makeQuery(nullptr, 4U);
        q.directionCount = 0U;
        expectReject(q, "directionCount=0");
    }
    {
        EnvelopeQuery q = makeQuery(nullptr, 4U);
        q.seed = 0U;
        expectReject(q, "seed=0（I-KIN-4——拒绝不做 0→1 替换）");
    }
    {
        EnvelopeQuery q = makeQuery(nullptr, 4U);
        q.anchorProbesMax = 0U;
        expectReject(q, "anchorProbesMax=0");
    }
    {
        EnvelopeQuery q = makeQuery(nullptr, 4U);
        q.bisectIterationsMax = 0U;
        expectReject(q, "bisectIterationsMax=0");
    }
    {
        EnvelopeQuery q = makeQuery(nullptr, 4U);
        q.initialValuesCount = 0U;
        expectReject(q, "initialValuesCount=0");
    }
    {
        EnvelopeQuery q = makeQuery(nullptr, 4U);
        q.iterationLimit = 0U;
        expectReject(q, "iterationLimit=0");
    }
    {
        EnvelopeQuery q = makeQuery(nullptr, 4U);
        q.positionTolerance = 0.0;
        expectReject(q, "positionTolerance=0");
    }
    {
        EnvelopeQuery q = makeQuery(nullptr, 4U);
        q.positionTolerance = std::numeric_limits<double>::quiet_NaN();
        expectReject(q, "positionTolerance=NaN");
    }
    {
        EnvelopeQuery q = makeQuery(nullptr, 4U);
        q.dedupThresholdPerAxis = -1.0;
        expectReject(q, "dedupThresholdPerAxis<0");
    }
    {
        EnvelopeQuery q = makeQuery(nullptr, 4U);
        q.referenceQ = {0.0, 0.0};  // 维度≠设备自由度 3
        expectReject(q, "referenceQ 维度不符（D-KIN-4 契约）");
    }
    {
        EnvelopeQuery q = makeQuery(nullptr, 4U);
        q.referenceQ = {0.0, 0.0,
                        std::numeric_limits<double>::infinity()};
        expectReject(q, "referenceQ 非有限分量");
    }
}

// =====================================================================
// Expected 错误面——TCP 未配置（NoTcp——§9.6 两分口径）
// =====================================================================

TEST(KinEnvelope, NoTcpErrorFace_WP15T15)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-10"},
                  std::vector<std::string>{"KIN-13"});

    // 无工具模型（KIN-NO-TCP"未配置"分支——makeModel withTool=false）。
    constexpr double L1 = 1.1;
    constexpr double L2 = 0.7;
    std::vector<rt::CanonicalJoint> joints;
    joints.push_back(revolute("n1", rw::math::Vector3D<double>(0, 0, 1),
                              trans(0, 0, 0), -3.14, 3.14));
    joints.push_back(revolute("n2", rw::math::Vector3D<double>(0, 1, 0),
                              trans(0, 0, 0), -3.14, 3.14));
    joints.push_back(revolute("n3", rw::math::Vector3D<double>(0, 1, 0),
                              trans(L1, 0, 0), -3.14, 3.14));
    const rt::CanonicalModel model = makeModel(joints, trans(L2, 0, 0),
                                               trans(0, 0, 0), false);
    TestView view(model);

    EnvelopeQuery query = makeQuery(nullptr, 4U);
    query.referenceQ = {0.0, 0.0, 0.0};  // rad（本模型 3 自由度——无腕部）
    const kin::Expected<EnvelopeResult> result =
        kin::computeApproximateEnvelope(view, shellTcp(), query);
    ASSERT_FALSE(result.ok()) << "TCP 未配置走 Expected 错误面";
    EXPECT_EQ(result.error().code, kin::KinematicsErrorCode::NoTcp);
}
