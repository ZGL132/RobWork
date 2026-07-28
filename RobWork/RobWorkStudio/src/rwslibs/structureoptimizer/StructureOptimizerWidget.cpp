#include "StructureOptimizerWidget.hpp"

#include "StructureOptimizationObjectiveProfile.hpp"

#include "OptimizationTaskTableModel.hpp"
#include "StructureCandidateTableModel.hpp"
#include "StructureConstraintTableModel.hpp"
#include "StructureOptimizationController.hpp"
#include "StructureOptimizationExportService.hpp"
#include "StructureOptimizationProjectAdapter.hpp"
#include "StructureOptimizationProjectFactory.hpp"
#include "StructureOptimizationUiLogic.hpp"
#include "StructureVariableTableModel.hpp"

#include <rwslibs/robotmodelbuilder/RobotModelSpecJson.hpp>

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableView>
#include <QVBoxLayout>

#include <algorithm>
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

} // namespace

StructureOptimizerWidget::StructureOptimizerWidget(QWidget* parent)
    : QWidget(parent),
      _variableModel(new StructureVariableTableModel(this)),
      _taskModel(new OptimizationTaskTableModel(this)),
      _constraintModel(new StructureConstraintTableModel(this)),
      _candidateModel(new StructureCandidateTableModel(this)),
      _controller(new StructureOptimizationController(this))
{
    _tabs = new QTabWidget(this);
    _tabs->setObjectName("structureOptimizerTabs");
    _tabs->addTab(createVariablePage(), "设计变量");
    _tabs->addTab(createTaskPage(), "任务与约束");
    _tabs->addTab(createSettingsPage(), "优化设置");
    _tabs->addTab(createCandidatePage(), "候选方案");
    _tabs->addTab(createReportPage(), "报告导出");

    _startButton = new QPushButton("开始优化", this);
    _startButton->setObjectName("startOptimizationButton");
    _pauseButton = new QPushButton("暂停", this);
    _pauseButton->setObjectName("pauseOptimizationButton");
    _cancelButton = new QPushButton("取消", this);
    _cancelButton->setObjectName("cancelOptimizationButton");
    _statusLabel = new QLabel("等待加载结构优化项目。", this);
    _statusLabel->setObjectName("structureOptimizationStatusLabel");
    _progressLabel = new QLabel("尚未运行", this);
    _progressLabel->setObjectName("structureOptimizationProgressLabel");

    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->addWidget(_startButton);
    buttonLayout->addWidget(_pauseButton);
    buttonLayout->addWidget(_cancelButton);
    buttonLayout->addStretch();
    buttonLayout->addWidget(_progressLabel);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
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
    connect(_variableModel, &QAbstractItemModel::dataChanged,
            this, &StructureOptimizerWidget::updateRunState);
    connect(_taskModel, &QAbstractItemModel::dataChanged,
            this, &StructureOptimizerWidget::updateRunState);
    connect(_constraintModel, &QAbstractItemModel::dataChanged,
            this, &StructureOptimizerWidget::updateRunState);
    for (QSpinBox* spin : {_candidateCountSpin, _eliteCountSpin, _localEliteCountSpin,
                           _finalVerificationCountSpin, _maxLocalSweepsSpin,
                           _gridStepsSpin, _seedSpin}) {
        connect(spin, QOverload<int>::of(&QSpinBox::valueChanged),
                this, &StructureOptimizerWidget::updateRunState);
    }
    connect(_strategyCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &StructureOptimizerWidget::updateRunState);
    for (QDoubleSpinBox* weight : _weightSpins) {
        connect(weight, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                this, &StructureOptimizerWidget::updateRunState);
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

void StructureOptimizerWidget::setProblem(
    const StructureOptimizationProblem& problem)
{
    _loadedProblem = problem;
    if (_loadedProblem.variables.empty())
        _loadedProblem.variables =
            StructureOptimizationUiLogic::suggestVariables(_loadedProblem.context);

    _variableModel->setVariables(_loadedProblem.variables);
    _taskModel->setTasks(_loadedProblem.tasks);
    _constraintModel->setConstraints(_loadedProblem.constraints);
    _candidateModel->setCandidates({});
    _lastResult = StructureOptimizationResult();

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

QString StructureOptimizerWidget::statusText() const
{
    return _statusLabel != nullptr ? _statusLabel->text() : QString();
}

QWidget* StructureOptimizerWidget::createVariablePage()
{
    QWidget* page = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(page);
    layout->addWidget(makeTableView(_variableModel, "structureVariableTable"));
    page->setLayout(layout);
    return page;
}

QWidget* StructureOptimizerWidget::createTaskPage()
{
    QWidget* page = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(page);
    layout->addWidget(makeTableView(_taskModel, "optimizationTaskTable"));

    QGroupBox* constraints = new QGroupBox("约束条件");
    QVBoxLayout* constraintLayout = new QVBoxLayout(constraints);
    constraintLayout->addWidget(makeTableView(_constraintModel, "structureConstraintTable"));
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
    layout->addRow("策略", _strategyCombo);

    _candidateCountSpin = makeSpinBox(1, 100000, _loadedProblem.run.candidateCount);
    _candidateCountSpin->setObjectName("structureOptimizationCandidateCount");
    layout->addRow("候选数量", _candidateCountSpin);

    _eliteCountSpin = makeSpinBox(1, 10000, _loadedProblem.run.eliteCount);
    _eliteCountSpin->setObjectName("structureOptimizationEliteCount");
    layout->addRow("精英数量", _eliteCountSpin);

    _localEliteCountSpin = makeSpinBox(1, 10000, _loadedProblem.run.localEliteCount);
    _localEliteCountSpin->setObjectName("structureOptimizationLocalEliteCount");
    layout->addRow("局部精修精英数", _localEliteCountSpin);

    _finalVerificationCountSpin = makeSpinBox(1, 10000,
                                              _loadedProblem.run.finalVerificationCount);
    _finalVerificationCountSpin->setObjectName("structureOptimizationFinalVerificationCount");
    layout->addRow("最终复核数", _finalVerificationCountSpin);

    _maxLocalSweepsSpin = makeSpinBox(1, 1000, _loadedProblem.run.maxLocalSweeps);
    _maxLocalSweepsSpin->setObjectName("structureOptimizationMaxLocalSweeps");
    layout->addRow("局部搜索轮数", _maxLocalSweepsSpin);

    _gridStepsSpin = makeSpinBox(2, 100, _loadedProblem.run.gridSteps);
    _gridStepsSpin->setObjectName("structureOptimizationGridSteps");
    layout->addRow("网格步数", _gridStepsSpin);

    _seedSpin = makeSpinBox(0, 2147483647,
                            static_cast<int>(_loadedProblem.run.randomSeed));
    _seedSpin->setObjectName("structureOptimizationSeed");
    layout->addRow("随机种子", _seedSpin);

    QGridLayout* weights = new QGridLayout();
    const QStringList names = {"可达率", "操纵度", "关节裕度", "碰撞", "紧凑性", "偏好"};
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
    layout->addRow("权重", weights);

    page->setLayout(layout);
    return page;
}

QWidget* StructureOptimizerWidget::createCandidatePage()
{
    QWidget* page = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(page);
    _candidateView = makeTableView(_candidateModel, "structureCandidateTable");
    layout->addWidget(_candidateView);
    QHBoxLayout* actions = new QHBoxLayout();
    QPushButton* preview = new QPushButton("预览候选", page);
    preview->setObjectName("previewStructureCandidateButton");
    QPushButton* clear = new QPushButton("清除预览", page);
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
    QPushButton* newFromModel = new QPushButton("从模型快照新建项目", page);
    newFromModel->setObjectName("newStructureOptimizationProjectFromModelButton");
    QPushButton* open = new QPushButton("打开项目", page);
    open->setObjectName("openStructureOptimizationProjectButton");
    QPushButton* save = new QPushButton("保存项目", page);
    save->setObjectName("saveStructureOptimizationProjectButton");
    QPushButton* exportAll = new QPushButton("导出报告和候选模型", page);
    exportAll->setObjectName("exportStructureOptimizationResultButton");
    layout->addWidget(newFromModel);
    layout->addWidget(open);
    layout->addWidget(save);
    layout->addWidget(exportAll);
    layout->addStretch();
    connect(newFromModel, &QPushButton::clicked,
            this, &StructureOptimizerWidget::newProjectFromModelSpec);
    connect(open, &QPushButton::clicked, this, &StructureOptimizerWidget::openProject);
    connect(save, &QPushButton::clicked, this, &StructureOptimizerWidget::saveProject);
    connect(exportAll, &QPushButton::clicked, this, &StructureOptimizerWidget::exportResult);
    page->setLayout(layout);
    return page;
}

void StructureOptimizerWidget::updateRunState()
{
    std::string reason;
    const bool runnable = StructureOptimizationUiLogic::hasRunnableInputs(
        collectProblem(), &reason);
    _startButton->setEnabled(runnable && !_controller->isRunning());
    _pauseButton->setEnabled(_controller->isRunning());
    _cancelButton->setEnabled(_controller->isRunning());
    if (runnable)
        _statusLabel->setText("结构优化项目已就绪。");
    else
        _statusLabel->setText(QString::fromStdString(reason));
}

void StructureOptimizerWidget::setEditingEnabled(bool enabled)
{
    _tabs->setEnabled(enabled);
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
}

void StructureOptimizerWidget::startOptimization()
{
    StructureOptimizationProblem problem = collectProblem();
    if (!_controller->start(problem))
        return;
    _statusLabel->setText("结构优化正在后台运行。");
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
    _statusLabel->setText("正在取消结构优化。");
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
    _pauseButton->setText(paused ? "继续" : "暂停");
}

void StructureOptimizerWidget::handleProgress(const StructureProgress& progress)
{
    _progressLabel->setText(QString("%1 %2/%3，最佳分 %4")
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
    _statusLabel->setText(result.canceled ? "结构优化已取消。" : "结构优化已完成。");
}

void StructureOptimizerWidget::handleFailed(const QString& message)
{
    _statusLabel->setText(message);
}

void StructureOptimizerWidget::previewSelectedCandidate()
{
    if (!_previewController || !_candidateView || !_candidateView->currentIndex().isValid()) {
        _statusLabel->setText("没有可预览的候选方案。");
        return;
    }
    const int row = _candidateView->currentIndex().row();
    const QModelIndex index = _candidateModel->index(row, StructureCandidateTableModel::IndexColumn);
    const StructureCandidateResult* candidate =
        _candidateModel->candidateByIndex(index.data().toInt());
    if (candidate == nullptr || !candidate->feasible) {
        _statusLabel->setText("只能预览可行候选方案。");
        return;
    }
    QString error;
    if (!_previewController->preview(collectProblem(), *candidate, &error)) {
        _statusLabel->setText(error);
        return;
    }
    _statusLabel->setText(QString("正在预览候选方案 #%1。").arg(candidate->index));
}

void StructureOptimizerWidget::clearCandidatePreview()
{
    if (_previewController)
        _previewController->clearPreview();
}

void StructureOptimizerWidget::newProjectFromModelSpec()
{
    const QString path = QFileDialog::getOpenFileName(
        this, "从模型快照新建结构优化项目", _projectPath,
        "Robot model snapshot (*.rmb.json)");
    if (path.isEmpty())
        return;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, "读取模型快照失败", file.errorString());
        return;
    }

    RobotModelSpec spec;
    std::string parseError;
    if (!RobotModelSpecJson::fromJson(file.readAll().toStdString(), spec, &parseError)) {
        QMessageBox::warning(this, "读取模型快照失败",
                             QString::fromStdString(parseError));
        return;
    }

    StructureOptimizationProblem problem;
    std::string factoryError;
    if (!StructureOptimizationProjectFactory::create(spec, problem, &factoryError)) {
        QMessageBox::warning(this, "创建结构优化项目失败",
                             QString::fromStdString(factoryError));
        return;
    }

    problem.context.sourceModelPath = path.toStdString();
    _projectPath.clear();
    setProblem(problem);
    _statusLabel->setText("已从模型快照创建结构优化项目，请添加任务点后开始优化。");
}

void StructureOptimizerWidget::openProject()
{
    const QString path = QFileDialog::getOpenFileName(
        this, "打开结构优化项目", _projectPath,
        "Structure optimization project (*.structure-optimization.json)");
    if (path.isEmpty())
        return;
    StructureOptimizationProblem problem;
    int selectedCandidateIndex = -1;
    QString error;
    if (!StructureOptimizationProjectAdapter::loadProject(
            path, problem, &selectedCandidateIndex, &error)) {
        QMessageBox::warning(this, "打开项目失败", error);
        return;
    }
    _projectPath = path;
    setProblem(problem);
    _statusLabel->setText("结构优化项目已载入。");
}

void StructureOptimizerWidget::saveProject()
{
    QString path = _projectPath;
    if (path.isEmpty()) {
        path = QFileDialog::getSaveFileName(
            this, "保存结构优化项目", "structure-optimization.structure-optimization.json",
            "Structure optimization project (*.structure-optimization.json)");
    }
    if (path.isEmpty())
        return;
    int selectedCandidateIndex = -1;
    if (_candidateView != nullptr && _candidateView->currentIndex().isValid())
        selectedCandidateIndex = _candidateView->currentIndex().siblingAtColumn(
            StructureCandidateTableModel::IndexColumn).data().toInt();
    QString error;
    if (!StructureOptimizationProjectAdapter::saveProject(
            path, collectProblem(), selectedCandidateIndex, &error)) {
        QMessageBox::warning(this, "保存项目失败", error);
        return;
    }
    _projectPath = path;
    _statusLabel->setText("结构优化项目已保存。");
}

void StructureOptimizerWidget::exportResult()
{
    if (_lastResult.candidates.empty()) {
        QMessageBox::information(this, "导出报告", "尚无可导出的优化结果。");
        return;
    }
    const QString directory = QFileDialog::getExistingDirectory(
        this, "选择导出目录", _projectPath.isEmpty() ? QString() : QFileInfo(_projectPath).absolutePath());
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
        QMessageBox::warning(this, "导出失败", exported.errors.join("\n"));
        return;
    }
    _statusLabel->setText("结构优化报告已导出到 " + directory);
}
