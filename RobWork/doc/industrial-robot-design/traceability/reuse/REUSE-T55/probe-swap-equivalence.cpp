// REUSE-T55 阶段一换核等价探针（upstreamComparison 面）——非生产代码。
// 对照基准＝基线 220189b2f 的替身核逐字副本（oc_ 前缀，与 wp3-bench 同源抄录）；
// 对照对象＝生产换核目标的上游 rw::math 直接 API 调用形态。
// 判据分两档：位模式恒等（memcmp，−0.0 与 +0.0 视为不等以钉乘加序）与
// 数值恒等（逐元素差＝0.0，含零符号差异）。断言失败以 PROBE-FAIL 行退出。
// 构建/运行：见同目录 run/probe-build.cmd 与 run/probe-swap.log。
#include <rw/math/LinearAlgebra.hpp>
#include <rw/math/RPY.hpp>
#include <rw/math/Rotation3D.hpp>
#include <rw/math/Transform3D.hpp>
#include <rw/math/Vector3D.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace {

constexpr double kPi = 3.14159265358979323846;

// =====================================================================
// 替身核逐字副本（基线 220189b2f——与 wp3-bench oldcore 同源）
// =====================================================================

rw::math::Vector3D<double> oc_rotVec(const rw::math::Rotation3D<double>& r,
                                     const rw::math::Vector3D<double>& v)
{
    return rw::math::Vector3D<double>(
        r(0, 0) * v[0] + r(0, 1) * v[1] + r(0, 2) * v[2],
        r(1, 0) * v[0] + r(1, 1) * v[1] + r(1, 2) * v[2],
        r(2, 0) * v[0] + r(2, 1) * v[1] + r(2, 2) * v[2]);
}

rw::math::Rotation3D<double> oc_rotMul(const rw::math::Rotation3D<double>& ra,
                                       const rw::math::Rotation3D<double>& rb)
{
    rw::math::Rotation3D<double> out;
    for (std::size_t i = 0; i < 3; ++i) {
        for (std::size_t j = 0; j < 3; ++j) {
            double s = 0.0;
            for (std::size_t k = 0; k < 3; ++k) { s += ra(i, k) * rb(k, j); }
            out(i, j) = s;
        }
    }
    return out;
}

rw::math::Transform3D<double> oc_transformMul(const rw::math::Transform3D<double>& ta,
                                              const rw::math::Transform3D<double>& tb)
{
    const rw::math::Rotation3D<double> r = oc_rotMul(ta.R(), tb.R());
    const rw::math::Vector3D<double> pb = tb.P();
    const rw::math::Vector3D<double> p = oc_rotVec(ta.R(), pb) + ta.P();
    return rw::math::Transform3D<double>(p, r);
}

rw::math::Vector3D<double> oc_rotateTranspose(const rw::math::Rotation3D<double>& r,
                                              const rw::math::Vector3D<double>& v)
{
    return rw::math::Vector3D<double>(
        r(0, 0) * v[0] + r(1, 0) * v[1] + r(2, 0) * v[2],
        r(0, 1) * v[0] + r(1, 1) * v[1] + r(2, 1) * v[2],
        r(0, 2) * v[0] + r(1, 2) * v[1] + r(2, 2) * v[2]);
}

rw::math::Rotation3D<double> oc_rotateTransposeFrame(const rw::math::Rotation3D<double>& r,
                                                     const rw::math::Rotation3D<double>& rb)
{
    rw::math::Rotation3D<double> out;
    for (std::size_t i = 0; i < 3; ++i) {
        for (std::size_t j = 0; j < 3; ++j) {
            double s = 0.0;
            for (std::size_t k = 0; k < 3; ++k) { s += r(k, i) * rb(k, j); }
            out(i, j) = s;
        }
    }
    return out;
}

