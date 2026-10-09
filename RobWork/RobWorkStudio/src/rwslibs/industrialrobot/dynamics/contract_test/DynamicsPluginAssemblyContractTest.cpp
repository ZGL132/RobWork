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
#include <sdurws/ird/dynamics/Commands.hpp>

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯登记（testkit §7.3）

// ASM-PLUG 收口批（P-DYN-8 消账）——真实注册面契约的消费头：
// 注册端口/模块接口/词表类型的公共面（跨单元测试链接边已登记
// IRD_TEST_TARGET_EDGES——sdurws_ird_dynamics_contract_test→sdurws_ird_ui）。
#include <sdurws/ird/ui/ICommandRegistry.hpp>    // ui::CommandDescriptor 完整类型（vector 成员析构）
#include <sdurws/ird/ui/IPluginUiModule.hpp>     // ui::IPluginUiModule（接口消费——捕获指针调 readonlyProjections）
#include <sdurws/ird/ui/IPluginUiRegistrar.hpp>  // ui::IPluginUiRegistrar/createPluginUiRegistrar/RegistrationOutcome
#include <sdurws/ird/ui/UiTypes.hpp>             // ui::StageId（七值词表——token 翻译对账）
#include <sdurws/ird/project/CommandService.hpp> // project::CommandEnvelope 完整类型（§11.2 草稿半区接口消费——
                                                 //   optional 返回值构造/析构实例化要求；execution→project
                                                 //   公共链传染可达，R-2 公共面合规）

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

    const auto bundle = sdurws::ird::dynamics::createDynamicsPluginAssembly();
    EXPECT_EQ(bundle.descriptor.pluginId, "dynamics")
        << "pluginId 必须＝ui.md §11.1 白名单 token（显示名永不替代身份——ARC-04）";
    EXPECT_EQ(bundle.descriptor.titleKey, "plugin.dynamics.title")
        << "titleKey 必须＝ui.md §3.5 键族 plugin.<id>.title（UX-02：值归 ui 文案资源）";

    // 纯值工厂：两次调用产生同值实例（无共享状态——文件头线程模型注）。
    const auto again = sdurws::ird::dynamics::createDynamicsPluginAssembly();
    EXPECT_EQ(again.descriptor.pluginId, bundle.descriptor.pluginId);
    EXPECT_EQ(again.descriptor.titleKey, bundle.descriptor.titleKey);
}

/**
 * 插件零计算红线·词表扫描（acceptance 2——卡 §9.5"UI 线程不得执行动力
 * 学计算"机器可断言面）：插件面（plugin/＋assembly/）全部翻译单元不含
 * RNEA/仿真/统计词表符号——插件只提交任务/呈现投影/查表定位，动力学
 * 计算唯一在计算库（二分结构的插件半区边界；ird_gates 第 4 步不扫
 * plugin/——本用例补齐该域的红线执行面）。
 *
 * ASM-PLUG 收口批具名豁免（诚实登记）：DynUiModule.hpp/.cpp 对
 * "Envelope" 单符号豁免——§11.2 冻结接口 buildDraftCommand 返回值元素
 * 类型名（project 命令信封）含该词面子串，类型名是 override 签名的强
 * 制面；语义与动力学工况包络统计零关涉（豁免理由与红线不松动论证见
 * 下方具名豁免半区用例 PluginFaceComputationScanUiModuleExemption_
 * ASMPLUG_ACC2——两用例同规则互为防线）。
 */
