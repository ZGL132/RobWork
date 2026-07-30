#include "EngineeringRequirementsWidget.hpp"

#include "RequirementCompiler.hpp"
#include "GeometryFeatureResolver.hpp"
#include "RequirementSetJson.hpp"
#include "StationImportService.hpp"
#include "StationTemplateService.hpp"

#include <rwslibs/robotmodelbuilder/RobotModelFingerprint.hpp>
#include <rwslibs/robotmodelbuilder/RobotModelSpecJson.hpp>

#include <rw/models/Device.hpp>
#include <rw/models/WorkCell.hpp>
#include <rw/kinematics/Frame.hpp>
#include <rw/kinematics/Kinematics.hpp>
#include <rw/math/RPY.hpp>
#include <rw/math/Rotation3D.hpp>

#include <QComboBox>
#include <QCheckBox>
#include <QColor>
#include <QDoubleSpinBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QInputDialog>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStringList>

#include <algorithm>
#include <cmath>

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

QDoubleSpinBox* nonNegativeLengthSpinBox(double value)
{
    QDoubleSpinBox* spin = lengthSpinBox(QString());
    spin->setRange(0.0, 1000.0);
    spin->setValue(value);
    return spin;
}

QSpinBox* stationCountSpinBox(int value)
{
    QSpinBox* spin = new QSpinBox();
    spin->setRange(1, 10000);
    spin->setValue(std::max(1, value));
    return spin;
}

void selectComboValue(QComboBox* combo, const QString& value)
{
    int index = combo->findData(value);
    if (index < 0 && !value.isEmpty()) {
        combo->addItem(value, value);
        index = combo->count() - 1;
    }
    if (index >= 0)
        combo->setCurrentIndex(index);
}

QComboBox* frameChoice(const rw::models::WorkCell* workcell, const QString& current, bool tcp)
{
    QComboBox* combo = new QComboBox();
    combo->setEditable(true);
    if (!tcp)
        combo->addItem("WORLD", "WORLD");
    if (workcell != nullptr) {
        if (tcp) {
            for (const rw::core::Ptr<rw::models::Device>& device : workcell->getDevices()) {
                if (device != nullptr && device->getEnd() != nullptr) {
                    const QString name = QString::fromStdString(device->getEnd()->getName());
                    combo->addItem(name, name);
                }
            }
        } else {
            for (rw::kinematics::Frame* frame : workcell->getFrames()) {
                if (frame == nullptr) continue;
                const QString name = QString::fromStdString(frame->getName());
                if (combo->findData(name) < 0)
                    combo->addItem(name, name);
            }
        }
    }
    selectComboValue(combo, current);
    return combo;
}

QString generationParameter(const PoseTask& station, const char* key, const QString& fallback = QString())
{
    const auto it = std::find_if(station.generation.parameters.begin(), station.generation.parameters.end(),
                                 [key] (const GenerationParameter& parameter) {
                                     return parameter.key == key;
                                 });
    return it == station.generation.parameters.end() ? fallback : QString::fromStdString(it->value);
}

int generationParameterInt(const PoseTask& station, const char* key, int fallback)
{
    bool ok = false;
    const int value = generationParameter(station, key).toInt(&ok);
    return ok ? value : fallback;
}

double generationParameterDouble(const PoseTask& station, const char* key, double fallback)
{
    bool ok = false;
    const double value = generationParameter(station, key).toDouble(&ok);
    return ok ? value : fallback;
}

bool templateKindFromGeneratorId(const std::string& generatorId, StationTemplateKind& kind)
{
    for (const StationTemplateKind candidate : {StationTemplateKind::BinPicking, StationTemplateKind::MachineTending,
                                                 StationTemplateKind::Palletizing, StationTemplateKind::Inspection,
                                                 StationTemplateKind::ToolChange, StationTemplateKind::Handover}) {
        if (generatorId == std::string(StationTemplateService::toString(candidate)) + ".v1") {
            kind = candidate;
            return true;
        }
    }
    return false;
}

void addTemplateKinds(QComboBox* combo)
{
    combo->addItem(QString::fromUtf8("料箱取料"), static_cast<int>(StationTemplateKind::BinPicking));
    combo->addItem(QString::fromUtf8("机床上下料"), static_cast<int>(StationTemplateKind::MachineTending));
    combo->addItem(QString::fromUtf8("码垛"), static_cast<int>(StationTemplateKind::Palletizing));
    combo->addItem(QString::fromUtf8("检测"), static_cast<int>(StationTemplateKind::Inspection));
    combo->addItem(QString::fromUtf8("换工具"), static_cast<int>(StationTemplateKind::ToolChange));
    combo->addItem(QString::fromUtf8("人机交接"), static_cast<int>(StationTemplateKind::Handover));
}

