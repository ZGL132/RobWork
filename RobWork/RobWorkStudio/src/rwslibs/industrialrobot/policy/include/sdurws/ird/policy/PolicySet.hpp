/**
 * @file   PolicySet.hpp
 * @brief  EngineeringPolicySet 数据模型——策略对象类型、碰撞规则与关节阈值
 *         子模型、四态承载与不可变发布契约。
 *
 * 设计依据：
 *   - units/policy.md §4 全章：§4.1 六个"策略/身份"概念区分、§4.2 字段表
 *     （全部字段构造后不可变，无 setter；值语义）＋§4.2.1 适用范围、§4.3
 *     碰撞规则子模型、§4.4 关节限位与行程阈值子模型、§4.5 未设置/无效/
 *     冲突/不适用四态、§4.6 可变性/版本/修订引用
 *   - 需求 ARC-05（策略单一权威、唯一默认＝附录 D 第 11 项）、CON-05/06
 *     （内容身份语义闭包）、NFR-COR-03（不静默吞错）、KIN-13（分析配置
 *     不得覆盖策略——显示/分析开关不存在于本类型）
 *   - 任务契约 tasks/foundation/POL-T02.json（≙WP-07-T02）acceptance 1～4
 *
 * 背景说明（本类型在产品中的角色）：EngineeringPolicySet 是"已解析、已
 * 校验、已发布"的策略对象——评估侧（碰撞/限位/兼容）唯一消费的策略真相
 * （PA-1：策略语义权威归 policy）。它由解析管线（resolvePolicy，POL-T04）
 * 的第⑦步产出；本头提供其类型层契约与发布门（make()），解析管线后续在其
 * 之上组装。RawPolicyInput（未解析输入，可携带显示单位与未设置字段）归
 * PolicyInput.hpp（POL-T03）——本头类型是"解析后"的形态：全部 SI 真值、
 * 无显示设置字段（UX-08 的数据层保证）。
 *
 * 四态承载（§4.5——贯穿本头的设计主轴，评审对照点）：
 *   1. 未设置（NotProvided）＝ std::optional 空（nearLimitRatio/
 *      conditionNumberWarning/safetyClearance）——有冻结默认→解析管线③
 *      填入（origin=DefaultAppendixD）；无冻结默认→保持 nullopt＝该检查
 *      显式不适用（P-POL-2/O-10 保守口径：**不发明数值**）；
 *   2. 无效（Invalid）＝ 已提供但非法（NaN/越域/语法错）——构造即抛
 *      PolicyError（fail-fast），主轨由解析诊断"拒绝发布"（POL-T04）；
 *   3. 冲突（Conflicting）＝ 规则集内部矛盾——解析期结构拒绝（§5.1④/
 *      §7.2，D-06：排除∩必检=∅ 强制），本头不重复实现（权威唯一）；
 *   4. 不适用（NotApplicable）＝ 字段级显式标记——空 optional 本身是语义
 *      内容（参与内容身份，§5.3），评估输出侧显式标记不伪造结论（POL-T08）。
 *
 * 不可变性（§4.6）：EngineeringPolicySet 全部数据成员为 const——构造后
 * 无任何修改途径（赋值被编译期删除，类型层保证而非约定）；"修改策略"＝
 * 新 RawPolicyInput → 重新解析 → 新内容身份 → 新修订（SA-03/PA-2）。
 * 子模型（CollisionRules/JointThresholds 等）为可变聚合值——它们是解析
 * 管线的中间载体（合法携带未设置字段）；其阈值槽位只能持有经 make() 域
 * 校验的 PolicyThreshold（"构造时校验有限性与符号"的类型层强制），发布门
 * （EngineeringPolicySet::make）再做一次全量结构校验。
 *
 * O-11/O-18 处置（契约 acceptance 3/4——登记类条款，不私裁）：
 *   - O-11（SEL-05 惯量比阈值归属，P-POL-3）：JointThresholds **不预填**
 *     惯量比字段——归属裁决在 selection 卡（WP-19-T01）；如裁决归 policy，
 *     走 schema 次版本追加可选字段（向后兼容，§4.6/§5.3 版本策略——
 *     kPolicySchemaVersionCurrent 即该通道的版本锚）；
 *   - O-18（耦合矩阵病态阈值并入建议，P-RT-7）：本 schema **不并入**——
 *     runtime 侧维持其设计默认；如裁决并入同样走次版本追加。
 *     二者均以测试钉住"字段不存在"（见 PolicySetTest O-11/O-18 用例）。
 *
 * 线程安全：本头全部实体为纯值类型（无共享可变状态），并发只读安全。
 * 确定性：阈值域校验为固定数值谓词（std::isfinite＋区间判断），同输入同
 * 结论（NFR-COR-02）；无 locale 依赖（诊断细节经 %.17g 格式化，"C" locale）。
 */

#ifndef SDURWS_IRD_POLICY_POLICYSET_HPP
#define SDURWS_IRD_POLICY_POLICYSET_HPP

#include <cstdint>
#include <cmath>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <sdurws/ird/core/DiagData.hpp>
#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/core/Evaluation.hpp>
#include <sdurws/ird/core/Identity.hpp>

#include <sdurws/ird/policy/Errors.hpp>

