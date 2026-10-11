/**
 * @file   EvaluationDims.hpp
 * @brief  评估维度扩展点（WP-16-T11 铺设；WP-16-T14 首次真实递进——
 *         TRJ-08-S1 启用批；WP-16-T15 第二次递进——TRJ-08-S2 启用批；
 *         WP-16-T16 第三次递进——TRJ-08-S3 启用批）——TRJ-08"首版轨迹
 *         评估接口保持可扩展，不创建空字段、空页面或占位模块"的接口约
 *         束承载头。
 *
 * 设计依据：
 *   - units/trajectory.md §6.6（扩展机制——TRJ-08 接口约束的设计承载四条：
 *     ①载荷版本化 kindToken，v2 起新 kindToken＋新 canonical schema、旧
 *     payload 永久可读；②Profile 版本化＝证据项集合显式变化，不新增"静
 *     默维度"；③运动律词表封闭、新增值＝本卡增量修订＋实现＋黄金算例四
 *     者同批；④能力声明＝descriptor 依赖声明与 Profile 引用即能力边界
 *     ——无"空接口"）、§12.7（可扩展维度接口——S1 启用时新增运动律
 *     token＋限值消费扩展＋payload v2；S2 启用时段约束扩展版本＋Profile
 *     递进）、§6.5（范围守护——S1/S2 的承载面是 payload kindToken 递进
 *     ＋Profile 版本递进＋段约束扩展版本结构，不是 TimedSample 成员/
 *     quality 指标/诊断码/SegmentConstraint 槽位）、§13.3（trj Required-
 *     EvidenceProfile v1 五行"实现落位冻结"；v2/v3 集合随 S1/S2 启用递
 *     进）、§13.4（payload kindToken 基线）、§19.1（R2 接口交接——
 *     TRJ-08-S1/S2 行交接物全单落位）、§19.2（对 optimization 的交接
 *     —— "评估键/Profile/payload 契约稳定"）
 *   - 需求 TRJ-08（P1/C/R1——接口约束本体；S1～S3 分期交付 V13-02）、
 *     TRJ-08-S1（P1/R2——jerk 上限约束，WP-16-T14 已落位）、TRJ-08-S2
 *     （P1/R2——工艺速度约束：指定段 TCP 速度受限或恒定，验收＝约束满
 *     足断言＋超限诊断）；OPT-03/06/07（优化域经 ③端口按 descriptor 判
 *     断可组合性——本头即"契约稳定面"的落值）
 *   - 任务契约 tasks/foundation/WP-16-T11.json（acceptance 1/2——扩展
 *     点铺设，已验收）、tasks/foundation/WP-16-T14.json（acceptance 2
 *     ——已验收）、tasks/foundation/WP-16-T15.json（acceptance 2——
 *     已验收）、tasks/foundation/WP-16-T16.json（acceptance 2——扩展点
 *     消费经本头词表第三次递进，TRJ-08 接口约束不破坏、零空字段/零占
 *     位）
 *
 * 背景说明（"可扩展"在本项目的准确含义——第一读者须知）：
 *   TRJ-08 的"可扩展"**不是**预建 jerk/工艺速度/连续工艺的字段或模块等
 *   后续填充（那恰是 TRJ-08 明文禁止的"空字段/占位模块"），而是指：当
 *   TRJ-08-S1～S3 在 R2 启用时，新增维度有**既定的、版本化的接入通道**，
 *   且该通道 today 就以真实接口存在并经测试钉扎。四条通道与它们的落位：
 *
 *   1. **载荷版本化**（§6.6.1）：本头 kTrjPayloadKindToken 是评估器
 *      DomainPayload.kindToken 的唯一书写点。**WP-16-T14 批（2026-10-11，
 *      TRJ-08-S1 启用）执行首次递进**：当前产出值递进 "trj.sequence-
 *      plan.v2"（v2 canonical schema 承载 jerk 限值校验维度）。**WP-16-
 *      T15 批（2026-10-11，TRJ-08-S2 启用）执行第二次递进**：当前产出
 *      值递进 "trj.sequence-plan.v3"（v3 canonical schema 承载工艺速度
 *      校验维度）。**WP-16-T16 批（2026-10-11，TRJ-08-S3 启用）执行第
 *      三次递进**：当前产出值递进 "trj.sequence-plan.v4"（v4 canonical
 *      schema 承载连续工艺段边界行为校验维度）；v1/v2/v3 转为历史已知
 *      值（kTrjPayloadKindTokenV1/V2/V3——isKnownTrjPayloadKindToken 词
 *      表四项，旧 payload 永久可读，不可变历史 PA-2）。消费面的分派协
 *      议＝isKnownTrjPayloadKindToken：已知 token 走对应版本分支；未知
 *      token 必须拒绝（消费面 fail-fast——**不允许静默按任一版本处理**，
 *      那是把未知 schema 当已知解读的数据完整性违约）。词表只增不删。
 *   2. **Profile 版本化**（§6.6.2）：trjProfileV1Items()（v1 注册集——
 *      §13.3 表五行，已交付面**永不增改**，PA-2）、trjProfileV2Items()
 *      （WP-16-T14 批递进——v1 五项＋trj.jerk-limit-record）、
 *      trjProfileV3Items()（WP-16-T15 批递进——v2 六项＋trj.process-
 *      speed-record）与 trjProfileV4Items()（WP-16-T16 批递进——v3 七
 *      项＋trj.continuous-process-boundary，§12.7 点名词形族）并存。
 *      isTrjProfileV1ItemId/isTrjProfileV2ItemId/isTrjProfileV3ItemId/
 *      isTrjProfileV4ItemId 为各自集合的判定面——评估器组装与黄金数据
 *      集按载荷 token 选集合校验"未注册证据项不得入对应 Profile 的证据
 *      集合"。已交付集合永不收编新维度项（"静默维度"禁止——§6.6.2）。
 *   3. **运动律词表**（§6.6.3/§12.7）：PlanConfig.hpp 词表已递进为两值
 *      （kTimeParamMethodQuinticSplineC2 / kTimeParamMethodSepticSplineC3
 *      ——WP-16-T14 批"本卡增量修订＋实现＋黄金算例"同批）；TimeParam.cpp
 *      的分派点为"词表校验→按 token 分派实现分支→词表外 fail-fast"——
 *      每分支真实存在，零空分支。S2/S3 不扩运动律词表（工艺速度校验/
 *      边界行为校验都是时间化产物上的独立校验维度，非运动律形态——
 *      WP-16-T15/T16 批登记）。
 *   4. **能力声明**（§6.6.4）：kTrjEvaluationKey/kTrjEvaluatorContract-
 *      Version（TrjTypes.hpp，既有）即评估器能力边界——优化域经 ③端口
 *      按 descriptor 判断可组合性，无空接口（本头不重复承载，仅注释
 *      指路）。
 *
 *   **零占位守护（§6.5 反向约束）**：本头与配套测试把"不建"也机器化——
 *   v1 证据项清单零 jerk/工艺速度/连续工艺词根（TRJ-08 禁止项不得以任何
 *   占位 itemId 混入 v1；v2 集合的 jerk 项、v3 集合的工艺速度项、v4 集
 *   合的连续工艺边界项是各自启用批的**真实交付**，非占位——有实现、有
 *   校验、有黄金算例）；
 *   SegmentSpaceType 词表（WP-16-T16 批起三值——ContinuousProcess 为
 *   S3 启用批的真实交付）、TimedSample 五成员、SegmentConstraint 三
 *   成员的封闭性编译期/运行期断言（S1 的 jerk 表达走峰值统计/限值视图
 *   不扩 TimedSample；**S2 的工艺速度约束走段约束扩展版本结构 Process-
 *   SpeedConstraint〔§19.1 S2 行交接物〕不扩 SegmentConstraint——§6.5
 *   "SegmentConstraint 无工艺速度槽位"守护在 S2 启用后继续成立**；**S3
 *   的连续工艺段走 SegmentSpaceType 词表扩展〔§19.1 S3 行交接物明文〕
 *   ——SegmentConstraint/TimedSample 本体在 S3 启用后同样零扩展**）；
 *   TRJ-* 码表零 jerk/工艺速度/连续工艺专属码（超限类复用 TRJ-LIMIT-
 *   EXCEEDED、边界行为类复用 TRJ-CONTINUITY-BROKEN 比较型码——语义本
 *   体复用，见 test/EvaluationDimsTest.cpp 用例注释逐项对应）。
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
 * @brief 轨迹域评估器 DomainPayload.kindToken 当前产出值（§13.4 基线经
 *        §6.6.1 版本递进后的字面落值；载荷版本化的版本锚）。
 *
 * 为什么需要唯一书写点：payload 的 kindToken 是"载荷 schema 版本"的唯
 * 一标识——消费面（评估器组装、缓存判定 judgeCacheHit、优化域 ③端口组
 * 合、T13 黄金数据集）必须与产出面引用同一字面值，第二处字面量一旦与
 * 本处漂移，等于两个 schema 版本假装同一个（数据完整性违约）。修改本值
 * ＝载荷版本递进（§6.6.1）：新值＋新 canonical schema 同批，旧值转为
 * isKnownTrjPayloadKindToken 词表的"历史已知值"保留（旧 payload 永久
 * 可读），不得删除。
 *
 * 版本递进台账（本头即台账权威——词表只增不删）：
 *   - v1 "trj.sequence-plan.v1"：TRJ-08 接口约束批（WP-16-T11）冻结产
 *     出值；TRJ-08-S1 启用（WP-16-T14 批）起为历史已知值（kTrjPayload-
 *     KindTokenV1）——v1 载荷永久可读（旧 payload 不回写不改写，PA-2）。
 *   - v2 "trj.sequence-plan.v2"：TRJ-08-S1 启用批（WP-16-T14，2026-10-11）
 *     产出值——canonical schema 承载 jerk 限值校验维度（§19.1 S1 行交
 *     接物"payload v2＋Profile 递进"）；TRJ-08-S2 启用（WP-16-T15 批）
 *     起为历史已知值（kTrjPayloadKindTokenV2）——v2 载荷永久可读。
 *   - v3 "trj.sequence-plan.v3"：TRJ-08-S2 启用批（WP-16-T15，2026-10-11）
 *     产出值——canonical schema 承载工艺速度校验维度（§19.1 S2 行交接
 *     物"段约束扩展版本＋节拍分解细化"经 ProcessSpeed.hpp 校验面落
 *     地）；TRJ-08-S3 启用（WP-16-T16 批）起为历史已知值（kTrjPayload-
 *     KindTokenV3）——v3 载荷永久可读。
 *   - v4 "trj.sequence-plan.v4"：TRJ-08-S3 启用批（WP-16-T16，2026-10-11）
 *     起的当前产出值——canonical schema 承载连续工艺段边界行为校验维
 *     度（v3 schema＋trj.continuous-process-boundary 证据项对应维度的
 *     显式扩展；§19.1 S3 行交接物"SegmentSpaceType 词表扩展＋段间边界
 *     行为定义"经 TrjTypes.hpp 词表第三值＋ContinuousProcess.hpp 校验
 *     面落地，本 token 是其载荷版本锚）。
 *
 * 词形契约：点分小写 kebab＋"v<阿拉伯数字>"版本后缀（§13.4 原文形态；
 * 消费面按精确串匹配——大小写敏感、无空白容忍，见 isKnownTrjPayloadKindToken）。
 */
