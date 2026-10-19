/**
 * @file residual_probe.cpp
 * @brief 本轮只读复核：以TCP在轴心的真实FK/IK隔离姿态残差，避免位置误差替代姿态纠错。
 * 需求REQ-09/A-16；纯测试产物，不改产品源码。使用固定模型、单初值与200次预算。
 */
#include "KinFkFixture.hpp"
#include <sdurws/ird/kinematics/Ik.hpp>
#include <sdurws/ird/testkit/gtest/RecordListener.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>
#include <QCoreApplication>
#include <gtest/gtest.h>
using namespace sdurws::ird::kinematics;
using namespace sdurws::ird::kinematics::testfixture;
using R=rw::math::Rotation3D<double>;
using T=rw::math::Transform3D<double>;
R rx(double a){return R(1,0,0,0,cos(a),-sin(a),0,sin(a),cos(a));}
T rotation(const R& r){return T(rw::math::Vector3D<double>(0,0,0),r);}
/** @brief 真实单轴快照请求；target在基座系，种子rad，默认全六分量且允许工具轴滚转。 */
IkRequest request(const TestView& view,const T& target,double seed) {
 IkRequest r; r.modelView=&view; r.targetInBase=target;
 r.tcp.toolObject=idFrom<core::ObjectId>("kin-tool");
 r.initialValues={{seed}}; r.iterationLimit=200;
 r.intervals=evaluationIntervals(view); r.referenceQ={0};
 r.targetRef.pointOid=idFrom<core::ObjectId>("kin-point-1");
 r.requestIdentity.snapshotId.bytes=digestOf("kin-ik-snap");
 r.requestIdentity.sliceId.bytes=digestOf("kin-ik-slice");
 r.taskConstraint=TaskPoseConstraint{}; r.taskConstraint->allowToolRoll=true; return r;
}
TEST(RequirementsResidualReview, NarrowRollOutsideSeedMustConverge_REQ09) {
 IRD_TEST_INFO("REQ-09", {}, std::nullopt);
 TestView view(makeModel({revolute("z",{0,0,1},trans(0,0,0),-3,3)},trans(0,0,0)));
 auto r=request(view,trans(0,0,0),.5);r.taskConstraint->rollRange={-.2,.2};
 const auto out=IkSolver().solve(r);
 ASSERT_FALSE(out.solutionSet.solutions.empty())<<"q=0为已知合法解；唯一初值0.5应由姿态残差拉回，而非依赖TCP偏置的位置项";
 EXPECT_LE(std::fabs(out.solutionSet.solutions.front().q[0]),.2+1e-6);
}
TEST(RequirementsResidualReview, OppositeToolAxisMustBeRejected_REQ09) {
 IRD_TEST_INFO("REQ-09", {}, std::nullopt);
 TestView view(makeModel({revolute("z",{0,0,1},trans(0,0,0),-.01,.01)},trans(0,0,0)));
 // 实际工具+Z恒为+Z，目标Rx(pi)要求-Z，任何允许Z滚转都不能改变此事实。
 const auto out=IkSolver().solve(request(view,rotation(rx(3.14159265358979323846)),0));
 EXPECT_TRUE(out.solutionSet.solutions.empty())<<"方向相反180度不得判为零姿态残差；返回解数="<<out.solutionSet.solutions.size();
}
TEST(RequirementsResidualReview, OppositeToolAxisFullPoseControl_REQ09) {
 IRD_TEST_INFO("REQ-09", {}, std::nullopt);
 TestView view(makeModel({revolute("z",{0,0,1},trans(0,0,0),-.01,.01)},trans(0,0,0)));
 auto r=request(view,rotation(rx(3.14159265358979323846)),0);r.taskConstraint.reset();
 EXPECT_TRUE(IkSolver().solve(r).solutionSet.solutions.empty());
}
TEST(RequirementsResidualReview, TiltedDirectionNeedsActualCorrection_REQ09) {
 IRD_TEST_INFO("REQ-09", {}, std::nullopt);
 TestView view(makeModel({revolute("x",{1,0,0},trans(0,0,0),-1,1)},trans(0,0,0)));
 // 目标Rx(0.2)，唯一初值0，已知q=0.2真实可达；沿X修正工具轴，而非只筛选已满足初值。
 const auto out=IkSolver().solve(request(view,rotation(rx(.2)),0));
 ASSERT_FALSE(out.solutionSet.solutions.empty())<<"允许滚转的方向任务仍需从不满足的初值纠正倾斜方向";
 EXPECT_NEAR(out.solutionSet.solutions.front().q[0],.2,1e-3);
}
int main(int argc,char** argv){QCoreApplication app(argc,argv);::testing::InitGoogleTest(&argc,argv);sdurws::ird::testkit::installTestRecordListener(argc,argv);return RUN_ALL_TESTS();}

