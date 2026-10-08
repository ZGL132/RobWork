/**
 * @file   ObjectiveParetoContractTest.cpp
 * @brief  目标指标与 Pareto 契约用例组（OptObjectiveParetoContract）——
 *         跨单元契约面：八项指标词表/默认激活分期契约（经
 *         IOptimizationObjectiveProvider 接口消费）、显式支配容差进
 *         config.opt 的承载契约（canonical 目标段——acceptance 1）、
 *         OPT-04 加权总分排除红线的产品面源码扫描——任务契约
 *         WP-20-T05 acceptance 1/2/3。
 *
 * 设计依据：
 *   - units/optimization.md §7.1/§7.3（八项词表与默认激活分期——契约断言
 *     以 §15.0/RV-03/F-02 口径钉住）、§7.4/DOPT-5（默认零容差——不发明
 *     阈值；显式容差进 config.opt 进 sliceId）、§4.3（canonical 目标段
 *     编码——"位图＋每目标容差三元组"）、§4.5（目标配置变更 ⇒ 新运行
 *     ——容差差异必须进 canonical 的失效面）、§16.1 DOPT-8（输出序三键
 *     且与线程无关——接口路径钉住）
 *   - 需求 OPT-04（**不以单一加权总分代替工程取舍**——需求附录 B 裁决
 *     排除："目标权重模板""候选表六维加权评分"均为裁决排除行；产品面
 *     不得复活加权语义——本组以源码扫描承载）、OPT-07（八项全部展示/
 *     默认激活分期）、AT-09（集合/排序满足容差支配）
 *   - 先例：BuildRedLineTest 的运行期源码扫描形态（剥注释后扫描——文档
 *     性提及不属行为）＋trajectory/dynamics 契约测试的词表钉住形态
 */

#include <sdurws/ird/optimization/Objective.hpp>
#include <sdurws/ird/optimization/Pareto.hpp>

#include <sdurws/ird/core/Compare.hpp>
#include <sdurws/ird/evidence/Evaluator.hpp>
#include <sdurws/ird/optimization/DiagCodes.hpp>
#include <sdurws/ird/optimization/Types.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <regex>
#include <string>
#include <vector>

#ifndef IRD_OPTIMIZATION_UNIT_ROOT
#error "契约扫描用例需要 IRD_OPTIMIZATION_UNIT_ROOT 编译定义（源码树根）"
#endif

using namespace sdurws::ird;
using optimization::CandidateId;
using optimization::FeasibleCandidate;
using optimization::IOptimizationObjectiveProvider;
using optimization::IParetoFrontBuilder;
using optimization::MetricDefinition;
using optimization::MetricId;
using optimization::ObjectiveEntry;
using optimization::ObjectiveSet;
using optimization::OptimizationObjectiveProvider;
using optimization::OptimizationStage;
using optimization::ParetoFrontBuilder;
using optimization::canonicalizeObjectiveSegment;
using optimization::defaultObjectives;
using optimization::makeObjectiveSet;

