/**
 * @file   JointLimits.hpp
 * @brief  关节限位与行程阈值评估契约与唯一实现——JointLimitSpec/JointLimitQuery/
 *         JointLimitFinding/JointMarginRecord/JointLimitEvaluation/
 *         IJointLimitEvaluator（§9.4）＋§7.4 行程/近限位/区间边界决策表的
 *         产品执行件（POL-T08）。
 *
 * 设计依据：
 *   - units/policy.md §9.4（IJointLimitEvaluator 契约——JointLimitSpec/
 *     JointLimitFinding/JointLimitEvaluation/IJointLimitEvaluator 的唯一权威
 *     章节；其"边界"段：区间有效性/工程工作范围有限性检出为诊断级发现并跳过
 *     〔判定权归命令处理器——D-13〕、行程上限超限输出比较型结果供 project
 *     处理器构造 ConfirmableFinding〔SA-15；policy 不执行放行〕、近限位阈值
 *     nullopt→显式 NotApplicable 标记〔不伪造数值〕）、§7.4（距离语义与边界
 *     值决策表的行程上限行/近限位比行/限位违例行——T＞L 才超限、边界含于
 *     合规侧 D-08、r＝距限位余量/区间半宽、q ∉ [qmin,qmax] 为硬过滤素材）、
 *     §4.4（JointThresholds 阈值来源——4π 唯一冻结默认、nearLimitRatio/
 *     conditionNumberWarning 无冻结默认 P-POL-2、travelLimitCheckEnabled
 *     校验开关）、§4.5（未设置/无效/冲突/不适用四态——评估输出显式标记
 *     不伪造结论）、§7.5（评估期数值异常——NaN/±Inf 不伪造、不入 findings）、
 *     §2.1 O-9 行（关节限位与行程上限评估契约的承接方）、§12 POL-T08 行
 *     （产物 JointLimits.hpp/.cpp；验证 POL-JNT-1/2）
 *   - 需求 MDL-06④（行程上限＝可配置工程策略校验，默认 4π，比较型诊断＋
 *     显式确认放行——policy 止于比较型结果供给）、MDL-12（continuous 关节
 *     工程工作范围有限性）、KIN-13（近限位阈值随策略传递——API 无阈值参数）、
 *     KIN-01（逐构型逐关节裕量素材的消费方）、SA-15/D-13（确认放行流素材；
 *     检出不阻断——判定权归命令处理器）、AT-01（处理器 prepare 走查 §10.4）、
 *     ERR-01（比较型三要素；不适用显式标记）、NFR-COR-03（不静默转 0/通过）
 *
 * 背景说明（本头在评估链路中的位置——第一读者须知）：
 *   调用方（project 处理器 prepare〔AT-01〕、kinematics 裕量计算〔KIN-01〕）
 *   把建模侧的关节事实装配成 JointLimitSpec 关节表（对象身份＋局部名＋限位/
 *   continuous 类型＋工程工作范围），连同逐构型关节位置（SI rad）组成
 *   JointLimitQuery，经 IJointLimitEvaluator::evaluate(q, policy, names)
 *   得到 JointLimitEvaluation。评估是**纯标量算术**（无几何、无框架调用）：
 *   行程比较、近限位比、限位违例、区间/范围有效性全部是可解析算例级的
 *   确定性计算——同 (查询, 策略, 名称映射) 输入必得逐字段相等输出
 *   （NFR-COR-02）。查询与输出为纯值类型，不持资源、不含回调。
 *
 *   "为什么评估器不阻断"（D-13 的结构落点——acceptance 2）：区间非法/
 *   工程范围非法在建模/工程语义上是**硬断言**（MDL-06/12），但其断言执行权
 *   归 modeling/project 处理器（就地阻断）；policy 只供给比较事实——本评估器
 *   把它们检出为诊断级发现（JointLimitFinding＋伴随 DiagnosticRecord）并
 *   跳过该关节，评估状态仍为 Completed（检出≠评估失败），**不**代替处理器
 *   放行/阻止命令（避免双权威，§9.4"非法调用"行）。
 *
 *   "为什么行程上限是唯一有默认的阈值"（P-POL-2/O-10 保守口径——acceptance 3）：
 *   4π 是附录 D 第 11 项唯一由上游冻结的工程策略默认（§4.4）；近限位比/
 *   条件数警告阈值无冻结默认——策略输入未提供（nullopt）时对应检查**显式
 *   不适用**，评估输出以 notApplicableChecks 字段级标记（本头
 *   JointCheckNotApplicable——§15.4 v0.9 登记的实现增量），绝不发明数值。
 *
 * 实现纪律：
 *   - 本头 include CollisionQuery.hpp 仅为复用 CollisionEvaluationStatus
 *     词表（§9.4"复用状态词表"原文）——该头携带 rw 值成员（Q/State），
 *     使本头间接依赖 rw include 面：**集成模式专属**（POL-T06/T07 同款
 *     gating——src/JointLimits.cpp 与 test TU 仅在 TARGET sdurw_kinematics
 *     存在时编译；冒烟模式无任何 TU include 本头，"目标注册＋include 路径"
 *     冒烟口径不受影响）。评估本体零框架调用——rw 依赖纯属词表复用的
 *     include 链传导，无任何 RobWork 运行时语义消费（R-5 红线不涉及）。
 *   - 全部类型为纯值聚合（无可变共享状态）；operator== 逐字段全量等值
 *     （输出逐字段一致断言的基础——NFR-COR-05 精神）。
 *   - 名称消费仅经 IPolicyNameContext（tryRuntimeName 转发——不可解析不
 *     猜测，ARC-04）；本单元内无任何名称前缀拼接/剥离（R-4/P-POL-8）。
 *   - 线程安全：评估器实例构造后只读、evaluate 为 const 纯函数——并发
 *     只读可重入；无随机源、无归约。
 */

