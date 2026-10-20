/**
 * @file HostEngineeringPolicyGuiTest.cpp
 * @brief A-23/UX-08：Windows真实模态策略编辑，验证控件到项目事务的完整链。
 * 测试不替代policy解析或project存储；所有项目写入均经过真实命令处理器。
 */
#include <gtest/gtest.h>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <sdurws/ird/project/ProjectStore.hpp>
#include <sdurws/ird/policy/PolicyInput.hpp>
#include <sdurws/ird/policy/SystemDefaultPolicy.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>
#include "plugin/HostEngineeringPolicy.hpp"

namespace sdurws::ird::ui::test {
namespace {
/** @brief GUI用真实临时项目；fixture保活存储到模态关闭后，仅主UI线程访问。 */
class HostPolicyGuiTest : public ::testing::Test {
protected:
    QTemporaryDir directory;
    std::unique_ptr<project::ProjectStore> store;
    HostEngineeringPolicy host;
    /** @brief 新建磁盘项目并注册生产处理器，错误由ASSERT停止当前用例。 */
    void SetUp() override
    {
        ASSERT_TRUE(directory.isValid());
        auto opened=project::ProjectStoreFactory::createNew(
            std::filesystem::path(directory.path().toStdWString())/"gui.rwdesign","A23GUI");
        store=std::move(opened.store);ASSERT_TRUE(store);
        HostEngineeringPolicy::registerCommandHandler(store->handlerRegistry());host.bindProjectStore(store.get());
    }
    /** @brief 在模态显示后操作真实控件；超时只关闭本测试窗口，避免失败时挂死整套GUI。 */
    void edit(const std::function<void(QDialog*)>& action)
    {
        QTimer timeout;timeout.setSingleShot(true);
        QObject::connect(&timeout,&QTimer::timeout,[] {
            if(auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget()))dialog->reject();
            ADD_FAILURE()<<"策略编辑交互超时";
        });timeout.start(10000);
        QTimer::singleShot(50,[action] {
            auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
            EXPECT_NE(dialog,nullptr);if(dialog)action(dialog);
        });
        host.edit(nullptr);
    }
    /** @brief 找到指定命名字段并输入；value[in]为用户可见文本，不直接修改模型。 */
    void text(QDialog* dialog,const char* name,const QString& value)
    {
        auto* field=dialog->findChild<QLineEdit*>(name);EXPECT_NE(field,nullptr);if(field)field->setText(value);
    }
    /** @brief 点击真实确认按钮；非法草稿应保留模态，成功才退出。 */
    void confirm(QDialog* dialog)
    {
        auto* buttons=dialog->findChild<QDialogButtonBox*>();EXPECT_NE(buttons,nullptr);
        if(buttons)buttons->button(QDialogButtonBox::Ok)->click();
    }
};
}
TEST_F(HostPolicyGuiTest, CancelAndProtectedFieldsNeverWrite_UX08_A23)
{
    IRD_TEST_INFO((std::vector<std::string>{"UX-08","ARC-05"}),(std::vector<std::string>{"AT-01"}));
    const auto initial=store->query().head();
    edit([&](QDialog* d) {
        EXPECT_TRUE(d->isVisible());EXPECT_TRUE(d->findChild<QLineEdit*>("policy_anchor")->isReadOnly());
        EXPECT_TRUE(d->findChild<QLineEdit*>("policy_compatibility")->isReadOnly());
        EXPECT_TRUE(d->findChild<QLineEdit*>("policy_near")->text().isEmpty());
        EXPECT_TRUE(d->findChild<QLabel*>("policy_source")->text().contains(QStringLiteral("系统缺省")));
        text(d,"policy_travel","33");d->reject();
    });
    EXPECT_EQ(store->query().head().id,initial.id);
}
TEST_F(HostPolicyGuiTest, AllThresholdsUnitsScopeAndSourceCommit_UX08_A23)
{
    IRD_TEST_INFO((std::vector<std::string>{"UX-08","KIN-12","ARC-05"}),(std::vector<std::string>{"AT-01"}));
    edit([&](QDialog* d) {
        d->findChild<QCheckBox*>("policy_collision")->setChecked(true);
        d->findChild<QCheckBox*>("policy_domain_0")->setChecked(true);
        d->findChild<QCheckBox*>("policy_domain_1")->setChecked(true);
        d->findChild<QCheckBox*>("policy_domain_2")->setChecked(true);
        d->findChild<QCheckBox*>("policy_domain_3")->setChecked(true);
        d->findChild<QCheckBox*>("policy_adjacent")->setChecked(false);
        text(d,"policy_clearance","0.02");d->findChild<QComboBox*>("policy_clearance_unit")->setCurrentText("mm");
        EXPECT_DOUBLE_EQ(d->findChild<QLineEdit*>("policy_clearance")->text().toDouble(),20.0);
        text(d,"policy_travel","20");d->findChild<QComboBox*>("policy_travel_unit")->setCurrentText("deg");
        text(d,"policy_near","0.1");text(d,"policy_condition","100");
        d->findChild<QCheckBox*>("policy_mode_2")->setChecked(true);
        d->findChild<QComboBox*>("policy_origin")->setCurrentIndex(2);text(d,"policy_note",QStringLiteral("真实窗口输入"));
        auto* tabs=d->findChild<QTabWidget*>();
        for(int i=0;i<tabs->count();++i) {tabs->setCurrentIndex(i);QApplication::processEvents();EXPECT_TRUE(tabs->currentWidget()->isVisible());}
        tabs->setCurrentIndex(0);
        // 真实窗口留图；可由执行目录指定持久化位置，不以图像生成替代控件显示。
        const auto path=qEnvironmentVariable("IRD_A23_SCREENSHOT");if(!path.isEmpty())EXPECT_TRUE(d->grab().save(path));
        confirm(d);
    });
    const auto resolved=host.resolvePolicyForRevision(store->query().head());ASSERT_TRUE(resolved.policy);
    EXPECT_TRUE(resolved.policy->collision.enabled);EXPECT_EQ(resolved.policy->collision.enabledDomains.size(),4u);
    EXPECT_DOUBLE_EQ(resolved.policy->collision.safetyClearance->siValue(),0.02);
    EXPECT_FALSE(resolved.policy->collision.excludeAdjacentLinksByDefault);
    EXPECT_NEAR(resolved.policy->jointThresholds.finiteRotationTravelLimit.siValue(),20.0,1e-14);
    EXPECT_DOUBLE_EQ(resolved.policy->jointThresholds.nearLimitRatio->siValue(),0.1);
    EXPECT_DOUBLE_EQ(resolved.policy->jointThresholds.conditionNumberWarning->siValue(),100.0);
    EXPECT_EQ(resolved.policy->applicability.modes,std::vector<core::EvaluationMode>{core::EvaluationMode::Verified});
    EXPECT_EQ(resolved.policy->origin.note,std::optional<std::string>{u8"真实窗口输入"});
}
TEST_F(HostPolicyGuiTest, InvalidRuleConflictAndStaleStayOpenWithZeroWrites_UX08_A23)
{
    IRD_TEST_INFO((std::vector<std::string>{"UX-08","ARC-05"}),(std::vector<std::string>{"AT-01"}));
    const auto base=store->query().head();
    edit([&](QDialog* d) {
        text(d,"policy_travel","abc");confirm(d);
        EXPECT_FALSE(d->findChild<QLabel*>("policy_error")->text().isEmpty());EXPECT_TRUE(d->isVisible());
        EXPECT_EQ(store->query().head().id,base.id);text(d,"policy_travel","20");
        // 同一角色对在必检与忽略两表同时出现；必须调用真实policy冲突检测阻断。
        for(const auto* name:{"policy_mandatory","policy_excluded"}) {
            d->findChild<QPushButton*>(QString::fromLatin1(name)+"_add")->click();
            auto* table=d->findChild<QTableWidget*>(name);
            static_cast<QComboBox*>(table->cellWidget(0,0))->setCurrentIndex(1);
            static_cast<QComboBox*>(table->cellWidget(0,2))->setCurrentIndex(1);
            table->item(0,1)->setText("RobotLink");table->item(0,3)->setText("Tool");table->item(0,5)->setText(QStringLiteral("检查理由"));
        }
        confirm(d);EXPECT_TRUE(d->isVisible());EXPECT_EQ(store->query().head().id,base.id);
        EXPECT_FALSE(d->findChild<QLabel*>("policy_error")->text().isEmpty());
        d->findChild<QTableWidget*>("policy_mandatory")->removeRow(0);d->findChild<QTableWidget*>("policy_excluded")->removeRow(0);
        policy::RawPolicyInput other;other.policyObject=policy::systemDefaultPolicyObjectId();other.collision.enabled=false;
        ASSERT_TRUE(HostEngineeringPolicy::submit(*store,base,other,true).committed());
        const auto drift=store->query().head().id;confirm(d);EXPECT_TRUE(d->isVisible());
        EXPECT_EQ(store->query().head().id,drift);EXPECT_TRUE(d->findChild<QLabel*>("policy_error")->text().contains(QStringLiteral("基线")));
        d->reject();
    });
}