namespace sdurws::ird::policy {

// 前向声明（detail 校验器以 const 引用承接——完整类型在下方定义）。
struct ScopeTarget;
struct PairRule;

// =====================================================================
// 单元级冻结常量（§4.2 字段表默认列＋§4.4 唯一冻结默认）。
// =====================================================================

/**
 * 策略 schema 当前代（§4.1"策略 schema 版本"行：当前 1；§5.3 版本策略）。
 *
 * 归属说明：§5.3 的 PolicySchema 常量权威载体是 PolicyInput.hpp（POL-T03），
 * 其落地后将**复用**本常量（include 本头）而非重复定义——单一事实源。
 * 本常量在本头先行落地的原因：发布门（EngineeringPolicySet::make）的类型层
 * 校验需要"当前代"这一事实（发布对象只可能由当前代码产生——未来代实例在
 * 当前程序中不可存在）。
 *
 * O-11/O-18 版本通道（acceptance 3/4）：次版本追加可选字段＝向后兼容
 * （旧编码可解析，新字段按未设置处理）；主版本变更＝破坏性（走设计变更
 * 评审＋全量切片身份重算登记——schemaVersion 参与内容身份）。惯量比
 * （P-POL-3/O-11）与耦合矩阵病态阈值（P-RT-7/O-18）如裁决并入，均经本
 * 通道追加，不在本代预填。
 */
inline constexpr std::uint32_t kPolicySchemaVersionCurrent = 1;

/**
 * 数值契约基线锚默认值（§4.2 numericContractAnchor 行默认列）。
 *
 * 语义：本策略默认值所依据的附录 D 版本——参与内容身份与兼容判定（§8.1）。
 * "appendixD@v1.16"＝设计冻结时的需求文档基线（REQUIREMENTS v1.16）。
 */
inline constexpr std::string_view kNumericContractAnchorDefault = "appendixD@v1.16";

/**
 * 有限限位旋转关节行程上限默认值（§4.4：4π——附录 D 第 11 项，唯一由上游
 * 冻结的工程策略默认，origin=DefaultAppendixD）。
 *
 * 单位：SI rad（不是度）。数值表达：以 double π 字面量×4 计算（乘 4 是
 * 2 的幂缩放，无舍入误差），与"4π"的数学值在 IEEE754 下逐位一致；
 * P-POL-2/O-10 口径：这是**唯一**冻结默认——其余阈值不在此发明数值。
 */
inline constexpr double kDefaultFiniteRotationTravelLimit = 4.0 * 3.141592653589793;

// =====================================================================
// detail——子模型结构校验的共享谓词（非公共契约；R-2 纪律同 core detail）。
// 此处仅声明（类型尚不完整）；定义在本头后部（§4.3 类型之后）——发布门
// （EngineeringPolicySet::make）与各 make() 工厂共用，单一事实源。
// =====================================================================

namespace detail {

/**
 * @brief ScopeTarget 字段级结构校验声明（§4.3：kind 决定哪个字段有效）。
 *
 * 仅校验"kind↔字段"一致性（Object→对象身份有效；Role→角色 token 非空；
 * Group→组名非空）。角色词表核对（RobotLink|Tool|Payload|EnvironmentObject|
 * Workpiece——建模语义）与组存在性（IPolicyValidationContext）归解析管线
 * ⑤（POL-T04）——本层不越权（PA-1）。
 *
 * @throws PolicyError(PolicyErrorCode::PolicyObjectInvalid) 一致性违约
 */
void requireScopeTargetWellFormed(const ScopeTarget& t, std::string_view where);

/**
 * @brief PairRule 字段级结构校验声明（§4.3：reason 非空必填——可追溯性）。
 *
 * @throws PolicyError(PolicyErrorCode::PolicyObjectInvalid) reason 空或
 *         两侧 ScopeTarget 结构非法
 */
void requirePairRuleWellFormed(const PairRule& r, std::string_view where);

}  // namespace detail

// =====================================================================
// §4.4 阈值承载：PolicyValueOrigin（来源三分＋DefaultAppendixD）与
// PolicyThreshold（SI 真值＋来源；构造时校验有限性与符号）。
// =====================================================================

/**
 * @brief 阈值来源词表（§4.4 D-04：不新增 core ProvenanceKind——策略默认
 *        语义不合其五类模型数据来源，policy 局部词表避免污染全局契约）。
 *
 * token 语义（§5.1③ 默认值解析、§4.4 默认/显式/继承说明）：
 *   - Explicit          输入显式提供；
 *   - DefaultAppendixD  解析管线③填入的附录 D 冻结默认（当前仅行程上限 4π）；
 *   - DefaultTemplate   模板策略默认（数值待 P-POL-2 裁决——裁决前不产生）；
 *   - Inherited         新方案分支沿用基线修订的策略对象（project 修订引用语义）。
 */
enum class PolicyValueOrigin { Explicit, DefaultAppendixD, DefaultTemplate, Inherited };

/**
 * @brief 阈值域词表（实现细节——单元卡 v0.3 增量登记的实现件）。
 *
 * 为什么需要它：§4.4 规定"单位由字段位置固定（本表逐字段标注），构造时
 * 校验有限性与符号"——要把"符号/区间"校验放在构造时，构造点必须知道该
 * 阈值将被放进哪个字段（字段位置）；本枚举即"字段位置"的编码。它**不是**
 * §4.4 字段表新增数据（PolicyThreshold 的公开数据面仍是 {siValue, origin}），
 * 而是校验元数据：私有成员＋访问器，不参与任何未来 codec 编码（编码按
 * 字段位置展开，§5.3）。
 *
 * 各域的数值窗（§4.4 逐字段标注＋§5.2 行 4）：
 *   SafetyClearance            SI m，[0, +∞)——0 合法（退化为仅碰撞检测，§7.4）；
 *   NearLimitRatio             无量纲，(0, 1]；
 *   ConditionNumberWarning     无量纲，[1, +∞)；
 *   FiniteRotationTravelLimit  SI rad，(0, +∞)。
 */
enum class PolicyThresholdDomain {
    SafetyClearance,
    NearLimitRatio,
    ConditionNumberWarning,
    FiniteRotationTravelLimit,
};

/**
 * @brief 策略阈值统一承载（§4.4 原文契约：SI 真值＋来源）。
 *
 * 值语义与不可变封装：数据成员私有、无 setter——字段级修改在类型层不可
 * 表达（runtime CanonicalModel 同款"封装式不可变值类型"惯用法）；整体
 * 赋值合法但其赋值源只能是经 make() 校验的实例（私有构造使"未校验阈值"
 * 不可存在），故不存在绕过校验的改值途径。需要整体可赋值的原因：子模型
 * 的 optional 槽位装配（JointThresholds/CollisionRules::make）以赋值/移动
 * 放入阈值——const 成员会使槽位装配在类型层不可行（实测 MSVC optional
 * 删除赋值）。
 *
 * 构造纪律（§4.4"构造时校验有限性与符号"）：唯一构造入口是 make()——
 * 私有构造函数强制之；这同时是 NFR-COR-03 的落点（NaN/越界值不可能静默
 * 进入策略模型）。
 *
 * "值相等"含域元数据：数值相同但域不同的两个阈值（如同值的安全间距与
 * 行程上限）不相等——单位不同（m 对 rad），工程语义不同。
 *
 * 线程安全：纯值类型；错误路径抛 PolicyError（异常轨，进程内——D-15）。
 */
class PolicyThreshold {
public:
    /**
     * @brief 唯一构造入口：有限性＋域窗校验（§4.4/§5.2 行 3/4）。
     *
     * 校验序（固定——确定性 NFR-COR-02）：先有限性后域窗，域窗内先判
     * "非正"后判"越窗"。
     *
     * @param siValue [in] SI 真值；单位语义由 domain 承载（m/无量纲/rad）
     * @param origin  [in] 来源（四值之一；类型层不限制来源与数值的组合——
     *                "DefaultAppendixD 只允许 4π"是解析管线③的职责，
     *                本工厂不越权裁决模板数值〔P-POL-2〕）
     * @param domain  [in] 目标字段域（决定合法区间——见枚举注释）
     * @return 已校验阈值（封装不可变）
     *
     * @throws PolicyError(PolicyErrorCode::ThresholdNonFinite)
     *         siValue 为 NaN/±Inf（§5.2 行 3）
     * @throws PolicyError(PolicyErrorCode::ThresholdNonPositive)
     *         域窗要求为正而 siValue≤0（NearLimitRatio/ConditionNumberWarning/
     *         FiniteRotationTravelLimit）；或要求非负而 siValue＜0（SafetyClearance）
     * @throws PolicyError(PolicyErrorCode::ThresholdOutOfRange)
     *         siValue 为正但越出域窗（NearLimitRatio＞1；ConditionNumberWarning＜1）
     */
    static PolicyThreshold make(double siValue, PolicyValueOrigin origin,
                                PolicyThresholdDomain domain)
    {
        // 第一步：有限性（§5.2 行 3——NaN/±Inf 拒绝；std::isfinite 覆盖
        // NaN/+Inf/-Inf 三态）。命中即抛——不静默转 0/默认（NFR-COR-03；
        // 原文保留归 RawPolicyInput，POL-T03/T04）。
        if (!std::isfinite(siValue)) {
            throw PolicyError(PolicyErrorCode::ThresholdNonFinite,
                              std::string{"阈值非有限（NaN/±Inf）: "}
                                  + formatThreshold(siValue));
        }
        // 第二步：域窗校验（§5.2 行 4 两码分列——非正 vs 越窗）。
        // 各域合法窗见 PolicyThresholdDomain 注释；边界值归属 D-08：
        // SafetyClearance=0 合法（仅碰撞检测）、NearLimitRatio=1 合法、
        // ConditionNumberWarning=1 合法、行程上限无上界窗（仅＞0）。
        switch (domain) {
        case PolicyThresholdDomain::SafetyClearance:
            if (siValue < 0.0) {   // [0,+∞)：0 合法（§4.3"退化为仅碰撞检测"）
                throw PolicyError(PolicyErrorCode::ThresholdNonPositive,
                                  std::string{"安全间距须≥0 m，实际 "}
                                      + formatThreshold(siValue));
            }
            break;
        case PolicyThresholdDomain::NearLimitRatio:
            if (siValue <= 0.0) {  // (0,1]：0 与负值同码（要求为正）
                throw PolicyError(PolicyErrorCode::ThresholdNonPositive,
                                  std::string{"近限位比须>0，实际 "}
                                      + formatThreshold(siValue));
            }
            if (siValue > 1.0) {   // 上窗越界（POL-PARSE-2 原文例 1.5）
                throw PolicyError(PolicyErrorCode::ThresholdOutOfRange,
                                  std::string{"近限位比须≤1（无量纲），实际 "}
                                      + formatThreshold(siValue));
            }
            break;
        case PolicyThresholdDomain::ConditionNumberWarning:
            if (siValue <= 0.0) {  // [1,+∞)：非正先于越窗判（固定校验序）
                throw PolicyError(PolicyErrorCode::ThresholdNonPositive,
                                  std::string{"条件数警告阈值须>0（无量纲），实际 "}
                                      + formatThreshold(siValue));
            }
            if (siValue < 1.0) {   // 正值但低于下窗（条件数恒≥1）
                throw PolicyError(PolicyErrorCode::ThresholdOutOfRange,
                                  std::string{"条件数警告阈值须≥1（无量纲），实际 "}
                                      + formatThreshold(siValue));
            }
            break;
        case PolicyThresholdDomain::FiniteRotationTravelLimit:
            if (siValue <= 0.0) {  // (0,+∞)：§4.6 反例"=0/负"均拒绝
                throw PolicyError(PolicyErrorCode::ThresholdNonPositive,
                                  std::string{"行程上限须>0 rad，实际 "}
                                      + formatThreshold(siValue));
            }
            break;
        }
        return PolicyThreshold(siValue, origin, domain);
    }

