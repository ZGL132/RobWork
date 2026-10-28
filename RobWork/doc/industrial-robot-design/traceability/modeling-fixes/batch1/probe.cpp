// 本轮隔离评审探针：只调用当前产品库；解析 DH oracle 仅存在此临时文件。
#include "test/BridgeFixtures.hpp"
#include <sdurws/ird/modeling/Template.hpp>
#include <sdurws/ird/modeling/StructureEdit.hpp>
#include <sdurws/ird/modeling/PropertyEstimation.hpp>
#include <sdurws/ird/modeling/DhConvert.hpp>
#include <sdurws/ird/modeling/Import.hpp>
#include <sdurws/ird/modeling/WorkCellReverseImport.hpp>
#include <sdurws/ird/modeling/ModelDiff.hpp>
#include <sdurws/ird/modeling/Package.hpp>
#include "plugin/ModelingCommandFlows.hpp"
#include <sdurws/ird/runtime/Compiler.hpp>
#include <sdurws/ird/runtime/Snapshot.hpp>
#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/testkit/gtest/RecordListener.hpp>
#include <rw/models/Device.hpp>
#include <rw/models/WorkCell.hpp>
#include <rw/models/Object.hpp>
#include <rw/geometry/Geometry.hpp>
#include <rw/math/RPY.hpp>
#include <rw/math/EAA.hpp>
#include <rw/math/Q.hpp>
#include <rw/kinematics/Kinematics.hpp>
#include <iostream>
#include <iomanip>
namespace m=sdurws::ird::modeling;
namespace r=sdurws::ird::runtime;
namespace c=sdurws::ird::core;
using T=rw::math::Transform3D<double>;
using R=rw::math::Rotation3D<double>;
using V=rw::math::Vector3D<double>;
using namespace m;
using namespace m::testbridge;
template<class X> auto provided(X x){return c::SourcedValue<X>::provided(x,userProv());}
struct Input final: r::IObjectBytesSource,r::IRevisionClosureSource,m::ObjectClosureView {
 struct Entry { c::ObjectId id;std::string token;std::vector<uint8_t> bytes;c::Digest256 hash; };
 std::vector<Entry> entries;
 c::ProjectId project=c::ProjectId::generate();c::BranchId branch=c::BranchId::generate();c::RevisionId rev=c::RevisionId::generate();
 void add(c::ObjectId id,std::string token,std::vector<uint8_t> bytes){c::ContentDigester d;d.update(bytes.data(),bytes.size());entries.push_back({id,token,bytes,d.finalize()});}
 std::optional<std::vector<uint8_t>> tryObjectBytes(c::ObjectId id,c::ContentVersion cv) const override {for(auto& e:entries)if(e.id==id&&e.hash==cv.bytes)return e.bytes;return {};}
 std::optional<r::RevisionSummary> tryRevision(c::RevisionId id) const override {if(id!=rev)return {};r::RevisionSummary s;s.id=rev;s.seq=1;s.branch=branch;for(auto& e:entries){c::ContentVersion cv;cv.bytes=e.hash;s.objectRefs.push_back({e.id,cv,e.token,e.hash});}return s;}
 bool objectInRevision(c::RevisionId revId,c::ObjectId id,c::ContentVersion cv) const override{return revId==rev&&tryObjectBytes(id,cv).has_value();}
 std::optional<m::ClosureObject> tryObject(c::ObjectId const& id) const override{for(auto& e:entries)if(e.id==id)return m::ClosureObject{e.token,e.bytes};return {};}
 std::optional<m::ClosureObject> tryObjectByToken(std::string_view token) const override{for(auto& e:entries)if(e.token==token)return m::ClosureObject{e.token,e.bytes};return {};}
};
struct DescReader final:r::IRobotDesignReader{
 r::RobotDesignDescription desc;
 explicit DescReader(r::RobotDesignDescription d):desc(std::move(d)){}
 r::Expected<r::RobotDesignDescription,r::RuntimeError> read(const std::vector<uint8_t>&,uint32_t)const override{return r::Expected<r::RobotDesignDescription,r::RuntimeError>::ok(desc);}
};
Input inputOf(const ModelingWorkingSet& ws){Input in;in.add(c::ObjectId::generate(),r::kRobotDesignObjectType,encode(ws.design));for(auto& t:ws.toolObjects)in.add(t.objectId,std::string(m::kToolDefinitionObjectType),encode(t));return in;}
r::CompileRequest requestOf(const Input& in,const r::IRobotDesignReader& reader){r::CompileRequest req;req.project=in.project;req.revision=in.rev;req.objects=&in;req.closure=&in;req.designReader=&reader;req.options.requestDynamicWorkCell=false;return req;}
struct ProductionCompileProbe final:CompileProbe{
 r::Expected<r::CanonicalModel,r::RuntimeError> buildCanonicalModel(const r::RobotDesignDescription& d)const override{
  ModelingWorkingSet ws;RobotDesignTemplateFactory tf;std::vector<c::DiagnosticRecord> diags;ws=tf.createDraft(TemplateId{kTemplateIdGeneric6R},r::InstallationPresetToken::Ground,"probe",diags).get();
  Input in=inputOf(ws);DescReader reader(d);return r::createCanonicalModelCompiler()->buildCanonicalModel(requestOf(in,reader));
 }
};
std::shared_ptr<const r::RuntimeSnapshot> compile(const ModelingWorkingSet& ws){Input in=inputOf(ws);RobotDesignReader reader(&in);auto outcome=r::createCanonicalModelCompiler()->compile(requestOf(in,reader));if(outcome.status!=r::CompileStatus::Published)throw std::runtime_error("production compile failed");return outcome.snapshot;}
ModelingWorkingSet seed(){RobotDesignTemplateFactory f;std::vector<c::DiagnosticRecord>d;return f.createDraft(TemplateId{kTemplateIdGeneric6R},r::InstallationPresetToken::Ground,"probe",d).get();}
T actual(const ModelingWorkingSet& ws,const std::vector<double>& q){auto s=compile(ws);auto wc=s->workCellHandle();auto device=wc->getDevices().front();auto state=wc->getDefaultState();device->setQ(rw::math::Q(q),state);auto end=device->getEnd();if(!ws.toolObjects.empty()){bool found=false;for(const auto& e:s->nameMap().entries()){if(e.scope==r::NameScope::Tcp){end=wc->findFrame(e.fullName);found=end!=nullptr;break;}}if(!found)throw std::runtime_error("probe TCP missing from production nameMap");}return rw::kinematics::Kinematics::worldTframe(end,state);}
double error(const T&a,const T&b){double re=0;for(int i=0;i<3;++i)for(int j=0;j<3;++j)re=std::max(re,std::abs(a.R()(i,j)-b.R()(i,j)));double pe=(a.P()-b.P()).norm2();std::cout<<" translationError_m="<<pe<<" rotationElementError="<<re;return std::max(pe,re);}
// 独立标准 DH 齐次矩阵，非产品展开 helper，不对末端调整组合顺序。
T oracleDh(double theta,double d,double a,double alpha){double ct=cos(theta),st=sin(theta),ca=cos(alpha),sa=sin(alpha);return T(V(a*ct,a*st,d),R(ct,-st*ca,st*sa,st,ct*ca,-ct*sa,0,sa,ca));}
void withBaseTool(ModelingWorkingSet& ws){ws.design.basePlacement.preset=r::InstallationPresetToken::Custom;ws.design.basePlacement.customEaa=provided(V(.2,-.3,.4));ws.design.basePlacement.basePosition=provided(V(.4,-.2,.7));ToolDefinition t;t.objectId=c::ObjectId::generate();t.localName="gripper";t.mountInterface=T(V(.03,-.04,.08),rw::math::RPY<double>(.2,-.1,.3).toRotation3D());t.tcpList.push_back({"tip",T(V(.04,.02,.12),rw::math::RPY<double>(-.1,.2,-.2).toRotation3D()),""});ws.toolObjects={t};ws.design.toolRefs={t.objectId};ws.design.defaultTcp=TcpRef{t.objectId,"tip"};}
T baseOf(const ModelingWorkingSet& ws){return T(ws.design.basePlacement.basePosition.value(),rw::math::EAA<double>(ws.design.basePlacement.customEaa.value()).toRotation3D());}
T toolOf(const ModelingWorkingSet& ws){return ws.toolObjects[0].mountInterface*ws.toolObjects[0].tcpList[0].offset;}
TEST(ModelingReview,DhThroughActualRuntimeVsAnalyticNonzeroQ){
 for(int n:{1,2,6,7}){auto ws=seed();ws.design.joints.resize(n,ws.design.joints.back());ws.design.links.resize(n+1,ws.design.links.back());
  ws.design.authority=AuthorityMode::StandardDH;withBaseTool(ws);
  for(int i=0;i<n;++i){auto& j=ws.design.joints[i];j.objectId=c::ObjectId::generate();j.localName="j"+std::to_string(i+1);j.dhDerived=DhParameters{.2+.07*i,.1+.02*i,.15+.03*i,(i%2?-.4:.6)};j.zeroOffset=(i%2?-.12:.17);j.axis=c::SourcedValue<V>::notProvided();j.origin=c::SourcedValue<JointPose>::notProvided();j.bounds=provided(JointLimits{-3,3});}
  for(int i=0;i<n+1;++i){ws.design.links[i].objectId=c::ObjectId::generate();ws.design.links[i].localName="l"+std::to_string(i);}
  for(double scale:{0.,.5,-.7}){std::vector<double> q(n);T expected=baseOf(ws);for(int i=0;i<n;++i){q[i]=scale*(i+1)/n;auto& j=ws.design.joints[i];auto d=*j.dhDerived;expected=expected*oracleDh(d.thetaOffset+j.zeroOffset+q[i],d.d,d.a,d.alpha);}expected=expected*toolOf(ws);auto got=actual(ws,q);double e=error(got,expected);std::cout<<"DH n="<<n<<" scale="<<scale<<" error="<<e<<" expectedP="<<expected.P()<<" actualP="<<got.P()<<"\n";EXPECT_LE(e,1e-8);}
 }
}
TEST(ModelingReview,PrismaticOffsetMustTranslateNotRotate){auto ws=seed();withBaseTool(ws);auto& j=ws.design.joints[0];j.type=JointType::Prismatic;j.axis=provided(V(1,0,0));j.origin=provided(JointPose{});j.zeroOffset=.25;j.maxVelocity=c::SourcedValue<double>::notProvided();j.maxAcceleration=c::SourcedValue<double>::notProvided();for(auto& x:ws.design.joints){x.maxVelocity=c::SourcedValue<double>::notProvided();x.maxAcceleration=c::SourcedValue<double>::notProvided();}
 for(double q0:{0.,.2,-.1}){std::vector<double> q(6,0);q[0]=q0;auto got=actual(ws,q);auto expected=baseOf(ws)*T(V(.25+q0,0,0),R::identity())*toolOf(ws);std::cout<<"Prismatic q="<<q0<<" error="<<error(got,expected)<<"\n";EXPECT_LE(error(got,expected),1e-8);}}