inline constexpr std::string_view kTrjPayloadKindToken = "trj.sequence-plan.v4";

/**
 * @brief 载荷 kindToken 历史已知值 v1（TRJ-08 接口约束批产出形态——
 *        §6.6.1"旧 payload 永久可读"的词表承载；只增不删，禁删除禁复用
 *        该词形指代其他 schema）。
 */
inline constexpr std::string_view kTrjPayloadKindTokenV1 = "trj.sequence-plan.v1";

/**
 * @brief 载荷 kindToken 历史已知值 v2（TRJ-08-S1 启用批产出形态——
 *        §6.6.1"旧 payload 永久可读"的词表承载；只增不删，禁删除禁复用
 *        该词形指代其他 schema。WP-16-T15 批表尾追加）。
 */
inline constexpr std::string_view kTrjPayloadKindTokenV2 = "trj.sequence-plan.v2";

/**
 * @brief 载荷 kindToken 历史已知值 v3（TRJ-08-S2 启用批产出形态——
 *        §6.6.1"旧 payload 永久可读"的词表承载；只增不删，禁删除禁复用
 *        该词形指代其他 schema。WP-16-T16 批表尾追加）。
 */
inline constexpr std::string_view kTrjPayloadKindTokenV3 = "trj.sequence-plan.v3";

