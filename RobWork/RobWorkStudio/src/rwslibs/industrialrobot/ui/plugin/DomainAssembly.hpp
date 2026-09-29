/**
 * @file   DomainAssembly.hpp
 * @brief  域插件装配产物与入口（UI-T23 三域集成收口形态）——宿主插件
 *         initialize 期装配三域插件模块（modeling/requirements/kinematics
 *         三门面）＋域模块登记表（draft.apply 遍历输入）＋单域装配失败
 *         隔离状态的私有装配面。
 *
 * 设计依据：
 *   - units/ui.md §10.9/§11.1（白名单八 token；装配期一次 registerPluginUi；
 *     UiText::resolve 唯一文案出口——UX-02）、§11.3（失败隔离——单域装配
 *     失败不中止启动，稳定诊断＋占位，其余域照常——acceptance 3 的装配
 *     面承载）；
 *   - 任务契约 tasks/foundation/UI-T23.json acceptance 2/3（draft.apply
 *     遍历已登记模块〔DomainModuleEntry 登记表〕；单域装配失败隔离）、
 *     acceptance 1（三域经三门面消费——modeling/kinematics/requirements
 *     公共装配门面，O-45 裁决补建的 requirements 缝）；
 *   - B1-SPEC §5.1（迁移三接入面的宿主消费——sharedSurfaceProviders 出
 *     线注册进共享树/检查器）、§6（链尾收口）；
 *   - 需求 SA-01/NFR-SEC-04（静态白名单——本装配面只在插件 initialize 期
 *     执行一次，无运行期再注册通道）；O-31（装配器同时看见两边——本 TU
 *     即宿主侧装配器，消费面仅 ui 公共头＋三域装配门面，零域私有头
 *     ——R-2）；
 *   - 先例：WP-24-T03 首版装配（modeling 单域）——本文件为其三域推广，
 *     modeling 的既有消费形态（值成员/面板工厂/草稿源）保持不变（零回归）。
 *
 * 诚实边界（登记 ui.md §13 UI-T23 落位注）：
 *   - requirements 的编辑器会话数据面（attachEditor＋loadBaseline 闭包
 *     适配）随需求域宿主会话任务接续——本装配面 attachEditor(nullptr)
 *     （显式无会话＝门面合法二态：树供给空集、buildDraftCommand 如实
 *     nullopt）；共享面 Provider 照常注册（无会话二态由域侧如实表达）；
 *   - kinematics 服务缝（KinPanelServices——域 plugin 私有类型，R-2 禁
 *     跨单元消费）在宿主形态不注入：面板降级语义如实呈现；会话姿态载体
 *     （KinSessionPose）注入缝随该类型公共化增量任务（D8 域侧承接
 *     applyHostJointState 已就位，宿主 Jog 桥经门面公共方法接线——写入
 *     时载体未注入由域侧返回 false 诚实降级＋Dev 留痕，不虚构写入）。
 *
 * 线程模型：全部函数 UI 线程（initialize 装配线程——§10.9 同口径）。
 */
#ifndef IRD_UI_PLUGIN_DOMAINASSEMBLY_HPP
#define IRD_UI_PLUGIN_DOMAINASSEMBLY_HPP

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <QDockWidget>   // 面板挂位 Dock（宿主侧 chrome——WP-24-T03 首版形态）

#include <sdurws/ird/modeling/ModelingPluginAssembly.hpp>      // 建模装配门面（O-31 装配器消费面）
#include <sdurws/ird/requirements/RequirementsPluginAssembly.hpp>  // 需求装配门面（O-45 补建缝——UI-T23 消费）
#include <sdurws/ird/kinematics/KinematicsPluginAssembly.hpp>  // 运动学装配门面（WP-15-T18 出线——UI-T23 消费）

#include "DomainModuleRunner.hpp"                // DomainModuleEntry（draft.apply 遍历条目——acceptance 2）

