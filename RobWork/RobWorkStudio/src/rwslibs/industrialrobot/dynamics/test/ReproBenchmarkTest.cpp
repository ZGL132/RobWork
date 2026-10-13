/**
 * @file   ReproBenchmarkTest.cpp
 * @brief  dynamics 域确定性复现基准用例（WP-23-T09 抽样——B-08）：逆动力学
 *         评估（InverseDynamicsEvaluator——RNEA）同配置复跑的等价集合＋
 *         稳定排序（逐时刻逐关节样本序——§4.6 排序纪律；NFR-COR-02）。
 *
 * 设计依据：
 *   - units/dynamics.md §4.6（样本排序纪律——逐时刻逐关节确定性行序）＋
 *     §10.1（评估请求面）
 *   - doc/industrial-robot-design/performance-baseline.md §2 B-08 行＋§4.6
 *     （不要求浮点逐字节相同——对照容差经 repro-reduction 档案）
 *   - 需求 NFR-COR-02；任务契约 tasks/foundation/WP-23-T09.json
 *
 * 抽样说明（各域抽样复现——本域取样点）：逆动力学评估是本域核心数值
 * 通道（RNEA 递推——逐样本独立计算，无跨样本归约）。本域无并行分片与
 * 随机种子入口——线程等价腿不适用，如实只跑复跑腿；报告如实记录两腿。
 *
 * rw 依赖形态：仅 rw::math 模板类（Vector3D——头文件实例化）——RneaTest
 * 同款，冒烟模式可编译运行（框架非模板符号零消费）。
 */

#include "TwoLinkFixture.hpp"   // makeTwoLinkModel/groundWorld/idFrom（域内夹具）

#include <sdurws/ird/dynamics/DynTypes.hpp>
#include <sdurws/ird/dynamics/InverseDynamics.hpp>
#include <sdurws/ird/evidence/Evaluator.hpp>
#include <sdurws/ird/runtime/BaseWorldTransform.hpp>   // gravityToBase——重力投影规则单点
#include <sdurws/ird/runtime/CanonicalModel.hpp>
#include <sdurws/ird/runtime/Description.hpp>
#include <sdurws/ird/testkit/ReproBenchmark.hpp>
#include <sdurws/ird/testkit/ToleranceProfile.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

namespace dyn = sdurws::ird::dynamics;
namespace rt = sdurws::ird::runtime;
namespace tk = sdurws::ird::testkit;
using sdurws::ird::core::ObjectId;
using dyn::InverseDynRequest;
using dyn::InverseDynSampleInput;

// =====================================================================
// 元素投影（DynamicsSample → SetMatchTraits 元素——§5.4.5 适配归域）。
// 元素与 traits 特化置于全局作用域（SetCheckTest 消费示范同款——特化须
// 在主模板的可达外围命名空间声明）。
// =====================================================================

/// 样本元素：身份＝(t, segmentIndex, jointIndex) 规范文本（行序键——§4.6
/// 排序纪律的投影）；数值＝关节总力矩（N·m，RNEA 逐项输出）。
struct ReproDynItem {
    std::string id;      ///< "t<时间>|seg<段>|j<关节>"（样本行身份）
    double tauTotal = 0.0;   ///< 关节总力矩，单位 N·m
};

namespace sdurws::ird::testkit {
template <>
struct SetMatchTraits<ReproDynItem> {
    static std::optional<std::string> identity(const ReproDynItem& e)
    {
        return e.id;
    }
    static std::vector<NumericFieldView> numerics(const ReproDynItem& e)
    {
        return {{"repro.torque", e.tauTotal}};
    }
};
}  // namespace sdurws::ird::testkit

