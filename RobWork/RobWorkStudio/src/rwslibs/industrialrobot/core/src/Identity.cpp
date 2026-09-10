/**
 * @file   Identity.cpp
 * @brief  身份基础类型的实现——解析/格式化/生成/FNV-1a 128 哈希（六类型共享单点实现）。
 *
 * 设计依据：
 *   - units/core.md §4.1 U-1（严格解析、保留值、FNV-1a 128）、§5.1（签名与错误）
 *   - 任务契约 tasks/foundation/CORE-T02.json（≙WP-03-T02，UT-ID-T 载体）
 *
 * 实现说明：头文件以宏展开六个同形 struct（声明），本文件以第二个宏展开
 * 五个成员函数的核外定义——两处宏参数 (类型, tag) 必须成对一致，错配会在
 * UT-ID-T 的"tag 不符拒绝"用例暴露（rev- 串喂错类型即失败）。
 */

#include <sdurws/ird/core/Identity.hpp>

#include <sdurws/ird/core/Errors.hpp>

#include <algorithm>
#include <random>
#include <stdexcept>

namespace sdurws::ird::core {
namespace detail {

// ---------------------------------------------------------------------
// 严格解析："<tag><32 个小写十六进制>"
// ---------------------------------------------------------------------
bool tryParseId128(std::string_view tag, std::string_view text,
                   std::array<std::uint8_t, 16>* out) noexcept
{
    // 前缀 tag 逐字符匹配：类型误用（rev- 串喂给 ObjectId）在此即失败。
    if (text.size() != tag.size() + 32) {           // 总长固定：tag＋32 hex
        return false;
    }
    for (std::size_t i = 0; i < tag.size(); ++i) {
        if (text[i] != tag[i]) {                    // tag 逐字符（含大小写——tag 本身小写）
            return false;
        }
    }
    // 十六进制体：恰 32 个、仅 [0-9a-f]（大写 A-F 拒绝——规范文本唯一小写）。
    const std::size_t body = tag.size();
    for (std::size_t i = 0; i < 32; ++i) {
        const char c = text[body + i];
        unsigned nibble;
        if (c >= '0' && c <= '9')      { nibble = static_cast<unsigned>(c - '0'); }
        else if (c >= 'a' && c <= 'f') { nibble = static_cast<unsigned>(c - 'a') + 10u; }
        else { return false; }                      // 大写/字母 g 以后/符号/空白/中文等一律拒绝
        // 字节序＝文本序：每两个字符合成一字节，高半字节在前——与 formatId128 对称。
        if ((i & 1u) == 0u) { (*out)[i / 2] = static_cast<std::uint8_t>(nibble << 4); }
        else { (*out)[i / 2] = static_cast<std::uint8_t>((*out)[i / 2] | nibble); }
    }
    return true;
}

std::string formatId128(std::string_view tag, const std::array<std::uint8_t, 16>& bytes)
{
    static constexpr char kHex[] = "0123456789abcdef";   // 规范文本唯一小写（§4.1）
    std::string s;
    s.reserve(tag.size() + 32);
    s.append(tag);
    for (const std::uint8_t b : bytes) {                 // 字节序＝文本序（高半字节在前）
        s.push_back(kHex[b >> 4]);
        s.push_back(kHex[b & 0x0Fu]);
    }
    return s;
}

// ---------------------------------------------------------------------
// FNV-1a 128 位哈希（§4.1 std::hash 规定算法；两 64 位半字模拟 128 位状态）
// ---------------------------------------------------------------------
void fnv1a128(const std::uint8_t* data, std::size_t n,
              std::uint64_t* outHi, std::uint64_t* outLo) noexcept
{
    // FNV-1a 128 偏移基＝0x6C62272E07BB0142_62B821756295C58D；素数＝2^88+2^8+0x3B。
    // 状态 (hi,lo) 视作 128 位大端整数：迭代"异或字节→乘素数（模 2^128）"。
    std::uint64_t hi = 0x6C62272E07BB0142ULL;
    std::uint64_t lo = 0x62B821756295C58DULL;

    // 累加一个 128 位项（无回绕加法）。
    const auto add = [&](std::uint64_t th, std::uint64_t tl) {
        const std::uint64_t nl = lo + tl;
        const std::uint64_t c = (nl < lo) ? 1ULL : 0ULL;
        hi = hi + th + c;
        lo = nl;
    };
    // 累加 state << shift（shift∈[0,127]；≥64 时低字全零、高字＝lo 左移）。
    const auto addShifted = [&](unsigned s) {
        if (s == 0)        { add(hi, lo); return; }
        if (s >= 64)       { add(lo << (s - 64), 0ULL); return; }
        add((lo >> (64 - s)) | (hi << s), lo << s);
    };
    // 64×64→高 64 位（32 位分解，避免编译器扩展头文件）。
    const auto mulHi64 = [](std::uint64_t a, std::uint64_t b) -> std::uint64_t {
        const std::uint64_t al = a & 0xFFFFFFFFULL, ah = a >> 32;
        const std::uint64_t bl = b & 0xFFFFFFFFULL, bh = b >> 32;
        const std::uint64_t p00 = al * bl;
        const std::uint64_t p01 = al * bh;
        const std::uint64_t p10 = ah * bl;
        const std::uint64_t p11 = ah * bh;
        const std::uint64_t mid = (p00 >> 32) + (p01 & 0xFFFFFFFFULL) + (p10 & 0xFFFFFFFFULL);
        return p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32);
    };
    // 累加 state * m（m<2^64；128 位截断：低＝lo*m，高＝mulHi(lo,m)＋lo 高字贡献 hi*m）。
    const auto addMulSmall = [&](std::uint64_t m) {
        const std::uint64_t tl = lo * m;
        const std::uint64_t th = mulHi64(lo, m) + hi * m;
        add(th, tl);
    };

