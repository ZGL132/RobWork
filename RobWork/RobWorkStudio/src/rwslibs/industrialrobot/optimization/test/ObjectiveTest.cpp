/**
 * @file   ObjectiveTest.cpp
 * @brief  目标指标分层与三项静态指标模型测试（OptObjective 组）——八项
 *         指标词表、默认激活分期、目标校验（阶段锁/评估器注册）、config.opt
 *         目标段 canonical 序列化与三项静态指标计算（值黄金对照＋缺失
 *         "—"语义）——任务契约 WP-20-T05 acceptance 2（OPT-VER-124/125/126
 *         观测点；OPT-07/NFR-COR-03/ERR-01 语义钉扎）。
 *
 * 设计依据：
 *   - units/optimization.md §7.1（八项指标定义表——token/方向/单位/来源键/
 *     阶段可算性逐行黄金）、§7.2（"—"不显示零；非有限不静默）、§7.3
 *     （默认激活分期与四条件静态子集）、§4.3（canonical 目标段——位图＋
 *     每目标容差三元组）、§13.1（OPT-VER-124 三项静态指标计算/125 其他
 *     五项显示"—"/126 缺失指标不当作零）
 *   - 需求 OPT-07（八项全部展示；默认激活分期）、NFR-COR-03（缺失不静默
 *     转 0）、ERR-01（不因空判动力学/器件不可行）
 *   - 用例名与断言注释带需求/AT 追溯（AGENTS §2.7）；数值断言给解析期望
 *
 * 测试形态（模型测试＝直调计算库，NFR-MNT-01）：指标计算为纯函数直调；
 * validateObjectives 的注册表条件以真 evidence::EvaluatorRegistry＋脚本化
 * 评估器替身承载（T04 同款形态——消费路径与生产一致，虚函数路径天然
 * 钉住，防"只测自由函数"盲区——WP-20-T03 B-1 教训）。
 */

#include <sdurws/ird/optimization/Objective.hpp>

#include <sdurws/ird/core/Compare.hpp>
#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/evidence/Evaluator.hpp>
#include <sdurws/ird/optimization/DiagCodes.hpp>
#include <sdurws/ird/optimization/Types.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace sdurws::ird;
using optimization::computeStaticMetrics;
using optimization::defaultObjectives;
using optimization::EnvelopeFacts;
using optimization::IOptimizationObjectiveProvider;
using optimization::LinkMassFact;
using optimization::makeObjectiveSet;
using optimization::canonicalizeObjectiveSegment;
using optimization::metricDefinitions;
using optimization::MetricComputation;
using optimization::MetricDefinition;
using optimization::MetricDirection;
using optimization::MetricId;
using optimization::ObjectiveEntry;
using optimization::ObjectiveSet;
using optimization::ObjectiveValidationReport;
using optimization::OptimizationError;
using optimization::OptimizationObjectiveProvider;
using optimization::OptimizationStage;
using optimization::PointMarginFact;
using optimization::StaticMetricFacts;
using optimization::StaticMetricResult;
using optimization::kMetricCount;
using optimization::kMetricGapNonFinite;
using optimization::kMetricGapPartialData;
using optimization::kMetricGapSourceMissing;
using optimization::kMetricGapStage;
using optimization::kOptEvaluatorMissing;
using optimization::kOptInputInvalid;
using optimization::kOptStageLocked;
using optimization::kOptStaticScreenKey;

namespace {

// =====================================================================
// 构造辅助
// =====================================================================

/// 非零指标 ID 位图辅助——按 MetricId 枚举序构造目标条目（容差可选）。
ObjectiveEntry entryOf(MetricId id, double rel = 0.0, double abs = 0.0)
{
    ObjectiveEntry e;
    e.metricId = id;
    e.tolerance.relative = rel;
    e.tolerance.absolute = abs;
    return e;
}

/// 注册表构造辅助：真 evidence::EvaluatorRegistry＋"opt-static-screen"
/// 脚本化评估器（descriptor 按 §6.7 要点——契约版本 1；Profile 先行注册
/// ——接入顺序）。返回 unique_ptr 由调用方持有（validateObjectives 只读
/// 消费指针）。
struct RegistryFixture
{
    std::unique_ptr<evidence::EvidenceProfileRegistry> profiles;
    std::unique_ptr<evidence::EvaluatorRegistry> registry;

