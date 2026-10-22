/**
 * @file   KinPanelFlows.cpp
 * @brief  界面数据流编排的实现（KinPanelFlows.hpp 全部落点）。
 *
 * 设计依据：KinPanelFlows.hpp 文件头（本 TU 是其全部函数的实现落点）；
 * 导出打包/编码与配置校验/变更提示均为域设施直调（NFR-MNT-04 唯一实现
 * 点纪律——Export.hpp/AnalysisConfig.hpp），本 TU 零二次实现。
 */

#include "KinPanelFlows.hpp"

#include <cmath>
#include <stdexcept>
#include <utility>

#include <sdurws/ird/kinematics/AnalysisConfig.hpp>  // validateAnalysisConfiguration（装配前置校验——域设施）
#include <sdurws/ird/kinematics/DiagCodes.hpp>       // kKinNoTcp/kKinConfigIllegal（稳定诊断码——禁字符串拼码）
#include <sdurws/ird/kinematics/Export.hpp>  // 导出打包/编码（KIN-08 值行——域设施）
#include <sdurws/ird/kinematics/Workspace.hpp>  // 探索式采样计算核/点云渲染/回写（★ KIN-R2 任务3——域设施直调）
#include "KinPanelCommandCatalog.hpp"        // kinCommandEnabledInSession（L-K11 id 门控）
#include "KinPanelModel.hpp"                 // configChangeHintRows（L-K7 提示行化）
#include "KinPanelRotation.hpp"              // rpyFromRotationSi（G4 回填姿态提取——审查 WP-15-T20 第三批）
#include "KinPanelUnits.hpp"                 // formatKinQuantityText（L-K5 回填投影）

