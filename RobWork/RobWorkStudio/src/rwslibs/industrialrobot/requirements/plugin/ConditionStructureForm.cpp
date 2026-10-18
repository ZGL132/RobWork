/**
 * @file ConditionStructureForm.cpp
 * @brief REQ-04工况结构表单实现，WP-14-T12；只做解析和装配，业务裁决归域。
 *
 * 暂存控件由Qt父对象持有。应用回调可能触发外部刷新，宿主必须排队执行
 * 写入，防止销毁当前发信按钮。未变化的SourcedValue原样保留来源和四态。
 */
#include "ConditionStructureForm.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <cmath>
#include <algorithm>
#include <stdexcept>

namespace sdurws::ird::requirements {
namespace {
/** @brief SI数值的往返文本，17位有效数字避免仅打开确认就舍入模型。 */
QString number(double value) { return QString::number(value, 'g', 17); }

/** @brief 读取有限数值；空/非数字/NaN/Inf拒绝，位置由调用者补充。 */
double finiteNumber(const QString& text)
{
    bool ok = false;
    const double value = text.trimmed().toDouble(&ok);
    if (!ok || !std::isfinite(value)) {
        throw std::invalid_argument("请输入有限数值，缺失请使用“未提供”");
    }
    return value;
}

/** @brief 向量按工具TCP系三个m分量显示，以逗号分隔，不改变坐标系。 */
QString vectorText(const rw::math::Vector3D<double>& value)
{
    return number(value[0]) + ", " + number(value[1]) + ", " + number(value[2]);
}

/** @brief 四态直投；非Provided绝不调用value，非法态原文用于后续修正。 */
template<class T, class Format>
QString sourcedText(const core::SourcedValue<T>& value, Format format)
{
    switch (value.state()) {
    case core::FieldState::Provided: return format(value.value());
    case core::FieldState::NotProvided: return QStringLiteral("未提供");
    case core::FieldState::NotApplicable: return QStringLiteral("不适用");
    case core::FieldState::Invalid:
        return QStringLiteral("无效：") + QString::fromStdString(value.invalidRawInput());
    }
    throw std::logic_error("工况表单：未知物性状态");
}

/**
 * @brief 暂存文本转四态；同文本返回原值，以保留原provenance，不伪造新来源。
 * @param text [in] 用户文本（SI，格式由列头声明）。
 * @param base [in] 原物性，新增行使用NotProvided。
 * @param format [in] 对应的确定性文本化函数。
 * @param parse [in] 对应的标量/向量严格解析器；非法输入抛invalid_argument。
 */
template<class T, class Format, class Parse>
core::SourcedValue<T> parseSourced(const QString& text, const core::SourcedValue<T>& base,
                                  Format format, Parse parse)
{
    if (text == sourcedText(base, format)) { return base; }
    if (text.trimmed().isEmpty() || text.trimmed() == QStringLiteral("未提供")) {
        return core::SourcedValue<T>::notProvided();
    }
    if (text.trimmed() == QStringLiteral("不适用")) {
        return core::SourcedValue<T>::notApplicable();
    }
    return core::SourcedValue<T>::provided(parse(text),
        core::ValueProvenance::make(core::ProvenanceKind::UserProvided));
}

/** @brief 仅接受三个有限m分量，禁止缺一项补零。 */
rw::math::Vector3D<double> parseVector(const QString& text)
{
    const auto parts = text.split(',');
    if (parts.size() != 3) { throw std::invalid_argument("质心须为x, y, z三个分量（m）"); }
    return {finiteNumber(parts[0]), finiteNumber(parts[1]), finiteNumber(parts[2])};
}

/** @brief 建立可编辑表格；列头带单位，控件由父对象持有。 */
QTableWidget* table(QWidget* parent, const char* name, const QStringList& headers)
{
    auto* result = new QTableWidget(0, headers.size(), parent);
    result->setObjectName(name);
    result->setHorizontalHeaderLabels(headers);
    result->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    result->setMinimumHeight(120);
    return result;
}

/** @brief 读取表格单元格文本；缺单元格表示实现缺陷，不能静默当空。 */
QString cell(QTableWidget* table, int row, int column)
{
    if (!table->item(row, column)) { throw std::logic_error("工况表单：单元格缺位"); }
    return table->item(row, column)->text();
}

/** @brief 浅引用行投影；类型固定词表，无载荷引用不填假标识。 */
void referenceRow(QTableWidget* table, int row, const RequirementReference& reference)
{
    auto* kind = new QComboBox(table);
    for (auto k : {RequirementRefKind::DefaultTcp, RequirementRefKind::Tool,
                    RequirementRefKind::World, RequirementRefKind::ModelFrame,
                    RequirementRefKind::SceneObject}) {
        kind->addItem(QString::fromStdString(std::string(requirementRefKindToken(k))), int(k));
    }
    kind->setCurrentIndex(kind->findData(int(reference.kind)));
    table->setCellWidget(row, 0, kind);
    table->setItem(row, 1, new QTableWidgetItem(reference.objectId
        ? QString::fromStdString(reference.objectId->toCanonical()) : QString()));
    table->setItem(row, 2, new QTableWidgetItem(QString::fromStdString(reference.tcpKey)));
}

/** @brief 解析浅引用，不裁剪用户提供的多余载荷；结构合法性归wellFormed域函数。 */
RequirementReference readReference(QTableWidget* table, int row)
{
    RequirementReference reference;
    reference.kind = RequirementRefKind(qobject_cast<QComboBox*>(table->cellWidget(row, 0))->currentData().toInt());
    const auto id = cell(table, row, 1).trimmed();
    if (!id.isEmpty()) {
        reference.objectId = core::ObjectId::fromCanonical(id.toStdString());
    }
    reference.tcpKey = cell(table, row, 2).toStdString();
    if (!reference.wellFormed()) { throw std::invalid_argument("工具引用的类别、对象标识与TCP键不匹配"); }
    return reference;
}

/** @brief 值集合格式化为逗号分隔的规范标识；保持输入序，不重排。 */
QString idsText(const std::vector<core::ObjectId>& ids)
{
    QStringList parts;
    for (const auto& id : ids) { parts << QString::fromStdString(id.toCanonical()); }
    return parts.join(", ");
}
}

ConditionStructureForm::ConditionStructureForm(const OperatingCondition& base,
    const std::vector<TaskPoint>& points, bool writable,
    std::function<void(OperatingCondition)> submit, QWidget* parent)
    : QWidget(parent), m_base(base), m_points(points), m_submit(std::move(submit))
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* help = new QLabel(QStringLiteral("结构编辑暂存于表单；确认后一次写入草稿。物性可填“未提供”或“不适用”，零表示已提供的零值。质心在工具TCP坐标系下。"), this);
    help->setWordWrap(true);
    layout->addWidget(help);
    m_payloads = table(this, "ird_condition_payloads", {"工具类别", "对象标识", "TCP键", "质量 (kg)", "质心 x,y,z (m)", "惯量 (kg·m²)"});
    m_events = table(this, "ird_condition_events", {"事件", "绑定工位", "时长 (s，空=未设)"});
    m_tools = table(this, "ird_condition_tools", {"工具类别", "对象标识", "TCP键"});
    // 增删只操作暂存表；删除按钮不是持久化对象删除，不触发项目写路径。
    auto group = [this, layout, writable](const QString& title, QTableWidget* values,
        const char* addName, const char* removeName, std::function<void()> add) {
        layout->addWidget(new QLabel(title, this));
        layout->addWidget(values);
        auto* buttons = new QHBoxLayout;
        auto* plus = new QPushButton(QStringLiteral("新增"), this);
        plus->setObjectName(addName);
        auto* minus = new QPushButton(QStringLiteral("删除选中行"), this);
        minus->setObjectName(removeName);
        buttons->addWidget(plus); buttons->addWidget(minus); buttons->addStretch();
        layout->addLayout(buttons);
        plus->setEnabled(writable); minus->setEnabled(writable);
        values->setEnabled(writable);
        connect(plus, &QPushButton::clicked, this, std::move(add));
        connect(minus, &QPushButton::clicked, this, [values] {
            if (values->currentRow() >= 0) { values->removeRow(values->currentRow()); }
        });
    };
    group("负载", m_payloads, "ird_condition_add_payload", "ird_condition_remove_payload", [this] {
        ConditionPayload p; p.toolRef.kind = RequirementRefKind::DefaultTcp; addPayload(p);
    });
    group("夹取 / 释放 / 驻留", m_events, "ird_condition_add_event", "ird_condition_remove_event", [this] {
        ConditionEvent e;
        if (!m_points.empty()) { e.stationRef = m_points.front().objectId; }
        addEvent(e);
    });
    group("工具引用", m_tools, "ird_condition_add_tool", "ird_condition_remove_tool", [this] {
        RequirementReference ref; ref.kind = RequirementRefKind::DefaultTcp; addTool(ref);
    });
    auto* form = new QFormLayout;
    m_environment = new QLineEdit(this); m_environment->setObjectName("ird_condition_environment");
    m_collision = new QCheckBox(QStringLiteral("要求无碰撞"), this); m_collision->setObjectName("ird_condition_collision");
    m_margin = new QLineEdit(this); m_margin->setObjectName("ird_condition_margin");
    m_scope = new QComboBox(this); m_scope->setObjectName("ird_condition_scope");
    m_scope->addItem("全部工位", int(AppliesToScope::AllStations));
    m_scope->addItem("所选工位", int(AppliesToScope::Stations));
    m_scope->addItem("不适用", int(AppliesToScope::None));
    m_stations = new QListWidget(this); m_stations->setObjectName("ird_condition_stations");
    form->addRow("环境对象标识（逗号分隔）", m_environment);
    form->addRow("碰撞要求", m_collision);
    form->addRow("最小关节裕量（rad/m，空=未设）", m_margin);
    form->addRow("适用范围", m_scope);
    form->addRow("适用工位", m_stations);
    layout->addLayout(form);
    for (auto* field : {static_cast<QWidget*>(m_environment), static_cast<QWidget*>(m_collision),
                       static_cast<QWidget*>(m_margin), static_cast<QWidget*>(m_scope),
                       static_cast<QWidget*>(m_stations)}) { field->setEnabled(writable); }
    m_error = new QLabel(this); m_error->setObjectName("ird_condition_structure_error");
    m_error->setWordWrap(true); layout->addWidget(m_error);
    auto* apply = new QPushButton("确认结构修改", this); apply->setObjectName("ird_condition_structure_apply");
    auto* cancel = new QPushButton("取消结构修改", this); cancel->setObjectName("ird_condition_structure_cancel");
    auto* actions = new QHBoxLayout; actions->addWidget(apply); actions->addWidget(cancel); layout->addLayout(actions);
    apply->setEnabled(writable); cancel->setEnabled(writable);
    connect(apply, &QPushButton::clicked, this, [this] { this->apply(); });
    connect(cancel, &QPushButton::clicked, this, [this] { reset(); });
    reset();
}

