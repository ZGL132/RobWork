/**
 * @file   TrajCurveView.hpp
 * @brief  trajectory 曲线视图（q/qd/qdd 只读投影的呈现面）——§14.5.1
 *         show-curve 命令的曲线入口页承载（WP-16-T12）。
 *
 * 设计依据：
 *   - units/trajectory.md §14.5.1（show-curve 行——"曲线视图（q/qd/qdd/
 *     时刻曲线；只读投影）……UI 线程（读归档 payload 投影）"）、§16.1
 *     （曲线视图入口随工作流页承载——T12 验收口径）、§16.2（零计算红线
 *     ——本视图只做数据重组与折线绘制，零统计/重采样/插值计算）；
 *   - 先例：dynamics/plugin/DynCurveChartView.hpp（WP-17-T09 曲线视图
 *     形态——QWidget 派生自绘、零 Q_OBJECT、paintEvent 折线）；
 *   - 任务契约 tasks/foundation/WP-16-T12.json acceptance 1。
 *
 * Qt 形态：QWidget 派生、纯虚 override——零 Q_OBJECT（无自定义信号/槽；
 * 与外部控件的联动由面板经公开方法驱动，dynamics 同款）。零 Q_OBJECT
 * 故插件目标保持 AUTOMOC OFF（DTB §5.1 v0.17 行口径）。
 *
 * 线程约束：仅 UI 线程访问（Qt 控件纪律——卡 §3.4）。
 */

#ifndef IRD_TRAJECTORY_PLUGIN_TRAJCURVEVIEW_HPP
#define IRD_TRAJECTORY_PLUGIN_TRAJCURVEVIEW_HPP

#include <QWidget>

#include <vector>

#include <sdurws/ird/trajectory/Playback.hpp> // TrajectoryCurveSeries（只读
                                               //   投影产物——T10 数据面契约）

class QPainter;  // 前置声明（paintEvent 入参完整类型在 Qt 头——cpp 到达）

namespace sdurws::ird::trajectory {

/**
 * @brief 曲线视图（逐关节折线呈现——横轴时刻、纵轴通道值；多关节叠加）。
 *
 * 呈现语义（诚实边界）：本视图只消费曲线投影序列（TrajectoryCurveSeries
 * ——归档样本表的逐关节视图，零重采样），绘制时把数据范围线性映射到
 * 视口像素（呈现缩放，非数据变换）；空序列呈现"无数据"空态（不虚构
 * 曲线——NFR-COR-03）。
 */
class TrajCurveView final : public QWidget {
public:
    /**
     * @brief 构造曲线视图（默认空数据空态；parent 归 Qt 父子树）。
     *
     * @param parent [in] Qt 父控件（可空——宿主布局接管）
     */
    explicit TrajCurveView(QWidget* parent = nullptr);

    /**
     * @brief 更新曲线数据（面板联动入口——关节/通道切换时调用）。
     *
     * @param series [in] 曲线序列（只读投影产物——值拷贝留呈现副本；
     *               空向量＝无数据空态）
     * @param channelLabel [in] 通道标签（如 "qd"；空态/图例文本的素材
     *               ——由面板按通道词表填中文标签，本视图不携带文案值）
     */
    void setSeries(const std::vector<TrajectoryCurveSeries>& series,
                   const QString& channelLabel);

protected:
    /**
     * @brief 绘制事件（折线＋轴＋空态文本——UI 线程）。
     *
     * @param event [in] Qt 绘制事件（本视图不消费细节字段）
     */
    void paintEvent(QPaintEvent* event) override;

private:
    std::vector<TrajectoryCurveSeries> m_series; ///< 呈现副本（最近一次
                                                  ///<   setSeries 的值拷贝）
    QString m_channelLabel;                      ///< 通道标签（图例/空态文本素材）
};

}  // namespace sdurws::ird::trajectory

#endif  // IRD_TRAJECTORY_PLUGIN_TRAJCURVEVIEW_HPP
