/**
 * @file   candidate-probe.cpp
 * @brief  GAP-CORE-C4-01 修订候选 R2 隔离实验探针——仅存在于实验现场。
 *
 * 职责：
 *   1. 生成固定语料（与 REUSE-T14 oracle.py 同源的 3168 组确定性网格；CPython
 *      MT19937 init_by_array(20261023) 复刻的 10000 组随机；对抗枚举；24 组
 *      命名回归＝T14 oracle-result.json 全部失配原件；非有限/正负零专项）。
 *   2. 以 boost::multiprecision::cpp_int（实验现场独立精确 oracle，生产白名单外、
 *      不进入产品目标）按输入 binary64 的精确实数语义计算期望。
 *   3. 对照候选 closeWithin（compare-candidate.cpp）与原生产实现逐字内联副本：
 *      溢出分支必须与精确 oracle 全一致；非分支路径必须与原实现逐位一致；
 *      原 18 组正常路径舍入差异保持（批准范围外的既定口径，不计为失败）。
 *   4. 输出 result.json 与控制台摘要；退出码 0 仅当全部硬断言通过。
 *
 * 本探针不是产品测试，不宣称全输入正确性证明；结论边界见 README.md。
 */

#include <sdurws/ird/core/Compare.hpp>

#include <boost/multiprecision/cpp_int.hpp>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <random>
#include <string>
#include <vector>

using boost::multiprecision::cpp_int;
using namespace sdurws::ird::core;

// 来自 compare-candidate.cpp 的实验统计埋点（全局作用域，避免匿名命名空间内部链接问题）。
extern int g_maxExactSignIter;
extern int g_sentinelCount;

namespace {

// ---------------------------------------------------------------------------
// 原生产 closeWithin 逐字内联副本（源：core/src/Compare.cpp @ 6fb26b31d）。
// 用途：非分支路径逐位不变式核查。与生产文件的一致性由实验 README 记录的
// 文件哈希与 diff 审查保证。
// ---------------------------------------------------------------------------
bool originalCloseWithin(double value, double reference, Tolerance t) noexcept
{
    const bool inputsFinite = value == value && reference == reference
                           && value - value == 0 && reference - reference == 0;
    if (!inputsFinite) {
        return false;
    }
    const double diff = value - reference;
    const double limit = t.relative * (reference < 0 ? -reference : reference)
                       + t.absolute;
    return (diff <= limit) && (-diff <= limit);
}

// ---------------------------------------------------------------------------
// MT19937 + CPython init_by_array 复刻（用于逐位复现 oracle.py 的随机语料）。
// 参考：MT19937 论文与 CPython Modules/_randommodule.c 的 init_by_array/random。
// ---------------------------------------------------------------------------
struct PyMt19937 {
    std::uint32_t st[624];
    int idx = 624;

