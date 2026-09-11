/**
 * @file   ReportTest.cpp
 * @brief  机器可读测试结果用例组——TK-RPT（units/testkit.md §7.2/§7.5/§8）。
 *
 * 设计依据：
 *   - units/testkit.md §7.2（TestRecord 字段表＋四类状态：Skipped/NotRun 不得
 *     计为 Passed；envUnavailable/datasetInvalid 不计失败）、§7.3（IRD_TEST_INFO
 *     追溯登记）、§7.5（四类分列＋"不可判定"判据）；§8 TK-RPT 行
 *   - 任务契约 tasks/foundation/TK-T10.json acceptance①：六类 outcome 聚合
 *     正确；ird-test-report 机器可读
 *
 * 测试环境：登记表为进程级单例——本文件各用例 begin/end 自闭合（不依赖
 * listener 在场；store 直接驱动），与 TestMain 安装的 listener 隔离（listener
 * 的 begin/end 发生在 gtest 事件层，测试体内的 store.beginRecord 属
 * 无活动记录窗口——两者不重叠）。串行纪律：gtest 同文件天然串行。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/testkit/Fixture.hpp>
#include <sdurws/ird/testkit/JsonLite.hpp>
#include <sdurws/ird/testkit/Report.hpp>
#include <sdurws/ird/testkit/gtest/GoldenFixture.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;
namespace {
using namespace sdurws::ird::testkit;
namespace rpt = sdurws::ird::testkit::report;

/// 构造指定 outcome 的最小记录（聚合用例的固定输入）。
rpt::TestRecord recordWith(rpt::Outcome outcome)
{
    rpt::TestRecord r;
    r.testId = std::string{"Suite.Case_"} + rpt::toToken(outcome);
    r.repro = ReproRecord{};   // 必填字段：默认复现记录兜底
    r.outcome = outcome;
    if (outcome != rpt::Outcome::Passed) {
        r.reason = std::string{rpt::toToken(outcome)} + ": 用例构造";
    }
    return r;
}

}  // namespace

/** 六类 token：枚举全覆盖、两两不同、与 §7.2 原文逐字一致。 */
TEST(OutcomeTokens, AllSixRegistered_UT_RPT)
{
    EXPECT_STREQ(rpt::toToken(rpt::Outcome::Passed), "passed");
    EXPECT_STREQ(rpt::toToken(rpt::Outcome::Failed), "failed");
    EXPECT_STREQ(rpt::toToken(rpt::Outcome::Skipped), "skipped");
    EXPECT_STREQ(rpt::toToken(rpt::Outcome::NotRun), "notRun");
    EXPECT_STREQ(rpt::toToken(rpt::Outcome::EnvUnavailable), "envUnavailable");
    EXPECT_STREQ(rpt::toToken(rpt::Outcome::DatasetInvalid), "datasetInvalid");
}

/** 六类聚合（acceptance①）：分列计数正确；Skipped 不计 Passed；环境/数据/
 * notRun 非零 → decisive=false（§7.5"不可判定"）；全绿报告 decisive=true。 */
TEST(ReportAggregation, SixOutcomesCountedAndDecisiveFlag_UT_RPT)
{
    rpt::Report report;
    report.add(recordWith(rpt::Outcome::Passed));
    report.add(recordWith(rpt::Outcome::Passed));
    report.add(recordWith(rpt::Outcome::Failed));
    report.add(recordWith(rpt::Outcome::Skipped));
    report.add(recordWith(rpt::Outcome::NotRun));
    report.add(recordWith(rpt::Outcome::EnvUnavailable));
    report.add(recordWith(rpt::Outcome::DatasetInvalid));
    const auto s = report.summary();
    EXPECT_EQ(s.total, 7);
    EXPECT_EQ(s.passed, 2);
    EXPECT_EQ(s.failed, 1);
    EXPECT_EQ(s.skipped, 1);
    EXPECT_EQ(s.notRun, 1);
    EXPECT_EQ(s.envUnavailable, 1);
    EXPECT_EQ(s.datasetInvalid, 1);
    EXPECT_FALSE(s.decisive()) << "notRun/env/dataset 非零→不可判定（§7.5）";
    // 对照组：全绿报告 → decisive=true（CI 绿灯的机器判据）。
    rpt::Report allPass;
    allPass.add(recordWith(rpt::Outcome::Passed));
    EXPECT_TRUE(allPass.summary().decisive());
    EXPECT_EQ(allPass.summary().passed, 1);
}

/** 记录 JSON 机器可读（acceptance①）：§7.2 字段表逐键可解析、outcome 为
 * 六值 token、dataset/repro/environment 嵌套结构完整。 */
