/**
 * @file   ContractSuiteFacilities.hpp
 * @brief  EX-T09 契约测试套件共享设施——替身边界声明、故障点标识登记、
 *         虚拟时钟、诊断 sink 占位与脚本化评估产出替身（§11 设施列）。
 *
 * 设计依据：
 *   - units/execution.md §11（验证方案与故障注入矩阵：设施与替身清单、
 *     替身边界声明、末段"每条用例经 IRD_TEST_INFO 登记＋时钟敏感用例一律
 *     ManualClock"纪律）、§12 EX-T09 行（产物：execution/test/*、
 *     contract_test/*——FakeWorkerMain＋FakeArchivePort＋FaultInterceptor
 *     接缝）、§13 testkit 交接行（faultPointId 命名——TK-T11 触发式消费）
 *   - units/testkit.md §6.4（FaultPlan/FaultLog/FaultInterceptor 原语与
 *     `<unit>/<接口>/<动作>` 命名约定；fake 只进测试目标——T-1）、§6.5
 *     （TestProcessRunner/EventWatch 消费纪律）、§6.6（ManualClock 通用
 *     fake 模式——"接口由消费者单元定义，testkit 提供通用 fake 模式"）、
 *     §7.2/§7.3（ird-test-report.json 与 gtest XML 并存）
 *   - 任务契约 tasks/foundation/EX-T09.json acceptance 2/4/5（替身边界
 *     声明在案＋faultPointId 命名登记；P-EX-8 处置：sink 空实现占位＋
 *     断言只针对已收编 18 项 EX-* 码值；AT-10/11/13/34 场景载体）
 *
 * ★ 替身边界声明（acceptance 2——R-7/自审项 A-9 的落文件面，先读本段再
 *   读本套件任何用例）：
 *
 *   本套件全部替身输出**不构成任何业务算法正确性证明**（同 evidence
 *   EV-REG-3 边界声明、execution.md 自审项 A-9）。§11 设施列各替身在
 *   本套件的落位形态与"替身性"边界逐项登记如下：
 *
 *   1. ScriptedEvaluator——评估器行为的脚本化替身。阶段 A 无业务评估器
 *      接入（§12 交付边界），评估语义由脚本承载：worker 进程内＝
 *      `sdurws_ird_execution_worker` exe 内置脚本替身评估器（§3.4"阶段 A
 *      仅测试替身"；按脚本发进度/心跳/检查点/批次/最终产出/崩溃/卡死/
 *      序号异常——acceptance 2 所指 FakeWorkerMain 的落位本体，不另造
 *      第二个 worker）；主进程接纳侧＝本头 ScriptedOutputDecoder（按脚本
 *      逐次应答 EvaluationOutput）。两侧均为脚本应答器，不验证任何评估
 *      算法。
 *   2. FaultInterceptor 接缝——testkit §6.4 原语，包住被测单元本就要求
 *      的窄接口（归档端口＝execution/archive/* 家族；生产代码零 testkit
 *      头，fake 只编译进测试目标）。故障点标识登记见下方 kFault* 常量与
 *      registeredFaultPointIds()（testkit §6.4 交接义务的登记面）。
 *   3. ManualClock——虚拟时钟（§6.6 通用 fake 模式的消费者侧定义）。
 *      时钟敏感用例一律经 advance() 虚拟推进后断言，**禁止 sleep 断言**
 *      （§11 末段纪律；泵循环中的真实等待只用于 I/O 到达，不是时序判据
 *      ——WorkerProcessContractTest pumpUntil 同款先例）。
 *   4. TempDir／DeterministicEnv——testkit 公共设施（Fixture.hpp，TK-T08
 *      交付）：临时目录隔离与确定性上下文记录（repro 随 ird-test-report.json
 *      的每条 TestRecord 携带——bindFixtureContext 接缝）。
 *
 *   此外本套件大量复用"真实件"：接纳编排（ResultAdmission）、登记表
 *   （RunRegistry）、状态机、检查点/缓存协调器与 project 真实存储
 *   （ProjectStore）——替身只出现在"别人的能力"接缝（解码/反解/诊断/
 *   归档故障/评估脚本），判定与存储语义全部走真实实现。
 *
 * P-EX-8 处置口径（acceptance 4）：EX-* 稳定码与 IExecutionDiagnosticsSink
 *   的名称/归属统一归 diagnostics 单元裁决（单元卡 §15.3 登记未决）——
 *   本套件提供 NullExecutionDiagnosticsSink **空实现占位**（project §3.2
 *   同模式：diagnostics 适配器裁决前，测试/装配侧以空实现占位，不预建
 *   适配器）；全部稳定码断言只针对 kRegisteredExStableCodes（diagnostics.md
 *   §4.6 已收编 18 项的测试侧只读镜像——权威归 diagnostics DiagCodes，
 *   冻结 diff 后按影响面增量同步，P-EX-1 同纪律），零私定新码值。
 *
 * 线程约束：全部设施为测试目标单线程使用（gtest 串行纪律）——非线程安全。
 * 构建边界（T-1）：本头与一切 fake 只进 `_test`/`_contract_test` 目标；
 *   产品库零 testkit 零 gtest（LinkageContractTest 持续钉住）。
 */

