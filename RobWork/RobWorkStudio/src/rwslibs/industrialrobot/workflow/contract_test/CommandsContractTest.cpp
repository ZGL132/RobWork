/**
 * @file   CommandsContractTest.cpp
 * @brief  命令集注册与工业高频命令的契约测试（WF-VER-222/223/226——
 *         units/workflow.md §8.3/§8.4/§11.2；与真实 ui CommandRegistry＋
 *         真实 project store 联合的端到端契约面）。
 *
 * 设计依据：
 *   - units/workflow.md §8.3（最小命令集注册与工业高频命令——本单元在装
 *     配期经 ui ICommandRegistry 注册端口登记；注册设施归 ui——SA-16；
 *     ≥10 条经面板模糊搜索可达〔UX-13/F5〕；复位关节零修订〔KIN-06〕；
 *     产生修订的命令经①端口）、§8.4（P-WF-3 会话级最小语义）、§11.2
 *     （WF-VER-222 命令集注册与可达〔重复绑定注册边界拒绝＝ui 设施行
 *     为〕；WF-VER-223 复位关节零修订；WF-VER-226 撤销/重做主线——新修
 *     订、历史不改写、空历史稳定提示）
 *   - REQUIREMENTS.md §18 UX-13 原文（命令面板 ≥10 条＋模糊搜索＋快捷键
 *     契约：应用级快捷键统一注册、重复绑定在注册边界拒绝并给诊断、插件
 *     不得私占全局快捷键、未绑定命令经面板模糊搜索可达）、KIN-06、
 *     PM-18/AT-29（撤销/重做——新修订、历史不改写）
 *   - ARCHITECTURE.md §7.11（CommandRegistry/HotkeyBindingTable 收归 ui；
 *     会话态命令不产生修订；产生修订的命令一律经①命令端口）
 *   - ui.md §7.1（最小命令集冻结表——描述符字段权威）、§7.2（注册协议：
 *     重复 id 拒绝＋UI-CMD-DUPLICATE 不覆盖不静默）、§7.4（模糊搜索：
 *     子序列匹配大小写不敏感＋关键字命中加权）
 *
 * 契约测试形态（§11.0——"契约测试＝跨单元联合"）：
 *   - **真实 ui CommandRegistry**：createCommandRegistry 真实设施（owner
 *     白名单 {"ui","workflow"}——workflow 为 ui.md §11.1 静态白名单第 8
 *     token），贡献清单经 registerWorkflowCommands 真实注册；命令面板模
 *     糊搜索经真实 paletteSnapshot（子序列匹配＋过渡中文命中——UI-T06
 *     键值分离过渡期承载）。
 *   - **真实 project store**：ProjectStoreFactory::createNew 真实落盘；
 *     修订端口桥＝L5 装配桥的测试等价物（诚实登记——桥接真实
 *     UndoRedoService/命令服务；applyDraft 的域信封组装归宿主装配任务，
 *     桥诚实返回未装配态）；种子命令为测试替身（BaselineSeedHandler 同
 *     款先例——机制真值全在 project 生产代码）。
 *   - **插件目标链接**：本文件消费 plugin 目录的 WorkflowCommandCatalog
 *     （IWorkflowCommandContributor 兑现面）——contract_test 目标已链
 *     sdurws_ird_workflow_plugin（Qt 传染允许面，T02 既有形态）。
 */

#include <gtest/gtest.h>

#include <QString>                                    // 默认键冻结值比对（QKeySequence::toString）

