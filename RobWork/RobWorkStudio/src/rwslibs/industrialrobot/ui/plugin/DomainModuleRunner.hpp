/**
 * @file   DomainModuleRunner.hpp
 * @brief  域模块应用编排器（DomainModuleRunner）——draft.apply 覆写面的
 *         多模块遍历单元：对全部已登记域模块逐一执行"会话锚同步→域信封
 *         组装→命令提交→回执回写"，消除首版只认 modeling 的硬编码。
 *
 * 设计依据：
 *   - 任务契约 tasks/foundation/UI-T23.json acceptance 2（动态模块覆盖——
 *     "draft.apply 遍历所有已登记 IPluginUiModule，消除 modeling 硬编码；
 *     无草稿模块返回空、有草稿模块返回领域命令，多域组合用例通过"）；
 *   - units/ui.md §11.2（IPluginUiModule::buildDraftCommand——§8.5 应用时
 *     命令组装的域侧唯一入口；moduleId＝域注册键，域外请求 nullopt）、
 *     §8.5（应用与 StaleRevisionRejected——拒绝不销毁草稿、不自动重试）、
 *     §7.7（draft.apply 提交时序——submit 唯一写路径）；
 *   - B1-SPEC §5（渐进迁移——三域插件各自落位 buildDraftCommand：
 *     modeling/requirements 为真实组装，kinematics 无草稿恒 nullopt〔其
 *     模块注释原文"语义实现而非占位"〕——遍历编排对三域一视同仁，域间
 *     零互知（R-1：跨域协作只经 ui 装配面））。
 *
 * 职责边界（为什么遍历单元独立成对，而不是写在 UiPlugin.cpp 里）：
 *   遍历决策（跳过无草稿域／逐域提交／逐域回写）是 ui 装配面的**可测产品
 *   逻辑**——独立成零 Qt 的编排在 ui/plugin/（插件目标私有面），测试目标
 *   可直接把本 TU 编入并注入测试注册模块（契约 acceptance 2"含一个测试
 *   注册模块"的具名用例承载）。本单元零 Q_OBJECT、零 Widget——AUTOMOC
 *   口径不变；域知识（锚语义/回执处理）全部经闭包注入，本单元对三域零
 *   include（R-1/R-2 的结构承载：遍历单元只见 IPluginUiModule 与 project
 *   命令面）。
 *
 * 线程模型：runDomainApply 仅 UI 线程调用（域模块 buildDraftCommand 内含
 *   PanelUiThreadGuard 断言；命令提交〔store.commands().submit〕在开发通道
 *   为 UI 线程同步形态——§7.7 时序）。
 */

#ifndef IRD_UI_PLUGIN_DOMAINMODULERUNNER_HPP
#define IRD_UI_PLUGIN_DOMAINMODULERUNNER_HPP

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <sdurws/ird/core/Identity.hpp>          // core::BranchId/RevisionId（会话锚值面）
#include <sdurws/ird/ui/IPluginUiModule.hpp>     // ui::IPluginUiModule（遍历的统一模块接口）
#include <sdurws/ird/ui/UiProjections.hpp>       // ui::CommandResultProjection（回执投影回写载体）

#include <sdurws/ird/project/CommandService.hpp> // project::CommandEnvelope/CommandResult/ICommandInteraction
                                                 // （提交面值——O-31 装配层特权边，插件目标既有链接）

namespace sdurws {
namespace ird {
namespace ui {

/**
 * @brief 已登记域模块的应用编排条目（装配期一次性填充——遍历单元的域
 *        接缝）。
 *
 * 填充纪律（UI-T23 装配面，DomainAssembly）：每个 registerPluginUi 成功
 * 的域恰好一条；域装配失败的域**不产生条目**（§11.3 失败隔离——失败的域
 * 不参与遍历，也不阻塞其余域）。
 *
 * 闭包可空语义：bindAnchor/onCommitted/onResult 任一为空＝该域无对应会话
 * 语义（如实跳过该步骤，不虚构调用）——例如 kinematics 无会话锚（v1 无
 * 草稿），requirements 草稿源挂接未接续前 onResult 为空（回执投影不回写
 * DraftController，锚前移照常）。
 */
struct DomainModuleEntry {
    /// 模块键（＝descriptor.pluginId，如 "modeling"/"requirements"——同时
    /// 是 buildDraftCommand 的 moduleId 入参与 DraftController 模块句柄）。
    std::string moduleId;
    /// 模块接口（非 owning——存活期由装配产物 DomainPluginAssembly 保证
    /// 覆盖遍历期；§11.2 三方法契约面）。
    IPluginUiModule* module = nullptr;
    /// 会话锚绑定（buildDraftCommand 前调用——信封 branch/baseRevision 取
    /// 自会话态；入参＝权威分支表首条 tip（INV-M3 单默认分支）。空＝无锚
    /// 语义域，跳过）。
    std::function<void(const core::BranchId&, const core::RevisionId&)> bindAnchor;
    /// Committed 回执的域处理（锚前移/根身份回填等域特定语义——入参＝
    /// 对端命令结果原值。空＝无回执处理域）。
    std::function<void(const project::CommandResult&)> onCommitted;
    /// 回执投影回写（DraftController onCommandResult——脏标记清零/撤销栈
    /// 清理/Stale 冲突暂存。空＝无草稿源挂接〔保存清单不含本模块〕，如实
    /// 跳过——诚实边界见 DomainAssembly 登记注）。
    std::function<void(const CommandResultProjection&)> onResult;
};

/**
 * @brief 单个域模块的应用结果（遍历报告的行值——只读值语义）。
 */
struct DomainApplyEntryReport {
    /// 单模块结局两值：NoDraft＝buildDraftCommand 返回 nullopt（无草稿可
    /// 应用——§8.5 合法形态，零提交）；Submitted＝已组装信封并提交。
    enum class Outcome : std::uint8_t {
        NoDraft,    ///< 无草稿可应用（跳过提交——不产生空修订）
        Submitted,  ///< 信封已提交（committed/rejection 见后续字段）
    };
    std::string moduleId;       ///< 模块键（与条目一致）
    Outcome outcome = Outcome::NoDraft;
    bool committed = false;     ///< 仅 Submitted 时有意义——对端 Committed
    std::string revision;       ///< 新修订 canonical 文本（committed 时非空）
    std::string rejectionReason;///< 拒绝/中止/失败原因 token（未 committed 时）
};

/**
 * @brief 一次 draft.apply 遍历的整体报告（调用方呈现/留痕的值源）。
 */
struct DomainApplyReport {
    /// 逐模块结果（遍历序＝条目登记序——NFR-COR-02 稳定序）。
    std::vector<DomainApplyEntryReport> entries;

