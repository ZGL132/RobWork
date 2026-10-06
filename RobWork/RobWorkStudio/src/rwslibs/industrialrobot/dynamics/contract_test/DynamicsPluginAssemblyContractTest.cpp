/**
 * @file   DynamicsPluginAssemblyContractTest.cpp
 * @brief  dynamics 插件可注册契约测试（DynPluginAssembly）——装配门面
 *         描述符值的登记出处自证＋插件零计算红线（卡 §9.5"UI 线程不得
 *         执行动力学计算"机器可断言面；任务契约 WP-17-T02 acceptance 1
 *         "最小可注册实现"与 acceptance 2"二分结构扫描"的执行证明）。
 *
 * 设计依据：
 *   - units/dynamics.md §9.5（UI 协作——命令族 dynamics.analyze/
 *     show-curves/locate-peak/replay-at/export-curve-data；"UI 线程不得
 *     执行动力学计算（RNEA/仿真/统计全在 worker）"；界面面随 WP-17-T09）、
 *     §10.7（IDynamicsCommandHandler 硬边界——处理器内零动力学计算）、
 *     §10.1~§10.5（计算接口面——词表取其符号根）、§7（统计实现——
 *     PeakRecord/RMS/包络符号归计算库）、§5（RNEA——符号归计算库）
 *   - units/ui.md §11.1（白名单 token "dynamics"）、§3.5（键族
 *     plugin.<id>.title）
 *   - 先例：selection/contract_test/SelectionPluginAssemblyContractTest.cpp
 *     （WP-19-T02"最小可注册实现"的执行证明同位面）——dynamics 同属
 *     业务域单元，T02 门面为自持描述符，真实宿主注册契约随 WP-17-T09
 *     插件界面任务落位
 *   - 任务契约 tasks/foundation/WP-17-T02.json acceptance 1/2
 */

#include <sdurws/ird/dynamics/DynamicsPluginAssembly.hpp>

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

