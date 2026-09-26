/**
 * @file   Evaluators.cpp
 * @brief  kin.pose-metrics 评估器实现——descriptor 组装、宿主注入工厂与
 *         评估主流程（IFkEvaluator 委托＋结构化诊断分轨）。
 *
 * 设计依据：
 *   - units/kinematics.md §4.3（依赖声明行）、§5.2（两分语义——FK 失败
 *     ≠工程不可行）、§9.3（评估器不自我注册；"抛 EvidenceError 或返回
 *     诊断，域自选，约定登记于域任务卡"——本域落值见 Evaluators.hpp
 *     类注与卡 §14.6）、§14.4 条 2
 *   - evidence 冻结契约（Evaluator.hpp/Envelope.hpp/Digest 经 core）；
 *     core DiagnosticRecord::make（C-3 校验——诊断内容面）
 *   - 治理裁决 O-37（宿主注入——工厂闭包，见头注）
 *   - 任务契约 tasks/foundation/WP-15-T03.json acceptance 3/4
 *
 * 确定性（NFR-COR-01/02）：evaluate() 为纯计算——无时钟/环境/线程数
 * 依赖；payload 摘要唯一经 core::ContentDigester（CR-02）；诊断文本为
 * 编译期字面量。
 */

#include <sdurws/ird/kinematics/Evaluators.hpp>

#include <sdurws/ird/core/DiagData.hpp>  // DiagnosticRecord::make（C-3 校验工厂）
#include <sdurws/ird/core/Digest.hpp>    // ContentDigester（SHA-256 唯一算法面——CR-02）
#include <sdurws/ird/core/Provenance.hpp>  // SourcedValue/ValueProvenance（比较型诊断数值侧）
#include <sdurws/ird/core/Units.hpp>     // UnitToken（比较型诊断单位 token——注册表）
#include <sdurws/ird/kinematics/DiagCodes.hpp>  // kKin* 码值常量（唯一书写点——禁拼码）

#include "CanonicalCodec.hpp"  // 私有编码原语（src/ 内共享——R-2 合规）

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <exception>
#include <functional>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace sdurws::ird::kinematics {

namespace {

// =====================================================================
// 装配期查询校验（调用方错误 fail-fast 轨——AGENTS §3；NFR-COR-03）
// =====================================================================

/// 视图自由度清点（可动关节链序计数——Fk.cpp 同口径；单一语义两处实现
/// 以适配器/服务解耦为界，测试的黄金算例两侧互证）。
std::size_t viewDof(const IKinRuntimeView& view)
{
    std::size_t dof = 0;
    for (const auto& j : view.model().chain().joints) {
        if (j.type != runtime::JointType::Fixed) { ++dof; }
    }
    return dof;
}

/// 查询校验：q 维度＝设备自由度且全部有限（违例→std::invalid_argument，
/// 不钳制不置零——评估输出面不承载调用方错误，见 Evaluators.hpp 分轨注）。
void validateQuery(const IKinRuntimeView& view, const PoseMetricsQuery& query)
{
    const std::size_t dof = viewDof(view);
    if (query.q.size() != dof) {
        throw std::invalid_argument(
            "kin.pose-metrics 查询非法：q 维度 " + std::to_string(query.q.size())
            + " != 设备自由度 " + std::to_string(dof)
            + "（NFR-COR-03：装配期 fail-fast，不钳制不置零）");
    }
    for (std::size_t k = 0; k < query.q.size(); ++k) {
        if (!std::isfinite(query.q[k])) {
            throw std::invalid_argument(
                "kin.pose-metrics 查询非法：q[" + std::to_string(k)
                + "] 非有限（NaN/Inf——NFR-COR-03：不置零）");
        }
    }
}

// =====================================================================
// 评估期结构化诊断（KIN-NO-DEVICE/KIN-NO-TCP——§9.6 error 级两码）
// =====================================================================

/// 由域错误映射取注册码文本（禁字符串拼码——唯一经 kinematicsDiagCode
/// 映射数据；缺映射＝注册/实现回归，内部不变量破坏，fail-fast 不静默）。
std::string requireMappedCode(KinematicsErrorCode code)
{
    const std::optional<std::string_view> mapped = kinematicsDiagCode(code);
    if (!mapped.has_value()) {
        // 不可达路径（NoDevice/NoTcp 的映射行随 T03 登记——Errors.cpp）；
        // 命中即实现/装配回归：内部契约违约走异常 fail-fast（AGENTS §3
        // 错误语义表——调用方/内部违约轨），不得静默丢弃结构化素材。
        throw std::logic_error("kin.pose-metrics：域错误缺少已登记稳定码映射"
                               "（实现/装配回归——§9.6 映射纪律）");
    }
    return std::string(*mapped);
}

/// 组装结构化诊断记录（subject 仅在语义对象存在时携带——不伪造；
/// context/cause/recommendedAction 非空为 C-3 校验前置）。
core::DiagnosticRecord makeStructuredDiag(const KinematicsError& err,
                                          std::optional<core::ObjectId> subject)
{
    // 码值经映射数据取自 DiagCodes.hpp 注册常量（PA-1：码值权威归
    // StableCodeRegistry——本函数只消费映射，不自造码文本）。
    return core::DiagnosticRecord::make(
        requireMappedCode(err.code),
        std::move(subject),
        std::nullopt,  // localName：诊断定位名缺失不伪造（§4.2 消费面）
        std::nullopt,  // runtimeName：⑥名称端口消费随 T04+ 名称面任务
        "kin.pose-metrics 位姿指标评估",                    // context
        err.detail,                                         // cause（内部诊断链语义）
        err.code == KinematicsErrorCode::NoDevice
            ? "核对模型闭包含机器人链后重建快照再评"        // recommendedAction
            : "配置工具 TCP 或修正 tcpRef 引用后再评");
}

}  // namespace

// =====================================================================
// descriptor 组装（§4.3 kin.pose-metrics 行——字段落值见头注）
// =====================================================================

evidence::EvaluatorDescriptor makePoseMetricsDescriptor()
{
    evidence::EvaluatorDescriptor d;
    d.key = kPoseMetricsEvaluationKey;
    d.contractVersion = kPoseMetricsContractVersion;

    // 依赖声明五条（§4.3 行原样；全部 Required——本评估器无条件依赖；
    // collision-models 不在行内——碰撞声明属 region-coverage/T07）。
    // resolutionNote 供人工评审"该键从哪里解析"（evidence §4.2.1——
    // 非身份载体，无语法约束）。
    d.inputs = {
        {"model.robot-design", evidence::DependencyKind::Object,
         evidence::DependencyRequiredness::Required, std::nullopt,
         "规范机器人链对象（Object 闭包内 robot-design——链/关节/限位真值）"},
        {"tcp", evidence::DependencyKind::Object,
         evidence::DependencyRequiredness::Required, std::nullopt,
         "工具定义对象（Object→tool-definition——TCP 偏置真值，AT-05① 失效面）"},
        {"policy.resolved", evidence::DependencyKind::Policy,
         evidence::DependencyRequiredness::Required, std::nullopt,
         "已解析工程策略内容身份（CON-06——策略变更全列失效）"},
        {"namemap", evidence::DependencyKind::NameMap,
         evidence::DependencyRequiredness::Required, std::nullopt,
         "运行时名称映射内容身份（CON-06——⑥端口诊断定位/结果标注）"},
        {"config.ik", evidence::DependencyKind::Configuration,
         evidence::DependencyRequiredness::Required, std::nullopt,
         "分析求解配置（KIN-13 canonical——入 sliceId 不入基准，D-04）"},
    };

    // Profile 声明引用：域 id 词表 "kin"（evidence isDomainProfileId）；
    // contentIdentity 置零值＝域不可申报（evidence §9.5/R-3——注册期由
    // Profile 注册表计算权威身份，申报非零即注册拒绝）。
    d.profile.profileId = "kin";
    d.profile.version = "1";
    d.profile.contentIdentity = core::ContentIdentity{};

    // 模式集：§4.3 行全三值（Preview/Quick/Verified——位姿指标可预览、
    // 可筛选、可正式验证；效力门禁在汇总/包络层）。
    d.supportedModes = {core::EvaluationMode::Preview,
                        core::EvaluationMode::Quick,
                        core::EvaluationMode::Verified};

    // 无跨调用状态（实例可共享——§9.4；本类零可变成员）。
    d.stateless = true;
    return d;
}

// =====================================================================
// PoseMetricsEvaluator——构造校验＋评估主流程
// =====================================================================

PoseMetricsEvaluator::PoseMetricsEvaluator(const IKinRuntimeView* view,
                                           PoseMetricsQuery query)
    : m_view(view), m_query(std::move(query)), m_descriptor(makePoseMetricsDescriptor())
{
    // 装配期 fail-fast（类注错误分轨）：空视图＝装配违约；q 维度/有限性
    // ＝调用方错误——两轨都不进入评估输出面（NFR-COR-03）。
    if (m_view == nullptr) {
        throw std::invalid_argument(
            "kin.pose-metrics：注入视图为空指针（宿主注入契约违约——O-37）");
    }
    validateQuery(*m_view, m_query);

    // TCP 键预检（装配期可判的 FrameUnresolved 提前——工具已可解析时，
    // 键不命中即调用方错误；工具侧缺失（未配置/悬空）保留到评估期＝
    // KIN-NO-TCP 结构化素材（§5.2 两分语义）。
    const runtime::CanonicalModel& model = m_view->model();
    if (!model.tools().empty()) {
        const auto loc = model.findObject(m_query.tcp.toolObject);
        if (loc.has_value() && loc->kind == runtime::CanonicalModel::ObjectKind::Tool) {
            const runtime::CanonicalTool& tool = model.tools().at(loc->index);
            if (!m_query.tcp.tcpKey.empty() && m_query.tcp.tcpKey != tool.localName) {
                throw std::invalid_argument(
                    "kin.pose-metrics：tcpKey '" + m_query.tcp.tcpKey
                    + "' 不命中工具 '" + tool.localName
                    + "' 的 canonical TCP（帧未解析——装配期 fail-fast）");
            }
        }
    }
}

const evidence::EvaluatorDescriptor& PoseMetricsEvaluator::descriptor() const
{
    // 返回引用指向成员副本（稳定存储——evidence §9.2 契约要求）。
    return m_descriptor;
}

evidence::EvaluationOutput PoseMetricsEvaluator::evaluate(
    const evidence::EvaluationRequest& /*request*/,
    evidence::IEvaluationContext& /*context*/)
{
    // 纯函数服务委托（计算面单一实现点＝FkEvaluator——评估器只做形态
    // 包装与错误分轨）。取消：契约显式豁免（§9.2 @取消——<1 s 内联计算，
    // 不周期查询 cancellationRequested）；context 不读取对象字节（模型
    // 经注入视图消费——O-37 宿主注入形态）。
    evidence::EvaluationOutput out;
    const FkEvaluator service;
    Expected<PoseMetrics> metrics = service.evaluate(*m_view, m_query.tcp, m_query.q);

    if (!metrics.ok()) {
        // 评估期结构化素材轨：NoDevice/NoTcp → 已注册稳定码诊断（零
        // payload、零证据项——FK 失败≠工程不可行，判定留 evidence）。
        // IllegalQ/FrameUnresolved 不可达（装配期已 fail-fast——构造器
        // 校验面），防御性归内部违约（requireMappedCode 的 logic_error
        // 同理——不静默）。
        const KinematicsError& err = metrics.error();
        std::optional<core::ObjectId> subject;
        if (err.code == KinematicsErrorCode::NoTcp) {
            subject = m_query.tcp.toolObject;  // 结构化素材携带指名工具身份
        }
        out.diagnostics.push_back(makeStructuredDiag(err, std::move(subject)));
        return out;
    }

    // 成功轨：canonical 字节（确定性面）＋SHA-256 摘要（CR-02 唯一算法
    // ——core ContentDigester；evidence §7.1 载荷完整性凭据）。
    const std::vector<std::uint8_t> bytes =
        encodePoseMetricsCanonical(metrics.get());
    core::ContentDigester digester;
    digester.update(bytes.data(), bytes.size());
    core::ContentIdentity digest;
    digest.bytes = digester.finalize();

    out.payload = evidence::DomainPayload{
        kPoseMetricsPayloadToken, bytes, digest};
    return out;
}

// =====================================================================
// PoseMetricsEvaluatorFactory——宿主注入工厂（闭包捕获；create 无参）
// =====================================================================

PoseMetricsEvaluatorFactory::PoseMetricsEvaluatorFactory(const IKinRuntimeView* view,
                                                         PoseMetricsQuery query)
    : m_view(view), m_query(std::move(query)), m_descriptor(makePoseMetricsDescriptor())
{
    // 与评估器同一装配校验面（工厂是宿主的注入入口——违约在装配期暴露，
    // 不迟至 create()）：空视图、q 维度/有限性、tcpKey 预检。
    if (m_view == nullptr) {
        throw std::invalid_argument(
            "kin.pose-metrics 工厂：注入视图为空指针（宿主注入契约违约——O-37）");
    }
    validateQuery(*m_view, m_query);
    // TCP 键预检（可解析到工具时即刻核对键匹配；工具侧缺失保留到评估期
    // ＝KIN-NO-TCP 结构化素材——与 PoseMetricsEvaluator 构造器同口径）。
    const runtime::CanonicalModel& model = m_view->model();
    if (!model.tools().empty()) {
        const auto loc = model.findObject(m_query.tcp.toolObject);
        if (loc.has_value() && loc->kind == runtime::CanonicalModel::ObjectKind::Tool) {
            const runtime::CanonicalTool& tool = model.tools().at(loc->index);
            if (!m_query.tcp.tcpKey.empty() && m_query.tcp.tcpKey != tool.localName) {
                throw std::invalid_argument(
                    "kin.pose-metrics 工厂：tcpKey '" + m_query.tcp.tcpKey
                    + "' 不命中工具 '" + tool.localName
                    + "' 的 canonical TCP（帧未解析——装配期 fail-fast）");
            }
        }
    }
}

const evidence::EvaluatorDescriptor& PoseMetricsEvaluatorFactory::descriptor() const
{
    return m_descriptor;
}

std::unique_ptr<evidence::IEngineeringEvaluator>
PoseMetricsEvaluatorFactory::create() const
{
    // 无参签名（O-37 裁决原文——registry.create() 形态兼容）；视图指针
    // 经闭包传递——注入语义的唯一通道（evidence D-07 十行级适配精神：
    // 本工厂即宿主侧闭包载体，非转发包装器）。
    return std::make_unique<PoseMetricsEvaluator>(m_view, m_query);
}

// =====================================================================
// 以下为 T04 批次实现（kin.task-point-ik——头注类注列纲，逐步对号）
// =====================================================================

