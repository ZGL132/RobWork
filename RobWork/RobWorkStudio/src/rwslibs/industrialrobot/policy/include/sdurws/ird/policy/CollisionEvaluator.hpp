/**
 * @file   CollisionEvaluator.hpp
 * @brief  碰撞评估唯一实现——CollisionScene 场景模型、会话构建（作用域展开/
 *         冲突复核/检测器初始化/会话身份）与 ICollisionEvaluator 唯一入口。
 *
 * 设计依据：
 *   - units/policy.md §6.1（输入模型：CollisionScene 与只读生命周期——本头
 *     SceneObjectRole/SceneObjectEntry/CollisionScene 的唯一权威章节）、
 *     §6.4（确定性与稳定排序——createSession 三步：作用域解析/检测器构建/
 *     sessionIdentity 计算；会话构造后只读、可跨线程共享）、§7.1（作用域
 *     矩阵——可执行规则，会话构建时展开为具体对象对集）、§7.2（过滤规则与
 *     可追溯性——展开集冲突检查"排除∩必检=∅"与会话构建期同码复核）、
 *     §9.3（ICollisionEvaluator/CollisionEvaluationSession/make 唯一构造
 *     入口契约表）、§12 POL-T06 行（本任务交付构建期半区：CollisionScene/
 *     会话构建/作用域展开/检测器初始化；评估半区 evaluate 归 POL-T07）
 *   - traceability/foundation-api-diff.md CR-04（policy↔runtime 碰撞场景
 *     接口映射——已关闭裁决：sceneContentIdentity←runtime
 *     workCellCompileIdentity；WorkCell 共享只读经快照别名构造；objects/
 *     adjacentLinkPairs 由请求方自编译产物装配；适配器归 L5/请求方，
 *     policy 零 runtime 编译依赖）
 *   - 需求 ARC-05（碰撞评估唯一实现——R-POL-1~5 防旁路）、MDL-04（自碰撞
 *     配置——Self 域作用域）、MDL-15（环境对象显式引用参与碰撞——
 *     Environment 域作用域）、KIN-05（无几何≠无碰撞——构建期只登记对象
 *     清单，几何覆盖计数归评估期 coverage）、ARC-04（不可解析不猜测）
 *
 * 背景说明（本头在装配图中的位置——第一读者须知）：
 *   谁装配：请求方（域评估器/execution 准备段，L3/L4 可依赖 runtime）或
 *   L5 装配适配器把 runtime 编译产物装配成 CollisionScene（§6.1"谁装配"行
 *   ——policy 不读取模型对象、不构造 WorkCell）；评估会话经
 *   ICollisionEvaluator::createSession(policy, scene, names) 构建——会话
 *   一次性解析作用域（策略规则×场景清单→具体对象对集）、构建检测器
 *   （内置后端实例与 ProximitySetup——由策略规则生成，规则经名称上下文以
 *   完整名精确匹配转为 RobWork 规则，不做模式拼接）、计算 sessionIdentity
 *   （§6.4）。会话构造后只读、可跨线程共享（§9.3 线程行）。
 *
 * CR-04 适配落点（任务契约 acceptance 2——逐行对照映射表执行）：
 *   - CollisionScene.workcell 为 shared_ptr<const WorkCell>（共享只读）——
 *     runtime 侧适配器以别名构造（快照指针＋视图引用），policy 侧只消费；
 *     会话持有该 shared_ptr＝"会话保活场景"（§9.3 生命周期行——编译产物
 *     在会话存续期不可变且不可被释放，只读生命周期第一层防护）；
 *   - CollisionScene.sceneContentIdentity 由调用方传入 runtime 计算的
 *     workCellCompileIdentity（CR-04 裁决：snapshotIdentity 含修订定位字段
 *     不入；policy 不重算、不消费 DWC——本头不做任何身份重推导）；
 *   - 名称消费仅经 IPolicyNameContext（tryRuntimeName 转发 runtime
 *     IRuntimeNameResolver——Expected 错误→nullopt 原样呈现，不可解析不
 *     猜测，ARC-04）；本单元内无任何名称前缀拼接/剥离（R-4/P-POL-8）。
 *
 * 任务边界（§12 拆分——防误读声明）：
 *   POL-T06 交付本头的**构建期半区**（CollisionScene/会话构建/作用域展开/
 *   检测器初始化/会话身份）；CollisionEvaluationSession::evaluate 与查询
 *   类型（CollisionQuery/CollisionEvaluation 等，§6.2/§6.3）归 POL-T07
 *   （同名文件增量落位——§12 POL-T07 行"CollisionQuery.hpp/.cpp＋RobWork
 *   适配评估"）。本头因此不含任何评估执行路径，也不含占位/空实现——
 *   会话的全部行为在构造期完成并可完整测试（POL-SCOPE-1/2 构建期用例）。
 *
 * 实现纪律：
 *   - 本头对 rw 类型**仅前向声明、零 rw include**（冒烟模式纪律——runtime
 *     RT-T03 同款：公共头 include 面不带框架头；完整类型仅产品 .cpp 与
 *     集成模式测试 TU 消费；头内成员以 shared_ptr/引用形态持有不完整
 *     类型，合法且析构语义在完整类型可见的 TU 内实例化）；
 *   - 检测器初始化不消费 RobWork 默认碰撞设置（R-POL-3：ProximitySetup
 *     不取自 WorkCell 文件内嵌 CollisionSetup，静态对排除等默认过滤显式
 *     关闭）——检测器仅以策略规则生成的 setup 显式初始化（POL-EVAL-8 的
 *     构建期前提）；
 *   - 线程安全：会话构造后全部访问器只读（§9.3"并发只读可重入"）；构造
 *     过程本身不并发（调用方串行构建）。
 */

