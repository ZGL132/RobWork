/**
 * @file   Preflight.hpp
 * @brief  优化预检 Preflight（OPT-11）——运行启动前的结构化只读检查面：
 *         20 检查项全表（§6.4）、阻塞/警告计数、逐项定位五元组
 *         {checkId, level, subject, basis, suggestion}、allowances 五元组、
 *         以及优化运行描述 OptimizationRunSpec（§4.3——WP-20-T06 移交的
 *         Preflight 消费面）。任务 WP-20-T08。
 *
 * 设计依据：
 *   - units/optimization.md §6.4（Preflight 检查项全表 20 项——每项级别/
 *     需求依据；输出五元组与 blockerCount/warningCount 汇总；allowances
 *     五元组定义；"Preflight 本身不改项目、不写磁盘、不产生修订（只读
 *     检查面）；诊断经 OPT-PREFLIGHT-BLOCKED（warning 级目录条目）汇总
 *     呈现"）、§6.5（阶段锁诊断是运行启动阻塞非候选淘汰）、§6.6
 *     （OPT-* 码登记表——本面不产诊断实例，稳定码消费归呈现层）、
 *     §4.3（OptimizationRunSpec 字段原文＋config.opt canonical 承载）、
 *     §4.4 I-OPT-1（输入冻结——spec.revision 与组装快照一致性）、§5.2/
 *     §5.7（绑定校验与阶段锁——#3/#10/#14 的规则来源）、§8.2（基线候选
 *     ——Evaluate Baseline 的执行形态已在 WP-20-T06 编排落位，本任务以
 *     用例钉扎其三语义面，见文件尾"Evaluate Baseline 承接"注）、§12.2
 *     （IOptimizationPreflightService 签名逐字——preflight(spec) 单参数；
 *     @pre spec.revision 可读、评估器注册表已装配）、§12.3（线程行——
 *     preflight const 只读可并发；错误语义——调用方错误 fail-fast）、
 *     §16.3 P-OPT-6（研究配置持久化通道裁决前：会话态＋导出副本承载，
 *     不入 .rwdesign——本面是纯函数检查面，零持久化写路径）
 *   - REQUIREMENTS §15 OPT-11（P0：优化运行前提供预检（Preflight）：阻塞/
 *     警告计数与逐项定位（写集冲突、未登记绑定、缺失上游工件等）；支持
 *     基线方案评估（Evaluate Baseline）作为候选比较基准）
 *   - 任务契约 tasks/foundation/WP-20-T08.json（acceptance 1~3；knownPitfalls
 *     P-OPT-6；outputs＝implementation/unit-tests/traceability-update）
 *
 * ★ 落位范围口径（诚实登记，防扩大）：§14.1 行"WP-20-T08 → §6.4/§8.2 →
 *   Preflight.hpp＋实现"。本头交付：①OptimizationRunSpec 运行描述值类型
 *   ＋validateRunSpec（§4.3——T06 登记注移交："OptimizationRunSpec 本批
 *   不落……Preflight 消费随 WP-20-T08"）；②PreflightReport/PreflightFinding/
 *   PreflightAllowances 报告数据面（§6.4 输出契约逐字——五元组五字段，
 *   不私增字段）；③IOptimizationPreflightService/OptimizationPreflightService
 *   （§12.2 O3 面——preflight(spec) 纯函数）；④PreflightInputs 检查输入
 *   载荷（§6.4 输入面"OptimizationRunSpec＋②查询端口＋评估器注册表清单
 *   ＋evidence 兼容判定"的构造注入形态——投影值/注册表只读指针，与 T07
 *   Applier 的 ProjectBaselineState 投影先例同款）。**不落**：IOptimization-
 *   RunController（§12.2 运行控制——T07 登记注已划归 WP-21 R2 任务提交
 *   通道，本批零新增消费者，NFR-MNT-04）；Evaluate Baseline 的新增执行件
 *   （§8.2 基线候选＝空补丁候选恒生成参与评估——执行形态已由 T06
 *   generateSeededLhsCandidates 批首基线＋TwoStageEvaluationOrchestrator
 *   同管线承载，本任务以 OPT-VER-134 三语义面用例钉扎，不加第二套基线
 *   评估代码）；研究配置持久化（P-OPT-6 裁决前不入 .rwdesign——会话态
 *   ＋T09 导出副本承载，本面零 io 写调用）。
 *
 * 背景说明（第一读者须知——三件事）：
 *   ① **Preflight 是只读报告面**（§6.4 @post"不修改任何项目状态；不产生
 *      诊断目录条目以外副作用"）：同输入同结论（纯函数，NFR-COR-02），
 *      可重复调用、可并发（§12.3 线程行）；它不评估候选、不编译模型、
 *      不写任何存储——检查全部基于构造注入的**投影值与注册表只读清单**
 *      （基线状态/tip/链型事实由 L5 装配面自②端口投影，与 Applier 先例
 *      同款"投影值而非端口引用"——端口引用会引入时序依赖，破坏纯函数面）。
 *   ② **五元组即呈现契约**（§6.4 输出原文）：每项 finding 只携带
 *      {checkId, level, subject, basis, suggestion} 五字段——checkId＝
 *      §6.4 表行号（1..20，跨版本稳定）；basis＝需求或契约 ID 原文；
 *      subject 为对象/绑定/评估键定位。域诊断码的目录呈现（OPT-PREFLIGHT-
 *      BLOCKED，warning 级）由调用方按 blockerCount 汇总产出——本面不产
 *      诊断实例（诊断实例须经 diagnostics 工厂消费已注册码，T04 I-C3 同
 *      款纪律），故 finding 不携带码字段（卡面五元组亦无此字段）。
 *   ③ **检查全量执行不短路**：Preflight 的价值在"一次给全缺项清单"
 *      （UX-10"未完成附缺项列表"同型）——20 项逐项独立判定、全部执行，
 *      findings 按检查项序稳定排列（确定性首序＝checkId 升序）；fail-fast
 *      只用于调用方契约违约（validateRunSpec——spec 本身非法时无法定位
 *      到任何检查项，属程序错误而非研究缺陷）。
 *
 * 线程约束：preflight()/validateRunSpec() const 只读、可并发（§12.3）；
 *   OptimizationPreflightService 实例持有的 PreflightInputs 为构造后只读
 *   快照（AnalysisSnapshot 自身冻结不可变；注册表指针只读消费——注册表
 *   运行期 find/manifest 并发只读安全）。确定性：同 (spec, inputs) ⇒ 同
 *   PreflightReport（纯函数——NFR-COR-02）。
 */

