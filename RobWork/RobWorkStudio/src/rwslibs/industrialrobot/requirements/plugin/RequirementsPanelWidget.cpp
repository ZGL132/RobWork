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
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWidget>

#include <sdurws/ird/ui/FlowLayout.hpp>  // 流式栅格（UI-T24 P3——命令条换行承载，钳制源 1 消除）
#include <sdurws/ird/ui/UiText.hpp>      // ui::resolveText（§3.5 唯一文案出口——按钮语义化 NFR-MNT-03）

#include <rw/math/Vector3D.hpp>  // rw::math::Vector3D（UI-T30 新增条目默认几何值）

#include <algorithm>  // std::min（删除锚回落位次钳制）
#include <set>        // uniqueEntryName 的已占名集合
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
    for (std::size_t i = 0; i < m_commandButtons.size(); ++i) {
        const auto a = m_commandAvailability ? m_commandAvailability(m_commands[i].id)
                                             : ui::CommandAvailability{};
        m_commandButtons[i]->setEnabled(
            m_commandSubmit != nullptr && (!m_commandAvailability || a.enabled));
    }
}

void RequirementsPanelWidget::setCommandAvailability(CommandAvailabilityFn availability)
{
    m_commandAvailability = std::move(availability);
    for (std::size_t i = 0; i < m_commandButtons.size(); ++i) {
        const auto a = m_commandAvailability ? m_commandAvailability(m_commands[i].id)
                                             : ui::CommandAvailability{};
        m_commandButtons[i]->setEnabled(
            m_commandSubmit != nullptr && (!m_commandAvailability || a.enabled));
        if (m_commandAvailability && !a.enabled && !a.disableReasonKey.empty()) {
            m_commandButtons[i]->setToolTip(QString::fromStdString(ui::resolveText(a.disableReasonKey)));
        }
    }
}

void RequirementsPanelWidget::setEditTargetProvider(EditTargetProvider provider)
{
    m_editTarget = std::move(provider);
}

