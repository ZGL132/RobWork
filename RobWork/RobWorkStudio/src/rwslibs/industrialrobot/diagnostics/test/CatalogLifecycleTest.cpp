/**
 * @file   CatalogLifecycleTest.cpp
 * @brief  诊断目录生命周期用例组（DT-LIFE-1/2/3＋P-DIAG-7）——容量护栏与
 *         分级淘汰、失败/取消后诊断保留、归档失败报告与关闭不等待。
 *
 * 设计依据：
 *   - units/diagnostics.md §10 DT-LIFE-1/DT-LIFE-2/DT-LIFE-3 行（§6.1/§6.2
 *     生命周期规则的注入观测面——§11 DIAG-T09 行验证方式）、§6.2（目录
 *     清理策略：容量上限默认 10,000 条可配、超限淘汰"已消费且非
 *     Warning/Error 的最旧 Info 条目"、Warning/Error 与活动任务关联不淘汰、
 *     DIAG-CATALOG-OVERFLOW 开发诊断登记溢出事实、失败/取消保留、归档失败
 *     EX-ARCHIVE-FAILED 用户可见、关闭无阻塞点）、§14.1 D-16/D-17（正常
 *     取消非错误；容量护栏工程默认）、§14.3 P-DIAG-7（容量默认 10,000 为
 *     可配工程默认，WP-23 校准，不作为需求语义）
 *   - 需求 NFR-REL-03（失败/取消后诊断保留——终结原因必附）、UX-03（正常
 *     取消非错误）、TASK-02（取消/失败/中断分类区分）、CON-02（清理永不
 *     触碰已持久化诊断——本套件钉目录半区，持久化半区见
 *     CatalogHostTraceStubTest）、PM-03（关闭不因非关键诊断等待）
 *   - 任务契约 tasks/foundation/DIAG-T09.json acceptance 1（DT-LIFE-1~3/5
 *     用例通过）与 acceptance 3（P-DIAG-7 处置）——逐条自证；
 *   - 用例名前缀 DtLife×/DtPdiag7 ＝矩阵行号/陷阱号，与 ird-test-report.json
 *     的 trace 追溯字段呼应（AGENTS.md §4.2 验证留痕）。
 *
 * 线程约束：全部用例单线程（容量护栏的并发面由目录互斥承载——Catalog.hpp
 * 线程注释；多线程交错属集成观测面，§10 未设行，不私建）。
 * DT-LIFE-3 的"日志仍在/关闭时限"半区使用真实 LoggingPipeline＋FileLogFileOps
 * （临时目录——LoggingTest 同款自持纪律，T-1 不链 testkit）。
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <sdurws/ird/core/DiagData.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/diagnostics/Catalog.hpp>
#include <sdurws/ird/diagnostics/DiagCodes.hpp>
#include <sdurws/ird/diagnostics/Errors.hpp>
#include <sdurws/ird/diagnostics/Factory.hpp>
#include <sdurws/ird/diagnostics/Logging.hpp>

namespace {

using namespace sdurws::ird::diagnostics;
// 测试文件位于全局匿名 ns：`core` 是 sdurws::ird 的成员，using-directive 不
// 引入兄弟命名空间——以别名使 core::X 限定名可见（既有套件同款处理面）。
namespace core = sdurws::ird::core;
namespace fs = std::filesystem;
using sdurws::ird::core::AttemptId;
using sdurws::ird::core::BranchId;
using sdurws::ird::core::ObjectId;
using sdurws::ird::core::ProjectId;
using sdurws::ird::core::RevisionId;
using sdurws::ird::core::RunId;
using sdurws::ird::core::TaskIdentity;

// ---------------------------------------------------------------------
// 夹具辅助（与 CatalogFactoryTest 同风格——自持不共享）。
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

/// 确定性测试时钟（§4.2 IClock 注释——testkit ManualClock 兼容形态）。
class ManualClock final : public IClock {
public:
    std::chrono::system_clock::time_point nowUtc() const override { return m_now; }

private:
    std::chrono::system_clock::time_point m_now{std::chrono::seconds{3000000}};
};

/// 组装并 seal 内置全量注册表（工厂按"运行期只读"消费）。
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
        std::string("生命周期用例注入"), std::string("注入原因"),
        std::string("注入建议动作"));
}

/// runtime 域上下文基线（sourceUnit/sourceInterface 必填——§4.2 token 边界）。
DiagContext makeRuntimeContext()
{
    DiagContext context;
    context.sourceUnit = "runtime";
    context.sourceInterface = "compile.workcell";
    return context;
}

/// execution 域上下文（EX 域码 create 前置：合法 task 五元组——§8.10）。
DiagContext makeExecutionContext(const TaskIdentity& task)
{
    DiagContext context;
    context.sourceUnit = "execution";
    context.sourceInterface = "channel.error-report";
    context.task = task;
    return context;
}

/// 记录型开发日志替身（IDevLogSink 窄接口——捕获溢出登记的通道/消息原文）。
class RecordingDevSink final : public IDevLogSink {
public:
    void logDev(std::string_view channel, std::string message) override
    {
        lines.emplace_back(std::string(channel), std::move(message));
    }

    /// (channel, message) 对清单（logDev 即时记录——单线程观测面）。
    std::vector<std::pair<std::string, std::string>> lines;
};

/// 计数观察者（§9.7 订阅面——append/去重命中后的变更通知次数观测）。
class CountingObserver final : public IDiagObserver {
public:
    void onCatalogChanged() override { ++changes; }
    int changes = 0;
};

// =====================================================================
// DT-LIFE-1／P-DIAG-7——容量护栏与分级淘汰（acceptance 1/3）
// =====================================================================

class DiagCatalogLifecycle : public ::testing::Test {
protected:
    void SetUp() override
    {
        sealBuiltinRegistry(m_registry);
        m_factory = std::make_unique<DiagnosticsFactory>(m_registry, m_clock);
    }

    /// 便捷入口：runtime 域用户级条目（码＋新 subject；去重键互异——
    /// DT-DUP-2"不同作用对象绝不同键"，本组用例的每条注入都换 subject）。
    DiagnosticEntry runtimeEntry(const std::string& code)
    {
        return m_factory->create(makeUserRecord(code, ObjectId::generate()),
                                 makeRuntimeContext());
    }

    /// 便捷入口：runtime 域用户级条目＋任务锚定（context.task 在 create 前
    /// 注入——dedupKey/条目字段由工厂按完整上下文一次派生，条目构造后不可
    /// 变，无事后改写路径；§4.2 不可变纪律）。
    DiagnosticEntry runtimeEntryInTask(const std::string& code, const TaskIdentity& task)
    {
        DiagContext context = makeRuntimeContext();
        context.task = task;
        return m_factory->create(makeUserRecord(code, ObjectId::generate()), context);
    }

    /// 便捷入口：execution 域条目（EX 码必带任务五元组——§8.10；causeId
    /// 非 0 时建立 causedBy 根因链——§6.3"指向更早条目"，caller 于 append 前
    /// 赋链与工厂 translate 同机制：链接指向性由目录 append 校验承载）。
    DiagnosticEntry execEntry(const std::string& code, const TaskIdentity& task,
                              DiagEntryId causeId = 0)
    {
        DiagnosticEntry entry =
            m_factory->create(makeUserRecord(code, ObjectId::generate()),
                              makeExecutionContext(task));
        if (causeId != 0) {
            entry.causedBy = causeId;
        }
        return entry;
    }

    /// snapshot({}) 中在场条目 id 集合（容量淘汰断言的观测面）。
    std::vector<DiagEntryId> presentIds(const DiagCatalog& catalog) const
    {
        std::vector<DiagEntryId> ids;
        for (const DiagProjectionItem& item : catalog.snapshot(DiagQuery{})) {
            ids.push_back(item.entryId);
        }
        std::sort(ids.begin(), ids.end());
        return ids;
    }

    /// @brief 断言"期望在场的 id 全在场、期望淘汰的 id 全消失"。
    void expectPresence(const DiagCatalog& catalog, const std::string& what,
                        const std::vector<DiagEntryId>& present,
                        const std::vector<DiagEntryId>& absent) const
    {
        const std::vector<DiagEntryId> ids = presentIds(catalog);
        for (const DiagEntryId id : present) {
            EXPECT_NE(std::find(ids.begin(), ids.end(), id), ids.end())
                << what << "：条目 " << id << " 应保留（在场集合不符）";
        }
        for (const DiagEntryId id : absent) {
            EXPECT_EQ(std::find(ids.begin(), ids.end(), id), ids.end())
                << what << "：条目 " << id << " 应已淘汰（在场集合不符）";
        }
    }

    StableCodeRegistry m_registry;   ///< 码表（每用例独立——互不污染）
    ManualClock m_clock;             ///< 确定性时钟（emittedAtUtc 可断言）
    std::unique_ptr<DiagnosticsFactory> m_factory;
};

/**
 * DT-LIFE-1：分级淘汰主路径——超限时自最旧起只淘汰"已消费 Info"；Warning/
 * Error 与活动任务关联条目保留；溢出事实以 DIAG-CATALOG-OVERFLOW 开发诊断
 * 登记（Dev 通道，不入目录）。观测点：目录计数＋溢出 Dev 诊断（§10 行）。
 */
