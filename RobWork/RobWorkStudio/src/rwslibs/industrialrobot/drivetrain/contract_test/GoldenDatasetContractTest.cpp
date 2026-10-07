/**
 * @file   GoldenDatasetContractTest.cpp
 * @brief  传动映射黄金数据集的登记契约用例组（DtGoldenDatasetContract）
 *         ——WP-18-T04 acceptance 2"testdata/golden/dt-* 按 DatasetManifest
 *         登记"的契约自证面：数据集经 GoldenDataset::load 全量校验（schema
 *         →完整性→交叉校验）通过、manifest 字段契约（登记要求/追溯锚点/
 *         独立性声明）、inputs/expected 交叉一致、容差档案通道就绪与
 *         P-DT-3 保守纪律（不引入运行时阈值语义）钉扎。
 *
 * 设计依据：
 *   - units/testkit.md §4.2.2（DatasetManifest 字段表——登记契约的权威
 *     词表）、§4.5（size＋SHA-256 完整性）、§4.3.1/§4.3.2（容差档案 schema
 *     与"只准更严"规则）、§5.1（装载语义——本组用例即其消费面）
 *   - units/drivetrain.md §14.1（黄金数据集口径——testdata/golden/dt-*、
 *     独立参考实现、附录 D 第 9 项容差、P-DT-3 保守不引入）、§8.4（黄金
 *     对照归测试侧——运行侧只做精确判据）
 *   - 需求 NFR-COR-01（解析算例为独立正确性依据首选）、DYN-04、AT-38
 *   - 任务契约 tasks/foundation/WP-18-T04.json acceptance 2/3
 *
 * 测试策略：本组用例针对**数据资产**的登记契约（区别于 GoldenDtTest 的
 * 数值消费面）——装载/校验走 testkit 真实实现（GoldenDataset/Tolerance-
 * Profile 不桩化），断言 manifest 与档案的字段落值；数据缺陷按 testkit
 * §7.2 分类暴露（DatasetInvalid——不伪装算法回归）。
 *
 * 线程约束：gtest 用例天然串行。
 */

#include <sdurws/ird/testkit/Dataset.hpp>
#include <sdurws/ird/testkit/JsonLite.hpp>
#include <sdurws/ird/testkit/TestPaths.hpp>
#include <sdurws/ird/testkit/ToleranceProfile.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <sdurws/ird/core/Units.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace tk = sdurws::ird::testkit;
namespace core = sdurws::ird::core;

namespace {

/// 数据集引用（被测契约对象——WP-18-T04 登记的唯一 dt-* 数据集；
/// DatasetRef 含 std::string——C++17 无 constexpr 字面串构造，以函数承载）。
tk::DatasetRef goldenRef()
{
    return {"dt-mapping-golden", "1.0.0"};
}

/// 档案引用（manifest.toleranceProfile 指向——契约一致性断言的期望值）。
constexpr const char* kProfileId = "dt-mapping";
constexpr const char* kProfileVersion = "1.0.0";

/// 读数据集文本文件（完整性已由 GoldenDataset::load 校验——此处仅解析）。
std::string readText(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

}  // namespace

// =====================================================================
// 登记契约：GoldenDataset::load 全量校验通过＋manifest 字段落值
// =====================================================================

/**
 * DatasetManifest 登记契约（acceptance 2 的机器断言面）：
 *   - 装载本身即全量校验（schema→integrity SHA-256/size→交叉校验）——
 *     load 返回即"登记完整且未被篡改"的构造性证明；
 *   - kind=analytic-case（解析算例——NFR-COR-01 独立正确性依据类）；
 *   - 登记要求覆盖（coveredRequirements 含 DYN-04）与 AT 锚点（coveredAt
 *     含 AT-38——本任务 R1 可验部分的数据面归属）；
 *   - 档案引用指向 dt-mapping@1.0.0 且真实存在；
 *   - 独立性声明（referenceSource.independentOfProductionImpl——§14.1
 *     独立参考实现纪律的登记面）；
 *   - 边角样例三布尔（附录 D C4 lint 强制——零值/近零/正负抵消）；
 *   - 生成器入库声明（generator.committed——期望可复现）。
 */
TEST(DtGoldenDatasetContract, ManifestRegistration_WP18T04_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-01", "DYN-04"},
                  std::vector<std::string>{"AT-38"});

