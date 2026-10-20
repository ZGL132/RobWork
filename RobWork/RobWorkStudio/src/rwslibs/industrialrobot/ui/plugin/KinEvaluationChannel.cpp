/**
 * @file   KinEvaluationChannel.cpp
 * @brief  kinematics 覆盖评估执行通道的产品装配半区实现（UI-T64——
 *         受理校验/工作集切片/后台执行/结构化结果账面/完成回投）。
 *
 * 设计依据：KinEvaluationChannel.hpp 文件头（职责链/权威边界/线程模型
 * 的权威声明在本 TU 逐段兑现）。
 */

#include "KinEvaluationChannel.hpp"
#include <sdurws/ird/policy/SystemDefaultPolicy.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <utility>

#include <QMetaObject>
#include <rw/math/RPY.hpp>  // 构造参数顺序为yaw/pitch/roll，区别于需求字段顺序

#include <sdurws/ird/kinematics/AnalysisConfig.hpp>  // analysisConfigurationDigest（受理对齐键）
#include <sdurws/ird/kinematics/Evaluators.hpp>      // runRegionCoverageComputation/RegionCoverageQuery
#include <sdurws/ird/kinematics/KinTypes.hpp>        // TcpRef/BatchDemands
#include <sdurws/ird/requirements/RequirementTypes.hpp>  // WorkRegion/SamplingPlan/RequirementReference
#include <sdurws/ird/requirements/Sampling.hpp>      // ISamplingPlanBuilder（计划摘要域函数——零复制）
#include <sdurws/ird/runtime/BaseWorldTransform.hpp>  // invertWorldBase（§6.1 权威反解纯函数——
                                                      // 审查 A01 修复：T_base_world 唯一来源，
                                                      // ui 零第二套基座变换代数）
#include <sdurws/ird/runtime/CanonicalModel.hpp>     // CanonicalModel（referenceQ/defaultTcp 提取）

#include <exception>

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
    : m_policy(std::make_shared<const policy::EngineeringPolicySet>(policy::makeSystemDefaultPolicySet())),
      m_deps(std::move(deps))
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
    m_latestSolve.reset(); // A-23：求解结果与覆盖/批量一致，策略/快照换绑必须同拍清账。
    // ★ KIN-P4：批量账面同拍清空（与覆盖账面同一换绑纪律）＋在途任务
    // 协作取消置位（真实取消路径——SessionSolve 求解探针/批量批间查询
    // 消费后早停返回取消态，非结局如实呈现；替换全部标志为新代——
    // 旧任务闭包持旧 shared_ptr，置位对其可见）。
    m_latestBatch.clear();
    m_latestBatchSnapshotId.reset();
    for (const std::shared_ptr<std::atomic_bool>& flag : m_cancelFlags) {
        flag->store(true, std::memory_order_release);
    }
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

/**
 * @brief 可取消评估上下文（★ KIN-P4——任务级协作取消标志的上下文载体；
 *        与 NullEvaluationContext 的差异：cancellationRequested 从任务级
 *        atomic 标志读取——换绑/解绑置位后批间查询与逐样本探针消费，
 *        任务早停返回 NotRun 残余，真实可取消；reportProgress 把域计算
 *        里程碑单调落值到任务级进度原子——taskRows 投影消费（MA-03 接
 *        线：此前进度被吞＝任务行永远无进度呈现））。
 *
 * 线程约束：cancellationRequested/reportProgress 均在后台评估线程被
 * 周期调用（atomic 读/写）；构造/析构在任务闭包栈——标志与进度原子的
 * shared_ptr 随闭包与 TaskEntry 双侧存活。
 */
class CancelableEvaluationContext final : public evidence::IEvaluationContext {
public:
    /// @param flag     [in] 任务级取消标志（submit 拍创建——换绑/解绑置位）。
    /// @param progress [in] 任务级进度原子（可空——单点求解无进度源）。
    explicit CancelableEvaluationContext(
        std::shared_ptr<std::atomic_bool> flag,
        std::shared_ptr<std::atomic_uint8_t> progress = nullptr)
        : m_flag(std::move(flag)), m_progress(std::move(progress))
    {
    }
    bool cancellationRequested() const override
    {
        return m_flag->load(std::memory_order_acquire);
    }
    void reportProgress(std::uint8_t percent, std::string_view phase) override
    {
        (void)phase;  // 阶段词表归域（solve-region 等）——任务行只取百分比
        // 单调化落值（域按完成数计百分比单调；取 max 防御并行分片的
        // 乱序报告——relaxed 竞争无害，取大者即真实进度）。
        if (m_progress != nullptr) {
            const auto current = m_progress->load(std::memory_order_relaxed);
            if (percent > current) {
                m_progress->store(percent, std::memory_order_relaxed);
            }
        }
    }
    std::optional<std::vector<std::uint8_t>> tryObjectBytes(
        core::ObjectId objectId, core::ContentVersion contentVersion) const override
    {
        (void)objectId;
        (void)contentVersion;
        return std::nullopt;  // 纯计算面零物化读取（NullEvaluationContext 同则）
    }

private:
    /// 任务级取消标志（submit 拍创建——换绑/解绑置位；共享所有权限定
    /// 在任务闭包与执行器成员表之间）。
    std::shared_ptr<std::atomic_bool> m_flag;
    /// 任务级进度原子（可空——跨线程落点语义见 TaskEntry::progress 注）。
    std::shared_ptr<std::atomic_uint8_t> m_progress;
};