    /// SI 真值访问器。单位由域（字段位置）固定：本类型不存单位——编码与
    /// 诊断的单位标注按域查表（§5.3"编码内为 SI 真值"）。
    double siValue() const noexcept { return m_siValue; }

    /// 阈值来源访问器（显式/附录 D 默认/模板默认/继承——见 PolicyValueOrigin）。
    PolicyValueOrigin origin() const noexcept { return m_origin; }

    /// 域元数据访问器（发布门核对"阈值域＝字段位置"用；非编码数据）。
    PolicyThresholdDomain domain() const noexcept { return m_domain; }

    /// 全成员精确等值（含域元数据——域不同即工程语义不同，见类注释）。
    bool operator==(const PolicyThreshold& o) const noexcept
    {
        return m_siValue == o.m_siValue && m_origin == o.m_origin
            && m_domain == o.m_domain;
    }
    bool operator!=(const PolicyThreshold& o) const noexcept { return !(*this == o); }

private:
    /// SI 真值（单位由 m_domain 承载的域固定——m/无量纲/rad）。
    double m_siValue;
    /// 阈值来源（四值之一——PolicyValueOrigin）。
    PolicyValueOrigin m_origin;
    /// 校验元数据：本阈值所属字段域（私有——非 §4.4 字段表数据面，不入编码）。
    PolicyThresholdDomain m_domain;

    /// 私有构造：仅 make()（校验通过后）可达——未校验阈值类型层不可存在。
    PolicyThreshold(double value, PolicyValueOrigin o, PolicyThresholdDomain d)
        : m_siValue(value), m_origin(o), m_domain(d)
    {
    }

    /// 开发诊断数值格式化（%.17g——round-trip 精确；仅进异常消息，
    /// 非持久化契约面；snprintf 用 "C" locale 数字格式，无千分位/本地小数点）。
    static std::string formatThreshold(double v)
    {
        char buf[40] = {};
        std::snprintf(buf, sizeof(buf), "%.17g", v);
        return std::string{buf};
    }
};

// =====================================================================
// §4.3 碰撞规则子模型（枚举与对规则）。
// =====================================================================

/// 规则级别（§4.3 token：must/should——Must=必检〔不可被过滤覆盖〕，
/// Should=可过滤〔须留痕 appliedFilters，§7.2〕）。编码 token 归 POL-T03。
enum class PolicyRuleLevel { Must, Should };

/// 碰撞域（§4.3 token：self/environment/tool/scene；语义冻结见 §4.3：
/// Self=连杆间、Tool=工具/负载×机器人及工件、Environment=机器人侧×环境、
/// Scene=环境对象间〔静态场景默认不检〕）。编码 token 归 POL-T03。
enum class CollisionDomain { Self, Environment, Tool, Scene };

/// 作用域目标三类（§4.3：对象 ID／角色类／显式组）。
enum class ScopeTargetKind { Object, Role, Group };

/**
 * @brief 作用域目标（§4.3 原文契约：三字段联合，kind 决定有效字段）。
 *
 * 值语义纯结构；kind↔字段一致性的构造入口见 makeObject/makeRole/makeGroup
 * （校验同一 detail 谓词——与发布门单源）。直接聚合构造仍可能（解析中间
 * 载体需要）；发布门兜底全量校验。
 * 线程安全：纯值。
 */
struct ScopeTarget {
    ScopeTargetKind kind = ScopeTargetKind::Object;  ///< 目标类别（决定有效字段）
    core::ObjectId object;                           ///< kind==Object 时有效（须 isValid）
    /// kind==Role 时有效：RobotLink|Tool|Payload|EnvironmentObject|Workpiece
    /// （场景对象角色词表归建模语义，本文消费——§6.1 SceneObjectRole 同源；
    /// 词表核对归解析⑤，此处仅承载）。
    std::string roleToken;
    /// kind==Group 时有效：策略内定义的显式对象组（§7.2）。
    std::string groupName;

