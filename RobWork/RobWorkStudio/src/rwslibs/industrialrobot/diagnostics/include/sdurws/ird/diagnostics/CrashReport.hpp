/**
 * @file   CrashReport.hpp
 * @brief  崩溃诊断文件（CrashReport）——§7.6 写出设施：脱敏后的进程/版本
 *         信息＋异常摘要＋最近 512 行开发日志快照＋活动任务/finding 清单＋
 *         打开的项目身份，写用户目录；写出失败降级 stderr，绝不阻塞/不抛。
 *
 * 设计依据：
 *   - units/diagnostics.md §7.6（崩溃前日志保留与崩溃诊断文件——内容六段、
 *     写出位置＝用户目录〔ARCH §5.4；不入 .rwdesign——D-10〕、写出失败→
 *     stderr 摘要＋放弃〔不递归诊断〕、AT-11 观测点"诊断文件写出且脱敏"）、
 *     §9.5（IRedactionService——崩溃文件为合法消费方）、§7.5 尾注（主进程
 *     崩溃诊断文件引用 worker 本地日志路径——脱敏后）
 *   - 需求 PM-17（异常/崩溃产生可报告的诊断文件）、NFR-SEC-07（脱敏——
 *     凭据零命中、路径按配置）、NFR-REL-01 精神（DT-LIFE-4：磁盘不足降级、
 *     不崩溃、不阻塞调用方）
 *   - 任务契约 tasks/foundation/DIAG-T08.json（≙WP-09-T05）：acceptance 2
 *     （DT-LIFE-4）、acceptance 3（崩溃诊断文件写出设施就位；OS 级崩溃捕获
 *     与应用壳装配归 WP-24-T02——dtb WP-09-T05 协作口径，本任务不越界实现
 *     dump 机制）、acceptance 4（P-DIAG-1）
 *
 * 背景说明（为什么是"写出设施"而不是"崩溃捕获器"）：
 *   §7.6 冻结的分工：OS 级崩溃捕获（minidump/SEH/信号）与"在异常捕获点调
 *   用本设施"的应用壳装配归 WP-24-T02（dtb WP-09-T05 协作口径）；本单元只
 *   承诺"给我崩溃现场数据，我写出一份脱敏、可定位、尽力而为的键值文本文
 *   件"——捕获机制与平台强相关，而文件格式与脱敏是 diagnostics 的数据契约
 *   职责（PA-1 权威归属）。设施被设计为 noexcept：崩溃路径上任何次生异常
 *   都会把"报告崩溃"变成"制造崩溃"——所有失败一律降级（stderr＋开发诊断
 *   一条，不再递归）。
 *
 * ★ P-DIAG-1 处置（契约 acceptance 4——崩溃文件携带的诊断摘要内嵌 core 契
 *   约数据）：CrashReportInput 直接以 core 值类型承载身份数据——openProject
 *   ＝core::ProjectId、activeTasks 成员＝core::TaskIdentity（TASK-03 五元组
 *   原样内嵌，不另造字符串形态；写出时经 toCanonical() 规范文本）。core.md
 *   v0.1（Draft 未冻结）为基线——基线漂移由测试侧 static_assert 编译期钉住
 *   （RedactionTest.cpp DtPdiag1 组），core 冻结 diff 后增量同步，不私改 core。
 *
 * 线程安全：write 可并发调用（本类无可变状态；文件接缝与脱敏服务各自的
 * 线程契约见其头文件——FileLogFileOps 内部互斥、IRedactionService 并发安
 * 全）。生命周期：四个注入依赖均为非拥有引用/指针，须覆盖本 writer 生命
 * 周期；devLog 可为空（降级诊断静默——装配前合法降态）。
 * 确定性：文件名与 writtenUtc 由注入 IClock 打点（测试注入 ManualClock 得
 * 到可断言的文件名/时间戳）；内容组装为纯函数（同输入同字节——除 IClock
 * 打点与 PID 外无环境依赖）。
 */

