#include "EngineeringRequirementsWidget.hpp"

#include "RequirementCompiler.hpp"
#include "RequirementSetJson.hpp"

#include <rwslibs/robotmodelbuilder/RobotModelFingerprint.hpp>
#include <rwslibs/robotmodelbuilder/RobotModelSpecJson.hpp>

#include <rw/models/Device.hpp>
#include <rw/models/WorkCell.hpp>
#include <rw/kinematics/Frame.hpp>
#include <rw/kinematics/Kinematics.hpp>
#include <rw/math/RPY.hpp>

#include <QComboBox>
#include <QCheckBox>
#include <QColor>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QSignalBlocker>

#include <algorithm>

namespace rws {
namespace {

QString levelText(RequirementLevel level) { return QString::fromLatin1(toString(level)); }
RequirementLevel levelFromText(const QString& text) {
    RequirementLevel result = RequirementLevel::Must;
    requirementLevelFromString(text.toStdString(), result);
    return result;
}
QTableWidgetItem* textItem(const QString& text) { return new QTableWidgetItem(text); }
double number(const QTableWidget* table, int row, int column, double fallback = 0.0) {
    bool ok = false;
    const double value = table->item(row, column) == nullptr ? fallback : table->item(row, column)->text().toDouble(&ok);
    return ok ? value : fallback;
}
QString text(const QTableWidget* table, int row, int column, const QString& fallback = QString()) {
    return table->item(row, column) == nullptr ? fallback : table->item(row, column)->text();
}
QComboBox* levelCombo(RequirementLevel level) {
    QComboBox* combo = new QComboBox();
    combo->addItems({"Must", "Should", "Info"});
    combo->setCurrentText(levelText(level));
    return combo;
}
QComboBox* enumCombo(const QString& objectName, const std::initializer_list<QPair<QString, int>>& values) {
    QComboBox* combo = new QComboBox();
    combo->setObjectName(objectName);
    for (const QPair<QString, int>& value : values)
        combo->addItem(value.first, value.second);
    return combo;
}
QDoubleSpinBox* lengthSpinBox(const QString& objectName) {
    QDoubleSpinBox* spin = new QDoubleSpinBox();
    spin->setObjectName(objectName);
    spin->setRange(-1000.0, 1000.0);
    spin->setDecimals(4);
    spin->setSuffix(" m");
    return spin;
}
QDoubleSpinBox* angleSpinBox(const QString& objectName) {
    QDoubleSpinBox* spin = new QDoubleSpinBox();
    spin->setObjectName(objectName);
    spin->setRange(-360.0, 360.0);
    spin->setDecimals(2);
    spin->setSuffix(" deg");
    return spin;
}
} // namespace

EngineeringRequirementsWidget::EngineeringRequirementsWidget(QWidget* parent) : QWidget(parent)
{
    _tabs = new QTabWidget(this);
    _tabs->setObjectName("engineeringRequirementsTabs");
    _tabs->addTab(createPoseTaskPage(), QString::fromUtf8("关键工位"));
    _tabs->addTab(createBoxRegionPage(), QString::fromUtf8("工作区域"));
    _tabs->addTab(createValidationPage(), QString::fromUtf8("校验与冻结"));
    _statusLabel = new QLabel(QString::fromUtf8("请先绑定 .rmb.json 模型，再定义研发需求。"), this);
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->addWidget(_tabs);
    layout->addWidget(_statusLabel);
    refreshTables();
}

QWidget* EngineeringRequirementsWidget::createPoseTaskPage()
{
    QWidget* page = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(page);
    QHBoxLayout* actions = new QHBoxLayout();
    QPushButton* add = new QPushButton(QString::fromUtf8("新增工位"), page); add->setObjectName("addRequirementPoseTaskButton");
    QPushButton* duplicate = new QPushButton(QString::fromUtf8("复制工位"), page); duplicate->setObjectName("duplicateRequirementPoseTaskButton");
    QPushButton* remove = new QPushButton(QString::fromUtf8("删除工位"), page); remove->setObjectName("removeRequirementPoseTaskButton");
    QPushButton* capture = new QPushButton(QString::fromUtf8("从当前 TCP 捕获"), page); capture->setObjectName("captureRequirementTcpButton");
    actions->addWidget(add); actions->addWidget(duplicate); actions->addWidget(remove); actions->addWidget(capture); actions->addStretch();
    layout->addLayout(actions);
    QSplitter* splitter = new QSplitter(Qt::Horizontal, page);
    _stationList = new QListWidget(splitter); _stationList->setObjectName("keyStationList");
    _stationList->setMinimumWidth(220);
    QWidget* inspector = new QWidget(splitter);
    QVBoxLayout* inspectorLayout = new QVBoxLayout(inspector);
    QFormLayout* form = new QFormLayout();
    _stationNameEdit = new QLineEdit(inspector); _stationNameEdit->setObjectName("keyStationNameEdit");
    _stationProcessTypeCombo = enumCombo("keyStationProcessTypeCombo", {
        {QString::fromUtf8("通用"), static_cast<int>(ProcessType::Generic)}, {QString::fromUtf8("取料"), static_cast<int>(ProcessType::Pick)},
        {QString::fromUtf8("放料"), static_cast<int>(ProcessType::Place)}, {QString::fromUtf8("机床上料"), static_cast<int>(ProcessType::MachineLoad)},
        {QString::fromUtf8("机床下料"), static_cast<int>(ProcessType::MachineUnload)}, {QString::fromUtf8("检测"), static_cast<int>(ProcessType::Inspect)},
        {QString::fromUtf8("焊缝起点"), static_cast<int>(ProcessType::WeldStart)}, {QString::fromUtf8("焊缝终点"), static_cast<int>(ProcessType::WeldEnd)},
        {QString::fromUtf8("换工具"), static_cast<int>(ProcessType::ToolChange)}, {QString::fromUtf8("安全待机"), static_cast<int>(ProcessType::SafeStandby)},
        {QString::fromUtf8("人机交接"), static_cast<int>(ProcessType::Handover)}});
    _stationLevelCombo = enumCombo("keyStationRequirementLevelCombo", {{"Must", static_cast<int>(RequirementLevel::Must)}, {"Should", static_cast<int>(RequirementLevel::Should)}, {"Info", static_cast<int>(RequirementLevel::Info)}});
    _stationReferenceFrameCombo = new QComboBox(inspector); _stationReferenceFrameCombo->setObjectName("keyStationReferenceFrameCombo");
    _stationTcpFrameCombo = new QComboBox(inspector); _stationTcpFrameCombo->setObjectName("keyStationTcpFrameCombo");
    _stationOrientationModeCombo = enumCombo("keyStationOrientationModeCombo", {{QString::fromUtf8("固定姿态"), static_cast<int>(OrientationMode::Fixed)}, {QString::fromUtf8("对齐坐标系"), static_cast<int>(OrientationMode::AlignFrame)}, {QString::fromUtf8("对齐几何法向"), static_cast<int>(OrientationMode::AlignGeometryNormal)}, {QString::fromUtf8("指向目标"), static_cast<int>(OrientationMode::PointAtTarget)}});
    _stationOrientationTargetFrameCombo = new QComboBox(inspector); _stationOrientationTargetFrameCombo->setObjectName("keyStationOrientationTargetFrameCombo");
    _stationFreeRollCheck = new QCheckBox(QString::fromUtf8("允许工具绕轴自由滚转"), inspector); _stationFreeRollCheck->setObjectName("keyStationFreeRollCheck");
    form->addRow(QString::fromUtf8("名称"), _stationNameEdit); form->addRow(QString::fromUtf8("工艺类型"), _stationProcessTypeCombo);
    form->addRow(QString::fromUtf8("要求等级"), _stationLevelCombo); form->addRow(QString::fromUtf8("参考系"), _stationReferenceFrameCombo);
    form->addRow(QString::fromUtf8("TCP"), _stationTcpFrameCombo); form->addRow(QString::fromUtf8("姿态规则"), _stationOrientationModeCombo);
    form->addRow(QString::fromUtf8("姿态目标"), _stationOrientationTargetFrameCombo); form->addRow(QString(), _stationFreeRollCheck);
    inspectorLayout->addLayout(form);
    QGroupBox* pathGroup = new QGroupBox(QString::fromUtf8("接近与撤离"), inspector);
    QFormLayout* pathForm = new QFormLayout(pathGroup);
    _stationApproachEnabled = new QCheckBox(QString::fromUtf8("沿工具 Z 轴接近"), pathGroup); _stationApproachEnabled->setObjectName("keyStationApproachEnabled");
    _stationApproachDistance = lengthSpinBox("keyStationApproachDistance");
    _stationRetractEnabled = new QCheckBox(QString::fromUtf8("沿参考系 Z 轴撤离"), pathGroup); _stationRetractEnabled->setObjectName("keyStationRetractEnabled");
    _stationRetractDistance = lengthSpinBox("keyStationRetractDistance");
    _stationMinimumJointMargin = lengthSpinBox("keyStationMinimumJointMargin");
    pathForm->addRow(_stationApproachEnabled, _stationApproachDistance); pathForm->addRow(_stationRetractEnabled, _stationRetractDistance);
    pathForm->addRow(QString::fromUtf8("最小关节裕度"), _stationMinimumJointMargin);
    inspectorLayout->addWidget(pathGroup);
    _stationAdvancedPoseGroup = new QGroupBox(QString::fromUtf8("高级坐标（固定姿态）"), inspector); _stationAdvancedPoseGroup->setObjectName("keyStationAdvancedPoseGroup");
    QFormLayout* poseForm = new QFormLayout(_stationAdvancedPoseGroup);
    _stationX = lengthSpinBox("keyStationX"); _stationY = lengthSpinBox("keyStationY"); _stationZ = lengthSpinBox("keyStationZ");
    _stationRoll = angleSpinBox("keyStationRoll"); _stationPitch = angleSpinBox("keyStationPitch"); _stationYaw = angleSpinBox("keyStationYaw");
    poseForm->addRow("X", _stationX); poseForm->addRow("Y", _stationY); poseForm->addRow("Z", _stationZ);
    poseForm->addRow("Roll", _stationRoll); poseForm->addRow("Pitch", _stationPitch); poseForm->addRow("Yaw", _stationYaw);
    inspectorLayout->addWidget(_stationAdvancedPoseGroup); inspectorLayout->addStretch();
    splitter->addWidget(inspector); splitter->setStretchFactor(1, 1); layout->addWidget(splitter);
    connect(add, &QPushButton::clicked, this, &EngineeringRequirementsWidget::addPoseTask);
    connect(duplicate, &QPushButton::clicked, this, &EngineeringRequirementsWidget::duplicatePoseTask);
    connect(remove, &QPushButton::clicked, this, &EngineeringRequirementsWidget::removePoseTask);
    connect(capture, &QPushButton::clicked, this, &EngineeringRequirementsWidget::captureCurrentTcp);
    connect(_stationList, &QListWidget::currentRowChanged, this, &EngineeringRequirementsWidget::refreshKeyStationInspector);
    connect(_stationOrientationModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &EngineeringRequirementsWidget::updateOrientationEditor);
    connect(_stationNameEdit, &QLineEdit::editingFinished, this, &EngineeringRequirementsWidget::commitKeyStationInspector);
    for (QComboBox* combo : {_stationProcessTypeCombo, _stationLevelCombo, _stationReferenceFrameCombo, _stationTcpFrameCombo, _stationOrientationTargetFrameCombo})
        connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &EngineeringRequirementsWidget::commitKeyStationInspector);
    for (QCheckBox* check : {_stationFreeRollCheck, _stationApproachEnabled, _stationRetractEnabled})
        connect(check, &QCheckBox::toggled, this, &EngineeringRequirementsWidget::commitKeyStationInspector);
    for (QDoubleSpinBox* spin : {_stationApproachDistance, _stationRetractDistance, _stationMinimumJointMargin, _stationX, _stationY, _stationZ, _stationRoll, _stationPitch, _stationYaw})
        connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &EngineeringRequirementsWidget::commitKeyStationInspector);
    return page;
}