    RegistryFixture()
    {
        // Profile "opt" v1.0 先行注册（§6.7/§13 接入顺序——评估器注册的
        // 前置；空必需项集——注册期完备性核对平凡通过）。
        profiles = std::make_unique<evidence::EvidenceProfileRegistry>();
        evidence::RequiredEvidenceProfile profile;
        profile.profileId = "opt";
        profile.version = "1.0";
        profiles->registerProfile(profile);

        // 脚本化评估器工厂：descriptor 按域聚合评估器要点（§6.7 表——
        // key/contractVersion 1/双模式/stateless；Profile 绑定 "opt"）。
        // evaluate() 不会被调用（validateObjectives 只查注册面）。
        class ScriptedFactory final : public evidence::IEvaluatorFactory {
        public:
            const evidence::EvaluatorDescriptor& descriptor() const override
            {
                static evidence::EvaluatorDescriptor d = [] {
                    evidence::EvaluatorDescriptor desc;
                    desc.key = std::string(optimization::kOptStaticScreenKey);
                    desc.contractVersion = 1U;
                    desc.profile.profileId = "opt";
                    desc.profile.version = "1.0";
                    desc.supportedModes = {core::EvaluationMode::Quick,
                                           core::EvaluationMode::Verified};
                    desc.stateless = true;
                    desc.threadSafety = evidence::ThreadSafety::FullyThreadSafe;
                    return desc;
                }();
                return d;
            }
            std::unique_ptr<evidence::IEngineeringEvaluator> create() const override
            {
                return nullptr;  // 校验面不实例化评估器（find 命中即可）。
            }
        };
        registry = std::make_unique<evidence::EvaluatorRegistry>(*profiles);
        registry->registerEvaluator(std::make_unique<ScriptedFactory>(), {});
    }
};

/// 三项静态指标黄金基线（解析值）：
///   包络：AABB (0,0,0)-(1,2,3) → 三向尺寸和 = 1+2+3 = 6.0 m；
///   质量：三连杆 2.5+1.0+0.5 = 4.0 kg；
///   裕量：三工位 min(0.8, 0.3, 0.5) = 0.3（归一化无量纲）。
StaticMetricFacts makeGoldenFacts()
{
    StaticMetricFacts facts;
    EnvelopeFacts env;
    env.xMin = 0.0; env.yMin = 0.0; env.zMin = 0.0;
    env.xMax = 1.0; env.yMax = 2.0; env.zMax = 3.0;
    facts.envelope = env;
    facts.linkMasses = {
        {"obj-link-1", 2.5, "provided"},
        {"obj-link-2", 1.0, "provided"},
        {"obj-link-3", 0.5, "provided"},
    };
    facts.pointMargins = {
        {"case-must-1", 0.8, "1"},
        {"case-must-2", 0.3, "1"},
        {"case-must-3", 0.5, "1"},
    };
    return facts;
}

/// 按指标取记录（测试断言辅助——metrics 恒八项全量枚举序）。
const MetricComputation& metricOf(const StaticMetricResult& r, MetricId id)
{
    return r.metrics[static_cast<std::size_t>(id)];
}

}  // namespace

// =====================================================================
// 词表与默认激活（OPT-07 词表面）
// =====================================================================

