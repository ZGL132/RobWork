#include "C:/Users/zgl18/.codex/worktrees/486b/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/requirements/gui_test/RequirementsSessionGuiTest.cpp"
#include <sdurws/ird/requirements/Sampling.hpp>
TEST_F(RequirementsSessionGuiTest, ReviewConfirmUnchangedRegionCreatesPlan) {
 const auto id=m_editor.workingSet().regions.entries.front().objectId;
 m_panel->refreshPanel(m_editor.workingSet(),m_report); m_panel->focusObject(id);
 m_panel->resize(1200,900); m_panel->show(); QApplication::processEvents();
 ASSERT_TRUE(m_editor.workingSet().plans.entries.empty());
 auto* f=m_panel->findChild<QWidget*>("ird_region_sampling_form"); ASSERT_NE(f,nullptr);
 f->findChild<QPushButton*>("ird_region_sampling_apply")->click(); QApplication::processEvents();
 EXPECT_EQ(m_editor.workingSet().plans.entries.size(),1u);
}
TEST_F(RequirementsSessionGuiTest, ReviewRandomDigest) {
 auto r=m_editor.workingSet().regions.entries.front(); SamplingPlanSpec s;
 s.positionSampling.method=PositionSamplingMethod::Random; s.positionSampling.count=10;
 std::vector<core::DiagnosticRecord> d; SamplingPlanBuilder b;
 auto plan=b.buildPlan(r,s,d); ASSERT_TRUE(plan.ok);
 auto digest=b.digest(plan.plan,d); ASSERT_TRUE(digest.ok());
 std::cout<<"REVIEW random_count="<<plan.plan.positionSampling.count<<" denominator="<<digest.get().positionSamples<<std::endl;
 EXPECT_EQ(digest.get().positionSamples,10u);
}
TEST_F(RequirementsSessionGuiTest, ReviewRegionSpacingThenResize) {
 const auto id=m_editor.workingSet().regions.entries.front().objectId;
 m_panel->refreshPanel(m_editor.workingSet(),m_report); m_panel->focusObject(id);
 m_panel->resize(1200,900); m_panel->show(); QApplication::processEvents();
 auto* f=m_panel->findChild<QWidget*>("ird_region_sampling_form"); ASSERT_NE(f,nullptr);
 auto* method=f->findChild<QComboBox*>("ird_region_sampling_method");
 method->setCurrentIndex(method->findData(int(PositionSamplingMethod::GridBySpacing)));
 for(int a=0;a<3;++a) f->findChild<QLineEdit*>(QString("ird_region_spacing_%1").arg(a))->setText("0.5");
 f->findChild<QPushButton*>("ird_region_sampling_apply")->click(); QApplication::processEvents();
 ASSERT_EQ(m_editor.workingSet().plans.entries.size(),1u);
 const auto oldCount=m_editor.workingSet().plans.entries.front().positionSampling.counts[0];
 auto* size=fieldEditor(*m_panel,"box-size-x"); ASSERT_NE(size,nullptr);
 size->setText("4.0"); QMetaObject::invokeMethod(size,"editingFinished",Qt::DirectConnection);
 QApplication::processEvents();
 const auto& r=m_editor.workingSet().regions.entries.front(); EXPECT_DOUBLE_EQ(r.box.size[0],4.0);
 SamplingPlanSpec spec; spec.positionSampling=r.positionSampling; spec.orientationSampling=r.orientationSampling;
 std::vector<core::DiagnosticRecord> diags;
 auto expected=SamplingPlanBuilder().buildPlan(r,spec,diags); ASSERT_TRUE(expected.ok);
 std::cout<<"REVIEW old="<<oldCount<<" stored="<<m_editor.workingSet().plans.entries.front().positionSampling.counts[0]<<" expected="<<expected.plan.positionSampling.counts[0]<<std::endl;
 EXPECT_EQ(m_editor.workingSet().plans.entries.front().positionSampling.counts,expected.plan.positionSampling.counts);
}
TEST_F(RequirementsSessionGuiTest, ReviewRegionBatchResizeSyncsPlan) {
 const auto id=m_editor.workingSet().regions.entries.front().objectId;
 m_panel->refreshPanel(m_editor.workingSet(),m_report); m_panel->focusObject(id);
 m_panel->resize(1200,900); m_panel->show(); QApplication::processEvents();
 auto* f=m_panel->findChild<QWidget*>("ird_region_sampling_form"); ASSERT_NE(f,nullptr);
 auto* method=f->findChild<QComboBox*>("ird_region_sampling_method");
 method->setCurrentIndex(method->findData(int(PositionSamplingMethod::GridBySpacing)));
 for(int a=0;a<3;++a) f->findChild<QLineEdit*>(QString("ird_region_spacing_%1").arg(a))->setText("0.5");
 f->findChild<QPushButton*>("ird_region_sampling_apply")->click(); QApplication::processEvents();
 ASSERT_EQ(m_editor.workingSet().plans.entries.size(),1u);
 auto* batch=m_panel->findChild<QWidget*>("ird_req_batch_region"); ASSERT_NE(batch,nullptr);
 auto* table=batch->findChild<QTableWidget*>("ird_param_table"); ASSERT_NE(table,nullptr);
 bool found=false;
 for(int row=0;row<table->rowCount();++row) if(table->item(row,0) && table->item(row,0)->data(Qt::UserRole).toString()=="box-size-x") {
  ASSERT_NE(table->item(row,1),nullptr); table->item(row,1)->setText("4"); found=true; break;
 }
 ASSERT_TRUE(found);
 QApplication::processEvents();
 batch->findChild<QPushButton*>("ird_param_apply")->click();
 batch->findChild<QPushButton*>("ird_param_confirm_yes")->click(); QApplication::processEvents();
 EXPECT_DOUBLE_EQ(m_editor.workingSet().regions.entries.front().box.size[0],4.0);
 const auto count=m_editor.workingSet().plans.entries.front().positionSampling.counts[0];
 std::cout<<"REVIEW batch_resize size="<<m_editor.workingSet().regions.entries.front().box.size[0]<<" plan_count="<<count<<std::endl;
 EXPECT_EQ(count,9u);
}
TEST_F(RequirementsSessionGuiTest, ReviewSegmentEnableToggle) {
 auto p=m_editor.workingSet().points.entries.front(); p.approach.distanceM=.25;
 ASSERT_TRUE(m_editor.applyEdit(p).accepted);
 m_panel->refreshPanel(m_editor.workingSet(),m_report); m_panel->focusObject(p.objectId);
 m_panel->resize(1200,900); m_panel->show(); QApplication::processEvents();
 auto combos=m_panel->findChildren<QComboBox*>("ird_segment_enabled_combo"); ASSERT_EQ(combos.size(),3);
 auto* combo=combos.front(); std::cout<<"REVIEW toggle before="<<combo->currentIndex()<<std::endl;
 combo->setCurrentIndex(1-combo->currentIndex()); QApplication::processEvents();
 std::cout<<"REVIEW toggle after approach="<<m_editor.workingSet().points.entries.front().approach.enabled<<std::endl;
 EXPECT_NE(m_editor.workingSet().points.entries.front().approach.enabled,p.approach.enabled);
}
#include <sdurws/ird/testkit/gtest/RecordListener.hpp>
int main(int argc,char** argv){QApplication app(argc,argv); ::testing::InitGoogleTest(&argc,argv); sdurws::ird::testkit::installTestRecordListener(argc,argv); return RUN_ALL_TESTS();}

