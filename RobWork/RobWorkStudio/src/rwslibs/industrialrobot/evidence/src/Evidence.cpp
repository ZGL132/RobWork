/**
 * @file   Evidence.cpp
 * @brief  证据契约校验器实现——Profile 注册期校验与内容身份计算（§6.1）、
 *         证据项 presence 纪律/清单绑定/完备性核对（§6.2）、Verified 模式
 *         前置固化校验（§6.2/CON-03）、证明字段级校验 validateProof
 *         （§6.3）、覆盖矩阵与区域采样证据校验（§6.6）。
 *
 * 设计依据：
 *   - units/evidence.md §6.1～§6.3、§6.6、§9.5（Profile 注册期语法面）、
 *     §12 EV-T05 行；契约 tasks/foundation/EV-T05.json acceptance 1～4
 *   - 校验器均为非抛出纯函数（契约头 Evidence.hpp 声明——§2.1"try* 分轨"
 *     先例）：返回 issue 清单、检查序固定、同一坏输入必得同一 issue 序列
 *     （NFR-COR-02）；全部实现无共享可变状态（并发只读安全）。
 *
 * Profile 内容身份编码（computeProfileContentIdentity——实现口径登记
 * 单元卡 v0.6）：非往返身份投影（SliceCodec baseline-projection 同款纪律
 * ——无 parse，唯一消费方式＝摘要）。字节布局：
 *   magic "IRDPRF1"（8 字节）｜codec 版本 0x01（1 字节）
 *   ｜profileId：u32 长度前缀＋字节｜version：同
 *   ｜required：u32 条数＋逐项（按 itemId 字典序规范化）
 *   ｜suggested：同 required
 * 每项编码：itemId（u32＋字节）｜itemClass（1 字节）｜substitutable
 * （1 字节 0/1）｜适用条件 presence（1 字节 0/1；有值时：conditionToken
 * u32＋字节、u32 键数＋逐键 u32＋字节）｜description（u32＋字节）。
 * 整型一律大端（§5.2 规则表同源）；摘要唯一经 core::ContentDigester
 * （CR-02——本单元不自建摘要）。
 *
 * 各校验器检查序见契约头对应 @brief 与 ProofIssueCode/ProfileIssueCode 等
 * 枚举声明序（枚举顺序＝检查序——注释纪律，Issues 输出逐枚举序产出）。
 */

#include <sdurws/ird/evidence/Evidence.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace sdurws::ird::evidence {
namespace {

// =====================================================================
// 编码原语（computeProfileContentIdentity 专用——规范字节流拼装）
// =====================================================================

/// 追加大端 u32（§5.2 定宽整型大端——同 codec 家族）。
void appendU32(std::vector<std::uint8_t>& out, std::uint32_t v)
{
    out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>(v & 0xFFu));
}

/// 追加长度前缀字符串（u32 长度＋原始字节——UTF-8 透明承载）。
void appendLenStr(std::vector<std::uint8_t>& out, const std::string& s)
{
    appendU32(out, static_cast<std::uint32_t>(s.size()));
    out.insert(out.end(), s.begin(), s.end());
}

/// 追加单条 Profile 项的规范编码（顺序＝契约头编码布局注释）。
void appendProfileItem(std::vector<std::uint8_t>& out, const EvidenceProfileItem& item)
{
    appendLenStr(out, item.itemId);
    out.push_back(static_cast<std::uint8_t>(item.itemClass));
    out.push_back(item.substitutableByInfeasibility ? 0x01u : 0x00u);
    // 适用条件 presence 字节：缺失（Always）≠空条件——两态必须可区分
    // （§5.2"optional 缺失与空值不等价"）。
    if (item.applicability.has_value()) {
        out.push_back(0x01u);
        appendLenStr(out, item.applicability->conditionToken);
        appendU32(out, static_cast<std::uint32_t>(item.applicability->referencedKeys.size()));
        for (const std::string& key : item.applicability->referencedKeys) {
            appendLenStr(out, key);
        }
    } else {
        out.push_back(0x00u);
    }
    appendLenStr(out, item.description);
}

/// 按 itemId 字典序规范化后的项序列（同内容任意登记序同身份——NFR-COR-02；
/// 排序是编码前的规范化，不修改调用方数据）。
std::vector<const EvidenceProfileItem*> sortedByItemId(const std::vector<EvidenceProfileItem>& items)
{
    std::vector<const EvidenceProfileItem*> sorted;
    sorted.reserve(items.size());
    for (const EvidenceProfileItem& item : items) {
        sorted.push_back(&item);
    }
    std::sort(sorted.begin(), sorted.end(),
              [](const EvidenceProfileItem* a, const EvidenceProfileItem* b) {
                  return a->itemId < b->itemId;
              });
    return sorted;
}

/// 计数是否含 NUL 的共用工装（词形下限——isWellFormedToken 的本地复述，
/// 避免为内部 lambda 之外暴露新契约）。
bool containsNul(const std::string& s) noexcept
{
    return s.find('\0') != std::string::npos;
}

