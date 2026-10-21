/**
 * @file   KinematicsPanelWidget.hpp
 * @brief  kinematics 域主面板（Qt Widgets，L4）——四面板合一 Tab 容器
 *         （位姿指标/任务点验证/区域覆盖/结果与可视化——卡 §9.8 面板表
 *         行 1~4）的薄装配。
 *
 * 设计依据：
 *   - units/kinematics.md §9.8（面板组成表行 1~4；线程与刷新约束——UI
 *     线程零计算、事件驱动刷新、面板不缓存权威结果）；§9.7.4 同案先例
 *     （modeling 面板"widget 只做投影结果的控件渲染、事件转接、命令
 *     转发——业务判定零入"）；
 *   - ARCH §3.3（业务域单元二分结构——面板只消费计算库公共接口＋ui
 *     端口，零计算逻辑与业务判定）；
 *   - 需求 UX-04/UX-05；任务契约 tasks/foundation/WP-15-T12.json
 *     acceptance 1/2。
 *
 * 背景说明（零计算逻辑的 widget 半区）：全部数值语义在零 Qt 呈现模型
 * （KinPanelModel/KinPanelFlows/KinPanelUnits——域设施消费面）内；widget
 * 只做：行集渲染、用户事件→编排函数转接、命令激活→提交出口转发。刷新
 * 一律事件驱动（refreshAll/refreshTaskArea 显式入口——无轮询定时器）。
 *
 * 线程约束：仅 UI 线程构造与访问（QWidget 固有约束＋§3.4）。
 */

#ifndef IRD_KINEMATICS_PLUGIN_KINEMATICSPANELWIDGET_HPP
#define IRD_KINEMATICS_PLUGIN_KINEMATICSPANELWIDGET_HPP

#include <functional>

#include <QComboBox>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QWidget>

#include <sdurws/ird/ui/UiTypes.hpp> // ui::CommandAvailability/CommandId
#include <sdurws/ird/ui/ICommandRegistry.hpp> // ui::CommandAvailability 定义

#include "KinPanelFlows.hpp"   // 编排函数（L-K2/3/6/9/10——同目录私有头）
#include "KinPanelModel.hpp"   // 行集投影（零 Qt 半区）
#include "KinPanelTypes.hpp"   // 会话态/服务缝

class QLineEdit;
class QVBoxLayout;

namespace sdurws {
namespace ird {
namespace ui {
class ParamEditModel;  // 前置声明（编辑会话模型——unique_ptr 成员；断环：
                       // FormEditCommon.hpp 完整类型在 cpp 消费）
}  // namespace ui
}  // namespace ird
}  // namespace sdurws

namespace sdurws {
namespace ird {
namespace kinematics {

/**
 * @brief kinematics 域主面板 widget（四面板合一——挂位于 ui 中央区
 *        StageId=Kinematics 位；求解配置高级面板独立成类，见
 *        KinematicsConfigPanel）。
 *
 * 所有权/生命周期：由装配层（宿主/harness）创建与持有（§10.9 面板工厂
 * 产物——ui 线程创建）；本类持有服务缝与会话态引用（非 owning——权威
 * 归装配层），不持有任何结果副本（投影现取——acceptance 2）。
 */
class KinematicsPanelWidget final : public QWidget {
    Q_OBJECT  // AUTOMOC（仅插件目标开启——DTB §5.1 v0.17 行口径）

public:
    /// 命令标题文案解析器（titleKey→工程用语——宿主接 ui::resolveText；
    /// modeling 先例 CommandTitleResolver 同形态）。
    using CommandTitleResolver = std::function<QString(const std::string& titleKey)>;
    using CommandAvailabilityFn = std::function<ui::CommandAvailability(const std::string&)>;

    /**
     * @brief 构造四面板主面板（UI 线程）。
     *
     * @param services [in] 服务缝聚合（值拷贝——缝本身非 owning）
     * @param session  [in,out] 会话态引用（权威载体归装配层/module）
     * @param parent   [in] Qt 父对象（可空——装配层自持）
     */
    KinematicsPanelWidget(KinPanelServices services,
                          KinModuleSessionState& session,
                          QWidget* parent = nullptr);

