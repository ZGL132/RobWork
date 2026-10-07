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
#include "RequirementsCommandFlows.hpp"  // 域命令 UI 流程装配（UI-T32 C 批次＋UI-T33 三维缝重载）

#include <QBrush>
#include <QAction>   // 导入下拉菜单动作（返工⑤）
#include <QButtonGroup>  // 自由度分段互斥组（UI-T37 R1——约束/自由二态）
#include <QCheckBox>  // 启用 Switch（UI-T37 R1——QSS 重绘指示器）
#include <QColor>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QMenu>  // 导入下拉菜单（返工⑤——CSV/JSON 呈现层整合）
#include <QStyledItemDelegate>  // 彩色 Tag 委托基类（UI-T37 R2）
#include <QSpinBox>  // 采样计数三轴分割（UI-T37 R1——实时点数预览）
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>  // 分组容器（UI-T37 返工——旧插件 QGroupBox 形态）
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMetaObject>  // 提交队列化（UI-T37 返工——出信号处理器防 UB）
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>  // 属性表单滚动容器（UI-T36——长表单小窗不截断）
#include <QStyle>     // 动态属性样式重算（UI-T37 R2——状态卡分色）
#include <QSlider>      // 覆盖率滑块（UI-T37 R1——滑块+输入双联）
#include <QTabWidget>
#include <QTreeWidget>
#include <QPainter>  // Tag 药丸绘制（UI-T37 R2——彩色标签委托）
#include <QVBoxLayout>
#include <QWidget>

#include <sdurws/ird/ui/FlowLayout.hpp>  // 流式栅格（UI-T24 P3——命令条换行承载，钳制源 1 消除）
#include <sdurws/ird/ui/UiText.hpp>      // ui::resolveText（§3.5 唯一文案出口——按钮语义化 NFR-MNT-03）
#include <sdurws/ird/ui/UiTheme.hpp>     // 工业风主题/卡片容器（UI-T37 R1——契约 acceptance 1）

#include <rw/math/Vector3D.hpp>  // rw::math::Vector3D（UI-T30 新增条目默认几何值）

#include <algorithm>  // std::min（删除锚回落位次钳制）
#include <charconv>   // std::to_chars（formatSiText——数值确定性文本化）
#include <functional>  // std::function（UI-T36 层级树递归定位）
#include <set>        // uniqueEntryName 的已占名集合
#include <stdexcept>
#include <utility>

namespace sdurws::ird::requirements {
namespace {

// 树/表节点列上的锚存取（UI-T36 修复：锚列＝行末隐藏列——makeTable 追加
// 的空标题末列。原 kAnchorColumn=1 为单列树形态遗留：两列以上显示列的
// 表格（区域/工况/必验）复用行装配时，锚规范文本覆盖第 1 显示列＝采样/
// 节拍列 obj- 直出的根因；统一改锚列＝行列数-1，各行装配以空串占位末
// 隐藏列〔makeRow/makeTableRow/renderTree 保证〕）。
int anchorColumnOf(const QTreeWidgetItem* item)
{
    return item->columnCount() - 1;
}

/// 树节点类别角色（col0 UserRole——UI-T36 层级化后分组/条目/根的判别键；
/// 值＝PanelTreeModel RequirementNodeKind 枚举 int 投影）。
constexpr int kNodeKindRole = static_cast<int>(Qt::UserRole);

void setNodeAnchor(QTreeWidgetItem* item, const std::optional<core::ObjectId>& oid)
{
    // 规范文本承载（toCanonical——"obj-<32hex>"；nullopt→空串）。
    item->setText(anchorColumnOf(item), oid.has_value()
                                     ? QString::fromStdString(oid.value().toCanonical())
                                     : QString());
}

std::optional<core::ObjectId> nodeAnchor(QTreeWidgetItem* item)
{
    const QString text = item->text(anchorColumnOf(item));
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

// 两列行装配（显示两列＋末隐藏锚列——UI-T36：行构造即含末隐藏列空串
// 占位，锚写入不再覆盖显示文本列）。
QTreeWidgetItem* makeRow(const QString& a, const QString& b, const std::optional<core::ObjectId>& anchor)
{
    auto* item = new QTreeWidgetItem(QStringList() << a << b << QString());
    setNodeAnchor(item, anchor);
    return item;
}

// 三显示列行装配（区域/工况表——显示三列＋末隐藏锚列；UI-T36 列补全：
// 覆盖目标/适用范围列此前恒空）。
QTreeWidgetItem* makeTableRow(const QString& a, const QString& b, const QString& c,
                              const std::optional<core::ObjectId>& anchor)
{
    auto* item = new QTreeWidgetItem(QStringList() << a << b << c << QString());
    setNodeAnchor(item, anchor);
    // 单元格完整文本承载于 tooltip（UI-T39——审核布局返工：列宽固定策略
    // 下长摘要〔区域名/采样摘要〕被省略号截断，完整文本移 tooltip 保留
    // 可读性；Qt::ToolTipRole 逐列设置）。
    item->setToolTip(0, a);
    item->setToolTip(1, b);
    item->setToolTip(2, c);
    return item;
}

// 四显示列行装配（工况表 R2——验收 Tag 列；显示四列＋末隐藏锚列）。
QTreeWidgetItem* makeTableRow4(const QString& a, const QString& b, const QString& c,
                               const QString& d,
                               const std::optional<core::ObjectId>& anchor)
{
    auto* item = new QTreeWidgetItem(
        QStringList() << a << b << c << d << QString());
    setNodeAnchor(item, anchor);
    // 同上——四列完整文本入 tooltip（适用范围长文本被固定列宽截断时）。
    item->setToolTip(0, a);
    item->setToolTip(1, b);
    item->setToolTip(2, c);
    item->setToolTip(3, d);
    return item;
}

/// 彩色 Tag 委托（UI-T37 R2 acceptance 4——必验红/可选灰圆角标签；纯
/// 呈现面：文本为唯一数据源，domain 语义归域侧派生）。
class TagDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override
    {
        const QString text = index.data(Qt::DisplayRole).toString();
        if (text.isEmpty()) {
            QStyledItemDelegate::paint(painter, option, index);
            return;
        }
        // 圆角药丸（必验＝警示橙/白字；可选＝描边灰）——规格 §8 三件套。
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);
        opt.text.clear();
        QStyledItemDelegate::paint(painter, opt, index);  // 底色/选中面
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        const QRect r = option.rect.adjusted(6, 3, -6, -3);
        const QRect pill = QRect(r.left(), r.top() + (r.height() - 18) / 2,
                                 qMin(r.width(), 44), 18);
        const QColor fill = text == QStringLiteral("必验")
                                ? QColor(ui::palette::kWarning)
                                : QColor(ui::palette::kCardBorder);
        painter->setPen(Qt::NoPen);
        painter->setBrush(fill);
        painter->drawRoundedRect(pill, 9, 9);
        painter->setPen(text == QStringLiteral("必验") ? Qt::white
                                                       : QColor(ui::palette::kText));
        painter->drawText(pill, Qt::AlignCenter, text);
        painter->restore();
    }
};

}  // namespace

// =====================================================================
// 构造与装配注入
// =====================================================================

RequirementsPanelWidget::RequirementsPanelWidget(bool writable, QWidget* parent)
    : QWidget(parent), m_writable(writable)
{
    m_threadGuard.assertOnUiThread();  // 构造线程＝UI 线程（§3.4——守卫绑定）
    m_commands = requirementsDomainCommands();  // 命令目录（§9.8 九条——装配数据）

    // 面板可用性下限（UI-T39——审核布局返工：需求编辑区在默认布局中被
    // 纵向堆叠压缩到卡片只见顶部，工程师需频繁滚动。最小尺寸经 Dock 内
    // 容传导为需求 Dock 的几何下限：高 560 px＝页头＋工具行＋至少一张
    // 卡片的可见下限；宽 380 px＝流式栅格双按钮行＋卡片表单的可读下限。
    // ★ 宽度上限的物理约束：最小窗口（§4.4 1280×720）下左列双 Dock＋
    //   右列属性 Dock 之外须为中央区保留 kCentralMinReserveWidth=320 px
    //   （UI-T24 中央区保护红线），面板最小宽传导 minimumSizeHint 后即
    //   回推算法的钳制下限——取 380 恰使 1280 窗口三列共存可满足
    //   （1280−380−主 Dock≈264−右 Dock 300＝中央 336 ≥ 320）；审核建议
    //   的 420~480 与该红线在此窗口物理不相容，默认尺寸改由宿主装载拍
    //   以初始尺寸形态给出〔非硬下限〕）。
    setMinimumSize(380, 560);

    // 骨架：顶部命令条＋草稿提示条＋（左树 1｜右四页面 2）水平区。
    auto* rootLayout = new QVBoxLayout(this);
    auto* contentLayout = new QHBoxLayout;
    rootLayout->addLayout(contentLayout);

    // 命令条（rootLayout 第 0 行——buildCommandBar 经 insertWidget 挂入）。
    buildCommandBar(this);

    // 双树职责提示条（F-499——宿主审核 P2，自持面）：需求树＝草稿编辑面、
    // 工业项目树＝已应用修订呈现——同一对象两处可见的认知锚在面板顶部
    // 常驻说明（纯呈现；muted 次级文本——与建模分节标题同款词表色）。
    // 共享项目树侧的状态标识涉共享装配面（PIPE §6.2 互斥），随后续任务
    // 协调落地（findings F-499 登记）。
    auto* draftHint = new QLabel(
        QStringLiteral("当前编辑：需求草稿——经『应用草稿』提交后生成新项目修订"
                       "（工业项目树呈现已应用版本）"),
        this);
    draftHint->setObjectName(QStringLiteral("ird_req_draft_hint"));
    draftHint->setWordWrap(true);
    draftHint->setStyleSheet(
        QStringLiteral("color: %1; padding: 1px 0;")
            .arg(QString::fromLatin1(ui::palette::kTextMuted)));
    rootLayout->insertWidget(1, draftHint);  // 命令条（第 0 行）之下、内容区之上

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
    // 工业风主题（UI-T37 R1——面板作用域安装：本面板子树生效，宿主
    // chrome/其他域面板不受影响；QSS 由 UiTheme 调色板常量单一词表拼装）。
    ui::applyIndustrialTheme(this);

    // F-539（UI-T71）：定位出口生产接线——PanelSelectionState 契约「widget
    // 执行滚动与高亮」的落位（此前 setLocateSink 生产零调用点，校验逐项行
    // 点击的 LocateTarget 产出即弃——UX-06 落点不可达）。本面板以既有
    // focusObject 为执行器：自持树滚动＋置当前行→itemSelectionChanged→
    // onTreeSelectionChanged→L-R1 前向链（检查器重投影＋panelHighlight 三
    // 维高亮顺链达成——HostMigrationProviders Deps.panelHighlight 既有通
    // 道）。无锚行在 locate() 入口已按「不伪造定位」拦截（规格行为）；
    // 闭包外身份由 focusObject 按清除语义对齐（不伪造落点）。
    m_selection.setLocateSink([this](const LocateTarget& t) {
        focusObject(t.scrollToNode);
    });

    // 构造完成即可刷新（空工作集态——投影产出空行/占位，不虚构内容；
    // 首次 refreshPanel 由装配层在载入基线后驱动）。
}

void RequirementsPanelWidget::setCommandSubmit(CommandSubmitFn submitFn)
{
    m_commandSubmit = std::move(submitFn);
    // 返工⑤：目录与按钮/菜单动作下标对齐（导入两槽位按钮为空——动作面在
    // m_importActions，可用性统一由 refreshPanel 现算，此处零重复判定）。
    // UI-T39：撤销三键随提交出口注入同步（项目级键的可达前提＝出口在）。
    refreshLifecycleAndEmptyStates();
    refreshCommandEnablement();
    refreshUndoButtons();
}