void RequirementsPanelWidget::setPostEditAction(PostEditAction action)
{
    m_postEditAction = std::move(action);
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
    // 域命令区（UI-T24 P3 重排）：九条命令按钮＋三个两级撤销按钮，流式
    // 栅格换行排布——原单行 QHBoxLayout 把 12 个按钮的最小宽度之和
    // （2074 px）顶成整列 Dock 的最小宽，左列收不下去、中央三维视图被挤
    // 至 18 px（钳制源 1——定位记录 traceability/builds/ui-t24/
    // clamp-source.md；流式栅格使行最小宽坍缩为单按钮最宽 ≈200 px）。
    // 文案（UX-02/NFR-MNT-03——F-430 家族需求域一族消账）：按钮标题经
    // UiText 键族①b 解析（cmd.<id>.title——§3.5 键约定，值在 UiText.cpp
    // 登记）；resolveText 缺键即 fail-fast 上抛＝键表完备性的构造期保证，
    // 面板侧零第二文案源（原"按钮暂以 id 呈现"过渡态就此消账）。
    auto* bar = new QWidget(top);
    auto* barLayout = new QVBoxLayout(bar);
    barLayout->setContentsMargins(0, 0, 0, 0);
    barLayout->setSpacing(2);
    // 无父对象构造（子布局形态——由 addLayout 收养，见下方安装处注释）。
    auto* flow = new ui::FlowLayout(nullptr, /*hSpacing=*/4, /*vSpacing=*/2);
    // 命令条语义分组分隔（UI-T25——九条目录命令按 §9.8 语义四组呈现：
    // 导入导出｜几何采集｜模板。分隔符用 QFrame 竖线（非 QPushButton——
    // 冒烟通道按 findChildren<QPushButton> 枚举九键对照，分隔符不入
    // 按钮集合、不改按钮挂位与顺序，仅呈现分组）。第四组（编辑——两级
    // 撤销三按钮）在目录循环后的追加段，组前另起一条分隔符。
    auto makeGroupSeparator = [bar]() {
        auto* sep = new QFrame(bar);
        sep->setFrameShape(QFrame::VLine);
        sep->setFrameShadow(QFrame::Sunken);
        sep->setFixedHeight(20);  // 竖线与按钮行等高（≈标准按钮高度）
        return sep;
    };
    // 分组首命令 id（词表——§9.8 命令表行序；命中即在按钮前加分隔）。
    const char* const groupLeaders[] = {"requirements.capture-tcp",
                                        "requirements.apply-template"};
    for (const ui::CommandDescriptor& d : m_commands) {
        for (const char* leader : groupLeaders) {
            if (d.id == leader) {
                flow->addWidget(makeGroupSeparator());
            }
        }
        auto* btn =
            new QPushButton(QString::fromStdString(ui::resolveText(d.titleKey)),
                            bar);
        btn->setToolTip(QString::fromStdString(d.menuPath));
        connect(btn, &QPushButton::clicked, this, &RequirementsPanelWidget::onCommandButtonClicked);
        flow->addWidget(btn);
        m_commandButtons.push_back(btn);
    }
    // 编辑组分隔符（目录九命令与两级撤销三按钮之间的强分隔——撤销族是
    // 编辑会话语义，与录入/模板类命令分属不同操作面向）。
    flow->addWidget(makeGroupSeparator());
    // 两级撤销三按钮（L-R4——草稿级撤销/重做与项目级撤销三处独立控件，
    // 不合并：草稿级动编辑器局部栈，项目级纯转发 ui 命令——语义不混用）。
    m_draftUndoButton = new QPushButton(QString::fromStdString("撤销本次编辑（草稿级）"), bar);
    m_draftRedoButton = new QPushButton(QString::fromStdString("重做本次编辑（草稿级）"), bar);
    m_projectUndoButton = new QPushButton(QString::fromStdString("撤销上次应用（项目级）"), bar);
    connect(m_draftUndoButton, &QPushButton::clicked, this, &RequirementsPanelWidget::onDraftUndo);
    connect(m_draftRedoButton, &QPushButton::clicked, this, &RequirementsPanelWidget::onDraftRedo);
    connect(m_projectUndoButton, &QPushButton::clicked, this, &RequirementsPanelWidget::onProjectUndo);
    flow->addWidget(m_draftUndoButton);
    flow->addWidget(m_draftRedoButton);
    flow->addWidget(m_projectUndoButton);
    // 以"无父对象构造＋addLayout"的子布局规范形态安装（Qt 布局树纪律：
    // 带 QWidget 父对象构造的是顶层布局候选、配 setLayout 使用——内容装配
    // 层 buildTopBar 同款；子布局必须无父构造后经父布局 addLayout 收养，
    // 收养时控件重挂到布局宿主 bar——漏收养＝布局永不执行、按钮滞留原点
    // 几何，首轮冒烟实证）。
    barLayout->addLayout(flow);
    // 状态行（就地错误/警告/摘要——非模态呈现，UX-03/07）：独立于按钮行
    // 之下一行占位（流式栅格按控件排布，长摘要文本混入按钮行会挤占换行）。
    m_statusLine = new QLabel(bar);
    m_statusLine->setWordWrap(true);  // 长摘要换行承载（非模态呈现不挤压按钮行）
    barLayout->addWidget(m_statusLine);

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

    // 自持导航 deprecated 标记（WP-14-T10——B1-SPEC §5.2 迁移期双形态
    // 并存：共享工业项目树已承载本域导航，自持树标记 deprecated 但保留
    // 可用，删除归 WP-24-T09；objectName 供 GUI 验证定位）。
    // UI-T25 文案治理：呈现文本改经 UiText 键解析（panel.requirements.
    // self-nav.note）——原字面量直出携带内部任务编号（WP-24-T09）与开发
    // 术语（deprecated），属 UX-02 内部名泄漏（F-430/F-432 家族标签一族
    // 消账）；编号与退役编排只留在本注释与设计文档，用户见工程化中文。
    m_navDeprecationLabel = new QLabel(left);
    m_navDeprecationLabel->setObjectName("requirementsNavDeprecationLabel");
    m_navDeprecationLabel->setWordWrap(true);
    m_navDeprecationLabel->setText(
        QString::fromStdString(
            ui::resolveText(ui::TextKey("panel.requirements.self-nav.note"))));
    leftLayout->addWidget(m_navDeprecationLabel);

    m_tree = makeTable(left, QStringList() << "需求对象");
    connect(m_tree, &QTreeWidget::itemSelectionChanged, this,
            &RequirementsPanelWidget::onTreeSelectionChanged);
    leftLayout->addWidget(m_tree);
}

void RequirementsPanelWidget::buildStationPage(QTabWidget* pages)
{
    // 右栏页①工位（检查器表单——行＝stationFieldsFor 投影）。
    // UI-T26 页签状态行：页头呈现『工位：〈对象名|未选择对象〉』随树
    // 选中刷新（updateTabHeaders）——空态不再是无信息空白。
    auto* page = new QWidget(pages);
    auto* lay = new QVBoxLayout(page);
    m_stationHeader = new QLabel(QStringLiteral("工位：未选择对象"), page);
    m_stationHeader->setObjectName("ird_req_tab_station_header");
    lay->addWidget(m_stationHeader);
    // 对象生命周期工具行（UI-T30 B1——新增/复制/删除三键）。
    lay->addWidget(makeLifecycleBar(page, "points", WorkingSetMember::Points,
                                    QStringLiteral("工位")));
    m_stationForm = new QFormLayout;
    lay->addLayout(m_stationForm);
    lay->addStretch(1);
    pages->addTab(page, "工位");
}

