/**
 * @file   CatalogFactoryTest.cpp
 * @brief  诊断目录与工厂用例组（DT-DIAG-1/2、DT-DUP-1/2、DT-CAT-1/2）——
 *         字段完整性、subject 边界、去重与分类区分、转译器与统一 sink。
 *
 * 设计依据：
 *   - units/diagnostics.md §10 DT-DIAG-1/DT-DIAG-2/DT-DUP-1/DT-DUP-2/
 *     DT-CAT-1/DT-CAT-2/DT-REG-3/DT-REG-4（工厂/目录半区——§11 任务分工：
 *     T03 套件自证注册表侧原语，本套件补齐工厂构造与 translate 路径）、
 *     §4.2（信封字段与工厂校验）、§6.4（去重键——不同作用对象绝不合并）、
 *     §4.3/§4.4（分类/严重/动作族——取消=Info 正常取消非错误）、§8.1/§9.2
 *     （转译总则与工厂契约）、§9.7（目录与统一 sink）
 *   - 需求 ERR-01（字段/绑定对象）、UX-03（三要素；正常取消非错误）、
 *     TASK-02（取消/失败/中断分类区分）、CON-02（三轴正交）、NFR-MNT-03
 *     （码单一权威——工厂只接受已注册码）
 *   - 任务契约 tasks/foundation/DIAG-T04.json acceptance 1~4（逐条自证：
 *     acceptance 1→DtDiag/DtDup/DtCat 组；acceptance 2→DtCr08/DtReg4 组；
 *     acceptance 3→DtPdiag5 组；acceptance 4→DtPdiag1 组）；
 *   - 用例名后缀＝矩阵行编号（DT-xxx-y），与 ird-test-report.json 的 trace
 *     追溯字段呼应（AGENTS.md §4.2 验证留痕）。
 *
 * 线程约束：全部用例单线程（工厂并发安全面以原子 entryId 承载——§9.2 契约
 * 表；多线程交错属集成观测面，§10 未设行，不私建）。
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <sdurws/ird/core/DiagData.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/core/Provenance.hpp>
#include <sdurws/ird/core/Units.hpp>
#include <sdurws/ird/diagnostics/Catalog.hpp>
#include <sdurws/ird/diagnostics/DiagCodes.hpp>
#include <sdurws/ird/diagnostics/Errors.hpp>
#include <sdurws/ird/diagnostics/Factory.hpp>

namespace {

using namespace sdurws::ird::diagnostics;
// 测试文件位于全局匿名 ns：`core` 是 sdurws::ird 的成员，using-directive 不
// 引入兄弟命名空间——以别名使 core::X 限定名可见（T03 套件同款处理面）。
namespace core = sdurws::ird::core;
using sdurws::ird::core::AttemptId;
using sdurws::ird::core::BranchId;
using sdurws::ird::core::ObjectId;
using sdurws::ird::core::ProjectId;
using sdurws::ird::core::RevisionId;
using sdurws::ird::core::RunId;
using sdurws::ird::core::TaskIdentity;

// ---------------------------------------------------------------------
// 夹具辅助（与 StableCodeRegistryTest 同风格——自持不共享）。
// ---------------------------------------------------------------------

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

/// 码小写形（文案键材料——P-DIAG-9 冻结约定 diag.<code-lower>.title/detail；
/// 与注册期派生同约定，注册边界保证登记键即该形）。
std::string codeLower(std::string_view code)
{
    std::string lower;
    for (const char ch : code) {
        lower.push_back(ch >= 'A' && ch <= 'Z' ? static_cast<char>(ch - 'A' + 'a') : ch);
    }
    return lower;
}

/// 确定性测试时钟（§4.2 IClock 注释——testkit ManualClock 兼容形态）。
class ManualClock final : public IClock {
public:
    std::chrono::system_clock::time_point nowUtc() const override { return m_now; }

private:
    std::chrono::system_clock::time_point m_now{std::chrono::seconds{1000000}};
};

/// 组装并 seal 内置全量注册表（工厂按"运行期只读"消费）。
/// 参数形态＝注册表禁拷贝/禁移动（进程级单例语义）——就地组装不经过返回值。
void sealBuiltinRegistry(StableCodeRegistry& registry)
{
    registerBuiltinCodes(registry);
    registry.seal();
}

/// 合法五元组（TASK-03——五字段全 isValid；execution 域码 create 前置）。
TaskIdentity makeTaskIdentity()
{
    TaskIdentity task;
    task.project = ProjectId::generate();
    task.branch = BranchId::generate();
    task.revision = RevisionId::generate();
    task.run = RunId::generate();
    task.attempt = AttemptId::fromCanonical("att-1");
    return task;
}

/// 以码构造最小合法用户级记录（正例基线；各用例仅偏离被测面）。
core::DiagnosticRecord makeUserRecord(const std::string& code, ObjectId subject)
{
    return core::DiagnosticRecord::make(
        code, subject, std::string("joint_5"), std::string("Robot.joint_5"),
        std::string("评估路径输入非法"), std::string("启用 Must 条目取值越域"),
        std::string("修正输入后重新评估"));
}

/// Dev 级码记录（EX-CHANNEL-PROTOCOL-ERROR——subject 可空，§4.2 合法实例 2）。
core::DiagnosticRecord makeDevRecord()
{
    return core::DiagnosticRecord::make(
        "EX-CHANNEL-PROTOCOL-ERROR", {}, {}, {},
        std::string("worker 通道帧序断裂"), std::string("帧序号回退"),
        std::string("检查回传批次序号"));
}

/// runtime 域上下文基线（sourceUnit/sourceInterface 必填——§4.2 token 边界）。
DiagContext makeBaseContext()
{
    DiagContext context;
    context.sourceUnit = "runtime";
    context.sourceInterface = "compile.workcell";
    return context;
}

/// execution 域上下文（EX 域码 create 前置：合法 task 五元组——§8.10）。
DiagContext makeExecutionContext()
{
    DiagContext context;
    context.sourceUnit = "execution";
    context.sourceInterface = "channel.error-report";
    context.task = makeTaskIdentity();
    return context;
}

// =====================================================================
// DT-DIAG-1／DT-DIAG-2——字段完整性与 subject 边界（acceptance 1 主体）
// =====================================================================

class DiagCatalogFactory : public ::testing::Test {
protected:
    void SetUp() override
    {
        registerBuiltinCodes(m_registry);
        m_registry.seal();
        m_factory = std::make_unique<DiagnosticsFactory>(m_registry, m_clock);
    }

    StableCodeRegistry m_registry;   ///< 码表（每用例独立——互不污染）
    ManualClock m_clock;             ///< 确定性时钟（emittedAtUtc 可断言）
    std::unique_ptr<DiagnosticsFactory> m_factory;
};

/// DT-DIAG-1：工厂产出条目的字段完整性——分类/严重自码表（不允许覆盖，
/// NFR-MNT-03）、entryId 单调 ≥1、dedupKey/orderKey 已赋、时间来自注入时钟、
/// contractVersions 追加码表版本标注（§4.5）、record 原样内嵌（P-DIAG-1）。
TEST_F(DiagCatalogFactory, DtDiag1_EntryFieldsCompleteFromRegistry)
{
    const ObjectId subject = ObjectId::generate();
    const DiagContext context = makeBaseContext();
    const core::DiagnosticRecord record = makeUserRecord("RT-INPUT-INVALID", subject);

    const DiagnosticEntry entry = m_factory->create(record, context);

    // 分类/严重＝码表登记值（§4.3——工厂解析，调用方无覆盖面）。
    const CodeDescriptor* descriptor = m_registry.find("RT-INPUT-INVALID");
    ASSERT_NE(descriptor, nullptr);
    EXPECT_EQ(entry.category, descriptor->category);
    EXPECT_EQ(entry.severity, descriptor->severity);
    // 身份/排序字段已赋（§9.2 后置"entryId/dedupKey/orderKey 已赋"）。
    EXPECT_GE(entry.entryId, 1u);
    EXPECT_EQ(entry.dedupKey.code, "RT-INPUT-INVALID");
    EXPECT_EQ(entry.dedupKey.scopeKind, ScopeKind::Object);   // 无 task 上下文→对象锚定
    EXPECT_EQ(entry.dedupKey.subject, subject);
    EXPECT_EQ(std::get<1>(entry.orderKey), entry.entryId);    // 第二分量＝entryId
    EXPECT_EQ(std::get<2>(entry.orderKey), "RT-INPUT-INVALID");
    // 时间来自注入时钟（§9.2"时间戳来自注入 IClock"——同输入同输出）。
    EXPECT_EQ(entry.emittedAtUtc, m_clock.nowUtc());
    // record 原样内嵌（ERR-01 语义字段——P-DIAG-1：core.md v0.1 基线）。
    EXPECT_EQ(entry.record, record);
    ASSERT_TRUE(entry.record.subject.has_value());
    // 码表版本标注（§4.5：registryVersion 进入 contractVersions）。
    bool hasVersionTag = false;
    for (const auto& [name, version] : entry.context.contractVersions) {
        if (name == "diag-code" && version == descriptor->registryVersion) {
            hasVersionTag = true;
        }
    }
    EXPECT_TRUE(hasVersionTag) << "码表版本标注缺失（§4.5）";
    // entryId 单调（同工厂连续创建——§9.2"entryId 单调序"）。
    const DiagnosticEntry second =
        m_factory->create(makeUserRecord("RT-UNIT-MISMATCH", ObjectId::generate()), context);
    EXPECT_EQ(second.entryId, entry.entryId + 1);
}

/// DT-DIAG-1：比较型三要素与"不适用显式标记"——requiresComparison 码必须携带
/// comparison（缺→ComparisonMissing）；三要素经 SourcedValue 承载，不适用为
/// 显式标记非数值（UX-03/ERR-01：state()==not-applicable、取值通道切断）。
TEST_F(DiagCatalogFactory, DtDiag1_ComparisonRequiredAndNotApplicableMarker)
{
    // 阶段 A 内置表无可确认/比较型码（§4.6 表尾行"阶段 B 起注册"）——按
    // §4.5 注册协议以测试域码补位（KIN 前缀→kinematics 域，前缀-所有权一致；
    // 在 seal 前注册——装配期原语纪律）。
    CodeDescriptor travel;
    travel.code = "KIN-05-TEST-COMPARISON";
    travel.ownerUnit = "kinematics";
    travel.category = DiagnosticCategory::Confirmable;
    travel.severity = DiagnosticSeverity::Warning;
    travel.titleKey = "diag." + codeLower(travel.code) + ".title";
    travel.detailKey = "diag." + codeLower(travel.code) + ".detail";
    travel.paramSchema = "[]";
    travel.confirmable = true;             // SA-15：可确认 ⇒ 必为比较型（注册期验证）
    travel.requiresComparison = true;
    travel.retryable = RetryKind::UserRetry;
    StableCodeRegistry localRegistry;
    registerBuiltinCodes(localRegistry);
    localRegistry.registerCode(travel);
    localRegistry.seal();
    DiagnosticsFactory factory(localRegistry, m_clock);

    core::ComparativeFields comparison;
    // 实际值侧＝有值（1.2 rad）；期望值侧＝不适用（显式标记——ERR-01"不伪造
    // 数值"：not-applicable 态的取值通道在类型层面切断）。注意 ComparativeValue
    // 的数值载荷在 .quantity 成员（core §4.8 两字段表）。
    comparison.actual.quantity = core::SourcedValue<double>::provided(
        1.2, core::ValueProvenance::make(core::ProvenanceKind::DerivedReadOnly));
    comparison.expected.quantity = core::SourcedValue<double>::notApplicable();
    comparison.actual.unit = *core::UnitToken::find("rad");
    comparison.expected.unit = *core::UnitToken::find("rad");

    const core::DiagnosticRecord record = core::DiagnosticRecord::make(
        "KIN-05-TEST-COMPARISON", ObjectId::generate(), std::string("joint_3"),
        std::string("Robot.joint_3"), std::string("行程比较"), std::string("期望侧不适用"),
        std::string("核对工况适用条件"), comparison);
    const DiagnosticEntry entry = factory.create(record, makeBaseContext());

    // 三要素进入条目且值形态保持（ERR-01 比较型字段——实际/期望/单位）。
    ASSERT_TRUE(entry.record.comparison.has_value());
    EXPECT_EQ(entry.record.comparison->actual.unit.symbol(), "rad");
    EXPECT_EQ(entry.record.comparison->expected.unit.symbol(), "rad");
    ASSERT_TRUE(entry.record.comparison->actual.quantity.tryValue().has_value());
    EXPECT_DOUBLE_EQ(*entry.record.comparison->actual.quantity.tryValue(), 1.2);
    // 不适用＝显式标记非数值（MDL-06/ERR-01——state 与取值通道双断言）。
    EXPECT_EQ(entry.record.comparison->expected.quantity.state(),
              core::FieldState::NotApplicable);
    EXPECT_FALSE(entry.record.comparison->expected.quantity.tryValue().has_value());

    // 反例：requiresComparison 码缺 comparison → ComparisonMissing（§9.2）。
    core::DiagnosticRecord missing = record;
    missing.comparison.reset();
    expectThrowsWithCode(
        [&] { (void)factory.create(missing, makeBaseContext()); },
        DiagnosticsErrorCode::ComparisonMissing, "比较型码缺三要素");
}

/// DT-DIAG-2：subject 边界——用户级码缺 subject 拒绝（SubjectMissing）；非法
/// （全零）subject 拒绝；Dev 码 subject 可空（core §4.8 交接——完整性强制归
/// diagnostics 边界，acceptance 1"用户级码缺 subject 拒绝"）。
TEST_F(DiagCatalogFactory, DtDiag2_SubjectBoundary)
{
    const DiagContext context = makeBaseContext();

    // 用户级码缺 subject → SubjectMissing。
    const core::DiagnosticRecord noSubject = core::DiagnosticRecord::make(
        "RT-INPUT-INVALID", {}, {}, {}, std::string("输入非法"), std::string("越域"),
        std::string("修正输入"));
    expectThrowsWithCode([&] { (void)m_factory->create(noSubject, context); },
                         DiagnosticsErrorCode::SubjectMissing, "用户级码缺 subject");

    // 非法（全零）subject 同样拒绝（"合法 subjectObjectId"——保留值不是绑定）。
    core::DiagnosticRecord zeroSubject = noSubject;
    zeroSubject.subject = ObjectId{};
    expectThrowsWithCode([&] { (void)m_factory->create(zeroSubject, context); },
                         DiagnosticsErrorCode::SubjectMissing, "用户级码 subject 全零");

    // Dev 码 subject 可空（core §4.8"瞬时开发诊断可空"——合法实例，§4.2）。
    const DiagnosticEntry devEntry =
        m_factory->create(makeDevRecord(), makeExecutionContext());
    EXPECT_EQ(devEntry.severity, DiagnosticSeverity::Dev);
    EXPECT_FALSE(devEntry.record.subject.has_value());
}

/// DT-DIAG-2：分类/严重仅自码表（构造唯一入口——类型无 setter 的机制阻断）＋
/// 未注册码/异常文本作码/废弃码拒绝（DT-REG-3/4 的工厂半区——T03 套件覆盖
/// 边界的补齐）。
TEST_F(DiagCatalogFactory, DtDiag2_CategoryFromRegistryAndCodeGuards)
{
    const ObjectId subject = ObjectId::generate();

    // 分类/严重与注册表逐码一致（抽查取消/执行失败——码表单一权威）。
    const DiagnosticEntry cancelled =
        m_factory->create(makeUserRecord("RT-CANCELLED", subject), makeBaseContext());
    EXPECT_EQ(cancelled.category, DiagnosticCategory::Canceled);
    EXPECT_EQ(cancelled.severity, DiagnosticSeverity::Info);
    const DiagnosticEntry crashed =
        m_factory->create(makeUserRecord("EX-WORKER-CRASHED", subject),
                          makeExecutionContext());
    EXPECT_EQ(crashed.category, DiagnosticCategory::ExecutionFailed);
    EXPECT_EQ(crashed.severity, DiagnosticSeverity::Error);

    // 未注册码 → CodeUnknown（§4.5"工厂以未注册码构造 → 拒绝"）。
    const core::DiagnosticRecord unknown = makeUserRecord("RT-NOT-REGISTERED", subject);
    expectThrowsWithCode([&] { (void)m_factory->create(unknown, makeBaseContext()); },
                         DiagnosticsErrorCode::CodeUnknown, "未注册码构造");

    // 异常文本直接作码 → CodeUnknown（DT-REG-4 工厂半区："诊断码不能通过
    // 异常文本临时生成"——字符串不经登记不可能成为码）。构造走 DiagnosticRecord
    // 的聚合形态（不经 core make 句法闸——那是 core 的防线；本断言钉住
    // diagnostics 工厂对"已绕过句法闸的记录"仍以未注册拒绝，纵深防御）。
    core::DiagnosticRecord asText;   // 未登记构造路径（聚合形态，字段直填）
    asText.code = "std exception: boom 42 （异常文本不是码）";
    asText.context = "输入非法";
    asText.cause = "越域";
    asText.recommendedAction = "修正输入";
    asText.subject = subject;
    expectThrowsWithCode([&] { (void)m_factory->create(asText, makeBaseContext()); },
                         DiagnosticsErrorCode::CodeUnknown, "异常文本作码");

    // 废弃码构造拒绝（§4.5.1——tombstone 只读映射，不承载新实例；局部表
    // 在 seal 前完成登记与废弃——装配期原语纪律）。
    StableCodeRegistry localRegistry;
    registerBuiltinCodes(localRegistry);
    CodeDescriptor legacyDescriptor;
    legacyDescriptor.code = "RT-04-LEGACY-CODE";
    legacyDescriptor.ownerUnit = "runtime";
    legacyDescriptor.category = DiagnosticCategory::InputInvalid;
    legacyDescriptor.severity = DiagnosticSeverity::Error;
    legacyDescriptor.titleKey = "diag." + codeLower(legacyDescriptor.code) + ".title";
    legacyDescriptor.detailKey = "diag." + codeLower(legacyDescriptor.code) + ".detail";
    legacyDescriptor.paramSchema = "[]";
    legacyDescriptor.retryable = RetryKind::UserRetry;
    localRegistry.registerCode(legacyDescriptor);
    localRegistry.deprecate("RT-04-LEGACY-CODE", std::optional<std::string>{});
    localRegistry.seal();
    DiagnosticsFactory legacyFactory(localRegistry, m_clock);
    const core::DiagnosticRecord legacy = core::DiagnosticRecord::make(
        "RT-04-LEGACY-CODE", subject, {}, {}, std::string("旧语义"),
        std::string("旧原因"), std::string("改用新码"));
    expectThrowsWithCode([&] { (void)legacyFactory.create(legacy, makeBaseContext()); },
                         DiagnosticsErrorCode::CodeDeprecated, "废弃码构造");
}

/// §4.5/§9.2 占位一致性：PRJ-LOCK-HELD 的 ["pid","host"] 模式——缺参/多参/
/// 精确匹配三分支（"实例的 context/cause/comparison 须按模式填充"）。
TEST_F(DiagCatalogFactory, DtDiag2_ParamSchemaPlaceholderConsistency)
{
    const ObjectId subject = ObjectId::generate();
    DiagContext context = makeBaseContext();
    context.sourceUnit = "project";
    context.sourceInterface = "store.open";
    const core::DiagnosticRecord record = makeUserRecord("PRJ-LOCK-HELD", subject);

    // 缺参 → ParamSchemaMismatch（模式 ["pid","host"]，实例空）。
    expectThrowsWithCode([&] { (void)m_factory->create(record, context); },
                         DiagnosticsErrorCode::ParamSchemaMismatch, "参数占位缺失");

    // 精确匹配 → 通过（§9.2 调用示例形态）。
    context.params = {{"pid", "4242"}, {"host", "workstation-7"}};
    const DiagnosticEntry ok = m_factory->create(record, context);
    EXPECT_EQ(ok.record.code, "PRJ-LOCK-HELD");

    // 多余占位键 → 同面拒绝（键集须完全一致——"按模式填充"）。
    context.params.emplace("extra", "nope");
    expectThrowsWithCode([&] { (void)m_factory->create(record, context); },
                         DiagnosticsErrorCode::ParamSchemaMismatch, "多余参数占位");
}

/// §8.10 上下文必填：execution 域码缺合法 task 五元组 → ContextMissing；
/// 携带合法五元组即通过（TASK-03——阶段 A 机械执行的锚定规则）。
TEST_F(DiagCatalogFactory, DtDiag2_ExecutionContextRequired)
{
    const ObjectId subject = ObjectId::generate();
    const core::DiagnosticRecord record = makeUserRecord("EX-WORKER-CRASHED", subject);

    // 缺 task → ContextMissing。
    DiagContext noTask = makeBaseContext();
    noTask.sourceUnit = "execution";
    noTask.sourceInterface = "channel.error-report";
    expectThrowsWithCode([&] { (void)m_factory->create(record, noTask); },
                         DiagnosticsErrorCode::ContextMissing, "execution 码缺 task");

    // task 残缺（attempt 空保留值）→ 同面拒绝（残缺五元组＝没有锚定）。
    DiagContext partialTask = noTask;
    partialTask.task = makeTaskIdentity();
    partialTask.task->attempt = AttemptId{};
    expectThrowsWithCode([&] { (void)m_factory->create(record, partialTask); },
                         DiagnosticsErrorCode::ContextMissing, "task 五元组残缺");

    // 合法五元组 → 通过，且去重键为 Task 作用域（scopeId 含五元组串）。
    DiagContext withTask = noTask;
    withTask.task = makeTaskIdentity();
    const DiagnosticEntry entry = m_factory->create(record, withTask);
    EXPECT_EQ(entry.dedupKey.scopeKind, ScopeKind::Task);
    EXPECT_NE(entry.dedupKey.scopeId.find("att-1"), std::string::npos);
}

// =====================================================================
// DT-DUP-1／DT-DUP-2——去重（acceptance 1：不同作用对象绝不合并）
// =====================================================================

/// DT-DUP-1：同 (code, subject, scope) 重复 append 折叠计数——目录保留首条、
/// occurrences=N；重复 snapshot 序稳定（§6.4；DT-DUP-1 目录半区）。
TEST_F(DiagCatalogFactory, DtDup1_SameKeyOccurrencesCounted)
{
    DiagCatalog catalog;
    const DiagContext context = makeBaseContext();
    const ObjectId subject = ObjectId::generate();
    const core::DiagnosticRecord record = makeUserRecord("RT-INPUT-INVALID", subject);

    DiagEntryId firstId = 0;
    for (int i = 0; i < 3; ++i) {
        DiagnosticEntry entry = m_factory->create(record, context);
        if (i == 0) {
            firstId = entry.entryId;
        }
        catalog.append(std::move(entry));
    }

    // 首条目＋occurrences=3（不重复占位——§6.4"目录保留首条，后续命中计数"）。
    EXPECT_EQ(catalog.size(), 1u);
    const std::vector<DiagProjectionItem> view = catalog.snapshot(DiagQuery{});
    ASSERT_EQ(view.size(), 1u);
    EXPECT_EQ(view[0].entryId, firstId);
    EXPECT_EQ(view[0].occurrences, 3u);
    // 重复投影序稳定（NFR-COR-02——同输入同序）。
    const std::vector<DiagProjectionItem> again = catalog.snapshot(DiagQuery{});
    ASSERT_EQ(again.size(), 1u);
    EXPECT_EQ(again[0].occurrences, 3u);
}

/// DT-DUP-2：不同作用对象绝不合并——同码两 subject 各自成条，subject 各自
/// 保留（§6.4 反例钉住："不同作用对象不能错误去重"）。
TEST_F(DiagCatalogFactory, DtDup2_DifferentSubjectsNeverMerged)
{
    DiagCatalog catalog;
    const DiagContext context = makeBaseContext();
    const ObjectId joint5 = ObjectId::generate();
    const ObjectId joint6 = ObjectId::generate();

    catalog.append(m_factory->create(makeUserRecord("RT-INPUT-INVALID", joint5), context));
    catalog.append(m_factory->create(makeUserRecord("RT-INPUT-INVALID", joint6), context));

    // 两条独立条目（DedupKey 不同——键含 subject）。
    EXPECT_EQ(catalog.size(), 2u);
    const std::vector<DiagProjectionItem> view = catalog.snapshot(DiagQuery{});
    ASSERT_EQ(view.size(), 2u);
    EXPECT_EQ(view[0].occurrences, 1u);
    EXPECT_EQ(view[1].occurrences, 1u);
    // subject 各自保留（展开即得逐对象明细——表 2④ 全量口径的目录面）。
    EXPECT_EQ(view[0].subject, joint5);
    EXPECT_EQ(view[1].subject, joint6);
}

/// DT-DUP-2 补面：同码同对象、不同任务运行——不同 DedupKey 不合并
/// （§6.4"跨任务不聚合"的键面保证）＋任务过滤互不混（DT-COLLAB-1 过滤面）。
TEST_F(DiagCatalogFactory, DtDup2_DifferentTaskScopeNotMerged)
{
    DiagCatalog catalog;
    const ObjectId subject = ObjectId::generate();

    DiagContext runOne = makeBaseContext();
    runOne.task = makeTaskIdentity();
    DiagContext runTwo = makeBaseContext();
    runTwo.task = makeTaskIdentity();

    catalog.append(m_factory->create(makeUserRecord("RT-INPUT-INVALID", subject), runOne));
    catalog.append(m_factory->create(makeUserRecord("RT-INPUT-INVALID", subject), runTwo));

    EXPECT_EQ(catalog.size(), 2u);   // 跨任务各自成条
    DiagQuery runOneOnly;
    runOneOnly.task = runOne.task;
    const std::vector<DiagProjectionItem> filtered = catalog.snapshot(runOneOnly);
    ASSERT_EQ(filtered.size(), 1u);
    EXPECT_EQ(filtered[0].context.task, runOne.task);   // 命中且互不混
}

// =====================================================================
// DT-CAT-1／DT-CAT-2——分类区分（acceptance 1：取消=Info 且正常取消非错误）
// =====================================================================

/// DT-CAT-1：输入非法 vs 执行失败——分类/严重/动作族全不同（CON-02/TASK-02：
/// 三轴正交的呈现分组；分类仅呈现映射不改写 outcome 轴——§4.3）。
TEST_F(DiagCatalogFactory, DtCat1_InputInvalidVsExecutionFailed)
{
    const ObjectId subject = ObjectId::generate();

    const DiagnosticEntry invalid =
        m_factory->create(makeUserRecord("RT-INPUT-INVALID", subject), makeBaseContext());
    const DiagnosticEntry failed =
        m_factory->create(makeUserRecord("EX-WORKER-CRASHED", subject),
                          makeExecutionContext());

    // 分类/动作族不同（§4.4 矩阵行——fix-input vs retry-task）；严重按 §4.6
    // 登记值逐码断言（两码均 Error——DT-CAT-1 的"区分"承载于分类/动作族，
    // 严重值为码表登记事实，不私改）。
    EXPECT_NE(invalid.category, failed.category);
    EXPECT_EQ(invalid.severity, DiagnosticSeverity::Error);
    EXPECT_EQ(failed.severity, DiagnosticSeverity::Error);
    EXPECT_NE(actionKindToken(invalid.category), actionKindToken(failed.category));
    EXPECT_EQ(actionKindToken(invalid.category), "fix-input");
    EXPECT_EQ(actionKindToken(failed.category), "retry-task");
}

/// DT-CAT-2：取消/失败/中断/超时四终态区分——取消=Info 且非错误（UX-03
/// "正常用户取消不属于错误、不产生错误诊断"）；失败=Error；中断=Info 可重跑；
/// 超时=Error（TASK-02 分类区分）。
TEST_F(DiagCatalogFactory, DtCat2_TerminalKindsDistinct)
{
    const ObjectId subject = ObjectId::generate();

    const DiagnosticEntry canceled =
        m_factory->create(makeUserRecord("RT-CANCELLED", subject), makeBaseContext());
    const DiagnosticEntry failed =
        m_factory->create(makeUserRecord("EX-WORKER-CRASHED", subject),
                          makeExecutionContext());
    const DiagnosticEntry interrupted =
        m_factory->create(makeUserRecord("EX-TASK-INTERRUPTED", subject),
                          makeExecutionContext());
    const DiagnosticEntry timeout =
        m_factory->create(makeUserRecord("EX-FORCE-TERMINATED", subject),
                          makeExecutionContext());

    // 取消＝Info 且非错误、动作族 none（§4.3 取消行/§4.4 矩阵——正常取消不
    // 产生错误呈现；RT-CANCELLED 定 Info 与 UX-03 同源）。
    EXPECT_EQ(canceled.category, DiagnosticCategory::Canceled);
    EXPECT_EQ(canceled.severity, DiagnosticSeverity::Info);
    EXPECT_EQ(actionKindToken(canceled.category), "none");
    // 失败＝Error（执行失败轴）。
    EXPECT_EQ(failed.category, DiagnosticCategory::ExecutionFailed);
    EXPECT_EQ(failed.severity, DiagnosticSeverity::Error);
    // 中断＝Info＋可重跑（NFR-REL-03"已中断"呈现）。
    EXPECT_EQ(interrupted.category, DiagnosticCategory::Interrupted);
    EXPECT_EQ(interrupted.severity, DiagnosticSeverity::Info);
    EXPECT_EQ(actionKindToken(interrupted.category), "rerun-interrupted");
    // 超时＝Error（强杀显式区分——EX-FORCE-TERMINATED）。
    EXPECT_EQ(timeout.category, DiagnosticCategory::Timeout);
    EXPECT_EQ(timeout.severity, DiagnosticSeverity::Error);
    // 四终态 (category, severity) 两两互异（分类区分的集合断言）。
    using CatSev = std::pair<DiagnosticCategory, DiagnosticSeverity>;
    const std::vector<CatSev> quad{
        {canceled.category, canceled.severity}, {failed.category, failed.severity},
        {interrupted.category, interrupted.severity}, {timeout.category, timeout.severity}};
    for (std::size_t i = 0; i < quad.size(); ++i) {
        for (std::size_t j = i + 1; j < quad.size(); ++j) {
            EXPECT_NE(quad[i], quad[j]) << "终态 " << i << " 与 " << j << " 分类混淆";
        }
    }
}

// =====================================================================
// CR-08／DT-REG-4——ErrorCodeTranslator（acceptance 2 主体）
// =====================================================================

/// 测试局部错误类型（CR-08 机制面：类型映射——登记后按静态类型命中；未登记
/// 类型落入 DIAG-REGISTRY-UNKNOWN-CODE 兜底）。
struct TestCompileError : std::runtime_error {
    explicit TestCompileError(std::string message)
        : std::runtime_error(std::move(message)) {}
};

/// 非异常体系的测试类型（兜底路径的"消息不可得"分支）。
struct TestOpaqueFailure {};

class ErrorCodeTranslatorTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        registerBuiltinCodes(m_registry);
        m_registry.seal();
        m_translator = std::make_unique<DiagnosticsFactory>(m_registry, m_clock);
        registerStageATranslations(*m_translator);   // §8.3：std::exception→RT-ROBWORK-ERROR
    }

    StableCodeRegistry m_registry;
    ManualClock m_clock;
    std::unique_ptr<DiagnosticsFactory> m_translator;
};

/// CR-08 处置：已登记类型映射转译为稳定码＋DiagnosticRecord——std::exception
/// 体系兜底至 RT-ROBWORK-ERROR（§8.3 阶段 A 清单）；根因条目 causedBy 链接、
/// subject 继承（§6.3 不丢根因/不丢作用对象）。
TEST_F(ErrorCodeTranslatorTest, DtCr08_RegisteredTypeTranslatesToStableCode)
{
    DiagCatalog catalog;
    const ObjectId subject = ObjectId::generate();
    const DiagContext context = makeBaseContext();

    // 根因条目（§6.3 链序：编译失败诊断先于兜底条目存在于目录）。
    DiagnosticEntry root =
        m_translator->create(makeUserRecord("RT-WC-COMPILE-FAILED", subject), context);
    catalog.append(root);

    // 转译（root 携带——用户级目标码的 subject 来源；§9.2 translate 签名）。
    const TestCompileError err("workcell compile failed: bad link");
    DiagnosticEntry derived = m_translator->translate(err, context, &root);
    catalog.append(std::move(derived));

    // 稳定码条目＋分类/严重自码表（RT-ROBWORK-ERROR＝执行失败/Error）。
    const std::vector<DiagProjectionItem> view = catalog.snapshot(DiagQuery{});
    ASSERT_EQ(view.size(), 2u);
    EXPECT_EQ(view[1].code, "RT-ROBWORK-ERROR");
    EXPECT_EQ(view[1].category, DiagnosticCategory::ExecutionFailed);
    EXPECT_EQ(view[1].severity, DiagnosticSeverity::Error);
    // 根因链接＋作用对象继承（§6.3——转换不得丢失根因/作用对象）。
    EXPECT_EQ(view[1].subject, subject);
}

/// CR-08 处置：未登记类型→DIAG-REGISTRY-UNKNOWN-CODE 兜底条目（保留来源
/// 类型名；非异常体系"消息不可得"不伪造），**不抛**（§9.2 错误行）；
/// DT-REG-4 的 translate 半区（T03 套件覆盖边界补齐）。
TEST_F(ErrorCodeTranslatorTest, DtCr08_UnregisteredTypeFallsBackNoThrow)
{
    const DiagContext context = makeBaseContext();
    const TestOpaqueFailure failure{};
    DiagnosticEntry entry;
    EXPECT_NO_THROW({ entry = m_translator->translate(failure, context); });

    // 兜底码为内置 Dev 级自省码（§4.6 DIAG 行——"红线的红线"）。
    EXPECT_EQ(entry.record.code, "DIAG-REGISTRY-UNKNOWN-CODE");
    EXPECT_EQ(entry.severity, DiagnosticSeverity::Dev);
    // 来源类型名保留（§8.1 规则 2——typeid 拼写，MSVC 下含 "struct " 前缀）。
    EXPECT_NE(entry.record.context.find("TestOpaqueFailure"), std::string::npos);
    // 消息不可得如实登记（不伪造）。
    EXPECT_NE(entry.record.cause.find("消息不可得"), std::string::npos);
}

/// CR-08 补面：异常消息截断（>512 字节截断＋标注——§7.6 摘要口径）；根因
/// 链接在兜底路径同样成立（root 非空→causedBy）。用**未登记阶段 A 清单**的
/// 局部转译器：std::exception 体系规则未注册时，异常类型才落入兜底（§8.3
/// 清单注册后任何 std::exception 派生类型都会命中 RT-ROBWORK-ERROR）。
TEST_F(ErrorCodeTranslatorTest, DtCr08_UnknownExceptionMessageTruncatedAndRootLinked)
{
    StableCodeRegistry bareRegistry;
    sealBuiltinRegistry(bareRegistry);
    DiagnosticsFactory bareTranslator(bareRegistry, m_clock);   // 无任何转译规则

    DiagCatalog catalog;
    const DiagContext context = makeBaseContext();
    DiagnosticEntry root = bareTranslator.create(
        makeUserRecord("RT-WC-COMPILE-FAILED", ObjectId::generate()), context);
    catalog.append(root);

    const std::string hugeMessage(4096, 'x');
    const TestCompileError err(hugeMessage);
    const DiagnosticEntry entry = bareTranslator.translate(err, context, &root);

    EXPECT_EQ(entry.record.code, "DIAG-REGISTRY-UNKNOWN-CODE");
    EXPECT_LT(entry.record.cause.size(), hugeMessage.size());
    EXPECT_NE(entry.record.cause.find("已截断"), std::string::npos);   // 截断标注
    ASSERT_TRUE(entry.causedBy.has_value());
    EXPECT_EQ(entry.causedBy, root.entryId);   // 兜底条目同样不丢根因（§6.3）
}

/// 转译注册面：阶段 A 清单审计（std::exception→RT-ROBWORK-ERROR——"PRJ/RT/
/// POLICY/EVI/EX 映射就位"的 RT 族证据，Factory.hpp 头注就位口径）；同型
/// 重复登记 DuplicateCode；未注册目标码 CodeUnknown；seal 后登记 Usage。
TEST_F(ErrorCodeTranslatorTest, DtCr08_RegistrationGuardsAndStageAAudit)
{
    // 阶段 A 清单就位（§8.3——RT 族兜底规则；审计面＝registeredTranslations）。
    const auto audit = m_translator->registeredTranslations();
    ASSERT_EQ(audit.size(), 1u);
    EXPECT_EQ(audit[0].second, "RT-ROBWORK-ERROR");

    // 同错误类型重复登记 → DuplicateCode（不覆盖不静默）。
    expectThrowsWithCode(
        [&] {
            m_translator->registerTranslation(typeid(std::exception), "RT-WC-COMPILE-FAILED");
        },
        DiagnosticsErrorCode::DuplicateCode, "重复登记转译规则");

    // 未注册目标码 → CodeUnknown（转译产物必须落在登记码面内）。
    expectThrowsWithCode(
        [&] {
            m_translator->registerTranslation(typeid(TestCompileError), "RT-NOT-REGISTERED");
        },
        DiagnosticsErrorCode::CodeUnknown, "目标码未注册");

    // seal 后登记 → Usage（"运行期 registerTranslation"违约面）。
    m_translator->seal();
    expectThrowsWithCode(
        [&] {
            m_translator->registerTranslation(typeid(TestCompileError), "RT-WC-COMPILE-FAILED");
        },
        DiagnosticsErrorCode::Usage, "运行期登记转译规则");
}

/// rootless 转译用户级目标码 → SubjectMissing（调用方须提供根因条目——
/// subject 边界对转译路径的一致执行；Factory.hpp translate 注释契约）。
TEST_F(ErrorCodeTranslatorTest, DtCr08_RootlessUserLevelTranslationRejected)
{
    const TestCompileError err("bad link");
    expectThrowsWithCode(
        [&] { (void)m_translator->translate(err, makeBaseContext()); },
        DiagnosticsErrorCode::SubjectMissing, "rootless 用户级转译");
}

// =====================================================================
// P-DIAG-5／P-DIAG-1——统一 sink 与 core 基线（acceptance 3/4 主体）
// =====================================================================

/// 捕获型开发日志替身（reportDev 路由观测——IDevLogSink 窄接口 §9.7）。
struct CapturingDevLog final : IDevLogSink {
    std::vector<std::pair<std::string, std::string>> lines;   ///< (channel, message)

    void logDev(std::string_view channel, std::string message) override
    {
        lines.emplace_back(std::string(channel), std::move(message));
    }
};

/// 目录变更观察者替身（§9.7 订阅通知观测）。
struct RecordingObserver final : IDiagObserver {
    int changes = 0;
    void onCatalogChanged() override { ++changes; }
};

/// P-DIAG-5 处置：DiagnosticsSinkImpl 三方语义——IDiagnosticSink 四方法直通；
/// report＝factory.create＋catalog.append（sourceUnit＝宿主标识，§9.7 尾注）；
/// reportDev＝logDev 路由（Dev 不入目录——§6.2）。同形签名与对端卡逐字一致
/// （project §5.0/execution §3.3——P-PR-6/P-EX-8 统一待详设修订，不私改对端）。
TEST(DiagnosticsSinkShape, DtPdiag5_ReportReportDevShapeCompatible)
{
    StableCodeRegistry registry;
    sealBuiltinRegistry(registry);
    ManualClock clock;
    DiagnosticsFactory factory(registry, clock);
    DiagCatalog catalog;
    CapturingDevLog devLog;
    DiagnosticsSinkImpl sink(factory, catalog, devLog, "project", "store.open");

    // report：用户诊断入目录，上下文锚定宿主标识（§9.7 尾注口径）。
    const ObjectId subject = ObjectId::generate();
    sink.report(core::DiagnosticRecord::make(
        "PRJ-STORE-CORRUPT", subject, {}, {}, std::string("存储损坏"),
        std::string("manifest 校验失败"), std::string("从备份恢复")));
    ASSERT_EQ(catalog.size(), 1u);
    const std::vector<DiagProjectionItem> view = catalog.snapshot(DiagQuery{});
    EXPECT_EQ(view[0].code, "PRJ-STORE-CORRUPT");
    EXPECT_EQ(view[0].context.sourceUnit, "project");
    EXPECT_EQ(view[0].context.sourceInterface, "store.open");

    // reportDev：开发诊断走日志路由，不产生目录条目（Dev 不入目录——§6.2）。
    sink.reportDev("project/store", "事务回滚完成");
    ASSERT_EQ(devLog.lines.size(), 1u);
    EXPECT_EQ(devLog.lines[0].first, "project/store");
    EXPECT_EQ(devLog.lines[0].second, "事务回滚完成");
    EXPECT_EQ(catalog.size(), 1u);

    // IDiagnosticSink 面直通（同一实现体——三方语义，§9.7 尾注）。
    IDiagnosticSink& sinkFace = sink;
    EXPECT_EQ(sinkFace.snapshot(DiagQuery{}).size(), 1u);
}

/// §9.7 append 守卫与目录行为：Dev 条目拒绝（Usage）；空 entryId 拒绝；订阅
/// 通知（追加/去重命中均通知）与退订（RAII）；安全摘要导出（稳定码/级别/
/// 分类/文案键/subject 在场，record 原始文本不在——§8.10 投影边界）。
TEST(DiagnosticsSinkShape, DtPdiag5_CatalogGuardsSubscribeAndSafeSummary)
{
    StableCodeRegistry registry;
    sealBuiltinRegistry(registry);
    ManualClock clock;
    DiagnosticsFactory factory(registry, clock);
    DiagCatalog catalog;

    const DiagnosticEntry entry =
        factory.create(makeUserRecord("RT-INPUT-INVALID", ObjectId::generate()),
                       makeBaseContext());

    // Dev 条目拒绝（§9.7 前置"若 Dev 码误入→Usage"）。
    DiagnosticEntry devEntry = factory.create(makeDevRecord(), makeExecutionContext());
    expectThrowsWithCode([&] { catalog.append(std::move(devEntry)); },
                         DiagnosticsErrorCode::Usage, "Dev 条目入目录");

    // 空 entryId 拒绝（绕过工厂分配器的未登记构造路径）。
    DiagnosticEntry zeroId = entry;
    zeroId.entryId = 0;
    expectThrowsWithCode([&] { catalog.append(zeroId); },
                         DiagnosticsErrorCode::Usage, "空 entryId");

    // 合法追加＋订阅通知（追加/去重命中均通知——§9.7 后置）。
    RecordingObserver observer;
    std::unique_ptr<ISubscription> subscription = catalog.subscribe(observer);
    catalog.append(entry);
    EXPECT_EQ(observer.changes, 1);
    catalog.append(factory.create(makeUserRecord("RT-INPUT-INVALID", *entry.record.subject),
                                  makeBaseContext()));
    EXPECT_EQ(observer.changes, 2);   // 去重命中（occurrences 变化）也通知

    // 退订后不再通知（RAII 句柄——§9.7）。
    subscription.reset();
    catalog.append(factory.create(makeUserRecord("RT-UNIT-MISMATCH", ObjectId::generate()),
                                  makeBaseContext()));
    EXPECT_EQ(observer.changes, 2);

    // 安全摘要导出：稳定码/级别/分类/文案键/subject 在场；record 原始文本不在
    // （§8.10——reporting 消费稳定码＋安全参数；UX-02 不显示内部细节）。行格式
    // ＝位置字段 "code|severity|category|titleKey|subject|occurrences|params"。
    const std::string summary = catalog.exportSafeSummary(DiagQuery{}, 0);
    EXPECT_NE(summary.find("RT-INPUT-INVALID"), std::string::npos);
    EXPECT_NE(summary.find("|error|"), std::string::npos);
    EXPECT_NE(summary.find("|input-invalid|"), std::string::npos);
    EXPECT_NE(summary.find("diag.rt-input-invalid.title"), std::string::npos);
    EXPECT_NE(summary.find("obj-"), std::string::npos);
    EXPECT_EQ(summary.find("越域"), std::string::npos);   // record 文本不进摘要
    // maxEntries 限幅（3 条目录 vs 1 条限幅——导出量受控）。
    const std::string limited = catalog.exportSafeSummary(DiagQuery{}, 1);
    EXPECT_EQ(std::count(limited.begin(), limited.end(), '\n'), 1);
    EXPECT_NE(limited, summary);
}

/// P-DIAG-1 处置：DiagnosticEntry 信封内嵌 core::DiagnosticRecord——类型面
/// 以 core.md v0.1 为基线（编译期钉住）；core 冻结 diff 后增量同步（不私改
/// core——类型断言即契约基线证据）。
TEST(DiagnosticsEnvelopeBaseline, DtPdiag1_EntryEmbedsCoreRecordBaseline)
{
    // 信封 record 字段类型＝core 契约类型（P-DIAG-1——基线编译期证据）。
    static_assert(std::is_same_v<decltype(DiagnosticEntry::record),
                                 core::DiagnosticRecord>,
                  "DiagnosticEntry.record 须为 core::DiagnosticRecord（SA-12 内嵌不扩充）");
    static_assert(std::is_same_v<decltype(DiagContext::task),
                                 std::optional<core::TaskIdentity>>,
                  "DiagContext.task 须为 core::TaskIdentity（TASK-03 五元组）");
    static_assert(std::is_same_v<decltype(DiagContext::snapshotId),
                                 std::optional<core::ContentIdentity>>,
                  "DiagContext.snapshotId 须为 core::ContentIdentity（P-DIAG-1 基线）");
    SUCCEED() << "信封内嵌 core 契约类型面与 core.md v0.1 基线一致（编译期钉住）";
}

}  // namespace
