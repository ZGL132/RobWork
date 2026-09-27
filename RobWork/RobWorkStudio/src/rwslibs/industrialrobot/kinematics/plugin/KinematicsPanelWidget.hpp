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

    /// 任务区刷新（L-K12——taskRowsProvider 现取；后台受理/完成回执后调用）。
    void refreshTaskArea();

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
    void onSolutionSelectionChanged();// L-K1 解表选中→检查器联动
    void onSolutionDoubleClicked(int row, int column); // L-K4 双击→会话姿态回写

private:
    // ---- 四面板构建（构造期一次——布局骨架；内容全走刷新）----
    QWidget* buildPosePane();       // 页①位姿指标（承接旧 Diagnose 页）
    QWidget* buildTaskPane();       // 页②任务点验证（承接旧 Task Points 页）
    QWidget* buildCoveragePane();   // 页③区域覆盖
    QWidget* buildResultsPane();    // 页④结果与可视化（承接旧 Visualization 页）

    // ---- 刷新半区（现取重投影——零缓存）----
    void refreshPosePane();         // 位姿指标行＋关节表（KIN-12 投影）
    void refreshResultsPane();      // 解表/统计/检查器
    void renderRows(QTableWidget* table, const std::vector<KinNamedValueRow>& rows);
    void renderSolutions(QTableWidget* table, const std::vector<KinSolutionRow>& rows);

    // ---- 会话态与服务缝（引用语义——权威归装配层）----
    KinPanelServices m_services;       ///< 服务缝聚合
    KinModuleSessionState& m_session;  ///< 会话态引用（非 owning）
    CommandTitleResolver m_titleResolver;  ///< 文案解析（可空＝键名原文）

    // ---- 页①控件（raw 指针＝Qt 父子所有权）----
    QTableWidget* m_poseTable = nullptr;    ///< 位姿指标行表
    QTableWidget* m_jointTable = nullptr;   ///< 关节表（显示单位投影 KIN-12）
    QLineEdit* m_targetX = nullptr;         ///< 求解目标位置编辑（m 显示制式）
    QLineEdit* m_targetY = nullptr;
    QLineEdit* m_targetZ = nullptr;
    QPushButton* m_solveButton = nullptr;   ///< Solve（L-K2）
    QTableWidget* m_inspectorTable = nullptr; ///< 候选检查器（健康摘要/关节）
    std::vector<QLineEdit*> m_jointEditors; ///< 关节表编辑行（求解目标参考构型）

    // ---- 页②控件 ----
    QTableWidget* m_taskPointTable = nullptr; ///< 任务点消费视图（只读——真值归 requirements）
    QPushButton* m_batchButton = nullptr;     ///< 批量分析提交（L-K3）

    // ---- 页③控件 ----
    QTableWidget* m_coverageTable = nullptr;  ///< 覆盖率结果表
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

    // ---- 容器与状态 ----
    QTabWidget* m_tabs = nullptr;  ///< 四面板 Tab 容器
    QLabel* m_statusLine = nullptr; ///< 状态行（非模态反馈——UX-03/07）
};

/**
 * @brief 求解配置高级面板（§9.8 面板表行 5——UX-04 高级参数收拢；
 *        独立挂位 PanelRegistration.advanced=true）。
 *
 * 数值字段编辑复用 ui 表单公共件（ParamEditModel＋createParamTablePanel
 * ——UI-T08 对端消费面）；初值策略为域枚举（QComboBox 自渲染——表单
 * 公共件只承载数值字段的宿主规则）；确认应用流＝applyConfigurationEdit
 * （L-K7——校验/提示/保存，零自动重算）。
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
     * @brief 析构（cpp 定义——m_paramModel/m_outlet 的 unique_ptr 成员
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
    /// 提示行刷新（确认应用后由 outlet 回调驱动）。
    void onApplyResult(const QString& statusText);

private:
    /// 域编辑出口（ui IFormEditOutlet 实现——ParamEditModel 确认移交面）。
    class ConfigEditOutlet;
    std::unique_ptr<ConfigEditOutlet> m_outlet;  ///< 编辑出口（确认应用→域半区）

    KinPanelServices m_services;       ///< 服务缝
    KinModuleSessionState& m_session;  ///< 会话态引用

    QComboBox* m_strategyBox = nullptr;   ///< 初值策略（三值词表——§5.3）
    QWidget* m_paramTable = nullptr;      ///< 表单公共件面板（createParamTablePanel 产物）
    QTableWidget* m_hintTable = nullptr;  ///< 依赖提示行（L-K7——纯提示）
    QTableWidget* m_policyTable = nullptr; ///< 策略只读摘要（无开关/无阈值——UX-08）
    QLabel* m_statusLine = nullptr;       ///< 状态行

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