TEST_F(DiagCatalogLifecycle, DtLife1_TieredEvictionKeepsWarningErrorAndActiveTask)
{
    DiagCatalog catalog;
    RecordingDevSink devSink;
    catalog.attachDevLogSink(&devSink);
    CountingObserver observer;
    const auto subscription = catalog.subscribe(observer);

    // 容量 4（可配工程默认的整值替换——P-DIAG-7 可配置面，WP-23 校准入口）。
    CatalogCapacityConfig config;
    config.enabled = true;
    config.maxEntries = 4;
    catalog.configureCapacity(config);

    // 注入并确认消费：E(Error)、W(Warning)、I1/I2(Info)。全部 markConsumed
    // ——淘汰判据②"已消费"的前提（ui/reporting 消费后的显式确认）。id 在
    // move 前捕获（append 接管条目值）。
    DiagnosticEntry err = runtimeEntry("RT-INPUT-INVALID");       // InputInvalid/Error
    DiagnosticEntry warn = runtimeEntry("RT-CAPABILITY-MISSING"); // ResourceMissing/Warning
    DiagnosticEntry info1 = runtimeEntry("RT-CANCELLED");         // Canceled/Info
    DiagnosticEntry info2 = runtimeEntry("RT-CANCELLED");         // 同码异 subject＝异键
    const DiagEntryId errId = err.entryId;
    const DiagEntryId warnId = warn.entryId;
    const DiagEntryId info1Id = info1.entryId;
    const DiagEntryId info2Id = info2.entryId;
    catalog.append(std::move(err));
    catalog.append(std::move(warn));
    catalog.append(std::move(info1));
    catalog.append(std::move(info2));
    for (const DiagEntryId id : {errId, warnId, info1Id, info2Id}) {
        catalog.markConsumed(id);
    }
    ASSERT_EQ(catalog.size(), 4u);
    EXPECT_EQ(devSink.lines.size(), 0u) << "未达上限不得产生溢出登记";

    // 活动任务关联的 Info 条目（第五条）：追加触发压力——最旧已消费 Info
    // （info1）被淘汰腾位，其余保留（§6.2 分级淘汰）。任务锚定在 create 前
    // 注入（条目不可变纪律——见 runtimeEntryInTask）。
    const TaskIdentity activeTask = makeTaskIdentity();
    catalog.setTaskActive(activeTask, true);
    DiagnosticEntry taskInfo = runtimeEntryInTask("RT-CANCELLED", activeTask);
    const DiagEntryId taskInfoId = taskInfo.entryId;
    catalog.append(std::move(taskInfo));
    catalog.markConsumed(taskInfoId);

    EXPECT_EQ(catalog.size(), 4u) << "淘汰后规模应回到容量上限内";
    expectPresence(catalog, "首轮淘汰", {errId, warnId, info2Id, taskInfoId},
                   {info1Id});
    // 目录内副本的任务保护：快照项携带 context（活动任务关联 Info 在场且
    // 带任务锚定——淘汰判据③的保护面）。
    bool taskInfoProtected = false;
    for (const DiagProjectionItem& item : catalog.snapshot(DiagQuery{})) {
        if (item.code == "RT-CANCELLED" && item.context.task.has_value()
            && *item.context.task == activeTask) {
            taskInfoProtected = true;
        }
    }
    EXPECT_TRUE(taskInfoProtected) << "活动任务关联 Info 应在场且带任务锚定";

    // 溢出事实登记：恰 1 条（本轮压力），通道 diag/catalog，消息含稳定码
    // DIAG-CATALOG-OVERFLOW 与淘汰数（§6.2"开发诊断登记溢出事实"）。
    ASSERT_EQ(devSink.lines.size(), 1u);
    EXPECT_EQ(devSink.lines[0].first, std::string(kCatalogInternalChannel));
    EXPECT_NE(devSink.lines[0].second.find("DIAG-CATALOG-OVERFLOW"), std::string::npos)
        << "溢出登记缺稳定码首 token：" << devSink.lines[0].second;
    EXPECT_NE(devSink.lines[0].second.find("evicted=1"), std::string::npos)
        << "溢出登记应含淘汰数：" << devSink.lines[0].second;

    // 继续追加：活动任务关联的 taskInfo 不淘汰（判据③），再次淘汰最旧已
    // 消费 Info（info2）——Warning/Error 依旧保留（判据①级别保护）。
    DiagnosticEntry info3 = runtimeEntry("RT-CANCELLED");
    const DiagEntryId info3Id = info3.entryId;
    catalog.append(std::move(info3));
    catalog.markConsumed(info3Id);
    EXPECT_EQ(catalog.size(), 4u);
    expectPresence(catalog, "次轮淘汰", {errId, warnId, taskInfoId, info3Id},
                   {info1Id, info2Id});
    ASSERT_EQ(devSink.lines.size(), 2u);
    EXPECT_NE(devSink.lines[1].second.find("evicted=1"), std::string::npos);

    // 变更通知：每次 append 恰一次（§9.7 后置——容量策略执行含在 append
    // 语义内，不额外通知）；6 次注入＝6 次通知。
    EXPECT_EQ(observer.changes, 6);
}

