/**
 * @file   PolicyPort.hpp
 * @brief  ④策略端口（ARCH §7.2④）——IPolicyProvider 契约、
 *         CollisionBackendDescriptor 复现要素载体与记忆化 Provider 实现。
 *
 * 设计依据：
 *   - units/policy.md §9.1（IPolicyProvider 接口契约——本头签名的唯一权威
 *     章节：前置/后置/错误/线程/确定性/副作用/生命周期/调用示例/非法调用
 *     逐行）、§3.1（组成表：PolicyPort.hpp＝PolicyResolutionRequest/
 *     PolicyResolution、CollisionBackendDescriptor、IPolicyProvider）、
 *     §6.5（唯一实现的装配、共享与防旁路——装配线 PolicyProvider(bytesSource,
 *     nameContext 适配器, evaluator)）、§12 POL-T05 行（本头＋PolicyPort.cpp
 *     为 POL-T05 产物：PolicyProvider 实现＋记忆化）
 *   - 需求 ARC-05（策略单一权威/碰撞评估唯一实现共享——④端口是域插件/
 *     execution/project 处理器取得策略与评估器的**唯一**通道）、UX-08（显示
 *     单位切换不进身份——端口交付的策略身份只随语义闭包，POL-ID-1/3 联动）、
 *     KIN-13（近限位阈值随策略对象传递——评估 API 无阈值参数，R-POL-5）
 *   - 任务契约 tasks/foundation/POL-T05.json（≙WP-07-T05）acceptance 1～3：
 *     ①注入接口与端口契约用例通过；②缓存一致（POL-ID-1 联动）；③CR-04/
 *     P-POL-9 处置约束——最小注入接口（Contexts.hpp）、适配器归 L5/ui 装配、
 *     本任务零 runtime/project 编译依赖（R-1/R-2）
 *
 * 背景说明（④端口在装配图中的位置——第一读者须知）：
 *   域插件（kinematics/trajectory/optimization/selection）与 execution 准备段
 *   不直接触碰项目存储，也不自行解析策略——它们经本端口一次取得两件事：
 *   resolvePolicy 给出"已解析、已校验、已发布"的 EngineeringPolicySet（含
 *   语义内容身份），collisionEvaluator()/collisionBackend() 给出进程内唯一
 *   碰撞评估实现及其复现要素（§6.5 装配图：三业务入口同一 evaluator 实例
 *   ——ARC-05"不得有重复算法"的消费侧保证，POL-SHARE-1 钉住）。
 *   端口实例由 L5/worker 宿主在装配期构造并注册为④端口唯一实现（§9.1
 *   生命周期行：主进程 1、每 worker 1；消费者持引用、不拥有；装配期注册、
 *   运行期不变——无"替换端口"API，§9.1 非法调用行）。
 *
 * 记忆化契约（§9.1 后置条件行——POL-ID-1 的端口级落点）：
 *   同 (policyObject, ContentVersion) 重复调用返回**逐字段相等**策略（身份
 *   必然一致）。实现为按该二元组的 memo 缓存：命中直接返回缓存值（天然
 *   逐字段相等）；未命中经"取字节→解码→七段解析管线"计算后入缓存。缓存
 *   收益的确定性前提＝内容编址（CON-05）：同版本字节恒同、解析为纯函数
 *   （POL-T04 管线契约）、闭包应答在同键下稳定——三者由装配侧保证（见
 *   PolicyProvider 构造函数注释的装配前提）。
 *
 * CR-04/P-POL-9 处置约束落点（任务契约 acceptance 3）：
 *   - 本头对 ICollisionEvaluator 只做**前置声明**——其完整定义归
 *     CollisionEvaluator.hpp（POL-T06，§3.1 组成表）；本头不包含任何
 *     runtime/project 头（R-1/R-2；BuildRedLineTest 机械钉住）；
 *   - 注入接口（IPolicyBytesSource/IPolicyValidationContext）只在本头**被
 *     消费**，适配器一律归 L5/ui 装配（P-POL-9 宿主＝ui，WP-10-T07）。
 *
 * 线程安全：IPolicyProvider 全部方法并发只读安全（§9.1 线程行"缓存内部
 * 同步"）——PolicyProvider 以互斥量保护 memo 表（逻辑 const：mutable）。
 * 确定性：同请求同应答（解析纯函数＋记忆化，§9.1 确定性行）。
 */