void ConditionStructureForm::addPayload(const ConditionPayload& payload, int origin)
{
    const int row = m_payloads->rowCount(); m_payloads->insertRow(row);
    referenceRow(m_payloads, row, payload.toolRef);
    m_payloads->item(row, 1)->setData(Qt::UserRole, origin);
    m_payloads->setItem(row, 3, new QTableWidgetItem(sourcedText(payload.mass, number)));
    m_payloads->setItem(row, 4, new QTableWidgetItem(sourcedText(payload.com, vectorText)));
    m_payloads->setItem(row, 5, new QTableWidgetItem(sourcedText(payload.inertia, number)));
}

void ConditionStructureForm::addTool(const RequirementReference& reference)
{
    const int row = m_tools->rowCount(); m_tools->insertRow(row);
    referenceRow(m_tools, row, reference);
}

void ConditionStructureForm::addEvent(const ConditionEvent& event)
{
    const int row = m_events->rowCount(); m_events->insertRow(row);
    auto* type = new QComboBox(m_events);
    for (auto kind : {ConditionEventType::Grasp, ConditionEventType::Release, ConditionEventType::Dwell}) {
        type->addItem(QString::fromStdString(std::string(conditionEventTypeToken(kind))), int(kind));
    }
    type->setCurrentIndex(type->findData(int(event.type))); m_events->setCellWidget(row, 0, type);
    auto* station = new QComboBox(m_events); station->addItem("未选择工位", QString());
    for (const auto& point : m_points) {
        station->addItem(QString::fromStdString(point.name), QString::fromStdString(point.objectId.toCanonical()));
    }
    const auto id = event.stationRef.isValid() ? QString::fromStdString(event.stationRef.toCanonical()) : QString();
    int index = station->findData(id);
    // 悬空引用必须可见，不能自动选择另一个工位造成错误绑定。
    if (index < 0) { station->addItem("目标已缺失：" + id, id); index = station->count() - 1; }
    station->setCurrentIndex(index); m_events->setCellWidget(row, 1, station);
    m_events->setItem(row, 2, new QTableWidgetItem(event.durationS ? number(*event.durationS) : QString()));
}

