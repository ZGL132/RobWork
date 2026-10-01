/**
 * @file   RequirementsCommandFlows.cpp
 * @brief  需求域九命令 UI 流程实现（UI-T32 组1：导出副本/模板/阵列/
 *         镜像/重生成＋解除关联；组2 导入、组3 捕获/拾取随后续提交）。
 *
 * 装配纪律：域逻辑全部经 TemplateArrayService/RequirementImporter 纯函数
 * （零重写——knownPitfalls 2）；批量应用经 submitBatchEdit（一次批次一次
 * 入栈——L-R9 主干）；对话框父窗口＝域面板 Dock（模态——宿主内居中）。
 * 反馈＝面板状态行中文摘要（UX-02；错误就地非模态——UX-03）。
 */

#include "RequirementsCommandFlows.hpp"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QFile>
#include <QIODevice>
#include <QSpinBox>
#include <QVBoxLayout>

#include <sdurws/ird/requirements/Import.hpp>        // RequirementImporter/exportCopy
#include <sdurws/ird/requirements/TemplateArray.hpp>  // TemplateArrayService/defaultTemplateParams

#include "PanelEditFlow.hpp"             // submitBatchEdit/IRequirementEditSink
#include "ImportWizardFlow.hpp"        // L-R10 五步向导域侧流（组2）
#include <sdurws/ird/io/Csv.hpp>          // makeCsvReader/RawTable（CSV 通道）
#include "RequirementsPanelWidget.hpp"   // 面板（状态行/选中锚）

namespace sdurws::ird::requirements {
namespace {

/// 域服务实例（无状态纯函数——函数局部构造即用，无共享态）。
TemplateArrayService templateService{};
RequirementImporter importer{};

/// 状态行反馈辅助（面板私有成员不可直达——经面板公开的反馈入口）。
void note(RequirementsPanelWidget& panel, const QString& text);

// ---------------------------------------------------------------------
// 导出副本（requirements.export-copy——readOnlyAllowed=true）
// ---------------------------------------------------------------------
bool flowExportCopy(RequirementsPanelWidget& panel, IRequirementEditor& editor)
{
    const QString selected = QFileDialog::getSaveFileName(
        &panel, QString::fromUtf8("导出需求副本"),
        QString::fromUtf8("requirements-copy.requirements.json"),
        QString::fromUtf8("JSON 副本 (*.requirements.json);;CSV 坐标表 (*.csv)"));
    if (selected.isEmpty()) {
        return true;  // 用户取消——非失败
    }
    ExportTarget target;
    target.filePath = std::filesystem::path(selected.toStdWString());
    target.draft = true;  // 草稿导出（O-46 口径：本批只出草稿副本）
    const ExportFormat format = selected.endsWith(QStringLiteral(".csv"),
                                                  Qt::CaseInsensitive)
                                    ? ExportFormat::Csv
                                    : ExportFormat::Json;
    const ExportOutcome out =
        importer.exportCopy(editor.workingSet(), format, target);
    if (!out.ok) {
        note(panel, QString::fromUtf8("导出失败：") + QString::fromStdString(out.error));
        return false;
    }
    note(panel, QString::fromUtf8("已导出需求副本（草稿标记）：") + selected);
    return true;
}

// ---------------------------------------------------------------------
// 应用工艺模板（requirements.apply-template——六类参数表单＋计数预览）
// ---------------------------------------------------------------------
bool flowApplyTemplate(RequirementsPanelWidget& panel, IRequirementEditor& editor,
                       IRequirementEditSink& sink)
{
    QDialog dialog(&panel);
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
    form->addRow(QString::fromUtf8("模板类别"), kindBox);

    // 参数行（单位标注——UX-05；默认值＝域黄金默认 defaultTemplateParams）。
    auto* baseName = new QLineEdit(QString::fromUtf8("工位"), &dialog);
    auto* spacing = new QDoubleSpinBox(&dialog);
    spacing->setSuffix(QString::fromUtf8(" m"));
    spacing->setRange(0.01, 100.0);
    auto* countX = new QSpinBox(&dialog);
    countX->setRange(1, 100);
    auto* countY = new QSpinBox(&dialog);
    countY->setRange(1, 100);
    form->addRow(QString::fromUtf8("名称前缀"), baseName);
    form->addRow(QString::fromUtf8("X 向间距"), spacing);
    form->addRow(QString::fromUtf8("X 向数量"), countX);
    form->addRow(QString::fromUtf8("Y 向数量"), countY);

    // 生成计数预览（参数/类别变更即重算——域函数试算，UI 零判定）。
    auto* preview = new QLabel(&dialog);
    auto refreshPreview = [&]() {
        TemplateParams params = defaultTemplateParams(
            TemplateKind(kindBox->currentData().toInt()));
        params.baseName = baseName->text().toStdString();
        params.spacingM = spacing->value();
        params.countX = countX->value();
        params.countY = countY->value();
        const EditBatch trial = templateService.applyTemplate(
            TemplateKind(kindBox->currentData().toInt()), params);
        preview->setText(trial.ok
                             ? QString::fromUtf8("将生成 %1 个工位").arg(
                                   int(trial.newPoints.size()))
                             : QString::fromUtf8("参数待修正：%1").arg(
                                   QString::fromStdString(trial.error.detail)));
    };
    QObject::connect(kindBox, &QComboBox::currentIndexChanged, &dialog, refreshPreview);
    // 类别切换时同步该类黄金默认到表单（用户可再改）。
    QObject::connect(kindBox, &QComboBox::currentIndexChanged, &dialog, [&]() {
        const TemplateParams def = defaultTemplateParams(
            TemplateKind(kindBox->currentData().toInt()));
        spacing->setValue(def.spacingM);
        countX->setValue(def.countX);
        countY->setValue(def.countY);
    });
    kindBox->currentIndexChanged(0);  // 初始默认

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         Qt::Horizontal, &dialog);
    form->addRow(preview);
    form->addRow(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) {
        return true;
    }