    // ---- 全量装载（任何校验失败抛 TestKitError——数据资产缺陷即失败）。
    const tk::GoldenDataset ds = tk::GoldenDataset::load(goldenRef());
    const tk::DatasetManifest& m = ds.manifest();

    // ---- schema 与定位一致性（datasetId/version 与目录名——§4.2.2）。
    EXPECT_EQ(m.schemaVersion, "ird-golden-manifest/1");
    EXPECT_EQ(m.datasetId, goldenRef().datasetId);
    EXPECT_EQ(m.version, goldenRef().version);

    // ---- kind=analytic-case（登记类别与交付物口径一致——解析算例）。
    ASSERT_TRUE(m.kind == tk::DatasetKind::AnalyticCase);

    // ---- 登记要求与 AT 锚点（契约 requirements/designRefs 的数据面）。
    EXPECT_FALSE(m.coveredRequirements.empty());
    EXPECT_NE(std::find(m.coveredRequirements.begin(), m.coveredRequirements.end(),
                        "DYN-04"),
              m.coveredRequirements.end())
        << "黄金数据集必须登记 DYN-04（任务契约 requirements 字段）";
    EXPECT_NE(std::find(m.coveredAt.begin(), m.coveredAt.end(), "AT-38"),
              m.coveredAt.end())
        << "黄金数据集必须登记 AT-38（R1 可验部分的数据面归属）";

    // ---- 档案引用（id/version 与消费测试 datasetRef→profileRef 链一致）。
    EXPECT_EQ(m.toleranceProfileId, std::string(kProfileId));
    EXPECT_EQ(m.toleranceProfileVersion, std::string(kProfileVersion));

    // ---- 独立性与方法声明（§14.1——期望值与产品实现零共享）。
    ASSERT_TRUE(m.referenceSourcePresent);
    EXPECT_TRUE(m.referenceSource.independentOfProductionImpl);
    EXPECT_EQ(m.referenceSource.method, "closed-form");

    // ---- 边角样例三布尔（附录 D C4——黄金数据集必含零值/近零/正负抵消）。
    ASSERT_TRUE(m.edgeCasesPresent);
    EXPECT_TRUE(m.edgeCases.zeroValue);
    EXPECT_TRUE(m.edgeCases.nearZero);
    EXPECT_TRUE(m.edgeCases.signCancellation);
    EXPECT_FALSE(m.edgeCases.sampleRefs.empty());

    // ---- 生成器与历史（期望值可复现＋首版登记）。
    ASSERT_TRUE(m.generatorPresent);
    EXPECT_TRUE(m.generatorCommitted);
    EXPECT_NE(m.generatorScript.find("generate/"), std::string::npos)
        << "生成脚本必须位于数据集 generate/ 目录";
    ASSERT_FALSE(m.history.empty());
    EXPECT_EQ(m.history.front().version, goldenRef().version);

    // ---- inputs/expected 至少各 1（§4.2.2 下限）。
    EXPECT_GE(m.inputs.size(), 1U);
    EXPECT_GE(m.expected.size(), 1U);
}

// =====================================================================
// 完整性契约：integrity 覆盖 inputs＋expected＋generate 全部文件
// =====================================================================

/**
 * §4.5 完整性登记契约：integrity 条目必须覆盖 manifest 声明的全部 inputs/
 * expected 文件与 generate 脚本（committed=true 时）——防"清单外文件漂移"；
 * 逐文件 SHA-256/size 校验已由 load 构造性执行（篡改即 DatasetInvalid），
 * 本用例断言**覆盖关系**（登记面的完备性——load 只校验已登记条目）。
 */
TEST(DtGoldenDatasetContract, IntegrityCoversAllFiles_WP18T04_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"CON-05"},
                  std::vector<std::string>{});

    const tk::GoldenDataset ds = tk::GoldenDataset::load(goldenRef());
    const tk::DatasetManifest& m = ds.manifest();

    const auto covered = [&](const std::string& rel) {
        return std::any_of(m.integrity.begin(), m.integrity.end(),
                           [&](const tk::IntegrityEntry& e) { return e.path == rel; });
    };
    for (const std::string& rel : m.inputs) {
        EXPECT_TRUE(covered(rel)) << "inputs 文件未登记完整性: " << rel;
    }
    for (const std::string& rel : m.expected) {
        EXPECT_TRUE(covered(rel)) << "expected 文件未登记完整性: " << rel;
    }
    if (m.generatorPresent && m.generatorCommitted) {
        EXPECT_TRUE(covered(m.generatorScript))
            << "committed 生成脚本未登记完整性: " << m.generatorScript;
    }
    // 数据集登记面收敛：integrity 至少 3 条（inputs＋expected＋generate）。
    EXPECT_GE(m.integrity.size(), 3U);
}