namespace {

// ---------------------------------------------------------------------
// 装配期查询校验（调用方错误 fail-fast 轨——TaskPointIkEvaluator 类注
// 错误分轨；NFR-COR-03 拒绝不钳制；KIN-TARGET-ILLEGAL 等语义锚）
// ---------------------------------------------------------------------

/// 目标位姿 12 分量全有限校验。
bool queryTargetAllFinite(const rw::math::Transform3D<double>& t)
{
    for (std::size_t i = 0; i < 3; ++i) {
        for (std::size_t j = 0; j < 3; ++j) {
            if (!std::isfinite(t.R()(i, j))) {
                return false;
            }
        }
        if (!std::isfinite(t.P()[i])) {
            return false;
        }
    }
    return true;
}

/// 查询校验（违例抛 std::invalid_argument——评估输出面不承载调用方错误）。
void validateTaskPointIkQuery(const IKinRuntimeView& view,
                              const TaskPointIkQuery& query)
{
    // 自由度清点（可动关节链序——FkEvaluator 同口径）。
    std::size_t dof = 0;
    for (const auto& j : view.model().chain().joints) {
        if (j.type != runtime::JointType::Fixed) {
            ++dof;
        }
    }

    // 目标与容差（KIN-TARGET-ILLEGAL——"目标非法：非有限/容差非法"）。
    if (!queryTargetAllFinite(query.targetInBase)) {
        throw std::invalid_argument(
            "kin.task-point-ik 查询非法：目标位姿含非有限分量"
            "（KIN-TARGET-ILLEGAL 语义锚——§5.5；装配期 fail-fast）");
    }
    if (!std::isfinite(query.positionTolerance) || query.positionTolerance <= 0.0) {
        throw std::invalid_argument(
            "kin.task-point-ik 查询非法：位置容差非法（须有限且>0，单位 m）"
            "——KIN-TARGET-ILLEGAL 语义锚（§5.5）");
    }
    if (!std::isfinite(query.orientationTolerance)
        || query.orientationTolerance <= 0.0) {
        throw std::invalid_argument(
            "kin.task-point-ik 查询非法：姿态容差非法（须有限且>0，单位 rad）"
            "——KIN-TARGET-ILLEGAL 语义锚（§5.5）");
    }

    // 参考构型（D-KIN-4——显式输入的契约面）。
    if (query.referenceQ.size() != dof) {
        throw std::invalid_argument(
            "kin.task-point-ik 查询非法：referenceQ 维度 "
            + std::to_string(query.referenceQ.size()) + " != 设备自由度 "
            + std::to_string(dof));
    }
    for (std::size_t k = 0; k < query.referenceQ.size(); ++k) {
        if (!std::isfinite(query.referenceQ[k])) {
            throw std::invalid_argument(
                "kin.task-point-ik 查询非法：referenceQ[" + std::to_string(k)
                + "] 非有限（NFR-COR-03：不置零）");
        }
    }

    // 求解参数面（计数/阈值/种子——I-KIN-4：seed=0 拒绝不静默替换）。
    if (query.initialValuesCount == 0U) {
        throw std::invalid_argument("kin.task-point-ik 查询非法：初值数量须 ≥1");
    }
    if (query.iterationLimit == 0U) {
        throw std::invalid_argument("kin.task-point-ik 查询非法：迭代上限须 ≥1");
    }
    if (!std::isfinite(query.dedupThresholdPerAxis)
        || query.dedupThresholdPerAxis <= 0.0) {
        throw std::invalid_argument(
            "kin.task-point-ik 查询非法：去重阈值非法（须有限且>0，rad|m 逐轴）");
    }
    if (query.initialStrategy == InitialValueStrategy::SeededRandom
        && query.seed == 0U) {
        throw std::invalid_argument(
            "kin.task-point-ik 查询非法：SeededRandom 的 seed=0（I-KIN-4："
            "拒绝，不做 0→1 静默替换——NFR-COR-03）");
    }

    // TCP 键预检（工具可解析时键不命中即装配违约——T03 同款口径；工具
    // 侧缺失（未配置/悬空）保留到评估期＝KIN-NO-TCP 结构化素材）。
    const runtime::CanonicalModel& model = view.model();
    if (!model.tools().empty()) {
        const auto loc = model.findObject(query.tcp.toolObject);
        if (loc.has_value() && loc->kind == runtime::CanonicalModel::ObjectKind::Tool) {
            const runtime::CanonicalTool& tool = model.tools().at(loc->index);
            if (!query.tcp.tcpKey.empty() && query.tcp.tcpKey != tool.localName) {
                throw std::invalid_argument(
                    "kin.task-point-ik：tcpKey '" + query.tcp.tcpKey
                    + "' 不命中工具 '" + tool.localName
                    + "' 的 canonical TCP（帧未解析——装配期 fail-fast）");
            }
        }
    }
}

// ---------------------------------------------------------------------
// 评估期结构化诊断（KIN-NO-TCP／KIN-RESIDUAL-EXCEEDED／
// KIN-JOINT-LIMIT-VIOLATED／KIN-SEARCH-EXHAUSTED——码值经 DiagCodes.hpp
// 注册常量，禁字符串拼码；碰撞过滤诊断归 T07——§9.6 任务列分工）
// ---------------------------------------------------------------------

/// 基础诊断组装（码值经 DiagCodes.hpp 注册常量传入——禁字符串拼码；
/// subject 仅在语义对象存在时携带——不伪造）。
core::DiagnosticRecord makeKinDiag(std::string_view code,
                                   std::optional<core::ObjectId> subject,
                                   const char* context,
                                   const std::string& cause,
                                   const char* recommendedAction)
{
    return core::DiagnosticRecord::make(std::string(code), std::move(subject),
                                        std::nullopt,   // localName 不伪造
                                        std::nullopt,   // runtimeName——⑥端口消费随名称面任务
                                        context, cause, recommendedAction);
}

/// 比较型诊断数值侧（SourcedValue 有值态——来源派生只读，methodTag 登记）。
core::ComparativeValue comparativeValue(double v, const char* unitSymbol)
{
    core::ComparativeValue cv;
    cv.quantity = core::SourcedValue<double>::provided(
        v, core::ValueProvenance::make(core::ProvenanceKind::DerivedReadOnly,
                                       std::nullopt, std::nullopt,
                                       std::string("kin-task-point-ik")));
    // 单位 token 经注册表 find（"m"/"rad" 已注册——Units.cpp 注册表；
    // 未命中＝实现缺陷，fail-fast 不产无单位比较诊断）。
    const std::optional<core::UnitToken> unit = core::UnitToken::find(unitSymbol);
    if (!unit.has_value()) {
        throw std::logic_error("kin.task-point-ik：比较型诊断单位 token 未注册"
                               "（实现/装配回归）");
    }
    cv.unit = *unit;
    return cv;
}

/// 残差复验过滤的比较型诊断（KIN-RESIDUAL-EXCEEDED——§9.6 行 4：
/// requiresComparison；按实际越限维度逐项产出（位置 m／姿态 rad）——
/// 复验判据为"逐项"，被过滤解至少一项越限，至少产出一条）。
void appendResidualDiags(std::vector<core::DiagnosticRecord>& out,
                         const FilteredSolutionRecord& r,
                         double posTol, double oriTol,
                         const core::ObjectId& pointOid)
{
    if (r.positionResidual > posTol) {
        out.push_back(core::DiagnosticRecord::make(
            std::string(kKinResidualExceeded), pointOid,
            std::nullopt, std::nullopt,
            "kin.task-point-ik 任务点 IK 硬过滤①残差复验（位置侧）",
            "FK 复算位置残差超容差（构型签名 " + r.signature + "）",
            "调整目标位姿或放宽求解配置容差后复评",
            core::ComparativeFields{comparativeValue(r.positionResidual, "m"),
                                    comparativeValue(posTol, "m")}));
    }
    if (r.orientationResidual > oriTol) {
        out.push_back(core::DiagnosticRecord::make(
            std::string(kKinResidualExceeded), pointOid,
            std::nullopt, std::nullopt,
            "kin.task-point-ik 任务点 IK 硬过滤①残差复验（姿态侧）",
            "FK 复算姿态残差超容差（构型签名 " + r.signature + "）",
            "调整目标姿态或放宽求解配置容差后复评",
            core::ComparativeFields{comparativeValue(r.orientationResidual, "rad"),
                                    comparativeValue(oriTol, "rad")}));
    }
}

}  // namespace

// =====================================================================
// makeTaskPointIkDescriptor（§4.3 kin.task-point-ik 行——字段落值见头注）
// =====================================================================

evidence::EvaluatorDescriptor makeTaskPointIkDescriptor()
{
    // 与 makePoseMetricsDescriptor 同构（§4.3 行"上述＋req.points"）；
    // 差异仅 key（Ik.hpp 唯一书写点常量）与第六条依赖。
    evidence::EvaluatorDescriptor d;
    d.key = kTaskPointIkEvaluationKey;
    d.contractVersion = kIkSolverContractVersion;

    d.inputs = {
        {"model.robot-design", evidence::DependencyKind::Object,
         evidence::DependencyRequiredness::Required, std::nullopt,
         "规范机器人链对象（Object 闭包内 robot-design——链/关节/限位真值）"},
        {"tcp", evidence::DependencyKind::Object,
         evidence::DependencyRequiredness::Required, std::nullopt,
         "工具定义对象（Object→tool-definition——TCP 偏置真值，AT-05① 失效面）"},
        {"policy.resolved", evidence::DependencyKind::Policy,
         evidence::DependencyRequiredness::Required, std::nullopt,
         "已解析工程策略内容身份（CON-06——策略变更全列失效）"},
        {"namemap", evidence::DependencyKind::NameMap,
         evidence::DependencyRequiredness::Required, std::nullopt,
         "运行时名称映射内容身份（CON-06——⑥端口诊断定位/结果标注）"},
        {"config.ik", evidence::DependencyKind::Configuration,
         evidence::DependencyRequiredness::Required, std::nullopt,
         "分析求解配置（KIN-13 canonical——入 sliceId 不入基准，D-04）"},
        {"req.points", evidence::DependencyKind::Object,
         evidence::DependencyRequiredness::Required, std::nullopt,
         "任务点对象闭包（KIN-02 消费对象面——pointOid 绑定与可达性事实）"},
    };

    // Profile 声明引用（域不申报身份——evidence §9.5/R-3，注册期计算）。
    d.profile.profileId = "kin";
    d.profile.version = "1";
    d.profile.contentIdentity = core::ContentIdentity{};

    // 模式集：§4.3 行全三值（Quick/Preview 载荷以 mode 承载 screening-only）。
    d.supportedModes = {core::EvaluationMode::Preview,
                        core::EvaluationMode::Quick,
                        core::EvaluationMode::Verified};

    d.stateless = true;
    return d;
}

// =====================================================================
// TaskPointIkEvaluator——构造校验＋评估主流程（结局映射见类注）
// =====================================================================

TaskPointIkEvaluator::TaskPointIkEvaluator(const IKinRuntimeView* view,
                                           TaskPointIkQuery query)
    : m_view(view), m_query(std::move(query)),
      m_descriptor(makeTaskPointIkDescriptor())
{
    // 装配期 fail-fast（类注错误分轨）：空视图＝装配违约；查询非法＝
    // 调用方错误——两轨都不进入评估输出面（NFR-COR-03）。
    if (m_view == nullptr) {
        throw std::invalid_argument(
            "kin.task-point-ik：注入视图为空指针（宿主注入契约违约——O-37）");
    }
    validateTaskPointIkQuery(*m_view, m_query);

    // 工具侧缺失（未配置/悬空）保留到评估期＝KIN-NO-TCP 结构化素材
    // （T03 两分口径——键不命中已在 validateTaskPointIkQuery 拒绝）。
    const runtime::CanonicalModel& model = m_view->model();
    if (model.tools().empty()) {
        KinematicsError err;
        err.code = KinematicsErrorCode::NoTcp;
        err.detail = "TCP 未配置：快照模型无工具（KIN-NO-TCP 素材——§9.6）";
        m_setupError = std::move(err);
    } else {
        const auto loc = model.findObject(m_query.tcp.toolObject);
        if (!loc.has_value()
            || loc->kind != runtime::CanonicalModel::ObjectKind::Tool) {
            KinematicsError err;
            err.code = KinematicsErrorCode::NoTcp;
            err.detail = "TCP 引用悬空：toolObject 未解析到快照工具"
                         "（KIN-NO-TCP 素材——§9.6）";
            m_setupError = std::move(err);
        }
    }
}

const evidence::EvaluatorDescriptor& TaskPointIkEvaluator::descriptor() const
{
    return m_descriptor;
}

evidence::EvaluationOutput TaskPointIkEvaluator::evaluate(
    const evidence::EvaluationRequest& request,
    evidence::IEvaluationContext& context)
{
    evidence::EvaluationOutput out;

    // ---- 评估期结构化素材轨：TCP 缺失（KIN-NO-TCP——零 payload 零证据；
    // FK 失败≠工程不可行，判定留 evidence）。----
    if (m_setupError.has_value()) {
        out.diagnostics.push_back(makeKinDiag(
            kKinNoTcp, m_query.pointOid,
            "kin.task-point-ik 任务点 IK 评估", m_setupError->detail,
            "配置工具 TCP 或修正 tcpRef 引用后再评"));
        return out;
    }

    // ---- 组装求解请求（§5.3 字段表；绑定面由评估请求填充——§5.6）。
    // 取消探针周期查询宿主上下文（§9.2 IIkSolver @取消——长运算必须
    // 周期查询；探针只决定提前返回，不影响数值——确定性不破坏）。----
    IkRequest ikRequest;
    ikRequest.targetInBase = m_query.targetInBase;
    ikRequest.positionTolerance = m_query.positionTolerance;
    ikRequest.orientationTolerance = m_query.orientationTolerance;
    ikRequest.modelView = m_view;
    ikRequest.tcp = m_query.tcp;
    ikRequest.iterationLimit = m_query.iterationLimit;
    ikRequest.intervals = evaluationIntervals(*m_view);
    ikRequest.initialValues = makeInitialValues(
        m_query.initialStrategy, m_query.initialValuesCount, m_query.seed,
        ikRequest.intervals, m_query.referenceQ);
    ikRequest.dedupThresholdPerAxis = m_query.dedupThresholdPerAxis;
    ikRequest.collisionSession = m_query.collisionSession;
    ikRequest.cancellationProbe = [&context]() {
        return context.cancellationRequested();
    };
    ikRequest.referenceQ = m_query.referenceQ;
    ikRequest.targetRef.pointOid = m_query.pointOid;
    ikRequest.targetRef.conditionId = m_query.conditionId;
    ikRequest.requestIdentity.snapshotId = request.snapshot.snapshotId;
    ikRequest.requestIdentity.sliceId = request.slice.sliceId;
    ikRequest.requestIdentity.configDigest = m_query.configDigest;
    ikRequest.requestIdentity.mode = request.mode;
    ikRequest.requestIdentity.seed = m_query.seed;
    ikRequest.requestIdentity.referenceQ = m_query.referenceQ;

    const IkOutcome outcome = IkSolver().solve(ikRequest);

    // ---- 取消轨：零素材输出（取消不是结局——§9.2；无 payload/证明/
    // 搜索记录/诊断）。----
    if (outcome.cancelled) {
        return out;
    }

    // ---- 硬过滤逐解诊断（评价/证据级——§5.5；碰撞原因诊断归 T07，
    // §9.6 任务列分工）。----
    for (const FilteredSolutionRecord& r : outcome.solutionSet.filteredRecords) {
        switch (r.reason) {
        case SolutionFilterReason::ResidualRecheck:
            appendResidualDiags(out.diagnostics, r, m_query.positionTolerance,
                                m_query.orientationTolerance, m_query.pointOid);
            break;
        case SolutionFilterReason::JointLimit:
            out.diagnostics.push_back(makeKinDiag(
                kKinJointLimitViolated, m_query.pointOid,
                "kin.task-point-ik 任务点 IK 硬过滤②限位",
                "解超关节限位/工作范围（构型签名 " + r.signature + "）",
                "核对目标可达域或求解配置的关节区间后复评"));
            break;
        case SolutionFilterReason::Collision:
            break;  // KIN-COLLISION-FILTERED 的产码面归 T07（§9.6）。
        }
    }

    // ---- 结局映射（§5.4 → EvaluationOutput；铁律：2/3/4 不得升级为
    // 任务级不可行——无 proof 除结局 5 外）。----
    switch (outcome.outcomeKind) {
    case IkOutcomeKind::SolutionsFound:
    case IkOutcomeKind::PartialCollision: {
        // 成功轨：canonical 字节（§5.6 绑定六要素内嵌）＋SHA-256 摘要
        // （CR-02 唯一算法面）。
        const std::vector<std::uint8_t> bytes =
            encodeTaskPointIkPayloadCanonical(outcome, request.task);
        core::ContentDigester digester;
        digester.update(bytes.data(), bytes.size());
        core::ContentIdentity digest;
        digest.bytes = digester.finalize();
        out.payload = evidence::DomainPayload{kTaskPointIkPayloadToken, bytes,
                                              digest};
        break;
    }
    case IkOutcomeKind::MultiInitNoConvergence:
    case IkOutcomeKind::AllCandidatesFiltered: {
        // 搜索未果（§8.1 C5/C8）：output.searchRecord 交付（汇总层判
        // DataInsufficient 附该记录——不得输出不可行结论）＋KIN-SEARCH-
        // EXHAUSTED 诊断（§9.6 行 10——附记录）。
        evidence::SearchExhaustedRecord record;
        record.searchBudgetUsed = outcome.solutionSet.searchRecord->searchBudgetUsed;
        record.initialGuessesTried = outcome.solutionSet.searchRecord->initialGuessesTried;
        record.filteredSolutions.reserve(
            outcome.solutionSet.filteredRecords.size());
        for (const FilteredSolutionRecord& r :
             outcome.solutionSet.filteredRecords) {
            evidence::FilteredSolution fs;
            fs.solutionRef = r.signature;  // 编码安全下限：非空＋无 NUL
            switch (r.reason) {
            case SolutionFilterReason::ResidualRecheck:
                fs.filterReason = evidence::SearchFilterReason::Residual;
                break;
            case SolutionFilterReason::JointLimit:
                fs.filterReason = evidence::SearchFilterReason::JointLimit;
                break;
            case SolutionFilterReason::Collision:
                fs.filterReason = evidence::SearchFilterReason::Collision;
                break;
            }
            record.filteredSolutions.push_back(std::move(fs));
        }
        out.searchRecord = std::move(record);
        out.diagnostics.push_back(makeKinDiag(
            kKinSearchExhausted, m_query.pointOid,
            "kin.task-point-ik 任务点 IK 求解",
            outcome.outcomeKind == IkOutcomeKind::MultiInitNoConvergence
                ? "全部初值迭代至上限未收敛（搜索未果 C5——复评可换初值/扩预算）"
                : "全部收敛候选被硬过滤（搜索未果 C8——构型级过滤不下任务结论）",
            "扩大初值集或迭代预算后按同一冻结输入复评（新 sliceId、同 "
            "inputBaseline——D-04）"));
        break;
    }
    case IkOutcomeKind::AnalyticBoundExceeded: {
        // 解析界限证明素材（仅素材不裁定——output.proof 承载，成立与否
        // 归 evidence validateProof；不入 opaque 载荷）。
        out.proof = outcome.proofMaterial->proof;
        break;
    }
    }
    return out;
}