/**
 * DT-LIFE-1：判据②"已消费"——未消费的 Info 即便更旧也不淘汰（淘汰未消费
 * 条目＝丢用户尚未见过的信息）；同容量的已消费 Info 正常淘汰。
 */
TEST_F(DiagCatalogLifecycle, DtLife1_UnconsumedInfoSurvivesDespiteAge)
{
    DiagCatalog catalog;
    CatalogCapacityConfig config;
    config.maxEntries = 2;
    catalog.configureCapacity(config);

    DiagnosticEntry olderUnconsumed = runtimeEntry("RT-CANCELLED");
    const DiagEntryId olderId = olderUnconsumed.entryId;
    catalog.append(std::move(olderUnconsumed));
    // 注意：不 markConsumed——最旧且未消费。

    DiagnosticEntry newerConsumed = runtimeEntry("RT-CANCELLED");
    const DiagEntryId newerId = newerConsumed.entryId;
    catalog.append(std::move(newerConsumed));
    catalog.markConsumed(newerId);
    ASSERT_EQ(catalog.size(), 2u);

    // 第三条注入触发压力：唯一可淘汰候选＝newerConsumed（olderUnconsumed 因
    // 未消费被保留——判据②压过"最旧优先"）。
    DiagnosticEntry probe = runtimeEntry("RT-CAPABILITY-MISSING");
    const DiagEntryId probeId = probe.entryId;
    catalog.append(std::move(probe));

    EXPECT_EQ(catalog.size(), 2u);
    expectPresence(catalog, "未消费保护", {olderId, probeId}, {newerId});
}

