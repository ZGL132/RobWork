/**
 * @file   TestMainPerf.cpp
 * @brief  `sdurws_ird_ui_perf_test` 的自有测试入口——QCoreApplication 级
 *         基准测试形态＋安装 testkit TestRecordListener（ird-test-report.json
 *         聚合）。
 *
 * 设计依据：
 *   - units/ui.md §12.1（测试目标分工；QCoreApplication 级模型/基准测试
 *     形态——无需 GUI 平台插件，testkit.md §6.7 模型测试豁免行）；
 *   - units/testkit.md §7.2/§7.3（ird-test-report.json 与 gtest XML 并存；
 *     installTestRecordListener 签名）；
 *   - 先例：ui/test/TestMainReport.cpp（UI 测试入口同款——WP-23-T02 交互
 *     响应基准目标复用该形态，零差异除注释与文件名）。
 *
 * 背景说明：基准用例在 QCoreApplication 线程（UI 线程测试形态）执行测量；
 * 事件循环不常驻（用例按需 processEvents 消化后台排队事件——后台态
 * Marshal 落地的消费半区，见 InteractionResponseBenchmarkTest.cpp）。
 *
 * 线程模型：单线程入口（gtest 串行执行——测试目标纪律）；QCoreApplication
 * 在 main 线程构造并存活至 RUN_ALL_TESTS 结束。
 */

#include <gtest/gtest.h>

#include <QCoreApplication>

#include <sdurws/ird/testkit/gtest/RecordListener.hpp>

/// 测试入口：构造 QCoreApplication→初始化 gtest→安装报告监听器→执行用例。
int main(int argc, char** argv)
{
    // QCoreApplication（非 QApplication）：基准测试形态的运行期标记——
    // 无 Widgets 平台插件依赖，可在无显示环境的服务端/headless 运行。
    QCoreApplication app(argc, argv);

    ::testing::InitGoogleTest(&argc, argv);
    // 静态存储期监听器（gtest listeners 不接管所有权——官方 recommended
    // 做法；进程生命周期与测试程序一致）。
    sdurws::ird::testkit::installTestRecordListener(argc, argv);
    return RUN_ALL_TESTS();
}
