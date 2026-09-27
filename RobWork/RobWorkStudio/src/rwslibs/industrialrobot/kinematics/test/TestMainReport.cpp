/**
 * @file   TestMainReport.cpp
 * @brief  `sdurws_ird_kinematics_test` 的自有测试入口——安装 testkit
 *         TestRecordListener（ird-test-report.json 用例级明细落盘）＋
 *         QCoreApplication 构造（WP-15-T12 增量）。
 *
 * 设计依据：
 *   - AGENTS.md §4.2（验证留痕——gtest XML＋ird-test-report.json 并存）
 *   - units/testkit.md §7.2/§7.3（机器可读测试结果与监听器安装原语）
 *   - 先例：modeling/test/TestMainReport.cpp（WP-13-T15 T15 增量——插件
 *     呈现模型测试消费 Qt 类型面所需的运行期前提）；requirements 同款
 *     自有 main 形态（gtest_main 固定入口无监听器安装点）
 *
 * 线程模型：单线程入口（gtest 串行执行——测试目标纪律）；QCoreApplication
 * 在 main 线程构造、全程序存活。模型测试豁免行：QCoreApplication 级不
 * 要求窗口系统（testkit §6.7）——PluginPanelTest（WP-15-T12）的界面链路
 * 用例在无 GUI 环境可执行；GUI 呈现用例另行以 envUnavailable 登记。
 */

#include <gtest/gtest.h>

#include <QCoreApplication>

#include <sdurws/ird/testkit/gtest/RecordListener.hpp>

/// 测试入口：构造 QCoreApplication→初始化 gtest→安装报告监听器→执行用例。
int main(int argc, char** argv)
{
    // QCoreApplication（非 QApplication）：模型测试形态的运行期标记——
    // Qt 类型面（QKeySequence 等）的构造前提，不启动事件循环、不要求
    // 窗口系统（modeling T15 先例同款口径）。
    QCoreApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    // 监听器为静态存储期对象（gtest listeners 不接管所有权——官方
    // recommended 做法；进程生命周期与测试程序一致）。聚合报告在
    // OnTestProgramEnd 写盘（缺省＝进程工作目录下 ird-test-report.json，
    // 可用 --ird_report=<path> 指定——留痕脚本据此把用例级明细写入
    // traceability/builds/wp15-t12/）；写盘失败只向 stderr 告警，不掩盖
    // 测试结果（RecordListener 契约——gtest 退出码仍真实反映用例成败）。
    sdurws::ird::testkit::installTestRecordListener(argc, argv);
    return RUN_ALL_TESTS();
}
