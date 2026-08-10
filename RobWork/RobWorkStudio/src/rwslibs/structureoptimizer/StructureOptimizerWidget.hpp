#ifndef RWS_STRUCTUREOPTIMIZER_STRUCTUREOPTIMIZERWIDGET_HPP
#define RWS_STRUCTUREOPTIMIZER_STRUCTUREOPTIMIZERWIDGET_HPP

#include "StructureOptimizationTypes.hpp"
#include "CandidatePreviewController.hpp"
#include "RobotModelStalenessChecker.hpp"

#include <rw/kinematics/State.hpp>

#include <QByteArray>
#include <QWidget>

#include <array>

class QLabel;
class QPushButton;
class QComboBox;
class QDoubleSpinBox;
class QSpinBox;
class QTabWidget;
class QTableView;

namespace rw { namespace models { class WorkCell; } }

namespace rws {

class OptimizationTaskTableModel;
class StructureCandidateTableModel;
class StructureConstraintTableModel;
class StructureOptimizationController;
class StructureVariableTableModel;
class RobWorkStudio;

/**
 * @brief 结构优化主界面 Widget 控件类。
 * 
 * 继承自 QWidget，是 StructureOptimizerPlugin 插件的核心图形界面。
 * 负责构建多页签界面（变量、任务/约束、算法设置、候选解结果、报告导出），
 * 管理与 RobWorkStudio 宿主的无对话框项目保存事务（Project Document Provider），
 * 驱动后台异步优化控制（StructureOptimizationController），并提供候选模型的 3D 实时渲染预览[cite: 2, 19, 24, 25]。
 */
class StructureOptimizerWidget : public QWidget
{
    Q_OBJECT

public:
    /**
     * @brief 构造函数，构建 UI 控件并初始化数据模型与控制器连接。
     * @param parent 可选的 Qt 父控件指针
     */
    explicit StructureOptimizerWidget(QWidget* parent = nullptr);

    /**
     * @brief 析构函数，负责安全取消运行中的优化任务并清理预览。
     */
    ~StructureOptimizerWidget() override;

    /**
     * @brief 使用指定的结构优化问题重置 UI 界面控件与各表格模型。
     * @param problem 输入的结构优化问题结构体
     */
    void setProblem(const StructureOptimizationProblem& problem);

    /**
     * @brief 从当前 UI 界面各个页签、表格模型和输入框中收集并组装完整的问题定义。
     * @return StructureOptimizationProblem 组装完成的优化问题结构体
     */
    StructureOptimizationProblem collectProblem() const;

    /**
     * @brief 获取界面底部状态栏当前的文本内容。
     * @return QString 状态栏文本
     */
    QString statusText() const;

    /**
     * @brief 设置 WorkCell 3D 预览宿主接口 (由插件 StructureOptimizerPlugin 实现并传入)[cite: 2, 24, 25]。
     * @param host 预览宿主指针
     */
    void setPreviewHost(IWorkCellPreviewHost* host);

    /**
     * @brief 注入 RobWorkStudio 主窗口指针，用于解析项目内资源 ID 和协同对话框。
     * @param studio RobWorkStudio 实例指针
     */
    void setRobWorkStudio(RobWorkStudio* studio);

    /**
     * @brief 设置当前的场景上下文 (WorkCell 和基准关节 State)。
     * @param workcell 场景工作单元指针
     * @param state 场景基准状态
     */
    void setScenarioContext(rw::models::WorkCell* workcell,
                            const rw::kinematics::State& state);

    /**
     * @brief 清空场景上下文。
     */
    void clearScenarioContext();

    /**
     * @brief 无对话框加载项目文档 (由项目 Provider 调用)。
     * 
     * 从指定路径加载 `.structure-optimization.json` 文件并更新 UI 界面。
     * 
     * @param path 项目文件绝对路径
     * @param error [out] 可选的输出错误描述字符串
     * @param projectRoot 可选的托管工程根目录
     * @return true 加载成功；false 加载失败
     */
    bool loadProjectDocument(const QString& path, QString* error = nullptr,
                             const QString& projectRoot = QString());

