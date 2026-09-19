/**
 * @file   TestMainReport.cpp
 * @brief  `sdurws_ird_ui_contract_test` 的自有测试入口——QCoreApplication 级
 *         headless 形态＋安装 testkit TestRecordListener。
 *
 * 设计依据：
 *   - units/ui.md §3.1（sdurws_ird_ui_contract_test：跨单元契约测试，无界面；
 *     带 Qt 事件循环 headless 用 QCoreApplication）、§12.1（第二层测试目标）；
 *   - units/testkit.md §7.2/§7.3（报告聚合与监听器安装——与 gtest XML 并存）；
 *   - 先例：ui/test/TestMainReport.cpp（UI-T02 同批）、execution/test/
 *     TestMainReport.cpp（EX-T09 形态源）。
 *
 * 背景说明：与 ui/test/TestMainReport.cpp 同形态（QCoreApplication＋监听器
 * 安装），差异仅在宿主目标——契约测试当前为构建图边界契约（落位期允许的
 * 最小形态），与 project/execution 的真实协作契约随 UI-T14 落地（O-31 形态
 * 裁决范围，本任务不私裁）。线程模型：单线程入口（gtest 串行纪律）；事件
 * 循环不启动（不调 exec）。
 */

#include <gtest/gtest.h>

#include <QCoreApplication>

#include <sdurws/ird/testkit/gtest/RecordListener.hpp>

/// 测试入口：构造 QCoreApplication→初始化 gtest→安装报告监听器→执行用例。
int main(int argc, char** argv)
{
    // QCoreApplication：headless 契约测试形态（无需 GUI 平台插件——ui.md
    // §3.1 该目标行的运行环境约定）。
    QCoreApplication app(argc, argv);

    ::testing::InitGoogleTest(&argc, argv);
    sdurws::ird::testkit::installTestRecordListener(argc, argv);
    return RUN_ALL_TESTS();
}
