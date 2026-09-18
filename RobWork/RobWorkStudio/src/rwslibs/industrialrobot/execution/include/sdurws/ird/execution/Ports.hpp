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
 * 头内归置登记（相对 §3.1 组成表的两处增量，登记单元卡 §15.4）：
 *   ①§3.1 的 Ports.hpp 行登记 IExecutionModelService/ICompileCacheJudge
 *   两接口（Preparing 段模型准备/编译缓存判定注入）——ICompileCacheJudge
 *   随其消费任务 EX-T08 落位；IExecutionModelService 已随 EX-T06 落位
 *   （EX-T05 曾登记"归置修正为随 EX-T06/T08 落位"——真实消费者是
 *   Preparing 段派发编排：WorkerAssignment.dispatch 的物化字节由其产出）；
 *   ②随 IExecutionModelService 一并落位 §3.3 原文点名的三件承载类型
 *   MaterializedDispatch/ModelPrepareResult/PrepareOptions（§3.3 代码段
 *   原文形态；PrepareOptions 字段面阶段 A 仅最小承载，随 runtime 注入
 *   适配器装配定形）。
 *
 * 线程约束：各接口的实现方各自保证线程安全；execution 侧的全部调用都
 *   发生在调度线程（RunRegistry/接纳编排/派发编排的唯一写者域——
 *   §4.2/§9.2/§6.4），实现方按"调度线程串行调用"设计即可，无须为并发
 *   付费。ICancelSignal 的实现例外——它被跨线程置位（取消命令任意线程
 *   受理），见 Controller.hpp 其注。
 */

#ifndef SDURWS_IRD_EXECUTION_PORTS_HPP
#define SDURWS_IRD_EXECUTION_PORTS_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sdurws/ird/core/Identity.hpp>      // ContentIdentity（名称映射绑定键、物化身份）、ObjectId（反解目标）
#include <sdurws/ird/evidence/Evaluator.hpp> // EvaluationOutput（解码产出——已注册边的公共头）
#include <sdurws/ird/evidence/Snapshot.hpp>  // AnalysisSnapshot（模型准备输入——已注册边的公共头）

