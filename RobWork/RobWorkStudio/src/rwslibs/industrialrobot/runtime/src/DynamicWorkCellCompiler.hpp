/**
 * @file   DynamicWorkCellCompiler.hpp
 * @brief  S7 DynamicWorkCell 编译器（单元内私有头）——CanonicalModel ＋
 *         S6 产物 WorkCell → rwsim DynamicWorkCell 的能力门控编译入口、
 *         产物结构与 §8.5 关联校验/销毁顺序承载。
 *
 * 设计依据：
 *   - units/runtime.md §5.2 S7 行（能力门控：任一被消费 Body 的物性
 *     NotProvided→跳过构造〔capability.hasDynamicWorkCell=false，警告诊断，
 *     不算失败〕；物性已提供但构造失败→DwcCompileFailed）、§5.6（能力缺失/
 *     输入非法/编译失败正交关系）、§8.5（同源校验/Body 覆盖/设备一致/
 *     销毁顺序）、§8.1（P-RT-3——rwsim dynamics 为已登记 L1 依赖）、
 *     §8.2（显式设值——DWC 重力、每 Body 质量惯量、Device 名）、§9.1
 *     （dynamicWorkCellState＝{Compiled, SkippedNoPhysics}＋Skipped 缺失
 *     对象清单——本结构的 status/skippedObjects 即其承载面）、§9.6（能力
 *     声明——hasDynamicWorkCell 派生口径＝全部连杆物性 Provided，RT-T04
 *     已登记）
 *   - 需求 MDL-06（双编译原子性——失败不发布半成品）、DYN-06（物性缺失
 *     降级非阻断——下游 DataInsufficient 判定归 dynamics）、CON-04（部分/
 *     失败产物不作命中——跳过状态入 DWC 层缓存键的事实承载）
 *   - 任务契约 tasks/foundation/RT-T08.json（产物：DWC 构造＋能力门控＋
 *     关联校验；acceptance：RT-CPX-1/2、RT-AD-3＋§5.6 降级路径＋R-2 API
 *     表达力前置实测——实测结论与选型登记见 units/runtime.md §15.4 v0.9）
 *
 * 私有头说明（R-2）：本头只被 src/DynamicWorkCellCompiler.cpp 与单元内测试
 * （test/ 与 src/ 同权，CMake 已建 include 路径）include——十段编译链
 * （RT-T11）是 S7 的唯一产品调用方，跨单元不暴露（下游 dynamics 消费面是
 * RT-T09 快照的 tryDynamicWorkCell/IRuntimeModelView，经 Adapter.hpp 的
 * DynamicWorkCellConstView 只读包装）。
 *
 * R-2 前置实测结论（acceptance 3 的 read 阶段消账，逐条登记 §15.4 v0.9）：
 *   1) rwsim DWC 构造器齐全（WorkCell::Ptr＋Body/Device/Constraint/
 *      Controller 列表——DynamicWorkCell.hpp 实测），可整体一次构造；
 *   2) RigidBody 强制要求 Object 基帧为 MovableFrame（RigidBody.cpp 构造
 *      实测 RW_THROW），而 S6 产物连杆帧为 FixedFrame——故 S7 为每个移动
 *      连杆在 WC 树内挂接 Body 承载 MovableFrame（名＝映射 Body 条目，
 *      §7.1"IRB6700.link_2.body"），基座连杆（落地固定）与有物性工具
 *      （刚连法兰）用 FixedBody（无 MovableFrame 要求，实测）；
 *   3) rwsim 摩擦 API 为 material-pair 载体（MaterialDataMap::addFrictionData，
 *      FrictionData{type=Coulomb|Custom, parameters 自由名值}），无逐关节
 *      摩擦一等职——偏置摩擦模型 {fv, fc, bias}（MDL-16）按 §8.8 默认
 *      "无补丁方案"承载：每关节（摩擦全 Provided 时）注册确定性材质
 *      （id＝该关节映射全名）并在 (mat, mat) 对上登记 Custom 摩擦数据；
 *      不改 rwsim 源码（SA-02），不触发补丁流程，能力位仍按内容派生。
 *
 * 线程安全：compileDynamicWorkCell 为可重入纯函数（无共享可变状态——
 * §5.5"编译器实例无共享可变状态"，每次调用独立构造全部对象）；构造期
 * 基线对象仅在本调用线程可见，发布后只读（§8.7）。
 */

#ifndef SDURWS_IRD_RUNTIME_DYNAMICWORKCELLCOMPILER_HPP
#define SDURWS_IRD_RUNTIME_DYNAMICWORKCELLCOMPILER_HPP

#include <string>
#include <vector>

#include <rwsim/dynamics/DynamicWorkCell.hpp>  // 产物类型（L1：P-RT-3 已登记）

