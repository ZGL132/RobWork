/**
 * @file   DomainAssembly.hpp
 * @brief  域插件装配产物与入口（WP-24-T03 首版装配）——宿主插件 initialize
 *         期装配建模插件模块（registrar 登记＋面板工厂消费）的私有装配面。
 *
 * 设计依据：
 *   - units/ui.md §10.9/§11.1（白名单八 token；装配期一次 registerPluginUi；
 *     UiText::resolve 唯一文案出口——UX-02）、§11.3（失败隔离——登记失败
 *     不中止启动，报告留痕）；
 *   - 需求 SA-01/NFR-SEC-04（静态白名单——本装配面只在插件 initialize 期
 *     执行一次，无运行期再注册通道）；O-31（装配器同时看见两边——本 TU
 *     即宿主侧装配器，消费面仅 ui 公共头＋建模装配门面）。
 *
 * 首版范围（ui.md §16.7 WP-24-T03 首版登记注同步）：登记校验＋报告＋面板
 *   挂位（宿主侧 Left Dock）＋UiText 文案解析＋会话种子；命令入 content
 *   注册表／StageStatusModel 汇聚／中央区 CentralAreaHost 挂位／draft.apply
 *   域侧挂钩随装配收口任务兑现。
 *
 * 线程模型：全部函数 UI 线程（initialize 装配线程——§10.9 同口径）。
 */
#ifndef IRD_UI_PLUGIN_DOMAINASSEMBLY_HPP
#define IRD_UI_PLUGIN_DOMAINASSEMBLY_HPP

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <QDockWidget>   // 面板挂位 Dock（宿主侧 chrome——WP-24-T03 首版形态）

#include <sdurws/ird/modeling/ModelingPluginAssembly.hpp>  // 建模装配门面（O-31 装配器消费面——ui→modeling 装配边唯一依赖）

namespace sdurws {
namespace ird {
namespace ui {

class IPluginUiRegistrar;

/// 域插件装配产物（bundle——宿主插件持有，模块存活至壳拆除＝§10.9 前置）。
struct DomainPluginAssembly {
    std::unique_ptr<class IPluginUiRegistrar> registrar;  ///< 注册端口（登记报告查询面）
    modeling::ModelingPluginAssembly modeling;            ///< 建模装配产物（首版唯一域——按值持有）
};

/**
 * @brief 执行域插件首版装配（initialize 装配期恰调一次）。
 *
 * 步骤：创建注册端口（白名单八 token）→创建建模装配产物（门面）→
 * UiText 文案解析绑定→命令提交出口绑定（宿主状态栏反馈）→会话种子→
 * registerPluginUi→报告行输出。
 *
 * @param pluginDock [in] 宿主插件本体 Dock（面板 Dock 的父对象——窗口树托管）
 * @param statusFeedback [in] 命令受理反馈通道（宿主状态栏瞬态消息——首版
 *                        提交出口的可见落点；域执行面随收口任务接线）
 * @param reportLines [out] 装配报告行（宿主日志/状态栏呈现——Dev 通道）
 * @return 装配产物（宿主插件持有；登记失败时 registrar 仍在——报告可查）
 */
std::unique_ptr<DomainPluginAssembly> assembleDomainPlugins(
    QDockWidget& pluginDock,
    std::function<void(const std::string&)> statusFeedback,
    std::vector<std::string>& reportLines);

/**
 * @brief 取建模面板（bundle 内工厂现调——宿主 Dock setWidget 挂位）。
 *
 * @param bundle [in] 装配产物（assembleDomainPlugins 产物）
 * @return 面板 widget（非 owning——调用方 Dock 接管）
 */
QWidget* modelingPanelWidget(const DomainPluginAssembly& bundle);

/// 建模面板 Dock 的标题（宿主 chrome 文案——resolveText 后由调用方设置）。
extern const char* const kModelingDockTitle;

}  // namespace ui
}  // namespace ird
}  // namespace sdurws

#endif  // IRD_UI_PLUGIN_DOMAINASSEMBLY_HPP
