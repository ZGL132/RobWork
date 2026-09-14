/**
 * @file   ConfirmableTest.cpp
 * @brief  可确认诊断服务用例组（DT-CFM-1~5 服务端半区）——状态机全转移、
 *         绑定四元组复核、失效路径、惰性过期与 FindingId/摘要设施。
 *
 * 设计依据：
 *   - units/diagnostics.md §10 DT-CFM-1~DT-CFM-3（创建/确认、拒绝、超时）、
 *     DT-CFM-4/DT-CFM-5（修订/策略变化后复核失效）、§5.2（字段表）、§5.3
 *     （绑定四元组与失效条件）、§5.4（服务端状态机与转移约束）、§9.3
 *     （接口契约）；P-DIAG-4（FindingId 自持解析句法钉住）、P-DIAG-6
 *     （expiresAtUtc 默认无限期；失效/过期不进 core 投影）、CR-02（摘要
 *     唯一路径的确定性观测面）——契约 knownPitfalls 逐项自证
 *   - 需求 MDL-06④（确认不豁免编译——成功路径的命令侧半区见
 *     ConfirmableProjectStubTest）、ERR-01、CON-06
 *   - 任务契约 tasks/foundation/DIAG-T05.json acceptance 1~4（逐条自证：
 *     acceptance 1→DtCfm1~DtCfm6 组；acceptance 3→DtPdiag4 组；
 *     acceptance 4→DtPdiag6/DtCr02 组；SA-15 装配面断言的产品侧 static_assert
 *     见 Confirmable.hpp，运行期半区在契约测试 DT-CFM-8）
 *   - 用例名后缀＝矩阵行编号（DT-xxx-y），与 ird-test-report.json 的 trace
 *     追溯字段呼应（AGENTS.md §4.2 验证留痕）
 *
 * 线程约束：全部用例单线程（服务并发面由内部互斥承载——§9.3 契约表；多线程
 * 交错属集成观测面，§10 未设行，不私建）。
 */

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
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
// 测试文件位于全局匿名 ns：`core` 是 sdurws::ird 的成员，using-directive 不
// 引入兄弟命名空间——以别名使 core::X 限定名可见（既有套件同款处理面）。
namespace core = sdurws::ird::core;
using sdurws::ird::core::BranchId;
using sdurws::ird::core::ProjectId;
using sdurws::ird::core::RevisionId;

// ---------------------------------------------------------------------
// 夹具辅助（与既有套件同风格——自持不共享）
// ---------------------------------------------------------------------

/// 可确认类测试码（§5.5 时序图的样例码——阶段 A 内置表全为 confirmable=false
/// （CodeDescriptor 注释：可确认域码阶段 B 起随域卡注册），本套件按"业务域码
/// 随域卡注册"的既有机制注册之：MDL 前缀所有权归 modeling（§4.5 前缀表））。
inline constexpr const char* kTravelLimitCode = "MDL-06-TRAVEL-LIMIT";

/// 组装并 seal 注册表：内置全量＋可确认测试码（服务 create 的 confirmable
/// 前置校验面——运行期只读消费）。参数形态＝注册表禁拷贝/禁移动（进程级
/// 单例语义）——就地组装不经过返回值。
void sealRegistryWithConfirmableCode(StableCodeRegistry& registry)
{
    registerBuiltinCodes(registry);
    CodeDescriptor confirmable;
    confirmable.code = kTravelLimitCode;
    confirmable.ownerUnit = "modeling";
    confirmable.category = DiagnosticCategory::Confirmable;   // §4.3："策略校验超限待用户显式确认"
    confirmable.severity = DiagnosticSeverity::Warning;       // §4.3：Warning"含可确认类"
    confirmable.titleKey = "diag.mdl-06-travel-limit.title";  // P-DIAG-9 键约定
    confirmable.detailKey = "diag.mdl-06-travel-limit.detail";
    confirmable.paramSchema = "[]";                            // 无参数（必填字段——显式声明）
    confirmable.confirmable = true;                            // 服务 create 的前置锚点
    confirmable.requiresComparison = true;                     // confirmable⇒比较型（注册期验证链）
    confirmable.retryable = RetryKind::UserRetry;              // 动作族 confirm-or-fix——UserRetry（§4.4）
    registry.registerCode(confirmable);
    registry.seal();
}

/// 确定性测试时钟（§4.2 IClock 注释——testkit ManualClock 兼容形态；可推进）。
class ManualClock final : public IClock {
public:
    std::chrono::system_clock::time_point nowUtc() const override { return m_now; }
    void advance(std::chrono::seconds delta) { m_now += delta; }

private:
    std::chrono::system_clock::time_point m_now{std::chrono::seconds{1000000}};
};

/// 绑定复核环境探针桩（IConfirmationEnvironment 实现口径——tip/策略/命令三
/// 当前值可由用例驱动变化，DT-CFM-4/5 的"输入变化"注入点）。
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

    core::RevisionId m_tip = RevisionId::generate();              ///< 当前分支 tip
    std::optional<core::ContentIdentity> m_policy;                ///< 当前已解析策略内容身份
    std::optional<core::ContentIdentity> m_command;               ///< 命令槽内当前命令载荷摘要
};

