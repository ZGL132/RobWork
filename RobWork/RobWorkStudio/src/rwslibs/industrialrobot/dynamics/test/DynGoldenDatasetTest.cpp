/**
 * @file   DynGoldenDatasetTest.cpp
 * @brief  动力学黄金数据集消费套件（DynGolden）——WP-17-T10 主交付面：
 *         testdata/golden/dyn-two-link-analytic 数据集经 testkit 黄金设施
 *         （GoldenFixture＋DatasetManifest 完整性＋ToleranceProfile 档案容
 *         差）消费。期望值来自数据文件（generate/make_dyn_two_link_golden
 *         .mjs 独立参考实现产物——拉格朗日封闭式，与产品 RNEA 零共享），
 *         覆盖：二连杆静态重力矩（地面）/倒挂静态重力矩（AT-37 动力侧）/
 *         完整任务循环（运动＋驻留，AT-07）三算例与三工况包络合并参考。
 *
 * 设计依据：
 *   - units/dynamics.md §11（验证方案——黄金数据集 testdata/golden/dyn-*、
 *     容差档案测试对照逐例声明、附录 D 第 9 项标量相对 1×10⁻⁹）、§5
 *     （RNEA 输入面与分项口径）、§7（峰值窗/RMS 含驻留/功率能量符号）、
 *     §7.4（多工况包络合并）、§5.5（摩擦 sgn₀ 约定）
 *   - units/testkit.md §4.2（DatasetManifest 登记——analytic-case 可独立
 *     正确性依据＋附录 D C4 零值/近零/正负抵消样例义务）、§4.3（容差
 *     档案）、§6.1（GoldenFixture 六步生命周期）
 *   - 需求 DYN-01/02/03（逆动力学/类型化输出/峰值窗＋RMS）、DYN-06（数
 *     据不足降级）、MDL-22/AT-37（重力投影单一消费）、NFR-COR-01（解析
 *     算例对照）、NFR-COR-02/03（确定性/非有限拒绝）
 *   - 任务契约 tasks/foundation/WP-17-T10.json acceptance 1/2
 *
 * 测试策略（黄金数据独立性——analytic-case 的 lint 义务）：期望值全部由
 * 数据集 generate/ 脚本拉格朗日封闭式推导（与产品 RNEA 递归牛顿—欧拉不
 * 同推导路线、零共享代码），本文件只做"装载→驱动产品入口→经档案容差对
 * 照"三件事（不书写参考数值公式——inputs 内手算锚点除外，锚点独立于参
 * 考实现双路防"两路同错"）。驱动入口＝产品公共接口
 * （InverseDynamicsEvaluator/DynamicsSeriesBuilder/DynamicsEnvelopeCalculator/
 * PowerEnergyCalculator——经公共类消费钉扎交付面，WP-20-T03 接口路径覆盖
 * 教训的正面落实）。能量分项（J）不在 core 单位表（P-DYN-9）——不经档
 * 案通道，以测试侧相对容差断言复核（dt-mapping 同款先例）。
 *
 * 线程约束：gtest 用例天然串行；评估器/统计器无状态纯函数（卡 §10.0）。
 */

#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/dynamics/DynTypes.hpp>
#include <sdurws/ird/dynamics/Envelope.hpp>
#include <sdurws/ird/dynamics/Errors.hpp>
#include <sdurws/ird/dynamics/InverseDynamics.hpp>
#include <sdurws/ird/dynamics/PowerEnergy.hpp>
#include <sdurws/ird/dynamics/SeriesBuilder.hpp>
#include <sdurws/ird/evidence/Snapshot.hpp>            // RequiredCaseSet/CaseEntry（包络覆盖分母）
#include <sdurws/ird/runtime/BaseWorldTransform.hpp>   // gravityToBase——重力投影规则单点
#include <sdurws/ird/runtime/CanonicalModel.hpp>
#include <sdurws/ird/testkit/Dataset.hpp>
#include <sdurws/ird/testkit/JsonLite.hpp>
#include <sdurws/ird/testkit/ToleranceProfile.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>
#include <sdurws/ird/testkit/gtest/GoldenFixture.hpp>

#include <gtest/gtest.h>

#include "TwoLinkFixture.hpp"  // 同单元测试夹具（R-2 允许——同目录自持直构基准）

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace dyn = sdurws::ird::dynamics;
namespace core = sdurws::ird::core;
namespace tk = sdurws::ird::testkit;
namespace dynfix = sdurws::ird::dynamics::testfixture;
namespace runtime = sdurws::ird::runtime;
namespace evidence = sdurws::ird::evidence;
// IRD_EXPECT_* 宏展开为无命名空间限定的 checkCloseWithin 等标识符
// （AssertMacros 契约——宏仅在"已开 testkit 可见性"的消费 TU 使用）。
using namespace sdurws::ird::testkit;

// =====================================================================
// 黄金 JSON 读取脚手架（数据集装载/完整性已由 GoldenFixture 校验；此处
// 只做字段提取——字段缺失记失败并回退安全值，防越界崩溃掩盖首因）
// =====================================================================

namespace {

/// 读数据集文本文件（inputs/expected 侧相对路径经 dataset 解析——路径必须
/// 登记于 manifest，越界抛 TestKitError）。
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

/// 必填对象字段（缺失记失败并返回空对象——调用方继续走安全路径）。
tk::JsonValue field(const tk::JsonValue& o, const char* key, tk::JsonValue fallback)
{
    const tk::JsonValue* v = o.find(key);
    if (v == nullptr) {
        ADD_FAILURE() << "黄金数据缺字段: " << key;
        return fallback;
    }
    return *v;
}

/// 布尔字段读取（JsonLite Bool 类别——boolean 成员；缺失记失败回退 false）。
bool boolOf(const tk::JsonValue& o, const char* key)
{
    const tk::JsonValue* v = o.find(key);
    if (v == nullptr || !v->isBool()) {
        ADD_FAILURE() << "黄金数据缺布尔字段: " << key;
        return false;
    }
    return v->boolean;
}

double num(const tk::JsonValue& o, const char* key)
{
    const tk::JsonValue* v = o.find(key);
    if (v == nullptr || !v->isNumber()) {
        ADD_FAILURE() << "黄金数据缺数值字段: " << key;
        return 0.0;
    }
    return v->number;
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
        } else {
            ADD_FAILURE() << "黄金数据数组含非数值元素: " << key;
        }
    }
    return out;
}