namespace sdurws {
namespace ird {
namespace kinematics {
namespace {

/// L-K7 修改集的封闭键表（AnalysisConfiguration 字段键——键表封闭的
/// 唯一书写点；未知键＝装配错误 fail-fast）。
constexpr const char* kKeyInitialStrategy = "initial-strategy";
constexpr const char* kKeyInitialValuesCount = "initial-values-count";
constexpr const char* kKeyIterationLimit = "iteration-limit";
constexpr const char* kKeyPositionTol = "position-residual-tolerance";
constexpr const char* kKeyOrientationTol = "orientation-residual-tolerance";
constexpr const char* kKeyDedupThreshold = "ik-dedup-threshold-per-axis";
constexpr const char* kKeyRegionSeed = "region-budget-seed";
constexpr const char* kKeyRegionThreads = "region-threads";
constexpr const char* kKeySeed = "seed";

/// u64 安全上限（2^53——double 精确整数范围；seed 键值经 double 载入的
/// 防损边界，超出即调用方装配错误）。
constexpr double kMaxUint53 = 9007199254740992.0;

}  // namespace

KinTcpResolution resolveSessionTcp(const KinPanelServices& services)
{
    KinTcpResolution out;

    // ---- 前提 0：模型视图缝（解析的输入真值面——空视图无从解析）。
    if (services.modelView == nullptr) {
        out.statusText = "TCP 未解析：模型视图未装配";
        return out;
    }
    const runtime::CanonicalModel& model = services.modelView->model();

    // ---- 前提 1：KIN-14 默认 TCP 权威（defaultTcpIndex——tools 非空时
    // 必有值且指向 tools 内项）。三前提任一不成立＝解析失败（稳定诊断码
    // KIN-NO-TCP＋建模侧配置指引——显式呈现，绝不以空 TcpRef 继续走流）。
    // 尾分支是防御面：builder 不变量已保证 defaultTcpIndex 指向有效项，
    // 越界＝装配层拿到未过 builder 的模型（内部违约如实拒绝）。
    const std::vector<runtime::CanonicalTool>& tools = model.tools();
    const std::optional<std::uint32_t>& defaultIdx = model.defaultTcpIndex();
    if (tools.empty() || !defaultIdx.has_value()
        || *defaultIdx >= tools.size()) {
        out.statusText = std::string("TCP 未解析：快照无默认工具（")
                         + std::string(kKinNoTcp)
                         + "；请先在建模侧配置工具与默认 TCP";
        return out;
    }
    // ---- A-15：默认工具身份仍按defaultTcpIndex，空键唯一明确表达默认偏移。
    // 实际命名键优先于旧工具名别名；若仍填localName，恰有同名非默认TCP
    // 时会无意切换偏移。这里不能以名称猜默认，也不改外部显式键的解析规则。
    const runtime::CanonicalTool& tool = tools.at(*defaultIdx);
    out.tcp.toolObject = tool.objectId;
    out.tcp.tcpKey.clear();
    out.ok = true;
    return out;
}

KinSolveRequestAssembly assembleSessionSolveRequest(
    const KinPanelServices& services, const KinModuleSessionState& session)
{
    KinSolveRequestAssembly out;

    // ---- 步骤 1：TCP 身份解析（A08 字段①——"合法 TCP 身份"义务）。
    // ★ KIN-P4：解析收拢到 resolveSessionTcp 单点（审查 §4——缺省 TCP
    // 的解析必须贯穿刷新、求解、设默认三个入口；本函数与位姿页刷新/
    // 设默认按钮共用同一实现，消除第二份解析逻辑）。
    const KinTcpResolution tcp = resolveSessionTcp(services);
    if (!tcp.ok) {
        out.statusText = "求解不可用：" + tcp.statusText;
        return out;
    }
    out.request.tcp = tcp.tcp;

    // ---- 步骤 2：求解配置前置校验（A08 字段④⑤的来源合法性——
    // savedConfig 会话纪律要求恒合法，但装配是边界：未初始化会话的
    // 默认构造配置 seed=0 非法（I-KIN-4），流入 makeInitialValues 属
    // "输出无定义"契约面。此处显式校验并以 KIN-CONFIG-ILLEGAL 拒绝，
    // NFR-COR-03 不钳制不静默）。
    try {
        validateAnalysisConfiguration(session.savedConfig);
    } catch (const std::invalid_argument& e) {
        out.statusText = std::string("求解不可用：求解配置非法（")
                         + std::string(kKinConfigIllegal) + "）：" + e.what();
        return out;
    }
    const AnalysisConfiguration& config = session.savedConfig;

    // ---- 步骤 3：逐轴评价区间（A08 字段④——模型关节限位，无界轴
    // 如实表达）。经域设施 evaluationIntervals 唯一实现点提取（三态：
    // bounds→workingRange→bounded=false）——与求解器限位过滤同源，
    // 零第二套真值。
    const std::vector<JointInterval> intervals = evaluationIntervals(*services.modelView);

    // 设备自由度（Fixed 不消耗 q——与求解器/FK 同一口径的链序计数）。
    std::size_t dof = 0;
    for (const runtime::CanonicalJoint& j :
         services.modelView->model().chain().joints) {
        if (j.type != runtime::JointType::Fixed) {
            ++dof;
        }
    }

    // ---- 步骤 4：参考构型（A08 字段③——当前会话姿态）。
    // 会话已设置且维度符→直接采用；维度违约→显式失败（会话姿态由装配
    // 层注入，尺寸错是装配事实不是可钳制的输入——NFR-COR-03）；未装配
    // /未设置→模型零位回退（面板此态下关节表为空，零位是唯一无信息
    // 注入的确定性回退——求解身份不含会话（D-KIN-4），回退值同为显式
    // 输入面，不构成对会话的隐式读取）。
    if (services.sessionPose != nullptr && services.sessionPose->isSet()) {
        const std::vector<double>& poseQ = services.sessionPose->jointConfiguration();
        if (poseQ.size() != dof) {
            out.statusText = "求解不可用：会话关节姿态维度 "
                             + std::to_string(poseQ.size()) + " != 设备自由度 "
                             + std::to_string(dof) + "（装配层注入违约——拒绝）";
            return out;
        }
        out.request.referenceQ = poseQ;
    } else {
        out.request.referenceQ.assign(dof, 0.0);
    }

    // ---- 步骤 5：初值集物化（A08 字段②——初值与数量来自求解配置）。
    // 经域设施 makeInitialValues 唯一实现点（策略三值×配置 seed——§3.4
    // 随机性唯一来源），零本地重写。
    out.request.initialValues = makeInitialValues(
        config.initialStrategy, config.initialValuesCount, config.seed,
        intervals, out.request.referenceQ);

    // ---- 步骤 6：配置字段直传（容差/迭代上限/去重阈值——求解参数面
    // 与批量通道同源配置，KIN-13）。
    out.request.positionTolerance = config.positionResidualTolerance;      // m
    out.request.orientationTolerance = config.orientationResidualTolerance; // rad（不是度）
    out.request.iterationLimit = config.iterationLimit;
    out.request.dedupThresholdPerAxis = config.ikDedupThresholdPerAxis;    // rad|m 逐轴

    // ---- 步骤 7：intervals 交付（步骤 3 提取、维度已按 dof 校齐——
    // 请求校验面要求与 modelView 同快照，此处即同一次提取）。
    out.request.intervals = intervals;

    // ---- 步骤 8：结果身份装配（★ KIN-P4——§5.6 绑定五元组载体补全：
    // 修复前内联求解的解集身份为缺省值（snapshotId/configDigest 全零、
    // seed=0、referenceQ 空）——导出文件头的快照锚随之全零、复算的
    // "原任务定义携带"无从取数。身份字段语义（D-KIN-4）：全部来自显式
    // 输入——snapshotId＝会话绑定锚（装配层注入）、configDigest＝当前
    // 配置摘要（域设施）、seed/referenceQ＝本请求的显式值；mode 保持
    // 域缺省（会话级求解零归档，模式语义随导出头如实承载）。
    out.request.requestIdentity.snapshotId = session.snapshotId;
    out.request.requestIdentity.configDigest =
        analysisConfigurationDigest(config);
    out.request.requestIdentity.seed = config.seed;
    out.request.requestIdentity.referenceQ = out.request.referenceQ;

    // 碰撞会话/取消探针/对象绑定：不装配——碰撞缝归正式宿主
    // （KIN-P4；缺会话＝求解器标记 collisionNotEvaluated 证据缺失，如实
    // 语义）；单点会话求解零对象绑定（D-KIN-4：身份只含显式输入）。
    out.ok = true;
    return out;
}

KinSolveFlowResult runSinglePointSolve(const KinPanelServices& services,
                                       KinModuleSessionState& session,
                                       const rw::math::Transform3D<double>& targetInBase,
                                       const IkRequest& request,
                                       std::chrono::milliseconds inlineBudget)
{
    KinSolveFlowResult result;

    // ---- 缝装配校验（未装配＝能力降级——如实说明，零调用不虚构）。
    if (services.modelView == nullptr || services.ikSolver == nullptr) {
        result.statusText = "求解不可用：模型视图或求解器未装配";
        return result;
    }

    // ---- 组装请求副本：绑定模型视图与目标位姿（其余字段由调用方装配
    //      直传——本函数零再加工，D-KIN-4：身份不含会话）。
    IkRequest req = request;
    req.targetInBase = targetInBase;
    req.modelView = services.modelView;

    // ---- 内联计时执行（steady_clock 唯一计时点；求解内部零计时介入，
    //      取消探针由调用方在 req.cancellationProbe 提供）。
    const auto t0 = std::chrono::steady_clock::now();
    const IkOutcome outcome = services.ikSolver->solve(req);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0);

    // ---- 取消/求解设施失败：非结局（§5.4/F-746③ 值面）——finish 内
    //      早退（预算判定对非结局不适用，与原早退位次等价）。
    if (outcome.cancelled || outcome.facilityFailure.has_value()) {
        return finishSinglePointSolve(session, std::move(outcome), elapsed);
    }
    result.inlineElapsed = elapsed;  // 超时/转后台分支也携带耗时（断言锚）