#include <sdurws/ird/project/CommandService.hpp>      // HandlerRegistry/CommandEnvelope/submit
#include <sdurws/ird/project/ProjectStore.hpp>        // ProjectStoreFactory/OpenStoreResult
#include <sdurws/ird/project/QueryPort.hpp>           // branchHistory/branchTips（修订观测点）
#include <sdurws/ird/project/UndoRedo.hpp>            // UndoRedoService（真实撤销/重做）
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯登记
#include <sdurws/ird/ui/ICommandRegistry.hpp>         // ui::createCommandRegistry（真实设施）
#include <sdurws/ird/workflow/Commands.hpp>
#include <sdurws/ird/workflow/Types.hpp>
#include "plugin/WorkflowCommandCatalog.hpp"          // 贡献目录/接口兑现面（plugin 私有头——
                                                      //   经单元根 PRIVATE include 解析，
                                                      //   modeling PluginPanelTest 同款）

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {

using namespace sdurws::ird;
using workflow::IRevisionCommandPort;
using workflow::IWorkflowSessionPort;
using workflow::RevisionOutcome;
using workflow::SessionActionResult;
using workflow::WorkflowCommandPorts;
using project::CommandEnvelope;
using project::CommandResult;
using project::HandlerRegistry;
using project::ICommandHandler;
using project::PrepareOutcome;
using project::ProjectStoreFactory;

// =====================================================================
// 会话端口桩（宿主面等价物——KIN-06 契约的触达观测面）
// =====================================================================

/// 会话动作桩（记录调用——复位关节契约的会话面观测点）。
class SessionStub final : public IWorkflowSessionPort {
public:
    int resetHome = 0;   ///< resetJointsToHome 调用计数
    int resetZero = 0;   ///< resetJointsToZero 调用计数
    SessionActionResult next{true, ""};  ///< 预置返回（成功无文案）

    SessionActionResult switchSchemeBranch() override { return next; }
    SessionActionResult cycleDisplayMode() override { return next; }
    SessionActionResult resetJointsToHome() override { ++resetHome; return next; }
    SessionActionResult resetJointsToZero() override { ++resetZero; return next; }
    SessionActionResult saveAllDrafts() override { return next; }
};

// =====================================================================
// 修订端口真实桥（L5 装配桥的测试等价物——诚实登记）
// =====================================================================

/**
 * @brief 修订命令端口真实桥——undo/redo 半区桥接真实 UndoRedoService
 *        （store.undoRedo()——逆命令提交经①端口全流程 S1~S7）；applyDraft
 *        半区诚实返回未装配态（域草稿信封组装归宿主装配链路〔WP-24-T03b
 *        形态〕，本契约会话无该链路——不伪造提交）。
 *
 * 可用性前置纪律（UndoRedo.hpp 菜单绑定行）：动作前先 status() 判定，
 * 不可用＝committed=false＋稳定提示键（空历史撤销是**正常业务分支**，
 * 不是异常——WF-VER-226"空历史稳定提示"的桥半区）。
 */
class StoreRevisionBridgeV2 final : public IRevisionCommandPort {
public:
    project::ProjectStore* store = nullptr;  ///< 目标存储上下文（非 owning——fixture 保证存活期）
    int undoCalls = 0;                       ///< undo 触达计数（桥路径观测面）
    int undoUnavailable = 0;                 ///< 前置判定不可用计数（稳定提示路径）
    int redoCalls = 0;                       ///< redo 触达计数
    int redoUnavailable = 0;                 ///< 前置判定不可用计数

    /// 应用修改：域信封组装链路未装配（诚实态——不伪造提交）。
    RevisionOutcome applyDraft() override
    {
        RevisionOutcome out;
        out.committed = false;
        out.reasonKey = "cmd.workflow.apply.not-assembled";
        return out;
    }

    /// 撤销：前置判定（先 status 后动作）→桥接 UndoRedoService.undo。
    RevisionOutcome undo() override
    {
        ++undoCalls;
        // 空历史＝稳定提示非异常（WF-VER-226"空历史稳定提示"的桥半区；
        // project 以 invalid_argument 表达可用性前置违约——入口判定避开）。
        if (!store->undoRedo().status(mainBranch()).canUndo) {
            ++undoUnavailable;
            RevisionOutcome out;
            out.committed = false;
            out.reasonKey = "cmd.workflow.undo.nothing";
            return out;
        }
        return fold(store->undoRedo().undo(mainBranch()));
    }

    /// 重做：前置判定（redo 栈空＝稳定提示）→桥接 UndoRedoService.redo。
    RevisionOutcome redo() override
    {
        ++redoCalls;
        if (!store->undoRedo().status(mainBranch()).canRedo) {
            ++redoUnavailable;
            RevisionOutcome out;
            out.committed = false;
            out.reasonKey = "cmd.workflow.redo.nothing";
            return out;
        }
        return fold(store->undoRedo().redo(mainBranch()));
    }

private:
    /// 主分支身份（createNew 初始分支）。
    [[nodiscard]] core::BranchId mainBranch() const
    {
        return store->query().currentMetadata().record.primaryBranchId;
    }

    /// 提交结果折叠（CommandResult 四态→RevisionOutcome 零加工——
    /// newRevision 规范词形透传；拒绝/中止/失败＝reasonKey 统一提示键，
    /// 机器半区由对端诊断承载）。
    static RevisionOutcome fold(const CommandResult& result)
    {
        RevisionOutcome out;
        out.committed = result.committed();
        if (result.committed() && result.newRevision.has_value()) {
            out.newRevisionId = result.newRevision->toCanonical();
        }
        if (!out.committed) {
            out.reasonKey = "cmd.workflow.revision.rejected";
        }
        return out;
    }
};

// =====================================================================
// 种子命令替身（BaselineSeedHandler 同款先例——机制真值全在 project）
// =====================================================================

/**
 * @brief 撤销/重做主线的种子命令对（正向＋逆向——§6.9"可逆性是处理器
 *        声明的事实"：CommandPlan.inverseCommandType 声明面；逆命令同
 *        为已注册处理器——S1 要求 type 已注册）。
 *
 * 可逆形态＝同对象写回：正向写 seedId→payload 'A'（声明逆命令
 * wf-cmd-seed-revert），逆向写 seedId→payload 'B'。重做（重放正向原始
 * 载荷）与撤销（执行逆命令）各自产生恰好一个新修订——链长 +1，历史
 * 只增（PA-2）。对象 id 由 fixture 一次生成注入（非 prepare 取号——
 * 重放路径 prepare 再次执行时不再分配新对象，保证撤销/重做往返回写
 * 同一对象）。
 */
class UndoSeedHandler final : public ICommandHandler {
public:
    /// 正向 token（无点词形——project §4.4.4 语法，测试注册）。
    [[nodiscard]] std::string commandType() const override { return "wf-cmd-seed"; }
    [[nodiscard]] std::uint32_t currentPayloadVersion() const override { return 1; }

    /// 正向计划：写 seedId='A'＋逆命令声明（wf-cmd-seed-revert）。
    PrepareOutcome prepare(project::HandlerContext& /*ctx*/,
                           const CommandEnvelope& /*envelope*/,
                           const project::RevisionView& /*baseSnapshot*/,
                           project::CommandPlan& out,
                           std::vector<core::DiagnosticRecord>& /*diags*/) override
    {
        project::ObjectWrite write;
        write.objectId = seedId;  // fixture 固定身份（见类注——重放同对象）
        write.objectTypeToken = "TestProbe";
        write.payloadCanonical = {'A'};
        out.objectWrites.push_back(std::move(write));
        out.inverseCommandType = std::string{"wf-cmd-seed-revert"};  // 逆命令声明面（§6.9）
        out.inversePayloadCanonical = std::vector<std::uint8_t>{};   // 逆载荷（成对出现——空载荷）
        out.summary = "wf 契约测试：撤销/重做主线种子修订（可逆）";
        return PrepareOutcome::Planned;
    }

    core::ObjectId seedId{};  ///< 正逆两半共写的对象身份（fixture 注入）
};

/// 种子的逆命令（undo 提交——写回 seedId='B'；对称声明 inverse＝正命令
/// ——撤销修订的 inverse 记录是会话 redo 栈的原始三元组来源〔§6.9〕，
/// 缺声明则重做前置违约）。
class UndoSeedInverseHandler final : public ICommandHandler {
public:
    [[nodiscard]] std::string commandType() const override { return "wf-cmd-seed-revert"; }
    [[nodiscard]] std::uint32_t currentPayloadVersion() const override { return 1; }

    PrepareOutcome prepare(project::HandlerContext& /*ctx*/,
                           const CommandEnvelope& /*envelope*/,
                           const project::RevisionView& /*baseSnapshot*/,
                           project::CommandPlan& out,
                           std::vector<core::DiagnosticRecord>& /*diags*/) override
    {
        project::ObjectWrite write;
        write.objectId = seedId;  // 与正向同一对象（写回语义）
        write.objectTypeToken = "TestProbe";
        write.payloadCanonical = {'B'};
        out.objectWrites.push_back(std::move(write));
        // 对称声明面（§6.9）：撤销修订携带的 inverse 记录＝被撤销命令
        // （wf-cmd-seed）的原始表达——会话 redo 栈据此重放。原始载荷与
        // commitSeedRevision 的 envelope.payloadCanonical 逐字一致（测试
        // 材料内固定字面量——非生产推断）。
        out.inverseCommandType = std::string{"wf-cmd-seed"};
        out.inversePayloadCanonical = std::vector<std::uint8_t>{'s', 'e', 'e', 'd'};
        out.summary = "wf 契约测试：种子逆命令（撤销半区）";
        return PrepareOutcome::Planned;
    }

    core::ObjectId seedId{};  ///< 与正向同写的对象身份（fixture 注入）
};

// =====================================================================
// 共享夹具（真实 registry＋真实 store——SA-16 唯一注册点的端到端面）
// =====================================================================

class CommandsContract : public ::testing::Test {
public:
    std::filesystem::path dir;
    std::unique_ptr<project::OpenStoreResult> opened;
    SessionStub session;
    StoreRevisionBridgeV2 revisionBridge;
    std::unique_ptr<ui::ICommandRegistry> registry;
    std::vector<ui::RegistrationResult> registration;
    core::ObjectId seedObjectId{};  ///< 种子命令对共写的对象身份（SetUp 一次生成）

    void SetUp() override
    {
        // 真实落盘黄金项目（临时目录——ComparisonContract 同款序列化）。
        static std::atomic<unsigned long long> seq{0};
        dir = std::filesystem::temp_directory_path()
            / ("ird-wf-t12-"
               + std::to_string(seq.fetch_add(1)) + "-"
               + std::to_string(
                   std::chrono::steady_clock::now().time_since_epoch().count()));
        opened = std::make_unique<project::OpenStoreResult>(
            ProjectStoreFactory::createNew(dir, "wf t12 contract", nullptr, nullptr));

        // 种子命令对装配期注册（测试替身 token——机制真值在 project；
        // 对象身份一次生成两半共享——可逆写回形态，见 handler 类注）。
        seedObjectId = core::ObjectId::generate();
        auto seed = std::make_unique<UndoSeedHandler>();
        seed->seedId = seedObjectId;
        auto seedInverse = std::make_unique<UndoSeedInverseHandler>();
        seedInverse->seedId = seedObjectId;
        opened->store->handlerRegistry().registerHandler(std::move(seed));
        opened->store->handlerRegistry().registerHandler(std::move(seedInverse));

        // 修订桥接线（真实 UndoRedoService 桥——L5 装配桥测试等价物）。
        revisionBridge.store = opened->store.get();

        // 真实 ui 注册表（SA-16 唯一注册点；owner 白名单＝壳层设施
        // "ui"＋workflow token；诊断面可空＝无目录测试场景——显式声明，
        // 拒绝语义由返回值轨承载〔CommandRegistryDeps 契约〕）。
        ui::CommandRegistryDeps deps;
        deps.ownerWhitelist = {"ui", "workflow"};
        registry = ui::createCommandRegistry(std::move(deps));

        // 贡献清单真实注册（§8.3 注册编排的可执行形态）。
        // 上下文快照注入（§7.5 谓词求值唯一来源）：Project 作用域命令
        // （draft.apply/project.undo/redo/scheme.switch/project.saveAs/
        // package.export）的默认使能谓词要求"有项目且可写"——本夹具已
        // 真实打开黄金项目，注入对应快照（未注入＝默认无项目态，submit
        // 会被 UI-CMD-NOT-EXECUTABLE 拒绝）。
        registry->presentContext(ui::UiContextSnapshot{true, true, {}});

        WorkflowCommandPorts ports;
        ports.lifecycle = nullptr;      // 本契约会话不触发向导类（注册不校验端口）
        ports.session = &session;
        ports.revisions = &revisionBridge;
        ports.collision = nullptr;
        ports.reportDialog = nullptr;
        ports.reportExport = nullptr;
        registration = workflow::registerWorkflowCommands(*registry, ports);
    }

    void TearDown() override
    {
        registration.clear();
        registry.reset();     // 注册表先拆（handler 持端口指针——先于 store 消亡）
        opened.reset();       // 析构释放锁句柄（先于目录清理——project 先例同序）
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }

    /// 主分支身份（createNew 初始分支——label="main"）。
    [[nodiscard]] core::BranchId mainBranch() const
    {
        return opened->store->query().currentMetadata().record.primaryBranchId;
    }

    /// 修订链长度（branchHistory——PA-2"历史只增不改"的观测点；maxCount
    /// 给足上限——本契约场景链长 ≤3）。
    [[nodiscard]] std::size_t historySize() const
    {
        return opened->store->query().branchHistory(mainBranch(), 1024).size();
    }

    /// 目录树字节面快照（相对路径 → 大小；lock 心跳面排除——CloseFlow
    /// 契约同款 helper 口径）。
    [[nodiscard]] std::map<std::string, std::uintmax_t> snapshotTree() const
    {
        std::map<std::string, std::uintmax_t> snapshot;
        std::error_code ec;
        for (auto it = fs::recursive_directory_iterator(dir, ec);
             it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (ec || !it->is_regular_file(ec)) {
                continue;
            }
            if (it->path().filename().string() == "lock") {
                continue;  // 持有方心跳重写面排除（零写入复核只做存在性）
            }
            snapshot[it->path().lexically_relative(dir).u8string()] = it->file_size(ec);
        }
        return snapshot;
    }

    /// 经种子命令产生一次真实修订（WF-VER-226 前置——canUndo 态）。
    void commitSeedRevision()
    {
        CommandEnvelope env;
        env.branch = mainBranch();
        env.commandType = "wf-cmd-seed";
        env.payloadFormatVersion = 1;
        env.payloadCanonical = {'s', 'e', 'e', 'd'};
        const CommandResult r = opened->store->commands().submit(env);
        ASSERT_TRUE(r.committed()) << "种子提交必须成功（前置事实）";
    }
};

// =====================================================================
// WF-VER-222——命令集注册（≥10 条）与面板模糊搜索可达
// =====================================================================

/**
 * @brief WF-VER-222 主链——贡献清单经真实注册表 14 条全登记成功（owner
 *        "workflow" 白名单放行），空模糊词面板快照返回全部 14 条（UX-13
 *        验收底线：≥10 条）。
 */
TEST_F(CommandsContract, Registration_AllFourteen_Ok_PaletteListsAll_UX13_F5)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-13"}, std::vector<std::string>{"WF-VER-222"});

    // 逐条注册结果全 Ok（owner 校验/句法校验/重复校验全过——装配期一次）。
    ASSERT_EQ(registration.size(), workflow::workflowCommandIds().size());
    for (std::size_t i = 0; i < registration.size(); ++i) {
        EXPECT_EQ(registration[i], ui::RegistrationResult::Ok)
            << "第 " << i << " 条注册被拒：" << workflow::workflowCommandIds()[i];
    }

    // 空模糊词＝全部命令（面板快照按注册序稳定——NFR-COR-02 界面延伸）。
    const std::vector<ui::CommandView> all = registry->paletteSnapshot("", 50);
    ASSERT_EQ(all.size(), workflow::workflowCommandIds().size()) << "面板应列出全部贡献命令";
    for (std::size_t i = 0; i < all.size(); ++i) {
        EXPECT_EQ(all[i].id, workflow::workflowCommandIds()[i]) << "注册序＝冻结表行序";
    }
}