// =====================================================================
// TaskPointIkEvaluatorFactory——宿主注入工厂（闭包捕获；create 无参）
// =====================================================================

TaskPointIkEvaluatorFactory::TaskPointIkEvaluatorFactory(
    const IKinRuntimeView* view, TaskPointIkQuery query)
    : m_view(view), m_query(std::move(query)),
      m_descriptor(makeTaskPointIkDescriptor())
{
    // 与评估器同一装配校验面（工厂是宿主的注入入口——违约在装配期
    // 暴露，不迟至 create()）。
    if (m_view == nullptr) {
        throw std::invalid_argument(
            "kin.task-point-ik 工厂：注入视图为空指针（宿主注入契约违约——O-37）");
    }
    validateTaskPointIkQuery(*m_view, m_query);
}

const evidence::EvaluatorDescriptor& TaskPointIkEvaluatorFactory::descriptor() const
{
    return m_descriptor;
}

std::unique_ptr<evidence::IEngineeringEvaluator>
TaskPointIkEvaluatorFactory::create() const
{
    // 无参签名（O-37 裁决——注册表兼容）；视图指针经闭包传递。
    return std::make_unique<TaskPointIkEvaluator>(m_view, m_query);
}

// =====================================================================
// encodeTaskPointIkPayloadCanonical（布局见头注；字段序即写序）
// =====================================================================

std::vector<std::uint8_t> encodeTaskPointIkPayloadCanonical(
    const IkOutcome& outcome, const core::TaskIdentity& task)
{
    std::vector<std::uint8_t> out;
    out.reserve(512);

    // magic "IRDIK01"（7 字节 ASCII）＋codec 版本 u32=1（布局演进即推进）。
    const char magic[] = {'I', 'R', 'D', 'I', 'K', '0', '1'};
    out.insert(out.end(), magic, magic + sizeof(magic));
    detail::putU32(out, 1U);

    // 结局面三标记。
    out.push_back(static_cast<std::uint8_t>(outcome.outcomeKind));
    out.push_back(outcome.cancelled ? 1U : 0U);
    out.push_back(outcome.collisionNotEvaluated ? 1U : 0U);

    // 请求身份块（§5.6 绑定面前半：snapshotId/sliceId/configDigest/
    // mode/seed/referenceQ——referenceQ 显式入身份，D-KIN-4）。
    const IkRequestIdentity& id = outcome.solutionSet.requestIdentity;
    detail::putBytes(out, id.snapshotId.bytes.data(), id.snapshotId.bytes.size());
    detail::putBytes(out, id.sliceId.bytes.data(), id.sliceId.bytes.size());
    detail::putBytes(out, id.configDigest.bytes.data(),
                     id.configDigest.bytes.size());
    out.push_back(static_cast<std::uint8_t>(id.mode));
    detail::putU64(out, id.seed);
    detail::putU32(out, static_cast<std::uint32_t>(id.referenceQ.size()));
    for (const double v : id.referenceQ) {
        detail::putF64(out, v);
    }

    // 对象绑定（pointOid/conditionId——§5.6 对象双重绑定）。
    const IkTargetRef& ref = outcome.solutionSet.targetRef;
    detail::putBytes(out, ref.pointOid.bytes.data(), ref.pointOid.bytes.size());
    out.push_back(ref.conditionId.has_value() ? 1U : 0U);
    if (ref.conditionId.has_value()) {
        detail::putBytes(out, ref.conditionId->bytes.data(),
                         ref.conditionId->bytes.size());
    }

    // 任务五元组（4×16B 强类型 id＋attempt u64——§5.6 绑定面后半）。
    detail::putBytes(out, task.project.bytes.data(), task.project.bytes.size());
    detail::putBytes(out, task.branch.bytes.data(), task.branch.bytes.size());
    detail::putBytes(out, task.revision.bytes.data(), task.revision.bytes.size());
    detail::putBytes(out, task.run.bytes.data(), task.run.bytes.size());
    detail::putU64(out, task.attempt.value);  // attempt＝u64 序号（非 Id128）

    // evaluationKey＋契约版本（§5.6 绑定六要素——长度前置的 ASCII）。
    const auto keyLen =
        static_cast<std::uint32_t>(std::strlen(kTaskPointIkEvaluationKey));
    detail::putU32(out, keyLen);
    detail::putBytes(out, kTaskPointIkEvaluationKey, keyLen);
    detail::putU32(out, outcome.solverContractVersion);

    // 统计四计数。
    const IkSolutionSetStatistics& stats = outcome.solutionSet.statistics;
    detail::putU64(out, stats.rawCount);
    detail::putU64(out, stats.convergedCount);
    detail::putU64(out, stats.dedupedCount);
    detail::putU64(out, stats.filteredCount);

    // 解集（稳定排序序——字节序即排序结果）。
    detail::putU32(out,
                   static_cast<std::uint32_t>(outcome.solutionSet.solutions.size()));
    for (const KinematicSolution& s : outcome.solutionSet.solutions) {
        detail::putU32(out, static_cast<std::uint32_t>(s.q.size()));
        for (const double v : s.q) {
            detail::putF64(out, v);
        }
        detail::putF64(out, s.positionResidual);
        detail::putF64(out, s.orientationResidual);
        detail::putU32(out, static_cast<std::uint32_t>(s.jointMargins.size()));
        for (const double v : s.jointMargins) {
            detail::putF64(out, v);
        }
        detail::putF64(out, s.minimumJointMargin);
        detail::putF64(out, s.manipulability);
        detail::putF64(out, s.conditionNumber);
        out.push_back(s.collisionStatus.evaluated ? 1U : 0U);
        out.push_back(s.collisionStatus.inCollision ? 1U : 0U);
        detail::putU32(out, static_cast<std::uint32_t>(
                                s.collisionStatus.objectIdPairs.size()));
        for (const core::ObjectId& oid : s.collisionStatus.objectIdPairs) {
            detail::putBytes(out, oid.bytes.data(), oid.bytes.size());
        }
        detail::putU32(out, s.sourceInitIndex);
        detail::putU32(out, s.iterations);
        const auto sigLen = static_cast<std::uint32_t>(s.signature.size());
        detail::putU32(out, sigLen);
        detail::putBytes(out, s.signature.data(), sigLen);
        detail::putU32(out, s.solverContractVersion);
    }

    // 过滤记录（诊断价值面——原因/对象对/指标）。
    detail::putU32(out, static_cast<std::uint32_t>(
                            outcome.solutionSet.filteredRecords.size()));
    for (const FilteredSolutionRecord& r : outcome.solutionSet.filteredRecords) {
        detail::putU32(out, static_cast<std::uint32_t>(r.q.size()));
        for (const double v : r.q) {
            detail::putF64(out, v);
        }
        out.push_back(static_cast<std::uint8_t>(r.reason));
        detail::putF64(out, r.positionResidual);
        detail::putF64(out, r.orientationResidual);
        detail::putF64(out, r.minimumJointMargin);
        detail::putF64(out, r.manipulability);
        detail::putU32(out, static_cast<std::uint32_t>(r.objectIdPairs.size()));
        for (const core::ObjectId& oid : r.objectIdPairs) {
            detail::putBytes(out, oid.bytes.data(), oid.bytes.size());
        }
        detail::putU32(out, r.sourceInitIndex);
        detail::putU32(out, r.iterations);
        const auto sigLen = static_cast<std::uint32_t>(r.signature.size());
        detail::putU32(out, sigLen);
        detail::putBytes(out, r.signature.data(), sigLen);
    }

    // 搜索未果记录（结局 2/3 有值——present u8）。
    const bool hasSearch = outcome.solutionSet.searchRecord.has_value();
    out.push_back(hasSearch ? 1U : 0U);
    if (hasSearch) {
        const IkSearchRecord& sr = *outcome.solutionSet.searchRecord;
        detail::putU64(out, sr.searchBudgetUsed);
        detail::putU64(out, sr.initialGuessesTried);
        detail::putU32(out, static_cast<std::uint32_t>(sr.iterationsPerInit.size()));
        for (const std::uint32_t it : sr.iterationsPerInit) {
            detail::putU32(out, it);
        }
    }
    return out;
}

// =====================================================================
// 以下为 T05 批量实现（kin.task-points-batch——Evaluators.hpp 类注列纲，
// 逐步对号；批量值模型/组装器实现见 src/Evidence.cpp）
// =====================================================================

namespace {

// ---------------------------------------------------------------------
// 批量工作项的内部求解状态（展开→全序→分批→写回的中间值——不跨出本
// 翻译单元；转 BatchWorkItemRecord 在 assembleRecord）
// ---------------------------------------------------------------------

/// 工作项类别（展开期判定：可解项进批次；预终结项不入批次——终态在
/// 展开期即确定，与批次执行无关，登记随卡 §14.6 v0.5）。
enum class WorkKind : std::uint8_t {
    Solvable,      ///< 可解项（进批次——逐项 IIkSolver 求解）
    NotApplicable, ///< 不适用（停用/None——显式标记）
    InputInvalid,  ///< 输入非法素材（悬空引用——V-12）
};

/// 单工作项的内部状态（结果槽按全序下标写回——§8.4 合并＝按全序归并
/// 而非到达序的承载结构：并行分片写各自槽位，无交叉写入）。
struct WorkState {
    core::ObjectId pointOid;      ///< 任务点身份（§5.6 绑定）
    core::ObjectId conditionId;   ///< 工况身份（§5.6 绑定）
    BatchRequirementLevel pointLevel = BatchRequirementLevel::Must; ///< 点等级（verdictInputs 分流）
    WorkKind kind = WorkKind::Solvable; ///< 展开期类别
    std::string reason;           ///< 预终结原因（预终结项必填——ERR-01）

    // ---- 求解输入（仅 Solvable 消费；展开期从投影值落定）----
    rw::math::Transform3D<double> targetInBase = runtime::detail::identityTransform3D();
    double positionTolerance = 1e-6;  ///< 位置容差（m）
    double orientationTolerance = 1e-6; ///< 姿态容差（rad）
    TcpRef tcp;                       ///< 生效 TCP（override 或查询默认）
    BatchDemands demands;             ///< 合并要求值（点∨工况——合并口径见类注）