#ifndef SDURWS_IRD_POLICY_POLICYPORT_HPP
#define SDURWS_IRD_POLICY_POLICYPORT_HPP

#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <sdurws/ird/core/DiagData.hpp>
#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/core/Identity.hpp>

#include <sdurws/ird/policy/Contexts.hpp>
#include <sdurws/ird/policy/Errors.hpp>
#include <sdurws/ird/policy/PolicyParsing.hpp>
#include <sdurws/ird/policy/PolicySet.hpp>

namespace sdurws::ird::policy {

// 前置声明：碰撞评估器唯一实现入口接口（§3.1 归属 CollisionEvaluator.hpp，
// POL-T06 落地完整定义）。本头仅以引用/共享指针形态消费——指针与引用的
// 声明、传递、解引用传递均不需完整类型；④端口的评估器半区因此可在
// POL-T06 之前完成接口冻结（业务单元可先面向本端口编程）。
class ICollisionEvaluator;

// =====================================================================
// CollisionBackendDescriptor——碰撞后端复现要素（§3.1 组成表归属本头；
// §8.1 版本要素行给出字段集，§7.4/R-7 给出 toleranceModel 语义）。
// =====================================================================

/**
 * @brief 碰撞后端身份与复现要素载体（§8.1 版本要素表"RobWork 版本/碰撞
 *        算法版本"两行的取值来源）。
 *
 * 三个字段是什么、谁在何时填（§8.1 行 707 原文取值）：
 *   - backendId：后端标识。内置唯一后端取 "rw.proximity.builtin-rw"
 *     （§6.5：内置 ProximityStrategyRW 为默认且**唯一**注册后端——ARC-05
 *     不引入第二碰撞算法；取值由 POL-T06 唯一构造入口冻结）。
 *   - backendVersion：后端版本串＝RobWork 基线版本（NFR-DEP-05 冻结版本
 *     基线的组成部分）。P-POL-5（未决项）登记：基线冻结前暂取 RobWork
 *     构建版本，WP-24 冻结后锁定取值并回填单元卡——本结构只承载值，
 *     不锁定取值来源（PA-1：版本口径变更走设计变更评审）。
 *   - toleranceModel：后端固有数值分辨率（内部容差模型）的登记串。语义
 *     边界（R-7/§7.4 三分表）：它只是**复现要素**——记录"该后端在多细的
 *     分辨率上判定相交/距离"，绝不进入工程判定；工程阈值唯一来自策略
 *     （无隐式阈值——POL-EVAL-8 钉住）。
 *
 * 去向：进入 sessionIdentity 组成（§6.5：sessionIdentity=f(policy, scene,
 * names, backend)）与 evidence ReproductionBlock.collisionBackendVersion
 * （§3.2 文档级对齐行——版本串为其取值来源）。
 *
 * 线程安全：纯值（不可变使用约定——装配期构造后不再修改）。
 */
struct CollisionBackendDescriptor {
    /// 后端标识（内置后端＝"rw.proximity.builtin-rw"，§6.5/§8.1）。
    std::string backendId;
    /// 后端版本串（＝RobWork 基线版本；P-POL-5：基线冻结前暂取构建版本）。
    std::string backendVersion;
    /// 后端固有数值分辨率登记（复现要素——非工程阈值，R-7/§7.4 三分）。
    std::string toleranceModel;