/// 三计数之和是否恰等于期望分母（溢出安全——计数为 u64，直接相加可能回绕；
/// 回绕的和"碰巧等于"分母会把分母残缺误判为完整，必须先防溢出）。
bool countsSumEquals(std::uint64_t a, std::uint64_t b, std::uint64_t c, std::uint64_t expected)
{
    const std::uint64_t kMax = std::numeric_limits<std::uint64_t>::max();
    if (a > kMax - b) {
        return false;  // a+b 溢出——总和超出 u64 表示域，必不等于合法分母
    }
    const std::uint64_t ab = a + b;
    if (ab > kMax - c) {
        return false;  // +c 溢出——同上
    }
    return ab + c == expected;
}

}  // namespace

// =====================================================================
// §6.1 通用必需项（表 4"通用必需项"原文三行——D-14：不复制域明细）
// =====================================================================

std::vector<EvidenceProfileItem> commonRequiredItems()
{
    // 三行的 itemId 为 evidence 侧稳定键（"common" 域前缀——登记单元卡
    // v0.6）；description 锚定需求 §8.1 表 4 通用必需项原文行文（内容
    // 权威在需求侧，此处为登记文案）。全部 Common 类＋替代标志 false
    // （C6：通用门禁不被替代规则豁免）＋无适用条件（恒适用）。
    EvidenceProfileItem snapshotIdentity;
    snapshotIdentity.itemId = "common.snapshot-identity";
    snapshotIdentity.itemClass = EvidenceItemClass::Common;
    snapshotIdentity.description =
        "完整 AnalysisSnapshot 身份（含已解析策略与 RuntimeNameMap 内容身份——CON-06）";
    snapshotIdentity.substitutableByInfeasibility = false;

    EvidenceProfileItem caseCoverage;
    caseCoverage.itemId = "common.case-coverage-matrix";
    caseCoverage.itemClass = EvidenceItemClass::Common;
    caseCoverage.description = "必验工况覆盖矩阵（EVI-02）";
    caseCoverage.substitutableByInfeasibility = false;

    EvidenceProfileItem modeGrade;
    modeGrade.itemId = "common.mode-evidence-grade";
    modeGrade.itemClass = EvidenceItemClass::Common;
    modeGrade.description = "评估模式与证据等级标识";
    modeGrade.substitutableByInfeasibility = false;

    return {std::move(snapshotIdentity), std::move(caseCoverage), std::move(modeGrade)};
}

// =====================================================================
// §6.1/§9.5 Profile 注册期校验
// =====================================================================

std::vector<ProfileIssue> validateEvidenceProfile(const RequiredEvidenceProfile& profile)
{
    std::vector<ProfileIssue> issues;

    // ---- Profile 级：域词表（封闭五域——§6.1 profileId 注释）与版本串 ----
    if (!isDomainProfileId(profile.profileId)) {
        issues.push_back({ProfileIssueCode::ProfileIdInvalid, {}, ProfileIssue::npos,
                          "profileId 不在五域词表（应为 kin|trj|dyn|sel|opt——§8.1 表 4）"});
    }
    if (profile.version.empty() || containsNul(profile.version)) {
        issues.push_back({ProfileIssueCode::ProfileVersionInvalid, {}, ProfileIssue::npos,
                          "version 为空或含 NUL（语义化版本串下限）"});
    }

    // ---- 项级：required∪suggested 合并序列逐项（先 required 后 suggested
    // ——index 语义与契约头一致）；每项内按枚举检查序产出。----
    std::size_t mergedIndex = 0;
    for (const std::vector<EvidenceProfileItem>* list : {&profile.required, &profile.suggested}) {
        for (const EvidenceProfileItem& item : *list) {
            const std::size_t idx = mergedIndex++;
            // 项 id 词形（"<域>.<项>"——isValidProfileItemId）。
            if (!isValidProfileItemId(item.itemId)) {
                issues.push_back({ProfileIssueCode::ItemIdSyntax, item.itemId, idx,
                                  "itemId 词形非法（应为 <域>.<项> 两段 kebab）"});
            }
            // 人读说明非空（表 4 行文锚定——空说明＝登记不完整，评审无据）。
            if (item.description.empty() || containsNul(item.description)) {
                issues.push_back({ProfileIssueCode::ItemDescriptionInvalid, item.itemId, idx,
                                  "description 为空或含 NUL（须锚定表 4 行文）"});
            }
            // Common 类禁止域登记（单一权威——Common 项唯一来源＝
            // commonRequiredItems，域重复登记即双账本，NFR-MNT-03）。
            if (item.itemClass == EvidenceItemClass::Common) {
                issues.push_back({ProfileIssueCode::CommonItemInDomainProfile, item.itemId, idx,
                                  "Common 类禁止域登记（通用必需项由 evidence 内建、隐式附加）"});
                // 替代标志一致性（C6）：Common＝通用门禁类，替代豁免只给
                // "因不可行而无法生成的成功产物"——门禁类必须 false。
                if (item.substitutableByInfeasibility) {
                    issues.push_back({ProfileIssueCode::SubstitutableFlagInconsistent,
                                      item.itemId, idx,
                                      "Common 类替代标志必须 false（C6：通用门禁不豁免）"});
                }
            }
            // 适用条件词形（§9.5：Profile 注册期做语法校验；与评估器声明的
            // 闭包交叉校验归评估器注册时——本处不查闭包）。
            if (item.applicability.has_value()) {
                const Applicability& cond = *item.applicability;
                if (!isWellFormedToken(cond.conditionToken)) {
                    issues.push_back({ProfileIssueCode::ApplicabilityTokenInvalid, item.itemId,
                                      idx, "conditionToken 为空或含 NUL（编码安全下限）"});
                }
                if (cond.referencedKeys.empty()) {
                    issues.push_back({ProfileIssueCode::ReferencedKeysEmpty, item.itemId, idx,
                                      "referencedKeys 为空（条件必须有决定键）"});
                }
                for (const std::string& key : cond.referencedKeys) {
                    if (!isValidDependencyKey(key)) {
                        issues.push_back({ProfileIssueCode::ReferencedKeySyntax, item.itemId, idx,
                                          "referencedKey 语法非法（[a-z][a-z0-9.-]{2,63}）"});
                    }
                }
            }
        }
    }
    return issues;
}