    // ---- 结果槽（全序写回；初值＝未运行）----
    BatchItemStatus status = BatchItemStatus::NotRun;
    std::optional<IkOutcomeKind> outcomeKind;
    std::optional<KinematicSolution> bestSolution;
    std::vector<FilteredSolutionRecord> filteredRecords;
    std::optional<IkSearchRecord> searchRecord;
    std::optional<AnalyticBoundMaterial> proofMaterial;
    std::vector<BatchDemandCheck> demandChecks;
    bool collisionNotEvaluated = false;
};

/// 工作项全序比较键（§8.4——(pointOid, conditionId) 字典序；ObjectId
/// 的 operator<＝字节字典序，core §5.1）。
bool workOrderLess(const WorkState& a, const WorkState& b)
{
    if (a.pointOid != b.pointOid) {
        return a.pointOid < b.pointOid;
    }
    return a.conditionId < b.conditionId;
}

// ---------------------------------------------------------------------
// 装配期批量查询校验（调用方错误 fail-fast 轨——TaskPointsBatchEvaluator
// 类注错误分轨；NFR-COR-03 拒绝不钳制）
// ---------------------------------------------------------------------

/// 批量查询校验（违例抛 std::invalid_argument——评估输出面不承载调用方
/// 错误）。caseSubset 随请求到达（构造期不可见），其引用完备性在
/// evaluate 期校验——登记随卡 §14.6 v0.5。
void validateBatchQuery(const IKinRuntimeView& view, const BatchQuery& query)
{
    // 设备自由度（可动关节链序——T03/T04 同口径）。
    std::size_t dof = 0;
    for (const auto& j : view.model().chain().joints) {
        if (j.type != runtime::JointType::Fixed) {
            ++dof;
        }
    }

    // 排序参考构型（D-KIN-4——显式输入的契约面）。
    if (query.referenceQ.size() != dof) {
        throw std::invalid_argument(
            "kin.task-points-batch 查询非法：referenceQ 维度 "
            + std::to_string(query.referenceQ.size()) + " != 设备自由度 "
            + std::to_string(dof));
    }
    for (std::size_t k = 0; k < query.referenceQ.size(); ++k) {
        if (!std::isfinite(query.referenceQ[k])) {
            throw std::invalid_argument(
                "kin.task-points-batch 查询非法：referenceQ[" + std::to_string(k)
                + "] 非有限（NFR-COR-03：不置零）");
        }
    }

    // 批量执行参数（批大小/线程数——≥1；批大小默认 256 见头文件常量）。
    if (query.maxBatchSize == 0U) {
        throw std::invalid_argument("kin.task-points-batch 查询非法：maxBatchSize 须 ≥1");
    }
    if (query.threadCount == 0U) {
        throw std::invalid_argument("kin.task-points-batch 查询非法：threadCount 须 ≥1");
    }

    // 求解参数面（计数/阈值/种子——I-KIN-4：seed=0 拒绝不静默替换）。
    if (query.initialValuesCount == 0U) {
        throw std::invalid_argument("kin.task-points-batch 查询非法：初值数量须 ≥1");
    }
    if (query.iterationLimit == 0U) {
        throw std::invalid_argument("kin.task-points-batch 查询非法：迭代上限须 ≥1");
    }
    if (!std::isfinite(query.dedupThresholdPerAxis)
        || query.dedupThresholdPerAxis <= 0.0) {
        throw std::invalid_argument(
            "kin.task-points-batch 查询非法：去重阈值非法（须有限且>0，rad|m 逐轴）");
    }
    if (query.initialStrategy == InitialValueStrategy::SeededRandom
        && query.seed == 0U) {
        throw std::invalid_argument(
            "kin.task-points-batch 查询非法：SeededRandom 的 seed=0（I-KIN-4："
            "拒绝，不做 0→1 静默替换——NFR-COR-03）");
    }

    // 目标位姿全有限校验（逐点——KIN-TARGET-ILLEGAL 语义锚）。
    auto targetAllFinite = [](const rw::math::Transform3D<double>& t) {
        for (std::size_t i = 0; i < 3; ++i) {
            for (std::size_t j = 0; j < 3; ++j) {
                if (!std::isfinite(t.R()(i, j))) {
                    return false;
                }
            }
            if (!std::isfinite(t.P()[i])) {
                return false;
            }
        }
        return true;
    };

    // 逐点投影值（目标/容差/身份/要求值——重复 oid 合法（集合语义去重），
    // 但每个条目自身须良构）。
    for (const BatchTaskPoint& p : query.points) {
        if (!p.pointOid.isValid()) {
            throw std::invalid_argument(
                "kin.task-points-batch 查询非法：任务点 objectId 为保留值");
        }
        if (!targetAllFinite(p.targetInBase)) {
            throw std::invalid_argument(
                "kin.task-points-batch 查询非法：任务点 " + p.pointOid.toCanonical()
                + " 目标位姿含非有限分量（KIN-TARGET-ILLEGAL 语义锚——§5.5）");
        }
        if (!std::isfinite(p.positionTolerance) || p.positionTolerance <= 0.0) {
            throw std::invalid_argument(
                "kin.task-points-batch 查询非法：任务点 " + p.pointOid.toCanonical()
                + " 位置容差非法（须有限且>0，单位 m）");
        }
        if (!std::isfinite(p.orientationTolerance) || p.orientationTolerance <= 0.0) {
            throw std::invalid_argument(
                "kin.task-points-batch 查询非法：任务点 " + p.pointOid.toCanonical()
                + " 姿态容差非法（须有限且>0，单位 rad）");
        }
        if (p.demands.minimumJointMargin.has_value()
            && !std::isfinite(*p.demands.minimumJointMargin)) {
            throw std::invalid_argument(
                "kin.task-points-batch 查询非法：任务点 " + p.pointOid.toCanonical()
                + " 最小关节裕量要求值非有限");
        }
    }

    // 逐工况投影值（身份/悬空清单不在此拒——悬空引用是 InputInvalid
    // 素材而非装配违约，V-12 语义分轨）。
    for (const BatchCondition& c : query.conditions) {
        if (!c.conditionId.isValid()) {
            throw std::invalid_argument(
                "kin.task-points-batch 查询非法：工况 objectId 为保留值");
        }
        if (c.demands.minimumJointMargin.has_value()
            && !std::isfinite(*c.demands.minimumJointMargin)) {
            throw std::invalid_argument(
                "kin.task-points-batch 查询非法：工况 " + c.conditionId.toCanonical()
                + " 最小关节裕量要求值非有限");
        }
    }

    // TCP 键预检（defaultTcp＋全部 override——工具可解析时键不命中即装配
    // 违约；工具侧缺失保留到评估期＝批量级 KIN-NO-TCP 零素材轨，T04
    // 两分口径同源）。
    const runtime::CanonicalModel& model = view.model();
    auto precheckTcp = [&](const TcpRef& tcp, const char* role) {
        if (model.tools().empty()) {
            return;  // 工具侧缺失——评估期批量级 KIN-NO-TCP（零素材）
        }
        const auto loc = model.findObject(tcp.toolObject);
        if (loc.has_value() && loc->kind == runtime::CanonicalModel::ObjectKind::Tool) {
            const runtime::CanonicalTool& tool = model.tools().at(loc->index);
            if (!tcp.tcpKey.empty() && tcp.tcpKey != tool.localName) {
                throw std::invalid_argument(
                    std::string("kin.task-points-batch：") + role + " tcpKey '"
                    + tcp.tcpKey + "' 不命中工具 '" + tool.localName
                    + "' 的 canonical TCP（帧未解析——装配期 fail-fast）");
            }
        }
    };
    precheckTcp(query.defaultTcp, "defaultTcp");
    for (const BatchTaskPoint& p : query.points) {
        if (p.tcpOverride.has_value()) {
            precheckTcp(*p.tcpOverride, "任务点 override");
        }
    }
}

// ---------------------------------------------------------------------
// 要求值对比（实际/要求/单位——§7.1 逐项明细 demandsChecked 的组装）
// ---------------------------------------------------------------------

/// 合并点级与工况级要求值（合并口径：碰撞取"或"、裕量取"更强"＝可选值
/// 中的较大者——登记随卡 §14.6 v0.5；比较型单位随检查行记录）。
BatchDemands mergeDemands(const BatchDemands& point, const BatchDemands& condition)
{
    BatchDemands merged;
    merged.collisionFreeRequired =
        point.collisionFreeRequired || condition.collisionFreeRequired;
    if (point.minimumJointMargin.has_value()
        && condition.minimumJointMargin.has_value()) {
        merged.minimumJointMargin =
            std::max(*point.minimumJointMargin, *condition.minimumJointMargin);
    } else if (point.minimumJointMargin.has_value()) {
        merged.minimumJointMargin = point.minimumJointMargin;
    } else {
        merged.minimumJointMargin = condition.minimumJointMargin;
    }
    return merged;
}

/// 可动关节的单位 token 列（链序，跳过 Fixed——裕量实际值 arg-min 关节
/// 的天然单位来源；"rad"＝转动/连续、"m"＝移动）。
std::vector<const char*> movableJointUnits(const IKinRuntimeView& view)
{
    std::vector<const char*> units;
    for (const auto& j : view.model().chain().joints) {
        if (j.type == runtime::JointType::Fixed) {
            continue;
        }
        units.push_back(j.type == runtime::JointType::Prismatic ? "m" : "rad");
    }
    return units;
}

/// 组装单个求解完成项的要求值对比行（四类——残差两行恒产出（评价自身
/// 的要求值面）；碰撞/裕量仅声明的要求产出，登记随卡 §14.6 v0.5；
/// evaluated=false＝无解可查/碰撞未启用——证据缺失口径，绝不解读为满足
/// （KIN-05 同源））。
std::vector<BatchDemandCheck> buildDemandChecks(const WorkState& item,
                                                const std::vector<JointInterval>& intervals,
                                                const std::vector<const char*>& jointUnits)
{
    std::vector<BatchDemandCheck> checks;
    const KinematicSolution* best =
        item.bestSolution.has_value() ? &*item.bestSolution : nullptr;

    // 行 1/2：位姿残差 vs 点容差（实际值来自解的收敛复验——§6.1 值模型；
    // 要求值＝点容差）。
    {
        BatchDemandCheck pos;
        pos.kind = BatchDemandCheck::Kind::PositionResidual;
        pos.evaluated = best != nullptr;
        pos.satisfied = pos.evaluated && best->positionResidual <= item.positionTolerance;
        pos.actual = pos.evaluated ? best->positionResidual : 0.0;
        pos.required = item.positionTolerance;
        pos.unit = "m";
        checks.push_back(std::move(pos));
    }
    {
        BatchDemandCheck ori;
        ori.kind = BatchDemandCheck::Kind::OrientationResidual;
        ori.evaluated = best != nullptr;
        ori.satisfied =
            ori.evaluated && best->orientationResidual <= item.orientationTolerance;
        ori.actual = ori.evaluated ? best->orientationResidual : 0.0;
        ori.required = item.orientationTolerance;
        ori.unit = "rad";
        checks.push_back(std::move(ori));
    }

    // 行 3：无碰撞要求（声明时才产出——布尔要求无量纲；未启用碰撞＝
    // evaluated=false 证据缺失，绝不解读为满足）。
    if (item.demands.collisionFreeRequired) {
        BatchDemandCheck col;
        col.kind = BatchDemandCheck::Kind::CollisionFree;
        col.evaluated = best != nullptr && best->collisionStatus.evaluated;
        col.satisfied = col.evaluated && !best->collisionStatus.inCollision;
        col.unit = "-";
        checks.push_back(std::move(col));
    }

    // 行 4：最小关节裕量（声明时才产出——实际值＝解的归一化裕量（D-KIN-6）
    // 经评价区间半行程换算回绝对距离，取 arg-min 关节的天然单位）。
    if (item.demands.minimumJointMargin.has_value()) {
        BatchDemandCheck marg;
        marg.kind = BatchDemandCheck::Kind::MinJointMargin;
        marg.evaluated = best != nullptr;
        marg.required = *item.demands.minimumJointMargin;
        marg.unit = "-";
        if (best != nullptr) {
            // 逐有界关节换算：绝对距离_i ＝ margin_i × (行程_i / 2)。
            // continuous 无工作范围关节 margin=+∞、不产生有限距离——
            // 不参与 arg-min（全无有界关节时 actual=+∞ 恒满足）。
            double minDistance = std::numeric_limits<double>::infinity();
            std::size_t argMin = jointUnits.size();
            for (std::size_t k = 0; k < best->jointMargins.size() && k < intervals.size();
                 ++k) {
                if (!intervals[k].bounded
                    || !std::isfinite(best->jointMargins[k])) {
                    continue;
                }
                const double halfStroke =
                    (intervals[k].upper - intervals[k].lower) / 2.0;
                const double distance = best->jointMargins[k] * halfStroke;
                if (distance < minDistance) {
                    minDistance = distance;
                    argMin = k;
                }
            }
            marg.actual = minDistance;
            if (argMin < jointUnits.size()) {
                marg.unit = jointUnits[argMin];
            }
            marg.satisfied = marg.actual >= marg.required;
        }
        checks.push_back(std::move(marg));
    }
    return checks;
}

// ---------------------------------------------------------------------
// 单项求解与结局映射（IkOutcome→WorkState 结果槽；§5.4 铁律的批量侧
// 落点——取消不是结局，2/3/4 不得升级为不可行）
// ---------------------------------------------------------------------

/// 求解单个工作项并把结局写入结果槽（cancelled=true 时槽保持 NotRun——
/// 调用方据此中止批次；探针接线由调用方传入）。
void solveItem(WorkState& item, const BatchQuery& query, const IIkSolver& solver,
               const IKinRuntimeView& view, const std::vector<JointInterval>& intervals,
               const std::vector<std::vector<double>>& initialValues,
               const core::ContentIdentity& snapshotId,
               const core::ContentIdentity& sliceId,
               core::EvaluationMode mode,
               const std::function<bool()>& cancellationProbe)
{
    // 组装求解请求（§5.3 字段表——T04 单点同构；绑定面随项落定，§5.6）。
    IkRequest ikRequest;
    ikRequest.targetInBase = item.targetInBase;
    ikRequest.positionTolerance = item.positionTolerance;
    ikRequest.orientationTolerance = item.orientationTolerance;
    ikRequest.modelView = &view;
    ikRequest.tcp = item.tcp;
    ikRequest.iterationLimit = query.iterationLimit;
    ikRequest.intervals = intervals;
    ikRequest.initialValues = initialValues;
    ikRequest.dedupThresholdPerAxis = query.dedupThresholdPerAxis;
    ikRequest.collisionSession = query.collisionSession;
    ikRequest.cancellationProbe = cancellationProbe;
    ikRequest.referenceQ = query.referenceQ;
    ikRequest.targetRef.pointOid = item.pointOid;
    ikRequest.targetRef.conditionId = item.conditionId;
    ikRequest.requestIdentity.snapshotId = snapshotId;
    ikRequest.requestIdentity.sliceId = sliceId;
    ikRequest.requestIdentity.configDigest = query.configDigest;
    ikRequest.requestIdentity.mode = mode;
    ikRequest.requestIdentity.seed = query.seed;
    ikRequest.requestIdentity.referenceQ = query.referenceQ;

    const IkOutcome outcome = solver.solve(ikRequest);
    if (outcome.cancelled) {
        return;  // 取消不是结局——槽保持 NotRun（无终局字段可解读）
    }

    item.collisionNotEvaluated = outcome.collisionNotEvaluated;
    item.outcomeKind = outcome.outcomeKind;
    switch (outcome.outcomeKind) {
    case IkOutcomeKind::SolutionsFound:
    case IkOutcomeKind::PartialCollision:
        // 结局 1/4→CandidateFound（最佳解＝稳定排序首位——§7.1 bestSolution；
        // 碰撞解已经过滤/标记，任务结论不受单解影响）。
        item.status = BatchItemStatus::CandidateFound;
        item.bestSolution = outcome.solutionSet.solutions.front();
        item.filteredRecords = outcome.solutionSet.filteredRecords;
        break;
    case IkOutcomeKind::MultiInitNoConvergence:
    case IkOutcomeKind::AllCandidatesFiltered:
        // 结局 2/3→搜索未果素材（记录必附——不得输出不可行，C5/C8）。
        item.status = outcome.outcomeKind == IkOutcomeKind::MultiInitNoConvergence
                          ? BatchItemStatus::NoConvergence
                          : BatchItemStatus::AllFiltered;
        item.searchRecord = outcome.solutionSet.searchRecord;
        item.filteredRecords = outcome.solutionSet.filteredRecords;
        break;
    case IkOutcomeKind::AnalyticBoundExceeded:
        // 结局 5→BoundExceeded（证明素材随槽交付——仅素材不裁定；producer
        // 重绑在组装器，Evaluators.hpp/Evidence.cpp 口径注）。
        item.status = BatchItemStatus::BoundExceeded;
        item.proofMaterial = outcome.proofMaterial;
        break;
    }
}

/// 结果槽→BatchWorkItemRecord（预终结项的 reason 已在展开期落定；可解
/// 项的 reason 清空——presence 纪律）。
BatchWorkItemRecord assembleRecord(const WorkState& item)
{
    BatchWorkItemRecord record;
    record.pointOid = item.pointOid;
    record.conditionId = item.conditionId;
    record.pointLevel = item.pointLevel;
    record.status = item.status;
    record.outcomeKind = item.outcomeKind;
    record.bestSolution = item.bestSolution;
    record.filteredRecords = item.filteredRecords;
    record.searchRecord = item.searchRecord;
    record.proofMaterial = item.proofMaterial;
    record.demandChecks = item.demandChecks;
    record.collisionNotEvaluated = item.collisionNotEvaluated;
    record.reason = item.reason;
    return record;
}

/// 内置默认求解器（生产路径——无替身注入时使用；无状态静态实例，
/// §9.4"无状态建议共享"）。
const IkSolver& internalDefaultSolver()
{
    static const IkSolver s;
    return s;
}

}  // namespace

// =====================================================================
// TaskPointsBatchEvaluator——构造校验＋批量评估主流程
// =====================================================================