    for (std::size_t i = 0; i < n; ++i) {
        // 异或字节（进入 128 位状态最低字节＝lo 低 8 位）
        lo ^= static_cast<std::uint64_t>(data[i]);
        // 乘素数：p = 2^88 + 2^8 + 0x3B（三项分解累加，模 2^128）
        addShifted(88);
        addShifted(8);
        addMulSmall(0x3BULL);
    }
    *outHi = hi;
    *outLo = lo;
}

// ---------------------------------------------------------------------
// 非零随机生成（thread_local 引擎——§5.1 线程安全承诺）
// ---------------------------------------------------------------------
std::array<std::uint8_t, 16> generateId128Bytes()
{
    // random_device 播种一次（thread_local）：进程内每线程独立引擎，无锁并发。
    // 生成器选择与契约：§4.1 明文"random_device 播种的 mt19937_64"——非密码学
    // 承诺，唯一性要求＝128 位随机空间下的碰撞概率可忽略。
    thread_local std::mt19937_64 engine{std::random_device{}()};
    std::array<std::uint8_t, 16> bytes{};
    do {
        const std::uint64_t w1 = engine();
        const std::uint64_t w2 = engine();
        for (int i = 0; i < 8; ++i) {                    // 大端装配：高字节在低下标（与 hex 序一致）
            bytes[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(w1 >> (56 - 8 * i));
            bytes[static_cast<std::size_t>(i) + 8] = static_cast<std::uint8_t>(w2 >> (56 - 8 * i));
        }
    } while (std::all_of(bytes.begin(), bytes.end(),
                         [](std::uint8_t b) { return b == 0; }));
    // 保留值纪律：全零重取（§4.1——"generate() 检验非零（零则重取）"）。
    return bytes;
}

}  // namespace detail

// ---------------------------------------------------------------------
// 六类型成员函数定义（宏＝与头文件声明成对的展开，参数必须与头文件一致）
// ---------------------------------------------------------------------
#define IRD_CORE_IMPLEMENT_ID_TYPE(TYPE_NAME, TAG_LIT)                                  \
    TYPE_NAME TYPE_NAME::generate() {                                                   \
        TYPE_NAME id;                                                                   \
        id.bytes = detail::generateId128Bytes();                                        \
        return id;                                                                      \
    }                                                                                   \
    TYPE_NAME TYPE_NAME::fromCanonical(std::string_view text) {                         \
        TYPE_NAME id;                                                                   \
        if (!detail::tryParseId128(TAG_LIT, text, &id.bytes)) {                         \
            /* 抛出轨迹：错误前缀稳定 core/identity/parse（§4.10/§5.1 错误行）。          \
               附带原文前 64 字符助诊断——超长截断防日志洪泛。 */                          \
            const std::size_t n = std::min<std::size_t>(text.size(), 64);               \
            throw CoreError(std::string{"core/identity/parse: " TAG_LIT " 期望 <tag><32 小写 hex>，实际: \""} \
                            + std::string(text.substr(0, n)) + "\"（长度 "                           \
                            + std::to_string(text.size()) + "）");                      \
        }                                                                               \
        return id;                                                                      \
    }                                                                                   \
    std::optional<TYPE_NAME> TYPE_NAME::tryFromCanonical(std::string_view text) noexcept { \
        TYPE_NAME id;                                                                   \
        if (!detail::tryParseId128(TAG_LIT, text, &id.bytes)) { return std::nullopt; }  \
        return id;                                                                      \
    }                                                                                   \
    std::string TYPE_NAME::toCanonical() const {                                        \
        return detail::formatId128(TAG_LIT, bytes);                                     \
    }                                                                                   \
    bool TYPE_NAME::isValid() const noexcept {                                          \
        return std::any_of(bytes.begin(), bytes.end(),                                  \
                           [](std::uint8_t b) { return b != 0; });                      \
    }

IRD_CORE_IMPLEMENT_ID_TYPE(ObjectId,   "obj-")
IRD_CORE_IMPLEMENT_ID_TYPE(ProjectId,  "prj-")
IRD_CORE_IMPLEMENT_ID_TYPE(BranchId,   "brn-")
IRD_CORE_IMPLEMENT_ID_TYPE(RevisionId, "rev-")
IRD_CORE_IMPLEMENT_ID_TYPE(RunId,      "run-")
IRD_CORE_IMPLEMENT_ID_TYPE(EventId,    "evt-")

// ---------------------------------------------------------------------
// AttemptId："att-<十进制>"——0 保留、前导零/符号/空白拒绝、2^64 溢出拒绝
// ---------------------------------------------------------------------
namespace {

/// 十进制体严格解析（try 轨）：首字符 '1'..'9'、其余 [0-9]、无 64 位溢出。
bool tryParseAttempt(std::string_view body, std::uint64_t* out) noexcept
{
    if (body.empty() || body[0] < '1' || body[0] > '9') {
        return false;   // 空/0 开头（含 "att-0" 保留值）/符号/前导零全部在此拒绝
    }
    std::uint64_t v = 0;
    for (const char c : body) {
        if (c < '0' || c > '9') { return false; }
        const std::uint64_t d = static_cast<std::uint64_t>(c - '0');
        // 溢出预检：v > (2^64-1-d)/10 时 v*10+d 必溢出——逐步检查不依赖 errno。
        if (v > (0xFFFFFFFFFFFFFFFFULL - d) / 10ULL) { return false; }
        v = v * 10ULL + d;
    }
    *out = v;
    return true;
}

}  // namespace

AttemptId AttemptId::fromCanonical(std::string_view text)
{
    AttemptId a;
    if (text.size() < 5 || text.substr(0, 4) != "att-"
        || !tryParseAttempt(text.substr(4), &a.value)) {
        const std::size_t n = std::min<std::size_t>(text.size(), 64);
        throw CoreError(std::string{"core/identity/parse: att- 期望 <十进制≥1>，实际: \""}
                        + std::string(text.substr(0, n)) + "\"（长度 " + std::to_string(text.size()) + "）");
    }
    return a;
}

std::optional<AttemptId> AttemptId::tryFromCanonical(std::string_view text) noexcept
{
    AttemptId a;
    if (text.size() < 5 || text.substr(0, 4) != "att-"
        || !tryParseAttempt(text.substr(4), &a.value)) {
        return std::nullopt;
    }
    return a;
}

std::string AttemptId::toCanonical() const
{
    return "att-" + std::to_string(value);
}

// ---------------------------------------------------------------------
// TaskIdentity：五元组值语义
// ---------------------------------------------------------------------
bool TaskIdentity::isValid() const noexcept
{
    // 五字段全合法（§5.1；UT-ID-T"缺一即 false"）。
    return project.isValid() && branch.isValid() && revision.isValid()
        && run.isValid() && attempt.isValid();
}

bool TaskIdentity::operator==(const TaskIdentity& o) const noexcept
{
    // 精确等值（附录 D 第 12 项：身份比较无容差）。
    return project == o.project && branch == o.branch && revision == o.revision
        && run == o.run && attempt == o.attempt;
}

bool TaskIdentity::operator<(const TaskIdentity& o) const noexcept
{
    // 字典序仅作容器键（§5.1"仅容器用"）——无业务排序语义，勿用于展示。
    if (project != o.project)   { return project < o.project; }
    if (branch != o.branch)     { return branch < o.branch; }
    if (revision != o.revision) { return revision < o.revision; }
    if (run != o.run)           { return run < o.run; }
    return attempt < o.attempt;
}

}  // namespace sdurws::ird::core