bool KinEvaluationExecutor::projectRefFrameToBase(
    const requirements::RequirementReference& refFrame,
    rw::math::Transform3D<double>& baseTref, std::string& errorText) const
{
    // ---- 第一步：参考系解析（refFrame 系 → 基座系 {B}）。
    // kinematics RegionBox/BatchTaskPoint 的契约是"宿主已把 refFrame 系
    // 解析到基座系"（§5.1 坐标纪律）——本执行器就是这里的"宿主"。
    // World 缺省＝恒等变换（不依赖解析缝）；ModelFrame 引用经解析缝取
    // 宿主帧世界位姿。（★ KIN-P4：本函数自 projectPlan 抽出——批量
    // 任务点切片共用同一参考系代数，零第二份实现。）
    rw::math::Transform3D<double> worldTref =
        rw::math::Transform3D<double>::identity();
    if (refFrame.kind == requirements::RequirementRefKind::ModelFrame
        || refFrame.kind == requirements::RequirementRefKind::SceneObject) {
        // REQ-01/09：夹具/零件也是既有可选参考系，按同一ObjectId名称端口
        // 解析到已发布帧；不能在需求预览支持SceneObject而评估切片中漏掉它。
        if (!m_deps.resolveFrame || !refFrame.objectId
            || !refFrame.objectId->isValid()) {
            errorText = "参考系不可解析（引用缺载荷或解析缝未装配）";
            return false;
        }
        const std::optional<rw::math::Transform3D<double>> resolved =
            m_deps.resolveFrame(*refFrame.objectId);
        if (!resolved.has_value()) {
            // 帧不可解析＝投影失败诚实呈现（与需求三维投影同款语义——
            // 不虚构几何）。
            errorText = "参考系在宿主场景中不可解析";
            return false;
        }
        worldTref = *resolved;
    } else if (refFrame.kind != requirements::RequirementRefKind::World) {
        errorText = "参考系种类不可用于位姿参考（支持 World/ModelFrame/SceneObject）";
        return false;
    }

    // ---- 第二步：参考系→基座系合成（T_base_ref＝T_base_world·T_world_ref）。
    // ★ 审查条目 A01 修复（KIN-P1）：runtime 权威读取点 worldToBase() 的
    // **值语义是 T_world_base**（基座相对世界的位姿——Snapshot.hpp §6.4
    // 行"§9.1 唯一存储在 CanonicalModel.world"；接口名中的 world→base 指
    // "世界系中看基座"，不是"世界坐标→基座坐标"的换算阵）。宿主此处要把
    // refFrame 系（世界系位姿 worldTref）解析到基座系，需要其逆
    // T_base_world：按 runtime §6.3"不新增第二套基座变换规则"，经
    // BaseWorldTransform 的权威反解纯函数 invertWorldBase 取得（正交旋转
    // 转置实现）——不在 ui 另造变换真值。
    //
    // 错误语义：invertWorldBase 对非正交旋转 fail-fast（RuntimeError——
    // 调用方契约违约）。产品路径的 T_world_base 已过编译链 S5 合法域校验
    // （正交性/有限性），本异常不可达；测试替身若注入非法旋转，此处转译
    // 为切片失败如实拒绝（不静默、也不让异常穿越受理面——ERR-01 同源
    // 纪律：拒绝 reason 带根因）。
    rw::math::Transform3D<double> tBaseWorld;
    try {
        tBaseWorld = runtime::invertWorldBase(m_snapshot->worldToBase());
    } catch (const std::exception& e) {
        errorText = std::string("基座—世界变换反解失败（T_world_base 非法"
                                "——非正交旋转）：") + e.what();
        return false;
    }
    // 复合读法（core §4.6）：T_ac＝T_ab·T_bc——refFrame 系在基座系的表达
    // ＝(基座系看世界)·(世界系看 refFrame)。平移单位 m、旋转无量纲。
    baseTref = tBaseWorld * worldTref;
    return true;
}

bool KinEvaluationExecutor::projectPlan(const requirements::SamplingPlan& plan,
                                        const requirements::WorkRegion& region,
                                        kinematics::SamplingPlan& out,
                                        std::string& errorText) const
{
    // ---- 第一~二步：参考系解析与基座系合成（★ KIN-P4 抽出至
    // projectRefFrameToBase——批量任务点切片共用同一参考系代数；
    // 失败 reason 原样透传）。
    rw::math::Transform3D<double> baseTref;
    if (!projectRefFrameToBase(region.refFrame, baseTref, errorText)) {
        return false;
    }

    // A-21/REQ-03：盒局部边长保持原值，参考系旋转作为纯值进入采样内核。
    // 任意合法刚体旋转均精确消费；包围盒会改变样本位置和覆盖率，禁止替代。
    out.box.center = baseTref.P() + baseTref.R() * region.box.center;
    out.box.size = region.box.size;
    out.box.orientationInBase = baseTref.R();

    // ---- 第五步：采样定义投影（域值→域投影型——字段搬运零换算）。
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

    // ---- 第六步：计划内容身份（域函数现算——零复制纪律；CON-05 禁
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
    // A-21/REQ-01：区域独立TCP与工位使用同一解析权威，只传工具ID和键。
    // 在受理线程拒绝悬空/未知键并定位区域；后台不会静默换成默认TCP。
    if (region.tcpRef && region.tcpRef->kind != requirements::RequirementRefKind::DefaultTcp) {
        const auto& ref = *region.tcpRef;
        if (!ref.wellFormed() || ref.kind != requirements::RequirementRefKind::Tool || !ref.objectId) {
            errorText = "区域“" + region.name + "”的TCP引用非法，请重新选择工具和TCP";
            return false;
        }
        const auto& tools = m_snapshot->model().tools();
        const auto tool = std::find_if(tools.begin(), tools.end(), [&](const auto& item) {
            return item.objectId == *ref.objectId;
        });
        if (tool == tools.end() || (!ref.tcpKey.empty() && ref.tcpKey != tool->localName
            && !std::any_of(tool->namedTcpOffsets.begin(), tool->namedTcpOffsets.end(),
                [&](const auto& entry) { return entry.first == ref.tcpKey; }))) {
            errorText = "区域“" + region.name + "”的工具/TCP未在当前模型解析，请重新选择TCP";
            return false;
        }
        out.tcpOverride = kinematics::TcpRef{*ref.objectId, ref.tcpKey};
    }
    out.regionObjectId = region.objectId;
    out.demands.collisionFreeRequired = region.demands.collisionFreeRequired;
    out.demands.minimumJointMargin = region.demands.minimumJointMargin;
    try {
        // 受理前统一核对完整计划新增字段；错误必须带业务名、原因与建议动作。
        (void)kinematics::sampleSetIdentity(out, m_config.regionBudget);
    } catch (const std::invalid_argument& e) {
        errorText = "区域“" + region.name + "”参考系/TCP非法：" + e.what()
            + "；请修正该区域引用后重试";
        return false;
    }
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

    // ---- 校验②③：快照绑定（缺位/不一致都拒绝——错评比拒绝更糟）。
    // ★ KIN-P4：三类 kind 共用此前置（原"校验①kind 分流拒绝"已随两
    // 通道真实分发退役——审查报告 §4 第二条"对 SessionSolve 与
    // TaskPointsBatch 的拒绝改为真实分发"）。
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

    // ---- 受理登记与后台派发的 kind 分流（每分支自完成切片/装配校验
    // ——失败 reason 带首个违例，ERR-01 不虚构受理）。
    ++m_seq;
    const auto cancelFlag =
        std::make_shared<std::atomic_bool>(false);  // 本任务协作取消标志
    m_cancelFlags.push_back(cancelFlag);

    if (request.kind == kinematics::KinChannelBackgroundKind::SessionSolve) {
        // ====== KIN-P4：单点求解后台通道（真实分发）======
        // ---- 校验④：目标载荷（通道请求的 solveTargetInBase——面板
        // 转后台时随载荷携带；缺失＝无法还原"求解什么"，如实拒绝）。
        if (!request.solveTargetInBase.has_value()) {
            --m_seq;  // 未受理——序号回退（防 taskRef 断档账面空行）
            m_cancelFlags.pop_back();
            ack.reason = "单点求解请求缺目标位姿载荷（转后台流未携带"
                         " solveTargetInBase——装配违约）";
            return ack;
        }
        // ---- 装配：完整合法请求（TCP 默认工具解析/intervals/零位参考
        // 构型/配置初值——与面板内联装配同语义；ui 执行器不可消费插件
        // 私有头，按同一 KIN-14/KIN-13 规则独立落位，注释互链）。
        SolveSlice slice = assembleSolveSlice(*request.solveTargetInBase);
        if (!slice.errorText.empty()) {
            --m_seq;
            m_cancelFlags.pop_back();
            ack.reason = "单点求解装配失败：" + slice.errorText;
            return ack;
        }
        const std::string taskRef = "kin-solve-" + std::to_string(m_epoch)
                                    + "-" + std::to_string(m_seq);
        ack.accepted = true;
        ack.reason = "单点求解已转后台执行";
        ack.taskRef = taskRef;
        m_tasks.push_back(TaskEntry{taskRef, request.epoch, true, false, ""});
        launchSolveTask(taskRef, request.epoch, request.snapshotId,
                        slice.configDigest, std::move(slice.request),
                        cancelFlag);
        return ack;
    }

    if (request.kind == kinematics::KinChannelBackgroundKind::TaskPointsBatch) {
        // ====== KIN-P4：批量验证执行通道（真实分发）======
        // ---- 校验④：工作集切片（任务点投影——失败原因透传）。
        BatchSlice slice = sliceBatch();
        if (!slice.errorText.empty()) {
            --m_seq;
            m_cancelFlags.pop_back();
            ack.reason = "批量验证切片失败：" + slice.errorText;
            return ack;
        }
        const std::string taskRef = "kin-batch-" + std::to_string(m_epoch)
                                    + "-" + std::to_string(m_seq);
        ack.accepted = true;
        ack.reason = "批量验证已受理（后台执行——结果见任务点表）";
        ack.taskRef = taskRef;
        m_tasks.push_back(TaskEntry{taskRef, request.epoch, true, false, ""});
        launchBatchTask(taskRef, request.epoch, request.snapshotId,
                        std::move(slice.query), std::move(slice.request),
                        cancelFlag);
        return ack;
    }

    // ====== RegionCoverage：UI-T64 既有通道（区域覆盖——切片不变）。
    CoverageSlice slice = sliceCoverage();
    if (!slice.errorText.empty()) {
        --m_seq;
        m_cancelFlags.pop_back();
        ack.reason = "覆盖评估切片失败：" + slice.errorText;
        return ack;
    }

    // ---- 受理登记（taskRef＝kind 前缀＋纪元＋序号——任务行对齐键）。
    // MA-03：进度原子随受理创建并入账——后台 reportProgress 单调落值，
    // taskRows 在 UI 线程读取（免锁跨线程最小共享面）。
    const std::string taskRef = "kin-cov-" + std::to_string(m_epoch) + "-"
                                + std::to_string(m_seq);
    ack.accepted = true;
    ack.reason = "覆盖评估已受理";
    ack.taskRef = taskRef;
    auto progressFlag = std::make_shared<std::atomic_uint8_t>(0);
    m_tasks.push_back(TaskEntry{taskRef, request.epoch, true, false, "",
                                progressFlag});

    // ---- 后台派发（闭包组装唯一书写点——launchCoverageTask；future
    // 必须持有（hpp m_inflight 注——丢弃即 submit 阻塞等完整个评估，
    // 违背后台语义）。提交时锚（受理校验已保证 request.snapshotId==
    // m_snapshotId——回投侧的绑定键取提交时值，迟到任务绝不打上新锚）。
    launchCoverageTask(taskRef, request.epoch, request.snapshotId,
                       slice.query.configDigest, std::move(slice.query),
                       std::move(slice.request), cancelFlag, progressFlag);

    return ack;
}