#ifndef SDURWS_IRD_POLICY_JOINTLIMITS_HPP
#define SDURWS_IRD_POLICY_JOINTLIMITS_HPP

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <sdurws/ird/core/DiagData.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/core/Provenance.hpp>

#include <sdurws/ird/policy/Contexts.hpp>
#include <sdurws/ird/policy/CollisionQuery.hpp>  // CollisionEvaluationStatus 词表复用（§9.4）
#include <sdurws/ird/policy/PolicySet.hpp>

namespace sdurws::ird::policy {

// =====================================================================
// §9.4 输入侧——关节规格与查询载体。
// =====================================================================

/**
 * @brief 关节规格（§9.4 原文五字段——调用方自建模事实装配的关节表条目）。
 *
 * 字段语义（§9.4 注释原文逐行）：
 *   - jointObject：关节对象身份（project 分配；输出的定位键——NFR-COR-05；
 *     表内必须唯一，重复＝调用方装配违约 fail-fast）；
 *   - localName：显示辅助（经⑥端口语；**不作为身份**——CON-06）；
 *   - qMin/qMax：有限限位下/上端，SI rad（不是度）；**二者须同时有/无**
 *     （单侧出现＝装配违约 fail-fast——§9.4 Spec 注释原文）；
 *   - isContinuous：类型保留（MDL-12）——true＝多圈连续关节：行程上限检查
 *     豁免（§7.4"continuous 关节免除"行），其工程工作范围（engineeringRange）
 *     必填且须为有限区间；
 *   - engineeringRange：continuous 必填有限区间（工程工作范围，SI rad，
 *     first＜second 且双端有限）——"必填"属装配契约（continuous 缺失＝
 *     调用方违约 fail-fast）；"有限性"（已提供但 min≥max/非有限端点）属
 *     评估检出→EngineeringRangeInvalid 诊断级发现（§9.4 边界段——
 *     两分登记于 policy.md §15.4 v0.9）。
 *
 * !isContinuous 且 qMin/qMax 均未设置：类型层合法（§9.4"同时有/无"）——
 * 语义为"未声明限位的有限旋转关节"：全部限位类检查不适用、裕量记录以
 * NotApplicable 显式标记（不伪造数值——§4.5 四态之 NotApplicable）。
 * 线程安全：纯值。
 */
struct JointLimitSpec {
    core::ObjectId jointObject;  ///< 关节对象身份（表内唯一；输出定位键）
    std::string localName;       ///< 局部名（显示辅助——不作身份，CON-06）
    /// 有限限位下端（SI rad）；与 qMax 同有同无。
    std::optional<double> qMin;
    /// 有限限位上端（SI rad）；与 qMin 同有同无。
    std::optional<double> qMax;
    bool isContinuous = false;   ///< continuous 类型保留（MDL-12——行程上限豁免）
    /// continuous 必填有限区间（工程工作范围，SI rad，first＜second）。
    std::optional<std::pair<double, double>> engineeringRange;

