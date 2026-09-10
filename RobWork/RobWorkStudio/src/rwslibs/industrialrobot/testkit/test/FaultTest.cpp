/**
 * @file   FaultTest.cpp
 * @brief  故障注入原语用例组——TK-FAULT（units/testkit.md §8）：occurrence 触发/
 *         命中记录/多故障点独立/AT-13 磁盘满模拟示例/生产面零 testkit 零 #ifdef TEST。
 *
 * 设计依据：
 *   - units/testkit.md §6.4（接缝原则、occurrence 语义、文件系统故障覆盖）、
 *     §8 TK-FAULT 行、§9 TK-T09 行
 *   - 需求 NFR-REL-01/02、PM-08；任务契约 tasks/foundation/TK-T09.json
 *     acceptance 三条（occurrence 触发/命中记录；TK-BUILD 复验；生产代码零
 *     testkit 头、零 #ifdef TEST）
 */

#include <gtest/gtest.h>

#include <sdurws/ird/testkit/Fault.hpp>
#include <sdurws/ird/testkit/ProcessRunner.hpp>   // 设计冻结头：仅验证可包含＋声明形态

#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace {
using namespace sdurws::ird::testkit;

// =====================================================================
// 被测接口与 fake（§6.4 用法示例的具体化——接口属测试目标，生产零感知）
// =====================================================================

/// 演示用窄接口（模拟 io 的文件写入器接缝——生产侧自有接口形态）。
class FakeFileWriter {
public:
    virtual ~FakeFileWriter() = default;
    virtual void writeChunk(const std::string& data) = 0;   // 磁盘满在写中途抛
    virtual int chunkCount() const noexcept = 0;
};

/// "真实实现"（被测接口的具体形态——拦截器未命中路径的转调目标）。
class MemoryWriter final : public FakeFileWriter {
public:
    void writeChunk(const std::string& data) override { sink_ += data; }
    int chunkCount() const noexcept override { return static_cast<int>(sink_.size()); }

private:
    std::string sink_;
};

/// 计划驱动的 fake：方法内先 shouldFire 裁决，命中抛接口错误，未命中转真实实现。
class PlannedWriter final : public FakeFileWriter {
public:
    explicit PlannedWriter(FaultPlan plan)
        : fault_(std::make_shared<MemoryWriter>(), std::move(plan)) {}

    void writeChunk(const std::string& data) override
    {
        // 磁盘满模拟（§6.4"写 N 字节后失败"的简化形态）：按计划在指定次注入。
        if (fault_.shouldFire("io/file-writer/write-chunk", ++calls_)) {
            throw std::runtime_error("disk-full-simulated");   // 被测接口声明的错误类型
        }
        fault_.real()->writeChunk(data);            // 未命中→真实实现
    }
    int chunkCount() const noexcept override { return fault_.real()->chunkCount(); }

    FaultLog faultLog() const { return fault_.log(); }

private:
    FaultInterceptor<FakeFileWriter> fault_;
    std::uint64_t calls_ = 0;
};

/** TK-FAULT 核心：occurrence 触发——恰在第 N 次注入，前后放行，命中记录一次。 */
TEST(FaultOccurrence, FiresExactlyAtPlannedOccurrence_UT_FAULT)
{
    FaultPlan plan;
    plan.triggers.push_back({"io/file-writer/write-chunk", 2});   // 第 2 次写注入

    PlannedWriter w(plan);
    const std::string chunk = "payload";
    EXPECT_NO_THROW(w.writeChunk(chunk));                         // 第 1 次：放行
    EXPECT_THROW(w.writeChunk(chunk), std::runtime_error);        // 第 2 次：注入磁盘满
    EXPECT_NO_THROW(w.writeChunk(chunk));                         // 第 3 次：放行（一次性触发）

    // 命中记录：恰一条（故障点, 2）——观测面与注入面一致（§6.4 FaultLog 语义）。
    const FaultLog log = w.faultLog();
    ASSERT_EQ(log.hits.size(), 1u);
    EXPECT_EQ(log.hits[0].first, "io/file-writer/write-chunk");
    EXPECT_EQ(log.hits[0].second, 2u);
}

/** 计划外行为：空计划永不触发；occurrence 超出计划值只触发一次。 */
TEST(FaultOccurrence, EmptyPlanAndBeyondPlan_UT_FAULT)
{
    FaultLog log;
    const FaultPlan empty;
    EXPECT_FALSE(faultShouldFire(empty, "any/point", 1, &log));
    EXPECT_TRUE(log.hits.empty());

    FaultPlan plan;
    plan.triggers.push_back({"exec/dispatch/send", 2});
    for (std::uint64_t n = 1; n <= 5; ++n) {
        const bool fire = faultShouldFire(plan, "exec/dispatch/send", n, &log);
        EXPECT_EQ(fire, n == 2);                                  // 仅第 2 次触发
    }
    ASSERT_EQ(log.hits.size(), 1u);
    EXPECT_EQ(log.hits[0].second, 2u);
}