#ifndef SDURWS_IRD_OPTIMIZATION_PREFLIGHT_HPP
#define SDURWS_IRD_OPTIMIZATION_PREFLIGHT_HPP

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sdurws/ird/core/Digest.hpp>      // core::ContentIdentity——快照/Profile
                                            //  内容身份（§4.3 spec.snapshotId）
#include <sdurws/ird/core/Identity.hpp>    // core::ProjectId/BranchId/RevisionId
                                            //  ——输入身份三元组（§4.3）
#include <sdurws/ird/evidence/Evaluator.hpp>  // evidence::EvaluatorRegistry/
                                            //  EvidenceProfileRegistry——评估器
                                            //  清单与 Profile 注册表（检查 7/9/
                                            //  17/20 消费；只读指针）
#include <sdurws/ird/evidence/Snapshot.hpp>   // evidence::AnalysisSnapshot——冻结
                                            //  输入快照（§4.1；检查 4/5/6/8/13/
                                            //  18 消费）
#include <sdurws/ird/optimization/Applier.hpp>  // ProjectBaselineState——②端口
                                            //  基线状态投影（检查 1/2 消费；
                                            //  T07 既有类型零第二定义）
#include <sdurws/ird/optimization/EvaluatorPorts.hpp>  // OptimizationConfiguration
                                            //  ——config.opt 载荷（spec.config；
                                            //  T06 既有契约）
#include <sdurws/ird/optimization/Types.hpp>   // OptimizationError——域异常