/**
 * DT-LIFE-1：链保护与多遍收敛——被幸存条目链接引用的 Info 不淘汰（append
 * 校验"链接指向已存在条目"的不变量不得被淘汰制造悬挂引用，§6.3）；引用者
 * 淘汰后，被引用者在下一次压力中变为可淘汰（引用计数快照逐遍收敛）。
 */
TEST_F(DiagCatalogLifecycle, DtLife1_LinkReferencedInfoProtectedThenReleasable)
{
    DiagCatalog catalog;
    CatalogCapacityConfig config;
    config.maxEntries = 2;
    catalog.configureCapacity(config);

    // 根因（旧）与派生（新）均为已消费 Info，causedBy 链接（§6.3）。
    DiagnosticEntry root = runtimeEntry("RT-CANCELLED");
    const DiagEntryId rootId = root.entryId;
    catalog.append(std::move(root));
    DiagnosticEntry derived = runtimeEntry("RT-CANCELLED");
    const DiagEntryId derivedId = derived.entryId;
    derived.causedBy = rootId;
    catalog.append(std::move(derived));
    catalog.markConsumed(rootId);
    catalog.markConsumed(derivedId);

    // 第一次压力：root 被 derived 引用（链保护），derived 无入链——淘汰
    // derived（"最旧优先"让位于链完整性；引用计数快照语义的直接观测面）。
    DiagnosticEntry probe1 = runtimeEntry("RT-CAPABILITY-MISSING");
    const DiagEntryId probe1Id = probe1.entryId;
    catalog.append(std::move(probe1));
    expectPresence(catalog, "链保护", {rootId, probe1Id}, {derivedId});

    // 第二次压力：derived 已不在目录，root 入链清零——随 probe2 的追加被
    // 淘汰（链释放，引用计数快照收敛的可观测结果）。
    DiagnosticEntry probe2 = runtimeEntry("RT-CANCELLED");
    const DiagEntryId probe2Id = probe2.entryId;
    catalog.append(std::move(probe2));
    catalog.markConsumed(probe2Id);
    // 第三次压力：probe1 为 Warning（级别保护——永不淘汰），唯一可淘汰候选
    // ＝已消费 Info（probe2）——probe2 随 probe3 的追加被淘汰。
    DiagnosticEntry probe3 = runtimeEntry("RT-CANCELLED");
    const DiagEntryId probe3Id = probe3.entryId;
    catalog.append(std::move(probe3));
    EXPECT_EQ(catalog.size(), 2u);
    expectPresence(catalog, "链释放", {probe1Id, probe3Id}, {rootId, probe2Id});
}

/**
 * DT-LIFE-1：软溢出——目录被不可淘汰条目（Warning/Error）占满且无候选时，
 * 新条目仍然追加（护栏不得牺牲证据完整性），并以 retention-protected 标记
 * 登记溢出事实（工程侧据此发现"护栏被保留规则压过"的运行形态）。
 */
