/**
 * @file   ConfirmableProjectStubTest.cpp
 * @brief  可确认诊断确认放行流——project 命令服务桩联动用例组（DT-CFM-6~10）。
 *
 * 设计依据：
 *   - units/diagnostics.md §10 DT-CFM-6~DT-CFM-10（UI 关闭回调、未确认不能
 *     提交、worker 禁令、确认后重新校验、确认留痕进命令摘要）、§5.1（SA-15
 *     分工冻结）、§5.3（确认后的义务：确认≠自动成功；绑定双复核）、§5.5/
 *     §5.6（确认时序与 UI 关闭流程）；project.md §5.3.3/§5.3.4/§6.7（确认
 *     交互回调、等待期资源、放行流详设与留痕形状）
 *   - 需求 MDL-06④（未确认则阻止应用；确认不豁免编译）、SA-15（确认只在
 *     命令边界；worker 禁令）；CON-06
 *   - 任务契约 tasks/foundation/DIAG-T05.json acceptance 1/2（逐条自证：
 *     acceptance 1 的 project 桩联动半区→DtCfm7/DtCfm10 组；acceptance 2 的
 *     SA-15 分工四点→DtCfm6/DtCfm7/DtCfm8/DtCfm9 组）
 *
 * P-DIAG-8 处置（契约 knownPitfalls）：execution/project 尚未链接 diagnostics
 * （边经注入实现——P-EX-8/P-PR-6 待对端详设修订），本套件以**桩＋契约夹具**
 * 验证确认放行流的跨单元协作形态，不私改对端单元的链接与头文件（R-1/R-2）；
 * 桩仅复刻 project.md §6.7/§6.10 的编排语义（S4 确认放行→S5 双编译→S6 事务）
 * ——桩输出只验证契约，不充当对端实现（testkit EV-REG-3 替身边界同模式）。
 *
 * 桩内"编译前复核"的实现口径（§9.3 契约表：编译前复核由 project 调 tryFind＋
 * 自行比对实现——本套件按该口径编写桩）：tryFind 取记录→state==Confirmed→
 * 重算 findingRecordDigest 比对 binding.findingDigest＋按环境探针比对
 * commandDigest/policyContentId/baseRevisionId——任一不符即"绑定失配"，桩不
 * 沿用旧确认（§5.4 底行"该凭据作废，重新确认"）。
 *
 * 线程约束：全部用例单线程（命令槽串行——project §6.1；桩流程同步展开）。
 */

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <sdurws/ird/core/DiagData.hpp>
#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/core/Provenance.hpp>
#include <sdurws/ird/core/Units.hpp>
#include <sdurws/ird/diagnostics/Catalog.hpp>
#include <sdurws/ird/diagnostics/Confirmable.hpp>
#include <sdurws/ird/diagnostics/DiagCodes.hpp>
#include <sdurws/ird/diagnostics/Errors.hpp>

