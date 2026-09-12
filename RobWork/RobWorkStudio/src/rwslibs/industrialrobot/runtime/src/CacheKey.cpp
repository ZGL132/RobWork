/**
 * @file   CacheKey.cpp
 * @brief  编译缓存纯判定的实现——键生成器（WC 键复用 §9.1 公式单点＋DWC
 *         链式派生＋分量指纹表）与判定表 judgeCompileCacheCompatibility。
 *
 * 设计依据：units/runtime.md §9.4（判定表逐行——本文件每个函数的分支都
 * 能对回判定表某一行）、§15.4 v0.11（实现层增补登记：指纹表①／DWC 链式
 * 派生②／构造入参选型③）；需求 CON-04（部分/失败产物不作完整命中）、
 * CON-05（键纯内容——本文件无任何 HEAD/时钟/会话状态读取）。
 *
 * 摘要边界（CR-02）：SHA-256 只经 core::ContentDigester（与 Codec/Snapshot
 * 同纪律）；"对什么字节做摘要"由本文件的分量编码注释声明。
 *
 * 两模式编译口径：本文件消费 src/Snapshot.cpp 的
 * snapshotidentity::computeWorkCellCompileIdentity（集成模式编译单元）——
 * 仅集成模式编译（CMake TARGET sdurw_kinematics 条件增列，§15.4 v0.11）。
 */

#include <sdurws/ird/runtime/CacheKey.hpp>

#include <sdurws/ird/core/Digest.hpp>             // ContentDigester（CR-02 单点摘要）
#include <sdurws/ird/runtime/BaseWorldTransform.hpp>  // kBaseWorldRuleVersion（注释引用——
                                                      // 该分量在 computeWorkCellCompileIdentity
                                                      // 内部生效，本文件不经手）
#include <sdurws/ird/runtime/Codec.hpp>           // rtcodec::kVersion*/kNameMapVersion*（编码头常量）
#include <sdurws/ird/runtime/Errors.hpp>          // RuntimeError/InputInvalid（构造期 fail-fast）

#include <cstddef>

namespace sdurws::ird::runtime {

namespace {

// =====================================================================
// 分量编码助手（指纹摘要的确定性字节面；大端定长/长度前缀——与
// RT-Codec 家族同规则。指纹编码只需"同分量同字节"，与 WC 复合键的
// 内部编码不要求逐字节一致——判定只依赖各指纹自身的确定性）。
// =====================================================================

/// 追加大端 u16（指纹编码用）。
void appendU16(std::vector<std::uint8_t>& out, std::uint16_t v)
{
    out.push_back(static_cast<std::uint8_t>(v >> 8));
    out.push_back(static_cast<std::uint8_t>(v & 0xFFu));
}

/// 追加大端 u32（指纹编码用）。
void appendU32(std::vector<std::uint8_t>& out, std::uint32_t v)
{
    out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>(v & 0xFFu));
}

/// 追加长度前缀字符串（u64 大端长度＋UTF-8 字节——指纹编码用）。
void appendString(std::vector<std::uint8_t>& out, const std::string& s)
{
    const std::uint64_t n = static_cast<std::uint64_t>(s.size());
    for (int shift = 56; shift >= 0; shift -= 8) {
        out.push_back(static_cast<std::uint8_t>((n >> shift) & 0xFFu));
    }
    out.insert(out.end(), s.begin(), s.end());
}

/// 追加 bool（1 字节：false=0 / true=1——编码域布尔单字节惯例）。
void appendBool(std::vector<std::uint8_t>& out, bool v)
{
    out.push_back(v ? 1u : 0u);
}

/// 对字节序列做一次 SHA-256（CR-02：只经 core::ContentDigester）。
core::ContentIdentity digestOfBytes(const std::vector<std::uint8_t>& bytes)
{
    core::ContentDigester d;
    d.update(bytes.data(), bytes.size());
    core::ContentIdentity id;
    id.bytes = d.finalize();
    return id;
}

/// reasons 的分量差异词（"<component>-changed"——稳定词汇，见 CacheKey.hpp）。
std::string changedToken(const std::string& component)
{
    return component + "-changed";
}

}  // namespace

// =====================================================================
// codecVersionsFromCodecHeaders——编码头常量的单一权威组装（声明见头）。
// =====================================================================

