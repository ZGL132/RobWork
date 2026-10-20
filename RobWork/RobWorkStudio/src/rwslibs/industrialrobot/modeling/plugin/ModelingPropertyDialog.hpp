/**
 * @file ModelingPropertyDialog.hpp
 * @brief F09/MDL-05：几何物性估算段元录入的私有UI边界。
 *
 * 输入沿modeling §5.3 SegmentSpec；所有计算交给唯一PropertyEstimator，
 * 本层只解析字段和转交用户应答。仅UI线程，无共享可变态或持久化配方。
 */
#ifndef IRD_MODELING_PLUGIN_MODELINGPROPERTYDIALOG_HPP
#define IRD_MODELING_PLUGIN_MODELINGPROPERTYDIALOG_HPP
#include <sdurws/ird/modeling/PropertyEstimation.hpp>
namespace sdurws::ird::modeling {
/**
 * @brief 打开段元编辑器，确认前不改任何工作集。
 * @param linkName [in] 连杆显示名，UTF-8，仅用于用户识别当前对象。
 * @return 已确认完整段元列表；取消返回nullopt。尺寸m、位姿平移在连杆系下
 *         为m，欧拉ZYX为rad，密度kg/m³；非法输入保留在窗口中待纠正。
 * 栈上QDialog拥有所有子控件；同步返回后不保留任何控件或业务裸指针。
 */
std::optional<std::vector<SegmentSpec>> editModelingPropertySegments(const std::string& linkName);
}
#endif
