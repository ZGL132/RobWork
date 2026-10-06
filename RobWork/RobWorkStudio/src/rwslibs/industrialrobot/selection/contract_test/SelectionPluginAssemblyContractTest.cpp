/**
 * @file   SelectionPluginAssemblyContractTest.cpp
 * @brief  selection 插件可注册契约测试（SelPluginAssembly）——装配门面
 *         描述符值的登记出处自证＋插件零计算红线（卡 §3.4 机器可断言
 *         面；任务契约 WP-19-T02 acceptance 1"最小可注册实现"与
 *         acceptance 2"二分结构扫描"的执行证明）。
 *
 * 设计依据：
 *   - units/selection.md §3.1（插件组成——界面面随 WP-19-T10）、§3.2
 *     （插件依赖仅本计算库＋Qt Widgets——无 ui 编译边）、§3.4（插件零
 *     计算红线——①插件翻译单元不含筛选/插值/校核/排序算法〔静态扫描：
 *     筛选词表函数符号零命中〕；②插件不直接读文件、不直接写项目、不
 *     直接调用 io/project 实现〔经端口〕）
 *   - units/ui.md §11.1（白名单 token "selection"）、§3.5（键族
 *     plugin.<id>.title）
 *   - 先例：workflow/contract_test/PluginRegistrationContractTest.cpp
 *     （WP-22-T02"最小可注册实现"的执行证明同位面）——差异（诚实登记，
 *     非遗漏）：workflow 门面实现 ui::IPluginUiModule 并以真实
 *     IPluginUiRegistrar 校验序三查承载"可注册"；selection 卡 §3.2 无
 *     ui 编译边（树/属性页协议列运行时注入列），T02 门面为自持描述符
 *     ——本组用例以描述符登记出处自证＋零计算词表扫描承载同位纪律，
 *     真实宿主注册契约随 WP-19-T10 对接 UI-T21/T22 协议时落位。
 *   - 任务契约 tasks/foundation/WP-19-T02.json acceptance 1/2
 */

#include <sdurws/ird/selection/SelectionPluginAssembly.hpp>

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯登记（testkit §7.3）

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

#include <gtest/gtest.h>

namespace fs = std::filesystem;

namespace {

/// selection 单元树根（industrialrobot 目录——IRD_SELECTION_UNIT_ROOT
/// 注入，见单元 CMakeLists 契约测试目标的 target_compile_definitions）。
const fs::path& unitRoot()
{
    static const fs::path dir = fs::path{IRD_SELECTION_UNIT_ROOT};
    return dir;
}

/// 读取文件全文；不可读显性失败。
std::string readFile(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    EXPECT_TRUE(static_cast<bool>(in)) << "无法读取: " << path.string();
    return std::string{std::istreambuf_iterator<char>(in),
                       std::istreambuf_iterator<char>()};
}

/// 收集插件面（plugin/＋assembly/）全部 C++ 源文件路径；两目录均应存在
/// （T02 落位形态——Qt 例外载体域）。
std::vector<fs::path> collectPluginFaceFiles()
{
    std::vector<fs::path> files;
    for (const auto* sub : {"plugin", "assembly"}) {
        const auto base = unitRoot() / "selection" / sub;
        if (!fs::exists(base)) {
            ADD_FAILURE() << "selection 插件面目录缺失: " << sub;
            continue;
        }
        std::error_code iec;
        for (auto it = fs::recursive_directory_iterator(base, iec);
             it != fs::recursive_directory_iterator(); it.increment(iec)) {
            if (iec || !it->is_regular_file(iec)) { continue; }
            const auto ext = it->path().extension().string();
            if (ext == ".hpp" || ext == ".h" || ext == ".cpp") {
                files.push_back(it->path());
            }
        }
    }
    return files;
}

/// 筛选/插值/校核/排序词表（卡 §3.4 静态扫描的具名词表面——取计算库
/// 规划接口与实现的符号根，§3.1 组成表＋§3.5 布局表＋§14 接口清单）：
/// 插件面任一文件出现任一符号即违约（插件只呈现结果、不执行计算——
/// 筛选计算唯一在 sdurws_ird_selection 计算库）。
constexpr std::array<const char*, 13> kComputationSymbols{
    "Screening",                    // §3.1 Screening 组件（电机/减速器硬筛选）
    "screenMotors",                 // §14.4 IHardConstraintSelector::screenMotors
    "screenGearboxes",              // §14.4 IHardConstraintSelector::screenGearboxes
    "IHardConstraintSelector",      // §14.4 硬筛选接口
    "IPerformanceCurveEvaluator",   // §14.3 插值接口
    "PerformanceCurve",             // §6.1 曲线模型（插值域）
    "CombinationCheck",             // §3.1 组合校核组件
    "FeasibleSet",                  // §3.1/§10 可行集组件
    "ICatalogValidator",            // §14.2 业务校验接口
    "ICatalogProvider",             // §14.1 目录快照供给接口
    "CatalogPackageSnapshot",       // §4.2 目录快照（计算库业务模型）
    "CatalogDiff",                  // §13.6 目录差异比较（会话工具计算面）
    "std::sort",                    // 排序算法（§10.4 稳定排序归计算库）
};

/// 直接读文件/写项目词表（卡 §3.4 第②条——插件不直接读文件、不直接
/// 写项目、不直接调用 io/project 实现：文件/项目访问唯一经端口）。
constexpr std::array<const char*, 6> kDirectAccessSymbols{
    "std::ifstream",                // 直接文件读（经 io 通道替代）
    "std::ofstream",                // 直接文件写（经 io 通道替代）
    "std::fstream",                 // 直接文件读写（经 io 通道替代）
    "fopen",                        // C 文件句柄（经 io 通道替代）
    "sdurws/ird/io/",               // io 单元头包含（端口注入替代）
    "sdurws/ird/project/",          // project 单元头包含（①命令端口替代）
};

}  // namespace