    /**
     * @brief 无对话框保存项目文档 (由项目 Provider 调用)。
     * 
     * 将当前 UI 的配置序列化并保存到目标路径。
     * 
     * @param targetPath 目标保存路径
     * @param error [out] 可选的输出错误描述字符串
     * @return true 保存成功；false 保存失败
     */
    bool saveProjectDocument(const QString& targetPath, QString* error = nullptr) const;

    /**
     * @brief 判断当前项目文档是否已被修改（未保存改动）。
     * @return true 内容已被修改；false 与已保存快照一致
     */
    bool isProjectDocumentDirty() const;

    /**
     * @brief 将当前项目文档标记为干净（已保存状态，更新快照基线）。
     */
    void markProjectDocumentClean();

    /**
     * @brief 开始一个新生成的项目文档（由项目资源管理器自动创建时调用）。
     * @param path 新生成文档的路径
     */
    void beginGeneratedProjectDocument(const QString& path);

    /**
     * @brief 清空项目文档上下文（在关闭或无无保存关闭时由 Provider 触发回调）。
     * 
     * 清空优化问题、项目路径、托管工程根与快照基线，复位模型来源状态，确保新工程不继承旧会话。
     */
    void clearProjectDocumentContext();

    /**
     * @brief 检查当前是否可以关闭项目文档。
     * @param reason [out] 若不能关闭（如后台优化正在运行中），写回阻止关闭的原因
     * @return true 允许关闭；false 阻止关闭
     */
    bool canCloseProjectDocument(QString* reason = nullptr) const;

Q_SIGNALS:
    /**
     * @brief 当界面上会改变可持久化优化问题的控件/模型发生变化时触发的信号。
     * 
     * 插件捕获此信号并更新 Provider 的脏状态（Dirty Status），避免仅刷新运行进度时误标记。
     */
    void projectDocumentChanged();

private:
    // ---- 界面与逻辑内部辅助函数 ----
    void setProblemWithManagedRoot(const StructureOptimizationProblem& problem,
                                   const QString& managedProjectRoot);
    QWidget* createVariablePage();      //!< 构建页签 1: 设计变量设置页
    QWidget* createTaskPage();          //!< 构建页签 2: 任务点与约束设置页
    QWidget* createSettingsPage();      //!< 构建页签 3: 优化策略与参数设置页
    QWidget* createCandidatePage();     //!< 构建页签 4: 候选解结果列表与预览页
    QWidget* createReportPage();        //!< 构建页签 5: 项目导入导出与报告页

    void updateRunState();              //!< 动态更新“开始优化”等按钮的可点击状态及状态栏提示
    void updateModelSourceStatus();     //!< 检查磁盘模型陈旧度（Staleness）并更新状态提示
    void setEditingEnabled(bool enabled); //!< 优化运行期间禁用/恢复界面的编辑控件
    void startOptimization();           //!< 点击按钮：启动后台异步优化
    void togglePause();                 //!< 点击按钮：切换暂停/继续状态
    void cancelOptimization();          //!< 点击按钮：发送取消指令
    void handleRunningChanged(bool running); //!< 响应后台运行状态改变
    void handlePausedChanged(bool paused);   //!< 响应后台暂停状态改变
    void handleProgress(const StructureProgress& progress); //!< 响应后台进度更新，刷新进度标签
    void handleCompleted(const StructureOptimizationResult& result); //!< 响应后台完成回调，更新候选解表格
    void handleFailed(const QString& message); //!< 响应后台异常失败回调
    void previewSelectedCandidate();    //!< 预览当前选中的可行候选解 3D 视图
    void clearCandidatePreview();       //!< 清除 3D 预览，恢复原始 WorkCell
    void newProjectFromModelSpec();     //!< 从模型快照 JSON 创建新项目
    void newProjectFromFrozenRequirements(); //!< 从冻结工程需求 JSON 创建新项目
    void openProject();                 //!< 手动导入导出项目 JSON
    void saveProject();                 //!< 手动导出项目 JSON
    void exportResult();                //!< 一站式导出优化报告及最佳模型文件[cite: 13, 25]
    void addTask();                     //!< 添加新任务点
    void duplicateSelectedTask();       //!< 复制选中的任务点
    void removeSelectedTask();          //!< 删除选中的任务点
    void addConstraint();               //!< 添加新约束条件
    void duplicateSelectedConstraint(); //!< 复制选中的约束条件
    void removeSelectedConstraint();    //!< 删除选中的约束条件
    int selectedCandidateIndex() const; //!< 获取候选解列表中当前选中行的候选索引

