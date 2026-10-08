/**
 * @file   StatusBannerContractTest.cpp
 * @brief  标题栏/状态栏与恢复横幅的契约测试（WF-VER-217/218——units/
 *         workflow.md §11.2 生命周期主线；PM-11 状态来源真实链＋PM-15/
 *         AT-21 三场景真实落盘＋统一诊断目录集成的真实承载半区）。
 *
 * 设计依据：
 *   - units/workflow.md §7.6（标题栏 `<显示名>[*][（只读）]`＋状态栏四
 *     字段的来源；恢复横幅三场景一句话汇总＋三动作；诊断经统一诊断目录
 *     集成——横幅取数与诊断表/日志同源）、§9（diagnostics 行：统一诊断
 *     目录条目＝横幅的问题数据源；execution 行：Interrupted 条目消费）、
 *     §11.2（217 观测点＝TitleStatusData 各状态组合逐项正确——来源链
 *     真实承载；218 观测点＝横幅数据经诊断目录集成——本文件主体）、
 *     §11.3（AT-21 承接行——残留草稿恢复/中断任务提示）
 *   - REQUIREMENTS.md §17 PM-11/PM-15 原文、AT-21（草稿与恢复——残留
 *     草稿恢复、中断任务提示）、P-DIAG-7（诊断目录/日志容量工程默认
 *     未校准期间按保守默认——本文件消费 DiagCatalog 默认护栏配置，
 *     不改容量参数：默认值面归 diagnostics DIAG-T09 已按 P-DIAG-7 登记，
 *     workflow 编排面零容量数值——I-WF-3 同精神）
 *   - project.md §5.1（open ⑤步恢复扫描——.staging 残留 PRJ-RECOVERY-
 *     IGNORED-UNCOMMITTED＋孤儿草稿 PRJ-RECOVERY-ORPHAN-DRAFT，随
 *     RecoveryReport 返回＋经 IDiagnosticsSink 上报双通道）、§7.4
 *     （恢复顺序①③——清单确定性排序）
 *   - execution.md §5.1 T14/§7.5（恢复期重建→Interrupted 终态＋
 *     EX-TASK-INTERRUPTED 状态标注诊断"已中断可重跑"——PM-15 场景②的
 *     真实词形；forRecoveryInterrupted 工厂路径）
 *   - diagnostics.md §9.7（DiagCatalog::snapshot 只读投影——恢复条目
 *     的查询面；DiagnosticsSinkImpl 三方语义）
 *
 * 契约测试形态（§11.0——"契约测试＝跨单元联合"）：与 project 真实存储
 * 实现（ProjectStoreFactory::createNew/open 真实落盘＋真实恢复扫描）、
 * execution 真实状态机（T14 恢复期重建工厂——中断条目词形真实）、
 * diagnostics 真实目录（StableCodeRegistry＋DiagnosticsFactory＋DiagCatalog
 * ——码注册/subject 边界/上下文校验全真链）四方联合。三场景盘面真实
 * 注入：.staging/<tx-id>/ 目录（场景①）＋drafts/<dir>/<mod>.draft.json.new
 * 残留（场景③）——open ⑤步真实扫描产出清单与诊断；L5 装配桥以测试
 * 等价物形态承接（project sink→目录注入补项目身份 subject——用户级
 * subject 边界；workflow 恢复事实端口从目录 snapshot 折叠——场景有无
 * 判定来自目录条目、计数来自对端清单事实，两口径不混用）。
 *
 * P-DIAG-7 留痕（契约 acceptance 3）：本文件对 DiagCatalog 零容量配置
 * 调用（configureCapacity 不触——默认护栏值即 DIAG-T09 登记的 P-DIAG-7
 * 工程默认，maxEntries=10,000 条/已消费最旧 Info 分级淘汰；校准归 WP-23
 * 性能验收，workflow 编排面零容量数值零校准义务——仅如实留痕）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/core/DiagData.hpp>              // core::DiagnosticRecord（码/subject/三文案字段）
#include <sdurws/ird/core/Identity.hpp>              // core 强类型（ProjectId/ObjectId/RunId/AttemptId）
#include <sdurws/ird/diagnostics/Catalog.hpp>        // DiagCatalog/DiagnosticsSinkImpl/DiagContext/DiagQuery
#include <sdurws/ird/diagnostics/DiagCodes.hpp>      // StableCodeRegistry/registerBuiltinCodes（码表真链）
#include <sdurws/ird/diagnostics/Factory.hpp>        // DiagnosticsFactory（create 校验链真链）
#include <sdurws/ird/execution/StateMachine.hpp>     // TaskStateMachine::forRecoveryInterrupted（T14 工厂）
#include <sdurws/ird/execution/TaskTypes.hpp>        // TaskRecord/TaskId（中断条目词形）
#include <sdurws/ird/project/ProjectStore.hpp>       // ProjectStoreFactory/OpenStoreRequest/IDiagnosticsSink
#include <sdurws/ird/project/QueryPort.hpp>          // IProjectQueryPort（currentMetadata/branchTips/listDrafts）
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp> // IRD_TEST_INFO——需求/AT 追溯登记
#include <sdurws/ird/workflow/Projection.hpp>
#include <sdurws/ird/workflow/Types.hpp>

#include <windows.h>  // CreateFileW/CloseHandle（外部持锁模拟——内核裁决面）

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

using namespace sdurws::ird;
namespace diag = sdurws::ird::diagnostics;
namespace exe = sdurws::ird::execution;
using workflow::RecoveryFacts;
using workflow::RecoveryScenario;
using workflow::RecoveryScenarioFact;
using workflow::TitleFacts;

/// PM-11 只读后缀黄金词形（测试作为调用方从 ui 文案表解析后传入——
/// 与模型测试同值，格式串以需求原文为准）。
constexpr const char* kReadonlySuffix = "（只读）";

// =====================================================================
// 夹具：临时目录（套件级总根＋用例级独立目录——ReconnectReadOnlyContract
// 同型；全部真实落盘）
// =====================================================================

class StatusBannerContract : public ::testing::Test {
protected:
    static void SetUpTestSuite()
    {
        std::error_code ec;
        s_base = fs::temp_directory_path(ec) / "ird_wf_status_banner_contract_test";
        ASSERT_FALSE(ec);
        fs::remove_all(s_base, ec);  // 前次运行残留防御（总根重建）
        fs::create_directories(s_base, ec);
        ASSERT_FALSE(ec) << "无法创建测试根目录: " << s_base.string();
    }

    static void TearDownTestSuite()
    {
        std::error_code ec;
        fs::remove_all(s_base, ec);  // 失败保留现场惯例——总根清理
    }

    void SetUp() override
    {
        m_dir = s_base / ("case" + std::to_string(++s_caseCounter));
        std::error_code ec;
        fs::create_directories(m_dir, ec);
        ASSERT_FALSE(ec);
    }

    /// 以 createNew 产出黄金项目（真实落盘——调用方持有 store；释放写
    /// 锁须 requestClose＋reset，重开即走真实 open 协议）。
    static project::OpenStoreResult createGolden(const fs::path& dir,
                                                 const char* displayName)
    {
        return project::ProjectStoreFactory::createNew(dir, displayName);
    }

    /// 场景①盘面注入：.staging/<tx-id>/ 事务残留目录＋残件文件（open ⑤
    /// 步恢复扫描按"非 tmp 的 .staging 子目录＝未提交事务"扫描——TxEngine
    /// scanForRecovery 词形；现场保留不删——§4.1 .staging 行口径）。
    void injectStagingResidue(const fs::path& projectDir,
                              const std::string& txName)
    {
        const fs::path txDir = projectDir / ".staging" / txName;
        std::error_code ec;
        fs::create_directories(txDir, ec);
        ASSERT_FALSE(ec) << txDir.string();
        // 残件文件（事务工作现场——内容任意：扫描按目录形态判定，不解析）。
        std::ofstream out(txDir / "payload.bin", std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(out.is_open());
        out << "unfinished-transaction-workspace";
    }

    /// 场景③盘面注入：drafts/<dir>/<mod>.draft.json.new 崩溃残留（open ⑤
    /// 步孤儿草稿扫描对 .new/.bak 后缀直接报告——scanOrphanDrafts 词形；
    /// 内容任意：残留不做归属校验）。
    void injectOrphanDraftResidue(const fs::path& projectDir)
    {
        const fs::path draftsDir = projectDir / "drafts" / "main";
        std::error_code ec;
        fs::create_directories(draftsDir, ec);
        ASSERT_FALSE(ec) << draftsDir.string();
        std::ofstream out(draftsDir / "mod.draft.json.new",
                          std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(out.is_open());
        out << "{\"crashed-save\":true}";
    }

    static fs::path s_base;
    static int s_caseCounter;
    fs::path m_dir;
};

fs::path StatusBannerContract::s_base;
int StatusBannerContract::s_caseCounter = 0;

// =====================================================================
// 捕获型诊断 sink（project IDiagnosticsSink 实现——open ⑤步真实产出链
// 的收集面；ReconnectReadOnlyContract::CapturingSink 同款形态）
// =====================================================================

class CapturingSink final : public project::IDiagnosticsSink {
public:
    std::vector<core::DiagnosticRecord> reports;///< 用户级诊断捕获（真实产出）

    void report(const core::DiagnosticRecord& record) override
    {
        reports.push_back(record);
    }
    void reportDev(const std::string& /*channel*/,
                   const std::string& /*message*/) override
    {
        // 开发级诊断不参与本契约观测点（PRJ-RECOVERY-* 都是用户级）——丢弃。
    }

    /// 码值逐字匹配（P-PR-6：码值＝diagnostics.md §4.6 收编清单）。
    bool hasCode(const std::string& code) const
    {
        for (const auto& r : reports) {
            if (r.code == code) {
                return true;
            }
        }
        return false;
    }
};

