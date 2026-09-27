/**
 * @file   PluginGraphContractTest.cpp
 * @brief  kinematics 插件的构建图与零计算逻辑契约（WP-15-T12 acceptance
 *         2/4 的契约测试半区——静态扫描＋链接面钉住）。
 *
 * 设计依据：
 *   - units/kinematics.md §9.8（线程与刷新约束——"UI 线程零计算＞1 s 全
 *     部转 execution""面板不缓存权威结果、阈值/排序/筛选全部消费域设施"）、
 *     §3.2（"插件目标 → 本计算库＋sdurws_ird_ui；禁止链接任何其他业务域
 *     单元（R-1）"——链接面唯一性）；
 *   - 先例：modeling/contract_test 同款文本扫描形态（IRD_*_UNIT_ROOT 注入
 *     ＋CMakeLists/include 指令行扫描——构建图契约的单元内复核面）；
 *   - 任务契约 tasks/foundation/WP-15-T12.json acceptance 2（插件零计算
 *     逻辑——静态扫描）/acceptance 4（插件目标链接面＝本计算库＋
 *     sdurws_ird_ui——ird_gates 零新增登记口径）。
 *
 * 与 BuildGraphContractTest 的分工：彼处钉产品计算库的六边（T02 面）；
 * 此处钉插件目标的链接块与插件源码的零计算扫描（T12 面）——两用例共同
 * 构成 acceptance 2/4 的机器复核半区，ird_gates 引擎直跑为全仓门禁。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <algorithm>
#include <fstream>
#include <filesystem>
#include <iterator>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

/// kinematics 单元树根（industrialrobot 目录——IRD_KINEMATICS_UNIT_ROOT
/// 编译定义注入，BuildGraphContractTest 同款）。
const fs::path& unitRoot()
{
    static const fs::path dir = fs::path{IRD_KINEMATICS_UNIT_ROOT};
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

/// 剥离 "#" 注释（行尾注释文字不是构建图引用——BuildGraphContractTest 同款）。
std::string stripComment(std::string line)
{
    const auto hashPos = line.find('#');
    if (hashPos != std::string::npos) {
        line.erase(hashPos);
    }
    return line;
}

/// 收集插件目标的链接块文本（target_link_libraries(sdurws_ird_kinematics_plugin
/// ...) 的实参序列——链接面逐项钉住）。
std::vector<std::string> pluginLinkBlockArgs()
{
    const std::string text = readFile(unitRoot() / "kinematics" / "CMakeLists.txt");
    std::istringstream lines(text);
    std::string line;
    bool inBlock = false;
    std::vector<std::string> args;
    while (std::getline(lines, line)) {
        const std::string code = stripComment(line);
        if (!inBlock && code.find("target_link_libraries(sdurws_ird_kinematics_plugin")
                != std::string::npos) {
            inBlock = true;  // 链接块开始（多行块——右括号闭合）
        }
        if (inBlock) {
            // 右括号剥成空格（块结尾"Qt6::Widgets)"的括号不是词法部分）。
            std::string clean = code;
            std::replace(clean.begin(), clean.end(), ')', ' ');
            std::istringstream words(clean);
            std::string w;
            while (words >> w) {
                args.push_back(w);
            }
            if (code.find(')') != std::string::npos) {
                break;  // 块闭合——收集完成
            }
        }
    }
    return args;
}

/// 收集 plugin/**.cpp/.hpp 全部源文件（零计算扫描域——plugin 目录全部
/// 翻译单元与头）。
std::vector<fs::path> pluginSources()
{
    std::vector<fs::path> files;
    const auto base = unitRoot() / "kinematics" / "plugin";
    if (!fs::exists(base)) {
        ADD_FAILURE() << "kinematics/plugin 目录缺失（插件未落位？）";
        return files;
    }
    for (auto it = fs::recursive_directory_iterator(base);
         it != fs::recursive_directory_iterator(); ++it) {
        const auto ext = it->path().extension().string();
        if (ext == ".cpp" || ext == ".hpp" || ext == ".h") {
            files.push_back(it->path());
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

/// 源码是否包含指令行 include（精确匹配 "#include <xxx>" 的出现）。
bool containsInclude(const std::string& text, const std::string& header)
{
    return text.find("#include <" + header + ">") != std::string::npos
        || text.find("#include \"" + header + "\"") != std::string::npos;
}

}  // namespace

/**
 * 插件链接面钉住（acceptance 4——"插件目标链接面＝本计算库＋
 * sdurws_ird_ui（§3.2——禁止链接其他业务域单元 R-1）"的逐项断言）：
 * 链接块实参恰为 {目标名, PUBLIC 可见性, sdurws_ird_kinematics,
 * sdurws_ird_ui, Qt6::Core, Qt6::Gui, Qt6::Widgets}——多一项即越界
 * （ird_gates 引擎直跑比对为全仓面，此处为单元内行级钉住）。
 */