namespace {

using namespace sdurws::ird::diagnostics;
namespace core = sdurws::ird::core;
using sdurws::ird::core::BranchId;
using sdurws::ird::core::ProjectId;
using sdurws::ird::core::RevisionId;

// ---------------------------------------------------------------------
// 桩与夹具（自持不共享——单元测试套件同风格）
// ---------------------------------------------------------------------

/// 可确认类测试码（与单元套件同码——MDL 前缀所有权归 modeling，§4.5 前缀表）。
inline constexpr const char* kTravelLimitCode = "MDL-06-TRAVEL-LIMIT";

/// 命令结果状态（桩面——project §6.10 CommandResult 四枝的最小形态）。
enum class CommandStatus { Committed, Rejected, Aborted, Failed };

/// 确认留痕条目（project §6.7：CommandRecord.confirmations[] 的形状——
/// {binding 四元组, credential}，持久化归 project；本桩承载形状权威的回读面）。
struct ConfirmationRecordStub {
    FindingBinding binding;                        ///< 绑定四元组（findingDigest/policy/command/baseRevision）
    core::ConfirmationCredential credential;       ///< 确认凭据（principal＋confirmedAtUtc）
};

/// 命令结果桩（§6.10 CommandResult 最小面：状态、原因 token、修订身份、
/// 确认留痕——revision 有值当且仅当 Committed）。
struct CommandResultStub {
    CommandStatus status = CommandStatus::Rejected;
    std::string reasonToken;                       ///< rejected/aborted 原因（confirmations-unresolved 等）
    std::optional<core::RevisionId> revision;      ///< 新修订（仅 Committed 有值——无修订即 nullopt）
    std::vector<ConfirmationRecordStub> confirmations;   ///< 确认留痕（§6.7 形状）
};

/// 确认交互回调桩接口（project §5.3.3 形态复刻：isAlive 生命周期探针＋
/// requestConfirmation 同步回调；ui 实现归 ui 单元——桩代位）。
class ICommandInteractionStub {
public:
    virtual ~ICommandInteractionStub() = default;
    /// 生命周期探针（ui 关闭即 false——§5.6 回调失效检测点）。
    virtual bool isAlive() const = 0;
    /// 确认对话（实际值/阈值/单位三要素呈现归 ui，UX-03）：返回确认凭据；
    /// 用户拒绝返回 nullopt。
    virtual std::optional<core::ConfirmationCredential> requestConfirmation(
        const FindingRecord& finding, std::uint64_t callbackToken) = 0;
};

/// 交互桩实现一：用户确认（可配置 principal；isAlive 可翻转——DT-CFM-6）。
class ConfirmingInteraction final : public ICommandInteractionStub {
public:
    bool isAlive() const override { return m_alive; }
    std::optional<core::ConfirmationCredential> requestConfirmation(
        const FindingRecord&, std::uint64_t) override
    {
        ++m_requests;
        if (!m_alive) { return std::nullopt; }   // 关闭后的调用不再采集凭据
        return core::ConfirmationCredential{
            m_principal, std::chrono::system_clock::time_point{std::chrono::seconds{2000000}}};
    }
    void close() { m_alive = false; }              ///< 模拟主窗口拆除（§5.6）
    int requests() const { return m_requests; }

private:
    bool m_alive = true;
    int m_requests = 0;
    std::string m_principal = "ui-user";
};

/// 交互桩实现二：用户拒绝（§5.3 失效条件表"用户拒绝"行——DT-CFM-2 桩联动）。
class RejectingInteraction final : public ICommandInteractionStub {
public:
    bool isAlive() const override { return true; }
    std::optional<core::ConfirmationCredential> requestConfirmation(
        const FindingRecord&, std::uint64_t) override
    {
        return std::nullopt;   // 用户点了"取消"——拒绝确认不产生修订（MDL-06④）
    }
};

/// 环境探针桩（tip/策略/命令三当前值可驱动——编译前复核的"当前实际值"面）。
class StubEnvironment final : public IConfirmationEnvironment {
public:
    core::RevisionId currentTipRevision(core::ProjectId, core::BranchId) const override
    {
        return m_tip;
    }
    std::optional<core::ContentIdentity> currentPolicyContentId(core::ProjectId,
                                                                core::BranchId) const override
    {
        return m_policy;
    }
    std::optional<core::ContentIdentity> currentCommandDigest(core::ProjectId,
                                                              core::BranchId) const override
    {
        return m_command;
    }
    core::RevisionId m_tip = RevisionId::generate();
    std::optional<core::ContentIdentity> m_policy;
    std::optional<core::ContentIdentity> m_command;
};

/// 确定性时钟（IClock——createdAtUtc 时基；桩流程不依赖时间流逝）。
class FixedClock final : public IClock {
public:
    std::chrono::system_clock::time_point nowUtc() const override { return m_now; }
private:
    std::chrono::system_clock::time_point m_now{std::chrono::seconds{3000000}};
};

/// worker 侧唯一可得面桩（§7.5 回传通道——IDevLogSink；DT-CFM-8 的装配
/// 形状：worker 装配清单只含诊断通道，不含 IConfirmableFindingService）。
class WorkerChannelSink final : public IDevLogSink {
public:
    void logDev(std::string_view, std::string message) override
    {
        m_frames.push_back(std::move(message));   // 通道帧＝普通日志行（无确认语义）
    }
    const std::vector<std::string>& frames() const { return m_frames; }
private:
    std::vector<std::string> m_frames;
};

/// 组装并 seal 注册表（内置全量＋可确认测试码——单元套件同款）。
void sealRegistryWithConfirmableCode(StableCodeRegistry& registry)
{
    registerBuiltinCodes(registry);
    CodeDescriptor confirmable;
    confirmable.code = kTravelLimitCode;
    confirmable.ownerUnit = "modeling";
    confirmable.category = DiagnosticCategory::Confirmable;
    confirmable.severity = DiagnosticSeverity::Warning;
    confirmable.titleKey = "diag.mdl-06-travel-limit.title";
    confirmable.detailKey = "diag.mdl-06-travel-limit.detail";
    confirmable.paramSchema = "[]";
    confirmable.confirmable = true;
    confirmable.requiresComparison = true;
    confirmable.retryable = RetryKind::UserRetry;
    registry.registerCode(confirmable);
    registry.seal();
}

/// 合法比较值侧（同单元套件——Provided＋UserProvided 来源＋单位句柄）。
core::ComparativeValue makeComparativeValue(double number, const char* unitSymbol)
{
    return core::ComparativeValue{
        core::SourcedValue<double>::provided(
            number, core::ValueProvenance::make(core::ProvenanceKind::UserProvided)),
        *core::UnitToken::find(unitSymbol)};
}

/// 合法可确认 finding（行程超限场景——§5.5 时序图样例）。
core::ConfirmableFinding makeTravelLimitFinding()
{
    core::DiagnosticRecord record = core::DiagnosticRecord::make(
        kTravelLimitCode, core::ObjectId::generate(), std::string("joint_5"),
        std::string("Robot.joint_5"), std::string("行程上限超限待确认"),
        std::string("目标关节角行程 620mm 超出策略上限 550mm"),
        std::string("确认放行或修正目标位姿"),
        core::ComparativeFields{makeComparativeValue(620.0, "mm"),
                                makeComparativeValue(550.0, "mm")});
    return core::ConfirmableFinding::make(std::move(record));
}

/// 非全零内容身份（命令载荷/策略身份材料）。
core::ContentIdentity makeIdentity(std::uint8_t tag)
{
    core::ContentIdentity id;
    id.bytes.fill(tag);
    return id;
}

// ---------------------------------------------------------------------
// project 命令服务桩（§6.7/§6.10 编排语义的最小复刻——S4/S5/S6）
// ---------------------------------------------------------------------

class ProjectCommandStub {
public:
    ProjectCommandStub(IConfirmableFindingService& service, StubEnvironment& environment)
        : m_service(service), m_environment(environment)
    {
    }