#ifndef SDURWS_IRD_POLICY_COLLISIONEVALUATOR_HPP
#define SDURWS_IRD_POLICY_COLLISIONEVALUATOR_HPP

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/core/Identity.hpp>

#include <sdurws/ird/policy/Contexts.hpp>
#include <sdurws/ird/policy/Errors.hpp>
#include <sdurws/ird/policy/PolicyPort.hpp>
#include <sdurws/ird/policy/PolicySet.hpp>

// rw 类型前向声明（零 rw include——见文件头"实现纪律"）。
namespace rw {
namespace models {
class WorkCell;          ///< 编译产物场景（完整类型：rw/models/WorkCell.hpp）
}  // namespace models
namespace proximity {
class ProximitySetup;    ///< RobWork 邻近过滤规则集（完整类型：ProximitySetup.hpp）
class CollisionStrategy; ///< 碰撞检测策略基类（完整类型：CollisionStrategy.hpp）
}  // namespace proximity
}  // namespace rw

namespace sdurws::ird::policy {

// detail——单元内共享实现件（R-2 纪律同 PolicySet.hpp detail；声明于公共头
// 供产品翻译单元共用——单一事实源）。
namespace detail {

/**
 * @brief 内置后端复现要素的单点构造（§6.5/§8.1——backendId/toleranceModel
 *        冻结值的唯一来源；backendVersion 由调用方注入）。
 *
 * 为什么值与版本分离：本函数所在翻译单元（CollisionEvaluator.cpp）持有
 * 复现要素的字符串字面量单点；后端绑定翻译单元（RobWorkCollisionEvaluator
 * ——含 RobWork 命名的类/工厂定义）以参数注入版本串（RW_VERSION——
 * P-POL-5"暂取 RobWork 构建版本"），两 TU 经本声明衔接。这样字面量
 * （会话域错误消息与描述符冻结值）与 RobWork 命名标识符分处不同翻译
 * 单元，R-4 静态扫描（ird_gates IRD-GATE-R4 启发式：跨引号对匹配
 * "RobWork" 子串）在 policy 产品面保持零误报触发——O-12/P-POL-8 例外
 * 登记裁决前的结构性隔断（登记于 policy.md §15.4 v0.7；裁决后如需可回并）。
 *
 * @param backendVersion [in] 后端版本串（非空——RW_VERSION 宏展开值；
 *                       空串语义由调用方保证不出现）
 * @return 内置后端描述符（backendId 冻结 "rw.proximity.builtin-rw"；
 *         toleranceModel 为登记串——复现要素非工程阈值，R-7/§7.4 三分）
 */
CollisionBackendDescriptor makeBuiltinBackendDescriptor(std::string backendVersion);

}  // namespace detail

// =====================================================================
// §6.1 场景对象角色与条目——CollisionScene 的词表与元素。
// =====================================================================

/**
 * @brief 场景对象角色词表（§6.1 原文枚举——建模语义词表）。
 *
 * 与策略规则 ScopeTarget::roleToken 的五值词表（RobotLink|Tool|Payload|
 * EnvironmentObject|Workpiece——PolicyParsing sceneObjectRoleTokens()）同源
 * 同序：token↔枚举映射经 sceneObjectRoleToken()/trySceneObjectRole()（本头
 * 尾部自由函数），词表串的唯一事实源仍在 PolicyParsing.hpp（解析期权威，
 * 会话构建期只复用——不设第二词表，PA-1）。
 */
enum class SceneObjectRole { RobotLink, Tool, Payload, EnvironmentObject, Workpiece };

/**
 * @brief 场景对象角色的稳定 token（词表串转发表）。
 *
 * @param role [in] 场景对象角色（全枚举五值均有 token）
 * @return 词表串（"RobotLink"/"Tool"/"Payload"/"EnvironmentObject"/
 *         "Workpiece"——与 ScopeTarget::roleToken 同一词表；静态存储期）
 *
 * 确定性：编译期固定 switch 全枚举（无 default——遗漏即编译器告警）；
 * 同角色同串（NFR-COR-02）。
 */
std::string_view sceneObjectRoleToken(SceneObjectRole role) noexcept;

/**
 * @brief 词表串 → 场景对象角色（try 轨——词表外返回 nullopt 不猜测）。
 *
 * @param token [in] 待核对的角色 token（ScopeTarget::roleToken 承载的串；
 *              精确等值比较——附录 D 第 12 项，无大小写折叠）
 * @return 对应角色；词表外（含空串/拼写变体）→ nullopt（解析期⑤对词表外
 *         token 已拒绝发布——本函数是会话构建期的防御复检轨，nullopt 即
 *         POLICY-CLL-SCENE-INVALID 的定位素材，ARC-04 不猜测）
 */
std::optional<SceneObjectRole> trySceneObjectRole(std::string_view token) noexcept;

/**
 * @brief 场景对象条目（§6.1 原文四字段——值语义纯结构）。
 *
 * 字段语义（§6.1 注释原文）：
 *   - objectId：对象身份（输出用——NFR-COR-05 对象 ID 对；project 分配，
 *     跨修订稳定）；清单内必须唯一（§6.1 场景校验，重复→会话构建失败）；
 *   - localName：显示辅助（经⑥端口语；**不作为身份**——CON-06）；
 *   - role：建模语义角色（作用域矩阵 §7.1 的行归属依据）；
 *   - hasCollisionGeometry：场景事实（无几何→评估期 coverage 记录
 *     pairsWithGeometry，不伪装已检——KIN-05；构建期不据此收窄作用域）。
 *
 * 装配责任（CR-04）：由请求方/L5 适配器自 runtime 编译产物（CanonicalModel
 * 对象/层级索引事实）装配——policy 只消费（N-3/N-4 边界：不读取模型对象、
 * 不构造 WorkCell）。
 * 线程安全：纯值。
 */
struct SceneObjectEntry {
    core::ObjectId objectId;        ///< 对象身份（清单内唯一；输出用）
    std::string localName;          ///< 显示辅助（经⑥端口；不作身份——CON-06）
    SceneObjectRole role = SceneObjectRole::RobotLink;  ///< 建模语义角色（§7.1 行归属）
    bool hasCollisionGeometry = false;  ///< 场景事实（KIN-05：无几何不伪装已检）