#ifndef SDURWS_IRD_DIAGNOSTICS_CRASHREPORT_HPP
#define SDURWS_IRD_DIAGNOSTICS_CRASHREPORT_HPP

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <sdurws/ird/core/Identity.hpp>         // core::ProjectId/TaskIdentity（P-DIAG-1 基线内嵌）
#include <sdurws/ird/diagnostics/Catalog.hpp>   // IClock（writtenUtc 打点——§4.2 同源）
#include <sdurws/ird/diagnostics/Confirmable.hpp>  // FindingId/FindingState（活动清单——§7.6）
#include <sdurws/ird/diagnostics/Logging.hpp>   // ILogFileOps（文件接缝——testkit D-10 注入面）
#include <sdurws/ird/diagnostics/Redaction.hpp> // IRedactionService（§9.5——崩溃文件合法消费方）

namespace sdurws::ird::diagnostics {

// =====================================================================
// 常量（§7.6 行原文数值）
// =====================================================================

/// 异常消息摘要上限（§7.6"异常类型与消息摘要（截断 512 B）"；经 safeSummary
/// 脱敏＋UTF-8 边界安全截断——Redaction.hpp）。
inline constexpr std::size_t kCrashReportExceptionMessageMaxBytes = 512;

/// 崩溃文件内开发日志快照行数上限（§7.6"最近 512 行开发日志快照"；数据源
/// ＝LoggingPipeline::devTailSnapshot——同值常量 kLogDevTailRingLines）。
inline constexpr std::size_t kCrashReportDevTailLines = 512;

// =====================================================================
// CrashReportInput（§7.6 内容六段的调用方输入形态）
// =====================================================================

/**
 * @brief 活动 finding 清单项（§7.6"活动 finding 清单（findingId/状态，不含
 *        载荷）"——载荷＝确认凭据/绑定四元组等，一律不入崩溃文件）。
 *
 * 值语义；纯值。
 */
struct CrashReportActiveFinding {
    FindingId id;                                   ///< finding 规范身份（fnd-<32hex>）
    FindingState state = FindingState::Pending;     ///< 服务端状态（§5.4 四值）
};

/**
 * @brief 崩溃现场输入（§7.6 内容六段的载体；由 L5 应用壳在异常捕获点组装
 *        ——PM-17；WP-24-T02 协作边界见文件头）。
 *
 * 值语义；纯值（并发只读安全）。自由文本字段（processInfo/exceptionType/
 * exceptionMessage/devLogTail 元素）由写出侧统一脱敏＋换行消毒——调用方无
 * 需预脱敏（防线不依赖调用方自觉，§9.6 同款纪律）。
 */
struct CrashReportInput {
    /// 进程/版本信息（如 "RobWorkStudio 1.2.3 (x64)"；脱敏后写入）。
    std::string processInfo;
    /// 进程 PID（0＝由写出侧自取当前进程 PID——常规形态；测试注入固定值
    /// 以获得确定性文件名）。
    std::uint64_t processId = 0;
    /// 异常类型名（如 "std::runtime_error"；脱敏后写入）。
    std::string exceptionType;
    /// 异常消息原文（写出侧经 safeSummary 截断 512 B＋脱敏——§7.6）。
    std::string exceptionMessage;
    /// 最近开发日志行快照（通常取自 LoggingPipeline::devTailSnapshot
    /// (kCrashReportDevTailLines)；写出侧逐行再脱敏——双保险，§7.6"内容＝
    /// 脱敏后的"不依赖第一道防线的完备性）。
    std::vector<std::string> devLogTail;
    /// 活动任务清单（五元组原样内嵌——P-DIAG-1；TASK-03）。
    std::vector<core::TaskIdentity> activeTasks;
    /// 活动 finding 清单（findingId/状态，不含载荷——§7.6）。
    std::vector<CrashReportActiveFinding> activeFindings;
    /// 打开的项目身份（projectId 规范文本；**不含路径原文**——项目路径按
    /// 策略脱敏且本结构不承载路径，§7.6"不含路径原文"）。
    std::optional<core::ProjectId> openProject;
};

// =====================================================================
// CrashReportWriter（§7.6 CrashReportWriter——写出设施）
// =====================================================================

/**
 * @brief 崩溃诊断文件写出器（§7.6"由 L5 应用壳在异常捕获点调用"）。
 *
 * 写出语义（§7.6 逐条）：
 *   - 位置＝调用方指定的用户目录（ARCH §5.4"用户目录\崩溃诊断文件"；**不
 *     入 .rwdesign**——D-10。目录由 L5 装配给产品路径、测试给临时目录——
 *     本类不查询进程环境，与脱敏服务同款确定性纪律）；文件名
 *     "crash-<epochMs>-<pid>.log"（UTC 毫秒来自注入 IClock＋进程 PID）；
 *   - 内容＝键值文本（§1.4"自有文本/键值格式"），六段：format/进程与版本/
 *     异常类型与消息摘要（≤512 B）/项目身份/活动任务/活动 finding/开发日
 *     志快照——全部自由文本经脱敏（Dev 档 NFR-SEC-07 全量；异常消息经
 *     safeSummary＝User 档＋512 B 截断——报告面不带栈/地址/哈希）；
 *   - 逐行追加经 ILogFileOps 接缝（开-写-关——FileLogFileOps 单行落盘，
 *     中途崩溃留部分文件仍有诊断价值）；任何一行失败即停止后续行；
 *   - 失败降级：stderr 一行摘要（含按策略脱敏后的目录——排障可用且不泄
 *     敏）＋开发诊断一条（DIAG-LOG-WRITE-FAILED——码表 §4.6 ⑤复用：87 码
 *     内置表冻结、无 crash 专属码，崩溃文件写出属诊断写出面，消息以
 *     "[crash-report-write-failed]" 前缀区分；登记 §14.4 v0.9），**不递归
 *     诊断**（诊断发射失败只吞不重试）；
 *   - 绝不抛出、绝不长阻塞（同步单遍写——崩溃路径的既定形态；阻塞面仅
 *     ILogFileOps 契约内的快速 I/O）。
 */
class CrashReportWriter final {
public:
    /**
     * @brief 构造（L5 装配期注入依赖）。
     *
     * @param redaction [in] 脱敏服务（§9.5 合法消费方"崩溃文件"；非拥有，
     *                  引用须覆盖本 writer 生命周期）
     * @param clock     [in] 时钟（文件名 epochMs 与 writtenUtc 打点；测试
     *                  注入 ManualClock 形态）
     * @param fileOps   [in] 文件接缝（逐行追加/冲刷；DT-LIFE-4 磁盘满注入点
     *                  ——ILogFileOps 契约同 DT-LOG-3）
     * @param devLog    [in] 开发诊断路由（写出失败降级诊断的承载；可空＝
     *                  静默降级——装配前合法降态。非拥有）
     */
    CrashReportWriter(const IRedactionService& redaction, const IClock& clock,
                      ILogFileOps& fileOps, IDevLogSink* devLog = nullptr) noexcept;