TEST(KinPluginGraph, PluginLinkageFaceExactlyKinematicsAndUi_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"ARC-02", "NFR-MNT-01"},
                  std::vector<std::string>{});

    const auto args = pluginLinkBlockArgs();
    ASSERT_FALSE(args.empty()) << "未找到 sdurws_ird_kinematics_plugin 链接块";

    // 逐项白名单（§3.2 明文链接面——本计算库＋ui＋Qt 三件套；PUBLIC 形态
    // 与 modeling 插件同构）。
    const std::set<std::string> allowed = {
        "target_link_libraries(sdurws_ird_kinematics_plugin", "PUBLIC",
        "sdurws_ird_kinematics", "sdurws_ird_ui",
        "Qt6::Core", "Qt6::Gui", "Qt6::Widgets"};
    for (const auto& a : args) {
        EXPECT_NE(allowed.find(a), allowed.end())
            << "插件链接块出现表外条目（§3.2——仅本计算库＋sdurws_ird_ui＋"
               "Qt；业务域互链即 R-1）: " << a;
    }
    // 必备项存在性（链接面不缩水）。
    for (const auto* must :
         {"sdurws_ird_kinematics", "sdurws_ird_ui", "Qt6::Widgets"}) {
        EXPECT_NE(std::find(args.begin(), args.end(), std::string(must)),
                  args.end())
            << "插件链接块缺失必备项（§3.2 链接面）: " << must;
    }
}

/**
 * 插件零计算逻辑扫描（acceptance 2——"阈值/排序/筛选全部消费域设施"
 * "UI 线程零计算"的静态半区）：plugin/** 全源码扫描三条禁令——
 *   1. 零自排序（std::sort/std::stable_sort/std::sort_heap——排序唯一经
 *      IKinematicSolutionSet 视图与 sortSolutions 唯一实现点，NFR-MNT-04）；
 *   2. 零自建线程/线程池（std::thread/QThread/QThreadPool/QtConcurrent
 *      ——UI 线程纪律＋后台执行唯一经后台缝；§3.4）；
 *   3. 零其他业务域单元 include（modeling/requirements/trajectory/
 *      dynamics/selection/optimization——R-1 include 面；插件只消费
 *      kinematics/ui/core/diagnostics/runtime/policy/evidence 公共头）。
 */
