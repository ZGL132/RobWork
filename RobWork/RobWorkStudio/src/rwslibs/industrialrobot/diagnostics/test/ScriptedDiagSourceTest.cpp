/**
 * @file   ScriptedDiagSourceTest.cpp
 * @brief  ScriptedDiagSource 替身回放与替身边界机检用例组（§11 DIAG-T10
 *         行产物——"按序列产出预设条目/finding/日志行"的行为自证＋
 *         替身边界声明（EV-REG-3 同模式）的机检面）。
 *
 * 设计依据：
 *   - units/diagnostics.md §10 头注（`_contract_test` 面"ScriptedDiagSource
 *     基座"——本替身与其消费套件落位于跨单元契约目标）、§11 DIAG-T10 行
 *     （完成条件："全部用例通过并留痕；替身边界声明"）
 *   - 任务契约 tasks/foundation/DIAG-T10.json acceptance 2（替身就位＋
 *     边界声明在案＋产品目标不链 testkit——本文件不 include 任何 testkit
 *     头，T-1 红线由 LinkageContractTest 沿用面＋本文件自持双面钉住）
 *   - 先例：evidence/test/EvaluatorPortSuiteTest.cpp 的 ScriptedDoubleBoundary
 *     机检面（EV-REG-3 的"文档声明＋机检两面"模式）
 *
 * 替身边界声明（全文见本目录 README.md §1；ScriptedDiagSource.hpp 文件头
 * 同款）：**替身输出仅验证 diagnostics 契约（条目/finding/日志行的产出
 * 路径与数据形状），不构成真实业务错误/worker 崩溃/磁盘故障等场景的
 * 正确性证明；脚本数据不得冒充真实诊断证据。**
 *
 * 线程约束：全部用例单线程（替身非线程安全——ScriptedDiagSource.hpp 类
 * 注释；日志管线内部线程为产品行为，经 flush 有界同步后断言）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/core/DiagData.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/diagnostics/Catalog.hpp>
#include <sdurws/ird/diagnostics/Confirmable.hpp>
#include <sdurws/ird/diagnostics/DiagCodes.hpp>
#include <sdurws/ird/diagnostics/Factory.hpp>
#include <sdurws/ird/diagnostics/Logging.hpp>

#include "ScriptedDiagSource.hpp"

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace {

namespace fs = std::filesystem;
using namespace sdurws::ird::diagnostics;
using sdurws::ird::diagnostics::testdoubles::ScriptedDiagSource;
using sdurws::ird::diagnostics::testdoubles::ScriptedFindingStep;
// 测试文件位于全局匿名 ns：`core` 是 sdurws::ird 的成员，using-directive 不
// 引入兄弟命名空间——以别名使 core::X 限定名可见（既有套件同款处理面）。
namespace core = sdurws::ird::core;
using sdurws::ird::core::BranchId;
using sdurws::ird::core::ObjectId;
using sdurws::ird::core::ProjectId;
using sdurws::ird::core::RevisionId;

// ---------------------------------------------------------------------
// 夹具辅助（与既有套件同风格——自持不共享，不引入共享测试库）
// ---------------------------------------------------------------------

/// 可确认类测试码（ConfirmableTest 同款——阶段 A 内置表全为 confirmable=false，
/// 按"业务域码随域卡注册"的既有机制注册：MDL 前缀所有权归 modeling）。
inline constexpr const char* kTravelLimitCode = "MDL-06-TRAVEL-LIMIT";

/// 确定性测试时钟（§4.2 IClock 注释——工厂时间戳与日志打点共用）。
class ManualClock final : public IClock {
public:
    std::chrono::system_clock::time_point nowUtc() const override { return m_now; }

private:
    std::chrono::system_clock::time_point m_now{std::chrono::seconds{3000000}};
};

/// 绑定复核环境探针桩（IConfirmationEnvironment——finding 服务构造必填；
/// 本套件不做确认提交，探针返回值不被触达，恒定值即可）。
class StubEnvironment final : public IConfirmationEnvironment {
public:
    core::RevisionId currentTipRevision(core::ProjectId, core::BranchId) const override
    {
        return m_tip;
    }
    std::optional<core::ContentIdentity> currentPolicyContentId(core::ProjectId,
                                                                core::BranchId) const override
    {
        return std::nullopt;
    }
    std::optional<core::ContentIdentity> currentCommandDigest(core::ProjectId,
                                                              core::BranchId) const override
    {
        return std::nullopt;
    }

private:
    core::RevisionId m_tip = RevisionId::generate();
};

/// 每用例独立临时目录（RAII 清理——LoggingTest 同款，日志文件观测面）。
class TempDirGuard {
public:
    TempDirGuard()
    {
        std::error_code ec;
        m_dir = fs::temp_directory_path(ec) / ("ird-diagscript-" + std::to_string(++s_seq));
        fs::create_directories(m_dir, ec);
    }
    ~TempDirGuard()
    {
        std::error_code ec;
        fs::remove_all(m_dir, ec);   // 尽力而为清理——失败不遮蔽用例结论
    }
    const fs::path& path() const { return m_dir; }

private:
    inline static int s_seq = 0;
    fs::path m_dir;
};

/// 组装并 seal 内置注册表＋可确认测试码（条目步/finding 步的真实校验面）。
void sealRegistry(StableCodeRegistry& registry)
{
    registerBuiltinCodes(registry);
    CodeDescriptor confirmable;
    confirmable.code = kTravelLimitCode;
    confirmable.ownerUnit = "modeling";
    confirmable.category = DiagnosticCategory::Confirmable;
    confirmable.severity = DiagnosticSeverity::Warning;
    confirmable.titleKey = "diag.mdl-06-travel-limit.title";   // P-DIAG-9 键约定
    confirmable.detailKey = "diag.mdl-06-travel-limit.detail";
    confirmable.paramSchema = "[]";                             // 无参数（必填字段显式声明）
    confirmable.confirmable = true;                             // 服务 create 前置锚点
    confirmable.requiresComparison = true;                      // confirmable⇒比较型
    confirmable.retryable = RetryKind::UserRetry;
    registry.registerCode(confirmable);
    registry.seal();
}

/// 合法用户级记录（正例契约形态基线——各步骤仅换码/subject）。
core::DiagnosticRecord makeUserRecord(const std::string& code, ObjectId subject)
{
    return core::DiagnosticRecord::make(
        code, subject, std::string("joint_5"), std::string("Robot.joint_5"),
        std::string("评估路径输入非法"), std::string("启用 Must 条目取值越域"),
        std::string("修正输入后重新评估"));
}

/// 合法比较值侧（Provided 数值＋用户来源＋单位——ConfirmableTest 同款）。
core::ComparativeValue makeComparativeValue(double number, const char* unitSymbol)
{
    const auto unit = core::UnitToken::find(unitSymbol);
    return core::ComparativeValue{
        core::SourcedValue<double>::provided(
            number, core::ValueProvenance::make(core::ProvenanceKind::UserProvided)),
        *unit};
}

/// 合法可确认 finding（比较型三要素——core C-1 工厂强制）。
core::ConfirmableFinding makeTravelLimitFinding(ObjectId subject)
{
    core::DiagnosticRecord record = core::DiagnosticRecord::make(
        kTravelLimitCode, subject, std::string("joint_5"), std::string("Robot.joint_5"),
        std::string("行程超限待确认"), std::string("实际行程 620mm 超过上限 550mm"),
        std::string("确认或修正行程参数"),
        core::ComparativeFields{makeComparativeValue(620.0, "mm"),
                                makeComparativeValue(550.0, "mm")});
    return core::ConfirmableFinding::make(std::move(record));
}

/// runtime 域上下文基线（sourceUnit/sourceInterface 必填——工厂 Usage 面）。
DiagContext makeBaseContext()
{
    DiagContext context;
    context.sourceUnit = "runtime";
    context.sourceInterface = "compile.workcell";
    return context;
}

/// execution 域上下文（EX 域码 create 前置：合法 task 五元组——§8.10；
/// 脚本步骤的契约形态数据须与码域前置一致）。
DiagContext makeExecutionContext()
{
    DiagContext context;
    context.sourceUnit = "execution";
    context.sourceInterface = "channel.error-report";
    core::TaskIdentity task;
    task.project = ProjectId::generate();
    task.branch = BranchId::generate();
    task.revision = RevisionId::generate();
    task.run = sdurws::ird::core::RunId::generate();
    task.attempt = sdurws::ird::core::AttemptId::fromCanonical("att-1");
    context.task = task;
    return context;
}

/// 读取整个文件（日志落盘观测辅助——文本模式按行读入字符串）。
std::string readFile(const fs::path& file)
{
    std::ifstream in(file);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

// ---------------------------------------------------------------------
// 套件夹具：真实产品装配（替身注资的四对象全部为产品实现——EV-REG-3
// 模式第 3 条"被测产品对象不做替身"的结构落点）
// ---------------------------------------------------------------------

class ScriptedDiagSourceSuite : public ::testing::Test {
protected:
    void SetUp() override
    {
        sealRegistry(m_registry);
        // 真实两级日志管线：FileLogFileOps＋临时目录（产品 I/O 路径——
        // 日志行内容经 flush 后的文件断言，非内存桩）。
        m_logger = std::make_unique<LoggingPipeline>(m_clock, m_fileOps);
        LogSinkConfig config;
        config.directory = m_tempDir.path();
        m_logger->configure(config);
        // 真实 finding 服务：转移日志注入真实管线（IDevLogSink 接线——
        // §5.4"每次状态转移写入开发级日志"的产品路径）。
        m_findings = std::make_unique<ConfirmableService>(m_registry, m_env, m_clock,
                                                          m_logger.get());
        m_factory = std::make_unique<DiagnosticsFactory>(m_registry, m_clock);
        m_source = std::make_unique<ScriptedDiagSource>(*m_factory, m_catalog,
                                                        *m_findings, *m_logger);
    }

    void TearDown() override
    {
        // 析构序：先停日志管线（有界排空）再释放服务/替身——引用生命周期
        // 后进先出（装配期注入契约）。
        m_source.reset();
        m_findings.reset();
        m_logger.reset();
    }

    /// flush 并返回 Tier-D 文件全文（日志步内容观测——产品落盘路径）。
    std::string flushAndReadDevLog()
    {
        EXPECT_TRUE(m_logger->flush(std::chrono::milliseconds{3000}));
        return readFile(m_tempDir.path() / "dev-diagnostics.log");
    }

    StableCodeRegistry m_registry;    ///< 码表（每用例独立——互不污染）
    ManualClock m_clock;              ///< 确定性时钟
    TempDirGuard m_tempDir;           ///< 日志临时目录
    FileLogFileOps m_fileOps;         ///< 真实文件操作（产品 I/O）
    std::unique_ptr<LoggingPipeline> m_logger;              ///< 真实日志管线
    StubEnvironment m_env;            ///< 探针桩（本套件不触达其返回值）
    std::unique_ptr<ConfirmableService> m_findings;         ///< 真实 finding 服务
    std::unique_ptr<DiagnosticsFactory> m_factory;          ///< 真实工厂
    DiagCatalog m_catalog;            ///< 真实目录
    std::unique_ptr<ScriptedDiagSource> m_source;           ///< 被测替身
};

// =====================================================================
// ScriptedReplay——按序列产出三形态（§11 DIAG-T10 行产物行为自证）
// =====================================================================

/// 混合脚本（条目→日志→finding→条目）按登记序逐步产出：每步后即断言
/// 产出事实（目录计数/entryId 序、服务 tryFind 命中、管线落盘行），证明
/// "按序列产出预设条目/finding/日志行"的步进语义与全局顺序。
TEST_F(ScriptedDiagSourceSuite, EntriesFindingsLogsProducedInScriptOrder)
{
    const ObjectId subject1 = ObjectId::generate();
    const ObjectId subject2 = ObjectId::generate();

    // 脚本登记（全局序）：Entry(RT-INPUT-INVALID) → Log(Dev 行) →
    // Finding(行程超限) → Entry(EX-WORKER-CRASHED)——三形态交错，步进序
    // 即断言序（"按序列产出"的观测口径）。
    m_source->scriptEntry(makeUserRecord("RT-INPUT-INVALID", subject1),
                          makeBaseContext());

    LogRecord logStep;
    logStep.tier = LogTier::Dev;
    logStep.level = LogLevel::Info;
    logStep.channel = "diag/test";                    // 通道 token（≤48 字符）
    logStep.message = "scripted-log-step-marker";     // 落盘扫描标记
    m_source->scriptLog(logStep);

    const ProjectId project = ProjectId::generate();
    const BranchId branch = BranchId::generate();
    const RevisionId baseRevision = RevisionId::generate();
    ScriptedFindingStep findingStep;
    findingStep.finding = makeTravelLimitFinding(subject2);
    findingStep.commandType = "apply.optimization";   // project 处理器 token（透传）
    findingStep.commandDigest = core::ContentIdentity::fromCanonical(
        "cid-1111111111111111111111111111111111111111111111111111111111111111");
    findingStep.project = project;
    findingStep.branch = branch;
    findingStep.baseRevisionId = baseRevision;
    findingStep.subjectScope = {subject2};
    m_source->scriptFinding(findingStep);

    m_source->scriptEntry(makeUserRecord("EX-WORKER-CRASHED", subject1),
                          makeExecutionContext());   // EX 域码前置 task 五元组（§8.10）

    // 回放前：目录空、无产出、脚本未动（前置状态钉住）。
    EXPECT_EQ(m_catalog.size(), 0u);
    EXPECT_EQ(m_source->replayedCount(), 0u);
    EXPECT_EQ(m_source->scriptedCount(), 4u);
    EXPECT_EQ(m_source->remainingCount(), 4u);

    // 第 1 步（条目）：目录恰 +1，entryId 记录为首个产出。
    ASSERT_TRUE(m_source->replayNext());
    EXPECT_EQ(m_catalog.size(), 1u);
    ASSERT_EQ(m_source->producedEntryIds().size(), 1u);
    EXPECT_EQ(m_source->producedEntryIds()[0], m_catalog.snapshot(DiagQuery{})[0].entryId);
    EXPECT_EQ(m_source->logsSent(), 0u);

    // 第 2 步（日志）：目录不变；行已提交（异步入队——计数即观测，内容
    // 断言在 flush 后统一进行）。
    ASSERT_TRUE(m_source->replayNext());
    EXPECT_EQ(m_catalog.size(), 1u);
    EXPECT_EQ(m_source->logsSent(), 1u);

    // 第 3 步（finding）：经真实服务创建——tryFind 命中且 Pending、绑定
    // 字段与脚本参数逐项一致（服务端绑定冻结的产品行为）。
    ASSERT_TRUE(m_source->replayNext());
    ASSERT_EQ(m_source->producedFindingIds().size(), 1u);
    const std::optional<FindingRecord> found =
        m_findings->tryFind(m_source->producedFindingIds()[0]);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->state, FindingState::Pending);
    EXPECT_EQ(found->sourceCommandType, "apply.optimization");
    EXPECT_EQ(found->baseRevisionId, baseRevision);
    EXPECT_EQ(found->project, project);
    EXPECT_EQ(found->branch, branch);

    // 第 4 步（条目）：目录再 +1（同 subject 异码不去重——§6.4 键面）。
    ASSERT_TRUE(m_source->replayNext());
    EXPECT_EQ(m_catalog.size(), 2u);
    ASSERT_EQ(m_source->producedEntryIds().size(), 2u);

    // 回放进度：4/4，耗尽（观测面自洽）。
    EXPECT_EQ(m_source->replayedCount(), 4u);
    EXPECT_EQ(m_source->remainingCount(), 0u);

    // 日志步内容落盘验证（真实管线 flush 后扫描标记——非内存桩）。
    const std::string devLog = flushAndReadDevLog();
    EXPECT_NE(devLog.find("scripted-log-step-marker"), std::string::npos)
        << "脚本日志行未落盘 Tier-D（实际内容：" << devLog << "）";
}

/// 脚本耗尽是有界正常终态：replayAll 一次回放全部并返回步数；再次
/// replayAll 为 0 步、replayNext 恒 false 且产出不再变化（无重复执行、
/// 无回绕——回放游标单调语义）。
TEST_F(ScriptedDiagSourceSuite, ExhaustedScriptIsBoundedNoOp)
{
    const ObjectId subject = ObjectId::generate();
    m_source->scriptEntry(makeUserRecord("RT-INPUT-INVALID", subject),
                          makeBaseContext());
    m_source->scriptEntry(makeUserRecord("RT-UNIT-MISMATCH", subject),
                          makeBaseContext());

    // 全量回放：返回值＝实际执行步数（2）；产出与进度一致。
    EXPECT_EQ(m_source->replayAll(), 2u);
    EXPECT_EQ(m_catalog.size(), 2u);
    EXPECT_EQ(m_source->replayedCount(), 2u);

    // 耗尽后重复回放：零步、零产出变化、replayNext 恒 false。
    EXPECT_EQ(m_source->replayAll(), 0u);
    EXPECT_FALSE(m_source->replayNext());
    EXPECT_EQ(m_catalog.size(), 2u);
    EXPECT_EQ(m_source->producedEntryIds().size(), 2u);
    EXPECT_EQ(m_source->replayedCount(), 2u);   // 游标不回绕
}

/// 空脚本回放为无害空操作（前置边界——不抛、零产出）。
TEST_F(ScriptedDiagSourceSuite, EmptyScriptReplaysNothing)
{
    EXPECT_EQ(m_source->scriptedCount(), 0u);
    EXPECT_FALSE(m_source->replayNext());
    EXPECT_EQ(m_source->replayAll(), 0u);
    EXPECT_EQ(m_catalog.size(), 0u);
    EXPECT_TRUE(m_source->producedEntryIds().empty());
    EXPECT_TRUE(m_source->producedFindingIds().empty());
    EXPECT_EQ(m_source->logsSent(), 0u);
}

// =====================================================================
// ScriptedDoubleBoundary——替身边界声明的机检面（EV-REG-3 同模式：
// 文档声明〔README.md §1〕＋替身头文件头声明＋本机检两面互为支撑）
// =====================================================================

/// 产品源码面零替身符号：递归扫描 src/＋include/ 断言 "ScriptedDiagSource"
/// 与 "testdoubles" 零命中——替身只存在于测试目标源码面（T-1 红线的替身
/// 侧对偶：替身绝不进入产品库/公共头；EV-REG-3"替身数据不进入任何持久
/// 形态"的 diagnostics 落点之一）。
TEST(ScriptedDoubleBoundary, ProductSourcesFreeOfDoubleSymbols)
{
    // IRD_DIAGNOSTICS_UNIT_ROOT＝industrialrobot 根（构建定义以单元名作子
    // 目录前缀——CMakeLists 注释），单元源码面＝<root>/diagnostics/{src,include}。
    const fs::path unitRoot = fs::path{IRD_DIAGNOSTICS_UNIT_ROOT} / "diagnostics";
    ASSERT_TRUE(fs::exists(unitRoot)) << "单元根不存在：" << unitRoot.string();

    std::size_t scanned = 0;   // 扫描计数——防"扫了个空目录"的恒真断言
    for (const char* sub : {"src", "include"}) {
        const fs::path root = unitRoot / sub;
        ASSERT_TRUE(fs::exists(root)) << "产品源码面缺失：" << root.string();
        for (const auto& entry : fs::recursive_directory_iterator(root)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".hpp"
                && entry.path().extension() != ".cpp") {
                continue;
            }
            std::ifstream in(entry.path());
            std::ostringstream buffer;
            buffer << in.rdbuf();
            const std::string text = buffer.str();
            EXPECT_EQ(text.find("ScriptedDiagSource"), std::string::npos)
                << "产品源码面出现替身符号：" << entry.path().string();
            EXPECT_EQ(text.find("testdoubles"), std::string::npos)
                << "产品源码面出现替身命名空间：" << entry.path().string();
            ++scanned;
        }
    }
    EXPECT_GE(scanned, 15u) << "扫描文件数异常偏少（src+include 应≥15 个文件）";
}

/// 替身边界声明在案（机检半区）：test/README.md §1 必须同时含四锚点——
/// "EV-REG-3"（同模式出处）、"仅验证"（边界正句）、"不构成"（能力否定
/// 句）、"ScriptedDiagSource"（替身点名）。缺任一即边界声明失守（acceptance 2）。
TEST(ScriptedDoubleBoundary, BoundaryDeclarationDocumentedInReadme)
{
    // 单元目录＝<industrialrobot 根>/diagnostics（构建定义以单元名作前缀）。
    const fs::path readme = fs::path{IRD_DIAGNOSTICS_UNIT_ROOT} / "diagnostics" / "test"
                          / "README.md";
    ASSERT_TRUE(fs::exists(readme)) << "替身边界声明文件缺失：" << readme.string();

    std::ifstream in(readme);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    const std::string text = buffer.str();

    EXPECT_NE(text.find("EV-REG-3"), std::string::npos) << "锚点缺失：EV-REG-3";
    EXPECT_NE(text.find("仅验证"), std::string::npos) << "锚点缺失：仅验证";
    EXPECT_NE(text.find("不构成"), std::string::npos) << "锚点缺失：不构成";
    EXPECT_NE(text.find("ScriptedDiagSource"), std::string::npos)
        << "锚点缺失：ScriptedDiagSource 点名";
}

/// 替身头文件头同款声明在案（EV-REG-3 两面模式的另一面——头文件本身
/// 携带边界声明，消费方不读 README 也能看到约束）。
TEST(ScriptedDoubleBoundary, HeaderCarriesBoundaryDeclaration)
{
    // 单元目录＝<industrialrobot 根>/diagnostics（构建定义以单元名作前缀）。
    const fs::path header = fs::path{IRD_DIAGNOSTICS_UNIT_ROOT} / "diagnostics" / "test"
                          / "ScriptedDiagSource.hpp";
    ASSERT_TRUE(fs::exists(header)) << "替身头缺失：" << header.string();

    std::ifstream in(header);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    const std::string text = buffer.str();

    EXPECT_NE(text.find("仅验证 diagnostics 契约"), std::string::npos)
        << "替身头缺边界正句";
    EXPECT_NE(text.find("不构成"), std::string::npos) << "替身头缺能力否定句";
    EXPECT_NE(text.find("EV-REG-3"), std::string::npos) << "替身头缺同模式出处";
}

}  // namespace
