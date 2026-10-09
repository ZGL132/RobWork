/**
 * @file   At07CoverageContractTest.cpp
 * @brief  AT-07 契约测试（DynAt07）——"完整任务循环输出类型化广义力/功率/
 *         峰值窗/RMS（含驻留）；数据不足降级可信等级；多工况不漏验"
 *         （REQUIREMENTS AT-07 原文三面）在动力学产品链上的端到端契约钉扎，
 *         数据面消费 dyn-two-link-analytic 黄金数据集（DatasetManifest 登
 *         记，与 test/DynGoldenDatasetTest.cpp 同一数据源）。
 *
 * 设计依据：
 *   - REQUIREMENTS AT-07（三面：完整循环包络/数据不足降级/多工况不漏验）
 *     ＋EVI-02（正式计算与正式判定必须覆盖全部启用的必验工况，不得漏验
 *     ——多工况包络合并仅为呈现方式）、DYN-06（不把估算/缺失包装成精确
 *     结论）、DYN-07（包络合并不替代必验工况覆盖规则）、DYN-03（完整循
 *     环 RMS 含驻留）
 *   - units/dynamics.md §7.4/§7.6（包络只属统计呈现、不自动形成工程判定；
 *     漏验→整体缺项呈现；Empty 不产出）、§5.5/§9.4（DYN-06 三层降级＋
 *     DYN-FRICTION-MISSING）、§8.1（覆盖矩阵五态——漏验不得输出完整正
 *     式结论）、§8.4（dyn Profile 六项证据装配）、§15.2 P-DYN-2/P-DYN-5
 *     （对端卡已落盘——消费键以卡内登记为准）
 *   - 任务契约 tasks/foundation/WP-17-T10.json acceptance 1/2/3
 *
 * 线程约束：gtest 用例天然串行；评估器/统计器/证据装配器无状态或单实例
 * 单线程使用（卡 §10.0）。
 */

#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/dynamics/DiagCodes.hpp>
#include <sdurws/ird/dynamics/DynTypes.hpp>
#include <sdurws/ird/dynamics/Envelope.hpp>
#include <sdurws/ird/dynamics/EvidenceBuilder.hpp>
#include <sdurws/ird/dynamics/InverseDynamics.hpp>
#include <sdurws/ird/dynamics/PowerEnergy.hpp>
#include <sdurws/ird/dynamics/SeriesBuilder.hpp>
#include <sdurws/ird/evidence/Evidence.hpp>            // EvidenceItem/EvidenceItemStatus
#include <sdurws/ird/evidence/Snapshot.hpp>            // RequiredCaseSet/CaseEntry（EVI-02 分母）
#include <sdurws/ird/runtime/BaseWorldTransform.hpp>
#include <sdurws/ird/runtime/CanonicalModel.hpp>
#include <sdurws/ird/testkit/Dataset.hpp>
#include <sdurws/ird/testkit/JsonLite.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>
#include <sdurws/ird/testkit/gtest/GoldenFixture.hpp>

#include <gtest/gtest.h>

#include "../test/TwoLinkFixture.hpp"  // 同单元测试夹具（R-2 允许——相对路径进同单元 test/ 目录）

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace dyn = sdurws::ird::dynamics;
namespace core = sdurws::ird::core;
namespace tk = sdurws::ird::testkit;
namespace ev = sdurws::ird::evidence;
namespace dynfix = sdurws::ird::dynamics::testfixture;
namespace runtime = sdurws::ird::runtime;
using namespace sdurws::ird::testkit;

// =====================================================================
// 黄金 JSON 读取脚手架（与 test/DynGoldenDatasetTest.cpp 同源口径——契约
// 测试目标独立自持，不跨目标共享私有头）
// =====================================================================

