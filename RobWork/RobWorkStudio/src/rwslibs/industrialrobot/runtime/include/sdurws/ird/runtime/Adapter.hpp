/**
 * @file   Adapter.hpp
 * @brief  RobWork 适配层公共面（§8.2～§8.4）——WorkCell 只读视图与
 *         RobWork 异常→稳定诊断的转译纯函数。
 *
 * 设计依据：
 *   - units/runtime.md §8.2（所有权与只读包装——公共接口不暴露可变句柄、
 *     "RobWork 默认值不作需求默认值"）、§8.3（WorkCellConstView 原文契约——
 *     零可变句柄、并发只读安全）、§8.4（RobWork 异常→稳定诊断转译表——
 *     适配层不吞异常）、§6.4（禁止清单——名称不作业务身份）
 *   - 需求 ARC-03（唯一规范模型——WC 是 CanonicalModel 的派生产物）、
 *     MDL-06（编译原子性——转译后编译以 Failed 终止）、MDL-14（运行时名称
 *     ——名称只是产物标签，互查唯经 RuntimeNameMap）、NFR-REL-05（用户诊断
 *     不含调用栈）
 *   - 任务契约 tasks/foundation/RT-T07.json（acceptance 1/3——RT-AD-1/2、
 *     RT-BW-4 的视图/转译承载面）
 *
 * 背景说明（本头在单元内的位置）：runtime 在 S6 段把 CanonicalModel 确定性
 * 编译为 rw::models::WorkCell（Frame 树/SerialDevice/Joint/限位）。基线库
 * 对象（WorkCell/Frame/Device）是可变句柄载体，若直接暴露给下游会破坏两条
 * 红线：①"发布后 WC 不再被修改"的并发只读承诺（§8.7）；②"RobWork 对象名
 * 不作业务身份"的 R-4 纪律（下游不得 findFrame 拼名取对象）。故公共接口
 * 只暴露本头的只读视图；可变句柄（addFrame/setBounds 等）只在编译器
 * src/WorkCellCompiler.cpp 内部可达（§8.2 只读包装行）。
 *
 * 分阶段落位登记（RT-T07/T08）：§3.1 模块表列 Adapter.hpp 承载
 * WorkCellConstView、DynamicWorkCellConstView、DeviceView、
 * RobWorkBaselineVersion、IRobWorkAdapterFactory 五实体。已按"首个消费者
 * 原则"落位：WorkCellConstView（RT-T07 消费）、translateRobWorkError（§8.4
 * 签名面）、DynamicWorkCellConstView（RT-T08 消费——S7 构造 DWC 后才有
 * 可视图语义）；其余实体随其消费任务落位——DeviceView/
 * RobWorkBaselineVersion/IRobWorkAdapterFactory 归快照/编译链任务
 * （RT-T09/RT-T11，基线版本记录随快照身份块——§8.4"RobWorkBaselineVersion
 * 随快照与缓存键记录"）。已按 DTB §5.4 在 units/runtime.md §15.4 增量登记。
 *
 * 线程安全：WorkCellConstView 持有的快照侧 WorkCell 发布后不可变（编译器
 * 不再触碰），视图全部方法 const、并发只读安全；translateRobWorkError 为
 * 纯函数（无共享状态）。
 * 生命周期约定：视图不拥有 WorkCell 的唯一引用——所有权在快照（§8.2"快照
 * 是唯一规范持有者"）；视图仅借持 rw::core::Ptr 引用计数，持视图期间对象
 * 存活。视图返回的 Frame 指针生命周期绑定快照——不得保存跨快照使用（§8.2
 * "不向上层暴露裸指针"行）。
 */

#ifndef SDURWS_IRD_RUNTIME_ADAPTER_HPP
#define SDURWS_IRD_RUNTIME_ADAPTER_HPP

#include <cstddef>
#include <exception>
#include <optional>
#include <string_view>

#include <rw/kinematics/State.hpp>

#include <sdurws/ird/core/DiagData.hpp>  // DiagnosticRecord（§8.4 转译产物）
#include <sdurws/ird/core/Identity.hpp>  // ObjectId（转译的 subject 定位）

// rw 基线类型在公共面只做前置声明（§8.2 只读包装——可变句柄的完整类型与
// 非const 方法在公共头不可达；实现文件 src/WorkCellCompiler.cpp 与
// src/DynamicWorkCellCompiler.cpp 内 include）。
namespace rw { namespace core { template< class T > class Ptr; } }
namespace rw { namespace models { class WorkCell; class SerialDevice; } }
namespace rw { namespace kinematics { class Frame; } }
namespace rwsim { namespace dynamics { class DynamicWorkCell; class Body; } }