QWidget* EngineeringRequirementsWidget::createBoxRegionPage()
{
    QWidget* page = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(page);
    QHBoxLayout* actions = new QHBoxLayout();
    QPushButton* add = new QPushButton(QString::fromUtf8("新增覆盖盒"), page); add->setObjectName("addRequirementBoxRegionButton");
    QPushButton* duplicate = new QPushButton(QString::fromUtf8("复制覆盖盒"), page); duplicate->setObjectName("duplicateRequirementBoxRegionButton");
    QPushButton* remove = new QPushButton(QString::fromUtf8("删除覆盖盒"), page); remove->setObjectName("removeRequirementBoxRegionButton");
    actions->addWidget(add); actions->addWidget(duplicate); actions->addWidget(remove); actions->addStretch();
    layout->addLayout(actions);
    _regionTable = new QTableWidget(page); _regionTable->setObjectName("engineeringRequirementBoxTable");
    _regionTable->setColumnCount(11);
    _regionTable->setHorizontalHeaderLabels({"ID", "名称", "等级", "参考系", "中心 X", "中心 Y", "中心 Z", "尺寸 X", "尺寸 Y", "尺寸 Z", "最小覆盖率"});
    _regionTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    layout->addWidget(_regionTable);
    connect(add, &QPushButton::clicked, this, &EngineeringRequirementsWidget::addBoxRegion);
    connect(duplicate, &QPushButton::clicked, this, &EngineeringRequirementsWidget::duplicateBoxRegion);
    connect(remove, &QPushButton::clicked, this, &EngineeringRequirementsWidget::removeBoxRegion);
    return page;
}