    /// 注入命令标题文案解析器（装配期；不注入＝呈现键名原文——不虚构文案）。
    void setCommandTitleResolver(CommandTitleResolver resolver);
    void setCommandAvailability(CommandAvailabilityFn availability);

    /// 全面板刷新（事件驱动出口——四面板全部现取重投影，零缓存）。
    void refreshAll();

    /// 会话可写性切换（L-K11——写类按钮禁用/恢复；触发光靠刷新）。
    void setWritable(bool writable);

    /**
     * @brief 显示单位切换（L-K8 唯一入口——设置会话投影后全面板重投影：
     *        零重算/零修订/结果不动，KIN-12/V-17）。
     *
     * @param units [in] 新显示单位投影（nullopt＝SI 直显制式）
     */
    void setDisplayUnits(const std::optional<DisplayUnitProjection>& units);

    /// 任务区刷新（L-K12——taskPointsProvider 现取；后台受理/完成回执后调用。
    /// 注意：任务九态/进度呈现归 ui 任务 Dock——本面板不渲染 taskRows 缝
    /// （审查 20261021 G10 注释失实修正——呈现边界如实登记）。
    void refreshTaskArea();

    /**
     * @brief 覆盖结果行渲染（审查 20261021 G1 修复——区域覆盖面板呈现
     *        半区接通；WP-15-T20 第三批）。
     *
     * 数据链：后台覆盖完成 → 装配门面 applyCoverageResult（锚核对＋
     * KinChannelCoverageResult 聚合→行翻译）→ 模块转发 → 本槽渲染
     * m_coverageTable（覆盖轴/计数比/标识三列）。位置/姿态双行；
     * zeroSamples＝"零样本"（比率不存在——不虚构 0%）；degraded＝
     * "数据不足"（降级如实——KIN-04 R8）。行集为调用方翻译产物——本
     * 函数零数值计算（widget 零计算逻辑纪律不变）。
     *
     * @param rows [in] 覆盖行集（空＝清表——对账失效/无结果的诚实空态）
     */
    void showCoverageRows(std::vector<KinCoverageRow> rows);

    /**
     * @brief 任务点定位/高亮（WP-15-T18 下行联动呈现半区——SelectionAdapter
     *        消费共享选择服务后的执行器落点；acceptance 4 v1 链路"任务点
     *        选择→结果面板高亮"的落点）。
     *
     * 语义：命中本域任务点消费视图的行锚（KinTaskPointRow.pointOid）＝切
     * 到任务点页并置当前行（零写回——本表只读）；未命中/nullopt＝清除高
     * 亮不伪造定位（requirements focusObject 同案）。
     *
     * @param oid [in] 目标任务点对象身份（requirements 域对象——消费面；
     *            nullopt＝清除高亮）
     */
    void focusTaskPoint(const std::optional<core::ObjectId>& oid);

    /// 最近状态行文本（测试/装配层读取面——非模态反馈的模型半区）。
    QString lastStatusText() const;

public Q_SLOTS:
    /// 状态行设置（编排函数返回值/宿主反馈的统一呈现入口——非模态）。
    void showStatusText(const QString& text);
private Q_SLOTS:
    // ---- 面板事件→编排函数转接（零业务逻辑——全部委托零 Qt 半区）----
    void onSolveClicked();            // L-K2 单点求解
    void onBatchClicked();            // L-K3 批量验证提交
    void onCoverageRunClicked();      // L-K6 覆盖运行提交
    void onResetHomeClicked();        // L-K4 复位 Home（会话命令）
    void onSetDefaultTcpClicked();    // L-K9 设默认 TCP
    void onSetDefaultDeviceClicked(); // L-K9 设默认设备
    void onExportJsonClicked();       // L-K10 导出 JSON 副本
    void onExportCsvClicked();        // L-K10 导出 CSV 副本
    void onRecomputeClicked();        // KIN-P4 复算（原任务定义×当前快照重绑定）
    void onSolutionSelectionChanged();// L-K1 解表选中→检查器联动
    void onSolutionDoubleClicked(int row, int column); // L-K4 双击→会话姿态回写
    // ---- 审查 20261021 修复批（WP-15-T20 第三批）----
    void onBackfillTcpClicked();      // G4：L-K5 以当前 TCP 为目标（位姿回填）
    void onDisplayUnitChanged(int index); // G5：L-K8 显示单位切换生产入口
    // ---- 探索式采样组（★ KIN-R2 任务3/KIN-09——结果与可视化面板）----
    void onWsModeChanged(int index);      // 采样模式切换（参数字段使能面）
    void onWsRunClicked();                // 运行采样（内联编排）
    void onWsExportCsvClicked();          // 导出点云 CSV（契约 v2 含 sampling-mode 列）
    void onWsWritebackClicked();          // 样本构型回写会话（KIN-06 零修订）
    // ---- 投影视图（★ KIN-R2 任务6）----
    void onWsProjectionPlaneChanged(int index); // 投影面切换（画布重绘）
    void onWsExportPngClicked();          // 导出投影视图 PNG（画布字节经写出缝）

private:
    // ---- 四面板构建（构造期一次——布局骨架；内容全走刷新）----
    QWidget* buildPosePane();       // 页①位姿指标（承接旧 Diagnose 页）
    QWidget* buildTaskPane();       // 页②任务点验证（承接旧 Task Points 页）
    QWidget* buildCoveragePane();   // 页③区域覆盖
    QWidget* buildResultsPane();    // 页④结果与可视化（承接旧 Visualization 页）

