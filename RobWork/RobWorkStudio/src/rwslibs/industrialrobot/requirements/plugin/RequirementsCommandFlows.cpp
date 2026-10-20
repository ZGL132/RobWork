/**
 * @file   RequirementsCommandFlows.cpp
 * @brief  需求域九命令 UI 流程实现（UI-T32 C 批次＋返工测试缝）。
 *
 * 装配纪律：域逻辑全部经 TemplateArrayService/RequirementImporter 纯函数
 * （零重写）；批量应用经 submitBatchEdit（一次批次一次入栈——L-R9）。
 * 交互经 CommandDialogHost（attempt1 fail B-1 修复：编排与应答分离——
 * 生产＝qtDialogHost 真实模态；测试＝预置替身）。反馈＝面板状态行中文。
 */

#include "RequirementsCommandFlows.hpp"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QPointer>
#include <QSpinBox>
#include <QTableWidget>
#include <QHeaderView>
#include <QPlainTextEdit> // 全量导入明细可滚动/复制，原文不作为HTML解释。
#include <QVBoxLayout>

#include <sdurws/ird/io/Csv.hpp>         // makeCsvReader/RawTable（CSV 通道）
#include <sdurws/ird/requirements/Capture.hpp>       // RequirementCaptureService/请求模型（UI-T33——捕获/拾取真实现）
#include <sdurws/ird/requirements/Readiness.hpp>     // CheckContext（捕获服务第三参——空上下文预检口径）
#include <sdurws/ird/requirements/Import.hpp>        // RequirementImporter/exportCopy/mapJson
#include <sdurws/ird/requirements/TemplateArray.hpp>  // TemplateArrayService/defaultTemplateParams

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <sstream> // 折线文本只解析坐标；弧长与采样仍归TemplateArrayService。
#include <QScrollArea>

#include "ImportWizardFlow.hpp"        // L-R10 五步向导域侧流（组2）
#include "PanelEditFlow.hpp"             // submitBatchEdit/IRequirementEditSink
#include "RequirementsPanelWidget.hpp"   // 面板（状态行/选中锚）

namespace sdurws::ird::requirements {
namespace {

/// 域服务实例（无状态纯函数——函数局部构造即用，无共享态）。
TemplateArrayService templateService{};
RequirementImporter importer{};

void note(RequirementsPanelWidget& panel, const QString& text)
{
    panel.showCommandFeedback(text);
}

/**
 * @brief 旋转矩阵 → RPY（roll/pitch/yaw，单位 rad）反解——TCP 捕获摘要
 *        呈现用（UI-T53 附带修复：原实现调 rw::math::RPY 构造——F-480
 *        同族框架外联符号，独立冒烟模式链接失败〔LNK2019〕；本插件禁
 *        重蹈同款纪律，改逐元素解析——与 modeling 侧 rotationToRpy 反解
 *        同式同源，公式一致性由各自单元测试钉住）。
 *
 * 正解约定 R＝Rz(yaw)·Ry(pitch)·Rx(roll)；反解：pitch＝atan2(-R20,hypot(R00,R10))，
 * roll＝atan2(R21,R22)，yaw＝atan2(R10,R00)；万向锁（|R20|≈1）时
 * roll＝0、yaw 改由 atan2(-R01,R11) 确定（确定性特例——工程惯例）。
 *
 * @param R [in] 旋转矩阵（无量纲正交阵）
 * @return {roll, pitch, yaw}，单位 rad
 */
std::array<double, 3> rotationToRpyPresentation(const rw::math::Rotation3D<double>& R)
{
    // 近±90°时R20可能先舍入成±1，asin会丢失仍保存在首列的微小余弦；
    // WP-14-T12/REQ-10数值回归要求1e-8rad偏离不能被吸附到万向锁。
    const double cp = std::hypot(R(0, 0), R(1, 0));
    const double pitch = std::atan2(-R(2, 0), cp);
    if (std::abs(cp) > 1e-12) {
        return {std::atan2(R(2, 1), R(2, 2)), pitch, std::atan2(R(1, 0), R(0, 0))};
    }
    return {0.0, pitch, std::atan2(-R(0, 1), R(1, 1))};
}

// =====================================================================
// 生产对话框宿主（真实 Qt 模态——测试缝的生产侧实现）
// =====================================================================

class QtDialogHost final : public CommandDialogHost {
public:
    std::optional<QString> saveFilePath(const QString& title,
                                        const QString& defaultPath,
                                        const QString& filter) override
    {
        const QString s = QFileDialog::getSaveFileName(nullptr, title, defaultPath, filter);
        return s.isEmpty() ? std::optional<QString>{} : std::optional<QString>{s};
    }

    std::optional<QString> openFilePath(const QString& title,
                                        const QString& filter) override
    {
        const QString s = QFileDialog::getOpenFileName(nullptr, title, QString(), filter);
        return s.isEmpty() ? std::optional<QString>{} : std::optional<QString>{s};
    }

    std::optional<int> chooseItem(const QString& title, const QString& label,
                                  const QStringList& items) override
    {
        bool ok = false;
        const QString choice = QInputDialog::getItem(nullptr, title, label, items, 0,
                                                     false, &ok);
        if (!ok) {
            return std::nullopt;
        }
        return static_cast<int>(items.indexOf(choice));
    }

