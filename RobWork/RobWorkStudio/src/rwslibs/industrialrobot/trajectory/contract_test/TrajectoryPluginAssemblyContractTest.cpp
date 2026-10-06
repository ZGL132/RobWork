/**
 * @file   TrajectoryPluginAssemblyContractTest.cpp
 * @brief  trajectory 插件可注册契约测试（TrjPluginAssembly）——装配门面
 *         描述符值的登记出处自证＋插件零计算红线（卡 §16.1"零计算逻辑
 *         ＝规划、碰撞、时间参数化、复检全部在 worker/计算库；UI 线程只
 *         做投影、插值查表、命令提交"机器可断言面；任务契约 WP-16-T03
 *         acceptance 1"最小可注册实现"与 acceptance 2"二分结构扫描"的
 *         执行证明）。
 *
 * 设计依据：
 *   - units/trajectory.md §16.1（插件目标与形态）、§16.2（零修订与零
 *     计算红线——机器可断言表）、§15 公共接口 13 族（词表取其符号根）、
 *     §14.5.1（会话命令族——命令处理器零计算逻辑）、§6.2（轨迹数据模型
 *     ——计算产物值类型归计算库）、§13.1（质量指标——统计面归计算库）
 *   - units/ui.md §11.1（白名单 token "trajectory"）、§3.5（键族
 *     plugin.<id>.title）
 *   - 先例：dynamics/contract_test/DynamicsPluginAssemblyContractTest.cpp
 *     （WP-17-T02"最小可注册实现"的执行证明同位面）——trajectory 同属
 *     业务域单元，T03 门面为自持描述符，真实宿主注册契约随 WP-16-T12
 *     插件界面任务落位
 *   - 任务契约 tasks/foundation/WP-16-T03.json acceptance 1/2
 */

#include <sdurws/ird/trajectory/TrajectoryPluginAssembly.hpp>

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

