// MDL-CR-1：隔离真实 runtime 编译，正负移动偏置与 SI 限值同时存在。
#include "../../modeling-review-20261028/probe.cpp"
TEST(ModelingBatch1, MixedChainOffsetAndRatesReachRuntime) {
 for(double offset : {-.25, .25}) {
  auto ws=seed(); withBaseTool(ws);
  auto& j=ws.design.joints[0]; j.type=JointType::Prismatic;
  j.axis=provided(V(1,0,0)); j.origin=provided(JointPose{}); j.zeroOffset=offset;
  j.maxVelocity=provided(.3); j.maxAcceleration=provided(.4);
  auto snapshot=compile(ws); auto device=snapshot->workCellHandle()->getDevices().front();
  EXPECT_DOUBLE_EQ(device->getVelocityLimits()[0],.3);
  EXPECT_DOUBLE_EQ(device->getAccelerationLimits()[0],.4);
  for(double q0 : {-.1,0.,.2}) {
   std::vector<double> q(6,0); q[0]=q0;
   auto expected=baseOf(ws)*T(V(offset+q0,0,0),R::identity())*toolOf(ws);
   EXPECT_LE(error(actual(ws,q),expected),1e-9);
  }
 }
}
