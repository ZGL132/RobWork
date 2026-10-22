/**
 * @file LogFileSuitabilityTest.cpp
 * @brief REUSE-T01 B35：复用原日志API/原测试，验证输出原语与轮转边界。
 *
 * 依据：units/diagnostics.md §7.6/§9.6、Logging.hpp ILogFileOps契约。
 * 仅测试侧探测，不实现第二日志管线；TempDir与报告器直接复用testkit。
 * 每个对象仅在本测试线程创建/销毁；LoggingPipeline内部线程由原实现管理。
 */
#include <gtest/gtest.h>
#include <rw/core/LogStreamWriter.hpp>
#include <rw/common/LogFileWriter.hpp>
#include <sdurws/ird/diagnostics/Logging.hpp>
#include <sdurws/ird/testkit/Fixture.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>

namespace {
namespace diag = sdurws::ird::diagnostics;
namespace fs = std::filesystem;
using sdurws::ird::testkit::TempDir;

/** @brief 读取已关闭文件的原始字节；路径[in]由TempDir持有，失败返回空串。
 * 不解析或重产日志格式，避免期望与产品同源。
 */
std::string readBytes(const fs::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input), {});
}

TEST(LogFileAudit, KeepOneIncludesActiveFile_DT_LOG_ROTATE)
{
    TempDir dir("b35-keep-one");
    diag::FileLogFileOps ops;
    diag::SystemClock clock;
    diag::LoggingPipeline logger(clock, ops);
    diag::LogSinkConfig cfg;
    cfg.directory = dir.path();
    cfg.userEnabled = false;
    cfg.rotateFiles = 1; // 公共契约允许1：总数包括活动文件，不能额外保留.1。
    cfg.rotateMaxBytes = 1;
    logger.configure(cfg);
    logger.logDev("audit/rotate", diag::LogLevel::Info, "first");
    logger.logDev("audit/rotate", diag::LogLevel::Info, "second");
    ASSERT_TRUE(logger.flush(diag::kLogDefaultFlushDeadline));
    const auto base = dir.path() / cfg.devFileName;
    ASSERT_TRUE(fs::exists(base));
    EXPECT_FALSE(fs::exists(fs::path(base.string() + ".1")))
        << "DT-LOG：rotateFiles=1应只剩活动文件";
    EXPECT_NE(readBytes(base).find("second"), std::string::npos);
}

TEST(LogFileAudit, ReducedRetentionRemovesOldGenerations_DT_LOG_ROTATE)
{
    TempDir dir("b35-shrink");
    diag::FileLogFileOps ops;
    const auto base = dir.path() / "retention.log";
    // 世代生成只调用真实文件接缝，不写第二份移位算法；先验证旧配置下存在四代。
    for (int i = 0; i < 5; ++i) {
        ASSERT_TRUE(ops.appendLine(base, "old-" + std::to_string(i)));
        ASSERT_TRUE(ops.rotateFile(base, 5));
    }
    ASSERT_TRUE(fs::exists(fs::path(base.string() + ".4")));
    ASSERT_TRUE(ops.appendLine(base, "before-shrink"));
    ASSERT_TRUE(ops.rotateFile(base, 2));
    ASSERT_TRUE(ops.appendLine(base, "after-shrink"));
    EXPECT_FALSE(fs::exists(fs::path(base.string() + ".2"))) << "总保留数降到2后旧二代应清除";
    EXPECT_FALSE(fs::exists(fs::path(base.string() + ".3"))) << "旧三代应清除";
    EXPECT_FALSE(fs::exists(fs::path(base.string() + ".4"))) << "旧四代应清除";
    EXPECT_EQ(readBytes(base), "after-shrink\n");
}

TEST(LogFileAudit, BinaryByteCountAndOpenFailure_DT_LOG_IO)
{
    TempDir dir("b35-bytes");
    diag::FileLogFileOps ops;
    const auto path = dir.path() / "bytes.log";
    EXPECT_EQ(ops.fileSize(path), 0u);
    ASSERT_TRUE(ops.appendLine(path, "汉")); // UTF-8三字节＋单个LF；不受Windows文本换行转换影响。
    EXPECT_EQ(readBytes(path), "汉\n");
    EXPECT_EQ(ops.fileSize(path), 4u);
    EXPECT_TRUE(ops.flushFile(path));
    // 父目录不存在时只观察open失败；不据此声称已验证磁盘满/关闭失败。
    EXPECT_FALSE(ops.appendLine(dir.path() / "absent" / "bad.log", "x"));
}

TEST(LogFileAudit, UpstreamStreamFailureNeedsExplicitPolicy_REUSE_UTIL_LOG)
{
    std::ostringstream failed;
    failed.setstate(std::ios::badbit);
    rw::core::LogStreamWriter writer(&failed); // 上游借持流，流比writer后销毁。
    EXPECT_NO_THROW(writer.write("lost"));
    EXPECT_NO_THROW(writer.flush());
    EXPECT_TRUE(failed.bad());
    EXPECT_TRUE(failed.str().empty());
    // 开启流异常可观测失败，但仍需调用方管理流、串行化及错误转译；不能丢掉产品bool语义。
    std::ostringstream throwing;
    throwing.exceptions(std::ios::badbit | std::ios::failbit);
    rw::core::LogStreamWriter checked(&throwing);
    EXPECT_THROW(throwing.setstate(std::ios::badbit), std::ios_base::failure);
    EXPECT_THROW(checked.write("lost"), std::ios_base::failure);
    throwing.clear(); // 让上游析构flush在有效流上完成，避免测试引入析构异常。
}

TEST(LogFileAudit, Utf8AndPhysicalNewlineStayOnOneRecord_DT_LOG_TEXT)
{
    TempDir dir("b35-text");
    diag::FileLogFileOps ops;
    diag::SystemClock clock;
    diag::LoggingPipeline logger(clock, ops);
    diag::LogSinkConfig cfg;
    cfg.directory = dir.path();
    cfg.userEnabled = false;
    cfg.rotateMaxBytes = 0;
    logger.configure(cfg);
    logger.logDev("audit/text", diag::LogLevel::Info, "汉\nnext\tvalue\\tail");
    ASSERT_TRUE(logger.flush(diag::kLogDefaultFlushDeadline));
    const auto bytes = readBytes(dir.path() / cfg.devFileName);
    EXPECT_NE(bytes.find("msg=汉\\nnext\\tvalue\\\\tail"), std::string::npos);
    EXPECT_EQ(std::count(bytes.begin(), bytes.end(), '\n'), 1)
        << "DT-LOG-2：载荷内换行不得生成第二条物理记录";
}

TEST(LogFileAudit, UpstreamFileWriterOpensWithTruncation_REUSE_UTIL_LOG)
{
    TempDir dir("b35-upstream-file");
    const auto path = dir.path() / "upstream.log";
    diag::FileLogFileOps ops;
    ASSERT_TRUE(ops.appendLine(path, "previous-content"));
    {
        rw::common::LogFileWriter writer(path.string());
        writer.writeln("fresh-content");
    }
    // 上游默认ofstream打开会截旧内容，不等于产品逐行append策略；保留其原构造失败语义。
    const auto bytes = readBytes(path);
    EXPECT_EQ(bytes.find("previous-content"), std::string::npos);
    EXPECT_NE(bytes.find("fresh-content"), std::string::npos);
    EXPECT_ANY_THROW(rw::common::LogFileWriter((dir.path() / "missing" / "x.log").string()));
}
} // namespace
