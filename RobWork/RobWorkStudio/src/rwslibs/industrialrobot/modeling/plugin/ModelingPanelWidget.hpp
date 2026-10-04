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
#include <memory>
#include <optional>

#include <QFrame>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QStringList>
#include <QTreeWidget>
#include <QWidget>

#include <sdurws/ird/modeling/Readiness.hpp>          // ModelReadinessReport（就绪条数据源）
#include <sdurws/ird/modeling/Template.hpp>           // ModelingWorkingSet（工作集——投影数据源）
#include <sdurws/ird/ui/FormEditCommon.hpp>           // ParamEditModel/IFormEditOutlet（UI-T53 编辑页公共件）
#include "PanelCommandCatalog.hpp"                    // modelingDomainCommands/applyReadOnlyGate（命令目录/L-7——同目录私有头）
#include "PanelEditFlow.hpp"                          // IPanelEditSink/submitJointFieldEdit（L-2 编排）
#include "GeometryResourceFlow.hpp"                   // runGeometryResourceSelection/GeometrySlot（UI-T48——资源选择流）
#include "PanelModel.hpp"                             // 五区投影（树/属性/就绪条/预览）
#include "PanelRefresh.hpp"                           // PanelRefreshCoordinator/UI 线程守卫（L-4/§3.4）
#include "PanelSelection.hpp"                         // PanelSelectionState（L-1 会话态）