core::ContentIdentity computeProfileContentIdentity(const RequiredEvidenceProfile& profile)
{
    // 规范字节流（布局见本文件头注释）——排序在前（同内容同字节，NFR-COR-02），
    // 摘要唯一经 core::ContentDigester（CR-02）。
    std::vector<std::uint8_t> encoding;
    encoding.reserve(64);
    const std::string_view magic{"IRDPRF1"};
    encoding.insert(encoding.end(), magic.begin(), magic.end());
    encoding.push_back(0x01);  // codec 版本（升版＝全体 Profile 身份变化——破坏性变更走评审）

    appendLenStr(encoding, profile.profileId);
    appendLenStr(encoding, profile.version);

    const auto appendList = [&encoding](const std::vector<EvidenceProfileItem>& items) {
        appendU32(encoding, static_cast<std::uint32_t>(items.size()));
        for (const EvidenceProfileItem* item : sortedByItemId(items)) {
            appendProfileItem(encoding, *item);
        }
    };
    appendList(profile.required);
    appendList(profile.suggested);

    core::ContentDigester digester;
    digester.update(encoding.data(), encoding.size());
    core::ContentIdentity identity;
    identity.bytes = digester.finalize();  // Digest256 即摘要字节本体（core §4.2）
    return identity;
}

// =====================================================================
// §6.2 证据项 presence 纪律
// =====================================================================

std::vector<EvidenceItemIssue> validateEvidenceItems(const std::vector<EvidenceItem>& items)
{
    std::vector<EvidenceItemIssue> issues;
    for (std::size_t i = 0; i < items.size(); ++i) {
        const EvidenceItem& item = items[i];
        // 规则 1：itemId 与 Profile 项同词形（证据按 itemId 对应 Profile 项
        // ——词形分叉会让"对应"关系无法机械核对）。
        if (!isValidProfileItemId(item.itemId)) {
            issues.push_back({EvidenceItemIssueCode::ItemIdSyntax, item.itemId, i,
                              "itemId 词形非法（应为 <域>.<项> 两段 kebab）"});
        }
        // 规则 2：Satisfied 必带非零产物摘要（无凭据的"已满足"＝伪造面，
        // NFR-COR-03；全零保留值视同缺失）。
        if (item.status == EvidenceItemStatus::Satisfied
            && (!item.artifactDigest.has_value()
                || *item.artifactDigest == core::Digest256{})) {
            issues.push_back({EvidenceItemIssueCode::SatisfiedDigestMissing, item.itemId, i,
                              "Satisfied 项必须携带非零 artifactDigest（§6.2 presence 纪律）"});
        }
        // 规则 3：Invalid 必带原因诊断（ERR-01：不伪造、失败留痕）。
        if (item.status == EvidenceItemStatus::Invalid && !item.invalidReason.has_value()) {
            issues.push_back({EvidenceItemIssueCode::InvalidReasonMissing, item.itemId, i,
                              "Invalid 项必须携带 invalidReason 诊断（ERR-01）"});
        }
        // 规则 4：NotApplicable 必带非空原因（C2：不适用显式标记——
        // EV-VER-7 的观测面；空串原因＝未解释的不适用）。
        if (item.status == EvidenceItemStatus::NotApplicable
            && (!item.notApplicableReason.has_value() || item.notApplicableReason->empty())) {
            issues.push_back({EvidenceItemIssueCode::NotApplicableReasonMissing, item.itemId, i,
                              "NotApplicable 项必须携带非空 notApplicableReason（C2/ERR-01）"});
        }
    }
    return issues;
}

