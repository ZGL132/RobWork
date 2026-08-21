#include "EngineeringRequirementsWidget.hpp"

#include "RequirementCompiler.hpp"
#include "RequirementFreezer.hpp"
#include "RequirementMigration.hpp"
#include <rwslibs/robotanalysiscore/RequirementExecutionJson.hpp>
#include "GeometryFeatureResolver.hpp"
#include "OrientationRuleResolver.hpp"
#include "RequirementSetJson.hpp"
#include "StationImportService.hpp"
#include "StationTemplateService.hpp"

#include <rwslibs/robotmodelbuilder/RobotModelFingerprint.hpp>
#include <rwslibs/robotmodelbuilder/RobotModelProjectPaths.hpp>
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
#include <QDir>
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
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QSizePolicy>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <set>

namespace rws {

namespace {

/**
 * @brief 工艺模板专属参数在 FormLayout 中的可见性位定义。
 *
 * 位值而非多个分散布尔值使“模板类型 -> 参数集合”的映射可以被单元测试覆盖；
 * 同时让 UI 层只负责显示/隐藏，不承担工艺模板生成算法的判断职责。
 */
enum TemplateParameterVisibility : unsigned int {
    TemplateParameterRows = 1U << 0,
    TemplateParameterColumns = 1U << 1,
    TemplateParameterLayers = 1U << 2,
    TemplateParameterRowSpacing = 1U << 3,
    TemplateParameterColumnSpacing = 1U << 4,
    TemplateParameterLayerSpacing = 1U << 5,
    TemplateParameterApproach = 1U << 6,
    TemplateParameterRetract = 1U << 7,
    TemplateParameterClearance = 1U << 8
};

} // namespace

unsigned int templateParameterVisibilityMask(StationTemplateKind kind)
{
    switch (kind) {
        case StationTemplateKind::BinPicking:
            // 料箱取料按层、行、列生成离散取料点，只消费阵列尺寸和三向间距。
            return TemplateParameterRows | TemplateParameterColumns | TemplateParameterLayers |
                   TemplateParameterRowSpacing | TemplateParameterColumnSpacing |
                   TemplateParameterLayerSpacing;
        case StationTemplateKind::MachineTending:
            // 机床上下料围绕同一作业点展开待机、接近和撤离动作，不生成网格阵列。
            return TemplateParameterApproach | TemplateParameterRetract | TemplateParameterClearance;
        case StationTemplateKind::Palletizing:
            // 码垛按工程师定义的行、列、层数生成，三向间距决定各放置点的中心对称位置。
            return TemplateParameterRows | TemplateParameterColumns | TemplateParameterLayers |
                   TemplateParameterRowSpacing |
                   TemplateParameterColumnSpacing | TemplateParameterLayerSpacing;
        case StationTemplateKind::Inspection:
        case StationTemplateKind::ToolChange:
        case StationTemplateKind::Handover:
            // 这些模板仅生成一个作业点，专属阵列和路径距离参数均不会参与生成。
            return 0U;
    }
    return 0U;
}

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
int positiveSampleCount(const QTableWidget* table, int row, int column, int fallback)
{
    // 覆盖盒的每轴采样点数会改变离散网格规模，因而属于影响覆盖率结论的工程输入，不能在
    // 表格同步时静默忽略。P2 覆盖率至少需要两个端点才能形成一个有尺寸的轴向区间；非整数、
    // 空值和小于 2 的输入统一回退到安全下限，避免后续评价器收到退化网格。
    bool ok = false;
    const int value = table->item(row, column) == nullptr
        ? fallback
        : table->item(row, column)->text().toInt(&ok);
    // 先取合法值或回退到 fallback，再钳制到 [2, MaxWorkspaceSamplesPerAxis]。
    // 上限与 RequirementCompiler 的采样密度安全上限保持一致，防止用户在 UI 中
    // 输入超大网格数，导致下游采样分析出现无界计算。
    const int safeValue = ok ? value : fallback;
    return std::min(MaxWorkspaceSamplesPerAxis,
                     std::max(2, safeValue));
}
QString text(const QTableWidget* table, int row, int column, const QString& fallback = QString()) {
    return table->item(row, column) == nullptr ? fallback : table->item(row, column)->text();
}

// 计算工位/覆盖盒的默认 TCP 帧名。
// preferredRobotName 非空时优先取"绑定模型对应的设备"末端作为默认 TCP：这样
// 默认值始终落在绑定机器人上，与冻结器的 WRONG_DEVICE 归属校验一致。若绑定设备
// 不存在则返回空串(交由调用方按未解析处理)；未指定绑定设备时回退到第一个有末端
// 的设备，保持旧行为。
std::string defaultTcpFrame(const rw::models::WorkCell* workcell,
                            const std::string& preferredRobotName = std::string())
{
    if (workcell == nullptr) return std::string();
    if (!preferredRobotName.empty()) {
        const rw::core::Ptr<rw::models::Device> preferred =
            workcell->findDevice(preferredRobotName);
        if (preferred != nullptr && preferred->getEnd() != nullptr)
            return preferred->getEnd()->getName();
        return std::string();
    }
    for (const rw::core::Ptr<rw::models::Device>& device : workcell->getDevices()) {
        if (device != nullptr && device->getEnd() != nullptr)
            return device->getEnd()->getName();
    }
    return std::string();
}

bool hasSameInspectorEditableValues(const PoseTask& left, const PoseTask& right)
{
    // 属性检查器只编辑工位的这一组字段。显式比较可避免控件刷新、重复信号或选择切换时
    // 向撤销栈写入没有语义变化的冗余快照，使一次真实编辑严格对应一次可撤销操作。
    return left.name == right.name &&
           left.processType == right.processType &&
           left.level == right.level &&
           left.refFrame == right.refFrame &&
           left.tcpFrame == right.tcpFrame &&
           left.orientation.mode == right.orientation.mode &&
           left.orientation.targetFrame == right.orientation.targetFrame &&
           left.orientation.targetPoint == right.orientation.targetPoint &&
           left.orientation.allowToolRollFree == right.orientation.allowToolRollFree &&
           left.tolerance.allowToolRollFree == right.tolerance.allowToolRollFree &&
           left.approach.enabled == right.approach.enabled &&
           left.approach.axis == right.approach.axis &&
           left.approach.distanceMeters == right.approach.distanceMeters &&
           left.retract.enabled == right.retract.enabled &&
           left.retract.axis == right.retract.axis &&
           left.retract.distanceMeters == right.retract.distanceMeters &&
           left.validation.minimumJointMargin == right.validation.minimumJointMargin &&
           left.position == right.position &&
           left.rpyDeg == right.rpyDeg;
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
    combo->addItem(QStringLiteral("Bin Picking"), static_cast<int>(StationTemplateKind::BinPicking));
    combo->addItem(QStringLiteral("Machine Tending"), static_cast<int>(StationTemplateKind::MachineTending));
    combo->addItem(QStringLiteral("Palletizing"), static_cast<int>(StationTemplateKind::Palletizing));
    combo->addItem(QStringLiteral("Inspection"), static_cast<int>(StationTemplateKind::Inspection));
    combo->addItem(QStringLiteral("Tool Change"), static_cast<int>(StationTemplateKind::ToolChange));
    combo->addItem(QStringLiteral("Handover"), static_cast<int>(StationTemplateKind::Handover));
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
    form->addRow(QStringLiteral("Template Type"), kind);
    form->addRow(QStringLiteral("Instance ID"), instanceId);
    form->addRow(QStringLiteral("Station ID Prefix"), idPrefix);
    form->addRow(QStringLiteral("Station Name Prefix"), namePrefix);
    form->addRow(QStringLiteral("Reference Frame"), referenceFrame);
    form->addRow("TCP", tcpFrame);
    form->addRow(QStringLiteral("Requirement Level"), level);
    form->addRow(QStringLiteral("Operation Offset X"), offsetX);
    form->addRow(QStringLiteral("Operation Offset Y"), offsetY);
    form->addRow(QStringLiteral("Operation Offset Z"), offsetZ);

    // 记录专属参数所在的 FormLayout 行。后续只切换行的可见性，不销毁控件或覆盖
    // 其现有值，因此工程师在模板之间比较方案时，切回原模板仍可保留已输入的数据。
    const int rowsRow = form->rowCount();
    form->addRow(QStringLiteral("Rows"), rows);
    const int columnsRow = form->rowCount();
    form->addRow(QStringLiteral("Columns"), columns);
    const int layersRow = form->rowCount();
    form->addRow(QStringLiteral("Layers"), layers);
    const int rowSpacingRow = form->rowCount();
    form->addRow(QStringLiteral("Row Spacing"), rowSpacing);
    const int columnSpacingRow = form->rowCount();
    form->addRow(QStringLiteral("Column Spacing"), columnSpacing);
    const int layerSpacingRow = form->rowCount();
    form->addRow(QStringLiteral("Layer Spacing"), layerSpacing);
    const int approachRow = form->rowCount();
    form->addRow(QStringLiteral("Approach Distance"), approach);
    const int retractRow = form->rowCount();
    form->addRow(QStringLiteral("Retract Distance"), retract);
    const int clearanceRow = form->rowCount();
    form->addRow(QStringLiteral("Clearance"), clearance);

    const auto updateTemplateParameterRows = [form, kind, rowsRow, columnsRow, layersRow,
                                              rowSpacingRow, columnSpacingRow, layerSpacingRow,
                                              approachRow, retractRow, clearanceRow] () {
        const StationTemplateKind selectedKind =
            static_cast<StationTemplateKind>(kind->currentData().toInt());
        const unsigned int visibleParameters = templateParameterVisibilityMask(selectedKind);

        // setRowVisible 同时处理字段标签和编辑器，避免只隐藏编辑器后留下无意义标签。
        form->setRowVisible(rowsRow, (visibleParameters & TemplateParameterRows) != 0U);
        form->setRowVisible(columnsRow, (visibleParameters & TemplateParameterColumns) != 0U);
        form->setRowVisible(layersRow, (visibleParameters & TemplateParameterLayers) != 0U);
        form->setRowVisible(rowSpacingRow, (visibleParameters & TemplateParameterRowSpacing) != 0U);
        form->setRowVisible(columnSpacingRow, (visibleParameters & TemplateParameterColumnSpacing) != 0U);
        form->setRowVisible(layerSpacingRow, (visibleParameters & TemplateParameterLayerSpacing) != 0U);
        form->setRowVisible(approachRow, (visibleParameters & TemplateParameterApproach) != 0U);
        form->setRowVisible(retractRow, (visibleParameters & TemplateParameterRetract) != 0U);
        form->setRowVisible(clearanceRow, (visibleParameters & TemplateParameterClearance) != 0U);
    };
    QObject::connect(kind, QOverload<int>::of(&QComboBox::currentIndexChanged), &dialog,
                     [updateTemplateParameterRows] (int) { updateTemplateParameterRows(); });
    updateTemplateParameterRows();

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
    combo->addItem(QStringLiteral("Linear"), static_cast<int>(StationArrayKind::Linear));
    combo->addItem(QStringLiteral("Rectangular"), static_cast<int>(StationArrayKind::Rectangular));
    combo->addItem(QStringLiteral("Circular"), static_cast<int>(StationArrayKind::Circular));
    combo->addItem(QStringLiteral("Polyline"), static_cast<int>(StationArrayKind::Polyline));
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
    dialog.setWindowTitle(QStringLiteral("Generate Station Array"));
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
    form->addRow(QStringLiteral("Array Type"), kind);
    form->addRow(QStringLiteral("Instance ID"), instanceId);
    form->addRow(QStringLiteral("Station ID Prefix"), idPrefix);
    form->addRow(QStringLiteral("Station Name Prefix"), namePrefix);
    form->addRow(QStringLiteral("Primary Count"), primaryCount);
    form->addRow(QStringLiteral("Secondary Count"), secondaryCount);
    form->addRow(QStringLiteral("Primary Step X"), primaryX);
    form->addRow(QStringLiteral("Primary Step Y"), primaryY);
    form->addRow(QStringLiteral("Primary Step Z"), primaryZ);
    form->addRow(QStringLiteral("Secondary Step X"), secondaryX);
    form->addRow(QStringLiteral("Secondary Step Y"), secondaryY);
    form->addRow(QStringLiteral("Secondary Step Z"), secondaryZ);
    form->addRow(QStringLiteral("Radius"), radius);
    form->addRow(QStringLiteral("Start Angle"), startAngle);
    form->addRow(QStringLiteral("End Angle"), endAngle);
    form->addRow(QStringLiteral("Polyline Points (m)"), polyline);
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
        QMessageBox::warning(parent, QStringLiteral("Invalid Polyline"),
                             QStringLiteral("Enter at least two points as x,y,z; separate points with semicolons."));
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
    _tabs->addTab(createPoseTaskPage(), QStringLiteral("Key Stations"));
    _tabs->addTab(createBoxRegionPage(), QStringLiteral("Workspace Regions"));
    _tabs->addTab(createValidationPage(), QStringLiteral("Validate & Publish"));
    _statusLabel = new QLabel(QStringLiteral("Select a .rmb.json model before defining engineering requirements."), this);
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->addWidget(_tabs);
    layout->addWidget(_statusLabel);
    refreshTables();
}

QWidget* EngineeringRequirementsWidget::createPoseTaskPage()
{
    QWidget* page = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(page);
    QWidget* toolbar = new QWidget(page);
    toolbar->setObjectName("keyStationCompactToolbar");
    toolbar->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    QHBoxLayout* actions = new QHBoxLayout(toolbar);
    actions->setContentsMargins(0, 0, 0, 0);
    actions->setSpacing(4);
    QWidget* actionHost = new QWidget(page);
    actionHost->setVisible(false);
    QPushButton* add = new QPushButton(QStringLiteral("Add Station"), page); add->setObjectName("addRequirementPoseTaskButton");
    QPushButton* duplicate = new QPushButton(QStringLiteral("Duplicate Station"), actionHost); duplicate->setObjectName("duplicateRequirementPoseTaskButton");
    QPushButton* remove = new QPushButton(QStringLiteral("Remove Station"), actionHost); remove->setObjectName("removeRequirementPoseTaskButton");
    QPushButton* capture = new QPushButton(QStringLiteral("Capture TCP Pose"), page); capture->setObjectName("captureRequirementTcpButton");
    QPushButton* pickGeometry = new QPushButton(QStringLiteral("Pick Geometry Frame"), actionHost);
    pickGeometry->setObjectName("pickRequirementGeometryFeatureButton");
    pickGeometry->setToolTip(QStringLiteral("Ctrl+double-click a fixture or part in the 3D view."));
    QPushButton* createTemplate = new QPushButton(QStringLiteral("Create from Template"), actionHost);
    createTemplate->setObjectName("createRequirementTemplateButton");
    createTemplate->setToolTip(QStringLiteral("Create stations from a process template."));
    QPushButton* updateTemplate = new QPushButton(QStringLiteral("Update Template"), actionHost);
    updateTemplate->setObjectName("updateRequirementTemplateButton");
    updateTemplate->setToolTip(QStringLiteral("Update linked stations in this template instance."));
    QPushButton* detachTemplate = new QPushButton(QStringLiteral("Detach Template"), actionHost);
    detachTemplate->setObjectName("detachRequirementTemplateButton");
    detachTemplate->setToolTip(QStringLiteral("Convert this generated station to a manually maintained station."));
    QPushButton* createArray = new QPushButton(QStringLiteral("Generate Array"), actionHost);
    createArray->setObjectName("createRequirementArrayButton");
    createArray->setToolTip(QStringLiteral("Generate a linear, rectangular, or circular array from this station."));
    QPushButton* mirror = new QPushButton(QStringLiteral("Mirror Station"), actionHost);
    mirror->setObjectName("mirrorRequirementStationButton");
    mirror->setToolTip(QStringLiteral("Mirror this fixed-orientation station about the reference-frame origin."));
    QPushButton* import = new QPushButton(QStringLiteral("Import Stations"), actionHost);
    import->setObjectName("importRequirementStationsButton");
    import->setToolTip(QStringLiteral("Import key stations from CSV or JSON. Invalid rows are not applied."));
    QPushButton* undo = new QPushButton(QStringLiteral("Undo"), actionHost);
    undo->setObjectName("undoRequirementOperationButton");
    QPushButton* redo = new QPushButton(QStringLiteral("Redo"), actionHost);
    redo->setObjectName("redoRequirementOperationButton");
    undo->setToolTip(QStringLiteral("Restore the requirement set before the most recent edit."));

    const auto addMenuAction = [] (QMenu* menu, QPushButton* button) {
        QAction* action = menu->addAction(button->text());
        action->setToolTip(button->toolTip());
        QObject::connect(action, &QAction::triggered, button, &QPushButton::click);
        QObject::connect(menu, &QMenu::aboutToShow, action,
                         [action, button] () { action->setEnabled(button->isEnabled()); });
    };
    const auto createMenuButton = [page] (const QString& text, const QString& objectName,
                                          QMenu* menu, const QString& toolTip) {
        QToolButton* button = new QToolButton(page);
        button->setText(text);
        button->setObjectName(objectName);
        button->setToolTip(toolTip);
        button->setMenu(menu);
        button->setPopupMode(QToolButton::InstantPopup);
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        button->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
        return button;
    };

    QToolButton* pick = new QToolButton(page);
    pick->setObjectName("keyStationPickFrameButton");
    pick->setText(QStringLiteral("Pick Frame"));
    pick->setToolTip(pickGeometry->toolTip());
    pick->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    QObject::connect(pick, &QToolButton::clicked, pickGeometry, &QPushButton::click);

    QMenu* editMenu = new QMenu(page);
    addMenuAction(editMenu, duplicate);
    addMenuAction(editMenu, remove);
    editMenu->addSeparator();
    addMenuAction(editMenu, undo);
    addMenuAction(editMenu, redo);
    QToolButton* edit = createMenuButton(QStringLiteral("Edit"), "keyStationEditMenu",
                                          editMenu, QStringLiteral("Station editing and history actions."));

    QMenu* templateMenu = new QMenu(page);
    addMenuAction(templateMenu, createTemplate);
    addMenuAction(templateMenu, updateTemplate);
    addMenuAction(templateMenu, detachTemplate);
    QToolButton* templates = createMenuButton(QStringLiteral("Templates"), "keyStationTemplateMenu",
                                               templateMenu, QStringLiteral("Station template actions."));

    QMenu* generateMenu = new QMenu(page);
    addMenuAction(generateMenu, createArray);
    addMenuAction(generateMenu, mirror);
    QToolButton* generate = createMenuButton(QStringLiteral("Generate"), "keyStationGenerateMenu",
                                              generateMenu, QStringLiteral("Create station variants."));

    QMenu* moreMenu = new QMenu(page);
    addMenuAction(moreMenu, import);
    QToolButton* more = createMenuButton(QStringLiteral("More"), "keyStationMoreMenu",
                                          moreMenu, QStringLiteral("Additional station actions."));

    add->setProperty("primaryAction", true);
    capture->setProperty("secondaryAction", true);
    actions->addWidget(add);
    actions->addWidget(capture);
    actions->addWidget(pick);
    actions->addWidget(edit);
    actions->addWidget(templates);
    actions->addWidget(generate);
    actions->addWidget(more);
    actions->addStretch();
    layout->addWidget(toolbar);
    QSplitter* splitter = new QSplitter(Qt::Horizontal, page);
    _stationList = new QListWidget(splitter); _stationList->setObjectName("keyStationList");
    // 允许 Dock 缩至 240px：工位列表最小宽度降到 100，水平策略 Ignored 不再撑开 Dock。
    _stationList->setMinimumWidth(100);
    _stationList->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    QWidget* inspector = new QWidget(splitter);
    // 检查器面板不设最小宽度、水平忽略，跟随 Dock 宽度收缩。
    inspector->setMinimumWidth(0);
    inspector->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    QVBoxLayout* inspectorLayout = new QVBoxLayout(inspector);
    QFormLayout* form = new QFormLayout();
    _stationNameEdit = new QLineEdit(inspector); _stationNameEdit->setObjectName("keyStationNameEdit");
    _stationProcessTypeCombo = enumCombo("keyStationProcessTypeCombo", {
        {QStringLiteral("Generic"), static_cast<int>(ProcessType::Generic)}, {QStringLiteral("Pick"), static_cast<int>(ProcessType::Pick)},
        {QStringLiteral("Place"), static_cast<int>(ProcessType::Place)}, {QStringLiteral("Machine Load"), static_cast<int>(ProcessType::MachineLoad)},
        {QStringLiteral("Machine Unload"), static_cast<int>(ProcessType::MachineUnload)}, {QStringLiteral("Inspect"), static_cast<int>(ProcessType::Inspect)},
        {QStringLiteral("Weld Start"), static_cast<int>(ProcessType::WeldStart)}, {QStringLiteral("Weld End"), static_cast<int>(ProcessType::WeldEnd)},
        {QStringLiteral("Tool Change"), static_cast<int>(ProcessType::ToolChange)}, {QStringLiteral("Safe Standby"), static_cast<int>(ProcessType::SafeStandby)},
        {QStringLiteral("Handover"), static_cast<int>(ProcessType::Handover)}});
    _stationLevelCombo = enumCombo("keyStationRequirementLevelCombo", {{"Must", static_cast<int>(RequirementLevel::Must)}, {"Should", static_cast<int>(RequirementLevel::Should)}, {"Info", static_cast<int>(RequirementLevel::Info)}});
    _stationReferenceFrameCombo = new QComboBox(inspector); _stationReferenceFrameCombo->setObjectName("keyStationReferenceFrameCombo");
    _stationTcpFrameCombo = new QComboBox(inspector); _stationTcpFrameCombo->setObjectName("keyStationTcpFrameCombo");
    _stationOrientationModeCombo = enumCombo("keyStationOrientationModeCombo", {{QStringLiteral("Fixed Orientation"), static_cast<int>(OrientationMode::Fixed)}, {QStringLiteral("Align Frame"), static_cast<int>(OrientationMode::AlignFrame)}, {QStringLiteral("Align Geometry Normal"), static_cast<int>(OrientationMode::AlignGeometryNormal)}, {QStringLiteral("Point at Target"), static_cast<int>(OrientationMode::PointAtTarget)}});
    _stationOrientationTargetFrameCombo = new QComboBox(inspector); _stationOrientationTargetFrameCombo->setObjectName("keyStationOrientationTargetFrameCombo");
    _stationOrientationTargetPointEdit = new QLineEdit(inspector);
    _stationOrientationTargetPointEdit->setObjectName("keyStationOrientationTargetPointEdit");
    _stationOrientationTargetPointEdit->setPlaceholderText(QStringLiteral("x, y, z (reference frame, m)"));
    _stationOrientationTargetPointEdit->setToolTip(QStringLiteral("Enter x, y, z in meters. Used when no target frame is selected."));
    _stationFreeRollCheck = new QCheckBox(QStringLiteral("Allow Tool Roll"), inspector); _stationFreeRollCheck->setObjectName("keyStationFreeRollCheck");
    form->addRow(QStringLiteral("Name"), _stationNameEdit); form->addRow(QStringLiteral("Process Type"), _stationProcessTypeCombo);
    form->addRow(QStringLiteral("Requirement Level"), _stationLevelCombo); form->addRow(QStringLiteral("Reference Frame"), _stationReferenceFrameCombo);
    form->addRow(QStringLiteral("TCP"), _stationTcpFrameCombo); form->addRow(QStringLiteral("Orientation Rule"), _stationOrientationModeCombo);
    _stationOrientationTargetFrameLabel = new QLabel(QStringLiteral("Orientation Target"), inspector);
    _stationOrientationTargetPointLabel = new QLabel(QStringLiteral("Target Point"), inspector);
    form->addRow(_stationOrientationTargetFrameLabel, _stationOrientationTargetFrameCombo);
    form->addRow(_stationOrientationTargetPointLabel, _stationOrientationTargetPointEdit);
    form->addRow(QString(), _stationFreeRollCheck);
    inspectorLayout->addLayout(form);
    QGroupBox* pathGroup = new QGroupBox(QStringLiteral("Approach & Retract"), inspector);
    QFormLayout* pathForm = new QFormLayout(pathGroup);
    _stationApproachEnabled = new QCheckBox(QStringLiteral("Approach Along Tool Z"), pathGroup); _stationApproachEnabled->setObjectName("keyStationApproachEnabled");
    _stationApproachDistance = lengthSpinBox("keyStationApproachDistance");
    _stationRetractEnabled = new QCheckBox(QStringLiteral("Retract Along Reference Z"), pathGroup); _stationRetractEnabled->setObjectName("keyStationRetractEnabled");
    _stationRetractDistance = lengthSpinBox("keyStationRetractDistance");
    _stationMinimumJointMargin = lengthSpinBox("keyStationMinimumJointMargin");
    pathForm->addRow(_stationApproachEnabled, _stationApproachDistance); pathForm->addRow(_stationRetractEnabled, _stationRetractDistance);
    pathForm->addRow(QStringLiteral("Minimum Joint Margin"), _stationMinimumJointMargin);
    inspectorLayout->addWidget(pathGroup);
    _stationAdvancedPoseGroup = new QGroupBox(QStringLiteral("Advanced Pose (Station Frame)"), inspector); _stationAdvancedPoseGroup->setObjectName("keyStationAdvancedPoseGroup");
    QFormLayout* poseForm = new QFormLayout(_stationAdvancedPoseGroup);
    _stationAdvancedPoseSourceLabel = new QLabel(_stationAdvancedPoseGroup);
    _stationAdvancedPoseSourceLabel->setObjectName("keyStationAdvancedPoseSourceLabel");
    _stationAdvancedPoseSourceLabel->setWordWrap(true);
    _stationX = lengthSpinBox("keyStationX"); _stationY = lengthSpinBox("keyStationY"); _stationZ = lengthSpinBox("keyStationZ");
    _stationRoll = angleSpinBox("keyStationRoll"); _stationPitch = angleSpinBox("keyStationPitch"); _stationYaw = angleSpinBox("keyStationYaw");
    poseForm->addRow(QStringLiteral("Pose Source"), _stationAdvancedPoseSourceLabel);
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
    connect(redo, &QPushButton::clicked, this, &EngineeringRequirementsWidget::redoLastOperation);
    connect(_stationList, &QListWidget::currentRowChanged, this, &EngineeringRequirementsWidget::refreshKeyStationInspector);
    connect(_stationOrientationModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &EngineeringRequirementsWidget::updateOrientationEditor);
    connect(_stationNameEdit, &QLineEdit::editingFinished, this, &EngineeringRequirementsWidget::commitKeyStationInspector);
    connect(_stationOrientationTargetPointEdit, &QLineEdit::editingFinished,
            this, &EngineeringRequirementsWidget::commitKeyStationInspector);
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
    QPushButton* add = new QPushButton(QStringLiteral("Add Region"), page); add->setObjectName("addRequirementBoxRegionButton");
    QPushButton* duplicate = new QPushButton(QStringLiteral("Duplicate Region"), page); duplicate->setObjectName("duplicateRequirementBoxRegionButton");
    QPushButton* remove = new QPushButton(QStringLiteral("Remove Region"), page); remove->setObjectName("removeRequirementBoxRegionButton");
    actions->addWidget(add); actions->addWidget(duplicate); actions->addWidget(remove); actions->addStretch();
    layout->addLayout(actions);
    _regionTable = new QTableWidget(page); _regionTable->setObjectName("engineeringRequirementBoxTable");
    _regionTable->setColumnCount(13);
    _regionTable->setHorizontalHeaderLabels({"ID", "Name", "Level", "Reference Frame", "Center X", "Center Y", "Center Z", "Size X", "Size Y", "Size Z", "Minimum Coverage", "Samples per Axis", "TCP Frame"});
    _regionTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    layout->addWidget(_regionTable);
    connect(add, &QPushButton::clicked, this, &EngineeringRequirementsWidget::addBoxRegion);
    connect(duplicate, &QPushButton::clicked, this, &EngineeringRequirementsWidget::duplicateBoxRegion);
    connect(remove, &QPushButton::clicked, this, &EngineeringRequirementsWidget::removeBoxRegion);
    // 表格单元格是覆盖盒的直接编辑入口。每次提交编辑后立即同步到数据模型并记录快照，
    // 防止工程师修改采样密度、尺寸或约束级别后无法撤销，或直到保存时才发现修改未生效。
    connect(_regionTable, &QTableWidget::cellChanged, this,
            [this] (int, int) { commitBoxRegionTableEdit(); });
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
    _validationSummaryLabel = new QLabel(page);
    _validationSummaryLabel->setObjectName("engineeringRequirementsValidationSummaryLabel");
    _validationSummaryLabel->setWordWrap(true);
    _diagnosticTable = new QTableWidget(page);
    _diagnosticTable->setObjectName("engineeringRequirementsDiagnosticTable");
    // 5 列诊断表：Code/Requirement/Field/Level/Message。Field 列承载诊断涉及的
    // 具体字段/码(如 REQ_TCP_FRAME_NOT_FOUND 问题指向的 TCP 字段)，便于工程师
    // 一眼定位问题出处。
    _diagnosticTable->setColumnCount(5);
    _diagnosticTable->setHorizontalHeaderLabels({"Code", "Requirement", "Field", "Level", "Message"});
    _diagnosticTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    _diagnosticTable->horizontalHeader()->setStretchLastSection(true);
    _diagnosticTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    _diagnosticTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    layout->addWidget(_modelLabel); layout->addWidget(_freezeLabel);
    layout->addWidget(_validationSummaryLabel); layout->addWidget(_diagnosticTable);
    QHBoxLayout* actions = new QHBoxLayout();
    QPushButton* bind = new QPushButton(QStringLiteral("Bind Model"), page); bind->setObjectName("bindRequirementModelButton");
    QPushButton* load = new QPushButton(QStringLiteral("Import Requirements"), page); load->setObjectName("loadRequirementSetButton");
    QPushButton* save = new QPushButton(QStringLiteral("Export Requirements"), page); save->setObjectName("saveRequirementSetButton");
    _freezeButton = new QPushButton(QStringLiteral("Check and Publish"), page); _freezeButton->setObjectName("freezeRequirementSetButton");
    QPushButton* unfreeze = new QPushButton(QStringLiteral("Edit Requirements"), page); unfreeze->setObjectName("unfreezeRequirementSetButton");
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
        // 刷新表格属于视图回填，必须屏蔽 cellChanged；否则 setItem 会被误判为用户编辑，
        // 从而污染撤销历史并在刷新过程中递归同步数据模型。
        const QSignalBlocker blocker(_regionTable);
        _regionTable->setRowCount(static_cast<int>(_requirements.boxRegions.size()));
        for (int row = 0; row < _regionTable->rowCount(); ++row) {
            const BoxRegion& region = _requirements.boxRegions[static_cast<std::size_t>(row)];
            _regionTable->setItem(row, 0, textItem(QString::fromStdString(region.id)));
            _regionTable->setItem(row, 1, textItem(QString::fromStdString(region.name)));
            QComboBox* level = levelCombo(region.level);
            _regionTable->setCellWidget(row, 2, level);
            // QTableWidget 不会把嵌入 ComboBox 的切换转发为 cellChanged，需单独接入。
            connect(level, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                    [this] (int) { commitBoxRegionTableEdit(); });
            _regionTable->setItem(row, 3, textItem(QString::fromStdString(region.refFrame)));
            for (int axis = 0; axis < 3; ++axis) {
                _regionTable->setItem(row, 4 + axis, textItem(QString::number(region.center[axis])));
                _regionTable->setItem(row, 7 + axis, textItem(QString::number(region.size[axis])));
            }
            _regionTable->setItem(row, 10, textItem(QString::number(region.minimumCoverage)));
            // 将采样密度展示为显式的审计字段，确保导入、编辑、冻结和重新打开项目后采用完全
            // 相同的覆盖率离散网格，而不是隐式回退到数据结构默认值。
            _regionTable->setItem(row, 11, textItem(QString::number(region.samplesPerAxis)));
            // TCP 帧未显式指定时，以绑定模型对应设备的末端作为默认值，确保展示的
            // TCP 归属绑定机器人，避免覆盖盒 TCP 默认指向第一台设备(可能非绑定设备)。
            const std::string tcpFrame = region.tcpFrame.empty()
                ? defaultTcpFrame(_workcell, _requirements.modelBinding.robotName) : region.tcpFrame;
            _regionTable->setItem(row, 12, textItem(QString::fromStdString(tcpFrame)));
        }
    }
    const bool editable = !_requirements.frozen;
    if (_regionTable != nullptr) _regionTable->setEnabled(editable);
    if (_freezeButton != nullptr) _freezeButton->setEnabled(editable);
    // "绑定机器人模型"按钮同样只允许在编辑态使用：重新绑定会改变模型指纹与编译
    // 结果，冻结后不应允许通过 UI 静默改绑，避免工件与模型来源不一致。
    for (const char* name : {"addRequirementPoseTaskButton", "duplicateRequirementPoseTaskButton",
                             "removeRequirementPoseTaskButton", "captureRequirementTcpButton",
                             "pickRequirementGeometryFeatureButton",
                             "createRequirementTemplateButton", "updateRequirementTemplateButton",
                              "detachRequirementTemplateButton", "createRequirementArrayButton",
                             "mirrorRequirementStationButton", "importRequirementStationsButton",
                             "addRequirementBoxRegionButton", "duplicateRequirementBoxRegionButton",
                             "removeRequirementBoxRegionButton", "bindRequirementModelButton"}) {
        if (QPushButton* button = findChild<QPushButton*>(name)) button->setEnabled(editable);
    }
    if (QPushButton* button = findChild<QPushButton*>("undoRequirementOperationButton"))
        button->setEnabled(editable && _undoStack.canUndo());
    if (QPushButton* button = findChild<QPushButton*>("redoRequirementOperationButton"))
        button->setEnabled(editable && _undoStack.canRedo());
    if (_modelLabel != nullptr)
        _modelLabel->setText(QStringLiteral("Model: %1").arg(QString::fromStdString(_requirements.modelBinding.sourcePath)));
    if (_freezeLabel != nullptr) {
        if (_requirements.frozen) {
            // 冻结时间来自冻结工件，而不是当前界面刷新时间。工程师重新打开项目或导出报告时，
            // 因而能准确识别本次优化将消费的是哪一次经过真实 WorkCell 校验的需求快照。
            const QString frozenAt = _frozenArtifact.frozenAt.empty()
                ? QStringLiteral("Not recorded")
                : QString::fromStdString(_frozenArtifact.frozenAt);
            const QString revision = _frozenArtifact.publication.revisionId.empty()
                ? QStringLiteral("Unnumbered")
                : QString::fromStdString(_frozenArtifact.publication.revisionId);
            _freezeLabel->setText(QStringLiteral("Status: Published\nVersion: %1\nPublished at (UTC): %2\nModel status: Verified")
                                      .arg(revision, frozenAt));
        } else {
            _freezeLabel->setText(QStringLiteral("Status: Draft\nCheck and publish before downstream analysis or optimization."));
        }
    }
    // 冻结后把审计摘要展示在冻结状态标签的悬停提示里：三个审计指纹(需求/模型/场景)
    // 加 Included/Excluded 条目计数与 Quick/Verified 验证阶段计数，方便工程师在不
    // 打开工件文件的情况下核对冻结内容的构成与验证覆盖程度。
    if (_requirements.frozen && _freezeLabel != nullptr) {
        int included = 0;
        int excluded = 0;
        int quick = 0;
        int verified = 0;
        for (const CompiledPoseTask& task : _compiled.poseTasks)
            (task.compileState == RequirementCompileState::Included ? ++included : ++excluded);
        for (const WorkspaceDemandRegion& region : _compiled.workspaceRegions) {
            (region.compileState == RequirementCompileState::Included ? ++included : ++excluded);
            (region.minimumVerificationStage == RequirementVerificationStage::Quick ? ++quick : ++verified);
        }
        _freezeLabel->setToolTip(QStringLiteral("Technical evidence: requirement=%1; model=%2; WorkCell=%3; Included %4; Excluded %5; Quick %6; Verified %7; schema v%8")
                                 .arg(QString::fromStdString(_frozenArtifact.requirementFingerprint),
                                      QString::fromStdString(_frozenArtifact.modelBinding.robotModelFingerprint),
                                      QString::fromStdString(_frozenArtifact.workcellFingerprint))
                                 .arg(included).arg(excluded).arg(quick).arg(verified)
                                 .arg(_frozenArtifact.schemaVersion));
    }
    refreshValidationPanel();
}

