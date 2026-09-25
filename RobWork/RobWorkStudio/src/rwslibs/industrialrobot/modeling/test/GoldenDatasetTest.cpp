/**
 * @file   GoldenDatasetTest.cpp
 * @brief  modeling 黄金数据集与容差档案的装载校验用例组（MdlGoldenDatasets）
 *         ——契约 tasks/foundation/WP-13-T16.json acceptance 2 的具名自证：
 *
 *   四个黄金数据集（mdl-template-6r／mdl-dh-equivalence／mdl-not-expressible
 *   ／mdl-urdf-import）经 testkit GoldenDataset::load 全链装载（解析→schema
 *   →integrity SHA-256＋size→交叉校验——testkit.md §4.2.2/§4.5，TK-T03），
 *   任一失败即"数据集非法级"显性失败（损坏数据不得伪装成算法回归）；
 *   容差档案 mdl-dh（testkit.md §4.3，TK-T04）装载并逐条核对"固定类不放宽"
 *   （tolerance==allowedMax——附录 D 第 4/5/6/7 项锚定）；套件消费的全部
 *   fieldPath 前置可解析（C4"报错不默认"——消费点 ToleranceUndefined 前移
 *   到 CI）；coveredRequirements 逐项在需求 ID 字典内（TK-MAN lint 语义）。
 *
 * 设计依据：units/modeling.md §10.1（黄金数据集四清单＋容差档案行）、
 * units/testkit.md §4.2/§4.3/§4.5/§4.6；需求 NFR-COR-01（黄金数据集为
 * 独立正确性依据）、NFR-COR-03（不静默通过）。
 *
 * 两模式均编译（runtime GoldenDataLintTest 同款口径）：本文件只消费
 * testkit（core＋标准库）与 testdata 资产，不触达 rw/rwsim 与 policy——
 * 数据资产校验要求集成/冒烟两模式口径一致。
 */

#include <sdurws/ird/testkit/Dataset.hpp>
#include <sdurws/ird/testkit/JsonLite.hpp>
#include <sdurws/ird/testkit/TestPaths.hpp>
#include <sdurws/ird/testkit/ToleranceProfile.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

namespace tk = sdurws::ird::testkit;

