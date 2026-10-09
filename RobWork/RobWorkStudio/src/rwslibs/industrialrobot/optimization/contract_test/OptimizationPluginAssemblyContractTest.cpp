/**
 * @file   OptimizationPluginAssemblyContractTest.cpp
 * @brief  optimization 插件可注册契约测试（OptPluginAssembly）——装配门面
 *         描述符值的登记出处自证＋插件零计算红线（二分结构红线——卡
 *         §3.2"sdurws_ird_optimization_plugin：只消费端口与只读投影，
 *         不持算法/判定真值"机器可断言面）＋白名单挂位词表对账（B 方案
 *         自持对账——P-OPT-10）＋R2 能力缺席扫描（暂停相关能力不作为
 *         B 期承诺——V12-06/AT-35；任务契约 WP-20-T02 acceptance 1/2 与
 *         WP-20-T10 acceptance 1/2/3 的执行证明）。
 *
 * 设计依据：
 *   - units/optimization.md §3.2（插件目标行——Qt Widgets 唯一允许面；
 *     "只消费端口与只读投影，不持算法/判定真值"）、§2.4（N4/N9 非所有权
 *     ——FK/IK/轨迹/碰撞算法与工程判定归各域/evidence）、§9.1（R1 边界
 *     ——取消＋进度为 B 期承诺，暂停相关能力归 R2 不呈现）、§12.1 十接口
 *     （词表取其符号根）、§14.1（WP-20-T10 行——插件界面面落位任务）、
 *     §7（指标与 Pareto——计算产物值类型归计算库）、§8.2（候选生成
 *     ——搜索策略归计算库）
 *   - units/ui.md §11.1（白名单 token "optimization"）、§3.5（键族
 *     plugin.<id>.title）
 *   - 先例：trajectory/contract_test/TrajectoryPluginAssemblyContractTest.
 *     cpp（WP-16-T03"最小可注册实现"的执行证明同位面）＋dynamics
 *     WP-17-T09（B 方案白名单自持词表对账先例——WP-20-T10 同款承接）
 *   - 任务契约 tasks/foundation/WP-20-T02.json acceptance 1/2、
 *     tasks/foundation/WP-20-T10.json acceptance 1/2/3
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
/// §7 指标值类型＋§8.2 生成/编排/编码面；WP-20-T10 随界面面落位增列
/// 编排器/生成器/指标计算/非支配构建/候选编码六个实现符号根）：插件面
/// 任一文件出现任一符号即违约（插件只提交任务/呈现投影/查表定位——
/// 优化计算唯一在计算库/worker 侧；§2.4 N4/N9——FK/IK/轨迹/碰撞算法
/// 与工程判定归各域与 evidence）。
constexpr std::array<const char*, 22> kComputationSymbols{
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
    "TwoStageEvaluationOrchestrator",  // §8.4 两级批量编排器（T06 实现面）
    "generateSeededLhsCandidates",     // §8.2 确定性候选生成（T06 实现面）
    "computeStaticMetrics",            // §7.1 三项静态指标计算（T05 实现面）
    "buildFront",                      // §7.4 非支配分层构建（T05 实现面）
    "evaluateCandidate",               // §8.3 单候选评估（管线执行面）
    "canonicalizeRunConfiguration",    // §4.3 config.opt canonical（T06 面）
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
 * ——无缓存无状态）。T10 起工厂返回装配门面（描述符为其 descriptor
 * 成员——WP-17-T09 同款形态；两登记值逐字保留）。
 */
TEST(OptPluginAssembly, DescriptorMatchesUiRegistrations_WP20T02_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-MNT-01"},
                  std::vector<std::string>{});

    const auto facade = sdurws::ird::optimization::createOptimizationPluginAssembly();
    EXPECT_EQ(facade.descriptor.pluginId, "optimization")
        << "pluginId 必须＝ui.md §11.1 白名单 token（显示名永不替代身份——ARC-04）";
    EXPECT_EQ(facade.descriptor.titleKey, "plugin.optimization.title")
        << "titleKey 必须＝ui.md §3.5 键族 plugin.<id>.title（UX-02：值归 ui 文案资源）";

    // 纯值工厂：两次调用产生同值实例（无共享状态——文件头线程模型注）。
    const auto again = sdurws::ird::optimization::createOptimizationPluginAssembly();
    EXPECT_EQ(again.descriptor.pluginId, facade.descriptor.pluginId);
    EXPECT_EQ(again.descriptor.titleKey, facade.descriptor.titleKey);
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

// =====================================================================
// WP-20-T10 新增契约面（acceptance 1/2——白名单对账与 R2 缺席扫描）
// =====================================================================