void EngineeringRequirementsWidget::refreshValidationPanel()
{
    if (_diagnosticTable == nullptr) return;
    std::vector<RequirementDiagnostic> diagnostics =
        RequirementCompiler::validateDetailed(_requirements);
    // 编辑态字段校验不足以确认真实 WorkCell 的 Frame/TCP/几何引用。只要当前
    // WorkCell 与绑定模型均可读取，就复用冻结器做无副作用预检；这样 UI 门禁和
    // 实际冻结使用同一套环境规则，不会出现“按钮可点、点击后才报 Must 错误”。
    const auto appendPreflightFailure = [&diagnostics] (const std::string& message) {
        RequirementDiagnostic diagnostic;
        diagnostic.code = "REQ_ENVIRONMENT_PRECHECK_FAILED";
        diagnostic.severity = RequirementDiagnosticSeverity::Error;
        diagnostic.level = RequirementLevel::Must;
        diagnostic.field = "workcell";
        diagnostic.message = message;
        diagnostic.source = "engineeringrequirements.freezer";
        diagnostic.blocking = true;
        diagnostics.push_back(diagnostic);
    };
    if (!_requirements.modelBinding.sourcePath.empty() && _workcell == nullptr) {
        appendPreflightFailure("The current WorkCell is unavailable for requirement validation.");
    } else if (_workcell != nullptr && !_requirements.modelBinding.sourcePath.empty()) {
        RobotModelSpec model;
        QString modelError;
        if (loadRobotModelDocument(QString::fromStdString(_requirements.modelBinding.sourcePath),
                                   _projectOutputDirectory, model, &modelError)) {
            FrozenRequirementArtifact preview;
            std::string freezeError;
            if (!RequirementFreezer::freeze(_requirements, *_workcell, activeWorkCellState(), model,
                                             preview, &freezeError,
                                             _projectOutputDirectory.toStdString())) {
                appendPreflightFailure(freezeError);
            }
        } else {
            appendPreflightFailure(modelError.toStdString());
        }
    }
    _diagnosticTable->setRowCount(static_cast<int>(diagnostics.size()));
    int blocking = 0;
    for (int row = 0; row < _diagnosticTable->rowCount(); ++row) {
        const RequirementDiagnostic& diagnostic = diagnostics[static_cast<std::size_t>(row)];
        if (diagnostic.blocking) ++blocking;
        _diagnosticTable->setItem(row, 0, new QTableWidgetItem(QString::fromStdString(
            diagnostic.code.empty() ? "REQ_INVALID" : diagnostic.code)));
        _diagnosticTable->setItem(row, 1, new QTableWidgetItem(QString::fromStdString(diagnostic.requirementId)));
        _diagnosticTable->setItem(row, 2, new QTableWidgetItem(QString::fromStdString(diagnostic.field)));
        _diagnosticTable->setItem(row, 3, new QTableWidgetItem(QString::fromLatin1(toString(diagnostic.level))));
        _diagnosticTable->setItem(row, 4, new QTableWidgetItem(QString::fromStdString(diagnostic.message)));
        const QColor color = diagnostic.blocking ? QColor(Qt::red) :
            (diagnostic.level == RequirementLevel::Info ? QColor(Qt::gray) : QColor(180, 120, 0));
        for (int column = 0; column < _diagnosticTable->columnCount(); ++column)
            _diagnosticTable->item(row, column)->setForeground(color);
    }
    if (_validationSummaryLabel != nullptr) {
        int included = 0;
        int excluded = 0;
        if (_compiled.frozen) {
            for (const CompiledPoseTask& task : _compiled.poseTasks)
                (task.compileState == RequirementCompileState::Included ? ++included : ++excluded);
            for (const WorkspaceDemandRegion& region : _compiled.workspaceRegions)
                (region.compileState == RequirementCompileState::Included ? ++included : ++excluded);
        }
        _validationSummaryLabel->setText(QStringLiteral(
            "Diagnostics: %1 | Blocking: %2\nCompiled: Included %3 | Excluded %4")
            .arg(diagnostics.size()).arg(blocking).arg(included).arg(excluded));
    }
    // 冻结按钮门禁：通常仅当需求未冻结且没有阻断性诊断时才允许冻结。
    // 但“缺少机器人模型指纹”必须保留可点击入口，才能让宿主项目门禁优先
    // 给出“先在 RobotModelBuilder 生成并加载 managed WorkCell”的明确反馈，
    // 而不是被编辑器按钮静默禁用并显示泛化的初始状态。
    bool onlyMissingRobotModelFingerprint = blocking == 1;
    if (onlyMissingRobotModelFingerprint) {
        for (const RequirementDiagnostic& diagnostic : diagnostics) {
            if (diagnostic.blocking &&
                diagnostic.message != "A robot model fingerprint is required.") {
                onlyMissingRobotModelFingerprint = false;
                break;
            }
        }
    }
    if (_freezeButton != nullptr)
        _freezeButton->setEnabled(!_requirements.frozen &&
                                  (blocking == 0 || onlyMissingRobotModelFingerprint));
}

