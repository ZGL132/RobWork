/**
 * @file   RequirementsPanelWidget.hpp
 * @brief  需求域面板（Qt Widgets，L4）——左栏需求对象树＋右栏工位/区域/
 *         工况/校验四面板的薄装配（卡 §9.8 面板表）。
 *
 * 设计依据：
 *   - units/requirements.md §9.8（面板组成——五区挂接 UX-09；界面逻辑表
 *     L-R1/L-R2/L-R4/L-R12；线程约束行"编辑器仅 UI 线程；命令异步；事件
 *     驱动刷新禁轮询；面板不缓存权威数据"）、§9.8 命令表（九条域命令
 *     ——按钮经装配层转发 ui CommandRegistry）
 *   - ARCH §3.3（业务域单元二分结构——面板只消费计算库公共接口＋ui 端口，
 *     零计算逻辑与业务判定）
 *   - 需求 UX-05（需求界面）、REQ-06（预览呈现）；任务契约
 *     tasks/foundation/WP-14-T08.json acceptance 1/2/4/5
 *
 * 背景说明：本类是 Qt 薄层——信息架构决策全部在零 Qt 呈现模型
 * （PanelTreeModel/PanelStationModel/PanelRegionModel/PanelConditionModel/
 * PanelValidationModel/PanelCommandCatalog/PanelEditFlow/PanelRefresh/
 * PanelSelection/ImportWizardFlow）与计算库（编辑器/校验器/服务）内；
 * widget 只做：投影结果的控件渲染、用户事件→模型/域入口的转接、命令
 * 激活→提交回调的转发。业务判定零入（门禁与契约测试的扫描面）。刷新
 * 一律事件驱动（refreshPanel 由装配层在编辑/修订事件后驱动；无轮询
 * 定时器——§9.8）。
 *
 * 线程约束：仅 UI 线程构造与访问（QWidget 固有约束＋§3.4——内部持有
 * PanelUiThreadGuard，编辑/刷新入口跨线程即 fail-fast）。
 */

#ifndef IRD_REQUIREMENTS_PLUGIN_REQUIREMENTSPANELWIDGET_HPP
#define IRD_REQUIREMENTS_PLUGIN_REQUIREMENTSPANELWIDGET_HPP

#include <functional>
#include <optional>

#include <QFrame>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QTreeWidget>
#include <QWidget>

#include <sdurws/ird/core/Identity.hpp>            // core::ObjectId（节点锚）
#include <sdurws/ird/requirements/Editor.hpp>      // IRequirementEditor/RequirementWorkingSet（投影数据源）
#include <sdurws/ird/requirements/Readiness.hpp>   // RequirementReadinessReport（校验面板数据源）
#include <sdurws/ird/requirements/Services.hpp>    // 领域服务（规范化/必验解析——面板模型域函数入口）
#include "PanelCommandCatalog.hpp"                 // 命令目录/就绪投影/L-R12 行门控
#include "PanelConditionModel.hpp"                 // 工况面板投影
#include "PanelEditFlow.hpp"                       // IRequirementEditSink/submitEntryEdit/LocalUndoTracker
#include "PanelRefresh.hpp"                        // PanelUiThreadGuard/RequirementsRefreshCoordinator
#include "PanelRegionModel.hpp"                    // 区域面板投影
#include "PanelSelection.hpp"                      // PanelSelectionState（L-R1 会话态）
#include "PanelStationModel.hpp"                   // 工位面板投影
#include "PanelTreeModel.hpp"                      // 对象树投影
#include "PanelValidationModel.hpp"                // 校验面板投影

class QLabel;
class QVBoxLayout;
class QFormLayout;

namespace sdurws::ird::requirements {

/**
 * @brief 需求域面板 widget（树＋四面板合一——挂位于 ui 中央区
 *        StageId=requirements 位）。
 *
 * 所有权/生命周期：由 L5 装配层（CentralAreaHost）创建与持有（§10.9
 * PanelRegistration.factory 的产品——ui 线程创建）；本类持有呈现模型对象
 * （选中态/撤销跟踪器/领域服务）与编辑流会话态，不持有工作集与就绪报告
 * 副本（refreshPanel 入参现取——"面板不缓存权威数据"）。
 */
class RequirementsPanelWidget final : public QWidget, public IRequirementEditSink {
    Q_OBJECT  // AUTOMOC（仅插件目标开启——DTB §5.1 v0.17 行口径；信号槽声明需求＝树联动与刷新回调）

public:
    /// 命令提交出口（装配层注入——绑定 ui ICommandRegistry.submit；非 owning）。
    using CommandSubmitFn = std::function<void(const ui::CommandId&)>;
    /// 编辑目标提供器（装配层注入——返回会话权威编辑器指针；L-R2 提交面
    /// 现取零缓存——面板不持有工作集副本）。
    using EditTargetProvider = std::function<IRequirementEditor*()>;
    /// 区域三维预览出口（装配层注入——绑定 ui View3D 契约；不注入＝仅
    /// 文本摘要呈现。预览几何为呈现面——结果着色归 KIN-07，本面板零结果
    /// 语义）。
    using RegionPreviewSink = std::function<void(const RegionPreviewGeometry&)>;

