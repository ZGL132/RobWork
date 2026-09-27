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

#include <sdurws/ird/kinematics/Export.hpp>  // 导出打包/编码（KIN-08 值行——域设施）
#include "KinPanelCommandCatalog.hpp"        // kinCommandEnabledInSession（L-K11 id 门控）
#include "KinPanelModel.hpp"                 // configChangeHintRows（L-K7 提示行化）
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
    result.inlineElapsed = elapsed;

    // ---- 取消：非结局（§5.4）——如实呈现，不入会话态。
    if (outcome.cancelled) {
        result.statusText = "求解已取消（非结局）";
        return result;
    }

    // ---- 内联预算判定（NFR-PERF-01/§9.8：>1 s 转后台；预算可注入——
    //      测试确定性触发，产品缺省恒 kInlineBudgetMs）。
    if (elapsed > inlineBudget) {
        if (services.backgroundSubmit) {
            // 自动转后台（会话级——不写 results；真实 execution 通道在
            // 装配层适配）。纪元随请求携带（迟到判定锚）。
            KinBackgroundRequest bg;
            bg.kind = KinBackgroundKind::SessionSolve;
            bg.snapshotId = session.snapshotId;
            bg.configDigest = analysisConfigurationDigest(session.savedConfig);
            bg.epoch = session.epoch;
            const KinBackgroundAck ack = services.backgroundSubmit(bg);
            result.deferredToBackground = ack.accepted;
            result.statusText = ack.accepted
                ? ("内联耗时 " + std::to_string(elapsed.count())
                   + " ms 超预算——已转后台执行（" + ack.reason + "）")
                : ("内联耗时 " + std::to_string(elapsed.count())
                   + " ms 超预算，后台提交被拒：" + ack.reason);
            return result;
        }
        // 缝未装配——不虚构转后台，如实说明（诚实边界）。
        result.statusText = "内联耗时 " + std::to_string(elapsed.count())
                            + " ms 超预算，且后台通道未装配——保留内联结果供参考";
    } else {
        result.statusText = "内联完成（" + std::to_string(elapsed.count())
                            + " ms），解 " + std::to_string(outcome.solutionSet.solutions.size())
                            + " 个";
    }

    // ---- 解集入会话态（唯一写点——会话级解集会话对象本身，非投影副本；
    //      原值面供导出/结果绑定，视图面构造时完成一次稳定排序供解表/
    //      检查器——双面同源零二次求解）。
    session.lastSessionSolutionSet =
        std::make_shared<const IkSolutionSet>(std::move(outcome.solutionSet));
    session.lastSessionSolutionSetView =
        std::make_shared<const KinematicSolutionSet>(*session.lastSessionSolutionSet);
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

std::optional<std::string> tcpBackfillText(const KinSessionPose* sessionPose,
                                           const KinModuleSessionState& session,
                                           const std::vector<double>& jointsSi)
{
    // 未设置（isSet()==false）或缝空——ui 回退编辑器缺省值，不得读值
    // （KinSessionPose.hpp 契约）。
    if (sessionPose == nullptr || !sessionPose->isSet()) {
        return std::nullopt;
    }
    // 会话姿态→显示制式文本（仅呈现默认值——求解身份不含会话，D-KIN-4）。
    std::string text;
    const std::vector<double>& q = sessionPose->jointConfiguration();
    for (std::size_t i = 0; i < q.size(); ++i) {
        // 关节角主导量纲——按角度制式投影（rad→deg；nullopt＝SI 直显）。
        const double shown = session.displayUnits.has_value()
                                 ? session.displayUnits->projectAngle(q[i])
                                 : q[i];
        if (i != 0) {
            text += ", ";
        }
        text += std::to_string(shown);
    }
    return text;
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
            // 枚举编码 1/2/3（ReferenceQ/SeededRandom/JointGrid）——域外
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

    // ---- 步骤 4：保存（用户级 PM-14 通道投影——缝未装配＝仅会话生效
    //      并如实说明；P-KIN-4：存储载体归 ui 侧，接口待冻结）。
    if (persist) {
        if (persist(draft)) {
            session.savedConfig = draft;  // 保存成功——基线前移
            result.statusText = "配置已保存（用户级）"
                                + std::string(hint.configChanged
                                                  ? "；受影响结果需重算（见提示）"
                                                  : "");
        } else {
            result.statusText = "配置保存失败（用户级通道拒绝）——编辑仅在会话生效";
        }
    } else {
        result.statusText = "用户级持久化通道未装配——编辑仅在会话生效";
    }
    result.ok = true;
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

}  // namespace kinematics
}  // namespace ird
}  // namespace sdurws
