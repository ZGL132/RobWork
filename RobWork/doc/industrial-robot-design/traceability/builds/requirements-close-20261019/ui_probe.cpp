#include "C:/Users/zgl18/.codex/worktrees/486b/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/ui/test/KinEvaluationChannelTest.cpp"
TEST(KinEvaluationChannel, ReviewDirectionPositiveCases) {
 auto ws=makeWorksetWithTaskPoint(); auto p=ws.points.entries.front(); ws.points.entries.clear();
 auto free=p; free.objectId=idFrom<core::ObjectId>("review-free"); free.name="free_control";
 free.pose.orientation.kind=req::OrientationRuleKind::ToolRollFree; ws.points.entries.push_back(free);
 auto pointing=p; pointing.objectId=idFrom<core::ObjectId>("review-point"); pointing.name="pointing_Z";
 pointing.pose.orientation.kind=req::OrientationRuleKind::PointAtTarget;
 pointing.pose.orientation.targetPoint={1.8,0,1}; ws.points.entries.push_back(pointing);
 auto normal=p; normal.objectId=idFrom<core::ObjectId>("review-normal"); normal.name="normal_Z";
 const auto frame=idFrom<core::ObjectId>("review-frame");
 normal.pose.orientation.kind=req::OrientationRuleKind::AlignGeometryNormal;
 normal.pose.orientation.targetSceneObject=frame;
 normal.pose.orientation.feature=req::OrientationFeature::FramePlaneNormal; ws.points.entries.push_back(normal);
 ScriptedEditor editor(ws); ExecutorHarness h;
 h.executor=std::make_shared<KinEvaluationExecutor>(KinEvaluationExecutor::Deps{
 &editor,[=](const core::ObjectId& id)->std::optional<rw::math::Transform3D<double>>{
 if(id!=frame) return std::nullopt; return trans(0,0,0);},QCoreApplication::instance(),
 [&h](const KinChannelBackgroundResultNote& note){h.notes.push_back(note);}});
 h.executor->attachView(h.view,h.boundId);
 auto config=makeLegalConfig(); config.initialStrategy=InitialValueStrategy::ReferenceQ;
 config.iterationLimit=200; h.executor->setConfiguration(config);
 KinChannelBackgroundRequest r; r.kind=KinChannelBackgroundKind::TaskPointsBatch;
 r.snapshotId=h.boundId; r.epoch=h.executor->epoch(); auto ack=h.executor->submit(r);
 ASSERT_TRUE(ack.accepted)<<ack.reason; h.drain(); const auto results=h.executor->latestBatchOutcomes();
 ASSERT_EQ(results.size(),3u);
 for(const auto& point:ws.points.entries){auto it=results.find(point.objectId); ASSERT_NE(it,results.end());
 std::cout<<"REVIEW "<<point.name<<" "<<it->second.outcomeState<<" "<<it->second.outcomeText<<std::endl;
 EXPECT_EQ(it->second.outcomeState,"completed")<<point.name;}
}
TEST(KinEvaluationChannel, ReviewTiltedDirectionPositiveCases) {
 const double a=.7;
 const rw::math::Rotation3D<double> tilt(1,0,0,0,cos(a),-sin(a),0,sin(a),cos(a));
 auto model=makeModel({revolute("tilted-joint",{0,0,1},rw::math::Transform3D<double>({0,0,0},tilt),-.01,.01)},rw::math::Transform3D<double>({0,0,0},rotZ(.4)));
 auto ws=makeWorksetWithTaskPoint(); auto p=ws.points.entries.front(); ws.points.entries.clear();
 p.pose.position=core::SourcedValue<rw::math::Vector3D<double>>::provided({0,0,0},core::ValueProvenance::make(core::ProvenanceKind::UserProvided));
 auto pointing=p; pointing.name="tilted_pointing"; pointing.objectId=idFrom<core::ObjectId>("tilted-point");
 pointing.pose.orientation.kind=req::OrientationRuleKind::PointAtTarget;
 pointing.pose.orientation.targetPoint={0,-sin(a),cos(a)}; ws.points.entries.push_back(pointing);
 auto normal=p; normal.name="tilted_normal"; normal.objectId=idFrom<core::ObjectId>("tilted-normal");
 const auto frame=idFrom<core::ObjectId>("tilted-frame");
 normal.pose.orientation.kind=req::OrientationRuleKind::AlignGeometryNormal;
 normal.pose.orientation.targetSceneObject=frame; normal.pose.orientation.feature=req::OrientationFeature::FramePlaneNormal; ws.points.entries.push_back(normal);
 ScriptedEditor editor(ws); ExecutorHarness h;
 h.view=std::make_shared<FixtureView>(model);
 h.executor=std::make_shared<KinEvaluationExecutor>(KinEvaluationExecutor::Deps{
 &editor,[=](const core::ObjectId& id)->std::optional<rw::math::Transform3D<double>>{
 if(id!=frame) return std::nullopt; return rw::math::Transform3D<double>({0,0,0},tilt);},QCoreApplication::instance(),
 [&h](const KinChannelBackgroundResultNote& note){h.notes.push_back(note);}});
 h.executor->attachView(h.view,h.boundId); auto config=makeLegalConfig();
 config.initialStrategy=InitialValueStrategy::ReferenceQ; config.iterationLimit=200; h.executor->setConfiguration(config);
 KinChannelBackgroundRequest r; r.kind=KinChannelBackgroundKind::TaskPointsBatch; r.snapshotId=h.boundId; r.epoch=h.executor->epoch();
 auto ack=h.executor->submit(r); ASSERT_TRUE(ack.accepted)<<ack.reason;
 h.drain(); const auto results=h.executor->latestBatchOutcomes(); ASSERT_EQ(results.size(),2u);
 for(const auto& point:ws.points.entries){auto it=results.find(point.objectId); ASSERT_NE(it,results.end());
 std::cout<<"REVIEW "<<point.name<<" "<<it->second.outcomeState<<" "<<it->second.outcomeText<<std::endl;
 EXPECT_EQ(it->second.outcomeState,"completed")<<point.name;}
}
/** @brief 直接走需求批量评估生产通道；夹具仅给出真实CanonicalModel和参考Frame。 */
void reviewDirectionBoundary(bool opposite) {
 const double desiredAngle=opposite?3.14159265358979323846:.2;
 const rw::math::Rotation3D<double> targetR(1,0,0,0,cos(desiredAngle),-sin(desiredAngle),0,sin(desiredAngle),cos(desiredAngle));
 auto model=opposite
  ?makeModel({revolute("review-z",{0,0,1},trans(0,0,0),-.01,.01)},trans(0,0,0))
  :makeModel({revolute("review-x",{1,0,0},trans(0,0,0),-1,1)},trans(0,0,0));
 auto ws=makeWorksetWithTaskPoint();auto p=ws.points.entries.front();ws.points.entries.clear();
 p.pose.position=core::SourcedValue<rw::math::Vector3D<double>>::provided({0,0,0},core::ValueProvenance::make(core::ProvenanceKind::UserProvided));
 auto pointing=p;pointing.name="boundary-pointing";pointing.objectId=idFrom<core::ObjectId>("boundary-point");
 pointing.pose.orientation.kind=req::OrientationRuleKind::PointAtTarget;
 pointing.pose.orientation.targetPoint={0,-sin(desiredAngle),cos(desiredAngle)};ws.points.entries.push_back(pointing);
 const auto frame=idFrom<core::ObjectId>("boundary-frame");
 auto normal=p;normal.name="boundary-normal";normal.objectId=idFrom<core::ObjectId>("boundary-normal");
 normal.pose.orientation.kind=req::OrientationRuleKind::AlignGeometryNormal;
 normal.pose.orientation.targetSceneObject=frame;normal.pose.orientation.feature=req::OrientationFeature::FramePlaneNormal;ws.points.entries.push_back(normal);
 ScriptedEditor editor(ws);ExecutorHarness h;h.view=std::make_shared<FixtureView>(model);
 h.executor=std::make_shared<KinEvaluationExecutor>(KinEvaluationExecutor::Deps{
 &editor,[=](const core::ObjectId& id)->std::optional<rw::math::Transform3D<double>>{
 if(id!=frame)return std::nullopt;return rw::math::Transform3D<double>({0,0,0},targetR);},QCoreApplication::instance(),
 [&h](const KinChannelBackgroundResultNote& note){h.notes.push_back(note);}});
 h.executor->attachView(h.view,h.boundId);auto config=makeLegalConfig();
 config.initialStrategy=InitialValueStrategy::ReferenceQ;config.iterationLimit=200;h.executor->setConfiguration(config);
 KinChannelBackgroundRequest r;r.kind=KinChannelBackgroundKind::TaskPointsBatch;r.snapshotId=h.boundId;r.epoch=h.executor->epoch();
 const auto ack=h.executor->submit(r);ASSERT_TRUE(ack.accepted)<<ack.reason;h.drain();
 const auto results=h.executor->latestBatchOutcomes();ASSERT_EQ(results.size(),2u);
 for(const auto& point:ws.points.entries){const auto it=results.find(point.objectId);ASSERT_NE(it,results.end());
 std::cout<<"BOUNDARY opposite="<<opposite<<" "<<point.name<<" "<<it->second.outcomeState<<" "<<it->second.outcomeText<<std::endl;
 EXPECT_EQ(it->second.outcomeState,opposite?"failed":"completed")<<point.name;}
}
TEST(KinEvaluationChannel, ReviewOppositeDirectionMustFail){reviewDirectionBoundary(true);}
TEST(KinEvaluationChannel, ReviewDirectionCorrectionMustConverge){reviewDirectionBoundary(false);}
#include <sdurws/ird/testkit/gtest/RecordListener.hpp>
int main(int argc,char** argv){QCoreApplication app(argc,argv); ::testing::InitGoogleTest(&argc,argv); sdurws::ird::testkit::installTestRecordListener(argc,argv); return RUN_ALL_TESTS();}

