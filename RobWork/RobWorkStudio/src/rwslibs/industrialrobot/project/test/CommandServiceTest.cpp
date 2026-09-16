/**
 * @file   CommandServiceTest.cpp
 * @brief  命令服务用例组（单元面）——S1 形式校验拒绝矩阵（§6.1/§6.3）、
 *         S3 处理器三态（§5.3.2）、S4 确认决策映射（§5.3.3/§6.7）、
 *         S6/S7 提交与事件（§6.8/D-18）、注册表契约（§5.3.5）、载荷
 *         版本策略与逆命令持久化（§6.4/§6.9）、hasUnresolvedPayload
 *         判据注入（§6.3 末行/T09 注入点）、元数据增量机制面（§4.5.1
 *         走查语义/P-PR-9 不越界）、P-PR-6 处置自证与确认放行流
 *         （PRJ-T11——PRJ-TX-2 四路/绑定与取消路径/确认留痕/UX-03）。
 *
 * 设计依据：
 *   - units/project.md §5.3（命令端口各节）、§6（S1～S7/拒绝语义/事件）、
 *     §11 PRJ-TX 组（本组承接其 T10/T11 落位面的单元内半区；PRJ-TX-1/
 *     TX-3 的契约测试半区见 CommandContractTest.cpp——§3.3 测试目标分工）；
 *   - 需求 ARC-01（命令原子产生修订）、PM-04（过期基线——契约测试承载）、
 *     SA-15（确认放行）、MDL-06（双编译——契约测试承载；MDL-06④ 可确认
 *     诊断放行——PRJ-T11 组）、D-18（事件发布失败不回滚）、UX-03（确认
 *     对话数据齐备）、ERR-01（诊断字段完整）；
 *   - 任务契约 tasks/foundation/PRJ-T10.json acceptance 3/4/5 与 tasks/
 *     foundation/PRJ-T11.json acceptance 1～4：acceptance 1＝PRJ-TX-2 四
 *     路拒绝（硬断言/无交互/拒绝/绑定失效）均无修订＋command.json 无新
 *     记录＋诊断字段完整；acceptance 2＝确认等待占槽/零事务资源/关闭取
 *     消（无永久等待）＋interaction 生命周期（挂起/恢复/丢失）；acceptance
 *     3＝确认留痕入 command.json（round-trip）＋UX-03 回调数据面；
 *     acceptance 4＝零 Widgets（回调纯接口）＋P-PR-7 签名不私改＋P-PR-6
 *     经 sink 注入。
 *
 * 测试口径登记（DTB §5.4）：
 *   1. 内置元数据命令族（project.create-branch 等，§6.5 含点示例）不落
 *      位——P-PR-9 待所有者裁决（§15.3，PRJ-T04 登记）；建支**机制面**
 *      经无点测试处理器 test-create-branch 走 CommandPlan.metadataChange
 *      声明面验证（元数据增量装配/发布校验/查询视图）。
 *   2. "无修订落盘"的观测点＝磁盘 revisions/ 目录计数＋查询端口视图
 *      双通道（地面事实优先——ProjectStoreTest 口径 2 同源）；"command.
 *      json 无新记录"的观测点＝磁盘逐修订目录文件存在性（acceptance 1
 *      ——无修订 ⇒ 无新 command.json，二者同源）。
 *   3. 事件观测经 core ReferenceEventBus（测试内参考总线——生产总线归
 *      execution/ui，T-1 边界）；事件发布失败注入经 FailingEventBus。
 *   4. 绑定失配分支的 submit 级注入经 PerturbingProbe（D-10 接缝的测试
 *      侧 fake——§6.7 失配分支生产不可达，ConfirmationFlow.hpp 实现口径
 *      ⑤）；四成员逐一失配的判定面另设纯函数级用例（BindingVerify 组）。
 *   5. "诊断字段完整"的观测＝显式逐字段断言（ERR-01 必填字段非空＋比较
 *      型出席）——testkit ContractCheck 谓词的消费随 PRJ-T15 按 T-1 允许
 *      形态登记（本目标不链 testkit——CMakeLists 登记口径），diagnostics
 *      单元的 DiagContractCheckTest 已钉住该谓词与本断言的同判性。
 */

#include "Codec.hpp"
#include "StubHandlers.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <fstream>
#include <future>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

namespace sdurws::ird::project::stub {

namespace {

/// 空 token 处理器桩（注册表负例用例的注入件——用例组局部定义，避免
/// 污染共享头）。
class EmptyTokenHandler final : public ICommandHandler {
public:
    [[nodiscard]] std::string commandType() const override { return ""; }
    [[nodiscard]] std::uint32_t currentPayloadVersion() const override { return 1; }
    PrepareOutcome prepare(HandlerContext&, const CommandEnvelope&,
                           const RevisionView&, CommandPlan&,
                           std::vector<core::DiagnosticRecord>&) override
    {
        return PrepareOutcome::Planned;
    }
};

/// 必失败总线（D-18 反证面）：publish 一律抛——验证"事件发布失败不回滚
/// 已提交修订"（§6.8；重试一次＋开发诊断在 TxEngine）。单 TU 定义（与
/// TxEngineTest 的 FakeBus 同款形态——注入桩随用例文件自持）。
class FailingEventBus final : public core::IDomainEventBus {
public:
    void publish(const core::DomainEvent&) override
    {
        throw std::runtime_error("injected bus failure（D-18 注入）");
    }