/// OPT-07：八项指标词表全量黄金对照（token/方向/单位/来源键/阶段可算性
/// ——§7.1 表逐行；token 冻结＝消费面契约）。
TEST(OptObjective, MetricDefinitionsGoldenTable)
{
    IRD_TEST_INFO("OPT-07", {}, std::nullopt);
    const std::vector<MetricDefinition> defs = metricDefinitions();
    ASSERT_EQ(defs.size(), kMetricCount) << "词表须八项全量（§7.1 表）";

    // 黄金表（§7.1 表行序＝枚举序）：token/方向/单位/来源键/StageB 可算。
    const struct {
        MetricId id;
        const char* token;
        MetricDirection dir;
        const char* unit;
        const char* sourceKey;
        bool stageBComputable;
    } golden[kMetricCount] = {
        {MetricId::Envelope,          "opt.metric.envelope",          MetricDirection::Minimize, "m",  "opt-static-screen",     true},
        {MetricId::StructuralMass,    "opt.metric.structural-mass",   MetricDirection::Minimize, "kg", "opt-static-screen",     true},
        {MetricId::MinJointMargin,    "opt.metric.min-joint-margin",  MetricDirection::Maximize, "1",  "opt-static-screen",     true},
        {MetricId::CycleTime,         "opt.metric.cycle-time",        MetricDirection::Minimize, "s",  "trj-sequence-plan",     false},
        {MetricId::DeviceCost,        "opt.metric.device-cost",       MetricDirection::Minimize, "",   "sel-combination-check", false},
        {MetricId::DeviceMass,        "opt.metric.device-mass",       MetricDirection::Minimize, "kg", "sel-combination-check", false},
        {MetricId::JointPositiveWork, "opt.metric.joint-positive-work", MetricDirection::Minimize, "J", "dyn-rnea-analysis",     false},
        {MetricId::MinDriveMargin,    "opt.metric.min-drive-margin",  MetricDirection::Maximize, "1",  "sel-combination-check", false},
    };
    for (std::size_t i = 0; i < kMetricCount; ++i) {
        EXPECT_EQ(defs[i].metricId, golden[i].id) << "第 " << i << " 项枚举序";
        EXPECT_EQ(optimization::toToken(defs[i].metricId), golden[i].token)
            << "token 冻结（§7.1）";
        EXPECT_EQ(defs[i].direction, golden[i].dir) << "方向冻结（§7.2）";
        EXPECT_EQ(defs[i].unitToken, golden[i].unit) << "单位（P-OPT-5 口径）";
        EXPECT_EQ(defs[i].sourceKey, golden[i].sourceKey) << "来源评估键";
        EXPECT_EQ(defs[i].computableInStageB, golden[i].stageBComputable)
            << "阶段可算性（§7.1）";
        // 词表方向与直接查询同一来源（单点消费——防两处口径漂移）。
        EXPECT_EQ(optimization::metricDirectionOf(defs[i].metricId), golden[i].dir);
    }
    // 方向 token 稳定书写。
    EXPECT_EQ(optimization::toToken(MetricDirection::Minimize), "min");
    EXPECT_EQ(optimization::toToken(MetricDirection::Maximize), "max");
}

/// OPT-07：默认激活分期（§7.3——StageB 三项静态；StageD 节拍/质量/成本；
/// REQUIREMENTS §15.0 权威口径）。
TEST(OptObjective, DefaultObjectivesStageSplit)
{
    IRD_TEST_INFO("OPT-07", {"AT-09"}, std::nullopt);
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

    // 默认容差全零（DOPT-5：默认零容差，不发明阈值——P-OPT-5 安全默认）。
    for (const auto& e : b.entries) {
        EXPECT_EQ(e.tolerance, core::Tolerance{});
    }
    for (const auto& e : d.entries) {
        EXPECT_EQ(e.tolerance, core::Tolerance{});
    }
}

// =====================================================================
// 目标校验（§7.3 条件 1/2——报告轨）
// =====================================================================