/// 转移日志收集器（IDevLogSink 窄接口——§5.4"每次转移写开发级日志"的观测面；
/// DIAG-T07 logger 落地前的桩形态，DiagnosticsSinkImpl 注入位同形）。
class CollectingDevLog final : public IDevLogSink {
public:
    void logDev(std::string_view channel, std::string message) override
    {
        m_lines.emplace_back(std::string{channel}, std::move(message));
    }
    const std::vector<std::pair<std::string, std::string>>& lines() const { return m_lines; }
    /// 清空收集（观测分段——只断言某段流程内的转移行）。
    void clear() { m_lines.clear(); }

private:
    std::vector<std::pair<std::string, std::string>> m_lines;   ///< (channel, message) 收集
};

/// 断言抛出 DiagnosticsError 且错误码为 expected（错误码面钉住——§9.0）。
template <class Fn>
void expectThrowsWithCode(Fn&& fn, DiagnosticsErrorCode expected, const char* what)
{
    try {
        fn();
        FAIL() << what << "：未抛出异常（应拒绝并抛 DiagnosticsError）";
    } catch (const DiagnosticsError& e) {
        EXPECT_EQ(e.code(), expected) << what << "：错误码面不符（what()=" << e.what() << "）";
    } catch (...) {
        FAIL() << what << "：抛出了非 DiagnosticsError 异常（单元唯一异常类型纪律）";
    }
}

/// 合法比较值侧（Provided 数值＋用户来源＋单位 mm——行程上限场景 §5.5）。
core::ComparativeValue makeComparativeValue(double number, const char* unitSymbol)
{
    const auto unit = core::UnitToken::find(unitSymbol);
    // 生命周期：值随结构拷贝（SourcedValue 纯值语义——持有 Provenance 拷贝）。
    return core::ComparativeValue{
        core::SourcedValue<double>::provided(
            number, core::ValueProvenance::make(core::ProvenanceKind::UserProvided)),
        *unit};
}

/// 合法比较型三要素（实际超期望——行程超限：实际 620mm > 期望 550mm，§5.5）。
core::ComparativeFields makeOverLimitComparison()
{
    return core::ComparativeFields{makeComparativeValue(620.0, "mm"),
                                   makeComparativeValue(550.0, "mm")};
}

/// 合法可确认 finding（可确认码＋比较型三要素＋合法 subject——create 正例基线；
/// 各用例仅偏离被测面）。
core::ConfirmableFinding makeTravelLimitFinding()
{
    core::DiagnosticRecord record = core::DiagnosticRecord::make(
        kTravelLimitCode, core::ObjectId::generate(), std::string("joint_5"),
        std::string("Robot.joint_5"), std::string("行程上限超限待确认"),
        std::string("目标关节角行程 620mm 超出策略上限 550mm"),
        std::string("确认放行或修正目标位姿"),
        makeOverLimitComparison());
    return core::ConfirmableFinding::make(std::move(record));
}

/// 合法命令载荷摘要（非全零 ContentIdentity——create 前置"必填"）。
core::ContentIdentity makePayloadDigest(std::uint8_t tag)
{
    core::ContentIdentity id;
    id.bytes.fill(tag);
    return id;
}

/// 确认凭据（principal＋UTC 时刻——core 契约；时间取自测试时钟基线附近）。
core::ConfirmationCredential makeCredential(std::string principal)
{
    return core::ConfirmationCredential{
        std::move(principal),
        std::chrono::system_clock::time_point{std::chrono::seconds{1000000}}};
}

/// 服务与依赖的整装夹具（构造序＝依赖序：注册表→时钟→环境→日志→服务）。
struct ServiceRig {
    StableCodeRegistry registry;
    ManualClock clock;
    StubEnvironment environment;
    CollectingDevLog devLog;
    std::optional<ConfirmableService> service;

    ServiceRig()
    {
        sealRegistryWithConfirmableCode(registry);
        // 探针基线：命令槽内有当前命令、无已解析策略（非策略来源 finding 的
        // 绑定该成员为空——复核跳过）；tip＝生成值（与 create 传入一致即可）。
        environment.m_command = makePayloadDigest(0xAA);
        service.emplace(registry, environment, clock, &devLog);
    }

    /// 以基线上下文创建一个 Pending finding（正例基线——各用例再偏离被测面）。
    /// 探针 tip 与绑定 baseRevision 对齐：finding 绑定的输入版本即创建时
    /// 分支 tip（§5.3——否则提交复核必然 revision-changed）。
    FindingRecord createBaseline(RevisionId baseRevision)
    {
        environment.m_tip = baseRevision;
        return service->create(makeTravelLimitFinding(), "ApplyModel",
                               makePayloadDigest(0xAA), ProjectId::generate(),
                               BranchId::generate(), baseRevision, {}, {core::ObjectId::generate()});
    }
};

// ---------------------------------------------------------------------
// DT-CFM-1：Finding 创建/确认（SA-15、MDL-06④）
// ---------------------------------------------------------------------

