/**
 * @file   ContractTestMain.cpp
 * @brief  `sdurws_ird_io_contract_test` 的自有测试入口——安装 testkit
 *         TestRecordListener（ird-test-report.json 用例级明细落盘）。
 *
 * 设计依据：
 *   - 任务契约 tasks/foundation/IO-T07.json acceptance 1（契约面用例
 *     （IoPackContract 组——V18/V20/V21/V22/V23/V31/V32）结果同样逐条
 *     登记进 ird-test-report.json，用例名携带需求/AT 追溯字段）；
 *   - units/testkit.md §7.2/§7.3（记录契约与监听器安装——与单元测试
 *     目标 main 同款形态）；先例＝project 侧双 main（TestMainReport.cpp
 *     ＋LockContractTest main 增量——PRJ-T15 验收 §4.4/4.6 在案）。
 *
 * 背景说明：契约测试目标与单元测试目标使用同一 testkit 报告设施；两份
 * 报告文件分开落盘（--ird_report 各自指定），避免用例级明细互相覆盖。
 * T-1 红线不变：testkit 仅进入本测试目标，产品库 sdurws_ird_io 零
 * testkit 边。
 *
 * 线程模型：单线程入口（gtest 串行执行）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/testkit/gtest/RecordListener.hpp>

/// 测试入口：初始化 gtest→安装报告监听器→执行全部用例。
int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    // 同 test/TestMainReport.cpp 口径：静态存储期监听器＋OnTestProgramEnd
    // 聚合写盘；写盘失败只告警不掩盖测试结果。
    sdurws::ird::testkit::installTestRecordListener(argc, argv);
    return RUN_ALL_TESTS();
}
