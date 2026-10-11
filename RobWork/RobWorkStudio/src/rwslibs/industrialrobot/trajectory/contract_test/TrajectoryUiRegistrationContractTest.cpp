/**
 * @file   TrajectoryUiRegistrationContractTest.cpp
 * @brief  trajectory 插件界面注册契约测试（TrjUiRegistration）——装配门
 *         面的 ui 类型翻译与注册端口链路（任务契约 WP-16-T12 acceptance 1
 *         "轨迹工作流页＋曲线视图入口（经 IPluginUiRegistrar 白名单挂位）
 *         界面链路用例"的执行证明＋acceptance 2 的 T12 面 zero-本地位扫
 *         描）。
 *
 * 设计依据：
 *   - units/trajectory.md §16.1（trajectory 经 IPluginUiRegistrar::
 *     registerPluginUi(PluginUiDescriptor) 注册——白名单 token "trajectory"
 *     已占位，ui.md §11.1；挂位 StageId::TrajectoryDynamics）、§14.5.1
 *     （七命令登记面——CommandDescriptor：id 点分小写、ownerUnit=
 *     "trajectory"）、§14.5.2（DomainReadinessItem（domainKey="trajectory"）
 *     供七态汇聚）、§16.2（零修订红线——View3DSessionPoseContract 四常
 *     量同款适用于轨迹播放会话态）、§16.3（衔接缝——本批零 ui 库链接，
 *     真实 registrar 实例消费归宿主装配批：测试以接口替身承载端口协议，
 *     替身纪律＝只证明端口/状态/错误传播）；
 *   - units/ui.md §10.9（RegistrationOutcome 四值/PluginUiDescriptor 冻结
 *     形状）、§6.4（StageId 七值）、§6.5（域注册键）、§7.1（命令描述符
 *     缺省面）；
 *   - 先例：dynamics/contract_test/DynamicsPluginAssemblyContractTest.cpp
 *     （ASM-PLUG 收口批"真实注册端口消费面"同位测试——dynamics 以真实
 *     createPluginUiRegistrar 自证〔其 ui 测试边已登记〕；trajectory 本批
 *     零 ui 库链接，同位面以 RegistrarSpy 承载——校验序本体属 ui 单元
 *     已验收行为，本域只证明"翻译正确＋提交协议＋结果透传"）；
 *   - 任务契约 tasks/foundation/WP-16-T12.json acceptance 1/2。
 *
 * 零链接纪律（读代码前先读）：本 TU 消费 ui 公共头（值类型＋抽象接口
 * ——纯头消费），**不链接 sdurws_ird_ui**（单元边治理归宿主装配批——
 * CreatePluginUiRegistrar 工厂符号零消费，契约测试 T12 面扫描钉住）；
 * 替身 RegistrarSpy 实现 ui::IPluginUiRegistrar 接口（三纯虚全实现——
 * 接口契约的消费面自证）。
 */

#include <sdurws/ird/trajectory/TrajectoryPluginAssembly.hpp>

#include <sdurws/ird/trajectory/Commands.hpp>  // kCommandTokens/kTrajectorySessionContract
                                                //   （词表与四常量——T10 公共契约）

#include <sdurws/ird/ui/IPluginUiModule.hpp>   // ui::IPluginUiModule（§11.2 三方法
                                                //   完整类型——投影/草稿用例调用面）
#include <sdurws/ird/ui/View3DContract.hpp>    // ui::View3DSessionPoseContract（§16.2
                                                //   四常量对账锚——ui 侧同值面）

#include <sdurws/ird/project/CommandService.hpp> // project::CommandEnvelope 完整
                                                  //   类型（§11.2 草稿半区接口
                                                  //   消费——optional 实例化点；
                                                  //   纯头消费零 project 链接，
                                                  //   dynamics 契约测试同款）

#include "plugin/TrajPanelCommandCatalog.hpp"  // trjDomainCommands/trjReadinessDomainKey
                                                //   （命令目录——同单元私有头）
#include "plugin/TrajPanelTypes.hpp"  // TrajModuleSessionState（会话态完整类型——
                                       //   同单元私有头，投影翻译用例消费）

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯
                                                       //   登记（testkit §7.3）

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace fs = std::filesystem;