// =====================================================================
// §6.2 证据清单绑定校验（防错误工况/对象引用——EV-COV-2 同源）
// =====================================================================

std::vector<ManifestBindingIssue>
validateEvidenceManifestBinding(const EvidenceManifest& manifest, const AnalysisSnapshot& snapshot)
{
    std::vector<ManifestBindingIssue> issues;

    // ---- 清单身份面：三个身份非零＋Profile 域词表＋版本串（绑定三元组
    // 是 envelope.evidence 的追溯面——任一残缺即清单不可追溯）。----
    if (!manifest.snapshotId.isValid() || !manifest.sliceId.isValid()
        || !manifest.profileContentIdentity.isValid() || !isDomainProfileId(manifest.profileId)
        || manifest.profileVersion.empty() || containsNul(manifest.profileVersion)) {
        issues.push_back({ManifestBindingIssueCode::ManifestIdentityInvalid, {},
                          ManifestBindingIssue::npos,
                          "清单身份面非法（snapshotId/sliceId/profileContentIdentity 须非零、"
                          "profileId 须为五域词表、profileVersion 须非空无 NUL）"});
    }

    // ---- 逐项绑定：caseScope ⊆ 快照冻结必验工况集、subject ∈ 对象闭包
    // （§6.2 原文——防错误工况/对象引用，EV-COV-2 同源；汇总器采信清单
    // 前调用）。----
    for (std::size_t i = 0; i < manifest.items.size(); ++i) {
        const EvidenceItem& item = manifest.items[i];
        if (item.caseScope.has_value()) {
            for (const CaseId& caseId : *item.caseScope) {
                bool found = false;
                for (const CaseEntry& entry : snapshot.caseSet.entries) {
                    if (entry.caseId == caseId) {
                        found = true;
                        break;
                    }
                }
                // 引用快照必验集之外的工况＝证据声称覆盖了不存在的输入
                // （错误工况引用——采信即判定失真）。
                if (!found) {
                    issues.push_back({ManifestBindingIssueCode::CaseScopeNotInSnapshot,
                                      item.itemId, i,
                                      "caseScope 引用快照必验工况集之外的工况（错误工况引用）"});
                }
            }
        }
        if (item.subject.has_value()) {
            bool found = false;
            for (const ObjectRefEntry& ref : snapshot.objectClosure) {
                if (ref.objectId == *item.subject) {
                    found = true;
                    break;
                }
            }
            // 对象闭包之外的对象＝快照未声明其内容版本——证据对该对象的
            // 断言不可追溯（CON-01 闭包完整性）。
            if (!found) {
                issues.push_back({ManifestBindingIssueCode::SubjectNotInObjectClosure,
                                  item.itemId, i, "subject 不在快照 objectClosure 内（错误对象引用）"});
            }
        }
    }
    return issues;
}

// =====================================================================
// §6.2 完备性核对（EvidenceMissing 码面数据源——EV-VER-7 判定面）
// =====================================================================

EvidenceCompletenessResult checkEvidenceCompleteness(const RequiredEvidenceProfile& profile,
                                                     const EvidenceManifest& manifest)
{
    EvidenceCompletenessResult result;

    // 清单按 itemId 的首个匹配索引（清单构造方保证 id 唯一——重复项为
    // 构造方违约，此处取首个匹配并保持确定性；不额外判定重复）。
    const auto findItem = [&manifest](const std::string& itemId) -> const EvidenceItem* {
        for (const EvidenceItem& item : manifest.items) {
            if (item.itemId == itemId) {
                return &item;
            }
        }
        return nullptr;
    };

    // 必需项逐项核对：未出现→Missing；NotApplicable→不计缺失（EV-VER-7：
    // "整体不因该缺项降级"由本行保证）；Satisfied→满足；其余三态
    // （Missing/Invalid/Unverified）→不满足、全量列出（不短路——表 2 ④）。
    for (const EvidenceProfileItem& required : profile.required) {
        const EvidenceItem* item = findItem(required.itemId);
        if (item == nullptr) {
            result.requiredGaps.push_back({required.itemId, EvidenceItemStatus::Missing});
            continue;
        }
        switch (item->status) {
        case EvidenceItemStatus::Satisfied:
        case EvidenceItemStatus::NotApplicable:
            break;  // 满足／不适用不计缺失（C2/ERR-01）
        case EvidenceItemStatus::Missing:
        case EvidenceItemStatus::Invalid:
        case EvidenceItemStatus::Unverified:
            result.requiredGaps.push_back({required.itemId, item->status});
            break;
        }
    }

    // 建议项：缺失不阻断、单独标注（表 4"建议证据项"列语义）；同样
    // NotApplicable 不计入。
    for (const EvidenceProfileItem& suggested : profile.suggested) {
        const EvidenceItem* item = findItem(suggested.itemId);
        if (item == nullptr) {
            result.suggestedGaps.push_back(suggested.itemId);
            continue;
        }
        if (item->status != EvidenceItemStatus::Satisfied
            && item->status != EvidenceItemStatus::NotApplicable) {
            result.suggestedGaps.push_back(suggested.itemId);
        }
    }
    return result;
}

