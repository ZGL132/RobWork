/**
 * @file   ContractSuiteFacilitiesTest.cpp
 * @brief  契约套件设施自证用例组（EX-T09）——故障点标识登记、P-EX-8 空
 *         占位 sink、收编码值镜像、ManualClock 与脚本替身边界的可执行
 *         自检（acceptance 2/4 的"在案"面从文档承诺落为具名断言）。
 *
 * 设计依据：
 *   - units/execution.md §11（设施与替身列＋替身边界声明；末段"每条用例
 *     经 IRD_TEST_INFO 登记"）、§12 EX-T09 行、§15.3 P-EX-8（sink 名称/
 *     归属统一待裁决不私定）、§13 testkit 交接行（faultPointId 命名）
 *   - units/testkit.md §6.4（命名约定 <unit>/<接口>/<动作>）、§6.6
 *     （ManualClock 通用 fake 模式）、§6.2/§6.3（TempDir/DeterministicEnv
 *     ——消费先例自证）、§7.3（IRD_TEST_INFO）
 *   - 任务契约 tasks/foundation/EX-T09.json acceptance 2（替身边界声明
 *     在案＋faultPointId 命名登记——execution/process-launcher/create、
 *     execution/channel/send-frame、execution/channel/recv-frame、
 *     execution/archive/*）、4（空实现占位＋断言码值只在收编 18 项）、
 *     5（AT-34 时钟纪律的设施面）
 */

#include "ContractSuiteFacilities.hpp"

#include <sdurws/ird/core/DiagData.hpp>
#include <sdurws/ird/testkit/Fixture.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

namespace {

using namespace sdurws::ird::execution;
namespace core = sdurws::ird::core;
namespace ev = sdurws::ird::evidence;
namespace tk = sdurws::ird::testkit;
namespace fs = std::filesystem;

// =====================================================================
// 故障点标识登记（acceptance 2——testkit §6.4 交接义务的具名自证）
// =====================================================================

TEST(ContractSuiteFacilities, FaultPointIdRegistryMatchesSection11Handover)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-MNT-02"}, std::vector<std::string>{});

    // §11/testkit §6.4 交接义务点名的四个接缝必须全部在登记表内。
    const std::vector<const char*> ids = exsuite::registeredFaultPointIds();
    const std::set<std::string> registry(ids.begin(), ids.end());
    EXPECT_EQ(ids.size(), registry.size()) << "登记表不得含重复项（确定性登记）";
    EXPECT_NE(registry.find("execution/process-launcher/create"), registry.end())
        << "进程启动接缝未登记（testkit §6.4 交接义务）";
    EXPECT_NE(registry.find("execution/channel/send-frame"), registry.end())
        << "通道发送接缝未登记";
    EXPECT_NE(registry.find("execution/channel/recv-frame"), registry.end())
        << "通道接收接缝未登记";
    // execution/archive/* 家族（§11 设施列通配形态）——四个端口方法逐一登记。
    EXPECT_NE(registry.find("execution/archive/begin"), registry.end());
    EXPECT_NE(registry.find("execution/archive/write-batch"), registry.end());
    EXPECT_NE(registry.find("execution/archive/finalize"), registry.end());
    EXPECT_NE(registry.find("execution/archive/abandon"), registry.end());

    // 命名约定：<unit>/<接口>/<动作>——两处 '/' 且以 execution/ 开头。
    for (const std::string& id : registry) {
        EXPECT_EQ(std::string(id).rfind("execution/", 0), 0u) << "单元前缀违约: " << id;
        EXPECT_EQ(std::count(id.begin(), id.end(), '/'), 2u) << "段数违约: " << id;
    }
}

// =====================================================================
// P-EX-8：空实现占位 sink（acceptance 4——project §3.2 同模式）
// =====================================================================

TEST(ContractSuiteFacilities, NullExecutionDiagnosticsSinkIsEmptyPlaceholder_PEX8)
{
    IRD_TEST_INFO(std::vector<std::string>{}, std::vector<std::string>{});

    // 占位语义：两通道均可注入且零输出面（裁决前不外报——占位不预建
    // diagnostics 适配器）；占用者可用、可弃，无任何可断言的外效。
    exsuite::NullExecutionDiagnosticsSink sink;
    core::DiagnosticRecord record = core::DiagnosticRecord::make(
        "EX-ARCHIVE-FAILED", std::nullopt, std::nullopt, std::nullopt,
        "execution/test", "占位 sink 注入面冒烟", "占位不外报");
    EXPECT_NO_THROW(sink.report(record));
    EXPECT_NO_THROW(sink.reportDev("execution/test", "占位开发通道注入"));
}

// =====================================================================
// 收编码值镜像（acceptance 4——断言码值只在 diagnostics 已收编 18 项内）
// =====================================================================

