/**
 * @file   TestMainReport.cpp
 * @brief  `sdurws_ird_execution_test` 的自有测试入口——安装 testkit
 *         TestRecordListener（ird-test-report.json 聚合落盘）。
 *
 * 设计依据：
 *   - units/execution.md §12 EX-T09 行（契约测试套件落地——§11 矩阵逐项
 *     留痕：gtest XML＋ird-test-report.json 并存）、§11 末段（每条用例经
 *     IRD_TEST_INFO 登记需求/AT 追溯——本入口即其落地面）
 *   - units/testkit.md §7.2（机器可读测试结果与 gtest XML 并存）、§7.3
 *     （installTestRecordListener 原文签名——测试 main 自持两种形态之一）
 *   - 先例：project/test/TestMainReport.cpp（PRJ-T15 同款）、io/test/
 *     TestMainReport.cpp
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
    // 监听器所有权按 gtest 公开契约移交：Append 接管并在测试程序结束时释放
    // （gtest/gtest.h TestEventListeners::Append 注释；F-958 修复后为堆对象，
    // 静态/栈对象地址交 Append 属非法 delete）。
    sdurws::ird::testkit::installTestRecordListener(argc, argv);
    return RUN_ALL_TESTS();
}