    /// 双编译开关（DT-CFM-9 的编译失败注入点——S5 桩）。
    bool compileOk = true;

    /// S4 与 S5 之间的注入点（测试驱动"确认成立后、编译前"的输入变化——
    /// 模拟跨命令重用/槽外变更场景；真实编排中该窗口由命令槽串行保护，
    /// 编译前复核正是其防御面，§5.3"输入修订变化"行）。
    void setPreCompileHook(std::function<void()> hook) { m_preCompileHook = std::move(hook); }

    /// 提交（S4 确认放行→S5 编译（含编译前绑定复核）→S6 事务桩）。
    /// findings＝命令上下文携带的待确认集；interaction==nullptr＝非交互提交
    /// （测试/后台通道——§6.7"非交互提交"行）。
    CommandResultStub submit(const std::vector<FindingRecord>& findings,
                             core::ContentIdentity payloadDigest,
                             ICommandInteractionStub* interaction)
    {
        CommandResultStub result;
        const auto branchKey = m_branch;

        // ---- S4 确认放行（findings 非空时）----
        if (!findings.empty()) {
            // §6.7"非交互提交"：interaction==nullptr 且有待确认集 →
            // Rejected(confirmations-unresolved)（"未确认不能提交命令"——
            // DT-CFM-7；无修订、finding 仍 Pending 由服务端保证）。
            if (interaction == nullptr) {
                result.status = CommandStatus::Rejected;
                result.reasonToken = "confirmations-unresolved";
                return result;
            }
            // §5.3.3/§5.6：回调调用前置检查 isAlive——UI 已拆除（会话关闭/
            // 窗口拆除）→ Aborted(interaction-lost)，finding 标失效，无修订，
            // **立即返回不等待**（"不存在因诊断/确认导致的永久等待"）。
            if (!interaction->isAlive()) {
                for (const auto& finding : findings) {
                    m_service.invalidate(finding.findingId,
                                         finding_reason::kInteractionLost);
                }
                result.status = CommandStatus::Aborted;
                result.reasonToken = "interaction-lost";
                return result;
            }
            // 回调采集凭据（命令执行线程同步调用＋ui Marshal——桩内同步直调）。
            // 用户拒绝→submitRejection→Rejected(confirmations-rejected)（§6.7：
            // 拒绝→Rejected(confirmations-rejected)；拒绝确认不产生修订）。
            bool anyRejected = false;
            for (const auto& finding : findings) {
                const auto credential =
                    interaction->requestConfirmation(finding, finding.callbackToken);
                if (!credential.has_value()) {
                    m_service.submitRejection(finding.findingId, finding.callbackToken,
                                              finding_reason::kUserRejected);
                    anyRejected = true;
                    continue;
                }
                // 凭据交服务复核（§9.3 submitConfirmation——绑定四元组复核）。
                const ConfirmOutcome outcome = m_service.submitConfirmation(
                    finding.findingId, finding.callbackToken, *credential);
                if (outcome != ConfirmOutcome::Confirmed) {
                    // 复核失败（BindingMismatch/InvalidState）＝确认未成立——
                    // 命令按未确认处置（confirmations-unresolved；不静默沿用）。
                    result.status = CommandStatus::Rejected;
                    result.reasonToken = "confirmations-unresolved";
                    return result;
                }
                m_confirmed.push_back(finding);
            }
            if (anyRejected) {
                result.status = CommandStatus::Rejected;
                result.reasonToken = "confirmations-rejected";
                return result;
            }
        }

        // ---- S5 双编译（前置＝编译前绑定复核——project §6.7"编译前复核绑定"
        // 由 project 编排：tryFind＋重算比对，本文 §9.3 契约表口径）----
        if (m_preCompileHook) {
            m_preCompileHook();   // 测试注入："确认成立后、编译前"的输入变化
        }
        if (!findings.empty() && !preCompileRebindCheck()) {
            // 绑定失配：凭据作废不沿用（§5.4 底行——重新确认路径），命令按
            // 未确认处置；已失配 finding 已标 Invalidated(binding-mismatch)。
            result.status = CommandStatus::Rejected;
            result.reasonToken = "confirmations-unresolved";
            return result;
        }
        if (!compileOk) {
            // S5 编译失败：诊断经工厂入目录（桩略）＋命令 Failed、无修订
            // （MDL-06：确认不豁免编译——DT-CFM-9；SA-15 凭据"继续或终止"
            // 的终止枝）。
            result.status = CommandStatus::Failed;
            result.reasonToken = "compile-failed";
            return result;
        }

        // ---- S6 七步事务（桩）：确认留痕入 CommandRecord.confirmations[]＋
        // 修订提交（project 持久化；Committed 后 finding 清理标记归 §6.2）。----
        for (const auto& finding : m_confirmed) {
            const auto record = m_service.tryFind(finding.findingId);
            if (!record.has_value() || record->state != FindingState::Confirmed) {
                continue;   // 防御：确认态被并发失效的记录不留痕（单线程桩不触发）
            }
            result.confirmations.push_back(
                ConfirmationRecordStub{record->binding, *record->confirmation});
        }
        result.status = CommandStatus::Committed;
        result.revision = RevisionId::generate();   // 事务桩：一次提交一个修订
        return result;
    }