/**
 * @brief 判定 payload kindToken 是否为轨迹域**已知**词表值（§6.6.1 消
 *        费面分派点——"分派点已存在但当前无空分支"的机器面，§12.7）。
 *
 * 词表演进纪律（本函数是唯一判定面，禁第二处词表副本）：
 *   - 当前词表＝{ "trj.sequence-plan.v4"（当前产出）, "trj.sequence-plan.
 *     v3"（历史已知——WP-16-T16 批递进）, "trj.sequence-plan.v2"（历史
 *     已知——WP-16-T15 批递进）, "trj.sequence-plan.v1"（历史已知——
 *     WP-16-T14 批递进） }；未来版本递进时在**本函数实现处表尾追加**
 *     历史值，只增不删——旧 payload 永久可读，PA-2 同源纪律；
 *   - 返回 true＝消费面可按该 token 对应版本的 schema 解读载荷；
 *   - 返回 false＝未知 token（词表外/未来版本回读旧程序/损坏数据）——
 *     **消费面必须拒绝**（fail-fast 或显式错误语义），禁止静默按任一已
 *     知版本处理（未知 schema 当已知解读＝数据完整性违约，NFR-COR-03
 *     不静默）。
 *
 * 匹配语义：精确串等值（大小写敏感、首尾空白即不同值——token 是身份面
 * 词形，不做任何规范化；身份面宽容＝身份漂移通道）。
 *
 * @param token [in] 待判定 token（evidence DomainPayload.kindToken 值；
 *              空串合法入参——判定结果 false）
 * @return true＝已知词表值；false＝未知（消费面拒绝分派）
 *
 * 纯函数；线程安全；确定性。O(词表长度×token 长度)，词表当前 4 项。
 */