// =====================================================================
// 交叉一致契约：inputs/expected 算例集合与样本规模一一对应
// =====================================================================

/**
 * 数据面交叉一致（消费测试可依的前提）：inputs.cases 与 expected.cases
 * 的算例 id 集合相等且逐算例样本数一致（轴×样本布局——expected.samples
 * 外层长度＝模型电机轴数、内层长度＝inputs 样本数）；AT-38 反例声明
 * （inputs.at38R1Rejections）齐备且期望码为 DT-* 码形态。
 */
TEST(DtGoldenDatasetContract, InputsExpectedCrossConsistency_WP18T04_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-01"},
                  std::vector<std::string>{"AT-38"});

    const tk::GoldenDataset ds = tk::GoldenDataset::load(goldenRef());
    const tk::JsonValue inputs = tk::parseJson(readText(ds.resolveInput(
        ds.manifest().inputs.front())));
    const tk::JsonValue expected = tk::parseJson(readText(ds.resolveExpected(
        ds.manifest().expected.front())));
    ASSERT_TRUE(inputs.isObject());
    ASSERT_TRUE(expected.isObject());

    const tk::JsonValue* inCases = inputs.find("cases");
    const tk::JsonValue* expCases = expected.find("cases");
    ASSERT_NE(inCases, nullptr);
    ASSERT_NE(expCases, nullptr);
    ASSERT_EQ(inCases->items.size(), expCases->items.size())
        << "inputs/expected 算例数不一致";

    for (std::size_t i = 0; i < inCases->items.size(); ++i) {
        const tk::JsonValue& inCase = inCases->items[i];
        const tk::JsonValue& expCase = expCases->items[i];
        ASSERT_TRUE(inCase.find("id") != nullptr);
        ASSERT_TRUE(expCase.find("id") != nullptr);
        EXPECT_EQ(inCase.find("id")->text, expCase.find("id")->text)
            << "算例顺序或 id 不一致（index=" << i << "）";

        // 模型电机轴数（inputs.models 反查）——expected.samples 外层长度。
        const tk::JsonValue* modelName = inCase.find("model");
        const tk::JsonValue* models = inputs.find("models");
        ASSERT_NE(modelName, nullptr);
        ASSERT_NE(models, nullptr);
        const tk::JsonValue* model = models->find(modelName->text);
        ASSERT_NE(model, nullptr) << "算例引用未知模型: " << modelName->text;
        const std::size_t nAxes = model->find("ratios")->items.size();

        const tk::JsonValue* inSamples = inCase.find("samples");
        const tk::JsonValue* expSamples = expCase.find("samples");
        ASSERT_NE(inSamples, nullptr);
        ASSERT_NE(expSamples, nullptr);
        ASSERT_EQ(expSamples->items.size(), nAxes)
            << "expected.samples 外层长度须等于电机轴数（算例 "
            << inCase.find("id")->text << "）";
        for (const tk::JsonValue& axis : expSamples->items) {
            ASSERT_EQ(axis.items.size(), inSamples->items.size())
                << "expected 样本数与 inputs 不一致（算例 "
                << inCase.find("id")->text << "）";
        }
    }

    // ---- AT-38 反例声明齐备（两变体＋期望码形态 DT-*）。
    const tk::JsonValue* rejections = inputs.find("at38R1Rejections");
    ASSERT_NE(rejections, nullptr);
    ASSERT_EQ(rejections->items.size(), 2U)
        << "AT-38 R1 反例须两个变体（非对角＋耦合窗口）";
    for (const tk::JsonValue& rej : rejections->items) {
        const tk::JsonValue* code = rej.find("expectCode");
        ASSERT_NE(code, nullptr);
        EXPECT_EQ(code->text.rfind("DT-", 0), 0U)
            << "期望码须为 DT-* 稳定码形态: " << code->text;
    }
}

// =====================================================================
// 档案契约：dt-mapping 档案就绪＋fieldPath 覆盖＋P-DT-3 保守纪律
// =====================================================================