    bool editTemplateParams(TemplateKind& kind, TemplateParams& params) override
    {
        // 六类参数表单＋生成计数预览（域函数试算——UI 零判定）。
        QDialog dialog;
        dialog.setWindowTitle(QString::fromUtf8("应用工艺模板（生成工位）"));
        auto* form = new QFormLayout(&dialog);
        auto* kindBox = new QComboBox(&dialog);
        const std::pair<TemplateKind, const char*> kinds[] = {
            {TemplateKind::BinPicking, "拆垛/拣选"}, {TemplateKind::MachineTending, "机床上下料"},
            {TemplateKind::Palletizing, "码垛"}, {TemplateKind::Inspection, "检测"},
            {TemplateKind::ToolChange, "换工具"}, {TemplateKind::Handover, "交接"}};
        for (const auto& k : kinds) {
            kindBox->addItem(QString::fromUtf8(k.second), int(k.first));
        }
        // REQ-07：类别是独立枚举，参数形状不编码类别，必须显式预填与回传。
        kindBox->setCurrentIndex(kindBox->findData(int(kind)));
        auto* baseName = new QLineEdit(QString::fromStdString(params.baseName), &dialog);
        auto* spacing = new QDoubleSpinBox(&dialog);
        spacing->setSuffix(QString::fromUtf8(" m"));
        spacing->setRange(0.01, 100.0);
        auto* countX = new QSpinBox(&dialog);
        countX->setRange(1, 100);
        auto* countY = new QSpinBox(&dialog);
        countY->setRange(1, 100);
        auto applyDefaults = [&](const TemplateParams& def) {
            spacing->setValue(def.spacingM);
            countX->setValue(def.countX);
            countY->setValue(def.countY);
        };
        applyDefaults(params);
        form->addRow(QString::fromUtf8("模板类别"), kindBox);
        form->addRow(QString::fromUtf8("名称前缀"), baseName);
        form->addRow(QString::fromUtf8("X 向间距"), spacing);
        form->addRow(QString::fromUtf8("X 向数量"), countX);
        form->addRow(QString::fromUtf8("Y 向数量"), countY);
        auto* preview = new QLabel(&dialog);
        auto refreshPreview = [&]() {
            TemplateParams trial = defaultTemplateParams(
                TemplateKind(kindBox->currentData().toInt()));
            trial.baseName = baseName->text().toStdString();
            trial.spacingM = spacing->value();
            trial.countX = countX->value();
            trial.countY = countY->value();
            const EditBatch t = templateService.applyTemplate(
                TemplateKind(kindBox->currentData().toInt()), trial);
            preview->setText(t.ok
                                 ? QString::fromUtf8("将生成 %1 个工位").arg(int(t.newPoints.size()))
                                 : QString::fromUtf8("参数待修正：%1").arg(
                                       QString::fromStdString(t.error.detail)));
        };
        QObject::connect(kindBox, &QComboBox::currentIndexChanged, &dialog, [&]() { applyDefaults(defaultTemplateParams(TemplateKind(kindBox->currentData().toInt()))); });
        QObject::connect(kindBox, &QComboBox::currentIndexChanged, &dialog, refreshPreview);
        // REQ-07：所有影响生成数量/参数合法性的输入共享同一域试算，避免预览与确认分叉。
        QObject::connect(countX, &QSpinBox::valueChanged, &dialog, refreshPreview);
        QObject::connect(countY, &QSpinBox::valueChanged, &dialog, refreshPreview);
        QObject::connect(spacing, &QDoubleSpinBox::valueChanged, &dialog, refreshPreview);
        QObject::connect(baseName, &QLineEdit::textChanged, &dialog, refreshPreview);
        refreshPreview();
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                             Qt::Horizontal, &dialog);
        form->addRow(preview);
        form->addRow(buttons);
        QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() != QDialog::Accepted) {
            return false;
        }
        kind = TemplateKind(kindBox->currentData().toInt());
        params = defaultTemplateParams(kind);
        params.baseName = baseName->text().toStdString();
        params.spacingM = spacing->value();
        params.countX = countX->value();
        params.countY = countY->value();
        return true;
    }

