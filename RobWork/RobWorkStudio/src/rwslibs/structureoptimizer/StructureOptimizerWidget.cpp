#include "StructureOptimizerWidget.hpp"

#include "StructureOptimizationObjectiveProfile.hpp"

#include "OptimizationTaskTableModel.hpp"
#include "StructureCandidateTableModel.hpp"
#include "StructureConstraintTableModel.hpp"
#include "StructureOptimizationController.hpp"
#include "StructureOptimizationExportService.hpp"
#include "StructureOptimizationProjectAdapter.hpp"
#include "StructureOptimizationProjectFactory.hpp"
#include "FrozenRequirementProjectImportService.hpp"
#include "StructureOptimizationUiLogic.hpp"
#include "StructureOptimizationTemplate.hpp"
#include "StructureCandidateComparison.hpp"
#include "StructureVariableFilterProxyModel.hpp"
#include "StructureVariableTableModel.hpp"

#include <rwslibs/engineeringrequirements/RequirementFreezer.hpp>
#include <rwslibs/robotmodelbuilder/RobotModelSpecJson.hpp>
#include <rws/RobWorkStudio.hpp>

#include <QComboBox>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QDir>
#include <QFormLayout>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableView>
#include <QMenu>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <optional>
#include <sstream>

using namespace rws;

namespace {

QTableView* makeTableView(QAbstractItemModel* model, const QString& objectName)
{
    QTableView* view = new QTableView();
    view->setObjectName(objectName);
    view->setModel(model);
    view->horizontalHeader()->setStretchLastSection(true);
    view->setSelectionBehavior(QAbstractItemView::SelectRows);
    return view;
}

QSpinBox* makeSpinBox(int minimum, int maximum, int value)
{
    QSpinBox* spinBox = new QSpinBox();
    spinBox->setRange(minimum, maximum);
    spinBox->setValue(value);
    return spinBox;
}

std::string uniqueId(const std::string& prefix, const std::vector<std::string>& existing)
{
    int suffix = 1;
    while (true) {
        const std::string candidate = prefix + "_" + std::to_string(suffix++);
        if (std::find(existing.begin(), existing.end(), candidate) == existing.end())
            return candidate;
    }
}

QString constraintKindLabel(StructureConstraintKind kind)
{
    switch (kind) {
    case StructureConstraintKind::ModelValid: return "Model Valid";
    case StructureConstraintKind::RequiredTaskReachable: return "Required Tasks Reachable";
    case StructureConstraintKind::RequiredTaskCollisionFree: return "Required Tasks Collision-Free";
    case StructureConstraintKind::MinimumJointMargin: return "Minimum Joint Margin";
    case StructureConstraintKind::MaximumTotalLength: return "Maximum Total Length";
    case StructureConstraintKind::MaximumBaseHeight: return "Maximum Base Height";
    case StructureConstraintKind::MaximumCrossSection: return "Maximum Cross-Section";
    case StructureConstraintKind::MaximumLinkSlenderness: return "Maximum Link Slenderness";
    case StructureConstraintKind::MinimumWorkspaceCoverage: return "Minimum Workspace Coverage";
    }
    return QString();
}

QString variableKindLabel(StructureVariableKind kind)
{
    switch (kind) {
        case StructureVariableKind::JointPositionX: return "JointPositionX";
        case StructureVariableKind::JointPositionY: return "JointPositionY";
        case StructureVariableKind::JointPositionZ: return "JointPositionZ";
        case StructureVariableKind::JointRotationRoll: return "JointRotationRoll";
        case StructureVariableKind::JointRotationPitch: return "JointRotationPitch";
        case StructureVariableKind::JointRotationYaw: return "JointRotationYaw";
        case StructureVariableKind::DhA: return "DhA";
        case StructureVariableKind::DhD: return "DhD";
        case StructureVariableKind::BaseHeight: return "BaseHeight";
        case StructureVariableKind::TcpOffsetX: return "TcpOffsetX";
        case StructureVariableKind::TcpOffsetY: return "TcpOffsetY";
        case StructureVariableKind::TcpOffsetZ: return "TcpOffsetZ";
        case StructureVariableKind::LinkRadius: return "LinkRadius";
    case StructureVariableKind::LinkWidth: return "LinkWidth";
    case StructureVariableKind::LinkHeight: return "LinkHeight";
    case StructureVariableKind::LinkDimensionX: return "LinkDimensionX";
    case StructureVariableKind::LinkDimensionY: return "LinkDimensionY";
    case StructureVariableKind::LinkDimensionZ: return "LinkDimensionZ";
    }
    return "Unknown";
}

StructureConstraint makeDefaultConstraint(StructureConstraintKind kind,
                                          const std::string& id)
{
    StructureConstraint constraint;
    constraint.id = id;
    constraint.label = constraintKindLabel(kind).toStdString();
    constraint.kind = kind;
    constraint.enabled = true;
    constraint.hard = true;
    switch (kind) {
    case StructureConstraintKind::RequiredTaskCollisionFree:
        constraint.threshold = 1.0;
        break;
    case StructureConstraintKind::MinimumJointMargin:
        constraint.threshold = 0.01;
        break;
    case StructureConstraintKind::MaximumTotalLength:
    case StructureConstraintKind::MaximumBaseHeight:
    case StructureConstraintKind::MaximumCrossSection:
    case StructureConstraintKind::MaximumLinkSlenderness:
    case StructureConstraintKind::MinimumWorkspaceCoverage:
        // These limits have no model-independent engineering default.
        constraint.enabled = false;
        break;
    case StructureConstraintKind::ModelValid:
    case StructureConstraintKind::RequiredTaskReachable:
        break;
    }
    return constraint;
}

} // namespace

