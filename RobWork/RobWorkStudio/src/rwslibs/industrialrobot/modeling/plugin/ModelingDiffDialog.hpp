/**
 * @file ModelingDiffDialog.hpp
 * @brief MDL-08/UX-13/AT-12差异窗口的私有呈现边界。
 *
 * 业务差异由ModelDiffService唯一产生；本窗口仅呈现、复制选择和定位。
 * 无状态、仅UI线程同步使用，不暴露公共端口或持有历史工作集。
 */
#ifndef IRD_MODELING_PLUGIN_MODELINGDIFFDIALOG_HPP
#define IRD_MODELING_PLUGIN_MODELINGDIFFDIALOG_HPP

#include "ModelingCommandFlows.hpp"

namespace sdurws::ird::modeling {
/**
 * @brief 打开完整只读差异窗口，关闭不修改模型或修订。
 * @param report [in] 原始服务报告，组内行序保持不变。
 * @param baseline/candidate [in] 本轮基线/草稿，仅借用至窗口关闭以解析名称和单位。
 * @param locate [in] 同步会话定位回调；可空，空时明确未装配。
 * Qt子控件由栈上QDialog父子树管理；仅UI线程，无共享可变态。
 */
void showModelingDiffDialog(const ModelDiffReport& report,
    const ModelingWorkingSet& baseline, const ModelingWorkingSet& candidate,
    const ModelingDiffLocator& locate);
}
#endif
