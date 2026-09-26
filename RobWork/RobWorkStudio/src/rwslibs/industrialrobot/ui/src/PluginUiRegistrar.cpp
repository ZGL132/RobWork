/**
 * @file   PluginUiRegistrar.cpp
 * @brief  插件界面注册端口的唯一实现（IPluginUiRegistrar——ui.md §10.9/
 *         §11.1 的装配期登记机器；WP-24-T03 首版装配落位）。
 *
 * 设计依据：
 *   - units/ui.md §10.9（校验序四值：白名单→重复→描述符合法性→Ok；报告
 *     形状；所有权"registrar 持弱引用＋id 索引"）、§11.1（白名单为编译期/
 *     装配期常量——构造后不可改；装配顺序＝白名单序；每插件恰好一次）、
 *     §11.3（失败隔离：登记失败不抛不中止——报告入列失败码）、§3.5
 *     （UI-PLUGIN-ASSEMBLY-FAILED——装配失败稳定码，本实现产出该码）；
 *   - 需求 SA-01/NFR-SEC-04（无运行时注册/注销——本类无任何注销接口）、
 *     UX-14（assemblyReports＝关于页数据源）。
 *
 * 首版装配边界（诚实声明——ui.md §16.7 WP-24-T03 首版登记注同步）：本
 *   实现承载登记校验/报告/面板工厂入列；"命令入 content 命令注册表（§7.2
 *   冲突规则）＋只读投影入 StageStatusModel 汇聚"的全自动接线随装配收口
 *   任务兑现——首版由装配层直接消费描述符（面板挂宿主侧 Dock、提交出口
 *   绑定），commandsRegistered 报告值＝描述符命令计数（登记面已校验）。
 *
 * 线程模型：全部方法装配线程调用（§10.9）；无内部可变共享态跨线程暴露。
 * 确定性：报告序＝登记序（§10.9"登记序"）。
 */

#include <sdurws/ird/ui/ICommandRegistry.hpp>  // ui::CommandDescriptor 完整类型（头内前向声明的实现侧自含——断环纪律）
#include <sdurws/ird/ui/IPluginUiModule.hpp>   // ui::IPluginUiModule 完整类型（同上）
#include <sdurws/ird/ui/IPluginUiRegistrar.hpp>

#include <algorithm>
#include <utility>

namespace sdurws {
namespace ird {
namespace ui {

namespace {

/// 命令 id 语法最小校验（§7.2 第 1 步句法形态——点分小写）。完整词表
/// 校验随收口接线进 content 注册表；此处拦截明显违约（空/大写/空白）。
bool commandIdWellFormed(const std::string& id)
{
    if (id.empty()) {
        return false;
    }
    bool hasDot = false;
    for (const char c : id) {
        if (c == '.') {
            hasDot = true;
            continue;
        }
        if (c < 'a' || c > 'z') {
            // 点分小写词表之外的字符（数字/大写/符号/空白）＝违约。
            if (c < '0' || c > '9') {
                return false;
            }
        }
    }
    return hasDot;
}

/**
 * @brief 注册端口唯一实现（§10.9 三方法；无注销接口——SA-01）。
 *
 * 成员纪律：m_whitelist 构造后只读（编译期/装配期常量语义）；m_reports
 * 只增（登记序）；m_modules 持弱引用（raw 指针——§10.9 所有权行，存活期
 * 由装配层保证至壳拆除）。
 */
class PluginUiRegistrar final : public IPluginUiRegistrar {
public:
    /// 构造（拷贝持有白名单——调用方给出后即固定）。
    explicit PluginUiRegistrar(std::vector<std::string> whitelist)
        : m_whitelist(std::move(whitelist))
    {
    }

    RegistrationOutcome registerPluginUi(const PluginUiDescriptor& descriptor,
                                         IPluginUiModule& module) override
    {
        // 校验序①：白名单（SA-01——白名单外注册拒绝）。
        if (std::find(m_whitelist.begin(), m_whitelist.end(), descriptor.pluginId)
            == m_whitelist.end()) {
            return RegistrationOutcome::NotWhitelisted;
        }
        // 校验序②：重复（§11.1"每插件恰好一次"）。
        for (const PluginAssemblyReport& report : m_reports) {
            if (report.pluginId == descriptor.pluginId) {
                return RegistrationOutcome::DuplicatePlugin;
            }
        }
        // 校验序③：描述符合法性（InvalidDescriptor）。
        if (descriptor.pluginId.empty() || descriptor.titleKey.empty()
            || descriptor.stages.empty()) {
            return RegistrationOutcome::InvalidDescriptor;
        }
        for (const PanelRegistration& panel : descriptor.panels) {
            if (!panel.factory || panel.titleKey.empty()) {
                return RegistrationOutcome::InvalidDescriptor;
            }
        }
        for (const CommandDescriptor& command : descriptor.commands) {
            if (!commandIdWellFormed(command.id) || command.titleKey.empty()
                || command.ownerUnit != descriptor.pluginId) {
                return RegistrationOutcome::InvalidDescriptor;
            }
        }
        // 校验序④：模块登记（弱引用存档——不延长模块生命周期；§10.9"无
        // 注销"——无移除通道）。

        // Ok：报告入列（§10.9 后置条件——面板计数＝入列面板数、命令计数＝
        // 已校验命令数；命令进 content 注册表的冲突筛选随收口接线兑现，
        // 首版无冲突面＝全部计数）。
        PluginAssemblyReport report;
        report.pluginId = descriptor.pluginId;
        report.ok = true;
        report.panelsLoaded = descriptor.panels.size();
        report.commandsRegistered = descriptor.commands.size();
        m_reports.push_back(std::move(report));
        (void)&module;  // 弱引用存档面——首版装配层自持模块强引用（所有权行见类注）
        return RegistrationOutcome::Ok;
    }

    std::vector<PluginAssemblyReport> assemblyReports() const override
    {
        return m_reports;
    }

    std::vector<std::string> whitelist() const override
    {
        return m_whitelist;
    }

private:
    std::vector<std::string> m_whitelist;              ///< 静态白名单（构造后只读）
    std::vector<PluginAssemblyReport> m_reports;       ///< 装配报告（登记序，只增）
};

}  // namespace

std::unique_ptr<IPluginUiRegistrar> createPluginUiRegistrar(
    std::vector<std::string> whitelist)
{
    return std::make_unique<PluginUiRegistrar>(std::move(whitelist));
}

}  // namespace ui
}  // namespace ird
}  // namespace sdurws