inline bool isKnownTrjPayloadKindToken(std::string_view token)
{
    // 词表（版本递进时此处表尾追加历史值——只增不删，每个 if 分支对应
    // 一个真实存在的 schema 版本，零空分支）。
    if (token == kTrjPayloadKindToken) { return true; }    // v4（当前产出）
    if (token == kTrjPayloadKindTokenV3) { return true; }  // v3（历史已知）
    if (token == kTrjPayloadKindTokenV2) { return true; }  // v2（历史已知）
    if (token == kTrjPayloadKindTokenV1) { return true; }  // v1（历史已知）
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

/// trj Profile v2 新增行（WP-16-T14 批——TRJ-08-S1 启用）：jerk 限值校
/// 验记录（§12.7 点名词形"trj.jerk-limit-record"；§19.1 S1 行"Profile
/// 递进"的交接物）。适用条件＝v2 运动律（jerk 受限评估）——纯 quintic
/// 评估该证据项 NotApplicable 不计缺失（类比 HasCartesianSegment 的条件
/// 豁免语义）；该记录是 TRJ-08-S1"限值满足断言"的证据承载（校验事实：
/// 峰值统计 vs 限值＋达标判定），v1 Profile 永不注册本项（§12.7 零占位
/// 守护——isTrjProfileV1ItemId 对本词形恒 false）。
inline constexpr std::string_view kTrjItemIdJerkLimitRecord
    = "trj.jerk-limit-record";

/// trj Profile v3 新增行（WP-16-T15 批——TRJ-08-S2 启用）：工艺速度约
/// 束校验记录（§12.7 点名词形族"trj.jerk-limit-record 等"的 S2 词形
/// "trj.process-speed-record"；§19.1 S2 行"段约束扩展版本＋节拍分解细
/// 化"交接物的证据承载）。适用条件＝路径含工艺速度约束段（受限/恒定任
/// 一模式）——无工艺约束段评估该证据项 NotApplicable 不计缺失（Has-
/// CartesianSegment 的条件豁免语义同族）；该记录是 TRJ-08-S2"约束满足
/// 断言＋超限诊断"的证据承载（校验事实：逐受检段 TCP 速度峰值/带判定
/// ＋违例素材——ProcessSpeed.hpp checkProcessSpeed 产物），v1/v2 Profile
/// 永不注册本项（§12.7 零占位守护——isTrjProfileV1ItemId/isTrjProfile-
/// V2ItemId 对本词形恒 false）。
inline constexpr std::string_view kTrjItemIdProcessSpeedRecord
    = "trj.process-speed-record";

/// trj Profile v4 新增行（WP-16-T16 批——TRJ-08-S3 启用）：连续工艺段
/// 边界行为校验记录（§12.7 点名词形族"trj.jerk-limit-record 等"的 S3
/// 词形"trj.continuous-process-boundary"——词形出处＝WP-16-T15 批测
/// 试（EvaluationDimsTest ProfileV3Progression 负向断言）预登记的 S3
/// 词形代表，本批转正；§19.1 S3 行"段间边界行为定义"交接物的证据承
/// 载）。适用条件＝路径含连续工艺段（SegmentSpaceType::ContinuousPro-
/// cess 任一段）——无连续工艺段评估该证据项 NotApplicable 不计缺失
/// （HasCartesianSegment 的条件豁免语义同族）；该记录是 TRJ-08-S3"连
/// 续段边界行为断言（段间速度连续、驻留/不停驻切换明确）"的证据承载
/// （校验事实：逐边界切换语义/速度连续判定＋违例素材——Continuous-
/// Process.hpp checkProcessBoundaries 产物），v1/v2/v3 Profile 永不注
/// 册本项（§12.7 零占位守护——isTrjProfileV1ItemId/isTrjProfileV2ItemId/
/// isTrjProfileV3ItemId 对本词形恒 false）。
inline constexpr std::string_view kTrjItemIdContinuousProcessBoundary
    = "trj.continuous-process-boundary";

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
    /// 仅当运动律为 jerk 受限形态（WP-16-T14 批表尾追加——v2 Profile 行
    /// 6 trj.jerk-limit-record：septic-spline-c3 评估适用；纯 quintic 评
    /// 估→NotApplicable 不计缺失。词表只增不删——追加经本卡增量修订，
    /// 本批即 §6.6.3"增量修订＋实现＋黄金算例"同批）。
    JerkLimitedMotionLaw,
    /// 仅当路径含工艺速度约束段（WP-16-T15 批表尾追加——v3 Profile 行 7
    /// trj.process-speed-record：受检段〔受限/恒定任一模式〕存在时适
    /// 用；无工艺约束段评估→NotApplicable 不计缺失——条件豁免语义与
    /// HasCartesianSegment 同族。词表只增不删——追加经本卡增量修订，
    /// 本批即 §6.6.2"增量修订＋实现＋黄金算例"同批）。
    ProcessSpeedConstrainedSegment,
    /// 仅当路径含连续工艺段（WP-16-T16 批表尾追加——v4 Profile 行 8
    /// trj.continuous-process-boundary：SegmentSpaceType::ContinuousPro-
    /// cess 任一段存在时适用；无连续工艺段评估→NotApplicable 不计缺
    /// 失——条件豁免语义与 HasCartesianSegment 同族。词表只增不删——
    /// 追加经本卡增量修订，本批即 §6.6.2"增量修订＋实现＋黄金算例"同
    /// 批）。
    ContinuousProcessSegment,
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
    // 单一数据源：直接遍历 v1 注册集——版本递进后本函数语义不变（仍只
    // 认 v1 五项），递进不回写旧判定面（旧 Profile 永久可读）。
    for (const TrjEvidenceItemSpec& item : trjProfileV1Items()) {
        if (item.itemId == itemId) { return true; }
    }
    return false;
}

