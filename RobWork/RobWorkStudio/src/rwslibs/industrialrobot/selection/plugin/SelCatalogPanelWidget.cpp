/**
 * @file   SelCatalogPanelWidget.cpp
 * @brief  selection 主面板的实现翻译单元（三页控件接线与会话刷新——
 *         数据消费全经模型层 L-Sx 流与服务缝，零业务计算）。
 *
 * 设计依据：SelCatalogPanelWidget.hpp 文件头（三页呈现面口径）。
 * 零计算红线执行面（卡 §3.4）：本 TU 全部为控件布局、行集填表与文案
 * 键解析——无任何选型计算符号（契约测试全文词表扫描钉住）；全部
 * 用户文本经 panelText（L-S5 文案解析——UX-02 唯一入口），呈现序＝
 * 缝给定序（零排序调用）。
 */

#include "SelCatalogPanelWidget.hpp"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStringList>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include <string>
#include <vector>

#include "SelPanelModule.hpp"  // 模块（缝/会话态——构造入参完整型）
#include "SelPanelCommandCatalog.hpp" // kSelBackfillUiCommandId（回填命令
                                      //   ui 命令 id——点分词形唯一书写点）

namespace sdurws::ird::selection {
namespace {

/// 表格只读呈现的公共整形（列宽随内容拉伸——纯呈现语义）。
void stretchTableColumns(QTableWidget* table)
{
    if (table != nullptr) {
        table->horizontalHeader()->setSectionResizeMode(
            QHeaderView::Stretch);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    }
}

}  // namespace

// =====================================================================
// 构造（三页控件接线——零 Q_OBJECT：事件经 lambda 连接）。
// =====================================================================

SelCatalogPanelWidget::SelCatalogPanelWidget(SelPanelModule& module,
                                             QWidget* parent)
    : QWidget(parent), m_module(module)
{
    // ---- Tab 容器（三页——kSel*PageKey 键经文案解析呈现页标题）。 ---
    m_tabs = new QTabWidget(this);

    // ---- 页一：工作流页（就绪投影＋缺项＋回填入口＋最近提交）。 ----
    QWidget* workflowPage = new QWidget(this);
    QVBoxLayout* workflowLayout = new QVBoxLayout(workflowPage);
    m_readinessLabel = new QLabel(workflowPage);   // L-S1 投影文本
    m_readinessLabel->setWordWrap(true);
    m_missingLabel = new QLabel(workflowPage);     // UX-10 缺项清单
    m_missingLabel->setWordWrap(true);
    m_backfillButton = new QPushButton(workflowPage);  // 回填入口——点击流
                                                       //   经 L-S4 提交
    m_recentEdit = new QPlainTextEdit(workflowPage);   // 最近提交清单（只读）
    m_recentEdit->setReadOnly(true);
    workflowLayout->addWidget(m_readinessLabel);
    workflowLayout->addWidget(m_missingLabel);
    workflowLayout->addWidget(m_backfillButton);
    workflowLayout->addWidget(m_recentEdit);
    // 回填按钮点击→模型层 L-S4 提交流（零 Q_OBJECT——lambda 连接；
    // 提交后的会话刷新由提交流尾部统一执行）。
    connect(m_backfillButton, &QPushButton::clicked, this,
            [this]() { submitBackfillCommand(); });

    // ---- 页二：目录管理页（版本清单表＋空态/选中提示）。 -----------
    QWidget* catalogPage = new QWidget(this);
    QVBoxLayout* catalogLayout = new QVBoxLayout(catalogPage);
    m_catalogHintLabel = new QLabel(catalogPage);  // 空态/选中提示
    m_catalogHintLabel->setWordWrap(true);
    m_catalogTable = new QTableWidget(catalogPage); // 列：目录 ID/版本/
                                                    //   来源/当前锁定
    const QStringList catalogHeaders = {QString::fromUtf8("目录 ID"),
                                        QString::fromUtf8("版本"),
                                        QString::fromUtf8("来源"),
                                        QString::fromUtf8("当前锁定")};
    m_catalogTable->setColumnCount(catalogHeaders.size());
    m_catalogTable->setHorizontalHeaderLabels(catalogHeaders);
    stretchTableColumns(m_catalogTable);
    catalogLayout->addWidget(m_catalogHintLabel);
    catalogLayout->addWidget(m_catalogTable);

    // ---- 页三：候选表页（候选表＋原因明细＋缺口明细——分轨呈现）。 -
    QWidget* candidatesPage = new QWidget(this);
    QVBoxLayout* candidatesLayout = new QVBoxLayout(candidatesPage);
    m_candidateTable = new QTableWidget(candidatesPage); // 列：组合/呈现态/
                                                         //   质量/最小裕量/
                                                         //   原因数/标注
    const QStringList candidateHeaders = {QString::fromUtf8("组合"),
                                          QString::fromUtf8("呈现态"),
                                          QString::fromUtf8("质量"),
                                          QString::fromUtf8("最小裕量"),
                                          QString::fromUtf8("原因数"),
                                          QString::fromUtf8("标注")};
    m_candidateTable->setColumnCount(candidateHeaders.size());
    m_candidateTable->setHorizontalHeaderLabels(candidateHeaders);
    stretchTableColumns(m_candidateTable);
    m_rejectionTable = new QTableWidget(candidatesPage); // 列：原因/轴/工况/
                                                         //   实际值/要求值/
                                                         //   单位/阈值来源/
                                                         //   诊断码
    const QStringList rejectionHeaders = {QString::fromUtf8("原因"),
                                          QString::fromUtf8("轴"),
                                          QString::fromUtf8("工况"),
                                          QString::fromUtf8("实际值"),
                                          QString::fromUtf8("要求值"),
                                          QString::fromUtf8("单位"),
                                          QString::fromUtf8("阈值来源"),
                                          QString::fromUtf8("诊断码")};
    m_rejectionTable->setColumnCount(rejectionHeaders.size());
    m_rejectionTable->setHorizontalHeaderLabels(rejectionHeaders);
    stretchTableColumns(m_rejectionTable);
    m_gapTable = new QTableWidget(candidatesPage); // 列：缺口维/轴/工况/
                                                   //   诊断码（与原因分轨）
    const QStringList gapHeaders = {QString::fromUtf8("缺口维"),
                                    QString::fromUtf8("轴"),
                                    QString::fromUtf8("工况"),
                                    QString::fromUtf8("诊断码")};
    m_gapTable->setColumnCount(gapHeaders.size());
    m_gapTable->setHorizontalHeaderLabels(gapHeaders);
    stretchTableColumns(m_gapTable);
    candidatesLayout->addWidget(m_candidateTable);
    candidatesLayout->addWidget(m_rejectionTable);
    candidatesLayout->addWidget(m_gapTable);

    // ---- 三页挂位（页标题键经 L-S5 解析——UX-02 唯一文本入口）。 ---
    m_tabs->addTab(workflowPage, panelText(kSelWorkflowPageKey));
    m_tabs->addTab(catalogPage, panelText(kSelCatalogPageKey));
    m_tabs->addTab(candidatesPage, panelText(kSelCandidatesPageKey));

    QVBoxLayout* rootLayout = new QVBoxLayout(this);
    rootLayout->addWidget(m_tabs);

    // 构造即整面刷新（装配时序——面板创建即呈现当前会话事实）。
    refreshFromSession();
}

// =====================================================================
// 文案便利（L-S5——全部用户文本的唯一入口）。
// =====================================================================

QString SelCatalogPanelWidget::panelText(const std::string& titleKey) const
{
    return QString::fromStdString(
        resolvePanelText(m_module.services, titleKey));
}

// =====================================================================
// 私有流：回填提交（L-S4——按钮点击→模型层→会话刷新）。
// =====================================================================

void SelCatalogPanelWidget::submitBackfillCommand()
{
    // 模型层 L-S4 提交流（空缝/不可用拒绝在模型层三态归一——面板零
    // 本地判定）；提交后整面刷新（最近提交清单随会话缓冲更新）。
    // 意图 token＝回填命令的 ui 命令 id（点分词形——与登记目录/宿主
    // 命令注册表同词面；真实 project 命令信封组装归宿主提交缝，
    // commandType 无点词形在彼侧换轨——插件零事务知识）。
    const std::string token(kSelBackfillUiCommandId);
    submitBackfill(m_module.session, m_module.services, token);
    refreshFromSession();
}

// =====================================================================
// 私有流：就绪投影呈现（L-S1——UX-10 七态素材文本合成）。
// =====================================================================

void SelCatalogPanelWidget::refreshReadiness()
{
    // 投影合成（纯透传——判定权威在域就绪校验，呈现零加工）。
    const SelReadinessRow row = readinessProjection(m_module.session);
    // 就绪结论行（inputComplete/在途任务/判定词——UX-10"未完成附缺
    // 项/计算中/数据不足"的素材呈现；判定值经 core 词表四值映射为
    // 文案键——呈现词形唯一书写点在此，值归宿主文案资源）。
    const char* verdictKey = "state.not-applicable.label";
    switch (row.verdict) {
    case core::EngineeringStatus::Feasible:
        verdictKey = "state.feasible.label";
        break;
    case core::EngineeringStatus::EngineeringInfeasible:
        verdictKey = "state.engineering-infeasible.label";
        break;
    case core::EngineeringStatus::DataInsufficient:
        verdictKey = "state.data-insufficient.label";
        break;
    case core::EngineeringStatus::NotApplicable:
        verdictKey = "state.not-applicable.label";
        break;
    }
    QString text = panelText("plugin.selection.readiness.summary");
    text += " ";
    text += panelText(verdictKey);
    if (!row.inputComplete) {
        text += " ";
        text += panelText("plugin.selection.readiness.incomplete");
    }
    if (row.hasActiveTask) {
        text += " ";
        text += panelText("plugin.selection.readiness.active-task");
    }
    m_readinessLabel->setText(text);

    // 缺项清单（UX-10"未完成附缺项列表"——键经 L-S5 解析逐条呈现；
    // 空清单＝整行隐藏，不伪造缺项）。
    if (row.missingItemKeys.empty()) {
        m_missingLabel->clear();
    } else {
        QString missing = panelText("plugin.selection.readiness.missing");
        for (const std::string& key : row.missingItemKeys) {
            missing += "\n- ";
            missing += panelText(key);
        }
        m_missingLabel->setText(missing);
    }
}

// =====================================================================
// 私有流：目录版本表刷新（L-S2——空态降级＋行集填表）。
// =====================================================================

void SelCatalogPanelWidget::refreshCatalogTable()
{
    bool notAssembled = false;
    const std::vector<SelCatalogRow> rows =
        catalogRowsPresented(m_module.services, &notAssembled);
    if (notAssembled) {
        // 缝未装配→空态呈现（不伪造行——NFR-COR-03）。
        m_catalogTable->setRowCount(0);
        m_catalogHintLabel->setText(
            panelText("plugin.selection.catalog.not-assembled"));
        return;
    }
    if (rows.empty()) {
        m_catalogHintLabel->setText(
            panelText("plugin.selection.catalog.empty"));
        return;
    }
    m_catalogHintLabel->clear();
    m_catalogTable->setRowCount(static_cast<int>(rows.size()));
    for (std::size_t r = 0; r < rows.size(); ++r) {
        const SelCatalogRow& row = rows[r];
        // 显示文本经模型层合成（L-S2 拼接——零哈希零内部标识）。
        m_catalogTable->setItem(
            static_cast<int>(r), 0,
            new QTableWidgetItem(
                QString::fromStdString(catalogDisplayText(row))));
        m_catalogTable->setItem(
            static_cast<int>(r), 1,
            new QTableWidgetItem(QString::fromStdString(row.version)));
        m_catalogTable->setItem(
            static_cast<int>(r), 2,
            new QTableWidgetItem(panelText(row.sourceLabelKey)));
        // 当前锁定列（事实位——"是/否"经文案键呈现，零本地判定）。
        m_catalogTable->setItem(
            static_cast<int>(r), 3,
            new QTableWidgetItem(panelText(row.selected
                                               ? "plugin.selection.common.yes"
                                               : "plugin.selection.common.no")));
    }
}

// =====================================================================
// 私有流：候选/原因/缺口表刷新（L-S3——分轨呈现＋空态降级）。
// =====================================================================

void SelCatalogPanelWidget::refreshCandidateTables()
{
    // ---- 候选表（行集透传——零判定；质量/裕量列经 formatMetric 带
    //      单位呈现，无素材＝"不适用"占位，不伪造 0）。 -----------------
    bool notAssembled = false;
    const std::vector<SelCandidateRow> candidates =
        candidateRowsPresented(m_module.services, &notAssembled);
    if (notAssembled) {
        m_candidateTable->setRowCount(0);
    } else {
        m_candidateTable->setRowCount(static_cast<int>(candidates.size()));
        for (std::size_t r = 0; r < candidates.size(); ++r) {
            const SelCandidateRow& row = candidates[r];
            m_candidateTable->setItem(
                static_cast<int>(r), 0,
                new QTableWidgetItem(panelText(row.combinationKeyLabel)));
            // 呈现态键直出词形（三态封闭词表——文案值归宿主；范围外
            // 语义经 data-insufficient 态＋标注列双面呈现——wp19-t08）。
            m_candidateTable->setItem(
                static_cast<int>(r), 1,
                new QTableWidgetItem(QString::fromStdString(row.verdictKey)));
            m_candidateTable->setItem(
                static_cast<int>(r), 2,
                new QTableWidgetItem(
                    row.hasMass ? QString::fromStdString(
                                      formatMetric(row.totalMassKg, "kg"))
                                : panelText(
                                      "plugin.selection.common.not-applicable")));
            m_candidateTable->setItem(
                static_cast<int>(r), 3,
                new QTableWidgetItem(
                    row.hasMargin
                        ? QString::fromStdString(
                              formatMetric(row.minMargin, ""))
                        : panelText(
                              "plugin.selection.common.not-applicable")));
            m_candidateTable->setItem(
                static_cast<int>(r), 4,
                new QTableWidgetItem(QString::number(row.reasonCount)));
            // 标注列（格级 note 键——空＝无标注；范围外键与缺口维键
            // 同词呈现——双面一致性）。
            m_candidateTable->setItem(
                static_cast<int>(r), 5,
                new QTableWidgetItem(
                    row.noteKey.empty()
                        ? QString()
                        : panelText(row.noteKey)));
        }
    }

    // ---- 淘汰原因明细表（比较型字段全列——SEL-06/ERR-01 呈现面）。 -
    notAssembled = false;
    const std::vector<SelRejectionRow> rejections =
        rejectionRowsPresented(m_module.services, &notAssembled);
    if (notAssembled) {
        m_rejectionTable->setRowCount(0);
    } else {
        m_rejectionTable->setRowCount(static_cast<int>(rejections.size()));
        for (std::size_t r = 0; r < rejections.size(); ++r) {
            const SelRejectionRow& row = rejections[r];
            m_rejectionTable->setItem(
                static_cast<int>(r), 0,
                new QTableWidgetItem(panelText(row.reasonKey)));
            m_rejectionTable->setItem(
                static_cast<int>(r), 1,
                new QTableWidgetItem(panelText(row.axisLabel)));
            m_rejectionTable->setItem(
                static_cast<int>(r), 2,
                new QTableWidgetItem(
                    row.caseLabel.empty()
                        ? QString()
                        : panelText(row.caseLabel)));
            // 数值列带单位显示（formatMetric——UX-02"数值带单位"）。
            m_rejectionTable->setItem(
                static_cast<int>(r), 3,
                new QTableWidgetItem(QString::fromStdString(
                    formatMetric(row.actual, row.unitToken))));
            m_rejectionTable->setItem(
                static_cast<int>(r), 4,
                new QTableWidgetItem(QString::fromStdString(
                    formatMetric(row.required, row.unitToken))));
            m_rejectionTable->setItem(
                static_cast<int>(r), 5,
                new QTableWidgetItem(QString::fromStdString(row.unitToken)));
            m_rejectionTable->setItem(
                static_cast<int>(r), 6,
                new QTableWidgetItem(panelText(row.thresholdSourceKey)));
            m_rejectionTable->setItem(
                static_cast<int>(r), 7,
                new QTableWidgetItem(
                    QString::fromStdString(row.diagCodeText)));
        }
    }

    // ---- 数据缺口明细表（与原因分轨——§10.2 空集语义分类的呈现面）。
    notAssembled = false;
    const std::vector<SelGapRow> gaps =
        gapRowsPresented(m_module.services, &notAssembled);
    if (notAssembled) {
        m_gapTable->setRowCount(0);
    } else {
        m_gapTable->setRowCount(static_cast<int>(gaps.size()));
        for (std::size_t r = 0; r < gaps.size(); ++r) {
            const SelGapRow& row = gaps[r];
            m_gapTable->setItem(
                static_cast<int>(r), 0,
                new QTableWidgetItem(QString::fromStdString(row.dimensionKey)));
            m_gapTable->setItem(
                static_cast<int>(r), 1,
                new QTableWidgetItem(
                    row.axisLabel.empty()
                        ? QString()
                        : panelText(row.axisLabel)));
            m_gapTable->setItem(
                static_cast<int>(r), 2,
                new QTableWidgetItem(
                    row.caseLabel.empty()
                        ? QString()
                        : panelText(row.caseLabel)));
            m_gapTable->setItem(
                static_cast<int>(r), 3,
                new QTableWidgetItem(
                    QString::fromStdString(row.diagCodeText)));
        }
    }
}

// =====================================================================
// 会话刷新（整面重投影——现取缝/会话态，零缓存）。
// =====================================================================

void SelCatalogPanelWidget::refreshFromSession()
{
    refreshReadiness();        // L-S1 就绪投影
    refreshCatalogTable();     // L-S2 目录行集
    refreshCandidateTables();  // L-S3 候选/原因/缺口行集

    // ---- 最近回填提交清单（会话缓冲直呈——AT-30 复算提示随行呈现）。
    QString recent;
    for (const SelBackfillRecord& record : m_module.session.recentBackfills) {
        if (!recent.isEmpty()) {
            recent += "\n";
        }
        recent += QString::fromStdString(record.commandToken);
        if (record.accepted) {
            // 受理行附复算提示（四域清单＋不沿用——AT-30 呈现面）。
            recent += " ";
            recent += panelText("plugin.selection.backfill.accepted");
            recent += " ";
            recent += panelText("plugin.selection.backfill.recalc");
            for (const std::string& domainKey :
                 record.notice.domainLabelKeys) {
                recent += " ";
                recent += panelText(domainKey);
            }
        } else {
            // 不受理行附拒绝键（用户可见不受理——诚实反馈）。
            recent += " ";
            recent += panelText("plugin.selection.backfill.rejected");
            recent += " ";
            recent += panelText(record.rejectionKey);
        }
    }
    m_recentEdit->setPlainText(recent);

    // ---- 回填按钮文本与可用性（可用性权威在宿主缝——空缝按可用
    //      呈现，点击时经 L-S4 空缝语义如实反馈）。标题键按 §3.5 键族
    //      从 ui 命令 id 程序化派生（与登记目录单一书写点同源——零
    //      字面复制，词形修订时随常量联动）。
    m_backfillButton->setText(panelText("cmd."
                                        + std::string(kSelBackfillUiCommandId)
                                        + ".title"));
    const std::string token(kSelBackfillUiCommandId);
    const bool available =
        !static_cast<bool>(m_module.services.backfillAvailability)
        || m_module.services.backfillAvailability(token);
    m_backfillButton->setEnabled(available);
}

}  // namespace sdurws::ird::selection