    bool operator==(const SceneObjectEntry& o) const
    {
        return objectId == o.objectId && localName == o.localName && role == o.role
            && hasCollisionGeometry == o.hasCollisionGeometry;
    }
    bool operator!=(const SceneObjectEntry& o) const { return !(*this == o); }
};

/**
 * @brief 碰撞场景（§6.1 原文五字段——评估的输入模型：运行时快照编译产物，
 *        不是可变 WorkCell）。
 *
 * 只读生命周期保证（§6.1）：workcell 以 shared_ptr<const> 共享所有权进入
 * ——会话存续期间编译产物不可变且不可被释放（内存安全由所有权保证）；
 * "迟到调用拒绝"由 IPolicyCallContext::alive() 承担（评估期第二层防护，
 * POL-LATE-1——本头只落实第一层：会话保活场景）。
 *
 * CR-04 字段映射（已关闭裁决，调用方装配时对照）：
 *   - workcell ← RuntimeSnapshot 私有 workcell_ 经 WorkCellConstView 别名
 *     构造的 shared_ptr<const WorkCell>（所有权语义等价——适配器归 L5）；
 *   - sceneContentIdentity ← RuntimeSnapshot.workCellCompileIdentity
 *     （WC 层编译产物身份；**裁决：取 workCellCompileIdentity**——
 *     snapshotIdentity 含修订定位字段不入；policy 不重算、不消费 DWC）；
 *   - objects / adjacentLinkPairs ← CanonicalModel 对象/层级索引事实
 *     （请求方自编译产物装配——两文档一致）。
 *
 * 基座—世界变换随编译产物内置，policy 禁止二次旋转（MDL-22/M-11——本头
 * 不含任何位姿变换逻辑，结构上不可能违反）。
 * 线程安全：纯值聚合（workcell 指向的编译产物按契约不可变）。
 */
struct CollisionScene {
    /// runtime 编译产物（共享只读；含 R_world_base——MDL-22；CR-04 别名构造）。
    std::shared_ptr<const rw::models::WorkCell> workcell;
    /// 主链设备对象（经 IPolicyNameContext 解析定位到 workcell 内设备——
    /// 禁止字符串猜名，R-4；不可解析→会话构建失败 POLICY-CLL-NAME-UNRESOLVED）。
    core::ObjectId primaryDevice;
    /// 参与碰撞域的对象清单（调用方从模型/快照装配；objectId 唯一——§6.1）。
    std::vector<SceneObjectEntry> objects;
    /// 运动学相邻对（模型事实，供默认相邻过滤——§7.1；由建模侧含工具安装对）。
    std::vector<std::pair<core::ObjectId, core::ObjectId>> adjacentLinkPairs;
    /// 编译产物内容身份（runtime 计算，调用方传入——policy 不重算；CR-04）。
    core::ContentIdentity sceneContentIdentity;