SnapshotCodecVersions codecVersionsFromCodecHeaders()
{
    // 三元取值全部来自各编码头常量（§9.1 codecVersions 行"消费方不得另写
    // 字面量"）——canonical-model（rtcodec）、name-map（IRDNAME）、
    // snapshot（snapshotcodec）。常量升级＝全体身份/键变化（§4.5）。
    SnapshotCodecVersions v;
    v.canonicalModelMajor = rtcodec::kVersionMajor;
    v.canonicalModelMinor = rtcodec::kVersionMinor;
    v.nameMapMajor = rtcodec::kNameMapVersionMajor;
    v.nameMapMinor = rtcodec::kNameMapVersionMinor;
    v.snapshotMajor = snapshotcodec::kVersionMajor;
    v.snapshotMinor = snapshotcodec::kVersionMinor;
    return v;
}

// =====================================================================
// RuntimeCompileCacheKeyBuilder——键生成器实现。
// =====================================================================

RuntimeCompileCacheKeyBuilder::RuntimeCompileCacheKeyBuilder(
    std::uint32_t compilerContractVersion, std::string compilerVersion,
    std::string robworkBaselineVersion, std::uint32_t nameMapRuleVersion,
    SnapshotCodecVersions codecVersions)
    : m_compilerContractVersion(compilerContractVersion)
    , m_compilerVersion(std::move(compilerVersion))
    , m_robworkBaselineVersion(std::move(robworkBaselineVersion))
    , m_nameMapRuleVersion(nameMapRuleVersion)
    , m_codecVersions(codecVersions)
{
    // 构造期 fail-fast（调用方错误——若延后到 buildKey 将违反 §10.0
    // "无（不抛）"）：契约版本 ≥1（§4.3.1 合法列）、实现版本/基线版本
    // 非空（§9.1 合法列"非空"）。空串键无缓存语义（与
    // computeWorkCellCompileIdentity 的非法域同口径——NFR-COR-03）。
    if (m_compilerContractVersion == 0u) {
        throw RuntimeError(RuntimeErrorCode::InputInvalid,
                           std::string{"RuntimeCompileCacheKeyBuilder: compilerContractVersion 须 ≥1"});
    }
    if (m_compilerVersion.empty() || m_robworkBaselineVersion.empty()) {
        throw RuntimeError(RuntimeErrorCode::InputInvalid,
                           std::string{"RuntimeCompileCacheKeyBuilder: 编译器实现版本/基线版本为空串"});
    }
}