    // ---- 刷新半区（现取重投影——零缓存）----
    void refreshPosePane();         // 位姿指标行＋关节表（KIN-12 投影）
    void refreshResultsPane();      // 解表/统计/检查器
    /// 采样组参数字段使能面（★ KIN-R2 任务3——随模式切换启用对应参数：
    /// 区域盒字段/区域位置采样字段/关节随机/关节网格互斥启用）。
    void refreshWsParamVisibility();
    /// 采样组摘要表现投（lastWorkspaceRun 现取——模式 token/状态计数/
    /// 取消标记零缓存；无产物＝空表）。
    void refreshWsSummary();
    /// 投影散点画布重绘（★ KIN-R2 任务6——lastWorkspaceRun→
    /// buildWorkspacePointCloud→projectWorkspacePoints→QPainter 离屏
    /// 绘制；无产物＝占位文本画布。绘制定位（数据→像素映射）为呈现
    /// 几何，业务语义（状态类别色/图例计数）直投域值，零业务判定）。
    void refreshWsProjectionCanvas();
    /// 目标输入行单位标注重建（KIN-P1 A07——标签/占位随显示制式＋基座系
    /// 参考系标识；refreshAll 与 L-K8 切换路径共同出口）。
    void refreshTargetUnitPresentation();
    /// 目标位姿六字段读取（★ KIN-R2 任务4——位置 XYZ＋姿态 RPY 编辑值 →
    /// 基座系 SI 目标位姿；显示制式逆换算唯一出口 resolveKinDisplayToSi/
    /// resolveKinAngleDisplayToSi。解析失败/非有限＝就地反馈并返回
    /// nullopt——调用方停在原提交前，不向求解器传非法目标）。
    std::optional<rw::math::Transform3D<double>> readTargetPoseFromEditors(
        const QString& errorPrefix);
    void renderRows(QTableWidget* table, const std::vector<KinNamedValueRow>& rows);
    void renderSolutions(QTableWidget* table, const std::vector<KinSolutionRow>& rows);

    // ---- 会话态与服务缝（引用语义——权威归装配层）----
    KinPanelServices m_services;       ///< 服务缝聚合
    KinModuleSessionState& m_session;  ///< 会话态引用（非 owning）
    CommandTitleResolver m_titleResolver;  ///< 文案解析（可空＝键名原文）
    CommandAvailabilityFn m_commandAvailability; ///< 注册表可用性查询

    // ---- 非阻塞单点求解（审查 G3 修复——WP-15-T20 第三批；插件零自建
    // 线程红线〔acceptance 2〕的通道化落位：异步能力经 inlineSolveAsync
    // 装配缝由 ui 执行器线程设施承载）----