void EngineeringRequirementsWidget::syncTablesToRequirements()
{
    if (_requirements.frozen) return;
    commitKeyStationInspector();
    if (_workcell != nullptr) {
        for (PoseTask& task : _requirements.poseTasks) {
            if (task.source != PoseTaskSource::GeometryFeature) continue;
            // 几何工位必须随当前 JOG/场景状态重新解释。例如工装 Frame 已被
            // 移动时，继续使用 WorkCell 默认状态会把保存的工作点投到旧位置。
            GeometryFeatureResolver::applyToStation(task.geometryFeature, *_workcell,
                                                     activeWorkCellState(), task, nullptr);
        }
    }
    const std::vector<BoxRegion> previousRegions = _requirements.boxRegions;
    _requirements.boxRegions.clear();
    for (int row = 0; _regionTable != nullptr && row < _regionTable->rowCount(); ++row) {
        const std::string id = text(_regionTable, row, 0).toStdString();
        BoxRegion region;
        for (const BoxRegion& previous : previousRegions) {
            if (previous.id == id) {
                region = previous;
                break;
            }
        }
        if (region.id.empty() && row < static_cast<int>(previousRegions.size()))
            region = previousRegions[static_cast<std::size_t>(row)];
        region.id = id; region.name = text(_regionTable, row, 1).toStdString();
        region.level = levelFromText(qobject_cast<QComboBox*>(_regionTable->cellWidget(row, 2))->currentText());
        region.refFrame = text(_regionTable, row, 3, "WORLD").toStdString();
        for (int axis = 0; axis < 3; ++axis) { region.center[axis] = number(_regionTable, row, 4 + axis); region.size[axis] = number(_regionTable, row, 7 + axis, 0.1); }
        region.minimumCoverage = number(_regionTable, row, 10, 0.8);
        region.samplesPerAxis = positiveSampleCount(_regionTable, row, 11, region.samplesPerAxis);
        region.tcpFrame = text(_regionTable, row, 12,
                               QString::fromStdString(region.tcpFrame)).trimmed().toStdString();
        // 表格 TCP 为空时回填默认值，且优先取绑定设备末端，保证同步后的覆盖盒
        // TCP 归属绑定机器人。
        if (region.tcpFrame.empty())
            region.tcpFrame = defaultTcpFrame(_workcell, _requirements.modelBinding.robotName);
        _requirements.boxRegions.push_back(region);
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
        const QString name = task.name.empty() ? QStringLiteral("Unnamed Station") : QString::fromStdString(task.name);
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
                                                   activeWorkCellState(), resolution, nullptr)) {
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
    _stationOrientationTargetFrameCombo->addItem(QStringLiteral("Not Specified"), QString());
    if (_workcell != nullptr) {
        for (rw::kinematics::Frame* frame : _workcell->getFrames()) {
            if (frame == nullptr) continue;
            const QString name = QString::fromStdString(frame->getName());
            addChoice(_stationReferenceFrameCombo, name);
            addChoice(_stationOrientationTargetFrameCombo, name);
        }
        // TCP 下拉框优先只列出绑定模型对应设备的末端：绑定设备存在时仅提供该设备的
        // TCP，引导工位 TCP 落在绑定机器人上(与冻结器归属校验一致)；未绑定时回退到
        // 列出全部设备的末端，保持旧行为。
        const rw::core::Ptr<rw::models::Device> boundDevice =
            _requirements.modelBinding.robotName.empty()
                ? rw::core::Ptr<rw::models::Device>()
                : _workcell->findDevice(_requirements.modelBinding.robotName);
        if (boundDevice != nullptr && boundDevice->getEnd() != nullptr) {
            addChoice(_stationTcpFrameCombo,
                      QString::fromStdString(boundDevice->getEnd()->getName()));
        }
        else if (_requirements.modelBinding.robotName.empty()) {
            for (const rw::core::Ptr<rw::models::Device>& device : _workcell->getDevices()) {
                if (device != nullptr && device->getEnd() != nullptr)
                    addChoice(_stationTcpFrameCombo,
                              QString::fromStdString(device->getEnd()->getName()));
            }
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
    selectOrUnresolved(_stationReferenceFrameCombo, reference, QStringLiteral("Unresolved: "));
    selectOrUnresolved(_stationTcpFrameCombo, tcp, QStringLiteral("Unresolved: "));
    selectOrUnresolved(_stationOrientationTargetFrameCombo, target, QStringLiteral("Unresolved: "));
}

void EngineeringRequirementsWidget::refreshKeyStationInspector()
{
    const int selected = selectedKeyStationIndex();
    const bool hasSelection = selected >= 0 && selected < static_cast<int>(_requirements.poseTasks.size());
    const bool editable = hasSelection && !_requirements.frozen;
    for (QWidget* editor : {static_cast<QWidget*>(_stationNameEdit), static_cast<QWidget*>(_stationProcessTypeCombo),
                            static_cast<QWidget*>(_stationLevelCombo), static_cast<QWidget*>(_stationReferenceFrameCombo),
                            static_cast<QWidget*>(_stationTcpFrameCombo), static_cast<QWidget*>(_stationOrientationModeCombo),
                            static_cast<QWidget*>(_stationOrientationTargetFrameCombo), static_cast<QWidget*>(_stationOrientationTargetPointEdit), static_cast<QWidget*>(_stationFreeRollCheck),
                            static_cast<QWidget*>(_stationApproachEnabled), static_cast<QWidget*>(_stationRetractEnabled),
                            static_cast<QWidget*>(_stationApproachDistance), static_cast<QWidget*>(_stationRetractDistance),
                            static_cast<QWidget*>(_stationMinimumJointMargin), static_cast<QWidget*>(_stationAdvancedPoseGroup)})
        if (editor != nullptr) editor->setEnabled(editable);
    if (!hasSelection) return;

    _refreshingKeyStationInspector = true;
    refreshFrameChoices();
    const PoseTask& task = _requirements.poseTasks[static_cast<std::size_t>(selected)];
    PoseTask displayedTask = task;
    _stationOrientationCoordinatesResolved = task.orientation.mode == OrientationMode::Fixed;
    if (!_stationOrientationCoordinatesResolved && _workcell != nullptr) {
        std::string resolutionError;
        _stationOrientationCoordinatesResolved = OrientationRuleResolver::applyToStation(
            displayedTask, *_workcell, activeWorkCellState(), &resolutionError);
    }
    const QSignalBlocker nameBlocker(_stationNameEdit), processBlocker(_stationProcessTypeCombo), levelBlocker(_stationLevelCombo);
    const QSignalBlocker modeBlocker(_stationOrientationModeCombo), rollBlocker(_stationFreeRollCheck);
    const QSignalBlocker targetPointBlocker(_stationOrientationTargetPointEdit);
    const QSignalBlocker approachBlocker(_stationApproachEnabled), retractBlocker(_stationRetractEnabled);
    const QSignalBlocker approachDistanceBlocker(_stationApproachDistance), retractDistanceBlocker(_stationRetractDistance);
    const QSignalBlocker jointMarginBlocker(_stationMinimumJointMargin), xBlocker(_stationX), yBlocker(_stationY), zBlocker(_stationZ);
    const QSignalBlocker rollValueBlocker(_stationRoll), pitchBlocker(_stationPitch), yawBlocker(_stationYaw);
    _stationNameEdit->setText(QString::fromStdString(task.name));
    _stationProcessTypeCombo->setCurrentIndex(_stationProcessTypeCombo->findData(static_cast<int>(task.processType)));
    _stationLevelCombo->setCurrentIndex(_stationLevelCombo->findData(static_cast<int>(task.level)));
    _stationOrientationModeCombo->setCurrentIndex(_stationOrientationModeCombo->findData(static_cast<int>(task.orientation.mode)));
    _stationOrientationTargetPointEdit->setText(QString::fromStdString(task.orientation.targetPoint));
    _stationFreeRollCheck->setChecked(task.orientation.allowToolRollFree);
    _stationApproachEnabled->setChecked(task.approach.enabled);
    _stationApproachDistance->setValue(task.approach.distanceMeters);
    _stationRetractEnabled->setChecked(task.retract.enabled);
    _stationRetractDistance->setValue(task.retract.distanceMeters);
    _stationMinimumJointMargin->setValue(task.validation.minimumJointMargin);
    _stationX->setValue(task.position[0]); _stationY->setValue(task.position[1]); _stationZ->setValue(task.position[2]);
    _stationRoll->setValue(displayedTask.rpyDeg[0]); _stationPitch->setValue(displayedTask.rpyDeg[1]); _stationYaw->setValue(displayedTask.rpyDeg[2]);
    updateOrientationEditor();
    _refreshingKeyStationInspector = false;
}

void EngineeringRequirementsWidget::commitKeyStationInspector()
{
    if (_requirements.frozen || _refreshingKeyStationInspector) return;
    const int selected = selectedKeyStationIndex();
    if (selected < 0 || selected >= static_cast<int>(_requirements.poseTasks.size())) return;
    const RequirementSet before = _requirements;
    const PoseTask& existing = _requirements.poseTasks[static_cast<std::size_t>(selected)];
    PoseTask updated = existing;
    updated.name = _stationNameEdit->text().trimmed().toStdString();
    updated.processType = static_cast<ProcessType>(_stationProcessTypeCombo->currentData().toInt());
    updated.level = static_cast<RequirementLevel>(_stationLevelCombo->currentData().toInt());
    updated.refFrame = _stationReferenceFrameCombo->currentData().toString().toStdString();
    updated.tcpFrame = _stationTcpFrameCombo->currentData().toString().toStdString();
    updated.orientation.mode = static_cast<OrientationMode>(_stationOrientationModeCombo->currentData().toInt());
    updated.orientation.targetFrame = _stationOrientationTargetFrameCombo->currentData().toString().toStdString();
    updated.orientation.targetPoint = _stationOrientationTargetPointEdit->text().trimmed().toStdString();
    updated.orientation.allowToolRollFree = _stationFreeRollCheck->isChecked();
    updated.tolerance.allowToolRollFree = updated.orientation.allowToolRollFree;
    updated.approach.enabled = _stationApproachEnabled->isChecked();
    updated.approach.axis = OffsetAxis::ToolZ;
    updated.approach.distanceMeters = _stationApproachDistance->value();
    updated.retract.enabled = _stationRetractEnabled->isChecked();
    updated.retract.axis = OffsetAxis::ReferenceZ;
    updated.retract.distanceMeters = _stationRetractDistance->value();
    updated.validation.minimumJointMargin = _stationMinimumJointMargin->value();
    updated.position = {{_stationX->value(), _stationY->value(), _stationZ->value()}};
    if (updated.orientation.mode == OrientationMode::Fixed)
        updated.rpyDeg = {{_stationRoll->value(), _stationPitch->value(), _stationYaw->value()}};
    if (hasSameInspectorEditableValues(existing, updated)) return;

    // 先构造候选工位、再比较、最后替换。这样快照一定保存的是修改前的完整需求集，
    // 并且同一信号被多个槽接收时，后续槽不会产生重复撤销记录。
    _requirements.poseTasks[static_cast<std::size_t>(selected)] = updated;
    recordRequirementEdit(before);
}

void EngineeringRequirementsWidget::commitBoxRegionTableEdit()
{
    if (_requirements.frozen || _regionTable == nullptr) return;
    const RequirementSet before = _requirements;
    syncTablesToRequirements();
    // 使用现有 JSON 序列化作为稳定的完整快照比较，覆盖盒既有文本又有嵌入式组合框，
    // 通过整套需求数据比较可避免仅凭单元格坐标漏掉级别、采样密度等字段。
    if (RequirementSetJson::toJson(before) == RequirementSetJson::toJson(_requirements)) return;
    recordRequirementEdit(before, false);
}

void EngineeringRequirementsWidget::updateOrientationEditor()
{
    if (_stationAdvancedPoseGroup == nullptr || _stationOrientationModeCombo == nullptr) return;
    const OrientationMode mode = static_cast<OrientationMode>(_stationOrientationModeCombo->currentData().toInt());
    const bool fixed = mode == OrientationMode::Fixed;
    const bool pointAtTarget = mode == OrientationMode::PointAtTarget;
    _stationAdvancedPoseGroup->setVisible(true);
    for (QDoubleSpinBox* spin : {_stationX, _stationY, _stationZ}) {
        if (spin != nullptr) spin->setReadOnly(false);
    }
    for (QDoubleSpinBox* spin : {_stationRoll, _stationPitch, _stationYaw}) {
        if (spin != nullptr) spin->setReadOnly(!fixed);
    }
    if (_stationAdvancedPoseSourceLabel != nullptr) {
        const QString reference = _stationReferenceFrameCombo == nullptr
            ? QStringLiteral("WORLD") : _stationReferenceFrameCombo->currentData().toString();
        if (fixed) {
            _stationAdvancedPoseSourceLabel->setText(
                QStringLiteral("Relative to station frame %1. Position and fixed orientation are editable.").arg(reference));
        } else if (_stationOrientationCoordinatesResolved) {
            _stationAdvancedPoseSourceLabel->setText(
                QStringLiteral("Relative to station frame %1. Orientation is resolved by rule and is read-only.").arg(reference));
        } else {
            _stationAdvancedPoseSourceLabel->setText(
                QStringLiteral("Relative to station frame %1. The rule cannot be resolved; showing the saved orientation.").arg(reference));
        }
    }
    // 固定姿态只显示高级 RPY；坐标系/法向模式只需选择目标 Frame；指向模式额外开放
    // 参考系内目标点。两种目标同时填写时解析器优先使用 Frame，避免状态不确定。
    if (_stationOrientationTargetFrameCombo != nullptr) {
        _stationOrientationTargetFrameCombo->setVisible(!fixed);
        _stationOrientationTargetFrameLabel->setVisible(!fixed);
        _stationOrientationTargetFrameLabel->setText(pointAtTarget
            ? QStringLiteral("Target Frame (Optional)") : QStringLiteral("Orientation Target"));
    }
    if (_stationOrientationTargetPointEdit != nullptr) {
        _stationOrientationTargetPointEdit->setVisible(pointAtTarget);
        _stationOrientationTargetPointLabel->setVisible(pointAtTarget);
    }
    if (!_refreshingKeyStationInspector) commitKeyStationInspector();
}

void EngineeringRequirementsWidget::bindModel()
{
    // 冻结门禁：重新绑定会改变 modelBinding 指纹并让既有编译/冻结结果作废，
    // 因此冻结后禁止改绑，提示用户先解冻，避免工件与模型来源不一致。
    if (_requirements.frozen) {
        setStatus(QStringLiteral("Requirements are frozen. Edit requirements before rebinding the robot model."));
        return;
    }
    // 项目上下文中的 robot-model.main 是权威模型来源。优先使用它，避免用户在
    // generated/robot-models 下再次挑选旁车或过期 sidecar；项目资源不可用时仍保留
    // 独立 WorkCell 工作流的手动选择能力。
    QString autoError;
    if (bindGeneratedProjectModel(&autoError)) {
        setStatus(QStringLiteral("Model bound. Requirements track the model content fingerprint."));
        return;
    }
    if (!autoError.isEmpty()) {
        setStatus(QStringLiteral("Project model auto-binding unavailable: %1 Select a model file manually.")
                      .arg(autoError));
    }
    const QString initialDirectory = _projectOutputDirectory.isEmpty()
        ? QString()
        : QDir(_projectOutputDirectory).filePath(QStringLiteral("generated/robot-models"));
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Bind Robot Model"),
                                                       initialDirectory, "Robot model (*.rmb.json)");
    if (path.isEmpty()) return;
    RobotModelSpec spec;
    QString error;
    if (!loadRobotModelDocument(path, _projectOutputDirectory, spec, &error)) {
        setStatus(error);
        return;
    }
    _requirements.modelBinding.sourcePath = path.toStdString();
    _requirements.modelBinding.robotName = spec.robotName;
    _requirements.modelBinding.robotModelFingerprint = RobotModelFingerprint::canonicalSha256(spec);
    // 换绑模型后旧的编译结果与冻结工件不再对应当前模型，必须清空，防止
    // 残留的冻结审计记录被当作新模型的已验证证据使用。
    _compiled = CompiledRequirementSet();
    _frozenArtifact = FrozenRequirementArtifact();
    setStatus(QStringLiteral("Model bound. Requirements track the model content fingerprint.")); refreshTables();
    // 绑定模型会改变需求资源中的 modelBinding。发出统一领域变更通知，使项目标题栏
    // 和 Provider 脏状态与其他需求编辑操作保持一致。
    Q_EMIT requirementsChanged();
}

// 记录当前项目输出目录（由主窗口 projectContextChanged 信号同步，空表示独立 WorkCell
// 工作流）。该路径只用于判断项目上下文，不参与需求资源序列化。
void EngineeringRequirementsWidget::setProjectOutputDirectory(const QString& projectDirectory)
{
    _projectOutputDirectory = projectDirectory.trimmed().isEmpty() ? QString() :
        QFileInfo(projectDirectory).absoluteFilePath();
}

// 导出副本的默认路径：有项目时落在项目内 requirements/exports/ 下（与正式资源分离），
// 无项目时回退为当前工作目录下的默认文件名。
QString EngineeringRequirementsWidget::requirementCopyExportPath(const QString& projectDirectory)
{
    if (projectDirectory.isEmpty())
        return QStringLiteral("requirements.requirements.json");
    return QDir(projectDirectory).filePath(
        QStringLiteral("requirements/exports/requirements-copy.requirements.json"));
}

// 导入副本的初始目录：优先项目的 requirements/exports（已有导出副本），否则回退
// requirements 目录；无项目时返回空让文件对话框使用默认位置。
QString EngineeringRequirementsWidget::requirementCopyImportDirectory(const QString& projectDirectory)
{
    if (projectDirectory.isEmpty())
        return QString();
    const QDir project(projectDirectory);
    const QString exports = project.filePath(QStringLiteral("requirements/exports"));
    return QDir(exports).exists() ? exports : project.filePath(QStringLiteral("requirements"));
}

// 记录项目清单中 robot-model.main 资源解析出的绝对路径（由插件在项目打开/模型变化时
// 同步，空表示当前项目尚未生成机器人模型）。仅用于绑定，不参与需求资源序列化。
void EngineeringRequirementsWidget::setProjectModelPath(const QString& modelPath)
{
    _projectModelPath = modelPath.trimmed().isEmpty() ? QString() :
        QFileInfo(modelPath).absoluteFilePath();
}

bool EngineeringRequirementsWidget::loadRobotModelDocument(const QString& path,
                                                            const QString& projectRoot,
                                                            RobotModelSpec& model,
                                                            QString* error) const
{
    QFile modelFile(path);
    if (!modelFile.open(QFile::ReadOnly)) {
        if (error != nullptr)
            *error = QStringLiteral("Cannot read robot model: %1").arg(path);
        return false;
    }

    RobotModelSpec storedModel;
    std::string parseError;
    if (!RobotModelSpecJson::fromJson(
            modelFile.readAll().toStdString(), storedModel, &parseError)) {
        if (error != nullptr)
            *error = QString::fromStdString(parseError);
        return false;
    }

    if (projectRoot.trimmed().isEmpty()) {
        model = storedModel;
    } else {
        QString pathError;
        if (!RobotModelProjectPaths::resolveManaged(
                storedModel, QFileInfo(projectRoot).absoluteFilePath(), model, &pathError)) {
            if (error != nullptr)
                *error = pathError;
            return false;
        }
    }
    if (error != nullptr)
        error->clear();
    return true;
}

// 自动绑定工程生成模型：只解析项目清单 robot-model.main 指向的 .rmb.json，
// 并校验其 robotName 与当前 WorkCell 设备一致后写入 requirements 的 modelBinding。
bool EngineeringRequirementsWidget::bindGeneratedProjectModel(QString* error)
{
    if (_workcell == nullptr) {
        if (error != nullptr) *error = QStringLiteral("No WorkCell is open.");
        return false;
    }
    if (_projectOutputDirectory.isEmpty()) {
        if (error != nullptr) *error = QStringLiteral("No project is open.");
        return false;
    }

    if (_projectModelPath.isEmpty()) {
        if (error != nullptr)
            *error = QStringLiteral("The project has no robot-model.main resource. Save and load a model in RobotModelBuilder first.");
        return false;
    }

    const QString path = _projectModelPath;
    RobotModelSpec model;
    QString modelError;
    if (!loadRobotModelDocument(path, _projectOutputDirectory, model, &modelError)) {
        if (error != nullptr)
            *error = QStringLiteral("Project robot model is invalid: %1").arg(modelError);
        return false;
    }

    // 工程模型必须与当前 WorkCell 中的设备名称一致，否则冻结校验的机器人绑定会指向错误对象。
    bool matchesDevice = false;
    for (const rw::models::Device::Ptr& device : _workcell->getDevices()) {
        if (device != nullptr && device->getName() == model.robotName) {
            matchesDevice = true;
            break;
        }
    }
    if (!matchesDevice) {
        if (error != nullptr)
            *error = QStringLiteral("The project model robot does not match the current WorkCell device.");
        return false;
    }

    // 绑定以源路径 + 名称 + 内容指纹记录；后续冻结会重新读取模型核验指纹。
    _requirements.modelBinding.sourcePath = path.toStdString();
    _requirements.modelBinding.robotName = model.robotName;
    _requirements.modelBinding.robotModelFingerprint = RobotModelFingerprint::canonicalSha256(model);
    // 与手动换绑保持一致：模型来源变化后，旧编译结果和冻结工件不再能证明当前绑定。
    _compiled = CompiledRequirementSet();
    _frozenArtifact = FrozenRequirementArtifact();
    if (error != nullptr) error->clear();
    refreshTables();
    Q_EMIT requirementsChanged();
    return true;
}

void EngineeringRequirementsWidget::saveRequirements()
{
    syncTablesToRequirements();
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Export Requirements"),
        requirementCopyExportPath(_projectOutputDirectory), "Requirement set (*.requirements.json)");
    if (path.isEmpty()) return;
    QString error;
    // 此按钮现在表示“导出一份项目外需求文件”。它复用相同的格式写入逻辑，但故意
    // 不调用 markProjectDocumentClean()，以免导出文件被误认为已保存回 rwproj 资源。
    if (!writeRequirementDocument(path, &error)) { setStatus(error); return; }
    setStatus(QStringLiteral("Requirements exported."));
}