StructureOptimizerWidget::StructureOptimizerWidget(QWidget* parent)
    : QWidget(parent),
      _variableModel(new StructureVariableTableModel(this)),
      _variableFilterModel(new StructureVariableFilterProxyModel(this)),
      _taskModel(new OptimizationTaskTableModel(this)),
      _constraintModel(new StructureConstraintTableModel(this)),
      _candidateModel(new StructureCandidateTableModel(this)),
      _controller(new StructureOptimizationController(this))
{
    _variableFilterModel->setSourceModel(_variableModel);
    _tabs = new QTabWidget(this);
    _tabs->setObjectName("structureOptimizerTabs");
    _tabs->addTab(createVariablePage(), "Design Variables");
    _tabs->addTab(createTaskPage(), "Tasks & Constraints");
    _tabs->addTab(createSettingsPage(), "Optimization Settings");
    _tabs->addTab(createCandidatePage(), "Candidates");
    _tabs->addTab(createReportPage(), "Export Report");

    _startButton = new QPushButton("Start Optimization", this);
    _startButton->setObjectName("startOptimizationButton");
    _pauseButton = new QPushButton("Pause", this);
    _pauseButton->setObjectName("pauseOptimizationButton");
    _cancelButton = new QPushButton("Cancel", this);
    _cancelButton->setObjectName("cancelOptimizationButton");
    _statusLabel = new QLabel("Load or create an optimization project.", this);
    _statusLabel->setObjectName("structureOptimizationStatusLabel");
    _progressLabel = new QLabel("Not started", this);
    _progressLabel->setObjectName("structureOptimizationProgressLabel");

    _templateCombo = new QComboBox(this);
    _templateCombo->setObjectName("structureOptimizationTemplateCombo");
    for (const StructureOptimizationTemplateInfo& info : StructureOptimizationTemplate::available())
        _templateCombo->addItem(QString::fromStdString(info.label),
                                static_cast<int>(info.kind));
    _applyTemplateButton = new QPushButton("Apply Template", this);
    _applyTemplateButton->setObjectName("applyStructureOptimizationTemplateButton");
    _preflightButton = new QPushButton("Preflight", this);
    _preflightButton->setObjectName("preflightStructureOptimizationButton");
    _baselineButton = new QPushButton("Evaluate Baseline", this);
    _baselineButton->setObjectName("evaluateStructureBaselineButton");
    _compareButton = new QPushButton("Compare Selected", this);
    _compareButton->setObjectName("compareStructureCandidatesButton");
    _preflightLabel = new QLabel("Preflight not run.", this);
    _preflightLabel->setObjectName("structureOptimizationPreflightLabel");
    _baselineLabel = new QLabel("Baseline not evaluated.", this);
    _baselineLabel->setObjectName("structureOptimizationBaselineLabel");
    _comparisonLabel = new QLabel("Select candidates to compare.", this);
    _comparisonLabel->setObjectName("structureCandidateComparisonLabel");
    for (QLabel* label : {_preflightLabel, _baselineLabel, _comparisonLabel})
        label->setWordWrap(true);

    QFrame* modelStatusBanner = new QFrame(this);
    modelStatusBanner->setObjectName("structureModelStatusBanner");
    modelStatusBanner->setFrameShape(QFrame::StyledPanel);
    QVBoxLayout* modelStatusLayout = new QVBoxLayout(modelStatusBanner);
    _modelStatusBannerText = new QLabel(modelStatusBanner);
    _modelStatusBannerText->setObjectName("structureModelStatusBannerText");
    _modelStatusBannerText->setWordWrap(true);
    _modelStatusBannerSource = new QLabel(modelStatusBanner);
    _modelStatusBannerSource->setObjectName("structureModelStatusBannerSource");
    _modelStatusBannerSource->setWordWrap(true);
    QToolButton* modelStatusDetails = new QToolButton(modelStatusBanner);
    modelStatusDetails->setObjectName("structureModelStatusBannerDetailsButton");
    modelStatusDetails->setText("Details");
    modelStatusDetails->setCheckable(true);
    _newProjectFromModelBannerButton =
        new QPushButton("New Project from Model Snapshot", this);
    _newProjectFromModelBannerButton->setObjectName(
        "newStructureOptimizationProjectFromModelBannerButton");
    _newProjectFromFrozenRequirementBannerButton =
        new QPushButton("New Project from Frozen Requirements", this);
    _newProjectFromFrozenRequirementBannerButton->setObjectName(
        "newStructureOptimizationProjectFromFrozenRequirementBannerButton");
    _modelStatusBannerSource->setVisible(false);
    _modelStatusBannerSource->setToolTip("The absolute path of the tracked model source.");
    modelStatusLayout->addWidget(_modelStatusBannerText);
    modelStatusLayout->addWidget(modelStatusDetails, 0, Qt::AlignLeft);
    modelStatusLayout->addWidget(_modelStatusBannerSource);
    modelStatusBanner->hide();
    _modelStatusBanner = modelStatusBanner;

    QHBoxLayout* projectToolbar = new QHBoxLayout();
    projectToolbar->setObjectName("structureProjectToolbar");
    projectToolbar->addWidget(_newProjectFromModelBannerButton);
    projectToolbar->addWidget(_newProjectFromFrozenRequirementBannerButton);
    projectToolbar->addStretch();

    QHBoxLayout* workflowToolbar = new QHBoxLayout();
    workflowToolbar->setObjectName("structureOptimizationWorkflowToolbar");
    workflowToolbar->addWidget(new QLabel("Template", this));
    workflowToolbar->addWidget(_templateCombo);
    workflowToolbar->addWidget(_applyTemplateButton);
    workflowToolbar->addWidget(_preflightButton);
    workflowToolbar->addWidget(_baselineButton);
    workflowToolbar->addWidget(_compareButton);
    workflowToolbar->addStretch();

    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->addWidget(_startButton);
    buttonLayout->addWidget(_pauseButton);
    buttonLayout->addWidget(_cancelButton);
    buttonLayout->addStretch();
    buttonLayout->addWidget(_progressLabel);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->addLayout(projectToolbar);
    mainLayout->addLayout(workflowToolbar);
    mainLayout->addWidget(_preflightLabel);
    mainLayout->addWidget(_baselineLabel);
    mainLayout->addWidget(_comparisonLabel);
    mainLayout->addWidget(_modelStatusBanner);
    mainLayout->addWidget(_tabs);
    mainLayout->addLayout(buttonLayout);
    mainLayout->addWidget(_statusLabel);
    setLayout(mainLayout);

    connect(_startButton, &QPushButton::clicked,
            this, &StructureOptimizerWidget::startOptimization);
    connect(_pauseButton, &QPushButton::clicked,
            this, &StructureOptimizerWidget::togglePause);
    connect(_cancelButton, &QPushButton::clicked,
            this, &StructureOptimizerWidget::cancelOptimization);
    connect(_applyTemplateButton, &QPushButton::clicked,
            this, &StructureOptimizerWidget::applyOptimizationTemplate);
    connect(_preflightButton, &QPushButton::clicked,
            this, &StructureOptimizerWidget::runStructurePreflight);
    connect(_baselineButton, &QPushButton::clicked,
            this, &StructureOptimizerWidget::evaluateStructureBaseline);
    connect(_compareButton, &QPushButton::clicked,
            this, &StructureOptimizerWidget::compareStructureCandidates);
    connect(_newProjectFromModelBannerButton, &QPushButton::clicked,
            this, &StructureOptimizerWidget::newProjectFromModelSpec);
    connect(_newProjectFromFrozenRequirementBannerButton, &QPushButton::clicked,
            this, &StructureOptimizerWidget::newProjectFromFrozenRequirements);
    connect(modelStatusDetails, &QToolButton::toggled,
            _modelStatusBannerSource, &QLabel::setVisible);
    connect(_variableModel, &QAbstractItemModel::dataChanged,
            this, &StructureOptimizerWidget::updateRunState);
    connect(_variableModel, &StructureVariableTableModel::editRejected, this,
            [this](const QString& message) { _statusLabel->setText(message); });
    connect(_taskModel, &QAbstractItemModel::dataChanged,
            this, &StructureOptimizerWidget::updateRunState);
    connect(_constraintModel, &QAbstractItemModel::dataChanged,
            this, &StructureOptimizerWidget::updateRunState);
    connect(_constraintModel, &StructureConstraintTableModel::editRejected, this,
            [this](const QString& message) { _statusLabel->setText(message); });

    // 表格模型的行增删和单元格编辑都会改变项目问题。保留原有 updateRunState 连接，
    // 并额外集中发出文档通知，交由插件进行快照比较而不是盲目置脏。
    const auto notifyProjectDocumentChanged = [this]() { Q_EMIT projectDocumentChanged(); };
    const std::array<QAbstractItemModel*, 3> projectModels = {{
        static_cast<QAbstractItemModel*>(_variableModel),
        static_cast<QAbstractItemModel*>(_taskModel),
        static_cast<QAbstractItemModel*>(_constraintModel)}};
    for (QAbstractItemModel* model : projectModels) {
        connect(model, &QAbstractItemModel::dataChanged, this, notifyProjectDocumentChanged);
        connect(model, &QAbstractItemModel::modelReset, this, notifyProjectDocumentChanged);
        connect(model, &QAbstractItemModel::rowsInserted, this,
                [notifyProjectDocumentChanged](const QModelIndex&, int, int) {
                    notifyProjectDocumentChanged();
                });
        connect(model, &QAbstractItemModel::rowsRemoved, this,
                [notifyProjectDocumentChanged](const QModelIndex&, int, int) {
                    notifyProjectDocumentChanged();
                });
    }
    for (QSpinBox* spin : {_candidateCountSpin, _eliteCountSpin, _localEliteCountSpin,
                           _finalVerificationCountSpin, _maxLocalSweepsSpin,
                           _gridStepsSpin, _seedSpin}) {
        connect(spin, QOverload<int>::of(&QSpinBox::valueChanged),
                this, &StructureOptimizerWidget::updateRunState);
        connect(spin, QOverload<int>::of(&QSpinBox::valueChanged),
                this, notifyProjectDocumentChanged);
    }
    connect(_strategyCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &StructureOptimizerWidget::updateRunState);
    connect(_strategyCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, notifyProjectDocumentChanged);
    for (QDoubleSpinBox* weight : _weightSpins) {
        connect(weight, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                this, &StructureOptimizerWidget::updateRunState);
        connect(weight, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                this, notifyProjectDocumentChanged);
    }

    connect(_controller, &StructureOptimizationController::runningChanged,
            this, &StructureOptimizerWidget::handleRunningChanged);
    connect(_controller, &StructureOptimizationController::pausedChanged,
            this, &StructureOptimizerWidget::handlePausedChanged);
    connect(_controller, &StructureOptimizationController::progressChanged,
            this, &StructureOptimizerWidget::handleProgress);
    connect(_controller, &StructureOptimizationController::completed,
            this, &StructureOptimizerWidget::handleCompleted);
    connect(_controller, &StructureOptimizationController::failed,
            this, &StructureOptimizerWidget::handleFailed);
    connect(_controller, &StructureOptimizationController::baselineCompleted,
            this, &StructureOptimizerWidget::handleBaselineCompleted);
    connect(_controller, &StructureOptimizationController::baselineFailed,
            this, &StructureOptimizerWidget::handleBaselineFailed);
    connect(_controller, &StructureOptimizationController::baselineRunningChanged,
            this, &StructureOptimizerWidget::handleBaselineRunningChanged);

    updateModelSourceStatus();
    updateRunState();
}

StructureOptimizerWidget::~StructureOptimizerWidget()
{
    _controller->cancel();
    clearCandidatePreview();
}

void StructureOptimizerWidget::setPreviewHost(IWorkCellPreviewHost* host)
{
    _previewController.reset(host != nullptr ? new CandidatePreviewController(host) : nullptr);
}

// 由插件在 initialize 注入宿主主窗口，用于按资源 ID 解析项目内需求文件（而非扫描目录）。
void StructureOptimizerWidget::setRobWorkStudio(RobWorkStudio* studio)
{
    _studio = studio;
}

void StructureOptimizerWidget::setScenarioContext(
    rw::models::WorkCell* workcell, const rw::kinematics::State& state)
{
    _scenarioWorkCell = workcell;
    _scenarioState = state;
}

void StructureOptimizerWidget::clearScenarioContext()
{
    _scenarioWorkCell = nullptr;
    _scenarioState = rw::kinematics::State();
}

void StructureOptimizerWidget::setProblem(
    const StructureOptimizationProblem& problem)
{
    setProblemWithManagedRoot(problem, QString());
}