    std::unique_ptr<core::IEventSubscription> subscribe(core::IDomainEventSink&) override
    {
        return nullptr;  // 本桩只投递——订阅面不被 D-18 用例消费
    }
};

}  // namespace

// =====================================================================
// 注册表契约（§5.3.5）
// =====================================================================

/// 注册清单投影：确定性排序（NFR-COR-02——字典序）＋size 一致。
TEST(CommandRegistry, RegisteredTypes_ListsSortedTokens)
{
    HandlerRegistry registry;
    registry.registerHandler(std::make_unique<HardAssertFailHandler>());
    registry.registerHandler(std::make_unique<AppendObjectHandler>());
    registry.registerHandler(std::make_unique<InvalidInputHandler>());

    const std::vector<std::string> tokens = registry.registeredCommandTypes();
    ASSERT_EQ(tokens.size(), 3u);
    EXPECT_EQ(tokens[0], "test-append-object");   // 字典序
    EXPECT_EQ(tokens[1], "test-hard-assert-fail");
    EXPECT_EQ(tokens[2], "test-invalid-input");
    EXPECT_EQ(registry.size(), 3u);
    // find 命中/未命中（未注册＝nullptr——S1 unknown-command 判据）。
    EXPECT_NE(registry.find("test-append-object"), nullptr);
    EXPECT_EQ(registry.find("test-no-such"), nullptr);
}

/// 重复 token 注册＝边界拒绝（§5.3.5 注释原文；实现口径⑥ fail-fast）。
TEST(CommandRegistry, DuplicateRegistration_RejectedInvalidArgument)
{
    HandlerRegistry registry;
    registry.registerHandler(std::make_unique<AppendObjectHandler>());
    EXPECT_THROW(registry.registerHandler(std::make_unique<AppendObjectHandler>()),
                 std::invalid_argument);
    // 空处理器/空 token 同为装配错误。
    EXPECT_THROW(registry.registerHandler(nullptr), std::invalid_argument);
    EXPECT_THROW(registry.registerHandler(std::make_unique<EmptyTokenHandler>()),
                 std::invalid_argument);
    EXPECT_EQ(registry.size(), 1u);
}

// =====================================================================
// S1 形式校验（§6.1 步 1/§6.3；实现口径①②）
// =====================================================================

/// 未知 commandType → Rejected(unknown-command)，无修订（§6.3 行 1）。
TEST(CommandSubmitS1, UnknownCommandType_RejectedNoRevision)
{
    ServiceFixture fx;
    fx.registerDefaults();
    const std::size_t revisionsBefore = fx.revisionDirCount();

    CommandEnvelope envelope = fx.appendEnvelope("payload", fx.primaryBranch());
    envelope.commandType = "test-no-such";
    const CommandResult result = fx.commands().submit(envelope);

    EXPECT_TRUE(result.rejected());
    EXPECT_EQ(result.status.rejection, CommandStatus::Rejection::UnknownCommand);
    EXPECT_FALSE(result.newRevision.has_value());
    EXPECT_EQ(fx.revisionDirCount(), revisionsBefore);  // 无修订落盘
    EXPECT_FALSE(fx.sink.hasUserCode("PRJ-STALE-REVISION-REJECTED"));
}

/// payload 版本不受理 → Rejected(invalid-payload)（§6.3 行 2/§6.4）。
TEST(CommandSubmitS1, PayloadVersionMismatch_RejectedInvalidPayload)
{
    ServiceFixture fx;
    fx.registerDefaults();

    CommandEnvelope envelope = fx.appendEnvelope("payload", fx.primaryBranch());
    envelope.payloadFormatVersion = 99;  // 处理器受理版本＝1
    const CommandResult result = fx.commands().submit(envelope);

    EXPECT_TRUE(result.rejected());
    EXPECT_EQ(result.status.rejection, CommandStatus::Rejection::InvalidPayload);
    EXPECT_FALSE(result.newRevision.has_value());
}

/// 目标分支不在权威分支表 → Rejected(invalid-payload)（实现口径①——
/// 封闭集无"未知分支"稳定码，不私扩）。
TEST(CommandSubmitS1, UnknownBranch_RejectedInvalidPayload)
{
    ServiceFixture fx;
    fx.registerDefaults();

    CommandEnvelope envelope = fx.appendEnvelope("payload",
                                                 core::BranchId::generate());
    const CommandResult result = fx.commands().submit(envelope);

    EXPECT_TRUE(result.rejected());
    EXPECT_EQ(result.status.rejection, CommandStatus::Rejection::InvalidPayload);
    EXPECT_FALSE(result.newRevision.has_value());
}

/// 只读上下文提交 → Rejected(not-writable)＋PRJ-LOCK-HELD（§6.1 S1/
/// §9.6；error＝LockHeldByOther——实现口径②；PM-07"只读可查看禁编辑"）。
TEST(CommandSubmitS1, ReadOnlyContext_RejectedNotWritableWithLockHeldDiag)
{
    ServiceFixture fx;
    fx.registerDefaults();

    // 第二实例只读打开（进程内同一项目——Writable 重复打开被 §9.3 拒绝，
    // 显式 ReadOnly 合法）。处理器按装配注册进**该上下文自己的**注册表
    // （注册表随上下文实例——拒绝面须是 not-writable 而非 unknown）。
    OpenStoreRequest request;
    request.path = fx.dir;
    request.mode = OpenMode::ReadOnly;
    request.diagnostics = &fx.sink;
    OpenStoreResult viewer = ProjectStoreFactory::open(request);
    ASSERT_TRUE(viewer.store != nullptr);
    EXPECT_FALSE(viewer.writable);
    dynamic_cast<ProjectStoreImpl*>(viewer.store.get())
        ->commandService()
        .registry()
        .registerHandler(std::make_unique<AppendObjectHandler>());

    CommandEnvelope envelope = fx.appendEnvelope("payload", fx.primaryBranch());
    const CommandResult result = viewer.store->commands().submit(envelope);

    EXPECT_TRUE(result.rejected());
    EXPECT_EQ(result.status.rejection, CommandStatus::Rejection::NotWritable);
    ASSERT_TRUE(result.error.has_value());
    EXPECT_EQ(result.error->code(), StoreErrorCode::LockHeldByOther);
    EXPECT_TRUE(fx.sink.hasUserCode("PRJ-LOCK-HELD"));  // P-PR-6 码值收编清单
    viewer.store.reset();
}

/// 关闭后提交 → Rejected(not-writable)＋PRJ-WRITE-AUTHORITY-LOST
/// （§9.6① 状态机门卫的 S1 提前面；PRJ-TX-9 迟到写拒绝的命令端口落位）。
TEST(CommandSubmitS1, ClosedContext_RejectedNotWritableWithAuthorityDiag)
{
    ServiceFixture fx;
    fx.registerDefaults();
    const core::BranchId mainBranch = fx.primaryBranch();  // 关闭前取（查询
    // 端口 Closed 后拒绝——§4.7，信封数据须先行装配）
    (void)fx.opened.store->requestClose();  // 返回在途数（0——无在途）
    ASSERT_TRUE(fx.opened.store->closed());

    CommandEnvelope envelope = fx.appendEnvelope("payload", mainBranch);
    const CommandResult result = fx.commands().submit(envelope);

    EXPECT_TRUE(result.rejected());
    EXPECT_EQ(result.status.rejection, CommandStatus::Rejection::NotWritable);
    ASSERT_TRUE(result.error.has_value());
    EXPECT_EQ(result.error->code(), StoreErrorCode::ContextClosed);
    EXPECT_TRUE(fx.sink.hasUserCode("PRJ-WRITE-AUTHORITY-LOST"));
}

// =====================================================================
// S3 处理器三态（§5.3.2 prepare 契约）
// =====================================================================

/// 硬断言失败 → Rejected(hard-assert-failed)＋定位诊断回传，无修订
/// （§6.3 行 3——MDL-06 就地阻止；处理器产出诊断原样随结果交付）。
TEST(CommandSubmitS3, HardAssertFailed_RejectedWithDiagnosticsNoRevision)
{
    ServiceFixture fx;
    fx.registerDefaults();
    const std::size_t revisionsBefore = fx.revisionDirCount();

    CommandEnvelope envelope;
    envelope.branch = fx.primaryBranch();
    envelope.commandType = "test-hard-assert-fail";
    envelope.payloadFormatVersion = 1;
    const CommandResult result = fx.commands().submit(envelope);

    EXPECT_TRUE(result.rejected());
    EXPECT_EQ(result.status.rejection, CommandStatus::Rejection::HardAssertFailed);
    ASSERT_EQ(result.diagnostics.size(), 1u);          // 处理器诊断透传
    EXPECT_EQ(result.diagnostics[0].code, "TEST-HARD-ASSERT-FAILED");
    EXPECT_FALSE(result.newRevision.has_value());
    EXPECT_EQ(fx.revisionDirCount(), revisionsBefore);  // 无修订落盘
}

/// 非法输入 → Rejected(invalid-payload)＋逐项诊断（§6.3 行 2 后半）。
TEST(CommandSubmitS3, PrepareInvalidInput_RejectedWithDiagnostics)
{
    ServiceFixture fx;
    fx.registerDefaults();

    CommandEnvelope envelope;
    envelope.branch = fx.primaryBranch();
    envelope.commandType = "test-invalid-input";
    envelope.payloadFormatVersion = 1;
    const CommandResult result = fx.commands().submit(envelope);

    EXPECT_TRUE(result.rejected());
    EXPECT_EQ(result.status.rejection, CommandStatus::Rejection::InvalidPayload);
    ASSERT_EQ(result.diagnostics.size(), 1u);
    EXPECT_EQ(result.diagnostics[0].code, "TEST-INVALID-INPUT");
    EXPECT_FALSE(result.newRevision.has_value());
}

// =====================================================================
// S4 确认放行决策映射（§5.3.3/§6.7）
// =====================================================================

/// 非交互提交＋待确认集 → Rejected(confirmations-unresolved)，findings
/// 回传（§6.7 末行"未确认则阻止应用"——测试/后台通道）。
TEST(CommandSubmitS4, FindingsWithoutInteraction_RejectedConfirmationsUnresolved)
{
    ServiceFixture fx;
    fx.registerDefaults();

    CommandEnvelope envelope;
    envelope.branch = fx.primaryBranch();
    envelope.commandType = "test-confirm-required";
    envelope.payloadFormatVersion = 1;
    const CommandResult result = fx.commands().submit(envelope);

    EXPECT_TRUE(result.rejected());
    EXPECT_EQ(result.status.rejection,
              CommandStatus::Rejection::ConfirmationsUnresolved);
    EXPECT_EQ(result.findings.size(), 1u);  // 未决集回传（§5.3.1）
    EXPECT_FALSE(result.newRevision.has_value());
}

/// 交互整体拒绝（nullopt）→ Rejected(confirmations-rejected)（§5.3.3
/// "返回空……→ confirmations-rejected"）。
TEST(CommandSubmitS4, InteractionRejects_RejectedConfirmationsRejected)
{
    ServiceFixture fx;
    fx.registerDefaults();
    StubInteraction interaction;
    interaction.rejectAll = true;

    CommandEnvelope envelope;
    envelope.branch = fx.primaryBranch();
    envelope.commandType = "test-confirm-required";
    envelope.payloadFormatVersion = 1;
    const CommandResult result = fx.commands().submit(envelope, &interaction);

    EXPECT_TRUE(result.rejected());
    EXPECT_EQ(result.status.rejection,
              CommandStatus::Rejection::ConfirmationsRejected);
    EXPECT_EQ(result.findings.size(), 1u);
    EXPECT_EQ(interaction.lastFindings.size(), 1u);  // 回调确实收到待确认集
    EXPECT_FALSE(result.newRevision.has_value());
}

/// 回调失效（isAlive==false）→ Aborted(interaction-lost)，无修订
/// （§5.3.3 生命周期契约）。
TEST(CommandSubmitS4, InteractionDead_AbortedInteractionLost)
{
    ServiceFixture fx;
    fx.registerDefaults();
    StubInteraction interaction;
    interaction.alive = false;

    CommandEnvelope envelope;
    envelope.branch = fx.primaryBranch();
    envelope.commandType = "test-confirm-required";
    envelope.payloadFormatVersion = 1;
    const CommandResult result = fx.commands().submit(envelope, &interaction);

    EXPECT_TRUE(result.aborted());
    EXPECT_EQ(result.status.abort, CommandStatus::Abort::InteractionLost);
    EXPECT_EQ(interaction.lastFindings.size(), 0u);  // 失效即不进回调
    EXPECT_FALSE(result.newRevision.has_value());
}

/// 回调抛出 → Aborted(interaction-lost)（§5.3.3"调用抛出"路径）。
TEST(CommandSubmitS4, InteractionThrows_AbortedInteractionLost)
{
    ServiceFixture fx;
    fx.registerDefaults();
    StubInteraction interaction;
    interaction.throwInCallback = true;

    CommandEnvelope envelope;
    envelope.branch = fx.primaryBranch();
    envelope.commandType = "test-confirm-required";
    envelope.payloadFormatVersion = 1;
    const CommandResult result = fx.commands().submit(envelope, &interaction);

    EXPECT_TRUE(result.aborted());
    EXPECT_EQ(result.status.abort, CommandStatus::Abort::InteractionLost);
    EXPECT_FALSE(result.newRevision.has_value());
}

/// 确认放行 → Committed（S4 正路——决策向量与 findings 一一对应；
/// 凭据绑定复核与 command.json 留痕随 PRJ-T11，§12 分工）。
TEST(CommandSubmitS4, InteractionConfirms_Committed)
{
    ServiceFixture fx;
    fx.registerDefaults();
    StubInteraction interaction;

    CommandEnvelope envelope;
    envelope.branch = fx.primaryBranch();
    envelope.commandType = "test-confirm-required";
    envelope.payloadFormatVersion = 1;
    const CommandResult result = fx.commands().submit(envelope, &interaction);

    EXPECT_TRUE(result.committed());
    ASSERT_TRUE(result.newRevision.has_value());
    EXPECT_TRUE(result.newHeadState.has_value());  // 新 HEAD 视图随附
}

// =====================================================================
// S6/S7 提交、事件与载荷策略（§6.4/§6.8/D-18）
// =====================================================================

/// 正常提交：恰好一个新修订＋seq 单调＋摘要/载荷持久化＋事件 FIFO
/// （RevisionCommitted→DependencyInvalidated，同修订——§6.8）＋负载
/// 原样存储（D-10 透传）＋parent＝原 tip。
TEST(CommandSubmitS6, AppendObject_CommittedExactlyOneRevisionWithEvents)
{
    ServiceFixture fx;
    fx.registerDefaults();

    // 前置基线：HEAD（r0）与分支 tip。
    const RevisionView headBefore = fx.opened.store->query().head();
    const core::BranchId mainBranch = fx.primaryBranch();

    CommandEnvelope envelope = fx.appendEnvelope("hello-objects", mainBranch);
    const CommandResult result = fx.commands().submit(envelope);

    // 终态：Committed{newRevision}，恰好一个新修订。
    ASSERT_TRUE(result.committed());
    ASSERT_TRUE(result.newRevision.has_value());
    ASSERT_TRUE(result.newHeadState.has_value());
    const RevisionView& view = *result.newHeadState;
    EXPECT_EQ(view.id, *result.newRevision);
    ASSERT_TRUE(view.parent.has_value());
    EXPECT_EQ(*view.parent, headBefore.id);
    EXPECT_EQ(view.branch, mainBranch);
    EXPECT_EQ(view.commandSummary, "test append object");
    EXPECT_EQ(view.seq, headBefore.seq + 1);  // seq 单调（O-15）

    // 负载原样存储（D-10——对象字节即信封载荷；经②端口读回）。
    bool payloadFound = false;
    for (const ObjectRef& ref : view.objectRefs) {
        if (ref.objectTypeToken == "TestData") {
            const auto bytes = fx.opened.store->query().tryObject(
                ref.objectId, ref.contentVersion);
            ASSERT_TRUE(bytes.has_value());
            const std::string stored(bytes->begin(), bytes->end());
            EXPECT_EQ(stored, "hello-objects");
            payloadFound = true;
        }
    }
    EXPECT_TRUE(payloadFound);

    // 事件 FIFO（S7 在事务第 6 步内——同发布者序）：Committed→Invalidated，
    // 载荷同一修订（§6.8）。
    ASSERT_GE(fx.events.events.size(), 2u);
    const core::DomainEvent& committed = fx.events.events[fx.events.events.size() - 2];
    const core::DomainEvent& invalidated = fx.events.events.back();
    EXPECT_EQ(committed.kind, core::DomainEventKind::RevisionCommitted);
    EXPECT_EQ(invalidated.kind, core::DomainEventKind::DependencyInvalidated);
    EXPECT_EQ(committed.asRevisionCommitted().revision, *result.newRevision);
    EXPECT_EQ(invalidated.asDependencyInvalidated().revision, *result.newRevision);
    EXPECT_EQ(committed.asRevisionCommitted().parent.has_value()
                  && *committed.asRevisionCommitted().parent == headBefore.id,
              true);
}

/// 对象身份双路径（实现口径——两条"project 分配"路径并存）：prepare 期
/// ctx.objectId() 取号与 S6 装配点补分配的提交都恰好落一个域对象。
TEST(CommandSubmitS6, ObjectIdAllocation_BothPathsCommit)
{
    ServiceFixture fx;
    auto withPrepareId = std::make_unique<AppendObjectHandler>();
    withPrepareId->allocateInPrepare = true;
    fx.registry().registerHandler(std::move(withPrepareId));

    const core::BranchId mainBranch = fx.primaryBranch();
    const std::size_t objectsBefore = fx.objectDirCount();

    CommandEnvelope viaCtx = fx.appendEnvelope("ctx-allocated", mainBranch);
    const CommandResult r1 = fx.commands().submit(viaCtx);
    ASSERT_TRUE(r1.committed());

    CommandEnvelope viaAssembly = fx.appendEnvelope("assembly-allocated", mainBranch);
    const CommandResult r2 = fx.commands().submit(viaAssembly);
    ASSERT_TRUE(r2.committed());

    // 对象库目录计数：每次提交恰新增 2 个 oid 目录（1 域对象＋1 元数据
    // 对象版本——元数据亦为对象文件，§4.4.3"既是对象负载又是自有格式"）。
    // 每次提交恰一域对象由下方 TestData 引用集断言精确化。
    EXPECT_EQ(fx.objectDirCount(), objectsBefore + 4);
    for (const RevisionId rev : {*r1.newRevision, *r2.newRevision}) {
        const RevisionView view = fx.opened.store->query().revision(rev);
        std::size_t domainObjects = 0;
        for (const ObjectRef& ref : view.objectRefs) {
            if (ref.objectTypeToken == "TestData") {
                ++domainObjects;
            }
        }
        EXPECT_EQ(domainObjects, 1u);
    }
}

/// 事件发布失败不回滚（D-18）：总线抛出 → 提交事实不受影响（Committed＋
/// 修订可查）；开发诊断通道保留重试证据（TxEngine 每败即诊断）。
TEST(CommandSubmitS7, EventPublishFailure_DoesNotRollbackRevision)
{
    FailingEventBus failingBus;
    StubSink sink;
    static std::atomic<unsigned long long> seq{0};
    const std::filesystem::path dir = std::filesystem::temp_directory_path()
        / ("ird-prj-t10-d18-" + std::to_string(seq.fetch_add(1)));
    // 前置清理：上次进程运行可能残留同名目录（异常路径跳过末尾清理——
    // createNew 前置"不存在或空目录"会拒绝）。
    std::error_code cleanupEc;
    std::filesystem::remove_all(dir, cleanupEc);
    OpenStoreResult opened = ProjectStoreFactory::createNew(
        dir, "d18 project", &failingBus, &sink);
    auto& service = dynamic_cast<ProjectStoreImpl*>(opened.store.get())
                        ->commandService();
    service.registry().registerHandler(std::make_unique<AppendObjectHandler>());

    CommandEnvelope envelope;
    envelope.branch
        = opened.store->query().currentMetadata().record.primaryBranchId;
    envelope.commandType = "test-append-object";
    envelope.payloadFormatVersion = 1;
    envelope.payloadCanonical = {'x'};

    const CommandResult result = opened.store->commands().submit(envelope);

    // D-18：提交事实不被事件失败回滚——修订已落盘可查。
    EXPECT_TRUE(result.committed());
    const RevisionView view = opened.store->query().revision(*result.newRevision);
    EXPECT_EQ(view.branch, envelope.branch);
    // 开发诊断可见（重试证据——不静默）。
    EXPECT_TRUE(sink.devContains("revision-committed")
                || sink.devContains("dependency-invalidated"));
    opened.store.reset();
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

/// 载荷版本策略＋逆命令持久化（§6.4/§6.9）：声明逆命令的提交把三元组
/// 随修订留痕；重开项目后视图仍可读（磁盘 round-trip）。
TEST(CommandSubmitS6, InverseDeclaration_PersistedAndRoundTrips)
{
    ServiceFixture fx;
    auto reversible = std::make_unique<AppendObjectHandler>();
    reversible->declareInverse = true;
    fx.registry().registerHandler(std::move(reversible));

    CommandEnvelope envelope = fx.appendEnvelope("reversible-payload",
                                                 fx.primaryBranch());
    const CommandResult result = fx.commands().submit(envelope);
    ASSERT_TRUE(result.committed());

    // 重开（新上下文——磁盘读回路径；注册同款处理器——否则"test-append-
    // object"对该上下文为未知类型，§6.3 末行会如实标记 unresolved，与本
    // 用例关注的逆命令 round-trip 断言无关）。
    fx.opened.store.reset();
    OpenStoreRequest request;
    request.path = fx.dir;
    request.eventBus = &fx.bus;
    request.diagnostics = &fx.sink;
    OpenStoreResult reopened = ProjectStoreFactory::open(request);
    ASSERT_TRUE(reopened.store != nullptr);
    dynamic_cast<ProjectStoreImpl*>(reopened.store.get())
        ->commandService()
        .registry()
        .registerHandler(std::make_unique<AppendObjectHandler>());

    const RevisionView head = reopened.store->query().head();
    ASSERT_TRUE(head.inverse.has_value());
    EXPECT_EQ(head.inverse->commandType, "test-append-undo");
    EXPECT_EQ(head.inverse->payloadFormatVersion, 1u);  // 处理器当前受理版本
    const std::string inversePayload(head.inverse->payloadCanonical.begin(),
                                     head.inverse->payloadCanonical.end());
    EXPECT_EQ(inversePayload, "reversible-payload");
    EXPECT_FALSE(head.hasUnresolvedPayload);
    reopened.store.reset();
}

/// 历史修订中的未知命令类型（§6.3 末行/T09 注入点真判定）：带注册提交
/// 后，卸载注册表重开 → hasUnresolvedPayload=true；带注册重开 → false。
TEST(CommandSubmitS6, UnknownCommandTypeInHistory_MarkedUnresolvedAfterReopen)
{
    ServiceFixture fx;
    fx.registerDefaults();
    CommandEnvelope envelope = fx.appendEnvelope("history", fx.primaryBranch());
    const CommandResult result = fx.commands().submit(envelope);
    ASSERT_TRUE(result.committed());
    // 当前会话：类型已注册 → resolved。
    EXPECT_FALSE(fx.opened.store->query().head().hasUnresolvedPayload);
    fx.opened.store.reset();

    // 无注册重开：payload 不解析但修订可读（历史浏览 PM-12-S1 不受影响）。
    OpenStoreRequest bare;
    bare.path = fx.dir;
    OpenStoreResult bareOpen = ProjectStoreFactory::open(bare);
    ASSERT_TRUE(bareOpen.store != nullptr);
    const RevisionView head = bareOpen.store->query().head();
    EXPECT_EQ(head.commandSummary, "test append object");  // 摘要可读
    EXPECT_TRUE(head.hasUnresolvedPayload);                // payload 不解析
    bareOpen.store.reset();

    // 带注册重开：真判定恢复 resolved（T10 注入后——QueryPort 增量落位
    // 说明 3 的"届时真判定"兑现；注册经内部通道——同单元私有头先例）。
    OpenStoreRequest withRegistry;
    withRegistry.path = fx.dir;
    OpenStoreResult regOpen = ProjectStoreFactory::open(withRegistry);
    ASSERT_TRUE(regOpen.store != nullptr);
    dynamic_cast<ProjectStoreImpl*>(regOpen.store.get())
        ->commandService()
        .registry()
        .registerHandler(std::make_unique<AppendObjectHandler>());
    EXPECT_FALSE(regOpen.store->query().head().hasUnresolvedPayload);
    regOpen.store.reset();
}

// =====================================================================
// 元数据增量机制面（§4.5.1 走查语义；P-PR-9 不越界——无点测试处理器）
// =====================================================================

/// 建支型提交（实现口径③——走查步骤 1 形态）：无 tipUpdate＋
/// addedBranches（base/tip＝源分支 tip）；CreateBranch 修订 parent＝源
/// 分支 tip、非任何分支 tip；既有分支 tip 不前进；查询端口双分支可见。
TEST(CommandSubmitS6, CreateBranchMechanics_NoTipUpdateAddedBranchVisible)
{
    ServiceFixture fx;
    fx.registerDefaults();

    // 前置：主分支上先提交一次（r1——与走查 r0→CreateBranch 可区分）。
    const core::BranchId mainBranch = fx.primaryBranch();
    const CommandResult r1 = fx.commands().submit(
        fx.appendEnvelope("base-content", mainBranch));
    ASSERT_TRUE(r1.committed());
    const core::RevisionId mainTip = *r1.newRevision;

    // 建支命令（源＝main，label 走查形态）。
    CommandEnvelope createBranch;
    createBranch.branch = mainBranch;  // 提交记录在活动分支（走查：会话所在分支）
    createBranch.commandType = "test-create-branch";
    createBranch.payloadFormatVersion = 1;
    const std::string payload = mainBranch.toCanonical() + "\n设计A";
    createBranch.payloadCanonical.assign(payload.begin(), payload.end());
    const CommandResult r2 = fx.commands().submit(createBranch);
    ASSERT_TRUE(r2.committed());

    // 走查语义断言（步骤 1 的 M1 形态）：
    const RevisionView head = fx.opened.store->query().head();
    EXPECT_EQ(head.branch, mainBranch);                      // 提交时活动分支
    ASSERT_TRUE(head.parent.has_value());
    EXPECT_EQ(*head.parent, mainTip);                        // parent＝源分支 tip
    EXPECT_EQ(head.commandSummary, "test create branch");

    const std::vector<BranchTip> tips = fx.opened.store->query().branchTips();
    ASSERT_EQ(tips.size(), 2u);                              // main＋新分支
    bool mainFound = false;
    bool createdFound = false;
    for (const BranchTip& tip : tips) {
        if (tip.id == mainBranch) {
            // 既有分支 tip 不前进（无 tipUpdate——走查 M1{main→r0} 形态）。
            EXPECT_EQ(tip.tip, mainTip);
            mainFound = true;
        } else {
            // 新条目 base/tip＝源分支 tip（一次写入——P-PR-8）。
            EXPECT_EQ(tip.base, mainTip);
            EXPECT_EQ(tip.tip, mainTip);
            EXPECT_EQ(tip.label, "设计A");
            createdFound = true;
        }
    }
    EXPECT_TRUE(mainFound && createdFound);

    // 闭包可达（D-4 双通道——CreateBranch 修订非 tip 不成孤岛）：
    // main 的历史沿 parent 链以 main 的 tip 为首（r2 不改 main tip——
    // 无 tipUpdate；head r2 经谱系通道可达，二者不同属一 tip 链正是
    // 走查步骤 1 的 M1 形态）。
    const std::vector<RevisionView> history
        = fx.opened.store->query().branchHistory(mainBranch, 10);
    ASSERT_GE(history.size(), 2u);
    EXPECT_EQ(history[0].id, mainTip);  // main tip 未被建支修订前进
}

/// R2 预留字段拒绝面（文件头"增量落位说明 1"）：newDisplayName 产出即
/// Rejected(invalid-payload)——防静默丢弃（R2 改名随其任务落位）。
TEST(CommandSubmitS6, MetadataDisplayNameR2Reserved_RejectedInvalidPayload)
{
    ServiceFixture fx;
    fx.registerDefaults();
    dynamic_cast<CreateBranchHandler*>(
        fx.registry().find("test-create-branch"))
        ->declareDisplayName = true;

    const std::size_t revisionsBefore = fx.revisionDirCount();
    CommandEnvelope envelope;
    envelope.branch = fx.primaryBranch();
    envelope.commandType = "test-create-branch";
    envelope.payloadFormatVersion = 1;
    const std::string payload
        = fx.primaryBranch().toCanonical() + "\n分支";
    envelope.payloadCanonical.assign(payload.begin(), payload.end());
    const CommandResult result = fx.commands().submit(envelope);

    EXPECT_TRUE(result.rejected());
    EXPECT_EQ(result.status.rejection, CommandStatus::Rejection::InvalidPayload);
    EXPECT_EQ(fx.revisionDirCount(), revisionsBefore);  // 无修订落盘
}

// =====================================================================
// 命令执行槽与装配违约（§6.1 串行化/§5.3.6 注入面）
// =====================================================================

/// 全局串行（§6.1"每存储上下文同时至多一个命令在执行"的直接观测）：
/// 槽内命令阻塞期间，第二提交不得进入 prepare；放行后二者各恰好一修订。
/// （双线程并发提交的端到端面——seq 单调/线性链——归契约测试 PRJ-TX-1。）
TEST(CommandSlot, SecondSubmitWaitsWhileFirstInPrepare)
{
    ServiceFixture fx;
    fx.registerDefaults();
    auto gateHandler = std::make_unique<GateHandler>();
    GateHandler* gate = gateHandler.get();
    fx.registry().registerHandler(std::move(gateHandler));

    const core::BranchId mainBranch = fx.primaryBranch();

    // 线程 A：提交 test-gated——在 prepare 内阻塞。
    CommandEnvelope gatedEnvelope;
    gatedEnvelope.branch = mainBranch;
    gatedEnvelope.commandType = "test-gated";
    gatedEnvelope.payloadFormatVersion = 1;
    auto firstDone = std::async(std::launch::async, [&fx, gatedEnvelope] {
        return fx.commands().submit(gatedEnvelope);
    });

    // 等待 A 进入 prepare（有界轮询——不预设调度时序）。
    for (int i = 0; i < 100 && gate->prepareEntered.load() == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_GE(gate->prepareEntered.load(), 1);

    // 主线程提交 append——A 持槽期间不得完成（轮询观测"等待"）。
    CommandEnvelope appendEnvelope = fx.appendEnvelope("queued", mainBranch);
    auto secondDone = std::async(std::launch::async,
                                 [&fx, appendEnvelope] {
                                     return fx.commands().submit(appendEnvelope);
                                 });
    EXPECT_EQ(secondDone.wait_for(std::chrono::milliseconds(300)),
              std::future_status::timeout);  // 第二个在等待（§6.1）

    gate->release();  // 放行 A——B 随后应串行完成
    const CommandResult firstResult = firstDone.get();
    const CommandResult secondResult = secondDone.get();
    EXPECT_TRUE(firstResult.committed());
    EXPECT_TRUE(secondResult.committed());

    // 串行的结果面：两修订 seq 严格递增、线性链（B 的 parent＝A）。
    const RevisionView a = fx.opened.store->query().revision(*firstResult.newRevision);
    const RevisionView b = fx.opened.store->query().revision(*secondResult.newRevision);
    EXPECT_EQ(b.seq, a.seq + 1);
    ASSERT_TRUE(b.parent.has_value());
    EXPECT_EQ(*b.parent, a.id);
}

/// requiresDualCompile 而编译端口未注入＝装配违约 fail-fast（实现口径
/// ——L5 装配面缺失是装配错误，不是运行期稳定码面）。
TEST(CommandSlot, DualCompileWithoutPort_FailsFastInvalidArgument)
{
    ServiceFixture fx;  // 未注入编译端口（宿主装配默认空）
    fx.registerDefaults();

    CommandEnvelope envelope;
    envelope.branch = fx.primaryBranch();
    envelope.commandType = "test-dual-compile";
    envelope.payloadFormatVersion = 1;
    envelope.payloadCanonical = {'y'};
    EXPECT_THROW(fx.commands().submit(envelope), std::invalid_argument);
}

// =====================================================================
// 确认放行流（PRJ-T11——§5.3.3/§5.3.4/§6.7；acceptance 1～4）
// =====================================================================

namespace {

/// 构造与 FindingsHandler 同形的比较型 finding（BindingVerify 纯函数级
/// 用例的注入件——四元组计算的输入；M-10 形态测试面，码值＝测试域自有）。
core::ConfirmableFinding makeUnitFinding()
{
    core::ComparativeFields comparison;
    comparison.actual.quantity = core::SourcedValue<double>::provided(
        2.0, core::ValueProvenance::make(core::ProvenanceKind::UserProvided));
    comparison.actual.unit = *core::UnitToken::find("m");
    comparison.expected.quantity = core::SourcedValue<double>::provided(
        1.0, core::ValueProvenance::make(core::ProvenanceKind::UserProvided));
    comparison.expected.unit = *core::UnitToken::find("m");
    return core::ConfirmableFinding::make(core::DiagnosticRecord::make(
        std::string{"TEST-COMPARE-LIMIT"}, std::nullopt, std::nullopt,
        std::nullopt, "test: 超限策略注入（四元组纯函数级用例）",
        "actual=2m expected=1m", "test: 确认或修正输入", comparison));
}

/// 读取修订目录的 command.json 并解析（磁盘地面事实——确认留痕与
/// "无新记录"观测点；解析失败即用例失败，不留假阳性通道）。
CommandRecord readCommandRecord(const std::filesystem::path& dir,
                                const core::RevisionId& rev)
{
    const std::filesystem::path path
        = dir / "revisions" / rev.toCanonical() / "command.json";
    std::ifstream in(path, std::ios::binary);
    EXPECT_TRUE(static_cast<bool>(in)) << "command.json 不可读: "
                                       << path.string();
    const std::string text{std::istreambuf_iterator<char>(in),
                           std::istreambuf_iterator<char>()};
    return codec::parseCommandRecord(text);
}

/// .staging 一级子目录名清单（排除 tmp——D-09 临时目录非事务资源；
/// 事务资源观测点＝是否存在 .staging/<tx-id>）。
std::vector<std::string> stagingTxDirs(const std::filesystem::path& dir)
{
    std::vector<std::string> names;
    std::error_code ec;
    const std::filesystem::path staging = dir / ".staging";
    if (!std::filesystem::exists(staging, ec) || ec) {
        return names;  // .staging 不存在＝零事务资源（常态）
    }
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(staging, ec)) {
        if (ec) {
            break;
        }
        const std::string name = entry.path().filename().string();
        if (entry.is_directory(ec) && !ec && name != "tmp") {
            names.push_back(name);
        }
    }
    return names;
}

/// 提交"test-confirm-required"信封（确认流用例组的标准信封）。
CommandEnvelope confirmEnvelope(const core::BranchId& branch,
                                const std::string& payload)
{
    CommandEnvelope envelope;
    envelope.branch = branch;
    envelope.commandType = "test-confirm-required";
    envelope.payloadFormatVersion = 1;
    envelope.payloadCanonical.assign(payload.begin(), payload.end());
    return envelope;
}

}  // namespace

// ---------------------------------------------------------------------
// acceptance 1——PRJ-TX-2 四路：均无修订、command.json 无新记录、诊断
// 字段完整（§11 PRJ-TX-2 行的 T11 落位面；路径 1～3 的决策映射语义
// 已由 CommandSubmitS4/S3 组钉住，本组补齐"零磁盘副作用＋诊断完整"
// 观测点；路径 4 绑定失效为本任务新增拒绝面）。
// ---------------------------------------------------------------------

/// 路径 1 硬断言失败：Rejected(hard-assert-failed)＋诊断字段完整
/// （ERR-01 必填字段非空），无修订、无新 command.json。
TEST(CommandConfirmTx2, Path1_HardAssert_NoRevisionNoCommandRecord)
{
    ServiceFixture fx;
    fx.registerDefaults();
    const std::size_t revisionsBefore = fx.revisionDirCount();

    CommandEnvelope envelope;
    envelope.branch = fx.primaryBranch();
    envelope.commandType = "test-hard-assert-fail";
    envelope.payloadFormatVersion = 1;
    const CommandResult result = fx.commands().submit(envelope);

    EXPECT_TRUE(result.rejected());
    EXPECT_EQ(result.status.rejection, CommandStatus::Rejection::HardAssertFailed);
    ASSERT_GE(result.diagnostics.size(), 1u);
    // 诊断字段完整（ERR-01：码/上下文/原因/建议动作必填非空——core C-3
    // 的事实面在结果回传后仍成立）。
    EXPECT_FALSE(result.diagnostics[0].code.empty());
    EXPECT_FALSE(result.diagnostics[0].context.empty());
    EXPECT_FALSE(result.diagnostics[0].cause.empty());
    EXPECT_FALSE(result.diagnostics[0].recommendedAction.empty());
    // 零磁盘副作用：无新修订 ⇒ 无新 command.json（同源观测，口径 2）。
    EXPECT_EQ(fx.revisionDirCount(), revisionsBefore);
}

/// 路径 2 无 interaction＋待确认集：Rejected(confirmations-unresolved)
/// ＋findings 回传，无修订、无新 command.json（§6.7 末行）。
TEST(CommandConfirmTx2, Path2_NoInteraction_NoRevisionNoCommandRecord)
{
    ServiceFixture fx;
    fx.registerDefaults();
    const std::size_t revisionsBefore = fx.revisionDirCount();

    const CommandResult result
        = fx.commands().submit(confirmEnvelope(fx.primaryBranch(), "tx2-p2"));

    EXPECT_TRUE(result.rejected());
    EXPECT_EQ(result.status.rejection,
              CommandStatus::Rejection::ConfirmationsUnresolved);
    EXPECT_EQ(result.findings.size(), 1u);
    EXPECT_EQ(result.findings[0].state, core::ConfirmationState::Pending);
    EXPECT_EQ(fx.revisionDirCount(), revisionsBefore);
}

/// 路径 3 interaction 拒绝：Rejected(confirmations-rejected)＋findings
/// 回传，无修订、无新 command.json（§5.3.3"拒绝 → 不产生修订"）。
TEST(CommandConfirmTx2, Path3_InteractionRejects_NoRevisionNoCommandRecord)
{
    ServiceFixture fx;
    fx.registerDefaults();
    StubInteraction interaction;
    interaction.rejectAll = true;
    const std::size_t revisionsBefore = fx.revisionDirCount();

    const CommandResult result = fx.commands().submit(
        confirmEnvelope(fx.primaryBranch(), "tx2-p3"), &interaction);

    EXPECT_TRUE(result.rejected());
    EXPECT_EQ(result.status.rejection,
              CommandStatus::Rejection::ConfirmationsRejected);
    EXPECT_EQ(result.findings.size(), 1u);
    EXPECT_EQ(fx.revisionDirCount(), revisionsBefore);
}

/// 路径 4 确认凭据绑定失效：复核失配 → Rejected(confirmations-unresolved)
/// （§6.7"四者任一不符…命令按未确认处置"——实现口径④），无修订、无新
/// command.json；失配维度进开发诊断（P-PR-6——reportDev 通道，不私造
/// 用户码 CR-08）；findings 保持 Pending（未确认处置的事实面）。
TEST(CommandConfirmTx2, Path4_BindingMismatch_RejectedUnresolvedNoRevision)
{
    ServiceFixture fx;
    fx.registerDefaults();
    StubInteraction interaction;  // 回调正常确认——失配由复核面捕获
    PerturbingProbe probe;        // 冻结点真值、复核点扰动（实现口径⑤）
    probe.member = PerturbingProbe::Member::FindingDigest;
    fx.impl().commandService().setConfirmationProbe(&probe);
    const std::size_t revisionsBefore = fx.revisionDirCount();

    const CommandResult result = fx.commands().submit(
        confirmEnvelope(fx.primaryBranch(), "tx2-p4"), &interaction);

    EXPECT_TRUE(result.rejected());
    EXPECT_EQ(result.status.rejection,
              CommandStatus::Rejection::ConfirmationsUnresolved);
    EXPECT_EQ(result.findings.size(), 1u);
    EXPECT_EQ(result.findings[0].state, core::ConfirmationState::Pending);
    EXPECT_FALSE(result.newRevision.has_value());
    EXPECT_EQ(fx.revisionDirCount(), revisionsBefore);  // 无修订＝无新留痕
    EXPECT_TRUE(fx.sink.devContains("绑定复核失配"));    // 维度定位（dev 通道）
    EXPECT_TRUE(fx.sink.reports.empty());               // 未私造用户级码
    // 探针面：冻结不经探针（生产真值直调），探针只在复核点被咨询——
    // 单 finding 一次复核失配即终止（calls==1，实现口径⑤）。
    EXPECT_EQ(probe.calls, 1);
}

/// 绑定复核纯函数级：四成员逐一失配均可检出且定性正确（acceptance 1
/// "findingDigest/policyContentId/commandDigest/baseRevisionId 任一不符"
/// 的判定面——生产执行序内不可达的失配分支在此逐成员驱动）。
TEST(BindingVerify, EachMemberMismatch_DetectedAndClassified)
{
    const core::ConfirmableFinding finding = makeUnitFinding();
    CommandEnvelope envelope;
    envelope.commandType = "test-confirm-required";
    envelope.payloadFormatVersion = 1;
    envelope.payloadCanonical = {'b', 'i', 'n', 'd'};
    const core::RevisionId base = core::RevisionId::generate();
    core::ConfirmationCredential credential;
    credential.principal = "unit-user";
    credential.confirmedAtUtc = std::chrono::system_clock::now();

    // 冻结（确认时点）与真值采集（复核时点）——同源计算必一致。
    const ConfirmationRecord frozen = confirm::freezeConfirmation(
        envelope, finding, credential, base);
    const confirm::ConfirmationActuals truth
        = confirm::ProductionConfirmationProbe{}.actuals(envelope, finding,
                                                         base);
    EXPECT_EQ(confirm::verifyBinding(frozen, truth),
              confirm::BindingVerdict::Bound);

    // 逐一扰动（四成员序——与 §6.7 四元组序一致）：成员失配 ⇒ 凭据失效
    // （verifyBinding 返回对应定性——实现口径④的判定前提）。
    confirm::ConfirmationActuals perturbed = truth;
    perturbed.findingDigest[0] = (perturbed.findingDigest[0] == '0') ? '1' : '0';
    EXPECT_EQ(confirm::verifyBinding(frozen, perturbed),
              confirm::BindingVerdict::FindingDigestMismatch);

    perturbed = truth;
    perturbed.policyContentId.back()
        = (perturbed.policyContentId.back() == '9') ? '8' : '9';
    EXPECT_EQ(confirm::verifyBinding(frozen, perturbed),
              confirm::BindingVerdict::PolicyContentMismatch);

    perturbed = truth;
    perturbed.commandDigest[0] = (perturbed.commandDigest[0] == '0') ? '1' : '0';
    EXPECT_EQ(confirm::verifyBinding(frozen, perturbed),
              confirm::BindingVerdict::CommandDigestMismatch);

    perturbed = truth;
    perturbed.baseRevisionId = std::string("rev-") + std::string(64, '0');
    EXPECT_EQ(confirm::verifyBinding(frozen, perturbed),
              confirm::BindingVerdict::BaseRevisionMismatch);
}

// ---------------------------------------------------------------------
// acceptance 2——确认绑定与取消路径：等待占用命令执行槽、零事务资源、
// 可被关闭取消（§5.3.4/D-07——无永久等待）；interaction 生命周期
// （挂起/恢复/丢失——§5.3.3/§5.6）。
// ---------------------------------------------------------------------

/// 确认等待占用命令执行槽（§5.3.4"确认等待占用命令执行槽"的直接观测）：
/// 回调挂起期间第二提交不得完成；放行后二者串行完成（seq 递增＋线性链）。
TEST(CommandConfirmLifecycle, ConfirmWait_HoldsSlot_SecondSubmitQueued)
{
    ServiceFixture fx;
    fx.registerDefaults();
    GatedInteraction interaction;
    const core::BranchId mainBranch = fx.primaryBranch();

    // 线程 A：提交待确认命令——在回调内挂起（用户停留在确认对话框）。
    auto firstDone = std::async(std::launch::async, [&fx, &interaction, mainBranch] {
        return fx.commands().submit(confirmEnvelope(mainBranch, "slot-a"),
                                    &interaction);
    });
    for (int i = 0; i < 100 && !interaction.entered.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_TRUE(interaction.entered.load());

    // 线程 B：A 持槽期间不得完成（§6.1——第二个等待）。
    auto secondDone = std::async(std::launch::async, [&fx, mainBranch] {
        return fx.commands().submit(fx.appendEnvelope("slot-b", mainBranch));
    });
    EXPECT_EQ(secondDone.wait_for(std::chrono::milliseconds(300)),
              std::future_status::timeout);

    // 放行（恢复）——A 确认提交、B 随后串行完成。
    interaction.release();
    const CommandResult firstResult = firstDone.get();
    const CommandResult secondResult = secondDone.get();
    ASSERT_TRUE(firstResult.committed());
    ASSERT_TRUE(secondResult.committed());
    const RevisionView a
        = fx.opened.store->query().revision(*firstResult.newRevision);
    const RevisionView b
        = fx.opened.store->query().revision(*secondResult.newRevision);
    EXPECT_EQ(b.seq, a.seq + 1);  // 串行序（槽内先 A 后 B）
}

/// 确认等待零事务资源（§5.3.4"不占用任何事务资源"的直接观测）：回调
/// 挂起期间 .staging 无事务目录、revisions 无增长（尚未创建 .staging/
/// <tx-id>、未写任何文件）。
TEST(CommandConfirmLifecycle, ConfirmWait_ZeroTransactionResources)
{
    ServiceFixture fx;
    fx.registerDefaults();
    GatedInteraction interaction;
    const std::size_t revisionsBefore = fx.revisionDirCount();

    auto pending = std::async(std::launch::async, [&fx, &interaction] {
        return fx.commands().submit(
            confirmEnvelope(fx.primaryBranch(), "zero-tx"), &interaction);
    });
    for (int i = 0; i < 100 && !interaction.entered.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_TRUE(interaction.entered.load());

    // 挂起时点的地面事实：无 .staging/<tx-id>（tmp 为 D-09 编译临时目录
    // ——非事务资源，排除）＋修订目录数不变。
    EXPECT_TRUE(stagingTxDirs(fx.dir).empty());
    EXPECT_EQ(fx.revisionDirCount(), revisionsBefore);

    interaction.release();
    EXPECT_TRUE(pending.get().committed());
}

/// 确认等待可被会话关闭取消（§5.3.4/D-07——无永久等待）：关闭请求使
/// 上下文进入 Draining（在途>0 时立即返回在途计数——排空由票据释放路径
/// 收尾，§9.7）；回调按 ui 拆除形态结束（抛出——§5.6"回调抛出→同
/// interaction-lost 路径"）→ 提交 Aborted、无修订；票据随 submit 返回
/// 释放，排空收尾随即完成（无死锁、无永久等待）。
TEST(CommandConfirmLifecycle, SessionClose_CancelsConfirmWait_NoPermanentWait)
{
    ServiceFixture fx;
    fx.registerDefaults();
    GatedInteraction interaction;
    interaction.throwOnRelease = true;  // ui 拆除形态（会话关闭传导）
    const std::size_t revisionsBefore = fx.revisionDirCount();

    auto pending = std::async(std::launch::async, [&fx, &interaction] {
        return fx.commands().submit(
            confirmEnvelope(fx.primaryBranch(), "close-cancel"), &interaction);
    });
    for (int i = 0; i < 100 && !interaction.entered.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_TRUE(interaction.entered.load());

    // 关闭请求：立即进入 Draining 并返回在途计数（非阻塞——排空收尾由
    // 票据释放触发，§9.7"排空等待"的机制化形态）。
    auto closer = std::async(std::launch::async,
                             [&fx] { return fx.opened.store->requestClose(); });
    ASSERT_EQ(closer.wait_for(std::chrono::seconds(5)),
              std::future_status::ready);      // 请求本身不等待（无阻塞）
    EXPECT_EQ(closer.get(), 1u);               // 在途计数＝挂起中的提交
    EXPECT_FALSE(fx.opened.store->closed());   // 排空未完成（票据在途）

    // 回调终结（ui 拆除）→ 提交中止、无修订；票据随 submit 返回释放，
    // Draining 排空收尾被触发——关闭事实随之成立（无永久等待）。
    interaction.release();
    const CommandResult result = pending.get();
    EXPECT_TRUE(result.aborted());
    EXPECT_EQ(result.status.abort, CommandStatus::Abort::InteractionLost);
    EXPECT_FALSE(result.newRevision.has_value());
    EXPECT_EQ(fx.revisionDirCount(), revisionsBefore);
    // 排空收尾在票据释放路径完成（提交线程）——有界轮询等待（不预设
    // 调度时序；5 s 上限防用例悬挂）。
    for (int i = 0; i < 500 && !fx.opened.store->closed(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_TRUE(fx.opened.store->closed());
}

/// 用户取消对话框（挂起后整体拒绝——§5.3.3"空 optional＝整体拒绝"在
/// 挂起/恢复面上的形态）：ReRejected(confirmations-rejected)、无修订，
/// 会话不受影响（可继续提交——取消不破坏上下文）。
TEST(CommandConfirmLifecycle, SuspendThenUserCancel_RejectedContextUnharmed)
{
    ServiceFixture fx;
    fx.registerDefaults();
    GatedInteraction interaction;
    interaction.confirmOnRelease = false;  // 用户点取消
    const core::BranchId mainBranch = fx.primaryBranch();

    auto pending = std::async(std::launch::async, [&fx, &interaction, mainBranch] {
        return fx.commands().submit(confirmEnvelope(mainBranch, "cancel"),
                                    &interaction);
    });
    for (int i = 0; i < 100 && !interaction.entered.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    interaction.release();
    const CommandResult result = pending.get();
    EXPECT_TRUE(result.rejected());
    EXPECT_EQ(result.status.rejection,
              CommandStatus::Rejection::ConfirmationsRejected);

    // 上下文可继续：同一会话再次提交照常落盘（取消只终结该次命令）。
    const CommandResult next
        = fx.commands().submit(fx.appendEnvelope("after-cancel", mainBranch));
    EXPECT_TRUE(next.committed());
}

/// 回调同步执行于提交线程（§5.3.3"命令服务在命令执行线程同步调用"——
/// ui Marshal 契约的 project 侧观测；P-PR-7 冻结签名的行为面自证）。
TEST(CommandConfirmLifecycle, Callback_SynchronousOnSubmitterThread)
{
    ServiceFixture fx;
    fx.registerDefaults();
    GatedInteraction interaction;

    std::thread::id submitterThreadId{};
    auto pending = std::async(std::launch::async, [&] {
        submitterThreadId = std::this_thread::get_id();
        return fx.commands().submit(
            confirmEnvelope(fx.primaryBranch(), "thread-check"), &interaction);
    });
    for (int i = 0; i < 100 && !interaction.entered.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_TRUE(interaction.entered.load());
    EXPECT_EQ(interaction.callbackThreadId, submitterThreadId);  // 同线程同步

    interaction.release();
    EXPECT_TRUE(pending.get().committed());
}

// ---------------------------------------------------------------------
// acceptance 3——确认留痕入 command.json（§4.4.4/§6.7；core.md §13.2
// 交接项：确认留痕编码由 project 交接冻结）＋UX-03 承接（确认对话数据
// 齐备由回调纯接口提供、文案不在 project 生成）。
// ---------------------------------------------------------------------

/// 确认通过的提交把绑定四元组＋凭据写入 command.json，磁盘回读逐字段
/// 与计算值一致（四元组重算同源比对——绑定事实可复核）＋canonical
/// round-trip（dump(parse(x))==x）。
TEST(CommandConfirmPersistence, Confirmations_PersistedToCommandJson_RoundTrip)
{
    ServiceFixture fx;
    fx.registerDefaults();
    StubInteraction interaction;  // 确认（principal=stub-user）
    const core::BranchId mainBranch = fx.primaryBranch();
    const RevisionView headBefore = fx.opened.store->query().head();

    const CommandEnvelope envelope = confirmEnvelope(mainBranch, "bind-rt");
    const CommandResult result = fx.commands().submit(envelope, &interaction);
    ASSERT_TRUE(result.committed());
    ASSERT_TRUE(result.newRevision.has_value());

    // 磁盘地面事实：修订目录内 command.json 的 confirmations[]。
    const CommandRecord persisted
        = readCommandRecord(fx.dir, *result.newRevision);
    ASSERT_EQ(persisted.confirmations.size(), 1u);
    const ConfirmationRecord& c = persisted.confirmations[0];
    ASSERT_GE(interaction.lastFindings.size(), 1u);
    const core::ConfirmableFinding& confirmed = interaction.lastFindings[0];
    // 四元组逐字段同源比对（重算值与落盘值一致——绑定可复核）。
    EXPECT_EQ(c.findingDigest, confirm::findingDigestHex(confirmed.record));
    EXPECT_EQ(c.policyContentId,
              confirm::policyContentIdentity(confirmed.record).toCanonical());
    EXPECT_EQ(c.commandDigest, confirm::commandDigestHex(envelope.payloadCanonical));
    EXPECT_EQ(c.baseRevisionId, headBefore.id.toCanonical());  // 确认基线＝提交前 tip
    // 凭据（§4.4.4 credential）：主体透传＋ISO-8601 带毫秒 UTC 文本。
    EXPECT_EQ(c.credential.principal, "stub-user");
    ASSERT_EQ(c.credential.confirmedAtUtc.size(), 24u);
    EXPECT_EQ(c.credential.confirmedAtUtc.back(), 'Z');
    EXPECT_EQ(c.credential.confirmedAtUtc[4], '-');
    EXPECT_EQ(c.credential.confirmedAtUtc[10], 'T');
    // 策略身份形态：cid- core 规范文本（CON-06——Codec 校验同级）。
    EXPECT_TRUE(core::ContentIdentity::tryFromCanonical(c.policyContentId)
                    .has_value());
    // canonical round-trip：重编码逐字节复现磁盘文本（NFR-COR-02）。
    const std::filesystem::path path = fx.dir / "revisions"
        / result.newRevision->toCanonical() / "command.json";
    std::ifstream in(path, std::ios::binary);
    ASSERT_TRUE(static_cast<bool>(in));
    const std::string diskText{std::istreambuf_iterator<char>(in),
                               std::istreambuf_iterator<char>()};
    EXPECT_EQ(codec::dump(persisted), diskText);
}

/// 无待确认集的提交不产出 confirmations 字段（§4.4.4 canonical 省略
/// 规则：空列表＝缺省字段——留痕面不写空结构）。
TEST(CommandConfirmPersistence, NoFindings_ConfirmationsFieldOmitted)
{
    ServiceFixture fx;
    fx.registerDefaults();

    const CommandEnvelope envelope
        = fx.appendEnvelope("no-confirmations", fx.primaryBranch());
    const CommandResult result = fx.commands().submit(envelope);
    ASSERT_TRUE(result.committed());

    const std::filesystem::path path = fx.dir / "revisions"
        / result.newRevision->toCanonical() / "command.json";
    std::ifstream in(path, std::ios::binary);
    ASSERT_TRUE(static_cast<bool>(in));
    const std::string diskText{std::istreambuf_iterator<char>(in),
                               std::istreambuf_iterator<char>()};
    EXPECT_EQ(diskText.find("\"confirmations\""), std::string::npos);
    const CommandRecord parsed = codec::parseCommandRecord(diskText);
    EXPECT_TRUE(parsed.confirmations.empty());
}

/// UX-03 承接：确认对话所需数据齐备由回调纯接口提供（比较三要素——
/// 实际/期望/单位随 finding 原样到达回调）；文案不在 project 生成
/// （NFR-REL-05 口径——project 在确认流零用户级诊断产出，文案权威归
/// diagnostics/ui）。
TEST(CommandConfirmPersistence, Callback_ReceivesComparativeData_Ux03)
{
    ServiceFixture fx;
    fx.registerDefaults();
    StubInteraction interaction;

    const CommandResult result = fx.commands().submit(
        confirmEnvelope(fx.primaryBranch(), "ux03"), &interaction);
    ASSERT_TRUE(result.committed());

    // 回调数据面：比较三要素齐备（实际/期望侧四态值＋单位——确认对话
    // 的呈现输入；core C-1 保证比较型出席）。
    ASSERT_GE(interaction.lastFindings.size(), 1u);
    const core::DiagnosticRecord& record = interaction.lastFindings[0].record;
    EXPECT_EQ(record.code, "TEST-COMPARE-LIMIT");
    ASSERT_TRUE(record.comparison.has_value());
    ASSERT_TRUE(record.comparison->actual.quantity.tryValue().has_value());
    ASSERT_TRUE(record.comparison->expected.quantity.tryValue().has_value());
    EXPECT_DOUBLE_EQ(*record.comparison->actual.quantity.tryValue(), 2.0);
    EXPECT_EQ(record.comparison->actual.unit.symbol(), "m");
    EXPECT_DOUBLE_EQ(*record.comparison->expected.quantity.tryValue(), 1.0);
    EXPECT_EQ(record.comparison->expected.unit.symbol(), "m");
    // 文案边界：project 在确认放行全流程零用户级诊断产出（finding 的
    // context/cause 文案由域处理器产出、project 原样透传——呈现归 ui，
    // 文案权威归 diagnostics/ui——P-PR-6 链路）。
    EXPECT_TRUE(fx.sink.reports.empty());
    EXPECT_TRUE(result.diagnostics.empty());
}

}  // namespace sdurws::ird::project::stub
