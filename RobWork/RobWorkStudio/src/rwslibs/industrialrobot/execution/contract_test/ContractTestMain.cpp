/**
 * @file   ContractTestMain.cpp
 * @brief  `sdurws_ird_execution_contract_test` 的自有测试入口——子进程模式
 *         拦截（EX-RCV-1 真进程场景）＋安装 testkit TestRecordListener
 *         （ird-test-report.json 聚合落盘）。
 *
 * 设计依据：
 *   - units/execution.md §12 EX-T09 行（契约测试套件落地——TestProcessRunner
 *     按 TK-T11 触发接入 EX-RCV-1 真进程场景）、§11（报告留痕：gtest XML＋
 *     ird-test-report.json 并存）
 *   - units/testkit.md §7.3（installTestRecordListener 原文签名——测试 main
 *     自持两种形态之一）、§6.5（TestProcessRunner 消费——子进程半边经同一
 *     exe 双形态协议承载）
 *   - 先例：project/test/LinkageContractTest 的 main 拦截（PRJ-T15 同款——
 *     子进程分派必须先于 InitGoogleTest）＋project/test/TestMainReport.cpp
 *     （RecordListener 安装形态）
 *
 * 运行形态：
 *   1. 子进程模式：`--ird-ex-rcv-child --store=<path> --marker=<path>`——
 *      运行 RecoveryChild 停靠协议（gtest 之外，永不进入 RUN_ALL_TESTS）；
 *   2. 测试模式：初始化 gtest→安装报告监听器→执行全部用例。可用
 *      --ird_report=<path> 指定报告路径（缺省＝工作目录下
 *      ird-test-report.json）；该参数在 gtest 解析前被摘除。
 *
 * 线程模型：单线程入口（gtest 串行执行——测试目标纪律）。
 */

#include "RecoveryChild.hpp"

#include <gtest/gtest.h>

#include <sdurws/ird/testkit/gtest/RecordListener.hpp>

/// 测试入口：子进程分派→gtest 初始化→报告监听器→执行全部用例。
int main(int argc, char** argv)
{
    // 子进程模式拦截（先于 InitGoogleTest——gtest 不认识自有参数；PRJ-T15
    // 同款时序）。命中即以子进程身份运行并退出（退出码＝子进程语义）。
    {
        exrcv::ChildArgs childArgs;
        if (exrcv::tryParseChildArgs(argc, argv, childArgs)) {
            return exrcv::runRecoveryParkChild(childArgs);
        }
    }

    ::testing::InitGoogleTest(&argc, argv);
    // 监听器为静态存储期对象（gtest listeners 不接管所有权——官方
    // recommended 做法；进程生命周期与测试程序一致）。聚合报告在
    // OnTestProgramEnd 写盘；写盘失败只向 stderr 告警，不掩盖测试结果
    // （RecordListener 契约）。
    sdurws::ird::testkit::installTestRecordListener(argc, argv);
    return RUN_ALL_TESTS();
}
