/**
 * @file ConditionStructureForm.hpp
 * @brief REQ-04 工况结构的内联暂存表单（WP-14-T12，units/requirements §4.5/§9.8）。
 *
 * 表单局部拥有候选值，不持编辑器或运行时对象。确认只回传一个完整工况，
 * 宿主负责检查基线和一次提交；取消仅重填本地控件。所有方法仅限UI线程。
 */
#ifndef IRD_REQUIREMENTS_PLUGIN_CONDITIONSTRUCTUREFORM_HPP
#define IRD_REQUIREMENTS_PLUGIN_CONDITIONSTRUCTUREFORM_HPP

#include <QWidget>
#include <functional>
#include <sdurws/ird/requirements/Editor.hpp>
#include <sdurws/ird/requirements/RequirementsPluginAssembly.hpp>

class QTableWidget;
class QLineEdit;
class QLabel;
class QComboBox;
class QCheckBox;
class QListWidget;

namespace sdurws::ird::requirements {

/**
 * @brief 工况集合编辑器；Qt父对象管理生命周期，非线程安全。
 *
 * 负载物性分别为kg、工具TCP系下m、kg·m²，事件时长s。未提供/不适用
 * 显式保留，绝不用零补齐。引用只编辑浅引用，不越权解析建模对象。
 */
class ConditionStructureForm final : public QWidget {
public:
    /**
     * @brief 建立独立暂存表单，所有入参均复制，回调不接管外部所有权。
     * @param base [in] 当前工况基线，取消恢复此值。
     * @param points [in] 可选工位，用名称显示、ObjectId绑定事件与适用范围。
     * @param writable [in] 只读时禁止修改、增删和确认。
     * @param submit [in] 确认后的候选接收者；只在解析和域构造校验成功时调用。
     * @param parent [in] Qt父对象；可空，负责销毁本表单。
     * @param choices [in] 可空目录快照；本表单复制值，空时明确降级到兼容轨。
     */
    ConditionStructureForm(const OperatingCondition& base, const std::vector<TaskPoint>& points,
                           bool writable, std::function<void(OperatingCondition)> submit,
                           QWidget* parent, const std::vector<RequirementsReferenceChoice>* choices = nullptr);
private:
    /// @brief 用基线重填全部控件；清除本地未确认增删和错误，不写草稿。
    void reset();
    /// @brief 新增一行负载；origin为基线序号或-1，保证未改物性保留原始来源。
    void addPayload(const ConditionPayload& payload, int origin = -1);
    /// @brief 新增一行事件；下拉以ObjectId绑定工位，缺少目标保持未选而不猜测。
    void addEvent(const ConditionEvent& event);
    /// @brief 新增工具浅引用；preserve为true才允许保留原缺失引用，false表示新行。
    void addTool(const RequirementReference& reference, bool preserve = true);
    /// @brief 解析全部行并由既有域函数校验；失败就地报告，成功只回传一次。
    void apply();

    OperatingCondition m_base;  ///< 不可变进入基线，所有物性来源保留于此。
    std::vector<TaskPoint> m_points;  ///< 工位名称/身份快照，只用于选择呈现。
    std::function<void(OperatingCondition)> m_submit;  ///< UI线程同步值回调。
    QTableWidget* m_payloads = nullptr;  ///< Qt子控件，列：工具类别/标识/TCP/质量/质心/惯量。
    QTableWidget* m_events = nullptr;  ///< Qt子控件，列：类型/工位/时长。
    QTableWidget* m_tools = nullptr;  ///< 工具引用集合，列：类别/标识/TCP。
    QLineEdit* m_environment = nullptr;  ///< 环境scene-object的ObjectId清单，逗号分隔。
    QLineEdit* m_margin = nullptr;  ///< 最小关节裕量，rad或m依关节类型；空为未要求。
    QCheckBox* m_collision = nullptr;  ///< 无碰撞要求。
    QComboBox* m_scope = nullptr;  ///< AllStations/Stations/None。
    QListWidget* m_stations = nullptr;  ///< 适用工位多选；身份承载于UserRole。
    QLabel* m_error = nullptr;  ///< 非模态错误，解析失败保留输入。
    std::optional<std::vector<RequirementsReferenceChoice>> m_choices; ///< 目录值副本，未装配与空目录不同。
    QListWidget* m_environmentChoices = nullptr; ///< 环境名称多选，身份只用于内部浅引用绑定。
};
}
#endif