// =====================================================================
// 开发日志替身（IDevLogSink 窄接口——DiagnosticsSinkImpl 的 reportDev
// 路由承载；CatalogFactoryTest::CapturingDevLog 同款形态）
// =====================================================================

struct CapturingDevLog final : diag::IDevLogSink {
    void logDev(std::string_view /*channel*/, std::string /*message*/) override
    {
        // 本契约不观测开发日志（Dev 不入目录——§6.2）。
    }
};

// =====================================================================
// 外部持锁模拟（Win32 独占句柄 RAII——内核裁决面，非任何单元内部 API；
// ReconnectReadOnlyContractTest::ScopedForeignLockHandle 同款形态）：
// 以 StoreLock 同款共享模式（GENERIC_READ|GENERIC_WRITE＋仅
// FILE_SHARE_READ）持有 lock 文件——后续任何写访问请求得
// ERROR_SHARING_VIOLATION（StoreLock 判 HeldByOther→降级只读）。
// =====================================================================

class ScopedForeignLockHandle {
public:
    explicit ScopedForeignLockHandle(const fs::path& lockFile)
    {
        m_handle = ::CreateFileW(lockFile.wstring().c_str(),
                                 GENERIC_READ | GENERIC_WRITE,
                                 FILE_SHARE_READ,
                                 nullptr, OPEN_EXISTING,
                                 FILE_ATTRIBUTE_NORMAL, nullptr);
    }
    ~ScopedForeignLockHandle()
    {
        if (m_handle != INVALID_HANDLE_VALUE) {
            ::CloseHandle(m_handle);
        }
    }
    /// 句柄是否就位（CreateFileW 成功——外部持锁面建立）。
    bool valid() const { return m_handle != INVALID_HANDLE_VALUE; }

private:
    HANDLE m_handle = INVALID_HANDLE_VALUE;///< OS 排他句柄（RAII 持有）
};

