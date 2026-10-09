/**
 * @file   SelGoldenDatasetContractTest.cpp
 * @brief  selection 黄金数据集的登记契约用例组（SelGoldenDatasetContract）
 *         ——WP-19-T11 acceptance 2"testdata/golden/sel-* 按 DatasetManifest
 *         登记"的契约自证面：四个数据集（sel-catalog/sel-screening/
 *         sel-combo/sel-backfill）经 GoldenDataset::load 全量校验（schema→
 *         完整性→交叉校验）通过、manifest 字段契约（登记要求/AT 锚点/独立
 *         性声明/边角三布尔/生成器入库）、integrity 覆盖关系、容差档案通
 *         道就绪与保守纪律（不引入运行时阈值语义）钉扎。
 *
 * 设计依据：
 *   - units/testkit.md §4.2.2（DatasetManifest 字段表——登记契约的权威词表）、
 *     §4.5（size＋SHA-256 完整性）、§4.3（容差档案 schema 与"只准更严"规则）、
 *     §5.1（装载语义——本组用例即其消费面）
 *   - units/selection.md §15.1（黄金数据集 testdata/golden/sel-* 四类样例）、
 *     §18（AT-08/AT-30 验证位置＝选型黄金数据）、§15.1"同输入同线程同种子
 *     结果稳定"
 *   - 需求 SEL-03/SEL-05/SEL-06/SEL-10（任务契约 requirements）、NFR-COR-01、
 *     AT-08/AT-30
 *   - 任务契约 tasks/foundation/WP-19-T11.json acceptance 2/3
 *
 * 测试策略（drivetrain GoldenDatasetContractTest 同款形态）：本组用例针对
 * **数据资产**的登记契约（区别于 SelGoldenDatasetTest 的数值消费面）——
 * 装载/校验走 testkit 真实实现（GoldenDataset/ToleranceProfile 不桩化），
 * 断言 manifest 与档案的字段落值；数据缺陷按 testkit §7.2 分类暴露
 * （DatasetInvalid——不伪装算法回归）。
 *
 * 线程约束：gtest 用例天然串行。
 */

#include <sdurws/ird/testkit/Dataset.hpp>
#include <sdurws/ird/testkit/TestPaths.hpp>
#include <sdurws/ird/testkit/ToleranceProfile.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace tk = sdurws::ird::testkit;

namespace {

/// 四数据集引用（被测契约对象——WP-19-T11 登记的 sel-* 全集）。
struct GoldenRef {
    const char* datasetId;
    const char* version;
    bool analytic;      ///< kind=analytic-case（独立性/边角三布尔强制面）
    const char* at;     ///< 契约锚点 AT（catalog/screening/combo＝AT-08、
                        ///  backfill＝AT-30）
};

const GoldenRef* goldenRefs()
{
    static const GoldenRef refs[] = {
        {"sel-catalog-golden", "1.0.0", false, "AT-08"},
        {"sel-screening-golden", "1.0.0", true, "AT-08"},
        {"sel-combo-golden", "1.0.0", true, "AT-08"},
        {"sel-backfill-golden", "1.0.0", true, "AT-30"},
    };
    return refs;
}

constexpr const char* kProfileId = "sel-golden";
constexpr const char* kProfileVersion = "1.0.0";

}  // namespace

// =====================================================================
// 登记契约：四数据集 GoldenDataset::load 全量校验通过＋manifest 字段落值
// =====================================================================

/**
 * DatasetManifest 登记契约（acceptance 2 的机器断言面，逐数据集全检）：
 *   - 装载本身即全量校验（schema→integrity SHA-256/size→交叉校验）——
 *     load 返回即"登记完整且未被篡改"的构造性证明；
 *   - 登记要求覆盖（SEL-* 族）与 AT 锚点（AT-08/AT-30——任务契约
 *     requirements/acceptance 的数据面归属）；
 *   - 档案引用统一指向 sel-golden@1.0.0；
 *   - analytic-case 三套件的独立性声明＋边角三布尔＋生成器入库；
 *   - 首版 history 登记。
 */