/// 按 id 查 inputs/expected 算例（找不到记失败并返回空对象）。
tk::JsonValue caseById(const tk::JsonValue& arr, const char* id)
{
    for (const tk::JsonValue& c : arr.items) {
        if (c.isObject() && c.find("id") != nullptr && c.find("id")->text == id) {
            return c;
        }
    }
    ADD_FAILURE() << "黄金数据缺算例: " << id;
    return tk::JsonValue{};
}

/// 工况 id 派生（与生成脚本同一约定：SHA-256("dyn-golden-cond-<id>") 前
/// 16 字节——包络并列决胜按字节字典序，两侧必须同派生）。
core::ObjectId goldenConditionId(const std::string& caseId)
{
    return dynfix::idFrom<core::ObjectId>("dyn-golden-cond-" + caseId);
}

/// ObjectId → 32 位小写十六进制（与 expected 内 conditionIdHex 对账）。
std::string hexOf(const core::ObjectId& id)
{
    static const char* kDigits = "0123456789abcdef";
    std::string s;
    s.reserve(32);
    for (const std::uint8_t b : id.bytes) {
        s.push_back(kDigits[b >> 4]);
        s.push_back(kDigits[b & 0x0F]);
    }
    return s;
}

/// 测试侧相对容差断言（能量 J 面——档案通道不承载 J 量纲，P-DYN-9；附录
/// D 第 9 项同口径：|a−e| ≤ 1e-9·|e|，e==0 退化绝对 1e-12）。
::testing::AssertionResult closeRel(const char* actualExpr, const char* expectedExpr,
                                    double actual, double expected)
{
    const double tol = 1e-9 * std::fabs(expected);
    const double diff = std::fabs(actual - expected);
    if (diff <= tol || (expected == 0.0 && diff <= 1e-12)) {
        return ::testing::AssertionSuccess();
    }
    return ::testing::AssertionFailure()
           << "actual（" << actualExpr << "）=" << actual
           << "；expected（" << expectedExpr << "）=" << expected
           << "；diff=" << diff << "（容差 1e-9 相对——附录 D 第 9 项）";
}

// =====================================================================
// 产品链驱动助手（评估→序列冻结→工况结果装配——全部经公共接口消费）
// =====================================================================

/// 激励样本（从黄金 inputs 样本行拼装——q/q̇/q̈ 列直拷，本域不重算）。
dyn::InverseDynSampleInput sampleFrom(const tk::JsonValue& s)
{
    dyn::InverseDynSampleInput in;
    in.t = num(s, "t");                                    // s
    in.segmentIndex = static_cast<std::uint32_t>(num(s, "segment"));
    in.q = numVec(s, "q");                                 // rad
    in.qd = numVec(s, "qd");                               // rad/s
    in.qdd = numVec(s, "qdd");                             // rad/s²
    return in;
}

/// 序列身份块（同 StatisticsTest 形态——测试值确定性派生；validitySeed 由
/// 调用方以评估器 validity 透传）。
dyn::SeriesIdentity makeSeriesIdentity(const std::string& tag, std::size_t planned,
                                       const core::ObjectId& condId,
                                       const dyn::DynamicsValidity& seed)
{
    dyn::SeriesIdentity id;
    id.snapshotId.bytes = dynfix::digestOf(tag + "-snap");
    id.sliceId.bytes = dynfix::digestOf(tag + "-slice");
    id.trajectoryPayloadId.bytes = dynfix::digestOf(tag + "-trj");
    id.conditionId = condId;
    id.toolObjectId = core::ObjectId{};          // 无工具模型（合法空 id）
    id.dynConfigDigest = "sha256-" + tag;        // config.dyn 摘要（测试值非空即可）
    id.task.project = dynfix::idFrom<core::ProjectId>(tag + "-prj");
    id.task.branch = dynfix::idFrom<core::BranchId>(tag + "-brn");
    id.task.revision = dynfix::idFrom<core::RevisionId>(tag + "-rev");
    id.task.run = dynfix::idFrom<core::RunId>(tag + "-run");
    id.task.attempt.value = 1u;                  // AttemptId＝u64 尝试序号（≥1 合法）
    id.plannedSampleCount = planned;
    id.validitySeed = seed;
    return id;
}

/// 评估黄金算例（请求组装消费 runtime gravityToBase 单点——同一编译变换
/// 的重力投影消费链，D-DYN-3 零二次旋转）。
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

    class Ctx final : public sdurws::ird::evidence::IEvaluationContext {
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

/// 工况结果装配（序列冻结＋峰值/RMS＋功率能量——§10.4/§10.5 唯一实现点
/// 产出；OperatingConditionResult 的编排层装配契约面）。
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
    result.caseScope.push_back(series.conditionId);  // 单工况自身（EVI-02 关联）
    result.series = series;
    result.peaks = env.computePeaks(series);
    result.powerEnergy = pe.compute(series);
    result.validity = series.validity;
    return result;
}

}  // namespace

// =====================================================================
// 黄金夹具（§6.1 六步生命周期——数据集装载/完整性/档案就绪由基类执行）
// =====================================================================

class DynGolden : public tk::GoldenFixture
{
protected:
    tk::DatasetRef datasetRef() const override
    {
        return {"dyn-two-link-analytic", "1.0.0"};
    }

