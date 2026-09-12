/**
 * @file   CompilerImpl.hpp
 * @brief  十段编译链产品实现（单元内私有头）——CanonicalModelCompiler
 *         （ICanonicalModelCompiler 的唯一产品实现）＋CompileTransaction
 *         （§5.3 编译事务状态机：瞬态产物持有＋逐段推进＋回滚）。
 *
 * 设计依据：
 *   - units/runtime.md §5.1～§5.7（十段链总览/逐步表/事务状态机/临时对象与
 *     资源复查/可重入与取消/正交关系/线程归属）、§3.1（Compiler.hpp 行
 *     "编译链阶段枚举"）、§8.6（资源读取与 S10 前复查——RT-RES-\*）
 *   - 需求 ARC-03（确定性纯变换）、MDL-06（双编译原子性——任一失败不发布
 *     半成品）、TASK-01（协作取消——D-11 取消只经外部请求）、CON-01（CM-0
 *     防混入）、NFR-REL-04（资源变化可检测的 runtime 切面）、UX-03（取消
 *     非错误）
 *   - 任务契约 tasks/foundation/RT-T11.json（产物：Compiler.hpp/.cpp——
 *     十段链/事务/取消/资源复查；acceptance：RT-CPX 全组＋RT-RES-1/2＋
 *     RT-CONT-1/2、D-11 无内部超时、S1～S10 装配完整＋失败回滚、资源摘要
 *     复查）
 *
 * ★ 为什么是私有头（src/ 而非 include/）：§3.1 模块清单把具体编译器类留在
 *   "src/（实现）"——公共面只承诺抽象 ICanonicalModelCompiler（§10.0 冻结
 *   契约），产品类型名不进公共契约（L5 装配经后续任务以工厂函数接入）；
 *   CompileTransaction 是 §5.3 原文命名的编译期内部对象，非公共契约实体。
 *
 * 线程安全：CanonicalModelCompiler 无成员（无共享可变状态——§5.5 可重入表
 * 原文"编译器实例无共享可变状态；同一编译器对象可被多线程并发调用，每次
 * 调用独立 CompileTransaction"）；CompileTransaction 每次调用各建一个实例，
 * 仅编译线程访问（§5.7）。确定性：同输入→同快照身份/同名称（无时钟/locale
 * /环境依赖；观测性字段由快照层承载，§9.1"不入"列）。
 */

#ifndef SDURWS_IRD_RUNTIME_COMPILERIMPL_HPP
#define SDURWS_IRD_RUNTIME_COMPILERIMPL_HPP

#include <optional>
#include <string>
#include <vector>

#include <sdurws/ird/core/DiagData.hpp>
#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/runtime/CanonicalModel.hpp>
#include <sdurws/ird/runtime/Compiler.hpp>
#include <sdurws/ird/runtime/Description.hpp>
#include <sdurws/ird/runtime/Errors.hpp>
#include <sdurws/ird/runtime/NameMap.hpp>
#include <sdurws/ird/runtime/Resource.hpp>
#include <sdurws/ird/runtime/Sources.hpp>

// 单元内 S6/S7 编译器产物与装配尾段（src/ 私有头——R-2 单元内共享）。
#include "WorkCellCompiler.hpp"
#include "DynamicWorkCellCompiler.hpp"

namespace sdurws::ird::runtime {

// =====================================================================
// CompileTransaction——编译事务（§5.3：瞬态产物全部由本对象持有，失败/
// 取消即析构——RAII 逐段清理；状态机按 CompileStage 全态推进）。
// =====================================================================

/**
 * @brief 单次编译调用的事务载体（§5.3"编译器内部 CompileTransaction 对象
 *         持有全部瞬态产物"的 C++ 承载）。
 *
 * 生命周期：每次 compile()/buildCanonicalModel() 调用在栈上各建一个实例
 * （编译器无共享状态——可重入的前提）；函数返回即析构，析构逆序释放全部
 * 瞬态产物（§5.2 各段"临时对象"列：RevisionSummary 投影、Description 值、
 * 资源摘要记录、CanonicalModel、S6 WC＋Frame/Joint 对象、S7 DWC 对象、
 * S8 映射值）——"失败时 S6/S7 的 RobWork 对象全部析构"（MDL-06）由
 * rollback() 的显式 reset＋成员作用域析构共同承载。
 *
 * 状态机（§5.3 原文状态图；stage() 供诊断定位与测试断言）：
 *   Idle → Anchored → Parsed → Validated → ResourcesReady → CanonicalBuilt
 *   → WorkCellCompiled → DynReady → NameMapBuilt → ConsistencyChecked
 *   → Published（唯一成功终态）；
 *   任一工作段失败/取消 → rollback() → RollingBack → RolledBack（幂等——
 *   重复调用 rollback() 不再改变状态；Published 之后 rollback() 拒绝——
 *   §5.3"发布后不再有失败路径"）。
 *
 * 线程约束：**非线程安全**——实例仅所属编译调用的线程访问（TASK-02 线程
 * 纪律；§5.7 编译线程归属）。
 */
class CompileTransaction {
public:
    CompileTransaction() = default;

