/**
 * @file   EvidenceBuilder.cpp
 * @brief  dyn Profile 证据装配器实现（EvidenceBuilder.hpp 契约的落地翻译
 *         单元）——§8.4 六项证据状态判定、降级表达（Invalid/Unverified＋
 *         DYN-* 稳定码诊断）、缺失清单全量组装、限定语判定与产物摘要
 *         投影（SHA-256）的唯一实现点。
 *
 * 产物摘要投影编码规则（证据项 artifactDigest 的取值源；投影＝身份锚，
 * 非往返载体——无 parse，唯一消费方式＝摘要比对）：
 *   1. 域分隔 magic "IRDDYEV1"（8 字节 ASCII）起头——防跨域摘要混同
 *      （DYN-06 证据面与其它域产物摘要不可互认）；随 magic 后写 u32 codec
 *      版本（1）与 u32 itemId 词表序号（§8.4 表行序 1..6）——同投影字段
 *      不同项 id 的摘要天然分离；
 *   2. 各项再写其绑定素材的规范编码（定宽小端，与 SeriesBuilder.cpp 同款
 *      纪律）：f64＝IEEE-754 位模式小端；u8/u32/u64 显式小端；bool＝0/1；
 *      ObjectId＝16 字节直通；ContentIdentity＝32 字节直通；
 *   3. 同输入字节必得同摘要（NFR-COR-02）；任何素材字段变化→摘要变化
 *      （evidence §6.2 绑定校验的凭据面——"产物存在且可核对"）。
 *
 * 确定性：证据状态判定为纯值分支（无环境/时钟/locale 依赖）；诊断合成
 *   经 DiagnosticRecord::make（C-3 校验——必填字段空串即抛，防"空原因"
 *   诊断静默传播）。
 */

#include <sdurws/ird/dynamics/EvidenceBuilder.hpp>

#include <sdurws/ird/dynamics/DiagCodes.hpp> // DYN-* 码值常量（唯一书写点——产码共用）

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <optional>
#include <utility>
#include <vector>

namespace sdurws::ird::dynamics {

namespace {

// =====================================================================
// 摘要流写入助手（与 SeriesBuilder.cpp 同款 canonical 小端纪律——本文件
 //   自持简化版：证据投影只用 u8/u32/u64/f64/raw/id16/cid32 七种面）。
// =====================================================================

/// 摘要流包装：update 转发＋各标量类型的显式小端编码。
class DigestWriter {
public:
    /// 构造（持有调用方摘要器引用——生命周期覆盖本写入器使用区间）。
    explicit DigestWriter(core::ContentDigester& d) : mDigest(d) {}

    /// 原始字节追加（magic/id 直通用）。
    void raw(const void* data, std::size_t n) { mDigest.update(data, n); }

    /// u8（枚举底层值/布尔——bool 以 0/1 编码）。
    void u8(unsigned v) { mDigest.update(&v, 1); }

    /// u32 显式小端（4 字节）。
    void u32(std::uint32_t v)
    {
        const unsigned char b[4] = {static_cast<unsigned char>(v & 0xFFu),
                                    static_cast<unsigned char>((v >> 8) & 0xFFu),
                                    static_cast<unsigned char>((v >> 16) & 0xFFu),
                                    static_cast<unsigned char>((v >> 24) & 0xFFu)};
        mDigest.update(b, 4);
    }

    /// u64 显式小端（8 字节——计数类字段用）。
    void u64(std::uint64_t v)
    {
        const unsigned char b[8] = {static_cast<unsigned char>(v & 0xFFu),
                                    static_cast<unsigned char>((v >> 8) & 0xFFu),
                                    static_cast<unsigned char>((v >> 16) & 0xFFu),
                                    static_cast<unsigned char>((v >> 24) & 0xFFu),
                                    static_cast<unsigned char>((v >> 32) & 0xFFu),
                                    static_cast<unsigned char>((v >> 40) & 0xFFu),
                                    static_cast<unsigned char>((v >> 48) & 0xFFu),
                                    static_cast<unsigned char>((v >> 56) & 0xFFu)};
        mDigest.update(b, 8);
    }

