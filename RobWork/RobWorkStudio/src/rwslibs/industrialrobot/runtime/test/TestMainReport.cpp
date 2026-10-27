/**
 * @file   TestMainReport.cpp
 * @brief  `sdurws_ird_runtime_test` 的自有测试入口——安装 testkit
 *         TestRecordListener（ird-test-report.json 用例级明细落盘）。
 *
 * 设计依据：
 *   - AGENTS.md §4.2（验证留痕——gtest XML＋ird-test-report.json 并存；
 *     主线功能审计 MA-09——此前本目标用 gtest_main 固定入口，无监听器
 *     安装点，--ird_report 传了也不产报告，留痕三件套缺件）
 *   - units/testkit.md §7.2/§7.3（机器可读测试结果与监听器安装原语）
 *   - 先例：kinematics/test/TestMainReport.cpp（WP-15-T12）；本单元为
 *     L2 计算单元（红线：零 Qt）——**不构造 QCoreApplication**，纯
 *     gtest 入口（与 kinematics 形态的唯一差异，豁免行如实登记）
 *
 * 线程模型：单线程入口（gtest 串行执行——测试目标纪律）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/testkit/gtest/RecordListener.hpp>

/// 测试入口：初始化 gtest→安装报告监听器→执行用例。
int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    // 监听器所有权按 gtest 公开契约移交：Append 接管并在测试程序结束时释放
    // （gtest/gtest.h TestEventListeners::Append 注释；F-958 修复后为堆对象，
    // 静态/栈对象地址交 Append 属非法 delete）。聚合报告
    // 在 OnTestProgramEnd 写盘（--ird_report=<path> 指定，缺省＝工作目录
    // 下 ird-test-report.json）；写盘失败只向 stderr 告警，不掩盖测试
    // 结果（RecordListener 契约——gtest 退出码仍真实反映用例成败）。
    sdurws::ird::testkit::installTestRecordListener(argc, argv);
    return RUN_ALL_TESTS();
}
