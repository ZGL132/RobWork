/**
 * @file   KinPanelChannelTranslation.hpp
 * @brief  装配通道值 ↔ 插件私有值的翻译函数声明（UI-T64——翻译单点
 *         纪律的具名面；plugin 私有头，仅本单元消费）。
 *
 * 设计依据：
 *   - assembly/sdurws/ird/kinematics/KinematicsPanelChannels.hpp 文件头
 *     （同构投影＋翻译单点——本头即翻译的声明面，定义在
 *     KinematicsPluginAssembly.cpp）；
 *   - PluginPanelTest 经本头对翻译函数做逐字段单元断言（同单元测试
 *     目标消费私有头合法——R-2 仅禁跨单元）。
 *
 * 语义纪律：全部函数为逐字段拷贝（枚举经 switch 逐值映射不按数值强转
 * ——防枚举值漂移被静默吞掉）；任何一侧字段增删都在本 TU 编译点暴露。
 */
#ifndef IRD_KINEMATICS_PLUGIN_KINPANELCHANNELTRANSLATION_HPP
#define IRD_KINEMATICS_PLUGIN_KINPANELCHANNELTRANSLATION_HPP

#include <sdurws/ird/kinematics/KinematicsPanelChannels.hpp>  // 通道值面

#include "KinPanelTypes.hpp"  // 插件私有值面（翻译目标）

namespace sdurws {
namespace ird {
namespace kinematics {

/// 通道请求 → 插件私有请求（五字段全拷贝）。
KinBackgroundRequest toPrivateRequest(const KinChannelBackgroundRequest& request);

/// 通道回执 → 插件私有回执（三字段全拷贝）。
KinBackgroundAck toPrivateAck(const KinChannelBackgroundAck& ack);

/// 通道完成通知 → 插件私有通知（四字段全拷贝）。
KinBackgroundResultNote toPrivateNote(const KinChannelBackgroundResultNote& note);

/// 通道任务点行 → 插件私有行（六字段全拷贝）。
KinTaskPointRow toPrivateTaskPoint(const KinChannelTaskPointRow& row);

/// 通道任务状态行 → 插件私有行（四字段全拷贝）。
KinTaskStatusRow toPrivateTaskStatus(const KinChannelTaskStatusRow& row);

}  // namespace kinematics
}  // namespace ird
}  // namespace sdurws

#endif  // IRD_KINEMATICS_PLUGIN_KINPANELCHANNELTRANSLATION_HPP