/// §7.3 条件 1：StageB 激活 StageD 专用指标 → OPT-STAGE-LOCKED（不静默
/// 降级；§6.5 启动阻塞非候选淘汰）。经接口消费（虚函数路径钉住）。
TEST(OptObjective, ValidateStageLockRejectsStageDMetrics)
{
    IRD_TEST_INFO("OPT-07", {"AT-09"}, std::nullopt);
    RegistryFixture fx;
    const OptimizationObjectiveProvider provider;

    // StageB 激活节拍（4 号 D-only 指标）→ 拒绝（阶段锁）。
    const ObjectiveSet bad = makeObjectiveSet(
        {entryOf(MetricId::Envelope), entryOf(MetricId::CycleTime)});
    const ObjectiveValidationReport r1 = provider.validateObjectives(
        bad, OptimizationStage::StageB, fx.registry.get());
    EXPECT_FALSE(r1.accepted);
    EXPECT_TRUE(r1.hasCode(kOptStageLocked)) << "§7.3 条件 1——OPT-STAGE-LOCKED";

    // 同目标集在 StageD 阶段面通过（阶段锁不误伤——阶段可算性判定正确）。
    const ObjectiveValidationReport r2 = provider.validateObjectives(
        bad, OptimizationStage::StageD, nullptr);
    EXPECT_FALSE(r2.hasCode(kOptStageLocked))
        << "StageD 八项全可激活（阶段面）";
    // StageD＋无注册表 → 逐条目 OPT-EVALUATOR-MISSING（条件 2 报告轨——
    // 注册表未装配不抛）。
    EXPECT_TRUE(r2.hasCode(kOptEvaluatorMissing));
}

/// §7.3 条件 2：来源评估器未注册/契约版本不符 → OPT-EVALUATOR-MISSING；
/// 注册齐备 → 通过（真注册表＋脚本化评估器——消费路径与生产一致）。
TEST(OptObjective, ValidateEvaluatorRegistration)
{
    IRD_TEST_INFO("OPT-07", {}, std::nullopt);
    const OptimizationObjectiveProvider provider;
    const ObjectiveSet stageB = defaultObjectives(OptimizationStage::StageB);

    // 无注册表（nullptr＝未装配）→ 三项全部 OPT-EVALUATOR-MISSING。
    const ObjectiveValidationReport missing = provider.validateObjectives(
        stageB, OptimizationStage::StageB, nullptr);
    EXPECT_FALSE(missing.accepted);
    EXPECT_EQ(missing.issues.size(), 3U);
    EXPECT_TRUE(missing.hasCode(kOptEvaluatorMissing));

    // 真 RegistryFixture（opt-static-screen 契约版本 1 在册）→ 通过。
    RegistryFixture fx;
    const ObjectiveValidationReport ok = provider.validateObjectives(
        stageB, OptimizationStage::StageB, fx.registry.get());
    EXPECT_TRUE(ok.accepted) << "注册齐备（§6.7 R1 域聚合评估器）";
    EXPECT_TRUE(ok.issues.empty());
}

/// 目标集自身非法（空集/重复 MetricId）→ OPT-INPUT-INVALID（首错定位）。
/// 两轨分验：makeObjectiveSet 构造轨 fail-fast（抛）＋validateObjectives
/// 报告轨（对直接聚合构造的原始集逐条拒绝——防绕过构造入口）。
TEST(OptObjective, ValidateRejectsEmptyAndDuplicate)
{
    IRD_TEST_INFO("OPT-07", {}, std::nullopt);
    const OptimizationObjectiveProvider provider;
    RegistryFixture fx;

    // 空集（研究定义无比较基础）——报告轨（直接聚合，不经 make）。
    const ObjectiveSet empty;  // 直接聚合（校验面不要求经 make 构造）。
    const ObjectiveValidationReport r1 = provider.validateObjectives(
        empty, OptimizationStage::StageB, fx.registry.get());
    EXPECT_FALSE(r1.accepted);
    EXPECT_TRUE(r1.hasCode(kOptInputInvalid));

    // 重复 MetricId（同指标两条目标语义未定义）——构造轨 fail-fast：
    // makeObjectiveSet 在重复上直接抛（消息携带重复指标 token）。
    try {
        (void)makeObjectiveSet(
            {entryOf(MetricId::StructuralMass),
             entryOf(MetricId::StructuralMass, 0.1, 0.0)});
        FAIL() << "重复目标应抛（makeObjectiveSet fail-fast）";
    } catch (const OptimizationError& e) {
        EXPECT_EQ(e.stableCode(), kOptInputInvalid);
    }

    // 重复集的报告轨防线（绕过 make 直接聚合——validateObjectives 同源
    // 判定面拒绝）。
    ObjectiveSet rawDup;
    rawDup.entries = {entryOf(MetricId::StructuralMass),
                      entryOf(MetricId::StructuralMass, 0.1, 0.0)};
    const ObjectiveValidationReport r2 = provider.validateObjectives(
        rawDup, OptimizationStage::StageB, fx.registry.get());
    EXPECT_FALSE(r2.accepted);
    EXPECT_TRUE(r2.hasCode(kOptInputInvalid));
}

