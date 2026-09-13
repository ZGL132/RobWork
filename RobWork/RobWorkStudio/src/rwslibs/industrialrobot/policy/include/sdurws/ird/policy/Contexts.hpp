/**
 * @file   Contexts.hpp
 * @brief  注入用最小接口——policy 对 runtime/project/宿主能力的零编译依赖
 *         消费面（IPolicyNameContext / IPolicyBytesSource / IPolicyCallContext）。
 *
 * 设计依据：
 *   - units/policy.md §3.3（对 runtime/project 能力的注入边界——依赖白名单的
 *     实施形态，本头三接口签名的唯一权威章节）、§9.7（注入用最小接口契约表：
 *     前置/后置/错误/线程/生命周期逐行）、§3.1（组成表：Contexts.hpp＝
 *     IPolicyNameContext、IPolicyCallContext、IPolicyBytesSource）、
 *     §12 POL-T05 行（本头为 POL-T05 产物之一）
 *   - traceability/foundation-api-diff.md CR-04（policy↔runtime 碰撞场景接口
 *     映射——IPolicyNameContext↔IRuntimeNameResolver 的适配裁决，已关闭）、
 *     CR-03（IPolicyBytesSource↔project IProjectQueryPort::tryObject 一比一转发）
 *   - 治理登记 P-POL-9（governance-log.md，closed）：策略编辑命令处理器适配器
 *     宿主＝ui 单元（DTB WP-10-T07 定稿）——本头**只定义接口、零适配器实现**，
 *     适配器归 L5/ui 装配（任务契约 POL-T05.json acceptance 3 处置约束）
 *   - 需求 ARC-05（策略单一权威——④端口所有者）、UX-08（显示单位切换不改
 *     策略身份——身份只随语义闭包，注入面不携带显示状态）、KIN-13（近限位
 *     阈值随策略对象经④端口传递——评估 API 无阈值参数，R-POL-5）
 *
 * 背景说明（为什么是"注入"而不是"链接"——第一读者须知）：
 *   policy（L2 计算内核）与 runtime/project（同为 L2/L3 平台内核）在 ARCH
 *   §3.5 依赖表中**没有**任何边（平台内核间彼此不横向互链），但碰撞评估
 *   客观上需要三类外部能力：
 *   - 名称解析（runtime ⑥端口 RuntimeNameMap：ObjectId↔运行时名）——会话
 *     构建期把策略规则对象对转为 RobWork 完整名；
 *   - 对象字节读取（project ②端口：按 (对象, 内容版本) 取策略对象字节）——
 *     ④端口 resolvePolicy 的取数来源；
 *   - 取消与上下文存活（execution/宿主状态）——评估在样本边界协作检查。
 *   三者统一用"值传递＋最小注入接口"解决（与 evidence.md §3.3 D-07、
 *   project.md IModelCompilePort P-PR-7 同一模式）：本头只声明**窄接口**，
 *   具体适配器由 L5 应用壳装配期提供（NFR-MNT-04——避免每个请求方重复
 *   适配），policy 保持零 runtime/project 编译依赖（R-1/R-2 红线）。
 *
 * CR-04 适配映射（已关闭裁决，适配器实现时对照——policy 侧不改签名）：
 *   - IPolicyNameContext ↔ runtime IRuntimeNameResolver：
 *     tryObjectId↔resolveObjectId、tryRuntimeName↔resolveRuntimeName、
 *     nameMapContentIdentity↔nameMapIdentity；runtime 侧 Expected 非抛出
 *     错误 → 本接口 nullopt（**不可解析不猜测**——ARC-04）。
 *   - IPolicyBytesSource ↔ project IProjectQueryPort::tryObject：一比一转发
 *     （CR-03 同形——nullopt＝该 (对象, 内容版本) 在存储侧无字节）。
 *   - CollisionScene.workcell 经快照别名构造共享只读、sceneContentIdentity←
 *     workCellCompileIdentity（归属 CollisionEvaluator.hpp/POL-T06，非本头）。
 *
 * 实现纪律（P-POL-9 处置约束——本头的边界声明）：
 *   - 本头**只定义接口**：不提供任何适配器/工厂/默认实现；适配器归 L5/ui
 *     装配（策略编辑命令适配器宿主＝ui，WP-10-T07 定稿——P-POL-9 已关闭）。
 *   - 适配器（在装配侧实现时）只做只读转发：名称适配器**不得**含 RobWork
 *     名称前缀拼接/剥离逻辑（R-4 红线；仅转发 runtime 解析结果——policy
 *     消费完整名不构成拼接，P-POL-8 措辞登记）。
 *   - 本头零 runtime/project/execution 头包含（R-1/R-2；BuildRedLineTest
 *     NoCrossUnitInclude 机械钉住——include 面白名单＝{core, policy}）。
 *
 * 线程安全：三接口的实现均须**并发只读安全**（§9.7 表"并发只读"行）——
 * 实现内不得有可变共享状态；装配期构建后视为不可变。
 */