QWidget* EngineeringRequirementsWidget::createValidationPage()
{
    QWidget* page = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(page);
    _modelLabel = new QLabel(page); _modelLabel->setObjectName("engineeringRequirementsModelBindingLabel");
    _modelLabel->setWordWrap(true);
    _freezeLabel = new QLabel(page); _freezeLabel->setObjectName("engineeringRequirementsFreezeLabel");
    _freezeLabel->setWordWrap(true);
    layout->addWidget(_modelLabel); layout->addWidget(_freezeLabel);
    QHBoxLayout* actions = new QHBoxLayout();
    QPushButton* bind = new QPushButton(QString::fromUtf8("绑定模型"), page); bind->setObjectName("bindRequirementModelButton");
    QPushButton* load = new QPushButton(QString::fromUtf8("加载需求"), page); load->setObjectName("loadRequirementSetButton");
    QPushButton* save = new QPushButton(QString::fromUtf8("保存需求"), page); save->setObjectName("saveRequirementSetButton");
    _freezeButton = new QPushButton(QString::fromUtf8("校验并冻结"), page); _freezeButton->setObjectName("freezeRequirementSetButton");
    QPushButton* unfreeze = new QPushButton(QString::fromUtf8("解冻编辑"), page); unfreeze->setObjectName("unfreezeRequirementSetButton");
    actions->addWidget(bind); actions->addWidget(load); actions->addWidget(save); actions->addWidget(_freezeButton); actions->addWidget(unfreeze); actions->addStretch();
    layout->addLayout(actions); layout->addStretch();
    connect(bind, &QPushButton::clicked, this, &EngineeringRequirementsWidget::bindModel);
    connect(load, &QPushButton::clicked, this, &EngineeringRequirementsWidget::loadRequirements);
    connect(save, &QPushButton::clicked, this, &EngineeringRequirementsWidget::saveRequirements);
    connect(_freezeButton, &QPushButton::clicked, this, &EngineeringRequirementsWidget::freezeRequirements);
    connect(unfreeze, &QPushButton::clicked, this, &EngineeringRequirementsWidget::unfreezeRequirements);
    return page;
}

