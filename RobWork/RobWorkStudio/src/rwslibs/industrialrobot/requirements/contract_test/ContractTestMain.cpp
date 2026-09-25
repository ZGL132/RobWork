/**
 * @file   ContractTestMain.cpp
 * @brief  `sdurws_ird_requirements_contract_test` 的自有测试入口——安装
 *         testkit TestRecordListener（契约面用例级明细与 gtest XML 并存）。
 *
 * 设计依据：
 *   - 先例：modeling/contract_test/ContractTestMain.cpp（WP-13-T02 同款——
 *     契约测试目标与单元测试目标同接报告设施，T-1 允许形态：仅测试
 *     目标链 testkit）
 *
 * 线程模型：单线程入口（gtest 串行执行——测试目标纪律）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/testkit/gtest/RecordListener.hpp>

/// 测试入口：初始化 gtest→安装报告监听器→执行全部用例。
int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    // 监听器契约与 _test 目标入口同款（TestMainReport.cpp 注）——写盘
    // 失败只告警不掩盖结果，退出码真实反映用例成败。
    sdurws::ird::testkit::installTestRecordListener(argc, argv);
    return RUN_ALL_TESTS();
}