TEST(ContractSuiteFacilities, RegisteredCodeMirrorHasExactlyTheIncorporatedEighteen_PEX8)
{
    IRD_TEST_INFO(std::vector<std::string>{}, std::vector<std::string>{});

    // 镜像口径：恰 18 项（diagnostics.md §4.6 收编清单——P-EX-8 v0.2 对齐
    // 说明）、无重复、成员判定双向正确。镜像条目与 DiagCodes 的逐字一致
    // 由收编 diff 流程保障（P-EX-1——本断言钉"数量与成员判定"不漂移）。
    const std::vector<const char*> codes = exsuite::registeredExStableCodes();
    EXPECT_EQ(codes.size(), 18u) << "收编镜像数量漂移——请按 diagnostics 冻结 diff 增量同步";
    const std::set<std::string> unique(codes.begin(), codes.end());
    EXPECT_EQ(unique.size(), codes.size()) << "镜像含重复码值";

    EXPECT_TRUE(exsuite::isRegisteredExStableCode("EX-ARCHIVE-FAILED"));
    EXPECT_TRUE(exsuite::isRegisteredExStableCode("EX-TASK-INTERRUPTED"));
    EXPECT_TRUE(exsuite::isRegisteredExStableCode("EX-WORKER-CRASHED"));
    EXPECT_FALSE(exsuite::isRegisteredExStableCode("EX-NOT-A-REGISTERED-CODE"));
    EXPECT_FALSE(exsuite::isRegisteredExStableCode(""));
}

// =====================================================================
// ManualClock（acceptance 5——时钟敏感用例的虚拟推进设施）
// =====================================================================

TEST(ContractSuiteFacilities, ManualClockAdvancesVirtuallyWithoutRealTime_Discipline)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-PERF-02"}, std::vector<std::string>{"AT-34"});

    // 虚拟推进语义：advance 只动虚拟值——两次 advance 之间真实时间流逝
    // 不进入读数（时序断言与真实时延解耦——§11 末段纪律的设施面）。
    exsuite::ManualClock clock;
    const auto base = clock.now();
    clock.advance(std::chrono::milliseconds{2000});
    EXPECT_EQ(clock.now() - base, std::chrono::milliseconds{2000});
    clock.advance(std::chrono::milliseconds{10000});
    EXPECT_EQ(clock.now() - base, std::chrono::milliseconds{12000});
    // ClockFn 注入形态：可调用对象直接绑定（TaskController::ClockFn 等面）。
    auto bound = [&clock] { return clock.now(); };
    EXPECT_EQ(bound(), clock.now());
}

// =====================================================================
// ScriptedOutputDecoder（acceptance 2——ScriptedEvaluator 接纳侧承载）
// =====================================================================

TEST(ContractSuiteFacilities, ScriptedOutputDecoderReplaysScriptThenFailsClosed)
{
    IRD_TEST_INFO(std::vector<std::string>{}, std::vector<std::string>{});

    // 脚本语义：FIFO 逐次应答（含显式 nullopt＝解码失败注入）；耗尽后
    // 恒 nullopt（fail-closed——不重放、不猜值）。
    exsuite::ScriptedOutputDecoder decoder;
    const std::vector<std::uint8_t> anyBytes{0x01};
    EXPECT_FALSE(decoder.tryDecode(anyBytes).has_value()) << "空脚本＝解码失败";

    ev::EvidenceItem item;
    item.itemId = "kin.reach-per-task-point";
    item.status = ev::EvidenceItemStatus::Satisfied;
    ev::EvaluationOutput first;
    first.evidence = {item};
    decoder.enqueue(first);
    decoder.enqueue(std::nullopt);  // 显式解码失败注入位

    const std::optional<ev::EvaluationOutput> replayed = decoder.tryDecode(anyBytes);
    ASSERT_TRUE(replayed.has_value());
    EXPECT_EQ(replayed->evidence.size(), 1u);
    EXPECT_FALSE(decoder.tryDecode(anyBytes).has_value()) << "显式 nullopt 应答";
    EXPECT_FALSE(decoder.tryDecode(anyBytes).has_value()) << "耗尽后恒失败（不重放）";
}

// =====================================================================
// testkit 设施消费自证（TempDir／DeterministicEnv——acceptance 2 设施列）
// =====================================================================

TEST(ContractSuiteFacilities, TempDirAndDeterministicEnvAreUsableAsDeclared)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-02"}, std::vector<std::string>{});

    // TempDir：构造即存在、路径隔离（两实例不串扰——§6.2）；失败保留
    // 开关可关（本用例现场无排查价值）。
    tk::TempDir first("ex-suite-a");
    tk::TempDir second("ex-suite-b");
    EXPECT_TRUE(fs::exists(first.path()));
    EXPECT_TRUE(fs::exists(second.path()));
    EXPECT_NE(first.path(), second.path());
    first.keepOnFailure(false);
    second.keepOnFailure(false);

    // DeterministicEnv：记录为唯一确定性上下文来源（§6.3）——默认值口径
    // （seed=20260909 固定值、threadCount=1）与不可变共享。
    tk::ReproRecord repro;
    EXPECT_EQ(repro.seed, 20260909u);
    EXPECT_EQ(repro.threadCount, 1);
    repro.notes = "EX-T09 契约套件确定性上下文";
    tk::DeterministicEnv env(repro);
    EXPECT_EQ(env.record().notes, repro.notes);
    EXPECT_EQ(env.record().seed, 20260909u);
}

}  // namespace
