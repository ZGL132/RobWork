/**
 * @file   KinematicsPanelWidget.cpp
 * @brief  kinematics 域主面板与求解配置高级面板的实现（头文件全部落点）。
 *
 * 设计依据：KinematicsPanelWidget.hpp 文件头；本 TU 零业务判定——按钮
 * 转接零 Qt 编排函数、表格渲染零 Qt 行集（acceptance 2 的 widget 半区）。
 */

#include "KinematicsPanelWidget.hpp"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QVBoxLayout>

#include <memory>
#include <utility>
#include <vector>

#include <sdurws/ird/core/Units.hpp>               // UnitToken::find（字段量纲锚）
#include <sdurws/ird/ui/FormEditCommon.hpp>        // 表单公共件（UI-T08 对端——参数表面板）
#include <sdurws/ird/ui/UiText.hpp>                // 文案键解析（UI-T25——迁移标记标签挂键；
                                                   // 构造期命令标题 resolver 尚未注入，此处直读
                                                   // UiText 静态表与 requirements 面板先例同款）
#include "KinPanelCommandCatalog.hpp"              // 命令目录（按钮词形与只读门控）

namespace sdurws {
namespace ird {
namespace kinematics {
namespace {

/// 表格便捷构建：只读行表（列头给定——零编辑语义；widget 层唯一重复处）。
QTableWidget* makeTable(const QStringList& headers, QWidget* parent)
{
    auto* table = new QTableWidget(0, headers.size(), parent);
    table->setHorizontalHeaderLabels(headers);
    table->horizontalHeader()->setStretchLastSection(true);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);  // 呈现零编辑
    table->verticalHeader()->setVisible(false);
    return table;
}

/// 填充"数值文本"单列表（状态行/统计类——零数值处理）。
void fillSingleColumn(QTableWidget* table, const QStringList& texts)
{
    table->setRowCount(static_cast<int>(texts.size()));
    for (int i = 0; i < texts.size(); ++i) {
        table->setItem(i, 0, new QTableWidgetItem(texts[i]));
    }
}

}  // namespace

// =====================================================================
// KinematicsPanelWidget——四面板合一主面板
// =====================================================================

KinematicsPanelWidget::KinematicsPanelWidget(KinPanelServices services,
                                             KinModuleSessionState& session,
                                             QWidget* parent)
    : QWidget(parent)
    , m_services(std::move(services))
    , m_session(session)
{
    // 四面板 Tab 容器（§9.8 面板表行 1~4 的落位载体——页序＝表行序，
    // 确定性登记序）。
    auto* layout = new QVBoxLayout(this);

    // 自持导航迁移状态标记（WP-15-T18——B1-SPEC §5.2 迁移期双形态并存；
    // objectName 供 GUI 验证定位，requirements 先例同款机制）。v1 措辞
    // 如实（O-44 裁决）：共享工业项目树是业务主导航（D3），本域树/页面
    // 接入面已注册但 v1 无本域树对象〔恒空集供给〕，本面板自持导航因此
    // **保留可用**、不作"已迁移"虚标——退役归 WP-24-T09。
    // UI-T25 文案治理：呈现文本改经 UiText 键解析（panel.kinematics.
    // self-nav.note）——原字面量直出携带内部裁决/任务编号（DTB O-44、
    // WP-24-T09），属 UX-02 内部名泄漏（F-430/F-432 家族标签一族消账）；
    // 编号与裁决出处只留在本注释与设计文档，用户见工程化中文（两域措辞
    // 按各自迁移事实区分，本域不作"已迁移"虚标的诚实口径不变）。
    m_navDeprecationLabel = new QLabel(this);
    m_navDeprecationLabel->setObjectName("kinematicsNavDeprecationLabel");
    m_navDeprecationLabel->setWordWrap(true);
    m_navDeprecationLabel->setText(
        QString::fromStdString(
            ui::resolveText(ui::TextKey("panel.kinematics.self-nav.note"))));
    layout->addWidget(m_navDeprecationLabel);

    m_tabs = new QTabWidget(this);
    m_tabs->addTab(buildPosePane(), tr("位姿指标"));
    m_tabs->addTab(buildTaskPane(), tr("任务点验证"));
    m_tabs->addTab(buildCoveragePane(), tr("区域覆盖"));
    m_tabs->addTab(buildResultsPane(), tr("结果与可视化"));
    layout->addWidget(m_tabs);

    // 状态行（非模态反馈——UX-03/07：一切反馈就地，零模态对话框）。
    m_statusLine = new QLabel(tr("就绪"), this);
    layout->addWidget(m_statusLine);

    // 初刷（事件驱动出口的首调——现取重投影）。
    refreshAll();
}

void KinematicsPanelWidget::setCommandTitleResolver(CommandTitleResolver resolver)
{
    m_titleResolver = std::move(resolver);
}

void KinematicsPanelWidget::setCommandAvailability(CommandAvailabilityFn availability)
{
    m_commandAvailability = std::move(availability);
    refreshAll();
}

QString KinematicsPanelWidget::lastStatusText() const
{
    return m_statusLine != nullptr ? m_statusLine->text() : QString();
}

void KinematicsPanelWidget::showStatusText(const QString& text)
{
    if (m_statusLine != nullptr) {
        m_statusLine->setText(text);
    }
}

QWidget* KinematicsPanelWidget::buildPosePane()
{
    auto* pane = new QWidget(this);
    auto* layout = new QHBoxLayout(pane);

    // ---- 左半：位姿指标行表＋关节表（KIN-12 显示投影）。
    auto* left = new QVBoxLayout();
    m_poseTable = makeTable({tr("指标"), tr("值")}, pane);
    left->addWidget(m_poseTable);
    m_jointTable = makeTable({tr("关节"), tr("当前构型")}, pane);
    left->addWidget(m_jointTable);
    layout->addLayout(left);

    // ---- 右半：目标编辑＋Solve＋候选检查器。
    auto* right = new QVBoxLayout();
    auto* form = new QFormLayout();
    // 目标位置编辑（显示制式文本——SI 换算经表单公共件解析口径；此处为
    // 求解目标的轻量编辑位，单位投影文本由 L-K8 刷新统一回填）。
    m_targetX = new QLineEdit(pane);
    m_targetY = new QLineEdit(pane);
    m_targetZ = new QLineEdit(pane);
    m_targetX->setPlaceholderText(tr("例如 0.400（单位：m）"));
    m_targetY->setPlaceholderText(tr("例如 0.000（单位：m）"));
    m_targetZ->setPlaceholderText(tr("例如 0.300（单位：m）"));
    form->addRow(tr("目标 X（m）"), m_targetX);
    form->addRow(tr("目标 Y（m）"), m_targetY);
    form->addRow(tr("目标 Z（m）"), m_targetZ);
    layout->addLayout(form);

    m_solveButton = new QPushButton(tr("求解 IK"), pane);
    m_solveButton->setToolTip(tr("需要模型视图和逆运动学求解服务"));
    connect(m_solveButton, &QPushButton::clicked, this,
            &KinematicsPanelWidget::onSolveClicked);
    right->addWidget(m_solveButton);

    m_inspectorTable = makeTable({tr("检查器"), tr("值")}, pane);
    right->addWidget(m_inspectorTable);
    layout->addLayout(right);
    return pane;
}

QWidget* KinematicsPanelWidget::buildTaskPane()
{
    auto* pane = new QWidget(this);
    auto* layout = new QVBoxLayout(pane);
    // 任务点消费视图（只读——真值归 requirements，acceptance 4；本表
    // 无编辑触发、无写回路径）。
    m_taskPointTable = makeTable({tr("任务点"), tr("启用"), tr("结果"),
                                  tr("必验覆盖")}, pane);
    layout->addWidget(m_taskPointTable);
    m_batchButton = new QPushButton(tr("批量验证（后台）"), pane);
    connect(m_batchButton, &QPushButton::clicked, this,
            &KinematicsPanelWidget::onBatchClicked);
    layout->addWidget(m_batchButton);
    return pane;
}

QWidget* KinematicsPanelWidget::buildCoveragePane()
{
    auto* pane = new QWidget(this);
    auto* layout = new QVBoxLayout(pane);
    m_coverageTable = makeTable({tr("覆盖轴"), tr("计数比"), tr("标识")}, pane);
    layout->addWidget(m_coverageTable);
    m_coverageRunButton = new QPushButton(tr("运行覆盖评估（后台）"), pane);
    connect(m_coverageRunButton, &QPushButton::clicked, this,
            &KinematicsPanelWidget::onCoverageRunClicked);
    layout->addWidget(m_coverageRunButton);
    return pane;
}

QWidget* KinematicsPanelWidget::buildResultsPane()
{
    auto* pane = new QWidget(this);
    auto* layout = new QVBoxLayout(pane);

    // 筛选（仅可用解——域规范谓词语义的呈现开关；排序权威在解集视图）。
    m_usableOnly = new QCheckBox(tr("仅可用解（碰撞证据完备且无碰撞）"), pane);
    connect(m_usableOnly, &QCheckBox::toggled, this,
            [this](bool) { refreshResultsPane(); });  // 筛选切换→现取重投影
    layout->addWidget(m_usableOnly);

    m_solutionTable = makeTable({tr("rank"), tr("构型签名"), tr("q"),
                                 tr("最小裕量"), tr("条件数"), tr("残差"),
                                 tr("碰撞")}, pane);
    // L-K1 选中联动：解表选中→检查器；L-K4 双击→会话姿态回写（零修订，
    // KIN-06——双击只改会话姿态）。
    connect(m_solutionTable, &QTableWidget::itemSelectionChanged, this,
            &KinematicsPanelWidget::onSolutionSelectionChanged);
    connect(m_solutionTable, &QTableWidget::itemDoubleClicked, this,
            [this](QTableWidgetItem* item) {
                if (item != nullptr) {
                    onSolutionDoubleClicked(item->row(), item->column());
                }
            });
    layout->addWidget(m_solutionTable);

    m_statTable = makeTable({tr("统计"), tr("值")}, pane);
    layout->addWidget(m_statTable);

    // 命令行（导出/复位/设默认——全部经编排函数，零内联逻辑）。
    auto* buttons = new QHBoxLayout();
    m_exportJsonButton = new QPushButton(tr("导出 JSON"), pane);
    m_exportCsvButton = new QPushButton(tr("导出 CSV"), pane);
    m_resetHomeButton = new QPushButton(tr("复位 Home"), pane);
    m_setTcpButton = new QPushButton(tr("设默认 TCP"), pane);
    m_setDeviceButton = new QPushButton(tr("设默认设备"), pane);
    connect(m_exportJsonButton, &QPushButton::clicked, this,
            &KinematicsPanelWidget::onExportJsonClicked);
    connect(m_exportCsvButton, &QPushButton::clicked, this,
            &KinematicsPanelWidget::onExportCsvClicked);
    connect(m_resetHomeButton, &QPushButton::clicked, this,
            &KinematicsPanelWidget::onResetHomeClicked);
    connect(m_setTcpButton, &QPushButton::clicked, this,
            &KinematicsPanelWidget::onSetDefaultTcpClicked);
    connect(m_setDeviceButton, &QPushButton::clicked, this,
            &KinematicsPanelWidget::onSetDefaultDeviceClicked);
    for (QPushButton* b : {m_exportJsonButton, m_exportCsvButton, m_resetHomeButton,
                           m_setTcpButton, m_setDeviceButton}) {
        buttons->addWidget(b);
    }
    layout->addLayout(buttons);
    return pane;
}

void KinematicsPanelWidget::renderRows(QTableWidget* table,
                                       const std::vector<KinNamedValueRow>& rows)
{
    if (table == nullptr) {
        return;
    }
    table->setRowCount(static_cast<int>(rows.size()));
    for (std::size_t i = 0; i < rows.size(); ++i) {
        table->setItem(static_cast<int>(i), 0,
                       new QTableWidgetItem(QString::fromStdString(rows[i].label)));
        table->setItem(static_cast<int>(i), 1,
                       new QTableWidgetItem(QString::fromStdString(rows[i].displayText)));
    }
}

void KinematicsPanelWidget::renderSolutions(QTableWidget* table,
                                            const std::vector<KinSolutionRow>& rows)
{
    if (table == nullptr) {
        return;
    }
    table->setRowCount(static_cast<int>(rows.size()));
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const KinSolutionRow& r = rows[i];
        const auto rank = static_cast<int>(i);
        table->setItem(rank, 0, new QTableWidgetItem(QString::number(
                                     static_cast<qlonglong>(r.rank))));
        table->setItem(rank, 1, new QTableWidgetItem(
                                     QString::fromStdString(r.signature)));
        table->setItem(rank, 2, new QTableWidgetItem(
                                     QString::fromStdString(r.qText)));
        table->setItem(rank, 3, new QTableWidgetItem(
                                     QString::fromStdString(r.minJointMarginText)));
        table->setItem(rank, 4, new QTableWidgetItem(
                                     QString::fromStdString(r.conditionNumberText)));
        table->setItem(rank, 5, new QTableWidgetItem(
                                     QString::fromStdString(r.positionResidualText)));
        table->setItem(rank, 6, new QTableWidgetItem(
                                     QString::fromStdString(r.collisionToken)));
    }
}

void KinematicsPanelWidget::refreshAll()
{
    refreshPosePane();
    refreshResultsPane();
    refreshTaskArea();
    // 覆盖页：结果经 results 投影刷新（事件驱动）——投影数据源由装配层
    // 经 refreshCoverage 携带 CoverageResult 调用；此处仅按钮态。
    const bool writable = m_session.writable;
    const auto commandEnabled = [this](const std::string& id, bool local) {
        return local && (!m_commandAvailability || m_commandAvailability(id).enabled);
    };
    m_batchButton->setEnabled(commandEnabled("kinematics.validate-task-points",
                                            writable && static_cast<bool>(m_services.backgroundSubmit)));
    m_coverageRunButton->setEnabled(commandEnabled("kinematics.evaluate-coverage",
                                                   writable && static_cast<bool>(m_services.backgroundSubmit)));
    m_setTcpButton->setEnabled(commandEnabled("kinematics.set-default-tcp",
                                              kinCommandEnabledInSession("kinematics.set-default-tcp", writable)
                                              && m_services.commandHandler != nullptr));
    m_setDeviceButton->setEnabled(commandEnabled("kinematics.set-default-device",
                                                 kinCommandEnabledInSession("kinematics.set-default-device", writable)
                                                 && m_services.commandHandler != nullptr));
    const bool exportReady = static_cast<bool>(m_services.exportWriter)
                             && m_session.lastSessionSolutionSetView != nullptr;
    m_exportJsonButton->setEnabled(commandEnabled("kinematics.export-results", exportReady));
    m_exportCsvButton->setEnabled(commandEnabled("kinematics.export-results", exportReady));
    m_solveButton->setEnabled(commandEnabled("kinematics.solve-ik",
                                             m_services.ikSolver != nullptr
                                             && m_services.modelView != nullptr));
}

void KinematicsPanelWidget::setDisplayUnits(const std::optional<DisplayUnitProjection>& units)
{
    // L-K8：设置会话投影后全面板重投影——SI 真值不变（结果不动），仅
    // 显示文本重建；零重算（无任何评估调用）/零修订（无命令提交）。
    m_session.displayUnits = units;
    refreshAll();
}

void KinematicsPanelWidget::refreshPosePane()
{
    // 位姿指标：域设施直调（IFkEvaluator——缝未装配＝空态呈现，不虚构行）。
    if (m_services.fkEvaluator == nullptr || m_services.modelView == nullptr) {
        renderRows(m_poseTable, std::vector<KinNamedValueRow>{});
        return;
    }
    // 会话姿态在设即评估之；未设＝空呈现（零虚构数据行——零位评估会
    // 伪造"当前构型"语义）。
    if (m_services.sessionPose == nullptr || !m_services.sessionPose->isSet()) {
        renderRows(m_poseTable, std::vector<KinNamedValueRow>{});
        m_jointTable->setRowCount(0);
        return;
    }
    const std::vector<double>& q = m_services.sessionPose->jointConfiguration();
    const TcpRef tcp;  // canonical TCP（空 tcpKey——快照内解析）
    const auto metrics = m_services.fkEvaluator->evaluate(*m_services.modelView, tcp, q);
    if (!metrics.ok()) {
        // 失败如实呈现（KinematicsError 的稳定码 token＋detail——非模态
        // 状态行；token 转发表唯一出口 Errors.hpp）。
        const KinematicsError& err = metrics.error();
        showStatusText(QString::fromStdString(
            std::string("位姿指标不可用：")
            + std::string(kinematicsErrorCodeToken(err.code)) + " "
            + err.detail));
        return;
    }
    renderRows(m_poseTable, poseMetricRows(metrics.get(), m_session.displayUnits));
    // 关节表（KIN-12 显示投影——显示制式下的关节向量）。
    m_jointTable->setRowCount(static_cast<int>(q.size()));
    for (std::size_t i = 0; i < q.size(); ++i) {
        m_jointTable->setItem(static_cast<int>(i), 0,
                              new QTableWidgetItem(tr("J%1").arg(i + 1)));
        const double shown = m_session.displayUnits.has_value()
                                 ? m_session.displayUnits->projectAngle(q[i])
                                 : q[i];
        m_jointTable->setItem(static_cast<int>(i), 1,
                              new QTableWidgetItem(QString::number(shown)));
    }
}

void KinematicsPanelWidget::refreshResultsPane()
{
    if (!m_session.lastSessionSolutionSetView) {
        renderSolutions(m_solutionTable, std::vector<KinSolutionRow>{});
        fillSingleColumn(m_statTable, QStringList{});
        renderRows(m_inspectorTable, std::vector<KinNamedValueRow>{});
        return;
    }
    const IKinematicSolutionSet& set = *m_session.lastSessionSolutionSetView;
    renderSolutions(m_solutionTable,
                    solutionRowsFor(set, m_usableOnly->isChecked(),
                                    m_session.displayUnits));
    // 统计行（四计数直投——零自算）。
    const auto st = set.statistics();
    fillSingleColumn(m_statTable,
                     {tr("初值展开数 %1").arg(qlonglong(st.rawCount)),
                      tr("收敛数 %1").arg(qlonglong(st.convergedCount)),
                      tr("去重后解数 %1").arg(qlonglong(st.dedupedCount)),
                      tr("硬过滤数 %1").arg(qlonglong(st.filteredCount))});
}

void KinematicsPanelWidget::setWritable(bool writable)
{
    m_session.writable = writable;  // 会话事实更新（ui 会话投影同源）
    refreshAll();
}

void KinematicsPanelWidget::refreshTaskArea()
{
    // L-K12 任务状态投影（taskRowsProvider 现取——中断如实、零过滤；
    // 数据源权威在 ui ITaskPresentationModel，Qt 层投影为行值）。
    if (m_taskPointTable == nullptr) {
        return;
    }
    // 任务点表（只读消费视图——provider 现取零缓存）。列 0 行锚额外携带
    // pointOid 规范文本（Qt::UserRole——WP-15-T18 focusTaskPoint 定位键；
    // 呈现文本仍为工程用语标签，锚对用户不可见）。
    const std::vector<KinTaskPointRow> points =
        m_services.taskPoints ? m_services.taskPoints() : std::vector<KinTaskPointRow>{};
    m_taskPointTable->setRowCount(static_cast<int>(points.size()));
    for (std::size_t i = 0; i < points.size(); ++i) {
        const KinTaskPointRow& p = points[i];
        const auto row = static_cast<int>(i);
        QTableWidgetItem* labelItem = new QTableWidgetItem(
            QString::fromStdString(p.label));
        labelItem->setData(Qt::UserRole,
                           QString::fromStdString(p.pointOid.toCanonical()));
        m_taskPointTable->setItem(row, 0, labelItem);
        m_taskPointTable->setItem(row, 1, new QTableWidgetItem(
                                              p.enabled ? tr("启用") : tr("停用")));
        m_taskPointTable->setItem(row, 2, new QTableWidgetItem(
                                              p.outcomeText.has_value()
                                                  ? QString::fromStdString(*p.outcomeText)
                                                  : tr("未计算")));
        m_taskPointTable->setItem(row, 3, new QTableWidgetItem(
                                              p.requiredCoverageDone.has_value()
                                                  ? (*p.requiredCoverageDone ? tr("完成")
                                                                             : tr("未完成"))
                                                  : tr("—")));
    }
}

void KinematicsPanelWidget::focusTaskPoint(const std::optional<core::ObjectId>& oid)
{
    // 下行联动呈现半区（WP-15-T18——SelectionAdapter 消费选择服务后的
    // 执行器落点；UI 线程约束同其余会话交互面，§3.4）。
    if (m_taskPointTable == nullptr) {
        return;
    }

    // 无目标（多选/清空选中）＝仅清除高亮：清当前行，不切页不伪造定位
    // （requirements focusObject 同款幂等清除语义）。
    if (!oid.has_value()) {
        m_taskPointTable->clearSelection();
        m_taskPointTable->setCurrentItem(nullptr);
        return;
    }

    // 线性扫描行锚（refreshTaskArea 写入的 Qt::UserRole 规范文本——行序
    // ＝provider 供给序，扫描即稳定语义）。命中＝切到任务点页并置当前行
    // （结果面板高亮可见性——acceptance 4 v1 链路"任务点选择→结果面板
    // 高亮"的呈现落点；任务点定义真值归 requirements，本表是其结果列的
    // 本域消费视图，高亮动作零写回）；未命中＝清除（该选中对象在本域
    // 消费视图无结果行——不伪造定位，与 requirements"闭包外对象不清行
    // 定位"同案）。
    const QString anchorText = QString::fromStdString(oid.value().toCanonical());
    for (int i = 0; i < m_taskPointTable->rowCount(); ++i) {
        if (QTableWidgetItem* it = m_taskPointTable->item(i, 0);
            it != nullptr && it->data(Qt::UserRole).toString() == anchorText) {
            if (m_tabs != nullptr) {
                m_tabs->setCurrentIndex(1);  // 页②任务点验证（构建序固定）
            }
            m_taskPointTable->setCurrentItem(it);
            m_taskPointTable->scrollToItem(it);
            return;
        }
    }
    m_taskPointTable->clearSelection();
    m_taskPointTable->setCurrentItem(nullptr);
}

void KinematicsPanelWidget::onSolveClicked()
{
    // L-K2 转接（零内联逻辑——编排函数承担）。目标位姿从编辑字段读取
    // （显示制式数值＝SI 直读的轻量编辑位——单位换算经会话投影；字段
    // 解析失败就地反馈保留原值，UX-05）。
    bool okX = false, okY = false, okZ = false;
    double x = m_targetX->text().toDouble(&okX);
    double y = m_targetY->text().toDouble(&okY);
    double z = m_targetZ->text().toDouble(&okZ);
    if (!okX || !okY || !okZ) {
        showStatusText(tr("目标位姿非法（数值解析失败）——原值保留"));
        return;
    }
    if (m_session.displayUnits.has_value()) {
        // 显示制式→SI（角度制式影响位置分量的场景不存在——位置恒长度；
        // 此处直读 m 数值，投影仅影响回填呈现）。
        x = m_session.displayUnits->projectLength(x);
        y = m_session.displayUnits->projectLength(y);
        z = m_session.displayUnits->projectLength(z);
    }
    // 位姿构造用 9 参旋转（头内 inline）——不实例化引用 rw 库符号的单参构造（冒烟链接面）。
    const rw::math::Transform3D<double> target(
        rw::math::Vector3D<double>(x, y, z),
        rw::math::Rotation3D<double>(1.0, 0.0, 0.0,
                                     0.0, 1.0, 0.0,
                                     0.0, 0.0, 1.0));
    IkRequest request;  // 其余字段＝契约缺省（harness/装配层可注入完整请求）
    const KinSolveFlowResult r =
        runSinglePointSolve(m_services, m_session, target, request);
    showStatusText(QString::fromStdString(r.statusText));
    refreshResultsPane();
}

void KinematicsPanelWidget::onBatchClicked()
{
    // L-K3 转接。
    showStatusText(QString::fromStdString(submitBatchValidation(m_services, m_session)));
    refreshTaskArea();
}

void KinematicsPanelWidget::onCoverageRunClicked()
{
    // L-K6 转接。
    showStatusText(QString::fromStdString(
        submitCoverageEvaluation(m_services, m_session)));
    refreshTaskArea();
}

void KinematicsPanelWidget::onResetHomeClicked()
{
    // L-K4 复位 Home（Home 构型取关节表当前值——宿主自命名位姿集注入的
    // 语义在装配层；此处以会话关节投影承载演示通道）。
    std::vector<double> homeQ;
    if (m_services.sessionPose != nullptr && m_services.sessionPose->isSet()) {
        homeQ = m_services.sessionPose->jointConfiguration();
    }
    showStatusText(QString::fromStdString(
        resetSessionHome(m_services.sessionPose, homeQ)));
}

void KinematicsPanelWidget::onSetDefaultTcpClicked()
{
    // L-K9 转接（canonical TCP——快照内解析）。
    showStatusText(QString::fromStdString(
        submitSetDefaultTcp(m_services, m_session, TcpRef{})));
}

void KinematicsPanelWidget::onSetDefaultDeviceClicked()
{
    // L-K9 转接（设备根身份取自模型视图——快照内解析的会话事实）。
    core::ObjectId robotOid;
    if (m_services.modelView != nullptr) {
        robotOid = m_services.modelView->model().chain().robotObjectId;
    }
    showStatusText(QString::fromStdString(
        submitSetDefaultDevice(m_services, m_session, robotOid)));
}

void KinematicsPanelWidget::onExportJsonClicked()
{
    showStatusText(QString::fromStdString(
        exportSessionResults(m_services, m_session, "kin-results.json", false)));
}

void KinematicsPanelWidget::onExportCsvClicked()
{
    showStatusText(QString::fromStdString(
        exportSessionResults(m_services, m_session, "kin-results.csv", true)));
}

void KinematicsPanelWidget::onSolutionSelectionChanged()
{
    // L-K1 检查器联动（选中→SolutionRef→检查器行集现取）。
    if (!m_session.lastSessionSolutionSetView || m_inspectorTable == nullptr) {
        return;
    }
    const QModelIndex idx = m_solutionTable->currentIndex();
    if (!idx.isValid()) {
        return;
    }
    const auto renderRow = static_cast<std::size_t>(idx.row());
    const auto rows = solutionRowsFor(*m_session.lastSessionSolutionSetView,
                                      m_usableOnly->isChecked(),
                                      m_session.displayUnits);
    if (renderRow >= rows.size()) {
        return;
    }
    const SolutionRef ref{rows[renderRow].rank};
    renderRows(m_inspectorTable,
               solutionInspectorRows(*m_session.lastSessionSolutionSetView, ref,
                                     m_session.displayUnits));
}

void KinematicsPanelWidget::onSolutionDoubleClicked(int row, int column)
{
    // L-K4 双击候选→会话姿态回写（KIN-06：只写会话态——零修订、零失效；
    // 回写值经域设施 writebackOf 从解集视图产出）。
    Q_UNUSED(column);
    if (!m_session.lastSessionSolutionSetView || m_services.sessionPose == nullptr) {
        return;
    }
    const auto rows = solutionRowsFor(*m_session.lastSessionSolutionSetView,
                                      m_usableOnly->isChecked(),
                                      m_session.displayUnits);
    if (row < 0 || static_cast<std::size_t>(row) >= rows.size()) {
        return;
    }
    // 视图稳定序取解→回写值（Render.hpp writebackOf——域设施唯一产出点）。
    const SolutionRef ref{rows[static_cast<std::size_t>(row)].rank};
    const SolutionSetView& ordered = m_session.lastSessionSolutionSetView->sorted();
    if (ref.solutionIndex >= ordered.size()) {
        return;
    }
    showStatusText(QString::fromStdString(
        applyPoseWriteback(m_services.sessionPose,
                           writebackOf(ordered[ref.solutionIndex]))));
    refreshPosePane();
}

// =====================================================================
// KinematicsConfigPanel——求解配置高级面板（UX-04）
// =====================================================================

/**
 * @brief 域编辑出口（ui IFormEditOutlet 实现——表单公共件确认移交的
 *        域侧半区；把 ParamEditSet 转译为 applyConfigurationEdit 流）。
 */
class KinematicsConfigPanel::ConfigEditOutlet final : public ui::IFormEditOutlet {
public:
    ConfigEditOutlet(KinPanelServices services, KinModuleSessionState& session,
                     std::function<void(const QString&)> statusSink,
                     std::function<void()> hintRefresh)
        : m_services(std::move(services))
        , m_session(session)
        , m_statusSink(std::move(statusSink))
        , m_hintRefresh(std::move(hintRefresh))
    {
    }