TEST_F(DiagCatalogLifecycle, DtLife1_RetentionProtectedSoftOverflowStillAppends)
{
    DiagCatalog catalog;
    RecordingDevSink devSink;
    catalog.attachDevLogSink(&devSink);
    CatalogCapacityConfig config;
    config.maxEntries = 2;
    catalog.configureCapacity(config);

    DiagnosticEntry err1 = runtimeEntry("RT-INPUT-INVALID");
    DiagnosticEntry err2 = runtimeEntry("RT-INPUT-INVALID");
    const DiagEntryId err1Id = err1.entryId;
    const DiagEntryId err2Id = err2.entryId;
    catalog.append(std::move(err1));
    catalog.append(std::move(err2));
    for (const DiagEntryId id : {err1Id, err2Id}) {
        catalog.markConsumed(id);   // 已消费也不淘汰——判据①级别保护优先
    }

    // 第三条 Error：无 Info 候选——软溢出，条目照常入目录（size 超上限）。
    DiagnosticEntry err3 = runtimeEntry("RT-INPUT-INVALID");
    const DiagEntryId err3Id = err3.entryId;
    catalog.append(std::move(err3));
    EXPECT_EQ(catalog.size(), 3u) << "软溢出不得丢弃新条目（证据完整性——§6.4 同精神）";
    expectPresence(catalog, "软溢出保全", {err1Id, err2Id, err3Id}, {});
    ASSERT_EQ(devSink.lines.size(), 1u);
    EXPECT_NE(devSink.lines[0].second.find("DIAG-CATALOG-OVERFLOW"), std::string::npos);
    EXPECT_NE(devSink.lines[0].second.find("retention-protected=1"), std::string::npos)
        << "保留保护溢出应显式标记：" << devSink.lines[0].second;
    EXPECT_NE(devSink.lines[0].second.find("evicted=0"), std::string::npos);
}

/**
 * P-DIAG-7／DT-LIFE-1：容量默认＝可配工程默认两面——①默认配置钉住
 * （enabled＋10,000 条，§14.3 登记值；WP-23 校准不构成需求偏差）；②校验面
 * （0 容量 Usage——关闭须显式 enabled=false）；③关闭护栏＝无淘汰；④缩容
 * 不立即淘汰、由下一次压力推动（淘汰判据需"已消费"事实——宁暂超限不越判据）。
 */
TEST_F(DiagCatalogLifecycle, DtPdiag7_DefaultsPinnedAndConfigurableSurface)
{
    DiagCatalog catalog;

    // ①默认值钉住（P-DIAG-7——与单元卡 §14.3 登记值逐项断言）。
    const CatalogCapacityConfig defaults = catalog.capacityConfig();
    EXPECT_TRUE(defaults.enabled);
    EXPECT_EQ(defaults.maxEntries, 10000u)
        << "目录容量工程默认应为 10,000 条（P-DIAG-7——WP-23 校准不构成需求偏差）";

    // ②0 容量 Usage（配置不变量——CatalogCapacityConfig 注释）。
    CatalogCapacityConfig zero = defaults;
    zero.maxEntries = 0;
    expectThrowsWithCode([&] { catalog.configureCapacity(zero); },
                         DiagnosticsErrorCode::Usage, "maxEntries==0");
    EXPECT_EQ(catalog.capacityConfig().maxEntries, 10000u)
        << "被拒绝的配置不得部分生效";

    // ③关闭护栏（enabled=false）：超"默认上限"注入不淘汰、无溢出登记。
    RecordingDevSink devSink;
    catalog.attachDevLogSink(&devSink);
    CatalogCapacityConfig off;
    off.enabled = false;
    catalog.configureCapacity(off);
    for (int i = 0; i < 12; ++i) {
        catalog.append(runtimeEntry("RT-CANCELLED"));
    }
    EXPECT_EQ(catalog.size(), 12u) << "护栏关闭＝不限容量";
    EXPECT_EQ(devSink.lines.size(), 0u) << "护栏关闭不得产生溢出登记";

    // ④缩容延迟生效：满 4 条已消费 Info 后缩至 2——不立即淘汰；下一次
    // append 压力一次腾到位（追加时 needed＝4+1-2＝3，淘汰 3 条最旧后追加）。
    DiagCatalog shrink;
    CatalogCapacityConfig four;
    four.maxEntries = 4;
    shrink.configureCapacity(four);   // 整值替换工程默认——P-DIAG-7 可配置面
    std::vector<DiagEntryId> ids;
    for (int i = 0; i < 4; ++i) {
        DiagnosticEntry e = runtimeEntry("RT-CANCELLED");
        ids.push_back(e.entryId);
        shrink.append(std::move(e));
        shrink.markConsumed(ids.back());
    }
    ASSERT_EQ(shrink.size(), 4u);
    CatalogCapacityConfig two;
    two.maxEntries = 2;
    shrink.configureCapacity(two);
    EXPECT_EQ(shrink.size(), 4u) << "缩容不立即淘汰（宁暂超限不越已消费判据）";
    DiagnosticEntry trigger = runtimeEntry("RT-CANCELLED");
    shrink.append(std::move(trigger));
    EXPECT_EQ(shrink.size(), 2u) << "压力推动一次腾位至配置内";
    // 最旧 3 条淘汰，最新一条（第 4 条）＋新追加保留——淘汰序＝入目录序。
    expectPresence(shrink, "缩容腾位", {ids[3], trigger.entryId},
                   {ids[0], ids[1], ids[2]});
}

