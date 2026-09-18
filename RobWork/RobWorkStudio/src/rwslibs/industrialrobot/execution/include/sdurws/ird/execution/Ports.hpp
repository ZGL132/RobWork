/**
 * @file   Ports.hpp
 * @brief  execution 的最小注入端口（§3.3）——诊断 sink、名称反解适配、
 *         评估产出解码三个接缝；适配器归 L5 应用壳装配期统一提供。
 *
 * 设计依据：
 *   - units/execution.md §3.3（对 runtime / policy / diagnostics 能力的
 *     注入边界——值传递＋最小注入接口；适配器归 L5，不归 execution）
 *   - ARCHITECTURE.md §3.5（依赖白名单：execution 只登记 core/evidence/
 *     project/diagnostics 四条边——runtime/policy 不在表内，SA-10 表外边
 *     ＝构建失败）、§4.5（接纳前名称反解——CON-06）
 *   - 需求 CON-06（运行时名称必须先反解为对象 ID 才能接纳结果）、
 *     ERR-01/NFR-REL-05（诊断两级：用户文案归 diagnostics/ui，本单元只
 *     产出结构化记录与开发通道消息）
 *   - 任务契约 tasks/foundation/EX-T04.json acceptance 3（名称反解经⑥
 *     端口注入解析器 INameResolverAdapter——P-EX-3 处置：runtime 零编译
 *     依赖不私建边；九步接纳步 8 编排消费本头接口）
 *
 * 背景说明（为什么这些接口出现在 execution 而实现不在这里）：
 *   execution 在接纳路径上需要三类"别人的能力"：①诊断上报（diagnostics
 *   单元的设施）；②运行时名称→对象 ID 反解（runtime ⑥端口的设施）；
 *   ③评估产出 canonical 字节的解码（worker 通道编解码——EX-T06 通道协议
 *   的对端）。若直接 include 这些单元的头，execution→runtime 就是 ARCH
 *   §3.5 表外边（构建失败）；diagnostics 边虽已登记，但链接形态按 P-EX-8
 *   裁决暂不落链接（CMakeLists 同款注）。因此三者都以"execution 定义最小
 *   接口形状、L5 装配期注入适配器"的形态消费——接口形状即本头全部内容，
 *   实现零在这里（依赖方向隔离，ARC-02 端口精神的实施件）。
 *
 * 头内归置登记（相对 §3.1 组成表的一处增量，登记单元卡 §15.4）：
 *   §3.1 的 Ports.hpp 行还登记 IExecutionModelService/ICompileCacheJudge
 *   两接口（Preparing 段模型准备/编译缓存判定注入）——其唯一消费者是
 *   调度线程的派发路径（EX-T05 调度器＋EX-T08 缓存治理），本头随 EX-T04
 *   先落接纳路径已消费的三接口，那两件随其消费任务落位（EX-T03
 *   Controller.hpp 先落编排接缝的归置先例），届时增量登记。
 *
 * 线程约束：三个接口的实现方各自保证线程安全；execution 侧的全部调用都
 *   发生在调度线程（RunRegistry/接纳编排的唯一写者域——§4.2/§9.2），实现
 *   方按"调度线程串行调用"设计即可，无须为并发付费。
 */

#ifndef SDURWS_IRD_EXECUTION_PORTS_HPP
#define SDURWS_IRD_EXECUTION_PORTS_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sdurws/ird/core/Identity.hpp>      // ContentIdentity（名称映射绑定键）、ObjectId（反解目标）
#include <sdurws/ird/evidence/Evaluator.hpp> // EvaluationOutput（解码产出——已注册边的公共头）

namespace sdurws::ird::execution {

// =====================================================================
// IExecutionDiagnosticsSink（§3.3 原文形态——诊断注入）
// =====================================================================

/**
 * @brief execution 诊断上报注入口（§3.3 原文名 IExecutionDiagnosticsSink；
 *        形态对齐 project §5.0 IDiagnosticsSink——同一模式的两单元各自
 *        最小接口，不共享类型，避免 execution→project 之外的耦合）。
 *
 * 两个通道的分工（§3.4 稳定码清单＋§9.3"迟到结果拒绝是否留诊断"行）：
 *   - report：携带完整 core::DiagnosticRecord 的结构化上报（稳定码在
 *     record.code——码值权威归 diagnostics StableCodeRegistry，已收编
 *     EX-\* 18 项；execution 只引用码面 token，不私定码值）；
 *   - reportDev：开发级自由文本通道（拒绝原因、装配漂移这类只有工程师
 *     看的消息——NFR-REL-05：不进用户界面文案）。
 *
 * P-EX-8 处置口径（登记单元卡 §15.3/§15.4）：sink 的名称/归属统一归
 *   diagnostics 单元裁决；在裁决落地前，本接口是 execution 侧的注入形状，
 *   测试/装配侧以轻量替身实现（project §3.2 同模式），不预建 diagnostics
 *   适配器。
 *
 * 线程约束：execution 仅在调度线程调用（RunRegistry/接纳编排的写者域）；
 *   实现方无须保证并发安全。
 */
class IExecutionDiagnosticsSink {
public:
    virtual ~IExecutionDiagnosticsSink() = default;