void StructureOptimizerWidget::setProblemWithManagedRoot(
    const StructureOptimizationProblem& problem, const QString& managedProjectRoot)
{
    // 项目会话边界(C2)：setProblem/New/Open/Close/导入全部汇聚于此。先取消
    // 在途运行并递增 projectEpoch，让旧会话的完成/进度事件在控制器内被丢弃，
    // 绝不写入新项目的候选表、结果缓存或状态栏。禁用按钮只是辅助防护。
    if (_controller != nullptr) {
        _controller->cancel();
        _controller->notifyProjectSessionChanged();
    }
    _managedProjectRoot = managedProjectRoot.trimmed().isEmpty()
        ? QString() : QFileInfo(managedProjectRoot).absoluteFilePath();
    _loadedProblem = problem;
    if (_loadedProblem.variables.empty())
        _loadedProblem.variables =
            StructureOptimizationUiLogic::suggestVariables(_loadedProblem.context);

    // M4: 抑制旧项目里与 TcpOffset* 同字段冲突的 JointPosition* 绑定。
    StructureOptimizationUiLogic::disableShadowedLegacyTcpDuplicates(
        _loadedProblem.variables);
    // M3: 按建议 id 后缀修正旧维度变量的错标 kind。
    StructureOptimizationUiLogic::migrateLegacyDrawableDimensionKinds(
        _loadedProblem.variables);

    // C1.1/D1: 冻结参考指纹持久化在契约 extensions 中，随 _loadedProblem 载入；
    // staleness 判定完全交给共享的 frozenContractStale(problem)，此处无需缓存。

    _variableModel->setVariables(_loadedProblem.variables);
    _taskModel->setTasks(_loadedProblem.tasks);
    _constraintModel->setConstraints(_loadedProblem.constraints);
    _candidateModel->setCandidates({});
    _lastResult = StructureOptimizationResult();
    _baselineResult = StructureOptimizationResult();
    _baselineOnlyRunning = false;
    // M16: 结果已清空，其运行时快照一并作废。
    _hasLastRunProblem = false;
    _lastRunVariableSchemaFingerprint.clear();
    if (_preflightLabel != nullptr)
        _preflightLabel->setText("Preflight not run.");
    if (_baselineLabel != nullptr)
        _baselineLabel->setText("Baseline not evaluated.");
    if (_comparisonLabel != nullptr)
        _comparisonLabel->setText("Select candidates to compare.");

    _candidateCountSpin->setValue(_loadedProblem.run.candidateCount);
    _eliteCountSpin->setValue(_loadedProblem.run.eliteCount);
    _localEliteCountSpin->setValue(_loadedProblem.run.localEliteCount);
    _finalVerificationCountSpin->setValue(_loadedProblem.run.finalVerificationCount);
    _maxLocalSweepsSpin->setValue(_loadedProblem.run.maxLocalSweeps);
    _gridStepsSpin->setValue(_loadedProblem.run.gridSteps);
    _seedSpin->setValue(static_cast<int>(_loadedProblem.run.randomSeed));
    const int strategyIndex = _strategyCombo->findData(static_cast<int>(_loadedProblem.run.strategy));
    _strategyCombo->setCurrentIndex(strategyIndex >= 0 ? strategyIndex : 0);
    const std::array<double, 6> weights = {{
        _loadedProblem.weights.reachability,
        _loadedProblem.weights.manipulability,
        _loadedProblem.weights.jointMargin,
        _loadedProblem.weights.collision,
        _loadedProblem.weights.compactness,
        _loadedProblem.weights.preference}};
    for (std::size_t i = 0; i < _weightSpins.size(); ++i)
        _weightSpins[i]->setValue(weights[i]);

    updateModelSourceStatus();
    updateRunState();
}

StructureOptimizationProblem StructureOptimizerWidget::collectProblem() const
{
    StructureOptimizationProblem problem = _loadedProblem;
    problem.variables = _variableModel->variables();
    problem.tasks = _taskModel->tasks();
    problem.constraints = _constraintModel->constraints();
    problem.run.strategy = static_cast<StructureStrategyKind>(
        _strategyCombo->currentData().toInt());
    problem.run.candidateCount = _candidateCountSpin->value();
    problem.run.eliteCount = _eliteCountSpin->value();
    problem.run.localEliteCount = _localEliteCountSpin->value();
    problem.run.finalVerificationCount = _finalVerificationCountSpin->value();
    problem.run.maxLocalSweeps = _maxLocalSweepsSpin->value();
    problem.run.gridSteps = _gridStepsSpin->value();
    problem.run.randomSeed = static_cast<unsigned int>(_seedSpin->value());
    problem.weights.reachability = _weightSpins[0]->value();
    problem.weights.manipulability = _weightSpins[1]->value();
    problem.weights.jointMargin = _weightSpins[2]->value();
    problem.weights.collision = _weightSpins[3]->value();
    problem.weights.compactness = _weightSpins[4]->value();
    problem.weights.preference = _weightSpins[5]->value();
    if (problem.objectives.empty() ||
        StructureOptimizationObjectiveProfile::isLegacyProfile(problem.objectives))
        problem.objectives =
            StructureOptimizationObjectiveProfile::legacyObjectives(problem.weights);
    return problem;
}

bool StructureOptimizerWidget::loadProjectDocument(const QString& path, QString* error,
                                                    const QString& projectRoot)
{
    StructureOptimizationProblem problem;
    int selectedCandidate = -1;
    if (!StructureOptimizationProjectAdapter::loadProject(
            path, problem, &selectedCandidate, error, projectRoot))
        return false;

    // setProblem 会复用现有模型和控件填充逻辑；随后才建立 rwproj 资源快照，确保
    // 初始化信号不会把刚刚加载的数据误标记为未保存修改。
    _projectPath = path;
    _projectDocumentPath = path;
    setProblemWithManagedRoot(problem, projectRoot);
    _savedProjectDocumentSnapshot.clear();
    _pendingProjectDocumentSnapshot.clear();
    QByteArray snapshot;
    if (!StructureOptimizationProjectAdapter::serializeProject(
            path, collectProblem(), selectedCandidateIndex(), snapshot, error))
        return false;
    _savedProjectDocumentSnapshot = snapshot;
    if (error != nullptr)
        error->clear();
    return true;
}

bool StructureOptimizerWidget::saveProjectDocument(const QString& targetPath, QString* error) const
{
    // C1.1/D1 修订：stale 项目允许保存草稿——契约 extensions 中的持久化参考
    // 指纹随 _loadedProblem 原样保留，重载后仍判定 stale，不存在持久化旁路。
    const int selectedCandidate = selectedCandidateIndex();
    if (!StructureOptimizationProjectAdapter::saveProject(
            targetPath, collectProblem(), selectedCandidate, error))
        return false;

    // 保存事务尚未完成，故只缓存候选基线；Provider 仅在所有资源提交成功后才会调用
    // markProjectDocumentClean()，从而让失败回滚保持原有脏状态。
    if (!StructureOptimizationProjectAdapter::serializeProject(
            targetPath, collectProblem(), selectedCandidate, _pendingProjectDocumentSnapshot, error))
        return false;
    return true;
}

bool StructureOptimizerWidget::isProjectDocumentDirty() const
{
    if (_projectDocumentPath.isEmpty())
        return false;
    QByteArray current;
    QString error;
    // 序列化异常时宁可报告脏状态，禁止把无法完整表示的编辑内容当作已安全保存。
    if (!StructureOptimizationProjectAdapter::serializeProject(
            _projectDocumentPath, collectProblem(), selectedCandidateIndex(), current, &error))
        return true;
    return current != _savedProjectDocumentSnapshot;
}

// 仅由 Provider 的 markClean 回调调用（全部资源提交成功后）：把保存事务暂存阶段
// 缓存的新基线提升为已保存基线；若没有暂存过（直接打开未改），基线保持不变。
void StructureOptimizerWidget::beginGeneratedProjectDocument(const QString& path)
{
    _projectDocumentPath = path;
    _savedProjectDocumentSnapshot.clear();
    _pendingProjectDocumentSnapshot.clear();
}

void StructureOptimizerWidget::markProjectDocumentClean()
{
    if (!_pendingProjectDocumentSnapshot.isEmpty()) {
        _savedProjectDocumentSnapshot = _pendingProjectDocumentSnapshot;
        _pendingProjectDocumentSnapshot.clear();
    }
}

void StructureOptimizerWidget::clearProjectDocumentContext()
{
    // 项目资源关闭回调执行完整重置:清空项目路径、文档路径与托管工程根目录,
    // 作废保存/待保存快照,并把模型来源状态复位为"未跟踪"。
    _projectPath.clear();
    _projectDocumentPath.clear();
    _managedProjectRoot.clear();
    _savedProjectDocumentSnapshot.clear();
    _pendingProjectDocumentSnapshot.clear();
    _modelSourceStatus = RobotModelSourceStatus::Untracked;
    // 以空优化问题连同空托管根重建状态,确保新工程不继承上一项目的优化会话。
    setProblemWithManagedRoot(StructureOptimizationProblem(), QString());
}

bool StructureOptimizerWidget::canCloseProjectDocument(QString* reason) const
{
    if (_controller != nullptr &&
        (_controller->isRunning() || _controller->isBaselineRunning())) {
        if (reason != nullptr)
            *reason = QStringLiteral("Optimization or baseline evaluation is still running. Cancel it or wait for completion.");
        return false;
    }
    if (reason != nullptr)
        reason->clear();
    return true;
}

bool StructureOptimizerWidget::isFrozenContractStale() const
{
    return StructureOptimizationUiLogic::frozenContractStale(collectProblem());
}

// 读取候选列表中当前选中项的索引；未选中或视图无效时返回 -1（表示不导出候选）。
int StructureOptimizerWidget::selectedCandidateIndex() const
{
    if (_candidateView == nullptr || !_candidateView->currentIndex().isValid())
        return -1;
    return _candidateView->currentIndex().siblingAtColumn(
        StructureCandidateTableModel::IndexColumn).data().toInt();
}

QString StructureOptimizerWidget::statusText() const
{
    return _statusLabel != nullptr ? _statusLabel->text() : QString();
}

