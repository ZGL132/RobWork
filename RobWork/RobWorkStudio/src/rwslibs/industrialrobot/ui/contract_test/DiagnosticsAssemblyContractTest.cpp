/**
 * @file DiagnosticsAssemblyContractTest.cpp
 * @brief NFR-SEC-07 专项回归：运行生产装配与真实文件出口，不替换 UI 码表供体。
 * QCoreApplication 测试，不启动界面；所有路径/凭据均为合成输入。
 */
#include "app/DiagnosticsAssembly.hpp"
#include <gtest/gtest.h>
#include <fstream>

namespace sdurws::ird::ui {
TEST(DiagnosticsAssemblyContract, RedactsPipelineTailDiskAndFactorySnapshot)
{
    namespace fs = std::filesystem;
    const auto dir = fs::temp_directory_path()
        / ("ird-assembly-" + core::ObjectId::generate().toCanonical());
    ASSERT_TRUE(fs::create_directory(dir));
    {
        app::DiagnosticsStack stack;
        app::assembleDiagnostics(dir, stack);
        diagnostics::LogRecord r;
        r.tier = diagnostics::LogTier::Dev;
        r.level = diagnostics::LogLevel::Info;
        r.channel = "diagnostics-assembly";
        r.message = "password=FakeAssembly932";
        stack.pipeline->log(r);
        ASSERT_TRUE(stack.pipeline->flush(std::chrono::seconds(2)));
        const auto tail = stack.pipeline->devTailSnapshot(10);
        ASSERT_FALSE(tail.empty());
        for (const auto& line : tail) EXPECT_EQ(line.find("FakeAssembly932"), std::string::npos);
        std::ifstream file(dir / "dev-diagnostics.log", std::ios::binary);
        const std::string text{std::istreambuf_iterator<char>(file), {}};
        EXPECT_NE(text.find("msg="), std::string::npos);
        EXPECT_EQ(text.find("FakeAssembly932"), std::string::npos);
        diagnostics::DiagContext c;
        c.sourceUnit = "ui";
        c.sourceInterface = "assembly-contract";
        c.params = {{"pid", "password=FakeFactory933"}, {"host", "synthetic"}};
        auto raw = core::DiagnosticRecord::make("PRJ-LOCK-HELD", core::ObjectId::generate(),
            {}, {}, "synthetic", "synthetic", "synthetic");
        const auto entry = stack.factory->create(raw, c);
        ASSERT_TRUE(entry.redactedContextSnapshot);
        EXPECT_EQ(entry.redactedContextSnapshot->find("FakeFactory933"), std::string::npos);
    }
    fs::remove(dir / "dev-diagnostics.log");
    fs::remove(dir / "user-diagnostics.log");
    fs::remove(dir);  // 已知文件与空目录；不递归删除。
}
}  // namespace sdurws::ird::ui
