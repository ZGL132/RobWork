/**
 * @file   CandidatePatch.cpp
 * @brief  候选补丁实现——canonical 序列化（magic "IRDOPTP1"）、CandidatePatchId
 *         与 CandidateId 身份链、构造校验（锁定/边界/值域/阶段锁）与补丁
 *         覆盖视图数据面（units/optimization.md §4.2/§5.4/§5.5/§8.2；
 *         任务 WP-20-T03）。
 *
 * 编码格式与校验规则逐条契约见公共头；本文件注释聚焦逐字节的确定性来源
 * （NFR-COR-02）与每个拒绝分支的业务语义归属（调用方错误 vs 比较型诊断）。
 */

#include <sdurws/ird/optimization/CandidatePatch.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

#include <sdurws/ird/optimization/DiagCodes.hpp>  // OPT-* 码值常量（唯一书写点）

namespace sdurws::ird::optimization {
namespace {

using sdurws::ird::optimization::kOptInputInvalid;
using sdurws::ird::optimization::kOptPatchIllegal;
using sdurws::ird::optimization::kOptStageLocked;
using sdurws::ird::optimization::kOptVarLocked;

/// canonical 魔数与编码版本（卡 §5.5 原文："IRDOPTP1"；升版即新身份——
/// 防跨版本误同；本常量是唯一书写点）。
constexpr char kMagic[8] = {'I', 'R', 'D', 'O', 'P', 'T', 'P', '1'};
constexpr std::uint32_t kCodecVersion = 1;

// ---- 小端定宽写原语（平台无关确定性——MSVC x64 虽为小端，显式字节序
// ---- 写出使格式契约不依赖平台字节序，跨平台重放可校验） ----

void putU16(std::vector<std::uint8_t>& out, std::uint16_t v)
{
    out.push_back(static_cast<std::uint8_t>(v & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFu));
}

void putU32(std::vector<std::uint8_t>& out, std::uint32_t v)
{
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFFu));
    }
}

void putF64Le(std::vector<std::uint8_t>& out, double v)
{
    // IEEE 754 位模式小端写出（memcpy 位保持——不经过整数转换的未定义行为；
    // 调用方保证 v 非 -0.0：构造规范化已把 -0.0 折叠为 +0.0，位模式唯一）。
    std::uint64_t bits = 0;
    static_assert(sizeof(bits) == sizeof(v), "double 必须 64 位（IEEE 754）");
    std::memcpy(&bits, &v, sizeof(bits));
    for (int i = 0; i < 8; ++i) {
        out.push_back(static_cast<std::uint8_t>((bits >> (8 * i)) & 0xFFu));
    }
}

void putString(std::vector<std::uint8_t>& out, std::string_view s)
{
    // u16 长度前缀＋UTF-8 字节（不含终止符）——定界编码（卡 §5.5"定界"）。
    putU16(out, static_cast<std::uint16_t>(s.size()));
    out.insert(out.end(), s.begin(), s.end());
}

/// 按 bindingId 字典序返回排序副本（canonical 与 diff 共用的确定性前提）。
std::vector<PatchItem> sortedItems(const std::vector<PatchItem>& items)
{
    std::vector<PatchItem> copy = items;
    std::sort(copy.begin(), copy.end(),
              [](const PatchItem& x, const PatchItem& y) { return x.bindingId < y.bindingId; });
    return copy;
}

/// 单补丁项的值形态判定（绑定类别→编码 tag；词表 Continuous 条目的 Quantized
/// 绑定同走 Scalar 通道——量化值已是对齐后网格标量）。
PatchValueTag valueTagOf(const VariableBinding& binding)
{
    switch (binding.kind) {
    case VariableKind::Enumeration:    return PatchValueTag::EnumIndex;
    case VariableKind::DiscreteDevice: return PatchValueTag::DiscreteRef;
    case VariableKind::Continuous:     [[fallthrough]];
    case VariableKind::Quantized:      [[fallthrough]];
    default:                           return PatchValueTag::Scalar;
    }
}

/// 补丁项在绑定集中查找（线性——研究内绑定数为十量级，无需索引）。
const VariableBinding* findBinding(const std::vector<VariableBinding>& bindings,
                                   const BindingToken& bindingId)
{
    const auto it = std::find_if(bindings.begin(), bindings.end(),
                                 [&](const VariableBinding& b) {
                                     return b.bindingId == bindingId;
                                 });
    return it == bindings.end() ? nullptr : &*it;
}

}  // namespace

