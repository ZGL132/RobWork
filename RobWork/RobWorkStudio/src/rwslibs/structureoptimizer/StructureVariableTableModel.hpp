#ifndef RWS_STRUCTUREOPTIMIZATION_STRUCTUREVARIABLETABLEMODEL_HPP
#define RWS_STRUCTUREOPTIMIZATION_STRUCTUREVARIABLETABLEMODEL_HPP

#include "StructureOptimizationTypes.hpp"

#include <QAbstractTableModel>

namespace rws {

/**
 * @brief 结构设计变量表格数据模型类。
 * 
 * 继承自 Qt 的 QAbstractTableModel，实现了自定义的模型-视图 (Model-View) 数据适配。
 * 用于将优化的结构设计变量列表 (std::vector<StructureDesignVariable>) 绑定显示在 QTableView 视图中，
 * 并支持用户在界面上直接编辑变量 ID、名称、目标名称、取值范围 (Min/Max)、步长 (Step) 及使能状态 (Enabled)。
 */
class StructureVariableTableModel : public QAbstractTableModel
{
public:
    /**
     * @brief 表格模型列索引定义枚举。
     */
    enum Column
    {
        IdColumn = 0,    //!< 第 0 列: 变量 ID (字符串)
        LabelColumn,     //!< 第 1 列: 显示标签/名称 (字符串)
        TargetColumn,    //!< 第 2 列: 目标关节/坐标系/几何体名称 (字符串)
        KindColumn,      //!< 第 3 列: 变量物理种类 (只读枚举名称，如 JointPositionX)
        CurrentColumn,   //!< 第 4 列: 当前初始物理值 (双精度浮点数)
        MinimumColumn,   //!< 第 5 列: 寻优搜索范围下限 (双精度浮点数)
        MaximumColumn,   //!< 第 6 列: 寻优搜索范围上限 (双精度浮点数)
        StepColumn,      //!< 第 7 列: 搜索步长/量化网格步长 (双精度浮点数)
        EnabledColumn,   //!< 第 8 列: 是否勾选启用该变量参与优化 (复选框 CheckBox)
        ColumnCount      //!< 总列数统计 (用于 columnCount 函数返回)
    };

    /**
     * @brief 构造函数。
     * @param parent 可选的 Qt 父对象指针，用于 Qt 对象树内存管理。
     */
    explicit StructureVariableTableModel(QObject* parent = nullptr);

    /**
     * @brief 获取表格的总行数 (即底层设计变量的数量)。
     * @param parent 父模型索引 (树状模型使用，对于表格模型通常为无效索引)
     * @return int 表格行数
     */
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;

    /**
     * @brief 获取表格的总列数 (即 ColumnCount 枚举值)。
     * @param parent 父模型索引
     * @return int 表格列数
     */
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;

    /**
     * @brief 根据模型索引和角色获取要在界面单元格中渲染的数据。
     * 
     * 处理 Qt::DisplayRole (文本显示)、Qt::EditRole (编辑态数据) 和 Qt::CheckStateRole (Enabled 列的复选框勾选状态)。
     * 
     * @param index 当前单元格的行、列模型索引
     * @param role 数据请求角色 (DisplayRole / EditRole / CheckStateRole 等)
     * @return QVariant 打包后的 Qt 通用变体类型数据，供 View 视图渲染
     */
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;

    /**
     * @brief 获取表格水平表头或垂直表头显示的标题内容。
     * 
     * 水平表头返回 "ID", "Name", "Target", "Type", "Current", "Min", "Max", "Step", "Enabled" 等列名；
     * 垂直表头返回从 1 开始的行号。
     * 
     * @param section 行或列的序号
     * @param orientation 表头方向 (Qt::Horizontal 水平表头 / Qt::Vertical 垂直行号)
     * @param role 数据请求角色
     * @return QVariant 表头标题内容
     */
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

    /**
     * @brief 获取单元格的交互标志位 (标志单元格是否可选中、可编辑、可勾选)。
     * 
     * @param index 当前单元格的模型索引
     * @return Qt::ItemFlags 单元格标志组合 (如 Qt::ItemIsEditable | Qt::ItemIsUserCheckable)
     */
    Qt::ItemFlags flags(const QModelIndex& index) const override;

    /**
     * @brief 当用户在 UI 界面上修改单元格数值或切换复选框勾选状态时更新底层数据。
     * 
     * 将前端用户输入的数据解析并写回对应的 StructureDesignVariable 结构体属性中，
     * 并发出 dataChanged 信号通知视图重新绘制。
     * 
     * @param index 被编辑单元格的模型索引
     * @param value 用户在界面输入的新数值 / 新勾选状态
     * @param role 数据编辑角色 (Qt::EditRole 或 Qt::CheckStateRole)
     * @return true 数据更新成功
     * @return false 索引无效或编辑失败
     */
    bool setData(const QModelIndex& index, const QVariant& value,
                 int role = Qt::EditRole) override;

    /**
     * @brief 使用新的设计变量向量重置并刷新表格数据。
     * 
     * 内部会触发 beginResetModel() 和 endResetModel() 信号通知绑定该模型的 QTableView 重绘。
     * 
     * @param variables 新的设计变量数据列表
     */
    void setVariables(const std::vector<StructureDesignVariable>& variables);

    /**
     * @brief 获取当前模型中保存的所有设计变量数据的只读引用。
     * 
     * @return const std::vector<StructureDesignVariable>& 最新的设计变量数据列表引用
     */
    const std::vector<StructureDesignVariable>& variables() const;

private:
    std::vector<StructureDesignVariable> _variables; //!< 底层真正保存设计变量数据列表的容器
};

} // namespace rws

#endif // RWS_STRUCTUREOPTIMIZATION_STRUCTUREVARIABLETABLEMODEL_HPP