/// 读取文件全文（读失败显性失败——数据资产损坏不得静默）。
std::string readFile(const std::filesystem::path& p)
{
    std::ifstream in(p, std::ios::binary);
    EXPECT_TRUE(static_cast<bool>(in)) << "无法读取: " << p.string();
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

/// 装载并断言数据集完整校验通过（GoldenDataset::load 全链：解析→schema→
/// 完整性〔SHA-256＋size〕→交叉校验——任一失败抛 TestKitError〔数据集
/// 非法级〕，本用例判失败）。
tk::GoldenDataset expectDatasetLoads(const char* id, const char* version)
{
    tk::GoldenDataset ds;
    EXPECT_NO_THROW(ds = tk::GoldenDataset::load({id, version}))
        << "数据集装载失败（数据资产缺陷——§7.2 dataset-invalid）";
    return ds;
}

/// 装载容差档案 mdl-dh（testdata/tolerance/mdl-dh/v1.0.0.json——目录名
/// ＝profileId；附录 D 第 4/5/6/7 项锚定）。
tk::ToleranceProfile loadMdlDhProfile()
{
    return tk::ToleranceProfile::load(tk::toleranceProfileDir("mdl-dh")
                                      / "v1.0.0.json");
}

// =====================================================================
// ACC2：容差档案 mdl-dh（TK-T04 消费面——固定类不放宽）
// =====================================================================

/**
 * @brief 档案装载全链＋"固定类不放宽"逐条核对：全部条目 source=
 *        appendixD-fixed（附录 D 第 4/5/6/7 项均为固定类）且 tolerance 与
 *        allowedMax 同值——档案层不存在任何放宽通道（§4.3.2 固定类语义）；
 *        第 7 项（SPD 口径）以零容差条目承载"特征值严格>0 不设数值余量"。
 */
TEST(MdlGoldenDatasets, ToleranceProfileMdlDhFixedNotRelaxed_WP13T16_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-01"},
                  std::vector<std::string>{"AT-16"});

    const tk::ToleranceProfile profile = loadMdlDhProfile();
    EXPECT_EQ(profile.profileId, "mdl-dh");
    EXPECT_EQ(profile.version, "1.0.0");
    EXPECT_NE(profile.basis.find("appendixD#4"), std::string::npos)
        << "basis 须锚定附录 D 项号（第 4 项——FK/编译链等价）";
    EXPECT_NE(profile.basis.find("appendixD#5"), std::string::npos)
        << "basis 须锚定附录 D 项号（第 5 项——DH 判定逐关节逐项上界）";
    EXPECT_NE(profile.basis.find("appendixD#6"), std::string::npos)
        << "basis 须锚定附录 D 项号（第 6 项——惯量对称性）";
    EXPECT_NE(profile.basis.find("appendixD#7"), std::string::npos)
        << "basis 须锚定附录 D 项号（第 7 项——SPD 判定口径）";
    ASSERT_FALSE(profile.entries.empty());

    // 卡 §10.1"固定类不放宽"的档案面执行：全部条目固定类且 tolerance==
    // allowedMax（数据集只准更严——本档案取等号即附录 D 原值）。
    for (const tk::ToleranceEntry& e : profile.entries) {
        EXPECT_EQ(e.source, tk::ToleranceSource::AppendixDFixed)
            << "条目 " << e.fieldPath << " 非固定类（档案口径违例）";
        ASSERT_TRUE(e.allowedMax.has_value())
            << "固定类条目 " << e.fieldPath << " 缺 allowedMax";
        EXPECT_DOUBLE_EQ(e.tolerance.relative, e.allowedMax->relative)
            << "条目 " << e.fieldPath << " 相对分量被放宽";
        EXPECT_DOUBLE_EQ(e.tolerance.absolute, e.allowedMax->absolute)
            << "条目 " << e.fieldPath << " 绝对分量被放宽";
    }
    // 第 7 项（SPD——严格 >0 零余量）：零容差条目存在且分量全零。
    const tk::ToleranceEntry& spd = profile.resolve("inertia.spd-eigenvalue");
    EXPECT_DOUBLE_EQ(spd.tolerance.relative, 0.0);
    EXPECT_DOUBLE_EQ(spd.tolerance.absolute, 0.0);
}

/**
 * @brief 套件消费的全部具体 fieldPath 前置可解析（C4"报错不默认"——
 *        路径未登记即在消费点 ToleranceUndefined；本用例前移到 CI 显性
 *        暴露）。'*' 模板按解析规则展开核对（dh[0]/dh[2]/dh-solve[5] 等）。
 */
TEST(MdlGoldenDatasets, ToleranceProfileResolvesAllSuitePaths_WP13T16_ACC2)
{
    const tk::ToleranceProfile profile = loadMdlDhProfile();

    // 消费面＝GoldenDhEquivalenceTest（展开/roundtrip/FK）与
    // GoldenImportTest（惯量对称性数据质量门）的全部 fieldPath。
    const std::vector<std::string> concretePaths = {
        "dh[0].origin.x", "dh[0].origin.y", "dh[0].origin.z",
        "dh[2].origin.x", "dh[2].origin.y", "dh[2].origin.z",
        "dh[0].axis.x", "dh[1].axis.y", "dh[2].axis.z",
        "dh-solve[0].thetaOffset", "dh-solve[1].d",
        "dh-solve[2].a", "dh-solve[3].alpha",
        "fk.max-position-deviation", "fk.max-orientation-deviation",
        "inertia.symmetry", "inertia.spd-eigenvalue",
    };
    for (const std::string& p : concretePaths) {
        EXPECT_NO_THROW((void)profile.resolve(p)) << "档案 mdl-dh 缺条目: " << p;
    }
}

// =====================================================================
// ACC2：mdl-template-6r（六轴模板参数锁定——D-MDL-7/AT-20 基线）
// =====================================================================

/**
 * @brief 数据集 1/4：mdl-template-6r 装载（manifest＋integrity＋交叉校验）
 *        ＋契约面观测（contract-fixture 类、T-MDL-1 锁定范围、inputs/
 *        expected 白名单可解析）。
 */