namespace {

std::string readDatasetText(const tk::GoldenDataset& ds, const char* rel, bool inputSide)
{
    const auto path = inputSide ? ds.resolveInput(rel) : ds.resolveExpected(rel);
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

tk::JsonValue loadJson(const tk::GoldenDataset& ds, const char* rel, bool inputSide)
{
    return tk::parseJson(readDatasetText(ds, rel, inputSide));
}

tk::JsonValue field(const tk::JsonValue& o, const char* key)
{
    const tk::JsonValue* v = o.find(key);
    if (v == nullptr) {
        ADD_FAILURE() << "黄金数据缺字段: " << key;
        return tk::JsonValue{};
    }
    return *v;
}

std::vector<double> numVec(const tk::JsonValue& o, const char* key)
{
    std::vector<double> out;
    const tk::JsonValue* v = o.find(key);
    if (v == nullptr || !v->isArray()) {
        ADD_FAILURE() << "黄金数据缺数组字段: " << key;
        return out;
    }
    for (const tk::JsonValue& item : v->items) {
        if (item.isNumber()) {
            out.push_back(item.number);
        }
    }
    return out;
}

/// 工况 id 派生（与生成脚本同一约定——SHA-256 前 16 字节，见黄金消费套件）。
core::ObjectId goldenConditionId(const std::string& caseId)
{
    return dynfix::idFrom<core::ObjectId>("dyn-golden-cond-" + caseId);
}

/// 序列身份块（同黄金消费套件形态——测试值确定性派生）。
dyn::SeriesIdentity makeSeriesIdentity(const std::string& tag, std::size_t planned,
                                       const core::ObjectId& condId,
                                       const dyn::DynamicsValidity& seed)
{
    dyn::SeriesIdentity id;
    id.snapshotId.bytes = dynfix::digestOf(tag + "-snap");
    id.sliceId.bytes = dynfix::digestOf(tag + "-slice");
    id.trajectoryPayloadId.bytes = dynfix::digestOf(tag + "-trj");
    id.conditionId = condId;
    id.toolObjectId = core::ObjectId{};
    id.dynConfigDigest = "sha256-" + tag;
    id.task.project = dynfix::idFrom<core::ProjectId>(tag + "-prj");
    id.task.branch = dynfix::idFrom<core::BranchId>(tag + "-brn");
    id.task.revision = dynfix::idFrom<core::RevisionId>(tag + "-rev");
    id.task.run = dynfix::idFrom<core::RunId>(tag + "-run");
    id.task.attempt.value = 1u;
    id.plannedSampleCount = planned;
    id.validitySeed = seed;
    return id;
}

/// 评估黄金算例（请求组装消费 runtime gravityToBase 单点——D-DYN-3）。
dyn::InverseDynOutcome evaluateGoldenCase(const runtime::CanonicalModel& model,
                                          const core::ObjectId& condId,
                                          const std::vector<dyn::InverseDynSampleInput>& samples)
{
    dyn::InverseDynRequest req;
    req.conditionId = condId;
    req.model = &model;
    const rw::math::Vector3D<double> gBase =
        sdurws::ird::runtime::gravityToBase(model.world().T_world_base.R(),
                                            model.world().gravityWorld);
    req.gravityBase[0] = gBase[0];
    req.gravityBase[1] = gBase[1];
    req.gravityBase[2] = gBase[2];
    req.samples = samples;

    class Ctx final : public ev::IEvaluationContext {
    public:
        bool cancellationRequested() const override { return false; }
        void reportProgress(std::uint8_t, std::string_view) override {}
        std::optional<std::vector<std::uint8_t>> tryObjectBytes(
            core::ObjectId, core::ContentVersion) const override
        {
            return std::nullopt;
        }
    } ctx;
    return dyn::InverseDynamicsEvaluator().evaluate(req, ctx);
}

/// 工况结果装配（序列冻结＋峰值＋功率能量——统计口径唯一实现点产出）。
dyn::OperatingConditionResult assembleCondition(const dyn::InverseDynOutcome& out,
                                                const std::string& tag)
{
    const dyn::SeriesIdentity id = makeSeriesIdentity(tag, out.validity.plannedSampleCount,
                                                      out.conditionId, out.validity);
    dyn::DynamicsSeriesBuilder b;
    for (const dyn::DynamicsSample& row : out.samples) {
        b.addSample(row);
    }
    const dyn::DynamicsSeries series = b.finalize(id);

    const dyn::DynamicsEnvelopeCalculator env;
    const dyn::PowerEnergyCalculator pe;
    dyn::OperatingConditionResult result;
    result.conditionId = series.conditionId;
    result.caseScope.push_back(series.conditionId);
    result.series = series;
    result.peaks = env.computePeaks(series);
    result.powerEnergy = pe.compute(series);
    result.validity = series.validity;
    return result;
}

/// 覆盖条目快捷构造（enabled/mandatory 逐条显式——EVI-02 分母口径面）。
ev::CaseEntry caseOf(const core::ObjectId& id, bool enabled, bool mandatory)
{
    ev::CaseEntry e;
    e.caseId = id;
    e.enabled = enabled;
    e.mandatory = mandatory;
    return e;
}

}  // namespace

// =====================================================================
// AT-07 契约夹具（黄金数据集 dyn-two-link-analytic——与消费套件同源）
// =====================================================================

class DynAt07 : public tk::GoldenFixture
{
protected:
    tk::DatasetRef datasetRef() const override
    {
        return {"dyn-two-link-analytic", "1.0.0"};
    }

