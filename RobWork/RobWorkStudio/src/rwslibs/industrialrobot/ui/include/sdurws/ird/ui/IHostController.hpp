/**
 * @file   IHostController.hpp
 * @brief  宿主控制器簇（IHostController）——方案 B.1 宿主融合产品形态下，
 *         ui 平台能力（命令注册/状态投影/会话/Draft 控制器族）在宿主
 *         （RobWorkStudio 主窗口）内的聚合装配面。
 *
 * 设计依据：
 *   - 方案 B.1 设计规格 B1-SPEC §2（D1 宿主唯一/D2 ui 平台能力转宿主内部
 *     控制器/D13 _plugin 与 _app 过渡期保留为开发验证通道）；
 *   - ARCHITECTURE.md §7.12＋SA-18（宿主融合产品形态与双树边界——本头是
 *     D1/D2 的代码落点之一）；
 *   - units/ui.md §4.1（布局总图的产品形态声明注——宿主为唯一产品主窗口）、
 *     §10.1 v1.17 增量注（本头的首消费冻结登记）、§13 UI-T19 行（任务卡）；
 *   - O-38 裁决（DTB §4.2：壳拆「内容装配层＋宿主层」两层、禁止顶层
 *     QMainWindow 嵌套宿主 Dock）与 O-43 裁决链（宿主深度融合三步承接——
 *     Dock 拓扑/宿主菜单/宿主状态栏已由 UI-T16~T18 落位，本头冻结该形态
 *     为终态的控制器面）；
 *   - 需求 UX-09（工作台承载形态）、ARC-02（端口面不破坏——本头只聚合
 *     既有公共契约，不新建第二命令入口/第二会话状态机，SA-16/PA-1 红线）。
 *
 * 背景说明（为什么需要"宿主控制器簇"这个聚合面）：
 *   方案 B.1 冻结的产品形态是"RobWorkStudio 是唯一产品主窗口"（D1）。在此
 *   形态下，ui 单元的平台能力不再是"一个自带 QMainWindow 的独立工作台"，
 *   而是**宿主 Dock 内的一簇控制器**（D2）：命令注册表（SA-16 唯一注册点）、
 *   状态投影（PM-11 永久文本＋瞬态消息，投影给宿主状态栏）、会话状态机
 *   （§5.2 七态）、草稿控制器（§8 保存半边）。宿主侧的装配层（发布桥
 *   UI-T20/项目树 UI-T21/共享检查器 UI-T22/正式产品装配 WP-24-T08）需要
 *   一个**单一聚合句柄**按族取用这些能力，而不是逐一认识壳/内容层的内部
 *   装配序——本头就是这个聚合句柄的契约。
 *
 * 产品形态声明（D1/D13——本头同时是声明的类型化承载）：
 *   - ProductMainWindow 词表**只有唯一值** RobWorkStudioHost：产品不存在
 *     第二个顶层主窗口形态；该词表是"宿主唯一"承诺在类型系统的固化
 *     （新增值＝产品形态变更，必须走 B1-SPEC 增量修订＋SA-18 变更评审）；
 *   - IWorkbenchShell 顶层窗口装配路径（WorkbenchHostKind::TopLevelWindow，
 *     harness `sdurws_ird_ui_app`）自本头落位起**降格为纯开发验证通道**
 *     （D13/SA-01 口径不变）：不入产品交付路径、不构成产品形态的第二主
 *     窗口；其行为回归仍由既有套件全量承载（UI-T19 验收 2——迁移期保留
 *     可用，禁止顺手删除）。
 *
 * 首消费冻结机制（ui.md §10 引导注）：本头接口签名随 UI-T19 首消费落位
 *   冻结并做单元卡增量修订登记（§10.1 v1.17）；后续消费者（UI-T20 发布桥
 *   ／UI-T21 项目树／WP-24-T08 正式装配）按本头契约取用，不改签名。
 *
 * 线程模型（ui.md §3.4 同口径）：全部方法只允许 UI 线程调用（聚合面本身
 *   无状态——真正状态在被聚合的各控制器内，各自线程纪律不变）。非线程
 *   安全：仅 UI 线程访问。
 *
 * 生命周期/所有权：本聚合不拥有任何被聚合对象——content/session 以引用
 *   注入、draft 以裸指针注入（可空＝该装配形态未装配草稿链路），所有权
 *   全部在宿主装配层（与 ShellWiring"注入实例所有权在装配层"同一纪律）；
 *   聚合实例（unique_ptr，经工厂取得）的存活期≤任一被聚合对象。
 */

