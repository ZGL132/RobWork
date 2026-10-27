/** @file PolicySessionBenchmark.cpp
 * @brief POL-T13：独立微基准；只复用上游真实几何，解析盒区间给出预期。
 * 不设未经批准的性能阈值，记录实际耗时供独验；不是 GUI 性能结论。
 */
#include <sdurws/ird/policy/CollisionEvaluator.hpp>
#include <sdurws/ird/policy/CollisionQuery.hpp>
#include <sdurws/ird/policy/PolicyParsing.hpp>
#include <rw/models/WorkCell.hpp>
#include <rw/models/RevoluteJoint.hpp>
#include <rw/models/SerialDevice.hpp>
#include <rw/models/RigidObject.hpp>
#include <rw/kinematics/FixedFrame.hpp>
#include <rw/geometry/Box.hpp>
#include <rw/geometry/Geometry.hpp>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <map>
using namespace sdurws::ird;
/** @brief 测试名称端口，精确转发，不拼剥前缀；只读并发安全。 */
struct Names : policy::IPolicyNameContext {
    std::map<core::ObjectId,std::string> values;
    std::optional<core::ObjectId> tryObjectId(const std::string&) const override {return {};}
    std::optional<std::string> tryRuntimeName(core::ObjectId id) const override {return values.at(id);}
    core::ContentIdentity nameMapContentIdentity() const override {core::ContentIdentity x;x.bytes[0]=3;return x;}
};
/** @brief 基准调用上下文，在同步基准整个生命周期保持存活。 */
struct Context : policy::IPolicyCallContext {
    bool cancellationRequested() const override {return false;}
    bool alive() const override {return true;}
};
/** @brief 确定性非零夹具身份，无持久化用途。 */
core::ObjectId id(unsigned char x) {core::ObjectId v;v.bytes[0]=x;return v;}
/** @brief 构造独立场景，盒边长单位m、中心间隔0.3m；q=0时按区间判断相交。 */
policy::CollisionScene scene(double edge,unsigned char tag) {
    auto wc=std::make_shared<rw::models::WorkCell>("Bench");
    auto base=rw::core::ownedPtr(new rw::kinematics::FixedFrame("Base",rw::math::Transform3D<>::identity()));wc->addFrame(base);
    auto joint=rw::core::ownedPtr(new rw::models::RevoluteJoint("J1",rw::math::Transform3D<>::identity()));wc->addFrame(joint,base);
    auto tool=rw::core::ownedPtr(new rw::kinematics::FixedFrame("Tool",rw::math::Transform3D<>::identity()));wc->addFrame(tool,joint);
    auto env=rw::core::ownedPtr(new rw::kinematics::FixedFrame("Env",rw::math::Transform3D<>(rw::math::Vector3D<>(0,0,.3))));wc->addFrame(env);
    wc->addDevice(rw::core::ownedPtr(new rw::models::SerialDevice(base,tool,"Robot",wc->getDefaultState())));
    for(auto f:{rw::kinematics::Frame::Ptr(tool),rw::kinematics::Frame::Ptr(env)}) {
        auto o=rw::core::ownedPtr(new rw::models::RigidObject(f));
        o->addGeometry(rw::core::ownedPtr(new rw::geometry::Geometry(rw::core::ownedPtr(new rw::geometry::Box(edge,edge,edge)))));wc->add(o);
    }
    policy::CollisionScene s;s.workcell=wc;s.primaryDevice=id(1);s.sceneContentIdentity.bytes[0]=tag;
    s.objects={{id(2),"Tool",policy::SceneObjectRole::RobotLink,true},{id(3),"Env",policy::SceneObjectRole::EnvironmentObject,true}};return s;
}
/** @brief Release 实测1000次交替场景建会话并评价，再对单会话评价5000次；任一数值反例立即失败。 */
int main() {
    struct Validation : policy::IPolicyValidationContext {
        bool objectExists(core::ObjectId) const override {return true;}
        std::optional<std::string> objectRole(core::ObjectId) const override {return {};}
        bool groupDefined(std::string_view) const override {return false;}
    } validation;
    policy::RawPolicyInput raw;raw.policyObject=id(9);
    raw.collision.enabledDomains={policy::CollisionDomain::Environment};
    raw.collision.safetyClearance=policy::RawThresholdInput{0,"m"};
    auto parsed=policy::resolvePolicy(raw,validation);if(!parsed.policy)return 3;
    const auto& p=*parsed.policy;
    Names names;names.values={{id(1),"Robot"},{id(2),"Tool"},{id(3),"Env"}};Context ctx;
    const auto small=scene(.2,1),big=scene(.8,2);auto evaluator=policy::makeRobWorkCollisionEvaluator();
    policy::CollisionQuery q;q.configurations={rw::math::Q(1,0.0)};
    using Clock=std::chrono::steady_clock;std::vector<double> us;
    for(int i=0;i<1000;++i){auto start=Clock::now();auto session=evaluator->createSession(p,i%2?big:small,names);auto out=session->evaluate(q,ctx);
        if(!out.finalized||out.findings.size()!=std::size_t(i%2))return 1;
        us.push_back(std::chrono::duration<double,std::micro>(Clock::now()-start).count());}
    auto session=evaluator->createSession(p,big,names);auto start=Clock::now();
    for(int i=0;i<5000;++i){auto out=session->evaluate(q,ctx);if(!out.finalized||out.findings.size()!=1)return 2;}
    auto mean=std::chrono::duration<double,std::micro>(Clock::now()-start).count()/5000;
    std::sort(us.begin(),us.end());
    std::cout<<"{\"sessions\":1000,\"evaluations\":5000,\"failed\":0,\"create_and_evaluate_p50_us\":"<<us[500]
             <<",\"create_and_evaluate_p95_us\":"<<us[950]<<",\"evaluate_mean_us\":"<<mean<<"}\n";
}
