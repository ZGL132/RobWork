/**
 * @file   GoldenDataLintTest.cpp
 * @brief  RT-T12 数据集 lint 用例——runtime 交付的黄金数据集与容差档案
 *         经 testkit 装载/校验全链（§4.5 资源完整性＋§4.2.2 字典校验），
 *         随 sdurws_ird_runtime_test 在集成/冒烟两模式运行（"lint 通过并
 *         随 CI 跑通"的用例面；独立工具形态见 testkit 的
 *         sdurws_ird_testdata_lint，同一装载路径）。
 *
 * 设计依据：
 *   - units/runtime.md §12 RT-T12 行（"数据集与容差档案 rt——lint 通过"）、
 *     §11 RT-EQ-1/RT-NM-1（数据集消费面——本用例钉住其"可装载"前置）
 *   - units/testkit.md §4.2.2（manifest 字段表——装载器全量校验）、§4.5
 *     （size＋SHA-256 完整性）、§4.3.1（档案校验）、§10.2（runtime 从
 *     testkit 接收数据集 schema/装载——T-2 允许的测试侧依赖方向）
 *   - 需求 NFR-COR-01（黄金数据集为独立正确性依据——损坏数据不得伪装成
 *     算法回归：装载失败＝数据集非法级，显性失败）
 *
 * 两模式说明：本文件只消费 testkit（core＋标准库）与 testdata 资产，不
 * 触碰 rw/rwsim——集成/冒烟两模式均编译进测试目标（与 ContractSuiteTest
 * 的集成专用分工相反：数据资产校验恰恰要求两模式口径一致）。
 */

#include <sdurws/ird/testkit/Dataset.hpp>
#include <sdurws/ird/testkit/JsonLite.hpp>
#include <sdurws/ird/testkit/TestPaths.hpp>
#include <sdurws/ird/testkit/ToleranceProfile.hpp>

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

/// 装载并断言数据集完整校验通过（ GoldenDataset::load 全链：解析→schema→
/// 完整性〔SHA-256＋size〕→交叉校验——任一失败抛 TestKitError〔数据集
/// 非法级〕，本用例判失败）。
tk::GoldenDataset expectDatasetLoads(const char* id, const char* version)
{
    tk::GoldenDataset ds;
    EXPECT_NO_THROW(ds = tk::GoldenDataset::load({id, version}))
        << "数据集装载失败（数据资产缺陷——§7.2 dataset-invalid）";
    return ds;
}

/**
 * RT-T12 数据集 1/2：rt-fk-equation（analytic-case——附录 D 第 4 项
 * FK/编译链等价验证的独立正确性依据；AT-16 载体）。
 */
TEST(GoldenDataLint, RtgFkEquationLoadsAndPassesManifest_RT_T12_ACC3)
{
    const tk::GoldenDataset ds = expectDatasetLoads("rt-fk-equation", "1.0.0");
    EXPECT_EQ(ds.manifest().datasetId, "rt-fk-equation");
    EXPECT_EQ(ds.manifest().kind, tk::DatasetKind::AnalyticCase);
    EXPECT_EQ(ds.manifest().toleranceProfileId, "rt-runtime");
    EXPECT_EQ(ds.manifest().toleranceProfileVersion, "1.0.0");
    // analytic-case 的独立性/边界样例约束（§4.2.2 装载器已校验，此处
    // 观测值面防"装载通过但内容漂移"）。
    ASSERT_TRUE(ds.manifest().referenceSourcePresent);
    EXPECT_TRUE(ds.manifest().referenceSource.independentOfProductionImpl)
        << "analytic-case 必须独立于生产实现（§4.2.2）";
    ASSERT_TRUE(ds.manifest().edgeCasesPresent);
    EXPECT_TRUE(ds.manifest().edgeCases.zeroValue && ds.manifest().edgeCases.nearZero
                && ds.manifest().edgeCases.signCancellation)
        << "附录 D C4：黄金算例必含零值/近零/正负抵消样例";
    // inputs/expected 白名单可解析（装载器交叉校验的复述钉住）。
    EXPECT_NO_THROW((void)ds.resolveInput("inputs/model.json"));
    EXPECT_NO_THROW((void)ds.resolveExpected("expected/fk.json"));
}

