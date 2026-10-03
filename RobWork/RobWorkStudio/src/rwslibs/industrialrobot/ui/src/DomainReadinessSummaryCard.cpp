/**
 * @file   DomainReadinessSummaryCard.cpp
 * @brief  跨域就绪摘要卡实现——判定词映射＋卡渲染（UI-T44；头注为契约）。
 *
 * 实现纪律：零判定（行值由宿主自各域 readonlyProjections 投影——判定
 * 权威在域侧 checker）；词表唯一出口＝UiText verdict.* 键（kVerdictTable，
 * UX-02 键值分离）；渲染＝控件装配，无业务分支。
 */

#include <sdurws/ird/ui/DomainReadinessSummaryCard.hpp>

#include <QFrame>
#include <QLabel>
#include <QVBoxLayout>

#include <sdurws/ird/ui/UiText.hpp>   // resolveText（verdict.* 键解析——词表唯一出口）
#include <sdurws/ird/ui/UiTheme.hpp>  // palette::kTextMuted（卡标题弱化色——五色词表）

namespace sdurws {
namespace ird {
namespace ui {

QString engineeringStatusDisplayName(core::EngineeringStatus status)
{
    // 全枚举 switch（新增枚举值编译期报错——不留默认分支吞新增态）。
    switch (status) {
    case core::EngineeringStatus::Feasible:
        return QString::fromStdString(resolveText("verdict.feasible"));
    case core::EngineeringStatus::EngineeringInfeasible:
        return QString::fromStdString(resolveText("verdict.engineering-infeasible"));
    case core::EngineeringStatus::DataInsufficient:
        return QString::fromStdString(resolveText("verdict.data-insufficient"));
    case core::EngineeringStatus::NotApplicable:
        return QString::fromStdString(resolveText("verdict.not-applicable"));
    }
    // 不可达面（全枚举已覆盖）——防御性回退键名（不虚构判定词）。
    return QStringLiteral("verdict.unknown");
}

DomainReadinessSummaryCard::DomainReadinessSummaryCard(const QString& title,
                                                       QWidget* parent)
    : QWidget(parent)
{
    auto* frame = new QFrame(this);
    frame->setObjectName(QStringLiteral("ird_readiness_summary_card"));
    auto* frameLayout = new QVBoxLayout(frame);
    frameLayout->setContentsMargins(6, 4, 6, 4);
    frameLayout->setSpacing(2);

    auto* header = new QLabel(title, frame);
    header->setStyleSheet(
        QStringLiteral("font-weight: 600; color: %1;")
            .arg(QString::fromLatin1(palette::kTextMuted)));
    frameLayout->addWidget(header);

    m_body = new QLabel(frame);
    m_body->setWordWrap(true);
    m_body->setTextInteractionFlags(Qt::TextSelectableByMouse);  // 摘要可复制（只读）
    frameLayout->addWidget(m_body);

    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->addWidget(frame);
}

void DomainReadinessSummaryCard::setRows(
    const std::vector<DomainReadinessSummaryRow>& rows)
{
    if (rows.empty()) {
        // 诚实空态（不虚构三域齐全——装配缺席/投影空集的合法二态）。
        m_body->setText(QStringLiteral("暂无域就绪投影"));
        return;
    }
    QString text;
    for (const DomainReadinessSummaryRow& row : rows) {
        if (!text.isEmpty()) {
            text += QLatin1Char('\n');
        }
        text += row.domainLabel + QStringLiteral("：") + row.verdictText;
        if (!row.noteText.isEmpty()) {
            text += QStringLiteral("（") + row.noteText + QStringLiteral("）");
        }
    }
    m_body->setText(text);
}

}  // namespace ui
}  // namespace ird
}  // namespace sdurws
