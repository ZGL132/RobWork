/**
 * @file   LinkageContractTest.cpp
 * @brief  ui 构建图边界契约用例组（UI-T02 落位期 `_contract_test` 分工面）
 *         ——ARCH §3.5 登记边、T-1 测试侧链接形态、三测试目标与 ctest 标签
 *         登记。
 *
 * 设计依据：
 *   - units/ui.md §3.1（依赖边"ui → core, diagnostics"；project/execution/
 *     evidence/policy/runtime 对 ui 零编译依赖——协作一律经运行时注入）、
 *     §3.4（测试目标分工：`_contract_test`＝跨单元契约面——EX-T01 同款
 *     "构建图边界契约"落位形态）、§12.1（三目标与标签）、§12.2（GUI 测试
 *     执行纪律）；
 *   - 任务契约 tasks/foundation/UI-T02.json acceptance 3（三目标按 DTB §5.5
 *     注册，LABELS ird/ird_gui）/ acceptance 4（testkit_qt 启用登记——gui
 *     目标即其消费者）/ acceptance 5（O-31——不创建指向 project/evidence/
 *     execution/policy/runtime 的链接）；
 *   - units/testkit.md §2.4 T-1（产品目标禁链 testkit；测试目标允许形态）、
 *     §3.5（testkit_qt 只被 GUI/模型测试目标链接）；
 *   - 先例：execution/contract_test/LinkageContractTest.cpp（EX-T01 构建图
 *     边界契约同款——CMakeLists 文本扫描，运行期"可链接"半区由目标链接
 *     关系本身承载）。
 *
 * 为什么扫描 CMakeLists 文本而非查询构建系统：落位期的链接契约权威登记在
 * CMake 脚本文本（ird_gates 亦以其为解析对象）；本用例对同一数据源做独立
 * 复核，构成"门禁判定＋用例自证"的双保险（EX-T01 同款设计）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

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

/// 截取 target_link_libraries(<target> 起始的括号块文本（找不到返回空串）。
/// 目标名后带一个空白字符再匹配：防止前缀误吸——"sdurws_ird_ui" 是
/// "sdurws_ird_ui_test" 的前缀，不带定界符的 find 会命中错误块（本单元
/// 目标名族共享前缀，必须以词边界区分）。本仓库链接声明形态统一为
/// "target_link_libraries(<目标> <可见性>…)"，目标名后必为空格。
/// CMake 命令块以 '(' 开 ')' 止（本仓库风格无嵌套括号——ird_gates 同前提），
/// 文本级截取即可覆盖多行链接声明。
std::string extractLinkBlock(const std::string& cmakeText, const std::string& target)
{
    const std::string key = "target_link_libraries(" + target + " ";
    const std::size_t begin = cmakeText.find(key);
    if (begin == std::string::npos) { return {}; }
    const std::size_t openParen = cmakeText.find('(', begin);
    const std::size_t end = cmakeText.find(')', openParen);
    if (openParen == std::string::npos || end == std::string::npos) { return {}; }
    return cmakeText.substr(begin, end - begin + 1);
}

}  // namespace

/**
 * ARCH §3.5 登记边契约（acceptance 5 具名证据）：sdurws_ird_ui 的链接声明
 * 仅含 core/diagnostics 两条登记边＋Qt 三件套；任何其他 ird 单元目标
 * （含 project/evidence/execution/policy/runtime 与业务域）零出现。
 *
 * O-31 边界：对 C-3/4/5/7/8/10/11 协作单元的链接形态归架构所有者裁决
 * （DTB §4.2 登记未决），本用例钉住"落位零表外边"现状，不构成消账。
 * 产品目标零 testkit（T-1）在本块一并核对（testkit 出现即越界）。
 */
TEST(UiLinkage, ArchEdgesCoreDiagnosticsOnly_UI_CTR)
{
    IRD_TEST_INFO("NFR-MNT-01", {}, std::nullopt);
    const std::string cmake = readFile(unitRoot() / "ui" / "CMakeLists.txt");
    const std::string productLink = extractLinkBlock(cmake, "sdurws_ird_ui");
    ASSERT_FALSE(productLink.empty()) << "未找到 sdurws_ird_ui 的链接声明块";
    // 两条登记边＋Qt 三件套必须在位（正向：例外承载面齐备——acceptance 1）。
    EXPECT_NE(productLink.find("sdurws_ird_core"), std::string::npos)
        << "缺 core 登记边（ARCH §3.5 ui->core）";
    EXPECT_NE(productLink.find("sdurws_ird_diagnostics"), std::string::npos)
        << "缺 diagnostics 登记边（ARCH §3.5 ui->diagnostics）";
    EXPECT_NE(productLink.find("Qt6::Core"), std::string::npos) << "缺 Qt6::Core";
    EXPECT_NE(productLink.find("Qt6::Gui"), std::string::npos) << "缺 Qt6::Gui";
    EXPECT_NE(productLink.find("Qt6::Widgets"), std::string::npos) << "缺 Qt6::Widgets";
    // 越界单元逐一反证（O-31 明文禁止面＋业务域全量——R-1 门禁同口径）。
    const std::vector<std::string> kForbidden = {
        "sdurws_ird_project", "sdurws_ird_evidence", "sdurws_ird_execution",
        "sdurws_ird_policy",  "sdurws_ird_runtime",   "sdurws_ird_testkit",
        "sdurws_ird_reporting", "sdurws_ird_modeling", "sdurws_ird_requirements",
        "sdurws_ird_kinematics", "sdurws_ird_trajectory", "sdurws_ird_dynamics",
        "sdurws_ird_drivetrain", "sdurws_ird_selection", "sdurws_ird_optimization",
        "sdurws_ird_workflow", "sdurws_ird_io",
    };
    for (const auto& target : kForbidden) {
        EXPECT_EQ(productLink.find(target), std::string::npos)
            << "sdurws_ird_ui 链接声明出现越界目标 " << target
            << "（O-31/R-1/T-1——acceptance 5 禁止）";
    }
}