QWidget* StructureOptimizerWidget::createVariablePage()
{
    QWidget* page = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(page);
    _variableView = makeTableView(_variableFilterModel, "structureVariableTable");
    _variableView->setAlternatingRowColors(true);
    _variableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    _variableView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    // 允许用户排序展示行；所有编辑/删除仍通过代理到源模型映射完成。
    _variableView->setSortingEnabled(true);
    _variableView->sortByColumn(StructureVariableTableModel::IdColumn, Qt::AscendingOrder);
    _variableView->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    QHeaderView* header = _variableView->horizontalHeader();
    header->setStretchLastSection(false);
    for (const int column : {StructureVariableTableModel::IdColumn,
                             StructureVariableTableModel::LabelColumn,
                             StructureVariableTableModel::TargetColumn,
                             StructureVariableTableModel::KindColumn}) {
        header->setSectionResizeMode(column, QHeaderView::Interactive);
    }
    for (const int column : {StructureVariableTableModel::CurrentColumn,
                             StructureVariableTableModel::MinimumColumn,
                             StructureVariableTableModel::MaximumColumn,
                             StructureVariableTableModel::StepColumn,
                             StructureVariableTableModel::PreferredColumn,
                             StructureVariableTableModel::PreferenceWeightColumn})
        header->setSectionResizeMode(column, QHeaderView::Stretch);
    header->setSectionResizeMode(StructureVariableTableModel::EnabledColumn,
                                 QHeaderView::Fixed);
    _variableView->setColumnWidth(StructureVariableTableModel::EnabledColumn, 56);

    _variableView->setColumnWidth(StructureVariableTableModel::IdColumn, 140);
    _variableView->setColumnWidth(StructureVariableTableModel::LabelColumn, 180);
    _variableView->setColumnWidth(StructureVariableTableModel::TargetColumn, 160);
    _variableView->setColumnWidth(StructureVariableTableModel::KindColumn, 160);
    _variableView->setColumnHidden(StructureVariableTableModel::PreferredColumn, true);
    _variableView->setColumnHidden(StructureVariableTableModel::PreferenceWeightColumn, true);
    QHBoxLayout* variableActions = new QHBoxLayout();
    _variableSearch = new QLineEdit(page);
    _variableSearch->setObjectName("structureVariableSearch");
    _variableSearch->setPlaceholderText("Search variables");
    _variableTypeFilter = new QComboBox(page);
    _variableTypeFilter->setObjectName("structureVariableTypeFilter");
    _variableTypeFilter->addItem("All Types");
    for (int kind = static_cast<int>(StructureVariableKind::JointPositionX);
         kind <= static_cast<int>(StructureVariableKind::LinkDimensionZ); ++kind) {
        const StructureVariableKind value = static_cast<StructureVariableKind>(kind);
        _variableTypeFilter->addItem(variableKindLabel(value), kind);
    }
    _showVariableAdvanced = new QCheckBox("Show Advanced", page);
    _showVariableAdvanced->setObjectName("showStructureVariableAdvanced");
    _addMissingSuggestionsButton = new QPushButton("Add Missing Suggestions", page);
    _addMissingSuggestionsButton->setObjectName("addMissingStructureVariablesButton");
    _addVariableButton = new QPushButton("Add Variable", page);
    _addVariableButton->setObjectName("addStructureVariableButton");
    _duplicateVariableButton = new QPushButton("Duplicate Selected", page);
    _duplicateVariableButton->setObjectName("duplicateStructureVariableButton");
    _removeVariablesButton = new QPushButton("Remove Selected", page);
    _removeVariablesButton->setObjectName("removeStructureVariablesButton");
    _restoreVariableBaselineButton = new QPushButton("Restore Model Baseline", page);
    _restoreVariableBaselineButton->setObjectName("restoreStructureVariableBaselineButton");
    _variableSearch->setMinimumWidth(180);
    _variableTypeFilter->setMinimumWidth(130);
    QToolButton* variableMore = new QToolButton(page);
    variableMore->setObjectName("structureVariableMoreButton");
    variableMore->setText("More");
    variableMore->setPopupMode(QToolButton::InstantPopup);
    QMenu* variableMenu = new QMenu(variableMore);
    _addMissingSuggestionsAction = variableMenu->addAction(
        "Add Missing Suggestions", _addMissingSuggestionsButton, &QPushButton::click);
    _restoreVariableBaselineAction = variableMenu->addAction(
        "Restore Model Baseline", _restoreVariableBaselineButton, &QPushButton::click);
    variableMore->setMenu(variableMenu);
    variableActions->addWidget(_variableSearch);
    variableActions->addWidget(_variableTypeFilter);
    variableActions->addWidget(_showVariableAdvanced);
    variableActions->addWidget(_addVariableButton);
    variableActions->addWidget(_duplicateVariableButton);
    variableActions->addWidget(_removeVariablesButton);
    variableActions->addWidget(variableMore);
    variableActions->addStretch();
    _addMissingSuggestionsButton->setVisible(false);
    _restoreVariableBaselineButton->setVisible(false);
    layout->addLayout(variableActions);
    layout->addWidget(_variableView);

    connect(_addVariableButton, &QPushButton::clicked,
            this, &StructureOptimizerWidget::addVariable);
    connect(_addMissingSuggestionsButton, &QPushButton::clicked,
            this, &StructureOptimizerWidget::addMissingSuggestedVariables);
    connect(_duplicateVariableButton, &QPushButton::clicked,
            this, &StructureOptimizerWidget::duplicateSelectedVariable);
    connect(_removeVariablesButton, &QPushButton::clicked,
            this, &StructureOptimizerWidget::removeSelectedVariables);
    connect(_restoreVariableBaselineButton, &QPushButton::clicked,
            this, &StructureOptimizerWidget::restoreModelBaseline);
    connect(_variableView->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, [this]() { updateVariableActionState(); });
    connect(_variableSearch, &QLineEdit::textChanged, this, [this](const QString& keyword) {
        _variableFilterModel->setKeyword(keyword);
        updateVariableActionState();
    });
    connect(_variableTypeFilter, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) {
                const QVariant value = _variableTypeFilter->currentData();
                _variableFilterModel->setKindFilter(value.isValid()
                    ? std::optional<StructureVariableKind>(
                        static_cast<StructureVariableKind>(value.toInt()))
                    : std::nullopt);
                updateVariableActionState();
            });
    connect(_showVariableAdvanced, &QCheckBox::toggled, this, [this](bool visible) {
        _variableView->setColumnHidden(StructureVariableTableModel::PreferredColumn, !visible);
        _variableView->setColumnHidden(
            StructureVariableTableModel::PreferenceWeightColumn, !visible);
    });
    connect(_variableModel, &QAbstractItemModel::modelReset,
            this, [this]() { updateVariableActionState(); });
    connect(_variableModel, &QAbstractItemModel::rowsInserted, this,
            [this](const QModelIndex&, int, int) { updateVariableActionState(); });
    connect(_variableModel, &QAbstractItemModel::rowsRemoved, this,
            [this](const QModelIndex&, int, int) { updateVariableActionState(); });
    connect(_variableFilterModel, &QAbstractItemModel::modelReset,
            this, [this]() { updateVariableActionState(); });
    connect(_variableFilterModel, &QAbstractItemModel::rowsInserted, this,
            [this](const QModelIndex&, int, int) { updateVariableActionState(); });
    connect(_variableFilterModel, &QAbstractItemModel::rowsRemoved, this,
            [this](const QModelIndex&, int, int) { updateVariableActionState(); });
    updateVariableActionState();
    page->setLayout(layout);
    return page;
}

QWidget* StructureOptimizerWidget::createTaskPage()
{
    QWidget* page = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(page);
    _taskView = makeTableView(_taskModel, "optimizationTaskTable");
    layout->addWidget(_taskView);
    QHBoxLayout* taskActions = new QHBoxLayout();
    QPushButton* addTask = new QPushButton("Add Task", page);
    addTask->setObjectName("addOptimizationTaskButton");
    QPushButton* duplicateTask = new QPushButton("Duplicate Task", page);
    duplicateTask->setObjectName("duplicateOptimizationTaskButton");
    QPushButton* removeTask = new QPushButton("Remove Task", page);
    removeTask->setObjectName("removeOptimizationTaskButton");
    taskActions->addWidget(addTask);
    taskActions->addWidget(duplicateTask);
    taskActions->addWidget(removeTask);
    taskActions->addStretch();
    layout->addLayout(taskActions);
    connect(addTask, &QPushButton::clicked, this, &StructureOptimizerWidget::addTask);
    connect(duplicateTask, &QPushButton::clicked,
            this, &StructureOptimizerWidget::duplicateSelectedTask);
    connect(removeTask, &QPushButton::clicked,
            this, &StructureOptimizerWidget::removeSelectedTask);

    QGroupBox* constraints = new QGroupBox("Constraints");
    QVBoxLayout* constraintLayout = new QVBoxLayout(constraints);
    _constraintView = makeTableView(_constraintModel, "structureConstraintTable");
    constraintLayout->addWidget(_constraintView);
    QHBoxLayout* constraintActions = new QHBoxLayout();
    _newConstraintKindCombo = new QComboBox(constraints);
    _newConstraintKindCombo->setObjectName("newStructureConstraintKindCombo");
    for (int kind = static_cast<int>(StructureConstraintKind::ModelValid);
         kind <= static_cast<int>(StructureConstraintKind::MinimumWorkspaceCoverage); ++kind) {
        const StructureConstraintKind value = static_cast<StructureConstraintKind>(kind);
        _newConstraintKindCombo->addItem(constraintKindLabel(value), kind);
    }
    QPushButton* addConstraint = new QPushButton("Add Constraint", constraints);
    addConstraint->setObjectName("addStructureConstraintButton");
    QPushButton* duplicateConstraint = new QPushButton("Duplicate Constraint", constraints);
    duplicateConstraint->setObjectName("duplicateStructureConstraintButton");
    QPushButton* removeConstraint = new QPushButton("Remove Constraint", constraints);
    removeConstraint->setObjectName("removeStructureConstraintButton");
    constraintActions->addWidget(_newConstraintKindCombo);
    constraintActions->addWidget(addConstraint);
    constraintActions->addWidget(duplicateConstraint);
    constraintActions->addWidget(removeConstraint);
    constraintActions->addStretch();
    constraintLayout->addLayout(constraintActions);
    connect(addConstraint, &QPushButton::clicked,
            this, &StructureOptimizerWidget::addConstraint);
    connect(duplicateConstraint, &QPushButton::clicked,
            this, &StructureOptimizerWidget::duplicateSelectedConstraint);
    connect(removeConstraint, &QPushButton::clicked,
            this, &StructureOptimizerWidget::removeSelectedConstraint);
    constraints->setLayout(constraintLayout);
    layout->addWidget(constraints);
    page->setLayout(layout);
    return page;
}

