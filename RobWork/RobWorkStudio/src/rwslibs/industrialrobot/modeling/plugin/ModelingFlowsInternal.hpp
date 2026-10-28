/**
 * @file 建模命令流内部适配；DH 与 Xacro 结果分别位于 dh/xacro 子命名空间。
 * 仅作 UI 编排，模型编译与生命周期由 runtime 持有（F14）。
 */
#ifndef IRD_MODELING_PLUGIN_MODELINGFLOWSINTERNAL_HPP
#define IRD_MODELING_PLUGIN_MODELINGFLOWSINTERNAL_HPP

#include <string>

#include "ModelingCommandFlows.hpp"  // ModelingDialogHost/ModuleSessionState

namespace sdurws::ird::modeling {

/// 权威切换流（modeling.switch-authority——L-9 判定先行；DhConvert TU）。
bool executeSwitchAuthorityFlow(ModuleSessionState& session,
                                ModelingDialogHost& host,
                                std::string& summary);

}  // namespace sdurws::ird::modeling

#endif  // IRD_MODELING_PLUGIN_MODELINGFLOWSINTERNAL_HPP