void EngineeringRequirementsWidget::refreshTables()
{
    refreshFrameChoices();
    refreshKeyStationList();
    refreshKeyStationInspector();
    if (_regionTable != nullptr) {
        _regionTable->setRowCount(static_cast<int>(_requirements.boxRegions.size()));
        for (int row = 0; row < _regionTable->rowCount(); ++row) {
            const BoxRegion& region = _requirements.boxRegions[static_cast<std::size_t>(row)];
            _regionTable->setItem(row, 0, textItem(QString::fromStdString(region.id)));
            _regionTable->setItem(row, 1, textItem(QString::fromStdString(region.name)));
            _regionTable->setCellWidget(row, 2, levelCombo(region.level));
            _regionTable->setItem(row, 3, textItem(QString::fromStdString(region.refFrame)));
            for (int axis = 0; axis < 3; ++axis) {
                _regionTable->setItem(row, 4 + axis, textItem(QString::number(region.center[axis])));
                _regionTable->setItem(row, 7 + axis, textItem(QString::number(region.size[axis])));
            }
            _regionTable->setItem(row, 10, textItem(QString::number(region.minimumCoverage)));
        }
    }
    const bool editable = !_requirements.frozen;
    if (_regionTable != nullptr) _regionTable->setEnabled(editable);
    if (_freezeButton != nullptr) _freezeButton->setEnabled(editable);
    for (const char* name : {"addRequirementPoseTaskButton", "duplicateRequirementPoseTaskButton",
                             "removeRequirementPoseTaskButton", "captureRequirementTcpButton",
                             "addRequirementBoxRegionButton", "duplicateRequirementBoxRegionButton",
                             "removeRequirementBoxRegionButton"}) {
        if (QPushButton* button = findChild<QPushButton*>(name)) button->setEnabled(editable);
    }
    if (_modelLabel != nullptr)
        _modelLabel->setText(QString::fromUtf8("模型：%1\n指纹：%2").arg(QString::fromStdString(_requirements.modelBinding.sourcePath), QString::fromStdString(_requirements.modelBinding.robotModelFingerprint)));
    if (_freezeLabel != nullptr)
        _freezeLabel->setText(_requirements.frozen ? QString::fromUtf8("状态：已冻结。需求指纹：%1").arg(QString::fromStdString(_compiled.requirementFingerprint)) : QString::fromUtf8("状态：可编辑。冻结后才可作为下游分析和优化输入。"));
}

void EngineeringRequirementsWidget::syncTablesToRequirements()
{
    if (_requirements.frozen) return;
    commitKeyStationInspector();
    _requirements.boxRegions.clear();
    for (int row = 0; _regionTable != nullptr && row < _regionTable->rowCount(); ++row) {
        BoxRegion region; region.id = text(_regionTable, row, 0).toStdString(); region.name = text(_regionTable, row, 1).toStdString();
        region.level = levelFromText(qobject_cast<QComboBox*>(_regionTable->cellWidget(row, 2))->currentText());
        region.refFrame = text(_regionTable, row, 3, "WORLD").toStdString();
        for (int axis = 0; axis < 3; ++axis) { region.center[axis] = number(_regionTable, row, 4 + axis); region.size[axis] = number(_regionTable, row, 7 + axis, 0.1); }
        region.minimumCoverage = number(_regionTable, row, 10, 0.8); _requirements.boxRegions.push_back(region);
    }
}

int EngineeringRequirementsWidget::selectedKeyStationIndex() const
{
    return _stationList == nullptr ? -1 : _stationList->currentRow();
}

