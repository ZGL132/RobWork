/**
 * @file   TestMainReport.cpp
 * @brief  `sdurws_ird_ui_test` 的自有测试入口——QCoreApplication 级模型测试
 *         形态＋安装 testkit TestRecordListener（ird-test-report.json 聚合）。
 *
 * 设计依据：
 *   - units/ui.md §12.1（sdurws_ird_ui_test＝无界面模型测试：QCoreApplication
 *     级——AGENTS 模型测试豁免，无需 GUI 平台插件）、§3.1（测试目标分工）；
 *   - units/testkit.md §6.7（模型测试豁免行：QCoreApplication 模型测试不要求
 *     GUI 平台插件）、§7.2/§7.3（ird-test-report.json 与 gtest XML 并存；
 *     installTestRecordListener 签名）；
 *   - 先例：execution/test/TestMainReport.cpp（EX-T09 同款）、io/test/
 *     TestMainReport.cpp。
 *
 * 背景说明（为什么 main 自持而不使用 GTest::gtest_main）：gtest_main 提供的
 * 固定入口无监听器安装点——IRD_TEST_INFO/IRD_* 宏写入的 TestRecord 在无
 * 监听器时是 no-op（不产生 ird-test-report.json）。本 TU 以自有 main 完成
 * QCoreApplication 构造（Qt 链接的运行期半区实证：动态库解析失败即在此
 * 显性失败，不留"构建过＝运行可跑"的假象）与监听器安装。运行可用
 * --ird_report=<path> 指定报告路径（缺省＝工作目录下 ird-test-report.json；
 * 参数在监听器安装时被摘除，不影响 gtest 解析）。
 *
 * 线程模型：单线程入口（gtest 串行执行——测试目标纪律）；QCoreApplication
 * 在 main 线程构造并存活至 RUN_ALL_TESTS 结束（Qt 要求 core app 对象与
 * 消费者同线程）。事件循环不启动（不调 exec——落位期用例为纯断言，
 * 无 queued 投递消费；需要事件泵的用例随 UI-T11+ 落地并自行处理）。
 */

#include <gtest/gtest.h>

#include <QCoreApplication>

#include <sdurws/ird/testkit/gtest/RecordListener.hpp>

/// 测试入口：构造 QCoreApplication→初始化 gtest→安装报告监听器→执行用例。
int main(int argc, char** argv)
{
    // QCoreApplication（非 QApplication）：模型测试形态的运行期标记——
    // 无 Widgets 平台插件依赖，可在无显示环境的服务端/headless 运行
    // （ui.md §12.2 第 7 条"仅使用 QCoreApplication 的模型测试不需要 GUI
    // 平台插件"）。argc/argv 交由 Qt 持有（Qt 参数如 -platform 在此消费）。
    QCoreApplication app(argc, argv);

    ::testing::InitGoogleTest(&argc, argv);
    // 静态存储期监听器（gtest listeners 不接管所有权——官方 recommended
    // 做法；进程生命周期与测试程序一致）。
    sdurws::ird::testkit::installTestRecordListener(argc, argv);
    return RUN_ALL_TESTS();
}