TEST(DynPluginAssembly, PluginFaceHasZeroComputationSymbols_WP17T02_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-MNT-01"},
                  std::vector<std::string>{"DYN-01", "DYN-03", "DYN-05", "DYN-08"});

    // 具名豁免（与豁免半区用例同规则——只对 DynUiModule 两文件放行
    // "Envelope" 单符号，其余符号与其余文件全量扫描）。
    constexpr std::array<const char*, 2> kUiModuleTus{
        "DynUiModule.hpp", "DynUiModule.cpp"};

    const auto files = collectPluginFaceFiles();
    ASSERT_FALSE(files.empty()) << "插件面无源文件（落位形态缺失）";

    for (const auto& file : files) {
        const std::string name = file.filename().string();
        const bool isUiModuleTu = std::any_of(
            std::begin(kUiModuleTus), std::end(kUiModuleTus),
            [&name](const char* n) { return name == n; });
        const std::string text = readFile(file);
        for (const char* symbol : kComputationSymbols) {
            if (isUiModuleTu && std::string(symbol) == "Envelope") {
                continue;  // 具名豁免（§11.2 接口类型名字面——见半区用例注）
            }
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
 *
 * ASM-PLUG 收口批具名豁免（诚实登记）：plugin/DynUiModule.cpp 对
 * "sdurws/ird/project/" 一项豁免——ui.md §11.2 冻结接口
 * IPluginUiModule::buildDraftCommand 的返回值 optional 携带 project 信封
 * 类型，`return std::nullopt` 的构造/析构实例化要求该元素完整类型，实现
 * TU 必须自含该公共头（modeling/requirements/kinematics/workflow 四先例
 * 的 UiModule 实现同款包含面）。豁免面仅该头包含行：DynUiModule 零
 * project 业务访问（不调用任何 project 实现符号——本函数恒返回空语义），
 * 红线语义（项目访问唯一经端口）不松动；该 TU 对其余符号与其余插件面
 * 文件全量扫描不变。
 */
TEST(DynPluginAssembly, PluginFaceHasNoDirectFileOrProjectAccess_WP17T02_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"ARC-02"},
                  std::vector<std::string>{});

    // 具名豁免清单（文件名 → 豁免符号）：ASM-PLUG 收口批唯一一条——
    // §11.2 接口签名的完整类型要求（见用例注）。新增豁免须先在此登记
    // 理由，不允许静默放宽。
    constexpr std::array<const char*, 1> kUiModuleHeaderOnly{
        "sdurws/ird/project/"};

    const auto files = collectPluginFaceFiles();
    ASSERT_FALSE(files.empty()) << "插件面无源文件（落位形态缺失）";

    for (const auto& file : files) {
        const std::string text = readFile(file);
        // 豁免判定：DynUiModule.cpp 仅放行 "sdurws/ird/project/" 一项
        // （接口实现必需——具名豁免），其余符号照常扫描。
        const bool isUiModuleTu =
            file.filename().string() == "DynUiModule.cpp";
        for (const char* symbol : kDirectAccessSymbols) {
            if (isUiModuleTu && symbol == kUiModuleHeaderOnly[0]) {
                continue;  // 具名豁免（§11.2 接口完整类型——非业务访问）
            }
            EXPECT_EQ(text.find(symbol), std::string::npos)
                << "插件面出现直接访问符号（文件/项目访问唯一经端口——"
                   "卡 §9.1）: " << symbol << " @ " << file.string();
        }
    }
}

/**
 * 插件零计算红线·计算词表扫描的具名豁免半区（ASM-PLUG 收口批——诚实
 * 登记，与直接访问扫描的具名豁免同构）：DynUiModule 两文件的
 * "Envelope" 命中面＝ui.md §11.2 冻结接口 IPluginUiModule::
 * buildDraftCommand 的返回值元素类型名（project 命令信封——其字面为
 * "…CommandEnvelope"，含词面子串）——该类型属 ui 装配接口词汇，与动力
 * 学工况包络统计语义零关涉；类型名是 override 签名的强制面（§11.2 冻
 * 结签名逐一同形），不可改名规避。词表扫描的红线语义（动力学计算唯一
 * 在计算库/worker）不松动：DynUiModule 零包络统计行为（恒 nullopt 草
 * 稿半区＋L-D1 透传投影），其余符号对其余插件面文件全量扫描不变。
 */
TEST(DynPluginAssembly, PluginFaceComputationScanUiModuleExemption_ASMPLUG_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-MNT-01"},
                  std::vector<std::string>{"DYN-08"});

    // 具名豁免（文件 → 豁免符号）：§11.2 接口签名的类型名字面（见用例
    // 注）；豁免仅对 DynUiModule.hpp/.cpp 两文件的 "Envelope" 单符号。
    constexpr std::array<const char*, 2> kUiModuleTus{
        "DynUiModule.hpp", "DynUiModule.cpp"};
    const char* exemptSymbol = "Envelope";

    const auto files = collectPluginFaceFiles();
    ASSERT_FALSE(files.empty()) << "插件面无源文件（落位形态缺失）";

    for (const auto& file : files) {
        const std::string name = file.filename().string();
        const bool isUiModuleTu = std::any_of(
            std::begin(kUiModuleTus), std::end(kUiModuleTus),
            [&name](const char* n) { return name == n; });
        const std::string text = readFile(file);
        for (const char* symbol : kComputationSymbols) {
            if (isUiModuleTu && std::string(symbol) == exemptSymbol) {
                continue;  // 具名豁免（§11.2 接口类型名字面——非计算语义）
            }
            EXPECT_EQ(text.find(symbol), std::string::npos)
                << "插件面出现计算词表符号（卡 §9.5——RNEA/仿真/统计唯一"
                   "在计算库/worker）: " << symbol << " @ " << file.string();
        }
    }
}

