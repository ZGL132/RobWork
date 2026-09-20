/**
 * @file   BuildPlacementTest.cpp
 * @brief  `sdurws_ird_ui_gui_test` 落位形态用例（UI-T02 acceptance 3/4 的
 *         gui 侧具名自证）——目标注册、ird_gui 串行标签、testkit_qt 消费。
 *
 * 设计依据：
 *   - units/ui.md §12.1（gui 目标行：Widgets/GUI 契约测试，LABELS ird_gui
 *     串行，链接 testkit＋testkit_qt）；
 *   - 任务契约 tasks/foundation/UI-T02.json acceptance 3（sdurws_ird_ui_gui_test
 *     按 DTB §5.5 注册）/ acceptance 4（testkit_qt 启用登记——本目标为其
 *     消费者）；
 *   - units/testkit.md §3.5/§10.1（_qt 目标只被 GUI/模型测试目标链接——
 *     消费者触发启用）。
 *
 * 形态变更登记（UI-T03）：落位期用例 GuiMainNoGuiStartup（钉"本版不启动
 * 任何 GUI 程序"——ui.md §12.2 第 8 条落位期纪律）按其自述的既定授权拆除
 * （"UI-T03+ 引入 GUI 用例时随本用例一并修订（届时删除本钉子并按 §12.2
 * 执行纪律留痕）"）；本目标现承载真实 GUI 用例（WorkbenchShellGuiTest.cpp），
 * 本文件保留目标注册/标签/消费形态的常驻自证。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace {

/// ui 单元树根（industrialrobot 目录——IRD_UI_UNIT_ROOT 注入）。
/// lexically_normal 规范化消除注入值尾部的 ".." 段（错误信息路径可读——
/// ui/test/BuildRedLineTest.cpp 同款口径）。
const fs::path& unitRoot()
{
    static const fs::path dir = fs::path{IRD_UI_UNIT_ROOT}.lexically_normal();
    return dir;
}

/// 全文读取；读失败显性失败（不留"读不到＝零命中"的假阳性通道）。
std::string readFile(const fs::path& file)
{
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        ADD_FAILURE() << "无法读取: " << file.string();
        return {};
    }
    return std::string{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

}  // namespace

/**
 * gui 目标落位形态（acceptance 3/4 具名证据）：CMake 侧注册齐备——
 * add_executable、ird_gui 串行标签、testkit_qt 消费者形态、ird_add_gtest
 * 报告目标。
 *
 * 说明：ird_gui 标签的"串行"语义由 CI/ctest 按 labels 过滤单实例调度实现
 * （testkit.md §6.7——不用 RUN_SERIAL 属性：设计文本钉住"串行标签"口径）；
 * 本用例核对标签登记在位。
 */
TEST(UiGuiPlacement, GuiTargetRegistration_UI_GUIBUILD)
{
    IRD_TEST_INFO("NFR-MNT-01", {}, std::nullopt);
    const std::string cmake = readFile(unitRoot() / "ui" / "CMakeLists.txt");
    // 目标声明在位（acceptance 3——三测试目标之一）。
    ASSERT_NE(cmake.find("add_executable(sdurws_ird_ui_gui_test"), std::string::npos)
        << "缺 sdurws_ird_ui_gui_test 注册";
    // ird_gui 串行标签（acceptance 3——"LABELS ird_gui 串行，不与模型/Meta
    // 测试合并命令"）。
    EXPECT_NE(cmake.find("PROPERTIES LABELS \"ird_gui\""), std::string::npos)
        << "缺 LABELS ird_gui 登记";
    // testkit_qt 消费者形态（acceptance 4——启用触发面之一；
    // testkit.md §3.5：_qt 只被 GUI/模型测试目标链接）。
    EXPECT_NE(cmake.find("sdurws_ird_testkit_qt"), std::string::npos)
        << "CMake 未登记 sdurws_ird_testkit_qt 消费";
    // add_test 经 ird_add_gtest 宏在位（DTB §5.5——add_test＋XML 报告目标）。
    EXPECT_NE(cmake.find("ird_add_gtest(sdurws_ird_ui_gui_test)"), std::string::npos)
        << "gui 目标未经 ird_add_gtest 注册（缺 add_test/XML 报告通道）";
}
