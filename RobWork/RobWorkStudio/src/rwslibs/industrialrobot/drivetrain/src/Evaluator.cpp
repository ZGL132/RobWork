/**
 * @file   Evaluator.cpp
 * @brief  传动映射评估器（③端口适配层）的实现——descriptor 装配、切片
 *         核对、必需条目提取、字节解码、核心映射调用、输出装配（Evidence
 *         Item＋payload＋诊断透传）与 Facts 提取。
 *
 * 设计依据：
 *   - Evaluator.hpp 文件头（流程五步/错误轨两分法/线程模型/偏差登记三则）
 *   - units/drivetrain.md §13.10（③端口注册面形态）、§12.1（UpstreamResult
 *     依赖与字节不可得的处置）、§12.4（取消＝批次边界，不发布完整结果）
 *   - evidence 冻结契约（Evaluator.hpp/Slice.hpp/Evidence.hpp/Envelope.hpp
 *     ——先例：kinematics/src/Evaluators.cpp 的 payload 摘要装配形态）
 *   - 任务契约 tasks/foundation/WP-18-T03.json（acceptance 1/3）
 *
 * 线程安全：实例单线程（descriptor 声明）；create() 仅构造无状态对象＝
 *   线程安全（§9.4 注册表在锁外调用 create）。
 */

#include <sdurws/ird/drivetrain/Evaluator.hpp>

#include <sdurws/ird/drivetrain/DiagCodes.hpp>

#include <sdurws/ird/core/Digest.hpp>

#include <stdexcept>
#include <utility>