    static PyMt19937 init_by_array(std::uint32_t key) {
        PyMt19937 g;
        g.st[0] = 19650218u;
        for (int i = 1; i < 624; ++i) {
            g.st[i] = (1812433253u * (g.st[i - 1] ^ (g.st[i - 1] >> 30)) + static_cast<std::uint32_t>(i));
        }
        int i = 1, j = 0;
        for (int k = 624; k; --k) {
            g.st[i] = (g.st[i] ^ ((g.st[i - 1] ^ (g.st[i - 1] >> 30)) * 1664525u)) + key + j;
            ++i; ++j;
            if (i >= 624) { g.st[0] = g.st[623]; i = 1; }
            if (j >= 1) j = 0;
        }
        for (int k = 623; k; --k) {
            g.st[i] = (g.st[i] ^ ((g.st[i - 1] ^ (g.st[i - 1] >> 30)) * 1566083941u)) - static_cast<std::uint32_t>(i);
            ++i;
            if (i >= 624) { g.st[0] = g.st[623]; i = 1; }
        }
        g.st[0] = 0x80000000u;
        g.idx = 624;
        return g;
    }
    std::uint32_t next() {
        if (idx >= 624) {
            for (int i = 0; i < 624; ++i) {
                const std::uint32_t y = (st[i] & 0x80000000u) | (st[(i + 1) % 624] & 0x7fffffffu);
                const std::uint32_t n = st[(i + 397) % 624] ^ (y >> 1);
                st[i] = n ^ ((y & 1u) ? 0x9908b0dfu : 0u);
            }
            idx = 0;
        }
        std::uint32_t y = st[idx++];
        y ^= y >> 11;
        y ^= (y << 7) & 0x9d2c5680u;
        y ^= (y << 15) & 0xefc60000u;
        y ^= y >> 18;
        return y;
    }
    double random() {   // CPython random()：53 位，(a>>5)*2^26+(b>>6)，除 2^53
        const std::uint32_t a = next() >> 5;
        const std::uint32_t b = next() >> 6;
        return (a * 67108864.0 + b) * (1.0 / 9007199254740992.0);
    }
    static double uniform(double a, double b, double r) { return a + (b - a) * r; }
};

// ---------------------------------------------------------------------------
// 精确 oracle：binary64 输入的精确实数比较（cpp_int，独立于生产公式）。
// value = m·2^e（m 整数，e ≥ −1074），比较 |v−r| ≤ rel·|r| + a。
// ---------------------------------------------------------------------------
struct Xd { cpp_int m; int e; };
Xd xd(double d) {
    std::uint64_t b = 0;
    std::memcpy(&b, &d, 8);
    const bool neg = (b >> 63) != 0;
    const int E = static_cast<int>((b >> 52) & 0x7FF);
    const std::uint64_t frac = b & 0xFFFFFFFFFFFFFULL;
    Xd x;
    if (E == 0) { x.m = frac; x.e = -1074; }
    else { x.m = (cpp_int(1) << 52) | frac; x.e = E - 1075; }
    if (neg) x.m = -x.m;
    return x;
}
bool oracleClose(double vd, double rd, double reld, double ad) {
    const Xd v = xd(vd), r = xd(rd), rel = xd(reld), a = xd(ad);
    int eL = v.e < r.e ? v.e : r.e;
    cpp_int L = (v.m << (v.e - eL)) - (r.m << (r.e - eL));
    if (L < 0) L = -L;
    cpp_int rm = r.m;
    if (rm < 0) rm = -rm;
    const cpp_int t1 = rel.m * rm;
    const int e1 = rel.e + r.e;
    const int eR = e1 < a.e ? e1 : a.e;
    cpp_int R = (t1 << (e1 - eR)) + (a.m << (a.e - eR));
    if (eL >= eR) L <<= (eL - eR); else R <<= (eR - eL);
    return L <= R;
}

// ---------------------------------------------------------------------------
// 语料
// ---------------------------------------------------------------------------
struct Case {
    double v, r, rel, a;
    const char* tag;
};

std::string hexbits(double d) {
    std::uint64_t b = 0;
    std::memcpy(&b, &d, 8);
    char buf[32];
    std::snprintf(buf, sizeof buf, "%016llx", static_cast<unsigned long long>(b));
    return buf;
}
double fromHexFloat(const char* s) { return std::strtod(s, nullptr); }

// T14 oracle-result.json 全部 24 组失配原件（顺序＝文件顺序；前 6 组 bothOverflow）。
const char* kNamed[24][4] = {
    {"0x1.fffffffffffffp+1023", "-0x1.fffffffffffffp+1022", "0x1.fffffffffffffp+0", "0x1.fffffffffffffp+1022"},
    {"0x1.ffffffffffffep+1023", "-0x1.fffffffffffffp+1023", "0x1.fffffffffffffp+0", "0x0.0p+0"},
    {"0x1.ffffffffffffep+1023", "-0x1.fffffffffffffp+1023", "0x1.fffffffffffffp+0", "0x0.0000000000001p-1022"},
    {"-0x1.fffffffffffffp+1023", "0x1.fffffffffffffp+1022", "0x1.fffffffffffffp+0", "0x1.fffffffffffffp+1022"},
    {"-0x1.ffffffffffffep+1023", "0x1.fffffffffffffp+1023", "0x1.fffffffffffffp+0", "0x0.0p+0"},
    {"-0x1.ffffffffffffep+1023", "0x1.fffffffffffffp+1023", "0x1.fffffffffffffp+0", "0x0.0000000000001p-1022"},
    {"-0x1.fffffffffffffp+1023", "0x0.0000000000001p-1022", "0x0.0p+0", "0x1.fffffffffffffp+1023"},
    {"-0x1.fffffffffffffp+1023", "0x0.0000000000001p-1022", "0x1.0000000000000p-1", "0x1.fffffffffffffp+1023"},
    {"-0x1.ffffffffffffep+1023", "0x1.fffffffffffffp+1023", "0x1.fffffffffffffp+0", "0x0.0p+0"},
    {"-0x1.ffffffffffffep+1023", "0x1.fffffffffffffp+1023", "0x1.fffffffffffffp+0", "0x0.0000000000001p-1022"},
    {"-0x1.fffffffffffffp+1022", "0x0.0000000000001p-1022", "0x0.0p+0", "0x1.fffffffffffffp+1022"},
    {"-0x1.fffffffffffffp+1022", "0x0.0000000000001p-1022", "0x1.0000000000000p-1", "0x1.fffffffffffffp+1022"},
    {"0x0.0000000000001p-1022", "-0x1.fffffffffffffp+1023", "0x0.0p+0", "0x1.fffffffffffffp+1023"},
    {"0x0.0000000000001p-1022", "-0x1.fffffffffffffp+1023", "0x1.0000000000000p-1", "0x1.fffffffffffffp+1022"},
    {"0x0.0000000000001p-1022", "-0x1.fffffffffffffp+1023", "0x1.0000000000000p+0", "0x0.0p+0"},
    {"0x0.0000000000001p-1022", "-0x1.7ffffffffffffp+1023", "0x1.0000000000000p+0", "0x0.0p+0"},
    {"0x0.0000000000001p-1022", "-0x1.fffffffffffffp+1022", "0x0.0p+0", "0x1.fffffffffffffp+1022"},
    {"0x0.0000000000001p-1022", "-0x1.fffffffffffffp+1022", "0x1.0000000000000p+0", "0x0.0p+0"},
    {"-0x0.0000000000001p-1022", "0x1.fffffffffffffp+1023", "0x0.0p+0", "0x1.fffffffffffffp+1023"},
    {"-0x0.0000000000001p-1022", "0x1.fffffffffffffp+1023", "0x1.0000000000000p-1", "0x1.fffffffffffffp+1022"},
    {"-0x0.0000000000001p-1022", "0x1.fffffffffffffp+1023", "0x1.0000000000000p+0", "0x0.0p+0"},
    {"-0x0.0000000000001p-1022", "0x1.7ffffffffffffp+1023", "0x1.0000000000000p+0", "0x0.0p+0"},
    {"-0x0.0000000000001p-1022", "0x1.fffffffffffffp+1022", "0x0.0p+0", "0x1.fffffffffffffp+1022"},
    {"-0x0.0000000000001p-1022", "0x1.fffffffffffffp+1022", "0x1.0000000000000p+0", "0x0.0p+0"},
};

struct Report {
    long long total = 0;
    long long branchHits = 0;
    long long overflowMismatch = 0;     // 分支内候选≠oracle（必须为 0）
    long long invarianceViolation = 0;  // 非分支候选≠原实现（必须为 0）
    long long normalOracleDiff = 0;     // 非分支 oracle 差异（既定舍入口径，信息项）
    long long sentinel = 0;             // exactSign 不可达哨兵（必须为 0）
    long long branchFixed = 0;          // 分支内原误绿→候选改判（信息项）
    int maxIter = 0;
    std::string mismatchJson;
};

void run(const Case& c, Report& rp) {
    ++rp.total;
    const Tolerance t{c.rel, c.a};
    const bool cand = closeWithin(c.v, c.r, t);
    const bool orig = originalCloseWithin(c.v, c.r, t);
    // 非有限 v/r：契约 false，两实现必须一致——此域内不允许任何行为差异。
    if ((!std::isfinite(c.v) || !std::isfinite(c.r))) {
        if (cand != orig) {
            ++rp.invarianceViolation;
            rp.mismatchJson += "{\"tag\":\"" + std::string(c.tag) + "\",\"input\":["
                             + hexbits(c.v) + "," + hexbits(c.r) + ","
                             + hexbits(c.rel) + "," + hexbits(c.a)
                             + "],\"kind\":\"invariance-nonfinite\"},";
        }
        return;
    }
    const double diff = c.v - c.r;
    const double limit = c.rel * (c.r < 0 ? -c.r : c.r) + c.a;
    const bool branch = std::isinf(diff) && std::isinf(limit) && limit > 0.0;

    if (g_maxExactSignIter > rp.maxIter) rp.maxIter = g_maxExactSignIter;
    rp.sentinel = g_sentinelCount;

    if (branch) {
        // 修订分支：允许与原实现不同（原误绿在此被纠正），以精确 oracle 为准。
        ++rp.branchHits;
        if (orig && !cand) ++rp.branchFixed;
        if (!std::isfinite(c.rel) || !std::isfinite(c.a)) {
            // rel/a 非有限属生产契约保留路径（candidate==original 已核）；oracle 域外不判。
            if (cand != orig) {
                ++rp.invarianceViolation;
                rp.mismatchJson += "{\"tag\":\"" + std::string(c.tag)
                                + "\",\"kind\":\"invariance-nontinite-tol\"},";
            }
            return;
        }
        const bool expect = oracleClose(c.v, c.r, c.rel, c.a);
        if (cand != expect) {
            ++rp.overflowMismatch;
            rp.mismatchJson += "{\"tag\":\"" + std::string(c.tag) + "\",\"input\":["
                             + hexbits(c.v) + "," + hexbits(c.r) + ","
                             + hexbits(c.rel) + "," + hexbits(c.a)
                             + "],\"oracle\":" + (expect ? "true" : "false")
                             + ",\"candidate\":" + (cand ? "true" : "false")
                             + ",\"original\":" + (orig ? "true" : "false") + "},";
        }
    } else {
        // 非分支：候选必须与原实现逐位一致（正常路径不变的硬约束）。
        if (cand != orig) {
            ++rp.invarianceViolation;
            rp.mismatchJson += "{\"tag\":\"" + std::string(c.tag) + "\",\"input\":["
                             + hexbits(c.v) + "," + hexbits(c.r) + ","
                             + hexbits(c.rel) + "," + hexbits(c.a)
                             + "],\"kind\":\"invariance\"},";
        }
        // 非分支的 oracle 差异＝既定舍入口径（T14 已记录 18 组量级），仅计数不判失败。
        if (std::isfinite(c.rel) && std::isfinite(c.a)) {
            const bool expect = oracleClose(c.v, c.r, c.rel, c.a);
            if (cand != expect) ++rp.normalOracleDiff;
        }
    }
}

}  // namespace

