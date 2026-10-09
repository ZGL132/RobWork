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

// ASM-PLUG 收口批（P-OPT-10 消账）——真实注册面契约的消费头：
// 注册端口/模块接口/词表类型的公共面（跨单元测试链接边已登记
// IRD_TEST_TARGET_EDGES——sdurws_ird_optimization_contract_test→sdurws_ird_ui）。
#include <sdurws/ird/ui/ICommandRegistry.hpp>    // ui::CommandDescriptor 完整类型（vector 成员析构）
#include <sdurws/ird/ui/IPluginUiModule.hpp>     // ui::IPluginUiModule（接口消费——捕获指针调 readonlyProjections）
#include <sdurws/ird/ui/IPluginUiRegistrar.hpp>  // ui::IPluginUiRegistrar/createPluginUiRegistrar/RegistrationOutcome
#include <sdurws/ird/ui/UiTypes.hpp>             // ui::StageId（七值词表——token 翻译对账）
#include <sdurws/ird/project/CommandService.hpp> // project::CommandEnvelope 完整类型（§11.2 草稿半区接口消费——
                                                 //   optional 返回值构造/析构实例化要求；optimization→project
                                                 //   为卡 §3.2 九登记边之一，R-2 公共面合规）

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
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
 *
 * ASM-PLUG 收口批具名豁免（诚实登记）：plugin/OptUiModule.cpp 对
 * "sdurws/ird/project/" 一项豁免——ui.md §11.2 冻结接口
 * IPluginUiModule::buildDraftCommand 的返回值 optional 携带 project 信
 * 封类型，`return std::nullopt` 的构造/析构实例化要求该元素完整类型，
 * 实现 TU 必须自含该公共头（modeling/requirements/kinematics/workflow
 * 四先例＋dynamics/selection ASM-PLUG 同批的 UiModule 实现同款包含面；
 * optimization→project 本为卡 §3.2 九登记边之一）。豁免面仅该头包含行：
 * OptUiModule 零 project 业务访问（不调用任何 project 实现符号——本函
 * 数恒返回空语义），红线语义（候选应用不绕过命令服务）不松动；该 TU
 * 对其余符号与其余插件面文件全量扫描不变。
 */
