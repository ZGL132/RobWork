/**
 * @file   KinPanelFlows.hpp
 * @brief  界面数据流编排（零 Qt）——§9.8 十二条数据流中模型层可测流的
 *         唯一编排点（L-K2/3/4/5/6/7/9/10/11/12；L-K1/L-K8 在 Model/Units）。
 *
 * 设计依据：
 *   - units/kinematics.md §9.8（界面逻辑十二条数据流表——每条流的交互/
 *     数据流/契约锚）；线程与刷新约束（"UI 线程零计算（>1 s 全部转
 *     execution——NFR-PERF-01）；会话级单点 FK/IK 内联门槛 <1 s，超界
 *     自动转后台并给状态反馈；面板不缓存权威结果"）；
 *   - 需求 NFR-PERF-01、KIN-06/AT-04（会话姿态零修订）、KIN-08/AT-04
 *     （导出零修订）、KIN-12/AT-27、PM-07（只读）；任务契约
 *     tasks/foundation/WP-15-T12.json acceptance 1~4。
 *
 * 背景说明（编排≠计算）：本头全部函数是"取缝→调域入口→装结果值"的
 * 编排——数值语义（求解/指标/排序/筛选/提示）全部在域设施内；本头的
 * 唯一"判断"是流控制（缝是否装配、内联预算是否超界、纪元是否过期、
 * 会话是否只读），均为界面可用性控制而非工程判定（ui §7.5 同案：
 * "界面使能态不是业务判定"）。
 *
 * 线程约束：全部函数仅 UI 线程调用（§3.4——缝与会话态同约束）。
 */

#ifndef IRD_KINEMATICS_PLUGIN_KINPANELFLOWS_HPP
#define IRD_KINEMATICS_PLUGIN_KINPANELFLOWS_HPP

#include <chrono>
#include <optional>
#include <string>
#include <vector>

#include <sdurws/ird/kinematics/AnalysisConfig.hpp>  // AnalysisConfiguration/变更提示（L-K7）
#include <sdurws/ird/kinematics/Commands.hpp>        // TcpRef/CommandSubmission（L-K9）
#include <sdurws/ird/kinematics/Ik.hpp>              // IkRequest/IkOutcome（L-K2）
#include <sdurws/ird/kinematics/Recompute.hpp>       // KinRecomputeRequest（KIN-P4 复算流）
#include <sdurws/ird/kinematics/Render.hpp>          // RenderPointWriteback/选中联动（L-K4/L-K1）
#include <sdurws/ird/kinematics/SolutionSet.hpp>     // IKinematicSolutionSet/SolutionRef
#include <sdurws/ird/evidence/Evaluator.hpp>         // evidence::IEvaluationContext（★ KIN-R2 任务3——采样宿主上下文接口）
#include "KinPanelTypes.hpp"                          // 会话态/服务缝/后台值（同目录私有头）

