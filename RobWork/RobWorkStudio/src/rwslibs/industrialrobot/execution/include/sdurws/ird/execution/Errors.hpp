/**
 * @file   Errors.hpp
 * @brief  execution 单元错误类型——ExecutionErrorCode（稳定 token 枚举）＋
 *         ExecutionError（唯一异常类型）。
 *
 * 设计依据：
 *   - units/execution.md §3.4（命名空间、错误类型与 CMake 集成——枚举值
 *     清单、token 对照、ExecutionError 形态、"17 个错误码 1:1＋2 个
 *     fail-fast token＋1 个状态标注码"的枚举对齐说明〔v0.2〕）
 *   - 需求 ERR-01（诊断记录字段——稳定码承载）、NFR-REL-05（用户可见文案
 *     不在本单元生成）、UX-03（正常用户取消不属于错误）
 *   - 任务契约 tasks/foundation/EX-T02.json（≙WP-08-T03；EX-SM-6 观测点
 *     "ExecutionError.code"——本头随 EX-T02 落地的直接动因）
 *
 * 背景说明（错误语义两分法，§10.8 接口共性约束表）：
 *   1. 调用方契约违约（本可由调用方避免的误用）→ 抛 ExecutionError，
 *      fail-fast，不允许静默返回默认值掩盖错误；
 *   2. 可预期失败（环境/运行条件导致，调用方应能处理）→ 经 Ack/诊断记录
 *      结构化反馈，不抛异常（如能力不支持、任务已终结的取消请求——
 *      §10.2 各前置行）。
 *   其中枚举值 ContextClosed/InvalidState 是纯 fail-fast token：按 §3.4
 *   v0.2 对齐说明，它们只用于异常携带与日志，不发稳定诊断码（稳定码权威
 *   ＝diagnostics StableCodeRegistry，已收编 18 项 EX-\* 码——见 DiagCodes）。
 *
 * 线程安全：枚举与异常均为纯值；toToken 纯函数（无共享可变状态）。
 */

#ifndef SDURWS_IRD_EXECUTION_ERRORS_HPP
#define SDURWS_IRD_EXECUTION_ERRORS_HPP

#include <stdexcept>
#include <string>

namespace sdurws::ird::execution {

/**
 * @brief execution 错误码枚举（§3.4 原文清单——token 稳定，不改名不重排）。
 *
 * 每个值的注释＝§3.4 清单行原文（稳定 token＋语义一句话）。码值分配权威
 * ＝diagnostics StableCodeRegistry（EX-\* 码 18 项已收编）；本枚举是
 * execution API 的错误分类面，与稳定码的对应关系见各值注释——其中
 * ContextClosed/InvalidState 无对应稳定码（调用方违约，token 仅供日志），
 * 状态标注码 EX-TASK-INTERRUPTED 反向地无对应枚举值（§5.1 Interrupted
 * 终态的状态标注诊断，非 API 错误）。
 */
enum class ExecutionErrorCode {
    SubmissionRejected,     ///< execution/submission-rejected   提交验证失败（快照/评估器/权限/预算）
    StaleSnapshot,          ///< execution/stale-snapshot        快照身份失效或载荷校验失败
    StoreReadOnly,          ///< execution/store-read-only       只读模式拒绝正式评估（PM-07）
    ResourceInsufficient,   ///< execution/resource-insufficient 资源不足（先节流后诊断，NFR-PERF-04）
    CapabilityUnsupported,  ///< execution/capability-unsupported 任务能力不支持（如暂停）——显式反馈非静默
    WorkerLaunchFailed,     ///< execution/worker-launch-failed  worker 启动失败
    WorkerCrashed,          ///< execution/worker-crashed        worker 异常退出（NFR-REL-02）
    WorkerHung,             ///< execution/worker-hung           心跳失联（超时判定）
    ForceTerminated,        ///< execution/force-terminated      强制终止（区别于普通失败的显式标记）
    ChannelProtocolError,   ///< execution/channel-protocol-error 帧非法/版本失配/序号断裂（开发诊断）
    RegistryUnknownRun,     ///< execution/registry-unknown-run  未知 RunId（迟到结果拒绝，开发诊断）
    RegistryMismatch,       ///< execution/registry-mismatch     五元组任一字段失配（开发诊断）
    StaleAttempt,           ///< execution/stale-attempt         陈旧 AttemptId（被取代尝试）
    CheckpointCorrupt,      ///< execution/checkpoint-corrupt    检查点完整性校验失败
    CheckpointIncompatible, ///< execution/checkpoint-incompatible 检查点不兼容（版本/身份/判定拒绝）
    ArchiveFailed,          ///< execution/archive-failed        归档失败（透传 StoreError 细节）
    ArchiveAuthorityLost,   ///< execution/archive-authority-lost 上下文已 Closed 的迟到归档（A7 防御）
    ContextClosed,          ///< execution/context-closed        调度器已排空/关闭（fail-fast token，不发稳定码）
    InvalidState,           ///< execution/invalid-state         非法转换请求（状态机拒绝；fail-fast token，不发稳定码）
};

/**
 * @brief 枚举→稳定 token（§3.4 各值注释中的 "execution/…" 串）。
 *
 * 用途：日志与开发诊断的机器判读面（诊断码本身是 EX-\* 稳定码，归
 * diagnostics；本 token 是 execution API 错误分类的日志形态）。
 *
 * @param v [in] 错误码枚举值
 * @return 稳定 token（静态存储期字符串字面量，调用方无须释放）；
 *         未知值返回 "execution/unknown"（防御分支——正常路径不可达，
 *         枚举值封闭）
 */
const char* toToken(ExecutionErrorCode v) noexcept;

/**
 * @brief execution 单元唯一异常类型（§3.4 契约：稳定 code＋开发诊断 detail）。
 *
 * 生命周期：按值抛出/捕获（标准异常惯例）；what() 由基类承载 detail。
 * detail 文案面向开发诊断（NFR-REL-05：用户可见文案不在本单元生成），
 * 约定前缀 "execution/<域>:"（§3.4 code() 注释原文）。
 *
 * 抛出语义（§10.8）：仅调用方契约违约抛出（如状态机矩阵外请求——
 * EX-SM-6）；可预期失败经结构化 Ack/诊断反馈，不走异常。
 *
 * 线程安全：不可变（code 在构造后只读）。
 */
class ExecutionError : public std::runtime_error {
public:
    /**
     * @brief 构造（code 与 detail 双载荷）。
     *
     * @param code   [in] 稳定错误码枚举值（code() 事后读取）
     * @param detail [in] 开发诊断明细，约定前缀 "execution/<域>:"；
     *                    空串允许（明细可缺，码不可缺）
     */
    ExecutionError(ExecutionErrorCode code, std::string detail);

    /// 构造时固定的错误码（noexcept 只读——异常对象不可变）。
    ExecutionErrorCode code() const noexcept { return m_code; }

private:
    ExecutionErrorCode m_code;   ///< 稳定错误码（构造后不可变）
};

}  // namespace sdurws::ird::execution

#endif  // SDURWS_IRD_EXECUTION_ERRORS_HPP