// =====================================================================
// 确定性序列化与身份（卡 §5.5/§4.2）
// =====================================================================

std::vector<std::uint8_t> canonicalize(const CandidatePatch& patch)
{
    // 排序副本（构造边界强制的序列化兜底——相同语义补丁在任意输入序下
    // 必得相同字节；重复 bindingId 在此暴露：重复语义无法定义确定性序）。
    const auto items = sortedItems(patch.items);
    for (std::size_t k = 1; k < items.size(); ++k) {
        if (items[k].bindingId == items[k - 1].bindingId) {
            throw OptimizationError(
                kOptPatchIllegal,
                "canonicalize: 补丁含重复 bindingId（无法定义确定性序列化）: "
                    + items[k].bindingId);
        }
    }
    std::vector<std::uint8_t> out;
    out.reserve(16 + items.size() * 24);
    out.insert(out.end(), std::begin(kMagic), std::end(kMagic));
    putU32(out, kCodecVersion);
    putU32(out, static_cast<std::uint32_t>(items.size()));
    for (const auto& item : items) {
        putString(out, item.bindingId);
        // 值形态判定（补丁项自持三成员，无绑定上下文）：离散引用非空→
        // Discrete 通道；枚举下标非零→Enum 通道；其余→Scalar 通道。
        // 说明：规范化构造（makeCandidatePatch）保证非当前形态成员为默认
        // 值——枚举下标 0 的项与标量 0.0 在 PatchItem 表达层本就同形
        // （值语义由绑定解释，bindingId 前缀保证无跨项歧义），编码按
        // Scalar 通道（标量值恒有定义）是确定且无损的。
        if (!item.discreteRef.empty()) {
            out.push_back(static_cast<std::uint8_t>(PatchValueTag::DiscreteRef));
            putString(out, item.discreteRef);
        } else if (item.enumIndex != 0) {
            out.push_back(static_cast<std::uint8_t>(PatchValueTag::EnumIndex));
            putU32(out, item.enumIndex);
        } else {
            out.push_back(static_cast<std::uint8_t>(PatchValueTag::Scalar));
            putF64Le(out, item.scalarValue);
        }
    }
    return out;
}

core::ContentIdentity patchIdentity(const CandidatePatch& patch)
{
    // CandidatePatchId ＝ SHA-256(canonical 字节)——内容寻址（CON-05；
    // ContentDigester 每次新建：实例非线程安全且 finalize 后不可复用）。
    const auto bytes = canonicalize(patch);
    core::ContentDigester digester;
    digester.update(bytes.data(), bytes.size());
    core::ContentIdentity id;
    id.bytes = digester.finalize();
    return id;
}

std::string CandidateId::toCanonical() const
{
    // "cnd-<64 小写 hex>"——本域自有格式（DOPT-3；不进 core Id128 tag 冻结集）。
    static const char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(4 + 64);
    out += "cnd-";
    for (std::uint8_t b : bytes) {
        out.push_back(kHex[b >> 4]);
        out.push_back(kHex[b & 0x0F]);
    }
    return out;
}

bool CandidateId::isValid() const noexcept
{
    return std::any_of(bytes.begin(), bytes.end(), [](std::uint8_t b) { return b != 0; });
}

CandidateId candidateIdOf(const core::ObjectId& baselineRoot,
                          const core::ContentVersion& baselineCv,
                          const CandidatePatch& patch)
{
    // 基线身份合法前置：全零＝保留值，不可作基线（调用方契约违约 fail-fast
    // ——用它算出的身份会与"未初始化"歧义）。
    const bool rootZero = std::all_of(baselineRoot.bytes.begin(), baselineRoot.bytes.end(),
                                      [](std::uint8_t b) { return b == 0; });
    if (rootZero || !baselineCv.isValid()) {
        throw OptimizationError(kOptInputInvalid,
                                "candidateIdOf: 基线根对象身份/内容版本为保留值全零"
                                "（须为有效 ObjectId/ContentVersion）");
    }
    // 卡 §4.2 公式原文：SHA-256(baselineRootOid ‖ baselineRootCv ‖
    // CandidatePatchId)——80 字节定长裸拼接（16+32+32，无分隔符歧义）。
    const core::ContentIdentity patchId = patchIdentity(patch);
    core::ContentDigester digester;
    digester.update(baselineRoot.bytes.data(), baselineRoot.bytes.size());
    digester.update(baselineCv.bytes.data(), baselineCv.bytes.size());
    digester.update(patchId.bytes.data(), patchId.bytes.size());
    CandidateId cid;
    cid.bytes = digester.finalize();
    return cid;
}