namespace sdurws::ird::runtime {

// =====================================================================
// WorkCellConstView——WorkCell 的只读包装（§8.3 原文契约）。
// =====================================================================

/**
 * @brief WorkCell 只读视图：零可变句柄——发布后 WC 不再被修改，并发只读
 *        安全（§8.3 原文契约的实现面）。
 *
 * 背景（为什么要包装而不直接给 Ptr<WorkCell>）：WorkCell/addFrame、
 * Joint/setBounds 等 modifiable 句柄一旦跨出编译器，"发布后不可变"的线程
 * 承诺（§8.7）与"名称↔对象互查只经 RuntimeNameMap"的 R-4 纪律（§8.2）就
 * 无法在类型层锁定。本视图只转发 const 查询；kinematics/trajectory/
 * dynamics/policy 四类消费方（§6.4）经快照的 IRuntimeModelView 取得本视图
 * （快照接口归 RT-T09；本任务交付视图本体供其复用）。
 *
 * 所有权：视图借持 rw::core::Ptr<const WorkCell>（引用计数＋1）——快照仍是
 * 唯一规范持有者（§8.2）；视图存活期间对象保证存活，视图析构不触发 WC
 * 释放（除非视图是最后持有者——快照已先行销毁的场景，引用计数语义兜底）。
 *
 * 线程安全：全部方法 const 且只读查询（findFrame/getFrames/defaultState
 * 为基线只读查询——§8.7"结构查询多线程只读安全"）；defaultState 返回值
 * 拷贝（每线程独立 State——禁止跨线程共享可变 State）。
 */
class WorkCellConstView {
public:
    /**
     * @brief 以快照侧持有的 WorkCell 构造只读视图。
     *
     * @param workCell [in] 编译产物 WorkCell（引用计数借持；空 Ptr 属调用方
     *                契约违约——无 WC 的场景由 capability 表达而非空视图，
     *                §8.3"hasWorkCell 恒 true"，本构造对空输入 fail-fast）
     *
     * @throws RuntimeError 码＝RobWorkError（runtime/robwork-null-handle）：
     *         workCell 为空——§8.4"空 Ptr→robwork-error＋操作名"的构造入口
     *         落点（调用方契约违约，fail-fast 不产出空视图）
     */
    explicit WorkCellConstView(rw::core::Ptr<const rw::models::WorkCell> workCell);

    /**
     * @brief WorkCell 只读引用（§8.3 原文方法——生命周期随快照）。
     *
     * 返回 const 引用：调用方可做全部只读查询（getFrames/getDevices/
     * getDefaultState 等），任何写路径在类型层不可达（const 修饰）。
     * @return 内部 WorkCell 的 const 引用（生命周期随视图＋快照；不抛）
     */
    const rw::models::WorkCell& workCell() const noexcept;

    /**
     * @brief 按运行时名查找 Frame（§8.3 原文方法）。
     *
     * @param name [in] Frame 全名（映射 fullName 逐字节相等——大小写敏感，
     *             与基线 findFrame 行为一致；不拆段、不猜前缀）
     * @return 命中＝Frame const 指针（生命周期绑定快照——不得保存跨快照
     *         使用，§8.2）；未命中＝nullptr（不抛、不默认命中——RT-NM-3
     *         同精神：查询失败显性化）
     *
     * ★ R-4 纪律：本入口只服务"已有名称→查对象"的下游只读消费；业务身份
     *   永远是 ObjectId，名称↔对象互查的权威路径是 RuntimeNameMap（§8.2
     *   "RobWork 对象名不作业务身份"行）。以自行拼接的名称调用本方法取
     *   对象属 R-4 违例（评审＋测试锁定）。
     */
    const rw::kinematics::Frame* findFrame(std::string_view name) const noexcept;

    /**
     * @brief 按运行时名查找 SerialDevice（§8.3 原文方法）。
     *
     * @param name [in] 设备名（映射 Device 条目 fullName——设备名本身无
     *             作用域前缀，§7.3"Device 作用域例外"）
     * @return 命中＝设备的 const Ptr（引用计数借持）；未命中＝空 Ptr
     *         （不抛；调用方以 operator bool 判别——与基线 findDevice
     *         语义一致）
     */
    rw::core::Ptr<const rw::models::SerialDevice>
        findDevice(std::string_view name) const noexcept;