    bool operator==(const JointLimitSpec& o) const
    {
        return jointObject == o.jointObject && localName == o.localName
            && qMin == o.qMin && qMax == o.qMax && isContinuous == o.isContinuous
            && engineeringRange == o.engineeringRange;
    }
    bool operator!=(const JointLimitSpec& o) const { return !(*this == o); }
};

/**
 * @brief 关节限位查询载体（§9.4 evaluate 签名的查询形——两成员实现形态，
 *        登记于 policy.md §15.4 v0.9：单元卡仅冻结 evaluate 签名与
 *        JointLimitSpec，查询载体的成员清单为实现落位）。
 *
 * 契约要点：
 *   - joints：关节表（处理器/建模装配；jointObject 唯一）；表序不进入输出
 *     序——评估按对象身份规范序处理，输出恒按 (对象字典序×kind)/
 *     (样本序×对象字典序) 稳定排序（§9.4 稳定排序行），与装配顺序无关
 *     （NFR-COR-02）；
 *   - configurations：逐构型关节位置（SI rad；内层向量与 joints **等长
 *     同序**——第 i 个元素对应 joints[i] 的位置值）；至少 1 个构型。
 *
 * ★ 成员清单即契约（KIN-13/R-POL-5 同源口径）：本类型不携带任何阈值/开关
 *   ——阈值唯一来源是 evaluate 传入的 EngineeringPolicySet（策略单一权威，
 *   ARC-05；结构上不可逐查询改判）。
 *
 * 线程安全：纯值。
 */
struct JointLimitQuery {
    /// 关节表（jointObject 唯一；§9.4 JointLimitSpec 装配）。
    std::vector<JointLimitSpec> joints;
    /// 逐构型关节位置（SI rad；内层与 joints 等长同序；至少 1 构型）。
    std::vector<std::vector<double>> configurations;