    /**
     * @brief 异步缝在场的单点求解（非阻塞形态）：请求经 inlineSolveAsync
     *        装配缝提交（job 在装配层工作线程恰执行一次；IIkSolver 纯函
     *        数线程安全——§9.2），UI 线程立即返回（界面持续响应——
     *        NFR-PERF-01）；完成回调（UI 线程）经 finishSinglePointSolve
     *        与内联路径同一写点入会话态（零双份计算——修复前同步跑完才
     *        事后判断超时，超时场景 UI 冻结全程＋同一请求后台重算）。在
     *        途期间求解按钮禁用（m_solveInFlight——防重复提交）；缝缺席
     *        ＝降级同步内联（runSinglePointSolve 原径——诚实边界，其内
     *        部超预算转后台语义不变）。
     *
     * @param targetInBase [in] 求解目标位姿（基座系 {B}，SI——编辑器逆换算产物）
     * @param request      [in] 装配完成的求解请求其余字段（零再加工）
     */
    void runSolveNonBlocking(const rw::math::Transform3D<double>& targetInBase,
                             const IkRequest& request);

    // ---- 页①控件（raw 指针＝Qt 父子所有权）----
    QLabel* m_frameLabel = nullptr;         ///< 参考系/TCP 身份标识行（★ KIN-R2 任务4——坐标系标识）
    QTableWidget* m_poseTable = nullptr;    ///< 位姿指标行表
    QTableWidget* m_jointTable = nullptr;   ///< 关节表（显示单位投影 KIN-12）
    QLineEdit* m_targetX = nullptr;         ///< 求解目标位置编辑（显示制式数值——提交逆换算回 SI，A07）
    QLineEdit* m_targetY = nullptr;
    QLineEdit* m_targetZ = nullptr;
    QLabel* m_targetXLabel = nullptr;       ///< 目标 X 行标签（单位符号随显示制式重建——A07 缺陷面①）
    QLabel* m_targetYLabel = nullptr;       ///< 同上（Y 轴）
    QLabel* m_targetZLabel = nullptr;       ///< 同上（Z 轴）
    QLineEdit* m_targetRoll = nullptr;      ///< 目标姿态 RPY-Roll 编辑（★ KIN-R2 任务4——显示制式角度，提交逆换算回 SI rad）
    QLineEdit* m_targetPitch = nullptr;     ///< 目标姿态 RPY-Pitch 编辑（同上）
    QLineEdit* m_targetYaw = nullptr;       ///< 目标姿态 RPY-Yaw 编辑（同上）
    QLabel* m_targetRollLabel = nullptr;    ///< RPY-Roll 行标签（角度单位随显示制式重建）
    QLabel* m_targetPitchLabel = nullptr;   ///< RPY-Pitch 行标签（同上）
    QLabel* m_targetYawLabel = nullptr;     ///< RPY-Yaw 行标签（同上）
    QPushButton* m_solveButton = nullptr;   ///< Solve（L-K2）
    QPushButton* m_backfillTcpButton = nullptr; ///< 以当前 TCP 为目标（G4——L-K5 产品入口）
    QComboBox* m_lengthUnitBox = nullptr;   ///< 长度显示单位下拉（G5——L-K8 入口；m/cm/mm）
    QComboBox* m_angleUnitBox = nullptr;    ///< 角度显示单位下拉（G5——L-K8 入口；rad/deg）
    QTableWidget* m_inspectorTable = nullptr; ///< 候选检查器（健康摘要/关节）

    // ---- 页②控件 ----
    QTableWidget* m_taskPointTable = nullptr; ///< 任务点消费视图（只读——真值归 requirements）
    QPushButton* m_batchButton = nullptr;     ///< 批量分析提交（L-K3）

    // ---- 页③控件 ----
    QTableWidget* m_coverageTable = nullptr;  ///< 覆盖率结果表（showCoverageRows 填充——G1 修复前恒空）
    QPushButton* m_coverageRunButton = nullptr; ///< 覆盖运行（L-K6）

    // ---- 页④控件 ----
    QCheckBox* m_usableOnly = nullptr;      ///< 结果筛选：仅可用解（域谓词语义）
    QTableWidget* m_solutionTable = nullptr;  ///< 解表（稳定排序——零自排）
    QTableWidget* m_statTable = nullptr;      ///< 解统计行
    QPushButton* m_exportJsonButton = nullptr; ///< 导出 JSON（L-K10）
    QPushButton* m_exportCsvButton = nullptr;  ///< 导出 CSV（L-K10）
    QPushButton* m_resetHomeButton = nullptr;  ///< 复位 Home（L-K4）
    QPushButton* m_setTcpButton = nullptr;     ///< 设默认 TCP（L-K9）
    QPushButton* m_setDeviceButton = nullptr;  ///< 设默认设备（L-K9）
    QPushButton* m_recomputeButton = nullptr;  ///< 复算（KIN-P4——原任务定义×当前快照）