namespace {

using sdurws::ird::trajectory::TrajectoryCommandPayload;
using sdurws::ird::trajectory::kTrajectorySessionContract;
using sdurws::ird::trajectory::kTrjCurvesPageKey;
using sdurws::ird::trajectory::kTrjDomainKey;
using sdurws::ird::trajectory::kTrjStageToken;
using sdurws::ird::trajectory::kTrjWorkflowPageKey;
namespace trj = sdurws::ird::trajectory;
namespace ui = sdurws::ird::ui;

// =====================================================================
// 注册端口替身（testkit 替身纪律——只承载端口/状态/错误传播：记录收
// 到的描述符与模块指针，返回可编程的登记结果；白名单校验序本体归 ui
// 单元已验收行为，替身不复制）。
// =====================================================================

/**
 * @brief 注册端口替身（ui::IPluginUiRegistrar 三纯虚全实现——接口契约
 *        消费面自证；非 owning 记录面，值拷贝描述符）。
 */
class RegistrarSpy final : public ui::IPluginUiRegistrar {
public:
    ui::RegistrationOutcome nextOutcome =
        ui::RegistrationOutcome::Ok;                 ///< 下次登记的返回值
    std::vector<ui::PluginUiDescriptor> received;    ///< 收到的描述符（登记序）
    std::vector<ui::IPluginUiModule*> modules;       ///< 收到的模块指针（平行序）

    /**
     * @brief 登记调用记录（翻译产物捕获＋可编程结果透返——§10.9 校验序
     *        归宿主实现的替身声明）。
     */
    ui::RegistrationOutcome registerPluginUi(
        const ui::PluginUiDescriptor& descriptor,
        ui::IPluginUiModule& module) override
    {
        received.push_back(descriptor);
        modules.push_back(&module);
        return nextOutcome;
    }

    /// @brief 报告查询（本测试零消费——空实现满足接口契约）。
    std::vector<ui::PluginAssemblyReport> assemblyReports() const override
    {
        return {};
    }