namespace sdurws::ird::execution {

// 前置声明：取消信号最小接口（完整定义与契约注释见 Controller.hpp——
// 引用形参只须声明可见，不拖入整个取消协议头；§3.3 原文 prepare 签名的
// 第三参数即此类型）。
class ICancelSignal;

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

// =====================================================================
// 模型准备注入（§3.3 原文形态——IExecutionModelService 及其三件承载类型；
// P-EX-3 处置件，EX-T06 落位）
// =====================================================================

/**
 * @brief 模型准备选项（§3.3 prepare 签名第二参数的承载——阶段 A 最小形）。
 *
 * 归置说明（登记单元卡 §15.4）：§3.3 代码段点名本类型但未给字段面——
 *   其真实字段（编译选项/缓存偏好等）是 runtime 注入适配器的装配知识，
 *   阶段 A 无消费者（EX-T08 缓存治理接入时经 ICompileCacheJudge 侧扩展）。
 *   本结构按"最小承载、拒绝预建"落位（NFR-MNT-04：无消费者的接口字段
 *   不预建）；唯一语义字段 preferCompileCache 传达"派发允许消费编译缓存"
 *   的调度侧意愿——判定本身仍归注入侧（execution 不私判，§2.1 N 表）。
 */
struct PrepareOptions {
    /// 派发是否允许消费编译缓存（判定与复用形态归注入侧——execution 只
    /// 表达意愿，不做兼容性裁决；EX-WKR-1 正常派发取缺省 true）。
    bool preferCompileCache = true;
};

/**
 * @brief 派发物（§3.3 原文形态）——物化载荷的字节对 execution 不透明。
 *
 * 背景说明（P-EX-3 处置的核心承载，acceptance 4）：Preparing 段的模型
 *   编译/物化是 runtime 能力（ARCH §3.5 未登记 execution→runtime 边，
 *   SA-10 表外边＝构建失败）——经本结构**值传递**跨越注入边界：适配器
 *   （L5 装配，包装 IRuntimeSnapshotFactory::create＋物化编码）产出字节，
 *   execution 只透传给 worker（DispatchRequest 载荷——ChannelProtocol.hpp）
 *   并把两份内容身份用于登记扩展字段与 worker 握手/身份核对（§6.5"worker
 *   侧身份核对"）；execution 全程不解析字节内容（runtime
 *   MaterializedSnapshotCodec 的解码在 worker 侧评估器装配——同样经注入）。
 *
 * 不可变性：构造后作为 WorkerAssignment.dispatch 携带（派发期事实——
 *   不可变纪律，§4.2 capability 行同源）；字节可能很大（物化快照）——
 *   按值持有、移动传递，不深拷贝（R-5 派发延迟缓解：分帧续传在
 *   ChannelProtocol 层，本结构不感知分片）。
 */
struct MaterializedDispatch {
    /// 物化快照字节（evidence SnapshotCodec(materialized) 产物——身份为
    /// refs-only 形态；字节对 execution 不透明，只透传与核对身份）。
    std::vector<std::uint8_t> snapshotBytes;
    /// 模型派发物字节（runtime MaterializedSnapshotCodec 产出；worker 侧
    /// 由评估器装配消费——execution 只透传，不解析，§3.3 原文注）。
    std::vector<std::uint8_t> modelBytes;
    /// 物化快照的内容身份（供登记扩展字段与 worker 握手核对——§6.4 时序
    /// 图"registerRun(五元组+runDir+扩展)"的扩展字段来源之一）。
    core::ContentIdentity snapshotIdentity;
    /// 模型派发物的内容身份（runtime §9.2 物化核对值的主进程侧副本——
    /// worker 重算摘要与其比对的参照值）。
    core::ContentIdentity modelIdentity;
};

/**
 * @brief 模型准备结果（§3.3 原文形态）——Preparing 段编排的分流依据。
 *
 * 消费语义（§6.4 派发编排）：ok=false→Preparing→Failed（T7——诊断透传
 *   prepare 的 diagnostics）；ok=true→DispatchRequest 携带 dispatch 字节
 *   派发（编译缓存三态仅供调度观测/呈现——复用与否的裁决已由注入侧做
 *   完，execution 不再二判）。
 */
struct ModelPrepareResult {
    bool ok = false;                                     ///< 编译/物化成功
    bool compileCacheFullReuse = false;                  ///< 编译缓存完整命中（经 ICompileCacheJudge——EX-T08 落位）
    bool compileCacheWorkCellOnlyReuse = false;          ///< 仅 WC 层可复用（runtime §9.4 三态）
    MaterializedDispatch dispatch;                       ///< 派发物（ok=true 时有效；ok=false 时为空字节）
    std::vector<core::DiagnosticRecord> diagnostics;     ///< 准备期诊断（透传——判定义务在注入侧）
};

/**
 * @brief 模型准备服务注入口（§3.3 原文名 IExecutionModelService——L5 把
 *        IRuntimeSnapshotFactory::create＋缓存键＋物化编码适配为本接口）。
 *
 * 为什么经注入（P-EX-3 处置，acceptance 4）：任务指令与 ARCH §3.5 白名单
 *   的出入按架构表执行——runtime/policy 不是 execution 的登记依赖边；
 *   本接口把"Preparing 段需要的能力"（模型编译/物化/编译缓存判定）与
 *   "能力的来源"（runtime）解耦。如架构侧将来补登 execution→runtime 边，
 *   适配器可原样退役为直连（接口形状不变，D-17；契约 acceptance 4"如
 *   架构补登边可平滑直连"）。
 *
 * 消费位置：仅调度线程的 Preparing 派发编排（§6.4——EX-T06 supervisor
 *   派发链与 EX-T05 调度器的接缝；阶段 A 契约测试以脚本化替身提供）。
 *
 * 线程约束：仅调度线程调用；实现方无须保证并发安全。
 */
class IExecutionModelService {
public:
    virtual ~IExecutionModelService() = default;

    /**
     * @brief 对一份冻结快照执行模型编译/物化，产出派发物（§3.3 原文签名）。
     *
     * @param snapshot [in] 被评估的冻结快照（evidence builder 产物——非空、
     *                     snapshotId 非保留值；调用方＝派发编排）
     * @param options  [in] 准备选项（编译缓存偏好等——见结构注释）
     * @param cancel   [in] 取消信号（编译是长操作——实现方应在协作点轮询；
     *                     置位后尽快返回 ok=false＋取消诊断，不抛）
     * @return 准备结果（ok=false＝准备失败〔含取消〕——诊断在 result 内
     *         透传；本接口不抛环境类异常，AGENTS §3 错误二分的可预期侧）
     *
     * 线程约束：仅调度线程调用。
     */
    virtual ModelPrepareResult prepare(const evidence::AnalysisSnapshot& snapshot,
                                       const PrepareOptions& options,
                                       const ICancelSignal& cancel) = 0;
};

}  // namespace sdurws::ird::execution

#endif  // SDURWS_IRD_EXECUTION_PORTS_HPP
