/**
 * @file DiagnosticsRegressionTest.cpp
 * @brief 2026-10-28 专项评审回归：ERR-01、diagnostics §6.3/6.4/7.4/7.7/9.3/9.6。
 * 独立预期只使用合成输入；文件测试仅写本次临时目录，不承担真实 UI/worker 验收。
 */
#include <sdurws/ird/diagnostics/Aggregation.hpp>
#include <sdurws/ird/diagnostics/Confirmable.hpp>
#include <sdurws/ird/diagnostics/CrashReport.hpp>
#include <sdurws/ird/diagnostics/Factory.hpp>
#include <sdurws/ird/diagnostics/Logging.hpp>
#include <sdurws/ird/diagnostics/Redaction.hpp>
#include <gtest/gtest.h>
#include <atomic>
#include <condition_variable>
#include <future>
#include <thread>

namespace sdurws::ird::diagnostics {
namespace {
using namespace std::chrono_literals;
namespace fs = std::filesystem;

/// 固定 UTC 时钟；测试期间不修改，支持日志线程读取。
struct FixedClock final : IClock {
    std::chrono::system_clock::time_point nowUtc() const override
    { return std::chrono::system_clock::time_point{1000000s}; }
};

/// 工厂借持的码表和时钟先构造、后析构。
struct Rig {
    StableCodeRegistry registry;
    FixedClock clock;
    DiagnosticsFactory factory{registry, clock};
    Rig() { registerBuiltinCodes(registry); }
};

/// 生成合法结构化来源，避免测试被无关前置校验挡住。
DiagContext context()
{
    DiagContext c;
    c.sourceUnit = "diagnostics";
    c.sourceInterface = "regression";
    return c;
}

/// 每次独立对象，不复用生产聚合 helper 生成预期。
core::DiagnosticRecord record(std::string code = "RT-INPUT-INVALID")
{
    return core::DiagnosticRecord::make(code, core::ObjectId::generate(), {}, {},
                                        "synthetic context", "synthetic cause", "synthetic action");
}

/// 接缝收集真实编码后的日志；同步由 mutex 保护。
struct MemoryOps : ILogFileOps {
    std::mutex mutex;
    std::vector<std::string> lines;
    bool appendLine(const fs::path&, std::string_view text) override
    {
        std::lock_guard<std::mutex> lock(mutex);
        lines.emplace_back(text);
        return true;
    }
    bool flushFile(const fs::path&) override { return true; }
    bool rotateFile(const fs::path&, std::uint32_t) override { return true; }
    std::uint64_t fileSize(const fs::path&) override { return 0; }
};

/// 禁止采样/节流掩盖被测出口；文件接缝不访问此合成目录。
LogSinkConfig config()
{
    LogSinkConfig c;
    c.directory = "synthetic-regression";
    c.userEnabled = false;
    c.throttleWindowSeconds = 0;
    c.sampleEveryDebug = 1;
    c.flushEveryLines = 0;
    c.flushInterval = 0ms;
    return c;
}

/// 普通开发消息（不是内部授权凭据）。
LogRecord message(std::string text)
{
    LogRecord r;
    r.tier = LogTier::Dev;
    r.level = LogLevel::Info;
    r.channel = "regression";
    r.message = std::move(text);
    return r;
}

TEST(DiagnosticsRegression, DescriptorEnumsRejectAtomically)
{
    Rig rig;
    const auto good = *rig.registry.find("RT-INPUT-INVALID");
    for (int field = 0; field < 3; ++field) {
        StableCodeRegistry registry;
        auto bad = good;
        if (field == 0) bad.category = static_cast<DiagnosticCategory>(255);
        if (field == 1) bad.severity = static_cast<DiagnosticSeverity>(255);
        if (field == 2) bad.retryable = static_cast<RetryKind>(255);
        EXPECT_THROW(registry.registerCode(bad), DiagnosticsError) << field;
        EXPECT_EQ(registry.find(good.code), nullptr);
        EXPECT_NO_THROW(registry.registerCode(good));
    }
}

TEST(DiagnosticsRegression, FactoryUsesAuthoritativeLevelWithoutMutatingInput)
{
    Rig rig;
    auto raw = record();
    raw.level = core::DiagnosticLevel::Warning;
    auto entry = rig.factory.create(raw, context());
    EXPECT_EQ(entry.record.level, core::DiagnosticLevel::Error);
    EXPECT_EQ(raw.level, core::DiagnosticLevel::Warning);
    raw = record("RT-CANCELLED"); // 非 Error 码的默认 Error 输入同样须规范化。
    entry = rig.factory.create(raw, context());
    EXPECT_EQ(entry.record.level, core::DiagnosticLevel::Warning);
    EXPECT_EQ(raw.level, core::DiagnosticLevel::Error);
}

TEST(DiagnosticsRegression, TranslationTruncatesAtUtf8Boundary)
{
    Rig rig;
    // 前缀余量不足一个汉字；允许舍弃整字，禁止保留破碎字节。
    const std::runtime_error error(std::string(511, 'a') + u8"中文");
    const auto entry = rig.factory.translate(error, context());
    EXPECT_EQ(entry.record.cause.substr(0, 511), std::string(511, 'a'));
    EXPECT_EQ(entry.record.cause.substr(511, 3), u8"…"); // 下一完整字符为截断标记，非残缺的“中”。
}

TEST(DiagnosticsRegression, IncomingRelationsProtectConsumedRoot)
{
    for (int relation = 0; relation < 3; ++relation) {
        Rig rig;
        DiagCatalog catalog;
        catalog.configureCapacity({true, 1});
        auto raw = record("RT-CANCELLED");
        raw.level = rig.registry.find(raw.code)->level;
        auto root = rig.factory.create(raw, context());
        catalog.append(root);
        catalog.markConsumed(root.entryId);
        auto child = rig.factory.create(record(), context());
        if (relation == 0) child.causedBy = root.entryId;
        if (relation == 1) child.relatedTo.push_back(root.entryId);
        if (relation == 2) child.supersedes = root.entryId;
        catalog.append(child);
        EXPECT_EQ(catalog.size(), 2u) << relation;
    }
}

TEST(DiagnosticsRegression, CatalogSortsBeforeSnapshotAndExportLimit)
{
    Rig rig;
    DiagCatalog catalog;
    auto a = rig.factory.create(record(), context());
    auto b = rig.factory.create(record("RT-RESOURCE-MISSING"), context());
    catalog.append(b);  // 确定性交错：先创建者迟到，无需概率性线程测试。
    catalog.append(a);
    const auto snapshot = catalog.snapshot({});
    ASSERT_EQ(snapshot.size(), 2u);
    EXPECT_EQ(snapshot.front().entryId, a.entryId);
    EXPECT_EQ(catalog.exportSafeSummary({}, 1).find(a.record.code), 0u);
}

TEST(DiagnosticsRegression, CauseGroupsRemainPartitionedByTask)
{
    Rig rig;
    DiagCatalog catalog;
    auto aContext = context(), bContext = context();
    core::TaskIdentity task{core::ProjectId::generate(), core::BranchId::generate(),
        core::RevisionId::generate(), core::RunId::generate(), core::AttemptId::fromCanonical("att-1")};
    aContext.task = task;
    task.attempt = core::AttemptId::fromCanonical("att-2");
    bContext.task = task;
    auto root = rig.factory.create(record(), aContext);
    auto a = rig.factory.create(record("RT-RESOURCE-MISSING"), aContext);
    auto b = rig.factory.create(record("RT-RESOURCE-MISSING"), bContext);
    a.causedBy = b.causedBy = root.entryId;
    catalog.append(root);
    catalog.append(a);
    catalog.append(b);
    DiagnosticAggregator aggregator;
    const auto groups = aggregator.aggregate({root, a, b}, {});
    ASSERT_EQ(groups.size(), 3u);
    for (const auto& group : groups) EXPECT_EQ(group.members.size(), 1u);
}

TEST(DiagnosticsRegression, JsonAndBareKeysRedactIdempotently)
{
    RedactionService redaction;
    for (const auto raw : {R"({"password":"FakePassword918"})", "key=FakeKey927",
                           R"({'token':'FakeToken928'})", "apiKey=FakeApi929"}) {
        const auto safe = redaction.redact(raw, LogTier::Dev);
        EXPECT_EQ(safe.find("Fake"), std::string::npos) << safe;
        EXPECT_EQ(redaction.redact(safe, LogTier::Dev), safe);
    }
}

TEST(DiagnosticsRegression, InternalNamesAndMetadataCannotBypassRedaction)
{
    FixedClock clock;
    MemoryOps ops;
    LoggingPipeline pipeline(clock, ops);
    pipeline.configure(config());
    pipeline.attachRedactionService(std::make_shared<RedactionService>());
    auto r = message("password=FakeInternal929");
    r.channel = "diag/logging";
    pipeline.log(r);
    r = message("safe");
    r.threadTag = "password=FakeTag930";
    r.code = "key=FakeCode932";
    r.channel = "review\nFORGED password=FakeChan931";
    pipeline.log(r);
    core::TaskIdentity task{core::ProjectId::generate(), core::BranchId::generate(),
        core::RevisionId::generate(), core::RunId::generate(), core::AttemptId::fromCanonical("att-1")};
    pipeline.replayWorkerBatch({1, task, 0, true, {r}});
    ASSERT_TRUE(pipeline.flush(2s));
    const auto tail = pipeline.devTailSnapshot(20);
    ASSERT_GE(tail.size(), 3u);
    for (const auto& line : tail) {
        EXPECT_EQ(line.find("Fake"), std::string::npos) << line;
        EXPECT_EQ(line.find('\n'), std::string::npos);
        EXPECT_EQ(line.find('\r'), std::string::npos);
    }
}

TEST(DiagnosticsRegression, FinalBatchDiscardsBufferedLaterRecords)
{
    FixedClock clock;
    MemoryOps ops;
    LoggingPipeline pipeline(clock, ops);
    pipeline.configure(config());
    core::TaskIdentity task{core::ProjectId::generate(), core::BranchId::generate(),
        core::RevisionId::generate(), core::RunId::generate(), core::AttemptId::fromCanonical("att-1")};
    pipeline.replayWorkerBatch({1, task, 1, false, {message("LATE_AFTER_FINAL")}});
    pipeline.replayWorkerBatch({1, task, 0, true, {message("FINAL")}});
    ASSERT_TRUE(pipeline.flush(2s));
    std::string all;
    for (const auto& line : pipeline.devTailSnapshot(20)) {
        EXPECT_EQ(line.find("LATE_AFTER_FINAL"), std::string::npos);
        all += line;
    }
    EXPECT_NE(all.find("msg=FINAL"), std::string::npos);
    EXPECT_NE(all.find(u8"陈旧批次 1 个"), std::string::npos);
}

TEST(DiagnosticsRegression, RedactionFailureNotificationCannotFeedItself)
{
    struct FailingRedactor final : RedactionService {
        mutable std::atomic<int> calls{0};
        explicit FailingRedactor(IDevLogSink* sink) : RedactionService({}, sink) {}
        void applyRules(std::string&, LogTier, RedactionCounters&) const override
        { ++calls; throw std::runtime_error("synthetic redaction failure"); }
    };
    FixedClock clock;
    MemoryOps ops;
    LoggingPipeline pipeline(clock, ops);
    pipeline.configure(config());
    auto redaction = std::make_shared<FailingRedactor>(&pipeline);
    pipeline.attachRedactionService(redaction);
    pipeline.log(message("password=FakeFailure934"));
    ASSERT_TRUE(pipeline.flush(1s));
    EXPECT_LE(redaction->calls.load(), 4);
    const auto tail = pipeline.devTailSnapshot(20);
    ASSERT_GE(tail.size(), 2u);
    for (const auto& line : tail) EXPECT_EQ(line.find("FakeFailure934"), std::string::npos);
}

TEST(DiagnosticsRegression, ConcurrentFlushesCompleteAfterQueuedWrites)
{
    FixedClock clock;
    MemoryOps ops;
    LoggingPipeline pipeline(clock, ops);
    pipeline.configure(config());
    for (int i = 0; i < 20; ++i) pipeline.log(message("ordered " + std::to_string(i)));
    auto first = std::async(std::launch::async, [&] { return pipeline.flush(1s); });
    auto second = std::async(std::launch::async, [&] { return pipeline.flush(1s); });
    EXPECT_TRUE(first.get());
    EXPECT_TRUE(second.get());
    std::lock_guard<std::mutex> lock(ops.mutex);
    EXPECT_EQ(ops.lines.size(), 20u);
}

TEST(DiagnosticsRegression, SelectedSampleClearsEarlierTail)
{
    FixedClock clock;
    for (const int count : {4, 5}) {
        MemoryOps ops;
        {
            LoggingPipeline pipeline(clock, ops);
            auto c = config();
            c.sampleEveryDebug = 4;
            pipeline.configure(c);
            for (int i = 1; i <= count; ++i) {
                auto r = message("SAMPLE_" + std::to_string(i));
                r.level = LogLevel::Debug;
                pipeline.log(r);
            }
        }
        std::string all;
        for (const auto& line : ops.lines) all += line;
        EXPECT_NE(all.find("SAMPLE_1"), std::string::npos);
        EXPECT_NE(all.find("SAMPLE_4"), std::string::npos);
        EXPECT_EQ(all.find("SAMPLE_3"), std::string::npos);
        EXPECT_EQ(all.find("SAMPLE_5") != std::string::npos, count == 5);
    }
}

TEST(DiagnosticsRegression, FileAndCrashWriterAcceptNativeUnicodePaths)
{
    FixedClock clock;
    FileLogFileOps ops;
    RedactionService redaction;
    const auto dir = fs::temp_directory_path()
        / fs::u8path(u8"ird 机器人 🤖 " + core::ObjectId::generate().toCanonical());
    ASSERT_TRUE(fs::create_directory(dir));
    const auto file = dir / "test.log";
    EXPECT_TRUE(ops.appendLine(file, "synthetic"));
    EXPECT_TRUE(fs::exists(file));
    CrashReportWriter writer(redaction, clock, ops);
    CrashReportInput input;
    input.processId = 77;
    input.exceptionMessage = "synthetic";
    const auto report = writer.write(input, dir);
    EXPECT_TRUE(report.has_value());
    if (report) { EXPECT_TRUE(fs::exists(*report)); fs::remove(*report); }
    fs::remove(file);
    fs::remove(dir);  // 只删除本测试创建的已知文件及空目录，不递归清理。
}

TEST(DiagnosticsRegression, FlushDeadlineIncludesFileOperation)
{
    struct SlowOps final : MemoryOps {
        bool flushFile(const fs::path&) override { std::this_thread::sleep_for(250ms); return true; }
    } ops;
    FixedClock clock;
    LoggingPipeline pipeline(clock, ops);
    pipeline.configure(config());
    pipeline.log(message("synthetic"));
    const auto start = std::chrono::steady_clock::now();
    EXPECT_FALSE(pipeline.flush(20ms));
    EXPECT_LT(std::chrono::steady_clock::now() - start, 150ms);
}

TEST(DiagnosticsRegression, ShutdownStopsQueuedWorkAtDrainBudget)
{
    struct SlowOps final : MemoryOps {
        bool appendLine(const fs::path& file, std::string_view line) override
        {
            std::this_thread::sleep_for(20ms);
            return MemoryOps::appendLine(file, line);
        }
    } ops;
    FixedClock clock;
    auto pipeline = std::make_unique<LoggingPipeline>(clock, ops);
    pipeline->configure(config());
    for (int i = 0; i < 210; ++i) pipeline->log(message("synthetic " + std::to_string(i)));
    const auto start = std::chrono::steady_clock::now();
    pipeline.reset();
    EXPECT_LT(std::chrono::steady_clock::now() - start, 3400ms);
    EXPECT_LT(ops.lines.size(), 210u);
}

TEST(DiagnosticsRegression, OverflowSummaryDoesNotMisclassifyHighPriorityDrops)
{
    struct GateOps final : MemoryOps {
        std::mutex gate;
        std::condition_variable cv;
        bool entered = false, released = false;
        bool appendLine(const fs::path& file, std::string_view line) override
        {
            std::unique_lock<std::mutex> lock(gate);
            entered = true;
            cv.notify_all();
            cv.wait_for(lock, 2s, [&] { return released; });
            lock.unlock();
            return MemoryOps::appendLine(file, line);
        }
    } ops;
    FixedClock clock;
    LoggingPipeline pipeline(clock, ops);
    auto c = config();
    c.queueCapacity = 2;
    pipeline.configure(c);
    auto r = message("INFLIGHT");
    r.level = LogLevel::Error;
    pipeline.log(r);
    {
        std::unique_lock<std::mutex> lock(ops.gate);
        ASSERT_TRUE(ops.cv.wait_for(lock, 2s, [&] { return ops.entered; }));
    }
    for (auto text : {"QUEUED_A", "QUEUED_B", "DROPPED_C"}) {
        r.message = text;
        pipeline.log(r);
    }
    {
        std::lock_guard<std::mutex> lock(ops.gate);
        ops.released = true;
        ops.cv.notify_all();
    }
    ASSERT_TRUE(pipeline.flush(2s));
    std::string summary;
    for (const auto& line : pipeline.devTailSnapshot(20)) {
        if (line.find("[log-overflow]") != std::string::npos) summary += line;
    }
    EXPECT_NE(summary.find(u8"丢弃队列项 1 条"), std::string::npos);
    EXPECT_EQ(summary.find("Debug/Trace"), std::string::npos);
}
}  // namespace
}  // namespace sdurws::ird::diagnostics