void EngineeringRequirementsWidget::refreshKeyStationList()
{
    if (_stationList == nullptr) return;
    const int previous = selectedKeyStationIndex();
    const QSignalBlocker blocker(_stationList);
    _stationList->clear();
    const std::vector<RequirementDiagnostic> diagnostics = RequirementCompiler::validateDetailed(_requirements);
    for (const PoseTask& task : _requirements.poseTasks) {
        const QString name = task.name.empty() ? QString::fromUtf8("未命名工位") : QString::fromStdString(task.name);
        QListWidgetItem* item = new QListWidgetItem(
            QString("%1  [%2]").arg(name, QString::fromLatin1(toString(task.processType))), _stationList);
        bool hasDiagnostic = false;
        bool hasBlockingDiagnostic = false;
        for (const RequirementDiagnostic& diagnostic : diagnostics) {
            if (diagnostic.requirementId != task.id) continue;
            hasDiagnostic = true;
            hasBlockingDiagnostic = hasBlockingDiagnostic || diagnostic.blocking;
        }
        if (task.level == RequirementLevel::Info) {
            item->setForeground(QColor(Qt::gray));
        } else if (hasBlockingDiagnostic) {
            item->setForeground(QColor(Qt::red));
        } else if (hasDiagnostic || _workcell == nullptr || _workcell->findFrame(task.refFrame) == nullptr ||
                   _workcell->findFrame(task.tcpFrame) == nullptr) {
            item->setForeground(QColor(180, 120, 0));
        } else {
            item->setForeground(QColor(0, 128, 0));
        }
    }
    if (!_requirements.poseTasks.empty())
        _stationList->setCurrentRow(std::clamp(previous, 0, static_cast<int>(_requirements.poseTasks.size()) - 1));
}

void EngineeringRequirementsWidget::refreshFrameChoices()
{
    if (_stationReferenceFrameCombo == nullptr || _stationTcpFrameCombo == nullptr ||
        _stationOrientationTargetFrameCombo == nullptr)
        return;

    const int selected = selectedKeyStationIndex();
    const PoseTask* task = selected >= 0 && selected < static_cast<int>(_requirements.poseTasks.size()) ?
        &_requirements.poseTasks[static_cast<std::size_t>(selected)] : nullptr;
    const QString reference = task == nullptr ? QStringLiteral("WORLD") : QString::fromStdString(task->refFrame);
    const QString tcp = task == nullptr ? QString() : QString::fromStdString(task->tcpFrame);
    const QString target = task == nullptr ? QString() : QString::fromStdString(task->orientation.targetFrame);
    const QSignalBlocker referenceBlocker(_stationReferenceFrameCombo);
    const QSignalBlocker tcpBlocker(_stationTcpFrameCombo);
    const QSignalBlocker targetBlocker(_stationOrientationTargetFrameCombo);
    _stationReferenceFrameCombo->clear();
    _stationTcpFrameCombo->clear();
    _stationOrientationTargetFrameCombo->clear();

    const auto addChoice = [] (QComboBox* combo, const QString& value) {
        if (!value.isEmpty() && combo->findData(value) < 0) combo->addItem(value, value);
    };
    addChoice(_stationReferenceFrameCombo, QStringLiteral("WORLD"));
    _stationOrientationTargetFrameCombo->addItem(QString::fromUtf8("未指定"), QString());
    if (_workcell != nullptr) {
        for (rw::kinematics::Frame* frame : _workcell->getFrames()) {
            if (frame == nullptr) continue;
            const QString name = QString::fromStdString(frame->getName());
            addChoice(_stationReferenceFrameCombo, name);
            addChoice(_stationOrientationTargetFrameCombo, name);
        }
        for (const rw::core::Ptr<rw::models::Device>& device : _workcell->getDevices()) {
            if (device != nullptr && device->getEnd() != nullptr)
                addChoice(_stationTcpFrameCombo, QString::fromStdString(device->getEnd()->getName()));
        }
    }
    const auto selectOrUnresolved = [] (QComboBox* combo, const QString& value, const QString& unresolvedPrefix) {
        if (value.isEmpty()) { combo->setCurrentIndex(0); return; }
        int index = combo->findData(value);
        if (index < 0) {
            combo->addItem(unresolvedPrefix + value, value);
            index = combo->count() - 1;
        }
        combo->setCurrentIndex(index);
    };
    selectOrUnresolved(_stationReferenceFrameCombo, reference, QString::fromUtf8("未解析："));
    selectOrUnresolved(_stationTcpFrameCombo, tcp, QString::fromUtf8("未解析："));
    selectOrUnresolved(_stationOrientationTargetFrameCombo, target, QString::fromUtf8("未解析："));
}