    /**
     * @brief 结构化诊断上报（稳定码诊断——ERR-01 承载面）。
     *
     * @param record [in] 已按 core::DiagnosticRecord::make 校验的记录
     *                    （code 为 StableCodeRegistry 已收编的稳定码，
     *                    如 EX-REGISTRY-UNKNOWN-RUN；本接口不二次校验）
     */
    virtual void report(const core::DiagnosticRecord& record) = 0;

    /**
     * @brief 开发级自由文本上报（不进用户界面——NFR-REL-05）。
     *
     * @param channel [in] 通道标识（约定＝稳定码 token 或域前缀，如
     *                 "EX-REGISTRY-MISMATCH"、"execution/admission"；
     *                 P-EX-8 裁决前按此约定，裁决后随其统一）
     * @param message [in] 开发诊断明细（面向日志/评审，机器不解析）
     */
    virtual void reportDev(const std::string& channel, const std::string& message) = 0;
};

// =====================================================================
// INameResolverAdapter（§3.3 原文形态——⑥端口反解注入；P-EX-3 处置件）
// =====================================================================

/**
 * @brief 运行时名称→对象 ID 反解注入口（§3.3 原文名 INameResolverAdapter
 *        ——L5 把 runtime ⑥端口〔RuntimeNameMap 反解〕适配为本接口）。
 *
 * 为什么经注入而不是直接链接 runtime（P-EX-3 处置，acceptance 3）：
 *   ARCH §3.5 未登记 execution→runtime 边，任务指令与架构表的出入按
 *   架构白名单执行（R-1/R-2 门禁硬约束，SA-10 表外边＝构建失败）；本
 *   接口把"需要的能力"与"能力的来源"解耦——如架构侧将来补登边，适配器
 *   可原样退役为直连（接口形状不变，D-17 同款）。
 *
 * 接纳语义（九步步 8，§9.2；需求 CON-06 原文"运行时名称必须先反解为
 *   对象 ID 才能接纳结果"）：到达结果的载荷材料里出现的每个运行时名称
 *   都必须按**登记时绑定的名称映射**（登记记录 nameMapIdentity——绝不用
 *   当前映射，否则项目切换后反解结果漂移）成功反解；任一名称反解失败
 *   ＝结果不可接纳（AdmissionDecision::RejectReason::NameUnresolved）。
 *   先反解、后接纳——反解失败的到达在 envelope 组装（步 9）之前即被
 *   拒绝，不产生任何正式结果对象。
 *
 * 线程约束：仅调度线程调用；实现方按串行调用设计。
 */
class INameResolverAdapter {
public:
    virtual ~INameResolverAdapter() = default;

    /**
     * @brief 按指定名称映射把一个运行时名称反解为对象 ID。
     *
     * @param nameMapIdentity [in] 登记记录绑定的名称映射内容身份
     *                        （CON-06——反解永远用登记值，不用当前值）
     * @param runtimeName     [in] 运行时名称（worker 载荷材料中出现；
     *                        可为任意非空文本——映射里没有即返回 nullopt）
     * @return 反解所得对象 ID；该映射下无此名称（或映射不可得）返回
     *         nullopt——调用方（接纳步 8）据以拒绝接纳，不猜、不兜底
     *
     * 线程约束：仅调度线程调用。
     */
    virtual std::optional<core::ObjectId> tryResolve(
        core::ContentIdentity nameMapIdentity, std::string_view runtimeName) const = 0;
};

// =====================================================================
// IEvaluationOutputDecoder（评估产出解码注入——归置增量，见文件头登记）
// =====================================================================

/**
 * @brief 评估产出 canonical 字节→结构化 EvaluationOutput 的解码注入口。
 *
 * 归置说明（相对 §3.1/§3.3 的一处增量，登记单元卡 §15.4）：到达信封
 *   （ArrivalEnvelope）的 payloadCanon 按 §10.5 携带"EvaluationOutput
 *   canonical"字节；其字节级规范形态是 worker 通道协议（EX-T06，
 *   ChannelProtocol.hpp）的契约面——编解码器既不属于 evidence（评估器
 *   只产出素材，不管传输），也不该由 execution 私造第二份编码（域外
 *    duplicating——PA-1/NFR-MNT-04）。因此 execution 只定义解码接缝：
 *   L5/EX-T06 装配期把通道解码器适配为本接口注入；测试以内存替身提供。
 *
 * 失败语义：解码失败（字节非法/版本不符/截断）返回 nullopt，**不抛**——
 *   接纳侧据以出具 Rejected(PayloadUndecodable)（载荷校验失败属可预期
 *   的通道数据错误，走结构化拒绝而非异常 fail-fast；AGENTS §3 错误二分
 *   的"可预期失败"侧）。
 *
 * 线程约束：仅调度线程调用。
 */
class IEvaluationOutputDecoder {
public:
    virtual ~IEvaluationOutputDecoder() = default;

    /**
     * @brief 解码一份评估产出 canonical 字节。
     *
     * @param canonical [in] 到达信封携带的 canonical 字节（原样透传，
     *                  本接口不校验五元组/绑定——那些是九步 1~5 的核对面）
     * @return 结构化评估产出；字节非法返回 nullopt（接纳侧拒绝）
     *
     * 线程约束：仅调度线程调用。
     */
    virtual std::optional<evidence::EvaluationOutput> tryDecode(
        const std::vector<std::uint8_t>& canonical) const = 0;
};

}  // namespace sdurws::ird::execution

#endif  // SDURWS_IRD_EXECUTION_PORTS_HPP