    /// 对象目标工厂（object 无效→PolicyObjectInvalid——见 detail 谓词）。
    static ScopeTarget makeObject(core::ObjectId object)
    {
        ScopeTarget t;
        t.kind = ScopeTargetKind::Object;
        t.object = object;
        detail::requireScopeTargetWellFormed(t, "ScopeTarget::makeObject");
        return t;
    }

    /// 角色类目标工厂（roleToken 空→PolicyObjectInvalid；词表核对归解析⑤）。
    static ScopeTarget makeRole(std::string roleToken)
    {
        ScopeTarget t;
        t.kind = ScopeTargetKind::Role;
        t.roleToken = std::move(roleToken);
        detail::requireScopeTargetWellFormed(t, "ScopeTarget::makeRole");
        return t;
    }

    /// 显式组目标工厂（groupName 空→PolicyObjectInvalid；组定义存在性归解析⑤）。
    static ScopeTarget makeGroup(std::string groupName)
    {
        ScopeTarget t;
        t.kind = ScopeTargetKind::Group;
        t.groupName = std::move(groupName);
        detail::requireScopeTargetWellFormed(t, "ScopeTarget::makeGroup");
        return t;
    }

    bool operator==(const ScopeTarget& o) const
    {
        return kind == o.kind && object == o.object && roleToken == o.roleToken
            && groupName == o.groupName;
    }
    bool operator!=(const ScopeTarget& o) const { return !(*this == o); }
};

/**
 * @brief 对象对规则（§4.3 原文契约：无序对＋级别＋非空理由）。
 *
 * 无序对语义：{first,second} 与 {second,first} 同一对——**规范化为字典序
 * 存储**归解析管线②（§5.1②"成对规则规范化为字典序（消除顺序歧义）"，
 * POL-T04）；本类型按原样承载，不做规范化（避免第二套排序权威）。
 * 线程安全：纯值。
 */
struct PairRule {
    ScopeTarget first;                              ///< 对端一（无序——见类注释）
    ScopeTarget second;                             ///< 对端二
    PolicyRuleLevel level = PolicyRuleLevel::Must;  ///< 必检/可过滤
    /// 过滤/必检理由（§4.3：非空必填——进入策略身份与评估诊断的可追溯性）。
    std::string reason;

    /// 工厂（reason 空/两侧 ScopeTarget 结构非法→PolicyObjectInvalid）。
    static PairRule make(ScopeTarget first, ScopeTarget second,
                         PolicyRuleLevel level, std::string reason)
    {
        PairRule r;
        r.first = std::move(first);
        r.second = std::move(second);
        r.level = level;
        r.reason = std::move(reason);
        detail::requirePairRuleWellFormed(r, "PairRule::make");
        return r;
    }

    bool operator==(const PairRule& o) const
    {
        // 无序对等值：按承载序比较即可——规范化（字典序重排）后同一策略的
        // 等价规则必然同序（解析②保证），本比较不承担无序化语义。
        return first == o.first && second == o.second && level == o.level
            && reason == o.reason;
    }
    bool operator!=(const PairRule& o) const { return !(*this == o); }
};

/**
 * @brief 碰撞规则子模型（§4.3 原文契约——字段表含默认列）。
 *
 * P-POL-2/O-10 保守口径（acceptance 2）：safetyClearance 以 std::optional
 * 承载——nullopt＝间距检查显式不适用，**不发明数值**（唯一冻结默认是
 * 行程上限 4π，不在本结构）。enabled==true 时必须有值——发布门以
 * POLICY-THRESHOLD-REQUIRED-MISSING 拒绝发布（§5.2 行 5）。
 *
 * 默认值说明：enabled/excludeAdjacentLinksByDefault 的 C++ 默认＝§4.3 伪码
 * 字面契约；enabledDomains 的 C++ 默认为空集——"默认解析 {Self,Environment,Tool}
 * （Scene 默认不启用）"是解析管线③的职责（§5.1③），本类型不预填（避免
 * 第二默认源）；空集＋enabled=true 在发布门被 ApplicabilityInvalid 拒绝
 * （§5.1④"无域可检"）。
 *
 * 不在本层实现的规则集语义（权威唯一，归解析管线④/POL-T04）：重复规则
 * 检测、排除∩必检冲突（含 Role/Group 展开后等价对——D-06）、组循环、
 * 对象存在性（需 IPolicyValidationContext）。本层只保证字段级结构。
 *
 * 线程安全：纯值聚合。
 */
struct CollisionRules {
    /// 碰撞域总开关（§4.3：Quick 预览缺碰撞证据 ≠ enabled=false——KIN-13）。
    bool enabled = true;
    /// 启用域（空集语义：解析管线③默认解析前/显式清空；发布门要求非空当 enabled）。
    std::vector<CollisionDomain> enabledDomains;
    /// 安全间距阈值，SI m，[0,+∞)（P-POL-2：无冻结默认——nullopt＝间距
    /// 检查显式不适用；enabled==true 时发布门强制有值）。
    std::optional<PolicyThreshold> safetyClearance;
    /// 相邻连杆默认过滤（消费场景的运动学相邻事实——模型事实非策略发明）。
    bool excludeAdjacentLinksByDefault = true;
    /// 必须检测对（不可被过滤覆盖——结构保证，§7.2）。
    std::vector<PairRule> mandatoryPairs;
    /// 允许忽略对（可过滤；理由必填）。
    std::vector<PairRule> excludedPairs;