void RequirementsPanelWidget::setCommandAvailability(CommandAvailabilityFn availability)
{
    m_commandAvailability = std::move(availability);
    // UI-T39：可用性查询注入后撤销三键随快照重估（项目级键由 project.undo
    // 谓词门控——无项目/只读/无可撤销修订＝禁用＋原因提示）。
    refreshLifecycleAndEmptyStates();
    refreshCommandEnablement();
    refreshUndoButtons();
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

void RequirementsPanelWidget::setStationMarkersSink(StationMarkersSink sink)
{
    m_stationMarkersSink = std::move(sink);
}

void RequirementsPanelWidget::emitStationMarkers(const RequirementWorkingSet& ws)
{
    // 工位标记投递（UI-T33 acceptance 1——非禁用工位全量集合；enabled
    // 过滤在面板〔投影值集合即呈现集合〕；refFrame 原值直投——宿主帧名
    // 解析归投影方〔World/ModelFrame 的宿主映射在装配层全缝侧〕）。出口
    // 未注入＝静默跳过（呈现面缺席的显式形态）。
    if (!m_stationMarkersSink) {
        return;
    }
    std::vector<StationMarkerProjection> markers;
    markers.reserve(ws.points.entries.size());
    for (const TaskPoint& point : ws.points.entries) {
        if (!point.enabled) {
            continue;  // 禁用工位不进三维（acceptance 1 词面——诚实呈现）
        }
        markers.push_back(StationMarkerProjection{point.name, point.refFrame});
    }
    m_stationMarkersSink(markers);
}

// =====================================================================
// 面板构建（构造期一次——布局骨架）
// =====================================================================

void RequirementsPanelWidget::buildCommandBar(QWidget* top)
{
    // 域命令区（UI-T24 P3 重排；UI-T39 分组收敛）：命令条呈现按使用频率
    // 分组——高频命令保持独立按钮（导入 ▾／导出副本＋两级撤销三键），低频
    // 命令（几何采集两条＋派生四条）收入『更多操作 ▾』下拉（审核返工：
    // 原九键同级平铺在窄 Dock 中折成两三行小按钮，工程师需在同等视觉
    // 权重的按钮里辨认目标——收敛后主排 6 键，行为宽度显著下降）。全部
    // 命令仍为注册目录中的独立命令（registry 面/可用性门控零变化），仅
    // 呈现层分组。流式栅格换行排布（原单行 QHBoxLayout 把 12 个按钮的
    // 最小宽度之和顶成整列 Dock 的最小宽——钳制源 1 消账记录见
    // traceability/builds/ui-t24/clamp-source.md）。
    // 文案（UX-02/NFR-MNT-03——F-430 家族需求域一族消账）：按钮标题经
    // UiText 键族①b 解析（cmd.<id>.title——§3.5 键约定，值在 UiText.cpp
    // 登记）；resolveText 缺键即 fail-fast 上抛＝键表完备性的构造期保证，
    // 面板侧零第二文案源。
    auto* bar = new QWidget(top);
    auto* barLayout = new QVBoxLayout(bar);
    barLayout->setContentsMargins(0, 0, 0, 0);
    barLayout->setSpacing(2);
    // 无父对象构造（子布局形态——由 addLayout 收养，见下方安装处注释）。
    auto* flow = new ui::FlowLayout(nullptr, /*hSpacing=*/4, /*vSpacing=*/2);
    // 编辑组分隔符（录入命令与两级撤销三按钮之间的强分隔——撤销族是编辑
    // 会话语义，与录入/派生类命令分属不同操作面向）。
    auto makeGroupSeparator = [bar]() {
        auto* sep = new QFrame(bar);
        sep->setFrameShape(QFrame::VLine);
        sep->setFrameShadow(QFrame::Sunken);
        sep->setFixedHeight(20);  // 竖线与按钮行等高（≈标准按钮高度）
        return sep;
    };
    // 返工⑤（所有者指令——顶部工具栏整合去重；UI-T39 扩至第二下拉）：
    // 导入 CSV/JSON 两键整合为『导入 ▾』；捕获两条＋派生四条整合为
    // 『更多操作 ▾』。m_commandButtons/m_importActions/m_moreActions 与
    // 目录下标对齐（下拉槽位按钮为空、动作在册；主排槽位反之）。
    m_commandButtons.assign(m_commands.size(), nullptr);
    m_importActions.assign(m_commands.size(), nullptr);
    m_moreActions.assign(m_commands.size(), nullptr);
    // 更多操作下拉宿主（首个命中命令的挂位——组序不变；QMenu 分组与
    // §9.8 命令表的语义分组一致：捕获｜派生，以菜单分隔线呈现）。
    auto moreMenuFor = [this, bar, &flow]() -> QMenu* {
        if (m_moreMenuButton == nullptr) {
            auto* dropdown = new QPushButton(QString::fromStdString(
                ui::resolveText(ui::TextKey{
                    "panel.requirements.more-actions.label"})),
                bar);
            dropdown->setObjectName(
                QStringLiteral("ird_req_more_actions_dropdown"));
            dropdown->setMenu(new QMenu(dropdown));
            connect(dropdown, &QPushButton::clicked, this, [dropdown] {
                // 无可用子项时按钮置灰不可点；可达时的点击语义＝弹出菜单
                //（QPushButton+setMenu 默认即弹出——此兜底仅防样式改动后
                // 菜单丢失）。
                dropdown->showMenu();
            });
            m_moreMenuButton = dropdown;
            flow->addWidget(dropdown);
        }
        return m_moreMenuButton->menu();
    };
    for (std::size_t i = 0; i < m_commands.size(); ++i) {
        const ui::CommandDescriptor& d = m_commands[i];
        const bool isImport = d.id == "requirements.import-csv"
                           || d.id == "requirements.import-json";
        const bool isLowFrequency =
            d.id == "requirements.capture-tcp"
            || d.id == "requirements.pick-feature"
            || d.id == "requirements.mirror-stations"
            || d.id == "requirements.create-array"
            || d.id == "requirements.apply-template"
            || d.id == "requirements.regenerate-linked";
        if (isImport) {
            if (m_importDropdownButton == nullptr) {
                // 下拉宿主按钮（目录序＝首条导入命令的挂位——组序不变）。
                auto* dropdown = new QPushButton(QString::fromStdString(
                    ui::resolveText(ui::TextKey{
                        "panel.requirements.import-dropdown.label"})),
                    bar);
                dropdown->setObjectName(
                    QStringLiteral("ird_req_import_dropdown"));
                auto* menu = new QMenu(dropdown);
                dropdown->setMenu(menu);
                connect(dropdown, &QPushButton::clicked, this, [dropdown] {
                    // 无项目态按钮置灰不可点；可达时的点击语义＝弹出菜单
                    //（QPushButton+setMenu 默认即弹出——此兜底仅防样式
                    // 改动后菜单丢失）。
                    dropdown->showMenu();
                });
                m_importDropdownButton = dropdown;
                flow->addWidget(dropdown);
            }
            QAction* action = m_importDropdownButton->menu()->addAction(
                QString::fromStdString(ui::resolveText(d.titleKey)));
            action->setToolTip(QString::fromStdString(d.menuPath));
            const std::string id(d.id);
            connect(action, &QAction::triggered, this, [this, id] {
                // 菜单动作激活→转发注入的提交出口（与独立按钮同一轨）。
                if (m_commandSubmit != nullptr) {
                    m_commandSubmit(id);
                }
            });
            m_importActions[i] = action;
            continue;
        }
        if (isLowFrequency) {
            // 低频命令入『更多操作 ▾』（捕获组与派生组之间加菜单分隔线
            // ——分组语义与 §9.8 命令表一致，目录挂位以首条命中命令为准）。
            QMenu* menu = moreMenuFor();
            if (d.id == "requirements.mirror-stations" && !menu->actions().empty()) {
                menu->addSeparator();  // 捕获组｜派生组的菜单内分隔
            }
            QAction* action = menu->addAction(
                QString::fromStdString(ui::resolveText(d.titleKey)));
            action->setToolTip(QString::fromStdString(d.menuPath));
            const std::string id(d.id);
            connect(action, &QAction::triggered, this, [this, id] {
                if (m_commandSubmit != nullptr) {
                    m_commandSubmit(id);  // 与独立按钮同一提交轨
                }
            });
            m_moreActions[i] = action;
            continue;
        }
        auto* btn =
            new QPushButton(QString::fromStdString(ui::resolveText(d.titleKey)),
                            bar);
        btn->setToolTip(QString::fromStdString(d.menuPath));
        connect(btn, &QPushButton::clicked, this, &RequirementsPanelWidget::onCommandButtonClicked);
        flow->addWidget(btn);
        m_commandButtons[i] = btn;
    }
    // 编辑组分隔符（目录主排命令与两级撤销三按钮之间的强分隔）。
    flow->addWidget(makeGroupSeparator());
    // 两级撤销三按钮（L-R4——草稿级撤销/重做与项目级撤销三处独立控件，
    // 不合并：草稿级动编辑器局部栈，项目级纯转发 ui 命令——语义不混用）。
    // 返工⑤（所有者指令）：按钮文本精简为标准动词（去"草稿级/项目级"括号
    // 后缀），完整语义移入 Tooltip；本三键为字面直出的存量面，随本批一并
    // 入 UiText 词表（NFR-MNT-03 面板侧零第二文案源）。
    m_draftUndoButton = new QPushButton(QString::fromStdString(
        ui::resolveText(ui::TextKey{"panel.requirements.undo-draft.label"})), bar);
    m_draftUndoButton->setToolTip(QString::fromStdString(
        ui::resolveText(ui::TextKey{"panel.requirements.undo-draft.tooltip"})));
    m_draftRedoButton = new QPushButton(QString::fromStdString(
        ui::resolveText(ui::TextKey{"panel.requirements.redo-draft.label"})), bar);
    m_draftRedoButton->setToolTip(QString::fromStdString(
        ui::resolveText(ui::TextKey{"panel.requirements.redo-draft.tooltip"})));
    m_projectUndoButton = new QPushButton(QString::fromStdString(
        ui::resolveText(ui::TextKey{"panel.requirements.undo-project.label"})), bar);
    m_projectUndoButton->setToolTip(QString::fromStdString(
        ui::resolveText(ui::TextKey{"panel.requirements.undo-project.tooltip"})));
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
    // 状态行（就地错误/警告/摘要——非模态呈现，UX-03/07）。UI-T37 返工②
    // （所有者指令：橙色警示条删除）——恢复裸状态行形态；R2 横幅连同
    // ✕ 关闭钮一并退役（showStatusLine 退化为直设文本，调用面不变）。
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
    // 左栏需求树（卡 §9.8 面板表第 1 行）。UI-T36 层级化：需求工程单根
    // →四分组（名称＋计数）→条目三级折叠形态（原"需求对象/需求集"双
    // 冗余层级与自持导航 deprecated 横幅一并退役——横幅承载的迁移期
    // 双形态并存自 WP-14-T10 起，UI-T36 起自持树升格为面板主导航呈现，
    // 与共享项目树联动语义不变〔L-R1 双向〕）。
    auto* leftLayout = new QVBoxLayout(left);

    // F-499（宿主审核 P2）：树名标注"（当前草稿）"——与工业项目树（已应用
    // 修订）的职责分界在两处呈现位同频（顶部提示条＋树名），消除"同一
    // 工位为何出现两次/哪棵可编辑"的认知歧义。
    m_tree = makeTable(left, QStringList() << "需求树（当前草稿）");
    m_tree->setRootIsDecorated(true);  // 层级折叠形态（分组节点的展开指示器）
    connect(m_tree, &QTreeWidget::itemSelectionChanged, this,
            &RequirementsPanelWidget::onTreeSelectionChanged);
    leftLayout->addWidget(m_tree);
}

void RequirementsPanelWidget::buildStationPage(QTabWidget* pages)
{
    // 右栏页①工位（检查器表单——行＝stationFieldsFor 投影）。
    // UI-T26 页签状态行：页头呈现『工位：〈对象名|未选择对象〉』随树
    // 选中刷新（updateTabHeaders）——空态不再是无信息空白。
    // UI-T37 R1 卡片化（acceptance 1）：平铺单表单→五卡纵向流（基础属性/
    // 空间与公差/自由度约束/动作阶段/姿态规则）；标题与"?"帮助位经 UiText
    // 键族①c（NFR-MNT-03 面板侧禁止第二文案源）。行宿主仍为 QFormLayout
    // ——renderInspector 按字段键路由到所属卡（stationFormForKey）。
    auto* page = new QWidget(pages);
    auto* lay = new QVBoxLayout(page);
    m_stationHeader = new QLabel(QStringLiteral("工位 > 未选择"), page);  // 返工⑤：面包屑精简（去与页签名重复的冒号态）
    m_stationHeader->setObjectName("ird_req_tab_station_header");
    lay->addWidget(m_stationHeader);
    // 对象生命周期工具行（UI-T30 B1——新增/复制/删除三键）。
    lay->addWidget(makeLifecycleBar(page, "points", WorkingSetMember::Points,
                                    QStringLiteral("工位")));
    // 属性卡片滚动容器（UI-T36 滚动承载保持——卡不进滚动区的只有页头与
    // 工具行；卡组在滚动区内小窗不截断）。
    auto* scroll = new QScrollArea(page);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* formHost = new QWidget(scroll);
    auto* hostLay = new QVBoxLayout(formHost);
    hostLay->setContentsMargins(0, 0, 0, 0);
    hostLay->setSpacing(8);
    QVBoxLayout* content = nullptr;
    const auto cardText = [](const char* key) {
        return QString::fromStdString(ui::resolveText(ui::TextKey{key}));
    };
    m_stationBasicCard = ui::createCard(
        formHost, cardText("panel.requirements.card.basic.title"),
        cardText("panel.requirements.card.basic.help"), &content);
    m_stationBasicForm = new QFormLayout;
    m_stationBasicForm->setLabelAlignment(Qt::AlignRight);
    content->addLayout(m_stationBasicForm);
    hostLay->addWidget(m_stationBasicCard);
    m_stationPoseCard = ui::createCard(
        formHost, cardText("panel.requirements.card.pose.title"),
        cardText("panel.requirements.card.pose.help"), &content);
    m_stationPoseForm = new QFormLayout;
    m_stationPoseForm->setLabelAlignment(Qt::AlignRight);
    content->addLayout(m_stationPoseForm);
    hostLay->addWidget(m_stationPoseCard);
    m_stationDofCard = ui::createCard(
        formHost, cardText("panel.requirements.card.dof.title"),
        cardText("panel.requirements.card.dof.help"), &content);
    m_stationDofForm = new QFormLayout;
    m_stationDofForm->setLabelAlignment(Qt::AlignRight);
    content->addLayout(m_stationDofForm);
    hostLay->addWidget(m_stationDofCard);
    m_stationSegmentCard = ui::createCard(
        formHost, cardText("panel.requirements.card.segment.title"),
        cardText("panel.requirements.card.segment.help"), &content);
    m_stationSegmentForm = new QFormLayout;
    m_stationSegmentForm->setLabelAlignment(Qt::AlignRight);
    content->addLayout(m_stationSegmentForm);
    hostLay->addWidget(m_stationSegmentCard);
    m_stationOrientCard = ui::createCard(
        formHost, cardText("panel.requirements.card.orientation.title"),
        cardText("panel.requirements.card.orientation.help"), &content);
    m_stationOrientForm = new QFormLayout;
    m_stationOrientForm->setLabelAlignment(Qt::AlignRight);
    content->addLayout(m_stationOrientForm);
    hostLay->addWidget(m_stationOrientCard);
    hostLay->addStretch(1);
    scroll->setWidget(formHost);
    lay->addWidget(scroll, /*stretch=*/1);
    // 空态提示（返工⑤——未选择对象时属性区收起为居中轻量提示；与滚动
    // 容器互斥显隐，由 refreshLifecycleAndEmptyStates 统一驱动）。
    m_stationScroll = scroll;
    scroll->setObjectName(QStringLiteral("ird_req_station_props_scroll"));
    m_stationEmptyHint = makeEmptyStateHint(page, "points");
    lay->addWidget(m_stationEmptyHint, /*stretch=*/1);
    pages->addTab(page, "工位");
}

void RequirementsPanelWidget::buildRegionPage(QTabWidget* pages)
{
    // 右栏页②区域（表＋检查器＋预览摘要——§9.8 面板表第 3 行）＋
    // UI-T26 页签状态行（同工位页）。
    auto* page = new QWidget(pages);
    auto* lay = new QVBoxLayout(page);
    m_regionHeader = new QLabel(QStringLiteral("区域 > 未选择"), page);
    m_regionHeader->setObjectName("ird_req_tab_region_header");
    lay->addWidget(m_regionHeader);
    // 对象生命周期工具行（UI-T30 B1）。
    lay->addWidget(makeLifecycleBar(page, "regions", WorkingSetMember::Regions,
                                    QStringLiteral("区域")));
    m_regionTable = makeTable(page, QStringList()
                                      << QStringLiteral("区域名称")
                                      << QStringLiteral("空间采样")
                                      << QStringLiteral("覆盖目标"));
    m_regionTable->setObjectName(QStringLiteral("ird_req_region_table"));  // 验证定位锚
    connect(m_regionTable, &QTreeWidget::itemSelectionChanged, this,
            &RequirementsPanelWidget::onTreeSelectionChanged);
    lay->addWidget(m_regionTable);
    // 属性表单滚动容器（UI-T36——同工位页；预览摘要留在滚动区外恒可见）。
    auto* regionScroll = new QScrollArea(page);
    regionScroll->setWidgetResizable(true);
    regionScroll->setFrameShape(QFrame::NoFrame);
    auto* regionFormHost = new QWidget(regionScroll);
    // UI-T37 R1 卡片化（acceptance 1/2）：区域检查器→四卡（基础属性/空间
    // 包围盒〔复合行〕/采样与达标〔滑块双联＋计数预览〕/高级参数〔折叠〕）。
    // 盒卡与采样卡为自定义行宿主（复合控件直挂 VBox）；基础/高级卡仍为
    // QFormLayout（renderRegionPage 按字段键路由）。
    auto* regionHostLay = new QVBoxLayout(regionFormHost);
    regionHostLay->setContentsMargins(0, 0, 0, 0);
    regionHostLay->setSpacing(8);
    QVBoxLayout* content = nullptr;
    const auto regionCardText = [](const char* key) {
        return QString::fromStdString(ui::resolveText(ui::TextKey{key}));
    };
    m_regionBasicCard = ui::createCard(
        regionFormHost, regionCardText("panel.requirements.card.basic.title"),
        regionCardText("panel.requirements.card.basic.help"), &content);
    m_regionBasicForm = new QFormLayout;
    m_regionBasicForm->setLabelAlignment(Qt::AlignRight);
    content->addLayout(m_regionBasicForm);
    regionHostLay->addWidget(m_regionBasicCard);
    m_regionBoxCard = ui::createCard(
        regionFormHost, regionCardText("panel.requirements.card.region-box.title"),
        regionCardText("panel.requirements.card.region-box.help"), &content);
    m_regionBoxLay = content;  // 复合行（盒中心/盒尺寸）直挂
    regionHostLay->addWidget(m_regionBoxCard);
    m_regionSamplingCard = ui::createCard(
        regionFormHost,
        regionCardText("panel.requirements.card.region-sampling.title"),
        regionCardText("panel.requirements.card.region-sampling.help"), &content);
    m_regionSamplingLay = content;  // 滑块双联/计数复合行直挂
    regionHostLay->addWidget(m_regionSamplingCard);
    m_regionAdvancedCard = ui::createCard(
        regionFormHost,
        regionCardText("panel.requirements.card.region-advanced.title"),
        regionCardText("panel.requirements.card.region-advanced.help"), &content,
        /*startCollapsed=*/true);  // 默认收起（高级参数去噪——设计规格 §4）
    // 返工⑤：卡片折叠统一走 createCard 标题栏折叠三角（原卡外"展开 ▸"
    // 独立按钮与 setVisible 翻转退役——第二折叠形态消账，折叠机制单一化）。
    m_regionAdvancedBody = new QWidget(m_regionAdvancedCard);
    auto* advancedLay = new QVBoxLayout(m_regionAdvancedBody);
    advancedLay->setContentsMargins(0, 0, 0, 0);
    m_regionAdvancedForm = new QFormLayout;
    m_regionAdvancedForm->setLabelAlignment(Qt::AlignRight);
    advancedLay->addLayout(m_regionAdvancedForm);
    content->addWidget(m_regionAdvancedBody);
    regionHostLay->addWidget(m_regionAdvancedCard);
    regionHostLay->addStretch(1);
    regionScroll->setWidget(regionFormHost);
    lay->addWidget(regionScroll, /*stretch=*/1);
    // 空态提示（返工⑤——未选择对象时属性区收起为居中轻量提示，替代空
    // 卡片骨架；文本走 UiText 词表）。
    m_regionScroll = regionScroll;
    regionScroll->setObjectName(QStringLiteral("ird_req_region_props_scroll"));
    m_regionEmptyHint = makeEmptyStateHint(page, "regions");
    lay->addWidget(m_regionEmptyHint, /*stretch=*/1);
    // 表头帮助（UI-T36——列语义悬浮说明）。
    m_regionTable->headerItem()->setToolTip(
        0, QStringLiteral("区域显示名（业务命名，非内部标识）"));
    m_regionTable->headerItem()->setToolTip(
        1, QStringLiteral("包围盒内离散采样摘要（方法＋计数/间距）"));
    m_regionTable->headerItem()->setToolTip(
        2, QStringLiteral("位置/姿态覆盖率下限（P≥/O≥）"));
    m_regionPreviewLabel = new QLabel(page);
    m_regionPreviewLabel->setWordWrap(true);
    lay->addWidget(m_regionPreviewLabel);
    // 颜色图例（UI-T65——F-495"颜色图例"项：三维采样着色词表的呈现面
    // 色块行；色值经 ui palette 词表供色——与三维后端 GL 分色同源单一
    // 供色点，本面零自配色值；蓝框＝区域框既有辨识蓝（UI-T65 返工——
    // 与三维框 None/缺省档同源消费 kRegionTintDefaultHex，原值
    // (0.25,0.45,1.0)＝#4073FF））。
    {
        auto* legend = new QLabel(page);
        legend->setObjectName("ird_req_region_legend");
        legend->setTextFormat(Qt::RichText);
        legend->setText(QStringLiteral(
                            "<span style=\"color:%1\">■</span> 通过　"
                            "<span style=\"color:%2\">■</span> 未通过　"
                            "<span style=\"color:%3\">■</span> 数据不足　"
                            "<span style=\"color:%4\">■</span> 未采样　"
                            "<span style=\"color:%5\">□</span> 当前选中区域")
                            .arg(QString::fromUtf8(ui::palette::kSampleGoodHex),
                                 QString::fromUtf8(ui::palette::kSampleFailedHex),
                                 QString::fromUtf8(ui::palette::kSampleWeakHex),
                                 QString::fromUtf8(ui::palette::kSampleNotSampledHex),
                                 QString::fromUtf8(ui::palette::kRegionTintDefaultHex)));
        lay->addWidget(legend);
    }
    pages->addTab(page, "区域");
}

void RequirementsPanelWidget::buildConditionPage(QTabWidget* pages)
{
    // 右栏页③工况（单表＋详情卡——§9.8 面板表第 4 行；返工④：必验清单
    // 预览表并入单表『是否必验』列，下半部改为详情与节拍配置卡）＋
    // UI-T26 页签状态行（同工位页）。
    auto* page = new QWidget(pages);
    auto* lay = new QVBoxLayout(page);
    m_conditionHeader = new QLabel(QStringLiteral("工况 > 未选择"), page);
    m_conditionHeader->setObjectName("ird_req_tab_condition_header");
    lay->addWidget(m_conditionHeader);
    // 对象生命周期工具行（UI-T30 B1）。
    lay->addWidget(makeLifecycleBar(page, "conditions",
                                    WorkingSetMember::Conditions,
                                    QStringLiteral("工况")));
    m_conditionTable = makeTable(page, QStringList()
                                        << QStringLiteral("工况")
                                        << QStringLiteral("目标节拍")
                                        << QStringLiteral("适用范围")
                                        << QStringLiteral("是否必验"));
    m_conditionTable->setObjectName(QStringLiteral("ird_req_condition_table"));  // 验证定位锚
    // 是否必验 Tag 列（UI-T37 R2 acceptance 4 引入；返工④表头由『验收』
    // 更名——所有者指令：必验清单预览表并入本列后列名自释）。
    // 必验红/可选灰彩色标签委托；第 3 显示列，对象锚仍居末隐藏列。
    m_conditionTable->setItemDelegateForColumn(3, new TagDelegate(m_conditionTable));
    connect(m_conditionTable, &QTreeWidget::itemSelectionChanged, this,
            &RequirementsPanelWidget::onTreeSelectionChanged);
    lay->addWidget(m_conditionTable);
    // 属性表单滚动容器（UI-T36 引入；返工④卡化——同工位/区域页结构：
    // QGroupBox 卡片宿主 QFormLayout，替代裸表单）。
    auto* conditionScroll = new QScrollArea(page);
    conditionScroll->setWidgetResizable(true);
    conditionScroll->setFrameShape(QFrame::NoFrame);
    auto* conditionFormHost = new QWidget(conditionScroll);
    // 返工④：检查器卡『工况详情与节拍配置』（标题/help 经 UiText 词表——
    // NFR-MNT-03 唯一文案出口）。必验清单预览表（m_mustList）按所有者
    // 指令撤销：其必验事实已并入上方单表『是否必验』列（派生仍走
    // resolveRequiredCases 域单点——P-EV-9 不变），腾出的下半部即本卡。
    auto* conditionHostLay = new QVBoxLayout(conditionFormHost);
    conditionHostLay->setContentsMargins(0, 0, 0, 0);
    QVBoxLayout* content = nullptr;
    const auto conditionCardText = [](const char* key) {
        return QString::fromStdString(ui::resolveText(ui::TextKey{key}));
    };
    m_conditionDetailCard = ui::createCard(
        conditionFormHost,
        conditionCardText("panel.requirements.card.condition-detail.title"),
        conditionCardText("panel.requirements.card.condition-detail.help"),
        &content);
    m_conditionForm = new QFormLayout;
    m_conditionForm->setLabelAlignment(Qt::AlignRight);
    content->addLayout(m_conditionForm);
    conditionHostLay->addWidget(m_conditionDetailCard);
    conditionHostLay->addStretch(1);  // 卡片贴顶（同区域页——空白沉底不出现在卡内）
    conditionScroll->setWidget(conditionFormHost);
    lay->addWidget(conditionScroll, /*stretch=*/1);
    // 空态提示（返工⑤——同工位/区域页：未选择时属性区收起为居中提示）。
    m_conditionScroll = conditionScroll;
    conditionScroll->setObjectName(QStringLiteral("ird_req_condition_props_scroll"));
    m_conditionEmptyHint = makeEmptyStateHint(page, "conditions");
    lay->addWidget(m_conditionEmptyHint, /*stretch=*/1);
    m_conditionTable->headerItem()->setToolTip(
        1, QStringLiteral("目标节拍（s；未设显示『未设』）"));
    m_conditionTable->headerItem()->setToolTip(
        2, QStringLiteral("适用的工位范围（全部工位/N 个工位/不适用）"));
    m_conditionTable->headerItem()->setToolTip(
        3, QStringLiteral("是否必验＝应用前必须通过校验（启用∧等级 Must 派生——域解析单点）；可选＝告警级"));
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

    // ---- 逐项过滤行（UI-T34 E 批次 acceptance 1——级别/层/稳定码三过滤；
    //      纯呈现层重投影，过滤词表封闭，过滤变更重渲染不触域）----
    auto* filterRow = new QWidget(page);
    auto* filterLay = new QHBoxLayout(filterRow);
    filterLay->setContentsMargins(0, 0, 0, 0);
    m_validationLevelFilter = new QComboBox(filterRow);
    m_validationLevelFilter->setObjectName(
        QStringLiteral("ird_req_validation_level_filter"));
    m_validationLevelFilter->addItem(QStringLiteral("全部级别"));
    m_validationLevelFilter->addItem(QStringLiteral("仅阻断"));
    m_validationLevelFilter->addItem(QStringLiteral("仅警告"));
    m_validationLevelFilter->addItem(QStringLiteral("仅不适用"));
    m_validationLayerFilter = new QComboBox(filterRow);
    m_validationLayerFilter->setObjectName(
        QStringLiteral("ird_req_validation_layer_filter"));
    m_validationLayerFilter->addItem(QStringLiteral("全部层"));
    for (int i = 0; i <= 9; ++i) {
        m_validationLayerFilter->addItem(QStringLiteral("R%1").arg(i));
    }
    m_validationCodeFilter = new QLineEdit(filterRow);
    m_validationCodeFilter->setObjectName(
        QStringLiteral("ird_req_validation_code_filter"));
    m_validationCodeFilter->setPlaceholderText(
        QStringLiteral("按稳定码过滤（如 REQ-READY）"));
    filterLay->addWidget(m_validationLevelFilter, 1);
    filterLay->addWidget(m_validationLayerFilter, 1);
    filterLay->addWidget(m_validationCodeFilter, 2);
    lay->addWidget(filterRow);

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
    // 过滤变更→以最近报告重投影（UI-T34——纯呈现层重渲染；报告缓存
    // m_lastReadiness 由 renderValidationPage 现存，不触域不重估）。
    auto refilter = [this]() {
        if (m_lastReadiness.has_value()) {
            renderValidationPage(*m_lastReadiness);
        }
    };
    connect(m_validationLevelFilter, &QComboBox::currentIndexChanged, this,
            [refilter](int) { refilter(); });
    connect(m_validationLayerFilter, &QComboBox::currentIndexChanged, this,
            [refilter](int) { refilter(); });
    connect(m_validationCodeFilter, &QLineEdit::textChanged, this,
            [refilter](const QString&) { refilter(); });
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
        // 返工⑤：动作/集合键入属性——可用性按"未选择对象＝复制/删除置灰、
        // 仅保留新增"刷新（refreshLifecycleAndEmptyStates 统一判定）。
        btn->setProperty("irdLifecycleAction", QString::fromLatin1(row.action));
        btn->setProperty("irdLifecycleKey", QString::fromLatin1(key));
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

QLabel* RequirementsPanelWidget::makeEmptyStateHint(QWidget* parent, const char* key)
{
    // 空态提示（返工⑤——所有者指令：未选择对象时属性区隐藏空卡片骨架，
    // 改为居中轻量提示；文本走 UiText 词表——NFR-MNT-03 单一文案出口）。
    // objectName 携集合键（gui_test 逐页定位面）。默认隐藏——可见性由
    // refreshLifecycleAndEmptyStates 统一驱动。
    auto* hint = new QLabel(QString::fromStdString(
        ui::resolveText(
            ui::TextKey{"panel.requirements.empty-selection.hint"})), parent);
    hint->setObjectName(QStringLiteral("ird_req_empty_hint_%1")
                            .arg(QString::fromLatin1(key)));
    hint->setAlignment(Qt::AlignCenter);
    hint->setWordWrap(true);
    hint->setVisible(false);
    return hint;
}

void RequirementsPanelWidget::refreshLifecycleAndEmptyStates()
{
    // 生命周期键＋空态提示统一刷新（返工⑤——"未选择对象"状态体验：
    // ①复制/删除置灰仅保留新增（会话内选择面）；②属性区滚动容器隐藏、
    // 居中提示显现。会话语义先行：无会话/只读＝全部禁用（诚实不可编辑
    // ——UI-T30 语义保持），仅会话内再按选择面分岔）。
    // UI-T39（审核四.5）：禁用键必须给出原因——无会话/只读/未选择三态
    // 的 tooltip 差异化，启用键回退基础提示（清残留原因）。会话判定＝
    // 提供器**现取结果**（provider 函数恒在——装配期注入；会话终结后
    // attachEditor(nullptr) 使现取返回空＝诚实无会话态）。
    IRequirementEditor* sessionEditor = m_editTarget ? m_editTarget() : nullptr;
    const QString noSessionReason = QString::fromStdString(ui::resolveText(
        ui::TextKey{"panel.requirements.lifecycle.tooltip.no-session"}));
    const QString readonlyReason = QString::fromStdString(ui::resolveText(
        ui::TextKey{"panel.requirements.lifecycle.tooltip.readonly"}));
    const QString noSelectionReason = QString::fromStdString(ui::resolveText(
        ui::TextKey{"panel.requirements.lifecycle.tooltip.no-selection"}));
    for (QPushButton* btn : findChildren<QPushButton*>()) {
        if (!btn->property("irdLifecycle").toBool()) {
            continue;
        }
        if (sessionEditor == nullptr || m_sessionDetached) {
            btn->setEnabled(false);
            btn->setToolTip(noSessionReason);  // 无会话——先开项目
            continue;
        }
        if (!m_writable) {
            btn->setEnabled(false);
            btn->setToolTip(readonlyReason);  // 只读项目——L-R12
            continue;
        }
        const QString action = btn->property("irdLifecycleAction").toString();
        if (action == QStringLiteral("add")) {
            btn->setEnabled(true);  // 新增不依赖选择——空集合也能起步（B1）
            btn->setToolTip(QString());
            continue;
        }
        const QString key = btn->property("irdLifecycleKey").toString();
        const bool hasSelection =
            key == QStringLiteral("points")     ? m_stationSelectedId.has_value()
            : key == QStringLiteral("regions")  ? m_regionSelectedId.has_value()
                                                : m_conditionSelectedId.has_value();
        btn->setEnabled(hasSelection);
        btn->setToolTip(hasSelection ? QString() : noSelectionReason);
    }
    // 属性区空态（各页独立——选择面按本页对象集合判定，跨页互不影响）。
    const auto applyState = [](QScrollArea* scroll, QLabel* hint, bool has) {
        if (scroll != nullptr) { scroll->setVisible(has); }
        if (hint != nullptr) { hint->setVisible(!has); }
    };
    applyState(m_stationScroll, m_stationEmptyHint,
               m_stationSelectedId.has_value());
    applyState(m_regionScroll, m_regionEmptyHint,
               m_regionSelectedId.has_value());
    applyState(m_conditionScroll, m_conditionEmptyHint,
               m_conditionSelectedId.has_value());
}

void RequirementsPanelWidget::refreshCommandEnablement()
{
    // 命令按钮/菜单动作可用性统一刷新（返工⑤——目录下标对齐：导入两槽位
    // 独立按钮为空、由『导入 ▾』菜单动作承载；下拉本体＝两导入命令可用性
    // 之或，双禁＝置灰）。setCommandSubmit/setCommandAvailability/refreshPanel
    // 三处共用同一判定面（原三处循环重复实现——本批收敛为单出口）。
    // UI-T39：第二下拉『更多操作 ▾』同构——六条低频命令槽位按钮为空、
    // 动作在 m_moreActions；下拉本体使能＝子项可用性之或（全部禁用＝
    // 置灰——无项目/只读态命令条不再呈现零散可用按钮）。
    if (m_commandButtons.size() != m_commands.size()
        || m_importActions.size() != m_commands.size()
        || m_moreActions.size() != m_commands.size()) {
        return;  // 目录未装配（构造序前置调用——保守跳过，零误禁）
    }
    bool importAnyEnabled = false;
    bool moreAnyEnabled = false;
    for (std::size_t i = 0; i < m_commandButtons.size(); ++i) {
        const auto a = m_commandAvailability ? m_commandAvailability(m_commands[i].id)
                                             : ui::CommandAvailability{};
        const bool enabled =
            m_commandSubmit != nullptr && (!m_commandAvailability || a.enabled);
        const QString disableReason =
            (m_commandAvailability && !a.enabled && !a.disableReasonKey.empty())
                ? QString::fromStdString(ui::resolveText(a.disableReasonKey))
                : QString();
        if (m_commandButtons[i] != nullptr) {
            m_commandButtons[i]->setEnabled(enabled);
            // 禁用原因随 tooltip 呈现（审核四.5）；启用时回退路径提示。
            m_commandButtons[i]->setToolTip(disableReason.isEmpty()
                                                ? QString::fromStdString(m_commands[i].menuPath)
                                                : disableReason);
        }
        if (m_importActions[i] != nullptr) {
            m_importActions[i]->setEnabled(enabled);
            m_importActions[i]->setToolTip(disableReason);
            importAnyEnabled = importAnyEnabled || enabled;
        }
        if (m_moreActions[i] != nullptr) {
            m_moreActions[i]->setEnabled(enabled);
            m_moreActions[i]->setToolTip(disableReason);
            moreAnyEnabled = moreAnyEnabled || enabled;
        }
    }
    if (m_importDropdownButton != nullptr) {
        m_importDropdownButton->setEnabled(importAnyEnabled);
    }
    if (m_moreMenuButton != nullptr) {
        m_moreMenuButton->setEnabled(moreAnyEnabled);
    }
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
        showStatusLine(QStringLiteral("未应用：未打开需求会话（不可编辑）"));
        return false;
    }
    const EditOutcome out = editor->applyEdit(edit);
    if (!out.accepted) {
        // 拒绝：就地错误（非模态——UX-03/07；工作集字节未动）。
        showStatusLine(QString::fromStdString(
            "未应用（" + out.error.detail + "）"));
        return false;
    }
    // 接受：入草稿撤销记账（UI-T39 修正——结构编辑〔新增/复制/删除〕与
    // 字段编辑同入 L-R4 记账：编辑器局部栈已随 applyEdit 入栈，记账漏记
    // ＝撤销键对结构编辑呈"不可撤销"假象；字段编辑轨 recordAppliedEdit
    // 的既有先例同语义）。随后走 sink 链（onEditApplied）——脏标记＋
    // 全面板重投影＋UI-T29 组合子就绪重估。
    m_undoTracker.recordAppliedEdit();
    onEditApplied(out.changeSummary);
    return true;
}

/// 工况新增向导内建面板（UI-T37 R2 acceptance 4——缺省工厂；测试经
/// setConditionWizardFactory 注入确定字段，本函数不进测试路径）。
/// 三步一屏：名称（防撞默认）／目标节拍（0＝未设——『未设』占位不伪造）/
/// 验收要求（必验/可选分段）。确认返回字段；取消返回 nullopt＝零新增。
std::optional<RequirementsPanelWidget::ConditionWizardFields>
defaultConditionWizard(QWidget* parent, const QString& suggestedName)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("新增工况"));
    auto* lay = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight);
    auto* nameEdit = new QLineEdit(suggestedName, &dialog);
    form->addRow(QStringLiteral("名称"), nameEdit);
    auto* cycleSpin = new QDoubleSpinBox(&dialog);
    cycleSpin->setRange(0.0, 1.0e6);
    cycleSpin->setDecimals(2);
    cycleSpin->setSingleStep(0.1);
    cycleSpin->setValue(0.0);
    cycleSpin->setSpecialValueText(QStringLiteral("未设"));  // 0＝未设（四态守恒）
    cycleSpin->setToolTip(QStringLiteral("该工况期望循环时间（s）；未设＝不参与节拍评估"));
    form->addRow(QStringLiteral("目标节拍 (s)"), cycleSpin);
    auto* verifyCombo = new QComboBox(&dialog);
    verifyCombo->addItem(QStringLiteral("必验"));   // Must（默认——§6.2 词表主面）
    verifyCombo->addItem(QStringLiteral("可选"));   // Should
    form->addRow(QStringLiteral("验收要求"), verifyCombo);
    lay->addLayout(form);
    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("创建"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    lay->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted) {
        return std::nullopt;  // 用户取消——调用方零新增
    }
    RequirementsPanelWidget::ConditionWizardFields fields;
    fields.name = nameEdit->text().trimmed().toStdString();
    fields.hasCycle = cycleSpin->value() > 0.0;
    fields.cycleSeconds = cycleSpin->value();
    fields.mustVerify = verifyCombo->currentIndex() == 0;
    return fields;
}