void EngineeringRequirementsWidget::loadRequirements()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Import Requirements"),
        requirementCopyImportDirectory(_projectOutputDirectory), "Requirement set (*.requirements.json)");
    if (path.isEmpty()) return;
    QString error;
    // 此按钮现在表示“导入项目外需求文件”。导入后应与当前 rwproj 的已保存快照比较，
    // 而非以导入文件作为新的项目基线，否则用户会在未保存项目时丢失修改提示。
    if (!loadRequirementDocument(path, false, &error)) { setStatus(error); return; }
}

bool EngineeringRequirementsWidget::loadProjectDocument(const QString& path, QString* error,
                                                         const QString& projectRoot)
{
    // Provider 只在资源路径已由 Registry 校验后进入这里；以项目资源为新的基线，
    // 保证刚打开 rwproj 时标题栏不会出现无意义的未保存星号。
    return loadRequirementDocument(path, true, error, projectRoot);
}

bool EngineeringRequirementsWidget::saveProjectDocument(const QString& targetPath, QString* error)
{
    if (!writeRequirementDocument(targetPath, error))
        return false;

    // targetPath 是保存事务的同目录暂存文件。记录它的规范字节但不立刻设为干净；若
    // 之后任一资源提交失败，Provider 不会调用 markClean()，旧基线仍然完好。
    _pendingProjectDocumentSnapshot = serializedProjectDocument(targetPath);
    return true;
}