TEST(ConfirmableTest, DtCfm1_CreateThenConfirmTransitionsToConfirmed)
{
    ServiceRig rig;
    const core::ConfirmableFinding source = makeTravelLimitFinding();
    const auto project = ProjectId::generate();
    const auto branch = BranchId::generate();
    const auto base = RevisionId::generate();
    const auto policy = makePayloadDigest(0x11);
    const auto payload = makePayloadDigest(0xAA);
    const core::ObjectId subject = core::ObjectId::generate();

    // 探针基线：tip＝绑定修订、策略身份＝绑定策略、命令槽内有当前命令
    // （绑定复核的"当前值"与冻结四元组一致）。
    rig.environment.m_tip = base;
    rig.environment.m_policy = policy;

    // create：后置（§9.3）——FindingRecord(Pending)；findingId 分配；binding
    // 四元组冻结；callbackToken 分配。
    const FindingRecord created = rig.service->create(
        source, "ApplyModel", payload, project, branch, base, policy, {subject});

    EXPECT_EQ(created.state, FindingState::Pending) << "初始态＝Pending（§5.2）";
    EXPECT_TRUE(created.findingId.isValid()) << "findingId 非零（保留值纪律）";
    EXPECT_NE(created.callbackToken, 0u) << "callbackToken 已分配（0＝保留值）";
    // 绑定四元组冻结（§5.3——逐成员核对）。
    EXPECT_EQ(created.binding.findingDigest, findingRecordDigest(source.record))
        << "findingDigest＝record canonical 摘要（唯一定义点复算比对）";
    EXPECT_EQ(created.binding.policyContentId, policy) << "策略内容身份入绑定（CON-06）";
    EXPECT_EQ(created.binding.commandDigest, payload) << "命令载荷摘要入绑定";
    EXPECT_EQ(created.binding.baseRevisionId, base) << "输入修订入绑定";
    EXPECT_EQ(created.project, project) << "项目身份入记录（复核定位域）";
    EXPECT_EQ(created.branch, branch) << "分支身份入记录";
    EXPECT_EQ(created.subjectScope, std::vector<core::ObjectId>{subject})
        << "作用对象集登记（确认只覆盖登记的作用对象——§5.3）";
    // expiresAtUtc 默认 nullopt（P-DIAG-6——acceptance 4）。
    EXPECT_FALSE(created.expiresAtUtc.has_value())
        << "expiresAtUtc 默认无限期（project §5.3.4 无超时自动确认）";
    // core 投影：Pending。
    EXPECT_EQ(created.coreProjection().state, core::ConfirmationState::Pending)
        << "core 投影三态：Pending（§5.4）";

    // submitConfirmation（绑定一致——环境未变）：Confirmed＋confirmation 写入。
    const core::ConfirmationCredential credential = makeCredential("user-alice");
    const ConfirmOutcome outcome =
        rig.service->submitConfirmation(created.findingId, created.callbackToken, credential);
    EXPECT_EQ(outcome, ConfirmOutcome::Confirmed) << "复核一致→Confirmed（§9.3 后置）";

    // 状态机观测（DT-CFM-1 观测点：FindingRecord.state；core 投影 Confirmed）。
    const std::optional<FindingRecord> after = rig.service->tryFind(created.findingId);
    ASSERT_TRUE(after.has_value()) << "已确认记录仍可查询（终态保留至清理周期）";
    EXPECT_EQ(after->state, FindingState::Confirmed);
    ASSERT_TRUE(after->confirmation.has_value()) << "confirmation 写入（principal/时间完整）";
    EXPECT_EQ(after->confirmation->principal, "user-alice");
    EXPECT_EQ(after->confirmation->confirmedAtUtc, credential.confirmedAtUtc);
    EXPECT_EQ(after->coreProjection().state, core::ConfirmationState::Confirmed)
        << "core 投影：Confirmed（§5.4——Confirmed 落回 core 三态）";
    EXPECT_TRUE(after->coreProjection().credential.has_value())
        << "core C-2：Confirmed ⇔ 凭据在场";
    // callbackToken 一次性：成功后同样作废（§5.2——再用→InvalidState）。
    EXPECT_EQ(rig.service->submitConfirmation(created.findingId, created.callbackToken,
                                              makeCredential("user-bob")),
              ConfirmOutcome::InvalidState)
        << "已 Confirmed 后再提交＝非法状态（§9.3 非法调用行）";
}

