/**
 * @file   IPluginUiRegistrar.hpp
 * @brief  插件界面注册端口（IPluginUiRegistrar）——静态白名单装配的登记
 *         契约（ui.md §10.9 冻结形状的代码落点）。
 *
 * 设计依据：
 *   - units/ui.md §10.9（PanelRegistration/PluginCapabilities/
 *     PluginUiDescriptor/RegistrationOutcome/PluginAssemblyReport/
 *     IPluginUiRegistrar 六形状冻结文本——本头为其逐字承载）、§11.1
 *     （白名单八 token 编译期词表；装配顺序＝白名单序；每插件恰好一次）、
 *     §11.3（装配失败降级——失败隔离不中止启动）；
 *   - 需求 SA-01/NFR-SEC-04（静态白名单、无运行时动态加载入口）、UX-14
 *     （装配报告进关于页）、ARC-02（插件间不互取面板指针）；
 *   - 落位任务：WP-24-T03 首版装配（owner 指示 2026-09-26 提前启动）。
 *     PluginAssemblyReport 自 AboutDialog.hpp 迁移至本头（该头 v1.2 预注
 *     "随装配任务落位时该形状迁移至其自有头"——NFR-MNT-03 单点；迁移为
 *     同形状搬家，AboutDialog.hpp 经包含保持源/二进制兼容）。
 *
 * 背景说明（为什么注册端口 ≠ 命令注册表）：命令的执行分发权威在
 *   ICommandRegistry（§7），而本端口只做**装配期登记**——校验白名单/
 *   重复/描述符合法性、累积 AssemblyReport、持有面板工厂按 stage 入列。
 *   首版装配的登记产物由装配层直接消费（面板挂位/提交出口绑定）；
 *   命令入 content 命令注册表（§7.2 冲突规则）与 StageStatusModel 汇聚
 *   的全自动接线随装配收口任务兑现（首版范围登记见 ui.md §16.7）。
 *
 * 线程模型：registerPluginUi／assemblyReports／whitelist 在装配线程调用
 *   （§10.9 线程行）；PanelRegistration.factory 只在 UI 线程调用。
 */
#ifndef SDURWS_IRD_UI_IPLUGINUIREGISTRAR_HPP
#define SDURWS_IRD_UI_IPLUGINUIREGISTRAR_HPP

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <sdurws/ird/ui/UiTypes.hpp>           // ui::StageId/TextKey（§6.5/§3.5 词表）

class QWidget;  // 前置声明：面板工厂产物（头文件不拖入 Widgets）