TEST(MdlGoldenDatasets, MdlTemplate6rLoadsAndPassesManifest_WP13T16_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-01", "MDL-22"},
                  std::vector<std::string>{"AT-01", "AT-20"});

    const tk::GoldenDataset ds = expectDatasetLoads("mdl-template-6r", "1.0.0");
    EXPECT_EQ(ds.manifest().datasetId, "mdl-template-6r");
    EXPECT_EQ(ds.manifest().kind, tk::DatasetKind::ContractFixture);
    EXPECT_EQ(ds.manifest().toleranceProfileId, "mdl-dh");
    EXPECT_EQ(ds.manifest().toleranceProfileVersion, "1.0.0");
    // AT 承载面：AT-01（V-01 模板创建）/AT-20（模板清单基线）。
    const std::vector<std::string>& ats = ds.manifest().coveredAt;
    EXPECT_NE(std::find(ats.begin(), ats.end(), "AT-01"), ats.end());
    EXPECT_NE(std::find(ats.begin(), ats.end(), "AT-20"), ats.end());
    // inputs/expected 白名单可解析（装载器交叉校验的复述钉住）。
    EXPECT_NO_THROW((void)ds.resolveInput("inputs/template-request.json"));
    EXPECT_NO_THROW((void)ds.resolveExpected("expected/six-axis-defaults.json"));
}

// =====================================================================
// ACC2/ACC4：其余数据集装载（随 WP-13-T16 数据集落位递增断言——文件内
// 不做条件跳过：数据集未落位时本组用例必须失败，防止"半落位"静默通过）
// =====================================================================

/**
 * @brief 数据集 2/4：mdl-dh-equivalence（DH/显式等价——AT-16；analytic-case
 *        类——独立正确性依据）。analytic-case 的独立性（referenceSource.
 *        independentOfProductionImpl）与 C4 边角样例（零值/近零/正负抵消
 *        三布尔全 true——附录 D C4 lint 强制）在此观测防漂移。
 */
TEST(MdlGoldenDatasets, MdlDhEquivalenceLoadsAndPassesManifest_WP13T16_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-02", "MDL-10"},
                  std::vector<std::string>{"AT-16"});

    const tk::GoldenDataset ds = expectDatasetLoads("mdl-dh-equivalence", "1.0.0");
    EXPECT_EQ(ds.manifest().datasetId, "mdl-dh-equivalence");
    EXPECT_EQ(ds.manifest().kind, tk::DatasetKind::AnalyticCase);
    EXPECT_EQ(ds.manifest().toleranceProfileId, "mdl-dh");
    // analytic-case 的独立性约束（§4.2.2 装载器已校验——此处观测值面）。
    ASSERT_TRUE(ds.manifest().referenceSourcePresent);
    EXPECT_TRUE(ds.manifest().referenceSource.independentOfProductionImpl)
        << "analytic-case 必须独立于生产实现（§4.2.2）";
    // C4 边角样例三布尔全 true＋sampleRefs 指向 expected 内条目（ACC4
    // 数据集面——消费面在 GoldenDhEquivalenceTest）。
    ASSERT_TRUE(ds.manifest().edgeCasesPresent);
    EXPECT_TRUE(ds.manifest().edgeCases.zeroValue);
    EXPECT_TRUE(ds.manifest().edgeCases.nearZero);
    EXPECT_TRUE(ds.manifest().edgeCases.signCancellation);
    EXPECT_FALSE(ds.manifest().edgeCases.sampleRefs.empty());
    for (const std::string& ref : ds.manifest().edgeCases.sampleRefs) {
        // sampleRefs 形如 "expected/<file>#<锚>"——文件半段可解析。
        const std::string file = ref.substr(0, ref.find('#'));
        EXPECT_NO_THROW((void)ds.resolveExpected(file))
            << "edgeCases.sampleRefs 指向不可解析条目: " << ref;
    }
}

