/**
 * @file   ModelingPanelWidget.hpp
 * @brief  建模域面板（Qt Widgets，L4）——五区信息架构的薄装配（卡 §9.7.1）。
 *
 * 设计依据：
 *   - units/modeling.md §9.7/§9.7.1（五区面板：建模结构树/属性编辑区/域
 *     工具区/就绪与诊断条/预览页）、§9.7.4（插件不直接调用 RobWork——
 *     ARC-03；编辑器仅 UI 线程）
 *   - ARCH §3.3（业务域单元二分结构——面板只消费计算库公共接口＋ui 端口，
 *     零计算逻辑与业务判定）
 *   - 需求 MDL-07；任务契约 tasks/foundation/WP-13-T15.json acceptance 1/2/3
 *
 * 背景说明：本类是 Qt 薄层——信息架构决策全部在零 Qt 呈现模型（PanelModel/
 * PanelSelection/PanelEditFlow/PanelCommandCatalog/PanelRefresh）与计算库
 * （就绪判定/编辑裁决/编码）内；widget 只做：投影结果的控件渲染、用户事件
 * →模型/域入口的转接、命令激活→提交回调的转发。业务判定零入（门禁与
 * 契约测试的扫描面）。刷新一律事件驱动（onRevisionEvent→refreshSink），
 * 无轮询定时器（§9.7.4）。
 *
 * 线程约束：仅 UI 线程构造与访问（QWidget 固有约束＋§3.4——内部持有
 * PanelUiThreadGuard，编辑/刷新入口跨线程即 fail-fast）。
 */

#ifndef IRD_MODELING_PLUGIN_MODELINGPANELWIDGET_HPP
#define IRD_MODELING_PLUGIN_MODELINGPANELWIDGET_HPP

#include <functional>
#include <optional>

#include <QFrame>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTreeWidget>
#include <QWidget>

#include <sdurws/ird/modeling/Readiness.hpp>          // ModelReadinessReport（就绪条数据源）
#include <sdurws/ird/modeling/Template.hpp>           // ModelingWorkingSet（工作集——投影数据源）
#include "PanelCommandCatalog.hpp"                    // modelingDomainCommands/applyReadOnlyGate（命令目录/L-7——同目录私有头）
#include "PanelEditFlow.hpp"                          // IPanelEditSink/submitJointFieldEdit（L-2 编排）
#include "PanelModel.hpp"                             // 五区投影（树/属性/就绪条/预览）
#include "PanelRefresh.hpp"                           // PanelRefreshCoordinator/UI 线程守卫（L-4/§3.4）
#include "PanelSelection.hpp"                         // PanelSelectionState（L-1 会话态）

class QLabel;
class QVBoxLayout;
class QFormLayout;

namespace sdurws::ird::modeling {

/**
 * @brief 建模域面板 widget（五区合一——挂位于 ui 中央区 StageId=modeling 位）。
 *
 * 所有权/生命周期：由 L5 装配层（CentralAreaHost）创建与持有（§10.9
 * PanelRegistration.factory 的产品——ui 线程创建）；本类持有呈现模型对象
 * （选中态/刷新协调器）与会话态，不持有工作集副本（投影入参——ACC5）。
 */
class ModelingPanelWidget final : public QWidget, public IPanelEditSink {
    Q_OBJECT  // AUTOMOC（仅插件目标开启——DTB §5.1 v0.17 行口径；信号槽声明需求＝树联动与刷新回调）

public:
    /// 命令提交出口（装配层注入——绑定 ui ICommandRegistry.submit；非 owning）。
    using CommandSubmitFn = std::function<void(const ui::CommandId&)>;

    /**
     * @brief 构造五区面板（UI 线程——§3.4）。
     *
     * @param writable [in] 初始会话可写性（L-7 门控初始态——ui 会话投影）
     * @param parent   [in] Qt 父对象（常规所有权；可为空——装配层自持）
     */
    explicit ModelingPanelWidget(bool writable = true, QWidget* parent = nullptr);

    /**
     * @brief 注入命令提交出口（装配期一次——面板按钮激活后经此转发到
     *        ui CommandRegistry；不注入＝命令按钮禁用——不虚构可达性）。
     */
    void setCommandSubmit(CommandSubmitFn submitFn);

    /// 命令标题文案解析器形态（titleKey→工程用语——宿主接 ui::resolveText）。
    using CommandTitleResolver = std::function<QString(const std::string& titleKey)>;

    /**
     * @brief 注入命令标题文案解析器（WP-24-T03 首版装配——装配层接
     *        ui::resolveText 后按钮呈现工程用语中文；不注入＝呈现 titleKey
     *        键名原文——不虚构文案，UX-02 的解析归宿主文案体系）。绑定
     *        即时重渲染既有按钮文本（装配序无关）。
     */
    void setCommandTitleResolver(CommandTitleResolver resolver);

    /// 编辑目标提供器（装配层注入——返回会话权威工作集的指针〔UI 线程单
    /// 例〕；L-2 提交面现取零缓存——面板不持有工作集副本，ACC5）。
    using EditTargetProvider = std::function<ModelingWorkingSet*()>;

    /**
     * @brief 注入编辑目标提供器（装配期一次；不注入＝属性编辑禁用——
     *        不虚构可编辑性。返回指针所有权在会话层，面板零持有）。
     */
    void setEditTargetProvider(EditTargetProvider provider);

