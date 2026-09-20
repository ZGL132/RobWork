/**
 * @file   CommandPalette.cpp
 * @brief  命令面板实现（§7.4）——只读投影渲染＋键盘导航＋统一提交路径。
 *
 * 设计依据：
 *   - units/ui.md §7.4（面板投影/模糊搜索消费/键盘导航 ↑↓/Enter/Esc/Tab、
 *     "禁用＋说明"发现性保留、结果上限 50 与"继续输入以缩小范围"、近期
 *     置顶分组）、§7.5/§7.6（可见/使能态呈现）、§10.3（paletteSnapshot/
 *     submit 消费面）；
 *   - 需求 UX-13（命令面板与快捷键互为可达性兜底——未绑定命令经面板
 *     可达）、PM-14（近期使用的持久化由注册表自动计数＋装配层落盘——
 *     面板不触碰存储）、PM-10（NoProject 态项目命令"禁用＋说明"）。
 *
 * 线程模型：仅 UI 线程（§3.4 M-1）。
 */

#include "CommandPalette_p.hpp"

#include <QKeyEvent>
#include <QVBoxLayout>

#include <QString>

#include <algorithm>
#include <map>

namespace sdurws {
namespace ird {
namespace ui {
namespace detail {

namespace {

/// 分类过渡中文名（Tab 补全类别的呈现与提示形态；§7.1 CommandCategory 词表
/// 行序——值随 kTitleTable 同案过渡承载，UI-T09 后随键资源化）。
const char* categoryTransitionalName(CommandCategory category)
{
    switch (category) {
    case CommandCategory::Project: return "项目";
    case CommandCategory::Edit: return "编辑";
    case CommandCategory::View: return "视图";
    case CommandCategory::Stage: return "阶段";
    case CommandCategory::Analysis: return "分析";
    case CommandCategory::Report: return "报告";
    case CommandCategory::Workbench: return "工作台";
    case CommandCategory::Help: return "帮助";
    }
    return "?";
}

}  // namespace

CommandPalettePanel::CommandPalettePanel(ICommandRegistry* commands,
                                         IGlobalShortcutRegistry* shortcuts,
                                         QWidget* parent)
    : QWidget(parent), m_commands(commands), m_shortcuts(shortcuts)
{
    // 浮动面板形态：主窗口的**子控件**（非顶级 Popup——Popup 的抓取语义
    // 在事件泵中会被外部事件关闭，对键盘自动化与程序化提交不确定；§7.4
    // 键盘导航表以 Esc/Enter 收口，不依赖点击外关闭）。objectName 面
    // （测试探针定位——findChild 契约，WorkbenchShellGuiTest 同款；UX-02：
    // objectName 是内部锚不进用户文案）。
    setObjectName(QString::fromLatin1("ird_command_palette"));
    setAutoFillBackground(true);

    // 结构：过滤输入框＋结果列表＋提示行（垂直单列——键盘流主轴）。
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    m_filter = new QLineEdit(this);
    m_filter->setObjectName(QString::fromLatin1("ird_command_palette_filter"));
    m_filter->setPlaceholderText(QString::fromUtf8("输入以搜索命令…"));
    m_filter->setClearButtonEnabled(true);
    // 过滤框文本变化→重取快照（textChanged 直连——无 Q_OBJECT，lambda 接线）。
    QObject::connect(m_filter, &QLineEdit::textChanged, this, [this] { repopulate(); });
    // 输入框键盘事件导向面板主路径（↑↓/Enter——eventFilter 转发）。
    m_filter->installEventFilter(this);
    layout->addWidget(m_filter);

    m_list = new QListWidget(this);
    m_list->setObjectName(QString::fromLatin1("ird_command_palette_list"));
    m_list->setUniformItemSizes(true);
    QObject::connect(m_list, &QListWidget::itemActivated, this, [this] { executeCurrent(); });
    layout->addWidget(m_list, 1);

    m_hint = new QLabel(this);
    m_hint->setWordWrap(true);
    layout->addWidget(m_hint);

    resize(520, 360);  // 面板推荐尺寸（§4.4 最小可用布局以上——不作窗口持久化）

    // 子面板默认隐藏（Qt 子控件随父窗口显示而显示——面板只能经 open()
    // 呈现，不随壳启动出现）。
    setVisible(false);
}

void CommandPalettePanel::open()
{
    // 打开即构建快照（§7.4"打开时构建快照"）＋过滤态复位＋顶部居中就位
    // （子面板无窗口管理器参与——坐标相对主窗口客户区）。
    m_categoryFilter.reset();
    m_filter->clear();
    repopulate();
    if (parentWidget() != nullptr) {
        const int x = std::max(0, (parentWidget()->width() - width()) / 2);
        move(x, 60);  // 顶部工具栏下方浮现（§4.1 顶栏行的视觉锚位）
    }
    show();
    raise();
    m_filter->setFocus();
}

void CommandPalettePanel::closePalette()
{
    m_categoryFilter.reset();
    hide();
}

bool CommandPalettePanel::isOpen() const
{
    return isVisible();
}

void CommandPalettePanel::repopulate()
{
    m_list->clear();

    const std::string fuzzy = m_filter->text().toStdString();
    // 快照＝注册表唯一口径（§7.4"面板是 CommandRegistry 的只读投影"——
    // 匹配/加权/稳定排序/近期置顶全部在 paletteSnapshot，本面板零复制）。
    const std::vector<CommandView> rows = m_commands->paletteSnapshot(fuzzy, kPaletteLimit);
    // 分类过滤（Tab 补全态）为面板本地呈现过滤——不回写注册表快照。
    std::vector<CommandView> visible;
    for (const CommandView& row : rows) {
        if (m_categoryFilter.has_value() && row.category != *m_categoryFilter) {
            continue;
        }
        visible.push_back(row);
    }

    // 近期置顶分组条（§7.4"最近使用置顶分组"——仅在存在近期命令时渲染）。
    const std::vector<CommandId> recents = m_commands->recentUsed();
    const bool hasRecent = std::any_of(visible.begin(), visible.end(),
                                       [&recents](const CommandView& v) {
                                           return std::find(recents.begin(), recents.end(), v.id)
                                               != recents.end();
                                       });
    if (hasRecent) {
        auto* group = new QListWidgetItem(QString::fromUtf8("—— 最近使用 ——"), m_list);
        group->setFlags(Qt::NoItemFlags);  // 分组条不可选（纯视觉分隔）
    }

    // 键位显示映射（快捷键表可空＝无键显示——面板可达性兜底场景）。
    std::map<std::string, std::string> keyTextByCommand;
    if (m_shortcuts != nullptr) {
        for (const HotkeyBinding& binding : m_shortcuts->bindings()) {
            keyTextByCommand.emplace(binding.command,
                                     binding.key.toString(QKeySequence::NativeText).toStdString());
        }
    }

    for (const CommandView& row : visible) {
        QString keyText;
        const auto it = keyTextByCommand.find(row.id);
        if (it != keyTextByCommand.end()) {
            keyText = QString::fromStdString(it->second);
        }
        auto* item = new QListWidgetItem(rowText(row, keyText), m_list);
        item->setData(Qt::UserRole, QString::fromStdString(row.id));
        // 未近期化的近期高亮：近期命令行加粗由工具提示承担（简单稳定）。
        if (std::find(recents.begin(), recents.end(), row.id) != recents.end()) {
            item->setToolTip(QString::fromUtf8("最近使用"));
        }
        if (!row.enabled) {
            // "禁用＋说明"（§7.4/PM-10——保留发现性，不给禁用理由的灰行）。
            item->setForeground(Qt::gray);
        }
    }

    if (m_list->count() > 0 && m_list->item(0)->flags() != Qt::NoItemFlags) {
        m_list->setCurrentRow(0);  // 首行预选（Enter 即执行——零额外击键）
    } else if (m_list->count() > 1) {
        m_list->setCurrentRow(1);  // 分组条占首行时跳过
    }

    // 提示行：上限截断提示（§7.4"继续输入以缩小范围"）＋分类过滤态。
    QString hint;
    if (rows.size() >= kPaletteLimit) {
        hint = QString::fromUtf8("结果较多，继续输入以缩小范围");
    }
    if (m_categoryFilter.has_value()) {
        hint += hint.isEmpty() ? QString() : QString::fromUtf8("；");
        hint += QString::fromUtf8("分类：")
                + QString::fromUtf8(categoryTransitionalName(*m_categoryFilter))
                + QString::fromUtf8("（Tab 取消）");
    }
    m_hint->setText(hint);
}

void CommandPalettePanel::executeCurrent()
{
    QListWidgetItem* item = m_list->currentItem();
    if (item == nullptr || item->flags() == Qt::NoItemFlags) {
        return;  // 空表/分组条——无命令可执行（防御，不虚构选择）
    }
    const CommandId id = item->data(Qt::UserRole).toString().toStdString();
    // 统一提交路径（§10.4 副作用行——面板与菜单/快捷键共用，无旁路）；
    // 拒绝（不可执行/只读）由注册表出 UI-CMD-NOT-EXECUTABLE 诊断，面板收起
    // 后由壳状态栏/诊断目录呈现（三条通道不混用——§7.7 结果回 UI 口径）。
    (void)m_commands->submit(id);
    closePalette();
}

void CommandPalettePanel::completeCategory()
{
    if (m_categoryFilter.has_value()) {
        m_categoryFilter.reset();  // 二次 Tab＝取消分类过滤（对称往返）
    } else {
        QListWidgetItem* item = m_list->currentItem();
        if (item == nullptr || item->flags() == Qt::NoItemFlags) {
            return;  // 无选中命令——无类别可补全
        }
        // 以选中命令的分类收窄（§7.4"Tab 补全类别"）——按 id 查回投影行。
        const CommandId id = item->data(Qt::UserRole).toString().toStdString();
        for (const CommandView& view : m_commands->query({})) {
            if (view.id == id) {
                m_categoryFilter = view.category;
                break;
            }
        }
    }
    repopulate();
}

QString CommandPalettePanel::rowText(const CommandView& view, const QString& keyText)
{
    // 行文本＝标题（过渡中文，回退 id）＋分组路径＋键位＋禁用说明。
    QString title = QString::fromStdString(view.title);
    if (title.isEmpty()) {
        title = QString::fromStdString(view.id);  // 无过渡文案回退 id（不虚构标题）
    }
    QString text = title;
    if (!view.menuPath.empty()) {
        text += QString::fromUtf8("　〔") + QString::fromStdString(view.menuPath)
                + QString::fromUtf8("〕");
    }
    if (!keyText.isEmpty()) {
        text += QString::fromUtf8("　") + keyText;
    }
    if (!view.enabled) {
        // 禁用说明行内呈现（§7.4"禁用＋说明"——键为文案键，值解析归
        // UiText/UI-T09；此处以稳定键回退显示，保证说明不缺席）。
        const QString reason = QString::fromStdString(view.disableReasonKey);
        text += QString::fromUtf8("　（不可用")
                + (reason.isEmpty() ? QString() : QString::fromUtf8("：") + reason)
                + QString::fromUtf8("）");
    }
    return text;
}

void CommandPalettePanel::keyPressEvent(QKeyEvent* event)
{
    // 面板级键盘主路径（§7.4 全表——全程无鼠标可达）。
    switch (event->key()) {
    case Qt::Key_Escape:
        closePalette();  // Esc 关闭
        event->accept();
        return;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        executeCurrent();  // Enter 执行
        event->accept();
        return;
    case Qt::Key_Tab:
        completeCategory();  // Tab 补全类别（§7.4）
        event->accept();
        return;
    default:
        QWidget::keyPressEvent(event);
        return;
    }
}

bool CommandPalettePanel::eventFilter(QObject* watched, QEvent* event)
{
    // 过滤框的导航键转发：↑↓ 移动选择、Enter 执行——输入不离手完成导航
    // （UX-13）；其余事件按默认链路（输入/粘贴/清空照常）。
    if (watched == m_filter && event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        switch (keyEvent->key()) {
        case Qt::Key_Down:
            if (m_list->count() > 0) {
                m_list->setCurrentRow(std::min(m_list->currentRow() + 1, m_list->count() - 1));
            }
            return true;  // 事件已消费（不过滤框文本无干扰）
        case Qt::Key_Up:
            if (m_list->count() > 0) {
                m_list->setCurrentRow(std::max(m_list->currentRow() - 1, 0));
            }
            return true;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            executeCurrent();
            return true;
        case Qt::Key_Escape:
            closePalette();
            return true;
        default:
            break;
        }
    }
    return QWidget::eventFilter(watched, event);
}

CommandPalettePanel* createCommandPalette(ICommandRegistry* commands,
                                          IGlobalShortcutRegistry* shortcuts,
                                          QWidget* parent)
{
    return new CommandPalettePanel(commands, shortcuts, parent);
}

}  // namespace detail
}  // namespace ui
}  // namespace ird
}  // namespace sdurws