/**
 * @brief WF-VER-222 可达性——逐命令模糊词经真实 paletteSnapshot 命中
 *        （14/14 全可达——≥10 条达标；模糊词取自标题/关键字的过渡中文
 *        与 id 词形，子序列匹配大小写不敏感〔ui §7.4〕）。
 */
TEST_F(CommandsContract, PaletteFuzzySearch_ReachesEveryCommand_UX13_F5)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-13"}, std::vector<std::string>{"WF-VER-222"});

    // 逐命令（模糊词, 期望命中的命令 id）黄金表——词取自 UI-T06 过渡
    // 中文表（标题/关键字）与 id 子串（大小写不敏感兜底）。
    const std::vector<std::pair<std::string, std::string>> probes = {
        {"新建", "project.new"},             // 标题"新建项目"连续包含
        {"打开", "project.open"},            // 标题"打开项目"
        {"保存", "draft.save"},              // 标题"保存草稿"
        {"应用", "draft.apply"},             // 标题"应用修改"
        {"撤销", "project.undo"},            // 标题"撤销"
        {"重做", "project.redo"},            // 标题"重做"
        {"方案", "scheme.switch"},           // 标题"切换方案"子序列
        {"另存", "project.saveAs"},          // 标题"项目另存为"连续包含
        {"评估包", "package.export"},        // 标题"导出评估包"子序列
        {"报告", "report.export"},           // 标题"导出报告"
        {"碰撞", "analysis.collisionCheck"}, // 关键字"碰撞"（加权命中）
        {"显示", "view.displayMode"},        // 标题"显示模式"
        {"home", "view.resetHome"},          // 标题"复位到 home 位"（大小写不敏感）
        {"零位", "view.resetZero"},          // 标题"复位到零位"
    };
    ASSERT_GE(probes.size(), std::size_t{10}) << "UX-13 底线：≥10 条可达（本表 14）";

    int reached = 0;
    for (const auto& [fuzzy, expectedId] : probes) {
        const std::vector<ui::CommandView> hits = registry->paletteSnapshot(fuzzy, 50);
        bool found = false;
        for (const ui::CommandView& view : hits) {
            if (view.id == expectedId) {
                found = true;
                break;
            }
        }
        EXPECT_TRUE(found) << "模糊词「" << fuzzy << "」应可达命令 " << expectedId;
        if (found) {
            ++reached;
        }
    }
    EXPECT_GE(reached, 10) << "UX-13/F5：≥10 条经模糊搜索到达（实测 " << reached << "）";
    EXPECT_EQ(reached, 14) << "本卡贡献的 14 条应全部可达（上限之上零死角）";
}