namespace sdurws::ird::optimization {

// =====================================================================
// 跨单元稳定 token 字面常量（R-1/R-2 红线的字面承载——本单元零 modeling/
// requirements/project 编译边，不可 include 其头文件；字面值经契约测试
// 钉扎，权威在各自单元：对象类型 token＝modeling ObjectTypes.hpp 常量、
// req-* token＝requirements src 编码常量、PRJ-* 码＝project 稳定码登记表。
// T04 评估键字面常量 kKinTaskPointsBatchKey 同款先例）
// =====================================================================

/// modeling 根对象类型 token（RobotDesign——检查 4 的基线设计工件锚）。
inline constexpr std::string_view kObjectTypeRobotDesign = "robot-design";
/// modeling 工具定义对象类型 token（TCP 挂其 tcpList——变量 #7 权威来源；
/// 检查 4 的 TCP 引用类型核对锚）。
inline constexpr std::string_view kObjectTypeToolDefinition = "tool-definition";
/// modeling 传动设计对象类型 token（检查 4 的传动比变量权威工件锚）。
inline constexpr std::string_view kObjectTypeRobotDrivetrain = "robot-drivetrain";
/// requirements 根集对象类型 token（检查 5——D-REQ-1 五对象分解的根）。
inline constexpr std::string_view kObjectTypeReqRootSet = "req-set";
/// requirements 任务点集合对象类型 token（检查 5 四集合之一）。
inline constexpr std::string_view kObjectTypeReqPointSet = "req-point-set";
/// requirements 工作区域集合对象类型 token（检查 5 四集合之一）。
inline constexpr std::string_view kObjectTypeReqRegionSet = "req-region-set";
/// requirements 工况集合对象类型 token（检查 5 四集合之一）。
inline constexpr std::string_view kObjectTypeReqConditionSet = "req-condition-set";
/// requirements 采样计划集合对象类型 token（检查 5 四集合之一）。
inline constexpr std::string_view kObjectTypeReqPlanSet = "req-plan-set";
/// optimization 域证据 Profile id（检查 9——五域词表成员，EVI-01 表 4）。
inline constexpr std::string_view kOptProfileId = "opt";
/// project 稳定码字面（检查 2——写集冲突提示 StaleRevisionRejected 风险；
/// 码值权威＝project 稳定码登记表，本单元字面承载＋契约测试钉扎）。
inline constexpr std::string_view kPrjStaleRevisionRejectedCode
    = "PRJ-STALE-REVISION-REJECTED";

// =====================================================================
// 检查项编号词表（§6.4 表行号 1..20——checkId 的取值域常量；行号一经
// 交付不得改动/复用，跨版本稳定——报告/呈现/测试的定位锚）
// =====================================================================

/// 检查 1：项目/分支/修订存在且当前修订可读（CON-01、②端口）。
inline constexpr std::uint8_t kPreflightCheckRevisionReadable = 1;
/// 检查 2：写集冲突（expectedRevision ≠ 当前 tip——PM-04）。
inline constexpr std::uint8_t kPreflightCheckWriteSetConflict = 2;
/// 检查 3：未登记变量绑定（§5.2、I-OPT-7）。
inline constexpr std::uint8_t kPreflightCheckUnregisteredBinding = 3;
/// 检查 4：缺少 modeling 工件（§5.3、P-MDL-6）。
inline constexpr std::uint8_t kPreflightCheckModelingArtifacts = 4;
/// 检查 5：缺少 requirements 工件（requirements §8.2）。
inline constexpr std::uint8_t kPreflightCheckRequirementArtifacts = 5;
/// 检查 6：缺少必验工况（EVI-02、REQ-06——P-EV-7 同源保守）。
inline constexpr std::uint8_t kPreflightCheckRequiredCases = 6;
/// 检查 7：缺少评估器（§6.1、EvaluatorSetId）。
inline constexpr std::uint8_t kPreflightCheckEvaluatorMissing = 7;
/// 检查 8：缺少 policy（CON-06）。
inline constexpr std::uint8_t kPreflightCheckPolicyMissing = 8;
/// 检查 9：缺少 RequiredEvidenceProfile（EVI-01 表 4）。
inline constexpr std::uint8_t kPreflightCheckProfileMissing = 9;
/// 检查 10：阶段能力冲突（OPT-03/§15.0——阶段锁目标面）。
inline constexpr std::uint8_t kPreflightCheckStageCapability = 10;
/// 检查 11：P-04 未冻结而请求 OPT-D（附录 C（O-28）、OPT-09）。
inline constexpr std::uint8_t kPreflightCheckP04NotFrozen = 11;
/// 检查 12：目录版本缺失（SEL-08——R2 离散变量引用的目录值域未锁定）。
inline constexpr std::uint8_t kPreflightCheckCatalogMissing = 12;
/// 检查 13：输入身份不一致（§4.4 I-OPT-1）。
inline constexpr std::uint8_t kPreflightCheckIdentityMismatch = 13;
/// 检查 14：不支持的变量（§5.3/§5.7——阻塞或警告，见 finding level）。
inline constexpr std::uint8_t kPreflightCheckUnsupportedVariable = 14;
/// 检查 15：不支持的链型（§2.1 支持矩阵、MDL-12）。
inline constexpr std::uint8_t kPreflightCheckUnsupportedChain = 15;
/// 检查 16：资源预算不合法（§4.3）。
inline constexpr std::uint8_t kPreflightCheckBudgetInvalid = 16;
/// 检查 17：目标配置不可计算（OPT-07、§7.3）。
inline constexpr std::uint8_t kPreflightCheckObjectiveNotComputable = 17;
/// 检查 18：外部资源未固化（CON-03、PM-01）。
inline constexpr std::uint8_t kPreflightCheckExternalResource = 18;
/// 检查 19：预估候选量超出预算（§4.3——警告，截断并计数）。
inline constexpr std::uint8_t kPreflightCheckEstimateOverBudget = 19;
/// 检查 20：版本兼容提示（EV-COV-4——警告，提示不可直接比较）。
inline constexpr std::uint8_t kPreflightCheckVersionCompatibility = 20;

// =====================================================================
// 优化运行完整描述（卡 §4.3 原文——WP-20-T06 登记注移交本任务落位；
// 消费面＝Preflight/运行启动。落位 Preflight.hpp 而非卡面 §4.3 Draft
// 标注的 Types.hpp：与 T06 OptimizationConfiguration 落 EvaluatorPorts.hpp
// 同因（Types.hpp 被 Objective.hpp/Variable.hpp 反向依赖，include 环），
// 且跟随首个消费者——偏差随单元卡增量修订登记，DTB §5.4）
// =====================================================================

/**
 * @brief 优化运行完整描述（卡 §4.3 原文字段——一次运行的冻结面）。
 *
 * 生命周期：研究定义编辑产物（RunPhase::Draft 态组装）；进入 Preflight
 * 前必须完成快照组装并冻结（I-OPT-1——spec.snapshotId 即组装完成的快照
 * 身份，与快照本体的一致性由检查 13 核对）；修改研究定义＝新
 * OptimizationRunSpec（旧运行归档保留，PA-2）。
 *
 * P-OPT-6 承载位（裁决前口径）：本类型是**会话态**值类型——跨会话恢复
 * 走研究结果 JSON 副本（§11.4 工件 1，T09 导出面），**不入 .rwdesign**
 * （项目文件零持久化通道；本头不提供任何序列化到项目存储的 API）。
 *
 * 值语义；线程安全：纯值。
 */
struct OptimizationRunSpec {
    core::ProjectId project{};   ///< 项目身份（须 isValid）
    core::BranchId branch{};     ///< 方案分支身份（须 isValid）
    core::RevisionId revision{}; ///< 运行输入修订（冻结锚——运行绑定原修订）
    core::ContentIdentity snapshotId{}; ///< 组装完成的快照身份（§4.1；须非
                                         ///  保留值——检查 13 与快照本体核对）
    OptimizationConfiguration config{}; ///< config.opt 载荷（T06——canonical
                                         ///  进 sliceId；校验面
                                         ///  validateConfiguration）
    evidence::EvidenceProfileRef profile{}; ///< "opt" 域 Profile 引用三元组
                                             ///  （EVI-01 表 4——卡面 §4.3 草拟
                                             ///  名 RequiredEvidenceProfileRef
                                             ///  在 evidence 磁盘头中不存在，
                                             ///  按 T06 增量③同款取磁盘实况类型）
    std::string createdBy{};     ///< 创建者（会话/用户标识——审计用；不参与
                                  ///  任何身份）
    /// 创建时刻（UTC——审计面；std::chrono::system_clock 纪元，不参与身份）。
    std::chrono::system_clock::time_point createdAtUtc{};
};

/**
 * @brief 校验运行描述（fail-fast 轨——调用方契约违约抛 OptimizationError）。
 *
 * 校验规则（检查序固定——确定性首错；全部不静默修正——NFR-COR-03）：
 *   ① project/branch/revision 三身份须 isValid（保留值＝未组装）；
 *   ② snapshotId 须非保留值（快照身份是检查 13 的核对锚，空值无法核对）；
 *   ③ config 经 validateConfiguration 全量复验（种子/预算/策略/目标/
 *      绑定元数据——T06 十条检查序）；
 *   ④ profile.profileId 必须为 "opt"（运行 Profile 是优化域 Profile——
 *      五域词表成员中的本域项，EVI-01 表 4；他域 Profile 引用属组装错误）；
 *   ⑤ profile.version 非空（三元组版本分量——Profile 解析的键之一）；
 *   ⑥ profile.contentIdentity 须非保留值（三元组身份分量）。
 *
 * 双轨分工（为什么 ③ 在这里抛、在 preflight 里却是检查 16 报告轨）：
 *   本函数是**程序化组装面的独立 fail-fast 消费点**（组装完成即校验，
 *   T06 validateConfiguration 构造纪律的运行描述层入口）；而 preflight
 *   的价值在"一次给全缺项清单"（UX-10 同型）——config 语义缺陷走检查 16
 *   报告轨逐项定位，用户修一处即可重检全部，不必修一个错抛一次异常。
 *   preflight 内部只复用①②④⑤⑥（身份/Profile 面——无法定位到检查项的
 *   组装不完整），config 面交检查 16。
 *
 * @param spec [in] 待校验运行描述
 * @throws OptimizationError(kOptInputInvalid) ①~⑥任一违例（消息含字段
 *         定位与期望形态——ERR-01 比较型定位纪律）
 *
 * 纯函数；线程安全（可重入）。
 */
void validateRunSpec(const OptimizationRunSpec& spec);

// =====================================================================
// Preflight 报告数据面（§6.4 输出契约——五元组/计数/allowances 五元组）
// =====================================================================

/**
 * @brief 检查项级别（§6.4"级别（命中即）"列——blocking＝运行启动阻断；
 *        warning＝随行提示不阻断）。
 */
enum class PreflightLevel {
    Blocking, ///< 阻塞（命中即计入 blockerCount——allowStart/allowQuick 为 false）
    Warning,  ///< 警告（命中计入 warningCount——不阻断启动；如 #19/#20）
};

/**
 * @brief 检查项级别稳定 token（"blocking"/"warning"——报告/导出的确定性
 *        书写；同 Types.hpp 各词表 toToken 纪律）。
 * @param l [in] 级别枚举值
 * @return 稳定 token 视图（编译期字面量，生命周期静态）
 */
std::string_view toToken(PreflightLevel l) noexcept;

/**
 * @brief 单条检查发现（§6.4 输出五元组**逐字**——{checkId, level, subject,
 *        basis, suggestion}；不私增字段：域诊断码的目录呈现由调用方按
 *        blockerCount 汇总产出 OPT-PREFLIGHT-BLOCKED 条目（warning 级），
 *        逐项定位走本结构五字段——§6.4 尾段原文）。
 *
 * 值语义；线程安全：纯值。
 */
struct PreflightFinding {
    std::uint8_t checkId = 0;    ///< 检查项编号（§6.4 表行号 1..20——上方
                                  ///  kPreflightCheck* 常量取值域）
    PreflightLevel level = PreflightLevel::Blocking; ///< 级别（命中即）
    std::string subject = {};    ///< 对象/绑定/评估键定位（ERR-01 对象面——
                                  ///  如 "obj-…"/"mdl.joint[2].dh.a"/
                                  ///  "opt-static-screen"/"req-point-set"）
    std::string basis = {};      ///< 需求或契约 ID（§6.4 表"需求/契约依据"
                                  ///  列原文——如 "PM-04"、"EVI-02、REQ-06"）
    std::string suggestion = {}; ///< 修复建议（中文——面向用户的动作指引；
                                  ///  检查 2 的 StaleRevisionRejected 风险
                                  ///  提示在本字段携带码名）
};

/**
 * @brief allowances 五元组（§6.4 输出契约——逐条与卡面定义同序同义）。
 *
 * 五位独立计算（防御式——每条件独立成立，不依赖检查项级别联动；卡面
 * allowVerified 的"必验工况非空/外部资源已固化"与检查 6/18 有意冗余：
 * 即使某检查项将来降级，allowances 条件仍独立成立）。
 *
 * 值语义；线程安全：纯值。
 */
struct PreflightAllowances {
    /// 无阻塞项（可提交 Quick 筛选任务——§6.4 allowStart 行）。
    bool allowStart = false;
    /// 无阻塞项且评估器支持 Preview（仅草稿预览，不产生正式证据——§6.4
    ///  allowPreview 行；R1 opt-static-screen 声明 {Quick,Verified}，
    ///  不支持 Preview ⇒ R1 恒 false，按注册 descriptor 事实查询不伪造）。
    bool allowPreview = false;
    /// 无阻塞项（§6.4 allowQuick 行——与 allowStart 同判据独立登记）。
    bool allowQuick = false;
    /// allowQuick ＋ 必验工况非空 ＋ 外部资源已固化（CON-03）＋ 覆盖矩阵
    /// 可达（§6.4 allowVerified 行——四条件独立评估）。
    bool allowVerified = false;
    /// allowVerified ＋ 契约版本匹配（Profile/评估器/算法/导出契约当前
    /// ——§6.4 allowFormalExport 行；R1 静态面承载＝评估器契约版本匹配
    /// ＋Profile 身份与注册表权威一致；导出契约三元组的"当前性"终判归
    /// T09 buildExportBundle（OPT-EXPORT-CONTRACT-STALE 执行点）——本位
    /// 只承载研究定义可预判部分，不伪造完整导出契约校验，诚实登记）。
    bool allowFormalExport = false;
};

/**
 * @brief Preflight 报告（§6.4 输出——检查发现全表＋计数汇总＋allowances
 *        五元组＋本次评估器集摘要）。
 *
 * findings 排序契约：按 checkId 升序、同 checkId 内按发现序（确定性——
 * NFR-COR-02；呈现层可稳定呈现缺项清单）。blockerCount/warningCount 与
 * findings 逐项 level 计数一致（不变式——测试钉扎）。
 *
 * evaluatorSetDigest：本次评估器注册清单摘要（evidence manifest digest
 * ——§4.2 辅助身份 EvaluatorSetId 的承载）；registry 缺失时为保留值
 * （检查 7 已阻塞，摘要无从计算）。运行启动后应把它登记进运行记录
 * （复现核对锚——消费归运行编排面）。
 *
 * 值语义；线程安全：纯值。
 */
struct PreflightReport {
    std::vector<PreflightFinding> findings = {}; ///< 全部发现（按 checkId 升序）
    std::uint32_t blockerCount = 0;  ///< 阻塞项计数（＝level==Blocking 条数）
    std::uint32_t warningCount = 0;  ///< 警告项计数（＝level==Warning 条数）
    PreflightAllowances allowances{}; ///< 五元组（§6.4）
    core::ContentIdentity evaluatorSetDigest{}; ///< 本次评估器集摘要
                                                 ///  （EvaluatorSetId 承载——
                                                 ///  §4.2；registry 缺失＝保留值）
};

// =====================================================================
// 检查输入（§6.4 输入面——构造注入；L5 装配形态）
// =====================================================================

/**
 * @brief 上次运行身份（检查 20 的兼容核对输入——EV-COV-4"评估器集/
 *        Profile 与上次运行不一致⇒提示不可直接比较"）。
 *
 * 来源：上次运行记录登记的 EvaluatorSetId（manifest 摘要）与 Profile
 * 内容身份（消费归运行编排/历史装载面——R1 会话内由调用方供给；跨会话
 * 历史装载随 T09 归档面）。无可比历史时缺省（nullopt——检查 20 不触发，
 * 如实不伪造可比性）。
 *
 * 值语义；线程安全：纯值。
 */
struct LastRunIdentity {
    core::ContentIdentity evaluatorSetDigest{}; ///< 上次运行的评估器集摘要
    core::ContentIdentity profileContentIdentity{}; ///< 上次运行的 Profile
                                                     ///  内容身份
};

/**
 * @brief Preflight 检查输入（§6.4"输入＝OptimizationRunSpec＋②查询端口
 *        ＋评估器注册表清单＋evidence 兼容判定"的构造注入载荷——服务
 *        构造时快照化，preflight() 运行期只读）。
 *
 * 为什么是投影值＋只读指针而不是端口引用：§12.3 生命周期行"服务实例由
 * L5 装配注入，运行期只读"＋preflight 纯函数面（可并发可重放）——端口
 * 引用会在检查执行时拉取可变项目状态（时序依赖，破坏同输入同结论）；
 * 调用方在组装 spec 的同一时点自②端口投影基线状态（Applier 先例同款），
 * 自装配面取得注册表只读指针。基线状态在检查后前进（tip 变化）属于
 * 并发世界的事实演进——提交期的 expectedRevision 失配兜底（检查 2 的
 * finding 本就是"预期标记"语义，Applier 同注）。
 *
 * 构造校验：snapshot.snapshotId 须非保留值、三身份须 isValid（检查输入
 * 不完整＝装配违约 fail-fast）；registry/profileRegistry 允许 nullptr
 * （对应检查 7/9/17 如实报告缺失——注册表未装配是研究环境的合法缺项
 * 状态，Preflight 的职责正是把它定位出来，不能反过来 fail-fast 拒检）。
 *
 * 值语义（指针成员为非 owning 只读借用——调用方保证 preflight() 期间
 * 存活）；线程安全：构造后只读，preflight() 可并发。
 */
struct PreflightInputs {
    evidence::AnalysisSnapshot snapshot{}; ///< 冻结输入快照（§4.1——检查
                                            ///  4/5/6/8/13/18 的消费源）
    ProjectBaselineState baseline{};       ///< ②端口基线状态投影（检查 1/2
                                            ///  ——branch/tip/writable；
                                            ///  writable 不在本面 20 项检查域
                                            ///  ——只读项目处置归 Applier
                                            ///  PM-07 通道化，不发明第 21 项）
    const evidence::EvaluatorRegistry* evaluatorRegistry = nullptr; ///< 评估器
                                            ///  注册表清单（检查 7/17/20；
                                            ///  可空＝未装配——检查 7 阻塞）
    const evidence::EvidenceProfileRegistry* profileRegistry = nullptr; ///<
                                            ///  Profile 注册表（检查 9；可空
                                            ///  ＝未装配——检查 9 阻塞）
    /// 基线链型在 R1 启用范围内（检查 15 的链型事实投影——P-OPT-2 裁决前
    /// 由调用方/测试供给，T04 StaticHardConstraintInput/T06 CandidateProjection
    /// 同名字段同款接缝；生产适配器＝链型门投影，随 P-OPT-2 落位）。
    bool baselineChainInEnabledScope = true;
    /// P-04 冻结位（检查 11——附录 C 修订留痕落位后由装配供给 true；R1 期
    /// 恒 false：WP-21-T01 冻结产物不存在，OPT-D 正式联合优化恒阻塞。
    /// 参数化而非硬编码 false：WP-21-T01 落位时装配面增量即可，检查逻辑
    /// 不改——§8.6"启用前置：P-04 冻结→WP-21-T02 增量修订本卡引用冻结值"）。
    bool p04Frozen = false;
    std::optional<LastRunIdentity> lastRun{}; ///< 上次运行身份（检查 20；
                                            ///  可空＝无可比历史）
};

// =====================================================================
// Preflight 服务（§12.2 O3 面——签名逐字）
// =====================================================================

/**
 * @brief 优化预检服务接口（§12.2 原文——运行启动前的结构化检查（OPT-11）。
 *        只读、无副作用、可重复调用）。
 *
 * 实现契约（§12.2/§12.3 逐条）：
 *   - @pre spec.revision 可读（快照已组装并冻结——I-OPT-1）；评估器注册表
 *     已装配完成（L5 装配期后——未装配时检查 7/9 如实阻塞，不是前置违约）；
 *   - @post 不修改任何项目状态；不产生诊断目录条目以外副作用（纯函数——
 *     P-OPT-6 会话态承载的检查面表达：零写盘、零项目修订）；
 *   - @throws OptimizationError(kOptInputInvalid) spec 字段非法（调用方
 *     契约违约，fail-fast——本域异常轨；与下方 preflight() 方法注同轨，
 *     G-ACC-T08-1 对齐：类注草拟的 std::invalid_argument 与实现不符，
 *     本域唯一异常类型＝OptimizationError，Types.hpp）；
 *   - @threadSafe 是（只读消费查询端口与注册表清单）；
 *   - @cancellation 预检为轻量纯检查（<1 s），不提供取消；
 *   - @determinism 同输入同结论（纯函数面）。
 */
class IOptimizationPreflightService {
public:
    virtual ~IOptimizationPreflightService() = default;