    bool operator==(const JointLimitQuery& o) const
    {
        return joints == o.joints && configurations == o.configurations;
    }
    bool operator!=(const JointLimitQuery& o) const { return !(*this == o); }
};

// =====================================================================
// §9.4 输出侧——发现、裕量记录、检查级不适用标记与评估结果。
// =====================================================================

/**
 * @brief 关节限位发现种类（§9.4 原文枚举——五值；语义与输出锚点）。
 *
 * 各 kind 的比较型三要素取值（actualValue/thresholdValue 的语义——单位随
 * kind 固定，§9.4 字段注释原文；本表为实现冻结的逐 kind 口径）：
 *   - IntervalInvalid          区间非法（qMin ≥ qMax）——actual=qMin、
 *                              threshold=qMax〔rad〕；
 *   - EngineeringRangeInvalid  工程工作范围非法（min≥max/端点非有限）
 *                              ——actual=range.first、threshold=range.second
 *                              〔rad〕；
 *   - TravelLimitExceeded      行程超限（T＞L，§7.4；T＝L 不超限——边界
 *                              含于合规侧 D-08）——actual=T=|qmax−qmin|、
 *                              threshold=L=策略行程上限〔rad〕；
 *   - NearLimit                近限位警告（r＜阈值，§7.4 严格小于；r＝阈值
 *                              不警告——同 D-08 口径）——actual=r（近限位比）、
 *                              threshold=策略 nearLimitRatio〔无量纲〕；
 *   - LimitViolated            限位违例（q ∉ [qmin,qmax]——闭区间端点不违例；
 *                              硬过滤素材，过滤动作归 kinematics KIN-02）
 *                              ——actual=q、threshold=被越过的限位端
 *                              〔rad〕。
 */
enum class JointLimitFindingKind {
    IntervalInvalid,
    EngineeringRangeInvalid,
    TravelLimitExceeded,
    NearLimit,
    LimitViolated,
};

/**
 * @brief 关节限位发现（§9.4 原文七字段——关节级比较型事实记录）。
 *
 * 字段语义（§9.4 注释原文）：
 *   - jointObject/localName/runtimeName：定位三元组（身份＋显示辅助——
 *     runtimeName 经名称上下文解析，不作身份，CON-06）；
 *   - kind：发现种类（取值语义见枚举注释）；
 *   - actualValue/thresholdValue：比较型三要素数据（UX-03/ERR-01——单位随
 *     kind 固定 rad/无量纲，取值口径见枚举注释表）；
 *   - level：规则级别（Must＝供处理器就地阻断/确认放行的素材；
 *     Should＝警告。固定口径：NearLimit→Should〔§7.4"Should 级默认"〕，
 *     其余四类→Must〔区间/范围非法供处理器硬断言阻断；行程超限供 SA-15
 *     ConfirmableFinding；限位违例为硬过滤素材〕）。
 *
 * 粒度契约（§9.4 稳定排序行"对象字典序×kind"的结构推论——每关节每 kind
 * 至多一条发现）：findings 是关节级判定，逐样本明细由 margins 承载
 * （JointMarginRecord 逐构型逐关节）；LimitViolated/NearLimit 的
 * actualValue 取**首个**命中样本的值（样本序升序——确定性约定，登记于
 * policy.md §15.4 v0.9）。发现自身不携带快照/策略身份（纯事实记录）——
 * 绑定由调用方组装证据/确认记录时完成（§8.4/SA-15）。
 * 线程安全：纯值。
 */
struct JointLimitFinding {
    core::ObjectId jointObject;   ///< 关节对象身份（输出定位键）
    std::string localName;        ///< 局部名（显示辅助——CON-06）
    std::string runtimeName;      ///< 运行时完整名（⑥端口解析——显示辅助）
    JointLimitFindingKind kind = JointLimitFindingKind::IntervalInvalid;  ///< 发现种类
    double actualValue = 0.0;     ///< 比较三要素·实际值（单位随 kind——见枚举注释）
    double thresholdValue = 0.0;  ///< 比较三要素·阈值/期望值（单位随 kind）
    PolicyRuleLevel level = PolicyRuleLevel::Must;  ///< 规则级别（口径见结构注释）

    bool operator==(const JointLimitFinding& o) const
    {
        return jointObject == o.jointObject && localName == o.localName
            && runtimeName == o.runtimeName && kind == o.kind
            && actualValue == o.actualValue && thresholdValue == o.thresholdValue
            && level == o.level;
    }
    bool operator!=(const JointLimitFinding& o) const { return !(*this == o); }
};

/**
 * @brief 逐构型逐关节裕量记录（§9.4 原文 margins 条目——KIN-01 关节裕量
 *        素材；四态承载见字段注释）。
 *
 * 裕量口径（§7.4 近限位比行原文：r＝距限位余量/区间半宽）：
 *   - marginToNearestLimit＝min(q−qmin, qmax−q)〔SI rad〕——位置到最近限位端
 *     的**有符号**距离：区间内 ∈[0, 半宽]；越限样本为负值（违例深度的事实
 *     记录——违例判定由 LimitViolated 发现承载，裕量数值如实保号）；
 *   - nearLimitRatioValue＝marginToNearestLimit / ((qmax−qmin)/2)〔无量纲〕
 *     ——§7.4 的 r：区间内 ∈[0,1]，越限为负（同上保号口径）。
 *
 * 四态（§4.5/§9.6"不伪造数值"口径——SourcedValue 承载）：
 *   - 有限限位且区间有效且构型位置有限 → Provided（provenance=
 *     derived-readonly，评估派生只读事实）；
 *   - continuous 关节（无限位可比）/未声明限位的有限关节 → **NotApplicable**
 *     （显式标记，KIN-01 消费方可区分"不适用"与"缺失"）；
 *   - 被跳过的非法关节（IntervalInvalid/EngineeringRangeInvalid）→ **无
 *     记录**（§9.4"跳过该关节（不伪造裕量）"原文——跳过即整个记录缺席）。
 *
 * 排序契约：margins 按 (sampleIndex 升序 → jointObject 字典序) 稳定排序
 * （实现冻结口径，登记于 policy.md §15.4 v0.9——与 findings 的
 * (对象×kind) 键互补，输出与装配顺序无关）。
 * 线程安全：纯值。
 */
struct JointMarginRecord {
    core::ObjectId jointObject;   ///< 关节对象身份（输出定位键）
    std::string localName;        ///< 局部名（显示辅助——CON-06）
    std::string runtimeName;      ///< 运行时完整名（⑥端口解析——显示辅助）
    std::size_t sampleIndex = 0;  ///< 采样位置（query.configurations 下标，0 基）
    /// 到最近限位的距离（SI rad；四态语义见结构注释）。
    core::SourcedValue<double> marginToNearestLimit;
    /// 近限位比 r（无量纲；四态语义见结构注释）。
    core::SourcedValue<double> nearLimitRatioValue;