/**
 * @brief 重复 id 注册边界拒绝（ui §7.2 设施行为——不覆盖不静默；UX-13
 *        快捷键契约"重复绑定在注册边界拒绝"的注册面同源验证）。
 */
TEST_F(CommandsContract, DuplicateRegistration_RejectedAtBoundary_NotOverwritten)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-13"}, std::vector<std::string>{"WF-VER-222"});

    // 再次注册同 id 描述符（同 owner）——注册边界拒绝（DuplicateId）。
    ui::CommandDescriptor duplicate = workflow::workflowCommandDescriptors().front();
    EXPECT_EQ(duplicate.id, "project.new");
    const ui::RegistrationResult result = registry->registerCommand(
        duplicate, [](const std::vector<ui::CommandParameter>&) {
            ui::CommandOutcome out;
            out.accepted = true;
            return out;
        });
    EXPECT_EQ(result, ui::RegistrationResult::DuplicateId) << "重复 id 在注册边界拒绝";

    // 不覆盖：原处理器仍在位（submit project.new 仍走贡献处理器——
    // 触发向导端口；本夹具 lifecycle 端口为空＝编排核 fail-fast 捕获为
    // 处理器异常→注册表吞异常返回 accepted=false——恰好证明未被覆盖为
    // 上面的恒真桩：若被覆盖，accepted 应为 true）。
    const ui::CommandOutcome out = registry->submit("project.new");
    EXPECT_FALSE(out.accepted) << "拒绝覆盖：原处理器保持（空端口装配违约→异常→拒绝）";
}

