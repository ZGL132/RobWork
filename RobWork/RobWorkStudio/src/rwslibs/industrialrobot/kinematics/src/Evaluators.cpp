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

#include <cmath>
#include <cstring>
#include <stdexcept>
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

}  // namespace sdurws::ird::kinematics