    /// @brief 白名单查询（§11.1 词表运行时载体——替身承载两 token 词形）。
    std::vector<std::string> whitelist() const override
    {
        return {"trajectory", "dynamics"};
    }
};

// =====================================================================
// 源码扫描辅助（T12 面 zero-本地位扫描——判定对象是代码行为，注释内的
// 文档性提及不属行为；与 CommandsBoundaryContractTest 的注释剥离器同款
// 状态机——不用 regex，MSVC 回溯爆栈先例）。
// =====================================================================

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

/// 线性单遍注释剥离器（CommandsBoundaryContractTest 同款状态机）。
std::string stripComments(const std::string& src)
{
    enum class State { Code, LineComment, BlockComment, StrLiteral, CharLiteral };
    std::string out;
    out.reserve(src.size());
    State state = State::Code;
    for (std::size_t i = 0; i < src.size(); ++i) {
        const char ch = src[i];
        const char next = (i + 1 < src.size()) ? src[i + 1] : '\0';
        switch (state) {
        case State::Code:
            if (ch == '/' && next == '/') {
                state = State::LineComment;
                out += "  ";
                ++i;
            } else if (ch == '/' && next == '*') {
                state = State::BlockComment;
                out += "  ";
                ++i;
            } else if (ch == '"') {
                state = State::StrLiteral;
                out += ch;
            } else if (ch == '\'') {
                state = State::CharLiteral;
                out += ch;
            } else {
                out += ch;
            }
            break;
        case State::LineComment:
            if (ch == '\n') { state = State::Code; }
            out += (ch == '\n') ? '\n' : ' ';
            break;
        case State::BlockComment:
            if (ch == '*' && next == '/') {
                state = State::Code;
                out += "  ";
                ++i;
            } else {
                out += (ch == '\n') ? '\n' : ' ';
            }
            break;
        case State::StrLiteral:
            if (ch == '\\') {
                out += ch;
                if (i + 1 < src.size()) { out += src[i + 1]; }
                ++i;
            } else {
                out += ch;
                if (ch == '"') { state = State::Code; }
            }
            break;
        case State::CharLiteral:
            if (ch == '\\') {
                out += ch;
                if (i + 1 < src.size()) { out += src[i + 1]; }
                ++i;
            } else {
                out += ch;
                if (ch == '\'') { state = State::Code; }
            }
            break;
        }
    }
    return out;
}

/// 收集 T12 界面面（plugin/＋assembly/）全部 C++ 源文件路径（与
/// TrajectoryPluginAssemblyContractTest 的插件面收集同域——递归两目录）。
std::vector<fs::path> collectUiFaceFiles()
{
    std::vector<fs::path> files;
    for (const auto* sub : {"plugin", "assembly"}) {
        const auto base = unitRoot() / "trajectory" / sub;
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

}  // namespace

// =====================================================================
// 翻译面（§10.9 冻结形状逐字段——白名单挂位链路的域侧半区）
// =====================================================================

/**
 * 用例：自持描述符 → ui::PluginUiDescriptor 翻译逐字段（acceptance 1——
 * "经 IPluginUiRegistrar 白名单挂位"的描述符值面）：pluginId＝白名单
 * token、恰一挂位 StageId::TrajectoryDynamics、能力声明三位、七命令
 * id/ownerUnit/键族、一条主面板登记。词表外 stageToken fail-fast。
 */
TEST(TrjUiRegistration, TranslateFieldsMatchUiFrozenShapes_WP16T12_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-02", "UX-10"},
                  std::vector<std::string>{});

    const trj::TrajectoryPluginAssembly bundle =
        trj::createTrajectoryPluginAssembly();
    const ui::PluginUiDescriptor translated =
        trj::translatePluginUiDescriptor(bundle.descriptor);

    // ---- 身份两字段（§11.1 白名单 token／§3.5 键族——T03 登记值直拷）。 -
    EXPECT_EQ(translated.pluginId, "trajectory")
        << "pluginId 必须＝ui.md §11.1 白名单 token（ARC-04——显示名永不替代身份）";
    EXPECT_EQ(translated.titleKey, "plugin.trajectory.title")
        << "titleKey 必须＝ui.md §3.5 键族（UX-02——token 不进用户文本）";

    // ---- 覆盖阶段（恰一——§16.1 挂位 StageId::TrajectoryDynamics）。 ---
    ASSERT_EQ(translated.stages.size(), 1u) << "本域单挂位面（§6.4 第 4 阶段）";
    EXPECT_EQ(translated.stages[0], ui::StageId::TrajectoryDynamics)
        << "挂位阶段必须＝trajectory-dynamics（§16.1——轨迹工作流＋曲线视图入口页）";

    // ---- 能力声明三位（§10.9"描述性，非判定性"——按描述符现状如实）。 -
    EXPECT_TRUE(translated.capabilities.providesStagePanel)
        << "一条主面板登记——提供阶段面板";
    EXPECT_TRUE(translated.capabilities.registersCommands)
        << "七命令登记——登记命令";
    EXPECT_TRUE(translated.capabilities.providesReadonlyProjection)
        << "§11.2 模块自报域行——参与七态汇聚（UX-10）";

    // ---- 七命令（§14.5.1 词表序——id/ownerUnit/键族逐条）。 ------------
    ASSERT_EQ(translated.commands.size(), 7u)
        << "§14.5.1 七命令全集（与 Commands.hpp kCommandTokens 逐位对账）";
    for (std::size_t i = 0; i < translated.commands.size(); ++i) {
        EXPECT_EQ(translated.commands[i].id, trj::kCommandTokens[i])
            << "命令 id 必须与词表逐位一致（表行序稳定）: 序 " << i;
        EXPECT_EQ(translated.commands[i].ownerUnit, "trajectory")
            << "ownerUnit＝pluginId（§7.2 白名单校验对位值）: 序 " << i;
        EXPECT_NE(translated.commands[i].titleKey.find("cmd."), std::string::npos)
            << "titleKey 落 §3.5 cmd.<id>.title 键族: 序 " << i;
        // §7.1 缺省面（域侧零本地判定——只读门控归宿主可用性缝）。
        EXPECT_TRUE(translated.commands[i].readOnlyAllowed)
            << "七命令全部零修订会话命令——readOnlyAllowed 保持缺省 true";
        EXPECT_EQ(translated.commands[i].scope, ui::CommandScope::Session)
            << "会话命令作用域＝Session（§7.1 缺省）";
    }

    // ---- 一条主面板（工作流页＋曲线视图入口合一 Tab——§16.1）。 --------
    ASSERT_EQ(translated.panels.size(), 1u) << "本域恰一条主面板登记";
    EXPECT_EQ(translated.panels[0].stage, ui::StageId::TrajectoryDynamics);
    EXPECT_EQ(translated.panels[0].titleKey, kTrjWorkflowPageKey)
        << "面板标题键＝工作流页键（曲线视图为页半区——合一主面板）";
    EXPECT_FALSE(translated.panels[0].advanced) << "主面板位（UX-04 非 advanced）";
    ASSERT_TRUE(static_cast<bool>(translated.panels[0].factory))
        << "面板工厂非空（§10.9 描述符合法性——空工厂宿主判 InvalidDescriptor）";

    // ---- 词表外 stageToken＝装配期数据违约（fail-fast——不静默翻译）。 -
    trj::TrajectoryPluginDescriptor broken = bundle.descriptor;
    broken.stageToken = "not-a-stage";
    EXPECT_THROW(trj::translatePluginUiDescriptor(broken), std::invalid_argument)
        << "词表外挂位 token 必须 fail-fast（调用方错误——宿主挂位断链显式失败）";
}

// =====================================================================
// 注册链路（§10.9 装配期一次——端口协议与结果透传）
// =====================================================================

/**
 * 用例：registerWithHostRegistrar 提交链路（acceptance 1——"界面链路用
 * 例"的端口半区）：翻译产物原样到达注册端口、模块半区同批提交、返回
 * Ok；重复激活复用同模块实例（registrar 弱引用稳定）。
 */
TEST(TrjUiRegistration, RegisterSubmitsDescriptorToHostRegistrar_WP16T12_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-02"},
                  std::vector<std::string>{});

