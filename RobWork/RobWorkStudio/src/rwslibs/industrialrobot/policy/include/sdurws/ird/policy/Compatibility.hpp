/**
 * @file   Compatibility.hpp
 * @brief  策略兼容判定（Compatibility）——消费方要求 × 已发布策略的纯函数
 *         判定：版本/后端/数值锚/模式适用性的 Incompatible 原因全量收集。
 *
 * 设计依据：
 *   - units/policy.md §8.1（版本要素表＋PolicyConsumerRequirements/
 *     PolicyCompatibility/checkPolicyCompatibility/IPolicyCompatibilityChecker
 *     冻结签名——本头是其类型层逐字承载）、§8.2（变化分类：schema 主版本
 *     变化→schema-future/obsolete 拒绝；碰撞后端版本变化→backend-mismatch
 *     拒绝；数值契约锚变化→numeric-contract-mismatch 拒绝；判定不抛异常
 *     ——判定是查询非违约）、§9.5（IPolicyCompatibilityChecker 契约表）、
 *     §3.1 组成表（Compatibility.hpp 五实体——PolicyConsumerRequirements/
 *     PolicyIncompatibilityReason/PolicyCompatibility/checkPolicyCompatibility/
 *     IPolicyCompatibilityChecker；本头不增删实体）
 *   - 需求 CON-04（缓存/检查点按契约判断兼容）、CON-05（内容身份参与缓存键
 *     ——本判定供身份素材，切片不命中判定归 evidence judgeCacheHit）、
 *     NFR-DEP-05（碰撞后端版本基线——复现要素）、ERR-01/UX-03（诊断三要素
 *     ——比较型仅数值判定）、CON-02（不兼容只拒绝**新复用**，历史结果永不
 *     删除——本判定无任何删除通道，§9.5 非法调用行）
 *   - 任务契约 tasks/foundation/POL-T09.json（≙WP-07-T09）acceptance 1/2：
 *     POL-COMPAT-1/2 版本/后端/锚失配全量 reason 用例；O-20/P-POL-5 保守口径
 *
 * 背景说明（本判定在产品中的角色）：域评估器/缓存接纳方（execution、
 * worker 握手、域评估器预检——§9.5 合法调用行）在消费一份已发布策略前，
 * 用本判定回答"这份策略在当前构建里能否按我的要求使用"。五项版本要素
 * （§8.1 表：策略 schema 代、策略内容身份、编译器契约版本、RobWork 版本、
 * 碰撞算法版本、数值契约锚）中，凡是**策略对象可携带**的（schema 代、
 * 数值契约锚、碰撞开关、适用模式）由本判定直接核对；**进程级**的后端事实
 * （当前唯一注册后端，§6.5）由实现以构建期冻结值核对——见
 * requiresBackend 字段注释。判定结果只声明"兼容/不兼容＋原因全量＋诊断"，
 * 不产生任何工程结论（§8.3——正式判定归 evidence），不影响历史结果
 * （CON-02——Superseded 不改写 payload，本判定是其上游素材供给方）。
 *
 * 实现口径登记（DTB §5.4——语义与 §8.1 冻结签名一致，细节精确化）：
 *   - C-1 全量不短路：七项检查全部执行，reasons 按检查序＝枚举声明序收集
 *     （evidence CacheHitReasons C-1 同款纪律——同失配必得同一清单，
 *     NFR-COR-02）；verdict＝reasons 非空即 Incompatible。
 *   - C-2 比较型三要素的适用面（UX-03"比较型校验〔限位、容差、裕量、预算
 *     等**数值**判定〕"原文）：schema 代失配为数值比较——comparison 携带
 *     实际/期望代（无量纲单位 "1"）；后端描述符与数值契约锚为**字符串**
 *     比较——core ComparativeValue 只承载数值＋已注册单位（DiagData.hpp），
 *     字符串侧以 cause 原文逐字承载（ERR-01"原因"字段），不伪造数值、不
 *     滥用 SourcedValue 四态（NotApplicable/Invalid 语义均不符）。本口径为
 *     policy.md §15.4 增量登记项。
 *   - C-3 policy-invalid 为防御臂：EngineeringPolicySet::make 发布门机械
 *     执行"只产出 Valid 实例"（PolicySet.hpp §4.5），经由公共 API 构造的
 *     策略对象恒为 Valid——该臂在当前类型层不可达，保留以兑现 §8.1 reason
 *     词表并为未来非门构造路径（若有）提供稳定码面；测试以词表全表用例
 *     钉住其 token（不可达性本身不可用公共 API 触发——测试文件头声明）。
 *   - C-4 后端核对对象（requiresBackend 注释详述）：当前进程唯一注册后端
 *     ＝运行构建的内置后端（§6.5"内置 ProximityStrategyRW 为默认且唯一
 *     注册后端"），其复现要素构建期冻结（§8.1 取值来源列"policy（构建期
 *     冻结）"）；backendVersion 暂取 RobWork 构建版本（RW_VERSION——
 *     P-POL-5/O-20 保守口径，**本单元不私定最终取值**，NFR-DEP-05 基线
 *     冻结后由 WP-24-T01 锁定回填）。
 *
 * 线程安全：本头全部实体为纯值/纯函数/无状态接口（§9.5"纯函数、可重入、
 * 无副作用；生命周期：无状态"），并发只读安全。
 * 确定性：七项检查为固定谓词（枚举等值/串精确等值/数值比较——附录 D 第
 * 12 项，无浮点容差参与），同输入同结论、reasons 同序（NFR-COR-02）。
 */

