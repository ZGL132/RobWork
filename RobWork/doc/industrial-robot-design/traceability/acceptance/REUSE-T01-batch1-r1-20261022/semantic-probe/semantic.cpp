#include <rw/kinematics/FixedFrame.hpp>
#include <rw/kinematics/StateStructure.hpp>
#include <rw/math/RPY.hpp>
#include <rw/math/EAA.hpp>
#include <rw/math/Q.hpp>
#include <rw/models/RevoluteJoint.hpp>
#include <rw/models/SerialDevice.hpp>
#include <iostream>
#include <cmath>
int main() {
 using namespace rw::kinematics;
 using namespace rw::math;
 using namespace rw::models;
 StateStructure structure;
 auto* base = new FixedFrame("base",Transform3D<>());
 auto* joint = new RevoluteJoint("joint",Transform3D<>());
 auto* tcp = new FixedFrame("tcp",Transform3D<>());
 structure.addFrame(base,structure.getRoot());
 structure.addFrame(joint,base);
 structure.addFrame(tcp,joint);
 joint->setActive(true);
 State state=structure.getDefaultState();
 SerialDevice device(base,tcp,"view",state);
 const Transform3D<> target(RPY<>(Pi,0,0).toRotation3D());
 device.setQ(Q(1,Pi),state);
 const double angle=EAA<>(inverse(device.baseTend(state).R())*target.R()).angle();
 std::cout << "{\"q\":" << Pi << ",\"targetReachableResidualRad\":" << angle << "}" << std::endl;
 return std::abs(angle)<1e-12 ? 0 : 1;
}
