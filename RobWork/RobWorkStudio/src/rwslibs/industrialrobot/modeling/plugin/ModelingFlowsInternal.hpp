/**
 * @file   ModelingFlowsInternal.hpp
 * @brief  命令流的域内拆分接口（插件私有——DhConvert/XacroExpand 两头
 *         同名 ExpandOutcome 不可共 TU，权威切换流独立编译单元承载）。
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