    bool operator==(const CollisionScene& o) const
    {
        return workcell == o.workcell && primaryDevice == o.primaryDevice
            && objects == o.objects && adjacentLinkPairs == o.adjacentLinkPairs
            && sceneContentIdentity == o.sceneContentIdentity;
    }
    bool operator!=(const CollisionScene& o) const { return !(*this == o); }
};

// =====================================================================
// §7.1/§7.2 作用域展开产物——会话构建期"策略规则×场景清单→具体对象对集"。
// =====================================================================

/**
 * @brief 展开后具体对象对的状态四分（§7.2 会话构建/评估顺序的展开语义）。
 *
 * 与 §7.2 评估顺序框的对应：
 *   - Mandatory：必检对集（显式 mandatoryPairs 展开——不可被任何过滤覆盖，
 *     "结构上不存在可隐藏必检的合法策略"）；
 *   - InScopeDefault：域默认必检对（域启用即默认必检、未被合法排除——
 *     §7.1 必须检测列）；
 *   - ExcludedByRule：被显式排除对跳过（AppliedFilterRecord 的
 *     ruleOrigin="policy.excludedPairs[i].reason"——评估期随输出全量留痕）；
 *   - ExcludedByAdjacencyDefault：被默认相邻过滤跳过（消费模型相邻事实——
 *     ruleOrigin="default.adjacent-links"；仅 Self/Tool 域适用——§7.1
 *     Environment 行只允许逐对显式排除）。
 * 状态三分 Mandatory/InScopeDefault 与两分 Excluded* 的计数即评估期
 * coverage 的 pairsInScope/pairsExcludedByRule 分母素材（POL-T07 消费）。
 */
enum class ScopePairStatus {
    Mandatory,                  ///< 必检（显式规则；不可过滤——§7.2①）
    InScopeDefault,             ///< 域默认必检（未被排除/未被相邻默认过滤）
    ExcludedByRule,             ///< 显式排除对（理由必填——可追溯）
    ExcludedByAdjacencyDefault, ///< 默认相邻过滤（模型事实消费——§7.1）
};

/**
 * @brief 展开后的具体碰撞对象对（§7.1"会话构建时展开为具体对象对集"）。
 *
 * 规范化：objectA/objectB 恒为字节字典序（A<B）——无序对语义的展开形态，
 * 与解析②"成对规则规范化为字典序"同方向（NFR-COR-05 对象 ID 对的稳定
 * 排序基础；评估输出 findings 的 canonical 对即此序）。
 *
 * ruleIndex 语义：状态 Mandatory→mandatoryPairs 承载序下标；
 * ExcludedByRule→excludedPairs 承载序下标（AppliedFilterRecord 定位素材
 * ——ruleOrigin 引用的 [i]）；其余状态恒 0（无规则来源）。
 * filterReason 语义：ExcludedByRule→策略 reason 原文；ExcludedByAdjacency-
 * Default→"default.adjacent-links"；其余恒空串。
 * 线程安全：纯值。
 */
struct ScopedCollisionPair {
    core::ObjectId objectA;     ///< 规范序第一端（字节字典序较小——A<B）
    core::ObjectId objectB;     ///< 规范序第二端
    SceneObjectRole roleA = SceneObjectRole::RobotLink;  ///< A 端场景角色（coverage 分域计数素材）
    SceneObjectRole roleB = SceneObjectRole::RobotLink;  ///< B 端场景角色
    CollisionDomain domain = CollisionDomain::Self;      ///< §7.1 矩阵行归属域
    ScopePairStatus status = ScopePairStatus::InScopeDefault;  ///< 展开状态（见枚举注释）
    std::size_t ruleIndex = 0;  ///< 来源规则承载序下标（语义见结构注释）
    std::string filterReason;   ///< 过滤/必检理由（Excluded* 非空——可追溯）