    /// 三工况端到端评估＋装配（黄金数据集三算例——完整链：RNEA→序列→统计
    /// →OperatingConditionResult；mounting/friction 按算例声明）。
    std::vector<dyn::OperatingConditionResult> evaluateAllThree()
    {
        const tk::JsonValue inputs = loadJson(*dataset, "inputs/dyn-two-link-inputs.json", true);
        std::vector<dyn::OperatingConditionResult> results;
        const char* kCaseIds[3] = {"static-ground", "static-inverted", "motion-dwell"};
        for (int c = 0; c < 3; ++c) {
            const tk::JsonValue cs = [&]() {
                for (const tk::JsonValue& x : field(inputs, "cases").items) {
                    if (x.isObject() && x.find("id") != nullptr
                        && x.find("id")->text == kCaseIds[c]) {
                        return x;
                    }
                }
                return tk::JsonValue{};
            }();
            const bool inverted = cs.find("mounting") != nullptr
                               && cs.find("mounting")->text == "inverted";
            const runtime::CanonicalModel model = dynfix::makeTwoLinkModel(
                inverted ? dynfix::invertedWorld() : dynfix::groundWorld(),
                dynfix::FrictionSpec{true, 0.05, 0.10, 0.01}, std::nullopt);
            std::vector<dyn::InverseDynSampleInput> samples;
            for (const tk::JsonValue& s : field(cs, "samples").items) {
                dyn::InverseDynSampleInput in;
                in.t = s.find("t")->number;
                in.segmentIndex = static_cast<std::uint32_t>(s.find("segment")->number);
                in.q = numVec(s, "q");
                in.qd = numVec(s, "qd");
                in.qdd = numVec(s, "qdd");
                samples.push_back(in);
            }
            const dyn::InverseDynOutcome out =
                evaluateGoldenCase(model, goldenConditionId(kCaseIds[c]), samples);
            results.push_back(assembleCondition(out, std::string{"at07-"} + kCaseIds[c]));
        }
        return results;
    }

    /// 三必验工况的冻结覆盖集（EVI-02 分母——enabled∧mandatory 逐条显式）。
    ev::RequiredCaseSet mandatoryThree()
    {
        ev::RequiredCaseSet cs;
        const char* kCaseIds[3] = {"static-ground", "static-inverted", "motion-dwell"};
        for (const char* id : kCaseIds) {
            cs.entries.push_back(caseOf(goldenConditionId(id), true, true));
        }
        return cs;
    }
};

// =====================================================================
// AT-07 面①＋面③：完整循环包络 × 多工况不漏验（EVI-02）
// =====================================================================

/**
 * AT-07 完整循环包络＋多工况不漏验（acceptance 1，V-24/V-25/V-26 契约面）：
 * 三必验工况全执行→coversAllMandatory=true 且 envelopeComplete=true（包络
 * 完整呈现）；漏一必验工况（2/3 results）→coversAllMandatory=false **而包
 * 络行集仍非空**（漂亮包络≠覆盖——包络合并不替代必验工况覆盖规则，DYN-07/
 * EVI-02）；disabled Must 不进分母（漏验解除——覆盖矩阵 C4 行）；Empty 工
 * 况在场口径差异钉扎：conditionCount＝results 条数（含 Empty）而覆盖分子
 * ＝completeness≠Empty 的工况集（wp17-t07 登记的两个在场口径不同——本契
 * 约按卡内登记口径逐字段消费，全程一致）。
 */
TEST_F(DynAt07, FullCycleEnvelopeAndNoMissedMandatory_WP17T10_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-03", "DYN-07", "EVI-02"},
                  std::vector<std::string>{"AT-07"}, datasetRef());
    ASSERT_TRUE(dataset.has_value());
    const std::vector<dyn::OperatingConditionResult> all = evaluateAllThree();
    ASSERT_EQ(all.size(), 3U);
    for (const dyn::OperatingConditionResult& r : all) {
        // 完整循环产出面：三工况全部 Complete（黄金算例无缺口/无非有限）。
        ASSERT_EQ(r.validity.completeness, dyn::DynamicsValidity::Completeness::Complete);
        ASSERT_EQ(r.peaks.size(), 12U);            // 每工况 2 关节 × 6 token
        ASSERT_EQ(r.caseScope.size(), 1U);         // caseScope＝单工况自身（EVI-02 关联）
        EXPECT_TRUE(r.caseScope[0] == r.conditionId);
    }