#ifndef SDURWS_IRD_UI_IHOSTCONTROLLER_HPP
#define SDURWS_IRD_UI_IHOSTCONTROLLER_HPP

#include <cstdint>
#include <functional>
#include <memory>

#include <sdurws/ird/ui/ICommandRegistry.hpp>     // ui::ICommandRegistry（SA-16 命令注册族——聚合源）
#include <sdurws/ird/ui/IDraftController.hpp>     // ui::IDraftController（§8 Draft 族——聚合源）
#include <sdurws/ird/ui/UiSessionController.hpp>  // ui::UiSessionController（§5.2 会话族——聚合源）
#include <sdurws/ird/ui/WorkbenchContent.hpp>     // ui::IWorkbenchContent（内容装配面——命令注册/状态投影的宿主内来源）

class QString;  // 前置声明：状态投影观察回调入参（随 WorkbenchContent.hpp 已声明，此处显式重申）

namespace sdurws {
namespace ird {
namespace ui {

// =====================================================================
// 产品主窗口形态词表（D1 宿主唯一——类型化声明）
// =====================================================================

/**
 * @brief 产品主窗口形态词表（**单值词表**——唯一值即声明）。
 *
 * 为什么用只有一个值的枚举而不是注释/常量：词表值进入类型系统后，任何
 * "第二产品主窗口形态"的引入都必须显式扩表——扩表动作天然要求 B1-SPEC
 * 增量修订与 SA-18 变更评审（§3 封闭清单同款纪律），无法在代码评审中
 * 被静默绕过。当前唯一值＝RobWorkStudio 宿主（D1）。
 */
enum class ProductMainWindow : std::uint8_t {
    /// 唯一产品主窗口形态：RobWorkStudio 宿主（菜单栏/状态栏/Dock 管理/
    /// 中央区由宿主承载；ui 平台能力以本头控制器簇形态存在于宿主内）。
    RobWorkStudioHost,
};

// =====================================================================
// IHostController——宿主控制器簇聚合面（D2；四控制器族）
// =====================================================================

/**
 * @brief 宿主控制器簇（命令注册/状态投影/会话/Draft 四族的宿主内聚合句柄）。
 *
 * 聚合语义（只聚合、不重建——PA-1 权威唯一）：
 *   - 命令注册族＝内容装配面内部的同一 ICommandRegistry 实例（SA-16 唯一
 *     注册点；宿主菜单/工具栏只路由命令板，§4.2 路由红线——本面不做第二
 *     注册入口，只交出权威实例的引用）；
 *   - 状态投影族＝内容装配面的两条状态观察钩子（PM-11 永久文本＋瞬态
 *     消息——宿主状态栏的承载面，UI-T18 契约面原样转发）；
 *   - 会话族＝装配层创建的 UiSessionController 实例（§5.2 七态状态机的
 *     编程入口；本面只持引用）；
 *   - Draft 族＝装配层创建的 IDraftController 实例（§8 保存半边；可空＝
 *     该装配形态尚未装配草稿链路——harness 形态现状，完整草稿链路任务
 *     接续后非空）。
 *
 * 非法使用：被聚合对象析构后继续使用本聚合（生命周期≤任一被聚合对象——
 * 见文件头"生命周期/所有权"）；从非 UI 线程调用（违约＝未定义行为＋DT
 * 断言——§10.1 通用约定同口径）。
 */
class IHostController {
public:
    virtual ~IHostController() = default;