TEST(ModelingReview,PrismaticVelocityAndAccelerationAreAccepted){for(bool acceleration:{false,true}){auto ws=seed();auto& j=ws.design.joints[0];j.type=JointType::Prismatic;j.maxVelocity=c::SourcedValue<double>::notProvided();j.maxAcceleration=c::SourcedValue<double>::notProvided();(acceleration?j.maxAcceleration:j.maxVelocity)=provided(.3);auto in=inputOf(ws);std::vector<c::DiagnosticRecord>d;auto desc=CanonicalModelInputBuilder{}.build(in,d);if(!desc.ok())std::cout<<"linear limit rejected: "<<desc.error().detail<<"\n";EXPECT_TRUE(desc.ok());}}
TEST(ModelingReview,StandardDhPreservesJointRateLimits){auto ws=seed();ws.design.authority=AuthorityMode::StandardDH;for(auto& j:ws.design.joints){j.dhDerived=DhParameters{};j.axis=c::SourcedValue<V>::notProvided();j.origin=c::SourcedValue<JointPose>::notProvided();}auto in=inputOf(ws);std::vector<c::DiagnosticRecord>d;auto desc=CanonicalModelInputBuilder{}.build(in,d);ASSERT_TRUE(desc.ok());EXPECT_TRUE(desc.get().joints[0].maxVelocity.tryValue().has_value());EXPECT_TRUE(desc.get().joints[0].maxAcceleration.tryValue().has_value());}
TEST(ModelingReview,AuthoritySwitchMustPreserveNonzeroQAxis){auto ws=seed();for(auto& j:ws.design.joints)j.axis=provided(V(1,0,0));ProductionCompileProbe p;std::vector<c::DiagnosticRecord>d;auto decision=prepareAuthoritySwitch(ws,ws,DhExplicitConverter{},p,d);std::cout<<"authority allowed="<<decision.allowed<<" determination="<<dhDeterminationToken(decision.determination)<<"\n";ASSERT_TRUE(decision.allowed);std::vector<double>q(6,0);q[0]=.5;double e=error(actual(ws,q),actual(decision.candidate,q));std::cout<<"authority nonzero-q error="<<e<<"\n";EXPECT_LE(e,1e-8);}
TEST(ModelingReview,StructureAddRemoveAddMustNotDuplicateIds){auto ws=seed();ASSERT_FALSE(addJointAt(ws,0));ASSERT_FALSE(removeJointAt(ws,0));ASSERT_FALSE(addJointAt(ws,0));auto violations=checkInvariants(ws.design);for(auto& v:violations)std::cout<<"structure invariant "<<invariantIdToken(v.id)<<" "<<v.subject<<"\n";EXPECT_TRUE(violations.empty());}
TEST(ModelingReview,StructureReorderKeepsPoseAndDrivetrainBinding){auto ws=seed();PoseSet ps;ps.objectId=c::ObjectId::generate();ps.entries.push_back({"saved",{.1,.2,.3,.4,.5,.6},""});ws.poseSetObject=ps;ws.design.poseSetRef=ps.objectId;DrivetrainDesign dt;dt.objectId=c::ObjectId::generate();for(int i=0;i<6;++i)dt.ratioPerJoint.push_back(provided(double(10+i)));ws.drivetrainObject=dt;ws.design.drivetrainRef=dt.objectId;auto first=ws.design.joints[0].objectId;ASSERT_FALSE(reorderJoint(ws,0,true));EXPECT_EQ(ws.design.joints[1].objectId,first);EXPECT_DOUBLE_EQ(ws.poseSetObject->entries[0].jointConfiguration[1],.1);EXPECT_DOUBLE_EQ(ws.drivetrainObject->ratioPerJoint[1].value(),10.);}
TEST(ModelingReview,RejectedDrivetrainEditMustNotMutate){for(int kind:{0,1,2}){auto ws=seed();ASSERT_FALSE(ws.drivetrainObject.has_value());auto before=ws;std::optional<DrivetrainEditError> err;if(kind==0)err=applyDrivetrainRatioEdit(ws,0,-1.);else if(kind==1)err=applyDrivetrainFrictionEdit(ws,0,std::numeric_limits<double>::quiet_NaN(),0.,0.);else err=applyDrivetrainTorqueLimitEdit(ws,0,std::numeric_limits<double>::quiet_NaN(),0.);ASSERT_TRUE(err.has_value());std::cout<<"rejected drivetrain edit kind="<<kind<<" createdObject="<<ws.drivetrainObject.has_value()<<" createdRef="<<ws.design.drivetrainRef.has_value()<<"\n";EXPECT_EQ(ws,before);}}
ValidatedSource xmlSource(const std::string& xml){ValidatedSource s;s.bytes.assign(xml.begin(),xml.end());s.entrySnapshot.finalPath="C:/review/model.xml";s.entrySnapshot.sizeBytes=s.bytes.size();s.dependencyTree.rootRel="model.xml";sdurws::ird::io::ResourceNode node;node.relPath="model.xml";node.exists=true;node.snapshot=s.entrySnapshot;s.dependencyTree.nodes.push_back(node);return s;}
TEST(ModelingReview,WorkCellRateUnitsAndInvalidText){for(const std::string value:{"180","bad"}){auto s=xmlSource("<WorkCell name='wc'><SerialDevice name='arm'><Frame name='base' type='Fixed'/><Joint name='j1' type='Revolute'><VelLimit max='"+value+"'/><AccLimit max='"+value+"'/></Joint></SerialDevice></WorkCell>");std::vector<c::DiagnosticRecord>d;auto out=ModelImportMapper{}.mapWorkCellXml(s,{},d);ASSERT_TRUE(out.draft);auto& j=out.draft->joints.back();std::cout<<"WC rate input="<<value<<" state="<<int(j.maxVelocity.state());if(j.maxVelocity.tryValue())std::cout<<" actual="<<j.maxVelocity.value();std::cout<<"\n";if(value=="180"){ASSERT_TRUE(j.maxVelocity.tryValue());EXPECT_NEAR(j.maxVelocity.value(),3.141592653589793,1e-12);EXPECT_NEAR(j.maxAcceleration.value(),3.141592653589793,1e-12);}else EXPECT_EQ(j.maxVelocity.state(),c::FieldState::Invalid);}}
TEST(ModelingReview,UrdfEmptyOrMalformedMustNotThrow){for(const std::string variant:{"<axis xyz=''/>","<limit lower='-1' upper='1' velocity='bad'/>","<limit lower='' upper='1'/>"}){const std::string xml="<robot name='robot'><link name='base'/><link name='tip'/><joint name='j' type='revolute'><parent link='base'/><child link='tip'/>"+variant+"</joint></robot>";std::vector<c::DiagnosticRecord>d;std::cout<<"URDF variant="<<variant<<"\n";EXPECT_NO_THROW(ModelImportMapper{}.mapUrdf(xmlSource(xml),{},d));}}
TEST(ModelingReview,DiffMustShowPartAndRateChanges){auto before=seed();withBaseTool(before);for(int kind:{0,1}){auto after=before;if(kind==0)after.toolObjects[0].tcpList[0].offset=T(V(1,2,3));else after.design.joints[0].maxVelocity=provided(123.);auto diff=ModelDiffService{}.diff(before,after);std::size_t count=diff.structure.size()+diff.parameters.size()+diff.properties.size();std::cout<<"diff kind="<<kind<<" count="<<count<<"\n";EXPECT_GT(count,0u);}}
struct ReviewHost final:ModelingDialogHost{QString path;std::optional<QString> openFilePath(const QString&,const QString&)override{return path;}std::optional<QString>saveFilePath(const QString&,const QString&,const QString&)override{return {};}std::optional<int>chooseItem(const QString&,const QString&,const QStringList&)override{return 0;}bool confirmProceed(const QString&,const QString&)override{return true;}bool confirmImport(const QString& text)override{std::cout<<text.toStdString()<<"\n";return true;}void showInfo(const QString&,const QString&)override{}std::optional<std::vector<CustomChainJointSpec>>declareCustomChain(const QString&,const CustomChainJointSpec&)override{return {};}};
TEST(ModelingReview,PackageUiImportMustKeepParts){auto ws=seed();withBaseTool(ws);auto in=inputOf(ws);PackageExportTarget target;target.targetFile="C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/build/modeling-review-20261028/probe.irdbundle";target.createdAtUtcIso8601="2026-10-28T00:00:00Z";std::vector<c::DiagnosticRecord>d;auto ex=ModelPackagePort{}.exportPackage(in,target,d);ASSERT_TRUE(ex.ok)<<ex.error.detail;ModuleSessionState session;session.draft=seed();ReviewHost host;host.path=QString::fromStdString(target.targetFile.string());std::string summary;ASSERT_TRUE(executeModelingCommand("modeling.import-package",session,{},host,summary))<<summary;std::cout<<"UI imported toolRefs="<<session.draft.design.toolRefs.size()<<" toolObjects="<<session.draft.toolObjects.size()<<"\n";EXPECT_EQ(session.draft.toolObjects.size(),1u);}
TEST(ModelingReview,DhZeroOffsetControl){auto ws=seed();ws.design.authority=AuthorityMode::StandardDH;for(auto& j:ws.design.joints){j.dhDerived=DhParameters{.2,.1,.15,.6};j.zeroOffset=0;j.axis=c::SourcedValue<V>::notProvided();j.origin=c::SourcedValue<JointPose>::notProvided();}withBaseTool(ws);for(double v:{0.,.4}){std::vector<double>q(6,v);T expected=baseOf(ws);for(auto& j:ws.design.joints){auto d=*j.dhDerived;expected=expected*oracleDh(d.thetaOffset+v,d.d,d.a,d.alpha);}expected=expected*toolOf(ws);std::cout<<"DH zeroOffset=0 q="<<v;double e=error(actual(ws,q),expected);std::cout<<"\n";EXPECT_LE(e,1e-8);}}
// 同一三角网格未经预烘焙；必须保留 T_link_geom。
struct ReviewResources final:r::IRuntimeResourceProvider{
 std::vector<uint8_t> bytes; c::Digest256 digest{};
 explicit ReviewResources(std::string text):bytes(text.begin(),text.end()){
  c::ContentDigester h;h.update(bytes.data(),bytes.size());digest=h.finalize();
 }
 r::Expected<r::ResourceBytes,r::ResourceReadError> tryResourceBytes(c::ObjectId)const override{
  return r::Expected<r::ResourceBytes,r::ResourceReadError>::ok({bytes.data(),bytes.size(),digest});
 }
};
TEST(ModelingReview,GeometryLocalPoseReachesProductionWorkCell){
 auto ws=seed();ReviewResources resource("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
 m::ResourceRef rr;rr.resourceId="review-mesh";rr.contentDigest=resource.digest;
 rr.state=m::ResourceState::Recorded;rr.externalRecord=m::ExternalResourceRecord{"C:/review/triangle.obj",resource.digest};
 ws.design.resourceManifest.push_back(rr);m::GeometryRef gr;gr.resourceRefId=rr.resourceId;
 gr.localTransform=T(V(1,2,3),rw::math::RPY<double>(.2,-.1,.3).toRotation3D());
 ws.design.links[1].visual=gr;
 auto in=inputOf(ws);RobotDesignReader reader(&in);auto req=requestOf(in,reader);req.resources=&resource;
 auto out=r::createCanonicalModelCompiler()->compile(req);ASSERT_EQ(out.status,r::CompileStatus::Published);
 auto wc=out.snapshot->workCellHandle();std::string frame;
 for(auto& n:out.snapshot->nameMap().entries())if(n.objectId==ws.design.links[1].objectId)frame=n.fullName;
 ASSERT_FALSE(frame.empty());bool found=false;
 for(auto& o:wc->getObjects())for(auto& g:o->getGeometry()){
  if(g->getId()!=frame+".visual")continue;
  found=true;std::cout<<"geometry localPose actual="<<g->getTransform()<<" expected="<<gr.localTransform<<"\n";
  EXPECT_LE(error(g->getTransform(),gr.localTransform),1e-9);
 }
 ASSERT_TRUE(found)<<"production mesh must be loaded before comparing placement";
}
int main(int argc,char**argv){std::cout<<std::setprecision(17);::testing::InitGoogleTest(&argc,argv);sdurws::ird::testkit::installTestRecordListener(argc,argv);return RUN_ALL_TESTS();}
