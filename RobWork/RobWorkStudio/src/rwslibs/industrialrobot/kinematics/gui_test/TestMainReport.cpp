/**
 * @file   TestMainReport.cpp
 * @brief  `sdurws_ird_kinematics_gui_test` 的自有测试入口——QApplication
 *         形态（真实 QWidget 事件探针的载体——KIN-P1 A07 回归起）。
 *
 * 设计依据：
 *   - units/kinematics.md §12（测试目标分层——本目标为 GUI 呈现层的
 *     自动化承载：真实 widget＋直接事件探针；与 QCoreApplication 级的
 *     sdurws_ird_kinematics_test 分工互补）；
 *   - 先例：ui/gui_test/TestMainReport.cpp（QApplication 形态＋testkit
 *     RecordListener 报告设施——ird-test-report.json 与 gtest XML 并存）；
 *   - units/testkit.md §6.7（GUI 测试单实例启动/QT_QPA_PLATFORM=windows/
 *     offscreen 禁用——执行纪律由运行方承载，本 main 不覆盖环境选择）。
 *
 * 背景（为什么需要本目标——KIN-P1 审查条目 A07 的回归形态）：
 *   目标输入的"标签随显示单位制＋提交逆换算回 SI"是真实 widget 行为
 *   （QLabel 文本＋QLineEdit 提交路径），QCoreApplication 级测试不构造
 *   QWidget（testkit §6.7 模型测试豁免行），无法复现"输入 0.1 被×1000
 *   提交成 100 m"的缺陷形态（审查 20261016 §3 A07）。本目标以
 *   QApplication 构造真实面板，经 QMetaObject::invokeMethod 直触槽函数，
 *   断言注入记录求解器收到的 SI 真值——事件到请求的数据流回归面。
 *
 * 线程模型：单线程入口（gtest 串行）；QApplication 在 main 线程构造并
 * 存活至 RUN_ALL_TESTS 结束（Qt 要求 app 对象与全部 Widget 同线程）。
 */

#include <gtest/gtest.h>

#include <QApplication>

#include <sdurws/ird/testkit/gtest/RecordListener.hpp>

/// 测试入口：构造 QApplication→初始化 gtest→安装报告监听器→执行用例。
int main(int argc, char** argv)
{
    // QApplication（Widgets 级）：真实 QWidget（KinematicsPanelWidget——
    // 表单/表格/按钮）的构造前提；平台插件由运行方环境提供
    // （QT_QPA_PLATFORM=windows——offscreen 禁用，testkit §6.7）。
    QApplication app(argc, argv);

    ::testing::InitGoogleTest(&argc, argv);
    // 静态存储期监听器（ird-test-report.json 与 gtest XML 并存——testkit
    // §7.2；--ird_report=<path> 指定报告路径，缺省工作目录；写盘失败只
    // 告警不掩盖 gtest 退出码）。
    sdurws::ird::testkit::installTestRecordListener(argc, argv);
    return RUN_ALL_TESTS();
}