#ifndef SDURWS_IRD_POLICY_CONTEXTS_HPP
#define SDURWS_IRD_POLICY_CONTEXTS_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/core/Identity.hpp>

namespace sdurws::ird::policy {

// =====================================================================
// IPolicyNameContext——runtime ⑥端口（名称映射）的最小只读注入面。
// =====================================================================

/**
 * @brief 对"对象身份↔运行时名称"映射的只读查询接口（§3.3 原文签名）。
 *
 * 为什么需要注入：policy 把策略规则里的对象对（ObjectId）转成 RobWork
 * 碰撞检测所需的完整 Frame/Device 名时，名称映射的**唯一权威**是 runtime
 * 的 RuntimeNameMap（⑥端口）——policy 不做名称拼接/剥离（R-4 红线），
 * 只经本接口读取 runtime 的解析结果。适配器由 L5/请求方提供（§9.7 表
 * "L5 共享；进程级"行；CR-04 裁决：Expected 错误→nullopt）。
 *
 * 后置契约（§9.7 表 IPolicyNameContext 行）：
 *   - 解析只读、幂等——同 (映射内容, 名称) 重复查询返回同应答；
 *   - 不修改映射（实现不得有可变状态外泄）；
 *   - 不可解析 → 返回 nullopt（**不猜测**——ARC-04：宁可显式缺名产生
 *     POLICY-CLL-NAME-UNRESOLVED 诊断，也不编造名称静默收窄必检集）。
 *
 * 合法/非法使用（§9.7 表原文）：合法＝会话构建解析 device/frames；非法＝
 * 实现内做前缀拼接（R-4——适配器不得含拼接逻辑，仅转发 runtime 结果）。
 *
 * 线程安全：实现须并发只读安全（会话构建可能并发调用）。
 */
class IPolicyNameContext {
public:
    virtual ~IPolicyNameContext() = default;

    /**
     * @brief 运行时名 → 对象身份（runtime ⑥端口 resolveObjectId 的转发形）。
     *
     * @param runtimeName [in] 运行时完整名（RobWork Frame/Device 路径名；
     *                    只读消费整名——适配器不得拼拆前缀，R-4/P-POL-8）
     * @return 对象身份；映射中无该名称 → nullopt（不猜测——ARC-04）
     */
    virtual std::optional<core::ObjectId> tryObjectId(const std::string& runtimeName) const = 0;

    /**
     * @brief 对象身份 → 运行时名（runtime ⑥端口 resolveRuntimeName 的转发形）。
     *
     * @param object [in] 对象身份（全零保留值语义由映射内容决定——实现按
     *               映射事实应答，本接口不预判）
     * @return 运行时完整名；映射中无该对象 → nullopt（不猜测——ARC-04）
     */
    virtual std::optional<std::string> tryRuntimeName(core::ObjectId object) const = 0;

    /**
     * @brief 名称映射的内容身份（CON-06：映射内容变化必须可观测）。
     *
     * 用途：sessionIdentity 的组成要素（§6.5——sessionIdentity=f(policy,
     * scene, names, backend)）与名称解析诊断的定位锚。同一映射内容 → 同
     * 身份（内容寻址）；runtime 侧重建/升级名称映射后身份改变，旧会话
     * 输出与新会话输出可区分（CON-06 失效联动）。
     *
     * @return 名称映射内容身份（非全零——由 runtime ⑥端口计算，适配器
     *         原样转发；worker 侧装配保证与主进程同一身份，§3.3 worker 行）
     */
    virtual core::ContentIdentity nameMapContentIdentity() const = 0;
};

// =====================================================================
// IPolicyBytesSource——project ②端口（对象字节读取）的最小只读注入面。
// =====================================================================

/**
 * @brief 按 (对象身份, 内容版本) 只读读取对象字节的接口（§3.3 原文签名）。
 *
 * 为什么需要注入：④端口 resolvePolicy 的取数来源是 project 持有的策略
 * 对象字节（对象字节对 project 不透明、内容版本按**对象字节**编址——
 * CON-05）；policy 与 project 无编译依赖边（ARCH §3.5），经本接口值传递
 * 取字节。适配器由 L5/worker 宿主提供（CR-03：与 project
 * IProjectQueryPort::tryObject 一比一转发）。
 *
 * 后置契约（§9.7 表 IPolicyBytesSource 行）：
 *   - 字节只读——实现不得缓存改动/版本改写；
 *   - 缺失 → nullopt（该 (对象, 内容版本) 在存储侧无字节——对象不存在或
 *     版本不在存储闭包内；调用方④端口将其转为 POLICY-OBJECT-MISSING
 *     诊断，不抛、不猜测）。
 *
 * worker 进程形态（§3.3 worker 行原文）：工作进程内本接口的实现为"随
 * 请求物化的快照载荷存储"（execution 序列化装配）——物化发生在端口服务
 * 之前，保证进程内同键应答稳定（记忆化确定性的存储前提）。
 *
 * 线程安全：实现须并发只读安全（并发 resolvePolicy 共享同一实例）。
 */
class IPolicyBytesSource {
public:
    virtual ~IPolicyBytesSource() = default;

