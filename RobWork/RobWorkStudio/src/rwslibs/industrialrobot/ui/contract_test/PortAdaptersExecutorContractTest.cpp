/**
 * @file   PortAdaptersExecutorContractTest.cpp
 * @brief  SerialTaskExecutor 受理位契约面直测（R3 审计 A16/C3 的"任务被
 *         拒收"变体回归钉——audit-r3-fixes-1 返工批 O-12）。
 *
 * 设计依据：
 *   - R3 审计 C3（audit/round3-20261015）：退出序列（aboutToQuit 停执行
 *     器）后再达的 saveAll/discardDraft——原 void 签名下任务被静默丢弃，
 *     同步入口 future.get() 无界挂起＝UI 假死（F-819 修了"任务抛异常"
 *     变体，本 TU 补"任务被拒收"变体的生产面钉）。
 *   - IDraftController.hpp DraftControllerDeps 受理位契约注（b3d586d9，
 *     2026-10-15）与 PortAdapters.hpp SerialTaskExecutor::post 契约注。
 *   - 整批验收记录 O-12（audit-r3-fixes-1-attempt1 §三观察项）：契约测试
 *     装配替身恒受理——拒绝面在生产执行器，可经 PortAdapters 直测补齐。
 *
 * 背景：DraftContractTest 的装配替身恒受理（恒 true），只能钉"受理路
 * 径"；"false＝任务未入队也永不执行"的生产面语义由本 TU 钉扎——被测 TU
 * app/PortAdapters.cpp 同源编入（CrashCapture/DomainAssembly 先例形态，
 * 见 ui/CMakeLists.txt 本批增列注），防"替身绿、生产面漂移"。
 *
 * 线程纪律：用例只消费 post 的返回值与任务执行的有界等待（future 10 s
 * 上限——线程调度延迟不在断言面）；"停止后永不执行"断言是结构性事实
 * （stop() 返回即工作线程已 join，不存在任何执行者），零 sleep 判据。
 */

#include "app/PortAdapters.hpp"

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <future>

using sdurws::ird::ui::app::SerialTaskExecutor;

namespace {

/// 有界等待任务执行完成（10 s 上限——真线程调度延迟不参与断言；超时即
/// 红并用例结束，不悬挂套件）。
bool waitsReady(std::future<bool>& future)
{
    return future.wait_for(std::chrono::seconds{10}) == std::future_status::ready;
}

}  // namespace

// =====================================================================
// 受理路径（true＝已入队且任务真实执行）
// =====================================================================

/**
 * 运行期受理语义：post 返回 true＝任务已入队、由串行落盘线程真实执行
 * （§10.5 注入纪律"post 即串行异步"的生产形态——§3.4 ui 后台落盘线程
 * 宿主）。false 只允许表达"执行器已停止"，运行期不得出现。
 */
TEST(PortAdaptersExecutor, PostAcceptedWhileRunningExecutesTask_R3_C3)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-03"},
                  std::vector<std::string>{"R3-C3"});

    SerialTaskExecutor executor;
    std::promise<bool> done;
    std::future<bool> doneFuture = done.get_future();

    const bool accepted = executor.post([&done] { done.set_value(true); });

    // 受理位＝true（运行期无拒收窗口），且任务确被执行（future 有界等待
    // ——受理与执行两半都钉住，防"返回 true 却丢弃任务"的漂移）。
    EXPECT_TRUE(accepted);
    ASSERT_TRUE(waitsReady(doneFuture)) << "任务 10 s 内未执行——受理位为 true "
                                           "却未入队（受理语义违约）";
    EXPECT_TRUE(doneFuture.get());
}

// =====================================================================
// 拒收路径（false＝未入队且永不执行——R3-C3 假死语义的可观测前提）
// =====================================================================

/**
 * 停止后拒收语义（R3 审计 C3 修复的生产面钉）：stop() 之后 post 返回
 * false，且任务未入队也永不执行。原缺陷形态（无条件入队＋void 签名）
 * 下调用方无从得知拒收——同步入口 future.get() 无界挂起＝退出序列 UI
 * 假死；本用例把"false 可观测"与"任务永不执行"两半都钉住：调用方按失
 * 败值轨就地收口的前提是 false 真实可达且伴随"未投递"事实。
 */
TEST(PortAdaptersExecutor, PostRejectedAfterStopTaskNeverExecutes_R3_C3)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-03"},
                  std::vector<std::string>{"R3-C3"});

    SerialTaskExecutor executor;
    executor.stop();  // 有界排空后工作线程已 join——此后不存在任何执行者

    bool executed = false;
    const bool accepted = executor.post([&executed] { executed = true; });

    // 受理位＝false（执行器已停止——调用方必须按失败值轨收口：同步入口
    // 不得 future.get()，异步入口须就地回收在途状态，IDraftController.hpp
    // DraftControllerDeps 契约注原文语义）。
    EXPECT_FALSE(accepted);
    // 任务永不执行：工作线程已被 stop() 收线，"无人可执行"是结构性事实
    // （非时序窗口——确定性断言，零 sleep 判据；原缺陷形态此处任务已
    // 入队且 post 返回 true，双断言同时翻红）。
    EXPECT_FALSE(executed);
}