bool editTemplateRequest(QWidget* parent, const rw::models::WorkCell* workcell,
                         StationTemplateRequest& request, bool updateExisting, const QString& title)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(title);
    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    QFormLayout* form = new QFormLayout();
    QComboBox* kind = new QComboBox(&dialog);
    addTemplateKinds(kind);
    kind->setCurrentIndex(kind->findData(static_cast<int>(request.kind)));
    QLineEdit* instanceId = new QLineEdit(QString::fromStdString(request.instanceId), &dialog);
    instanceId->setReadOnly(updateExisting);
    QLineEdit* idPrefix = new QLineEdit(QString::fromStdString(request.idPrefix), &dialog);
    QLineEdit* namePrefix = new QLineEdit(QString::fromStdString(request.namePrefix), &dialog);
    QComboBox* referenceFrame = frameChoice(workcell, QString::fromStdString(request.referenceFrame), false);
    QComboBox* tcpFrame = frameChoice(workcell, QString::fromStdString(request.tcpFrame), true);
    QComboBox* level = new QComboBox(&dialog);
    level->addItem("Must", static_cast<int>(RequirementLevel::Must));
    level->addItem("Should", static_cast<int>(RequirementLevel::Should));
    level->addItem("Info", static_cast<int>(RequirementLevel::Info));
    level->setCurrentIndex(level->findData(static_cast<int>(request.level)));
    QDoubleSpinBox* offsetX = lengthSpinBox(QString()); offsetX->setValue(request.operationOffsetMeters[0]);
    QDoubleSpinBox* offsetY = lengthSpinBox(QString()); offsetY->setValue(request.operationOffsetMeters[1]);
    QDoubleSpinBox* offsetZ = lengthSpinBox(QString()); offsetZ->setValue(request.operationOffsetMeters[2]);
    QSpinBox* rows = stationCountSpinBox(request.rows);
    QSpinBox* columns = stationCountSpinBox(request.columns);
    QSpinBox* layers = stationCountSpinBox(request.layers);
    QDoubleSpinBox* rowSpacing = nonNegativeLengthSpinBox(request.rowSpacingMeters);
    QDoubleSpinBox* columnSpacing = nonNegativeLengthSpinBox(request.columnSpacingMeters);
    QDoubleSpinBox* layerSpacing = nonNegativeLengthSpinBox(request.layerSpacingMeters);
    QDoubleSpinBox* approach = nonNegativeLengthSpinBox(request.approachDistanceMeters);
    QDoubleSpinBox* retract = nonNegativeLengthSpinBox(request.retractDistanceMeters);
    QDoubleSpinBox* clearance = nonNegativeLengthSpinBox(request.clearanceMeters);
    form->addRow(QString::fromUtf8("模板类型"), kind);
    form->addRow(QString::fromUtf8("实例 ID"), instanceId);
    form->addRow(QString::fromUtf8("工位 ID 前缀"), idPrefix);
    form->addRow(QString::fromUtf8("工位名称前缀"), namePrefix);
    form->addRow(QString::fromUtf8("参考系"), referenceFrame);
    form->addRow("TCP", tcpFrame);
    form->addRow(QString::fromUtf8("需求等级"), level);
    form->addRow(QString::fromUtf8("作业偏置 X"), offsetX);
    form->addRow(QString::fromUtf8("作业偏置 Y"), offsetY);
    form->addRow(QString::fromUtf8("作业偏置 Z"), offsetZ);
    form->addRow(QString::fromUtf8("行数"), rows);
    form->addRow(QString::fromUtf8("列数"), columns);
    form->addRow(QString::fromUtf8("层数"), layers);
    form->addRow(QString::fromUtf8("行间距"), rowSpacing);
    form->addRow(QString::fromUtf8("列间距"), columnSpacing);
    form->addRow(QString::fromUtf8("层间距"), layerSpacing);
    form->addRow(QString::fromUtf8("接近距离"), approach);
    form->addRow(QString::fromUtf8("撤离距离"), retract);
    form->addRow(QString::fromUtf8("安全距离"), clearance);
    layout->addLayout(form);
    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return false;
    request.kind = static_cast<StationTemplateKind>(kind->currentData().toInt());
    request.instanceId = instanceId->text().trimmed().toStdString();
    request.idPrefix = idPrefix->text().trimmed().toStdString();
    request.namePrefix = namePrefix->text().trimmed().toStdString();
    request.referenceFrame = referenceFrame->currentText().trimmed().toStdString();
    request.tcpFrame = tcpFrame->currentText().trimmed().toStdString();
    request.level = static_cast<RequirementLevel>(level->currentData().toInt());
    request.operationOffsetMeters = {{offsetX->value(), offsetY->value(), offsetZ->value()}};
    request.rows = rows->value();
    request.columns = columns->value();
    request.layers = layers->value();
    request.rowSpacingMeters = rowSpacing->value();
    request.columnSpacingMeters = columnSpacing->value();
    request.layerSpacingMeters = layerSpacing->value();
    request.approachDistanceMeters = approach->value();
    request.retractDistanceMeters = retract->value();
    request.clearanceMeters = clearance->value();
    return true;
}

bool templateRequestFromStation(const PoseTask& station, StationTemplateRequest& request)
{
    if (!templateKindFromGeneratorId(station.generation.generatorId, request.kind))
        return false;
    request.instanceId = station.generation.instanceId;
    request.idPrefix = generationParameter(station, "idPrefix", QString::fromStdString(station.id)).toStdString();
    request.namePrefix = generationParameter(station, "namePrefix", QString::fromStdString(station.name)).toStdString();
    request.referenceFrame = generationParameter(station, "referenceFrame", QString::fromStdString(station.refFrame)).toStdString();
    request.tcpFrame = generationParameter(station, "tcpFrame", QString::fromStdString(station.tcpFrame)).toStdString();
    request.level = station.level;
    request.operationOffsetMeters = {{
        generationParameterDouble(station, "operationOffsetX", station.position[0]),
        generationParameterDouble(station, "operationOffsetY", station.position[1]),
        generationParameterDouble(station, "operationOffsetZ", station.position[2])
    }};
    request.rows = generationParameterInt(station, "rows", request.rows);
    request.columns = generationParameterInt(station, "columns", request.columns);
    request.layers = generationParameterInt(station, "layers", request.layers);
    request.rowSpacingMeters = generationParameterDouble(station, "rowSpacingMeters", request.rowSpacingMeters);
    request.columnSpacingMeters = generationParameterDouble(station, "columnSpacingMeters", request.columnSpacingMeters);
    request.layerSpacingMeters = generationParameterDouble(station, "layerSpacingMeters", request.layerSpacingMeters);
    request.approachDistanceMeters = generationParameterDouble(station, "approachDistanceMeters", request.approachDistanceMeters);
    request.retractDistanceMeters = generationParameterDouble(station, "retractDistanceMeters", request.retractDistanceMeters);
    request.clearanceMeters = generationParameterDouble(station, "clearanceMeters", request.clearanceMeters);
    return true;
}