    /// 逐字段精确相等（复现要素比对用——版本串差异即不相等，无容差）。
    bool operator==(const CollisionBackendDescriptor& o) const noexcept
    {
        return backendId == o.backendId && backendVersion == o.backendVersion
            && toleranceModel == o.toleranceModel;
    }
    bool operator!=(const CollisionBackendDescriptor& o) const noexcept
    {
        return !(*this == o);
    }
};

// =====================================================================
// PolicyResolutionRequest / PolicyResolution——④端口请求与结果载体
// （§9.1 原文字段，值语义纯聚合）。
// =====================================================================

/**
 * @brief 策略解析请求（§9.1 原文两字段）。
 *
 * 字段语义与调用前置（§9.1 前置条件行）：
 *   - policyObject：策略对象身份（project 分配的持久化对象——ObjectId）。
 *     全零保留值＝调用方契约违约（④端口 fail-fast 拒绝——与解析管线对
 *     RawPolicyInput.policyObject 的复检同款纪律，POL-T04 先例）。
 *   - expectedVersion：期望内容版本（可选）。**若给**，须为修订闭包内版本
 *     （§9.1 前置行原文）。调用流程（§10.1）：调用方先经 project ②端口取得
 *     策略对象引用 (oid, cv)，再持该二元组发起请求——内容编址解析（CON-05）
 *     是本端口**唯一**服务形态；未携带版本（nullopt）的请求不可编址取数，
 *     端口返回空 policy＋POLICY-OBJECT-MISSING 诊断（不抛——§9.1 错误类型
 *     行"版本不符"族的可恢复输入条件；见 PolicyProvider::resolvePolicy 注释
 *     的错误矩阵）。
 *
 * 线程安全：纯值。
 */
struct PolicyResolutionRequest {
    /// 策略对象身份（全零＝调用方契约违约——fail-fast）。
    core::ObjectId policyObject;
    /// 期望内容版本（nullopt＝未指定——不可编址取数，诊断轨拒绝）。
    std::optional<core::ContentVersion> expectedVersion;
};

/**
 * @brief 策略解析结果（§9.1 原文两字段）。
 *
 * 不变式（§9.1 错误类型行——错误语义的载体约定）：
 *   - policy 非空 ⇔ 本次请求成功解析并发布（diagnostics 此时仅 Info 级——
 *     POLICY-INFO-DEFAULT-APPLIED 等，随发布对象附带）；
 *   - policy 为空时 diagnostics **必非空**且全量（存储侧条件→
 *     POLICY-OBJECT-MISSING；字节层条件→decode 稳定码转发——
 *     POLICY-SCHEMA-VERSION-FUTURE/-UNKNOWN、POLICY-ENCODING-INVALID；
 *     策略内容非法→解析管线 POLICY-* 全量诊断不短路——POL-T04 管线契约
 *     在端口的延续）；不抛（环境/输入条件走诊断轨——只有调用方契约违约
 *     fail-fast，见 PolicyProvider::resolvePolicy 错误矩阵）。
 *
 * 与 PolicyParseResult 的关系：解析管线的 PolicyParseResult.sourceVersion
 * 由 Provider 在记忆化键处填充（POL-T04 登记的 PA-1 落点）；端口结果本体
 * 不再携带版本字段——§9.1 冻结面只有两字段，版本信息经诊断文案定位。
 *
 * 线程安全：纯值（按值持有；EngineeringPolicySet 为不可变对象）。
 */
struct PolicyResolution {
    /// 已发布策略对象（成功时非空；无效/缺失/版本不符时空＋诊断）。
    std::optional<EngineeringPolicySet> policy;
    /// 全量诊断（成功时仅 Info 级；失败时非空且不短路）。
    std::vector<core::DiagnosticRecord> diagnostics;