    // ---- 内联预算判定（NFR-PERF-01/§9.8：>1 s 转后台；预算可注入——
    //      测试确定性触发，产品缺省恒 kInlineBudgetMs）。
    if (elapsed > inlineBudget) {
        if (services.backgroundSubmit) {
            // 自动转后台（会话级——不写 results；真实 execution 通道在
            // 装配层适配）。纪元随请求携带（迟到判定锚）。★ KIN-P4：
            // solveTargetInBase 随载荷携带（表尾追加字段）——后台执行器
            // 据此还原"求解什么"（修复前载荷不含目标，SessionSolve 通道
            // 只能整体拒绝——审查 §4 第二条的根因半区）。
            KinBackgroundRequest bg;
            bg.kind = KinBackgroundKind::SessionSolve;
            bg.snapshotId = session.snapshotId;
            bg.configDigest = analysisConfigurationDigest(session.savedConfig);
            bg.epoch = session.epoch;
            bg.solveTargetInBase = targetInBase;
            const KinBackgroundAck ack = services.backgroundSubmit(bg);
            result.deferredToBackground = ack.accepted;
            result.statusText = ack.accepted
                ? ("内联耗时 " + std::to_string(elapsed.count())
                   + " ms 超预算——已转后台执行（" + ack.reason + "）")
                : ("内联耗时 " + std::to_string(elapsed.count())
                   + " ms 超预算，后台提交被拒：" + ack.reason);
            return result;
        }
        // 缝未装配——不虚构转后台，如实说明（诚实边界）；解集按超时保留
        // 语义入会话态（finish 唯一写点），文案覆写为超时保留说明。
        KinSolveFlowResult finished =
            finishSinglePointSolve(session, std::move(outcome), elapsed);
        finished.statusText = "内联耗时 " + std::to_string(elapsed.count())
                              + " ms 超预算，且后台通道未装配——保留内联结果供参考";
        return finished;
    }
    return finishSinglePointSolve(session, std::move(outcome), elapsed);
}

KinSolveFlowResult finishSinglePointSolve(KinModuleSessionState& session,
                                          IkOutcome outcome,
                                          std::chrono::milliseconds elapsed)
{
    KinSolveFlowResult result;
    result.inlineElapsed = elapsed;

    // ---- 取消：非结局（§5.4）——如实呈现，不入会话态。
    if (outcome.cancelled) {
        result.statusText = "求解已取消（非结局）";
        return result;
    }

    // ---- 求解设施失败：非结局（F-746③ 值面——与取消轨同读法）——如实
    // 呈现，不入会话态（无终局结局可解读；outcomeKind/solutionSet 均无
    // 语义，不得当"解 0 个"呈现——无伪完成）。
    if (outcome.facilityFailure.has_value()) {
        result.statusText = "求解设施失败（非结局——求解中途 FK 复算失败，"
                            "详见评估诊断；请核对模型视图后重试）";
        return result;
    }

    // ---- 内联完成文案＋解集入会话态（唯一写点——会话级解集会话对象本身，
    // 非投影副本；原值面供导出/结果绑定，视图面构造时完成一次稳定排序供
    // 解表/检查器——双面同源零二次求解）。
    result.statusText = "内联完成（" + std::to_string(elapsed.count())
                        + " ms），解 " + std::to_string(outcome.solutionSet.solutions.size())
                        + " 个";
    session.lastSessionSolutionSet =
        std::make_shared<const IkSolutionSet>(std::move(outcome.solutionSet));
    session.lastSessionSolutionSetView =
        std::make_shared<const KinematicSolutionSet>(*session.lastSessionSolutionSet);
    result.resultCommitted = true;
    return result;
}

std::string submitBatchValidation(const KinPanelServices& services,
                                  const KinModuleSessionState& session)
{
    // ---- L-K11 只读门控（写类命令——只读会话拒绝＋诊断语义状态行）。
    if (!kinCommandEnabledInSession("kinematics.validate-task-points",
                                    session.writable)) {
        return "批量验证已拒绝：只读会话不可提交正式评估（写 results）";
    }
    // ---- 后台缝装配校验（未装配＝如实拒绝——不虚构已提交，ERR-01）。
    if (!services.backgroundSubmit) {
        return "批量验证不可用：执行通道未装配";
    }
    KinBackgroundRequest bg;
    bg.kind = KinBackgroundKind::TaskPointsBatch;
    bg.snapshotId = session.snapshotId;
    bg.configDigest = analysisConfigurationDigest(session.savedConfig);
    bg.epoch = session.epoch;
    const KinBackgroundAck ack = services.backgroundSubmit(bg);
    return ack.accepted ? ("批量验证已受理（任务 " + ack.taskRef + "）——进度见任务区")
                        : ("批量验证提交被拒：" + ack.reason);
}

std::string submitCoverageEvaluation(const KinPanelServices& services,
                                     const KinModuleSessionState& session)
{
    // 语义同批量验证（L-K11 门控/缝校验/纪元携带）——kind 换覆盖评估。
    if (!kinCommandEnabledInSession("kinematics.evaluate-coverage",
                                    session.writable)) {
        return "覆盖评估已拒绝：只读会话不可提交正式评估（写 results）";
    }
    if (!services.backgroundSubmit) {
        return "覆盖评估不可用：执行通道未装配";
    }
    KinBackgroundRequest bg;
    bg.kind = KinBackgroundKind::RegionCoverage;
    bg.snapshotId = session.snapshotId;
    bg.configDigest = analysisConfigurationDigest(session.savedConfig);
    bg.epoch = session.epoch;
    const KinBackgroundAck ack = services.backgroundSubmit(bg);
    return ack.accepted ? ("覆盖评估已受理（任务 " + ack.taskRef + "）——覆盖率经结果投影刷新")
                        : ("覆盖评估提交被拒：" + ack.reason);
}

std::string noteBackgroundResult(KinModuleSessionState& session,
                                 const KinBackgroundResultNote& note)
{
    // ---- 迟到判定（L-K12：迟到结果不进当前会话——纪元不符即丢弃）。
    if (note.acceptedEpoch != session.epoch) {
        return "迟到结果已丢弃（提交纪元 " + std::to_string(note.acceptedEpoch)
               + " ≠ 当前纪元 " + std::to_string(session.epoch) + "）";
    }
    // 中断如实呈现（NFR-REL-03——"已中断"不美化）；正常态原样摘要。
    if (note.interrupted) {
        return "已中断：" + note.summaryText;
    }
    return note.summaryText;
}

std::string applyPoseWriteback(KinSessionPose* sessionPose,
                               const RenderPointWriteback& writeback)
{
    // 会话缝未装配＝能力降级（如实说明——不虚构写入）。
    if (sessionPose == nullptr) {
        return "会话姿态未装配——回写不可用";
    }
    // 经域设施唯一写点（Render.hpp writebackToSessionPose——非有限值
    // 校验在其内；零修订零失效零事件：KIN-06/AT-04 结构保证——本函数
    // 无任何命令提交通道）。
    writebackToSessionPose(*sessionPose, writeback);
    return "会话姿态已更新（零修订——仅会话态）";
}

std::string resetSessionHome(KinSessionPose* sessionPose,
                             const std::vector<double>& homeQ)
{
    if (sessionPose == nullptr) {
        return "会话姿态未装配——复位不可用";
    }
    // KIN-06"复位走会话命令零修订"（MDL-17 建模侧同则登记）——以 Home
    // 构型覆写会话姿态，不产生任何修订/失效。
    sessionPose->resetToHome(homeQ);
    return "已复位 Home（零修订——仅会话态）";
}

std::string resetSessionHomeFromNamedSource(const KinPanelServices& services)
{
    // ---- 拒绝态①：会话姿态缝未装配（无写点可落——如实说明）。
    if (services.sessionPose == nullptr) {
        return "复位 Home 不可用：会话姿态未装配";
    }
    // ---- 拒绝态②：具名 Home 来源缝未装配（KIN-P3/审查 A09——来源未
    // 装配＝按钮应已禁用；此处为直达槽路径的防御面，显式拒绝不写回）。
    if (!services.homePose) {
        return "复位 Home 不可用：具名 Home 来源未装配"
               "（需装配层从命名位姿集保留键 homeConfiguration 注入）";
    }
    // ---- 拒绝态③：来源在场但模型/会话未定义 Home（nullopt）——绝不
    // 静默把当前姿态充当 Home（审查 A09 根因即"无目标来源的原样写回"
    // 假成功），给用户明确提示。
    const std::optional<std::vector<double>> home = services.homePose();
    if (!home.has_value()) {
        return "复位 Home 不可用：模型/会话未定义具名 Home 姿态"
               "（命名位姿集保留键 homeConfiguration 缺失——请先在建模侧"
               "定义 Home）——未写回";
    }
    // ---- 拒绝态④：来源值维度与设备自由度不符（装配层解析违约/模型
    // 变更未同步）——显式反馈不钳制不截断（截断写回会伪造复位语义，
    // NFR-COR-03）。dof 口径与求解器/FK 一致（Fixed 不消耗 q）。
    std::size_t dof = 0;
    if (services.modelView != nullptr) {
        for (const runtime::CanonicalJoint& j :
             services.modelView->model().chain().joints) {
            if (j.type != runtime::JointType::Fixed) {
                ++dof;
            }
        }
    }
    if (services.modelView == nullptr || home->size() != dof) {
        return "复位 Home 不可用：Home 维度 " + std::to_string(home->size())
               + " != 设备自由度 " + std::to_string(dof)
               + "（来源解析违约或模型已变更）——未写回";
    }
    // ---- 拒绝态⑤：来源值含非有限分量（F-903——NaN Home 双轨统一）。
    // 修复前维度检查通过而值含 NaN 时，非有限拒绝只在写入口
    // （KinSessionPose::setJointConfiguration 的 fail-fast）触发——异常
    // 穿越编排函数直达 Qt 槽，与维度违约的显式提示轨双轨不一致（验收
    // 探针实测异常传播、会话态零污染）。本态前置拦截：值面违约与维度
    // 违约同走提示轨（显式反馈零写回），编排层不再让任何可预见的调用
    // 方违约以异常形态离开本函数。NaN/±∞ 的可达来源＝装配层解析违约
    // （正常装配下 Home 来自命名位姿集解码的合法数值）。
    for (std::size_t i = 0; i < home->size(); ++i) {
        if (!std::isfinite((*home)[i])) {
            return "复位 Home 不可用：Home 第 " + std::to_string(i + 1)
                   + " 分量非有限（NaN/±∞——来源解析违约）——未写回";
        }
    }
    // ---- 通过：唯一写点覆写会话姿态（零修订——KIN-06/AT-04）。
    return resetSessionHome(services.sessionPose, *home);
}

std::optional<std::string> tcpBackfillText(
    const KinSessionPose* sessionPose, const KinModuleSessionState& session,
    const std::vector<double>& jointsSi,
    const std::vector<runtime::JointType>& dofJointTypes)
{
    // 未设置（isSet()==false）或缝空——ui 回退编辑器缺省值，不得读值
    // （KinSessionPose.hpp 契约）。
    if (sessionPose == nullptr || !sessionPose->isSet()) {
        return std::nullopt;
    }
    // 维度契约：逐轴类型表与关节向量同维同链序（KIN-P3/审查 A05——失配
    // ＝调用方装配错误，fail-fast 而非猜量纲）。
    if (jointsSi.size() != dofJointTypes.size()) {
        throw std::invalid_argument(
            "tcpBackfillText: 关节向量维度 " + std::to_string(jointsSi.size())
            + " != 逐轴类型表维度 " + std::to_string(dofJointTypes.size()));
    }
    // 会话姿态→显示制式文本（仅呈现默认值——求解身份不含会话，D-KIN-4）。
    // 逐轴按天然量纲投影（移动轴长度/转动轴角度——审查 A05 同缺陷面的
    // 修复：修复前回填文本对全轴按角度投影，混合链移动轴显示失实）。
    std::string text;
    const std::vector<double>& q = sessionPose->jointConfiguration();
    if (q.size() != dofJointTypes.size()) {
        throw std::invalid_argument(
            "tcpBackfillText: 会话姿态维度 " + std::to_string(q.size())
            + " != 逐轴类型表维度 " + std::to_string(dofJointTypes.size()));
    }
    for (std::size_t i = 0; i < q.size(); ++i) {
        if (i != 0) {
            text += ", ";
        }
        text += formatJointAxisText(q[i], dofJointTypes[i],
                                    session.displayUnits);
    }
    return text;
}

std::optional<KinTcpTargetBackfill> tcpTargetBackfillValues(
    const KinPanelServices& services, const KinModuleSessionState& session)
{
    // ---- 前提缝四件（缺一＝回填不可用——nullopt，调用方状态行如实说明；
    // 不虚构回填——与 tcpBackfillText 未设置轨同纪律）。
    if (services.fkEvaluator == nullptr || services.modelView == nullptr
        || services.sessionPose == nullptr
        || !services.sessionPose->isSet()) {
        return std::nullopt;
    }
    // ---- TCP 身份（三入口统一解析单点——KIN-14 默认工具权威；失败＝
    // 回填不可用，绝不以空 TcpRef 求 FK）。
    const KinTcpResolution tcp = resolveSessionTcp(services);
    if (!tcp.ok) {
        return std::nullopt;
    }
    // ---- FK 求值（会话姿态→当前 TCP 位姿——基座系 {B}）；失败＝诚实
    // 回退（维度违约等调用方装配态——不做部分回填）。
    const auto metrics = services.fkEvaluator->evaluate(
        *services.modelView, tcp.tcp,
        services.sessionPose->jointConfiguration());
    if (!metrics.ok()) {
        return std::nullopt;
    }
    const rw::math::Transform3D<double>& pose = metrics.get().tcpInBase;
    // ---- 显示制式投影（SI→显示正向——KIN-12 读侧投影唯一口径；数值
    // 本体不带单位后缀——单位随编辑器行标签）。姿态提取 roll/pitch/yaw
    // （Z-Y-X——KinPanelRotation 轴义，与位姿页显示行同源，审查 G7 消
    // 歧登记同源）。
    KinTcpTargetBackfill out;
    out.xDisplay = session.displayUnits.has_value()
        ? session.displayUnits->projectLength(pose.P()[0])
        : pose.P()[0];
    out.yDisplay = session.displayUnits.has_value()
        ? session.displayUnits->projectLength(pose.P()[1])
        : pose.P()[1];
    out.zDisplay = session.displayUnits.has_value()
        ? session.displayUnits->projectLength(pose.P()[2])
        : pose.P()[2];
    const std::array<double, 3> rpy = rpyFromRotationSi(pose.R());
    out.rollDisplay = session.displayUnits.has_value()
        ? session.displayUnits->projectAngle(rpy[0])
        : rpy[0];
    out.pitchDisplay = session.displayUnits.has_value()
        ? session.displayUnits->projectAngle(rpy[1])
        : rpy[1];
    out.yawDisplay = session.displayUnits.has_value()
        ? session.displayUnits->projectAngle(rpy[2])
        : rpy[2];
    return out;
}

KinConfigEditResult applyConfigurationEdit(
    KinModuleSessionState& session, const AnalysisConfiguration& savedBefore,
    const std::vector<std::pair<std::string, double>>& changes,
    const KinConfigPersistFn& persist)
{
    KinConfigEditResult result;

    // ---- 步骤 1：逐键写回草稿（键表封闭——未知键＝调用方装配错误，
    //      fail-fast 不静默丢弃）。
    AnalysisConfiguration draft = savedBefore;
    for (const auto& [key, value] : changes) {
        if (key == kKeyInitialStrategy) {
            // 枚举编码 0/1/2（ReferenceQ/SeededRandom/JointGrid）——域外
            // 数值（越界/非整数）由 validate 步骤拦截（枚举值域校验）。
            draft.initialStrategy = static_cast<InitialValueStrategy>(
                static_cast<int>(value));
        } else if (key == kKeyInitialValuesCount) {
            draft.initialValuesCount = static_cast<std::uint32_t>(value);
        } else if (key == kKeyIterationLimit) {
            draft.iterationLimit = static_cast<std::uint32_t>(value);
        } else if (key == kKeyPositionTol) {
            draft.positionResidualTolerance = value;  // 单位 m
        } else if (key == kKeyOrientationTol) {
            draft.orientationResidualTolerance = value;  // 单位 rad（不是度）
        } else if (key == kKeyDedupThreshold) {
            draft.ikDedupThresholdPerAxis = value;  // rad|m 逐轴
        } else if (key == kKeyRegionSeed) {
            if (value < 0.0 || value > kMaxUint53) {
                throw std::invalid_argument(
                    "配置键 region-budget-seed 超出无损整数范围");
            }
            draft.regionBudget.seed = static_cast<std::uint64_t>(value);
        } else if (key == kKeyRegionThreads) {
            draft.regionBudget.threadCount = static_cast<std::uint32_t>(value);
        } else if (key == kKeySeed) {
            if (value < 0.0 || value > kMaxUint53) {
                throw std::invalid_argument("配置键 seed 超出无损整数范围");
            }
            draft.seed = static_cast<std::uint64_t>(value);  // 0 非法（I-KIN-4）
        } else {
            throw std::invalid_argument("未知配置字段键：" + key);
        }
    }

    // ---- 步骤 2：域设施校验（非法即整批拒绝零变更——NFR-COR-03 不钳制；
    //      异常文案即首错定位）。
    try {
        validateAnalysisConfiguration(draft);
    } catch (const std::invalid_argument& e) {
        result.ok = false;
        result.reason = e.what();
        result.statusText = "配置未保存：校验拒绝（原配置保持不变）";
        return result;
    }

    // ---- 步骤 3：依赖提示（域设施 analyzeConfigurationChange——纯提示，
    //      L-K7"不自动重算"：本函数无任何评估触发入口）。
    const AnalysisConfigChangeHint hint =
        analyzeConfigurationChange(savedBefore, draft);
    result.hintRows = configChangeHintRows(hint);

    // F04/AT-27：持久化是基线前移的前提。拒绝、缺通道或存储异常都
    // 返回失败且零会话更新；异常仅在副作用端口边界转换，未知键仍fail-fast。
    if (!persist) {
        result.reason = "用户级持久化通道未装配——原配置保持不变，请装配后重试";
    } else {
        try {
            if (!persist(draft)) {
                result.reason = "配置保存失败（用户级通道拒绝）——原配置保持不变，可重试";
            }
        } catch (const std::exception& error) {
            result.reason = "配置保存失败（用户级存储异常）：" + std::string(error.what())
                            + "——原配置保持不变，可重试";
        }
    }
    if (!result.reason.empty()) {
        result.statusText = result.reason;
        return result;
    }
    session.savedConfig = draft;
    result.ok = true;
    result.statusText = "配置已保存（用户级）" + std::string(hint.configChanged
        ? "；受影响结果需重算（见提示）" : "");
    return result;
}

std::string submitSetDefaultTcp(const KinPanelServices& services,
                                const KinModuleSessionState& session,
                                const TcpRef& tcp)
{
    // ---- 只读门控（写类命令——新修订）。
    if (!kinCommandEnabledInSession("kinematics.set-default-tcp",
                                    session.writable)) {
        return "设默认 TCP 已拒绝：只读会话不可产生修订";
    }
    // ---- 门面缝装配校验。
    if (services.commandHandler == nullptr) {
        return "设默认 TCP 不可用：命令门面未装配";
    }
    // ---- L-K9 数据流：面板发起→门面（组装 modeling 命令）→①端口→
    //      新修订回执→依赖失效提示（KIN-14/§9.7）。
    const CommandSubmission sub = services.commandHandler->setProjectDefaultTcp(tcp);
    if (sub.committed()) {
        return "默认 TCP 已更新（新修订 " + sub.newRevision->toCanonical() + "）"
               "——依赖默认 TCP 的结果需重算";
    }
    // NotCommitted：诊断码透传文本（KIN-*/MDL-* 门面自产与透传记录；
    // DiagCode 即 std::string——直拼）。
    std::string text = "设默认 TCP 未提交：";
    for (const core::DiagnosticRecord& d : sub.diagnostics) {
        text += d.code + " ";
    }
    return text;
}

std::string submitSetDefaultDevice(const KinPanelServices& services,
                                   const KinModuleSessionState& session,
                                   const core::ObjectId& robotOid)
{
    // 门控/缝/编排语义同 TCP 路径（对象为 robot-design 根——R1 单设备
    // 模型的设备指定语义，Commands.hpp §9.2 行文）。
    if (!kinCommandEnabledInSession("kinematics.set-default-device",
                                    session.writable)) {
        return "设默认设备已拒绝：只读会话不可产生修订";
    }
    if (services.commandHandler == nullptr) {
        return "设默认设备不可用：命令门面未装配";
    }
    const CommandSubmission sub = services.commandHandler->setProjectDefaultDevice(robotOid);
    if (sub.committed()) {
        return "默认设备已更新（新修订 " + sub.newRevision->toCanonical() + "）"
               "——依赖默认设备的结果需重算";
    }
    std::string text = "设默认设备未提交：";
    for (const core::DiagnosticRecord& d : sub.diagnostics) {
        text += d.code + " ";
    }
    return text;
}

KinRecomputeFlowResult recomputeSessionFromIdentity(
    const KinPanelServices& services, KinModuleSessionState& session,
    const rw::math::Transform3D<double>& targetInBase)
{
    KinRecomputeFlowResult result;

    // ---- 拒绝态 1：原任务定义不在场（会话尚无求解产物——"复算"无
    // 原点；如实拒绝，绝不以空身份构造复算请求）。
    if (!session.lastSessionSolutionSet) {
        result.statusText = "复算不可用：会话尚无求解产物（先完成一次求解"
                            "——复算按原任务定义重绑定当前快照）";
        return result;
    }
    // ---- 拒绝态 2：缝缺位（模型视图/求解器——复算执行面）。
    if (services.modelView == nullptr || services.ikSolver == nullptr) {
        result.statusText = "复算不可用：模型视图或求解器未装配";
        return result;
    }
    // ---- 拒绝态 3：当前快照未绑定（重绑定的目标锚缺失——空身份被域
    // 设施拒绝，此处前置拦截给可读原因）。
    if (session.snapshotId == core::ContentIdentity{}) {
        result.statusText = "复算不可用：当前会话未绑定快照（先应用建模"
                            "草稿生成发布快照）";
        return result;
    }

    // ---- 步骤 1：复算请求构造（域设施 buildKinRecomputeRequest——
    // §7.4 三要件：sliceId/mode/seed/referenceQ 逐字段取自原结果身份，
    // snapshotId＝当前会话绑定〔重绑定〕）。原任务定义中可恢复的字段
    // 全部从身份显式携带（D-KIN-4——不复用会话残留）。域设施对空评估
    // 键/空快照 fail-fast——本函数前置已拦空快照，此处异常属域校验链
    // 兜底（如实拦截转状态行，不让异常穿越 Qt 槽）。
    try {
        result.recompute = buildKinRecomputeRequest(
            session.lastSessionSolutionSet->requestIdentity,
            kTaskPointIkEvaluationKey, session.snapshotId);
    } catch (const std::invalid_argument& e) {
        result.statusText =
            std::string("复算不可用：复算请求构造被拒——") + e.what();
        return result;
    }
    const KinRecomputeRequest& recompute = *result.recompute;

    // ---- 步骤 2：TCP 解析（三入口统一单点——KIN-P4；失败即拒绝，
    // 绝不以空 TcpRef 提交求解器）。
    const KinTcpResolution tcp = resolveSessionTcp(services);
    if (!tcp.ok) {
        result.statusText = "复算不可用：" + tcp.statusText;
        return result;
    }

    // ---- 步骤 3：请求重装配（同键同种子、当前快照与当前配置——
    // 差异语义：seed/referenceQ 从原身份显式携带〔同键复算〕，容差/
    // 迭代/初值策略与数量取当前 savedConfig〔配置演化的当前性判定归
    // evidence 对账——CON-02，本流如实按当前配置执行〕；intervals 取
    // 当前快照——改型后的新限位即复算的输入面）。
    const AnalysisConfiguration& config = session.savedConfig;
    try {
        validateAnalysisConfiguration(config);
    } catch (const std::invalid_argument& e) {
        result.statusText = std::string("复算不可用：求解配置非法（")
                            + std::string(kKinConfigIllegal) + "）：" + e.what();
        return result;
    }
    const std::vector<JointInterval> intervals =
        evaluationIntervals(*services.modelView);
    const std::vector<std::vector<double>> initialValues = makeInitialValues(
        config.initialStrategy, config.initialValuesCount, recompute.seed,
        intervals, recompute.referenceQ);

    IkRequest req;
    req.targetInBase = targetInBase;            // 基座系 {B}（§5.3——面板当前编辑值）
    req.tcp = tcp.tcp;                          // 三入口统一的合法工具身份
    req.positionTolerance = config.positionResidualTolerance;        // m
    req.orientationTolerance = config.orientationResidualTolerance;  // rad（不是度）
    req.modelView = services.modelView;         // 当前快照视图（重绑定）
    req.initialValues = initialValues;
    req.iterationLimit = config.iterationLimit;
    req.intervals = intervals;
    req.dedupThresholdPerAxis = config.ikDedupThresholdPerAxis;      // rad|m 逐轴
    req.referenceQ = recompute.referenceQ;      // 原身份显式携带（D-KIN-4）
    // ---- 结果身份重填（重绑定可见面）：snapshotId/configDigest＝当前
    // 会话值（新解集导出头随之锚定当前快照），seed/referenceQ＝原身份值
    // （同键复算的携带证据）。mode 沿原身份（同键同模式——EVI-01）。
    req.requestIdentity.snapshotId = session.snapshotId;
    req.requestIdentity.configDigest =
        analysisConfigurationDigest(config);
    req.requestIdentity.mode = recompute.mode;
    req.requestIdentity.seed = recompute.seed;
    req.requestIdentity.referenceQ = recompute.referenceQ;

    // ---- 步骤 4：内联求解（复算规模与单点求解同量级——内联执行语义
    // 同 L-K2；取消/设施失败如实呈现，零会话写入）。
    const IkOutcome outcome = services.ikSolver->solve(req);
    if (outcome.cancelled) {
        result.statusText = "复算已取消（非结局）";
        return result;
    }
    if (outcome.facilityFailure.has_value()) {
        result.statusText = "复算设施失败（非结局——求解中途 FK 复算失败，"
                            "请核对模型视图后重试）";
        return result;
    }

    // ---- 步骤 5：新解集入会话态（唯一写点——与 runSinglePointSolve 同
    // 纪律；原解集被覆盖，新旧解数对比在状态行如实给出）。
    const std::size_t originalCount =
        session.lastSessionSolutionSet->solutions.size();
    session.lastSessionSolutionSet =
        std::make_shared<const IkSolutionSet>(std::move(outcome.solutionSet));
    session.lastSessionSolutionSetView =
        std::make_shared<const KinematicSolutionSet>(*session.lastSessionSolutionSet);
    result.ok = true;
    result.statusText =
        "已按原任务定义复算（快照重绑定为当前发布快照）——原解 "
        + std::to_string(originalCount) + " 个 → 新解 "
        + std::to_string(session.lastSessionSolutionSet->solutions.size())
        + " 个";
    return result;
}

std::string exportSessionResults(const KinPanelServices& services,
                                 const KinModuleSessionState& session,
                                 const std::string& targetPath, bool asCsv)
{
    // ---- 缝/数据前置校验（四态如实：通道未装配/无结果/写出失败/成功）。
    if (!services.exportWriter) {
        return "导出不可用：写出通道未装配";
    }
    if (!session.lastSessionSolutionSet) {
        return "无可导出结果（会话尚无求解产物）";
    }

    // ---- 域设施打包（KIN-08 值行——来源声明：会话内存结果；runId 必空
    //      ——KinExportHeader 纪律）。快照绑定取自解集请求身份（§2.5 溯源）。
    const IkSolutionSet& raw = *session.lastSessionSolutionSet;
    KinExportHeader header;
    header.source = KinExportSource::SessionMemory;
    header.runId = "";  // SessionMemory 必空——buildKinExportPackage 校验面
    header.snapshotId = raw.requestIdentity.snapshotId;
    header.mode = raw.requestIdentity.mode;

    // ---- 打包＋编码（域设施唯一实现点——本函数零行构造零编码算术）。
    KinExportPackage package = buildKinExportPackage(
        header, buildSolutionExportRows(raw),
        buildFilteredSolutionExportRows(raw), {});
    const std::string content =
        asCsv ? encodeKinExportCsv(package) : encodeKinExportJson(package);

    // ---- io 写出缝（AtomicFile 语义归 io/装配层——失败旧文件完好）。
    if (services.exportWriter(targetPath, content)) {
        return "已导出副本（" + targetPath + "）——零修订";
    }
    return "导出写出失败（目标路径不可写？）——旧文件不受影响";
}

// =====================================================================
// 探索式工作空间采样（★ KIN-R2 任务3/KIN-09——实现落点；声明面见
// KinPanelFlows.hpp 本节注）
// =====================================================================

KinWorkspaceSamplingFlowResult runWorkspaceSamplingFlow(
    const KinPanelServices& services, KinModuleSessionState& session,
    const KinWorkspaceSamplingParams& params,
    evidence::IEvaluationContext& context)
{
    KinWorkspaceSamplingFlowResult result;
    // 模式溯源 token（F-905 感知半区——域词表唯一出口，禁第二处字面量；
    // 拒绝轨也回填 token 便于状态行标注"哪种模式被拒"）。
    result.samplingModeToken =
        std::string(workspaceSamplingModeToken(params.mode));

    // ---- 拒绝态①：模型视图缝未装配（采样执行面缺失——如实拒绝）。
    if (services.modelView == nullptr) {
        result.statusText = "采样不可用：模型视图未装配";
        return result;
    }
    const IKinRuntimeView& view = *services.modelView;

    // ---- 拒绝态②：TCP 身份解析（三入口统一单点——区域模式逐样本 IK
    // 与关节模式 FK 都消费合法工具身份；失败＝KIN-NO-TCP 提示轨，绝不
    // 以空 TcpRef 继续走流）。
    const KinTcpResolution tcp = resolveSessionTcp(services);
    if (!tcp.ok) {
        result.statusText = "采样不可用：" + tcp.statusText;
        return result;
    }

    // ---- 步骤 1：域查询装配（面板参数 SI 直投；语义逐字段对位
    // WorkspaceSamplingQuery 注——装配层零再加工）。
    WorkspaceSamplingQuery query;
    query.mode = params.mode;
    query.region.center = rw::math::Vector3D<double>(
        params.regionCenterX, params.regionCenterY, params.regionCenterZ);  // 基座系 m
    query.region.size = rw::math::Vector3D<double>(
        params.regionSizeX, params.regionSizeY, params.regionSizeZ);        // 基座系 m
    // 位置采样定义（仅区域模式消费）：随机＝randomCount 撒点、网格＝
    // 三轴计数体心网格（§7.2 两方法探索式直用）。
    query.position.method = params.positionRandom
                                ? kinematics::PositionSamplingDefinition::Method::Random
                                : kinematics::PositionSamplingDefinition::Method::Grid;
    query.position.gridCounts = params.regionGridCounts;
    query.position.randomCount = params.regionRandomCount;
    // 姿态定义退化单方向单 roll（KIN-09 主径为位置点云——面板不暴露
    // 姿态采样编辑，登记为呈现批次边界；斐波那契螺旋×roll 均分在退化
    // 配置下即单姿态）。
    query.orientation.directionSamples = 1U;
    query.orientation.rollSamples = 1U;
    // 关节模式参数（域面收 per-axis 向量——面板统一值施加全部自由度，
    // UI 参数化子集登记于参数结构注）。
    query.jointRandomCount = params.jointRandomCount;
    // ---- 步骤 2：设备自由度（Fixed 不消耗 q——与求解器/FK 同一口径）。
    std::size_t dof = 0;
    for (const runtime::CanonicalJoint& j : view.model().chain().joints) {
        if (j.type != runtime::JointType::Fixed) {
            ++dof;
        }
    }
    query.jointGridCounts.assign(dof, params.jointGridPerAxis);
    // ---- 步骤 3：求解配置直投（seed/预算权威面——savedConfig 会话
    // 纪律恒合法；配置非法（如 seed=0）由域校验面拒绝转状态行）。
    query.config = session.savedConfig;
    // ---- 步骤 4：排序参考构型＝模型链零位（D-KIN-4 显式输入的确定性
    // 投影——与正式通道执行器 sliceCoverage 同口径，零会话隐式读取）。
    for (const runtime::CanonicalJoint& j : view.model().chain().joints) {
        if (j.type != runtime::JointType::Fixed) {
            query.referenceQ.push_back(j.zeroOffset);  // rad|m（链序）
        }
    }
    query.tcp = tcp.tcp;
    // 碰撞会话不注入（统一策略注入面——空＝碰撞不在范围，逐样本
    // collisionNotEvaluated 如实标记，绝不解读为无碰撞——KIN-05）；批
    // 大小/求解器替身沿域缺省（256/D-KIN-6；services.ikSolver 缺省不
    // 注入——探索式与单点求解的通道分立，注入语义留装配层后续批次）。

    // ---- 步骤 5：域校验（invalid_argument＝调用方参数错误——转状态行
    // 不让异常穿越 Qt 槽；文案含首错字段/实际值/合法域）。
    try {
        validateWorkspaceSamplingQuery(view, query);
    } catch (const std::invalid_argument& e) {
        result.statusText =
            std::string("采样查询非法（") + result.samplingModeToken
            + "）：" + e.what();
        return result;
    }

    // ---- 步骤 6：内联运行（域计算核——取消/进度经注入上下文；
    // logic_error＝域内部不变量破坏，如实转状态行不静默）。
    WorkspaceSampleRun run;
    try {
        run = runWorkspaceSampling(view, query, context);
    } catch (const std::invalid_argument& e) {
        // 域校验链兜底（步骤 5 已前置拦——此态属防御面）。
        result.statusText =
            std::string("采样查询非法（") + result.samplingModeToken
            + "）：" + e.what();
        return result;
    } catch (const std::exception& e) {
        result.statusText = std::string("采样运行失败：") + e.what();
        return result;
    }

    // ---- 步骤 7：产物入会话态（唯一写点——含 cancelled partial 产物；
    // R2 边界：探索式产物不入 results 归档，域类型级无覆盖率字段）。
    const std::uint64_t total = run.results.size();
    std::uint64_t reached = 0;
    std::uint64_t dataInsufficient = 0;
    std::uint64_t unreachable = 0;
    for (const WorkspaceSampleResult& r : run.results) {
        // 逐样本状态计数（五值词表——呈现侧摘要的真实计数面，零估算）。
        switch (r.state) {
            case SampleState::Reached:
                ++reached;
                break;
            case SampleState::DataInsufficient:
                ++dataInsufficient;
                break;
            case SampleState::Unreachable:
                ++unreachable;
                break;
            default:
                break;  // NotRun/NotApplicable——notRun 数由 run 自带
        }
    }
    session.lastWorkspaceRun =
        std::make_shared<const WorkspaceSampleRun>(std::move(run));
    result.ok = true;
    result.statusText =
        std::string("采样完成（") + result.samplingModeToken + "）：共 "
        + std::to_string(total) + " 样本，可达 " + std::to_string(reached)
        + "、不可达 " + std::to_string(unreachable) + "、数据不足 "
        + std::to_string(dataInsufficient)
        + (session.lastWorkspaceRun->cancelled
               ? "；运行被取消（partial 产物——未运行样本如实保留）"
               : "");
    return result;
}

std::string exportWorkspacePointCloudCsv(const KinPanelServices& services,
                                         const KinModuleSessionState& session,
                                         const std::string& targetPath)
{
    // ---- 四态如实（与 exportSessionResults 同纪律）：缝未装配/无产物/
    // 写出失败/成功。
    if (!services.exportWriter) {
        return "点云导出不可用：写出通道未装配";
    }
    if (!session.lastWorkspaceRun) {
        return "无可导出点云（尚未运行采样——先运行一次工作空间采样）";
    }
    // ---- 域设施链（渲染数据唯一组装点→契约 v2 CSV 编码——行头含
    // sampling-mode 溯源列，F-905 下游感知的导出半区；呈现侧零自拼行）。
    const WorkspacePointCloudRenderData data =
        buildWorkspacePointCloud(*session.lastWorkspaceRun);
    const std::string content = workspacePointCloudExportCsv(data);
    if (services.exportWriter(targetPath, content)) {
        return "已导出点云（" + targetPath + "，契约 v2 含 sampling-mode 列）——零修订";
    }
    return "点云导出写出失败（目标路径不可写？）——旧文件不受影响";
}

std::string writebackWorkspacePoint(const KinPanelServices& services,
                                    const KinModuleSessionState& session,
                                    std::uint64_t sampleIndex)
{
    // ---- 拒绝态：会话姿态缝/采样产物缺位（如实——不虚构写入）。
    if (services.sessionPose == nullptr) {
        return "回写不可用：会话姿态未装配";
    }
    if (!session.lastWorkspaceRun) {
        return "回写不可用：尚未运行采样（先运行一次工作空间采样）";
    }
    // ---- 域回写值→既有会话回写通道（workspacePointWriteback 对越界/
    // 无解样本 fail-fast——就地转状态行，异常不穿越 Qt 槽；NFR-COR-03
    // 与 F-903 提示轨同则）。
    try {
        const RenderPointWriteback writeback =
            workspacePointWriteback(*session.lastWorkspaceRun, sampleIndex);
        return applyPoseWriteback(services.sessionPose, writeback)
               + "（样本 " + std::to_string(sampleIndex) + "）";
    } catch (const std::invalid_argument& e) {
        return std::string("回写被拒：") + e.what();
    }
}

std::string exportWorkspaceProjectionPng(const KinPanelServices& services,
                                         const KinModuleSessionState& session,
                                         const std::string& targetPath,
                                         const std::string& pngBytes)
{
    // ---- 四态如实（与点云 CSV 导出同纪律）。空 PNG 字节＝呈现面编码
    // 失败的误用形态——如实拒绝不写空文件。
    if (!services.exportWriter) {
        return "PNG 导出不可用：写出通道未装配";
    }
    if (!session.lastWorkspaceRun) {
        return "无可导出投影（尚未运行采样——先运行一次工作空间采样）";
    }
    if (pngBytes.empty()) {
        return "PNG 导出不可用：画布编码为空（呈现面异常）";
    }
    // ---- 写出（字节直传——io write 以 std::string_view 承载，二进制
    // 安全；PNG 权威分工的勘察口径见头注）。
    if (services.exportWriter(targetPath, pngBytes)) {
        return "已导出投影视图（" + targetPath + "）——零修订";
    }
    return "PNG 导出写出失败（目标路径不可写？）——旧文件不受影响";
}

}  // namespace kinematics
}  // namespace ird
}  // namespace sdurws
