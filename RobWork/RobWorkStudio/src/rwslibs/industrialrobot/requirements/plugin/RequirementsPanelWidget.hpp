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

#include <array>
#include <functional>
#include <optional>

#include <QFrame>
#include <QLabel>
#include <QComboBox>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QTreeWidget>
#include <QWidget>

#include <sdurws/ird/core/Identity.hpp>            // core::ObjectId（节点锚）
#include <sdurws/ird/requirements/Editor.hpp>      // IRequirementEditor/RequirementWorkingSet（投影数据源）
#include <sdurws/ird/requirements/Readiness.hpp>   // RequirementReadinessReport（校验面板数据源）
#include <sdurws/ird/requirements/RequirementsPluginAssembly.hpp>  // RequirementsView3DSeams（UI-T33——本单元门面消费面）
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

class QAction;
class QGroupBox;
class QLabel;
class QScrollArea;
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
    using CommandAvailabilityFn = std::function<ui::CommandAvailability(const ui::CommandId&)>;
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
    void setCommandAvailability(CommandAvailabilityFn availability);

    /** @brief 注入编辑目标提供器（装配期一次；不注入＝编辑禁用——不虚构
     *         可编辑性。返回指针所有权在会话层，面板零持有）。 */
    void setEditTargetProvider(EditTargetProvider provider);

    /// 编辑后动作钩子形态（UI-T29 最小校验——装配层接就绪重算＋refreshPanel
    /// 编排；modeling 面板 setPostEditAction 同款先例）。
    using PostEditAction = std::function<void()>;

    /**
     * @brief 注入编辑后动作（每次 L-2 接受后触发——装配层在此重估就绪
     *        并以新报告 refreshPanel；不注入＝无后置动作，行为同前）。
     */
    void setPostEditAction(PostEditAction action);

    /** @brief 注入区域三维预览出口（装配期；不注入＝预览仅文本摘要）。 */
    void setRegionPreviewSink(RegionPreviewSink sink);

    /// 工位标记投影值（UI-T33 收口——非禁用工位的挂帧标记；enabled 过滤
    /// 在面板——投影值集合即呈现集合）。refFrame 原值直投（宿主帧名解析
    /// 归投影方——World/ModelFrame 的宿主帧名映射在装配层全缝侧）。
    struct StationMarkerProjection {
        std::string label;                          ///< 工位名（场景节点名后缀——UX-02）
        RequirementReference refFrame;              ///< 参考系原值（投影方解析宿主帧名）
    rw::math::Vector3D<double> position;       ///< 标记位置（m；refFrame 系——UI-T74/F-555 增补：工位坐标进三维投影）
    };
    /// 工位标记出口（随面板重载全量投递——会话编辑/选择刷新的承接点）。
    using StationMarkersSink =
        std::function<void(const std::vector<StationMarkerProjection>&)>;

    /** @brief 注入工位标记出口（装配期；不注入＝无三维标记投递）。 */
    void setStationMarkersSink(StationMarkersSink sink);

    /// 工位标记投递（refreshPanel 枢纽调用——非禁用工位全量集合；出口
    /// 未注入＝静默跳过）。
    void emitStationMarkers(const RequirementWorkingSet& ws);

    /**
     * @brief 工位页当前选中锚（UI-T33——拾取写回的目标点解析输入；
     *       nullopt＝未选择，命令流以诚实指引呈现）。
     */
    std::optional<core::ObjectId> selectedPointId() const
    {
        return m_stationSelectedId;
    }

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

    /**
     * @brief 会话脱离复位（UI-T39——审核 P1：项目关闭/切换后清理全部旧
     *        会话态的按钮残留）。清空会话选中锚与草稿撤销记账（栈随编辑
     *        器基线复位的事实对齐），控件投影收拢为无会话空态（树/表/
     *        卡片清空、生命周期与撤销键全禁、空态提示显现）。零修订。
     */
    void resetForSessionDetached();

    /**
     * @brief 基线重载登记（UI-T39——宿主在编辑器 loadBaseline 重建会话
     *        后调用：草稿撤销记账随编辑器局部栈复位归零＋撤销键刷新；
     *        漏登记＝记账指向已不存在的栈条目，撤销键呈"幽灵可撤销"）。
     */
    void noteBaselineReloaded();

    /**
     * @brief 按集合种类取当前选中锚（UI-T39——审核五.1：命令按输入类型
     *        校验选中对象，不依赖"全局最后选中"）。
     *
     * @param member [in] 集合种类（Points＝工位页锚；Regions/Conditions 同理）
     * @return 该页当前选中锚（未选择＝nullopt——调用方就地提示，不虚构源条目）
     */
    std::optional<core::ObjectId> selectionAnchor(WorkingSetMember member) const;

    /**
     * @brief 定位并高亮指定对象（宿主迁移三接入面的域面板执行器——
     *        WP-14-T10：SelectionAdapter 的"树选→面板高亮"落点与 D6
     *        复杂编辑页"域自持打开"的激活动作共用本入口）。
     *
     * 行为：写会话选中锚（幂等——重复定位同一对象不重复刷新）→自持
     * 对象树滚动至对应行并置为当前行（触发行选中→检查器重投影——L-R1
     * 既有数据流复用，零新增刷新路径）。
     *
     * @param oid [in] 目标对象身份（nullopt＝仅清除选中高亮——多选/
     *            清空选中时的对称收口；闭包外对象按清除处置——不伪造
     *            定位）；零修订（KIN-06/AT-04——选中是会话态）
     */
    void focusObject(const std::optional<core::ObjectId>& oid);

    /**
     * @brief 当前会话选中锚（UI-T32 命令流程的源条目定位——镜像/阵列/
     *        重生成按选中条目取源；无选中＝nullopt）。
     */
    std::optional<core::ObjectId> selectedObjectId() const
    {
        return m_lastSelected;
    }

    /// 状态行反馈出口（命令流程的就地中文呈现——UX-02/03）。
    void showCommandFeedback(const QString& text) { showStatusLine(text); }

    /// 状态行只读回取（gui 用例断言面——降级/错误文案呈现）。
    QString showCommandFeedbackText() const { return m_statusLine->text(); }

    /**
     * @brief 执行一条域命令的 UI 流程（UI-T32——命令路由的执行端：经
     *        RequirementsCommandFlows 装配对话框/表单与域纯函数；无会话
     *        ＝就地提示不虚构）。
     * @return 流程是否完成（应用/用户取消＝true；失败/未知命令＝false）
     */
    bool executeDomainCommand(const std::string& commandId);

    /**
     * @brief 域命令执行（UI-T33 重载——三维视图缝透传命令流：capture-tcp/
     *        pick-feature 随缝在位解除降级；view3d 为空指针＝诚实降级原文，
     *        其余命令零影响）。
     */
    bool executeDomainCommand(const std::string& commandId,
                              const RequirementsView3DSeams* view3d);

    // ---- IRequirementEditSink（L-R2 分流回调——widget 层落点）------------

    /// 编辑接受分支：全面板增量刷新（经编辑目标提供器现取工作集）＋脏标记呈现。
    void onEditApplied(const std::string& changeSummary) override;
    /// 会话脏通知（标题 `*` 标记呈现半区——PM-04/PM-11；信号上呈装配层）。
    void notifySessionDirty() override;

    // ---- UI-T37 R2：工况新增向导（acceptance 4——创建即带完整起步配置；
    //      取消＝零新增。注入缝与 UI-T32 CommandDialogHost/FakeDialogHost
    //      同族：缺省＝内建 QDialog 面板，测试注入确定字段工厂）------------
    /// 向导确认字段（验收要求 mustVerify→RequirementLevel::Must/Should——
    /// 必验派生归域侧 resolveRequiredCases，面板不私判 I-REQ-9）。
    struct ConditionWizardFields {
        std::string name;           ///< 工况名（空＝面板用防撞默认名）
        bool hasCycle = false;      ///< 目标节拍是否设置（false＝未设，不伪造）
        double cycleSeconds = 0.0;  ///< 目标节拍（s；hasCycle 时有效，>0）
        bool mustVerify = true;     ///< true＝必验（Must）；false＝可选（Should）
    };
    using ConditionWizardFn =
        std::function<std::optional<ConditionWizardFields>()>;
    /// 注入工况新增向导工厂（装配期一次；缺省＝内建对话框）。
    void setConditionWizardFactory(ConditionWizardFn factory)
    {
        m_conditionWizard = std::move(factory);
    }
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
    /// 区域检查器行编辑提交（UI-T31 B2——工位轨同构：权威值对照短路→
    /// parseFieldValueText→applyRegionEditSet→submitEntryEdit）。
    void onRegionFieldEditingFinished();
    /// 工况检查器行编辑提交（UI-T31 B2——同上，applyConditionEditSet）。
    void onConditionFieldEditingFinished();
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
    /// 页签状态行刷新（UI-T26；返工⑤——格式改面包屑『工位 > 〈名|未选择〉』，
    /// 去与页签名重复的冒号态）。
    void updateTabHeaders();
    /// 生命周期键＋属性区空态统一刷新（返工⑤——会话内未选择＝复制/删除
    /// 置灰仅保留新增＋空态提示替代空卡骨架；无会话/只读＝全禁诚实态）。
    void refreshLifecycleAndEmptyStates();
    /// 命令按钮/菜单动作可用性统一刷新（返工⑤——原三处循环收敛单出口；
    /// 导入下拉＝两导入命令可用性之或，更多操作下拉＝六条派生命令之或
    /// ——UI-T39 命令条分组后第二下拉宿主）。
    void refreshCommandEnablement();
    /// 两级撤销三键可用性/提示统一刷新（UI-T39——审核 P1：草稿级键随
    /// 记账器与可写性；项目级键随 project.undo 命令可用性快照〔无项目/
    /// 只读/无可撤销修订＝禁用＋原因提示〕；无会话＝三键全禁零残留）。
    void refreshUndoButtons();
    /// 空态提示标签工厂（返工⑤——objectName ird_req_empty_hint；文本走
    /// UiText 词表，默认隐藏由统一刷新驱动）。
    QLabel* makeEmptyStateHint(QWidget* parent, const char* key);

    // ---- 会话态与呈现模型（零 Qt 半区——全部在 plugin/ 呈现层）----
    PanelSelectionState m_selection;          ///< L-R1 会话选中态（零修订）
    PanelUiThreadGuard m_threadGuard;         ///< §3.4 UI 线程守卫（构造线程绑定）
    LocalUndoTracker m_undoTracker;           ///< L-R4 草稿级撤销记账（canUndo/canRedo 事实源）
    TaskPointService m_pointService;          ///< 领域服务（顺序键/构造校验——面板模型入口）
    WorkRegionService m_regionService;        ///< 领域服务（采样规范化——D-REQ-2 单点复用）
    OperatingConditionService m_conditionService;  ///< 领域服务（必验解析——P-EV-9 单点复用）

    // ---- UI-T37 R1：卡片化检查器（acceptance 1/2/3——卡片标题/帮助位经
    //      UiText 键族①c；主题经 ui UiTheme::applyIndustrialTheme 面板作用
    //      域安装）。卡片＝QFrame#card（UiTheme::createCard 工厂）；表单行
    //      按字段键路由到所属卡片（stationFormForKey/regionFormForKey）----
    QGroupBox* m_stationBasicCard = nullptr;     ///< 工位卡①基础属性
    QFormLayout* m_stationBasicForm = nullptr;   ///< 卡①行宿主
    QGroupBox* m_stationPoseCard = nullptr;      ///< 工位卡②空间与公差
    QFormLayout* m_stationPoseForm = nullptr;    ///< 卡②行宿主
    QGroupBox* m_stationDofCard = nullptr;       ///< 工位卡③自由度约束（矩阵）
    QFormLayout* m_stationDofForm = nullptr;  ///< 卡③行宿主（逐行＝标签＋分段对）
    QGroupBox* m_stationSegmentCard = nullptr;   ///< 工位卡④动作阶段（呈现面）
    QFormLayout* m_stationSegmentForm = nullptr; ///< 卡④行宿主
    QGroupBox* m_stationOrientCard = nullptr;    ///< 工位卡⑤姿态规则（词表行）
    QFormLayout* m_stationOrientForm = nullptr;  ///< 卡⑤行宿主
    QGroupBox* m_regionBasicCard = nullptr;      ///< 区域卡①基础属性
    QFormLayout* m_regionBasicForm = nullptr;    ///< 区域卡①行宿主
    QGroupBox* m_regionBoxCard = nullptr;        ///< 区域卡②空间包围盒（复合行）
    QVBoxLayout* m_regionBoxLay = nullptr;    ///< 卡②复合行宿主
    QGroupBox* m_regionSamplingCard = nullptr;   ///< 区域卡③采样与达标
    QVBoxLayout* m_regionSamplingLay = nullptr;  ///< 卡③宿主（滑块/计数复合行）
    QGroupBox* m_regionAdvancedCard = nullptr;   ///< 区域卡④高级参数（默认折叠）
    QWidget* m_regionAdvancedBody = nullptr;  ///< 折叠体（toggle 切换可见性）
    QFormLayout* m_regionAdvancedForm = nullptr; ///< 折叠体行宿主

    /// 工位值提交（复合/二态控件共用轨——队列化出信号处理器：提交链同步
    /// 刷新重建控件树，槽内直执＝发射控件被删 use-after-free——返工修复）。
    void submitStationValue(const std::string& key, const QString& text);
    void submitStationValueNow(const std::string& key, const QString& text);
    /// 工位二态提交（applyStationToggleEdit——启用开关/自由度矩阵共用）。
    void submitStationToggle(const std::string& key, bool on);
    void submitStationToggleNow(const std::string& key, bool on);
    /// 工位枚举提交（applyStationEnumEdit——等级 QComboBox；UI-T37 返工）。
    void submitStationEnumValue(const std::string& key, const QString& text);
    void submitStationEnumValueNow(const std::string& key, const QString& text);
    /// 区域值提交（onRegionFieldEditingFinished 的 sender 无关形）。
    void submitRegionValue(const std::string& key, const QString& text);
    void submitRegionValueNow(const std::string& key, const QString& text);
    /// 区域二态提交（applyRegionToggleEdit）。
    void submitRegionToggle(const std::string& key, bool on);
    void submitRegionToggleNow(const std::string& key, bool on);
    /// 区域采样计数提交（applyRegionSamplingCountsEdit——counts 三元组整数
    /// 回填；formatCounts 的逆变换，域规范化/裁决仍走 submitEntryEdit）。
    void submitRegionCounts(const std::array<std::uint32_t, 3>& counts);
    void submitRegionCountsNow(const std::array<std::uint32_t, 3>& counts);
    /// 工位行键→所属卡片表单路由（词表分派——键族集合封闭）。
    QFormLayout* stationFormForKey(const std::string& key) const;
    /// 区域/工况表行同步（信号屏蔽——树点击驱动检查器联动用；返工③）。
    void syncRegionTableSelection(const core::ObjectId& id);
    void syncConditionTableSelection(const core::ObjectId& id);
    /// 区域/工况枚举提交（applyRegionEnumEdit/applyConditionEnumEdit——
    /// 等级与启用 QComboBox 轨；返工②）。
    void submitRegionEnumValue(const std::string& key, const QString& text);
    void submitRegionEnumValueNow(const std::string& key, const QString& text);
    void submitConditionEnumValue(const std::string& key, const QString& text);
    void submitConditionEnumValueNow(const std::string& key, const QString& text);
    /// 状态文本统一出口（横幅可见性随文本——R2 警示条）。
    void showStatusLine(const QString& text);

    // ---- 对象生命周期（UI-T30 B1——工位/区域/工况新增/复制/删除）----
    /**
     * @brief 生命周期工具行（每页签页头下方一行——新增/复制/删除三键）。
     *
     * 按钮经 lambda 捕获集合种类分派到三个结构槽；objectName 锚
     * `ird_req_<action>_<key>`（action=add|duplicate|remove；key=points|
     * regions|conditions——gui_test findChild 定位面）。
     */
    QWidget* makeLifecycleBar(QWidget* parent, const char* key,
                              WorkingSetMember member, const QString& noun);
    /// 新增槽：构造合法起步条目（域夹具同款默认值）＋自动名防撞 →
    /// applyEdit → 接受＝sink 链刷新＋选中新条目；拒绝＝状态行就地错误。
    void onAddEntry(WorkingSetMember member);
    /// 复制槽：当前选中条目深拷贝＋新 id＋『 副本』名防撞 → applyEdit →
    /// 选中副本；溯源字段保持源值（复件非伪造模板物——诚实登记）。
    void onDuplicateEntry(WorkingSetMember member);
    /// 删除槽：选中条目 removeEdit → 接受后锚回落（同集合规范序次条，
    /// 空集合＝清空选中）；拒绝（含被必验引用等域拒绝面）＝就地错误。
    void onRemoveEntry(WorkingSetMember member);
    /// 集合内未占用的条目名（base 后缀递增——I-REQ-3 拒绝面前的第一道
    /// 防线；撞名仍由域侧裁决，双保险）。
    std::string uniqueEntryName(const RequirementWorkingSet& ws,
                                WorkingSetMember member, const std::string& base);
    /// 结构编辑统一提交轨（applyEdit → 接受走 onEditApplied sink 链——
    /// UI-T29 组合子随之触发就绪重估；拒绝就地报错。返回接受与否）。
    bool submitStructuralEdit(const RequirementEdit& edit);
    std::vector<ui::CommandDescriptor> m_commands;  ///< 域命令目录（§9.8 九条——装配数据）
    CommandSubmitFn m_commandSubmit;          ///< 命令提交出口（装配层注入；空＝按钮禁用）
    CommandAvailabilityFn m_commandAvailability; ///< 注册表可用性查询
    EditTargetProvider m_editTarget;          ///< 编辑目标提供器（装配层注入；空＝编辑禁用）
    PostEditAction m_postEditAction;          ///< 编辑后动作（UI-T29——就绪重算钩子；可空）
    RegionPreviewSink m_regionPreview;        ///< 区域三维预览出口（装配层注入；空＝文本摘要）
    StationMarkersSink m_stationMarkersSink;  ///< 工位标记出口（UI-T33——装配层注入；空＝无投递）
    ConditionWizardFn m_conditionWizard;      ///< 工况向导缝（空＝内建对话框——R2）
    bool m_writable = true;                   ///< 会话可写性（L-R12 门控输入）
    bool m_dirty = false;                     ///< 会话脏标记（PM-04/PM-11 呈现半区）
    bool m_sessionDetached = false;           ///< 会话脱离旗标（UI-T39——resetFor-
                                              ///< SessionDetached 置位、refreshPanel
                                              ///< 复位；无会话判定的双保险半区——
                                              ///< 编辑目标提供器在测试缝/装配间隙
                                              ///< 可能仍返回旧编辑器）

    // ---- 最近一次刷新的选中锚（零数据缓存——只存"选中锚"，内容一律从
    //      调用方入参现取；refreshPanel 结束后本类不持有 ws/report）----
    std::optional<core::ObjectId> m_lastSelected;  ///< 检查器当前选中锚（跨刷新保持）

    // ---- 控件（raw 指针＝Qt 父子所有权——构造期挂树，随 Qt 析构）----
    QTreeWidget* m_tree = nullptr;              ///< 左栏需求树（层级＝需求工程根→
                                                ///< 四分组→条目；末隐藏列＝锚规范文本）
    QTabWidget* m_pages = nullptr;              ///< 右栏四页面容器（工位/区域/工况/校验）
    std::vector<StationFieldRow> m_stationRows; ///< 当前工位行（含字段键——提交时的回填参数）
    QTreeWidget* m_regionTable = nullptr;       ///< 区域表（L-R1 行选中）
    QLabel* m_regionPreviewLabel = nullptr;     ///< 区域预览摘要（几何出口注入时同步投递 View3D）
    QTreeWidget* m_conditionTable = nullptr;    ///< 工况表（L-R1 行选中；末列＝是否必验 Tag）
    QFormLayout* m_conditionForm = nullptr;     ///< 工况检查器表单（详情卡内行宿主）
    QGroupBox* m_conditionDetailCard = nullptr; ///< 工况卡『工况详情与节拍配置』（返工④——
                                                ///< 结构同工位/区域卡；必验清单预览表
                                                ///< 并入单表是否必验列后撤销）
    QLabel* m_validationCounts = nullptr;       ///< 校验分层计数行（Blocking/Warning 汇总）
    // 页签状态行（UI-T26——页头『〈页名〉：〈对象名|未选择对象〉』）。
    QLabel* m_stationHeader = nullptr;          ///< 工位页头（随树选中刷新）
    QLabel* m_regionHeader = nullptr;           ///< 区域页头（随树选中刷新）
    QLabel* m_conditionHeader = nullptr;        ///< 工况页头（随树选中刷新）
    QLabel* m_validationHeader = nullptr;       ///< 校验页头（诚实静态『尚未执行』）
    QTreeWidget* m_validationLayers = nullptr;  ///< R0~R9 分层结果行
    QTreeWidget* m_validationItems = nullptr;   ///< 逐项行（可点击——L-R1 反向定位跳转）
    QLabel* m_validationNotes = nullptr;        ///< 预览/正式语义说明（REQ-06 固定文案）
    // ---- UI-T34 E 批次：逐项过滤（级别/层/稳定码）＋报告缓存 ----
    QComboBox* m_validationLevelFilter = nullptr;  ///< 级别过滤（4 值封闭词表）
    QComboBox* m_validationLayerFilter = nullptr;  ///< 层过滤（全部＋R0~R9）
    QLineEdit* m_validationCodeFilter = nullptr;   ///< 稳定码子串过滤
    std::optional<RequirementReadinessReport> m_lastReadiness;  ///< 最近报告缓存（过滤重投影——值语义）
    std::vector<QPushButton*> m_commandButtons; ///< 域命令按钮（与 m_commands 下标对齐；
                                                ///< 返工⑤导入两槽位＝空指针，动作面在
                                                ///< m_importActions；UI-T39 分组后低频
                                                ///< 六槽位＝空指针，动作面在 m_moreActions）
    std::vector<QAction*> m_importActions;      ///< 导入菜单动作（与 m_commands 下标对齐；
                                                ///< 非导入槽位＝空——返工⑤下拉整合）
    QPushButton* m_importDropdownButton = nullptr; ///< 『导入 ▾』下拉宿主（返工⑤——
                                                ///< CSV/JSON 两键呈现层整合）
    std::vector<QAction*> m_moreActions;        ///< 更多操作菜单动作（UI-T39——与
                                                ///< m_commands 下标对齐；捕获/模板/
                                                ///< 派生四组六条低频命令入下拉，
                                                ///< 非槽位＝空）
    QPushButton* m_moreMenuButton = nullptr;    ///< 『更多操作 ▾』下拉宿主（UI-T39——
                                                ///< 命令条"高频主排＋更多操作"分组的
                                                ///< 下拉承载；使能＝子项可用性之或）
    bool m_regionColumnsSized = false;    ///< 区域表摘要列首刷成形旗标（UI-T39 列宽
                                          ///< 策略——用户手调列宽跨刷新保持）
    bool m_conditionColumnsSized = false; ///< 工况表摘要列首刷成形旗标（同上）
    QPushButton* m_draftUndoButton = nullptr;   ///< 草稿级撤销（L-R4 两级之一——独立控件）
    QPushButton* m_draftRedoButton = nullptr;   ///< 草稿级重做（同上）
    QPushButton* m_projectUndoButton = nullptr; ///< 项目级撤销（转发面——与草稿级不混用）
    QLabel* m_statusLine = nullptr;             ///< 就地错误/警告/摘要行（横幅内消息标签——UX-03/07）
    // ---- 返工⑤：空态与页内选择面 ----
    QScrollArea* m_stationScroll = nullptr;     ///< 工位属性区滚动容器（空态隐藏）
    QScrollArea* m_regionScroll = nullptr;      ///< 区域属性区滚动容器（同上）
    QScrollArea* m_conditionScroll = nullptr;   ///< 工况属性区滚动容器（同上）
    QLabel* m_stationEmptyHint = nullptr;       ///< 工位空态提示（与滚动容器互斥显隐）
    QLabel* m_regionEmptyHint = nullptr;        ///< 区域空态提示（同上）
    QLabel* m_conditionEmptyHint = nullptr;     ///< 工况空态提示（同上）
    std::optional<core::ObjectId> m_stationSelectedId;    ///< 工位页当前选中锚（页内置灰判定源）
    std::optional<core::ObjectId> m_regionSelectedId;     ///< 区域页当前选中锚（同上）
    std::optional<core::ObjectId> m_conditionSelectedId;  ///< 工况页当前选中锚（同上）
};

}  // namespace sdurws::ird::requirements

#endif  // IRD_REQUIREMENTS_PLUGIN_REQUIREMENTSPANELWIDGET_HPP
