/**
 * @file   TestMainReport.cpp
 * @brief  `sdurws_ird_modeling_test` 的自有测试入口——安装 testkit
 *         TestRecordListener（ird-test-report.json 用例级明细落盘）。
 *
 * 设计依据：
 *   - AGENTS.md §4.2（验证留痕——gtest XML＋ird-test-report.json 并存）
 *   - units/testkit.md §7.2/§7.3（机器可读测试结果与监听器安装原语）
 *   - 先例：io/test/TestMainReport.cpp（IO-T07 同款——gtest_main 固定
 *     入口无监听器安装点，IRD_TEST_INFO 写入的 TestRecord 在无监听器时
 *     是 no-op，因此以自有 main 替换 gtest_main；PRJ-T15 更早先例）
 *
 * T15 增量（WP-13-T15）：QCoreApplication 构造——插件呈现模型测试消费
 * ui 公共面（CommandInteractionBridge 的 Marshal 半区为 QObject 实现，
 * 事件投递需 Qt Core 事件系统；ui/test/TestMainReport.cpp 同款先例——
 * 模型测试豁免行：QCoreApplication 级不要求窗口系统，testkit §6.7）。
 * 链接面：Qt Core 经插件目标 PUBLIC 传染（sdurws_ird_modeling_plugin→Qt）。
 *
 * 线程模型：单线程入口（gtest 串行执行——测试目标纪律）；QCoreApplication
 * 即测试的"UI 线程"（PanelUiThreadGuard 的 owner＝主线程——插件面板
 * 装配语义）。
 */

#include <gtest/gtest.h>

#include <QCoreApplication>

#include <sdurws/ird/testkit/gtest/RecordListener.hpp>

/// 测试入口：构造 QCoreApplication→初始化 gtest→安装报告监听器→执行用例。
int main(int argc, char** argv)
{
    // QCoreApplication（非 QApplication）：模型测试形态的运行期标记——
    // Marshal 面需要事件系统，但窗口系统（Widgets GUI）本目标不消费
    // （V-30 GUI 用例仅登记流程不执行——契约 acceptance 3）。
    QCoreApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    // 监听器为静态存储期对象（gtest listeners 不接管所有权——官方
    // recommended 做法；进程生命周期与测试程序一致）。聚合报告在
    // OnTestProgramEnd 写盘（缺省＝进程工作目录下 ird-test-report.json，
    // 可用 --ird_report=<path> 指定——留痕脚本据此把用例级明细写入
    // traceability/builds/wp13-t02/）；写盘失败只向 stderr 告警，不掩盖
    // 测试结果（RecordListener 契约——gtest 退出码仍真实反映用例成败）。
    sdurws::ird::testkit::installTestRecordListener(argc, argv);
    return RUN_ALL_TESTS();
}