    /**
     * @brief 组装工厂：字段级结构校验（发布门同一谓词集——单源）。
     *
     * @throws PolicyError(ThresholdRequiredMissing) enabled==true 且
     *         safetyClearance 未设置（§5.2 行 5——无冻结默认项不得静默省略）
     * @throws PolicyError(ApplicabilityInvalid) enabled==true 且
     *         enabledDomains 为空（§5.1④"无域可检"；§5.2 行 11 归类）
     * @throws PolicyError(PolicyObjectInvalid) safetyClearance 的域不是
     *         SafetyClearance（单位域错位）、或任一 PairRule 结构非法
     */
    static CollisionRules make(bool enabled,
                               std::vector<CollisionDomain> enabledDomains,
                               std::optional<PolicyThreshold> safetyClearance,
                               bool excludeAdjacentLinksByDefault,
                               std::vector<PairRule> mandatoryPairs,
                               std::vector<PairRule> excludedPairs)
    {
        // 必填阈值缺省（§5.2 行 5）：P-POL-2 保守口径的直接执行点——缺失
        // 即拒绝发布，不发明默认数值（如需空缺语义走 P-POL-2 裁决）。
        if (enabled && !safetyClearance.has_value()) {
            throw PolicyError(PolicyErrorCode::ThresholdRequiredMissing,
                              "碰撞已启用但安全间距未设置（无冻结默认，P-POL-2）");
        }
        // 空域启用（§5.1④）：无域可检的策略非法。
        if (enabled && enabledDomains.empty()) {
            throw PolicyError(PolicyErrorCode::ApplicabilityInvalid,
                              "碰撞已启用但启用域为空（无域可检，§5.1④）");
        }
        // 单位域错位防呆：安全间距槽位上的阈值必须以 SafetyClearance 域
        // 构造（rad/无量纲阈值误放此处在此暴露）。
        if (safetyClearance.has_value()
            && safetyClearance->domain() != PolicyThresholdDomain::SafetyClearance) {
            throw PolicyError(PolicyErrorCode::PolicyObjectInvalid,
                              "safetyClearance 槽位阈值域与字段位置不符（须为安全间距域）");
        }
        CollisionRules rules;
        rules.enabled = enabled;
        rules.enabledDomains = std::move(enabledDomains);
        rules.safetyClearance = std::move(safetyClearance);
        rules.excludeAdjacentLinksByDefault = excludeAdjacentLinksByDefault;
        rules.mandatoryPairs = std::move(mandatoryPairs);
        rules.excludedPairs = std::move(excludedPairs);
        // 规则对结构（含 reason 非空）——逐对校验。
        const std::string_view where = "CollisionRules::make";
        for (const auto& r : rules.mandatoryPairs) {
            detail::requirePairRuleWellFormed(r, where);
        }
        for (const auto& r : rules.excludedPairs) {
            detail::requirePairRuleWellFormed(r, where);
        }
        return rules;
    }

    bool operator==(const CollisionRules& o) const
    {
        return enabled == o.enabled && enabledDomains == o.enabledDomains
            && safetyClearance == o.safetyClearance
            && excludeAdjacentLinksByDefault == o.excludeAdjacentLinksByDefault
            && mandatoryPairs == o.mandatoryPairs && excludedPairs == o.excludedPairs;
    }
    bool operator!=(const CollisionRules& o) const { return !(*this == o); }
};

// =====================================================================
// §4.4 关节限位与行程阈值子模型。
// =====================================================================

/**
 * @brief 关节限位/行程阈值子模型（§4.4 原文契约——含四态承载主轴）。
 *
 * P-POL-2/O-10 保守口径（acceptance 2）：nearLimitRatio 与
 * conditionNumberWarning 以 std::optional 承载——nullopt＝该警告检查显式
 * 不适用（无冻结默认，**不发明数值**）；有值时阈值槽位只能持有经 make()
 * 域校验的 PolicyThreshold。finiteRotationTravelLimit 为值字段（非 optional
 * ——4π 是唯一冻结默认，默认成员初始化 origin=DefaultAppendixD）。
 *
 * O-11（acceptance 3）：本结构**不预填惯量比字段**（SEL-05 归属裁决在
 * selection 卡 WP-19-T01——P-POL-3）；裁决归策略时经 kPolicySchemaVersionCurrent
 * 的次版本追加通道（§4.6/§5.3 向后兼容）。测试以 SFINAE 钉住"字段不存在"。
 *
 * 边界语义（§7.4 冻结，本层不实现比较）：行程 |qmax−qmin| ＞ 阈值才超限，
 * 等于阈值不超限（比较逻辑归 IJointLimitEvaluator，POL-T08）。
 *
 * 线程安全：纯值聚合。
 */
struct JointThresholds {
    /// 近限位比警告阈值，无量纲，(0,1]；nullopt＝该警告检查显式不适用
    /// （无冻结默认，P-POL-2——§4.4 字段注释原文）。
    std::optional<PolicyThreshold> nearLimitRatio;
    /// 条件数警告阈值，无量纲，[1,+∞)；nullopt＝同上（P-POL-2）。
    std::optional<PolicyThreshold> conditionNumberWarning;
    /// 有限限位旋转关节行程上限，SI rad，(0,+∞)；默认 4π（DefaultAppendixD
    /// ——附录 D 第 11 项，唯一已冻结工程策略默认）；仅适用有限限位旋转关节
    /// （continuous 关节豁免——POL-JNT-1）。
    PolicyThreshold finiteRotationTravelLimit = defaultFiniteRotationTravelLimit();
    /// 校验开关（true=默认执行 MDL-06④ 策略校验）。
    bool travelLimitCheckEnabled = true;

    /// 默认行程上限（4π rad，DefaultAppendixD——kDefaultFiniteRotationTravelLimit
    /// 的已校验承载；默认成员初始化的调用点，发布门之外的正确默认来源）。
    static PolicyThreshold defaultFiniteRotationTravelLimit()
    {
        return PolicyThreshold::make(kDefaultFiniteRotationTravelLimit,
                                     PolicyValueOrigin::DefaultAppendixD,
                                     PolicyThresholdDomain::FiniteRotationTravelLimit);
    }

    /**
     * @brief 组装工厂：以裸值＋来源构造全部阈值槽位（域校验经 PolicyThreshold::make）。
     *
     * 与逐字段手工赋值等价但单点完成域绑定——供解析管线③（默认值解析后
     * 组装）与测试使用；直接聚合构造＋赋值 make 出的阈值同样合法（发布门
     * 兜底核对域绑定）。
     *
     * @param nearLimitRatioValue          [in] 近限位比（无量纲，(0,1]）；
     *                                     nullopt＝检查显式不适用（P-POL-2）
     * @param nearLimitRatioOrigin         [in] 近限位比来源（value 为 nullopt 时不消费）
     * @param conditionNumberWarningValue  [in] 条件数警告阈值（无量纲，[1,+∞)）；
     *                                     nullopt＝同上
     * @param conditionNumberWarningOrigin [in] 条件数阈值来源
     * @param finiteRotationTravelLimitRad [in] 行程上限（SI rad，(0,+∞)；非度）
     * @param travelLimitOrigin            [in] 行程上限来源（默认解析填
     *                                     DefaultAppendixD——§5.1③）
     * @param travelLimitCheckEnabled      [in] 行程校验开关（默认 true）
     * @return 组装完成的子模型（阈值全部经域校验）
     *
     * @throws PolicyError 各阈值 make() 的域校验异常（NonFinite/NonPositive/
     *         OutOfRange——语义见 PolicyThreshold::make）
     */
    static JointThresholds make(std::optional<double> nearLimitRatioValue,
                                PolicyValueOrigin nearLimitRatioOrigin,
                                std::optional<double> conditionNumberWarningValue,
                                PolicyValueOrigin conditionNumberWarningOrigin,
                                double finiteRotationTravelLimitRad,
                                PolicyValueOrigin travelLimitOrigin,
                                bool travelLimitCheckEnabled = true)
    {
        JointThresholds jt;
        // 未设置项保持 nullopt（显式不适用——四态承载 1/4 态；不发明数值）。
        if (nearLimitRatioValue.has_value()) {
            jt.nearLimitRatio = PolicyThreshold::make(
                *nearLimitRatioValue, nearLimitRatioOrigin,
                PolicyThresholdDomain::NearLimitRatio);
        }
        if (conditionNumberWarningValue.has_value()) {
            jt.conditionNumberWarning = PolicyThreshold::make(
                *conditionNumberWarningValue, conditionNumberWarningOrigin,
                PolicyThresholdDomain::ConditionNumberWarning);
        }
        jt.finiteRotationTravelLimit = PolicyThreshold::make(
            finiteRotationTravelLimitRad, travelLimitOrigin,
            PolicyThresholdDomain::FiniteRotationTravelLimit);
        jt.travelLimitCheckEnabled = travelLimitCheckEnabled;
        return jt;
    }