/**
 * RT-T12 数据集 2/2：rt-namemap-roundtrip（contract-fixture——§7.4
 * AT-18 双向往返的契约夹具）。
 */
TEST(GoldenDataLint, RtNamemapRoundtripLoadsAndPassesManifest_RT_T12_ACC3)
{
    const tk::GoldenDataset ds = expectDatasetLoads("rt-namemap-roundtrip", "1.0.0");
    EXPECT_EQ(ds.manifest().datasetId, "rt-namemap-roundtrip");
    EXPECT_EQ(ds.manifest().kind, tk::DatasetKind::ContractFixture);
    EXPECT_EQ(ds.manifest().toleranceProfileId, "rt-runtime");
    EXPECT_NO_THROW((void)ds.resolveInput("inputs/robot.json"));
    EXPECT_NO_THROW((void)ds.resolveExpected("expected/namemap.json"));
}

/**
 * 容差档案 rt（testdata/tolerance/rt-runtime/v1.0.0.json——目录名＝
 * profileId"rt-runtime"，"rt"只是单元卡 §11 行的助记名）：装载全链（schema/
 * 句法/单位/allowedMax 条件规则）＋套件使用的 fieldPath 全部可解析
 * （附录 D C4"报错不默认"——路径未登记即在消费点 ToleranceUndefined，
 * 本用例前移到 CI 显性暴露）。
 */
TEST(GoldenDataLint, ToleranceProfileRtLoadsAndResolvesAllSuitePaths_RT_T12_ACC3)
{
    const tk::ToleranceProfile profile
        = tk::ToleranceProfile::load(tk::toleranceProfileDir("rt-runtime") / "v1.0.0.json");
    EXPECT_EQ(profile.profileId, "rt-runtime");
    EXPECT_EQ(profile.version, "1.0.0");
    EXPECT_FALSE(profile.basis.empty()) << "档案须带依据声明（附录 D 项号）";

    // 契约套件与数据集消费的全部 fieldPath（ContractSuiteTest 的
    // IRD_EXPECT_CLOSE 调用面；'*' 模板按解析规则展开核对）。
    const std::vector<std::string> concretePaths = {
        "fk[0].origin.x", "fk[3].origin.z", "fk[0].axis.x", "fk[3].axis.z",
        "base-world.point.x", "base-world.point.y", "base-world.point.z",
        "base-world.inverse.x", "base-world.inverse.y", "base-world.inverse.z",
        "base-world.gravity.x", "base-world.gravity.y", "base-world.gravity.z",
    };
    for (const std::string& p : concretePaths) {
        EXPECT_NO_THROW((void)profile.resolve(p)) << "档案 rt 缺条目: " << p;
    }
}

/**
 * coveredRequirements 对需求 ID 字典逐项校验（TK-MAN 的 lint 语义——
 * 与独立工具 sdurws_ird_testdata_lint 同路径；本用例使其随常规测试
 * 目标在两模式 CI 运行）。字典＝<testdata 根>/requirements-ids.json。
 */
TEST(GoldenDataLint, CoveredRequirementsInDictionary_RT_T12_ACC3)
{
    // 字典与 testdata 根平铺（testdata/requirements-ids.json——lint 工具
    // 同款资产；goldenDataRoot() 即 testdata 根）。
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

    for (const auto* dsId : {"rt-fk-equation", "rt-namemap-roundtrip"}) {
        const tk::GoldenDataset ds = expectDatasetLoads(dsId, "1.0.0");
        for (const std::string& req : ds.manifest().coveredRequirements) {
            EXPECT_NE(dict.find(req), dict.end())
                << dsId << ": coveredRequirements ID 不在字典: " << req;
        }
    }
}

}  // namespace