// =====================================================================
// 补丁构造与校验（卡 §5.5 拒绝形态＋§5.4 锁定＋§5.7 阶段锁）
// =====================================================================

bool PatchValidationReport::hasCode(std::string_view code) const noexcept
{
    return std::any_of(issues.begin(), issues.end(),
                       [&](const PatchValidationIssue& i) { return i.code == code; });
}

PatchValidationReport validatePatchItems(const std::vector<VariableBinding>& bindings,
                                         OptimizationStage stage,
                                         const std::vector<PatchItem>& items)
{
    PatchValidationReport report;
    std::vector<const BindingToken*> seen;  // 重复检测（研究规模小，线性足够）
    for (const auto& item : items) {
        // 码参取 string_view——DiagCodes 常量为 string_view，MSVC 下到
        // string 无隐式转换，显式在此单点转换。
        const auto addIssue = [&](std::string_view code, std::string subject,
                                  std::string detail) {
            PatchValidationIssue i;
            i.code = std::string(code);
            i.bindingId = item.bindingId;
            i.subject = std::move(subject);
            i.detail = std::move(detail);
            report.issues.push_back(std::move(i));
        };
        // ① 空 bindingId——补丁项必须可定位到绑定。
        if (item.bindingId.empty()) {
            addIssue(kOptPatchIllegal, {}, "补丁项 bindingId 为空");
            continue;
        }
        // ② 重复绑定（同一绑定在补丁中出现两次——语义冲突，先于值检查）。
        if (std::any_of(seen.begin(), seen.end(),
                        [&](const BindingToken* t) { return *t == item.bindingId; })) {
            addIssue(kOptPatchIllegal, {}, "补丁含重复 bindingId（同一绑定只允许一个取值）");
            continue;
        }
        seen.push_back(&item.bindingId);
        // ③ 绑定存在性——补丁只覆盖研究定义已登记的绑定（未知＝非法）。
        const VariableBinding* binding = findBinding(bindings, item.bindingId);
        if (binding == nullptr) {
            addIssue(kOptPatchIllegal, {}, "未知绑定（未在研究定义登记——补丁只覆盖已激活绑定）");
            continue;
        }
        // ⑦ 锁定/未授权（§5.4——生成阶段拒绝＋OPT-VAR-LOCKED，比较型定位
        //    bindingId＋对象；authorized=false 未联动 locked 的手工矛盾状态
        //    同样按锁定拒绝——同一语义的防御面，OPT-VER-103 观测点）。
        if (binding->locked || !binding->authorized) {
            addIssue(kOptVarLocked, binding->diagSubject,
                     "补丁触及锁定/未授权变量（改型未授权参数默认锁定——授权后"
                     "方可进入补丁，§5.4）");
            continue;
        }
        // ②' 词表匹配＋③ 阶段锁（§5.7——StageB 引用未启用条目〔如电机型号〕
        //    即阶段锁拒绝，不降级不丢弃；boundings 已登记但词表未启用＝研究
        //    定义装配越界，仍按阶段锁拒绝）。
        const VariableDefinition* d = matchDefinition(item.bindingId, stage);
        const bool stageEnabled = d != nullptr
            && (stage == OptimizationStage::StageB ? d->enabledInStageB : d->enabledInStageD);
        if (d == nullptr) {
            addIssue(kOptPatchIllegal, binding->diagSubject,
                     "未知绑定（不在本阶段词表——token 形态须匹配 §5.3 词表）");
            continue;
        }
        if (!stageEnabled) {
            addIssue(kOptStageLocked, binding->diagSubject,
                     "阶段锁：当前阶段不支持该变量（§5.7——不降级、不丢弃）");
            continue;
        }
        // ⑤ 类别一致性（与绑定校验同口径——词表 Continuous 允许 Quantized）。
        const bool kindOk = (d->kind == VariableKind::Continuous
                             && (binding->kind == VariableKind::Continuous
                                 || binding->kind == VariableKind::Quantized))
            || (d->kind == binding->kind);
        if (!kindOk) {
            addIssue(kOptPatchIllegal, binding->diagSubject,
                     "绑定值类别与词表定义不符（词表 " + std::string(toToken(d->kind))
                         + "，绑定 " + std::string(toToken(binding->kind)) + "）");
            continue;
        }
        // ⑧⑨⑩ 值检查（按值形态分派；全部不静默截断——NFR-COR-03）。
        const PatchValueTag tag = valueTagOf(*binding);
        if (tag == PatchValueTag::Scalar) {
            const double v = item.scalarValue;
            if (!std::isfinite(v)) {
                addIssue(kOptPatchIllegal, binding->diagSubject,
                         "补丁值非有限（NaN/±Inf 拒绝——I-OPT-9 不静默截断）");
            } else if (d->valueMustBePositive && !(v > 0.0)) {
                // 词表值域硬约束（I-MDL-11 传动比 >0 且有限——非发明阈值）。
                addIssue(kOptPatchIllegal, binding->diagSubject,
                         "补丁值违反词表值域（须 >0——I-MDL-11）");
            } else if (v < binding->lowerBound || v > binding->upperBound) {
                addIssue(kOptPatchIllegal, binding->diagSubject,
                         "补丁值越界（[lower,upper] 闭区间外——不截断，NFR-COR-03）");
            }
        } else if (tag == PatchValueTag::EnumIndex) {
            if (item.enumIndex >= binding->enumValues.size()) {
                addIssue(kOptPatchIllegal, binding->diagSubject,
                         "枚举下标越界（enumValues 封闭值域外）");
            }
        } else {  // DiscreteRef
            if (item.discreteRef.empty()) {
                addIssue(kOptPatchIllegal, binding->diagSubject,
                         "离散器件引用为空（须为目录 modelId/组合规范引用——R2）");
            }
        }
    }
    return report;
}

