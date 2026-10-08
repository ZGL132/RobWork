/**
 * @file   CombinationCheckContractTest.cpp
 * @brief  组合校核契约测试组（SelCombinationCheckContract）——公共接口
 *         交付面钉扎（经 IDeviceCombinationBuilder/IEngineeringEvaluator
 *         接口分派消费）、SEL-05 红线机器可断言面（不自建映射实现——
 *         产品面零映射公式/零他单元 include 扫描）、惯量比未裁决不阻断
 *         语义、ERR-01 原因字段完备性与词表封闭性。
 *
 * 设计依据：
 *   - units/selection.md §9.1（"selection 不得重新计算"红线——机器可断言
 *     形态）、§11.3（未裁决期保守行为）、§10.3（词表封闭）、§3.2（零
 *     drivetrain 编译边——P-SEL-2 推荐端口形态）、§14.5（接口契约）
 *   - 需求 SEL-05、DYN-04（消费）、ERR-01、NFR-MNT-07（无重复算法）
 *   - 任务契约 tasks/foundation/WP-19-T05.json acceptance 2（不自建映射
 *     实现；惯量比阈值为可配置工程规则；未判定维度不整体阻断）
 *
 * 与单元测试的分工：单元测试管黄金值与判定正确性；本契约文件管"合同
 * 面"——接口分派可达性、红线扫描、词表稳定性、字段完备性。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/evidence/Evaluator.hpp>
#include <sdurws/ird/selection/CatalogProvider.hpp>
#include <sdurws/ird/selection/CatalogTypes.hpp>
#include <sdurws/ird/selection/Combination.hpp>
#include <sdurws/ird/selection/DiagCodes.hpp>
#include <sdurws/ird/selection/Screening.hpp>

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO

#include <filesystem>
#include <fstream>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

using namespace sdurws::ird::selection;
using namespace sdurws::ird;

#ifndef IRD_SELECTION_UNIT_ROOT
#error "契约测试需要 IRD_SELECTION_UNIT_ROOT 注入（CMake 编译定义）"
#endif

namespace {

/// 产品面源码收集（include/**＋src/**——门禁 R 系列的扫描域同口径）。
std::vector<std::string> collectProductSources()
{
    std::vector<std::string> files;
    const std::string root = IRD_SELECTION_UNIT_ROOT;
    const std::vector<std::string> dirs = {root + "/selection/include",
                                            root + "/selection/src"};
    for (const std::string& dir : dirs) {
        // 非递归足够（selection 产品面两层结构固定——include/sdurws/ird/
        // selection/ 与 src/ 各一层；新增子目录须同步扩展此扫描）。
        for (const auto& entry : std::filesystem::directory_iterator(dir)) {
            if (entry.is_regular_file()) {
                files.push_back(entry.path().string());
            }
        }
    }
    return files;
}

/// 读文件全文（文本模式——UTF-8 原样）。
std::string readFile(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
}

// ---- 自持最小夹具（契约测试不复用 test/ 支撑头——HardScreeningContract
//      Test 同款先例；单轴单候选对的最小判定面足够行使合同语义）。----

/// 契约目录身份。
CatalogIdentity contractCatalog()
{
    CatalogIdentity c;
    c.catalogId = "cat-cc";
    c.version = "v1";
    c.source = "组合校核契约测试";
    return c;
}

/// 契约电机（能力值宽松——契约面只关心结构语义，不判边界）。
MotorCatalogEntry contractMotor()
{
    MotorCatalogEntry m;
    m.modelId = "M-X";
    m.vendor = "CC";
    m.displayName = "契约电机";
    m.catalog = contractCatalog();
    m.ratedTorque = 100.0;   // N·m（宽松——任何合理工作点都通过）
    m.peakTorque = 200.0;    // N·m
    m.ratedSpeed = 500.0;    // rad/s
    m.maxSpeed = 1000.0;     // rad/s
    m.ratedPower = 5000.0;   // W
    m.dutyClass = "S1";
    m.rotorInertia = 0.01;   // kg·m²
    m.mass = 3.0;            // kg
    m.mounting = MountSpec{"flangeA", "shaftB"};
    m.status = ValidationStatus::Valid;
    return m;
}

/// 契约减速器。
GearboxCatalogEntry contractGearbox()
{
    GearboxCatalogEntry g;
    g.modelId = "G-X";
    g.vendor = "CC";
    g.displayName = "契约减速器";
    g.catalog = contractCatalog();
    g.ratedOutputTorque = 500.0;  // N·m
    g.peakOutputTorque = 1000.0;  // N·m
    g.maxInputSpeed = 1000.0;     // rad/s
    g.ratio = 10.0;               // n:1（c＝0.1）
    g.efficiency = 0.9;
    g.mountingOrientation = "flangeA";
    g.mass = 2.0;                 // kg
    g.mounting = MountSpec{"flangeA", "shaftB"};
    g.status = ValidationStatus::Valid;
    return g;
}

/// 契约快照（单候选对）。
CatalogPackageSnapshot contractSnapshot()
{
    CatalogPackageSnapshot s;
    s.manifest.formatVersion = kCatalogFormatVersion;
    s.manifest.identity = contractCatalog();
    s.motors = {contractMotor()};
    s.gearboxes = {contractGearbox()};
    s.compatibility = {CompatibilityRecord{"M-X", "G-X", "flangeA"}};
    return s;
}

/// 契约映射批（单组合单轴事实——宽松能力下的全通过面）。
MappingBatchFacts contractMappingBatch(const DeviceCombination& combo,
                                       const core::ObjectId& axis)
{
    MappingBatchFacts batch;
    batch.mappingContractVersion = 1;
    batch.mappingAlgorithmVersion = 1;
    batch.completeness = CompletenessKind::Complete;
    MappingCombinationFact cf;
    cf.combinationId = combo.id;
    cf.catalog = combo.catalog;
    cf.axes = combo.axes;
    batch.combinations.push_back(cf);
    MappingAxisFact a;
    a.combinationId = combo.id;
    a.jointId = axis;
    a.caseId = "case-1";
    a.motorTorqueRms = 1.0;    // N·m（远低于 M-X 能力——全维通过面）
    a.motorTorquePeak = 2.0;   // N·m
    a.motorSpeedPeak = 50.0;   // rad/s
    a.motorSpeedRms = 30.0;    // rad/s
    a.motorPowerPeak = 100.0;  // W
    a.motorPowerRms = 60.0;    // W
    a.peakDuration = 1.0;      // s
    a.peakAtTime = 0.5;        // s
    a.peakSegmentId = "seg-1";
    a.inertiaRatio = 2.0;      // 无量纲（映射数值事实）
    a.reflectedInertia = 1.0;  // kg·m²
    a.efficiencyApplied = true;
    batch.axes.push_back(a);
    return batch;
}

/// 契约组合夹具（单轴单组合——build 产出与映射批同源）。
struct ComboFixture {
    CatalogPackageSnapshot snapshot = contractSnapshot();
    core::ObjectId axis = core::ObjectId::generate();
    std::vector<DeviceCombination> combos;
};

ComboFixture makeComboFixture()
{
    ComboFixture fx;
    DeviceCombinationBuilder builder;
    fx.combos = builder
                    .build({AxisCandidateList{fx.axis, {"M-X"}, {"G-X"}}},
                           fx.snapshot.compatibility, BatchBudget{8},
                           fx.snapshot.manifest.identity)
                    .combinations;
    return fx;
}

/// 取消查询替身（从不取消——evidence §11 测试设施形态）。
class NeverCancelContext final : public evidence::IEvaluationContext {
public:
    bool cancellationRequested() const override { return false; }
    void reportProgress(std::uint8_t, std::string_view) override {}
    std::optional<std::vector<std::uint8_t>> tryObjectBytes(core::ObjectId,
                                                            core::ContentVersion)
        const override
    {
        return std::nullopt;
    }
};

}  // namespace

// ---------------------------------------------------------------------
// 接口交付面钉扎（WP-20-T03 接口路径零覆盖教训前移）
// ---------------------------------------------------------------------

/// 组合构造经 IDeviceCombinationBuilder 接口分派可达（交付面＝接口，
/// 实现类只是其唯一产品实现——不留只测实现类的盲区）。
TEST(SelCombinationCheckContract, BuilderInterfaceDispatchable)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-05"}, std::vector<std::string>{"AT-08"});  // R1——接口交付面——IDeviceCombinationBuilder 分派
    const ComboFixture fx = makeComboFixture();
    // 经接口引用消费（多态分派——非实现类静态调用）。
    const IDeviceCombinationBuilder& builder = DeviceCombinationBuilder{};
    const CombinationSet set = builder.build(
        {AxisCandidateList{fx.axis, {"M-X"}, {"G-X"}}},
        fx.snapshot.compatibility, BatchBudget{8}, fx.snapshot.manifest.identity);
    ASSERT_EQ(set.combinations.size(), std::size_t{1});
    EXPECT_EQ(set.combinations[0].axes.size(), std::size_t{1});
    EXPECT_EQ(set.combinations[0].axes[0].motorModelId, "M-X");
}

/// 组合校核核心经公共值面消费（CombinationCheckCoreInput——直调面契约；
/// 评估器分派面另有用例）＋取消查询通道复用 evidence::IEvaluationContext。
TEST(SelCombinationCheckContract, CoreConsumableThroughPublicValueFace)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-05"}, std::vector<std::string>{"AT-08"});  // R1——核心公共值面——直调消费契约
    const ComboFixture fx = makeComboFixture();
    CombinationCheckCoreInput in;
    in.snapshot = &fx.snapshot;
    AxisWorkpointFacts facts;
    facts.jointId = fx.axis;
    facts.caseId = "case-1";
    facts.jointTorquePeak = 50.0;   // N·m（宽松能力下全维通过）
    facts.jointTorqueRms = 20.0;    // N·m
    facts.jointSpeedPeak = 5.0;     // rad/s
    in.axisFacts = {facts};
    in.mappingBatch = contractMappingBatch(fx.combos[0], fx.axis);
    in.criteria = ScreeningCriteria{};
    NeverCancelContext ctx;
    const std::vector<CombinationCheckOutcome> outcomes = checkCombinations(in, &ctx);
    ASSERT_EQ(outcomes.size(), std::size_t{1});
    EXPECT_EQ(outcomes[0].record.verdict, VerdictKind::Feasible);
}

/// SEL-09 范围外契约（WP-19-T08）：经公共值面消费——移动关节轴的组合
/// 产出零淘汰原因（不静默套用旋转传动）＋DataInsufficient 语义＋稳定码
/// SEL-INPUT-AXIS-OUT-OF-SCOPE 缺口；词表映射同码（reasonTokenDiagCode
/// 唯一映射点）——SEL-09 语义的机器可断言红线面。
TEST(SelCombinationCheckContract, OutOfScopeAxisNeverYieldsRejectionReason)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-09"}, std::vector<std::string>{"AT-08"});  // R1——SEL-09 红线——范围外轴零淘汰原因＋DataInsufficient＋稳定码
    const ComboFixture fx = makeComboFixture();
    CombinationCheckCoreInput in;
    in.snapshot = &fx.snapshot;
    AxisWorkpointFacts facts;
    facts.jointId = fx.axis;
    facts.caseId = "case-1";
    facts.jointKind = JointKind::Prismatic;  // 移动关节——范围外信号
    // 超限旋转工作点（反证面——若套用旋转传动必产生淘汰原因）。
    facts.jointTorquePeak = 999.0;   // N·m
    facts.jointTorqueRms = 888.0;    // N·m
    facts.jointSpeedPeak = 777.0;    // rad/s
    in.axisFacts = {facts};
    in.mappingBatch = contractMappingBatch(fx.combos[0], fx.axis);
    in.criteria = ScreeningCriteria{};
    const std::vector<CombinationCheckOutcome> outcomes = checkCombinations(in, nullptr);
    ASSERT_EQ(outcomes.size(), std::size_t{1});
    const FeasibilityRecord& rec = outcomes[0].record;
    // 红线一：零淘汰原因（旋转传动维度未执行——含映射事实供给也不消费）。
    EXPECT_TRUE(rec.reasons.empty())
        << "移动关节轴不得产生任何旋转传动淘汰原因（SEL-09 不静默套用）";
    // 红线二：DataInsufficient 语义（不是 Feasible——范围外不得当通过；
    // 不是 Rejected——范围外是数据/边界类事实，不升级整机不可行）。
    EXPECT_EQ(rec.verdict, VerdictKind::DataInsufficient);
    // 红线三：范围外缺口携带登记表稳定码（词表映射唯一实现点同码）。
    ASSERT_EQ(rec.gaps.size(), std::size_t{1});
    EXPECT_EQ(rec.gaps[0].diagCode, std::string(kSelInputAxisOutOfScope));
    EXPECT_EQ(reasonTokenDiagCode(ReasonToken::AxisOutOfScope),
              kSelInputAxisOutOfScope);
}

/// 评估器经 IEngineeringEvaluator 接口分派可达（③端口被调方契约——
/// 工厂 create 产出接口指针后按接口消费全流程）。
TEST(SelCombinationCheckContract, EvaluatorInterfaceDispatchable)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-05"}, std::vector<std::string>{"AT-38"});  // R1——接口交付面——IEngineeringEvaluator 分派
    InMemoryCatalogProvider provider;
    CatalogPackageSnapshot snapshot = contractSnapshot();
    snapshot.contentIdentity = computePackageContentIdentity(snapshot);
    snapshot.manifest.identity.contentIdentity = snapshot.contentIdentity;
    (void)provider.lockVersion(snapshot, core::ObjectId::generate());
    // 经工厂创建并按接口消费（descriptor 访问——分派面最小行使）。
    CombinationCheckEvaluatorFactory factory(provider);
    std::unique_ptr<evidence::IEngineeringEvaluator> evaluator = factory.create();
    ASSERT_TRUE(evaluator != nullptr);
    const evidence::EvaluatorDescriptor d = evaluator->descriptor();
    EXPECT_EQ(d.key, "sel-combination-check");
    // 依赖声明含 dt.mapping UpstreamResult（③端口消费声明面——SEL-05）。
    bool declaresMapping = false;
    for (const evidence::DependencyDeclaration& dep : d.inputs) {
        if (dep.key == "dt.mapping"
            && dep.kind == evidence::DependencyKind::UpstreamResult) {
            declaresMapping = true;
        }
    }
    EXPECT_TRUE(declaresMapping) << "组合校核评估器必须经③端口声明映射依赖";
}

// ---------------------------------------------------------------------
// SEL-05 红线机器可断言面（不自建映射实现）
// ---------------------------------------------------------------------

/// 产品面零他单元 include（零 drivetrain 编译边——P-SEL-2 推荐端口形态；
/// 零 dynamics/modeling 等 R-2 业务间边——卡 §3.2 边表）。
TEST(SelCombinationCheckContract, ProductFaceHasNoCrossUnitIncludes)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-05"}, std::vector<std::string>{"AT-38"});  // R1——SEL-05 红线——零他单元编译边
    for (const std::string& path : collectProductSources()) {
        const std::string text = readFile(path);
        EXPECT_EQ(text.find("#include <sdurws/ird/drivetrain/"), std::string::npos)
            << "产品面包含 drivetrain 头（零编译边红线——" << path << "）";
        EXPECT_EQ(text.find("#include <sdurws/ird/dynamics/"), std::string::npos)
            << path;
        EXPECT_EQ(text.find("#include <sdurws/ird/modeling/"), std::string::npos)
            << path;
        EXPECT_EQ(text.find("#include <sdurws/ird/runtime/"), std::string::npos)
            << path;
        EXPECT_EQ(text.find("#include <sdurws/ird/policy/"), std::string::npos)
            << path;
        EXPECT_EQ(text.find("#include <sdurws/ird/project/"), std::string::npos)
            << path;
    }
}

/// 产品面零映射公式词表（不自建映射实现的机器可断言面——传动比映射/
/// 虚功/电机侧力矩速度功率计算/反射惯量公式的表达式词表零命中；映射
/// 事实【字段名消费】除外——红线禁止的是计算，不是事实承载）。
TEST(SelCombinationCheckContract, ProductFaceHasNoMappingFormulaTokens)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-05"}, std::vector<std::string>{"AT-38"});  // R1——SEL-05 红线——零映射公式词表
    // 词表（计算表达式特征——映射输出的唯一产生者是 drivetrain 映射核）：
    //   乘除传动比的表达式 / 电机侧速度/加速度符号 / 虚功记法 / drivetrain
    //   映射核符号。
    const std::vector<std::string> formulaTokens = {
        "ratioC *", "* ratioC", "/ ratioC", "ratioC*", "*c *", "thetaDDot",
        "thetaDot =", "tauIdeal", "pRotor", "Cᵀ", "virtualWork",
        "DriveTrainMappingCore", "JointSeriesView", "ReflectedInertiaResult",
    };
    for (const std::string& path : collectProductSources()) {
        const std::string text = readFile(path);
        for (const std::string& token : formulaTokens) {
            EXPECT_EQ(text.find(token), std::string::npos)
                << "产品面命中映射公式词表 «" << token << "»（" << path
                << "——不自建映射实现，SEL-05/§9.1）";
        }
    }
}

/// 候选传动参数构造的 c＝1/n 单点换算在唯一书写点（makeCombinationDrive-
/// Inputs——§9.1"候选传动参数构造"允许面；第二处书写即红线命中）。
TEST(SelCombinationCheckContract, RatioConversionIsSingleSite)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-05"}, std::vector<std::string>{"AT-08"});  // R1——c＝1/n 换算——单点书写
    std::size_t sites = 0;
    for (const std::string& path : collectProductSources()) {
        const std::string text = readFile(path);
        std::size_t pos = text.find("1.0 / gearbox->ratio");
        while (pos != std::string::npos) {
            ++sites;
            pos = text.find("1.0 / gearbox->ratio", pos + 1);
        }
    }
    EXPECT_EQ(sites, std::size_t{1})
        << "c＝1/n 换算必须在 makeCombinationDriveInputs 单点书写（禁第二处）";
}

// ---------------------------------------------------------------------
// 惯量比未裁决语义（§11.3——不整体阻断）
// ---------------------------------------------------------------------

/// 规则未配置（R1 默认）⇒ 未判定标记置位且组合可行不受阻（该维度不是
/// 需求 §8.1 表 4 选型域必需项）；规则配置也不产生淘汰原因（词表封闭）。
TEST(SelCombinationCheckContract, InertiaRatioNeverBlocksVerdict)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-05"}, std::vector<std::string>{"AT-08"});  // R1——惯量比未裁决——未判定维度不整体阻断（P-SEL-4/P-POL-3）
    const ComboFixture fx = makeComboFixture();
    CombinationCheckCoreInput base;
    base.snapshot = &fx.snapshot;
    AxisWorkpointFacts facts;
    facts.jointId = fx.axis;
    facts.caseId = "case-1";
    facts.jointTorquePeak = 50.0;   // N·m
    facts.jointTorqueRms = 20.0;    // N·m
    facts.jointSpeedPeak = 5.0;     // rad/s
    base.axisFacts = {facts};
    base.mappingBatch = contractMappingBatch(fx.combos[0], fx.axis);
    base.criteria = ScreeningCriteria{};
    // 未配置（默认）。
    const std::vector<CombinationCheckOutcome> unsettled =
        checkCombinations(base, nullptr);
    ASSERT_EQ(unsettled.size(), std::size_t{1});
    EXPECT_TRUE(unsettled[0].inertiaRatioUnsettled);
    EXPECT_EQ(unsettled[0].record.verdict, VerdictKind::Feasible);
    // 配置极端严参考值 0.001（全部惯量比超参考）——仍不产生淘汰原因、
    // 不改变 verdict（参考语义——§11.3"不作为淘汰原因、不作为可行依据"）。
    base.inertiaRule.referenceMaxRatio = 0.001;
    const std::vector<CombinationCheckOutcome> withStrictRef =
        checkCombinations(base, nullptr);
    EXPECT_EQ(withStrictRef[0].record.verdict, VerdictKind::Feasible);
    EXPECT_TRUE(withStrictRef[0].record.reasons.empty());
    // InertiaRatioRule 结构不携带任何默认阈值数字（P-DT-2/P-SEL-4——
    // 未配置即 nullopt，无写死值）。
    InertiaRatioRule fresh;
    EXPECT_FALSE(fresh.referenceMaxRatio.has_value());
    EXPECT_TRUE(fresh.source.empty());
}

/// 未判定 token 的词表文本钉扎（§10.3 封闭词表——inertia-ratio-policy-
/// unsettled 为显式未判定语义，非淘汰）。
TEST(SelCombinationCheckContract, UnsettledTokenTextIsStable)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-05"}, std::vector<std::string>{"AT-08"});  // R1——词表封闭——未判定 token 文本钉扎
    EXPECT_EQ(reasonTokenText(ReasonToken::InertiaRatioPolicyUnsettled),
              "inertia-ratio-policy-unsettled");
    EXPECT_EQ(reasonTokenText(ReasonToken::ComboIncompatible), "combo-incompatible");
    EXPECT_EQ(reasonTokenText(ReasonToken::AxisMappingIncomplete),
              "axis-mapping-incomplete");
    EXPECT_EQ(reasonTokenText(ReasonToken::CatalogVersionIncompatible),
              "catalog-version-incompatible");
}

/// §9.3 三码已登记（DiagCodes 登记表——T02 物化；诊断产出面的码值来源）。
TEST(SelCombinationCheckContract, ComboDiagCodesRegistered)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-05"}, std::vector<std::string>{"AT-08"});  // R1——§9.3 三码——登记表物化核对
    EXPECT_EQ(kSelComboIncompatible, "SEL-COMBO-INCOMPATIBLE");
    EXPECT_EQ(kSelComboAxisMappingIncomplete, "SEL-COMBO-AXIS-MAPPING-INCOMPLETE");
    EXPECT_EQ(kSelIdentityMismatch, "SEL-IDENTITY-MISMATCH");
}

// ---------------------------------------------------------------------
// ERR-01 原因字段完备性（组合级原因全字段）
// ---------------------------------------------------------------------

/// 组合级淘汰原因携带比较型字段齐备（实际/要求/单位/阈值来源/建议动作
/// /目录版本/切片与映射身份——§10.3"每条原因包含全部字段"）。
TEST(SelCombinationCheckContract, ComboReasonsCarryComparativeFields)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-06"}, std::vector<std::string>{"ERR-01"});  // R1——组合级原因——ERR-01 字段完备性
    const ComboFixture fx = makeComboFixture();
    CombinationCheckCoreInput in;
    in.snapshot = &fx.snapshot;
    AxisWorkpointFacts facts;
    facts.jointId = fx.axis;
    facts.caseId = "case-1";
    facts.jointTorquePeak = 50.0;   // N·m
    facts.jointTorqueRms = 20.0;    // N·m
    facts.jointSpeedPeak = 5.0;     // rad/s
    in.axisFacts = {facts};
    in.mappingBatch = contractMappingBatch(fx.combos[0], fx.axis);
    in.criteria = ScreeningCriteria{};
    in.mappingBatch.combinations[0].catalog.version = "v0";  // 目录错配——组合级原因面。
    const std::vector<CombinationCheckOutcome> outcomes = checkCombinations(in, nullptr);
    ASSERT_EQ(outcomes.size(), std::size_t{1});
    ASSERT_FALSE(outcomes[0].record.reasons.empty());
    for (const RejectionReason& r : outcomes[0].record.reasons) {
        EXPECT_FALSE(r.thresholdSource.empty()) << "阈值来源必填";
        EXPECT_FALSE(r.suggestion.empty()) << "建议动作必填（ERR-01）";
        EXPECT_FALSE(r.requiredText.empty());
        EXPECT_EQ(r.catalog, fx.snapshot.manifest.identity);
        // diagRef 恒 nullopt（逐 token 稳定码映射随 WP-19-T06——T04 口径）。
        EXPECT_FALSE(r.diagRef.has_value());
    }
}
