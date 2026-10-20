/**
 * @file TestMainReport.cpp
 * @brief core测试的原生报告入口——MA-09补扫发现65条XML记录缺少配套JSON。
 *
 * 设计依据：AGENTS.md §4.2、units/testkit.md §7.2/7.3；记录格式与结果判定
 * 仍由testkit唯一管理，本文件只安装一次监听器。core产品库仍不依赖testkit
 * 或Qt；依赖只属于测试可执行文件。主线程串行运行，不启动GUI或后台线程。
 */
#include <gtest/gtest.h>
#include <sdurws/ird/testkit/gtest/RecordListener.hpp>

/**
 * @brief 初始化gtest并安装唯一原生报告监听器，返回真实用例执行退出码。
 * @param argc [in,out] 参数数量；gtest和testkit摘除自身参数。
 * @param argv [in,out] 进程持有的参数，不接管字符内存；支持--ird_report路径。
 * @return gtest退出码；报告写盘失败按testkit既有环境错误约定向stderr记录。
 */
int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    sdurws::ird::testkit::installTestRecordListener(argc, argv);
    return RUN_ALL_TESTS();
}
