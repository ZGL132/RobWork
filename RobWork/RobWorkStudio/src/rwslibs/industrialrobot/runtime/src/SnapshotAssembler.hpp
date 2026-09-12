/**
 * @file   SnapshotAssembler.hpp
 * @brief  快照装配尾段与编译链终态工具（单元内私有头）——SnapshotAssembler
 *         （S8 交叉校验＋身份装配＋S10 发布）＋取消/失败终态构造＋诊断转译。
 *
 * 设计依据：
 *   - units/runtime.md §5.2 S8/S10 行（交叉校验与"唯一对外发布点"）、§5.3
 *     （事务终态：Failed 诊断全量／Cancelled 非 error——UX-03）、§8.4
 *     （RobWork 异常→稳定诊断转译纪律）、§9.6（发布门禁组合表）
 *   - 任务契约 tasks/foundation/RT-T11.json（十段链 compile() 整体事务——
 *     RT-T11 的产品编译器与 RT-T09 的快照工厂共用同一尾段实现）
 *
 * ★ 提升原因（RT-T11，v0.9⑥ scopedFullName 先例同款）：本头原内容全部是
 *   src/Snapshot.cpp 内的定义（类定义＋匿名命名空间函数）。RT-T11 的产品
 *   编译器（src/CompilerImpl.cpp 的 compile() 整体事务）必须与工厂 create
 *   共用 S8+S10 装配尾段与终态构造——两处各写一份即第二套发布逻辑，MDL-06
 *   原子性与 §9.6 门禁语义的维护隐患。故提升为本私有头（R-2：src/ 单元内
 *   共享合法；跨单元仍不可见），工厂与产品编译器同权消费；行为零变化
 *   （RT-T09 既有用例全部回归）。
 *
 * 线程安全：SnapshotAssembler::assemble 为无共享状态的静态纯编排（并发
 * 调用各自局部产物）；终态构造函数为纯值构造。全部实体仅编译线程使用
 * （§5.7——编译允许命令线程或后台线程，同一请求不并发重入）。
 */

#ifndef SDURWS_IRD_RUNTIME_SNAPSHOTASSEMBLER_HPP
#define SDURWS_IRD_RUNTIME_SNAPSHOTASSEMBLER_HPP

#include <chrono>
#include <string>
#include <vector>

#include <sdurws/ird/core/DiagData.hpp>
#include <sdurws/ird/runtime/CanonicalModel.hpp>
#include <sdurws/ird/runtime/Compiler.hpp>
#include <sdurws/ird/runtime/Errors.hpp>
#include <sdurws/ird/runtime/NameMap.hpp>
#include <sdurws/ird/runtime/Snapshot.hpp>  // RuntimeSnapshot（friend 构造）/SnapshotOrigin

// 单元内 S6/S7 编译器产物（完整类型仅在实现文件可达——此处前置声明与
// Snapshot.hpp 同口径；assemble 以 const 引用消费 S6、指针消费 S7）。
struct WorkCellCompileOutcome;
struct DynamicWorkCellCompileOutcome;