// =====================================================================
// WF-VER-223——复位关节零修订（KIN-06，端到端）
// =====================================================================

/**
 * @brief WF-VER-223——经注册表 submit 复位 Home/Zero：会话端口被触达、
 *        修订链零增长、UndoRedo 可用性双 false 不变、目录树字节面零变
 *        化（KIN-06：只改会话姿态，不修改设计模型/不触发失效——盘面复
 *        核）。
 */
TEST_F(CommandsContract, ResetHomeZero_ZeroRevisionEndToEnd_KIN06)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-06", "UX-13"}, std::vector<std::string>{"WF-VER-223"});

    // 基线：修订链长度、可撤销/可重做位、目录树字节面。
    const std::size_t historyBefore = historySize();
    const project::UndoRedoStatus statusBefore =
        opened->store->undoRedo().status(mainBranch());
    EXPECT_FALSE(statusBefore.canUndo);
    EXPECT_FALSE(statusBefore.canRedo);
    const auto treeBefore = snapshotTree();

    // 命令面板同路径提交（三处入口统一 submit——SA-16 唯一路径）。
    const ui::CommandOutcome home = registry->submit("view.resetHome");
    EXPECT_TRUE(home.accepted) << "复位 Home 受理（会话动作）";
    const ui::CommandOutcome zero = registry->submit("view.resetZero");
    EXPECT_TRUE(zero.accepted) << "复位 Zero 受理（会话动作）";

    // 会话端口恰好各一次（宿主面触达——真实执行语义）。
    EXPECT_EQ(session.resetHome, 1);
    EXPECT_EQ(session.resetZero, 1);

    // ★ 零修订盘面复核：修订链零增长＋可用性位不变＋目录树逐文件一致。
    EXPECT_EQ(historySize(), historyBefore) << "复位关节不得产生修订（KIN-06）";
    const project::UndoRedoStatus statusAfter =
        opened->store->undoRedo().status(mainBranch());
    EXPECT_FALSE(statusAfter.canUndo) << "无新修订→无可撤销";
    EXPECT_FALSE(statusAfter.canRedo);
    const auto treeAfter = snapshotTree();
    EXPECT_EQ(treeAfter, treeBefore) << "复位关节零写入（目录树字节面一致）";
}

