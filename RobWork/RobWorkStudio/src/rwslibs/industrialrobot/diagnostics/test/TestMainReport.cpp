/**
 * @file TestMainReport.cpp
 * @brief 诊断测试报告入口；直接复用既有 testkit 监听器，不另建报告转换器。
 *
 * 依据 AGENTS §4：本次真实文件回归需要 XML 与原生 JSON 并存。入口同时
 * 供单元/契约目标编译；仅测试侧依赖 testkit，不改变 diagnostics 产品依赖。
 */
#include <gtest/gtest.h>
#include <sdurws/ird/testkit/gtest/RecordListener.hpp>

/**
 * @brief 安装既有唯一报告监听器，再运行测试。
 * @param argc [in,out] 参数数量，由 gtest/testkit 摘除其参数。
 * @param argv [in,out] 进程持有的参数；支持 --ird_report 路径。
 * @return 原始 gtest 退出码；报告失败沿 testkit 原错误轨记录。
 */
int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    sdurws::ird::testkit::installTestRecordListener(argc, argv);
    return RUN_ALL_TESTS();
}