    /// 黄金模型构建（inputs 数值与测试夹具基准几何一致性的漂移守卫——
    /// 失配即数据/夹具漂移缺陷，先记失败再按夹具常量构建：黄金对照仍会
    /// 失败，双信号定位）。mounting＝地面/倒挂；frictionProvided＝干净黄
    /// 金模型（正小值三元组 Provided——runtime 合法域要求＞0）/降级对照面（三元组全缺 NotProvided）。
    runtime::CanonicalModel buildModel(const tk::JsonValue& inputs, bool inverted,
                                       bool frictionProvided)
    {
        // ---- 黄金模型与测试夹具基准几何的一致性漂移守卫（逐字段精确
        // 比对——失配记失败：数据/夹具漂移缺陷，黄金对照随后的失败即双
        // 信号定位；构建仍按夹具常量继续——EXPECT 在非 void 函数合法）。
        const tk::JsonValue m = field(inputs, "model", {});
        const tk::JsonValue links = field(m, "links", {});
        const bool linksOk = links.isArray() && links.items.size() >= 2U;
        EXPECT_TRUE(linksOk) << "黄金数据 model.links 非法（须为两连杆数组）";
        if (linksOk) {
            EXPECT_NEAR(num(links.items[0], "massKg"), dynfix::kM1, 0.0);           // kg
            EXPECT_NEAR(num(links.items[0], "comFromJointM"), dynfix::kC1, 0.0);    // m
            EXPECT_NEAR(num(links.items[0], "inertiaYyAboutCom"), dynfix::kI1Yy,
                        0.0);                                                       // kg·m²
            EXPECT_NEAR(num(links.items[1], "massKg"), dynfix::kM2, 0.0);           // kg
            EXPECT_NEAR(num(links.items[1], "comFromJointM"), dynfix::kC2, 0.0);    // m
            EXPECT_NEAR(num(links.items[1], "inertiaYyAboutCom"), dynfix::kI2Yy,
                        0.0);                                                       // kg·m²
        } else {
            ADD_FAILURE() << "黄金数据 model.links 缺失——跳过几何漂移守卫";
        }
        EXPECT_NEAR(num(m, "link1LengthM"), dynfix::kL1, 0.0);                  // m
        const tk::JsonValue fric = field(m, "friction", {});
        EXPECT_EQ(boolOf(fric, "provided"), frictionProvided);
        if (frictionProvided) {
            // 正小值三元组＝Provided（runtime 构建器合法域要求 Provided 摩擦
            // 值＞0——§4.3.3；干净算例——frictionMissing=false）。
            EXPECT_DOUBLE_EQ(num(fric, "viscous"), 0.05);  // N·m·s/rad
            EXPECT_DOUBLE_EQ(num(fric, "coulomb"), 0.10);  // N·m
            EXPECT_DOUBLE_EQ(num(fric, "bias"), 0.01);     // N·m
        }
        // 摩擦注入（MDL-16 三元组——provided=正小值全 Provided；否则全缺）。
        const dynfix::FrictionSpec fricSpec
            = frictionProvided ? dynfix::FrictionSpec{true, 0.05, 0.10, 0.01}
                               : dynfix::FrictionSpec{};
        return dynfix::makeTwoLinkModel(inverted ? dynfix::invertedWorld()
                                                 : dynfix::groundWorld(),
                                        fricSpec, std::nullopt);
    }
};

// =====================================================================
// ACC2 登记面：dyn-* 数据集按 DatasetManifest 登记的机器钉扎
// =====================================================================

/**
 * manifest 登记面（acceptance 2）：kind=analytic-case（可独立正确性依据）、
 * 附录 D C4 三布尔全 true＋样例引用非空、referenceSource.independentOf-
 * ProductionImpl=true（独立性声明）、生成脚本 committed、integrity 覆盖
 * inputs+expected+generate 三文件（装载器已逐文件 sha256/sizeBytes 校验
 * ——本用例钉登记面本身）；需求/AT 追溯面含 DYN-01/AT-07。装载期校验任
 * 一失败时 fixture 已 GSKIP（dataset-invalid）——能进入本用例即装载面已过。
 */
TEST_F(DynGolden, DatasetRegistrationFace_WP17T10_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-01", "DYN-03", "NFR-COR-01"},
                  std::vector<std::string>{"AT-07"}, datasetRef());
    ASSERT_TRUE(dataset.has_value());
    ASSERT_TRUE(profile.has_value());

    const tk::DatasetManifest& mf = dataset->manifest();
    EXPECT_EQ(mf.schemaVersion, "ird-golden-manifest/1");
    EXPECT_EQ(mf.datasetId, "dyn-two-link-analytic");
    EXPECT_EQ(mf.version, "1.0.0");
    EXPECT_EQ(mf.kind, tk::DatasetKind::AnalyticCase);   // analytic-case＝可独立正确性依据
    EXPECT_EQ(mf.scenarioCategory, "dyn/two-link-analytic");

    // 附录 D C4：黄金数据集必含零值/近零值/正负抵消样例（lint 强制——三
    // 布尔全 true＋sampleRefs 指向 expected 内具体条目）。
    ASSERT_TRUE(mf.edgeCasesPresent);
    EXPECT_TRUE(mf.edgeCases.zeroValue);
    EXPECT_TRUE(mf.edgeCases.nearZero);
    EXPECT_TRUE(mf.edgeCases.signCancellation);
    EXPECT_GE(mf.edgeCases.sampleRefs.size(), 4U);       // 三类样例逐例给出定位

    // 独立性声明（testkit §4.2.2：analytic-case 必填且 independent=true）。
    ASSERT_TRUE(mf.referenceSourcePresent);
    EXPECT_TRUE(mf.referenceSource.independentOfProductionImpl);
    EXPECT_EQ(mf.referenceSource.method, "closed-form");

    // 生成脚本入库＋完整性申报覆盖三文件（inputs/expected/generate）。
    ASSERT_TRUE(mf.generatorPresent);
    EXPECT_TRUE(mf.generatorCommitted);
    ASSERT_EQ(mf.integrity.size(), 3U);

    // 需求/AT 追溯面（acceptance 的挂靠 ID 逐项在册）。
    const auto contains = [](const std::vector<std::string>& v, const char* x) {
        return std::find(v.begin(), v.end(), x) != v.end();
    };
    EXPECT_TRUE(contains(mf.coveredRequirements, "DYN-01"));
    EXPECT_TRUE(contains(mf.coveredRequirements, "DYN-03"));
    EXPECT_TRUE(contains(mf.coveredRequirements, "NFR-COR-01"));
    EXPECT_TRUE(contains(mf.coveredAt, "AT-07"));
    EXPECT_TRUE(contains(mf.coveredAt, "AT-37"));

    // 容差档案引用与版本（GoldenFixture 步骤③已按此装载成功）。
    EXPECT_EQ(mf.toleranceProfileId, "dyn-two-link");
    EXPECT_EQ(mf.toleranceProfileVersion, "1.0.0");
}

// =====================================================================
// 黄金算例 1：静态重力矩（地面安装）——含精确零分项与手算锚点
// =====================================================================

/**
 * 黄金算例 static-ground（acceptance 2"静态重力矩"面）：q̇=q̈=0 逐姿态静
 * 态评估，五分项/总力矩/功率/能量逐样本经档案容差对照 expected（拉格朗
 * 日封闭式独立参考）；静态行惯性/科氏/功率/能量**精确零**（EXPECT_EQ——
 * 零值样例义务）；手算锚点（静力矩平衡 r×F 第三路）独立钉扎量级。
 */