/// trajectory 单元树根（industrialrobot 目录——IRD_TRAJECTORY_UNIT_ROOT
/// 注入，见单元 CMakeLists 契约测试目标的 target_compile_definitions）。
const fs::path& unitRoot()
{
    static const fs::path dir = fs::path{IRD_TRAJECTORY_UNIT_ROOT};
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
/// （T03 落位形态——Qt 例外载体域）。
std::vector<fs::path> collectPluginFaceFiles()
{
    std::vector<fs::path> files;
    for (const auto* sub : {"plugin", "assembly"}) {
        const auto base = unitRoot() / "trajectory" / sub;
        if (!fs::exists(base)) {
            ADD_FAILURE() << "trajectory 插件面目录缺失: " << sub;
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

/// 轨迹域计算词表（卡 §16.1"零计算逻辑＝规划、碰撞、时间参数化、复检
/// 全部在 worker/计算库"红线的具名符号面——取计算库规划接口与实现的
/// 符号根，§15.1~§15.10/§15.12 接口清单＋§6.2 数据模型＋§13.1 质量统计）
/// ：插件面任一文件出现任一符号即违约（插件只提交任务/呈现投影/查表
/// 定位——轨迹计算唯一在 worker 侧计算库）。
constexpr std::array<const char*, 15> kComputationSymbols{
    "PlanPtpSegment",            // §15.1 PTP 规划
    "PlanCartesianLine",         // §15.2 笛卡尔直线规划
    "CheckContinuity",           // §15.3 路径连接与连续性检查
    "IKinematicsComputePort",    // §15.4 IK/FK 注入端口（消费面适配归装配）
    "IPathPlannerAdapter",       // §15.5 规划器适配
    "RecheckPipeline",           // §15.6 路径复检编排
    "SmoothPipeline",            // §15.7 平滑
    "TimeParameterize",          // §15.8 时间参数化
    "ComputeQuality",            // §15.9 质量指标
    "TrjSequencePlanEvaluator",  // §15.10 域评估器
    "submitTrajectoryTask",      // §15.12 任务提交适配
    "TimedSample",               // §6.2 时间化样本（计算产物值类型）
    "TrajectoryQuality",         // §13.1 质量指标聚合（统计产物）
    "std::sort",                 // 排序算法（§7.5 构型选择稳定排序归计算库）
    "std::accumulate",           // 归约算法（§13.1 质量统计归计算库）
};

/// 直接读文件/写项目词表（插件纪律——插件不直接读文件、不直接写项目
/// 目录：文件访问经 io 通道、归档经执行通道（卡 §14.3"trajectory 不
/// 直接写盘"协作红线；dynamics 同款第②条形态））。
constexpr std::array<const char*, 6> kDirectAccessSymbols{
    "std::ifstream",               // 直接文件读（经 io 通道替代）
    "std::ofstream",               // 直接文件写（经 io 通道替代）
    "std::fstream",                // 直接文件读写（经 io 通道替代）
    "fopen",                       // C 文件句柄（经 io 通道替代）
    "sdurws/ird/io/",              // io 单元头包含（端口注入替代）
    "sdurws/ird/project/",         // project 单元头包含（②查询端口替代）
};

}  // namespace

/**
 * 描述符登记出处自证（acceptance 1——最小可注册形态的值面）：工厂产物
 * 两字段必须逐字等于 ui.md 登记值——pluginId＝"trajectory"（§11.1 白
 * 名单 token）、titleKey＝"plugin.trajectory.title"（§3.5 键族
 * plugin.<id>.title，id 词表＝白名单 token）。多次调用同值（纯值工厂
 * ——无缓存无状态）。
 */
TEST(TrjPluginAssembly, DescriptorMatchesUiRegistrations_WP16T03_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-MNT-01"},
                  std::vector<std::string>{});

    const auto descriptor = sdurws::ird::trajectory::createTrajectoryPluginAssembly();
    EXPECT_EQ(descriptor.pluginId, "trajectory")
        << "pluginId 必须＝ui.md §11.1 白名单 token（显示名永不替代身份——ARC-04）";
    EXPECT_EQ(descriptor.titleKey, "plugin.trajectory.title")
        << "titleKey 必须＝ui.md §3.5 键族 plugin.<id>.title（UX-02：值归 ui 文案资源）";

    // 纯值工厂：两次调用产生同值实例（无共享状态——文件头线程模型注）。
    const auto again = sdurws::ird::trajectory::createTrajectoryPluginAssembly();
    EXPECT_EQ(again.pluginId, descriptor.pluginId);
    EXPECT_EQ(again.titleKey, descriptor.titleKey);
}

/**
 * 插件零计算红线·词表扫描（acceptance 2——卡 §16.1/§16.2"UI 线程不得
 * 执行规划/碰撞/时间参数化"机器可断言面）：插件面（plugin/＋assembly/）
 * 全部翻译单元不含轨迹域计算词表符号——插件只提交任务/呈现投影/查表
 * 定位，轨迹计算唯一在计算库/worker（二分结构的插件半区边界；ird_gates
 * 第 4 步不扫 plugin/——本用例补齐该域的红线执行面）。
 */
TEST(TrjPluginAssembly, PluginFaceHasZeroComputationSymbols_WP16T03_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-MNT-01"},
                  std::vector<std::string>{"TRJ-01", "TRJ-03", "TRJ-05"});

    const auto files = collectPluginFaceFiles();
    ASSERT_FALSE(files.empty()) << "插件面无源文件（落位形态缺失）";

    for (const auto& file : files) {
        const std::string text = readFile(file);
        for (const char* symbol : kComputationSymbols) {
            EXPECT_EQ(text.find(symbol), std::string::npos)
                << "插件面出现计算词表符号（卡 §16.1——规划/碰撞/时间化"
                   "唯一在计算库/worker）: " << symbol << " @ "
                   << file.string();
        }
    }
}

/**
 * 插件零计算红线·直接访问扫描（acceptance 2——协作红线机器可断言面）：
 * 插件面不直接读文件、不直接写项目目录（文件访问经 io 通道、归档经
 * 执行通道——卡 §14.3"trajectory 不直接写盘"）。
 */
TEST(TrjPluginAssembly, PluginFaceHasNoDirectFileOrProjectAccess_WP16T03_ACC2)
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
                   "卡 §14.3）: " << symbol << " @ " << file.string();
        }
    }
}