#ifndef SDURWS_IRD_EXECUTION_TEST_CONTRACT_SUITE_FACILITIES_HPP
#define SDURWS_IRD_EXECUTION_TEST_CONTRACT_SUITE_FACILITIES_HPP

#include <sdurws/ird/core/DiagData.hpp>        // DiagnosticRecord（sink 签名面）
#include <sdurws/ird/execution/Ports.hpp>      // IExecutionDiagnosticsSink／IEvaluationOutputDecoder（被替身的消费者自有接口）

#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sdurws::ird::execution {
namespace exsuite {

// =====================================================================
// 故障点标识登记（acceptance 2——testkit §6.4 交接义务）
// =====================================================================
//
// 命名约定（testkit §6.4 原文）：`<unit>/<接口>/<动作>`。本表是 execution
// 单元对 testkit §10.2 交接清单（"execution：进程启动/通道发送"）的登记
// 兑现：接缝最小集＝进程启动一处＋通道收发两处＋归档端口家族（family
// 通配 execution/archive/*，展开为端口四方法）。故障注入用例（EX-ARC-1/2
// 等）经 testkit FaultInterceptor 按这些 ID 接线；生产代码不含 testkit 头、
// 不加 #ifdef TEST（§6.4 接缝原则——接缝＝生产窄接口本身）。

/// 进程启动接缝（IProcessOps 故障面——src/win32/ProcessLauncher 生产窄接口）。
inline constexpr const char* kFaultProcessLauncherCreate = "execution/process-launcher/create";
/// 通道发送接缝（ChannelPair 数据/命令流写面——EX-WKR/CHN 真进程故障面）。
inline constexpr const char* kFaultChannelSendFrame = "execution/channel/send-frame";
/// 通道接收接缝（ChannelPair 读面/FrameAssembler——帧错乱注入位）。
inline constexpr const char* kFaultChannelRecvFrame = "execution/channel/recv-frame";
/// 归档端口 begin 接缝（execution/archive/* 家族——§11"经 project 端口 fake"）。
inline constexpr const char* kFaultArchiveBegin = "execution/archive/begin";
/// 归档端口 writeBatch 接缝（EX-ARC-1 磁盘满/EX-ARC-2 写入拒绝的注入位）。
inline constexpr const char* kFaultArchiveWriteBatch = "execution/archive/write-batch";
/// 归档端口 finalize 接缝（manifest 发布故障面）。
inline constexpr const char* kFaultArchiveFinalize = "execution/archive/finalize";
/// 归档端口 abandon 接缝（责任终结故障面——观测为主）。
inline constexpr const char* kFaultArchiveAbandon = "execution/archive/abandon";

/// 登记清单（顺序固定——确定性；设施自证用例比对此表，防登记漂移）。
inline std::vector<const char*> registeredFaultPointIds()
{
    return {kFaultProcessLauncherCreate, kFaultChannelSendFrame, kFaultChannelRecvFrame,
            kFaultArchiveBegin, kFaultArchiveWriteBatch, kFaultArchiveFinalize,
            kFaultArchiveAbandon};
}

// =====================================================================
// EX-* 稳定码只读镜像（acceptance 4——断言只针对已收编 18 项）
// =====================================================================
//
// 权威源＝diagnostics 单元 StableCodeRegistry（DiagCodes.hpp/.cpp，DIAG-T03
// 落位）。本表是 diagnostics.md §4.6 已收编 EX-* 清单的**测试侧只读镜像**，
// 仅用于套件内断言自检（"本套件断言过的每个稳定码都在收编清单内"——
// ContractSuiteFacilitiesTest 具名钉住）；不参与任何产品逻辑，不构成第二
// 码表（码值裁决权在 diagnostics——P-EX-8 不私定）。diagnostics 冻结出
// diff 时本表按影响面增量同步（P-EX-1 纪律）。

/// 已收编 18 项 EX-* 稳定码（diagnostics.md §4.6——按码表序誊录）。
inline std::vector<const char*> registeredExStableCodes()
{
    return {
        "EX-ARCHIVE-AUTHORITY-LOST", "EX-ARCHIVE-FAILED", "EX-CAPABILITY-UNSUPPORTED",
        "EX-CHANNEL-PROTOCOL-ERROR", "EX-CHECKPOINT-CORRUPT", "EX-CHECKPOINT-INCOMPATIBLE",
        "EX-FORCE-TERMINATED", "EX-REGISTRY-MISMATCH", "EX-REGISTRY-UNKNOWN-RUN",
        "EX-RESOURCE-INSUFFICIENT", "EX-SNAPSHOT-STALE", "EX-STALE-ATTEMPT",
        "EX-STORE-READ-ONLY", "EX-TASK-INTERRUPTED", "EX-TASK-REJECTED",
        "EX-WORKER-CRASHED", "EX-WORKER-HUNG", "EX-WORKER-LAUNCH-FAILED",
    };
}

/// 码值是否在已收编清单内（套件断言的自检闸——新码值出现即自检失败，
/// 把"不私定码值"从纪律变成可执行检查）。
inline bool isRegisteredExStableCode(std::string_view code)
{
    for (const char* registered : registeredExStableCodes()) {
        if (code == registered) {
            return true;
        }
    }
    return false;
}

// =====================================================================
// ManualClock——虚拟时钟（§11 末段纪律／testkit §6.6）
// =====================================================================

/**
 * @brief 测试虚拟时钟（steady 域——now()/advance() 两面）。
 *
 * 用法纪律（§11 末段/testkit §6.5）：时钟敏感用例（2 s/10 s 协作窗、心跳
 * 失联窗、排空兜底阈值）一律以本时钟作被测编排的注入时钟，测试内
 * advance() 虚拟推进后调用 poll 类编排原语并断言状态——**不得**以真实
 * sleep 等待时序（真实时延不是被测语义，且在慢机上会把参数误判放大成
 * 用例失败，execution.md 风险 R-4）。start 缺省取真实当前时刻——仅作
 * 可读的时点基线，断言只用相对推进量（不与真实墙钟比较）。
 */
class ManualClock {
public:
    using time_point = std::chrono::steady_clock::time_point;

