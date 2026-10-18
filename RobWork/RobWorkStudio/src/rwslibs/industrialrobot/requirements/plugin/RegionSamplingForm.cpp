/**
 * @file RegionSamplingForm.cpp
 * @brief REQ-03完整采样暂存实现；WP-14-T12，单元卡§5.2/§9.8。
 *
 * 数值全部SI，计数严格解析，域不变量只经validateWorkRegion裁决。
 * 不复制GridBySpacing规范化、不生成随机样本；确认后回传值，零持久化写入。
 */
#include "RegionSamplingForm.hpp"
#include "ReferenceChoiceBox.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace sdurws::ird::requirements {
namespace {
/// @brief SI数值17位往返；仅打开确认不能改变原浮点字面。
QString number(double value) { return QString::number(value, 'g', 17); }
/**
 * @brief 解析纯十进制非负计数，禁止负数/小数/溢出转成另一合法值。
 * @param text [in] 用户文本；空串拒绝，不把缺失伪造为零。
 * @return uint32计数；词法或范围错误抛invalid_argument供表单就地呈现。
 */
std::uint32_t count(const QString& text)
{
    const auto input = text.trimmed();
    if (input.isEmpty()) { throw std::invalid_argument("计数不能为空"); }
    for (const auto ch : input) {
        if (ch < QLatin1Char('0') || ch > QLatin1Char('9')) {
            throw std::invalid_argument("计数须为非负十进制整数");
        }
    }
    bool ok = false;
    const auto value = input.toULongLong(&ok);
    if (!ok || value > std::numeric_limits<std::uint32_t>::max()) {
        throw std::invalid_argument("计数超出uint32范围（0~4294967295）");
    }
    return static_cast<std::uint32_t>(value);
}
/// @brief 有限SI数值解析；用户错误抛invalid_argument，不吞NaN或缺失。
double finiteNumber(const QString& text)
{
    bool ok = false;
    const double value = text.trimmed().toDouble(&ok);
    if (!ok || !std::isfinite(value)) { throw std::invalid_argument("请输入有限数值"); }
    return value;
}
}

