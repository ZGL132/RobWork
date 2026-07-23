#ifndef RWS_STRUCTUREOPTIMIZER_STRUCTUREOPTIMIZERWIDGET_HPP
#define RWS_STRUCTUREOPTIMIZER_STRUCTUREOPTIMIZERWIDGET_HPP

#include <QWidget>

//! @brief StructureOptimizer 主 Widget 存根。
//!
//! 后续 Task 中将包含优化参数配置、运行控制、结果可视化的完整 UI。
class StructureOptimizerWidget : public QWidget
{
    Q_OBJECT

public:
    explicit StructureOptimizerWidget(QWidget* parent = nullptr);
};

#endif // RWS_STRUCTUREOPTIMIZER_STRUCTUREOPTIMIZERWIDGET_HPP
