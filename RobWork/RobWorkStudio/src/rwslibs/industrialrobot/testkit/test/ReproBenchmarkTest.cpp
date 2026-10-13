/**
 * @file   ReproBenchmarkTest.cpp
 * @brief  确定性复现基准夹具用例组（TK-REPRO）——三腿执行/注入违例检出/
 *         报告序列化（WP-23-T09 夹具本体的元测试：夹具失败能力证明）。
 *
 * 设计依据：
 *   - units/testkit.md §5.4（SetCheck 判定语义——本夹具委托不重实现）、
 *     §6.3（ReproRecord/DeterministicEnv——确定性上下文唯一来源）、
 *     §10.1"性能基准夹具"行（WP-23 消费者提案落位——WP-23-T09）
 *   - doc/industrial-robot-design/performance-baseline.md §2 B-08 行＋§4.6
 *     （归约容差经 repro-reduction 档案承载——本测试消费同一档案文件，
 *     与五域复现用例同一数值来源）
 *   - 需求 NFR-COR-02；任务契约 tasks/foundation/WP-23-T09.json
 *     （验收"各域抽样复现用例通过"的夹具侧失败能力——验收协议
 *     "验证测试真实失败能力"对夹具本体的同款要求）
 *
 * 用例策略（元测试——测夹具，不测任何业务域）：
 *   合成候选生成器（splitmix64 自持 PRNG——与产品代码零共享）产确定性
 *   元素流；以"注入形态"开关构造四条路径：确定性复跑（通过）、数值扰动
 *   （集合违例检出）、顺序置换（稳定排序违例检出）、报告序列化回读。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/testkit/Fixture.hpp>
#include <sdurws/ird/testkit/JsonLite.hpp>
#include <sdurws/ird/testkit/ReproBenchmark.hpp>
#include <sdurws/ird/testkit/ToleranceProfile.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace {
using namespace sdurws::ird::testkit;

// =====================================================================
// 合成被测对象（元测试载体——与任何业务域零关联）
// =====================================================================

/// 合成候选元素：身份键＋一个无量纲数值（repro.value 档案键——SI 真值）。
struct FakeCandidate {
    std::string id;      ///< 身份键（"cand-<i>"——批内唯一）
    double value = 0.0;  ///< SI 无量纲值（[0,1)——splitmix64 派生）
};

/// splitmix64 自持 PRNG（确定性生成器——同种子同序列，元测试的"被测
/// 确定性计算"替身；与产品 optimization 的 splitmix64 实现零共享代码）。
inline std::uint64_t splitmix64(std::uint64_t& state)
{
    state += 0x9E3779B97F4A7C15ull;
    std::uint64_t z = state;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

/// 注入形态（第二腿/第三腿对参考流的扰动方式——违例检出能力的激励面）。
enum class Inject {
    None,          ///< 不扰动——确定性复现（通过路径）
    ValueShift,    ///< 第 7 号元素值 +1e-6（超 1e-12 容差——集合违例）
    SwapAdjacent,  ///< 交换 3/4 号元素位置（集合等价、稳定排序违例）
};

/// 合成生成器：seed 派生 n 个元素；inject 只作用于"第 2 次及以后的调用"
/// ——模拟"同输入复跑出现了不该出现的差异"（被测性质破坏的注入面）。
/// 调用计数从 1 起：第 1 次调用＝参考腿（恒干净）；后续腿按 inject 扰动。
struct FakeGenerator {
    std::size_t n = 0;        ///< 元素数
    std::uint64_t seed = 0;   ///< 确定性种子（ReproRecord 消费面）
    Inject inject = Inject::None;
    mutable int calls = 0;    ///< 已调用次数（mutable——run 闭包按值捕获副本）

    std::vector<FakeCandidate> operator()(std::uint32_t) const
    {
        ++calls;
        std::uint64_t state = seed;
        std::vector<FakeCandidate> out;
        out.reserve(n);
        for (std::size_t i = 0; i < n; ++i) {
            // 值域 [0,1)：u53 尾数直除（无符号整除舍入——确定性）。
            const double v = static_cast<double>(splitmix64(state) >> 11)
                / 9007199254740992.0;
            out.push_back({"cand-" + std::to_string(i), v});
        }
        // 扰动只落在非首腿（参考腿必须干净——否则对照基线自身失真）。
        if (calls >= 2) {
            if (inject == Inject::ValueShift && out.size() > 7U) {
                out[7].value += 1e-6;   // 1e-6 >> 1e-12 相对容差——必超
            }
            if (inject == Inject::SwapAdjacent && out.size() > 4U) {
                std::swap(out[3], out[4]);
            }
        }
        return out;
    }
};

}  // namespace

/// traits 特化（§5.4.5 消费方适配——身份键精确匹配＋单数值字段档案键）。
template <>
struct SetMatchTraits<FakeCandidate> {
    static std::optional<std::string> identity(const FakeCandidate& e)
    {
        return e.id;
    }
    static std::vector<NumericFieldView> numerics(const FakeCandidate& e)
    {
        return {{"repro.value", e.value}};   // repro-reduction 档案键（字面量——
                                             // 静态存储期，string_view 生命周期安全）
    }
};

namespace {

/// repro-reduction 档案（编译期数据根注入——与五域用例同一数值来源）。
const ToleranceProfile& reproProfile()
{
    static const ToleranceProfile profile = ToleranceProfile::load(
        fs::path{IRD_TESTDATA_ROOT} / "tolerance" / "repro-reduction"
        / "v1.0.0.json");
    return profile;
}

/// 元测试规格（200 元素/种子 20260909〔ReproRecord 默认口径同源〕/线程腿 4）。
ReproCaseSpec makeSpec(Inject inject)
{
    ReproCaseSpec spec;
    spec.caseId = "tk-repro-fake";
    spec.domain = "testkit-meta";
    spec.inputProvenance = "synthetic-splitmix64(n=200,inject="
        + std::to_string(static_cast<int>(inject)) + ")";
    spec.repro.seed = 20260909;
    spec.repro.threadCount = 1;
    spec.parallelThreadCount = 4;
    return spec;
}

}  // namespace

// =====================================================================
// 通过路径：确定性复跑＋跨线程腿全部等价同序——夹具判 true 并如实测量
// =====================================================================

/** 三腿等价（NFR-COR-02 通过面）：passed/逐腿判定/测量值非零在位。 */
TEST(ReproBenchmark, ThreeLegsEquivalentPassMeasured_WP23T09_ACC1)
{
    ReproCaseSpec spec = makeSpec(Inject::None);
    const ReproCaseReport r = runReproCase<FakeCandidate>(
        spec, reproProfile(), SetMatchTraits<FakeCandidate>{},
        FakeGenerator{200, spec.repro.seed, Inject::None, 0});

    // 判定组：两对照腿（rerun/threads-4）全等价同序。
    EXPECT_TRUE(r.passed);
    ASSERT_EQ(r.verdicts.size(), 2U);
    EXPECT_EQ(r.verdicts[0].legId, "rerun");
    EXPECT_TRUE(r.verdicts[0].setEquivalent);
    EXPECT_TRUE(r.verdicts[0].sameOrder);
    EXPECT_EQ(r.verdicts[1].legId, "threads-4");
    EXPECT_TRUE(r.verdicts[1].setEquivalent);
    EXPECT_TRUE(r.verdicts[1].sameOrder);
    EXPECT_TRUE(r.violations.empty()) << (r.violations.empty()
        ? "" : r.violations.front());

    // 测量组（真实测量产生——零预填断言）：三腿齐全、时长非负、元素数
    // 如实记录；确定性合成流位级一致→偏差观测恰为 0（这正是被测性质）。
    ASSERT_EQ(r.legs.size(), 3U);
    EXPECT_EQ(r.legs[0].legId, "reference");
    EXPECT_EQ(r.legs[0].elementCount, 200U);
    for (const ReproLegMeasurement& leg : r.legs) {
        EXPECT_GE(leg.durationMs, 0.0) << leg.legId;
    }
    EXPECT_DOUBLE_EQ(r.maxAbsDeviation, 0.0);
    EXPECT_DOUBLE_EQ(r.maxRelativeDeviation, 0.0);
    // 逐元素比较总数＝每腿 200 匹配×1 字段×2 对照腿＝400。
    EXPECT_EQ(r.comparedValueCount, 400U);
}