bool EngineeringRequirementsWidget::isProjectDocumentDirty()
{
    if (_projectDocumentPath.isEmpty())
        return false;
    if (_projectDocumentMigrationPending)
        return true;
    // 表格编辑器可能尚未触发失焦提交。比较前强制同步，令项目脏状态和最终保存内容
    // 使用同一份领域数据，避免“显示干净但保存后发生变化”的不一致。
    syncTablesToRequirements();
    return serializedProjectDocument(_projectDocumentPath) != _savedProjectDocumentSnapshot;
}

void EngineeringRequirementsWidget::markProjectDocumentClean()
{
    if (!_pendingProjectDocumentSnapshot.isEmpty()) {
        _savedProjectDocumentSnapshot = _pendingProjectDocumentSnapshot;
        _pendingProjectDocumentSnapshot.clear();
    } else if (!_projectDocumentPath.isEmpty()) {
        // 打开项目时没有保存暂存文件，直接从当前模型生成基线快照。
        _savedProjectDocumentSnapshot = serializedProjectDocument(_projectDocumentPath);
    }
    _projectDocumentMigrationPending = false;
}

// 首次编辑生成资源后建立会话基线：记录项目内路径并以当前配置为已保存快照，
// 使随后的编辑能正确参与主窗口脏状态；路径仅存于运行时，不进入业务 JSON。
void EngineeringRequirementsWidget::beginGeneratedProjectDocument(const QString& path)
{
    _projectDocumentPath = path;
    _pendingProjectDocumentSnapshot.clear();
    _savedProjectDocumentSnapshot = serializedProjectDocument(path);
    _projectDocumentMigrationPending = false;
}