QWidget* StructureOptimizerWidget::createSettingsPage()
{
    QWidget* page = new QWidget();
    QFormLayout* layout = new QFormLayout(page);

    _strategyCombo = new QComboBox(page);
    _strategyCombo->addItem("Hybrid", static_cast<int>(StructureStrategyKind::Hybrid));
    _strategyCombo->addItem("Random", static_cast<int>(StructureStrategyKind::Random));
    _strategyCombo->addItem("Grid", static_cast<int>(StructureStrategyKind::Grid));
    _strategyCombo->setObjectName("structureOptimizationStrategyCombo");
    layout->addRow("Strategy", _strategyCombo);

    _candidateCountSpin = makeSpinBox(1, 100000, _loadedProblem.run.candidateCount);
    _candidateCountSpin->setObjectName("structureOptimizationCandidateCount");
    layout->addRow("Candidates", _candidateCountSpin);

    _eliteCountSpin = makeSpinBox(1, 10000, _loadedProblem.run.eliteCount);
    _eliteCountSpin->setObjectName("structureOptimizationEliteCount");
    layout->addRow("Elite Candidates", _eliteCountSpin);

    _localEliteCountSpin = makeSpinBox(1, 10000, _loadedProblem.run.localEliteCount);
    _localEliteCountSpin->setObjectName("structureOptimizationLocalEliteCount");
    layout->addRow("Local Refinement Elites", _localEliteCountSpin);

    _finalVerificationCountSpin = makeSpinBox(1, 10000,
                                              _loadedProblem.run.finalVerificationCount);
    _finalVerificationCountSpin->setObjectName("structureOptimizationFinalVerificationCount");
    layout->addRow("Final Verification Candidates", _finalVerificationCountSpin);

    _maxLocalSweepsSpin = makeSpinBox(1, 1000, _loadedProblem.run.maxLocalSweeps);
    _maxLocalSweepsSpin->setObjectName("structureOptimizationMaxLocalSweeps");
    layout->addRow("Local Search Sweeps", _maxLocalSweepsSpin);

    _gridStepsSpin = makeSpinBox(2, 100, _loadedProblem.run.gridSteps);
    _gridStepsSpin->setObjectName("structureOptimizationGridSteps");
    layout->addRow("Grid Steps", _gridStepsSpin);

    _seedSpin = makeSpinBox(0, 2147483647,
                            static_cast<int>(_loadedProblem.run.randomSeed));
    _seedSpin->setObjectName("structureOptimizationSeed");
    layout->addRow("Random Seed", _seedSpin);

    QGridLayout* weights = new QGridLayout();
    const QStringList names = {"Reachability", "Manipulability", "Joint Margin", "Collision", "Compactness", "Preference"};
    const QStringList objectNames = {
        "structureOptimizationWeightReachability",
        "structureOptimizationWeightManipulability",
        "structureOptimizationWeightJointMargin",
        "structureOptimizationWeightCollision",
        "structureOptimizationWeightCompactness",
        "structureOptimizationWeightPreference"};
    for (int i = 0; i < names.size(); ++i) {
        QDoubleSpinBox* weight = new QDoubleSpinBox(page);
        weight->setRange(0.0, 1.0);
        weight->setSingleStep(0.01);
        weight->setObjectName(objectNames[i]);
        _weightSpins[static_cast<std::size_t>(i)] = weight;
        weights->addWidget(new QLabel(names[i], page), i / 2, (i % 2) * 2);
        weights->addWidget(weight, i / 2, (i % 2) * 2 + 1);
    }
    layout->addRow("Objective Weights", weights);

    page->setLayout(layout);
    return page;
}

QWidget* StructureOptimizerWidget::createCandidatePage()
{
    QWidget* page = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(page);
    _candidateView = makeTableView(_candidateModel, "structureCandidateTable");
    _candidateView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    layout->addWidget(_candidateView);
    QHBoxLayout* actions = new QHBoxLayout();
    QPushButton* preview = new QPushButton("Preview Candidate", page);
    preview->setObjectName("previewStructureCandidateButton");
    QPushButton* clear = new QPushButton("Clear Preview", page);
    clear->setObjectName("clearStructureCandidatePreviewButton");
    actions->addWidget(preview);
    actions->addWidget(clear);
    actions->addStretch();
    layout->addLayout(actions);
    connect(preview, &QPushButton::clicked,
            this, &StructureOptimizerWidget::previewSelectedCandidate);
    connect(clear, &QPushButton::clicked,
            this, &StructureOptimizerWidget::clearCandidatePreview);
    page->setLayout(layout);
    return page;
}

QWidget* StructureOptimizerWidget::createReportPage()
{
    QWidget* page = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(page);
    QPushButton* open = new QPushButton("Import Project", page);
    open->setObjectName("openStructureOptimizationProjectButton");
    QPushButton* save = new QPushButton("Export Project", page);
    save->setObjectName("saveStructureOptimizationProjectButton");
    QPushButton* exportAll = new QPushButton("Export Report & Models", page);
    exportAll->setObjectName("exportStructureOptimizationResultButton");
    layout->addWidget(open);
    layout->addWidget(save);
    layout->addWidget(exportAll);
    layout->addStretch();
    connect(open, &QPushButton::clicked, this, &StructureOptimizerWidget::openProject);
    connect(save, &QPushButton::clicked, this, &StructureOptimizerWidget::saveProject);
    connect(exportAll, &QPushButton::clicked, this, &StructureOptimizerWidget::exportResult);
    page->setLayout(layout);
    return page;
}

std::vector<StructureDesignVariable>
StructureOptimizerWidget::availableSuggestedVariables() const
{
    const std::vector<StructureDesignVariable> suggested =
        StructureOptimizationUiLogic::suggestVariables(_loadedProblem.context);
    const std::vector<StructureDesignVariable>& current = _variableModel->variables();
    std::vector<StructureDesignVariable> available;
    available.reserve(suggested.size());
    for (const StructureDesignVariable& candidate : suggested) {
        const bool alreadyAdded = std::any_of(
            current.begin(), current.end(), [&candidate](const StructureDesignVariable& variable) {
                return variable.id == candidate.id;
            });
        if (!alreadyAdded)
            available.push_back(candidate);
    }
    return available;
}

void StructureOptimizerWidget::updateVariableActionState()
{
    if (_variableView == nullptr || _addVariableButton == nullptr ||
        _addMissingSuggestionsButton == nullptr || _duplicateVariableButton == nullptr ||
        _removeVariablesButton == nullptr || _restoreVariableBaselineButton == nullptr ||
        _variableSearch == nullptr || _variableTypeFilter == nullptr ||
        _showVariableAdvanced == nullptr)
        return;

    const bool editable = _tabs != nullptr && _tabs->isEnabled() &&
                          (_controller == nullptr || !_controller->isRunning());
    _variableView->setEnabled(editable);
    _variableSearch->setEnabled(editable);
    _variableTypeFilter->setEnabled(editable);
    _showVariableAdvanced->setEnabled(editable);
    _addMissingSuggestionsButton->setEnabled(editable && !availableSuggestedVariables().empty());
    if (_addMissingSuggestionsAction != nullptr)
        _addMissingSuggestionsAction->setEnabled(
            editable && !availableSuggestedVariables().empty());
    _addVariableButton->setEnabled(editable && !availableSuggestedVariables().empty());
    const bool hasSelection =
        editable && _variableView->selectionModel() != nullptr &&
        !_variableView->selectionModel()->selectedRows().isEmpty();
    _duplicateVariableButton->setEnabled(hasSelection);
    _removeVariablesButton->setEnabled(hasSelection);
    _restoreVariableBaselineButton->setEnabled(editable);
    if (_restoreVariableBaselineAction != nullptr)
        _restoreVariableBaselineAction->setEnabled(editable);
}

void StructureOptimizerWidget::addVariable()
{
    if (_controller->isRunning())
        return;

    const std::vector<StructureDesignVariable> available = availableSuggestedVariables();
    if (available.empty())
        return;

    QStringList labels;
    labels.reserve(static_cast<int>(available.size()));
    for (const StructureDesignVariable& candidate : available) {
        labels.append(QString("%1 (%2)")
                          .arg(QString::fromStdString(candidate.label),
                               QString::fromStdString(candidate.id)));
    }

    bool accepted = false;
    const QString selected = QInputDialog::getItem(
        this, "Add Design Variable", "Available model variables:", labels, 0, false, &accepted);
    if (!accepted)
        return;
    const int selectedIndex = labels.indexOf(selected);
    if (selectedIndex < 0)
        return;

    if (_variableModel->appendVariable(available[static_cast<std::size_t>(selectedIndex)])) {
        updateVariableActionState();
        updateRunState();
    }
}

void StructureOptimizerWidget::addMissingSuggestedVariables()
{
    if (_controller->isRunning())
        return;

    int added = 0;
    for (const StructureDesignVariable& variable : availableSuggestedVariables()) {
        if (_variableModel->appendVariable(variable))
            ++added;
    }
    if (added > 0) {
        updateVariableActionState();
        updateRunState();
    }
}