    /// f64＝IEEE-754 位模式的 u64 小端编码（memcpy 取位——无别名违例）。
    void f64(double v)
    {
        std::uint64_t bits = 0;
        static_assert(sizeof(bits) == sizeof(v), "double 必须 64 位（IEEE-754）");
        std::memcpy(&bits, &v, sizeof(bits));
        u64(bits);
    }

    /// 16 字节强类型 id（ObjectId——Identity.hpp bytes 直通）。
    void id16(const core::ObjectId& oid) { raw(oid.bytes.data(), oid.bytes.size()); }

    /// 32 字节内容身份（ContentIdentity——Digest.hpp bytes 直通）。
    void cid32(const core::ContentIdentity& cid) { raw(cid.bytes.data(), cid.bytes.size()); }

private:
    core::ContentDigester& mDigest; ///< 调用方摘要器（不接管所有权）
};

// =====================================================================
// 证据项 id 词表序（§8.4 表行序——摘要投影与 items()/missingList() 行序
 //   的唯一序源；NFR-COR-02 确定性）。
// =====================================================================

/// §8.4 表行序的六项 id（下标 0..5 ↔ 行 1..6；词表序号进摘要投影）。
constexpr std::array<std::string_view, 6> kItemOrder = {
    kDynItemJointSeries,     // 行 1（Required）
    kDynItemPeakRms,         // 行 2（Required）
    kDynItemProvenance,      // 行 3（Required）
    kDynItemLoadCondition,   // 行 4（Required）
    kDynItemPowerEnergy,     // 行 5（Suggested）
    kDynItemForwardCheck,    // 行 6（Suggested）
};

/// 摘要投影的公共头：magic＋codec 版本＋itemId 行序号（全部项一致起头）。
void writeProjectionHeader(DigestWriter& w, std::size_t itemOrderIndex)
{
    w.raw("IRDDYEV1", 8);             // 域分隔 magic（版本 1）
    w.u32(1);                         // codec 版本（投影编码演进＝递增）
    w.u32(static_cast<std::uint32_t>(itemOrderIndex) + 1u);  // itemId 词表序号（1 基）
}

/// 计算 Satisfied 项的产物摘要（绑定素材的规范编码投影→SHA-256）。
/// 各项投影字段（见 EvidenceBuilder.hpp 类注释与文件头编码规则）：
///   ①series：conditionId＋series.contentIdentity；
///   ②stats：＋peaks 全行（value/tPeak/窗端/segmentIndex—— PeakRecord
///     的数值行；conditionId 列不重复写，公共头已锚定工况）；
///   ③provenance：＋validity 来源事实六分量（计数/布尔——降级凭据）；
///   ④load-condition：＋toolObjectId＋payloadVariant 覆盖集（升序去重）；
///   ⑤power-energy：＋摘要数值面（cycleDurationS/标志位/逐关节能量行）；
///   ⑥forward-check：＋四态/比较样本数/六误差/积分步数。
core::Digest256 digestSatisfiedItem(std::size_t itemOrderIndex, const core::ObjectId& conditionId,
                                    const DynamicsSeries& series,
                                    const OperatingConditionResult* condition,
                                    const ForwardCheckOutcome* forward)
{
    core::ContentDigester d;
    DigestWriter w(d);
    writeProjectionHeader(w, itemOrderIndex);
    w.id16(conditionId);              // 工况锚（证据 caseScope 的字节面）
    w.cid32(series.contentIdentity);  // 序列内容身份（素材同源绑定锚）
    switch (itemOrderIndex) {
    case 1:  // ② peak-rms：逐峰值行的数值投影（token 序见 Envelope.hpp 表）
        if (condition != nullptr) {
            w.u64(condition->peaks.size());
            for (const PeakRecord& p : condition->peaks) {
                w.f64(p.value);
                w.f64(p.tPeakS);
                w.f64(p.windowStartS);
                w.f64(p.windowEndS);
                w.u32(p.segmentIndex);
            }
        }
        break;
    case 2:  // ③ provenance：来源事实六分量（DYN-06 三层的凭据面）
        w.u64(series.validity.estimatedLinkCount);
        w.u64(series.validity.estimatedPayloadCount);
        w.u8(series.validity.frictionMissing ? 1u : 0u);
        w.u8(series.validity.externalValidationPending ? 1u : 0u);
        w.u64(series.validity.plannedSampleCount);
        w.u64(series.validity.actualSampleCount);
        break;
    case 3: {  // ④ load-condition：工具身份＋负载变体覆盖集（升序去重——确定性）
        w.id16(series.toolObjectId);
        std::vector<std::uint32_t> variants;
        for (const DynamicsSample& s : series.samples) {
            variants.push_back(s.payloadVariantIndex);
        }
        std::sort(variants.begin(), variants.end());
        variants.erase(std::unique(variants.begin(), variants.end()), variants.end());
        w.u64(variants.size());
        for (std::uint32_t v : variants) { w.u32(v); }
        break;
    }
    case 4:  // ⑤ power-energy：摘要数值面（无效字段 NaN 位模式照编——
             //   该形态不会出现在 Satisfied 项，见 build 判定）
        if (condition != nullptr) {
            const PowerEnergySummary& pe = condition->powerEnergy;
            w.f64(pe.cycleDurationS);
            w.u8(pe.includesDwell ? 1u : 0u);
            w.u8(pe.timeParamAvailable ? 1u : 0u);
            w.u64(pe.joints.size());
            for (const PowerEnergySummary::JointPowerEnergy& j : pe.joints) {
                w.u32(j.jointIndex);
                w.f64(j.positiveEnergyJ);
                w.f64(j.negativeEnergyJ);
                w.f64(j.netEnergyJ);
                w.f64(j.meanPowerW);
            }
        }
        break;
    case 5:  // ⑥ forward-check：四态/比较面投影（误差 NaN 位模式照编）
        if (forward != nullptr) {
            w.u8(static_cast<unsigned>(forward->state));
            w.u64(forward->comparedSamples);
            w.f64(forward->maxErrQ);
            w.f64(forward->rmsErrQ);
            w.f64(forward->maxErrQd);
            w.f64(forward->rmsErrQd);
            w.f64(forward->tOfMaxErrQ);
            w.f64(forward->tOfMaxErrQd);
            w.u32(forward->integratorSteps);
        }
        break;
    default:  // ① series：公共头两锚即全部投影（condition/forward 为空）
        break;
    }
    return d.finalize();
}

/// 合成 Invalid 态的原因诊断（invalidReason——evidence §6.2"附原因诊断"；
/// 稳定码取 DiagCodes 常量唯一书写点，context/cause/recommendedAction
/// 必填非空——core C-3 校验强制）。
core::DiagnosticRecord makeInvalidReason(const std::string& code, const core::ObjectId& subject,
                                         const std::string& cause)
{
    return core::DiagnosticRecord::make(
        code, subject, std::optional<std::string>{}, std::optional<std::string>{},
        std::string{"dynamics 证据装配（dyn Profile）"}, cause,
        std::string{"建模侧补全参数/标定来源后重编译并复算动力学——估算值不得作为精确结论使用"});
}

}  // namespace

// =====================================================================
// dynProfile（§8.4 表逐行实例化——域 Profile 登记面）。
// =====================================================================

evidence::RequiredEvidenceProfile dynProfile()
{
    using evidence::EvidenceItemClass;
    using evidence::EvidenceProfileItem;

    // 行构造助手：itemId＋类别＋替代标志＋人读说明（锚定表 4 行文——
    // description 非空是注册期校验强制项；全部六项无条件＝Always）。
    auto row = [](std::string_view id, EvidenceItemClass cls, bool substitutable,
                  const char* desc) {
        EvidenceProfileItem item;
        item.itemId = std::string{id};
        item.itemClass = cls;
        item.substitutableByInfeasibility = substitutable;
        item.description = desc;
        return item;
    };

    evidence::RequiredEvidenceProfile profile;
    profile.profileId = std::string{kDynProfileId};
    profile.version = std::string{kDynProfileVersion};
    // contentIdentity 保留值：注册时由 evidence 计算覆盖（域不可申报——
    // evidence §6.1/§9.5；此处不得申报非零值，评估器注册期校验会拒绝）。
    profile.required = {
        row(kDynItemJointSeries, EvidenceItemClass::Required, true,
            "关节侧广义力序列（RNEA、类型化单位）——表 4 动力学行"),
        row(kDynItemPeakRms, EvidenceItemClass::Required, true,
            "峰值（含持续时间窗与所在段）与完整循环 RMS（含驻留）"),
        row(kDynItemProvenance, EvidenceItemClass::Required, false,
            "物性/摩擦参数来源标记（缺失按 DYN-06 降级并列入缺失清单）"),
        row(kDynItemLoadCondition, EvidenceItemClass::Required, false,
            "负载工况标识"),
    };
    profile.suggested = {
        row(kDynItemPowerEnergy, EvidenceItemClass::Suggested, true,
            "机械功率/能量分项（数据完整时产出）"),
        row(kDynItemForwardCheck, EvidenceItemClass::Suggested, true,
            "正动力学一致性检查记录（mode=Standard 且前置齐备；Skip→NotRun）"),
    };
    return profile;
}

// =====================================================================
// trustQualifiers（DYN-06 限定语纪律唯一实现点——§5.5 三层→词表三值）。
// =====================================================================

std::vector<std::string_view> trustQualifiers(const DynamicsValidity& validity)
{
    std::vector<std::string_view> out;
    // 层序＝§5.5 登记序（缺失→估算→外部验证）——确定性输出（NFR-COR-02），
    // 消费方（reporting/selection）按下标解读或在集合语义下自由重排。
    if (validity.frictionMissing) {
        // 第 1 层：摩擦参数缺失（MDL-16 未填写）——结果在输入不完整条件
        // 下产出，汇总层据此走 DataInsufficient（数值已按 0 计入，但证据
        // 不包装精确）。
        out.push_back(kQualifierDataInsufficient);
    }
    if (validity.estimatedLinkCount > 0 || validity.estimatedPayloadCount > 0) {
        // 第 2 层：估算物性（GeometricEstimate 来源或 com/inertia 缺失的
        // 保守估算）——估算值限定语强制（D-DYN-6），不得省略。
        out.push_back(kQualifierEstimated);
    }
    if (validity.externalValidationPending) {
        // 第 3 层：CON-03 Recorded 态引用资源——外部验证未完成限定语。
        out.push_back(kQualifierExternalValidationIncomplete);
    }
    return out;
}

// =====================================================================
// addSeries / addConditionResult / addForwardCheck（fail-fast 调用方错误轨）。
// =====================================================================

void DynamicsEvidenceBuilder::addSeries(const DynamicsSeries& series)
{
    // 单工况语义：同一装配器第二次注入＝调用方契约违约（一个 dyn Profile
    // 证据清单对应一个工况——多工况各自持装配器，逐工况行经 caseScope
    // 区分；跨工况混注会使 EvidenceItem 的 caseScope 语义失真）。
    if (mHaveSeries) {
        throw DynamicsError("evidence-duplicate-series",
                            "同一装配器重复注入序列（工况 obj 序＝"
                                + series.conditionId.toCanonical() + "）——单装配器单工况");
    }
    // 身份块防御校验（§10.0"缺身份拒绝"——正式冻结在 SeriesBuilder::
    // finalize，此处为装配面防线）：身份分量任一缺失即拒绝装配。
    const bool identityOk = series.snapshotId.isValid() && series.sliceId.isValid()
        && series.trajectoryPayloadId.isValid() && series.conditionId.isValid()
        && series.task.isValid() && !series.algorithmVersion.empty()
        && !series.dynConfigDigest.empty();
    if (!identityOk) {
        throw DynamicsError("evidence-series-identity-missing",
                            "序列身份块不完整（snapshotId/sliceId/trajectoryPayloadId/"
                            "conditionId/task/algorithmVersion/dynConfigDigest 任一缺失）"
                            "——§10.0 缺身份拒绝装配");
    }
    mSeries = series;
    mHaveSeries = true;
}

void DynamicsEvidenceBuilder::addConditionResult(const OperatingConditionResult& result)
{
    if (mHaveCondition) {
        throw DynamicsError("evidence-duplicate-condition",
                            "同一装配器重复注入工况结果——单装配器单工况");
    }
    if (!mHaveSeries) {
        // 绑定锚前置：统计结果的工况一致性校验依赖序列身份块——缺锚即
        // 无法防"错误工况引用"（evidence §6.2 EV-COV-2 同源），先注序列。
        throw DynamicsError("evidence-series-missing",
                            "注入工况结果前未注入序列（addSeries 前置）——"
                            "统计结果缺绑定锚");
    }
    if (!(result.conditionId == mSeries.conditionId)) {
        // 错误工况引用＝绑定校验失败（evidence §6.2"防错误工况引用"）——
        // 装配面直接拒绝（调用方错误轨），不降级为 Invalid 证据行。
        throw DynamicsError("evidence-condition-mismatch",
                            "工况结果 conditionId 与序列不一致（结果="
                                + result.conditionId.toCanonical() + "，序列="
                                + mSeries.conditionId.toCanonical() + "）");
    }
    mCondition = result;
    mHaveCondition = true;
}

void DynamicsEvidenceBuilder::addForwardCheck(const ForwardCheckOutcome& outcome)
{
    if (mHaveForward) {
        throw DynamicsError("evidence-duplicate-forward",
                            "同一装配器重复注入正动力学检查产出");
    }
    mForward = outcome;
    mHaveForward = true;
}

// =====================================================================
// build（六项状态判定＋缺失清单＋限定语——§10.6 后置的执行面）。
// =====================================================================

DynamicsEvidence DynamicsEvidenceBuilder::build()
{
    // mItems 恒 6 行（§8.4 表行序）；每行先给 Missing 缺省，再按已注入
    // 素材覆写——"未装配的项显式 Missing、不可省略"（§10.6 非法示例行）。
    mItems.assign(6, evidence::EvidenceItem{});
    mGaps.clear();
    mQualifiers.clear();
    mEvidence = DynamicsEvidence{};

    // 行构造助手：itemId/状态必填；摘要/原因按 presence 纪律填充
    // （Satisfied⇒摘要非零、Invalid⇒原因诊断、NotApplicable⇒原因文本——
    // evidence §6.2；caseScope＝{conditionId} 单工况逐工况记录）。
    auto row = [&](std::size_t idx, evidence::EvidenceItemStatus st,
                   std::optional<core::Digest256> digest,
                   std::optional<core::DiagnosticRecord> invalid,
                   std::optional<std::string> notApplicable,
                   std::optional<core::ObjectId> subject) {
        evidence::EvidenceItem item;
        item.itemId = std::string{kItemOrder[idx]};
        item.status = st;
        item.artifactDigest = std::move(digest);
        item.invalidReason = std::move(invalid);
        item.notApplicableReason = std::move(notApplicable);
        if (mHaveSeries) {
            item.caseScope = std::vector<core::ObjectId>{mSeries.conditionId};
        }
        item.subject = std::move(subject);
        mItems[idx] = std::move(item);
    };

    // 缺失清单追加助手：NotApplicable 不计缺失（C2/EV-VER-7——显式标记
    // 即不降级），其余非 Satisfied 态全量入清单（不因首个缺失短路）。
    auto gap = [&](std::size_t idx, evidence::EvidenceItemStatus st, std::string reason) {
        if (st == evidence::EvidenceItemStatus::NotApplicable
            || st == evidence::EvidenceItemStatus::Satisfied) {
            return;
        }
        EvidenceGap g;
        g.itemId = std::string{kItemOrder[idx]};
        g.status = st;
        g.reason = std::move(reason);
        mGaps.push_back(std::move(g));
    };

    // ---- 行 1 ①dyn.joint-generalized-force-series（Required）----
    if (!mHaveSeries || mSeries.samples.empty()) {
        // 未注入/空序列＝无产物（Empty 语义——§4.6 绝不做 0 值伪装）。
        row(0, evidence::EvidenceItemStatus::Missing, {}, {}, {},
            mHaveSeries ? std::optional<core::ObjectId>{mSeries.conditionId} : std::nullopt);
        gap(0, evidence::EvidenceItemStatus::Missing,
            mHaveSeries ? "序列无样本行（Empty——不产出无值序列）" : "序列未装配");
    } else {
        row(0, evidence::EvidenceItemStatus::Satisfied,
            digestSatisfiedItem(0, mSeries.conditionId, mSeries, nullptr, nullptr), {}, {},
            mSeries.conditionId);
        mEvidence.seriesRefs = {mSeries.conditionId};
    }

    // ---- 行 2 ②dyn.peak-rms-statistics（Required）----
    if (!mHaveCondition || mCondition.peaks.empty()) {
        // 统计器对无 Ok 行序列返回空峰值集（§7.6 Empty 不产出统计）——
        // 无产物即 Missing，绝不以 0 值统计冒充。
        row(1, evidence::EvidenceItemStatus::Missing, {}, {}, {},
            mHaveSeries ? std::optional<core::ObjectId>{mSeries.conditionId} : std::nullopt);
        gap(1, evidence::EvidenceItemStatus::Missing,
            mHaveCondition ? "无有效样本行——峰值/RMS 统计不产出（Empty）" : "工况结果未装配");
    } else {
        row(1, evidence::EvidenceItemStatus::Satisfied,
            digestSatisfiedItem(1, mSeries.conditionId, mSeries, &mCondition, nullptr), {}, {},
            mSeries.conditionId);
        mEvidence.statsRefs = {mSeries.conditionId};
    }

    // ---- 行 3 ③dyn.property-friction-provenance（Required——DYN-06 核心）----
    if (!mHaveSeries) {
        row(2, evidence::EvidenceItemStatus::Missing, {}, {}, {}, {});
        gap(2, evidence::EvidenceItemStatus::Missing, "序列未装配——无来源标记素材");
    } else {
        const DynamicsValidity& v = mSeries.validity;
        // 三层降级判定（§5.5 互斥呈现：Invalid〔缺失/估算〕优先于
        // Unverified〔外部验证〕——保守纪律：任何不满足都完整呈现，汇总
        // 层对两态同等对待〔决策表④〕，优先序只影响 reason 的归层文案）。
        if (v.frictionMissing) {
            // 第 1 层：摩擦参数缺失（MDL-16 未填写→DataInsufficient 素材
            // ——V-09 行"property-friction-provenance=Invalid/降级标记"）。
            const std::string cause =
                "关节摩擦参数缺失（MDL-16）——分项按 0 计入、评估继续，但证据"
                "降级 DataInsufficient（DYN-06），不得包装为精确结论";
            row(2, evidence::EvidenceItemStatus::Invalid, {},
                makeInvalidReason(std::string{kDynFrictionMissing}, mSeries.conditionId, cause),
                {}, mSeries.conditionId);
            gap(2, evidence::EvidenceItemStatus::Invalid,
                "物性/摩擦来源标记不满足：关节摩擦参数缺失（DYN-FRICTION-MISSING）");
        } else if (v.estimatedLinkCount > 0 || v.estimatedPayloadCount > 0) {
            // 第 2 层：估算物性在场（GeometricEstimate 来源或 com/inertia
            // 缺失保守估算——V-10 行"estimated 计数＋限定语 estimated；
            // 不升级精确"）。
            const std::string cause = "物性含估算来源（连杆估算 "
                + std::to_string(v.estimatedLinkCount) + " 个、末端件估算 "
                + std::to_string(v.estimatedPayloadCount)
                + " 个）——低估惯性提示，证据降级（DYN-PROPERTY-DOWNGRADED），"
                  "估算结果不得包装为精确结论（DYN-06）";
            row(2, evidence::EvidenceItemStatus::Invalid, {},
                makeInvalidReason(std::string{kDynPropertyDowngraded}, mSeries.conditionId, cause),
                {}, mSeries.conditionId);
            gap(2, evidence::EvidenceItemStatus::Invalid,
                "物性/摩擦来源标记不满足：估算物性在场（DYN-PROPERTY-DOWNGRADED）");
        } else if (v.externalValidationPending) {
            // 第 3 层：CON-03 Recorded 态引用资源——产物存在但未经外部
            // 验证＝Unverified 的 evidence 本义（§6.2"产物存在但未在满足
            // 正式要求的条件下验证"；诊断区别于 Missing——状态行本身即
            // 机器判读面，报告限定语 external-validation-incomplete 随
            // trustQualifiers 供数）。
            row(2, evidence::EvidenceItemStatus::Unverified, {}, {}, {}, mSeries.conditionId);
            gap(2, evidence::EvidenceItemStatus::Unverified,
                "物性来源为外部验证未完成资源（CON-03 Recorded）——正式判定不采信");
        } else {
            // 全干净：全部物性已提供、无估算来源、无外部验证待定——来源
            // 标记完整（结果可作精确结论呈现——无限定语，不弱化也不添加）。
            row(2, evidence::EvidenceItemStatus::Satisfied,
                digestSatisfiedItem(2, mSeries.conditionId, mSeries, nullptr, nullptr), {}, {},
                mSeries.conditionId);
            mEvidence.provenanceRefs = {mSeries.conditionId};
        }
    }

    // ---- 行 4 ④dyn.load-condition-identity（Required）----
    if (!mHaveSeries) {
        row(3, evidence::EvidenceItemStatus::Missing, {}, {}, {}, {});
        gap(3, evidence::EvidenceItemStatus::Missing, "序列未装配——无负载工况标识素材");
    } else {
        // 身份块经 addSeries 防御校验（conditionId/工具身份有效）——负载
        // 工况标识（conditionId＋toolObjectId＋payloadVariant 覆盖集）齐备。
        row(3, evidence::EvidenceItemStatus::Satisfied,
            digestSatisfiedItem(3, mSeries.conditionId, mSeries, nullptr, nullptr), {}, {},
            mSeries.conditionId);
        mEvidence.loadConditionRefs = {mSeries.conditionId};
    }

    // ---- 行 5 ⑤dyn.power-energy-split（Suggested——数据完整时产出）----
    if (!mHaveCondition) {
        row(4, evidence::EvidenceItemStatus::Missing, {}, {}, {}, {});
        gap(4, evidence::EvidenceItemStatus::Missing, "功率/能量摘要未装配（建议项——不阻断）");
    } else if (!mCondition.powerEnergy.timeParamAvailable) {
        // 无时间参数→能量/平均功率字段显式 NaN（§4.6 不伪造）——NaN 形态
        // 不是产物，建议项 Missing（suggestedGaps 单独标注，不阻断④级）。
        row(4, evidence::EvidenceItemStatus::Missing, {}, {}, {}, mSeries.conditionId);
        gap(4, evidence::EvidenceItemStatus::Missing,
            "时间参数不可用——能量/平均功率字段显式无效（不伪造积分，§4.6）");
    } else {
        row(4, evidence::EvidenceItemStatus::Satisfied,
            digestSatisfiedItem(4, mSeries.conditionId, mSeries, &mCondition, nullptr), {}, {},
            mSeries.conditionId);
        mEvidence.powerEnergyAvailable = true;
    }

    // ---- 行 6 ⑥dyn.forward-dynamics-consistency（Suggested）----
    if (!mHaveForward) {
        // 建议项未执行＝缺失（不阻断——suggestedGaps 单独标注）。
        row(5, evidence::EvidenceItemStatus::Missing, {}, {}, {},
            mHaveSeries ? std::optional<core::ObjectId>{mSeries.conditionId} : std::nullopt);
        gap(5, evidence::EvidenceItemStatus::Missing,
            "正动力学一致性检查未装配（建议项——不阻断）");
    } else {
        switch (mForward.state) {
        case DynamicsValidity::ForwardCheckState::Passed:
            row(5, evidence::EvidenceItemStatus::Satisfied,
                digestSatisfiedItem(5, mSeries.conditionId, mSeries, nullptr, &mForward), {}, {},
                mHaveSeries ? std::optional<core::ObjectId>{mSeries.conditionId} : std::nullopt);
            mEvidence.forwardCheckAvailable = true;
            break;
        case DynamicsValidity::ForwardCheckState::Failed: {
            // 失败＝Invalid＋检查器附带的首条 DYN-FD-* 诊断（比较型素材/
            // 发散/数值异常——失败不能判定模型无效，§6.3.5；建议项 Invalid
            // 不阻断必需面）。防御面：检查器 Failed 必附诊断，空清单属实
            // 现违约——以一致性失败码合成保守原因，不伪造"通过"。
            core::DiagnosticRecord reason =
                mForward.diagnostics.empty()
                    ? makeInvalidReason(std::string{kDynFdConsistencyFailed},
                                        mSeries.conditionId,
                                        "正动力学一致性检查 Failed 但未附诊断素材"
                                        "（防御性合成——实现违约面）")
                    : mForward.diagnostics.front();
            if (!reason.subject.has_value() && mHaveSeries) {
                reason.subject = mSeries.conditionId;  // 可定位纪律（ERR-01）
            }
            row(5, evidence::EvidenceItemStatus::Invalid, {}, std::move(reason), {},
                mHaveSeries ? std::optional<core::ObjectId>{mSeries.conditionId} : std::nullopt);
            gap(5, evidence::EvidenceItemStatus::Invalid,
                "正动力学一致性检查失败（建议项——失败不判定模型无效，§6.3.5）");
            break;
        }
        case DynamicsValidity::ForwardCheckState::NotRun:
            // 数据不足/取消→未产出（V-14：建议项缺失不阻断、不伪造 Passed；
            // 取消零错误诊断 UX-03——状态行 Missing 承载，不附原因诊断）。
            row(5, evidence::EvidenceItemStatus::Missing, {}, {}, {},
                mHaveSeries ? std::optional<core::ObjectId>{mSeries.conditionId} : std::nullopt);
            gap(5, evidence::EvidenceItemStatus::Missing,
                "正动力学一致性检查未执行（数据不足/取消——NotRun）");
            break;
        case DynamicsValidity::ForwardCheckState::NotApplicable:
            // mode=Skip＝显式不适用（§6.3.4——非缺失，C2/EV-VER-7：不计
            // 缺失、原因必填、不降级）。
            row(5, evidence::EvidenceItemStatus::NotApplicable, {}, {},
                std::string{"forwardCheck.mode=Skip——显式不适用（§6.3.4），非缺失"},
                mHaveSeries ? std::optional<core::ObjectId>{mSeries.conditionId} : std::nullopt);
            break;
        }
    }

    // ---- 限定语缓存（§5.5 三层——序列已注入时判定；无序列＝空清单）----
    if (mHaveSeries) {
        mQualifiers = trustQualifiers(mSeries.validity);
    }

    mBuilt = true;
    return mEvidence;
}

// =====================================================================
// 访问器（build 后读取——未 build 返回空，不预生成）。
// =====================================================================

std::vector<evidence::EvidenceItem> DynamicsEvidenceBuilder::evidenceItems() const
{
    return mItems;  // 未 build 时 mItems 为空——不预生成（防半成品清单逸出）
}

std::vector<EvidenceGap> DynamicsEvidenceBuilder::missingList() const
{
    return mGaps;
}

std::vector<std::string_view> DynamicsEvidenceBuilder::trustQualifiersCached() const
{
    return mQualifiers;
}

}  // namespace sdurws::ird::dynamics