TEST(OptPluginAssembly, PluginFaceHasNoDirectFileOrProjectAccess_WP20T02_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"ARC-02"},
                  std::vector<std::string>{"AT-12"});

    const auto files = collectPluginFaceFiles();
    ASSERT_FALSE(files.empty()) << "插件面无源文件（落位形态缺失）";

    for (const auto& file : files) {
        const std::string text = readFile(file);
        // 豁免判定：OptUiModule.cpp 仅放行 "sdurws/ird/project/" 一项
        // （§11.2 接口实现必需——具名豁免），其余符号照常扫描。
        const bool isUiModuleTu =
            file.filename().string() == "OptUiModule.cpp";
        for (const char* symbol : kDirectAccessSymbols) {
            if (isUiModuleTu
                && std::string(symbol) == "sdurws/ird/project/") {
                continue;  // 具名豁免（§11.2 接口完整类型——非业务访问）
            }
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

// =====================================================================
// ASM-PLUG 收口批新增契约面（P-OPT-10 消账——真实 IPluginUiRegistrar
// 注册翻译的字段同构＋词表一致＋fail-fast 三面自证；任务验收标准②。
// dynamics DynPluginRegistration／selection SelPluginRegistration 同构
// ——三域一体收口）。
// =====================================================================

namespace {

/// 捕获型注册器测试替身（IPluginUiRegistrar 的测试半实现——只捕获
/// registerPluginUi 入参，不做白名单/重复校验；校验序行为由真实端口
/// 用例另行自证——替身只承担"捕获描述符供断言"职责）。
class CapturingRegistrar final : public sdurws::ird::ui::IPluginUiRegistrar {
public:
    sdurws::ird::ui::RegistrationOutcome registerPluginUi(
        const sdurws::ird::ui::PluginUiDescriptor& descriptor,
        sdurws::ird::ui::IPluginUiModule& module) override
    {
        captured = descriptor;    // 值拷贝捕获（调用方可即弃——§10.9 入参语义）
        capturedModule = &module; // 弱引用捕获（接口消费断言面）
        ++calls;
        return nextOutcome;
    }
    std::vector<sdurws::ird::ui::PluginAssemblyReport> assemblyReports()
        const override
    {
        return {};  // 替身无报告累积（报告行为归真实端口用例）
    }
    std::vector<std::string> whitelist() const override { return {}; }

    sdurws::ird::ui::PluginUiDescriptor captured;               ///< 捕获的描述符
    sdurws::ird::ui::IPluginUiModule* capturedModule = nullptr; ///< 捕获的模块弱引用
    int calls = 0;                                              ///< 登记调用计数
    sdurws::ird::ui::RegistrationOutcome nextOutcome =
        sdurws::ird::ui::RegistrationOutcome::Ok;               ///< 可配置回放值
};

}  // namespace

/**
 * 翻译字段同构＋词表一致（ASM-PLUG 验收标准②——自持描述符翻译为真实
 * ui::PluginUiDescriptor 后逐字段对账）：经注册器测试替身捕获翻译产物，
 * 断言 pluginId/titleKey/stages/capabilities/panels 与自持描述符一一对
 * 应（"字段同构无 ui 侧编译期校验"的缺口随真实编译边建立解除——字段
 * 名/类型漂移即编译错误，本用例再钉值面对应）；命令面恒空（本域描述符
 * 零命令字段——文件头注 2 诚实缺席，翻译产物不预建占位）。
 */
TEST(OptPluginRegistration, TranslatedDescriptorMatchesSelfHeldFace_ASMPLUG_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"SA-01", "UX-02"},
                  std::vector<std::string>{});

    namespace opt = sdurws::ird::optimization;
    auto assembly = opt::createOptimizationPluginAssembly();
    CapturingRegistrar registrar;

    // 激活动作：自持描述符翻译＋宿主登记（接口注入——替身捕获产物）。
    const auto outcome = opt::registerWithHostRegistrar(assembly, &registrar);
    ASSERT_EQ(outcome, sdurws::ird::ui::RegistrationOutcome::Ok)
        << "替身回放 Ok——翻译与登记动作本身不得失败";
    ASSERT_EQ(registrar.calls, 1) << "装配期一次（§11.1）";

    const auto& d = registrar.captured;
    // 身份两字段（§11.1 白名单第 7 token／§3.5 键族——逐字直拷）。
    EXPECT_EQ(d.pluginId, "optimization");
    EXPECT_EQ(d.titleKey, "plugin.optimization.title");
    // stages：恰一阶段＝挂位 token 的 ui 词表形态（§6.4 第 6——
    // "optimization" 对位 Optimization）。
    ASSERT_EQ(d.stages.size(), 1U);
    EXPECT_EQ(d.stages[0], sdurws::ird::ui::StageId::Optimization);
    // capabilities：面板非空/命令面恒 false（R1 域命令词表未随卡面登记
    // ——§9.1 R1 边界）/投影恒自报（翻译规则三声明）。
    EXPECT_TRUE(d.capabilities.providesStagePanel);
    EXPECT_FALSE(d.capabilities.registersCommands)
        << "命令面缺席＝诚实缺席（不预建占位——NFR-MNT-04）";
    EXPECT_TRUE(d.capabilities.providesReadonlyProjection);
    // commands：恒空（SA-16 命令入口权威归 ui——运行启动/取消经宿主绑
    // 定缝、候选应用经宿主编排，零私造词表）。
    EXPECT_TRUE(d.commands.empty());
    // panels：恰一条主面板（stage 词表对位/titleKey 直拷/advanced=false/
    // factory 闭包非空——宿主挂位可调用面）。
    ASSERT_EQ(d.panels.size(), 1U);
    EXPECT_EQ(d.panels[0].stage, sdurws::ird::ui::StageId::Optimization);
    EXPECT_EQ(d.panels[0].titleKey, assembly.descriptor.panels[0].titleKey);
    EXPECT_FALSE(d.panels[0].advanced);
    EXPECT_TRUE(static_cast<bool>(d.panels[0].factory));
    // 模块半区：捕获的 module 与门面 uiModule() 同一实例（§11.2 模块
    // 真身——registrar 弱引用的对象）。
    ASSERT_NE(registrar.capturedModule, nullptr);
    EXPECT_EQ(registrar.capturedModule, assembly.uiModule());
}

