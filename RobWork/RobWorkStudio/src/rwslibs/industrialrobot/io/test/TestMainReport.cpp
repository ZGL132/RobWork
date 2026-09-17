/**
 * @file   TestMainReport.cpp
 * @brief  `sdurws_ird_io_test` 的自有测试入口——安装 testkit
 *         TestRecordListener（ird-test-report.json 用例级明细落盘）。
 *
 * 设计依据：
 *   - 任务契约 tasks/foundation/IO-T07.json acceptance 1（"§11.2 IO-V01~
 *     V32 全用例……结果逐条登记（gtest XML＋ird-test-report.json——用例
 *     名携带需求/AT 追溯字段〔testkit §3.6 约定〕）"——本 TU 即报告设施
 *     的接线点：gtest_main 固定入口无监听器安装点，IRD_TEST_INFO 写入的
 *     TestRecord 在无监听器时是 no-op，因此以自有 main 替换 gtest_main）；
 *   - units/testkit.md §7.2（机器可读测试结果与 gtest XML 并存）、§7.3
 *     （installTestRecordListener 原文签名——测试 main 自持两种形态之一）、
 *     D-06（testkit 库本体零 gtest——监听器是 gtest/ 适配层 header-only
 *     代码，在消费方 TU 编译）；
 *   - 先例：project/test/TestMainReport.cpp（PRJ-T15 同款——消费 testkit
 *     为 T-1 允许形态，仅测试目标链接，产品目标零 testkit 边）。
 *
 * 背景说明（为什么替换 GTest::gtest_main）：§11.2 矩阵 32 行用例的
 * 需求/AT 追溯字段经 IRD_TEST_INFO 登记进 TestRecordStore；聚合写盘由
 * 监听器在 OnTestProgramEnd 完成。缺省报告落盘＝进程工作目录下
 * ird-test-report.json；可用 --ird_report=<path> 指定（该参数在 gtest
 * 解析前被摘除，不影响 gtest）——留痕脚本据此把用例级明细写入
 * traceability/builds/wp11-t07/。
 *
 * 线程模型：单线程入口（gtest 串行执行——测试目标纪律；io 测试内部
 * 自建的工作线程仅在用例体内 join，不影响监听器单线程语义）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/testkit/gtest/RecordListener.hpp>

/// 测试入口：初始化 gtest→安装报告监听器→执行全部用例。
int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    // 监听器为静态存储期对象（gtest listeners 不接管所有权——官方
    // recommended 做法；进程生命周期与测试程序一致）。聚合报告在
    // OnTestProgramEnd 写盘；写盘失败只向 stderr 告警，不掩盖测试结果
    // （RecordListener 契约——gtest 退出码仍真实反映用例成败）。
    sdurws::ird::testkit::installTestRecordListener(argc, argv);
    return RUN_ALL_TESTS();
}