namespace sdurws::ird::drivetrain {
namespace {

// =====================================================================
// 内部工具
// =====================================================================

/// 依赖声明快捷构造（resolutionNote＝域注释，供人工评审"该键从哪里解析"
/// ——非身份载体，evidence §4.2.1 DependencyDeclaration 行）。
evidence::DependencyDeclaration declare(std::string key,
                                        evidence::DependencyKind kind,
                                        std::string note)
{
    evidence::DependencyDeclaration d;
    d.key = std::move(key);
    d.kind = kind;
    d.requiredness = evidence::DependencyRequiredness::Required;
    d.resolutionNote = std::move(note);
    return d;
}

/// 在切片条目中按键定位依赖条目（(kind,key) 字典序存储——evidence §4.2.2；
/// 线性扫描即可，条目数为个位数）。
const evidence::DependencyEntry* findEntry(const evidence::InputSlice& slice,
                                           std::string_view key)
{
    for (const evidence::DependencyEntry& e : slice.entries) {
        if (e.key == key) {
            return &e;
        }
    }
    return nullptr;
}

/// 摘要快捷计算（SHA-256——core ContentDigester 唯一算法面，CR-02）。
core::ContentIdentity digestOf(const std::vector<std::uint8_t>& bytes)
{
    core::ContentDigester digester;
    digester.update(bytes.data(), bytes.size());
    core::ContentIdentity digest;
    digest.bytes = digester.finalize();
    return digest;
}

}  // namespace

// =====================================================================
// descriptor 工厂
// =====================================================================

evidence::EvaluatorDescriptor makeMappingDescriptor()
{
    evidence::EvaluatorDescriptor d;
    // 评估键/契约版本：唯一书写点常量（Codec.hpp——descriptor/codec/测试
    // 共用；评估键 kebab 形态的偏差登记见 Evaluator.hpp 文件头）。
    d.key = std::string(kMappingEvaluationKey);
    d.contractVersion = kMappingContractVersion;

    // 依赖声明（§12.1——两条 Required；提议键 dyn.joint-series 待 dynamics
    // 卡对齐〔P-DT-6〕；config.dt-mapping 不入 R1 声明——Evaluator.hpp 偏差 3）。
    d.inputs.push_back(declare(std::string(kModelDrivetrainKey),
                               evidence::DependencyKind::Object,
                               "robot-drivetrain 对象的归一化传动模型（组装方按 "
                               "Codec 协议编码挂入切片 Object 条目）"));
    d.inputs.push_back(declare(std::string(kJointSeriesKey),
                               evidence::DependencyKind::UpstreamResult,
                               "dynamics 关节侧序列（跨运行依赖；提议键待 "
                               "P-DT-6 对齐——对齐前按本卡 §12.1 提议契约）"));

    // Profile 绑定（偏差 2——消费域 sel；contentIdentity 保留值＝域不可
    // 申报，注册表权威计算——§9.5 实现口径 R-3）。
    d.profile.profileId = std::string(kMappingProfileId);
    d.profile.version = std::string(kMappingProfileVersion);
    d.profile.contentIdentity = core::ContentIdentity{};

    // 模式与实例语义（D-DT-14——Preview 暂不含：消费需求未登记，保守）。
    d.supportedModes = {core::EvaluationMode::Quick, core::EvaluationMode::Verified};
    d.stateless = true;
    d.threadSafety = evidence::ThreadSafety::SingleThread;
    return d;
}

// =====================================================================
// 评估器实例
// =====================================================================

DriveTrainMappingEvaluator::DriveTrainMappingEvaluator(StageCapability stage)
    : m_descriptor(makeMappingDescriptor())
    , m_stage(stage)
{
}

const evidence::EvaluatorDescriptor& DriveTrainMappingEvaluator::descriptor() const
{
    return m_descriptor;
}

evidence::EvaluationOutput DriveTrainMappingEvaluator::evaluate(
    const evidence::EvaluationRequest& request, evidence::IEvaluationContext& context)
{
    // ---- ①切片核对（派发契约前提：execution 按 evaluationKey 派发，切片
    // 绑定键/契约版本与本评估器登记值不一致＝调用方契约违约，fail-fast）。
    if (request.slice.evaluationKey != kMappingEvaluationKey
        || request.slice.evaluatorContractVersion != kMappingContractVersion) {
        throw std::invalid_argument(
            "drivetrain 评估器：切片绑定评估键/契约版本与登记值不符（切片 «"
            + request.slice.evaluationKey + "» v"
            + std::to_string(request.slice.evaluatorContractVersion)
            + "，期望 «" + std::string(kMappingEvaluationKey) + "» v"
            + std::to_string(kMappingContractVersion)
            + "）——execution 派发契约违约，fail-fast");
    }

    // ---- ②必需条目提取（派发前依赖闭包由 execution 注册期校验保证；运行
    // 期缺失＝第二道防线 fail-fast——与注册闭包同语义，不静默降级）。
    const evidence::DependencyEntry* modelEntry
        = findEntry(request.slice, kModelDrivetrainKey);
    const evidence::DependencyEntry* seriesEntry = findEntry(request.slice, kJointSeriesKey);
    if (modelEntry == nullptr || seriesEntry == nullptr) {
        throw std::invalid_argument(
            "drivetrain 评估器：切片缺少必需依赖条目（model.drivetrain 与 "
            "dyn.joint-series 均为 Required——注册闭包与运行期双道防线）");
    }

    evidence::EvaluationOutput out;

    // ---- ③条目载荷与字节读取（Object 条目按 id+version 从宿主物化快照
    // 读取——§9.3 tryObjectBytes；不可得＝数据类：返回诊断＋空 payload，
    // 不伪造完整结果）。
    const auto* modelPayload
        = std::get_if<evidence::ObjectDependencyPayload>(&modelEntry->payload);
    if (modelPayload == nullptr) {
        throw std::invalid_argument(
            "drivetrain 评估器：model.drivetrain 条目载荷形态与 Object 类不符"
            "（variant 下标违约——切片冻结协议破坏，fail-fast）");
    }
    const auto modelBytes = context.tryObjectBytes(modelPayload->objectId,
                                                   modelPayload->contentVersion);
    if (!modelBytes.has_value()) {
        core::DiagnosticRecord rec = core::DiagnosticRecord::make(
            std::string(kDtInputSampleMissing), std::nullopt, std::nullopt,
            std::nullopt,
            "drivetrain 评估输入读取",
            "切片 Object 条目（model.drivetrain）的对象字节不可得（宿主物化"
            "快照中无该对象或版本不符）",
            "核对快照物化与切片条目版本锚定后重派发");
        out.diagnostics.push_back(std::move(rec));
        // EvaluationOutput 无 completeness 字段（evidence 冻结字段面）——
        // 完整性素材经诊断通道表达（payload 不产出＝消费方可判不完整）。
        return out;
    }

    // ---- ④解码＋映射＋统计（同一算法路径＝DriveTrainMappingCore——③端口
    // 形态与注入形态同一算法，D-DT-2；解码违约＝数据类诊断，不抛——组装方
    // 字节形态错误属数据面而非宿主编程错误）。
    DriveTrainModel model;
    JointSeriesView series;
    bool decoded = true;
    std::string decodeError;
    try {
        model = decodeDriveTrainModel(*modelBytes);
    } catch (const std::invalid_argument& ex) {
        decoded = false;
        decodeError = ex.what();
    }
    if (decoded) {
        // UpstreamResult 条目载荷＝UpstreamResultRef（上游切片身份——§4.2.1，
        // 无对象 id）：序列内容按本域物化锚约定读取（jointSeriesAnchor——
        // 组装方把序列字节物化在锚 id 对应对象下，P-DT-6 对齐前的提议承载，
        // 见 Codec.hpp；版本参数保留值＝版本一致性由上游切片身份承载）。
        const auto* seriesRef
            = std::get_if<evidence::UpstreamResultDependencyPayload>(&seriesEntry->payload);
        if (seriesRef == nullptr) {
            throw std::invalid_argument(
                "drivetrain 评估器：dyn.joint-series 条目载荷形态与 UpstreamResult "
                "类不符（variant 下标违约——切片冻结协议破坏，fail-fast）");
        }
        const auto seriesBytes = context.tryObjectBytes(
            jointSeriesAnchor(seriesRef->upstreamSliceId), core::ContentVersion{});
        if (!seriesBytes.has_value()) {
            core::DiagnosticRecord rec = core::DiagnosticRecord::make(
                std::string(kDtInputSampleMissing), std::nullopt, std::nullopt,
                std::nullopt,
                "drivetrain 评估输入读取",
                "切片 UpstreamResult 条目（dyn.joint-series）的序列字节不可得"
                "（上游结果未按物化锚约定物化或版本不符）",
                "确认 dynamics 上游结果已归档并按 jointSeriesAnchor 物化后重派发");
            out.diagnostics.push_back(std::move(rec));
            return out;
        }
        try {
            series = decodeJointSeries(*seriesBytes);
        } catch (const std::invalid_argument& ex) {
            decoded = false;
            decodeError = ex.what();
        }
    }
    if (!decoded) {
        core::DiagnosticRecord rec = core::DiagnosticRecord::make(
            std::string(kDtMatrixNonfinite), std::nullopt, std::nullopt,
            std::nullopt,
            "drivetrain 评估输入解码", decodeError,
            "核对组装方编码协议版本（IRDDTD1/IRDDTJ1）与字节完整性后重派发");
        out.diagnostics.push_back(std::move(rec));
        return out;
    }

    // 核心调用（取消回调适配——宿主上下文的取消查询即批次边界信号）。
    class ContextCancellation final : public ICancellation {
    public:
        explicit ContextCancellation(evidence::IEvaluationContext& ctx)
            : m_ctx(ctx)
        {
        }
        bool cancellationRequested() const override
        {
            return m_ctx.cancellationRequested();
        }

    private:
        evidence::IEvaluationContext& m_ctx;
    } cancellation(context);

    // 能力位经构造注入（WP-18-T05——装配清单决定 R1/R2；四参入口显式
    // 声明，无隐式能力提升）。核心内含阻断面/一致性自检/统计——同一算法
    // 路径（③端口形态与注入形态同一算法——D-DT-2）。
    DriveTrainMappingOutput mapping = DriveTrainMappingCore().evaluate(model, series,
                                                                       &cancellation,
                                                                       m_stage);
    out.diagnostics = mapping.diagnostics;
    // 映射完整性（Complete/Partial）随 payload 内的映射输出承载——
    // EvaluationOutput 顶层无该字段（evidence 冻结字段面；消费方按
    // decodeMappingOutput 还原完整性素材）。

    // ---- ⑤输出装配：payload（canonical 字节＋SHA-256 摘要——CR-02 唯一
    // 算法；kinematics Evaluators.cpp 同款先例）。
    const std::vector<std::uint8_t> payloadBytes = encodeMappingOutput(mapping);
    out.payload = evidence::DomainPayload{std::string(kMappingPayloadToken), payloadBytes,
                                          digestOf(payloadBytes)};

    // EvidenceItem（sel 域工作点必需项——有工作点产出即 Satisfied＋摘要；
    // 取消/降级场景按 presence 纪律以 Missing＋invalidReason 语义由汇总层
    // 处置，本层只对"有完整素材"的形态出 Satisfied 项）。
    if (mapping.completeness == CompletenessState::Complete
        && !mapping.points.empty()) {
        evidence::EvidenceItem item;
        item.itemId = std::string(kMappingProfileId) + ".motor-op-point";
        item.status = evidence::EvidenceItemStatus::Satisfied;
        core::Digest256 artifactDigest{};
        const core::ContentIdentity full = digestOf(payloadBytes);
        artifactDigest = full.bytes;
        item.artifactDigest = artifactDigest;
        item.subject = mapping.points.front().axisId;
        out.evidence.push_back(std::move(item));
    }

    return out;
}

// =====================================================================
// 工厂
// =====================================================================

DriveTrainMappingEvaluatorFactory::DriveTrainMappingEvaluatorFactory()
    : m_descriptor(makeMappingDescriptor())
{
}

const evidence::EvaluatorDescriptor& DriveTrainMappingEvaluatorFactory::descriptor() const
{
    return m_descriptor;
}

std::unique_ptr<evidence::IEngineeringEvaluator> DriveTrainMappingEvaluatorFactory::create()
    const
{
    // 无状态对象构造（create() 线程安全——仅栈上构造＋转移所有权）。
    return std::make_unique<DriveTrainMappingEvaluator>();
}

// =====================================================================
// 事实提供器
// =====================================================================

DriveTrainEvidenceFacts DriveTrainFactsProvider::facts(
    const DriveTrainMappingOutput& output) const
{
    DriveTrainEvidenceFacts f;
    // 身份块（NFR-COR-04 可追溯——§5.5 身份链）。
    f.identity = output.identity;
    f.upstreamSliceId = output.upstreamSliceId;
    f.algorithmVersion = output.algorithmVersion;
    f.contractVersion = output.contractVersion;
    f.caseId = output.caseId;
    f.caseCovered = output.caseId; // 覆盖素材＝本批工况（跨工况汇总归 evidence）

    // 完整性与缺失（DataInsufficient 素材——判定归 evidence）。
    f.completeness = output.completeness;
    f.missingItems = output.missingItems;
    f.estimatedSource = output.missingItems.end()
                        != std::find(output.missingItems.begin(),
                                     output.missingItems.end(), "estimated-source");

    // 逐轴事实摘要（数值事实引用面——RMS/反射惯量/效率可用性）。
    f.axes.reserve(output.points.size());
    for (const MotorOperatingPoint& p : output.points) {
        DriveTrainEvidenceFacts::AxisFact ax;
        ax.axisId = p.axisId;
        ax.jointIndex = p.jointIndex;
        ax.tauRms = p.tauRms;
        ax.omegaRms = p.omegaRms;
        ax.reflectedInertia = p.reflectedInertia;
        ax.efficiencyApplied = p.etaApplied.has_value();
        f.axes.push_back(ax);
    }

    // 诊断引用面（码值列表按产出序——完整记录在映射输出 diagnostics 通道）。
    f.diagnosticCount = output.diagnostics.size();
    f.diagnosticCodes.reserve(output.diagnostics.size());
    for (const core::DiagnosticRecord& rec : output.diagnostics) {
        f.diagnosticCodes.push_back(rec.code);
    }
    return f;
}

}  // namespace sdurws::ird::drivetrain