    const dyn::DynamicsEnvelopeCalculator env;

    // ---- 正面（V-24）：三必验全执行→覆盖呈现完备＋包络完整。
    {
        const dyn::DynamicsEnvelope envAll = env.mergeEnvelope(all, mandatoryThree());
        EXPECT_TRUE(envAll.coversAllMandatory);
        EXPECT_EQ(envAll.conditionCount, 3U);      // 在场口径：results 条数
        ASSERT_EQ(envAll.joints.size(), 2U);       // 两关节行（Ok 行关节并集）
        for (const auto& row : envAll.joints) {
            EXPECT_TRUE(row.envelopeComplete);
            // 来源工况集＝三工况全集（每关节均有三工况 Ok 行贡献）。
            ASSERT_EQ(row.contributingConditions.size(), 3U);
        }
    }

    // ---- 反面（V-25）：漏一必验工况→coversAllMandatory=false，但包络行集
    // 仍非空（漂亮包络≠覆盖——包络合并仅为呈现方式，EVI-02/DYN-07；
    // 正式判定归 evidence 覆盖矩阵与汇总层，本域不越权定级）。
    {
        std::vector<dyn::OperatingConditionResult> partial(all.begin(), all.begin() + 2);
        const dyn::DynamicsEnvelope envPartial = env.mergeEnvelope(partial, mandatoryThree());
        EXPECT_FALSE(envPartial.coversAllMandatory);      // 漏验如实呈现（缺项方向）
        EXPECT_EQ(envPartial.conditionCount, 2U);
        ASSERT_FALSE(envPartial.joints.empty());          // 包络行集仍非空＝呈现面成立
        for (const auto& row : envPartial.joints) {
            EXPECT_TRUE(row.envelopeComplete);            // 在场两工况仍全 Complete
            EXPECT_EQ(row.contributingConditions.size(), 2U);
        }
    }

    // ---- 覆盖分母口径（C4 行）：disabled Must＝NotApplicable 不参与分母
    // ——分母声明 motion-dwell 为 disabled（替换其 enabled 条目——冻结集
    // caseId 唯一性）→漏掉同一工况不构成漏验（覆盖呈现完备，§8.1 覆盖矩阵）。
    {
        std::vector<dyn::OperatingConditionResult> partial(all.begin(), all.begin() + 2);
        ev::RequiredCaseSet coverage;
        coverage.entries.push_back(caseOf(goldenConditionId("static-ground"), true, true));
        coverage.entries.push_back(caseOf(goldenConditionId("static-inverted"), true, true));
        coverage.entries.push_back(
            caseOf(goldenConditionId("motion-dwell"), /*enabled=*/false, /*mandatory=*/true));
        const dyn::DynamicsEnvelope envDis = env.mergeEnvelope(partial, coverage);
        EXPECT_TRUE(envDis.coversAllMandatory);
        EXPECT_EQ(envDis.conditionCount, 2U);
    }