TaskPointsBatchEvaluator::TaskPointsBatchEvaluator(const IKinRuntimeView* view,
                                                   BatchQuery query)
    : m_view(view), m_query(std::move(query)),
      m_descriptor(makeTaskPointsBatchDescriptor())
{
    // 装配期 fail-fast（类注错误分轨）：空视图＝装配违约；查询非法＝
    // 调用方错误——两轨都不进入评估输出面（NFR-COR-03）。
    if (m_view == nullptr) {
        throw std::invalid_argument(
            "kin.task-points-batch：注入视图为空指针（宿主注入契约违约——O-37）");
    }
    validateBatchQuery(*m_view, m_query);

    // 工具侧缺失（未配置/悬空）保留到评估期＝批量级 KIN-NO-TCP 零素材
    // （T04 两分口径同源——键不命中已在 validateBatchQuery 拒绝）。逐个
    // 去重 TCP 引用核对：defaultTcp＋全部 override。
    const runtime::CanonicalModel& model = m_view->model();
    auto checkToolPresence = [&](const TcpRef& tcp) {
        if (model.tools().empty()) {
            KinematicsError err;
            err.code = KinematicsErrorCode::NoTcp;
            err.detail = "TCP 未配置：快照模型无工具（KIN-NO-TCP 素材——§9.6）";
            m_setupError = std::move(err);
            return;
        }
        const auto loc = model.findObject(tcp.toolObject);
        if (!loc.has_value()
            || loc->kind != runtime::CanonicalModel::ObjectKind::Tool) {
            KinematicsError err;
            err.code = KinematicsErrorCode::NoTcp;
            err.detail = "TCP 引用悬空：toolObject 未解析到快照工具"
                         "（KIN-NO-TCP 素材——§9.6）";
            m_setupError = std::move(err);
        }
    };
    if (!m_setupError.has_value()) {
        checkToolPresence(m_query.defaultTcp);
    }
    for (const BatchTaskPoint& p : m_query.points) {
        if (m_setupError.has_value()) {
            break;
        }
        if (p.tcpOverride.has_value()) {
            checkToolPresence(*p.tcpOverride);
        }
    }
}

const evidence::EvaluatorDescriptor& TaskPointsBatchEvaluator::descriptor() const
{
    // 返回引用指向成员副本（稳定存储——evidence §9.2 契约要求）。
    return m_descriptor;
}

evidence::EvaluationOutput TaskPointsBatchEvaluator::evaluate(
    const evidence::EvaluationRequest& request,
    evidence::IEvaluationContext& context)
{
    evidence::EvaluationOutput out;

    // ---- 批量级零素材轨：工具侧缺失（KIN-NO-TCP——T03/T04 两分口径：
    // 模型侧缺失≠工程不可行，零 payload 零证据，判定留 evidence）。----
    if (m_setupError.has_value()) {
        out.diagnostics.push_back(makeKinDiag(
            kKinNoTcp, std::nullopt,
            "kin.task-points-batch 批量任务点验证", m_setupError->detail,
            "配置工具 TCP 或修正 tcpRef 引用后重提批量评估"));
        return out;
    }

    // ---- 纯计算面（runBatchComputation——展开/分批/求解/取消/检查点/
    // 进度/完成矩阵）＋组装器交付（唯一组装点——证据行/搜索未果聚合/
    // 证明素材/verdictInputs/payload/诊断全部经 KinematicEvidenceBuilder，
    // NFR-MNT-04）。----
    const BatchComputation computation =
        runBatchComputation(*m_view, m_query, request, context);
    const KinematicEvidenceBuilder builder;
    const EvidenceContext ctx{&request};
    return builder.build(computation, ctx);
}

// =====================================================================
// runBatchComputation——批量计算核心（§7.1 批量执行图步骤 1~6；声明见
// Evaluators.hpp——测试直调面与评估器形态的分离线）
// =====================================================================

BatchComputation runBatchComputation(const IKinRuntimeView& view,
                                     const BatchQuery& query,
                                     const evidence::EvaluationRequest& request,
                                     evidence::IEvaluationContext& context)
{
    // 装配校验（直调面与评估器构造器同款 fail-fast 面——防御直调绕过
    // 构造器；NFR-COR-03）。
    validateBatchQuery(view, query);

    // ---- 展开前置：工况投影索引（重复 conditionId 集合语义去重——首见
    // 优先）＋caseSubset 规范化（去重＋字典序——完成矩阵素材对账分母）。----
    std::map<core::ObjectId, const BatchCondition*> conditionIndex;
    for (const BatchCondition& c : query.conditions) {
        conditionIndex.emplace(c.conditionId, &c);  // emplace＝重复键保留首见
    }
    std::vector<core::ObjectId> caseSubset = request.caseSubset;
    std::sort(caseSubset.begin(), caseSubset.end());
    caseSubset.erase(std::unique(caseSubset.begin(), caseSubset.end()),
                     caseSubset.end());
    // 请求装配完备性（调用方契约违约 fail-fast——caseSubset 引用的工况
    // 必须在投影集中；随请求到达故在评估期校验，登记随卡 §14.6 v0.5）。
    for (const core::ObjectId& caseId : caseSubset) {
        if (conditionIndex.find(caseId) == conditionIndex.end()) {
            throw std::invalid_argument(
                "kin.task-points-batch：caseSubset 引用投影集中不存在的工况 "
                + caseId.toCanonical() + "（请求装配契约违约——fail-fast）");
        }
    }

    // ---- 步骤 1：展开——启用点集×caseSubset 产 (point,condition) 工作项。
    // 点索引同样按集合语义去重（首见优先）；点遍历保持查询注入序（去重
    // 后相对序——确定性展开，排序在步骤 2 统一完成）。----
    std::map<core::ObjectId, const BatchTaskPoint*> pointIndex;
    std::vector<const BatchTaskPoint*> pointOrder;
    for (const BatchTaskPoint& p : query.points) {
        const bool inserted = pointIndex.emplace(p.pointOid, &p).second;
        if (inserted) {
            pointOrder.push_back(&p);
        }
    }

    // 悬空引用核对集（逐工况的 Stations 显式清单——不在点集的对象产
    // InputInvalid 工作项，V-12；集合语义去重防清单内重复登记膨胀）。
    std::vector<WorkState> workItems;
    std::set<std::pair<core::ObjectId, core::ObjectId>> seenKeys;
    auto appendItem = [&](WorkState item) {
        const auto key = std::make_pair(item.pointOid, item.conditionId);
        if (seenKeys.insert(key).second) {
            workItems.push_back(std::move(item));
        }
    };

    for (const core::ObjectId& caseId : caseSubset) {
        const BatchCondition& condition = *conditionIndex[caseId];

        // 悬空引用素材（V-12——先于 enabled/None 判定：数据缺陷必须显性
        // 化，不因停用被吞，ERR-01）。
        if (condition.appliesTo == BatchAppliesToScope::Stations) {
            std::set<core::ObjectId> uniqueStations(condition.stations.begin(),
                                                    condition.stations.end());
            for (const core::ObjectId& station : uniqueStations) {
                if (pointIndex.find(station) == pointIndex.end()) {
                    WorkState dangling;
                    dangling.pointOid = station;
                    dangling.conditionId = caseId;
                    dangling.kind = WorkKind::InputInvalid;
                    dangling.status = BatchItemStatus::InputInvalid;
                    dangling.reason =
                        "任务点引用悬空：工况适用范围引用点集外对象 "
                        + station.toCanonical()
                        + "（InputInvalid 素材——V-12，附 KIN-POINT-REF-DANGLING）";
                    appendItem(std::move(dangling));
                }
            }
        }

        // 逐点展开（appliesTo 过滤：仅 Stations 清单外不产项——§7.1 过滤
        // 语义；AllStations/None 都产项——None 在下方显式标记
        // NotApplicable，ERR-01 不静默丢弃）。
        for (const BatchTaskPoint* p : pointOrder) {
            const bool applicable =
                condition.appliesTo != BatchAppliesToScope::Stations
                || std::find(condition.stations.begin(), condition.stations.end(),
                             p->pointOid)
                    != condition.stations.end();
            if (!applicable) {
                continue;  // appliesTo 过滤——不产工作项（§7.1 过滤语义）
            }

            WorkState item;
            item.pointOid = p->pointOid;
            item.conditionId = caseId;
            item.pointLevel = p->level;
            item.targetInBase = p->targetInBase;
            item.positionTolerance = p->positionTolerance;
            item.orientationTolerance = p->orientationTolerance;
            item.tcp = p->tcpOverride.has_value() ? *p->tcpOverride
                                                  : query.defaultTcp;
            item.demands = mergeDemands(p->demands, condition.demands);

            // 预终结判定（优先级：点停用→工况停用→范围 None——显式标记
            // 附原因；其余为可解项）。
            if (!p->enabled) {
                item.kind = WorkKind::NotApplicable;
                item.status = BatchItemStatus::NotApplicable;
                item.reason = "任务点停用（disabled——NotApplicable 显式标记，§7.1）";
            } else if (!condition.enabled) {
                item.kind = WorkKind::NotApplicable;
                item.status = BatchItemStatus::NotApplicable;
                item.reason = "工况停用（disabled——NotApplicable 显式标记，§7.1）";
            } else if (condition.appliesTo == BatchAppliesToScope::None) {
                item.kind = WorkKind::NotApplicable;
                item.status = BatchItemStatus::NotApplicable;
                item.reason = "工况适用范围=None（NotApplicable 显式标记，§7.1）";
            } else {
                item.kind = WorkKind::Solvable;
                // 可解项保持 NotRun 初值——求解完成后覆写终态；取消路径
                // 的 NotRun 即此初值的自然保留（无伪完成）。
            }
            appendItem(std::move(item));
        }
    }

    // ---- 步骤 2：全序——(pointOid, conditionId) 字典序（§8.4，分片前
    // 全序确定；去重后严格全序——排序稳定性无观测面）。----
    std::sort(workItems.begin(), workItems.end(), workOrderLess);

    // 可解项下标集合（预终结项不入批次——终态展开期已定，登记随卡
    // §14.6 v0.5；acceptance 2 自检公式的"Σ批项数"＝已求解项数＋预终结
    // 项数的展开期完成部分）。
    std::vector<std::size_t> solvable;
    std::uint64_t preTerminalCount = 0;
    for (std::size_t i = 0; i < workItems.size(); ++i) {
        if (workItems[i].kind == WorkKind::Solvable) {
            solvable.push_back(i);
        } else {
            ++preTerminalCount;
        }
    }

    // 求解环境（逐项共享——同一快照视图/评价区间/初值集：确定性来源
    // 全部固定，逐项输入仅目标/TCP/绑定不同）。
    const std::vector<JointInterval> intervals = evaluationIntervals(view);
    const std::vector<std::vector<double>> initialValues =
        makeInitialValues(query.initialStrategy, query.initialValuesCount,
                          query.seed, intervals, query.referenceQ);
    const std::vector<const char*> jointUnits = movableJointUnits(view);
    const IIkSolver& solver =
        query.solver != nullptr ? *query.solver : internalDefaultSolver();

    // ---- 步骤 3：分批（每批 ≤maxBatchSize——默认 256，D-KIN-6）。----
    const std::uint64_t batchCount =
        solvable.empty()
            ? 0U
            : (static_cast<std::uint64_t>(solvable.size()) + query.maxBatchSize - 1U)
                  / query.maxBatchSize;

    // 取消传播旗标（批内共享——任一项观测到取消即停止批内派发；原子
    // 松散序足够：只做"是否停止"的保守判定，false 告警的代价＝多解一项，
    // 不影响素材正确性）。
    std::atomic<bool> batchCancelled{false};
    std::uint64_t completedItemsForProgress = preTerminalCount;  // 进度分母基准
    std::uint64_t completedBatches = 0;

    // 单项求解闭包（批内并行时各线程只写自己的结果槽——§8.4 合并＝按
    // 全序槽位归并，与完成顺序无关）。
    auto solveAt = [&](std::size_t slot) {
        // 批内项起点取消检查（批间"每批至少一次"之外的自适应粒度——
        // V-22 本单元侧：观测到取消即不再派发新项/新批）。
        if (batchCancelled.load(std::memory_order_relaxed)
            || context.cancellationRequested()) {
            batchCancelled.store(true, std::memory_order_relaxed);
            return;
        }
        WorkState& item = workItems[solvable[slot]];
        solveItem(item, query, solver, view, intervals, initialValues,
                  request.snapshot.snapshotId, request.slice.sliceId,
                  request.mode,
                  [&context, &batchCancelled]() {
                      if (context.cancellationRequested()) {
                          batchCancelled.store(true, std::memory_order_relaxed);
                          return true;
                      }
                      return false;
                  });
        if (item.status == BatchItemStatus::NotRun) {
            // 求解器报告取消（取消不是结局——槽保持 NotRun，中止批内
            // 后续项派发）。
            batchCancelled.store(true, std::memory_order_relaxed);
            return;
        }
        item.demandChecks = buildDemandChecks(item, intervals, jointUnits);
    };

    for (std::uint64_t b = 0; b < batchCount; ++b) {
        // ---- 步骤 4 前置：批间协作取消查询（每批至少一次——V-22 本单元
        // 侧；ARCH §4.4 的 2 s 停止派发界由本查询点＋批内探针承载）。----
        if (context.cancellationRequested()) {
            batchCancelled.store(true, std::memory_order_relaxed);
        }
        if (batchCancelled.load(std::memory_order_relaxed)) {
            break;  // 停止派发新批——剩余可解项保持 NotRun（无伪完成）
        }

        // 本批的连续槽区间（§8.4——并行分片＝全序工作项的连续区间）。
        const std::size_t begin =
            static_cast<std::size_t>(b) * query.maxBatchSize;
        const std::size_t end =
            std::min<std::size_t>(begin + query.maxBatchSize, solvable.size());

        // 分片执行（threadCount=1 内联单线程——逐位一致；>1 时 T 个连续
        // 分片并行，各线程只写各自槽位，join 后按分片序重抛首个异常——
        // 确定性失败面）。
        const std::size_t threads =
            std::min<std::size_t>(query.threadCount, end - begin);
        if (threads <= 1) {
            for (std::size_t slot = begin; slot < end; ++slot) {
                solveAt(slot);
                if (batchCancelled.load(std::memory_order_relaxed)) {
                    break;  // 批内中止——剩余槽走 NotRun（循环外统一处理）
                }
            }
        } else {
            std::vector<std::exception_ptr> shardErrors(threads);
            auto shardWorker = [&](std::size_t t) {
                try {
                    // 第 t 片＝[begin + t·n/T, begin + (t+1)·n/T)——连续
                    // 区间（n＝批内槽数）；区间边界整除余数摊入末片。
                    const std::size_t n = end - begin;
                    const std::size_t lo = begin + t * n / threads;
                    const std::size_t hi = begin + (t + 1) * n / threads;
                    for (std::size_t slot = lo; slot < hi; ++slot) {
                        if (batchCancelled.load(std::memory_order_relaxed)) {
                            return;
                        }
                        solveAt(slot);
                    }
                } catch (...) {
                    shardErrors[t] = std::current_exception();
                }
            };
            std::vector<std::thread> workers;
            workers.reserve(threads - 1);
            for (std::size_t t = 1; t < threads; ++t) {
                workers.emplace_back(shardWorker, t);
            }
            shardWorker(0);  // 主线程跑第 0 片——避免空等
            for (std::thread& w : workers) {
                w.join();
            }
            // 异常按分片序重抛首个（确定性失败面——同一坏输入必报同一
            // 首错，NFR-COR-02；批内取消旗标不是异常）。
            for (std::size_t t = 0; t < threads; ++t) {
                if (shardErrors[t] != nullptr) {
                    std::rethrow_exception(shardErrors[t]);
                }
            }
        }

        if (batchCancelled.load(std::memory_order_relaxed)) {
            break;  // 批内取消/失败中断——本批剩余与后续批不推进 watermark
        }

        // ---- 步骤 5：批完成——检查点 watermark（批粒度，P-KIN-7 最小
        // 端口）＋进度上报（批粒度，percent＝已完成项占比，phase 固定
        // "solve-batch"——§7.1 reportProgress(percent, phase) 批粒度落值）。----
        ++completedBatches;
        if (query.checkpointSink != nullptr) {
            query.checkpointSink->batchWatermark(completedBatches, batchCount);
        }
        completedItemsForProgress += (end - begin);
        const std::uint64_t percent =
            workItems.empty()
                ? 100U
                : std::min<std::uint64_t>(
                    100U, completedItemsForProgress * 100U
                              / static_cast<std::uint64_t>(workItems.size()));
        context.reportProgress(static_cast<std::uint8_t>(percent), "solve-batch");
    }

    // ---- 取消/中断收尾：未求解可解项如实 NotRun（§7.1"取消→未完成批
    // 如实标记 NotRun"；以"槽仍处于 NotRun 初值且无原因文本"判定未派发
    // ——批内部分完成时已解项的终态如实保留，不回写 NotRun）。----
    const bool incomplete = batchCancelled.load(std::memory_order_relaxed);
    std::uint64_t notRunCount = 0;
    for (const std::size_t slot : solvable) {
        WorkState& item = workItems[slot];
        if (item.status == BatchItemStatus::NotRun && item.reason.empty()) {
            item.reason =
                "未运行：协作取消/失败中止后未派发（NotRun 如实标记——"
                "已完成批 " + std::to_string(completedBatches) + "/"
                + std::to_string(batchCount) + "，watermark 保留可续）";
            ++notRunCount;
        }
    }
    // 计数守恒（Σ批项数＝预终结项＋已求解项；NotRun＝未派发项——
    // acceptance 2 自检公式的三个元）。
    const std::uint64_t processedCount =
        preTerminalCount + (static_cast<std::uint64_t>(solvable.size()) - notRunCount);

    // ---- 步骤 6：完成矩阵素材＋完整性自检＋组装器交付。----
    BatchComputation computation;
    computation.snapshotId = request.snapshot.snapshotId;
    computation.sliceId = request.slice.sliceId;
    computation.configDigest = query.configDigest;
    computation.mode = request.mode;
    computation.seed = query.seed;
    computation.referenceQ = query.referenceQ;
    computation.task = request.task;
    computation.caseSubset = caseSubset;
    computation.batchCount = batchCount;
    computation.completedBatchCount = completedBatches;

    // 逐工况完成标记（acceptance 2——caseSubset 每项有终态标记；口径：
    // 任一 NotRun→notRun；否则全 NotApplicable 或零项→notApplicable；
    // 否则 executed（含 InputInvalid——该项评估已完成并留素材））。登记
    // 随卡 §14.6 v0.5。
    for (const core::ObjectId& caseId : caseSubset) {
        BatchCaseCompletion cc;
        cc.conditionId = caseId;
        std::uint64_t computed = 0;
        std::uint64_t notRun = 0;
        std::uint64_t notApplicable = 0;
        for (const WorkState& item : workItems) {
            if (item.conditionId != caseId) {
                continue;
            }
            switch (item.status) {
            case BatchItemStatus::NotRun:
                ++notRun;
                break;
            case BatchItemStatus::NotApplicable:
                ++notApplicable;
                break;
            default:
                ++computed;
                break;  // CandidateFound/NoConvergence/AllFiltered/BoundExceeded/
                        // InputInvalid——计算/素材终态
            }
        }
        cc.computedItemCount = computed;
        cc.notRunItemCount = notRun;
        cc.notRun = notRun > 0;
        cc.notApplicable = !cc.notRun && computed == 0;  // 全不适用（含零项）
        cc.executed = !cc.notRun && !cc.notApplicable;
        computation.caseCompletion.push_back(cc);
    }

    // 工作项记录（全序——证据序＝工作项序，§8.4）。
    for (const WorkState& item : workItems) {
        computation.items.push_back(assembleRecord(item));
    }
    computation.totalWorkItems = static_cast<std::uint64_t>(computation.items.size());
    computation.processedItemCount = processedCount;
    computation.notRunItemCount = notRunCount;
    computation.incomplete = incomplete;

    // 组装前自检（acceptance 2——工作项总数＝Σ批项数＋NotRun 数；此处
    // 为评估器侧主检，组装器复核同式；违例＝内部缺陷 logic_error 不静默）。
    if (computation.totalWorkItems
        != computation.processedItemCount + computation.notRunItemCount) {
        throw std::logic_error(
            "kin.task-points-batch：完整性自检失败（工作项总数≠Σ批项数＋"
            "NotRun 数——acceptance 2/§7.1；内部缺陷，fail-fast）");
    }


    // 计算核心到此为止（证据组装唯一经 KinematicEvidenceBuilder——调用方
    // 侧分工：TaskPointsBatchEvaluator::evaluate ＝ TCP 零素材轨＋本函数＋
    // builder.build；测试直调本函数断言批量语义，NFR-MNT-01）。
    return computation;
}

