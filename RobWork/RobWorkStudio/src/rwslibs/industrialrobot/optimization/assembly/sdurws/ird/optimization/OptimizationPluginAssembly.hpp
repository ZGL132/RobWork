/**
 * @file   OptimizationPluginAssembly.hpp
 * @brief  optimization 插件装配门面——宿主装配层消费 optimization 插件的
 *         **唯一公共入口**（WP-20-T02 最小可注册形态）。
 *
 * 设计依据：
 *   - units/optimization.md §3.1/§3.2（目标布局——plugin/ 目录承载
 *     sdurws_ird_optimization_plugin Qt Widgets 界面目标；界面面落位归
 *     WP-20-T10"实现 optimization 插件界面"——变量表/约束页/运行控制/
 *     候选表与对比）、§14.1（WP-20-T10 行——本头不预建占位接口）、
 *     §2.4（N11 非所有权——Qt UI 真值归 ui；本插件只持只读投影消费面）
 *   - units/ui.md §11.1（IPluginUiRegistrar 静态白名单八 token 含
 *     "optimization"——ui/src/AboutDialog.cpp pluginUiWhitelist 实测在册
 *     行，2026-10-07；StageId::Optimization 阶段挂位）、§3.5（文案键族
 *     plugin.<id>.title——id 词表＝§11.1 白名单 token，值归 ui 文案资源；
 *     UX-02：token 本身不进用户文本）
 *   - 先例：trajectory/assembly/TrajectoryPluginAssembly.hpp（WP-16-T03
 *     同款"自持描述符装配门面"最小可注册形态；dynamics WP-17-T02/
 *     selection WP-19-T02/workflow WP-22-T02 更早先例）——optimization
 *     与 trajectory/dynamics/selection 同属业务域单元、插件界面面均随后
 *     续任务（WP-20-T10），T02 批次零 Q_OBJECT 类、零 ui 编译边（本头零
 *     ui 头包含）
 *
 * ★ 落位形态说明（诚实登记，非遗漏）：本门面以**自持描述符**承载可注册
 *   面——pluginId/titleKey 两字段均有 ui.md 登记出处（§11.1 token＋§3.5
 *   键族），零私造词表；面板/命令/协议供给（变量表、约束页、运行控制
 *   〔取消/进度漏斗〕、候选表与对比视图的域侧数据面与命令族注册）随
 *   WP-20-T10 插件界面任务落位——本头不预建占位接口（NFR-MNT-04）。零
 *   业务计算逻辑（二分结构红线——卡 §3.2"只消费端口与只读投影，不持
 *   算法/判定真值"）：本结构是纯值聚合，优化域计算类符号零出现——契约
 *   测试 OptimizationPluginAssemblyContractTest 以词表扫描钉住（契约
 *   acceptance 2"二分结构扫描"的插件半区执行面）。
 *
 * 落位形态说明（文件位置）：本头位于 optimization 单元的 `assembly/`
 * 目录（plugin 目标的 PUBLIC include 面——插件界面目标的装配契约头，
 * 非产品 include/ 扫描域；与 plugin/ 同理在零 Qt 红线的文件域之外——
 * ird_gates 第 4 步产品面扫描域为 include/**＋src/**）。实现
 * （plugin/OptimizationPluginAssembly.cpp）编入 sdurws_ird_optimization_
 * plugin 目标。
 *
 * 线程模型：createOptimizationPluginAssembly 为纯值工厂（无共享状态、
 * 无 Qt 调用）——任意线程可调用；T02 阶段装配时序未涉及（面板随
 * WP-20-T10 落位时按其任务卡的 UI 线程约束执行——§9.1"不在 UI 线程
 * 执行候选评估"）。
 */

#ifndef IRD_OPTIMIZATION_ASSEMBLY_OPTIMIZATIONPLUGINASSEMBLY_HPP
#define IRD_OPTIMIZATION_ASSEMBLY_OPTIMIZATIONPLUGINASSEMBLY_HPP

#include <string>

namespace sdurws::ird::optimization {

/**
 * @brief optimization 插件装配描述符（WP-20-T02 最小可注册形态的装配产物）。
 *
 * 字段取舍口径（逐字段有据，零私造词表）：
 *   - pluginId：ui.md §11.1 白名单 token "optimization"（编译期/装配期
 *     常量词表，L5 应用壳固定；ui/src/AboutDialog.cpp pluginUiWhitelist
 *     实测在册）——插件身份的唯一书写点；
 *   - titleKey：ui.md §3.5 键族 plugin.<id>.title 的 "plugin.optimization.
 *     title"（id 词表＝§11.1 白名单 token；UX-02：token 本身不进用户
 *     文本——用户见中文标题，值归 ui 文案资源，本域不携带值）。
 *
 * 面板/命令/能力声明（变量表、约束页、运行控制——取消/进度漏斗、候选
 * 表与对比视图——卡 §14.1 WP-20-T10 行的落位面）**不在本结构**：随
 * WP-20-T10 以真实 ui 类型落位（本头零 ui 头包含）。零业务计算逻辑
 * （二分结构红线——卡 §3.2"不持算法/判定真值"）：本结构是纯值聚合，
 * 优化域计算类符号零出现——契约测试词表扫描钉住（该扫描为全文扫描、
 * 不剥注释——本头注释亦不书写词表符号字样）。
 */
struct OptimizationPluginDescriptor {
    /// 插件身份（ui.md §11.1 白名单 token——"optimization"；显示名永不
    /// 替代身份，ARC-04 纪律）。
    std::string pluginId;
    /// 标题文案键（ui.md §3.5 键族 plugin.<id>.title——值归 ui 文案
    /// 资源文件，本域零文案值）。
    std::string titleKey;
};

/**
 * @brief 创建 optimization 插件装配描述符（每次调用全新实例——无缓存）。
 *
 * T02 批次装配规则（逐字段见类型注）：pluginId＝"optimization"、
 * titleKey＝"plugin.optimization.title"。
 *
 * @return 装配描述符（纯值——零 Qt、零业务计算调用）
 */
OptimizationPluginDescriptor createOptimizationPluginAssembly();

}  // namespace sdurws::ird::optimization

#endif  // IRD_OPTIMIZATION_ASSEMBLY_OPTIMIZATIONPLUGINASSEMBLY_HPP
