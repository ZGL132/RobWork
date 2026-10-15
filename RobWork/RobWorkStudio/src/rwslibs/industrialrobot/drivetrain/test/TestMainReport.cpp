/**
 * @file   TestMainReport.cpp
 * @brief  `sdurws_ird_drivetrain_test` 的自有测试入口——安装 testkit
 *         TestRecordListener（ird-test-report.json 用例级明细落盘）。
 *
 * 设计依据：
 *   - AGENTS.md §4.2（验证留痕——gtest XML＋ird-test-report.json 并存）
 *   - units/testkit.md §7.2/§7.3（机器可读测试结果与监听器安装原语）
 *   - 先例：workflow/test/TestMainReport.cpp（WP-22-T02 同款——gtest_main
 *     固定入口无监听器安装点，IRD_TEST_INFO 写入的 TestRecord 在无监听器
 *     时是 no-op，因此以自有 main 替换 gtest_main）
 *
 * 线程模型：单线程入口（gtest 串行执行——测试目标纪律）。
 *
 * 与 requirements/kinematics 先例的差异说明（诚实登记，非遗漏）：本入口
 * **不构造 QCoreApplication**——drivetrain T02 测试面（登记表＋红线扫描）
 * 零 Qt 消费，测试目标链接面亦无 Qt 目标（本单元无 plugin 目标——卡
 * §2.3，无 Qt 例外载体，后续批次同样零 Qt——L2 共享计算服务红线）；若
 * 未来出现 Qt 类型面用例（不预期），照 requirements T08 增量先例补
 * QCoreApplication 构造。
 *
 * Debug 构建断言通道重定向（F-795 回归批引入，2026-10-15）：①F-795 回归
 * 用例 GetStringStreamEndEmptyGuard_F_795 的"修复前必红"判据依赖 MSVC
 * Debug 配置的容器断言（vector operator[] 越界取址），默认报告模式含窗口
 * 弹窗（_CRTDBG_MODE_WNDW），无头/桌面环境会阻塞等待人工点击——F-728/
 * F-789 ctest 挂起防线同族纪律，必须在入口统一消解；②实测该 exe 在
 * Debug 配置的退出期存在与用例无关的既有 CRT 堆断言噪声（零用例运行亦
 * 触发——filter 空集运行复现，见 _e2-evidence/debug-teardown-attrib-
 * diagcodes.log），同样会弹窗挂起。两类断言经本入口统一路由到 stderr：
 * 无头可观测（日志留证）、不弹窗、不改变退出码语义。Release 构建零影响
 * （整段 #if 编译出）。噪声本体为既有现象、与 F-795 修复无关，诊断与消
 * 除不在本批范围（本批仅保证其不阻塞运行）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/testkit/gtest/RecordListener.hpp>

#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h> // _CrtSetReportMode/_CrtSetReportFile（断言报告通道）
#include <cstdlib>  // _set_error_mode（assert() 输出目标——stderr）
#endif

/// 测试入口：初始化 gtest→安装报告监听器→执行用例。
int main(int argc, char** argv)
{
#if defined(_MSC_VER) && defined(_DEBUG)
    // CRT 断言通道重定向（仅 MSVC Debug 构建编译进——Release 零影响）：
    // WARN/ERROR/ASSERT 三类报告全部路由到 stderr、不启用窗口模式
    // （_CRTDBG_MODE_WNDW）。背景见文件头"F-795"段——容器断言红证据与
    // 退出期既有堆断言噪声都经此无头化；放在 main 最前，保证其后任何
    // 断言（含用例执行与进程收尾）都不弹窗。
    _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _set_error_mode(_OUT_TO_STDERR);
#endif

    ::testing::InitGoogleTest(&argc, argv);
    // 监听器为静态存储期对象（gtest listeners 不接管所有权——官方
    // recommended 做法；进程生命周期与测试程序一致）。聚合报告在
    // OnTestProgramEnd 写盘（缺省＝进程工作目录下 ird-test-report.json，
    // 可用 --ird_report=<path> 指定——留痕脚本据此把用例级明细写入
    // traceability/builds/wp18-t02/）；写盘失败只向 stderr 告警，不掩盖
    // 测试结果（RecordListener 契约——gtest 退出码仍真实反映用例成败）。
    sdurws::ird::testkit::installTestRecordListener(argc, argv);
    return RUN_ALL_TESTS();
}