// =====================================================================
// L5 装配桥（测试等价物）：project 产出记录 → 统一诊断目录注入。
//
// L5 真实形态：project 打开期经 IDiagnosticsSink 上报的记录进入统一
// 目录时，由装配层补齐目录侧必需项——用户级 subject 边界（factory
// SubjectMissing 校验：用户级码缺 subject 即拒）以**项目元数据对象**
// 身份绑定（打开期唯一确定的项目作用对象——ProjectMetadataView.ref.oid）；
// 上下文锚定来源通道（sourceUnit/sourceInterface）。本桥即该装配面的
// 测试等价物——码注册校验/subject 边界/上下文规则全部走真实 factory。
// =====================================================================

class CatalogIngestBridge {
public:
    CatalogIngestBridge(diag::DiagnosticsFactory& factory,
                        diag::DiagCatalog& catalog, core::ObjectId projectObject)
        : m_factory(factory), m_catalog(catalog), m_projectObject(projectObject)
    {
    }

    /// 注入一条打开期恢复诊断（真实产出记录 → 补 subject → factory 校验
    /// → 目录追加；码注册失败/subject 违约在 factory 真链上抛）。
    void ingestOpenRecovery(const core::DiagnosticRecord& produced)
    {
        diag::DiagContext context;
        context.sourceUnit = "project";       ///< 来源单元（§4.2 必填 ≤32 字符）
        context.sourceInterface = "store.open";///< 来源通道（打开⑤步恢复扫描）
        // 记录重建：同码同文案＋项目身份 subject（subject 边界的装配补齐
        // ——文案三字段原样透传，零加工）。
        const core::DiagnosticRecord rebuilt = core::DiagnosticRecord::make(
            produced.code, m_projectObject, produced.localName,
            produced.runtimeName, produced.context, produced.cause,
            produced.recommendedAction);
        m_catalog.append(m_factory.create(rebuilt, context));
    }

    /// 注入一条中断任务标注诊断（execution T14 重建产物 → 补 subject＋
    /// execution 域必填 task 五元组〔§8.10：ownerUnit=="execution" 的码
    /// 缺合法 task 即 ContextMissing〕→ 目录追加）。
    void ingestInterruptedTask(const core::DiagnosticRecord& produced,
                               const core::TaskIdentity& task)
    {
        diag::DiagContext context;
        context.sourceUnit = "execution";       ///< 来源单元（execution 恢复扫描）
        context.sourceInterface = "recovery.scan";///< 来源通道（T14 重建）
        context.task = task;                    ///< execution 域码必填五元组（§8.10）
        const core::DiagnosticRecord rebuilt = core::DiagnosticRecord::make(
            produced.code, m_projectObject, produced.localName,
            produced.runtimeName, produced.context, produced.cause,
            produced.recommendedAction);
        m_catalog.append(m_factory.create(rebuilt, context));
    }

private:
    diag::DiagnosticsFactory& m_factory;///< 诊断工厂（真实校验链——非 owning）
    diag::DiagCatalog& m_catalog;       ///< 统一诊断目录（非 owning）
    core::ObjectId m_projectObject;     ///< 项目元数据对象（subject 边界的绑定值）
};