namespace sdurws {
namespace ird {
namespace kinematics {

/// 会话级内联预算（NFR-PERF-01/§9.8：<1 s——1000 ms；超过即转后台）。
inline constexpr std::chrono::milliseconds kInlineBudgetMs{1000};

// =====================================================================
// L-K2 单点求解（面板 Solve→IIkSolver 内联 <1 s 或转后台→解表→检查器）
// =====================================================================

/**
 * @brief 会话默认 TCP 解析结果（KIN-P4/审查 §4——三入口统一的解析值）。
 *
 * ok==false 时 tcp 无语义：statusText 承载稳定诊断码（KIN-NO-TCP）与
 * 用户可读原因——调用方显式呈现并停在该入口，绝不以空 TcpRef 继续走
 * 后续流（位姿页刷新、求解、设默认 TCP 三个入口共用本解析，空身份的
 * 后果随入口各异：FK 拒评/求解器 fail-fast/设默认写悬空引用）。
 */
struct KinTcpResolution {
    /// 解析是否成功（false＝tcp 无语义——statusText 直显）。
    bool ok = false;
    /// 解析产物（ok==true 时非空——TcpRef{默认工具对象身份, 工具局部名}；
    /// 名字经快照对象索引可解析，请求自描述）。
    TcpRef tcp;
    /// 失败原因（ok==false 非空——KIN-NO-TCP＋建模侧配置指引）。
    std::string statusText;
};

/**
 * @brief 会话默认 TCP 的名称解析单点（KIN-P4——审查 §4"缺省 TCP 的解析
 *        必须贯穿刷新、求解、设默认三个入口"的唯一实现）。
 *
 * 解析规则（KIN-14 默认 TCP 权威）：模型快照 tools 非空且
 * defaultTcpIndex 有值且指向有效项 → TcpRef{tool.objectId, tool.localName}
 * （与 assembleSessionSolveRequest 步骤 1 同一语义——本函数抽出后该处
 * 改调本函数，消除第二份解析）。任一前提不成立＝解析失败：
 *   - 模型视图未装配（空缝——能力降级如实呈现）；
 *   - 快照无工具（tools 空）/默认索引缺失（KIN-NO-TCP"未配置"）；
 *   - 默认索引越界（builder 不变量违约——防御面，同轨拒绝）。
 *
 * @param services [in] 服务缝（modelView 读取——空视图＝解析失败）
 * @return 解析结果（ok/tcp/statusText——语义见 KinTcpResolution）
 *
 * 纯编排（零评估调用）；UI 线程；确定性（同模型同输出——NFR-COR-02）。
 */
KinTcpResolution resolveSessionTcp(const KinPanelServices& services);

/**
 * @brief 单点求解流的结果值（编排的产出——解表/检查器的输入）。
 */
struct KinSolveFlowResult {
    /// 是否已转后台（true＝内联超界经后台缝提交——解表等待投影刷新）。
    bool deferredToBackground = false;
    /// 状态行文本（内联耗时/转后台说明/调用方错误如实呈现——非模态）。
    std::string statusText;
    /// 内联耗时（内联路径有值——预算核对的测试断言锚）。
    std::optional<std::chrono::milliseconds> inlineElapsed;
};

/**
 * @brief 会话求解请求的装配结果（KIN-P2/A08——assembleSessionSolveRequest
 *        的产出值）。
 *
 * ok==false 时请求**不得**提交求解器：装配失败意味着请求会 fail-fast
 * （KIN-NO-TCP/配置非法等调用方错误轨）——按钮路径把 statusText 呈现给
 * 用户并停在原地，绝不提交残缺请求（审查 A08 根因之一即"提交缺字段请
 * 期望求解器兜底"）。
 */
struct KinSolveRequestAssembly {
    /// 装配是否成功（false＝request 无语义，statusText 承载失败原因）。
    bool ok = false;
    /// 装配产出的完整请求（ok==true 时通过 IkSolver 请求校验面——
    /// TCP 已解析、初值/referenceQ/intervals 维度齐全、配置字段透传）。
    IkRequest request;
    /// 装配失败原因（ok==false 非空——含稳定诊断码 KIN-NO-TCP/
    /// KIN-CONFIG-ILLEGAL 等，供状态行直显）。
    std::string statusText;
};

/**
 * @brief 从会话/模型装配完整合法的单点求解请求（L-K2 装配半区——
 *        KIN-P2/审查 A08 的修复落点）。
 *
 * ★ 审查 A08 根因：真实按钮曾以默认构造 IkRequest 提交（TCP 身份空/
 * 初值空/referenceQ 维度 0/intervals 维度 0/迭代上限 0）——真实
 * IkSolver 请求校验（fail-fast 轨）必然拒绝，异常穿越 Qt 槽。本函数把
 * 装配义务显式化，逐字段来源：
 *   - TCP 身份：模型快照 defaultTcpIndex（KIN-14 默认 TCP 权威）→
 *     TcpRef{tool.objectId, tool.localName}——名称经快照对象索引可解析
 *     （空 tcpKey 语义上等价 canonical TCP，此处显式填 localName 使请
 *     求自描述）；无工具/默认索引缺失＝KIN-NO-TCP 装配失败（显式报错
 *     不提交——解析失败的请求没有"先发后修"的道理）；
 *   - 初值集：savedConfig（initialStrategy/initialValuesCount/seed）×
 *     evaluationIntervals(view) × referenceQ——经域设施 makeInitialValues
 *     唯一实现点物化（零本地重写）；
 *   - referenceQ：当前会话姿态（KinSessionPose——宿主持有的会话事实）；
 *     会话未设置时以模型零位回退（确定性、如实注释——面板此态下关节
 *     表为空，零位是唯一无信息注入的确定性选择）；会话姿态维度与模型
 *     自由度不符＝装配失败显式拒绝（NFR-COR-03 不钳制）；
 *   - intervals：evaluationIntervals(view)——逐轴模型关节限位（三态：
 *     bounds→workingRange→无分析限位如实 bounded=false）；
 *   - 容差/迭代上限/去重阈值：savedConfig 同名字段直传；
 *   - 碰撞会话/取消探针/对象绑定/结果身份：不装配（缝归 KIN-P4 正式
 *     宿主；单点会话求解零绑定——D-KIN-4 身份不含会话）。
 *
 * savedConfig 前置校验（validateAnalysisConfiguration）：会话纪律要求
 * savedConfig 恒合法（KinPanelTypes.hpp 注），但装配是"合法配置→合法
 * 请求"的边界——配置非法（如默认构造 seed=0 的未初始化会话）时显式
 * 失败并给 KIN-CONFIG-ILLEGAL，绝不让非法配置流入 makeInitialValues
 * （其 seed=0 行为契约即"输出无定义"）。
 *
 * @param services [in] 服务缝（modelView/sessionPose——空视图＝装配失败）
 * @param session  [in] 会话态（savedConfig 读取——零写入）
 * @return 装配结果（ok/statusText/request——语义见 KinSolveRequestAssembly）
 *
 * 纯编排（零求解算术——初值物化经域设施）；UI 线程；确定性（同会话
 * 同模型同输出——NFR-COR-01/02）。
 */
KinSolveRequestAssembly assembleSessionSolveRequest(
    const KinPanelServices& services, const KinModuleSessionState& session);

/**
 * @brief 单点求解流（L-K2 唯一编排点）。
 *
 * 执行序：
 *   1. 缝装配校验（视图/求解器缺失→状态行如实说明，零调用）；
 *   2. 组装 IkRequest（targetInBase/容差/referenceQ/初值/区间/迭代上限
 *      ——配置字段直传；身份不含会话，D-KIN-4）；
 *   3. 内联计时执行（steady_clock——唯一计时点；求解本身零计时介入）；
 *   4. 内联耗时 ≤ kInlineBudgetMs→解集入会话态（唯一写点）＋状态行；
 *      > 预算→若后台缝装配：按 SessionSolve 转后台（自动）＋状态行；
 *      缝未装配→状态行如实说明"超内联预算且后台通道未装配"（不虚构
 *      转后台）。
 *
 * @param services    [in] 服务缝（modelView/ikSolver/backgroundSubmit）
 * @param session     [in,out] 会话态（内联成功时写入 lastSessionSolutionSet）
 * @param targetInBase [in] 求解目标位姿（基座坐标系 {B}——§5.1 唯一来源）
 * @param request     [in] 求解请求其余字段（初值/容差/区间/配置投影——
 *                    产品路径经 assembleSessionSolveRequest 装配（KIN-P2/
 *                    A08）；本函数零再加工）
 * @param inlineBudget [in] 内联预算（缺省 kInlineBudgetMs——产品口径；
 *                    测试注入更小值以确定性触发超界分支，不改产品门槛）
 * @return 编排结果（解表内容经 session.lastSessionSolutionSet 现取——
 *         本结果值不携带解集，防双权威）
 *
 * 纯编排（零求解算术）；UI 线程；确定性（同输入同缝行为同值）。
 */
KinSolveFlowResult runSinglePointSolve(const KinPanelServices& services,
                                       KinModuleSessionState& session,
                                       const rw::math::Transform3D<double>& targetInBase,
                                       const IkRequest& request,
                                       std::chrono::milliseconds inlineBudget = kInlineBudgetMs);

// =====================================================================
// L-K3 / L-K6 批量验证与区域覆盖提交（经 execution 后台——进度/取消）
// =====================================================================

/**
 * @brief 批量任务点验证提交流（L-K3 唯一编排点——写 results 正式评估）。
 *
 * 执行序：只读门控（L-K11——写类命令只读会话拒绝，状态行给诊断语义）→
 * 后台缝装配校验（未装配→如实拒绝）→纪元随请求携带→缝提交→回执状态行。
 * 进度/取消不在本函数（经任务投影现取——L-K12 的数据源纪律）。
 *
 * @param services [in] 服务缝（backgroundSubmit/commandSubmit）
 * @param session  [in] 会话态（snapshotId/configDigest/epoch 读取）
 * @return 受理说明文本（状态行——受理/只读拒绝/通道未装配如实三态）
 *
 * 纯编排；UI 线程。
 */
std::string submitBatchValidation(const KinPanelServices& services,
                                  const KinModuleSessionState& session);

/**
 * @brief 区域覆盖评估提交流（L-K6 唯一编排点——语义同 L-K3，kind 换
 *        RegionCoverage；覆盖运行后覆盖率经 results 投影刷新）。
 *
 * @param services [in] 服务缝
 * @param session  [in] 会话态
 * @return 受理说明文本（同 submitBatchValidation 三态）
 *
 * 纯编排；UI 线程。
 */
std::string submitCoverageEvaluation(const KinPanelServices& services,
                                     const KinModuleSessionState& session);

/**
 * @brief 后台完成结果投递（L-K12 迟到判定唯一点——装配层在任务归档后
 *        调用；模块比对现纪元，过期结果丢弃并给状态反馈）。
 *
 * @param session [in,out] 会话态（epoch 比对；丢弃不改任何会话数据）
 * @param note    [in] 完成通知（acceptedEpoch/kind/interrupted/摘要）
 * @return 状态行文本（受理→摘要原样；中断→"已中断"如实前缀；迟到→
 *         "迟到结果已丢弃"）
 *
 * 纯编排；UI 线程。
 */
std::string noteBackgroundResult(KinModuleSessionState& session,
                                 const KinBackgroundResultNote& note);

// =====================================================================
// L-K4 / L-K5 会话姿态（双击候选回写/复位 Home/TCP 回填——零修订）
// =====================================================================

/**
 * @brief 可视化点/候选解回写会话姿态（L-K4 双击入口——KIN-06：只写 ui
 *        会话态，零修订、零失效、不入缓存身份）。
 *
 * @param sessionPose [in,out] 会话姿态容器（宿主持有——唯一写点）
 * @param writeback   [in] 回写值（Render.hpp writebackOf 产出——域设施）
 * @return 状态行文本（会话缝未装配→如实说明；成功→确认文本）
 *
 * 纯编排；UI 线程；零修订（结构保证——本函数无任何命令提交通道）。
 */
std::string applyPoseWriteback(KinSessionPose* sessionPose,
                               const RenderPointWriteback& writeback);

/**
 * @brief 复位 Home（L-K4 第三入口——kinematics.reset-session-pose 命令的
 *        数据面；零修订——MDL-17 建模侧登记的会话命令语义）。
 *
 * @param sessionPose [in,out] 会话姿态容器（可空——空则返回说明文本）
 * @param homeQ       [in] Home 构型（rad|m 链序——调用方自命名位姿集读取）
 * @return 状态行文本
 *
 * 纯编排；UI 线程。
 */
std::string resetSessionHome(KinSessionPose* sessionPose,
                             const std::vector<double>& homeQ);

/**
 * @brief 复位 Home——具名来源版（KIN-P3/审查 A09 的编排落点：Home 目标
 *        只能来自 KinPanelServices.homePose 具名来源缝，绝不以当前姿态
 *        充当 Home）。
 *
 * 执行序（六态如实，拒绝路径零写回）：
 *   1. 会话姿态缝未装配→"复位不可用"（无写点可落）；
 *   2. homePose 来源缝未装配→"复位不可用：具名 Home 来源未装配"——
 *      调用方（widget）应已禁用按钮，此分支为防御面（直达槽的路径）；
 *   3. 来源在场但模型/会话未定义 Home（nullopt）→"复位不可用：未定义
 *      具名 Home 姿态（保留键 homeConfiguration 缺失）"——显式提示，
 *      不写回；
 *   4. 来源值维度与设备自由度不符→"复位不可用：Home 维度 ≠ 设备自由度"
 *      （装配层解析违约/模型变更未同步的显式反馈——NFR-COR-03 不钳制
 *      不截断，截断写回会伪造复位语义）；
 *   5. 来源值含非有限分量（NaN/±∞——F-903 双轨统一）→"复位不可用：
 *      第 i 分量非有限……未写回"（值面违约与维度违约同走提示轨——
 *      修复前该形态只在写入口 fail-fast、异常直达 Qt 槽，与提示轨双轨
 *      不一致；编排层前置拦截后不再有可预见的调用方违约以异常形态
 *      离开本函数）；
 *   6. 全部通过→resetSessionHome（唯一写点覆写会话姿态——零修订）。
 *
 * @param services [in] 服务缝（sessionPose/homePose/modelView）
 * @return 状态行文本（成功/各拒绝态——语义见执行序）
 *
 * 纯编排；UI 线程；零修订（结构保证——无任何命令提交通道）。
 */
std::string resetSessionHomeFromNamedSource(const KinPanelServices& services);

/**
 * @brief "以当前 TCP 为目标"回填（L-K5——会话态读→显示单位回填；仅呈现
 *        默认值，求解身份不含会话——D-KIN-4）。
 *
 * @param sessionPose   [in] 会话姿态容器（可空/未设置→nullopt——ui 回退
 *                      编辑器缺省值，不得读值）
 * @param session       [in] 会话态（displayUnits 投影读取）
 * @param jointsSi      [in] 当前设备关节向量（rad|m——目标编辑器的 SI 基线）
 * @param dofJointTypes [in] 逐自由度关节类型表（KIN-P3/审查 A05——回填
 *                      文本逐轴按天然量纲投影：移动轴长度、转动轴角度；
 *                      与 jointsSi 同维同链序，失配＝调用方装配错误）
 * @return 回填文本（显示制式下的关节向量文本；未设置/缝空＝nullopt）
 *
 * @throws std::invalid_argument jointsSi 与 dofJointTypes 维度不一致或
 *         分量非有限（同显示投影契约）
 *
 * 纯编排；UI 线程。
 */
std::optional<std::string> tcpBackfillText(
    const KinSessionPose* sessionPose, const KinModuleSessionState& session,
    const std::vector<double>& jointsSi,
    const std::vector<runtime::JointType>& dofJointTypes);

// =====================================================================
// L-K7 求解配置编辑（确认→保存→依赖提示——不自动重算）
// =====================================================================

/**
 * @brief 配置编辑应用结果（L-K7 编排产出——提示行集＋保存说明）。
 */
struct KinConfigEditResult {
    /// 应用是否成功（false＝校验拒绝——reason 为首错定位，配置零变更）。
    bool ok = false;
    /// 拒绝原因（ok==false 非空——validateAnalysisConfiguration 文案）。
    std::string reason;
    /// 依赖提示行集（ok==true 时经域设施产出——L-K7 纯提示）。
    std::vector<KinNamedValueRow> hintRows;
    /// 状态行文本（保存结果/未装配保存缝的如实说明）。
    std::string statusText;
};

/**
 * @brief 求解配置编辑应用流（L-K7 唯一编排点——表单确认后的域侧半区；
 *        ui 表单公共件 ParamEditModel::confirmApply 的移交出口实现面）。
 *
 * 执行序：
 *   1. 逐键写回草稿配置（changes 的键值→AnalysisConfiguration 字段——
 *      键词表封闭，未知键＝调用方错误 fail-fast）；
 *   2. validateAnalysisConfiguration（域设施——非法即整批拒绝零变更，
 *      NFR-COR-03 不钳制）；
 *   3. analyzeConfigurationChange（域设施——依赖提示数据）；
 *   4. 保存缝装配时→configPersist（用户级 PM-14 通道）＋saved 更新；
 *      未装配→编辑仅在会话生效，状态行如实说明"用户级持久化通道未
 *      装配"（P-KIN-4——存储接口待 ui 卡冻结）。
 *   全程零重算触发（L-K7"不自动重算"——结构保证：无任何评估入口）。
 *
 * @param session       [in,out] 会话态（savedConfig 基线与更新面）
 * @param savedBefore   [in] 保存基线（应用前 savedConfig——提示比较基准）
 * @param changes       [in] 确认的修改集（键值对——ParamEditSet 转译）
 * @param persist       [in] 用户级保存缝（可空——见执行序 4）
 * @return 应用结果（提示行/状态行）
 *
 * @throws std::invalid_argument changes 含未知字段键（键表封闭——装配错误）
 *
 * 纯编排；UI 线程。
 */
KinConfigEditResult applyConfigurationEdit(
    KinModuleSessionState& session, const AnalysisConfiguration& savedBefore,
    const std::vector<std::pair<std::string, double>>& changes,
    const KinConfigPersistFn& persist);

// =====================================================================
// L-K9 设默认 TCP/设备（面板发起→门面→①端口→新修订→失效提示）
// =====================================================================

/**
 * @brief 设默认 TCP 提交流（L-K9——kinematics.set-default-tcp 命令的
 *        域半区：门面组装 modeling 命令经①端口，新修订回执→失效提示）。
 *
 * @param services [in] 服务缝（commandHandler——未装配则返回禁用说明）
 * @param session  [in] 会话态（只读门控——写类命令只读会话拒绝）
 * @param tcp      [in] 目标默认 TCP（toolObject＋tcpKey）
 * @return 状态行文本（Committed→"新修订 <id>——依赖默认 TCP 的结果需重算"
 *         提示；NotCommitted→诊断码透传文本；只读/未装配→如实说明）
 *
 * 纯编排；UI 线程。
 */
std::string submitSetDefaultTcp(const KinPanelServices& services,
                                const KinModuleSessionState& session,
                                const TcpRef& tcp);

/**
 * @brief 设默认设备提交流（L-K9——kinematics.set-default-device 命令域
 *        半区；语义同 submitSetDefaultTcp，对象为 robot-design 根）。
 *
 * @param services [in] 服务缝
 * @param session  [in] 会话态
 * @param robotOid [in] 待设为默认的设备根对象身份
 * @return 状态行文本（同上三态）
 *
 * 纯编排；UI 线程。
 */
std::string submitSetDefaultDevice(const KinPanelServices& services,
                                   const KinModuleSessionState& session,
                                   const core::ObjectId& robotOid);

// =====================================================================
// L-K10 结果导出（筛选/排序→JSON/CSV 副本——io 通道，零修订）
// =====================================================================

/**
 * @brief 复算流的结果值（编排的产出——新旧解集对比的状态行素材）。
 */
struct KinRecomputeFlowResult {
    /// 复算是否完成（false＝前置缺失/装配失败——新解集无语义）。
    bool ok = false;
    /// 状态行文本（复算绑定说明＋新旧解数对比/失败原因——非模态）。
    std::string statusText;
    /// 复算请求值（ok==true 时有值——buildKinRecomputeRequest 产出；
    /// "按原 slice 重新提交"的构造面证据，随状态行语义留痕）。
    std::optional<KinRecomputeRequest> recompute;
};

/**
 * @brief 会话结果复算流（KIN-P4/审查 §4——"原任务定义与当前快照重绑定"
 *        的编排落点；结果页"复算"按钮的域半区）。
 *
 * 语义（§7.4 三要件的会话级兑现）：
 *   - **按原 slice 重新提交**——原任务定义中可从结果身份恢复的字段
 *     （seed/referenceQ/mode）经域设施 buildKinRecomputeRequest 从原解集
 *     requestIdentity 显式携带（不复用会话残留——D-KIN-4）；快照绑定
 *     换为当前会话 snapshotId（重绑定——CON-05 内容寻址：工程未变则
 *     同快照，改型后新快照）；
 *   - **目标位姿取当前编辑值**——单点会话求解的目标不在结果身份内
 *     （身份字段封闭：snapshotId/sliceId/configDigest/mode/seed/
 *     referenceQ），目标位姿的现值载体就是面板目标编辑面（用户可见可
 *     改——改型评估的工程语义即"同一目标在新模型下重新求解"）；
 *   - **请求重装配**——TCP 经 resolveSessionTcp（三入口统一解析）、
 *     intervals 取当前快照、容差/迭代/去重/初值策略与数量取当前
 *     savedConfig、seed 取原身份（同键同种子）、referenceQ 取原身份；
 *     结果身份（requestIdentity）整体重填：snapshotId/configDigest＝
 *     当前会话值（重绑定可见面）、seed/referenceQ＝原身份值（同键）。
 *
 * 执行序（拒绝态全部零求解零写会话）：
 *   1. 前置校验（原解集在场/模型视图与求解器在场/当前快照已绑定——
 *      缺一即拒绝，如实原因）；
 *   2. buildKinRecomputeRequest（域设施——原身份提取＋当前快照绑定；
 *      空快照被域设施拒绝——invalid_argument 显式拦截转状态行）；
 *   3. TCP 解析（resolveSessionTcp——失败即拒绝，绝不空 TcpRef 求解）；
 *   4. 内联求解（IIkSolver::solve——复算规模与单点求解同量级，内联
 *      预算语义同 L-K2；取消/设施失败如实呈现不入会话态）；
 *   5. 新解集入会话态（唯一写点——覆盖 lastSessionSolutionSet/View；
 *      原解集的对比素材在状态行：新旧解数）。
 *
 * @param services     [in] 服务缝（modelView/ikSolver——复算执行面）
 * @param session      [in,out] 会话态（原解集读取；成功时写新解集——
 *                     唯一写点与 runSinglePointSolve 同纪律）
 * @param targetInBase [in] 复算目标位姿（基座系 {B}——面板当前编辑值）
 * @return 编排结果（ok/statusText/recompute——语义见结构注）
 *
 * 纯编排（求解经域求解器，零本地算术）；UI 线程；零修订（会话级复算
 * ——AT-04：显式应用才经命令端口）。
 */
KinRecomputeFlowResult recomputeSessionFromIdentity(
    const KinPanelServices& services, KinModuleSessionState& session,
    const rw::math::Transform3D<double>& targetInBase);

/**
 * @brief 结果导出流（L-K10 唯一编排点——会话解集→域设施打包编码→
 *        io 写出缝；AT-04 零修订结构保证：无任何命令提交通道）。
 * @brief 结果导出流（L-K10 唯一编排点——会话解集→域设施打包编码→
 *        io 写出缝；AT-04 零修订结构保证：无任何命令提交通道）。
 *
 * @param services    [in] 服务缝（exportWriter 未装配→返回禁用说明）
 * @param session     [in] 会话态（lastSessionSolutionSet——nullopt＝无
 *                    可导出结果，如实说明）
 * @param targetPath  [in] 目标文件路径（规范文本——写出经 io 层）
 * @param asCsv       [in] true＝CSV 编码；false＝JSON 编码
 * @return 状态行文本（成功/写出失败/通道未装配/无结果——如实四态）
 *
 * 纯编排；UI 线程；零修订。
 */
std::string exportSessionResults(const KinPanelServices& services,
                                 const KinModuleSessionState& session,
                                 const std::string& targetPath, bool asCsv);

// =====================================================================
// 探索式工作空间采样（★ KIN-R2 任务3/KIN-09——结果与可视化面板的采样
// 模式编辑入口的编排半区；域计算核 runWorkspaceSampling 已在 P5 落位，
// 本节把面板参数装配为域查询并承接点云产物入会话态）
// =====================================================================

/**
 * @brief 探索式采样的最小宿主上下文（evidence::IEvaluationContext 实现
 *        ——内联运行面：无外部取消源、无进度设施、无快照物化读取）。
 *
 * 诚实边界（随卡 §14.6 v0.29 登记）：KIN-09 原文"可取消、有进度"的
 * 后台通道化归后续批次——本上下文对应的是**小预算内联运行**语义（面板
 * 暴露的样本量级为演示/诊断面），取消恒 false、进度上报即弃（不伪造
 * 百分比——reportProgress 显式空实现，与执行器侧"进度设施未装配不伪造"
 * 同则）。物化读取恒 nullopt——探索式零物化消费（NullEvaluationContext
 * 同款语义）。
 *
 * 线程约束：仅 UI 线程构造与传引用（与面板流同约束）。
 */
class KinInlineSamplingContext final : public evidence::IEvaluationContext {
public:
    bool cancellationRequested() const override
    {
        return false;  // 内联运行无外部取消源——诚实常值（非伪造取消）
    }
    void reportProgress(std::uint8_t percent, std::string_view phase) override
    {
        (void)percent;
        (void)phase;  // 进度设施未装配——不上报不伪造（EX-T05 通道留后续）
    }
    std::optional<std::vector<std::uint8_t>> tryObjectBytes(
        core::ObjectId objectId, core::ContentVersion contentVersion) const override
    {
        (void)objectId;
        (void)contentVersion;
        return std::nullopt;  // 探索式零物化消费（纯计算面）
    }
};

/**
 * @brief 探索式采样运行结果（编排值——状态行文本＋模式溯源 token）。
 *
 * samplingModeToken＝workspaceSamplingModeToken(mode) 三值词表词
 * （"cartesian-region"/"joint-random"/"joint-grid"——F-905 下游感知
 * 半区：面板呈现与导出行的采样模式来源标注同源，禁第二处字面量）。
 */
struct KinWorkspaceSamplingFlowResult {
    /// 运行是否已入会话态（true＝lastWorkspaceRun 已更新——含 cancelled
    /// partial 产物；false＝拒绝/失败轨，会话态零触碰）。
    bool ok = false;
    /// 状态行文本（成功摘要含模式/完成/可达计数；拒绝轨含原因——ERR-01）。
    std::string statusText;
    /// 采样模式 token（F-905 溯源标注——呈现/导出同源词表值）。
    std::string samplingModeToken;
};

/**
 * @brief 运行一次探索式工作空间采样并入会话态（★ KIN-R2 任务3——面板
 *        "运行采样"的编排落点；KIN-09 全链的 UI 侧承接）。
 *
 * 执行序（拒绝轨零会话写入）：
 *   1. 缝校验（modelView 未装配→如实拒绝）；
 *   2. TCP 解析（resolveSessionTcp 三入口统一单点——区域模式逐样本 IK
 *      与关节模式 FK 都需要合法工具身份；失败＝KIN-NO-TCP 提示）；
 *   3. 域查询装配（mode/区域盒/位置采样定义/关节采样定义直投；姿态
 *      定义退化单方向单 roll——KIN-09 位置点云主径；config＝会话
 *      savedConfig〔seed/预算权威——I-KIN-4 非法配置由域校验面拒绝〕；
 *      referenceQ＝模型链零位〔D-KIN-4 显式输入的确定性投影——与正式
 *      通道执行器同口径〕；tcp＝解析产物）；
 *   4. 域校验（validateWorkspaceSamplingQuery——invalid_argument＝调用
 *      方参数错误，转状态行不让异常穿越 Qt 槽）；
 *   5. 内联运行（runWorkspaceSampling＋KinInlineSamplingContext——
 *      cancelled partial 产物合法入会话态并如实标注）；
 *   6. 产物入会话态（lastWorkspaceRun 唯一写点）＋摘要文本。
 *
 * @param services [in] 服务缝（modelView 必需；ikSolver 可空＝域内置
 *                 求解器——生产路径）
 * @param session  [in,out] 会话态（savedConfig 读取；lastWorkspaceRun
 *                 唯一写点）
 * @param params   [in] 面板采样参数（SI 真值——widget 提交面已换算）
 * @param context  [in] 宿主上下文（KinInlineSamplingContext 缺省形态；
 *                 测试可注入取消探针替身）
 * @return 编排结果（ok/状态行/模式 token——语义见结构注）
 *
 * 纯编排；UI 线程；探索式产物不入 results 归档（R2 边界——域类型级
 * 保证）；求解配置 seed=0 等非法值由域校验面拒绝转状态行。
 */
KinWorkspaceSamplingFlowResult runWorkspaceSamplingFlow(
    const KinPanelServices& services, KinModuleSessionState& session,
    const KinWorkspaceSamplingParams& params,
    evidence::IEvaluationContext& context);

/**
 * @brief 导出点云 CSV（★ KIN-R2 任务3/F-905——结果与可视化面板"导出
 *        点云"的编排落点；契约版本 2 行格式含 sampling-mode 溯源列）。
 *
 * 数据链：lastWorkspaceRun → buildWorkspacePointCloud（域渲染数据唯一
 * 组装点）→ workspacePointCloudExportCsv（v2 编码）→ exportWriter 缝。
 * 未采样/缝未装配/写出失败＝如实四态（与 exportSessionResults 同纪律）。
 *
 * @param services   [in] 服务缝（exportWriter）
 * @param session    [in] 会话态（lastWorkspaceRun——nullopt＝无产物）
 * @param targetPath [in] 目标文件路径
 * @return 状态行文本
 *
 * 纯编排；UI 线程；零修订（值行导出——AT-04 同则）。
 */
std::string exportWorkspacePointCloudCsv(const KinPanelServices& services,
                                         const KinModuleSessionState& session,
                                         const std::string& targetPath);

/**
 * @brief 点云样本会话回写（★ KIN-R2 任务3——可视化点回写的采样面落点；
 *        KIN-06/AT-04 零修订）。
 *
 * 数据链：lastWorkspaceRun → workspacePointWriteback（域唯一产出点——
 * 关节模式样本返回采样构型、区域模式样本返回最佳解 q）→
 * applyPoseWriteback（既有会话回写通道）。样本越界/无解＝域 fail-fast
 * 异常就地转状态行（不穿越 Qt 槽）。
 *
 * @param services    [in] 服务缝（sessionPose）
 * @param session     [in] 会话态（lastWorkspaceRun）
 * @param sampleIndex [in] 采样点全局序（results 对齐键——面板回写输入
 *                    字段）
 * @return 状态行文本
 *
 * 纯编排；UI 线程；零修订。
 */
std::string writebackWorkspacePoint(const KinPanelServices& services,
                                    const KinModuleSessionState& session,
                                    std::uint64_t sampleIndex);

/**
 * @brief 导出投影视图 PNG（★ KIN-R2 任务6——"PNG 导出"最小可用入口的
 *        写出半区；零 Qt——PNG 字节编码在 widget 呈现面完成，本函数只
 *        承接字节经既有 exportWriter 缝落盘）。
 *
 * 勘察口径（随卡 §14.6 v0.30 登记）：三维视图截图 PNG 的权威承载归
 * 框架 RWStudioView3D（REQUIREMENTS 附录 A"等价承接"行——UX-11/AT-25）；
 * 接入 RWStudio 场景的三维散点/包络渲染需扩展 ui 单元 View3DPreview-
 * Contract 值面＋HostView3DPreviewBackend 场景节点管理（数千点元的
 * 生命周期/性能面）——成本超出本批，登记后续批次。本入口为**面板内
 * 2D 正交投影**（域 projectWorkspacePoints——XY/XZ/YZ 舍轴投影）的
 * QPainter 画布 PNG：字节面经 exportWriter（io AtomicFile write 以
 * std::string_view 承载——二进制安全，已核实）落盘，与文本导出同一
 * 通道同一纪律。
 *
 * @param services   [in] 服务缝（exportWriter）
 * @param session    [in] 会话态（lastWorkspaceRun——nullopt＝无产物）
 * @param targetPath [in] 目标文件路径
 * @param pngBytes   [in] PNG 编码字节（widget 呈现面产出——本函数零
 *                    解释零转码直传写出缝）
 * @return 状态行文本（成功/写出失败/通道未装配/无产物——如实四态）
 *
 * 纯编排；UI 线程；零修订。
 */
std::string exportWorkspaceProjectionPng(const KinPanelServices& services,
                                         const KinModuleSessionState& session,
                                         const std::string& targetPath,
                                         const std::string& pngBytes);

}  // namespace kinematics
}  // namespace ird
}  // namespace sdurws

#endif  // IRD_KINEMATICS_PLUGIN_KINPANELFLOWS_HPP