// =====================================================================
// makeObjectiveSet fail-fast 与 canonical 目标段（§4.3）
// =====================================================================

/// makeObjectiveSet fail-fast 面（空/重复/容差负值/容差非有限——调用方
/// 错误轨，OPT-INPUT-INVALID）。
TEST(OptObjective, MakeObjectiveSetFailFast)
{
    IRD_TEST_INFO("OPT-07", {}, std::nullopt);
    // 空集。
    try {
        (void)makeObjectiveSet({});
        FAIL() << "空目标集应抛（无比较基础）";
    } catch (const OptimizationError& e) {
        EXPECT_EQ(e.stableCode(), kOptInputInvalid);
    }
    // 重复。
    try {
        (void)makeObjectiveSet({entryOf(MetricId::Envelope),
                                entryOf(MetricId::Envelope)});
        FAIL() << "重复目标应抛";
    } catch (const OptimizationError& e) {
        EXPECT_EQ(e.stableCode(), kOptInputInvalid);
    }
    // 负容差（破坏序语义）。
    try {
        (void)makeObjectiveSet({entryOf(MetricId::Envelope, -0.5, 0.0)});
        FAIL() << "负容差应抛";
    } catch (const OptimizationError& e) {
        EXPECT_EQ(e.stableCode(), kOptInputInvalid);
    }
    // NaN 容差。
    try {
        (void)makeObjectiveSet({entryOf(MetricId::Envelope, 0.0,
                                        std::nan(""))});
        FAIL() << "非有限容差应抛";
    } catch (const OptimizationError& e) {
        EXPECT_EQ(e.stableCode(), kOptInputInvalid);
    }
}

