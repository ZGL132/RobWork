// =============================================================================
//  KinematicThresholdsDialog 的实现
// =============================================================================
//
// 布局结构:
//   QFormLayout 表单:8 个阈值输入框(近限位比值/条件数×2/奇异值/可操作度/
//                    位置容差/姿态容差/IK 去重阈值)
//   + 校验提示标签(_validationLabel)
//   + OK / Cancel 按钮。
//
// 单位换算仅在两个容差字段进行(与 KinematicThresholdsDialog.hpp 头部约定一致):
//   - positionTolerance    :SI 米  <-> 显示长度单位;
//   - orientationTolerance :SI 度   <-> 显示角度单位。
//
// 联动校验:
//   两个条件数输入框任一数值变化都会触发 validate(),实时检查
//   conditionWarning < conditionFail,错误提示即时刷新在 _validationLabel。
#include "KinematicThresholdsDialog.hpp"

#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QVBoxLayout>

#include <cmath>

using namespace rws;

namespace {

// 创建并配置一个 QDoubleSpinBox 的工厂辅助函数。
//   - setRange / setDecimals:按字段语义设置上下限与精度(条件数/可操作度
//     需要较多小数位);
//   - setKeyboardTracking(false):关闭逐键校验回调,避免用户编辑中间值
//     (如清空输入、暂态值)时过早触发 valueChanged,减少多余校验。
QDoubleSpinBox* makeSpin (QWidget* parent, const QString& objectName,
                          double minimum, double maximum, int decimals)
{
    QDoubleSpinBox* spin = new QDoubleSpinBox (parent);
    spin->setObjectName (objectName);
    spin->setRange (minimum, maximum);
    spin->setDecimals (decimals);
    spin->setKeyboardTracking (false);
    return spin;
}

}    // namespace

