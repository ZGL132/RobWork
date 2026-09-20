/**
 * @file   TestMainReport.cpp
 * @brief  `sdurws_ird_reporting_contract_test` 的自有测试入口——安装 testkit
 *         TestRecordListener（ird-test-report.json 聚合落盘，RPT-T11 契约
 *         套件的留痕面）。
 *
 * 设计依据：
 *   - units/reporting.md §11 RPT-T11 行（verify 命令含 `_contract_test` 目标
 *     构建＋ctest 收口——结论二元＝全部用例通过，UI-T14 ctest 先例）、§3.4
 *     （`_contract_test`＝跨单元契约面）
 *   - units/testkit.md §7.2/§7.3（ird-test-report.json 与 gtest XML 并存；
 *     installTestRecordListener 原文签名——测试 main 自持两种形态之一）
 *   - 先例：execution/contract_test/ContractTestMain.cpp（EX-T09 同款——
 *     无子进程分派形态）、io/test/TestMainReport.cpp
 *
 * 背景说明：gtest_main 的固定入口无监听器安装点——IRD_TEST_INFO 写入的
 * TestRecord 在无监听器时是 no-op。本 TU 以自有 main 调用
 * installTestRecordListener，链接面相应从 GTest::gtest_main 改为
 * GTest::gtest＋sdurws_ird_testkit（T-1 允许形态：仅测试目标链 testkit——
 * 产品目标零 testkit 边不变，LinkageContractTest::NoTestkitEdge 钉住）。
 * 运行可用 --ird_report=<path> 指定报告路径（缺省＝工作目录下
 * ird-test-report.json；参数在 gtest 解析前被摘除）。
 *
 * 消费纪律（任务契约 acceptance 5）：TestRecord 与 ird-test-report.json 同
 * ReviewReport 数据契约无共享（testkit §2.3 对照表 reporting 行冻结口径）。
 *
 * 线程模型：单线程入口（gtest 串行执行——测试目标纪律）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/testkit/gtest/RecordListener.hpp>

/// 测试入口：初始化 gtest→安装报告监听器→执行全部用例。
int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    // 静态存储期监听器（gtest listeners 不接管所有权——官方 recommended
    // 做法；进程生命周期与测试程序一致）。
    sdurws::ird::testkit::installTestRecordListener(argc, argv);
    return RUN_ALL_TESTS();
}
