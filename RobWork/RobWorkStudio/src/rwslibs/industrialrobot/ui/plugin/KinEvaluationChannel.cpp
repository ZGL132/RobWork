/**
 * @file   KinEvaluationChannel.cpp
 * @brief  kinematics 覆盖评估执行通道的产品装配半区实现（UI-T64——
 *         受理校验/工作集切片/后台执行/结构化结果账面/完成回投）。
 *
 * 设计依据：KinEvaluationChannel.hpp 文件头（职责链/权威边界/线程模型
 * 的权威声明在本 TU 逐段兑现）。
 */

#include "KinEvaluationChannel.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <utility>

#include <QMetaObject>

#include <sdurws/ird/kinematics/AnalysisConfig.hpp>  // analysisConfigurationDigest（受理对齐键）
#include <sdurws/ird/kinematics/Evaluators.hpp>      // runRegionCoverageComputation/RegionCoverageQuery
#include <sdurws/ird/kinematics/KinTypes.hpp>        // TcpRef/BatchDemands
#include <sdurws/ird/requirements/RequirementTypes.hpp>  // WorkRegion/SamplingPlan/RequirementReference
#include <sdurws/ird/requirements/Sampling.hpp>      // ISamplingPlanBuilder（计划摘要域函数——零复制）
#include <sdurws/ird/runtime/CanonicalModel.hpp>     // CanonicalModel（referenceQ/defaultTcp 提取）

