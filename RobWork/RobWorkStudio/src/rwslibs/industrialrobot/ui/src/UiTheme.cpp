/**
 * @file   UiTheme.cpp
 * @brief  工业风主题基建实现（调色板拼装 QSS＋卡片容器工厂）。
 *
 * 设计依据：契约 UI-T37 acceptance 1；设计规格 §8（QSS 五条）。实现纪律：
 * QSS 全部由 palette 常量拼装——色值单一词表（规格文档/头文件/本实现
 * 三处不得出现第二色值源）。
 */

#include <sdurws/ird/ui/UiTheme.hpp>

#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>

namespace sdurws::ird::ui {

QGroupBox* createCard(QWidget* parent, const QString& title, const QString& helpText,
                      QVBoxLayout** contentOut)
{
    // 前置：内容布局出口必填——空分组没有装配意义（调用方错误 fail-fast）。
    if (contentOut == nullptr) {
        return nullptr;
    }
    // QGroupBox 原生组框（UI-T37 返工——对齐 engineeringrequirements 旧
    // 插件的分组形态：原生标题＋组框线框；QSS 承载圆角/字体美化）。
    auto* card = new QGroupBox(title, parent);
    card->setObjectName(QStringLiteral("ird_card"));
    card->setFlat(false);
    auto* lay = new QVBoxLayout(card);
    // 4px 基数网格（规格 §8 第 2 条）：组内边距 12、条目间距 4。
    lay->setContentsMargins(12, 8, 12, 12);
    lay->setSpacing(4);
    // 帮助位（"?"）右对齐一行——成段说明文字的唯一收纳处（去噪纪律；
    // 无帮助文案时隐藏）。
    auto* helpRow = new QHBoxLayout();
    helpRow->setContentsMargins(0, 0, 0, 0);
    helpRow->addStretch(1);
    auto* helpLabel = new QLabel(QStringLiteral("?"), card);
    helpLabel->setObjectName(QStringLiteral("ird_card_help"));
    helpLabel->setToolTip(helpText);
    helpLabel->setVisible(!helpText.isEmpty());
    helpRow->addWidget(helpLabel);
    lay->addLayout(helpRow);
    // 内容布局（无父构造——由本组布局收养，Qt 布局树纪律）。
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
    // 分组框：QGroupBox#ird_card 原生标题＋线框（对齐旧插件分组形态）。
    // 分段钮：checkable QPushButton，checked＝主色底白字。
    // Switch：QCheckBox#ird_switch 指示器重绘为 36×20 圆角滑轨。
    // Tag：QLabel#ird_tag 的动态属性 tag=must|opt 分色（property 选择器）。
    const QString qss = QStringLiteral(
        "QGroupBox#ird_card {"
        "  background: %CARD%;"
        "  border: 1px solid %BORDER%;"
        "  border-radius: 6px;"
        "  margin-top: 10px;"
        "  font-weight: 600; font-size: 10pt; color: %TEXT%;"
        "}"
        "QGroupBox#ird_card::title { subcontrol-origin: margin; left: 8px;"
        "  padding: 0 3px; background: %CARD%; }"
        "QLabel#ird_card_help { color: %MUTED%; border: 1px solid %BORDER%;"
        "  border-radius: 7px; min-width: 14px; max-width: 14px; qproperty-alignment: AlignCenter; }"
        "QLabel#ird_tag { border-radius: 9px; padding: 1px 8px; }"
        "QLabel#ird_tag[tag=\"must\"] { background: %WARN%; color: white; }"
        "QLabel#ird_tag[tag=\"opt\"] { background: %BORDER%; color: %TEXT%; }"
        "QLabel#ird_validation_status { font-weight: 600; border: 1px solid %BORDER%;"
        "  border-radius: 4px; padding: 6px 10px; background: %CARD%; }"
        "QLabel#ird_validation_status[state=\"ok\"] { border-color: %SUCCESS%; color: %SUCCESS%; }"
        "QLabel#ird_validation_status[state=\"warn\"] { border-color: %WARN%; color: %WARN%; }"
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
                               .replace(QStringLiteral("%SUCCESS%"),
                                        QString::fromLatin1(palette::kSuccess))
                               .replace(QStringLiteral("%TEXT%"),
                                        QString::fromLatin1(palette::kText))
                               .replace(QStringLiteral("%MUTED%"),
                                        QString::fromLatin1(palette::kTextMuted));
    root->setStyleSheet(filled);
}

}  // namespace sdurws::ird::ui