TEST(TestRecordJson, MachineReadableFieldTable_UT_RPT)
{
    rpt::TestRecord r;
    r.testId = "Suite.Case";
    r.requirementIds = {"KIN-12"};
    r.atIds = {"AT-03"};
    r.hasDataset = true;
    r.datasetId = "kin-fk-planar-2r";
    r.datasetVersion = "1.0.0";
    r.toleranceProfile = "kin-fk@1.0.0";
    r.repro.seed = 20260909;
    r.repro.threadCount = 2;
    r.environment = rpt::defaultEnvironment();
    r.outcome = rpt::Outcome::EnvUnavailable;
    r.reason = "env-unavailable: 步骤①数据根不可解析";
    r.artifacts.push_back({"C:/tmp/scene", "tempdir-scene"});
    r.durationMs = 5;
    const auto v = parseJson(r.toJsonText());
    ASSERT_TRUE(v.isObject());
    // 字段表逐键核对（§7.2：机器可读＝键可寻址，缺失即不可判定）。
    const auto* testId = v.find("testId");
    ASSERT_NE(testId, nullptr);
    EXPECT_EQ(testId->text, "Suite.Case");
    const auto* reqs = v.find("requirementIds");
    ASSERT_NE(reqs, nullptr);
    ASSERT_TRUE(reqs->isArray());
    EXPECT_EQ(reqs->items.size(), 1u);
    EXPECT_EQ(reqs->items[0].text, "KIN-12");
    const auto* ats = v.find("atIds");
    ASSERT_NE(ats, nullptr);
    EXPECT_EQ(ats->items.size(), 1u);
    // dataset 条件字段：置位时为 {id, version} 对象。
    const auto* dataset = v.find("dataset");
    ASSERT_NE(dataset, nullptr);
    EXPECT_EQ(dataset->find("id")->text, "kin-fk-planar-2r");
    EXPECT_EQ(dataset->find("version")->text, "1.0.0");
    // repro 嵌套：复现记录可寻址（NFR-COR-02 报告随附复现上下文）。
    const auto* repro = v.find("repro");
    ASSERT_NE(repro, nullptr);
    EXPECT_DOUBLE_EQ(repro->find("seed")->number, 20260909.0);
    EXPECT_DOUBLE_EQ(repro->find("threadCount")->number, 2.0);
    const auto* outcome = v.find("outcome");
    ASSERT_NE(outcome, nullptr);
    EXPECT_EQ(outcome->text, "envUnavailable");
    const auto* reason = v.find("reason");
    ASSERT_NE(reason, nullptr);
    EXPECT_FALSE(reason->text.empty());
    const auto* duration = v.find("durationMs");
    ASSERT_NE(duration, nullptr);
    EXPECT_DOUBLE_EQ(duration->number, 5.0);
    const auto* artifacts = v.find("artifacts");
    ASSERT_NE(artifacts, nullptr);
    EXPECT_EQ(artifacts->items.size(), 1u);
    EXPECT_EQ(artifacts->items[0].find("kind")->text, "tempdir-scene");
}

/** 写出往返（§7.2 落盘形态）：writeFile → JsonLite 重新解析——schema/
 * summary/tests 三键与计数一致（与 gtest XML 并存的机器核验路径）。 */
TEST(ReportWrite, FileRoundTripMachineReadable_UT_RPT)
{
    TempDir dir{"rpt"};
    rpt::Report report;
    auto r = recordWith(rpt::Outcome::Passed);
    r.testId = "Suite.Writes";
    report.add(std::move(r));
    const auto path = dir.path() / "ird-test-report.json";
    report.writeFile(path);
    const auto v = parseJson([&path] {
        // 读回文件文本（JSON 可解析性本身即"机器可读"的核验——JsonLite 严格解析）。
        std::ifstream in(path, std::ios::binary);
        std::stringstream buffer;
        buffer << in.rdbuf();
        return buffer.str();
    }());
    EXPECT_STREQ(v.find("schemaVersion")->text.c_str(), "ird-test-report/1");
    const auto* summary = v.find("summary");
    ASSERT_NE(summary, nullptr);
    EXPECT_DOUBLE_EQ(summary->find("passed")->number, 1.0);
    EXPECT_TRUE(summary->find("decisive")->boolean);
    const auto* tests = v.find("tests");
    ASSERT_NE(tests, nullptr);
    ASSERT_TRUE(tests->isArray());
    EXPECT_EQ(tests->items.size(), 1u);
    EXPECT_EQ(tests->items[0].find("testId")->text, "Suite.Writes");
}

/** 登记表会合协议：夹具预置 outcome 优先于 listener 计算值（§7.2——
 * 环境不可用不得因 GTEST_SKIP 被计为 skipped）。endAndTake 闭环——不入册，
 * 不干扰 listener 聚合（栈式嵌套会话）。 */