// =====================================================================
// L5 装配桥（测试等价物）：统一诊断目录 → workflow 恢复事实端口
// （workflow::IRecoveryFactPort 的桥接实现——场景有无判定来自目录
// snapshot 条目（PM-15"经统一诊断目录集成"的结构兑现：横幅取数与
// 诊断表/日志同源）；计数来自对端清单事实（RecoveryReport 清单长度/
// 中断条目数——目录去重折叠口径≠涉事条目口径，两口径不混用，
// RecoveryScenarioFact 注释即契约锚）。
// =====================================================================

class CatalogRecoveryFactBridge final : public workflow::IRecoveryFactPort {
public:
    const diag::DiagCatalog* catalog = nullptr;///< 统一诊断目录（非 owning）
    project::RecoveryReport report;            ///< 真实 open 产出（计数事实源）
    bool writable = true;                      ///< 会话写权限（store->writable() 透传）

    RecoveryFacts collectFacts() override
    {
        RecoveryFacts facts;
        facts.writable = writable;

        // 全量快照（DiagQuery{} 不过滤——恢复条目按稳定码逐字识别）。
        const std::vector<diag::DiagProjectionItem> view =
            catalog->snapshot(diag::DiagQuery{});
        bool hasIgnored = false;
        bool hasOrphan = false;
        std::size_t interruptedTasks = 0;
        for (const diag::DiagProjectionItem& item : view) {
            if (item.code == "PRJ-RECOVERY-IGNORED-UNCOMMITTED") {
                hasIgnored = true;  // 场景①目录事实
            } else if (item.code == "PRJ-RECOVERY-ORPHAN-DRAFT") {
                hasOrphan = true;   // 场景③目录事实
            } else if (item.code == "EX-TASK-INTERRUPTED") {
                // 中断条目计数＝目录折叠计数合计（每任务一条标注——
                // occurrences 即中断任务条目数）。
                interruptedTasks += item.occurrences;
            }
        }

        // 仅命中场景入清单（itemCount ≥ 1——无事实不入，端口契约）。
        if (hasIgnored && !report.ignoredStagingTxs.empty()) {
            facts.scenarios.push_back(RecoveryScenarioFact{
                RecoveryScenario::IgnoredUnfinishedSave,
                report.ignoredStagingTxs.size()});
        }
        if (interruptedTasks > 0) {
            facts.scenarios.push_back(RecoveryScenarioFact{
                RecoveryScenario::InterruptedTask, interruptedTasks});
        }
        if (hasOrphan && !report.orphanDraftFiles.empty()) {
            facts.scenarios.push_back(RecoveryScenarioFact{
                RecoveryScenario::OrphanDraft, report.orphanDraftFiles.size()});
        }
        return facts;
    }
};

// =====================================================================
// L5 装配桥（测试等价物）：真实 store + ui 会话半区 → 标题事实端口
// （workflow::ITitleFactPort 的桥接实现——displayName/writable/分支
// label 经真实 store 查询面取数；anyDirty 两源合并的磁盘半源经
// listDrafts 实测，会话脏半区归 ui 会话态（本契约无编辑会话＝恒 false）；
// 当前性投影经 ui C-7 值搬运（无正式评估＝nullopt 不可判定）。
// =====================================================================

class StoreTitleFactBridge final : public workflow::ITitleFactPort {
public:
    project::ProjectStore* store = nullptr;///< 当前存储上下文（非 owning）
    bool sessionDirty = false;             ///< ui 会话脏半区（本契约恒 false）
    ui::CurrentnessProjection currentness; ///< ui C-7 搬运位（无评估＝nullopt）

    TitleFacts collectFacts() const override
    {
        TitleFacts facts;
        // 显示名：project 权威（M.projectDisplayName——INV-M3 权威元数据）。
        facts.displayName =
            store->query().currentMetadata().record.projectDisplayName;
        // 写权限：INV-SES-1 唯一判定源（PM-07 降级/显式只读同位表达）。
        facts.writable = store->writable();
        // 当前方案：活动分支 label（本契约取首分支——黄金项目主分支；
        // 活动分支登记权威在宿主会话层，本桥以其测试等价物承载）。
        const std::vector<project::BranchTip> tips = store->query().branchTips();
        if (!tips.empty()) {
            facts.schemeLabel = tips.front().label;
        }
        // anyDirty 磁盘半源实测：逐分支草稿清单（.new/.bak 残留不列入——
        // listDrafts 扫描语义；残留算孤儿草稿场景不算未保存标记）。
        bool diskDraft = false;
        for (const project::BranchTip& tip : tips) {
            if (!store->query().listDrafts(tip.id).empty()) {
                diskDraft = true;
            }
        }
        facts.anyDirty = sessionDirty || diskDraft;
        facts.resultsCurrentness = currentness;
        return facts;
    }
};