    /**
     * @brief 读取指定内容版本的对象字节（project ②端口 tryObject 转发形）。
     *
     * @param object  [in] 对象身份（策略对象——ObjectId，project 分配）
     * @param version [in] 期望内容版本（CON-05 内容编址——字节级摘要版本戳；
     *                全零保留值无编址意义，④端口入口处已 fail-fast 拒绝）
     * @return 对象字节（PolicyCodec::decode 的合法输入形态）；存储侧无该
     *         (对象, 版本) → nullopt（环境事实，非异常——错误分类见
     *         units/policy.md §9.1 错误类型行"对象缺失/版本不符→不抛"）
     */
    virtual std::optional<std::vector<std::uint8_t>>
    tryObjectBytes(core::ObjectId object, core::ContentVersion version) const = 0;
};

// =====================================================================
// IPolicyCallContext——宿主（execution/主进程）注入的取消与存活查询面。
// =====================================================================

/**
 * @brief 评估调用上下文——协作取消与运行/快照上下文存活的只读查询接口
 *        （§3.3 原文签名；实现由 execution/宿主注入）。
 *
 * 为什么需要注入：长序列碰撞评估（PathSequence/SampleSet）必须可被取消、
 * 且不得在快照/运行结束后继续产出"正式"输出（§6.1 只读生命周期双保险的
 * 第二层——第一层是场景 shared_ptr 共享所有权）。宿主持有取消标志与存活
 * 事实，评估在**样本边界**轮询本接口（不跨线程打断计算——协作式取消，
 * NFR-PERF-02 精神；TASK-02 取消传递的 policy 侧承接）。
 *
 * 后置契约（§9.7 表 IPolicyCallContext 行）：
 *   - cancellationRequested()/alive() 反映宿主状态（查询瞬时事实）；
 *   - 每运行/调用一个实例；运行结束置失效（alive()=false）。
 *
 * 合法/非法使用（§9.7 表原文）：合法＝evaluate 传入；非法＝失效后仍期待
 * 正式输出（§6.1 迟到拒绝——POL-LATE-1：alive()=false 后评估返回
 * Failed＋POLICY-CLL-CONTEXT-EXPIRED，finalized=false，无正式输出）。
 *
 * 线程安全：实现须并发只读安全（取消标志可能由其他线程置位——实现内部
 * 自行保证可见性；本接口不承诺阻塞等待语义）。
 */
class IPolicyCallContext {
public:
    virtual ~IPolicyCallContext() = default;

    /**
     * @brief 宿主是否已请求取消（协作取消查询——NFR-PERF-02 精神）。
     *
     * @return true＝已请求取消：评估在样本边界尽早停止并返回 status=
     *         Canceled（部分 findings 保留、finalized=false、无错误诊断——
     *         POL-EVAL-6"取消不产正式证据"）
     */
    virtual bool cancellationRequested() const = 0;

    /**
     * @brief 运行/快照上下文是否仍存活（§6.1 迟到调用拒绝的事实源）。
     *
     * @return true＝存活（评估可产出正式输出）；false＝已失效（快照已废弃/
     *         运行已结束——评估返回 Failed＋POLICY-CLL-CONTEXT-EXPIRED，
     *         finalized=false，POL-LATE-1）
     */
    virtual bool alive() const = 0;
};

}  // namespace sdurws::ird::policy

#endif  // SDURWS_IRD_POLICY_CONTEXTS_HPP