    bool operator==(const JointThresholds& o) const
    {
        return nearLimitRatio == o.nearLimitRatio
            && conditionNumberWarning == o.conditionNumberWarning
            && finiteRotationTravelLimit == o.finiteRotationTravelLimit
            && travelLimitCheckEnabled == o.travelLimitCheckEnabled;
    }
    bool operator!=(const JointThresholds& o) const { return !(*this == o); }
};

// =====================================================================
// §4.2.1 适用范围与 §4.2 来源/校验状态。
// =====================================================================

/**
 * @brief 策略适用范围（§4.2.1 原文契约——空集语义见表）。
 *
 * 空集语义：modes 空＝全部模式适用；modelObjects/taskObjects/caseObjects
 * 空＝不限（模型/任务/工况）。P-POL-7（D-14）：caseObjects 仅限定适用
 * 工况范围，**不支持按工况差异化阈值**（工况差异经场景对象集表达——
 * ARC-05 单一权威）。
 *
 * 引用对象存在性校验（修订闭包内）归解析管线⑤（经
 * IPolicyValidationContext——POL-T04）；本类型只承载集合。无 make 工厂：
 * 无字段级结构约束（EvaluationMode/ObjectId 类型即词表）。
 * 线程安全：纯值聚合。
 */
struct PolicyApplicability {
    /// 适用评估模式（空＝全部；非空时为 Preview/Quick/Verified 子集——
    /// 类型层天然保证：core::EvaluationMode 枚举即三值词表）。
    std::vector<core::EvaluationMode> modes;
    /// 适用模型对象（空＝不限；非空时策略仅适用于所列 RobotDesign 对象快照）。
    std::vector<core::ObjectId> modelObjects;
    /// 适用任务对象（空＝不限——语义同上）。
    std::vector<core::ObjectId> taskObjects;
    /// 适用工况对象（空＝不限；仅限定范围不差异化阈值——P-POL-7/D-14）。
    std::vector<core::ObjectId> caseObjects;

    bool operator==(const PolicyApplicability& o) const
    {
        return modes == o.modes && modelObjects == o.modelObjects
            && taskObjects == o.taskObjects && caseObjects == o.caseObjects;
    }
    bool operator!=(const PolicyApplicability& o) const { return !(*this == o); }
};

/// 策略创建来源类别（§4.2 origin 行：Template/Imported/UserEdited/SystemDefault）。
enum class PolicyOriginKind { Template, Imported, UserEdited, SystemDefault };

/**
 * @brief 策略创建来源（§4.2 origin 行——**审计字段，不参与内容身份**）。
 *
 * 来源标注用于审计追溯（模板创建/导入/人工编辑/系统默认）；内容身份只对
 * 语义闭包计算（§5.3），故同语义不同来源的两个策略对象内容身份相同
 * （D-03——显示与审计信息不影响工程语义）。
 * 线程安全：纯值聚合。
 */
struct PolicyOrigin {
    PolicyOriginKind kind = PolicyOriginKind::UserEdited;  ///< 来源类别
    std::optional<core::ObjectId> sourceObject;            ///< 来源对象（如导入自某对象；可空）
    std::optional<std::string> note;                       ///< 审计备注（可空）

    bool operator==(const PolicyOrigin& o) const
    {
        return kind == o.kind && sourceObject == o.sourceObject && note == o.note;
    }
    bool operator!=(const PolicyOrigin& o) const { return !(*this == o); }
};

/**
 * @brief 策略校验状态（§4.5 原文三态）。
 *
 * 状态语义与载体边界：
 *   - NotValidated：草稿期中间态，**仅解析器内部**，不对外发布；
 *   - Valid：通过 §5.1 全部校验，唯一可发布态；
 *   - Invalid：校验失败终态，诊断随附（载体＝PolicyParseResult.diagnostics，
 *     POL-T04——不产生可消费的策略对象）。
 * 发布门（EngineeringPolicySet::make）机械执行"工厂只产出 Valid 实例"。
 */
enum class PolicyValidationState { NotValidated, Valid, Invalid };

// =====================================================================
// §4.2 发布对象：EngineeringPolicySet（全字段构造后不可变）。
// =====================================================================

/**
 * @brief 已发布策略对象（§4.2 字段表——全部字段构造后不可变，无 setter；
 *        值语义）。
 *
 * 生命周期与权威（§4.6）：由解析管线（resolvePolicy 第⑦步，POL-T04）经
 * make() 发布门产出，构造后无任何修改途径——全部数据成员 const，赋值被
 * 编译期删除；"修改策略"＝新 RawPolicyInput → 重新解析 → 新内容身份 →
 * 经①命令端口产生新对象内容版本与新修订（project 持久化，SA-03/PA-2）。
 * 值语义（拷贝构造可用）——调用方按值持有/传递，无共享所有权。
 *
 * 发布门契约（§4.5）：make() 是唯一构造入口，机械执行——
 *   - 只产出 Valid 态实例（validationState 参数非 Valid 即拒——
 *     "发布对象只存在 Valid 态"的类型层强制）；
 *   - 内容身份必须有效（发布对象必有语义身份——CON-05/06）；
 *   - schemaVersion 恰为当前代（未来/未知代在当前程序中不可发布）；
 *   - 子模型字段级结构全量核对（含阈值域＝字段位置绑定）。
 *   规则集语义检查（重复/冲突/循环/对象存在性）**不在**本门——权威在
 *   解析管线④⑤（POL-T04，PA-1；D-06 结构拒绝在其落位）。
 *
 * 内容身份语义闭包（§4.2/§5.3——POL-T03 codec 的计算对象，此处为文档
 * 锚点）：参与＝{schemaVersion, collision, jointThresholds, applicability,
 * numericContractAnchor}；不参与＝{policyObject, origin, validationState,
 * validationDiagnostics, compatibilityNotes}（管理与审计）。显示设置
 * （显示单位/渲染分组/高亮开关）不存在于任何字段——UX-08 数据层保证
 * （POL-ID-3 由 POL-T03/T04 用例钉住）。
 *
 * 线程安全：不可变值类型（构造后只读），并发只读安全。
 */
class EngineeringPolicySet {
public:
    // ---- 身份块（§4.2 字段表——对象身份/编码代/语义身份） ----
    /// 项目对象身份（project 分配；跨修订稳定——§4.1 策略对象身份行；
    /// **不参与内容身份**）。
    const core::ObjectId policyObject;
    /// 策略编码格式代号（当前 1＝kPolicySchemaVersionCurrent；参与内容身份）。
    const std::uint32_t schemaVersion;
    /// 语义内容身份（§5.3，发布时由 policy 计算，调用方不可申报；参与
    /// 依赖切片与缓存键——CON-06）。
    const core::ContentIdentity contentIdentity;

