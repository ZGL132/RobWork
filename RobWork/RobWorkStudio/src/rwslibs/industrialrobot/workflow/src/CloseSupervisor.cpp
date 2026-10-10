/**
 * @file   CloseSupervisor.cpp
 * @brief  L5 关闭控制器（StoreCloseSupervisor）的实现翻译单元（头文件为
 *         权威契约——本文件承载监督循环的①~⑤序逐步兑现与线程收敛）。
 *
 * 设计依据：见 CloseSupervisor.hpp 文件头（project §9.7 兜底义务、A7
 * 等待语义、poll 复合驱动与 F-578 锁域纪律、R1 零新增码的 Dev 日志
 * 通道选择）。本文件新增语义＝无（全部在头注释）。
 */

#include <sdurws/ird/workflow/CloseSupervisor.hpp>

#include <utility>

#include <sdurws/ird/workflow/Types.hpp>  // workflow::WorkflowError（构造参数违约 fail-fast）

namespace sdurws {
namespace ird {
namespace workflow {

// 监督通道 token（Dev 日志的 LogChannel 词面——"来源子系统"登记面；
// 零稳定码，消息原文即事实承载——D-WF-7/R1 纪律）。
constexpr const char* kCloseSupervisorChannel = "workflow/close-supervisor";

StoreCloseSupervisor::StoreCloseSupervisor(project::ProjectStore& store,
                                           execution::TaskScheduler& scheduler,
                                           Config config,
                                           diagnostics::IDevLogSink* devLog,
                                           ClockFn clock)
    : m_store(store)
    , m_scheduler(scheduler)
    , m_config(config)
    , m_devLog(devLog)
    , m_clock(std::move(clock))
{
    // 装配违约 fail-fast（§10.3 错误语义行——零/负阈值无监督语义；WorkflowError
    // 前缀 "workflow/caller-contract:" 由类型自带）。
    if (m_config.abandonThreshold <= std::chrono::milliseconds::zero()) {
        throw WorkflowError("关闭监督器兜底阈值必须为正（abandonThreshold>0）");
    }
    if (m_config.giveUpGrace <= std::chrono::milliseconds::zero()) {
        throw WorkflowError("关闭监督器放弃宽限必须为正（giveUpGrace>0）");
    }
    if (m_config.pollInterval <= std::chrono::milliseconds::zero()) {
        throw WorkflowError("关闭监督器轮询拍间隔必须为正（pollInterval>0）");
    }
}

StoreCloseSupervisor::~StoreCloseSupervisor()
{
    // 析构自动收敛（监督线程不留后台悬挂——头文件线程模型行的兑现）。
    stop();
}

void StoreCloseSupervisor::start()
{
    // 收割上一轮已自然退出（①/④/⑤路径 return）的线程对象——thread
    // 赋值前必须 join，否则 std::terminate；join 后 running 位必已被
    // 线程自身清除（见 superviseLoop 收尾）。
    if (m_thread.joinable()) {
        m_thread.join();
    }
    // 幂等（线程仍在监督中 no-op）——宿主重复触发（如关闭入口重入）安全。
    if (m_running.load(std::memory_order_acquire)) {
        return;
    }
    // 复位本次监督的过程态（观测位不在此复位——历史事实保留，见 stop 注；
    // 过程位与线程状态配对复位，保证 start/stop 多轮使用语义清晰）。
    m_stopRequested.store(false, std::memory_order_release);
    m_running.store(true, std::memory_order_release);
    m_thread = std::thread([this] { superviseLoop(); });
}

void StoreCloseSupervisor::stop() noexcept
{
    // 恒收敛（不以 running 位为跳过条件——监督循环的自然退出路径先置
    // 观测位后收尾〔放弃日志在 running 清位之后仍有写入〕，stop 若见
    // running==false 即跳过 join 会与收尾竞态：主线程在放弃日志写入前
    // 读快照——留痕丢失）。未启动＝线程对象不 joinable＝天然 no-op；
    // 重复调用＝二次 joinable 已 false＝幂等。
    m_stopRequested.store(true, std::memory_order_release);
    if (m_thread.joinable()) {
        m_thread.join();  // 阻塞至监督循环完全退出——返回后观测位/日志
                          // 全部可见（线程收尾 happens-before join 返回）
    }
    m_running.store(false, std::memory_order_release);
}

void StoreCloseSupervisor::superviseLoop()
{
    // 阈值计时起点（t0＝start 时刻——Config.abandonThreshold 的量度基准）。
    const auto now = [this] {
        return m_clock ? m_clock()
                       : std::chrono::steady_clock::now();
    };
    const auto t0 = now();
    const auto tDiag = t0 + m_config.abandonThreshold;  // ②段兜底诊断时点
    const auto tGiveUp = tDiag + m_config.giveUpGrace;  // ③段放弃监督时点
    bool abandonDiagWritten = false;  // ③段一次性标志（Dev 日志恰一条）

    // 监督循环（①~④序——头文件 start() 注）：**先观测后查停止**——
    // 循环体首拍必须完成闭合观测再让位给 stop 请求（宿主 start 后立即
    // stop 的短会话形态下，闭合事实不被停止请求吞掉；此后每拍同序）。
    // 线程约束澄清（tick 单线程域）：调度推进（TaskScheduler::tick——
    // 取消协议/排空监视/自动兜底的推进泵）归宿主**调度循环线程**（真实
    // 宿主形态：关闭期间调度循环持续 tick——Scheduler.hpp"tick 仅调度域
    // 单线程"）；本监督线程**只观测不驱动**（并发 tick＝锁域违例——
    // 实测会死锁），这正是"编排线程只观察不推进"（Lifecycle.cpp
    // awaitStoreClosed 同款纪律）的监督面表达。
    //   ① store 已闭合 → 记观测退出（关闭完成——监督成功收场）；
    //   ② 越兜底阈值仍未闭合 → 一次性 Dev 日志（"残留会话"告知——project
    //      §9.7 登记义务：强制 abandonAll 已由宿主调度循环的排空自动兜底
    //      承载，本日志承载"若仍未闭合说明存在非任务侧在途引用"的诚实
    //      告知）；
    //   ③ 越放弃宽限仍未闭合 → Dev 日志＋放弃退出（不阻塞进程退出——
    //      project §9.7 原文义务；非任务侧引用〔在途命令/草稿保存〕不是
    //      abandon 处置面，监督不能替它清零，如实留痕后退出）；
    //   ④ stop() 请求 → 退出（宿主主动收敛——run 返回后的正常路径）。
    while (true) {
        // ① 闭合观测（closed() 为内部互斥快照查询——并发安全）。
        if (m_store.closed()) {
            m_storeClosedObserved.store(true, std::memory_order_release);
            m_running.store(false, std::memory_order_release);
            return;
        }

        // ④ 停止请求（在闭合观测**之后**检查——首拍观测优先语义）。
        if (m_stopRequested.load(std::memory_order_acquire)) {
            break;
        }

        const auto t = now();

        // ② 兜底诊断（一次性——超阈值瞬间恰一条，不逐拍刷屏）。
        if (!abandonDiagWritten && t >= tDiag) {
            abandonDiagWritten = true;
            m_abandonDiagObserved.store(true, std::memory_order_release);
            if (m_devLog != nullptr) {
                m_devLog->logDev(
                    kCloseSupervisorChannel,
                    "关闭排空超阈值仍未闭合：强制 abandonAll(ForceTerminated) "
                    "已由排空编排自动兜底承载；若存储上下文仍未闭合，说明存在"
                    "非任务侧在途引用（在途命令/草稿保存）——继续监督至宽限");
            }
        }

        // ③ 放弃监督（不阻塞进程退出——§9.7 原文义务）。路径序：先
        // 置观测位（宿主 wait-gaveUp 循环的释放面）→**写日志**→清
        // running→return——日志写入先于线程退出，join 返回后必然可见。
        if (t >= tGiveUp) {
            m_gaveUp.store(true, std::memory_order_release);
            if (m_devLog != nullptr) {
                m_devLog->logDev(
                    kCloseSupervisorChannel,
                    "关闭监督放弃：宽限期内存储上下文仍未闭合（非任务侧在途"
                    "引用未释放）——残留事实已留痕，监督退出不阻塞进程退出；"
                    "请人工核查在途命令/草稿保存持有者");
            }
            m_running.store(false, std::memory_order_release);
            return;
        }

        // 拍间隔（awaitStoreClosed 同款 1 ms 拍频——避免忙等烧核；clock
        // 注入形态下的真实睡眠与虚拟推进解耦：测试用短阈值＋真实拍频）。
        std::this_thread::sleep_for(m_config.pollInterval);
    }

    // ④ stop() 请求退出——走到此处即宿主主动收敛（闭合与否以观测位为
    // 准——m_storeClosedObserved 只在真实观测到 closed() 时置位）。
    m_running.store(false, std::memory_order_release);
}

}  // namespace workflow
}  // namespace ird
}  // namespace sdurws