TEST_F(DynGolden, StaticGroundAnalyticGolden_WP17T10_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-01", "DYN-02", "NFR-COR-01"},
                  std::vector<std::string>{"AT-37"}, datasetRef());
    ASSERT_TRUE(dataset.has_value());
    ASSERT_TRUE(profile.has_value());

    const tk::JsonValue inputs = loadJson(*dataset, "inputs/dyn-two-link-inputs.json", true);
    const tk::JsonValue expected = loadJson(*dataset, "expected/dyn-two-link-expected.json", false);
    const tk::JsonValue inCase = caseById(field(inputs, "cases", {}), "static-ground");
    const tk::JsonValue expCase = caseById(field(expected, "cases", {}), "static-ground");
    ASSERT_TRUE(inCase.isObject());
    ASSERT_TRUE(expCase.isObject());

    const runtime::CanonicalModel model = buildModel(inputs, false, true);
    const core::ObjectId cond = goldenConditionId("static-ground");
    std::vector<dyn::InverseDynSampleInput> samples;
    for (const tk::JsonValue& s : field(inCase, "samples", {}).items) {
        samples.push_back(sampleFrom(s));
    }
    const dyn::InverseDynOutcome out = evaluateGoldenCase(model, cond, samples);
    ASSERT_EQ(out.samples.size(), samples.size() * 2U);   // 逐时刻×2 关节行
    EXPECT_EQ(out.validity.completeness, dyn::DynamicsValidity::Completeness::Complete);
    EXPECT_FALSE(out.validity.frictionMissing);           // 正小值三元组 Provided——无降级

    // ---- 逐样本逐字段档案对照（行序 (t, jointIndex)——§4.6 排序纪律）。
    const tk::JsonValue expSamples = field(expCase, "samples", {});
    ASSERT_EQ(expSamples.items.size(), samples.size());
    for (std::size_t i = 0; i < expSamples.items.size(); ++i) {
        const tk::JsonValue& e = expSamples.items[i];
        for (std::size_t j = 0; j < 2; ++j) {
            const dyn::DynamicsSample& r = out.samples[i * 2 + j];
            const tk::JsonValue& ej = e.find("joints")->items[j];
            const std::string pfx = "cases[0].samples[" + std::to_string(i)
                                  + "].joints[" + std::to_string(j) + "]";
            const std::string spfx
                = "cases[0].samples[" + std::to_string(i) + "]";  // 样本级路径（t）
            SCOPED_TRACE(pfx);
            EXPECT_EQ(r.jointIndex, j);
            EXPECT_EQ(r.numericState, dyn::SampleNumericState::Ok);
            IRD_EXPECT_CLOSE(spfx + ".t", r.t, num(e, "t"), *profile, "s");
            IRD_EXPECT_CLOSE(pfx + ".q", r.q, num(ej, "q"), *profile, "rad");
            IRD_EXPECT_CLOSE(pfx + ".qd", r.qd, num(ej, "qd"), *profile, "rad/s");
            IRD_EXPECT_CLOSE(pfx + ".qdd", r.qdd, num(ej, "qdd"), *profile, "rad/s^2");
            IRD_EXPECT_CLOSE(pfx + ".tauGravity", r.tauGravity, num(ej, "tauGravity"),
                             *profile, "N*m");
            IRD_EXPECT_CLOSE(pfx + ".tauInertia", r.tauInertia, num(ej, "tauInertia"),
                             *profile, "N*m");
            IRD_EXPECT_CLOSE(pfx + ".tauCoriolisCentrifugal", r.tauCoriolisCentrifugal,
                             num(ej, "tauCoriolisCentrifugal"), *profile, "N*m");
            IRD_EXPECT_CLOSE(pfx + ".tauFriction", r.tauFriction, num(ej, "tauFriction"),
                             *profile, "N*m");
            IRD_EXPECT_CLOSE(pfx + ".tauTotal", r.tauTotal, num(ej, "tauTotal"),
                             *profile, "N*m");
            IRD_EXPECT_CLOSE(pfx + ".mechanicalPower", r.mechanicalPower,
                             num(ej, "mechanicalPower"), *profile, "W");
            // 五分项恒等式（§5.2——分项可加性逐样本成立）。
            const double sum = r.tauGravity + r.tauInertia + r.tauCoriolisCentrifugal
                             + r.tauFriction + r.tauExternal;
            EXPECT_NEAR(r.tauTotal, sum, 1e-9 * std::max(1.0, std::fabs(r.tauTotal)));
            // 能量积分状态（J——档案通道不承载，测试侧相对容差；静态恒 0）。
            EXPECT_PRED_FORMAT2(closeRel, r.energyIntegralJ, num(ej, "energyIntegralJ"));
            // 零值样例（静态行）：惯性/科氏/功率/能量**精确零**——绝不做
            // 0 值伪装的反向钉扎（NFR-COR-03）；摩擦＝bias（q̇=0 ⇒ 库仑项
            // sgn₀(0)=0 零贡献、黏性零贡献——D-DYN-7 的黄金面，经档案对照）。
            EXPECT_EQ(r.tauInertia, 0.0);
            EXPECT_EQ(r.tauCoriolisCentrifugal, 0.0);
            EXPECT_EQ(r.mechanicalPower, 0.0);
            EXPECT_EQ(r.energyIntegralJ, 0.0);
        }
    }
    // 近零样例钉扎（q=(π/2,0) 臂竖直悬挂——重力矩噪声级）。
    {
        const dyn::DynamicsSample& r = out.samples[2 * 2 + 0];  // samples[2].joints[0]
        EXPECT_LT(std::fabs(r.tauGravity), 1e-12);              // N·m（cos(π/2) 噪声级）
    }

    // ---- 手算锚点（独立第三路——静力矩平衡 r×F，不依赖拉格朗日公式；
    // 绝对量级钉扎，防"参考实现与产品实现两路同错"）。
    const tk::JsonValue& anc = field(inCase, "anchors", {});
    const std::size_t ai = static_cast<std::size_t>(num(anc, "sampleIndex"));
    EXPECT_NEAR(out.samples[ai * 2 + 0].tauGravity, num(field(anc, "joint0", {}), "tauGravity"),
                1e-9);  // N·m
    EXPECT_NEAR(out.samples[ai * 2 + 1].tauGravity, num(field(anc, "joint1", {}), "tauGravity"),
                1e-9);  // N·m
}