    bool operator==(const JointMarginRecord& o) const
    {
        return jointObject == o.jointObject && localName == o.localName
            && runtimeName == o.runtimeName && sampleIndex == o.sampleIndex
            && marginToNearestLimit == o.marginToNearestLimit
            && nearLimitRatioValue == o.nearLimitRatioValue;
    }
    bool operator!=(const JointMarginRecord& o) const { return !(*this == o); }
};

/**
 * @brief 检查级显式不适用标记（实现增量载体——acceptance 3 的执行点；
 *        policy.md §15.4 v0.9 登记）。
 *
 * 为什么需要独立字段：§9.4 JointLimitEvaluation 的五成员清单（status/
 * findings/margins/diagnostics/finalized）没有"该检查整体未执行"的承载，
 * 而 P-POL-2/O-10 保守口径要求"阈值未设置→该检查输出**显式** NotApplicable
 * 标记，不伪造数值"（§4.5 四态：不适用＝字段级显式标记，正常状态、不伪造
 * 结论）。本类型即该标记：出现于 notApplicableChecks 的检查＝本次评估中
 * 该检查对**全部关节**未执行（阈值缺席或策略开关关闭），消费方不得把
 * "无该类发现"读作"检查通过"。
 *
 * 注意区分：conditionNumberWarning 是策略持有的阈值槽位（§4.4），其消费方
 * 不是本评估器（关节限位评估无条件数检查种类——§9.4 Kind 五值不含之）；
 * 其未设置时按 acceptance 3 同口径输出 NotApplicable 标记。已设置时本评估器
 * 仍不消费（权威唯一 R-6——条件数计算归其所有单元），也不产生标记。
 * 线程安全：纯值。
 */
struct JointCheckNotApplicable {
    /**
     * @brief 检查种类（实现冻结三值：两个 P-POL-2 阈值槽位＋行程校验开关）。
     */
    enum class Check {
        NearLimitRatio,         ///< 近限位比检查（阈值未设置→不适用）
        ConditionNumberWarning, ///< 条件数警告检查（阈值未设置→不适用）
        TravelLimit,            ///< 行程上限检查（travelLimitCheckEnabled=false→不适用）
    };

    Check check = Check::NearLimitRatio;  ///< 不适用检查种类
    std::string cause;                    ///< 不适用原因（可追溯——P-POL-2/策略开关）