void ConditionStructureForm::reset()
{
    m_payloads->setRowCount(0); m_events->setRowCount(0); m_tools->setRowCount(0);
    for (std::size_t i = 0; i < m_base.payloads.size(); ++i) { addPayload(m_base.payloads[i], int(i)); }
    for (const auto& event : m_base.events) { addEvent(event); }
    for (const auto& tool : m_base.toolRefs) { addTool(tool); }
    m_environment->setText(idsText(m_base.environmentRefs));
    m_collision->setChecked(m_base.demands.collisionFreeRequired);
    m_margin->setText(m_base.demands.minimumJointMargin ? number(*m_base.demands.minimumJointMargin) : QString());
    m_scope->setCurrentIndex(m_scope->findData(int(m_base.appliesTo.scope)));
    m_stations->clear();
    auto addStation = [this](const core::ObjectId& id, const QString& name, bool selected) {
        auto* item = new QListWidgetItem(name, m_stations);
        item->setData(Qt::UserRole, QString::fromStdString(id.toCanonical()));
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(selected ? Qt::Checked : Qt::Unchecked);
    };
    for (const auto& point : m_points) {
        addStation(point.objectId, QString::fromStdString(point.name),
            std::find(m_base.appliesTo.stations.begin(), m_base.appliesTo.stations.end(), point.objectId) != m_base.appliesTo.stations.end());
    }
    for (const auto& id : m_base.appliesTo.stations) {
        if (std::none_of(m_points.begin(), m_points.end(), [&id](const TaskPoint& point) { return point.objectId == id; })) {
            addStation(id, "目标已缺失：" + QString::fromStdString(id.toCanonical()), true);
        }
    }
    m_error->clear();
}