/** @brief UX-08：合法规则、范围和来源必须真实提交，再次打开应无损回显；取消不增修订。 */
TEST_F(HostPolicyGuiTest, RulesScopeAndSourceReopenWithoutLoss_UX08_A23)
{
    IRD_TEST_INFO((std::vector<std::string>{"UX-08","ARC-05"}),(std::vector<std::string>{"AT-01"}));
    // 元数据对象随修订轮换，不能用作稳定引用夹具。先创建持久策略对象，
    // 本用例仅检验各引用字段运输/存在性，不把该对象冒充真实模型或任务。
    policy::RawPolicyInput initial;initial.policyObject=policy::systemDefaultPolicyObjectId();
    initial.collision.enabled=false; // 夹具无碰撞间距，显式关闭而不使用Raw的启用缺省。
    ASSERT_TRUE(HostEngineeringPolicy::submit(*store,store->query().head(),initial,true).committed());
    const auto seed=host.resolvePolicyForRevision(store->query().head());ASSERT_TRUE(seed.policy);
    const auto object=seed.policy->policyObject;
    const auto id=QString::fromStdString(object.toCanonical());
    edit([&](QDialog* d) {
        d->findChild<QPushButton*>("policy_mandatory_add")->click();
        auto* table=d->findChild<QTableWidget*>("policy_mandatory");
        static_cast<QComboBox*>(table->cellWidget(0,0))->setCurrentIndex(1);
        static_cast<QComboBox*>(table->cellWidget(0,2))->setCurrentIndex(1);
        table->item(0,1)->setText("RobotLink");table->item(0,3)->setText("Tool");
        static_cast<QComboBox*>(table->cellWidget(0,4))->setCurrentIndex(1);
        table->item(0,5)->setText(QStringLiteral("装配重点检查"));
        for(const auto* field:{"policy_models","policy_tasks","policy_cases","policy_source_object"})text(d,field,id);
        d->findChild<QComboBox*>("policy_origin")->setCurrentIndex(2);
        confirm(d);
    });
    const auto revision=store->query().head();
    const auto resolved=host.resolvePolicyForRevision(revision);ASSERT_TRUE(resolved.policy);
    ASSERT_EQ(resolved.policy->collision.mandatoryPairs.size(),1u);
    EXPECT_EQ(resolved.policy->collision.mandatoryPairs.front().level,policy::PolicyRuleLevel::Should);
    EXPECT_EQ(resolved.policy->applicability.modelObjects,std::vector<core::ObjectId>{object});
    EXPECT_EQ(resolved.policy->applicability.taskObjects,std::vector<core::ObjectId>{object});
    EXPECT_EQ(resolved.policy->applicability.caseObjects,std::vector<core::ObjectId>{object});
    EXPECT_EQ(resolved.policy->origin.sourceObject,std::optional<core::ObjectId>{object});
    edit([&](QDialog* d) {
        EXPECT_TRUE(d->findChild<QLabel*>("policy_source")->text().contains(QStringLiteral("项目策略")));
        auto* table=d->findChild<QTableWidget*>("policy_mandatory");ASSERT_EQ(table->rowCount(),1);
        EXPECT_EQ(table->item(0,5)->text(),QStringLiteral("装配重点检查"));
        for(const auto* field:{"policy_models","policy_tasks","policy_cases","policy_source_object"})
            EXPECT_EQ(d->findChild<QLineEdit*>(field)->text(),id);
        // 保存每一页的真实窗口截图，供视觉复核表格和长引用文本的可用性。
        const auto path=qEnvironmentVariable("IRD_A23_SCREENSHOT");auto* tabs=d->findChild<QTabWidget*>();
        if(!path.isEmpty())for(int page=0;page<tabs->count();++page) {
            tabs->setCurrentIndex(page);QApplication::processEvents();
            EXPECT_TRUE(d->grab().save(path+QStringLiteral("-page%1.png").arg(page)));
        }
        d->reject();
    });
    EXPECT_EQ(store->query().head().id,revision.id);
}