/// R2 能力符号词表（acceptance 2 禁止项"暂停/继续归 R2（AT-35）——
/// V12-06"的机器可断言面）：插件面任一文件出现任一符号/token 即违约。
/// 词表取**标识符/token 精确形态**（执行侧请求取消接口名、能力声明位、
/// 连字符命令 token 形态）——全文本扫描（不剥注释），故插件面注释亦不
/// 书写这些字样（以中文表述替代）。
constexpr std::array<const char*, 7> kR2CapabilitySymbols{
    "requestPause",             // 执行侧暂停请求接口（R2——§9.2）
    "requestResume",            // 执行侧继续请求接口（R2）
    "supportsPause",            // 能力声明位（§9.4——R1 恒 false）
    "request-pause",            // 连字符 token 形态
    "request-resume",           // 连字符 token 形态
    "cmd.optimization.pause",   // 命令键形态（键族 cmd.<id>）
    "cmd.optimization.resume",  // 命令键形态
};

/**
 * R2 能力缺席扫描（WP-20-T10 acceptance 2 禁止项——"暂停/继续归 R2
 * 〔AT-35〕，V12-06"的机器可断言面）：插件面（plugin/＋assembly/）全部
 * 翻译单元不含 R2 能力符号——运行控制面板只承载进度与取消（B 期承诺
 * AT-34/OPT-VER-139），暂停相关呈现入口零登记（卡 §9.1"UI 只显示进度
 * 和取消状态"；§15.0——暂停不作 B 期承诺）。
 */
TEST(OptPluginAssembly, PluginFaceHasNoR2PauseResumeSurface_WP20T10_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-06"},
                  std::vector<std::string>{"AT-35"});

    const auto files = collectPluginFaceFiles();
    ASSERT_FALSE(files.empty()) << "插件面无源文件（落位形态缺失）";

    for (const auto& file : files) {
        const std::string text = readFile(file);
        for (const char* symbol : kR2CapabilitySymbols) {
            EXPECT_EQ(text.find(symbol), std::string::npos)
                << "插件面出现 R2 能力符号（暂停相关能力归 OPT-D/R2——"
                   "V12-06/AT-35，B 期零呈现入口）: " << symbol << " @ "
                   << file.string();
        }
    }
}

/**
 * 白名单挂位词表对账（WP-20-T10 acceptance 1 括号语义——"经注册端口
 * 白名单挂位"的 B 方案自持对账半区）：描述符 pluginId 落于 ui.md §11.1
 * 静态白名单八 token、挂位阶段 token 落于 §6.4 七阶段词表——两词表以
 * 测试自持常量对账（零 ui 头包含——本单元依赖白名单无 ui 编译边，
 * P-OPT-10 缺口登记归宿主装配批次收口；dynamics WP-17-T09 同款先例）。
 */
TEST(OptPluginAssembly, PluginIdAlignsHostWhitelistVocabulary_WP20T10_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"SA-01", "UX-12"},
                  std::vector<std::string>{});

    // ui.md §11.1 静态白名单八 token（编译期常量词表——AboutDialog.cpp
    // pluginUiWhitelist 同源清单的测试侧对账锚）。
    constexpr std::array<const char*, 8> kUiWhitelist{
        "modeling", "requirements", "kinematics", "trajectory",
        "dynamics", "selection", "optimization", "workflow"};
    // ui.md §6.4 七阶段 token（UX-12 七阶段序——StageId 的 token 形态）。
    constexpr std::array<const char*, 7> kStageTokens{
        "modeling", "requirements", "kinematics", "trajectory-dynamics",
        "selection", "optimization", "reporting"};

    const auto facade =
        sdurws::ird::optimization::createOptimizationPluginAssembly();
    const bool pluginInWhitelist =
        std::any_of(kUiWhitelist.begin(), kUiWhitelist.end(),
                    [&facade](const char* token) {
                        return facade.descriptor.pluginId == token;
                    });
    EXPECT_TRUE(pluginInWhitelist)
        << "pluginId 必须落于 ui.md §11.1 白名单八 token（SA-01 静态白名单"
           "——表外注册在宿主登记边界拒绝）";

    const bool stageInVocabulary =
        std::any_of(kStageTokens.begin(), kStageTokens.end(),
                    [&facade](const char* token) {
                        return facade.descriptor.stageToken == token;
                    });
    EXPECT_TRUE(stageInVocabulary)
        << "挂位阶段 token 必须落于 ui.md §6.4 七阶段词表（StageId::"
           "Optimization 的 token 形态）";

    // 面板登记记录的挂位阶段与描述符一致（同一 token——单面板单挂位）。
    ASSERT_EQ(facade.descriptor.panels.size(), 1u);
    EXPECT_EQ(facade.descriptor.panels[0].stageToken,
              facade.descriptor.stageToken);
}