    // ---- Empty 在场口径钉扎（wp17-t07 登记口径——两个在场口径不同）：
    // conditionCount＝results 条数（含 Empty 工况，计数如实）；覆盖分子＝
    // completeness≠Empty 的工况集（Empty＝执行未产出样本，保守不计——
    // 缺项方向只低报不高报）。注入 motion-dwell 的 Empty 形态结果：三项在
    // 场但只有两项产出样本→coversAllMandatory=false 且 conditionCount=3。
    {
        // Empty 结果：同工况 id、零样本序列（Empty 完整度——峰值装配一致
        // 性校验对空序列放行：computePeaks(空)＝空，与装配的空 peaks 恒同）。
        dyn::OperatingConditionResult emptyMotion;
        emptyMotion.conditionId = all[2].conditionId;
        emptyMotion.caseScope.push_back(all[2].conditionId);
        dyn::SeriesIdentity id = makeSeriesIdentity("at07-empty-motion", 0,
                                                    all[2].conditionId,
                                                    dyn::DynamicsValidity{});
        dyn::DynamicsSeriesBuilder b;   // 零样本——finalize 产 Empty 序列
        emptyMotion.series = b.finalize(id);
        emptyMotion.peaks = env.computePeaks(emptyMotion.series);
        ASSERT_TRUE(emptyMotion.peaks.empty());
        emptyMotion.validity = emptyMotion.series.validity;
        ASSERT_EQ(emptyMotion.validity.completeness, dyn::DynamicsValidity::Completeness::Empty);

        std::vector<dyn::OperatingConditionResult> withEmpty{all[0], all[1], emptyMotion};
        const dyn::DynamicsEnvelope envEmpty = env.mergeEnvelope(withEmpty, mandatoryThree());
        EXPECT_EQ(envEmpty.conditionCount, 3U);           // 在场口径 1：results 条数
        EXPECT_FALSE(envEmpty.coversAllMandatory);        // 在场口径 2：分子≠分母
        ASSERT_EQ(envEmpty.joints.size(), 2U);            // 统计行只来自两实测工况
    }
}

// =====================================================================
// AT-07 面②：数据不足降级可信等级（DYN-06——不包装成精确结论）
// =====================================================================

/**
 * AT-07 数据不足降级（acceptance 1，V-09/V-10 契约面；黄金数据集
 * degradationFace——与主模型同链、唯一差异＝摩擦三元组全缺 MDL-16）：
 * ①评估继续（分项按 0 计入——序列齐全不阻断）；②validity.frictionMissing
 * ＝true＋诊断含 DYN-FRICTION-MISSING 稳定码素材；③证据链
 * （DynamicsEvidenceBuilder 全装配）：provenance 项 Invalid＋invalidReason
 * 码、缺失清单全量列出（不短路）、限定语恰含 data-insufficient（DYN-06
 * ——缺失层）；④对照面：同输入干净模型（正小值三元组 Provided）→provenance
 * 项 Satisfied、无限定语（不弱化也不添加——精确结论只在干净链上成立）。
 */
