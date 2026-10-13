/**
 * @file   PerfBenchmarkTest.cpp
 * @brief  性能基准计时统计夹具用例组（TK-PERF）——P95 线性插值/预热剔除/
 *         无效样本留痕/报告序列化（WP-23-T02 夹具本体的元测试：夹具失败
 *         能力与协议公式保真证明）。
 *
 * 设计依据：
 *   - units/testkit.md §10.1"性能基准夹具"行（WP-23-T02 消费者提案落位
 *     ——本测试即提案的元测试半区）；
 *   - doc/industrial-robot-design/performance-baseline.md §4.2（P95 线性
 *     插值公式、每组有效样本 ≥15、剔除首次预热、无效样本留痕）、§4.3
 *     （禁止 3 次中位数替代 P95）、§4.8（报告必含原始样本序列）；
 *   - 任务契约 tasks/foundation/WP-23-T02.json（验收"导航/选择/筛选/编辑/
 *     视图切换 P95 ≤200 ms"的夹具侧保真前提——验收协议"验证测试真实
 *     失败能力"对夹具本体的同款要求）。
 *
 * 用例策略（元测试——测夹具，不测任何业务域）：全部时长为真实墙钟
 * （steady_clock），预热区分度用真实 sleep 构造（60 ms ≫ 采样调用亚毫秒
 * 级）——不伪造任何测量数字，只控制被测闭包的真实耗时形态。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/testkit/Fixture.hpp>      // TempDir（报告落盘用例）
#include <sdurws/ird/testkit/JsonLite.hpp>    // parseJson（报告回读）
#include <sdurws/ird/testkit/PerfBenchmark.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
namespace {
using namespace sdurws::ird::testkit;

// =====================================================================
// P95 线性插值分位数——协议 §4.2 公式对已知向量的保真
// =====================================================================

/**
 * 元测试：协议 §4.2 公式在可手算向量上的结果核对。
 *   n=15，样本 1..15：h=(15−1)×0.95=13.3 → P95=x[13]+0.3×(x[14]−x[13])
 *     ＝14＋0.3×1＝14.3（升序后 x[13]=14、x[14]=15——1 起样本值即下标+1）；
 *   n=20，样本 1..20：h=19×0.95=18.05 → P95=19＋0.05×1＝19.05；
 *   n=2，样本 {10,20}：h=0.95 → P95=10＋0.95×10＝19.5；
 *   n=1：h=0 → P95＝该样本本身（插值退化）。
 * 另核：入参乱序不改变结果（函数内部升序——协议"样本升序排列"由实现
 * 承担而非调用方预排序）。
 */
TEST(PerfBenchmark, P95LinearInterpolationKnownVectors_WP23T02_ACC1)
{
    // n=15：期望 14.3（手算——协议公式逐项）。
    std::vector<double> s15;
    for (int i = 1; i <= 15; ++i) { s15.push_back(static_cast<double>(i)); }
    EXPECT_DOUBLE_EQ(percentileLinearInterpolated(s15, 0.95), 14.3);

    // n=20：期望 19.05。
    std::vector<double> s20;
    for (int i = 1; i <= 20; ++i) { s20.push_back(static_cast<double>(i)); }
    EXPECT_DOUBLE_EQ(percentileLinearInterpolated(s20, 0.95), 19.05);

    // n=2 两点插值：h=0.95 落在两点间——10＋0.95×(20−10)=19.5。
    EXPECT_DOUBLE_EQ(percentileLinearInterpolated({10.0, 20.0}, 0.95), 19.5);

    // n=1 退化：P95＝唯一样本（无插值余地）。
    EXPECT_DOUBLE_EQ(percentileLinearInterpolated({7.5}, 0.95), 7.5);

    // 乱序等价：同一多重集打乱后结果不变（内部排序的保真证明）。
    std::vector<double> shuffled{15.0, 3.0, 9.0, 1.0, 12.0, 6.0, 14.0, 2.0,
                                 8.0, 13.0, 5.0, 11.0, 7.0, 4.0, 10.0};
    EXPECT_DOUBLE_EQ(percentileLinearInterpolated(shuffled, 0.95),
                     percentileLinearInterpolated(s15, 0.95));

    // 调用方违约面：空序列与 q 越界 fail-fast（不返回伪值）。
    EXPECT_THROW(percentileLinearInterpolated({}, 0.95), std::invalid_argument);
    EXPECT_THROW(percentileLinearInterpolated(s15, 1.5), std::out_of_range);
}