    /**
     * @brief 构造需求域面板（UI 线程——§3.4）。
     *
     * @param writable [in] 初始会话可写性（L-R12 门控初始态——ui 会话投影）
     * @param parent   [in] Qt 父对象（常规所有权；可为空——装配层自持）
     */
    explicit RequirementsPanelWidget(bool writable = true, QWidget* parent = nullptr);

    /// 不可拷贝/不可移动（Qt 对象＋会话态绑定——§10.9 生命周期）。
    RequirementsPanelWidget(const RequirementsPanelWidget&) = delete;
    RequirementsPanelWidget& operator=(const RequirementsPanelWidget&) = delete;

    /** @brief 注入命令提交出口（装配期一次——面板按钮激活后经此转发到
     *         ui CommandRegistry；不注入＝命令按钮禁用——不虚构可达性）。 */
    void setCommandSubmit(CommandSubmitFn submitFn);

    /** @brief 注入编辑目标提供器（装配期一次；不注入＝编辑禁用——不虚构
     *         可编辑性。返回指针所有权在会话层，面板零持有）。 */
    void setEditTargetProvider(EditTargetProvider provider);

    /** @brief 注入区域三维预览出口（装配期；不注入＝预览仅文本摘要）。 */
    void setRegionPreviewSink(RegionPreviewSink sink);

    /**
     * @brief 全面板刷新（事件驱动的刷新出口——装配层在编辑接受/修订事件/
     *        只读切换后调用；也可由装配层在打开项目等场景直接驱动）。
     *
     * 五区全部现取重投影（零缓存）：树/检查器取 @p ws，校验面板取
     * @p report（§9.8——面板不缓存权威数据；refreshPanel 结束后本类不
     * 持有 ws/report）。
     */
    void refreshPanel(const RequirementWorkingSet& ws, const RequirementReadinessReport& report);

    /// 会话可写性切换（L-R12——ui 只读横幅同源事实；触发全面板重投影）。
    void setWritable(bool writable);

    // ---- IRequirementEditSink（L-R2 分流回调——widget 层落点）------------

    /// 编辑接受分支：全面板增量刷新（经编辑目标提供器现取工作集）＋脏标记呈现。
    void onEditApplied(const std::string& changeSummary) override;
    /// 会话脏通知（标题 `*` 标记呈现半区——PM-04/PM-11；信号上呈装配层）。
    void notifySessionDirty() override;
    /// 编辑拒绝分支：就地错误呈现（状态行——非模态，UX-03/07；值控件回退
    /// 显示工作集权威值——由全面板重投影实现）。
    void onEditRejected(const EditRejection& rejection) override;
    /// 批次警告知情登记（L-R9——状态行逐条登记，warning 不阻断应用）。
    void onBatchWarning(const core::DiagnosticRecord& warning) override;

Q_SIGNALS:
    /// 会话脏态变化（装配层接 ui DraftController.notifySessionDirty——PM-04）。
    void sessionDirtyChanged(bool dirty);
    /// 选中变化（L-R1 正向——装配层接 ui SelectionModel 会话态；零修订）。
    void selectionChanged(const QString& objectKey);

private Q_SLOTS:
    /// 树选中变化（L-R1 正向半区入口——写会话态＋检查器重投影）。
    void onTreeSelectionChanged();
    /// 检查器行编辑提交（L-R2 入口——表单回填→submitEntryEdit 域裁决）。
    void onInspectorEditingFinished();
    /// 命令按钮激活（域命令区——转发注入的提交出口）。
    void onCommandButtonClicked();
    /// 草稿级撤销/重做（L-R4 草稿级半区——包裹编辑器局部撤销＋记账）。
    void onDraftUndo();
    void onDraftRedo();
    /// 项目级撤销（L-R4 项目级半区——纯转发面：经注入的命令提交出口转
    /// 发 ui 项目撤销命令；未注入＝禁用——不虚构可达性）。
    void onProjectUndo();

private:
    // ---- 面板构建（构造期一次——布局骨架；内容全部走 refreshPanel）----
    void buildCommandBar(QWidget* top);                    // 顶部域命令区＋两级撤销
    void buildTreePane(QWidget* left);                     // 左栏需求对象树
    void buildStationPage(QTabWidget* pages);              // 右栏页①工位（检查器表单）
    void buildRegionPage(QTabWidget* pages);               // 右栏页②区域（表＋检查器＋预览）
    void buildConditionPage(QTabWidget* pages);            // 右栏页③工况（表＋检查器＋必验预览）
    void buildValidationPage(QTabWidget* pages);           // 右栏页④校验（分层计数＋逐项＋语义说明）