namespace sdurws::ird::runtime {

// =====================================================================
// 编译链终态工具（原 Snapshot.cpp 匿名命名空间——工厂与产品编译器共用；
// 定义仍在 src/Snapshot.cpp，本头只提供单元内声明）。
// =====================================================================

/**
 * @brief 段边界取消轮询（§3.3——令牌可空＝不可取消；每段边界检查一次，
 *        §5.5"每段边界与资源逐项循环检查"的边界检查点）。
 * @param cancel [in] 取消令牌（可空）
 * @return true＝已请求取消（编译走 RollingBack→Cancelled——D-11）
 */
bool cancelRequested(const ICompileCancelToken* cancel);

/**
 * @brief 取消终态（§9.6 组合表 Cancelled 行：取消诊断非 error 级——UX-03/
 *        D-11；无快照、可重入重试）。
 * @return CompileOutcome{Cancelled, 恰一条 RT-CANCELLED 非 error 诊断}
 */
CompileOutcome cancelledOutcome();

/**
 * @brief 失败终态（单条稳定码诊断——调用方错误语义：环境错误经稳定码登记）。
 * @param record [in] 已转译的诊断记录（移动入终态）
 * @return CompileOutcome{Failed, 含该诊断；无快照——MDL-06 不发布半成品}
 */
CompileOutcome failedOutcome(core::DiagnosticRecord record);

/**
 * @brief RuntimeError→稳定诊断记录（§8.4 转译纪律的编译链面）。
 *
 * 码面：registryCode 非空→注册码（12 个 1:1 码）；RobWorkError→事件码
 * RT-ROBWORK-ERROR（§10.11 冻结——事件路径）；UnknownObject/ContextReleased
 * 不会到达本函数（调用方契约违约走 fail-fast 异常轨——§3.4 总纲）。
 * cause 携 what()（"runtime/xxx: detail"——token 定位保留于开发诊断）。
 *
 * @param e     [in] 编译链内捕获的 RuntimeError
 * @param stage [in] 失败段定位串（如 "S1-S5"/"S6-S10"——进 context 文案）
 * @return 稳定码诊断记录（码值权威＝diagnostics StableCodeRegistry——PA-1）
 */
core::DiagnosticRecord toDiagnostic(const RuntimeError& e, const char* stage);

/**
 * @brief S8 名称生成期警告（RuntimeNameNotice）→ 警告级诊断的转译。
 *
 * v0.5⑥ 登记的转译落位（RT-T11 交付）：映射生成期的消歧/空名警告
 * （§7.2——数据三元组原名/消歧名/对象由 RuntimeNameNotice 承载）在本函数
 * 转译为 CompileOutcome/快照诊断块的警告级记录。建议码 RT-NAME-DISAMBIGUATED
 * （待 diagnostics StableCodeRegistry 收编——PA-1：本单元不私裁码值，登记面
 * units/runtime.md §15.4 v0.12；收编后仅替换本函数码常量单点）。
 *
 * @param map [in] 名称映射（生成期警告来源；只读）
 * @return 警告级诊断记录（每 notice 一条；无警告＝空集）
 *
 * 线程/确定性：纯函数——同映射同记录序（notices 生成序）。
 */
std::vector<core::DiagnosticRecord> translateNameMapNotices(const RuntimeNameMap& map);

// =====================================================================
// SnapshotAssembler——快照装配尾段（RuntimeSnapshot friend；create/
// materialize/产品 compile() 的 S8＋S10 公共路径）。
// =====================================================================

/**
 * @brief S8＋S10 装配尾段：交叉校验、身份装配、发布门禁、快照构造。
 *
 * 职责（工厂 create/materialize 与 RT-T11 产品编译器 compile() 的汇合点
 * ——三条编排路径的 S6/S7 产物在此合流）：
 *   S8 交叉校验（WC 实际名 ↔ 映射逐一相等）→ 身份块装配（§9.1"入"字段
 *   取值口径）→ DWC 事实状态归类（三成因分支）→ 诊断合并 → S10 原子发布
 *   （RuntimeSnapshot 私有构造——本类是 friend，编排方经 assemble() 调用）。
 */
class SnapshotAssembler {
public:
    SnapshotAssembler() = delete;  ///< 全静态工具类——禁止实例化

    /**
     * @brief 装配并发布快照（RuntimeSnapshot 私有构造的唯一调用方）。
     *
     * @param model            [in] 规范模型（S5 产物或物化重建产物）
     * @param map              [in] 名称映射（create/compile＝buildRuntimeNameMap；worker＝parseNameMap 重建）
     * @param s6               [in] S6 产物（WC 非空——空句柄由编译器 fail-fast）
     * @param dwcOutcome       [in] S7 产物（nullptr＝本次编译未请求 DWC——§9.4 capabilityLevel）
     * @param options          [in] 编译选项（全集——快照记录实际请求）
     * @param compilerContractVersion [in] 编译器契约版本（create/compile＝compiler 申报；worker＝载荷值传递）
     * @param compilerVersion  [in] 编译器实现版本（同上）
     * @param origin           [in] 创建来源（Command/Worker）
     * @param extraWarnings    [in] 编排路径的额外警告级诊断（S8 消歧警告转译等
     *        ——§9.1 快照 diagnostics"模型警告＋编译链警告合并"；默认空＝
     *        既有工厂行为零变化）
     * @return Published 终态（快照唯一出口——§5.2 S10）
     *
     * @throws RuntimeError S8 交叉校验失败（NameConflict——S8 硬失败通道）；
     *         RuntimeSnapshot 构造门禁失败（防御面——理论不可达）
     */
    static CompileOutcome assemble(const CanonicalModel& model,
                                   const RuntimeNameMap& map,
                                   const WorkCellCompileOutcome& s6,
                                   const DynamicWorkCellCompileOutcome* dwcOutcome,
                                   const CompileOptions& options,
                                   std::uint32_t compilerContractVersion,
                                   const std::string& compilerVersion,
                                   SnapshotOrigin origin,
                                   std::vector<core::DiagnosticRecord> extraWarnings
                                   = {});
};  // class SnapshotAssembler（friend——RuntimeSnapshot 私有构造的唯一入口）

}  // namespace sdurws::ird::runtime

#endif  // SDURWS_IRD_RUNTIME_SNAPSHOTASSEMBLER_HPP