    /** @brief REQ-11完整参数表单；每次输入仅试算，确认才覆盖调用方参数。 */
    bool editArrayDefinition(ArrayKind kind, ArrayParams& params) override
    {
        QDialog dialog; dialog.setWindowTitle("批量阵列参数（源参考系）"); dialog.resize(680, 720);
        auto* layout = new QVBoxLayout(&dialog);
        auto* scroll = new QScrollArea(&dialog); scroll->setWidgetResizable(true);
        auto* body = new QWidget(scroll); auto* form = new QFormLayout(body);
        scroll->setWidget(body); layout->addWidget(scroll);
        // 控件由dialog的Qt父子链拥有；以下闭包仅在exec期间使用，绝不延迟写草稿。
        const auto scalar = [&](const QString& key, const QString& label, double value, const QString& unit) {
            auto* field = new QDoubleSpinBox(body); field->setObjectName("array-" + key);
            field->setDecimals(15); field->setRange(-1000000.0, 1000000.0);
            field->setSuffix(unit); field->setValue(value); form->addRow(label, field); return field;
        };
        const auto count = [&](const QString& key, const QString& label, int value) {
            auto* field = new QSpinBox(body); field->setObjectName("array-" + key);
            // 单轴1000是交互保护，乘积仍由服务的资源门统一裁决。
            field->setRange(1, 1000); field->setValue(value); form->addRow(label, field); return field;
        };
        const auto vector = [&](const QString& key, const QString& label, const rw::math::Vector3D<>& value, const QString& unit) {
            return std::array<QDoubleSpinBox*, 3>{scalar(key + "-x", label + " X", value[0], unit),
                scalar(key + "-y", label + " Y", value[1], unit), scalar(key + "-z", label + " Z", value[2], unit)};
        };
        const auto readVector = [](const std::array<QDoubleSpinBox*, 3>& fields) {
            return rw::math::Vector3D<>(fields[0]->value(), fields[1]->value(), fields[2]->value());
        };
        auto* count1 = count("count", "列数/生成数量", params.count);
        auto* spacing = scalar("spacing", kind == ArrayKind::Polyline ? "弧长间距" : "列间距", kind == ArrayKind::Polyline ? params.polylineSpacingM : params.spacingM, " m");
        std::array<QDoubleSpinBox*, 3> direction{}, direction2{}, direction3{}, center{}, normal{};
        QSpinBox* count2 = nullptr; QSpinBox* count3 = nullptr;
        QDoubleSpinBox* spacing2 = nullptr; QDoubleSpinBox* spacing3 = nullptr;
        QDoubleSpinBox* radius = nullptr; QDoubleSpinBox* start = nullptr; QDoubleSpinBox* step = nullptr;
        QPlainTextEdit* polyline = nullptr;
        if (kind == ArrayKind::Linear || kind == ArrayKind::Rectangular) { direction = vector("direction", "列方向", params.direction, ""); }
        if (kind == ArrayKind::Rectangular) {
            count2 = count("count2", "行数", params.count2); spacing2 = scalar("spacing2", "行间距", params.spacing2M, " m");
            direction2 = vector("direction2", "行方向", params.direction2, "");
            count3 = count("count3", "层数", params.count3); spacing3 = scalar("spacing3", "层间距", params.spacing3M, " m");
            direction3 = vector("direction3", "层方向", params.direction3, "");
        } else if (kind == ArrayKind::Circular) {
            spacing->hide(); form->labelForField(spacing)->hide();
            center = vector("center", "圆心", params.center, " m"); normal = vector("normal", "圆平面法向", params.circleNormal, "");
            radius = scalar("radius", "半径", params.radiusM, " m");
            start = scalar("start-angle", "起始角", params.startAngleRad, " rad"); step = scalar("angle-step", "角步长", params.angleStepRad, " rad");
        } else if (kind == ArrayKind::Polyline) {
            count1->hide(); form->labelForField(count1)->hide();
            polyline = new QPlainTextEdit(body); polyline->setObjectName("array-polyline");
            QStringList lines;
            for (const auto& p : params.polyline) { lines << QString("%1 %2 %3").arg(p[0], 0, 'g', 17).arg(p[1], 0, 'g', 17).arg(p[2], 0, 'g', 17); }
            polyline->setPlainText(lines.join('\n')); polyline->setMinimumHeight(120);
            form->addRow("折线顶点（每行X Y Z，m）", polyline);
        }
        auto* preview = new QLabel(&dialog); preview->setObjectName("array-preview"); preview->setWordWrap(true); layout->addWidget(preview);
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog); layout->addWidget(buttons);
        buttons->button(QDialogButtonBox::Ok)->setText("确认生成草稿"); buttons->button(QDialogButtonBox::Cancel)->setText("取消");
        ArrayParams staged = params;
        const auto refresh = [&] {
            staged = params; staged.count = count1->value(); staged.spacingM = spacing->value();
            if (direction[0]) { staged.direction = readVector(direction); }
            if (count2) {
                staged.count2 = count2->value(); staged.spacing2M = spacing2->value(); staged.direction2 = readVector(direction2);
                staged.count3 = count3->value(); staged.spacing3M = spacing3->value(); staged.direction3 = readVector(direction3);
            }
            if (radius) { staged.radiusM = radius->value(); staged.center = readVector(center); staged.circleNormal = readVector(normal); staged.startAngleRad = start->value(); staged.angleStepRad = step->value(); }
            if (polyline) {
                staged.polyline.clear(); staged.polylineSpacingM = spacing->value();
                // 文本格式只认三列有限数值；几何退化与弧长合法性由原服务判定。
                for (const auto& line : polyline->toPlainText().split('\n')) {
                    if (line.trimmed().isEmpty()) { continue; }
                    std::istringstream input(line.toStdString()); input.imbue(std::locale::classic());
                    double x, y, z; std::string extra;
                    if (!(input >> x >> y >> z) || (input >> extra)) {
                        preview->setText("顶点格式错误：每行只允许X Y Z三个数，单位m"); buttons->button(QDialogButtonBox::Ok)->setEnabled(false); return;
                    }
                    staged.polyline.emplace_back(x, y, z);
                }
            }
            const auto trial = templateService.applyArray(kind, staged);
            preview->setText(trial.ok ? QString("将生成 %1 个工位；确认后一次整体撤销").arg(trial.newPoints.size()) : QString::fromStdString(trial.error.detail));
            buttons->button(QDialogButtonBox::Ok)->setEnabled(trial.ok);
        };
        for (auto* field : body->findChildren<QDoubleSpinBox*>()) { QObject::connect(field, &QDoubleSpinBox::valueChanged, &dialog, refresh); }
        for (auto* field : body->findChildren<QSpinBox*>()) { QObject::connect(field, &QSpinBox::valueChanged, &dialog, refresh); }
        if (polyline) { QObject::connect(polyline, &QPlainTextEdit::textChanged, &dialog, refresh); }
        refresh();
        QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept); QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() != QDialog::Accepted) { return false; }
        params = std::move(staged); return true;
    }

    /** @brief REQ-11镜面表单：同源服务试算，同参考系反射，不复制跨系变换。 */
    bool editMirrorPlane(const std::vector<TaskPoint>& sources, MirrorPlaneSpec& plane) override
    {
        QDialog dialog; dialog.setWindowTitle("镜像工位（过参考系原点）"); dialog.resize(620, 450);
        auto* form = new QFormLayout(&dialog);
        auto* kind = new QComboBox(&dialog); kind->setObjectName("mirror-frame-kind"); kind->addItems({"世界坐标系", "模型Frame", "场景对象"});
        kind->setCurrentIndex(plane.refFrame.kind == RequirementRefKind::World ? 0 : plane.refFrame.kind == RequirementRefKind::ModelFrame ? 1 : 2);
        auto* id = new QLineEdit(plane.refFrame.objectId ? QString::fromStdString(plane.refFrame.objectId->toCanonical()) : QString(), &dialog); id->setObjectName("mirror-frame-id");
        form->addRow("参考系", kind); form->addRow("参考对象标识", id);
        std::array<QDoubleSpinBox*, 3> normal{};
        for (int i = 0; i < 3; ++i) {
            auto* field = new QDoubleSpinBox(&dialog); field->setObjectName(QString("mirror-normal-%1").arg(i)); field->setDecimals(12); field->setRange(-1000000, 1000000); field->setValue(plane.axisNormal[i]); normal[i] = field;
            form->addRow(QString("镜面法向 %1").arg(QChar::fromLatin1("XYZ"[i])), field);
        }
        auto* preview = new QLabel(&dialog); preview->setWordWrap(true); form->addRow(preview);
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog); form->addRow(buttons);
        MirrorPlaneSpec staged = plane;
        const auto refresh = [&] {
            id->setEnabled(kind->currentIndex() != 0);
            staged.refFrame = {};
            if (kind->currentIndex() != 0) {
                staged.refFrame.kind = kind->currentIndex() == 1 ? RequirementRefKind::ModelFrame : RequirementRefKind::SceneObject;
                // 用户文本不是程序契约违约，必须用core非抛出解析出口，非法值留在表单。
                staged.refFrame.objectId = core::ObjectId::tryFromCanonical(id->text().trimmed().toStdString());
                if (!staged.refFrame.objectId || !staged.refFrame.objectId->isValid()) { preview->setText("请输入有效参考对象标识"); buttons->button(QDialogButtonBox::Ok)->setEnabled(false); return; }
            }
            staged.axisNormal = {normal[0]->value(), normal[1]->value(), normal[2]->value()};
            const auto trial = templateService.applyMirror(sources, staged);
            preview->setText(trial.ok ? QString("将镜像 %1 个工位").arg(trial.newPoints.size()) : QString::fromStdString(trial.error.detail)); buttons->button(QDialogButtonBox::Ok)->setEnabled(trial.ok);
        };
        QObject::connect(kind, &QComboBox::currentIndexChanged, &dialog, refresh); QObject::connect(id, &QLineEdit::textChanged, &dialog, refresh);
        for (auto* field : normal) { QObject::connect(field, &QDoubleSpinBox::valueChanged, &dialog, refresh); }
        refresh(); QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept); QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() != QDialog::Accepted) { return false; }
        plane = staged; return true;
    }

    bool editImportMapping(const io::RawTable& table, FieldMapping& mapping,
                             ImportUnitOptions& units) override
    {
        // REQ-05：控件只收集声明，换算/字段语义全部调用既有领域函数。
        QDialog dialog; dialog.setWindowTitle("CSV字段映射与单位预览"); dialog.resize(850, 650);
        auto* layout = new QVBoxLayout(&dialog);
        auto* grid = new QTableWidget(static_cast<int>(kImportFieldCount), 4, &dialog);
        grid->setObjectName("import-mapping-table");
        grid->setHorizontalHeaderLabels({"字段（*必填）", "CSV来源列", "输入单位", "原文 → SI预览"});
        grid->setEditTriggers(QAbstractItemView::NoEditTriggers);
        grid->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
        grid->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
        layout->addWidget(grid);
        auto* message = new QLabel(&dialog); message->setWordWrap(true); layout->addWidget(message);
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        buttons->button(QDialogButtonBox::Ok)->setText("下一步：逐行预览");
        buttons->button(QDialogButtonBox::Cancel)->setText("取消"); layout->addWidget(buttons);
        std::array<QComboBox*, kImportFieldCount> columns{};
        std::array<QComboBox*, kImportFieldCount> unitFields{};
        FieldMapping stagedMapping = mapping; ImportUnitOptions stagedUnits = units;
        const auto& dictionary = importer.fieldDictionary();
        for (const auto& spec : dictionary.fields()) {
            const auto row = static_cast<std::size_t>(spec.field);
            const auto key = QString::fromStdString(std::string(importFieldToken(spec.field)));
            grid->setItem(static_cast<int>(row), 0, new QTableWidgetItem(key + (spec.required ? " *" : "")));
            auto* column = new QComboBox(grid); columns[row] = column;
            column->setObjectName("import-map-" + key); column->addItem("未映射/忽略", -1);
            for (std::size_t col = 0; col < table.report.header.size(); ++col) {
                column->addItem(QString("%1：%2").arg(col + 1).arg(QString::fromStdString(table.report.header[col])), static_cast<int>(col));
            }
            if (mapping.isMapped(spec.field)) { column->setCurrentIndex(column->findData(static_cast<int>(mapping.columnFor(spec.field)))); }
            auto* unit = new QComboBox(grid); unitFields[row] = unit;
            unit->setObjectName("import-unit-" + key); unit->setEditable(true); unit->addItem("");
            if (spec.kind == ImportColumnKind::Length) { unit->addItems({"m", "mm", "cm"}); unit->lineEdit()->setPlaceholderText("m（默认）"); }
            else if (spec.kind == ImportColumnKind::Angle) { unit->addItems({"rad", "deg"}); unit->lineEdit()->setPlaceholderText("rad（默认）"); }
            else { unit->setEnabled(false); }
            unit->setCurrentText(QString::fromStdString(units.unitFor(spec.field)));
            grid->setCellWidget(static_cast<int>(row), 1, column); grid->setCellWidget(static_cast<int>(row), 2, unit);
            grid->setItem(static_cast<int>(row), 3, new QTableWidgetItem);
        }
        const auto refreshPreview = [&] {
            bool complete = true;
            QStringList issues;
            for (const auto& spec : dictionary.fields()) {
                const auto row = static_cast<std::size_t>(spec.field);
                const int column = columns[row]->currentData().toInt();
                if (column < 0) { stagedMapping.clear(spec.field); }
                else { stagedMapping.assign(spec.field, static_cast<std::size_t>(column)); }
                stagedUnits.setUnit(spec.field, unitFields[row]->currentText().trimmed().toStdString());
                grid->item(static_cast<int>(row), 3)->setText("");
                if (spec.required && column < 0) { complete = false; issues << QString::fromStdString(std::string(importFieldToken(spec.field))) + "缺少来源列"; }
            }
            // 域侧唯一换算入口与随后mapCsv同源；坏样本不转0，留到逐行错误。
            for (const auto& sample : previewUnitConversion(table, stagedMapping, stagedUnits, dictionary)) {
                const auto row = static_cast<int>(sample.field);
                const QString siUnit = dictionary.spec(sample.field).kind == ImportColumnKind::Length ? "m" : "rad";
                const QString result = !sample.unitUsable ? "单位无法换算"
                    : sample.siSample ? QString::number(*sample.siSample, 'g', 17) + " " + siUnit : "样本不是有效数值（见下一步行错误）";
                grid->item(row, 3)->setText(QString::fromStdString(sample.rawSample) + " "
                    + QString::fromStdString(sample.declaredUnit) + " → " + result);
                if (!sample.unitUsable) { complete = false; issues << QString::fromStdString(std::string(importFieldToken(sample.field))) + "单位非法"; }
            }
            message->setText(issues.isEmpty() ? "确认映射后将逐行预览；此步骤不写入草稿。未映射可选字段使用字典默认值。" : issues.join("；"));
            buttons->button(QDialogButtonBox::Ok)->setEnabled(complete);
        };
        for (std::size_t row = 0; row < kImportFieldCount; ++row) {
            QObject::connect(columns[row], &QComboBox::currentIndexChanged, &dialog, refreshPreview);
            QObject::connect(unitFields[row], &QComboBox::currentTextChanged, &dialog, refreshPreview);
        }
        refreshPreview();
        QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() != QDialog::Accepted) { return false; }
        mapping = stagedMapping; units = stagedUnits; return true;
    }

    bool confirmImport(const QString& summaryText) override
    {
        // A-09：所有错误都可滚动查看和复制；QMessageBox裁切与前8条限制均移除。
        const bool capture = summaryText.startsWith(QStringLiteral("捕获 TCP"));
        const bool pick = summaryText.startsWith(QStringLiteral("拾取几何特征"));
        QDialog dialog;
        dialog.setWindowTitle(capture ? QStringLiteral("捕获TCP写回确认")
            : pick ? QStringLiteral("拾取特征写回确认") : QStringLiteral("导入逐行预览与确认"));
        dialog.resize(850, 650);
        auto* layout = new QVBoxLayout(&dialog);
        auto* details = new QPlainTextEdit(&dialog); details->setObjectName("import-outcome-details");
        details->setReadOnly(true); details->setPlainText(summaryText); layout->addWidget(details);
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        buttons->button(QDialogButtonBox::Ok)->setText(capture || pick
            ? QStringLiteral("确认写入草稿") : QStringLiteral("确认正确行写入草稿"));
        buttons->button(QDialogButtonBox::Cancel)->setText("取消");
        layout->addWidget(buttons);
        QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        return dialog.exec() == QDialog::Accepted;
    }

    RegenerateAction chooseRegenerateAction() override
    {
        QMessageBox box(QMessageBox::Question, QString::fromUtf8("重生成/解除关联"),
                        QString::fromUtf8("对生成批次执行："), QMessageBox::NoButton);
        box.addButton(QString::fromUtf8("按参数重生成"), QMessageBox::AcceptRole);
        box.addButton(QString::fromUtf8("解除关联（保留条目）"), QMessageBox::ActionRole);
        box.addButton(QMessageBox::Cancel);
        box.exec();
        const QAbstractButton* clicked = box.clickedButton();
        if (clicked == nullptr) {
            return RegenerateAction::Cancel;
        }
        const int role = box.buttonRole(const_cast<QAbstractButton*>(clicked));
        return role == QMessageBox::AcceptRole ? RegenerateAction::Regenerate
               : role == QMessageBox::ActionRole ? RegenerateAction::Detach
                                                 : RegenerateAction::Cancel;
    }
};

}  // namespace

