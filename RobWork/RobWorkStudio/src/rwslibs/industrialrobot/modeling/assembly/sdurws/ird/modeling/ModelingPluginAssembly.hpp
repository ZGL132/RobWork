/**
 * @file   ModelingPluginAssembly.hpp
 * @brief  建模插件装配门面——首版装配（WP-24-T03）中宿主装配层消费建模
 *         插件的**唯一公共入口**。
 *
 * 设计依据：
 *   - units/ui.md §10.9/§11.1（registerPluginUi 入参形状；白名单 token；
 *     面板工厂语义）、§11.2（IPluginUiModule 三方法）；
 *   - units/modeling.md §9.7.3（命令目录/装配描述符——PanelCommandCatalog
 *     的建模侧登记数据经本门面直通装配层）、§14.6 R-MDL-1（P-MDL-8 消账
 *     路径：ui 侧头落位→继承切换→装配接线）；
 *   - O-31 裁决（装配器同时看见两边写单行适配器）——本头即建模侧给装配
 *     器的"单行适配器"面：装配层只见本头与 ui 公共头，零建模私有头依赖
 *     （R-2；plugin/ 私有实现细节全部封装在本门面之后）。
 *
 * 落位形态说明：本头位于 modeling 单元的 `assembly/` 目录（plugin 目标的
 * PUBLIC include 面——插件界面目标的装配契约头，非产品 include/ 扫描域；
 * 与 plugin/ 同理在零 Qt 红线的文件域之外）。实现（plugin/
 * ModelingPluginAssembly.cpp）编入 `sdurws_ird_modeling_plugin` 目标。
 *
 * 线程模型：本门面全部函数 UI 线程调用（模块/面板同约束——§3.4）。
 */
#ifndef IRD_MODELING_ASSEMBLY_MODELINGPLUGINASSEMBLY_HPP
#define IRD_MODELING_ASSEMBLY_MODELINGPLUGINASSEMBLY_HPP

#include <functional>
#include <memory>
#include <optional>
#include <string>

#include <QString>                                 // 文案解析返回值（bindTextResolver）
#include <sdurws/ird/core/Identity.hpp>            // core::BranchId/RevisionId/ObjectId（会话锚/回执值面）
#include <sdurws/ird/ui/IPluginUiRegistrar.hpp>    // ui::PluginUiDescriptor（§10.9 装配描述符）
#include <sdurws/ird/ui/UiTypes.hpp>               // ui::CommandId（命令提交出口入参）

namespace sdurws {
namespace ird {
namespace ui {
class IModuleDraftSource;  // 前向声明（§10.5 模块草稿源——draftSource 返回类型）
}  // namespace ui
}  // namespace ird
}  // namespace sdurws

namespace sdurws {
namespace ird {
namespace modeling {

class ModelingUiModule;  // 前置声明（内部具体类型——消费方面只见 IPluginUiModule 接口）

/// 模块句柄（ModuleDraftHandle 词表——与 descriptor.pluginId 同 token；
/// 宿主装配层 attachModule 首参取此值）。
inline constexpr char kModuleHandle[] = "modeling";

/**
 * @brief 建模插件装配产物（createModelingPluginAssembly 的返回值）。
 *
 * 消费序（宿主装配层的标准用法）：
 *   ①`bindTextResolver`（接 ui::resolveText——命令按钮呈现工程用语）；
 *   ②`bindCommandSubmit`（接宿主提交面——首版为状态行回显，收口接
 *     CommandRegistry）；
 *   ③`seedTemplateSession`（首版会话种子——generic-6r 真实草稿）；
 *   ④`registrar.registerPluginUi(descriptor, *module)`；
 *   ⑤`descriptor.panels.front().factory()` 取面板挂位。
 *
 * 所有权：module 由本结构 unique_ptr 持有；内部实现指针非 owning（指向
 * module 的具体对象——随 module 生存，消费方不解引用）。可移动（装配层
 * 转移持有）；不可拷贝。
 */
struct ModelingPluginAssembly {
    std::unique_ptr<ui::IPluginUiModule> module;  ///< 模块（接口面——三方法契约）
    ui::PluginUiDescriptor descriptor;            ///< 装配描述符（§10.9 形状直通）

    /// 命令提交出口绑定（转发模块内部——面板创建前后皆可）。
    void bindCommandSubmit(std::function<void(const ui::CommandId&)> submit);

    /// 文案解析绑定（titleKey→工程用语；转发模块内部）。
    void bindTextResolver(std::function<QString(const std::string& titleKey)> resolve);

    /// 首版会话种子（generic-6r 真实草稿；转发模块内部）。
    void seedTemplateSession();

    /// 会话锚绑定（打开成功后——分支＋tip；转发模块内部，T03b-2）。
    void bindSessionAnchor(const sdurws::ird::core::BranchId& branch,
                           const sdurws::ird::core::RevisionId& base);

    /// 应用回执回写（submit Committed 后——基线前移＋根身份回填；T03b-2）。
    void noteAppliedRevision(const sdurws::ird::core::RevisionId& newBase,
                             const std::optional<sdurws::ird::core::ObjectId>& rootObjectId);

    /// 模块草稿源视图（§10.5 attachModule 第二参数——本对象实现四方法；
    /// 存活期随 module）。const 语义：源接口四方法中两 const 两非 const，
    /// 引用可变性随对象本身，不受本访问器限定。
    ui::IModuleDraftSource& modelingDraftSource() const;

    ModelingPluginAssembly();
    ~ModelingPluginAssembly();
    ModelingPluginAssembly(ModelingPluginAssembly&&) noexcept;
    ModelingPluginAssembly& operator=(ModelingPluginAssembly&&) noexcept;
    ModelingPluginAssembly(const ModelingPluginAssembly&) = delete;
    ModelingPluginAssembly& operator=(const ModelingPluginAssembly&) = delete;

private:
    class ModelingUiModule* m_impl = nullptr;  ///< 具体模块（非 owning——module 持有）

    friend ModelingPluginAssembly createModelingPluginAssembly();  ///< 唯一装配点（实现侧回填 m_impl）
};

/**
 * @brief 创建建模插件装配产物（每次调用全新实例——descriptor 由
 *        modelingPanelRegistration/modelingDomainCommands 现产，无缓存）。
 *
 * @return 装配产物（UI 线程构造——模块会话态绑定构造线程）
 */
ModelingPluginAssembly createModelingPluginAssembly();

}  // namespace modeling
}  // namespace ird
}  // namespace sdurws

#endif  // IRD_MODELING_ASSEMBLY_MODELINGPLUGINASSEMBLY_HPP