/** 多故障点独立计数＋命中按实际注入顺序记录（跨点交错场景）。 */
TEST(FaultOccurrence, MultiplePointsIndependentCounting_UT_FAULT)
{
    FaultPlan plan;
    plan.triggers.push_back({"io/file-writer/write-chunk", 1});
    plan.triggers.push_back({"io/file-writer/rename", 2});

    FaultLog log;
    // 交错命中序列：write(1)→rename(1)→write(2)→rename(2)——只有第 1、4 次探测命中。
    EXPECT_TRUE(faultShouldFire(plan, "io/file-writer/write-chunk", 1, &log));
    EXPECT_FALSE(faultShouldFire(plan, "io/file-writer/rename", 1, &log));
    EXPECT_FALSE(faultShouldFire(plan, "io/file-writer/write-chunk", 2, &log));
    EXPECT_TRUE(faultShouldFire(plan, "io/file-writer/rename", 2, &log));

    ASSERT_EQ(log.hits.size(), 2u);                               // 注入顺序＝记录顺序
    EXPECT_EQ(log.hits[0].first, "io/file-writer/write-chunk");
    EXPECT_EQ(log.hits[1].first, "io/file-writer/rename");
    EXPECT_EQ(log.hits[1].second, 2u);
}

/** AT-13 载体示例：磁盘满驱动保存事务（写中途失败→调用方决定回滚——错误路径演练）。 */
TEST(FaultScenario, DiskFullDrivesErrorPath_UT_FAULT)
{
    FaultPlan plan;
    plan.triggers.push_back({"io/file-writer/write-chunk", 3});   // 第 3 块磁盘满
    PlannedWriter w(plan);
    int written = 0;
    for (int i = 0; i < 5; ++i) {
        try {
            w.writeChunk("block");
            ++written;
        } catch (const std::runtime_error&) {
            break;                                                // 事务调用方在此回滚（§6.4 AT-13 形态）
        }
    }
    EXPECT_EQ(written, 2);                                        // 恰写入 2 块后失败
}

/** 真实实现委托：real() 返回绑定对象（shared 所有权恒定——未命中路径可用）。 */
TEST(FaultWiring, RealAccessorDelegation_UT_FAULT)
{
    auto real = std::make_shared<MemoryWriter>();
    FaultInterceptor<FakeFileWriter> interceptor(real, FaultPlan{});
    EXPECT_EQ(interceptor.real().get(), real.get());              // 同一对象
    EXPECT_NO_THROW(interceptor.real()->writeChunk("direct"));    // 委托通道可用
}

/** FaultInterceptor 契约违约：real 为空直接抛 Usage（构造期 fail-fast）。 */
TEST(FaultWiring, NullRealRejected_UT_FAULT)
{
    EXPECT_THROW((FaultInterceptor<FakeFileWriter>(nullptr, FaultPlan{})),
                 TestKitError);
}

/** ProcessRunner 设计冻结头：可包含＋声明形态钉住（实现仍冻结——O-33，不实例化）。 */
TEST(ProcessRunnerHeader, FrozenDeclarationsPresent_UT_FAULT)
{
    // 头可被独立包含且类型为类类型（声明存在）；定义缺失＝无法实例化（编译期即拦）。
    static_assert(std::is_class_v<sdurws::ird::testkit::TestProcessRunner>,
                  "TestProcessRunner 声明缺失（冻结头被意外删改）");
    static_assert(std::is_class_v<sdurws::ird::testkit::EventWatch>,
                  "EventWatch 声明缺失");
    static_assert(std::is_enum_v<sdurws::ird::testkit::ProcessExitKind>,
                  "ProcessExitKind 声明缺失");
    SUCCEED() << "ProcessRunner.hpp 冻结声明在位（实现按 O-33 触发交付）";
}

/** 生产面扫描复验（acceptance 2/3）：全部产品单元 include/src 零 testkit 头、
 *  零 #ifdef TEST/#if defined(TEST)——接缝原则的源码面自检（TkBuild.T1 的加强版）。 */
TEST(ProductBoundary, ZeroTestkitHeadersAndTestIfdef_UT_FAULT)
{
    const std::filesystem::path root{IRD_TESTKIT_UNIT_ROOT};
    ASSERT_TRUE(std::filesystem::exists(root));                   // 路径锚定（防空扫恒真）
    int scanned = 0;
    for (const auto& unit : std::filesystem::directory_iterator(root)) {
        if (!unit.is_directory()) continue;
        const auto unitName = unit.path().filename().string();
        if (unitName == "testkit") continue;                      // testkit 自身豁免
        for (const auto* sub : {"include", "src"}) {
            const auto dir = unit.path() / sub;
            if (!std::filesystem::exists(dir)) continue;
            for (const auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
                if (!entry.is_regular_file()) continue;
                const auto ext = entry.path().extension().string();
                if (ext != ".hpp" && ext != ".h" && ext != ".cpp") continue;
                std::ifstream in(entry.path(), std::ios::binary);
                ASSERT_TRUE(in) << "无法读取: " << entry.path().string();
                std::string line;
                int lineno = 0;
                while (std::getline(in, line)) {
                    ++lineno;
                    EXPECT_EQ(line.find("sdurws/ird/testkit/"), std::string::npos)
                        << "产品面含 testkit 头（T-1）: " << entry.path().string()
                        << ":" << lineno;
                    const auto ifdefPos = line.find("#ifdef TEST");
                    const auto definedPos = line.find("defined(TEST");
                    EXPECT_EQ(ifdefPos, std::string::npos)
                        << "产品面含 #ifdef TEST: " << entry.path().string() << ":" << lineno;
                    EXPECT_EQ(definedPos, std::string::npos)
                        << "产品面含 defined(TEST): " << entry.path().string() << ":" << lineno;
                }
                ++scanned;
            }
        }
    }
    EXPECT_GT(scanned, 0) << "扫描集为空（路径配置错误——防恒真）";
}
}  // namespace
