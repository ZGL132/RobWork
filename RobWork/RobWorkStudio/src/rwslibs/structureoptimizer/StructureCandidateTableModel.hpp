#ifndef RWS_STRUCTUREOPTIMIZATION_STRUCTURECANDIDATETABLEMODEL_HPP
#define RWS_STRUCTUREOPTIMIZATION_STRUCTURECANDIDATETABLEMODEL_HPP

#include "StructureOptimizationTypes.hpp"

#include <QAbstractTableModel>

namespace rws {

/**
 * @brief 结构优化候选解结果表格数据模型类。
 * 
 * 继承自 Qt 的 QAbstractTableModel，用于在 UI 界面的 QTableView 控件中展示优化算法计算产出的
 * 所有候选解列表 (std::vector<StructureCandidateResult>)。
 * 
 * 该模型为只读视图模型，直观呈现各个候选解的可行性 (Feasible)、综合得分 (TotalScore)、
 * 运动学可达性、可操作度、关节裕度、无碰撞率、运动链总长以及相比基线 (Baseline) 模型的性能提升幅度 (%)。
 */
class StructureCandidateTableModel : public QAbstractTableModel
{
public:
    /**
     * @brief 表格模型列索引定义枚举。
     */
    enum Column
    {
        IndexColumn = 0,       //!< 第 0 列: 候选解编号索引 (如 #0 标识基线模型)
        FeasibleColumn,        //!< 第 1 列: 可行性标志 (显示 "Feasible" 可行 或 "Infeasible" 不可行)
        TotalScoreColumn,      //!< 第 2 列: 多目标综合加权总得分 (双精度浮点数，决策排序的核心依据)
        ReachabilityColumn,    //!< 第 3 列: 任务可达性比例 (百分比或比率，如 100%)
        ManipulabilityColumn,  //!< 第 4 列: 可操作度 (Manipulability) P10 分位数 (反映运动学灵活性)
        JointMarginColumn,     //!< 第 5 列: 关节限位安全裕度 P10 分位数 (反映远离物理极限角度的余量)
        CollisionColumn,       //!< 第 6 列: 无碰撞率 (Collision-Free Rate，百分比或比率)
        TotalLengthColumn,     //!< 第 7 列: 连杆运动链总长度 (m，反映机械臂本体紧凑度)
        ImprovementColumn,     //!< 第 8 列: 相比原始基线 (Baseline) 模型的综合性能提升幅度 (%)
        ColumnCount            //!< 总列数统计 (用于 columnCount 函数返回 9)
    };

    /**
     * @brief 构造函数。
     * @param parent 可选的 Qt 父对象指针，用于 QObject 对象树管理。
     */
    explicit StructureCandidateTableModel(QObject* parent = nullptr);

    /**
     * @brief 获取表格的总行数 (即候选解的总数量)。
     * @param parent 父模型索引 (表格模型通常传入默认无效索引)
     * @return int 表格行数
     */
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;

    /**
     * @brief 获取表格的总列数 (即 ColumnCount 枚举值 9)。
     * @param parent 父模型索引
     * @return int 表格列数
     */
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;

    /**
     * @brief 根据模型索引和数据角色获取要在界面单元格中渲染的数据。
     * 
     * 处理 Qt::DisplayRole (文本渲染) 与 Qt::ForegroundRole / Qt::BackgroundRole (针对不可行解或高分解进行颜色高亮)。
     * 
     * @param index 当前单元格的模型索引 (包含行、列号)
     * @param role 数据请求角色
     * @return QVariant 打包后的 Qt 通用变体类型数据
     */
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;

    /**
     * @brief 获取表格水平表头显示的标题文本内容。
     * 
     * 水平表头依次返回 "Index", "Feasible", "Score", "Reachability", "Manipulability", "Joint Margin", "Collision Free", "Total Length", "Improvement"。
     * 
     * @param section 行或列的序号
     * @param orientation 表头方向 (Qt::Horizontal 水平表头 / Qt::Vertical 垂直行号)
     * @param role 数据请求角色
     * @return QVariant 表头标题文本
     */
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

    /**
     * @brief 用新的候选解列表重置并刷新表格数据。
     * 
     * 内部会触发 beginResetModel() 和 endResetModel() 通知 QTableView 视图重绘。
     * 
     * @param candidates 候选解评估结果向量
     */
    void setCandidates(const std::vector<StructureCandidateResult>& candidates);

    /**
     * @brief 使用完整的结构优化运行结果对象更新模型。
     * 
     * 内部除了提取 result.candidates 候选解列表外，还会记录 result.baselineCandidateIndex，
     * 用于精确计算各个解相比基线 (Baseline) 模型的性能提升百分比 (Improvement %)。
     * 
     * @param result 结构优化最终运行结果对象
     */
    void setResult(const StructureOptimizationResult& result);

    /**
     * @brief 获取当前模型中保存的所有候选解数据的只读引用。
     * @return const std::vector<StructureCandidateResult>& 候选解数据列表引用
     */
    const std::vector<StructureCandidateResult>& candidates() const;

    /**
     * @brief 根据原始候选解编号索引查找对应的候选解结果结构体指针。
     * 
     * 用于 UI 在表格排序或筛选后，精准定位用户在界面选中的候选解对象。
     * 
     * @param candidateIndex 候选解的唯一编号索引 (如 0, 1, 5)
     * @return const StructureCandidateResult* 指向候选解对象的只读指针，未找到时返回 nullptr
     */
    const StructureCandidateResult* candidateByIndex(int candidateIndex) const;

private:
    std::vector<StructureCandidateResult> _candidates; //!< 底层真正保存所有候选解评估数据的容器
    int _baselineCandidateIndex = -1;                   //!< 原始基线 (Baseline) 模型在列表中的索引号 (缺省 -1)
};

} // namespace rws

#endif // RWS_STRUCTUREOPTIMIZATION_STRUCTURECANDIDATETABLEMODEL_HPP