void addArrayKinds(QComboBox* combo)
{
    combo->addItem(QString::fromUtf8("线性阵列"), static_cast<int>(StationArrayKind::Linear));
    combo->addItem(QString::fromUtf8("矩形阵列"), static_cast<int>(StationArrayKind::Rectangular));
    combo->addItem(QString::fromUtf8("圆周阵列"), static_cast<int>(StationArrayKind::Circular));
    combo->addItem(QString::fromUtf8("沿折线等距"), static_cast<int>(StationArrayKind::Polyline));
}

QString polylineText(const std::vector<std::array<double, 3>>& points)
{
    QStringList segments;
    for (const std::array<double, 3>& point : points)
        segments.push_back(QString::number(point[0], 'g', 12) + "," + QString::number(point[1], 'g', 12) + "," + QString::number(point[2], 'g', 12));
    return segments.join(';');
}

bool parsePolylineText(const QString& text, std::vector<std::array<double, 3>>& points)
{
    points.clear();
    const QStringList segments = text.split(';', Qt::SkipEmptyParts);
    for (const QString& segment : segments) {
        const QStringList values = segment.split(',', Qt::KeepEmptyParts);
        if (values.size() != 3)
            return false;
        std::array<double, 3> point;
        for (int axis = 0; axis < 3; ++axis) {
            bool ok = false;
            point[axis] = values[axis].trimmed().toDouble(&ok);
            if (!ok || !std::isfinite(point[axis]))
                return false;
        }
        points.push_back(point);
    }
    // UI 只负责文本转换；真正的零长度和弧长校验仍由核心服务统一执行。
    return points.size() >= 2;
}

bool editArrayRequest(QWidget* parent, StationArrayRequest& request)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(QString::fromUtf8("批量生成工位"));
    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    QFormLayout* form = new QFormLayout();
    QComboBox* kind = new QComboBox(&dialog);
    addArrayKinds(kind);
    kind->setCurrentIndex(kind->findData(static_cast<int>(request.kind)));
    QLineEdit* instanceId = new QLineEdit(QString::fromStdString(request.instanceId), &dialog);
    QLineEdit* idPrefix = new QLineEdit(QString::fromStdString(request.idPrefix), &dialog);
    QLineEdit* namePrefix = new QLineEdit(QString::fromStdString(request.namePrefix), &dialog);
    QSpinBox* primaryCount = stationCountSpinBox(request.primaryCount);
    QSpinBox* secondaryCount = stationCountSpinBox(request.secondaryCount);
    QDoubleSpinBox* primaryX = lengthSpinBox(QString()); primaryX->setValue(request.primaryStepMeters[0]);
    QDoubleSpinBox* primaryY = lengthSpinBox(QString()); primaryY->setValue(request.primaryStepMeters[1]);
    QDoubleSpinBox* primaryZ = lengthSpinBox(QString()); primaryZ->setValue(request.primaryStepMeters[2]);
    QDoubleSpinBox* secondaryX = lengthSpinBox(QString()); secondaryX->setValue(request.secondaryStepMeters[0]);
    QDoubleSpinBox* secondaryY = lengthSpinBox(QString()); secondaryY->setValue(request.secondaryStepMeters[1]);
    QDoubleSpinBox* secondaryZ = lengthSpinBox(QString()); secondaryZ->setValue(request.secondaryStepMeters[2]);
    QDoubleSpinBox* radius = nonNegativeLengthSpinBox(request.radiusMeters);
    QDoubleSpinBox* startAngle = angleSpinBox(QString()); startAngle->setValue(request.startAngleDeg);
    QDoubleSpinBox* endAngle = angleSpinBox(QString()); endAngle->setValue(request.endAngleDeg);
    QLineEdit* polyline = new QLineEdit(polylineText(request.polylinePointsMeters), &dialog);
    polyline->setPlaceholderText("x,y,z; x,y,z; ...");
    form->addRow(QString::fromUtf8("阵列类型"), kind);
    form->addRow(QString::fromUtf8("实例 ID"), instanceId);
    form->addRow(QString::fromUtf8("工位 ID 前缀"), idPrefix);
    form->addRow(QString::fromUtf8("工位名称前缀"), namePrefix);
    form->addRow(QString::fromUtf8("主方向数量"), primaryCount);
    form->addRow(QString::fromUtf8("次方向数量"), secondaryCount);
    form->addRow(QString::fromUtf8("主步长 X"), primaryX);
    form->addRow(QString::fromUtf8("主步长 Y"), primaryY);
    form->addRow(QString::fromUtf8("主步长 Z"), primaryZ);
    form->addRow(QString::fromUtf8("次步长 X"), secondaryX);
    form->addRow(QString::fromUtf8("次步长 Y"), secondaryY);
    form->addRow(QString::fromUtf8("次步长 Z"), secondaryZ);
    form->addRow(QString::fromUtf8("半径"), radius);
    form->addRow(QString::fromUtf8("起始角"), startAngle);
    form->addRow(QString::fromUtf8("终止角"), endAngle);
    form->addRow(QString::fromUtf8("折线点（m）"), polyline);
    layout->addLayout(form);
    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return false;
    request.kind = static_cast<StationArrayKind>(kind->currentData().toInt());
    request.instanceId = instanceId->text().trimmed().toStdString();
    request.idPrefix = idPrefix->text().trimmed().toStdString();
    request.namePrefix = namePrefix->text().trimmed().toStdString();
    request.primaryCount = primaryCount->value();
    request.secondaryCount = secondaryCount->value();
    request.primaryStepMeters = {{primaryX->value(), primaryY->value(), primaryZ->value()}};
    request.secondaryStepMeters = {{secondaryX->value(), secondaryY->value(), secondaryZ->value()}};
    request.radiusMeters = radius->value();
    request.startAngleDeg = startAngle->value();
    request.endAngleDeg = endAngle->value();
    if (request.kind == StationArrayKind::Polyline && !parsePolylineText(polyline->text(), request.polylinePointsMeters)) {
        QMessageBox::warning(parent, QString::fromUtf8("折线输入无效"),
                             QString::fromUtf8("折线必须包含至少两个点，每个点使用 x,y,z，点之间用分号分隔。"));
        return false;
    }
    return true;
}