class QLabel;
class QVBoxLayout;
class QFormLayout;
class QComboBox;
class QDoubleSpinBox;
class QStackedWidget;

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
    using CommandAvailabilityFn = std::function<ui::CommandAvailability(const ui::CommandId&)>;

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
    void setCommandAvailability(CommandAvailabilityFn availability);

    /// 编辑后动作钩子形态（T03b-2b——就绪重算由装配层注入）。
    using PostEditAction = std::function<void()>;

    /**
     * @brief 注入编辑后动作（每次 L-2 接受后触发——装配层接就绪重算；
     *        不注入＝无后置动作，行为同前）。
     */
    void setPostEditAction(PostEditAction action);

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

    /// 已应用修订预览注入（UI-T41 A3——模块应用/失效时调用；D-MDL-10：
    /// 内容只来自 AppliedRevisionView，nullopt＝空态占位）。
    void setAppliedPreview(const std::optional<AppliedRevisionView>& view);

    /// 状态行回执分级（UI-T41 批次B B2——词表着色：Success 绿＝接受回执、
    /// Warning 橙＝拒绝/待处置、Info 正文＝命令回执；阻断不设红色——调色板
    /// 五色词表无红，阻断以警示橙加粗承载，防彩虹化纪律）。
    enum class OutcomeSeverity { Info, Success, Warning };

    /// 命令流程回执/原因就地呈现（UI-T41 A2——状态行非模态唯一出口；
    /// 批次B：按 severity 着色＋追加诊断历史，不清空覆盖）。
    void setOutcomeMessage(const QString& message,
                           OutcomeSeverity severity = OutcomeSeverity::Info);
    /// 修订事件重演入口（UI-T41 A4——模块 onRevisionCommitted 调用；L-4
    /// 保序重演＋手工处置横幅。无会话编辑面＝静默返回）。
    void replayOnRevisionEvent();
    /// 当前选中锚（UI-T41——命令流的目标解析输入；nullopt＝无选中）。
    std::optional<core::ObjectId> selectedAnchor() const { return m_lastSelected; }

    /// 会话可写性切换（L-7——ui 只读横幅同源事实；触发展开面重投影）。
    void setWritable(bool writable);

    /**
     * @brief 定位并高亮指定对象（宿主迁移三接入面的域面板执行器——
     *        WP-13-T20：SelectionAdapter 的"树选→面板高亮"落点与 D6
     *        复杂编辑页"域自持打开"的激活动作共用本入口）。
     *
     * 行为：写会话选中锚（幂等——重复定位同一对象不重复刷新）→自持
     * 结构树滚动至对应行并置为当前行（触发行选中→属性区重投影——L-1
     * 既有数据流复用，零新增刷新路径）。
     *
     * @param oid [in] 目标对象身份（nullopt＝仅清除选中高亮——多选/
     *            清空选中时的对称收口；闭包外对象按清除处置——不伪造
     *            定位）；零修订（KIN-06/AT-04——选中是会话态）
     */
    void focusObject(const std::optional<core::ObjectId>& oid);

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
    /// 属性区＋编辑页的统一刷新入口（选中/编辑后/refreshPanel 共用——
    /// UI-T53 起本入口在属性行重投影后同步驱动编辑页，两区同源刷新）。
    void refreshPropertiesFromLastWorkingSet();
    /// 属性行重投影（UI-T53 从 refreshPropertiesFromLastWorkingSet 抽出
    /// ——原实现整体更名，刷新入口语义不变）。
    void refreshPropertyRowsFromLastWorkingSet();
    std::optional<core::ObjectId> nodeAnchor(QTreeWidgetItem* item) const;  // 树行→锚（隐藏列）
    /// 结构树全量重建（UI-T47 从 refreshPanel 抽出——结构操作按钮槽与
    /// refreshPanel 共用同一树投影，零第二实现）。
    void refreshStructureTree(const ModelingWorkingSet& ws);
    /// 结构操作按钮槽（UI-T47——新增/删除/上移/下移/六轴重置五钮的共用
    /// 落点；域裁决唯一在 StructureEdit 四原语，本槽零判定）。
    void onStructureOpClicked(int opIndex);
    /// 几何挂接按钮槽（UI-T48——资源选择器入口；域裁决唯一在 GeometryLinkEdit）。
    void onGeometryAttachClicked(GeometrySlot slot);
    /// 几何摘除按钮槽（UI-T48——幂等摘除；清单条目不级联删除）。
    void onGeometryDetachClicked(GeometrySlot slot);
    /// 视觉→碰撞复制按钮槽（UI-T49——§5.2 几何生成辅助②；域裁决唯一在
    /// GeometryLinkEdit.copyVisualToCollision，覆盖确认交互在本槽承载）。
    void onGeometryCopyToCollisionClicked();
    /// 复制钮禁用态刷新（UI-T49——visual 未设＝禁用＋toolTip 原因；重建
    /// 与增量两条投影路径共用同一判定，杜绝禁用态滞后）。
    void refreshCollisionCopyGating(const ModelingWorkingSet& ws,
                                    std::size_t linkIndex);

    // ---- UI-T53 关节详细编辑页（"编辑"页签——B.1 复杂对象编辑模式关节侧）----

    /// 编辑页出口（ParamEditSet→域编辑流分组转译——.cpp 内定义的具体类；
    /// 域裁决唯一在 applyJointFieldEdit，本类零判定）。
    class JointDetailEditOutlet;

    /// 编辑页签骨架（构造期一次——提示行＋ParamTablePanel 宿主容器）。
    void buildJointEditPane(QVBoxLayout* bottom);
    /// 编辑页随选中/权威变化同步（refreshPropertiesFromLastWorkingSet 尾
    /// 驱动）：换目标＝重建模型（specs 量纲随关节类型），同目标＝推基线
    /// （暂存编辑保留——未完成输入不因刷新丢失）。
    void refreshJointEditPane();
    /// 编辑页模型重建（字段集 12 行装配＋createParamTablePanel 挂载）。
    void rebuildJointEditModel(const JointEntry& joint);
    /// 权威值→表单基线回填（SI 真值；未提供字段＝nullopt 基线——kFieldUnset
    /// 占位呈现，不伪造 0）。
    void pushJointEditBaselines(const JointEntry& joint);
    /// 出口移交落点（IFormEditOutlet::applyEdits 转接——键分组装配域编辑
    /// 流：axis-*→Axis、origin-*→Origin、zero-offset→ZeroOffset、
    /// bounds-*→Bounds；提交序固定登记序，单组拒绝不阻断其余组）。
    void applyJointDetailEdits(const ui::ParamEditSet& editSet);

    // ---- UI-T54 基座安装姿态编辑页（"编辑"页签第二模式——锚外对象入口）----

    /// 基座安装编辑页内容构建（构造期一次——表单挂 m_basePage 布局）。
    void buildBasePlacementPane();
    /// 权威值→基座表单回填（ws 现取；预设组合框/位置/EAA 三组；进页与
    /// 应用后调用——不在全局刷新中回填，避免踩踏未应用输入）。
    void refreshBasePlacementPane();
    /// 应用槽（整体替换语义→applyBasePlacementEdit 域原语；接受走 L-2
    /// 分流〔onEditApplied〕，拒绝经 basePlacementEditErrorCodeToken 就地
    /// 呈现——工作集字节不变由域内强保证）。
    void onBasePlacementApplyClicked();
    /// 还原槽（放弃未应用输入——权威值回填，零域调用零脏化）。
    void onBasePlacementRestoreClicked();

    /**
     * @brief 命令按钮使能态统一刷新（UI-T43——L-7 门控三输入的单一判定面）。
     *
     * 判定合取（三输入缺一即禁用——不虚构可达性）：
     *   ①提交出口已注入（m_commandSubmit 非空）；
     *   ②只读门控放行（writable=true，或命令 readOnlyAllowed=true——
     *     §7.6/L-7 的面板半区）；
     *   ③注册表可用性放行（未注入提供器＝不设限；注入后按快照求值）。
     *
     * setCommandSubmit／setCommandAvailability／setWritable／refreshPanel
     * 四个触发点全部经本单出口刷新（原四处循环重复实现收敛——返工先例
     * ＝需求面板 refreshCommandEnablement）；此后任一输入变化（如只读
     * 切换后再刷新可用性快照）都不会复活被另一输入禁用的按钮。
     */
    void refreshCommandEnablement();

    // ---- 会话态与呈现模型（零 Qt 半区——全部在 plugin/ 呈现层）----
    PanelSelectionState m_selection;          ///< L-1 会话选中态（零修订）
    PanelUiThreadGuard m_threadGuard;         ///< §3.4 UI 线程守卫（构造线程绑定）
    PanelRefreshCoordinator m_refresh;        ///< L-4 刷新协调器（事件驱动）
    std::vector<ui::CommandDescriptor> m_commands;  ///< 域命令目录（§9.7.3 十条——装配数据）
    CommandSubmitFn m_commandSubmit;          ///< 命令提交出口（装配层注入；空＝按钮禁用）
    CommandAvailabilityFn m_commandAvailability; ///< 注册表可用性查询（空＝仅提交出口门控）
    CommandTitleResolver m_titleResolver;     ///< 标题文案解析器（WP-24-T03；空＝呈现键名原文）
    PostEditAction m_postEditAction;          ///< 编辑后动作（T03b-2b 就绪重算钩子；可空）
    EditTargetProvider m_editTarget;          ///< 编辑目标提供器（装配层注入；空＝编辑禁用）
    bool m_writable = true;                   ///< 会话可写性（L-7 门控输入）
    bool m_dirty = false;                     ///< 会话脏标记（PM-04/PM-11 呈现半区）

    // ---- 最近一次刷新的工作集投影锚（零数据缓存——只存"选中锚"，内容
    //      一律从调用方入参现取；refreshPanel 结束后本类不持有 ws/report）----
    std::optional<core::ObjectId> m_lastSelected;  ///< 属性区当前选中锚（跨刷新保持）
    std::optional<AppliedRevisionView> m_appliedPreview; ///< 预览页数据源（A3——模块注入的已应用修订视图）

    // ---- UI-T41 批次B（B1/B2/B3 呈现态）--------------------------------
    QLineEdit* m_warningEditor = nullptr;     ///< 校验拒绝的行内警示编辑器（B1——警示描边；重建即清）
    QPushButton* m_historyToggle = nullptr;   ///< 诊断历史折叠钮（B2——可勾选）
    QPlainTextEdit* m_historyView = nullptr;  ///< 诊断历史只读视图（B2——最近 20 条不清空覆盖）
    QStringList m_history;                    ///< 诊断历史内容（B2——定容 FIFO，20 条）
    /// 视觉→碰撞复制钮（UI-T49——禁用态随 visual 有无变化；QPointer：
    /// 属性区重建路径销毁行控件时自动置空，增量路径安全刷新）
    QPointer<QPushButton> m_collisionCopyBtn;

    // ---- UI-T53 关节详细编辑页（"编辑"页签——构造期挂树，随 Qt 析构）----
    std::unique_ptr<ui::IFormEditOutlet> m_jointEditOutlet; ///< 移交出口（构造期一次——转译到本面板域编辑流）
    QWidget* m_jointEditHost = nullptr;       ///< 页签容器（提示行＋面板宿主——QVBoxLayout 承载）
    QLabel* m_jointEditHint = nullptr;        ///< 空态提示（非关节选中/无会话——不伪造编辑页）
    QWidget* m_jointEditPanel = nullptr;      ///< createParamTablePanel 产物（换选中目标时重建）
    std::unique_ptr<ui::ParamEditModel> m_jointEditModel;  ///< 编辑会话模型（基线/暂存/就地错误三层——ui 公共件）
    std::optional<std::size_t> m_jointEditTarget;  ///< 页当前绑定关节下标（nullopt＝未绑定——重建/推基线分路锚）

    // ---- UI-T54 基座安装姿态编辑页（raw 指针＝Qt 父子所有权）----
    QStackedWidget* m_editStack = nullptr;      ///< 编辑页签模态堆栈（0＝关节区，1＝基座安装页）
    QWidget* m_basePage = nullptr;              ///< 基座安装编辑页（stacked 第 2 页）
    QComboBox* m_basePreset = nullptr;          ///< 安装预设（四值词表——runtime 单一权威的呈现映射）
    QDoubleSpinBox* m_basePosX = nullptr;       ///< 基座原点位置 X，单位 m（世界坐标系下表示）
    QDoubleSpinBox* m_basePosY = nullptr;       ///< 基座原点位置 Y，单位 m（世界坐标系下表示）
    QDoubleSpinBox* m_basePosZ = nullptr;       ///< 基座原点位置 Z，单位 m（世界坐标系下表示）
    QDoubleSpinBox* m_baseEaaX = nullptr;       ///< Custom 预设 EAA 绕 X 分量，单位 rad（仅自定义预设启用）
    QDoubleSpinBox* m_baseEaaY = nullptr;       ///< Custom 预设 EAA 绕 Y 分量，单位 rad（仅自定义预设启用）
    QDoubleSpinBox* m_baseEaaZ = nullptr;       ///< Custom 预设 EAA 绕 Z 分量，单位 rad（仅自定义预设启用）
    QPushButton* m_baseApplyBtn = nullptr;      ///< 应用钮（整体替换提交）
    QLabel* m_baseHint = nullptr;               ///< 基座页无会话/只读态提示

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