CommandDialogHost& qtDialogHost()
{
    static QtDialogHost host;
    return host;
}

// =====================================================================
// 九命令流程（host 注入形态——编排与应答分离）
// =====================================================================

namespace {

bool flowExportCopy(RequirementsPanelWidget& panel, IRequirementEditor& editor,
                    CommandDialogHost& host)
{
    const auto selected = host.saveFilePath(
        QString::fromUtf8("导出需求副本"),
        QString::fromUtf8("requirements-copy.requirements.json"),
        QString::fromUtf8("JSON 副本 (*.requirements.json);;CSV 坐标表 (*.csv)"));
    if (!selected.has_value()) {
        return true;  // 用户取消
    }
    ExportTarget target;
    target.filePath = std::filesystem::path(selected->toStdWString());
    target.draft = true;  // 草稿导出（O-46 口径）
    const ExportFormat format = selected->endsWith(QStringLiteral(".csv"), Qt::CaseInsensitive)
                                    ? ExportFormat::Csv
                                    : ExportFormat::Json;
    const ExportOutcome out = importer.exportCopy(editor.workingSet(), format, target);
    if (!out.ok) {
        note(panel, QString::fromUtf8("导出失败：") + QString::fromStdString(out.error));
        return false;
    }
    note(panel, QString::fromUtf8("已导出需求副本（草稿标记）：") + *selected);
    return true;
}

bool flowApplyTemplate(RequirementsPanelWidget& panel, IRequirementEditor& editor,
                       IRequirementEditSink& sink, CommandDialogHost& host)
{
    // REQ-07：六类模板共享参数结构，类别必须从生产表单显式回传。
    TemplateKind kind = TemplateKind::Palletizing;
    TemplateParams params = defaultTemplateParams(kind);
    if (!host.editTemplateParams(kind, params)) {
        return true;  // 用户取消：没有草稿提交，也不改变生成来源。
    }
    const EditBatch batch = templateService.applyTemplate(kind, params);
    if (!batch.ok) {
        note(panel, QString::fromUtf8("未应用（模板参数）：")
                        + QString::fromStdString(batch.error.detail));
        return false;
    }
    return submitBatchEdit(editor, sink, batch) == EditSubmitOutcome::Applied;
}

bool flowMirrorStations(RequirementsPanelWidget& panel, IRequirementEditor& editor,
                        IRequirementEditSink& sink, CommandDialogHost& host)
{
    // 输入类型＝工位（UI-T39——审核五.1：镜像按工位页锚取源，不依赖"全局
    // 最后选中"——后者随树/表/页签任何一次点击漂移，选中区域/工况后触发
    // 镜像会静默找不到源条目）。
    const std::optional<core::ObjectId> selected =
        panel.selectionAnchor(WorkingSetMember::Points);
    if (!selected.has_value()) {
        note(panel, QString::fromUtf8("未应用：镜像需要先选中一个工位条目"));
        return false;
    }
    std::vector<TaskPoint> sources;
    for (const TaskPoint& p : editor.workingSet().points.entries) {
        if (p.objectId == selected.value()) {
            sources.push_back(p);
        }
    }
    // REQ-11：以源参考系预填，允许任意法向；异系由领域拒绝，UI不复制变换。
    if (sources.empty()) { note(panel, "未应用：源工位已不存在，请重新选择"); return false; }
    MirrorPlaneSpec plane; plane.refFrame = sources.front().refFrame; plane.axisNormal = {1, 0, 0};
    if (!host.editMirrorPlane(sources, plane)) { return true; }
    const EditBatch batch = templateService.applyMirror(sources, plane);
    if (!batch.ok) {
        note(panel, QString::fromUtf8("未应用（镜像）：")
                        + QString::fromStdString(batch.error.detail));
        return false;
    }
    return submitBatchEdit(editor, sink, batch) == EditSubmitOutcome::Applied;
}

bool flowCreateArray(RequirementsPanelWidget& panel, IRequirementEditor& editor,
                     IRequirementEditSink& sink, CommandDialogHost& host)
{
    // 输入类型＝工位（UI-T39——审核五.1，镜像同款按工位页锚取源）。
    const std::optional<core::ObjectId> selected =
        panel.selectionAnchor(WorkingSetMember::Points);
    if (!selected.has_value()) {
        note(panel, QString::fromUtf8("未应用：阵列需要先选中一个源工位条目"));
        return false;
    }
    const QStringList kinds{QString::fromUtf8("线性"), QString::fromUtf8("矩形"),
                            QString::fromUtf8("圆形"), QString::fromUtf8("折线")};
    const auto kindChoice = host.chooseItem(QString::fromUtf8("批量阵列"),
                                            QString::fromUtf8("阵列构型："), kinds);
    if (!kindChoice.has_value()) {
        return true;
    }
    ArrayParams params;
    for (const TaskPoint& p : editor.workingSet().points.entries) {
        if (p.objectId == selected.value()) {
            params.sources.push_back(p);
        }
    }
    const ArrayKind kind = ArrayKind(*kindChoice);
    // 参数均显式承载，不再由总数推断行数或固定折线/整圆；以下仅为初始建议。
    params.count = 3; params.spacingM = 0.5; params.count2 = 2; params.spacing2M = 0.5;
    params.spacing3M = 0.5; params.radiusM = 0.5; params.angleStepRad = 2.0943951023931953;
    params.polyline = {{0, 0, 0}, {1, 0, 0}, {1, 1, 1}}; params.polylineSpacingM = 0.5;
    if (!host.editArrayDefinition(kind, params)) { return true; }
    const EditBatch batch = templateService.applyArray(kind, params);
    if (!batch.ok) {
        note(panel, QString::fromUtf8("未应用（阵列参数）：")
                        + QString::fromStdString(batch.error.detail));
        return false;
    }
    return submitBatchEdit(editor, sink, batch) == EditSubmitOutcome::Applied;
}

bool flowRegenerateLinked(RequirementsPanelWidget& panel, IRequirementEditor& editor,
                          IRequirementEditSink& sink, CommandDialogHost& host)
{
    // 输入类型＝工位（UI-T39——审核五.1，镜像同款按工位页锚取源）。
    const std::optional<core::ObjectId> selected =
        panel.selectionAnchor(WorkingSetMember::Points);
    if (!selected.has_value()) {
        note(panel, QString::fromUtf8("未应用：重生成需要先选中一个生成工位"));
        return false;
    }
    std::string instanceId;
    for (const TaskPoint& p : editor.workingSet().points.entries) {
        if (p.objectId == selected.value() && p.generation.has_value()
            && p.generation->linked) {
            instanceId = p.generation->instanceId;
            break;
        }
    }
    if (instanceId.empty()) {
        note(panel, QString::fromUtf8("未应用：选中工位不是生成条目（无批次溯源）"));
        return false;
    }
    switch (host.chooseRegenerateAction()) {
    case CommandDialogHost::RegenerateAction::Cancel:
        return true;
    case CommandDialogHost::RegenerateAction::Regenerate: {
        const RegenerateOutcome out =
            templateService.regenerate(editor.workingSet(), instanceId);
        if (!out.found) {
            note(panel, QString::fromUtf8("未应用（重生成定位）：")
                            + QString::fromStdString(out.error.detail));
            return false;
        }
        return submitBatchEdit(editor, sink, out.batch) == EditSubmitOutcome::Applied;
    }
    default: break;
    }
    // REQ-11：unlinkGenerator返回全工作集值；只提取本批次的已关联项，保留ID，
    // 不把未命中的条目也写一遍。编辑器原子更新载荷负责统一校验与一次撤销。
    const std::vector<TaskPoint> unlinked =
        unlinkGenerator(editor.workingSet().points.entries, instanceId);
    EditBatch batch; batch.ok = true;
    for (const TaskPoint& p : unlinked) {
        for (const auto& original : editor.workingSet().points.entries) {
            if (original.objectId == p.objectId && original.generation && original.generation->linked
                && original.generation->instanceId == instanceId) { batch.updatedPoints.push_back(p); break; }
        }
    }
    batch.summary = "解除关联：" + std::to_string(batch.updatedPoints.size()) + " 个条目转为手工条目";
    return submitBatchEdit(editor, sink, batch) == EditSubmitOutcome::Applied;
}

/// 导入产出预览确认＋应用（CSV/JSON 共用——行级错误 AT-02 部分成功）。
bool confirmImportOutcome(RequirementsPanelWidget& panel, IRequirementEditor& editor,
                          IRequirementEditSink& sink, CommandDialogHost& host,
                          const ImportOutcome& outcome,
                          const std::vector<core::DiagnosticRecord>& diags)
{
    if (outcome.status == ImportOutcome::Status::Rejected) {
        QString detail;
        for (const core::DiagnosticRecord& d : diags) {
            detail += QString::fromStdString(d.context + "\n" + d.cause + "\n建议：" + d.recommendedAction) + QStringLiteral("\n");
        }
        note(panel, QString::fromUtf8("导入被拒绝（结构级）：") + detail.trimmed());
        return false;
    }
    QString summary = QString::fromUtf8("可导入行：%1；错误行：%2")
                          .arg(int(outcome.entries.size()))
                          .arg(int(outcome.rowErrors.size()));
    QString rows;
    // 不截断行级错误：context含原始行号/列名/原文，cause和action构成完整修正信息。
    for (const auto& error : outcome.rowErrors) {
        rows += QString::fromStdString(error.context + "\n原因：" + error.cause
            + "\n建议：" + error.recommendedAction + "\n\n");
    }
    for (const auto& diagnostic : diags) {
        rows += QString::fromStdString(diagnostic.context + "\n" + diagnostic.cause
            + "\n建议：" + diagnostic.recommendedAction + "\n\n");
    }
    for (const auto& column : outcome.ignoredColumns) { rows += "忽略列：" + QString::fromStdString(column) + "\n"; }
    for (const auto& field : outcome.defaultedFields) { rows += "使用默认字段：" + QString::fromStdString(field) + "\n"; }
    for (const auto& point : outcome.entries) {
        rows += "正确行工位：" + QString::fromStdString(point.name);
        if (const auto position = point.pose.position.tryValue()) {
            rows += QString("；位置（参考系下，m）：%1, %2, %3")
                .arg((*position)[0], 0, 'g', 17).arg((*position)[1], 0, 'g', 17).arg((*position)[2], 0, 'g', 17);
        }
        rows += "\n";
    }
    if (!host.confirmImport(summary + (rows.isEmpty() ? QString() : QStringLiteral("\n") + rows)
                            + QString::fromUtf8("\n\n确认导入正确行到草稿？"))) {
        return true;  // 用户取消
    }
    if (outcome.entries.empty()) {
        note(panel, "无正确行，未写入草稿"); return true; // 不产生空编辑或虚假脏标记。
    }
    const auto batch = importDraftBatch(outcome);
    if (submitBatchEdit(editor, sink, batch) != EditSubmitOutcome::Applied) { return false; }
    note(panel, QString::fromUtf8("已导入 %1 个工位（%2 条错误行跳过）")
        .arg(batch.newPoints.size()).arg(outcome.rowErrors.size()));
    return true;
}

bool flowImportCsv(RequirementsPanelWidget& panel, IRequirementEditor& editor,
                   IRequirementEditSink& sink, CommandDialogHost& host)
{
    const auto file = host.openFilePath(QString::fromUtf8("导入工位（CSV）"),
                                        QString::fromUtf8("CSV 坐标表 (*.csv)"));
    if (!file.has_value()) {
        return true;
    }
    auto reader = io::makeCsvReader();
    const auto probed = reader->probe(std::filesystem::path(file->toStdWString()),
                                      nullptr, nullptr);
    if (!probed) {
        note(panel, QString::fromUtf8("CSV 方言/编码预检失败：")
                        + QString::fromStdString(probed.error.detail));
        return false;
    }
    io::CsvReadOptions options;
    options.headerRow = 1;
    options.retainRows = true;
    const auto read = reader->read(std::filesystem::path(file->toStdWString()), options,
                                   [](std::uint64_t, io::CsvRowView&&) { return true; },
                                   nullptr, nullptr);
    if (!read) {
        note(panel, QString::fromUtf8("CSV 读取失败：")
                        + QString::fromStdString(read.error.detail));
        return false;
    }
    try {
        ImportWizardFlow wizard(importer);
        wizard.setSourceTable(read.value);
        ImportUnitOptions units = ImportUnitOptions::defaults();
        ImportMappingView view = wizard.prepareMapping(units);
        if (!host.editImportMapping(read.value, view.mapping, units)) { return true; }
        const ImportOutcome outcome = wizard.executeMapping(view.mapping, units);
        return confirmImportOutcome(panel, editor, sink, host, outcome,
                                    wizard.lastDiagnostics());
    } catch (const std::exception& e) {
        note(panel, QString::fromUtf8("导入失败：") + QString::fromLocal8Bit(e.what()));
        return false;
    }
}

bool flowImportJson(RequirementsPanelWidget& panel, IRequirementEditor& editor,
                    IRequirementEditSink& sink, CommandDialogHost& host)
{
    const auto file = host.openFilePath(QString::fromUtf8("导入需求（JSON）"),
                                        QString::fromUtf8("JSON 文档 (*.json)"));
    if (!file.has_value()) {
        return true;
    }
    QFile f(*file);
    if (!f.open(QIODevice::ReadOnly)) {
        note(panel, QString::fromUtf8("无法读取文件：") + *file);
        return false;
    }
    const QByteArray bytes = f.readAll();
    const std::vector<std::uint8_t> raw(bytes.cbegin(), bytes.cend());
    std::vector<core::DiagnosticRecord> diags;
    const ImportOutcome outcome = importer.mapJson(raw, diags);
    return confirmImportOutcome(panel, editor, sink, host, outcome, diags);
}

bool flowCaptureTcp(RequirementsPanelWidget& panel,
                    IRequirementEditor& editor, IRequirementEditSink& sink,
                    CommandDialogHost& host,
                    const RequirementsView3DSeams* view3d)
{
    const QPointer<RequirementsPanelWidget> panelAlive(&panel);
    // 缝缺省＝诚实降级原文（back-compat——绑定前两命令保持阶段 A 口径）。
    if (view3d == nullptr || !view3d->listDevices
        || !view3d->tcpWorldPose) {
        note(panel, QString::fromUtf8(
                        "捕获 TCP 需要三维视图关节状态数据源——该数据源将在后续版本提供"));
        return false;
    }
    // 发起即固定草稿和闭包，确认对话的嵌套事件循环可能切换项目或接收编辑。
    // 守卫先检查原身份，短路后才读取原编辑器，不能向复用地址的新会话写旧值。
    const auto canWrite = panel.interactiveWriteGuard(&editor);
    const auto original = editor.workingSet();
    const auto closureRefs = view3d->sessionClosureRefs
        ? view3d->sessionClosureRefs() : std::vector<project::ObjectRef>{};
    // 设备清点（宿主 WorkCell 现查）：零台＝诚实失败；多台＝宿主下拉选择。
    const std::vector<std::string> devices = view3d->listDevices();
    if (devices.empty()) {
        note(panel, QString::fromUtf8(
                        "捕获 TCP 失败：三维视图未发现设备（WorkCell 未装载机器人）"));
        return false;
    }
    QString deviceName = QString::fromStdString(devices.front());
    if (devices.size() > 1) {
        QStringList candidates;
        for (const std::string& d : devices) {
            candidates << QString::fromStdString(d);
        }
        const auto picked =
            host.chooseItem(QString::fromUtf8("选择设备"),
                            QString::fromUtf8("捕获 TCP——请选择目标设备："),
                            candidates);
        if (!picked.has_value()) {
            return false;  // 用户取消——流程静默终止
        }
        if (!panelAlive) { return false; }
        deviceName = candidates.at(*picked);
    }
    // TCP 世界系位姿（网关 State 只读——KIN-06 零修订）。
    const auto pose = view3d->tcpWorldPose(deviceName.toStdString());
    if (!pose.has_value()) {
        note(panel, QString::fromUtf8("捕获 TCP 失败：设备 %1 的 TCP 位姿不可得")
                        .arg(deviceName));
        return false;
    }

    // 目标任务点＝捕获即新建固定点（域语义 captureTcpAsFixedPoint——
    // pointName 确定性命名＋集合内去重后缀，I-REQ-3 唯一性由服务复核）。
    std::vector<std::string> siblingNames;
    for (const TaskPoint& p : editor.workingSet().points.entries) {
        siblingNames.push_back(p.name);
    }
    std::string pointName = "TcpCapture";
    for (int suffix = 2;
         std::find(siblingNames.begin(), siblingNames.end(), pointName)
         != siblingNames.end();
         ++suffix) {
        pointName = "TcpCapture" + std::to_string(suffix);
    }

    // 写回确认（REQ-08——确认凭据随请求过域门；拒绝走服务保守门径）。
    // RPY 呈现反解（UI-T53 附带修复——rw::math::RPY 外联符号换逐元素解析，
    // 见 rotationToRpyPresentation 注）。
    const rw::math::Vector3D<double> p = pose->P();
    const auto rpy = rotationToRpyPresentation(pose->R());
    const QString summary =
        QString::fromUtf8("捕获 TCP（设备 %1）：位置（%2, %3, %4） m、"
                          "姿态 RPY（%5, %6, %7） rad——新建固定任务点 %8。确认写回？")
            .arg(deviceName)
            .arg(p[0], 0, 'f', 4)
            .arg(p[1], 0, 'f', 4)
            .arg(p[2], 0, 'f', 4)
            .arg(rpy[0], 0, 'f', 4)
            .arg(rpy[1], 0, 'f', 4)
            .arg(rpy[2], 0, 'f', 4)
            .arg(QString::fromStdString(pointName));
    if (!host.confirmImport(summary)) {  // 确认缝复用（CommandDialogHost 通用摘要确认面——REQ-08 语义)
        return false;  // 用户取消——流程静默终止（零数据变更）
    }
    if (!panelAlive) { return false; }

    // 写回前再次现取真值；位姿单位m/rad、世界系，比较原捕获矩阵而非摘要舍入值。
    // UI线程同步FK无随机噪声，同状态应逐元素一致；任何变化都要求用户重捕。
    const auto currentPose = view3d->tcpWorldPose(deviceName.toStdString());
    if (!canWrite() || editor.workingSet() != original
        || view3d->listDevices() != devices || currentPose != pose
        || (view3d->sessionClosureRefs && view3d->sessionClosureRefs() != closureRefs)) {
        note(panel, QString::fromUtf8("捕获未写入：会话、草稿、可写状态或TCP已改变，请重新捕获"));
        return false;
    }

    // 域服务裁决（REQ-08 确认门＋STALE 对账＋I-REQ-3/5 校验全部在域侧——
    // 流程零判定；World 缺省参考系＝捕获值世界系直投）。
    CapturedTcpPose captured;
    captured.position = p;
    captured.rpy = rw::math::Vector3D<double>(rpy[0], rpy[1], rpy[2]);
    captured.refFrame = RequirementReference{};  // World 缺省（捕获值世界系）
    captured.sessionRevisionId = view3d->sessionRevisionId;
    CaptureTcpRequest request;
    request.captured = captured;
    request.pointName = pointName;
    request.tolerance = ToleranceSpec{};  // 设计默认（§4.3）
    request.confirmation = CaptureConfirmation::confirmed(
        "ui", std::chrono::system_clock::now());
    request.siblingNames = siblingNames;

    RequirementCaptureService service;
    const CaptureOutcome outcome =
        service.captureTcpAsFixedPoint(editor, request, CheckContext{closureRefs});
    for (const core::DiagnosticRecord& diag : outcome.diags) {
        note(panel, QString::fromStdString(diag.cause));
    }
    if (!outcome.accepted) {
        note(panel, QString::fromUtf8("捕获 TCP 未写入（域侧拒绝）——详见上方诊断"));
        return false;
    }
    // 域服务已产生恰一个局部栈帧；复用批次完成通知统一记账/刷新/标脏。
    sink.onBatchEditApplied("捕获TCP并新建固定任务点");
    sink.notifySessionDirty();
    note(panel, QString::fromUtf8("已捕获 TCP 至新任务点 %1（位置与固定姿态均为世界系）")
                    .arg(QString::fromStdString(pointName)));
    return true;
}

bool flowPickFeature(RequirementsPanelWidget& panel,
                     IRequirementEditor& editor, IRequirementEditSink& sink,
                     CommandDialogHost& host,
                     const RequirementsView3DSeams* view3d)
{
    const QPointer<RequirementsPanelWidget> panelAlive(&panel);
    // 缝缺省＝诚实降级原文（back-compat 同 capture-tcp）。
    if (view3d == nullptr || !view3d->resolveFrameObjectId
        || !view3d->lastPickedObjectId) {
        note(panel, QString::fromUtf8(
                        "拾取几何特征需要三维视图——三维交互将在后续版本提供"));
        return false;
    }
    const std::optional<core::ObjectId> picked = view3d->lastPickedObjectId();
    if (!picked.has_value()) {
        // 网关分发未命中本域（名称映射未发布/非业务对象）——交互指引
        // 取代阶段 A 的"版本未提供"（真实可操作路径的诚实呈现）。
        note(panel, QString::fromUtf8(
                        "尚未拾取到模型元素——请先在三维视图按住 Ctrl 双击目标对象"));
        return false;
    }
    const std::optional<core::ObjectId> targetPoint = panel.selectedPointId();
    if (!targetPoint.has_value()) {
        note(panel, QString::fromUtf8(
                        "拾取几何特征需要目标工位——请先在需求树或工位页选中目标任务点"));
        return false;
    }
    const auto canWrite = panel.interactiveWriteGuard(&editor);
    const auto original = editor.workingSet();
    CheckContext closureContext;
    if (view3d->sessionClosureRefs) { closureContext.closureRefs = view3d->sessionClosureRefs(); }
    // 按现有浅引用契约分流；不能把场景对象或joint/link身份伪装成robot-design。
    const auto ref = std::find_if(closureContext.closureRefs.begin(), closureContext.closureRefs.end(),
        [&](const auto& value) { return value.objectId == *picked; });
    if (ref == closureContext.closureRefs.end()) {
        note(panel, QStringLiteral("拾取目标不在当前修订中，请重新拾取")); return false;
    }
    PickedFeature feature;
    feature.target.objectId = *picked;
    QString featureLabel;
    if (ref->objectTypeToken == expectedTargetToken(RequirementRefKind::SceneObject)) {
        feature.target.kind = RequirementRefKind::SceneObject;
        const QStringList choices{QStringLiteral("对齐对象坐标系"),
            QStringLiteral("对齐对象坐标平面法向"), QStringLiteral("对齐对象反向坐标平面法向")};
        const auto choice = host.chooseItem(QStringLiteral("选择拾取特征"),
            QStringLiteral("使用对象坐标系，或该坐标系平面的法向："), choices);
        if (!choice || !panelAlive) { return false; }
        if (*choice < 0 || *choice >= choices.size()) { return false; }
        featureLabel = choices.at(*choice);
        if (*choice != 0) {
            feature.feature = OrientationFeature::FramePlaneNormal;
            feature.invertNormal = *choice == 2;
        }
    } else if (ref->objectTypeToken == expectedTargetToken(RequirementRefKind::ModelFrame)) {
        feature.target.kind = RequirementRefKind::ModelFrame;
        featureLabel = QStringLiteral("对齐模型坐标系");
    } else {
        note(panel, QStringLiteral("该拾取对象不是可引用的模型或场景坐标系，请选择夹具/零件坐标系"));
        return false;
    }

    // 写回前确认（REQ-08——L-R6"写回前确认"的对话桥承载：用户拒绝＝
    // 零数据变更静默终止；确认凭据在用户 Yes 之后才采集——SA-15 禁止
    // 凭据先于确认事实存在）。
    const QString confirmText =
        QString::fromUtf8("拾取几何特征写回确认\n\n"
                          "将把三维拾取特征写入原目标工位：%1。确认写回？").arg(featureLabel);
    if (!host.confirmImport(confirmText)) {
        return false;  // 用户取消＝零数据变更（SA-15 导入路径取消面）
    }
    if (!panelAlive) { return false; }
    if (!canWrite() || editor.workingSet() != original || panel.selectedPointId() != targetPoint
        || view3d->lastPickedObjectId() != picked
        || (view3d->sessionClosureRefs && view3d->sessionClosureRefs() != closureContext.closureRefs)) {
        note(panel, QStringLiteral("拾取未写入：会话、目标、可写状态或拾取结果已改变，请重新拾取"));
        return false;
    }

    // 域服务裁决（applyPickToOrientation——拾取目标→工位姿态规则写回；
    // 确认凭据随请求过域门）。
    ApplyPickRequest request;
    request.picked = feature;
    request.targetPointId = *targetPoint;
    request.confirmation = CaptureConfirmation::confirmed(
        "ui", std::chrono::system_clock::now());

    RequirementCaptureService service;
    // 浅核对上下文（UI-T33 收口——会话闭包引用元数据经缝取自宿主查询
    // 端口 head().objectRefs；缝缺省/空返回＝空上下文——picked 目标浅
    // 核对如实拒绝，不虚构通过）。
    const CaptureOutcome outcome =
        service.applyPickToOrientation(editor, request, closureContext);
    for (const core::DiagnosticRecord& diag : outcome.diags) {
        note(panel, QString::fromStdString(diag.cause));
    }
    if (!outcome.accepted) {
        note(panel, QString::fromUtf8("拾取结果未写入（域侧拒绝）——详见上方诊断"));
        return false;
    }
    sink.onBatchEditApplied("拾取特征并更新工位姿态规则");
    sink.notifySessionDirty();
    note(panel, QString::fromUtf8("拾取结果已写入目标工位姿态规则"));
    return true;
}

}  // namespace