void StructureOptimizerWidget::duplicateSelectedVariable()
{
    if (_controller->isRunning() || _variableView == nullptr ||
        !_variableView->currentIndex().isValid())
        return;

    const QModelIndex sourceIndex =
        _variableFilterModel->mapToSource(_variableView->currentIndex());
    if (!sourceIndex.isValid())
        return;
    const int row = _variableModel->duplicateVariable(sourceIndex.row());
    if (row < 0)
        return;

    const QModelIndex duplicateIndex = _variableFilterModel->mapFromSource(
        _variableModel->index(row, StructureVariableTableModel::IdColumn));
    if (duplicateIndex.isValid()) {
        _variableView->selectionModel()->setCurrentIndex(
            duplicateIndex, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    }
    updateVariableActionState();
    updateRunState();
}

void StructureOptimizerWidget::removeSelectedVariables()
{
    if (_controller->isRunning() || _variableView == nullptr ||
        _variableView->selectionModel() == nullptr)
        return;

    const QModelIndexList selectedRows = _variableView->selectionModel()->selectedRows();
    if (selectedRows.isEmpty())
        return;

    const std::vector<StructureDesignVariable>& variables = _variableModel->variables();
    QModelIndexList sourceRows;
    QStringList names;
    for (const QModelIndex& index : selectedRows) {
        const QModelIndex sourceIndex = _variableFilterModel->mapToSource(index);
        if (sourceIndex.isValid() && sourceIndex.row() >= 0 &&
            sourceIndex.row() < static_cast<int>(variables.size())) {
            const StructureDesignVariable& variable =
                variables[static_cast<std::size_t>(sourceIndex.row())];
            names.append(QString::fromStdString(
                variable.label.empty() ? variable.id : variable.label));
            sourceRows.append(sourceIndex);
        }
    }
    if (names.isEmpty())
        return;

    const QString text = QString("Remove %1 selected design variable(s)?\n\n%2")
        .arg(names.size())
        .arg(names.join("\n"));
    if (QMessageBox::question(this, "Remove Design Variables", text,
                              QMessageBox::Yes | QMessageBox::No,
                              QMessageBox::No) != QMessageBox::Yes)
        return;

    if (_variableModel->removeRows(sourceRows) > 0) {
        updateVariableActionState();
        updateRunState();
    }
}

void StructureOptimizerWidget::restoreModelBaseline()
{
    if (_controller->isRunning())
        return;

    const QString text =
        "Restore the complete model baseline?\n\n"
        "This overwrites all variable additions, removals, ranges, values, and enabled states.";
    if (QMessageBox::question(this, "Restore Model Baseline", text,
                              QMessageBox::Yes | QMessageBox::No,
                              QMessageBox::No) != QMessageBox::Yes)
        return;

    _variableModel->resetVariables(
        StructureOptimizationUiLogic::suggestVariables(_loadedProblem.context));
    updateVariableActionState();
    updateRunState();
}

void StructureOptimizerWidget::updateRunState()
{
    std::string reason;
    const StructureOptimizationProblem problem = collectProblem();
    const bool runnable = StructureOptimizationUiLogic::hasRunnableInputs(problem, &reason);
    const bool contractStale = isFrozenContractStale();
    const bool modelReady = !problem.context.modelSpec.robotName.empty() &&
                            !problem.context.modelSpec.transformJoints.empty();
    _startButton->setEnabled(runnable && !contractStale && !_controller->isRunning() && !_baselineOnlyRunning);
    _applyTemplateButton->setEnabled(modelReady && !_controller->isRunning() && !_baselineOnlyRunning);
    _preflightButton->setEnabled(!_controller->isRunning() && !_baselineOnlyRunning);
    _baselineButton->setEnabled(modelReady && !contractStale && !_controller->isRunning() && !_baselineOnlyRunning);
    _compareButton->setEnabled(_lastResult.candidates.size() > 1 &&
                               !_controller->isRunning() && !_baselineOnlyRunning);
    _pauseButton->setEnabled(_controller->isRunning());
    _cancelButton->setEnabled(_controller->isRunning());
    if (contractStale) {
        // C1/D1: 冻结契约已过期。Preflight 仍可执行，但结果必须附带过期警示，
        // 且 Start/Baseline/Verified 与正式导出保持阻断。
        if (_preflightLabel != nullptr)
            _preflightLabel->setText(QStringLiteral(
                "Preflight note: frozen requirements are STALE. Edits diverge from the frozen "
                "execution contract; re-freeze from the requirement source."));
        _statusLabel->setText(QStringLiteral(
            "Frozen requirements are stale. Start, baseline, verified evaluation and formal "
            "export are blocked until the project is re-frozen from the requirement source."));
        return;
    }
    if (runnable) {
        if (_modelSourceStatus == RobotModelSourceStatus::Current)
            _statusLabel->setText("Optimization project ready.");
    }
    else
        _statusLabel->setText(QString::fromStdString(reason));
}

void StructureOptimizerWidget::updateModelSourceStatus()
{
    const RobotModelStalenessResult result =
        RobotModelStalenessChecker::checkManaged(
            _loadedProblem.context, _projectPath, _managedProjectRoot);
    _modelSourceStatus = result.status;
    if (_modelStatusBanner == nullptr || _modelStatusBannerText == nullptr ||
        _modelStatusBannerSource == nullptr)
        return;

    if (result.status == RobotModelSourceStatus::Current) {
        _modelStatusBanner->hide();
        return;
    }

    _modelStatusBanner->show();
    QToolButton* detailsButton = _modelStatusBanner->findChild<QToolButton*>(
        QStringLiteral("structureModelStatusBannerDetailsButton"));
    if (detailsButton != nullptr)
        detailsButton->setVisible(!result.resolvedSourcePath.isEmpty());
    _modelStatusBannerSource->setVisible(detailsButton != nullptr && detailsButton->isChecked());
    _modelStatusBannerSource->setText(
        result.resolvedSourcePath.isEmpty()
            ? QString()
            : QString("Tracked source: %1").arg(result.resolvedSourcePath));
    _modelStatusBannerSource->setToolTip(_modelStatusBannerSource->text());
    _modelStatusBanner->setToolTip(_modelStatusBannerSource->text());
    switch (result.status) {
        case RobotModelSourceStatus::ModelSpecIncomplete:
            _modelStatusBannerText->setText(
                "The embedded model snapshot is incomplete. Load a complete model snapshot or "
                "create a new project from frozen requirements.");
            return;
        case RobotModelSourceStatus::Current:
            return;
        case RobotModelSourceStatus::Untracked:
            _modelStatusBannerText->setText(
                "Model source is untracked. This project uses its embedded frozen snapshot.");
            return;
        case RobotModelSourceStatus::Stale:
            _modelStatusBannerText->setText(
                "Model source is stale. This project continues to use its embedded frozen "
                "snapshot; it is not synchronized automatically.");
            return;
        case RobotModelSourceStatus::SourceMissing:
            _modelStatusBannerText->setText(
                "Model source is missing. This project continues to use its embedded frozen "
                "snapshot.");
            return;
        case RobotModelSourceStatus::SourceInvalid:
            _modelStatusBannerText->setText(
                "Model source is invalid. This project continues to use its embedded frozen "
                "snapshot.");
            return;
    }
}

void StructureOptimizerWidget::setEditingEnabled(bool enabled)
{
    _tabs->setEnabled(enabled);
    _templateCombo->setEnabled(enabled);
    _applyTemplateButton->setEnabled(enabled);
    _preflightButton->setEnabled(enabled);
    _baselineButton->setEnabled(enabled);
    _compareButton->setEnabled(enabled);
    _candidateCountSpin->setEnabled(enabled);
    _eliteCountSpin->setEnabled(enabled);
    _localEliteCountSpin->setEnabled(enabled);
    _finalVerificationCountSpin->setEnabled(enabled);
    _maxLocalSweepsSpin->setEnabled(enabled);
    _gridStepsSpin->setEnabled(enabled);
    _seedSpin->setEnabled(enabled);
    _strategyCombo->setEnabled(enabled);
    for (QDoubleSpinBox* weight : _weightSpins)
        weight->setEnabled(enabled);
    updateVariableActionState();
}

void StructureOptimizerWidget::applyOptimizationTemplate()
{
    if (_templateCombo == nullptr)
        return;
    StructureOptimizationProblem problem = collectProblem();
    std::string error;
    const auto kind = static_cast<StructureOptimizationTemplateKind>(
        _templateCombo->currentData().toInt());
    if (!StructureOptimizationTemplate::apply(kind, problem, &error)) {
        _statusLabel->setText(QString::fromStdString(error));
        return;
    }
    setProblemWithManagedRoot(problem, _managedProjectRoot);
    runStructurePreflight();
    _statusLabel->setText(QString("Applied template: %1.")
                              .arg(_templateCombo->currentText()));
    Q_EMIT projectDocumentChanged();
}

void StructureOptimizerWidget::runStructurePreflight()
{
    const std::vector<StructurePreflightFinding> findings =
        StructureOptimizationUiLogic::preflight(collectProblem());
    int failures = 0;
    int warnings = 0;
    for (const StructurePreflightFinding& finding : findings) {
        if (finding.severity == AnalysisStatus::Fail)
            ++failures;
        else if (finding.severity == AnalysisStatus::Warning)
            ++warnings;
    }
    if (findings.empty()) {
        _preflightLabel->setText("Preflight: ready. No blocking findings.");
        return;
    }
    QString summary = QString("Preflight: %1 blocking, %2 warning(s).")
                          .arg(failures).arg(warnings);
    if (!findings.front().message.empty())
        summary += " " + QString::fromStdString(findings.front().message);
    _preflightLabel->setText(summary);
}

void StructureOptimizerWidget::evaluateStructureBaseline()
{
    // S7: force one synchronous source read/fingerprint immediately before a
    // Verified baseline; routine cell edits do not repeatedly hit the disk.
    updateModelSourceStatus();
    if (isFrozenContractStale()) {
        _statusLabel->setText(QStringLiteral(
            "Baseline blocked: frozen requirements are stale. Re-freeze from the requirement source."));
        return;
    }
    runStructurePreflight();
    const StructureOptimizationProblem problem = collectProblem();
    const bool modelReady = !problem.context.modelSpec.robotName.empty() &&
                            !problem.context.modelSpec.transformJoints.empty();
    if (!modelReady || !_controller->startBaselineEvaluation(problem)) {
        _statusLabel->setText("Baseline evaluation could not be started.");
        return;
    }
    _baselineLabel->setText("Baseline: evaluating current model...");
    _statusLabel->setText("Evaluating the current model baseline.");
}

void StructureOptimizerWidget::compareStructureCandidates()
{
    if (_candidateView == nullptr || _candidateView->selectionModel() == nullptr) {
        _comparisonLabel->setText("Comparison: no candidate table is available.");
        return;
    }
    std::vector<int> indices;
    QSet<int> unique;
    for (const QModelIndex& row : _candidateView->selectionModel()->selectedRows()) {
        const int index = _candidateModel->index(row.row(), StructureCandidateTableModel::IndexColumn)
                              .data().toInt();
        if (!unique.contains(index)) {
            unique.insert(index);
            indices.push_back(index);
        }
    }
    const StructureCandidateComparison comparison =
        StructureCandidateComparison::compare(_lastResult, indices);
    if (!comparison.valid) {
        _comparisonLabel->setText(QString("Comparison: %1")
                                      .arg(QString::fromStdString(comparison.error)));
        return;
    }
    QStringList entries;
    for (const StructureCandidateComparisonRow& row : comparison.rows) {
        entries.push_back(QString("#%1 score %2 (%3%4)")
                              .arg(row.candidateIndex)
                              .arg(row.score, 0, 'f', 3)
                              .arg(row.scoreDelta >= 0.0 ? "+" : "")
                              .arg(row.scoreDelta, 0, 'f', 3));
    }
    _comparisonLabel->setText(QString("Comparison vs baseline #%1: %2")
                                  .arg(comparison.baselineCandidateIndex)
                                  .arg(entries.join("; ")));
}

void StructureOptimizerWidget::startOptimization()
{
    updateModelSourceStatus();
    if (isFrozenContractStale()) {
        // 直接调用防御：按钮通常已被 updateRunState 禁用。
        _statusLabel->setText(QStringLiteral(
            "Start blocked: frozen requirements are stale. Re-freeze from the requirement source."));
        return;
    }
    StructureOptimizationProblem problem = collectProblem();
    if (!_controller->start(problem))
        return;
    // M16: 捕获运行时不可变快照——完成后候选预览只允许基于这份问题，
    // 不再用当前 collectProblem()（变量表可能已被继续编辑）。
    _lastRunProblem = problem;
    _hasLastRunProblem = true;
    _lastRunVariableSchemaFingerprint =
        StructureOptimizationUiLogic::designVariableSchemaFingerprint(
            problem.variables);
    _statusLabel->setText("Optimization running in the background.");
}

void StructureOptimizerWidget::togglePause()
{
    if (_controller->isPaused())
        _controller->resume();
    else
        _controller->pause();
}

void StructureOptimizerWidget::cancelOptimization()
{
    _controller->cancel();
    _statusLabel->setText("Canceling optimization.");
}

void StructureOptimizerWidget::handleRunningChanged(bool running)
{
    setEditingEnabled(!running);
    _startButton->setEnabled(!running);
    _pauseButton->setEnabled(running);
    _cancelButton->setEnabled(running);
    if (!running)
        updateRunState();
}

void StructureOptimizerWidget::handlePausedChanged(bool paused)
{
    _pauseButton->setText(paused ? "Resume" : "Pause");
}

void StructureOptimizerWidget::handleProgress(const StructureProgress& progress)
{
    _progressLabel->setText(QString("%1 %2/%3, best score %4")
                                .arg(QString::fromStdString(progress.stage))
                                .arg(progress.completed)
                                .arg(progress.planned)
                                .arg(progress.bestScore, 0, 'f', 2));
}

void StructureOptimizerWidget::handleCompleted(
    const StructureOptimizationResult& result)
{
    _lastResult = result;
    _candidateModel->setResult(result);
    int feasibleCount = 0;
    for (const StructureCandidateResult& candidate : result.candidates)
        feasibleCount += candidate.feasible ? 1 : 0;
    QString status = QString("%1 Final Verified %2, cache hits %3, sensitivity %4.")
        .arg(result.canceled ? "Optimization canceled." : "Optimization complete.")
        .arg(result.diagnostics.finalVerifiedCandidates)
        .arg(result.diagnostics.cacheHits)
        .arg(QString::fromStdString(result.sensitivity.robustnessGrade));
    if (_loadedProblem.evaluation.coverageBox.enabled) {
        const std::array<int, 3>& cells = _loadedProblem.evaluation.coverageBox.cells;
        status += QString(" Workspace sampling Quick %1 / Verified %2, grid %3x%4x%5.")
            .arg(_loadedProblem.evaluation.quickWorkspace.sampleCount)
            .arg(_loadedProblem.evaluation.verifiedWorkspace.sampleCount)
            .arg(cells[0])
            .arg(cells[1])
            .arg(cells[2]);
    }
    status += QString(" Feasible candidates %1, best #%2.")
                  .arg(feasibleCount).arg(result.bestCandidateIndex);
    _statusLabel->setText(status);
    if (result.baselineCandidateIndex >= 0 && result.bestCandidateIndex >= 0) {
        const StructureCandidateComparison comparison =
            StructureCandidateComparison::compare(result, {result.bestCandidateIndex});
        if (comparison.valid && !comparison.rows.empty()) {
            const StructureCandidateComparisonRow& row = comparison.rows.front();
            _comparisonLabel->setText(
                QString("Best #%1 vs baseline #%2: score %3%4, reachability %5%6, length %7%8 m.")
                    .arg(row.candidateIndex)
                    .arg(comparison.baselineCandidateIndex)
                    .arg(row.scoreDelta >= 0.0 ? "+" : "")
                    .arg(row.scoreDelta, 0, 'f', 3)
                    .arg(row.reachabilityDelta >= 0.0 ? "+" : "")
                    .arg(row.reachabilityDelta * 100.0, 0, 'f', 1)
                    .arg(row.lengthDelta >= 0.0 ? "+" : "")
                    .arg(row.lengthDelta, 0, 'f', 3));
        }
    }
    Q_EMIT projectDocumentChanged ();
    Q_EMIT optimizationCompletedForWorkflow (!result.canceled && !result.candidates.empty ());
}

void StructureOptimizerWidget::handleFailed(const QString& message)
{
    _statusLabel->setText(message);
}

void StructureOptimizerWidget::handleBaselineCompleted(
    const StructureOptimizationResult& result)
{
    _baselineResult = result;
    if (result.candidates.empty()) {
        _baselineLabel->setText("Baseline: no result was produced.");
        return;
    }
    const StructureCandidateResult& candidate = result.candidates.front();
    _baselineLabel->setText(QString("Baseline: score %1, reachability %2, length %3 m.")
                                .arg(candidate.totalScore, 0, 'f', 3)
                                .arg(candidate.raw.weightedReachability * 100.0, 0, 'f', 1)
                                .arg(candidate.raw.totalKinematicLength, 0, 'f', 3));
    _statusLabel->setText("Current model baseline evaluated.");
    Q_EMIT projectDocumentChanged();
}

void StructureOptimizerWidget::handleBaselineFailed(const QString& message)
{
    const bool canceled = message.contains(QStringLiteral("canceled"), Qt::CaseInsensitive);
    _baselineLabel->setText(canceled ? "Baseline: evaluation canceled."
                                     : "Baseline: evaluation failed.");
    _statusLabel->setText(message);
}

void StructureOptimizerWidget::handleBaselineRunningChanged(bool running)
{
    _baselineOnlyRunning = running;
    if (running) {
        _tabs->setEnabled(false);
        _templateCombo->setEnabled(false);
        _applyTemplateButton->setEnabled(false);
        _preflightButton->setEnabled(false);
        _baselineButton->setEnabled(false);
        _compareButton->setEnabled(false);
    } else {
        _tabs->setEnabled(true);
        updateRunState();
    }
}

void StructureOptimizerWidget::previewSelectedCandidate()
{
    if (!_previewController || !_candidateView || !_candidateView->currentIndex().isValid()) {
        _statusLabel->setText("No candidate selected for preview.");
        return;
    }
    const int row = _candidateView->currentIndex().row();
    const QModelIndex index = _candidateModel->index(row, StructureCandidateTableModel::IndexColumn);
    const StructureCandidateResult* candidate =
        _candidateModel->candidateByIndex(index.data().toInt());
    if (candidate == nullptr || !candidate->feasible) {
        _statusLabel->setText("Only feasible candidates can be previewed.");
        return;
    }
    QString error;
    // M16: 预览必须绑定启动时的不可变快照；当前变量定义与快照模式不一致时
    // 明确拒绝并说明原因，绝不把历史候选套到漂移后的模型上。
    const StructurePreviewPermission permission =
        StructureOptimizationUiLogic::evaluatePreviewPermission(
            _hasLastRunProblem, _lastRunVariableSchemaFingerprint,
            StructureOptimizationUiLogic::designVariableSchemaFingerprint(
                collectProblem().variables));
    if (!permission.allowed) {
        _statusLabel->setText(QString::fromStdString(permission.reason));
        return;
    }
    if (!_previewController->preview(_lastRunProblem, *candidate, &error)) {
        _statusLabel->setText(error);
        return;
    }
    _statusLabel->setText(QString("Previewing candidate #%1.").arg(candidate->index));
}

void StructureOptimizerWidget::clearCandidatePreview()
{
    if (_previewController)
        _previewController->clearPreview();
}

void StructureOptimizerWidget::newProjectFromModelSpec()
{
    const QString path = QFileDialog::getOpenFileName(
        this, "New Project from Model Snapshot", _projectPath,
        "Robot model snapshot (*.rmb.json)");
    if (path.isEmpty())
        return;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, "Cannot Read Model Snapshot", file.errorString());
        return;
    }

    RobotModelSpec spec;
    std::string parseError;
    if (!RobotModelSpecJson::fromJson(file.readAll().toStdString(), spec, &parseError)) {
        QMessageBox::warning(this, "Cannot Read Model Snapshot",
                             QString::fromStdString(parseError));
        return;
    }

    StructureOptimizationProblem problem;
    std::string factoryError;
    if (!StructureOptimizationProjectFactory::create(spec, path, problem, &factoryError)) {
        QMessageBox::warning(this, "Cannot Create Optimization Project",
                             QString::fromStdString(factoryError));
        return;
    }

    _projectPath.clear();
    _managedProjectRoot.clear();
    setProblem(problem);
    _statusLabel->setText(
        QStringLiteral("Optimization project created from the model snapshot. Add task points before starting."));
}