/// T14 恢复期重建（真实 execution 工厂——场景②"任务已中断"条目的真实
/// 词形：Interrupted 终态＋EX-TASK-INTERRUPTED 状态标注诊断；Recovery-
/// ProcessContractTest::makeRecoverableRecord 同款受理记录底座）。
exe::TaskStateMachine buildInterruptedTaskMachine()
{
    exe::TaskRecord record;
    record.taskId = exe::TaskId::generate();
    record.submission.snapshot.project = core::ProjectId::generate();
    record.submission.snapshot.branch = core::BranchId::generate();
    record.submission.snapshot.revision = core::RevisionId::generate();
    record.submission.evaluatorKey = "kin-batch-ik";
    record.submission.contractVersion = 1;
    record.submission.mode = core::EvaluationMode::Verified;
    record.run = core::RunId::generate();       ///< 归档预留未终结的运行（工厂守卫必填）
    record.attempt = core::AttemptId::fromCanonical("att-1");///< 尝试 ≥1（工厂守卫）
    record.state = core::TaskState::Interrupted;///< 恢复扫描的指派结论（工厂守卫）
    return exe::TaskStateMachine::forRecoveryInterrupted(record, nullptr);
}

/// 从记录集提取指定码的首条（重建注入的源记录——真实产出文案透传）。
const core::DiagnosticRecord* findRecord(
    const std::vector<core::DiagnosticRecord>& records, const std::string& code)
{
    for (const core::DiagnosticRecord& r : records) {
        if (r.code == code) {
            return &r;
        }
    }
    return nullptr;
}

// =====================================================================
// WF-VER-218：恢复横幅三场景真实落盘＋统一诊断目录集成（PM-15/AT-21）
// =====================================================================

