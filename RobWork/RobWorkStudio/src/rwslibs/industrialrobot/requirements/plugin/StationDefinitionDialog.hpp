/**
 * @file StationDefinitionDialog.hpp
 * @brief REQ-01/09工位完整定义暂存表单（WP-14-T12，单元卡§4.3/§5.3）。
 * 私有Qt交互面；不解析模型名称或计算姿态。控件由栈上对话框拥有，限UI线程。
 */
#ifndef IRD_REQUIREMENTS_PLUGIN_STATIONDEFINITIONDIALOG_HPP
#define IRD_REQUIREMENTS_PLUGIN_STATIONDEFINITIONDIALOG_HPP
#include <optional>
#include <sdurws/ird/requirements/RequirementsPluginAssembly.hpp>
class QWidget;
namespace sdurws::ird::requirements {
/**
 * @brief 编辑完整工位，确认返回一个值，取消零写入；用户错误就地反馈。
 * @param parent [in] 非拥有Qt父对象，调用期间存活，限UI线程。
 * @param base [in] 原条目快照，未改的来源及四态位置原样保留。
 * @param points [in] 当前工位集合快照，用于重名检查，不接管所有权。
 * @param choices [in] 当前修订名称目录快照；nullptr表示未装配，明确降级提示。
 * @return 合法完整条目或取消nullopt；位置m、角度rad，坐标仍在所选参考系。
 * @throws std::bad_alloc 内存不足；用户解析错误不向外抛出。
 */
std::optional<TaskPoint> editStationDefinition(QWidget* parent, const TaskPoint& base,
                                               const PointSet& points,
    const std::vector<RequirementsReferenceChoice>* choices = nullptr);
}
#endif
