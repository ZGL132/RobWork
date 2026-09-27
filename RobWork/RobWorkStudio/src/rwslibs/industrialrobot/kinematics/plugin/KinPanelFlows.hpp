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
#include <sdurws/ird/kinematics/Render.hpp>          // RenderPointWriteback/选中联动（L-K4/L-K1）
#include <sdurws/ird/kinematics/SolutionSet.hpp>     // IKinematicSolutionSet/SolutionRef
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
 *                    调用方从配置面板与快照装配；本函数零再加工）
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
 * @brief "以当前 TCP 为目标"回填（L-K5——会话态读→显示单位回填；仅呈现
 *        默认值，求解身份不含会话——D-KIN-4）。
 *
 * @param sessionPose [in] 会话姿态容器（可空/未设置→nullopt——ui 回退
 *                    编辑器缺省值，不得读值）
 * @param jointsSi    [in] 当前设备关节向量（rad|m——目标编辑器的 SI 基线）
 * @return 回填文本（显示制式下的关节向量文本；未设置/缝空＝nullopt）
 *
 * 纯编排；UI 线程。
 */
std::optional<std::string> tcpBackfillText(const KinSessionPose* sessionPose,
                                           const KinModuleSessionState& session,
                                           const std::vector<double>& jointsSi);

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

}  // namespace kinematics
}  // namespace ird
}  // namespace sdurws

#endif  // IRD_KINEMATICS_PLUGIN_KINPANELFLOWS_HPP
