/**
 * @file   FlowLayout.hpp
 * @brief  流式栅格布局（FlowLayout）——控件按行排列、行满自动换行的
 *         公共呈现构件。
 *
 * 设计依据：
 *   - units/ui.md §4.4（响应式尺寸与最小可用布局——左栏最小内容尺寸
 *     240 px；顶栏/命令条在窄宽度下必须可缩）、§10.1 v1.25 增量注
 *     （UI-T24 P2 装配侧尺寸策略的契约面落位）；
 *   - 任务契约 tasks/foundation/UI-T24.json acceptance 2/3（中央区保护
 *     ＋需求命令按钮行流式栅格重排零重叠）。
 *
 * 背景说明（为什么需要本类）：Qt 公共 API 没有可直接使用的流式布局
 * （QHBoxLayout 单行排布会把整行子控件最小宽度之和顶成容器最小宽度）。
 * UI-T24 钳制源定位（traceability/builds/ui-t24/clamp-source.md）实证：
 * 需求面板命令条 12 按钮单行累计最小宽 2074 px、主 Dock 顶栏单行累计
 * 1054 px，把停靠列宽与中央三维视图一并钳死（中央区实测 18 px）。流式
 * 栅格让按钮行在窄容器下换行承载，行最小宽坍缩为"单行内最宽控件"，
 * 容器恢复 §4.4 规定的可缩性。
 *
 * 消费方：ui 内容装配层（顶栏 ird_top_bar_content）与 requirements 插件
 * 面板（命令条）——经本公共头共用同一实现（单一权威，不私设第二份流式
 * 布局拷贝；NFR-MNT 单点纪律）。
 *
 * 线程约束：仅 UI 线程构造与访问（QLayout 固有约束）。
 * 无 Q_OBJECT 声明：本类不自持信号槽（零 AUTOMOC 依赖——ui 库目标不开
 * AUTOMOC 的既定口径，ui.md §10.1 v0.5 注⑤）。
 */

#ifndef IRD_UI_FLOWLAYOUT_HPP
#define IRD_UI_FLOWLAYOUT_HPP

#include <QLayout>
#include <QRect>
#include <QSize>
#include <QWidget>

#include <list>

namespace sdurws {
namespace ird {
namespace ui {

/**
 * @brief 流式栅格布局：子控件自左向右逐个排布，行尾放不下即换行。
 *
 * 生命周期/所有权：构造时绑定父控件（QLayout 标准形态——父控件析构
 * 级联销毁布局与其托管的子项）；addWidget 转入的子控件由 Qt 父子树
 * 托管，本布局不拥有业务对象。非线程安全（仅 UI 线程）。
 *
 * 高度自适应：hasHeightForWidth 恒真——父布局（QVBoxLayout 等）按宽度
 * 反查所需行高，换行后容器自动增高（需求面板命令条从 25 px 单行变多行
 * 时面板纵向自然让位）。
 */
class FlowLayout final : public QLayout {
public:
    /**
     * @brief 构造流式布局并绑定父控件。
     *
     * @param parent   [in] 父控件（布局即为其主布局——与 QLayout(QWidget*)
     *                 语义一致）；不允许空。
     * @param hSpacing [in] 水平间距，单位 px；缺省 6（按钮行视觉呼吸感，
     *                 与 QStyle 默认布局间距同档）。
     * @param vSpacing [in] 垂直行距，单位 px；缺省 2（紧凑多行——命令条
     *                 换行不虚耗纵向空间）。
     */
    explicit FlowLayout(QWidget* parent, int hSpacing = 6, int vSpacing = 2);
    ~FlowLayout() override;

    // ---- QLayout 契约（子项管理——takeAt 交还所有权给调用方/Qt）----
    void addItem(QLayoutItem* item) override;
    int count() const override;
    QLayoutItem* itemAt(int index) const override;
    QLayoutItem* takeAt(int index) override;

    // ---- 尺寸协商 ----
    QSize sizeHint() const override;
    QSize minimumSize() const override;
    Qt::Orientations expandingDirections() const override;
    bool hasHeightForWidth() const override;
    int heightForWidth(int width) const override;
    void setGeometry(const QRect& rect) override;

private:
    /// 智能指针句柄（Qt 6.x 起用于兼容旧的 int 句柄遍历——本类只经
    /// itemAt/takeAt 访问，句柄即下标）。
    using ItemList = std::list<QLayoutItem*>;

    /**
     * @brief 执行换行排布计算（布局引擎本体——两遍算法）。
     *
     * 第 1 遍按"行剩余宽度是否容纳该项"分行并累计每行行高（行内最高项）；
     * 第 2 遍逐行落位，行内所有项按最终行高垂直居中（单遍算法中后到的
     * 高项会改变行高、先行项已按旧行高落位，居中会失真——两遍保证一致）。
     *
     * @param rect     [in] 可用几何（setGeometry 传实际区域；heightForWidth
     *                 传以宽度构造的探测区域）
     * @param testOnly [in] true＝只算不落位（尺寸协商路径；测试探测不得
     *                 触碰子控件几何）
     * @return 内容总高，单位 px（含行距；不含父边距——QLayout 已扣除）
     */
    int doLayout(const QRect& rect, bool testOnly) const;

    ItemList m_items;     ///< 子项序列（add 序即排布序——确定性）
    int m_hSpace;         ///< 水平间距，单位 px（构造期定值）
    int m_vSpace;         ///< 垂直行距，单位 px（构造期定值）
};

}  // namespace ui
}  // namespace ird
}  // namespace sdurws

#endif  // IRD_UI_FLOWLAYOUT_HPP
