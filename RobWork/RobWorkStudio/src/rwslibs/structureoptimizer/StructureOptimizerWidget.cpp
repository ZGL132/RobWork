#include "StructureOptimizerWidget.hpp"

#include "OptimizationTaskTableModel.hpp"
#include "StructureCandidateTableModel.hpp"
#include "StructureOptimizationController.hpp"
#include "StructureOptimizationUiLogic.hpp"
#include "StructureVariableTableModel.hpp"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
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
    _candidateModel->setCandidates({});

    _candidateCountSpin->setValue(_loadedProblem.run.candidateCount);
    _eliteCountSpin->setValue(_loadedProblem.run.eliteCount);
    _seedSpin->setValue(static_cast<int>(_loadedProblem.run.randomSeed));

    updateRunState();
}

StructureOptimizationProblem StructureOptimizerWidget::collectProblem() const
{
    StructureOptimizationProblem problem = _loadedProblem;
    problem.variables = _variableModel->variables();
    problem.tasks = _taskModel->tasks();
    problem.run.candidateCount = _candidateCountSpin->value();
    problem.run.eliteCount = _eliteCountSpin->value();
    problem.run.randomSeed = static_cast<unsigned int>(_seedSpin->value());
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

    QGroupBox* constraints = new QGroupBox("硬约束摘要");
    QVBoxLayout* constraintLayout = new QVBoxLayout(constraints);
    constraintLayout->addWidget(new QLabel("模型合法、必达任务可达、碰撞安全、尺寸边界。"));
    constraints->setLayout(constraintLayout);
    layout->addWidget(constraints);
    page->setLayout(layout);
    return page;
}

QWidget* StructureOptimizerWidget::createSettingsPage()
{
    QWidget* page = new QWidget();
    QFormLayout* layout = new QFormLayout(page);

    QComboBox* strategy = new QComboBox(page);
    strategy->addItems({"Hybrid", "Random", "Grid"});
    strategy->setObjectName("structureOptimizationStrategyCombo");
    layout->addRow("策略", strategy);

    _candidateCountSpin = makeSpinBox(1, 100000, _loadedProblem.run.candidateCount);
    _candidateCountSpin->setObjectName("structureOptimizationCandidateCount");
    layout->addRow("候选数量", _candidateCountSpin);

    _eliteCountSpin = makeSpinBox(1, 10000, _loadedProblem.run.eliteCount);
    _eliteCountSpin->setObjectName("structureOptimizationEliteCount");
    layout->addRow("精英数量", _eliteCountSpin);

    _seedSpin = makeSpinBox(0, 2147483647,
                            static_cast<int>(_loadedProblem.run.randomSeed));
    _seedSpin->setObjectName("structureOptimizationSeed");
    layout->addRow("随机种子", _seedSpin);

    QGridLayout* weights = new QGridLayout();
    const QStringList names = {"可达率", "操纵度", "关节裕度", "碰撞", "紧凑性", "偏好"};
    for (int i = 0; i < names.size(); ++i) {
        QDoubleSpinBox* weight = new QDoubleSpinBox(page);
        weight->setRange(0.0, 1.0);
        weight->setSingleStep(0.01);
        weight->setValue(0.1);
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
    layout->addWidget(makeTableView(_candidateModel, "structureCandidateTable"));
    page->setLayout(layout);
    return page;
}

QWidget* StructureOptimizerWidget::createReportPage()
{
    QWidget* page = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(page);
    layout->addWidget(new QLabel("报告导出将在候选比较阶段接入 JSON、CSV 与模型包导出。"));
    QPushButton* exportJson = new QPushButton("导出结果 JSON", page);
    exportJson->setEnabled(false);
    layout->addWidget(exportJson);
    layout->addStretch();
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
    _seedSpin->setEnabled(enabled);
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
    _candidateModel->setCandidates(result.candidates);
    _statusLabel->setText(result.canceled ? "结构优化已取消。" : "结构优化已完成。");
}

void StructureOptimizerWidget::handleFailed(const QString& message)
{
    _statusLabel->setText(message);
}