#ifndef SDURWS_IRD_POLICY_COMPATIBILITY_HPP
#define SDURWS_IRD_POLICY_COMPATIBILITY_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sdurws/ird/core/DiagData.hpp>
#include <sdurws/ird/core/Evaluation.hpp>

#include <sdurws/ird/policy/PolicyPort.hpp>
#include <sdurws/ird/policy/PolicySet.hpp>

namespace sdurws::ird::policy {

// =====================================================================
// PolicyIncompatibilityReason——不兼容原因词表（§8.1 reasons 注释行七值，
// kebab token；枚举序＝检查序——实现口径 C-1，一经交付只许表尾追加）。
// =====================================================================

/**
 * @brief 不兼容原因词表（§8.1 PolicyCompatibility.reasons 注释行原文七值）。
 *
 * token 与枚举一一对应（reasonToken()）；枚举顺序＝checkPolicyCompatibility
 * 的固定检查序（同失配必得同一清单——NFR-COR-02；实现口径 C-1）。一经
 * 交付不得改动/插入既有值——reason token 持久化于失效原因清单与诊断报告
 * （evidence Superseded 素材），稳定第一；新增原因只能表尾追加并同步
 * 单元卡增量修订（Errors.hpp 同款纪律）。
 *
 * 各值语义与触发条件（§8.2 变化分类表逐行）：
 *   - SchemaFuture                策略 schema 代高于消费方支持上限（消费方
 *                                 旧代码读新策略——schema-future，PM-06 只读拒绝）；
 *   - SchemaObsolete              策略 schema 代低于消费方支持下限（消费方
 *                                 新代码要求新代——schema-obsolete）；
 *   - CollisionRequiredDisabled   消费方需要碰撞能力而策略 collision.enabled=false
 *                                 （§8.1 注释行原文"策略 enabled=false → 不兼容"）；
 *   - BackendMismatch             消费方指定后端与当前注册后端复现要素不一致
 *                                 （§8.2"碰撞后端版本变化→拒绝"——backend-mismatch，
 *                                 NFR-DEP-05/CON-04）；
 *   - NumericContractMismatch     消费方指定数值契约锚与策略 numericContractAnchor
 *                                 不一致（§8.2"附录 D 修订→拒绝"——数值契约锚
 *                                 变化走需求变更）；
 *   - ModeNotApplicable           消费方目标模式不在策略适用范围（§8.1 评估
 *                                 模式行"适用性核对用"；策略 modes 空集＝全部
 *                                 适用，不触发本原因）；
 *   - PolicyInvalid               策略对象校验状态非 Valid（防御臂——实现口径
 *                                 C-3：发布门之下类型层不可达，保留码面）。
 */
enum class PolicyIncompatibilityReason {
    SchemaFuture,                ///< schema-future——策略代＞消费方上限
    SchemaObsolete,              ///< schema-obsolete——策略代＜消费方下限
    CollisionRequiredDisabled,   ///< collision-required-disabled——需要碰撞而策略禁用
    BackendMismatch,             ///< backend-mismatch——后端复现要素失配
    NumericContractMismatch,     ///< numeric-contract-mismatch——数值契约锚失配
    ModeNotApplicable,           ///< mode-not-applicable——目标模式不在适用范围
    PolicyInvalid,               ///< policy-invalid——策略对象非 Valid（防御臂）
};

/**
 * @brief 取不兼容原因的稳定 token（§8.1 reasons 注释行 kebab 词表原文）。
 *
 * @param reason [in] 不兼容原因（全枚举七值均有 token——全函数，永不返回空）
 * @return 稳定 token（"schema-future" 等七串之一；静态存储期，调用方无需释放）
 *
 * 确定性：编译期固定 switch 全枚举表（无 default——新增枚举值未登记表项时
 * 编译器告警暴露遗漏）；同原因同串、无 locale 依赖（NFR-COR-02）。token
 * 即 §8.1 注释行原文（"schema-future/schema-obsolete/collision-required-
 * disabled/backend-mismatch/numeric-contract-mismatch/mode-not-applicable/
 * policy-invalid"），持久化于失效原因清单——不改名。
 */
std::string_view reasonToken(PolicyIncompatibilityReason reason) noexcept;

// =====================================================================
// PolicyConsumerRequirements——消费方兼容要求（§8.1 冻结代码逐字段承载）。
// =====================================================================

/**
 * @brief 消费方（域评估器/缓存判定）的兼容要求（§8.1 冻结代码逐字段）。
 *
 * 使用方式：消费方按自身依赖声明要求（缺省值＝最宽松：schema 当前代、
 * 不需要碰撞、任意已注册后端、无数值锚核对、不限模式），再以
 * checkPolicyCompatibility(策略, 要求) 取判定。字段均为**要求侧**输入——
 * 判定函数不回写、不修改（纯函数，§9.5 副作用行）。
 *
 * 线程安全：纯值聚合。
 */
struct PolicyConsumerRequirements {
    /**
     * 消费方可接受的策略 schema 代闭区间（§8.1 冻结缺省 [1,1]＝当前代）。
     *
     * 语义（§8.2"schema 主版本变化"行）：策略代高于上限 → schema-future
     * （消费方不认识未来代——PM-06 只读拒绝）；低于下限 → schema-obsolete
     * （消费方已不再支持旧代）。上下限均为**支持闭区间**端点——等于边界
     * 即兼容（闭区间语义，不设隐藏容差）。
     */
    std::uint32_t minSchemaVersion = 1, maxSchemaVersion = 1;