/**
 * 元测试：预热剔除与无效样本留痕（§4.2 两条采样纪律的执行器半区）。
 *
 * 真实时长构造：预热腿闭包真实 sleep 60 ms（远大于采样调用的亚毫秒
 * 耗时）——若预热样本被错误计入 P95 统计，n=5 有效样本下 P95 必然 ≥
 * 60 ms 量级；断言 P95 < 30 ms 即预热剔除的结构证明。无效样本：闭包按
 * 调用序在两处抛出（真实异常路径）——断言逐例留痕（valid=false＋原因）
 * 且有效样本仍达下限（无效不计入分母——采样腿自动补采）。
 */
TEST(PerfBenchmark, WarmupExcludedAndInvalidSamplesTraced_WP23T02_ACC1)
{
    InteractionGroupSpec spec;
    spec.opId = "meta-op";
    spec.stateId = "meta-state";
    spec.warmupRuns = 1;         // §4.2：剔除各组首次预热
    spec.minValidSamples = 5;    // 元测试规模（协议下限 15 由消费方基准用例承载）

    std::size_t callIndex = 0;   // 调用序计数（异常注入的定位面）
    InteractionGroupReport report =
        runInteractionGroup(spec, [&callIndex] {
            const std::size_t k = callIndex++;
            if (k == 0) {
                // 预热腿：真实 sleep 构造大时长——计入统计即暴露。
                std::this_thread::sleep_for(std::chrono::milliseconds(60));
                return;
            }
            if (k == 2 || k == 4) {
                // 采样腿第 1/3 次调用：真实异常→无效样本（区间不完整）。
                throw std::runtime_error("meta: injected failure");
            }
        });

    // 全量执行序列：1 预热＋7 采样调用（5 有效＋2 无效）＝8 条留痕。
    ASSERT_EQ(report.samples.size(), 8u);
    EXPECT_EQ(report.warmupRuns, 1u);
    EXPECT_EQ(report.validCount, 5u);
    EXPECT_EQ(report.invalidCount, 2u);

    // 预热样本：excluded=true 且时长被真实记录（60 ms 量级——留痕不删）。
    EXPECT_TRUE(report.samples[0].excluded);
    EXPECT_GE(report.samples[0].durationMs, 50.0);

    // 无效样本：原位留痕＋原因可读（协议 §4.2"留痕登记失效原因"）。
    // 序位推演：[0]=预热；采样调用 k=1..7 对应 [1..7]——k=2/k=4 抛出，
    // 故无效样本落在 [2] 与 [4]。
    EXPECT_FALSE(report.samples[2].valid);
    EXPECT_FALSE(report.samples[4].valid);
    EXPECT_NE(report.samples[2].invalidReason.find("injected failure"),
              std::string::npos);

    // P95 只由有效样本构成：5 个亚毫秒样本的 P95 必须远小于预热时长
    // （若预热/无效被计入统计，此断言必炸——剔除纪律的结构证明）。
    EXPECT_LT(report.p95Ms, 30.0);
    EXPECT_LT(report.maxValidMs, 30.0);
    EXPECT_LE(report.minValidMs, report.p95Ms);
}

/**
 * 元测试：报告序列化往返——会话 JSON 的字段面与原始样本序列完整性
 * （协议 §4.8：分位数约定登记＋原始样本不得只报统计值）。
 */