std::string uniqueInstanceId(const RequirementSet& requirements, const std::string& prefix)
{
    const std::string base = prefix.empty() ? "generated" : prefix;
    for (int suffix = 1; ; ++suffix) {
        const std::string candidate = base + "_" + std::to_string(suffix);
        const bool exists = std::any_of(requirements.poseTasks.begin(), requirements.poseTasks.end(),
            [&candidate] (const PoseTask& station) { return station.generation.instanceId == candidate; });
        if (!exists)
            return candidate;
    }
}

std::string uniqueStationId(const RequirementSet& requirements, const std::string& prefix)
{
    const std::string base = prefix.empty() ? "station" : prefix;
    for (int suffix = 1; ; ++suffix) {
        const std::string candidate = suffix == 1 ? base : base + "_" + std::to_string(suffix);
        const bool exists = std::any_of(requirements.poseTasks.begin(), requirements.poseTasks.end(),
            [&candidate] (const PoseTask& station) { return station.id == candidate; });
        if (!exists)
            return candidate;
    }
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
    QPushButton* pickGeometry = new QPushButton(QString::fromUtf8("从 3D 拾取几何 Frame"), page);
    pickGeometry->setObjectName("pickRequirementGeometryFeatureButton");
    pickGeometry->setToolTip(QString::fromUtf8("点击后，在 3D 视图中按住 Ctrl 双击目标工装或工件。"));
    QPushButton* createTemplate = new QPushButton(QString::fromUtf8("工艺模板"), page);
    createTemplate->setObjectName("createRequirementTemplateButton");
    createTemplate->setToolTip(QString::fromUtf8("基于料箱取料、机床上下料等工艺模板批量创建工位"));
    QPushButton* updateTemplate = new QPushButton(QString::fromUtf8("更新模板"), page);
    updateTemplate->setObjectName("updateRequirementTemplateButton");
    updateTemplate->setToolTip(QString::fromUtf8("更新当前模板实例中仍保持关联的工位"));
    QPushButton* detachTemplate = new QPushButton(QString::fromUtf8("解除关联"), page);
    detachTemplate->setObjectName("detachRequirementTemplateButton");
    detachTemplate->setToolTip(QString::fromUtf8("将当前模板或阵列工位转为独立手工维护的工位"));
    QPushButton* createArray = new QPushButton(QString::fromUtf8("批量阵列"), page);
    createArray->setObjectName("createRequirementArrayButton");
    createArray->setToolTip(QString::fromUtf8("从当前工位生成线性、矩形或圆周阵列"));
    QPushButton* mirror = new QPushButton(QString::fromUtf8("镜像工位"), page);
    mirror->setObjectName("mirrorRequirementStationButton");
    mirror->setToolTip(QString::fromUtf8("以当前参考系原点为基准镜像固定姿态工位"));
    QPushButton* import = new QPushButton(QString::fromUtf8("导入工位"), page);
    import->setObjectName("importRequirementStationsButton");
    import->setToolTip(QString::fromUtf8("从 CSV 或 JSON 批量导入关键工位；任何错误记录均不会写入当前需求"));
    QPushButton* undo = new QPushButton(QString::fromUtf8("撤销批量操作"), page);
    undo->setObjectName("undoRequirementOperationButton");
    undo->setToolTip(QString::fromUtf8("恢复最近一次模板、阵列、镜像或导入操作前的完整需求快照"));
    actions->addWidget(add); actions->addWidget(duplicate); actions->addWidget(remove); actions->addWidget(capture); actions->addWidget(pickGeometry);
    actions->addWidget(createTemplate); actions->addWidget(updateTemplate); actions->addWidget(detachTemplate);
    actions->addWidget(createArray); actions->addWidget(mirror); actions->addWidget(import); actions->addWidget(undo); actions->addStretch();
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
    connect(pickGeometry, &QPushButton::clicked, this, &EngineeringRequirementsWidget::requestGeometryFeaturePick);
    connect(createTemplate, &QPushButton::clicked, this, &EngineeringRequirementsWidget::createTemplateStations);
    connect(updateTemplate, &QPushButton::clicked, this, &EngineeringRequirementsWidget::updateSelectedTemplateStations);
    connect(detachTemplate, &QPushButton::clicked, this, &EngineeringRequirementsWidget::detachSelectedTemplateStation);
    connect(createArray, &QPushButton::clicked, this, &EngineeringRequirementsWidget::createStationArray);
    connect(mirror, &QPushButton::clicked, this, &EngineeringRequirementsWidget::mirrorSelectedStation);
    connect(import, &QPushButton::clicked, this, &EngineeringRequirementsWidget::importStations);
    connect(undo, &QPushButton::clicked, this, &EngineeringRequirementsWidget::undoLastOperation);
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
                             "pickRequirementGeometryFeatureButton",
                             "createRequirementTemplateButton", "updateRequirementTemplateButton",
                              "detachRequirementTemplateButton", "createRequirementArrayButton",
                             "mirrorRequirementStationButton", "importRequirementStationsButton",
                             "addRequirementBoxRegionButton", "duplicateRequirementBoxRegionButton",
                             "removeRequirementBoxRegionButton"}) {
        if (QPushButton* button = findChild<QPushButton*>(name)) button->setEnabled(editable);
    }
    if (QPushButton* button = findChild<QPushButton*>("undoRequirementOperationButton"))
        button->setEnabled(editable && _undoStack.canUndo());
    if (_modelLabel != nullptr)
        _modelLabel->setText(QString::fromUtf8("模型：%1\n指纹：%2").arg(QString::fromStdString(_requirements.modelBinding.sourcePath), QString::fromStdString(_requirements.modelBinding.robotModelFingerprint)));
    if (_freezeLabel != nullptr)
        _freezeLabel->setText(_requirements.frozen ? QString::fromUtf8("状态：已冻结。需求指纹：%1").arg(QString::fromStdString(_compiled.requirementFingerprint)) : QString::fromUtf8("状态：可编辑。冻结后才可作为下游分析和优化输入。"));
}

