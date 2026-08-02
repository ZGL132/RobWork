#include "EngineeringRequirementsWidget.hpp"

#include "RequirementCompiler.hpp"
#include "RequirementFreezer.hpp"
#include "GeometryFeatureResolver.hpp"
#include "OrientationRuleResolver.hpp"
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
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
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
    return ok ? std::max(2, value) : std::max(2, fallback);
}
QString text(const QTableWidget* table, int row, int column, const QString& fallback = QString()) {
    return table->item(row, column) == nullptr ? fallback : table->item(row, column)->text();
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

    // 记录专属参数所在的 FormLayout 行。后续只切换行的可见性，不销毁控件或覆盖
    // 其现有值，因此工程师在模板之间比较方案时，切回原模板仍可保留已输入的数据。
    const int rowsRow = form->rowCount();
    form->addRow(QString::fromUtf8("行数"), rows);
    const int columnsRow = form->rowCount();
    form->addRow(QString::fromUtf8("列数"), columns);
    const int layersRow = form->rowCount();
    form->addRow(QString::fromUtf8("层数"), layers);
    const int rowSpacingRow = form->rowCount();
    form->addRow(QString::fromUtf8("行间距"), rowSpacing);
    const int columnSpacingRow = form->rowCount();
    form->addRow(QString::fromUtf8("列间距"), columnSpacing);
    const int layerSpacingRow = form->rowCount();
    form->addRow(QString::fromUtf8("层间距"), layerSpacing);
    const int approachRow = form->rowCount();
    form->addRow(QString::fromUtf8("接近距离"), approach);
    const int retractRow = form->rowCount();
    form->addRow(QString::fromUtf8("撤离距离"), retract);
    const int clearanceRow = form->rowCount();
    form->addRow(QString::fromUtf8("安全距离"), clearance);

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
    QPushButton* undo = new QPushButton(QString::fromUtf8("撤销操作"), page);
    undo->setObjectName("undoRequirementOperationButton");
    QPushButton* redo = new QPushButton(QString::fromUtf8("重做操作"), page);
    redo->setObjectName("redoRequirementOperationButton");
    undo->setToolTip(QString::fromUtf8("恢复最近一次工位、覆盖盒、模板、阵列、镜像或导入操作前的完整需求快照"));
    actions->addWidget(add); actions->addWidget(duplicate); actions->addWidget(remove); actions->addWidget(capture); actions->addWidget(pickGeometry); actions->addWidget(undo); actions->addWidget(redo);
    actions->addWidget(createTemplate); actions->addWidget(updateTemplate); actions->addWidget(detachTemplate);
    actions->addWidget(createArray); actions->addWidget(mirror); actions->addWidget(import); actions->addStretch();
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
    _stationOrientationTargetPointEdit = new QLineEdit(inspector);
    _stationOrientationTargetPointEdit->setObjectName("keyStationOrientationTargetPointEdit");
    _stationOrientationTargetPointEdit->setPlaceholderText(QString::fromUtf8("x, y, z（相对参考系，m）"));
    _stationOrientationTargetPointEdit->setToolTip(QString::fromUtf8("指向目标模式下可输入目标点坐标，格式为 x, y, z，单位 m；未选择目标坐标系时使用该点。"));
    _stationFreeRollCheck = new QCheckBox(QString::fromUtf8("允许工具绕轴自由滚转"), inspector); _stationFreeRollCheck->setObjectName("keyStationFreeRollCheck");
    form->addRow(QString::fromUtf8("名称"), _stationNameEdit); form->addRow(QString::fromUtf8("工艺类型"), _stationProcessTypeCombo);
    form->addRow(QString::fromUtf8("要求等级"), _stationLevelCombo); form->addRow(QString::fromUtf8("参考系"), _stationReferenceFrameCombo);
    form->addRow(QString::fromUtf8("TCP"), _stationTcpFrameCombo); form->addRow(QString::fromUtf8("姿态规则"), _stationOrientationModeCombo);
    _stationOrientationTargetFrameLabel = new QLabel(QString::fromUtf8("姿态目标"), inspector);
    _stationOrientationTargetPointLabel = new QLabel(QString::fromUtf8("目标点"), inspector);
    form->addRow(_stationOrientationTargetFrameLabel, _stationOrientationTargetFrameCombo);
    form->addRow(_stationOrientationTargetPointLabel, _stationOrientationTargetPointEdit);
    form->addRow(QString(), _stationFreeRollCheck);
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
    _stationAdvancedPoseGroup = new QGroupBox(QString::fromUtf8("高级坐标（工位坐标）"), inspector); _stationAdvancedPoseGroup->setObjectName("keyStationAdvancedPoseGroup");
    QFormLayout* poseForm = new QFormLayout(_stationAdvancedPoseGroup);
    _stationAdvancedPoseSourceLabel = new QLabel(_stationAdvancedPoseGroup);
    _stationAdvancedPoseSourceLabel->setObjectName("keyStationAdvancedPoseSourceLabel");
    _stationAdvancedPoseSourceLabel->setWordWrap(true);
    _stationX = lengthSpinBox("keyStationX"); _stationY = lengthSpinBox("keyStationY"); _stationZ = lengthSpinBox("keyStationZ");
    _stationRoll = angleSpinBox("keyStationRoll"); _stationPitch = angleSpinBox("keyStationPitch"); _stationYaw = angleSpinBox("keyStationYaw");
    poseForm->addRow(QString::fromUtf8("坐标说明"), _stationAdvancedPoseSourceLabel);
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
    QPushButton* add = new QPushButton(QString::fromUtf8("新增覆盖盒"), page); add->setObjectName("addRequirementBoxRegionButton");
    QPushButton* duplicate = new QPushButton(QString::fromUtf8("复制覆盖盒"), page); duplicate->setObjectName("duplicateRequirementBoxRegionButton");
    QPushButton* remove = new QPushButton(QString::fromUtf8("删除覆盖盒"), page); remove->setObjectName("removeRequirementBoxRegionButton");
    actions->addWidget(add); actions->addWidget(duplicate); actions->addWidget(remove); actions->addStretch();
    layout->addLayout(actions);
    _regionTable = new QTableWidget(page); _regionTable->setObjectName("engineeringRequirementBoxTable");
    _regionTable->setColumnCount(12);
    _regionTable->setHorizontalHeaderLabels({"ID", "名称", "等级", "参考系", "中心 X", "中心 Y", "中心 Z", "尺寸 X", "尺寸 Y", "尺寸 Z", "最小覆盖率", "每轴采样点"});
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
    layout->addWidget(_modelLabel); layout->addWidget(_freezeLabel);
    QHBoxLayout* actions = new QHBoxLayout();
    QPushButton* bind = new QPushButton(QString::fromUtf8("绑定模型"), page); bind->setObjectName("bindRequirementModelButton");
    QPushButton* load = new QPushButton(QString::fromUtf8("导入需求副本"), page); load->setObjectName("loadRequirementSetButton");
    QPushButton* save = new QPushButton(QString::fromUtf8("导出需求副本"), page); save->setObjectName("saveRequirementSetButton");
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
    if (QPushButton* button = findChild<QPushButton*>("redoRequirementOperationButton"))
        button->setEnabled(editable && _undoStack.canRedo());
    if (_modelLabel != nullptr)
        _modelLabel->setText(QString::fromUtf8("模型：%1\n指纹：%2").arg(QString::fromStdString(_requirements.modelBinding.sourcePath), QString::fromStdString(_requirements.modelBinding.robotModelFingerprint)));
    if (_freezeLabel != nullptr) {
        if (_requirements.frozen) {
            // 冻结时间来自冻结工件，而不是当前界面刷新时间。工程师重新打开项目或导出报告时，
            // 因而能准确识别本次优化将消费的是哪一次经过真实 WorkCell 校验的需求快照。
            const QString frozenAt = _frozenArtifact.frozenAt.empty()
                ? QString::fromUtf8("历史项目未记录")
                : QString::fromStdString(_frozenArtifact.frozenAt);
            _freezeLabel->setText(QString::fromUtf8("状态：已冻结。冻结时间（UTC）：%1\n需求指纹：%2")
                                      .arg(frozenAt, QString::fromStdString(_compiled.requirementFingerprint)));
        } else {
            _freezeLabel->setText(QString::fromUtf8("状态：可编辑。冻结后才可作为下游分析和优化输入。"));
        }
    }
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
    _requirements.boxRegions.clear();
    for (int row = 0; _regionTable != nullptr && row < _regionTable->rowCount(); ++row) {
        BoxRegion region; region.id = text(_regionTable, row, 0).toStdString(); region.name = text(_regionTable, row, 1).toStdString();
        region.level = levelFromText(qobject_cast<QComboBox*>(_regionTable->cellWidget(row, 2))->currentText());
        region.refFrame = text(_regionTable, row, 3, "WORLD").toStdString();
        for (int axis = 0; axis < 3; ++axis) { region.center[axis] = number(_regionTable, row, 4 + axis); region.size[axis] = number(_regionTable, row, 7 + axis, 0.1); }
        region.minimumCoverage = number(_regionTable, row, 10, 0.8);
        region.samplesPerAxis = positiveSampleCount(_regionTable, row, 11, region.samplesPerAxis);
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
                QString::fromUtf8("相对工位参考系 %1；位置和固定姿态均可编辑。").arg(reference));
        } else if (_stationOrientationCoordinatesResolved) {
            _stationAdvancedPoseSourceLabel->setText(
                QString::fromUtf8("相对工位参考系 %1；姿态由规则解析，仅显示。").arg(reference));
        } else {
            _stationAdvancedPoseSourceLabel->setText(
                QString::fromUtf8("相对工位参考系 %1；当前无法解析姿态规则，显示已保存的工位姿态。").arg(reference));
        }
    }
    // 固定姿态只显示高级 RPY；坐标系/法向模式只需选择目标 Frame；指向模式额外开放
    // 参考系内目标点。两种目标同时填写时解析器优先使用 Frame，避免状态不确定。
    if (_stationOrientationTargetFrameCombo != nullptr) {
        _stationOrientationTargetFrameCombo->setVisible(!fixed);
        _stationOrientationTargetFrameLabel->setVisible(!fixed);
        _stationOrientationTargetFrameLabel->setText(pointAtTarget
            ? QString::fromUtf8("目标坐标系（可选）") : QString::fromUtf8("姿态目标"));
    }
    if (_stationOrientationTargetPointEdit != nullptr) {
        _stationOrientationTargetPointEdit->setVisible(pointAtTarget);
        _stationOrientationTargetPointLabel->setVisible(pointAtTarget);
    }
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

