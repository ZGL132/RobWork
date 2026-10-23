/** @file Probe.cpp
 * @brief REUSE-T09：真实compileWorkCell移动关节offset反例；复用既有最小Canonical夹具。
 * 只测原产品库，无替身/新算法；0.4m沿局部Z平移，不能被当成0.4rad旋转。
 */
#include "WorkCellCompiler.hpp"
#include "CanonicalModelFixture.hpp"
#include <rw/models/WorkCell.hpp>
#include <rw/models/SerialDevice.hpp>
#include <rw/models/PrismaticJoint.hpp>
#include <rw/math/Rotation3D.hpp>
#include <gtest/gtest.h>
using namespace sdurws::ird::runtime;
/** @brief MDL-11/RT-EQ-1：默认态应已嵌入0.4m零偏，断言为独立长度值。 */
TEST(RuntimeOffset, PrismaticTranslation_MDL11_REUSE_T09) {
 auto f=testfixture::minimalFixture();
 f.chain.joints[0].type=JointType::Prismatic;
 f.chain.joints[0].axis=rw::math::Vector3D<>(0,0,1);
 f.chain.joints[0].zeroOffset=0.4;
 for(auto& joint:f.chain.joints)joint.origin=detail::identityTransform3D();
 f.chain.joints[1].zeroOffset=0;
 const auto model=f.build();const auto out=compileWorkCell(model);
 const auto device=out.workCell->findDevice<rw::models::SerialDevice>(out.deviceName);
 ASSERT_FALSE(device.isNull());
 const auto pose=device->baseTend(out.workCell->getDefaultState());
 ASSERT_NEAR(pose.P()[2],0.4,1e-9) << "移动关节零偏单位为m，应向Z平移，不得变成旋转";
 EXPECT_NEAR(pose.P()[0],0,1e-9);EXPECT_NEAR(pose.P()[1],0,1e-9);
}
/** @brief 上游真实关节在相同零偏下直接给出平移，不需自写Rodrigues。 */
TEST(RuntimeOffset, UpstreamPrismaticTransform_MDL11_REUSE_T09) {
 rw::models::PrismaticJoint joint("upstream",rw::math::Transform3D<>::identity());
 const auto pose=joint.getTransform(0.4);
 EXPECT_DOUBLE_EQ(pose.P()[2],0.4);
 EXPECT_EQ(pose.R(),rw::math::Rotation3D<>::identity());
}