/// 三场景并存：①③经真实 open 恢复扫描（.staging 残留＋.new 孤儿草稿
/// 真实注入盘面——RecoveryReport 清单＋PRJ-RECOVERY-* 诊断真实产出），
/// ②经真实 execution T14 重建（EX-TASK-INTERRUPTED 真实词形）；统一诊断
/// 目录真链（码注册/subject 边界/上下文校验全真）折叠后，经
/// IStatusProjectionProvider 接口产出横幅数据：择一序（①为主文案）、
/// 详情行降级（②③）、三动作与可用位、计数透传——AT-21"残留草稿恢复、
/// 中断任务提示"观测点。
TEST_F(StatusBannerContract, ThreeScenarios_RealRecovery_CatalogFolded_BannerData_P15_AT21)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-15"}, std::vector<std::string>{"AT-21"});

    // ---- 排布：黄金项目落盘 → 注入场景①③盘面 → 释放写锁。
    const fs::path projectDir = m_dir / "banner-p15.rwdesign";
    {
        project::OpenStoreResult created = createGolden(projectDir, "恢复横幅契约黄金项目");
        ASSERT_TRUE(created.store != nullptr);
        ASSERT_TRUE(created.writable);
        injectStagingResidue(projectDir, "tx-left-open");   // 场景①（.staging 残留）
        injectOrphanDraftResidue(projectDir);               // 场景③（.new 孤儿残留）
        (void)created.store->requestClose();  // 显式弃值（nodiscard——释放写锁，返回在途数不参与断言）
        created.store.reset();                              // 释放写锁（重开走真实 open）
    }

    // ---- 真实 open（Writable＋捕获 sink）：⑤步恢复扫描真实产出。
    CapturingSink sink;
    project::OpenStoreRequest request;
    request.path = projectDir;
    request.diagnostics = &sink;
    const project::OpenStoreResult reopened =
        project::ProjectStoreFactory::open(request);
    ASSERT_TRUE(reopened.store != nullptr);
    ASSERT_TRUE(reopened.writable) << "无外部持锁——可写打开应成功";

    // 场景①清单事实（真实扫描：.staging 残留事务，忽略不删）。
    ASSERT_EQ(reopened.recovery.ignoredStagingTxs.size(), 1u);
    EXPECT_EQ(reopened.recovery.ignoredStagingTxs.at(0), "tx-left-open");
    // 场景③清单事实（真实扫描：.new 残留＝孤儿草稿，现场保留）。
    ASSERT_EQ(reopened.recovery.orphanDraftFiles.size(), 1u);
    EXPECT_EQ(reopened.recovery.orphanDraftFiles.at(0),
              (fs::path("drafts") / "main" / "mod.draft.json.new").string());
    // 打开期恢复诊断真实产出（PM-15"经统一诊断目录集成"的上游——对端
    // 已按双通道上报：清单随报告＋sink 即时）。
    EXPECT_TRUE(sink.hasCode("PRJ-RECOVERY-IGNORED-UNCOMMITTED"));
    EXPECT_TRUE(sink.hasCode("PRJ-RECOVERY-ORPHAN-DRAFT"));

    // ---- 场景②真实词形：execution T14 恢复期重建（Interrupted 终态＋
    // EX-TASK-INTERRUPTED 状态标注——"已中断可重跑"）。
    const exe::TaskStateMachine interrupted = buildInterruptedTaskMachine();
    ASSERT_TRUE(interrupted.record().termination.has_value());
    EXPECT_EQ(*interrupted.record().termination, exe::TerminationCause::Interrupted);
    const core::DiagnosticRecord* interruptedMark = findRecord(
        interrupted.record().diagnostics, "EX-TASK-INTERRUPTED");
    ASSERT_NE(interruptedMark, nullptr)
        << "T14 重建应携带 EX-TASK-INTERRUPTED 状态标注诊断（PM-15 场景②真实词形）";

    // ---- 统一诊断目录真链装配（码注册→seal→工厂→目录）＋L5 注入桥。
    diag::StableCodeRegistry registry;
    diag::registerBuiltinCodes(registry);
    registry.seal();
    diag::SystemClock clock;
    diag::DiagnosticsFactory factory(registry, clock);
    diag::DiagCatalog catalog;  // 默认护栏配置（P-DIAG-7 工程默认——零配置调用，见文件头留痕）
    CapturingDevLog devLog;
    diag::DiagnosticsSinkImpl sinkImpl(factory, catalog, devLog, "project", "store.open");
    (void)sinkImpl;  // 装配面自证（三方语义实例化——本契约经 factory 直注入目录）

    // subject 绑定值＝项目元数据对象（打开期唯一确定的项目作用对象——
    // 用户级 subject 边界的装配补齐点）。
    const core::ObjectId projectObject =
        reopened.store->query().currentMetadata().ref.objectId;
    ASSERT_TRUE(projectObject.isValid());
    CatalogIngestBridge bridge(factory, catalog, projectObject);

    // ①③：open 真实产出记录注入目录（真实 factory 校验链：码已注册＋
    // subject 合法＋params 与 schema 相符）。
    const core::DiagnosticRecord* ignoredRec =
        findRecord(sink.reports, "PRJ-RECOVERY-IGNORED-UNCOMMITTED");
    const core::DiagnosticRecord* orphanRec =
        findRecord(sink.reports, "PRJ-RECOVERY-ORPHAN-DRAFT");
    ASSERT_NE(ignoredRec, nullptr);
    ASSERT_NE(orphanRec, nullptr);
    ASSERT_NO_THROW(bridge.ingestOpenRecovery(*ignoredRec));
    ASSERT_NO_THROW(bridge.ingestOpenRecovery(*orphanRec));

    // ②：T14 重建标注注入目录（execution 域码必填五元组——真实 TaskRecord
    // 的提交快照＋run/attempt 组装）。
    core::TaskIdentity identity;
    identity.project = interrupted.record().submission.snapshot.project;
    identity.branch = interrupted.record().submission.snapshot.branch;
    identity.revision = interrupted.record().submission.snapshot.revision;
    identity.run = *interrupted.record().run;
    identity.attempt = interrupted.record().attempt;
    ASSERT_TRUE(identity.isValid());
    ASSERT_NO_THROW(bridge.ingestInterruptedTask(*interruptedMark, identity));

    // 目录侧真实观测：三条恢复条目在册（与诊断表/日志同源——PM-15 集成
    // 语义的目录面）。
    const std::vector<diag::DiagProjectionItem> catalogView =
        catalog.snapshot(diag::DiagQuery{});
    ASSERT_EQ(catalogView.size(), 3u);

    // ---- workflow 消费面：恢复事实桥（目录折叠）＋标题事实桥（真实
    // store 取数）→ O9 服务（经接口引用消费——接口路径钉扎）。
    CatalogRecoveryFactBridge recoveryBridge;
    recoveryBridge.catalog = &catalog;
    recoveryBridge.report = reopened.recovery;
    recoveryBridge.writable = reopened.writable;

    StoreTitleFactBridge titleBridge;
    titleBridge.store = reopened.store.get();
    // 黄金项目有孤儿 .new 残留但 listDrafts 不列入残留（扫描语义）——
    // anyDirty 磁盘半源为 false（无当前有效草稿）。
    titleBridge.sessionDirty = false;

    workflow::StatusProjectionProvider provider(titleBridge, recoveryBridge);
    workflow::IStatusProjectionProvider& iface = provider;

    // ---- 横幅数据断言（WF-VER-218 观测点：横幅数据经诊断目录集成）。
    const std::optional<workflow::RecoveryBannerData> banner = iface.recoveryBanner();
    ASSERT_TRUE(banner.has_value()) << "三场景并存——横幅必须呈现";
    // 择一序：场景①（未完成保存已忽略）紧迫度最高——主文案。
    EXPECT_EQ(banner->scenario, RecoveryScenario::IgnoredUnfinishedSave);
    EXPECT_EQ(banner->summaryKey, "ui.recovery.banner.summary.ignored-saves");
    EXPECT_EQ(banner->itemCount, 1u);  // .staging 残留事务数（真实清单长度）
    EXPECT_TRUE(banner->summaryArgs.empty());
    // 详情行降级（同紧迫度序：②中断在前、③草稿在后——不堆叠）。
    ASSERT_EQ(banner->detailKeys.size(), 2u);
    EXPECT_EQ(banner->detailKeys[0], "ui.recovery.banner.summary.interrupted");
    EXPECT_EQ(banner->detailKeys[1], "ui.recovery.banner.summary.orphan-drafts");
    // 三动作（PM-15 冻结三动作恒在场）＋可用位（可写会话——恢复/放弃双可用）。
    EXPECT_EQ(banner->actionDetailsKey, "ui.recovery.banner.action.details");
    EXPECT_EQ(banner->actionRestoreKey, "ui.recovery.banner.action.restore");
    EXPECT_EQ(banner->actionDiscardKey, "ui.recovery.banner.action.discard");
    EXPECT_TRUE(banner->restoreAvailable);
    EXPECT_TRUE(banner->discardAvailable);

    // ---- 标题数据断言（同会话——显示名真实来源＋可写无后缀位）。
    const workflow::TitleStatusData title = iface.titleStatus();
    EXPECT_EQ(title.displayName, "恢复横幅契约黄金项目");
    EXPECT_FALSE(title.readOnly);
    EXPECT_FALSE(title.dirty);  // 残留不是草稿（listDrafts 扫描语义）
    EXPECT_EQ(title.schemeLabel, "main");  // 黄金项目主分支 label（P-PR-8）
}