// =====================================================================
// 黄金算例 2：倒挂静态重力矩（AT-37 动力侧——符号翻转、量值相等）
// =====================================================================

/**
 * 黄金算例 static-inverted（acceptance 2"倒挂重力矩"面）：同一冻结链仅安
 * 装姿态编译差异（T_world_base＝R_x(π)），g_base=(0,0,+9.81)（gravityToBase
 * 投影单点），静态重力矩逐样本反号、量值相等（AT-37——重力入口只有 g_base
 * 投影向量，二次旋转在接口面结构上不可表达）；逐样本经档案对照 expected。
 */
TEST_F(DynGolden, StaticInvertedGoldenAt37_WP17T10_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-22", "DYN-01"},
                  std::vector<std::string>{"AT-37"}, datasetRef());
    ASSERT_TRUE(dataset.has_value());
    ASSERT_TRUE(profile.has_value());

    const tk::JsonValue inputs = loadJson(*dataset, "inputs/dyn-two-link-inputs.json", true);
    const tk::JsonValue expected = loadJson(*dataset, "expected/dyn-two-link-expected.json", false);
    const tk::JsonValue inCase = caseById(field(inputs, "cases", {}), "static-inverted");
    const tk::JsonValue expCase = caseById(field(expected, "cases", {}), "static-inverted");
    ASSERT_TRUE(inCase.isObject());
    ASSERT_TRUE(expCase.isObject());

    // 倒挂投影自检（消费同一编译变换的证据链——倒挂基座系重力沿 +Z）。
    const runtime::CanonicalModel inverted = buildModel(inputs, true, true);
    const rw::math::Vector3D<double> gBaseInv =
        sdurws::ird::runtime::gravityToBase(inverted.world().T_world_base.R(),
                                            inverted.world().gravityWorld);
    EXPECT_NEAR(gBaseInv[2], 9.81, 1e-12);                // m/s²（+Z——倒挂投影）

    const core::ObjectId cond = goldenConditionId("static-inverted");
    std::vector<dyn::InverseDynSampleInput> samples;
    for (const tk::JsonValue& s : field(inCase, "samples", {}).items) {
        samples.push_back(sampleFrom(s));
    }
    const dyn::InverseDynOutcome out = evaluateGoldenCase(inverted, cond, samples);
    ASSERT_EQ(out.samples.size(), samples.size() * 2U);

    // 地面同数据对照（符号翻转面——两算例输入相同、唯一差异＝安装姿态）。
    const runtime::CanonicalModel ground = buildModel(inputs, false, true);
    const dyn::InverseDynOutcome outG = evaluateGoldenCase(
        ground, goldenConditionId("static-ground"), samples);

    const tk::JsonValue expSamples = field(expCase, "samples", {});
    ASSERT_EQ(expSamples.items.size(), samples.size());
    for (std::size_t i = 0; i < expSamples.items.size(); ++i) {
        const tk::JsonValue& e = expSamples.items[i];
        for (std::size_t j = 0; j < 2; ++j) {
            const dyn::DynamicsSample& r = out.samples[i * 2 + j];
            const tk::JsonValue& ej = e.find("joints")->items[j];
            const std::string pfx = "cases[1].samples[" + std::to_string(i)
                                  + "].joints[" + std::to_string(j) + "]";
            SCOPED_TRACE(pfx);
            IRD_EXPECT_CLOSE(pfx + ".tauGravity", r.tauGravity, num(ej, "tauGravity"),
                             *profile, "N*m");
            IRD_EXPECT_CLOSE(pfx + ".tauTotal", r.tauTotal, num(ej, "tauTotal"),
                             *profile, "N*m");
            IRD_EXPECT_CLOSE(pfx + ".mechanicalPower", r.mechanicalPower,
                             num(ej, "mechanicalPower"), *profile, "W");
            // AT-37：倒挂＝地面反号、量值相等——符号翻转只作用于**重力通
            // 道**（重力经 g_base 线性进入）；摩擦项与安装姿态无关（零速行
            // ＝bias 两算例恒同），总力矩不作镜像断言（总力矩差异＝2·bias
            // ——黄金算例 tauTotal 列已经档案逐行对照承载）。
            const dyn::DynamicsSample& rg = outG.samples[i * 2 + j];
            EXPECT_NEAR(r.tauGravity, -rg.tauGravity,
                        1e-9 * std::max(1.0, std::fabs(rg.tauGravity)));
        }
    }
}

// =====================================================================
// 黄金算例 3：完整任务循环（运动段＋驻留段——AT-07"完整循环"主面）
// =====================================================================

/**
 * 黄金算例 motion-dwell（acceptance 1"完整循环包络"面）：非均匀采样运动
 * 段（7 样本）＋驻留段（3 样本）——①逐样本五分项/功率/能量经档案对照；
 * ②序列→computePeaks 12 行（每关节 6 token：值经档案、发生时间/所在段/
 * 位等值窗逐项对照）；③时间加权 RMS 含驻留经档案（≠样本平均）；④E⁺/E⁻
 * 正负功分项（正负抵消样例——E⁺>0>E⁻）与 powerPeak 幅值经档案＋测试侧
 * 相对容差；⑤零值样例（驻留行功率/惯性/科氏精确零）与近零样例（q̇₁≈3e-16
 * →功率噪声级）钉扎。
 */