CandidatePatch makeCandidatePatch(const std::vector<VariableBinding>& bindings,
                                  OptimizationStage stage,
                                  const std::vector<PatchItem>& items,
                                  std::string label)
{
    const PatchValidationReport report = validatePatchItems(bindings, stage, items);
    if (!report.ok()) {
        // fail-fast 轨：码取首个问题的稳定码（一个补丁一个主拒绝原因——
        // 完整问题清单经诊断轨 validatePatchItems 获取）。
        throw OptimizationError(report.issues.front().code,
                                "makeCandidatePatch: " + report.issues.front().bindingId
                                    + " " + report.issues.front().detail);
    }
    // 构造规范化（"相同语义补丁 ⇒ 相同字节"的构造前提，卡 §5.5 ②）：
    //   量化项→对齐后网格值；-0.0→+0.0；非当前形态成员清零/清空；
    //   按 bindingId 字典序排序。
    CandidatePatch out;
    out.label = std::move(label);
    out.items.reserve(items.size());
    for (const auto& item : items) {
        PatchItem n = item;
        const VariableBinding* binding = findBinding(bindings, n.bindingId);
        const PatchValueTag tag = valueTagOf(*binding);
        if (tag == PatchValueTag::Scalar) {
            n.enumIndex = 0;
            n.discreteRef.clear();
            if (binding->kind == VariableKind::Quantized) {
                n.scalarValue = quantizeToStepHalfEven(n.scalarValue, binding->step);
            }
            if (n.scalarValue == 0.0) {
                n.scalarValue = 0.0;  // -0.0 折叠为 +0.0（IEEE 位模式唯一）
            }
        } else if (tag == PatchValueTag::EnumIndex) {
            n.scalarValue = 0.0;
            n.discreteRef.clear();
        } else {
            n.scalarValue = 0.0;
            n.enumIndex = 0;
        }
        out.items.push_back(std::move(n));
    }
    std::sort(out.items.begin(), out.items.end(),
              [](const PatchItem& x, const PatchItem& y) { return x.bindingId < y.bindingId; });
    return out;
}

// =====================================================================
// 量化对齐（卡 §8.2——确定性 round-half-even）
// =====================================================================