    /**
     * @brief 执行 Preflight，输出阻塞/警告清单与 allowances 五元组（§6.4）。
     *
     * @param spec [in] 运行描述（调用方持有，调用期间有效；先经
     *             validateRunSpec 校验——本实现入口防御复验）
     * @return 报告（20 检查项全量执行——不短路；findings 按 checkId 升序）
     *
     * @throws OptimizationError(kOptInputInvalid) spec 自身组装不完整
     *         （validateRunSpec 拒绝面——见其注释"为什么这些是异常轨"）
     */
    virtual PreflightReport preflight(const OptimizationRunSpec& spec) const = 0;
};

/**
 * @brief Preflight 服务实现（检查输入构造注入——L5 装配形态；全部检查
 *        逻辑的唯一定义点）。
 *
 * 执行序（固定——确定性；20 项全量执行不短路，findings 按 checkId 升序）：
 *   1. spec 身份/Profile 面 fail-fast（①②④⑤⑥——与 validateRunSpec 同源
 *      校验的 config 除外子集：config 语义缺陷走检查 16 报告轨，见
 *      validateRunSpec 注"双轨分工"）；
 *   2. 检查 1（修订可读）：baseline.branch 与 spec.branch 一致且 tip 非保留
 *      值——②端口投影面（CON-01）；
 *   3. 检查 2（写集冲突）：baseline.tip ≠ spec.revision → 阻塞（提示
 *      PRJ-STALE-REVISION-REJECTED 风险——PM-04）；
 *   4. 检查 16（预算合法）：validateConfiguration(spec.config) 捕获异常转
 *      阻塞 finding（报告面不抛——研究缺陷逐项定位，§4.3）；
 *   5. 检查 3＋14（绑定面）：OptimizationVariableProvider(stage).
 *      validateBindings(变量集, 快照)——kOptInputInvalid/kOptVarLocked
 *      issue → 检查 3 阻塞；kOptStageLocked issue → 检查 14 阻塞（绑定在
 *      激活集）；kOptVarUnbindable issue → 检查 14 警告（绑定存在但未
 *      激活——§5.3 Mesh 基线截面同源警告）；
 *   6. 检查 10＋17（目标面）：validateObjectives(目标集, stage, 注册表)——
 *      kOptStageLocked issue → 检查 10 阻塞（阶段能力冲突）；其余 issue →
 *      检查 17 阻塞（目标不可计算——OPT-07）；
 *   7. 检查 4（modeling 工件）：快照 objectClosure 须含 robot-design 与
 *      robot-drivetrain 条目；TCP 变量绑定（mdl.tcp[*] 模式）的 diagSubject
 *      对象在闭包中的类型须为 tool-definition（引用悬空/类型错位 → 阻塞，
 *      §5.3/P-MDL-6）；
 *   8. 检查 5（requirements 工件）：闭包须含 req-set 根＋四集合（point/
 *      region/condition/plan）——缺任一逐条阻塞（requirements §8.2）；
 *   9. 检查 6（必验工况）：caseSet.entries 空或无 enabled∧mandatory 条目 →
 *      阻塞（保守——空必验集合不得输出正式通过，P-EV-7 同源，EVI-02）；
 *  10. 检查 7（评估器）：StageB——opt-static-screen 未注册或契约版本≠1 →
 *      阻塞；StageD——联合评估键 WP-21 登记前不可用 → 阻塞（OPT-EVALUATOR-
 *      MISSING，§5.7 StageD 行）；
 *  11. 检查 8（policy）：快照 policyRef 内容身份非保留值（CON-06）；
 *  12. 检查 9（Profile）："opt" Profile 在注册表可解析且身份与 spec.profile
 *      一致（EVI-01 表 4）；
 *  13. 检查 11（P-04）：stage==StageD 且 !p04Frozen → 阻塞（O-28/OPT-09；
 *      R1 期 p04Frozen 恒 false——StageD 正式联合优化恒阻塞）；
 *  14. 检查 12（目录值域）：StageD 离散绑定的 enumValues 空 → 阻塞
 *      （SEL-08——目录版本未锁定；结构化 CatalogIdentity 承载随 WP-21-T02，
 *      R1 以"离散绑定必须携带非空目录值域"为最小事实核验，诚实登记）；
 *  15. 检查 13（身份一致）：spec 四身份字段与快照对应字段逐一相等
 *      （I-OPT-1——project/branch/revision/snapshotId）；
 *  16. 检查 15（链型）：!baselineChainInEnabledScope → 阻塞（§2.1/MDL-12）；
 *  17. 检查 18（外部资源）：快照 externalResources 存在 Recorded 态 → 阻塞
 *      （CON-03/PM-01——Verified/正式导出前须固化）；
 *  18. 检查 19（预估超预算）：研究变量集全部为枚举采样维度时估算组合数
 *      Π|enumValues|，> maxCandidates → 警告（截断并计数——§4.3；含连续/
 *      量化维度的 LHS 研究无有限组合上限语义，不触发——检查逻辑登记于
 *      实现注，WP-21-T06 策略扩展时按策略预估替换，检查逻辑不变）；
 *  19. 检查 20（版本兼容）：lastRun 在场时逐面比对（评估器集摘要/Profile
 *      身份）——不一致逐面警告（EV-COV-4 不可直接比较提示）；
 *  20. allowances 五元组计算（§6.4 五行定义独立评估）＋计数汇总。
 *
 * 线程约束：实例构造后只读；preflight() 可并发（§12.3——注册表 find/
 * manifest 并发只读安全）。确定性：同 (spec, inputs) ⇒ 同报告。
 */
class OptimizationPreflightService final : public IOptimizationPreflightService {
public:
    /**
     * @brief 构造（检查输入注入＋装配校验）。
     *
     * @param inputs [in] 检查输入（值语义拷贝；注册表指针为非 owning 只读
     *               借用——调用方保证 preflight() 期间存活）
     *
     * @throws std::invalid_argument inputs.snapshot.snapshotId 为保留值或
     *         快照三身份任一无效（装配违约 fail-fast——检查输入不完整无法
     *         产出可信报告；registry/profileRegistry 可空，见 PreflightInputs 注）
     */
    explicit OptimizationPreflightService(PreflightInputs inputs);

    /// @copydoc IOptimizationPreflightService::preflight
    PreflightReport preflight(const OptimizationRunSpec& spec) const override;

private:
    PreflightInputs m_inputs;  ///< 检查输入（构造拷贝后只读——装配快照）
};

}  // namespace sdurws::ird::optimization

#endif  // SDURWS_IRD_OPTIMIZATION_PREFLIGHT_HPP