    trj::TrajectoryPluginAssembly bundle = trj::createTrajectoryPluginAssembly();
    RegistrarSpy spy;

    const ui::RegistrationOutcome outcome =
        trj::registerWithHostRegistrar(bundle, &spy);
    EXPECT_EQ(outcome, ui::RegistrationOutcome::Ok) << "替身 Ok——如实透传";

    // ---- 提交面：恰一次登记、翻译产物逐字段原样（域侧零改写）。 -------
    ASSERT_EQ(spy.received.size(), 1u) << "§10.9 装配期一次";
    EXPECT_EQ(spy.received[0].pluginId, "trajectory");
    ASSERT_EQ(spy.received[0].panels.size(), 1u);
    EXPECT_EQ(spy.received[0].stages[0], ui::StageId::TrajectoryDynamics);
    EXPECT_EQ(spy.received[0].commands.size(), 7u);

    // ---- 模块半区：§11.2 接口实例同批提交（激活路径创建）。 ------------
    ASSERT_EQ(spy.modules.size(), 1u);
    EXPECT_EQ(spy.modules[0], bundle.uiModule())
        << "登记的模块半区＝门面 uiModule()（同一实例——弱引用稳定性锚）";
    EXPECT_NE(bundle.uiModule(), nullptr) << "激活即创建 §11.2 模块半区";

    // ---- 重复激活：复用同模块实例（重入防御——registrar 弱引用不悬垂）。 -
    const ui::RegistrationOutcome second =
        trj::registerWithHostRegistrar(bundle, &spy);
    EXPECT_EQ(second, ui::RegistrationOutcome::Ok);
    ASSERT_EQ(spy.modules.size(), 2u);
    EXPECT_EQ(spy.modules[1], spy.modules[0]) << "模块半区复用同实例";
}

/**
 * 用例：登记结果三非 Ok 值如实透传（§10.9 校验序归宿主实现——域侧零
 * 本地判定、零改写、零吞没；§11.3 失败隔离归宿主呈现）。
 */
TEST(TrjUiRegistration, RegisterOutcomePassthroughZeroLocalVerdict_WP16T12_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-02"},
                  std::vector<std::string>{});

    for (const ui::RegistrationOutcome forced :
         {ui::RegistrationOutcome::NotWhitelisted,
          ui::RegistrationOutcome::DuplicatePlugin,
          ui::RegistrationOutcome::InvalidDescriptor}) {
        trj::TrajectoryPluginAssembly bundle = trj::createTrajectoryPluginAssembly();
        RegistrarSpy spy;
        spy.nextOutcome = forced;
        const ui::RegistrationOutcome outcome =
            trj::registerWithHostRegistrar(bundle, &spy);
        EXPECT_EQ(outcome, forced)
            << "非 Ok 结果必须逐值透传（域侧零本地判定——校验序全归宿主）";
    }
}

/**
 * 用例：空 registrar 指针 fail-fast（调用方装配违约——宿主装配批次必
 * 传端口；不静默吞、不虚构登记成功）。
 */
TEST(TrjUiRegistration, NullRegistrarFailsFast_WP16T12_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-02"},
                  std::vector<std::string>{});

    trj::TrajectoryPluginAssembly bundle = trj::createTrajectoryPluginAssembly();
    EXPECT_THROW(trj::registerWithHostRegistrar(bundle, nullptr),
                 std::invalid_argument)
        << "空注册端口＝装配程序缺陷，fail-fast（错误归类：调用方错误）";
}

