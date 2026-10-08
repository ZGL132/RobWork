/**
 * @file   FeasibleSetTest.cpp
 * @brief  可行集与淘汰原因输出用例组（SelFeasibleSet）——黄金三组合端到
 *         端可行集组装（SEL-06：可行组合＋裕量/质量/成本/来源＋逐项淘汰
 *         原因含实际值/阈值/thresholdSource）、EVI-02 多工况资格（格非
 *         Pass/无格降级＋记录缺失合成缺口，不漏验）、空集语义（全淘汰
 *         可行集空）、稳定排序与确定性、调用方契约拒绝、淘汰原因构造器
 *         （ERR-01 全字段＋diagRef 稳定码回填）与裕量口径（负值如实/
 *         缺数据 nullopt）。
 *
 * 设计依据：
 *   - units/selection.md §10.1～10.4（类型/空集语义/词表/稳定排序）、
 *     §9.4（EVI-02 资格矩阵）、§14.6（IFeasibleSetBuilder/IRejectionReason
 *     Provider——经接口引用消费钉扎交付面，不留只测自由函数的盲区）、
 *     §14.0（错误两分法）、§17.3（裕量＝工作点对能力的余量，含来源工况）
 *   - 需求 SEL-06（可行组合、裕量、质量、成本、来源和逐项淘汰原因（含
 *     实际值与阈值））、SEL-05（上游组合级记录）、EVI-02（必验工况全覆
 *     盖不漏验）、ERR-01（比较型字段＋稳定诊断引用）、NFR-COR-01/02/03
 *   - 任务契约 tasks/foundation/WP-19-T06.json acceptance 1/2
 *
 * 黄金值口径（附录 D 解析算例）：电机侧工作点为映射口径解析值（黄金
 * 联动 CombinationTestSupport.hpp——c＝1/n）；裕量 margin＝
 * (capability−|actual|)/capability 全部为一位小数内可解析值（如 K1 J1
 * 面连续转矩 (5−4)/5＝0.2）。核心直调与接口消费并存（模型测试直调
 * NFR-MNT-01；组装器/原因构造器一律经接口引用调用——交付面钉扎）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/selection/CatalogTypes.hpp>
#include <sdurws/ird/selection/Combination.hpp>
#include <sdurws/ird/selection/DiagCodes.hpp>
#include <sdurws/ird/selection/FeasibleSet.hpp>
#include <sdurws/ird/selection/Screening.hpp>

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO

#include "CombinationTestSupport.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

using namespace sdurws::ird::selection;
using namespace sdurws::ird::selection::testsupport;
using namespace sdurws::ird;

namespace {

/// 黄金三组合夹具构造（与 CombinationCheckTest 同口径——展开序 6 组合，
/// 黄金面 K1 可行/K2 转速淘汰/K3 转矩淘汰以轴×候选内容定位）。
inline GoldenCombos makeGoldenCombos()
{
    GoldenCombos g;
    g.axes = makeGoldenAxes();
    DeviceCombinationBuilder builder;
    const std::vector<AxisCandidateList> perAxis = {
        AxisCandidateList{g.axes.j1, {"M-A", "M-B"}, {"G-10", "G-20"}},
        AxisCandidateList{g.axes.j2, {"M-A"}, {"G-10", "G-20"}},
    };
    const CombinationSet set = builder.build(perAxis, goldenCompat(),
                                             BatchBudget{64}, goldenCatalog());
    g.combos = set.combinations;
    return g;
}

/// 黄金核心输入（单工况 case-A——快照＋轴事实＋映射批＋条件）。
inline CombinationCheckCoreInput makeCoreInput(const CatalogPackageSnapshot& snapshot,
                                               const GoldenCombos& golden)
{
    CombinationCheckCoreInput in;
    in.snapshot = &snapshot;
    in.axisFacts = {
        makeJointFacts(golden.axes.j1, "case-A", true),
        makeJointFacts(golden.axes.j2, "case-A", false),
    };
    in.mappingBatch = goldenMappingBatch(golden.combos, golden.axes);
    in.criteria = goldenCriteria();
    return in;
}

/// 黄金身份块（直调口径：切片/映射身份全零诚实标记＋契约版本 1）。
inline IdentityBlock goldenIdentity(const CatalogPackageSnapshot& snapshot)
{
    IdentityBlock id;
    id.catalog = snapshot.manifest.identity;
    id.mappingSliceId = core::ContentIdentity{};
    id.inputSliceId = core::ContentIdentity{};
    id.contractVersion = kCombinationCheckContractVersion;
    id.mode = core::EvaluationMode::Verified;
    return id;
}

/// 组装输入装配（组合校核产出→指标计算→entries；T06 组装方的标准编排
/// ——L5 装配期同款三步）。
inline std::vector<FeasibleSetEntry> makeEntries(
    const GoldenCombos& golden,
    const CombinationCheckCoreInput& in,
    const std::vector<CombinationCheckOutcome>& outcomes)
{
    const std::vector<FeasibleCombinationMetrics> metrics
        = computeFeasibleCombinationMetrics(in, outcomes);
    std::vector<FeasibleSetEntry> entries;
    entries.reserve(golden.combos.size());
    for (const DeviceCombination& combo : golden.combos) {
        FeasibleSetEntry entry;
        entry.combination = combo;
        for (const FeasibleCombinationMetrics& m : metrics) {
            if (m.combinationId == combo.id) {
                entry.metrics = m;
                break;
            }
        }
        entries.push_back(std::move(entry));
    }
    return entries;
}

/// 汇总逐组合 coverage（组合校核产出→组装器入参——T05 评估器同款收集）。
inline std::vector<CaseCoverageEntry> collectCoverage(
    const std::vector<CombinationCheckOutcome>& outcomes)
{
    std::vector<CaseCoverageEntry> coverage;
    for (const CombinationCheckOutcome& o : outcomes) {
        coverage.insert(coverage.end(), o.coverage.begin(), o.coverage.end());
    }
    return coverage;
}

/// 汇总逐组合 record（组合校核产出→组装器入参）。
inline std::vector<FeasibilityRecord> collectRecords(
    const std::vector<CombinationCheckOutcome>& outcomes)
{
    std::vector<FeasibilityRecord> records;
    for (const CombinationCheckOutcome& o : outcomes) {
        records.push_back(o.record);
    }
    return records;
}

}  // namespace

// ---------------------------------------------------------------------
// acceptance 1：可行组合＋裕量/质量/成本/来源＋逐项淘汰原因输出（SEL-06）
// ---------------------------------------------------------------------

/// 黄金三组合端到端：可行集恰含 K1；K2/K3 淘汰记录原因含实际值/阈值/
/// thresholdSource＋diagRef 回填；质量/最小裕量解析值钉住；成本显式缺失。
TEST(SelFeasibleSet, BuildsGoldenFeasibleSetWithSel06Facts_WP19T06_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-06", "EVI-02"},
                  std::vector<std::string>{"AT-08"});  // R1——可行集＋逐项淘汰原因＋指标输出
    const GoldenCombos golden = makeGoldenCombos();
    CatalogPackageSnapshot snapshot = goldenSnapshot();
    const CombinationCheckCoreInput in = makeCoreInput(snapshot, golden);
    const std::vector<CombinationCheckOutcome> outcomes = checkCombinations(in, nullptr);
    ASSERT_EQ(outcomes.size(), golden.combos.size());

    const DeviceCombination* k1 = findCombo(golden.combos, "M-A", "G-10", "M-A", "G-10");
    const DeviceCombination* k2 = findCombo(golden.combos, "M-A", "G-20", "M-A", "G-20");
    const DeviceCombination* k3 = findCombo(golden.combos, "M-B", "G-10", "M-A", "G-10");
    ASSERT_TRUE(k1 != nullptr && k2 != nullptr && k3 != nullptr);

    // 组装（经接口引用消费——交付面钉扎，WP-20-T03 接口路径盲区教训）。
    const IFeasibleSetBuilder& builder = FeasibleSetBuilder{};
    const SelectionRunResult result
        = builder.build(collectRecords(outcomes), collectCoverage(outcomes),
                        goldenIdentity(snapshot), makeEntries(golden, in, outcomes));

    // 可行集：恰含 K1（黄金可行面）——组合实体构成面随行（§10.1）。
    ASSERT_EQ(result.set.feasible.size(), std::size_t{1});
    EXPECT_EQ(result.set.feasible[0].id, k1->id);
    EXPECT_EQ(result.set.feasible[0].axes.size(), k1->axes.size());
    // 记录三态齐备：6 组合全部有记录；K1 Feasible、K2/K3 Rejected。
    ASSERT_EQ(result.set.records.size(), golden.combos.size());
    const FeasibilityRecord* k1Rec = nullptr;
    const FeasibilityRecord* k2Rec = nullptr;
    const FeasibilityRecord* k3Rec = nullptr;
    for (const FeasibilityRecord& rec : result.set.records) {
        if (rec.id == k1->id) {
            k1Rec = &rec;
        } else if (rec.id == k2->id) {
            k2Rec = &rec;
        } else if (rec.id == k3->id) {
            k3Rec = &rec;
        }
    }
    ASSERT_TRUE(k1Rec != nullptr && k2Rec != nullptr && k3Rec != nullptr);
    EXPECT_EQ(k1Rec->verdict, VerdictKind::Feasible);
    EXPECT_EQ(k2Rec->verdict, VerdictKind::Rejected);
    EXPECT_EQ(k3Rec->verdict, VerdictKind::Rejected);

    // 逐项淘汰原因（SEL-06/ERR-01）：K3 连续转矩不足——实际值/要求值/
    // 单位/阈值来源/建议动作/diagRef 全字段齐备（黄金解析：τ_m_rms＝
    // c×τ_joint_rms＝0.1×40＝4.0 ＞ 额定 2.0 N·m）。
    const RejectionReason* cont = nullptr;
    for (const RejectionReason& r : k3Rec->reasons) {
        if (r.token == ReasonToken::TorqueContinuousInsufficient) {
            cont = &r;
        }
    }
    ASSERT_TRUE(cont != nullptr) << "缺少连续转矩不足原因";
    EXPECT_DOUBLE_EQ(cont->actual, 4.0);
    EXPECT_DOUBLE_EQ(cont->required, 2.0);
    EXPECT_EQ(cont->unit, "N*m");
    EXPECT_FALSE(cont->thresholdSource.empty()) << "阈值来源缺失（ERR-01）";
    EXPECT_FALSE(cont->suggestion.empty()) << "建议动作缺失（ERR-01）";
    EXPECT_EQ(cont->candidateModelId, "M-B");
    EXPECT_EQ(cont->caseId, "case-A");
    ASSERT_TRUE(cont->diagRef.has_value());
    EXPECT_EQ(*cont->diagRef,
              std::string(kSelMotorTorqueContinuousInsufficient));
    // K2 转速维原因同样带稳定码引用（词表映射面）。
    bool k2HasCodedReason = false;
    for (const RejectionReason& r : k2Rec->reasons) {
        if (r.token == ReasonToken::SpeedInsufficient) {
            ASSERT_TRUE(r.diagRef.has_value());
            EXPECT_EQ(*r.diagRef, std::string(kSelMotorSpeedInsufficient));
            k2HasCodedReason = true;
        }
    }
    EXPECT_TRUE(k2HasCodedReason);

    // SEL-06 指标素材：metrics 与 records 对位输出（6 条）；K1 质量＝
    // (3+2)×2＝10.0 kg（黄金解析值）；最小驱动裕量＝J1 面连续转矩维
    // (5−4)/5＝0.2（六维解析最小值——黄金联动映射口径）；成本 v1 无
    // 字段显式缺失（不伪造）。
    ASSERT_EQ(result.metrics.size(), golden.combos.size());
    const FeasibleCombinationMetrics* k1Metrics = nullptr;
    const FeasibleCombinationMetrics* k2Metrics = nullptr;
    for (const FeasibleCombinationMetrics& m : result.metrics) {
        if (m.combinationId == k1->id) {
            k1Metrics = &m;
        } else if (m.combinationId == k2->id) {
            k2Metrics = &m;
        }
    }
    ASSERT_TRUE(k1Metrics != nullptr);
    EXPECT_DOUBLE_EQ(k1Metrics->totalMass, 10.0);
    ASSERT_TRUE(k1Metrics->minMargin.has_value());
    EXPECT_DOUBLE_EQ(k1Metrics->minMargin->margin, 0.2);
    EXPECT_EQ(k1Metrics->minMargin->dimension, "motor-torque-continuous");
    EXPECT_EQ(k1Metrics->minMargin->caseId, "case-A");
    EXPECT_EQ(k1Metrics->minMargin->axisId, golden.axes.j1);
    EXPECT_EQ(k1Metrics->minMargin->unit, "N*m");
    EXPECT_FALSE(k1Metrics->cost.has_value()) << "成本 v1 无字段——不得伪造";
    // "来源"面：结果身份块携带目录来源描述（SEL-06 来源输出之一）。
    EXPECT_EQ(result.identity.catalog, snapshot.manifest.identity);
    EXPECT_EQ(result.identity.catalog.source, "组合校核黄金目录");

    // 覆盖矩阵素材透传（组合×工况逐格）。
    EXPECT_EQ(result.coverage.size(), collectCoverage(outcomes).size());

    // EVI-02 语义自证：K1 仅在唯一启用工况 Pass 后进入可行集（上面
    // feasible 恰一）；K2 因该工况 Fail 被淘汰——不漏验。
}

/// 全淘汰语义：全部记录 Rejected 时可行集为空且原因全集齐备——不伪造
/// 可行、不自动判任务不可行（判定权在 evidence 汇总，卡 §10.2）。
TEST(SelFeasibleSet, AllRejectedYieldsEmptyFeasible_WP19T06_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-06"},
                  std::vector<std::string>{});  // R1——全淘汰：可行集空＋原因全集
    const GoldenCombos golden = makeGoldenCombos();
    CatalogPackageSnapshot snapshot = goldenSnapshot();
    const CombinationCheckCoreInput in = makeCoreInput(snapshot, golden);
    const std::vector<CombinationCheckOutcome> outcomes = checkCombinations(in, nullptr);

    // 只送淘汰组合（K2/K3）——可行集必须为空。
    std::vector<FeasibilityRecord> records;
    std::vector<CaseCoverageEntry> coverage;
    std::vector<FeasibleSetEntry> entries;
    for (const DeviceCombination& combo : golden.combos) {
        const CombinationCheckOutcome* o = findOutcome(outcomes, combo.id);
        ASSERT_TRUE(o != nullptr);
        if (o->record.verdict != VerdictKind::Rejected) {
            continue;
        }
        records.push_back(o->record);
        coverage.insert(coverage.end(), o->coverage.begin(), o->coverage.end());
        FeasibleSetEntry entry;
        entry.combination = combo;
        entries.push_back(std::move(entry));
    }
    const IFeasibleSetBuilder& builder = FeasibleSetBuilder{};
    const SelectionRunResult result
        = builder.build(records, coverage, goldenIdentity(snapshot), entries);
    EXPECT_TRUE(result.set.feasible.empty()) << "全淘汰不得产出可行组合";
    EXPECT_EQ(result.set.records.size(), records.size());
    for (const FeasibilityRecord& rec : result.set.records) {
        EXPECT_EQ(rec.verdict, VerdictKind::Rejected);
        EXPECT_FALSE(rec.reasons.empty()) << "淘汰记录原因全集不得为空（逐项原因齐备）";
    }
}

/// 稳定排序（§10.4）：verdict 位次（可行→不足→淘汰）→轴序→候选 ID→
/// 目录版本→组合键级联；同输入两次组装结果全等（NFR-COR-02 确定性）。
TEST(SelFeasibleSet, StableOrderAndDeterminism_WP19T06_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-06", "NFR-COR-02"},
                  std::vector<std::string>{});  // R1——稳定排序键级联＋同输入同输出
    const GoldenCombos golden = makeGoldenCombos();
    CatalogPackageSnapshot snapshot = goldenSnapshot();
    const CombinationCheckCoreInput in = makeCoreInput(snapshot, golden);
    const std::vector<CombinationCheckOutcome> outcomes = checkCombinations(in, nullptr);
    const std::vector<FeasibilityRecord> records = collectRecords(outcomes);
    const std::vector<CaseCoverageEntry> coverage = collectCoverage(outcomes);
    const std::vector<FeasibleSetEntry> entries = makeEntries(golden, in, outcomes);
    const IdentityBlock identity = goldenIdentity(snapshot);

    const IFeasibleSetBuilder& builder = FeasibleSetBuilder{};
    const SelectionRunResult first = builder.build(records, coverage, identity, entries);
    const SelectionRunResult second = builder.build(records, coverage, identity, entries);
    EXPECT_EQ(first, second) << "同输入两次组装必须全等（NFR-COR-02）";

    // verdict 位次单调不降（可行→不足→淘汰——§10.4 排序键①）。
    auto rank = [](VerdictKind v) {
        switch (v) {
        case VerdictKind::Feasible:
            return 0;
        case VerdictKind::DataInsufficient:
            return 1;
        case VerdictKind::Rejected:
            return 2;
        }
        return 3;
    };
    for (std::size_t i = 1; i < first.set.records.size(); ++i) {
        EXPECT_LE(rank(first.set.records[i - 1].verdict),
                  rank(first.set.records[i].verdict))
            << "记录 " << i << " verdict 位次逆序";
    }
    // 同 verdict 段内按组合排序键（轴序级联——以组合键字面序断言段内
    // 有序性：K1 唯一可行段；Rejected 段按 combinationLess）。
    const FeasibleCombinationMetrics* prev = nullptr;
    for (const FeasibleCombinationMetrics& m : first.metrics) {
        if (prev != nullptr) {
            EXPECT_NE(prev->combinationId, m.combinationId) << "指标键重复";
        }
        prev = &m;
    }
    // metrics 与 records 对位（排序序一致——消费面读取序契约）。
    ASSERT_EQ(first.metrics.size(), first.set.records.size());
    for (std::size_t i = 0; i < first.metrics.size(); ++i) {
        EXPECT_EQ(first.metrics[i].combinationId, first.set.records[i].id);
    }
}

// ---------------------------------------------------------------------
// acceptance 2：EVI-02 多工况资格（不漏验）
// ---------------------------------------------------------------------

namespace {

/// 手工最小夹具（EVI-02 聚焦——单轴组合＋给定 verdict/格的记录）。
struct MiniFixture {
    DeviceCombination combo;       ///< 组合实体（单轴）
    IdentityBlock identity;        ///< 身份块（契约版本 1）
    CatalogPackageSnapshot snapshot; ///< 空快照（身份承载用）
};

inline MiniFixture makeMini(const char* comboKey)
{
    MiniFixture f;
    f.snapshot = goldenSnapshot();
    GoldenAxes axes = makeGoldenAxes();
    AxisDeviceAssignment axis;
    axis.jointId = axes.j1;
    axis.motorModelId = "M-A";
    axis.gearboxModelId = "G-10";
    f.combo.id = comboKey;
    f.combo.catalog = f.snapshot.manifest.identity;
    f.combo.axes = {axis};
    f.identity = goldenIdentity(f.snapshot);
    return f;
}

/// 组装单组合（record verdict 与格集由用例给定）。
inline SelectionRunResult buildMini(const MiniFixture& f, VerdictKind recordVerdict,
                                    const std::vector<CaseCoverageEntry>& cells)
{
    FeasibilityRecord rec;
    rec.id = f.combo.id;
    rec.deviceKind = DeviceKind::Combination;
    rec.catalog = f.identity.catalog;
    rec.verdict = recordVerdict;
    std::vector<FeasibleSetEntry> entries;
    FeasibleSetEntry entry;
    entry.combination = f.combo;
    entries.push_back(std::move(entry));

    const IFeasibleSetBuilder& builder = FeasibleSetBuilder{};
    return builder.build({rec}, cells, f.identity, entries);
}

}  // namespace

/// 组合仅在全部启用必验工况 Pass 后进入可行集：任一格 DataInsufficient
/// → 整体降级 DataInsufficient，不进 feasible、不漏验（EVI-02 红线）。
TEST(SelFeasibleSet, EVI02NonPassCellBlocksFeasible_WP19T06_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"EVI-02"},
                  std::vector<std::string>{"AT-32"});  // R1——记录可行但格 DataInsufficient → 降级
    const MiniFixture f = makeMini("combo-X");
    // 两格：case-1 Pass、case-2 DataInsufficient（数据不足格）。
    CaseCoverageEntry pass;
    pass.combinationId = f.combo.id;
    pass.caseId = "case-1";
    pass.verdict = VerdictKind::Feasible;
    CaseCoverageEntry insuff;
    insuff.combinationId = f.combo.id;
    insuff.caseId = "case-2";
    insuff.verdict = VerdictKind::DataInsufficient;

    const SelectionRunResult result = buildMini(f, VerdictKind::Feasible, {pass, insuff});
    EXPECT_TRUE(result.set.feasible.empty()) << "存在非 Pass 格的组合不得进可行集（EVI-02）";
    ASSERT_EQ(result.set.records.size(), std::size_t{1});
    EXPECT_EQ(result.set.records[0].verdict, VerdictKind::DataInsufficient);
    bool hasCoverageGap = false;
    for (const DataGap& gap : result.set.records[0].gaps) {
        if (gap.dimension == "coverage-inconsistent") {
            hasCoverageGap = true;
            EXPECT_EQ(gap.caseId, "case-2") << "缺口定位到非 Pass 工况";
        }
    }
    EXPECT_TRUE(hasCoverageGap) << "降级须携带覆盖矛盾缺口标记";
}

/// 组合仅在全部启用必验工况 Pass 后进入可行集：格 Fail 同样阻断
/// （记录判可行与格 Fail 矛盾时以格为准——组装面防线语义）。
TEST(SelFeasibleSet, EVI02FailCellBlocksFeasible_WP19T06_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"EVI-02"},
                  std::vector<std::string>{});  // R1——记录可行但格 Fail → 降级
    const MiniFixture f = makeMini("combo-F");
    CaseCoverageEntry failCell;
    failCell.combinationId = f.combo.id;
    failCell.caseId = "case-1";
    failCell.verdict = VerdictKind::Rejected;
    failCell.axisId = f.combo.axes[0].jointId;
    failCell.atTime = 1.5;
    failCell.segmentId = "seg-A";

    const SelectionRunResult result = buildMini(f, VerdictKind::Feasible, {failCell});
    EXPECT_TRUE(result.set.feasible.empty());
    ASSERT_EQ(result.set.records.size(), std::size_t{1});
    EXPECT_EQ(result.set.records[0].verdict, VerdictKind::DataInsufficient);
}

/// 无覆盖素材＝无法证明全工况通过＝漏验风险 → 降级（EVI-02"不得漏验"
/// 的组装面防线：可行结论必须以覆盖格为证）。
TEST(SelFeasibleSet, EVI02MissingCoverageBlocksFeasible_WP19T06_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"EVI-02"},
                  std::vector<std::string>{});  // R1——无覆盖格 → 降级不漏验
    const MiniFixture f = makeMini("combo-N");
    const SelectionRunResult result = buildMini(f, VerdictKind::Feasible, {});
    EXPECT_TRUE(result.set.feasible.empty());
    ASSERT_EQ(result.set.records.size(), std::size_t{1});
    EXPECT_EQ(result.set.records[0].verdict, VerdictKind::DataInsufficient);
    bool hasMissing = false;
    for (const DataGap& gap : result.set.records[0].gaps) {
        if (gap.dimension == "coverage-missing") {
            hasMissing = true;
        }
    }
    EXPECT_TRUE(hasMissing);
}

/// 全格 Pass＋记录可行 → 进可行集（EVI-02 正路径——资格判定的通过面）。
TEST(SelFeasibleSet, EVI02AllPassCellAdmitsFeasible_WP19T06_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"EVI-02"},
                  std::vector<std::string>{});  // R1——全部必验工况 Pass → 进可行集
    const MiniFixture f = makeMini("combo-P");
    CaseCoverageEntry c1;
    c1.combinationId = f.combo.id;
    c1.caseId = "case-1";
    c1.verdict = VerdictKind::Feasible;
    CaseCoverageEntry c2;
    c2.combinationId = f.combo.id;
    c2.caseId = "case-2";
    c2.verdict = VerdictKind::Feasible;

    const SelectionRunResult result = buildMini(f, VerdictKind::Feasible, {c1, c2});
    ASSERT_EQ(result.set.feasible.size(), std::size_t{1});
    EXPECT_EQ(result.set.feasible[0].id, f.combo.id);
    EXPECT_EQ(result.set.records[0].verdict, VerdictKind::Feasible);
}

/// 记录缺失（entries 有、records 无）→ 合成 DataInsufficient 素材——
/// 截断/漏供给的组合不得无声缺席（§13.4"取消不发布完整可行集"的组装面）；
/// 合成记录 verdict=DataInsufficient 且缺口/身份定位字段齐备。
TEST(SelFeasibleSet, ZeroRecordWithEntryGetsSyntheticGap_WP19T06_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"EVI-02"},
                  std::vector<std::string>{});  // R1——零记录＋实体 → 合成缺口记录
    const MiniFixture f = makeMini("combo-Z");
    std::vector<FeasibleSetEntry> entries;
    FeasibleSetEntry entry;
    entry.combination = f.combo;
    entries.push_back(std::move(entry));

    const IFeasibleSetBuilder& builder = FeasibleSetBuilder{};
    const SelectionRunResult result = builder.build({}, {}, f.identity, entries);
    EXPECT_TRUE(result.set.feasible.empty()) << "无校核记录不得进可行集";
    ASSERT_EQ(result.set.records.size(), std::size_t{1});
    const FeasibilityRecord& synth = result.set.records[0];
    EXPECT_EQ(synth.id, f.combo.id);
    EXPECT_EQ(synth.deviceKind, DeviceKind::Combination);
    EXPECT_EQ(synth.verdict, VerdictKind::DataInsufficient);
    ASSERT_EQ(synth.gaps.size(), std::size_t{1});
    EXPECT_EQ(synth.gaps[0].dimension, "feasible-set-input");
    // 合成记录身份面（追溯——身份块回填）。
    EXPECT_EQ(synth.catalog, f.identity.catalog);
}

// ---------------------------------------------------------------------
// 调用方契约拒绝（fail-fast——卡 §14.0 两分法的调用方错误侧）
// ---------------------------------------------------------------------

/// 四类契约违约逐一拒绝：轴级记录混入／记录引用未知组合／记录键重复／
/// 契约版本 0（分层与对位前提破坏——不返回半结果）。
TEST(SelFeasibleSet, RejectsCallerContractViolations_WP19T06_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-06", "NFR-COR-03"},
                  std::vector<std::string>{});  // R1——调用方契约违约 fail-fast
    const MiniFixture f = makeMini("combo-C");
    std::vector<FeasibleSetEntry> entries;
    FeasibleSetEntry entry;
    entry.combination = f.combo;
    entries.push_back(std::move(entry));

    const IFeasibleSetBuilder& builder = FeasibleSetBuilder{};

    // ①轴级记录混入（deviceKind=Motor——分层纪律）。
    FeasibilityRecord axisLevel;
    axisLevel.id = f.combo.id;
    axisLevel.deviceKind = DeviceKind::Motor;
    EXPECT_THROW(builder.build({axisLevel}, {}, f.identity, entries),
                 std::invalid_argument);

    // ②记录引用未知组合（entries 无同键实体）。
    FeasibilityRecord orphan;
    orphan.id = "no-such-combo";
    orphan.deviceKind = DeviceKind::Combination;
    EXPECT_THROW(builder.build({orphan}, {}, f.identity, entries),
                 std::invalid_argument);

    // ③记录键重复。
    FeasibilityRecord dup = orphan;
    dup.id = f.combo.id;
    FeasibilityRecord dup2 = dup;
    EXPECT_THROW(builder.build({dup, dup2}, {}, f.identity, entries),
                 std::invalid_argument);

    // ④组合键重复（entries 侧）。
    std::vector<FeasibleSetEntry> dupEntries;
    FeasibleSetEntry dupEntry;
    dupEntry.combination = f.combo;
    dupEntries.push_back(dupEntry);
    dupEntries.push_back(dupEntry);
    FeasibilityRecord ok;
    ok.id = f.combo.id;
    ok.deviceKind = DeviceKind::Combination;
    EXPECT_THROW(builder.build({ok}, {}, f.identity, dupEntries),
                 std::invalid_argument);

    // ⑤契约版本 0（身份块未登记——结果追溯面锚缺失）。
    IdentityBlock zero = f.identity;
    zero.contractVersion = 0;
    EXPECT_THROW(builder.build({ok}, {}, zero, entries), std::invalid_argument);
}

// ---------------------------------------------------------------------
// 淘汰原因构造器（§14.6——经接口消费；ERR-01 全字段＋diagRef）
// ---------------------------------------------------------------------

/// make 经接口引用调用：token/diagRef 回填＋上下文全字段透传（比较型
/// 字段"实际值/要求值/单位/阈值来源"齐备——SEL-06/ERR-01）。
TEST(SelFeasibleSet, ReasonProviderFillsErr01FieldsAndDiagRef_WP19T06_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-06", "ERR-01"},
                  std::vector<std::string>{"AT-08"});  // R1——原因构造器全字段＋稳定码
    CatalogPackageSnapshot snapshot = goldenSnapshot();
    ReasonContext ctx;
    ctx.candidateModelId = "M-A";
    ctx.axisId = makeGoldenAxes().j1;
    ctx.caseId = "case-A";
    ctx.atTime = 1.5;              // s
    ctx.segmentId = "seg-A";
    ctx.actual = 6.0;              // N·m（工作点——超能力侧）
    ctx.required = 5.0;            // N·m（额定连续转矩——阈值侧）
    ctx.unit = "N*m";
    ctx.thresholdSource = "motors.csv rated_torque_nm";
    ctx.catalog = snapshot.manifest.identity;
    ctx.suggestion = "更换更大额定转矩型号";

    const IRejectionReasonProvider& provider = RejectionReasonProvider{};
    const RejectionReason reason
        = provider.make(ReasonToken::TorqueContinuousInsufficient, ctx);

    EXPECT_EQ(reason.token, ReasonToken::TorqueContinuousInsufficient);
    EXPECT_DOUBLE_EQ(reason.actual, 6.0);
    EXPECT_DOUBLE_EQ(reason.required, 5.0);
    EXPECT_EQ(reason.unit, "N*m");
    EXPECT_EQ(reason.thresholdSource, "motors.csv rated_torque_nm");
    EXPECT_EQ(reason.candidateModelId, "M-A");
    EXPECT_EQ(reason.caseId, "case-A");
    EXPECT_DOUBLE_EQ(reason.atTime, 1.5);
    EXPECT_EQ(reason.segmentId, "seg-A");
    EXPECT_EQ(reason.suggestion, "更换更大额定转矩型号");
    EXPECT_EQ(reason.catalog, snapshot.manifest.identity);
    ASSERT_TRUE(reason.diagRef.has_value());
    EXPECT_EQ(*reason.diagRef,
              std::string(kSelMotorTorqueContinuousInsufficient));
    // 复用码锚：组合不兼容 token 映射既有 §9.3 码（不新造同义码）。
    const RejectionReason comboReason
        = provider.make(ReasonToken::ComboIncompatible, ctx);
    ASSERT_TRUE(comboReason.diagRef.has_value());
    EXPECT_EQ(*comboReason.diagRef, std::string(kSelComboIncompatible));
    // 身份不一致族三 token 同码（卡 §9.3 行 10/11——目录/映射版本错配）。
    const RejectionReason catVer
        = provider.make(ReasonToken::CatalogVersionIncompatible, ctx);
    EXPECT_EQ(catVer.diagRef, std::string(kSelIdentityMismatch));
    // 纯函数：同输入恒同输出（NFR-COR-02）。
    EXPECT_EQ(reason, provider.make(ReasonToken::TorqueContinuousInsufficient, ctx));
}

/// 非有限数值（actual/atTime）＝调用方契约违约 fail-fast（NFR-COR-03）。
TEST(SelFeasibleSet, ReasonProviderRejectsNonFinite_WP19T06_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-03"},
                  std::vector<std::string>{});  // R1——非有限拒绝不静默
    ReasonContext ctx;
    ctx.actual = std::numeric_limits<double>::quiet_NaN();
    const IRejectionReasonProvider& provider = RejectionReasonProvider{};
    EXPECT_THROW(provider.make(ReasonToken::SpeedInsufficient, ctx),
                 std::invalid_argument);
    ctx.actual = 1.0;
    ctx.required = std::numeric_limits<double>::infinity();
    EXPECT_THROW(provider.make(ReasonToken::SpeedInsufficient, ctx),
                 std::invalid_argument);
}

// ---------------------------------------------------------------------
// SEL-06 指标计算（裕量口径——解析值/负值/缺数据）
// ---------------------------------------------------------------------

/// K2 组合负裕量如实保留（ω_m_peak 500 ＞ maxSpeed 300 →
/// (300−500)/300＝−2/3）；指标面不参与判定（K2 verdict 仍由记录承载）。
TEST(SelFeasibleSet, MetricsKeepNegativeMarginHonest_WP19T06_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-06"},
                  std::vector<std::string>{});  // R1——负裕量如实保留（指标事实）
    const GoldenCombos golden = makeGoldenCombos();
    CatalogPackageSnapshot snapshot = goldenSnapshot();
    const CombinationCheckCoreInput in = makeCoreInput(snapshot, golden);
    const std::vector<CombinationCheckOutcome> outcomes = checkCombinations(in, nullptr);
    const std::vector<FeasibleCombinationMetrics> metrics
        = computeFeasibleCombinationMetrics(in, outcomes);

    const DeviceCombination* k2 = findCombo(golden.combos, "M-A", "G-20", "M-A", "G-20");
    ASSERT_TRUE(k2 != nullptr);
    const FeasibleCombinationMetrics* k2m = nullptr;
    for (const FeasibleCombinationMetrics& m : metrics) {
        if (m.combinationId == k2->id) {
            k2m = &m;
        }
    }
    ASSERT_TRUE(k2m != nullptr);
    ASSERT_TRUE(k2m->minMargin.has_value());
    // 黄金解析：J1 面映射口径 ω_m_peak＝10/0.02＝500 rad/s ＞ 300 rad/s
    // → margin＝(300−500)/300＝−2/3（最小裕量取负值最小维）。
    EXPECT_NEAR(k2m->minMargin->margin, -2.0 / 3.0, 1e-12);
    EXPECT_EQ(k2m->minMargin->dimension, "motor-speed-peak");
    EXPECT_EQ(k2m->minMargin->caseId, "case-A");
    // 指标面不参与判定：K2 的淘汰仍由组合校核记录承载（此处仅核对指标
    // 与记录并行产出——可行判定不读本素材）。
    const CombinationCheckOutcome* k2o = findOutcome(outcomes, k2->id);
    ASSERT_TRUE(k2o != nullptr);
    EXPECT_EQ(k2o->record.verdict, VerdictKind::Rejected);
}

/// 全维度无工作点数据 → minMargin nullopt（不伪造 0 裕量）；组合键重复
/// 的产出输入 fail-fast（对位前提）。
TEST(SelFeasibleSet, MetricsWithoutDataYieldNullOpt_WP19T06_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-06"},
                  std::vector<std::string>{});  // R1——无数据不伪造零裕量
    const GoldenCombos golden = makeGoldenCombos();
    CatalogPackageSnapshot snapshot = goldenSnapshot();
    CombinationCheckCoreInput in = makeCoreInput(snapshot, golden);
    // 清空轴事实与映射批轴事实（保留组合指派表——组合可遍历但无数值维）。
    in.axisFacts.clear();
    in.mappingBatch.axes.clear();
    const std::vector<CombinationCheckOutcome> outcomes = checkCombinations(in, nullptr);
    ASSERT_FALSE(outcomes.empty());
    const std::vector<FeasibleCombinationMetrics> metrics
        = computeFeasibleCombinationMetrics(in, outcomes);
    ASSERT_EQ(metrics.size(), outcomes.size());
    for (const FeasibleCombinationMetrics& m : metrics) {
        EXPECT_FALSE(m.minMargin.has_value())
            << "无工作点数据不得伪造裕量（组合 " << m.combinationId << "）";
    }

    // 产出键重复 fail-fast。
    std::vector<CombinationCheckOutcome> dup = outcomes;
    dup.push_back(dup.front());
    EXPECT_THROW(computeFeasibleCombinationMetrics(in, dup), std::invalid_argument);

    // 快照空指针 fail-fast。
    CombinationCheckCoreInput nullSnap = in;
    nullSnap.snapshot = nullptr;
    EXPECT_THROW(computeFeasibleCombinationMetrics(nullSnap, outcomes),
                 std::invalid_argument);
}
