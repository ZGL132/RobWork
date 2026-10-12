/**
 * @file   ReproBenchmarkTest.cpp
 * @brief  optimization 域确定性复现基准用例（WP-23-T09 抽样——B-08）：种子
 *         化候选生成（generateSeededLhsCandidates——seeded-lhs 策略）同配置
 *         复跑的等价候选集合＋稳定排序（NFR-COR-02/附录 D 第 8 项）。
 *
 * 设计依据：
 *   - units/optimization.md §8.2（确定性候选生成——种子唯一决定序列；批内
 *     项按 bindingId 字典序——稳定序契约）＋§8.4（R1 threadCount 恒 1——
 *     并行不落本编排，线程数进 config 身份）
 *   - doc/industrial-robot-design/performance-baseline.md §2 B-08 行＋§4.6
 *     （不要求浮点逐字节相同——对照容差经 repro-reduction 档案）
 *   - 需求 NFR-COR-02；任务契约 tasks/foundation/WP-23-T09.json
 *
 * 抽样说明（各域抽样复现——本域取样点）：种子化候选生成是本域"候选集合"
 * 的源头（I-OPT-2：种子 ≥1 且 0 非法；同种子同配置必得同批——候选身份
 * candidateIdOf 内容寻址，身份本身即确定性的可观测面）。本域 R1 并行形态
 * ＝threadCount 恒 1 且进身份（ParallelConfiguration 注）——改线程数即改
 * *输入*而非执行参数，"跨线程等价"在本域不适用；如实只跑复跑腿。
 */

#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/core/Units.hpp>
#include <sdurws/ird/optimization/CandidatePatch.hpp>
#include <sdurws/ird/optimization/EvaluatorPorts.hpp>
#include <sdurws/ird/optimization/Objective.hpp>
#include <sdurws/ird/optimization/Types.hpp>
#include <sdurws/ird/optimization/Variable.hpp>
#include <sdurws/ird/testkit/ReproBenchmark.hpp>
#include <sdurws/ird/testkit/ToleranceProfile.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace opt = sdurws::ird::optimization;
namespace core = sdurws::ird::core;
namespace tk = sdurws::ird::testkit;

// =====================================================================
// 元素投影（CandidatePatch → SetMatchTraits 元素——§5.4.5 适配归域）。
// 元素与 traits 特化置于全局作用域（SetCheckTest 消费示范同款——特化须
// 在主模板的可达外围命名空间声明）。
// =====================================================================

/// 候选元素：身份＝candidateIdOf 内容寻址规范文本（同种子同配置必同身份
/// ——身份键本身即确定性的可观测面）；数值＝补丁项标量值（bindingId 字典
/// 序——批内项稳定序契约）＋逐项档案键（按绑定单位量纲取 repro.length/
/// repro.angle/repro.value）。
struct ReproCandidateItem {
    std::string id;                        ///< "cnd-<64hex>"（候选身份）
    std::vector<std::string> valueKeys;    ///< 逐项档案键（与 values 对齐）
    std::vector<double> values;            ///< 补丁项标量值（SI 单位）
};