#include <rw/core/Ptr.hpp>
#include <rw/models/WorkCell.hpp>

#include <sdurws/ird/core/DiagData.hpp>           // DiagnosticRecord（警告级诊断）
#include <sdurws/ird/core/Identity.hpp>           // core::ObjectId（缺失对象清单）
#include <sdurws/ird/runtime/CanonicalModel.hpp>  // 输入：CanonicalModel
#include "WorkCellCompiler.hpp"                   // 输入：WorkCellCompileOutcome（S6 产物）

namespace sdurws::ird::runtime {

/**
 * @brief S7 编译状态（§9.1 dynamicWorkCellState 枚举的承载——"Skipped 是
 *        能力事实，非失败"）。
 *
 * 值语义纯枚举；线程安全。
 */
enum class DwcCompileStatus {
    /// 物性齐备且构造成功——DWC 可用（快照侧 capability.hasDynamicWorkCell=true）。
    Compiled,
    /// 存在被消费 Body（链连杆）物性 NotProvided——跳过构造（§5.2 S7 门控；
    /// 警告级诊断随 outcome.warnings，不算失败——§5.6 正交表）。
    SkippedNoPhysics,
};

/**
 * @brief S7 编译产物（§5.2 S7 行输出列＋§9.1 dynamicWorkCellState 的 C++
 *        承载）。
 *
 * 所有权与生命周期（§8.2/§8.5）：dynamicWorkCell/workCell 以引用计数持有
 * （快照是唯一规范持有者——本结构是编译事务（RT-T11 CompileTransaction）
 * 向快照交接前的瞬态载体）；编译失败（异常）时局部对象析构＝DWC 及其
 * Body/Device 瞬态对象随之释放（MDL-06"失败时 S6/S7 的 RobWork 对象全部
 * 析构"的本任务落点）。
 *
 * ★ 销毁顺序（§8.5 销毁顺序行，RT-AD-3 的类型层承载）：成员声明序中
 *   dynamicWorkCell 在 workCell **之前**——析构逆序使 workCell 成员先出
 *   作用域，但 WC 对象因 rwsim DWC 自持 WC 引用（DWC._workcell，实测）而
 *   存活至 DWC 释放，即 WC 实际释放不早于 DWC（"DWC 先于 WC 释放"；
 *   值成员 status/deviceName/清单先析构无害——均为纯值）。RT-T09 的
 *   RuntimeSnapshot 私有成员必须保持同一声明序（§8.5"快照成员声明序"，
 *   已登记 §15.4 v0.9）；RT-AD-3 用例以引用计数观测钉住该性质。
 *
 * 确定性：同 model＋同 S6 产物重复调用产出结构相等 DWC（Body 集同名、
 * 每体质量/质心/惯量逐元素相等、设备同名同关节集、重力相等——§5.5
 * "同输入重复编译"DWC 面；RT-EQ 的 DWC 侧断言归 RT-T12 全量矩阵）。
 *
 * 线程安全：产物 DWC 发布后只读（编译器不再触碰）；经
 * DynamicWorkCellConstView 只读共享（公共面不暴露本结构的可变 Ptr——
 * §8.2 只读包装，R-2 下仅单元内可见）。
 */
struct DynamicWorkCellCompileOutcome {
    /// 编译状态（Skipped 时 dynamicWorkCell 为空——与 capability 声明一致，
    /// §9.1"与 capability 一致（不一致构造拒绝）"的事前保证：本结构只经
    /// compileDynamicWorkCell 产出，两字段一致性由编译器内部分派点锁定）。
    DwcCompileStatus status = DwcCompileStatus::SkippedNoPhysics;
    /// 设备名（透传 S6 产物——S8 交叉校验与 RT-T11 装配的定位面；值语义）。
    std::string deviceName;
    /// Skipped 时的缺失物性对象清单（§9.1"Skipped 时的缺失对象清单"——
    /// 链序、可含多条；Compiled 时恒空）。
    std::vector<core::ObjectId> skippedObjects;
    /// 警告级诊断（§5.2 S7"警告诊断，不算失败"；Compiled 时恒空；Skipped
    /// 时恰一条 RT-CAPABILITY-MISSING——subject 定位首个缺失对象，§9.6）。
    std::vector<core::DiagnosticRecord> warnings;
    /// 编译产物 DWC（Skipped 时为空 Ptr；§8.5 成员声明序见结构体注释）。
    rwsim::dynamics::DynamicWorkCell::Ptr dynamicWorkCell;
    /// S6 产物 WorkCell（引用计数共享——与输入 outcome 同一对象，"同源"
    /// 的产物侧凭证；§8.5 成员声明序见结构体注释）。
    rw::core::Ptr<rw::models::WorkCell> workCell;
};

/**
 * @brief S7 DynamicWorkCell 编译（§5.2 S7 行——CanonicalModel＋S6 产物 →
 *        rwsim DynamicWorkCell，含能力门控与 §8.5 关联校验）。
 *
 * 编译步骤（实现体内逐步注释对应）：
 *   1. 名称源唯一化＋门控判定：buildRuntimeNameMap(model)（与 S6 同源——
 *      Body 承载帧名来自映射 Body 条目）；重走连杆物性四态得缺失清单，
 *      与 model.capabilities().hasDynamicWorkCell（S5 派生，PA-1 单一权威）
 *      防御性互核——不一致即内部违约→DwcCompileFailed（不静默放行）；
 *   2. 门控为假→降级返回：SkippedNoPhysics＋缺失清单＋一条警告级
 *      RT-CAPABILITY-MISSING（subject＝首个缺失连杆；正常返回不抛——
 *      §5.6"缺失不妨碍发布"）；此时不触碰 WC（零副作用）；
 *   3. 门控为真→构造（全部瞬态对象由局部持有，异常即析构）：
 *      a. WC 设备解析（JointDevice——RigidDevice 的运动学模型输入）；
 *      b. 基座连杆 Body：Body 承载 FixedFrame（名＝映射 Body 条目）挂
 *         BaseFrame 下＋RigidObject 挂 WC＋FixedBody（落地固定基座；
 *         BodyInfo 逐字承载 canonical 质量/质心/惯量——§8.2 显式设值）；
 *      c. 移动连杆 Body（i＝1..N，链序）：Body 承载 MovableFrame（RigidBody
 *         强制，R-2 实测）挂连杆帧下＋RigidObject＋RigidBody；
 *      d. 有物性工具 Body：FixedBody 锚在其既有 Tcp 帧（名＝映射 Tcp 条目
 *         ——§7.1 Body 范围仅覆盖 link，工具体不新造帧名，R-4 纪律）；
 *      e. RigidDevice（基座体＋移动连杆体序列＋WC 设备）＋DynamicWorkCell
 *         整体构造（Body/Device 一次注册——基线构造器实测）；
 *      f. 显式设值：DWC 重力＝model.world().gravityWorld（§8.2——基线
 *         构造默认 −9.82 不得残留；DYN-01 世界系重力）；
 *      g. 摩擦承载（§8.8 无补丁方案；摩擦数据挂 DWC 的 MaterialDataMap，
 *         故在 DWC 构造后经可变访问器注册）：逐关节（三元组全 Provided）
 *         注册材质（id＝关节映射全名）＋Custom 摩擦数据 (fv, fc, bias)；
 *   4. §8.5 关联校验（全部通过才产出；违者 DwcCompileFailed——内部缺陷
 *      类，不得放行）：同源（DWC.getWorkCell()==本 WC）、Body 覆盖
 *      （Body 数＝有物性连杆数＋有物性工具数，逐名命中）、设备一致
 *      （RigidDevice 各 Body 回溯关节名集＝WC 设备关节集，逐关节名经
 *      映射交叉）；
 *   5. 全程 try/catch：RobWork 异常/bad_alloc/其他 std 异常经
 *      translateRobWorkError（stage="S7"→DwcCompileFailed，§8.4）转译后
 *      以 RuntimeError fail-fast——转译不吞异常、不发布半成品（MDL-06）。
 *
 * @param model          [in] 规范模型（应为 CanonicalModelBuilder 产物——
 *                       S5 构造不变量成立；只读，本函数不修改）
 * @param workCellOutcome [in] S6 编译产物（workCell 非空——空输入属调用方
 *                       契约违约，fail-fast；同源校验的输入面）
 *
 * @return 编译产物（Compiled：dynamicWorkCell/workCell 非空；Skipped：
 *         dynamicWorkCell 为空、skippedObjects/warnings 非空——两态均正常
 *         返回，仅构造失败走异常轨）
 *
 * @throws RuntimeError 码＝DwcCompileFailed（RobWork 异常转译/内部不变量
 *         破坏——detail 含 stage=S7 定位）、RobWorkError（空 WC 句柄——
 *         §8.4 空句柄行）、StructureInvalid（名称映射缺要素——防御面）、
 *         ResourceBudget（std::bad_alloc——§5.5）
 *
 * 复杂度：O(链长＋工具)（对象构造线性；无迭代求解）。
 */
DynamicWorkCellCompileOutcome compileDynamicWorkCell(
    const CanonicalModel& model, const WorkCellCompileOutcome& workCellOutcome);

}  // namespace sdurws::ird::runtime

#endif  // SDURWS_IRD_RUNTIME_DYNAMICWORKCELLCOMPILER_HPP