// =====================================================================
// WF-VER-226——撤销/重做主线（PM-18/AT-29，端到端）
// =====================================================================

/**
 * @brief WF-VER-226 主线——种子修订后经注册表 submit 撤销/重做：各产生
 *        恰好一个新修订（修订链单调 +1）；历史只增不改（早期修订仍可
 *        读）；撤销后可重做、重做后再撤销态正确（PM-18/AT-29）。
 */
TEST_F(CommandsContract, UndoRedo_ProducesNewRevisions_HistoryAppendOnly_P18_AT29)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-18"}, std::vector<std::string>{"AT-29", "WF-VER-226"});

    // 前置：种子提交（一个可逆修订——canUndo 态）。链长黄金值＝2：
    // createNew 的初始基线修订（PM-01"项目创建、模板基线修订"）＋种子一次。
    commitSeedRevision();
    const std::size_t historyAfterSeed = historySize();
    ASSERT_EQ(historyAfterSeed, std::size_t{2})
        << "createNew 基线修订 1＋种子修订 1＝链长 2";
    ASSERT_TRUE(opened->store->undoRedo().status(mainBranch()).canUndo);

    // 撤销：经统一提交路径（SA-16——菜单/面板/快捷键同路由）。accepted
    // ＝派发语义（注册表强制——处理器应答已发生）；业务成功证据＝修订
    // 链 +1＋messageKey 空（成功无特别文案）。
    const ui::CommandOutcome undo = registry->submit("project.undo");
    ASSERT_TRUE(undo.accepted) << "撤销派发成功";
    EXPECT_FALSE(undo.messageKey.has_value()) << "业务成功无失败文案键";
    EXPECT_EQ(historySize(), historyAfterSeed + 1)
        << "撤销产生恰好一个新修订（PA-2：撤销是新修订不是抹除）";
    EXPECT_TRUE(opened->store->undoRedo().status(mainBranch()).canRedo)
        << "撤销后会话 redo 栈非空";

    // 历史不改写：最早修订（种子——branchHistory 新→旧序的链尾）仍可
    // 读（tryRevision 非空）。
    const auto history = opened->store->query().branchHistory(mainBranch(), 1024);
    ASSERT_GE(history.size(), std::size_t{2});
    EXPECT_TRUE(opened->store->query().tryRevision(history.back().id).has_value())
        << "早期修订不被改写（PA-2 历史只增）";

    // 重做：重放原始载荷——再产生一个新修订（非复现旧修订身份）。
    const ui::CommandOutcome redo = registry->submit("project.redo");
    ASSERT_TRUE(redo.accepted) << "重做应受理";
    EXPECT_EQ(historySize(), historyAfterSeed + 2)
        << "重做产生新修订（链长再 +1——重放不是回退）";
}