    // ---- 刷新辅助（零缓存——全部从入参/提供器现取）----
    void renderTree(const RequirementWorkingSet& ws);          // 树重投影（保持选中锚）
    void renderInspector(const RequirementWorkingSet& ws);     // 检查器按当前选中重投影（L-R1 反向）
    void renderRegionPage(const RequirementWorkingSet& ws);    // 区域页重投影
    void renderConditionPage(const RequirementWorkingSet& ws); // 工况页重投影
    void renderValidationPage(const RequirementReadinessReport& report);  // 校验页重投影

    // ---- 会话态与呈现模型（零 Qt 半区——全部在 plugin/ 呈现层）----
    PanelSelectionState m_selection;          ///< L-R1 会话选中态（零修订）
    PanelUiThreadGuard m_threadGuard;         ///< §3.4 UI 线程守卫（构造线程绑定）
    LocalUndoTracker m_undoTracker;           ///< L-R4 草稿级撤销记账（canUndo/canRedo 事实源）
    TaskPointService m_pointService;          ///< 领域服务（顺序键/构造校验——面板模型入口）
    WorkRegionService m_regionService;        ///< 领域服务（采样规范化——D-REQ-2 单点复用）
    OperatingConditionService m_conditionService;  ///< 领域服务（必验解析——P-EV-9 单点复用）
    std::vector<ui::CommandDescriptor> m_commands;  ///< 域命令目录（§9.8 九条——装配数据）
    CommandSubmitFn m_commandSubmit;          ///< 命令提交出口（装配层注入；空＝按钮禁用）
    EditTargetProvider m_editTarget;          ///< 编辑目标提供器（装配层注入；空＝编辑禁用）
    RegionPreviewSink m_regionPreview;        ///< 区域三维预览出口（装配层注入；空＝文本摘要）
    bool m_writable = true;                   ///< 会话可写性（L-R12 门控输入）
    bool m_dirty = false;                     ///< 会话脏标记（PM-04/PM-11 呈现半区）

    // ---- 最近一次刷新的选中锚（零数据缓存——只存"选中锚"，内容一律从
    //      调用方入参现取；refreshPanel 结束后本类不持有 ws/report）----
    std::optional<core::ObjectId> m_lastSelected;  ///< 检查器当前选中锚（跨刷新保持）

    // ---- 控件（raw 指针＝Qt 父子所有权——构造期挂树，随 Qt 析构）----
    QTreeWidget* m_tree = nullptr;              ///< 左栏对象树（隐藏第 1 列＝锚规范文本）
    QTabWidget* m_pages = nullptr;              ///< 右栏四页面容器（工位/区域/工况/校验）
    QFormLayout* m_stationForm = nullptr;       ///< 工位检查器表单（行＝StationFieldRow）
    std::vector<QLineEdit*> m_stationEditors;   ///< 工位编辑行（与投影行序对应——L-R2 提交面）
    std::vector<StationFieldRow> m_stationRows; ///< 当前工位行（含字段键——提交时的回填参数）
    QTreeWidget* m_regionTable = nullptr;       ///< 区域表（L-R1 行选中）
    QFormLayout* m_regionForm = nullptr;        ///< 区域检查器表单（行＝StationFieldRow）
    QLabel* m_regionPreviewLabel = nullptr;     ///< 区域预览摘要（几何出口注入时同步投递 View3D）
    QTreeWidget* m_conditionTable = nullptr;    ///< 工况表（L-R1 行选中）
    QFormLayout* m_conditionForm = nullptr;     ///< 工况检查器表单
    QTreeWidget* m_mustList = nullptr;          ///< 必验清单预览（RequirementProfile 投影行）
    QLabel* m_validationCounts = nullptr;       ///< 校验分层计数行（Blocking/Warning 汇总）
    QTreeWidget* m_validationLayers = nullptr;  ///< R0~R9 分层结果行
    QTreeWidget* m_validationItems = nullptr;   ///< 逐项行（可点击——L-R1 反向定位跳转）
    QLabel* m_validationNotes = nullptr;        ///< 预览/正式语义说明（REQ-06 固定文案）
    std::vector<QPushButton*> m_commandButtons; ///< 域命令按钮（与 m_commands 序对应）
    QPushButton* m_draftUndoButton = nullptr;   ///< 草稿级撤销（L-R4 两级之一——独立控件）
    QPushButton* m_draftRedoButton = nullptr;   ///< 草稿级重做（同上）
    QPushButton* m_projectUndoButton = nullptr; ///< 项目级撤销（转发面——与草稿级不混用）
    QLabel* m_statusLine = nullptr;             ///< 就地错误/警告/摘要行（非模态——UX-03/07）
};

}  // namespace sdurws::ird::requirements

#endif  // IRD_REQUIREMENTS_PLUGIN_REQUIREMENTSPANELWIDGET_HPP