TEST(KinPluginGraph, PluginSourcesZeroComputationScan_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-PERF-01", "NFR-MNT-04"},
                  std::vector<std::string>{});

    const auto files = pluginSources();
    ASSERT_GE(files.size(), 10U) << "plugin 源文件数异常（落位不完整？）";

    for (const auto& f : files) {
        const std::string text = readFile(f);
        const auto rel = f.string();

        // ---- 禁令 1：零自排序（呈现排序权威唯一在域设施）。
        EXPECT_EQ(text.find("std::sort"), std::string::npos)
            << "插件源出现 std::sort（排序唯一经域设施——NFR-MNT-04）: " << rel;
        EXPECT_EQ(text.find("std::stable_sort"), std::string::npos)
            << "插件源出现 std::stable_sort: " << rel;

        // ---- 禁令 2：零自建线程（UI 线程＋后台缝纪律——§3.4/§9.8）。
        EXPECT_EQ(text.find("std::thread"), std::string::npos)
            << "插件源出现 std::thread（后台执行唯一经缝——acceptance 2）: "
            << rel;
        EXPECT_EQ(text.find("QThread"), std::string::npos)
            << "插件源出现 QThread: " << rel;
        EXPECT_EQ(text.find("QtConcurrent"), std::string::npos)
            << "插件源出现 QtConcurrent: " << rel;
        EXPECT_EQ(text.find("std::async"), std::string::npos)
            << "插件源出现 std::async: " << rel;

        // ---- 禁令 3：零其他业务域单元 include（R-1 include 面）。
        for (const char* unit : {"modeling", "requirements", "trajectory",
                                 "dynamics", "selection", "optimization"}) {
            const std::string token = std::string("sdurws/ird/") + unit + "/";
            EXPECT_EQ(text.find(token), std::string::npos)
                << "插件源 include 其他业务域单元（R-1）: " << rel << " → " << token;
        }
    }
}

/**
 * 插件文件域隔离（零 Qt 红线的文件域半区——R-3 扫描域为 include/**＋
 * src/**）：Qt 消费必须只在 plugin/、app/、assembly/ 三目录（产品扫描域
 * 零触碰——modeling 落位同款先例）。
 */
TEST(KinPluginGraph, QtConsumptionConfinedToPluginFileDomains_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-MNT-01"}, std::vector<std::string>{});

    // include/＋src/ 产品扫描域零新增（插件不向产品面引入任何文件——
    // "plugin 目录不入扫描域"的负向断言：扫描域内不存在 plugin 名下文件）。
    for (const auto* sub : {"include", "src"}) {
        const auto base = unitRoot() / "kinematics" / sub;
        ASSERT_TRUE(fs::exists(base)) << "产品面目录缺失: " << sub;
        for (auto it = fs::recursive_directory_iterator(base);
             it != fs::recursive_directory_iterator(); ++it) {
            const std::string name = it->path().filename().string();
            EXPECT_EQ(name.find("Panel"), std::string::npos)
                << "插件面文件不得进入产品扫描域（R-3 文件域隔离）: "
                << it->path().string();
        }
    }

    // assembly/ 装配契约头存在（O-31 装配器单行适配面——ui 宿主的唯一
    // 公共入口）。
    const auto assemblyHeader = unitRoot() / "kinematics" / "assembly"
        / "sdurws" / "ird" / "kinematics" / "KinematicsPluginAssembly.hpp";
    EXPECT_TRUE(fs::exists(assemblyHeader))
        << "装配契约头缺失（宿主装配层的唯一公共入口）";
}

/// 任务点表只读消费视图（acceptance 4——"任务点表为只读消费视图（真值
/// 归 requirements——§2.5 重定位结论）"的静态半区）：插件面零任务点写回
/// 通道（无 setter/写接口——数据源缝为 const 提供器）。
TEST(KinPluginGraph, TaskPointViewIsReadOnlyConsumption_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-03"}, std::vector<std::string>{});

    // 数据源缝形态：KinTaskPointsProvider 为 const 提供器（值返回）——
    // 结构上无写回通道（KinPanelTypes.hpp 冻结形态钉住）。
    const std::string types = readFile(unitRoot() / "kinematics" / "plugin"
                                       / "KinPanelTypes.hpp");
    EXPECT_NE(types.find("using KinTaskPointsProvider = "
                         "std::function<std::vector<KinTaskPointRow>()>"),
              std::string::npos)
        << "任务点数据源缝形态漂移（const 提供器——只读消费视图的结构保证）";
    // 行结构无写入面（KinTaskPointRow 成员全值＋无 setter——头内无
    // "setTaskPoint"字样）。
    EXPECT_EQ(types.find("setTaskPoint"), std::string::npos)
        << "插件面出现任务点写入口（真值归 requirements——§2.5 重定位）";
}