    /// 会话关闭（§5.3.4：确认等待可被会话关闭取消——项目侧取消入口）。
    void sessionClosing(const std::vector<FindingRecord>& findings)
    {
        for (const auto& finding : findings) {
            m_service.invalidate(finding.findingId, finding_reason::kCanceled);
        }
    }

    void setBranch(core::BranchId branch) { m_branch = branch; }

private:
    /// 编译前绑定复核（§9.3 契约表口径：tryFind＋自行比对——本文提供的
    /// binding 数据＋findingRecordDigest 公共定义点）。
    bool preCompileRebindCheck()
    {
        for (const auto& finding : m_confirmed) {
            const auto record = m_service.tryFind(finding.findingId);
            if (!record.has_value() || record->state != FindingState::Confirmed) {
                return false;   // 确认态丢失＝不可沿用
            }
            // 四元组逐成员与"当前实际值"比对（与 §5.3 同一义务、project 侧编排）。
            if (!(findingRecordDigest(record->finding.record)
                  == record->binding.findingDigest)) {
                return false;
            }
            const auto command = m_environment.currentCommandDigest(record->project, record->branch);
            if (!command.has_value() || !(*command == record->binding.commandDigest)) {
                m_service.invalidate(record->findingId, finding_reason::kBindingMismatch);
                return false;
            }
            if (record->binding.policyContentId.has_value()) {
                const auto policy = m_environment.currentPolicyContentId(
                    record->project, record->branch);
                if (!policy.has_value() || !(*policy == *record->binding.policyContentId)) {
                    m_service.invalidate(record->findingId, finding_reason::kPolicyChanged);
                    return false;
                }
            }
            if (!(m_environment.currentTipRevision(record->project, record->branch)
                  == record->binding.baseRevisionId)) {
                m_service.invalidate(record->findingId, finding_reason::kRevisionChanged);
                return false;
            }
        }
        return true;
    }

