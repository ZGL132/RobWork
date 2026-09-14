/**
 * @file   CatalogHostTraceStubTest.cpp
 * @brief  宿主留痕形状与持久化诊断不可改写——envelope/CommandRecord 对接桩
 *         用例组（DT-LIFE-5＋宿主留痕形状冻结）。
 *
 * 设计依据：
 *   - units/diagnostics.md §10 DT-LIFE-5 行（持久化诊断＋Superseded 投影→
 *     重读→记录字节不变，观测点"摘要比对"）、§6.1 生命周期图（"持久化部分
 *     随宿主不可变——当前性变化不改写任何已持久化诊断〔CON-02〕"）、§6.2
 *     （"最终结果中的诊断"行：目录条目与 envelope 内记录**同源**＝同一
 *     record 值、entryId 不入 envelope——会话态身份不持久化；"诊断与历史
 *     结果的绑定"行：持久化诊断的绑定＝宿主对象身份〔envelope 的 task/
 *     snapshotId；CommandRecord 的 revision〕；"诊断对象不能因当前性变化
 *     被改写"行：Superseded 是 evidence 对结果的投影，不传播为对诊断记录
 *     的修改；更新＝新条目＋supersedes，旧条目保留不删）、§4.2 序列化行
 *     （DiagnosticEntry 本体不序列化；record 部分随宿主对象持久化，编码归
 *     宿主所有者——project/evidence canonical 编码）
 *   - 需求 CON-02（完整性/当前性/工程判定正交——当前性投影不改写历史）、
 *     CON-05（内容寻址——摘要比对）、PA-2（不可变历史）、TASK-02（取消/
 *     失败/中断不得进正式报告——Dev 码不进历史的宿主侧印证）
 *   - 任务契约 tasks/foundation/DIAG-T09.json acceptance 1（DT-LIFE-5 半区）
 *     与 acceptance 2（宿主留痕形状冻结：诊断随宿主持久化的编码归宿主所有者
 *     ——envelope canonical 归 evidence、CommandRecord 归 project；本任务只
 *     交付对接数据形状，不私改宿主编码）——逐条自证。
 *
 * P-DIAG-8 处置（同 ConfirmableProjectStubTest 先例）：evidence/project 尚未
 * 链接 diagnostics，本套件以**桩＋契约夹具**验证宿主留痕的对接数据形状，不
 * 私改对端单元的链接与头文件（R-1/R-2）；桩编码器定义在测试侧——这本身即
 * acceptance 2 的结构性证明：diagnostics 交付的持久化载荷是 core::
 * DiagnosticRecord 纯值（core 契约，§7.8 冻结），宿主编码不需要 diagnostics
 * 提供任何序列化设施（无 envelope/CommandRecord 编码 API 随本任务新增）。
 *
 * 线程约束：全部用例单线程（桩流程同步展开）。
 */

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <sdurws/ird/core/DiagData.hpp>
#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/diagnostics/Catalog.hpp>
#include <sdurws/ird/diagnostics/DiagCodes.hpp>
#include <sdurws/ird/diagnostics/Errors.hpp>
#include <sdurws/ird/diagnostics/Factory.hpp>