namespace sdurws {
namespace ird {
namespace ui {

class IPluginUiRegistrar;
class IModuleDraftSource;   // 前向声明（modelingDraftSource 返回类型——§10.5）
class IUiAboutDataSource;   // 前向声明（bundleAboutSource 返回类型——§11.4）

/**
 * @brief 域插件装配产物（bundle——宿主插件持有，模块存活至壳拆除＝§10.9 前置）。
 *
 * 域成员形态（UI-T23 三域推广）：
 *   - modeling 保持**值成员**（首版语义——建模装配失败＝宿主装配失败
 *     fail-fast，维持 WP-24-T03 既有行为零回归；UiPlugin 既有调用点
 *     m_domains->modeling 不变）；
 *   - requirements/kinematics 为 **optional**（§11.3 多域失败隔离：单域
 *     create/register 异常被捕获登记为失败状态，缺席不中止其余域——
 *     acceptance 3 的注入面；成功时值语义同 modeling）。
 */
struct DomainPluginAssembly {
    /// 析构（cpp 定义——aboutSource 的删除器实例化需要 IUiAboutDataSource
    /// 完整类型，out-of-line 让本头保持前置声明即可包含）。
    ~DomainPluginAssembly();
    std::unique_ptr<class IPluginUiRegistrar> registrar;  ///< 注册端口（登记报告查询面）
    modeling::ModelingPluginAssembly modeling;            ///< 建模装配产物（必成域——首版语义保持）
    /// 需求装配产物（失败隔离缺席＝nullopt——§11.3 多域形态，acceptance 3）。
    std::optional<requirements::RequirementsPluginAssembly> requirements;
    /// 运动学装配产物（失败隔离缺席＝nullopt——同上）。
    std::optional<kinematics::KinematicsPluginAssembly> kinematics;
    std::unique_ptr<class IUiAboutDataSource> aboutSource;  ///< 关于框数据源（惰性构造——bundleAboutSource）

    /**
     * @brief 单域装配状态（acceptance 3——失败隔离的状态面：装配层据此
     *        在共享树呈现占位＋出稳定诊断；测试具名用例的状态断言源）。
     */
    struct DomainAssemblyStatus {
        std::string domainKey;  ///< 域键（＝白名单 token，如 "requirements"）
        bool ok = false;        ///< 装配＋登记是否成功
        std::string detail;     ///< 失败原因（异常 what——Dev 留痕/占位文案素材）
    };

    /// 三域装配状态（登记序＝白名单序——modeling/requirements/kinematics
    /// 恰三条；测试断言与占位呈现的输入）。
    std::vector<DomainAssemblyStatus> statuses;

    /// 已登记域模块条目（draft.apply 遍历输入——acceptance 2；仅登记成功
    /// 的域入表，登记序＝白名单序。条目的域闭包由装配序填充：modeling 含
    /// 锚绑定/回执回写/锚前移全套，requirements 含锚绑定/锚前移〔草稿源
    /// 挂接未接续——onResult 空，见文件头诚实边界〕，kinematics 全空闭包
    /// 〔无草稿域——buildDraftCommand 恒 nullopt 的接口面消费〕）。
    std::vector<DomainModuleEntry> applyEntries;