// =====================================================================
// 失败能力（元测试）：数值扰动→集合违例；顺序置换→稳定排序违例
// =====================================================================

/** 数值扰动检出（夹具必须能失败——验收协议"真实失败能力"的夹具面）。 */
TEST(ReproBenchmark, ValueShiftDetectedAsSetViolation_WP23T09_ACC2)
{
    ReproCaseSpec spec = makeSpec(Inject::ValueShift);
    const ReproCaseReport r = runReproCase<FakeCandidate>(
        spec, reproProfile(), SetMatchTraits<FakeCandidate>{},
        FakeGenerator{200, spec.repro.seed, Inject::ValueShift, 0});

    EXPECT_FALSE(r.passed);
    // 复跑腿与线程腿都被扰动（calls≥2 全生效）——集合均不等价。
    ASSERT_EQ(r.verdicts.size(), 2U);
    for (const ReproLegVerdict& v : r.verdicts) {
        EXPECT_FALSE(v.setEquivalent) << v.legId;
        // 第 7 号元素身份配对值超容差：两侧均不配对。缺失/多余各计 2＝
        // SetCheck 既有记账语义（§5.4.2）：阶段 1 身份失配各列一次＋阶段 2
        // 剩余池数值复配失败再各列一次——夹具如实转写，不改记账规则。
        EXPECT_EQ(v.missingExpected, 2U) << v.legId;
        EXPECT_EQ(v.extraActual, 2U) << v.legId;
    }
    ASSERT_FALSE(r.violations.empty());
    EXPECT_NE(r.violations.front().find("集合不等价"), std::string::npos)
        << r.violations.front();
}

