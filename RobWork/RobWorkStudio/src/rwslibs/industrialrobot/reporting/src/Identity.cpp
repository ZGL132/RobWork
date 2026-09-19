/**
 * @file   Identity.cpp
 * @brief  reporting 身份类型实现——ReportId 解析/格式化/生成（自建 Id128
 *         实现）＋ReportLevel token 表。
 *
 * 设计依据：
 *   - units/reporting.md §4.1（ReportId 规范文本 rpt-<32hex>；句法同 core
 *     Id128 约定）、§4.2（level token level-b/level-c）、§7.3（发布记录）、
 *     §3.1 组成表（Identity.hpp 对应实现随 RPT-T02 落地）
 *   - core.md §4.1 U-1（Id128 句法/保留值/生成纪律——约定来源；实现为
 *     reporting 自建，P-RPT-3——不调用 core 头内 detail 非公共契约层）
 *   - 需求 NFR-COR-02（确定性：同值同串/同串同值）、ARC-04 精神（稳定
 *     身份、精确等值）
 *   - 任务契约 tasks/foundation/RPT-T02.json acceptance 1
 *
 * 实现说明（为什么自建而不调用 core::detail）：core 公共头 Identity.hpp
 * 的 detail 命名空间在头注释中明言"非公共契约（R-2 纪律：私有细节不入
 * 跨单元承诺）"——跨单元只消费公共面。因此本文件按 core §4.1 U-1 的
 * **公开约定**（句法/保留值/字节序/生成器）逐条自建实现；两侧句法一致性
 * 由契约测试钉住（tag 隔离：core 串喂 ReportId 必失败、反之亦然）。先例：
 * core/src/Identity.cpp（CORE-T02，句法与生成机制的对照锚点）。
 */

#include <sdurws/ird/reporting/Identity.hpp>

#include <sdurws/ird/reporting/Errors.hpp>

#include <algorithm>
#include <random>
#include <utility>

