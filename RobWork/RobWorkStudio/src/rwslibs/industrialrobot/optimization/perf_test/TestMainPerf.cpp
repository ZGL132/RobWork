/**
 * @file   TestMainPerf.cpp
 * @brief  `sdurws_ird_optimization_perf_test` 的自有测试入口——基准测试形态
 *         ＋安装 testkit TestRecordListener（ird-test-report.json 聚合）。
 *
 * 设计依据：
 *   - units/testkit.md §7.2/§7.3（ird-test-report.json 与 gtest XML 并存；
 *     installTestRecordListener 签名）；
 *   - 先例：execution/perf_test/TestMainPerf.cpp（WP-23-T03 取消时序基准
 *     入口同款形态；ui WP-23-T02 首创）——optimization 单元为 L2 计算库
 *     〔零 Qt，D-01〕，本入口不构造 QCoreApplication（基准用例在普通线程
 *     执行，无 UI 事件循环面——与 execution 入口完全同形）。
 *
 * 线程模型：gtest 串行执行用例；B-06 基准用例内部的并行只发生在
 * JointRunOrchestrator::run() 内部（run 按 config.parallel.threadCount 派生
 * worker 线程并 join——不外泄），用例之间零并发。
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