    TemplateParams params = defaultTemplateParams(TemplateKind(kindBox->currentData().toInt()));
    params.baseName = baseName->text().toStdString();
    params.spacingM = spacing->value();
    params.countX = countX->value();
    params.countY = countY->value();
    const EditBatch batch = templateService.applyTemplate(
        TemplateKind(kindBox->currentData().toInt()), params);
    if (!batch.ok) {
        note(panel, QString::fromUtf8("未应用（模板参数）：")
                        + QString::fromStdString(batch.error.detail));
        return false;
    }
    const EditSubmitOutcome outcome = submitBatchEdit(editor, sink, batch);
    return outcome == EditSubmitOutcome::Applied;
}

// ---------------------------------------------------------------------
// 工位镜像（requirements.mirror-stations——选中工位＋三预设镜面）
// ---------------------------------------------------------------------
bool flowMirrorStations(RequirementsPanelWidget& panel, IRequirementEditor& editor,
                        IRequirementEditSink& sink)
{
    const std::optional<core::ObjectId> selected = panel.selectedObjectId();
    if (!selected.has_value()) {
        note(panel, QString::fromUtf8("未应用：镜像需要先选中一个工位条目"));
        return false;
    }
    const QStringList planes{QString::fromUtf8("YZ 面（X=0）"), QString::fromUtf8("XZ 面（Y=0）"),
                             QString::fromUtf8("XY 面（Z=0）")};
    bool ok = false;
    QString choiceStr = QInputDialog::getItem(&panel, QString::fromUtf8("镜像工位"),
                                             QString::fromUtf8("镜面（过参考系原点）："),
                                             planes, 0, false, &ok);
    if (!ok) {
        return true;
    }
    const rw::math::Vector3D<double> normals[3] = {
        {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}};
    MirrorPlaneSpec plane;
    plane.axisNormal = normals[static_cast<int>(planes.indexOf(choiceStr))];

    std::vector<TaskPoint> sources;
    for (const TaskPoint& p : editor.workingSet().points.entries) {
        if (p.objectId == selected.value()) {
            sources.push_back(p);
        }
    }
    const EditBatch batch = templateService.applyMirror(sources, plane);
    if (!batch.ok) {
        note(panel, QString::fromUtf8("未应用（镜像）：")
                        + QString::fromStdString(batch.error.detail));
        return false;
    }
    return submitBatchEdit(editor, sink, batch) == EditSubmitOutcome::Applied;
}

// ---------------------------------------------------------------------
// 批量阵列（requirements.create-array——四构型参数表单）
// ---------------------------------------------------------------------
bool flowCreateArray(RequirementsPanelWidget& panel, IRequirementEditor& editor,
                     IRequirementEditSink& sink)
{
    const std::optional<core::ObjectId> selected = panel.selectedObjectId();
    if (!selected.has_value()) {
        note(panel, QString::fromUtf8("未应用：阵列需要先选中一个源工位条目"));
        return false;
    }
    const QStringList kinds{QString::fromUtf8("线性"), QString::fromUtf8("矩形"),
                            QString::fromUtf8("圆形"), QString::fromUtf8("折线")};
    bool ok = false;
    QString kindStr = QInputDialog::getItem(&panel, QString::fromUtf8("批量阵列"),
                                                 QString::fromUtf8("阵列构型："), kinds, 0,
                                                 false, &ok);
    if (!ok) {
        return true;
    }
    QDialog dialog(&panel);
    dialog.setWindowTitle(QString::fromUtf8("批量阵列参数"));
    auto* form = new QFormLayout(&dialog);
    auto* count = new QSpinBox(&dialog);
    count->setRange(1, 1000);
    auto* spacing = new QDoubleSpinBox(&dialog);
    spacing->setSuffix(QString::fromUtf8(" m"));
    spacing->setRange(0.001, 100.0);
    spacing->setValue(0.5);
    form->addRow(QString::fromUtf8("生成数量"), count);
    form->addRow(QString::fromUtf8("间距/半径"), spacing);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         Qt::Horizontal, &dialog);
    form->addRow(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) {
        return true;
    }