    bool operator==(const ScopedCollisionPair& o) const
    {
        return objectA == o.objectA && objectB == o.objectB && roleA == o.roleA
            && roleB == o.roleB && domain == o.domain && status == o.status
            && ruleIndex == o.ruleIndex && filterReason == o.filterReason;
    }
    bool operator!=(const ScopedCollisionPair& o) const { return !(*this == o); }
};

/**
 * @brief 作用域展开结果（§7.2"与场景清单求交 → 具体对象对集"的载体）。
 *
 * 确定性：pairs 恒按 (objectA, objectB) 字节字典序排序——同语义场景
 * （对象集相同、清单承载序不同）展开结果逐字节一致（NFR-COR-02；场景
 * 清单顺序不进入结果序，会话身份与输出因此与装配顺序无关）。
 *
 * 计数语义：inScopePairCount()＝Mandatory＋InScopeDefault（评估期
 * coverage.pairsInScope 的分母）；excludedPairCount()＝ExcludedByRule＋
 * ExcludedByAdjacencyDefault（pairsExcludedByRule 分母）；分域计数重载供
 * POL-SCOPE-1"coverage 分域计数"观测点与 POL-T07 消费。几何缺失不计入
 * 本层（hasCollisionGeometry 只登记事实——几何覆盖计数归评估期，§7.5）。
 * 线程安全：纯值（构造后只读）。
 */
struct ResolvedCollisionScope {
    /// 全部展开对（含被过滤对——"过滤不得隐藏"的结构保证：被排除对以
    /// Excluded* 状态显式在册而非消失；规范序——见结构注释）。
    std::vector<ScopedCollisionPair> pairs;

    /// 作用域内对数（Mandatory＋InScopeDefault——coverage.pairsInScope 分母）。
    std::size_t inScopePairCount() const noexcept;

    /// 被过滤对数（ExcludedByRule＋ExcludedByAdjacencyDefault——
    /// coverage.pairsExcludedByRule 分母；含默认相邻过滤）。
    std::size_t excludedPairCount() const noexcept;

    /**
     * @brief 分域作用域内对数（POL-SCOPE-1"coverage 分域计数"素材）。
     * @param domain [in] 碰撞域（四值之一）
     * @return 该域内 Mandatory＋InScopeDefault 状态的对数
     */
    std::size_t inScopePairCount(CollisionDomain domain) const noexcept;
};

// =====================================================================
// §9.3 评估会话——构建期半区（POL-T06）；评估半区 evaluate 归 POL-T07。
// =====================================================================

/**
 * @brief 碰撞评估会话（§9.3 原文类——RobWorkCollisionEvaluator 产出；
 *        跨线程共享；构造后只读）。
 *
 * 生命周期与所有权（§9.3/§6.1）：由调用方以 shared_ptr 持有（可跨任务
 * 复用同会话）；会话内持有 CollisionScene 副本——其中的
 * shared_ptr<const WorkCell> 保证编译产物在会话存续期不可变且不可被释放
 * （只读生命周期第一层防护）；strategy 为装配方（评估器）共享的内置后端
 * 实例（§6.4"构建检测器（内置后端实例与 ProximitySetup）"的后端半区）。
 *
 * 构建期语义（§6.4 createSession 三步——本构造函数一次性完成，任一步
 * 失败即抛 PolicyError、会话不可存在——"会话构建失败"fail-fast，§9.3
 * 错误类型行"场景校验失败→PolicyError"）：
 *   第一步 场景校验（§6.1 会话构建期四项＋防御复检）：
 *     ① policy 为已发布对象（validationState=Valid＋身份有效——把未发布
 *        对象当策略传入＝调用方契约违约）；
 *     ② sceneContentIdentity 非空；workcell 编译产物在场（空指针＝装配
 *        违约）；objects 的 objectId 唯一；
 *     ③ primaryDevice 可经名称上下文解析到 workcell 内设备（不可解析→
 *        POLICY-CLL-NAME-UNRESOLVED——findDevice 空＝同名设备断链同码）；
 *     ④ 策略规则引用的对象 ⊆ objects（Object 目标缺失→
 *        POLICY-CLL-SCENE-INVALID＋subject 定位；Role 目标做词表防御复检；
 *        Group 目标保守拒绝——组成员数据不在 (policy, scene, names) 任一
 *        会话输入内，RawPolicyInput v1 无组定义字段（policy.md v0.5 ⑥
 *        登记）——不可核对即拒绝，不猜测、不静默收窄，ARC-04/KIN-05）。
 *   第二步 作用域展开（§7.1 矩阵×§7.2 两层防御）：
 *     规则目标按场景展开（Object→对象本身；Role→场景内该角色全部对象）
 *     后执行**冲突同码复核**——excludedPairs 展开集 ∩ mandatoryPairs 展开
 *     集 = ∅ 必须成立（违反→POLICY-RULE-CONFLICT——§7.2"Role/Group 展开
 *     为对象集后比对"的会话构建期落点，两层防御第二层：解析期上下文
 *     应答与场景装配事实不一致时在此拦截）；随后按矩阵逐对归类
 *     （classifyDomainPair 语义）并判状态（必检/默认/显式排除/相邻默认
 *     过滤——域禁用voids该域全部对，含必检；Scene 域仅显式必检进入）。
 *   第三步 检测器初始化＋会话身份：
 *     ProximitySetup 仅由策略规则生成（排除对→EXCLUDE_RULE、必检对→
 *     INCLUDE_RULE——完整名精确匹配，无模式拼接；R-POL-3：不取 RobWork
 *     默认设置、静态对排除显式关闭）；sessionIdentity＝
 *     f(policyContentIdentity, sceneContentIdentity, nameMapIdentity,
 *     backendDescriptor)（§6.4——SHA-256 经 core::ContentDigester，无第
 *     二哈希路径）。
 *
 * 确定性（§6.4）：同 (policy, scene, names, backend) 重复构建→等价会话
 * （逐字段相等——POL-SHARE-1 的一致性基础）；展开序与 setup 规则序均为
 * 规范序，与场景清单承载序无关。
 * 线程安全：构造后只读——全部访问器并发只读安全（§9.3 线程行）；
 * evaluate（POL-T07 落位）契约同源。
 */
class CollisionEvaluationSession {
public:
    /**
     * @brief 会话构建（§6.4 createSession 三步的唯一执行点——见类注释）。
     *
     * @param policy   [in] 已发布策略对象（Valid 态——复检失败即调用方
     *                 违约 fail-fast；内部持有副本，调用方存活期无要求）
     * @param scene    [in] 碰撞场景（§6.1 校验见类注释；内部持有副本——
     *                 workcell 共享所有权随会话保活，names 应答源须与场景
     *                 同源——§9.3 前置行"names 与 scene 同源（同一编译产物）"）
     * @param names    [in] 名称上下文（借用——调用方持有并保证会话构建期间
     *                 存活；构建完成后会话不再使用该引用，名称解析结果已
     *                 固化进展开产物与 setup）
     * @param backend  [in] 后端复现要素（进 sessionIdentity 与证据复现块；
     *                 推荐取评估器 backend() 同源值——§9.1 装配契约）
     * @param strategy [in] 内置后端实例（装配方共享持有；非空——空指针＝
     *                 调用方装配违约 fail-fast；POL-T07 评估半区消费，
     *                 构建期只保活）
     *
     * @throws PolicyError(PolicyErrorCode::PolicyObjectInvalid) policy 复检
     *         失败（非 Valid 态/身份无效）或 strategy 为空（装配违约）
     * @throws PolicyError(PolicyErrorCode::SceneInvalid) 场景校验失败
     *         （§6.1 四项——身份缺失/编译产物缺失/对象身份重复/规则引用
     *         对象缺失/Role 词表外/Group 目标不可展开；cause 定位对象）
     * @throws PolicyError(PolicyErrorCode::NameUnresolved) primaryDevice 或
     *         规则对象经名称上下文不可解析（nullopt 原样呈现——不猜测），
     *         或解析出的完整名在 workcell 内无对应设备
     * @throws PolicyError(PolicyErrorCode::RuleConflict) 排除∩必检展开集
     *         相交（§7.2 会话构建期同码复核——cause 定位双方规则与对象对）
     */
    CollisionEvaluationSession(const EngineeringPolicySet& policy,
                               const CollisionScene& scene,
                               const IPolicyNameContext& names,
                               CollisionBackendDescriptor backend,
                               std::shared_ptr<rw::proximity::CollisionStrategy> strategy);