    // ---- 底层数据与控制对象指针 ----
    StructureOptimizationProblem _loadedProblem;                     //!< 当前加载的优化问题容器
    StructureVariableTableModel* _variableModel = nullptr;          //!< 设计变量表格 MVC 模型
    OptimizationTaskTableModel* _taskModel = nullptr;               //!< 任务点表格 MVC 模型
    StructureConstraintTableModel* _constraintModel = nullptr;      //!< 约束条件表格 MVC 模型
    StructureCandidateTableModel* _candidateModel = nullptr;        //!< 候选解结果表格 MVC 模型
    StructureOptimizationController* _controller = nullptr;         //!< 后台异步优化控制器
    std::unique_ptr<CandidatePreviewController> _previewController; //!< 候选模型 3D 渲染预览控制器
    StructureOptimizationResult _lastResult;                        //!< 上一次优化运行的最终结果
    QString _projectPath;                                           //!< 当前项目文件路径
    QString _managedProjectRoot;                                    //!< 托管工程的根目录
    QString _projectDocumentPath;                                   //!< 关联的项目资源文档路径
    QByteArray _savedProjectDocumentSnapshot;                       //!< 已保存的项目文档 JSON 规范快照，用于对比脏状态
    mutable QByteArray _pendingProjectDocumentSnapshot;             //!< 暂存的项目文档快照
    RobotModelSourceStatus _modelSourceStatus = RobotModelSourceStatus::Untracked; //!< 源模型陈旧度校验状态
    rw::models::WorkCell* _scenarioWorkCell = nullptr;               //!< 宿主场景 WorkCell 指针
    rw::kinematics::State _scenarioState;                           //!< 宿主场景 State 对象
    RobWorkStudio* _studio = nullptr;                               //!< 宿主 RobWorkStudio 实例指针

    // ---- GUI 图形控件指针 ----
    QTabWidget* _tabs = nullptr;                       //!< 五页签的主 Tab 控件
    QTableView* _taskView = nullptr;                   //!< 任务点表格视图
    QTableView* _constraintView = nullptr;             //!< 约束条件表格视图
    QTableView* _candidateView = nullptr;              //!< 候选解结果表格视图
    QPushButton* _startButton = nullptr;               //!< 开始优化按钮
    QPushButton* _pauseButton = nullptr;               //!< 暂停/继续按钮
    QPushButton* _cancelButton = nullptr;              //!< 取消优化按钮
    QLabel* _statusLabel = nullptr;                    //!< 底部状态栏信息标签
    QLabel* _progressLabel = nullptr;                  //!< 优化阶段与进度标签
    QSpinBox* _candidateCountSpin = nullptr;           //!< 候选解总数输入框
    QSpinBox* _eliteCountSpin = nullptr;               //!< 精英候选解数量输入框
    QSpinBox* _localEliteCountSpin = nullptr;          //!< 局部搜索精英解数量输入框
    QSpinBox* _finalVerificationCountSpin = nullptr;   //!< 最终精评复核解数量输入框
    QSpinBox* _maxLocalSweepsSpin = nullptr;           //!< 局部精细搜索扫掠次数输入框
    QSpinBox* _gridStepsSpin = nullptr;                //!< 网格遍历步数输入框
    QSpinBox* _seedSpin = nullptr;                     //!< 随机种子输入框
    QComboBox* _strategyCombo = nullptr;               //!< 寻优策略选择下拉框 (Hybrid/Random/Grid)
    QComboBox* _newConstraintKindCombo = nullptr;      //!< 新建约束类型选择下拉框
    std::array<QDoubleSpinBox*, 6> _weightSpins = {{nullptr, nullptr, nullptr,
                                                    nullptr, nullptr, nullptr}}; //!< 6 多目标权重输入框数组 (可达性、可操作度等)
};

} // namespace rws

#endif // RWS_STRUCTUREOPTIMIZER_STRUCTUREOPTIMIZERWIDGET_HPP