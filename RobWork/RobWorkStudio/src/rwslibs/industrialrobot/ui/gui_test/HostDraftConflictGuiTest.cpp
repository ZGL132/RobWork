/**
 * @file HostDraftConflictGuiTest.cpp
 * @brief PM-04/AT-21/29：真实控制器加真实Windows窗口，验证保留/三选项/失败门控。
 * 替身仅位于草稿读写和域源端口；真实磁盘及完整建模重建另由契约/域GUI覆盖。
 */
#include <gtest/gtest.h>
#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>
#include "plugin/HostDraftConflict.hpp"

namespace sdurws::ird::ui::test {
namespace {
/** @brief 仅观测C-5边界写请求；UI线程夹具持有，模型计算不打替身。 */
class ConflictStore final:public IUiDraftStorePort {
public:
    bool ok=true;std::vector<DraftDocumentProjection> saved;
    /** @brief 记录原完整投影，ok=false代表磁盘错误；不伪造项目修订。 */
    DraftSaveOutcome save(const DraftDocumentProjection& doc) override {
        saved.push_back(doc);return {ok,ok?"":"disk-full",ok?"":"磁盘空间不足"};
    }
    /** @brief 此GUI专项不读取磁盘，返回端口合法Missing；真读取由契约用例验证。 */
    DraftLoadOutcome tryLoad(const std::string&) override{return {};}
    /** @brief 冲突流程不应丢弃原稿；触达即令用例失败。 */
    DraftDiscardOutcome discard(const std::string&) override{ADD_FAILURE()<<"不应丢弃旧稿";return {};}
};
/** @brief C-5清单边界空值；会话脏与冲突由真实控制器持有，仅本UI线程访问。 */
class ConflictQuery final:public IUiDraftQueryPort {
public:
    /** @brief 返回合法空清单，不影响控制器会话脏代次。 */
    std::vector<DraftRowProjection> listDrafts() const override{return {};}
};
/** @brief 域源端口边界的读错/重建观测；不参与建模算法或序列化。 */
class ConflictSource final:public IModuleDraftSource {
public:
    DraftDocumentProjection doc;bool fail=false;int rebuilt=0;
    /** @brief 返回窗口工程用语；固定中文不泄漏内部token。 */
    std::string displayName() const override{return "建模";}
    /** @brief 以值返回未应用原稿，控制器不能修改源payload/base。 */
    DraftDocumentProjection buildDraftDocument() const override{return doc;}
    /** @brief 恢复不属于本冲突流程，触达即失败。 */
    void adoptRestoredDocument(const DraftDocumentProjection&) override{ADD_FAILURE()<<"不应恢复磁盘稿";}
    /** @brief 观测显式重建；失败抛错且旧值不变，成功才采纳固定tip。 */
    void rebuildOnRevision(const std::string& tip) override{
        if(fail){throw std::runtime_error("固定修订读取失败，原草稿已保留");}
        ++rebuilt;doc.baseRevisionId=core::RevisionId::fromCanonical(tip);doc.payload="当前完整基线";
    }
};
/** @brief 真实控制器/窗口夹具；端口及所有闭包由本fixture保活到模态退出。 */
class DraftConflictGui:public ::testing::Test {
protected:
    ConflictSource source;std::shared_ptr<ConflictStore> store=std::make_shared<ConflictStore>();
    std::unique_ptr<IDraftController> controller;bool current=true;core::RevisionId tip;
    /** @brief 装配合法可写会话与真实Stale状态；不以窗口输入伪造保存成功。 */
    void SetUp() override{
        DraftControllerDeps deps;deps.postToDiskThread=[](std::function<void()> task){task();return true;};
        deps.postToUiThread=[](std::function<void()> task){task();return true;};
        controller=createDraftController(std::move(deps));DraftSessionBinding binding;
        binding.projectId=core::ProjectId::generate();binding.branchId=core::BranchId::generate();
        binding.writable=true;binding.store=store;binding.drafts=std::make_shared<ConflictQuery>();
        controller->bindSession(binding);source.doc.moduleId="modeling";source.doc.schemaVersion=1;
        source.doc.branchId=binding.branchId;source.doc.baseRevisionId=core::RevisionId::generate();
        source.doc.payload="原未应用完整稿";controller->attachModule("modeling",source);
        controller->notifySessionDirty("modeling");tip=core::RevisionId::generate();
        CommandResultProjection rejected;rejected.rejectionReason="stale-revision";
        StaleRevisionDetailProjection detail;detail.currentTipRevision=tip.toCanonical();
        detail.draftBaseRevision=source.doc.baseRevisionId.toCanonical();detail.advancedSummary={"修改工程策略"};
        detail.involvedObjectNames={"工具 焊枪","模型 机械臂"};rejected.staleDetail=detail;
        controller->onCommandResult("modeling",rejected);
    }
    /** @brief 在真实模态事件循环操作控件；超时仅关闭本轮窗口并显式失败。 */
    DraftConflictResolution run(const std::function<void(QDialog*,int)>& action){
        int step=0;bool seen=false;QTimer poll,timeout;poll.setInterval(50);timeout.setSingleShot(true);
        QObject::connect(&poll,&QTimer::timeout,[&]{
            auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if(dialog&&dialog->objectName()=="ird_draft_conflict"){seen=true;action(dialog,step++);}
        });
        QObject::connect(&timeout,&QTimer::timeout,[]{ADD_FAILURE()<<"草稿冲突窗口超时";
            if(auto* d=qobject_cast<QDialog*>(QApplication::activeModalWidget())){d->reject();}});
        poll.start();timeout.start(5000);
        const auto result=presentDraftConflict(nullptr,*controller,"modeling",[&]{return current;});
        poll.stop();timeout.stop();EXPECT_TRUE(seen)<<"正式冲突窗口未呈现";return result;
    }
    /** @brief 点击具名真实按钮；缺控件令用例失败而非解引用空指针。 */
    void click(QDialog* dialog,const char* name){auto* button=dialog->findChild<QPushButton*>(name);
        EXPECT_NE(button,nullptr);if(button){button->click();}}
    /** @brief 保存本轮真实窗口，供人工视觉核对；[in] name固定证据名，无用户数据路径。 */
    void snapshot(QDialog* dialog,const QString& name){
        const auto out=qEnvironmentVariable("IRD_MODELING_GUI_EVIDENCE_OUT");
        if(out.isEmpty()){return;}
        ASSERT_TRUE(QDir().mkpath(out));EXPECT_TRUE(dialog->grab().save(out+"/"+name+".png"));
    }
};

/** @brief PM-04/AT-29：先保存旧稿，查看涉事对象再取消，原稿和冲突不变。 */
TEST_F(DraftConflictGui, ViewThenCancelRetainsOriginal_PM04_AT29){
    IRD_TEST_INFO("PM-04",{"AT-29"},std::nullopt);const auto before=source.doc;
    EXPECT_EQ(run([&](QDialog* d,int step){
        ASSERT_EQ(store->saved.size(),1U);EXPECT_EQ(store->saved.front().origin,DraftOrigin::ApplyRetained);
        if(step==0){click(d,"ird_draft_conflict_diff");}
        else{auto* names=d->findChild<QLabel*>("ird_draft_conflict_objects");ASSERT_NE(names,nullptr);
            EXPECT_TRUE(names->isVisible());EXPECT_TRUE(names->text().contains(QStringLiteral("焊枪")));
            snapshot(d,QStringLiteral("conflict-objects-cancel"));
            click(d,"ird_draft_conflict_cancel");}
    }),DraftConflictResolution::Cancelled);
    EXPECT_EQ(source.doc.payload,before.payload);EXPECT_EQ(source.doc.baseRevisionId,before.baseRevisionId);
    EXPECT_EQ(source.rebuilt,0);EXPECT_TRUE(controller->summary().anyDirty);
    EXPECT_TRUE(controller->staleConflictData("modeling").present);
}
/** @brief PM-04/AT-21：只有显式推荐按钮成功重建一次，不能自动提交或重试。 */
TEST_F(DraftConflictGui, ExplicitRebuildConsumesFixedTip_PM04_AT21){
    IRD_TEST_INFO("PM-04",{"AT-21"},std::nullopt);
    EXPECT_EQ(run([&](QDialog* d,int){click(d,"ird_draft_conflict_reedit");}),DraftConflictResolution::Rebuilt);
    EXPECT_EQ(source.rebuilt,1);EXPECT_EQ(source.doc.baseRevisionId,tip);
    EXPECT_FALSE(controller->staleConflictData("modeling").present);
}
/** @brief PM-08/AT-29：保存失败禁重建，显示失败并保留可取消的原草稿。 */
TEST_F(DraftConflictGui, SaveFailureDisablesRebuild_PM08_AT29){
    IRD_TEST_INFO("PM-08",{"AT-29"},std::nullopt);store->ok=false;
    run([&](QDialog* d,int){auto* button=d->findChild<QPushButton*>("ird_draft_conflict_reedit");
        ASSERT_NE(button,nullptr);EXPECT_FALSE(button->isEnabled());
        auto* error=d->findChild<QLabel*>("ird_draft_conflict_error");ASSERT_NE(error,nullptr);
        EXPECT_TRUE(error->text().contains(QStringLiteral("磁盘空间不足")));
        snapshot(d,QStringLiteral("conflict-save-failed"));click(d,"ird_draft_conflict_cancel");});
    EXPECT_EQ(source.rebuilt,0);EXPECT_TRUE(controller->summary().anyDirty);
}
/** @brief PM-04/AT-29：模态期间会话/tip漂移或reader失败均不得采纳新基线。 */
TEST_F(DraftConflictGui, ChangedSessionOrReaderFailureKeepsDraft_PM04_AT29){
    IRD_TEST_INFO("PM-04",{"AT-29"},std::nullopt);const auto before=source.doc;
    for(const bool readerFails:{false,true}){
        current=true;source.fail=readerFails;
        run([&](QDialog* d,int step){if(step==0){if(!readerFails){current=false;}click(d,"ird_draft_conflict_reedit");}
            else{auto* error=d->findChild<QLabel*>("ird_draft_conflict_error");ASSERT_NE(error,nullptr);
                EXPECT_FALSE(error->text().isEmpty());click(d,"ird_draft_conflict_cancel");}});
        EXPECT_EQ(source.doc.payload,before.payload);EXPECT_EQ(source.doc.baseRevisionId,before.baseRevisionId);
        EXPECT_EQ(source.rebuilt,0);EXPECT_TRUE(controller->staleConflictData("modeling").present);
    }
    // 与会话身份守卫独立：同一会话也可能收到更新的冲突详情。旧窗口的
    // 确认不能消费新tip/新base；控制器仍应保留未决状态供用户重新获取详情。
    current=true;source.fail=false;
    for(const bool changeTip:{true,false}){
        // 每轮先恢复同一原冲突，使第二轮单独验证base漂移而非再次触发tip漂移。
        CommandResultProjection initial;initial.rejectionReason="stale-revision";
        StaleRevisionDetailProjection stable;stable.currentTipRevision=tip.toCanonical();
        stable.draftBaseRevision=before.baseRevisionId.toCanonical();initial.staleDetail=stable;
        controller->onCommandResult("modeling",initial);
        run([&](QDialog* d,int step){
            if(step==0){
                CommandResultProjection rejected;rejected.rejectionReason="stale-revision";
                StaleRevisionDetailProjection detail;
                detail.currentTipRevision=changeTip?core::RevisionId::generate().toCanonical():tip.toCanonical();
                detail.draftBaseRevision=changeTip?before.baseRevisionId.toCanonical():core::RevisionId::generate().toCanonical();
                rejected.staleDetail=detail;controller->onCommandResult("modeling",rejected);
                click(d,"ird_draft_conflict_reedit");
            }else{
                auto* button=d->findChild<QPushButton*>("ird_draft_conflict_reedit");ASSERT_NE(button,nullptr);
                EXPECT_FALSE(button->isEnabled());click(d,"ird_draft_conflict_cancel");
            }
        });
        EXPECT_EQ(source.rebuilt,0);EXPECT_EQ(source.doc.payload,before.payload);
        EXPECT_EQ(source.doc.baseRevisionId,before.baseRevisionId);
    }
}
}
}
