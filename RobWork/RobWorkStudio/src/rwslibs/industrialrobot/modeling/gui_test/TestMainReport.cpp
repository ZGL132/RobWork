/**
 * @file   TestMainReport.cpp
 * @brief  `sdurws_ird_modeling_gui_test` 的自有测试入口——QApplication 形态
 *         ＋安装 testkit TestRecordListener（UI-T27 返工补建目标）。
 *
 * 设计依据：
 *   - 任务契约 tasks/foundation/UI-T27.json acceptance（建模 gui_test 新
 *     目标，ird_gui 串行——UI-T27 诚实登记缺口的兑现载体）；
 *   - units/modeling.md §9.7（面板 Widgets 级契约的执行面）、testkit §6.7
 *     （GUI 测试单实例串行）、§7.2/§7.3（ird-test-report.json 与 gtest XML
 *     并存）；
 *   - 先例：ui/gui_test/TestMainReport.cpp（QApplication main＋监听器安装
 *     同款形态——sdurws_ird_ui_gui_test 落位面复用，零行为分叉）。
 *
 * 线程模型：单线程入口（gtest 串行纪律；GUI 目标以 ird_gui 标签单实例
 * 串行调度）。QApplication 在 main 线程构造并存活至 RUN_ALL_TESTS 结束
 * （Qt 要求 app 对象与全部 Widget 同线程）。
 */

#include <gtest/gtest.h>

#include <QApplication>

#include <sdurws/ird/testkit/gtest/RecordListener.hpp>

/// 测试入口：构造 QApplication→初始化 gtest→安装报告监听器→执行用例。
int main(int argc, char** argv)
{
    // QApplication（Widgets 级）：GUI 用例的运行期前提。平台插件路径由
    // ctest 注册的 ENVIRONMENT（QT_QPA_PLATFORM_PLUGIN_PATH）或运行方
    // 环境提供（ird_gui 串行口径——本 main 不覆盖运行方的环境选择）。
    QApplication app(argc, argv);

    ::testing::InitGoogleTest(&argc, argv);
    // 静态存储期监听器（ird-test-report.json 与 gtest XML 并存——testkit
    // §7.2；--ird_report=<path> 指定报告路径，缺省工作目录）。
    sdurws::ird::testkit::installTestRecordListener(argc, argv);
    return RUN_ALL_TESTS();
}
