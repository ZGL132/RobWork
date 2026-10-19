/**
 * @file   HostEngineeringPolicy.hpp
 * @brief  宿主工程策略适配器（A-23）——系统缺省与项目发布策略的只读投影、
 *         按修订解析回调和编辑入口。
 *
 * 适配器由 UI 宿主持有；策略语义仍归 policy，项目对象/修订仍归 project。
 * 本头是 plugin 私有头，不改变 UI 公共端口和项目存储格式。
 */
#ifndef SDURWS_IRD_UI_HOSTENGINEERINGPOLICY_HPP
#define SDURWS_IRD_UI_HOSTENGINEERINGPOLICY_HPP

#include <functional>
#include <memory>
#include <sdurws/ird/project/CommandService.hpp>
class QWidget;

#include <sdurws/ird/policy/PolicyPort.hpp>
#include <sdurws/ird/project/QueryPort.hpp>
#include <sdurws/ird/project/ProjectStore.hpp>
#include <sdurws/ird/ui/UiPorts.hpp>

#include "../src/WorkbenchShell_p.hpp"

namespace sdurws::ird::ui {

/**
 * @brief 项目策略在L5的单一装配适配器；由UiPlugin共享持有，仅UI线程可绑定/编辑。
 * 原始project指针只用于同步测试借用；生产用shared别名保活，关闭后只返回失败诊断。
 * 发布策略纯值可跨线程持有，但后台不得调用本对象的可变绑定/HEAD接口。
 */
class HostEngineeringPolicy final : public IPolicySummarySource,
                                    public detail::IPolicySummaryEditSource {
public:
    using EditCallback = std::function<void()>;
    using RevisionResolver = std::function<policy::PolicyResolution(
        const project::RevisionView&)>;

    /** @brief 构造系统缺省源；editCallback[in]空则编辑入口无动作，不获得写权限。 */
    explicit HostEngineeringPolicy(EditCallback editCallback = {});
    /** @brief 返回当前修订SI摘要；坏策略/多候选为available=false，无项目显示缺省。 */
    PolicySummaryProjection summary() const override;
    /** @brief 同步触发宿主编辑回调；必须由命令注册表先完成只读/项目门控。 */
    void requestPolicyEdit() override;

    /** @brief 绑定当前项目的只读修订源；绑定不取得项目所有权。 */
    void setRevisionSource(std::function<project::RevisionView()> source);

    /** @brief 绑定项目上下文并装配同一套按修订策略解析逻辑。 */
    void bindProjectStore(project::ProjectStore* store);

    /** @brief 生产绑定：store[in]共享别名保活到解析和模态编辑结束，关闭后拒绝读取。 */
    void bindProjectStore(std::shared_ptr<project::ProjectStore> store);
    /** @brief 装配期仅注册一次的私有命令；registry[in,out]为当前项目处理器表。 */
    static void registerCommandHandler(project::HandlerRegistry& registry);
    /** @brief 经project提交完整草稿；base[in]为打开表单时基线，过期/只读零写入。 */
    static project::CommandResult submit(project::ProjectStore& store,
        const project::RevisionView& base, const policy::RawPolicyInput& input, bool allocateNew);
    /** @brief 模态编辑全字段；parent[in]借用Qt父窗口，取消无写入，诊断可原位重试。 */
    void edit(QWidget* parent);
    /** @brief UI线程重读当前修订并发布共享纯值；失败发布空，不回退。 */
    void refreshPublishedPolicy();
    /** @brief 返回当前发布值；后台请求必须按值捕获shared_ptr，不在后台回查HEAD。 */
    std::shared_ptr<const policy::EngineeringPolicySet> publishedPolicy() const { return m_publishedPolicy; }

    /** @brief 绑定宿主真实的按修订解析器；UI线程装配，后台只读消费。 */
    void setRevisionResolver(RevisionResolver resolver);
    /**
     * @brief 只解析revision[in]闭包内的(oid,cv)，不以当前HEAD替代。
     * @return 已发布不可变值或空值及完整诊断；无策略对象才消费系统缺省。
     * @throws policy::PolicyError 调用方身份契约错误；数据错误以诊断返回。
     */
    policy::PolicyResolution resolvePolicyForRevision(
        const project::RevisionView& revision) const;

private:
    std::shared_ptr<project::ProjectStore> m_storeOwner; ///< 生产路径的别名保活，不独占关闭权。
    project::ProjectStore* m_store = nullptr; ///< 测试可同步借用；生产由m_storeOwner保活。
    std::shared_ptr<const policy::EngineeringPolicySet> m_publishedPolicy; ///< UI发布、任务捕获的不可变策略。
    policy::EngineeringPolicySet m_policy;
    EditCallback m_editCallback;
    RevisionResolver m_revisionResolver;
    std::function<project::RevisionView()> m_revisionSource;
};

}  // namespace sdurws::ird::ui

#endif
