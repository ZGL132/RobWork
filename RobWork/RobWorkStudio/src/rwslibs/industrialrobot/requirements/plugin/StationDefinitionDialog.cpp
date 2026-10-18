/**
 * @file StationDefinitionDialog.cpp
 * @brief 工位完整定义表单（REQ-01/09，units/requirements.md §4.3/§5.3）。
 *
 * 引用型规则需要同时提供kind与载荷，故在局部暂存完整定义，校验后返回
 * 一个值，避免先写入非法规则再补参数。仅装配领域值；引用存在性、几何
 * 解析和坐标变换仍归既有端口。Qt父子所有权，所有操作均限UI线程。
 */
#include "StationDefinitionDialog.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QStandardItemModel> // 缺失目录项可显示但不能新选。
#include <QVBoxLayout>
#include <cmath>
#include <algorithm>
#include <stdexcept>

namespace sdurws::ird::requirements {
namespace {
/** @brief SI数值用17位往返文本，防止只打开确认就改变内容身份。 */
QString number(double value) { return QString::number(value, 'g', 17); }
/** @brief 解析有限数，空/NaN/Inf抛用户输入错误，由表单原位反馈。 */
double finiteNumber(const QString& text)
{
    bool ok = false;
    const double value = text.trimmed().toDouble(&ok);
    if (!ok || !std::isfinite(value)) { throw std::invalid_argument("请输入有限数值"); }
    return value;
}
/** @brief 解析规范对象ID，不猜测名称；跨聚合存在性仍归就绪校验。 */
core::ObjectId objectId(const QString& text)
{
    const auto value = core::ObjectId::tryFromCanonical(text.trimmed().toStdString());
    if (!value || *value == core::ObjectId{}) { throw std::invalid_argument("引用对象不能为空，须提供有效对象标识"); }
    return *value;
}
/** @brief 解析三个有限分量，禁止补零；m/rad及参考系由字段标签声明。 */
rw::math::Vector3D<double> vectorValue(const QString& text)
{
    const auto parts = text.split(',');
    if (parts.size() != 3) { throw std::invalid_argument("请输入逗号分隔的三个分量"); }
    return {finiteNumber(parts[0]), finiteNumber(parts[1]), finiteNumber(parts[2])};
}
/** @brief 原参考系向量无损文本化，不做隐式坐标转换。 */
QString vectorText(const rw::math::Vector3D<double>& value)
{ return number(value[0]) + ", " + number(value[1]) + ", " + number(value[2]); }

/** @brief 引用控件束，借用Qt子控件且限对话框生命周期，不拥有任何跨域对象。 */
struct ReferenceFields {
    QComboBox* kind = nullptr;
    QLineEdit* target = nullptr;
    QLineEdit* tcp = nullptr;
    QComboBox* choice = nullptr; ///< 目录索引只关联值副本，不保存跨域对象指针。
    std::vector<std::optional<RequirementReference>> references; ///< 行号→浅引用值，空行用于未指定。
    std::vector<bool> available; ///< 与引用行一一对应，新不可用目标在确认层也拒绝。
    std::optional<RequirementReference> original; ///< 允许原样保留历史缺失引用，不静默修正。
    bool directoryActive = false; ///< false仅走旧宿主明确降级的兼容输入轨。
    bool optional = false; ///< 仅可选TCP允许真正清空引用。
    /** @brief 确认时组装浅引用；未指定返回nullopt，非法载荷抛invalid_argument。 */
    std::optional<RequirementReference> value() const
    {
        if (directoryActive) {
            const int index = choice->currentIndex();
            if (index < 0 || static_cast<std::size_t>(index) >= references.size()
                || (!references[index] && !optional)) {
                throw std::invalid_argument("请按业务名称选择引用目标");
            }
            if (references[index] && !available[index] && references[index] != original) {
                throw std::invalid_argument("引用当前不可用，请重新选择业务目标");
            }
            return references[index];
        }
        if (kind->currentData().toInt() < 0) { return std::nullopt; }
        RequirementReference result;
        result.kind = static_cast<RequirementRefKind>(kind->currentData().toInt());
        if (result.kind != RequirementRefKind::World && result.kind != RequirementRefKind::DefaultTcp) {
            result.objectId = objectId(target->text());
            if (result.kind == RequirementRefKind::Tool) { result.tcpKey = tcp->text().trimmed().toStdString(); }
        }
        if (!result.wellFormed()) { throw std::invalid_argument("引用不完整：工具引用还须指定TCP键"); }
        return result;
    }
};
/**
 * @brief 建立引用字段，种类范围由使用场景决定；控件归表单Qt父对象。
 * @param form [in,out] 借用行宿主。
 * @param prefix [in] 控件定位前缀，不写入模型。
 * @param kinds [in] 使用处允许的种类。
 * @param base [in] 原浅引用，不做跨域解析。
 * @param optional [in] true时允许“未指定”，用于清空可选TCP。
 * @param choices [in] 名称目录快照，nullptr表示旧装配未提供能力；控件只复制值。
 * @return Qt借用控件束及引用值副本；非法新选在value()确认时抛invalid_argument。
 */
ReferenceFields referenceFields(QFormLayout* form, const QString& prefix,
    std::initializer_list<RequirementRefKind> kinds,
    const std::optional<RequirementReference>& base, bool optional,
    const std::vector<RequirementsReferenceChoice>* choices)
{
    auto* parent = form->parentWidget();
    ReferenceFields fields;
    fields.kind = new QComboBox(parent); fields.target = new QLineEdit(parent);
    fields.tcp = new QLineEdit(parent); fields.choice = new QComboBox(parent);
    fields.optional = optional; fields.original = base; fields.directoryActive = choices != nullptr;
    fields.choice->setObjectName(prefix + "-choice");
    auto* hint = new QLabel(parent); hint->setObjectName(prefix + "-directory-hint");
    hint->setWordWrap(true); form->addRow("业务名称", fields.choice); form->addRow(hint);
    // 缺省目录不冒充空闭包；旧装配保留原输入兼容轨，正式宿主有目录时隐藏它。
    if (!choices) {
        fields.choice->addItem("名称目录未装配"); fields.choice->setEnabled(false);
        hint->setText("宿主名称目录未装配；以下为兼容输入，不能验证目标当前存在性");
    } else {
        fields.choice->addItem(optional ? "未指定" : "请选择目标");
        fields.references.push_back(std::nullopt); fields.available.push_back(true);
        int selected = -1;
        for (const auto& entry : *choices) {
            if (std::find(kinds.begin(), kinds.end(), entry.reference.kind) == kinds.end()) { continue; }
            fields.choice->addItem(QString::fromStdString(entry.displayName)
                + (entry.available ? QString() : QString("（不可用）")));
            fields.references.push_back(entry.reference); fields.available.push_back(entry.available);
            const int row = fields.choice->count()-1;
            fields.choice->setItemData(row, QString::fromStdString(entry.unavailableReason), Qt::ToolTipRole);
            if (!entry.available) {
                if (auto* model = qobject_cast<QStandardItemModel*>(fields.choice->model())) {
                    model->item(row)->setEnabled(false);
                }
            }
            if (base && entry.reference == *base) { selected = row; }
        }
        if (base && selected < 0) {
            // 历史悬空引用仍是原值，打开再确认不得悄悄改成World/首个候选。
            fields.choice->addItem("当前引用（目录中缺失，保留原值）");
            fields.references.push_back(base); fields.available.push_back(false); selected = fields.choice->count()-1;
            if (auto* model = qobject_cast<QStandardItemModel*>(fields.choice->model())) {
                model->item(selected)->setEnabled(false);
            }
        }
        fields.choice->setCurrentIndex(selected < 0 ? 0 : selected);
        hint->setText("按业务名称选择；不可用项保留原因，引用变化不自动换算坐标");
    }
    fields.kind->setObjectName(prefix + "-kind");
    fields.target->setObjectName(prefix + "-object");
    fields.tcp->setObjectName(prefix + "-key");
    if (optional) { fields.kind->addItem("未指定", -1); }
    for (const auto kind : kinds) {
        fields.kind->addItem(QString::fromStdString(std::string(requirementRefKindToken(kind))), static_cast<int>(kind));
    }
    if (base) {
        fields.kind->setCurrentIndex(fields.kind->findData(static_cast<int>(base->kind)));
        if (base->objectId) { fields.target->setText(QString::fromStdString(base->objectId->toCanonical())); }
        fields.tcp->setText(QString::fromStdString(base->tcpKey));
    }
    form->addRow("引用种类", fields.kind); form->addRow("目标对象标识", fields.target);
    form->addRow("TCP键（Tool）", fields.tcp);
    const auto update = [fields] {
        const int kind = fields.kind->currentData().toInt();
        fields.target->setEnabled(kind == static_cast<int>(RequirementRefKind::ModelFrame)
            || kind == static_cast<int>(RequirementRefKind::SceneObject) || kind == static_cast<int>(RequirementRefKind::Tool));
        fields.tcp->setEnabled(kind == static_cast<int>(RequirementRefKind::Tool));
    };
    QObject::connect(fields.kind, &QComboBox::currentIndexChanged, parent, update); update();
    if (choices) {
        // 兼容字段仅供旧装配路径使用；正式名称目录不向用户展示内部ObjectId。
        for (auto* widget : {static_cast<QWidget*>(fields.kind), static_cast<QWidget*>(fields.target), static_cast<QWidget*>(fields.tcp)}) {
            widget->hide(); if (auto* label=form->labelForField(widget)) { label->hide(); }
        }
    }
    return fields;
}
}

std::optional<TaskPoint> editStationDefinition(QWidget* parent, const TaskPoint& base,
                                               const PointSet& points,
    const std::vector<RequirementsReferenceChoice>* choices)
{
    QDialog dialog(parent); dialog.setObjectName("ird_station_definition_dialog");
    dialog.setWindowTitle("编辑工位完整定义"); dialog.resize(600, 720);
    auto* layout = new QVBoxLayout(&dialog);
    auto* scroll = new QScrollArea(&dialog); scroll->setWidgetResizable(true);
    auto* body = new QWidget(scroll); auto* form = new QFormLayout(body);
    scroll->setWidget(body); layout->addWidget(scroll);
    // 文本和枚举保持原类型，不误送数量词表（A-06）。
    const auto textField = [body](QFormLayout* host, const char* key, const QString& label, const QString& text) {
        auto* edit = new QLineEdit(text, body); edit->setObjectName(key); host->addRow(label, edit); return edit;
    };
    auto* name = textField(form, "station-name", "名称", QString::fromStdString(base.name));
    auto* process = new QComboBox(body); process->setObjectName("station-process");
    for (auto tag : {ProcessTag::Generic, ProcessTag::Pick, ProcessTag::Place, ProcessTag::MachineLoad,
        ProcessTag::MachineUnload, ProcessTag::Inspect, ProcessTag::WeldStart, ProcessTag::WeldEnd,
        ProcessTag::ToolChange, ProcessTag::SafeStandby, ProcessTag::Handover}) {
        process->addItem(QString::fromStdString(std::string(processTagToken(tag))), static_cast<int>(tag));
    }
    process->setCurrentIndex(process->findData(static_cast<int>(base.processTag))); form->addRow("工艺标签", process);
    auto* sequence = textField(form, "station-sequence", "前驱工位名（空为未设）", QString::fromStdString(base.sequenceKey.value_or("")));
    auto* note = textField(form, "station-note", "备注", QString::fromStdString(base.note));
    // 四态位置未改时保留整个SourcedValue，包括来源与非法原文；不以零填缺轴。
    const QString positionBefore = base.pose.position.state() == core::FieldState::Provided
        ? vectorText(base.pose.position.value()) : base.pose.position.state() == core::FieldState::NotApplicable
        ? QString("不适用") : base.pose.position.state() == core::FieldState::Invalid
        ? QString::fromStdString(base.pose.position.invalidRawInput()) : QString("未提供");
    auto* position = textField(form, "station-position", "位置x,y,z（m，参考系下）", positionBefore);
    form->addRow(new QLabel("参考坐标系（改变引用不会自动换算数值）", body));
    const auto reference = referenceFields(form, "station-reference", {RequirementRefKind::World,
        RequirementRefKind::ModelFrame, RequirementRefKind::SceneObject}, base.refFrame, false, choices);
    form->addRow(new QLabel("工具/TCP", body));
    const auto tcp = referenceFields(form, "station-tcp", {RequirementRefKind::DefaultTcp, RequirementRefKind::Tool}, base.tcpRef, true, choices);
    auto* rule = new QComboBox(body); rule->setObjectName("station-rule-kind");
    for (auto kind : {OrientationRuleKind::Fixed, OrientationRuleKind::AlignFrame,
        OrientationRuleKind::AlignGeometryNormal, OrientationRuleKind::PointAtTarget, OrientationRuleKind::ToolRollFree}) {
        rule->addItem(QString::fromStdString(std::string(orientationRuleKindToken(kind))));
    }
    form->addRow("姿态规则（参数暂存）", rule);
    auto* stack = new QStackedWidget(body); QFormLayout* pages[5];
    for (auto& page : pages) { auto* widget = new QWidget(stack); page = new QFormLayout(widget); stack->addWidget(widget); }
    auto* rpy = textField(pages[0], "station-rpy", "Roll,Pitch,Yaw（rad，参考系下，Rz·Ry·Rx）", vectorText(base.pose.orientation.fixedRpy));
    // REQ-09的AlignFrame契约只接受模型/场景；World仍可作为上方位置参考系，
    // 但不能出现在此目标列表，否则用户选中后只能得到领域拒绝。
    const auto target = referenceFields(pages[1], "station-target", {RequirementRefKind::ModelFrame,
        RequirementRefKind::SceneObject}, base.pose.orientation.kind == OrientationRuleKind::AlignFrame
            ? std::optional<RequirementReference>(base.pose.orientation.targetFrame) : std::nullopt, false, choices);
    std::optional<RequirementReference> sceneBase;
    if (base.pose.orientation.targetSceneObject) {
        sceneBase = RequirementReference{}; sceneBase->kind = RequirementRefKind::SceneObject;
        sceneBase->objectId = base.pose.orientation.targetSceneObject;
    }
    const auto scene = referenceFields(pages[2], "station-geometry",
        {RequirementRefKind::SceneObject}, sceneBase, false, choices);
    scene.target->setObjectName("station-scene");
    auto* feature = new QComboBox(body); feature->setObjectName("station-feature"); feature->addItems({"FrameOrigin", "FramePlaneNormal"});
    feature->setCurrentIndex(base.pose.orientation.feature == OrientationFeature::FramePlaneNormal ? 1 : 0);
    pages[2]->addRow("几何特征", feature);
    auto* invert = new QCheckBox("法向取反", body); invert->setObjectName("station-invert");
    invert->setChecked(base.pose.orientation.invertNormal); pages[2]->addRow(invert);
    auto* targetPoint = textField(pages[3], "station-target-point", "目标x,y,z（m，参考系下）", vectorText(base.pose.orientation.targetPoint));
    auto* rollMin = textField(pages[4], "station-roll-min", "滚转下限（rad）", number(base.pose.orientation.rollRange.min));
    auto* rollMax = textField(pages[4], "station-roll-max", "滚转上限（rad）", number(base.pose.orientation.rollRange.max));
    form->addRow(stack);
    QObject::connect(rule, &QComboBox::currentIndexChanged, stack, &QStackedWidget::setCurrentIndex);
    rule->setCurrentIndex(static_cast<int>(base.pose.orientation.kind)); stack->setCurrentIndex(rule->currentIndex());
    auto* error = new QLabel(&dialog); error->setObjectName("station-definition-error"); error->setWordWrap(true); layout->addWidget(error);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText("确认写入草稿"); buttons->button(QDialogButtonBox::Cancel)->setText("取消");
    layout->addWidget(buttons); std::optional<TaskPoint> result;
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        try {
            TaskPoint candidate = base;
            candidate.name = name->text().trimmed().toStdString(); candidate.note = note->text().toStdString();
            candidate.processTag = static_cast<ProcessTag>(process->currentData().toInt());
            candidate.sequenceKey = sequence->text().trimmed().isEmpty() ? std::nullopt
                : std::optional<std::string>(sequence->text().trimmed().toStdString());
            if (position->text() != positionBefore) {
                if (position->text().trimmed().isEmpty() || position->text().trimmed() == "未提供") {
                    candidate.pose.position = core::SourcedValue<rw::math::Vector3D<double>>::notProvided();
                } else if (position->text().trimmed() == "不适用") {
                    candidate.pose.position = core::SourcedValue<rw::math::Vector3D<double>>::notApplicable();
                } else {
                    candidate.pose.position = core::SourcedValue<rw::math::Vector3D<double>>::provided(vectorValue(position->text()),
                        core::ValueProvenance::make(core::ProvenanceKind::UserProvided));
                }
            }
            candidate.refFrame = *reference.value(); candidate.tcpRef = tcp.value();
            // 从缺省载荷建立选中规则，避免旧规则的非活动字段泄漏到新kind。
            OrientationRule orientation; orientation.kind = static_cast<OrientationRuleKind>(rule->currentIndex());
            switch (orientation.kind) {
            case OrientationRuleKind::Fixed: orientation.fixedRpy = vectorValue(rpy->text()); break;
            case OrientationRuleKind::AlignFrame: orientation.targetFrame = *target.value(); break;
            case OrientationRuleKind::AlignGeometryNormal:
                orientation.targetSceneObject = scene.value()->objectId;
                orientation.feature = feature->currentIndex() == 0 ? OrientationFeature::FrameOrigin : OrientationFeature::FramePlaneNormal;
                orientation.invertNormal = invert->isChecked(); break;
            case OrientationRuleKind::PointAtTarget: orientation.targetPoint = vectorValue(targetPoint->text()); break;
            case OrientationRuleKind::ToolRollFree: orientation.rollRange = {finiteNumber(rollMin->text()), finiteNumber(rollMax->text())}; break;
            }
            candidate.pose.orientation = orientation;
            if (const auto invalid = validateTaskPoint(candidate)) { throw std::invalid_argument(invalid->detail); }
            for (const auto& other : points.entries) {
                if (other.objectId != base.objectId && other.name == candidate.name) { throw std::invalid_argument("名称已存在，请使用不同名称"); }
            }
            result = std::move(candidate); dialog.accept();
        } catch (const std::invalid_argument& failure) { error->setText(QString::fromUtf8(failure.what())); }
    });
    if (dialog.exec() != QDialog::Accepted) { return std::nullopt; }
    return result;
}
}