void EngineeringRequirementsWidget::syncTablesToRequirements()
{
    if (_requirements.frozen) return;
    commitKeyStationInspector();
    if (_workcell != nullptr) {
        for (PoseTask& task : _requirements.poseTasks) {
            if (task.source != PoseTaskSource::GeometryFeature) continue;
            GeometryFeatureResolver::applyToStation(task.geometryFeature, *_workcell,
                                                    _workcell->getDefaultState(), task, nullptr);
        }
    }
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
        if (task.source == PoseTaskSource::GeometryFeature && _workcell != nullptr) {
            GeometryFeatureResolution resolution;
            if (!GeometryFeatureResolver::resolve(task.geometryFeature, task.refFrame, *_workcell,
                                                  _workcell->getDefaultState(), resolution, nullptr)) {
                hasDiagnostic = true;
                hasBlockingDiagnostic = hasBlockingDiagnostic || task.level == RequirementLevel::Must;
            }
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

    _refreshingKeyStationInspector = true;
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
    _refreshingKeyStationInspector = false;
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
    Q_EMIT requirementsChanged();
}

void EngineeringRequirementsWidget::updateOrientationEditor()
{
    if (_stationAdvancedPoseGroup == nullptr || _stationOrientationModeCombo == nullptr) return;
    const bool fixed = static_cast<OrientationMode>(_stationOrientationModeCombo->currentData().toInt()) == OrientationMode::Fixed;
    _stationAdvancedPoseGroup->setVisible(fixed);
    if (_stationOrientationTargetFrameCombo != nullptr)
        _stationOrientationTargetFrameCombo->setVisible(!fixed);
    if (!_refreshingKeyStationInspector) commitKeyStationInspector();
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
    _requirements = parsed; _compiled = CompiledRequirementSet(); _undoStack.clear(); if (_requirements.frozen) RequirementCompiler::compile(_requirements, _compiled, &error);
    setStatus(QString::fromUtf8("研发需求已加载。")); refreshTables();
    Q_EMIT requirementsChanged();
}

void EngineeringRequirementsWidget::importStations()
{
    if (_requirements.frozen) return;
    syncTablesToRequirements();
    const QString path = QFileDialog::getOpenFileName(this, QString::fromUtf8("导入关键工位"), QString(),
        "Station data (*.csv *.json);;CSV (*.csv);;JSON (*.json)");
    if (path.isEmpty()) return;
    QFile file(path);
    if (!file.open(QFile::ReadOnly | QFile::Text)) {
        setStatus(QString::fromUtf8("无法读取工位导入文件。"));
        return;
    }
    const RequirementSet before = _requirements;
    StationImportResult result;
    std::string error;
    const bool isJson = QFileInfo(path).suffix().compare("json", Qt::CaseInsensitive) == 0;
    const QByteArray content = file.readAll();
    const bool imported = isJson ? StationImportService::appendJson(_requirements, content.toStdString(), path.toStdString(), result, &error) :
                                   StationImportService::appendCsv(_requirements, content.toStdString(), path.toStdString(), result, &error);
    if (!imported) {
        QStringList details;
        for (const StationImportDiagnostic& diagnostic : result.diagnostics) {
            details.push_back(QString::fromUtf8("记录 %1：%2").arg(diagnostic.recordNumber).arg(QString::fromStdString(diagnostic.message)));
            if (details.size() == 8) break;
        }
        const QString message = details.isEmpty() ? QString::fromStdString(error) : details.join('\n');
        QMessageBox::warning(this, QString::fromUtf8("工位导入失败"), message);
        setStatus(QString::fromUtf8("导入未写入任何工位：请根据逐行诊断修正源文件后重试。"));
        return;
    }
    // 服务成功后才记录操作前快照，保证撤销栈中不出现失败导入或用户取消的伪操作。
    pushUndoSnapshot(before);
    refreshTables();
    setStatus(QString::fromUtf8("已从“%1”原子导入 %2 个关键工位；每个工位都保留来源文件和原始记录号。").arg(path).arg(result.importedCount));
    Q_EMIT requirementsChanged();
}

void EngineeringRequirementsWidget::undoLastOperation()
{
    if (_requirements.frozen) return;
    if (!_undoStack.undo(_requirements)) {
        setStatus(QString::fromUtf8("当前没有可撤销的批量操作。"));
        return;
    }
    // 撤销后原冻结编译产物不再可信，必须等待工程师重新校验并冻结。
    _compiled = CompiledRequirementSet();
    refreshTables();
    setStatus(QString::fromUtf8("已恢复最近一次批量操作前的完整需求快照。"));
    Q_EMIT requirementsChanged();
}

void EngineeringRequirementsWidget::freezeRequirements()
{
    syncTablesToRequirements(); std::string error; CompiledRequirementSet compiled;
    std::vector<std::string> unresolvedAdvisoryStations;
    for (const PoseTask& task : _requirements.poseTasks) {
        if (task.source != PoseTaskSource::GeometryFeature) continue;
        GeometryFeatureResolution resolution;
        const bool resolved = _workcell != nullptr && GeometryFeatureResolver::resolve(
            task.geometryFeature, task.refFrame, *_workcell, _workcell->getDefaultState(), resolution, &error);
        if (resolved) continue;
        const QString message = QString::fromUtf8("几何特征 Frame 无法解析：%1").arg(QString::fromStdString(task.id));
        if (task.level == RequirementLevel::Must) { setStatus(message); return; }
        unresolvedAdvisoryStations.push_back(task.id);
    }
    if (!RequirementCompiler::compile(_requirements, compiled, &error)) { setStatus(QString::fromStdString(error)); return; }
    for (const std::string& id : unresolvedAdvisoryStations) {
        compiled.poseTasks.erase(std::remove_if(compiled.poseTasks.begin(), compiled.poseTasks.end(),
            [&id] (const CompiledPoseTask& task) { return task.id == id; }), compiled.poseTasks.end());
        RequirementDiagnostic diagnostic;
        diagnostic.requirementId = id;
        diagnostic.level = RequirementLevel::Should;
        diagnostic.message = "Geometry feature frame is unresolved and was excluded from compiled tasks: " + id;
        diagnostic.blocking = false;
        compiled.diagnostics.push_back(diagnostic);
    }
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
    Q_EMIT requirementsChanged();
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
    Q_EMIT requirementsChanged();
}

void EngineeringRequirementsWidget::removePoseTask()
{
    if (_requirements.frozen) return;
    syncTablesToRequirements();
    const int row = selectedKeyStationIndex();
    if (row < 0 || row >= static_cast<int>(_requirements.poseTasks.size())) return;
    _requirements.poseTasks.erase(_requirements.poseTasks.begin() + row);
    refreshTables();
    Q_EMIT requirementsChanged();
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
        Q_EMIT requirementsChanged();
    } catch (const std::exception& exception) {
        setStatus(QString::fromUtf8("无法捕获当前 TCP：%1").arg(QString::fromUtf8(exception.what())));
    }
}
void EngineeringRequirementsWidget::requestGeometryFeaturePick()
{
    if (_requirements.frozen || _workcell == nullptr || selectedKeyStationIndex() < 0) {
        setStatus(QString::fromUtf8("请先打开 WorkCell、选择关键工位，并保持需求处于可编辑状态。"));
        return;
    }
    setStatus(QString::fromUtf8("几何拾取已启用：请在 3D 视图中按住 Ctrl 双击目标工装或工件的 Frame。"));
    Q_EMIT geometryFeaturePickRequested();
}
bool EngineeringRequirementsWidget::applyGeometryFeatureFrame(const QString& frameName, QString* error)
{
    if (_requirements.frozen || _workcell == nullptr) {
        const QString message = QString::fromUtf8("请在已打开 WorkCell 且需求未冻结时拾取几何特征。");
        if (error != nullptr) *error = message;
        setStatus(message);
        return false;
    }
    const int selected = selectedKeyStationIndex();
    if (selected < 0 || selected >= static_cast<int>(_requirements.poseTasks.size())) {
        const QString message = QString::fromUtf8("请先在关键工位列表中选择一个工位。");
        if (error != nullptr) *error = message;
        setStatus(message);
        return false;
    }
    PoseTask& station = _requirements.poseTasks[static_cast<std::size_t>(selected)];
    GeometryFeatureReference feature;
    feature.type = GeometryFeatureType::FramePlaneNormal;
    feature.frameName = frameName.toStdString();
    feature.objectName = feature.frameName;
    feature.geometryName = "FramePlaneNormal";
    std::string resolveError;
    if (!GeometryFeatureResolver::applyToStation(feature, *_workcell, _workcell->getDefaultState(), station, &resolveError)) {
        const QString message = QString::fromStdString(resolveError);
        if (error != nullptr) *error = message;
        setStatus(message);
        return false;
    }
    refreshTables();
    setStatus(QString::fromUtf8("已关联几何 Frame“%1”：作业位会随当前 WorkCell 重新解析；面法向姿态已记录，连续路径验证将在 P3 执行。").arg(frameName));
    Q_EMIT requirementsChanged();
    if (error != nullptr) error->clear();
    return true;
}
void EngineeringRequirementsWidget::createTemplateStations()
{
    if (_requirements.frozen) return;
    syncTablesToRequirements();
    StationTemplateRequest request;
    const int selected = selectedKeyStationIndex();
    if (selected >= 0 && selected < static_cast<int>(_requirements.poseTasks.size())) {
        const PoseTask& station = _requirements.poseTasks[static_cast<std::size_t>(selected)];
        request.referenceFrame = station.refFrame;
        request.tcpFrame = station.tcpFrame;
        request.operationOffsetMeters = station.position;
    }
    request.instanceId = uniqueInstanceId(_requirements, "template");
    request.idPrefix = "station";
    request.namePrefix = "Template";
    if (!editTemplateRequest(this, _workcell, request, false, QString::fromUtf8("创建工艺模板工位")))
        return;
    const RequirementSet before = _requirements;
    const int firstGeneratedRow = static_cast<int>(_requirements.poseTasks.size());
    std::string error;
    if (!StationTemplateService::appendTemplate(_requirements, request, &error)) {
        QMessageBox::warning(this, QString::fromUtf8("无法创建模板"), QString::fromStdString(error));
        return;
    }
    pushUndoSnapshot(before);
    refreshTables();
    if (_stationList != nullptr) _stationList->setCurrentRow(firstGeneratedRow);
    setStatus(QString::fromUtf8("已创建工艺模板实例“%1”，其生成工位保持关联，可在后续统一更新或解除关联。").arg(QString::fromStdString(request.instanceId)));
    Q_EMIT requirementsChanged();
}