/** @brief UX-08/UX-11：只读项目与未绑定项目不得弹出可写编辑器，也不得创建修订。 */
TEST_F(HostPolicyGuiTest, ReadOnlyAndUnboundNeverOpenWritableEditor_UX11_A23)
{
    IRD_TEST_INFO((std::vector<std::string>{"UX-08","UX-11"}),(std::vector<std::string>{"AT-01"}));
    const auto path=store->canonicalPath();const auto initial=store->query().head().id;
    host.bindProjectStore(static_cast<project::ProjectStore*>(nullptr));
    EXPECT_EQ(store->requestClose(),0u);store.reset();
    project::OpenStoreRequest request;request.path=path;request.mode=project::OpenMode::ReadOnly;
    auto opened=project::ProjectStoreFactory::open(request);store=std::move(opened.store);ASSERT_TRUE(store);
    host.bindProjectStore(store.get());host.edit(nullptr);
    EXPECT_EQ(QApplication::activeModalWidget(),nullptr);EXPECT_EQ(store->query().head().id,initial);
    host.bindProjectStore(static_cast<project::ProjectStore*>(nullptr));host.edit(nullptr);
    EXPECT_EQ(QApplication::activeModalWidget(),nullptr);
}

/** @brief F07/P-MDL-3（WP-13-T25）：导入排除候选预填的诚实缺席面——空项目
 * （无已应用模型）候选不可解析：警示行如实列出未解析对、忽略表零预填、

/** @brief F07/P-MDL-3（WP-13-T25）：导入排除候选预填的诚实缺席面——空项目
 * （无已应用模型）候选不可解析：警示行如实列出未解析对、忽略表零预填、
 * 取消零写入；候选不经确认绝不产生策略内容（policy 权威不变）。*/