    // ---- 页④探索式采样组（★ KIN-R2 任务3/KIN-09——编辑入口＋运行＋
    // 导出＋回写；raw 指针＝Qt 父子所有权）----
    QComboBox* m_wsModeBox = nullptr;        ///< 采样模式（区域盒/关节随机/关节网格——域三值词表序）
    QComboBox* m_wsPositionMethodBox = nullptr; ///< 区域位置采样方法（随机/网格——仅区域模式启用）
    QLineEdit* m_wsRegionCX = nullptr;       ///< 区域盒中心 X（显示制式长度——提交逆换算 SI m）
    QLineEdit* m_wsRegionCY = nullptr;       ///< 区域盒中心 Y（同上）
    QLineEdit* m_wsRegionCZ = nullptr;       ///< 区域盒中心 Z（同上）
    QLineEdit* m_wsRegionSX = nullptr;       ///< 区域盒边长 X（同上；须 >0）
    QLineEdit* m_wsRegionSY = nullptr;       ///< 区域盒边长 Y（同上）
    QLineEdit* m_wsRegionSZ = nullptr;       ///< 区域盒边长 Z（同上）
    QLineEdit* m_wsRegionRandom = nullptr;   ///< 区域随机样本数（无量纲）
    QLineEdit* m_wsRegionGridX = nullptr;    ///< 区域网格 X 轴计数（无量纲）
    QLineEdit* m_wsRegionGridY = nullptr;    ///< 区域网格 Y 轴计数（无量纲）
    QLineEdit* m_wsRegionGridZ = nullptr;    ///< 区域网格 Z 轴计数（无量纲）
    QLineEdit* m_wsJointRandom = nullptr;    ///< 关节随机样本数（无量纲）
    QLineEdit* m_wsJointGrid = nullptr;      ///< 关节网格逐轴取点数（无量纲——统一值施加全轴）
    QPushButton* m_wsRunButton = nullptr;    ///< 运行采样（内联编排——runWorkspaceSamplingFlow）
    QPushButton* m_wsExportCsvButton = nullptr; ///< 导出点云 CSV（exportWorkspacePointCloudCsv）
    QLineEdit* m_wsSampleIndex = nullptr;    ///< 回写样本序输入（results 全局序）
    QPushButton* m_wsWritebackButton = nullptr; ///< 样本构型回写会话（writebackWorkspacePoint）
    QTableWidget* m_wsSummaryTable = nullptr;   ///< 采样摘要表（lastWorkspaceRun 现投）
    KinInlineSamplingContext m_wsContext;   ///< 内联采样宿主上下文（无取消/进度设施——诚实边界）

    // ---- 页④投影视图（★ KIN-R2 任务6——2D 正交投影散点画布＋PNG 导出
    // 的最小可用入口；三维 RWStudioView3D 接入成本登记后续批次）----
    QComboBox* m_wsProjectionPlaneBox = nullptr; ///< 投影面（XY/XZ/YZ——域三值词表序）
    QLabel* m_wsCanvasLabel = nullptr;       ///< 散点画布（QPixmap 载体——离屏 QPainter 绘制）
    QPushButton* m_wsExportPngButton = nullptr; ///< 导出投影视图 PNG（exportWorkspaceProjectionPng）

