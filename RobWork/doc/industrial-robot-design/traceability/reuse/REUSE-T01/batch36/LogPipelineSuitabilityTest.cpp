/**
 * @file LogPipelineSuitabilityTest.cpp
 * @brief REUSE-T01 B36：采样收尾、worker收尾边界与既有线程候选的只读探测。
 *
 * 设计依据：Logging.hpp首条/每N条/末条及final批定义，units/diagnostics.md §7.4/7.5。
 * 复用原管线、FileLogFileOps、SystemClock、testkit TempDir、上游IOUtil/线程设施。
 * 不实现第二日志器；对象由测试线程持有，日志线程随真实管线析构回收。
 */
#include <gtest/gtest.h>
#include <rw/core/IOUtil.hpp>
#include <rw/common/ThreadPool.hpp>
#include <rw/common/ThreadSafeQueue.hpp>
#include <boost/function.hpp> // ThreadPool公共头只前置声明WorkFunction；沿上游ThreadTest补完整类型。
#include <sdurws/ird/diagnostics/Logging.hpp>
#include <sdurws/ird/testkit/Fixture.hpp>
#include <thread>

namespace {
namespace diag = sdurws::ird::diagnostics;
namespace core = sdurws::ird::core;
using sdurws::ird::testkit::TempDir;

/** @brief 复用上游IOUtil读原始日志；path[in]为已关闭测试文件，失败沿上游异常传播。
 * @return 原字节字符串；无格式解析或黄金值重产。
 */
std::string readLog(const std::filesystem::path& path)
{
    std::vector<char> bytes;
    rw::core::IOUtil::readFile(path.string(), bytes);
    return std::string(bytes.begin(), bytes.end());
}

/** @brief 构造合法测试五元组；返回全字段有效身份，不重实现任何身份规则。
 * 仅调用已有core生成入口；测试只比隔离性，不依赖随机值具体字节。
 */
core::TaskIdentity makeTask()
{
    core::TaskIdentity task;
    task.project = core::ProjectId::generate();
    task.branch = core::BranchId::generate();
    task.revision = core::RevisionId::generate();
    task.run = core::RunId::generate();
    task.attempt = core::AttemptId{1};
    return task;
}

/** @brief 生成单行合法worker批；task/seq/final/message[in]均由测试场景指定。
 * @return 原公开DTO；不执行排序、重放或协议判断。
 */
diag::WorkerLogBatch batch(core::TaskIdentity task, std::uint64_t seq,
                          bool final, std::string message)
{
    diag::WorkerLogBatch out;
    out.workerId = 11;
    out.task = std::move(task);
    out.seq = seq;
    out.final = final;
    diag::LogRecord record;
    record.tier = diag::LogTier::Dev;
    record.level = diag::LogLevel::Info;
    record.channel = "audit/replay";
    record.message = std::move(message);
    out.records.push_back(std::move(record));
    return out;
}

TEST(LogPipelineAudit, AlreadyEmittedLastSampleDoesNotReplayEarlierDrop_DT_LOG_SAMPLE)
{
    TempDir dir("b36-sample");
    diag::FileLogFileOps ops;
    diag::SystemClock clock;
    {
        diag::LoggingPipeline logger(clock, ops);
        diag::LogSinkConfig cfg;
        cfg.directory = dir.path();
        cfg.userEnabled = false;
        cfg.rotateMaxBytes = 0;
        cfg.sampleEveryDebug = 3;
        logger.configure(cfg);
        logger.logDev("audit/sample", diag::LogLevel::Debug, "sample-first");
        logger.logDev("audit/sample", diag::LogLevel::Debug, "sample-middle");
        logger.logDev("audit/sample", diag::LogLevel::Debug, "sample-last");
        ASSERT_TRUE(logger.flush(diag::kLogDefaultFlushDeadline));
    } // 原管线的析构兑现末条；依赖ops/clock仍活着。
    const auto bytes = readLog(dir.path() / "dev-diagnostics.log");
    EXPECT_NE(bytes.find("sample-first"), std::string::npos);
    EXPECT_NE(bytes.find("sample-last"), std::string::npos);
    EXPECT_EQ(bytes.find("sample-middle"), std::string::npos)
        << "DT-LOG采样最后一条已落盘，不应收尾补出更早被采掉的中间条";
}

TEST(LogPipelineAudit, BufferedBatchAfterFinalSequenceIsStale_DT_LOG_REPLAY)
{
    TempDir dir("b36-final");
    diag::FileLogFileOps ops;
    diag::SystemClock clock;
    {
        diag::LoggingPipeline logger(clock, ops);
        diag::LogSinkConfig cfg;
        cfg.directory = dir.path();
        cfg.userEnabled = false;
        cfg.rotateMaxBytes = 0;
        logger.configure(cfg);
        const auto task = makeTask();
        logger.replayWorkerBatch(batch(task, 1, false, "after-final-invalid"));
        logger.replayWorkerBatch(batch(task, 0, true, "final-zero"));
        ASSERT_TRUE(logger.flush(diag::kLogDefaultFlushDeadline));
    }
    const auto bytes = readLog(dir.path() / "dev-diagnostics.log");
    EXPECT_NE(bytes.find("final-zero"), std::string::npos);
    EXPECT_EQ(bytes.find("after-final-invalid"), std::string::npos)
        << "final定义之后无更多批次，较高序号缓冲必须按陈旧批处理";
}

TEST(LogPipelineAudit, SameWorkerDifferentTaskStreamsRemainIndependent_DT_LOG_REPLAY)
{
    TempDir dir("b36-streams");
    diag::FileLogFileOps ops;
    diag::SystemClock clock;
    diag::LoggingPipeline logger(clock, ops);
    diag::LogSinkConfig cfg;
    cfg.directory = dir.path();
    cfg.userEnabled = false;
    cfg.rotateMaxBytes = 0;
    logger.configure(cfg);
    const auto a = makeTask();
    const auto b = makeTask();
    logger.replayWorkerBatch(batch(a, 1, true, "stream-a-final"));
    logger.replayWorkerBatch(batch(b, 0, true, "stream-b-zero"));
    ASSERT_TRUE(logger.flush(diag::kLogDefaultFlushDeadline));
    const auto bytes = readLog(dir.path() / cfg.devFileName);
    EXPECT_NE(bytes.find("stream-a-final"), std::string::npos);
    EXPECT_NE(bytes.find("stream-b-zero"), std::string::npos);
    EXPECT_NE(bytes.find("EX-CHANNEL-PROTOCOL-ERROR"), std::string::npos)
        << "A缺0应补条，不污染同worker下B的独立流";
}

TEST(LogPipelineAudit, ExistingQueueAndZeroThreadPoolSemantics_REUSE_UTIL_THREAD)
{
    rw::common::ThreadSafeQueue<int> queue;
    queue.push(7);
    queue.push(8);
    int out = 0;
    ASSERT_TRUE(queue.try_pop(&out));
    EXPECT_EQ(out, 7);
    ASSERT_TRUE(queue.try_pop(&out));
    EXPECT_EQ(out, 8);
    EXPECT_FALSE(queue.try_pop(&out)); // 不调用空队列阻塞pop；此原语没有关闭/deadline协议。
    const auto caller = std::this_thread::get_id();
    std::thread::id actual;
    rw::common::ThreadPool pool(0);
    pool.addWork([&](rw::common::ThreadPool*) { actual = std::this_thread::get_id(); });
    EXPECT_EQ(actual, caller) << "上游threads=0在提交线程执行，不能当日志专用线程";
    pool.waitForEmptyQueue();
}
} // namespace