TEST(ConfirmableTest, DtCfm1_CreatePreconditionViolationsFailFast)
{
    ServiceRig rig;

    // ①非比较型（core C-1）→ ComparisonMissing：手工构造绕过 core::make 的
    // 形态（服务端复验，不信任上游类型状态——确认凭据只能经服务写入）。
    core::ConfirmableFinding nonComparative;
    nonComparative.record = core::DiagnosticRecord::make(
        kTravelLimitCode, core::ObjectId::generate(), {}, {}, std::string("ctx"),
        std::string("cause"), std::string("action"));   // comparison 为空
    expectThrowsWithCode(
        [&] {
            rig.service->create(nonComparative, "ApplyModel", makePayloadDigest(1),
                                ProjectId::generate(), BranchId::generate(),
                                RevisionId::generate(), {}, {core::ObjectId::generate()});
        },
        DiagnosticsErrorCode::ComparisonMissing, "非比较型 finding");

    // ②创建即带凭据/非 Pending 态 → Usage（凭据只经 submitConfirmation 写入）。
    core::ConfirmableFinding preConfirmed = makeTravelLimitFinding();
    preConfirmed.confirm(makeCredential("user"));
    expectThrowsWithCode(
        [&] {
            rig.service->create(preConfirmed, "ApplyModel", makePayloadDigest(1),
                                ProjectId::generate(), BranchId::generate(),
                                RevisionId::generate(), {}, {core::ObjectId::generate()});
        },
        DiagnosticsErrorCode::Usage, "创建即带凭据（绕过状态机）");

    // ③未注册码 → Usage（码值权威＝注册表；detail 指明）。
    core::DiagnosticRecord unknownCode = core::DiagnosticRecord::make(
        "MDL-06-NOT-REGISTERED", core::ObjectId::generate(), {}, {},
        std::string("ctx"), std::string("cause"), std::string("action"),
        makeOverLimitComparison());
    expectThrowsWithCode(
        [&] {
            rig.service->create(core::ConfirmableFinding::make(std::move(unknownCode)),
                                "ApplyModel", makePayloadDigest(1), ProjectId::generate(),
                                BranchId::generate(), RevisionId::generate(), {},
                                {core::ObjectId::generate()});
        },
        DiagnosticsErrorCode::Usage, "未注册码");

    // ④已注册但非可确认码（confirmable=false——内置 RT 码）→ Usage。
    core::DiagnosticRecord notConfirmable = core::DiagnosticRecord::make(
        "RT-ROBWORK-ERROR", core::ObjectId::generate(), {}, {}, std::string("ctx"),
        std::string("cause"), std::string("action"), makeOverLimitComparison());
    expectThrowsWithCode(
        [&] {
            rig.service->create(core::ConfirmableFinding::make(std::move(notConfirmable)),
                                "ApplyModel", makePayloadDigest(1), ProjectId::generate(),
                                BranchId::generate(), RevisionId::generate(), {},
                                {core::ObjectId::generate()});
        },
        DiagnosticsErrorCode::Usage, "非可确认码");

    // ⑤命令上下文缺失（§9.0 ContextMissing 行"命令路径→command/revision"）：
    // 命令摘要全零、修订身份全零各一探针。
    expectThrowsWithCode(
        [&] {
            rig.service->create(makeTravelLimitFinding(), "ApplyModel",
                                core::ContentIdentity{}, ProjectId::generate(),
                                BranchId::generate(), RevisionId::generate(), {},
                                {core::ObjectId::generate()});
        },
        DiagnosticsErrorCode::ContextMissing, "命令载荷摘要全零");
    expectThrowsWithCode(
        [&] {
            rig.service->create(makeTravelLimitFinding(), "ApplyModel",
                                makePayloadDigest(1), ProjectId::generate(),
                                BranchId::generate(), core::RevisionId{}, {},
                                {core::ObjectId::generate()});
        },
        DiagnosticsErrorCode::ContextMissing, "baseRevisionId 全零");

    // ⑥空命令类型 token / 空作用对象集 / 非法 ObjectId → Usage。
    expectThrowsWithCode(
        [&] {
            rig.service->create(makeTravelLimitFinding(), "", makePayloadDigest(1),
                                ProjectId::generate(), BranchId::generate(),
                                RevisionId::generate(), {}, {core::ObjectId::generate()});
        },
        DiagnosticsErrorCode::Usage, "sourceCommandType 为空");
    expectThrowsWithCode(
        [&] {
            rig.service->create(makeTravelLimitFinding(), "ApplyModel",
                                makePayloadDigest(1), ProjectId::generate(),
                                BranchId::generate(), RevisionId::generate(), {}, {});
        },
        DiagnosticsErrorCode::Usage, "subjectScope 为空");
    expectThrowsWithCode(
        [&] {
            rig.service->create(makeTravelLimitFinding(), "ApplyModel",
                                makePayloadDigest(1), ProjectId::generate(),
                                BranchId::generate(), RevisionId::generate(), {},
                                {core::ObjectId{}});
        },
        DiagnosticsErrorCode::Usage, "subjectScope 含全零 ObjectId");

    // 违约路径全部 fail-fast：无任何半构造记录产生（注册表只有违约前的基线
    // 记录——本用例未创建成功任何记录，pendingFor 为空）。
    EXPECT_TRUE(rig.service->pendingFor(RevisionId::generate()).empty())
        << "前置违约不产生记录（fail-fast——AGENTS.md 错误语义）";
}

// ---------------------------------------------------------------------
// DT-CFM-2：拒绝路径（MDL-06④——拒绝确认不产生修订；core 投影 Rejected）
// ---------------------------------------------------------------------

TEST(ConfirmableTest, DtCfm2_RejectionInvalidatesWithCoreRejectedProjection)
{
    ServiceRig rig;
    const FindingRecord created = rig.createBaseline(RevisionId::generate());

    // 提交拒绝（ui 返回 rejected——§9.3 submitRejection）。
    rig.service->submitRejection(created.findingId, created.callbackToken,
                                 finding_reason::kUserRejected);

    const std::optional<FindingRecord> after = rig.service->tryFind(created.findingId);
    ASSERT_TRUE(after.has_value());
    // 观测点（§10 DT-CFM-2）：state/reasonToken。
    EXPECT_EQ(after->state, FindingState::Invalidated);
    ASSERT_TRUE(after->rejectionReason.has_value());
    EXPECT_EQ(after->rejectionReason, finding_reason::kUserRejected);
    EXPECT_FALSE(after->confirmation.has_value()) << "拒绝无凭据写入";
    // core 投影：Rejected（MDL-06④——拒绝即阻止应用；core C-2 无凭据）。
    EXPECT_EQ(after->coreProjection().state, core::ConfirmationState::Rejected);
    EXPECT_FALSE(after->coreProjection().credential.has_value());

    // 终态不可逆：拒绝后再确认/再拒绝均拒绝（InvalidState——§5.4 转移约束）。
    EXPECT_EQ(rig.service->submitConfirmation(created.findingId, created.callbackToken,
                                              makeCredential("user")),
              ConfirmOutcome::InvalidState);
    expectThrowsWithCode(
        [&] {
            rig.service->submitRejection(created.findingId, created.callbackToken,
                                         finding_reason::kUserRejected);
        },
        DiagnosticsErrorCode::InvalidState, "已失效后重复拒绝");
}