    /// 以给定时点起步（缺省＝构造时刻的真实 steady 值——可读基线）。
    explicit ManualClock(time_point start = std::chrono::steady_clock::now())
        : m_now(start)
    {
    }

    /// 被测编排的 ClockFn 注入形态（与 TaskController::ClockFn 等消费接口
    /// 的签名对齐——可调用对象直接绑本方法）。
    time_point now() const noexcept { return m_now; }

    /// 虚拟推进（单位 ms；只前进不回退——回退会造出"先于起点"的负时序，
    /// 无被测语义）。
    void advance(std::chrono::milliseconds delta) noexcept { m_now += delta; }

private:
    time_point m_now;   ///< 当前虚拟时刻（advance 单调前进）
};

// =====================================================================
// NullExecutionDiagnosticsSink——P-EX-8 空实现占位（acceptance 4）
// =====================================================================

/**
 * @brief 诊断 sink 的**空实现占位**（P-EX-8——project §3.2 同模式）。
 *
 * 背景（为何是空实现而不是适配器）：IExecutionDiagnosticsSink 的名称/
 * 归属统一归 diagnostics 单元裁决（单元卡 §15.3 P-EX-8 登记未决——裁决
 * 权在所有者，测试侧不私定）。在裁决落地前，测试/装配侧需要一份"能被
 * 注入、不产生任何外效"的最小 sink 占位——project 单元在 PRJ-T03 起的
 * 同款先例（project.md §3.2："IDiagnosticsSink……diagnostics 详设产出前
 * 以空实现占位于测试/装配侧"）。裁决后本占位随单元卡增量修订收编统一
 * 形态。
 *
 * 断言纪律（acceptance 4 后半）：需要**观测**诊断的用例不用本占位，而
 * 用各测试文件内的收集型替身（CollectingSink——观测面，不是装配占位）；
 * 收集型替身上断言的稳定码必须全部通过 isRegisteredExStableCode 自检
 * （18 项收编清单内，零私定）。
 */
class NullExecutionDiagnosticsSink final : public IExecutionDiagnosticsSink {
public:
    /// 结构化通道：空实现（占位语义——无输出面可断言；裁决前不外报）。
    void report(const core::DiagnosticRecord&) override {}