    /// @brief 按域键查装配状态（未登记域键＝nullopt——调用方防御）。
    const DomainAssemblyStatus* statusOf(const std::string& domainKey) const;
};

/**
 * @brief 执行域插件装配（UI-T23 三域形态——initialize 装配期恰调一次）。
 *
 * 步骤（白名单序；单域失败隔离——§11.3）：
 *   ①创建注册端口（白名单八 token）；
 *   ②建模装配（必成域——失败上抛＝宿主装配失败，首版语义保持）：
 *     门面→UiText 文案绑定→会话种子→registerPluginUi→登记表/状态；
 *   ③需求装配（try/catch——失败登记状态不中止）：门面→attachEditor
 *     (nullptr)（显式无会话——诚实边界）→registerPluginUi→登记表/状态；
 *   ④运动学装配（try/catch——同上）：门面→UiText 文案绑定→
 *     registerPluginUi→登记表/状态（服务缝不注入——诚实边界见文件头）；
 *   ⑤报告行输出（三域逐一：ok/panels/commands；失败域附 UI-PLUGIN-
 *     ASSEMBLY-FAILED 状态行——稳定诊断的装配面出线，诊断目录通道由
 *     宿主接——reportLines 交 UiPlugin 双通道出线）。
 *
 * @param pluginDock [in] 宿主插件本体 Dock（面板 Dock 的父对象——窗口树托管；现由面板工厂闭包自持，保留参数兼容首版签名）
 * @param reportLines [out] 装配报告行（宿主日志/状态栏呈现——Dev 通道）
 * @return 装配产物（宿主插件持有；登记失败时 registrar 仍在——报告可查）
 */
std::unique_ptr<DomainPluginAssembly> assembleDomainPlugins(
    QDockWidget& pluginDock,
    std::vector<std::string>& reportLines);

/**
 * @brief 取关于框数据源（§11.4 IUiAboutDataSource 的装配报告半区实装——
 *        registrar.assemblyReports() 现取现拼〔ACC5 零缓存〕，关于框每次
 *        打开现取；versionBaseline 半区恒 available=false〔WP-24-T01 基线
 *        已落盘但呈现值源未接线——版本区保持"未装载"占位，不虚构〕）。
 *
 * @param bundle [in,out] 装配产物（适配器惰性构造入 bundle——宿主持有至
 *                壳拆除，存活期覆盖返回指针的使用期）
 * @return 数据源指针（非 owning——bundle 内部适配器，随 bundle 存活）
 */
ui::IUiAboutDataSource* bundleAboutSource(DomainPluginAssembly& bundle);

/**
 * @brief 取建模面板（bundle 内工厂现调——宿主 Dock setWidget 挂位）。
 *
 * @param bundle [in] 装配产物（assembleDomainPlugins 产物）
 * @return 面板 widget（非 owning——调用方 Dock 接管）
 */
QWidget* modelingPanelWidget(const DomainPluginAssembly& bundle);

/**
 * @brief 取需求主面板（五面板区合一 Tab 容器——descriptor.panels.front()
 *        工厂现调；装配失败的域返回 nullptr——宿主跳过挂位，占位呈现由
 *        共享树承担，§11.3）。
 *
 * @param bundle [in] 装配产物
 * @return 面板 widget（非 owning——调用方 Dock 接管；域缺席＝nullptr）
 */
QWidget* requirementsPanelWidget(const DomainPluginAssembly& bundle);

/**
 * @brief 取运动学主面板（四面板区合一 Tab 容器——失败隔离缺席＝nullptr）。
 *
 * @param bundle [in] 装配产物
 * @return 面板 widget（非 owning——调用方 Dock 接管；域缺席＝nullptr）
 */
QWidget* kinematicsPanelWidget(const DomainPluginAssembly& bundle);

/**
 * @brief 取运动学高级面板（求解配置——UX-04 高级面板位；失败隔离缺席
 *        ＝nullptr）。
 *
 * @param bundle [in] 装配产物
 * @return 面板 widget（非 owning——调用方 Dock 接管；域缺席＝nullptr）
 */
QWidget* kinematicsAdvancedPanelWidget(const DomainPluginAssembly& bundle);

/// 建模模块草稿源（§10.5 attachModule 第二参数——宿主打开成功后挂接）。
ui::IModuleDraftSource& modelingDraftSource(DomainPluginAssembly& bundle);

/// 建模面板 Dock 的标题（宿主 chrome 文案——resolveText 后由调用方设置）。
extern const char* const kModelingDockTitle;
/// 需求面板 Dock 的标题（宿主 chrome 文案——同上）。
extern const char* const kRequirementsDockTitle;
/// 运动学主面板 Dock 的标题（宿主 chrome 文案——同上）。
extern const char* const kKinematicsDockTitle;
/// 运动学高级面板 Dock 的标题（宿主 chrome 文案——同上）。
extern const char* const kKinematicsAdvancedDockTitle;

}  // namespace ui
}  // namespace ird
}  // namespace sdurws

#endif  // IRD_UI_PLUGIN_DOMAINASSEMBLY_HPP
