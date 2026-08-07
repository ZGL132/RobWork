// =============================================================================
//  KinematicThresholdsDialog:运动学分析阈值编辑对话框
// =============================================================================
//
// 该对话框以模态(modal)方式编辑完整阈值快照(KinematicThresholds),
// 供用户在 Report 页面调整"近限位 / 奇异 / 容差"等判定阈值,
// 修改结果由调用方回写分析器,实时影响后续分析。
//
// 单位约定:
//   - 对话框内部值统一采用分析器的 SI 约定:长度用米、角度用度;
//   - 界面显示时仅两个容差字段做单位换算:
//       positionTolerance    按长度单位(米/厘米/毫米/英寸)显示与解析;
//       orientationTolerance 按角度单位(度/弧度/百分度/圈)显示与解析;
//   - 其余阈值(近限位比值/条件数/奇异值/可操作度/IK 去重阈值)为
//     无量纲或 SI 固定单位,不随显示单位换算。
//
// 一致性校验:
//   点击 OK 时先执行 validate();conditionWarning 必须小于 conditionFail,
//   否则停留在对话框并提示错误,不关闭。
#ifndef RWS_KINEMATICANALYSIS_KINEMATICTHRESHOLDSDIALOG_HPP
#define RWS_KINEMATICANALYSIS_KINEMATICTHRESHOLDSDIALOG_HPP

#include "KinematicAnalysisTypes.hpp"

#include <QDialog>

class QDoubleSpinBox;
class QLabel;

namespace rws {

// 完整阈值快照的模态编辑器。
// 编辑期间不修改外部数据:构造时拷贝初始快照,通过 thresholds() 读取结果。
// 单位换算策略见文件头部说明 —— 仅两个容差字段随显示单位换算。
// Modal editor for the complete threshold snapshot. Values are stored in the
// analyzer's SI conventions (metres/degrees); only the two tolerance fields
// are converted for display.
class KinematicThresholdsDialog : public QDialog
{
    Q_OBJECT

  public:
    // 构造:传入初始阈值快照与显示单位。
    // lengthUnit/angleUnit 仅影响两个容差字段的显示与解析,其余字段不受影响;
    // 二者默认分别取 Meters / Degrees。
    explicit KinematicThresholdsDialog (
        const KinematicThresholds& thresholds,
        KinematicLengthUnit lengthUnit = KinematicLengthUnit::Meters,
        KinematicAngleUnit angleUnit = KinematicAngleUnit::Degrees,
        QWidget* parent = nullptr);

    // 汇总各输入框当前值生成新的阈值快照。
    // 容差字段由显示单位换算回 SI(长度 -> 米,角度 -> 度)。
    // 调用方应在对话框以 Accepted 关闭后调用,以保证读取的是被确认的结果。
    KinematicThresholds thresholds () const;

  public Q_SLOTS:
    // 重写 accept:先校验,校验通过才调用基类 accept 关闭对话框。
    // 校验失败时保持对话框打开并显示错误提示。
    void accept () override;

  private:
    // 把阈值快照(单位:米/度)写入各输入框;两个容差字段换算为显示单位。
    // 写入完成后触发一次 validate() 刷新提示状态。
    void setThresholds (const KinematicThresholds& thresholds);
    // 校验当前输入的一致性:conditionWarning 与 conditionFail 必须为有限值,
    // 且 warning < fail。结果写入 _validationLabel,返回是否通过。
    bool validate () const;

    // 长度显示单位(决定 positionTolerance 的显示与换算)。
    KinematicLengthUnit _lengthUnit;
    // 角度显示单位(决定 orientationTolerance 的显示与换算)。
    KinematicAngleUnit _angleUnit;
    // 近关节限位比值输入框:无量纲 [0,1],越低越接近限位。
    QDoubleSpinBox* _nearJointLimitRatioSpin;
    // 条件数警告阈值输入框。
    QDoubleSpinBox* _conditionWarningSpin;
    // 条件数失败阈值输入框:必须大于 _conditionWarningSpin,否则校验失败。
    QDoubleSpinBox* _conditionFailSpin;
    // 最小奇异值警告阈值输入框(低于该值视为接近奇异)。
    QDoubleSpinBox* _singularValueWarningSpin;
    // 可操作度警告阈值输入框(低于该值视为退化)。
    QDoubleSpinBox* _manipulabilityWarningSpin;
    // 位置容差输入框:以长度单位显示,读取时换算回米。
    QDoubleSpinBox* _positionToleranceSpin;
    // 姿态容差输入框:以角度单位显示,读取时换算回度。
    QDoubleSpinBox* _orientationToleranceSpin;
    // IK 候选 Q 去重阈值输入框(无穷范数,rad/m)。
    QDoubleSpinBox* _ikDuplicateQThresholdSpin;
    // 校验提示标签:默认隐藏,validate() 失败时显示错误文本并自动换行。
    QLabel* _validationLabel;
};

}    // namespace rws

#endif    // RWS_KINEMATICANALYSIS_KINEMATICTHRESHOLDSDIALOG_HPP