/**
 * @brief trj RequiredEvidenceProfile **v2** 证据项注册集（WP-16-T14 批
 *        ——TRJ-08-S1 启用的 Profile 版本递进；§13.3 v1 表＋新增行的唯
 *        一书写点）。
 *
 * 版本化纪律（§6.6.2——"Profile 版本递进＝证据项集合显式变化"）：
 *   - v2＝v1 五项**原样**（行序不变，PA-2——已交付行零改写）＋表尾新
 *     增行 6：trj.jerk-limit-record（Required / JerkLimitedMotionLaw）。
 *     与 kTrjPayloadKindToken 的 "v2" 后缀**同批生效**（§19.1 S1 行交接
 *     物：payload v2＋Profile 递进）；后续 S2/S3 递进以 v3 形态新落，本
 *     v2 集合届时转为历史已知集合（只增不删——v1 同款纪律）。
 *   - 返回方式：按值返回 std::array（调用方持有副本）——零静态初始化顺
 *     序问题、零共享可变状态（线程安全）；顺序＝行序（确定性，NFR-COR-02）。
 *
 * @return v2 注册集（恰 6 项；行序＝v1 五行＋jerk 行）
 *
 * 纯函数；线程安全；确定性。
 */
inline std::array<TrjEvidenceItemSpec, 6> trjProfileV2Items()
{
    // v1 五项原样转写（行序/类别/条件零改写——PA-2；与 trjProfileV1Items
    // 的清单数据源一致性由测试钉扎〔逐项对照〕）。
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
        // TRJ-08-S1 新增行（§12.7 点名词形；适用条件＝v2 运动律）。
        { kTrjItemIdJerkLimitRecord,
          TrjEvidenceItemClass::Required,
          TrjEvidenceItemApplicability::JerkLimitedMotionLaw },
    } };
}