TEST(SelGoldenDatasetContract, ManifestsRegistered_WP19T11_ACC2)
{
    IRD_TEST_INFO((std::vector<std::string>{"SEL-03", "SEL-05", "SEL-06", "SEL-10",
                                            "NFR-COR-01"}),
                  (std::vector<std::string>{"AT-08", "AT-30"}));

    for (std::size_t gi = 0; gi < 4; ++gi) {
        const GoldenRef& ref = goldenRefs()[gi];
        SCOPED_TRACE(ref.datasetId);
        // ---- 全量装载（任何校验失败抛 TestKitError——数据资产缺陷即失败）。
        const tk::GoldenDataset ds = tk::GoldenDataset::load({ref.datasetId, ref.version});
        const tk::DatasetManifest& m = ds.manifest();

        EXPECT_EQ(m.schemaVersion, "ird-golden-manifest/1");
        EXPECT_EQ(m.datasetId, ref.datasetId);
        EXPECT_EQ(m.version, ref.version);
        EXPECT_EQ(m.kind == tk::DatasetKind::AnalyticCase, ref.analytic)
            << "kind 与登记面不符";

        // ---- 登记要求与 AT 锚点（SEL-* 族＋AT-08/AT-30）。
        EXPECT_FALSE(m.coveredRequirements.empty());
        EXPECT_TRUE(std::any_of(m.coveredRequirements.begin(),
                                m.coveredRequirements.end(),
                                [](const std::string& r) {
                                    return r.rfind("SEL-", 0) == 0 || r.rfind("MDL-", 0) == 0
                                        || r.rfind("DYN-", 0) == 0 || r.rfind("NFR-", 0) == 0;
                                }))
            << "登记要求须为 SEL-*/MDL-*/DYN-*/NFR-* 族";
        EXPECT_NE(std::find(m.coveredAt.begin(), m.coveredAt.end(), ref.at),
                  m.coveredAt.end())
            << "黄金数据集必须登记 " << ref.at;

        // ---- 档案引用（统一 sel-golden@1.0.0——四数据集共享一条档案）。
        EXPECT_EQ(m.toleranceProfileId, std::string(kProfileId));
        EXPECT_EQ(m.toleranceProfileVersion, std::string(kProfileVersion));

        if (ref.analytic) {
            // ---- 独立性声明（NFR-COR-01——期望值与产品实现零共享）。
            ASSERT_TRUE(m.referenceSourcePresent);
            EXPECT_TRUE(m.referenceSource.independentOfProductionImpl);
            // ---- 边角样例三布尔（附录 D C4——零值/近零/正负抵消）。
            ASSERT_TRUE(m.edgeCasesPresent);
            EXPECT_TRUE(m.edgeCases.zeroValue);
            EXPECT_TRUE(m.edgeCases.nearZero);
            EXPECT_TRUE(m.edgeCases.signCancellation);
            EXPECT_FALSE(m.edgeCases.sampleRefs.empty());
        }

        // ---- 生成器入库＋首版 history（期望可复现）。
        ASSERT_TRUE(m.generatorPresent);
        EXPECT_TRUE(m.generatorCommitted);
        EXPECT_NE(m.generatorScript.find("generate/"), std::string::npos);
        ASSERT_FALSE(m.history.empty());
        EXPECT_EQ(m.history.front().version, ref.version);
        // ---- inputs/expected 至少各 1（§4.2.2 下限）。
        EXPECT_GE(m.inputs.size(), 1U);
        EXPECT_GE(m.expected.size(), 1U);
    }
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
TEST(SelGoldenDatasetContract, IntegrityCoversAllFiles_WP19T11_ACC2)
{
    IRD_TEST_INFO((std::vector<std::string>{"CON-05"}),
                  std::vector<std::string>{});

    for (std::size_t gi = 0; gi < 4; ++gi) {
        const GoldenRef& ref = goldenRefs()[gi];
        SCOPED_TRACE(ref.datasetId);
        const tk::GoldenDataset ds = tk::GoldenDataset::load({ref.datasetId, ref.version});
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
        EXPECT_GE(m.integrity.size(), 3U);
    }
}

// =====================================================================
// 档案契约：sel-golden 档案就绪＋fieldPath 覆盖＋保守纪律
// =====================================================================

/**
 * 容差档案登记契约（acceptance 2 的档案面）：
 *   - 档案装载通过（schema ird-tolerance-profile/1——"只准更严"规则由装载
 *     构造性执行）；
 *   - fieldPath 覆盖消费面（组合质量核算＋回填合成 12 条目——抽样 resolve
 *     逐条命中，黄金数值断言经档案通道的前提）；
 *   - 保守纪律钉扎：全部条目 source=appendixD-fixed 且相对容差≤1×10⁻⁹——
 *     档案只服务测试侧黄金对照，不承载任何运行时数值一致性校验阈值（运行
 *     侧保持精确判据——附录 D C7）。
 */
TEST(SelGoldenDatasetContract, ToleranceProfileChannelAndConservatism_WP19T11_ACC2)
{
    IRD_TEST_INFO((std::vector<std::string>{"NFR-COR-01"}),
                  std::vector<std::string>{});

    const auto root = tk::goldenDataRoot();
    const auto profilePath = root / "tolerance" / kProfileId
                             / (std::string{"v"} + kProfileVersion + ".json");
    const tk::ToleranceProfile profile = tk::ToleranceProfile::load(profilePath);
    EXPECT_EQ(profile.profileId, std::string(kProfileId));
    EXPECT_EQ(profile.version, std::string(kProfileVersion));
    EXPECT_GE(profile.entries.size(), 12U)
        << "档案条目须覆盖消费面（组合质量＋回填合成 12 通道）";

    // ---- fieldPath 覆盖面（抽样 resolve——具体路径必须命中模板条目）。
    const std::vector<std::string> probes = {
        "combos[0].totalMass",
        "backfill.axes[0].synthesis.massKg",
        "backfill.axes[0].synthesis.comM.x",
        "backfill.axes[0].synthesis.comM.z",
        "backfill.axes[0].synthesis.inertia.ixx",
        "backfill.axes[0].synthesis.inertia.iyz",
        "backfill.axes[0].synthesis.rotorInertiaKgM2",
    };
    for (const std::string& p : probes) {
        EXPECT_NO_THROW((void)profile.resolve(p))
            << "fieldPath 未命中容差条目: " << p;
    }

    // ---- 保守纪律（档案通道＝测试侧对照；不引入运行时阈值）。
    for (const tk::ToleranceEntry& e : profile.entries) {
        EXPECT_EQ(e.source, tk::ToleranceSource::AppendixDFixed)
            << "条目 " << e.fieldPath
            << " 来源须为 appendixD-fixed（黄金对照容差——不引入配置/策略通道"
               "的运行时阈值）";
        EXPECT_LE(e.tolerance.relative, 1e-9)
            << "条目 " << e.fieldPath << " 相对容差超出附录 D 第 9 项上限";
        EXPECT_GE(e.tolerance.absolute, 0.0);
    }
}

// =====================================================================
// 数据根契约：消费链定位一致（golden/ 与 tolerance/ 同根）
// =====================================================================

/**
 * 消费链定位契约：sel-* 四数据集与 sel-golden 档案位于同一数据根
 * （TestPaths 唯一入口——任何单元不得自行拼装数据根路径，testkit §4.6）；
 * 本用例钉扎"黄金数据目录布局"（golden/<id>/<version>/manifest.json）的
 * selection 侧落位一致性。
 */
TEST(SelGoldenDatasetContract, DataRootLayout_WP19T11_ACC2)
{
    IRD_TEST_INFO((std::vector<std::string>{"NFR-COR-01"}),
                  std::vector<std::string>{});

    const auto root = tk::goldenDataRoot();
    for (std::size_t gi = 0; gi < 4; ++gi) {
        const GoldenRef& ref = goldenRefs()[gi];
        const auto manifestPath = root / "golden" / ref.datasetId / ref.version
                                  / "manifest.json";
        EXPECT_TRUE(std::filesystem::exists(manifestPath))
            << "manifest 不在数据根布局位置: " << manifestPath.string();
    }
    EXPECT_TRUE(std::filesystem::exists(root / "tolerance" / kProfileId));
}