CompileCacheKey RuntimeCompileCacheKeyBuilder::buildKey(const CanonicalModel& model,
                                                        const CompileOptions& options) const noexcept
{
    // 第一步：模型有效性门（noexcept 约束下的"未 finalize"路径）。
    // Failed/Cancelled 不产生模型，防御性输入（身份全零）在此提前返回
    // 零键——该键进入判定必被 judge 的有效性门 Incompatible 拒绝
    // （§9.4 判定表"部分模型/失败产物永不构成命中"行），半成品在判定面
    // 被结构性拦截，不产出可伪装命中的占位键（NFR-COR-03）。
    const core::ContentIdentity& modelIdentity = model.contentIdentity();
    if (!modelIdentity.isValid()) {
        CompileCacheKey unfinalized;
        // workCellKey 全零（保留值）＋DWC nullopt＋空指纹——"未 finalize"
        // 的唯一判定面表示；judge 对其恒 Incompatible(key-invalid)。
        return unfinalized;
    }

    // 第二步：WC 层键——复用 §9.1 公式单点（§15.4 v0.10②：RT-T10 不写
    // 第二套键编码）。该函数的非法域（零身份/空版本串）已在本函数第一步
    // 与构造期全部排除，此处调用不可抛（noexcept 成立的前提，勿删前置）。
    CompileCacheKey key;
    key.workCellKey = snapshotidentity::computeWorkCellCompileIdentity(
        modelIdentity, m_compilerContractVersion, m_compilerVersion,
        m_robworkBaselineVersion, m_nameMapRuleVersion, m_codecVersions, options);

    // 第三步：DWC 层键——链式派生 SHA-256(workCellKey‖requestDynamicWorkCell)
    // （§15.4 v0.11②）。requestDynamicWorkCell（＝capabilityLevel，"是否
    // 要求 DWC"）是 WC 无关子集之外的唯一增量分量；链式派生保证"任一 WC
    // 分量变→DWC 键必变"（RT-CACHE-1 判据的结构性前提）。未请求 DWC
    // （false）→ nullopt：键生成不预判物性——物性缺失跳过是编译事实
    // （§5.2 S7），其"缓存侧 nullopt 落账"归 execution（acceptance 2 边界）。
    if (options.requestDynamicWorkCell) {
        std::vector<std::uint8_t> dwcSeed;
        dwcSeed.reserve(key.workCellKey.bytes.size() + 1);
        dwcSeed.insert(dwcSeed.end(), key.workCellKey.bytes.begin(),
                       key.workCellKey.bytes.end());
        appendBool(dwcSeed, true);
        key.dynamicWorkCellKey = digestOfBytes(dwcSeed);
    }

    // 第四步：分量指纹表（8 条，声明序＝WC 键编码域分量序——§9.1 公式
    // 声明序）。judge 逐项比对产出精确 reasons（RT-CACHE-1"reasons 精确"）。
    // 各指纹编码：同分量同字节即可（判定只依赖指纹自身的确定性）。
    //   注意 model 分量直接取 modelIdentity 本身——它已是摘要，再摘要无义。
    //   base-world-rule 分量取 kBaseWorldRuleVersion 单点（§6——规则版本
    //   常量；与 WC 键内部取值同源，保证指纹与复合键的分量集一致）。
    key.components.reserve(8);

    key.components.push_back({std::string{"model"}, modelIdentity});

    {
        std::vector<std::uint8_t> enc;
        appendU32(enc, m_compilerContractVersion);
        key.components.push_back({std::string{"contract"}, digestOfBytes(enc)});
    }
    {
        std::vector<std::uint8_t> enc;
        appendString(enc, m_compilerVersion);
        key.components.push_back({std::string{"compiler"}, digestOfBytes(enc)});
    }
    {
        std::vector<std::uint8_t> enc;
        appendString(enc, m_robworkBaselineVersion);
        key.components.push_back({std::string{"baseline"}, digestOfBytes(enc)});
    }
    {
        std::vector<std::uint8_t> enc;
        appendU32(enc, m_nameMapRuleVersion);
        key.components.push_back({std::string{"namemap-rule"}, digestOfBytes(enc)});
    }
    {
        std::vector<std::uint8_t> enc;
        appendU32(enc, kBaseWorldRuleVersion);
        key.components.push_back({std::string{"base-world-rule"}, digestOfBytes(enc)});
    }
    {
        std::vector<std::uint8_t> enc;
        appendU16(enc, m_codecVersions.canonicalModelMajor);
        appendU16(enc, m_codecVersions.canonicalModelMinor);
        appendU16(enc, m_codecVersions.nameMapMajor);
        appendU16(enc, m_codecVersions.nameMapMinor);
        appendU16(enc, m_codecVersions.snapshotMajor);
        appendU16(enc, m_codecVersions.snapshotMinor);
        key.components.push_back({std::string{"codec"}, digestOfBytes(enc)});
    }
    {
        // WC 层选项子集（D-04 分层——requestDynamicWorkCell 不在此表：
        // 它只入 DWC 层键，其变化经 optional 缺性在判定层单独成词）。
        std::vector<std::uint8_t> enc;
        appendBool(enc, options.includeCollisionGeometry);
        appendU32(enc, static_cast<std::uint32_t>(options.geometryDetail));
        key.components.push_back({std::string{"wc-options"}, digestOfBytes(enc)});
    }

    return key;
}

// =====================================================================
// judgeCompileCacheCompatibility——判定表唯一执行点（行序见头注释）。
// =====================================================================