    /**
     * @brief 全面板刷新（事件驱动的刷新出口——PanelRefreshCoordinator 的
     *        RefreshSink 落点；也可由装配层在打开项目等场景直接驱动）。
     *
     * 五区全部现取重投影（零缓存——ACC5）：树/属性区取 @p ws，就绪条取
     * @p report，预览页仅当装配层提供 AppliedRevisionView（编辑态刷新
     * 不触碰预览页——D-MDL-10 结构保证：本方法无工作集→预览通道）。
     */
    void refreshPanel(const ModelingWorkingSet& ws, const ModelReadinessReport& report);

    /// 会话可写性切换（L-7——ui 只读横幅同源事实；触发展开面重投影）。
    void setWritable(bool writable);

    /// IPanelEditSink（L-2 接受分支）：属性区/就绪条增量刷新＋脏标记呈现。
    void onEditApplied(const std::string& subjectPath) override;
    /// IPanelEditSink（L-2/ L-8 拒绝分支）：就地错误呈现（状态行——非模态）。
    void onEditRejected(const EditRejection& rejection) override;
    /// IPanelEditSink：会话脏通知（标题 `*` 标记——PM-04/PM-11 呈现半区）。
    void notifySessionDirty() override;

Q_SIGNALS:
    /// 会话脏态变化（装配层接 ui DraftController.notifySessionDirty——PM-04）。
    void sessionDirtyChanged(bool dirty);
    /// 选中变化（L-1 正向——装配层接 ui SelectionModel 会话态；零修订）。
    void selectionChanged(const QString& objectKey);

private Q_SLOTS:
    /// 树选中变化（L-1 正向半区入口——写会话态＋属性区重投影）。
    void onTreeSelectionChanged();
    /// 属性行编辑提交（L-2 入口——域裁决，分流见 IPanelEditSink 回调）。
    void onPropertyEditingFinished();
    /// 命令按钮激活（域工具区——转发注入的提交出口）。
    void onCommandButtonClicked();

private:
    // ---- 五区构建（构造期一次——布局骨架；内容全部走 refreshPanel）----
    void buildStructureTreePane(QVBoxLayout* left);     // 区①建模结构树
    void buildPropertyPane(QVBoxLayout* right);         // 区②属性编辑区
    void buildToolsPane(QVBoxLayout* bottom);           // 区③域工具区（StageId=modeling）
    void buildReadinessPane(QVBoxLayout* bottom);       // 区④就绪与诊断条
    void buildPreviewPane(QVBoxLayout* bottom);         // 区⑤预览页（仅已应用修订）

    // ---- 接线辅助 ----
    void refreshPropertiesFromLastWorkingSet();  // 属性区按当前选中重投影（L-1 反向/编辑后）
    std::optional<core::ObjectId> nodeAnchor(QTreeWidgetItem* item) const;  // 树行→锚（隐藏列）

    // ---- 会话态与呈现模型（零 Qt 半区——全部在 plugin/ 呈现层）----
    PanelSelectionState m_selection;          ///< L-1 会话选中态（零修订）
    PanelUiThreadGuard m_threadGuard;         ///< §3.4 UI 线程守卫（构造线程绑定）
    PanelRefreshCoordinator m_refresh;        ///< L-4 刷新协调器（事件驱动）
    std::vector<ui::CommandDescriptor> m_commands;  ///< 域命令目录（§9.7.3 十条——装配数据）
    CommandSubmitFn m_commandSubmit;          ///< 命令提交出口（装配层注入；空＝按钮禁用）
    CommandTitleResolver m_titleResolver;     ///< 标题文案解析器（WP-24-T03；空＝呈现键名原文）
    EditTargetProvider m_editTarget;          ///< 编辑目标提供器（装配层注入；空＝编辑禁用）
    bool m_writable = true;                   ///< 会话可写性（L-7 门控输入）
    bool m_dirty = false;                     ///< 会话脏标记（PM-04/PM-11 呈现半区）

    // ---- 最近一次刷新的工作集投影锚（零数据缓存——只存"选中锚"，内容
    //      一律从调用方入参现取；refreshPanel 结束后本类不持有 ws/report）----
    std::optional<core::ObjectId> m_lastSelected;  ///< 属性区当前选中锚（跨刷新保持）

    // ---- 五区控件（raw 指针＝Qt 父子所有权——构造期挂树，随 Qt 析构）----
    QTreeWidget* m_tree = nullptr;            ///< 区①结构树（隐藏第 1 列＝锚规范文本）
    QFormLayout* m_propertyForm = nullptr;    ///< 区②属性表单（行＝PropertyFieldRow）
    std::vector<QLineEdit*> m_propertyEditors;///< 属性编辑行（与投影行序对应——L-2 提交面）
    std::vector<PropertyFieldRow> m_propertyRows;  ///< 当前属性行（含字段键——提交时的域入口参数）
    std::vector<QPushButton*> m_commandButtons;    ///< 区③命令按钮（与 m_commands 序对应）
    QLabel* m_readinessCounts = nullptr;      ///< 区④三组计数行
    QTreeWidget* m_readinessItems = nullptr;  ///< 区④逐项行（可点击——定位跳转）
    QPlainTextEdit* m_preview = nullptr;      ///< 区⑤预览页（只读——仅已应用修订内容）
    QLabel* m_statusLine = nullptr;           ///< 就地错误/手工处置横幅（非模态——UX-03/07）
};

}  // namespace sdurws::ird::modeling

#endif  // IRD_MODELING_PLUGIN_MODELINGPANELWIDGET_HPP
