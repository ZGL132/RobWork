/**
 * @file   OptPanelModel.hpp
 * @brief  optimization 插件面板的模型层流（零 Qt 可测半区）——就绪投影
 *         合成、变量表行集、约束页呈现、进度漏斗、运行控制流、候选表
 *         行集、候选对比、阶段锁横幅与文案解析（units/optimization.md
 *         §9.1 面板侧模型面；WP-20-T10 界面链路用例的主被测面）。
 *
 * 设计依据：
 *   - units/optimization.md §9.1（运行控制 R1 形态——取消＋进度为 B 期
 *     承诺〔AT-34〕；phaseToken 八词表；"UI 只显示进度和取消状态"红线；
 *     §15.0——暂停相关能力不作为 B 期承诺，本面零其呈现入口〔V12-06/
 *     AT-35 边界，契约测试词表扫描钉住〕）、§6.5（阶段锁呈现边界——
 *     阻塞横幅＋缺项清单，绝不呈现为候选淘汰）、§6.4（Preflight 五元组
 *     ——横幅逐项定位素材）、§7.1~§7.2（八项指标"全部展示/缺失显示
 *     —"，零哈希/内部标识进用户文本〔UX-02〕——候选行以"候选 N"呈现，
 *     身份规范文本仅作内部数据承载）、§7.4（候选序＝编排产出序——
 *     面板零重排）
 *   - 先例：dynamics/plugin/DynPanelModel.hpp（模型层 L-Dx 具名流纪律
 *     ——WP-17-T09；optimization 对应为 L-Ox 流，测试用例以流编号具名）
 *   - 需求 UX-02（工程用语、零哈希进用户文本）、UX-10（七态统一呈现
 *     素材：未完成附缺项、计算中附进度阶段与取消）、UX-03（正常取消
 *     不属于错误、不产生错误诊断）、TASK-01（协作取消）、AT-34（取消
 *     协作生效与进度显示）
 *   - 任务契约 tasks/foundation/WP-20-T10.json acceptance 1/2/3
 *
 * 流清单（L-Ox——每条流的具名测试见 test/OptPluginPanelTest.cpp）：
 *   - L-O1 就绪投影合成：会话事实→OptReadinessRow（透传，零判定）。
 *   - L-O2 变量表行集：绑定投影＋词表查表→逐绑定呈现行（阶段启用列
 *     经词表匹配——"登记了但阶段不支持"如实呈现，语义不混同）。
 *   - L-O3 约束页呈现：阶段＋清单缝值→阻塞横幅＋执行清单行（StageB
 *     清单行＝§6.2 序透传；StageD→阶段锁横幅、清单行恒空）。
 *   - L-O4 进度漏斗：进度样本→§9.1 八阶段漏斗行（完成/进行/待达三态；
 *     词表外 token＝调用方契约违约 fail-fast）。
 *   - L-O5 运行控制流：启动/取消按钮→门控检查→缝转发→会话呈现位
 *     更新（取消永不产生错误诊断——UX-03）。
 *   - L-O6 候选表行集：运行结果→候选行（编排产出序透传零重排；八项
 *     指标列缺失显示"—"；淘汰原因 token 透传）。
 *   - L-O7 候选对比：两候选行→八指标差值行（方向感知；缺失不伪造）。
 *   - L-O8 阶段锁横幅：检查报告阻塞发现→横幅条目（绝不映射进候选行）。
 *   - L-O9 文案解析流：键→缝解析（空缝兜底键名原文）；解析结果哈希
 *     形态守卫（UX-02 零哈希进用户文本——64 位十六进制串检测）。
 *
 * 线程约束：仅 UI 线程访问（会话态实参非线程安全——OptPanelTypes.hpp）。
 * 错误语义：启动/取消的门控拒绝＝用户可见不受理（accepted=false＋拒绝
 *   token——非异常）；漏斗词表外 token＝调用方契约违约 fail-fast（抛
 *   OptimizationError(kOptInputInvalid)——防呈现面静默容忍宿主漂移）；
 *   其余查询的空态语义见各函数注。
 */

