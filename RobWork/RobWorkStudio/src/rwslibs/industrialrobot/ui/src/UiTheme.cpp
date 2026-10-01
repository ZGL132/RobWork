/**
 * @file   UiTheme.cpp
 * @brief  工业风主题基建实现（调色板拼装 QSS＋卡片容器工厂）。
 *
 * 设计依据：契约 UI-T37 acceptance 1；设计规格 §8（QSS 五条）。实现纪律：
 * QSS 全部由 palette 常量拼装——色值单一词表（规格文档/头文件/本实现
 * 三处不得出现第二色值源）。
 */

#include <sdurws/ird/ui/UiTheme.hpp>

#include <QHBoxLayout>
#include <QLabel>

namespace sdurws::ird::ui {

QFrame* createCard(QWidget* parent, const QString& title, const QString& helpText,
                   QVBoxLayout** contentOut)
{
    // 前置：内容布局出口必填——空卡片没有装配意义（调用方错误 fail-fast）。
    if (contentOut == nullptr) {
        return nullptr;
    }
    auto* card = new QFrame(parent);
    card->setObjectName(QStringLiteral("ird_card"));
    auto* lay = new QVBoxLayout(card);
    // 4px 基数网格（规格 §8 第 2 条）：卡内边距 12、条目间距 4。
    lay->setContentsMargins(12, 12, 12, 12);
    lay->setSpacing(4);
    // 标题行：标题（10pt 加粗由 QSS 承载）＋"?" 帮助位（成段说明文字的
    // 唯一收纳处——去噪纪律；无帮助文案时占位隐藏，保持标题行高度稳定）。
    // 无父构造（子布局经 addLayout 收养——带 QWidget 父构造即顶层布局
    // 候选，与卡布局冲突＝"already has a layout"运行告警，首轮 gui 实证）。
    auto* header = new QHBoxLayout();
    header->setContentsMargins(0, 0, 0, 4);
    header->setSpacing(4);
    auto* titleLabel = new QLabel(title, card);
    titleLabel->setObjectName(QStringLiteral("ird_card_title"));
    header->addWidget(titleLabel);
    header->addStretch(1);
    auto* helpLabel = new QLabel(QStringLiteral("?"), card);
    helpLabel->setObjectName(QStringLiteral("ird_card_help"));
    helpLabel->setToolTip(helpText);
    helpLabel->setVisible(!helpText.isEmpty());
    header->addWidget(helpLabel);
    lay->addLayout(header);
    // 内容布局（无父构造——由本卡布局收养，Qt 布局树纪律）。
    auto* content = new QVBoxLayout();
    content->setContentsMargins(0, 0, 0, 0);
    content->setSpacing(4);
    lay->addLayout(content);
    *contentOut = content;
    return card;
}

void applyIndustrialTheme(QWidget* root)
{
    if (root == nullptr) {
        return;  // 无根＝无作用域（调用方错误静默容忍——装配早期可空）
    }
    // QSS 由调色板常量拼装（单一色值源——规格 §8 全部五条的机器可读形）。
    // 分段钮：checkable QPushButton，checked＝主色底白字；首尾圆角经
    // objectName 约定（ird_seg_first/ird_seg_last）合并相邻直角。
    // Switch：QCheckBox#ird_switch 指示器重绘为 36×20 圆角滑轨。
    // Tag：QLabel#ird_tag 的动态属性 tag=must|opt 分色（property 选择器）。
    const QString qss = QStringLiteral(
        "QWidget#ird_card, QFrame#ird_card {"
        "  background: %CARD%;"
        "  border: 1px solid %BORDER%;"
        "  border-radius: 6px;"
        "}"
        "QLabel#ird_card_title { font-size: 10pt; font-weight: 600; color: %TEXT%; }"
        "QLabel#ird_card_help { color: %MUTED%; border: 1px solid %BORDER%;"
        "  border-radius: 7px; min-width: 14px; max-width: 14px; qproperty-alignment: AlignCenter; }"
        "QLabel#ird_tag { border-radius: 9px; padding: 1px 8px; }"
        "QLabel#ird_tag[tag=\"must\"] { background: %WARN%; color: white; }"
        "QLabel#ird_tag[tag=\"opt\"] { background: %BORDER%; color: %TEXT%; }"
        "QFrame#ird_banner { background: %WARN%; border-radius: 4px; }"
        "QFrame#ird_banner QLabel { color: white; }"
        "QLabel#ird_banner_code { color: rgba(255,255,255,180); font-family: Consolas; }"
        "QPushButton#ird_seg { border: 1px solid %BORDER%; background: %CARD%;"
        "  padding: 3px 10px; color: %TEXT%; }"
        "QPushButton#ird_seg:checked { background: %PRIMARY%; color: white;"
        "  border-color: %PRIMARY%; }"
        "QPushButton#ird_seg:hover { border-color: %PRIMARY%; }"
        "QPushButton#ird_seg_first { border-top-left-radius: 4px;"
        "  border-bottom-left-radius: 4px; }"
        "QPushButton#ird_seg_last { border-top-right-radius: 4px;"
        "  border-bottom-right-radius: 4px; }"
        "QCheckBox#ird_switch { spacing: 0; }"
        "QCheckBox#ird_switch::indicator { width: 36px; height: 20px;"
        "  border-radius: 10px; background: %BORDER%; }"
        "QCheckBox#ird_switch::indicator:checked { background: %PRIMARY%; }"
        "QLineEdit:focus, QDoubleSpinBox:focus, QSpinBox:focus {"
        "  border: 1px solid %PRIMARY%; }"
        "QToolTip { background: #37404A; color: white; border: 0;"
        "  border-radius: 4px; padding: 4px; }"
    );
    const QString filled = QString(qss)
                               .replace(QStringLiteral("%CARD%"),
                                        QString::fromLatin1(palette::kCard))
                               .replace(QStringLiteral("%BORDER%"),
                                        QString::fromLatin1(palette::kCardBorder))
                               .replace(QStringLiteral("%PRIMARY%"),
                                        QString::fromLatin1(palette::kPrimary))
                               .replace(QStringLiteral("%WARN%"),
                                        QString::fromLatin1(palette::kWarning))
                               .replace(QStringLiteral("%TEXT%"),
                                        QString::fromLatin1(palette::kText))
                               .replace(QStringLiteral("%MUTED%"),
                                        QString::fromLatin1(palette::kTextMuted));
    root->setStyleSheet(filled);
}

}  // namespace sdurws::ird::ui
