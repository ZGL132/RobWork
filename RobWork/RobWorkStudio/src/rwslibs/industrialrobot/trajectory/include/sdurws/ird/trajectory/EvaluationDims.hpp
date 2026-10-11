/**
 * @file   EvaluationDims.hpp
 * @brief  评估维度扩展点（WP-16-T11）——TRJ-08"首版轨迹评估接口保持可扩
 *         展，不创建空字段、空页面或占位模块"的接口约束承载头。
 *
 * 设计依据：
 *   - units/trajectory.md §6.6（扩展机制——TRJ-08 接口约束的设计承载四条：
 *     ①载荷版本化 kindToken="trj.sequence-plan.v1"，v2 起新 kindToken＋新
 *     canonical schema、旧 payload 永久可读；②Profile 版本化＝证据项集合
 *     显式变化，不新增"静默维度"；③运动律词表封闭、新增值＝本卡增量修订
 *     ＋实现＋黄金算例四者同批；④能力声明＝descriptor 依赖声明与 Profile
 *     引用即能力边界——无"空接口"）、§12.7（可扩展维度接口——motionLaw-
 *     Token 词表＋运动律分派点当前单分支零空分支；jerk/工艺速度/连续工艺
 *     段对应的新证据项不在 v1 Profile 注册——零占位）、§6.5（R1 范围守护
 *     ——不建 jerk 字段/工艺速度槽位/ContinuousProcess 段类型/空页面）、
 *     §13.3（trj RequiredEvidenceProfile 逐行实例化表——itemId 列标注
 *     "实现落位冻结"）、§13.4（EvaluationOutput.payload kindToken=
 *     "trj.sequence-plan.v1"）、§19.1（R2 接口交接——TRJ-08-S1～S3 各自
 *     的启用承接点）、§19.2（对 optimization 的交接——"评估键/Profile/
 *     payload 契约稳定"）
 *   - 需求 TRJ-08（P1/C/R1——首版轨迹评估接口保持可扩展〔为 jerk、工艺
 *     速度、连续工艺段预留评估维度〕，不创建空字段、空页面或占位模块；
 *     三个后续用户功能按 TRJ-08-S1～S3 分期交付，V13-02 拆分：接口约束
 *     R1、用户功能分期并独立验收）；OPT-03/06/07（优化域经 ③端口按
 *     descriptor 判断可组合性——本头即"契约稳定面"的落值）
 *   - 任务契约 tasks/foundation/WP-16-T11.json（acceptance 1——评估维度
 *     扩展点可扩展性评审通过；acceptance 2——零空字段/空页面/占位模块，
 *     不实现 TRJ-08-S1～S3 用户功能）
 *
 * 背景说明（"可扩展"在本项目的准确含义——第一读者须知）：
 *   TRJ-08 的"可扩展"**不是**预建 jerk/工艺速度/连续工艺的字段或模块等
 *   后续填充（那恰是 TRJ-08 明文禁止的"空字段/占位模块"），而是指：当
 *   TRJ-08-S1～S3 在 R2 启用时，新增维度有**既定的、版本化的接入通道**，
 *   且该通道 today 就以真实接口存在并经测试钉扎。四条通道与它们的落位：
 *
 *   1. **载荷版本化**（§6.6.1）：本头 kTrjPayloadKindToken 是评估器
 *      DomainPayload.kindToken 的唯一书写点（当前值 "trj.sequence-plan.
 *      v1"）。S1/S2/S3 任一启用时递进为新 token（如 "trj.sequence-plan.
 *      v2"）＋新 canonical schema，旧 token 与旧 payload 永久可读（不
 *      可变历史 PA-2）。消费面的分派协议＝isKnownTrjPayloadKindToken：
 *      已知 token 走对应版本分支；未知 token 必须拒绝（消费面 fail-fast
 *      ——**不允许静默按 v1 处理**，那是把未知 schema 当已知解读的数据
 *      完整性违约）。词表只增不删。
 *   2. **Profile 版本化**（§6.6.2）：本头 trjProfileV1Items() 是 trj 域
 *      证据项 v1 注册集的唯一书写点（§13.3 表"实现落位冻结"列的五项）。
 *      新增证据维度（如 S1 的 trj.jerk-limit-record）＝**新 itemId 常量
 *      ＋Profile 版本递进**四者同批（evidence.md §9.5 注册期验证），不得
 *      把新维度项静默塞进 v1 清单（"静默维度"禁止）。isTrjProfileV1ItemId
 *      即"该 itemId 是否属于 v1 注册集"的判定面——评估器组装与黄金数据
 *      集据此校验"未注册证据项不得入 v1 Profile 的证据集合"。
 *   3. **运动律词表**（§6.6.3/§12.7）：kTimeParamMethodQuinticSplineC2
 *      （PlanConfig.hpp）是封闭词表当前唯一值；TimeParam.cpp 的分派点为
 *      "词表校验→唯一实现分支→词表外 fail-fast"——零空分支。S1 启用时
 *      新增 token＋新运动律构造＋黄金算例同批（本头只钉扎词表唯一值纪律，
 *      不预建第二分支）。
 *   4. **能力声明**（§6.6.4）：kTrjEvaluationKey/kTrjEvaluatorContract-
 *      Version（TrjTypes.hpp，既有）即评估器能力边界——优化域经 ③端口
 *      按 descriptor 判断可组合性，无空接口（本头不重复承载，仅注释
 *      指路）。
 *
 *   **零占位守护（§6.5 反向约束）**：本头与配套测试把"不建"也机器化——
 *   v1 证据项清单零 jerk/工艺速度/连续工艺词根（TRJ-08 禁止项不得以任何
 *   占位 itemId 混入 v1）；SegmentSpaceType 两值词表、TimedSample 五成员、
 *   SegmentConstraint 三成员的封闭性编译期/运行期断言；TRJ-* 码表零 jerk
 *   码（见 test/EvaluationDimsTest.cpp 用例注释逐项对应）。
 *
 *   **P-TRJ-11 规避登记**（上游基线漂移）：本头纯值面——只依赖 C++
 *   标准库（string_view/array/cstdint），零上游单元 include、零 rw 符号，
 *   上游签名漂移对本头暴露面为零；后续评估器批（Evidence.hpp/Evaluators.
 *   hpp，§4.2 布局表）消费本头常量时亦只取值不扩展本头签名。
 *
 * 头文件依赖纪律：纯头落位（无配套 .cpp）——全部实体为 inline 常量/
 * inline 函数，值面零运行状态，双模式（集成/冒烟）无条件编译（Errors.hpp
 * "纯头"先例）。线程安全：全部实体并发只读安全（无共享可变状态）。确定
 * 性：词表/清单为编译期常量，判定函数为纯函数——同输入等价输出
 * （NFR-COR-02）。
 */