/**
 * 装配描述符承载完整登记面（WP-17-T09 acceptance 1——T02 两字段逐字
 * 保留＋挂位/域键/命令/面板登记面的值面断言）：工作流页＋曲线视图
 * 合一主面板恰一条登记记录（advanced=false），五命令描述符与 §9.5 词表
 * 逐位一致，键族形态 cmd.<token>.title（ui.md §3.5 派生规则单一）。
 */
TEST(DynPluginAssembly, DescriptorCarriesRegistrationFace_WP17T09_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-02", "UX-10", "DYN-08"},
                  std::vector<std::string>{"AT-04"});

    namespace dyn = sdurws::ird::dynamics;
    const auto bundle = dyn::createDynamicsPluginAssembly();

    // T02 登记值逐字保留（WP-17-T02 契约测试同口径——扩展不漂移）。
    EXPECT_EQ(bundle.descriptor.pluginId, "dynamics");
    EXPECT_EQ(bundle.descriptor.titleKey, "plugin.dynamics.title");

    // 挂位阶段/域注册键（ui.md §6.4 七阶段第 4 token／§6.5 域注册键）。
    EXPECT_EQ(bundle.descriptor.stageToken, "trajectory-dynamics");
    EXPECT_EQ(bundle.descriptor.readinessDomainKey, "dynamics");

    // 命令登记面：五条、与 §9.5 词表逐位一致（行序＝表行序）、键族派生。
    ASSERT_EQ(bundle.descriptor.commands.size(), dyn::kCommandTokens.size());
    for (std::size_t i = 0; i < bundle.descriptor.commands.size(); ++i) {
        EXPECT_EQ(bundle.descriptor.commands[i].token, dyn::kCommandTokens[i])
            << "token 与词表逐位一致（跨版本命令契约——改动即断链）";
        EXPECT_EQ(bundle.descriptor.commands[i].titleKey,
                  "cmd." + std::string(dyn::kCommandTokens[i]) + ".title")
            << "标题键＝键族派生（值归宿主文案资源——UX-02 零文案值）";
    }

    // 面板登记面：恰一条主面板记录（工作流页＋曲线视图合一 Tab——
    // 同数据双实例的呈现漂移防御，见 assembly 头"面板面形态"）。
    ASSERT_EQ(bundle.descriptor.panels.size(), 1u);
    EXPECT_EQ(bundle.descriptor.panels[0].stageToken, "trajectory-dynamics");
    EXPECT_EQ(bundle.descriptor.panels[0].titleKey,
              "plugin.dynamics.panel.workflow.title");
    EXPECT_FALSE(bundle.descriptor.panels[0].advanced)
        << "主面板位（UX-04 非 advanced——高级位本域无第二面板）";
    EXPECT_TRUE(static_cast<bool>(bundle.descriptor.panels[0].factory))
        << "面板工厂闭包非空（§10.9 InvalidDescriptor 防御面）";
}

/**
 * 白名单 token 对账（WP-17-T09 acceptance 1"白名单挂位"的词表半区）：
 * pluginId 落于 ui.md §11.1 八 token 编译期词表（测试自持词表常量对账
 * ——真实运行时载体＝宿主注册端口的 whitelist()，本单元零 ui 编译边
 * 〔登记缺口＝单元卡 P-DYN-8，归宿主装配批次收口〕；两侧同源文档
 * §11.1——值漂移即本用例或 ui 侧词表用例失败）。
 */