// =====================================================================
// KIN-P4 后台派发三通道（闭包组装——视图随 shared_ptr 捕获保证存活；
// 取消标志随闭包只读消费；回投经 QMetaObject 回 UI 线程）
// =====================================================================

namespace {
/** @brief 缺失碰撞设施的端口应答；不是碰撞算法，任何构型均保持证据不足。 */
class MissingPolicyCollisionSession final : public kinematics::IKinCollisionSession {
public:
    /** @brief q[in]为rad/m构型；设施未装配返回EvidenceMissing，不给无碰撞结论。 */
    kinematics::IkCollisionVerdict evaluate(const std::vector<double>&) const override
    {
        kinematics::IkCollisionVerdict result;
        result.state=kinematics::IkCollisionEvaluationState::EvidenceMissing;
        result.statusDetail="当前策略要求碰撞检查，但宿主碰撞设施未装配";
        return result;
    }
};
}

namespace {
/** @brief 同步/后台使用相同碰撞不足语义的L5求解适配器；宿主持有，仅UI线程调用。 */
class PolicyAwareIkSolver final : public kinematics::IIkSolver {
public:
    /** @brief source[in]返回已发布策略值；不拥有project，不回查存储。 */
    explicit PolicyAwareIkSolver(std::function<std::shared_ptr<const policy::EngineeringPolicySet>()> source)
        : m_source(std::move(source)) {}
    /** @brief request[in]原请求不改；缺设施经端口返回EvidenceMissing后交真实求解器消费。 */
    kinematics::IkOutcome solve(const kinematics::IkRequest& request) const override
    {
        const auto policy=m_source();
        auto bound=request;
        MissingPolicyCollisionSession missing;
        if (!policy || policy->collision.enabled) { bound.collisionSession=&missing; }
        return kinematics::IkSolver{}.solve(bound);
    }
private:
    std::function<std::shared_ptr<const policy::EngineeringPolicySet>()> m_source; ///< 宿主存活期内调用。
};
}
std::unique_ptr<kinematics::IIkSolver> makePolicyAwareIkSolver(
    std::function<std::shared_ptr<const policy::EngineeringPolicySet>()> source)
{
    return std::make_unique<PolicyAwareIkSolver>(std::move(source));
}

