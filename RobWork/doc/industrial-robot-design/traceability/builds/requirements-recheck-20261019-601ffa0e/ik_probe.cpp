#include "KinFkFixture.hpp"
#include <sdurws/ird/kinematics/Ik.hpp>
#include <iostream>
using namespace sdurws::ird::kinematics;
using namespace sdurws::ird::kinematics::testfixture;
using R=rw::math::Rotation3D<double>;
using T=rw::math::Transform3D<double>;
R rx(double a){return R(1,0,0,0,cos(a),-sin(a),0,sin(a),cos(a));}
R ry(double a){return R(cos(a),0,sin(a),0,1,0,-sin(a),0,cos(a));}
T rotation(const R& r){return T(rw::math::Vector3D<double>(0,0,0),r);}
void run(const char* name, const TestView& v, const T& target, double seed,
         std::optional<TaskPoseConstraint> c, double knownQ){
  IkRequest r; r.modelView=&v; r.targetInBase=target;
  r.tcp.toolObject=idFrom<core::ObjectId>("kin-tool");
  r.initialValues={{seed}}; r.iterationLimit=200;
  r.intervals=evaluationIntervals(v); r.referenceQ={0};
  r.targetRef.pointOid=idFrom<core::ObjectId>("kin-point-1");
  r.requestIdentity.snapshotId.bytes=digestOf("kin-ik-snap");
  r.requestIdentity.sliceId.bytes=digestOf("kin-ik-slice");
  r.taskConstraint=c;
  auto out=IkSolver().solve(r);
  std::cout<<name<<" known_valid_q="<<knownQ<<" solutions="<<out.solutionSet.solutions.size();
  if(!out.solutionSet.solutions.empty()) std::cout<<" solved_q="<<out.solutionSet.solutions.front().q[0];
  std::cout<<" outcome="<<int(out.outcomeKind)<<"\n";
}
int main(){
 TestView z(makeModel({revolute("z",{0,0,1},trans(0,0,0),-3,3)},trans(0,0,0)));
 TaskPoseConstraint c; c.allowToolRoll=true; c.rollRange={-.2,.2};
 run("narrow_roll_need_iteration",z,trans(0,0,0),.5,c,0);
 c.rollRange={.1,.2};
 run("positive_roll_valid_seed",z,trans(0,0,0),.15,c,.15);
 run("positive_roll_invalid_seed",z,trans(0,0,0),-.15,c,.15);
 TestView tilt(makeModel({revolute("tilt-z",{0,0,1},rotation(rx(.7)),.39,.41)},trans(0,0,0)));
 c.rollRange={-3.141592653589793,3.141592653589793};
 run("tilted_tool_free_roll",tilt,rotation(rx(.7)),.4,c,.4);
 TestView euler(makeModel({revolute("tilt-x",{1,0,0},rotation(ry(1.3)),-3,3)},trans(0,0,0)));
 c=TaskPoseConstraint{};
 run("euler_nonzero_pitch",euler,rotation(ry(1.3)*rx(.13)),0,c,.13);
 run("euler_control_no_constraint",euler,rotation(ry(1.3)*rx(.13)),0,std::nullopt,.13);
 TestView gimbal(makeModel({revolute("gimbal-z",{0,0,1},trans(0,0,0),-3,3)},rotation(ry(1.5707963267948966))));
 run("gimbal_positive_pitch",gimbal,rotation(rotZ(.3)*ry(1.5707963267948966)),0,c,.3);
 run("gimbal_control_no_constraint",gimbal,rotation(rotZ(.3)*ry(1.5707963267948966)),0,std::nullopt,.3);
 for(double p:{1.4,1.55,1.7,2.0,2.8}) {
  TestView scan(makeModel({revolute("scan-x",{1,0,0},rotation(ry(p)),-3,3)},trans(0,0,0)));
  std::cout<<"pitch="<<p<<" "; run("euler_scan",scan,rotation(ry(p)*rx(.13)),0,c,.13);
  std::cout<<"pitch="<<p<<" "; run("euler_scan_control",scan,rotation(ry(p)*rx(.13)),0,std::nullopt,.13);
 }
 TestView pitchYaw(makeModel({revolute("review-y",{0,1,0},rotation(rotZ(.7)),-3,3)},trans(0,0,0)));
 run("pitch_under_nonzero_yaw",pitchYaw,rotation(rotZ(.7)*ry(.13)),0,c,.13);
 run("pitch_under_nonzero_yaw_control",pitchYaw,rotation(rotZ(.7)*ry(.13)),0,std::nullopt,.13);
 TestView mask(makeModel({revolute("review-mask",{0,0,1},rotation(rotZ(.7)*ry(.3)*rx(.4)),-.01,.01)},trans(0,0,0)));
 c.constrainedDof={false,false,false,false,false,true};
 run("gimbal_yaw_only_already_satisfied",mask,rotation(rotZ(.7)*ry(1.5707963267948966)),0,c,0);
}
