/**
 * @file   Compiler.hpp
 * @brief  编译链公共契约面——CompileOptions/CompileRequest/CompileStatus/
 *         CompileOutcome/ICanonicalModelCompiler（§10.0 原文实体）＋
 *         GeometryDetail 枚举。
 *
 * 设计依据：
 *   - units/runtime.md §10.0（公共接口总表——CompileOptions/CompileRequest/
 *     CompileStatus/CompileOutcome/ICanonicalModelCompiler 的完整属性与原文
 *     签名；注释头即"Compiler.hpp / Snapshot.hpp"分跨两头的契约面）、§3.1
 *     （模块清单——Compiler.hpp 行"CompileRequest/CompileOptions/
 *     CompileOutcome/CompileStatus、ICanonicalModelCompiler、编译链阶段枚举"）、
 *     §5.1/§5.2（CompileRequest 输入面与十段表各注入点失败的错误归属）
 *   - 需求 ARC-03（确定性纯变换）、MDL-06（编译原子性——Failed/Cancelled
 *     无快照）、CON-04（部分/失败产物不入缓存）、TASK-01（协作取消）
 *   - 任务契约 tasks/foundation/RT-T09.json（工厂 create/materialize 的
 *     签名依赖本头实体；分阶段登记见 units/runtime.md §15.4 v0.10）
 *
 * 分阶段落位登记（RT-T09，DTB §5.4）：§12 RT-T11 行产物"Compiler.hpp/.cpp
 * （十段链/事务/取消/资源复查）"——本头由 RT-T09 先行建立 §10.0 冻结的
 * **契约面**（IRuntimeSnapshotFactory 的 create 签名按值消费 CompileRequest/
 * CompileOutcome/ICanonicalModelCompiler，无本头则快照工厂无法成文）；十段
 * 链实现（完整 ICanonicalModelCompiler 产品实现＋事务/取消/资源复查）仍归
 * RT-T11 增量落位——届时本头只增不改为契约面，实现独立成文（与 v0.4/v0.8
 * "首个消费者原则"分阶段先例同口径）。
 *
 * 接口属性（§10.0 属性表——ICanonicalModelCompiler）：
 *   - 无状态、可重入（多线程各自请求；"同一请求不可并发重入"由调用方保证）；
 *   - compile()＝整体事务入口（S1–S10）；buildCanonicalModel()＝分段入口
 *     （S1–S5，编译链内部使用／测试替身注入点——§10.0 原文注释；快照
 *     工厂 RT-T09 经此分段入口编排 S6–S10）；
 *   - 确定性（同输入同身份同名称）；只读输入、零磁盘写（命令路径临时文件
 *     归 project 事务——§5.2/§10.0）。
 *
 * 线程安全：全部纯值/无状态接口；CompileOutcome 的快照以 shared_ptr<const>
 * 交接（原子发布语义，§9.2）。
 */

#ifndef SDURWS_IRD_RUNTIME_COMPILER_HPP
#define SDURWS_IRD_RUNTIME_COMPILER_HPP

#include <cstdint>
#include <memory>
#include <vector>

#include <sdurws/ird/core/DiagData.hpp>      // DiagnosticRecord（诊断全量）
#include <sdurws/ird/core/Identity.hpp>      // RevisionId（CompileRequest.revision）
#include <sdurws/ird/runtime/CanonicalModel.hpp>  // CanonicalModel（分段入口产物）
#include <sdurws/ird/runtime/Description.hpp>  // IRobotDesignReader（注入——§3.3）
#include <sdurws/ird/runtime/Errors.hpp>     // Expected/RuntimeError（分段入口两态轨）
#include <sdurws/ird/runtime/Resource.hpp>   // IRuntimeResourceProvider（注入——§3.3）
#include <sdurws/ird/runtime/Sources.hpp>    // IObjectBytesSource/IRevisionClosureSource/
                                             // ICompileCancelToken（注入——§3.3）