void RequirementsPanelWidget::onAddEntry(WorkingSetMember member)
{
    IRequirementEditor* editor = m_editTarget ? m_editTarget() : nullptr;
    if (editor == nullptr) {
        showStatusLine(QStringLiteral("未应用：未打开需求会话（不可编辑）"));
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
        // 工况新增向导（UI-T37 R2 acceptance 4——创建即带完整起步配置，
        // 禁止无配置空白条目；取消＝零新增）。缝缺省＝内建对话框（缺省
        // 绑定在此兜底——遍历实录修复：此前缝空直接创建无配置条目，与
        // 本条纪律矛盾且宿主真实用户永不弹向导；测试经
        // setConditionWizardFactory 注入确定字段〔FakeDialogHost 同族〕）。
        std::function<std::optional<ConditionWizardFields>()> wizard =
            m_conditionWizard;
        if (!wizard) {
            wizard = [this, suggested =
                                QString::fromStdString(uniqueEntryName(
                                    ws, member, "工况"))]() {
                return defaultConditionWizard(this, suggested);
            };
        }
        auto fields = wizard();
        {
            if (!fields.has_value()) {
                return;  // 取消——不产生条目
            }
            c.name = uniqueEntryName(ws, member,
                                     fields->name.empty()
                                         ? std::string("工况")
                                         : fields->name);
            c.level = fields->mustVerify ? RequirementLevel::Must
                                         : RequirementLevel::Should;
            if (fields->hasCycle && fields->cycleSeconds > 0.0) {
                c.targetCycleTimeS = fields->cycleSeconds;
            }
        }
        if (submitStructuralEdit(RequirementEdit{c})) { focusObject(c.objectId); }
    }
}