// =====================================================================
// TaskPointsBatchEvaluatorFactory——宿主注入工厂（闭包捕获；create 无参）
// =====================================================================

TaskPointsBatchEvaluatorFactory::TaskPointsBatchEvaluatorFactory(
    const IKinRuntimeView* view, BatchQuery query)
    : m_view(view), m_query(std::move(query)),
      m_descriptor(makeTaskPointsBatchDescriptor())
{
    // 与评估器同一装配校验面（工厂是宿主的注入入口——违约在装配期
    // 暴露，不迟至 create()；validateBatchQuery 为本 TU 匿名命名空间的
    // 同一校验函数——TaskPointsBatchEvaluator 构造器同款调用）。
    if (m_view == nullptr) {
        throw std::invalid_argument(
            "kin.task-points-batch 工厂：注入视图为空指针（宿主注入契约违约——O-37）");
    }
    validateBatchQuery(*m_view, m_query);
}

const evidence::EvaluatorDescriptor& TaskPointsBatchEvaluatorFactory::descriptor() const
{
    return m_descriptor;
}

std::unique_ptr<evidence::IEngineeringEvaluator>
TaskPointsBatchEvaluatorFactory::create() const
{
    // 无参签名（O-37 裁决——注册表兼容）；视图/求解器/检查点指针经闭包
    // 传递——注入语义的唯一通道（生命周期约束见 BatchQuery 注）。
    return std::make_unique<TaskPointsBatchEvaluator>(m_view, m_query);
}

// =====================================================================
// makeTaskPointsBatchDescriptor——依赖声明与形态（§4.3 行）
// =====================================================================

evidence::EvaluatorDescriptor makeTaskPointsBatchDescriptor()
{
    evidence::EvaluatorDescriptor d;
    d.key = kTaskPointsBatchEvaluationKey;
    d.contractVersion = kTaskPointsBatchContractVersion;

    // 依赖声明七条（§4.3 行原样：pose-metrics 五条＋req.points＋
    // req.conditions——"上述＋req.conditions(Object)（caseSubset 分批）"；
    // 全部 Required——批量通道无条件依赖；collision-models 不在行内，
    // 碰撞声明属 region-coverage/T07）。resolutionNote 供人工评审
    // （非身份载体，无语法约束）。
    d.inputs = {
        {"model.robot-design", evidence::DependencyKind::Object,
         evidence::DependencyRequiredness::Required, std::nullopt,
         "规范机器人链对象（Object 闭包内 robot-design——链/关节/限位真值）"},
        {"tcp", evidence::DependencyKind::Object,
         evidence::DependencyRequiredness::Required, std::nullopt,
         "工具定义对象（Object→tool-definition——TCP 偏置真值，AT-05① 失效面）"},
        {"policy.resolved", evidence::DependencyKind::Policy,
         evidence::DependencyRequiredness::Required, std::nullopt,
         "已解析工程策略内容身份（CON-06——策略变更全列失效）"},
        {"namemap", evidence::DependencyKind::NameMap,
         evidence::DependencyRequiredness::Required, std::nullopt,
         "运行时名称映射内容身份（CON-06——⑥端口诊断定位/结果标注）"},
        {"config.ik", evidence::DependencyKind::Configuration,
         evidence::DependencyRequiredness::Required, std::nullopt,
         "分析求解配置（KIN-13 canonical——入 sliceId 不入基准，D-04）"},
        {"req.points", evidence::DependencyKind::Object,
         evidence::DependencyRequiredness::Required, std::nullopt,
         "任务点集合对象（req-point-set——批量展开的启用点集分母）"},
        {"req.conditions", evidence::DependencyKind::Object,
         evidence::DependencyRequiredness::Required, std::nullopt,
         "工况集合对象（req-condition-set——caseSubset 分批的适用范围/"
         "要求值来源，§4.3 行原文）"},
    };

    // Profile 声明引用：域 id 词表 "kin"（evidence isDomainProfileId）；
    // contentIdentity 置零值＝域不可申报（evidence §9.5/R-3）。
    d.profile.profileId = "kin";
    d.profile.version = "1";
    d.profile.contentIdentity = core::ContentIdentity{};

    // 模式集：§4.3 行两值（Quick/Verified——批量通道不做 Preview；Quick
    // 载荷以 mode 字段承载 screening-only 语义，效力门禁在汇总/包络层）。
    d.supportedModes = {core::EvaluationMode::Quick, core::EvaluationMode::Verified};

    // 无跨调用状态（实例可共享）＋完全线程安全（§9.2 头注原文——并行
    // 分片要求可重入）。
    d.stateless = true;
    d.threadSafety = evidence::ThreadSafety::FullyThreadSafe;
    return d;
}

// =====================================================================
// taskPointsBatchCapability——任务类型能力声明（§8.3 提交行值面）
// =====================================================================

execution::TaskCapability taskPointsBatchCapability()
{
    execution::TaskCapability capability;
    // 暂停不支持（R1 如实声明——收到暂停请求给明确状态反馈，EX-SM-7；
    // 缺省即 false，显式置位以声明语义）。
    capability.supportsPause = false;
    // 检查点＝批 watermark（§8.3 能力声明原文——CheckpointGranularity::
    // Batch；watermark 产出面＝IBatchCheckpointSink 每批一次）。
    capability.checkpointGranularity = execution::CheckpointGranularity::Batch;
    // 强制终止代价＝低（批间边界即安全中止点，无跨批不变量——§8.3
    // "强制终止代价=低"原文；仅呈现语义，不影响协议，TaskTypes 注）。
    capability.forceTerminateCost = execution::ForceTerminateCost::Cheap;
    return capability;
}

// =====================================================================
// 以下为 T06 区域覆盖段（WP-15-T06——§7.2 覆盖率图的评估器落位；既有
// T03~T05 段不回改。同 TU 匿名命名空间前段工具（makeKinDiag/
// internalDefaultSolver 等）按声明序可见——复用不重定义）
// =====================================================================

namespace {

// ---------------------------------------------------------------------
// 装配期区域覆盖查询校验（调用方错误 fail-fast 轨——WorkspaceSampler
// 类注错误分轨；NFR-COR-03 拒绝不钳制）
// ---------------------------------------------------------------------

/// 区域覆盖查询校验（违例抛 std::invalid_argument——评估输出面不承载
/// 调用方错误）。身份对账随请求到达（构造期不可见），其在 evaluate 期
/// 结构化诊断轨执行——分轨登记随卡 §14.6 v0.6。
void validateRegionCoverageQuery(const IKinRuntimeView& view,
                                 const RegionCoverageQuery& query)
{
    // 设备自由度（可动关节链序——T03~T05 同口径）。
    std::size_t dof = 0;
    for (const auto& j : view.model().chain().joints) {
        if (j.type != runtime::JointType::Fixed) {
            ++dof;
        }
    }

    // 排序参考构型（D-KIN-4——显式输入的契约面）。
    if (query.referenceQ.size() != dof) {
        throw std::invalid_argument(
            "kin.region-coverage 查询非法：referenceQ 维度 "
            + std::to_string(query.referenceQ.size()) + " != 设备自由度 "
            + std::to_string(dof));
    }
    for (std::size_t k = 0; k < query.referenceQ.size(); ++k) {
        if (!std::isfinite(query.referenceQ[k])) {
            throw std::invalid_argument(
                "kin.region-coverage 查询非法：referenceQ[" + std::to_string(k)
                + "] 非有限（NFR-COR-03：不置零）");
        }
    }

    // 求解参数面（计数/容差/阈值——I-KIN-4：seed=0 拒绝不静默替换）。
    if (query.initialValuesCount == 0U) {
        throw std::invalid_argument("kin.region-coverage 查询非法：初值数量须 ≥1");
    }
    if (query.iterationLimit == 0U) {
        throw std::invalid_argument("kin.region-coverage 查询非法：迭代上限须 ≥1");
    }
    if (!std::isfinite(query.positionTolerance) || query.positionTolerance <= 0.0) {
        throw std::invalid_argument(
            "kin.region-coverage 查询非法：位置容差非法（须有限且>0，单位 m）");
    }
    if (!std::isfinite(query.orientationTolerance)
        || query.orientationTolerance <= 0.0) {
        throw std::invalid_argument(
            "kin.region-coverage 查询非法：姿态容差非法（须有限且>0，单位 rad）");
    }
    if (!std::isfinite(query.dedupThresholdPerAxis)
        || query.dedupThresholdPerAxis <= 0.0) {
        throw std::invalid_argument(
            "kin.region-coverage 查询非法：去重阈值非法（须有限且>0，rad|m 逐轴）");
    }
    if (query.initialStrategy == InitialValueStrategy::SeededRandom
        && query.budget.seed == 0U) {
        throw std::invalid_argument(
            "kin.region-coverage 查询非法：SeededRandom 的 seed=0（I-KIN-4："
            "拒绝，不做 0→1 静默替换——NFR-COR-03）");
    }
    // 采样预算种子全域非 0（Random 位置采样的序列源——Grid 不消费但种子
    // 入 sampleSetIdentity，0 种子的身份面无意义且违反 I-KIN-4 统一口径）。
    if (query.budget.seed == 0U) {
        throw std::invalid_argument(
            "kin.region-coverage 查询非法：budget.seed=0（I-KIN-4：拒绝——"
            "采样种子是样本集身份的决定输入）");
    }
    if (query.budget.threadCount == 0U) {
        throw std::invalid_argument("kin.region-coverage 查询非法：threadCount 须 ≥1");
    }
    if (query.maxBatchSize == 0U) {
        throw std::invalid_argument("kin.region-coverage 查询非法：maxBatchSize 须 ≥1");
    }

    // 逐计划投影值（身份/几何/计数——I-REQ-6 非退化与 requirements ≥1
    // 词表的装配期面；计数 0 合法（零样本场景），此处不拒）。
    for (const SamplingPlan& plan : query.plans) {
        if (!plan.regionObjectId.isValid()) {
            throw std::invalid_argument(
                "kin.region-coverage 查询非法：区域 objectId 为保留值");
        }
        if (!plan.planContentIdentity.isValid()) {
            throw std::invalid_argument(
                "kin.region-coverage 查询非法：计划内容身份为零值（区域 "
                + plan.regionObjectId.toCanonical()
                + "——计划 canonical 身份是 sampleSetIdentity 的输入，宿主"
                  "组装违约）");
        }
        for (std::size_t i = 0; i < 3; ++i) {
            if (!std::isfinite(plan.box.size[i]) || plan.box.size[i] <= 0.0) {
                throw std::invalid_argument(
                    "kin.region-coverage 查询非法：区域 "
                    + plan.regionObjectId.toCanonical() + " Box size["
                    + std::to_string(i) + "] 非正/非有限（I-REQ-6 区域非退化"
                    "——单位 m）");
            }
            if (!std::isfinite(plan.box.center[i])) {
                throw std::invalid_argument(
                    "kin.region-coverage 查询非法：区域 "
                    + plan.regionObjectId.toCanonical() + " Box center["
                    + std::to_string(i) + "] 非有限（单位 m）");
            }
        }
        if (plan.orientation.directionSamples == 0U || plan.orientation.rollSamples == 0U) {
            throw std::invalid_argument(
                "kin.region-coverage 查询非法：区域 "
                + plan.regionObjectId.toCanonical()
                + " 姿态采样计数须 ≥1（requirements 字段表原文）");
        }
    }

    // 逐工况投影值（身份良构——要求值合并的消费面）。
    for (const BatchCondition& c : query.conditions) {
        if (!c.conditionId.isValid()) {
            throw std::invalid_argument(
                "kin.region-coverage 查询非法：工况 objectId 为保留值");
        }
    }

    // TCP 键预检（defaultTcp——工具可解析时键不命中即装配违约；工具侧
    // 缺失保留到评估期＝批量级 KIN-NO-TCP 零素材轨，T04/T05 两分口径）。
    const runtime::CanonicalModel& model = view.model();
    const TcpRef& tcp = query.defaultTcp;
    if (!model.tools().empty()) {
        const auto loc = model.findObject(tcp.toolObject);
        if (loc.has_value() && loc->kind == runtime::CanonicalModel::ObjectKind::Tool) {
            const runtime::CanonicalTool& tool = model.tools().at(loc->index);
            if (!tcp.tcpKey.empty() && tcp.tcpKey != tool.localName) {
                throw std::invalid_argument(
                    "kin.region-coverage：defaultTcp tcpKey '" + tcp.tcpKey
                    + "' 不命中工具 '" + tool.localName
                    + "' 的 canonical TCP（帧未解析——装配期 fail-fast）");
            }
        }
    }
}

// ---------------------------------------------------------------------
// 碰撞要求合并（KIN-05 承接的判定面——RegionCoverageQuery 结构体注的
// 合并口径落点）
// ---------------------------------------------------------------------

/// 区域覆盖的碰撞要求判定（任一计划级要求或任一启用工况要求——覆盖
/// 评估运行于任务工况语境；V13-01：无布尔开关，会话在场即启用）。
bool collisionRequiredForPlan(const RegionCoverageQuery& query,
                              const SamplingPlan& plan)
{
    if (plan.demands.collisionFreeRequired) {
        return true;
    }
    for (const BatchCondition& c : query.conditions) {
        if (c.enabled && c.demands.collisionFreeRequired) {
            return true;
        }
    }
    return false;
}

/// 位置样本评估的"姿态无约束"残差容差落值（rad——角度型残差量程 [0,π]，
/// π 容差接受任意姿态＝存在性口径；收敛判据逐项比较，故收敛⇔位置残差
/// 入容差——黄金锁定随 T13，登记随卡 §14.6 v0.6）。
constexpr double kPoseFreeTolerance = 3.14159265358979323846;

}  // namespace

