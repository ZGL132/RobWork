/**
 * @file   OptimizationPanelWidget.hpp
 * @brief  optimization 主面板（四页合一 Tab 容器：变量表/约束页/运行
 *         控制〔取消/进度漏斗〕/候选表与对比）——§9.1 运行控制与 §3.1
 *         插件界面四页的呈现面（零业务计算：数据消费全经模型层与服务
 *         缝；WP-20-T10）。
 *
 * 设计依据：
 *   - units/optimization.md §9.1（运行控制 R1 边界：UI 只显示进度和
 *     取消状态；**暂停相关能力归 OPT-D/R2——本面板零其呈现入口**
 *     〔V12-06/AT-35 边界，契约测试词表扫描钉住〕；"不在 UI 线程执行
 *     候选评估"红线——全部数据消费走缝的现成投影）、§6.5（阶段锁
 *     呈现为阻塞横幅＋缺项清单——绝不呈现为候选淘汰）、§7.1（八项
 *     指标全列呈现，缺失显示"—"）、UX-13 精神（候选对比区——差异
 *     高亮的呈现半区）
 *   - 先例：dynamics/plugin/DynamicsPanelWidget.hpp（域面板零业务判定
 *     纪律＋两页合一 Tab 形态——WP-17-T09；optimization 收缩为四页）
 *   - 需求 UX-02（工程用语、零哈希/内部插件名进用户文本——文案全经
 *     键解析，候选行以"候选 N"呈现）、UX-10（七态统一呈现素材：未
 *     完成附缺项列表、计算中附进度阶段与取消）、UX-03（正常取消不
 *     属于错误——零错误诊断呈现）、TASK-01（协作取消——批边界粒度）
 *   - 任务契约 tasks/foundation/WP-20-T10.json acceptance 1/2/3
 *
 * 线程约束：仅 UI 线程访问（缝调用与会话态同线程——OptPanelTypes.hpp）。
 * Qt 形态：QWidget 派生、纯虚 override——零 Q_OBJECT（无信号槽需求；
 *   按钮点击经 lambda 连接；AUTOMOC 保持 OFF——单元 CMake 落位注）。
 */

#ifndef IRD_OPTIMIZATION_PLUGIN_OPTIMIZATIONPANELWIDGET_HPP
#define IRD_OPTIMIZATION_PLUGIN_OPTIMIZATIONPANELWIDGET_HPP

#include <QWidget>

#include "OptPanelModel.hpp"  // 模型层流（L-O1~L-O9——被测主面）
#include "OptPanelTypes.hpp"  // OptPanelServices/OptModuleSessionState

class QLabel;
class QComboBox;
class QTableWidget;
class QPushButton;
class QTabWidget;

#include <cstddef>
#include <string>
#include <vector>

namespace sdurws::ird::optimization {

class OptPanelModule;  // 前置声明（构造入参引用——完整类型在 OptPanelModule.hpp）

/**
 * @brief optimization 主面板（四页合一 Tab——宿主装配层经面板工厂
 *        创建并持有；模块引用由工厂闭包传递）。
 *
 * 数据流纪律：全部数据消费走 OptPanelServices 缝与模型层流（L-O1~
 * L-O9）——面板零环境访问、零评估、零命令判定；缝空＝如实降级呈现。
 */
class OptimizationPanelWidget final : public QWidget {
public:
    /**
     * @brief 构造主面板并完成全部控件接线（每次调用新建——装配层
     *        恰调一次；[in] 模块引用非 owning，存活期由装配层保证）。
     *
     * @param module [in] 插件模块（服务缝＋会话态的持有者——面板经
     *                它访问缝与会话；构造即做一次会话刷新）
     * @param parent [in] Qt 父控件（可空——宿主布局接管）
     */
    explicit OptimizationPanelWidget(OptPanelModule& module,
                                     QWidget* parent = nullptr);

    /**
     * @brief 会话刷新（会话事实变化后的整面重投影——现取缝/会话态
     *        重建呈现；不触任何缓存权威——面板零缓存纪律）。
     */
    void refreshFromSession();

private:
    // ---- 私有流（UI 事件→模型层调用——每段注释标注消费的 L-Ox）----
    void requestStart();       ///< 启动按钮点击流（L-O5 启动半区）
    void requestCancel();      ///< 取消按钮点击流（L-O5 取消半区——
                               ///<   UX-03 零错误诊断）
    void refreshRunControl();  ///< 运行控制页刷新流（L-O1 就绪行＋L-O4
                               ///<   漏斗——进度/取消状态呈现）
    void refreshVariablePage();   ///< 变量表页刷新流（L-O2）
    void refreshConstraintPage(); ///< 约束页刷新流（L-O3＋L-O8 横幅）
    void refreshCandidatePage();  ///< 候选表页刷新流（L-O6 表＋L-O7 对比区）
    void rebuildComparison(int indexA, int indexB); ///< 对比区重建（L-O7）

    // ---- 文案便利（L-O9 解析——全部用户文本的唯一入口）------------
    QString panelText(const std::string& titleKey) const;

    OptPanelModule& m_module;          ///< 模块引用（缝＋会话态——非 owning）
    QTabWidget* m_tabs = nullptr;      ///< 四页 Tab 容器（完整型在实现 TU）
    QLabel* m_readinessLabel = nullptr;   ///< 就绪投影呈现（变量表页顶——
                                          ///<   七态素材＋缺项清单）
    QTableWidget* m_variableTable = nullptr; ///< 变量表（绑定行×11 列）
    QLabel* m_constraintBanner = nullptr;    ///< 约束页横幅区（阶段锁/阻塞——
                                             ///<   §6.5 呈现边界）
    QTableWidget* m_constraintTable = nullptr; ///< 约束执行清单表（序/约束/依据）
    QPushButton* m_startButton = nullptr;    ///< "检查并计算"按钮（L-O5 启动）
    QPushButton* m_cancelButton = nullptr;   ///< "取消计算"按钮（L-O5 取消）
    QLabel* m_progressLabel = nullptr;       ///< 进度百分比＋批计数读数
    QLabel* m_statusLabel = nullptr;         ///< 运行状态行（状态词直译——
                                             ///<   取消呈现为状态非错误）
    std::vector<QLabel*> m_funnelLabels;     ///< 漏斗八段标签（词表序——
                                             ///<   与漏斗行集逐位对位）
    QTableWidget* m_candidateTable = nullptr; ///< 候选表（标签/状态/八指标/
                                              ///<   淘汰原因等列）
    QComboBox* m_compareA = nullptr;         ///< 对比 A 侧选择（候选行序）
    QComboBox* m_compareB = nullptr;         ///< 对比 B 侧选择（候选行序）
    QTableWidget* m_compareTable = nullptr;  ///< 对比区表（八指标 A/B/差值）
    QLabel* m_emptyHint = nullptr;           ///< 候选页空态提示（尚无结果）
};

}  // namespace sdurws::ird::optimization

#endif  // IRD_OPTIMIZATION_PLUGIN_OPTIMIZATIONPANELWIDGET_HPP