    /**
     * 消费方是否需要碰撞能力（§8.1 冻结注释行："消费方需要碰撞能力
     * （策略 enabled=false → 不兼容）"）。缺省 false＝消费方不消费碰撞，
     * 策略开关不参与判定；true 且策略禁用碰撞 → CollisionRequiredDisabled
     * （消费方如轨迹复检 TRJ-04 依赖碰撞复检——禁用策略下不可履约）。
     */
    bool requiresCollision = false;

    /**
     * 消费方要求的后端复现要素（§8.1 冻结注释行："缺省=任意已注册后端"）。
     *
     * 核对对象（实现口径 C-4）：当前进程唯一注册后端＝运行构建的内置后端
     * （§6.5——本产品不引入第二碰撞算法，ARC-05；后端复现要素构建期冻结，
     * §8.1 取值来源列"policy（构建期冻结）"）。要求给出时逐字段精确比较
     * （CollisionBackendDescriptor::operator==——backendId/backendVersion/
     * toleranceModel 三串任一不等即 BackendMismatch；版本串比较为精确
     * 等值——附录 D 第 12 项，无容差）。缺省（nullopt）＝不指定后端，
     * 当前注册后端无论为何均可（"任意已注册后端"原文语义）。
     *
     * 版本口径（P-POL-5/O-20——acceptance 2）：当前注册后端的
     * backendVersion 暂取 RobWork 构建版本（RW_VERSION）；NFR-DEP-05
     * 基线冻结后由 WP-24-T01 锁定回填——本单元不私定最终取值（消费方
     * 声明要求时应取自 evaluator->backend()/复现块同源值，§9.1）。
     */
    std::optional<CollisionBackendDescriptor> requiresBackend;