// =====================================================================
// WF-VER-217：标题栏来源真实链（PM-11——显示名/方案/只读位经真实 store）
// =====================================================================

/// 标题状态来源链：createNew 真实落盘 → store 查询面真实取数（显示名＝
/// M.projectDisplayName；方案＝主分支 label；写权限＝writable()）→
/// 投影组装 → 标题串逐字（可写无草稿＝纯显示名——PM-11 格式基线组合）。
/// 结果状态键＝过期（无正式评估＝不可判定——P-UI-2 归入 stale，绝不亮
/// "当前"）。
TEST_F(StatusBannerContract, TitleSources_FromRealStore_QueryFace_P11)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-11"}, std::vector<std::string>{});

    // ---- 排布：黄金项目（保持打开——可写会话）。
    const fs::path projectDir = m_dir / "title-p11.rwdesign";
    project::OpenStoreResult opened = createGolden(projectDir, "标题来源黄金项目");
    ASSERT_TRUE(opened.store != nullptr);
    ASSERT_TRUE(opened.writable);

    // 真实取数面自证（来源链对端事实）：显示名/主分支 label 与 createNew
    // 输入一致——投影透传的上游真实性。
    const project::ProjectMetadataView meta =
        opened.store->query().currentMetadata();
    EXPECT_EQ(meta.record.projectDisplayName, "标题来源黄金项目");
    const std::vector<project::BranchTip> tips = opened.store->query().branchTips();
    ASSERT_FALSE(tips.empty());
    EXPECT_EQ(tips.front().label, "main");

    // ---- L5 标题事实桥（真实 store 取数）＋O9 服务（接口消费）。
    StoreTitleFactBridge titleBridge;
    titleBridge.store = opened.store.get();

    // 空目录腿（无恢复事实）：真链装配但不注入任何条目——横幅折叠为
    // 空集（nullopt 直通）。
    diag::StableCodeRegistry emptyRegistry;
    diag::registerBuiltinCodes(emptyRegistry);
    emptyRegistry.seal();
    diag::SystemClock emptyClock;
    diag::DiagnosticsFactory emptyFactory(emptyRegistry, emptyClock);
    diag::DiagCatalog emptyCatalog;  // 空目录（默认护栏配置——P-DIAG-7 留痕同口径）
    CapturingDevLog emptyDevLog;
    diag::DiagnosticsSinkImpl emptySink(emptyFactory, emptyCatalog, emptyDevLog,
                                        "project", "store.open");
    (void)emptySink;

    CatalogRecoveryFactBridge recoveryBridge;  // 空事实腿
    recoveryBridge.catalog = &emptyCatalog;

    workflow::StatusProjectionProvider provider(titleBridge, recoveryBridge);
    workflow::IStatusProjectionProvider& iface = provider;

    const workflow::TitleStatusData title = iface.titleStatus();
    EXPECT_EQ(title.displayName, "标题来源黄金项目");
    EXPECT_FALSE(title.dirty);   // 无草稿无会话脏——`*` 位不亮
    EXPECT_FALSE(title.readOnly);// 可写——（只读）位不亮
    EXPECT_EQ(title.schemeLabel, "main");
    // 键半区：结果状态键＝stale（无评估＝不可判定——P-UI-2）。
    EXPECT_EQ(title.resultsStatusLabelKey, workflow::kResultsStatusLabelStaleKey);
    EXPECT_EQ(title.readonlySuffixKey, workflow::kTitleReadonlySuffixKey);
    EXPECT_EQ(title.schemeLabelKey, workflow::kStatusBarSchemeLabelKey);

    // 标题串逐字（PM-11 格式基线组合：`<显示名>` 单独——`*`/后缀全不亮）。
    EXPECT_EQ(workflow::buildTitleText(title, kReadonlySuffix),
              std::string("标题来源黄金项目"));

    // 无恢复事实——横幅不渲染（空目录折叠——nullopt 直通经接口）。
    EXPECT_FALSE(iface.recoveryBanner().has_value());

    (void)opened.store->requestClose();  // 显式弃值（nodiscard——释放写锁的幂等收尾，返回在途数不参与断言）
    opened.store.reset();
}

// =====================================================================
// PM-11 只读后缀＋PM-15 放弃门（真实降级链——writable 是唯一判定源）
// =====================================================================