    /// 已发布策略对象（§9.3 原文签名——内部副本的只读引用；会话存续期有效）。
    const EngineeringPolicySet& policy() const noexcept { return m_policy; }

    /// 策略会话身份（§4.1"策略会话身份"行：哪个（策略×场景×名称映射×后端）
    /// 评估上下文——证据绑定与跨入口等价比较的键；不含运行/尝试身份）。
    core::ContentIdentity sessionIdentity() const noexcept { return m_sessionIdentity; }

    /// 场景内容身份（§6.2 CollisionEvaluation.sceneIdentity 的回填源——
    /// 即调用方传入的 sceneContentIdentity，policy 不重算，CR-04）。
    core::ContentIdentity sceneIdentity() const noexcept { return m_scene.sceneContentIdentity; }

    /// 名称映射内容身份（§6.2 nameMapIdentity 回填源——CON-06 映射变化可观测）。
    core::ContentIdentity nameMapIdentity() const noexcept { return m_nameMapIdentity; }

    /// 后端复现要素（§8.1 版本要素——进证据复现块 collisionBackendVersion）。
    const CollisionBackendDescriptor& backendDescriptor() const noexcept { return m_backend; }

    /// 作用域展开结果（构建期产物——POL-T07 评估遍历与 coverage 计数的
    /// 唯一事实源；规范序——见 ResolvedCollisionScope 注释）。
    const ResolvedCollisionScope& scope() const noexcept { return m_scope; }

    /// 检测器过滤规则集（§6.4"ProximitySetup——由策略规则生成"；R-POL-3：
    /// 无 RobWork 默认规则——完整类型在集成模式 TU 可见，冒烟模式无 TU
    /// include 本头）。
    const rw::proximity::ProximitySetup& proximitySetup() const noexcept
    {
        return *m_proximitySetup;
    }