    void applyEdits(const ui::ParamEditSet& editSet) override
    {
        // UI-T08 对端（acceptance 3"命令确认交互经 ui 表单公共件"）：
        // 确认修改集→域编排（校验/提示/保存——L-K7 唯一编排点）。
        std::vector<std::pair<std::string, double>> changes;
        changes.reserve(editSet.changes.size());
        for (const ui::ParamChange& c : editSet.changes) {
            changes.emplace_back(c.key, c.newSi);  // SI 真值直传（KIN-12）
        }
        const KinConfigEditResult r = applyConfigurationEdit(
            m_session, m_session.savedConfig, changes, m_services.configPersist);
        m_statusSink(QString::fromStdString(r.ok ? r.statusText : r.reason));
        if (r.ok) {
            m_hintRefresh();  // 提示行重投（事件驱动——零自动重算）
        }
    }

private:
    KinPanelServices m_services;
    KinModuleSessionState& m_session;
    std::function<void(const QString&)> m_statusSink;
    std::function<void()> m_hintRefresh;
};

KinematicsConfigPanel::KinematicsConfigPanel(KinPanelServices services,
                                             KinModuleSessionState& session,
                                             QWidget* parent)
    : QWidget(parent)
    , m_services(std::move(services))
    , m_session(session)
{
    auto* layout = new QVBoxLayout(this);

    // ---- 初值策略（域枚举字段——三值词表 §5.3；表单公共件数值半区之外
    //      的域自渲染位）。
    m_strategyBox = new QComboBox(this);
    m_strategyBox->addItem(tr("ReferenceQ（参考构型）"));
    m_strategyBox->addItem(tr("SeededRandom（种子随机）"));
    m_strategyBox->addItem(tr("JointGrid（关节网格）"));
    connect(m_strategyBox, &QComboBox::currentIndexChanged, this,
            &KinematicsConfigPanel::onStrategyChanged);
    layout->addWidget(new QLabel(tr("初值策略"), this));
    layout->addWidget(m_strategyBox);

    // ---- 数值字段编辑（ui 表单公共件——UI-T08 对端；字段 spec 装配期
    //      给定键/标签/量纲/约束——域语义，ui 宿主规则）。
    const auto len = core::UnitToken::find("m").value();
    const auto rad = core::UnitToken::find("rad").value();
    const auto dim = core::UnitToken::find("1").value();
    std::vector<ui::QuantityFieldSpec> specs;
    specs.push_back(ui::makeQuantityFieldSpec(
        "initial-values-count", tr("多初值数量").toStdString(),
        core::QuantityKind::Dimensionless, dim, dim,
        ui::QuantityBounds{1.0, 1024.0}, true));
    specs.push_back(ui::makeQuantityFieldSpec(
        "iteration-limit", tr("迭代上限").toStdString(),
        core::QuantityKind::Dimensionless, dim, dim,
        ui::QuantityBounds{1.0, 100000.0}, true));
    specs.push_back(ui::makeQuantityFieldSpec(
        "position-residual-tolerance", tr("位置容差").toStdString(),
        core::QuantityKind::Length, len, len, ui::QuantityBounds{1e-9, 1.0}, false));
    specs.push_back(ui::makeQuantityFieldSpec(
        "orientation-residual-tolerance", tr("姿态容差").toStdString(),
        core::QuantityKind::Angle, rad, rad, ui::QuantityBounds{1e-9, 1.0}, false));
    specs.push_back(ui::makeQuantityFieldSpec(
        "ik-dedup-threshold-per-axis", tr("去重阈值（逐轴）").toStdString(),
        core::QuantityKind::Angle, rad, rad, ui::QuantityBounds{1e-9, 1.0}, false));
    specs.push_back(ui::makeQuantityFieldSpec(
        "region-budget-seed", tr("采样种子").toStdString(),
        core::QuantityKind::Dimensionless, dim, dim,
        ui::QuantityBounds{1.0, 9007199254740992.0}, true));
    specs.push_back(ui::makeQuantityFieldSpec(
        "region-threads", tr("采样线程数").toStdString(),
        core::QuantityKind::Dimensionless, dim, dim,
        ui::QuantityBounds{1.0, 64.0}, true));
    specs.push_back(ui::makeQuantityFieldSpec(
        "seed", tr("随机种子").toStdString(),
        core::QuantityKind::Dimensionless, dim, dim,
        ui::QuantityBounds{1.0, 9007199254740992.0}, true));
    m_paramModel = std::make_unique<ui::ParamEditModel>(std::move(specs));

    // 编辑出口（确认移交→applyConfigurationEdit——L-K7 域半区）。
    m_outlet = std::make_unique<ConfigEditOutlet>(
        m_services, m_session,
        [this](const QString& t) { showStatusText(t); },
        [this]() { refreshAll(); });

    // 表单公共件面板（确认区/单位切换/批量粘贴——宿主规则全量继承）。
    m_paramTable = ui::createParamTablePanel(*m_paramModel, m_outlet.get(),
                                             ui::ParamTablePanelOptions{}, this);
    layout->addWidget(m_paramTable);

    // ---- 依赖提示行（L-K7 纯提示——不自动重算）。
    m_hintTable = makeTable({tr("提示"), tr("说明")}, this);
    layout->addWidget(m_hintTable);

    // ---- 策略只读摘要（无碰撞开关、无判定阈值——UX-08；文本由宿主从
    //      policy 投影，缺省如实说明）。
    m_policyTable = makeTable({tr("策略（只读）"), tr("值")}, this);
    layout->addWidget(m_policyTable);

    m_statusLine = new QLabel(tr("配置面板就绪"), this);
    layout->addWidget(m_statusLine);
    refreshAll();
}

KinematicsConfigPanel::~KinematicsConfigPanel() = default;

void KinematicsConfigPanel::refreshAll()
{
    // 基线注入（权威值来自会话态——表单零缓存基线）。
    const AnalysisConfiguration& c = m_session.savedConfig;
    m_paramModel->setBaseline("initial-values-count",
                              static_cast<double>(c.initialValuesCount));
    m_paramModel->setBaseline("iteration-limit",
                              static_cast<double>(c.iterationLimit));
    m_paramModel->setBaseline("position-residual-tolerance",
                              c.positionResidualTolerance);
    m_paramModel->setBaseline("orientation-residual-tolerance",
                              c.orientationResidualTolerance);
    m_paramModel->setBaseline("ik-dedup-threshold-per-axis",
                              c.ikDedupThresholdPerAxis);
    m_paramModel->setBaseline("region-budget-seed",
                              static_cast<double>(c.regionBudget.seed));
    m_paramModel->setBaseline("region-threads",
                              static_cast<double>(c.regionBudget.threadCount));
    m_paramModel->setBaseline("seed", static_cast<double>(c.seed));
    m_strategyBox->setCurrentIndex(static_cast<int>(c.initialStrategy));

    // 策略只读摘要（缺省文本＝如实说明——宿主可经装配注入替换）。
    fillSingleColumn(m_policyTable,
                     {tr("碰撞策略（只读）：策略会话在场与启用事实由 policy 承载"),
                      tr("判定阈值（只读）：阈值唯一来源 policy JointThresholds")});
}

QString KinematicsConfigPanel::lastStatusText() const
{
    return m_statusLine != nullptr ? m_statusLine->text() : QString();
}

void KinematicsConfigPanel::showStatusText(const QString& text)
{
    if (m_statusLine != nullptr) {
        m_statusLine->setText(text);
    }
}

void KinematicsConfigPanel::onStrategyChanged(int index)
{
    // 域枚举字段编辑（index 即 InitialValueStrategy 枚举值——§5.3 三值词
    // 表；非法 index 由 QComboBox 值域排除）。编辑即草稿（不自动保存——
    // 保存经表单确认流，L-K7）。
    if (index < 0) {
        return;
    }
    m_draftStrategy = static_cast<InitialValueStrategy>(index);
}

void KinematicsConfigPanel::onApplyResult(const QString& statusText)
{
    showStatusText(statusText);
}

}  // namespace kinematics
}  // namespace ird
}  // namespace sdurws