TEST_F(DynAt07, DataInsufficientDegradation_WP17T10_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-06", "MDL-16"},
                  std::vector<std::string>{"AT-07"}, datasetRef());
    ASSERT_TRUE(dataset.has_value());

    const tk::JsonValue inputs = loadJson(*dataset, "inputs/dyn-two-link-inputs.json", true);
    const tk::JsonValue face = field(inputs, "degradationFace");
    const tk::JsonValue sample = field(face, "sample");
    ASSERT_TRUE(sample.isObject()) << "黄金数据 degradationFace.sample 非法";
    const std::vector<double> q = numVec(sample, "q");
    const std::vector<double> qd = numVec(sample, "qd");
    const std::vector<double> qdd = numVec(sample, "qdd");
    ASSERT_EQ(q.size(), 2U);
    ASSERT_EQ(qd.size(), 2U);
    ASSERT_EQ(qdd.size(), 2U);

    // 黄金降级模型＝与主模型同链、摩擦三元组全缺（FrictionSpec 默认全缺）。
    const runtime::CanonicalModel degraded =
        dynfix::makeTwoLinkModel(dynfix::groundWorld(), dynfix::FrictionSpec{}, std::nullopt);
    const core::ObjectId cond = dynfix::idFrom<core::ObjectId>("at07-degradation-cond");
    std::vector<dyn::InverseDynSampleInput> samples;
    {
        dyn::InverseDynSampleInput in;
        in.t = sample.find("t")->number;    // s（isObject 已断言——字段必在）
        in.segmentIndex = 0;
        in.q = q;
        in.qd = qd;
        in.qdd = qdd;
        samples.push_back(in);
        // 第二样本（同位形、非零速度——降级面序列可积分、统计可产出）。
        dyn::InverseDynSampleInput in2 = in;
        in2.t = in.t + 0.1;
        samples.push_back(in2);
    }
    const dyn::InverseDynOutcome out = evaluateGoldenCase(degraded, cond, samples);
    ASSERT_EQ(out.samples.size(), 4U);                    // 2 时刻 × 2 关节（评估继续）

    // ---- ①②降级标记与稳定码素材（数值继续、证据不包装精确——两层分开）。
    EXPECT_TRUE(out.validity.frictionMissing);
    EXPECT_EQ(out.validity.completeness, dyn::DynamicsValidity::Completeness::Complete);
    ASSERT_FALSE(out.diagnostics.empty());
    bool hasFrictionMissing = false;
    for (const core::DiagnosticRecord& d : out.diagnostics) {
        if (d.code == std::string{dyn::kDynFrictionMissing}) {
            hasFrictionMissing = true;
        }
    }
    EXPECT_TRUE(hasFrictionMissing) << "DYN-FRICTION-MISSING 素材必附（MDL-16 M-8 闭环）";

    const dyn::OperatingConditionResult result = assembleCondition(out, "at07-degradation");

    // ---- ③证据链：dyn Profile 六项装配——provenance 项 Invalid。
    dyn::DynamicsEvidenceBuilder builder;
    builder.addSeries(result.series);
    builder.addConditionResult(result);
    builder.build();
    const std::vector<ev::EvidenceItem> items = builder.evidenceItems();
    ASSERT_EQ(items.size(), 6U);                          // §8.4 表恒 6 行
    EXPECT_TRUE(ev::validateEvidenceItems(items).empty()); // presence 纪律零问题

    const ev::EvidenceItem* prov = nullptr;
    for (const ev::EvidenceItem& it : items) {
        if (it.itemId == std::string{dyn::kDynItemProvenance}) {
            prov = &it;
        }
    }
    ASSERT_NE(prov, nullptr);
    EXPECT_EQ(prov->status, ev::EvidenceItemStatus::Invalid);
    ASSERT_TRUE(prov->invalidReason.has_value());
    EXPECT_EQ(prov->invalidReason->code, std::string{dyn::kDynFrictionMissing});

    // 缺失清单全量列出（不短路）：至少含 provenance 项；其余必需项不误伤。
    const std::vector<dyn::EvidenceGap> gaps = builder.missingList();
    bool gapHasProvenance = false;
    for (const dyn::EvidenceGap& g : gaps) {
        if (g.itemId == std::string{dyn::kDynItemProvenance}) {
            gapHasProvenance = true;
        }
    }
    EXPECT_TRUE(gapHasProvenance);
    // 限定语恰含 data-insufficient（缺失层——DYN-06 三层之一；不弱化）。
    const std::vector<std::string_view> quals = builder.trustQualifiersCached();
    bool hasDataInsufficient = false;
    for (const std::string_view q2 : quals) {
        if (q2 == dyn::kQualifierDataInsufficient) {
            hasDataInsufficient = true;
        }
    }
    EXPECT_TRUE(hasDataInsufficient);
    EXPECT_FALSE(quals.empty()) << "缺失链不得包装为精确结论（DYN-06）";

    // ---- ④对照面：同输入干净模型（正小值三元组 Provided）→精确结论成立。
    const runtime::CanonicalModel clean = dynfix::makeTwoLinkModel(
        dynfix::groundWorld(), dynfix::FrictionSpec{true, 0.05, 0.10, 0.01}, std::nullopt);
    const dyn::InverseDynOutcome outClean =
        evaluateGoldenCase(clean, cond, samples);
    EXPECT_FALSE(outClean.validity.frictionMissing);
    const dyn::OperatingConditionResult resultClean =
        assembleCondition(outClean, "at07-degradation-clean");
    dyn::DynamicsEvidenceBuilder cleanBuilder;
    cleanBuilder.addSeries(resultClean.series);
    cleanBuilder.addConditionResult(resultClean);
    cleanBuilder.build();
    const std::vector<std::string_view> cleanQuals = cleanBuilder.trustQualifiersCached();
    EXPECT_TRUE(cleanQuals.empty()) << "干净链无限定语（不弱化也不添加——RPT-05 同源纪律）";
    for (const ev::EvidenceItem& it : cleanBuilder.evidenceItems()) {
        if (it.itemId == std::string{dyn::kDynItemProvenance}) {
            EXPECT_EQ(it.status, ev::EvidenceItemStatus::Satisfied);
            EXPECT_TRUE(it.artifactDigest.has_value());
        }
    }
}