// =====================================================================
// §6.2 Verified 模式前置校验（CON-03/PM-01）
// =====================================================================

std::vector<SnapshotModeIssue> validateSnapshotForMode(const AnalysisSnapshot& snapshot,
                                                       core::EvaluationMode mode)
{
    std::vector<SnapshotModeIssue> issues;

    // Verified 前置①：被消费外部资源全部固化（CON-03/PM-01——未固化即
    // 阻断正式结论，证据不足口径；逐条列出涉事资源便于定位）。Quick/
    // Preview 不强制固化（表 1：其产物本就不支撑正式结论）。
    if (mode == core::EvaluationMode::Verified) {
        for (std::size_t i = 0; i < snapshot.externalResources.size(); ++i) {
            if (snapshot.externalResources[i].state == ExternalResourceStatus::Recorded) {
                issues.push_back({SnapshotModeIssueCode::ExternalResourceNotSolidified, i,
                                  "Verified 模式存在未固化（Recorded）的被消费外部资源"
                                  "（CON-03：未固化即阻断正式结论）"});
            }
        }
    }

    // Verified 前置②（各模式通用面）：复现块版本要素完整——builder 冻结
    // 已挡，此处对快照值复核（手工构造/反序列化路径同受约束，防线不因
    // 调用路径而缺）。
    if (snapshot.reproduction.productVersion.empty()
        || snapshot.reproduction.evidenceContractVersion.empty()) {
        issues.push_back({SnapshotModeIssueCode::ReproductionIncomplete,
                          SnapshotModeIssue::npos,
                          "复现块版本要素不完整（productVersion/evidenceContractVersion 须非空）"});
    }
    return issues;
}

// =====================================================================
// §6.3 证明字段级校验（validateProof——"缺一即 Invalid"）
// =====================================================================