/** 顺序置换检出：集合等价保持、稳定排序违例——集合/顺序不可互替（§5.4.3）。 */
TEST(ReproBenchmark, SwapDetectedAsOrderViolation_WP23T09_ACC3)
{
    ReproCaseSpec spec = makeSpec(Inject::SwapAdjacent);
    const ReproCaseReport r = runReproCase<FakeCandidate>(
        spec, reproProfile(), SetMatchTraits<FakeCandidate>{},
        FakeGenerator{200, spec.repro.seed, Inject::SwapAdjacent, 0});

    EXPECT_FALSE(r.passed);
    ASSERT_EQ(r.verdicts.size(), 2U);
    for (const ReproLegVerdict& v : r.verdicts) {
        EXPECT_TRUE(v.setEquivalent) << v.legId << "（置换不改集合）";
        EXPECT_FALSE(v.sameOrder) << v.legId << "（置换必破按位序）";
    }
    ASSERT_FALSE(r.violations.empty());
    bool sawOrder = false;
    for (const std::string& v : r.violations) {
        if (v.find("稳定排序") != std::string::npos) { sawOrder = true; }
    }
    EXPECT_TRUE(sawOrder) << "违例清单缺稳定排序定位";
}

// =====================================================================
// 报告面：JSON 序列化回读＋文件写出（拖挂语义——写失败不改判定）
// =====================================================================

/** 报告 JSON 回读：字段齐全、判定与测量组一致（协议 §4.8 留痕面）。 */
TEST(ReproBenchmark, ReportJsonRoundtrip_WP23T09_ACC1)
{
    const ReproCaseSpec spec = makeSpec(Inject::None);
    const ReproCaseReport r = runReproCase<FakeCandidate>(
        spec, reproProfile(), SetMatchTraits<FakeCandidate>{},
        FakeGenerator{50, spec.repro.seed, Inject::None, 0});

    const JsonValue j = parseJson(reproReportToJson(r));
    ASSERT_TRUE(j.isObject());
    const JsonValue* passed = j.find("passed");
    const JsonValue* legs = j.find("legs");
    const JsonValue* verdicts = j.find("verdicts");
    const JsonValue* seed = j.find("seed");
    ASSERT_TRUE(passed != nullptr && passed->isBool());
    EXPECT_TRUE(passed->boolean);
    ASSERT_TRUE(legs != nullptr && legs->isArray());
    EXPECT_EQ(legs->items.size(), 3U);
    ASSERT_TRUE(verdicts != nullptr && verdicts->isArray());
    EXPECT_EQ(verdicts->items.size(), 2U);
    ASSERT_TRUE(seed != nullptr && seed->isNumber());
    EXPECT_DOUBLE_EQ(seed->number, 20260909.0);
}

/** 报告文件写出：TempDir 落盘成功且文件可回读解析（拖挂通道真实可用）。 */
TEST(ReproBenchmark, ReportFileWriteRoundtrip_WP23T09_ACC1)
{
    const ReproCaseSpec spec = makeSpec(Inject::None);
    const ReproCaseReport r = runReproCase<FakeCandidate>(
        spec, reproProfile(), SetMatchTraits<FakeCandidate>{},
        FakeGenerator{20, spec.repro.seed, Inject::None, 0});

    TempDir dir("tk-repro-report");
    dir.keepOnFailure(false);
    ASSERT_TRUE(writeReproReportJson(dir.path(), r));
    const JsonValue j = parseJson(
        [](const fs::path& p) {
            std::ifstream in(p, std::ios::binary);
            std::string s{(std::istreambuf_iterator<char>(in)),
                          std::istreambuf_iterator<char>()};
            return s;
        }(dir.path() / (r.caseId + ".json")));
    const JsonValue* passed = j.find("passed");
    ASSERT_TRUE(passed != nullptr && passed->isBool());
    EXPECT_TRUE(passed->boolean);
}
