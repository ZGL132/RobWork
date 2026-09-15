/**
 * @file   StoreTypes.hpp
 * @brief  project 存储层公共值类型（增量落位）——锁持有者记录与诊断 sink 契约。
 *
 * 设计依据：
 *   - units/project.md §3.1（组成表：`StoreTypes.hpp`——OpenMode/OpenStoreRequest、
 *     StoreError/StoreErrorCode、RecoveryReport、LockInfo、SchemaInfo、StorePath，
 *     详见 §4、§5.0/§5.1。**本文件按 §12 任务节奏增量落位**：PRJ-T03 只落本
 *     任务卡范围内的类型——§9.2 锁持有者记录与 §5.0 IDiagnosticsSink；其余
 *     类型（StoreError/OpenStoreRequest 等）随 PRJ-T04/T08 落地时增补，不预建
 *     无消费者接口（NFR-MNT-04））；
 *   - §5.0（错误类型、诊断码与 diagnostics 适配——IDiagnosticsSink 定义原文）；
 *   - §9.2（第二实例读取 PID：固定宽度记录，容忍撕裂读）；
 *   - 任务契约 tasks/foundation/PRJ-T03.json acceptance 2/4（PRJ-LOCK-HELD 含
 *     持有 PID；锁诊断经 IDiagnosticsSink 适配器产出 core::DiagnosticRecord
 *     ——P-PR-6 处置：注入式先行、不直链 diagnostics 库）。
 *
 * 背景说明（P-PR-6 处置口径，为什么 sink 在 project 而实现在外部）：
 *   ARCH §3.5 登记 project→diagnostics 边，但 sink 的统一形态与归属归
 *   P-PR-6/P-EX-8 裁决（governance-log 两项均 open）。裁决前的实现形态＝
 *   **注入式先行**：project 自有接口（本文件），diagnostics（或 L5 装配）
 *   提供实现经构造注入；project 不链接 diagnostics 目标、不 include 其任何
 *   头（ird_gates 白名单预登记 project->diagnostics ≠ 要求链接）。因此本
 *   头只依赖 core 公共契约（core::DiagnosticRecord——DiagData.hpp），这是
 *   本单元首个 core 头消费点（此前 PRJ-T01/T02 按 P-PR-1 仅建链接边；本
 *   消费面由任务契约 PRJ-T03.json acceptance 4 明文要求，core.md §4.8 的
 *   DiagnosticRecord 已随 DIAG-T03/T04 在 diagnostics 侧钉住 v0.1 基线）。
 *
 * 线程安全：LockHolderRecord 为纯值类型（可任意复制）；IDiagnosticsSink
 * 实现的线程约束由实现方声明（project 侧调用点：命令/心跳线程与调用方
 * 线程——实现方须按 §9.8 线程模型自行串行化）。
 */

#ifndef SDURWS_IRD_PROJECT_STORETYPES_HPP
#define SDURWS_IRD_PROJECT_STORETYPES_HPP

#include <cstdint>
#include <string>

#include <sdurws/ird/core/DiagData.hpp>

namespace sdurws::ird::project {

/**
 * @brief 写锁持有者记录（`.rwdesign/lock` 文件内容的一行式固定宽度编码，
 *        §4.1 lock 行＋§9.2）。
 *
 * 背景说明：lock 文件由持有进程以固定宽度记录原地重写（§9.4——文件本身
 * 永不删除重建，防锁对象分裂 D-03），内容含持有者 PID、主机名与心跳时间
 * 戳。第二实例在获取写锁被内核拒绝后读取本记录用于"项目被 PID=<n> 持有"
 * 提示（PM-07）；记录只作诊断呈现，**不参与任何权限判定**（SA-17：写权限
 * 唯一依据＝OS 独占句柄；心跳陈旧不得触发接管或失权——ARCH §6.8 D-03）。
 *
 * 撕裂容忍（§9.2）：读取方可能与持有方的心跳原地重写并发——字段可能读到
 * 半截/垃圾内容。解析方必须容忍（缺失字段留空、垃圾数字取 0），不得视为
 * 存储损坏。
 */
struct LockHolderRecord {
    /// 持有进程 ID；单位＝OS PID（Windows 进程标识符，无物理单位）。
    /// 0 表示未知（撕裂读/记录为空）——诊断文案须按"未知"呈现而非显示 PID=0。
    std::uint32_t pid = 0;
    /// 持有者主机名（诊断用途；固定宽度字段内的空白填充由解析方剔除）。
    std::string host;
    /// 心跳时间戳，ISO-8601 UTC 带毫秒（如 2026-09-15T08:30:45.123Z）。
    /// **仅诊断用途**（§9.4：心跳周期 30 s，由 StoreLock 内心跳线程原地重写）；
    /// 不作写权限判据（SA-17/D-03），不作活性推测依据（卡顿不接管）。
    std::string heartbeatUtc;

    bool operator==(const LockHolderRecord& o) const noexcept
    {
        return pid == o.pid && host == o.host && heartbeatUtc == o.heartbeatUtc;
    }
    bool operator!=(const LockHolderRecord& o) const noexcept { return !(*this == o); }
};

/**
 * @brief project 诊断 sink 契约（§5.0 原文形态）——project 定义，
 *        diagnostics（或 L5 装配）提供实现。
 *
 * 背景说明（两级日志的分流规则）：report() 承载**用户级**稳定诊断（码值
 * 已收编 diagnostics StableCodeRegistry——PRJ-* 全量 10 项见 diagnostics.md
 * §4.6，P-PR-6 码值部分已消账）；reportDev() 承载**开发级**诊断（通道名
 * 自由 token，如 "project/lock-recovery"——接管残留报告、心跳写失败等不
 * 面向用户的观察点）。sink 指针在装配注入时可空（§5.1 OpenStoreRequest：
 * "可空：退化为开发诊断"）——可空时 project 侧丢弃诊断产出（装配方失去
 * 观察面是其自身选择，存储行为不受影响）。
 *
 * 生命周期与所有权：实现对象由装配方（L5/测试）持有，注入的裸指针为
 * **非 owning**，其生存期必须覆盖全部消费它的 project 对象。
 *
 * 线程约束：project 会在多线程调用（心跳线程＋调用方线程）——实现方必须
 * 自行保证线程安全（diagnostics 侧 §9.8 承诺单管线串行）。
 */
class IDiagnosticsSink {
public:
    /// 虚析构：经接口指针删除实现对象是多态所有权的常规路径。
    virtual ~IDiagnosticsSink() = default;

    /**
     * @brief 上报用户级稳定诊断记录。
     *
     * @param record [in] 已通过 core::DiagnosticRecord::make() 工厂校验
     *               （C-3：码句法＋必填串非空）的记录；project 侧产出点
     *               使用的码值限于 diagnostics.md §4.6 收编的 PRJ-* 清单
     *               （码值分配权威在 diagnostics——CR-08，project 不私造码）。
     */
    virtual void report(const core::DiagnosticRecord& record) = 0;

    /**
     * @brief 上报开发级诊断（不进入用户可见诊断目录的观察点）。
     *
     * @param channel [in] 通道 token（如 "project/lock-recovery"）；调用方
     *                约定前缀 "project/" 以便 diagnostics 侧分流/脱敏路由。
     * @param message [in] 自由文本（面向开发诊断，可含路径/PID 等明细——
     *                用户级脱敏归 diagnostics 的 NFR-SEC-07 处置）。
     */
    virtual void reportDev(const std::string& channel, const std::string& message) = 0;
};

}  // namespace sdurws::ird::project

#endif  // SDURWS_IRD_PROJECT_STORETYPES_HPP