/// §4.3 canonical 目标段：黄金布局（u8 位图＋每激活目标 ε_rel/ε_abs f64
/// 小端）＋声明序无关（同目标集不同声明序 ⇒ 同段字节——身份稳定）。
TEST(OptObjective, CanonicalSegmentGoldenLayout)
{
    IRD_TEST_INFO("OPT-04", {"AT-09"}, std::nullopt);
    // 显式容差配置（acceptance 1：容差进 config.opt——经本段承载）。
    const ObjectiveSet set = makeObjectiveSet({
        entryOf(MetricId::StructuralMass, 0.0, 0.25),  // bit 1
        entryOf(MetricId::Envelope,       0.01, 0.0),  // bit 0
    });
    const std::vector<std::uint8_t> seg = canonicalizeObjectiveSegment(set);

    // 黄金布局：1 字节位图（bit0|bit1＝0x03）＋2 激活目标×16 字节＝33 字节；
    // 枚举序（Envelope 先于 StructuralMass）——bit0 容差对在前。
    ASSERT_EQ(seg.size(), 1U + 2U * 16U);
    EXPECT_EQ(seg[0], 0x03) << "位图 bit0(Envelope)|bit1(StructuralMass)";
    // 小端 f64 读回（编码＝uint64 位模式移位低位在前；本仓库 MSVC x64 宿主
    // 为小端，位拷贝读回即原值——跨端宿主读回需自行按小端重组，编码侧
    // 已平台无关）。
    const auto readF64Le = [&seg](std::size_t offset) {
        std::uint8_t raw[8];
        for (int i = 0; i < 8; ++i) {
            raw[i] = seg[offset + static_cast<std::size_t>(i)];
        }
        double v = 0.0;
        static_assert(sizeof(double) == 8, "位重组假定 8 字节");
        std::memcpy(&v, raw, 8);
        return v;
    };
    EXPECT_DOUBLE_EQ(readF64Le(1), 0.01) << "Envelope ε_rel（枚举序第一）";
    EXPECT_DOUBLE_EQ(readF64Le(9), 0.0) << "Envelope ε_abs";
    EXPECT_DOUBLE_EQ(readF64Le(17), 0.0) << "StructuralMass ε_rel";
    EXPECT_DOUBLE_EQ(readF64Le(25), 0.25) << "StructuralMass ε_abs";

    // 声明序无关：同集合反序声明 ⇒ 同段字节（声明序不入身份——I-OPT-8
    // 同源口径；排序键②的声明序语义在 ParetoTest 单测，两语义分层）。
    const ObjectiveSet reversed = makeObjectiveSet({
        entryOf(MetricId::Envelope,       0.01, 0.0),
        entryOf(MetricId::StructuralMass, 0.0, 0.25),
    });
    EXPECT_EQ(canonicalizeObjectiveSegment(reversed), seg)
        << "同目标集不同声明序 ⇒ 同 canonical 段（确定性）";

    // 容差不同 ⇒ 段字节不同（显式容差进 config.opt ⇒ 新 sliceId ⇒ 新
    // 运行——§4.5 失效映射的承载）。
    const ObjectiveSet otherTol = makeObjectiveSet({
        entryOf(MetricId::StructuralMass, 0.0, 0.26),
        entryOf(MetricId::Envelope,       0.01, 0.0),
    });
    EXPECT_NE(canonicalizeObjectiveSegment(otherTol), seg)
        << "容差差异必须进 canonical（缓存失效面——acceptance 1）";

    // 非法集序列化 fail-fast（防绕过构造入口）。
    try {
        (void)canonicalizeObjectiveSegment(ObjectiveSet{});
        FAIL() << "空集序列化应抛";
    } catch (const OptimizationError& e) {
        EXPECT_EQ(e.stableCode(), kOptInputInvalid);
    }
}

// =====================================================================
// 三项静态指标计算（OPT-VER-124/125/126）
// =====================================================================

/// OPT-VER-124：三项静态指标黄金对照（解析期望值；§7.1 暂定包络口径——
/// 三向尺寸之和）＋八项全量枚举序（候选表八列固定呈现）。
TEST(OptObjective, StaticMetricsGoldenValues)
{
    IRD_TEST_INFO("OPT-07", {"AT-09"}, std::nullopt);
    const StaticMetricResult r =
        computeStaticMetrics(OptimizationStage::StageB, makeGoldenFacts());
    ASSERT_EQ(r.metrics.size(), kMetricCount);

    // 包络：1+2+3 = 6.0 m（暂定口径：基座系 AABB 三向尺寸之和——P-OPT-5
    // 登记中；detail 带暂定口径标注）。
    const MetricComputation& env = metricOf(r, MetricId::Envelope);
    ASSERT_TRUE(env.valueSi.has_value());
    EXPECT_DOUBLE_EQ(*env.valueSi, 6.0);
    EXPECT_TRUE(env.detail.find("P-OPT-5") != std::string::npos)
        << "暂定口径留痕标注（P-OPT-5 裁决前允许范围）";

    // 结构质量：2.5+1.0+0.5 = 4.0 kg。
    const MetricComputation& mass = metricOf(r, MetricId::StructuralMass);
    ASSERT_TRUE(mass.valueSi.has_value());
    EXPECT_DOUBLE_EQ(*mass.valueSi, 4.0);

    // 最小关节裕量：min(0.8,0.3,0.5) = 0.3（kin D-KIN-2 归一化口径——
    // 本单元只合成最小值）。
    const MetricComputation& margin = metricOf(r, MetricId::MinJointMargin);
    ASSERT_TRUE(margin.valueSi.has_value());
    EXPECT_DOUBLE_EQ(*margin.valueSi, 0.3);

    // 无部分缺失——DataInsufficient 素材标志不置位。
    EXPECT_FALSE(r.partialMarginDataInsufficient);
}