/**
 * T-1 测试侧允许形态契约（testkit.md §2.4）：testkit 目标只出现在三个
 * 测试目标的链接声明中，产品库 sdurws_ird_ui 的链接块零 testkit。
 *
 * 说明：测试目标直链 testkit 触发的 ird_gates SUB 命中属 F-221 同型既有
 * 登记（全仓 12 个测试目标同形态——T-1 允许形态、机器面回填待 WP-01-T03，
 * 登记提交件见 traceability/wp10-t02-gate-registrations.md）；本契约钉住的
 * 是"testkit 只被测试目标消费"的红线本身。
 */
TEST(UiLinkage, TestkitOnlyLinkedByTestTargets_UI_CTR)
{
    IRD_TEST_INFO("NFR-MNT-01", {}, std::nullopt);
    const std::string cmake = readFile(unitRoot() / "ui" / "CMakeLists.txt");
    // 产品库链接块：testkit 零出现（T-1 正向红线——产品目标不链 testkit，
    // testkit 不随产品分发）。
    const std::string productLink = extractLinkBlock(cmake, "sdurws_ird_ui");
    ASSERT_FALSE(productLink.empty());
    EXPECT_EQ(productLink.find("sdurws_ird_testkit"), std::string::npos)
        << "产品库 sdurws_ird_ui 链接 testkit（T-1 违例——testkit.md §2.4）";
    // 三个测试目标链接块：testkit 必须在位（报告设施 RecordListener 的
    // 消费面——T-1 允许形态；缺链则 ird-test-report.json 无产出通道）。
    for (const auto& target : {"sdurws_ird_ui_test", "sdurws_ird_ui_contract_test",
                               "sdurws_ird_ui_gui_test"}) {
        const std::string link = extractLinkBlock(cmake, target);
        ASSERT_FALSE(link.empty()) << "未找到 " << target << " 的链接声明块";
        EXPECT_NE(link.find("sdurws_ird_testkit"), std::string::npos)
            << target << " 未链接 testkit（RecordListener 报告设施缺失）";
        // 被测产品目标必须在位（公共头可 include＋库可链接的验证半区）。
        EXPECT_NE(link.find("sdurws_ird_ui"), std::string::npos)
            << target << " 未链接被测目标 sdurws_ird_ui";
    }
}

/**
 * 三测试目标与 ctest 标签登记契约（acceptance 3 具名证据）：三个目标按
 * DTB §5.5 注册（add_test 经 ird_add_gtest 宏）；LABELS ird（test/
 * contract_test——ui.md §12.1 表）；LABELS ird_gui（gui_test 串行——
 * §12.2 单实例纪律）；gui 目标链接 sdurws_ird_testkit_qt（acceptance 4
 * 启用形态）。
 */
TEST(UiLinkage, ThreeTestTargetsAndLabels_UI_CTR)
{
    IRD_TEST_INFO("NFR-MNT-01", {}, std::nullopt);
    const std::string cmake = readFile(unitRoot() / "ui" / "CMakeLists.txt");
    // 三目标声明在位（不预建空目标——本批即首个消费者落位）。
    EXPECT_NE(cmake.find("add_executable(sdurws_ird_ui_test"), std::string::npos)
        << "缺 sdurws_ird_ui_test 注册";
    EXPECT_NE(cmake.find("add_executable(sdurws_ird_ui_contract_test"),
                        std::string::npos)
        << "缺 sdurws_ird_ui_contract_test 注册";
    EXPECT_NE(cmake.find("add_executable(sdurws_ird_ui_gui_test"), std::string::npos)
        << "缺 sdurws_ird_ui_gui_test 注册";
    // ctest 标签（ui.md §12.1 表：ird / ird_gui 串行）。
    EXPECT_NE(cmake.find("PROPERTIES LABELS \"ird\""), std::string::npos)
        << "缺 LABELS ird 登记（test/contract_test）";
    EXPECT_NE(cmake.find("PROPERTIES LABELS \"ird_gui\""), std::string::npos)
        << "缺 LABELS ird_gui 登记（gui_test 串行）";
    // gui 目标链接 testkit_qt（acceptance 4：单列目标只被 GUI/模型测试目标
    // 链接——本目标为其启用触发面）。
    const std::string guiLink = extractLinkBlock(cmake, "sdurws_ird_ui_gui_test");
    ASSERT_FALSE(guiLink.empty());
    EXPECT_NE(guiLink.find("sdurws_ird_testkit_qt"), std::string::npos)
        << "gui_test 未链接 sdurws_ird_testkit_qt（启用形态缺失）";
    // gtest 接入（DTB §5.5 唯一机制）：vcpkg find_package(GTest CONFIG
    // REQUIRED) 失败即停——REQUIRED 调用在位核对。
    EXPECT_NE(cmake.find("find_package(GTest CONFIG REQUIRED)"), std::string::npos)
        << "缺 DTB §5.5 gtest REQUIRED 接入";
}