// =====================================================================
// 零修订契约对账锚（§16.2——四常量同值的编译单元内对账）
// =====================================================================

/**
 * 用例：kTrajectorySessionContract 与 ui::View3DSessionPoseContract 四常
 * 量逐位同值（§16.2 机器可断言面——两侧各以文档钉住值字面冻结，本编
 * 译单元同时消费两侧类型做运行时对账；任一侧漂移即本用例失败＝必须
 * 走需求/单元卡修订，代码不得私改）。
 */
TEST(TrjUiRegistration, SessionContractAnchoredToUiView3DContract_WP16T12)
{
    IRD_TEST_INFO(std::vector<std::string>{"AT-04", "KIN-06"},
                  std::vector<std::string>{});

    // ui 侧契约（头内默认成员值——ui.md §16.7 冻结：KIN-06/AT-04 钉住；
    // 零链接消费：值初始化不触碰 ui 库符号）。
    const ui::View3DSessionPoseContract uiContract{};

    // 四常量逐位对账（sessionStateOnly/writesDesignModel/producesRevision/
    // invalidatesResults——轨迹播放会话态与 View3D 会话姿态同款语义）。
    EXPECT_EQ(kTrajectorySessionContract.sessionStateOnly,
              uiContract.sessionStateOnly)
        << "sessionStateOnly 对账（KIN-06——会话姿态零修订语义族）";
    EXPECT_EQ(kTrajectorySessionContract.writesDesignModel,
              uiContract.writesDesignModel)
        << "writesDesignModel 对账（ARC-04——模型快照零触）";
    EXPECT_EQ(kTrajectorySessionContract.producesRevision,
              uiContract.producesRevision)
        << "producesRevision 对账（AT-04——动画/导出零修订）";
    EXPECT_EQ(kTrajectorySessionContract.invalidatesResults,
              uiContract.invalidatesResults)
        << "invalidatesResults 对账（CON-02——当前性正交）";
    // trajectory 侧钉住值自身复核（缺省构造成员值——改动即编译失败面
    // 的运行时复核，双保险）。
    EXPECT_TRUE(kTrajectorySessionContract.sessionStateOnly);
    EXPECT_FALSE(kTrajectorySessionContract.writesDesignModel);
    EXPECT_FALSE(kTrajectorySessionContract.producesRevision);
    EXPECT_FALSE(kTrajectorySessionContract.invalidatesResults);
}

// =====================================================================
// 投影与草稿半区（§11.2 模块三方法——七态汇聚供给面）
// =====================================================================

/**
 * 用例：readonlyProjections 投影行（§14.5.2——DomainReadinessItem
 * domainKey="trajectory" 供七态汇聚；会话态四呈现字段纯透传，零判定
 * 零缓存）；buildDraftCommand 恒 nullopt（§14.5.1 七命令修订列全部
 * "无"——域无草稿语义，零修订 AT-04）。
 */
TEST(TrjUiRegistration, ModuleProjectionAndDraftHalf_WP16T12_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-10", "AT-04"},
                  std::vector<std::string>{});

    trj::TrajectoryPluginAssembly bundle = trj::createTrajectoryPluginAssembly();
    RegistrarSpy spy;
    ASSERT_EQ(trj::registerWithHostRegistrar(bundle, &spy),
              ui::RegistrationOutcome::Ok);
    ui::IPluginUiModule* module = bundle.uiModule();
    ASSERT_NE(module, nullptr);

    // ---- 投影翻译（会话态注入→投影行逐字段——域只供数，呈现映射归 ui）。 -
    trj::TrajModuleSessionState& state = bundle.session();
    state.epoch = 3;
    state.verdict = sdurws::ird::core::EngineeringStatus::DataInsufficient;
    state.inputComplete = false;
    state.missingItemKeys = {"req.trj.start-state", "req.trj.target"};
    state.hasActiveTask = true;

    const std::vector<ui::DomainReadinessItem> rows = module->readonlyProjections();
    ASSERT_EQ(rows.size(), 1u) << "单行——trajectory 域行";
    EXPECT_EQ(rows[0].domainKey, kTrjDomainKey)
        << "domainKey 必须＝\"trajectory\"（§6.5 域注册词表——宿主汇聚对账锚）";
    EXPECT_EQ(rows[0].verdict,
              sdurws::ird::core::EngineeringStatus::DataInsufficient)
        << "verdict 透传（core 词表直用——零判定）";
    EXPECT_FALSE(rows[0].inputComplete) << "inputComplete 透传（UX-10）";
    EXPECT_EQ(rows[0].missingItemKeys.size(), 2u) << "缺项清单透传（UX-10）";
    EXPECT_TRUE(rows[0].hasActiveTask) << "在途任务事实透传（\"计算中\"素材）";

    // ---- 草稿半区：恒 nullopt（域内外一致——不产生空修订）。 -----------
    EXPECT_FALSE(module->buildDraftCommand(kTrjDomainKey).has_value())
        << "域内 moduleId 亦 nullopt（§14.5.1 七命令零修订——无草稿工作集）";
    EXPECT_FALSE(module->buildDraftCommand("elsewhere").has_value())
        << "域外请求 nullopt（§11.2 参数语义）";
}