namespace {

namespace fs = std::filesystem;

/// optimization 单元树根（industrialrobot 目录——与 BuildRedLineTest 同款
/// 编译定义注入口径）。
const fs::path& unitRoot()
{
    static const fs::path dir = fs::path{IRD_OPTIMIZATION_UNIT_ROOT};
    return dir;
}

/// 读取文件全文（二进制——与 BuildRedLineTest 同款）。
std::string readFile(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    return std::string{std::istreambuf_iterator<char>(in),
                       std::istreambuf_iterator<char>()};
}

/// 剥离 // 行注释与 /* */ 块注释（文档性提及不属行为——与门禁 F-011
/// 修复后口径一致；字符串字面量内的注释符会被误剥，但加权语义扫描的
/// 词表不含注释符形态，误剥不影响判定方向〔宁严勿漏〕）。
std::string stripComments(const std::string& src)
{
    std::string out;
    out.reserve(src.size());
    enum class State { Code, LineComment, BlockComment };
    State state = State::Code;
    for (std::size_t i = 0; i < src.size(); ++i) {
        const char c = src[i];
        const char next = (i + 1 < src.size()) ? src[i + 1] : '\0';
        switch (state) {
        case State::Code:
            if (c == '/' && next == '/') {
                state = State::LineComment;
                ++i;
            } else if (c == '/' && next == '*') {
                state = State::BlockComment;
                ++i;
            } else {
                out.push_back(c);
            }
            break;
        case State::LineComment:
            if (c == '\n') {
                out.push_back(c);
                state = State::Code;
            }
            break;
        case State::BlockComment:
            if (c == '*' && next == '/') {
                state = State::Code;
                ++i;
            }
            break;
        }
    }
    return out;
}

/// 产品面源码文件清单（include/**＋src/**——与 ird_gates 第 4 步"产品面"
/// 同口径；加权总分红线扫描域）。路径拼接与 BuildRedLineTest 同款：
/// 单元根（industrialrobot 目录）下 optimization/<sub>。
std::vector<fs::path> productSources()
{
    std::vector<fs::path> files;
    for (const auto& dir : {"include", "src"}) {
        const auto base = unitRoot() / "optimization" / dir;
        for (const auto& entry : fs::recursive_directory_iterator(base)) {
            if (entry.is_regular_file()
                && (entry.path().extension() == ".hpp"
                    || entry.path().extension() == ".cpp")) {
                files.push_back(entry.path());
            }
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

/// 非零候选身份（与 ParetoTest 同构——契约面独立自持）。
CandidateId makeCid(unsigned char seed)
{
    CandidateId id;
    id.bytes.fill(seed);
    return id;
}

/// 三目标可行候选（StageB 默认集：min 包络/min 结构质量/max 裕量——
/// 接口消费的标准场景；槽位与默认目标集对齐，防激活指标缺失防线误伤）。
FeasibleCandidate makeCandidate(unsigned char seed, double envelope, double margin)
{
    FeasibleCandidate c;
    c.candidateId = makeCid(seed);
    c.metricValues.assign(optimization::kMetricCount, std::nullopt);
    c.metricValues[static_cast<std::size_t>(MetricId::Envelope)] = envelope;
    c.metricValues[static_cast<std::size_t>(MetricId::StructuralMass)] = envelope;
    c.metricValues[static_cast<std::size_t>(MetricId::MinJointMargin)] = margin;
    return c;
}

}  // namespace

// =====================================================================
// 词表与默认激活契约（经接口消费——虚函数路径钉住）
// =====================================================================

/// OPT-07 契约：八项指标全部展示的词表面（八项 token 互异且全带
/// "opt.metric." 前缀；方向词表 §7.2 冻结口径——六 min 两 max）＋默认
 /// 激活分期（§7.3/§15.0——StageB 三项静态/StageD 三项）。经
/// IOptimizationObjectiveProvider 接口指针消费（接口路径钉住——
/// WP-20-T03 B-1 教训：不留只测自由函数的盲区）。
TEST(OptObjectiveParetoContract, MetricVocabularyAndDefaultActivation)
{
    IRD_TEST_INFO("OPT-07", {"AT-09"}, std::nullopt);
    const OptimizationObjectiveProvider provider;
    const IOptimizationObjectiveProvider& api = provider;

    // 八项词表：全带 opt.metric. 前缀、token 互异、方向冻结口径。
    const std::vector<MetricDefinition> defs = api.metricDefinitions();
    ASSERT_EQ(defs.size(), 8U);
    for (std::size_t i = 0; i < defs.size(); ++i) {
        const std::string token(optimization::toToken(defs[i].metricId));
        EXPECT_EQ(token.substr(0, 11), "opt.metric.") << "词形契约（§7.1）";
        for (std::size_t j = i + 1; j < defs.size(); ++j) {
            EXPECT_NE(defs[i].metricId, defs[j].metricId) << "ID 互异";
        }
    }
    // 方向冻结：包络/质量/节拍/成本/器件质量/正机械功＝min；关节裕量/
    // 驱动裕量＝max（§7.2 原文）。
    EXPECT_EQ(defs[0].direction, optimization::MetricDirection::Minimize);
    EXPECT_EQ(defs[1].direction, optimization::MetricDirection::Minimize);
    EXPECT_EQ(defs[2].direction, optimization::MetricDirection::Maximize);
    EXPECT_EQ(defs[3].direction, optimization::MetricDirection::Minimize);
    EXPECT_EQ(defs[4].direction, optimization::MetricDirection::Minimize);
    EXPECT_EQ(defs[5].direction, optimization::MetricDirection::Minimize);
    EXPECT_EQ(defs[6].direction, optimization::MetricDirection::Minimize);
    EXPECT_EQ(defs[7].direction, optimization::MetricDirection::Maximize);

    // 默认激活分期契约（§7.3 默认激活行——REQUIREMENTS §15.0 权威）。
    const ObjectiveSet b = defaultObjectives(OptimizationStage::StageB);
    ASSERT_EQ(b.entries.size(), 3U);
    EXPECT_EQ(b.entries[0].metricId, MetricId::Envelope);
    EXPECT_EQ(b.entries[1].metricId, MetricId::StructuralMass);
    EXPECT_EQ(b.entries[2].metricId, MetricId::MinJointMargin);
    const ObjectiveSet d = defaultObjectives(OptimizationStage::StageD);
    ASSERT_EQ(d.entries.size(), 3U);
    EXPECT_EQ(d.entries[0].metricId, MetricId::CycleTime);
    EXPECT_EQ(d.entries[1].metricId, MetricId::StructuralMass);
    EXPECT_EQ(d.entries[2].metricId, MetricId::DeviceCost);
}

// =====================================================================
// 显式容差进 config.opt 契约（acceptance 1——canonical 目标段承载）
// =====================================================================

/// DOPT-5/§4.5 契约：支配容差默认零（不发明阈值——P-OPT-5 安全默认）；
/// 显式容差差异必须进 canonical 目标段字节（⇒ 新 sliceId ⇒ 新运行——
/// 缓存失效面）；默认零容差下 buildFront 经接口路径输出容差支配面
/// （AT-09"集合/排序满足容差支配"的契约观测）。
TEST(OptObjectiveParetoContract, ToleranceConfigCarriedIntoConfigOpt)
{
    IRD_TEST_INFO("OPT-04", {"AT-09"}, std::nullopt);
    // 默认容差全零（DOPT-5）。
    const ObjectiveSet def = defaultObjectives(OptimizationStage::StageB);
    for (const auto& e : def.entries) {
        EXPECT_EQ(e.tolerance, core::Tolerance{}) << "默认零容差——不发明阈值";
    }

    // 显式容差 vs 零容差 ⇒ 段字节不同（进 config.opt ⇒ 身份变化——§4.5
    // "目标配置（激活/方向/支配容差）→ config.opt 变化"）。三项目标全部
    // 显式 ε_abs 0.01（逐目标容差——近相等对在全部激活维度容差内）。
    const ObjectiveSet explicitTol = makeObjectiveSet(
        {{MetricId::Envelope, {0.0, 0.01}},
         {MetricId::StructuralMass, {0.0, 0.01}},
         {MetricId::MinJointMargin, {0.0, 0.01}}});
    EXPECT_NE(canonicalizeObjectiveSegment(explicitTol),
              canonicalizeObjectiveSegment(def))
        << "显式容差必须进 canonical（缓存失效面——acceptance 1）";

    // 容差支配契约观测（AT-09）：同一候选对在零容差/显式容差下的支配
    // 关系翻转——近相等对 (1.000, 0.9) vs (1.008, 0.9)。
    const FeasibleCandidate a = makeCandidate(0x91, 1.000, 0.9);
    const FeasibleCandidate b = makeCandidate(0x92, 1.008, 0.9);
    const IParetoFrontBuilder& builder = ParetoFrontBuilder{};
    // 零容差：a 严格支配 b → a rank 0、b rank 1。
    const auto r0 = builder.buildFront({a, b}, def);
    EXPECT_EQ(r0.entries[0].candidateId, makeCid(0x91));
    EXPECT_EQ(r0.entries[0].nondominationRank, 0U);
    EXPECT_EQ(r0.entries[1].nondominationRank, 1U);
    // 显式 ε_abs 0.01：互不支配 → 同居第一前沿（集合满足容差支配）。
    const auto r1 = builder.buildFront({a, b}, explicitTol);
    EXPECT_EQ(r1.entries[0].nondominationRank, 0U);
    EXPECT_EQ(r1.entries[1].nondominationRank, 0U);
    EXPECT_EQ(r1.nondominatedIds.size(), 2U) << "容差内互不支配（AT-09）";
}

// =====================================================================
// OPT-04 加权总分排除红线（需求附录 B 裁决排除——源码扫描）
// =====================================================================

/// OPT-04/附录 B 裁决排除契约：产品面（include/**＋src/**）剥注释后不得
/// 出现加权总分语义标识符（"目标权重模板""候选表六维加权评分"为需求
/// 附录 B 裁决排除行——Pareto＋指标分层取代；未来回归引入即失败）。
/// 词表为**标识符级**窄词表（避免误伤文档性中文否定语境——注释已剥离，
/// 中文书写不进扫描）。
TEST(OptObjectiveParetoContract, NoWeightedScoreInProductSources)
{
    IRD_TEST_INFO("OPT-04", {"AT-09"}, std::nullopt);
    // 标识符级窄词表（camelCase/snake_case 常见形态——加权评分/权重和/
    // 总分承载变量或函数名的可触及形态）。
    const std::regex weightedPattern{
        "weightedScore|weighted_sum|weightedSum|scoreWeight|score_weight"
        "|totalScore|total_score|weightVector|weight_vector"
        "|combinedScore|combined_score|aggregateScore|aggregate_score"};
    const std::vector<fs::path> files = productSources();
    ASSERT_FALSE(files.empty()) << "产品面源码非空（扫描域自检）";
    for (const auto& f : files) {
        const std::string code = stripComments(readFile(f));
        std::smatch m;
        std::regex_search(code, m, weightedPattern);
        EXPECT_TRUE(m.empty())
            << "OPT-04 加权总分红线：产品面发现加权语义标识符（附录 B 裁决"
               "排除）—— " << f.string();
    }
}
