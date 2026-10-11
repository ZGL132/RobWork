/**
 * @file   TrajCurveView.cpp
 * @brief  trajectory 曲线视图的实现翻译单元——折线绘制与空态呈现
 *         （WP-16-T12；零计算红线的呈现面执行体）。
 *
 * 设计依据：TrajCurveView.hpp 文件头（呈现语义与诚实边界）。
 *
 * 零计算红线执行面：本 TU 只做数据重组（范围扫描的极值查找——呈现缩
 * 放）与 QPainter 折线绘制；零统计/重采样/插值符号（契约测试全文词表
 * 扫描钉住——极值查找是手写线性循环，非算法库调用）。
 */

#include "TrajCurveView.hpp"

#include <QColor>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>

#include <cmath>    // std::isfinite（呈现缩放的防御性非有限值跳过）
#include <cstddef>  // std::size_t
#include <limits>

namespace sdurws::ird::trajectory {

namespace {

/// 绘图区内边距（像素——轴/图例留白；呈现参数，非业务阈值）。
constexpr int kMarginPx = 36;

}  // namespace

TrajCurveView::TrajCurveView(QWidget* parent) : QWidget(parent)
{
    // 最小尺寸（布局提示——呈现参数；窗口缩放由 Qt 布局承接）。
    setMinimumSize(240, 160);
}

void TrajCurveView::setSeries(
    const std::vector<TrajectoryCurveSeries>& series, const QString& channelLabel)
{
    // 值拷贝留呈现副本（缝产物在面板侧即弃——本视图持有自己的快照）。
    m_series = series;
    m_channelLabel = channelLabel;
    update();  // 触发重绘（Qt 事件队列——UI 线程）
}

void TrajCurveView::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.fillRect(rect(), palette().window());
    painter.setRenderHint(QPainter::Antialiasing, true);

    // ---- 空态：无数据如实呈现（不虚构曲线——NFR-COR-03）。 ----------
    if (m_series.empty()) {
        painter.drawText(rect(), Qt::AlignCenter,
                         tr("曲线数据未装配（%1）").arg(m_channelLabel));
        return;
    }

    // ---- 第一步：数据范围扫描（手写线性循环——呈现缩放的极值查找；
    //       非统计计算：只读当前副本，零副作用）。全部值 SI（通道词表
    //       注：q rad|m、qd rad/s|m/s、qdd rad/s²|m/s²；时刻 s）。 ------
    double tMin = std::numeric_limits<double>::infinity();
    double tMax = -std::numeric_limits<double>::infinity();
    double vMin = std::numeric_limits<double>::infinity();
    double vMax = -std::numeric_limits<double>::infinity();
    std::size_t pointCount = 0;
    for (const TrajectoryCurveSeries& one : m_series) {
        // 逐点扫描（timesS/values 逐点对齐——投影产物不变量；本循环取
        // min/max，防御性跳过非有限值——payload 校验已在上游 fail-fast，
        // 此处只为呈现鲁棒，不静默造值）。
        const std::size_t n = one.timesS.size() < one.values.size()
                                  ? one.timesS.size()
                                  : one.values.size();
        pointCount += n;
        for (std::size_t i = 0; i < n; ++i) {
            const double t = one.timesS[i];
            const double v = one.values[i];
            if (!std::isfinite(t) || !std::isfinite(v)) { continue; }
            if (t < tMin) { tMin = t; }
            if (t > tMax) { tMax = t; }
            if (v < vMin) { vMin = v; }
            if (v > vMax) { vMax = v; }
        }
    }
    if (pointCount == 0 || !std::isfinite(tMin)) {
        // 全序列无有效点——空态呈现（与无数据同口径）。
        painter.drawText(rect(), Qt::AlignCenter,
                         tr("曲线数据无有效点（%1）").arg(m_channelLabel));
        return;
    }

    // 退化范围（单点/常值曲线）扩为单位半宽，避免除零与像素塌缩——
    // 呈现参数（轴刻度语义不承诺）。
    if (!(tMax > tMin)) { tMax = tMin + 1.0; }
    if (!(vMax > vMin)) { vMax = vMin + 0.5; }

    // ---- 第二步：视口映射（数据坐标→像素坐标——线性呈现缩放）。 ----
    const int plotLeft = kMarginPx;
    const int plotBottom = height() - kMarginPx;
    const int plotWidth = width() - 2 * kMarginPx;
    const int plotHeight = height() - 2 * kMarginPx;
    if (plotWidth <= 0 || plotHeight <= 0) { return; }  // 视口过小——放弃本帧
    const double tSpan = tMax - tMin;
    const double vSpan = vMax - vMin;
    const auto toPixelX = [&](double t) {
        return plotLeft + static_cast<int>((t - tMin) / tSpan * plotWidth);
    };
    const auto toPixelY = [&](double v) {
        return plotBottom - static_cast<int>((v - vMin) / vSpan * plotHeight);
    };

    // ---- 第三步：轴与图例（轴刻度不标数值——呈现骨架；游标读数归
    //       面板读数区，本视图零文本读数职责）。 ----------------------
    painter.setPen(palette().windowText().color());
    painter.drawLine(plotLeft, kMarginPx, plotLeft, plotBottom);           // 纵轴
    painter.drawLine(plotLeft, plotBottom,
                     plotLeft + plotWidth, plotBottom);                    // 横轴
    painter.drawText(rect(), Qt::AlignTop | Qt::AlignHCenter,
                     tr("轨迹曲线（%1）—关节叠加").arg(m_channelLabel));

    // ---- 第四步：逐关节折线（关节序＝曲线序——投影产物升序；首关
    //       节取高亮色、其余取交替色——呈现区分，非语义编码）。 -------
    for (std::size_t s = 0; s < m_series.size(); ++s) {
        const TrajectoryCurveSeries& one = m_series[s];
        const std::size_t n = one.timesS.size() < one.values.size()
                                  ? one.timesS.size()
                                  : one.values.size();
        if (n < 2) { continue; }  // 单点不成线——如实跳过
        // 折线色：首关节高亮蓝、其余灰阶交替（呈现参数）。
        painter.setPen(s == 0 ? QColor(0, 102, 204)
                              : QColor(120 + static_cast<int>(s) * 17 % 100,
                                       120, 120));
        QPainterPath path;
        bool penDown = false;
        for (std::size_t i = 0; i < n; ++i) {
            const double t = one.timesS[i];
            const double v = one.values[i];
            if (!std::isfinite(t) || !std::isfinite(v)) {
                penDown = false;  // 非有限点断笔（不连线穿越非法值）
                continue;
            }
            const QPoint p(toPixelX(t), toPixelY(v));
            if (!penDown) {
                path.moveTo(p);
                penDown = true;
            } else {
                path.lineTo(p);
            }
        }
        painter.drawPath(path);
    }
}

}  // namespace sdurws::ird::trajectory