/// OPT-VER-125：StageB 其余五项恒"—"——nullopt（不显示为零）＋stage 类
/// gap token；**不因空判动力学/器件不可行**（数据面缺失语义，ERR-01）。
TEST(OptObjective, StageBFiveDMetricsNotComputableNotInfeasible)
{
    IRD_TEST_INFO("OPT-07", {"AT-09"}, std::nullopt);
    const StaticMetricResult r =
        computeStaticMetrics(OptimizationStage::StageB, makeGoldenFacts());
    const MetricId dOnly[5] = {MetricId::CycleTime, MetricId::DeviceCost,
                               MetricId::DeviceMass, MetricId::JointPositiveWork,
                               MetricId::MinDriveMargin};
    for (const MetricId id : dOnly) {
        const MetricComputation& m = metricOf(r, id);
        EXPECT_FALSE(m.valueSi.has_value())
            << "D-only 指标须为 nullopt（'—'，不显示为零）：" << toToken(id);
        EXPECT_FALSE(m.valueSi.has_value() && *m.valueSi == 0.0)
            << "不得输出 0 冒充（OPT-07/UX-03）";
        EXPECT_EQ(m.gapToken, kMetricGapStage) << "gap＝stage（§7.1 阶段可算）";
    }
    // 五项缺失不置位候选级 DataInsufficient 素材（StageB 恒缺失是阶段
    // 语义，不是数据缺陷——不判不可行）。
    EXPECT_FALSE(r.partialMarginDataInsufficient);
    // StageD 调用同结构：D-only 五项如实标注联合面未落位（不伪造可算）。
    const StaticMetricResult rd =
        computeStaticMetrics(OptimizationStage::StageD, makeGoldenFacts());
    for (const MetricId id : dOnly) {
        const MetricComputation& m = metricOf(rd, id);
        EXPECT_FALSE(m.valueSi.has_value());
        EXPECT_EQ(m.gapToken, kMetricGapSourceMissing);
    }
}

/// OPT-VER-126：缺失指标不当作零——质量任一连杆 NotProvided → 整体"—"
/// （不按 0 合成，NFR-COR-03）；缺几何→包络"—"。
TEST(OptObjective, MissingSourcesAreNotZero)
{
    IRD_TEST_INFO("NFR-COR-03", {"AT-09"}, std::nullopt);
    StaticMetricFacts facts = makeGoldenFacts();
    facts.linkMasses[1].massKg = std::nullopt;  // 中间连杆缺失（边界形态：
                                                //  缺失非首尾——定位正确性）
    const StaticMetricResult r =
        computeStaticMetrics(OptimizationStage::StageB, facts);
    const MetricComputation& mass = metricOf(r, MetricId::StructuralMass);
    EXPECT_FALSE(mass.valueSi.has_value()) << "缺失不按 0 合成（§7.1 行 2）";
    EXPECT_EQ(mass.gapToken, kMetricGapSourceMissing);
    // 其余两指标不受影响（逐指标独立）。
    EXPECT_TRUE(metricOf(r, MetricId::Envelope).valueSi.has_value());
    EXPECT_TRUE(metricOf(r, MetricId::MinJointMargin).valueSi.has_value());

    // 缺几何→包络"—"。
    StaticMetricFacts noEnv = makeGoldenFacts();
    noEnv.envelope = std::nullopt;
    const StaticMetricResult r2 =
        computeStaticMetrics(OptimizationStage::StageB, noEnv);
    EXPECT_FALSE(metricOf(r2, MetricId::Envelope).valueSi.has_value());
    EXPECT_EQ(metricOf(r2, MetricId::Envelope).gapToken, kMetricGapSourceMissing);
}

