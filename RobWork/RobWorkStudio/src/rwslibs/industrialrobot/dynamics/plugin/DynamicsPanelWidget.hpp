/**
 * @file   DynamicsPanelWidget.hpp
 * @brief  dynamics 工作流页＋曲线视图主面板（两页合一 Tab 容器）——
 *         §9.5 UI 协作五命令的呈现面（零业务计算：数据消费全经模型层
 *         与服务缝；WP-17-T09）。
 *
 * 设计依据：
 *   - units/dynamics.md §9.5（五命令行：analyze＝提交即返／show-curves
 *     ＝曲线联动读归档投影／locate-peak＝游标跳转＋姿态同步素材／
 *     replay-at＝按 t 驱动会话姿态（零修订四常量）／export-curve-data
 *     ＝导出 token 承载执行归宿主通道；投影行七态素材）；
 *   - units/dynamics.md §10.7（命令适配器受理语义——面板先经模型层
 *     L-D3 受理，受理成功再经提交缝转宿主 CommandRegistry——O13：
 *     命令权威归 ui，面板零注册零私占）；
 *   - 先例：kinematics/plugin/KinematicsPanelWidget.hpp（域面板零业务
 *     判定纪律——WP-15-T12；dynamics 按其形态收缩为两页）；
 *   - 需求 UX-02（工程用语、零哈希/内部插件名——文案全经键解析）、
 *     UX-10（七态素材呈现：未完成附缺项、计算中、数据不足标注、
 *     失败/拒绝如实呈现）；DYN-08（曲线联动/峰值定位/时刻回放）；
 *   - 任务契约 tasks/foundation/WP-17-T09.json acceptance 1/2。
 *
 * 线程约束：仅 UI 线程访问（缝调用与会话态同线程——DynPanelTypes.hpp）。
 * Qt 形态：QWidget 派生、纯虚 override——零 Q_OBJECT（无信号槽需求；
 *   按钮点击经 lambda 连接；AUTOMOC 保持 OFF——单元 CMake 落位注）。
 */

#ifndef IRD_DYNAMICS_PLUGIN_DYNAMICSPANELWIDGET_HPP
#define IRD_DYNAMICS_PLUGIN_DYNAMICSPANELWIDGET_HPP

#include <QWidget>

#include "DynPanelModel.hpp"  // DynChannelId/resolvePanelText（模型层流）
#include "DynPanelTypes.hpp"  // DynPanelServices/DynModuleSessionState
#include "DynCurveChartView.hpp" // 曲线视图（组合控件）

class QLabel;
class QComboBox;
class QPlainTextEdit;
class QDoubleSpinBox;
class QPushButton;

#include <string>
#include <vector>

namespace sdurws::ird::dynamics {

class DynPanelModule;  // 前置声明（构造入参引用——完整类型在 DynPanelModule.hpp）

/**
 * @brief dynamics 主面板（工作流页＋曲线视图——宿主装配层经面板工厂
 *        创建并持有；模块引用由工厂闭包传递）。
 *
 * 数据流纪律：全部数据消费走 DynPanelServices 缝与模型层流（L-D1~
 * L-D6）——面板零环境访问、零统计、零命令判定；缝空＝如实降级呈现。
 */
class DynamicsPanelWidget final : public QWidget {
public:
    /**
     * @brief 构造主面板并完成全部控件接线（每次调用新建——装配层
     *        恰调一次；[in] 模块引用非 owning，存活期由装配层保证）。
     *
     * @param module [in] 插件模块（服务缝＋会话态的持有者——面板经
     *                它访问缝与会话；构造即做一次会话刷新）
     * @param parent [in] Qt 父控件（可空——宿主布局接管）
     */
    explicit DynamicsPanelWidget(DynPanelModule& module,
                                 QWidget* parent = nullptr);

    /**
     * @brief 会话刷新（会话事实变化后的整面重投影——现取缝/会话态
     *        重建呈现；不触任何缓存权威——面板零缓存纪律）。
     */
    void refreshFromSession();

private:
    // ---- 私有流（UI 事件→模型层调用——每段注释标注消费的 L-Dx）----
    void submitCommand(const std::string& commandToken); ///< 命令按钮点击流
    void runReplayAt();        ///< 回放时刻查询流（L-D5）
    void locatePeakAndJump();  ///< 峰值定位＋游标跳转流（L-D4）
    void refreshCurveView();   ///< 曲线视图刷新流（L-D2 数据消费）
    void refreshReadiness();   ///< 就绪投影呈现流（L-D1）
    void rebuildJointCombo();  ///< 关节下拉重建（曲线投影行集驱动）

    // ---- 文案便利（L-D6 解析——全部用户文本的唯一入口）------------
    QString panelText(const std::string& titleKey) const;

    DynPanelModule& m_module;        ///< 模块引用（缝＋会话态——非 owning）
    DynCurveChartView* m_chart = nullptr;   ///< 曲线视图（曲线视图页）
    QLabel* m_readinessLabel = nullptr;     ///< 就绪投影呈现（工作流页）
    QPlainTextEdit* m_recentEdit = nullptr; ///< 最近命令清单（只读呈现）
    QLabel* m_replayReading = nullptr;      ///< 回放时刻读数（逐关节文本）
    QDoubleSpinBox* m_replayTimeS = nullptr;///< 回放时刻输入（单位 s）
    QComboBox* m_jointCombo = nullptr;      ///< 关节选择（曲线视图页）
    QComboBox* m_channelCombo = nullptr;    ///< 通道选择（曲线视图页）
    QLabel* m_dataStatusLabel = nullptr;    ///< 数据完整性标注（数据不足态）
    std::vector<QPushButton*> m_commandButtons; ///< 命令按钮（可用性门控
                                                ///<   刷新——与目录同序）
};

}  // namespace sdurws::ird::dynamics

#endif  // IRD_DYNAMICS_PLUGIN_DYNAMICSPANELWIDGET_HPP
