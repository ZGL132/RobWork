#include <rw/kinematics/FixedFrame.hpp>
#include <rw/kinematics/StateStructure.hpp>
#include <rw/math/RPY.hpp>
#include <rw/math/EAA.hpp>
#include <rw/math/Q.hpp>
#include <rw/models/RevoluteJoint.hpp>
#include <rw/models/SerialDevice.hpp>
#include <rw/invkin/JacobianIKSolver.hpp>
#include <iostream>
#include <iomanip>
#include <cmath>
int main() {
 using namespace rw::kinematics;
 using namespace rw::math;
 using namespace rw::models;
 const auto actual=RPY<>(0.7,0.4,0.6).toRotation3D();
 const auto desired=RPY<>(0.7,0.4,1.2).toRotation3D();
 const RPY<> a(actual),d(desired);
 const EAA<> error=actual*EAA<>(inverse(actual)*desired);
 // Independent axis-angle identity: spatial log(Ra^-1*Rd) = Ra*X*deltaRoll.
 const Vector3D<> oracle(0.6*std::cos(0.7)*std::cos(0.4),0.6*std::sin(0.7)*std::cos(0.4),-0.6*std::sin(0.4));
 const double oracleError=(Vector3D<>(error[0],error[1],error[2])-oracle).norm2();
 StateStructure structure;
 auto* base=new FixedFrame("base",Transform3D<>());
 auto* joint=new RevoluteJoint("joint",Transform3D<>());
 auto* tcp=new FixedFrame("tcp",Transform3D<>());
 structure.addFrame(base,structure.getRoot()); structure.addFrame(joint,base); structure.addFrame(tcp,joint); joint->setActive(true);
 State state=structure.getDefaultState();
 auto device=rw::core::ownedPtr(new SerialDevice(base,tcp,"view",state));
 const Transform3D<> target(RPY<>(0,0,Pi).toRotation3D());
 // All Rz(q) preserve +Z while target Rx(pi) maps +Z to -Z: unreachable for every q.
 double maximumAxisResidual=0;
 for(double q : {-Pi,-1.0,0.0,1.0,Pi}) { device->setQ(Q(1,q),state); maximumAxisResidual=std::max(maximumAxisResidual,(device->baseTend(state).R().getCol(2)-Vector3D<>(0,0,1)).norm2()); }
 state=structure.getDefaultState();
 rw::invkin::JacobianIKSolver solver(device,state);
 const bool converged=solver.solveLocal(target,1e-6,state,2);
 const double residual=EAA<>(inverse(device->baseTend(state).R())*target.R()).angle();
 std::cout<<std::setprecision(17)<<"{\"yawDelta\":"<<d[0]-a[0]<<",\"pitchDelta\":"<<d[1]-a[1]<<",\"rollDelta\":"<<d[2]-a[2]<<",\"spatialError\":["<<error[0]<<","<<error[1]<<","<<error[2]<<"],\"oracleError\":"<<oracleError<<",\"yzNorm\":"<<std::hypot(error[1],error[2])<<",\"sampledZAxisResidual\":"<<maximumAxisResidual<<",\"targetZ\":"<<target.R()(2,2)<<",\"reportedConverged\":"<<(converged?"true":"false")<<",\"halfTurnResidualRad\":"<<residual<<"}"<<std::endl;
 return std::abs(d[0]-a[0])<1e-12 && std::abs(d[1]-a[1])<1e-12 && std::abs(d[2]-a[2]-0.6)<1e-12 && oracleError<1e-12 && std::hypot(error[1],error[2])>0.2 && maximumAxisResidual<1e-12 && std::abs(target.R()(2,2)+1)<1e-12 && converged && std::abs(residual-Pi)<1e-12 ? 0:1;
}