namespace sdurws::ird::testkit {
template <>
struct SetMatchTraits<ReproCandidateItem> {
    static std::optional<std::string> identity(const ReproCandidateItem& e)
    {
        return e.id;
    }
    static std::vector<NumericFieldView> numerics(const ReproCandidateItem& e)
    {
        std::vector<NumericFieldView> out;
        out.reserve(e.values.size());
        for (std::size_t i = 0; i < e.values.size(); ++i) {
            out.push_back({e.valueKeys[i], e.values[i]});   // 键＝元素自有串
                                                            // （存活期覆盖断言）
        }
        return out;
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

/// 变量绑定构造（EvaluatorPortsTest makeLengthBinding 同款字段面）。
/// diagSubject 由绑定序号派生固定规范 id（复现要素——不用随机 id/哈希）。
opt::VariableBinding makeBinding(int index, const std::string& id,
                                 opt::VariableKind kind, const char* unit,
                                 double lower, double upper, double step,
                                 double defaultValue)
{
    opt::VariableBinding b;
    b.bindingId = id;
    b.kind = kind;
    b.unit = core::UnitToken::find(unit).value();
    b.lowerBound = lower;          // SI 单位（与 unit 一致）
    b.upperBound = upper;
    b.step = step;                 // 量化步长（Continuous 恒 0）
    b.defaultValue = defaultValue;
    b.authorized = true;
    b.locked = false;
    b.authorityFieldPath = "repro-benchmark/" + id;   // 权威定位（测试值）
    // 固定 id（序号补零成 32 位 hex——"obj-<32hex>" 规范文本形态，非解析）。
    char suffix[33];
    std::snprintf(suffix, sizeof(suffix), "%032d", index);
    b.diagSubject = std::string("obj-") + suffix;
    return b;
}

/// 基线身份（固定字面量——复现要素，不用随机 id）。
opt::OptimizationConfiguration makeConfig()
{
    opt::OptimizationConfiguration c;
    c.stage = opt::OptimizationStage::StageB;
    c.seed = 20260909U;            // I-OPT-2：≥1（ReproRecord 默认口径同源）
    c.budget.maxCandidates = 24U;  // 24 候选（含批首基线——抽样规模）
    c.budget.maxVerifiedCandidates = 8U;
    c.strategyId = std::string(opt::kStrategySeededLhs);
    c.objectives = opt::defaultObjectives(opt::OptimizationStage::StageB);
    // 变量集＝R1 内置词表（§5.3——token 须匹配词表 pattern；R1 无量化/枚举
    // 强制变量，连续 m/rad 两量纲覆盖档案键 repro.length/repro.angle）。
    c.variables = {
        makeBinding(1, "mdl.joint[2].dh.a", opt::VariableKind::Continuous,
                    "m", 0.2, 0.8, 0.0, 0.5),          // 长度维
        makeBinding(2, "mdl.joint[3].dh.a", opt::VariableKind::Continuous,
                    "m", 0.1, 0.6, 0.0, 0.35),         // 长度维
        makeBinding(3, "mdl.joint[4].dh.d", opt::VariableKind::Continuous,
                    "m", 0.05, 0.45, 0.0, 0.25),       // 长度维
        makeBinding(4, "mdl.joint[5].dh.d", opt::VariableKind::Continuous,
                    "m", 0.1, 0.5, 0.0, 0.3),          // 长度维
        makeBinding(5, "mdl.joint[6].origin.t.z", opt::VariableKind::Continuous,
                    "m", 0.0, 0.4, 0.0, 0.2),          // 长度维
        makeBinding(6, "mdl.base.orientation.eaa.x", opt::VariableKind::Continuous,
                    "rad", -0.1, 0.1, 0.0, 0.0),       // 角度维
        makeBinding(7, "mdl.base.orientation.eaa.y", opt::VariableKind::Continuous,
                    "rad", -0.1, 0.1, 0.0, 0.0),       // 角度维
    };
    opt::validateConfiguration(c);
    return c;
}

/// 候选批投影（批序＝生成序——稳定序契约面；身份＝内容寻址）。
std::vector<ReproCandidateItem> projectBatch(
    const std::vector<opt::CandidatePatch>& batch,
    const opt::OptimizationConfiguration& config)
{
    const core::ObjectId baselineRoot = core::ObjectId::fromCanonical(
        "obj-00000000000000000000000000000abc");
    const core::ContentVersion baselineCv = core::ContentVersion::fromCanonical(
        "cv-00000000000000000000000000000000000000000000000000000000000000bb");

    // 绑定单位 → 档案键（量纲归档——长度/角度/无量纲三键）。
    std::vector<std::string> unitKeys;
    unitKeys.reserve(config.variables.size());
    for (const opt::VariableBinding& v : config.variables) {
        if (v.unit == core::UnitToken::find("m").value()) {
            unitKeys.push_back("repro.length");
        } else if (v.unit == core::UnitToken::find("rad").value()) {
            unitKeys.push_back("repro.angle");
        } else {
            unitKeys.push_back("repro.value");
        }
    }

    std::vector<ReproCandidateItem> out;
    out.reserve(batch.size());
    for (const opt::CandidatePatch& patch : batch) {
        ReproCandidateItem e;
        e.id = opt::candidateIdOf(baselineRoot, baselineCv, patch).toCanonical();
        for (const opt::PatchItem& item : patch.items) {
            // 补丁项单位＝其绑定单位（bindingId 字典序——键序与值序对齐：
            // 变量表本身按 bindingId 字典序构造，见 makeConfig）。
            std::size_t keyIndex = 0;
            for (std::size_t vi = 0; vi < config.variables.size(); ++vi) {
                if (config.variables[vi].bindingId == item.bindingId) {
                    keyIndex = vi;
                    break;
                }
            }
            e.valueKeys.push_back(unitKeys[keyIndex]);
            e.values.push_back(item.scalarValue);
        }
        out.push_back(std::move(e));
    }
    return out;
}

}  // namespace

/**
 * optimization 域复现基准（B-08 抽样）：seeded-lhs 24 候选批（6 变量：
 * 连续长度/角度＋量化无量纲——覆盖三类档案键）同种子同配置复跑——等价
 * 候选集合＋稳定排序。
 */
TEST(OptReproBenchmark, SeededLhsRerunEquivalentSetAndOrder_WP23T09_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-02"},
                  std::vector<std::string>{"AT-09"});

    // ---- 复现用例执行（两腿：reference/rerun——线程腿不适用，§8.4 R1）----
    tk::ReproCaseSpec spec;
    spec.caseId = "opt-seeded-lhs-rerun";
    spec.domain = "optimization";
    spec.inputProvenance =
        "lhsConfigFixture(24 candidates, 7 R1-vocabulary continuous bindings "
        "[5 dh/origin m + 2 base-eaa rad], seed=20260909, strategy=seeded-lhs)";
    spec.repro.seed = 20260909;
    spec.repro.threadCount = 1;
    spec.parallelThreadCount = 0;    // R1 threadCount 恒 1 且进身份——线程腿不适用

    const opt::OptimizationConfiguration config = makeConfig();
    const tk::ReproCaseReport report = tk::runReproCase<ReproCandidateItem>(
        spec, reproProfile(), tk::SetMatchTraits<ReproCandidateItem>{},
        [&config](std::uint32_t) {
            return projectBatch(opt::generateSeededLhsCandidates(config), config);
        });

    // 判定（NFR-COR-02：等价候选集合＋稳定排序——本域抽样面）。
    ASSERT_EQ(report.verdicts.size(), 1U);
    EXPECT_TRUE(report.verdicts.front().setEquivalent);
    EXPECT_TRUE(report.verdicts.front().sameOrder);
    EXPECT_TRUE(report.passed) << (report.violations.empty()
        ? "" : report.violations.front());
    // 元素面：24 候选（含批首基线）如实留痕。
    EXPECT_EQ(report.legs.front().elementCount, 24U);

    // 报告拖挂（IRD_REPRO_REPORT_DIR 设置时落盘——基准执行留痕）。
    if (const auto dir = tk::reproReportDirFromEnv()) {
        EXPECT_TRUE(tk::writeReproReportJson(*dir, report));
    }
}