/// dynamics 单元树根（industrialrobot 目录——IRD_DYNAMICS_UNIT_ROOT
/// 注入，见单元 CMakeLists 契约测试目标的 target_compile_definitions）。
const fs::path& unitRoot()
{
    static const fs::path dir = fs::path{IRD_DYNAMICS_UNIT_ROOT};
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
        const auto base = unitRoot() / "dynamics" / sub;
        if (!fs::exists(base)) {
            ADD_FAILURE() << "dynamics 插件面目录缺失: " << sub;
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

/// RNEA/仿真/统计词表（卡 §9.5"UI 线程不得执行动力学计算"红线的具名
/// 符号面——取计算库规划接口与实现的符号根，§5 逆动力学＋§6 正动力学＋
/// §7 统计＋§10 接口清单）：插件面任一文件出现任一符号即违约（插件只
/// 提交任务/呈现投影/查表定位——动力学计算唯一在 worker 侧计算库）。
constexpr std::array<const char*, 14> kComputationSymbols{
    "RNEA",                        // §5 递归牛顿—欧拉（算法本体）
    "InverseDynamics",             // §10.1 IInverseDynamicsEvaluator（逆动力学评估）
    "ForwardDynamics",             // §10.2 IForwardDynamicsValidator（正动力学仿真）
    "DynamicsSample",              // §4.4 单样本动力学记录（逐样本计算产物）
    "DynamicsSeries",              // §4.4 单工况序列（统计输入面）
    "SeriesBuilder",               // §10.3 IDynamicsSeriesBuilder（序列构建）
    "Envelope",                    // §10.4 IDynamicsEnvelopeCalculator（包络计算）
    "PeakRecord",                  // §4.4/§7.2 峰值记录（窗与段计算）
    "PowerEnergy",                 // §10.5 IPowerEnergyCalculator（功率/能量积分）
    "EvidenceBuilder",             // §10.6 IDynamicsEvidenceBuilder（证据装配）
    "TrajectorySourcePort",        // §10.8 ITrajectorySourcePort（上游读取——装配面注入）
    "OperatingConditionResult",    // §4.4 单工况结果聚合（评估器产物）
    "std::sort",                   // 排序算法（§4.6 样本排序归计算库）
    "std::accumulate",             // 归约算法（§7.3 RMS 积分归计算库）
};

/// 直接读文件/写项目词表（插件纪律——插件不直接读文件、不直接写项目
/// 目录：文件访问经 io 通道、归档经 execution→IResultArchivePort——
/// 卡 §9.1"不直接写项目目录"协作红线；selection 同款第②条形态）。
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
 * 两字段必须逐字等于 ui.md 登记值——pluginId＝"dynamics"（§11.1 白名单
 * token）、titleKey＝"plugin.dynamics.title"（§3.5 键族 plugin.<id>.title，
 * id 词表＝白名单 token）。多次调用同值（纯值工厂——无缓存无状态）。
 */
TEST(DynPluginAssembly, DescriptorMatchesUiRegistrations_WP17T02_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-MNT-01"},
                  std::vector<std::string>{});

    const auto descriptor = sdurws::ird::dynamics::createDynamicsPluginAssembly();
    EXPECT_EQ(descriptor.pluginId, "dynamics")
        << "pluginId 必须＝ui.md §11.1 白名单 token（显示名永不替代身份——ARC-04）";
    EXPECT_EQ(descriptor.titleKey, "plugin.dynamics.title")
        << "titleKey 必须＝ui.md §3.5 键族 plugin.<id>.title（UX-02：值归 ui 文案资源）";

    // 纯值工厂：两次调用产生同值实例（无共享状态——文件头线程模型注）。
    const auto again = sdurws::ird::dynamics::createDynamicsPluginAssembly();
    EXPECT_EQ(again.pluginId, descriptor.pluginId);
    EXPECT_EQ(again.titleKey, descriptor.titleKey);
}

/**
 * 插件零计算红线·词表扫描（acceptance 2——卡 §9.5"UI 线程不得执行动力
 * 学计算"机器可断言面）：插件面（plugin/＋assembly/）全部翻译单元不含
 * RNEA/仿真/统计词表符号——插件只提交任务/呈现投影/查表定位，动力学
 * 计算唯一在计算库（二分结构的插件半区边界；ird_gates 第 4 步不扫
 * plugin/——本用例补齐该域的红线执行面）。
 */
TEST(DynPluginAssembly, PluginFaceHasZeroComputationSymbols_WP17T02_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-MNT-01"},
                  std::vector<std::string>{"DYN-01", "DYN-03", "DYN-05", "DYN-08"});

    const auto files = collectPluginFaceFiles();
    ASSERT_FALSE(files.empty()) << "插件面无源文件（落位形态缺失）";

    for (const auto& file : files) {
        const std::string text = readFile(file);
        for (const char* symbol : kComputationSymbols) {
            EXPECT_EQ(text.find(symbol), std::string::npos)
                << "插件面出现计算词表符号（卡 §9.5——RNEA/仿真/统计唯一"
                   "在计算库/worker）: " << symbol << " @ " << file.string();
        }
    }
}

/**
 * 插件零计算红线·直接访问扫描（acceptance 2——协作红线机器可断言面）：
 * 插件面不直接读文件、不直接写项目目录（文件访问经 io 通道、归档经
 * execution→IResultArchivePort——卡 §9.1"不直接写项目目录"）。
 */
TEST(DynPluginAssembly, PluginFaceHasNoDirectFileOrProjectAccess_WP17T02_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"ARC-02"},
                  std::vector<std::string>{});

    const auto files = collectPluginFaceFiles();
    ASSERT_FALSE(files.empty()) << "插件面无源文件（落位形态缺失）";

    for (const auto& file : files) {
        const std::string text = readFile(file);
        for (const char* symbol : kDirectAccessSymbols) {
            EXPECT_EQ(text.find(symbol), std::string::npos)
                << "插件面出现直接访问符号（文件/项目访问唯一经端口——"
                   "卡 §9.1）: " << symbol << " @ " << file.string();
        }
    }
}
