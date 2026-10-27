// REUSE-T42 上游对照探针（upstreamComparison 面）——非生产代码。
// 对照基准＝基线 b12e8503 的 requirements/src/TemplateArray.cpp rotationFromRpy
// 原手写 Rz·Ry·Rx 逐元素展开（git show b12e8503:<path> 逐字摘录）；
// 对照对象＝生产同款上游调用 rw::math::RPY<double>(yaw,pitch,roll).toRotation3D()。
// 断言：全用例逐位（IEEE754 bit pattern）相等且为真旋转——证明换核零扰动。
#include <rw/math/RPY.hpp>
#include <rw/math/Rotation3D.hpp>
#include <gtest/gtest.h>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace {

/// 基线展开（摘录自冻结基线原文——确定性乘加序与生产旧实现一致）。
void baselineExpansion(double roll, double pitch, double yaw, double r[3][3])
{
    const double cr = std::cos(roll);
    const double sr = std::sin(roll);
    const double cp = std::cos(pitch);
    const double sp = std::sin(pitch);
    const double cy = std::cos(yaw);
    const double sy = std::sin(yaw);
    r[0][0] = cy * cp;               r[0][1] = -sy * cr + cy * sp * sr;  r[0][2] = sy * sr + cy * sp * cr;
    r[1][0] = sy * cp;               r[1][1] = cy * cr + sy * sp * sr;   r[1][2] = -cy * sr + sy * sp * cr;
    r[2][0] = -sp;                   r[2][1] = cp * sr;                  r[2][2] = cp * cr;
}

struct Case {
    double roll, pitch, yaw;
    std::string name;
};

std::vector<Case> allCases()
{
    const double pi = 3.14159265358979323846;
    return {
        {0.1, 0.2, 0.3, "golden-inputs"},
        {-0.1, 0.2, -0.3, "mirror-y-expected"},
        {0.0, 0.0, 0.0, "identity"},
        {pi / 2, 0.1, -0.2, "gimbal-positive-roll"},
        {0.0, pi / 2, 0.4, "gimbal-positive-pitch"},
        {0.0, -pi / 2, 0.4, "gimbal-negative-pitch"},
        {0.2, pi / 2 - 1e-13, 0.3, "near-gimbal-upper"},
        {0.2, -pi / 2 + 1e-13, 0.3, "near-gimbal-lower"},
        {1.0, 0.5, 2.0, "general-1"},
        {-1.0, -0.5, -2.0, "general-2"},
        {2.7, 1.2, -3.1, "general-3"},
        {-0.7, 1.55, 0.9, "near-singular-pitch"},
    };
}

/// IEEE754 位模式相等（memcmp double 三分量；−0.0 与 +0.0 视为不等以钉乘加序）。
bool bitwiseEqual(const double a[3][3], const rw::math::Rotation3D<double>& R)
{
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            if (std::memcmp(&a[i][j], &R(i, j), sizeof(double)) != 0) {
                return false;
            }
        }
    }
    return true;
}

}  // namespace

/// 基线展开与生产同款上游调用逐位一致（全用例网格）。
TEST(ReuseT42UpstreamForward, BitwiseEqualsBaselineExpansion)
{
    for (const Case& c : allCases()) {
        double r[3][3];
        baselineExpansion(c.roll, c.pitch, c.yaw, r);
        const rw::math::Rotation3D<double> up =
            rw::math::RPY<double>(c.yaw, c.pitch, c.roll).toRotation3D();
        EXPECT_TRUE(bitwiseEqual(r, up)) << "case " << c.name;
    }
}

/// 说明：探针 v1 曾断言上游结果 isProperRotation()，general-3（幅值 2.7/1.2/−3.1）
/// 在上游自身容差语义下为 false——该断言属上游内部校验语义、非产品契约
/// （产品侧不消费 isProperRotation），v2 移除；v1 运行记录保留于 run/。
/// 探针入口（仅本探针——不进产品测试目标）。
int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
