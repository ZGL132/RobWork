/**
 * @file   TestMainReport.cpp
 * @brief  `sdurws_ird_ui_gui_test` 的自有测试入口——落位期不启动任何 GUI
 *         程序（§12.2 纪律），仅安装 testkit TestRecordListener。
 *
 * 设计依据：
 *   - units/ui.md §12.1（sdurws_ird_ui_gui_test＝Widgets/GUI 契约测试，ctest
 *     LABELS ird_gui 串行）、§12.2（Windows GUI 测试执行流程——第 8 条
 *     "本节为流程设计，本版不启动任何 GUI 程序：用例全部'已设计、未执行'"）；
 *   - units/testkit.md §6.7（GUI 测试单实例启动/QT_QPA_PLATFORM=windows/
 *     offscreen 禁用——GUI 用例随 UI-T03+/UI-T14 落地时遵守，见下方注记）；
 *   - 任务契约 tasks/foundation/UI-T02.json acceptance 3（gui 目标注册，
 *     "不与模型/Meta 测试合并命令"）。
 *
 * 背景说明（为什么本 main 不构造 QApplication）：GUI 用例（五区创建/布局
 * 恢复/对话框/面板导航）尚未落地；在无 GUI 用例的目标里构造 QApplication
 * 会让"冒烟通过"虚化为"GUI 环境可用"的假证据，且使本目标在 headless 环境
 * 依赖平台插件行为（§12.2 禁 offscreen、要求 VS x64 环境＋windows 平台
 * 插件）。落位期用例只做构建形态断言（BuildPlacementTest.cpp——零 Qt 事件
 * 循环、零平台插件依赖）；UI-T03+ 引入 GUI 用例时，本 main 按其用例需要
 * 升级为 QApplication 形态并同步修订用例（届时执行纪律按 §12.2 全文执行）。
 *
 * 线程模型：单线程入口（gtest 串行纪律；GUI 目标未来在 CI 中以 ird_gui
 * 标签单实例串行调度——testkit.md §6.7）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/testkit/gtest/RecordListener.hpp>

/// 测试入口：初始化 gtest→安装报告监听器→执行用例（不构造任何 QApplication）。
int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    // 静态存储期监听器（ird-test-report.json 与 gtest XML 并存——testkit §7.2；
    // --ird_report=<path> 指定报告路径，缺省工作目录）。
    sdurws::ird::testkit::installTestRecordListener(argc, argv);
    return RUN_ALL_TESTS();
}