// =====================================================================
// T12 面 zero-本地位扫描（acceptance 2——白名单权威归宿主的结构承载）
// =====================================================================

/**
 * 用例：T12 界面面（plugin/＋assembly/）源码扫描——真实 registrar 工厂
 * 零消费（createPluginUiRegistrar 归宿主装配批——本域零构造）；登记结
 * 果三非 Ok 值零本地判定（校验序全归宿主实现——透传面只搬运返回值）。
 * 剥注释后扫描（文档性提及不是行为）。
 */
TEST(TrjUiRegistration, T12FacesZeroRegistrarConstructionAndVerdict_WP16T12_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-02"},
                  std::vector<std::string>{});

    const std::vector<fs::path> files = collectUiFaceFiles();
    ASSERT_FALSE(files.empty()) << "界面面无源文件（落位形态缺失）";

    // 禁词表（代码面——英文标识符；注释内文档性提及剥离后不命中）：
    //   - createPluginUiRegistrar：真实 registrar 工厂——宿主装配批消费
    //     面（ui 库符号；本域零链接零构造——出现即 ui 边越权）；
    //   - NotWhitelisted/DuplicatePlugin/InvalidDescriptor：§10.9 校验序
    //     的三非 Ok 判定值——域侧零本地判定（透传面零比较零改写；代码
    //     中出现＝本地复判宿主权威，违约）。
    const std::vector<std::string> forbidden{
        "createPluginUiRegistrar", "NotWhitelisted", "DuplicatePlugin",
        "InvalidDescriptor"};
    for (const fs::path& file : files) {
        const std::string code = stripComments(readFile(file));
        for (const std::string& token : forbidden) {
            EXPECT_EQ(code.find(token), std::string::npos)
                << "T12 界面面出现宿主权威符号（白名单校验/真实 registrar "
                   "工厂归宿主装配批——§16.3 衔接缝）: " << token << " @ "
                   << file.string();
        }
    }
}

// =====================================================================
// 命令目录与词表对账（§14.5.1——登记面与运行期词表单一数据源）
// =====================================================================

/**
 * 用例：trjDomainCommands 目录产物与 Commands.hpp kCommandTokens 逐位
 * 一致（登记面＝token 词表的唯一投影——表外命令零登记；titleKey 键族
 * 派生无漂移）。
 */
TEST(TrjUiRegistration, CommandCatalogMatchesTokenWordlist_WP16T12_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-02"},
                  std::vector<std::string>{});

    const std::vector<trj::TrjCommandDescriptor> catalog = trj::trjDomainCommands();
    ASSERT_EQ(catalog.size(), trj::kCommandTokens.size())
        << "目录条数＝词表条数（§14.5.1 七命令全集）";
    for (std::size_t i = 0; i < catalog.size(); ++i) {
        EXPECT_EQ(catalog[i].token, trj::kCommandTokens[i])
            << "目录 token 与词表逐位一致（表行序稳定）: 序 " << i;
        EXPECT_EQ(catalog[i].titleKey,
                  "cmd." + catalog[i].token + ".title")
            << "titleKey 键族派生（§3.5——零字面复制）: 序 " << i;
    }
    EXPECT_EQ(trj::trjReadinessDomainKey(), kTrjDomainKey)
        << "域注册键＝词表常量便利形态（§6.5）";
    EXPECT_EQ(std::string(kTrjStageToken), "trajectory-dynamics")
        << "挂位 token 词表值（§6.4 第 4 阶段——StageId::TrajectoryDynamics）";
    EXPECT_EQ(std::string(kTrjCurvesPageKey),
              "plugin.trajectory.panel.curves.title")
        << "曲线视图页键（§3.5 键族——合一主面板页半区）";
}