void StructureOptimizerWidget::newProjectFromFrozenRequirements()
{
    const QString readinessError = robotProjectWorkCellReadinessError(_studio);
    if (!readinessError.isEmpty()) {
        _statusLabel->setText(readinessError);
        return;
    }
    QString path;
    QString resolveError;
    const bool managedRequirement =
        _studio != nullptr &&
        _studio->resolveProjectResource(
            QStringLiteral("engineering-requirements.main"), path, &resolveError);
    if (managedRequirement && !_studio->confirmSaveBeforeProjectResourceRead(this)) {
        _statusLabel->setText(
            "Import canceled. Save project changes before reading frozen requirements.");
        return;
    }
    if (path.isEmpty()) {
        const QString initialDirectory = _studio != nullptr && !_studio->projectDirectory().isEmpty()
            ? QDir(_studio->projectDirectory()).filePath(QStringLiteral("requirements"))
            : _projectPath;
        path = QFileDialog::getOpenFileName(
            this, "New Project from Frozen Requirements", initialDirectory,
            "Frozen engineering requirement (*.requirements.json *.json)");
    }
    if (path.isEmpty())
        return;

    if (_scenarioWorkCell == nullptr) {
        QMessageBox::warning(this, "Create Structure Optimization Project Failed",
                             "No active WorkCell is available for frozen scenario validation.");
        return;
    }

    StructureOptimizationProblem problem;
    FrozenRequirementValidationResult validation;
    std::string importError;
    // 冻结需求是跨插件的只读交付物。所有文件解析、模型一致性复核及 P2 能力边界检查均由
    // 服务层完成；界面不能直接把可编辑 RequirementSet 转成任务点，以免绕过冻结审计门禁。
    const bool imported = managedRequirement
        ? FrozenRequirementProjectImportService::createProblem(
              path, *_scenarioWorkCell, _scenarioState, problem, &validation, &importError,
              _studio->projectDirectory())
        : FrozenRequirementProjectImportService::createProblem(
              path, *_scenarioWorkCell, _scenarioState, problem, &validation, &importError);
    if (!imported) {
        QMessageBox::warning(this, "Cannot Create Optimization Project",
                             QString::fromStdString(importError));
        return;
    }

    // 需求文件是上游输入而不是结构优化项目本身。清除项目路径可保证后续“保存项目”写入一个
    // 新文件，既不会修改冻结需求，也不会覆盖用户此前打开的优化项目。
    _projectPath.clear();
    setProblemWithManagedRoot(
        problem, managedRequirement ? _studio->projectDirectory() : QString());
    _statusLabel->setText(
        "Optimization project created from frozen requirements.");
    QString validationStatus =
        tr("Created a structure optimization project from frozen requirements.");
    if (validation.robotStateChanged) {
        validationStatus +=
            tr(" Robot joint state differs from the frozen state, but fixtures and the external "
               "environment are unchanged. Frozen requirements remain valid.");
    }
    for (const std::string& warning : validation.warnings)
        validationStatus += QLatin1Char(' ') + QString::fromStdString(warning);
    _statusLabel->setText(validationStatus);
    Q_EMIT projectDocumentChanged();
}