    ArrayParams params;
    for (const TaskPoint& p : editor.workingSet().points.entries) {
        if (p.objectId == selected.value()) {
            params.sources.push_back(p);  // 源条目值（服务零回写——I-REQ-10）
        }
    }
    const ArrayKind kind = ArrayKind(static_cast<int>(kinds.indexOf(kindStr)));
    if (kind == ArrayKind::Linear) {
        params.count = count->value();
        params.spacingM = spacing->value();
    } else if (kind == ArrayKind::Rectangular) {
        params.count = count->value();
        params.spacingM = spacing->value();
        params.count2 = qMax(1, count->value() / 2);
        params.spacing2M = spacing->value();
    } else if (kind == ArrayKind::Circular) {
        params.count = count->value();
        params.radiusM = spacing->value();
        // 全圆均布（起始 +X、步距 2π/N——对话框无角度字段时的确定默认）。
        params.angleStepRad = 2.0 * 3.14159265358979323846 / count->value();
    } else {
        // 折线：默认沿 +X 两顶点（顶点编辑归后续增量——确定默认保证可生成）。
        params.count = count->value();
        params.polyline = {rw::math::Vector3D<double>(0.0, 0.0, 0.0),
                           rw::math::Vector3D<double>(spacing->value(), 0.0, 0.0)};
    }
    const EditBatch batch = templateService.applyArray(kind, params);
    if (!batch.ok) {
        note(panel, QString::fromUtf8("未应用（阵列参数）：")
                        + QString::fromStdString(batch.error.detail));
        return false;
    }
    return submitBatchEdit(editor, sink, batch) == EditSubmitOutcome::Applied;
}

// ---------------------------------------------------------------------
// 重生成/解除关联（requirements.regenerate-linked——选中 linked 工位）
// ---------------------------------------------------------------------
bool flowRegenerateLinked(RequirementsPanelWidget& panel, IRequirementEditor& editor,
                          IRequirementEditSink& sink)
{
    const std::optional<core::ObjectId> selected = panel.selectedObjectId();
    if (!selected.has_value()) {
        note(panel, QString::fromUtf8("未应用：重生成需要先选中一个生成工位"));
        return false;
    }
    // 选中条目的生成批次实例标识（generation 为 optional：无溯源＝手工
    // 条目，无批次可重生成；linked=false＝已解除，同样不参与）。
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
    QMessageBox box(QMessageBox::Question, QString::fromUtf8("重生成/解除关联"),
                    QString::fromUtf8("对生成批次执行："), QMessageBox::NoButton, &panel);
    const QPushButton* regenerate =
        box.addButton(QString::fromUtf8("按参数重生成"), QMessageBox::AcceptRole);
    const QPushButton* detach =
        box.addButton(QString::fromUtf8("解除关联（保留条目）"), QMessageBox::ActionRole);
    box.addButton(QMessageBox::Cancel);
    box.exec();
    if (box.clickedButton() == regenerate) {
        const RegenerateOutcome out =
            templateService.regenerate(editor.workingSet(), instanceId);
        if (!out.found) {
            note(panel, QString::fromUtf8("未应用（重生成定位）：")
                            + QString::fromStdString(out.error.detail));
            return false;
        }
        return submitBatchEdit(editor, sink, out.batch) == EditSubmitOutcome::Applied;
    }
    if (box.clickedButton() == detach) {
        // 解除关联＝unlinkGenerator 产出新条目值（linked=false）——逐条
        // upsert（编辑器单快照语义按批次：构造 EditBatch 走 submitBatchEdit
        // 不可行〔unlink 产出非批次〕，逐条 applyEdit 各自成步——解除是
        // 溯源元数据变更，逐条快照可接受〔撤销多步〕，中文摘要如实）。
        const std::vector<TaskPoint> unlinked =
            unlinkGenerator(editor.workingSet().points.entries, instanceId);
        int applied = 0;
        for (const TaskPoint& p : unlinked) {
            if (editor.applyEdit(RequirementEdit{p}).accepted) {
                ++applied;
            }
        }
        sink.onEditApplied("解除关联：" + std::to_string(applied) + " 个条目转为手工条目");
        note(panel, QString::fromUtf8("已解除关联 %1 个条目").arg(applied));
        return applied > 0;
    }
    return true;  // 取消
}

