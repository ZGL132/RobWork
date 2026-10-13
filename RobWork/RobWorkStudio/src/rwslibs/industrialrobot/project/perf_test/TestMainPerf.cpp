/**
 * @file   TestMainPerf.cpp
 * @brief  `sdurws_ird_project_perf_test` 的自有测试入口——黄金项目（1,000
 *         对象/100 修订）打开热启动基准的测试形态＋安装 testkit
 *         TestRecordListener（ird-test-report.json 聚合）。
 *
 * 设计依据：
 *   - units/testkit.md §7.2/§7.3（ird-test-report.json 与 gtest XML 并存；
 *     installTestRecordListener 签名）；
 *   - 先例：execution/perf_test/TestMainPerf.cpp（WP-23-T03 取消时序基准
 *     入口同款形态）——project 单元零 Qt（D-01 含 Core 亦禁），本入口
 *     不构造 QCoreApplication，基准用例在普通线程执行。
 *
 * 线程模型：gtest 串行执行用例；冷启动用例内部经 testkit TestProcessRunner
 * 启动独立采样子进程（sdurws_ird_project_openbench_worker——每冷启动样本
 * 一个全新进程，PM-16 原文"冷启动重置＝采样前重启进程"的产品承载）。
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