CompileCacheCompatibility judgeCompileCacheCompatibility(const CompileCacheKey& requested,
                                                         const CompileCacheKey& cached)
{
    CompileCacheCompatibility out;
    out.verdict = CompileCacheCompatibility::Verdict::Incompatible;

    // ---- 第 1 步：键有效性门（判定表"部分模型/失败产物永不构成命中"
    // 行）——任一侧 workCellKey 全零（未 finalize／buildKey 对无效模型的
    // 产出）或"在场 DWC 键"全零（伪造部分键）→ Incompatible(key-invalid)。
    // 该门先于一切比对：部分/失败产物不存在"作为完整命中"的路径
    // （CON-04；审查表行"是否允许半成品进入缓存或下游"）。
    const bool requestedValid = requested.workCellKey.isValid()
        && (!requested.dynamicWorkCellKey.has_value()
            || requested.dynamicWorkCellKey->isValid());
    const bool cachedValid = cached.workCellKey.isValid()
        && (!cached.dynamicWorkCellKey.has_value() || cached.dynamicWorkCellKey->isValid());
    if (!requestedValid || !cachedValid) {
        out.reasons = {std::string{"key-invalid"}};
        return out;
    }

    // ---- 第 2 步：WC 层分量比对（判定表"任一基础分量不等"行）。
    // 指纹可比前提：两侧指纹表非空且分量名序完全一致（同一 builder 产出
    // 必满足——声明序固定）；否则跳过指纹，交由复合键兜底（保守 reasons）。
    bool fingerprintsComparable = !requested.components.empty()
        && requested.components.size() == cached.components.size();
    if (fingerprintsComparable) {
        for (std::size_t i = 0; i < requested.components.size(); ++i) {
            if (requested.components[i].component != cached.components[i].component) {
                // 分量名序失配＝两侧键非同一 builder 产出（调用方混装）——
                // 不产生误导性差异清单，退回复合键兜底。
                fingerprintsComparable = false;
                break;
            }
        }
    }

    std::vector<std::string> componentDiffs;
    bool componentChanged = false;
    if (fingerprintsComparable) {
        for (std::size_t i = 0; i < requested.components.size(); ++i) {
            if (!(requested.components[i].digest == cached.components[i].digest)) {
                componentDiffs.push_back(changedToken(requested.components[i].component));
            }
        }
        componentChanged = !componentDiffs.empty();
    }

    // 复合键不等或指纹检出差异→Incompatible；reasons 优先取精确清单，
    // 指纹不可比（或指纹一致而复合键不等——构造不一致的防御面）时退回
    // 单条兜底词。不存在"版本接近可凑用"的语义等价复用（§4.3.6 保守方向）。
    if (!(requested.workCellKey == cached.workCellKey) || componentChanged) {
        out.reasons = componentDiffs.empty()
            ? std::vector<std::string>{std::string{"workcell-key-changed"}}
            : std::move(componentDiffs);
        return out;
    }

    // ---- 第 3 步：DWC 层判定（判定表前两行——WC 已相等）。
    // optional 缺性组合的语义（有向：requested 在前）：
    //   两侧都无（请求不要求 DWC 且缓存侧 SkippedNoPhysics）→ FullReuse
    //     （判定表第 1 行"同为 nullopt 且请求不要求 DWC"）；
    //   两侧都有且相等 → FullReuse（"DWC 键等"）；
    //   缓存 nullopt 而请求要求 → WorkCellOnlyReuse（RT-CACHE-2 行：
    //     "cached 物性缺失（DWC 键 nullopt）、requested 要求 DWC"——
    //     显式非 FullReuse，调用方可复用 WC 层产物、须重编 DWC）；
    //   请求不要求而缓存有 → WorkCellOnlyReuse（保守：缓存产物含请求未
    //     要的 DWC，不构成完整命中——宁可重算不错复用）；
    //   两侧都有但不等 → WorkCellOnlyReuse（"DWC 键不等"分支）。
    if (requested.dynamicWorkCellKey.has_value() != cached.dynamicWorkCellKey.has_value()) {
        out.verdict = CompileCacheCompatibility::Verdict::WorkCellOnlyReuse;
        out.reasons = {cached.dynamicWorkCellKey.has_value() ? std::string{"dwc-not-requested"}
                                                             : std::string{"dwc-missing-in-cached"}};
        return out;
    }
    if (requested.dynamicWorkCellKey.has_value()
        && !(requested.dynamicWorkCellKey == cached.dynamicWorkCellKey)) {
        out.verdict = CompileCacheCompatibility::Verdict::WorkCellOnlyReuse;
        out.reasons = {std::string{"dwc-key-mismatch"}};
        return out;
    }

    // ---- FullReuse：全分量相等（策略/种子/线程不在键内——它们经 evidence
    // 切片身份进入评估缓存，CON-06/§9.4 第 1 行；本判定从未读取它们）。
    out.verdict = CompileCacheCompatibility::Verdict::FullReuse;
    out.reasons.clear();
    return out;
}

}  // namespace sdurws::ird::runtime