double quantizeToStepHalfEven(double value, double step)
{
    if (!std::isfinite(step) || !(step > 0.0) || !std::isfinite(value)) {
        throw OptimizationError(kOptInputInvalid,
                                "quantizeToStepHalfEven: 步长必须 >0 且有限、"
                                "值必须有限（绑定校验已拒，此处为最后防线）");
    }
    // 第一步：算格商 k＝value/step（IEEE 754 除法——同输入同商，确定性）。
    const double k = value / step;
    // 第二步：取下格 floor(k) 与分数部分 frac（半格判定值 0.5 为 2 的幂，
    // 比较精确无表示误差）。
    const double floorK = std::floor(k);
    const double frac = k - floorK;
    // 第三步：常规最近格；恰在半格（frac==0.5）舍入到偶数格
    // （round-half-even——IEEE 754-2008 默认语义；std::fmod 对负下格返回
    // 带符号余数，偶格判定用 fmod==0 涵盖 ±0.0）。
    if (frac > 0.5) {
        return (floorK + 1.0) * step;  // 上格
    }
    if (frac < 0.5) {
        return floorK * step;          // 下格
    }
    const double even = std::fmod(floorK, 2.0) == 0.0;
    return (even ? floorK : floorK + 1.0) * step;  // 半格→偶格
}

// =====================================================================
// 补丁覆盖视图（P-OPT-2 数据面）
// =====================================================================

CandidateDesignOverlay buildCandidateDesignOverlay(
    const std::vector<VariableBinding>& bindings, OptimizationStage stage,
    const CandidatePatch& patch)
{
    // 视图只对合法补丁定义（拒绝面与 makeCandidatePatch 一致——fail-fast）。
    const PatchValidationReport report = validatePatchItems(bindings, stage, patch.items);
    if (!report.ok()) {
        throw OptimizationError(report.issues.front().code,
                                "buildCandidateDesignOverlay: "
                                    + report.issues.front().bindingId + " "
                                    + report.issues.front().detail);
    }
    CandidateDesignOverlay overlay;
    overlay.patchId = patchIdentity(patch);
    overlay.entries.reserve(patch.items.size());
    for (const auto& item : patch.items) {
        const VariableBinding* binding = findBinding(bindings, item.bindingId);
        // 校验已保证 binding 非空（③ 存在性检查）——此处断言级防御。
        if (binding == nullptr) {
            throw OptimizationError(kOptPatchIllegal,
                                    "buildCandidateDesignOverlay: 内部一致性破坏——"
                                    "绑定缺失 " + item.bindingId);
        }
        const VariableDefinition* d = matchDefinition(item.bindingId, stage);
        CandidateDesignOverlayEntry e;
        e.bindingId = item.bindingId;
        // 权威字段定位：绑定的实例化值优先（含具体索引），退回词表形态。
        e.authorityFieldPath = !binding->authorityFieldPath.empty()
            ? binding->authorityFieldPath
            : (d != nullptr ? d->authorityFieldPath : std::string{});
        e.diagSubject = binding->diagSubject;
        e.valueTag = valueTagOf(*binding);
        // 单位符号：绑定有效单位的注册符号原样（SI 真值不换算——I-OPT-8；
        // 显示换算由 ui 消费 core::convert）。
        if (binding->unit.isValid()) {
            e.unitSymbol = std::string(binding->unit.symbol());
        }
        switch (e.valueTag) {
        case PatchValueTag::Scalar:
            e.scalarValue = item.scalarValue;  // 传动比条目＝c 口径值（V12-02）
            break;
        case PatchValueTag::EnumIndex:
            e.enumIndex = item.enumIndex;
            // 枚举键文本（物化可读——材料条目即 MaterialRef 键）。
            if (item.enumIndex < binding->enumValues.size()) {
                e.enumValue = binding->enumValues[item.enumIndex];
            }
            break;
        case PatchValueTag::DiscreteRef:
            e.discreteRef = item.discreteRef;
            break;
        }
        overlay.entries.push_back(std::move(e));
    }
    // entries 按 bindingId 升序（补丁规范化序的直接延续——视图稳定性）。
    std::sort(overlay.entries.begin(), overlay.entries.end(),
              [](const CandidateDesignOverlayEntry& x, const CandidateDesignOverlayEntry& y) {
                  return x.bindingId < y.bindingId;
              });
    return overlay;
}

}  // namespace sdurws::ird::optimization