// =====================================================================
// WorkspaceSampler——构造校验＋评估主流程（§7.2 覆盖率图）
// =====================================================================

WorkspaceSampler::WorkspaceSampler(const IKinRuntimeView* view,
                                   RegionCoverageQuery query)
    : m_view(view), m_query(std::move(query)),
      m_descriptor(makeRegionCoverageDescriptor())
{
    // 装配期 fail-fast（类注错误分轨）：空视图＝装配违约；查询非法＝
    // 调用方错误——两轨都不进入评估输出面（NFR-COR-03）。
    if (m_view == nullptr) {
        throw std::invalid_argument(
            "kin.region-coverage：注入视图为空指针（宿主注入契约违约——O-37）");
    }
    validateRegionCoverageQuery(*m_view, m_query);

    // 工具侧缺失（未配置/悬空）保留到评估期＝批量级 KIN-NO-TCP 零素材
    // （T05 两分口径同源——键不命中已在 validateRegionCoverageQuery 拒绝）。
    const runtime::CanonicalModel& model = m_view->model();
    if (model.tools().empty()) {
        KinematicsError err;
        err.code = KinematicsErrorCode::NoTcp;
        err.detail = "TCP 未配置：快照模型无工具（KIN-NO-TCP 素材——§9.6）";
        m_setupError = std::move(err);
    } else {
        const auto loc = model.findObject(m_query.defaultTcp.toolObject);
        if (!loc.has_value()
            || loc->kind != runtime::CanonicalModel::ObjectKind::Tool) {
            KinematicsError err;
            err.code = KinematicsErrorCode::NoTcp;
            err.detail = "TCP 引用悬空：toolObject 未解析到快照工具"
                         "（KIN-NO-TCP 素材——§9.6）";
            m_setupError = std::move(err);
        }
    }
}

const evidence::EvaluatorDescriptor& WorkspaceSampler::descriptor() const
{
    // 返回引用指向成员副本（稳定存储——evidence §9.2 契约要求）。
    return m_descriptor;
}

runtime::Expected<SampleSet, KinematicsError> WorkspaceSampler::generateSamples(
    const SamplingPlan& plan, const RegionSamplingBudget& budget) const
{
    // §9.2 独立入口（契约测试/检查点续跑核对 sampleSetIdentity 用）——
    // 单计划面委托多计划计算核（单元素向量＝计划内局部序 0 起，与全局
    // 序一致）。装配校验的轻量面：计划级良构在此复检（无视图上下文，
    // 几何/计数即可判定；身份/种子违约同轨拒绝）。
    try {
        return runtime::Expected<SampleSet, KinematicsError>::ok(
            generateSampleSet({plan}, budget));
    } catch (const std::exception& e) {
        // 生成核的自检 logic_error＝实现缺陷（fail-fast 语义）；此处转
        // Expected 错误侧承载——§9.1"非异常出口"的接口契约形态。
        KinematicsError err;
        err.code = KinematicsErrorCode::IllegalQ;
        err.detail = std::string("样本生成失败：") + e.what();
        return runtime::Expected<SampleSet, KinematicsError>::err(std::move(err));
    }
}

CoverageResult WorkspaceSampler::computeCoverage(const SampleSet& set,
                                                 const SampleResultSet& results) const
{
    // 唯一实现点委托（NFR-MNT-04——接口方法零本地副本）。
    return kinematics::computeCoverage(set, results);
}

evidence::EvaluationOutput WorkspaceSampler::evaluate(
    const evidence::EvaluationRequest& request,
    evidence::IEvaluationContext& context)
{
    evidence::EvaluationOutput out;

    // ---- 批量级零素材轨：工具侧缺失（KIN-NO-TCP——T03/T04/T05 两分
    // 口径：模型侧缺失≠工程不可行，零 payload 零证据，判定留 evidence）。----
    if (m_setupError.has_value()) {
        out.diagnostics.push_back(makeKinDiag(
            kKinNoTcp, std::nullopt,
            "kin.region-coverage 区域覆盖评估", m_setupError->detail,
            "配置工具 TCP 或修正 tcpRef 引用后重提区域覆盖评估"));
        return out;
    }

    // ---- 步骤 1：身份对账（先于生成——绝不沿用未冻结/不一致样本集，
    // acceptance 3/O-38：冻结前以快照 SamplingPlanRef 对账为准，不一致即
    // DataInsufficient＝证据缺失）。----
    std::vector<PlanIdentityCheck> checks;
    checks.reserve(m_query.plans.size());
    std::vector<const SamplingPlan*> mismatched;
    for (const SamplingPlan& plan : m_query.plans) {
        PlanIdentityCheck check;
        check.regionObjectId = plan.regionObjectId;
        check.planContentIdentity = plan.planContentIdentity;
        check.plannedPositionSamples = plannedPositionSampleCount(plan);
        check.plannedPoseSamples = plannedPoseSampleCount(plan);
        check.computedIdentity =
            sampleSetIdentity(plan.planContentIdentity, m_query.budget);

        // 快照冻结凭据核对（evidence §4.1.4——SamplingPlanRef 五字段中
        // 本单元可核对的三元：身份＋分母两值；regionObjectId 为对账键）。
        const std::vector<evidence::SamplingPlanRef>& refs
            = request.snapshot.samplingPlans;
        const auto it = std::find_if(
            refs.begin(), refs.end(),
            [&plan](const evidence::SamplingPlanRef& r) {
                return r.regionObjectId == plan.regionObjectId;
            });
        if (it == refs.end()) {
            // 未冻结：快照无该区域的 SamplingPlanRef——绝不沿用未冻结
            // 样本集（acceptance 3 原文语义）。
            check.matched = false;
        } else {
            check.matched = it->sampleSetIdentity == check.computedIdentity
                && it->plannedPositionSamples == check.plannedPositionSamples
                && it->plannedPoseSamples == check.plannedPoseSamples;
        }
        if (!check.matched) {
            mismatched.push_back(&plan);
        }
        checks.push_back(std::move(check));
    }
    if (!mismatched.empty()) {
        // KIN-SAMPLE-IDENTITY-MISMATCH（§9.6 行 13，error）——零素材输出
        //（无 payload/无证据行＝无覆盖率输出，V-15"错误码；无覆盖率输出"）；
        // cause 逐计划列举（缺失/不一致分述——可定位诊断）。
        std::string cause = "样本集身份对账失败（未冻结/不一致样本集绝不沿"
                            "用——§7.2）：";
        for (const SamplingPlan* plan : mismatched) {
            const bool absent = std::none_of(
                request.snapshot.samplingPlans.begin(),
                request.snapshot.samplingPlans.end(),
                [&plan](const evidence::SamplingPlanRef& r) {
                    return r.regionObjectId == plan->regionObjectId;
                });
            cause += absent ? " 快照无 SamplingPlanRef（未冻结）：" : " 身份/分母与快照不一致：";
            cause += plan->regionObjectId.toCanonical() + "；";
        }
        out.diagnostics.push_back(makeKinDiag(
            kKinSampleIdentityMismatch, mismatched.front()->regionObjectId,
            "kin.region-coverage 样本集身份对账", cause,
            "按冻结采样计划重建快照 SamplingPlanRef 或修正采样预算/种子后重评"));
        return out;
    }

    // ---- 步骤 2~5：纯计算面（确定性采样→零样本判定→逐样本评估→覆盖
    // 率计算；取消/检查点/进度在计算核内）。----
    RegionCoverageComputation computation =
        runRegionCoverageComputation(*m_view, m_query, request, context);
    computation.identityChecks = std::move(checks);

    // ---- 零样本判定（V-13）：任一计划乘积=0 → 该计划覆盖率不定义 →
    // 整体降级 DataInsufficient＋KIN-COVERAGE-ZERO-SAMPLES（绝不输出
    // 0%/100%——无比率字段；登记随卡 §14.6 v0.6：逐计划零样本即全评估
    // 降级，聚合分母的存在不恢复该计划的覆盖结论）。----
    bool anyZeroPlan = false;
    for (const PlanIdentityCheck& check : computation.identityChecks) {
        if (check.plannedPositionSamples == 0U || check.plannedPoseSamples == 0U) {
            anyZeroPlan = true;
        }
    }
    if (anyZeroPlan) {
        computation.coverage.downgraded = true;
    }

    // ---- 步骤 6：素材组装（payload canonical＋逐计划证据行＋诊断——
    // 摘要唯一经 core::ContentDigester，CR-02）。----
    const std::vector<std::uint8_t> payloadBytes =
        encodeRegionCoveragePayloadCanonical(computation, request.task);
    core::ContentDigester digester;
    digester.update(payloadBytes.data(), payloadBytes.size());
    core::ContentIdentity payloadDigest;
    payloadDigest.bytes = digester.finalize();
    out.payload = evidence::DomainPayload{kRegionCoveragePayloadToken, payloadBytes,
                                          payloadDigest};

    // 逐计划证据行（itemId=kKinRegionCoverageRowId——表 4 运动学行"区域
    // 覆盖率"落位；subject=regionObjectId 溯源；artifactDigest＝载荷整体
    // 摘要——逐计划明细/状态表在载荷内，行以摘要＋对象绑定，T05 批量
    // 行同款形态；Satisfied＝产物存在——含 partial/零样本素材，漏验
    // 后果经 diag/incomplete 标记交 evidence 覆盖矩阵）。
    out.evidence.reserve(computation.identityChecks.size());
    for (const PlanIdentityCheck& check : computation.identityChecks) {
        evidence::EvidenceItem row;
        row.itemId = kKinRegionCoverageRowId;
        row.status = evidence::EvidenceItemStatus::Satisfied;
        row.artifactDigest = payloadDigest.bytes;
        row.subject = check.regionObjectId;
        out.evidence.push_back(std::move(row));
    }

    // 诊断面（零样本/不完整——两码均 §9.6 在册行，产码面随本任务）。
    if (anyZeroPlan) {
        out.diagnostics.push_back(makeKinDiag(
            kKinCoverageZeroSamples, std::nullopt,
            "kin.region-coverage 覆盖率计算",
            "存在计划样本乘积=0（零样本——覆盖率不定义，判 DataInsufficient；"
            "绝不输出 0%/100%——§7.2/V-13）",
            "修正区域采样计数（I-REQ-6 非退化）后按同一冻结样本集语义重建"
            "计划再评"));
    }
    if (computation.coverage.incomplete) {
        std::uint64_t notRunTotal = computation.coverage.position.notRun
            + computation.coverage.orientation.notRun;
        out.diagnostics.push_back(makeKinDiag(
            kKinResultIncomplete, std::nullopt,
            "kin.region-coverage 区域覆盖评估",
            "逐样本评估不完整：NotRun " + std::to_string(notRunTotal)
                + " 样本（协作取消/失败中止——partial 不产正式覆盖率，"
                  "重跑同一样本集；§7.2）",
            "恢复任务（新 attempt 自 watermark 续跑）后按同一冻结样本集复评"));
    }

    // 携带模式敏感性标记（Quick/Preview 的 screening-only 语义在 mode
    // 字段——载荷绑定块已携带；效力门禁归汇总/包络层，§8.4）。
    return out;
}

// =====================================================================
// runRegionCoverageComputation——区域覆盖计算核心（步骤 2~5；声明见
// Evaluators.hpp——测试直调面与评估器形态的分离线）
// =====================================================================