// 自动绑定工程生成模型：只解析项目清单 robot-model.main 指向的 .rmb.json，
// 并校验其 robotName 与当前 WorkCell 设备一致后写入 requirements 的 modelBinding。
bool EngineeringRequirementsWidget::bindGeneratedProjectModel(QString* error)
{
    if (_workcell == nullptr) {
        if (error != nullptr) *error = QString::fromUtf8("当前没有打开 WorkCell。");
        return false;
    }
    if (_projectOutputDirectory.isEmpty()) {
        if (error != nullptr) *error = QString::fromUtf8("当前没有打开项目。");
        return false;
    }

    if (_projectModelPath.isEmpty()) {
        if (error != nullptr)
            *error = QString::fromUtf8("项目清单中没有 robot-model.main 模型资源，请先在 RoboModelBuilder 中保存并加载模型。");
        return false;
    }

    const QString path = _projectModelPath;
    QFile modelFile(path);
    if (!modelFile.open(QFile::ReadOnly)) {
        if (error != nullptr) *error = QString::fromUtf8("无法读取项目机器人模型：%1").arg(path);
        return false;
    }
    RobotModelSpec model;
    std::string parseError;
    if (!RobotModelSpecJson::fromJson(modelFile.readAll().toStdString(), model, &parseError)) {
        if (error != nullptr)
            *error = QString::fromUtf8("项目机器人模型无效：%1").arg(QString::fromStdString(parseError));
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
            *error = QString::fromUtf8("项目模型的机器人名称与当前 WorkCell 设备不匹配。");
        return false;
    }

    // 绑定以源路径 + 名称 + 内容指纹记录；后续冻结会重新读取模型核验指纹。
    _requirements.modelBinding.sourcePath = path.toStdString();
    _requirements.modelBinding.robotName = model.robotName;
    _requirements.modelBinding.robotModelFingerprint = RobotModelFingerprint::canonicalSha256(model);
    if (error != nullptr) error->clear();
    refreshTables();
    Q_EMIT requirementsChanged();
    return true;
}

