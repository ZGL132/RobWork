/**
 * @file   RequirementsPanelWidget.cpp
 * @brief  需求域面板 widget 实现——五区薄装配（渲染/转接/转发，零业务判定）。
 *
 * 设计依据：units/requirements.md §9.8（面板表＋界面逻辑表＋线程约束）；
 * 契约 WP-14-T08 acceptance 1/2/4/5。实现纪律：一切信息架构决策在零 Qt
 * 呈现模型与计算库；widget 只做投影渲染、事件转接、命令转发；刷新事件
 * 驱动（无轮询定时器）；不缓存权威数据（refreshPanel 入参现取）。
 */

#include "RequirementsPanelWidget.hpp"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWidget>

#include <stdexcept>
#include <utility>

namespace sdurws::ird::requirements {
namespace {

// 树/表节点列上的锚存取（隐藏第 1 列＝ObjectId 规范文本——Qt 项与域身份
// 的关联通道；空串＝无锚节点〔分组/汇总行〕）。
constexpr int kAnchorColumn = 1;

void setNodeAnchor(QTreeWidgetItem* item, const std::optional<core::ObjectId>& oid)
{
    // 规范文本承载（toCanonical——"obj-<32hex>"；nullopt→空串）。
    item->setText(kAnchorColumn, oid.has_value()
                                     ? QString::fromStdString(oid.value().toCanonical())
                                     : QString());
}

std::optional<core::ObjectId> nodeAnchor(QTreeWidgetItem* item)
{
    const QString text = item->text(kAnchorColumn);
    if (text.isEmpty()) {
        return std::nullopt;  // 无锚节点（分组/汇总行）——nullopt 不伪造
    }
    // try 轨解析（文本可能被外部改动——解析失败按无锚处置，不猜测）。
    return core::ObjectId::tryFromCanonical(text.toStdString());
}

// 建一个只读表（标题列＋隐藏锚列——headers 值拷贝后追加锚列标题）。
QTreeWidget* makeTable(QWidget* parent, const QStringList& headers)
{
    QStringList allHeaders = headers;  // 值拷贝（const 入参不可就地追加）
    allHeaders << QString();          // 末列＝隐藏锚列
    auto* t = new QTreeWidget(parent);
    t->setColumnCount(allHeaders.size());
    t->setHeaderLabels(allHeaders);
    t->setRootIsDecorated(false);
    t->setColumnHidden(allHeaders.size() - 1, true);  // 锚列隐藏（仅承载关联键）
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    return t;
}

// 两列行装配（显示列＋锚）。
QTreeWidgetItem* makeRow(const QString& a, const QString& b, const std::optional<core::ObjectId>& anchor)
{
    auto* item = new QTreeWidgetItem(QStringList() << a << b);
    setNodeAnchor(item, anchor);
    return item;
}

}  // namespace

// =====================================================================
// 构造与装配注入
// =====================================================================

RequirementsPanelWidget::RequirementsPanelWidget(bool writable, QWidget* parent)
    : QWidget(parent), m_writable(writable)
{
    m_threadGuard.assertOnUiThread();  // 构造线程＝UI 线程（§3.4——守卫绑定）
    m_commands = requirementsDomainCommands();  // 命令目录（§9.8 九条——装配数据）

    // 骨架：顶部命令条＋（左树 1｜右四页面 2）水平区。
    auto* rootLayout = new QVBoxLayout(this);
    auto* contentLayout = new QHBoxLayout;
    rootLayout->addLayout(contentLayout);

    // 命令条（rootLayout 第 0 行——buildCommandBar 经 insertWidget 挂入）。
    buildCommandBar(this);

    // 左栏对象树（权 1）。
    auto* left = new QWidget(this);
    contentLayout->addWidget(left, 1);
    buildTreePane(left);

    // 右栏四页面容器（权 2——Tab：工位/区域/工况/校验，UX-09 右栏投影）。
    auto* right = new QWidget(this);
    contentLayout->addWidget(right, 2);
    auto* rightLayout = new QVBoxLayout(right);
    m_pages = new QTabWidget(right);
    rightLayout->addWidget(m_pages);
    // 四页面（先容器后页——各 build 在 m_pages 上创建并挂页）。
    buildStationPage(m_pages);
    buildRegionPage(m_pages);
    buildConditionPage(m_pages);
    buildValidationPage(m_pages);

    // 构造完成即可刷新（空工作集态——投影产出空行/占位，不虚构内容；
    // 首次 refreshPanel 由装配层在载入基线后驱动）。
}

void RequirementsPanelWidget::setCommandSubmit(CommandSubmitFn submitFn)
{
    m_commandSubmit = std::move(submitFn);
}

void RequirementsPanelWidget::setEditTargetProvider(EditTargetProvider provider)
{
    m_editTarget = std::move(provider);
}

void RequirementsPanelWidget::setRegionPreviewSink(RegionPreviewSink sink)
{
    m_regionPreview = std::move(sink);
}

// =====================================================================
// 面板构建（构造期一次——布局骨架）
// =====================================================================

void RequirementsPanelWidget::buildCommandBar(QWidget* top)
{
    // 域命令区：九条命令一列按钮（与 m_commands 序对应——L 转发提交出口）。
    auto* bar = new QWidget(top);
    auto* barLayout = new QHBoxLayout(bar);
    barLayout->setContentsMargins(0, 0, 0, 0);
    for (const ui::CommandDescriptor& d : m_commands) {
        auto* btn = new QPushButton(QString::fromStdString(d.id), bar);
        btn->setToolTip(QString::fromStdString(d.menuPath));
        // 命令标题过渡文案归 UiText（键即契约——按钮暂以 id 呈现，不虚构
        // 标题值；UI-T09 文案资源落地后由装配层刷新）。
        connect(btn, &QPushButton::clicked, this, &RequirementsPanelWidget::onCommandButtonClicked);
        barLayout->addWidget(btn);
        m_commandButtons.push_back(btn);
    }
    // 两级撤销三按钮（L-R4——草稿级撤销/重做与项目级撤销三处独立控件，
    // 不合并：草稿级动编辑器局部栈，项目级纯转发 ui 命令——语义不混用）。
    m_draftUndoButton = new QPushButton(QString::fromStdString("撤销本次编辑（草稿级）"), bar);
    m_draftRedoButton = new QPushButton(QString::fromStdString("重做本次编辑（草稿级）"), bar);
    m_projectUndoButton = new QPushButton(QString::fromStdString("撤销上次应用（项目级）"), bar);
    connect(m_draftUndoButton, &QPushButton::clicked, this, &RequirementsPanelWidget::onDraftUndo);
    connect(m_draftRedoButton, &QPushButton::clicked, this, &RequirementsPanelWidget::onDraftRedo);
    connect(m_projectUndoButton, &QPushButton::clicked, this, &RequirementsPanelWidget::onProjectUndo);
    barLayout->addWidget(m_draftUndoButton);
    barLayout->addWidget(m_draftRedoButton);
    barLayout->addWidget(m_projectUndoButton);
    // 状态行（就地错误/警告/摘要——非模态呈现，UX-03/07）。
    m_statusLine = new QLabel(bar);
    barLayout->addWidget(m_statusLine, 1);

    // 挂到根布局顶部（构造序保证：构造函数先建 QVBoxLayout(this) 再调本
    // 函数——layout() 恒为 QVBoxLayout；异常布局形态＝装配缺陷 fail-fast）。
    auto* rootLayout = qobject_cast<QVBoxLayout*>(layout());
    if (rootLayout == nullptr) {
        throw std::logic_error("需求面板：根布局缺失或非 QVBoxLayout（装配缺陷）");
    }
    rootLayout->insertWidget(0, bar);
}

void RequirementsPanelWidget::buildTreePane(QWidget* left)
{
    // 左栏需求对象树（卡 §9.8 面板表第 1 行——两列：显示名＋隐藏锚）。
    auto* leftLayout = new QVBoxLayout(left);
    m_tree = makeTable(left, QStringList() << "需求对象");
    connect(m_tree, &QTreeWidget::itemSelectionChanged, this,
            &RequirementsPanelWidget::onTreeSelectionChanged);
    leftLayout->addWidget(m_tree);
}

void RequirementsPanelWidget::buildStationPage(QTabWidget* pages)
{
    // 右栏页①工位（检查器表单——行＝stationFieldsFor 投影）。
    auto* page = new QWidget(pages);
    auto* lay = new QVBoxLayout(page);
    m_stationForm = new QFormLayout;
    lay->addLayout(m_stationForm);
    lay->addStretch(1);
    pages->addTab(page, "工位");
}

void RequirementsPanelWidget::buildRegionPage(QTabWidget* pages)
{
    // 右栏页②区域（表＋检查器＋预览摘要——§9.8 面板表第 3 行）。
    auto* page = new QWidget(pages);
    auto* lay = new QVBoxLayout(page);
    m_regionTable = makeTable(page, QStringList() << "区域" << "采样" << "覆盖目标");
    connect(m_regionTable, &QTreeWidget::itemSelectionChanged, this,
            &RequirementsPanelWidget::onTreeSelectionChanged);
    lay->addWidget(m_regionTable);
    m_regionForm = new QFormLayout;
    lay->addLayout(m_regionForm);
    m_regionPreviewLabel = new QLabel(page);
    lay->addWidget(m_regionPreviewLabel);
    pages->addTab(page, "区域");
}

void RequirementsPanelWidget::buildConditionPage(QTabWidget* pages)
{
    // 右栏页③工况（表＋检查器＋必验清单预览——§9.8 面板表第 4 行）。
    auto* page = new QWidget(pages);
    auto* lay = new QVBoxLayout(page);
    m_conditionTable = makeTable(page, QStringList() << "工况" << "节拍" << "适用范围");
    connect(m_conditionTable, &QTreeWidget::itemSelectionChanged, this,
            &RequirementsPanelWidget::onTreeSelectionChanged);
    lay->addWidget(m_conditionTable);
    m_conditionForm = new QFormLayout;
    lay->addLayout(m_conditionForm);
    m_mustList = makeTable(page, QStringList() << "必验工况" << "必验");
    lay->addWidget(m_mustList);
    pages->addTab(page, "工况");
}

void RequirementsPanelWidget::buildValidationPage(QTabWidget* pages)
{
    // 右栏页④校验（分层计数＋R0~R9 行＋逐项跳转＋语义说明——面板表第 5 行）。
    auto* page = new QWidget(pages);
    auto* lay = new QVBoxLayout(page);
    m_validationCounts = new QLabel(page);
    lay->addWidget(m_validationCounts);
    m_validationLayers = makeTable(page, QStringList() << "层" << "检查" << "阻断" << "警告");
    lay->addWidget(m_validationLayers);
    m_validationItems = makeTable(page, QStringList() << "级别" << "层" << "码" << "说明");
    connect(m_validationItems, &QTreeWidget::itemClicked, this,
            [this](QTreeWidgetItem* item, int) {
                // L-R1 反向定位：逐项行点击→树滚动＋三维高亮（校验面板的
                // 逐项定位跳转——UX-06；无锚行点击无效，不伪造定位）。
                if (const auto anchor = nodeAnchor(item)) {
                    m_selection.locate(anchor.value());
                }
            });
    lay->addWidget(m_validationItems);
    m_validationNotes = new QLabel(page);
    m_validationNotes->setWordWrap(true);
    lay->addWidget(m_validationNotes);
    pages->addTab(page, "校验");
}

// =====================================================================
// 全面板刷新（事件驱动出口——零缓存，全部现取重投影）
// =====================================================================

void RequirementsPanelWidget::refreshPanel(const RequirementWorkingSet& ws,
                                           const RequirementReadinessReport& report)
{
    m_threadGuard.assertOnUiThread();  // §3.4——刷新入口跨线程即 fail-fast
    renderTree(ws);
    renderInspector(ws);
    renderRegionPage(ws);
    renderConditionPage(ws);
    renderValidationPage(report);

    // 两级撤销呈现（L-R4——从编辑器草稿态与记账器现取，可位驱动按钮）。
    if (IRequirementEditor* editor = m_editTarget ? m_editTarget() : nullptr) {
        const TwoLevelUndoView undoView = twoLevelUndoView(*editor, m_undoTracker);
        m_draftUndoButton->setEnabled(m_writable && undoView.canUndoLocal);
        m_draftRedoButton->setEnabled(m_writable && undoView.canRedoLocal);
        // 项目级撤销是转发面——可达性随命令提交出口（未注入＝禁用）。
        m_projectUndoButton->setEnabled(static_cast<bool>(m_commandSubmit));
    }
}

void RequirementsPanelWidget::setWritable(bool writable)
{
    m_writable = writable;  // L-R12——ui 只读横幅同源事实
    // 可写性切换＝全面板重投影（检查器行门控随行投影刷新；命令按钮可达
    // 性由 readOnlyAllowed 的 ui 命令门控面承担——本面板只控编辑行与撤销钮）。
    if (IRequirementEditor* editor = m_editTarget ? m_editTarget() : nullptr) {
        // 空报告＝只读切换不新造判定——刷新沿用最近校验结论由装配层驱动；
        // 此处仅重投影编辑面（树/检查器/区域/工况），校验页待装配层携带
        // 报告的下一轮 refreshPanel。
        const RequirementWorkingSet& ws = editor->workingSet();
        renderTree(ws);
        renderInspector(ws);
        renderRegionPage(ws);
        renderConditionPage(ws);
    }
}

// =====================================================================
// 渲染辅助（投影结果→控件；零业务判定）
// =====================================================================

void RequirementsPanelWidget::renderTree(const RequirementWorkingSet& ws)
{
    // UX-02 守卫在模型层（ensureNoInternalIdentity）——widget 只渲染行。
    const std::vector<RequirementNode> nodes = buildRequirementTree(ws);
    m_tree->clear();
    for (const RequirementNode& n : nodes) {
        auto* item = new QTreeWidgetItem(
            QStringList() << QString::fromStdString(n.displayLabel));
        setNodeAnchor(item, n.objectId);
        item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        m_tree->addTopLevelItem(item);
        // 恢复选中锚（跨刷新保持——L-R1 会话态；仅条目级锚可恢复）。
        if (n.objectId.has_value() && m_lastSelected.has_value()
            && n.objectId.value() == m_lastSelected.value()) {
            item->setSelected(true);
        }
    }
}

void RequirementsPanelWidget::renderInspector(const RequirementWorkingSet& ws)
{
    // 检查器按当前选中重投影（L-R1 反向半区；无选中＝空表单——不虚构）。
    while (m_stationForm->rowCount() > 0) {
        m_stationForm->removeRow(0);
    }
    m_stationEditors.clear();
    m_stationRows.clear();
    if (!m_lastSelected.has_value()) {
        return;
    }
    // 选中的条目须在工作集内定位（闭包外身份＝空表单——呈现保持原状）。
    const TaskPoint* point = nullptr;
    for (const TaskPoint& p : ws.points.entries) {
        if (p.objectId == m_lastSelected.value()) {
            point = &p;
            break;
        }
    }
    if (point == nullptr) {
        return;
    }
    // 行投影（writable 门控在模型层——applyReadOnlyGate 同源规则内置）。
    m_stationRows = stationFieldsFor(*point, m_writable);
    for (const StationFieldRow& r : m_stationRows) {
        auto* edit = new QLineEdit(QString::fromStdString(
            r.valueText == "未提供" || r.valueText == "未设" ? "" : r.valueText));
        edit->setReadOnly(r.enablement != StationFieldEnablement::Editable
                          || !m_writable);  // 灰显行只读（L-R12 行门控）
        edit->setProperty("irdFieldKey", QString::fromStdString(r.fieldKey));
        connect(edit, &QLineEdit::editingFinished, this,
                &RequirementsPanelWidget::onInspectorEditingFinished);
        auto* label = new QLabel(QString::fromStdString(r.label), this);
        const QString unit = QString::fromStdString(r.unitText);
        m_stationForm->addRow(label, edit);
        m_stationEditors.push_back(edit);
    }
}

void RequirementsPanelWidget::renderRegionPage(const RequirementWorkingSet& ws)
{
    // 区域表（一区域一行——L-R1 行选中锚）。
    const std::vector<RegionRow> rows = regionRows(ws.regions.entries);
    m_regionTable->clear();
    for (const RegionRow& r : rows) {
        m_regionTable->addTopLevelItem(makeRow(QString::fromStdString(r.name),
                                               QString::fromStdString(r.samplingText),
                                               r.objectId));
    }
    // 区域检查器（选中区域行→regionFieldsFor；未选中＝空表单——不虚构）。
    while (m_regionForm->rowCount() > 0) {
        m_regionForm->removeRow(0);
    }
    std::optional<core::ObjectId> selectedRegion;
    if (m_regionTable->currentItem() != nullptr) {
        selectedRegion = nodeAnchor(m_regionTable->currentItem());
    }
    for (const WorkRegion& region : ws.regions.entries) {
        if (!selectedRegion.has_value() || region.objectId != selectedRegion.value()) {
            continue;
        }
        const std::vector<StationFieldRow> fields =
            regionFieldsFor(region, m_regionService, m_writable);
        for (const StationFieldRow& fr : fields) {
            auto* edit = new QLineEdit(QString::fromStdString(fr.valueText), this);
            edit->setReadOnly(fr.enablement != StationFieldEnablement::Editable || !m_writable);
            m_regionForm->addRow(QString::fromStdString(fr.label), edit);
        }
        break;  // 至多一个选中区域——命中即止（确定性）
    }
    // 区域轮廓与采样格预览（呈现几何——regionPreviewGeometry；结果着色归
    // KIN-07，本预览零结果语义：仅文本摘要＋可选 View3D 投递）。
    if (!ws.regions.entries.empty()) {
        const WorkRegion& first = ws.regions.entries.front();
        const RegionPreviewGeometry geo = regionPreviewGeometry(first, m_regionService);
        m_regionPreviewLabel->setText(QString::fromStdString(geo.summaryText));
        if (m_regionPreview) {
            m_regionPreview(geo);  // View3D 出口注入时投递（装配层接线）
        }
    } else {
        m_regionPreviewLabel->setText(QString());
    }
}

void RequirementsPanelWidget::renderConditionPage(const RequirementWorkingSet& ws)
{
    // 工况表（一工况一行）。
    const std::vector<ConditionRow> rows = conditionRows(ws.conditions.entries);
    m_conditionTable->clear();
    for (const ConditionRow& r : rows) {
        m_conditionTable->addTopLevelItem(makeRow(QString::fromStdString(r.name),
                                                  QString::fromStdString(r.cycleText),
                                                  r.objectId));
    }
    // 工况检查器（选中行→conditionFieldsFor）。
    while (m_conditionForm->rowCount() > 0) {
        m_conditionForm->removeRow(0);
    }
    if (m_conditionTable->currentItem() != nullptr) {
        if (const auto anchor = nodeAnchor(m_conditionTable->currentItem())) {
            for (const OperatingCondition& c : ws.conditions.entries) {
                if (c.objectId == anchor.value()) {
                    for (const StationFieldRow& fr :
                         conditionFieldsFor(c, m_conditionService, m_writable)) {
                        auto* edit =
                            new QLineEdit(QString::fromStdString(fr.valueText), this);
                        edit->setReadOnly(fr.enablement != StationFieldEnablement::Editable
                                          || !m_writable);
                        m_conditionForm->addRow(QString::fromStdString(fr.label), edit);
                    }
                    break;
                }
            }
        }
    }
    // 必验清单预览（RequirementProfile 投影——派生经域函数，P-EV-9 单点）。
    const std::vector<MustListEntryRow> mustRows =
        mustListPreview(ws.points.entries, ws.regions.entries, ws.conditions.entries,
                        m_conditionService);
    m_mustList->clear();
    for (const MustListEntryRow& r : mustRows) {
        m_mustList->addTopLevelItem(
            makeRow(QString::fromStdString(r.label),
                    r.enabled && r.mandatory ? QString("必验") : QString(),
                    r.caseId));
    }
}

void RequirementsPanelWidget::renderValidationPage(const RequirementReadinessReport& report)
{
    // 校验面板（报告→行卡纯投影——零重估零判定，P-REQ-6 边界）。
    const ValidationPanelProjection proj = projectValidationPanel(report);
    m_validationCounts->setText(QString("阻断 %1 · 警告 %2")
                                    .arg(proj.blockingCount)
                                    .arg(proj.warningCount));
    m_validationLayers->clear();
    for (const ValidationLayerRow& r : proj.layers) {
        m_validationLayers->addTopLevelItem(new QTreeWidgetItem(QStringList()
            << QString::fromStdString(r.layerToken) << QString::fromStdString(r.title)
            << QString::number(r.blocking) << QString::number(r.warning)));
    }
    m_validationItems->clear();
    for (const ValidationItemRow& r : proj.items) {
        auto* item = new QTreeWidgetItem(QStringList()
            << QString::fromStdString(r.levelToken) << QString::fromStdString(r.layerToken)
            << QString::fromStdString(r.code) << QString::fromStdString(r.summary));
        setNodeAnchor(item, r.jumpTarget);  // 跳转锚（无定位行＝空锚——不可点击跳转）
        m_validationItems->addTopLevelItem(item);
    }
    // 预览/正式语义说明（REQ-06 固定文案——零加工直投）。
    m_validationNotes->setText(QString::fromStdString(proj.previewNote) + "\n"
                               + QString::fromStdString(proj.formalNote));
}

// =====================================================================
// 槽：用户事件→模型/域入口转接
// =====================================================================

void RequirementsPanelWidget::onTreeSelectionChanged()
{
    // L-R1 正向半区：树/区域表/工况表选中→会话态＋检查器重投影（零修订）。
    std::optional<core::ObjectId> anchor;
    if (QObject::sender() == m_tree && m_tree->currentItem() != nullptr) {
        anchor = nodeAnchor(m_tree->currentItem());
    } else if (QObject::sender() == m_regionTable && m_regionTable->currentItem() != nullptr) {
        anchor = nodeAnchor(m_regionTable->currentItem());
    } else if (QObject::sender() == m_conditionTable
               && m_conditionTable->currentItem() != nullptr) {
        anchor = nodeAnchor(m_conditionTable->currentItem());
    }
    if (m_selection.select(anchor)) {
        m_lastSelected = anchor;
        Q_EMIT selectionChanged(
            anchor.has_value() ? QString::fromStdString(anchor.value().toCanonical())
                               : QString());
        // 检查器重投影（从提供器现取——零缓存）。
        if (IRequirementEditor* editor = m_editTarget ? m_editTarget() : nullptr) {
            renderInspector(editor->workingSet());
        }
    }
}

void RequirementsPanelWidget::onInspectorEditingFinished()
{
    // L-R2 入口：控件提交→表单回填→submitEntryEdit 域裁决（分流见 sink 回调）。
    m_threadGuard.assertOnUiThread();
    IRequirementEditor* editor = m_editTarget ? m_editTarget() : nullptr;
    if (editor == nullptr || !m_writable || !m_lastSelected.has_value()) {
        return;  // 编辑禁用（未注入/只读/无选中）——不虚构可编辑性
    }
    // 定位被编辑行（sender 的 irdFieldKey 属性——提交时的回填参数）。
    auto* edit = qobject_cast<QLineEdit*>(QObject::sender());
    if (edit == nullptr) {
        return;
    }
    const std::string key = edit->property("irdFieldKey").toString().toStdString();
    // 从工作集定位当前条目（权威值——检查器行的基线）。
    const RequirementWorkingSet& ws = editor->workingSet();
    for (const TaskPoint& p : ws.points.entries) {
        if (p.objectId != m_lastSelected.value()) {
            continue;
        }
        // 数量字段经 ui FormEditCommon 解析（parseFieldValueText——呈现层
        // 输入合法性＋单位换算唯一出口，SA-12；业务裁决在 applyEdit 域链）。
        for (const ui::QuantityFieldSpec& spec : stationQuantitySpecs()) {
            if (spec.key != key) {
                continue;
            }
            const ui::ValueParseResult parsed = parseFieldValueText(
                edit->text().toStdString(), spec);
            if (!parsed.ok) {
                // 就地错误（非模态——UX-05；值控件回退由失败不写回实现）。
                m_statusLine->setText(QString::fromStdString(parsed.reason));
                renderInspector(ws);  // 回退显示工作集权威值
                return;
            }
            // ParamEditSet 装配（SI 真值——回填词表见 applyStationEditSet）。
            ui::ParamEditSet edits;
            ui::ParamChange change;
            change.key = key;
            change.label = spec.label;
            change.newSi = parsed.siValue;
            edits.changes.push_back(change);
            std::vector<std::string> known;
            const TaskPoint candidate = applyStationEditSet(p, edits, known);
            // 域裁决（submitEntryEdit——接受/拒绝分流见 IRequirementEditSink）。
            const EditSubmitOutcome outcome = submitEntryEdit(*editor, *this, candidate);
            if (outcome == EditSubmitOutcome::Applied) {
                m_undoTracker.recordAppliedEdit();  // 撤销记账（L-R4 事实源）
            }
            return;
        }
        // 词表外键＝实现缺陷（表单/回填词表漂移）——fail-fast。
        throw std::logic_error("工位面板：编辑行携带词表外键 " + key);
    }
}

void RequirementsPanelWidget::onCommandButtonClicked()
{
    // 域命令区：按钮激活→转发注入的提交出口（不本地执行任何域逻辑——
    // 命令入口权威归 ui CommandRegistry，PA-1）。
    auto* btn = qobject_cast<QPushButton*>(QObject::sender());
    if (btn == nullptr || !m_commandSubmit) {
        return;
    }
    for (std::size_t i = 0; i < m_commandButtons.size(); ++i) {
        if (m_commandButtons[i] == btn) {
            m_commandSubmit(m_commands[i].id);
            return;
        }
    }
}

void RequirementsPanelWidget::onDraftUndo()
{
    // L-R4 草稿级半区：编辑器局部撤销（零修订）＋记账＋刷新。
    m_threadGuard.assertOnUiThread();
    IRequirementEditor* editor = m_editTarget ? m_editTarget() : nullptr;
    if (editor == nullptr || !m_writable) {
        return;
    }
    if (m_undoTracker.undo(*editor)) {
        m_statusLine->setText("已撤销一次编辑（草稿级，零修订）");
        if (IRequirementEditor* e2 = m_editTarget()) {
            refreshPanel(e2->workingSet(), RequirementReadinessReport{});
        }
    }
}

void RequirementsPanelWidget::onDraftRedo()
{
    // L-R4 草稿级半区：编辑器局部重做（零修订）＋记账＋刷新。
    m_threadGuard.assertOnUiThread();
    IRequirementEditor* editor = m_editTarget ? m_editTarget() : nullptr;
    if (editor == nullptr || !m_writable) {
        return;
    }
    if (m_undoTracker.redo(*editor)) {
        m_statusLine->setText("已重做一次编辑（草稿级，零修订）");
        if (IRequirementEditor* e2 = m_editTarget()) {
            refreshPanel(e2->workingSet(), RequirementReadinessReport{});
        }
    }
}

void RequirementsPanelWidget::onProjectUndo()
{
    // L-R4 项目级半区：纯转发面（经注入的命令提交出口转发 ui 项目撤销
    // 命令——UndoRedoService 语义归 project，本面板不复制不代理其判定）。
    if (m_commandSubmit) {
        m_commandSubmit("edit.undo");  // ui 命令词表（壳级撤销命令——装配层绑定）
    }
}

// =====================================================================
// IRequirementEditSink（L-R2 分流回调）
// =====================================================================

void RequirementsPanelWidget::onEditApplied(const std::string& changeSummary)
{
    // 接受分支：全面板增量刷新（经提供器现取工作集——不使用旧副本）＋
    // 脏标记呈现（PM-04/PM-11）。
    m_threadGuard.assertOnUiThread();
    m_dirty = true;
    if (IRequirementEditor* editor = m_editTarget ? m_editTarget() : nullptr) {
        // 校验页待装配层携带就绪报告的下一轮 refreshPanel（编辑态即时预检
        // 的调用编排归装配层——本面板不自行调用校验器，保持零判定）。
        const RequirementWorkingSet& ws = editor->workingSet();
        renderTree(ws);
        renderInspector(ws);
        renderRegionPage(ws);
        renderConditionPage(ws);
    }
    m_statusLine->setText(QString::fromStdString(changeSummary));
    Q_EMIT sessionDirtyChanged(true);
}

void RequirementsPanelWidget::notifySessionDirty()
{
    // 会话脏通知（标题 `*` 标记的生产者接线点——信号上呈装配层）。
    m_threadGuard.assertOnUiThread();
    m_dirty = true;
    Q_EMIT sessionDirtyChanged(true);
}

void RequirementsPanelWidget::onEditRejected(const EditRejection& rejection)
{
    // 拒绝分支：就地错误呈现（状态行——非模态，UX-03/07；值控件回退由
    // 重投影显示工作集权威值实现）。
    m_threadGuard.assertOnUiThread();
    m_statusLine->setText(QString("编辑被拒绝（%1）：")
                              .arg(QString::fromStdString(rejection.codeToken))
                          + QString::fromStdString(rejection.detail));
    if (IRequirementEditor* editor = m_editTarget ? m_editTarget() : nullptr) {
        renderInspector(editor->workingSet());  // 原值保留——回退权威值显示
    }
}

void RequirementsPanelWidget::onBatchWarning(const core::DiagnosticRecord& warning)
{
    // 批次警告知情登记（L-R9——warning 不阻断应用；状态行逐条登记）。
    m_threadGuard.assertOnUiThread();
    m_statusLine->setText(QString("批次警告（%1）：")
                              .arg(QString::fromStdString(warning.code))
                          + QString::fromStdString(warning.cause));
}

}  // namespace sdurws::ird::requirements