void ConditionStructureForm::apply()
{
    OperatingCondition candidate = m_base;
    try {
        candidate.payloads.clear(); candidate.events.clear(); candidate.toolRefs.clear();
        for (int row = 0; row < m_payloads->rowCount(); ++row) {
            const int origin = m_payloads->item(row, 1)->data(Qt::UserRole).toInt();
            ConditionPayload payload = origin >= 0 ? m_base.payloads.at(std::size_t(origin)) : ConditionPayload{};
            payload.toolRef = readReference(m_payloads, row);
            payload.mass = parseSourced(cell(m_payloads, row, 3), payload.mass, number, finiteNumber);
            payload.com = parseSourced(cell(m_payloads, row, 4), payload.com, vectorText, parseVector);
            payload.inertia = parseSourced(cell(m_payloads, row, 5), payload.inertia, number, finiteNumber);
            candidate.payloads.push_back(std::move(payload));
        }
        for (int row = 0; row < m_tools->rowCount(); ++row) { candidate.toolRefs.push_back(readReference(m_tools, row)); }
        for (int row = 0; row < m_events->rowCount(); ++row) {
            ConditionEvent event;
            event.type = ConditionEventType(qobject_cast<QComboBox*>(m_events->cellWidget(row, 0))->currentData().toInt());
            const auto station = qobject_cast<QComboBox*>(m_events->cellWidget(row, 1))->currentData().toString();
            if (!station.isEmpty()) { event.stationRef = core::ObjectId::fromCanonical(station.toStdString()); }
            if (!cell(m_events, row, 2).trimmed().isEmpty()) { event.durationS = finiteNumber(cell(m_events, row, 2)); }
            candidate.events.push_back(event);
        }
        candidate.environmentRefs.clear();
        for (const auto& id : m_environment->text().split(',', Qt::SkipEmptyParts)) {
            candidate.environmentRefs.push_back(core::ObjectId::fromCanonical(id.trimmed().toStdString()));
        }
        candidate.demands.collisionFreeRequired = m_collision->isChecked();
        candidate.demands.minimumJointMargin.reset();
        if (!m_margin->text().trimmed().isEmpty()) {
            const double value = finiteNumber(m_margin->text());
            if (value < 0) { throw std::invalid_argument("最小关节裕量不可为负（rad/m）"); }
            candidate.demands.minimumJointMargin = value;
        }
        candidate.appliesTo.scope = AppliesToScope(m_scope->currentData().toInt());
        candidate.appliesTo.stations.clear();
        if (candidate.appliesTo.scope == AppliesToScope::Stations) {
            for (int i = 0; i < m_stations->count(); ++i) {
                const auto* item = m_stations->item(i);
                if (item->checkState() == Qt::Checked) {
                    candidate.appliesTo.stations.push_back(core::ObjectId::fromCanonical(item->data(Qt::UserRole).toString().toStdString()));
                }
            }
        }
        if (const auto error = validateOperatingCondition(candidate)) {
            m_error->setText(QString::fromStdString(error->detail)); return;
        }
    } catch (const std::exception& error) {
        // 正常用户解析失败只改变就地提示；草稿、撤销栈和未确认输入全部保留。
        m_error->setText(QStringLiteral("结构输入无效：") + QString::fromUtf8(error.what())); return;
    }
    m_error->clear();
    if (candidate != m_base && m_submit) { m_submit(std::move(candidate)); }
}
}