/**
 * @brief 数据集 3/4：mdl-not-expressible（不可表达终判/非权威样本——
 *        AT-16；contract-fixture 类）。五状态覆盖面的另一半：NotExpressible
 *        终判不进入求解、Approximate 不得成为权威、AnalysisFailed 数值失败
 *        轴（V-11 行"五状态各含样例"）。
 */
TEST(MdlGoldenDatasets, MdlNotExpressibleLoadsAndPassesManifest_WP13T16_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-10"},
                  std::vector<std::string>{"AT-16"});

    const tk::GoldenDataset ds = expectDatasetLoads("mdl-not-expressible", "1.0.0");
    EXPECT_EQ(ds.manifest().datasetId, "mdl-not-expressible");
    EXPECT_EQ(ds.manifest().kind, tk::DatasetKind::ContractFixture);
    EXPECT_EQ(ds.manifest().toleranceProfileId, "mdl-dh");
    EXPECT_NO_THROW((void)ds.resolveInput("inputs/samples.json"));
    EXPECT_NO_THROW((void)ds.resolveExpected("expected/samples-expected.json"));
}

/**
 * @brief 数据集 4/4：mdl-urdf-import（导入映射——AT-15/17；contract-fixture
 *        类）。inputs 携带 URDF＋网格资源＋Xacro 黄金样例（V-09 循环面——
 *        AT-31），expected 携带导入报告逐项期望（V-06 计数消费面）。
 */
TEST(MdlGoldenDatasets, MdlUrdfImportLoadsAndPassesManifest_WP13T16_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-03", "MDL-11", "MDL-12"},
                  std::vector<std::string>{"AT-15", "AT-17"});

    const tk::GoldenDataset ds = expectDatasetLoads("mdl-urdf-import", "1.0.0");
    EXPECT_EQ(ds.manifest().datasetId, "mdl-urdf-import");
    EXPECT_EQ(ds.manifest().kind, tk::DatasetKind::ContractFixture);
    EXPECT_EQ(ds.manifest().toleranceProfileId, "mdl-dh");
    const std::vector<std::string>& ats = ds.manifest().coveredAt;
    EXPECT_NE(std::find(ats.begin(), ats.end(), "AT-15"), ats.end());
    EXPECT_NE(std::find(ats.begin(), ats.end(), "AT-17"), ats.end());
    // inputs 白名单逐项可解析（URDF 主样例＋Xacro 黄金样例对——AT-31 面）。
    EXPECT_NO_THROW((void)ds.resolveInput("inputs/golden-6r.urdf"));
    EXPECT_NO_THROW((void)ds.resolveInput("inputs/xacro/golden-macro.xacro"));
    EXPECT_NO_THROW((void)ds.resolveInput("inputs/xacro/cycle-a.xacro"));
    EXPECT_NO_THROW((void)ds.resolveExpected("expected/import-report.json"));
}

/**
 * @brief coveredRequirements 对需求 ID 字典逐项校验（TK-MAN lint 语义——
 *        与独立工具 sdurws_ird_testdata_lint 同路径；字典＝testdata/
 *        requirements-ids.json，四个 modeling 数据集全量核对）。
 */
TEST(MdlGoldenDatasets, CoveredRequirementsInDictionary_WP13T16_ACC2)
{
    const std::filesystem::path dictPath
        = tk::goldenDataRoot() / "requirements-ids.json";
    const tk::JsonValue root = tk::parseJson(readFile(dictPath));
    const tk::JsonValue* ids = root.find("ids");
    ASSERT_NE(ids, nullptr) << "字典缺 ids 数组";
    std::set<std::string> dict;
    for (const tk::JsonValue& id : ids->items) {
        dict.insert(id.text);
    }
    ASSERT_FALSE(dict.empty()) << "需求 ID 字典为空（路径配错防线）";

    for (const auto* dsId : {"mdl-template-6r", "mdl-dh-equivalence",
                             "mdl-not-expressible", "mdl-urdf-import"}) {
        const tk::GoldenDataset ds = expectDatasetLoads(dsId, "1.0.0");
        for (const std::string& req : ds.manifest().coveredRequirements) {
            EXPECT_NE(dict.find(req), dict.end())
                << dsId << ": coveredRequirements ID 不在字典: " << req;
        }
    }
}

}  // namespace