namespace sdurws {
namespace ird {
namespace ui {

namespace {

/**
 * @brief 发布快照的 IKinRuntimeView 包裹器（attachSnapshot 的产品路径
 *        ——RuntimeSnapshot→视图接口的逐字转发薄壳；UI-T64 执行器内部
 *        使用，每次换绑新实例——"每请求构建"语义的载体）。
 *
 * 语义逐字对应（无第二语义）：model()/worldToBase() 纯转发——两接口
 * 的成员语义注释本就互相引用（KinTypes.hpp IKinRuntimeView 注："语义
 * 同 runtime::IRuntimeModelView::model()"）。线程约束：并发只读安全
 * （快照构造后只读——runtime §8.3）。
 */
class SnapshotViewAdapter final : public kinematics::IKinRuntimeView {
public:
    explicit SnapshotViewAdapter(
        std::shared_ptr<const runtime::RuntimeSnapshot> snapshot)
        : m_snapshot(std::move(snapshot))
    {
    }
    const runtime::CanonicalModel& model() const override
    {
        return m_snapshot->model();
    }
    rw::math::Transform3D<double> worldToBase() const override
    {
        return m_snapshot->worldToBase();
    }

private:
    /// 快照存活期锚（任务闭包经 shared_ptr 捕获——存活到任务结束）。
    std::shared_ptr<const runtime::RuntimeSnapshot> m_snapshot;
};

}  // namespace

// =====================================================================
// KinEvaluationExecutor——构造与会话事实接线
// =====================================================================

KinEvaluationExecutor::KinEvaluationExecutor(Deps deps)
    : m_deps(std::move(deps))
{
}

void KinEvaluationExecutor::attachSnapshot(
    std::shared_ptr<const runtime::RuntimeSnapshot> snapshot,
    core::ContentIdentity snapshotId)
{
    // 产品路径：快照包进视图接口（每次换绑新包裹实例——"每请求构建"
    // 语义的载体；旧实例随在途任务 shared_ptr 存活到任务结束）。
    std::shared_ptr<const kinematics::IKinRuntimeView> view =
        snapshot != nullptr
            ? std::shared_ptr<const kinematics::IKinRuntimeView>(
                std::make_shared<SnapshotViewAdapter>(std::move(snapshot)))
            : nullptr;
    attachView(std::move(view), std::move(snapshotId));
}

void KinEvaluationExecutor::attachView(
    std::shared_ptr<const kinematics::IKinRuntimeView> view,
    core::ContentIdentity snapshotId)
{
    // 换绑语义（hpp 类注）：视图＋身份整体替换＋纪元推进＋账面清空。
    // 纪元推进使在途任务的完成回执按旧纪元比对即"迟到"（L-K12——
    // 跨快照结果不进新会话视图）；账面清空使旧快照结果对新快照不可
    // 投影（PA 权威纪律的呈现侧镜像——非第二状态源）。缝地址稳定的
    // 长命视图（m_sessionView）同拍 rebind——面板 modelView 缝消费。
    m_snapshot = view;
    m_sessionView.rebind(std::move(view));
    m_snapshotId = snapshotId;
    ++m_epoch;
    m_latestCoverage.reset();
}

void KinEvaluationExecutor::setConfiguration(
    const kinematics::AnalysisConfiguration& config)
{
    // 与 kinematics 会话态 savedConfig 同源同值（装配层单点写两处）；
    // 合法性由装配纪律保证（I-KIN-4——seed≥1），本执行器不复判（域
    // 校验链单点）。
    m_config = config;
}

// =====================================================================
// 切片投影（UI 线程受理期——域投影零语义复判）
// =====================================================================

bool KinEvaluationExecutor::projectPlan(const requirements::SamplingPlan& plan,
                                        const requirements::WorkRegion& region,
                                        kinematics::SamplingPlan& out,
                                        std::string& errorText) const
{
    // ---- 第一步：参考系解析（refFrame 系 → 基座系 {B}）。
    // kinematics RegionBox 的契约是"宿主已把 refFrame 系解析到基座系"
    // （§5.1 坐标纪律）——本执行器就是这里的"宿主"。World 缺省＝恒等
    // 变换（不依赖解析缝）；ModelFrame 引用经解析缝取宿主帧世界位姿。
    rw::math::Transform3D<double> worldTref =
        rw::math::Transform3D<double>::identity();
    if (region.refFrame.kind == requirements::RequirementRefKind::ModelFrame) {
        if (!m_deps.resolveFrame || !region.refFrame.objectId
            || !region.refFrame.objectId->isValid()) {
            errorText = "区域参考系不可解析（引用缺载荷或解析缝未装配）";
            return false;
        }
        const std::optional<rw::math::Transform3D<double>> resolved =
            m_deps.resolveFrame(*region.refFrame.objectId);
        if (!resolved.has_value()) {
            // 帧不可解析＝投影失败诚实呈现（与需求三维投影同款语义——
            // 不虚构几何）。
            errorText = "区域参考系在宿主场景中不可解析";
            return false;
        }
        worldTref = *resolved;
    } else if (region.refFrame.kind != requirements::RequirementRefKind::World) {
        errorText = "区域参考系种类不在本批支持面（仅 World/ModelFrame）";
        return false;
    }

    // ---- 第二步：旋转检测（R1 轴对齐盒约束的诚实拒绝面）。
    // kinematics RegionBox 是基座系**轴对齐**盒；refFrame 相对基座存在
    // 旋转时盒变换后不再轴对齐——取保守包络会悄悄改变覆盖率语义（区
    // 域变大），旋转参考系解析留后续任务（契约批注），此处如实拒绝。
    const rw::math::Transform3D<double> baseTref =
        m_snapshot->worldToBase() * worldTref;  // T_base_ref＝T_base_world·T_world_ref
    const rw::math::Rotation3D<double>& r = baseTref.R();
    const double rotNorm = std::max({std::fabs(r(0, 0) - 1.0), std::fabs(r(0, 1)),
                                     std::fabs(r(0, 2)), std::fabs(r(1, 0)),
                                     std::fabs(r(1, 1) - 1.0), std::fabs(r(1, 2)),
                                     std::fabs(r(2, 0)), std::fabs(r(2, 1)),
                                     std::fabs(r(2, 2) - 1.0)});
    if (rotNorm > 1e-9) {  // 1e-9＝装配投影的数值零（远小于任何工程容差；
                           // 仅吸收恒等矩阵的浮点表示误差）
        errorText = "区域参考系相对基座存在旋转——R1 轴对齐盒约束，旋转"
                    "参考系解析留后续任务";
        return false;
    }

    // ---- 第三步：盒平移直投（旋转已排除——中心点平移变换，尺寸不变）。
    out.box.center = baseTref.P() + region.box.center;  // 平移合成（基座系，m）
    out.box.size = region.box.size;                     // 尺寸原值（m；非退化校验归域装配面）

    // ---- 第四步：采样定义投影（域值→域投影型——字段搬运零换算）。
    // 位置侧：工作集计划条目恒为规范化 Grid 形态（D-REQ-2——计划构建
    // 期规范化），GridBySpacing 出现即编辑器契约违约（如实拒绝，不静默
    // 再规范化——规范化域函数单点在 requirements，本执行器零复制）。
    switch (plan.positionSampling.method) {
        case requirements::PositionSamplingMethod::Grid:
            out.position.method = kinematics::PositionSamplingDefinition::Method::Grid;
            out.position.gridCounts = plan.positionSampling.counts;
            break;
        case requirements::PositionSamplingMethod::GridBySpacing:
            errorText = "采样计划未规范化（GridBySpacing 原始形态——编辑器"
                        "契约违约）";
            return false;
        case requirements::PositionSamplingMethod::Random:
            out.position.method = kinematics::PositionSamplingDefinition::Method::Random;
            out.position.randomCount = plan.positionSampling.count;
            break;
    }
    // 姿态侧：两计数＋方法 token 原样直投（≥1 约束归域装配面 fail-fast）。
    out.orientation.directionSamples = plan.orientationSampling.directionSamples;
    out.orientation.rollSamples = plan.orientationSampling.rollSamples;
    out.orientation.methodToken = plan.orientationSampling.methodToken;

    // ---- 第五步：计划内容身份（域函数现算——零复制纪律；CON-05 禁
    // 伪造身份）。requirements::SamplingPlanBuilder 无状态，digest 产出
    // 计划 canonical 摘要与分母面——摘要字节直构 ContentIdentity。
    static const requirements::SamplingPlanBuilder kPlanBuilder{};
    std::vector<core::DiagnosticRecord> diags;
    const auto digest = kPlanBuilder.digest(plan, diags);
    if (!digest.ok()) {
        errorText = "采样计划摘要计算失败（计划形态非法——域拒绝面透传）";
        return false;
    }
    out.planContentIdentity.bytes = digest.get().planDigest;  // Digest256 数组直构（32 字节）

    // ---- 锚与要求值（区域锚/区域级碰撞要求——KIN-05 承接面直投；
    // 镜像溯源不入评估——域注"生成不消费"，保持缺省形态）。
    out.regionObjectId = region.objectId;
    out.demands.collisionFreeRequired = region.demands.collisionFreeRequired;
    out.demands.minimumJointMargin = region.demands.minimumJointMargin;
    return true;
}

KinEvaluationExecutor::CoverageSlice KinEvaluationExecutor::sliceCoverage() const
{
    CoverageSlice slice;
    if (m_deps.editor == nullptr) {
        slice.errorText = "需求工作集未装配";
        return slice;
    }

    // ---- 工作集现取（UI 线程——编辑器线程纪律；值拷贝随任务闭包走）。
    const requirements::RequirementWorkingSet& ws = m_deps.editor->workingSet();
    if (ws.plans.entries.empty()) {
        slice.errorText = "工作集无采样计划（req-plan-set 空）";
        return slice;
    }

    // ---- referenceQ：模型链零位（确定性投影——D-KIN-4 显式输入的
    // 产品落位：排序参考构型＝权威零位，同模型同值）。
    const runtime::CanonicalModel& model = m_snapshot->model();
    for (const auto& joint : model.chain().joints) {
        slice.query.referenceQ.push_back(joint.zeroOffset);  // rad|m（链序）
    }

    // ---- defaultTcp：模型默认 TCP（KIN-14——tools 非空时 defaultTcpIndex
    // 必有值）；无工具＝缺省 TcpRef（评估面产 KIN-NO-TCP 零素材——诚实
    // 评估产出而非受理拒绝：无工具模型评估"不可达/无 TCP"是合法结论）。
    if (model.defaultTcpIndex().has_value()) {
        slice.query.defaultTcp.toolObject =
            model.tools().at(*model.defaultTcpIndex()).objectId;
        slice.query.defaultTcp.tcpKey.clear();  // 空串＝canonical TCP（单 TCP 面）
    }

    // ---- 求解配置七参数直投（savedConfig→Query——T10 前直传投影；
    // seed 随 budget 携带——§3.4 单一种子纪律；configDigest＝配置摘要
    // 入结果身份——与面板请求携带的 configDigest 同源函数产出）。
    slice.query.initialStrategy = m_config.initialStrategy;
    slice.query.initialValuesCount = m_config.initialValuesCount;
    slice.query.iterationLimit = m_config.iterationLimit;
    slice.query.positionTolerance = m_config.positionResidualTolerance;
    slice.query.orientationTolerance = m_config.orientationResidualTolerance;
    slice.query.dedupThresholdPerAxis = m_config.ikDedupThresholdPerAxis;
    slice.query.budget = m_config.regionBudget;
    slice.query.configDigest = analysisConfigurationDigest(m_config);

    // ---- 计划投影（逐计划；首个违例即切片失败——受理拒绝）。
    slice.query.plans.reserve(ws.plans.entries.size());
    for (const requirements::SamplingPlan& plan : ws.plans.entries) {
        // regionRef → 区域条目（悬空＝计划引用违约——如实拒绝）。
        const auto regionIt = std::find_if(
            ws.regions.entries.begin(), ws.regions.entries.end(),
            [&plan](const requirements::WorkRegion& r) {
                return r.objectId == plan.regionRef;
            });
        if (regionIt == ws.regions.entries.end()) {
            slice.errorText = "采样计划引用的区域条目不存在（regionRef 悬空）";
            return slice;
        }
        kinematics::SamplingPlan projected;
        if (!projectPlan(plan, *regionIt, projected, slice.errorText)) {
            return slice;  // errorText 已带首个违例原因
        }
        slice.query.plans.push_back(std::move(projected));
    }

    // ---- 工况投影（碰撞要求的工况侧来源——KIN-05：要求在场而缺碰撞
    // 会话→样本 DataInsufficient。**必须**投影而非省略：丢工况＝伪造
    // "无要求"评估语义）。启用∧等级过滤＝域冻结规则（§6.2 必验集合
    // ＝enabled∧Must——本执行器投影全部条目、enabled 原值直投，过滤
    // 语义归域消费侧；投影不裁剪）。
    slice.query.conditions.reserve(ws.conditions.entries.size());
    for (const requirements::OperatingCondition& cond : ws.conditions.entries) {
        kinematics::BatchCondition projected;
        projected.conditionId = cond.objectId;
        projected.level = cond.level == requirements::RequirementLevel::Must
                              ? kinematics::BatchRequirementLevel::Must
                              : kinematics::BatchRequirementLevel::Should;
        projected.enabled = cond.enabled;
        switch (cond.appliesTo.scope) {
            case requirements::AppliesToScope::AllStations:
                projected.appliesTo = kinematics::BatchAppliesToScope::AllStations;
                break;
            case requirements::AppliesToScope::Stations:
                projected.appliesTo = kinematics::BatchAppliesToScope::Stations;
                break;
            case requirements::AppliesToScope::None:
                projected.appliesTo = kinematics::BatchAppliesToScope::None;
                break;
        }
        projected.stations = cond.appliesTo.stations;
        // 工况级要求值直投（RequirementDemand 与 BatchDemands 同构两字段
        // ——碰撞取"或"的合并语义归域消费侧，投影不预合并）。
        projected.demands.collisionFreeRequired = cond.demands.collisionFreeRequired;
        projected.demands.minimumJointMargin = cond.demands.minimumJointMargin;
        slice.query.conditions.push_back(std::move(projected));
    }

    // ---- 评估请求（绑定面最小构造——Preview 会话级语义：零 envelope、
    // 零对账轨；task 五元组生成值仅供任务绑定透传，纯计算面零校验）。
    slice.request.mode = core::EvaluationMode::Preview;
    slice.request.task.project = core::ProjectId::generate();
    slice.request.task.branch = core::BranchId::generate();
    slice.request.task.revision = core::RevisionId::generate();
    slice.request.task.run = core::RunId::generate();
    slice.request.task.attempt = core::AttemptId{1U};  // 尝试序 ≥1（会话级单尝试）
    return slice;
}

// =====================================================================
// 受理与执行（UI 线程受理面＋后台执行＋回投）
// =====================================================================

kinematics::KinChannelBackgroundAck KinEvaluationExecutor::submit(
    const kinematics::KinChannelBackgroundRequest& request)
{
    kinematics::KinChannelBackgroundAck ack;

    // ---- 校验①：kind 分流（本批范围＝区域覆盖通道——另两类执行缝
    // 未装配，reason 写明拒绝归属，不虚构受理，ERR-01）。
    if (request.kind == kinematics::KinChannelBackgroundKind::SessionSolve) {
        ack.reason = "单点求解后台通道未装配（内联求解缝未注入——单点求解"
                     "暂不可用）";
        return ack;
    }
    if (request.kind == kinematics::KinChannelBackgroundKind::TaskPointsBatch) {
        ack.reason = "批量验证执行通道未装配（本批范围＝区域覆盖 L-K6——"
                     "批量通道留后续任务）";
        return ack;
    }

    // ---- 校验②③：快照绑定（缺位/不一致都拒绝——错评比拒绝更糟）。
    if (m_snapshot == nullptr) {
        ack.reason = "结果绑定快照缺位——无已应用模型可评估（先应用建模"
                     "草稿生成发布快照）";
        return ack;
    }
    if (!(request.snapshotId == m_snapshotId)) {
        ack.reason = "结果绑定快照与当前会话不一致（面板会话态滞后于快照"
                     "换绑——重开会话后重试）";
        return ack;
    }

    // ---- 校验④：切片（工作集现取——失败原因透传）。
    CoverageSlice slice = sliceCoverage();
    if (!slice.errorText.empty()) {
        ack.reason = "覆盖评估切片失败：" + slice.errorText;
        return ack;
    }

    // ---- 受理登记（taskRef＝kind 前缀＋纪元＋序号——任务行对齐键）。
    ++m_seq;
    const std::string taskRef = "kin-cov-" + std::to_string(m_epoch) + "-"
                                + std::to_string(m_seq);
    ack.accepted = true;
    ack.reason = "覆盖评估已受理";
    ack.taskRef = taskRef;
    m_tasks.push_back(TaskEntry{taskRef, request.epoch, true, false, ""});

    // ---- 后台执行（视图随闭包 shared_ptr 捕获保证存活——换绑前的
    // 委托值在途隔离；纯计算面零项目写入）。future 必须持有（hpp
    // m_inflight 注——丢弃即 submit 阻塞等完整个评估，违背后台语义）。
    std::shared_ptr<const kinematics::IKinRuntimeView> view = m_snapshot;
    const std::uint64_t epoch = request.epoch;
    // 提交时锚（受理校验已保证 request.snapshotId==m_snapshotId——回投
    // 侧的绑定键取提交时值，迟到任务绝不打上新锚）。
    const core::ContentIdentity snapshotIdAtSubmit = request.snapshotId;
    const core::ContentIdentity configDigestAtSubmit = slice.query.configDigest;
    QObject* routeContext = m_deps.routeContext;
    KinEvaluationExecutor* self = this;
    m_inflight.push_back(
        std::async(std::launch::async, [self, routeContext, view, epoch, taskRef,
                                        snapshotIdAtSubmit, configDigestAtSubmit,
                                        query = std::move(slice.query),
                                        request = slice.request]() {
        // 评估消费换绑前委托（请求结束失效——任务结束随闭包析构；
        // 后台线程只读）。委托空＝受理后解绑的竞态残留——防御拒绝。
        if (view == nullptr) {
            kinematics::KinChannelBackgroundResultNote orphan;
            orphan.acceptedEpoch = epoch;
            orphan.kind = kinematics::KinChannelBackgroundKind::RegionCoverage;
            orphan.interrupted = false;
            orphan.summaryText = "覆盖评估中止：会话视图已解绑";
            QMetaObject::invokeMethod(
                routeContext,
                [self, taskRef, epoch, orphan]() {
                    self->onViewDetached(taskRef, epoch, orphan.summaryText);
                },
                Qt::QueuedConnection);
            return;
        }
        NullEvaluationContext context;
        kinematics::KinChannelCoverageResult result;
        try {
            const kinematics::RegionCoverageComputation computation =
                kinematics::runRegionCoverageComputation(*view, query, request,
                                                         context);
            // ---- 成功：结构化投影（逐样本＋覆盖率标记——域值直投翻译）。
            result.ok = true;
            result.samples.reserve(computation.results.results.size());
            for (const auto& record : computation.results.results) {
                kinematics::KinChannelSampleRecord projected;
                projected.sampleIndex = record.sampleIndex;
                switch (record.state) {
                    case kinematics::SampleState::Reached:
                        projected.state = kinematics::KinChannelSampleState::Reached;
                        break;
                    case kinematics::SampleState::Unreachable:
                        projected.state =
                            kinematics::KinChannelSampleState::Unreachable;
                        break;
                    case kinematics::SampleState::DataInsufficient:
                        projected.state =
                            kinematics::KinChannelSampleState::DataInsufficient;
                        break;
                    case kinematics::SampleState::NotApplicable:
                        projected.state =
                            kinematics::KinChannelSampleState::NotApplicable;
                        break;
                    case kinematics::SampleState::NotRun:
                        projected.state = kinematics::KinChannelSampleState::NotRun;
                        break;
                }
                projected.reason = record.reason;
                result.samples.push_back(std::move(projected));
            }
            const kinematics::CoverageTotals& pos = computation.coverage.position;
            result.position = {pos.planned, pos.reached, pos.unreachable,
                               pos.dataInsufficient, pos.notRun,
                               pos.notApplicable};
            const kinematics::CoverageTotals& ori =
                computation.coverage.orientation;
            result.orientation = {ori.planned, ori.reached, ori.unreachable,
                                  ori.dataInsufficient, ori.notRun,
                                  ori.notApplicable};
            result.positionDefined = computation.coverage.positionDefined;
            result.orientationDefined = computation.coverage.orientationDefined;
            result.downgraded = computation.coverage.downgraded;
            result.incomplete = computation.coverage.incomplete;
        } catch (const std::exception& e) {
            // 装配期 fail-fast 面（视图空/Box 退化/seed 非法/维度违约等
            // ——调用方错误轨）：结构化失败入账（ok=false＋errorText），
            // 不崩溃不静默——失败可见面（ERR-01 同源纪律）。
            result = kinematics::KinChannelCoverageResult{};
            result.ok = false;
            result.errorText = std::string("覆盖评估执行失败：") + e.what();
        }

        // ---- 回投 UI 线程（账面写点/note 投递的线程分格——类注职责链
        // 末段；queued functor 在 receiver 析构后由 Qt 安全丢弃——
        // 执行器与上下文同存活期，无悬垂）。
        QMetaObject::invokeMethod(
            routeContext,
            [self, taskRef, snapshotIdAtSubmit, configDigestAtSubmit, epoch,
             result]() {
                self->onCoverageFinished(taskRef, snapshotIdAtSubmit,
                                         configDigestAtSubmit, epoch, result,
                                         false);
            },
            Qt::QueuedConnection);
    }));

    return ack;
}

void KinEvaluationExecutor::onCoverageFinished(
    const std::string& taskRef, const core::ContentIdentity& snapshotIdAtSubmit,
    const core::ContentIdentity& configDigestAtSubmit, std::uint64_t epoch,
    kinematics::KinChannelCoverageResult result, bool interrupted)
{
    // ---- 在途 future 惰性清理（完成回投即该任务 future 已就绪——
    // get() 立即返回；整批扫描防累积，任务量级＝人工操作频度）。
    for (auto it = m_inflight.begin(); it != m_inflight.end();) {
        if (it->wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            it->get();
            it = m_inflight.erase(it);
        } else {
            ++it;
        }
    }

    // ---- 迟到判定（提交纪元≠当前纪元——会话已换绑/拆除：产物按提交
    // 锚丢弃，绝不打上新锚入账；账目终态与 note 照常——消费侧门面给
    // "迟到丢弃"反馈，L-K12）。
    const bool stale = (epoch != m_epoch);

    // ---- 账目终态（登记序线性查找——任务量级＝人工操作频度，无性能面）。
    const auto it = std::find_if(m_tasks.begin(), m_tasks.end(),
                                 [&taskRef](const TaskEntry& t) {
                                     return t.taskRef == taskRef;
                                 });
    if (it != m_tasks.end()) {
        it->running = false;
        it->interrupted = interrupted;
        it->summary = result.ok ? (std::to_string(result.position.reached) + "/"
                                   + std::to_string(result.position.planned)
                                   + " 点可达")
                                : result.errorText;
    }

    // ---- 结果入账（成功/失败都入——失败可见面；绑定键＝提交时值——
    // 消费方核对 snapshotId 的权威锚；迟到不入账）。
    if (!stale) {
        result.epoch = epoch;
        result.snapshotId = snapshotIdAtSubmit;
        result.configDigest = configDigestAtSubmit;
        m_latestCoverage = std::move(result);
    }

    // ---- 完成通知投递（迟到判定/中断如实在门面消费侧——L-K12；槽
    // 缺位＝仅账面更新，不虚构通知）。
    if (m_deps.resultRoute) {
        kinematics::KinChannelBackgroundResultNote note;
        note.acceptedEpoch = epoch;
        note.kind = kinematics::KinChannelBackgroundKind::RegionCoverage;
        note.interrupted = interrupted;
        note.summaryText = stale
                               ? "迟到结果已按提交锚丢弃（会话已换绑）"
                               : (m_latestCoverage && m_latestCoverage->ok
                                      ? ("覆盖评估完成："
                                         + std::to_string(m_latestCoverage->position.reached)
                                         + "/"
                                         + std::to_string(m_latestCoverage->position.planned)
                                         + " 点可达（位置口径）")
                                      : ("覆盖评估失败："
                                         + (m_latestCoverage
                                                ? m_latestCoverage->errorText
                                                : std::string{"结果未入账"})));
        m_deps.resultRoute(note);
    }
}

void KinEvaluationExecutor::onViewDetached(const std::string& taskRef,
                                           std::uint64_t epoch,
                                           const std::string& reason)
{
    // ---- 在途 future 惰性清理（同 onCoverageFinished——回投拍已就绪）。
    for (auto it = m_inflight.begin(); it != m_inflight.end();) {
        if (it->wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            it->get();
            it = m_inflight.erase(it);
        } else {
            ++it;
        }
    }
    // ---- 账目终态（中止如实——零结果入账：解绑无产物可消费）。
    const auto it = std::find_if(m_tasks.begin(), m_tasks.end(),
                                 [&taskRef](const TaskEntry& t) {
                                     return t.taskRef == taskRef;
                                 });
    if (it != m_tasks.end()) {
        it->running = false;
        it->interrupted = true;  // 中止＝中断终态（NFR-REL-03 如实语义）
        it->summary = reason;
    }
    // ---- 完成通知投递（中止原因透传——迟到判定/中断如实在消费侧）。
    if (m_deps.resultRoute) {
        kinematics::KinChannelBackgroundResultNote note;
        note.acceptedEpoch = epoch;
        note.kind = kinematics::KinChannelBackgroundKind::RegionCoverage;
        note.interrupted = true;
        note.summaryText = reason;
        m_deps.resultRoute(note);
    }
}

std::vector<kinematics::KinChannelTaskStatusRow> KinEvaluationExecutor::taskRows() const
{
    // 任务账目直投（登记序——在途在前完成的在后自然呈现；状态词经 ui
    // 九态词表键——running/completed/failed 三态覆盖本批账面形态）。
    std::vector<kinematics::KinChannelTaskStatusRow> rows;
    rows.reserve(m_tasks.size());
    for (const TaskEntry& t : m_tasks) {
        kinematics::KinChannelTaskStatusRow row;
        row.taskRefText = t.taskRef;
        if (t.running) {
            row.stateLabelKey = "task.state.running";
            row.percent = std::nullopt;  // 进度设施未装配——不伪造百分比
        } else if (t.interrupted) {
            row.stateLabelKey = "task.state.interrupted";
        } else if (t.summary.rfind("覆盖评估失败", 0) == 0) {
            row.stateLabelKey = "task.state.failed";
        } else {
            row.stateLabelKey = "task.state.completed";
        }
        row.interrupted = t.interrupted;
        rows.push_back(std::move(row));
    }
    return rows;
}

}  // namespace ui
}  // namespace ird
}  // namespace sdurws
