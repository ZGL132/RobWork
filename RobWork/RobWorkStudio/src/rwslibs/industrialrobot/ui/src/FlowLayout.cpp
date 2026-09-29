/**
 * @file   FlowLayout.cpp
 * @brief  流式栅格布局实现（契约见 FlowLayout.hpp 文件头）。
 *
 * 实现口径：换行算法为经典流式布局（Qt 案例形态的改造版）——本文件的
 * 增量点在于：①逐行高度取"行内最高项"，行内其余项垂直居中（按钮行混排
 * 高度不齐时对齐观感）；②尺寸协商与落位共用同一段计算（testOnly 拍），
 * 协商结果与实际排布永不偏差；③最小宽＝最宽单项（不是行累计——流式
 * 布局的可缩性正是 P2 钳制源消除的落点，见 clamp-source.md）。
 */

#include <sdurws/ird/ui/FlowLayout.hpp>

#include <algorithm>
#include <list>
#include <vector>

namespace sdurws {
namespace ird {
namespace ui {

namespace {

/// 两个正整数的较大者（布局内局部工具——std::max 在 MSVC 与 Qt 宏冲突
/// 面上用括号包住调用即可，这里直接标准写法）。
int layoutMax(int a, int b)
{
    return (a > b) ? a : b;
}

}  // namespace

FlowLayout::FlowLayout(QWidget* parent, int hSpacing, int vSpacing)
    : QLayout(parent), m_hSpace(hSpacing), m_vSpace(vSpacing)
{
    // 父控件主布局语义（QLayout(QWidget*) 构造已注册——此处无需再 setLayout）。
}

FlowLayout::~FlowLayout()
{
    // 析构自持子项（QLayout 契约：未被父布局接管的项由本布局销毁）。
    while (!m_items.empty()) {
        delete m_items.front();
        m_items.pop_front();
    }
}

void FlowLayout::addItem(QLayoutItem* item)
{
    m_items.push_back(item);
}

int FlowLayout::count() const
{
    return static_cast<int>(m_items.size());
}

QLayoutItem* FlowLayout::itemAt(int index) const
{
    auto it = m_items.begin();
    std::advance(it, index);
    return (it == m_items.end()) ? nullptr : *it;
}

QLayoutItem* FlowLayout::takeAt(int index)
{
    auto it = m_items.begin();
    std::advance(it, index);
    if (it == m_items.end()) {
        return nullptr;
    }
    QLayoutItem* item = *it;
    m_items.erase(it);
    return item;
}

Qt::Orientations FlowLayout::expandingDirections() const
{
    // 不声明横向扩展：容器变宽时多余宽度交还给父布局（按钮行靠左排布
    // ——与原 QHBoxLayout 尾部 addStretch 的视觉语义等价）。
    return Qt::Orientations{};
}

bool FlowLayout::hasHeightForWidth() const
{
    // 宽度决定行数、行数决定高度——高度对宽度敏感（父布局据此自适应）。
    return true;
}

int FlowLayout::heightForWidth(int width) const
{
    // 探测拍：以给定宽度算内容总高（不落位——尺寸协商不得触碰子控件）。
    return doLayout(QRect(0, 0, width, 0), /*testOnly=*/true);
}

QSize FlowLayout::sizeHint() const
{
    // 建议尺寸＝按"父控件当前宽"折算的行高（换行形态下高度即最优信息；
    // 宽度交还父布局分配）。minimumSizeHint 为 0 的控件（如 QLabel 弹性位）
    // 取 sizeHint 计入。
    const int parentWidth =
        parentWidget() != nullptr ? parentWidget()->width() : 0;
    int height = doLayout(QRect(0, 0, parentWidth, 0), /*testOnly=*/true);
    if (height <= 0) {
        height = -1;  // 空布局＝无建议（交 Qt 默认处理）
    }
    return QSize(-1, height);
}

QSize FlowLayout::minimumSize() const
{
    // 最小尺寸＝单项最小宽的最大者（宽方向）×单项最小高的最大者（高方向）
    // ——流式布局可缩性的关键：不累计整行宽度（若累计即退化为 QHBoxLayout
    // 的钳制形态，P2 源 1/源 2 的 2074/1054 px 正是这么来的）。
    QSize minSize(0, 0);
    for (const QLayoutItem* item : m_items) {
        const QWidget* widget = item->widget();
        // 空间占位项（spacer）允许为零尺寸；真实控件取其最小尺寸提示，
        // 并叠加固定最小宽（setMinimumWidth 的调用面——尊重显式约束）。
        const QSize itemMin = item->minimumSize();
        minSize.setWidth(layoutMax(minSize.width(), itemMin.width()));
        minSize.setHeight(layoutMax(minSize.height(), itemMin.height()));
        if (widget == nullptr) {
            continue;
        }
        const QSize hint = widget->minimumSizeHint();
        minSize.setWidth(layoutMax(minSize.width(), hint.width()));
        minSize.setHeight(layoutMax(minSize.height(), hint.height()));
    }
    const QMargins m = contentsMargins();
    minSize += QSize(m.left() + m.right(), m.top() + m.bottom());
    return minSize;
}

void FlowLayout::setGeometry(const QRect& rect)
{
    // 先走基类（contentsRect 计算边距），再按内容区落位（实际拍）。
    QLayout::setGeometry(rect);
    doLayout(contentsRect(), /*testOnly=*/false);
}

int FlowLayout::doLayout(const QRect& rect, bool testOnly) const
{
    if (m_items.empty()) {
        return 0;
    }

    const int hSpace = m_hSpace;
    const int vSpace = m_vSpace;

    // ---- 第 1 遍：换行计算（纯几何判定，不落位）------------------------
    // 逐项判定"当前行剩余宽度是否容纳该项"，越界即封行开新行；同时累计
    // 每行的行高（行内最高项）。分行结果先入 lines 暂存，第二遍统一落位
    // （单遍算法里后到的高项会改变行高、先行项已按旧行高落位——垂直居中
    // 会失真；两遍算法保证行内所有项按最终行高居中）。
    struct Line {
        std::vector<QLayoutItem*> items;  ///< 行内项（非 const——setGeometry
                                          ///< 落位需要可变项；项所有权仍在 m_items）
        int height = 0;  ///< 本行行高＝行内最高项的建议高，单位 px
    };
    std::vector<Line> lines;
    Line* current = &lines.emplace_back();
    int x = rect.x();
    for (QLayoutItem* item : m_items) {
        const QSize want = item->sizeHint();
        // 行宽判定：非首项且新项右缘越界即换行（x 归位由新行隐含——落位
        // 拍逐行重新起算）。
        if (!current->items.empty() && want.width() > rect.right() - x + 1) {
            current = &lines.emplace_back();
            x = rect.x();
        }
        current->items.push_back(item);
        current->height = layoutMax(current->height, want.height());
        x += want.width() + hSpace;
    }

    // ---- 第 2 遍：落位（行内垂直居中；testOnly 拍零触碰）--------------
    // 行高信息在第一遍已定——行内所有项（含先到项）统一按最终行高居中。
    int y = rect.y();
    int contentHeight = 0;
    for (const Line& line : lines) {
        int itemX = rect.x();
        for (QLayoutItem* item : line.items) {
            const QSize want = item->sizeHint();
            if (!testOnly) {
                const int centeredY =
                    y + (line.height > want.height()
                             ? (line.height - want.height()) / 2
                             : 0);
                item->setGeometry(QRect(QPoint(itemX, centeredY), want));
            }
            itemX += want.width() + hSpace;
        }
        y += line.height + vSpace;
        contentHeight = y - vSpace - rect.y();
    }
    // 内容总高＝末行底缘相对区域起点（供 heightForWidth／最小高协商）。
    return contentHeight;
}

}  // namespace ui
}  // namespace ird
}  // namespace sdurws