// ---------------------------------------------------------------------
// DT-CFM-3：超时（仅显式设置时；默认实例恒 Pending——P-DIAG-6）
// ---------------------------------------------------------------------

TEST(ConfirmableTest, DtCfm3_ExpirationOnlyWhenExplicitlyArmed)
{
    ServiceRig rig;
    const FindingRecord unarmed = rig.createBaseline(RevisionId::generate());

    // 默认（未 armExpiration）：推进时钟仍恒 Pending（"默认实例恒 Pending"
    // ——§10 DT-CFM-3 观测点；P-DIAG-6 无超时自动确认）。
    rig.clock.advance(std::chrono::hours{24 * 365});
    ASSERT_FALSE(rig.service->tryFind(unarmed.findingId)->expiresAtUtc.has_value());
    EXPECT_EQ(rig.service->tryFind(unarmed.findingId)->state, FindingState::Pending)
        << "未设置过期：时钟推进不触发 Expired";
    // 未设置过期时确认照常成功（无限期——确认不被时间阻断）。
    EXPECT_EQ(rig.service->submitConfirmation(unarmed.findingId, unarmed.callbackToken,
                                              makeCredential("user")),
              ConfirmOutcome::Confirmed);

    // 显式设置（armExpiration——实现类原语，P-DIAG-6："显式设置"唯一起点）。
    const FindingRecord armed = rig.createBaseline(RevisionId::generate());
    const auto deadline = rig.clock.nowUtc() + std::chrono::minutes{30};
    rig.service->armExpiration(armed.findingId, deadline);

    // 到期前：确认窗口有效（仍 Pending）。
    EXPECT_EQ(rig.service->tryFind(armed.findingId)->state, FindingState::Pending);
    // 推进至过期时刻：惰性转移 Expired（§5.4——"expiresAtUtc 到期→Expired"）。
    rig.clock.advance(std::chrono::minutes{31});
    const std::optional<FindingRecord> expired = rig.service->tryFind(armed.findingId);
    ASSERT_TRUE(expired.has_value());
    EXPECT_EQ(expired->state, FindingState::Expired) << "到达过期时刻→Expired（终态）";
    EXPECT_EQ(expired->coreProjection().state, core::ConfirmationState::Pending)
        << "core 投影：Expired 不进 core 枚举（P-DIAG-6——服务端设施态）";
    // 过期后确认→InvalidState（终态不可确认；§10 无 Expired 结果值——口径
    // 登记于单元卡 v0.6）。
    EXPECT_EQ(rig.service->submitConfirmation(armed.findingId, armed.callbackToken,
                                              makeCredential("user")),
              ConfirmOutcome::InvalidState);
}

// ---------------------------------------------------------------------
// DT-CFM-4：输入修订变化后确认失效（BindingMismatch→revision-changed；
// 不静默沿用——观测点 ConfirmOutcome）
// ---------------------------------------------------------------------

TEST(ConfirmableTest, DtCfm4_RevisionChangeYieldsBindingMismatchInvalidation)
{
    ServiceRig rig;
    const auto base = RevisionId::generate();
    const FindingRecord created = rig.createBaseline(base);
    rig.environment.m_tip = base;   // 探针基线：tip 与绑定一致

    // 输入修订变化：tip 前进（跨命令重用/基线漂移——§5.3 失效条件表）。
    rig.environment.m_tip = RevisionId::generate();

    // 提交确认：复核不符→BindingMismatch（观测点 ConfirmOutcome）＋
    // Invalidated(revision-changed)——不静默沿用旧确认（§9.3 后置）。
    const ConfirmOutcome outcome = rig.service->submitConfirmation(
        created.findingId, created.callbackToken, makeCredential("user"));
    EXPECT_EQ(outcome, ConfirmOutcome::BindingMismatch);

    const std::optional<FindingRecord> after = rig.service->tryFind(created.findingId);
    ASSERT_TRUE(after.has_value());
    EXPECT_EQ(after->state, FindingState::Invalidated);
    ASSERT_TRUE(after->rejectionReason.has_value());
    EXPECT_EQ(after->rejectionReason, finding_reason::kRevisionChanged)
        << "不符成员归类：baseRevisionId→revision-changed（DT-CFM-4）";
    EXPECT_FALSE(after->confirmation.has_value()) << "失效凭据不写入 confirmation";
    // core 投影：非用户拒绝的失效不伪造 core 终局（Pending——P-DIAG-6）。
    EXPECT_EQ(after->coreProjection().state, core::ConfirmationState::Pending);

    // token 已作废（finishTransition 清零）：旧 token 再用→InvalidState。
    EXPECT_EQ(rig.service->submitConfirmation(created.findingId, created.callbackToken,
                                              makeCredential("user")),
              ConfirmOutcome::InvalidState)
        << "凭据作废后不得重放（§5.4——不静默沿用旧确认）";
}