/// §7.1 行 3 缺失语义两分支：全部工位数据不足→"—"（不升级候选状态）；
/// 部分不足→值不输出＋DataInsufficient 素材标志（判定归管线——PA-1）。
TEST(OptObjective, JointMarginPartialDataSemantics)
{
    IRD_TEST_INFO("OPT-07", {"AT-09"}, std::nullopt);
    // 全部数据不足（空表＝Must 证据整体缺席）→ SourceMissing，标志不置位。
    StaticMetricFacts allMissing = makeGoldenFacts();
    allMissing.pointMargins.clear();
    const StaticMetricResult r1 =
        computeStaticMetrics(OptimizationStage::StageB, allMissing);
    EXPECT_FALSE(metricOf(r1, MetricId::MinJointMargin).valueSi.has_value());
    EXPECT_EQ(metricOf(r1, MetricId::MinJointMargin).gapToken,
              kMetricGapSourceMissing);
    EXPECT_FALSE(r1.partialMarginDataInsufficient);

    // 部分不足（3 点缺 1）→ PartialData＋素材标志置位。
    StaticMetricFacts partial = makeGoldenFacts();
    partial.pointMargins[1].minMargin = std::nullopt;
    const StaticMetricResult r2 =
        computeStaticMetrics(OptimizationStage::StageB, partial);
    EXPECT_FALSE(metricOf(r2, MetricId::MinJointMargin).valueSi.has_value())
        << "部分缺失时值不输出（候选整体 DataInsufficient 素材）";
    EXPECT_EQ(metricOf(r2, MetricId::MinJointMargin).gapToken,
              kMetricGapPartialData);
    EXPECT_TRUE(r2.partialMarginDataInsufficient)
        << "管线联动素材（§7.1 行 3——候选状态判定归管线，PA-1）";
}

/// NFR-COR-03：非有限输入不静默修正——NaN 包络坐标/负质量/负裕量均按
/// NonFiniteInput 轨处理（值不输出，"非有限→评估失败处理"的素材面）。
TEST(OptObjective, NonFiniteInputsNotSilentlyFixed)
{
    IRD_TEST_INFO("NFR-COR-03", {}, std::nullopt);
    // NaN 包络坐标。
    StaticMetricFacts nanEnv = makeGoldenFacts();
    nanEnv.envelope->yMax = std::nan("");
    const StaticMetricResult r1 =
        computeStaticMetrics(OptimizationStage::StageB, nanEnv);
    EXPECT_EQ(metricOf(r1, MetricId::Envelope).gapToken, kMetricGapNonFinite);
    EXPECT_FALSE(metricOf(r1, MetricId::Envelope).valueSi.has_value());

    // 面序反转（xMax < xMin——同型输入缺陷）。
    StaticMetricFacts flipped = makeGoldenFacts();
    flipped.envelope->xMin = 5.0;
    flipped.envelope->xMax = 1.0;
    const StaticMetricResult r2 =
        computeStaticMetrics(OptimizationStage::StageB, flipped);
    EXPECT_EQ(metricOf(r2, MetricId::Envelope).gapToken, kMetricGapNonFinite);

    // 负质量（物理不可能——上游事实缺陷）。
    StaticMetricFacts negMass = makeGoldenFacts();
    negMass.linkMasses[0].massKg = -1.0;
    const StaticMetricResult r3 =
        computeStaticMetrics(OptimizationStage::StageB, negMass);
    EXPECT_EQ(metricOf(r3, MetricId::StructuralMass).gapToken,
              kMetricGapNonFinite);

    // 非有限裕量。
    StaticMetricFacts nanMargin = makeGoldenFacts();
    nanMargin.pointMargins[0].minMargin = std::nan("");
    const StaticMetricResult r4 =
        computeStaticMetrics(OptimizationStage::StageB, nanMargin);
    EXPECT_EQ(metricOf(r4, MetricId::MinJointMargin).gapToken,
              kMetricGapNonFinite);
}