    bool operator==(const JointCheckNotApplicable& o) const
    {
        return check == o.check && cause == o.cause;
    }
    bool operator!=(const JointCheckNotApplicable& o) const { return !(*this == o); }
};

/**
 * @brief 关节限位评估结果（§9.4 原文五成员＋notApplicableChecks 实现增量
 *        ——evaluate 的唯一输出形）。
 *
 * 终态性契约（§9.4"复用状态词表（Completed/Failed）"——CollisionQuery.hpp
 * 词表，Canceled 不在本评估器的触发面：纯标量评估无样本间协作取消点，
 * evaluate 签名亦无调用上下文参数）：
 *   - Completed＝评估完成（有/无发现皆可），finalized=true——包括检出了
 *     IntervalInvalid/EngineeringRangeInvalid 的场合：**检出≠评估失败**
 *     （D-13 诊断级不阻断，acceptance 2——判定权归命令处理器）；
 *   - Failed＝评估级失败（构型位置非有限〔§7.5 不伪造不入 findings〕或
 *     名称不可解析〔ARC-04 不猜测〕），finalized=false——非终态输出不得
 *     进入正式证据/确认流（CON-04 口径）。
 *
 * 稳定排序（§9.4 原文）：findings 按 (jointObject 字典序 → kind)；margins
 * 按 (sampleIndex → jointObject)（结构注释口径）；notApplicableChecks 按
 * Check 枚举序固定产出。同 (查询, 策略, 名称映射) 重复评估输出逐字段相等
 * （NFR-COR-02）。
 * 线程安全：纯值。
 */
struct JointLimitEvaluation {
    /// 评估状态（复用 §6.2 词表——Completed/Failed 两值在本评估器触发面）。
    CollisionEvaluationStatus status = CollisionEvaluationStatus::Failed;
    /// 关节级发现（稳定排序——结构注释；诊断级不阻断，D-13）。
    std::vector<JointLimitFinding> findings;
    /// 逐构型逐关节裕量（KIN-01 素材；四态承载——结构注释）。
    std::vector<JointMarginRecord> margins;
    /// 检查级显式不适用标记（acceptance 3 载体——JointCheckNotApplicable 注释）。
    std::vector<JointCheckNotApplicable> notApplicableChecks;
    /// 评估期诊断（POLICY-JNT-TABLE-INVALID/POLICY-JNT-ENGINEERING-RANGE-
    /// INVALID 伴随条目＋POLICY-CLL-* 失败定位——§9.6 建议码；码值权威归
    /// diagnostics StableCodeRegistry，PA-1）。
    std::vector<core::DiagnosticRecord> diagnostics;
    /// 仅 Completed=true；Failed 恒 false（非终态不入正式证据——CON-04）。
    bool finalized = false;

    bool operator==(const JointLimitEvaluation& o) const
    {
        return status == o.status && findings == o.findings && margins == o.margins
            && notApplicableChecks == o.notApplicableChecks
            && diagnostics == o.diagnostics && finalized == o.finalized;
    }
    bool operator!=(const JointLimitEvaluation& o) const { return !(*this == o); }
};

// =====================================================================
// §9.4 契约接口与唯一产品实现。
// =====================================================================

/**
 * @brief 关节限位与行程上限评估契约（§9.4 原文接口——ARC-05 唯一实现的
 *        消费面；本单元提供唯一产品实现 JointLimitEvaluator）。
 *
 * 契约（§9.4 表逐行）：前置——policy 为已发布（Valid）对象、查询满足
 * JointLimitQuery 匹配契约；后置——同参数重复评估→逐字段相等输出（纯函数、
 * 可重入、无副作用）；确定性——无随机源/无归约（NFR-COR-02）；生命周期——
 * 评估器无状态（进程级共享安全）。合法调用：处理器 prepare（AT-01）、
 * kinematics 裕量计算（KIN-01）。非法调用：以本评估器结果直接放行/阻止
 * 命令（决策归 project 处理器——SA-15/D-13，§9.4"非法调用"行原文）。
 * 线程安全：实现须并发只读安全。
 */
class IJointLimitEvaluator {
public:
    virtual ~IJointLimitEvaluator() = default;