    /// 析构（瞬态产物 RAII 释放——与 rollback() 等价的最終清理）。
    ~CompileTransaction() = default;
    CompileTransaction(const CompileTransaction&) = delete;            ///< 事务不可拷贝
    CompileTransaction& operator=(const CompileTransaction&) = delete; ///< 事务不可赋值

    /// 当前阶段（§5.3 状态机观测面——失败诊断的段定位与测试断言输入）。
    CompileStage stage() const noexcept { return m_stage; }

    /// 推进到下一工作段（仅编译链内部按 §5.3 图顺序调用——不公开改写语义）。
    void advance(CompileStage next) noexcept { m_stage = next; }

    /**
     * @brief 回滚（§5.3：失败/取消 → RollingBack → RolledBack）。
     *
     * 显式 reset 全部瞬态产物（optional.reset() 触发 WC/DWC 等基线对象
     * 析构——"S6/S7 的 RobWork 对象全部析构"，MDL-06），再落 RolledBack。
     * 幂等：已处于 RollingBack/RolledBack/初始 Idle 时直接置 RolledBack
     * （§5.3"重复进入 RollingBack 幂等"）；Published 后调用为调用方契约
     * 违约（发布后无失败路径）——防御性忽略（状态机不变，理论上不可达）。
     */
    void rollback() noexcept
    {
        if (m_stage == CompileStage::Published) {
            return;  // 发布后无失败路径（§5.3）——防御性忽略，不破坏终态
        }
        m_stage = CompileStage::RollingBack;
        // 逐段析构瞬态产物（声明逆序：映射→DWC→WC→模型→Description→摘要）。
        m_nameMap.reset();
        m_dwcOutcome.reset();
        m_wcOutcome.reset();
        m_model.reset();
        m_description.reset();
        m_consumedResources.clear();
        m_warnings.clear();
        m_builtFrom = core::Digest256{};
        m_stage = CompileStage::RolledBack;
    }

    // ---- 瞬态产物槽位（编译链各段顺序填充；均为"失败即弃"对象）----

