/**
 * @file   TestMainReport.cpp
 * @brief  `sdurws_ird_ui_gui_test` 的自有测试入口——QApplication 形态
 *         （UI-T03 起承载真实 GUI 用例）＋安装 testkit TestRecordListener。
 *
 * 设计依据：
 *   - units/ui.md §12.1（sdurws_ird_ui_gui_test＝Widgets/GUI 契约测试，
 *     ctest LABELS ird_gui 串行）、§12.2（Windows GUI 测试执行流程）；
 *   - units/testkit.md §6.7（GUI 测试单实例启动/QT_QPA_PLATFORM=windows/
 *     offscreen 禁用）；§7.2/§7.3（ird-test-report.json 与 gtest XML 并存）；
 *   - 任务契约 tasks/foundation/UI-T03.json verify 行（sdurws_ird_ui_gui_test
 *     为本任务两条 verify 目标之一——UI-WB/SES 用例的执行载体）。
 *
 * 形态变更登记（UI-T03）：UI-T02 落位期本 main 不构造任何 Qt 应用对象
 * （当时"本版不启动任何 GUI 程序"——§12.2 第 8 条，其用例钉子按既定授权
 * 随本任务拆除）；UI-T03 引入真实 GUI 用例（UI-WB-1/2/3、UI-SES-1）后，
 * 本 main 升级为 QApplication 形态。执行纪律按 §12.2 全文由运行方承载：
 * VS x64 环境、QT_QPA_PLATFORM=windows（本 main 不覆盖运行方的环境选择）、
 * 绝对路径单实例启动、不与模型/Meta 测试合并命令。
 *
 * 线程模型：单线程入口（gtest 串行纪律；GUI 目标在 CI 中以 ird_gui 标签
 * 单实例串行调度——testkit.md §6.7）。QApplication 在 main 线程构造并
 * 存活至 RUN_ALL_TESTS 结束（Qt 要求 app 对象与全部 Widget 同线程）。
 */

#include <gtest/gtest.h>

#include <QApplication>

#include <sdurws/ird/testkit/gtest/RecordListener.hpp>

/// 测试入口：构造 QApplication→初始化 gtest→安装报告监听器→执行用例。
int main(int argc, char** argv)
{
    // QApplication（Widgets 级）：GUI 用例的运行期前提——五区 Dock/布局
    // 记忆/首页控件全部需要 Widgets 事件循环与平台插件（§12.2 要求
    // QT_QPA_PLATFORM=windows，由运行方环境提供；此处不设置/不覆盖）。
    QApplication app(argc, argv);

    ::testing::InitGoogleTest(&argc, argv);
    // 静态存储期监听器（ird-test-report.json 与 gtest XML 并存——testkit
    // §7.2；--ird_report=<path> 指定报告路径，缺省工作目录）。
    sdurws::ird::testkit::installTestRecordListener(argc, argv);
    return RUN_ALL_TESTS();
}