TEST_F(DynGolden, MotionDwellFullCycleGolden_WP17T10_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-01", "DYN-03"},
                  std::vector<std::string>{"AT-07"}, datasetRef());
    ASSERT_TRUE(dataset.has_value());
    ASSERT_TRUE(profile.has_value());

    const tk::JsonValue inputs = loadJson(*dataset, "inputs/dyn-two-link-inputs.json", true);
    const tk::JsonValue expected = loadJson(*dataset, "expected/dyn-two-link-expected.json", false);
    const tk::JsonValue inCase = caseById(field(inputs, "cases", {}), "motion-dwell");
    const tk::JsonValue expCase = caseById(field(expected, "cases", {}), "motion-dwell");
    ASSERT_TRUE(inCase.isObject());
    ASSERT_TRUE(expCase.isObject());

    const runtime::CanonicalModel model = buildModel(inputs, false, true);
    const core::ObjectId cond = goldenConditionId("motion-dwell");
    std::vector<dyn::InverseDynSampleInput> samples;
    for (const tk::JsonValue& s : field(inCase, "samples", {}).items) {
        samples.push_back(sampleFrom(s));
    }
    const dyn::InverseDynOutcome out = evaluateGoldenCase(model, cond, samples);
    ASSERT_EQ(out.samples.size(), samples.size() * 2U);
    EXPECT_EQ(out.validity.completeness, dyn::DynamicsValidity::Completeness::Complete);

    // ---- ①逐样本逐字段档案对照。
    const tk::JsonValue expSamples = field(expCase, "samples", {});
    ASSERT_EQ(expSamples.items.size(), samples.size());
    for (std::size_t i = 0; i < expSamples.items.size(); ++i) {
        const tk::JsonValue& e = expSamples.items[i];
        for (std::size_t j = 0; j < 2; ++j) {
            const dyn::DynamicsSample& r = out.samples[i * 2 + j];
            const tk::JsonValue& ej = e.find("joints")->items[j];
            const std::string pfx = "cases[2].samples[" + std::to_string(i)
                                  + "].joints[" + std::to_string(j) + "]";
            SCOPED_TRACE(pfx);
            IRD_EXPECT_CLOSE(pfx + ".tauGravity", r.tauGravity, num(ej, "tauGravity"),
                             *profile, "N*m");
            IRD_EXPECT_CLOSE(pfx + ".tauInertia", r.tauInertia, num(ej, "tauInertia"),
                             *profile, "N*m");
            IRD_EXPECT_CLOSE(pfx + ".tauCoriolisCentrifugal", r.tauCoriolisCentrifugal,
                             num(ej, "tauCoriolisCentrifugal"), *profile, "N*m");
            IRD_EXPECT_CLOSE(pfx + ".tauFriction", r.tauFriction, num(ej, "tauFriction"),
                             *profile, "N*m");
            IRD_EXPECT_CLOSE(pfx + ".tauTotal", r.tauTotal, num(ej, "tauTotal"),
                             *profile, "N*m");
            IRD_EXPECT_CLOSE(pfx + ".mechanicalPower", r.mechanicalPower,
                             num(ej, "mechanicalPower"), *profile, "W");
            const double sum = r.tauGravity + r.tauInertia + r.tauCoriolisCentrifugal
                             + r.tauFriction + r.tauExternal;
            EXPECT_NEAR(r.tauTotal, sum, 1e-9 * std::max(1.0, std::fabs(r.tauTotal)));
            EXPECT_PRED_FORMAT2(closeRel, r.energyIntegralJ, num(ej, "energyIntegralJ"));
        }
    }
    // 零值样例（驻留行 i=7..9，segment=1）：q̇=q̈=0 ⇒ 功率/惯性/科氏精确零
    // （摩擦＝bias——库仑项 sgn₀(0)=0 零贡献，经档案逐行对照）。
    for (std::size_t i = 7; i < 10; ++i) {
        for (std::size_t j = 0; j < 2; ++j) {
            const dyn::DynamicsSample& r = out.samples[i * 2 + j];
            SCOPED_TRACE("驻留零值样例 samples[" + std::to_string(i) + "].joints["
                         + std::to_string(j) + "]");
            EXPECT_EQ(r.segmentIndex, 1U);
            EXPECT_EQ(r.mechanicalPower, 0.0);              // W（τ·0＝精确零）
            EXPECT_EQ(r.tauInertia, 0.0);                   // N·m（M·0）
            EXPECT_EQ(r.tauCoriolisCentrifugal, 0.0);       // N·m（h·0）
        }
    }
    // 近零样例（samples[3]＝t=1.0，q̇₁=cos(π/2)·π²/2≈3.0e-16 噪声级）。
    {
        const dyn::DynamicsSample& r = out.samples[3 * 2 + 0];
        SCOPED_TRACE("近零样例 samples[3].joints[0]");
        EXPECT_LT(r.qd, 1e-12);                             // rad/s（噪声级速度）
        EXPECT_GT(r.qd, 0.0);                               // 正号（cos(π/2)>0）
        EXPECT_LT(std::fabs(r.mechanicalPower), 1e-12);     // W（近零功率——非伪造零）
    }

    // ---- ②序列冻结＋峰值对照（computePeaks 唯一统计口径实现点）。
    const dyn::OperatingConditionResult result = assembleCondition(out, "golden-motion-dwell");
    ASSERT_EQ(result.series.samples.size(), samples.size() * 2U);
    const dyn::DynamicsEnvelopeCalculator env;
    const std::vector<dyn::PeakRecord> peaks = env.computePeaks(result.series);
    const tk::JsonValue expPeaks = field(field(expCase, "stats", {}), "peaks", {});
    ASSERT_EQ(peaks.size(), 12U);                          // 2 关节 × 6 token（§7.6 全序）
    ASSERT_EQ(expPeaks.items.size(), 12U);
    for (std::size_t k = 0; k < 12; ++k) {
        const tk::JsonValue& e = expPeaks.items[k];
        const std::size_t j = static_cast<std::size_t>(num(e, "jointIndex"));
        const int token = static_cast<int>(num(e, "token"));
        static const char* kTokenPath[6] = {"tauPos", "tauNeg", "velocity", "acceleration",
                                            "powerPos", "powerNeg"};
        const std::string vp = "cases[2].stats.peak." + std::string{kTokenPath[token]};
        const std::string tp = "cases[2].stats.peak.";
        SCOPED_TRACE("峰值行 j=" + std::to_string(j) + " token=" + std::to_string(token)
                     + "（行序＝(jointIndex 升序, token 序) 全序——行下标 k=j*6+token 对位）");
        EXPECT_EQ(peaks[k].conditionId, cond);             // 来源工况透传
        IRD_EXPECT_CLOSE(vp, peaks[k].value, num(e, "value"), *profile,
                         token >= 4 ? "W" : (token == 2 ? "rad/s" : "N*m"));
        IRD_EXPECT_CLOSE(tp + "tPeak", peaks[k].tPeakS, num(e, "tPeakS"), *profile, "s");
        EXPECT_EQ(peaks[k].segmentIndex,
                  static_cast<std::uint32_t>(num(e, "segmentIndex")));
        IRD_EXPECT_CLOSE(tp + "windowStart", peaks[k].windowStartS, num(e, "windowStartS"),
                         *profile, "s");
        IRD_EXPECT_CLOSE(tp + "windowEnd", peaks[k].windowEndS, num(e, "windowEndS"),
                         *profile, "s");
    }

    // ---- ③完整循环时间加权 RMS（含驻留——≠样本平均）。
    const tk::JsonValue expRms = field(field(expCase, "stats", {}), "rmsTau", {});
    for (const tk::JsonValue& e : expRms.items) {
        const std::size_t j = static_cast<std::size_t>(num(e, "jointIndex"));
        const double rms = env.computeRms(result.series, static_cast<std::uint32_t>(j));
        IRD_EXPECT_CLOSE("cases[2].stats.rms.tau", rms, num(e, "rmsTau"), *profile, "N*m");
    }

    // ---- ④功率/能量分项（E⁺/E⁻ J 面测试侧容差；powerPeak 幅值经档案）。
    const dyn::PowerEnergySummary pe = result.powerEnergy;
    const tk::JsonValue expPe = field(field(expCase, "stats", {}), "powerEnergy", {});
    EXPECT_TRUE(pe.timeParamAvailable);
    EXPECT_TRUE(pe.includesDwell);                         // 驻留计入（Verified 口径）
    EXPECT_NEAR(pe.cycleDurationS, 3.2, 1e-12);            // s（完整循环全跨度含驻留）
    const tk::JsonValue expJoints = field(expPe, "joints", {});
    ASSERT_EQ(pe.joints.size(), expJoints.items.size());
    for (std::size_t j = 0; j < pe.joints.size(); ++j) {
        const tk::JsonValue& e = expJoints.items[j];
        SCOPED_TRACE("能量行 joints[" + std::to_string(j) + "]");
        EXPECT_EQ(pe.joints[j].jointIndex, j);
        // E⁺/E⁻/E_net/meanPower（J/W——P-DYN-9 测试侧相对容差）。
        EXPECT_PRED_FORMAT2(closeRel, pe.joints[j].positiveEnergyJ, num(e, "positiveEnergyJ"));
        EXPECT_PRED_FORMAT2(closeRel, pe.joints[j].negativeEnergyJ, num(e, "negativeEnergyJ"));
        EXPECT_PRED_FORMAT2(closeRel, pe.joints[j].netEnergyJ, num(e, "netEnergyJ"));
        EXPECT_PRED_FORMAT2(closeRel, pe.joints[j].meanPowerW, num(e, "meanPowerW"));
        // 正负抵消样例（joints[0]——E⁺>0>E⁻ 并存；E_net=E⁺+E⁻ 恒等式）。
        if (j == 0) {
            EXPECT_GT(pe.joints[j].positiveEnergyJ, 0.0);  // J（驱动/加速正功）
            EXPECT_LT(pe.joints[j].negativeEnergyJ, 0.0);  // J（制动负功）
            EXPECT_NEAR(pe.joints[j].netEnergyJ,
                        pe.joints[j].positiveEnergyJ + pe.joints[j].negativeEnergyJ, 0.0);
            // V-22 对照性质：E_net＝逐样本能量积分状态末值（评估器出口）。
            const dyn::DynamicsSample& last = out.samples[(samples.size() - 1) * 2 + 0];
            EXPECT_PRED_FORMAT2(closeRel, last.energyIntegralJ, pe.joints[j].netEnergyJ);
        }
        // 幅值功率峰（窗＋段——T04 幅值形态口径）。
        const tk::JsonValue& ep = field(e, "powerPeak", {});
        IRD_EXPECT_CLOSE("cases[2].stats.peak.powerAmp", pe.joints[j].powerPeak.value,
                         num(ep, "value"), *profile, "W");
        IRD_EXPECT_CLOSE("cases[2].stats.peak.tPeak", pe.joints[j].powerPeak.tPeakS,
                         num(ep, "tPeakS"), *profile, "s");
        EXPECT_EQ(pe.joints[j].powerPeak.segmentIndex,
                  static_cast<std::uint32_t>(num(ep, "segmentIndex")));
        IRD_EXPECT_CLOSE("cases[2].stats.peak.windowStart", pe.joints[j].powerPeak.windowStartS,
                         num(ep, "windowStartS"), *profile, "s");
        IRD_EXPECT_CLOSE("cases[2].stats.peak.windowEnd", pe.joints[j].powerPeak.windowEndS,
                         num(ep, "windowEndS"), *profile, "s");
    }
}