namespace sdurws {
namespace ird {
namespace ui {

// 包含面纪律（断环——见下）：CommandDescriptor/IPluginUiModule 以**前向
// 声明**承载，本头不包含 ICommandRegistry.hpp／IPluginUiModule.hpp。原因：
// ICommandRegistry→IWorkbenchShell→UiPorts→AboutDialog 是既有包含链，而
// AboutDialog.hpp 经包含消费本头的 PluginAssemblyReport（NFR-MNT-03 迁移）
// ——若本头再包含 ICommandRegistry，链在"AboutDialog 进行中"的 TU 里回引
// UiPorts(465) 即未定义（C2065）。前向声明的合法性：C++17 允许
// std::vector<T> 以不完整 T 声明成员/形参（本头不实例化其成员）；消费/
// 实现侧（装配层、registrar 实现）需要完整类型时自含 ICommandRegistry.hpp
// ＋IPluginUiModule.hpp（两侧均非 ui 头包含域——ui/src/PluginUiRegistrar.cpp
 // 与宿主插件装配 TU 已自含）。
struct CommandDescriptor;  // 前向声明（§7 命令描述符——vector 成员/形参）
class IPluginUiModule;     // 前向声明（registerPluginUi 引用形参）

// =====================================================================
// §10.9 冻结形状（逐字段对齐卡文——NFR-MNT-03 单一权威）
// =====================================================================

/// 阶段面板登记记录（§10.9 PanelRegistration——创建 QWidget 族面板）。
struct PanelRegistration {
    /// 挂位阶段（StageId——中央区按 stage 装配位；首版装配的宿主侧
    /// Dock 承载见 ui.md §16.7 首版登记注）。
    StageId stage{};
    /// 面板标题文案键（"stage.<id>.title"——§3.5 键约定）。
    TextKey titleKey;
    /// 面板工厂（ui 线程调用；产物归调用方接管——宿主层持有）。
    std::function<QWidget*()> factory;
    /// UX-04 高级面板标记（false＝主面板）。
    bool advanced = false;
};

/// 插件能力声明（§10.9 原文"描述性，非判定性"）。
struct PluginCapabilities {
    bool providesStagePanel = false;         ///< 提供阶段面板
    bool providesReadonlyProjection = false; ///< 参与 StageStatusModel 汇聚
    bool registersCommands = false;          ///< 登记命令
};

/// 插件装配描述符（§10.9 PluginUiDescriptor——registerPluginUi 首参）。
struct PluginUiDescriptor {
    /// 插件注册标识（白名单 token——§11.1 八 token 词表）。
    std::string pluginId;
    /// 插件标题文案键。
    TextKey titleKey;
    /// 覆盖的阶段清单。
    std::vector<StageId> stages;
    /// 能力声明。
    PluginCapabilities capabilities;
    /// 随装配登记的命令描述符（§7——冲突规则随收口接线兑现）。
    std::vector<CommandDescriptor> commands;
    /// 阶段面板登记记录。
    std::vector<PanelRegistration> panels;
};

/// 登记结果（§10.9 RegistrationOutcome 四值词表）。
enum class RegistrationOutcome {
    Ok,                ///< 登记成功
    DuplicatePlugin,   ///< 重复注册（§11.1"每插件恰好一次"）
    NotWhitelisted,    ///< 白名单外注册（SA-01）
    InvalidDescriptor  ///< 描述符非法（pluginId 空／面板工厂空／命令 id 语法）
};

/// 插件装配报告（§10.9 PluginAssemblyReport——UX-14 关于页数据源；
/// 自 AboutDialog.hpp 迁移至本头——NFR-MNT-03 单点，形状零变化）。
struct PluginAssemblyReport {
    /// 插件注册标识（白名单 token——§10.9 原文"白名单 token"）。
    std::string pluginId;
    /// 装配是否成功（false＝§11.3 装配失败——面板降级占位、失败隔离）。
    bool ok = false;
    /// 已装配面板数（PanelRegistration 入列计数——§10.9 后置条件）。
    std::size_t panelsLoaded = 0;
    /// 已注册命令数（冲突拒绝的不计——§7.2 第 2 步）。
    std::size_t commandsRegistered = 0;
    /// 失败诊断码清单（ok==false 时非空；码值经诊断码表冻结）。
    std::vector<std::string> failureDiagnostics;
};

// =====================================================================
// IPluginUiRegistrar——注册端口（§10.9 三方法）
// =====================================================================

/**
 * @brief 插件界面注册端口（装配层在装配期调用；运行期无注册/注销——
 *        SA-01/NFR-SEC-04）。
 */
class IPluginUiRegistrar {
public:
    virtual ~IPluginUiRegistrar() = default;

    /**
     * @brief 登记一个插件界面模块（装配期一次——§10.9 原文）。
     *
     * 校验序（返回值轨，不抛）：白名单（NotWhitelisted）→重复
     * （DuplicatePlugin）→描述符合法性（InvalidDescriptor：pluginId 空、
     * titleKey 空、stages 空、面板工厂空、命令 id 语法）→Ok（报告入列，
     * 描述符按 stage 入列供装配层消费）。
     *
     * @param descriptor [in] 插件装配描述符（拷贝入列——调用方可即弃）
     * @param module     [in] 插件界面模块（非 owning 弱引用＋id 索引——
     *                   存活期由装配层保证至壳拆除）
     * @return 登记结果（§10.9 四值）
     */
    virtual RegistrationOutcome registerPluginUi(const PluginUiDescriptor& descriptor,
                                                 IPluginUiModule& module) = 0;

    /// @brief 装配报告（§10.9——关于对话框数据源 UX-14；登记序）。
    virtual std::vector<PluginAssemblyReport> assemblyReports() const = 0;

    /// @brief 静态白名单（§11.1 八 token 词表的运行时只读载体）。
    virtual std::vector<std::string> whitelist() const = 0;
};

/**
 * @brief 创建注册端口实例（实现类封闭库内——R-2 同 createWorkbenchShell
 *        惯例）。
 *
 * @param whitelist [in] 静态白名单（§11.1 八 token；由装配层固定——本
 *                  工厂拷贝持有，构造后不可改＝编译期/装配期常量语义）
 * @return 注册端口（调用方 unique_ptr 持有）
 */
std::unique_ptr<IPluginUiRegistrar> createPluginUiRegistrar(
    std::vector<std::string> whitelist);

}  // namespace ui
}  // namespace ird
}  // namespace sdurws

#endif  // SDURWS_IRD_UI_IPLUGINUIREGISTRAR_HPP
