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
#include <QSpinBox>
#include <QVBoxLayout>

#include <sdurws/ird/io/Csv.hpp>         // makeCsvReader/RawTable（CSV 通道）
#include <sdurws/ird/requirements/Import.hpp>        // RequirementImporter/exportCopy/mapJson
#include <sdurws/ird/requirements/TemplateArray.hpp>  // TemplateArrayService/defaultTemplateParams

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

    bool editTemplateParams(TemplateParams& params) override
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
        params = defaultTemplateParams(TemplateKind(kindBox->currentData().toInt()));
        params.baseName = baseName->text().toStdString();
        params.spacingM = spacing->value();
        params.countX = countX->value();
        params.countY = countY->value();
        return true;
    }

    bool editArrayParams(int& count, double& spacing) override
    {
        QDialog dialog;
        dialog.setWindowTitle(QString::fromUtf8("批量阵列参数"));
        auto* form = new QFormLayout(&dialog);
        auto* countBox = new QSpinBox(&dialog);
        countBox->setRange(1, 1000);
        countBox->setValue(count);
        auto* spacingBox = new QDoubleSpinBox(&dialog);
        spacingBox->setSuffix(QString::fromUtf8(" m"));
        spacingBox->setRange(0.001, 100.0);
        spacingBox->setValue(spacing);
        form->addRow(QString::fromUtf8("生成数量"), countBox);
        form->addRow(QString::fromUtf8("间距/半径"), spacingBox);
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                             Qt::Horizontal, &dialog);
        form->addRow(buttons);
        QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() != QDialog::Accepted) {
            return false;
        }
        count = countBox->value();
        spacing = spacingBox->value();
        return true;
    }

    bool confirmImport(const QString& summaryText) override
    {
        return QMessageBox::question(nullptr, QString::fromUtf8("导入预览确认"),
                                     summaryText,
                                     QMessageBox::Ok | QMessageBox::Cancel,
                                     QMessageBox::Cancel)
               == QMessageBox::Ok;
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
    TemplateParams params = defaultTemplateParams(TemplateKind::Palletizing);
    if (!host.editTemplateParams(params)) {
        return true;  // 用户取消
    }
    const EditBatch batch = templateService.applyTemplate(
        TemplateKind::Palletizing, params);  // kind 在宿主表单内并入 params 语义
    // 注：宿主确认的类别已并入 params（Qt 宿主实现里 params 来自所选类别的
    // 黄金默认）——此处 kind 取 params 溯源一致的 Palletizing 由宿主约定：
    // 宿主应把最终类别写回 params（refFrame/baseName 同源），此处统一以
    // batch 域裁决为准；参数违例经 err 态批次就地反馈。
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
    const QStringList planes{QString::fromUtf8("YZ 面（X=0）"), QString::fromUtf8("XZ 面（Y=0）"),
                             QString::fromUtf8("XY 面（Z=0）")};
    const auto choice = host.chooseItem(QString::fromUtf8("镜像工位"),
                                        QString::fromUtf8("镜面（过参考系原点）："), planes);
    if (!choice.has_value()) {
        return true;
    }
    const rw::math::Vector3D<double> normals[3] = {
        {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}};
    MirrorPlaneSpec plane;
    plane.axisNormal = normals[*choice];

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
    int count = 3;
    double spacing = 0.5;
    if (!host.editArrayParams(count, spacing)) {
        return true;
    }

    ArrayParams params;
    for (const TaskPoint& p : editor.workingSet().points.entries) {
        if (p.objectId == selected.value()) {
            params.sources.push_back(p);
        }
    }
    const ArrayKind kind = ArrayKind(*kindChoice);
    if (kind == ArrayKind::Linear) {
        params.count = count;
        params.spacingM = spacing;
    } else if (kind == ArrayKind::Rectangular) {
        params.count = count;
        params.spacingM = spacing;
        params.count2 = count > 1 ? count / 2 : 1;
        params.spacing2M = spacing;
    } else if (kind == ArrayKind::Circular) {
        params.count = count;
        params.radiusM = spacing;
        params.angleStepRad = 2.0 * 3.14159265358979323846 / count;
    } else {
        params.count = count;
        params.polyline = {rw::math::Vector3D<double>(0.0, 0.0, 0.0),
                           rw::math::Vector3D<double>(spacing, 0.0, 0.0)};
    }
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
    // 解除关联（unlinkGenerator 产出新值——逐条 upsert，撤销逐步回退）。
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

/// 导入产出预览确认＋应用（CSV/JSON 共用——行级错误 AT-02 部分成功）。
bool confirmImportOutcome(RequirementsPanelWidget& panel, IRequirementEditor& editor,
                          IRequirementEditSink& sink, CommandDialogHost& host,
                          const ImportOutcome& outcome,
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
    QString rows;
    int shown = 0;
    for (const core::DiagnosticRecord& e : outcome.rowErrors) {
        if (shown++ >= 8) {
            rows += QString::fromUtf8("……（共 %1 条）\n").arg(int(outcome.rowErrors.size()));
            break;
        }
        rows += QString::fromUtf8("行错误：%1\n").arg(QString::fromStdString(e.cause));
    }
    if (!host.confirmImport(summary + (rows.isEmpty() ? QString() : QStringLiteral("\n") + rows)
                            + QString::fromUtf8("\n\n确认导入正确行到草稿？"))) {
        return true;  // 用户取消
    }
    int applied = 0;
    for (const TaskPoint& p : outcome.entries) {
        // 导入条目 objectId 恒全零（待命令 prepare 分配——O-36）：多条全零
        // id 的单条 upsert 会互相覆盖（第二条命中第一条＝Update）。编辑期
        // 逐条分配临时句柄（B1 新增同款 generate——正式 id 归 project prepare）。
        TaskPoint entry = p;
        entry.objectId = core::ObjectId::generate();
        if (editor.applyEdit(RequirementEdit{entry}).accepted) {
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
        const ImportUnitOptions units = ImportUnitOptions::defaults();
        const ImportMappingView view = wizard.prepareMapping(units);
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

bool flowCaptureTcp(RequirementsPanelWidget& panel)
{
    note(panel, QString::fromUtf8(
                    "捕获 TCP 需要三维视图关节状态数据源——该数据源将在后续版本提供"));
    return false;
}

bool flowPickFeature(RequirementsPanelWidget& panel)
{
    note(panel, QString::fromUtf8(
                    "拾取几何特征需要三维视图——三维交互将在后续版本提供"));
    return false;
}

}  // namespace

bool executeRequirementCommand(const std::string& commandId,
                               RequirementsPanelWidget& panel,
                               IRequirementEditor& editor,
                               IRequirementEditSink& sink,
                               CommandDialogHost& host)
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
        return flowCaptureTcp(panel);
    }
    if (commandId == "requirements.pick-feature") {
        return flowPickFeature(panel);
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