RegionSamplingForm::RegionSamplingForm(const WorkRegion& base, bool writable,
    std::function<void(WorkRegion)> submit, QWidget* parent,
    const std::vector<RequirementsReferenceChoice>* choices)
    : QWidget(parent), m_base(base), m_submit(std::move(submit))
{
    setObjectName("ird_region_sampling_form");
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* help = new QLabel(QStringLiteral("区域定义与采样：修改先暂存，确认后整批写入草稿。间距在区域参考系下，单位m；覆盖率填写0~1。随机种子与预算在分析配置中设置。"), this);
    help->setWordWrap(true); layout->addWidget(help);
    auto* fields = new QFormLayout; m_fields = fields;
    layout->addLayout(fields);
    // 每个字段单独带明确标签与定位名；计数用文本避免QSpinBox的有符号上限截断。
    const auto line = [this, fields](const QString& label, const QString& name) {
        auto* edit = new QLineEdit(this); edit->setObjectName(name);
        fields->addRow(label, edit); return edit;
    };
    m_name = line("区域名称", "ird_region_definition_name");
    if (choices) {
        m_reference=new ReferenceChoiceBox(*choices,{RequirementRefKind::World,RequirementRefKind::ModelFrame,
            RequirementRefKind::SceneObject},base.refFrame,false,this);
        m_reference->setObjectName("ird_region_reference_choice"); fields->addRow("参考坐标系",m_reference);
        m_tcp=new ReferenceChoiceBox(*choices,{RequirementRefKind::Tool,RequirementRefKind::DefaultTcp},base.tcpRef,true,this);
        m_tcp->setObjectName("ird_region_tcp_choice"); fields->addRow("工具 / TCP",m_tcp);
    }
    auto* referenceHint=new QLabel(choices ? "按业务名选择引用；变更参考系不自动换算盒坐标"
        : "宿主名称目录未装配；区域参考系和TCP保留原值",this);
    referenceHint->setWordWrap(true); fields->addRow(referenceHint);
    m_method = new QComboBox(this); m_method->setObjectName("ird_region_sampling_method");
    m_method->addItem("网格计数 (Grid)", int(PositionSamplingMethod::Grid));
    m_method->addItem("网格间距 (GridBySpacing)", int(PositionSamplingMethod::GridBySpacing));
    m_method->addItem("随机计数 (Random)", int(PositionSamplingMethod::Random));
    fields->addRow("位置采样方法", m_method);
    for (int axis = 0; axis < 3; ++axis) {
        const auto label = QString::fromLatin1("XYZ").mid(axis, 1);
        m_counts[axis] = line("网格计数 " + label, QString("ird_region_grid_count_%1").arg(axis));
        m_spacing[axis] = line("网格间距 " + label + " (m)", QString("ird_region_spacing_%1").arg(axis));
    }
    m_random = line("随机样本总数", "ird_region_random_count");
    m_direction = line("姿态方向采样数（≥1）", "ird_region_direction_samples");
    m_roll = line("姿态滚转采样数（≥1）", "ird_region_roll_samples");
    m_token = line("姿态采样方法token（原样传递）", "ird_region_orientation_method");
    m_positionCoverage = line("位置覆盖率下限（0~1）", "ird_region_position_coverage");
    m_hasOrientationCoverage = new QCheckBox("设置姿态覆盖率下限", this);
    m_hasOrientationCoverage->setObjectName("ird_region_orientation_coverage_enabled");
    fields->addRow(m_hasOrientationCoverage);
    m_orientationCoverage = line("姿态覆盖率下限（0~1）", "ird_region_orientation_coverage");
    m_collision = new QCheckBox("要求无碰撞", this); m_collision->setObjectName("ird_region_collision");
    fields->addRow(m_collision);
    m_margin = line("最小关节裕量（rad/m，空=未设）", "ird_region_margin");
    m_sequence = line("前驱区域名称（空=未设）", "ird_region_definition_sequence");
    m_note = line("备注", "ird_region_definition_note");
    m_error = new QLabel(this); m_error->setObjectName("ird_region_sampling_error");
    m_error->setWordWrap(true); layout->addWidget(m_error);
    auto* buttons = new QHBoxLayout;
    auto* applyButton = new QPushButton("确认区域定义与采样", this);
    applyButton->setObjectName("ird_region_sampling_apply");
    auto* cancel = new QPushButton("取消暂存修改", this);
    cancel->setObjectName("ird_region_sampling_cancel");
    buttons->addWidget(applyButton); buttons->addWidget(cancel); layout->addLayout(buttons);
    connect(m_method, &QComboBox::currentIndexChanged, this, [this] { updateMethodVisibility(); });
    connect(m_hasOrientationCoverage, &QCheckBox::toggled, m_orientationCoverage, &QWidget::setEnabled);
    connect(applyButton, &QPushButton::clicked, this, [this] { apply(); });
    connect(cancel, &QPushButton::clicked, this, [this] { reset(); });
    reset();
    // 父级禁用统一覆盖所有编辑入口；避免只读状态下仍能通过未显式列出的字段修改。
    setEnabled(writable);
}

