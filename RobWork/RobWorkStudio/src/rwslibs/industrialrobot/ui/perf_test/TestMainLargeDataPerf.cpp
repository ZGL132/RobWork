/**
 * @file   TestMainLargeDataPerf.cpp
 * @brief  `sdurws_ird_ui_largedata_perf_test` 的自有测试入口——无 Qt 的
 *         纯 gtest 基准测试形态＋安装 testkit TestRecordListener
 *         （ird-test-report.json 聚合）。
 *
 * 设计依据：
 *   - units/ui.md §12.1（测试目标分工；模型/基准测试形态）；
 *   - units/testkit.md §7.2/§7.3（ird-test-report.json 与 gtest XML 并存；
 *     installTestRecordListener 签名）；
 *   - 先例：ui/perf_test/TestMainPerf.cpp（WP-23-T02 交互响应基准入口——
 *     本目标零 Qt 消费（三类数据的加载/保存/分页全部在模型/存储层承载，
 *     Widget 与事件循环均在口径外），故入口不需要 QCoreApplication，
 *     其余形态与该先例逐条同构）。
 *
 * 线程模型：单线程入口（gtest 串行执行——测试目标纪律）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/testkit/gtest/RecordListener.hpp>

/// 测试入口：初始化 gtest→安装报告监听器→执行用例。
int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    // 监听器所有权按 gtest 公开契约移交：Append 接管并在测试程序结束时释放
    // （gtest/gtest.h TestEventListeners::Append 注释；F-958 修复后为堆对象，
    // 静态/栈对象地址交 Append 属非法 delete）。
    sdurws::ird::testkit::installTestRecordListener(argc, argv);
    return RUN_ALL_TESTS();
}
