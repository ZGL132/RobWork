/**
 * @file   DynCurveChartView.cpp
 * @brief  dynamics 曲线视图的实现翻译单元——Qt 自绘（paintEvent）：
 *         坐标轴＋单通道折线＋游标联动线＋峰值标记（呈现变换零业务
 *         计算；红线边界见头文件登记）。
 *
 * 设计依据：DynCurveChartView.hpp 文件头（数据契约与呈现边界）。
 * 呈现文本：全部中文工程用语＋量纲标签（dynChannelUnit——§4.5 分派），
 * 零哈希/内部 token 直出（UX-02）。
 */

#include "DynCurveChartView.hpp"

#include <QPainter>
#include <QPalette>
#include <QPen>

#include <cmath>
#include <limits>

namespace sdurws::ird::dynamics {

namespace {

/// 绘图区内边距，单位像素（左轴刻度文字与底部时间刻度的留白——
/// 呈现布局参数，非业务阈值）。
constexpr int kMarginPx = 48;

/// 折线/游标/峰值的画笔宽，单位像素（呈现样式常量）。
constexpr qreal kLineJoinWidth = 1.6;

/// 有限数值判别（NaN/±Inf 不参与视口归一化——非有限值不进曲线是
/// 投影器既有的 Ok 行纪律，此处防御一致：呈现侧不因单点非有限崩溃）。
bool isFiniteValue(double v)
{
    return std::isfinite(v);
}

}  // namespace

DynCurveChartView::DynCurveChartView(QWidget* parent, DynChannelId channelId)
    : QWidget(parent)
    , m_channel(channelId)
{
    // 曲线视图最小尺寸（呈现布局参数——工作流页 Tab 内可缩放）。
    setMinimumSize(320, 220);
}

void DynCurveChartView::setCurve(std::uint32_t jointIndex,
                                 DynJointType jointType,
                                 const std::vector<double>& t,
                                 const std::vector<double>& values)
{
    m_jointIndex = jointIndex;
    m_jointType = jointType;
    m_t = t;          // 快照拷贝（缝数据可能被刷新——控件持独立副本）
    m_values = values;
    // 数据刷新后游标/峰值标记的既有时刻仍可能落在新范围——保持原值
    // 由 paintEvent 裁剪呈现（不越界误标），调用方联动流会重设。
    update();  // Qt：请求重绘
}

void DynCurveChartView::setChannel(DynChannelId channelId)
{
    if (m_channel == channelId) {
        return;  // 同通道幂等（避免无谓清空与重绘）
    }
    m_channel = channelId;
    // 通道切换＝数据形态改变（量纲/点列）——清空快照等待父面板刷新
    // （呈现空态，不沿用旧通道曲线冒充新通道——NFR-COR-03 呈现面）。
    m_t.clear();
    m_values.clear();
    m_hasPeak = false;
    update();
}

void DynCurveChartView::setCursorTimeS(double tS)
{
    m_cursorS = tS;
    m_hasCursor = true;
    update();
}

void DynCurveChartView::setPeakMarker(double tPeakS, double value)
{
    m_peakS = tPeakS;
    m_peakValue = value;
    m_hasPeak = true;
    update();
}

void DynCurveChartView::clearPeakMarker()
{
    m_hasPeak = false;
    update();
}

void DynCurveChartView::clearCurve()
{
    m_t.clear();
    m_values.clear();
    m_hasCursor = false;
    m_hasPeak = false;
    update();
}

void DynCurveChartView::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.fillRect(rect(), palette().window());
    painter.setRenderHint(QPainter::Antialiasing, true);

    const int plotLeft = kMarginPx;
    const int plotTop = kMarginPx / 2;
    const int plotRight = width() - kMarginPx / 2;
    const int plotBottom = height() - kMarginPx / 2;
    const qreal plotW = static_cast<qreal>(plotRight - plotLeft);
    const qreal plotH = static_cast<qreal>(plotBottom - plotTop);
    if (plotW <= 1.0 || plotH <= 1.0) {
        return;  // 控件过小（布局过渡态）——无空间即无绘制
    }

    // ---- 标题（关节＋通道＋量纲——工程用语，UX-02）。 ----------------
    const QString title = tr("关节 %1 · %2（%3）")
                              .arg(m_jointIndex)
                              .arg(QString::fromStdString(dynChannelKey(m_channel)))
                              .arg(QString::fromStdString(
                                  dynChannelUnit(m_channel, m_jointType)));
    painter.setPen(palette().color(QPalette::WindowText));
    painter.drawText(QRect(0, 4, width(), plotTop - 6),
                     Qt::AlignHCenter | Qt::AlignVCenter, title);

    // ---- 空态（无快照数据——"无数据"呈现，不伪造曲线）。 -----------
    if (m_t.empty() || m_values.empty()) {
        painter.setPen(palette().color(QPalette::Mid));
        painter.drawText(rect(), Qt::AlignCenter, tr("无数据"));
        return;
    }