    // ---- 语义闭包块（参与内容身份的字段） ----
    /// 碰撞规则子模型（§4.3）。
    const CollisionRules collision;
    /// 关节限位/行程阈值子模型（§4.4）。
    const JointThresholds jointThresholds;
    /// 适用范围（§4.2.1；参与内容身份）。
    const PolicyApplicability applicability;
    /// 数值契约基线锚（参与内容身份与兼容判定——§8.1；默认
    /// "appendixD@v1.16"）。
    const std::string numericContractAnchor;

    // ---- 管理与审计块（不参与内容身份） ----
    /// 创建来源（审计字段——D-03）。
    const PolicyOrigin origin;
    /// 校验状态（发布门强制＝Valid——§4.5）。
    const PolicyValidationState validationState;
    /// 解析/校验诊断（含已修复项的告知性诊断；**不参与内容身份**）。
    const std::vector<core::DiagnosticRecord> validationDiagnostics;
    /// 向后兼容与迁移说明（自由文本；**不参与内容身份**——迁移执行归
    /// project 升级器）。
    const std::optional<std::string> compatibilityNotes;

    /**
     * @brief 发布门：唯一构造入口（§4.5"工厂只产出 Valid 实例"的机械执行）。
     *
     * 校验序（固定——确定性；错误即抛不静默）：
     *   ① 身份有效性（policyObject）→ ② schema 代核对 → ③ 内容身份有效性
     *   → ④ 校验状态＝Valid → ⑤ 数值契约锚非空 → ⑥ 碰撞规则结构（复用
     *   CollisionRules::make 同一谓词集：必填阈值缺省/空域启用/域绑定/
     *   规则对结构）→ ⑦ 阈值域＝字段位置绑定。
     *
     * @param policyObject          [in] 策略对象身份（须 isValid——project 分配）
     * @param schemaVersion         [in] schema 代（须==kPolicySchemaVersionCurrent：
     *                              大于→未来代拒绝，小于→未知代拒绝）
     * @param contentIdentity       [in] 语义内容身份（须 isValid——发布对象
     *                              必有内容身份；由解析管线⑥计算，调用方不可
     *                              申报假身份）
     * @param collision             [in] 碰撞规则子模型（字段级结构经校验）
     * @param jointThresholds       [in] 阈值子模型（域绑定经核对）
     * @param applicability         [in] 适用范围（空集语义见类型注释）
     * @param origin                [in] 创建来源（审计字段，不校验语义）
     * @param validationState       [in] 须为 Valid（非 Valid＝试图发布未通过
     *                              校验的对象——fail-fast）
     * @param validationDiagnostics [in] 随附诊断（告知性；发布对象诊断应只含
     *                              告知性条目——逐条严重级核对归解析管线，本门
     *                              不重复判级）
     * @param compatibilityNotes    [in] 兼容注记（可空）
     * @param numericContractAnchor [in] 数值契约锚（非空；默认
     *                              "appendixD@v1.16"）
     * @return 已发布策略对象（全字段 const——构造后不可变）
     *
     * @throws PolicyError(PolicyErrorCode::PolicyObjectInvalid)
     *         ①③④⑤任一失败，或⑥⑦的字段级结构违约（域错位/规则对结构非法）
     * @throws PolicyError(PolicyErrorCode::SchemaVersionFuture)
     *         schemaVersion 大于当前代（未来版本不可由当前代码发布）
     * @throws PolicyError(PolicyErrorCode::SchemaVersionUnknown)
     *         schemaVersion 小于当前代（未知旧代）
     * @throws PolicyError(ThresholdRequiredMissing)
     *         collision.enabled==true 且 safetyClearance 未设置（§5.2 行 5）
     * @throws PolicyError(ApplicabilityInvalid)
     *         collision.enabled==true 且 enabledDomains 为空（§5.1④）
     */
    static EngineeringPolicySet
    make(core::ObjectId policyObject,
         std::uint32_t schemaVersion,
         core::ContentIdentity contentIdentity,
         CollisionRules collision,
         JointThresholds jointThresholds,
         PolicyApplicability applicability,
         PolicyOrigin origin,
         PolicyValidationState validationState,
         std::vector<core::DiagnosticRecord> validationDiagnostics = {},
         std::optional<std::string> compatibilityNotes = std::nullopt,
         std::string numericContractAnchor = std::string{kNumericContractAnchorDefault})
    {
        // ① 对象身份有效性（发布对象必须挂在已分配的策略对象上——全零
        // 保留值非法）。
        if (!policyObject.isValid()) {
            throw PolicyError(PolicyErrorCode::PolicyObjectInvalid,
                              "policyObject 身份无效（全零保留值）");
        }
        // ② schema 代核对（类型层：当前代码只能发布当前代——未来/未知代
        // 的实例不可存在；解析期对**输入**的未来代拒绝同码面〔POL-PARSE-1〕）。
        if (schemaVersion > kPolicySchemaVersionCurrent) {
            throw PolicyError(PolicyErrorCode::SchemaVersionFuture,
                              "schemaVersion 高于当前代（未来版本拒绝发布，PM-06）");
        }
        if (schemaVersion < kPolicySchemaVersionCurrent) {
            throw PolicyError(PolicyErrorCode::SchemaVersionUnknown,
                              "schemaVersion 低于当前已知最低代");
        }
        // ③ 内容身份有效性（发布对象必有语义身份——CON-05/06；全零＝
        // 未计算，非法）。
        if (!contentIdentity.isValid()) {
            throw PolicyError(PolicyErrorCode::PolicyObjectInvalid,
                              "contentIdentity 无效（发布对象必有语义内容身份）");
        }
        // ④ 校验状态＝Valid（§4.5：发布对象只存在 Valid 态——NotValidated
        // 不对外、Invalid 不产生可消费对象）。
        if (validationState != PolicyValidationState::Valid) {
            throw PolicyError(PolicyErrorCode::PolicyObjectInvalid,
                              "validationState 非 Valid（工厂只产出 Valid 实例，§4.5）");
        }
        // ⑤ 数值契约锚非空（§4.2 必填列——锚参与内容身份与兼容判定）。
        if (numericContractAnchor.empty()) {
            throw PolicyError(PolicyErrorCode::PolicyObjectInvalid,
                              "numericContractAnchor 为空（必填）");
        }
        // ⑥ 碰撞规则字段级结构——复用 CollisionRules::make 的同一谓词集
        // （返回值弃用：校验通过即丢弃重组副本，调用方传入值原样入发布对象，
        // 保证"发布对象字段＝调用方字段"的精确性）。
        static_cast<void>(CollisionRules::make(collision.enabled, collision.enabledDomains,
                                               collision.safetyClearance,
                                               collision.excludeAdjacentLinksByDefault,
                                               collision.mandatoryPairs,
                                               collision.excludedPairs));
        // ⑦ 阈值域＝字段位置绑定（"单位由字段位置固定"的机械核对——
        // 阈值对象携带其构造域，错位即拒绝）。
        if (jointThresholds.nearLimitRatio.has_value()
            && jointThresholds.nearLimitRatio->domain()
                   != PolicyThresholdDomain::NearLimitRatio) {
            throw PolicyError(PolicyErrorCode::PolicyObjectInvalid,
                              "nearLimitRatio 槽位阈值域与字段位置不符");
        }
        if (jointThresholds.conditionNumberWarning.has_value()
            && jointThresholds.conditionNumberWarning->domain()
                   != PolicyThresholdDomain::ConditionNumberWarning) {
            throw PolicyError(PolicyErrorCode::PolicyObjectInvalid,
                              "conditionNumberWarning 槽位阈值域与字段位置不符");
        }
        if (jointThresholds.finiteRotationTravelLimit.domain()
            != PolicyThresholdDomain::FiniteRotationTravelLimit) {
            throw PolicyError(PolicyErrorCode::PolicyObjectInvalid,
                              "finiteRotationTravelLimit 槽位阈值域与字段位置不符");
        }
        // 全部校验通过——构造不可变实例（const 成员经初始化列表一次性绑定）。
        return EngineeringPolicySet(
            std::move(policyObject), schemaVersion, std::move(contentIdentity),
            std::move(collision), std::move(jointThresholds), std::move(applicability),
            std::move(origin), validationState, std::move(validationDiagnostics),
            std::move(compatibilityNotes), std::move(numericContractAnchor));
    }

