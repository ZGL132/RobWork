/**
 * @file RegionSamplingForm.hpp
 * @brief REQ-03区域定义与采样的私有暂存表单（WP-14-T12，单元卡§5.2/§9.8）。
 *
 * 本表单只编辑定义，不生成样本或持有随机种子；算法与预算归kinematics。
 * Qt父对象拥有控件；候选和基线为值副本，所有方法仅UI线程，非线程安全。
 */
#ifndef IRD_REQUIREMENTS_PLUGIN_REGIONSAMPLINGFORM_HPP
#define IRD_REQUIREMENTS_PLUGIN_REGIONSAMPLINGFORM_HPP
#include <QWidget>
#include <functional>
#include <sdurws/ird/requirements/RequirementTypes.hpp>
#include <sdurws/ird/requirements/RequirementsPluginAssembly.hpp>
class QFormLayout;
class QComboBox;
class QLineEdit;
class QCheckBox;
class QLabel;
namespace sdurws::ird::requirements {
class ReferenceChoiceBox;
/** @brief 区域完整采样暂存区；草稿权威仍归外部编辑器，不保存编辑器指针。 */
class RegionSamplingForm final : public QWidget {
public:
    /**
     * @brief 从完整区域快照建立暂存区；构造和取消均不触发submit。
     * @param base [in] 原区域值；间距m、覆盖率0~1，未激活模式载荷原样保留。
     * @param writable [in] false时全部编辑及确认禁用。
     * @param submit [in] 合法且变化的完整候选接收者；宿主须排队检查原基线。
     * @param parent [in] 非拥有Qt父对象，负责销毁本表单。
     * @param choices [in] 可空名称目录快照；为空时明确显示能力缺失，保留原引用。
     */
    RegionSamplingForm(const WorkRegion& base, bool writable,
                       std::function<void(WorkRegion)> submit, QWidget* parent,
                       const std::vector<RequirementsReferenceChoice>* choices = nullptr);
private:
    /// @brief 按基线重填全部控件，清除错误与未确认输入；零写入。
    void reset();
    /// @brief 仅切换本地载荷显隐；不补值、不规范化、不提交。
    void updateMethodVisibility();
    /// @brief 严格解析活动载荷，复用域校验；错误保留本地输入，成功回传一次。
    void apply();
    WorkRegion m_base; ///< 不变进入基线，保留溯源/未编辑字段。
    std::function<void(WorkRegion)> m_submit; ///< UI线程同步值出口，宿主负责排队。
    QFormLayout* m_fields = nullptr; ///< Qt拥有的表单布局，用于标签与载荷共同显隐。
    QComboBox* m_method = nullptr; ///< Qt子控件，数据为既有采样枚举。
    std::array<QLineEdit*, 3> m_counts{}; ///< Grid每轴非负uint32计数。
    std::array<QLineEdit*, 3> m_spacing{}; ///< refFrame系三向间距，m。
    QLineEdit* m_random = nullptr; ///< Random非负uint32样本总数。
    QLineEdit* m_direction = nullptr; ///< 姿态方向数，>=1。
    QLineEdit* m_roll = nullptr; ///< 姿态滚转数，>=1。
    QLineEdit* m_token = nullptr; ///< 原样方法token，解释权归KIN-04。
    QLineEdit* m_positionCoverage = nullptr; ///< 位置覆盖率0~1。
    QCheckBox* m_hasOrientationCoverage = nullptr; ///< 未设与已设显式区分。
    QLineEdit* m_orientationCoverage = nullptr; ///< 姿态覆盖率0~1，未设时禁用。
    QLineEdit* m_name = nullptr; ///< 区域名称，集合唯一性归编辑器。
    QLineEdit* m_sequence = nullptr; ///< 前驱名称，空为未设，不生成假引用。
    QLineEdit* m_note = nullptr; ///< 备注原文。
    QCheckBox* m_collision = nullptr; ///< 区域无碰撞要求。
    QLineEdit* m_margin = nullptr; ///< 可选关节裕量，rad/m依关节类型。
    QLabel* m_error = nullptr; ///< 用户输入错误；草稿未写入。
    ReferenceChoiceBox* m_reference = nullptr; ///< Qt子控件；参考系不在本层解析或换算。
    ReferenceChoiceBox* m_tcp = nullptr; ///< Qt子控件；可选工具精确到TCP键。
};
}
#endif