void RequirementsPanelWidget::buildRegionPage(QTabWidget* pages)
{
    // 右栏页②区域（表＋检查器＋预览摘要——§9.8 面板表第 3 行）＋
    // UI-T26 页签状态行（同工位页）。
    auto* page = new QWidget(pages);
    auto* lay = new QVBoxLayout(page);
    m_regionHeader = new QLabel(QStringLiteral("区域：未选择对象"), page);
    m_regionHeader->setObjectName("ird_req_tab_region_header");
    lay->addWidget(m_regionHeader);
    // 对象生命周期工具行（UI-T30 B1）。
    lay->addWidget(makeLifecycleBar(page, "regions", WorkingSetMember::Regions,
                                    QStringLiteral("区域")));
    m_regionTable = makeTable(page, QStringList() << "区域" << "采样" << "覆盖目标");
    m_regionTable->setObjectName(QStringLiteral("ird_req_region_table"));  // 验证定位锚
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
    // 右栏页③工况（表＋检查器＋必验清单预览——§9.8 面板表第 4 行）＋
    // UI-T26 页签状态行（同工位页）。
    auto* page = new QWidget(pages);
    auto* lay = new QVBoxLayout(page);
    m_conditionHeader = new QLabel(QStringLiteral("工况：未选择对象"), page);
    m_conditionHeader->setObjectName("ird_req_tab_condition_header");
    lay->addWidget(m_conditionHeader);
    // 对象生命周期工具行（UI-T30 B1）。
    lay->addWidget(makeLifecycleBar(page, "conditions",
                                    WorkingSetMember::Conditions,
                                    QStringLiteral("工况")));
    m_conditionTable = makeTable(page, QStringList() << "工况" << "节拍" << "适用范围");
    m_conditionTable->setObjectName(QStringLiteral("ird_req_condition_table"));  // 验证定位锚
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
    // UI-T26 页签状态行：『校验：尚未执行』诚实静态——动态校验状态随
    // 校验呈现任务接入（计数标签 m_validationCounts 的既有刷新面不动）。
    auto* page = new QWidget(pages);
    auto* lay = new QVBoxLayout(page);
    m_validationHeader = new QLabel(QStringLiteral("校验：尚未执行"), page);
    m_validationHeader->setObjectName("ird_req_tab_validation_header");
    lay->addWidget(m_validationHeader);
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
// 对象生命周期（UI-T30 B1——工位/区域/工况新增/复制/删除）
// =====================================================================

QWidget* RequirementsPanelWidget::makeLifecycleBar(QWidget* parent,
                                                  const char* key,
                                                  WorkingSetMember member,
                                                  const QString& noun)
{
    // 三键工具行（页签内局部操作——不进顶部域命令条：九条域命令目录
    // 为装配数据冻结面，结构操作是面板编排层）。流式栅格（FlowLayout
    // ——命令条同构件）：窄窗下三键自动换行，不撑大主 Dock 最小宽
    // （UI-T24 中央区保护的钳制纪律，B1 冒烟实证回归的对照修复）。
    auto* bar = new QWidget(parent);
    auto* lay = new ui::FlowLayout(bar, /*hSpacing=*/4, /*vSpacing=*/2);
    struct Row { const char* action; const char* verb; };
    const Row rows[] = {
        {"add", "新增"}, {"duplicate", "复制"}, {"remove", "删除"}};
    for (const auto& row : rows) {
        auto* btn = new QPushButton(
            QStringLiteral("%1%2").arg(QString::fromUtf8(row.verb), noun), bar);
        // objectName 锚：ird_req_<action>_<key>——gui_test findChild 定位面。
        btn->setObjectName(QStringLiteral("ird_req_%1_%2")
                               .arg(QString::fromLatin1(row.action),
                                    QString::fromLatin1(key)));
        // 会话可用性标记（refreshPanel 统一刷新——无会话禁用，不虚构可编辑）。
        btn->setProperty("irdLifecycle", true);
        const WorkingSetMember m = member;
        if (std::string_view(row.action) == "add") {
            connect(btn, &QPushButton::clicked, this,
                    [this, m] { onAddEntry(m); });
        } else if (std::string_view(row.action) == "duplicate") {
            connect(btn, &QPushButton::clicked, this,
                    [this, m] { onDuplicateEntry(m); });
        } else {
            connect(btn, &QPushButton::clicked, this,
                    [this, m] { onRemoveEntry(m); });
        }
        lay->addWidget(btn);
    }
    return bar;
}

std::string RequirementsPanelWidget::uniqueEntryName(
    const RequirementWorkingSet& ws, WorkingSetMember member,
    const std::string& base)
{
    // 名字集合现取（集合内唯一 I-REQ-3——Editor 拒绝面前的第一道防线；
    // 撞名仍以域侧裁决为准，双保险）。
    std::set<std::string> taken;
    if (member == WorkingSetMember::Points) {
        for (const TaskPoint& e : ws.points.entries) { taken.insert(e.name); }
    } else if (member == WorkingSetMember::Regions) {
        for (const WorkRegion& e : ws.regions.entries) { taken.insert(e.name); }
    } else {
        for (const OperatingCondition& e : ws.conditions.entries) {
            taken.insert(e.name);
        }
    }
    std::string candidate = base;
    for (int suffix = 2; taken.count(candidate) != 0U; ++suffix) {
        candidate = base + " " + std::to_string(suffix);
    }
    return candidate;
}

bool RequirementsPanelWidget::submitStructuralEdit(const RequirementEdit& edit)
{
    m_threadGuard.assertOnUiThread();
    IRequirementEditor* editor = m_editTarget ? m_editTarget() : nullptr;
    if (editor == nullptr) {
        m_statusLine->setText(QStringLiteral("未应用：未打开需求会话（不可编辑）"));
        return false;
    }
    const EditOutcome out = editor->applyEdit(edit);
    if (!out.accepted) {
        // 拒绝：就地错误（非模态——UX-03/07；工作集字节未动）。
        m_statusLine->setText(QString::fromStdString(
            "未应用（" + out.error.detail + "）"));
        return false;
    }
    // 接受：走既有 sink 链（onEditApplied）——脏标记＋全面板重投影＋
    // UI-T29 组合子就绪重估；结构操作与字段编辑同一条刷新轨。
    onEditApplied(out.changeSummary);
    return true;
}

void RequirementsPanelWidget::onAddEntry(WorkingSetMember member)
{
    IRequirementEditor* editor = m_editTarget ? m_editTarget() : nullptr;
    if (editor == nullptr) {
        m_statusLine->setText(QStringLiteral("未应用：未打开需求会话（不可编辑）"));
        return;
    }
    const RequirementWorkingSet& ws = editor->workingSet();
    // 合法起步条目（域夹具同款默认值——面板只做编排，合法性最终由域侧
    // applyEdit 裁决；自动名防撞见 uniqueEntryName）。
    if (member == WorkingSetMember::Points) {
        TaskPoint p;
        p.objectId = core::ObjectId::generate();
        p.name = uniqueEntryName(ws, member, "工位");
        p.pose.constrainedDof.z = true;
        p.pose.position =
            core::SourcedValue<rw::math::Vector3D<double>>::provided(
                rw::math::Vector3D<double>(0.0, 0.0, 0.0),
                core::ValueProvenance::make(core::ProvenanceKind::UserProvided));
        p.work = TaskSegment{true, SegmentAxis::ToolZ, 1.0};
        if (submitStructuralEdit(RequirementEdit{p})) { focusObject(p.objectId); }
    } else if (member == WorkingSetMember::Regions) {
        WorkRegion r;
        r.objectId = core::ObjectId::generate();
        r.name = uniqueEntryName(ws, member, "区域");
        r.box = BoundingBox{rw::math::Vector3D<double>(1.0, 1.0, 1.0),
                            rw::math::Vector3D<double>(1.0, 1.0, 1.0)};
        r.positionSampling = PositionSampling{
            PositionSamplingMethod::Grid, {2, 2, 2}, {0, 0, 0}, 0};
        if (submitStructuralEdit(RequirementEdit{r})) { focusObject(r.objectId); }
    } else {
        OperatingCondition c;
        c.objectId = core::ObjectId::generate();
        c.name = uniqueEntryName(ws, member, "工况");
        if (submitStructuralEdit(RequirementEdit{c})) { focusObject(c.objectId); }
    }
}

void RequirementsPanelWidget::onDuplicateEntry(WorkingSetMember member)
{
    IRequirementEditor* editor = m_editTarget ? m_editTarget() : nullptr;
    if (editor == nullptr || !m_lastSelected.has_value()) {
        m_statusLine->setText(QStringLiteral(
            "未应用：复制需要先选中一个条目"));
        return;
    }
    const RequirementWorkingSet& ws = editor->workingSet();
    if (member == WorkingSetMember::Points) {
        for (const TaskPoint& src : ws.points.entries) {
            if (src.objectId != m_lastSelected.value()) { continue; }
            TaskPoint copy = src;  // 深拷贝（值类型）——溯源字段保持源值
            copy.objectId = core::ObjectId::generate();
            copy.name = uniqueEntryName(ws, member, src.name + " 副本");
            if (submitStructuralEdit(RequirementEdit{copy})) {
                focusObject(copy.objectId);
            }
            return;
        }
    } else if (member == WorkingSetMember::Regions) {
        for (const WorkRegion& src : ws.regions.entries) {
            if (src.objectId != m_lastSelected.value()) { continue; }
            WorkRegion copy = src;
            copy.objectId = core::ObjectId::generate();
            copy.name = uniqueEntryName(ws, member, src.name + " 副本");
            if (submitStructuralEdit(RequirementEdit{copy})) {
                focusObject(copy.objectId);
            }
            return;
        }
    } else {
        for (const OperatingCondition& src : ws.conditions.entries) {
            if (src.objectId != m_lastSelected.value()) { continue; }
            OperatingCondition copy = src;
            copy.objectId = core::ObjectId::generate();
            copy.name = uniqueEntryName(ws, member, src.name + " 副本");
            if (submitStructuralEdit(RequirementEdit{copy})) {
                focusObject(copy.objectId);
            }
            return;
        }
    }
    m_statusLine->setText(QStringLiteral("未应用：选中对象不属于该集合"));
}

void RequirementsPanelWidget::onRemoveEntry(WorkingSetMember member)
{
    IRequirementEditor* editor = m_editTarget ? m_editTarget() : nullptr;
    if (editor == nullptr || !m_lastSelected.has_value()) {
        m_statusLine->setText(QStringLiteral(
            "未应用：删除需要先选中一个条目"));
        return;
    }
    // 删除前记录集合内索引（规范序）——接受后的锚回落取同位次条。
    std::size_t removedIndex = 0;
    if (member == WorkingSetMember::Points) {
        for (std::size_t i = 0; i < editor->workingSet().points.entries.size(); ++i) {
            if (editor->workingSet().points.entries[i].objectId
                    == m_lastSelected.value()) {
                removedIndex = i;
                break;
            }
        }
    } else if (member == WorkingSetMember::Regions) {
        for (std::size_t i = 0;
             i < editor->workingSet().regions.entries.size(); ++i) {
            if (editor->workingSet().regions.entries[i].objectId
                    == m_lastSelected.value()) {
                removedIndex = i;
                break;
            }
        }
    } else {
        for (std::size_t i = 0;
             i < editor->workingSet().conditions.entries.size(); ++i) {
            if (editor->workingSet().conditions.entries[i].objectId
                    == m_lastSelected.value()) {
                removedIndex = i;
                break;
            }
        }
    }
    if (!submitStructuralEdit(removeEdit(m_lastSelected.value(), member))) {
        return;  // 拒绝（如必验引用等域拒绝面）——就地错误已呈现
    }
    // 锚回落：删除后同集合取同位次（越界取末条）条目；空集合＝清空选中。
    const RequirementWorkingSet& after = editor->workingSet();
    if (member == WorkingSetMember::Points) {
        if (after.points.entries.empty()) {
            focusObject(std::nullopt);
        } else {
            const std::size_t idx = std::min(removedIndex,
                                             after.points.entries.size() - 1);
            focusObject(after.points.entries[idx].objectId);
        }
    } else if (member == WorkingSetMember::Regions) {
        if (after.regions.entries.empty()) {
            focusObject(std::nullopt);
        } else {
            const std::size_t idx = std::min(removedIndex,
                                             after.regions.entries.size() - 1);
            focusObject(after.regions.entries[idx].objectId);
        }
    } else {
        if (after.conditions.entries.empty()) {
            focusObject(std::nullopt);
        } else {
            const std::size_t idx = std::min(removedIndex,
                                             after.conditions.entries.size() - 1);
            focusObject(after.conditions.entries[idx].objectId);
        }
    }
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
    // 生命周期工具行可用性（UI-T30——与撤销键同源事实：无会话/只读＝
    // 禁用，不虚构可编辑；属性过滤定位，零成员持有）。
    for (QPushButton* btn : findChildren<QPushButton*>()) {
        if (btn->property("irdLifecycle").toBool()) {
            btn->setEnabled(m_editTarget != nullptr && m_writable);
        }
    }
    for (std::size_t i = 0; i < m_commandButtons.size(); ++i) {
        const auto a = m_commandAvailability ? m_commandAvailability(m_commands[i].id)
                                             : ui::CommandAvailability{};
        m_commandButtons[i]->setEnabled(
            m_commandSubmit != nullptr && (!m_commandAvailability || a.enabled));
        if (m_commandAvailability && !a.enabled && !a.disableReasonKey.empty()) {
            m_commandButtons[i]->setToolTip(QString::fromStdString(ui::resolveText(a.disableReasonKey)));
        }
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

void RequirementsPanelWidget::focusObject(const std::optional<core::ObjectId>& oid)
{
    m_threadGuard.assertOnUiThread();  // §3.4——选中态是会话对象（UI 线程）

    // 无目标（多选/清空选中）或闭包外身份＝仅清除：会话选中锚置空（幂等
    // ——重复清除不抖动），树清当前行。不伪造定位（闭包外对象在本域
    // 自持树中无行——renderTree 只承载工作集条目）。
    if (!oid.has_value()) {
        if (m_selection.select(std::nullopt)) {
            m_lastSelected.reset();
            m_tree->clearSelection();
            m_tree->setCurrentItem(nullptr);
            // 页签状态行同步回落（UI-T30——clear() 的选中变更信号在部分
            // 时序下不触发刷新，此处显式收口：清除后三页头一致回
            // 『未选择对象』，不残留已删对象名）。
            updateTabHeaders();
        }
        return;
    }

    // 自持树滚动定位：线性扫描顶层行锚（renderTree 行序＝投影序——与
    // treeRowIndexFor 同一线性语义；需求树无嵌套行）。命中＝置当前行
    // （触发行选中→onTreeSelectionChanged→检查器重投影——L-R1 既有数据
    // 流复用，零新增刷新路径）；未命中＝清除选中（对象已删除/他路编辑
    // 后的漂移——不伪造定位）。
    const QString anchorText = QString::fromStdString(oid.value().toCanonical());
    QTreeWidgetItem* target = nullptr;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        if (m_tree->topLevelItem(i)->text(kAnchorColumn) == anchorText) {
            target = m_tree->topLevelItem(i);
            break;
        }
    }
    if (target == nullptr) {
        if (m_selection.select(std::nullopt)) {
            m_lastSelected.reset();
            m_tree->clearSelection();
            m_tree->setCurrentItem(nullptr);
        }
        return;
    }
    // setCurrentItem 触发 itemSelectionChanged→onTreeSelectionChanged→
    // select(anchor)——会话态与检查器刷新走既有单一路径；scrollToItem
    // 保证可视（定位是呈现动作）。
    m_tree->scrollToItem(target, QAbstractItemView::EnsureVisible);
    m_tree->setCurrentItem(target);
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
        // 记录本次投影的权威文本。editingFinished 可能由焦点切换触发，
        // 仅当文本真的改变时才进入编辑流，避免刷新/重投影造成幻影脏化。
        edit->setProperty("irdAuthoritativeValue",
                         QString::fromStdString(
                             r.valueText == "未提供" || r.valueText == "未设"
                                 ? std::string{}
                                 : r.valueText));
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
    const std::optional<core::ObjectId> previousRegion =
        m_regionTable->currentItem() ? nodeAnchor(m_regionTable->currentItem()) : std::nullopt;
    // 区域表（一区域一行——L-R1 行选中锚）。
    const std::vector<RegionRow> rows = regionRows(ws.regions.entries);
    m_regionTable->clear();
    for (const RegionRow& r : rows) {
        auto* item = makeRow(QString::fromStdString(r.name),
                             QString::fromStdString(r.samplingText), r.objectId);
        m_regionTable->addTopLevelItem(item);
        if (previousRegion.has_value() && r.objectId == previousRegion.value()) {
            m_regionTable->setCurrentItem(item);
        }
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
            auto* edit = new QLineEdit(QString::fromStdString(
                fr.valueText == "未提供" || fr.valueText == "未设"
                    ? std::string{}
                    : fr.valueText), this);
            // B2（UI-T31）：数值行按 enablement 可编（specs 词表行——
            // 编辑提交轨见 onRegionFieldEditingFinished；非词表行〔名称/
            // 等级等〕保持只读呈现——本批只解除数值字段的诚实降级）。
            const bool editable = fr.enablement == StationFieldEnablement::Editable;
            edit->setReadOnly(!editable);
            edit->setProperty("irdFieldKey", QString::fromStdString(fr.fieldKey));
            // 权威文本（幻影脏化短路基准——工位轨同款；占位值以空串为
            // 基准，输入即变更进入编辑流）。
            edit->setProperty("irdAuthoritativeValue",
                              QString::fromStdString(
                                  fr.valueText == "未提供" || fr.valueText == "未设"
                                      ? std::string{}
                                      : fr.valueText));
            if (editable) {
                connect(edit, &QLineEdit::editingFinished, this,
                        &RequirementsPanelWidget::onRegionFieldEditingFinished);
            }
            m_regionForm->addRow(QString::fromStdString(fr.label), edit);
        }
        break;  // 至多一个选中区域——命中即止（确定性）
    }
    // 区域轮廓与采样格预览（呈现几何——regionPreviewGeometry；结果着色归
    // KIN-07，本预览零结果语义：仅文本摘要＋可选 View3D 投递）。
    if (!ws.regions.entries.empty()) {
        const WorkRegion* selected = nullptr;
        if (m_regionTable->currentItem() != nullptr) {
            const auto anchor = nodeAnchor(m_regionTable->currentItem());
            if (anchor.has_value()) {
                for (const WorkRegion& region : ws.regions.entries) {
                    if (region.objectId == anchor.value()) {
                        selected = &region;
                        break;
                    }
                }
            }
        }
        const WorkRegion& previewRegion = selected != nullptr
                                              ? *selected : ws.regions.entries.front();
        const RegionPreviewGeometry geo = regionPreviewGeometry(previewRegion, m_regionService);
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
    // 工况表（一工况一行）。选中锚跨 rebuild 保持（B2——区域页
    // previousRegion 同款：clear 前保存、rebuild 后恢复；缺恢复＝检查器
    // 在每次刷新后恒空〔既有缺陷，B2 检查器激活路径的修复〕）。
    const std::optional<core::ObjectId> previousCondition =
        m_conditionTable->currentItem()
            ? nodeAnchor(m_conditionTable->currentItem()) : std::nullopt;
    const std::vector<ConditionRow> rows = conditionRows(ws.conditions.entries);
    m_conditionTable->clear();
    for (const ConditionRow& r : rows) {
        auto* item = makeRow(QString::fromStdString(r.name),
                             QString::fromStdString(r.cycleText),
                             r.objectId);
        m_conditionTable->addTopLevelItem(item);
        if (previousCondition.has_value() && r.objectId == previousCondition.value()) {
            m_conditionTable->setCurrentItem(item);
        }
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
                        auto* edit = new QLineEdit(QString::fromStdString(
                            fr.valueText == "未提供" || fr.valueText == "未设"
                                ? std::string{}
                                : fr.valueText), this);
                        // B2（UI-T31）：数值行按 enablement 可编（specs 词
                        // 表行——onConditionFieldEditingFinished 提交轨）；
                        // 派生/引用行保持只读（区域页同款收窄）。
                        const bool editable =
                            fr.enablement == StationFieldEnablement::Editable;
                        edit->setReadOnly(!editable);
                        edit->setProperty("irdFieldKey",
                                          QString::fromStdString(fr.fieldKey));
                        edit->setProperty("irdAuthoritativeValue",
                                          QString::fromStdString(
                                              fr.valueText == "未提供"
                                                      || fr.valueText == "未设"
                                                  ? std::string{}
                                                  : fr.valueText));
                        if (editable) {
                            connect(edit, &QLineEdit::editingFinished, this,
                                    &RequirementsPanelWidget::
                                        onConditionFieldEditingFinished);
                        }
                        m_conditionForm->addRow(QString::fromStdString(fr.label),
                                                edit);
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

void RequirementsPanelWidget::updateTabHeaders()
{
    // 页签状态行刷新（UI-T26）：对象名＝当前树选中行的显示文本（与用户
    // 在树里看到的名字同源——树行文本即 displayLabel 投影，零第二名称
    // 源）；未选中＝『未选择对象』。校验页头恒『尚未执行』（动态校验
    // 状态归校验呈现任务——诚实静态，不虚构已执行）。
    const QString objectName =
            (m_tree != nullptr && m_tree->currentItem() != nullptr)
                ? m_tree->currentItem()->text(0)
                : QString();
    const QString suffix =
            objectName.isEmpty() ? QStringLiteral("未选择对象") : objectName;
    if (m_stationHeader != nullptr) {
        m_stationHeader->setText(QStringLiteral("工位：") + suffix);
    }
    if (m_regionHeader != nullptr) {
        m_regionHeader->setText(QStringLiteral("区域：") + suffix);
    }
    if (m_conditionHeader != nullptr) {
        m_conditionHeader->setText(QStringLiteral("工况：") + suffix);
    }
    if (m_validationHeader != nullptr) {
        m_validationHeader->setText(QStringLiteral("校验：尚未执行"));
    }
}

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
    updateTabHeaders();
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
    if (edit->text() == edit->property("irdAuthoritativeValue").toString()) {
        return;  // 焦点切换/刷新回调未改变值，不进入编辑流也不增加脏标记。
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

void RequirementsPanelWidget::onRegionFieldEditingFinished()
{
    // B2（UI-T31）区域轨——工位槽同构：权威值短路→词表 spec 匹配→
    // parseFieldValueText（唯一解析出口，SA-12）→applyRegionEditSet→
    // submitEntryEdit 域裁决（接受/拒绝分流见 sink；四态语义与工位一致）。
    m_threadGuard.assertOnUiThread();
    IRequirementEditor* editor = m_editTarget ? m_editTarget() : nullptr;
    if (editor == nullptr || !m_writable) {
        return;  // 无会话/只读——不虚构可编辑
    }
    auto* edit = qobject_cast<QLineEdit*>(QObject::sender());
    if (edit == nullptr) {
        return;
    }
    if (edit->text() == edit->property("irdAuthoritativeValue").toString()) {
        return;  // 未修改短路（幻影脏化消除——T27 语义跨域一致）
    }
    const std::string key = edit->property("irdFieldKey").toString().toStdString();
    const RequirementWorkingSet& ws = editor->workingSet();
    for (const WorkRegion& region : ws.regions.entries) {
        // 区域检查器绑定区域表选中行（renderRegionPage 的投影锚）。
        std::optional<core::ObjectId> selected;
        if (m_regionTable->currentItem() != nullptr) {
            selected = nodeAnchor(m_regionTable->currentItem());
        }
        if (!selected.has_value() || region.objectId != selected.value()) {
            continue;
        }
        for (const ui::QuantityFieldSpec& spec : regionQuantitySpecs()) {
            if (spec.key != key) {
                continue;
            }
            const ui::ValueParseResult parsed =
                parseFieldValueText(edit->text().toStdString(), spec);
            if (!parsed.ok) {
                // 拒绝·解析面：就地错误＋回退权威值（重投影）。
                m_statusLine->setText(QString::fromStdString(parsed.reason));
                renderRegionPage(ws);
                return;
            }
            ui::ParamEditSet edits;
            ui::ParamChange change;
            change.key = key;
            change.label = spec.label;
            change.newSi = parsed.siValue;
            edits.changes.push_back(change);
            std::vector<std::string> known;
            const WorkRegion candidate = applyRegionEditSet(region, edits, known);
            const EditSubmitOutcome outcome =
                submitEntryEdit(*editor, *this, RequirementEdit{candidate});
            if (outcome == EditSubmitOutcome::Applied) {
                m_undoTracker.recordAppliedEdit();  // L-R4 记账
            }
            return;
        }
        // 词表外键＝实现缺陷（行模型与 specs 词表漂移）——fail-fast。
        throw std::logic_error("区域面板：编辑行携带词表外键 " + key);
    }
}

void RequirementsPanelWidget::onConditionFieldEditingFinished()
{
    // B2（UI-T31）工况轨——同构（applyConditionEditSet 回填）。
    m_threadGuard.assertOnUiThread();
    IRequirementEditor* editor = m_editTarget ? m_editTarget() : nullptr;
    if (editor == nullptr || !m_writable) {
        return;
    }
    auto* edit = qobject_cast<QLineEdit*>(QObject::sender());
    if (edit == nullptr) {
        return;
    }
    if (edit->text() == edit->property("irdAuthoritativeValue").toString()) {
        return;
    }
    const std::string key = edit->property("irdFieldKey").toString().toStdString();
    const RequirementWorkingSet& ws = editor->workingSet();
    if (m_conditionTable->currentItem() == nullptr) {
        return;
    }
    const auto selected = nodeAnchor(m_conditionTable->currentItem());
    if (!selected.has_value()) {
        return;
    }
    for (const OperatingCondition& c : ws.conditions.entries) {
        if (c.objectId != selected.value()) {
            continue;
        }
        for (const ui::QuantityFieldSpec& spec : conditionQuantitySpecs(c)) {
            if (spec.key != key) {
                continue;
            }
            const ui::ValueParseResult parsed =
                parseFieldValueText(edit->text().toStdString(), spec);
            if (!parsed.ok) {
                m_statusLine->setText(QString::fromStdString(parsed.reason));
                renderConditionPage(ws);
                return;
            }
            ui::ParamEditSet edits;
            ui::ParamChange change;
            change.key = key;
            change.label = spec.label;
            change.newSi = parsed.siValue;
            edits.changes.push_back(change);
            std::vector<std::string> known;
            const OperatingCondition candidate =
                applyConditionEditSet(c, edits, known);
            const EditSubmitOutcome outcome =
                submitEntryEdit(*editor, *this, RequirementEdit{candidate});
            if (outcome == EditSubmitOutcome::Applied) {
                m_undoTracker.recordAppliedEdit();
            }
            return;
        }
        throw std::logic_error("工况面板：编辑行携带词表外键 " + key);
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
    // UI-T29 最小校验：编辑后动作（装配层在此重估就绪并以新报告
    // refreshPanel——校验页『尚未执行』静态的实时化编排；本面板零判定，
    // 判定权威在域侧 Readiness——P-REQ-6 边界不变）。
    if (m_postEditAction) {
        m_postEditAction();
    }
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
