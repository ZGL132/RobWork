/**
 * @file   BuildPlacementTest.cpp
 * @brief  `sdurws_ird_ui_gui_test` 落位期形态用例（UI-T02 acceptance 3/4 的
 *         gui 侧具名自证）——目标注册、ird_gui 串行标签、testkit_qt 消费、
 *         "本版不启动 GUI"纪律钉住。
 *
 * 设计依据：
 *   - units/ui.md §12.1（gui 目标行：Widgets/GUI 契约测试，LABELS ird_gui
 *     串行，链接 testkit＋testkit_qt）、§12.2 第 8 条（本版不启动任何 GUI
 *     程序——GUI 用例"已设计、未执行"，不得记载为通过）；
 *   - 任务契约 tasks/foundation/UI-T02.json acceptance 3（sdurws_ird_ui_gui_test
 *     按 DTB §5.5 注册）/ acceptance 4（testkit_qt 启用登记——本目标为其
 *     消费者）；落位期允许最小构建冒烟用例（父 CMakeLists 既有约定，
 *     EX-T01 同款口径）；
 *   - units/testkit.md §3.5/§10.1（_qt 目标只被 GUI/模型测试目标链接——
 *     消费者触发启用）。
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

/// 剥离行注释与块注释后再做行为判定（本用例扫描对象 TestMainReport.cpp
/// 的文件头注释恰好要"提及"QApplication 以说明为何不构造它——注释散文
/// 中的字样属文档而非行为，与 ird_gates R-4 扫描 F-011 消账同款口径；
/// UI-T02 首跑实测注释命中，以此修正）。实现与 ui/test/BuildRedLineTest.cpp
/// 的同名声明的语义一致（各测试 TU 自持、零共享头——测试设施不跨目标
/// 泄露，T-1 同款纪律）。
std::string stripComments(const std::string& src)
{
    std::string out;
    out.reserve(src.size());
    bool inLineComment = false;
    bool inBlockComment = false;
    bool inString = false;
    for (std::size_t i = 0; i < src.size(); ++i) {
        const char c = src[i];
        const char next = (i + 1 < src.size()) ? src[i + 1] : '\0';
        if (inLineComment) {
            if (c == '\n') { inLineComment = false; out.push_back(c); }
            continue;
        }
        if (inBlockComment) {
            if (c == '*' && next == '/') { inBlockComment = false; ++i; out.push_back(' '); }
            continue;
        }
        if (inString) {
            out.push_back(c);
            if (c == '\\' && next != '\0') { out.push_back(next); ++i; }
            else if (c == '"') { inString = false; }
            continue;
        }
        if (c == '/' && next == '/') { inLineComment = true; continue; }
        if (c == '/' && next == '*') { inBlockComment = true; ++i; out.push_back(' '); continue; }
        if (c == '"') { inString = true; out.push_back(c); continue; }
        out.push_back(c);
    }
    return out;
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

/**
 * "本版不启动 GUI"纪律钉住（ui.md §12.2 第 8 条）：落位期 gui_test 的 main
 * 不构造 QApplication/QGuiApplication（GUI 用例未落地，不得以"GUI 冒烟
 * 通过"虚化环境证据——TestMainReport.cpp 文件头设计依据的常驻自证形态）。
 * UI-T03+ 引入 GUI 用例时随本用例一并修订（届时删除本钉子并按 §12.2
 * 执行纪律留痕）。
 */
TEST(UiGuiPlacement, GuiMainNoGuiStartup_UI_GUIBUILD)
{
    IRD_TEST_INFO("NFR-MNT-01", {}, std::nullopt);
    const std::string main = stripComments(
        readFile(unitRoot() / "ui" / "gui_test" / "TestMainReport.cpp"));
    // 落位期形态：main 零 QApplication/QGuiApplication 构造（注释剥离后
    // 判定——文件头注释"提及"该类型以说明设计依据，属文档非行为）。
    // 含 QCoreApplication 字符串的 include 亦为零（core app 构造同理不入
    // 落位期 gui_test 面；stray 子串由两模式并查覆盖）。
    EXPECT_EQ(main.find("QApplication"), std::string::npos)
        << "落位期 gui_test main 出现 QApplication（违反 ui.md §12.2 第 8 条"
           "——本版不启动任何 GUI 程序）";
    EXPECT_EQ(main.find("QGuiApplication"), std::string::npos)
        << "落位期 gui_test main 出现 QGuiApplication（同上）";
    EXPECT_EQ(main.find("QCoreApplication"), std::string::npos)
        << "落位期 gui_test main 出现 QCoreApplication（同上——GUI 用例落地"
           "前不构造任何 Qt 应用对象）";
}