namespace {

using namespace sdurws::ird::diagnostics;
namespace core = sdurws::ird::core;
using sdurws::ird::core::AttemptId;
using sdurws::ird::core::BranchId;
using sdurws::ird::core::ContentDigester;
using sdurws::ird::core::Digest256;
using sdurws::ird::core::ObjectId;
using sdurws::ird::core::ProjectId;
using sdurws::ird::core::RevisionId;
using sdurws::ird::core::RunId;
using sdurws::ird::core::TaskIdentity;

// ---------------------------------------------------------------------
// 夹具辅助（与单元套件同风格——自持不共享）。
// ---------------------------------------------------------------------

/// 确定性测试时钟（§4.2 IClock 注释——testkit ManualClock 兼容形态）。
class ManualClock final : public IClock {
public:
    std::chrono::system_clock::time_point nowUtc() const override { return m_now; }

private:
    std::chrono::system_clock::time_point m_now{std::chrono::seconds{4000000}};
};

/// 合法五元组（TASK-03——五字段全 isValid）。
TaskIdentity makeTaskIdentity(const char* attemptToken)
{
    TaskIdentity task;
    task.project = ProjectId::generate();
    task.branch = BranchId::generate();
    task.revision = RevisionId::generate();
    task.run = RunId::generate();
    task.attempt = AttemptId::fromCanonical(attemptToken);
    return task;
}

/// 用户级记录（正例基线；非比较型——comparison 空，桩编码按"无三要素"分支）。
core::DiagnosticRecord makeUserRecord(const std::string& code, ObjectId subject)
{
    return core::DiagnosticRecord::make(
        code, subject, std::string("joint_3"), std::string("Robot.joint_3"),
        std::string("宿主留痕用例注入"), std::string("注入原因"),
        std::string("注入建议动作"));
}

/// execution 域上下文（EX 码 create 前置：合法 task——§8.10）。
DiagContext makeExecutionContext(const TaskIdentity& task)
{
    DiagContext context;
    context.sourceUnit = "execution";
    context.sourceInterface = "channel.error-report";
    context.task = task;
    return context;
}

/// 命令路径上下文（project 域——§6.2"诊断与历史结果的绑定"行的 revision 载体；
/// params 键集与 PRJ-LOCK-HELD 的 paramSchema 一致——§4.5 占位校验）。
DiagContext makeCommandContext(const TaskIdentity& task, const RevisionId& revision)
{
    DiagContext context;
    context.sourceUnit = "project";
    context.sourceInterface = "commands.submit";
    context.project = task.project;
    context.branch = task.branch;
    context.revision = revision;
    context.task = task;
    context.commandType = std::string("apply-policy");
    context.params = {{"pid", "policy-v3"}, {"host", "l5-session"}};
    return context;
}

// ---------------------------------------------------------------------
// 宿主留痕桩（编码器定义在测试侧＝acceptance 2 的结构性证明——见文件头）。
// ---------------------------------------------------------------------

/**
 * @brief evidence 所有者桩：ResultEnvelope.diagnostics 的 canonical 编码与
 *        当前性投影（§6.2"最终结果中的诊断"行）。
 *
 * 桩语义（只复刻 evidence 卡的对接形状，不充当实现）：
 *   - persistEnvelope：终结收口——持久化"正式诊断 record 值全量"（目录条目
 *     与 envelope 内记录同源；entryId 不入 envelope）。Dev 码拒绝（§6.1：
 *     historical=true 的码才允许——Dev 不进历史）；
 *   - markSuperseded：当前性变化（Current→Superseded）——只改投影标记，
 *     **不触碰**已持久化字节（CON-02 的宿主侧纪律）；
 *   - persistedRecords：从"磁盘"（内存字节账本）回读的记录值——重读路径。
 */
class StubEvidenceEnvelopeStore {
public:
    /// @brief 终结收口持久化（canonical 编码：确定性字段拼接——宿主所有）。
    /// @return true＝持久化成功；false＝拒绝（Dev 码不进历史）
    bool persistEnvelope(const TaskIdentity& task,
                         const std::vector<DiagnosticEntry>& entries)
    {
        // historical=true 才允许：Dev 码不入宿主持久化（§6.1 生命周期图）。
        for (const DiagnosticEntry& entry : entries) {
            if (entry.severity == DiagnosticSeverity::Dev) {
                return false;   // 拒绝且不写字节（半截持久化比拒绝更糟）
            }
        }
        // canonical 编码（evidence 所有——本桩只复刻"确定性字节"的形状义务）：
        // envelope 头＝task 五元组规范文本；诊断段＝逐条 record 值段。
        std::string bytes = "envelope|task=" + taskCanonical(task);
        for (const DiagnosticEntry& entry : entries) {
            bytes += "|diag=";
            bytes += recordCanonical(entry.record);
        }
        m_bytes = bytes;
        m_records.clear();
        for (const DiagnosticEntry& entry : entries) {
            m_records.push_back(entry.record);   // 同源 record 值（无 entryId）
        }
        return true;
    }

    /// 当前性投影（Current→Superseded）：**只置标记，不改持久化字节**——
    /// Superseded 不传播为对诊断记录的修改（CON-02/§6.2 冻结行）。
    void markSuperseded(const AttemptId& attempt) { m_superseded = attempt; }

    /// 是否处于 Superseded 投影（重读场景的前置）。
    bool isSuperseded(const AttemptId& attempt) const
    {
        return m_superseded.has_value() && *m_superseded == attempt;
    }

    /// 从持久化字节账本回读的记录值（"重读"路径——与写入同一字节源）。
    const std::vector<core::DiagnosticRecord>& persistedRecords() const
    {
        return m_records;
    }