void note(RequirementsPanelWidget& panel, const QString& text)
{
    panel.showCommandFeedback(text);
}

// ---------------------------------------------------------------------
// 导入向导（组2——CSV 经 ImportWizardFlow 五步；JSON 直映射同预览确认）
// ---------------------------------------------------------------------

/// 导入产出预览确认（计数＋行级错误清单前 8 条——契约"行级错误列表"
/// 呈现面；用户确认＝正确行入草稿〔部分成功语义 AT-02〕）。
bool confirmImportOutcome(RequirementsPanelWidget& panel, IRequirementEditor& editor,
                          IRequirementEditSink& sink, const ImportOutcome& outcome,
                          const std::vector<core::DiagnosticRecord>& diags)
{
    if (outcome.status == ImportOutcome::Status::Rejected) {
        QString detail;
        for (const core::DiagnosticRecord& d : diags) {
            detail += QString::fromStdString(d.cause) + QStringLiteral("\n");
        }
        note(panel, QString::fromUtf8("导入被拒绝（结构级）：") + detail.trimmed());
        return false;
    }
    QString summary = QString::fromUtf8("可导入行：%1；错误行：%2")
                          .arg(int(outcome.entries.size()))
                          .arg(int(outcome.rowErrors.size()));
    if (!outcome.ignoredColumns.empty()) {
        summary += QString::fromUtf8("\n忽略列（未映射）：%1")
                       .arg(QString::fromStdString(
                           [&] { std::string s; for (auto& c : outcome.ignoredColumns) { s += c + ","; } return s; }()));
    }
    QString rows;
    int shown = 0;
    for (const core::DiagnosticRecord& e : outcome.rowErrors) {
        if (shown++ >= 8) {
            rows += QString::fromUtf8("……（共 %1 条）\n").arg(int(outcome.rowErrors.size()));
            break;
        }
        rows += QString::fromUtf8("行错误：%1\n").arg(QString::fromStdString(e.cause));
    }
    const QMessageBox::StandardButton choice = QMessageBox::question(
        &panel, QString::fromUtf8("导入预览确认"),
        summary + (rows.isEmpty() ? QString() : QStringLiteral("\n") + rows)
            + QString::fromUtf8("\n\n确认导入正确行到草稿？"),
        QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Cancel);
    if (choice != QMessageBox::Ok) {
        return true;  // 用户取消——非失败
    }
    // 应用：逐条 applyEdit（向导 confirmAndApplyToDraft 同语义——正确行
    // 入草稿；每条单快照，撤销逐步回退）。
    int applied = 0;
    for (const TaskPoint& p : outcome.entries) {
        if (editor.applyEdit(RequirementEdit{p}).accepted) {
            ++applied;
        }
    }
    sink.onEditApplied("导入完成：新增 " + std::to_string(applied) + " 个工位（错误行 "
                       + std::to_string(outcome.rowErrors.size()) + " 条已跳过）");
    note(panel, QString::fromUtf8("已导入 %1 个工位（%2 条错误行跳过）")
                    .arg(applied).arg(int(outcome.rowErrors.size())));
    return applied > 0 || outcome.entries.empty();
}