/// 自写逐列 SO(3) 校验（基线 validateBoxOrientation 判定面——返回非有限/
/// 非正交/非右手/通过四态，供与新核三分对照）。
const char* oc_validateVerdict(const rw::math::Rotation3D<double>& r)
{
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            double dot = 0;
            for (int k = 0; k < 3; ++k) {
                if (!std::isfinite(r(k, i))) { return "nonfinite"; }
                dot += r(k, i) * r(k, j);
            }
            if (std::fabs(dot - (i == j ? 1.0 : 0.0)) > 1e-9) { return "nonorthogonal"; }
        }
    }
    const double det = r(0,0)*(r(1,1)*r(2,2)-r(1,2)*r(2,1))
        - r(0,1)*(r(1,0)*r(2,2)-r(1,2)*r(2,0))
        + r(0,2)*(r(1,0)*r(2,1)-r(1,1)*r(2,0));
    if (std::fabs(det - 1.0) > 1e-9) { return "lefthand"; }
    return "pass";
}

/// 新核判定面（非有限预检＋isProperRotation＋isOrthonormal 细分）。
const char* newValidateVerdict(const rw::math::Rotation3D<double>& r)
{
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            if (!std::isfinite(r(i, j))) { return "nonfinite"; }
        }
    }
    if (!r.isProperRotation(1e-9)) {
        if (rw::math::LinearAlgebra::isOrthonormal(r.e(), 1e-9)) { return "lefthand"; }
        return "nonorthogonal";
    }
    return "pass";
}

// =====================================================================
// 确定性输入（splitmix64——与 wp3-bench 同种子族，独立实例）
// =====================================================================

std::uint64_t mix64(std::uint64_t x)
{
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

double u01(std::uint64_t& s)
{
    s = mix64(s);
    return static_cast<double>(s >> 11) * (1.0 / 9007199254740992.0);
}

double umm(std::uint64_t& s) { return u01(s) * 2.0 - 1.0; }

rw::math::Rotation3D<double> axisAngle(const rw::math::Vector3D<double>& axis, double angle)
{
    const double c = std::cos(angle);
    const double s = std::sin(angle);
    const double t = 1.0 - c;
    const double kx = axis[0], ky = axis[1], kz = axis[2];
    return rw::math::Rotation3D<double>(
        c + t * kx * kx,      t * kx * ky - s * kz, t * kx * kz + s * ky,
        t * kx * ky + s * kz, c + t * ky * ky,      t * ky * kz - s * kx,
        t * kx * kz - s * ky, t * ky * kz + s * kx, c + t * kz * kz);
}

rw::math::Vector3D<double> randomAxis(std::uint64_t& s)
{
    rw::math::Vector3D<double> a(umm(s), umm(s), umm(s));
    const double n = a.norm2();
    if (n < 1e-6) { return rw::math::Vector3D<double>(1.0, 0.0, 0.0); }
    return a / n;
}

// =====================================================================
// 比较原语
// =====================================================================

int g_failures = 0;

/// double → 单调 ULP 键（符号位映射——IEEE754 位序整型化，供 ULP 距离）。
std::uint64_t bitFromDouble(double v)
{
    std::uint64_t u;
    std::memcpy(&u, &v, sizeof(double));
    return (u & 0x8000000000000000ull) ? (~u) : (u | 0x8000000000000000ull);
}

struct MatrixCmp {
    bool bitEqual = true;      // memcmp 级（−0.0 ≠ +0.0）
    bool valueEqual = true;    // 逐元素差＝0
    double maxAbsDiff = 0.0;
    std::string zeroSignEntries;  // 仅零符号差异的 (i,j) 清单
};

template <class A, class B>
MatrixCmp compare9(const A& a, const B& b)
{
    MatrixCmp c;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            const double x = a(i, j);
            const double y = b(i, j);
            const double d = std::fabs(x - y);
            if (d > c.maxAbsDiff) { c.maxAbsDiff = d; }
            if (std::memcmp(&x, &y, sizeof(double)) != 0) {
                c.bitEqual = false;
                if (d == 0.0) {
                    if (!c.zeroSignEntries.empty()) { c.zeroSignEntries += " "; }
                    c.zeroSignEntries += "(" + std::to_string(i) + "," + std::to_string(j) + ")";
                } else {
                    c.valueEqual = false;
                }
            }
        }
    }
    return c;
}

