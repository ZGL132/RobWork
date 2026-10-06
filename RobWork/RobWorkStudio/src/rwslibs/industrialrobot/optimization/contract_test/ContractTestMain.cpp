/**
 * @file   ContractTestMain.cpp
 * @brief  `sdurws_ird_optimization_contract_test` 的自有测试入口——安装
 *         testkit TestRecordListener（ird-test-report.json 用例级明细
 *         落盘）。
 *
 * 设计依据：
 *   - AGENTS.md §4.2（验证留痕——gtest XML＋ird-test-report.json 并存）
 *   - units/testkit.md §7.2/§7.3（机器可读测试结果与监听器安装原语）
 *   - 先例：trajectory/contract_test/ContractTestMain.cpp 同款自有 main
 *     形态（contract_test 与 _test 同口径——dynamics/selection/workflow
 *     更早先例）
 *
 * 线程模型：单线程入口（gtest 串行执行——测试目标纪律）。零
 * QCoreApplication 构造（trajectory/dynamics/selection/workflow 落位批
 * 同款诚实登记）——本目标消费插件目标 sdurws_ird_optimization_plugin
 * 的装配门面，但 T02 门面面（自持描述符工厂＋源码扫描）零 Qt 类调用
 * （Qt 仅经链接面传染，为 WP-20-T10 面板批次预备的载体面）——无运行期
 * Qt 事件循环需求。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/testkit/gtest/RecordListener.hpp>

/// 测试入口：初始化 gtest→安装报告监听器→执行用例。
int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    // 监听器为静态存储期对象（gtest listeners 不接管所有权——官方
    // recommended 做法；进程生命周期与测试程序一致）。聚合报告在
    // OnTestProgramEnd 写盘（缺省＝进程工作目录下 ird-test-report.json，
    // 可用 --ird_report=<path> 指定——留痕脚本据此把用例级明细写入
    // traceability/builds/wp20-t02/）；写盘失败只向 stderr 告警，不掩盖
    // 测试结果（RecordListener 契约——gtest 退出码仍真实反映用例成败）。
    sdurws::ird::testkit::installTestRecordListener(argc, argv);
    return RUN_ALL_TESTS();
}
