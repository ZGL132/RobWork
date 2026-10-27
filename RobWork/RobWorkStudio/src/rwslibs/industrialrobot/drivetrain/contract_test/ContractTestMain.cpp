/**
 * @file   ContractTestMain.cpp
 * @brief  `sdurws_ird_drivetrain_contract_test` 的自有测试入口——安装
 *         testkit TestRecordListener（ird-test-report.json 用例级明细
 *         落盘）。
 *
 * 设计依据：
 *   - AGENTS.md §4.2（验证留痕——gtest XML＋ird-test-report.json 并存）
 *   - units/testkit.md §7.2/§7.3（机器可读测试结果与监听器安装原语）
 *   - 先例：kinematics/test/TestMainReport.cpp 同款自有 main 形态
 *     （contract_test 与 _test 同口径——workflow/contract_test/
 *     ContractTestMain.cpp 更近先例）
 *
 * 线程模型：单线程入口（gtest 串行执行——测试目标纪律）。零
 * QCoreApplication 构造（workflow T02 同款诚实登记）——drivetrain 零 Qt
 * 消费（L2 共享计算服务，无 plugin 目标），契约测试面（构建图边界扫描）
 * 无 Qt 类型。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/testkit/gtest/RecordListener.hpp>

/// 测试入口：初始化 gtest→安装报告监听器→执行用例。
int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    // 监听器所有权按 gtest 公开契约移交：Append 接管并在测试程序结束时释放
    // （gtest/gtest.h TestEventListeners::Append 注释；F-958 修复后为堆对象，
    // 静态/栈对象地址交 Append 属非法 delete）。聚合报告在
    // OnTestProgramEnd 写盘（缺省＝进程工作目录下 ird-test-report.json，
    // 可用 --ird_report=<path> 指定——留痕脚本据此把用例级明细写入
    // traceability/builds/wp18-t02/）；写盘失败只向 stderr 告警，不掩盖
    // 测试结果（RecordListener 契约——gtest 退出码仍真实反映用例成败）。
    sdurws::ird::testkit::installTestRecordListener(argc, argv);
    return RUN_ALL_TESTS();
}
