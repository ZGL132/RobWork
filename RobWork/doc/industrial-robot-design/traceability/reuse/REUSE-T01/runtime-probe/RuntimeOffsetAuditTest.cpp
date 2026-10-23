/**
 * @file RuntimeOffsetAuditTest.cpp
 * @brief REUSE-T01 真实runtime编译器偏置审计；发现既有缺陷，不接入产品算法。
 * 依据：ARC-03、MDL-06、KIN-01；复用runtime现有夹具/编译入口/测试报告。
 * 单元内测试可读私有入口；不赋予kinematics构造WorkCell的产品权限。
 */
#include "CanonicalModelFixture.hpp"
#include "WorkCellCompiler.hpp"
#include <rw/math/EAA.hpp>
#include <rw/math/Q.hpp>
#include <rw/models/SerialDevice.hpp>
#include <gtest/gtest.h>

namespace {
/**
 * @brief 构建纯关节运动夹具；第一关节运动，第二转动关节位移为零。
 * @param type [in] 第一关节类型；只用于旋转或移动审计。
 * @param axis [in] 第一关节局部单位轴；坐标系为基座系，原点变换均恒等。
 * @return runtime原有合法夹具；offset为0.25 rad或m，线程私有、无共享状态。
 */
sdurws::ird::runtime::testfixture::Fixture fixture(
    sdurws::ird::runtime::JointType type, const rw::math::Vector3D<double>& axis)
{
    auto f = sdurws::ird::runtime::testfixture::minimalFixture();
    f.chain.joints[0].type = type;
    f.chain.joints[0].axis = axis;
    f.chain.joints[0].zeroOffset = 0.25;
    return f;
}
}

// ARC-03：旋转控制组。q_auth=offset+q_rw=0.375 rad，公开EAA作对照。
TEST(RuntimeOffsetAudit, RevoluteOffsetMatchesAuthoritativeCoordinate) {
    using namespace sdurws::ird::runtime;
    auto f = fixture(JointType::Revolute, rw::math::Vector3D<double>(0,0,1));
    const auto out = compileWorkCell(f.build());
    const auto device = out.workCell->findDevice<rw::models::SerialDevice>(out.deviceName);
    ASSERT_FALSE(device.isNull());
    auto state = out.workCell->getDefaultState();
    device->setQ(rw::math::Q(2,0.125,0.0),state);
    const auto actual = device->baseTend(state);
    const auto expected = rw::math::EAA<double>(0,0,0.375).toRotation3D();
    for(int r=0;r<3;++r) for(int c=0;c<3;++c) EXPECT_NEAR(actual.R()(r,c),expected(r,c),1e-12);
}

// ARC-03/KIN-01：移动offset单位m，权威q=0.375m，必须平移且不引入旋转。
// 本测试使用规范期望，若当前编译器失败即保存真实FAIL，不改黄金/容差洗绿。
TEST(RuntimeOffsetAudit, PrismaticOffsetMustTranslateWithoutRotation) {
    using namespace sdurws::ird::runtime;
    const rw::math::Vector3D<double> axes[] = {{1,0,0},{0,1,0},{0,0,1}};
    for(const auto& axis: axes) {
        SCOPED_TRACE(::testing::Message()<<"axis="<<axis);
        auto f = fixture(JointType::Prismatic,axis);
        const auto out = compileWorkCell(f.build());
        const auto device = out.workCell->findDevice<rw::models::SerialDevice>(out.deviceName);
        ASSERT_FALSE(device.isNull());
        auto state = out.workCell->getDefaultState();
        device->setQ(rw::math::Q(2,0.125,0.0),state);
        const auto actual = device->baseTend(state);
        for(int r=0;r<3;++r) {
            EXPECT_NEAR(actual.P()[r],axis[r]*0.375,1e-12);
            for(int c=0;c<3;++c) EXPECT_NEAR(actual.R()(r,c),r==c?1.0:0.0,1e-12);
        }
    }
}
