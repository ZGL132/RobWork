/**
 * @file   SelCatalogPanelWidget.hpp
 * @brief  selection 工作流页＋目录管理页＋候选表页主面板（三页合一
 *         Tab 容器）——§16 WP-19-T10 行界面交付面的呈现本体（零业务
 *         计算：数据消费全经模型层与服务缝；WP-19-T10）。
 *
 * 设计依据：
 *   - units/selection.md §16 WP-19-T10 行（目录选择/版本/候选表/工作
 *     点摘要/淘汰原因/可行集/回填入口/进度/取消/诊断的呈现面——三页
 *     分区承载）、§3.4（插件零计算红线——面板零判定零环境访问）、
 *     §2.3（NFR-PERF-01——UI 线程不执行计算：数据全部缝现取）；
 *   - units/ui.md §6.6（UX-02——文案全经键解析，零哈希/内部标识）；
 *     §6.3/§6.8（UX-10 七态素材呈现：未完成附缺项、计算中、数据
 *     不足标注、失败/拒绝如实呈现）；
 *   - 先例：dynamics/plugin/DynamicsPanelWidget.hpp（域面板零业务
 *     判定纪律——WP-17-T09；selection 按其形态承载为三页）；
 *   - 任务契约 tasks/foundation/WP-19-T10.json acceptance 1/2。
 *
 * 线程约束：仅 UI 线程访问（缝调用与会话态同线程——SelPanelTypes.hpp）。
 * Qt 形态：QWidget 派生、纯虚 override——零 Q_OBJECT（无信号槽需求；
 *   按钮点击/表格选择经 lambda 连接；AUTOMOC 保持 OFF——单元 CMake
 *   落位注）。
 */

#ifndef IRD_SELECTION_PLUGIN_SELCATALOGPANELWIDGET_HPP
#define IRD_SELECTION_PLUGIN_SELCATALOGPANELWIDGET_HPP

#include <QWidget>

#include "SelPanelModel.hpp"  // L-Sx 模型层流（数据消费入口）
#include "SelPanelTypes.hpp"  // SelPanelServices/SelModuleSessionState

class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTabWidget;
class QTableWidget;

namespace sdurws::ird::selection {

class SelPanelModule;  // 前置声明（构造入参引用——完整类型在
                       //   SelPanelModule.hpp）

/**
 * @brief selection 主面板（工作流页＋目录管理页＋候选表页——宿主装配
 *        层经面板工厂创建并持有；模块引用由工厂闭包传递）。
 *
 * 数据流纪律：全部数据消费走 SelPanelServices 缝与模型层流（L-S1~
 * L-S5）——面板零环境访问、零业务判定；缝空＝如实降级呈现。
 */
class SelCatalogPanelWidget final : public QWidget {
public:
    /**
     * @brief 构造主面板并完成全部控件接线（每次调用新建——装配层
     *        恰调一次；[in] 模块引用非 owning，存活期由装配层保证）。
     *
     * @param module [in] 插件模块（服务缝＋会话态的持有者——面板经
     *                它访问缝与会话；构造即做一次会话刷新）
     * @param parent [in] Qt 父控件（可空——宿主布局接管）
     */
    explicit SelCatalogPanelWidget(SelPanelModule& module,
                                   QWidget* parent = nullptr);

    /**
     * @brief 会话刷新（会话事实变化后的整面重投影——现取缝/会话态
     *        重建呈现；不触任何缓存权威——面板零缓存纪律）。
     */
    void refreshFromSession();

private:
    // ---- 私有流（UI 事件→模型层调用——每段注释标注消费的 L-Sx）----
    void submitBackfillCommand();  ///< 回填按钮点击流（L-S4）
    void refreshReadiness();       ///< 就绪投影呈现流（L-S1）
    void refreshCatalogTable();    ///< 目录版本表刷新流（L-S2）
    void refreshCandidateTables(); ///< 候选/原因/缺口表刷新流（L-S3）

    // ---- 文案便利（L-S5 解析——全部用户文本的唯一入口）------------
    QString panelText(const std::string& titleKey) const;

    SelPanelModule& m_module;            ///< 模块引用（缝＋会话态——非 owning）
    QTabWidget* m_tabs = nullptr;        ///< 三页 Tab 容器
    QLabel* m_readinessLabel = nullptr;  ///< 就绪投影呈现（工作流页）
    QLabel* m_missingLabel = nullptr;    ///< 缺项清单呈现（UX-10"未完成
                                         ///<   附缺项列表"——工作流页）
    QPushButton* m_backfillButton = nullptr; ///< 回填入口按钮（工作流页——
                                             ///<   唯一写命令的发起位）
    QPlainTextEdit* m_recentEdit = nullptr;  ///< 最近回填提交清单（只读
                                             ///<   呈现——含复算提示素材）
    QTableWidget* m_catalogTable = nullptr;  ///< 目录版本表（目录管理页）
    QLabel* m_catalogHintLabel = nullptr;    ///< 目录空态/选中提示（目录
                                             ///<   管理页——空缝降级呈现）
    QTableWidget* m_candidateTable = nullptr; ///< 候选表（候选表页）
    QTableWidget* m_rejectionTable = nullptr; ///< 淘汰原因明细表（候选表页）
    QTableWidget* m_gapTable = nullptr;       ///< 数据缺口明细表（候选表页
                                              ///<   ——与原因分轨呈现）
};

}  // namespace sdurws::ird::selection

#endif  // IRD_SELECTION_PLUGIN_SELCATALOGPANELWIDGET_HPP
