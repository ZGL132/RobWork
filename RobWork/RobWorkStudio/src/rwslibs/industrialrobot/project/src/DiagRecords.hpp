/**
 * @file   DiagRecords.hpp
 * @brief  project 单元用户级诊断记录的共享工厂（PRJ-\* 稳定码→
 *         core::DiagnosticRecord 的装配点；私有实现头，不出 include/——
 *         R-2 纪律）。
 *
 * 设计依据：
 *   - units/project.md §5.0（错误类型、诊断码与 diagnostics 适配——
 *     IDiagnosticsSink 产出的记录形态；码值分配权威＝diagnostics
 *     StableCodeRegistry，PRJ-\* 全量 10 项已收编 diagnostics.md §4.6
 *     ——P-PR-6 码值部分消账）、§6.2（stale-revision 附
 *     PRJ-STALE-REVISION-REJECTED——§6.3 表"过期修订"行）；
 *   - 需求 PM-07（只读打开提示数据）、PM-08（恢复诊断）、NFR-REL-05
 *     （用户可见文案不在 project 生成的边界澄清：本工厂只装配**数据面**
 *     ——context/cause/recommendedAction 三串是诊断记录的必填字段
 *     （core C-3），其呈现文案与脱敏归 diagnostics/ui（P-PR-6 链路）；
 *     project 侧文案保持最小、面向开发定位）。
 *
 * 背景说明（为什么集中于此）：写拒绝/过期基线类稳定诊断的产出点分散在
 *   多个端口实现（ProjectStoreImpl 门卫、CommandServiceImpl S1/S2、后续
 *   PRJ-T12/T14 写入口）——同一码值的记录装配若各写一份必然漂移（文案
 *   分叉、字段不一致）。本头是**唯一装配点**：码值字符串逐字取自
 *   diagnostics.md §4.6 收编清单（不私造码——CR-08），文案单处维护。
 *
 * 线程安全：全部为纯函数（每次调用构造独立记录——无共享状态）。
 */

#ifndef SDURWS_IRD_PROJECT_SRC_DIAGRECORDS_HPP
#define SDURWS_IRD_PROJECT_SRC_DIAGRECORDS_HPP

#include <optional>
#include <string>

#include <sdurws/ird/core/DiagData.hpp>

namespace sdurws::ird::project::diagrec {

/**
 * @brief PRJ-WRITE-AUTHORITY-LOST 记录（上下文态写拒绝——§5.1 生命周期
 *        图"迟到写请求=拒绝+诊断"；锁面拒绝的诊断由 StoreLock 产出）。
 *
 * @param detail [in] 开发定位明细（状态/分支/路径键值——进 cause 字段）
 */
inline core::DiagnosticRecord makeWriteAuthorityLost(const std::string& detail)
{
    return core::DiagnosticRecord::make(
        std::string{"PRJ-WRITE-AUTHORITY-LOST"},
        std::nullopt,
        std::nullopt, std::nullopt,
        "项目存储上下文已不接受写入（关闭中或已关闭/写权限已丢失）",
        detail,
        "如需继续编辑，请重新打开该项目；迟到的保存/提交/归档请求"
        "已被拒绝，数据未受影响");
}

/**
 * @brief PRJ-LOCK-HELD 记录（只读上下文写拒绝＋降级打开场景——含持有
 *        PID 时进 detail）。
 *
 * @param detail [in] 开发定位明细（持有者 PID 等——进 cause 字段）
 */
inline core::DiagnosticRecord makeLockHeld(const std::string& detail)
{
    return core::DiagnosticRecord::make(
        std::string{"PRJ-LOCK-HELD"},
        std::nullopt,
        std::nullopt, std::nullopt,
        "项目当前以只读方式打开（写权限由其他实例持有或介质只读）",
        detail,
        "关闭占用该项目的其他窗口/实例后重试；只读模式下可以查看"
        "但不能编辑、提交或保存草稿");
}

/**
 * @brief PRJ-STALE-REVISION-REJECTED 记录（过期基线提交拒绝——§6.2/
 *        §6.3 表"过期修订"行；PM-04 草稿基线冲突与 PM-18 撤销基线
 *        过期同码）。
 *
 * 背景（§6.2"附当前 tip 与差异定位数据"）：expected/actual 两个修订
 *   身份与分支身份进 cause——分支级定位；对象级差异（哪个对象前进）可
 *   由消费方经②查询端口比对两个修订的引用集得出（端口返回全量引用集，
 *   无需本记录重复携带——§6.8"修订引用集（全量）"）。
 *
 * @param detail [in] 差异定位数据（branch=<brn> expected=<rev>
 *                actual-tip=<rev> 键值形态——进 cause 字段）
 */
inline core::DiagnosticRecord makeStaleRevision(const std::string& detail)
{
    return core::DiagnosticRecord::make(
        std::string{"PRJ-STALE-REVISION-REJECTED"},
        std::nullopt,
        std::nullopt, std::nullopt,
        "提交被拒绝：命令基线落后于分支当前版本（expectedRevision 与"
        "分支 tip 不一致——§6.2 并发校验）",
        detail,
        "基于当前版本重新调整输入后重试；未应用的草稿已完整保留"
        "（origin=apply-retained 语义——PM-04），可对照新旧修订引用"
        "重新合并");
}

}  // namespace sdurws::ird::project::diagrec

#endif  // SDURWS_IRD_PROJECT_SRC_DIAGRECORDS_HPP