void EngineeringRequirementsWidget::updateSelectedTemplateStations()
{
    if (_requirements.frozen) return;
    syncTablesToRequirements();
    const int selected = selectedKeyStationIndex();
    if (selected < 0 || selected >= static_cast<int>(_requirements.poseTasks.size())) {
        setStatus(QString::fromUtf8("请先选择一个仍与工艺模板保持关联的关键工位。"));
        return;
    }
    const PoseTask& station = _requirements.poseTasks[static_cast<std::size_t>(selected)];
    StationTemplateRequest request;
    if (!station.generation.linked || !templateRequestFromStation(station, request)) {
        setStatus(QString::fromUtf8("当前工位不是可更新的模板关联工位；阵列和已解除关联的工位不会被模板更新覆盖。"));
        return;
    }
    if (!editTemplateRequest(this, _workcell, request, true, QString::fromUtf8("更新工艺模板")))
        return;
    TemplateUpdatePreview preview;
    std::string error;
    if (!StationTemplateService::previewTemplateUpdate(_requirements, station.generation.instanceId, request, preview, &error)) {
        QMessageBox::warning(this, QString::fromUtf8("无法预览模板更新"), QString::fromStdString(error));
        return;
    }
    const QString message = QString::fromUtf8("本次更新将替换 %1 个仍保持关联的工位，并生成 %2 个新工位。已解除关联的工位不会修改。是否继续？")
        .arg(preview.replacedStationIds.size()).arg(preview.generatedStations.size());
    if (QMessageBox::question(this, QString::fromUtf8("确认模板更新"), message,
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;
    const RequirementSet before = _requirements;
    if (!StationTemplateService::applyTemplateUpdate(_requirements, preview, &error)) {
        QMessageBox::warning(this, QString::fromUtf8("无法应用模板更新"), QString::fromStdString(error));
        return;
    }
    pushUndoSnapshot(before);
    refreshTables();
    setStatus(QString::fromUtf8("模板实例“%1”已更新；解除关联的工位保持原样。").arg(QString::fromStdString(request.instanceId)));
    Q_EMIT requirementsChanged();
}

void EngineeringRequirementsWidget::detachSelectedTemplateStation()
{
    if (_requirements.frozen) return;
    syncTablesToRequirements();
    const int selected = selectedKeyStationIndex();
    if (selected < 0 || selected >= static_cast<int>(_requirements.poseTasks.size())) {
        setStatus(QString::fromUtf8("请先选择一个由模板或阵列生成的关键工位。"));
        return;
    }
    PoseTask& station = _requirements.poseTasks[static_cast<std::size_t>(selected)];
    if (station.generation.instanceId.empty()) {
        setStatus(QString::fromUtf8("当前工位不是由模板或阵列生成，已经是独立工位。"));
        return;
    }
    if (!station.generation.linked) {
        setStatus(QString::fromUtf8("当前工位已经解除关联，之后的模板更新不会覆盖它。"));
        return;
    }
    const RequirementSet before = _requirements;
    std::string error;
    if (!StationTemplateService::detachStation(_requirements, station.id, &error)) {
        QMessageBox::warning(this, QString::fromUtf8("无法解除关联"), QString::fromStdString(error));
        return;
    }
    pushUndoSnapshot(before);
    refreshTables();
    setStatus(QString::fromUtf8("工位“%1”已解除与模板的关联，可单独调整且不会被后续模板更新覆盖。").arg(QString::fromStdString(station.name)));
    Q_EMIT requirementsChanged();
}

void EngineeringRequirementsWidget::createStationArray()
{
    if (_requirements.frozen) return;
    syncTablesToRequirements();
    const int selected = selectedKeyStationIndex();
    if (selected < 0 || selected >= static_cast<int>(_requirements.poseTasks.size())) {
        setStatus(QString::fromUtf8("请先选择一个关键工位，批量阵列会从该工位复制工艺语义、姿态规则和验证要求。"));
        return;
    }
    const PoseTask source = _requirements.poseTasks[static_cast<std::size_t>(selected)];
    StationArrayRequest request;
    request.instanceId = uniqueInstanceId(_requirements, source.id + "_array");
    request.idPrefix = source.id + "_array";
    request.namePrefix = source.name + " array";
    request.polylinePointsMeters = {source.position, {{source.position[0] + request.primaryStepMeters[0],
                                                       source.position[1] + request.primaryStepMeters[1],
                                                       source.position[2] + request.primaryStepMeters[2]}}};
    if (!editArrayRequest(this, request))
        return;
    const RequirementSet before = _requirements;
    const int firstGeneratedRow = static_cast<int>(_requirements.poseTasks.size());
    std::string error;
    if (!StationTemplateService::appendArray(_requirements, source.id, request, &error)) {
        QMessageBox::warning(this, QString::fromUtf8("无法生成阵列"), QString::fromStdString(error));
        return;
    }
    pushUndoSnapshot(before);
    refreshTables();
    if (_stationList != nullptr) _stationList->setCurrentRow(firstGeneratedRow);
    setStatus(QString::fromUtf8("已从工位“%1”生成阵列实例“%2”；阵列工位独立维护，不受模板更新影响。")
        .arg(QString::fromStdString(source.name), QString::fromStdString(request.instanceId)));
    Q_EMIT requirementsChanged();
}

void EngineeringRequirementsWidget::mirrorSelectedStation()
{
    if (_requirements.frozen) return;
    syncTablesToRequirements();
    const int selected = selectedKeyStationIndex();
    if (selected < 0 || selected >= static_cast<int>(_requirements.poseTasks.size())) {
        setStatus(QString::fromUtf8("请先选择需要镜像的关键工位。"));
        return;
    }
    const PoseTask& source = _requirements.poseTasks[static_cast<std::size_t>(selected)];
    if (source.orientation.mode != OrientationMode::Fixed) {
        setStatus(QString::fromUtf8("当前镜像只支持固定姿态工位；基于 Frame、几何法向或目标指向的姿态规则需要在 P3 结合场景镜像后再解析。"));
        return;
    }
    bool accepted = false;
    const QStringList planes = {"YZ (X=0)", "XZ (Y=0)", "XY (Z=0)"};
    const QString selectedPlane = QInputDialog::getItem(this, QString::fromUtf8("镜像关键工位"),
        QString::fromUtf8("以当前参考系原点为基准的镜像平面"), planes, 0, false, &accepted);
    if (!accepted)
        return;
    const int axis = planes.indexOf(selectedPlane);
    if (axis < 0)
        return;
    const RequirementSet before = _requirements;
    rw::math::Rotation3D<> reflection = axis == 0 ? rw::math::Rotation3D<>(-1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0) :
        (axis == 1 ? rw::math::Rotation3D<>(1.0, 0.0, 0.0, 0.0, -1.0, 0.0, 0.0, 0.0, 1.0) :
                     rw::math::Rotation3D<>(1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, -1.0));
    const rw::math::Rotation3D<> rotation = rw::math::RPY<>(source.rpyDeg[0] * rw::math::Pi / 180.0,
        source.rpyDeg[1] * rw::math::Pi / 180.0, source.rpyDeg[2] * rw::math::Pi / 180.0).toRotation3D();
    const rw::math::RPY<> mirroredRpy(reflection * rotation * reflection);
    PoseTask mirrored = source;
    mirrored.id = uniqueStationId(_requirements, source.id + "_mirror");
    mirrored.name += " mirror";
    mirrored.source = PoseTaskSource::FrameOffset;
    mirrored.geometryFeature = GeometryFeatureReference();
    mirrored.generation = StationGenerationProvenance();
    mirrored.position[axis] = -mirrored.position[axis];
    for (int angle = 0; angle < 3; ++angle)
        mirrored.rpyDeg[angle] = mirroredRpy[angle] * 180.0 / rw::math::Pi;
    mirrored.note = "Mirrored from station " + source.id + " about " + selectedPlane.toStdString() + ".";
    _requirements.poseTasks.push_back(mirrored);
    pushUndoSnapshot(before);
    refreshTables();
    if (_stationList != nullptr) _stationList->setCurrentRow(static_cast<int>(_requirements.poseTasks.size()) - 1);
    setStatus(QString::fromUtf8("已创建镜像工位“%1”；位置和固定姿态均已在当前参考系内进行合法镜像。")
        .arg(QString::fromStdString(mirrored.name)));
    Q_EMIT requirementsChanged();
}

void EngineeringRequirementsWidget::addBoxRegion() { if (_requirements.frozen) return; syncTablesToRequirements(); BoxRegion region; region.id = "box_" + std::to_string(_requirements.boxRegions.size() + 1); region.name = QString::fromUtf8("工作区域 %1").arg(_requirements.boxRegions.size() + 1).toStdString(); _requirements.boxRegions.push_back(region); refreshTables(); }
void EngineeringRequirementsWidget::duplicateBoxRegion() { if (_requirements.frozen) return; syncTablesToRequirements(); const int row = _regionTable->currentRow(); if (row < 0 || row >= static_cast<int>(_requirements.boxRegions.size())) return; BoxRegion copy = _requirements.boxRegions[static_cast<std::size_t>(row)]; copy.id += "_copy"; copy.name += " Copy"; _requirements.boxRegions.insert(_requirements.boxRegions.begin() + row + 1, copy); refreshTables(); }
void EngineeringRequirementsWidget::removeBoxRegion() { if (_requirements.frozen) return; syncTablesToRequirements(); const int row = _regionTable->currentRow(); if (row < 0 || row >= static_cast<int>(_requirements.boxRegions.size())) return; _requirements.boxRegions.erase(_requirements.boxRegions.begin() + row); refreshTables(); }
void EngineeringRequirementsWidget::setWorkCell(rw::models::WorkCell* workcell) { _workcell = workcell; setStatus(workcell == nullptr ? QString::fromUtf8("当前未打开 WorkCell；引用 Frame 会显示为未解析。") : QString::fromUtf8("已连接当前 WorkCell。")); refreshTables(); }
RequirementSet EngineeringRequirementsWidget::requirementSet() const { return _requirements; }
QString EngineeringRequirementsWidget::statusText() const { return _statusLabel == nullptr ? QString() : _statusLabel->text(); }
void EngineeringRequirementsWidget::pushUndoSnapshot(const RequirementSet& snapshot)
{
    // 只由成功的批量操作调用，撤销粒度与工程师一次明确意图保持一致。
    _undoStack.pushSnapshot(snapshot);
}
void EngineeringRequirementsWidget::setStatus(const QString& text) { if (_statusLabel != nullptr) _statusLabel->setText(text); }

} // namespace rws