TEST(DynPluginAssembly, PluginIdAlignsHostWhitelistVocabulary_WP17T09_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-02"},
                  std::vector<std::string>{});

    namespace dyn = sdurws::ird::dynamics;
    // ui.md §11.1 八 token 词表（静态白名单——宿主 pluginUiWhitelist
    // 与注册端口 whitelist() 的同源值；测试侧自持对账锚）。
    constexpr std::array<const char*, 8> kUiWhitelist{
        "modeling", "requirements", "kinematics", "trajectory",
        "dynamics", "selection", "optimization", "workflow"};

    const auto descriptor = dyn::createDynamicsPluginAssembly();
    bool whitelisted = false;
    for (const char* token : kUiWhitelist) {
        whitelisted = whitelisted || descriptor.descriptor.pluginId == token;
    }
    EXPECT_TRUE(whitelisted)
        << "pluginId 必须落于 ui.md §11.1 白名单词表（白名单外注册在宿主"
           "端口被拒——NotWhitelisted）";
    // 挂位阶段 token 亦属宿主词表（§6.4 七阶段——挂位断链防御）。
    const char* stage = descriptor.descriptor.stageToken.c_str();
    EXPECT_STREQ(stage, "trajectory-dynamics");
}

// =====================================================================
// ASM-PLUG 收口批新增契约面（P-DYN-8 消账——真实 IPluginUiRegistrar 注册
// 翻译的字段同构＋词表一致＋fail-fast 三面自证；任务验收标准②）。
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
        captured = descriptor;   // 值拷贝捕获（调用方可即弃——§10.9 入参语义）
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

    sdurws::ird::ui::PluginUiDescriptor captured;              ///< 捕获的描述符
    sdurws::ird::ui::IPluginUiModule* capturedModule = nullptr; ///< 捕获的模块弱引用
    int calls = 0;                                              ///< 登记调用计数
    sdurws::ird::ui::RegistrationOutcome nextOutcome =
        sdurws::ird::ui::RegistrationOutcome::Ok;               ///< 可配置回放值
};

}  // namespace

/**
 * 翻译字段同构＋词表一致（ASM-PLUG 验收标准②——自持描述符翻译为真实
 * ui::PluginUiDescriptor 后逐字段对账）：经注册器测试替身捕获翻译产物，
 * 断言 pluginId/titleKey/stages/capabilities/commands/panels 六面与自持
 * 描述符一一对应（"字段同构无 ui 侧编译期校验"的缺口随真实编译边建立
 * 解除——字段名/类型漂移即编译错误，本用例再钉值面对应）。
 */
TEST(DynPluginRegistration, TranslatedDescriptorMatchesSelfHeldFace_ASMPLUG_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"SA-01", "UX-02", "DYN-08"},
                  std::vector<std::string>{});

    namespace dyn = sdurws::ird::dynamics;
    auto assembly = dyn::createDynamicsPluginAssembly();
    CapturingRegistrar registrar;

    // 激活动作：自持描述符翻译＋宿主登记（接口注入——替身捕获产物）。
    const auto outcome = dyn::registerWithHostRegistrar(assembly, &registrar);
    ASSERT_EQ(outcome, sdurws::ird::ui::RegistrationOutcome::Ok)
        << "替身回放 Ok——翻译与登记动作本身不得失败";
    ASSERT_EQ(registrar.calls, 1) << "装配期一次（§11.1）";

    const auto& d = registrar.captured;
    // 身份两字段（§11.1 白名单 token／§3.5 键族——逐字直拷）。
    EXPECT_EQ(d.pluginId, "dynamics");
    EXPECT_EQ(d.titleKey, "plugin.dynamics.title");
    // stages：恰一阶段＝挂位 token 的 ui 词表形态（§6.4 第 4——
    // "trajectory-dynamics" 对位 TrajectoryDynamics）。
    ASSERT_EQ(d.stages.size(), 1U);
    EXPECT_EQ(d.stages[0], sdurws::ird::ui::StageId::TrajectoryDynamics);
    // capabilities：面板非空/命令非空/投影恒自报（翻译规则三声明）。
    EXPECT_TRUE(d.capabilities.providesStagePanel);
    EXPECT_TRUE(d.capabilities.registersCommands);
    EXPECT_TRUE(d.capabilities.providesReadonlyProjection);
    // commands：五条逐字段对账（id＝token、owner＝pluginId、titleKey 键族
    // ——§7.2 owner 白名单校验的对位值）。
    ASSERT_EQ(d.commands.size(), assembly.descriptor.commands.size());
    for (std::size_t i = 0; i < d.commands.size(); ++i) {
        EXPECT_EQ(d.commands[i].id, assembly.descriptor.commands[i].token);
        EXPECT_EQ(d.commands[i].ownerUnit, "dynamics");
        EXPECT_EQ(d.commands[i].titleKey,
                  assembly.descriptor.commands[i].titleKey);
    }
    // panels：恰一条主面板（stage 词表对位/titleKey 直拷/advanced=false/
    // factory 闭包非空——宿主挂位可调用面）。
    ASSERT_EQ(d.panels.size(), 1U);
    EXPECT_EQ(d.panels[0].stage, sdurws::ird::ui::StageId::TrajectoryDynamics);
    EXPECT_EQ(d.panels[0].titleKey,
              assembly.descriptor.panels[0].titleKey);
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
 * ＋装配报告恰一条且 ok=true、面板/命令计数与描述符一致；白名单＝ui.md
 * §11.1 八 token 编译期词表）。同时经接口消费钉扎 §11.2 模块公共交付面：
 * 激活产物 readonlyProjections() 返回单行且 domainKey＝"dynamics"（§6.5
 * 汇聚对账锚）。
 */