namespace {

/// repro-reduction 档案（B-08 数值唯一来源——与 testkit 元测试同一文件）。
const tk::ToleranceProfile& reproProfile()
{
    static const tk::ToleranceProfile profile = tk::ToleranceProfile::load(
        tk::toleranceProfileDir("repro-reduction") / "v1.0.0.json");
    return profile;
}

/// 激励样本（正弦解析形态——q/q̇/q̈ 列由固定字面量参数派生，无随机）。
InverseDynSampleInput sampleAt(double t, double phase, std::uint32_t segment)
{
    InverseDynSampleInput s;
    s.t = t;                                   // s
    s.segmentIndex = segment;
    // 三关节激励（二连杆模型 2 自由度——第三列被域校验拒绝，故只用两列；
    // 相位字面量派生——确定性输入，同参数同序列）。
    s.q = {0.3 * std::sin(phase), -0.2 * std::cos(phase)};                // rad
    s.qd = {0.3 * std::cos(phase), 0.2 * std::sin(phase)};                // rad/s
    s.qdd = {-0.3 * std::sin(phase), 0.2 * std::cos(phase)};              // rad/s²
    return s;
}

/// 评估执行（RneaTest 请求组装同款——重力投影经 runtime 规则函数单点）。
dyn::InverseDynOutcome evaluateOnce(const rt::CanonicalModel& model,
                                    const ObjectId& condition,
                                    const std::vector<InverseDynSampleInput>& samples)
{
    InverseDynRequest req;
    req.conditionId = condition;
    req.model = &model;
    const rw::math::Vector3D<double> gBase =
        rt::gravityToBase(model.world().T_world_base.R(),
                          model.world().gravityWorld);
    req.gravityBase[0] = gBase[0];
    req.gravityBase[1] = gBase[1];
    req.gravityBase[2] = gBase[2];
    req.samples = samples;

    class Ctx final : public sdurws::ird::evidence::IEvaluationContext {
    public:
        bool cancellationRequested() const override { return false; }
        void reportProgress(std::uint8_t, std::string_view) override {}
        std::optional<std::vector<std::uint8_t>>
        tryObjectBytes(ObjectId, sdurws::ird::core::ContentVersion) const override
        {
            return std::nullopt;
        }
    } ctx;
    return dyn::InverseDynamicsEvaluator().evaluate(req, ctx);
}

/// 逐样本行投影（输出行序＝§4.6 确定性行序——逐时刻逐关节）。
std::vector<ReproDynItem> projectSamples(const dyn::InverseDynOutcome& out)
{
    std::vector<ReproDynItem> items;
    items.reserve(out.samples.size());
    for (const dyn::DynamicsSample& s : out.samples) {
        ReproDynItem e;
        e.id = "t" + std::to_string(s.t) + "|seg"
            + std::to_string(s.segmentIndex) + "|j"
            + std::to_string(s.jointIndex);
        e.tauTotal = s.tauTotal;
        items.push_back(std::move(e));
    }
    return items;
}

}  // namespace

/**
 * dynamics 域复现基准（B-08 抽样）：平面二连杆（域内夹具）×40 采样点
 * 正弦激励逆动力学——同输入复跑等价集合＋稳定排序。
 */
TEST(DynReproBenchmark, InverseDynRerunEquivalentSetAndOrder_WP23T09_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-02"},
                  std::vector<std::string>{"AT-37"});

    // ---- 确定性输入装配（夹具直构模型＋字面量激励——零随机）----
    const rt::CanonicalModel model =
        dyn::testfixture::makeTwoLinkModel(dyn::testfixture::groundWorld(),
                                           dyn::testfixture::FrictionSpec{},
                                           std::nullopt);
    const ObjectId condition = dyn::testfixture::idFrom<ObjectId>("dyn-repro-cond");

    constexpr int kSampleCount = 40;
    std::vector<InverseDynSampleInput> samples;
    samples.reserve(kSampleCount);
    for (int i = 0; i < kSampleCount; ++i) {
        samples.push_back(sampleAt(i * 0.01, 0.1 * i,   // s；rad 相位——确定性派生
                                   static_cast<std::uint32_t>(i / 20)));
    }

    // ---- 复现用例执行（两腿：reference/rerun——线程腿不适用）----
    tk::ReproCaseSpec spec;
    spec.caseId = "dyn-inverse-rerun";
    spec.domain = "dynamics";
    spec.inputProvenance =
        "twoLinkFixture(40 sinusoidal samples, 2 dof, gravity via "
        "gravityToBase, no friction model)";
    spec.repro.seed = 20260909;      // 无随机消费域——记录默认口径
    spec.repro.threadCount = 1;
    spec.parallelThreadCount = 0;    // RNEA 逐样本独立——线程腿不适用

    const tk::ReproCaseReport report = tk::runReproCase<ReproDynItem>(
        spec, reproProfile(), tk::SetMatchTraits<ReproDynItem>{},
        [&](std::uint32_t) {
            return projectSamples(evaluateOnce(model, condition, samples));
        });

    // 判定（NFR-COR-02：等价集合＋稳定排序——本域抽样面）。
    ASSERT_EQ(report.verdicts.size(), 1U);
    EXPECT_TRUE(report.verdicts.front().setEquivalent);
    EXPECT_TRUE(report.verdicts.front().sameOrder);
    EXPECT_TRUE(report.passed) << (report.violations.empty()
        ? "" : report.violations.front());
    // 元素面：40 采样×2 关节＝80 行（样本行序完整性如实留痕）。
    EXPECT_EQ(report.legs.front().elementCount,
              static_cast<std::size_t>(kSampleCount) * 2U);

    // 报告拖挂（IRD_REPRO_REPORT_DIR 设置时落盘——基准执行留痕）。
    if (const auto dir = tk::reproReportDirFromEnv()) {
        EXPECT_TRUE(tk::writeReproReportJson(*dir, report));
    }
}