TEST(PerfBenchmark, SessionReportJsonRoundtrip_WP23T02_ACC1)
{
    // 装配一份两组合成会话（零硬件依赖——字段面覆盖即可）。
    PerfSessionReport session;
    session.caseId = "meta-b01";
    session.baselineDoc = "performance-baseline #2 B-01";
    session.gitCommit = "0123456789abcdef0123456789abcdef01234567";
    session.buildConfig = "Release";
    session.datasetId = "meta-ds";
    session.datasetVersion = "1";
    session.machineName = "meta-machine";
    session.cpuInfo = "meta-cpu";
    session.osInfo = "meta-os";
    session.memoryInfo = "meta-mem";
    session.storageInfo = "meta-ssd";
    session.powerPolicyNote = "meta-power";
    session.backgroundNote = "meta-bg";

    InteractionGroupReport g;
    g.opId = "nav";
    g.stateId = "idle";
    g.warmupRuns = 1;
    g.validCount = 2;
    g.invalidCount = 1;
    g.p95Ms = 12.5;
    g.maxValidMs = 13.0;
    g.minValidMs = 12.0;
    g.samples.push_back({100.0, true, true, ""});
    g.samples.push_back({0.5, false, false, "meta: boom"});
    g.samples.push_back({12.0, false, true, ""});
    g.samples.push_back({13.0, false, true, ""});
    session.groups.push_back(g);

    PerfThresholdVerdict v;
    v.opId = "nav";
    v.stateId = "idle";
    v.thresholdMs = 200.0;
    v.thresholdSource = "REQUIREMENTS NFR-PERF-01";
    v.measuredP95Ms = 12.5;
    v.pass = true;
    session.verdicts.push_back(v);
    session.allPassed = true;
    session.notes.push_back("meta note");

    // 解析回读：标量字段、分位数约定串、原始样本序列（含无效样本原因）、
    // 阈值判定出处——逐面核对（键序确定性由 dumpJson 保证，此处只核对
    // 语义面）。
    const JsonValue j = parseJson(perfReportToJson(session));
    ASSERT_TRUE(j.isObject());
    EXPECT_STREQ(j.find("caseId")->text.c_str(), "meta-b01");
    EXPECT_STREQ(j.find("p95Convention")->text.c_str(), p95ConventionToken());
    EXPECT_STREQ(j.find("gitCommit")->text.c_str(), session.gitCommit.c_str());
    EXPECT_STREQ(j.find("datasetId")->text.c_str(), "meta-ds");

    const JsonValue* groups = j.find("groups");
    ASSERT_TRUE(groups != nullptr && groups->isArray());
    ASSERT_EQ(groups->items.size(), 1u);
    const JsonValue& g0 = groups->items[0];
    EXPECT_DOUBLE_EQ(g0.find("p95Ms")->number, 12.5);
    const JsonValue* samples = g0.find("samples");
    ASSERT_TRUE(samples != nullptr && samples->isArray());
    ASSERT_EQ(samples->items.size(), 4u);  // 原始序列全量：预热＋无效＋2 有效
    EXPECT_FALSE(samples->items[1].find("valid")->boolean);
    EXPECT_STREQ(samples->items[1].find("invalidReason")->text.c_str(),
                 "meta: boom");

    const JsonValue* verdicts = j.find("verdicts");
    ASSERT_TRUE(verdicts != nullptr && verdicts->isArray());
    ASSERT_EQ(verdicts->items.size(), 1u);
    EXPECT_DOUBLE_EQ(verdicts->items[0].find("thresholdMs")->number, 200.0);
    EXPECT_STREQ(verdicts->items[0].find("thresholdSource")->text.c_str(),
                 "REQUIREMENTS NFR-PERF-01");
    EXPECT_TRUE(verdicts->items[0].find("pass")->boolean);
    EXPECT_TRUE(j.find("allPassed")->boolean);
}

/**
 * 元测试：报告文件写出——TempDir 落盘成功且文件可回读解析（拖挂通道
 * 真实可用；写失败返回 false 不抛的拖挂纪律面）。
 */
TEST(PerfBenchmark, SessionReportFileWriteRoundtrip_WP23T02_ACC1)
{
    TempDir dir("tk-perf-report");

    PerfSessionReport session;
    session.caseId = "meta-file";
    session.buildConfig = "Release";

    EXPECT_TRUE(writePerfReportJson(dir.path(), session));
    const fs::path written = dir.path() / "meta-file.json";
    ASSERT_TRUE(fs::exists(written));

    // 回读解析：caseId 往返一致（文件通道与序列化通道衔接无缺损）。
    std::ifstream in(written, std::ios::binary);
    const std::string text{(std::istreambuf_iterator<char>(in)),
                           std::istreambuf_iterator<char>()};
    const JsonValue j = parseJson(text);
    ASSERT_TRUE(j.isObject());
    EXPECT_STREQ(j.find("caseId")->text.c_str(), "meta-file");
    EXPECT_STREQ(j.find("schemaVersion")->text.c_str(), "ird-perf-report/1");
}

}  // namespace