/**
 * @brief 判定证据项 ID 是否属于 trj Profile **v2** 注册集（WP-16-T14 批
 *        判定面——评估器组装面在 payload token=v2 时据此校验证据集合注
 *        册资格；黄金数据集/契约测试以本函数锁定 v2 集合封闭性）。
 *
 * 消费语义与 isTrjProfileV1ItemId 对称：匹配语义＝精确串等值（大小写敏
 * 感；缺 "trj." 域前缀即 false——itemId 是身份面词形，不做规范化）。
 *
 * @param itemId [in] 待判定证据项 ID；空串合法入参（判定 false）
 * @return true＝属于 v2 注册集；false＝不属于（含一切未注册词形）
 *
 * 纯函数；线程安全；确定性。实现＝遍历 trjProfileV2Items()（单一数据源
 * ——禁第二处清单副本）。
 */
inline bool isTrjProfileV2ItemId(std::string_view itemId)
{
    for (const TrjEvidenceItemSpec& item : trjProfileV2Items()) {
        if (item.itemId == itemId) { return true; }
    }
    return false;
}

/**
 * @brief trj RequiredEvidenceProfile **v3** 证据项注册集（WP-16-T15 批
 *        ——TRJ-08-S2 启用的 Profile 版本递进；§13.3 v2 表＋新增行的唯
 *        一书写点）。
 *
 * 版本化纪律（§6.6.2——"Profile 版本递进＝证据项集合显式变化"）：
 *   - v3＝v2 六项**原样**（行序不变，PA-2——已交付行零改写）＋表尾新
 *     增行 7：trj.process-speed-record（Required /
 *     ProcessSpeedConstrainedSegment）。与 kTrjPayloadKindToken 的 "v3"
 *     后缀**同批生效**（§19.1 S2 行交接物）；后续 S3 递进以 v4 形态新
 *     落，本 v3 集合届时转为历史已知集合（只增不删——v1/v2 同款纪律）。
 *   - 返回方式：按值返回 std::array（调用方持有副本）——零静态初始化顺
 *     序问题、零共享可变状态（线程安全）；顺序＝行序（确定性，NFR-COR-02）。
 *
 * @return v3 注册集（恰 7 项；行序＝v1 五行＋jerk 行＋工艺速度行）
 *
 * 纯函数；线程安全；确定性。
 */
inline std::array<TrjEvidenceItemSpec, 7> trjProfileV3Items()
{
    // v1/v2 六项原样转写（行序/类别/条件零改写——PA-2；与 trjProfileV1-
    // Items/trjProfileV2Items 的清单数据源一致性由测试钉扎〔逐项对照〕）。
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
        // TRJ-08-S1 新增行（§12.7 点名词形；适用条件＝v2 运动律）。
        { kTrjItemIdJerkLimitRecord,
          TrjEvidenceItemClass::Required,
          TrjEvidenceItemApplicability::JerkLimitedMotionLaw },
        // TRJ-08-S2 新增行（§12.7 点名词形族的 S2 词形；适用条件＝路径
        // 含工艺速度约束段）。
        { kTrjItemIdProcessSpeedRecord,
          TrjEvidenceItemClass::Required,
          TrjEvidenceItemApplicability::ProcessSpeedConstrainedSegment },
    } };
}

/**
 * @brief 判定证据项 ID 是否属于 trj Profile **v3** 注册集（WP-16-T15 批
 *        判定面——评估器组装面在 payload token=v3 时据此校验证据集合注
 *        册资格；黄金数据集/契约测试以本函数锁定 v3 集合封闭性）。
 *
 * 消费语义与 isTrjProfileV1ItemId/isTrjProfileV2ItemId 对称：匹配语义＝
 * 精确串等值（大小写敏感；缺 "trj." 域前缀即 false——itemId 是身份面
 * 词形，不做规范化）。
 *
 * @param itemId [in] 待判定证据项 ID；空串合法入参（判定 false）
 * @return true＝属于 v3 注册集；false＝不属于（含一切未注册词形）
 *
 * 纯函数；线程安全；确定性。实现＝遍历 trjProfileV3Items()（单一数据源
 * ——禁第二处清单副本）。
 */