    // 禁拷贝/禁移动：引用注入面要求地址稳定（同单元设施纪律）。
    CrashReportWriter(const CrashReportWriter&) = delete;
    CrashReportWriter& operator=(const CrashReportWriter&) = delete;

    ~CrashReportWriter() = default;

    /**
     * @brief 写出崩溃诊断文件（§7.6；noexcept——崩溃路径绝不允许次生异常）。
     *
     * @param input     [in] 崩溃现场输入（六段载体；自由文本由本方法脱敏）
     * @param directory [in] 目标目录（用户目录——ARCH §5.4/D-10；不存在时
     *                  best-effort 创建，创建失败由后续写失败路径承载）
     *
     * @return 写出的文件完整路径；失败＝nullopt（已 stderr＋开发诊断降级）
     *
     * @throws 无（noexcept；全部环境/逻辑异常捕获降级——acceptance"不崩溃"）
     */
    std::optional<std::filesystem::path> write(const CrashReportInput& input,
                                               const std::filesystem::path& directory) noexcept;

private:
    const IRedactionService* m_redaction;  ///< 脱敏服务（非拥有——见构造注释）
    const IClock* m_clock;                 ///< 时钟（非拥有——文件名/时间戳打点）
    ILogFileOps* m_fileOps;                ///< 文件接缝（非拥有——DT-LIFE-4 注入点）
    IDevLogSink* m_devLog;                 ///< 开发诊断路由（非拥有；可空）
};

}  // namespace sdurws::ird::diagnostics

#endif  // SDURWS_IRD_DIAGNOSTICS_CRASHREPORT_HPP
