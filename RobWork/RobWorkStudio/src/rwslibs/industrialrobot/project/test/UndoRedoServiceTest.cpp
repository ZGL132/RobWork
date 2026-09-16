/**
 * @file   UndoRedoServiceTest.cpp
 * @brief  撤销/重做服务单元测试（PRJ-T13）——acceptance 四条逐条落位
 *         为具名用例：PRJ-TX-5（undo/redo 部分——应用→撤销→重做各恰
 *         一新修订＋历史只增不改写〔AT-29〕）、边界语义（空历史稳定
 *         提示数据／跨分支提交后再回〔栈按分支隔离——D-11〕／重开会话
 *         redo 失效、canUndo 由 tip 推导／逆命令注册协议〔project 只提
 *         交〕／新命令清空 redo＋一次性 stale 说明）、与草稿局部撤销的
 *         边界（§2.3/REQ-11 分界的存储侧形态）、P-PR-1 身份基线自证。
 *
 * 设计依据：
 *   - units/project.md §5.5（UndoRedoService 契约）、§6.9（语义详表——
 *     各用例锚点逐条标注）、§6.2（expectedRevision 并发校验——提交竞
 *     争的裁决面）、§6.5（注册协议——逆命令由业务处理器产出）、§4.5.1
 *     （分支走查——跨分支用例的模型）、§2.3（非目标——草稿局部撤销
 *     边界）；
 *   - 需求 PM-18（撤销/重做＝新修订的提交机制、空历史稳定提示）、
 *     PA-2（不可变历史）、D-11（会话内按分支、不持久化）、AT-29；
 *   - 任务契约 tasks/foundation/PRJ-T13.json acceptance 1～4。
 *
 * 测试域语义约定：可逆命令族＝"test-append-object"（declareInverse=
 *   true）与"test-append-undo"（StubHandlers.hpp——对称声明族）；载荷
 *   字节即对象负载原样（域语义测试内部自洽——project 不解释）。不可逆
 *   对照命令＝本文件局部 "test-plain-note"（无 inverse 的纯元数据修订）。
 */

#include <windows.h>

#include <gtest/gtest.h>

#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/project/CommandService.hpp>
#include <sdurws/ird/project/DraftService.hpp>
#include <sdurws/ird/project/ProjectStore.hpp>
#include <sdurws/ird/project/QueryPort.hpp>
#include <sdurws/ird/project/StoreTypes.hpp>
#include <sdurws/ird/project/UndoRedo.hpp>

#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "StubHandlers.hpp"

using namespace sdurws::ird;                       // NOLINT——测试域惯例
using namespace sdurws::ird::project;              // NOLINT
namespace stub = sdurws::ird::project::stub;

namespace {

// ---------------------------------------------------------------------
// 局部测试处理器：不可逆纯元数据命令（"test-plain-note" v1）——新命令
// 清空 redo 用例的"undo 之外的命令"注入面（§6.9 行 4：任何新命令→redo
// 栈清空）。无 inverse 声明＝可逆性声明制的不可逆侧（§6.9 行 1）。
// ---------------------------------------------------------------------

class PlainNoteHandler final : public ICommandHandler {
public:
    [[nodiscard]] std::string commandType() const override
    {
        return "test-plain-note";
    }
    [[nodiscard]] std::uint32_t currentPayloadVersion() const override
    {
        return 1;
    }

    PrepareOutcome prepare(HandlerContext& /*ctx*/, const CommandEnvelope& /*envelope*/,
                           const RevisionView& /*baseSnapshot*/, CommandPlan& out,
                           std::vector<core::DiagnosticRecord>& /*diags*/) override
    {
        // 纯元数据修订（objectWrites 为空）、无逆命令声明——提交后该分
        // 支 tip 无 inverse（undo 链在此断裂，§6.9 空历史行语义）。
        out.summary = "test plain note";
        return PrepareOutcome::Planned;
    }
};

// ---------------------------------------------------------------------
// 共享夹具：ServiceFixture（临时项目＋捕获 sink＋总线订阅）＋撤销/重做
// 用例组的缺省注册面。不调用 registerDefaults（其中 AppendObjectHandler
// 为 declareInverse=false 缺省实例）——本用例组需要 declareInverse=true
// 的可逆族，显式注册避免同 token 冲突。
// ---------------------------------------------------------------------

class UndoRedoServiceTest : public ::testing::Test {
protected:
    stub::ServiceFixture fx;