    /**
     * @brief 执行关节限位与行程阈值评估（§9.4 原文签名——执行序与错误语义
     *        见实现类 evaluate 注释）。
     *
     * @param q      [in] 查询载体（关节表＋逐构型位置，SI rad）
     * @param policy [in] 已发布策略对象（阈值唯一来源——KIN-13/R-POL-5）
     * @param names  [in] 名称上下文（借用——调用期存活即可；runtimeName 解析源）
     * @return 评估结果（JointLimitEvaluation——稳定排序＋终态性契约见结构注释）
     *
     * @throws PolicyError(PolicyErrorCode::PolicyObjectInvalid) policy 发布态
     *         复检失败（非 Valid/身份无效——调用方违约 fail-fast）
     * @throws PolicyError(PolicyErrorCode::QueryInvalid) 查询契约违约（表/构型
     *         匹配、身份重复/无效、限位单侧、continuous 缺工程范围——装配违约
     *         fail-fast；"已提供但非法"的语义检出走诊断级发现，见类注释）
     */
    virtual JointLimitEvaluation evaluate(const JointLimitQuery& q,
                                          const EngineeringPolicySet& policy,
                                          const IPolicyNameContext& names) const = 0;
};

/**
 * @brief 关节限位评估唯一产品实现（§2.1 O-9/§9.4——ARC-05 唯一实现口径的
 *        关节限位侧落点；无状态值语义，进程级共享安全）。
 *
 * evaluate 执行序（固定——确定性 NFR-COR-02；各步错误语义见步骤注释）：
 *   ① policy 发布态复检（validationState=Valid＋policyObject/contentIdentity
 *      有效）→ 违约抛 PolicyError(PolicyObjectInvalid)——把未发布对象当
 *      策略传入＝调用方契约违约（与会话侧同码同语义，§6.4 createSession ①）；
 *   ② 查询契约校验 → 违约抛 PolicyError(QueryInvalid)：joints/configurations
 *      非空、jointObject 有效且不重复、qMin/qMax 同有同无、continuous 必填
 *      engineeringRange（"必填"＝装配契约，见 JointLimitSpec 注释两分）、
 *      每构型维度＝joints 数量；
 *   ③ 构型位置有限性 → 任一非有限（NaN/±Inf）：Failed＋POLICY-CLL-
 *      EVALUATION-FAILED（原文 %.17g 保留——NFR-COR-03 不伪造不入 findings）、
 *      finalized=false（§7.5 数值异常行同构）；
 *   ④ 名称解析（规范序遍历）→ tryRuntimeName 返回 nullopt：Failed＋
 *      POLICY-CLL-NAME-UNRESOLVED（subject 定位关节——不猜测，ARC-04）、
 *      finalized=false；
 *   ⑤ 检查级不适用标记预填（Check 枚举序）：nearLimitRatio/
 *      conditionNumberWarning 未设置→P-POL-2 口径标记；travelLimitCheckEnabled
 *      =false→行程检查停用标记；
 *   ⑥ 逐关节检查（规范序＝jointObject 字典序；输出与装配顺序无关）：
 *      有限限位关节——区间有效性（qMin＜qMax，违约→IntervalInvalid 发现＋
 *      POLICY-JNT-TABLE-INVALID 伴随诊断＋**跳过该关节**：无裕量记录、无
 *      其余检查——D-13 诊断级，状态仍 Completed）；行程比较（开关开启时，
 *      T＞L→TravelLimitExceeded——T＝L 不超限，D-08 边界含于合规侧）；逐构
 *      型裕量＋限位违例＋近限位（§7.4 口径见枚举注释）；
 *      continuous 关节——工程范围有限性（已提供但 min≥max/端点非有限→
 *      EngineeringRangeInvalid 发现＋POLICY-JNT-ENGINEERING-RANGE-INVALID
 *      伴随诊断＋跳过）；合法者裕量记录以 NotApplicable 显式标记（无限位
 *      可比——不伪造数值）；行程上限检查豁免（§7.4"continuous 关节免除"）；
 *   ⑦ 稳定排序＋终态化（findings/margins/notApplicableChecks 排序契约见
 *      JointLimitEvaluation 注释；Completed→finalized=true）。
 *
 * 阈值消费（KIN-13/R-POL-5）：行程上限＝policy.jointThresholds.
 * finiteRotationTravelLimit.siValue()（DefaultAppendixD 4π 或显式值——来源
 * 无关，单位 SI rad）；近限位比＝policy.jointThresholds.nearLimitRatio
 * （nullopt＝检查不适用）；开关＝travelLimitCheckEnabled。API 无任何阈值
 * 参数——逐查询改判在结构上不可表达。
 */
class JointLimitEvaluator final : public IJointLimitEvaluator {
public:
    /// @copydoc IJointLimitEvaluator::evaluate
    JointLimitEvaluation evaluate(const JointLimitQuery& q,
                                  const EngineeringPolicySet& policy,
                                  const IPolicyNameContext& names) const override;
};

/**
 * @brief 唯一实现的唯一构造入口（§6.5 makeRobWorkCollisionEvaluator 同款
 *        装配线口径——供 L5/请求方装配，产出无状态评估器实例）。
 *
 * @return 唯一实现实例（调用方经 unique_ptr 持有；实例无状态，亦可缺省
 *         构造 JointLimitEvaluator 直用——本入口为装配线对称性而设）
 */
std::unique_ptr<IJointLimitEvaluator> makeJointLimitEvaluator();

}  // namespace sdurws::ird::policy

#endif  // SDURWS_IRD_POLICY_JOINTLIMITS_HPP