    std::optional<RevisionSummary> m_summary;   ///< S1 锚定的修订只读视图
    std::optional<RobotDesignDescription> m_description; ///< S2 解析产物（Description 值）
    core::Digest256 m_builtFrom{};              ///< S2：robot-design 对象字节摘要（header.builtFrom）
    std::vector<core::DiagnosticRecord> m_warnings; ///< S3/S4 警告级诊断（随模型发布）
    /// S4 已消费资源记录（resourceId＋申报摘要＋状态）——S10 前复查的输入
    /// （§5.4：Solidified 免复查；记录仅编译期内存活，绝不跨编译复用——防陈旧）。
    std::vector<ResourceRef> m_consumedResources;
    std::optional<CanonicalModel> m_model;      ///< S5 规范模型
    std::optional<WorkCellCompileOutcome> m_wcOutcome;           ///< S6 产物（WC 引用计数持有）
    std::optional<DynamicWorkCellCompileOutcome> m_dwcOutcome;   ///< S7 产物（nullopt＝未请求）
    std::optional<RuntimeNameMap> m_nameMap;    ///< S8 名称映射

private:
    CompileStage m_stage = CompileStage::Idle; ///< 当前状态机阶段（§5.3）
};

// =====================================================================
// CanonicalModelCompiler——十段编译链产品实现（§10.0 ICanonicalModelCompiler
// 的唯一产品实现；§12 RT-T11 行交付主体）。
// =====================================================================

/**
 * @brief 确定性编译器产品实现（§5 十段链；无状态、可重入）。
 *
 * 两个入口的实现分工（§10.0 原文契约）：
 *   - compile()：整体事务（S1–S10）——MDL-06 原子性的唯一产品入口。内部
 *     建立独立 CompileTransaction，按 §5.3 状态机推进：S1～S5 经共享实现
 *     函数（与本类 buildCanonicalModel 同一路径），S6/S7 调用单元内编译器
 *     （WorkCellCompiler/DynamicWorkCellCompiler），S8 建立名称映射，S9
 *     一致性检查（§6 规则——S6 内置 BaseMount 读回复核，RT-T07 交付面），
 *     **S10 发布前执行 §5.4 资源摘要复查**（Recorded 资源逐项复读比对，
 *     Solidified 免复查——任一不符即 ResourceChanged 整体失败），最后经
 *     SnapshotAssembler::assemble 原子发布（与工厂 create 共用尾段——
 *     单一发布逻辑）。全程 D-11：编译器无内部超时，取消只经外部令牌
 *     （每段边界＋资源逐项循环轮询 ICompileCancelToken）。
 *   - buildCanonicalModel()：分段入口（S1–S5）——"编译链内部使用/测试
 *     替身注入点"（§10.0 原文注释）。工厂 create 经它取得模型后自行编排
 *     S6–S10；本实现与 compile() 共享同一 S1～S5 内部函数（单一语义源）。
 *
 * 错误语义（§3.4 总纲＋§5.2 十段表归属）：
 *   - 调用方契约违约（UnknownObject〔修订不存在〕/ContextReleased〔上下文
 *     已关闭〕）＝fail-fast 异常轨（不走诊断收集；两入口一致——工厂按同轨
 *     重抛）；
 *   - 环境/输入错误＝查询轨：compile() 返回 Failed＋稳定码诊断（全量，
 *     不短路）；buildCanonicalModel 返回 Expected err（RuntimeError 携码与
 *     定位 detail）；
 *   - 取消＝Cancelled 终态（非 error 诊断——UX-03；compile() 专有——分段
 *     入口的取消轮询由编排方在段边界执行）；
 *   - std::bad_alloc→ResourceBudget（§5.5）；其余 std 异常→§8.4 转译
 *     （RobWork 基线异常等）。
 *
 * 版本面：contractVersion()=1（编译器契约版本——进快照身份/缓存键/evidence
 * 复现块；契约面变化＝全体键变化，升版走设计变更评审）；
 * implementationVersion()="ird-runtime-canonical-compiler/1.0"（实现内部
 * 演进标识——"契约或实现变化＝新键"的实现侧分量）。
 *
 * 线程安全：无成员（全部状态在调用栈上的 CompileTransaction）——多线程
 * 可并发调用同一实例（§5.5）；同一 CompileRequest 不可并发重入（调用方
 * 约定，§10.0 属性表）。
 */
class CanonicalModelCompiler final : public ICanonicalModelCompiler {
public:
    CanonicalModelCompiler() = default;

    // ---- ICanonicalModelCompiler（§10.0 契约——语义见类注释与本头说明）----

    CompileOutcome compile(const CompileRequest& request) override;
    Expected<CanonicalModel, RuntimeError>
        buildCanonicalModel(const CompileRequest& request) override;
    std::uint32_t contractVersion() const noexcept override;
    std::string implementationVersion() const noexcept override;

private:
    /**
     * @brief S1–S5 共享实现（compile() 与 buildCanonicalModel() 的单一语义源）。
     *
     * 逐段执行：S1 锚定修订（闭包来源取投影，nullopt→UnknownObject 违约轨）
     * → S2 规范模型解析（objectRefs 全量闭包校验〔CM-0，RT-CONT-1〕＋
     * robot-design 对象定位/字节读取/摘要复核/reader 解析）→ S3 结构与单位
     * 校验（Error 阻断全量收集；Warning 收集进事务随模型发布）→ S4 资源
     * 读取与完整性校验（摘要复核＋Recorded 固化警告＋逐项取消轮询）→
     * S5 CanonicalModel 构造（§4.2 Description→§4.3 Canonical 部件装配＋
     * builder 不变量）。每段入口检查取消令牌并推进事务状态机。
     *
     * @param request [in] 编译请求（注入源＋选项＋取消令牌）
     * @param tx      [in,out] 本次调用的事务（瞬态产物逐段填充）
     * @param pollCancellations [in] 是否在段边界/资源逐项循环轮询取消令牌
     *        （compile()＝true——§5.5 取消密度契约；buildCanonicalModel()＝
     *        false——分段入口的取消检查点归编排方〔工厂〕所有，§10.0）
     * @return 构造完成的不可变规范模型（同时留在 tx.m_model——调用方二选一消费）
     *
     * @throws RuntimeError 调用方契约违约码直接抛出（违约轨）；其余归属码
     *         同样以异常在内部传递、由两入口的 catch 分别转译为诊断/err
     *         （§5.2 各段失败条件——detail 携段号与对象/字段定位）
     */
    CanonicalModel runStagesOneToFive(const CompileRequest& request, CompileTransaction& tx,
                                      bool pollCancellations);
};

}  // namespace sdurws::ird::runtime

#endif  // SDURWS_IRD_RUNTIME_COMPILERIMPL_HPP