int main() {
    const double m = std::numeric_limits<double>::max();
    const double dmin = std::numeric_limits<double>::denorm_min();
    const double minn = std::numeric_limits<double>::min();
    Report rp;

    // ---- 1. 确定性网格（与 REUSE-T14 oracle.py 完全同源：11×8×9×4）----
    const double gv[] = {m, std::nextafter(m, 0), m * .75, m * .5, -m,
                         -std::nextafter(m, 0), -m * .75, -m * .5, 0., dmin, -dmin};
    const double gr[] = {m, -m, m * .75, -m * .75, m * .5, -m * .5, 0., dmin};
    const double grel[] = {0., .5, 1., 1.5, std::nextafter(2., 0.), 2.,
                           std::nextafter(2., std::numeric_limits<double>::infinity()), 3., m};
    const double ga[] = {0., dmin, m * .5, m};
    for (double v : gv)
        for (double r : gr)
            for (double rel : grel)
                for (double a : ga)
                    run({v, r, rel, a, "grid"}, rp);

    // ---- 2. CPython MT 复刻随机 10000（种子 20261023，逐位复现 oracle.py）----
    {
        PyMt19937 rng = PyMt19937::init_by_array(20261023u);
        for (int i = 0; i < 10000; ++i) {
            const double v = std::ldexp(PyMt19937::uniform(.5, 1, rng.random()), 1024);
            const double r = -std::ldexp(PyMt19937::uniform(.5, 1, rng.random()), 1024);
            const double rel = PyMt19937::uniform(0, 4, rng.random());
            const double a = std::ldexp(PyMt19937::uniform(0, 1, rng.random()), 1023);
            run({v, r, rel, a, "rand-cpython-mt"}, rp);
        }
    }

    // ---- 3. 对抗枚举：极值邻域×阈值邻点（确定性）----
    {
        const double bases[] = {m, std::nextafter(m, 0), 0x1p1023,
                                std::nextafter(0x1p1023, 0), m * .75, m * .5};
        const double rels[] = {0., .5, 1., 1.5, std::nextafter(1.5, 0),
                               std::nextafter(1.5, std::numeric_limits<double>::infinity()),
                               std::nextafter(2., 0.), 2.,
                               std::nextafter(2., std::numeric_limits<double>::infinity()),
                               3., m, std::nextafter(m, 0)};
        const double abss[] = {0., dmin, minn, m * .5, m};
        for (double base : bases) {
            const double ulpB = base - std::nextafter(base, 0);   // 有限 ulp（顶端不得用 +inf 方向）
            for (int ioff = 0; ioff < 3; ++ioff)
                for (int joff = 0; joff < 3; ++joff)
                    for (int sv = -1; sv <= 1; sv += 2)
                        for (int sr = -1; sr <= 1; sr += 2)
                            for (double rel : rels)
                                for (double a : abss) {
                                    const double v = sv * (base - ioff * ulpB);
                                    const double r = sr * (base - joff * ulpB);
                                    run({v, r, rel, a, "adv-extreme"}, rp);
                                }
        }
        // 次正规/最小正规数族
        const double sm[] = {dmin, std::nextafter(dmin, std::numeric_limits<double>::infinity()),
                             minn, std::nextafter(minn, 0), std::nextafter(minn, std::numeric_limits<double>::infinity()), 0.};
        const double srel[] = {0., dmin, 1., 2., m};
        const double sa[] = {0., dmin, minn, m};
        for (double v0 : sm)
            for (double r0 : sm)
                for (int sv = -1; sv <= 1; sv += 2)
                    for (int sr = -1; sr <= 1; sr += 2)
                        for (double rel : srel)
                            for (double a : sa)
                                run({sv * v0, sr * r0, rel, a, "adv-subnormal"}, rp);
    }

    // ---- 4. 命名回归：T14 全部 24 组失配原件＋GAP 三红例（标量及镜像）----
    for (int i = 0; i < 24; ++i)
        run({fromHexFloat(kNamed[i][0]), fromHexFloat(kNamed[i][1]),
             fromHexFloat(kNamed[i][2]), fromHexFloat(kNamed[i][3]),
             i < 6 ? "named-overflow" : "named-normalpath"}, rp);
    for (int sv = -1; sv <= 1; sv += 2)
        for (int sr = -1; sr <= 1; sr += 2) {
            run({sv * m, sr * m, 1.5, 0., "named-red-1.5"}, rp);
            run({sv * m, sr * m, std::nextafter(2., 0.), 0., "named-red-below2"}, rp);
            run({sv * m, sr * m, 2., 0., "named-red-at2"}, rp);
            run({sv * m, sr * m, 0.5, m, "named-red-abs"}, rp);
            run({sv * m, sr * m, 1.0, m, "named-red-abs2"}, rp);
        }
    // vector/Q 重载消费同一标量入口：vector 路径直接验证（Q 无 rw 头不编）。
    {
        const bool vecOk = !allCloseWithin(std::vector<double>{m},
                                           std::vector<double>{-m},
                                           Tolerance::make(1.5, 0.0));
        if (!vecOk) {
            ++rp.overflowMismatch;
            rp.mismatchJson += "{\"tag\":\"vector-red\"},";
        }
    }

    // ---- 5. 非有限与正负零专项 ----
    {
        const double nans[] = {std::numeric_limits<double>::quiet_NaN(),
                               std::numeric_limits<double>::signaling_NaN(),
                               std::numeric_limits<double>::infinity(),
                               -std::numeric_limits<double>::infinity()};
        for (double bad : nans) {
            run({bad, 0., 1., 1., "nonfinite-v"}, rp);
            run({0., bad, 1., 1., "nonfinite-r"}, rp);
            run({bad, bad, 1., 1., "nonfinite-both"}, rp);
            run({m, -m, bad, 0., "nonfinite-rel-onbranch"}, rp);  // limit 视 bad 而定，逐位保持
        }
        run({0., -0., 0., 0., "zero-pm"}, rp);
        run({-0., 0., 0., 0., "zero-mp"}, rp);
        run({-0., -0., 0., 0., "zero-mm"}, rp);
        run({0., 0., 0., 0., "zero-pp"}, rp);
    }

    // ---- 汇总 ----
    std::string json = "{\n  \"candidate\": \"R2 fma-residual + TwoSum exact-fold\",\n";
    json += "  \"baseHead\": \"6fb26b31ddcd514ba2d3dab8ff548b977b54c9c6\",\n";
    json += "  \"seed\": 20261023,\n";
    json += "  \"totals\": {\"cases\": " + std::to_string(rp.total)
          + ", \"branchHits\": " + std::to_string(rp.branchHits)
          + ", \"branchFixed\": " + std::to_string(rp.branchFixed) + "},\n";
    json += "  \"overflowMismatch\": " + std::to_string(rp.overflowMismatch) + ",\n";
    json += "  \"invarianceViolation\": " + std::to_string(rp.invarianceViolation) + ",\n";
    json += "  \"normalOracleDiff\": " + std::to_string(rp.normalOracleDiff) + ",\n";
    json += "  \"sentinel\": " + std::to_string(rp.sentinel) + ",\n";
    json += "  \"maxExactSignIter\": " + std::to_string(rp.maxIter) + ",\n";
    json += "  \"mismatches\": [" + (rp.mismatchJson.empty()
          ? std::string() : rp.mismatchJson.substr(0, rp.mismatchJson.size() - 1)) + "]\n}\n";
    {
        std::ofstream f("result.json", std::ios::binary);
        f << json;
    }
    std::printf("cases=%lld branchHits=%lld branchFixed=%lld overflowMismatch=%lld "
                "invarianceViolation=%lld normalOracleDiff=%lld sentinel=%lld maxIter=%d\n",
                rp.total, rp.branchHits, rp.branchFixed, rp.overflowMismatch,
                rp.invarianceViolation, rp.normalOracleDiff, rp.sentinel, rp.maxIter);
    const bool pass = rp.overflowMismatch == 0 && rp.invarianceViolation == 0
                   && rp.sentinel == 0;
    std::printf(pass ? "VERDICT: PASS\n" : "VERDICT: FAIL\n");
    return pass ? 0 : 1;
}