    /// 逐字段精确相等（记忆化后置条件"逐字段相等"的可断言载体——测试与
    /// 缓存自检用；无容差，NFR-COR-02 同款口径）。
    bool operator==(const PolicyResolution& o) const
    {
        return policy == o.policy && diagnostics == o.diagnostics;
    }
    bool operator!=(const PolicyResolution& o) const { return !(*this == o); }
};

// =====================================================================
// IPolicyProvider——④策略端口接口（§9.1 原文三方法；ARCH §7.2④）。
// =====================================================================

/**
 * @brief ④策略端口（ARCH §7.2④）：策略解析与碰撞评估唯一实现的进程级
 *        共享入口。
 *
 * 契约表（units/policy.md §9.1——逐行权威，此处只列实现要点）：
 *   - 前置：实例由 L5/worker 宿主装配注入（bytesSource＋nameContext 适配器
 *     就绪——§6.5 装配线；评估器半区由宿主注入，见 PolicyProvider 构造
 *     注释）；expectedVersion（若给）须为修订闭包内版本；
 *   - 后置：同 (policyObject, ContentVersion) 重复调用返回逐字段相等策略
 *     （记忆化——POL-ID-1）；collisionEvaluator() 每次返回同一实例引用；
 *   - 错误：对象缺失/版本不符→空 policy＋POLICY-SCHEMA-*／存储侧诊断转发
 *     （不抛）；实现内部异常→PolicyError；
 *   - 线程：全部方法并发只读安全（缓存内部同步）；确定性：是（解析纯函数
 *     ＋记忆化）；副作用：无外部副作用、内部缓存；
 *   - 生命周期：端口实例进程级单例（主进程 1、每 worker 1）；消费者持
 *     引用、不拥有；装配期注册、运行期不变（无替换 API）。
 *
 * 合法调用示例（§9.1 原文）：`auto res = provider.resolvePolicy({policyOid,
 * cv}); if (res.policy) { … }`（域插件/处理器/execution 准备段）。
 * 非法调用（§9.1 原文）：以 RawPolicyInput 冒充已解析策略传入快照组装
 * （类型层不可表达）；运行期替换端口实例（无此 API）。
 */
class IPolicyProvider {
public:
    virtual ~IPolicyProvider() = default;

    /**
     * @brief 解析策略对象（记忆化）——④端口的主通道（§9.1 原文签名）。
     *
     * @param request [in] 解析请求（对象身份＋可选期望版本——见结构注释）
     * @return 解析结果：成功＝policy＋Info 级诊断；对象缺失/版本不符/字节
     *         损坏/策略内容非法＝空 policy＋全量诊断（不抛）；调用方契约
     *         违约（全零对象身份/全零版本）＝抛 PolicyError（fail-fast）
     *
     * @throws PolicyError(PolicyErrorCode::PolicyObjectInvalid) policyObject
     *         为全零保留值，或 expectedVersion 已给但为全零保留值（调用方
     *         契约违约——合法编址请求不可能产生，静默转诊断会掩盖装配侧 bug）
     */
    virtual PolicyResolution resolvePolicy(const PolicyResolutionRequest& request) const = 0;

    /**
     * @brief 共享碰撞评估唯一实现实例（§9.1 原文签名——返回同一引用）。
     *
     * 后置：每次返回**同一实例**引用（§9.1 后置行；POL-SHARE-1 的实例
     * 同一性断言在此基础上成立——三业务入口同一 evaluator）。
     * 前置：宿主已在装配期注入评估器半区（§6.5 装配线）；未装配即调用＝
     * 调用方契约违约。
     *
     * @return 唯一实现的引用（消费者持引用、不拥有——端口进程级存活）
     *
     * @throws PolicyError(PolicyErrorCode::PortAssemblyIncomplete) 评估器
     *         半区尚未注入（装配未完成即对外服务——宿主装配期遗漏）
     */
    virtual ICollisionEvaluator& collisionEvaluator() const = 0;