/// DT-CFM-4 补充：命令槽语义（currentCommandDigest 失配/nullopt＝跨命令重用
/// ——绑定 commandDigest 失配同样 BindingMismatch，归 binding-mismatch）。
TEST(ConfirmableTest, DtCfm4_CommandDigestMismatchYieldsBindingMismatch)
{
    ServiceRig rig;
    const FindingRecord created = rig.createBaseline(RevisionId::generate());

    // 命令槽为空（原命令已终结——旧凭据跨命令重放）。
    rig.environment.m_command = std::nullopt;
    EXPECT_EQ(rig.service->submitConfirmation(created.findingId, created.callbackToken,
                                              makeCredential("user")),
              ConfirmOutcome::BindingMismatch);
    EXPECT_EQ(rig.service->tryFind(created.findingId)->rejectionReason,
              finding_reason::kBindingMismatch)
        << "命令载荷摘要失配→binding-mismatch（§5.3 四元组复核）";
}

// ---------------------------------------------------------------------
// DT-CFM-5：策略版本变化后确认失效（CON-06——policy-changed）
// ---------------------------------------------------------------------

TEST(ConfirmableTest, DtCfm5_PolicyChangeYieldsBindingMismatchInvalidation)
{
    ServiceRig rig;
    const auto policy = makePayloadDigest(0x11);
    const auto base = RevisionId::generate();
    rig.environment.m_tip = base;   // 探针基线：tip＝绑定修订（§5.3）
    const FindingRecord created = rig.service->create(
        makeTravelLimitFinding(), "ApplyModel", makePayloadDigest(0xAA),
        ProjectId::generate(), BranchId::generate(), base, policy,
        {core::ObjectId::generate()});
    rig.environment.m_policy = policy;   // 探针基线：策略身份与绑定一致

    // 策略版本变化：已解析策略内容身份改变（④端口复核——CON-06）。
    rig.environment.m_policy = makePayloadDigest(0x22);

    const ConfirmOutcome outcome = rig.service->submitConfirmation(
        created.findingId, created.callbackToken, makeCredential("user"));
    EXPECT_EQ(outcome, ConfirmOutcome::BindingMismatch);
    EXPECT_EQ(rig.service->tryFind(created.findingId)->rejectionReason,
              finding_reason::kPolicyChanged)
        << "不符成员归类：policyContentId→policy-changed（DT-CFM-5）";

    // 对照半区：非策略来源类 finding（绑定无 policyContentId）策略变化不触发
    // ——该成员不在其绑定约束内（复核跳过；"策略来源类须 policyContentId"
    // 的必填义务归调用方——§5.2 条件必填）。
    const FindingRecord noPolicy = rig.createBaseline(RevisionId::generate());
    rig.environment.m_policy = makePayloadDigest(0x33);
    EXPECT_EQ(rig.service->submitConfirmation(noPolicy.findingId, noPolicy.callbackToken,
                                              makeCredential("user")),
              ConfirmOutcome::Confirmed)
        << "绑定无策略身份：策略变化不参与复核（成员跳过）";
}

// ---------------------------------------------------------------------
// DT-CFM-6 服务端半区：invalidate 入口（write-lost/interaction-lost）＋
// token 一次性；"无永久等待"的服务端即时性
// ---------------------------------------------------------------------

TEST(ConfirmableTest, DtCfm6_InvalidateTransitionsAndTerminalGuards)
{
    ServiceRig rig;
    const FindingRecord lost = rig.createBaseline(RevisionId::generate());

    // 回调失效路径（§5.3 失效条件表"回调失效"行——project §5.3.3 信号驱动；
    // 桩联动半区在契约测试）：服务端入口 invalidate。
    rig.service->invalidate(lost.findingId, finding_reason::kInteractionLost);
    const std::optional<FindingRecord> afterLost = rig.service->tryFind(lost.findingId);
    EXPECT_EQ(afterLost->state, FindingState::Invalidated);
    EXPECT_EQ(afterLost->rejectionReason, finding_reason::kInteractionLost);
    // callbackToken 作废（§5.6）：再用→InvalidState（§10 DT-CFM-6 观测点）。
    EXPECT_EQ(rig.service->submitConfirmation(lost.findingId, lost.callbackToken,
                                              makeCredential("user")),
              ConfirmOutcome::InvalidState);

    // 权限变化路径（§5.3"权限变化"行——write-lost）。
    const FindingRecord writeLost = rig.createBaseline(RevisionId::generate());
    rig.service->invalidate(writeLost.findingId, finding_reason::kWriteLost);
    EXPECT_EQ(rig.service->tryFind(writeLost.findingId)->state, FindingState::Invalidated);
    EXPECT_EQ(rig.service->tryFind(writeLost.findingId)->rejectionReason,
              finding_reason::kWriteLost);

    // 会话关闭路径（§5.3"会话关闭/命令中止"行——canceled；无永久等待）。
    const FindingRecord canceled = rig.createBaseline(RevisionId::generate());
    rig.service->invalidate(canceled.findingId, finding_reason::kCanceled);
    EXPECT_EQ(rig.service->tryFind(canceled.findingId)->state, FindingState::Invalidated);
    EXPECT_EQ(rig.service->tryFind(canceled.findingId)->rejectionReason,
              finding_reason::kCanceled);

    // 终态不可逆（§5.4）：对已失效记录再失效→InvalidState 异常。
    expectThrowsWithCode(
        [&] { rig.service->invalidate(lost.findingId, finding_reason::kCanceled); },
        DiagnosticsErrorCode::InvalidState, "终态后再失效");

    // 调用方违约：未知 id／空 reason→Usage（fail-fast——§9.0 错误语义总纲）。
    expectThrowsWithCode([&] { rig.service->invalidate(FindingId::generate(),
                                                       finding_reason::kCanceled); },
                         DiagnosticsErrorCode::Usage, "invalidate 未知 findingId");
    expectThrowsWithCode(
        [&] {
            const FindingRecord pending = rig.createBaseline(RevisionId::generate());
            rig.service->invalidate(pending.findingId, "");
        },
        DiagnosticsErrorCode::Usage, "invalidate 空 reasonToken");
}