void StructureOptimizerWidget::openProject()
{
    const QString path = QFileDialog::getOpenFileName(
        this, "Import Optimization Project", _projectPath,
        "Structure optimization project (*.structure-optimization.json)");
    if (path.isEmpty())
        return;
    StructureOptimizationProblem problem;
    int selectedCandidateIndex = -1;
    QString error;
    if (!StructureOptimizationProjectAdapter::loadProject(
            path, problem, &selectedCandidateIndex, &error)) {
        QMessageBox::warning(this, "Cannot Open Project", error);
        return;
    }
    _projectPath = path;
    _managedProjectRoot.clear();
    setProblem(problem);
    if (_modelSourceStatus == RobotModelSourceStatus::Current)
        _statusLabel->setText("Optimization project loaded.");
}

void StructureOptimizerWidget::saveProject()
{
    // 活动 rwproj 的正式资源由主窗口 Registry 通过 Provider 保存；此按钮只导出一份
    // 项目外副本，始终要求用户选择目标路径，避免绕过多资源事务直接覆盖正式资源。
    const QString path = QFileDialog::getSaveFileName(
        this, "Export Optimization Project", _projectPath.isEmpty()
            ? QStringLiteral("structure-optimization.structure-optimization.json") : _projectPath,
        "Structure optimization project (*.structure-optimization.json)");
    if (path.isEmpty())
        return;
    int selectedCandidateIndex = -1;
    if (_candidateView != nullptr && _candidateView->currentIndex().isValid())
        selectedCandidateIndex = _candidateView->currentIndex().siblingAtColumn(
            StructureCandidateTableModel::IndexColumn).data().toInt();
    QString error;
    if (!StructureOptimizationProjectAdapter::saveProject(
            path, collectProblem(), selectedCandidateIndex, &error)) {
        QMessageBox::warning(this, "Cannot Export Project", error);
        return;
    }
    _projectPath = path;
    _statusLabel->setText("Optimization project exported.");
}

void StructureOptimizerWidget::exportResult()
{
    if (isFrozenContractStale()) {
        QMessageBox::information(this, QStringLiteral("Export Report"),
                                 QStringLiteral(
                                     "Formal export is blocked: frozen requirements are stale. "
                                     "Re-freeze from the requirement source before exporting results."));
        return;
    }
    if (_lastResult.candidates.empty()) {
        QMessageBox::information(this, "Export Report", "No optimization results to export.");
        return;
    }
    const QString directory = QFileDialog::getExistingDirectory(
        this, "Select Export Directory", _projectPath.isEmpty() ? QString() : QFileInfo(_projectPath).absolutePath());
    if (directory.isEmpty())
        return;
    StructureOptimizationExportRequest request;
    request.directory = directory;
    if (_candidateView != nullptr && _candidateView->currentIndex().isValid()) {
        request.selectedCandidateIndex = _candidateView->currentIndex().siblingAtColumn(
            StructureCandidateTableModel::IndexColumn).data().toInt();
        const StructureCandidateResult* candidate =
            _candidateModel->candidateByIndex(request.selectedCandidateIndex);
        request.exportCandidateModel = candidate != nullptr && candidate->feasible;
    } else {
        request.exportCandidateModel = false;
    }
    const StructureOptimizationExportResult exported =
        StructureOptimizationExportService::exportAll(collectProblem(), _lastResult, request);
    if (!exported.ok) {
        QMessageBox::warning(this, "Export Failed", exported.errors.join("\n"));
        return;
    }
    _statusLabel->setText("Report exported to " + directory);
}

void StructureOptimizerWidget::addTask()
{
    std::vector<std::string> ids;
    for (const OptimizationTaskPoint& task : _taskModel->tasks())
        ids.push_back(task.point.id);

    OptimizationTaskPoint task;
    task.point.id = uniqueId("task", ids);
    task.point.name = "New Task";
    task.point.refFrame = "WORLD";
    // Empty means that the evaluator uses the project's default device end frame.
    task.point.tcpFrame.clear();
    task.point.enabled = true;
    task.required = true;
    const int row = _taskModel->appendTask(task);
    _taskView->selectRow(row);
    updateRunState();
}

void StructureOptimizerWidget::duplicateSelectedTask()
{
    if (_taskView == nullptr || !_taskView->currentIndex().isValid())
        return;
    const int sourceRow = _taskView->currentIndex().row();
    const std::vector<OptimizationTaskPoint>& tasks = _taskModel->tasks();
    if (sourceRow < 0 || sourceRow >= static_cast<int>(tasks.size()))
        return;
    std::vector<std::string> ids;
    for (const OptimizationTaskPoint& task : tasks)
        ids.push_back(task.point.id);
    OptimizationTaskPoint copy = tasks[static_cast<std::size_t>(sourceRow)];
    copy.point.id = uniqueId("task", ids);
    copy.point.name += " (Copy)";
    const int row = _taskModel->appendTask(copy);
    _taskView->selectRow(row);
    updateRunState();
}

void StructureOptimizerWidget::removeSelectedTask()
{
    if (_taskView == nullptr || !_taskView->currentIndex().isValid())
        return;
    if (_taskModel->removeTask(_taskView->currentIndex().row()))
        updateRunState();
}

void StructureOptimizerWidget::addConstraint()
{
    std::vector<std::string> ids;
    for (const StructureConstraint& constraint : _constraintModel->constraints())
        ids.push_back(constraint.id);
    const StructureConstraintKind kind = static_cast<StructureConstraintKind>(
        _newConstraintKindCombo->currentData().toInt());
    StructureConstraint constraint = makeDefaultConstraint(
        kind, uniqueId("constraint", ids));
    const int row = _constraintModel->appendConstraint(constraint);
    _constraintView->selectRow(row);
    updateRunState();
}

void StructureOptimizerWidget::duplicateSelectedConstraint()
{
    if (_constraintView == nullptr || !_constraintView->currentIndex().isValid())
        return;
    const int sourceRow = _constraintView->currentIndex().row();
    const std::vector<StructureConstraint>& constraints = _constraintModel->constraints();
    if (sourceRow < 0 || sourceRow >= static_cast<int>(constraints.size()))
        return;
    std::vector<std::string> ids;
    for (const StructureConstraint& constraint : constraints)
        ids.push_back(constraint.id);
    StructureConstraint copy = constraints[static_cast<std::size_t>(sourceRow)];
    copy.id = uniqueId("constraint", ids);
    copy.label += " (Copy)";
    const int row = _constraintModel->appendConstraint(copy);
    _constraintView->selectRow(row);
    updateRunState();
}

void StructureOptimizerWidget::removeSelectedConstraint()
{
    if (_constraintView == nullptr || !_constraintView->currentIndex().isValid())
        return;
    if (_constraintModel->removeConstraint(_constraintView->currentIndex().row()))
        updateRunState();
}