// =====================================================================
// DT-LIFE-2——失败/取消/中断后诊断保留（acceptance 1；NFR-REL-03）
// =====================================================================

/**
 * DT-LIFE-2：失败任务——终结后诊断照常保留于目录（会话内可见），终结原因
 * 诊断必附（EX-TASK-REJECTED causedBy EX-WORKER-CRASHED——§6.3 根因链）；
 * 活动保护注销（任务终结）不触发删除（NFR-REL-03：Failed 任务的诊断保留）。
 */
TEST_F(DiagCatalogLifecycle, DtLife2_FailedTaskDiagnosticsRetainedWithTerminationReason)
{
    DiagCatalog catalog;
    const TaskIdentity task = makeTaskIdentity();
    catalog.setTaskActive(task, true);

    // 运行期诊断：worker 崩溃（根因）→ 任务拒绝（终结原因，causedBy 根因
    // ——§6.2"失败/取消后的诊断保留"行"终结原因诊断必附"的承载形状）。
    DiagnosticEntry crash = execEntry("EX-WORKER-CRASHED", task);
    const DiagEntryId crashId = crash.entryId;
    catalog.append(std::move(crash));
    DiagnosticEntry rejected = execEntry("EX-TASK-REJECTED", task, crashId);
    catalog.append(std::move(rejected));

    // 任务终结（execution/L5 在终态同步注销保护——PA-1；注销不删除条目）。
    catalog.setTaskActive(task, false);

    // 终结后按任务查询：两条俱全，终结原因码在结果集（§10 DT-LIFE-2 观测点
    // "结果集含原因码"），根因链在场（§6.3 不丢根因）。
    DiagQuery byTask;
    byTask.task = task;
    const std::vector<DiagProjectionItem> items = catalog.snapshot(byTask);
    ASSERT_EQ(items.size(), 2u) << "失败任务终结后诊断必须保留（NFR-REL-03）";
    bool hasReason = false;
    bool hasRootCause = false;
    for (const DiagProjectionItem& item : items) {
        if (item.code == "EX-TASK-REJECTED") {
            hasReason = true;
            EXPECT_EQ(item.severity, DiagnosticSeverity::Error) << "终结原因＝Error（用户可见）";
        }
        if (item.code == "EX-WORKER-CRASHED") {
            hasRootCause = true;
        }
    }
    EXPECT_TRUE(hasReason) << "终结原因诊断必附（EX-TASK-REJECTED）";
    EXPECT_TRUE(hasRootCause) << "根因诊断保留（§6.3 链完整）";
}

/**
 * DT-LIFE-2：取消任务——正常取消仅 Info 级任务记录（RT-CANCELLED），不产生
 * 任何 Error 级用户诊断（UX-03/D-16）；记录在终结后保留（任务事实可追溯）。
 * 中断任务（EX-TASK-INTERRUPTED，Info＋可重跑动作族）同样保留——§6.2 行的
 * Failed/Canceled/Interrupted 三态并查。
 */
TEST_F(DiagCatalogLifecycle, DtLife2_CanceledTaskInfoRecordOnlyNoErrorDiagnostics)
{
    DiagCatalog catalog;

    // 取消任务：仅 Info 任务记录（UX-03——正常取消非错误）。任务锚定在
    // create 前注入（条目不可变纪律——runtimeEntryInTask）。
    const TaskIdentity canceled = makeTaskIdentity();
    catalog.setTaskActive(canceled, true);
    DiagnosticEntry cancel = runtimeEntryInTask("RT-CANCELLED", canceled);
    catalog.append(std::move(cancel));

    // 中断任务：Info 级中断记录（§4.4 中断默认 Info——PM-08/15 恢复数据源）。
    const TaskIdentity interrupted = makeTaskIdentity();
    catalog.setTaskActive(interrupted, true);
    DiagnosticEntry interrupt = execEntry("EX-TASK-INTERRUPTED", interrupted);
    catalog.append(std::move(interrupt));

    // 终结（取消/中断完成）——注销保护后照常保留。
    catalog.setTaskActive(canceled, false);
    catalog.setTaskActive(interrupted, false);

    // 取消任务查询：恰一条 Info 记录、零 Error 诊断（正常取消不产生用户
    // 错误诊断——UX-03"不产生错误诊断"的目录面观测）。
    DiagQuery byCanceled;
    byCanceled.task = canceled;
    const std::vector<DiagProjectionItem> cancelItems = catalog.snapshot(byCanceled);
    ASSERT_EQ(cancelItems.size(), 1u) << "取消任务仅 Info 任务记录（UX-03/D-16）";
    EXPECT_EQ(cancelItems[0].code, "RT-CANCELLED");
    EXPECT_EQ(cancelItems[0].severity, DiagnosticSeverity::Info);
    EXPECT_EQ(cancelItems[0].actionKind, "none") << "正常取消无建议动作（§4.4 矩阵）";

    // 中断任务查询：Info 级中断记录保留，动作族＝rerun-interrupted（可重跑）。
    DiagQuery byInterrupted;
    byInterrupted.task = interrupted;
    const std::vector<DiagProjectionItem> interruptItems = catalog.snapshot(byInterrupted);
    ASSERT_EQ(interruptItems.size(), 1u);
    EXPECT_EQ(interruptItems[0].code, "EX-TASK-INTERRUPTED");
    EXPECT_EQ(interruptItems[0].severity, DiagnosticSeverity::Info);
    EXPECT_EQ(interruptItems[0].actionKind, "rerun-interrupted");

    // 全目录无任何 Error 级条目（两任务均为非失败终态——TASK-02 分类区分
    // 在生命周期面的印证）。
    for (const DiagProjectionItem& item : catalog.snapshot(DiagQuery{})) {
        EXPECT_NE(item.severity, DiagnosticSeverity::Error)
            << "正常取消/中断不得产生 Error 诊断（UX-03）：" << item.code;
    }
}