    // ---- 容器与状态 ----
    QTabWidget* m_tabs = nullptr;  ///< 四面板 Tab 容器
    QLabel* m_statusLine = nullptr; ///< 状态行（非模态反馈——UX-03/07）
    bool m_solveInFlight = false;  ///< 异步求解在途（G3——防重复提交；完成回调清位＋刷新）
};

/**
 * @brief 求解配置高级面板（§9.8 面板表行 5——UX-04 高级参数收拢；
 *        独立挂位 PanelRegistration.advanced=true）。
 *
 * 数值字段编辑复用 ui 表单公共件（ParamEditModel＋createParamTablePanel
 * ——UI-T08 对端消费面）；初值策略为域枚举（QComboBox 自渲染——表单
 * 公共件只承载数值字段的宿主规则）；整体确认覆盖数值和策略，确认快照
 * 过期则零写入；applyConfigurationEdit保存成功后才前移基线，失败保留
 * 草稿供重试（WP-15-T20 F04）。面板及控件由Qt父子链持有，session由
 * 宿主拥有且须比面板长寿；非线程安全，仅UI线程访问，零自动重算。
 */
class KinematicsConfigPanel final : public QWidget {
    Q_OBJECT

public:
    /**
     * @brief 构造高级面板（UI 线程）。
     *
     * @param services [in] 服务缝（configPersist——保存缝可空＝如实禁用）
     * @param session  [in,out] 会话态（savedConfig 基线）
     * @param parent   [in] Qt 父对象
     */
    KinematicsConfigPanel(KinPanelServices services,
                          KinModuleSessionState& session,
                          QWidget* parent = nullptr);

    /**
     * @brief 析构（cpp 定义——m_paramModel 的 unique_ptr 成员
     *        析构需要完整类型；头内仅前向声明即断环）。
     */
    ~KinematicsConfigPanel() override;

    /// 从会话态重投（基线/策略/提示行现取——事件驱动刷新出口）。
    void refreshAll();

    /// 最近状态行文本（测试读取面）。
    QString lastStatusText() const;

public Q_SLOTS:
    /// 状态行设置（确认应用结果/保存反馈的统一呈现入口——非模态）。
    void showStatusText(const QString& text);

private Q_SLOTS:
    /// 初值策略切换（域枚举字段——表单公共件数值半区之外的域自渲染）。
    void onStrategyChanged(int index);
    /// @brief 准备整个配置的非模态确认；非法输入零保存并定位首错。
    void prepareConfigurationSave();
    /// @brief 确认冻结的修改集；草稿或权威基线变化即拒绝，保存失败保留草稿。
    void confirmConfigurationSave();

private:
    /**
     * @brief 读取数值和策略的完整修改集；数值为SI（m/rad/无量纲）。
     * @return 注册序数值字段后接策略0/1/2；未修改返回空，不更新任何基线。
     * 仅UI线程；错误状态由调用者先行检查，不把错误输入降级为旧合法值保存。
     */
    std::vector<std::pair<std::string, double>> configurationChanges() const;

    KinPanelServices m_services;       ///< 服务缝
    KinModuleSessionState& m_session;  ///< 会话态引用

    QComboBox* m_strategyBox = nullptr;   ///< 初值策略（三值词表——§5.3）
    QWidget* m_paramTable = nullptr;      ///< 表单公共件面板（createParamTablePanel 产物）
    QTableWidget* m_hintTable = nullptr;  ///< 依赖提示行（L-K7——纯提示）
    QTableWidget* m_policyTable = nullptr; ///< 策略只读摘要（无开关/无阈值——UX-08）
    QLabel* m_statusLine = nullptr;       ///< 状态行
    QWidget* m_confirmRegion = nullptr; ///< 整体确认区，Qt父子链持有
    QLabel* m_confirmText = nullptr; ///< 同一显示单位下的old→new明细
    /// 待确认内容仅为一次交互快照，不是配置权威；保存成功才前移session基线。
    std::optional<AnalysisConfiguration> m_confirmationBaseline;
    std::vector<std::pair<std::string, double>> m_confirmationChanges;

    /// 编辑会话模型（ui 表单公共件——字段注册/基线/暂存/确认规则承载；
    /// unique_ptr 成员——完整类型在 cpp 消费，头内前置声明）。
    std::unique_ptr<ui::ParamEditModel> m_paramModel;
    /// 策略草稿值（切换即暂存——保存经表单确认流，L-K7 不自动保存）。
    InitialValueStrategy m_draftStrategy = InitialValueStrategy::JointGrid;
};

}  // namespace kinematics
}  // namespace ird
}  // namespace sdurws

#endif  // IRD_KINEMATICS_PLUGIN_KINEMATICSPANELWIDGET_HPP
