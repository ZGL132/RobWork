/**
 * @file   TestMain.cpp
 * @brief  testkit 自身测试目标的自持 main（DTB §5.5"测试 main 自持"的
 *         RecordListener 适配形态）。
 *
 * 设计依据：units/testkit.md §7.3（installTestRecordListener——消费方 main
 * 或 gtest main 替换处调用一次）；任务契约 tasks/foundation/TK-T10.json。
 *
 * 说明：本 main 使 sdurws_ird_testkit_test 本身成为 RecordListener 的
 * 端到端载体——每次运行聚合产出 ird-test-report.json（与 gtest XML 并存，
 * §7.2），CI/验收可直接消费该文件核对六类 outcome 聚合。可传
 * --ird_report=<path> 指定落盘位置（缺省＝进程工作目录）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/testkit/gtest/RecordListener.hpp>

int main(int argc, char** argv)
{
    // 第一步：安装报告监听（摘除自维护参数 --ird_report=，再交 gtest 解析）。
    ::sdurws::ird::testkit::installTestRecordListener(argc, argv);
    // 第二步：gtest 初始化与全量运行（OnTestProgramEnd 聚合写盘）。
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