    /**
     * @brief 碰撞后端复现要素供给（§9.1 原文签名——进 sessionIdentity 与
     *        evidence 复现块）。
     *
     * @return 装配期注入的后端描述符（§8.1 版本要素——值语义按值返回）
     */
    virtual CollisionBackendDescriptor collisionBackend() const = 0;
};

// =====================================================================
// PolicyProvider——④端口唯一实现（§12 POL-T05 产物：PolicyProvider 实现
// ＋记忆化）。
// =====================================================================

/**
 * @brief IPolicyProvider 的唯一产品实现：取字节→解码→七段解析管线→
 *        (policyObject, ContentVersion) 记忆化。
 *
 * 装配契约（§6.5 装配线＋§9.1 前置条件行的实现落点）：
 *   - 宿主在装配期构造本对象并注册为④端口唯一实现；全部依赖由宿主持有
 *     并保证存活期覆盖端口实例（端口进程级单例、运行期不变）；
 *   - **解析半区**（resolvePolicy 依赖）：bytesSource（对象字节——CR-03
 *     适配 project ②端口）＋validationContext（修订闭包查询——解析④⑤步
 *     的 objectExists/objectRole/groupDefined 应答源）。两者应答须满足：
 *     同键下稳定（内容编址下同 (对象, 版本) 字节恒同＝CON-05；闭包应答在
 *     同一修订闭包内恒定）——这是"记忆化结果＝重算结果"的确定性前提
 *     （§9.7 IPolicyValidationContext 行"应答确定性是 POL-ID-1 的前提"）；
 *   - **评估器半区**（collisionEvaluator/collisionBackend 依赖）：evaluator
 *     （唯一实现实例——宿主经 POL-T06 唯一构造入口 makeRobWorkCollision-
 *     Evaluator 创建）＋backend（复现要素——推荐取 evaluator->backend()
 *     同源值，保证 sessionIdentity 组成一致）。评估器半区可后于解析半区
 *     注入（构造时允许空——装配分步完成），但**注入前调用
 *     collisionEvaluator() 即契约违约**（fail-fast，PortAssemblyIncomplete）；
 *   - 与 §6.5 装配线的登记差异（DTB §5.4，随单元卡 v0.6 变更记录）：装配
 *     线原文 "PolicyProvider(bytesSource, nameContext 适配器, evaluator)"
 *     中的 nameContext 未进构造参数——本端口三方法的契约面均不消费名称
 *     映射（createSession(policy, scene, names) 由调用方显式传名，§9.3），
 *     持有不消费的接口指针违反最小装配面；名称上下文仍按 §9.1 前置行在
 *     宿主装配环境就绪，随会话构建（POL-T06）显式传递。另：解析④⑤步的
 *     闭包查询（validationContext）为 §9.7 已登记注入面，装配线示意未列。
 *
 * 记忆化语义（POL-ID-1 的端口级机制）：
 *   - 缓存键＝(policyObject, ContentVersion) 二元组（内容编址——CON-05：
 *     同版本字节恒同，键即完整输入的代理）；
 *   - 成功与失败**都**缓存：键下结果在进程内是纯函数值（字节/闭包应答
 *     稳定前提），缓存负结果使"同键重复调用逐字段相等"对失败情形同样
 *     成立（§9.1 后置行不限成功态）；存储侧后续物化（worker 场景）发生在
 *     端口服务之前（Contexts.hpp worker 行），不产生键下状态变化；
 *   - 并发未命中：允许多线程同时重算（解析为纯函数，结果相同），入表
 *     互斥量保护、先入为准——结果逐字段一致，仅多付一次计算（§9.1 线程行
 *     "缓存内部同步"的实现口径）；
 *   - 无淘汰策略：键空间＝进程消费的策略对象版本集，有界（内容编址下
 *     同键结果恒定，无失效问题——CON-05；进程级生命周期与端口一致）。
 *
 * 错误矩阵（resolvePolicy——§9.1 错误类型行的逐项落点）：
 *   1. policyObject 全零 / expectedVersion 已给但全零 → 抛
 *      PolicyError(PolicyObjectInvalid)（调用方契约违约——fail-fast，
 *      不入诊断轨；与解析管线对全零 policyObject 的复检同款）；
 *   2. expectedVersion 未给（nullopt）→ 空 policy＋POLICY-OBJECT-MISSING
 *      诊断（不可编址取数的可恢复输入条件——"版本不符"族，不抛）；
 *   3. 存储无该 (对象, 版本) 字节（bytesSource→nullopt）→ 空 policy＋
 *      POLICY-OBJECT-MISSING 诊断（对象缺失——环境事实，不抛；subject 绑
 *      定请求对象）；
 *   4. 字节损坏/字节层版本越代（decode 抛 PolicyError）→ 捕获转译为空
 *      policy＋同码面诊断（cause 携带异常全文——POLICY-SCHEMA-VERSION-
 *      FUTURE/-UNKNOWN、POLICY-ENCODING-INVALID；§9.1 错误行
 *      "POLICY-SCHEMA-*／存储侧诊断转发"的字节层落点：未来/未知代的策略
 *      对象在端口层同样"空 policy＋POLICY-SCHEMA-*＋升级指引"，不抛、
 *      不前向猜测解析）；
 *   5. 策略内容非法（解码成功、解析管线产出 Error 级诊断）→ 空 policy＋
 *      管线全量诊断原样转发（不短路——POL-T04 管线契约的端口延续）；
 *   6. 其余实现内部异常（如内存耗尽）→ 不捕获，按 §9.1"实现内部异常→
 *      PolicyError"语义向上传播（不吞错——NFR-COR-03）。
 *
 * 线程安全：全部方法并发只读安全（memo 表经 std::mutex 保护——mutable，
 * 逻辑 const；依赖对象的并发只读安全性由其自身契约保证——Contexts.hpp/
 * PolicyParsing.hpp 各自的线程行）。不可复制（互斥量成员——端口为进程级
 * 单例，复制无意义且破坏"运行期不变"）。
 */
class PolicyProvider final : public IPolicyProvider {
public:
    /**
     * @brief 装配构造（宿主装配期一次；§6.5 装配线的实现落点）。
     *
     * @param bytesSource       [in] 对象字节源（宿主持有并保证存活期覆盖本
     *                          端口；引用形态＝借用契约，端口不接管所有权——
     *                          CR-03 适配 project ②端口的装配侧适配器）
     * @param validationContext [in] 修订闭包查询（同上借用契约；应答须同键
     *                          稳定——记忆化确定性前提，见类注释装配契约）
     * @param evaluator         [in] 碰撞评估唯一实现实例（共享持有——与宿主
     *                          共享所有权；可为空＝评估器半区待注入〔装配
     *                          分步〕，注入前 collisionEvaluator() 契约违约）
     * @param backend           [in] 碰撞后端复现要素（推荐取 evaluator->
     *                          backend() 同源值——sessionIdentity 组成一致）
     *
     * 不抛（构造只存依赖——校验延迟到使用点：装配分步语义下"部分装配"
     * 是合法中间态，fail-fast 面在 collisionEvaluator() 与 resolvePolicy）。
     */
    PolicyProvider(const IPolicyBytesSource& bytesSource,
                   const IPolicyValidationContext& validationContext,
                   std::shared_ptr<ICollisionEvaluator> evaluator,
                   CollisionBackendDescriptor backend);