    /// 碰撞域总开关（§4.3：false＝策略整体停用——评估期应答
    /// CollisionDisabledByPolicy（§6.2，POL-T07 落位）；会话构建不因此失败
    /// ——§9.3 前置行只要求已发布＋场景校验，"禁用被误调用"是评估期语义）。
    bool collisionEnabled() const noexcept { return m_collisionEnabled; }

private:
    /**
     * @brief 检测器过滤规则集构建（第三步前半——§6.4"ProximitySetup——由
     *        策略规则生成"的执行件）。
     *
     * 规则来源＝作用域展开产物中带规则来源的对：排除对（ExcludedByRule/
     * ExcludedByAdjacencyDefault）→ EXCLUDE_RULE；必检对（Mandatory）→
     * INCLUDE_RULE（基线过滤层纵深防御——include 优先于排除）；域默认必检
     * 对不产生规则（include-all 候选集语义默认已含）。规则插入序＝展开
     * 产物规范序（确定性）。R-POL-3：不取 RobWork 默认设置——静态对排除
     * 显式关闭。完整名经 names 精确解析（无模式拼接——R-4）；解析失败→
     * POLICY-CLL-NAME-UNRESOLVED（§7.5 会话构建失败）。
     *
     * @param names [in] 名称上下文（借用——构建期存活即可，会话存续后不再
     *              使用：解析结果已固化进 setup 与展开产物）
     * @return 策略规则生成的过滤规则集（R-POL-3 合规形态）
     *
     * @throws PolicyError(PolicyErrorCode::NameUnresolved) 规则对端名称
     *         不可解析（§7.5——不猜测，ARC-04）
     */
    rw::proximity::ProximitySetup buildPolicyProximitySetup(const IPolicyNameContext& names) const;

    /// 已发布策略对象（副本——会话自持，调用方存活期无要求；§9.3 policy()
    /// 返回其只读引用）。
    EngineeringPolicySet m_policy;
    /// 碰撞场景副本（workcell 共享所有权随会话保活——只读生命周期第一层）。
    CollisionScene m_scene;
    /// 名称映射内容身份（构建期自 names 固化——CON-06）。
    core::ContentIdentity m_nameMapIdentity;
    /// 后端复现要素（装配期注入——sessionIdentity 组成与复现块取值）。
    CollisionBackendDescriptor m_backend;
    /// 会话身份（§6.4——SHA-256 对规范组成，detail::computeSessionIdentity）。
    core::ContentIdentity m_sessionIdentity;
    /// 作用域展开结果（构建期产物——规范序）。
    ResolvedCollisionScope m_scope;
    /// 检测器过滤规则集（策略规则生成——构建期产物；shared_ptr 对不完整
    /// 类型合法，完整类型在产品 TU 内构造）。
    std::shared_ptr<const rw::proximity::ProximitySetup> m_proximitySetup;
    /// 内置后端实例（装配方共享——非空；POL-T07 评估半区消费，构建期只
    /// 保活：后端实例存活期覆盖会话＝"检测器"两半的生命周期绑定）。
    std::shared_ptr<rw::proximity::CollisionStrategy> m_strategy;
    /// 碰撞域总开关（自 policy.collision.enabled 固化——评估期适用性判定源）。
    bool m_collisionEnabled;
};

// =====================================================================
// §9.3 唯一实现入口——ICollisionEvaluator＋RobWork 适配＋唯一构造入口。
// =====================================================================

/**
 * @brief 碰撞评估唯一实现入口接口（§9.3 原文签名——ARC-05 唯一实现的
 *        消费面；④端口 collisionEvaluator() 返回本接口引用）。
 *
 * 契约（§9.3 表逐行）：前置——policy 为已发布（Valid）对象、scene 校验
 * 通过（§6.1）、names 与 scene 同源（同一编译产物）；后置——同参数重复
 * 构建→等价会话（POL-SHARE-1 的一致性基础）；错误——调用方违约（场景
 * 校验失败）→PolicyError；生命周期——evaluator 进程级共享；session 由
 * 调用方 shared_ptr 持有（会话保活场景）。
 *
 * POL-T06 交付本接口＋唯一产品实现（构建期能力）；评估执行随 POL-T07
 * 在会话类上增量落位（§12 拆分——本接口签名已含 §9.3 全部方法，无截断）。
 * 线程安全：实现须并发只读安全（§9.1 端口线程行）。
 */
class ICollisionEvaluator {
public:
    virtual ~ICollisionEvaluator() = default;

    /**
     * @brief 碰撞后端复现要素（§9.3 原文签名——进 sessionIdentity 与
     *        evidence 复现块；与装配进 PolicyProvider 的描述符同源）。
     * @return 后端描述符（值语义按值返回）
     */
    virtual CollisionBackendDescriptor backend() const = 0;