    /// 全字段精确等值（11 字段逐成员——"对象逐字段相等"〔POL-ID-1 观测点〕
    /// 的类型层承载；内容身份相等性判定归 §5.3 语义闭包，本比较含审计字段）。
    bool operator==(const EngineeringPolicySet& o) const
    {
        return policyObject == o.policyObject && schemaVersion == o.schemaVersion
            && contentIdentity == o.contentIdentity && collision == o.collision
            && jointThresholds == o.jointThresholds && applicability == o.applicability
            && numericContractAnchor == o.numericContractAnchor && origin == o.origin
            && validationState == o.validationState
            && validationDiagnostics == o.validationDiagnostics
            && compatibilityNotes == o.compatibilityNotes;
    }
    bool operator!=(const EngineeringPolicySet& o) const { return !(*this == o); }

private:
    /// 私有构造：仅 make()（校验通过后）可达——未发布/非法实例类型层不可存在。
    EngineeringPolicySet(core::ObjectId policyObjectArg,
                         std::uint32_t schemaVersionArg,
                         core::ContentIdentity contentIdentityArg,
                         CollisionRules collisionArg,
                         JointThresholds jointThresholdsArg,
                         PolicyApplicability applicabilityArg,
                         PolicyOrigin originArg,
                         PolicyValidationState validationStateArg,
                         std::vector<core::DiagnosticRecord> validationDiagnosticsArg,
                         std::optional<std::string> compatibilityNotesArg,
                         std::string numericContractAnchorArg)
        : policyObject(std::move(policyObjectArg))
        , schemaVersion(schemaVersionArg)
        , contentIdentity(std::move(contentIdentityArg))
        , collision(std::move(collisionArg))
        , jointThresholds(std::move(jointThresholdsArg))
        , applicability(std::move(applicabilityArg))
        , numericContractAnchor(std::move(numericContractAnchorArg))
        , origin(std::move(originArg))
        , validationState(validationStateArg)
        , validationDiagnostics(std::move(validationDiagnosticsArg))
        , compatibilityNotes(std::move(compatibilityNotesArg))
    {
    }
};

// =====================================================================
// detail 校验器定义（类型已完整；声明见本头前部——工厂与发布门共用）。
// =====================================================================

namespace detail {

inline void requireScopeTargetWellFormed(const ScopeTarget& t, std::string_view where)
{
    // inline（POL-T03 修复登记）：本头为 header-only 类型契约，工厂与
    // 发布门在头内调用这两个谓词——POL-T03 引入第二个产品翻译单元
    // （PolicyInput.cpp）后，非 inline 的头内定义在 MSVC 下触发
    // LNK2005 多重定义；inline 标注使多 TU 各自持有等价定义而链接器
    // 合并之（ODR 合规），不改任何校验语义。
    switch (t.kind) {
    case ScopeTargetKind::Object:
        // kind==Object：object 字段须有效（全零保留值＝未分配身份，非法——
        // project 分配者纪律同 core 保留值约定）。
        if (!t.object.isValid()) {
            throw PolicyError(PolicyErrorCode::PolicyObjectInvalid,
                              std::string{where} + ": ScopeTarget kind=Object 但对象身份无效");
        }
        break;
    case ScopeTargetKind::Role:
        // kind==Role：roleToken 须非空（词表核对归解析⑤——此处仅非空）。
        if (t.roleToken.empty()) {
            throw PolicyError(PolicyErrorCode::PolicyObjectInvalid,
                              std::string{where} + ": ScopeTarget kind=Role 但角色 token 为空");
        }
        break;
    case ScopeTargetKind::Group:
        // kind==Group：groupName 须非空（组定义自洽性归解析④⑤）。
        if (t.groupName.empty()) {
            throw PolicyError(PolicyErrorCode::PolicyObjectInvalid,
                              std::string{where} + ": ScopeTarget kind=Group 但组名为空");
        }
        break;
    }
}

inline void requirePairRuleWellFormed(const PairRule& r, std::string_view where)
{
    // inline：同 requireScopeTargetWellFormed 的 POL-T03 修复登记（多
    // 翻译单元 ODR 合规——语义零变化）。
    // reason 非空必填（§4.3 字段注释：过滤/必检理由进入策略身份与评估诊断
    // ——空理由＝不可追溯规则，发布即拒绝）。
    if (r.reason.empty()) {
        throw PolicyError(PolicyErrorCode::PolicyObjectInvalid,
                          std::string{where} + ": PairRule 理由为空（§4.3 非空必填）");
    }
    requireScopeTargetWellFormed(r.first, where);
    requireScopeTargetWellFormed(r.second, where);
}

}  // namespace detail

}  // namespace sdurws::ird::policy

#endif  // SDURWS_IRD_POLICY_POLICYSET_HPP
