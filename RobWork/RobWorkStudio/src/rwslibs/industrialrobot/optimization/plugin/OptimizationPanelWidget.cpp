/**
 * @file   OptimizationPanelWidget.cpp
 * @brief  optimization 主面板的实现翻译单元（四页接线与会话刷新——
 *         WP-20-T10）。
 *
 * 设计依据：OptimizationPanelWidget.hpp 文件头（四页纪律与红线）＋
 * OptPanelModel.hpp（L-O1~L-O9 流契约）。
 *
 * 零计算红线执行面：本 TU 全部为控件构建、模型层流调用与文本直读——
 * 无评估/统计/排序符号（契约测试全文词表扫描钉住）。候选评估绝不在
 * UI 线程执行（卡 §9.1 红线）——面板消费的全部是缝返回的现成投影。
 */

#include "OptimizationPanelWidget.hpp"

#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QStringList>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include "OptPanelModule.hpp"  // OptPanelModule（缝/会话态——完整型）

namespace sdurws::ird::optimization {
namespace {

/// 双精度屏读数书写（呈现面文本转换——6 位有效数字；"—"＝缺槽位）。
/// 这是纯文本格式化（QString::number）——非数值判定，零容差语义。
QString metricText(const std::optional<double>& value)
{
    return value.has_value() ? QString::number(*value, 'g', 6)
                             : QStringLiteral("—");
}

/// 候选状态 token→状态词键（状态词直译——呈现层词表查表，零判定）。
QString statusText(const std::string& statusToken,
                   const std::function<std::string(const std::string&)>& resolve)
{
    // 状态词键＝"plugin.optimization.status.<token>" 键族——值归宿主
    // 文案资源；解析结果经 L-O9 守卫（哈希形态回退键名）。
    return QString::fromStdString(resolve("plugin.optimization.status."
                                          + statusToken));
}

}  // namespace

// =====================================================================
// 构造（四页构建＋控件接线——每次调用新建，装配层恰调一次）。
// =====================================================================

OptimizationPanelWidget::OptimizationPanelWidget(OptPanelModule& module,
                                                 QWidget* parent)
    : QWidget(parent)
    , m_module(module)
{
    // ---- 顶层布局：四页 Tab 容器（页序＝kOptPanelPageKeys 词表序）。 --
    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(4, 4, 4, 4);
    m_tabs = new QTabWidget(this);
    rootLayout->addWidget(m_tabs);

    // ---- 页一：变量表（就绪投影＋绑定行表）。 -------------------------
    auto* variablesPage = new QWidget(this);
    auto* variablesLayout = new QVBoxLayout(variablesPage);
    m_readinessLabel = new QLabel(variablesPage);
    m_readinessLabel->setWordWrap(true);
    variablesLayout->addWidget(m_readinessLabel);
    m_variableTable = new QTableWidget(0, 11, variablesPage);
    m_variableTable->setHorizontalHeaderLabels(
        {QStringLiteral("绑定"), QStringLiteral("类别"), QStringLiteral("单位"),
         QStringLiteral("下界"), QStringLiteral("上界"), QStringLiteral("步长"),
         QStringLiteral("默认值"), QStringLiteral("锁定"), QStringLiteral("授权"),
         QStringLiteral("阶段启用"), QStringLiteral("权威字段")});
    m_variableTable->horizontalHeader()->setStretchLastSection(true);
    m_variableTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    variablesLayout->addWidget(m_variableTable);
    m_tabs->addTab(variablesPage, panelText(kOptPanelPageKeys[0]));

    // ---- 页二：约束页（阻塞横幅＋执行清单表）。 ----------------------
    // 横幅区＝§6.5 阻塞呈现位（阶段锁/检查阻塞——启动阻塞，绝不呈现
    // 为候选淘汰；候选页淘汰原因列的取值域不含阶段锁码——模型层纪律）。
    auto* constraintsPage = new QWidget(this);
    auto* constraintsLayout = new QVBoxLayout(constraintsPage);
    m_constraintBanner = new QLabel(constraintsPage);
    m_constraintBanner->setWordWrap(true);
    constraintsLayout->addWidget(m_constraintBanner);
    m_constraintTable = new QTableWidget(0, 3, constraintsPage);
    m_constraintTable->setHorizontalHeaderLabels(
        {QStringLiteral("序"), QStringLiteral("约束"), QStringLiteral("依据")});
    m_constraintTable->horizontalHeader()->setStretchLastSection(true);
    m_constraintTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    constraintsLayout->addWidget(m_constraintTable);
    m_tabs->addTab(constraintsPage, panelText(kOptPanelPageKeys[1]));

    // ---- 页三：运行控制（启动/取消按钮＋进度读数＋漏斗八段）。 -------
    // R1 边界（卡 §9.1）：UI 只显示进度和取消状态——按钮恰两个
    // （检查并计算/取消计算），零其他运行干预入口。
    auto* runPage = new QWidget(this);
    auto* runLayout = new QVBoxLayout(runPage);
    auto* buttonRow = new QHBoxLayout();
    m_startButton = new QPushButton(runPage);
    m_cancelButton = new QPushButton(runPage);
    buttonRow->addWidget(m_startButton);
    buttonRow->addWidget(m_cancelButton);
    buttonRow->addStretch(1);
    runLayout->addLayout(buttonRow);
    m_statusLabel = new QLabel(runPage);
    m_statusLabel->setWordWrap(true);
    runLayout->addWidget(m_statusLabel);
    m_progressLabel = new QLabel(runPage);
    runLayout->addWidget(m_progressLabel);
    // 漏斗八段标签（词表序直排——段态刷新只改样式文案前缀，零重排）。
    auto* funnelRow = new QHBoxLayout();
    for (std::size_t i = 0; i < kOptProgressPhaseTokens.size(); ++i) {
        auto* phaseLabel = new QLabel(runPage);
        funnelRow->addWidget(phaseLabel);
        m_funnelLabels.push_back(phaseLabel);
    }
    runLayout->addLayout(funnelRow);
    runLayout->addStretch(1);
    // 按钮接线（lambda 直连——零 Q_OBJECT 信号槽声明；点击流走模型层
    // L-O5，门控与拒绝呈现全在模型层，控件只转译结果文本）。
    connect(m_startButton, &QPushButton::clicked, this,
            [this]() { requestStart(); });
    connect(m_cancelButton, &QPushButton::clicked, this,
            [this]() { requestCancel(); });
    m_tabs->addTab(runPage, panelText(kOptPanelPageKeys[2]));

    // ---- 页四：候选表与对比（候选表＋A/B 选择＋对比区）。 ------------
    auto* candidatesPage = new QWidget(this);
    auto* candidatesLayout = new QVBoxLayout(candidatesPage);
    m_emptyHint = new QLabel(candidatesPage);
    m_emptyHint->setWordWrap(true);
    candidatesLayout->addWidget(m_emptyHint);
    m_candidateTable = new QTableWidget(0, 7, candidatesPage);
    m_candidateTable->setHorizontalHeaderLabels(
        {QStringLiteral("候选"), QStringLiteral("状态"), QStringLiteral("基线"),
         QStringLiteral("筛选专用"), QStringLiteral("工程判定"),
         QStringLiteral("淘汰原因"), QStringLiteral("命中回放")});
    m_candidateTable->horizontalHeader()->setStretchLastSection(true);
    m_candidateTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    // 八项指标不挤主表（列宽受限）——对比区承载逐指标 A/B/差值三列
    // （八项全量展示，OPT-07；主表保留状态与原因概览列）。
    candidatesLayout->addWidget(m_candidateTable);
    auto* compareRow = new QHBoxLayout();
    m_compareA = new QComboBox(candidatesPage);
    m_compareB = new QComboBox(candidatesPage);
    compareRow->addWidget(m_compareA);
    compareRow->addWidget(m_compareB);
    compareRow->addStretch(1);
    candidatesLayout->addLayout(compareRow);
    m_compareTable = new QTableWidget(0, 5, candidatesPage);
    m_compareTable->setHorizontalHeaderLabels(
        {QStringLiteral("指标"), QStringLiteral("单位"), QStringLiteral("方案 A"),
         QStringLiteral("方案 B"), QStringLiteral("差值 B−A")});
    m_compareTable->horizontalHeader()->setStretchLastSection(true);
    m_compareTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    candidatesLayout->addWidget(m_compareTable);
    // 对比区选择联动（选项变化即重建对比——L-O7；同侧/越界由模型层
    // fail-fast，选择器互斥由双下拉可选项约束）。
    connect(m_compareA, &QComboBox::currentIndexChanged, this,
            [this](int index) { rebuildComparison(index, m_compareB->currentIndex()); });
    connect(m_compareB, &QComboBox::currentIndexChanged, this,
            [this](int index) { rebuildComparison(m_compareA->currentIndex(), index); });
    m_tabs->addTab(candidatesPage, panelText(kOptPanelPageKeys[3]));

    // 构造即整面刷新（缝/会话态现状直呈现）。
    refreshFromSession();
}

// =====================================================================
// 私有流（UI 事件→模型层调用）。
// =====================================================================

void OptimizationPanelWidget::requestStart()
{
    // L-O5 启动半区（门控检查序固定在模型层——控件只转译结果）。
    const OptActionOutcome outcome =
        requestRunStart(m_module.session, m_module.services);
    if (!outcome.accepted) {
        // 拒因＝呈现状态词（非错误诊断——转译为提示文案键后解析）。
        m_statusLabel->setText(panelText("plugin.optimization.action.start."
                                         + outcome.rejectionToken));
        return;
    }
    // 受理→状态行提示＋刷新（运行事实推进由装配层会话刷新注入）。
    m_statusLabel->setText(panelText("plugin.optimization.action.start.accepted"));
    refreshRunControl();
}

void OptimizationPanelWidget::requestCancel()
{
    // L-O5 取消半区——UX-03：正常取消不属于错误、不产生错误诊断；
    // 拒因一律呈现为状态提示（非错误样式）。
    const OptActionOutcome outcome =
        requestRunCancel(m_module.session, m_module.services);
    if (!outcome.accepted) {
        m_statusLabel->setText(panelText("plugin.optimization.action.cancel."
                                         + outcome.rejectionToken));
        return;
    }
    m_statusLabel->setText(
        panelText("plugin.optimization.action.cancel.accepted"));
    refreshRunControl();
}

void OptimizationPanelWidget::refreshRunControl()
{
    // ---- L-O1 就绪行（状态词＋导出/应用可用位直译呈现）。 -----------
    const OptReadinessRow readiness = readinessProjection(m_module.session);
    QString statusLine = panelText("plugin.optimization.run.phase."
                                   + std::string(toToken(readiness.runPhase)));
    if (readiness.cancelRequested) {
        // 协作取消已请求的随行提示（批边界生效前——非错误样式）。
        statusLine += QStringLiteral(" · ")
                      + panelText("plugin.optimization.run.cancel-pending");
    }
    if (readiness.formalExportAvailable) {
        statusLine += QStringLiteral(" · ")
                      + panelText("plugin.optimization.run.formal-available");
    }
    m_statusLabel->setText(statusLine);

    // ---- L-O4 漏斗（缝空/nullopt→"无在途任务"空态；词表外 token 的
    // fail-fast 在模型层——控件不捕获，呈现面不静默容忍宿主漂移）。 --
    std::optional<OptProgressSample> sample;
    if (static_cast<bool>(m_module.services.progressSource)) {
        sample = m_module.services.progressSource();
    }
    const std::vector<OptFunnelRow> funnel = progressFunnelRows(sample);
    if (funnel.empty()) {
        m_progressLabel->setText(
            panelText("plugin.optimization.run.no-active-task"));
        for (QLabel* phaseLabel : m_funnelLabels) {
            phaseLabel->setText(QStringLiteral("·"));
        }
    } else {
        // 百分比＋批计数读数（样本值直读——零平滑零外推）。
        m_progressLabel->setText(
            QStringLiteral("%1% · %2/%3")
                .arg(sample->percent)
                .arg(sample->batchesDone)
                .arg(sample->batchesTotal));
        // 漏斗段标签（行集与标签数组逐位对位——词表序恒定）。
        for (std::size_t i = 0; i < m_funnelLabels.size() && i < funnel.size();
             ++i) {
            const QString title = panelText(funnel[i].titleKey);
            switch (funnel[i].state) {
            case OptFunnelState::Done:
                m_funnelLabels[i]->setText(QStringLiteral("✓") + title);
                break;
            case OptFunnelState::Active:
                m_funnelLabels[i]->setText(QStringLiteral("▶") + title);
                break;
            default:
                m_funnelLabels[i]->setText(title);
                break;
            }
        }
    }

    // ---- 按钮可用性（会话事实直译门控——与 L-O5 门控同源，零二次
    // 判定语义：可用性只影响点击前视觉，权威门控在模型层检查序）。 --
    m_startButton->setEnabled(m_module.session.writable
                              && !m_module.session.hasActiveTask
                              && !m_module.session.cancelRequested);
    m_cancelButton->setEnabled(m_module.session.hasActiveTask
                               && !m_module.session.cancelRequested);
}

void OptimizationPanelWidget::refreshVariablePage()
{
    // ---- L-O1 就绪投影（七态素材＋缺项清单）。 ----------------------
    const OptReadinessRow readiness = readinessProjection(m_module.session);
    QStringList missing;
    for (const std::string& key : readiness.missingItemKeys) {
        missing << panelText(key);
    }
    QString readinessLine = panelText("plugin.optimization.readiness.prefix");
    readinessLine += readiness.inputComplete
                         ? panelText("plugin.optimization.readiness.complete")
                         : panelText("plugin.optimization.readiness.incomplete");
    if (!missing.isEmpty()) {
        readinessLine += QStringLiteral("  ") + missing.join(QStringLiteral("；"));
    }
    m_readinessLabel->setText(readinessLine);

    // ---- L-O2 变量表行集（缝空→空态提示，不伪造行）。 ---------------
    if (!static_cast<bool>(m_module.services.variableSource)) {
        m_variableTable->setRowCount(0);
        return;
    }
    const std::vector<OptVariableRow> rows = variableTableRows(
        m_module.session.stage, m_module.services.variableSource());
    m_variableTable->setRowCount(static_cast<int>(rows.size()));
    for (int r = 0; r < static_cast<int>(rows.size()); ++r) {
        const OptVariableRow& row = rows[static_cast<std::size_t>(r)];
        // 权威字段列承担人读定位（UX-02——绑定 token 是内部键不进用户
        // 文本；单位空串→"—"〔值非物理量——UX-03 不适用显式标记〕）。
        m_variableTable->setItem(
            r, 0, new QTableWidgetItem(QString::fromStdString(row.authorityFieldPath)));
        m_variableTable->setItem(
            r, 1, new QTableWidgetItem(QString::fromStdString(row.kindToken)));
        m_variableTable->setItem(
            r, 2,
            new QTableWidgetItem(row.unitSymbol.empty()
                                     ? QStringLiteral("—")
                                     : QString::fromStdString(row.unitSymbol)));
        m_variableTable->setItem(
            r, 3, new QTableWidgetItem(QString::number(row.lowerBound, 'g', 6)));
        m_variableTable->setItem(
            r, 4, new QTableWidgetItem(QString::number(row.upperBound, 'g', 6)));
        m_variableTable->setItem(
            r, 5, new QTableWidgetItem(QString::number(row.step, 'g', 6)));
        m_variableTable->setItem(
            r, 6,
            new QTableWidgetItem(QString::number(row.defaultValue, 'g', 6)));
        m_variableTable->setItem(
            r, 7,
            new QTableWidgetItem(row.locked ? QStringLiteral("是") : QStringLiteral("否")));
        m_variableTable->setItem(
            r, 8,
            new QTableWidgetItem(row.authorized ? QStringLiteral("是") : QStringLiteral("否")));
        m_variableTable->setItem(
            r, 9,
            new QTableWidgetItem(row.stageEnabled ? QStringLiteral("是") : QStringLiteral("否")));
        m_variableTable->setItem(
            r, 10,
            new QTableWidgetItem(QString::fromStdString(row.authorityFieldPath)));
    }
}

void OptimizationPanelWidget::refreshConstraintPage()
{
    // ---- L-O8 横幅（阻塞发现→横幅区；报告缺省→"尚未检查"提示）。 --
    const std::vector<OptBannerItem> banners =
        stageLockBannerItems(m_module.session);
    if (!m_module.session.latestPreflight.has_value()) {
        m_constraintBanner->setText(
            panelText("plugin.optimization.banner.not-checked"));
    } else if (banners.empty()) {
        m_constraintBanner->setText(
            panelText("plugin.optimization.banner.none"));
    } else {
        // 逐条横幅（诊断 token 不进用户文本——标题键＋定位＋建议三段；
        // §6.5：横幅是启动阻塞呈现位，与候选淘汰严格分立）。
        QStringList lines;
        for (const OptBannerItem& banner : banners) {
            lines << QString("%1 [%2] %3")
                         .arg(panelText(banner.titleKey))
                         .arg(QString::fromStdString(banner.subject))
                         .arg(QString::fromStdString(banner.suggestion));
        }
        m_constraintBanner->setText(lines.join(QStringLiteral("\n")));
    }

    // ---- L-O3 约束页呈现（阶段分派在模型层——StageD 阶段锁横幅＋空
    // 行集；StageB 清单行透传；缝空→空态）。 --------------------------
    std::optional<std::vector<ConstraintSpec>> plan;
    if (static_cast<bool>(m_module.services.constraintPlanSource)) {
        plan = m_module.services.constraintPlanSource();
    }
    const OptConstraintPage page =
        constraintPagePresentation(m_module.session, plan);
    if (page.dataState == "not-assembled") {
        m_constraintTable->setRowCount(0);
        return;
    }
    m_constraintTable->setRowCount(static_cast<int>(page.planRows.size()));
    for (int r = 0; r < static_cast<int>(page.planRows.size()); ++r) {
        const OptConstraintRow& row = page.planRows[static_cast<std::size_t>(r)];
        m_constraintTable->setItem(
            r, 0, new QTableWidgetItem(QString::number(row.ordinal)));
        m_constraintTable->setItem(
            r, 1,
            new QTableWidgetItem(QString::fromStdString(row.constraintToken)));
        m_constraintTable->setItem(
            r, 2,
            new QTableWidgetItem(QString::fromStdString(row.evaluationKey)));
    }
}

void OptimizationPanelWidget::refreshCandidatePage()
{
    // ---- L-O6 候选表（缝空/nullopt→空态提示，不伪造行）。 -----------
    if (!static_cast<bool>(m_module.services.runResultSource)) {
        m_emptyHint->setText(
            panelText("plugin.optimization.candidates.not-assembled"));
        m_candidateTable->setRowCount(0);
        m_compareA->clear();
        m_compareB->clear();
        m_compareTable->setRowCount(0);
        return;
    }
    const std::optional<OptimizationRunResult> runResult =
        m_module.services.runResultSource();
    if (!runResult.has_value()) {
        m_emptyHint->setText(
            panelText("plugin.optimization.candidates.none"));
        m_candidateTable->setRowCount(0);
        m_compareA->clear();
        m_compareB->clear();
        m_compareTable->setRowCount(0);
        return;
    }
    m_emptyHint->setText(QString());

    const std::vector<OptCandidateRow> rows = candidateTableRows(*runResult);
    m_candidateTable->setRowCount(static_cast<int>(rows.size()));
    for (int r = 0; r < static_cast<int>(rows.size()); ++r) {
        const OptCandidateRow& row = rows[static_cast<std::size_t>(r)];
        // 呈现标签＝"候选 N"（UX-02——身份规范文本零进用户文本）；
        // 淘汰原因列＝token 串接（词表 token 透传——阶段锁码不在其域）。
        QStringList reasons;
        for (const std::string& token : row.rejectionReasonTokens) {
            reasons << QString::fromStdString(token);
        }
        m_candidateTable->setItem(
            r, 0,
            new QTableWidgetItem(QString::fromStdString(row.displayLabel)));
        m_candidateTable->setItem(
            r, 1,
            new QTableWidgetItem(
                statusText(row.statusToken, m_module.services.textResolver)));
        m_candidateTable->setItem(
            r, 2,
            new QTableWidgetItem(row.isBaseline ? QStringLiteral("是") : QStringLiteral("否")));
        m_candidateTable->setItem(
            r, 3,
            new QTableWidgetItem(row.screeningOnly ? QStringLiteral("是") : QStringLiteral("否")));
        m_candidateTable->setItem(
            r, 4,
            new QTableWidgetItem(
                row.engineeringStatus == core::EngineeringStatus::Feasible
                    ? QStringLiteral("可行")
                    : (row.engineeringStatus
                               == core::EngineeringStatus::EngineeringInfeasible
                           ? QStringLiteral("不可行")
                           : QStringLiteral("—"))));
        m_candidateTable->setItem(
            r, 5, new QTableWidgetItem(reasons.join(QStringLiteral("；"))));
        m_candidateTable->setItem(
            r, 6,
            new QTableWidgetItem(row.cacheHit ? QStringLiteral("是") : QStringLiteral("否")));
    }

    // ---- 对比选择器重建（候选项按呈现标签入列；A 默认首行、B 默认次行
    // ——行数不足两项时对比区保持空）。 --------------------------------
    QSignalBlocker blockA(m_compareA);
    QSignalBlocker blockB(m_compareB);
    m_compareA->clear();
    m_compareB->clear();
    for (const OptCandidateRow& row : rows) {
        const QString label = QString::fromStdString(row.displayLabel);
        m_compareA->addItem(label);
        m_compareB->addItem(label);
    }
    if (rows.size() >= 2) {
        m_compareA->setCurrentIndex(0);
        m_compareB->setCurrentIndex(1);
        rebuildComparison(0, 1);
    } else {
        m_compareTable->setRowCount(0);
    }
}

void OptimizationPanelWidget::rebuildComparison(int indexA, int indexB)
{
    // 缝缺/无结果/行数不足→对比区清空（空态，不伪造差值）。
    if (!static_cast<bool>(m_module.services.runResultSource)
        || indexA < 0 || indexB < 0 || indexA == indexB) {
        m_compareTable->setRowCount(0);
        return;
    }
    const std::optional<OptimizationRunResult> runResult =
        m_module.services.runResultSource();
    if (!runResult.has_value()) {
        m_compareTable->setRowCount(0);
        return;
    }
    // L-O7 对比行（越界/同候选 fail-fast 在模型层——本处下标已守卫）。
    const std::vector<OptComparisonRow> rows =
        candidateComparison(*runResult, static_cast<std::size_t>(indexA),
                            static_cast<std::size_t>(indexB));
    m_compareTable->setRowCount(static_cast<int>(rows.size()));
    for (int r = 0; r < static_cast<int>(rows.size()); ++r) {
        const OptComparisonRow& row = rows[static_cast<std::size_t>(r)];
        // 差异高亮＝行首标记（differs 位——零容差发明；方向劣势/优势
        // 不另设箭头，bBetter 留给后续批次样式化——本批文本面直译）。
        const QString marker = row.differs ? QStringLiteral("≠ ") : QString();
        m_compareTable->setItem(
            r, 0,
            new QTableWidgetItem(marker
                                 + QString::fromStdString(row.metricToken)));
        m_compareTable->setItem(
            r, 1,
            new QTableWidgetItem(row.unitToken.empty()
                                     ? QStringLiteral("—")
                                     : QString::fromStdString(row.unitToken)));
        m_compareTable->setItem(r, 2, new QTableWidgetItem(metricText(row.valueA)));
        m_compareTable->setItem(r, 3, new QTableWidgetItem(metricText(row.valueB)));
        m_compareTable->setItem(r, 4, new QTableWidgetItem(metricText(row.delta)));
    }
}

// =====================================================================
// 会话刷新（会话事实变化后的整面重投影——四页逐页刷新）。
// =====================================================================

void OptimizationPanelWidget::refreshFromSession()
{
    refreshVariablePage();
    refreshConstraintPage();
    refreshRunControl();
    refreshCandidatePage();
}

// =====================================================================
// 文案便利（L-O9 解析——全部用户文本的唯一入口）。
// =====================================================================

QString OptimizationPanelWidget::panelText(const std::string& titleKey) const
{
    // L-O9（缝解析＋空缝键名兜底＋哈希形态回退守卫——守卫在模型层）。
    return QString::fromStdString(
        resolvePanelText(m_module.services, titleKey));
}

}  // namespace sdurws::ird::optimization
