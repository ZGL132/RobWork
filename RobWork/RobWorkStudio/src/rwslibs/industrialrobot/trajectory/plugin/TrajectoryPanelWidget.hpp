/**
 * @file   TrajectoryPanelWidget.hpp
 * @brief  trajectory 工作流面板（工作流页＋曲线视图入口合一 Tab）——
 *         §16.1 阶段页面承载（WP-16-T12 验收口径的界面面）。
 *
 * 设计依据：
 *   - units/trajectory.md §16.1（"StageId::TrajectoryDynamics 页面承载
 *     轨迹工作流＋曲线视图入口"——本面板即该页面的域侧载体；"零计算
 *     逻辑：规划、碰撞、时间参数化、复检全部在 worker/计算库；UI 线程
 *     只做投影、插值查表（动画）、命令提交"）、§14.5.1（七命令行——
 *     命令受理零修订）、§14.5.2（投影与会话态——面板呈现事实经会话态
 *     注入；播放＝按时刻插值查表驱动会话姿态）、§16.2（零修订与零计算
 *     红线机器可断言）；
 *   - 先例：dynamics/plugin/DynamicsPanelWidget.hpp（WP-17-T09 面板形态
 *     ——零 Q_OBJECT、connect 外部控件信号到成员函数、缝空态降级）；
 *   - 任务契约 tasks/foundation/WP-16-T12.json acceptance 1/2。
 *
 * Qt 形态：QWidget 派生、纯虚 override——零 Q_OBJECT（无自定义信号/槽；
 * 按钮点击/下拉切换经 Qt5 函数指针 connect 语法接到本类成员函数，接收
 * 者上下文＝this 的 QObject 基类面——零 moc 需求，dynamics 同款先例）。
 * 零 Q_OBJECT 故插件目标保持 AUTOMOC OFF（DTB §5.1 v0.17 行口径）。
 *
 * 交互语义（诚实边界——真实宿主面归宿主装配批）：
 *   - 命令按钮点击＝"受理＋提交"两步：先经 TrajectoryCommandHandler
 *     受理（词表查表——§15.11 零计算适配器，T10 落位），受理记录入会
 *     话缓冲并刷新呈现；再经命令提交出口缝转发宿主命令管线（真实
 *     CommandRegistry 接线归宿主装配批——空缝呈现"出口未装配"）。
 *   - 动画播放/进度＝查表驱动：playbackSource 缝取查表面 → sampleAt(t)
 *     纯插值查表（UI 线程红线内）→ 帧读数更新＋会话姿态驱动缝出帧
 *     （真实 View3D 桥归宿主装配批——空缝零姿态效果，读数照常）。
 *     零修订断言面＝kTrajectorySessionContract（四常量，契约测试钉扎）。
 *
 * 线程约束：仅 UI 线程访问（Qt 控件与会话态纪律——卡 §3.4）。
 */

#ifndef IRD_TRAJECTORY_PLUGIN_TRAJECTORYPANELWIDGET_HPP
#define IRD_TRAJECTORY_PLUGIN_TRAJECTORYPANELWIDGET_HPP

#include <QString>  // channelLabel 返回类型（Qt Core——呈现文本素材）
#include <QWidget>

#include <vector>

#include "TrajPanelTypes.hpp" // TrajPanelServices/TrajModuleSessionState/
                              //   TrajCommandRecord（模块会话面值）＋
                              //   TrajectoryCommandHandler（完整类型——
                              //   Commands.hpp 经本头传递，成员值语义）

class QComboBox;   // 前置声明（曲线页联动控件——cpp 到达完整类型）
class QLabel;      // 前置声明（就绪/帧读数文本）
class QPlainTextEdit; // 前置声明（最近命令清单——只读）
class QPushButton; // 前置声明（命令区/播放按钮）
class QSlider;     // 前置声明（动画进度）

