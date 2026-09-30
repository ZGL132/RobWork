/**
 * @file   TestMainReport.cpp
 * @brief  `sdurws_ird_requirements_gui_test` 的自有测试入口——QApplication
 *         形态＋安装 testkit TestRecordListener（UI-T29 补建目标）。
 *
 * 设计依据：
 *   - 任务契约 tasks/foundation/UI-T29.json acceptance（requirements 面
 *     板 gui_test 具名用例——会话数据面/编辑生效/预检随编辑刷新的执行
 *     载体）；units/requirements.md §9.8；
 *   - testkit §6.7（GUI 单实例串行）、§7.2/§7.3（ird-test-report.json 与
 *     gtest XML 并存）；
 *   - 先例：ui/modeling 两域 gui_test 的 TestMainReport 同款形态。
 *
 * 线程模型：单线程入口（gtest 串行；QApplication main 线程构造并存活
 * 至 RUN_ALL_TESTS 结束——Qt 要求 app 对象与全部 Widget 同线程）。
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
