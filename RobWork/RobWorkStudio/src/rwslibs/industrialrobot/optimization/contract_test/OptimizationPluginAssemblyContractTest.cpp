/**
 * @file   OptimizationPluginAssemblyContractTest.cpp
 * @brief  optimization 插件可注册契约测试（OptPluginAssembly）——装配门面
 *         描述符值的登记出处自证＋插件零计算红线（二分结构红线——卡
 *         §3.2"sdurws_ird_optimization_plugin：只消费端口与只读投影，
 *         不持算法/判定真值"机器可断言面；任务契约 WP-20-T02 acceptance 1
 *         "最小可注册实现"与 acceptance 2"二分结构扫描"的执行证明）。
 *
 * 设计依据：
 *   - units/optimization.md §3.2（插件目标行——Qt Widgets 唯一允许面；
 *     "只消费端口与只读投影，不持算法/判定真值"）、§2.4（N4/N9 非所有权
 *     ——FK/IK/轨迹/碰撞算法与工程判定归各域/evidence）、§12.1 十接口
 *     （词表取其符号根）、§14.1（WP-20-T10 行——插件界面面落位任务）、
 *     §7（指标与 Pareto——计算产物值类型归计算库）、§8.2（候选生成
 *     ——搜索策略归计算库）
 *   - units/ui.md §11.1（白名单 token "optimization"）、§3.5（键族
 *     plugin.<id>.title）
 *   - 先例：trajectory/contract_test/TrajectoryPluginAssemblyContractTest.
 *     cpp（WP-16-T03"最小可注册实现"的执行证明同位面）——optimization
 *     同属业务域单元，T02 门面为自持描述符，真实宿主注册契约随 WP-20-T10
 *     插件界面任务落位
 *   - 任务契约 tasks/foundation/WP-20-T02.json acceptance 1/2
 */

#include <sdurws/ird/optimization/OptimizationPluginAssembly.hpp>

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

/// optimization 单元树根（industrialrobot 目录——IRD_OPTIMIZATION_UNIT_ROOT
/// 注入，见单元 CMakeLists 契约测试目标的 target_compile_definitions）。
const fs::path& unitRoot()
{
    static const fs::path dir = fs::path{IRD_OPTIMIZATION_UNIT_ROOT};
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
        const auto base = unitRoot() / "optimization" / sub;
        if (!fs::exists(base)) {
            ADD_FAILURE() << "optimization 插件面目录缺失: " << sub;
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

/// 优化域计算词表（二分结构红线——卡 §3.2 插件目标行"只消费端口与只读
/// 投影，不持算法/判定真值"的具名符号面——取计算库 §12.1 十接口与实现
/// 符号根：变量/约束/目标/生成器/管线/Pareto/预检/运行控制/应用/导出＋
/// §7 指标值类型＋§8.2 生成产物）：插件面任一文件出现任一符号即违约
/// （插件只提交任务/呈现投影/查表定位——优化计算唯一在计算库/worker
/// 侧；§2.4 N4/N9——FK/IK/轨迹/碰撞算法与工程判定归各域与 evidence）。
constexpr std::array<const char*, 16> kComputationSymbols{
    "IOptimizationVariableProvider",   // §12.1 变量词表服务面
    "IOptimizationConstraintProvider", // §12.1 约束编排面
    "IOptimizationObjectiveProvider",  // §12.1 目标指标配置面
    "ICandidateGenerator",             // §12.1 候选生成策略接口（OPT-10）
    "IEvaluationPipeline",             // §12.1 评估管线（O7）
    "IParetoFrontBuilder",             // §12.1 Pareto 非支配集构建（O9）
    "IOptimizationPreflightService",   // §12.1 预检服务（O3）
    "IOptimizationRunController",      // §12.1 运行控制器
    "IOptimizationCandidateApplier",   // §12.1 候选应用组装（O13）
    "IOptimizationExportProvider",     // §12.1 导出事实提供（O12）
    "IRobustnessReviewer",             // §12.1 鲁棒性复核调用面（OPT-09）
    "CandidatePatch",                  // §5.5 候选补丁（计算产物值类型）
    "MetricValue",                     // §7 指标值（计算产物值类型）
    "std::sort",                       // 排序算法（§7.4 稳定排序归计算库）
    "std::accumulate",                 // 归约算法（§7 指标合成归计算库）
    "generateNextBatch",               // §8.2 候选批生成（搜索策略执行面）
};

/// 直接读文件/写项目词表（插件纪律——插件不直接读文件、不直接写项目
/// 目录：文件访问经 io 通道、候选应用经①命令端口（卡 §10.2 红线"不
/// 直接修改 RobotDesign、不直接写项目文件、不绕过 ProjectCommandService"；
/// trajectory 同款第②条形态））。
constexpr std::array<const char*, 6> kDirectAccessSymbols{
    "std::ifstream",               // 直接文件读（经 io 通道替代）
    "std::ofstream",               // 直接文件写（经 io 通道替代）
    "std::fstream",                // 直接文件读写（经 io 通道替代）
    "fopen",                       // C 文件句柄（经 io 通道替代）
    "sdurws/ird/io/",              // io 单元头包含（端口注入替代）
    "sdurws/ird/project/",         // project 单元头包含（①命令端口替代）
};

}  // namespace

/**
 * 描述符登记出处自证（acceptance 1——最小可注册形态的值面）：工厂产物
 * 两字段必须逐字等于 ui.md 登记值——pluginId＝"optimization"（§11.1 白
 * 名单 token）、titleKey＝"plugin.optimization.title"（§3.5 键族
 * plugin.<id>.title，id 词表＝白名单 token）。多次调用同值（纯值工厂
 * ——无缓存无状态）。
 */
TEST(OptPluginAssembly, DescriptorMatchesUiRegistrations_WP20T02_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-MNT-01"},
                  std::vector<std::string>{});

    const auto descriptor = sdurws::ird::optimization::createOptimizationPluginAssembly();
    EXPECT_EQ(descriptor.pluginId, "optimization")
        << "pluginId 必须＝ui.md §11.1 白名单 token（显示名永不替代身份——ARC-04）";
    EXPECT_EQ(descriptor.titleKey, "plugin.optimization.title")
        << "titleKey 必须＝ui.md §3.5 键族 plugin.<id>.title（UX-02：值归 ui 文案资源）";

    // 纯值工厂：两次调用产生同值实例（无共享状态——文件头线程模型注）。
    const auto again = sdurws::ird::optimization::createOptimizationPluginAssembly();
    EXPECT_EQ(again.pluginId, descriptor.pluginId);
    EXPECT_EQ(again.titleKey, descriptor.titleKey);
}