namespace sdurws::ird::trajectory {

class TrajCurveView;       // 前置声明（曲线视图——同目录私有头）
class TrajPanelModule;     // 前置声明（模块引用——缝/会话态归属）

/**
 * @brief 轨迹工作流面板（Tab 两页：工作流页＝就绪投影＋命令区＋动画
 *        播放区＋最近命令；曲线视图页＝通道/关节联动折线）。
 */
class TrajectoryPanelWidget final : public QWidget {
public:
    /**
     * @brief 构造面板并做首次会话刷新（面板创建即呈现会话事实——空缝
     *        全部如实降级，不虚构数据）。
     *
     * @param module [in] 域面板模块（非 owning——缝/会话态真值归属；
     *               存活期由装配层保证覆盖面板存活期）
     * @param parent [in] Qt 父控件（可空——宿主布局接管）
     */
    explicit TrajectoryPanelWidget(TrajPanelModule& module,
                                   QWidget* parent = nullptr);

    ~TrajectoryPanelWidget() override;

    /**
     * @brief 会话刷新（会话事实注入后的面板同步——现取会话态重投影：
     *        就绪文本/命令可用性/最近命令清单；面板不缓存会话副本——
     *        每次刷新现取，防第二真值）。
     */
    void refreshFromSession();

private:
    // ---- 界面组装（构造期一次——UI 线程）。 ----------------------------
    QWidget* buildWorkflowPage();  ///< 工作流页（就绪＋命令＋动画＋清单）
    QWidget* buildCurvesPage();    ///< 曲线视图页（联动控件＋折线视图）

    // ---- 交互处理（Qt 信号→成员函数——零 Q_OBJECT 的函数指针 connect）。
    void onCommandButtonClicked();        ///< 命令按钮：受理＋提交两步
    void onPlayClicked();                 ///< 动画播放：进度位置查表驱动
    void onSeekMoved(int sliderValue);    ///< 进度拖动：查表定位帧读数
    void onCurveJointChanged(int index);  ///< 关节切换：重投影曲线页
    void onCurveChannelChanged(int index);///< 通道切换：重投影曲线页

    // ---- 呈现辅助（查表/读数——零计算红线的 UI 线程合法操作面）。 ----
    /// 按进度值查表并更新帧读数与会话姿态驱动缝（t＝进度比例×总时长；
    /// 缝未装配时读数区呈现未装配态）。
    void seekToSliderValue(int sliderValue);
    /// 重投影曲线页（curvesSource 缝取序列→曲线视图；空缝呈现空态）。
    void refreshCurvesPage();
    /// 通道下拉序号→通道词表值（0/1/2→Position/Velocity/Acceleration；
    /// 越序号回落 Position——呈现侧防御，不抛）。
    static TrajectoryCurveChannel channelFromIndex(int index);
    /// 通道词表值→中文标签（图例/空态文本素材——本域唯一文案面：
    /// 通道词表是界面语义，不属于宿主文案资源键族）。
    static QString channelLabel(TrajectoryCurveChannel channel);

    TrajPanelModule& m_module;   ///< 域面板模块（非 owning——缝/会话态归属）
    TrajectoryCommandHandler m_commandHandler; ///< 命令受理适配器（§15.11——
                                               ///<   无状态查表器，T10 落位）

    // ---- 工作流页控件（构造期建——非 owning，Qt 父子树接管）。 --------
    QLabel* m_readinessLabel = nullptr;    ///< 就绪投影文本（UX-10 七态素材）
    QPlainTextEdit* m_recentList = nullptr; ///< 最近命令清单（只读呈现缓冲）
    QLabel* m_frameReadout = nullptr;      ///< 动画帧读数（t/q/qd 文本行）
    QSlider* m_progressSlider = nullptr;   ///< 动画进度（0..1000 映射总时长）
    std::vector<QPushButton*> m_commandButtons; ///< 七命令按钮（词表序）

    // ---- 曲线页控件。 ------------------------------------------------
    QComboBox* m_jointCombo = nullptr;     ///< 关节选择（投影序列序）
    QComboBox* m_channelCombo = nullptr;   ///< 通道选择（三值词表序）
    TrajCurveView* m_curveView = nullptr;  ///< 曲线视图（自绘折线）
};

}  // namespace sdurws::ird::trajectory

#endif  // IRD_TRAJECTORY_PLUGIN_TRAJECTORYPANELWIDGET_HPP