std::vector<ProofIssue> validateProof(const DeterministicInfeasibilityProof& proof,
                                      const IProducerRegistryView& producerRegistry,
                                      const AnalysisSnapshot& snapshot,
                                      const core::ContentIdentity& expectedSliceId)
{
    std::vector<ProofIssue> issues;

    // ---- ①类别合法（三类之外即"证明"不成立——EV-VER-2/3/4 的拒绝面：
    // "多初值全发散"等搜索未果情形不得冒充证明，走 SearchExhaustedRecord
    // →DataInsufficient 口径）。枚举值域外只能经非法转换到达——防御性
    // 范围检查。----
    if (static_cast<std::uint8_t>(proof.category)
        > static_cast<std::uint8_t>(ProofCategory::MandatoryStateCollision)) {
        issues.push_back({ProofIssueCode::CategoryInvalid, "category", ProofIssue::npos,
                          "类别不在三类词表（AnalyticBound|ConstraintContradiction|"
                          "MandatoryStateCollision——数值搜索未果不是证明）"});
    }

    // ---- ②类别条件字段齐备（§6.3"按 category 的条件必填"——逐字段
    // 校验，缺一即 Invalid；EV-VER-6 的"字段级校验逐项观测"面）。----
    switch (proof.category) {
    case ProofCategory::AnalyticBound:
        if (proof.boundExpression.empty()) {
            issues.push_back({ProofIssueCode::BoundExpressionMissing, "boundExpression",
                              ProofIssue::npos, "AnalyticBound 证明必须携带界限表达"});
        }
        break;
    case ProofCategory::ConstraintContradiction:
        if (proof.contradictionExpression.empty()) {
            issues.push_back({ProofIssueCode::ContradictionExpressionMissing,
                              "contradictionExpression", ProofIssue::npos,
                              "ConstraintContradiction 证明必须携带约束矛盾表达"});
        }
        break;
    case ProofCategory::MandatoryStateCollision:
        // 必经状态三要素逐项观测（EV-VER-6 反例：缺 mandatoryState 的同款
        // 证明→Invalid——缺失要素在此逐个列出）。
        if (proof.mandatoryState.stateKind.empty() || containsNul(proof.mandatoryState.stateKind)) {
            issues.push_back({ProofIssueCode::MandatoryStateKindMissing,
                              "mandatoryState.stateKind", ProofIssue::npos,
                              "必经状态碰撞证明必须携带 stateKind（域登记词表 token）"});
        }
        if (proof.mandatoryState.nonSelectabilityBasis.empty()) {
            issues.push_back({ProofIssueCode::MandatoryStateBasisMissing,
                              "mandatoryState.nonSelectabilityBasis", ProofIssue::npos,
                              "必经状态碰撞证明必须携带不可选择性依据（必经性）"});
        }
        if (!proof.mandatoryState.objectId.isValid()) {
            issues.push_back({ProofIssueCode::MandatoryStateObjectInvalid,
                              "mandatoryState.objectId", ProofIssue::npos,
                              "必经状态所属对象身份非法（保留值）"});
        }
        // 碰撞对象对：非空（无证据来源的碰撞主张＝空口断言）＋逐对合法。
        if (proof.collisionPairs.empty()) {
            issues.push_back({ProofIssueCode::CollisionPairsMissing, "collisionPairs",
                              ProofIssue::npos, "必经状态碰撞证明必须携带碰撞对象对清单"});
        }
        for (std::size_t i = 0; i < proof.collisionPairs.size(); ++i) {
            const CollisionPair& pair = proof.collisionPairs[i];
            // 对象对合法性：双方非零＋A≠B（"对"要求两个不同对象）。
            const bool idsValid = pair.objectIdA.isValid() && pair.objectIdB.isValid()
                && !(pair.objectIdA == pair.objectIdB);
            // 判定必须为肯定（碰撞对是碰撞主张的构型级证据来源——否定
            // 判定不能支撑主张，NFR-COR-03 不伪造）。
            if (!idsValid || !pair.inCollision) {
                issues.push_back({ProofIssueCode::CollisionPairInvalid, "collisionPairs", i,
                                  "碰撞对象对非法（双方身份须非零且不同、判定须为碰撞）"});
            }
        }
        break;
    }

    // ---- ③产生者已注册且契约版本相符（§6.3：未注册产生者的"证明"不可
    // 追溯；契约版本不符＝算法契约已变〔CON-04〕）。词形非法时先拒绝、
    // 不查表（查表前提是键可被注册表无歧义识别）。----
    if (!isValidEvaluationKey(proof.producer)) {
        issues.push_back({ProofIssueCode::ProducerKeyInvalid, "producer", ProofIssue::npos,
                          "producer 评估键词形非法（[a-z][a-z0-9-]{1,63}）"});
    } else if (!producerRegistry.isRegistered(proof.producer)) {
        issues.push_back({ProofIssueCode::ProducerNotRegistered, "producer", ProofIssue::npos,
                          "producer 未注册（产生者必须已登记于评估器注册表）"});
    } else if (!producerRegistry.contractVersionMatches(proof.producer,
                                                        proof.producerContractVersion)) {
        issues.push_back({ProofIssueCode::ContractVersionMismatch, "producerContractVersion",
                          ProofIssue::npos, "producer 契约版本与注册值不符（CON-04）"});
    }

    // ---- ④快照/切片绑定（§6.3：非空且与被汇总结果一致——绑定失败＝
    // 证明针对别的输入，采信即张冠李戴）。----
    if (!proof.snapshotId.isValid()) {
        issues.push_back({ProofIssueCode::SnapshotIdMissing, "snapshotId", ProofIssue::npos,
                          "snapshotId 为空身份（证明必须绑定输入快照）"});
    } else if (!(proof.snapshotId == snapshot.snapshotId)) {
        issues.push_back({ProofIssueCode::SnapshotIdMismatch, "snapshotId", ProofIssue::npos,
                          "snapshotId 与被汇总快照不一致"});
    }
    if (!proof.sliceId.isValid()) {
        issues.push_back({ProofIssueCode::SliceIdMissing, "sliceId", ProofIssue::npos,
                          "sliceId 为空身份（证明必须绑定输入切片）"});
    } else if (!(proof.sliceId == expectedSliceId)) {
        issues.push_back({ProofIssueCode::SliceIdMismatch, "sliceId", ProofIssue::npos,
                          "sliceId 与被汇总结果切片不一致"});
    }

    // ---- ⑤coverageClaim 句法（§6.3 两规范声明按类别核对——作用域声明
    // 错配＝C8 任务级作用域越界，如构型级碰撞冒充"覆盖全部允许选择"）。----
    const bool claimOk = proof.category == ProofCategory::MandatoryStateCollision
        ? proof.coverageClaim == kCoverageClaimMandatoryState
        : proof.coverageClaim == kCoverageClaimAllAlternatives;
    if (!claimOk) {
        issues.push_back({ProofIssueCode::CoverageClaimInvalid, "coverageClaim",
                          ProofIssue::npos,
                          "coverageClaim 与类别规范声明不符（Analytic/Contradiction→"
                          "覆盖全部允许选择；MandatoryState→该必经状态）"});
    }
    return issues;
}

// =====================================================================
// §6.6 覆盖矩阵校验（EVI-02——分母＝快照冻结态必验工况集）
// =====================================================================

