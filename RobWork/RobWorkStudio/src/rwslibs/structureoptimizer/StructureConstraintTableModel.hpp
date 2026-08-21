#ifndef RWS_STRUCTUREOPTIMIZATION_STRUCTURECONSTRAINTTABLEMODEL_HPP
#define RWS_STRUCTUREOPTIMIZATION_STRUCTURECONSTRAINTTABLEMODEL_HPP

#include "StructureOptimizationTypes.hpp"

#include <QAbstractTableModel>
#include <QString>

namespace rws {

/**
 * @brief 结构优化约束条件表格数据模型类。
 * 
 * 继承自 Qt 的 QAbstractTableModel，实现了自定义的模型-视图 (Model-View) 绑定。
 * 用于将优化的物理/工程约束列表 (std::vector<StructureConstraint>) 呈现到 UI 表格 (QTableView) 中，
 * 支持用户在界面上动态编辑约束的目标指标、比较算子 (大于/小于/介于)、阈值、使能开关及硬/软约束属性。
 */
class StructureConstraintTableModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    /**
     * @brief 表格模型列索引定义枚举。
     */
    enum Column
    {
        IdColumn = 0,             //!< 第 0 列: 约束唯一 ID (如 "c_max_length")
        LabelColumn,              //!< 第 1 列: UI 显示友好名称 (如 "臂长上限约束")
        TargetColumn,             //!< 第 2 列: 约束监控的目标指标 (如 Reachability, TotalLength, CollisionFreeRate)
        KindColumn,               //!< 第 3 列: 比较算子类型 (如 GreaterThan, LessThan, Between)
        ThresholdColumn,          //!< 第 4 列: 主阈值数值 (如 1.5)
        SecondaryThresholdColumn, //!< 第 5 列: 副阈值数值 (当 Kind 为 Between 区间比较时使用)
        EnabledColumn,            //!< 第 6 列: 使能状态复选框 (CheckBox，控制该约束是否生效)
        HardColumn,               //!< 第 7 列: 是否为硬约束复选框 (CheckBox，true: 违反直接判为 Infeasible 淘汰；false: 软约束扣分)
        ColumnCount               //!< 总列数统计 (用于 columnCount 函数返回 8)
    };

    /**
     * @brief 构造函数。
     * @param parent 可选的 Qt 父对象指针，用于 QObject 内存树管理。
     */
    explicit StructureConstraintTableModel(QObject* parent = nullptr);

    /**
     * @brief 获取表格的总行数 (即当前约束条件的总数量)。
     * @param parent 父模型索引 (表格模型传默认无效索引)
     * @return int 表格当前行数
     */
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;

    /**
     * @brief 获取表格的总列数 (即 ColumnCount 枚举值 8)。
     * @param parent 父模型索引
     * @return int 表格列数
     */
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;

    /**
     * @brief 根据单元格模型索引和角色获取要在界面中渲染的数据。
     * 
     * 处理 Qt::DisplayRole (文本显示)、Qt::EditRole (编辑态数据) 
     * 以及 Qt::CheckStateRole (Enabled 列与 Hard 列的勾选框状态)。
     * 
     * @param index 单元格的模型索引 (行、列)
     * @param role 数据请求角色
     * @return QVariant 打包后的 Qt 通用变体类型数据
     */
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;

    /**
     * @brief 获取表格水平表头（列标题）的内容。
     * 
     * 水平表头依次返回 "ID", "Label", "Target Metric", "Kind", "Threshold", "Secondary Threshold", "Enabled", "Hard"。
     * 
     * @param section 行或列的序号
     * @param orientation 表头方向 (Qt::Horizontal 水平表头 / Qt::Vertical 垂直行号)
     * @param role 数据请求角色
     * @return QVariant 表头显示的标题文本
     */
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

    /**
     * @brief 获取指定单元格的交互标志位 (控制单元格可选中、可编辑或可勾选)。
     * 
     * @param index 单元格的模型索引
     * @return Qt::ItemFlags 单元格标志位组合
     */
    Qt::ItemFlags flags(const QModelIndex& index) const override;

    /**
     * @brief 当用户在前端 UI 界面上修改约束数值、切换下拉框或勾选复选框时更新底层数据。
     * 
     * 将前端输入的数据写回对应的 StructureConstraint 结构体，并发出 dataChanged 信号驱动视图更新。
     * 
     * @param index 被编辑单元格的模型索引
     * @param value 用户输入的新数值 / 新勾选状态
     * @param role 编辑角色 (Qt::EditRole 或 Qt::CheckStateRole)
     * @return true 数据更新成功；false 索引无效或修改失败
     */
    bool setData(const QModelIndex& index, const QVariant& value,
                 int role = Qt::EditRole) override;

    /**
     * @brief 用新的约束条件列表重置并刷新表格数据。
     * 
     * 内部会调用 beginResetModel() 和 endResetModel() 通知 UI 视图重新绑定绘制。
     * 
     * @param constraints 新的约束定义向量
     */
    void setConstraints(const std::vector<StructureConstraint>& constraints);

    /**
     * @brief 在表格末尾追加一个新的约束条件。
     * 
     * 内部会调用 beginInsertRows() 和 endInsertRows() 通知视图添加新行。
     * 
     * @param constraint 待添加的约束结构体
     * @return int 插入新行后的行索引号
     */
    int appendConstraint(const StructureConstraint& constraint);

    /**
     * @brief 删除指定行索引号的约束条件。
     * 
     * 内部会调用 beginRemoveRows() 和 endRemoveRows() 通知视图移除对应行。
     * 
     * @param row 待删除的行号
     * @return true 删除成功；false 行号越界删除失败
     */
    bool removeConstraint(int row);

    /**
     * @brief 获取当前表格中保存的所有约束条件数据的只读引用。
     * @return const std::vector<StructureConstraint>& 最新的约束条件列表引用
     */
    const std::vector<StructureConstraint>& constraints() const;

Q_SIGNALS:
    void editRejected(const QString& message);

private:
    std::vector<StructureConstraint> _constraints; //!< 底层真正保存所有约束定义数据的容器
};

} // namespace rws

#endif // RWS_STRUCTUREOPTIMIZATION_STRUCTURECONSTRAINTTABLEMODEL_HPP