    /// 持久化 canonical 字节（重编码比对与摘要比对的原料）。
    const std::string& bytes() const { return m_bytes; }

private:
    /// record 段 canonical 编码（桩内实现——编码归属宿主，见文件头）。
    static std::string recordCanonical(const core::DiagnosticRecord& record)
    {
        // 字段定序拼接（\x1f 作字段分隔——值域不含控制字符的工程约定）；
        // comparison 缺省＝"-"（本套件注入均为非比较型）。
        std::string out = record.code;
        out += '\x1f';
        out += record.subject.has_value() ? record.subject->toCanonical() : "-";
        out += '\x1f';
        out += record.localName.value_or("-");
        out += '\x1f';
        out += record.runtimeName.value_or("-");
        out += '\x1f';
        out += record.context;
        out += '\x1f';
        out += record.cause;
        out += '\x1f';
        out += record.recommendedAction;
        out += '\x1f';
        out += record.comparison.has_value() ? "cmp" : "-";
        return out;
    }

    /// 五元组规范文本（桩内拼接——与 EntryDetail 私有 taskScopeId 同形但独立，
    /// 桩不依赖产品私有头）。
    static std::string taskCanonical(const TaskIdentity& task)
    {
        return task.project.toCanonical() + "|" + task.branch.toCanonical() + "|"
             + task.revision.toCanonical() + "|" + task.run.toCanonical() + "|"
             + task.attempt.toCanonical();
    }

    std::string m_bytes;                                    ///< 持久化字节账本
    std::vector<core::DiagnosticRecord> m_records;          ///< 回读记录值
    std::optional<AttemptId> m_superseded;                  ///< 当前性投影标记
};

/**
 * @brief project 所有者桩：CommandRecord 的诊断留痕形状（§6.2"诊断与历史
 *        结果的绑定"行——持久化诊断的绑定＝CommandRecord 的 revision）。
 */
class StubProjectCommandStore {
public:
    /// 命令留痕持久化（绑定字段＝修订身份；记录值同源拷贝）。
    void persistCommandRecord(const RevisionId& revision,
                              const std::vector<DiagnosticEntry>& entries)
    {
        m_revision = revision;
        m_records.clear();
        for (const DiagnosticEntry& entry : entries) {
            m_records.push_back(entry.record);
        }
    }

    const RevisionId& boundRevision() const { return m_revision; }
    const std::vector<core::DiagnosticRecord>& records() const { return m_records; }

private:
    RevisionId m_revision;   ///< 绑定身份（宿主对象身份——非 entryId）
    std::vector<core::DiagnosticRecord> m_records;
};

/// SHA-256 摘要（CON-05——摘要比对观测点；CR-02：只经 core ContentDigester）。
Digest256 digestOf(const std::string& bytes)
{
    ContentDigester digester;
    digester.update(bytes.data(), bytes.size());
    return digester.finalize();
}

/// 投影项的呈现字段等值（DiagProjectionItem 无 operator==——逐字段比对；
/// 字段集＝§9.7 契约面）。
bool projectionEqual(const DiagProjectionItem& a, const DiagProjectionItem& b)
{
    return a.entryId == b.entryId && a.code == b.code && a.titleKey == b.titleKey
        && a.detailKey == b.detailKey && a.category == b.category
        && a.severity == b.severity && a.subject == b.subject
        && a.localName == b.localName && a.runtimeName == b.runtimeName
        && a.actionKind == b.actionKind && a.comparison == b.comparison
        && a.context.project == b.context.project
        && a.context.branch == b.context.branch
        && a.context.revision == b.context.revision
        && a.context.task == b.context.task
        && a.context.snapshotId == b.context.snapshotId
        && a.context.sliceId == b.context.sliceId
        && a.context.policyContentId == b.context.policyContentId
        && a.context.commandType == b.context.commandType
        && a.context.commandDigest == b.context.commandDigest
        && a.context.sourceUnit == b.context.sourceUnit
        && a.context.sourceInterface == b.context.sourceInterface
        && a.context.contractVersions == b.context.contractVersions
        && a.context.params == b.context.params
        && a.occurrences == b.occurrences
        && a.aggregatedUnder == b.aggregatedUnder;
}

// =====================================================================
// 测试组
// =====================================================================

class DiagHostTraceStub : public ::testing::Test {
protected:
    void SetUp() override
    {
        registerBuiltinCodes(m_registry);
        m_registry.seal();
        m_factory = std::make_unique<DiagnosticsFactory>(m_registry, m_clock);
    }

