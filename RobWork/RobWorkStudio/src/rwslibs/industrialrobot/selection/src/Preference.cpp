/**
 * @file   Preference.cpp
 * @brief  企业偏好过滤实现（selection 单元）——PreferenceFilter::apply
 *         （SEL-07 分轨五步：契约校验→候选聚合→硬可行判定→偏好维度
 *         判定→输出装配；Preference.hpp 头注为语义权威）。
 *
 * 设计依据：
 *   - units/selection.md §10.2（空集语义"用户过滤后为空＝偏好过滤结果，
 *     与硬约束淘汰分开"）、§10.3（user-preference-filtered 边界/偏好组
 *     token）、§10.4（分轨纪律）、§14.0（错误两分法——调用方契约违约
 *     fail-fast；偏好标注缺失是数据事实不是异常）、D-SEL-8
 *   - 需求 SEL-07（企业自定义优选品牌、供应状态和系列限制，不改变硬
 *     能力判定）、ERR-01（偏好原因同样携带阈值来源/建议动作——文本
 *     比较侧以 actualText/requiredText 承载）、NFR-COR-02（确定性）
 *   - 任务契约 tasks/foundation/WP-19-T07.json acceptance 1
 *
 * 确定性（NFR-COR-02）：候选聚合用 std::map（有序键——只影响查找路径，
 * 输出序由末尾 stable_sort 的固定键决定，与容器遍历序无关）；维度执行
 * 序＝品牌→供应状态→系列（固定登记序）；输出序＝(deviceKind, modelId)
 * 升序。同输入恒同输出。
 */

#include <sdurws/ird/selection/Preference.hpp>

#include <algorithm>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>

