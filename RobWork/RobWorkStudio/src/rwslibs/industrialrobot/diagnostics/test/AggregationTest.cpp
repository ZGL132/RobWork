/**
 * @file   AggregationTest.cpp
 * @brief  诊断聚合器用例组（DT-AGG-1~5、DT-CHAIN-1）与 P-DIAG-1 处置钉住
 *         ——同根因稳定聚合、成员明细全保留、多工况稳定排序、局部碰撞不
 *         升级、原因链不丢根因、聚合纯函数确定性。
 *
 * 设计依据：
 *   - units/diagnostics.md §10 DT-AGG-1~5/DT-CHAIN-1/DT-DUP-2（聚合器半区
 *     ——§11 任务分工：目录/去重半区在 CatalogFactoryTest，本套件自证聚合
 *     视图与原因链遍历）、§6.3（原因链——根因不被聚合吞没/转换不丢根因）、
 *     §6.4（聚合键/计数语义/稳定排序/跨任务不聚合/局部碰撞不升级——C8；
 *     搜索未果口径 §8.1 C5 同表核对）、§9.4（聚合器契约表——纯函数/确定性/
 *     无写路径）
 *   - 需求 ERR-01（成员明细携带作用对象）、EVI-01（证据缺失全量列出——
 *     聚合不吞缺失项）、NFR-COR-02（同输入同序——DT-AGG-2 观测点）
 *   - 任务契约 tasks/foundation/DIAG-T06.json acceptance 1~4（逐条自证：
 *     acceptance 1→DtAgg1/DtAgg2/DtAgg3/DtDup 组；acceptance 2→DtAgg4 组；
 *     acceptance 3→DtChain1 组；acceptance 4→DtPdiag1 组）；
 *   - 用例名后缀＝矩阵行编号（DT-xxx-y），与 ird-test-report.json 的 trace
 *     追溯字段呼应（AGENTS.md §4.2 验证留痕）。
 *
 * 线程约束：全部用例单线程（§9.4 契约表"并发安全"由纯函数无共享状态承载
 * ——无互斥面可测；多线程交错属集成观测面，§10 未设行，不私建）。
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <sdurws/ird/core/DiagData.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/diagnostics/Aggregation.hpp>
#include <sdurws/ird/diagnostics/Catalog.hpp>
#include <sdurws/ird/diagnostics/DiagCodes.hpp>
#include <sdurws/ird/diagnostics/Errors.hpp>
#include <sdurws/ird/diagnostics/Factory.hpp>

namespace {

using namespace sdurws::ird::diagnostics;
// 测试文件位于全局匿名 ns：`core` 是 sdurws::ird 的成员，using-directive 不
// 引入兄弟命名空间——以别名使 core::X 限定名可见（T04/T05 套件同款处理面）。
namespace core = sdurws::ird::core;
using sdurws::ird::core::AttemptId;
using sdurws::ird::core::BranchId;
using sdurws::ird::core::ObjectId;
using sdurws::ird::core::ProjectId;
using sdurws::ird::core::RevisionId;
using sdurws::ird::core::RunId;
using sdurws::ird::core::TaskIdentity;

// ---------------------------------------------------------------------
// P-DIAG-1 编译期契约基线钉住（acceptance 4——core 冻结 diff 后增量同步、
// 不私改 core）。 AggregatedEntry 成员内嵌的 core 契约值类型在编译期钉死：
// core.md v0.1（Draft 未冻结）基线的任何漂移会使本断言编译失败——即"基线
// 漂移被机器看见"的机制（DIAG-T04 信封 record 钉住同先例）。
// ---------------------------------------------------------------------
static_assert(
    std::is_same_v<decltype(AggregatedEntry::MemberDetail::subject),
                   std::optional<core::ObjectId>>,
    "P-DIAG-1: MemberDetail::subject 必须内嵌 core::ObjectId（core.md v0.1 基线；"
    "core 冻结 diff 后增量同步，不私改 core）");
static_assert(
    std::is_same_v<decltype(AggregatedEntry::MemberDetail::entryId), DiagEntryId>,
    "成员身份＝目录条目身份类型（会话态引用，不入持久化身份——§4.2）");
static_assert(
    !std::is_same_v<decltype(AggregatedEntry::MemberDetail::localName),
                    std::string>,
    "localName 缺失语义＝nullopt（不伪造空串——core §4.8 字段口径）");

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
    std::chrono::system_clock::time_point m_now{std::chrono::seconds{2000000}};
};

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

/// 评估/运行域上下文基线（sourceUnit/sourceInterface 必填——§4.2 token 边界）。
DiagContext makeBaseContext(std::string sourceUnit = "runtime")
{
    DiagContext context;
    context.sourceUnit = std::move(sourceUnit);
    context.sourceInterface = "evaluate.batch";
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

/// 以码构造最小合法用户级记录（正例基线；各用例仅偏离被测面）。
core::DiagnosticRecord makeUserRecord(const std::string& code, ObjectId subject,
                                      std::string localName)
{
    return core::DiagnosticRecord::make(
        code, subject, std::move(localName), std::string("Robot.joint_5"),
        std::string("评估路径诊断"), std::string("触发原因"), std::string("建议动作"));
}

// =====================================================================
// DT-AGG-1／DT-CHAIN-1——同根因聚合、成员明细、原因链（acceptance 1/3）
// =====================================================================

class DiagAggregation : public ::testing::Test {
protected:
    void SetUp() override
    {
        registerBuiltinCodes(m_registry);
        m_registry.seal();
        m_factory = std::make_unique<DiagnosticsFactory>(m_registry, m_clock);
        // 阶段 A 转译清单（std::exception→RT-ROBWORK-ERROR——§8.3）：translate
        // 链路用例的派生条目来源（Factory.hpp registerStageATranslations）。
        registerStageATranslations(*m_factory);
    }

    /// 以工厂创建一条用户级条目（码＋局部名可变——去重键随 subject 区分）。
    DiagnosticEntry makeEntry(const std::string& code, const std::string& localName,
                              const DiagContext& context)
    {
        return m_factory->create(makeUserRecord(code, ObjectId::generate(), localName), context);
    }

    StableCodeRegistry m_registry;   ///< 码表（每用例独立——互不污染）
    ManualClock m_clock;             ///< 确定性时钟（orderKey 时间分量可复现）
    std::unique_ptr<DiagnosticsFactory> m_factory;
};

/// DT-AGG-1/DT-DUP-1：同根因稳定聚合——同根因派生条目折叠为一组（键＝根因
/// 身份），occurrences＝成员实际次数（5 名成员、阈值 3 → 计 5 非"≥3"截断，
/// acceptance 1"实际次数非截断"）；根因条目本身不被吞没（§6.3——组外自成
/// 呈现项）；两次聚合逐元素相等（稳定）；成员明细 subject/localName 全保留
/// （DT-AGG-3 观测点在本套件的派生链半区）。
TEST_F(DiagAggregation, DtAgg1_SameCauseRootFoldsDerivedEntriesWithExactCounts)
{
    // 根因条目（无 causedBy）＋5 条派生条目（translate 经 RT-ROBWORK-ERROR
    // 目标码，causedBy 分别指向 E1/E2/E4——根因同为 E1，§6.3 链例）。
    const DiagnosticEntry root = makeEntry("RT-INPUT-INVALID", "joint_root", makeBaseContext());
    const DiagnosticEntry d1 = m_factory->translate(
        std::runtime_error("stage1"), makeBaseContext(), &root);
    const DiagnosticEntry d2 = m_factory->translate(
        std::runtime_error("stage2"), makeBaseContext(), &root);
    const DiagnosticEntry d3 = m_factory->translate(
        std::runtime_error("stage3"), makeBaseContext(), &d1);
    const DiagnosticEntry d4 = m_factory->translate(
        std::runtime_error("stage4"), makeBaseContext(), &d1);
    const DiagnosticEntry d5 = m_factory->translate(
        std::runtime_error("stage5"), makeBaseContext(), &d3);

    ASSERT_TRUE(d1.causedBy.has_value() && *d1.causedBy == root.entryId);
    ASSERT_TRUE(d5.causedBy.has_value() && *d5.causedBy == d3.entryId);

    const std::vector<DiagnosticEntry> entries{root, d1, d2, d3, d4, d5};
    AggregationScope scope;
    scope.minGroupSize = 3;  // 阈值 3＜成员数 5：计数必须为实际值 5（非"≥3"）

    const DiagnosticAggregator aggregator;
    const std::vector<AggregatedEntry> first = aggregator.aggregate(entries, scope);
    const std::vector<AggregatedEntry> second = aggregator.aggregate(entries, scope);

    // 稳定聚合：两次构建同序同值（§9.4 确定性；DT-AGG-2 的同根因半区）。
    ASSERT_EQ(first.size(), second.size());
    for (std::size_t i = 0; i < first.size(); ++i) {
        EXPECT_EQ(first[i], second[i]) << "第 " << i << " 项两次聚合不一致";
    }

    // 恰一条"同根因"组：根因导航＝根因条目身份。
    const AggregatedEntry* group = nullptr;
    std::size_t standaloneRoots = 0;
    for (const AggregatedEntry& item : first) {
        if (item.causeRootEntryId.has_value()) {
            ASSERT_EQ(group, nullptr) << "同根因组不止一条（每派生条目恰归一组）";
            group = &item;
        } else {
            ++standaloneRoots;
        }
    }
    ASSERT_NE(group, nullptr) << "同根因组缺失";
    EXPECT_EQ(*group->causeRootEntryId, root.entryId);
    // 计数语义（§6.4）：occurrences＝成员实际次数总和＝5（非阈值截断）。
    EXPECT_EQ(group->occurrences, 5u);
    ASSERT_EQ(group->members.size(), 5u);
    // 成员明细全保留：5 个派生条目身份逐一在场（不吞成员——§6.4）。
    for (const DiagEntryId id : {d1.entryId, d2.entryId, d3.entryId, d4.entryId, d5.entryId}) {
        bool found = false;
        for (const auto& member : group->members) {
            found = found || member.entryId == id;
        }
        EXPECT_TRUE(found) << "成员 " << id << " 被聚合吞没";
    }
    // 组码＝根因条目码（实现口径——组码只是呈现主键）；成员最高严重＝Error。
    EXPECT_EQ(group->code, "RT-INPUT-INVALID");
    EXPECT_EQ(group->severity, DiagnosticSeverity::Error);
    // 根因不被聚合吞没（§6.3）：根因条目在组外有自己的呈现项（5 条目共
    // 1 组＋1 单成员项＝视图 2 项）。
    EXPECT_EQ(first.size(), 2u);
    EXPECT_EQ(standaloneRoots, 1u);
}

/// DT-AGG-1：同码多对象折叠且成员作用对象全保留（"不同作用对象绝不合并"
/// 的 DT-DUP-2 口径在聚合面＝呈现折叠不抹除对象——N 个对象逐条在明细；
/// acceptance 1"聚合保留全部成员明细"）。
TEST_F(DiagAggregation, DtAgg1_MultiObjectFoldKeepsEverySubjectVisible)
{
    // 同码（RT-INPUT-INVALID）×5 个不同 subject——DedupKey 互异（DT-DUP-2
    // 目录口径），聚合按键二折叠为一条"N 个对象受影响"视图项（§6.4 多对象
    // 场景），5 个对象逐条在成员明细。
    std::vector<DiagnosticEntry> entries;
    std::vector<ObjectId> subjects;
    for (int i = 0; i < 5; ++i) {
        subjects.push_back(ObjectId::generate());
        entries.push_back(m_factory->create(
            makeUserRecord("RT-INPUT-INVALID", subjects.back(),
                           "joint_" + std::to_string(i)),
            makeBaseContext()));
    }

    const DiagnosticAggregator aggregator;
    const std::vector<AggregatedEntry> view = aggregator.aggregate(entries, AggregationScope{});

    ASSERT_EQ(view.size(), 1u) << "同码多对象应折叠为一条视图项";
    const AggregatedEntry& group = view.front();
    EXPECT_EQ(group.code, "RT-INPUT-INVALID");
    EXPECT_FALSE(group.causeRootEntryId.has_value()) << "无因果条目不得携带根因导航";
    EXPECT_EQ(group.occurrences, 5u);
    ASSERT_EQ(group.members.size(), 5u);
    // 逐对象明细：每个 subject＋局部名都在场（展开即得逐对象明细——§6.4；
    // 顺序按成员 orderKey 升序＝创建序，确定性）。
    for (std::size_t i = 0; i < subjects.size(); ++i) {
        ASSERT_TRUE(group.members[i].subject.has_value());
        EXPECT_EQ(*group.members[i].subject, subjects[i]);
        ASSERT_TRUE(group.members[i].localName.has_value());
        EXPECT_EQ(*group.members[i].localName, "joint_" + std::to_string(i));
    }
    // DT-DUP-2 口径的计数半区：不同作用对象＝不同 DedupKey，occurrenceCount
    // 各自独立计数（不互算——"不同作用对象不能错误去重"）。
    EXPECT_EQ(aggregator.occurrenceCount(entries, entries[0].dedupKey), 1u);
    EXPECT_EQ(aggregator.occurrenceCount(entries, entries[4].dedupKey), 1u);
}

/// DT-DUP-1 计数半区（acceptance 1"同根因稳定聚合"的重复出现口径）：同
/// code＋同 subject＋同 scope 的重复条目（分批原始流形态），occurrenceCount
/// 返回实际出现次数；聚合视图对重复键条目逐条保留（聚合器不执行目录侧去重
/// 折叠——PA-1 权威唯一，折叠计数归 DiagCatalog）。
TEST_F(DiagAggregation, DtDup1_RepeatedOccurrenceCountedNotFolded)
{
    // 同工厂、同码、同 subject、同上下文 → 三条目同 DedupKey（工厂不去重
    // ——去重是目录 append 行为，§9.2 创建与记录分离）。
    const ObjectId subject = ObjectId::generate();
    const DiagContext context = makeBaseContext();
    std::vector<DiagnosticEntry> entries;
    for (int i = 0; i < 3; ++i) {
        entries.push_back(m_factory->create(
            makeUserRecord("RT-INPUT-INVALID", subject, "joint_same"), context));
    }
    ASSERT_EQ(entries[0].dedupKey, entries[2].dedupKey) << "同键前提不成立";

    const DiagnosticAggregator aggregator;
    // 计数＝实际出现次数 3（"同一错误重复出现→计数"的查询承载）。
    EXPECT_EQ(aggregator.occurrenceCount(entries, entries[0].dedupKey), 3u);
    // 未命中键计 0（边界）。
    DedupKey other;
    other.code = "RT-INPUT-INVALID";
    other.subject = ObjectId::generate();
    EXPECT_EQ(aggregator.occurrenceCount(entries, other), 0u);
}

/// DT-AGG-2：多工况稳定排序——批量条目乱序插入，两次独立构建（各自全新
/// 工厂＋同确定性时钟）聚合输出同序：观测点＝orderKey 序列相等（身份类值
/// ObjectId 为随机生成、不跨构建可比——比较面取确定性字段：视图 orderKey/
/// 码/计数/成员 entryId 序列；NFR-COR-02——acceptance 1"多工况稳定排序"）。
TEST_F(DiagAggregation, DtAgg2_TwoBuildsFromShuffledInputProduceIdenticalOrder)
{
    // 构建器：同码表/时钟下重建同构批量——每次全新工厂（entryId 序从 1 起，
    // 与时钟分量共同保证 orderKey 可复现）。
    const auto buildEntries = [this](bool shuffled) {
        DiagnosticsFactory localFactory(m_registry, m_clock);
        std::vector<DiagnosticEntry> batch;
        const TaskIdentity task = makeTaskIdentity();
        for (int i = 0; i < 3; ++i) {  // 多对象（Object 域）
            batch.push_back(localFactory.create(
                makeUserRecord("RT-INPUT-INVALID", ObjectId::generate(),
                               "joint_" + std::to_string(i)),
                makeBaseContext()));
        }
        for (int i = 0; i < 2; ++i) {  // 多工况（Task 域——同任务不同对象）
            batch.push_back(localFactory.create(
                makeUserRecord("EVI-EVIDENCE-MISSING", ObjectId::generate(),
                               "case_item_" + std::to_string(i)),
                makeExecutionContext(task)));
        }
        batch.push_back(localFactory.create(
            makeUserRecord("POLICY-CLL-SCENE-INVALID", ObjectId::generate(), "policy_obj"),
            makeBaseContext("policy")));
        if (shuffled) {
            std::reverse(batch.begin(), batch.end());  // 乱序插入（§10 DT-AGG-2 前置）
        }
        return batch;
    };

    // 两次独立构建：build1 乱序插入、build2 再乱序（另一构建的乱序副本）——
    // 输入顺序不同不应影响输出序（§6.4"多工况/多对象批量诊断的顺序确定性
    // 由创建顺序＋orderKey 双重保证"）。
    const std::vector<DiagnosticEntry> build1 = buildEntries(true);
    const std::vector<DiagnosticEntry> build2 = buildEntries(true);
    ASSERT_EQ(build1.size(), build2.size());

    const DiagnosticAggregator aggregator;
    const std::vector<AggregatedEntry> view1 = aggregator.aggregate(build1, AggregationScope{});
    const std::vector<AggregatedEntry> view2 = aggregator.aggregate(build2, AggregationScope{});

    // 同输入同序：两次构建输出结构逐项一致（确定性字段全比对；观测点
    // "orderKey 序列相等"含于 per-item 断言）。
    ASSERT_EQ(view1.size(), view2.size());
    for (std::size_t i = 0; i < view1.size(); ++i) {
        EXPECT_EQ(view1[i].code, view2[i].code) << "第 " << i << " 项跨构建码不一致";
        EXPECT_EQ(view1[i].occurrences, view2[i].occurrences) << "第 " << i << " 项计数不一致";
        EXPECT_EQ(view1[i].severity, view2[i].severity) << "第 " << i << " 项严重不一致";
        EXPECT_EQ(view1[i].category, view2[i].category) << "第 " << i << " 项分类不一致";
        EXPECT_EQ(view1[i].orderKey, view2[i].orderKey) << "第 " << i << " 项 orderKey 不一致";
        ASSERT_EQ(view1[i].members.size(), view2[i].members.size());
        for (std::size_t j = 0; j < view1[i].members.size(); ++j) {
            EXPECT_EQ(view1[i].members[j].entryId, view2[i].members[j].entryId)
                << "第 " << i << " 项第 " << j << " 成员跨构建不一致";
        }
    }
    // 输出序＝orderKey 严格升序（§6.4 稳定排序行——成员键唯一无并列）。
    for (std::size_t i = 1; i < view1.size(); ++i) {
        EXPECT_LT(view1[i - 1].orderKey, view1[i].orderKey) << "视图序非升序";
    }
    for (const AggregatedEntry& item : view1) {
        // 视图 orderKey＝成员最早 orderKey（min 语义——实现口径）：逐成员取
        // 条目 orderKey 求最小，与视图项携带值精确相等。
        ASSERT_FALSE(item.members.empty());
        OrderKey earliest;
        bool firstMember = true;
        for (const auto& member : item.members) {
            for (const DiagnosticEntry& entry : build1) {
                if (entry.entryId == member.entryId) {
                    if (firstMember || entry.orderKey < earliest) {
                        earliest = entry.orderKey;
                    }
                    firstMember = false;
                }
            }
        }
        EXPECT_EQ(item.orderKey, earliest) << "视图 orderKey 非成员最早值";
    }
    // 输入序无关性补面：同一构建的正序副本聚合结果与乱序副本逐项同构
    // （条目完全同源——此处可全字段含 subject 比对）。
    const std::vector<DiagnosticEntry> ordered = buildEntries(false);
    const std::vector<AggregatedEntry> viewOrdered =
        aggregator.aggregate(ordered, AggregationScope{});
    ASSERT_EQ(viewOrdered.size(), view1.size());
    for (std::size_t i = 0; i < viewOrdered.size(); ++i) {
        // 正序副本与 build1 非同源（独立构建——随机身份不同），仍比确定性面。
        EXPECT_EQ(viewOrdered[i].orderKey, view1[i].orderKey);
        EXPECT_EQ(viewOrdered[i].occurrences, view1[i].occurrences);
        EXPECT_EQ(viewOrdered[i].code, view1[i].code);
    }
}

/// DT-AGG-2 补面（§6.4 多工况/多任务行）：同码同作用域类别但不同任务五元组
/// 的条目**绝不跨任务折叠**（不同 TaskIdentity＝不同 scope）；同任务域内的
/// 同码条目正常折叠。
TEST_F(DiagAggregation, DtAgg2_EntriesFromDifferentTasksNeverMerge)
{
    const TaskIdentity taskA = makeTaskIdentity();
    const TaskIdentity taskB = makeTaskIdentity();
    const auto makeTaskEntry = [this](const TaskIdentity& task, const std::string& name) {
        return m_factory->create(makeUserRecord("EVI-EVIDENCE-MISSING", ObjectId::generate(), name),
                                 makeExecutionContext(task));
    };
    const DiagnosticEntry a1 = makeTaskEntry(taskA, "case_a1");
    const DiagnosticEntry a2 = makeTaskEntry(taskA, "case_a2");
    const DiagnosticEntry b1 = makeTaskEntry(taskB, "case_b1");

    const DiagnosticAggregator aggregator;
    const std::vector<AggregatedEntry> view =
        aggregator.aggregate({a1, a2, b1}, AggregationScope{});

    // 任务 A 的两条折叠为一组（同 code＋Task scope＋同五元组）；任务 B 独立
    // 单成员项（跨任务不聚合）——视图恰 2 项且成员归属严格按任务分区。
    ASSERT_EQ(view.size(), 2u);
    std::size_t taskAMembers = 0;
    std::size_t taskBMembers = 0;
    for (const AggregatedEntry& item : view) {
        for (const auto& member : item.members) {
            if (member.entryId == b1.entryId) {
                EXPECT_EQ(item.members.size(), 1u) << "跨任务条目被并入他组";
                ++taskBMembers;
            } else {
                ++taskAMembers;
            }
        }
    }
    EXPECT_EQ(taskAMembers, 2u);
    EXPECT_EQ(taskBMembers, 1u);
}

/// DT-AGG-3：聚合不吞缺失项（EVI-01 全量列出——acceptance 1"缺失项全量
/// 列出"）：6 条证据缺失（缺失对象各不同）与 3 条其他条目同批聚合，缺失组
/// 成员计数＝6、逐对象在场；两组明细总成员数＝9（不因聚合短路丢条目）。
TEST_F(DiagAggregation, DtAgg3_MissingEvidenceMembersAllListed)
{
    std::vector<DiagnosticEntry> entries;
    std::vector<ObjectId> missingObjects;
    for (int i = 0; i < 6; ++i) {
        missingObjects.push_back(ObjectId::generate());
        entries.push_back(m_factory->create(
            makeUserRecord("EVI-EVIDENCE-MISSING", missingObjects.back(),
                           "missing_" + std::to_string(i)),
            makeBaseContext("evidence")));
    }
    for (int i = 0; i < 3; ++i) {
        entries.push_back(m_factory->create(
            makeUserRecord("RT-INPUT-INVALID", ObjectId::generate(), "input_" + std::to_string(i)),
            makeBaseContext()));
    }

    const DiagnosticAggregator aggregator;
    const std::vector<AggregatedEntry> view = aggregator.aggregate(entries, AggregationScope{});

    // 两组（缺失组＋输入组），成员总数不丢（全量列出——§8.1 表 2④）。
    ASSERT_EQ(view.size(), 2u);
    std::size_t totalMembers = 0;
    const AggregatedEntry* missingGroup = nullptr;
    for (const AggregatedEntry& item : view) {
        totalMembers += item.members.size();
        if (item.code == "EVI-EVIDENCE-MISSING") {
            missingGroup = &item;
        }
    }
    EXPECT_EQ(totalMembers, 9u) << "聚合丢弃了条目（EVI-01 全量列出被违反）";
    ASSERT_NE(missingGroup, nullptr);
    // 缺失组：6 名成员逐一在场（不因聚合短路——DT-AGG-3 观测点"成员计数＝N"）。
    EXPECT_EQ(missingGroup->occurrences, 6u);
    ASSERT_EQ(missingGroup->members.size(), 6u);
    for (const ObjectId& object : missingObjects) {
        bool found = false;
        for (const auto& member : missingGroup->members) {
            found = found || (member.subject.has_value() && *member.subject == object);
        }
        EXPECT_TRUE(found) << "缺失对象未全量列出（EVI-01）";
    }
    // 分类＝成员共同分类的投影（不私造汇总分类——实现口径）。
    ASSERT_TRUE(missingGroup->category.has_value());
    EXPECT_EQ(*missingGroup->category, DiagnosticCategory::EvidenceMissing);
}

// =====================================================================
// DT-AGG-4——局部碰撞/搜索未果不升级（acceptance 2；C8＋C5 同表核对）
// =====================================================================

/// DT-AGG-4：局部碰撞不自动升级任务不可行——N 条构型/路径级碰撞呈现（阶段 A
/// 内置表无碰撞域码，以 PolicyDenied 类码承载同分类同严重语义——§4.3/§4.4
/// "策略拒绝＝Warning"）聚合后：分类仍是 policy-denied（分类不变）、严重仍
/// Warning、全部输出中无 infeasibility-proof 分类条目（C8——聚合仅呈现分组
/// 无判定权；§9.4"期望聚合产出工程判定＝非法调用，无此能力"）；同表核对
/// 搜索未果口径（§8.1 C5）：DataInsufficient 类条目聚合后同样不升级。
TEST_F(DiagAggregation, DtAgg4_ConfigurationCollisionNeverEscalatesToInfeasible)
{
    // 同一任务域内：5 条策略拒绝（构型级碰撞呈现）＋4 条数据不足（搜索未果
    // 呈现）——一批聚合（"同表核对"）。
    const TaskIdentity task = makeTaskIdentity();
    std::vector<DiagnosticEntry> entries;
    for (int i = 0; i < 5; ++i) {
        entries.push_back(m_factory->create(
            makeUserRecord("POLICY-CLL-SCENE-INVALID", ObjectId::generate(),
                           "config_collision_" + std::to_string(i)),
            makeExecutionContext(task)));
    }
    for (int i = 0; i < 4; ++i) {
        entries.push_back(m_factory->create(
            makeUserRecord("EVI-SNAPSHOT-INCOMPLETE", ObjectId::generate(),
                           "search_unresolved_" + std::to_string(i)),
            makeExecutionContext(task)));
    }

    const DiagnosticAggregator aggregator;
    const std::vector<AggregatedEntry> view = aggregator.aggregate(entries, AggregationScope{});

    // 恰两条视图项（策略拒绝组＋数据不足组），全部成员保留（9＝5＋4）。
    ASSERT_EQ(view.size(), 2u);
    std::size_t totalMembers = 0;
    for (const AggregatedEntry& item : view) {
        totalMembers += item.members.size();
    }
    EXPECT_EQ(totalMembers, 9u);

    for (const AggregatedEntry& item : view) {
        // 分类不变：成员共同分类原样投影，绝不产生 infeasibility-proof 语义
        // （C8：任务级不可行证明归域产生、evidence 校验——聚合无判定权）。
        ASSERT_TRUE(item.category.has_value());
        EXPECT_NE(*item.category, DiagnosticCategory::InfeasibilityProof)
            << "聚合产出了不可行证明语义（DT-AGG-4 反例命中）";
        EXPECT_TRUE(*item.category == DiagnosticCategory::PolicyDenied
                    || *item.category == DiagnosticCategory::DataInsufficient)
            << "聚合私造了成员之外的分类";
        // 严重不升级：策略拒绝/数据不足均为 Warning（§4.4 矩阵默认），聚合
        // 不抬升（成员最高严重即 Warning——无 Error 成员）。
        EXPECT_EQ(item.severity, DiagnosticSeverity::Warning);
        // 类型层面无证明字段（§9.4"无此能力"）：AggregatedEntry 的字段集
        // （code/causeRootEntryId/severity/category/scopeKind/occurrences/
        // orderKey/members——Aggregation.hpp 逐字段注释）不含任何证明/不可行
        // 承载——上面的分类与严重断言即"无工程判定出口"的运行期观测面。
    }
    // 显式分类集合断言（§10 DT-AGG-4 观测点"分类枚举集合"）：恰为两输入类
    // （排序后按枚举声明序——PolicyDenied=4 先于 DataInsufficient=10）。
    std::vector<DiagnosticCategory> categories;
    for (const AggregatedEntry& item : view) {
        categories.push_back(*item.category);
    }
    std::sort(categories.begin(), categories.end());
    ASSERT_EQ(categories.size(), 2u);
    EXPECT_EQ(categories[0], DiagnosticCategory::PolicyDenied);
    EXPECT_EQ(categories[1], DiagnosticCategory::DataInsufficient);
}

/// DT-AGG-1 补面（实现口径——不私造汇总分类）：同根因组成员分类不一致时
/// 视图项 category＝nullopt（手造条目直接驱动纯函数——聚合路径不依赖工厂）；
/// 同时覆盖手造环的防御性拒绝（构造保证无环，触发即条目被手改——Usage）。
TEST_F(DiagAggregation, DtAgg1_MixedCategoryCauseRootGroupYieldsNoInventedCategory)
{
    // 手造三态：根因（无 causedBy）＋两条分类不同的派生（同指向根因）。
    DiagnosticEntry root;
    root.entryId = 1;
    root.record.code = "X-ROOT";
    root.severity = DiagnosticSeverity::Error;
    DiagnosticEntry derivedWarn;
    derivedWarn.entryId = 2;
    derivedWarn.record.code = "X-WARN-DERIVED";
    derivedWarn.severity = DiagnosticSeverity::Warning;
    derivedWarn.category = DiagnosticCategory::EvidenceMissing;
    derivedWarn.causedBy = 1;
    DiagnosticEntry derivedErr;
    derivedErr.entryId = 3;
    derivedErr.record.code = "X-ERR-DERIVED";
    derivedErr.severity = DiagnosticSeverity::Error;
    derivedErr.category = DiagnosticCategory::ExecutionFailed;
    derivedErr.causedBy = 1;

    const DiagnosticAggregator aggregator;
    const std::vector<AggregatedEntry> view =
        aggregator.aggregate({root, derivedWarn, derivedErr}, AggregationScope{});

    // 同根因组：成员最高严重＝Error（级别秩取最大——非枚举序）；分类不一致
    // → nullopt（不私造汇总分类）；组码＝根因码。
    ASSERT_EQ(view.size(), 2u);
    const AggregatedEntry* group = nullptr;
    for (const AggregatedEntry& item : view) {
        if (item.causeRootEntryId.has_value()) {
            group = &item;
        }
    }
    ASSERT_NE(group, nullptr);
    EXPECT_EQ(group->code, "X-ROOT");
    EXPECT_FALSE(group->category.has_value()) << "分类不一致时不得私造汇总分类";
    EXPECT_EQ(group->severity, DiagnosticSeverity::Error);
    EXPECT_EQ(group->occurrences, 2u);

    // 手造环（10↔11 互指）：构造保证无环（单调 entryId），聚合的容差遍历
    // 环防御应 Usage fail-fast（不静默死循环/不静默返回）。
    DiagnosticEntry ringA;
    ringA.entryId = 10;
    ringA.record.code = "X-RING-A";
    ringA.severity = DiagnosticSeverity::Error;
    ringA.causedBy = 11;
    DiagnosticEntry ringB;
    ringB.entryId = 11;
    ringB.record.code = "X-RING-B";
    ringB.severity = DiagnosticSeverity::Error;
    ringB.causedBy = 10;
    expectThrowsWithCode([&] { (void)aggregator.aggregate({ringA, ringB}, AggregationScope{}); },
                         DiagnosticsErrorCode::Usage, "手造原因环应 Usage 拒绝");
}

// =====================================================================
// DT-CHAIN-1——原因链不丢根因（acceptance 3）
// =====================================================================

/// DT-CHAIN-1：错误转换派生条目的 causedBy 链完整到根因（禁止翻译后只剩
/// 最后一层）——worker 崩溃场景：根因 EX-WORKER-CRASHED → 转译派生（任务
/// 失败层，causedBy 根因）→ 再转译派生（告知层，causedBy 任务失败层）；
/// CauseLink::chainOf 从告知层逐层遍历至 EX-WORKER-CRASHED 根因；断链/未知
/// 起点/空身份的严格查询违约 Usage（不返回半截链——半截链会掩盖"只剩最后
/// 一层"缺陷）。
TEST_F(DiagAggregation, DtChain1_TranslatedDerivativeChainWalksToRootCause)
{
    // 根因：worker 崩溃（EX 域码——execution 上下文前置 task，§8.10）。
    const DiagnosticEntry crash = m_factory->create(
        core::DiagnosticRecord::make(
            "EX-WORKER-CRASHED", ObjectId::generate(), std::string("worker-1"),
            std::string("Robot.worker-1"), std::string("worker 进程异常退出"),
            std::string("评估工作进程崩溃"), std::string("查看开发日志定位崩溃点")),
        makeExecutionContext(makeTaskIdentity()));
    // 派生层 1：崩溃转译为任务失败面（RT-ROBWORK-ERROR 目标码，causedBy 根因
    // ——§8.1 规则 1"转换产出必须链接根因"；subject 继承根因）。
    const DiagnosticEntry rejected = m_factory->translate(
        std::runtime_error("task rejected by worker crash"), makeBaseContext(), &crash);
    // 派生层 2：任务失败再转译为结果缺失告知面（causedBy 派生层 1——链的
    // 中间层必须保留，"翻译后只剩最后一层"即断链）。
    const DiagnosticEntry notice = m_factory->translate(
        std::runtime_error("result missing notice"), makeBaseContext(), &rejected);

    // 链接完整性：逐层 causedBy 精确指向（acceptance 3"链完整到根因"）。
    ASSERT_TRUE(rejected.causedBy.has_value());
    EXPECT_EQ(*rejected.causedBy, crash.entryId);
    ASSERT_TRUE(notice.causedBy.has_value());
    EXPECT_EQ(*notice.causedBy, rejected.entryId);
    EXPECT_EQ(rejected.record.code, "RT-ROBWORK-ERROR");
    EXPECT_EQ(notice.record.code, "RT-ROBWORK-ERROR");

    // 链遍历（DT-CHAIN-1 观测点）：告知层 → 任务失败层 → 崩溃根因，全链
    // 三层逐层在链上（链长度 3＝中间层未被翻译吞掉）。
    const std::vector<DiagnosticEntry> entries{crash, rejected, notice};
    const std::vector<DiagEntryId> chain = CauseLink::chainOf(entries, notice.entryId);
    ASSERT_EQ(chain.size(), 3u);
    EXPECT_EQ(chain[0], notice.entryId);
    EXPECT_EQ(chain[1], rejected.entryId);
    EXPECT_EQ(chain[2], crash.entryId);
    // 根因即 EX-WORKER-CRASHED（"链遍历至 EX-WORKER-CRASHED"观测点）。
    const DiagEntryId rootId = CauseLink::rootOf(entries, notice.entryId);
    EXPECT_EQ(rootId, crash.entryId);
    bool rootIsCrash = false;
    for (const DiagnosticEntry& entry : entries) {
        rootIsCrash = rootIsCrash || (entry.entryId == rootId && entry.record.code == "EX-WORKER-CRASHED");
    }
    EXPECT_TRUE(rootIsCrash);

    // 聚合视图对链的容差半区：断链子集（缺中间层）不抛、按集内可见部分分组
    // （实现口径①——呈现事实，不伪造根因；严格判定归 CauseLink）。
    const DiagnosticAggregator aggregator;
    const std::vector<AggregatedEntry> partial =
        aggregator.aggregate({crash, notice}, AggregationScope{});
    EXPECT_EQ(partial.size(), 2u) << "断链子集中派生条目应独立呈现（不伪造根因）";

    // 严格查询违约面：断链（缺中间层）→ Usage；未知起点 → Usage；空身份 →
    // Usage（不返回半截链——半截链掩盖丢根因缺陷）。
    expectThrowsWithCode([&] { (void)CauseLink::chainOf({crash, notice}, notice.entryId); },
                         DiagnosticsErrorCode::Usage, "断链子集的严格链查询应 Usage");
    expectThrowsWithCode([&] { (void)CauseLink::rootOf(entries, 999); },
                         DiagnosticsErrorCode::Usage, "未知起点应 Usage");
    expectThrowsWithCode([&] { (void)CauseLink::rootOf(entries, 0); },
                         DiagnosticsErrorCode::Usage, "空身份起点应 Usage");
}

// =====================================================================
// P-DIAG-1 处置与聚合纯函数契约（acceptance 4＋§9.4 契约表）
// =====================================================================

/// P-DIAG-1 运行期半区（acceptance 4——成员内嵌 core 契约值）：成员明细的
/// subject 是源记录 core::ObjectId 的原样值拷贝（core 契约值不改写、不重算
/// ——core 冻结 diff 后增量同步的运行期佐证；编译期钉住见文件头 static_assert）。
TEST_F(DiagAggregation, DtPdiag1_MemberDetailEmbedsCoreContractValuesVerbatim)
{
    const ObjectId subject = ObjectId::generate();
    const DiagnosticEntry entry = m_factory->create(
        makeUserRecord("RT-INPUT-INVALID", subject, "joint_p1"), makeBaseContext());

    const DiagnosticAggregator aggregator;
    const std::vector<AggregatedEntry> view =
        aggregator.aggregate({entry}, AggregationScope{});

    ASSERT_EQ(view.size(), 1u);
    ASSERT_EQ(view.front().members.size(), 1u);
    // core 契约值原样内嵌：与源 record.subject 逐值相等（不改写）。
    ASSERT_TRUE(view.front().members.front().subject.has_value());
    EXPECT_EQ(*view.front().members.front().subject, subject);
    ASSERT_TRUE(entry.record.subject.has_value());
    EXPECT_EQ(*view.front().members.front().subject, *entry.record.subject);
}

/// §9.4 契约表（acceptance 1 确定性半区＋调用方违约面）：聚合为纯函数
/// （同输入两次调用逐元素相等、输入集合不被改写——"接口无写路径"）；
/// 阈值可配（§6.4 触发行）；空集聚合＝空视图；调用方违约（entryId 0/重复/
/// Dev 级/minGroupSize 0）Usage fail-fast。
TEST_F(DiagAggregation, DtAgg5_PureFunctionDeterminismAndCallerViolations)
{
    const DiagnosticEntry e1 = makeEntry("RT-INPUT-INVALID", "joint_a", makeBaseContext());
    const DiagnosticEntry e2 = makeEntry("RT-INPUT-INVALID", "joint_b", makeBaseContext());
    const std::vector<DiagnosticEntry> entries{e1, e2};

    const DiagnosticAggregator aggregator;
    const std::vector<DiagnosticEntry> before = entries;  // 输入深拷贝基线
    const std::vector<AggregatedEntry> first = aggregator.aggregate(entries, AggregationScope{});
    const std::vector<AggregatedEntry> second = aggregator.aggregate(entries, AggregationScope{});
    // 纯函数：同输入同输出；输入集合逐字节未变（无写路径——§9.4）。
    ASSERT_EQ(first.size(), second.size());
    for (std::size_t i = 0; i < first.size(); ++i) {
        EXPECT_EQ(first[i], second[i]);
    }
    EXPECT_EQ(entries, before);

    // 阈值可配（§6.4 触发行）：同码 2 条——阈值 3 不折叠（两条单成员项），
    // 阈值 2 折叠（一条双成员项）。
    AggregationScope strict;
    strict.minGroupSize = 3;
    const std::vector<AggregatedEntry> unfolded = aggregator.aggregate(entries, strict);
    EXPECT_EQ(unfolded.size(), 2u);
    for (const AggregatedEntry& item : unfolded) {
        EXPECT_EQ(item.members.size(), 1u);
        EXPECT_EQ(item.occurrences, 1u);
    }
    AggregationScope loose;
    loose.minGroupSize = 2;
    const std::vector<AggregatedEntry> folded = aggregator.aggregate(entries, loose);
    ASSERT_EQ(folded.size(), 1u);
    EXPECT_EQ(folded.front().members.size(), 2u);
    EXPECT_EQ(folded.front().occurrences, 2u);

    // 空集边界：聚合＝空视图；计数＝0（§9.4 无空输入拒绝契约行）。
    const std::vector<DiagnosticEntry> empty;
    EXPECT_TRUE(aggregator.aggregate(empty, AggregationScope{}).empty());
    EXPECT_EQ(aggregator.occurrenceCount(empty, DedupKey{}), 0u);

    // 调用方违约面（fail-fast——AGENTS.md 错误语义）：entryId 0／entryId
    // 重复／Dev 级条目／阈值 0。
    DiagnosticEntry zeroId;
    zeroId.entryId = 0;  // 其余字段默认（severity 默认 Error，先命中 id 检查）
    expectThrowsWithCode([&] { (void)aggregator.aggregate({zeroId}, AggregationScope{}); },
                         DiagnosticsErrorCode::Usage, "entryId 0 应 Usage");

    // 重复身份：两个独立工厂的条目身份序列都从 1 起——同批输入即重复。
    StableCodeRegistry otherRegistry;
    registerBuiltinCodes(otherRegistry);
    otherRegistry.seal();
    DiagnosticsFactory otherFactory(otherRegistry, m_clock);
    const DiagnosticEntry clashing =
        otherFactory.create(makeUserRecord("RT-UNIT-MISMATCH", ObjectId::generate(), "joint_x"),
                            makeBaseContext());
    ASSERT_EQ(clashing.entryId, e1.entryId) << "重复身份前提不成立";
    expectThrowsWithCode([&] { (void)aggregator.aggregate({e1, clashing}, AggregationScope{}); },
                         DiagnosticsErrorCode::Usage, "entryId 重复应 Usage");

    // Dev 级条目（§4.3 传播规则"Dev 不参与用户级聚合"——Dev 码不入目录，
    // 出现在聚合输入即调用方违约。EX 域码创建前置 task——§8.10 对 execution
    // 域码无严重级豁免，故携 execution 上下文）。
    const DiagnosticEntry devEntry = m_factory->create(
        core::DiagnosticRecord::make(
            "EX-CHANNEL-PROTOCOL-ERROR", {}, {}, {},
            std::string("worker 通道帧序断裂"), std::string("帧序号回退"),
            std::string("检查回传批次序号")),
        makeExecutionContext(makeTaskIdentity()));
    EXPECT_EQ(devEntry.severity, DiagnosticSeverity::Dev);
    expectThrowsWithCode([&] { (void)aggregator.aggregate({devEntry}, AggregationScope{}); },
                         DiagnosticsErrorCode::Usage, "Dev 级条目应 Usage");

    AggregationScope zeroThreshold;
    zeroThreshold.minGroupSize = 0;
    expectThrowsWithCode([&] { (void)aggregator.aggregate(entries, zeroThreshold); },
                         DiagnosticsErrorCode::Usage, "阈值 0 应 Usage");
}

}  // namespace