void KinEvaluationExecutor::launchCoverageTask(
    const std::string& taskRef, std::uint64_t epoch,
    const core::ContentIdentity& snapshotIdAtSubmit,
    const core::ContentIdentity& configDigestAtSubmit,
    kinematics::RegionCoverageQuery query,
    evidence::EvaluationRequest request,
    const std::shared_ptr<std::atomic_bool>& cancelFlag,
    const std::shared_ptr<std::atomic_uint8_t>& progressFlag)
{
    std::shared_ptr<const kinematics::IKinRuntimeView> view = m_snapshot;
    const auto policyAtSubmit=m_policy; // UI受理拍捕获策略及其contentIdentity，后台不回查项目。
    QObject* routeContext = m_deps.routeContext;
    KinEvaluationExecutor* self = this;
    m_inflight.push_back(
        std::async(std::launch::async,
                   [self, routeContext, view, policyAtSubmit, epoch, taskRef, cancelFlag,
                    progressFlag,
                    snapshotIdAtSubmit, configDigestAtSubmit,
                    query = std::move(query),
                    request = std::move(request)]() mutable {
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
        MissingPolicyCollisionSession missingCollision;
        if (policyAtSubmit && policyAtSubmit->collision.enabled) { query.collisionSession=&missingCollision; }
        if (!policyAtSubmit) { query.collisionSession=&missingCollision; }
        // MA-03（主线功能审计 2026-10-21）：此前此处是 NullEvaluationContext
        // ——计算不消费取消（cancelFlag 只挡迟到结果，大型采样计算本身
        // 不早停）、进度回报被吞（任务行永远无百分比）。接通可取消上下文：
        // 批间查询＋逐样本探针消费 cancelFlag，runRegionCoverageComputation
        // 停止派发剩余批次（NotRun 残余）；reportProgress 单调落值。
        CancelableEvaluationContext context{cancelFlag, progressFlag};
        kinematics::KinChannelCoverageResult result;
        try {
            const kinematics::RegionCoverageComputation computation =
                kinematics::runRegionCoverageComputation(*view, query, request,
                                                         context);
            // ---- 成功：结构化投影（逐样本＋覆盖率标记——域值直投翻译）。
            result.ok = true;
            result.samples.reserve(computation.results.results.size());
            // ---- 逐样本投影（状态五值翻译＋坐标/区域锚直投——UI-T65：
            // results 与 samples 按 sampleIndex 双射对齐（computeCoverage
            // 保证），同下标取坐标零查找）。
            for (std::size_t i = 0; i < computation.results.results.size(); ++i) {
                const auto& record = computation.results.results[i];
                const auto& sample = computation.samples.samples[i];
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
                projected.position = sample.position;      // 基座系 m（直投）
                projected.regionObjectId = sample.regionObjectId;  // 区域锚
                // 样本类别直投（UI-T65 返工——双口径判定键：消费侧按
                // kind 过滤复算位置轴计数，与域 computeCoverage 同定义）。
                projected.kind = sample.kind == kinematics::SampleKind::Pose
                                     ? kinematics::KinChannelSampleKind::Pose
                                     : kinematics::KinChannelSampleKind::Position;
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
        // 执行器与上下文同存活期，无悬垂）。取消（MA-03 接通后计算真实
        // 早停——NotRun 残余＋incomplete=true）按"中断"轨入账而非伪装
        // 成一次完整覆盖：cancelled 读 acquire 与计算探针同序，置位即
        // 至少一批未派发。
        const bool cancelledNow = cancelFlag->load(std::memory_order_acquire);
        QMetaObject::invokeMethod(
            routeContext,
            [self, taskRef, snapshotIdAtSubmit, configDigestAtSubmit, epoch,
             result, cancelledNow]() {
                self->onCoverageFinished(taskRef, snapshotIdAtSubmit,
                                         configDigestAtSubmit, epoch, result,
                                         cancelledNow);
            },
            Qt::QueuedConnection);
    }));
}

void KinEvaluationExecutor::launchSolveTask(
    const std::string& taskRef, std::uint64_t epoch,
    const core::ContentIdentity& snapshotIdAtSubmit,
    const core::ContentIdentity& configDigestAtSubmit,
    kinematics::IkRequest request,
    const std::shared_ptr<std::atomic_bool>& cancelFlag)
{
    std::shared_ptr<const kinematics::IKinRuntimeView> view = m_snapshot;
    const auto policyAtSubmit=m_policy; // UI受理拍捕获策略及其contentIdentity，后台不回查项目。
    QObject* routeContext = m_deps.routeContext;
    // 求解器（Deps 注入可空——空＝内置 IkSolver 生产实例；无状态可共享，
    // 静态存储期——后台线程安全消费）。
    static const kinematics::IkSolver kBuiltInSolver;
    const kinematics::IIkSolver* solver =
        m_deps.sessionSolver != nullptr ? m_deps.sessionSolver : &kBuiltInSolver;
    KinEvaluationExecutor* self = this;
    m_inflight.push_back(
        std::async(std::launch::async,
                   [self, routeContext, view, policyAtSubmit, epoch, taskRef, cancelFlag,
                    snapshotIdAtSubmit, configDigestAtSubmit, solver,
                    request]() mutable {
        kinematics::KinChannelSolveResult result;
        result.snapshotId = snapshotIdAtSubmit;
        result.epoch = epoch;
        result.configDigest = configDigestAtSubmit;

        // ---- 视图绑定与取消探针接线（请求级真值来源＝换绑前委托——
        // 与覆盖通道同存活期纪律；取消探针＝任务级 atomic 标志，换绑/
        // 解绑置位后求解在下一探针点早停——真实可取消路径，NFR-PERF-01
        // ">1 s 转后台"的响应性闭环）。
        if (view == nullptr) {
            result.ok = false;
            result.errorText = "会话视图已解绑（受理后换绑的竞态残留）";
        } else {
            kinematics::IkRequest bound = request;
            bound.modelView = view.get();
            MissingPolicyCollisionSession missingCollision;
            if (!policyAtSubmit || policyAtSubmit->collision.enabled) { bound.collisionSession=&missingCollision; }
            bound.cancellationProbe = [cancelFlag] {
                return cancelFlag->load(std::memory_order_acquire);
            };
            // ---- 真实求解（域求解器纯函数——五类结局/取消/设施失败
            // 三轨读法与内联路径同序：先取消、再设施失败、最后结局）。
            const kinematics::IkOutcome outcome = solver->solve(bound);
            if (outcome.cancelled) {
                // 取消：非结局（§5.4）——如实标记，零解集入账。
                result.cancelled = true;
                result.ok = false;
                result.errorText = "求解已取消（会话已换绑/拆除）";
                result.summaryText = result.errorText;
            } else if (outcome.facilityFailure.has_value()) {
                result.ok = false;
                result.errorText = "求解设施失败（非结局——求解中途 FK "
                                   "复算失败，详见评估诊断）";
                result.summaryText = result.errorText;
            } else if (outcome.collisionEvidenceMissing) {
                // 几何解允许保留诊断，但缺碰撞证据不能通过ok通道进入会话可行结果。
                result.ok = false;
                result.errorText = "数据不足：当前策略要求碰撞检查，但碰撞证据/设施缺失";
                result.summaryText = result.errorText;
                result.solutionSet = std::make_shared<const kinematics::IkSolutionSet>(outcome.solutionSet);
            } else {
                result.ok = true;
                // ---- 解集原值随结果投递（shared_ptr 值转移——宿主闭包
                // 经门面 applySessionSolveOutcome 直投会话态，零再加工）。
                result.solutionSet =
                    std::make_shared<const kinematics::IkSolutionSet>(
                        std::move(outcome.solutionSet));
                result.summaryText =
                    "后台求解完成：解 "
                    + std::to_string(result.solutionSet->solutions.size())
                    + " 个";
            }
        }

        // ---- 回投 UI 线程（结构化结果入账＋槽投递＋note——onSolveFinished）。
        QMetaObject::invokeMethod(
            routeContext,
            [self, taskRef, result]() {
                self->onSolveFinished(taskRef, result.snapshotId,
                                      result.configDigest, result.epoch,
                                      result, false);
            },
            Qt::QueuedConnection);
    }));
}

void KinEvaluationExecutor::launchBatchTask(
    const std::string& taskRef, std::uint64_t epoch,
    const core::ContentIdentity& snapshotIdAtSubmit,
    kinematics::BatchQuery query,
    evidence::EvaluationRequest request,
    const std::shared_ptr<std::atomic_bool>& cancelFlag)
{
    std::shared_ptr<const kinematics::IKinRuntimeView> view = m_snapshot;
    const auto policyAtSubmit=m_policy; // UI受理拍捕获策略及其contentIdentity，后台不回查项目。
    QObject* routeContext = m_deps.routeContext;
    KinEvaluationExecutor* self = this;
    m_inflight.push_back(
        std::async(std::launch::async,
                   [self, routeContext, view, policyAtSubmit, epoch, taskRef, cancelFlag,
                    snapshotIdAtSubmit, query = std::move(query),
                    request = std::move(request)]() mutable {
        // ---- 视图空＝受理后解绑的竞态残留——中止回投（与覆盖通道同轨）。
        if (view == nullptr) {
            QMetaObject::invokeMethod(
                routeContext,
                [self, taskRef, epoch]() {
                    self->onViewDetached(taskRef, epoch,
                                         "批量验证中止：会话视图已解绑");
                },
                Qt::QueuedConnection);
            return;
        }
        // ---- 可取消上下文（★ KIN-P4——任务级取消标志接批间查询与逐项
        // 求解探针：换绑/解绑置位后批量早停，未派发项如实 NotRun）。
        MissingPolicyCollisionSession missingCollision;
        if (!policyAtSubmit || policyAtSubmit->collision.enabled) { query.collisionSession=&missingCollision; }
        CancelableEvaluationContext context(cancelFlag);
        std::optional<kinematics::BatchComputation> computation;
        std::string errorText;
        try {
            // ---- 批量纯计算面（展开→全序→分批→逐批求解→完成矩阵）。
            query.solver = nullptr;  // 生产路径——内置 IkSolver（域默认）
            computation =
                kinematics::runBatchComputation(*view, query, request, context);
        } catch (const std::exception& e) {
            // 装配期 fail-fast 面（投影值校验拒绝——视图空/维度违约等）：
            // 结构化失败入账（不崩溃不静默——ERR-01）。
            errorText = std::string("批量验证执行失败：") + e.what();
        }

        // ---- 回投 UI 线程（逐点合账＋note）。
        QMetaObject::invokeMethod(
            routeContext,
            [self, taskRef, snapshotIdAtSubmit, epoch, computation,
             errorText]() {
                if (!errorText.empty()) {
                    // 执行失败：账目终态＋note 如实（零逐点入账）。
                    self->onBatchFailed(taskRef, snapshotIdAtSubmit, epoch,
                                        errorText);
                    return;
                }
                self->recordBatchOutcomes(*computation, snapshotIdAtSubmit);
                // 任务账目终态＋note 投递（复用求解完成面——kind 载荷
                // 差异只在 summary；批量摘要带逐点统计）。
                self->finishBatchTask(taskRef, snapshotIdAtSubmit, epoch,
                                      *computation);
            },
            Qt::QueuedConnection);
    }));
}

// =====================================================================
// KIN-P4 单点求解切片与批量切片（UI 线程受理期——域投影零语义复判）
// =====================================================================

bool KinEvaluationExecutor::resolveDefaultTcp(kinematics::TcpRef& out,
                                              std::string& errorText) const
{
    // ---- KIN-14 默认 TCP 权威解析（模型快照 tools/defaultTcpIndex →
    // TcpRef{toolObject, 空键}；空键明确选默认偏移，防实际键与工具名碰撞。与面板侧
    // resolveSessionTcp 同语义——ui 执行器不可消费插件私有头（R-2），
    // 按同一 KIN-14 规则独立落位（两处注释互链；解析规则权威在
    // KinTypes.hpp TcpRef 注，非本执行器私设）。
    const runtime::CanonicalModel& model = m_snapshot->model();
    const std::vector<runtime::CanonicalTool>& tools = model.tools();
    const std::optional<std::uint32_t>& defaultIdx = model.defaultTcpIndex();
    if (tools.empty() || !defaultIdx.has_value()
        || *defaultIdx >= tools.size()) {
        errorText = "TCP 未解析：快照无默认工具（KIN-NO-TCP）";
        return false;
    }
    const runtime::CanonicalTool& tool = tools.at(*defaultIdx);
    out.toolObject = tool.objectId;
    out.tcpKey.clear();  // A-15：查询级默认必须用空键，工具名可能恰为另一个实际TCP键。
    return true;
}

KinEvaluationExecutor::SolveSlice KinEvaluationExecutor::assembleSolveSlice(
    const rw::math::Transform3D<double>& targetInBase) const
{
    SolveSlice slice;
    const runtime::CanonicalModel& model = m_snapshot->model();

    // ---- 步骤 1：TCP 默认工具解析（三入口统一语义——KIN-P4）。
    kinematics::TcpRef tcp;
    if (!resolveDefaultTcp(tcp, slice.errorText)) {
        return slice;
    }
    slice.request.tcp = tcp;

    // ---- 步骤 2：配置前置校验（I-KIN-4——非法配置流初值物化属"输出
    // 无定义"契约面；显式拒绝不静默，NFR-COR-03）。
    try {
        kinematics::validateAnalysisConfiguration(m_config);
    } catch (const std::invalid_argument& e) {
        slice.errorText = std::string("求解配置非法（KIN-CONFIG-ILLEGAL）：")
                          + e.what();
        return slice;
    }

    // ---- 步骤 3：逐轴评价区间（域设施唯一实现点——与求解器限位过滤
    // 同源，零第二套真值）。
    const std::vector<kinematics::JointInterval> intervals =
        kinematics::evaluationIntervals(*m_snapshot);

    // ---- 步骤 4：参考构型＝模型链零位（确定性投影——后台执行器不读
    // 会话姿态：会话姿态真值在 kinematics 模块会话态（UI 线程载体），
    // 后台任务零共享可变态；零位是 D-KIN-4 显式输入的确定性落位，与
    // 覆盖切片的 referenceQ 同口径）。
    std::size_t dof = 0;
    for (const runtime::CanonicalJoint& j : model.chain().joints) {
        if (j.type != runtime::JointType::Fixed) {
            ++dof;
        }
    }
    slice.request.referenceQ.assign(dof, 0.0);  // rad|m（链序）

    // ---- 步骤 5：初值集物化（配置策略×seed——§3.4 随机性唯一来源）。
    slice.request.initialValues = kinematics::makeInitialValues(
        m_config.initialStrategy, m_config.initialValuesCount, m_config.seed,
        intervals, slice.request.referenceQ);

    // ---- 步骤 6：配置字段直传＋目标位姿（基座系 {B}——面板转后台时
    // 随载荷携带的现值）。
    slice.request.targetInBase = targetInBase;
    slice.request.positionTolerance = m_config.positionResidualTolerance;      // m
    slice.request.orientationTolerance = m_config.orientationResidualTolerance; // rad
    slice.request.iterationLimit = m_config.iterationLimit;
    slice.request.dedupThresholdPerAxis = m_config.ikDedupThresholdPerAxis;    // rad|m
    slice.request.intervals = intervals;

    // ---- 结果身份（§5.6 绑定面——snapshotId/configDigest 入账锚；
    // mode＝Verified？否——会话级求解不写 results，身份模式随域缺省
    // Verified 承载求解参数面，账面消费不产 envelope（诚实边界同覆盖
    // 通道 Preview 语义：本通道零归档）。sliceId 空（单点会话零切片）。
    slice.request.requestIdentity.snapshotId = m_snapshotId;
    slice.request.requestIdentity.configDigest =
        kinematics::analysisConfigurationDigest(m_config);
    slice.request.requestIdentity.seed = m_config.seed;
    slice.request.requestIdentity.referenceQ = slice.request.referenceQ;
    slice.configDigest = slice.request.requestIdentity.configDigest;
    return slice;
}

KinEvaluationExecutor::BatchSlice KinEvaluationExecutor::sliceBatch() const
{
    BatchSlice slice;
    if (m_deps.editor == nullptr) {
        slice.errorText = "需求工作集未装配";
        return slice;
    }

    // ---- 工作集现取（UI 线程——编辑器线程纪律；值拷贝随任务闭包走）。
    const requirements::RequirementWorkingSet& ws = m_deps.editor->workingSet();
    if (ws.points.entries.empty()) {
        slice.errorText = "工作集无任务点（req-point-set 空）";
        return slice;
    }

    // ---- 参考构型：模型链零位（确定性投影——D-KIN-4 显式输入；与
    // 覆盖切片同口径）。
    const runtime::CanonicalModel& model = m_snapshot->model();
    for (const auto& joint : model.chain().joints) {
        slice.query.referenceQ.push_back(joint.zeroOffset);  // rad|m（链序）
    }

    // ---- defaultTcp：模型默认 TCP（KIN-14——解析失败＝受理拒绝，与
    // 单点通道同口径：批量验证的默认 TCP 是查询级承载，缺失即无法求解）。
    kinematics::TcpRef tcp;
    if (!resolveDefaultTcp(tcp, slice.errorText)) {
        slice.errorText = "批量验证切片失败：" + slice.errorText;
        return slice;
    }
    slice.query.defaultTcp = tcp;

    // ---- 求解配置七参数直投（同覆盖切片——T10 前直传投影）。
    slice.query.initialStrategy = m_config.initialStrategy;
    slice.query.initialValuesCount = m_config.initialValuesCount;
    slice.query.iterationLimit = m_config.iterationLimit;
    slice.query.dedupThresholdPerAxis = m_config.ikDedupThresholdPerAxis;
    slice.query.seed = m_config.seed;
    slice.query.threadCount = m_config.regionBudget.threadCount;
    slice.query.configDigest = kinematics::analysisConfigurationDigest(m_config);

    // ---- 任务点投影（逐点；参考系解析与容差/要求值直投——零语义复判）。
    // 启用与停用条目都投影（停用点产 NotApplicable 显式标记——域 §7.1）。
    slice.query.points.reserve(ws.points.entries.size());
    for (const requirements::TaskPoint& point : ws.points.entries) {
        kinematics::BatchTaskPoint projected;
        projected.pointOid = point.objectId;
        projected.level = point.level == requirements::RequirementLevel::Must
                              ? kinematics::BatchRequirementLevel::Must
                              : kinematics::BatchRequirementLevel::Should;
        projected.enabled = point.enabled;
        // A-15/REQ-01：仅投影ObjectId＋原tcpKey，变换真值仍由同一runtime快照提供。
        // 深度引用校验在受理线程完成，防后台异常被误呈现为“不可达”。
        if (point.tcpRef && point.tcpRef->kind != requirements::RequirementRefKind::DefaultTcp) {
            const auto& ref = *point.tcpRef;
            const auto loc = ref.objectId ? model.findObject(*ref.objectId) : std::nullopt;
            if (ref.kind != requirements::RequirementRefKind::Tool || !loc
                || loc->kind != runtime::CanonicalModel::ObjectKind::Tool) {
                slice.errorText = "任务点「" + point.name + "」TCP工具引用悬空";
                return slice;
            }
            const auto& tool = model.tools().at(loc->index);
            const auto named = std::find_if(tool.namedTcpOffsets.begin(), tool.namedTcpOffsets.end(),
                [&](const auto& entry) { return entry.first == ref.tcpKey; });
            if (!ref.tcpKey.empty() && named == tool.namedTcpOffsets.end() && ref.tcpKey != tool.localName) {
                slice.errorText = "任务点「" + point.name + "」TCP键不存在：" + ref.tcpKey;
                return slice;
            }
            kinematics::TcpRef overrideRef;
            overrideRef.toolObject = *ref.objectId; overrideRef.tcpKey = ref.tcpKey;
            projected.tcpOverride = std::move(overrideRef);
        }
        // ---- 点参考系解析（★ KIN-P4：与区域计划共用 projectRefFrameToBase
        // 单点——World 恒等/ModelFrame 解析缝；任务点为完整位姿，
        // 不受区域轴对齐盒的可表示性限制，旋转直接参与位姿链）。
        rw::math::Transform3D<double> baseTref;
        if (!projectRefFrameToBase(point.refFrame, baseTref, slice.errorText)) {
            return slice;
        }
        // ---- 目标位姿（refFrame 系 → 基座系 {B}：位置＝T_base_ref·p；
        // point.pose.position 在 refFrame 系（RequirementTypes.hpp 字段注）。
        // 位置与姿态的参考目标必须分开：AlignFrame的目标只决定旋转，
        // 不能拿目标Frame的原点覆盖任务点位置。未提供位置（MDL-06
        // 缺失不转零）＝点数据不足，如实拒绝该点投影（ERR-01）。
        const std::optional<rw::math::Vector3D<double>> pointPosition =
            point.pose.position.tryValue();
        if (!pointPosition.has_value()) {
            slice.errorText = "任务点「" + point.name
                              + "」未提供约束位置（数据缺失——如实拒绝）";
            return slice;
        }
        projected.targetInBase.P() =
            baseTref.P() + baseTref.R() * *pointPosition;
        // REQ-09：固定姿态字面按roll/pitch/yaw（rad）存储，应用序是
        // Rz(yaw)·Ry(pitch)·Rx(roll)。RobWork的RPY构造参数却按Z/Y/X，
        // 因此必须显式反序传入，再左乘参考系→基座系的旋转。
        // 这里只修复既有完整位姿载体能表达的Fixed/AlignFrame；其余规则
        // 的方向约束/滚转自由与分量掩码仍需消费者能力，不能据此宣称完整支持。
        projected.targetInBase.R() = baseTref.R();
        kinematics::TaskPoseConstraint taskConstraint;
        taskConstraint.referenceInBase = baseTref.R();
        taskConstraint.constrainedDof = {
            point.pose.constrainedDof.x, point.pose.constrainedDof.y,
            point.pose.constrainedDof.z, point.pose.constrainedDof.roll,
            point.pose.constrainedDof.pitch, point.pose.constrainedDof.yaw};
        const auto& rule = point.pose.orientation;
        if (rule.kind == requirements::OrientationRuleKind::Fixed) {
            const auto& rpy = rule.fixedRpy;
            if (!std::isfinite(rpy[0]) || !std::isfinite(rpy[1]) || !std::isfinite(rpy[2])) {
                slice.errorText = "任务点「" + point.name + "」固定姿态含非有限角（rad）";
                return slice;
            }
            projected.targetInBase.R() = baseTref.R()
                * rw::math::RPY<double>(rpy[2], rpy[1], rpy[0]).toRotation3D();
        } else if (rule.kind == requirements::OrientationRuleKind::AlignFrame) {
            rw::math::Transform3D<double> baseTtarget;
            if (!projectRefFrameToBase(rule.targetFrame, baseTtarget, slice.errorText)) {
                // 目标悬空是输入错误；拒绝整个受理，既有结果账面不被替换。
                slice.errorText = "任务点「" + point.name + "」对齐目标：" + slice.errorText;
                return slice;
            }
            projected.targetInBase.R() = baseTtarget.R();
        } else if (rule.kind == requirements::OrientationRuleKind::AlignGeometryNormal
                   || rule.kind == requirements::OrientationRuleKind::PointAtTarget) {
            rw::math::Vector3D<double> direction;
            if (rule.kind == requirements::OrientationRuleKind::AlignGeometryNormal) {
                if (!rule.feature.has_value()
                    || *rule.feature != requirements::OrientationFeature::FramePlaneNormal
                    || !rule.targetSceneObject.has_value()) {
                    slice.errorText = "任务点「" + point.name + "」法向规则缺少FramePlaneNormal引用";
                    return slice;
                }
                rw::math::Transform3D<double> baseTtarget;
                if (!projectRefFrameToBase(
                        requirements::RequirementReference{
                            requirements::RequirementRefKind::SceneObject,
                            rule.targetSceneObject}, baseTtarget, slice.errorText)) {
                    slice.errorText = "任务点「" + point.name + "」几何法向目标不可解析：" + slice.errorText;
                    return slice;
                }
                direction = baseTtarget.R() * rw::math::Vector3D<double>(0.0, 0.0, 1.0);
                if (rule.invertNormal) direction = -direction;
            } else {
                const rw::math::Vector3D<double> targetBase =
                    baseTref.P() + baseTref.R() * rule.targetPoint;
                direction = targetBase - projected.targetInBase.P();
            }
            const double length = std::sqrt(direction.norm2());
            if (!std::isfinite(length) || length <= 1e-12) {
                slice.errorText = "任务点「" + point.name + "」方向目标与任务点重合，无法确定工具轴";
                return slice;
            }
            direction *= (1.0 / length);
            rw::math::Vector3D<double> refAxis(1.0, 0.0, 0.0);
            if (std::fabs(refAxis.dot(direction)) > 0.9) refAxis = rw::math::Vector3D<double>(0.0, 1.0, 0.0);
            rw::math::Vector3D<double> xAxis = refAxis.cross(direction);
            xAxis *= (1.0 / std::sqrt(xAxis.norm2()));
            const rw::math::Vector3D<double> yAxis = direction.cross(xAxis);
            projected.targetInBase.R() = rw::math::Rotation3D<double>(
                xAxis[0], yAxis[0], direction[0], xAxis[1], yAxis[1], direction[1],
                xAxis[2], yAxis[2], direction[2]);
            // ★ 批34（评审 F4）：方向规则只约束工具轴指向——绕轴滚转是
            //   规则未定义面。上面 targetInBase.R() 的参考轴选取（X 优先、
            //  近平行换 Y）只是构造任意正交基的实现细节，把它当约束目标＝
            //   额外固定了工具滚转（评审反例）。放开为全周自由
            //   （allowToolRoll＋span=2π——求解器语义"全周＝无区间残差"，
            //   残差侧 yaw 行按 rollResidualActive=0 置零不驱动）。
            taskConstraint.allowToolRoll = true;
            taskConstraint.rollRange = {-3.14159265358979323846, 3.14159265358979323846};
        }
        if (rule.kind == requirements::OrientationRuleKind::ToolRollFree) {
            taskConstraint.allowToolRoll = true;
            taskConstraint.rollRange = {rule.rollRange.min, rule.rollRange.max};
        }
        projected.taskConstraint = taskConstraint;
        // ---- 容差/要求值直投（I-REQ-5 >0 有限——域校验链保证）。
        projected.positionTolerance = point.tolerance.positionTolerance;    // m
        projected.orientationTolerance = point.tolerance.orientationTolerance; // rad
        projected.demands.collisionFreeRequired =
            point.demands.collisionFreeRequired;
        projected.demands.minimumJointMargin = point.demands.minimumJointMargin;
        slice.query.points.push_back(std::move(projected));
    }

    // ---- 工况投影（与覆盖切片同构——KIN-05 承接面直投）。
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
        projected.demands.collisionFreeRequired = cond.demands.collisionFreeRequired;
        projected.demands.minimumJointMargin = cond.demands.minimumJointMargin;
        slice.query.conditions.push_back(std::move(projected));
    }

    // ---- caseSubset＝启用工况 id 集（批量展开分母——EVI-02；空集＝
    // 无工况可展开，工作项 0——如实受理，完成矩阵空集即诚实结论）。
    for (const requirements::OperatingCondition& cond : ws.conditions.entries) {
        if (cond.enabled) {
            slice.request.caseSubset.push_back(cond.objectId);
        }
    }

    // ---- 评估请求（绑定面最小构造——Verified 会话语义：批量验证是
    // 正式评估通道（写 results 门控在面板提交流）——域批量评估器不做
    // Preview（§4.3 模式集两值），故取 Verified；task 五元组生成值仅供
    // 任务绑定透传，纯计算面零校验）。
    slice.request.mode = core::EvaluationMode::Verified;
    slice.request.task.project = core::ProjectId::generate();
    slice.request.task.branch = core::BranchId::generate();
    slice.request.task.revision = core::RevisionId::generate();
    slice.request.task.run = core::RunId::generate();
    slice.request.task.attempt = core::AttemptId{1U};  // 尝试序 ≥1（会话级单尝试）
    return slice;
}

void KinEvaluationExecutor::onSolveFinished(
    const std::string& taskRef, const core::ContentIdentity& snapshotIdAtSubmit,
    const core::ContentIdentity& configDigestAtSubmit, std::uint64_t epoch,
    kinematics::KinChannelSolveResult result, bool interrupted)
{
    // ---- 在途 future/取消标志惰性清理（完成回投即该任务已终态——
    // 与 onCoverageFinished 同纪律）。
    for (auto it = m_inflight.begin(); it != m_inflight.end();) {
        if (it->wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            it->get();
            it = m_inflight.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = m_cancelFlags.begin(); it != m_cancelFlags.end();) {
        if (it->use_count() <= 1) {
            it = m_cancelFlags.erase(it);  // 仅剩账面强引用＝任务已终态
        } else {
            ++it;
        }
    }

    // ---- 迟到判定（提交纪元≠当前纪元——会话已换绑/拆除：产物按提交
    // 锚丢弃，绝不打上新锚入账；账目终态与 note 照常——L-K12）。
    const bool stale = (epoch != m_epoch);

    // ---- 账目终态（登记序线性查找——任务量级＝人工操作频度）。
    const auto it = std::find_if(m_tasks.begin(), m_tasks.end(),
                                 [&taskRef](const TaskEntry& t) {
                                     return t.taskRef == taskRef;
                                 });
    if (it != m_tasks.end()) {
        it->running = false;
        it->interrupted = interrupted || result.cancelled;
        it->summary = result.ok
                          ? result.summaryText
                          : (result.errorText.empty() ? "求解未完成"
                                                      : result.errorText);
    }

    // ---- 结果入账（成功/失败/取消都入——失败可见面；绑定键＝提交时
    // 值；迟到不入账）。
    if (!stale) {
        m_latestSolve = std::move(result);
    }

    // ---- 结构化投递（宿主闭包核对锚后经门面写会话态——槽空＝仅账面，
    // 不虚构回写）＋note 投递（中断如实：取消是任务的**真实中断事实**
    // ——与迟到判定正交叠加，NFR-REL-03；stale 时 summaryText 表达迟到
    // 丢弃、interrupted 仍如实携带取消事实——两个语义轴不互斥）。
    if (m_deps.sessionSolveRoute && !stale) {
        m_deps.sessionSolveRoute(*m_latestSolve);
    }
    if (m_deps.resultRoute) {
        kinematics::KinChannelBackgroundResultNote note;
        note.acceptedEpoch = epoch;
        note.kind = kinematics::KinChannelBackgroundKind::SessionSolve;
        note.interrupted = interrupted || result.cancelled;
        note.summaryText = stale
                               ? "迟到结果已按提交锚丢弃（会话已换绑）"
                               : (m_latestSolve ? m_latestSolve->summaryText
                                                : std::string{"结果未入账"});
        m_deps.resultRoute(note);
    }
}

void KinEvaluationExecutor::recordBatchOutcomes(
    const kinematics::BatchComputation& computation,
    const core::ContentIdentity& snapshotIdAtSubmit)
{
    // ---- 逐点聚合（computation.items 按 (pointOid, conditionId) 全序
    // ——顺序扫描分组；点级状态词＝逐工况记录的合成投影：
    //   - 任一 NotRun → "not-run"（取消/失败批——部分结果如实标注）；
    //   - 全部 NotApplicable → "not-applicable"（停用点/无适用工况）；
    //   - 任一失败轨（NoConvergence/AllFiltered/BoundExceeded/
    //     InputInvalid）→ "failed"（工程不可行/数据错误的如实呈现）；
    //   - 其余（≥1 CandidateFound 且无失败轨）→ "completed"。
    // 摘要＝解数/工况数计数（如"解 2 个/2 工况"）；完成矩阵＝无 NotRun
    // 工作项（NotApplicable 不算未完成——显式标记语义）。
    std::map<core::ObjectId, kinematics::KinChannelTaskPointOutcome> merged;
    const core::ObjectId* currentPoint = nullptr;
    std::size_t solvedCount = 0;
    std::size_t conditionCount = 0;
    bool anyNotRun = false;
    bool anyFailed = false;
    bool anyCandidate = false;
    auto flush = [&]() {
        if (currentPoint == nullptr) {
            return;
        }
        kinematics::KinChannelTaskPointOutcome outcome;
        outcome.pointOid = *currentPoint;
        if (anyNotRun) {
            outcome.outcomeState = "not-run";
        } else if (!anyCandidate && !anyFailed) {
            outcome.outcomeState = "not-applicable";
        } else if (anyFailed || !anyCandidate) {
            outcome.outcomeState = "failed";
        } else {
            outcome.outcomeState = "completed";
        }
        outcome.outcomeText =
            "解 " + std::to_string(solvedCount) + " 个/"
            + std::to_string(conditionCount) + " 工况";
        outcome.requiredCoverageDone = !anyNotRun;
        merged[*currentPoint] = std::move(outcome);
        currentPoint = nullptr;
        solvedCount = 0;
        conditionCount = 0;
        anyNotRun = false;
        anyFailed = false;
        anyCandidate = false;
    };
    for (const kinematics::BatchWorkItemRecord& item : computation.items) {
        if (currentPoint == nullptr || !(*currentPoint == item.pointOid)) {
            flush();
            currentPoint = &item.pointOid;
        }
        ++conditionCount;
        switch (item.status) {
            case kinematics::BatchItemStatus::CandidateFound:
                ++solvedCount;
                anyCandidate = true;
                break;
            case kinematics::BatchItemStatus::NoConvergence:
            case kinematics::BatchItemStatus::AllFiltered:
            case kinematics::BatchItemStatus::BoundExceeded:
            case kinematics::BatchItemStatus::InputInvalid:
                anyFailed = true;
                break;
            case kinematics::BatchItemStatus::NotRun:
                anyNotRun = true;
                break;
            case kinematics::BatchItemStatus::NotApplicable:
                break;  // 显式标记——不计失败不算完成（ERR-01 词表完备）
        }
    }
    flush();

    // ---- 入账（整批原子替换——绑定锚同拍写；消费方核对锚后合入
    // taskPoints 投影）。
    m_latestBatch = std::move(merged);
    m_latestBatchSnapshotId = snapshotIdAtSubmit;
}

void KinEvaluationExecutor::finishBatchTask(
    const std::string& taskRef, const core::ContentIdentity& snapshotIdAtSubmit,
    std::uint64_t epoch, const kinematics::BatchComputation& computation)
{
    // ---- 在途清理（同 onSolveFinished——终态回投拍）。
    for (auto it = m_inflight.begin(); it != m_inflight.end();) {
        if (it->wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            it->get();
            it = m_inflight.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = m_cancelFlags.begin(); it != m_cancelFlags.end();) {
        if (it->use_count() <= 1) {
            it = m_cancelFlags.erase(it);
        } else {
            ++it;
        }
    }

    // ---- 迟到判定＋账目终态（批量摘要＝逐点统计——Solved/Total 工作项）。
    const bool stale = (epoch != m_epoch);
    std::uint64_t solvedItems = 0;
    for (const kinematics::BatchWorkItemRecord& item : computation.items) {
        if (item.status == kinematics::BatchItemStatus::CandidateFound) {
            ++solvedItems;
        }
    }
    const auto it = std::find_if(m_tasks.begin(), m_tasks.end(),
                                 [&taskRef](const TaskEntry& t) {
                                     return t.taskRef == taskRef;
                                 });
    if (it != m_tasks.end()) {
        it->running = false;
        it->interrupted = computation.incomplete;
        it->summary = "批量验证完成：工作项 " + std::to_string(solvedItems)
                      + "/" + std::to_string(computation.totalWorkItems)
                      + " 有解（点 " + std::to_string(m_latestBatch.size())
                      + " 个——结果见任务点表）";
    }
    if (m_deps.resultRoute) {
        kinematics::KinChannelBackgroundResultNote note;
        note.acceptedEpoch = epoch;
        note.kind = kinematics::KinChannelBackgroundKind::TaskPointsBatch;
        note.interrupted = computation.incomplete;
        note.summaryText = stale
                               ? "迟到结果已按提交锚丢弃（会话已换绑）"
                               : (it != m_tasks.end() ? it->summary
                                                      : std::string{"批量验证完成"});
        m_deps.resultRoute(note);
    }
}

void KinEvaluationExecutor::onBatchFailed(
    const std::string& taskRef, const core::ContentIdentity& snapshotIdAtSubmit,
    std::uint64_t epoch, const std::string& errorText)
{
    // ---- 在途清理（终态回投拍——同 onSolveFinished）。
    for (auto it = m_inflight.begin(); it != m_inflight.end();) {
        if (it->wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            it->get();
            it = m_inflight.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = m_cancelFlags.begin(); it != m_cancelFlags.end();) {
        if (it->use_count() <= 1) {
            it = m_cancelFlags.erase(it);
        } else {
            ++it;
        }
    }
    // ---- 账目终态（失败如实——零逐点入账）。
    const auto it = std::find_if(m_tasks.begin(), m_tasks.end(),
                                 [&taskRef](const TaskEntry& t) {
                                     return t.taskRef == taskRef;
                                 });
    if (it != m_tasks.end()) {
        it->running = false;
        it->interrupted = false;
        it->summary = errorText;
    }
    if (m_deps.resultRoute) {
        kinematics::KinChannelBackgroundResultNote note;
        note.acceptedEpoch = epoch;
        note.kind = kinematics::KinChannelBackgroundKind::TaskPointsBatch;
        note.interrupted = false;
        note.summaryText = errorText;
        m_deps.resultRoute(note);
    }
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
            // MA-03：覆盖任务的进度原子由后台 reportProgress 单调落值——
            // 真实百分比呈现；无进度源（单点求解）维持缺省不伪造。
            if (t.progress != nullptr) {
                row.percent = static_cast<int>(
                    t.progress->load(std::memory_order_relaxed));
            } else {
                row.percent = std::nullopt;
            }
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
