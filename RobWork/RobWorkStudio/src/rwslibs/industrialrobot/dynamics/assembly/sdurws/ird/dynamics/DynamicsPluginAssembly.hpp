/**
 * @file   DynamicsPluginAssembly.hpp
 * @brief  dynamics 插件装配门面——宿主装配层消费 dynamics 插件的
 *         **唯一公共入口**（WP-17-T02 最小可注册形态）。
 *
 * 设计依据：
 *   - units/dynamics.md §9.5（UI 协作——dynamics.analyze/show-curves/
 *     locate-peak/replay-at/export-curve-data 命令族与"UI 线程不得执行
 *     动力学计算"红线；插件界面落位随 WP-17-T09——本头不预建占位接口）、
 *     §3.2（目标布局——_plugin 目标随 WP-17-T02：最小可注册实现，零业务
 *     计算逻辑）、§2.4 O13（IDynamicsCommandHandler 只是领域命令适配器，
 *     不是全局命令注册表——命令注册权威归 ui CommandRegistry）
 *   - units/ui.md §11.1（白名单八 token 词表——"dynamics" 为在册 token，
 *     ui/src/AboutDialog.cpp pluginUiWhitelist 实测含 "dynamics" 行）、
 *     §3.5（文案键族 plugin.<id>.title——id 词表＝§11.1 白名单 token，
 *     值归 ui 文案资源；UX-02：token 本身不进用户文本）
 *   - 先例：selection/assembly/SelectionPluginAssembly.hpp（WP-19-T02
 *     同款"自持描述符装配门面"最小可注册形态）——dynamics 与 selection
 *     同属业务域单元、插件界面面均随后续任务（WP-17-T09/WP-19-T10），
 *     T02 批次零 Q_OBJECT 类、零 ui 编译边（本头零 ui 头包含）
 *
 * ★ 落位形态说明（诚实登记，非遗漏）：本门面以**自持描述符**承载可注册
 *   面——pluginId/titleKey 两字段均有 ui.md 登记出处（§11.1 token＋§3.5
 *   键族），零私造词表；面板/命令/协议供给（dynamics.analyze 提交链、
 *   曲线联动投影、IPluginUiRegistrar 真实注册）随 WP-17-T09 插件界面
 *   任务落位——本头不预建占位接口（NFR-MNT-04）。零业务计算逻辑：本
 *   结构是纯值聚合，动力学计算类符号零出现——契约测试
 *   DynamicsPluginAssemblyContractTest 以词表扫描钉住（卡 §9.5"UI 线程
 *   不得执行动力学计算"红线的 T02 侧执行面）。
 *
 * 落位形态说明（文件位置）：本头位于 dynamics 单元的 `assembly/` 目录
 * （plugin 目标的 PUBLIC include 面——插件界面目标的装配契约头，非产品
 * include/ 扫描域；与 plugin/ 同理在零 Qt 红线的文件域之外——ird_gates
 * 第 4 步产品面扫描域为 include/**＋src/**）。实现
 * （plugin/DynamicsPluginAssembly.cpp）编入 sdurws_ird_dynamics_plugin
 * 目标。
 *
 * 线程模型：createDynamicsPluginAssembly 为纯值工厂（无共享状态、
 * 无 Qt 调用）——任意线程可调用；T02 阶段装配时序未涉及（面板随
 * WP-17-T09 落位时按其任务卡的 UI 线程约束执行）。
 */

#ifndef IRD_DYNAMICS_ASSEMBLY_DYNAMICSPLUGINASSEMBLY_HPP
#define IRD_DYNAMICS_ASSEMBLY_DYNAMICSPLUGINASSEMBLY_HPP

#include <string>

namespace sdurws::ird::dynamics {

/**
 * @brief dynamics 插件装配描述符（WP-17-T02 最小可注册形态的装配产物）。
 *
 * 字段取舍口径（逐字段有据，零私造词表）：
 *   - pluginId：ui.md §11.1 白名单 token "dynamics"（编译期/装配期
 *     常量词表，L5 应用壳固定；ui/src/AboutDialog.cpp pluginUiWhitelist
 *     实测在册）——插件身份的唯一书写点；
 *   - titleKey：ui.md §3.5 键族 plugin.<id>.title 的 "plugin.dynamics.
 *     title"（id 词表＝§11.1 白名单 token；UX-02：token 本身不进用户
 *     文本——用户见中文标题，值归 ui 文案资源，本域不携带值）。
 *
 * 面板/命令/能力声明（dynamics.analyze/show-curves/locate-peak/
 * replay-at/export-curve-data 命令族与曲线联动/峰值定位/时刻回放投影
 * ——卡 §9.5）**不在本结构**：随 WP-17-T09 以真实 ui 类型落位（本头
 * 零 ui 头包含）。零业务计算逻辑（卡 §9.5 红线）：本结构是纯值聚合，
 * 动力学计算类符号零出现——契约测试词表扫描钉住（该扫描为全文扫描、
 * 不剥注释——本头注释亦不书写词表符号字样）。
 */
struct DynamicsPluginDescriptor {
    /// 插件身份（ui.md §11.1 白名单 token——"dynamics"；显示名永不
    /// 替代身份，ARC-04 纪律）。
    std::string pluginId;
    /// 标题文案键（ui.md §3.5 键族 plugin.<id>.title——值归 ui 文案
    /// 资源文件，本域零文案值）。
    std::string titleKey;
};

/**
 * @brief 创建 dynamics 插件装配描述符（每次调用全新实例——无缓存）。
 *
 * T02 批次装配规则（逐字段见类型注）：pluginId＝"dynamics"、
 * titleKey＝"plugin.dynamics.title"。
 *
 * @return 装配描述符（纯值——零 Qt、零业务计算调用）
 */
DynamicsPluginDescriptor createDynamicsPluginAssembly();

}  // namespace sdurws::ird::dynamics

#endif  // IRD_DYNAMICS_ASSEMBLY_DYNAMICSPLUGINASSEMBLY_HPP