// 构造函数:创建全部输入框、校验标签与按钮,连接校验信号。
// 两个容差输入框的上下限与后缀由单位换算决定:
//   - 位置容差上限 = 1e6 米换算为显示长度单位(并附加长度单位后缀);
//   - 姿态容差上限 = 360 度换算为显示角度单位(并附加角度单位后缀)。
// 构造末尾调用 setThresholds 把传入的初始快照写入输入框并触发校验。
KinematicThresholdsDialog::KinematicThresholdsDialog (
    const KinematicThresholds& thresholds,
    KinematicLengthUnit lengthUnit,
    KinematicAngleUnit angleUnit,
    QWidget* parent) :
    QDialog (parent),
    _lengthUnit (lengthUnit),
    _angleUnit (angleUnit),
    _nearJointLimitRatioSpin (nullptr),
    _conditionWarningSpin (nullptr),
    _conditionFailSpin (nullptr),
    _singularValueWarningSpin (nullptr),
    _manipulabilityWarningSpin (nullptr),
    _positionToleranceSpin (nullptr),
    _orientationToleranceSpin (nullptr),
    _ikDuplicateQThresholdSpin (nullptr),
    _validationLabel (nullptr)
{
    setModal (true);
    setWindowTitle (tr ("Kinematic thresholds"));

    QVBoxLayout* root = new QVBoxLayout (this);
    QFormLayout* form = new QFormLayout ();
    form->setFieldGrowthPolicy (QFormLayout::AllNonFixedFieldsGrow);

    _nearJointLimitRatioSpin = makeSpin (
        this, QStringLiteral ("nearJointLimitRatioSpin"), 0.0, 1.0, 6);
    _conditionWarningSpin = makeSpin (
        this, QStringLiteral ("conditionWarningSpin"), 0.0, 1e15, 6);
    _conditionFailSpin = makeSpin (
        this, QStringLiteral ("conditionFailSpin"), 0.0, 1e15, 6);
    _singularValueWarningSpin = makeSpin (
        this, QStringLiteral ("singularValueWarningSpin"), 0.0, 1e15, 12);
    _manipulabilityWarningSpin = makeSpin (
        this, QStringLiteral ("manipulabilityWarningSpin"), 0.0, 1e15, 12);
    // 位置容差:上限由 1e6 米换算为当前长度单位,并附加单位后缀(mm/in 等),
    // 使输入框数值直接以用户选择的单位显示。
    // 姿态容差:上限由 360 度换算为当前角度单位,并附加单位后缀(deg/rad 等)。
    _positionToleranceSpin = makeSpin (
        this, QStringLiteral ("positionToleranceSpin"), 0.0,
        displayLengthFromMeters (1e6, _lengthUnit), 9);
    _positionToleranceSpin->setSuffix (QStringLiteral (" ") +
                                       QString::fromLatin1 (unitSuffix (_lengthUnit)));
    _orientationToleranceSpin = makeSpin (
        this, QStringLiteral ("orientationToleranceSpin"), 0.0,
        displayAngleFromDegrees (360.0, _angleUnit), 9);
    _orientationToleranceSpin->setSuffix (QStringLiteral (" ") +
                                          QString::fromLatin1 (unitSuffix (_angleUnit)));
    _ikDuplicateQThresholdSpin = makeSpin (
        this, QStringLiteral ("ikDuplicateQThresholdSpin"), 0.0, 1e15, 12);

    form->addRow (tr ("Near joint-limit ratio:"), _nearJointLimitRatioSpin);
    form->addRow (tr ("Condition warning:"), _conditionWarningSpin);
    form->addRow (tr ("Condition fail:"), _conditionFailSpin);
    form->addRow (tr ("Singular value warning:"), _singularValueWarningSpin);
    form->addRow (tr ("Manipulability warning:"), _manipulabilityWarningSpin);
    form->addRow (tr ("Position tolerance:"), _positionToleranceSpin);
    form->addRow (tr ("Orientation tolerance:"), _orientationToleranceSpin);
    form->addRow (tr ("IK duplicate Q threshold:"), _ikDuplicateQThresholdSpin);
    root->addLayout (form);

    // 校验提示标签:默认隐藏,仅在校验失败时显示(由 validate 控制显隐)。
    _validationLabel = new QLabel (this);
    _validationLabel->setObjectName (QStringLiteral ("validationLabel"));
    _validationLabel->setWordWrap (true);
    _validationLabel->setVisible (false);
    root->addWidget (_validationLabel);

    QDialogButtonBox* buttons = new QDialogButtonBox (
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->setObjectName (QStringLiteral ("thresholdDialogButtons"));
    root->addWidget (buttons);
    connect (buttons, &QDialogButtonBox::accepted,
             this, &KinematicThresholdsDialog::accept);
    connect (buttons, &QDialogButtonBox::rejected,
             this, &KinematicThresholdsDialog::reject);
    // 两个条件数输入框任一数值变化即触发 validate(),
    // 实时刷新 "conditionWarning < conditionFail" 的一致性提示。
    connect (_conditionWarningSpin,
             static_cast< void (QDoubleSpinBox::*) (double) > (&QDoubleSpinBox::valueChanged),
             this, [this] { validate (); });
    connect (_conditionFailSpin,
             static_cast< void (QDoubleSpinBox::*) (double) > (&QDoubleSpinBox::valueChanged),
             this, [this] { validate (); });

    setThresholds (thresholds);
}

// 把外部阈值快照(单位:米/度)写入各输入框,两个容差字段换算为显示单位。
// setValue 本身不触发校验;末尾的 validate() 统一刷新提示状态。
void KinematicThresholdsDialog::setThresholds (const KinematicThresholds& thresholds)
{
    _nearJointLimitRatioSpin->setValue (thresholds.nearJointLimitRatio);
    _conditionWarningSpin->setValue (thresholds.conditionWarning);
    _conditionFailSpin->setValue (thresholds.conditionFail);
    _singularValueWarningSpin->setValue (thresholds.singularValueWarning);
    _manipulabilityWarningSpin->setValue (thresholds.manipulabilityWarning);
    _positionToleranceSpin->setValue (
        displayLengthFromMeters (thresholds.positionToleranceMeters, _lengthUnit));
    _orientationToleranceSpin->setValue (
        displayAngleFromDegrees (thresholds.orientationToleranceDeg, _angleUnit));
    _ikDuplicateQThresholdSpin->setValue (thresholds.ikDuplicateQThreshold);
    validate ();
}

// 汇总各输入框当前值,生成阈值快照(单位:米/度)。
// 容差字段由显示单位换算回 SI:长度 -> 米、角度 -> 度。
// 注意:此函数只读取数值不做校验,应仅在对话框 Accepted 后使用其结果。
KinematicThresholds KinematicThresholdsDialog::thresholds () const
{
    KinematicThresholds result;
    result.nearJointLimitRatio = _nearJointLimitRatioSpin->value ();
    result.conditionWarning = _conditionWarningSpin->value ();
    result.conditionFail = _conditionFailSpin->value ();
    result.singularValueWarning = _singularValueWarningSpin->value ();
    result.manipulabilityWarning = _manipulabilityWarningSpin->value ();
    result.positionToleranceMeters = metersFromDisplayLength (
        _positionToleranceSpin->value (), _lengthUnit);
    result.orientationToleranceDeg = degreesFromDisplayAngle (
        _orientationToleranceSpin->value (), _angleUnit);
    result.ikDuplicateQThreshold = _ikDuplicateQThresholdSpin->value ();
    return result;
}

// 校验条件数一致性:warning 与 fail 必须为有限值,且 warning < fail。
// 结果写入 _validationLabel(错误文本 + 显示/隐藏),返回是否通过。
// 纯只读校验,不修改任何输入框内容。
bool KinematicThresholdsDialog::validate () const
{
    const bool valid = std::isfinite (_conditionWarningSpin->value ()) &&
                       std::isfinite (_conditionFailSpin->value ()) &&
                       _conditionWarningSpin->value () < _conditionFailSpin->value ();
    _validationLabel->setText (valid ? QString () :
                               tr ("Condition warning must be lower than condition fail."));
    _validationLabel->setVisible (!valid);
    return valid;
}

// 重写 accept:校验未通过时直接返回,保持对话框打开;通过后调用基类
// accept 关闭对话框并返回 QDialog::Accepted,调用方再读取 thresholds()。
void KinematicThresholdsDialog::accept ()
{
    if (!validate ())
        return;
    QDialog::accept ();
}