    // ---- 命令注册族（SA-16 唯一注册点——宿主只路由不私设）----

    /**
     * @brief 命令注册表（内容装配面内部的同一实例——非副本非代理）。
     *
     * 宿主侧（菜单融合/命令面板/快捷键承载）经本引用做**路由与使能态
     * 查询**；登记新命令仍走装配期注册协议（§7.2 静态白名单，无运行期
     * 改写通道——本面不新增登记入口）。
     *
     * @return 内容装配面的命令注册表引用（build() 后恒有效）
     */
    virtual ICommandRegistry& commandRegistry() = 0;

    // ---- 状态投影族（PM-11 双面——宿主状态栏承载；UI-T18 钩子原样转发）----

    /**
     * @brief 注册 PM-11 永久状态文本观察者（转发内容装配面同名钩子；
     *        formatProjectStatusText 唯一权威的投影面语义不变）。
     *
     * @param observer [in] 回调（UI 线程；入参＝PM-11 格式文本；空＝清除；
     *                  生命周期≤内容装配面实例）
     */
    virtual void setStatusTextObserver(std::function<void(const QString&)> observer) = 0;

    /**
     * @brief 注册瞬态状态消息观察者（转发内容装配面同名钩子——命令反馈/
     *        即时可见性补偿等非阻断消息，showMessage 语义）。
     *
     * @param observer [in] 回调（UI 线程；入参＝消息文本＋超时毫秒
     *                  [单位 ms，0＝驻留至下一条]；空＝清除）
     */
    virtual void setStatusMessageObserver(
        std::function<void(const QString& message, int timeoutMs)> observer) = 0;

    // ---- 会话族（§5.2 七态状态机——装配层实例的引用）----

    /**
     * @brief 会话控制器（§5.2 打开/关闭/切换状态机的编程入口）。
     *
     * @return 装配层创建的会话控制器引用（宿主编排打开协议/关闭时序时
     *         经此驱动；触发时机编排归装配层、状态机推进归控制器——
     *         §11.5 分工表原文）
     */
    virtual UiSessionController& sessionController() = 0;

    // ---- Draft 族（§8 保存半边——可空聚合成员）----

    /**
     * @brief 草稿控制器（可空——该装配形态未装配草稿链路时返回 nullptr）。
     *
     * 可空语义的产品形态对照：harness 开发验证形态现状未装配 Draft
     * Controller（PortAdapters 装配注）；宿主融合产品路径的草稿链路随
     * 完整草稿链路任务接续——消费方必须显式处理空值（不假设非空），
     * 空值时保存/恢复入口由装配层决定禁用或占位，不在本聚合内代答。
     *
     * @return 草稿控制器指针（非 owning——所有权在装配层；未装配＝nullptr）
     */
    virtual IDraftController* draftController() = 0;
};

// =====================================================================
// 装配入口
// =====================================================================

/**
 * @brief 创建宿主控制器簇（宿主装配层独占持有——unique_ptr 所有权即刻
 *        移交；被聚合对象以引用/指针注入，所有权不转移）。
 *
 * 为什么是工厂函数：具体实现类封闭在库内（R-2 同 createWorkbenchShell/
 * createWorkbenchContent 惯例），宿主侧只见本契约面。
 *
 * @param content        [in] 内容装配面（build() 后传入——命令注册/状态
 *                       投影两族的聚合源；非空引用，调用方契约）
 * @param session        [in] 会话控制器（装配层实例；非空引用，调用方契约）
 * @param draftController [in] 草稿控制器（可空＝未装配草稿链路——见
 *                       draftController() 注释；非 owning）
 * @return 聚合实例（不拥有被聚合对象——存活期约束见文件头）
 */
std::unique_ptr<IHostController> createHostController(
    IWorkbenchContent& content,
    UiSessionController& session,
    IDraftController* draftController = nullptr);

}  // namespace ui
}  // namespace ird
}  // namespace sdurws

#endif  // SDURWS_IRD_UI_IHOSTCONTROLLER_HPP
