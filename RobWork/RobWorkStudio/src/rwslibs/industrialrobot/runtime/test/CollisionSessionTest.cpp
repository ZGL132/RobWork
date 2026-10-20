/**
 * @file   CollisionSessionTest.cpp
 * @brief  运行时真实碰撞设施测试（MA-02——本批可机器验证面）。
 *
 * 覆盖口径（诚实登记）：
 *   - 本 TU 验证会话装配三轨（设备在位可构造／快照缺位返回 null／
 *     safetyClearance>0 显式拒绝）与判定守卫（自由度失配→未评价）。
 *   - **同一真实场景的碰撞/不碰撞/排除正反例与宿主画面**需要带摆位场景
 *     对象的发布快照夹具——归统一验收批（MA-07/08 联合终验同批），本批
 *     不以组件通过冒充全链交付。
 *
 * 设计依据：units/runtime.md（MA-02 批登记）；审计 REPORT.md MA-02 行。
 */

#include <gtest/gtest.h>

#include "CompilerImpl.hpp"         // 被测：产品编译器（单元私有头——测试与 src 同权）
#include "RuntimeTestDoubles.hpp"  // ContractHarness（test/ 私有夹具——同目录引用）

#include <sdurws/ird/runtime/CollisionSession.hpp>
#include <sdurws/ird/runtime/Compiler.hpp>
#include <sdurws/ird/runtime/Snapshot.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <memory>
#include <stdexcept>
#include <vector>

namespace {

using namespace sdurws::ird;
namespace td = sdurws::ird::runtime::testdoubles;
using td::ContractHarness;

/// 编译夹具至已发布快照（HostPresentationViewTest 同款助手——本 TU 独立
/// 声明，不跨文件共享非头文件工具）。
std::shared_ptr<const runtime::RuntimeSnapshot> compilePublished(ContractHarness& h)
{
    td::ScriptedReader reader(h.description);
    td::ScriptedObjectSource objects = h.objectSource();
    td::ScriptedClosureSource closure = h.closureSource();
    runtime::CompileRequest req = h.makeRequest(reader, objects, closure);

    runtime::CanonicalModelCompiler compiler;
    const runtime::CompileOutcome out = compiler.compile(req);
    EXPECT_EQ(out.status, runtime::CompileStatus::Published)
        << "夹具应发布快照（诊断："
        << (out.diagnostics.empty() ? std::string{} : out.diagnostics.front().cause)
        << "）";
    return out.snapshot;
}

}  // namespace

/**
 * MA-02 装配面：已发布快照（两关节设备在位）→会话可构造；evaluate 以
 * 自由度失配向量查询→evaluated=false（设施守卫轨——不猜截断/补零）。
 */
TEST(RuntimeCollisionSessionTest, MakeFromPublishedSnapshotAndDimensionGuard_MA02)
{
    IRD_TEST_INFO(std::vector<std::string>{"MA-02"}, {});
    ContractHarness h = ContractHarness::make();
    auto snapshot = compilePublished(h);
    ASSERT_TRUE(snapshot != nullptr);

    auto session = runtime::RuntimeCollisionSession::make(
        snapshot, runtime::RuntimeCollisionRules{});
    ASSERT_TRUE(session != nullptr)
        << "两关节快照应可构造真实碰撞会话（设备在位）";

    // 自由度失配守卫：3 维查询对 2 自由度设备→未评价（FacilityFailed 轨）。
    const auto mismatch = session->evaluate({0.0, 0.0, 0.0});
    EXPECT_FALSE(mismatch.evaluated)
        << "自由度失配仍给出判定（守卫违约）";

    // 正确自由度查询→evaluated=true（布尔结论不锁死——几何依赖夹具）。
    const auto ok = session->evaluate({0.0, 0.0});
    EXPECT_TRUE(ok.evaluated) << "正确自由度查询未完成评价";
}

/**
 * MA-02 诚实降级面：快照缺位（null）→make 返回 null（宿主据此回落
 * EvidenceMissing——不虚构无碰撞）；safetyClearance>0 的容差判定未实
 * 机化——make 显式拒绝（InputInvalid——不静默降级为 0 阈值伪证据）。
 */
TEST(RuntimeCollisionSessionTest, HonestDegradationPaths_MA02)
{
    IRD_TEST_INFO(std::vector<std::string>{"MA-02", "KIN-05"}, {});

    // 快照缺位→null（EvidenceMissing 回落面）。
    auto nullSession = runtime::RuntimeCollisionSession::make(
        nullptr, runtime::RuntimeCollisionRules{});
    EXPECT_TRUE(nullSession == nullptr) << "空快照仍构造会话（降级面违约）";

    // safetyClearance>0 → InputInvalid 显式拒绝（不静默降级为 0）。
    runtime::RuntimeCollisionRules marginRules;
    marginRules.safetyClearanceM = 0.01;  // 10 mm——本批未实机化的容差面
    ContractHarness h = ContractHarness::make();
    auto snapshot = compilePublished(h);
    ASSERT_TRUE(snapshot != nullptr);
    bool threw = false;
    try {
        runtime::RuntimeCollisionSession::make(snapshot, marginRules);
    } catch (const std::exception&) {
        threw = true;
    }
    EXPECT_TRUE(threw) << "safetyClearance>0 未显式拒绝（伪间距证据风险）";
}