RegionCoverageComputation runRegionCoverageComputation(
    const IKinRuntimeView& view, const RegionCoverageQuery& query,
    const evidence::EvaluationRequest& request, evidence::IEvaluationContext& context)
{
    // 装配校验（直调面与评估器构造器同款 fail-fast 面——防御直调绕过
    // 构造器；NFR-COR-03）。
    validateRegionCoverageQuery(view, query);

    RegionCoverageComputation out;
    out.snapshotId = request.snapshot.snapshotId;
    out.sliceId = request.slice.sliceId;
    out.configDigest = query.configDigest;
    out.mode = request.mode;
    out.seed = query.budget.seed;
    out.referenceQ = query.referenceQ;
    out.task = request.task;

    // ---- 步骤 2：确定性采样（D-KIN-6——同 (plans,budget) 同样本集同序；
    // 复评不得增删更换样本的结构保证；生成核自检违例 logic_error）。----
    out.samples = generateSampleSet(query.plans, query.budget);

    // ---- 零样本早退（无样本可评估——空双射合法，覆盖率全轴不定义；
    // 诊断面在评估器组装步产出）。----
    if (out.samples.samples.empty()) {
        out.coverage = computeCoverage(out.samples, SampleResultSet{});
        return out;
    }

    // ---- 步骤 4 评估环境（逐样本共享——同一快照视图/评价区间/初值集：
    // 确定性来源全部固定；初值种子与采样种子同源＝budget.seed，§3.4
    // 单一种子纪律）。----
    const std::vector<JointInterval> intervals = evaluationIntervals(view);
    const std::vector<std::vector<double>> initialValues = makeInitialValues(
        query.initialStrategy, query.initialValuesCount, query.budget.seed,
        intervals, query.referenceQ);
    const IIkSolver& solver =
        query.solver != nullptr ? *query.solver : internalDefaultSolver();

    // 逐计划碰撞要求（合并口径——collisionRequiredForPlan；样本按
    // regionObjectId 回查所属计划的要求）。
    std::map<core::ObjectId, bool> planCollisionRequired;
    for (const SamplingPlan& plan : query.plans) {
        planCollisionRequired.emplace(plan.regionObjectId,
                                      collisionRequiredForPlan(query, plan));
    }

    // ---- 步骤 4：逐样本评估（分批＋协作取消＋检查点＋进度＋并行分片
    // ——T05 同构：结果槽按全序下标写回，合并＝按全序归并）。----
    const std::uint64_t sampleTotal = static_cast<std::uint64_t>(out.samples.samples.size());
    const std::uint64_t batchCount =
        (sampleTotal + query.maxBatchSize - 1U) / query.maxBatchSize;

    // 结果槽预置（全序槽位——NotRun 初值；并行分片各线程只写各自槽位，
    // 无交叉写入；取消路径的 NotRun 即此初值的自然保留）。
    out.results.results.assign(out.samples.samples.size(), SampleResultRecord{});
    for (std::size_t i = 0; i < out.results.results.size(); ++i) {
        out.results.results[i].sampleIndex = out.samples.samples[i].sampleIndex;
    }

    // 取消传播旗标（批内共享——任一样本观测到取消即停止批内派发；原子
    // 松散序足够：只做"是否停止"的保守判定，T05 同款取舍）。
    std::atomic<bool> batchCancelled{false};
    std::uint64_t completedBatches = 0;

    // 单样本求解闭包（批内并行时各线程只写自己的结果槽——§8.4）。
    auto evaluateAt = [&](std::size_t slot) {
        // 批内样本起点取消检查（批间"每批至少一次"之外的自适应粒度）。
        if (batchCancelled.load(std::memory_order_relaxed)
            || context.cancellationRequested()) {
            batchCancelled.store(true, std::memory_order_relaxed);
            return;
        }
        const SampleRecord& sample = out.samples.samples[slot];
        SampleResultRecord result;
        result.sampleIndex = sample.sampleIndex;

        // 缺检测器轨（KIN-05 语义承接，acceptance 5）：碰撞要求在场而
        // 会话为空 → 该样本 DataInsufficient——绝不视为无碰撞；稳定诊断
        // 码 KIN-COLLISION-UNAVAILABLE 的产码面归 T07（§9.6 任务列分工），
        // 本轨以样本状态＋原因文本承载素材。
        const auto reqIt = planCollisionRequired.find(sample.regionObjectId);
        const bool collisionRequired =
            reqIt != planCollisionRequired.end() && reqIt->second;
        if (collisionRequired && query.collisionSession == nullptr) {
            result.state = SampleState::DataInsufficient;
            result.collisionNotEvaluated = true;
            result.reason =
                "碰撞要求在场而碰撞检测器不可用（KIN-05——缺检测器绝不视为"
                "无碰撞；该样本证据缺失）";
            out.results.results[slot] = std::move(result);
            return;
        }

        // 逐样本 IK 请求（位置样本＝位置存在性 IK：姿态无约束落值 π——
        // 收敛判据逐项比较故收敛⇔位置入容差；位姿样本＝完整位姿 IK）。
        IkRequest ikRequest;
        if (sample.kind == SampleKind::Position) {
            ikRequest.targetInBase = rw::math::Transform3D<double>(
                sample.position,
                rw::math::Rotation3D<double>(1, 0, 0, 0, 1, 0, 0, 0, 1));
            ikRequest.orientationTolerance = kPoseFreeTolerance;
        } else {
            ikRequest.targetInBase = sample.pose;
            ikRequest.orientationTolerance = query.orientationTolerance;
        }
        ikRequest.positionTolerance = query.positionTolerance;
        ikRequest.modelView = &view;
        ikRequest.tcp = query.defaultTcp;
        ikRequest.initialValues = initialValues;
        ikRequest.iterationLimit = query.iterationLimit;
        ikRequest.intervals = intervals;
        ikRequest.dedupThresholdPerAxis = query.dedupThresholdPerAxis;
        ikRequest.collisionSession = query.collisionSession;
        ikRequest.referenceQ = query.referenceQ;
        ikRequest.targetRef.pointOid = sample.regionObjectId;
        ikRequest.requestIdentity.snapshotId = request.snapshot.snapshotId;
        ikRequest.requestIdentity.sliceId = request.slice.sliceId;
        ikRequest.requestIdentity.configDigest = query.configDigest;
        ikRequest.requestIdentity.mode = request.mode;
        ikRequest.requestIdentity.seed = query.budget.seed;
        ikRequest.requestIdentity.referenceQ = query.referenceQ;
        ikRequest.cancellationProbe = [&context, &batchCancelled]() {
            if (context.cancellationRequested()) {
                batchCancelled.store(true, std::memory_order_relaxed);
                return true;
            }
            return false;
        };

        const IkOutcome outcome = solver.solve(ikRequest);
        if (outcome.cancelled) {
            // 取消不是结局（§9.2）——槽保持 NotRun 初值，中止批内后续派发。
            batchCancelled.store(true, std::memory_order_relaxed);
            return;
        }

        // 结局→样本状态映射（文件头"样本状态五值词表的映射口径"）：
        // 1/4→Reached（存在性凭据）；5→Unreachable（解析界限确定性证明，
        // 素材随 outcome 但逐样本状态表只载状态——证明素材通道归汇总层
        // 消费载荷外的搜索/证明面，本通道登记随卡 §14.6 v0.6）；2/3→
        // DataInsufficient（C5/C8——绝不输出不可行）。
        switch (outcome.outcomeKind) {
        case IkOutcomeKind::SolutionsFound:
        case IkOutcomeKind::PartialCollision:
            result.state = SampleState::Reached;
            break;
        case IkOutcomeKind::AnalyticBoundExceeded:
            result.state = SampleState::Unreachable;
            break;
        case IkOutcomeKind::MultiInitNoConvergence:
        case IkOutcomeKind::AllCandidatesFiltered:
            result.state = SampleState::DataInsufficient;
            break;
        }
        result.outcomeKind = outcome.outcomeKind;
        result.collisionNotEvaluated = outcome.collisionNotEvaluated;
        out.results.results[slot] = std::move(result);
    };

    for (std::uint64_t b = 0; b < batchCount; ++b) {
        // 批间协作取消查询（每批至少一次——V-22 本单元侧；ARCH §4.4 的
        // 2 s 停止派发界由本查询点＋批内探针承载）。
        if (context.cancellationRequested()) {
            batchCancelled.store(true, std::memory_order_relaxed);
        }
        if (batchCancelled.load(std::memory_order_relaxed)) {
            break;  // 停止派发新批——剩余样本保持 NotRun（无伪完成）
        }

        // 本批的连续槽区间（§8.4——并行分片＝样本全序的连续区间）。
        const std::size_t begin =
            static_cast<std::size_t>(b) * query.maxBatchSize;
        const std::size_t end = std::min<std::size_t>(
            begin + query.maxBatchSize, out.samples.samples.size());

        // 分片执行（threadCount=1 内联单线程——逐位一致；>1 时 T 个连续
        // 分片并行，各线程只写各自槽位，join 后按分片序重抛首个异常——
        // 确定性失败面；T05 同款）。
        const std::size_t threads =
            std::min<std::size_t>(query.budget.threadCount, end - begin);
        if (threads <= 1) {
            for (std::size_t slot = begin; slot < end; ++slot) {
                evaluateAt(slot);
                if (batchCancelled.load(std::memory_order_relaxed)) {
                    break;  // 批内中止——剩余槽走 NotRun
                }
            }
        } else {
            std::vector<std::exception_ptr> shardErrors(threads);
            auto shardWorker = [&](std::size_t t) {
                try {
                    // 第 t 片＝[begin + t·n/T, begin + (t+1)·n/T)——连续
                    // 区间；余数摊入末片。
                    const std::size_t n = end - begin;
                    const std::size_t lo = begin + t * n / threads;
                    const std::size_t hi = begin + (t + 1) * n / threads;
                    for (std::size_t slot = lo; slot < hi; ++slot) {
                        if (batchCancelled.load(std::memory_order_relaxed)) {
                            return;
                        }
                        evaluateAt(slot);
                    }
                } catch (...) {
                    shardErrors[t] = std::current_exception();
                }
            };
            std::vector<std::thread> workers;
            workers.reserve(threads - 1);
            for (std::size_t t = 1; t < threads; ++t) {
                workers.emplace_back(shardWorker, t);
            }
            shardWorker(0);  // 主线程跑第 0 片——避免空等
            for (std::thread& w : workers) {
                w.join();
            }
            // 异常按分片序重抛首个（确定性失败面——NFR-COR-02）。
            for (std::size_t t = 0; t < threads; ++t) {
                if (shardErrors[t] != nullptr) {
                    std::rethrow_exception(shardErrors[t]);
                }
            }
        }

        if (batchCancelled.load(std::memory_order_relaxed)) {
            break;  // 批内取消/失败中断——本批剩余与后续批不推进 watermark
        }

        // ---- 批完成：检查点 watermark（样本批粒度——§8.3 能力声明
        // CheckpointGranularity::Sample 的落点；P-KIN-7 最小端口）＋进度
        // 上报（phase="solve-sample"）。----
        ++completedBatches;
        if (query.checkpointSink != nullptr) {
            query.checkpointSink->batchWatermark(completedBatches, batchCount);
        }
        const std::uint64_t percent = sampleTotal == 0U
            ? 100U
            : std::min<std::uint64_t>(
                100U, (end * 100U) / sampleTotal);
        context.reportProgress(static_cast<std::uint8_t>(percent),
                               kRegionCoveragePhase);
    }

    // ---- 取消/中断收尾：未评估样本如实 NotRun（§7.2"取消→partial→
    // 不产正式覆盖率"；以"槽仍处于 NotRun 初值且无原因文本"判定未派发
    // ——已评估样本的终态如实保留，不回写）。----
    std::uint64_t notRunCount = 0;
    for (SampleResultRecord& r : out.results.results) {
        if (r.state == SampleState::NotRun && r.reason.empty()) {
            r.reason =
                "未运行：协作取消/失败中止后未派发（NotRun 如实标记——已完成"
                "样本批 " + std::to_string(completedBatches) + "/"
                + std::to_string(batchCount) + "，watermark 保留可续）";
            ++notRunCount;
        }
    }

    // ---- 步骤 5：覆盖率计算（唯一实现点委托——双射核查/守恒式内置）。----
    out.coverage = computeCoverage(out.samples, out.results);

    // NotRun 计数自检（NotRun 槽位数与覆盖率轴计数一致——内部不变量，
    // 违例 logic_error 不静默）。
    if (out.coverage.position.notRun + out.coverage.orientation.notRun
        != notRunCount) {
        throw std::logic_error(
            "kin.region-coverage：NotRun 计数自检失败（覆盖率轴计数≠收尾"
            "标记数——内部缺陷，fail-fast）");
    }

    // 计算核心到此为止（证据组装唯一在评估器 evaluate 步骤 6——测试
    // 直调本函数断言采样/逐样本/取消语义，NFR-MNT-01）。
    return out;
}

// =====================================================================
// makeRegionCoverageDescriptor——依赖声明与形态（§4.3 行）
// =====================================================================

evidence::EvaluatorDescriptor makeRegionCoverageDescriptor()
{
    evidence::EvaluatorDescriptor d;
    d.key = kRegionCoverageEvaluationKey;
    d.contractVersion = kRegionCoverageContractVersion;

    // 依赖声明九条（§4.3 行原样：八条 Required＋collision-models 一条
    // Conditional——条件依赖语义：碰撞启用状态只读自 policy，策略未启用
    // 碰撞时该键不进切片（V13-01），resolutionNote 登记条件语义）。
    d.inputs = {
        {"model.robot-design", evidence::DependencyKind::Object,
         evidence::DependencyRequiredness::Required, std::nullopt,
         "规范机器人链对象（Object 闭包内 robot-design——链/关节/限位真值）"},
        {"tcp", evidence::DependencyKind::Object,
         evidence::DependencyRequiredness::Required, std::nullopt,
         "工具定义对象（Object→tool-definition——TCP 偏置真值，AT-05① 失效面）"},
        {"req.regions", evidence::DependencyKind::Object,
         evidence::DependencyRequiredness::Required, std::nullopt,
         "区域集合对象（req-region-set——Box 几何与采样定义来源）"},
        {"req.sampling-plans", evidence::DependencyKind::Object,
         evidence::DependencyRequiredness::Required, std::nullopt,
         "采样计划集合对象（req-plan-set——planContentIdentity 来源，"
         "计划变更独立失效面）"},
        {"req.conditions", evidence::DependencyKind::Object,
         evidence::DependencyRequiredness::Required, std::nullopt,
         "工况集合对象（req-condition-set——碰撞要求的工况侧来源）"},
        {"policy.resolved", evidence::DependencyKind::Policy,
         evidence::DependencyRequiredness::Required, std::nullopt,
         "已解析工程策略内容身份（CON-06——策略变更全列失效；碰撞启用"
         "状态的只读源，V13-01）"},
        {"namemap", evidence::DependencyKind::NameMap,
         evidence::DependencyRequiredness::Required, std::nullopt,
         "运行时名称映射内容身份（CON-06——⑥端口诊断定位/结果标注）"},
        {"config.ik", evidence::DependencyKind::Configuration,
         evidence::DependencyRequiredness::Required, std::nullopt,
         "分析求解配置（KIN-13 canonical——入 sliceId 不入样本基准，D-04）"},
        {"collision-models", evidence::DependencyKind::Object,
         evidence::DependencyRequiredness::Conditional, std::nullopt,
         "碰撞模型集合对象（策略启用碰撞时进切片——V13-01 条件依赖；"
         "本单元不设碰撞布尔开关）"},
    };

    // Profile 声明引用：域 id 词表 "kin"（evidence isDomainProfileId）；
    // contentIdentity 置零值＝域不可申报（evidence §9.5/R-3）。
    d.profile.profileId = "kin";
    d.profile.version = "1";
    d.profile.contentIdentity = core::ContentIdentity{};

    // 模式集：§4.3 行两值（Quick/Verified——覆盖通道不做 Preview；Quick
    // 载荷以 mode 字段承载 screening-only 语义，效力门禁在汇总/包络层）。
    d.supportedModes = {core::EvaluationMode::Quick, core::EvaluationMode::Verified};

    // 无跨调用状态（实例可共享）＋完全线程安全（逐样本并行分片要求
    // 可重入——§9.2 头注同款）。
    d.stateless = true;
    d.threadSafety = evidence::ThreadSafety::FullyThreadSafe;
    return d;
}

// =====================================================================
// WorkspaceSamplerFactory——宿主注入工厂（闭包捕获；create 无参）
// =====================================================================

WorkspaceSamplerFactory::WorkspaceSamplerFactory(const IKinRuntimeView* view,
                                                 RegionCoverageQuery query)
    : m_view(view), m_query(std::move(query)),
      m_descriptor(makeRegionCoverageDescriptor())
{
    // 与评估器同一装配校验面（工厂是宿主的注入入口——违约在装配期
    // 暴露，不迟至 create()；validateRegionCoverageQuery 为本 TU 匿名
    // 命名空间的同一校验函数）。
    if (m_view == nullptr) {
        throw std::invalid_argument(
            "kin.region-coverage 工厂：注入视图为空指针（宿主注入契约违约——O-37）");
    }
    validateRegionCoverageQuery(*m_view, m_query);
}

const evidence::EvaluatorDescriptor& WorkspaceSamplerFactory::descriptor() const
{
    return m_descriptor;
}

std::unique_ptr<evidence::IEngineeringEvaluator>
WorkspaceSamplerFactory::create() const
{
    // 无参签名（O-37 裁决——注册表兼容）；视图/求解器/检查点/碰撞会话
    // 指针经闭包传递——注入语义的唯一通道（生命周期约束见查询值注）。
    return std::make_unique<WorkspaceSampler>(m_view, m_query);
}

// =====================================================================
// regionCoverageCapability——任务类型能力声明（§8.3 提交行值面）
// =====================================================================

execution::TaskCapability regionCoverageCapability()
{
    execution::TaskCapability capability;
    // 暂停不支持（R1 如实声明——EX-SM-7 口径，与批量通道一致）。
    capability.supportsPause = false;
    // 检查点＝样本 watermark（§8.3 能力声明原文"样本 watermark（coverage）"
    // ——CheckpointGranularity::Sample；watermark 产出面＝IBatchCheckpoint-
    // Sink 每样本批一次）。
    capability.checkpointGranularity = execution::CheckpointGranularity::Sample;
    // 强制终止代价＝低（样本批边界即安全中止点，无跨批不变量）。
    capability.forceTerminateCost = execution::ForceTerminateCost::Cheap;
    return capability;
}

}  // namespace sdurws::ird::kinematics