    /**
     * 消费方要求的数值契约锚（§8.1 冻结注释行："附录 D 基线锚核对"）。
     *
     * 给出时与策略 numericContractAnchor 精确等值比较（锚串形如
     * "appendixD@v1.16"——策略默认 kNumericContractAnchorDefault）；
     * 不一致 → NumericContractMismatch（§8.2"数值契约锚变化"行：附录 D
     * 修订走需求变更——新旧锚下的数值语义不同，拒绝复用是保守正确方向）。
     * 缺省（nullopt）＝不核对锚。
     */
    std::optional<std::string> requiresNumericContract;

    /**
     * 消费方的目标评估模式（§8.1 冻结注释行："目标模式适用性核对"）。
     *
     * 给出时核对策略适用范围（PolicyApplicability::modes）：策略 modes 为
     * 空集＝全部模式适用（§4.2.1 空集语义——不触发）；非空且不含目标模式
     * → ModeNotApplicable。模式只是适用性核对输入（§8.1 评估模式行"模式
     * 不是评估输入"——R-POL-5：评估 API 无模式参数，本字段只做消费前预检）。
     * 缺省（nullopt）＝不限模式。
     */
    std::optional<core::EvaluationMode> mode;
};

// =====================================================================
// PolicyCompatibility——判定结果（§8.1 冻结代码逐字段承载）。
// =====================================================================

/**
 * @brief 兼容判定结果（§8.1 冻结代码：verdict＋原因全量＋诊断全量）。
 *
 * 不变式（实现口径 C-1 的结果面）：reasons 为空 ⇔ verdict==Compatible ⇔
 * diagnostics 为空；reasons 与 diagnostics 按下标一一对应（每条原因恰好
 * 一条 POLICY-VERSION-INCOMPATIBLE 诊断，§8.2 诊断行），且顺序＝枚举
 * 声明序（检查序）的子序列。
 *
 * 错误语义（§8.2 末段原文）：判定是查询非违约——**不抛异常**；不兼容
 * 只影响新复用，历史结果永不删除（CON-02——本类型无任何删除/改写通道）。
 *
 * 线程安全：纯值。
 */
struct PolicyCompatibility {
    /**
     * 判定结论（§8.1 冻结枚举两值）：Compatible＝全部要求满足（可复用/
     * 可消费）；Incompatible＝至少一项失配（reasons 全量列出）。
     */
    enum Verdict { Compatible, Incompatible } verdict;

    /// 不兼容原因全量（检查序——见 PolicyIncompatibilityReason 注释；
    /// Compatible 时为空）。
    std::vector<PolicyIncompatibilityReason> reasons;

