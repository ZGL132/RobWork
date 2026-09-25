/**
 * @file   ModelingPanelWidget.cpp
 * @brief  建模域面板实现——五区控件装配与事件转接（卡 §9.7.1/§9.7.2）。
 *
 * 设计依据：units/modeling.md §9.7 系（信息架构与接线）、§9.7.4（事件驱动
 * ／零缓存／UI 线程）、ui.md §4.2/§6.6（UX-02）；契约 WP-13-T15
 * acceptance 1/2/3。实现纪律：零计算逻辑（判定唯一在计算库——本文件只有
 * 控件装配与"事件→呈现模型/域入口"的转接）；一切投影现取（零权威数据
 * 缓存）；错误就地呈现（无任何 exec()/模态路径——UX-07）。
 */

#include "ModelingPanelWidget.hpp"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QTabWidget>
#include <QVBoxLayout>

#include <sdurws/ird/ui/UiTypes.hpp>  // ui::TextKey（命令标题键——呈现层键解析约定）

namespace sdurws::ird::modeling {
namespace {

/// 树锚列（隐藏列——节点锚 ObjectId 规范文本；L-1 关联键的控件承载）。
constexpr int kAnchorColumn = 1;
/// 树标签列（呈现列——localName＋工程用语标签，UX-02）。
constexpr int kLabelColumn = 0;

/// 逐行呈现文字（行＝PropertyFieldRow——值/单位/使能/徽标的文本化）。
QString rowText(const PropertyFieldRow& row)
{
    QString text = QString::fromStdString(row.valueText);
    if (!row.unitText.empty()) {
        text += QStringLiteral(" ") + QString::fromStdString(row.unitText);  // UX-05：数值＋单位同显
    }
    return text;
}

/// 使能三态→控件只读位（灰显＝值可见不可改——§7.2/L-7 共用语义）。
bool rowReadOnly(FieldEnablement en) noexcept
{
    return en != FieldEnablement::Editable;
}

}  // namespace

// =====================================================================
// 构造与五区骨架
// =====================================================================

ModelingPanelWidget::ModelingPanelWidget(bool writable, QWidget* parent)
    : QWidget(parent), m_writable(writable)
{
    // 区位总布局：左右分栏（树｜属性）＋下部叠放（工具/就绪/预览——
    // 页签承载三区，对应 UX-09 五区布局的建模域投影）。
    auto* mainLayout = new QHBoxLayout(this);
    auto* left = new QVBoxLayout();
    auto* right = new QVBoxLayout();
    mainLayout->addLayout(left, 5);
    mainLayout->addLayout(right, 4);
    buildStructureTreePane(left);   // 区①建模结构树（左栏）
    buildPropertyPane(right);       // 区②属性编辑区（右栏）

    auto* tabs = new QTabWidget(this);
    auto* toolsPage = new QWidget(tabs);
    auto* readinessPage = new QWidget(tabs);
    auto* previewPage = new QWidget(tabs);
    buildToolsPane(new QVBoxLayout(toolsPage));        // 区③域工具区（StageId=modeling）
    buildReadinessPane(new QVBoxLayout(readinessPage));// 区④就绪与诊断条
    buildPreviewPane(new QVBoxLayout(previewPage));    // 区⑤预览页（仅已应用修订）
    tabs->addTab(toolsPage, QStringLiteral("工具"));
    tabs->addTab(readinessPage, QStringLiteral("就绪"));
    tabs->addTab(previewPage, QStringLiteral("预览"));
    right->addWidget(tabs, 1);

    // 就地状态行（错误/横幅——非模态呈现的唯一出口，置底部常驻）。
    m_statusLine = new QLabel(this);
    m_statusLine->setWordWrap(true);
    right->addWidget(m_statusLine);

    // 命令目录装载（§9.7.3 十条——装配数据，构造期一次；按钮使能态随
    // writable 与注入出口切换——见 refreshPanel/setWritable）。
    m_commands = modelingDomainCommands();
    for (std::size_t i = 0; i < m_commands.size(); ++i) {
        auto* btn = new QPushButton(QString::fromStdString(m_commands[i].titleKey), toolsPage);
        btn->setToolTip(QString::fromStdString(m_commands[i].id));  // 悬停显示命令 id（装配对账面）
        connect(btn, &QPushButton::clicked, this, &ModelingPanelWidget::onCommandButtonClicked);
        m_commandButtons.push_back(btn);
        // 命令按 readOnlyAllowed 分组布局（简单纵排——呈现密度非本层关切）。
        static_cast<QVBoxLayout*>(toolsPage->layout())->addWidget(btn);
    }
}

void ModelingPanelWidget::setCommandSubmit(CommandSubmitFn submitFn)
{
    m_commandSubmit = std::move(submitFn);
}

void ModelingPanelWidget::setEditTargetProvider(EditTargetProvider provider)
{
    m_editTarget = std::move(provider);
}

// ---- 区①建模结构树 ---------------------------------------------------

void ModelingPanelWidget::buildStructureTreePane(QVBoxLayout* left)
{
    left->addWidget(new QLabel(QStringLiteral("建模结构"), this));
    m_tree = new QTreeWidget(this);
    m_tree->setColumnCount(2);
    m_tree->setHeaderHidden(true);
    m_tree->hideColumn(kAnchorColumn);  // 锚列隐藏——UX-02：界面不见哈希/内部标识
    connect(m_tree, &QTreeWidget::itemSelectionChanged, this,
            &ModelingPanelWidget::onTreeSelectionChanged);
    left->addWidget(m_tree, 1);
}

// ---- 区②属性编辑区 ---------------------------------------------------

void ModelingPanelWidget::buildPropertyPane(QVBoxLayout* right)
{
    right->addWidget(new QLabel(QStringLiteral("属性"), this));
    auto* formHost = new QWidget(this);
    m_propertyForm = new QFormLayout(formHost);
    m_propertyForm->setLabelAlignment(Qt::AlignRight);
    right->addWidget(formHost);
}

// ---- 区③域工具区 -----------------------------------------------------

void ModelingPanelWidget::buildToolsPane(QVBoxLayout* bottom)
{
    bottom->setContentsMargins(0, 0, 0, 0);
    // 命令按钮在构造函数主体统一装载（目录来自 modelingDomainCommands）。
}

// ---- 区④就绪与诊断条 -------------------------------------------------

void ModelingPanelWidget::buildReadinessPane(QVBoxLayout* bottom)
{
    m_readinessCounts = new QLabel(this);  // 三组计数行（L0~L11 分层结果经逐项行呈现）
    bottom->addWidget(m_readinessCounts);
    m_readinessItems = new QTreeWidget(this);
    m_readinessItems->setHeaderHidden(true);
    m_readinessItems->setRootIsDecorated(false);
    bottom->addWidget(m_readinessItems, 1);
    // 逐项点击→定位跳转（L-1 反向半区——锚＝隐藏列的 ObjectId 规范文本）。
    connect(m_readinessItems, &QTreeWidget::itemClicked, this,
            [this](QTreeWidgetItem* item, int) {
                if (item == nullptr) { return; }
                const std::string anchor =
                    item->text(kAnchorColumn).toStdString();
                if (anchor.empty()) { return; }  // 无主体行（note）——不可定位
                const auto oid = core::ObjectId::tryFromCanonical(anchor);
                if (oid.has_value()) {
                    m_selection.locate(*oid);  // 树滚动＋三维高亮（经 sink——零修订）
                }
            });
}

// ---- 区⑤预览页 -------------------------------------------------------

void ModelingPanelWidget::buildPreviewPane(QVBoxLayout* bottom)
{
    m_preview = new QPlainTextEdit(this);
    m_preview->setReadOnly(true);  // 只读预览（内容仅来自 AppliedRevisionView——D-MDL-10）
    bottom->addWidget(m_preview, 1);
}

// =====================================================================
// 全面板刷新（事件驱动出口——零缓存：内容全部来自入参现取）
// =====================================================================

void ModelingPanelWidget::refreshPanel(const ModelingWorkingSet& ws,
                                       const ModelReadinessReport& report)
{
    m_threadGuard.assertOnUiThread();  // §3.4——刷新触点同样是编辑面

    // ---- 区①结构树：全量重建（投影行序＝呈现序；UX-02 标签已过守卫）----
    m_tree->blockSignals(true);  // 重建期的选中变化不回环（避免重投影风暴）
    m_tree->clear();
    const std::string lastAnchor =
        m_lastSelected.has_value() ? m_lastSelected->toCanonical() : std::string();
    for (const StructureNode& n : buildStructureTree(ws)) {
        auto* item = new QTreeWidgetItem(m_tree);
        item->setText(kLabelColumn, QString::fromStdString(n.displayLabel));
        item->setText(kAnchorColumn,
                      n.objectId.has_value() ? QString::fromStdString(n.objectId->toCanonical())
                                             : QString());
        item->setDisabled(!n.objectId.has_value());  // 分组行不可选（无锚——仅折叠呈现）
        // 选中保持：重建后按锚恢复当前行（L-1 选中态跨刷新恒定）。
        if (!lastAnchor.empty() && item->text(kAnchorColumn) == QString::fromStdString(lastAnchor)) {
            m_tree->setCurrentItem(item);
        }
    }
    m_tree->blockSignals(false);

    // ---- 区②属性区：按当前选中锚现取重投影（选中失效＝空态——不伪造行）----
    refreshPropertiesFromLastWorkingSet();

    // ---- 区④就绪条：三组计数＋逐项行（报告直投——零判定）----
    const ReadinessBarProjection bar = projectReadinessBar(report);
    m_readinessCounts->setText(QStringLiteral("阻断 %1 ｜ 警告 %2 ｜ 待确认 %3 ｜ 提示 %4")
                                   .arg(bar.counts.blockers)
                                   .arg(bar.counts.warnings)
                                   .arg(bar.counts.confirmables)
                                   .arg(bar.counts.notes));
    m_readinessItems->clear();
    for (const ReadinessItemRow& row : bar.items) {
        auto* item = new QTreeWidgetItem(m_readinessItems);
        item->setText(kLabelColumn,
                      QStringLiteral("[%1] %2")
                          .arg(QString::fromStdString(row.severity),
                               QString::fromStdString(row.summary)));
        item->setText(kAnchorColumn, row.jumpTarget.has_value()
                                         ? QString::fromStdString(row.jumpTarget->toCanonical())
                                         : QString());  // 无主体＝不可点击定位
        m_readinessItems->addTopLevelItem(item);
    }

    // ---- 区③命令使能态：writable=false 时 readOnlyAllowed=false 的按钮
    //      禁用（L-7 命令半区）；未注入提交出口的命令一律禁用（不虚构可达）。
    for (std::size_t i = 0; i < m_commandButtons.size(); ++i) {
        const bool readonlyBlocked = !m_writable && !m_commands[i].readOnlyAllowed;
        m_commandButtons[i]->setEnabled(m_commandSubmit != nullptr && !readonlyBlocked);
    }
}

void ModelingPanelWidget::setWritable(bool writable)
{
    m_writable = writable;
    // L-7 控件半区：现有属性行即时降级/恢复（行序同键——只变使能）。
    m_propertyRows = applyReadOnlyGate(m_propertyRows, m_writable);
    for (std::size_t i = 0; i < m_propertyEditors.size() && i < m_propertyRows.size(); ++i) {
        m_propertyEditors[i]->setReadOnly(rowReadOnly(m_propertyRows[i].enablement));
        m_propertyEditors[i]->setText(rowText(m_propertyRows[i]));
    }
    // 命令按钮使能态在下次 refreshPanel 统一重算（事件驱动——无即时轮询面）。
}

// =====================================================================
// 属性区投影与 L-2 编辑转接
// =====================================================================

void ModelingPanelWidget::refreshPropertiesFromLastWorkingSet()
{
    // 现取编辑目标（装配层会话工作集——面板零副本）；无会话＝空态。
    ModelingWorkingSet* ws = m_editTarget ? m_editTarget() : nullptr;
    m_propertyForm->removeRow(0);  // 清旧行（QFormLayout removeRow 逐行删除其控件）
    m_propertyEditors.clear();
    m_propertyRows.clear();
    if (ws == nullptr || !m_lastSelected.has_value()) { return; }

    const auto target = resolveSelection(*ws, *m_lastSelected);
    if (!target.has_value()) { return; }  // 闭包外身份——属性区空态（不伪造行）

    m_propertyRows = applyReadOnlyGate(propertyFieldsFor(*ws, *target), m_writable);
    for (const PropertyFieldRow& row : m_propertyRows) {
        auto* editor = new QLineEdit(this);
        editor->setText(rowText(row));
        editor->setReadOnly(rowReadOnly(row.enablement));  // 灰显＝§7.2 派生只读/L-7 只读会话
        connect(editor, &QLineEdit::editingFinished, this,
                &ModelingPanelWidget::onPropertyEditingFinished);
        m_propertyForm->addRow(QString::fromStdString(row.fieldKey), editor);
        m_propertyEditors.push_back(editor);
    }
}

void ModelingPanelWidget::onTreeSelectionChanged()
{
    // L-1 正向半区：树点击→会话选中态（零修订）→属性区重投影。
    const auto anchor = nodeAnchor(m_tree->currentItem());
    if (m_selection.select(anchor)) {
        m_lastSelected = anchor;
        Q_EMIT selectionChanged(anchor.has_value()
                                  ? QString::fromStdString(anchor->toCanonical())
                                  : QString());
        refreshPropertiesFromLastWorkingSet();
    }
}

void ModelingPanelWidget::onPropertyEditingFinished()
{
    m_threadGuard.assertOnUiThread();  // §3.4（editingFinished 只在 UI 线程——防御面）
    // 编辑目标现取（零缓存）；行→编辑意图的转接在本函数内完成（不缓存
    // 编辑意图——重演队列由刷新协调器经 recordPending 维护）。
    ModelingWorkingSet* ws = m_editTarget ? m_editTarget() : nullptr;
    if (ws == nullptr) { return; }  // 无会话编辑面——不虚构提交
    QLineEdit* senderEditor = qobject_cast<QLineEdit*>(sender());
    if (senderEditor == nullptr) { return; }
    // 行定位：编辑器指针与投影行序一致（m_propertyEditors 与 m_propertyRows
    // 同序 push——构造/刷新纪律）。
    std::size_t row = 0;
    while (row < m_propertyEditors.size() && m_propertyEditors[row] != senderEditor) { ++row; }
    if (row >= m_propertyRows.size()) { return; }

    PropertyFieldRow& rowRef = m_propertyRows[row];
    const std::string& key = rowRef.fieldKey;
    const QString text = senderEditor->text();

    // 先回显权威值——拒绝时保持原值（L-2"保留原值"的强顺序保证；接受时
    // 由 refreshPropertiesFromLastWorkingSet 重投影覆盖）。
    senderEditor->setText(rowText(rowRef));

    // 编辑面划界（MDL-07 表单最小版）：数值行（zero-offset）走单值编辑轨；
    // 复合行（bounds 双值/axis 向量/type 枚举/物性组）保留只读投影——其
    // 编辑走批量粘贴与域命令（面板不自行发明解析器——插件零计算逻辑）。
    if (key != "zero-offset") {
        EditRejection r;
        r.codeToken = "value-not-finite";
        r.detail = "该字段为复合行（" + key + "），请经批量粘贴或域命令编辑";
        onEditRejected(r);
        return;
    }
    bool ok = false;
    const double parsed = text.toDouble(&ok);
    if (!ok) {
        // 非数值输入：就地拒绝（UX-03——域函数未触，工作集未动）。
        EditRejection r;
        r.codeToken = "value-not-finite";
        r.detail = "输入不是数值：" + text.toStdString();
        onEditRejected(r);
        return;
    }

    // 选中锚→关节下标（L-1 关联键复用——编辑只对选中关节生效）。
    const auto target = resolveSelection(*ws, *m_lastSelected);
    if (!target.has_value() || target->kind != SelectedTarget::Kind::Joint) { return; }

    // L-2 提交（域裁决唯一——合法性全部在 applyJointFieldEdit 内）。
    const auto outcome = submitJointFieldEdit(*ws, *this, target->index,
                                              JointEditField::ZeroOffset, parsed);
    if (outcome == EditSubmitOutcome::Applied) {
        // 接受：入重演队列（L-4 保序重演的编辑意图）＋属性区即时重投影。
        PendingEdit e;
        e.jointIndex = target->index;
        e.field = JointEditField::ZeroOffset;
        e.value = parsed;
        m_refresh.recordPending(e);
        refreshPropertiesFromLastWorkingSet();
    }
}

void ModelingPanelWidget::onCommandButtonClicked()
{
    QPushButton* btn = qobject_cast<QPushButton*>(sender());
    if (btn == nullptr || !m_commandSubmit) { return; }
    // 命令激活→提交出口转发（id 点分小写——ui CommandRegistry 词表；
    // 装配层绑定 registry.submit——域命令语义归处理器族，面板零逻辑）。
    for (std::size_t i = 0; i < m_commandButtons.size(); ++i) {
        if (m_commandButtons[i] == btn) {
            m_commandSubmit(m_commands[i].id);
            return;
        }
    }
}

// =====================================================================
// IPanelEditSink（L-2/L-8 分流回调）
// =====================================================================

void ModelingPanelWidget::onEditApplied(const std::string& subjectPath)
{
    // 接受分支：树/属性区/就绪条的增量刷新由装配层刷新出口统一驱动
    // （事件驱动纪律——本回调只标记脏＋呈现；全面板刷新随⑤/手工触发）。
    notifySessionDirty();
    m_statusLine->setText(QString::fromStdString("已应用：" + subjectPath));
}

void ModelingPanelWidget::onEditRejected(const EditRejection& rejection)
{
    // 拒绝分支：就地呈现原因（非模态——UX-03/07）；值控件已回显权威值
    // （onPropertyEditingFinished 先回显后提交的强顺序保证）。
    m_statusLine->setText(QString::fromStdString("未应用（" + rejection.codeToken
                                                 + "）：" + rejection.detail));
}

void ModelingPanelWidget::notifySessionDirty()
{
    if (!m_dirty) {
        m_dirty = true;
        Q_EMIT sessionDirtyChanged(true);  // PM-04/PM-11——标题 `*`（装配层接 DraftController）
    }
}

// =====================================================================
// 辅助
// =====================================================================

std::optional<core::ObjectId> ModelingPanelWidget::nodeAnchor(QTreeWidgetItem* item) const
{
    if (item == nullptr) { return std::nullopt; }
    const std::string anchor = item->text(kAnchorColumn).toStdString();
    if (anchor.empty()) { return std::nullopt; }  // 分组行无锚
    return core::ObjectId::tryFromCanonical(anchor);
}

}  // namespace sdurws::ird::modeling