void EngineeringRequirementsWidget::refreshKeyStationInspector()
{
    const int selected = selectedKeyStationIndex();
    const bool hasSelection = selected >= 0 && selected < static_cast<int>(_requirements.poseTasks.size());
    const bool editable = hasSelection && !_requirements.frozen;
    for (QWidget* editor : {static_cast<QWidget*>(_stationNameEdit), static_cast<QWidget*>(_stationProcessTypeCombo),
                            static_cast<QWidget*>(_stationLevelCombo), static_cast<QWidget*>(_stationReferenceFrameCombo),
                            static_cast<QWidget*>(_stationTcpFrameCombo), static_cast<QWidget*>(_stationOrientationModeCombo),
                            static_cast<QWidget*>(_stationOrientationTargetFrameCombo), static_cast<QWidget*>(_stationFreeRollCheck),
                            static_cast<QWidget*>(_stationApproachEnabled), static_cast<QWidget*>(_stationRetractEnabled),
                            static_cast<QWidget*>(_stationApproachDistance), static_cast<QWidget*>(_stationRetractDistance),
                            static_cast<QWidget*>(_stationMinimumJointMargin), static_cast<QWidget*>(_stationAdvancedPoseGroup)})
        if (editor != nullptr) editor->setEnabled(editable);
    if (!hasSelection) return;

    refreshFrameChoices();
    const PoseTask& task = _requirements.poseTasks[static_cast<std::size_t>(selected)];
    const QSignalBlocker nameBlocker(_stationNameEdit), processBlocker(_stationProcessTypeCombo), levelBlocker(_stationLevelCombo);
    const QSignalBlocker modeBlocker(_stationOrientationModeCombo), rollBlocker(_stationFreeRollCheck);
    const QSignalBlocker approachBlocker(_stationApproachEnabled), retractBlocker(_stationRetractEnabled);
    const QSignalBlocker approachDistanceBlocker(_stationApproachDistance), retractDistanceBlocker(_stationRetractDistance);
    const QSignalBlocker jointMarginBlocker(_stationMinimumJointMargin), xBlocker(_stationX), yBlocker(_stationY), zBlocker(_stationZ);
    const QSignalBlocker rollValueBlocker(_stationRoll), pitchBlocker(_stationPitch), yawBlocker(_stationYaw);
    _stationNameEdit->setText(QString::fromStdString(task.name));
    _stationProcessTypeCombo->setCurrentIndex(_stationProcessTypeCombo->findData(static_cast<int>(task.processType)));
    _stationLevelCombo->setCurrentIndex(_stationLevelCombo->findData(static_cast<int>(task.level)));
    _stationOrientationModeCombo->setCurrentIndex(_stationOrientationModeCombo->findData(static_cast<int>(task.orientation.mode)));
    _stationFreeRollCheck->setChecked(task.orientation.allowToolRollFree);
    _stationApproachEnabled->setChecked(task.approach.enabled);
    _stationApproachDistance->setValue(task.approach.distanceMeters);
    _stationRetractEnabled->setChecked(task.retract.enabled);
    _stationRetractDistance->setValue(task.retract.distanceMeters);
    _stationMinimumJointMargin->setValue(task.validation.minimumJointMargin);
    _stationX->setValue(task.position[0]); _stationY->setValue(task.position[1]); _stationZ->setValue(task.position[2]);
    _stationRoll->setValue(task.rpyDeg[0]); _stationPitch->setValue(task.rpyDeg[1]); _stationYaw->setValue(task.rpyDeg[2]);
    updateOrientationEditor();
}

void EngineeringRequirementsWidget::commitKeyStationInspector()
{
    if (_requirements.frozen) return;
    const int selected = selectedKeyStationIndex();
    if (selected < 0 || selected >= static_cast<int>(_requirements.poseTasks.size())) return;
    PoseTask& task = _requirements.poseTasks[static_cast<std::size_t>(selected)];
    task.name = _stationNameEdit->text().trimmed().toStdString();
    task.processType = static_cast<ProcessType>(_stationProcessTypeCombo->currentData().toInt());
    task.level = static_cast<RequirementLevel>(_stationLevelCombo->currentData().toInt());
    task.refFrame = _stationReferenceFrameCombo->currentData().toString().toStdString();
    task.tcpFrame = _stationTcpFrameCombo->currentData().toString().toStdString();
    task.orientation.mode = static_cast<OrientationMode>(_stationOrientationModeCombo->currentData().toInt());
    task.orientation.targetFrame = _stationOrientationTargetFrameCombo->currentData().toString().toStdString();
    task.orientation.allowToolRollFree = _stationFreeRollCheck->isChecked();
    task.tolerance.allowToolRollFree = task.orientation.allowToolRollFree;
    task.approach.enabled = _stationApproachEnabled->isChecked();
    task.approach.axis = OffsetAxis::ToolZ;
    task.approach.distanceMeters = _stationApproachDistance->value();
    task.retract.enabled = _stationRetractEnabled->isChecked();
    task.retract.axis = OffsetAxis::ReferenceZ;
    task.retract.distanceMeters = _stationRetractDistance->value();
    task.validation.minimumJointMargin = _stationMinimumJointMargin->value();
    task.position = {{_stationX->value(), _stationY->value(), _stationZ->value()}};
    task.rpyDeg = {{_stationRoll->value(), _stationPitch->value(), _stationYaw->value()}};
    refreshKeyStationList();
}