    /// 注册可逆命令族（append 对称族）＋建支处理器（跨分支用例消费）。
    void registerUndoFamily()
    {
        auto append = std::make_unique<stub::AppendObjectHandler>();
        append->declareInverse = true;  // 对称族正向半边：逆＝test-append-undo
        fx.registry().registerHandler(std::move(append));
        fx.registry().registerHandler(std::make_unique<stub::AppendUndoHandler>());
    }

    /// 建支信封（载荷两行＝源分支规范文本＋label——StubHandlers 约定）。
    [[nodiscard]] CommandEnvelope createBranchEnvelope(const core::BranchId& source,
                                                       const std::string& label) const
    {
        CommandEnvelope envelope;
        envelope.branch = source;
        envelope.commandType = "test-create-branch";
        envelope.payloadFormatVersion = 1;
        const std::string payload = source.toCanonical() + "\n" + label;
        envelope.payloadCanonical.assign(payload.begin(), payload.end());
        return envelope;
    }

    /// 当前分支 tip 修订视图（查询端口强轨——测试断言基线）。
    [[nodiscard]] RevisionView tipView(const core::BranchId& branch) const
    {
        for (const BranchTip& tip : fx.opened.store->query().branchTips()) {
            if (tip.id == branch) {
                return fx.opened.store->query().revision(tip.tip);
            }
        }
        ADD_FAILURE() << "分支不在权威分支表";
        return RevisionView{};
    }
};

// =====================================================================
// acceptance 1——PRJ-TX-5（undo/redo 部分）：应用→撤销→重做各恰一新修
// 订；修订只增（revisionSeq 递增、revisions/ 目录只增、既有修订清单与
// 视图不变）＝"历史不改写"（PM-18/PA-2/AT-29）。
// =====================================================================

TEST_F(UndoRedoServiceTest, ApplyUndoRedoEachCommittedAsNewRevisionHistoryAppendOnly)
{
    registerUndoFamily();
    auto& store = *fx.opened.store;
    auto& query = store.query();
    auto& undoRedo = store.undoRedo();
    const core::BranchId main = fx.primaryBranch();

    // ---- 应用（apply）：经命令端口提交可逆命令（可逆性由处理器声明——
    //      §6.9 行 1）。初始 r0（project-init，无 inverse）之后首个修订。
    const CommandResult applied = fx.commands().submit(fx.appendEnvelope("alpha-1", main));
    ASSERT_TRUE(applied.committed()) << "前置提交失败——用例前提不成立";
    const RevisionView v1 = query.revision(*applied.newRevision);
    ASSERT_TRUE(v1.inverse.has_value());       // 逆命令随修订持久化（§4.4.4）
    EXPECT_EQ(v1.inverse->commandType, "test-append-undo");

    // ---- 应用后状态：canUndo 由 tip 推导（磁盘事实）；undoSummary＝将
    //      被撤销命令的摘要（PM-18 提示数据面）。
    const UndoRedoStatus s1 = undoRedo.status(main);
    ASSERT_TRUE(s1.canUndo);
    EXPECT_EQ(s1.undoSummary, std::optional<std::string>{"test append object"});
    EXPECT_FALSE(s1.canRedo);                  // 会话尚无撤销——redo 不可用
    EXPECT_FALSE(s1.blockedReason.has_value());

    // ---- 历史基线捕获（"历史不改写"断言的对照面——PA-2）。
    const std::size_t dirsAfterApply = fx.revisionDirCount();
    const std::vector<RevisionView> historyBefore = query.branchHistory(main, 50);

    // ---- 撤销：恰一个新修订（§5.5——envelope 取自 tip 的 inverse 记录）。
    const CommandResult undone = undoRedo.undo(main);
    ASSERT_TRUE(undone.committed());
    ASSERT_TRUE(undone.newRevision.has_value());
    const RevisionView v2 = query.revision(*undone.newRevision);
    EXPECT_GT(v2.seq, v1.seq);                             // revisionSeq 递增（O-15）
    EXPECT_EQ(fx.revisionDirCount(), dirsAfterApply + 1);  // revisions/ 只增
    EXPECT_EQ(v2.parent, v1.id);                           // 分支 tip 维度父链
    EXPECT_EQ(v2.branch, main);
    // 撤销修订的 inverse＝被撤销命令的原始表达（对称声明面——redo 信封
    // 的数据源，§6.9；project 只拷贝不构造）。
    ASSERT_TRUE(v2.inverse.has_value());
    EXPECT_EQ(v2.inverse->commandType, "test-append-object");
    EXPECT_EQ(v2.inverse->payloadCanonical, "alpha-1");

    // ---- 历史只增不改写（撤销后）：既有修订清单原序保留＋视图逐字段
    //      不变（修订只增断言——acceptance 1 后半）。
    const std::vector<RevisionView> historyAfterUndo = query.branchHistory(main, 50);
    ASSERT_EQ(historyAfterUndo.size(), historyBefore.size() + 1);
    for (std::size_t i = 0; i < historyBefore.size(); ++i) {
        EXPECT_TRUE(historyAfterUndo[i + 1] == historyBefore[i]) << "第 " << i << " 项";
    }
    EXPECT_TRUE(query.revision(v1.id) == v1);

    // ---- 撤销后状态：canRedo＝会话栈非空且与 tip 一致；redoSummary＝
    //      被撤销命令摘要；canUndo 仍真（撤销修订自身可逆——对称族）。
    const UndoRedoStatus s2 = undoRedo.status(main);
    ASSERT_TRUE(s2.canRedo);
    EXPECT_EQ(s2.redoSummary, std::optional<std::string>{"test append object"});
    ASSERT_TRUE(s2.canUndo);

    // ---- 重做：重放原始载荷——同样恰好一个新修订（§6.9 行 2/4）。
    const CommandResult redone = undoRedo.redo(main);
    ASSERT_TRUE(redone.committed());
    ASSERT_TRUE(redone.newRevision.has_value());
    const RevisionView v3 = query.revision(*redone.newRevision);
    EXPECT_GT(v3.seq, v2.seq);
    EXPECT_EQ(fx.revisionDirCount(), dirsAfterApply + 2);
    EXPECT_EQ(v3.parent, v2.id);
    EXPECT_EQ(v3.commandSummary, "test append object");    // 原始命令重放
    EXPECT_EQ(v3.inverse->commandType, "test-append-undo");  // 循环对称

    // ---- 历史不改写（重做后）：v1/v2 视图与清单序保持——AT-29 锚点
    //      「应用→撤销→重做产生新修订且历史不改写」。
    EXPECT_TRUE(query.revision(v1.id) == v1);
    EXPECT_TRUE(query.revision(v2.id) == v2);
    const std::vector<RevisionView> historyAfterRedo = query.branchHistory(main, 50);
    ASSERT_EQ(historyAfterRedo.size(), historyAfterUndo.size() + 1);
    for (std::size_t i = 0; i < historyAfterUndo.size(); ++i) {
        EXPECT_TRUE(historyAfterRedo[i + 1] == historyAfterUndo[i]) << "第 " << i << " 项";
    }

    // ---- 重做后状态：redo 栈耗尽（仅会话内一条）；undo 仍可用（tip 的
    //      inverse 由重放命令的声明提供——周期可持续）。
    const UndoRedoStatus s3 = undoRedo.status(main);
    EXPECT_FALSE(s3.canRedo);
    ASSERT_TRUE(s3.canUndo);
}

// ---- acceptance 1 补充：重做在内容面恢复原始载荷（引用级撤销的测试域
//      形态——撤销修订不引用域对象，重做修订重新携带原负载对象）。

TEST_F(UndoRedoServiceTest, UndoThenRedoRestoresOriginalPayloadAndCommandFamily)
{
    registerUndoFamily();
    auto& store = *fx.opened.store;
    auto& query = store.query();
    auto& undoRedo = store.undoRedo();
    const core::BranchId main = fx.primaryBranch();

    const CommandResult applied = fx.commands().submit(fx.appendEnvelope("beta-1", main));
    ASSERT_TRUE(applied.committed());
    const RevisionView v1 = query.revision(*applied.newRevision);

    // ---- 撤销：纯元数据修订——objectRefs 仅剩元数据对象（测试域"移除
    //      引用"形态；引用集级回退的业务判定归域处理器——§6.5/§6.9）。
    const CommandResult undone = undoRedo.undo(main);
    ASSERT_TRUE(undone.committed());
    const RevisionView v2 = query.revision(*undone.newRevision);
    ASSERT_EQ(v2.objectRefs.size(), std::size_t{1});
    EXPECT_EQ(v2.objectRefs[0].objectTypeToken, "ProjectMetadata");  // kMetadataTypeToken

    // ---- 重做：重放原始载荷——新修订重新携带同负载对象（tryObject 读
    //      回逐字节一致＝"重新提交被撤销命令的原始载荷"的内容面证据）。
    const CommandResult redone = undoRedo.redo(main);
    ASSERT_TRUE(redone.committed());
    const RevisionView v3 = query.revision(*redone.newRevision);
    bool restored = false;
    for (const ObjectRef& ref : v3.objectRefs) {
        if (ref.objectTypeToken != "TestData") {
            continue;
        }
        const auto bytes = query.tryObject(ref.objectId, ref.contentVersion);
        ASSERT_TRUE(bytes.has_value());
        const std::string payload(bytes->begin(), bytes->end());
        EXPECT_EQ(payload, "beta-1");
        restored = true;
    }
    EXPECT_TRUE(restored) << "重做修订未携带重放的域对象";
}

// =====================================================================
// acceptance 2——边界语义：空历史稳定提示数据（不抛错）＋前置违约
// fail-fast（§6.9 空历史行；UndoRedo.hpp 错误语义总表）。
// =====================================================================

TEST_F(UndoRedoServiceTest, EmptyHistoryStablePromptDataAndPreconditionViolationFailsFast)
{
    // 全新项目：r0（project-init，无 inverse）＝空历史。
    auto& store = *fx.opened.store;
    auto& undoRedo = store.undoRedo();
    const core::BranchId main = fx.primaryBranch();

    // ---- 稳定提示数据：全 false＋空摘要＋无 blockedReason——不抛错，
    //      重复调用幂等（PM-18"空历史明确稳定提示"）。
    for (int round = 0; round < 2; ++round) {
        const UndoRedoStatus s = undoRedo.status(main);
        EXPECT_FALSE(s.canUndo) << "第 " << round << " 轮";
        EXPECT_FALSE(s.canRedo);
        EXPECT_FALSE(s.undoSummary.has_value());
        EXPECT_FALSE(s.redoSummary.has_value());
        EXPECT_FALSE(s.blockedReason.has_value());
    }

    // ---- 未知分支：同样稳定空状态（noexcept 契约的降级表达——不抛）。
    const UndoRedoStatus ghost = undoRedo.status(core::BranchId::generate());
    EXPECT_FALSE(ghost.canUndo);
    EXPECT_FALSE(ghost.canRedo);
    EXPECT_FALSE(ghost.blockedReason.has_value());

    // ---- 前置违约 fail-fast：canUndo/canRedo 为 false 时调用 undo/redo
    //      ＝调用方契约违约（菜单绑定纪律——先 status() 后动作）。
    EXPECT_THROW(undoRedo.undo(main), std::invalid_argument);
    EXPECT_THROW(undoRedo.redo(main), std::invalid_argument);
}

// =====================================================================
// acceptance 2——跨分支提交后再回：栈＝会话内按分支（D-11/§6.9 跨分支
// 行）；§4.5.1 走查的分支模型上验证兄弟分支提交互不清栈。
// =====================================================================

TEST_F(UndoRedoServiceTest, CrossBranchCommitDoesNotTouchSiblingBranchStacks)
{
    registerUndoFamily();
    fx.registry().registerHandler(std::make_unique<stub::CreateBranchHandler>());
    auto& store = *fx.opened.store;
    auto& query = store.query();
    auto& undoRedo = store.undoRedo();
    const core::BranchId main = fx.primaryBranch();

    // ---- 走查前置：main 上提交可逆命令（r1）；自 main 建支 feature-b
    //      （B 的 base/tip＝建支时 main 的 tip——§4.5.1 步骤 1/3）。
    const CommandResult applied = fx.commands().submit(fx.appendEnvelope("alpha-1", main));
    ASSERT_TRUE(applied.committed());
    const CommandResult branched
        = fx.commands().submit(createBranchEnvelope(main, "feature-b"));
    ASSERT_TRUE(branched.committed());
    core::BranchId featureB{};
    for (const BranchTip& tip : query.branchTips()) {
        if (tip.id != main) {
            featureB = tip.id;
        }
    }
    ASSERT_TRUE(featureB.isValid()) << "建支失败——用例前提不成立";

    // ---- main 上撤销（r2）：会话 redo 栈记录归属 main。
    const CommandResult undone = undoRedo.undo(main);
    ASSERT_TRUE(undone.committed());
    ASSERT_TRUE(undoRedo.status(main).canRedo);

    // ---- 跨分支提交：在 feature-b 上提交可逆命令——不得清 main 的栈
    //      （§6.9"栈按分支隔离"），B 自身 canUndo 由其 tip 推导。
    const CommandResult onB = fx.commands().submit(fx.appendEnvelope("on-b", featureB));
    ASSERT_TRUE(onB.committed());
    const UndoRedoStatus mainStatus = undoRedo.status(main);
    EXPECT_TRUE(mainStatus.canRedo) << "兄弟分支的提交清空了 main 的会话栈";
    const UndoRedoStatus bStatus = undoRedo.status(featureB);
    EXPECT_TRUE(bStatus.canUndo);   // B 的 tip 携带 inverse——磁盘推导
    EXPECT_FALSE(bStatus.canRedo);  // B 无会话撤销记录
    // B 的 undoSummary 指向 B 自己 tip 的命令摘要（数据源＝B tip 视图的
    // commandSummary，逐字比对——非 main 的会话数据泄漏）。
    EXPECT_EQ(bStatus.undoSummary,
              std::optional<std::string>{tipView(featureB).commandSummary});

    // ---- 再回 main：重做可用且成功（新修订在 main 上；seq 全局单调）。
    const CommandResult redone = undoRedo.redo(main);
    ASSERT_TRUE(redone.committed());
    EXPECT_GT(query.revision(*redone.newRevision).seq,
              query.revision(*onB.newRevision).seq);
    const RevisionView redoRev = query.revision(*redone.newRevision);
    EXPECT_EQ(redoRev.branch, main);
    // B 的历史与栈不受影响（再断言）。
    EXPECT_TRUE(undoRedo.status(featureB).canUndo);
    EXPECT_FALSE(undoRedo.status(featureB).canRedo);
}

// =====================================================================
// acceptance 2——新命令清空 redo 栈（§6.9 行 4"标准语义"）＋一次性
// stale 说明（§6.9 行 6"过期基线→stale-revision"）。
// =====================================================================

TEST_F(UndoRedoServiceTest, NewCommandClearsRedoStackWithOneShotStaleReason)
{
    registerUndoFamily();
    fx.registry().registerHandler(std::make_unique<PlainNoteHandler>());
    auto& store = *fx.opened.store;
    auto& undoRedo = store.undoRedo();
    const core::BranchId main = fx.primaryBranch();

    // 前置：应用→撤销（会话 redo 栈非空）。
    const CommandResult applied = fx.commands().submit(fx.appendEnvelope("alpha-1", main));
    ASSERT_TRUE(applied.committed());
    const CommandResult undone = undoRedo.undo(main);
    ASSERT_TRUE(undone.committed());
    ASSERT_TRUE(undoRedo.status(main).canRedo);

    // ---- 会话内直接提交新命令（undo 之外的命令——PlainNote 无 inverse）。
    CommandEnvelope note;
    note.branch = main;
    note.commandType = "test-plain-note";
    note.payloadFormatVersion = 1;
    const CommandResult noteResult = fx.commands().submit(note);
    ASSERT_TRUE(noteResult.committed());

    // ---- 第一次 status：redo 消失＋一次性 stale 说明（机器可读面——
    //      StaleRevisionRejected 复用 §4.4.8 封闭集现有码）。
    const UndoRedoStatus after = undoRedo.status(main);
    EXPECT_FALSE(after.canRedo);
    ASSERT_TRUE(after.blockedReason.has_value());
    EXPECT_EQ(after.blockedReason->code(), StoreErrorCode::StaleRevisionRejected);

    // ---- 第二次 status：旗标已消费——回到平凡空状态（轮询面不重复
    //      呈现同一事实——实现口径③）。
    const UndoRedoStatus settled = undoRedo.status(main);
    EXPECT_FALSE(settled.canRedo);
    EXPECT_FALSE(settled.blockedReason.has_value());

    // ---- 栈已清空：redo 前置违约（调用方未再经 status() 判定）。
    EXPECT_THROW(undoRedo.redo(main), std::invalid_argument);
}

// =====================================================================
// acceptance 2——重开项目（会话边界）：redo 仅会话内（D-11——会话栈不
// 持久化）；重启后 canUndo 由 tip 修订的 inverse 记录推导（§5.5）。
// =====================================================================

TEST_F(UndoRedoServiceTest, ReopenProjectRedoLostUndoDerivedFromTip)
{
    registerUndoFamily();
    const core::BranchId main = fx.primaryBranch();

    // ---- 会话 1：应用→撤销（会话 redo 栈有一条记录）。
    const CommandResult applied = fx.commands().submit(fx.appendEnvelope("alpha-1", main));
    ASSERT_TRUE(applied.committed());
    const CommandResult undone = fx.opened.store->undoRedo().undo(main);
    ASSERT_TRUE(undone.committed());
    const core::RevisionId undoRev = *undone.newRevision;

    // ---- 关闭并重开（同一磁盘项目——存储上下文消亡＝会话消亡，D-11
    //      的机制边界；锁随析构释放，重开重新取得写权限）。
    fx.opened.store.reset();
    OpenStoreRequest request;
    request.path = fx.dir;
    request.mode = OpenMode::Writable;
    request.eventBus = &fx.bus;
    request.diagnostics = &fx.sink;
    fx.opened = ProjectStoreFactory::open(request);

    // ---- L5 装配边界（§6.5）：处理器注册归属每个存储上下文——重开＝新
    //      装配，可逆命令族须重新注册；会话栈不随注册恢复（redo 仍为空）。
    registerUndoFamily();

    // ---- 会话 2 状态：canRedo 恒 false（会话栈为空——redo 不持久化）；
    //      canUndo 真由 tip（撤销修订）的 inverse 推导——与会话记忆无关。
    auto& undoRedo = fx.opened.store->undoRedo();
    const UndoRedoStatus reopened = undoRedo.status(main);
    ASSERT_TRUE(reopened.canUndo);
    EXPECT_FALSE(reopened.canRedo);
    EXPECT_FALSE(reopened.redoSummary.has_value());

    // ---- 重启后 redo 不可用（会话栈空——前置违约 fail-fast）。
    EXPECT_THROW(undoRedo.redo(main), std::invalid_argument);

    // ---- 重启后的 undo（撤销"撤销"）照常工作——恰一新修订、seq 前进。
    //      （注：对称声明族下该撤销自身可逆，成功后会话 redo 栈重新有
    //      条目——那是本会话的新撤销记录，与本用例钉住的"重启丢失"不
    //      同源；故 redo 断言先行。）
    const CommandResult undoneAgain = undoRedo.undo(main);
    ASSERT_TRUE(undoneAgain.committed());
    EXPECT_GT(fx.opened.store->query().revision(*undoneAgain.newRevision).seq,
              fx.opened.store->query().revision(undoRev).seq);
}

// =====================================================================
// acceptance 2——逆命令由业务处理器产出、project 只提交（§6.5 注册协
// 议）：未注册逆命令 token 时 undo 透传 unknown-command——零修订、会话
// 栈不变；处理器就位后同一撤销请求即可用。
// =====================================================================

TEST_F(UndoRedoServiceTest, UnregisteredInverseTokenTypeRelayedAsUnknownCommandNoRevision)
{
    // 只注册正向半边（append 声明逆＝"test-append-undo"）——逆命令处理
    // 器故意不注册：可逆性声明是处理器的事实，project 不代行（§6.9 行 1
    // ＋§6.5 注册协议）。
    auto append = std::make_unique<stub::AppendObjectHandler>();
    append->declareInverse = true;
    fx.registry().registerHandler(std::move(append));

    auto& store = *fx.opened.store;
    auto& undoRedo = store.undoRedo();
    const core::BranchId main = fx.primaryBranch();

    const CommandResult applied = fx.commands().submit(fx.appendEnvelope("alpha-1", main));
    ASSERT_TRUE(applied.committed());

    // canUndo 真由磁盘推导（inverse 记录存在）——与逆命令处理器是否注
    // 册无关（可用性推导面）；注册协议的裁决在提交面。
    ASSERT_TRUE(undoRedo.status(main).canUndo);
    const std::size_t dirsBefore = fx.revisionDirCount();

    // ---- undo：信封组装后经命令端口 submit——S1 形式校验拒绝（未注册
    //      token→unknown-command，§6.3）；结果原样透传，零修订。
    const CommandResult rejected = undoRedo.undo(main);
    ASSERT_TRUE(rejected.rejected());
    EXPECT_EQ(rejected.status.rejection, CommandStatus::Rejection::UnknownCommand);
    EXPECT_FALSE(rejected.newRevision.has_value());
    EXPECT_EQ(fx.revisionDirCount(), dirsBefore);

    // ---- 会话栈未入（提交失败——栈只在 Committed 后变更，可重试语义）。
    EXPECT_FALSE(undoRedo.status(main).canRedo);

    // ---- 处理器（业务侧）就位后：同一撤销请求成功——"project 只提交"
    //      的正向面。
    fx.registry().registerHandler(std::make_unique<stub::AppendUndoHandler>());
    const CommandResult undone = undoRedo.undo(main);
    ASSERT_TRUE(undone.committed());
    EXPECT_EQ(fx.revisionDirCount(), dirsBefore + 1);
    ASSERT_TRUE(undoRedo.status(main).canRedo);
}

// =====================================================================
// acceptance 3——与草稿局部撤销的边界（§2.3 非目标/§6.9 末行：项目命令
// 撤销产生修订；未应用草稿内的编辑级撤销归 ui＋业务域〔REQ-11〕，不经
// 命令服务、不产生修订）。存储侧形态：草稿落盘零修订、撤销服务状态不
// 因草稿变化；撤销命令不消费草稿（草稿生命周期独立——PM-04）。
// =====================================================================

TEST_F(UndoRedoServiceTest, DraftEditingStaysOutsideCommandUndoService)
{
    registerUndoFamily();
    auto& store = *fx.opened.store;
    auto& query = store.query();
    auto& undoRedo = store.undoRedo();
    const core::BranchId main = fx.primaryBranch();

    // 前置：应用一条可逆命令（撤销服务的命令面基线）。
    const CommandResult applied = fx.commands().submit(fx.appendEnvelope("alpha-1", main));
    ASSERT_TRUE(applied.committed());
    const std::size_t dirsAfterApply = fx.revisionDirCount();
    const UndoRedoStatus before = undoRedo.status(main);
    ASSERT_TRUE(before.canUndo);

    // ---- 未应用草稿"编辑"（落盘——PM-04 保存与应用分离）：零修订、
    //      撤销服务状态零变化（草稿不进命令面——REQ-11 分界的存储侧）。
    DraftDocument doc;
    doc.projectId = store.projectId();
    doc.branchId = main;
    doc.moduleId = "modeling";
    doc.baseRevisionId = tipView(main).id;
    doc.payload = "{\"local-edit\":1}";
    doc.savedAtUtc = "2026-09-16T03:00:00Z";
    doc.origin = DraftOrigin::Manual;
    const SaveResult saved = store.drafts().save(doc);
    ASSERT_TRUE(saved.ok) << "草稿落盘失败——用例前提不成立";
    EXPECT_EQ(fx.revisionDirCount(), dirsAfterApply);  // 不产生任何修订
    const UndoRedoStatus afterDraft = undoRedo.status(main);
    EXPECT_EQ(afterDraft.canUndo, before.canUndo);
    EXPECT_EQ(afterDraft.canRedo, before.canRedo);
    EXPECT_EQ(afterDraft.undoSummary, before.undoSummary);

    // ---- 项目命令撤销与草稿互不干涉：undo 产生新修订（命令级），草稿
    //      原样保留（撤销不消费/不清除草稿——编辑级撤销归 ui DraftController）。
    const CommandResult undone = undoRedo.undo(main);
    ASSERT_TRUE(undone.committed());
    EXPECT_EQ(fx.revisionDirCount(), dirsAfterApply + 1);
    std::vector<core::DiagnosticRecord> loadDiags;
    const auto draft = store.drafts().tryLoad(main, "modeling", loadDiags);
    ASSERT_TRUE(draft.has_value()) << "撤销后草稿应原样保留（生命周期独立——PM-04）";
    EXPECT_EQ(draft->payload, doc.payload);
    EXPECT_EQ(draft->baseRevisionId, doc.baseRevisionId);  // 草稿基线仍指原修订
}

// =====================================================================
// acceptance 4——P-PR-1 处置：本服务消费/产出的身份类型＝core.md v0.1
// 公共契约类型（无本地第二身份形态）；身份文本化一律经 core 规范 API。
// =====================================================================

TEST_F(UndoRedoServiceTest, Ppr1_SessionTypesAreCoreContractIdentities)
{
    // ---- 编译期同一性：InverseRecord 的修订身份＝core::RevisionId（与
    //      磁盘清单/命令留痕同一类型——core v0.1 §4.1 基线消费，P-PR-1）。
    static_assert(std::is_same_v<decltype(std::declval<InverseRecord&>().undoRevision),
                                 core::RevisionId>,
                  "InverseRecord 必须复用 core 身份契约类型（P-PR-1）");
    static_assert(std::is_same_v<decltype(std::declval<InverseRecord&>().redoPayloadFormatVersion),
                                 std::uint32_t>,
                  "载荷版本三元组与 §6.4 契约同型");

    registerUndoFamily();
    auto& store = *fx.opened.store;
    auto& query = store.query();
    auto& undoRedo = store.undoRedo();
    const core::BranchId main = fx.primaryBranch();

    const CommandResult applied = fx.commands().submit(fx.appendEnvelope("alpha-1", main));
    ASSERT_TRUE(applied.committed());
    const CommandResult undone = undoRedo.undo(main);
    ASSERT_TRUE(undone.committed());
    const RevisionView v2 = query.revision(*undone.newRevision);

    // ---- 运行期：身份经 core 规范文本往返（toCanonical/tryFromCanonical
    //      ——core 公共 API，服务无本地重格式化）后语义不变，且可直接作
    //      为 status/undo 的消费键。
    const std::string canonical = v2.id.toCanonical();
    const auto parsed = core::RevisionId::tryFromCanonical(canonical);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(*parsed, v2.id);
    const auto branchParsed = core::BranchId::tryFromCanonical(main.toCanonical());
    ASSERT_TRUE(branchParsed.has_value());
    const UndoRedoStatus s = undoRedo.status(*branchParsed);
    EXPECT_TRUE(s.canUndo);
    EXPECT_TRUE(s.canRedo);
}

}  // namespace