#ifndef IRD_OPTIMIZATION_PLUGIN_OPTPANELMODEL_HPP
#define IRD_OPTIMIZATION_PLUGIN_OPTPANELMODEL_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sdurws/ird/core/Evaluation.hpp>        // core::EngineeringStatus
                                                 //   （投影行同构字段）
#include <sdurws/ird/optimization/Constraint.hpp> // ConstraintSpec（约束页缝值）
#include <sdurws/ird/optimization/Objective.hpp> // MetricId/MetricDefinition
                                                 //   （八项指标词表——对比行
                                                 //   的方向/单位唯一来源）
#include <sdurws/ird/optimization/Run.hpp>       // OptimizationRunResult
                                                 //   （候选表数据源）
#include <sdurws/ird/optimization/Types.hpp>     // OptimizationStage/RunPhase
                                                 //   /CandidateStatus 词表
#include <sdurws/ird/optimization/Variable.hpp>  // VariableBinding（变量表
                                                 //   数据源）
#include "OptPanelTypes.hpp"                     // OptModuleSessionState/
                                                 //   OptPanelServices/
                                                 //   OptProgressSample（同目录
                                                 //   私有头）

namespace sdurws::ird::optimization {

// =====================================================================
// 进度阶段词表（§9.1 phaseToken 八值——漏斗的呈现序＝词表序；本常量表
// 是插件面内该词表的唯一书写点，注释零字面复制）。
// =====================================================================

/// §9.1 进度阶段 token 表（漏斗八段固定顺序——改序/增删即跨版本契约
/// 变更，必须走单元卡增量修订；漏斗函数对表外 token fail-fast）。
extern const std::array<const char*, 8> kOptProgressPhaseTokens;

/// 四页标题键（§3.5 键族 plugin.optimization.panel.<页>.title——值归
/// 宿主文案资源；常量表为插件面内唯一书写点）。
extern const std::array<const char*, 4> kOptPanelPageKeys;

// =====================================================================
// L-O1 就绪投影行（ui.md §6.5 DomainReadinessItem 字段同构自持值＋
// 运行控制扩展位）。
// =====================================================================

/**
 * @brief 域就绪投影行（宿主装配层翻译为 ui 汇聚行的素材——基础字段与
 *        ui::DomainReadinessItem 一一对应；runPhase/cancelRequested/
 *        formalExportAvailable 三位为本域运行控制扩展呈现位）。
 *
 * 语义锚（透传纪律）：全部字段取自会话事实（OptModuleSessionState——
 * 权威分属 Preflight/执行侧/evidence），本模型零判定、零缓存（防第二
 * 真值）；domainKey 恒 "optimization"（域注册键——ui.md §6.5 词表）。
 * formalExportAvailable＝(runPhase==Completed) 的词表直译——取消/失败
 * 结果不呈现正式导出/应用入口（TASK-02 呈现面）。
 * 值语义纯结构；线程安全。
 */
struct OptReadinessRow {
    std::string domainKey;                      ///< 域注册键（恒 "optimization"）
    core::EngineeringStatus verdict =
        core::EngineeringStatus::NotApplicable; ///< 最近正式判定（core 词表
                                                ///<   直用——无判定＝NotApplicable）
    bool inputComplete = false;                 ///< 就绪校验结论（UX-10"未完成
                                                ///<   附缺项列表"素材）
    std::vector<std::string> missingItemKeys;   ///< 缺项文案键清单
    bool hasActiveTask = false;                 ///< 在途任务事实（"计算中"素材）
    RunPhase runPhase = RunPhase::Draft;        ///< 运行编排状态（§4.4 词表消费）
    bool cancelRequested = false;               ///< 协作取消已请求呈现位
    bool formalExportAvailable = false;         ///< 正式导出/应用入口可用位
                                                ///<   （仅 Completed 为 true——
                                                ///<   TASK-02 呈现面直译）
};

/**
 * @brief L-O1 就绪投影合成（会话事实→投影行——纯透传，零判定）。
 *
 * @param session [in] 插件会话态（事实由装配层注入/刷新）
 * @return 投影行（domainKey 恒 "optimization"；formalExportAvailable
 *         由 runPhase==Completed 直译——零额外判定）
 */
OptReadinessRow readinessProjection(const OptModuleSessionState& session);

// =====================================================================
// L-O2 变量表行集（变量表页呈现的结构面——数据重组＋词表查表，零判定）。
// =====================================================================

/**
 * @brief 变量表呈现行（变量表页行素材——绑定投影值的逐字段直拷＋词表
 *        匹配的两个登记事实列）。
 *
 * 语义锚：registered/stageEnabled 两列来自词表匹配（matchDefinition——
 * "token 是否为登记变量"与"该阶段是否启用"两个事实分列呈现，防 AT-09
 * 同型的语义混同："未登记"≠"登记了但阶段不支持"）；锁定/授权列直拷
 * 绑定位（未授权默认锁定的 OPT-02 语义在研究初始化侧已施加，本表只
 * 如实呈现）。值语义纯结构；线程安全。
 */
struct OptVariableRow {
    std::string bindingId = {};      ///< 绑定 token（研究内唯一——稳定键；
                                     ///  不进用户文本〔UX-02——权威字段列
                                     ///  承担人读定位〕，行身份对账用）
    std::string kindToken = {};      ///< 值类别 token（§4.3 词表 toToken）
    std::string unitSymbol = {};     ///< SI 单位符号（"m"/"rad"/"1"——枚举/
                                     ///  离散为空串＝值非物理量，呈现"—"）
    double lowerBound = 0.0;         ///< 下界（含），SI 单位；连续/量化有效
    double upperBound = 0.0;         ///< 上界（含），SI 单位
    double step = 0.0;               ///< 量化步长（与单位同量纲；0＝非量化）
    double defaultValue = 0.0;       ///< 默认值（基线值，SI 单位）
    bool locked = false;             ///< 锁定位（§5.4——直拷呈现）
    bool authorized = false;         ///< 授权位（§5.4——直拷呈现）
    bool registered = true;          ///< 词表登记命中（false＝未登记——词表
                                     ///  匹配未命中，如实呈现不修饰）
    bool stageEnabled = false;       ///< 当前阶段启用（词表条目 enabledIn-
                                     ///  StageB/D 按会话阶段取值；未登记
                                     ///  恒 false）
    std::string authorityFieldPath = {}; ///< 权威字段定位（实例化后缀——
                                         ///  人读定位列〔UX-02〕）
};

/**
 * @brief L-O2 变量表行集重组（阶段＋绑定投影→逐绑定呈现行）。
 *
 * 组装规则：行序＝绑定投影入参序（研究定义侧已按 canonical 确定性
 * 排序——本函数零重排，NFR-COR-02）；每行的类别 token/单位符号/登记
 * 两列经词表匹配取得，其余数值列直拷。空绑定集→空行集（空态呈现，
 * 不伪造行）。
 *
 * @param stage    [in] 研究阶段（阶段启用列的判型依据）
 * @param bindings [in] 绑定投影（缝产物——只读）
 * @return 变量表行集（行数＝绑定数；空集→空）
 */
std::vector<OptVariableRow> variableTableRows(
    OptimizationStage stage,
    const std::vector<VariableBinding>& bindings);

// =====================================================================
// L-O3 约束页呈现（约束清单＋阶段锁横幅——§6.5 呈现边界的执行点）。
// =====================================================================

/**
 * @brief 约束清单呈现行（约束页行素材——执行清单条目的直拷投影）。
 *
 * 值语义纯结构；线程安全。
 */
struct OptConstraintRow {
    int ordinal = 0;                 ///< 执行序号（1 基——§6.2 全序呈现）
    std::string constraintToken = {}; ///< 约束 token（§6.2 词表 toToken）
    std::string evaluationKey = {};  ///< 依据评估键/标识（事实溯源锚——
                                     ///  呈现"依据"列）
};

/**
 * @brief 阻塞横幅条目（§6.5 阻塞横幅＋缺项清单的行素材——运行启动
 *        阻塞的诊断呈现，与候选淘汰列严格分立）。
 *
 * diagToken 取稳定码 token（kOptPreflightBlocked 目录呈现码或
 * kOptStageLocked 阶段锁码——DiagCodes.hpp 词表常量，零拼码）；
 * subject/suggestion 逐字透传发现五元组（§6.4——对象定位＋修复建议
 * 为 Preflight 服务产出的用户面中文文本，本面零改写）。
 * 值语义纯结构；线程安全。
 */
struct OptBannerItem {
    std::string diagToken = {};      ///< 稳定码 token（DiagCodes.hpp 词表）
    std::string titleKey = {};       ///< 横幅标题键（§3.5 键族——值归宿主）
    std::uint8_t checkId = 0;        ///< 检查项编号（§6.4 表 1..20；0＝非
                                     ///  Preflight 来源〔如阶段锁〕）
    std::string subject = {};        ///< 对象/绑定/评估键定位（ERR-01 直传）
    std::string suggestion = {};     ///< 修复建议（Preflight 产出中文直传）
};

/**
 * @brief 约束页呈现聚合（横幅＋清单行的二分组——呈现侧按组渲染）。
 *
 * 值语义纯结构；线程安全。
 */
struct OptConstraintPage {
    /// 数据状态 token："ok"＝清单在位；"not-assembled"＝清单缝未装配
    /// （呈现"约束清单未装配"空态）；"stage-locked"＝阶段清单未登记
    /// （StageD——横幅承载阶段锁，行集恒空）。
    std::string dataState = "not-assembled";
    std::vector<OptBannerItem> banners;      ///< 阻塞横幅（§6.5——启动阻塞，
                                             ///  非候选淘汰）
    std::vector<OptConstraintRow> planRows;  ///< 执行清单行（§6.2 序透传）
};

/**
 * @brief L-O3 约束页呈现合成（阶段＋清单缝值→横幅＋行集）。
 *
 * 组装规则（分支语义固定——确定性）：
 *   - StageD：恒阶段锁横幅（kOptStageLocked——运行启动阻塞，§6.5 呈现
 *     为横幅＋缺项说明，绝不呈现为候选淘汰）＋空行集＋dataState=
 *     "stage-locked"（R1 联合约束清单未登记——不预建占位，NFR-MNT-04）；
 *     本分支**不消费清单缝值**（即使缝返回非空也按未登记呈现——清单
 *     权威在约束编排面，StageD 拒绝语义见 §12.2）。
 *   - StageB＋缝值 nullopt：dataState="not-assembled"＋空行集＋空横幅
 *     （清单未装配——空态呈现，不伪造行）。
 *   - StageB＋缝值在位：dataState="ok"＋行集按入参序透传（序号 1 基
 *     重编——呈现列，不改清单语义序）＋横幅透传调用方追加项（本函数
 *     自身不产横幅——阻塞横幅唯一来源是 L-O8 的检查报告映射）。
 *
 * @param session [in] 插件会话态（stage 字段判型）
 * @param plan    [in] 清单缝值（nullopt＝未装配/未登记）
 * @return 约束页呈现聚合
 */
OptConstraintPage constraintPagePresentation(
    const OptModuleSessionState& session,
    const std::optional<std::vector<ConstraintSpec>>& plan);

// =====================================================================
// L-O4 进度漏斗（§9.1"UI 进度漏斗消费"的模型层落点）。
// =====================================================================

/// 漏斗行三态词表（int 值——呈现侧按值分派样式）。
enum class OptFunnelState : int {
    Pending = 0, ///< 待达（词表序在当前阶段之后）
    Active = 1,  ///< 进行中（当前阶段——"计算中附进度阶段"UX-10 素材）
    Done = 2,    ///< 已完成（词表序在当前阶段之前）
};

/**
 * @brief 漏斗呈现行（运行控制页漏斗行素材）。
 *
 * 值语义纯结构；线程安全。
 */
struct OptFunnelRow {
    std::string phaseToken = {};  ///< 阶段 token（§9.1 词表值）
    std::string titleKey = {};    ///< 阶段标题键（§3.5 键族 plugin.
                                  ///<  optimization.phase.<token>.title）
    OptFunnelState state = OptFunnelState::Pending; ///< 漏斗三态
};

/**
 * @brief L-O4 进度漏斗合成（进度样本→八段漏斗行）。
 *
 * 组装规则：行序＝§9.1 词表序（kOptProgressPhaseTokens——漏斗固定
 * 八段）；样本缺省（nullopt＝无在途任务）→空行集（呈现"无在途任务"
 * 空态，不伪造漏斗）；样本在位→当前阶段 token 在词表中的下标 i：
 * 下标 <i 的段＝Done、下标 i 的段＝Active、下标 >i 的段＝Pending。
 * 批计数与百分比不进漏斗行（呈现区单独直读样本值——漏斗只承载
 * 阶段推进面）。
 *
 * @param sample [in] 进度样本（缝产物；nullopt＝无在途任务）
 * @return 漏斗行集（8 行；样本缺省→空集）
 *
 * @throws OptimizationError(kOptInputInvalid) 样本 phaseToken 不在 §9.1
 *         八词表（调用方契约违约 fail-fast——防呈现面静默容忍宿主漂移）
 */
std::vector<OptFunnelRow> progressFunnelRows(
    const std::optional<OptProgressSample>& sample);

// =====================================================================
// L-O5 运行控制流（启动/取消按钮→门控→缝转发——UX-03 零错误诊断）。
// =====================================================================

/**
 * @brief 运行控制动作结果（用户可见不受理的呈现面语义——非异常轨）。
 *
 * accepted=false 时 rejectionToken 取固定词表值（呈现侧按 token 解析
 * 提示文案键）：拒绝 token 是呈现状态词不是诊断码——启动/取消的失败
 * 不产生任何错误诊断（UX-03"正常用户取消不属于错误、不产生错误诊断"）。
 * 值语义纯结构；线程安全。
 */
struct OptActionOutcome {
    bool accepted = false;        ///< 是否受理（false＝用户可见不受理）
    std::string rejectionToken = {}; ///< 拒因 token（受理时空串；词表见
                                     ///<  各动作函数注）
};

/**
 * @brief L-O5 运行启动流（"检查并计算"按钮——门控检查序固定）。
 *
 * 门控检查序（首错即返——确定性；拒绝 token 词表）：
 *   1. 缝未装配→"not-assembled"（呈现"启动出口未装配"）；
 *   2. 会话只读（!writable）→"read-only"（PM-07 只读项目事实——
 *      插件零二次判定，直译呈现）；
 *   3. 已有在途任务（hasActiveTask）→"active-task"（"计算中"——
 *      重复启动不受理）；
 *   4. 取消协作中（cancelRequested）→"cancel-pending"（取消请求已
 *      发出、等待批边界生效期间不接受新启动）；
 *   5. 检查报告在位且存在阻塞（latestPreflight 的 allowances.
 *      allowStart==false）→"blocked"（§6.5——阻塞横幅＋缺项清单已在
 *      界面呈现，用户修正研究定义后重新检查；横幅素材走 L-O8，本函数
 *      零重复定位）；
 *   6. 经缝转发宿主（runStart()）——false→"rejected-by-host"（宿主
 *      拒绝＝用户可见不受理，非异常）；true→受理。
 *
 * 本函数零写会话态（启动受理后的事实推进由装配层经会话刷新注入）。
 *
 * @param session  [in] 插件会话态（只读——门控事实源）
 * @param services [in] 服务缝聚合（runStart 缝）
 * @return 动作结果（拒绝 token 词表见上——非诊断码）
 */
OptActionOutcome requestRunStart(const OptModuleSessionState& session,
                                 const OptPanelServices& services);

/**
 * @brief L-O5 运行取消流（"取消计算"按钮——协作取消的呈现面入口）。
 *
 * 门控检查序（首错即返）：
 *   1. 缝未装配→"not-assembled"；
 *   2. 无在途任务（!hasActiveTask）→"no-active-task"；
 *   3. 已请求取消（cancelRequested）→"already-requested"（防重复
 *      提交——协作取消批边界生效前按钮呈灰）；
 *   4. 经缝转发宿主（runCancel()）——false→"rejected-by-host"；
 *      true→受理并置会话 cancelRequested=true（呈现位——权威完成以
 *      runPhase 推进到终态取消值为准，装配层刷新注入）。
 *
 * 取消永不产生错误诊断（UX-03）：本函数不写任何横幅/错误素材；
 * 取消后的呈现＝运行状态词直译（"已取消（协作取消）"——非错误态）。
 *
 * @param session  [in,out] 插件会话态（受理时置 cancelRequested——
 *                 其余字段不触）
 * @param services [in] 服务缝聚合（runCancel 缝）
 * @return 动作结果（拒绝 token 词表见上——非诊断码）
 */
OptActionOutcome requestRunCancel(OptModuleSessionState& session,
                                  const OptPanelServices& services);

// =====================================================================
// L-O6 候选表行集（候选表页数据面——编排产出序透传，零重排）。
// =====================================================================

/// 八项指标列数（MetricId 词表全量——候选表八列固定呈现，OPT-07）。
inline constexpr std::size_t kOptMetricColumnCount = 8;

/**
 * @brief 候选表呈现行（候选表页行素材——运行结果记录的呈现投影）。
 *
 * 语义锚：
 *   - displayLabel＝"候选 N"（N＝编排序号 1 基）——UX-02 工程用语，
 *     身份规范文本（64 位十六进制形态）**不进用户文本**，仅承载于
 *     candidateIdCanonical 字段供日志/导出通道（呈现控件只显示
 *     displayLabel——哈希泄漏守卫由 L-O9 与用例钉住）；
 *   - 指标八列按 MetricId 枚举序对位（词表序＝列序——确定性），
 *     nullopt＝"—"（缺失不按 0 合成——NFR-COR-03/OPT-07）；
 *   - 淘汰原因 token 透传（记录面评估记录＋编排层追加两源合并——
 *     阶段锁码**不在**其取值域：阶段锁是启动阻塞非候选淘汰，§6.5）；
 *   - 行序＝运行结果候选序（Quick 批在前、Verified 批在后，编排产出
 *     序——本函数零重排，T05/T06 稳定序是唯一权威，§7.4）。
 * 值语义纯结构；线程安全。
 */
struct OptCandidateRow {
    std::string displayLabel = {};      ///< 呈现标签（"候选 N"——UX-02）
    std::string candidateIdCanonical = {}; ///< 候选身份规范文本（内部身份
                                           ///  承载——零进用户文本）
    bool isBaseline = false;            ///< 基线候选标记（§8.2 批首基线）
    std::string statusToken = {};       ///< 候选状态 token（§4.3 词表 toToken）
    bool screeningOnly = false;         ///< screening-only 标记（Quick 记录
                                        ///  恒 true——不支撑正式结论）
    bool formalPassEligible = false;    ///< 正式通过资格位（evidence 五条件）
    core::EngineeringStatus engineeringStatus =
        core::EngineeringStatus::NotApplicable; ///< 工程判定（透传）
    std::array<std::optional<double>, kOptMetricColumnCount> metricValues = {};
                                        ///< 八项指标值（枚举序对位；SI 单位
                                        ///<  随指标——m/kg/s/J/无量纲；
                                        ///<  nullopt＝"—"）
    std::vector<std::string> rejectionReasonTokens; ///< 淘汰原因 token（记录
                                        ///  面＋编排追加两源合并透传）
    bool cacheHit = false;              ///< 命中回放标记（审计透传——命中
                                        ///  ≠Current，两套语义不混同）
};

/**
 * @brief L-O6 候选表行集重组（运行结果→候选行，编排产出序透传）。
 *
 * @param result [in] 运行结果聚合（缝产物——只读；候选序＝编排产出序）
 * @return 候选行集（行数＝结果候选数；空结果→空行集）
 */
std::vector<OptCandidateRow> candidateTableRows(const OptimizationRunResult& result);

// =====================================================================
// L-O7 候选对比（候选表页对比区数据面——方向感知的差值呈现）。
// =====================================================================

/**
 * @brief 候选对比行（对比区行素材——八指标逐行 A/B 值与差值）。
 *
 * 语义锚：
 *   - 方向/单位取自指标定义词表（metricDefinitions——唯一权威，本面
 *     零复制零第二词表）；
 *   - delta＝B−A（两侧齐备时的呈现差值——呈现减法非域计算：不进任何
 *     判定/身份，仅显示屏读数）；
 *   - differs＝两侧齐备且值不等（浮点直接不等比较——呈现高亮语义，
 *     **零容差发明**：容差支配判定唯一在计算库 §7.4，DOPT-5 纪律）；
 *   - bBetter＝方向感知的优势位（两侧齐备时：B 严格优于 A 按词表方向；
 *     缺任一侧→nullopt——缺失不伪造结论，UX-03"不适用显式标记"）。
 * 值语义纯结构；线程安全。
 */
struct OptComparisonRow {
    std::string metricToken = {};     ///< 指标 token（§7.1 词表 toToken）
    std::string unitToken = {};       ///< 单位符号（词表值直拷；空串＝口径
                                      ///  未冻结，呈现"—"）
    bool minimize = true;             ///< 优化方向（true=min／false=max——
                                      ///  词表 MetricDirection 直译）
    std::optional<double> valueA = {}; ///< A 侧值（SI 单位；nullopt＝"—"）
    std::optional<double> valueB = {}; ///< B 侧值（SI 单位；nullopt＝"—"）
    std::optional<double> delta = {}; ///< 差值 B−A（两侧齐备才有值——呈现
                                      ///  减法，零判定语义）
    bool differs = false;             ///< 差异高亮位（两侧齐备且不等——
                                      ///  零容差发明）
    std::optional<bool> bBetter = {}; ///< B 严格优于 A（方向感知；nullopt
                                      ///  ＝不可判——缺任一侧）
};

/**
 * @brief L-O7 候选对比合成（运行结果内两候选→八指标对比行）。
 *
 * 组装规则：行序＝MetricId 枚举序（词表序＝§7.1 表行序）；A/B 两侧值
 * 取两候选行记录的指标槽位（槽位缺失〔nullopt〕→该行两侧如实缺）。
 * 索引为候选表行序（0 基——呈现控件选中行直接对位）。
 *
 * @param result [in] 运行结果聚合（缝产物——只读）
 * @param indexA [in] A 侧候选在候选序中的下标（0 基；越界＝调用方错误）
 * @param indexB [in] B 侧候选下标（同上）
 * @return 对比行集（恒 8 行——八项全量展示，OPT-07）
 *
 * @throws std::invalid_argument indexA/indexB 越界或两下标相等（调用方
 *         契约违约 fail-fast——对比自身无意义）
 */
std::vector<OptComparisonRow> candidateComparison(const OptimizationRunResult& result,
                                                  std::size_t indexA,
                                                  std::size_t indexB);

// =====================================================================
// L-O8 阶段锁横幅（§6.5 呈现边界——阻塞诊断绝不呈现为候选淘汰）。
// =====================================================================

/**
 * @brief L-O8 阻塞横幅条目合成（会话检查报告→横幅条目集）。
 *
 * 组装规则：latestPreflight 缺省→空条目集（呈现"尚未检查"提示——
 * 不伪造阻塞项）；在位→逐条 Blocking 级发现映射为横幅条目（checkId/
 * subject/suggestion 逐字透传；diagToken 恒取 kOptPreflightBlocked
 * 目录呈现码——§6.4"诊断码目录呈现由调用方按 blockerCount 汇总产出"，
 * 本插件即该调用方；条目序＝发现序透传〔报告已按 checkId 升序——
 * T08 排序契约〕）。Warning 级发现**不进**横幅（不阻断——呈现于
 * 缺项清单区，走 missingItemKeys 通道）。阶段锁码条目（若检查报告
 * 携带阶段锁语义发现）同轨透传——横幅是启动阻塞的唯一呈现位，
 * **绝不**映射进候选淘汰列（§6.5；用例钉住候选行淘汰原因取值域）。
 *
 * @param session [in] 插件会话态（latestPreflight 事实源）
 * @return 横幅条目集（发现序透传；无阻塞→空集）
 */
std::vector<OptBannerItem> stageLockBannerItems(const OptModuleSessionState& session);

// =====================================================================
// L-O9 文案解析流（UX-02——键→工程用语，零哈希泄漏守卫）。
// =====================================================================

/**
 * @brief L-O9 文案解析（titleKey→用户文本；空缝兜底键名原文）。
 *
 * 解析序：缝存在→经缝解析（宿主文案资源唯一出口）；缝空→返回键名
 * 原文（dynamics 同纪律——开发 harness 的可见缺口，产品装配必接宿主
 * 解析器）。哈希形态守卫（UX-02"零哈希进用户文本"）：缝解析结果若呈
 * 64 位十六进制串形态（内容摘要泄漏——ARC-04 身份不进呈现），按泄漏
 * 处置返回键名原文并回传标记——呈现宁可键名也不可哈希。
 *
 * @param services    [in] 服务缝聚合（textResolver 缝）
 * @param titleKey    [in] 文案键（§3.5 键族——值归宿主文案资源）
 * @param outFellBack [out] 可空回传：true＝兜底（缝空或解析结果哈希
 *                    形态）；false＝缝解析原值
 * @return 用户文本（工程用语——或键名兜底）
 */
std::string resolvePanelText(const OptPanelServices& services,
                             const std::string& titleKey,
                             bool* outFellBack = nullptr);

/// 候选状态词键族前缀（§3.5——plugin.optimization.status.<状态 token>；
/// 值归宿主文案资源。本常量为插件面内该键族的**唯一书写点**——widget
/// 与测试均经 candidateStatusText 消费，零字面复制）。
inline constexpr const char* kOptCandidateStatusKeyPrefix =
    "plugin.optimization.status.";

/**
 * @brief L-O9 候选状态词解析（statusToken→用户文本；空缝兜底键名原文）。
 *
 * 键拼装＝键族前缀＋状态 token（OptCandidateRow.statusToken——§4.3 词表
 * toToken 值），解析全程走 resolvePanelText（L-O9 同款守卫：空缝兜底键名
 * ＋哈希形态回退键名）。
 *
 * 为什么收口到模型层（F-635，缺陷草案号）：候选表状态列曾是面板文本中
 * 唯一直调可空 textResolver std::function 的入口——空缝装配态（开发
 * harness 未注入宿主解析器）渲染候选表即 bad_function_call 崩溃 UI 线程，
 * 而面板其余文本全部经 resolvePanelText 空缝守卫。收口后状态列与其余
 * 面板文本同走唯一解析流：空缝不崩、显示键名原文（对齐 L-O9 兜底语义），
 * 哈希泄漏守卫随之生效。
 *
 * @param services    [in] 服务缝聚合（textResolver 缝——可空装配态合法）
 * @param statusToken [in] 候选状态 token（§4.3 词表值，如 "feasible"）
 * @return 用户文本（宿主工程用语——或 "plugin.optimization.status.<token>"
 *         键名兜底）
 *
 * 纯函数；线程安全（只读消费缝聚合）。
 */
std::string candidateStatusText(const OptPanelServices& services,
                                const std::string& statusToken);

}  // namespace sdurws::ird::optimization

#endif  // IRD_OPTIMIZATION_PLUGIN_OPTPANELMODEL_HPP