TEST_F(HostPolicyGuiTest, ImportedCandidatesUnresolvedListedNeverSilent_MDL04_WP13T25)
{
    IRD_TEST_INFO((std::vector<std::string>{"MDL-04","ARC-05"}),(std::vector<std::string>{"AT-19"}));
    const auto initial=store->query().head();
    // 模态应答模式（同夹具 edit 辅助——50ms 后检视并取消）。
    QTimer timeout;timeout.setSingleShot(true);
    QObject::connect(&timeout,&QTimer::timeout,[] {
        if(auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget()))dialog->reject();
        ADD_FAILURE()<<"策略编辑交互超时";
    });timeout.start(10000);
    QTimer::singleShot(50,[&] {
        auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
        ASSERT_NE(dialog,nullptr)<<"带候选的编辑器未弹出";
        auto* source=dialog->findChild<QLabel*>("policy_source");ASSERT_NE(source,nullptr);
        EXPECT_TRUE(source->text().contains(QStringLiteral("未解析 2 对")))<<"未解析候选必须如实警示（不静默丢弃）";
        EXPECT_TRUE(source->text().contains(QStringLiteral("base ↔ l1")))<<"候选对名称可观察";
        auto* table=dialog->findChild<QTableWidget*>("policy_excluded");ASSERT_NE(table,nullptr);
        EXPECT_EQ(table->rowCount(),0)<<"不可解析候选不得预填（无对象身份不虚构规则）";
        dialog->reject();
    });
    host.edit(nullptr,{{"base","l1"},{"l1","l2"}});
    EXPECT_EQ(store->query().head().id,initial.id)<<"取消零写入";
}
} // namespace sdurws::ird::ui::test