CoverageCheckResult validateCaseCoverageMatrix(const CaseCoverageMatrix& matrix,
                                               const AnalysisSnapshot& snapshot)
{
    CoverageCheckResult result;

    // ---- ①与计划核对（§6.6 第 1 条）：矩阵声明的必验集身份必须等于
    // 快照冻结值——对不上即分母被调包（②级门禁失败面，EV-T06 消费）。----
    if (!(matrix.requiredCaseSetId == snapshot.caseSet.requiredCaseSetId)) {
        result.matrixLegal = false;
        result.issues.push_back({CoverageIssueCode::RequiredCaseSetIdMismatch, {},
                                 CoverageIssue::npos,
                                 "requiredCaseSetId 与快照冻结必验工况集身份不符（与计划核对）"});
    }

    // ---- ②逐条目检查（输入序；一次看全全部条目问题）。----
    for (std::size_t i = 0; i < matrix.entries.size(); ++i) {
        const CaseCoverageEntry& entry = matrix.entries[i];
        const std::string caseIdText = entry.caseId.isValid() ? entry.caseId.toCanonical()
                                                              : std::string{};
        // 条目身份保留值（无法对账——先于其余检查）。
        if (!entry.caseId.isValid()) {
            result.matrixLegal = false;
            result.issues.push_back({CoverageIssueCode::EntryCaseIdInvalid, caseIdText, i,
                                     "条目 caseId 为保留值（无法对账）"});
        }
        // 重复记录拒绝（EV-COV-2：同工况出现多条＝覆盖语义歧义——矩阵非法；
        // 与更早条目重复即在当前下标报告，逐个重复处均有 issue）。
        for (std::size_t j = 0; j < i; ++j) {
            if (matrix.entries[j].caseId == entry.caseId) {
                result.matrixLegal = false;
                result.issues.push_back({CoverageIssueCode::DuplicateCaseEntry, caseIdText, i,
                                         "caseId 重复（重复记录拒绝——EV-COV-2）"});
                break;
            }
        }
        // 错误工况引用（EV-COV-2：caseId ⊆ 必验集——引用集外工况即对不
        // 存在的输入声称覆盖）。
        if (entry.caseId.isValid()) {
            bool inRequiredSet = false;
            for (const CaseEntry& required : snapshot.caseSet.entries) {
                if (required.caseId == entry.caseId) {
                    inRequiredSet = true;
                    break;
                }
            }
            if (!inRequiredSet) {
                result.matrixLegal = false;
                result.issues.push_back({CoverageIssueCode::UnknownCaseReference, caseIdText, i,
                                         "caseId 不在快照必验工况集内（错误工况引用→Invalid）"});
            }
        }
        // NotApplicable 必填原因（§6.6 约束行——未解释的不适用不可采信）。
        if (entry.status == CaseExecutionStatus::NotApplicable
            && (!entry.notApplicableReason.has_value() || entry.notApplicableReason->empty())) {
            result.matrixLegal = false;
            result.issues.push_back({CoverageIssueCode::NotApplicableReasonMissing, caseIdText, i,
                                     "NotApplicable 条目必须携带非空原因（§6.6 约束行）"});
        }
        // 追溯面（runId/resultSliceId）有值必须非零（残缺追溯面＝覆盖
        // 无法对账到运行/结果）。
        if (entry.runId.has_value() && !entry.runId->isValid()) {
            result.matrixLegal = false;
            result.issues.push_back({CoverageIssueCode::EntryRunRefInvalid, caseIdText, i,
                                     "runId 有值但为保留值（覆盖追溯面损坏）"});
        }
        if (entry.resultSliceId.has_value() && !entry.resultSliceId->isValid()) {
            result.matrixLegal = false;
            result.issues.push_back({CoverageIssueCode::EntryResultSliceRefInvalid, caseIdText, i,
                                     "resultSliceId 有值但为保留值（覆盖追溯面损坏）"});
        }
    }

    // ---- ③漏验核对（EV-COV-1 面）：快照必验集中每个 enabled∧mandatory
    // 工况必须有 Executed 条目（存在性核对——重复/非法条目已由②判罚，
    // 此处只看"有没有一条 Executed"）。分母＝快照冻结态必验工况集
    // （acceptance 4 保守字面——evidence 只消费冻结标记不解释语义）。----
    bool allMandatoryCovered = true;
    for (const CaseEntry& required : snapshot.caseSet.entries) {
        // enabled∧mandatory 工况才进分母（快照冻结时由需求对象解析的
        // 标记——§4.1.3；evidence 不解释标记，O-14 口径）。
        if (!(required.enabled && required.mandatory)) {
            continue;
        }
        bool executed = false;
        for (const CaseCoverageEntry& entry : matrix.entries) {
            if (entry.caseId == required.caseId && entry.status == CaseExecutionStatus::Executed) {
                executed = true;
                break;
            }
        }
        // 漏验＝覆盖不完备（非矩阵非法——两类判定面分离，EV-COV-1/EV-COV-2
        // 各自观测）；包络合并结果不在此函数视野（DYN-07：它根本不是
        // 矩阵条目的合法替代物——不存在"包络凭据"字段）。
        if (!executed) {
            allMandatoryCovered = false;
            result.issues.push_back(
                {CoverageIssueCode::MissingMandatoryExecution, required.caseId.toCanonical(),
                 CoverageIssue::npos, "enabled∧mandatory 工况缺少 Executed 条目（漏验——EVI-02）"});
        }
    }
    // 覆盖完备 ⇔ 矩阵合法 ∧ 无漏验（§6.6 判据；非法矩阵不得宣称完备——
    // 重复/错误引用未清零前"完备"无意义，保守方向）。
    result.coverageComplete = result.matrixLegal && allMandatoryCovered;
    return result;
}

