/**
 * @file   TestMainPerf.cpp
 * @brief  `sdurws_ird_execution_perf_test` 的自有测试入口——基准测试形态
 *         ＋安装 testkit TestRecordListener（ird-test-report.json 聚合）。
 *
 * 设计依据：
 *   - units/testkit.md §7.2/§7.3（ird-test-report.json 与 gtest XML 并存；
 *     installTestRecordListener 签名）；
 *   - 先例：ui/perf_test/TestMainPerf.cpp（WP-23-T02 交互响应基准入口同款
 *     形态）——差异仅一处：execution 单元零 Qt（D-01），本入口不构造
 *     QCoreApplication（基准用例在普通线程执行，无 UI 事件循环面）。
 *
 * 线程模型：gtest 串行执行用例；每个基准用例内部自管驱动线程（真实
 * worker 进程的"调度线程"——见 CancelTimingBenchmarkTest.cpp 的 rig）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/testkit/gtest/RecordListener.hpp>

/// 测试入口：初始化 gtest→安装报告监听器→执行用例。
int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    // 静态存储期监听器（gtest listeners 不接管所有权——官方 recommended
    // 做法；进程生命周期与测试程序一致）。
    sdurws::ird::testkit::installTestRecordListener(argc, argv);
    return RUN_ALL_TESTS();
}