inline bool isTrjProfileV3ItemId(std::string_view itemId)
{
    for (const TrjEvidenceItemSpec& item : trjProfileV3Items()) {
        if (item.itemId == itemId) { return true; }
    }
    return false;
}

/**
 * @brief trj RequiredEvidenceProfile **v4** 证据项注册集（WP-16-T16 批
 *        ——TRJ-08-S3 启用的 Profile 版本递进；§13.3 v3 表＋新增行的唯
 *        一书写点）。
 *
 * 版本化纪律（§6.6.2——"Profile 版本递进＝证据项集合显式变化"）：
 *   - v4＝v3 七项**原样**（行序不变，PA-2——已交付行零改写）＋表尾新
 *     增行 8：trj.continuous-process-boundary（Required /
 *     ContinuousProcessSegment）。与 kTrjPayloadKindToken 的 "v4" 后缀
 *     **同批生效**（§19.1 S3 行交接物）；后续递进以 v5 形态新落，本 v4
 *     集合届时转为历史已知集合（只增不删——v1/v2/v3 同款纪律）。
 *   - 返回方式：按值返回 std::array（调用方持有副本）——零静态初始化顺
 *     序问题、零共享可变状态（线程安全）；顺序＝行序（确定性，NFR-COR-02）。
 *
 * @return v4 注册集（恰 8 项；行序＝v1 五行＋jerk 行＋工艺速度行＋连续
 *         工艺边界行）
 *
 * 纯函数；线程安全；确定性。
 */
inline std::array<TrjEvidenceItemSpec, 8> trjProfileV4Items()
{
    // v1/v2/v3 七项原样转写（行序/类别/条件零改写——PA-2；与 trjProfile-
    // V1Items/trjProfileV2Items/trjProfileV3Items 的清单数据源一致性由
    // 测试钉扎〔逐项对照〕）。
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
        // TRJ-08-S1 新增行（§12.7 点名词形；适用条件＝v2 运动律）。
        { kTrjItemIdJerkLimitRecord,
          TrjEvidenceItemClass::Required,
          TrjEvidenceItemApplicability::JerkLimitedMotionLaw },
        // TRJ-08-S2 新增行（§12.7 点名词形族的 S2 词形；适用条件＝路径
        // 含工艺速度约束段）。
        { kTrjItemIdProcessSpeedRecord,
          TrjEvidenceItemClass::Required,
          TrjEvidenceItemApplicability::ProcessSpeedConstrainedSegment },
        // TRJ-08-S3 新增行（§12.7 点名词形族的 S3 词形；适用条件＝路径
        // 含连续工艺段）。
        { kTrjItemIdContinuousProcessBoundary,
          TrjEvidenceItemClass::Required,
          TrjEvidenceItemApplicability::ContinuousProcessSegment },
    } };
}

/**
 * @brief 判定证据项 ID 是否属于 trj Profile **v4** 注册集（WP-16-T16 批
 *        判定面——评估器组装面在 payload token=v4 时据此校验证据集合注
 *        册资格；黄金数据集/契约测试以本函数锁定 v4 集合封闭性）。
 *
 * 消费语义与 isTrjProfileV1ItemId/V2/V3 对称：匹配语义＝精确串等值（大
 * 小写敏感；缺 "trj." 域前缀即 false——itemId 是身份面词形，不做规范
 * 化）。
 *
 * @param itemId [in] 待判定证据项 ID；空串合法入参（判定 false）
 * @return true＝属于 v4 注册集；false＝不属于（含一切未注册词形）
 *
 * 纯函数；线程安全；确定性。实现＝遍历 trjProfileV4Items()（单一数据源
 * ——禁第二处清单副本）。
 */
inline bool isTrjProfileV4ItemId(std::string_view itemId)
{
    for (const TrjEvidenceItemSpec& item : trjProfileV4Items()) {
        if (item.itemId == itemId) { return true; }
    }
    return false;
}

}  // namespace sdurws::ird::trajectory

#endif  // IRD_TRAJECTORY_EVALUATIONDIMS_HPP