void EngineeringRequirementsWidget::updateOrientationEditor()
{
    if (_stationAdvancedPoseGroup == nullptr || _stationOrientationModeCombo == nullptr) return;
    const bool fixed = static_cast<OrientationMode>(_stationOrientationModeCombo->currentData().toInt()) == OrientationMode::Fixed;
    _stationAdvancedPoseGroup->setVisible(fixed);
    if (_stationOrientationTargetFrameCombo != nullptr)
        _stationOrientationTargetFrameCombo->setVisible(!fixed);
    commitKeyStationInspector();
}

void EngineeringRequirementsWidget::bindModel()
{
    const QString path = QFileDialog::getOpenFileName(this, QString::fromUtf8("绑定机器人模型"), QString(), "Robot model (*.rmb.json)");
    if (path.isEmpty()) return;
    QFile file(path); if (!file.open(QFile::ReadOnly)) { setStatus(QString::fromUtf8("无法读取模型文件。")); return; }
    RobotModelSpec spec; std::string error;
    if (!RobotModelSpecJson::fromJson(file.readAll().toStdString(), spec, &error)) { setStatus(QString::fromStdString(error)); return; }
    _requirements.modelBinding.sourcePath = path.toStdString();
    _requirements.modelBinding.robotName = spec.robotName;
    _requirements.modelBinding.robotModelFingerprint = RobotModelFingerprint::canonicalSha256(spec);
    setStatus(QString::fromUtf8("已绑定模型，需求将使用模型内容指纹追溯。")); refreshTables();
}

void EngineeringRequirementsWidget::saveRequirements()
{
    syncTablesToRequirements();
    const QString path = QFileDialog::getSaveFileName(this, QString::fromUtf8("保存研发需求"), "requirements.requirements.json", "Requirement set (*.requirements.json)");
    if (path.isEmpty()) return;
    QFile file(path); if (!file.open(QFile::WriteOnly | QFile::Text)) { setStatus(QString::fromUtf8("无法保存需求文件。")); return; }
    file.write(QByteArray::fromStdString(RequirementSetJson::toJson(_requirements))); setStatus(QString::fromUtf8("研发需求已保存。"));
}

void EngineeringRequirementsWidget::loadRequirements()
{
    const QString path = QFileDialog::getOpenFileName(this, QString::fromUtf8("加载研发需求"), QString(), "Requirement set (*.requirements.json)");
    if (path.isEmpty()) return;
    QFile file(path); if (!file.open(QFile::ReadOnly)) { setStatus(QString::fromUtf8("无法读取需求文件。")); return; }
    RequirementSet parsed; std::string error;
    if (!RequirementSetJson::fromJson(file.readAll().toStdString(), parsed, &error)) { setStatus(QString::fromStdString(error)); return; }
    _requirements = parsed; _compiled = CompiledRequirementSet(); if (_requirements.frozen) RequirementCompiler::compile(_requirements, _compiled, &error);
    setStatus(QString::fromUtf8("研发需求已加载。")); refreshTables();
}

void EngineeringRequirementsWidget::freezeRequirements()
{
    syncTablesToRequirements(); std::string error; CompiledRequirementSet compiled;
    if (!RequirementCompiler::compile(_requirements, compiled, &error)) { setStatus(QString::fromStdString(error)); return; }
    _requirements.frozen = true;
    _compiled = compiled;
    const int pathPending = static_cast<int>(std::count_if(compiled.poseTasks.begin(), compiled.poseTasks.end(), [] (const CompiledPoseTask& task) {
        return task.pathValidationPending;
    }));
    setStatus(QString::fromUtf8("需求已校验并冻结：%1 个工位可用于 P2 运动学优化；%2 项建议需求未验证，未进入优化；%3 个接近/撤离规则已记录，连续 IK 与路径碰撞将在 P3 验证。")
        .arg(compiled.poseTasks.size()).arg(compiled.diagnostics.size()).arg(pathPending));
    refreshTables();
}
void EngineeringRequirementsWidget::unfreezeRequirements() { _requirements.frozen = false; _compiled = CompiledRequirementSet(); setStatus(QString::fromUtf8("需求已解冻，可继续编辑。")); refreshTables(); }
void EngineeringRequirementsWidget::addPoseTask()
{
    if (_requirements.frozen) return;
    syncTablesToRequirements();
    PoseTask task;
    task.id = "station_" + std::to_string(_requirements.poseTasks.size() + 1);
    task.name = QString::fromUtf8("关键工位 %1").arg(_requirements.poseTasks.size() + 1).toStdString();
    task.refFrame = "WORLD";
    _requirements.poseTasks.push_back(task);
    refreshTables();
    if (_stationList != nullptr) _stationList->setCurrentRow(static_cast<int>(_requirements.poseTasks.size()) - 1);
}