#ifndef IRD_TRAJECTORY_EVALUATIONDIMS_HPP
#define IRD_TRAJECTORY_EVALUATIONDIMS_HPP

#include <array>
#include <cstdint>
#include <string_view>

namespace sdurws::ird::trajectory {

// =====================================================================
// 载荷 kindToken 词表（§6.6.1 载荷版本化——唯一书写点）
// =====================================================================

/**
 * @brief 轨迹域评估器 DomainPayload.kindToken 当前值（§13.4 冻结基线
 *        "kindToken=\"trj.sequence-plan.v1\""的字面落值；§6.6.1 载荷版
 *        本化的版本锚）。
 *
 * 为什么需要唯一书写点：payload 的 kindToken 是"载荷 schema 版本"的唯
 * 一标识——消费面（评估器组装、缓存判定 judgeCacheHit、优化域 ③端口组
 * 合、T13 黄金数据集）必须与产出面引用同一字面值，第二处字面量一旦与
 * 本处漂移，等于两个 schema 版本假装同一个（数据完整性违约）。修改本值
 * ＝载荷版本递进（§6.6.1）：新值＋新 canonical schema 同批，旧值转为
 * isKnownTrjPayloadKindToken 词表的"历史已知值"保留（旧 payload 永久
 * 可读），不得删除。
 *
 * 词形契约：点分小写 kebab＋"v<阿拉伯数字>"版本后缀（§13.4 原文形态；
 * 消费面按精确串匹配——大小写敏感、无空白容忍，见 isKnownTrjPayloadKindToken）。
 */
inline constexpr std::string_view kTrjPayloadKindToken = "trj.sequence-plan.v1";

/**
 * @brief 判定 payload kindToken 是否为轨迹域**已知**词表值（§6.6.1 消
 *        费面分派点——"分派点已存在但当前无空分支"的机器面，§12.7）。
 *
 * 词表演进纪律（本函数是唯一判定面，禁第二处词表副本）：
 *   - 当前词表 v1＝{ "trj.sequence-plan.v1" }（单一已知值——与唯一产出
 *     面一致；未来版本递进时在**本函数实现处表尾追加**历史值，只增不删
 *     ——旧 payload 永久可读，PA-2 同源纪律）；
 *   - 返回 true＝消费面可按该 token 对应版本的 schema 解读载荷；
 *   - 返回 false＝未知 token（词表外/未来版本回读旧程序/损坏数据）——
 *     **消费面必须拒绝**（fail-fast 或显式错误语义），禁止静默按 v1 处
 *     理（未知 schema 当已知解读＝数据完整性违约，NFR-COR-03 不静默）。
 *
 * 匹配语义：精确串等值（大小写敏感、首尾空白即不同值——token 是身份面
 * 词形，不做任何规范化；身份面宽容＝身份漂移通道）。
 *
 * @param token [in] 待判定 token（evidence DomainPayload.kindToken 值；
 *              空串合法入参——判定结果 false）
 * @return true＝已知词表值；false＝未知（消费面拒绝分派）
 *
 * 纯函数；线程安全；确定性。O(词表长度×token 长度)，词表当前 1 项。
 */
inline bool isKnownTrjPayloadKindToken(std::string_view token)
{
    // 词表 v1：当前唯一已知值（版本递进时此处表尾追加历史值——只增不删，
    // 每个 if 分支对应一个真实存在的 schema 版本，零空分支）。
    if (token == kTrjPayloadKindToken) { return true; }
    return false;
}

// =====================================================================
// trj 证据 Profile 标识与 v1 证据项注册集（§6.6.2 Profile 版本化——
// §13.3 表"实现落位冻结"的唯一书写点）
// =====================================================================

/**
 * @brief trj 域 RequiredEvidenceProfile 标识（§13.3 "profileId=\"trj\""
 *        的字面落值；EvidenceProfileRegistry 注册与评估器 descriptor.
 *        profile 引用共用——评估器批落位时经本常量引用，禁第二处字面量）。
 */
inline constexpr std::string_view kTrjProfileId = "trj";

/// §13.3 行 1：分段路径与时间参数化记录（含速度/加速度限制校验）——
/// Required，适用条件＝总是（不可行替代时按替代规则豁免"成功产物"）。
inline constexpr std::string_view kTrjItemIdPathAndTimeRecord
    = "trj.path-and-time-record";
/// §13.3 行 2：平滑复检证据（细分步长、段内碰撞判定、预算占用）——
/// Required，适用条件＝总是（纯关节路径同样复检——TRJ-04 不限笛卡尔段）。
inline constexpr std::string_view kTrjItemIdSmoothRecheckEvidence
    = "trj.smooth-recheck-evidence";
/// §13.3 行 3：段内 IK 连续性检查（TRJ-02）——Required，适用条件＝路径
/// 含笛卡尔段（纯关节路径→NotApplicable，不计缺失——C2 例）。
inline constexpr std::string_view kTrjItemIdCartesianIkContinuity
    = "trj.cartesian-ik-continuity";
/// §13.3 行 4：节拍分阶段分解（接近/作业/撤离/转移/驻留）——Suggested，
/// 适用条件＝时间参数化成功时产出。
inline constexpr std::string_view kTrjItemIdCyclePhaseBreakdown
    = "trj.cycle-phase-breakdown";
/// §13.3 行 5：轨迹动画数据——Suggested，适用条件＝成功轨迹（入 payload
/// 供 ui 消费）。
inline constexpr std::string_view kTrjItemIdAnimationData
    = "trj.animation-data";

/**
 * @brief 证据项类别词表（§13.3 表 itemClass 列两值——封闭词表）。
 *
 * 词形与语义与 evidence 卡 RequiredEvidenceProfile 的 itemClass 一致
 * （evidence.md §6.1"内容归需求，各域逐行实例化，不新增/删除/改写"）：
 * Required 缺失计入汇总缺失清单；Suggested 缺失不阻断但可观察。扩展新
 * 类别值＝evidence 卡词表演进＋本卡增量修订，不得私增。
 */
enum class TrjEvidenceItemClass : std::uint8_t {
    /// 必需项（缺失→汇总缺失清单——evidence aggregateVerdict 判定面）。
    Required,
    /// 建议项（REQ-06：有效 Should 未满足给警告——不阻断）。
    Suggested,
};

/**
 * @brief 证据项适用条件词表（§13.3 表"适用条件"列的枚举化——封闭词表）。
 *
 * 为什么是词表而不是自由文本：适用条件决定"证据项缺失是否计缺失"（如
 * trj.cartesian-ik-continuity 纯关节路径→NotApplicable 不计缺失——C2
 * 例）；自由文本无法机器判定，词表值→判定语义的映射由评估器组装面消费
 * （本头只冻结词表与逐项映射，不实现判定——判定归评估器批，本头不预建
 * 其逻辑）。新增条件值＝本卡增量修订＋评估器消费实现同批。
 */
enum class TrjEvidenceItemApplicability : std::uint8_t {
    /// 总是适用（Required 行 1/2：不可行替代豁免规则另按 evidence 表 4/C6）。
    Always,
    /// 仅当路径含笛卡尔段（行 3；纯关节→NotApplicable 不计缺失——C2 例）。
    HasCartesianSegment,
    /// 仅当时间参数化成功（行 4；失败→该建议项不产出——§12.4 不伪造）。
    TimeParamSucceeded,
    /// 仅当成功轨迹（行 5；失败/取消轨迹无动画数据——§6.7 部分结果语义）。
    SuccessfulTrajectory,
};

/**
 * @brief trj 证据项注册条目（§13.3 表一行的值承载——itemId＋类别＋适用
 *        条件三要素；"对应表 4 内容"列的语义文字不入值面——它由证据项
 *        的 artifact 语义承载，卡片 §13.3 表本身是文档权威）。
 *
 * 值语义纯结构（string_view 借用常量字面——静态存储期，借出方无需管理
 * 生命周期）；线程安全。
 */
struct TrjEvidenceItemSpec {
    /// 证据项 ID（"trj." 前缀点分 kebab 词形——§13.3 表第一列原样）。
    std::string_view itemId;
    /// 类别（Required/Suggested——§13.3 表第二列）。
    TrjEvidenceItemClass itemClass;
    /// 适用条件（TrjEvidenceItemApplicability 词表——§13.3 表第四列）。
    TrjEvidenceItemApplicability applicability;
};

/**
 * @brief trj RequiredEvidenceProfile **v1** 证据项注册集（§13.3 表 trj
 *        前缀五行的唯一书写点——行序＝表行序，确定性）。
 *
 * 版本化纪律（§6.6.2——"Profile 版本递进＝证据项集合显式变化"）：
 *   - 本函数返回的集合与 kTrjPayloadKindToken 的 "v1" 后缀**同批生效**：
 *     S1/S2/S3 任一启用时，新证据项（如 trj.jerk-limit-record）以新
 *     itemId 常量＋**Profile 版本递进**承载——届时新集合以 v2 形态新落
 *     （新函数或版本参数），**本 v1 集合永不增改**（已交付注册集不可变
 *     ——PA-2；v1 清单里出现 jerk/工艺速度/连续工艺项即"静默维度"违约，
 *     §6.6.2）。
 *   - §13.3 表第六行"通用必需项（common.snapshot-identity 等）"为
 *     evidence 内建隐式附加项，不属本域清单（evidence.md §6.1"内容归
 *     需求"——本域只实例化 trj 前缀行）。
 *   - 返回方式：按值返回 std::array（调用方持有副本）——零静态初始化顺
 *     序问题、零共享可变状态（线程安全）；顺序＝§13.3 表行序（确定性，
 *     NFR-COR-02）。
 *
 * @return v1 注册集（恰 5 项；行序＝§13.3 表行序）
 *
 * 纯函数；线程安全；确定性。
 */
inline std::array<TrjEvidenceItemSpec, 5> trjProfileV1Items()
{
    return { {
        { kTrjItemIdPathAndTimeRecord,
          TrjEvidenceItemClass::Required,
          TrjEvidenceItemApplicability::Always },
        { kTrjItemIdSmoothRecheckEvidence,
          TrjEvidenceItemClass::Required,
          TrjEvidenceItemApplicability::Always },
        { kTrjItemIdCartesianIkContinuity,
          TrjEvidenceItemClass::Required,
          TrjEvidenceItemApplicability::HasCartesianSegment },
        { kTrjItemIdCyclePhaseBreakdown,
          TrjEvidenceItemClass::Suggested,
          TrjEvidenceItemApplicability::TimeParamSucceeded },
        { kTrjItemIdAnimationData,
          TrjEvidenceItemClass::Suggested,
          TrjEvidenceItemApplicability::SuccessfulTrajectory },
    } };
}

/**
 * @brief 判定证据项 ID 是否属于 trj Profile **v1** 注册集（§12.7"新增
 *        证据项经 Profile 版本递进；trj.jerk-limit-record 等**不在 v1
 *        Profile 注册**"的判定面——零占位约束的机器承载）。
 *
 * 消费语义：评估器组装面在把证据项写入 v1 Profile 的证据集合前以本函数
 * 校验注册资格——未注册项不得入 v1 集合（应走 Profile 版本递进，§6.6.2）；
 * 黄金数据集/契约测试以本函数锁定 v1 集合封闭性。匹配语义＝精确串等值
 * （大小写敏感；"path-and-time-record" 缺 "trj." 域前缀即 false——
 * itemId 是身份面词形，不做规范化）。
 *
 * @param itemId [in] 待判定证据项 ID；空串合法入参（判定 false）
 * @return true＝属于 v1 注册集；false＝不属于（v1 集合外——含未来 S1～
 *         S3 维度项与一切未注册词形）
 *
 * 纯函数；线程安全；确定性。实现＝遍历 trjProfileV1Items()（单一数据源，
 * 清单扩面自动一致——禁第二处清单副本）。
 */
inline bool isTrjProfileV1ItemId(std::string_view itemId)
{
    // 单一数据源：直接遍历 v1 注册集——未来 v2 集合落位时本函数语义不变
    // （仍只认 v1 五项），版本递进不回写旧判定面（旧 Profile 永久可读）。
    for (const TrjEvidenceItemSpec& item : trjProfileV1Items()) {
        if (item.itemId == itemId) { return true; }
    }
    return false;
}

}  // namespace sdurws::ird::trajectory

#endif  // IRD_TRAJECTORY_EVALUATIONDIMS_HPP