    /// 随附诊断全量（与 reasons 下标一一对应；码面 POLICY-VERSION-INCOMPATIBLE
    /// ——§8.2"比较型：实际/期望"＋建议动作；Compatible 时为空）。码值权威
    /// 归 diagnostics StableCodeRegistry（PA-1）——本单元只产出建议码原文，
    /// POL-T10 Diagnostics.hpp 落位后迁入码表（JointLimits.cpp 同款迁移通道）。
    std::vector<core::DiagnosticRecord> diagnostics;
};

// =====================================================================
// checkPolicyCompatibility——判定纯函数（§8.1 冻结签名；§9.5 契约表）。
// =====================================================================

/**
 * @brief 策略兼容判定（§8.1 冻结签名——纯函数；§9.5 契约表逐行）。
 *
 * 判定序（固定——实现口径 C-1；与枚举声明序一致）：
 *   ① policy-invalid（防御）→ ② schema-future → ③ schema-obsolete →
 *   ④ collision-required-disabled → ⑤ backend-mismatch →
 *   ⑥ numeric-contract-mismatch → ⑦ mode-not-applicable。
 *   全部执行、全量收集、不首错短路；verdict＝reasons 非空即 Incompatible。
 *
 * @param policy       [in] 已发布策略对象（§9.5 前置行"policy 为已发布对象"
 *                     ——发布门产出的 Valid 实例；防御臂之外的核对不依赖
 *                     对象来源，只读消费）
 * @param requirements [in] 消费方兼容要求（缺省值＝最宽松要求；本函数不
 *                     修改入参——纯函数）
 * @return 判定结果（verdict＋reasons＋diagnostics；不变式见
 *         PolicyCompatibility 注释）
 *
 * @throws 无（§9.5 错误行"不抛（判定是查询）"——任何输入组合都返回结构
 *         完整的结果；字符串比较为 std::string 精确等值，无异常路径）
 *
 * 复杂度：O(检查项数)＝O(1)（模式适用性核对为三值词表线性扫描，上界
 * 常数）；无堆分配热点（诊断串构造除外——判定频率为每会话/每缓存接纳
 * 一次，非热路径）。
 *
 * 线程/确定性/副作用（§9.5）：纯函数、可重入、无副作用；同输入同结果
 * （reasons 同序、diagnostics 逐字节相等——NFR-COR-02）。
 */
PolicyCompatibility checkPolicyCompatibility(const EngineeringPolicySet& policy,
                                             const PolicyConsumerRequirements& requirements);

// =====================================================================
// IPolicyCompatibilityChecker——注入形态接口（§8.1 冻结代码逐字承载）。
// =====================================================================

/**
 * @brief 兼容判定的注入形态（§8.1 冻结代码："无状态；自由函数即可满足，
 *        接口供宿主装配"）。
 *
 * 职责边界：本接口**不是**第二判定实现——实现方（宿主装配的适配器，
 * 归 L5/请求方）应把 check() 委托给自由函数 checkPolicyCompatibility
 * （单一判定权威，PA-1；自建第二套判定逻辑＝旁路，评审打回）。接口存在
 * 的意义：消费方若面向接口编程（如替身注入测试、宿主统一装配面），可在
 * 不依赖自由函数符号的前提下消费判定（§8.1 注入形态行原文）。
 *
 * 契约（§9.5 契约表逐行）：前置——policy 为已发布对象；后置——判定纯
 * 函数结果（Compatible/Incompatible＋reasons＋诊断全量）；错误——不抛
 * （判定是查询）；线程/确定性/副作用——纯函数、可重入、无副作用；生命
 * 周期——无状态（实现不得携带可变成员）。
 * 非法调用：以 Incompatible 结果删除历史结果（§9.5 非法调用行——历史
 * 保留 CON-02；本判定仅拒绝**新复用**）。
 *
 * 线程安全：实现须并发只读安全（无状态）。
 */
class IPolicyCompatibilityChecker {
public:
    virtual ~IPolicyCompatibilityChecker() = default;

    /**
     * @brief 执行兼容判定（语义与自由函数同一——实现须委托单一权威）。
     *
     * @param policy       [in] 已发布策略对象（只读消费）
     * @param requirements [in] 消费方兼容要求（只读消费）
     * @return 判定结果（与 checkPolicyCompatibility 同契约——不抛、全量、
     *         确定性）
     */
    virtual PolicyCompatibility check(const EngineeringPolicySet& policy,
                                      const PolicyConsumerRequirements& requirements) const = 0;
};

}  // namespace sdurws::ird::policy

#endif  // SDURWS_IRD_POLICY_COMPATIBILITY_HPP