/// 外部持锁降级只读（Win32 独占句柄模拟第二写者——内核裁决
/// ERROR_SHARING_VIOLATION→HeldByOther 降级，ReconnectReadOnlyContract
/// 同款口径）：writable=false 真实事实 → 标题串携带"（只读）"后缀；
/// 同会话孤儿草稿横幅的[放弃]动作禁用（写操作——ui §8.3-4）而[恢复草稿]
/// 保持可用（载入查看非写操作）——PM-11 后缀位与 PM-15 动作可用位同源
/// 于 writable 的真实联动。
TEST_F(StatusBannerContract, ReadOnlyDegraded_TitleSuffix_DiscardGate_P11_P15)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-11", "PM-15"},
                  std::vector<std::string>{"AT-21"});

    // ---- 排布：黄金项目＋场景③盘面 → 释放 → 外部独占句柄持锁。
    const fs::path projectDir = m_dir / "readonly-p11.rwdesign";
    {
        project::OpenStoreResult created = createGolden(projectDir, "只读后缀契约黄金项目");
        ASSERT_TRUE(created.store != nullptr);
        injectOrphanDraftResidue(projectDir);
        (void)created.store->requestClose();  // 显式弃值（nodiscard——释放写锁，返回在途数不参与断言）
        created.store.reset();
    }
    ScopedForeignLockHandle foreignLock(projectDir / "lock");
    ASSERT_TRUE(foreignLock.valid()) << "外部持锁面未建立（lock 文件不在位？）";

    // ---- 真实 open：Writable 请求被锁竞争降级只读（不阻塞等待——PM-07）。
    CapturingSink sink;
    project::OpenStoreRequest request;
    request.path = projectDir;
    request.diagnostics = &sink;
    const project::OpenStoreResult reopened =
        project::ProjectStoreFactory::open(request);
    ASSERT_TRUE(reopened.store != nullptr);
    EXPECT_FALSE(reopened.writable) << "外部持锁——应降级只读（PM-07 不阻塞等待）";
    ASSERT_EQ(reopened.recovery.orphanDraftFiles.size(), 1u)
        << "孤儿草稿残留应被真实扫描（场景③清单事实）";

    // ---- 统一诊断目录：场景③条目注入（真实产出记录——与用例一同链）。
    diag::StableCodeRegistry registry;
    diag::registerBuiltinCodes(registry);
    registry.seal();
    diag::SystemClock clock;
    diag::DiagnosticsFactory factory(registry, clock);
    diag::DiagCatalog catalog;
    CapturingDevLog devLog;
    diag::DiagnosticsSinkImpl sinkImpl(factory, catalog, devLog, "project", "store.open");
    (void)sinkImpl;

    const core::DiagnosticRecord* orphanRec =
        findRecord(sink.reports, "PRJ-RECOVERY-ORPHAN-DRAFT");
    ASSERT_NE(orphanRec, nullptr);
    const core::ObjectId projectObject =
        reopened.store->query().currentMetadata().ref.objectId;
    CatalogIngestBridge bridge(factory, catalog, projectObject);
    ASSERT_NO_THROW(bridge.ingestOpenRecovery(*orphanRec));

    // ---- workflow 消费面：只读事实贯通两条投影（标题＋横幅）。
    CatalogRecoveryFactBridge recoveryBridge;
    recoveryBridge.catalog = &catalog;
    recoveryBridge.report = reopened.recovery;
    recoveryBridge.writable = reopened.writable;  // false——真实降级事实

    StoreTitleFactBridge titleBridge;
    titleBridge.store = reopened.store.get();

    workflow::StatusProjectionProvider provider(titleBridge, recoveryBridge);
    workflow::IStatusProjectionProvider& iface = provider;

    // 标题串逐字：`<显示名>（只读）`（PM-11 只读后缀位＝writable 取反的
    // 真实来源——降级是 store 事实不是呈现偏好）。
    const workflow::TitleStatusData title = iface.titleStatus();
    EXPECT_EQ(title.displayName, "只读后缀契约黄金项目");
    EXPECT_TRUE(title.readOnly);
    EXPECT_EQ(workflow::buildTitleText(title, kReadonlySuffix),
              std::string("只读后缀契约黄金项目") + kReadonlySuffix);

    // 横幅：场景③命中＋[放弃]禁用（写操作×只读会话——ui §8.3-4）＋
    // [恢复草稿]可用（载入查看非写操作）。
    const std::optional<workflow::RecoveryBannerData> banner = iface.recoveryBanner();
    ASSERT_TRUE(banner.has_value());
    EXPECT_EQ(banner->scenario, RecoveryScenario::OrphanDraft);
    EXPECT_EQ(banner->summaryKey, "ui.recovery.banner.summary.orphan-drafts");
    ASSERT_EQ(banner->summaryArgs.size(), 1u);
    EXPECT_EQ(banner->summaryArgs[0], "1");  // {0}＝孤儿草稿计数（真实清单长度）
    EXPECT_TRUE(banner->restoreAvailable);
    EXPECT_FALSE(banner->discardAvailable)
        << "只读会话禁放弃（写操作）——可用位与 writable 真实联动";
}

}  // namespace