TEST(DynPluginRegistration, ActivationPassesRealRegistrarPort_ASMPLUG_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"SA-01", "UX-14"},
                  std::vector<std::string>{});

    namespace dyn = sdurws::ird::dynamics;
    // 真实注册端口（实现封闭 ui 库内——工厂创建；白名单由本测试充当
    // 装配层固定——§11.1 八 token 编译期词表）。
    auto registrar = sdurws::ird::ui::createPluginUiRegistrar(
        {"modeling", "requirements", "kinematics", "trajectory",
         "dynamics", "selection", "optimization", "workflow"});
    ASSERT_NE(registrar, nullptr) << "注册端口工厂返回空（ui 库装配违约）";

    auto assembly = dyn::createDynamicsPluginAssembly();
    const auto outcome = dyn::registerWithHostRegistrar(assembly, registrar.get());
    EXPECT_EQ(outcome, sdurws::ird::ui::RegistrationOutcome::Ok)
        << "真实校验序三查应全过（白名单在册＋无重复＋描述符合法）";

    const auto reports = registrar->assemblyReports();
    ASSERT_EQ(reports.size(), 1U) << "装配报告恰一条（装配期一次）";
    EXPECT_EQ(reports[0].pluginId, "dynamics");
    EXPECT_TRUE(reports[0].ok);
    EXPECT_EQ(reports[0].panelsLoaded, 1U) << "一条主面板登记记录";
    EXPECT_EQ(reports[0].commandsRegistered,
              assembly.descriptor.commands.size()) << "五命令全量登记";
    EXPECT_TRUE(reports[0].failureDiagnostics.empty());

    // 接口消费钉扎（公共交付面——§11.2 三方法的 readonlyProjections 半
    // 区）：经 ui 接口指针调用，投影行 domainKey 恒 "dynamics"（L-D1
    // 透传流经 DynUiModule 翻译——缺省会话态＝缺省行，不伪造可行性）。
    ASSERT_NE(assembly.uiModule(), nullptr);
    const auto rows = assembly.uiModule()->readonlyProjections();
    ASSERT_EQ(rows.size(), 1U);
    EXPECT_EQ(rows[0].domainKey, "dynamics");
    EXPECT_FALSE(rows[0].inputComplete)
        << "缺省会话态＝输入不完整（缺省行不伪造可行性——ERR-01）";
    // 草稿半区：恒 nullopt（§9.5 五命令零修订——域无草稿语义实现）。
    EXPECT_FALSE(assembly.uiModule()->buildDraftCommand("dynamics").has_value());
}

/**
 * 无 registrar 实现即 fail-fast（ASM-PLUG 验收标准②——激活路径对空端
 * 口的防御面）：registrar 空指针＝调用方装配违约，显式异常而非静默吞、
 * 不虚构登记成功（错误归类：调用方错误 fail-fast）。
 */
TEST(DynPluginRegistration, ActivationWithoutRegistrarFailsFast_ASMPLUG_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-03"},
                  std::vector<std::string>{});

    namespace dyn = sdurws::ird::dynamics;
    auto assembly = dyn::createDynamicsPluginAssembly();
    EXPECT_THROW(dyn::registerWithHostRegistrar(assembly, nullptr),
                 std::invalid_argument)
        << "空注册端口必须 fail-fast（不静默吞、不虚构登记成功）";
    // fail-fast 后模块半区未创建（异常先于翻译/登记——装配态未被污染）。
    EXPECT_EQ(assembly.uiModule(), nullptr);
}