namespace sdurws::ird::runtime {

// 前置声明（R-2/只读包装纪律——CompileOutcome 持快照句柄但不触碰其内部；
// shared_ptr 的构造/析构对不完整类型可用——快照完整类型在 Snapshot.hpp）。
class RuntimeSnapshot;

/**
 * @brief 几何细节级别（§10.0 CompileOptions.geometryDetail——编译缓存键成分）。
 *
 * 阶段 A 只有 Full 一档（编译全部几何引用；Visual 恒编译——§10.0
 * includeCollisionGeometry 注释）。细化档位（如包围盒/简化网格）属需求面
 * 扩展：新增枚举值走设计变更评审（枚举数值进入编译缓存键编码域——一经
 * 交付不得改动既有值，稳定第一，与 JointType/NameScope 同纪律）。
 */
enum class GeometryDetail {
    Full, ///< 全细节（R1 唯一档位——视觉/碰撞几何引用全部编译入 WC）
};

// =====================================================================
// CompileOptions——编译选项（§10.0 原文三字段；进入快照身份与缓存键）。
// =====================================================================

/**
 * @brief 编译选项集（§10.0 原文契约——布尔/枚举集）。
 *
 * 身份与缓存键语义（§9.1/§9.4）：全三字段进入 snapshotIdentity 的
 * compileOptions 域；WC 层缓存键只消费"DWC 无关子集"
 * （includeCollisionGeometry＋geometryDetail），requestDynamicWorkCell
 * （＝capabilityLevel，"是否要求 DWC"）只入 DWC 层键（D-04 分层键决策）。
 * 值语义聚合体；operator== 为确定性逐字段比较（缓存键编码的前提——
 * NFR-COR-02）；线程安全。
 */
struct CompileOptions {
    /// 编译碰撞几何入 WC（Visual 恒编译——§10.0 原文注释；资源几何挂接
    /// 随 io 接入任务落位，v0.8⑦ 分阶段登记）。
    bool includeCollisionGeometry = true;
    /// 几何细节级别（缓存键成分——§10.0 原文注释；阶段 A 单档 Full）。
    GeometryDetail geometryDetail = GeometryDetail::Full;
    /// 请求能力级别（§9.4 capabilityLevel——"是否要求 DWC"；false＝本次
    /// 编译不构造 DynamicWorkCell，快照 dynamicWorkCellState 以编译事实为准）。
    bool requestDynamicWorkCell = true;

    /// 精确等值（逐字段——缓存键编码域的前提；确定性纯函数、不抛）。
    bool operator==(const CompileOptions& o) const noexcept
    {
        return includeCollisionGeometry == o.includeCollisionGeometry
            && geometryDetail == o.geometryDetail
            && requestDynamicWorkCell == o.requestDynamicWorkCell;
    }
    bool operator!=(const CompileOptions& o) const noexcept { return !(*this == o); }
};

// =====================================================================
// CompileRequest——编译输入面（§10.0 原文——注入源集合＋选项＋取消令牌）。
// =====================================================================

/**
 * @brief 编译请求（§10.0 原文契约——S1 的全部输入）。
 *
 * 字段语义（与 §3.3 注入边界一致——适配器归 L5 装配期提供，本结构只承载）：
 *   - revision：目标修订（或预解析 RevisionSummary 的 worker 物化路径——
 *     §10.0 原文注释；两路径的取舍归编译器实现 RT-T11）；
 *   - objects/closure：project ②端口的注入适配（可空性见 §10.0 注释——
 *     objects 可空＝已带 RevisionSummary 的物化路径）；
 *   - designReader：modeling 注入（S2 解析；阶段 A 以 ScriptedReader 替身）；
 *   - resources：io 注入（S4 资源读取；阶段 B 接入，编译链对 nullopt 资源
 *     缺失的转译见 §5.2 S4 归属表）；
 *   - cancel：可空＝不可取消（§3.3——D-11 取消语义归 execution）。
 *
 * 所有权：全部注入指针由调用方持有（编译期不接管——§3.3 生命周期约定）；
 * 编译期间调用方必须保证指针有效（§5.1 前置）。
 * 值语义聚合体；线程安全（纯值——注入实现自身的并发安全由实现方保证，
 * §5.5）。
 */
struct CompileRequest {
    /// 目标修订身份（ARC-01——一次命令提交＝一个修订；worker 物化路径可为
    /// 预解析 RevisionSummary 承载，§10.0 原文注释）。
    core::RevisionId revision;
    /// 对象字节只读来源（project ②端口适配；可空＝物化路径——§10.0）。
    const IObjectBytesSource* objects = nullptr;
    /// 修订闭包只读来源（闭包成员判定——CM-0 防混入的判定输入）。
    const IRevisionClosureSource* closure = nullptr;
    /// RobotDesign 解析器（modeling 注入——S2；阶段 A 测试替身）。
    const IRobotDesignReader* designReader = nullptr;
    /// 资源提供者（io 注入——S4；可为 null＝无资源可读（缺失即 ResourceMissing））。
    const IRuntimeResourceProvider* resources = nullptr;
    /// 编译选项（§9.1 compileOptions 的请求侧——快照记录实际使用的选项集）。
    CompileOptions options;
    /// 取消令牌（execution 注入；可空＝不可取消——§3.3/D-11）。
    const ICompileCancelToken* cancel = nullptr;
};

// =====================================================================
// CompileStatus/CompileOutcome——编译结果面（§10.0 原文三态＋全量诊断）。
// =====================================================================

/**
 * @brief 编译终态（§10.0 原文三态）。
 *
 * 与快照发布的原子性绑定（§5.2 S10"唯一对外发布点"）：仅 Published 携带
 * 快照；Failed/Cancelled 恒无快照（不发布半成品——MDL-06；取消不是错误
 * ——UX-03/D-11）。枚举值进入诊断与报告面，一经交付不得改动。
 */
enum class CompileStatus {
    Published, ///< 编译成功且快照已发布（snapshot 非空）
    Failed,    ///< 编译硬失败（诊断含 error 级全量；snapshot 恒空）
    Cancelled, ///< 协作取消（取消诊断非 error 级；snapshot 恒空——可重试）
};

/**
 * @brief 编译结果（§10.0 原文契约——status＋快照句柄＋诊断全量）。
 *
 * 不变量（MDL-06 原子性的结果面投影）：
 *   - status==Published ⟺ snapshot != nullptr（仅 Published 非空——原文）；
 *   - diagnostics 恒全量（不短路——§10.0 原文注释；失败时含 error 级与
 *     既有警告级记录，取消时仅取消级非 error 记录——§9.6 组合表）；
 *   - 快照句柄为 shared_ptr<const RuntimeSnapshot>——调用方与全部下游
 *     消费者共享（§9.2 发布＝shared_ptr 原子交接）。
 *
 * 所有权：snapshot 由本结构与全部持有时方共享（引用计数）；诊断为值集合。
 * 线程安全：纯值（快照本体构造后只读——§9.1）；跨线程交接经 shared_ptr
 * （happens-before 由其保证——§9.2）。
 */
struct CompileOutcome {
    /// 编译终态（见枚举注释——三态原子性）。
    CompileStatus status = CompileStatus::Failed;
    /// 已发布快照（仅 Published 非空——§10.0 原文；shared_ptr<const> 共享）。
    std::shared_ptr<const RuntimeSnapshot> snapshot;
    /// 诊断全量（不短路——§10.0 原文注释；码值权威＝diagnostics，PA-1）。
    std::vector<core::DiagnosticRecord> diagnostics;
};

// =====================================================================
// ICanonicalModelCompiler——十段链实现入口（§10.0 原文抽象；产品实现归
// RT-T11，本头冻结接口契约面）。
// =====================================================================

/**
 * @brief 确定性编译器抽象（§10.0 原文契约——"十段链的实现入口（§5）；
 *         无状态、可重入"）。
 *
 * 两个入口的分工（§10.0 原文注释）：
 *   - compile()：整体事务（S1–S10）——MDL-06 原子性的唯一产品入口；
 *     诊断全量登记、事务回滚、取消轮询、资源复查（§5.3/§5.4）归其实现；
 *   - buildCanonicalModel()：分段入口（S1–S5）——"编译链内部使用/
 *     测试替身注入点；独立于 compile 的整体事务"。快照工厂
 *     （IRuntimeSnapshotFactory::create）经此入口取得 CanonicalModel 后
 *     编排 S6–S10（§10.0 create 行"S1–S10 编排"的工厂侧分担面）。
 *
 * 实现方约束（§10.0 属性表逐项）：
 *   - 无状态、可重入：多线程各自请求（同一 CompileRequest 不可并发重入）；
 *   - 确定性：同输入同身份同名称（ARC-03/NFR-COR-02）；
 *   - 只读输入、零磁盘写（命令路径临时文件归 project 事务——§10.0）；
 *   - buildCanonicalModel 失败返回 Expected err（查询轨不抛——注入失败
 *     的归属转译见 Sources.hpp 文件头表）；未知修订/已释放上下文等调用方
 *     违约码（UnknownObject/ContextReleased）按 §3.4 总纲走 fail-fast 异常轨。
 */
class ICanonicalModelCompiler {
public:
    virtual ~ICanonicalModelCompiler() = default;

