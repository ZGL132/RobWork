/**
 * @file   main.cpp
 * @brief  监听器所有权定向测试子进程（sdurws_ird_testkit_lsnchild）——F-958。
 *
 * 设计依据：
 *   - units/testkit.md §7.3（installTestRecordListener 为生产安装入口）、§3.6
 *     （tools/ 不进产品安装面）；F-958（Append 所有权契约错配——本子进程是
 *     ListenerOwnershipTest 的隔离子进程载体）
 *   - 先例：tools/prochelper/main.cpp（TK-T11——"行为完全受测试控制"的子进程
 *     载体；CMake 对齐输出目录，用例零 PATH/工作目录依赖）
 *
 * 背景说明（为什么需要独立子进程而不是在 sdurws_ird_testkit_test 内造样本）：
 *   预期失败/显式跳过样本会改变进程退出码与报告聚合，必须与正常测试驱动隔离
 *   （失败子进程的非零退出码由父测试明确验证，不污染父进程自身的报告）；
 *   installTestRecordListener 契约为每进程一次，所有权移交的端到端证明
 *   （事件接收→聚合落盘→进程正常退出）也必须在真实进程退出路径上观察。
 *
 * 参数与模式（父测试经 ProcessSpec.args 传入；均为 ASCII/路径）：
 *   --gtest_filter=LsnChild.*      gtest 原生过滤——父测试据此选择单一用例
 *                                  （Passes→passed、Fails→failed、Skips→skipped）
 *   --ird_report=<path>            生产安装入口的自维护参数（原样透传，由
 *                                  installTestRecordListener 摘除）
 *   --lsn_mode=legacy              旧实现反例载体（见下）；仅独验/取证时人工
 *                                  执行，不进入常规父测试断言
 *
 * legacy 反例载体说明：复刻修复前的安装接缝（函数内 static 监听器对象地址
 * 交给拥有型 TestEventListeners::Append——F-958 缺陷本体，非产品 fallback、
 * 非第二报告路径；生产接缝已按 gtest 公开契约改为堆对象移交）。非法 delete
 * 属未定义行为，进程退出表现不确定（可能无可见症状、CRT 诊断或崩溃）——
 * 取证须如实记录观察结果，不得预设/伪造"红测"。
 *
 * 线程/并发：单线程顺序执行；gtest 串行纪律。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/testkit/gtest/RecordListener.hpp>

#include <string>

/**
 * @brief 摘除本辅助进程自有参数 --lsn_mode=<value>。
 *
 * @param argc [in,out] 参数计数；摘除后同步缩减
 * @param argv [in,out] 参数表；被摘除项置 nullptr（与安装函数摘除风格一致）
 * @return 模式值（无该参数时为空串＝默认生产行为）
 */
std::string takeLsnMode(int& argc, char** argv)
{
    std::string mode;
    int newArgc = 0;
    for (int i = 0; i < argc; ++i) {
        const std::string arg = argv[i] != nullptr ? argv[i] : "";
        const std::string prefix = "--lsn_mode=";
        if (arg.rfind(prefix, 0) == 0) {
            mode = arg.substr(prefix.size());
        } else {
            argv[newArgc++] = argv[i];
        }
    }
    for (int i = newArgc; i < argc; ++i) { argv[i] = nullptr; }
    argc = newArgc;
    return mode;
}

/// 正常路径样本：报告应聚合为 passed 且进程退出码 0。
TEST(LsnChild, Passes)
{
    EXPECT_EQ(1, 1);
}

/// 预期失败样本：gtest 判失败、进程退出码非 0（由父测试明确验证）。
TEST(LsnChild, Fails)
{
    EXPECT_EQ(1, 2) << "lsnchild 预期失败样本（F-958 失败态载体）";
}

/// 显式跳过样本：GTEST_SKIP→报告 skipped、不影响退出码（退出 0）。
TEST(LsnChild, Skips)
{
    GTEST_SKIP() << "lsnchild 显式跳过样本（F-958 skip 态载体）";
}

/**
 * @brief 子进程入口：生产安装入口（或 legacy 反例）→ gtest 初始化 → 全量运行。
 * @param argc [in,out] 参数数量（installTestRecordListener 摘除 --ird_report）
 * @param argv [in,out] 进程参数
 * @return gtest 退出码（有失败用例＝非 0；父测试按 §6.5 四态语义裁决）
 */
int main(int argc, char** argv)
{
    const std::string mode = takeLsnMode(argc, argv);
    if (mode == "legacy") {
        // 旧实现反例（F-958 本体复刻，仅取证用）：静态对象地址交拥有型 Append。
        static ::sdurws::ird::testkit::TestRecordListener legacyListener{
            "ird-test-report.json"};
        ::testing::UnitTest::GetInstance()->listeners().Append(&legacyListener);
    } else {
        // 生产安装入口（与各单元测试 main 同一接缝；--ird_report 在此摘除）。
        ::sdurws::ird::testkit::installTestRecordListener(argc, argv);
    }
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