bool executeRequirementCommand(const std::string& commandId,
                               RequirementsPanelWidget& panel,
                               IRequirementEditor& editor,
                               IRequirementEditSink& sink,
                               CommandDialogHost& host)
{
    return executeRequirementCommand(commandId, panel, editor, sink, host,
                                     nullptr);  // 无缝＝两命令诚实降级原文
}

bool executeRequirementCommand(const std::string& commandId,
                               RequirementsPanelWidget& panel,
                               IRequirementEditor& editor,
                               IRequirementEditSink& sink,
                               CommandDialogHost& host,
                               const RequirementsView3DSeams* view3d)
{
    if (commandId == "requirements.export-copy") {
        return flowExportCopy(panel, editor, host);
    }
    if (commandId == "requirements.apply-template") {
        return flowApplyTemplate(panel, editor, sink, host);
    }
    if (commandId == "requirements.mirror-stations") {
        return flowMirrorStations(panel, editor, sink, host);
    }
    if (commandId == "requirements.create-array") {
        return flowCreateArray(panel, editor, sink, host);
    }
    if (commandId == "requirements.regenerate-linked") {
        return flowRegenerateLinked(panel, editor, sink, host);
    }
    if (commandId == "requirements.import-csv") {
        return flowImportCsv(panel, editor, sink, host);
    }
    if (commandId == "requirements.import-json") {
        return flowImportJson(panel, editor, sink, host);
    }
    if (commandId == "requirements.capture-tcp") {
        return flowCaptureTcp(panel, editor, sink, host, view3d);
    }
    if (commandId == "requirements.pick-feature") {
        return flowPickFeature(panel, editor, sink, host, view3d);
    }
    note(panel, QString::fromUtf8("该流程将在后续版本提供（%1）")
                    .arg(QString::fromStdString(commandId)));
    return false;
}

bool executeRequirementCommand(const std::string& commandId,
                               RequirementsPanelWidget& panel,
                               IRequirementEditor& editor,
                               IRequirementEditSink& sink)
{
    return executeRequirementCommand(commandId, panel, editor, sink, qtDialogHost());
}

}  // namespace sdurws::ird::requirements
