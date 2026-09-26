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

}  // namespace sdurws::ird::kinematics
