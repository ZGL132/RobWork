/**
 * @file   PluginGraphContractTest.cpp
 * @brief  requirements 插件装配门面契约（WP-14-T11 acceptance 1/3 的契约
 *         测试半区——静态扫描＋构建图钉住）。
 *
 * 设计依据：
 *   - DTB §4.2 O-45 裁决（WP-14-T11 补建缝的授权出处——门面与 modeling/
 *     kinematics 双先例同构：assembly/ 落插件目标 PUBLIC include 面、实现
 *     编入 sdurws_ird_requirements_plugin、宿主装配层零插件私有头依赖）；
 *   - units/requirements.md §11（WP-14-T11 行——落位形态权威）、§9.8
 *     （域命令登记数据经门面直通）；
 *   - 先例：kinematics/contract_test/PluginGraphContractTest.cpp（WP-15-T12
 *     同款文本扫描形态——IRD_*_UNIT_ROOT 注入＋CMakeLists/include 指令行
 *     扫描——构建图契约的单元内复核面）；
 *   - 任务契约 tasks/foundation/WP-14-T11.json acceptance 1（门面出线面/
 *     宿主零私有头依赖）/acceptance 3（ird_gates 零增量——本测试钉构建
 *     图半区，引擎直跑为全仓门禁）。
 *
 * 与 BuildGraphContractTest 的分工：彼处钉产品计算库的五边（T02 面）；
 * 此处钉装配门面的落位形态与出线面（T11 面）——两用例共同构成本任务
 * acceptance 的机器复核半区。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯登记（testkit §7.3）

#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

/// requirements 单元树根（industrialrobot 目录——IRD_REQUIREMENTS_UNIT_ROOT
/// 注入，BuildGraphContractTest 同款）。
const fs::path& unitRoot()
{
    static const fs::path dir = fs::path{IRD_REQUIREMENTS_UNIT_ROOT};
    return dir;
}

/// 读取文本文件（不存在/不可读＝测试装配错误，显性失败）。
std::string readFile(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    EXPECT_TRUE(static_cast<bool>(in)) << "无法读取: " << path.string();
    return std::string{std::istreambuf_iterator<char>(in),
                       std::istreambuf_iterator<char>()};
}

}  // namespace

// =====================================================================
// ACC1——门面落位形态与私有头隔离（O-31 单行适配面纪律）
// =====================================================================

/// 门面头落位于 assembly/ 且零引号 include（宿主消费面只见本头与 ui 公共
/// 头——插件私有头不外溢，R-2 门面纪律的机器复核半区）。
TEST(RequirementsPluginGraph, FacadeHeaderAssemblyLocationZeroPrivateInclude_O45)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-12", "NFR-MNT-01"},
                  std::vector<std::string>{"ACC1-facade-location", "R-2"});
    const fs::path facade =
        unitRoot() / "requirements" / "assembly" / "sdurws" / "ird" / "requirements"
        / "RequirementsPluginAssembly.hpp";
    ASSERT_TRUE(fs::exists(facade)) << facade.string();
    const std::string src = readFile(facade);

    // 零引号 include：门面头的依赖面只允许尖括号形式（系统/公共头——
    // 引号形式是同目录/相对路径私有头的惯例载体，出现在门面头即私有头
    // 外溢）。零 Qt include 同面核查（assembly/ 与 plugin/ 同在零 Qt 红线
    // 文件域之外——但门面头自身零 Qt 才能让宿主装配 TU 不被拖入 Widgets，
    // kinematics/modeling 双先例同款）。
    EXPECT_EQ(src.find("#include \"", 0), std::string::npos) << "门面头出现引号 include";
    EXPECT_EQ(src.find("#include <Q", 0), std::string::npos) << "门面头出现 Qt include";

    // 零插件私有头名命中（双保险——即使私有头被写成尖括号路径也拦住）。
    // 扫描面＝#include 指令行（"引用私有头"的机器面；注释/文档提及不构
    // 成编译期依赖——kinematics BuildGraphContractTest 的行扫描同口径）。
    std::istringstream stream(src);
    std::string line;
    while (std::getline(stream, line)) {
        const auto includePos = line.find("#include");
        if (includePos == std::string::npos) {
            continue;
        }
        for (const char* privateHeader :
             {"RequirementsUiModule.hpp", "RequirementsPanelWidget.hpp",
              "HostMigrationProviders.hpp", "PanelCommandCatalog.hpp",
              "PanelEditFlow.hpp"}) {
            EXPECT_EQ(line.find(privateHeader), std::string::npos)
                << "门面头 include 插件私有头: " << privateHeader;
        }
    }
}

/// 门面出线面＝acceptance 1 词表（模块接口面＋三接入面句柄＋选择服务
/// 接线＋三维拾取上报＋会话接线——O-45 补缝的完整装配面）。
TEST(RequirementsPluginGraph, FacadeExposeAcceptanceSurface_O45)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-12", "NFR-MNT-01"},
                  std::vector<std::string>{"ACC1-expose-surface"});
    const fs::path facade =
        unitRoot() / "requirements" / "assembly" / "sdurws" / "ird" / "requirements"
        / "RequirementsPluginAssembly.hpp";
    ASSERT_TRUE(fs::exists(facade)) << facade.string();
    const std::string src = readFile(facade);

    // 出线面词表（acceptance 1 逐项——缺失任一即宿主装配缝缺位回归）。
    for (const char* symbol :
         {"createRequirementsPluginAssembly",       // 唯一装配点
          "RequirementsSharedSurfaceHandles",       // 三接入面句柄结构（同构先例名）
          "sharedSurfaceProviders",                 // 树节点/属性页供给者出口
          "attachSelectionService",                 // 选择服务接线（下行联动启用）
          "detachSelectionService",                 // 显式退订（关闭清理半区）
          "reportView3DPick",                       // 三维拾取上报（上行）
          "attachEditor",                           // 编辑器注入（buildDraftCommand 数据前提）
          "bindSessionAnchor",                      // 会话锚（branch/基线）
          "noteAppliedRevision",                    // 应用回执（基线前移＋根回填）
          "bindReadiness",                          // 就绪报告（readonlyProjections 数据源）
          "onSessionDetached",                      // 会话脱离（项目关闭清理）
          "std::unique_ptr<ui::IPluginUiModule> module"}) {  // IPluginUiModule 接口面
        EXPECT_NE(src.find(symbol), std::string::npos) << "门面缺出线面: " << symbol;
    }
}

/// P-REQ-8 消账机器复核：RequirementsUiModule 已切换为 IPluginUiModule
/// 真实继承（门面 module 出线零适配层——与 modeling/kinematics 同构）。
TEST(RequirementsPluginGraph, UiModuleInterfaceSwitchRegistered_O45)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-MNT-01"},
                  std::vector<std::string>{"ACC1-interface-switch", "P-REQ-8"});
    const std::string moduleHeader = readFile(unitRoot() / "requirements" / "plugin"
                                              / "RequirementsUiModule.hpp");
    // 继承切换（三方法签名与冻结接口同形——override 标注由编译器复核，
    // 此处钉声明形态防回退）。
    EXPECT_NE(moduleHeader.find("class RequirementsUiModule final : public ui::IPluginUiModule"),
              std::string::npos)
        << "RequirementsUiModule 未继承 IPluginUiModule（P-REQ-8 消账回退）";
}

// =====================================================================
// ACC3——构建图钉住（ird_gates 零增量的单元内复核半区）
// =====================================================================

/// 门面实现编入插件目标＋assembly/ PUBLIC include 面存在（宿主消费
/// <sdurws/ird/requirements/RequirementsPluginAssembly.hpp> 的构建面）。
TEST(RequirementsPluginGraph, FacadeImplInPluginTargetAndPublicInclude_O45)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-MNT-01"},
                  std::vector<std::string>{"ACC3-build-graph"});
    const std::string cmake = readFile(unitRoot() / "requirements" / "CMakeLists.txt");

    // ①实现 TU 编入 sdurws_ird_requirements_plugin 源列表（kinematics/
    //   modeling 同款落位——plugin/ 私有实现编入插件目标自身）。
    EXPECT_NE(cmake.find("plugin/RequirementsPluginAssembly.cpp"), std::string::npos)
        << "门面实现未编入插件目标";

    // ②assembly/ PUBLIC include 面（宿主装配层的 include 解析路径——
    //   kinematics CMake 278~282 行同款形态）。
    EXPECT_NE(cmake.find("sdurws_ird_requirements_plugin PUBLIC"), std::string::npos)
        << "插件目标缺 PUBLIC include 面";
    EXPECT_NE(cmake.find("${CMAKE_CURRENT_SOURCE_DIR}/assembly"), std::string::npos)
        << "assembly/ 未暴露为 include 路径";
}

/// 插件目标链接面零变化（本任务只补缝——不新增任何链接边，ird_gates
/// 零增量的构建图前提；单行链接语句＝T-1 扫描的登记形态）。
TEST(RequirementsPluginGraph, PluginTargetLinkFaceUnchanged_O45)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-MNT-01", "R-1"},
                  std::vector<std::string>{"ACC3-link-face"});
    const std::string cmake = readFile(unitRoot() / "requirements" / "CMakeLists.txt");
    // 链接面＝本单元计算库＋sdurws_ird_ui＋Qt 三件套（WP-14-T08 落位既定
    // 面——逐字钉住，新增任一边即本用例红）。
    EXPECT_NE(cmake.find(
                  "target_link_libraries(sdurws_ird_requirements_plugin PUBLIC "
                  "sdurws_ird_requirements sdurws_ird_ui Qt6::Core Qt6::Gui Qt6::Widgets)"),
              std::string::npos)
        << "插件目标链接面漂移（R-1 链接面钉住失败）";
}