// =====================================================================
// DT-LIFE-3——诊断归档失败报告（acceptance 1；project §10.1 诊断半区）
// =====================================================================

/// DT-LIFE-3 的真实日志管线夹具（LoggingTest 同款自持纪律：临时目录＋
/// FileLogFileOps＋ManualClock——验证"日志仍在/关闭有界"的真实文件半区）。
class DiagCatalogArchiveFailure : public DiagCatalogLifecycle {
protected:
    void SetUp() override
    {
        DiagCatalogLifecycle::SetUp();
        m_dir = makeTempDir();
        m_cfg.directory = m_dir;
        m_pipeline = std::make_unique<LoggingPipeline>(m_clock, m_fileOps);
        m_pipeline->configure(m_cfg);
    }

    void TearDown() override
    {
        m_pipeline.reset();   // 有界排空（析构关闭语义——§9.6）
        std::error_code ec;
        fs::remove_all(m_dir, ec);   // 尽力而为清理——失败不遮蔽用例结论
    }

    static fs::path makeTempDir()
    {
        std::error_code ec;
        static int seq = 0;
        const fs::path dir =
            fs::temp_directory_path(ec) / ("ird-lifetest-" + std::to_string(++seq));
        fs::create_directories(dir, ec);
        return dir;
    }

    /// 读取管线落盘文件全文（Tier 半区断言的观测面）。
    std::string readFile(const char* fileName) const
    {
        std::ifstream in(m_dir / fileName, std::ios::binary);
        std::ostringstream buffer;
        buffer << in.rdbuf();
        return buffer.str();
    }

    fs::path m_dir;                                        ///< 每用例独立临时目录
    LogSinkConfig m_cfg;                                   ///< 默认两级文件名（§7.3）
    FileLogFileOps m_fileOps;                              ///< 真实文件接缝
    std::unique_ptr<LoggingPipeline> m_pipeline;           ///< 真实两级日志管线
};

/**
 * DT-LIFE-3：归档失败路径——归档桩失败后 EX-ARCHIVE-FAILED（Error，用户可见
 * "结果未归档"）随任务终态入目录；目录既有诊断不受影响；日志仍在（容量护栏
 * 的溢出登记经真实管线落盘 Tier-D、Tier-U 无 Dev 行——Dev 不入用户目录）。
 * 归档编排与 abandon 归 project（§6.2 行注）——本用例承载 diagnostics 半区：
 * 码面/目录/日志设施在失败路径上全部完好。
 */
