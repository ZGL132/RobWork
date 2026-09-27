/**
 * @file   HostController.cpp
 * @brief  宿主控制器簇（IHostController）的实现——四控制器族（命令注册/
 *         状态投影/会话/Draft）在宿主内的纯聚合转发面（UI-T19，方案 B.1
 *         D2；契约头 include/sdurws/ird/ui/IHostController.hpp）。
 *
 * 设计依据：
 *   - units/ui.md §10.1 v1.17 增量注（IWorkbenchContent::commandRegistry()
 *     访问器增量＋本实现的首消费落位登记）、§13 UI-T19 行；
 *   - B1-SPEC §2 D2（ui 平台能力转宿主内部控制器——本实现只聚合既有权威
 *     实例，不重建任何控制器语义：PA-1 权威唯一/SA-16 命令唯一入口红线
 *     的结构性执行面）；
 *   - R-2 红线：实现类封闭在库内（本 TU 匿名命名空间），宿主侧只见契约头。
 *
 * 背景说明（为什么全部是转发、没有一行新语义）：UI-T19 的验收是"行为回归
 *   零变化"（契约 acceptance 2）。本聚合的价值在**装配图的形状**——宿主侧
 *   装配层（UI-T20+ 发布桥/项目树、WP-24-T08 正式装配）以单一句柄取用四族
 *   能力，替代"逐一认识壳/内容层装配序"的隐式知识；被聚合的每一族仍以
 *   既有实现为唯一权威（命令注册＝内容层同一实例，状态投影＝内容层钩子
 *   原样转发），因此任何行为变化都会先体现在既有控制器上、由既有测试
 *   套件拦截，本层零新增行为面。
 *
 * 线程模型（ui.md §3.4）：全部方法 UI 线程调用（聚合面无状态——真正的
 *   状态在被聚合对象内，各自线程纪律不变）。非线程安全。
 */

#include <sdurws/ird/ui/IHostController.hpp>

#include <utility>

namespace sdurws {
namespace ird {
namespace ui {

namespace {

/**
 * @brief 宿主控制器簇实现（文件私有——R-2：实现类型不出库）。
 *
 * 成员全部为引用/裸指针（非 owning——所有权在宿主装配层，契约头"生命
 * 周期/所有权"行：本实例存活期≤任一被聚合对象）。转发语义逐方法对应
 * 契约头注释，此处不重复。
 */
class HostControllerImpl final : public IHostController {
public:
    /// 装配期一次性绑定被聚合对象（工厂唯一入口——不提供默认构造）。
    HostControllerImpl(IWorkbenchContent& content,
                       UiSessionController& session,
                       IDraftController* draftController)
        : m_content(content)
        , m_session(session)
        , m_draft(draftController)
    {
    }

    // ---- 命令注册族：交出内容装配面内部的权威实例（SA-16——非副本）----
    ICommandRegistry& commandRegistry() override
    {
        return m_content.commandRegistry();
    }

    // ---- 状态投影族：原样转发内容装配面双钩子（UI-T18 契约面语义不变）----
    void setStatusTextObserver(std::function<void(const QString&)> observer) override
    {
        m_content.setStatusTextObserver(std::move(observer));
    }

    void setStatusMessageObserver(
        std::function<void(const QString& message, int timeoutMs)> observer) override
    {
        m_content.setStatusMessageObserver(std::move(observer));
    }

    // ---- 会话族：返回装配层实例引用（§11.5 分工不变——编排归装配层）----
    UiSessionController& sessionController() override
    {
        return m_session;
    }

    // ---- Draft 族：可空聚合成员（未装配草稿链路的形态返回 nullptr）----
    IDraftController* draftController() override
    {
        return m_draft;
    }

private:
    /// 内容装配面（命令注册/状态投影两族的来源；非 owning）。
    IWorkbenchContent& m_content;
    /// 会话控制器（§5.2 状态机；非 owning）。
    UiSessionController& m_session;
    /// 草稿控制器（§8 保存半边；可空＝未装配；非 owning）。
    IDraftController* m_draft;
};

}  // namespace

std::unique_ptr<IHostController> createHostController(
    IWorkbenchContent& content,
    UiSessionController& session,
    IDraftController* draftController)
{
    return std::make_unique<HostControllerImpl>(content, session, draftController);
}

}  // namespace ui
}  // namespace ird
}  // namespace sdurws