    /// @brief 无草稿模块数（"无可应用变更"的诚实计数——不虚构提交）。
    std::size_t noDraftCount() const;
    /// @brief 已提交（Committed）模块数。
    std::size_t committedCount() const;
    /// @brief 已组装并提交（无论成败）模块数＝entries 数−NoDraft 数。
    std::size_t submittedCount() const;
    /// @brief 是否存在任何提交（状态行汇总文案的判定位）。
    bool anyCommitted() const noexcept;
};

/**
 * @brief 信封提交函数类型（对端命令网关的接缝——装配层注入
 *        store.commands().submit；独立成类型便于测试注入替身提交面）。
 *
 * 第二参为确认交互适配器（可空＝非交互提交——Confirmable 集存在时按
 * Rejected(confirmations-unresolved) 呈现，不虚构放行；与首版交互边界
 * 同口径，UiPlugin 传入 HostCommandInteraction 实例）。
 */
using DomainCommandSubmitFn = std::function<project::CommandResult(
    project::CommandEnvelope envelope, project::ICommandInteraction* interaction)>;

/**
 * @brief 执行 draft.apply 的多模块遍历（acceptance 2 的编排本体）。
 *
 * 编排次序（逐条目，任一条目的失败不影响其余条目——域间失败隔离）：
 *   ①锚同步：anchor 非空且条目 bindAnchor 非空 → 先绑定（信封组装读取
 *     的会话权威态在此之后——T03b-2c"组装前必须锚定"纪律的遍历推广）；
 *   ②域信封组装：module->buildDraftCommand(moduleId)——nullopt＝无草稿
 *     可应用，记 NoDraft 行跳过提交（不产生空修订，§8.5）；
 *   ③提交：submit(*envelope, interaction)——对端权威校验（硬断言/策略
 *     确认/Stale/只读）全在对端，本编排零判定（§7.5 界面使能态≠业务判定）；
 *   ④回执：onResult 非空 → 组装 CommandResultProjection 回写（四态映射
 *     与首版 orchestrateApplyDraft 逐字同源——Committed/Rejected〔七种
 *     rejection token〕/Aborted〔canceled|interaction-lost〕/Failed）；
 *     onCommitted 非空且 Committed → 域回执处理（锚前移/根回填）。
 *
 * @param entries     [in] 已登记域模块条目（登记序即遍历序）
 * @param anchor      [in] 会话锚（权威分支表首条 branch/tip；nullopt＝
 *                    无分支表〔不可达——有会话必有分支表〕，全部条目跳过
 *                    锚同步如实组装）
 * @param submit      [in] 信封提交函数（非空——空函数属装配缺陷，遍历
 *                    直接返回空报告并经 devLog 留痕，不抛）
 * @param interaction [in] 确认交互适配器（可空＝非交互提交——见类型注）
 * @param devLog      [in] Dev 留痕通道（可空＝静默；逐模块结局一行留痕，
 *                    通道 token 归调用方——本编排只交消息文本）
 * @return 遍历报告（entries 数＝条目数，每域恰一行）
 *
 * @note UI 线程调用（§3.4——域模块断言与命令提交线程纪律，见文件头）。
 */
DomainApplyReport runDomainApply(
    const std::vector<DomainModuleEntry>& entries,
    const std::optional<std::pair<core::BranchId, core::RevisionId>>& anchor,
    const DomainCommandSubmitFn& submit,
    project::ICommandInteraction* interaction,
    const std::function<void(const std::string&)>& devLog = {});

}  // namespace ui
}  // namespace ird
}  // namespace sdurws

#endif  // IRD_UI_PLUGIN_DOMAINMODULERUNNER_HPP