void RequirementsPanelWidget::onDuplicateEntry(WorkingSetMember member)
{
    IRequirementEditor* editor = m_editTarget ? m_editTarget() : nullptr;
    if (editor == nullptr || !m_lastSelected.has_value()) {
        showStatusLine(QStringLiteral(
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
    showStatusLine(QStringLiteral("未应用：选中对象不属于该集合"));
}

void RequirementsPanelWidget::onRemoveEntry(WorkingSetMember member)
{
    IRequirementEditor* editor = m_editTarget ? m_editTarget() : nullptr;
    if (editor == nullptr || !m_lastSelected.has_value()) {
        showStatusLine(QStringLiteral(
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
    m_sessionDetached = false;  // 活会话驱动（装配层 refreshPanel 编排——脱离旗标复位）
    renderTree(ws);
    renderInspector(ws);
    renderRegionPage(ws);
    renderConditionPage(ws);
    renderValidationPage(report);
    emitStationMarkers(ws);  // UI-T33——工位标记随面板重载全量投递（会话编辑/选择刷新承接点）

    // 两级撤销呈现（UI-T39 收敛——可用性/提示判定全部在 refreshUndoButtons
    // 单出口：草稿级随记账器＋可写性，项目级随命令可用性快照）。
    refreshUndoButtons();
    // 生命周期工具行＋属性区空态（返工⑤统一刷新——无会话/只读＝全禁；
    // 会话内未选择＝复制/删除置灰仅保留新增＋空态提示替代空卡骨架）。
    refreshLifecycleAndEmptyStates();
    refreshCommandEnablement();
}

void RequirementsPanelWidget::setWritable(bool writable)
{
    m_writable = writable;  // L-R12——ui 只读横幅同源事实
    // 可写性切换＝全面板重投影（检查器行门控随行投影刷新；命令按钮可达
    // 性由 readOnlyAllowed 的 ui 命令门控面承担——本面板只控编辑行与撤销钮）。
    // UI-T39（审核 P1）：切换后必须同步刷新全部状态承载面——生命周期键/
    // 空态、命令按钮、两级撤销三键此前不随 setWritable 刷新，只读切换后
    // 残留可写态按钮（新增/撤销仍可点）。空报告＝只读切换不新造判定——
    // 刷新沿用最近校验结论（m_lastReadiness 有值则以之重投影校验页，校验
    // 页不被清空）；编辑面四页按现取工作集重投影。
    if (IRequirementEditor* editor = m_editTarget ? m_editTarget() : nullptr) {
        const RequirementWorkingSet& ws = editor->workingSet();
        renderTree(ws);
        renderInspector(ws);
        renderRegionPage(ws);
        renderConditionPage(ws);
        if (m_lastReadiness.has_value()) {
            renderValidationPage(*m_lastReadiness);  // 校验结论保持——只读不清空
        }
    }
    refreshLifecycleAndEmptyStates();
    refreshCommandEnablement();
    refreshUndoButtons();
}

void RequirementsPanelWidget::resetForSessionDetached()
{
    // 会话脱离复位（UI-T39——审核 P1"会话关闭时清理所有旧按钮状态"）：
    // 项目关闭/切换后，面板此前残留上一会话的树/表/卡片内容与可用按钮
    // （生命周期键、撤销键、命令键全部按上一会话末态呈现）。本方法把
    // 全部会话态归零、投影收拢为无会话空态。零修订（会话态清理面）。
    m_threadGuard.assertOnUiThread();
    m_sessionDetached = true;  // 无会话判定旗标（refreshPanel 复位——UI-T39）
    // ①会话选中锚与草稿撤销记账归零（记账器事实源＝编辑器局部栈——栈
    //   已随基线复位/会话终结，记账同步归零；跨会话残留即"幽灵可撤销"）。
    m_selection.select(std::nullopt);
    m_lastSelected.reset();
    m_stationSelectedId.reset();
    m_regionSelectedId.reset();
    m_conditionSelectedId.reset();
    m_undoTracker.reset();
    m_lastReadiness.reset();  // 上一会话的校验结论不再呈现（新会话现算）
    // 列宽首刷旗标归零（新会话首批数据重新按内容成形——UI-T39 列宽策略）。
    m_regionColumnsSized = false;
    m_conditionColumnsSized = false;
    // ②控件投影收拢（树/表清空＋表单清行＋校验页清空——"面板不缓存权
    //   威数据"的无会话形态：无数据源即无投影内容）。
    m_tree->blockSignals(true);
    m_tree->clear();
    m_tree->blockSignals(false);
    m_regionTable->blockSignals(true);
    m_regionTable->clear();
    m_regionTable->blockSignals(false);
    m_conditionTable->blockSignals(true);
    m_conditionTable->clear();
    m_conditionTable->blockSignals(false);
    // 卡片表单清行（清空辅助 clearFormRows/clearBoxRows 定义在本文件
    // 渲染辅助段——本方法位于其前，按同款语义内联：removeRow 释放行内
    // 控件、takeAt 收养布局项——与 renderXxx 重投影前置清空一致）。
    const auto clearForm = [](QFormLayout* form) {
        if (form == nullptr) {
            return;
        }
        while (form->rowCount() > 0) {
            form->removeRow(0);
        }
    };
    const auto clearVBox = [](QVBoxLayout* lay) {
        if (lay == nullptr) {
            return;
        }
        while (QLayoutItem* item = lay->takeAt(0)) {
            if (item->widget() != nullptr) {
                item->widget()->deleteLater();
            }
            delete item;
        }
    };
    clearForm(m_stationBasicForm);
    clearForm(m_stationPoseForm);
    clearForm(m_stationDofForm);
    clearForm(m_stationSegmentForm);
    clearForm(m_stationOrientForm);
    clearForm(m_regionBasicForm);
    clearVBox(m_regionBoxLay);
    clearVBox(m_regionSamplingLay);
    clearForm(m_regionAdvancedForm);
    clearForm(m_conditionForm);
    if (m_validationCounts != nullptr) {
        m_validationCounts->clear();
    }
    if (m_validationLayers != nullptr) {
        m_validationLayers->clear();
    }
    if (m_validationItems != nullptr) {
        m_validationItems->clear();
    }
    m_regionPreviewLabel->clear();
    // ③状态面统一刷新（生命周期/命令/撤销三出口在无会话输入下全禁；
    //   空态提示显现、页头回落『未选择』）。
    refreshLifecycleAndEmptyStates();
    refreshCommandEnablement();
    refreshUndoButtons();
    updateTabHeaders();
    showStatusLine(QString());
}

void RequirementsPanelWidget::noteBaselineReloaded()
{
    // 基线重载登记（UI-T39）：编辑器 loadBaseline 已把局部栈复位——本类
    // 的撤销记账（对栈深的外部记账）同步归零，撤销/重做键随新记账刷新。
    // 零修订（记账是会话态）。
    m_threadGuard.assertOnUiThread();
    m_undoTracker.reset();
    refreshUndoButtons();
}

std::optional<core::ObjectId> RequirementsPanelWidget::selectionAnchor(
    WorkingSetMember member) const
{
    // 按集合种类取锚（UI-T39——审核五.1：镜像/阵列/重生成等命令声明输入
    // 类型后按页取锚；全局 m_lastSelected 不再充当命令源条目——它随树/
    // 表/页签任何一次点击漂移，工位命令拿到区域锚会静默找不到源）。
    switch (member) {
    case WorkingSetMember::Points:
        return m_stationSelectedId;
    case WorkingSetMember::Regions:
        return m_regionSelectedId;
    case WorkingSetMember::Conditions:
        return m_conditionSelectedId;
    }
    return std::nullopt;
}

void RequirementsPanelWidget::refreshUndoButtons()
{
    // 两级撤销三键的可用性/提示统一出口（UI-T39——审核 P1）。判定输入：
    //   草稿级（撤销/重做）＝编辑器草稿视图（twoLevelUndoView——canUndo-
    //     Local/canRedoLocal）＋可写性（L-R12——只读会话不可改草稿）；
    //   项目级（撤销上次应用）＝project.undo 命令可用性快照（宿主注册表
    //     谓词——无项目/只读/无可撤销修订三维，disableReasonKey 即原因
    //     提示）；无可用性查询注入＝退化为"提交出口存在性"（gui 测试
    //     缝——不虚构可用性，注册表缺席时无法断言修订事实）。
    // 无会话（编辑目标缺位）＝草稿级两键全禁（无编辑器栈即无可撤销）；
    // 项目级键仍按命令快照呈现（命令面事实独立于本域会话）。
    IRequirementEditor* editor = m_editTarget ? m_editTarget() : nullptr;
    if (editor != nullptr) {
        const TwoLevelUndoView undoView = twoLevelUndoView(*editor, m_undoTracker);
        m_draftUndoButton->setEnabled(m_writable && undoView.canUndoLocal);
        m_draftRedoButton->setEnabled(m_writable && undoView.canRedoLocal);
        // 禁用原因提示（审核四.5——禁用键必须给出原因；启用时清残留）。
        m_draftUndoButton->setToolTip(
            (m_writable && undoView.canUndoLocal)
                ? QString::fromStdString(ui::resolveText(
                      ui::TextKey{"panel.requirements.undo-draft.tooltip"}))
                : QString::fromStdString(ui::resolveText(
                      ui::TextKey{"panel.requirements.undo-draft.tooltip"})
                                  + "（当前没有可撤销的编辑）"));
        m_draftRedoButton->setToolTip(
            (m_writable && undoView.canRedoLocal)
                ? QString::fromStdString(ui::resolveText(
                      ui::TextKey{"panel.requirements.redo-draft.tooltip"}))
                : QString::fromStdString(ui::resolveText(
                      ui::TextKey{"panel.requirements.redo-draft.tooltip"})
                                  + "（当前没有可重做的编辑）"));
    } else {
        m_draftUndoButton->setEnabled(false);
        m_draftRedoButton->setEnabled(false);
    }
    // 项目级撤销：按 project.undo 命令可用性快照门控（宿主谓词＝磁盘 tip
    // inverse 推导的 canUndo——§5.5；无快照查询＝退化二态，不虚构）。
    const bool submitPresent = m_commandSubmit != nullptr;
    bool projectUndoEnabled = submitPresent;
    QString projectUndoReason;
    if (m_commandAvailability != nullptr) {
        const auto a = m_commandAvailability(ui::CommandId{"project.undo"});
        projectUndoEnabled = submitPresent && a.enabled;
        if (!a.enabled && !a.disableReasonKey.empty()) {
            projectUndoReason =
                QString::fromStdString(ui::resolveText(a.disableReasonKey));
        }
    }
    m_projectUndoButton->setEnabled(projectUndoEnabled);
    m_projectUndoButton->setToolTip(
        projectUndoReason.isEmpty()
            ? QString::fromStdString(ui::resolveText(
                  ui::TextKey{"panel.requirements.undo-project.tooltip"}))
            : projectUndoReason);
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

    // 自持树滚动定位（UI-T36 层级化：递归行扫描——层级化后行序≠投影序，
    // 深度优先遍历分组子树匹配锚；命中路径逐级展开保证可视）。命中＝置
    // 当前行（触发行选中→onTreeSelectionChanged→检查器重投影——L-R1 既有
    // 数据流复用，零新增刷新路径）；未命中＝清除选中（对象已删除/他路
    // 编辑后的漂移——不伪造定位）。
    const QString anchorText = QString::fromStdString(oid.value().toCanonical());
    const std::function<QTreeWidgetItem*(QTreeWidgetItem*)> findMatched =
        [&](QTreeWidgetItem* parent) -> QTreeWidgetItem* {
        for (int i = 0; i < parent->childCount(); ++i) {
            QTreeWidgetItem* child = parent->child(i);
            if (child->text(anchorColumnOf(child)) == anchorText) {
                return child;
            }
            if (child->childCount() > 0) {
                if (QTreeWidgetItem* hit = findMatched(child)) {
                    child->setExpanded(true);  // 命中路径逐级展开（可视前提）
                    return hit;
                }
            }
        }
        return nullptr;
    };
    QTreeWidgetItem* target = findMatched(m_tree->invisibleRootItem());
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
    // UI-T36 层级化：模型层投影仍为平铺序（需求集→分组→条目），widget 层
    // 重构为三级折叠树——需求工程单根→四分组（名称＋计数，分组节点无锚
    // 不可选中定位）→条目（锚＝ObjectId）；默认展开根与分组（条目级折叠）。
    const std::vector<RequirementNode> nodes = buildRequirementTree(ws);
    // 选中保持＝重建前快照（UI-T37 R1 修复：clear() 同步触发
    // itemSelectionChanged→onTreeSelectionChanged 以 nullopt 重置
    // m_lastSelected——树侧"跨刷新保持选中"此前实为失效，表侧 T31 局部
    // 快照先例同款修复；重建期信号屏蔽防 L-R1 重入）。
    const std::optional<core::ObjectId> wanted = m_lastSelected;
    QTreeWidgetItem* restoreItem = nullptr;
    m_tree->blockSignals(true);
    m_tree->clear();
    auto* root = new QTreeWidgetItem(
        QStringList() << QStringLiteral("需求工程") << QString());
    root->setData(0, kNodeKindRole,
                  static_cast<int>(RequirementNodeKind::RequirementRoot));
    m_tree->addTopLevelItem(root);
    QTreeWidgetItem* group = nullptr;
    for (const RequirementNode& n : nodes) {
        // 需求集根层级由"需求工程"承载（模型层 RequirementRoot 节点跳过）。
        if (n.kind == RequirementNodeKind::RequirementRoot) {
            continue;
        }
        const bool isGroup =
            n.kind == RequirementNodeKind::PointsGroup
            || n.kind == RequirementNodeKind::RegionsGroup
            || n.kind == RequirementNodeKind::ConditionsGroup
            || n.kind == RequirementNodeKind::PlansGroup;
        if (isGroup) {
            group = new QTreeWidgetItem(
                QStringList() << QString::fromStdString(n.displayLabel)
                              << QString());
            group->setData(0, kNodeKindRole, static_cast<int>(n.kind));
            group->setFlags(group->flags() & ~Qt::ItemIsEditable);
            root->addChild(group);
            group->setExpanded(true);  // 默认展开分组（条目级再折叠）
            continue;
        }
        auto* item = new QTreeWidgetItem(
            QStringList() << QString::fromStdString(n.displayLabel) << QString());
        setNodeAnchor(item, n.objectId);
        item->setData(0, kNodeKindRole, static_cast<int>(n.kind));
        item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        (group != nullptr ? group : root)->addChild(item);
        // 恢复选中锚（跨刷新保持——L-R1 会话态；仅条目级锚可恢复）。
        if (n.objectId.has_value() && wanted.has_value()
            && n.objectId.value() == wanted.value()) {
            restoreItem = item;
        }
    }
    root->setExpanded(true);
    m_tree->blockSignals(false);
    // 选中恢复：命中→setCurrentItem 走一次 L-R1 正向（成员/会话态对齐）；
    // 锚失效〔对象已删〕→按清除语义对齐（不伪造定位）。
    if (restoreItem != nullptr) {
        m_tree->setCurrentItem(restoreItem);
        restoreItem->setSelected(true);
    } else if (m_lastSelected.has_value()) {
        m_selection.select(std::nullopt);
        m_lastSelected.reset();
        updateTabHeaders();  // 锚清空的页头即时回落（『未选择对象』语义）
    }
}

// ---- UI-T37 R1 本文件局部辅助（卡片化检查器渲染面）---------------------

/// 6 位小数裁尾零文本化（PanelStationModel formatDeterministic 同规则——
/// 数值显示格式单一词表；此处仅用于复合控件提交词组装，回读一律工作集）。
QString formatSiText(double v)
{
    char buf[32];
    auto res = std::to_chars(buf, buf + sizeof(buf), v, std::chars_format::fixed, 6);
    std::string s(buf, res.ptr);
    if (s.find('.') != std::string::npos) {
        while (!s.empty() && s.back() == '0') { s.pop_back(); }
        if (!s.empty() && s.back() == '.') { s.pop_back(); }
    }
    return QString::fromStdString(s);
}

/// 清空 QFormLayout 全部行（卡片重投影前置——removeRow 释放行内控件）。
void clearFormRows(QFormLayout* form)
{
    while (form->rowCount() > 0) {
        form->removeRow(0);
    }
}

/// 层码→语义类别标签（UI-T37 R2 acceptance 5——去晦涩：显示面用语义
/// 标签，R 层码转 Tooltip/过滤 UserRole；映射源＝分层投影自带的检查
/// 标题〔域侧权威语义——面板零私设词表〕，未命中回落原码）。
QString semanticLayerLabel(
    const std::vector<ValidationLayerRow>& layers, const std::string& token)
{
    for (const ValidationLayerRow& r : layers) {
        if (r.layerToken == token) {
            return QString::fromStdString(r.title);
        }
    }
    return QString::fromStdString(token);
}

/// 清空 VBox 全部子项（矩阵/复合行宿主重排前置——取出的项连同控件删除）。
void clearBoxRows(QVBoxLayout* lay)
{
    while (lay->count() > 0) {
        QLayoutItem* item = lay->takeAt(0);
        delete item->widget();
        delete item->layout();
        delete item;
    }
}

void RequirementsPanelWidget::renderInspector(const RequirementWorkingSet& ws)
{
    // 检查器按当前选中重投影（L-R1 反向半区；无选中＝空卡片——不虚构）。
    // UI-T37 R1：单表单→五卡路由（基础/空间公差/自由度矩阵/动作阶段/姿态
    // 规则）；行级升级＝启用 Switch、自由度 2×3 分段矩阵、位置复合行、
    // 容差 SpinBox。既有提交语义（权威值短路→spec 解析→域裁决）不变。
    clearFormRows(m_stationBasicForm);
    clearFormRows(m_stationPoseForm);
    clearFormRows(m_stationDofForm);
    clearFormRows(m_stationSegmentForm);
    clearFormRows(m_stationOrientForm);
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

    // 数量词表（一次取用——行键可编辑性的裁决面）。UI-T37 崩溃修复：本页
    // 名称/等级/段/姿态种类等行此前呈可编辑态而词表无键，提交即触发"词表
    // 外键 fail-fast"崩溃——本批起对齐区域轨 T31 诚实降级（非词表行只读）。
    const std::vector<ui::QuantityFieldSpec> specs = stationQuantitySpecs();
    const auto specDriven = [&specs](const std::string& key) {
        for (const ui::QuantityFieldSpec& s : specs) {
            if (s.key == key) {
                return true;
            }
        }
        return false;
    };

    // 文本行（灰显规则＝L-R12 行门控 ∧ 数量词表裁决）。等级行＝QComboBox
    // （UI-T37 返工——对齐旧插件"枚举全用 QComboBox"组件形态；提交轨＝
    // applyStationEnumEdit，词表 Must/Should 直投）。
    auto renderTextRow = [&](QFormLayout* form, const StationFieldRow& r) {
        if (r.fieldKey == "orientation-kind"
            && r.enablement == StationFieldEnablement::Editable) {
            // 姿态规则五规则下拉（返工③——tryOrientationRuleKind 词表直投；
            // 参数行随模型按 kind 显隐重投影）。
            auto* combo = new QComboBox(this);
            combo->setObjectName(QStringLiteral("ird_station_orientation_combo"));
            const OrientationRuleKind kinds[] = {
                OrientationRuleKind::Fixed, OrientationRuleKind::AlignFrame,
                OrientationRuleKind::AlignGeometryNormal,
                OrientationRuleKind::PointAtTarget,
                OrientationRuleKind::ToolRollFree};
            for (const OrientationRuleKind k : kinds) {
                combo->addItem(QString::fromStdString(
                    std::string(orientationRuleKindToken(k))));
            }
            { QSignalBlocker blocker(combo);
            combo->setCurrentText(QString::fromStdString(r.valueText)); }
            combo->setEnabled(m_writable);
            combo->setToolTip(QStringLiteral(
                "参数行按规则种类显隐（Fixed＝固定欧拉角；PointAtTarget＝目"
                "标点；ToolRollFree＝滚转区间）"));
            connect(combo, &QComboBox::currentIndexChanged, this,
                    [this, key = r.fieldKey, combo](int) {
                        submitStationEnumValue(key, combo->currentText());
                    });
            form->addRow(new QLabel(QString::fromStdString(r.label), this), combo);
            return;
        }
        if (r.fieldKey == "level"
            && r.enablement == StationFieldEnablement::Editable) {
            auto* combo = new QComboBox(this);
            combo->setObjectName(QStringLiteral("ird_station_level_combo"));
            combo->addItem(QStringLiteral("Must"));
            combo->addItem(QStringLiteral("Should"));
            combo->setCurrentText(QString::fromStdString(r.valueText));
            combo->setEnabled(m_writable);
            connect(combo, &QComboBox::currentIndexChanged, this,
                    [this, key = r.fieldKey, combo](int) {
                        submitStationEnumValue(key, combo->currentText());
                    });
            form->addRow(new QLabel(QString::fromStdString(r.label), this), combo);
            return;
        }
        const bool placeholder =
            r.valueText == "未提供" || r.valueText == "未设";
        auto* edit = new QLineEdit(
            placeholder ? QString() : QString::fromStdString(r.valueText), this);
        const bool editable = r.enablement == StationFieldEnablement::Editable
                              && specDriven(r.fieldKey) && m_writable;
        edit->setReadOnly(!editable);
        edit->setProperty("irdFieldKey", QString::fromStdString(r.fieldKey));
        // 权威文本（幻影脏化短路基准——提交/刷新双轨同源）。
        edit->setProperty("irdAuthoritativeValue",
                          QString::fromStdString(
                              placeholder ? std::string{} : r.valueText));
        if (editable) {
            connect(edit, &QLineEdit::editingFinished, this,
                    &RequirementsPanelWidget::onInspectorEditingFinished);
        }
        form->addRow(new QLabel(QString::fromStdString(r.label), this), edit);
    };

    // 容差 SpinBox 行（acceptance 2——步进 0.01/6 位小数；提交词经
    // formatSiText 组装后仍走 spec 解析轨——SA-12 唯一出口不旁路）。
    auto renderSpinRow = [&](const StationFieldRow& r) {
        auto* spin = new QDoubleSpinBox(this);
        spin->setDecimals(6);
        spin->setSingleStep(0.01);
        spin->setRange(-1.0e12, 1.0e12);  // 宽域——合法性由 spec 解析/域链裁决
        { QSignalBlocker blocker(spin);  // 初始化静默（setValue 触发 valueChanged→提交→重入崩溃——首轮 gui 实证）
        spin->setValue(QString::fromStdString(r.valueText).toDouble()); }
        spin->setProperty("irdFieldKey", QString::fromStdString(r.fieldKey));
        spin->setProperty("irdAuthoritativeValue",
                          QString::fromStdString(r.valueText));
        // editingFinished（回车/失焦）而非 valueChanged——逐键提交＝每次
        // 按键全面板刷新重建（发射控件被删＝UB 且无法连续输入；旧插件提
        // 交语义同为编辑完成制）。提交经 submitStationValue 队列化出险。
        connect(spin, &QDoubleSpinBox::editingFinished, this,
                [this, key = r.fieldKey, spin]() {
                    const QString text = formatSiText(spin->value());
                    if (text == spin->property("irdAuthoritativeValue").toString()) {
                        return;  // 幻影脏化短路（与行编辑同轨）
                    }
                    submitStationValue(key, text);
                });
        auto* cell = new QWidget(this);
        auto* lay = new QHBoxLayout(cell);
        lay->setContentsMargins(0, 0, 0, 0);
        lay->setSpacing(4);
        lay->addWidget(spin);
        lay->addWidget(new QLabel(QString::fromStdString(r.unitText), this));
        lay->addStretch(1);
        m_stationPoseForm->addRow(new QLabel(QString::fromStdString(r.label), this),
                                  cell);
    };

    // 启用 Switch 行（acceptance 3——QCheckBox QSS 重绘；提交轨＝
    // applyStationToggleEdit 布尔回填＋域裁决）。
    auto renderSwitchRow = [&](QFormLayout* form, const StationFieldRow& r,
                               std::function<void(bool)> submit) {
        // 启用下拉行（UI-T37 返工②——所有者指令：布尔枚举一律 QComboBox；
        // 提交轨＝applyStationToggleEdit 布尔回填＋域裁决）。
        auto* combo = new QComboBox(this);
        combo->setObjectName(QStringLiteral("ird_station_enabled_combo"));
        combo->addItem(QStringLiteral("启用"));
        combo->addItem(QStringLiteral("停用"));
        { QSignalBlocker blocker(combo);  // 初始化静默（防触发提交重入）
        combo->setCurrentIndex(QString::fromStdString(r.valueText)
                                       == QStringLiteral("是")
                                   ? 0
                                   : 1); }
        const bool enabled =
            r.enablement == StationFieldEnablement::Editable && m_writable;
        combo->setEnabled(enabled);
        if (enabled) {
            connect(combo, &QComboBox::currentIndexChanged, this,
                    [this, submit, combo](int idx) { submit(idx == 0); });
        }
        form->addRow(new QLabel(QString::fromStdString(r.label), this), combo);
    };

    // 位置复合行（acceptance 2——一行三值＋单位；UI-T47 位置编辑轨落位：
    // Provided 态三轴 SpinBox 条件可编辑（m_writable 门控），提交轨＝
    // submitStationValue("pose-position-x/y/z") 队列化出险——与同卡容差
    // SpinBox 同一提交链（幻影脏化短路＋editingFinished＋域裁决回退）。
    // 非数值形态（四态占位）维持灰显原文不伪造——未提供态无逐轴输入面，
    // "提供位置"录入归捕获 TCP/导入通道（域侧回填对未提供基线 fail-fast
    // 同轨，四态语义无"以零补全"通道）。
    auto renderPoseRow = [&](const StationFieldRow& r) {
        // 逐轴三行（UI-T37 返工——对齐旧插件 X/Y/Z 逐行 QDoubleSpinBox 的
        // 布置形态）。
        const QString text = QString::fromStdString(r.valueText);
        std::array<double, 3> vals{0.0, 0.0, 0.0};
        bool ok = text != QStringLiteral("未提供") && !text.isEmpty();
        if (ok) {
            const QStringList parts = text.split(QLatin1Char(','));
            ok = parts.size() == 3;
            for (int i = 0; ok && i < 3; ++i) {
                vals[i] = parts[i].trimmed().toDouble(&ok);
            }
        }
        static const char* kAxes[] = {"X", "Y", "Z"};
        static const char* kAxisKeys[] = {"pose-position-x", "pose-position-y",
                                          "pose-position-z"};
        const QString unit = QString::fromStdString(r.unitText);
        // 可编辑＝会话可写（行 enablement 恒 Editable——模型层词表默认；
        // 位置行无域级只读语义，只读门控唯一事实源＝会话可写性——L-R12
        // 与区域轨盒复合行同款合取形态）。
        const bool editable = m_writable;
        if (ok) {
            for (int i = 0; i < 3; ++i) {
                auto* spin = new QDoubleSpinBox(this);
                spin->setDecimals(6);
                spin->setRange(-1.0e12, 1.0e12);  // 宽域——合法性归域校验链
                { QSignalBlocker blocker(spin);   // 初始化静默（防提交重入）
                spin->setValue(vals[i]); }
                spin->setReadOnly(!editable);  // UI-T47——只读呈现退役
                spin->setEnabled(editable);
                spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
                spin->setMaximumWidth(130);
                spin->setProperty("irdFieldKey",
                                  QString::fromLatin1(kAxisKeys[i]));
                // 权威值＝同函数格式化（formatSiText 与提交侧比较同形——
                // 幻影脏化短路的等值前提；域原文 r.valueText 为逗号复合
                // 形态，不直接可比）。
                spin->setProperty("irdAuthoritativeValue", formatSiText(vals[i]));
                if (editable) {
                    // editingFinished（回车/失焦）——与同卡容差行同一提交
                    // 语义（逐键提交＝重建式刷新下发射控件被删＝UB）。
                    connect(spin, &QDoubleSpinBox::editingFinished, this,
                            [this, key = std::string(kAxisKeys[i]), spin]() {
                                const QString text = formatSiText(spin->value());
                                if (text == spin->property("irdAuthoritativeValue")
                                                 .toString()) {
                                    return;  // 幻影脏化短路（同卡先例）
                                }
                                submitStationValue(key, text);
                            });
                }
                m_stationPoseForm->addRow(
                    QString("%1 (%2)").arg(QString::fromUtf8(kAxes[i]), unit),
                    spin);
            }
        } else {
            // 非数值形态（未提供等四态占位）——原文灰显，不伪造。
            auto* e = new QLineEdit(text, this);
            e->setReadOnly(true);
            e->setProperty("irdFieldKey", QString::fromStdString(r.fieldKey));
            m_stationPoseForm->addRow(
                new QLabel(QString::fromStdString(r.label), this), e);
        }
    };

    // 自由度 2×3 分段矩阵（acceptance 3——受约束/自由二态；提交轨＝
    // applyStationToggleEdit；已态重击不重复提交——幻影编辑消除）。
    auto renderDofMatrix = [&](const std::vector<const StationFieldRow*>& rows) {
        // 约束下拉行（UI-T37 返工②——所有者指令：能下拉的全部下拉；提交
        // 轨＝applyStationToggleEdit；已态重选零提交——幻影编辑消除）。
        for (const StationFieldRow* r : rows) {
            const QString key = QString::fromStdString(r->fieldKey);
            const bool constrained =
                QString::fromStdString(r->valueText) == QStringLiteral("受约束");
            const bool enabled =
                r->enablement == StationFieldEnablement::Editable && m_writable;
            auto* combo = new QComboBox(this);
            combo->setObjectName(QStringLiteral("ird_dof_combo"));
            combo->setProperty("irdDofKey", key);
            combo->addItem(QStringLiteral("约束"));
            combo->addItem(QStringLiteral("自由"));
            { QSignalBlocker blocker(combo);  // 初始化静默（防触发提交重入）
            combo->setCurrentIndex(constrained ? 0 : 1); }
            combo->setEnabled(enabled);
            if (enabled) {
                connect(combo, &QComboBox::currentIndexChanged, this,
                        [this, key, was = constrained, combo](int idx) {
                            if ((idx == 0) == was) {
                                return;  // 已态重选——零提交
                            }
                            submitStationToggle(key.toStdString(), idx == 0);
                        });
            }
            m_stationDofForm->addRow(
                new QLabel(QString::fromStdString(r->label), this), combo);
        }
        // 快捷预设行（契约 acceptance 3——全部约束/全部自由）：逐键走既有
        // 布尔轨（已态跳过＝零幻影提交；撤销粒度＝单键，批量单快照归
        // submitBatchEdit 后续接入——ui.md §13 登记口径）。
        auto* presetBar = new QWidget(this);
        auto* pl = new QHBoxLayout(presetBar);
        pl->setContentsMargins(0, 0, 0, 0);
        pl->setSpacing(4);
        const struct Preset { const char* label; bool on; const char* anchor; } presets[] = {
            {u8"全部约束", true, "ird_dof_preset_constrain"},
            {u8"全部自由", false, "ird_dof_preset_free"},
        };
        for (const Preset& p : presets) {
            auto* b = new QPushButton(QString::fromUtf8(p.label), this);
            b->setObjectName(QString::fromLatin1(p.anchor));
            b->setFocusPolicy(Qt::NoFocus);
            b->setEnabled(m_writable);
            b->setToolTip(QStringLiteral("六轴按预设批量切换（已态轴跳过）"));
            // 键名值捕获（rows 向量为渲染期局部——悬空捕获＝词表外键崩溃，
            // 首轮 gui 实证）；已态判定在点击时读工作集权威值。
            QStringList keys;
            for (const StationFieldRow* r : rows) {
                keys << QString::fromStdString(r->fieldKey);
            }
            connect(b, &QPushButton::clicked, this,
                    [this, keys, on = p.on](bool) {
                        IRequirementEditor* editor =
                            m_editTarget ? m_editTarget() : nullptr;
                        if (editor == nullptr || !m_writable
                            || !m_lastSelected.has_value()) {
                            return;
                        }
                        const RequirementWorkingSet& ws = editor->workingSet();
                        for (const TaskPoint& pt : ws.points.entries) {
                            if (pt.objectId != m_lastSelected.value()) {
                                continue;
                            }
                            const ConstrainedDof& d = pt.pose.constrainedDof;
                            for (const QString& k : keys) {
                                bool cur = false;
                                if (k == QStringLiteral("dof-x")) { cur = d.x; }
                                else if (k == QStringLiteral("dof-y")) { cur = d.y; }
                                else if (k == QStringLiteral("dof-z")) { cur = d.z; }
                                else if (k == QStringLiteral("dof-roll")) { cur = d.roll; }
                                else if (k == QStringLiteral("dof-pitch")) { cur = d.pitch; }
                                else if (k == QStringLiteral("dof-yaw")) { cur = d.yaw; }
                                if (cur != on) {
                                    submitStationToggle(k.toStdString(), on);
                                }
                            }
                            return;
                        }
                    });
            pl->addWidget(b);
        }
        pl->addStretch(1);
        m_stationDofForm->addRow(QStringLiteral("快捷"), presetBar);
    };

    // 动作阶段复合行（返工③——启用/轴下拉＋距离 SpinBox；对齐旧插件
    // "Approach & Retract" 分组形态。值/单位词面解析＝模型 format 词面
    // 逆（"启用 ToolZ"＋"· 1 m"——确定性词面，零域判定）；距离经既有
    // segment-*-distance spec 轨，启用经 toggle 轨、轴经 enum 新词表键）。
    auto renderSegmentRow = [&](const StationFieldRow& r) {
        const QString text = QString::fromStdString(r.valueText);
        const bool segOn = text.startsWith(QStringLiteral("启用"));
        const bool hasAxis = r.fieldKey != QStringLiteral("segment-work");
        QString axisToken;
        if (hasAxis) {
            const int sp = text.indexOf(QLatin1Char(' '));
            if (sp > 0) {
                axisToken = text.mid(sp + 1);
            }
        }
        // 距离现值＝单位词面逆（"· 1 m"→1；解析失败＝不渲染距离行）。
        double dist = 0.0;
        bool hasDist = false;
        QString unit = QString::fromStdString(r.unitText);
        if (unit.startsWith(QStringLiteral("· "))
            && unit.endsWith(QStringLiteral(" m"))) {
            unit.remove(0, 2);
            unit.chop(2);
            dist = unit.toDouble(&hasDist);
        }
        const QString key = QString::fromStdString(r.fieldKey);
        auto* cell = new QWidget(this);
        auto* lay = new QHBoxLayout(cell);
        lay->setContentsMargins(0, 0, 0, 0);
        lay->setSpacing(4);
        auto* enabledCombo = new QComboBox(this);
        enabledCombo->setObjectName(QStringLiteral("ird_segment_enabled_combo"));
        enabledCombo->addItem(QStringLiteral("启用"));
        enabledCombo->addItem(QStringLiteral("停用"));
        { QSignalBlocker blocker(enabledCombo);
        enabledCombo->setCurrentIndex(segOn ? 0 : 1); }
        enabledCombo->setEnabled(m_writable);
        connect(enabledCombo, &QComboBox::currentIndexChanged, this,
                [this, key](int idx) {
                    submitStationToggle(key.toStdString(), idx == 0);
                });
        lay->addWidget(enabledCombo);
        if (!axisToken.isEmpty()) {
            auto* axisCombo = new QComboBox(this);
            axisCombo->setObjectName(QStringLiteral("ird_segment_axis_combo"));
            axisCombo->addItem(QStringLiteral("ToolZ"));
            axisCombo->addItem(QStringLiteral("ReferenceZ"));
            { QSignalBlocker blocker(axisCombo);
            axisCombo->setCurrentText(axisToken); }
            axisCombo->setEnabled(m_writable);
            axisCombo->setToolTip(
                QStringLiteral("进退轴（§4.3——ToolZ 工具系／ReferenceZ 参考系）"));
            connect(axisCombo, &QComboBox::currentIndexChanged, this,
                    [this, key, axisCombo](int) {
                        submitStationEnumValue(
                            key.toStdString()
                                + QStringLiteral("-axis").toStdString(),
                            axisCombo->currentText());
                    });
            lay->addWidget(axisCombo);
        }
        if (hasDist) {
            auto* spin = new QDoubleSpinBox(this);
            spin->setDecimals(6);
            spin->setSingleStep(0.01);
            spin->setRange(1.0e-12, 1.0e9);  // 正数下界（spec 同口径预过滤）
            { QSignalBlocker blocker(spin);
            spin->setValue(dist); }
            spin->setEnabled(m_writable);
            spin->setMaximumWidth(130);
            connect(spin, &QDoubleSpinBox::editingFinished, this,
                    [this, key, spin]() {
                        // 距离 spec 键（approach/retract——work 无距离）。
                        const QString specKey =
                            key == QStringLiteral("segment-approach")
                                ? QStringLiteral("segment-approach-distance")
                                : QStringLiteral("segment-retract-distance");
                        submitStationValue(specKey.toStdString(),
                                           formatSiText(spin->value()));
                    });
            lay->addWidget(spin);
            lay->addWidget(new QLabel(QStringLiteral("m"), this));
        }
        lay->addStretch(1);
        m_stationSegmentForm->addRow(
            new QLabel(QString::fromStdString(r.label), this), cell);
    };

    // 行分拣路由（键族集合封闭——未知键＝投影/卡片漂移 fail-fast）。
    std::vector<const StationFieldRow*> dofRows;
    for (const StationFieldRow& r : m_stationRows) {
        if (r.fieldKey.rfind("dof-", 0) == 0) {
            dofRows.push_back(&r);
            continue;
        }
        if (r.fieldKey == "enabled") {
            renderSwitchRow(m_stationBasicForm, r,
                            [this, key = r.fieldKey](bool on) {
                                submitStationToggle(key, on);
                            });
            continue;
        }
        if (r.fieldKey == "pose-position") {
            renderPoseRow(r);
            continue;
        }
        if (r.fieldKey == "tolerance-position"
            || r.fieldKey == "tolerance-orientation") {
            renderSpinRow(r);
            continue;
        }
        if (r.fieldKey == "segment-approach" || r.fieldKey == "segment-work"
            || r.fieldKey == "segment-retract") {
            renderSegmentRow(r);
            continue;
        }
        QFormLayout* form = stationFormForKey(r.fieldKey);
        if (form == nullptr) {
            throw std::logic_error("工位面板：投影行键无卡片宿主 " + r.fieldKey);
        }
        renderTextRow(form, r);
    }
    renderDofMatrix(dofRows);
}

void RequirementsPanelWidget::showStatusLine(const QString& text)
{
    // 状态文本统一出口（R2 警示条——横幅可见性随文本：非空即示、清空即
    // 隐；m_statusLine＝横幅内消息标签，showCommandFeedbackText 断言面
    // 不变）。
    m_statusLine->setText(text);
}

QFormLayout* RequirementsPanelWidget::stationFormForKey(const std::string& key) const
{
    // 词表分派（封闭集合＝stationFieldsFor 全键；dof/enabled/pose/容差/
    // segment-* 在分拣层已截获不落此处）。
    if (key == "name" || key == "level" || key == "source" || key == "process-tag"
        || key == "sequence-key" || key == "note") {
        return m_stationBasicForm;
    }
    if (key == "orientation-kind" || key == "fixed-rpy-r" || key == "fixed-rpy-p"
        || key == "fixed-rpy-y" || key == "target-frame" || key == "target-point-x"
        || key == "target-point-y" || key == "target-point-z" || key == "roll-min"
        || key == "roll-max" || key == "target-scene" || key == "feature"
        || key == "invert-normal") {
        return m_stationOrientForm;
    }
    return nullptr;  // 词表外键＝投影/卡片漂移（调用方 fail-fast）
}

void RequirementsPanelWidget::submitStationValue(const std::string& key,
                                                 const QString& text)
{
    // 队列化出信号处理器（UI-T37 返工——修改容差/启用即崩的根因修复）：
    // 提交链会同步刷新并清空重建控件树；若在信号槽内直接执行，正在发射
    // 信号的控件（SpinBox/行编辑/Switch）会在发射途中被删除＝use-after-
    // free。旧插件同场景不崩＝其刷新为固定控件回填不重建；本面板重建式
    // 刷新下，队列化（事件循环下一拍执行，控件删除发生在发射完成后）是
    // 等价的出险排除。
    QMetaObject::invokeMethod(
        this,
        [this, key, text] { submitStationValueNow(key, text); },
        Qt::QueuedConnection);
}

void RequirementsPanelWidget::submitStationValueNow(const std::string& key,
                                                    const QString& text)
{
    // L-R2 提交轨（复合/SpinBox 控件共用——与行编辑槽同语义；显键形供
    // 非 QLineEdit 控件复用）。权威值短路在控件侧已完成。
    m_threadGuard.assertOnUiThread();
    IRequirementEditor* editor = m_editTarget ? m_editTarget() : nullptr;
    if (editor == nullptr || !m_writable || !m_lastSelected.has_value()) {
        return;
    }
    const RequirementWorkingSet& ws = editor->workingSet();
    for (const TaskPoint& p : ws.points.entries) {
        if (p.objectId != m_lastSelected.value()) {
            continue;
        }
        for (const ui::QuantityFieldSpec& spec : stationQuantitySpecs()) {
            if (spec.key != key) {
                continue;
            }
            const ui::ValueParseResult parsed =
                parseFieldValueText(text.toStdString(), spec);
            if (!parsed.ok) {
                showStatusLine(QString::fromStdString(parsed.reason));
                renderInspector(ws);  // 回退显示工作集权威值
                return;
            }
            ui::ParamEditSet edits;
            ui::ParamChange change;
            change.key = key;
            change.label = spec.label;
            change.newSi = parsed.siValue;
            edits.changes.push_back(change);
            std::vector<std::string> known;
            const TaskPoint candidate = applyStationEditSet(p, edits, known);
            const EditSubmitOutcome outcome = submitEntryEdit(*editor, *this, candidate);
            if (outcome == EditSubmitOutcome::Applied) {
                m_undoTracker.recordAppliedEdit();
            }
            return;
        }
        throw std::logic_error("工位面板：编辑行携带词表外键 " + key);
    }
}

void RequirementsPanelWidget::submitStationToggle(const std::string& key, bool on)
{
    // 队列化出信号处理器（同 submitStationValue——提交链同步删发射控件＝UB）。
    QMetaObject::invokeMethod(
        this, [this, key, on] { submitStationToggleNow(key, on); },
        Qt::QueuedConnection);
}

void RequirementsPanelWidget::submitStationEnumValue(const std::string& key,
                                                     const QString& text)
{
    // 队列化出信号处理器（QComboBox currentIndexChanged 同样可能发生在
    // 面板重建期——同 UB 排除口径）。
    QMetaObject::invokeMethod(
        this, [this, key, text] { submitStationEnumValueNow(key, text); },
        Qt::QueuedConnection);
}

void RequirementsPanelWidget::submitStationEnumValueNow(const std::string& key,
                                                        const QString& text)
{
    // 枚举提交轨（applyStationEnumEdit——等级 QComboBox；域裁决同链）。
    m_threadGuard.assertOnUiThread();
    IRequirementEditor* editor = m_editTarget ? m_editTarget() : nullptr;
    if (editor == nullptr || !m_writable || !m_lastSelected.has_value()) {
        return;
    }
    const RequirementWorkingSet& ws = editor->workingSet();
    for (const TaskPoint& p : ws.points.entries) {
        if (p.objectId != m_lastSelected.value()) {
            continue;
        }
        std::vector<std::string> known;
        const TaskPoint candidate =
            applyStationEnumEdit(p, key, text.toStdString(), known);
        const EditSubmitOutcome outcome = submitEntryEdit(*editor, *this, candidate);
        if (outcome == EditSubmitOutcome::Applied) {
            m_undoTracker.recordAppliedEdit();
        }
        return;
    }
}

void RequirementsPanelWidget::submitRegionEnumValue(const std::string& key,
                                                    const QString& text)
{
    // 队列化出信号处理器（同 submitStationValue——UB 排除）。
    QMetaObject::invokeMethod(
        this, [this, key, text] { submitRegionEnumValueNow(key, text); },
        Qt::QueuedConnection);
}

void RequirementsPanelWidget::submitRegionEnumValueNow(const std::string& key,
                                                       const QString& text)
{
    // 区域枚举提交轨（applyRegionEnumEdit——等级 QComboBox；域裁决同链）。
    m_threadGuard.assertOnUiThread();
    IRequirementEditor* editor = m_editTarget ? m_editTarget() : nullptr;
    if (editor == nullptr || !m_writable) {
        return;
    }
    const RequirementWorkingSet& ws = editor->workingSet();
    for (const WorkRegion& region : ws.regions.entries) {
        std::optional<core::ObjectId> selected;
        if (m_regionTable->currentItem() != nullptr) {
            selected = nodeAnchor(m_regionTable->currentItem());
        }
        if (!selected.has_value() || region.objectId != selected.value()) {
            continue;
        }
        std::vector<std::string> known;
        const WorkRegion candidate =
            applyRegionEnumEdit(region, key, text.toStdString(), known);
        const EditSubmitOutcome outcome =
            submitEntryEdit(*editor, *this, RequirementEdit{candidate});
        if (outcome == EditSubmitOutcome::Applied) {
            m_undoTracker.recordAppliedEdit();
        }
        return;
    }
}

void RequirementsPanelWidget::submitConditionEnumValue(const std::string& key,
                                                       const QString& text)
{
    // 队列化出信号处理器（同 submitStationValue——UB 排除）。
    QMetaObject::invokeMethod(
        this, [this, key, text] { submitConditionEnumValueNow(key, text); },
        Qt::QueuedConnection);
}

void RequirementsPanelWidget::submitConditionEnumValueNow(const std::string& key,
                                                          const QString& text)
{
    // 工况枚举提交轨（applyConditionEnumEdit——等级/启用；域裁决同链）。
    m_threadGuard.assertOnUiThread();
    IRequirementEditor* editor = m_editTarget ? m_editTarget() : nullptr;
    if (editor == nullptr || !m_writable) {
        return;
    }
    const RequirementWorkingSet& ws = editor->workingSet();
    for (const OperatingCondition& c : ws.conditions.entries) {
        std::optional<core::ObjectId> selected;
        if (m_conditionTable->currentItem() != nullptr) {
            selected = nodeAnchor(m_conditionTable->currentItem());
        }
        if (!selected.has_value() || c.objectId != selected.value()) {
            continue;
        }
        std::vector<std::string> known;
        const OperatingCondition candidate =
            applyConditionEnumEdit(c, key, text.toStdString(), known);
        const EditSubmitOutcome outcome =
            submitEntryEdit(*editor, *this, RequirementEdit{candidate});
        if (outcome == EditSubmitOutcome::Applied) {
            m_undoTracker.recordAppliedEdit();
        }
        return;
    }
}

void RequirementsPanelWidget::submitStationToggleNow(const std::string& key, bool on)
{
    // 二态提交轨（启用开关/自由度矩阵——applyStationToggleEdit 布尔回填
    // ＋submitEntryEdit 域裁决；拒绝面与行编辑同链）。
    m_threadGuard.assertOnUiThread();
    IRequirementEditor* editor = m_editTarget ? m_editTarget() : nullptr;
    if (editor == nullptr || !m_writable || !m_lastSelected.has_value()) {
        return;
    }
    const RequirementWorkingSet& ws = editor->workingSet();
    for (const TaskPoint& p : ws.points.entries) {
        if (p.objectId != m_lastSelected.value()) {
            continue;
        }
        std::vector<std::string> known;
        const TaskPoint candidate = applyStationToggleEdit(p, key, on, known);
        const EditSubmitOutcome outcome = submitEntryEdit(*editor, *this, candidate);
        if (outcome == EditSubmitOutcome::Applied) {
            m_undoTracker.recordAppliedEdit();
        }
        return;
    }
}

void RequirementsPanelWidget::renderRegionPage(const RequirementWorkingSet& ws)
{
    const std::optional<core::ObjectId> previousRegion =
        m_regionTable->currentItem() ? nodeAnchor(m_regionTable->currentItem()) : std::nullopt;
    // 区域表（一区域一行——L-R1 行选中锚）。UI-T36：三显示列补全
    // （空间采样＝方法＋计数摘要、覆盖目标＝P≥/O≥ 阈值摘要——模型层
    // regionRows 既有投影字段；内部 ObjectId 锚退居末隐藏列）。
    const std::vector<RegionRow> rows = regionRows(ws.regions.entries);
    // 重建期信号屏蔽（UI-T37 返工②——修复①：恢复选中的 setCurrentItem
    // 触发 itemSelectionChanged→onTreeSelectionChanged 把页签切到区域并
    // 改写 m_lastSelected＝"点启用后跳到区域界面"的根因；程序性重建非
    // 用户点击，L-R1 信号只在真实交互时发射）。
    m_regionTable->blockSignals(true);
    m_regionTable->clear();
    for (const RegionRow& r : rows) {
        auto* item = makeTableRow(QString::fromStdString(r.name),
                                  QString::fromStdString(r.samplingText),
                                  QString::fromStdString(r.coverageText),
                                  r.objectId);
        m_regionTable->addTopLevelItem(item);
        if (previousRegion.has_value() && r.objectId == previousRegion.value()) {
            m_regionTable->setCurrentItem(item);
        }
    }
    m_regionTable->blockSignals(false);
    // 列宽策略（UI-T39——审核布局返工：原三列全部 resizeColumnToContents
    // 在名称/采样摘要较长时把数值列挤到滚动，右侧属性卡可见面积缩水）。
    // 名称列伸缩（占满余宽——主定位列），采样/覆盖两列内容自适应＋用户
    // 可调（Interactive——初始宽度随内容一次成形，长文本截断由 tooltip
    // 承载完整语义，不再横向挤压）。
    QHeaderView* regionHeader = m_regionTable->header();
    regionHeader->setSectionResizeMode(0, QHeaderView::Stretch);
    regionHeader->setSectionResizeMode(1, QHeaderView::Interactive);
    regionHeader->setSectionResizeMode(2, QHeaderView::Interactive);
    if (!m_regionColumnsSized && !rows.empty()) {
        // 首批数据到达时按内容一次成形两摘要列（后续刷新不再改写——用户
        // 手调的列宽跨刷新保持；m_regionColumnsSized 会话内一次性旗标）。
        m_regionTable->resizeColumnToContents(1);
        m_regionTable->resizeColumnToContents(2);
        m_regionColumnsSized = true;
    }
    // 返工⑤：选择成员与表事实同步（restore 成败即成员增删——空态提示与
    // 复制/删除置灰据此驱动，不再依赖重建期信号）。
    m_regionSelectedId = m_regionTable->currentItem() != nullptr
                             ? nodeAnchor(m_regionTable->currentItem())
                             : std::optional<core::ObjectId>();
    // 区域检查器（选中区域行→regionFieldsFor；未选中＝空卡片——不虚构）。
    // UI-T37 R1：卡片化路由（基础/包围盒复合行/采样达标/高级折叠）。
    // 盒中心/盒尺寸三标量行视觉合并为复合行——各编辑器保持原 fieldKey/
    // 权威值/提交槽（irdFieldKey 定位面与提交语义零变化）。
    clearFormRows(m_regionBasicForm);
    clearBoxRows(m_regionBoxLay);
    clearBoxRows(m_regionSamplingLay);
    clearFormRows(m_regionAdvancedForm);
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

        // 词表面（可编辑裁决——UI-T37：采样计数/间距/随机数/备注等非词表
        // 行诚实灰显〔此前可编辑但提交即词表外键 fail-fast〕；覆盖率姿态
        // 行本批入词表可设值）。
        const std::vector<ui::QuantityFieldSpec> specs = regionQuantitySpecs();
        const auto specDriven = [&specs](const std::string& key) {
            for (const ui::QuantityFieldSpec& s : specs) {
                if (s.key == key) {
                    return true;
                }
            }
            return false;
        };

        // 普通文本行（含灰显规则与提交槽挂接——原 B2 行为保持）。
        auto renderRegionTextRow = [&](QFormLayout* form, const StationFieldRow& fr) {
            // 等级下拉行（UI-T37 返工②——枚举一律 QComboBox；提交轨＝
            // applyRegionEnumEdit，词表 Must/Should）。
            if (fr.fieldKey == "level"
                && fr.enablement == StationFieldEnablement::Editable) {
                auto* combo = new QComboBox(this);
                combo->setObjectName(QStringLiteral("ird_region_level_combo"));
                combo->addItem(QStringLiteral("Must"));
                combo->addItem(QStringLiteral("Should"));
                { QSignalBlocker blocker(combo);
                combo->setCurrentText(QString::fromStdString(fr.valueText)); }
                combo->setEnabled(m_writable);
                connect(combo, &QComboBox::currentIndexChanged, this,
                        [this, key = fr.fieldKey, combo](int) {
                            submitRegionEnumValue(key, combo->currentText());
                        });
                form->addRow(QString::fromStdString(fr.label), combo);
                return;
            }
            const bool placeholder =
                fr.valueText == "未提供" || fr.valueText == "未设";
            auto* edit = new QLineEdit(
                placeholder ? QString() : QString::fromStdString(fr.valueText), this);
            const bool editable = fr.enablement == StationFieldEnablement::Editable
                                  && specDriven(fr.fieldKey) && m_writable;
            edit->setReadOnly(!editable);
            edit->setProperty("irdFieldKey", QString::fromStdString(fr.fieldKey));
            edit->setProperty("irdAuthoritativeValue",
                              QString::fromStdString(
                                  placeholder ? std::string{} : fr.valueText));
            if (editable) {
                connect(edit, &QLineEdit::editingFinished, this,
                        &RequirementsPanelWidget::onRegionFieldEditingFinished);
            }
            form->addRow(QString::fromStdString(fr.label), edit);
        };

        // 盒复合行（acceptance 2——三标量行合并为"一行三值＋单位"；编辑器
        // 逐个保持既有键/权威值/槽，视觉合并零提交面变化）。
        auto renderBoxCompositeRow = [&](const char* title, const char* unit,
                                         const char* kx, const char* ky,
                                         const char* kz,
                                         const std::vector<StationFieldRow>& all) {
            auto* cell = new QWidget(this);
            auto* lay = new QHBoxLayout(cell);
            lay->setContentsMargins(0, 0, 0, 0);
            lay->setSpacing(4);
            static const char* kAxes[] = {"X", "Y", "Z"};
            const char* keys[] = {kx, ky, kz};
            for (int i = 0; i < 3; ++i) {
                lay->addWidget(new QLabel(QString::fromLatin1(kAxes[i]), this));
                const StationFieldRow* row = nullptr;
                for (const StationFieldRow& fr : all) {
                    if (fr.fieldKey == keys[i]) {
                        row = &fr;
                        break;
                    }
                }
                if (row == nullptr) {
                    throw std::logic_error(std::string("区域面板：盒复合行缺键 ")
                                           + keys[i]);
                }
                auto* edit = new QLineEdit(
                    QString::fromStdString(row->valueText), this);
                const bool editable =
                    row->enablement == StationFieldEnablement::Editable && m_writable;
                edit->setReadOnly(!editable);
                edit->setMaximumWidth(110);
                edit->setProperty("irdFieldKey",
                                  QString::fromStdString(row->fieldKey));
                edit->setProperty("irdAuthoritativeValue",
                                  QString::fromStdString(row->valueText));
                if (editable) {
                    connect(edit, &QLineEdit::editingFinished, this,
                            &RequirementsPanelWidget::onRegionFieldEditingFinished);
                }
                lay->addWidget(edit);
            }
            lay->addWidget(new QLabel(QString::fromLatin1(unit), this));
            lay->addStretch(1);
            m_regionBoxLay->addWidget(new QLabel(QString::fromUtf8(title), this));
            m_regionBoxLay->addWidget(cell);
        };

        // 覆盖率滑块＋输入双联（acceptance 2——0~100% 双向联动；行编辑器
        // 保留原键/权威值/提交槽，滑块为伴生控件：拨动＝组词提交，输入行
        // 仍走原 editingFinished 轨——双入口一出口）。
        auto renderCoverageRow = [&](const StationFieldRow& fr) {
            auto* cell = new QWidget(this);
            auto* lay = new QHBoxLayout(cell);
            lay->setContentsMargins(0, 0, 0, 0);
            lay->setSpacing(4);
            auto* slider = new QSlider(Qt::Horizontal, this);
            slider->setObjectName(QStringLiteral("ird_region_coverage_slider"));
            slider->setRange(0, 100);
            slider->setTracking(false);  // 拖动只走视觉——松手才发 valueChanged（提交去抖）
            { QSignalBlocker blocker(slider);  // 初始化静默（valueChanged→提交→重入）
            slider->setValue(static_cast<int>(
                QString::fromStdString(fr.valueText).toDouble() * 100.0 + 0.5));
            }
            auto* edit = new QLineEdit(QString::fromStdString(fr.valueText), this);
            edit->setMaximumWidth(90);
            const bool editable =
                fr.enablement == StationFieldEnablement::Editable && m_writable;
            slider->setEnabled(editable);
            edit->setReadOnly(!editable);
            const std::string key = fr.fieldKey;
            edit->setProperty("irdFieldKey", QString::fromStdString(key));
            edit->setProperty("irdAuthoritativeValue",
                              QString::fromStdString(fr.valueText));
            if (editable) {
                connect(edit, &QLineEdit::editingFinished, this,
                        &RequirementsPanelWidget::onRegionFieldEditingFinished);
                connect(slider, &QSlider::valueChanged, this, [this, key, edit](int v) {
                    // 滑块→输入行组词提交（比率 0~1 六位裁尾——formatSiText
                    // 与 spec 解析词形一致）；权威值对照防幻影。
                    const QString text = formatSiText(v / 100.0);
                    if (text == edit->property("irdAuthoritativeValue").toString()) {
                        return;
                    }
                    edit->setText(text);
                    submitRegionValue(key, text);
                });
            }
            lay->addWidget(slider, /*stretch=*/1);
            lay->addWidget(edit);
            lay->addWidget(new QLabel(QStringLiteral("%"), this));
            m_regionSamplingLay->addWidget(new QLabel(QString::fromStdString(fr.label), this));
            m_regionSamplingLay->addWidget(cell);
        };

        // 采样计数复合行（acceptance 2——三轴分割数＋『共 N 个离散点』实时
        // 预览；提交轨＝applyRegionSamplingCountsEdit〔formatCounts 逆变换
        // 整数回填〕，域规范化/裁决仍走 submitEntryEdit）。
        auto renderCountsRow = [&](const StationFieldRow& fr) {
            auto* cell = new QWidget(this);
            auto* lay = new QHBoxLayout(cell);
            lay->setContentsMargins(0, 0, 0, 0);
            lay->setSpacing(4);
            const QString text = QString::fromStdString(fr.valueText);
            const QStringList parts =
                text.split(QChar(0x00D7));  // '×'＝U+00D7（formatCounts 词面——窄字面量经执行字符集截断不匹配，首轮 gui 实证）
            std::array<std::uint32_t, 3> init{1, 1, 1};
            const bool ok = parts.size() == 3;
            auto* countLabel = new QLabel(this);
            countLabel->setObjectName(QStringLiteral("ird_region_count_label"));
            auto syncCount = [countLabel, lay]() {
                // 实时预览（纯呈现乘法——乘积即离散点总数）。
                std::uint64_t product = 1;
                for (int i = 0; i < lay->count(); ++i) {
                    if (auto* sp = qobject_cast<QSpinBox*>(lay->itemAt(i)->widget())) {
                        product *= sp->value();
                    }
                }
                countLabel->setText(QStringLiteral("▸ 共 %1 个离散点").arg(product));
            };
            const bool editable = fr.enablement == StationFieldEnablement::Editable
                                  && m_writable;
            static const char* kAxes[] = {"X", "Y", "Z"};
            for (int i = 0; i < 3; ++i) {
                lay->addWidget(new QLabel(QString::fromLatin1(kAxes[i]), this));
                auto* sp = new QSpinBox(this);
                sp->setRange(1, 999);  // ≥1（§5.2——零样本非 Grid 词形）
                sp->setObjectName(QStringLiteral("ird_region_count_spin"));
                if (ok) {
                    { QSignalBlocker blocker(sp);  // 初始化静默（valueChanged→提交→重入）
                    sp->setValue(parts[i].toInt()); }
                }
                sp->setEnabled(editable);
                lay->addWidget(sp);
            }
            if (editable) {
                const QString authoritative = QString::fromStdString(fr.valueText);
                for (int i = 0; i < lay->count(); ++i) {
                    if (auto* sp = qobject_cast<QSpinBox*>(lay->itemAt(i)->widget())) {
                        connect(sp, qOverload<int>(&QSpinBox::valueChanged), this,
                                [this, cell, syncCount, authoritative](int) {
                                    syncCount();
                                    // 提交词（三 spin 现值）——幻影守卫：与
                                    // 权威词同形则零提交。
                                    std::array<std::uint32_t, 3> now{1, 1, 1};
                                    int idx = 0;
                                    for (int j = 0; j < cell->layout()->count(); ++j) {
                                        if (auto* s = qobject_cast<QSpinBox*>(
                                                cell->layout()->itemAt(j)->widget())) {
                                            now[idx++] = static_cast<std::uint32_t>(s->value());
                                        }
                                    }
                                    const QString word =
                                        QString::number(now[0]) + QChar(0x00D7)
                                        + QString::number(now[1]) + QChar(0x00D7)
                                        + QString::number(now[2]);
                                    if (word == authoritative) {
                                        return;
                                    }
                                    submitRegionCounts(now);
                                });
                    }
                }
            }
            lay->addWidget(new QLabel(QStringLiteral("×"), this));
            lay->addWidget(countLabel);
            lay->addStretch(1);
            m_regionSamplingLay->addWidget(new QLabel(QString::fromStdString(fr.label), this));
            m_regionSamplingLay->addWidget(cell);
            syncCount();
        };

        // 启用 Switch 行（区域轨——applyRegionToggleEdit）。
        auto renderRegionSwitchRow = [&](const StationFieldRow& fr) {
            auto* combo = new QComboBox(this);
            combo->setObjectName(QStringLiteral("ird_region_enabled_combo"));
            combo->addItem(QStringLiteral("启用"));
            combo->addItem(QStringLiteral("停用"));
            { QSignalBlocker blocker(combo);  // 初始化静默（防触发提交重入）
            combo->setCurrentIndex(QString::fromStdString(fr.valueText) == QStringLiteral("是") ? 0 : 1); }
            const bool enabled =
                fr.enablement == StationFieldEnablement::Editable && m_writable;
            combo->setEnabled(enabled);
            if (enabled) {
                connect(combo, &QComboBox::currentIndexChanged, this,
                        [this, key = fr.fieldKey](int idx) {
                            submitRegionToggle(key, idx == 0);
                        });
            }
            m_regionBasicForm->addRow(QString::fromStdString(fr.label), combo);
        };

        // 分拣路由（键族封闭——未知键 fail-fast，区域轨与工位轨同纪律）。
        std::vector<const StationFieldRow*> boxCenter;
        std::vector<const StationFieldRow*> boxSize;
        const StationFieldRow* countsRow = nullptr;
        for (const StationFieldRow& fr : fields) {
            const std::string& k = fr.fieldKey;
            if (k == "box-center-x" || k == "box-center-y" || k == "box-center-z") {
                boxCenter.push_back(&fr);
                continue;
            }
            if (k == "box-size-x" || k == "box-size-y" || k == "box-size-z") {
                boxSize.push_back(&fr);
                continue;
            }
            if (k == "sampling-counts") {
                countsRow = &fr;
                continue;
            }
            if (k == "coverage-position" || k == "coverage-orientation") {
                // 姿态覆盖率『未设』＝灰显占位（本批已入词表——有值即可滑；
                // 未设态收进高级卡占位呈现，不伪造零）。
                if (fr.valueText == "未设") {
                    renderRegionTextRow(m_regionAdvancedForm, fr);
                } else {
                    renderCoverageRow(fr);
                }
                continue;
            }
            if (k == "sequence-key" || k == "note" || k == "sampling-spacing"
                || k == "sampling-normalized" || k == "sampling-random"
                || k == "orientation-sampling") {
                renderRegionTextRow(m_regionAdvancedForm, fr);
                continue;
            }
            if (k == "enabled") {
                renderRegionSwitchRow(fr);
                continue;
            }
            if (k == "name" || k == "level" || k == "ref-frame" || k == "mandatory") {
                renderRegionTextRow(m_regionBasicForm, fr);
                continue;
            }
            throw std::logic_error("区域面板：投影行键无卡片宿主 " + k);
        }
        if (boxCenter.size() == 3) {
            renderBoxCompositeRow("盒中心 (X/Y/Z)", "m", "box-center-x",
                                  "box-center-y", "box-center-z", fields);
        }
        if (boxSize.size() == 3) {
            renderBoxCompositeRow("盒尺寸 (长/宽/高)", "m", "box-size-x",
                                  "box-size-y", "box-size-z", fields);
        }
        if (countsRow != nullptr) {
            renderCountsRow(*countsRow);
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
    m_conditionTable->blockSignals(true);  // 重建期信号屏蔽（同区域表——返工②修复①）
    m_conditionTable->clear();
    for (const ConditionRow& r : rows) {
        // UI-T36：三显示列补全（目标节拍＝数值 s/未设、适用范围＝三值
        // 词表摘要——模型层 conditionRows 既有投影字段；锚退居末隐藏列）。
        // UI-T37 R2：第四列『验收』Tag（必验/可选）——派生经域函数
        // resolveRequiredCases 单条目解析（P-EV-9 单点，面板零复判；
        // conditionFieldsFor 必验行同源口径）。
        QString verifyText;
        for (const OperatingCondition& c : ws.conditions.entries) {
            if (c.objectId != r.objectId) { continue; }
            const std::vector<OperatingCondition> single{c};
            const RequiredCaseResolution resolution =
                m_conditionService.resolveRequiredCases(single);
            for (const RequiredCaseEntry& e : resolution.entries) {
                if (e.caseId == r.objectId && e.enabled && e.mandatory) {
                    verifyText = QStringLiteral("必验");
                    break;
                }
            }
            if (verifyText.isEmpty()) {
                verifyText = QStringLiteral("可选");
            }
            break;
        }
        auto* item = makeTableRow4(QString::fromStdString(r.name),
                                   QString::fromStdString(r.cycleText),
                                   QString::fromStdString(r.appliesToText),
                                   verifyText,
                                   r.objectId);
        m_conditionTable->addTopLevelItem(item);
        if (previousCondition.has_value() && r.objectId == previousCondition.value()) {
            m_conditionTable->setCurrentItem(item);
        }
    }
    m_conditionTable->blockSignals(false);
    // 列宽策略（UI-T39——审核布局返工，区域表同款）：名称列伸缩；节拍/
    // 必验两短列内容自适应一次成形；适用范围列（长文本高发——多工况枚举
    // 摘要）固定上限宽度，截断语义由行 tooltip 承载，不再横向挤压。
    QHeaderView* conditionHeader = m_conditionTable->header();
    conditionHeader->setSectionResizeMode(0, QHeaderView::Stretch);
    conditionHeader->setSectionResizeMode(1, QHeaderView::Interactive);
    conditionHeader->setSectionResizeMode(2, QHeaderView::Interactive);
    conditionHeader->setSectionResizeMode(3, QHeaderView::Interactive);
    if (!m_conditionColumnsSized && !rows.empty()) {
        m_conditionTable->resizeColumnToContents(1);
        m_conditionTable->resizeColumnToContents(3);
        m_conditionTable->resizeColumnToContents(2);
        // 适用范围列上限（240 px≈4~5 个工况枚举的摘要宽度——超长走
        // tooltip；此上限同时是属性卡可见面积的保护线）。
        if (m_conditionTable->columnWidth(2) > 240) {
            m_conditionTable->setColumnWidth(2, 240);
        }
        m_conditionColumnsSized = true;
    }
    // 返工⑤：选择成员与表事实同步（同区域页——空态/置灰判定源）。
    m_conditionSelectedId = m_conditionTable->currentItem() != nullptr
                                ? nodeAnchor(m_conditionTable->currentItem())
                                : std::optional<core::ObjectId>();
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
                        // 枚举下拉行（UI-T37 返工②——等级/启用一律
                        // QComboBox；提交轨＝applyConditionEnumEdit）。
                        if ((fr.fieldKey == "level"
                                || fr.fieldKey == "enabled")) {
                            auto* combo = new QComboBox(this);
                            combo->setObjectName(
                                fr.fieldKey == "level"
                                    ? QStringLiteral("ird_condition_level_combo")
                                    : QStringLiteral(
                                        "ird_condition_enabled_combo"));
                            if (fr.fieldKey == "level") {
                                combo->addItem(QStringLiteral("Must"));
                                combo->addItem(QStringLiteral("Should"));
                                { QSignalBlocker blocker(combo);
                                combo->setCurrentText(
                                    QString::fromStdString(fr.valueText)); }
                            } else {
                                combo->addItem(QStringLiteral("启用"));
                                combo->addItem(QStringLiteral("停用"));
                                { QSignalBlocker blocker(combo);
                                combo->setCurrentIndex(
                                    QString::fromStdString(fr.valueText)
                                            == QStringLiteral("是")
                                        ? 0
                                        : 1); }
                            }
                            combo->setEnabled(m_writable);
                            connect(combo, &QComboBox::currentIndexChanged,
                                    this, [this, key = fr.fieldKey, combo](int) {
                                        // enabled 词面＝是/否（模型词表）；level＝token 直投。
                                        const QString text = key == QStringLiteral("enabled")
                                            ? (combo->currentIndex() == 0 ? QStringLiteral("是")
                                                                          : QStringLiteral("否"))
                                            : combo->currentText();
                                        submitConditionEnumValue(key, text);
                                    });
                            m_conditionForm->addRow(
                                QString::fromStdString(fr.label), combo);
                            continue;
                        }
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
    // 返工④：必验清单预览表已撤销——必验事实改由单表『是否必验』列
    // 直投（上方行循环内 resolveRequiredCases 单条目解析，P-EV-9 单点
    // 不变）；跨域必验明细经各域页等级列呈现，不再重复建表。
}

void RequirementsPanelWidget::renderValidationPage(const RequirementReadinessReport& report)
{
    // 校验面板（报告→行卡纯投影——零重估零判定，P-REQ-6 边界）。
    m_lastReadiness = report;  // 过滤重投影的报告缓存（UI-T34——值语义深拷贝）
    // UI-T34 E 批次：①页头实时化（阻断计数直投——『尚未执行』静态退役，
    // 报告经 UI-T29 组合子已随编辑实时）；②三过滤（级别/层/码——呈现层
    // 子集选择）；③Blocking 行红色前景（拒绝分组呈现——应用被拒引导的
    // 视觉面）＋过滤后阻断行点击可定位（m_selection.locate 既有轨）。
    const ValidationPanelProjection proj = projectValidationPanel(report);
    m_validationHeader->setText(
        proj.blockingCount > 0
            ? QString("校验：存在 %1 项阻断——先处理校验页阻断项再应用")
                  .arg(proj.blockingCount)
            : QString("校验：就绪（阻断 0 · 警告 %1）").arg(proj.warningCount));
    // 状态看板卡（UI-T37 R2 acceptance 5——通过/存在 N 项二态＋阻塞计数；
    // QSS 动态属性分色：ok＝成功绿描边、warn＝警示橙）。
    m_validationCounts->setText(
        proj.blockingCount > 0
            ? QString("⚠ 存在 %1 项提示（阻塞: %2）")
                  .arg(proj.blockingCount + proj.warningCount)
                  .arg(proj.blockingCount)
            : QString("✔ 结构校验通过（阻断 0 · 提示 %1）").arg(proj.warningCount));
    m_validationCounts->setObjectName(QStringLiteral("ird_validation_status"));
    m_validationCounts->setProperty("state",
                                    proj.blockingCount > 0 ? "warn" : "ok");
    // 动态属性变更需样式重算（QSS property 选择器面）。
    m_validationCounts->style()->unpolish(m_validationCounts);
    m_validationCounts->style()->polish(m_validationCounts);
    m_validationLayers->clear();
    for (const ValidationLayerRow& r : proj.layers) {
        m_validationLayers->addTopLevelItem(new QTreeWidgetItem(QStringList()
            << QString::fromStdString(r.layerToken) << QString::fromStdString(r.title)
            << QString::number(r.blocking) << QString::number(r.warning)));
    }
    // 过滤应答现取（词表封闭：级别 4 值/层 R0~R9/码子串不区分大小写）。
    const int levelChoice = m_validationLevelFilter != nullptr
                                ? m_validationLevelFilter->currentIndex() : 0;
    const QString layerChoice = m_validationLayerFilter != nullptr
                                    ? m_validationLayerFilter->currentText() : QString();
    const QString codeNeed = m_validationCodeFilter != nullptr
                                 ? m_validationCodeFilter->text() : QString();
    m_validationItems->clear();
    for (const ValidationItemRow& r : proj.items) {
        // 过滤判定（呈现层子集选择——不改报告、不改排序；全命中＝全行）。
        if (levelChoice == 1 && r.levelToken != "Blocking") { continue; }
        if (levelChoice == 2 && r.levelToken != "Warning") { continue; }
        if (levelChoice == 3 && r.levelToken != "NotApplicable") { continue; }
        if (layerChoice != QStringLiteral("全部层")
            && QString::fromStdString(r.layerToken) != layerChoice) {
            continue;
        }
        if (!codeNeed.isEmpty()
            && !QString::fromStdString(r.code).contains(codeNeed, Qt::CaseInsensitive)) {
            continue;
        }
        auto* item = new QTreeWidgetItem(QStringList()
            << QString::fromStdString(r.levelToken)
            << semanticLayerLabel(proj.layers, r.layerToken)
            << QString::fromStdString(r.code) << QString::fromStdString(r.summary));
        // 语义标签旁路（UX-02 诚实呈现双轨——显示面用语义类别，原始层码
        // 入 Tooltip 供日志检索；过滤行仍按层码词表）。
        item->setToolTip(1, QString::fromStdString(r.layerToken));
        setNodeAnchor(item, r.jumpTarget);  // 跳转锚（无定位行＝空锚——不可点击跳转）
        if (r.levelToken == "Blocking") {
            // 阻断行视觉分组（深红前景——拒绝分组呈现；文案零加工）。
            item->setForeground(0, QBrush(QColor(176, 32, 32)));
            item->setForeground(3, QBrush(QColor(176, 32, 32)));
        }
        m_validationItems->addTopLevelItem(item);
    }
    // 修订语义一行化（UI-T37 R2 acceptance 5——去噪：成段说明文字收
    // Tooltip，面板面只留一行微文案；『修订只增不改』关键词保留——
    // UI-T34 acceptance 3 的语义文案不丢）。
    m_validationNotes->setText(
        QString::fromUtf8("ⓘ 修订说明：应用＝产生新修订（修订只增不改）；"
                          "应用前的编辑保留在草稿，可撤销。"));
    m_validationNotes->setToolTip(
        QString::fromStdString(proj.previewNote) + "\n\n"
        + QString::fromStdString(proj.formalNote));
}

// =====================================================================
// 槽：用户事件→模型/域入口转接
// =====================================================================

void RequirementsPanelWidget::updateTabHeaders()
{
    // 页签状态行刷新（UI-T26；返工⑤格式精简——『工位：未选择对象』与
    // 页签名语义重复，改面包屑『工位 > 〈对象名|未选择〉』）。对象名＝
    // 当前树选中行的显示文本（与用户在树里看到的名字同源——树行文本即
    // displayLabel 投影，零第二名称源）。校验页头不在此刷新（UI-T34——
    // 随 renderValidationPage 的报告实时化）。
    // UI-T39（审核 P1"双树职责"）：有选中对象时页头追加『（草稿）』标注
    // ——本面板呈现的是需求草稿工作集（基线＋未应用编辑），对象尚未成为
    // 已应用修订的一部分；已应用对象见宿主共享工业项目树。标注文案经
    // UiText（panel.requirements.draft-marker）——NFR-MNT-03 单一出口。
    const QString objectName =
            (m_tree != nullptr && m_tree->currentItem() != nullptr)
                ? m_tree->currentItem()->text(0)
                : QString();
    const QString draftMarker = QString::fromStdString(ui::resolveText(
        ui::TextKey{"panel.requirements.draft-marker"}));
    const QString suffix =
            objectName.isEmpty()
                ? QStringLiteral("未选择")
                : objectName + QStringLiteral("（") + draftMarker
                      + QStringLiteral("）");
    if (m_stationHeader != nullptr) {
        m_stationHeader->setText(QStringLiteral("工位 > ") + suffix);
    }
    if (m_regionHeader != nullptr) {
        m_regionHeader->setText(QStringLiteral("区域 > ") + suffix);
    }
    if (m_conditionHeader != nullptr) {
        m_conditionHeader->setText(QStringLiteral("工况 > ") + suffix);
    }
}

void RequirementsPanelWidget::onTreeSelectionChanged()
{
    // L-R1 正向半区：树/区域表/工况表选中→会话态＋检查器重投影（零修订）。
    // UI-T36：需求树分组/条目点击均联动右侧页签；分组/根节点无锚——仅切
    // 页签，不清空既有选中锚（点分组＝浏览该类对象，不破坏当前编辑目标）。
    std::optional<core::ObjectId> anchor;
    if (QObject::sender() == m_tree && m_tree->currentItem() != nullptr) {
        QTreeWidgetItem* item = m_tree->currentItem();
        using K = RequirementNodeKind;
        const K kind = static_cast<K>(item->data(0, kNodeKindRole).toInt());
        switch (kind) {
        case K::Point:
        case K::PointsGroup:
            m_pages->setCurrentIndex(0);
            break;
        case K::Region:
        case K::RegionsGroup:
        case K::Plan:  // 计划＝区域采样派生面（联动区域页）
            m_pages->setCurrentIndex(1);
            break;
        case K::Condition:
        case K::ConditionsGroup:
            m_pages->setCurrentIndex(2);
            break;
        default:  // 需求工程根——无页签语义
            break;
        }
        if (item->text(anchorColumnOf(item)).isEmpty()) {
            return;  // 分组/根节点（无锚）——页签已联动，选中锚保持
        }
        anchor = nodeAnchor(item);
    } else if (QObject::sender() == m_regionTable && m_regionTable->currentItem() != nullptr) {
        anchor = nodeAnchor(m_regionTable->currentItem());
    } else if (QObject::sender() == m_conditionTable
               && m_conditionTable->currentItem() != nullptr) {
        anchor = nodeAnchor(m_conditionTable->currentItem());
    }
    // 返工⑤：选择面按集合登记（页内复制/删除置灰＋空态提示的单一事实源
    // ——页内选择面互相独立：树选点不清区域表选中，反之亦然；点分组/根
    // ＝无锚，各页选择面保持——与"分组点击不破坏编辑目标"同语义）。
    if (anchor.has_value()) {
        if (IRequirementEditor* editor = m_editTarget ? m_editTarget() : nullptr) {
            const RequirementWorkingSet& ws = editor->workingSet();
            bool isPoint = false;
            bool isRegion = false;
            bool isCondition = false;
            for (const TaskPoint& p : ws.points.entries) {
                if (p.objectId == anchor.value()) { isPoint = true; break; }
            }
            for (const WorkRegion& r : ws.regions.entries) {
                if (r.objectId == anchor.value()) { isRegion = true; break; }
            }
            for (const OperatingCondition& c : ws.conditions.entries) {
                if (c.objectId == anchor.value()) { isCondition = true; break; }
            }
            if (isPoint) {
                m_stationSelectedId = anchor;
            } else if (isRegion) {
                m_regionSelectedId = anchor;
            } else if (isCondition) {
                m_conditionSelectedId = anchor;
            }
        }
    } else if (QObject::sender() == m_tree) {
        // 树点击落空（点空白/锚清空）＝工位页选择面清除（表侧成员不动——
        // 表选中面由各自 sender 分支/重建同步承载）。
        m_stationSelectedId.reset();
    }
    refreshLifecycleAndEmptyStates();
    updateTabHeaders();
    if (m_selection.select(anchor)) {
        m_lastSelected = anchor;
        Q_EMIT selectionChanged(
            anchor.has_value() ? QString::fromStdString(anchor.value().toCanonical())
                               : QString());
        // 检查器重投影（从提供器现取——零缓存）。
        if (IRequirementEditor* editor = m_editTarget ? m_editTarget() : nullptr) {
            const RequirementWorkingSet& ws = editor->workingSet();
            renderInspector(ws);
            // 分域检查器联动（UI-T37 返工③——修复"新建区域后树/列表点击
            // 属性不联动"：区域/工况检查器分别由区域表/工况表选中驱动，
            // 此前树或表点击区域/工况只走工位轨＝属性面不更新。旧插件
            // currentRowChanged→refreshXxxInspector 同款；表行同步后按
            // 种类重投影对应域）。
            if (anchor.has_value()) {
                bool isRegion = false;
                bool isCondition = false;
                for (const WorkRegion& r : ws.regions.entries) {
                    if (r.objectId == anchor.value()) { isRegion = true; break; }
                }
                for (const OperatingCondition& c : ws.conditions.entries) {
                    if (c.objectId == anchor.value()) { isCondition = true; break; }
                }
                if (isRegion) {
                    syncRegionTableSelection(anchor.value());
                    renderRegionPage(ws);
                } else if (isCondition) {
                    syncConditionTableSelection(anchor.value());
                    renderConditionPage(ws);
                }
            }
        }
    }
}

void RequirementsPanelWidget::syncRegionTableSelection(const core::ObjectId& id)
{
    // 表行同步（信号屏蔽——程序性选中非用户点击，防 L-R1 重入）。
    for (int i = 0; i < m_regionTable->topLevelItemCount(); ++i) {
        if (nodeAnchor(m_regionTable->topLevelItem(i)) == id) {
            m_regionTable->blockSignals(true);
            m_regionTable->setCurrentItem(m_regionTable->topLevelItem(i));
            m_regionTable->blockSignals(false);
            return;
        }
    }
}

void RequirementsPanelWidget::syncConditionTableSelection(const core::ObjectId& id)
{
    for (int i = 0; i < m_conditionTable->topLevelItemCount(); ++i) {
        if (nodeAnchor(m_conditionTable->topLevelItem(i)) == id) {
            m_conditionTable->blockSignals(true);
            m_conditionTable->setCurrentItem(m_conditionTable->topLevelItem(i));
            m_conditionTable->blockSignals(false);
            return;
        }
    }
}

void RequirementsPanelWidget::onInspectorEditingFinished()
{
    // L-R2 入口：控件提交→submitStationValue 统一轨（UI-T37 R1 委托形——
    // 显键/显文本参数供 SpinBox 等非 QLineEdit 控件复用同一提交语义）。
    m_threadGuard.assertOnUiThread();
    // 定位被编辑行（sender 的 irdFieldKey 属性——提交时的回填参数）。
    auto* edit = qobject_cast<QLineEdit*>(QObject::sender());
    if (edit == nullptr) {
        return;
    }
    if (edit->text() == edit->property("irdAuthoritativeValue").toString()) {
        return;  // 焦点切换/刷新回调未改变值，不进入编辑流也不增加脏标记。
    }
    submitStationValue(edit->property("irdFieldKey").toString().toStdString(),
                       edit->text());
}

void RequirementsPanelWidget::onRegionFieldEditingFinished()
{
    // B2（UI-T31）区域轨——UI-T37 R1 起委托 submitRegionValue 统一轨
    // （显键/显文本形供滑块双联等伴生控件复用同一提交语义）。
    m_threadGuard.assertOnUiThread();
    auto* edit = qobject_cast<QLineEdit*>(QObject::sender());
    if (edit == nullptr) {
        return;
    }
    if (edit->text() == edit->property("irdAuthoritativeValue").toString()) {
        return;  // 未修改短路（幻影脏化消除——T27 语义跨域一致）
    }
    submitRegionValue(edit->property("irdFieldKey").toString().toStdString(),
                      edit->text());
}

void RequirementsPanelWidget::submitRegionValue(const std::string& key,
                                                const QString& text)
{
    // 队列化出信号处理器（同 submitStationValue——提交链同步删发射控件＝UB）。
    QMetaObject::invokeMethod(
        this, [this, key, text] { submitRegionValueNow(key, text); },
        Qt::QueuedConnection);
}

void RequirementsPanelWidget::submitRegionValueNow(const std::string& key,
                                                   const QString& text)
{
    // B2（UI-T31）区域轨统一提交：权威值短路（控件侧）→词表 spec 匹配→
    // parseFieldValueText（唯一解析出口，SA-12）→applyRegionEditSet→
    // submitEntryEdit 域裁决（接受/拒绝分流见 sink；四态语义与工位一致）。
    m_threadGuard.assertOnUiThread();
    IRequirementEditor* editor = m_editTarget ? m_editTarget() : nullptr;
    if (editor == nullptr || !m_writable) {
        return;  // 无会话/只读——不虚构可编辑
    }
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
                parseFieldValueText(text.toStdString(), spec);
            if (!parsed.ok) {
                // 拒绝·解析面：就地错误＋回退权威值（重投影）。
                showStatusLine(QString::fromStdString(parsed.reason));
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

void RequirementsPanelWidget::submitRegionToggle(const std::string& key, bool on)
{
    // 队列化出信号处理器（同 submitStationValue——UB 排除）。
    QMetaObject::invokeMethod(
        this, [this, key, on] { submitRegionToggleNow(key, on); },
        Qt::QueuedConnection);
}

void RequirementsPanelWidget::submitRegionToggleNow(const std::string& key, bool on)
{
    // 区域二态提交轨（启用开关——applyRegionToggleEdit 布尔回填＋域裁决）。
    m_threadGuard.assertOnUiThread();
    IRequirementEditor* editor = m_editTarget ? m_editTarget() : nullptr;
    if (editor == nullptr || !m_writable) {
        return;
    }
    const RequirementWorkingSet& ws = editor->workingSet();
    for (const WorkRegion& region : ws.regions.entries) {
        std::optional<core::ObjectId> selected;
        if (m_regionTable->currentItem() != nullptr) {
            selected = nodeAnchor(m_regionTable->currentItem());
        }
        if (!selected.has_value() || region.objectId != selected.value()) {
            continue;
        }
        std::vector<std::string> known;
        const WorkRegion candidate = applyRegionToggleEdit(region, key, on, known);
        const EditSubmitOutcome outcome =
            submitEntryEdit(*editor, *this, RequirementEdit{candidate});
        if (outcome == EditSubmitOutcome::Applied) {
            m_undoTracker.recordAppliedEdit();
        }
        return;
    }
}

void RequirementsPanelWidget::submitRegionCounts(
    const std::array<std::uint32_t, 3>& counts)
{
    // 队列化出信号处理器（同 submitStationValue——UB 排除）。
    QMetaObject::invokeMethod(
        this, [this, counts] { submitRegionCountsNow(counts); },
        Qt::QueuedConnection);
}

void RequirementsPanelWidget::submitRegionCountsNow(
    const std::array<std::uint32_t, 3>& counts)
{
    // 采样计数提交轨（UI-T37 R1——applyRegionSamplingCountsEdit 回填；计数
    // 合法域归域校验链——拒绝面与行编辑同链，就地错误呈现一致）。
    m_threadGuard.assertOnUiThread();
    IRequirementEditor* editor = m_editTarget ? m_editTarget() : nullptr;
    if (editor == nullptr || !m_writable) {
        return;
    }
    const RequirementWorkingSet& ws = editor->workingSet();
    for (const WorkRegion& region : ws.regions.entries) {
        std::optional<core::ObjectId> selected;
        if (m_regionTable->currentItem() != nullptr) {
            selected = nodeAnchor(m_regionTable->currentItem());
        }
        if (!selected.has_value() || region.objectId != selected.value()) {
            continue;
        }
        std::vector<std::string> known;
        const WorkRegion candidate =
            applyRegionSamplingCountsEdit(region, counts, known);
        const EditSubmitOutcome outcome =
            submitEntryEdit(*editor, *this, RequirementEdit{candidate});
        if (outcome == EditSubmitOutcome::Applied) {
            m_undoTracker.recordAppliedEdit();
        }
        return;
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
                showStatusLine(QString::fromStdString(parsed.reason));
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
    // UI-T39（审核 P1）：撤销后与普通编辑共用同一条"重估校验"链——先
    // 现取工作集重投影四页，再经编辑后动作组合子重估就绪并以最新报告
    // refreshPanel（不再以空报告直刷校验页——空报告会把校验页打回
    // 『尚未执行』态，撤销前的校验结论失真）。
    m_threadGuard.assertOnUiThread();
    IRequirementEditor* editor = m_editTarget ? m_editTarget() : nullptr;
    if (editor == nullptr || !m_writable) {
        return;
    }
    if (m_undoTracker.undo(*editor)) {
        showStatusLine("已撤销一次编辑（草稿级，零修订）");
        if (IRequirementEditor* e2 = m_editTarget()) {
            const RequirementWorkingSet& ws = e2->workingSet();
            renderTree(ws);
            renderInspector(ws);
            renderRegionPage(ws);
            renderConditionPage(ws);
        }
        // 就绪重估＋以最新报告全面板刷新（装配层组合子——缺位＝仅四页
        // 重投影的降级形态，校验页保持既有呈现，不虚构空结论）。
        if (m_postEditAction) {
            m_postEditAction();
        }
        // 撤销/重做键随记账同步（栈深变化——重做键点亮/撤销键可能熄灭；
        // 不依赖组合子注入与否——UI-T39）。
        refreshUndoButtons();
    }
}

void RequirementsPanelWidget::onDraftRedo()
{
    // L-R4 草稿级半区：编辑器局部重做（零修订）＋记账＋刷新（校验重估
    // 链与 onDraftUndo 同构——UI-T39 审核返工，见上）。
    m_threadGuard.assertOnUiThread();
    IRequirementEditor* editor = m_editTarget ? m_editTarget() : nullptr;
    if (editor == nullptr || !m_writable) {
        return;
    }
    if (m_undoTracker.redo(*editor)) {
        showStatusLine("已重做一次编辑（草稿级，零修订）");
        if (IRequirementEditor* e2 = m_editTarget()) {
            const RequirementWorkingSet& ws = e2->workingSet();
            renderTree(ws);
            renderInspector(ws);
            renderRegionPage(ws);
            renderConditionPage(ws);
        }
        if (m_postEditAction) {
            m_postEditAction();
        }
        refreshUndoButtons();  // 记账同步（同 onDraftUndo——UI-T39）
    }
}

void RequirementsPanelWidget::onProjectUndo()
{
    // L-R4 项目级半区：纯转发面（经注入的命令提交出口转发 ui 项目撤销
    // 命令——UndoRedoService 语义归 project，本面板不复制不代理其判定）。
    // UI-T39（审核 P1 修正）：命令 id 此前误写 "edit.undo"——宿主词表中
    // 无此注册项，点击命中"未知命令"拒绝、无任何撤销发生。修正为宿主
    // 注册的 project.undo（WorkbenchContent §7.1 命令表＋UiPlugin 撤销
    // 编排覆写——逆命令提交产生新修订，事件总线同步各域）。
    if (m_commandSubmit) {
        m_commandSubmit("project.undo");
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
    showStatusLine(QString::fromStdString(changeSummary));
    Q_EMIT sessionDirtyChanged(true);
    // UI-T39 最小校验：编辑后动作（装配层在此重估就绪并以新报告
    // refreshPanel——校验页『尚未执行』静态的实时化编排；本面板零判定，
    // 判定权威在域侧 Readiness——P-REQ-6 边界不变）。
    if (m_postEditAction) {
        m_postEditAction();
    }
    // 撤销键随入栈同步（UI-T39——编辑接受后草稿栈深 +1，撤销键立即可用；
    // 不依赖组合子是否驱动 refreshPanel——宿主注入面缺位时按钮态仍诚实）。
    refreshUndoButtons();
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
    showStatusLine(QString("编辑被拒绝（%1）：")
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
    showStatusLine(QString("批次警告（%1）：")
                              .arg(QString::fromStdString(warning.code))
                          + QString::fromStdString(warning.cause));
}

bool RequirementsPanelWidget::executeDomainCommand(const std::string& commandId)
{
    return executeDomainCommand(commandId, nullptr);  // 无缝＝两命令诚实降级原文
}

bool RequirementsPanelWidget::executeDomainCommand(
    const std::string& commandId, const RequirementsView3DSeams* view3d)
{
    m_threadGuard.assertOnUiThread();
    IRequirementEditor* editor = m_editTarget ? m_editTarget() : nullptr;
    if (editor == nullptr) {
        showStatusLine(QStringLiteral("未执行：未打开需求会话（该命令需要项目会话）"));
        return false;
    }
    // flows 装配层（对话框/表单＋域纯函数）；sink＝本面板（L-R2 分流）。
    // view3d＝三维视图缝透传（UI-T33——capture-tcp/pick-feature 解除降级）。
    return executeRequirementCommand(commandId, *this, *editor, *this,
                                    qtDialogHost(), view3d);
}

}  // namespace sdurws::ird::requirements