// 项目关闭或切换时释放仅用于脏比较的路径与快照，防止旧项目基线污染新项目。
void EngineeringRequirementsWidget::clearProjectDocumentContext()
{
    // 关闭/切换项目时执行完整会话重置:除脏比较所需的路径与快照外,还要销毁
    // 上一项目的全部需求数据与 UI 状态,确保新项目不会继承旧项目任何内容。
    // -- 数据层:需求集合、编译结果、冻结产物与撤销历史一并清空 --
    _pendingFrozenArtifactValidation = false;
    _pendingFrozenArtifactProjectRoot.clear();
    _requirements = RequirementSet();
    _compiled = CompiledRequirementSet();
    _frozenArtifact = FrozenRequirementArtifact();
    _undoStack.clear();
    // -- 项目关联:输出目录与模型路径归零,站点朝向坐标视为已解析 --
    _projectOutputDirectory.clear();
    _projectModelPath.clear();
    _stationOrientationCoordinatesResolved = true;
    // -- 脏比较基线:项目文档路径与保存/待保存快照全部作废,避免旧基线污染新项目 --
    _projectDocumentPath.clear();
    _savedProjectDocumentSnapshot.clear();
    _pendingProjectDocumentSnapshot.clear();
    _projectDocumentMigrationPending = false;
    // 重置后刷新表格,使关键工位列表与盒体区域表格立即反映清空后的状态。
    refreshTables();
}

QByteArray EngineeringRequirementsWidget::serializedProjectDocument(const QString& documentPath) const
{
    // 工程目录可以整体搬迁，故模型引用必须相对于所属需求 JSON 保存。绝对路径只在
    // 内存中参与模型读取与冻结校验，绝不被写进项目资源。
    QJsonObject project = RequirementSetJson::toObject(_requirements);
    // frozenArtifact 属于完整需求文档的 envelope，不属于 RequirementSet 扩展。
    // 即使内存数据来自受影响的旧版本，也只允许在顶层写出一份规范工件。
    if (project.value("extensions").isObject()) {
        QJsonObject extensions = project.value("extensions").toObject();
        extensions.remove("frozenArtifact");
        if (extensions.isEmpty())
            project.remove("extensions");
        else
            project["extensions"] = extensions;
    }
    const QDir documentDirectory(QFileInfo(documentPath).absolutePath());
    const auto relativizeBinding = [&documentDirectory](QJsonObject& binding) {
        const QString sourcePath = binding.value("sourcePath").toString();
        if (sourcePath.isEmpty())
            return;
        const QFileInfo sourceInfo(sourcePath);
        const QString absolutePath = sourceInfo.isRelative() ?
            documentDirectory.absoluteFilePath(sourcePath) : sourceInfo.absoluteFilePath();
        binding["sourcePath"] = documentDirectory.relativeFilePath(absolutePath);
    };

    QJsonObject modelBinding = project.value("modelBinding").toObject();
    relativizeBinding(modelBinding);
    project["modelBinding"] = modelBinding;
    if (_requirements.frozen && !_frozenArtifact.requirementFingerprint.empty()) {
        // Keep all three copies of the model path in the frozen artifact portable.
        // The execution fingerprint includes provenance.sourcePath, so recompute it
        // after the representation-only path change.
        FrozenRequirementArtifact portableArtifact = _frozenArtifact;
        const auto relativizePath = [&documentDirectory] (std::string& sourcePath) {
            if (sourcePath.empty()) return;
            const QFileInfo sourceInfo(QString::fromStdString(sourcePath));
            const QString absolutePath = sourceInfo.isRelative()
                ? documentDirectory.absoluteFilePath(sourceInfo.filePath())
                : sourceInfo.absoluteFilePath();
            sourcePath = documentDirectory.relativeFilePath(absolutePath).toStdString();
        };
        relativizePath(portableArtifact.modelBinding.sourcePath);
        relativizePath(portableArtifact.compiled.modelBinding.sourcePath);
        relativizePath(portableArtifact.execution.provenance.sourcePath);
        portableArtifact.executionFingerprint =
            RequirementExecutionJson::fingerprint(portableArtifact.execution);
        project["frozenArtifact"] = FrozenRequirementArtifactJson::toObject(portableArtifact);
    }
    return QJsonDocument(project).toJson(QJsonDocument::Indented);
}

// 把需求写入目标文件（项目暂存路径或导出副本共用）：先同步表格编辑，再用
// QSaveFile 原子写入规范化 JSON，避免中断留下半截需求文件。
bool EngineeringRequirementsWidget::writeRequirementDocument(const QString& targetPath, QString* error)
{
    syncTablesToRequirements();
    const QString directory = QFileInfo(targetPath).absolutePath();
    if (!QDir().mkpath(directory)) {
        if (error != nullptr)
            *error = QString::fromUtf8("Unable to create requirement directory: %1").arg(directory);
        return false;
    }
    QSaveFile file(targetPath);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error != nullptr)
            *error = QStringLiteral("Cannot save requirements file: %1").arg(file.errorString());
        return false;
    }
    if (file.write(serializedProjectDocument(targetPath)) < 0 || !file.commit()) {
        if (error != nullptr)
            *error = QStringLiteral("Cannot commit requirements file: %1").arg(file.errorString());
        return false;
    }
    if (error != nullptr)
        error->clear();
    return true;
}

// 加载需求文档：解析 JSON 并把相对模型路径还原为绝对路径，再重建冻结工件。
// captureProjectSnapshot 为 true 时（项目 Provider 路径）把加载内容作为新的
// 已保存基线；false 时（导入副本）不更新基线，避免未保存项目丢失脏提示。
bool EngineeringRequirementsWidget::loadRequirementDocument(const QString& path,
                                                            bool captureProjectSnapshot,
                                                            QString* error,
                                                            const QString& projectRoot)
{
    _pendingFrozenArtifactValidation = false;
    _pendingFrozenArtifactProjectRoot.clear();
    QFile file(path); if (!file.open(QFile::ReadOnly)) { if (error != nullptr) *error = QStringLiteral("Cannot read requirements file: %1").arg(file.errorString()); return false; }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error != nullptr) *error = QStringLiteral("Requirements file is not valid JSON: %1").arg(parseError.errorString());
        return false;
    }
    RequirementDocumentMigrationResult documentMigration;
    std::string migrationError;
    if (!migrateRequirementDocument(document.object(), documentMigration, &migrationError)) {
        if (error != nullptr) *error = QString::fromStdString(migrationError);
        return false;
    }
    QJsonObject project = documentMigration.document;
    const QJsonValue artifactValue = project.value("frozenArtifact");
    // RequirementSetJson 仍保持对未知扩展冲突的严格保护。仅在完整文档边界移除
    // 已明确归属 envelope 的 frozenArtifact，避免它再次进入 RequirementSet.extensions。
    QJsonObject requirementSetObject = project;
    requirementSetObject.remove("frozenArtifact");
    // 将项目内保存的相对模型路径恢复为绝对路径，后续冻结真实性校验与模型读取接口
    // 仍可沿用既有约定；冻结工件中的模型绑定也必须同步恢复。
    const QDir documentDirectory(QFileInfo(path).absolutePath());
    const auto resolveBinding = [&documentDirectory](QJsonObject& binding) {
        const QString sourcePath = binding.value("sourcePath").toString();
        if (!sourcePath.isEmpty() && QFileInfo(sourcePath).isRelative())
            binding["sourcePath"] = documentDirectory.absoluteFilePath(sourcePath);
    };
    QJsonObject modelBinding = requirementSetObject.value("modelBinding").toObject();
    resolveBinding(modelBinding);
    requirementSetObject["modelBinding"] = modelBinding;
    RequirementSet parsed;
    std::string parseMessage;
    if (!RequirementSetJson::fromObject(requirementSetObject, parsed, &parseMessage)) { if (error != nullptr) *error = QString::fromStdString(parseMessage); return false; }

    CompiledRequirementSet compiled;
    FrozenRequirementArtifact artifact;
    const QString validationRoot = projectRoot.trimmed().isEmpty()
        ? _projectOutputDirectory
        : QFileInfo(projectRoot).absoluteFilePath();
    QString loadStatus = QStringLiteral("Requirements loaded and editable.");
    if (!artifactValue.isUndefined()) {
        const bool artifactParsed = artifactValue.isObject() &&
            FrozenRequirementArtifactJson::fromObject(
                artifactValue.toObject(), artifact, &parseMessage);
        if (!artifactParsed) {
            parsed.frozen = false;
            artifact = FrozenRequirementArtifact();
            const QString reason = artifactValue.isObject()
                ? QString::fromStdString(parseMessage)
                : QStringLiteral("frozenArtifact must be an object");
            loadStatus = QStringLiteral(
                "Requirements loaded, but frozen evidence is invalid (%1). Freeze again.")
                             .arg(reason);
        } else {
            // Validate the portable artifact before resolving paths. Afterwards restore
            // every duplicated path in memory and rehash its execution contract so its
            // internal provenance remains coherent at the moved project location.
            const auto resolveArtifactPath = [&documentDirectory] (std::string& sourcePath) {
                if (sourcePath.empty()) return;
                const QFileInfo sourceInfo(QString::fromStdString(sourcePath));
                if (sourceInfo.isRelative())
                    sourcePath = documentDirectory.absoluteFilePath(sourceInfo.filePath()).toStdString();
            };
            resolveArtifactPath(artifact.modelBinding.sourcePath);
            resolveArtifactPath(artifact.compiled.modelBinding.sourcePath);
            resolveArtifactPath(artifact.execution.provenance.sourcePath);
            artifact.executionFingerprint = RequirementExecutionJson::fingerprint(artifact.execution);

            // 重开项目时重新读取模型并比对当前 WorkCell/State。任何一项不一致都
            // 会将需求降级为编辑态，明确要求工程师在当前工程环境中再次冻结。
            RobotModelSpec model;
            QString modelError;
            const bool modelReadable = !parsed.modelBinding.sourcePath.empty() &&
                loadRobotModelDocument(QString::fromStdString(parsed.modelBinding.sourcePath),
                                       validationRoot, model, &modelError);
            if (!modelReadable)
                parseMessage = modelError.toStdString();
            if (_workcell == nullptr) {
                // Project resources are loaded before RobWorkStudio opens the WorkCell.
                // Preserve the artifact and verify it when the WorkCell arrives.
                parsed.frozen = false;
                _pendingFrozenArtifactValidation = true;
                _pendingFrozenArtifactProjectRoot = validationRoot;
                loadStatus = QStringLiteral(
                    "Requirements and frozen audit artifact loaded; waiting for the WorkCell to verify them.");
            } else {
                FrozenRequirementValidationResult validationResult;
                const bool artifactCurrent = modelReadable &&
                    RequirementFreezer::isCurrent(
                        artifact, parsed, *_workcell, activeWorkCellState(), model, &parseMessage,
                        validationRoot.toStdString(), &validationResult);
                if (artifactCurrent) {
                    parsed.frozen = true;
                    compiled = artifact.compiled;
                    loadStatus = QStringLiteral(
                        "Requirements and frozen audit artifact loaded and match the current "
                        "model and WorkCell.");
                    for (const std::string& warning : validationResult.warnings)
                        loadStatus += QStringLiteral("\nWarning: %1")
                                          .arg(QString::fromStdString(warning));
                } else {
                    parsed.frozen = false;
                    artifact = FrozenRequirementArtifact();
                    const QString reason = QString::fromStdString(parseMessage);
                    loadStatus = QStringLiteral(
                        "Requirements loaded, but frozen evidence is stale or cannot be "
                        "verified (%1). Freeze again.")
                                     .arg(reason);
                }
            }
        }
    } else if (parsed.frozen) {
        // 兼容旧项目：旧格式只有 frozen 标志而没有工件、环境和模型证据，必须
        // 视为未验证，不能让它绕过当前版本的冻结门禁。
        parsed.frozen = false;
        loadStatus = QStringLiteral("Requirements loaded. The legacy file has no frozen audit artifact; freeze again.");
    }
    if (documentMigration.migrated) {
        loadStatus += QStringLiteral(
            "\nHistorical requirements document format was migrated. Save the project to "
            "persist the normalized format.");
        for (const std::string& warning : documentMigration.warnings)
            loadStatus += QStringLiteral("\nMigration warning: %1")
                              .arg(QString::fromStdString(warning));
    }

    _requirements = parsed;
    _compiled = compiled;
    _frozenArtifact = artifact;
    _undoStack.clear();
    setStatus(loadStatus);
    refreshTables();
    if (captureProjectSnapshot) {
        if (!projectRoot.trimmed().isEmpty())
            _projectOutputDirectory = validationRoot;
        _projectDocumentPath = path;
        _pendingProjectDocumentSnapshot.clear();
        _savedProjectDocumentSnapshot = serializedProjectDocument(path);
        _projectDocumentMigrationPending = documentMigration.migrated;
    }
    Q_EMIT requirementsChanged();
    if (error != nullptr) error->clear();
    return true;
}

