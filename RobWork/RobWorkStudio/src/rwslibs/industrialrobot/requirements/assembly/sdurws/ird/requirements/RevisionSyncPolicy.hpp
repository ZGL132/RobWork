/**
 * @file   RevisionSyncPolicy.hpp
 * @brief  需求会话外部修订同步决策（UI-T35 P2——RevisionCommitted 事件的
 *         三分岔判定纯函数）。
 *
 * 设计依据：
 *   - 任务契约 tasks/foundation/UI-T35.json acceptance 4（外部修订同步：
 *     零未应用编辑＝从新 HEAD 重导线；有未应用编辑＝STALE 提示不丢草稿；
 *     自身应用事件经会话基线比对跳过——gui/UT 具名用例自证面）；
 *   - units/ui.md §13 UI-T35 行（需求域修订同步 sink）；
 *   - R2 中间验收审核 P2（"外部修订事件没有同步需求域会话"——本批立项
 *     指控之四）。
 *
 * 背景说明：项目级撤销/重做/他域命令提交都会向域事件总线 publish
 * RevisionCommitted（core 事件总线是唯一机制——WP-24-T03b 收口形态）。
 * 需求编辑器 loadBaseline 无 rebase 语义（STALE 诚实边界——units/
 * requirements.md §4.6）：会话基线落后于事件修订时，能否安全重导线取决
 * 于草稿是否为空。本文件把该"事件→动作"判定收成一个纯函数：判定输入
 * 全部显式传参（会话基线/事件修订/未应用编辑数），零隐藏状态、零副作用
 * ——宿主事件 sink（UiPlugin RequirementsRevisionSyncSink）按返回值执行
 * 动作，判定与执行分离，具名 UT 逐分支自证。
 *
 * 线程约束：判定本身是纯值运算（任意线程可调）；宿主消费点在 UI 线程
 * （事件 publish 于命令提交线程＝UI 线程，ui.md §16.7 并发边界注——
 * 其他线程来源出现时须经 postToUiThread marshal）。
 * 确定性来源：同输入恒同输出（NFR-COR-01——判定无时间/随机源，无浮点）。
 */

#ifndef IRD_REQUIREMENTS_ASSEMBLY_REVISIONSYNCPOLICY_HPP
#define IRD_REQUIREMENTS_ASSEMBLY_REVISIONSYNCPOLICY_HPP

#include <cstddef>
#include <cstdint>
#include <optional>

#include <sdurws/ird/core/Identity.hpp>  // core::RevisionId（修订身份——§4.1）

namespace sdurws {
namespace ird {
namespace requirements {

/**
 * @brief 外部修订事件对需求会话的应答动作（判定结果——不含执行语义）。
 */
enum class ExternalRevisionSync : std::uint8_t {
    /// 自身应用回执：事件修订＝会话当前基线（noteAppliedRevision 已前移
    /// 锚）——跳过零动作（重导线会造成无谓的面板重刷与就绪重算）。
    SkipSelfApplied,
    /// 安全重导线：零未应用编辑——宿主从新 HEAD 重建基线闭包（会话锚/
    /// 根身份刷新），草稿零丢失（项目级撤销/重做/他域修订的自动跟进）。
    RewireFromHead,
    /// 冻结提示：有未应用编辑——不自动重载（防丢草稿），宿主出状态栏
    /// STALE 提示（建议先应用或撤销草稿；应用时 expectedRevision 失配
    /// 由命令 prepare 诚实拒绝——不虚构成功）。
    StaleNotice,
};

/**
 * @brief 判定外部修订事件应答动作（纯函数——同输入恒同输出）。
 *
 * 判定序（与宿主 UI-T35 落位分支逐分支同构——此处是同一语义的具名化，
 * 宿主侧不再自持分支逻辑）：
 *   ①基线有值且等于事件修订→SkipSelfApplied（自身回执）；
 *   ②未应用编辑数＝0→RewireFromHead（基线 nullopt＝会话未绑定基线的
 *     防御形态，与"零编辑"同路径——重导线经 loadBaseline 重新绑定基线）；
 *   ③其余（有未应用编辑）→StaleNotice。
 *
 * @param sessionBase    [in] 会话当前基线修订（RequirementsPluginAssembly::
 *                       sessionBaseRevision() 直投值；nullopt＝未绑定）
 * @param eventRevision  [in] 事件携带的修订身份（RevisionCommitted 载荷）
 * @param unappliedEdits [in] 编辑器未应用编辑数（draftStatus().edits；≥0）
 *
 * @return 应答动作（各值语义见枚举注释）
 */
inline ExternalRevisionSync planExternalRevisionSync(
    const std::optional<core::RevisionId>& sessionBase,
    const core::RevisionId& eventRevision,
    std::size_t unappliedEdits)
{
    // ①自身回执对账：事件修订与会话基线同值＝自己刚应用完的修订事件
    // 回流。noteAppliedRevision 的锚前移发生在提交回执路径，与总线事件
    // 抵达序无保证——按值比对（而非序号/时序）是与抵达序无关的唯一
    // 可靠判据。
    if (sessionBase.has_value() && *sessionBase == eventRevision) {
        return ExternalRevisionSync::SkipSelfApplied;
    }
    // ②零未应用编辑＝重导线零丢失——从新 HEAD 重建基线是唯一能把会话
    // 拉回权威态的动作（基线 nullopt 同路径：无基线即无可言"落后"）。
    if (unappliedEdits == 0) {
        return ExternalRevisionSync::RewireFromHead;
    }
    // ③有未应用编辑＝草稿基于旧基线——编辑器 loadBaseline 无 rebase，
    // 重载即丢草稿；交宿主出 STALE 提示（诚实边界，不静默吞差异）。
    return ExternalRevisionSync::StaleNotice;
}

}  // namespace requirements
}  // namespace ird
}  // namespace sdurws

#endif  // IRD_REQUIREMENTS_ASSEMBLY_REVISIONSYNCPOLICY_HPP