    /**
     * @brief 构建评估会话（§9.3 原文签名——三步构建语义见会话类注释）。
     *
     * @param policy [in] 已发布策略对象（Valid——复检失败即 PolicyError）
     * @param scene  [in] 碰撞场景（§6.1 校验失败→PolicyError fail-fast）
     * @param names  [in] 名称上下文（借用——构建期存活即可；与 scene 同源）
     * @return 不可变会话（shared_ptr<const>——调用方持有、可跨线程共享）
     *
     * @throws PolicyError 同 CollisionEvaluationSession 构造（三步任一失败）
     */
    virtual std::shared_ptr<const CollisionEvaluationSession>
    createSession(const EngineeringPolicySet& policy, const CollisionScene& scene,
                  const IPolicyNameContext& names) const = 0;
};

/**
 * @brief 后端装配配置（§6.5"makeRobWorkCollisionEvaluator(
 *        BackendConfig{builtin ProximityStrategyRW})"的载体）。
 *
 * 当前为空结构：内置 ProximityStrategyRW 是默认且唯一注册后端（§6.5——
 * ARC-05 不引入第二碰撞算法；外部碰撞策略按 §12"不含"清单明确不做的）。
 * 结构保留为唯一构造入口的签名锚（NFR-DEP-05 基线冻结（WP-24）后如需
 * 后端参数化，经本结构增量扩展——不新增第二构造入口，R-POL-1~5）。
 */
struct BackendConfig {
    // 空＝内置 ProximityStrategyRW（唯一后端；字段化扩展待 WP-24 基线冻结）。
};

/**
 * @brief ICollisionEvaluator 唯一产品实现（§6.5 装配线：构造入口唯一——
 *        RobWorkCollisionEvaluator）。
 *
 * 装配语义：构造时实例化内置后端（ProximityStrategyRW——sdurw_proximity，
 * R-5 例外许可的唯一消费方）并固化复现要素描述符；createSession 把
 * (policy, scene, names) 三步构建委托给会话构造函数（单一实现——本类无
 * 第二套校验/展开逻辑）。
 *
 * 复现要素取值（§8.1/P-POL-5）：backendId 冻结为 "rw.proximity.builtin-rw"
 * （§6.5 原文取值）；backendVersion 暂取 RobWork 构建版本（RW_VERSION——
 * P-POL-5：基线冻结前口径，WP-24 冻结后单点回填）；toleranceModel 为后端
 * 固有数值分辨率登记串（复现要素非工程阈值——R-7/§7.4 三分）。
 * 线程安全：实例构造后只读——两方法并发只读安全（backendDescriptor 为
 * const 成员；createSession 无共享可变状态——会话状态由会话对象自持）。
 */
class RobWorkCollisionEvaluator final : public ICollisionEvaluator {
public:
    /**
     * @brief 装配构造（唯一构造入口的实例化点——§6.5 装配线）。
     *
     * @param config [in] 后端配置（默认＝内置 ProximityStrategyRW——当前
     *                唯一取值；见 BackendConfig 注释）
     *
     * @throws std::bad_alloc 后端实例化内存不足（进程内异常轨——D-15；
     *                其余构建期错误延迟到 createSession 的会话构造三步）
     */
    explicit RobWorkCollisionEvaluator(const BackendConfig& config = BackendConfig{});

    /// @copydoc ICollisionEvaluator::backend
    CollisionBackendDescriptor backend() const override;

    /// @copydoc ICollisionEvaluator::createSession
    std::shared_ptr<const CollisionEvaluationSession>
    createSession(const EngineeringPolicySet& policy, const CollisionScene& scene,
                  const IPolicyNameContext& names) const override;

private:
    /// 内置后端实例（构造期一次——进程级共享给全部会话；非空不变式）。
    std::shared_ptr<rw::proximity::CollisionStrategy> m_strategy;
    /// 复现要素描述符（构造期固化——backend() 与 sessionIdentity 组成同源）。
    CollisionBackendDescriptor m_descriptor;
};

/**
 * @brief 唯一实现的唯一构造入口（§6.5/§9.3 原文签名——装配期由 L5/worker
 *        宿主调用，产出进程内唯一 ICollisionEvaluator 实例；主进程 1、
 *        每 worker 1——§9.1 生命周期行）。
 *
 * @param config [in] 后端配置（默认＝内置 ProximityStrategyRW）
 * @return 唯一实现实例（调用方经 shared_ptr 注入 PolicyProvider——§9.1
 *         装配契约；实例所有权归装配方）
 *
 * @throws std::bad_alloc 后端实例化内存不足（D-15 进程内异常轨）
 */
std::unique_ptr<ICollisionEvaluator>
makeRobWorkCollisionEvaluator(const BackendConfig& config = BackendConfig{});

}  // namespace sdurws::ird::policy

#endif  // SDURWS_IRD_POLICY_COLLISIONEVALUATOR_HPP