TEST(RecordStore, PresetOutcomeWinsOverComputed_UT_RPT)
{
    auto& store = rpt::TestRecordStore::instance();
    store.beginRecord("Suite.Preset", rpt::defaultEnvironment());
    ASSERT_NE(store.current(), nullptr);
    rpt::setOutcome(rpt::Outcome::DatasetInvalid, "dataset-invalid: 步骤②装载失败");
    const auto finalized
        = store.endAndTake(rpt::Outcome::Passed, "", 3);   // 计算值＝Passed
    ASSERT_NE(finalized, nullptr);
    EXPECT_EQ(finalized->outcome, rpt::Outcome::DatasetInvalid)
        << "预置分类必须优先（§7.2 分类语义）";
    EXPECT_EQ(finalized->reason, "dataset-invalid: 步骤②装载失败");
    EXPECT_EQ(finalized->durationMs, 3);
}

/** IRD_TEST_INFO 落点：需求/AT/数据集/档案写入当前记录（§7.3；A.1 形态）。 */
TEST(RecordStore, IrdTestInfoFillsRecord_UT_RPT)
{
    auto& store = rpt::TestRecordStore::instance();
    store.beginRecord("Suite.Info", rpt::defaultEnvironment());
    rpt::irdTestInfo("KIN-12", {"AT-03"}, DatasetRef{"kin-fk-planar-2r", "1.0.0"},
                     std::string{"kin-fk@1.0.0"});
    const auto finalized = store.endAndTake(rpt::Outcome::Passed, "", 1);
    ASSERT_NE(finalized, nullptr);
    EXPECT_EQ(finalized->requirementIds, std::vector<std::string>{"KIN-12"});
    EXPECT_EQ(finalized->atIds, std::vector<std::string>{"AT-03"});
    EXPECT_TRUE(finalized->hasDataset);
    EXPECT_EQ(finalized->datasetId, "kin-fk-planar-2r");
    EXPECT_EQ(finalized->datasetVersion, "1.0.0");
    EXPECT_EQ(finalized->toleranceProfile, "kin-fk@1.0.0");
}

/** 嵌套会话隔离：listener 活动记录（本测试自己的）之上叠加自有会话——
 * 自由函数只命中栈顶（嵌套记录），listener 记录不受污染；endAndTake
 * 闭环不入册（不进聚合报告）。 */
TEST(RecordStore, NestedSessionIsolatesFromListener_UT_RPT)
{
    auto& store = rpt::TestRecordStore::instance();
    // 前置：listener 已安装（TestMain）——存在外层活动记录（本测试自身）。
    const auto* outer = store.current();
    ASSERT_NE(outer, nullptr) << "listener 已安装→外层记录必须活动";
    const std::string outerId = outer->testId;
    // 叠加嵌套会话并注入"污染"（outcome/artifact）。
    store.beginRecord("Suite.Nested", rpt::defaultEnvironment());
    rpt::setOutcome(rpt::Outcome::Failed, "x");   // 只命中栈顶（嵌套记录）
    rpt::addArtifact("/tmp/x", "kind");
    const auto finalized = store.endAndTake(rpt::Outcome::Passed, "", 0);
    ASSERT_NE(finalized, nullptr);
    EXPECT_EQ(finalized->testId, "Suite.Nested");
    EXPECT_EQ(finalized->outcome, rpt::Outcome::Failed) << "注入命中嵌套记录";
    EXPECT_EQ(finalized->artifacts.size(), 1u);
    // 外层记录未被污染：仍是本测试、未定稿态（嵌套语义的隔离证明）。
    const auto* restored = store.current();
    ASSERT_NE(restored, nullptr);
    EXPECT_EQ(restored->testId, outerId);
    EXPECT_EQ(restored->outcome, rpt::Outcome::NotRun);
    EXPECT_TRUE(restored->artifacts.empty());
}

/** 六步夹具与报告接线（§6.1 步骤⑥）：DemoFixture 测试体的 repro/数据集
 * 已进当前记录——listener 端到端由 TestMain 安装，全量运行后以
 * ird-test-report.json 落盘文件核验（验收证据直接消费该产物）。 */
class RptDemoFixture : public GoldenFixture {
};

TEST_F(RptDemoFixture, FixtureContextBoundToRecord_UT_RPT)
{
    // 当前活动记录由 listener 的 OnTestStart 建立——测试体内可见。
    const auto* rec = rpt::TestRecordStore::instance().current();
    ASSERT_NE(rec, nullptr) << "listener 已安装（TestMain）→记录必须活动";
    EXPECT_EQ(rec->testId, "RptDemoFixture.FixtureContextBoundToRecord_UT_RPT");
    EXPECT_EQ(rec->repro.seed, 20260909ULL) << "步骤⑥：夹具复现上下文已登记";
    EXPECT_EQ(rec->environment.os, "Windows");
    EXPECT_EQ(rec->outcome, rpt::Outcome::NotRun) << "OnTestEnd 前为未定稿态";
}