/**
 * 描述符登记出处自证（acceptance 1——最小可注册形态的值面）：工厂产物
 * 两字段必须逐字等于 ui.md 登记值——pluginId＝"selection"（§11.1 白名单
 * 第 6 token）、titleKey＝"plugin.selection.title"（§3.5 键族
 * plugin.<id>.title，id 词表＝白名单 token）。多次调用同值（纯值工厂
 ——无缓存无状态）。
 */
TEST(SelPluginAssembly, DescriptorMatchesUiRegistrations_WP19T02_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-MNT-01"},
                  std::vector<std::string>{});

    const auto descriptor = sdurws::ird::selection::createSelectionPluginAssembly();
    EXPECT_EQ(descriptor.pluginId, "selection")
        << "pluginId 必须＝ui.md §11.1 白名单 token（显示名永不替代身份——ARC-04）";
    EXPECT_EQ(descriptor.titleKey, "plugin.selection.title")
        << "titleKey 必须＝ui.md §3.5 键族 plugin.<id>.title（UX-02：值归 ui 文案资源）";

    // 纯值工厂：两次调用产生同值实例（无共享状态——文件头线程模型注）。
    const auto again = sdurws::ird::selection::createSelectionPluginAssembly();
    EXPECT_EQ(again.pluginId, descriptor.pluginId);
    EXPECT_EQ(again.titleKey, descriptor.titleKey);
}

/**
 * 插件零计算红线·词表扫描（acceptance 2——卡 §3.4 第①条机器可断言面）：
 * 插件面（plugin/＋assembly/）全部翻译单元不含筛选/插值/校核/排序词表
 * 符号——插件只呈现结果，筛选/插值/校核/排序计算唯一在计算库（二分
 * 结构的插件半区边界；ird_gates 第 4 步不扫 plugin/——本用例补齐该域
 * 的红线执行面）。
 */
TEST(SelPluginAssembly, PluginFaceHasZeroComputationSymbols_WP19T02_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-MNT-01"},
                  std::vector<std::string>{"SEL-03", "SEL-04", "SEL-05"});

    const auto files = collectPluginFaceFiles();
    ASSERT_FALSE(files.empty()) << "插件面无源文件（落位形态缺失）";

    for (const auto& file : files) {
        const std::string text = readFile(file);
        for (const char* symbol : kComputationSymbols) {
            EXPECT_EQ(text.find(symbol), std::string::npos)
                << "插件面出现计算词表符号（卡 §3.4——筛选/插值/校核/排序"
                   "唯一在计算库）: " << symbol << " @ " << file.string();
        }
    }
}

/**
 * 插件零计算红线·直接访问扫描（acceptance 2——卡 §3.4 第②条机器可
 * 断言面）：插件面不直接读文件、不直接写项目、不直接调用 io/project
 * 实现（经端口——编辑/回填提交经页面出口→域命令→①命令端口；数据
 * 读取经 io 通道注入）。
 */
TEST(SelPluginAssembly, PluginFaceHasNoDirectFileOrProjectAccess_WP19T02_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"ARC-02"},
                  std::vector<std::string>{});

    const auto files = collectPluginFaceFiles();
    ASSERT_FALSE(files.empty()) << "插件面无源文件（落位形态缺失）";

    for (const auto& file : files) {
        const std::string text = readFile(file);
        for (const char* symbol : kDirectAccessSymbols) {
            EXPECT_EQ(text.find(symbol), std::string::npos)
                << "插件面出现直接访问符号（卡 §3.4——文件/项目访问唯一"
                   "经端口）: " << symbol << " @ " << file.string();
        }
    }
}
