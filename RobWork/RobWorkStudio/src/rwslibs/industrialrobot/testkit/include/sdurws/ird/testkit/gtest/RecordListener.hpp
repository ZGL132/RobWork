/**
 * @file   RecordListener.hpp
 * @brief  TestRecordListener——gtest 事件到 TestRecord/ird-test-report.json 的
 *         适配头（§7.3；header-only，消费方 TU 编译）。
 *
 * 设计依据：
 *   - units/testkit.md §7.2（记录契约）、§7.3（installTestRecordListener 签名、
 *     测试 main 自持两种形态之一）、§7.5（CI 消费）；任务契约
 *     tasks/foundation/TK-T10.json（≙WP-02-T10）
 *
 * 分层说明（D-06 边界）：本头 include <gtest/gtest.h>，仅供消费方
 * `_test`/`_contract_test` 目标包含（与 AssertMacros.hpp/GoldenFixture.hpp
 * 同款分层先例）；库本体零 gtest，报告数据类型在 Report.hpp（库侧）。
 *
 * 用法（§7.3 原文签名）：消费方 main（或 gtest main 替换处）调用一次
 *   installTestRecordListener(argc, argv);
 * 可选参数 --ird_report=<path> 指定报告落盘路径（缺省＝进程工作目录下的
 * ird-test-report.json；参数会被从 argv 摘除，不影响 gtest 解析）。
 * 运行结束聚合写盘，与 gtest XML 并存（§7.2）。
 */

#ifndef SDURWS_IRD_TESTKIT_GTEST_RECORDLISTENER_HPP
#define SDURWS_IRD_TESTKIT_GTEST_RECORDLISTENER_HPP

#include <gtest/gtest.h>  // 消费方 _test 目标专用——见文件头分层说明

#include <sdurws/ird/testkit/Report.hpp>

#include <chrono>
#include <filesystem>
#include <cstdio>   // std::fprintf（写盘失败告警——不掩盖测试结果）
#include <string>
#include <vector>

namespace sdurws::ird::testkit {

/**
 * @brief gtest 事件监听 → TestRecord 生命周期驱动（§7.3）。
 *
 * OnTestStart：建记录（testId＝"<suite>.<case>"，环境＝defaultEnvironment()）；
 * OnTestEnd：outcome 判定——夹具预置（envUnavailable/datasetInvalid，§7.2）
 * 优先，其次 gtest 跳过/失败/通过；OnTestProgramEnd：聚合写盘。
 */
class TestRecordListener : public ::testing::EmptyTestEventListener {
public:
    /**
     * @brief 构造监听器。
     *
     * @param reportPath [in] 报告落盘路径（OnTestProgramEnd 写出）
     */
    explicit TestRecordListener(std::filesystem::path reportPath)
        : reportPath_(std::move(reportPath))
    {
    }

    void OnTestStart(const ::testing::TestInfo& info) override
    {
        start_ = std::chrono::steady_clock::now();
        // testId＝gtest 全名 "<suite>.<case>"（§7.2 字段表口径）。
        const std::string testId
            = std::string{info.test_suite_name()} + "." + info.name();
        report::TestRecordStore::instance().beginRecord(testId,
                                                report::defaultEnvironment());
    }

    void OnTestEnd(const ::testing::TestInfo& info) override
    {
        namespace chr = std::chrono;
        const auto durationMs = static_cast<int>(
            chr::duration_cast<chr::milliseconds>(chr::steady_clock::now() - start_)
                .count());
        // gtest 计算值（夹具预置优先级更高——store.endRecord 内部裁决）。
        const auto* result = info.result();
        report::Outcome computed = report::Outcome::Passed;
        std::string reason;
        if (result != nullptr && result->Failed()) {
            computed = report::Outcome::Failed;
            reason = "failed: " + std::to_string(result->total_part_count())
                + " part(s)——详见 gtest 输出与 comparisons 字段";
        } else if (result != nullptr && result->Skipped()) {
            // 显式跳过（GTEST_SKIP）——§7.2：不计通过，理由登记，逐条消账。
            computed = report::Outcome::Skipped;
            reason = "skipped: 显式跳过（GTEST_SKIP；夹具级环境/数据失败见"
                     " envUnavailable/datasetInvalid 专项记录）";
        }
        report::TestRecordStore::instance().endRecord(computed, reason, durationMs);
    }

    void OnTestProgramEnd(const ::testing::UnitTest&) override
    {
        // 聚合写盘（§7.2"与 gtest XML 并存"）。写盘失败＝环境错误：向 stderr
        // 告警但不改变 gtest 退出码——报告机制自身故障不得掩盖测试结果。
        report::Report report;
        for (auto& record : report::TestRecordStore::instance().takeAll()) {
            report.add(std::move(record));
        }
        try {
            report.writeFile(reportPath_);
        } catch (const TestKitError& e) {
            std::fprintf(stderr, "ird-test-report 写盘失败（%s）——CI 归档将缺失\n",
                         e.what());
        }
    }

private:
    std::filesystem::path reportPath_;                       ///< 报告落盘路径
    std::chrono::steady_clock::time_point start_{};          ///< 用例起点（耗时统计）
};

/**
 * @brief 安装 TestRecordListener（§7.3 原文签名；main 中调用一次）。
 *
 * @param argc [in,out] 命令行参数计数；--ird_report 参数会被摘除并同步缩减
 * @param argv [in,out] 命令行参数；被摘除项置 nullptr（与 gtest 摘除风格一致）
 *
 * 参数语法：--ird_report=<path>（缺省＝工作目录下 ird-test-report.json）。
 * 监听器为静态存储期对象（gtest listeners 不接管所有权——官方 recommended
 * 做法，进程生命周期与测试程序一致）。
 */
inline void installTestRecordListener(int& argc, char** argv)
{
    std::filesystem::path reportPath{"ird-test-report.json"};
    // 摘除 --ird_report=<path>（自维护参数；gtest 不识别该形态）。
    std::vector<char*> kept;
    kept.reserve(static_cast<std::size_t>(argc));
    for (int i = 0; i < argc; ++i) {
        const std::string arg = argv[i] != nullptr ? argv[i] : "";
        const std::string prefix = "--ird_report=";
        if (arg.rfind(prefix, 0) == 0) {
            reportPath = arg.substr(prefix.size());
        } else {
            kept.push_back(argv[i]);
        }
    }
    int newArgc = 0;
    for (char* a : kept) { argv[newArgc++] = a; }
    for (int i = newArgc; i < argc; ++i) { argv[i] = nullptr; }
    argc = newArgc;
    // 静态存储期监听器（gtest listeners 不接管所有权）。
    static TestRecordListener listener{reportPath};
    ::testing::UnitTest::GetInstance()->listeners().Append(&listener);
}

}  // namespace sdurws::ird::testkit

#endif  // SDURWS_IRD_TESTKIT_GTEST_RECORDLISTENER_HPP