    StableCodeRegistry m_registry;   ///< 码表（每用例独立）
    ManualClock m_clock;             ///< 确定性时钟
    std::unique_ptr<DiagnosticsFactory> m_factory;
};

/**
 * DT-LIFE-5：持久化诊断不因当前性改写——任务终结收口持久化后，当前性投影
 * （Current→Superseded）不传播为对诊断记录的修改：重读记录逐值不变、重编码
 * 字节逐字节不变、SHA-256 摘要不变（CON-02/CON-05；观测点"摘要比对"）。
 */
TEST_F(DiagHostTraceStub, DtLife5_PersistedBytesInvariantUnderCurrentnessProjection)
{
    DiagCatalog catalog;
    const TaskIdentity task = makeTaskIdentity("att-1");

    // 运行期诊断入目录（同源值在 append 前另存——目录不改写条目的对照面）。
    DiagnosticEntry crash = m_factory->create(
        makeUserRecord("EX-WORKER-CRASHED", ObjectId::generate()),
        makeExecutionContext(task));
    const core::DiagnosticRecord crashBefore = crash.record;
    DiagnosticEntry rejected = m_factory->create(
        makeUserRecord("EX-TASK-REJECTED", ObjectId::generate()),
        makeExecutionContext(task));
    rejected.causedBy = crash.entryId;
    const core::DiagnosticRecord rejectedBefore = rejected.record;
    catalog.append(std::move(crash));
    catalog.append(std::move(rejected));

    // 终结收口：envelope 持久化（record 值全量——同源）。
    StubEvidenceEnvelopeStore store;
    // 条目值已在 append 时被搬移——从目录快照取同源 record（§6.2"目录条目
    // 与 envelope 内记录同源"）；快照的 context/record 经投影携带……投影不
    // 含 record 全文（呈现键化）——同源值取法＝append 前另存的对照值：
    // 桩持久化输入用"另存记录重组的条目值"（工厂同一 record 的确定性重造：
    // 同输入同输出——NFR-COR-01，P-DIAG-1 值语义）。
    std::vector<DiagnosticEntry> terminalSet;
    DiagnosticEntry crashView = m_factory->create(crashBefore, makeExecutionContext(task));
    DiagnosticEntry rejectedView =
        m_factory->create(rejectedBefore, makeExecutionContext(task));
    terminalSet.push_back(std::move(crashView));
    terminalSet.push_back(std::move(rejectedView));
    ASSERT_TRUE(store.persistEnvelope(task, terminalSet));
    const std::string bytesBefore = store.bytes();
    const Digest256 digestBefore = digestOf(bytesBefore);

    // 当前性变化：重跑产生新尝试，旧尝试被标记 Superseded（evidence 对结果
    // 的投影——不改写任何已持久化诊断）。
    const AttemptId oldAttempt = task.attempt;
    store.markSuperseded(oldAttempt);
    ASSERT_TRUE(store.isSuperseded(oldAttempt));

    // 重读：记录逐值不变（对照 append 前另存值），重编码字节不变，摘要不变。
    const std::vector<core::DiagnosticRecord>& reread = store.persistedRecords();
    ASSERT_EQ(reread.size(), 2u);
    EXPECT_EQ(reread[0], crashBefore) << "重读记录必须与收口值逐值一致（CON-02）";
    EXPECT_EQ(reread[1], rejectedBefore);
    EXPECT_EQ(store.bytes(), bytesBefore) << "持久化字节不得因当前性变化而改写";
    EXPECT_EQ(digestOf(store.bytes()), digestBefore) << "摘要比对（DT-LIFE-5 观测点）";

    // 目录半区：当前性投影后目录条目照常在场、值不变（会话内可见性不受
    // 当前性影响——§6.2"失败/取消后的诊断保留"的补充面）。
    DiagQuery byTask;
    byTask.task = task;
    EXPECT_EQ(catalog.snapshot(byTask).size(), 2u);
}

/**
 * DT-LIFE-5：目录条目永不改写；"更新"＝新条目＋supersedes——旧条目保留不删
 * （不可变；§6.3 supersedes 语义）。取代条目须指向已存在条目（append 校验），
 * 指向未知条目 Usage 拒绝（链接真实性由目录承载）。
 */
TEST_F(DiagHostTraceStub, DtLife5_UpdateIsSupersedingEntryAndOldEntryUntouched)
{
    DiagCatalog catalog;
    // 同一问题（同码同对象）在两次尝试中先后发现：att-1 首发现、att-2 重试
    // 后再发现（五元组含 attempt——去重键不同，绝不折叠为计数——§6.4）。
    const TaskIdentity first = makeTaskIdentity("att-1");
    TaskIdentity retry = first;
    retry.attempt = AttemptId::fromCanonical("att-2");

    DiagnosticEntry stale1 = m_factory->create(
        makeUserRecord("EX-SNAPSHOT-STALE", ObjectId::generate()),
        makeExecutionContext(first));
    const DiagEntryId stale1Id = stale1.entryId;
    catalog.append(std::move(stale1));

    // 首发现条目的呈现快照（改写对照基线）。
    DiagQuery byFirst;
    byFirst.task = first;
    const std::vector<DiagProjectionItem> before = catalog.snapshot(byFirst);
    ASSERT_EQ(before.size(), 1u);

    // 重试再发现：新条目 supersedes 旧条目（旧条目保留不删——不可变）。
    DiagnosticEntry stale2 = m_factory->create(
        makeUserRecord("EX-SNAPSHOT-STALE", before[0].subject.value()),
        makeExecutionContext(retry));
    stale2.supersedes = stale1Id;
    catalog.append(std::move(stale2));   // 链接指向已存在条目——接受

    // 旧条目逐字段原样（无任何改写路径——投影级对照）；两代条目并存。
    const std::vector<DiagProjectionItem> after = catalog.snapshot(byFirst);
    ASSERT_EQ(after.size(), 1u) << "按 att-1 任务查询仍恰一条（旧条目未被改写/删除）";
    EXPECT_TRUE(projectionEqual(before[0], after[0])) << "旧条目呈现字段逐项不变";
    DiagQuery byRetry;
    byRetry.task = retry;
    const std::vector<DiagProjectionItem> retryItems = catalog.snapshot(byRetry);
    ASSERT_EQ(retryItems.size(), 1u) << "取代条目独立成条（新尝试五元组）";
    EXPECT_EQ(retryItems[0].code, "EX-SNAPSHOT-STALE");

    // 链接真实性：supersedes 指向不存在条目 → Usage 拒绝（§6.3 校验的目录
    // 侧承载——取代关系必须是真实链接，不可伪造导航）。换新 subject＝异去
    // 重键（同键会先被去重折叠计数、走不到链接校验——§6.4 顺序语义）。
    DiagnosticEntry bogus = m_factory->create(
        makeUserRecord("EX-SNAPSHOT-STALE", ObjectId::generate()),
        makeExecutionContext(first));
    bogus.supersedes = 999999;
    try {
        catalog.append(std::move(bogus));
        FAIL() << "supersedes 指向不存在条目应被拒绝";
    } catch (const DiagnosticsError& e) {
        EXPECT_EQ(e.code(), DiagnosticsErrorCode::Usage);
    }
}

/**
 * 宿主留痕形状冻结（acceptance 2）：envelope 载荷＝record 值全量——
 * 会话态身份（entryId）不入持久化形状（两个"会话"的工厂各自分配不同
 * entryId，同一 record 值集产出逐字节相同的宿主编码）；编码器由宿主侧
 * （本桩）定义——diagnostics 不提供宿主编码设施。
 */
TEST_F(DiagHostTraceStub, HostTrace_EnvelopeShapeIsRecordValuesWithoutSessionIdentity)
{
    // 两个独立工厂＝两个会话（entryId 分配器互不相干——同 record 值、异
    // 会话身份）。
    DiagnosticsFactory sessionA(m_registry, m_clock);
    DiagnosticsFactory sessionB(m_registry, m_clock);
    const TaskIdentity task = makeTaskIdentity("att-1");
    const core::DiagnosticRecord shared =
        makeUserRecord("EX-WORKER-CRASHED", ObjectId::generate());

    StubEvidenceEnvelopeStore storeA;
    StubEvidenceEnvelopeStore storeB;
    // 前置条目推进 sessionA 的 entryId 分配器——两会话对同一 record 值分配出
    // **不同的**会话身份（前置断言在 move 前捕获），字节仍须逐字节一致。
    DiagnosticEntry warmup = sessionA.create(shared, makeExecutionContext(task));
    const DiagEntryId warmupId = warmup.entryId;
    DiagnosticEntry entryA = sessionA.create(shared, makeExecutionContext(task));
    const DiagEntryId idA = entryA.entryId;
    storeA.persistEnvelope(task, {std::move(entryA)});
    DiagnosticEntry entryB = sessionB.create(shared, makeExecutionContext(task));
    const DiagEntryId idB = entryB.entryId;
    storeB.persistEnvelope(task, {std::move(entryB)});
    EXPECT_NE(warmupId, 0u) << "前置条目分配自检";
    EXPECT_NE(idA, idB)
        << "前置自检：两会话的会话身份确定不同（entryId 分配器互不相干）";

    // 会话身份不漏入持久化形状：字节逐字节一致（若编码掺入 entryId，两
    // 会话的字节必不相同——反例钉住"entryId 不入 envelope"）。
    EXPECT_EQ(storeA.bytes(), storeB.bytes())
        << "宿主编码只由 record 值决定（会话 entryId 不入形状——§6.2）";
    EXPECT_EQ(digestOf(storeA.bytes()), digestOf(storeB.bytes()));
    ASSERT_EQ(storeA.persistedRecords().size(), 1u);
    EXPECT_EQ(storeA.persistedRecords()[0], shared) << "同源 record 值";
}

/**
 * 宿主留痕形状冻结（acceptance 2）：CommandRecord 的绑定＝修订身份（宿主
 * 对象身份）——不是 entryId（会话态身份不入绑定）；记录值同源。
 */
TEST_F(DiagHostTraceStub, HostTrace_CommandRecordBindsRevisionNotEntryId)
{
    DiagCatalog catalog;
    const TaskIdentity task = makeTaskIdentity("att-1");
    const RevisionId revision = RevisionId::generate();

    // 命令路径诊断（project 域——命令上下文携带 revision；PRJ-LOCK-HELD 的
    // paramSchema＝["pid","host"]——§4.5 占位一致校验要求 params 键集恰同）。
    DiagnosticEntry lockHeld = m_factory->create(
        makeUserRecord("PRJ-LOCK-HELD", ObjectId::generate()),
        makeCommandContext(task, revision));
    const core::DiagnosticRecord recordBefore = lockHeld.record;
    catalog.append(std::move(lockHeld));

    // 命令留痕持久化（project 所有者——桩按 §6.7 形状承载）。
    StubProjectCommandStore store;
    // 同 DT-LIFE-5 用例的同源取法：工厂对同一 record 值的确定性重造。
    DiagnosticEntry view = m_factory->create(recordBefore, makeCommandContext(task, revision));
    std::vector<DiagnosticEntry> trace;
    trace.push_back(std::move(view));
    store.persistCommandRecord(revision, trace);

    // 绑定字段＝修订身份（非 entryId）；记录值同源。
    EXPECT_EQ(store.boundRevision(), revision) << "绑定＝宿主对象身份（§6.2）";
    ASSERT_EQ(store.records().size(), 1u);
    EXPECT_EQ(store.records()[0], recordBefore) << "留痕记录同源（不改写）";
}

/**
 * 宿主留痕形状冻结（acceptance 2/TASK-02）：Dev 码不进历史——宿主持久化
 * 接口拒绝 Dev 级记录（§6.1"historical=true 的码才允许——Dev 码不进历史"
 * 的宿主侧闸门；临时诊断只走开发日志）。
 */
TEST_F(DiagHostTraceStub, HostTrace_DevCodesNeverEnterHostPersistence)
{
    // Dev 级码（EX-CHANNEL-PROTOCOL-ERROR）可经工厂创建（subject 可空——
    // §4.2 合法实例 2），但不得进入宿主持久化。
    const TaskIdentity task = makeTaskIdentity("att-1");
    DiagnosticEntry dev = m_factory->create(
        core::DiagnosticRecord::make(
            "EX-CHANNEL-PROTOCOL-ERROR", {}, {}, {}, std::string("通道帧序断裂"),
            std::string("帧序号回退"), std::string("检查回传批次序号")),
        makeExecutionContext(task));
    EXPECT_EQ(dev.severity, DiagnosticSeverity::Dev);

    StubEvidenceEnvelopeStore store;
    std::vector<DiagnosticEntry> terminal;
    terminal.push_back(std::move(dev));
    EXPECT_FALSE(store.persistEnvelope(task, terminal))
        << "宿主持久化必须拒绝 Dev 码（不进历史——TASK-02/§6.1）";
    EXPECT_TRUE(store.bytes().empty()) << "拒绝路径不得写入半截字节";
}

}  // namespace