// =====================================================================
// 黄金算例 4：三工况包络合并（DYN-07 合并参考对照＋内容身份确定性）
// =====================================================================

/**
 * 三工况（静态地面/倒挂/运动驻留）装配为 OperatingConditionResult 后经
 * mergeEnvelope 合并，逐槽对照 expected.envelopeMerge（拉格朗日参考实现
 * 按 §7.4/T07 登记口径独立推导的合并参考）：①五峰值记录槽值经档案
 * （envelope.joints[*].peak.*）；②胜者工况归属逐位对账（conditionIdHex
 * ——并列取 conditionId 更小工况的确定性决胜）；③RMS 跨工况 max；④来源
 * 工况集（contributingConditions 升序）；⑤conditionCount＝results 条数
 * （在场口径 1——wp17-t07 登记口径）＋coversAllMandatory（三必验全在
 * →true）；⑥内容身份确定性（同输入重合并逐位同摘要，NFR-COR-02）。
 */
TEST_F(DynGolden, EnvelopeMergeGolden_WP17T10_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-03", "DYN-07"},
                  std::vector<std::string>{"AT-07"}, datasetRef());
    ASSERT_TRUE(dataset.has_value());
    ASSERT_TRUE(profile.has_value());

    const tk::JsonValue inputs = loadJson(*dataset, "inputs/dyn-two-link-inputs.json", true);
    const tk::JsonValue expected = loadJson(*dataset, "expected/dyn-two-link-expected.json", false);
    const runtime::CanonicalModel ground = buildModel(inputs, false, true);
    const runtime::CanonicalModel inverted = buildModel(inputs, true, true);

    // ---- 三工况端到端评估＋装配（评估→序列→统计→OperatingConditionResult）。
    std::vector<dyn::OperatingConditionResult> results;
    const char* kCaseIds[3] = {"static-ground", "static-inverted", "motion-dwell"};
    const runtime::CanonicalModel* kModels[3] = {&ground, &inverted, &ground};
    for (int c = 0; c < 3; ++c) {
        const tk::JsonValue inCase = caseById(field(inputs, "cases", {}), kCaseIds[c]);
        ASSERT_TRUE(inCase.isObject());
        std::vector<dyn::InverseDynSampleInput> samples;
        for (const tk::JsonValue& s : field(inCase, "samples", {}).items) {
            samples.push_back(sampleFrom(s));
        }
        const dyn::InverseDynOutcome out =
            evaluateGoldenCase(*kModels[c], goldenConditionId(kCaseIds[c]), samples);
        results.push_back(assembleCondition(out, std::string("golden-") + kCaseIds[c]));
    }

    // 覆盖分母＝快照冻结 RequiredCaseSet（三工况 enabled∧mandatory——EVI-02）。
    evidence::RequiredCaseSet coverage;
    for (const char* id : kCaseIds) {
        evidence::CaseEntry e;
        e.caseId = goldenConditionId(id);
        e.label = id;
        e.enabled = true;
        e.mandatory = true;
        coverage.entries.push_back(e);
    }

    const dyn::DynamicsEnvelopeCalculator env;
    const dyn::DynamicsEnvelope envelope = env.mergeEnvelope(results, coverage);
    const tk::JsonValue expEnv = field(expected, "envelopeMerge", {});
    ASSERT_EQ(envelope.conditionCount, 3U);                // 在场口径：results 条数
    EXPECT_TRUE(envelope.coversAllMandatory);              // 三必验全在（呈现参考）
    const tk::JsonValue expJoints = field(expEnv, "joints", {});
    ASSERT_EQ(envelope.joints.size(), expJoints.items.size());
    for (std::size_t j = 0; j < envelope.joints.size(); ++j) {
        const tk::JsonValue& e = expJoints.items[j];
        const dyn::DynamicsEnvelope::JointEnvelope& row = envelope.joints[j];
        SCOPED_TRACE("包络行 joints[" + std::to_string(j) + "]");
        EXPECT_EQ(row.jointIndex, j);
        EXPECT_TRUE(row.envelopeComplete);                 // 全部来源 Complete
        // 五峰值记录槽（token 4/5 已并入 powerPeak 单记录位——§4.4 包络
        // 结构仅 5 槽；槽序＝§7.2 token 序——值经档案、时间/段/窗逐项对照、
        // 胜者工况归属与 expected 的 conditionIdHex 逐位对账）。
        const std::pair<const char*, const dyn::PeakRecord&> slotTab[5] = {
            {"tauPos", row.tauMaxPositive},
            {"tauNeg", row.tauMaxNegative},
            {"velocity", row.velocityPeak},
            {"acceleration", row.accelerationPeak},
            {"powerAmp", row.powerPeak},
        };
        const char* kSlotKeys[5] = {"tauMaxPositive", "tauMaxNegative", "velocityPeak",
                                    "accelerationPeak", "powerPeak"};
        for (int s = 0; s < 5; ++s) {
            const tk::JsonValue& ep = field(e, kSlotKeys[s], {});
            const std::string base = std::string{"envelope.joints["} + std::to_string(j)
                                   + "].peak.";
            const std::string vp = base + slotTab[s].first;  // 值槽路径（量纲分槽）
            SCOPED_TRACE(vp);
            IRD_EXPECT_CLOSE(vp, slotTab[s].second.value, num(ep, "value"), *profile,
                             s == 2 ? "rad/s" : (s == 3 ? "rad/s^2"
                                                        : (s == 4 ? "W" : "N*m")));
            // 时间/窗路径＝槽位无关的样本级模板（与值槽同层——见档案条目）。
            IRD_EXPECT_CLOSE(base + "tPeak", slotTab[s].second.tPeakS, num(ep, "tPeakS"),
                             *profile, "s");
            EXPECT_EQ(slotTab[s].second.segmentIndex,
                      static_cast<std::uint32_t>(num(ep, "segmentIndex")));
            IRD_EXPECT_CLOSE(base + "windowStart", slotTab[s].second.windowStartS,
                             num(ep, "windowStartS"), *profile, "s");
            IRD_EXPECT_CLOSE(base + "windowEnd", slotTab[s].second.windowEndS,
                             num(ep, "windowEndS"), *profile, "s");
            // 胜者工况归属（§7.4——并列取 conditionId 更小工况；值/时间/段/
            // 窗随胜者整体归因）。
            const tk::JsonValue* wh = ep.find("conditionIdHex");
            if (wh != nullptr && wh->isString()) {
                EXPECT_EQ(hexOf(slotTab[s].second.conditionId), wh->text);
            }
        }
        // RMS 跨工况 max（包络＝最坏工况呈现——NaN 不参与）。
        IRD_EXPECT_CLOSE("envelope.rms.tau", row.rmsTau, num(e, "rmsTau"), *profile, "N*m");
        // 来源工况集（处理序升序＝ObjectId 字典序——与 expected 逐位对账）。
        const tk::JsonValue* ec = e.find("contributingConditionHexes");
        ASSERT_TRUE(ec != nullptr && ec->isArray());
        ASSERT_EQ(row.contributingConditions.size(), ec->items.size());
        for (std::size_t k = 0; k < row.contributingConditions.size(); ++k) {
            EXPECT_EQ(hexOf(row.contributingConditions[k]), ec->items[k].text);
        }
    }

    // ---- 内容身份确定性（同输入重合并逐位同摘要——NFR-COR-02）。
    const dyn::DynamicsEnvelope again = env.mergeEnvelope(results, coverage);
    EXPECT_EQ(again.contentIdentity.bytes, envelope.contentIdentity.bytes);
    EXPECT_TRUE(again.joints.size() == envelope.joints.size());
}

// =====================================================================
// 黄金算例 5：条件 id 派生对账（生成脚本与消费测试同约定——SHA-256 前 16
// 字节；包络并列决胜按字节字典序，两侧同派生是对账前提）
// =====================================================================

TEST_F(DynGolden, ConditionIdDerivationMatchesGenerator_WP17T10_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-02"}, std::vector<std::string>{},
                  datasetRef());
    ASSERT_TRUE(dataset.has_value());
    const tk::JsonValue expected = loadJson(*dataset, "expected/dyn-two-link-expected.json", false);
    for (const tk::JsonValue& c : field(expected, "cases", {}).items) {
        const std::string id = c.find("id")->text;
        // expected 内登记的 conditionIdHex（node:crypto SHA-256 派生）＝
        // 测试侧 core::ContentDigester 同算法派生——逐位一致。
        const core::ObjectId derived = goldenConditionId(id);
        const std::string registered = c.find("conditionIdHex")->text;
        EXPECT_EQ(hexOf(derived), registered) << "算例 " << id << " 的工况 id 派生失配";
    }
}