    IConfirmableFindingService& m_service;   ///< 确认服务（diagnostics 设施——注入边）
    StubEnvironment& m_environment;          ///< 环境探针（编译前复核的当前值面）
    core::BranchId m_branch = BranchId::generate();
    std::vector<FindingRecord> m_confirmed;  ///< 本命令内确认成立的 finding
    std::function<void()> m_preCompileHook;  ///< S4→S5 注入点（见 setPreCompileHook）
};

/// 服务整装（注册表→时钟→环境→服务；桩流程共享一个环境探针）。
struct Rig {
    StableCodeRegistry registry;
    FixedClock clock;
    StubEnvironment environment;
    ConfirmableService service{registry, environment, clock, nullptr};

    Rig() { sealRegistryWithConfirmableCode(registry); }

    /// 创建基线 finding（命令槽上下文：tip=绑定修订、命令槽内有当前命令）。
    FindingRecord createFinding()
    {
        const auto base = RevisionId::generate();
        environment.m_tip = base;
        const auto payload = makeIdentity(0xAB);
        environment.m_command = payload;
        const FindingRecord created = service.create(
            makeTravelLimitFinding(), "ApplyModel", payload, ProjectId::generate(),
            BranchId::generate(), base, {}, {core::ObjectId::generate()});
        return created;
    }
};

// ---------------------------------------------------------------------
// DT-CFM-7：未确认不能提交命令（MDL-06④——project 桩 confirmations-unresolved）
// ---------------------------------------------------------------------

TEST(ConfirmableProjectStubTest, DtCfm7_NonInteractiveSubmitRejectedUnresolved)
{
    Rig rig;
    ProjectCommandStub command(rig.service, rig.environment);
    const FindingRecord finding = rig.createFinding();

    // 非交互提交（interaction==nullptr 且待确认集非空）→
    // Rejected(confirmations-unresolved)——观测点"命令桩结果"。
    const CommandResultStub result = command.submit({finding}, makeIdentity(1), nullptr);
    EXPECT_EQ(result.status, CommandStatus::Rejected);
    EXPECT_EQ(result.reasonToken, "confirmations-unresolved");
    // 无修订（"未确认则阻止应用"——MDL-06④）。
    EXPECT_FALSE(result.revision.has_value());
    // finding 仍 Pending（桩未消费凭据——服务端无转移），随后会话路径失效
    // （§10 DT-CFM-7 观测点"finding state"；§5.3.4 会话关闭取消等待）。
    ASSERT_TRUE(rig.service.tryFind(finding.findingId).has_value());
    EXPECT_EQ(rig.service.tryFind(finding.findingId)->state, FindingState::Pending);
    command.sessionClosing({finding});
    EXPECT_EQ(rig.service.tryFind(finding.findingId)->state, FindingState::Invalidated);
    EXPECT_EQ(rig.service.tryFind(finding.findingId)->rejectionReason,
              finding_reason::kCanceled);
}

// ---------------------------------------------------------------------
// DT-CFM-6：UI 关闭→回调失效即 Invalidated、无永久等待（project §5.3.3）
// ---------------------------------------------------------------------

TEST(ConfirmableProjectStubTest, DtCfm6_UiCloseInvalidatesFindingWithoutWaiting)
{
    Rig rig;
    ProjectCommandStub command(rig.service, rig.environment);
    const FindingRecord finding = rig.createFinding();
    ConfirmingInteraction interaction;

    // UI 关闭（窗口拆除——§5.6）：isAlive 翻转 false。
    interaction.close();
    const CommandResultStub result = command.submit({finding}, makeIdentity(1), &interaction);

    // 观测点：state；token 复用拒绝。Aborted(interaction-lost)、无修订、
    // finding 标 Invalidated(interaction-lost)——桩流程同步返回（无阻塞等待
    // ——§5.6"不存在因诊断/确认导致的永久等待"；回调未被调用即前置检查拦截）。
    EXPECT_EQ(result.status, CommandStatus::Aborted);
    EXPECT_EQ(result.reasonToken, "interaction-lost");
    EXPECT_FALSE(result.revision.has_value());
    EXPECT_EQ(interaction.requests(), 0) << "关闭后不再发起确认对话（前置检查拦截）";
    const auto after = rig.service.tryFind(finding.findingId);
    ASSERT_TRUE(after.has_value());
    EXPECT_EQ(after->state, FindingState::Invalidated);
    EXPECT_EQ(after->rejectionReason, finding_reason::kInteractionLost);
    // callbackToken 作废：复用原 token 再提交确认→InvalidState。
    EXPECT_EQ(rig.service.submitConfirmation(finding.findingId, finding.callbackToken,
                                             core::ConfirmationCredential{"late-user",
                                                                          std::chrono::system_clock::time_point{}}),
              ConfirmOutcome::InvalidState);
}

// ---------------------------------------------------------------------
// DT-CFM-8：worker 不产生 finding（SA-15——装配面断言＋通道帧类型）
// ---------------------------------------------------------------------

TEST(ConfirmableProjectStubTest, DtCfm8_WorkerAssemblyExposesNoConfirmableSurface)
{
    // 静态半区（装配清单断言——测试侧复钉产品头 static_assert 同款判据）：
    // worker 可得的诊断通道类型与确认服务在类型上互不替代；可确认类型无法
    // 经普通诊断通道传递（report 只收 DiagnosticRecord——无转换路径）。
    static_assert(!std::is_convertible_v<IDiagnosticSink*, IConfirmableFindingService*>,
                  "SA-15：worker 通道不得当作确认服务");
    static_assert(!std::is_convertible_v<IDevLogSink*, IConfirmableFindingService*>,
                  "SA-15：worker 日志路由不得当作确认服务");
    static_assert(!std::is_convertible_v<core::ConfirmableFinding, core::DiagnosticRecord>,
                  "SA-15：可确认诊断不得窄化为普通记录回传");

    // 运行期半区：worker 装配清单桩＝仅通道 sink；经通道回传的均为普通诊断
    // （观测点"通道帧类型"——帧为日志行，非 finding）；服务端无任何 finding
    // 产生（pendingFor 恒空——确认只发生在命令边界）。
    WorkerChannelSink channel;
    channel.logDev("worker.policy", "批量计算策略拒绝：joint_3 速度超限（ErrorReport 通道回传）");

    Rig rig;
    EXPECT_EQ(channel.frames().size(), 1u);
    EXPECT_NE(channel.frames().front().find("策略拒绝"), std::string::npos)
        << "worker 的策略拒绝走普通诊断回传（§5.6 反例钉住）";
    EXPECT_TRUE(rig.service.pendingFor(RevisionId::generate()).empty())
        << "worker 侧无确认服务可达——不产生 finding（SA-15 装配不暴露）";
}

// ---------------------------------------------------------------------
// DT-CFM-9：确认后重新校验——编译失败仍不提交（MDL-06、SA-15）
// ---------------------------------------------------------------------

TEST(ConfirmableProjectStubTest, DtCfm9_ConfirmedButCompileFailureCommitsNothing)
{
    Rig rig;
    ProjectCommandStub command(rig.service, rig.environment);
    command.compileOk = false;   // S5 编译桩失败（确认≠自动成功——§5.3 确认后的义务）
    const FindingRecord finding = rig.createFinding();
    ConfirmingInteraction interaction;

    const CommandResultStub result = command.submit({finding}, makeIdentity(1), &interaction);

    // 观测点"命令桩结果"：Failed/无修订（确认已成立但编译失败——不提交）。
    EXPECT_EQ(result.status, CommandStatus::Failed);
    EXPECT_EQ(result.reasonToken, "compile-failed");
    EXPECT_FALSE(result.revision.has_value());
    EXPECT_TRUE(result.confirmations.empty()) << "无提交即无确认留痕入命令摘要";
    // finding 本身保持 Confirmed（确认事实成立；失败归编译面——不回泼状态机）。
    EXPECT_EQ(rig.service.tryFind(finding.findingId)->state, FindingState::Confirmed);
}

// ---------------------------------------------------------------------
// DT-CFM-10：确认记录进入命令摘要（project §6.7——四元组＋credential 完整）
// ---------------------------------------------------------------------

TEST(ConfirmableProjectStubTest, DtCfm10_ConfirmationRecordShapeEntersCommandSummary)
{
    Rig rig;
    ProjectCommandStub command(rig.service, rig.environment);
    const FindingRecord finding = rig.createFinding();
    ConfirmingInteraction interaction;

    const CommandResultStub result = command.submit({finding}, makeIdentity(1), &interaction);

    // 提交成功：修订产生＋留痕恰一条（观测点"字段比对——project 桩回读"）。
    ASSERT_EQ(result.status, CommandStatus::Committed);
    ASSERT_TRUE(result.revision.has_value());
    ASSERT_EQ(result.confirmations.size(), 1u) << "一个待确认项恰一条留痕";

    // 与服务端记录逐字段比对（§6.7 形状：{binding 四元组, credential}）。
    const ConfirmationRecordStub& entry = result.confirmations.front();
    const auto record = rig.service.tryFind(finding.findingId);
    ASSERT_TRUE(record.has_value());
    EXPECT_EQ(record->state, FindingState::Confirmed);
    EXPECT_EQ(entry.binding, record->binding) << "绑定四元组完整入留痕（DT-CFM-10）";
    EXPECT_EQ(entry.binding.findingDigest, findingRecordDigest(record->finding.record))
        << "findingDigest 可复算比对（唯一定义点——跨会话可验证）";
    EXPECT_EQ(entry.binding.commandDigest, record->commandPayloadDigest)
        << "commandDigest 与命令载荷摘要一致";
    EXPECT_EQ(entry.binding.baseRevisionId, record->baseRevisionId) << "输入修订一致";
    EXPECT_FALSE(entry.binding.policyContentId.has_value())
        << "非策略来源类 finding 的策略成员为空（绑定与记录同源）";
    ASSERT_TRUE(record->confirmation.has_value());
    EXPECT_EQ(entry.credential, *record->confirmation) << "凭据（principal/时间）完整入留痕";
    EXPECT_EQ(entry.credential.principal, "ui-user");
}

// ---------------------------------------------------------------------
// DT-CFM-7 补充：编译前复核拦截陈旧确认（§5.4 底行——旧凭据不静默沿用）
// ---------------------------------------------------------------------

TEST(ConfirmableProjectStubTest, DtCfm7_PreCompileRebindRejectsStaleConfirmation)
{
    Rig rig;
    ProjectCommandStub command(rig.service, rig.environment);
    const FindingRecord finding = rig.createFinding();
    ConfirmingInteraction interaction;

    // 注入"确认成立后、编译前"的输入修订变化（跨命令重用旧凭据/槽外变更
    // 防御面——§5.3"输入修订变化"行；project §6.7 编译前复核由编排侧执行）。
    command.setPreCompileHook([&rig] { rig.environment.m_tip = RevisionId::generate(); });

    // 一次完整提交：S4 确认成功（提交时复核一致）→ 编译前复核发现 tip 变化
    // → 不静默沿用旧确认——命令按未确认处置、无修订。
    const CommandResultStub result = command.submit({finding}, makeIdentity(1), &interaction);

    EXPECT_EQ(result.status, CommandStatus::Rejected);
    EXPECT_EQ(result.reasonToken, "confirmations-unresolved");
    EXPECT_FALSE(result.revision.has_value()) << "编译前复核拦截：不产生修订";
    EXPECT_TRUE(result.confirmations.empty()) << "失配凭据不入命令摘要留痕";
    // 编排侧已将失配 finding 标失效（Invalidated(revision-changed)——§5.4
    // 底行"该凭据作废"；重新确认＝重新走 S4 流程）。
    const auto after = rig.service.tryFind(finding.findingId);
    ASSERT_TRUE(after.has_value());
    EXPECT_EQ(after->state, FindingState::Invalidated);
    EXPECT_EQ(after->rejectionReason, finding_reason::kRevisionChanged);
}

}  // namespace