void EngineeringRequirementsWidget::validateLoadedFrozenArtifact()
{
    if (!_pendingFrozenArtifactValidation || _workcell == nullptr)
        return;

    _pendingFrozenArtifactValidation = false;
    const QString validationRoot = _pendingFrozenArtifactProjectRoot;
    _pendingFrozenArtifactProjectRoot.clear();
    RobotModelSpec model;
    QString modelError;
    std::string validationMessage;
    const bool modelReadable = !_requirements.modelBinding.sourcePath.empty() &&
        loadRobotModelDocument(QString::fromStdString(_requirements.modelBinding.sourcePath),
                               validationRoot, model, &modelError);
    if (!modelReadable)
        validationMessage = modelError.toStdString();
    FrozenRequirementValidationResult validationResult;
    const bool artifactCurrent = modelReadable &&
        RequirementFreezer::isCurrent(
            _frozenArtifact, _requirements, *_workcell, activeWorkCellState(), model,
            &validationMessage, validationRoot.toStdString(), &validationResult);
    if (artifactCurrent) {
        _requirements.frozen = true;
        _compiled = _frozenArtifact.compiled;
        QString status = QStringLiteral(
            "Requirements and frozen audit artifact loaded and match the current model and WorkCell.");
        for (const std::string& warning : validationResult.warnings)
            status += QStringLiteral("\nWarning: %1").arg(QString::fromStdString(warning));
        setStatus(status);
    } else {
        _requirements.frozen = false;
        _compiled = CompiledRequirementSet();
        _frozenArtifact = FrozenRequirementArtifact();
        const QString reason = validationMessage.empty() ?
            QStringLiteral("Frozen evidence could not be verified") :
            QString::fromStdString(validationMessage);
        setStatus(QStringLiteral(
            "Requirements loaded, but frozen evidence is stale or cannot be verified (%1). Freeze again.")
                      .arg(reason));
    }
    if (_projectDocumentMigrationPending) {
        setStatus(statusText() + QStringLiteral(
            "\nHistorical requirements document format was migrated. Save the project to "
            "persist the normalized format."));
    }
    refreshTables();
    if (!_projectDocumentPath.isEmpty())
        _savedProjectDocumentSnapshot = serializedProjectDocument(_projectDocumentPath);
    Q_EMIT requirementsChanged();
}

void EngineeringRequirementsWidget::importStations()
{
    if (_requirements.frozen) return;
    syncTablesToRequirements();
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Import Key Stations"), QString(),
        "Station data (*.csv *.json);;CSV (*.csv);;JSON (*.json)");
    if (path.isEmpty()) return;
    QFile file(path);
    if (!file.open(QFile::ReadOnly | QFile::Text)) {
        setStatus(QStringLiteral("Cannot read station import file."));
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
            details.push_back(QStringLiteral("Record %1: %2").arg(diagnostic.recordNumber).arg(QString::fromStdString(diagnostic.message)));
            if (details.size() == 8) break;
        }
        const QString message = details.isEmpty() ? QString::fromStdString(error) : details.join('\n');
        QMessageBox::warning(this, QStringLiteral("Station Import Failed"), message);
        setStatus(QStringLiteral("No stations imported. Fix the record diagnostics and try again."));
        return;
    }
    // 服务成功后才记录操作前快照，保证撤销栈中不出现失败导入或用户取消的伪操作。
    pushUndoSnapshot(before);
    refreshTables();
    setStatus(QStringLiteral("Imported %2 key stations from %1.").arg(path).arg(result.importedCount));
    Q_EMIT requirementsChanged();
}

void EngineeringRequirementsWidget::undoLastOperation()
{
    if (_requirements.frozen) return;
    if (!_undoStack.undo(_requirements)) {
        setStatus(QStringLiteral("No batch operation to undo."));
        return;
    }
    // 撤销后原冻结编译产物不再可信，必须等待工程师重新校验并冻结。
    _compiled = CompiledRequirementSet();
    refreshTables();
    setStatus(QStringLiteral("Restored requirements from before the last batch operation."));
    Q_EMIT requirementsChanged();
}

void EngineeringRequirementsWidget::redoLastOperation()
{
    if (_requirements.frozen || !_undoStack.redo(_requirements)) return;
    // 重做同样使编译缓存失效，后续冻结必须基于恢复后的完整编辑态重新校验。
    _compiled = CompiledRequirementSet();
    _frozenArtifact = FrozenRequirementArtifact();
    refreshTables();
    setStatus(QStringLiteral("Redid the last requirements edit."));
    Q_EMIT requirementsChanged();
}

void EngineeringRequirementsWidget::freezeRequirements()
{
    if (_freezeReadinessCheck) {
        QString readinessError;
        if (!_freezeReadinessCheck(&readinessError)) {
            setStatus(readinessError);
            return;
        }
    }
    syncTablesToRequirements();
    if (_workcell == nullptr) {
        setStatus(QStringLiteral("Cannot freeze requirements. Open the WorkCell to validate frames, TCPs, and fixtures."));
        return;
    }
    // 编辑态未绑定模型时，尝试绑定项目清单中的 robot-model.main 工程模型；
    // 无法自动绑定时给出明确提示，而不是以空的 modelBinding 继续冻结。
    if (_requirements.modelBinding.sourcePath.empty()) {
        QString bindingError;
        if (!bindGeneratedProjectModel(&bindingError)) {
            setStatus(QString::fromUtf8(
                "Cannot freeze requirements. Create and save a matching .rmb.json model in RobotModelBuilder. %1")
                .arg(bindingError.isEmpty() ? QString() : QStringLiteral("Reason: ") + bindingError));
            return;
        }
    }

    // 绑定路径只是编辑态引用，冻结时必须重新读取模型并以其内容指纹核验，防止
    // 文件已被替换而需求仍沿用过期模型指纹的情况进入结构优化链路。
    RobotModelSpec model;
    std::string error;
    QString modelError;
    if (!loadRobotModelDocument(
            QString::fromStdString(_requirements.modelBinding.sourcePath),
            _projectOutputDirectory, model, &modelError)) {
        setStatus(QStringLiteral("Cannot freeze requirements: %1").arg(modelError));
        return;
    }

    FrozenRequirementArtifact artifact;
    // RequirementFreezer 集中完成真实 Frame/TCP、几何特征与模型指纹门禁，并
    // 将 Should 项的排除理由写入工件，避免旧界面逻辑仅在内存中临时删任务。
    if (!RequirementFreezer::freeze(_requirements, *_workcell, activeWorkCellState(), model, artifact,
                                    &error, _projectOutputDirectory.toStdString())) {
        setStatus(QString::fromStdString(error));
        return;
    }
    _requirements.frozen = true;
    _compiled = artifact.compiled;
    artifact.publication.revisionNumber = std::max(1, _requirements.version);
    artifact.publication.revisionId = "REQ-" + std::to_string(artifact.publication.revisionNumber);
    artifact.publication.state = "published";
    artifact.publication.publishedAt = artifact.frozenAt;
    _frozenArtifact = artifact;
    // 统计实际进入 P2 优化的工位数：只计 Included 且非 Info 级(Info 仅作审计记录)。
    const int availableTasks = static_cast<int>(std::count_if(
        _compiled.poseTasks.begin(), _compiled.poseTasks.end(), [] (const CompiledPoseTask& task) {
            return task.compileState == RequirementCompileState::Included &&
                   task.level != RequirementLevel::Info;
        }));
    // 有待验证接近/撤离(路径)规则的工位数：只有 Included 项才计入。
    const int pathPending = static_cast<int>(std::count_if(_compiled.poseTasks.begin(), _compiled.poseTasks.end(), [] (const CompiledPoseTask& task) {
        return task.compileState == RequirementCompileState::Included && task.pathValidationPending;
    }));
    // 建议性(非阻塞)需求按 requirementId 去重统计，提示"未验证、未进入优化"的条数，
    // 与工位总数(含 Info)和诊断总数解耦，避免把审计诊断数量误报为需求数量。
    std::set<std::string> advisoryRequirementIds;
    for (const RequirementDiagnostic& diagnostic : _compiled.diagnostics) {
        if (!diagnostic.blocking && !diagnostic.requirementId.empty())
            advisoryRequirementIds.insert(diagnostic.requirementId);
    }
    setStatus(QStringLiteral("Requirements frozen: %1 stations are available for kinematic optimization; %2 optional requirements are excluded; %3 approach/retract rules are recorded.")
        .arg(availableTasks).arg(advisoryRequirementIds.size()).arg(pathPending));
    refreshTables();
    Q_EMIT requirementsChanged();
    // 发布请求携带资源元数据：资源 id、项目内文档路径、需求指纹与 schema 版本，
    // 供插件把冻结工件发布到正确的项目资源位置并用于后续一致性核对。
    Q_EMIT freezePublicationRequested(QStringLiteral("engineering-requirements"),
                                      _projectDocumentPath,
                                      QString::fromStdString(_frozenArtifact.requirementFingerprint),
                                      _frozenArtifact.schemaVersion);
}
void EngineeringRequirementsWidget::unfreezeRequirements()
{
    if (!_requirements.frozen) return;
    _requirements.frozen = false;
    _compiled = CompiledRequirementSet();
    _frozenArtifact = FrozenRequirementArtifact();
    setStatus(QStringLiteral("Requirements are editable."));
    refreshTables();
    Q_EMIT requirementsChanged();
    Q_EMIT requirementsUnfrozen();
}
void EngineeringRequirementsWidget::addPoseTask()
{
    if (_requirements.frozen) return;
    syncTablesToRequirements();
    const RequirementSet before = _requirements;
    PoseTask task;
    task.id = "station_" + std::to_string(_requirements.poseTasks.size() + 1);
    task.name = QStringLiteral("Key Station %1").arg(_requirements.poseTasks.size() + 1).toStdString();
    task.refFrame = "WORLD";
    _requirements.poseTasks.push_back(task);
    recordRequirementEdit(before);
    if (_stationList != nullptr) _stationList->setCurrentRow(static_cast<int>(_requirements.poseTasks.size()) - 1);
}

void EngineeringRequirementsWidget::duplicatePoseTask()
{
    if (_requirements.frozen) return;
    syncTablesToRequirements();
    const int row = selectedKeyStationIndex();
    if (row < 0 || row >= static_cast<int>(_requirements.poseTasks.size())) return;
    const RequirementSet before = _requirements;
    PoseTask copy = _requirements.poseTasks[static_cast<std::size_t>(row)];
    copy.id += "_copy";
    copy.name += " Copy";
    _requirements.poseTasks.insert(_requirements.poseTasks.begin() + row + 1, copy);
    recordRequirementEdit(before);
    if (_stationList != nullptr) _stationList->setCurrentRow(row + 1);
}

void EngineeringRequirementsWidget::removePoseTask()
{
    if (_requirements.frozen) return;
    syncTablesToRequirements();
    const int row = selectedKeyStationIndex();
    if (row < 0 || row >= static_cast<int>(_requirements.poseTasks.size())) return;
    const RequirementSet before = _requirements;
    _requirements.poseTasks.erase(_requirements.poseTasks.begin() + row);
    recordRequirementEdit(before);
}
void EngineeringRequirementsWidget::captureCurrentTcp()
{
    if (_requirements.frozen) return;
    if (_workcell == nullptr || _workcell->getDevices().empty()) {
        setStatus(QStringLiteral("Cannot capture TCP pose. Open a WorkCell with a device."));
        return;
    }
    // 捕获 TCP 优先取绑定模型对应的设备，确保捕获到的末端就是实际用于分析的
    // 机器人；找不到绑定设备时才回退到第一个设备(与旧行为一致)。
    rw::core::Ptr<rw::models::Device> device;
    if (!_requirements.modelBinding.robotName.empty())
        device = _workcell->findDevice(_requirements.modelBinding.robotName);
    if (device == nullptr)
        device = _workcell->getDevices().front();
    if (device == nullptr || device->getBase() == nullptr || device->getEnd() == nullptr) {
        setStatus(QStringLiteral("Cannot capture TCP pose. The default device has no valid base or TCP."));
        return;
    }
    try {
        const rw::math::Transform3D<> baseTtcp = rw::kinematics::Kinematics::frameTframe(
            device->getBase(), device->getEnd(), activeWorkCellState());
        const rw::math::RPY<> rpy(baseTtcp.R());
        syncTablesToRequirements();
        const RequirementSet before = _requirements;
        PoseTask task;
        task.id = "station_" + std::to_string(_requirements.poseTasks.size() + 1);
        task.name = QStringLiteral("TCP Capture %1").arg(_requirements.poseTasks.size() + 1).toStdString();
        task.source = PoseTaskSource::CapturedTcp;
        task.refFrame = device->getBase()->getName();
        task.tcpFrame = device->getEnd()->getName();
        for (int axis = 0; axis < 3; ++axis) {
            task.position[axis] = baseTtcp.P()[axis];
            task.rpyDeg[axis] = rpy[axis] * 180.0 / rw::math::Pi;
        }
        task.note = "Captured from current WorkCell TCP.";
        _requirements.poseTasks.push_back(task);
        setStatus(QStringLiteral("TCP pose captured. Adjust its level and tolerance before freezing."));
        recordRequirementEdit(before);
    } catch (const std::exception& exception) {
        setStatus(QStringLiteral("Cannot capture TCP pose: %1").arg(QString::fromUtf8(exception.what())));
    }
}
void EngineeringRequirementsWidget::requestGeometryFeaturePick()
{
    if (_requirements.frozen || _workcell == nullptr || selectedKeyStationIndex() < 0) {
        setStatus(QStringLiteral("Open a WorkCell, select a key station, and keep requirements editable."));
        return;
    }
    setStatus(QStringLiteral("Geometry pick enabled. Ctrl+double-click a fixture or part frame in the 3D view."));
    Q_EMIT geometryFeaturePickRequested();
}
bool EngineeringRequirementsWidget::applyGeometryFeatureFrame(const QString& frameName, QString* error)
{
    if (_requirements.frozen || _workcell == nullptr) {
        const QString message = QStringLiteral("Open a WorkCell and edit requirements before picking geometry.");
        if (error != nullptr) *error = message;
        setStatus(message);
        return false;
    }
    const int selected = selectedKeyStationIndex();
    if (selected < 0 || selected >= static_cast<int>(_requirements.poseTasks.size())) {
        const QString message = QStringLiteral("Select a key station first.");
        if (error != nullptr) *error = message;
        setStatus(message);
        return false;
    }
    const RequirementSet before = _requirements;
    PoseTask& station = _requirements.poseTasks[static_cast<std::size_t>(selected)];
    GeometryFeatureReference feature;
    feature.type = GeometryFeatureType::FramePlaneNormal;
    feature.frameName = frameName.toStdString();
    feature.objectName = feature.frameName;
    feature.geometryName = "FramePlaneNormal";
    std::string resolveError;
    if (!GeometryFeatureResolver::applyToStation(feature, *_workcell, activeWorkCellState(), station, &resolveError)) {
        const QString message = QString::fromStdString(resolveError);
        if (error != nullptr) *error = message;
        setStatus(message);
        return false;
    }
    recordRequirementEdit(before);
    setStatus(QStringLiteral("Geometry frame %1 linked. The station resolves against the current WorkCell.").arg(frameName));
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
    if (!editTemplateRequest(this, _workcell, request, false, QStringLiteral("Create Template Stations")))
        return;
    const RequirementSet before = _requirements;
    const int firstGeneratedRow = static_cast<int>(_requirements.poseTasks.size());
    std::string error;
    if (!StationTemplateService::appendTemplate(_requirements, request, &error)) {
        QMessageBox::warning(this, QStringLiteral("Cannot Create Template"), QString::fromStdString(error));
        return;
    }
    pushUndoSnapshot(before);
    refreshTables();
    if (_stationList != nullptr) _stationList->setCurrentRow(firstGeneratedRow);
    setStatus(QStringLiteral("Template instance %1 created. Its stations remain linked for later updates.").arg(QString::fromStdString(request.instanceId)));
    Q_EMIT requirementsChanged();
}