/**
 * 真实注册端口自证（ASM-PLUG 验收标准②④——黑盒校验序：翻译产物经
 * ui 单元 PluginUiRegistrar 实现（createPluginUiRegistrar）登记返回 Ok
 * ＋装配报告恰一条且 ok=true、面板计数与描述符一致；白名单＝ui.md
 * §11.1 八 token 编译期词表）。同时经接口消费钉扎 §11.2 模块公共交付
 * 面：激活产物 readonlyProjections() 返回单行且 domainKey＝
 * "optimization"（§6.5 汇聚对账锚）。
 */
TEST(OptPluginRegistration, ActivationPassesRealRegistrarPort_ASMPLUG_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"SA-01", "UX-14"},
                  std::vector<std::string>{});

    namespace opt = sdurws::ird::optimization;
    // 真实注册端口（实现封闭 ui 库内——工厂创建；白名单由本测试充当
    // 装配层固定——§11.1 八 token 编译期词表）。
    auto registrar = sdurws::ird::ui::createPluginUiRegistrar(
        {"modeling", "requirements", "kinematics", "trajectory",
         "dynamics", "selection", "optimization", "workflow"});
    ASSERT_NE(registrar, nullptr) << "注册端口工厂返回空（ui 库装配违约）";

    auto assembly = opt::createOptimizationPluginAssembly();
    const auto outcome = opt::registerWithHostRegistrar(assembly, registrar.get());
    EXPECT_EQ(outcome, sdurws::ird::ui::RegistrationOutcome::Ok)
        << "真实校验序三查应全过（白名单在册＋无重复＋描述符合法——命令"
           "面空集不触发命令句法查）";

    const auto reports = registrar->assemblyReports();
    ASSERT_EQ(reports.size(), 1U) << "装配报告恰一条（装配期一次）";
    EXPECT_EQ(reports[0].pluginId, "optimization");
    EXPECT_TRUE(reports[0].ok);
    EXPECT_EQ(reports[0].panelsLoaded, 1U) << "一条主面板登记记录";
    EXPECT_EQ(reports[0].commandsRegistered, 0U)
        << "命令面缺席＝零命令登记（诚实缺席——不预建占位）";
    EXPECT_TRUE(reports[0].failureDiagnostics.empty());

    // 接口消费钉扎（公共交付面——§11.2 三方法的 readonlyProjections 半
    // 区）：经 ui 接口指针调用，投影行 domainKey 恒 "optimization"（L-O1
    // 透传流经 OptUiModule 翻译——缺省会话态＝缺省行，不伪造可行性；
    // 本域行扩展呈现位不经 §6.5 汇聚——ui 冻结形状无承载，面板半区
    // 承载）。
    ASSERT_NE(assembly.uiModule(), nullptr);
    const auto rows = assembly.uiModule()->readonlyProjections();
    ASSERT_EQ(rows.size(), 1U);
    EXPECT_EQ(rows[0].domainKey, "optimization");
    EXPECT_FALSE(rows[0].inputComplete)
        << "缺省会话态＝输入不完整（缺省行不伪造可行性——ERR-01）";
    // 草稿半区：恒 nullopt（候选应用两步组合经宿主编排——域无草稿语义
    // 实现）。
    EXPECT_FALSE(assembly.uiModule()->buildDraftCommand("optimization").has_value());
}

/**
 * 无 registrar 实现即 fail-fast（ASM-PLUG 验收标准②——激活路径对空端
 * 口的防御面）：registrar 空指针＝调用方装配违约，显式异常而非静默吞、
 * 不虚构登记成功（错误归类：调用方错误 fail-fast）。
 */
TEST(OptPluginRegistration, ActivationWithoutRegistrarFailsFast_ASMPLUG_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-03"},
                  std::vector<std::string>{});

    namespace opt = sdurws::ird::optimization;
    auto assembly = opt::createOptimizationPluginAssembly();
    EXPECT_THROW(opt::registerWithHostRegistrar(assembly, nullptr),
                 std::invalid_argument)
        << "空注册端口必须 fail-fast（不静默吞、不虚构登记成功）";
    // fail-fast 后模块半区未创建（异常先于翻译/登记——装配态未被污染）。
    EXPECT_EQ(assembly.uiModule(), nullptr);
}