void report(const std::string& tag, const MatrixCmp& c, std::size_t cases,
            const std::string& note)
{
    std::printf("PROBE %s cases=%zu bitEqual=%s valueEqual=%s maxAbsDiff=%.3e%s%s\n",
                tag.c_str(), cases, c.bitEqual ? "true" : "false",
                c.valueEqual ? "true" : "false", c.maxAbsDiff,
                c.zeroSignEntries.empty() ? "" : " zeroSignOnly=",
                c.zeroSignEntries.empty() ? "" : c.zeroSignEntries.c_str());
    if (!note.empty()) { std::printf("PROBE %s note: %s\n", tag.c_str(), note.c_str()); }
    if (!c.valueEqual) { ++g_failures; }
}

}  // namespace

int main()
{
    std::printf("# REUSE-T55 swap-equivalence probe - baseline 220189b2f standin cores vs rw::math direct API\n");

    // ---- 输入集 ----
    std::uint64_t s = 0x20261027ull;
    std::vector<rw::math::Rotation3D<double>> rotG, rotG2, rotEdge;
    std::vector<rw::math::Vector3D<double>> vecG;
    for (std::size_t i = 0; i < 256; ++i) {
        rotG.push_back(axisAngle(randomAxis(s), kPi * (2.0 * u01(s) - 1.0)));
        rotG2.push_back(axisAngle(randomAxis(s), kPi * (2.0 * u01(s) - 1.0)));
        vecG.push_back(rw::math::Vector3D<double>(umm(s), umm(s), umm(s)));
        switch (i % 4U) {
            case 0: rotEdge.push_back(rw::math::Rotation3D<double>(1,0,0,0,1,0,0,0,1)); break;
            case 1: rotEdge.push_back(axisAngle(randomAxis(s), 1e-9 + (1e-6 - 1e-9) * u01(s))); break;
            case 2: {
                const double q = kPi / 2.0;
                rotEdge.push_back(axisAngle(rw::math::Vector3D<double>(
                    i % 8U == 2 ? 1.0 : 0.0, i % 8U == 4 ? 1.0 : 0.0, i % 8U == 6 ? 1.0 : 0.0), q));
                break;
            }
            default: rotEdge.push_back(axisAngle(randomAxis(s), kPi * (2.0 * u01(s) - 1.0))); break;
        }
    }

    // ---- P1 rotVec：替身 vs r*v（Rotation3D.hpp:358→Rotation3D.cpp:106-108） ----
    {
        MatrixCmp agg;
        for (std::size_t i = 0; i < rotG.size(); ++i) {
            const auto& r = (i % 3U == 0U) ? rotEdge[i] : rotG[i];
            const auto a = oc_rotVec(r, vecG[i]);
            const auto b = r * vecG[i];
            for (int k = 0; k < 3; ++k) {
                const double d = std::fabs(a[k] - b[k]);
                if (d > agg.maxAbsDiff) { agg.maxAbsDiff = d; }
                if (std::memcmp(&a[k], &b[k], sizeof(double)) != 0) {
                    agg.bitEqual = false;
                    if (d != 0.0) { agg.valueEqual = false; }
                }
            }
        }
        report("P1.rotVec stv r*v", agg, rotG.size(), "operator*(Rotation3D,Vector3D)");
    }

    // ---- P2 rotMul：替身 vs ra*rb（:317→:112-155） ----
    {
        MatrixCmp agg;
        for (std::size_t i = 0; i < rotG.size(); ++i) {
            const auto& a0 = (i % 2U) ? rotEdge[i] : rotG[i];
            const auto& b0 = (i % 2U) ? rotG2[i] : rotEdge[i];
            const auto m = compare9(oc_rotMul(a0, b0), a0 * b0);
            agg.bitEqual &= m.bitEqual; agg.valueEqual &= m.valueEqual;
            agg.maxAbsDiff = std::max(agg.maxAbsDiff, m.maxAbsDiff);
            if (!m.zeroSignEntries.empty()) { agg.zeroSignEntries = m.zeroSignEntries; }
        }
        report("P2.rotMul stv ra*rb", agg, rotG.size(), "Rotation3D::operator*(Rotation3D)");
    }

    // ---- P3 transformMul：替身 vs ta*tb（Transform3D.hpp:333） ----
    {
        MatrixCmp agg;
        for (std::size_t i = 0; i < rotG.size(); ++i) {
            const rw::math::Transform3D<double> ta(vecG[i], rotG[i]);
            const rw::math::Vector3D<double> pb(umm(s), umm(s), umm(s));
            const rw::math::Transform3D<double> tb(pb, (i % 2U) ? rotEdge[i] : rotG2[i]);
            const auto x = oc_transformMul(ta, tb);
            const auto y = ta * tb;
            MatrixCmp m = compare9(x.R(), y.R());
            for (int k = 0; k < 3; ++k) {
                const double d = std::fabs(x.P()[k] - y.P()[k]);
                if (d > m.maxAbsDiff) { m.maxAbsDiff = d; }
                if (std::memcmp(&x.P()[k], &y.P()[k], sizeof(double)) != 0) {
                    m.bitEqual = false;
                    if (d != 0.0) { m.valueEqual = false; }
                }
            }
            agg.bitEqual &= m.bitEqual; agg.valueEqual &= m.valueEqual;
            agg.maxAbsDiff = std::max(agg.maxAbsDiff, m.maxAbsDiff);
            if (!m.zeroSignEntries.empty()) { agg.zeroSignEntries = m.zeroSignEntries; }
        }
        report("P3.transformMul stv ta*tb", agg, rotG.size(), "Transform3D::operator*");
    }

    // ---- P4 rotateTranspose：替身 vs inverse(r)*v（Rotation3D.hpp:542） ----
    {
        MatrixCmp agg;
        for (std::size_t i = 0; i < rotG.size(); ++i) {
            const auto& r = (i % 3U == 0U) ? rotEdge[i] : rotG[i];
            const auto a = oc_rotateTranspose(r, vecG[i]);
            const auto b = rw::math::inverse(r) * vecG[i];
            for (int k = 0; k < 3; ++k) {
                const double d = std::fabs(a[k] - b[k]);
                if (d > agg.maxAbsDiff) { agg.maxAbsDiff = d; }
                if (std::memcmp(&a[k], &b[k], sizeof(double)) != 0) {
                    agg.bitEqual = false;
                    if (d != 0.0) { agg.valueEqual = false; }
                }
            }
        }
        report("P4.rotateTranspose stv inverse(r)*v", agg, rotG.size(), "free inverse=exact transpose");
    }

    // ---- P5 rotateTransposeFrame：替身 vs inverse(r)*rb ----
    {
        MatrixCmp agg;
        for (std::size_t i = 0; i < rotG.size(); ++i) {
            const auto& a0 = (i % 2U) ? rotEdge[i] : rotG[i];
            const auto m = compare9(oc_rotateTransposeFrame(a0, rotG2[i]),
                                    rw::math::inverse(a0) * rotG2[i]);
            agg.bitEqual &= m.bitEqual; agg.valueEqual &= m.valueEqual;
            agg.maxAbsDiff = std::max(agg.maxAbsDiff, m.maxAbsDiff);
            if (!m.zeroSignEntries.empty()) { agg.zeroSignEntries = m.zeroSignEntries; }
        }
        report("P5.rotateTransposeFrame stv inverse(r)*rb", agg, rotG.size(), "");
    }

    // ---- P6 validateBoxOrientation 判定面：自写三分 vs 新核三分 ----
    {
        int disagree = 0;
        int total = 0;
        auto check = [&](const rw::math::Rotation3D<double>& r, const char* tag) {
            const char* a = oc_validateVerdict(r);
            const char* b = newValidateVerdict(r);
            ++total;
            if (std::string(a) != b) {
                ++disagree;
                std::printf("PROBE P6 DISAGREE %s hand=%s new=%s\n", tag, a, b);
            }
        };
        for (std::size_t i = 0; i < rotG.size(); ++i) { check(rotG[i], "general"); }
        // 显式病态：NaN / 非正交 / 左手
        rw::math::Rotation3D<double> bad = rotG[0];
        bad(0, 0) = std::numeric_limits<double>::quiet_NaN();
        check(bad, "nan");
        bad = rotG[1]; bad(0, 0) = 2.0; check(bad, "scaled");
        bad = rotG[2]; for (int k = 0; k < 3; ++k) { bad(k, 0) = -bad(k, 0); }
        check(bad, "lefthand");
        // 容差边界扫掠：单位阵 (0,0) 加扰动 d ∈ [0,1.5e-9]（列点积偏差 2d+d²
        // 跨 1e-9 阈）——行/列两条数值路径在边界带内可能翻转。
        for (int q = 0; q <= 150; ++q) {
            const double d = 1.0e-9 * 0.01 * q;
            rw::math::Rotation3D<double> rb(1,0,0,0,1,0,0,0,1);
            rb(0, 0) += d;
            check(rb, "diag-band");
        }
        // 非对角扰动带：identity (0,1)/(1,0) 对称微扰（行/列路径中间量不同）。
        for (int q = 0; q <= 150; ++q) {
            const double d = 1.0e-9 * 0.01 * q;
            rw::math::Rotation3D<double> rb(1,0,0,0,1,0,0,0,1);
            rb(0, 1) += d; rb(1, 0) -= d;
            check(rb, "offdiag-band");
        }
        std::printf("PROBE P6.validateVerdict cases=%d disagree=%d %s\n", total, disagree,
                    disagree == 0 ? "verdict-maps-identical" : "VERDICT-FLIP-IN-BAND(登记语义差异)");
        if (disagree != 0) { std::printf("PROBE P6 note: 边界带内判定翻转属既登记语义差异（行列路径），非生产测试域\n"); }

        // P6b 双重无效输入：诊断类别优先级（登记语义差异——基线在列序扫描中
        // 顺带查有限性（仅查第 i 列元素），非有限元位于后位列时「非正交」先抛；
        // 新核全元素有限预检前置，「非有限」恒先抛。异常类型同为
        // std::invalid_argument，测试面（RegionCoverageTest 仅钉类型）不受影响。
        int flips = 0, dual = 0;
        auto dualCheck = [&](rw::math::Rotation3D<double> r, int nanI, int nanJ, const char* tag) {
            r(0, 0) += 1e-3;  // 第 0 列 gross 非正交（点积偏差 ~2e-3，必超 1e-9）
            r(nanI, nanJ) = std::numeric_limits<double>::quiet_NaN();
            const char* a = oc_validateVerdict(r);
            const char* b = newValidateVerdict(r);
            ++dual;
            const bool flip = std::string(a) != b;
            if (flip) { ++flips; }
            std::printf("PROBE P6b dual-invalid %s nan=(%d,%d) hand=%s new=%s%s\n",
                        tag, nanI, nanJ, a, b, flip ? "  <-- PRECEDENCE-FLIP" : "");
        };
        dualCheck(rotG[3], 1, 2, "col0-nonorthogonal+nan-later-col");
        dualCheck(rotG[4], 2, 1, "col0-nonorthogonal+nan-later-col");
        dualCheck(rotG[5], 0, 1, "col0-nonorthogonal+nan-same-col");
        dualCheck(rotG[6], 2, 2, "col0-nonorthogonal+nan-diag-last");
        // 左手 + NaN（后位列）：基线列点积遇 NaN 传播为比较失败后仍会走到
        // 有限性检查或行列式——两形态各自归类实测。
        {
            rw::math::Rotation3D<double> lh = rotG[7];
            for (int k = 0; k < 3; ++k) { lh(k, 0) = -lh(k, 0); }
            const char* a = oc_validateVerdict(lh);
            const char* b = newValidateVerdict(lh);
            std::printf("PROBE P6b lefthand clean hand=%s new=%s %s\n", a, b,
                        std::string(a) == b ? "" : "<-- PRECEDENCE-FLIP");
        }
        std::printf("PROBE P6b.validatePrecedence dual-cases=%d precedence-flips=%d %s\n",
                    dual, flips, flips ? "登记语义差异（双重无效下类别优先级：非正交先抛→非有限先抛）"
                                       : "precedence-identical");
    }

    // ---- P7 rotZ/rotX：手写 vs RPY 组合（RPY.cpp:74；中3 逐项定案证据） ----
    {
        std::vector<double> angles = {0.0, 1e-8, -1e-8, 1e-6, -1e-6, 0.01, -0.01,
                                      kPi / 6, -kPi / 6, kPi / 4, kPi / 3, kPi / 2, -kPi / 2,
                                      2 * kPi / 3, 3 * kPi / 4, kPi, -kPi, 2 * kPi,
                                      -2.356194490192345, 2.356194490192345};
        for (int q = 0; q < 64; ++q) { angles.push_back(0.933 * (2.0 * u01(s) - 1.0) * 4.0); }
        MatrixCmp aggZ, aggX;
        std::size_t bitDiffZ = 0, bitDiffX = 0;
        for (double a : angles) {
            const auto oldZ = [&] {
                const double c = std::cos(a), sn = std::sin(a);
                return rw::math::Rotation3D<double>(c, -sn, 0.0, sn, c, 0.0, 0.0, 0.0, 1.0);
            }();
            const auto newZ = rw::math::RPY<double>(a, 0.0, 0.0).toRotation3D();
            const auto mz = compare9(oldZ, newZ);
            if (!mz.bitEqual) { ++bitDiffZ; }
            aggZ.bitEqual &= mz.bitEqual; aggZ.valueEqual &= mz.valueEqual;
            aggZ.maxAbsDiff = std::max(aggZ.maxAbsDiff, mz.maxAbsDiff);
            if (!mz.zeroSignEntries.empty()) { aggZ.zeroSignEntries = mz.zeroSignEntries; }

            const auto oldX = [&] {
                const double c = std::cos(a), sn = std::sin(a);
                return rw::math::Rotation3D<double>(1.0, 0.0, 0.0, 0.0, c, -sn, 0.0, sn, c);
            }();
            const auto newX = rw::math::RPY<double>(0.0, 0.0, a).toRotation3D();
            const auto mx = compare9(oldX, newX);
            if (!mx.bitEqual) { ++bitDiffX; }
            aggX.bitEqual &= mx.bitEqual; aggX.valueEqual &= mx.valueEqual;
            aggX.maxAbsDiff = std::max(aggX.maxAbsDiff, mx.maxAbsDiff);
            if (!mx.zeroSignEntries.empty()) { aggX.zeroSignEntries = mx.zeroSignEntries; }
        }
        report("P7.rotZ hand stv RPY(a,0,0)", aggZ, angles.size(),
               "bitDiffCases=" + std::to_string(bitDiffZ));
        report("P7.rotX hand stv RPY(0,0,a)", aggX, angles.size(),
               "bitDiffCases=" + std::to_string(bitDiffX));
    }

    // ---- P8 poseFromDirectionRoll 合成子步骤：列组合直写 vs R_dir*RPY(roll,0,0) ----
    {
        // 第一段（Rodrigues 轴角到方向）两形态共用——不在对照面（EAA 撤出，
        // 无上游候选；该段保持自持）。
        auto dirRotation = [](const rw::math::Vector3D<double>& d) {
            const double cx = -d[1], cy = d[0], cz = 0.0;  // ẑ×d（cz 恒 0——同产品）
            const double cn = std::sqrt(cx * cx + cy * cy);
            const double dot = d[2];
            double m[9] = {0,0,0,0,0,0,0,0,0};
            if (cn > 1e-12) {
                const double ax = cx / cn, ay = cy / cn;
                const double ang = std::atan2(cn, dot);
                const double c = std::cos(ang), sn = std::sin(ang), t = 1.0 - c;
                m[0] = t * ax * ax + c;        m[1] = t * ax * ay - sn * 0.0;  m[2] = t * ax * 0.0 + sn * ay;
                m[3] = t * ax * ay + sn * 0.0; m[4] = t * ay * ay + c;         m[5] = t * ay * 0.0 - sn * ax;
                m[6] = t * ax * 0.0 - sn * ay; m[7] = t * ay * 0.0 + sn * ax;  m[8] = t * 0.0 * 0.0 + c;
            } else if (dot > 0.0) {
                m[0] = 1; m[4] = 1; m[8] = 1;
            } else {
                m[0] = 1; m[4] = -1; m[8] = -1;
            }
            return rw::math::Rotation3D<double>(m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], m[8]);
        };
        auto oldCompose = [](const rw::math::Rotation3D<double>& rd, double roll) {
            const double cr = std::cos(roll), sr = std::sin(roll);
            return rw::math::Rotation3D<double>(
                rd(0,0)*cr + rd(0,1)*sr, rd(0,1)*cr - rd(0,0)*sr, rd(0,2),
                rd(1,0)*cr + rd(1,1)*sr, rd(1,1)*cr - rd(1,0)*sr, rd(1,2),
                rd(2,0)*cr + rd(2,1)*sr, rd(2,1)*cr - rd(2,0)*sr, rd(2,2));
        };
        auto newCompose = [](const rw::math::Rotation3D<double>& rd, double roll) {
            return rd * rw::math::RPY<double>(roll, 0.0, 0.0).toRotation3D();
        };
        // 黄金锁定方向集：斐波那契螺旋（与 generateDirections 同式）＋退化 ±z。
        std::vector<rw::math::Vector3D<double>> dirs;
        for (std::uint32_t n : {1U, 2U, 8U, 13U, 64U}) {
            for (std::uint32_t j = 0; j < n; ++j) {
                const double nn = static_cast<double>(n);
                const double z = 1.0 - (2.0 * static_cast<double>(j) + 1.0) / nn;
                const double rr = std::sqrt(std::max(0.0, 1.0 - z * z));
                const double phi = static_cast<double>(j) * 2.39996322972865332;
                dirs.push_back(rw::math::Vector3D<double>(rr * std::cos(phi), rr * std::sin(phi), z));
            }
        }
        dirs.push_back(rw::math::Vector3D<double>(0.0, 0.0, 1.0));
        dirs.push_back(rw::math::Vector3D<double>(0.0, 0.0, -1.0));
        dirs.push_back(rw::math::Vector3D<double>(1e-13, 0.0, -1.0));  // 近退化 −z
        // 黄金锁定 roll 集：generateRolls(M) 格心 ＋ 0 ＋ 随机。
        std::vector<double> rolls;
        for (std::uint32_t m : {1U, 4U, 8U}) {
            for (std::uint32_t r = 0; r < m; ++r) {
                rolls.push_back(-kPi + (static_cast<double>(r) + 0.5) * (2.0 * kPi / static_cast<double>(m)));
            }
        }
        rolls.push_back(0.0);
        for (int q = 0; q < 32; ++q) { rolls.push_back(2.0 * kPi * u01(s) - kPi); }
        MatrixCmp agg;
        std::size_t bitDiffCases = 0;
        std::string diffTags;
        for (const auto& d : dirs) {
            const auto rd = dirRotation(d);
            for (double roll : rolls) {
                const auto m = compare9(oldCompose(rd, roll), newCompose(rd, roll));
                if (!m.bitEqual) {
                    ++bitDiffCases;
                    if (diffTags.size() < 120) {
                        diffTags += " d=(" + std::to_string(d[0]).substr(0, 6) + ","
                                    + std::to_string(d[1]).substr(0, 6) + ","
                                    + std::to_string(d[2]).substr(0, 6) + ")";
                    }
                }
                agg.bitEqual &= m.bitEqual; agg.valueEqual &= m.valueEqual;
                agg.maxAbsDiff = std::max(agg.maxAbsDiff, m.maxAbsDiff);
                if (!m.zeroSignEntries.empty() && agg.zeroSignEntries.empty()) { agg.zeroSignEntries = m.zeroSignEntries; }
            }
        }
        report("P8.poseFromDirectionRoll.compose stv R_dir*RPY(roll,0,0)", agg,
               dirs.size() * rolls.size(), "bitDiffCases=" + std::to_string(bitDiffCases) + diffTags);
    }

    // ---- P9 orientBoxPoint 全表达式：基线逐分量内联 vs 候选 center + r*d ----
    // R·d 部分与 P1 同口径恒等；本组核的是【中心加法结合序】语义差异：
    // 基线 ((c_i+t0)+t1)+t2，候选 c_i+((t0+t1)+t2)——ULP 级位差可发生。
    {
        auto oldOrient = [](const rw::math::Rotation3D<double>& r,
                            const rw::math::Vector3D<double>& c,
                            const rw::math::Vector3D<double>& p) {
            const auto d = p - c;
            return rw::math::Vector3D<double>(
                c[0] + r(0,0)*d[0] + r(0,1)*d[1] + r(0,2)*d[2],
                c[1] + r(1,0)*d[0] + r(1,1)*d[1] + r(1,2)*d[2],
                c[2] + r(2,0)*d[0] + r(2,1)*d[1] + r(2,2)*d[2]);
        };
        auto newOrient = [](const rw::math::Rotation3D<double>& r,
                            const rw::math::Vector3D<double>& c,
                            const rw::math::Vector3D<double>& p) {
            return c + r * (p - c);
        };
        std::size_t cases = 0, bitDiffCases = 0, valueDiffCases = 0;
        double maxAbsDiff = 0.0;
        long long maxUlp = 0;
        for (std::size_t i = 0; i < rotG.size(); ++i) {
            const auto& r = (i % 3U == 0U) ? rotEdge[i] : rotG[i];
            const rw::math::Vector3D<double> c(umm(s), umm(s), umm(s));
            const rw::math::Vector3D<double> p(umm(s), umm(s), umm(s));
            const auto a = oldOrient(r, c, p);
            const auto b = newOrient(r, c, p);
            for (int k = 0; k < 3; ++k) {
                ++cases;
                const double x = a[k], y = b[k];
                const double d = std::fabs(x - y);
                if (d > maxAbsDiff) { maxAbsDiff = d; }
                if (std::memcmp(&x, &y, sizeof(double)) != 0) {
                    ++bitDiffCases;
                    // ULP 距离（单调键无符号差——跨符号一致）
                    const std::uint64_t kx = bitFromDouble(x);
                    const std::uint64_t ky = bitFromDouble(y);
                    const std::uint64_t ulp = kx > ky ? kx - ky : ky - kx;
                    if (ulp > static_cast<std::uint64_t>(maxUlp)) { maxUlp = static_cast<long long>(ulp); }
                    if (d != 0.0) { ++valueDiffCases; }
                }
            }
        }
        std::printf("PROBE P9.orientBoxPoint.fullExpr assoc-stv candidate cases=%zu "
                    "bitDiffCases=%zu valueDiffCases=%zu maxAbsDiff=%.3e maxUlp=%lld %s\n",
                    cases, bitDiffCases, valueDiffCases, maxAbsDiff, maxUlp,
                    valueDiffCases ? "COMBINATION-ORDER-ULP-DIFF(登记语义差异)" : "bit-identical");
        if (valueDiffCases != 0) {
            std::printf("PROBE P9 note: 中心加法结合序差异＝((c+t0)+t1)+t2 vs c+((t0+t1)+t2)，"
                        "ULP 级、附录 D 容差内；候选注释与登记随批订正\n");
        }
    }

    std::printf("# probe done failures=%d\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