void EngineeringRequirementsWidget::updateSelectedTemplateStations()
{
    if (_requirements.frozen) return;
    syncTablesToRequirements();
    const int selected = selectedKeyStationIndex();
    if (selected < 0 || selected >= static_cast<int>(_requirements.poseTasks.size())) {
        setStatus(QStringLiteral("Select a key station that is still linked to a template."));
        return;
    }
    const PoseTask& station = _requirements.poseTasks[static_cast<std::size_t>(selected)];
    StationTemplateRequest request;
    if (!station.generation.linked || !templateRequestFromStation(station, request)) {
        setStatus(QStringLiteral("This station is not an updatable template station."));
        return;
    }
    if (!editTemplateRequest(this, _workcell, request, true, QStringLiteral("Update Template")))
        return;
    TemplateUpdatePreview preview;
    std::string error;
    if (!StationTemplateService::previewTemplateUpdate(_requirements, station.generation.instanceId, request, preview, &error)) {
        QMessageBox::warning(this, QStringLiteral("Cannot Preview Template Update"), QString::fromStdString(error));
        return;
    }
    const QString message = QStringLiteral("This update replaces %1 linked stations and creates %2 new stations. Detached stations are unchanged. Continue?")
        .arg(preview.replacedStationIds.size()).arg(preview.generatedStations.size());
    if (QMessageBox::question(this, QStringLiteral("Confirm Template Update"), message,
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;
    const RequirementSet before = _requirements;
    if (!StationTemplateService::applyTemplateUpdate(_requirements, preview, &error)) {
        QMessageBox::warning(this, QStringLiteral("Cannot Apply Template Update"), QString::fromStdString(error));
        return;
    }
    pushUndoSnapshot(before);
    refreshTables();
    setStatus(QStringLiteral("Template instance %1 updated. Detached stations are unchanged.").arg(QString::fromStdString(request.instanceId)));
    Q_EMIT requirementsChanged();
}

void EngineeringRequirementsWidget::detachSelectedTemplateStation()
{
    if (_requirements.frozen) return;
    syncTablesToRequirements();
    const int selected = selectedKeyStationIndex();
    if (selected < 0 || selected >= static_cast<int>(_requirements.poseTasks.size())) {
        setStatus(QStringLiteral("Select a key station generated by a template or array."));
        return;
    }
    PoseTask& station = _requirements.poseTasks[static_cast<std::size_t>(selected)];
    if (station.generation.instanceId.empty()) {
        setStatus(QStringLiteral("This station is already independent."));
        return;
    }
    if (!station.generation.linked) {
        setStatus(QStringLiteral("This station is detached and will not be updated by its template."));
        return;
    }
    const RequirementSet before = _requirements;
    std::string error;
    if (!StationTemplateService::detachStation(_requirements, station.id, &error)) {
        QMessageBox::warning(this, QStringLiteral("Cannot Detach Template"), QString::fromStdString(error));
        return;
    }
    pushUndoSnapshot(before);
    refreshTables();
    setStatus(QStringLiteral("Station %1 detached from its template.").arg(QString::fromStdString(station.name)));
    Q_EMIT requirementsChanged();
}

void EngineeringRequirementsWidget::createStationArray()
{
    if (_requirements.frozen) return;
    syncTablesToRequirements();
    const int selected = selectedKeyStationIndex();
    if (selected < 0 || selected >= static_cast<int>(_requirements.poseTasks.size())) {
        setStatus(QStringLiteral("Select a key station to generate an array."));
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
        QMessageBox::warning(this, QStringLiteral("Cannot Generate Array"), QString::fromStdString(error));
        return;
    }
    pushUndoSnapshot(before);
    refreshTables();
    if (_stationList != nullptr) _stationList->setCurrentRow(firstGeneratedRow);
    setStatus(QStringLiteral("Array instance %2 generated from station %1.")
        .arg(QString::fromStdString(source.name), QString::fromStdString(request.instanceId)));
    Q_EMIT requirementsChanged();
}

void EngineeringRequirementsWidget::mirrorSelectedStation()
{
    if (_requirements.frozen) return;
    syncTablesToRequirements();
    const int selected = selectedKeyStationIndex();
    if (selected < 0 || selected >= static_cast<int>(_requirements.poseTasks.size())) {
        setStatus(QStringLiteral("Select a key station to mirror."));
        return;
    }
    const PoseTask& source = _requirements.poseTasks[static_cast<std::size_t>(selected)];
    if (source.orientation.mode != OrientationMode::Fixed) {
        setStatus(QStringLiteral("Only fixed-orientation stations can be mirrored."));
        return;
    }
    bool accepted = false;
    const QStringList planes = {"YZ (X=0)", "XZ (Y=0)", "XY (Z=0)"};
    const QString selectedPlane = QInputDialog::getItem(this, QStringLiteral("Mirror Key Station"),
        QStringLiteral("Mirror plane through the reference-frame origin"), planes, 0, false, &accepted);
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
    setStatus(QStringLiteral("Mirrored station %1 created.")
        .arg(QString::fromStdString(mirrored.name)));
    Q_EMIT requirementsChanged();
}

void EngineeringRequirementsWidget::addBoxRegion()
{
    if (_requirements.frozen) return;
    syncTablesToRequirements();
    const RequirementSet before = _requirements;
    BoxRegion region;
    region.id = "box_" + std::to_string(_requirements.boxRegions.size() + 1);
    region.name = QStringLiteral("Workspace Region %1").arg(_requirements.boxRegions.size() + 1).toStdString();
    // 新建覆盖盒的默认 TCP 优先取绑定设备末端，避免默认值指向非绑定设备。
    region.tcpFrame = defaultTcpFrame(_workcell, _requirements.modelBinding.robotName);
    _requirements.boxRegions.push_back(region);
    recordRequirementEdit(before);
}

void EngineeringRequirementsWidget::duplicateBoxRegion()
{
    if (_requirements.frozen || _regionTable == nullptr) return;
    syncTablesToRequirements();
    const int row = _regionTable->currentRow();
    if (row < 0 || row >= static_cast<int>(_requirements.boxRegions.size())) return;
    const RequirementSet before = _requirements;
    BoxRegion copy = _requirements.boxRegions[static_cast<std::size_t>(row)];
    copy.id += "_copy";
    copy.name += " Copy";
    _requirements.boxRegions.insert(_requirements.boxRegions.begin() + row + 1, copy);
    recordRequirementEdit(before);
}

void EngineeringRequirementsWidget::removeBoxRegion()
{
    if (_requirements.frozen || _regionTable == nullptr) return;
    syncTablesToRequirements();
    const int row = _regionTable->currentRow();
    if (row < 0 || row >= static_cast<int>(_requirements.boxRegions.size())) return;
    const RequirementSet before = _requirements;
    _requirements.boxRegions.erase(_requirements.boxRegions.begin() + row);
    recordRequirementEdit(before);
}
void EngineeringRequirementsWidget::setWorkCell(rw::models::WorkCell* workcell)
{
    // State 的内部结构由 WorkCell 决定。切换场景时先丢弃旧 State，避免旧关节
    // 配置被用于新场景中的 TCP 捕获、几何解析或冻结环境指纹计算。
    _workcell = workcell;
    _currentState.reset();
    if (workcell != nullptr && _pendingFrozenArtifactValidation) {
        validateLoadedFrozenArtifact();
        return;
    }
    // 切换 WorkCell 会改变场景/环境指纹，旧的冻结与编译结果全部失效：解冻需求并
    // 清空编译快照与冻结工件，防止上一场景的"已验证"证据被带入新场景。
    _requirements.frozen = false;
    _compiled = CompiledRequirementSet();
    _frozenArtifact = FrozenRequirementArtifact();
    _pendingFrozenArtifactValidation = false;
    _pendingFrozenArtifactProjectRoot.clear();
    setStatus(workcell == nullptr ? QStringLiteral("No WorkCell is open. Referenced frames are unresolved.")
                                : QStringLiteral("Connected to the current WorkCell."));
    refreshTables();
}

void EngineeringRequirementsWidget::setCurrentState(const rw::kinematics::State& state)
{
    // 复制而不是保存外部引用：RobWorkStudio 会持续替换其 State，插件在按钮
    // 回调和冻结流程中需要一份仍然有效、与最后一次界面渲染一致的快照。
    _currentState = std::make_unique<rw::kinematics::State>(state);
}

rw::kinematics::State EngineeringRequirementsWidget::activeWorkCellState() const
{
    // 所有调用方已经检查 _workcell 非空。初始化阶段若尚未收到 stateChanged
    // 事件，只在此处有限地回退默认 State；按值返回可避免引用 WorkCell 返回的
    // 临时默认 State。交互后的实际状态始终优先使用插件保存的快照。
    return _currentState != nullptr ? *_currentState : _workcell->getDefaultState();
}
RequirementSet EngineeringRequirementsWidget::requirementSet() const { return _requirements; }
QString EngineeringRequirementsWidget::statusText() const { return _statusLabel == nullptr ? QString() : _statusLabel->text(); }

void EngineeringRequirementsWidget::setFreezeReadinessCheck(
    std::function<bool(QString*)> check)
{
    _freezeReadinessCheck = std::move(check);
}
void EngineeringRequirementsWidget::reportFreezePublicationResult(bool saved, const QString& error)
{
    if (saved) {
        setStatus(QString::fromUtf8(
            "Requirements checked and published. Downstream plugins can read the latest published version."));
        return;
    }

    const QString detail = error.trimmed().isEmpty()
        ? QStringLiteral("Unknown save error.")
        : error.trimmed();
    setStatus(QString::fromUtf8(
        "Requirements checked successfully, but publishing failed. The project was not updated. Reason: %1")
                  .arg(detail));
}
void EngineeringRequirementsWidget::pushUndoSnapshot(const RequirementSet& snapshot)
{
    // 每条快照都是一次已经成功的工程语义修改之前的完整状态，不区分批量或普通编辑。
    _undoStack.pushSnapshot(snapshot);
}

void EngineeringRequirementsWidget::recordRequirementEdit(const RequirementSet& snapshot, bool refreshAllWidgets)
{
    // 编辑态一旦发生任何变化，之前的编译结果和冻结审计证据均不能继续代表当前数据。
    // 统一在此处使其失效，避免不同按钮遗漏清理而让下游误用陈旧的任务工件。
    pushUndoSnapshot(snapshot);
    _compiled = CompiledRequirementSet();
    _frozenArtifact = FrozenRequirementArtifact();
    if (refreshAllWidgets) {
        refreshTables();
    } else {
        // 覆盖盒表格仍处于用户正在编辑的单元格时不整体重绘，防止光标和未提交文本被打断；
        // 但撤销/重做按钮必须立即反映新的历史状态。
        if (QPushButton* undo = findChild<QPushButton*>("undoRequirementOperationButton"))
            undo->setEnabled(!_requirements.frozen && _undoStack.canUndo());
        if (QPushButton* redo = findChild<QPushButton*>("redoRequirementOperationButton"))
            redo->setEnabled(!_requirements.frozen && _undoStack.canRedo());
    }
    Q_EMIT requirementsChanged();
}
void EngineeringRequirementsWidget::setStatus(const QString& text) { if (_statusLabel != nullptr) _statusLabel->setText(text); }

} // namespace rws