    /**
     * @brief Frame 总数（§8.3 原文方法——含 WORLD 根帧）。
     *
     * 用途：结构等价断言（同输入重复编译 WC 结构相等——§5.5/RT-EQ 系）
     * 与测试的规模核对。
     * @return WC 内 Frame 总数（基线 getFrames().size()——含 WORLD；不抛）
     */
    std::size_t frameCount() const noexcept;

    /**
     * @brief 默认状态值拷贝（§8.3 原文方法——调用方线程私有）。
     *
     * 关节位 q 全 0（编译器不设初值——q 的权威语义经 zeroOffset 映射在
     * bounds/静止位姿，默认状态即 RobWork 零位）。每次调用返回独立值——
     * State 是唯一可变工作区且线程私有（§8.7"每线程 State 工厂"语义的
     * 视图层承载；快照侧 makeState 归 RT-T09，复用本实现）。
     * @return WC 默认状态的值拷贝（修改它不影响 WC 与其他调用方；不抛）
     */
    rw::kinematics::State defaultState() const;

private:
    /// 借持的只读 WC 句柄（构造时非空——唯一构造入口已 fail-fast 空输入）。
    rw::core::Ptr<const rw::models::WorkCell> m_workCell;
};

// =====================================================================
// DynamicWorkCellConstView——DynamicWorkCell 的只读包装（§8.2/§8.3 只读
// 包装纪律的 DWC 面；RT-T08 落位——§3.1 模块表原列实体，"S7 构造 DWC 后
// 才有视图语义"）。
// =====================================================================

/**
 * @brief DynamicWorkCell 只读视图：零可变句柄——发布后 DWC 不再被修改，
 *        并发只读安全（§8.3 精神的 DWC 面；RT-T08 交付）。
 *
 * 背景（与 WorkCellConstView 同源的包装理由）：rwsim DynamicWorkCell/Body
 * 携带大量可变入口（setGravity/setCollisionMargin/addBody/Body::setForce
 * 等），直接暴露 Ptr 会破坏"发布后不可变"的线程承诺（§8.7）。下游
 * dynamics/drivetrain（DYN-06 消费面——§13.2）经快照的
 * IRuntimeModelView（RT-T09）取得本视图，只读消费物性/摩擦/重力数据。
 *
 * 所有权：视图借持 rw::core::Ptr<const DynamicWorkCell>（引用计数＋1）——
 * 快照仍是唯一规范持有者（§8.2）；视图存活期间对象保证存活。★ 生命周期
 * 耦合：DWC 内部自持其 WC 引用（§8.5 销毁顺序行——"rwsim DWC 自持 WC
 * 引用"），故持有 DWC 即隐式保活 WC，两视图可独立使用。
 *
 * 线程安全：全部方法 const 且只读查询（findBody/getBodies 为基线只读
 * 查询——§8.7"结构查询多线程只读安全"）；不暴露重力/边距等写路径。
 */
class DynamicWorkCellConstView {
public:
    /**
     * @brief 以快照侧持有的 DynamicWorkCell 构造只读视图。
     *
     * @param dynamicWorkCell [in] 编译产物 DWC（引用计数借持；空 Ptr 属调用
     *                        方契约违约——DWC 缺失场景由 capability 表达
     *                        〔hasDynamicWorkCell=false 时快照不持有本视图〕
     *                        而非空视图，§9.6；本构造对空输入 fail-fast）
     *
     * @throws RuntimeError 码＝RobWorkError（runtime/robwork-null-handle）：
     *         dynamicWorkCell 为空——§8.4"空 Ptr→robwork-error＋操作名"
     *         的构造入口落点
     */
    explicit DynamicWorkCellConstView(
        rw::core::Ptr<const rwsim::dynamics::DynamicWorkCell> dynamicWorkCell);

    /**
     * @brief DynamicWorkCell 只读引用（生命周期随视图＋快照）。
     *
     * 返回 const 引用：调用方可做全部只读查询（getBodies/getDynamicDevices/
     * getMaterialData〔const 面〕/getGravity 等），任何写路径在类型层不可达。
     * @return 内部 DWC 的 const 引用（不抛）
     */
    const rwsim::dynamics::DynamicWorkCell& dynamicWorkCell() const noexcept;