    // ---- 视口归一化扫描（呈现视口变换——非业务统计：最小/最大值
    //      仅用于把数据映射进控件像素区，不产出任何统计判定值；单遍
    //      线性扫描，仅取有限值——非有限点跳过不出线）。 --------------
    double tMin = std::numeric_limits<double>::infinity();
    double tMax = -std::numeric_limits<double>::infinity();
    double vMin = std::numeric_limits<double>::infinity();
    double vMax = -std::numeric_limits<double>::infinity();
    bool anyFinite = false;
    const std::size_t count = std::min(m_t.size(), m_values.size());
    for (std::size_t i = 0; i < count; ++i) {
        const double tv = m_t[i];
        const double vv = m_values[i];
        if (!isFiniteValue(tv) || !isFiniteValue(vv)) {
            continue;  // 非有限点不进视口（呈现侧防御——与投影 Ok 行纪律一致）
        }
        anyFinite = true;
        if (tv < tMin) { tMin = tv; }
        if (tv > tMax) { tMax = tv; }
        if (vv < vMin) { vMin = vv; }
        if (vv > vMax) { vMax = vv; }
    }
    if (!anyFinite) {
        painter.setPen(palette().color(QPalette::Mid));
        painter.drawText(rect(), Qt::AlignCenter, tr("无有效数据点"));
        return;
    }
    // 零跨度退化（常值通道——平线）：纵轴补对称半跨，避免除零。
    if (vMax - vMin < 1e-300) {
        vMin -= 0.5;
        vMax += 0.5;
    }

    // ---- 数据→像素映射（线性变换——呈现几何）。 ----------------------
    const auto xOf = [&](double tS) {
        return plotLeft + (tS - tMin) / (tMax - tMin) * plotW;
    };
    const auto yOf = [&](double v) {
        return plotBottom - (v - vMin) / (vMax - vMin) * plotH;
    };

    // 坐标区边框＋端点刻度值（首/末时刻与纵轴上下限——极简刻度面）。
    painter.setPen(palette().color(QPalette::Mid));
    painter.drawRect(QRectF(plotLeft, plotTop, plotW, plotH));
    painter.drawText(QRect(0, plotBottom + 2, plotLeft - 4, 16),
                     Qt::AlignRight, QString::number(tMin, 'g', 4));
    painter.drawText(QRect(plotRight - 40, plotBottom + 2, 60, 16),
                     Qt::AlignLeft, QString::number(tMax, 'g', 4));
    painter.drawText(QRect(0, plotTop - 14, plotLeft - 4, 14),
                     Qt::AlignRight, QString::number(vMax, 'g', 4));
    painter.drawText(QRect(0, plotBottom - 2, plotLeft - 4, 14),
                     Qt::AlignRight, QString::number(vMin, 'g', 4));

    // ---- 折线（有限点连接；非有限点断线——不虚构连续性）。 ----------
    painter.setPen(QPen(palette().highlight().color(), kLineJoinWidth));
    bool penDown = false;
    QPointF prev;
    for (std::size_t i = 0; i < count; ++i) {
        const double tv = m_t[i];
        const double vv = m_values[i];
        if (!isFiniteValue(tv) || !isFiniteValue(vv)) {
            penDown = false;  // 断线（非有限点两侧不连线——呈现不插值）
            continue;
        }
        const QPointF pt(xOf(tv), yOf(vv));
        if (penDown) {
            painter.drawLine(prev, pt);
        }
        prev = pt;
        penDown = true;
    }

    // ---- 峰值标记（locate-peak 跳转落点——标记线＋圆点＋读数）。 ----
    if (m_hasPeak && isFiniteValue(m_peakS) && m_peakS >= tMin
        && m_peakS <= tMax) {
        QPen peakPen(palette().link().color(), kLineJoinWidth, Qt::DashLine);
        painter.setPen(peakPen);
        const qreal px = xOf(m_peakS);
        painter.drawLine(QPointF(px, plotTop), QPointF(px, plotBottom));
        if (isFiniteValue(m_peakValue)) {
            painter.setBrush(palette().link());
            painter.drawEllipse(QPointF(px, yOf(m_peakValue)), 3.5, 3.5);
            painter.setPen(palette().color(QPalette::WindowText));
            painter.drawText(QRect(static_cast<int>(px) - 60, plotTop - 16,
                                   120, 14),
                             Qt::AlignHCenter,
                             QString::number(m_peakValue, 'g', 6));
        }
    }

    // ---- 游标联动线（show-curves 游标联动的呈现落点）。 -------------
    if (m_hasCursor && isFiniteValue(m_cursorS) && m_cursorS >= tMin
        && m_cursorS <= tMax) {
        painter.setPen(QPen(palette().color(QPalette::WindowText), 1.0, Qt::DotLine));
        const qreal cx = xOf(m_cursorS);
        painter.drawLine(QPointF(cx, plotTop), QPointF(cx, plotBottom));
        // 游标时刻读数（底部——工程用语，单位 s）。
        painter.setPen(palette().color(QPalette::WindowText));
        painter.drawText(QRect(static_cast<int>(cx) - 50, plotTop + 2,
                               100, 14),
                         Qt::AlignHCenter,
                         tr("t = %1 s").arg(QString::number(m_cursorS, 'g', 6)));
    }
}

}  // namespace sdurws::ird::dynamics
