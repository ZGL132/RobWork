/**
 * @file   TestMainReport.cpp
 * @brief  `sdurws_ird_project_test` 的自有测试入口——安装 testkit
 *         TestRecordListener（ird-test-report.json 聚合落盘）。
 *
 * 设计依据：
 *   - units/project.md §12 PRJ-T15 行（PRJ-T15 消费登记：testkit 设施
 *     按 T-1 允许形态接入测试目标——任务契约 acceptance 2"TempDir/
 *     DeterministicEnv/IRD_TEST_INFO/IRD_EXPECT_* 按测试目标接入"）、
 *     §11 头注（每用例注明需求/AT——IRD_TEST_INFO 的落地面）；
 *   - units/testkit.md §7.2（机器可读测试结果与 gtest XML 并存）、§7.3
 *     （installTestRecordListener 原文签名——测试 main 自持两种形态之
 *     一）、D-06（testkit 库本体零 gtest——监听器是 gtest/ 适配层的
 *     header-only 代码，在消费方 TU 编译）。
 *
 * 背景说明（为什么替换 GTest::gtest_main）：gtest_main 提供的固定入口
 * 无监听器安装点——IRD_TEST_INFO/IRD_* 宏写入的 TestRecord 在无监听器
 * 时是 no-op（不产生 ird-test-report.json）。本 TU 以自有 main 调用
 * installTestRecordListener（testkit/test/TestMain.cpp 同款先例），链接
 * 面相应从 GTest::gtest_main 改为 GTest::gtest（CMakeLists 同步登记）。
 * 运行可用 --ird_report=<path> 指定报告路径（缺省＝工作目录下
 * ird-test-report.json）；该参数在 gtest 解析前被摘除，不影响 gtest。
 *
 * 线程模型：单线程入口（gtest 串行执行——测试目标纪律）。
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
    // （RecordListener 契约）。
    sdurws::ird::testkit::installTestRecordListener(argc, argv);
    return RUN_ALL_TESTS();
}