/**
 * 插件零计算红线·词表扫描（acceptance 2——二分结构"不持算法/判定真值"
 * 机器可断言面）：插件面（plugin/＋assembly/）全部翻译单元不含优化域
 * 计算词表符号——插件只提交任务/呈现投影/查表定位，优化计算唯一在
 * 计算库/worker（二分结构的插件半区边界；ird_gates 第 4 步不扫 plugin/
 * ——本用例补齐该域的红线执行面）。
 */
TEST(OptPluginAssembly, PluginFaceHasZeroComputationSymbols_WP20T02_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-MNT-01"},
                  std::vector<std::string>{"OPT-04", "OPT-06"});

    const auto files = collectPluginFaceFiles();
    ASSERT_FALSE(files.empty()) << "插件面无源文件（落位形态缺失）";

    for (const auto& file : files) {
        const std::string text = readFile(file);
        for (const char* symbol : kComputationSymbols) {
            EXPECT_EQ(text.find(symbol), std::string::npos)
                << "插件面出现计算词表符号（二分结构——算法/判定真值唯一"
                   "在计算库/worker，卡 §3.2）: " << symbol << " @ "
                   << file.string();
        }
    }
}

/**
 * 插件零计算红线·直接访问扫描（acceptance 2——协作红线机器可断言面）：
 * 插件面不直接读文件、不直接写项目目录（文件访问经 io 通道、候选应用
 * 经①命令端口——卡 §10.2"不直接修改 RobotDesign、不直接写项目文件、
 * 不绕过 ProjectCommandService"红线）。
 */
TEST(OptPluginAssembly, PluginFaceHasNoDirectFileOrProjectAccess_WP20T02_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"ARC-02"},
                  std::vector<std::string>{"AT-12"});

    const auto files = collectPluginFaceFiles();
    ASSERT_FALSE(files.empty()) << "插件面无源文件（落位形态缺失）";

    for (const auto& file : files) {
        const std::string text = readFile(file);
        for (const char* symbol : kDirectAccessSymbols) {
            EXPECT_EQ(text.find(symbol), std::string::npos)
                << "插件面出现直接访问符号（文件/项目访问唯一经端口——"
                   "卡 §10.2 候选应用红线）: " << symbol << " @ "
                   << file.string();
        }
    }
}