    /**
     * @brief 按运行时名查找 Body（§8.3 findFrame 精神的 DWC 面）。
     *
     * @param name [in] Body 全名（映射 Body/Tcp 条目逐字节相等——Body 名＝
     *             其承载帧名，基线 Body::getName 契约；S7 编译器保证每体
     *             名字来自映射，RT-CPX 用例钉住）
     * @return 命中＝Body 的 const Ptr（引用计数借持；生命周期绑定快照）；
     *         未命中＝空 Ptr（不抛、不默认命中——与基线 findBody 语义一致）
     */
    rw::core::Ptr<const rwsim::dynamics::Body> findBody(std::string_view name) const noexcept;

    /**
     * @brief Body 总数（§8.5 Body 覆盖校验的消费侧读数）。
     * @return DWC 内 Body 总数（基线 getBodies().size()——含基座/连杆/工具体）
     */
    std::size_t bodyCount() const noexcept;

private:
    /// 借持的只读 DWC 句柄（构造时非空——唯一构造入口已 fail-fast 空输入）。
    rw::core::Ptr<const rwsim::dynamics::DynamicWorkCell> m_dynamicWorkCell;
};

// =====================================================================
// translateRobWorkError——RobWork 异常→稳定诊断的转译纯函数（§8.4）。
// =====================================================================

/**
 * @brief 把编译过程捕获的异常转译为稳定诊断记录（§8.4 原文签名的实现）。
 *
 * 设计依据（§8.4 转译表逐行）：
 *   - rw::core::Exception（基线统一异常，含消息）→ RT-WC-COMPILE-FAILED
 *     （stage＝"S6"）或 RT-DWC-COMPILE-FAILED（stage＝"S7"，RT-T08 使用）；
 *     cause＝原始消息摘要——剥离基线 what() 前缀中的 "Id[..]" 与
 *     "<file>:<line> :" 调用位置信息（NFR-REL-05：用户诊断不含调用栈；
 *     开发诊断如需原文可另行走日志，本函数产物面向诊断收集器）；
 *   - std::bad_alloc → RT-RESOURCE-BUDGET（§5.5"内存不足→Failed＋
 *     ResourceBudget"的转译落点）；
 *   - 其余 std::exception → RT-ROBWORK-ERROR（事件码——Errors.hpp
 *     registryCode 注释：该码的产出归 RT-T07/T08/T11 转译点，不走
 *     StableCodeRegistry 注册路径）。
 *
 * ★ 转译不吞异常（§8.4"适配层不吞异常"）：本函数只产出诊断记录，**不**
 * 决定编译终止——调用方（编译器 S6/S7/S8 边界）在转译后必须以失败终止
 * 编译（本单元实现为抛 RuntimeError 携带对应稳定码；诊断记录本身由
 * RT-T11 编译链在 CompileOutcome.diagnostics 全量登记）。
 *
 * @param ex      [in] 捕获的异常（编译器统一以 const std::exception& 捕获
 *                后转入；函数内部按实际动态类型分派转译行）
 * @param stage   [in] 编译段标识（"S6"/"S7"——十段链段号，§5.2；进入诊断
 *                的 context 字段供定位；当前任务只产出 S6 面，S7 为
 *                RT-T08 预留同款入口）
 * @param subject [in] 定位的规范对象（尽量定位——如编译失败的 robot
 *                ObjectId；无法定位时可为 nullopt——DiagnosticRecord 的
 *                subject 语义：稳定项必带、瞬时开发诊断可空）
 *
 * @return 转译后的稳定诊断记录（code 为 "RT-*" 注册码/事件码形态——码值
 *         权威归 diagnostics StableCodeRegistry，本函数经 Errors.hpp 的
 *         registryCode 冻结映射取串，不私裁码值；PA-1）
 *
 * @throws CoreError 诊断码句法/必填串违约（DiagnosticRecord::make 的 C-3
 *         校验——码表固定非空，理论不可达；防御性向上传播）
 *
 * 确定性：同异常类型＋同消息＋同输入→逐字段相等记录（RT-AD-1 可重复
 * 断言；无环境/时钟依赖）。复杂度 O(消息长度)。
 */
core::DiagnosticRecord translateRobWorkError(const std::exception& ex,
                                             std::string_view stage,
                                             std::optional<core::ObjectId> subject);

}  // namespace sdurws::ird::runtime

#endif  // SDURWS_IRD_RUNTIME_ADAPTER_HPP
