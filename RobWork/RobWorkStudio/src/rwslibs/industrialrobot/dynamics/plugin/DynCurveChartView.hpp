/**
 * @file   DynCurveChartView.hpp
 * @brief  dynamics 曲线视图（自绘单通道折线＋游标联动＋峰值标记）——
 *         §9.5 dynamics.show-curves/locate-peak 的呈现面控件（零业务
 *         计算：仅数据→像素的呈现变换；WP-17-T09）。
 *
 * 设计依据：
 *   - units/dynamics.md §9.5（"各关节曲线联动（q/q̇/q̈/τ/P 逐关节多曲线
 *     ＋游标联动）——读归档 payload 投影／UI 线程（零计算）"）；
 *   - 先例：仓库呈现面"控件零业务判定"纪律（kinematics PluginPanel
 *     同款——widget 只呈现，判定在模型层/域设施）；
 *   - 任务契约 tasks/foundation/WP-17-T09.json acceptance 1（曲线视图）。
 *
 * ★ 零计算红线的呈现边界（诚实登记）：本控件的数值运算只有两类——
 *   ①视口归一化扫描（当前通道数据的最小/最大值，供折线映射到控件
 *   像素区）；②时间→横轴像素的线性映射。二者皆为**呈现视口变换**，
 *   不产出任何统计值供业务判定（峰值/RMS/包络统计唯一在计算库并经
 *   归档——卡 §9.5"UI 线程不得执行动力学计算"红线；本控件零评估/
 *   统计符号，契约测试全文词表扫描钉住）。
 *
 * 线程约束：仅 UI 线程访问（QWidget 常规约束）。
 * Qt 形态：QWidget 派生、纯虚 override（paintEvent/sizeHint）——零
 *   Q_OBJECT（无信号槽/动态属性需求；命令编排全部经模型层与缝——
 *   命令注册权威归宿主 CommandRegistry，控件不私占）。AUTOMOC 保持
 *   OFF（单元 CMake 落位注）。
 */

#ifndef IRD_DYNAMICS_PLUGIN_DYNCURVECHARTVIEW_HPP
#define IRD_DYNAMICS_PLUGIN_DYNCURVECHARTVIEW_HPP

#include <cstdint>
#include <vector>

#include <QWidget>

#include "DynPanelModel.hpp"  // DynChannelId/dynChannelUnit（通道词表——
                              //   同单元插件私有头）

namespace sdurws::ird::dynamics {

/**
 * @brief 单通道曲线视图（时间轴折线——呈现面）。
 *
 * 数据契约：setCurve 快照拷贝时间轴与所选通道数值列（两列逐下标对位
 * ——JointCurves 契约；调用方先选通道再给数据亦可，重设通道即清空
 * 数据等待下次刷新——呈现空态，不伪造曲线）。
 */
class DynCurveChartView final : public QWidget {
public:
    /**
     * @brief 构造曲线视图（默认空数据空态；parent 归 Qt 父子树）。
     *
     * @param parent [in] Qt 父控件（可空——宿主布局接管）
     * @param channelId [in] 初始通道（默认位置通道——下拉联动的初值）
     */
    explicit DynCurveChartView(QWidget* parent = nullptr,
                               DynChannelId channelId = DynChannelId::Position);

    /**
     * @brief 更新曲线快照（时间轴＋数值列拷贝——[in] 只读）。
     *
     * @param jointIndex [in] 关节序号（标题呈现素材）
     * @param jointType  [in] 关节型（量纲标签判型——标题单位随型分派）
     * @param t          [in] 时间轴，单位 s（与 values 逐下标对位）
     * @param values     [in] 所选通道数值列（量纲由通道＋关节型决定）
     */
    void setCurve(std::uint32_t jointIndex, DynJointType jointType,
                  const std::vector<double>& t,
                  const std::vector<double>& values);

    /// @brief 切换显示通道（清空当前快照等待刷新——空态不伪造）。
    void setChannel(DynChannelId channelId);

    /// @brief 当前显示通道。
    DynChannelId channel() const noexcept { return m_channel; }

    /**
     * @brief 设置游标时刻（联动线横坐标），单位 s。
     *
     * @param tS [in] 游标时刻（越出数据范围时游标不出现在绘制区——
     *           呈现裁剪，零钳制语义：游标值本身原样保留）
     */
    void setCursorTimeS(double tS);

    /// @brief 游标时刻，单位 s（越界语义见 setCursorTimeS——原样返回）。
    double cursorTimeS() const noexcept { return m_cursorS; }

    /// @brief 游标是否有效（setCurve 后默认 false——未联动态）。
    bool hasCursor() const noexcept { return m_hasCursor; }

    /**
     * @brief 设置峰值标记（locate-peak 跳转的呈现落点——标记线＋圆点）。
     *
     * @param tPeakS [in] 峰值时刻，单位 s
     * @param value  [in] 峰值数值（量纲按通道＋关节型——读数标注素材）
     */
    void setPeakMarker(double tPeakS, double value);

    /// @brief 清除峰值标记（切换关节/通道时父面板调用）。
    void clearPeakMarker();

    /// @brief 清空全部数据与标记（空态呈现——"无数据"文案）。
    void clearCurve();

protected:
    /// @brief 自绘折线/轴/游标/峰值标记（零业务判定——呈现变换）。
    void paintEvent(QPaintEvent* event) override;

private:
    DynChannelId m_channel;          ///< 当前显示通道
    std::uint32_t m_jointIndex = 0;  ///< 当前关节序号（标题素材）
    DynJointType m_jointType{};      ///< 当前关节型（量纲标签判型）
    std::vector<double> m_t;         ///< 时间轴快照，单位 s（与 m_values 对位）
    std::vector<double> m_values;    ///< 数值列快照（量纲按通道＋关节型）
    double m_cursorS = 0.0;          ///< 游标时刻，单位 s
    bool m_hasCursor = false;        ///< 游标有效位（联动联动状态）
    bool m_hasPeak = false;          ///< 峰值标记有效位
    double m_peakS = 0.0;            ///< 峰值标记时刻，单位 s
    double m_peakValue = 0.0;        ///< 峰值标记数值（读数标注素材）
};

}  // namespace sdurws::ird::dynamics

#endif  // IRD_DYNAMICS_PLUGIN_DYNCURVECHARTVIEW_HPP