TEST_F(DiagCatalogArchiveFailure, DtLife3_ArchiveFailureUserVisibleAndFacilitiesIntact)
{
    DiagCatalog catalog;
    catalog.attachDevLogSink(m_pipeline.get());   // 溢出登记走真实管线（§6.2）
    CatalogCapacityConfig config;
    config.maxEntries = 3;
    catalog.configureCapacity(config);

    // 任务运行诊断（已消费 Info×3——占满容量，为溢出登记创造触发条件）。
    const TaskIdentity task = makeTaskIdentity();
    std::vector<DiagEntryId> infoIds;
    for (int i = 0; i < 3; ++i) {
        DiagnosticEntry info = runtimeEntryInTask("RT-CANCELLED", task);
        infoIds.push_back(info.entryId);
        catalog.append(std::move(info));
        catalog.markConsumed(infoIds.back());
    }
    ASSERT_EQ(catalog.size(), 3u);

    // 归档桩失败（project §10.1 归档端口的环境错误——桩形态，P-DIAG-8 同款
    // 桩验证纪律；本单元不实现归档编排，只承载失败报告的诊断面）。
    const bool archiveSucceeded = false;
    ASSERT_FALSE(archiveSucceeded);

    // 失败报告：EX-ARCHIVE-FAILED（执行/Error——归档失败透传 StoreError 细节，
    // §4.6 收编码）随任务终态入目录。追加同时触发容量压力——最旧已消费 Info
    // 被淘汰腾位，溢出事实经真实管线落盘（"目录与日志不受影响"的活性证明）。
    DiagnosticEntry archiveFailed =
        m_factory->create(makeUserRecord("EX-ARCHIVE-FAILED", ObjectId::generate()),
                          makeExecutionContext(task));
    EXPECT_EQ(archiveFailed.severity, DiagnosticSeverity::Error)
        << "EX-ARCHIVE-FAILED 登记＝Error（用户可见——码表权威）";
    catalog.append(std::move(archiveFailed));
    // EX-ARCHIVE-FAILED 为 Error——不在淘汰判据内（级别保护），必在场。

    // 目录半区：失败报告在场（用户可见），既有诊断未受归档失败影响。
    DiagQuery byTask;
    byTask.task = task;
    const std::vector<DiagProjectionItem> items = catalog.snapshot(byTask);
    ASSERT_EQ(items.size(), 3u);
    bool hasArchiveFailure = false;
    int infoCount = 0;
    for (const DiagProjectionItem& item : items) {
        if (item.code == "EX-ARCHIVE-FAILED") {
            hasArchiveFailure = true;
            EXPECT_EQ(item.category, DiagnosticCategory::ExecutionFailed);
            EXPECT_EQ(item.actionKind, "retry-task") << "失败态动作族（§4.4）";
        } else {
            ++infoCount;
        }
    }
    EXPECT_TRUE(hasArchiveFailure) << "归档失败报告必附（EX-ARCHIVE-FAILED 用户可见）";
    EXPECT_EQ(infoCount, 2u) << "容量压力淘汰 1 条最旧已消费 Info（护栏正常运转）";

    // 日志半区：溢出登记真实落盘 Tier-D（日志仍在），且不进 Tier-U（Dev 不入
    // 用户目录——§6.2"临时诊断 vs 正式诊断"行；NFR-REL-05 两级分流）。
    ASSERT_TRUE(m_pipeline->flush(std::chrono::milliseconds{2000}))
        << "关闭前排空（有界等待——真实文件冲刷应成功）";
    EXPECT_NE(readFile("dev-diagnostics.log").find("DIAG-CATALOG-OVERFLOW"),
              std::string::npos)
        << "溢出登记应落盘开发级文件";
    EXPECT_EQ(readFile("user-diagnostics.log").find("DIAG-CATALOG-OVERFLOW"),
              std::string::npos)
        << "Dev 登记不得进入用户级文件（NFR-REL-05）";
}

/**
 * DT-LIFE-3：关闭不等待——归档失败路径结束后，诊断设施无任何关闭阻塞点
 * （§6.2 末行"关闭流程不能因非关键诊断永久等待"：目录回收＝内存释放；日志
 * flush 有界）。观测点"关闭时限"：目录析构与日志排空均以墙钟有界完成。
 */
TEST_F(DiagCatalogArchiveFailure, DtLife3_CloseAfterArchiveFailureIsBounded)
{
    auto catalog = std::make_unique<DiagCatalog>();
    catalog->attachDevLogSink(m_pipeline.get());
    const TaskIdentity task = makeTaskIdentity();

    // 复现归档失败现场：运行诊断＋失败报告入目录（EX-ARCHIVE-FAILED Error）。
    DiagnosticEntry info = runtimeEntryInTask("RT-CANCELLED", task);
    catalog->append(std::move(info));
    DiagnosticEntry archiveFailed =
        m_factory->create(makeUserRecord("EX-ARCHIVE-FAILED", ObjectId::generate()),
                          makeExecutionContext(task));
    catalog->append(std::move(archiveFailed));
    ASSERT_EQ(catalog->size(), 2u);

    // 目录回收（会话结束全量回收——§6.2）：内存释放，无 I/O、无等待——
    // 1 s 墙钟上界（数百量级的条目释放为微秒级；上界仅为"非永久等待"的
    // 防退化断言，WP-23 性能口径不在此）。堆分配＋reset＝可测量的析构点。
    const auto destroyBegin = std::chrono::steady_clock::now();
    catalog.reset();
    const auto destroyElapsed = std::chrono::steady_clock::now() - destroyBegin;
    EXPECT_LT(destroyElapsed, std::chrono::seconds{1})
        << "目录回收不得阻塞（§6.2 关闭无阻塞点）";

    // 日志排空（关闭路径另一半）：flush 有界返回（§9.6——不永久等待）。
    const auto flushBegin = std::chrono::steady_clock::now();
    const bool drained = m_pipeline->flush(std::chrono::milliseconds{3000});
    const auto flushElapsed = std::chrono::steady_clock::now() - flushBegin;
    EXPECT_TRUE(drained) << "无故障注入时排空应成功";
    EXPECT_LT(flushElapsed, std::chrono::seconds{4})
        << "flush 有界（deadline=3 s——超时放弃而非永久等待，§9.6）";
}

}  // namespace