// =====================================================================
// §6.6 区域采样证据校验（KIN-04 R3/R8）
// =====================================================================

RegionCoverageCheckResult
validateRegionCoverageEvidence(const RegionCoverageEvidence& evidence,
                               const AnalysisSnapshot& snapshot)
{
    RegionCoverageCheckResult result;

    // ---- ①样本集锚（分母来源核对）：身份非零＋可定位到快照冻结采样
    // 计划＋声明分母与冻结计划值一致（§6.6"与快照采样计划一致——分母
    // 来源"；KIN-04"同一冻结样本集复评"的凭据面）。----
    if (!evidence.sampleSetIdentity.isValid()) {
        result.valid = false;
        result.issues.push_back({RegionCoverageIssueCode::SampleSetIdentityInvalid,
                                 RegionCoverageColumn::Position,
                                 "sampleSetIdentity 为保留值（无分母锚）"});
    } else {
        const SamplingPlanRef* plan = nullptr;
        for (const SamplingPlanRef& candidate : snapshot.samplingPlans) {
            if (candidate.sampleSetIdentity == evidence.sampleSetIdentity) {
                plan = &candidate;
                break;
            }
        }
        if (plan == nullptr) {
            result.valid = false;
            result.issues.push_back({RegionCoverageIssueCode::SampleSetNotFoundInSnapshot,
                                     RegionCoverageColumn::Position,
                                     "样本集身份不在快照冻结采样计划中（分母来源错配）"});
        } else {
            // 声明分母与冻结计划逐栏核对（分母被调包＝覆盖率数值失真）。
            if (evidence.plannedPositionSamples != plan->plannedPositionSamples) {
                result.valid = false;
                result.issues.push_back({RegionCoverageIssueCode::PlannedCountMismatch,
                                         RegionCoverageColumn::Position,
                                         "位置栏声明分母与快照冻结采样计划不符"});
            }
            if (evidence.plannedPoseSamples != plan->plannedPoseSamples) {
                result.valid = false;
                result.issues.push_back({RegionCoverageIssueCode::PlannedCountMismatch,
                                         RegionCoverageColumn::Pose,
                                         "姿态栏声明分母与快照冻结采样计划不符"});
            }
        }
    }

    // ---- ②逐栏核对（Position→Pose）：分母完整性（EV-COV-3：三类计数
    // 之和==计划分母，不符→证据 Invalid）＋零样本判定（plannedTotal==0
    // 且计数和为 0——覆盖率不定义；事实面在此暴露，DataInsufficient
    // 裁定归 EV-T06 汇总）。----
    const auto checkColumn = [&result, &evidence](RegionCoverageColumn column,
                                                  const RegionCoverageCounts& counts,
                                                  std::uint64_t plannedTotal) {
        const bool sumMatches = countsSumEquals(counts.reachable, counts.unreachable,
                                                counts.dataInsufficient, plannedTotal);
        if (!sumMatches) {
            result.valid = false;
            result.issues.push_back({RegionCoverageIssueCode::DenominatorIncomplete, column,
                                     "分母完整性不符：reachable+unreachable+dataInsufficient "
                                     "≠ 计划分母（EV-COV-3——不可达保留分母）"});
        } else if (plannedTotal == 0) {
            // 和为 0 且分母为 0＝零样本：覆盖率不定义（不得输出 0%/100%
            // ——EV-COV-3 反例面；非 issue——形状自洽，判定归汇总层）。
            result.zeroSample = true;
        }
        if (counts.dataInsufficient > 0) {
            // 降级必要（§6.6 downgraded 行：dataInsufficient>0 → true——
            // 覆盖率数值仅作参考值、结论整体降级 DataInsufficient）。
            result.downgradedRequired = true;
            if (!evidence.downgraded) {
                result.valid = false;
                result.issues.push_back({RegionCoverageIssueCode::DowngradedFlagInconsistent,
                                         column,
                                         "dataInsufficient>0 但 downgraded==false"
                                         "（§6.6 downgraded 行）"});
            }
        }
    };
    checkColumn(RegionCoverageColumn::Position, evidence.position, evidence.plannedPositionSamples);
    checkColumn(RegionCoverageColumn::Pose, evidence.pose, evidence.plannedPoseSamples);
    return result;
}

}  // namespace sdurws::ird::evidence