// ---------------------------------------------------------------------
// pendingFor：按输入修订过滤＋仅 Pending＋确定性序（§9.3）
// ---------------------------------------------------------------------

TEST(ConfirmableTest, DtCfm1_PendingForFiltersByRevisionAndState)
{
    ServiceRig rig;
    const auto target = RevisionId::generate();
    const auto other = RevisionId::generate();
    const FindingRecord a = rig.createBaseline(target);
    const FindingRecord b = rig.createBaseline(target);
    const FindingRecord c = rig.createBaseline(other);

    // 失效其一（target 修订上的 a）与 other 修订上的 c：pendingFor 只含
    // Pending（终态不占待确认集）且按修订过滤。
    rig.service->invalidate(a.findingId, finding_reason::kWriteLost);
    rig.service->invalidate(c.findingId, finding_reason::kCanceled);

    const std::vector<FindingRecord> pending = rig.service->pendingFor(target);
    ASSERT_EQ(pending.size(), 1u) << "仅基线未失效的那条保留";
    EXPECT_EQ(pending.front().findingId, b.findingId);
    EXPECT_TRUE(rig.service->pendingFor(other).empty()) << "修订过滤＋终态排除（c 已失效）";

    // 确定性序（NFR-COR-02 精神）：同集合同序——两次调用逐元素相等。
    const FindingRecord d = rig.createBaseline(target);
    const FindingRecord e = rig.createBaseline(target);
    EXPECT_EQ(rig.service->pendingFor(target), rig.service->pendingFor(target))
        << "同集合两次查询同序（findingId 字典序遍历）";
    EXPECT_EQ(rig.service->pendingFor(target).size(), 3u) << "b/d/e 三条 Pending";
    EXPECT_NE(d.findingId, e.findingId) << "generate 身份唯一（128 位随机）";
}

// ---------------------------------------------------------------------
// P-DIAG-4 处置：FindingId 自持解析——句法与 core Id128 严格一致（acceptance 3）
// ---------------------------------------------------------------------