void RegionSamplingForm::reset()
{
    m_name->setText(QString::fromStdString(m_base.name));
    if (m_reference) { m_reference->resetSelection(); m_tcp->resetSelection(); }
    m_method->setCurrentIndex(m_method->findData(int(m_base.positionSampling.method)));
    for (int axis = 0; axis < 3; ++axis) {
        m_counts[axis]->setText(QString::number(m_base.positionSampling.counts[axis]));
        m_spacing[axis]->setText(number(m_base.positionSampling.spacing[axis]));
    }
    m_random->setText(QString::number(m_base.positionSampling.count));
    m_direction->setText(QString::number(m_base.orientationSampling.directionSamples));
    m_roll->setText(QString::number(m_base.orientationSampling.rollSamples));
    m_token->setText(QString::fromStdString(m_base.orientationSampling.methodToken));
    m_positionCoverage->setText(number(m_base.coverageTargets.minPositionCoverage));
    m_hasOrientationCoverage->setChecked(m_base.coverageTargets.minOrientationCoverage.has_value());
    m_orientationCoverage->setText(m_base.coverageTargets.minOrientationCoverage
        ? number(*m_base.coverageTargets.minOrientationCoverage) : QString());
    m_orientationCoverage->setEnabled(m_hasOrientationCoverage->isChecked());
    m_collision->setChecked(m_base.demands.collisionFreeRequired);
    m_margin->setText(m_base.demands.minimumJointMargin ? number(*m_base.demands.minimumJointMargin) : QString());
    m_sequence->setText(m_base.sequenceKey ? QString::fromStdString(*m_base.sequenceKey) : QString());
    m_note->setText(QString::fromStdString(m_base.note));
    m_error->clear(); updateMethodVisibility();
}

void RegionSamplingForm::updateMethodVisibility()
{
    const auto method = PositionSamplingMethod(m_method->currentData().toInt());
    auto* fields = m_fields;
    // 标签与字段一起显隐，隐藏载荷保留原输入；确认只解析活动方法，旧零间距不阻塞Grid。
    for (int axis = 0; axis < 3; ++axis) {
        fields->setRowVisible(m_counts[axis], method == PositionSamplingMethod::Grid);
        fields->setRowVisible(m_spacing[axis], method == PositionSamplingMethod::GridBySpacing);
    }
    fields->setRowVisible(m_random, method == PositionSamplingMethod::Random);
}

void RegionSamplingForm::apply()
{
    if (!isEnabled()) { return; } // 防御直接槽调用，禁写不只依赖按钮灰显。
    WorkRegion candidate = m_base;
    try {
        if (m_reference) { candidate.refFrame=*m_reference->reference(); candidate.tcpRef=m_tcp->reference(); }
        candidate.name = m_name->text().toStdString();
        if (m_name->text().trimmed().isEmpty()) { throw std::invalid_argument("区域名称不能为空"); }
        candidate.positionSampling.method = PositionSamplingMethod(m_method->currentData().toInt());
        if (candidate.positionSampling.method == PositionSamplingMethod::Grid) {
            for (int axis = 0; axis < 3; ++axis) { candidate.positionSampling.counts[axis] = count(m_counts[axis]->text()); }
        } else if (candidate.positionSampling.method == PositionSamplingMethod::GridBySpacing) {
            for (int axis = 0; axis < 3; ++axis) { candidate.positionSampling.spacing[axis] = finiteNumber(m_spacing[axis]->text()); }
        } else {
            candidate.positionSampling.count = count(m_random->text());
        }
        candidate.orientationSampling.directionSamples = count(m_direction->text());
        candidate.orientationSampling.rollSamples = count(m_roll->text());
        candidate.orientationSampling.methodToken = m_token->text().toStdString();
        candidate.coverageTargets.minPositionCoverage = finiteNumber(m_positionCoverage->text());
        candidate.coverageTargets.minOrientationCoverage = m_hasOrientationCoverage->isChecked()
            ? std::optional<double>{finiteNumber(m_orientationCoverage->text())} : std::nullopt;
        candidate.demands.collisionFreeRequired = m_collision->isChecked();
        candidate.demands.minimumJointMargin = m_margin->text().trimmed().isEmpty()
            ? std::nullopt : std::optional<double>{finiteNumber(m_margin->text())};
        candidate.sequenceKey = m_sequence->text().isEmpty() ? std::nullopt
            : std::optional<std::string>{m_sequence->text().toStdString()};
        candidate.note = m_note->text().toStdString();
        if (const auto error = validateWorkRegion(candidate)) {
            m_error->setText(QString::fromStdString(error->detail)); return;
        }
    } catch (const std::invalid_argument& error) {
        m_error->setText(QStringLiteral("区域输入无效：") + QString::fromUtf8(error.what())); return;
    }
    m_error->clear();
    if (candidate != m_base && m_submit) { m_submit(std::move(candidate)); }
}
}