void EngineeringRequirementsWidget::saveRequirements()
{
    syncTablesToRequirements();
    const QString path = QFileDialog::getSaveFileName(
        this, QString::fromUtf8("保存研发需求"),
        requirementCopyExportPath(_projectOutputDirectory), "Requirement set (*.requirements.json)");
    if (path.isEmpty()) return;
    QString error;
    // 此按钮现在表示“导出一份项目外需求文件”。它复用相同的格式写入逻辑，但故意
    // 不调用 markProjectDocumentClean()，以免导出文件被误认为已保存回 rwproj 资源。
    if (!writeRequirementDocument(path, &error)) { setStatus(error); return; }
    setStatus(QString::fromUtf8("研发需求已保存。"));
}

void EngineeringRequirementsWidget::loadRequirements()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QString::fromUtf8("加载研发需求"),
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
}

// 首次编辑生成资源后建立会话基线：记录项目内路径并以当前配置为已保存快照，
// 使随后的编辑能正确参与主窗口脏状态；路径仅存于运行时，不进入业务 JSON。
void EngineeringRequirementsWidget::beginGeneratedProjectDocument(const QString& path)
{
    _projectDocumentPath = path;
    _pendingProjectDocumentSnapshot.clear();
    _savedProjectDocumentSnapshot = serializedProjectDocument(path);
}