namespace sdurws::ird::reporting {

namespace {

/// ReportId 的 tag 字面（§4.1 规范文本前缀——P-RPT-3 自建 tag）。
constexpr std::string_view kTag = "rpt-";
/// 十六进制体的固定长度（Id128 形——32 个小写 hex 字符＝128 位）。
constexpr std::size_t kHexBodyLen = 32;

/**
 * @brief 严格解析 "rpt-<32 个小写十六进制>"（try 轨原语）。
 *
 * 规则与 core §4.1 U-1 逐条同型（自建实现）：
 *   - 总长固定 tag＋32（36）；tag 逐字符匹配（含大小写——tag 本身小写，
 *     "RPT-..." 即拒绝）；
 *   - 十六进制体仅 [0-9a-f]：大写 A-F 拒绝（规范文本唯一小写）、'g' 以后
 *     字母/符号/空白/多字节 UTF-8 序列一律拒绝；
 *   - 无前后缀/空白容忍——"宽容入口"会造成持久化文本二义，宁拒不归一。
 *
 * @param text [in] 待解析文本（任意串）
 * @param out  [out] 成功时的 16 字节输出（每两个 hex 字符合成一字节、高
 *             半字节在前——字节序＝文本序，与 format 对称）
 * @return true＝句法合法且已写出 out；false＝任一规则违约
 *
 * noexcept 纯函数（无分配、无 locale）——调用方据此实现 try* 双轨。
 */
bool tryParseReportId(std::string_view text, std::array<std::uint8_t, 16>* out) noexcept
{
    // 第一步：总长门（36＝tag 4＋hex 32）——长度不符即失败，后续无越界面。
    if (text.size() != kTag.size() + kHexBodyLen) {
        return false;
    }
    // 第二步：tag 逐字符匹配——类型误用（obj-/cid-/rev- 等他类身份串）在此即失败。
    for (std::size_t i = 0; i < kTag.size(); ++i) {
        if (text[i] != kTag[i]) {
            return false;
        }
    }
    // 第三步：十六进制体逐字符校验并装配字节——仅 [0-9a-f]（大写拒绝）。
    for (std::size_t i = 0; i < kHexBodyLen; ++i) {
        const char c = text[kTag.size() + i];
        unsigned nibble;
        if (c >= '0' && c <= '9') {
            nibble = static_cast<unsigned>(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            nibble = static_cast<unsigned>(c - 'a') + 10u;
        } else {
            // 大写/非法字符：规范文本唯一小写——拒绝而非归一化（NFR-COR-02）。
            return false;
        }
        // 字节序＝文本序：偶数下标字符为高半字节、奇数下标为低半字节，
        // 两字符合成一字节——与 formatReportId 的展开顺序严格对称。
        if ((i & 1u) == 0u) {
            (*out)[i / 2] = static_cast<std::uint8_t>(nibble << 4);
        } else {
            (*out)[i / 2] = static_cast<std::uint8_t>((*out)[i / 2] | nibble);
        }
    }
    return true;
}

/**
 * @brief 格式化为 "rpt-<32 个小写十六进制>"（bytes 按字节序直出）。
 *
 * 与 tryParseReportId 使用同一字节序约定——保证 parse(format(x))==x 往返
 * （acceptance 1）。全零字节同样照常格式化：句法合法与"保留值/空"是两个
 * 判定面（isValid），格式化不越权代替合法性判定。
 */
std::string formatReportId(const std::array<std::uint8_t, 16>& bytes)
{
    static constexpr char kHex[] = "0123456789abcdef";   // 规范文本唯一小写
    std::string s;
    s.reserve(kTag.size() + kHexBodyLen);
    s.append(kTag);
    for (const std::uint8_t b : bytes) {
        s.push_back(kHex[b >> 4]);        // 高半字节在前（与解析装配对称）
        s.push_back(kHex[b & 0x0Fu]);     // 低半字节在后
    }
    return s;
}

/**
 * @brief 非零随机生成 16 字节（thread_local 引擎——多线程并发生成安全）。
 *
 * 机制与 core detail::generateId128Bytes 同款（§4.1 U-1 公开约定）：
 * random_device 播种的 mt19937_64、每线程独立引擎（无锁并发）；生成器
 * 非密码学承诺，唯一性要求＝128 位随机空间下碰撞概率可忽略（报告对象
 * 分配频率低——构建成功一次一分配，§4.1）。
 */
std::array<std::uint8_t, 16> generateReportIdBytes()
{
    // random_device 播种一次（thread_local）：进程内每线程独立引擎。
    thread_local std::mt19937_64 engine{std::random_device{}()};
    std::array<std::uint8_t, 16> bytes{};
    // 保留值纪律：全零重取（§4.1 U-1——"generate() 保证非零（零则重取）"；
    // 概率上 2^-128 触发一次重取，循环不构成实际停顿）。
    do {
        const std::uint64_t w1 = engine();
        const std::uint64_t w2 = engine();
        // 大端装配：高字节在低下标（与 hex 文本序一致——parse/format 对称）。
        for (int i = 0; i < 8; ++i) {
            bytes[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(w1 >> (56 - 8 * i));
            bytes[static_cast<std::size_t>(i) + 8] = static_cast<std::uint8_t>(w2 >> (56 - 8 * i));
        }
    } while (std::all_of(bytes.begin(), bytes.end(), [](std::uint8_t b) { return b == 0; }));
    return bytes;
}

/**
 * @brief ReportId 解析失败的统一 detail（稳定子前缀——开发诊断定位面）。
 *
 * what() 全形＝"reporting/data-invalid: identity/parse: ..."——稳定码面
 * （token）之外的定位子域借用 core "core/identity/parse:" 的命名习惯，
 * 便于日志检索时与 core 身份解析失败区分域。
 */
std::string identityParseDetail(std::string_view text)
{
    // 第一步：子域前缀（稳定检索面）。
    std::string detail = "identity/parse: rpt- 期望 <tag><32 小写 hex>，实际: \"";
    // 第二步：截断防刷屏（异常文本进日志——超长输入只保留前 96 字符）。
    const std::size_t kMaxEcho = 96;
    detail.append(text.substr(0, kMaxEcho));
    if (text.size() > kMaxEcho) {
        detail += "…";
    }
    detail += "\"";
    return detail;
}

}  // namespace

ReportId ReportId::generate()
{
    ReportId id;
    id.bytes = generateReportIdBytes();
    return id;
}

ReportId ReportId::fromCanonical(std::string_view text)
{
    ReportId id;
    // try 轨判据与抛出轨迹共用同一解析原语（单点实现——两轨永不分歧）。
    if (!tryParseReportId(text, &id.bytes)) {
        // 解码边界 fail-fast：DataInvalid＝"数据错误（报告字段非法）"——
        // 身份文本是持久化/导出物中的报告字段（§3.5 码面注释）。
        throw ReportError(ReportErrorCode::DataInvalid, identityParseDetail(text));
    }
    return id;
}

std::optional<ReportId> ReportId::tryFromCanonical(std::string_view text) noexcept
{
    ReportId id;
    if (!tryParseReportId(text, &id.bytes)) {
        return std::nullopt;   // 可恢复路径：失败即空可选（§1.4 try* 约定）
    }
    return id;
}

std::string ReportId::toCanonical() const
{
    return formatReportId(bytes);
}

bool ReportId::isValid() const noexcept
{
    // 保留值判定：全零＝空/未设置（§4.1 U-1）——任一非零字节即有效。
    return std::any_of(bytes.begin(), bytes.end(), [](std::uint8_t b) { return b != 0; });
}

std::string_view token(ReportLevel level) noexcept
{
    // token 表（§4.2 字段表原文）：全枚举 2 值、无 default——新增级别值
    // 未登记表项时全枚举告警暴露遗漏（NFR-COR-02 确定性防线）。
    switch (level) {
    case ReportLevel::B: return "level-b";
    case ReportLevel::C: return "level-c";
    }
    // 不可达：全枚举已覆盖。
    return "level-unknown";
}

std::optional<ReportLevel> tryLevelFromToken(std::string_view token) noexcept
{
    // 恰匹配两级规范字面（大小写敏感、无空白容忍——见 Identity.hpp 类型
    // 注释的"规范文本唯一形态"口径）；其余一律 nullopt。
    if (token == "level-b") {
        return ReportLevel::B;
    }
    if (token == "level-c") {
        return ReportLevel::C;
    }
    return std::nullopt;
}

ReportLevel levelFromToken(std::string_view token)
{
    // try 轨判据复用（单点实现）；失败走 DataInvalid——持久化文本中的
    // 级别字段非法（§3.5 "数据错误（报告字段非法）"码面）。
    if (const auto level = tryLevelFromToken(token)) {
        return *level;
    }
    std::string detail = "level-token: 期望 \"level-b\"|\"level-c\"，实际: \"";
    // 截断防刷屏（同 identityParseDetail 口径）。
    constexpr std::size_t kMaxEcho = 96;
    detail.append(token.substr(0, kMaxEcho));
    if (token.size() > kMaxEcho) {
        detail += "…";
    }
    detail += "\"";
    throw ReportError(ReportErrorCode::DataInvalid, std::move(detail));
}

}  // namespace sdurws::ird::reporting