bool flowImportCsv(RequirementsPanelWidget& panel, IRequirementEditor& editor,
                   IRequirementEditSink& sink)
{
    const QString file = QFileDialog::getOpenFileName(
        &panel, QString::fromUtf8("导入工位（CSV）"), QString(),
        QString::fromUtf8("CSV 坐标表 (*.csv)"));
    if (file.isEmpty()) {
        return true;
    }
    // io 读取（probe→read retainRows——RawTable 在手）；方言失败＝结构级
    // 反馈（不猜——io 纪律）。
    auto reader = io::makeCsvReader();
    const auto probed = reader->probe(std::filesystem::path(file.toStdWString()),
                                      /*budget=*/nullptr, /*cancel=*/nullptr);
    if (!probed) {
        note(panel, QString::fromUtf8("CSV 方言/编码预检失败：")
                        + QString::fromStdString(probed.error.detail));
        return false;
    }
    io::CsvReadOptions options;
    options.headerRow = 1;      // 首行表头（字段字典自动识别前提）
    options.retainRows = true;  // 向导需要整表（映射/预览）
    const auto read = reader->read(std::filesystem::path(file.toStdWString()), options,
                                   [](std::uint64_t, io::CsvRowView&&) { return true; },
                                   nullptr, nullptr);
    if (!read) {
        note(panel, QString::fromUtf8("CSV 读取失败：")
                        + QString::fromStdString(read.error.detail));
        return false;
    }
    // 域向导五步流：表→自动映射＋单位预览→映射执行→（预览确认在共通面）。
    try {
        ImportWizardFlow wizard(importer);
        wizard.setSourceTable(read.value);
        const ImportUnitOptions units = ImportUnitOptions::defaults();
        const ImportMappingView view = wizard.prepareMapping(units);
        const ImportOutcome outcome = wizard.executeMapping(view.mapping, units);
        return confirmImportOutcome(panel, editor, sink, outcome,
                                    wizard.lastDiagnostics());
    } catch (const std::exception& e) {
        note(panel, QString::fromUtf8("导入失败：") + QString::fromLocal8Bit(e.what()));
        return false;
    }
}

bool flowImportJson(RequirementsPanelWidget& panel, IRequirementEditor& editor,
                    IRequirementEditSink& sink)
{
    const QString file = QFileDialog::getOpenFileName(
        &panel, QString::fromUtf8("导入需求（JSON）"), QString(),
        QString::fromUtf8("JSON 文档 (*.json)"));
    if (file.isEmpty()) {
        return true;
    }
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) {
        note(panel, QString::fromUtf8("无法读取文件：") + file);
        return false;
    }
    const QByteArray bytes = f.readAll();
    std::vector<std::uint8_t> raw(bytes.cbegin(), bytes.cend());
    std::vector<core::DiagnosticRecord> diags;
    const ImportOutcome outcome = importer.mapJson(raw, diags);
    return confirmImportOutcome(panel, editor, sink, outcome, diags);
}

// ---------------------------------------------------------------------
// 捕获 TCP / 拾取几何特征（组3——降级与流程壳）
// ---------------------------------------------------------------------

bool flowCaptureTcp(RequirementsPanelWidget& panel, IRequirementEditor& /*editor*/)
{
    // 宿主关节状态来源未接线（RuntimePublishBridge 名称解析当前空映射——
    // knownPitfalls 4 降级路径）：不伪造当前位姿，就地引导。
    note(panel, QString::fromUtf8(
                    "捕获 TCP 需要三维视图关节状态数据源——该数据源将在后续版本提供"));
    return false;
}

bool flowPickFeature(RequirementsPanelWidget& panel)
{
    // 流程壳（UI-T33 收口——三维拾取上行就绪前保持引导文案，不私开通道）。
    note(panel, QString::fromUtf8(
                    "拾取几何特征需要三维视图——三维交互将在后续版本提供"));
    return false;
}

}  // namespace

bool executeRequirementCommand(const std::string& commandId,
                               RequirementsPanelWidget& panel,
                               IRequirementEditor& editor,
                               IRequirementEditSink& sink)
{
    if (commandId == "requirements.export-copy") {
        return flowExportCopy(panel, editor);
    }
    if (commandId == "requirements.apply-template") {
        return flowApplyTemplate(panel, editor, sink);
    }
    if (commandId == "requirements.mirror-stations") {
        return flowMirrorStations(panel, editor, sink);
    }
    if (commandId == "requirements.create-array") {
        return flowCreateArray(panel, editor, sink);
    }
    if (commandId == "requirements.regenerate-linked") {
        return flowRegenerateLinked(panel, editor, sink);
    }
    if (commandId == "requirements.import-csv") {
        return flowImportCsv(panel, editor, sink);
    }
    if (commandId == "requirements.import-json") {
        return flowImportJson(panel, editor, sink);
    }
    if (commandId == "requirements.capture-tcp") {
        return flowCaptureTcp(panel, editor);
    }
    if (commandId == "requirements.pick-feature") {
        return flowPickFeature(panel);
    }
    note(panel, QString::fromUtf8("该流程将在后续版本提供（%1）")
                    .arg(QString::fromStdString(commandId)));
    return false;
}

}  // namespace sdurws::ird::requirements