// 项目关闭或切换时释放仅用于脏比较的路径与快照，防止旧项目基线污染新项目。
void EngineeringRequirementsWidget::clearProjectDocumentContext()
{
    _projectDocumentPath.clear();
    _savedProjectDocumentSnapshot.clear();
    _pendingProjectDocumentSnapshot.clear();
}

QByteArray EngineeringRequirementsWidget::serializedProjectDocument(const QString& documentPath) const
{
    // 工程目录可以整体搬迁，故模型引用必须相对于所属需求 JSON 保存。绝对路径只在
    // 内存中参与模型读取与冻结校验，绝不被写进项目资源。
    QJsonObject project = RequirementSetJson::toObject(_requirements);
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
        QJsonObject artifact = FrozenRequirementArtifactJson::toObject(_frozenArtifact);
        QJsonObject artifactBinding = artifact.value("modelBinding").toObject();
        relativizeBinding(artifactBinding);
        artifact["modelBinding"] = artifactBinding;
        project["frozenArtifact"] = artifact;
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
            *error = QString::fromUtf8("无法保存需求文件：%1").arg(file.errorString());
        return false;
    }
    if (file.write(serializedProjectDocument(targetPath)) < 0 || !file.commit()) {
        if (error != nullptr)
            *error = QString::fromUtf8("无法提交需求文件：%1").arg(file.errorString());
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
    QFile file(path); if (!file.open(QFile::ReadOnly)) { if (error != nullptr) *error = QString::fromUtf8("无法读取需求文件：%1").arg(file.errorString()); return false; }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error != nullptr) *error = QString::fromUtf8("需求文件不是有效 JSON：%1").arg(parseError.errorString());
        return false;
    }
    QJsonObject project = document.object();
    // 将项目内保存的相对模型路径恢复为绝对路径，后续冻结真实性校验与模型读取接口
    // 仍可沿用既有约定；冻结工件中的模型绑定也必须同步恢复。
    const QDir documentDirectory(QFileInfo(path).absolutePath());
    const auto resolveBinding = [&documentDirectory](QJsonObject& binding) {
        const QString sourcePath = binding.value("sourcePath").toString();
        if (!sourcePath.isEmpty() && QFileInfo(sourcePath).isRelative())
            binding["sourcePath"] = documentDirectory.absoluteFilePath(sourcePath);
    };
    QJsonObject modelBinding = project.value("modelBinding").toObject();
    resolveBinding(modelBinding);
    project["modelBinding"] = modelBinding;
    QJsonObject artifactObject = project.value("frozenArtifact").toObject();
    if (!artifactObject.isEmpty()) {
        QJsonObject artifactBinding = artifactObject.value("modelBinding").toObject();
        resolveBinding(artifactBinding);
        artifactObject["modelBinding"] = artifactBinding;
        project["frozenArtifact"] = artifactObject;
    }
    RequirementSet parsed;
    std::string parseMessage;
    if (!RequirementSetJson::fromObject(project, parsed, &parseMessage)) { if (error != nullptr) *error = QString::fromStdString(parseMessage); return false; }

    CompiledRequirementSet compiled;
    FrozenRequirementArtifact artifact;
    const QString validationRoot = projectRoot.trimmed().isEmpty()
        ? _projectOutputDirectory
        : QFileInfo(projectRoot).absoluteFilePath();
    QString loadStatus = QString::fromUtf8("研发需求已加载，处于可编辑状态。");
    const QJsonValue artifactValue = project.value("frozenArtifact");
    if (!artifactValue.isUndefined()) {
        if (!artifactValue.isObject() || !FrozenRequirementArtifactJson::fromObject(artifactValue.toObject(), artifact, &parseMessage)) {
            if (error != nullptr) *error = QString::fromUtf8("冻结审计工件无效：%1").arg(QString::fromStdString(parseMessage));
            return false;
        }

        // 重开项目时重新读取模型并比对当前 WorkCell/State。任何一项不一致都
        // 会将需求降级为编辑态，明确要求工程师在当前工程环境中再次冻结。
        RobotModelSpec model;
        QFile modelFile(QString::fromStdString(parsed.modelBinding.sourcePath));
        const bool modelReadable = !parsed.modelBinding.sourcePath.empty() && modelFile.open(QFile::ReadOnly) &&
                                   RobotModelSpecJson::fromJson(modelFile.readAll().toStdString(), model, &parseMessage);
        FrozenRequirementValidationResult validationResult;
        const bool artifactCurrent = modelReadable && _workcell != nullptr &&
            RequirementFreezer::isCurrent(
                artifact, parsed, *_workcell, activeWorkCellState(), model, &parseMessage,
                validationRoot.toStdString(), &validationResult);
        if (artifactCurrent) {
            parsed.frozen = true;
            compiled = artifact.compiled;
            loadStatus = QString::fromUtf8("研发需求及冻结审计工件已加载，且与当前模型和 WorkCell 一致。");
            for (const std::string& warning : validationResult.warnings)
                loadStatus += QString::fromUtf8("\n警告：%1").arg(QString::fromStdString(warning));
        } else {
            parsed.frozen = false;
            artifact = FrozenRequirementArtifact();
            const QString reason = modelReadable && _workcell == nullptr ?
                QString::fromUtf8("当前未打开 WorkCell") : QString::fromStdString(parseMessage);
            loadStatus = QString::fromUtf8("研发需求已加载，但冻结证据已过期或无法验证（%1）；请重新冻结。")
                .arg(reason);
        }
    } else if (parsed.frozen) {
        // 兼容旧项目：旧格式只有 frozen 标志而没有工件、环境和模型证据，必须
        // 视为未验证，不能让它绕过当前版本的冻结门禁。
        parsed.frozen = false;
        loadStatus = QString::fromUtf8("研发需求已加载；旧文件缺少冻结审计工件，请重新冻结。" );
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
    }
    Q_EMIT requirementsChanged();
    if (error != nullptr) error->clear();
    return true;
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