void EngineeringRequirementsWidget::duplicatePoseTask()
{
    if (_requirements.frozen) return;
    syncTablesToRequirements();
    const int row = selectedKeyStationIndex();
    if (row < 0 || row >= static_cast<int>(_requirements.poseTasks.size())) return;
    PoseTask copy = _requirements.poseTasks[static_cast<std::size_t>(row)];
    copy.id += "_copy";
    copy.name += " Copy";
    _requirements.poseTasks.insert(_requirements.poseTasks.begin() + row + 1, copy);
    refreshTables();
    if (_stationList != nullptr) _stationList->setCurrentRow(row + 1);
}

void EngineeringRequirementsWidget::removePoseTask()
{
    if (_requirements.frozen) return;
    syncTablesToRequirements();
    const int row = selectedKeyStationIndex();
    if (row < 0 || row >= static_cast<int>(_requirements.poseTasks.size())) return;
    _requirements.poseTasks.erase(_requirements.poseTasks.begin() + row);
    refreshTables();
}
void EngineeringRequirementsWidget::captureCurrentTcp()
{
    if (_requirements.frozen) return;
    if (_workcell == nullptr || _workcell->getDevices().empty()) {
        setStatus(QString::fromUtf8("无法捕获当前 TCP：未打开包含设备的 WorkCell。"));
        return;
    }
    const rw::core::Ptr<rw::models::Device> device = _workcell->getDevices().front();
    if (device == nullptr || device->getBase() == nullptr || device->getEnd() == nullptr) {
        setStatus(QString::fromUtf8("无法捕获当前 TCP：默认设备没有有效的 Base 或 TCP。"));
        return;
    }
    try {
        const rw::math::Transform3D<> baseTtcp = rw::kinematics::Kinematics::frameTframe(
            device->getBase(), device->getEnd(), _workcell->getDefaultState());
        const rw::math::RPY<> rpy(baseTtcp.R());
        syncTablesToRequirements();
        PoseTask task;
        task.id = "station_" + std::to_string(_requirements.poseTasks.size() + 1);
        task.name = QString::fromUtf8("TCP 捕获工位 %1").arg(_requirements.poseTasks.size() + 1).toStdString();
        task.source = PoseTaskSource::CapturedTcp;
        task.refFrame = device->getBase()->getName();
        task.tcpFrame = device->getEnd()->getName();
        for (int axis = 0; axis < 3; ++axis) {
            task.position[axis] = baseTtcp.P()[axis];
            task.rpyDeg[axis] = rpy[axis] * 180.0 / rw::math::Pi;
        }
        task.note = "Captured from current WorkCell TCP.";
        _requirements.poseTasks.push_back(task);
        setStatus(QString::fromUtf8("已从当前设备 TCP 捕获位姿；可在冻结前调整其等级和公差。"));
        refreshTables();
    } catch (const std::exception& exception) {
        setStatus(QString::fromUtf8("无法捕获当前 TCP：%1").arg(QString::fromUtf8(exception.what())));
    }
}
void EngineeringRequirementsWidget::addBoxRegion() { if (_requirements.frozen) return; syncTablesToRequirements(); BoxRegion region; region.id = "box_" + std::to_string(_requirements.boxRegions.size() + 1); region.name = QString::fromUtf8("工作区域 %1").arg(_requirements.boxRegions.size() + 1).toStdString(); _requirements.boxRegions.push_back(region); refreshTables(); }
void EngineeringRequirementsWidget::duplicateBoxRegion() { if (_requirements.frozen) return; syncTablesToRequirements(); const int row = _regionTable->currentRow(); if (row < 0 || row >= static_cast<int>(_requirements.boxRegions.size())) return; BoxRegion copy = _requirements.boxRegions[static_cast<std::size_t>(row)]; copy.id += "_copy"; copy.name += " Copy"; _requirements.boxRegions.insert(_requirements.boxRegions.begin() + row + 1, copy); refreshTables(); }
void EngineeringRequirementsWidget::removeBoxRegion() { if (_requirements.frozen) return; syncTablesToRequirements(); const int row = _regionTable->currentRow(); if (row < 0 || row >= static_cast<int>(_requirements.boxRegions.size())) return; _requirements.boxRegions.erase(_requirements.boxRegions.begin() + row); refreshTables(); }
void EngineeringRequirementsWidget::setWorkCell(rw::models::WorkCell* workcell) { _workcell = workcell; setStatus(workcell == nullptr ? QString::fromUtf8("当前未打开 WorkCell；引用 Frame 会显示为未解析。") : QString::fromUtf8("已连接当前 WorkCell。")); refreshTables(); }
RequirementSet EngineeringRequirementsWidget::requirementSet() const { return _requirements; }
QString EngineeringRequirementsWidget::statusText() const { return _statusLabel == nullptr ? QString() : _statusLabel->text(); }
void EngineeringRequirementsWidget::setStatus(const QString& text) { if (_statusLabel != nullptr) _statusLabel->setText(text); }

} // namespace rws