/**
 * @brief WF-VER-226 空历史稳定提示——无可撤销修订时撤销：稳定提示键承
 *        载业务结果（注册表 accepted＝派发语义——ui.md §10.3/v1.18"处
 *        理器应答已发生"；业务失败承载位＝messageKey）、不崩溃、不产生
 *        修订、桥走前置判定路径不触 project undo 动作。
 */
TEST_F(CommandsContract, UndoRedo_EmptyHistory_StableNotice_NoCrash_P18)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-18"}, std::vector<std::string>{"WF-VER-226"});

    // fresh store：仅 createNew 基线修订（不可逆——canUndo=false，"空历
    // 史"指无任何可撤销命令修订）。
    ASSERT_EQ(historySize(), std::size_t{1});
    ASSERT_FALSE(opened->store->undoRedo().status(mainBranch()).canUndo);
    const ui::CommandOutcome undo = registry->submit("project.undo");
    // ★ 注册表 accepted 语义（ui.md §10.3/v1.18——"注册表强制 accepted=
    //   true＝处理器应答已发生"）：submit().accepted 表示派发与应答，**
    //   不是业务成败**；空历史的业务失败承载位＝messageKey 稳定提示键。
    EXPECT_TRUE(undo.accepted) << "处理器应答已发生（派发语义——非业务成功）";
    ASSERT_TRUE(undo.messageKey.has_value());
    EXPECT_EQ(*undo.messageKey, "cmd.workflow.undo.nothing")
        << "空历史撤销给稳定提示键（不崩溃、不伪装业务成功）";
    EXPECT_EQ(historySize(), std::size_t{1}) << "空历史撤销零修订";
    // 桥路径复核：前置判定命中稳定提示路径（未调用 project undo 动作）。
    EXPECT_EQ(revisionBridge.undoCalls, 1);
    EXPECT_EQ(revisionBridge.undoUnavailable, 1);
}

// =====================================================================
// 贡献者接口与描述符冻结行（SA-16 数据面＋接口消费路径钉扎）
// =====================================================================

/**
 * @brief IWorkflowCommandContributor 经接口虚派发消费（WP-20-T03 首轮
 *        接口盲区教训的常设对正面）——contributedCommands() 返回 14 条，
 *        与词表同序同 id；bindable 全 true（SA-16：可经 ui 快捷键表改
 *        绑——插件不私占）；关键字集全集齐备（可达性兜底）。
 */
TEST_F(CommandsContract, Contributor_InterfaceDispatch_FourteenCommands_BindableAll_SA16)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-13", "SA-16"}, std::vector<std::string>{"WF-VER-222"});

    // 经抽象基类引用消费（接口路径钉扎——不是具体类直调）。
    workflow::IWorkflowCommandContributor& contributor = *(new workflow::WorkflowCommandContributor());
    std::unique_ptr<workflow::IWorkflowCommandContributor> owned(&contributor);
    const std::vector<ui::CommandDescriptor> contributed = contributor.contributedCommands();

    const std::vector<std::string>& ids = workflow::workflowCommandIds();
    ASSERT_EQ(contributed.size(), ids.size()) << "贡献清单＝词表 14 条";
    for (std::size_t i = 0; i < ids.size(); ++i) {
        EXPECT_EQ(contributed[i].id, ids[i]) << "贡献序＝词表序（第 " << i << " 条）";
        EXPECT_EQ(contributed[i].ownerUnit, "workflow")
            << "owner＝白名单 token（ui.md §11.1 第 8 位）";
        EXPECT_TRUE(contributed[i].bindable) << "SA-16：全体可绑定（经 ui 表改绑，不私占）";
        EXPECT_FALSE(contributed[i].keywordKeys.empty())
            << "关键字集齐备（未绑定命令经面板可达——UX-13 兜底）：" << ids[i];
        EXPECT_EQ(contributed[i].titleKey, std::string("cmd.") + ids[i] + ".title")
            << "标题键形（ui.md §3.5 键约定）";
    }
}