void EngineeringRequirementsWidget::redoLastOperation()
{
    if (_requirements.frozen || !_undoStack.redo(_requirements)) return;
    // 重做同样使编译缓存失效，后续冻结必须基于恢复后的完整编辑态重新校验。
    _compiled = CompiledRequirementSet();
    _frozenArtifact = FrozenRequirementArtifact();
    refreshTables();
    setStatus(QString::fromUtf8("已重做最近一次需求编辑操作。"));
    Q_EMIT requirementsChanged();
}

void EngineeringRequirementsWidget::freezeRequirements()
{
    syncTablesToRequirements();
    if (_workcell == nullptr) {
        setStatus(QString::fromUtf8("无法冻结需求：请先打开实际 WorkCell，以验证 Frame、TCP 与工装状态。"));
        return;
    }
    // 编辑态未绑定模型时，尝试绑定项目清单中的 robot-model.main 工程模型；
    // 无法自动绑定时给出明确提示，而不是以空的 modelBinding 继续冻结。
    if (_requirements.modelBinding.sourcePath.empty()) {
        QString bindingError;
        if (!bindGeneratedProjectModel(&bindingError)) {
            setStatus(QString::fromUtf8(
                "无法冻结需求：当前机器人尚无可自动绑定的项目模型；请在 RobotModelBuilder 生成并保存匹配的 .rmb.json。%1")
                .arg(bindingError.isEmpty() ? QString() : QString::fromUtf8("原因：") + bindingError));
            return;
        }
    }

    // 绑定路径只是编辑态引用，冻结时必须重新读取模型并以其内容指纹核验，防止
    // 文件已被替换而需求仍沿用过期模型指纹的情况进入结构优化链路。
    QFile modelFile(QString::fromStdString(_requirements.modelBinding.sourcePath));
    if (!modelFile.open(QFile::ReadOnly)) {
        setStatus(QString::fromUtf8("无法冻结需求：绑定的机器人模型文件无法读取。"));
        return;
    }
    RobotModelSpec model;
    std::string error;
    if (!RobotModelSpecJson::fromJson(modelFile.readAll().toStdString(), model, &error)) {
        setStatus(QString::fromStdString(error));
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
    _frozenArtifact = artifact;
    const int pathPending = static_cast<int>(std::count_if(_compiled.poseTasks.begin(), _compiled.poseTasks.end(), [] (const CompiledPoseTask& task) {
        return task.pathValidationPending;
    }));
    setStatus(QString::fromUtf8("需求已校验并冻结：%1 个工位可用于 P2 运动学优化；%2 项建议需求未验证，未进入优化；%3 个接近/撤离规则已记录，连续 IK 与路径碰撞将在 P3 验证。")
        .arg(_compiled.poseTasks.size()).arg(_compiled.diagnostics.size()).arg(pathPending));
    refreshTables();
    Q_EMIT requirementsChanged();
    Q_EMIT freezePublicationRequested();
}
void EngineeringRequirementsWidget::unfreezeRequirements()
{
    if (!_requirements.frozen) return;
    _requirements.frozen = false;
    _compiled = CompiledRequirementSet();
    _frozenArtifact = FrozenRequirementArtifact();
    setStatus(QString::fromUtf8("需求已解冻，可继续编辑。"));
    refreshTables();
    Q_EMIT requirementsChanged();
}
void EngineeringRequirementsWidget::addPoseTask()
{
    if (_requirements.frozen) return;
    syncTablesToRequirements();
    const RequirementSet before = _requirements;
    PoseTask task;
    task.id = "station_" + std::to_string(_requirements.poseTasks.size() + 1);
    task.name = QString::fromUtf8("关键工位 %1").arg(_requirements.poseTasks.size() + 1).toStdString();
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
            device->getBase(), device->getEnd(), activeWorkCellState());
        const rw::math::RPY<> rpy(baseTtcp.R());
        syncTablesToRequirements();
        const RequirementSet before = _requirements;
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
        recordRequirementEdit(before);
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
    setStatus(QString::fromUtf8("已关联几何 Frame“%1”：作业位会随当前 WorkCell 重新解析；面法向姿态已记录，连续路径验证将在 P3 执行。").arg(frameName));
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

void EngineeringRequirementsWidget::addBoxRegion()
{
    if (_requirements.frozen) return;
    syncTablesToRequirements();
    const RequirementSet before = _requirements;
    BoxRegion region;
    region.id = "box_" + std::to_string(_requirements.boxRegions.size() + 1);
    region.name = QString::fromUtf8("工作区域 %1").arg(_requirements.boxRegions.size() + 1).toStdString();
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
    _frozenArtifact = FrozenRequirementArtifact();
    setStatus(workcell == nullptr ? QString::fromUtf8("当前未打开 WorkCell；引用 Frame 会显示为未解析。")
                                : QString::fromUtf8("已连接当前 WorkCell，等待接收最新场景状态。"));
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
void EngineeringRequirementsWidget::reportFreezePublicationResult(bool saved, const QString& error)
{
    if (saved) {
        setStatus(QString::fromUtf8(
            "需求已校验、冻结并随完整项目事务保存；下游插件可直接读取最新冻结工件。"));
        return;
    }

    const QString detail = error.trimmed().isEmpty()
        ? QString::fromUtf8("未知保存错误。")
        : error.trimmed();
    setStatus(QString::fromUtf8(
        "需求已冻结在内存中，但项目保存失败，冻结工件尚未发布。请修复保存问题后执行“保存项目”。原因：%1")
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