    /// @copydoc IPolicyProvider::resolvePolicy
    PolicyResolution resolvePolicy(const PolicyResolutionRequest& request) const override;

    /// @copydoc IPolicyProvider::collisionEvaluator
    ICollisionEvaluator& collisionEvaluator() const override;

    /// @copydoc IPolicyProvider::collisionBackend
    CollisionBackendDescriptor collisionBackend() const override;

private:
    /// 对象字节源（借用——宿主持有，存活期覆盖本端口；见构造函数契约）。
    const IPolicyBytesSource* m_bytesSource;
    /// 修订闭包查询（借用——同上；解析④⑤步 objectExists/objectRole/
    /// groupDefined 的应答源）。
    const IPolicyValidationContext* m_validationContext;
    /// 碰撞评估唯一实现（共享持有；空＝评估器半区待注入——装配分步）。
    /// shared_ptr 对不完整类型合法（删除器在宿主构造点捕获——T06 完整定义
    /// 处）；本类不触发其析构语义。
    std::shared_ptr<ICollisionEvaluator> m_evaluator;
    /// 碰撞后端复现要素（值语义——装配期注入后只读）。
    CollisionBackendDescriptor m_backend;

    /// 记忆化缓存键：内容编址二元组 (对象身份, 内容版本)——字节序字典序
    /// （Id128/Digest256 既有 operator<，确定性遍历序——NFR-COR-02）。
    using CacheKey = std::pair<core::ObjectId, core::ContentVersion>;

    /// 记忆化表（键下结果恒定——无淘汰；mutable＝逻辑 const 下的内部同步）。
    /// 非线程安全容器，全部访问经 m_cacheMutex（§9.1 线程行"缓存内部同步"）。
    mutable std::map<CacheKey, PolicyResolution> m_cache;
    /// 缓存互斥量（保护 m_cache；持锁时间＝查表/入表——解析在锁外执行，
    /// 不持锁调用外部依赖，避免锁传播）。
    mutable std::mutex m_cacheMutex;
};

}  // namespace sdurws::ird::policy

#endif  // SDURWS_IRD_POLICY_POLICYPORT_HPP