/**
 * @brief 描述符冻结行逐字段黄金断言（ui.md §7.1 冻结表同值——两卡增量
 *        同步义务的实现面：默认快捷键/作用域/只读可用/菜单路径/分类/关
 *        键字键逐列对表）。
 */
TEST_F(CommandsContract, Descriptors_FrozenRows_GoldenFieldsPerUiTable)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-13"}, std::vector<std::string>{"WF-VER-222"});

    const std::vector<ui::CommandDescriptor> rows = workflow::workflowCommandDescriptors();
    ASSERT_EQ(rows.size(), std::size_t{14});

    // 逐行黄金断言（行序＝冻结表行序；QKeySequence 以序列化文本比对）。
    struct Golden {
        const char* id;
        ui::CommandCategory category;
        ui::CommandScope scope;
        bool readOnlyAllowed;
        const char* shortcut;  // nullptr＝无默认键（面板可达 UX-13 兜底）
        const char* menuPath;
        std::size_t keywordCount;
    };
    const Golden golden[14] = {
        {"project.new",              ui::CommandCategory::Project,   ui::CommandScope::Session, true,  "Ctrl+N",      "文件/新建",        2},
        {"project.open",             ui::CommandCategory::Project,   ui::CommandScope::Session, true,  "Ctrl+O",      "文件/打开",        1},
        {"draft.save",               ui::CommandCategory::Edit,      ui::CommandScope::Project, false, "Ctrl+S",      "文件/保存草稿",    1},
        {"draft.apply",              ui::CommandCategory::Edit,      ui::CommandScope::Project, false, "Ctrl+Return", "文件/应用修改",    1},
        {"project.undo",             ui::CommandCategory::Edit,      ui::CommandScope::Project, false, "Ctrl+Z",      "编辑/撤销",        1},
        {"project.redo",             ui::CommandCategory::Edit,      ui::CommandScope::Project, false, "Ctrl+Y",      "编辑/重做",        1},
        {"scheme.switch",            ui::CommandCategory::Stage,     ui::CommandScope::Project, false, nullptr,       "阶段/切换方案",    1},
        {"project.saveAs",           ui::CommandCategory::Project,   ui::CommandScope::Project, false, nullptr,       "文件/项目另存为",  1},
        {"package.export",           ui::CommandCategory::Project,   ui::CommandScope::Project, false, nullptr,       "文件/导出评估包",  1},
        {"report.export",            ui::CommandCategory::Report,    ui::CommandScope::Project, true,  nullptr,       "文件/导出报告",    1},
        {"analysis.collisionCheck",  ui::CommandCategory::Analysis,  ui::CommandScope::Project, false, nullptr,       "工具/碰撞检查",    2},
        {"view.displayMode",         ui::CommandCategory::View,      ui::CommandScope::View,    true,  nullptr,       "视图/显示模式",    1},
        {"view.resetHome",           ui::CommandCategory::View,      ui::CommandScope::View,    true,  nullptr,       "视图/复位到 home 位", 2},
        {"view.resetZero",           ui::CommandCategory::View,      ui::CommandScope::View,    true,  nullptr,       "视图/复位到零位",  2},
    };

    for (std::size_t i = 0; i < 14; ++i) {
        const ui::CommandDescriptor& row = rows[i];
        EXPECT_EQ(row.id, golden[i].id) << "行序冻结（第 " << i << " 行）";
        EXPECT_EQ(row.category, golden[i].category) << golden[i].id << " 分类";
        EXPECT_EQ(row.scope, golden[i].scope) << golden[i].id << " 作用域";
        EXPECT_EQ(row.readOnlyAllowed, golden[i].readOnlyAllowed)
            << golden[i].id << " 只读可用（§7.6）";
        EXPECT_EQ(row.menuPath, golden[i].menuPath) << golden[i].id << " 菜单路径";
        EXPECT_EQ(row.keywordKeys.size(), golden[i].keywordCount)
            << golden[i].id << " 关键字键数";
        if (golden[i].shortcut == nullptr) {
            EXPECT_FALSE(row.defaultShortcut.has_value())
                << golden[i].id << " 无默认键——面板可达兜底";
        } else {
            ASSERT_TRUE(row.defaultShortcut.has_value()) << golden[i].id << " 默认键在场";
            EXPECT_EQ(row.defaultShortcut->toString(),
                      QString::fromLatin1(golden[i].shortcut))
                << golden[i].id << " 默认键＝冻结表列值（SA-16 数据面）";
        }
    }
}

}  // namespace