TEST(ConfirmableTest, DtPdiag4_FindingIdCanonicalSyntaxMatchesCoreId128Rules)
{
    // generate→toCanonical 形态：fnd-＋32 小写 hex；parse(format(x))==x 往返。
    const FindingId id = FindingId::generate();
    const std::string canonical = id.toCanonical();
    EXPECT_EQ(canonical.size(), 36u) << "4（tag \"fnd-\"）＋32（hex）＝36";
    EXPECT_EQ(canonical.substr(0, 4), "fnd-") << "tag 冻结（P-DIAG-4）";
    for (std::size_t i = 4; i < canonical.size(); ++i) {
        const char ch = canonical[i];
        EXPECT_TRUE((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'))
            << "仅小写十六进制（大写拒绝——core Id128 同规则）";
    }
    const auto round = FindingId::tryFromCanonical(canonical);
    ASSERT_TRUE(round.has_value()) << "规范文本可解析";
    EXPECT_EQ(*round, id) << "parse(format(x))==x 往返（字节序一致）";
    EXPECT_TRUE(id.isValid());
    EXPECT_FALSE(FindingId{}.isValid()) << "全零＝保留值（isValid 恒 false）";

    // 严格性探针（句法与 core Id128 严格一致——tag/长度/字符集/边界逐项）：
    // tag 不符（obj- 前缀喂给 fnd 解析必须在边界失败——core 强类型纪律同款）；
    // 大写 hex；长度多一字节；缺 tag；空串。
    EXPECT_FALSE(FindingId::tryFromCanonical("obj-" + canonical.substr(4)).has_value());
    EXPECT_FALSE(FindingId::tryFromCanonical("fnd-" + std::string(32, 'A')).has_value());
    EXPECT_FALSE(FindingId::tryFromCanonical(canonical + "0").has_value());
    EXPECT_FALSE(FindingId::tryFromCanonical(canonical.substr(1)).has_value());
    EXPECT_FALSE(FindingId::tryFromCanonical("").has_value());
    // 抛出轨迹：fromCanonical 违约→DiagnosticsError(Usage)（本单元错误轨）。
    expectThrowsWithCode([&] { FindingId::fromCanonical("rev-0000"); },
                         DiagnosticsErrorCode::Usage, "tag 不符的 fromCanonical");
}

// ---------------------------------------------------------------------
// P-DIAG-6 处置：失效/过期的 core 投影钉住（acceptance 4——服务端扩展态
// 不进 core 枚举）；CR-02 处置：findingDigest 确定性与敏感性（acceptance 4）
// ---------------------------------------------------------------------

TEST(ConfirmableTest, DtPdiag6_NonRejectInvalidationAndExpiryProjectToCorePending)
{
    ServiceRig rig;
    // Invalidated(非 user-rejected)：core 投影 Pending（设施态不落 core——
    // §5.4 图注"Invalidated/Expired 仅存在于服务端记录"）。
    const FindingRecord invalidated = rig.createBaseline(RevisionId::generate());
    rig.service->invalidate(invalidated.findingId, finding_reason::kPolicyChanged);
    EXPECT_EQ(rig.service->tryFind(invalidated.findingId)->coreProjection().state,
              core::ConfirmationState::Pending);

    // Expired：同样投影 Pending。
    const FindingRecord expired = rig.createBaseline(RevisionId::generate());
    rig.service->armExpiration(expired.findingId, rig.clock.nowUtc());
    rig.clock.advance(std::chrono::seconds{1});
    ASSERT_EQ(rig.service->tryFind(expired.findingId)->state, FindingState::Expired);
    EXPECT_EQ(rig.service->tryFind(expired.findingId)->coreProjection().state,
              core::ConfirmationState::Pending);
    // 对照：user-rejected 投影 Rejected（该半区在 DtCfm2 已断言——此处不重复）。
}

TEST(ConfirmableTest, DtCr02_FindingDigestDeterministicAndSensitive)
{
    // 确定性（NFR-COR-02）：同记录两次摘要逐字节相等（DT-REG-1"两次计算相等"
    // 观测点同款）。
    const core::ConfirmableFinding finding = makeTravelLimitFinding();
    const core::Digest256 first = findingRecordDigest(finding.record);
    const core::Digest256 second = findingRecordDigest(finding.record);
    EXPECT_EQ(first, second) << "同内容同摘要（SHA-256 经 core ContentDigester——唯一路径）";

    // 敏感性（代表性字段逐项——内容变则摘要变，绑定复核才有意义）：
    // ①实际值数值；②单位；③context 文本；④subject 身份。
    core::DiagnosticRecord mutated = finding.record;
    mutated.comparison->actual = makeComparativeValue(621.0, "mm");
    EXPECT_NE(findingRecordDigest(mutated), first) << "实际值变化→摘要变化";

    mutated = finding.record;
    mutated.comparison->actual = makeComparativeValue(620.0, "deg");
    EXPECT_NE(findingRecordDigest(mutated), first) << "单位变化→摘要变化";

    mutated = finding.record;
    mutated.context = "行程上限超限待确认（另一会话）";
    EXPECT_NE(findingRecordDigest(mutated), first) << "上下文文本变化→摘要变化";

    mutated = finding.record;
    mutated.subject = core::ObjectId::generate();
    EXPECT_NE(findingRecordDigest(mutated), first) << "subject 变化→摘要变化";
}

// ---------------------------------------------------------------------
// §5.4 转移约束：每次状态转移写入开发级日志（findingId/转移/原因 token）
// ---------------------------------------------------------------------

TEST(ConfirmableTest, DtCfm6_StateTransitionsWriteDevLog)
{
    ServiceRig rig;
    // 两条 finding 绑定同一修订（同一输入版本可挂多个待确认项）——创建不
    // 推移探针 tip，保证随后的确认复核仍一致。
    const auto base = RevisionId::generate();
    const FindingRecord created = rig.createBaseline(base);
    const FindingRecord other = rig.createBaseline(base);
    rig.devLog.clear();   // 清掉 create 初态行，只观测转移行

    // 转移一：确认成功（Pending→Confirmed）。
    rig.service->submitConfirmation(created.findingId, created.callbackToken,
                                    makeCredential("user"));
    // 转移二：失效（Pending→Invalidated，带原因）。
    rig.service->invalidate(other.findingId, finding_reason::kInteractionLost);

    ASSERT_EQ(rig.devLog.lines().size(), 2u) << "每次转移恰一行（§5.4 可追溯）";
    for (const auto& [channel, message] : rig.devLog.lines()) {
        EXPECT_EQ(channel, "diag.confirmable") << "转移日志通道固定（§7.2 LogChannel 面）";
    }
    // 转移行内容：各行携带各自 findingId＋转移目标态；失效行附原因 token。
    EXPECT_NE(rig.devLog.lines()[0].second.find(created.findingId.toCanonical()),
              std::string::npos)
        << "确认行携带 findingId：" << rig.devLog.lines()[0].second;
    EXPECT_NE(rig.devLog.lines()[0].second.find("Confirmed"), std::string::npos)
        << "转移目标态入行：" << rig.devLog.lines()[0].second;
    EXPECT_NE(rig.devLog.lines()[1].second.find(other.findingId.toCanonical()),
              std::string::npos)
        << "失效行携带 findingId：" << rig.devLog.lines()[1].second;
    EXPECT_NE(rig.devLog.lines()[1].second.find("Invalidated"), std::string::npos);
    EXPECT_NE(rig.devLog.lines()[1].second.find("interaction-lost"), std::string::npos)
        << "原因 token 入行（可追溯——§5.4）：" << rig.devLog.lines()[1].second;
}

}  // namespace