    /// 开发通道：空实现（同上——NFR-REL-05 的"不进用户界面"在测试装配
    /// 下的极端形态：哪儿都不进）。
    void reportDev(const std::string&, const std::string&) override {}
};

// =====================================================================
// ScriptedOutputDecoder——ScriptedEvaluator 的接纳侧承载（§11 设施列）
// =====================================================================

/**
 * @brief 脚本化评估产出解码替身（IEvaluationOutputDecoder 的测试实现）。
 *
 * 边界（复述文件头声明 1 的接纳侧半边）：按脚本逐次应答预置的
 * EvaluationOutput——脚本耗尽返回 nullopt（即"解码失败"注入形态，接纳侧
 * PayloadUndecodable 拒绝路径的驱动面）。它只应答脚本，不解码任何真实
 * 载荷语义——对评估算法的正确性零证明力（R-7）。
 *
 * worker 侧对偶＝sdurws_ird_execution_worker exe 的内置脚本替身评估器
 * （FakeWorkerMain 本体——按脚本发进度/心跳/结果/崩溃/卡死，§3.4）：
 * 两侧共同构成 §11"ScriptedEvaluator"的端到端形态，不另建第二套脚本
 * 语言（PA-1 不私造语义；脚本格式即 worker/main.cpp 实现的
 * "stage-a-script:v1" 行文）。
 */
class ScriptedOutputDecoder final : public IEvaluationOutputDecoder {
public:
    /// 入脚本一条应答（nullopt 合法＝该次到达按解码失败拒绝）。
    void enqueue(std::optional<evidence::EvaluationOutput> output)
    {
        m_script.push_back(std::move(output));
    }

    /// 消脚本一条（FIFO；耗尽＝nullopt——解码失败注入，不抛）。
    std::optional<evidence::EvaluationOutput> tryDecode(
        const std::vector<std::uint8_t>&) const override
    {
        if (m_cursor >= m_script.size()) {
            return std::nullopt;
        }
        return std::move(m_script[m_cursor++]);
    }

private:
    /// 应答脚本（入队序＝应答序——确定性 FIFO，NFR-COR-02）。
    std::vector<std::optional<evidence::EvaluationOutput>> m_script;
    /// 下一条应答的下标（FIFO 游标——vector 头删代价高，测试用游标等价）。
    mutable std::size_t m_cursor = 0;
};

}  // namespace exsuite
}  // namespace sdurws::ird::execution

#endif  // SDURWS_IRD_EXECUTION_TEST_CONTRACT_SUITE_FACILITIES_HPP
