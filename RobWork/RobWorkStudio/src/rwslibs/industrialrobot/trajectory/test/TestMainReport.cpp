/**
 * @file   TestMainReport.cpp
 * @brief  `sdurws_ird_trajectory_test` 的自有测试入口——安装 testkit
 *         TestRecordListener（ird-test-report.json 用例级明细落盘）。
 *
 * 设计依据：
 *   - AGENTS.md §4.2（验证留痕——gtest XML＋ird-test-report.json 并存）
 *   - units/testkit.md §7.2/§7.3（机器可读测试结果与监听器安装原语）
 *   - 先例：dynamics/test/TestMainReport.cpp（WP-17-T02 同款——
 *     gtest_main 固定入口无监听器安装点，IRD_TEST_INFO 写入的 TestRecord
 *     在无监听器时是 no-op，因此以自有 main 替换 gtest_main）
 *
 * 线程模型：单线程入口（gtest 串行执行——测试目标纪律）。
 *
 * 与 dynamics 先例的差异说明（诚实登记，非遗漏）：本入口**不构造
 * QCoreApplication**——trajectory T03 测试面（登记表＋红线扫描）零 Qt
 * 消费，测试目标链接面亦无 Qt 目标（_test 不链 _plugin——插件目标随
 * 契约测试消费其装配门面）；WP-16-T12 出现 Qt 类型面用例时照 modeling
 * PluginPanelTest 先例补 QCoreApplication 构造。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/testkit/gtest/RecordListener.hpp>

/// 测试入口：初始化 gtest→安装报告监听器→执行用例。
int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    // 监听器为静态存储期对象（gtest listeners 不接管所有权——官方
    // recommended 做法；进程生命周期与测试程序一致）。聚合报告在
    // OnTestProgramEnd 写盘（缺省＝进程工作目录下 ird-test-report.json，
    // 可用 --ird_report=<path> 指定——留痕脚本据此把用例级明细写入
    // traceability/builds/wp16-t03/）；写盘失败只向 stderr 告警，不掩盖
    // 测试结果（RecordListener 契约——gtest 退出码仍真实反映用例成败）。
    sdurws::ird::testkit::installTestRecordListener(argc, argv);
    return RUN_ALL_TESTS();
}