/**
 * 容差档案登记契约（acceptance 2 的档案面）：
 *   - 档案装载通过（schema ird-tolerance-profile/1——"只准更严"规则由
 *     装载构造性执行）；
 *   - fieldPath 覆盖消费面的具体路径（抽样 resolve 逐条命中——黄金数值
 *     断言经档案通道的前提）；
 *   - P-DT-3 保守纪律钉扎：全部条目 source=appendixD-fixed 且相对容差
 *     ≤1×10⁻⁹（附录 D 第 9 项）——档案只服务测试侧黄金对照，不承载任何
 *     运行时数值一致性校验阈值（无 analysis-config-default/
 *     engineering-policy-default 来源条目；运行侧保持精确判据）。
 */
TEST(DtGoldenDatasetContract, ToleranceProfileChannelAndPdt3_WP18T04_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-01"},
                  std::vector<std::string>{});

    // ---- 档案装载（manifest 引用链一致——路径由 GoldenFixture 同款规则
    // 解析：<root>/tolerance/<id>/v<version>.json）。
    const auto root = tk::goldenDataRoot();
    const auto profilePath = root / "tolerance" / kProfileId
                             / (std::string{"v"} + kProfileVersion + ".json");
    const tk::ToleranceProfile profile = tk::ToleranceProfile::load(profilePath);
    EXPECT_EQ(profile.profileId, std::string(kProfileId));
    EXPECT_EQ(profile.version, std::string(kProfileVersion));
    EXPECT_GE(profile.entries.size(), 20U)
        << "档案条目须覆盖消费面（逐样本 10 列＋统计/惯量面）";

    // ---- fieldPath 覆盖面（抽样 resolve——具体路径必须命中模板条目；
    // 未命中抛 TestKitError(ToleranceUndefined)＝失败）。
    std::vector<std::string> probes = {
        "cases[analytic-single].samples[0].theta",
        "cases[virtualwork-multi].samples[2][1].thetaDot",
        "cases[virtualwork-multi].matrixForm.thetaDot[*]",
        "cases[virtualwork-multi].matrixForm.tauIdeal[*]",
        "cases[energy-bidirectional].samples[0][3].pTransmission",
        "cases[dwell-zero].points[0].tauRms",
        "cases[dwell-zero].points[0].zeroDwell.timeShare",
        "cases[inertia-golden].points[0].reflectedInertia",
        "inertiaCases[0].axes[0].jReflected",
        "inertiaCases[0].axes[1].inertiaRatio",
    };
    for (const std::string& p : probes) {
        EXPECT_NO_THROW((void)profile.resolve(p))
            << "fieldPath 未命中容差条目: " << p;
    }

    // ---- P-DT-3 保守纪律（档案通道＝测试侧对照；不引入运行时阈值）。
    for (const tk::ToleranceEntry& e : profile.entries) {
        EXPECT_EQ(e.source, tk::ToleranceSource::AppendixDFixed)
            << "条目 " << e.fieldPath
            << " 来源须为 appendixD-fixed（黄金对照容差——不引入配置/策略"
               "通道的运行时阈值，P-DT-3）";
        EXPECT_LE(e.tolerance.relative, 1e-9)
            << "条目 " << e.fieldPath << " 相对容差超出附录 D 第 9 项上限";
        // 绝对分量逐例声明（§14.1——黄金算例可自带更严值并声明）。
        EXPECT_GE(e.tolerance.absolute, 0.0);
    }
}

// =====================================================================
// 数据根契约：消费链定位一致（golden/ 与 tolerance/ 同根）
// =====================================================================

/**
 * 消费链定位契约：dt-mapping-golden 数据集与 dt-mapping 档案位于同一数据
 * 根（TestPaths 唯一入口——任何单元不得自行拼装数据根路径，testkit §4.6）；
 * 本用例钉扎"黄金数据目录布局"（golden/<id>/<version>/manifest.json）的
 * drivetrain 侧落位一致性。
 */
TEST(DtGoldenDatasetContract, DataRootLayout_WP18T04_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-01"},
                  std::vector<std::string>{});

    const auto root = tk::goldenDataRoot();
    const auto manifestPath = root / "golden" / goldenRef().datasetId
                              / goldenRef().version / "manifest.json";
    EXPECT_TRUE(std::filesystem::exists(manifestPath))
        << "manifest 不在数据根布局位置: " << manifestPath.string();
    // 数据根下单元子目录（golden/tolerance）同根并存——消费链两通道可达。
    EXPECT_TRUE(std::filesystem::exists(root / "golden" / goldenRef().datasetId));
    EXPECT_TRUE(std::filesystem::exists(root / "tolerance" / kProfileId));
}
