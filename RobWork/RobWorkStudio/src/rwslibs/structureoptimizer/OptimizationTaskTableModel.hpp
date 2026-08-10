#ifndef RWS_STRUCTUREOPTIMIZATION_OPTIMIZATIONTASKTABLEMODEL_HPP
#define RWS_STRUCTUREOPTIMIZATION_OPTIMIZATIONTASKTABLEMODEL_HPP

#include "StructureOptimizationTypes.hpp"

#include <QAbstractTableModel>

namespace rws {

/**
 * @brief 结构优化任务点表格数据模型类。
 * 
 * 继承自 Qt 的 QAbstractTableModel，实现了自定义的模型-视图 (Model-View) 架构。
 * 用于将优化的目标任务点列表 (std::vector<OptimizationTaskPoint>) 绑定到 UI 表格控件 (QTableView) 中，
 * 支持用户在界面上动态编辑任务点的位姿坐标 (X,Y,Z,Roll,Pitch,Yaw)、必达属性 (Required)、使能状态 (Enabled) 及评分权重 (Weight)。
 */
class OptimizationTaskTableModel : public QAbstractTableModel
{
public:
    /**
     * @brief 表格模型列索引定义枚举。
     */
    enum Column
    {
        IdColumn = 0,     //!< 第 0 列: 任务点唯一 ID (字符串)
        NameColumn,       //!< 第 1 列: 任务点显示名称 (字符串)
        RequiredColumn,   //!< 第 2 列: 是否为必达硬约束点 (复选框 CheckBox，未达到则方案判为 Infeasible)
        EnabledColumn,    //!< 第 3 列: 是否勾选启用该任务点参与优化 (复选框 CheckBox)
        XColumn,          //!< 第 4 列: 三维 X 轴平移位置 (m)
        YColumn,          //!< 第 5 列: 三维 Y 轴平移位置 (m)
        ZColumn,          //!< 第 6 列: 三维 Z 轴平移位置 (m)
        RollColumn,       //!< 第 7 列: 姿态 Roll 翻滚角 (rad/deg)
        PitchColumn,      //!< 第 8 列: 姿态 Pitch 俯仰角 (rad/deg)
        YawColumn,        //!< 第 9 列: 姿态 Yaw 偏航角 (rad/deg)
        RefFrameColumn,   //!< 第 10 列: 参考基准坐标系名称 (如 "WORLD")
        TcpFrameColumn,   //!< 第 11 列: 末端工具 TCP 坐标系名称 (如 "Tool0")
        WeightColumn,     //!< 第 12 列: 该任务点的可达性/可操作度评分权重 (双精度浮点数)
        ColumnCount       //!< 总列数统计 (用于 columnCount 函数返回)
    };

    /**
     * @brief 构造函数。
     * @param parent 可选的 Qt 父对象指针，用于 QObject 对象树内存管理。
     */
    explicit OptimizationTaskTableModel(QObject* parent = nullptr);

    /**
     * @brief 获取表格的总行数 (即底层任务点的总数量)。
     * @param parent 父模型索引 (表格模型通常传默认无效索引)
     * @return int 表格当前行数
     */
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;

    /**
     * @brief 获取表格的总列数 (即 ColumnCount 枚举值 13)。
     * @param parent 父模型索引
     * @return int 表格列数
     */
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;

    /**
     * @brief 根据单元格模型索引和角色获取要在界面中渲染的数据。
     * 
     * 处理 Qt::DisplayRole (文本显示)、Qt::EditRole (编辑态数据) 
     * 以及 Qt::CheckStateRole (Required 列和 Enabled 列的勾选框状态)。
     * 
     * @param index 单元格的行、列模型索引
     * @param role 数据请求角色
     * @return QVariant 供 QTableView 渲染的打包变体数据
     */
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;

    /**
     * @brief 获取表格水平表头（列标题）或垂直表头（行号）的内容。
     * 
     * 水平表头依次返回 "ID", "Name", "Required", "Enabled", "X", "Y", "Z", "Roll", "Pitch", "Yaw", "RefFrame", "TCP", "Weight"。
     * 
     * @param section 行或列的序号
     * @param orientation 表头方向 (Qt::Horizontal 水平表头 / Qt::Vertical 垂直行号)
     * @param role 数据请求角色
     * @return QVariant 表头显示的标题内容
     */
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

    /**
     * @brief 获取指定单元格的交互标志位 (控制单元格是否可选中、可编辑、可勾选)。
     * 
     * @param index 单元格的模型索引
     * @return Qt::ItemFlags 单元格标志位组合
     */
    Qt::ItemFlags flags(const QModelIndex& index) const override;

    /**
     * @brief 当用户在前端 UI 界面上修改单元格数值或勾选框时更新底层数据。
     * 
     * 将前端用户输入的数据解析后写回对应的 OptimizationTaskPoint 结构体，
     * 并发出 dataChanged 信号通知视图刷新重绘。
     * 
     * @param index 被编辑单元格的模型索引
     * @param value 用户输入的新数值 / 新勾选状态
     * @param role 编辑角色 (Qt::EditRole 或 Qt::CheckStateRole)
     * @return true 数据更新成功；false 索引无效或修改失败
     */
    bool setData(const QModelIndex& index, const QVariant& value,
                 int role = Qt::EditRole) override;

    /**
     * @brief 重置并用新的任务点列表刷新整个表格数据。
     * 
     * 内部会调用 beginResetModel() 和 endResetModel() 通知视图重新绑定绘制。
     * 
     * @param tasks 新的任务点数据向量
     */
    void setTasks(const std::vector<OptimizationTaskPoint>& tasks);

    /**
     * @brief 在表格末尾追加一个新的任务点。
     * 
     * 内部会调用 beginInsertRows() 和 endInsertRows() 以便视图动画平滑插入。
     * 
     * @param task 待添加的任务点数据结构体
     * @return int 插入新行后的行索引号
     */
    int appendTask(const OptimizationTaskPoint& task);

    /**
     * @brief 删除指定行索引号的任务点。
     * 
     * 内部会调用 beginRemoveRows() 和 endRemoveRows() 通知视图更新。
     * 
     * @param row 待删除的行号
     * @return true 删除成功；false 行号越界删除失败
     */
    bool removeTask(int row);

    /**
     * @brief 获取当前表格中保存的所有任务点数据的只读引用。
     * @return const std::vector<OptimizationTaskPoint>& 最新的任务点列表引用
     */
    const std::vector<OptimizationTaskPoint>& tasks() const;

private:
    std::vector<OptimizationTaskPoint> _tasks; //!< 底层真正保存所有任务点数据的容器
};

} // namespace rws

#endif // RWS_STRUCTUREOPTIMIZATION_OPTIMIZATIONTASKTABLEMODEL_HPP