namespace sdurws::ird::selection {
namespace {

// ---------------------------------------------------------------------
// 契约校验与白名单工具（TU 内私有——与 Screening.cpp/FeasibleSet.cpp
// "各 TU 自持最小实现"的先例一致，不建跨 TU 私有头，NFR-MNT-04）
// ---------------------------------------------------------------------

/// 值 ∈ 白名单（逐字符精确匹配——区分大小写，词表值纪律；无模糊匹配）。
bool inWhitelist(const std::string& value, const std::vector<std::string>& whitelist)
{
    for (const std::string& w : whitelist) {
        if (value == w) {
            return true;
        }
    }
    return false;
}

/// 白名单逗号连接（requiredText 承载——ERR-01"要求值"侧的文本呈现；
/// 白名单已过空串校验，连接序＝配置序——登记确定性问题：企业配置本身
/// 是有序输入，保序呈现）。
std::string joinWhitelist(const std::vector<std::string>& whitelist)
{
    std::string joined;
    for (std::size_t i = 0; i < whitelist.size(); ++i) {
        if (i > 0) {
            joined += ", ";
        }
        joined += whitelist[i];
    }
    return joined;
}

/// 快照主表内按 modelId 查 vendor（品牌商业权威＝目录条目——卡 §2.1）；
/// 未命中返回 nullptr（调用方决定 fail-fast 语义）。
const std::string* findVendor(const CatalogPackageSnapshot& snapshot,
                              const ModelId& modelId, bool isMotor)
{
    if (isMotor) {
        for (const MotorCatalogEntry& m : snapshot.motors) {
            if (m.modelId == modelId) {
                return &m.vendor;
            }
        }
    } else {
        for (const GearboxCatalogEntry& g : snapshot.gearboxes) {
            if (g.modelId == modelId) {
                return &g.vendor;
            }
        }
    }
    return nullptr;
}

/// 候选是否存在快照对应主表（records 引用完整性校验用——型号 ID 在电机
/// 与减速器两张主表中独立唯一，先查电机再查减速器）。
bool existsInSnapshot(const CatalogPackageSnapshot& snapshot, const ModelId& modelId)
{
    return findVendor(snapshot, modelId, true) != nullptr
        || findVendor(snapshot, modelId, false) != nullptr;
}

}  // namespace

// =====================================================================
// PreferenceFilter::apply（五步语义——Preference.hpp 头注）
// =====================================================================

std::vector<PreferenceFilterOutcome> PreferenceFilter::apply(
    const CatalogPackageSnapshot& snapshot,
    const std::vector<FeasibilityRecord>& records,
    const std::vector<CandidateEnterpriseFacts>& enterpriseFacts,
    const EnterprisePreference& preference,
    const IRejectionReasonProvider& reasonProvider) const
{
    // ---- 第 1 步：契约校验（调用方错误 fail-fast——卡 §14.0 两分法）----

    // ①组合级记录拒收：偏好过滤是候选级处理（品牌/供应状态/系列都是
    // 候选属性）；组合级记录经 T05/T06 链路，混入＝装配链违约。
    for (const FeasibilityRecord& rec : records) {
        if (rec.deviceKind == DeviceKind::Combination) {
            throw std::invalid_argument(
                "SEL-PREFERENCE(apply): 组合级记录不得进入偏好过滤（"
                "deviceKind==Combination——偏好过滤是候选级，组合级经 "
                "T05/T06 链路，调用方契约违约）");
        }
        // ②记录引用的候选必须存在于快照对应主表（引用完整性——硬筛选
        // 产物必然来自快照；脱节说明调用方混用了不同目录的记录）。
        if (!existsInSnapshot(snapshot, rec.candidateModelId)) {
            throw std::invalid_argument(
                "SEL-PREFERENCE(apply): 记录引用的候选不在快照内（modelId="
                + rec.candidateModelId + "——引用完整性破坏，调用方契约违约）");
        }
    }
    // ③企业标注的 modelId 唯一性（同一候选两条矛盾标注无法消解——数据
    // 矛盾在边界拒绝；注意"标注引用不在快照的 modelId"不是错误：企业
    // 标注库可为多代目录的超集，该条目忽略——Preference.hpp @note）。
    std::map<ModelId, const CandidateEnterpriseFacts*> factIndex; // modelId 有序——查找路径
    for (const CandidateEnterpriseFacts& f : enterpriseFacts) {
        const auto ins = factIndex.emplace(f.modelId, &f);
        if (!ins.second) {
            throw std::invalid_argument(
                "SEL-PREFERENCE(apply): 企业标注 modelId 重复（modelId="
                + f.modelId + "——同一候选两条矛盾标注，调用方契约违约）");
        }
    }
    // ④白名单空串拒收：空串是"未标注"哨兵语义，混入白名单会使未标注
    // 候选恒命中——语义歧义，配置错误在边界拒绝（调用方错误——企业
    // 配置经本函数消费，配置合法性与"维度是否启用"在此把守）。
    auto rejectEmptyEntry = [](const std::vector<std::string>& list, const char* dim) {
        for (const std::string& v : list) {
            if (v.empty()) {
                throw std::invalid_argument(
                    std::string("SEL-PREFERENCE(apply): 白名单含空串条目（维度=")
                    + dim + "——空串是未标注哨兵语义，不得作为白名单值，"
                            "调用方契约违约）");
            }
        }
    };
    rejectEmptyEntry(preference.preferredVendors, "preferred-vendors");
    rejectEmptyEntry(preference.allowedAvailability, "allowed-availability");
    rejectEmptyEntry(preference.allowedSeries, "allowed-series");

    // ---- 第 2 步：候选聚合（跨轴去重——保留首现序的聚合面）----
    // 聚合键＝(deviceKind, candidateModelId)；首现序进入聚合序（稳定，
    // 不依赖哈希遍历——map 有序键，NFR-COR-02）。
    std::map<std::pair<int, ModelId>, bool> candidateIndex; // 键→是否硬可行
    for (const FeasibilityRecord& rec : records) {
        // pair<int,...> 的 int＝DeviceKind 数值序（Motor=0 < Gearbox=1；
        // Combination 已在契约校验拒收，不进本表）。
        const auto key = std::make_pair(static_cast<int>(rec.deviceKind),
                                        rec.candidateModelId);
        // 候选"硬可行"＝存在任一 verdict==Feasible 的记录（§10.2 分层：
        // 候选在任一轴可行即在候选级可行——偏好维度只需对硬可行候选
        // 判定；置位后不清零——可行事实不可被其他轴的淘汰翻案）。
        auto it = candidateIndex.find(key);
        if (it == candidateIndex.end()) {
            candidateIndex.emplace(key, rec.verdict == VerdictKind::Feasible);
        } else if (rec.verdict == VerdictKind::Feasible) {
            it->second = true;
        }
    }

    // ---- 第 3~4 步：逐候选偏好维度判定（仅硬可行候选；维度全量执行
    //      不短路；维度序＝品牌→供应状态→系列——固定登记序）----

    std::vector<PreferenceFilterOutcome> outcomes;
    outcomes.reserve(candidateIndex.size());
    for (const auto& entry : candidateIndex) {
        const DeviceKind kind = static_cast<DeviceKind>(entry.first.first);
        const ModelId& modelId = entry.first.second;
        const bool hardFeasible = entry.second;

        PreferenceFilterOutcome out;
        out.modelId = modelId;
        out.deviceKind = kind;

        // 品牌维度：vendor 从快照主表读取（商业权威＝目录条目——卡
        // §2.1；企业标注不携品牌，避免双权威）。快照存在性已在第 1 步
        // 校验，此处 findVendor 不可能为空（防御性兜底走"不启用"分支）。
        const std::string* vendor = findVendor(snapshot, modelId,
                                               kind == DeviceKind::Motor);
        // ②标注面：命中白名单恒输出 preferredVendorHit（含硬不可行候选
        // ——呈现层排序建议不区分硬判定；白名单空＝维度不适用＝false）。
        if (vendor != nullptr && !preference.preferredVendors.empty()
            && inWhitelist(*vendor, preference.preferredVendors)) {
            out.preferredVendorHit = true;
        }

        // ①过滤面：偏好维度判定仅对硬可行候选执行（其余候选的 verdict
        // 已由硬筛选定论——偏好不适用，passesPreference 恒 true、零原因
        // ——偏好不改写硬结论，D-SEL-8 分轨纪律）。
        if (hardFeasible) {
            // 查企业标注（未标注＝事实——对应维度按"未命中"保守处理）。
            const auto factIt = factIndex.find(modelId);
            static const CandidateEnterpriseFacts kEmptyFacts{}; // 未标注兜底（全空串）
            const CandidateEnterpriseFacts& facts =
                factIt != factIndex.end() ? *factIt->second : kEmptyFacts;

            // 原因上下文的公共面（每维度差异仅 thresholdSource/actualText/
            // requiredText/suggestion——文本比较统一：数值侧 0、单位空串、
            // 工况/时刻/段空——T04 文本类比较同款分轨，ERR-01 文本承载）。
            ReasonContext ctx;
            ctx.candidateModelId = modelId;
            // axisId 全零＝候选级原因（无单轴定位——T05 组合级同款约定，
            // ReasonContext 头注）；caseId/atTime/segmentId 同理不适用。
            ctx.caseId = std::string{};
            ctx.atTime = 0.0;      // 文本比较无数值时刻，单位 s 的 0 仅占位
            ctx.actual = 0.0;      // 文本比较数值侧无意义（actualText 承载）
            ctx.required = 0.0;    // 同上（requiredText 承载）
            ctx.unit = std::string{}; // 文本比较无单位
            ctx.catalog = snapshot.manifest.identity; // 判定所用快照身份（追溯）
            // inputSliceId/mappingId 保持全零——直调路径无 evidence 切片/
            // 映射批（诚实标记，不伪造；同 T04 直调路径口径）。

            // 维度 A：品牌（vendor ∉ 优选白名单 → 偏好原因）。
            if (!preference.preferredVendors.empty() && vendor != nullptr
                && !inWhitelist(*vendor, preference.preferredVendors)) {
                ctx.thresholdSource = kPrefDimVendor;
                ctx.actualText = *vendor;                                  // 实际品牌
                ctx.requiredText = joinWhitelist(preference.preferredVendors); // 期望白名单
                ctx.suggestion =
                    "候选品牌不在企业优选清单（偏好过滤，非工程结论）——"
                    "如需保留请扩充企业偏好配置的优选品牌白名单";
                out.preferenceReasons.push_back(
                    reasonProvider.make(ReasonToken::UserPreferenceFiltered, ctx));
            }
            // 维度 B：供应状态（标注缺失或 ∉ 白名单 → 偏好原因；未标注
            // 不默认通过——企业漏标呈现层可见，保守不放行）。
            if (!preference.allowedAvailability.empty()
                && !inWhitelist(facts.availability, preference.allowedAvailability)) {
                ctx.thresholdSource = kPrefDimAvailability;
                ctx.actualText = facts.availability.empty()
                                     ? std::string(kPrefAnnotationAbsent)
                                     : facts.availability;                 // 实际供应状态
                ctx.requiredText = joinWhitelist(preference.allowedAvailability);
                ctx.suggestion =
                    "候选供应状态不在企业允许清单（偏好过滤，非工程结论）——"
                    "请核对供应状态标注或调整企业偏好配置";
                out.preferenceReasons.push_back(
                    reasonProvider.make(ReasonToken::UserPreferenceFiltered, ctx));
            }
            // 维度 C：系列（语义与供应状态同构——白名单式，未标注保守）。
            if (!preference.allowedSeries.empty()
                && !inWhitelist(facts.series, preference.allowedSeries)) {
                ctx.thresholdSource = kPrefDimSeries;
                ctx.actualText = facts.series.empty()
                                     ? std::string(kPrefAnnotationAbsent)
                                     : facts.series;                       // 实际系列
                ctx.requiredText = joinWhitelist(preference.allowedSeries);
                ctx.suggestion =
                    "候选系列不在企业允许清单（偏好过滤，非工程结论）——"
                    "请核对系列标注或调整企业偏好配置";
                out.preferenceReasons.push_back(
                    reasonProvider.make(ReasonToken::UserPreferenceFiltered, ctx));
            }
        }

        // ---- 第 5 步（候选级）：通过判定＝启用的偏好维度零未命中。
        out.passesPreference = out.preferenceReasons.empty();
        outcomes.push_back(std::move(out));
    }

    // ---- 第 5 步（输出装配）：(deviceKind, modelId) 升序（NFR-COR-02
    // 确定性——map 遍历已按该键序，此处 stable_sort 为显式声明输出契约；
    // 同键不可达——聚合键即唯一）。
    std::stable_sort(
        outcomes.begin(), outcomes.end(),
        [](const PreferenceFilterOutcome& a, const PreferenceFilterOutcome& b) {
            if (a.deviceKind != b.deviceKind) {
                return static_cast<int>(a.deviceKind) < static_cast<int>(b.deviceKind);
            }
            return a.modelId < b.modelId;
        });
    return outcomes;
}

}  // namespace sdurws::ird::selection
