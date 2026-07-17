#ifndef RWS_KINEMATICANALYSIS_TASKPOINTTABLEMODEL_HPP
#define RWS_KINEMATICANALYSIS_TASKPOINTTABLEMODEL_HPP

#include "KinematicAnalysisTypes.hpp"

#include <QAbstractTableModel>
#include <QString>
#include <vector>

namespace rws {

//! @brief Table model 列索引枚举,统一所有读写代码,避免列号硬编码漂移。
//! 列顺序与 RobotAnalysisCsv 标准字段一致,后两列 status / reason 是 UI 衍生,
//! 后面 8 列 raw/usable/bestQ/.../collision 是 P1 批量 IK 结果衍生。
//! 必须在 buildTaskPointTab / setItemDelegate 之前定义。
enum TaskPointColumn
{
    ColEnabled   = 0,
    ColId        = 1,
    ColName      = 2,
    ColType      = 3,
    ColRefFrame  = 4,
    ColTcpFrame  = 5,
    ColX         = 6,
    ColY         = 7,
    ColZ         = 8,
    ColRoll      = 9,
    ColPitch     = 10,
    ColYaw       = 11,
    ColPosTol    = 12,
    ColOriTol    = 13,
    ColFreeRoll  = 14,
    ColWeight    = 15,
    ColNote      = 16,
    ColStatus    = 17,
    ColReason    = 18,
    ColRawCandidates    = 19,
    ColUsableSolutions  = 20,
    ColBestQ            = 21,
    ColPositionError    = 22,
    ColOrientationError = 23,
    ColMinMargin        = 24,
    ColCondition        = 25,
    ColCollision        = 26,
    TaskPointColumnCount = 27
};

//! @brief 单行的数据 + 分析结果打包。
//!  - point:用户输入的 TaskPoint 定义;
//!  - hasResult / result:批量 IK 分析结果(可能为 disabled 或未分析);
//!  - validationWarnings:导入/编辑后的 RobotAnalysisValidation 报告。
struct TaskPointTableRow
{
    TaskPoint point;
    bool hasResult = false;
    TaskPointReachabilityResult result;
    std::vector< AnalysisWarning > validationWarnings;
};

//! @brief 真正的 QAbstractTableModel — Task point 数据源。
//!
//! 关注点分离:本类只管 rows/columns/data,UI 通过 QTableView 渲染;
//! 不再像 P2 之前那样把 setCell / cellText 散在 widget.cpp 里。
//! 上层按钮逻辑(Add/Remove/Import/Export/Analyze)通过
//! taskPoints/setRowsFromTaskPoints/insertRows/removeRows 等显式方法操作。
//!
//! editable 列:0..16(任务点定义);result 列(17..26)只读。
//! CheckStateRole 仅用于 ColEnabled。
//! BackgroundRole / ToolTipRole 用于表达 validation 错误与 result 状态。
class TaskPointTableModel : public QAbstractTableModel
{
    Q_OBJECT
  public:
    explicit TaskPointTableModel (QObject* parent = nullptr);
    ~TaskPointTableModel () override = default;

    // ---- QAbstractTableModel 接口 ----
    int rowCount (const QModelIndex& parent = QModelIndex ()) const override;
    int columnCount (const QModelIndex& parent = QModelIndex ()) const override;
    QVariant data (const QModelIndex& index, int role = Qt::DisplayRole) const override;
    bool setData (const QModelIndex& index, const QVariant& value,
                  int role = Qt::EditRole) override;
    QVariant headerData (int section, Qt::Orientation orientation,
                          int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags (const QModelIndex& index) const override;
    bool insertRows (int row, int count, const QModelIndex& parent = QModelIndex ()) override;
    bool removeRows (int row, int count, const QModelIndex& parent = QModelIndex ()) override;

    // ---- 行级数据访问 ----
    //! @brief 取所有行的 taskPoint(用于 CSV 导出、Analyze all)。
    //! 失败行通过 *error 返回简短的英文描述,行号从 1 开始。
    std::vector< TaskPoint > taskPoints (QString* error = nullptr) const;

    //! @brief 取所有行的数据 + 分析结果(供 Report 导出 / Apply best Q 等使用)。
    std::vector< TaskPointReachabilityResult > results () const;

    //! @brief 取单行(0-based row)的 TaskPoint;row 越界返回空 TaskPoint。
    TaskPoint taskPointAt (int row) const;
    TaskPointReachabilityResult resultAt (int row) const;
    bool hasResultAt (int row) const;
    const KinematicIkSolution* bestUsableSolutionForRow (int row) const;
    bool hasUsableResult (int row) const;

    //! @brief 覆盖所有行(用于 CSV 导入)。原有 _lastTaskPointResults 不动。
    void setRowsFromTaskPoints (const std::vector< TaskPoint >& points);

    //! @brief 追加一行(用于 Import current TCP)。返回新行的 row 索引。
    int appendTaskPoint (const TaskPoint& point);

    //! @brief 把单个分析结果写回对应 row(用于 analyzeSelectedTaskPointRows)。
    void setResultForRow (int row, const TaskPointReachabilityResult& result);

    //! @brief 批量覆盖所有结果(用于 Analyze all)。
    void setResults (const std::vector< TaskPointReachabilityResult >& results,
                     double reachableRate);

    //! @brief 清空所有行的结果(用于 Import / Remove 时不让旧结果污染新数据)。
    void clearAllResults ();

    //! @brief 触发所有行的 RobotAnalysisValidation,返回首条错误摘要。
    //! 失败时 *summary 包含每行首条错误;返回 true 表示无错误。
    bool validateAll (QString* summary = nullptr);

    //! @brief 清空所有行的 validation 背景与 tooltip(下一次 validateAll 之前先清)。
    void clearValidationMarks ();

    //! @brief 取所有行的 validationWarnings(供 UI 详细查看)。
    std::vector< std::vector< AnalysisWarning > > allValidationWarnings () const;

    //! @brief 暴露最近一次 validateAll 的可达率(由 setResults 内部更新)。
    double reachableRate () const { return _reachableRate; }

    //! @brief 表头文本(供 QTableView / 单元测试复用)。
    static QString headerText (int column);
    static QStringList allHeaderTexts ();

  private:
    // 内部维护的原始数据;UI 通过 QAbstractTableModel 接口读写。
    std::vector< TaskPointTableRow > _rows;
    double _reachableRate = 0.0;

    QString taskPointToString (const TaskPoint& p, int column) const;
    bool stringToTaskPointField (const QString& s, int column, TaskPoint& p) const;
    void recomputeValidation (TaskPointTableRow& row);
};

}    // namespace rws

#endif    // RWS_KINEMATICANALYSIS_TASKPOINTTABLEMODEL_HPP