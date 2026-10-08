/**
 * @file   Run.cpp
 * @brief  运行结果聚合实现——OptimizationRunId 生成/解析、归档阶段 token、
 *         聚合工厂 assembleRunResult（任务 WP-20-T07）。
 *
 * 设计依据：include/sdurws/ird/optimization/Run.hpp 文件头（契约面逐条
 *   注明出处）；本实现翻译单元只承载该头的机械落位，零新增语义。
 *
 * 确定性口径（NFR-COR-02）：generate() 为唯一随机面（运行启动一次性）；
 *   其余全部纯函数——同输入同输出。
 * 错误语义（AGENTS §2.5）：调用方契约违约（保留值身份/编排终态越集/
 *   规范文本解析失败）＝ fail-fast 抛 OptimizationError(kOptInputInvalid)；
 *   本翻译单元零环境错误路径（无 IO/无共享状态）。
 */

#include <sdurws/ird/optimization/Run.hpp>

#include <limits>
#include <random>
#include <utility>

#include <sdurws/ird/optimization/DiagCodes.hpp>  // kOptInputInvalid——稳定码
                                                  //  唯一书写点（禁字符串拼码）

namespace sdurws::ird::optimization {

// =====================================================================
// OptimizationRunId——生成/解析/格式化（本域自持，不动 core detail 层）
// =====================================================================

OptimizationRunId OptimizationRunId::generate()
{
    // 生成引擎：thread_local mt19937_64 双字拼接——core Id128 generate 的
    // 同款形态（Identity.hpp"thread_local mt19937_64"）。线程安全：引擎
    // 为线程私有，多线程并发生成无共享状态。
    // 随机性用途说明：运行身份是"一次运行"的唯一命名（非内容寻址、非
    // 安全用途）——mt19937_64 的统计质量足够；零值重取实现保留值纪律
    // （全零＝空，isValid() 恒 false——core §4.1 U-1 同源）。
    static thread_local std::mt19937_64 engine{std::random_device{}()};

    OptimizationRunId id;
    for (unsigned attempt = 0; attempt < 64; ++attempt) {
        // 两次 64 位采样拼 128 位（高半字/低半字独立——字节序＝规范文本
        // 序，与 toCanonical/fromCanonical 的字节直出口径一致）。
        const std::uint64_t hi = engine();
        const std::uint64_t lo = engine();
        for (std::size_t i = 0; i < 8; ++i) {
            id.bytes[i] = static_cast<std::uint8_t>((hi >> (8 * i)) & 0xFFU);
            id.bytes[8 + i] = static_cast<std::uint8_t>((lo >> (8 * i)) & 0xFFU);
        }
        bool allZero = true;
        for (std::uint8_t b : id.bytes) {
            if (b != 0) {
                allZero = false;
                break;
            }
        }
        if (!allZero) {
            return id;  // 非零即接受——64 次重取全部为零的概率为 2^-1024 量级
        }
    }
    // 不可达路径（见上——概率论证）；防御面：抛域异常而非返回保留值
    // （保留值外漏＝身份面污染，宁可 fail-fast）。
    throw OptimizationError(kOptInputInvalid,
                            "optimization/run: OptimizationRunId 生成失败"
                            "（64 次重取均为全零保留值——随机源异常）");
}

std::string OptimizationRunId::toCanonical() const
{
    // "opt-run-" 前缀＋16 字节直出小写十六进制（32 字符）——字节序与
    // generate/parse 一致（parse(format(x))==x 往返契约）。
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(32 + 8);
    out += "opt-run-";
    for (std::uint8_t b : bytes) {
        out.push_back(kHex[(b >> 4) & 0x0FU]);
        out.push_back(kHex[b & 0x0FU]);
    }
    return out;
}

namespace {

/// 十六进制字符→半字节值；非法字符返回 false（tryParse 的字符集门）。
bool hexNibble(char c, std::uint8_t* out) noexcept
{
    if (c >= '0' && c <= '9') {
        *out = static_cast<std::uint8_t>(c - '0');
        return true;
    }
    if (c >= 'a' && c <= 'f') {
        *out = static_cast<std::uint8_t>(c - 'a' + 10);
        return true;
    }
    return false;  // 大写与符号一概拒绝——严格解析（core tryParseId128 同款）
}

/// 共享解析核（try 轨；fromCanonical 在 false 时抛域异常）。
/// 校验序：前缀逐字符 → 总长度恰 8+32 → hex 字符集 → 写入字节。
bool tryParseRunId(std::string_view text, OptimizationRunId* out) noexcept
{
    static constexpr std::string_view kPrefix = "opt-run-";
    if (text.size() != kPrefix.size() + 32) {
        return false;  // 长度门：前缀 8 字节＋32 hex 字符
    }
    for (std::size_t i = 0; i < kPrefix.size(); ++i) {
        if (text[i] != kPrefix[i]) {
            return false;  // 前缀门：逐字符匹配（"run-…"等异前缀拒绝）
        }
    }
    for (std::size_t i = 0; i < 16; ++i) {
        std::uint8_t hi = 0;
        std::uint8_t lo = 0;
        if (!hexNibble(text[kPrefix.size() + 2 * i], &hi)
            || !hexNibble(text[kPrefix.size() + 2 * i + 1], &lo)) {
            return false;  // 字符集门：仅 [0-9a-f]（大写拒绝）
        }
        out->bytes[i] = static_cast<std::uint8_t>((hi << 4) | lo);
    }
    return true;
}

}  // namespace

std::optional<OptimizationRunId> OptimizationRunId::tryFromCanonical(
    std::string_view text) noexcept
{
    OptimizationRunId id;
    if (tryParseRunId(text, &id)) {
        return id;
    }
    return std::nullopt;  // try 轨：解析失败不抛（io/导出容错收集场景）
}

OptimizationRunId OptimizationRunId::fromCanonical(std::string_view text)
{
    OptimizationRunId id;
    if (!tryParseRunId(text, &id)) {
        // 抛出轨迹：调用方契约违约 fail-fast——消息含输入文本定位
        // （比较型定位纪律 ERR-01；文本可能来自外部，截断防日志膨胀）。
        std::string shown(text.substr(0, 48));
        throw OptimizationError(kOptInputInvalid,
                                "optimization/run: OptimizationRunId 规范文本"
                                "解析失败（期望 opt-run-<32 小写 hex>，实际："
                                + shown + "）");
    }
    return id;
}

bool OptimizationRunId::isValid() const noexcept
{
    for (std::uint8_t b : bytes) {
        if (b != 0) {
            return true;  // 任一非零字节即有效——保留值＝全零
        }
    }
    return false;
}

// =====================================================================
// 归档阶段 token（封闭词表——Types.hpp 各 toToken 同款 switch 形态）
// =====================================================================

std::string_view toToken(ArchivePhase p) noexcept
{
    switch (p) {
    case ArchivePhase::Pending:
        return "pending";
    case ArchivePhase::Archived:
        return "archived";
    }
    // 封闭枚举不可达——到达即编译器警告面（-Wswitch 已覆盖）；此 return
    // 仅为满足 noexcept 签名（MSVC 不识别全部路径返回时的兜底）。
    return "pending";
}

// =====================================================================
// 聚合工厂（执行序见头注 assembleRunResult 契约——五步固定序）
// =====================================================================

bool isSearchEmpty(const OptimizationRunResult& result) noexcept
{
    // 搜索空＝正常完成但可行集为空（OPT-VER-120：warning 语义、非任务
    // 不可行）。可行集取 pareto.feasibleIds（T05 显式面——去重后全体）。
    return result.runPhase == RunPhase::Completed && result.pareto.feasibleIds.empty();
}

OptimizationRunResult assembleRunResult(
    const OptimizationRunId& runId,
    const core::ProjectId& project,
    const core::BranchId& branch,
    const core::RevisionId& revision,
    const core::ContentIdentity& snapshotId,
    const core::ObjectId& baselineRoot,
    const core::ContentVersion& baselineCv,
    const OptimizationConfiguration& config,
    const TwoStageRunResult& orchestrated,
    std::vector<RunTaskRecord> tasks,
    ArchivePhase archivePhase)
{
    // ---- 第 1 步：身份面校验（调用方契约违约 fail-fast——保留值不可作
    //      运行/输入身份；core §4.1 保留值纪律的聚合侧强制）。
    if (!runId.isValid()) {
        throw OptimizationError(kOptInputInvalid,
                                "optimization/run: runId 为全零保留值——"
                                "运行身份须由 OptimizationRunId::generate() 生成");
    }
    if (!project.isValid() || !branch.isValid() || !revision.isValid()) {
        throw OptimizationError(kOptInputInvalid,
                                "optimization/run: 输入身份面存在保留值"
                                "（project/branch/revision 须全部 isValid——"
                                "运行绑定原修订，§4.2 输入身份行）");
    }
    if (!snapshotId.isValid() || !baselineRoot.isValid()
        || !baselineCv.isValid()) {
        throw OptimizationError(kOptInputInvalid,
                                "optimization/run: 快照身份或基线锚为保留值"
                                "（snapshotId/baselineRoot/baselineCv 须非全零"
                                "——候选身份公式输入，§4.2）");
    }

    // ---- 第 2 步：编排终态校验。T06 编排只产出 Completed/Canceled 两终态
    //      （Failed/Interrupted 以异常传播不落聚合——Types.hpp RunPhase 词
    //      表注）；越集值＝调用方把非编排产物喂给了本工厂（如 Draft 中间
    //      态），属契约违约。
    if (orchestrated.runPhase != RunPhase::Completed
        && orchestrated.runPhase != RunPhase::Canceled) {
        throw OptimizationError(
            kOptInputInvalid,
            std::string("optimization/run: 编排终态越集（实际 ")
            + std::string(toToken(orchestrated.runPhase))
            + "；聚合只接受 completed/canceled——failed/interrupted 以异常"
              "传播承载，不落归档聚合）");
    }

    // ---- 第 3 步：组装投影（零判定计算——结论面字段逐项透传，T06 产出
    //      即权威；本工厂只加身份/资格/归档三轴）。
    OptimizationRunResult result;
    result.runId = runId;
    result.project = project;
    result.branch = branch;
    result.revision = revision;
    result.snapshotId = snapshotId;
    result.baselineRoot = baselineRoot;
    result.baselineCv = baselineCv;
    result.config = config;
    result.runPhase = orchestrated.runPhase;
    result.archivePhase = archivePhase;

    // ---- 第 4 步：资格位推导（TASK-02/§10.6：仅 Completed 具正式导出
    //      资格——取消结果不伪装完整；Interrupted 恒 false 已由第 2 步
    //      挡在聚合之外，此处布尔直推）。
    result.allowFormalExport = (orchestrated.runPhase == RunPhase::Completed);

    // ---- 第 5 步：任务映射与候选合并（Quick 批在前、Verified 批在后——
    //      保持编排产出序；screeningOnly 标记在记录内，呈现面消费）。
    result.tasks = std::move(tasks);
    result.candidates = orchestrated.quickRecords;
    result.candidates.insert(result.candidates.end(),
                             orchestrated.verifiedRecords.begin(),
                             orchestrated.verifiedRecords.end());
    result.pareto = orchestrated.pareto;
    result.audit = orchestrated.audit;

    return result;
}

}  // namespace sdurws::ird::optimization