    /**
     * @brief 整体事务编译（S1–S10——§5 十段链；产品唯一入口）。
     * @param request [in] 编译请求（注入源＋选项＋取消令牌——见结构注释）
     * @return Published（快照唯一出口）／Failed（诊断全量）／Cancelled
     *
     * @throws 实现可按 §3.4 总纲对调用方契约违约 fail-fast（工程约定——
     *         正常失败面全部经返回值表达，不抛）
     */
    virtual CompileOutcome compile(const CompileRequest& request) = 0;

    /**
     * @brief 分段入口：构造 CanonicalModel（S1–S5——§5.2 前五段）。
     *
     * @param request [in] 编译请求（同 compile）
     * @return ok＝不可变规范模型（不发布快照——§10.0 属性表"返回不可变
     *         模型或诊断；不发布快照"行）；err＝分段失败（RuntimeError 携
     *         §5.2 S1–S5 归属码与定位 detail）
     *
     * @throws RuntimeError 调用方契约违约码（UnknownObject/ContextReleased
     *         ——§3.4 总纲 fail-fast 轨；消费方＝快照工厂按同轨重抛）
     */
    virtual Expected<CanonicalModel, RuntimeError>
        buildCanonicalModel(const CompileRequest& request) = 0;

    /**
     * @brief 编译器契约版本（§9.1 compilerContractVersion——进入快照身份、
     *        缓存键与 evidence 复现块；合法 ≥1。契约变化＝全体键变化）。
     * @return 契约版本号（noexcept 纯查询）
     */
    virtual std::uint32_t contractVersion() const noexcept = 0;

    /**
     * @brief 编译器实现版本（§9.1 compilerVersion 字符串——非空；实现内部
     *        演进标识，"契约或实现变化＝新键"（§9.4）中的实现侧分量）。
     * @return 实现版本串（非空约定由实现方保证）
     */
    virtual std::string implementationVersion() const noexcept = 0;
};

}  // namespace sdurws::ird::runtime

#endif  // SDURWS_IRD_RUNTIME_COMPILER_HPP
