/**
 * @file   ModelingUiModuleHostWiringGuiTest.cpp
 * @brief  建模模块宿主接线 Widgets 级回归钉（UI-T43——集成模式专属 gating）。
 *
 * 设计依据：
 *   - units/ui.md §7.6（只读条件——L-7 门控输入的面板半区）、§11.2
 *     （IPluginUiModule 装配缝——bindTextResolver/bindCommandSubmit 暂存
 *     时序语义）；
 *   - units/modeling.md §9.7.2/§9.7.3（面板接线与命令目录）；
 *   - 先例：ui/gui_test（QApplication main＋findChild 定位）、
 *     modeling/test/PluginModuleT03BTest.cpp（模块级被测面先例）。
 *
 * ★ gating 说明（集成模式专属）：本文件被测类型 ModelingUiModule 构造期
 * 持 policy 行程评估器符号（makeJointLimitEvaluator——policy/JointLimits.cpp
 * 仅集成模式编译），冒烟模式链接该 obj 即 LNK2019。gating 同
 * PluginModuleT03BTest/ReadinessTest 先例（modeling/CMakeLists.txt TARGET
 * sdurw_kinematics 条件编入）——冒烟口径"目标注册＋include 路径"不受影响。
 *
 * 断言纪律：与 ModelingPanelGuiTest 同款——findChild 公共控件树定位，
 * 不触面板私有面；命令按钮与 modelingDomainCommands() 目录按下标对账
 * （size 相等断言作序漂移守卫）。
 */

#include <gtest/gtest.h>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QWidget>
#include <QDir>
#include <QPixmap>
#include <QTableWidget>
#include <QDialog>
#include <QScrollBar>
#include <QScrollArea> // F08确认完整物性在真实滚动窗口中可达。
#include <QMouseEvent> // 使用既有Widgets事件链检查可见按钮，无新增Qt Test依赖。
#include <QTimer>
#include <QTemporaryDir>
#include <QFile>
#include <QFileInfo> // F08真实中文网格文件选择，路径始终沿Qt UTF-8转换。
#include <functional> // 文件对话嵌套事件的边界动作，不替代域逻辑。
#include <QFileDialog>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator> // 冲突重建后遍历真实树项，确认新修订工具已呈现。
#include <sdurws/ird/modeling/StructureEdit.hpp>
#include <sdurws/ird/ui/SelectionService.hpp>
#include <memory>
#include <sdurws/ird/modeling/ModelingPluginAssembly.hpp>

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯登记

#include <sdurws/ird/ui/UiText.hpp>       // ui::resolveText（生产装配序解析值——UX-02）
#include "plugin/ModelingUiModule.hpp"    // 被测模块（同单元 PRIVATE include 面）
#include "plugin/ModelingCommandFlows.hpp" // CustomChainDeclaration 完整型（UI-T63——module 前向声明的实现 TU 面）
#include "plugin/ModelingPanelWidget.hpp" // 面板具体类型（createPanel 返回值下转型——
                                          // 命令按钮对账需要目录序，QWidget 面不够）
#include "plugin/PanelCommandCatalog.hpp" // modelingDomainCommands（对账目录）

using namespace sdurws::ird;
using namespace sdurws::ird::modeling;

namespace {

/**
 * @brief 差异定位测试的名称端口边界：纯草稿尚未发布，无运行时名可映射。
 * SelectionService本身、模块适配器和面板均真实执行；不替换其选中算法。
 */
struct DiffDraftNameMap final : ui::IUiRuntimeNameMapPort {
    std::optional<core::ObjectId> resolveObjectIdFromRuntimeName(const std::string&) const override
    {return std::nullopt;}
    std::optional<std::string> resolveRuntimeName(const core::ObjectId&) const override
    {return std::nullopt;}
};

/// 命令按钮全集（构造序＝目录序——Qt 子对象按挂树序枚举；诊断历史折叠
/// 钮非命令按钮，按 objectName 剔除）。与 ModelingPanelGuiTest 同款辅助
/// （两文件分目标编入，不共享匿名命名空间——复制面最小化）。
QList<QPushButton*> commandButtonsOf(
    const QWidget& panel, const std::vector<ui::CommandDescriptor>& catalog)
{
    QMap<QString, QPushButton*> byId;
    for (QPushButton* btn : panel.findChildren<QPushButton*>()) {
        if (btn->objectName() == QStringLiteral("ird_modeling_history_toggle")) {
            continue;  // 历史折叠钮不入命令对账（B2 呈现件）
        }
        if (btn->objectName() == QStringLiteral("ird_modeling_more_toggle")) {
            continue;  // 工具区折叠开关不入命令对账（F-502 呈现件——非命令目录按钮）
        }
        if (btn->objectName().startsWith(QStringLiteral("ird_modeling_struct_"))) {
            continue;  // 结构操作钮不入命令对账（UI-T47 呈现件——非命令目录按钮）
        }
        if (btn->objectName().startsWith(QStringLiteral("ird_modeling_edit_"))) {
            continue;  // 编辑页模式钮不入命令对账（UI-T53/T54 呈现件）
        }
        if (btn->objectName().startsWith(QStringLiteral("ird_modeling_base_"))) {
            continue;  // 基座页应用/还原钮不入命令对账（UI-T54 呈现件）
        }
        if (btn->objectName().startsWith(QStringLiteral("ird_param_"))) {
            continue;  // 编辑页参数表内部钮不入命令对账（UI-T53~T55 呈现件——apply/取消/确认区）
        }
        if (btn->objectName().startsWith(QStringLiteral("ird_modeling_cmd_"))) {
            byId.insert(btn->objectName(), btn);  // 命令钮按 id 归位（目录序输出）
        }
    }
    // F-502 折叠重排后子树序≠目录序——按命令钮 objectName 挂载的 id 后缀
    // 恢复目录序（UI-T56；与 ModelingPanelGuiTest 同款）。
    QList<QPushButton*> ordered;
    for (const auto& desc : catalog) {
        const QString key = QStringLiteral("ird_modeling_cmd_")
                            + QString::fromStdString(desc.id);
        if (byId.contains(key)) { ordered.push_back(byId.value(key)); }
    }
    return ordered;
}

}  // namespace

/**
 * @brief MDL-08/UX-13/AT-12：真实命令/模态窗口/面板树定位接线。
 * 只读也能查看；使用真实编辑原语删除末关节，证明不能按旧下标误定位。
 */
TEST(ModelingUiModuleHostWiring, DiffLocatesLiveObjectsAndRefusesDeleted_UX13_AT12)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-08","UX-13"},{"AT-12"},std::nullopt);
    ui::SelectionService::Deps selectionDeps;
    selectionDeps.nameMap=std::make_shared<DiffDraftNameMap>();
    selectionDeps.treeLocator=[](const core::ObjectId&){return false;}; // 未装配共享树，仅观测共享选中真状态。
    ui::SelectionService selection(selectionDeps); // 先于模块构造，确保订阅销毁时服务仍存活。
    ModelingUiModule module;module.seedTemplateSession();
    const auto branch=core::BranchId::generate();const auto revision=core::RevisionId::generate();
    module.bindSessionAnchor(branch,revision);
    module.noteAppliedRevision(revision,core::ObjectId::generate());
    const auto baseline=*module.session().baselineSnapshot;
    ASSERT_FALSE(applyJointFieldEdit(module.session().draft,0,JointEditField::ZeroOffset,JointEditValue{0.25}));
    ASSERT_FALSE(removeJointAt(module.session().draft,5));
    module.session().draft.design.displayName="diff-current";
    ResourceRef resource;resource.resourceId="resource-without-node";
    module.session().draft.design.resourceManifest.push_back(resource);
    module.recomputeReadiness();
    const auto before=module.session().draft;
    int commands=0,selections=0;module.bindCommandSubmit([&](const ui::CommandId&){++commands;});
    std::unique_ptr<QWidget> widget(module.createPanel());
    auto* panel=qobject_cast<ModelingPanelWidget*>(widget.get());ASSERT_NE(panel,nullptr);
    module.attachSelectionService(selection);
    QObject::connect(panel,&ModelingPanelWidget::selectionChanged,panel,[&](const QString&){++selections;});
    widget->resize(1400,900);widget->show();QApplication::processEvents();
    module.setWritable(false);
    auto* command=widget->findChild<QPushButton*>("ird_modeling_cmd_modeling.diff-baseline");
    ASSERT_NE(command,nullptr);EXPECT_TRUE(command->isEnabled());
    QTimer watchdog;
    QObject::connect(&watchdog,&QTimer::timeout,[]{
        ADD_FAILURE()<<"完整差异窗口未按预期关闭";
        if(auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget())){dialog->reject();}
    });watchdog.setSingleShot(true);watchdog.start(5000);
    QTimer::singleShot(0,[&]{
        auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());ASSERT_NE(dialog,nullptr);
        auto* table=dialog->findChild<QTableWidget*>("ird_modeling_diff_table");ASSERT_NE(table,nullptr);
        auto* jump=dialog->findChild<QPushButton*>("ird_modeling_diff_locate");ASSERT_NE(jump,nullptr);
        auto* feedback=dialog->findChild<QLabel*>("ird_modeling_diff_feedback");ASSERT_NE(feedback,nullptr);
        int root=-1,live=-1,removed=-1,res=-1;
        for(int row=0;row<table->rowCount();++row){
            const auto path=table->item(row,2)->text();
            if(path=="displayName"){root=row;}
            if(path=="joints[0].zeroOffset"){live=row;}
            if(path=="joints[5]"){removed=row;}
            if(path.startsWith("resourceManifest[")){res=row;}
        }
        ASSERT_GE(root,0);ASSERT_GE(live,0);ASSERT_GE(removed,0);ASSERT_GE(res,0);
        table->setCurrentCell(root,0);jump->click();
        EXPECT_EQ(panel->selectedAnchor(),module.session().draft.rootObjectId);
        EXPECT_EQ(selection.selectedObjectIds(),std::vector<core::ObjectId>{*module.session().draft.rootObjectId});
        EXPECT_EQ(selection.selectionSource(),ui::SelectionSource::Command);
        table->setCurrentCell(live,0);
        // 双击与按钮均经过生产连接；直接发出控件信号，不绕过定位回调。
        Q_EMIT table->cellDoubleClicked(live,0);
        const auto selected=std::optional<core::ObjectId>(before.design.joints.front().objectId);
        EXPECT_EQ(panel->selectedAnchor(),selected);EXPECT_GE(selections,2);
        EXPECT_EQ(selection.selectedObjectIds(),std::vector<core::ObjectId>{*selected});
        EXPECT_EQ(selection.selectionSource(),ui::SelectionSource::Command);
        auto* tree=widget->findChild<QTreeWidget*>("ird_modeling_struct_tree");ASSERT_NE(tree,nullptr);
        ASSERT_NE(tree->currentItem(),nullptr);EXPECT_TRUE(tree->currentItem()->text(0).contains("j1"));
        EXPECT_TRUE(feedback->text().contains(QStringLiteral("已定位")));
        // 相同命令选中被服务幂等省略广播时，重复定位仍恢复域面板，不能假成功。
        panel->focusObject(module.session().draft.rootObjectId);
        jump->click();EXPECT_EQ(panel->selectedAnchor(),selected);
        table->setCurrentCell(removed,0);jump->click();
        EXPECT_EQ(panel->selectedAnchor(),selected);
        EXPECT_EQ(selection.selectedObjectIds(),std::vector<core::ObjectId>{*selected});
        EXPECT_TRUE(feedback->text().contains(QStringLiteral("无法定位")));
        table->setCurrentCell(res,0);jump->click();
        EXPECT_EQ(panel->selectedAnchor(),selected);
        EXPECT_TRUE(feedback->text().contains(QStringLiteral("没有独立")));
        const auto out=qEnvironmentVariable("IRD_MODELING_GUI_EVIDENCE_OUT");
        if(!out.isEmpty()){
            ASSERT_TRUE(QDir().mkpath(out));
            ASSERT_TRUE(dialog->grab().save(QDir(out).filePath("diff-deleted-refused.png")));
        }
        // 已装配后退订是关闭/重装配边界，必须拒绝并保持两份既有状态。
        module.detachSelectionService();table->setCurrentCell(root,0);jump->click();
        EXPECT_EQ(panel->selectedAnchor(),selected);
        EXPECT_EQ(selection.selectedObjectIds(),std::vector<core::ObjectId>{*selected});
        EXPECT_TRUE(feedback->text().contains(QStringLiteral("尚未连接")));
        dialog->reject();
    });
    // 正式按钮在本夹具只发往宿主命令端；直接驱动生产模块落点保持同一路由。
    EXPECT_TRUE(module.executeDomainCommand("modeling.diff-baseline"));
    EXPECT_EQ(module.session().draft,before);EXPECT_EQ(*module.session().baselineSnapshot,baseline);
    EXPECT_EQ(module.session().branch,branch);EXPECT_EQ(module.session().baseRevision,revision);
    EXPECT_EQ(commands,0);
    module.detachSelectionService();
}

TEST(ModelingUiModuleHostWiring, DraftHistoryRestoresFieldsAndStructureWithoutRevision_PM18_L5)
{
    IRD_TEST_INFO("PM-18",{},std::nullopt);
    ModelingUiModule module;module.seedTemplateSession();
    const auto branch=core::BranchId::generate();const auto revision=core::RevisionId::generate();
    module.bindSessionAnchor(branch,revision);
    module.noteAppliedRevision(revision,core::ObjectId::generate());
    int submitted=0;module.bindCommandSubmit([&](const ui::CommandId&){++submitted;});
    std::unique_ptr<QWidget> widget(module.createPanel());
    auto* panel=qobject_cast<ModelingPanelWidget*>(widget.get());ASSERT_NE(panel,nullptr);
    auto* undo=widget->findChild<QPushButton*>("ird_modeling_draft_undo");
    auto* redo=widget->findChild<QPushButton*>("ird_modeling_draft_redo");
    ASSERT_NE(undo,nullptr)<<"F17：草稿局部撤销入口缺失";
    ASSERT_NE(redo,nullptr)<<"F17：草稿局部重做入口缺失";
    EXPECT_FALSE(undo->isEnabled());EXPECT_FALSE(redo->isEnabled());
    const auto original=module.session().draft;
    // 使用真实接受流（同生产数值编辑出口），并点击真实结构与历史按钮。
    EXPECT_EQ(submitJointFieldEdit(module.session().draft,*panel,0,JointEditField::ZeroOffset,
        JointEditValue{0.25}),EditSubmitOutcome::Applied);
    const auto first=module.session().draft;EXPECT_TRUE(undo->isEnabled());
    panel->focusObject(first.design.joints.front().objectId);
    auto* add=widget->findChild<QPushButton*>("ird_modeling_struct_add");ASSERT_NE(add,nullptr);
    add->click();const auto second=module.session().draft;
    ASSERT_EQ(second.design.joints.size(),original.design.joints.size()+1);
    module.recomputeReadiness(); // 纯重算不得制造额外撤销步。
    undo->click();EXPECT_EQ(module.session().draft,first);
    undo->click();EXPECT_EQ(module.session().draft,original);
    EXPECT_FALSE(undo->isEnabled());EXPECT_TRUE(redo->isEnabled());
    redo->click();EXPECT_EQ(module.session().draft,first);
    redo->click();EXPECT_EQ(module.session().draft,second);
    EXPECT_FALSE(redo->isEnabled());
    module.setWritable(false);EXPECT_FALSE(undo->isEnabled());
    undo->click();EXPECT_EQ(module.session().draft,second);
    module.setWritable(true);undo->click();EXPECT_EQ(module.session().draft,first);
    EXPECT_EQ(submitJointFieldEdit(module.session().draft,*panel,0,JointEditField::ZeroOffset,
        JointEditValue{0.5}),EditSubmitOutcome::Applied);
    EXPECT_FALSE(redo->isEnabled())<<"分叉编辑必须清除旧重做";
    undo->click();EXPECT_EQ(module.session().draft,first);
    EXPECT_EQ(module.session().branch,branch);EXPECT_EQ(module.session().baseRevision,revision);
    EXPECT_EQ(submitted,0)<<"局部撤销不得走项目命令通道";
    module.noteAppliedRevision(core::RevisionId::generate(),original.rootObjectId);
    EXPECT_FALSE(undo->isEnabled());EXPECT_FALSE(redo->isEnabled());
    widget->resize(1100,850);widget->show();QApplication::processEvents();
    const auto out=qEnvironmentVariable("IRD_MODELING_GUI_EVIDENCE_OUT");
    if(!out.isEmpty()){
        ASSERT_TRUE(QDir().mkpath(out));
        ASSERT_TRUE(widget->grab().save(QDir(out).filePath("draft-history.png")));
    }
}

/** @brief MDL-12/PM-18：工程范围确认参与草稿局部历史，不产生项目命令或修订。 */
TEST(ModelingUiModuleHostWiring, ContinuousRangeDraftHistory_MDL12_AT17)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-12","PM-18"},std::vector<std::string>{"AT-17"});
    ModelingUiModule module;module.seedTemplateSession();
    const auto branch=core::BranchId::generate();
    const auto revision=core::RevisionId::generate();
    module.bindSessionAnchor(branch,revision);
    auto& joint=module.session().draft.design.joints[0];joint.type=JointType::Continuous;
    joint.bounds=core::SourcedValue<JointLimits>::notApplicable();
    joint.workingRange=core::SourcedValue<JointLimits>::notProvided();
    module.recomputeReadiness(); // 将导入形态设为局部历史起点，不把测试准备当用户编辑。
    module.noteAppliedRevision(revision,core::ObjectId::generate());
    int commands=0;module.bindCommandSubmit([&](const ui::CommandId&){++commands;});
    std::unique_ptr<QWidget> widget(module.createPanel());
    auto* panel=qobject_cast<ModelingPanelWidget*>(widget.get());ASSERT_NE(panel,nullptr);
    const auto before=module.session().draft;
    EXPECT_EQ(submitJointFieldEdit(module.session().draft,*panel,0,JointEditField::WorkingRange,
        JointEditValue{JointLimits{-8,10}}),EditSubmitOutcome::Applied);
    const auto confirmed=module.session().draft;
    widget->findChild<QPushButton*>("ird_modeling_draft_undo")->click();EXPECT_EQ(module.session().draft,before);
    widget->findChild<QPushButton*>("ird_modeling_draft_redo")->click();EXPECT_EQ(module.session().draft,confirmed);
    EXPECT_EQ(module.session().baseRevision,revision);EXPECT_EQ(module.session().branch,branch);EXPECT_EQ(commands,0);
}

TEST(ModelingUiModuleHostWiring, DraftHistoryClearsOnDetachAndRejectDoesNotEnter_PM18_L5)
{
    IRD_TEST_INFO("PM-18",{},std::nullopt);
    ModelingUiModule module;module.seedTemplateSession();
    std::unique_ptr<QWidget> widget(module.createPanel());
    auto* panel=qobject_cast<ModelingPanelWidget*>(widget.get());ASSERT_NE(panel,nullptr);
    auto* undo=widget->findChild<QPushButton*>("ird_modeling_draft_undo");
    auto* redo=widget->findChild<QPushButton*>("ird_modeling_draft_redo");
    ASSERT_NE(undo,nullptr);ASSERT_NE(redo,nullptr);
    const auto original=module.session().draft;
    EXPECT_EQ(submitJointFieldEdit(module.session().draft,*panel,0,JointEditField::ZeroOffset,
        JointEditValue{0.25}),EditSubmitOutcome::Applied);
    const auto accepted=module.session().draft;
    EXPECT_EQ(submitJointFieldEdit(module.session().draft,*panel,0,JointEditField::Bounds,
        JointEditValue{std::pair<double,double>{2.0,-2.0}}),EditSubmitOutcome::Rejected);
    EXPECT_EQ(module.session().draft,accepted);
    undo->click();EXPECT_EQ(module.session().draft,original);
    EXPECT_FALSE(undo->isEnabled());EXPECT_TRUE(redo->isEnabled());
    module.onSessionDetached();
    EXPECT_FALSE(undo->isEnabled());EXPECT_FALSE(redo->isEnabled());
    module.seedTemplateSession();
    EXPECT_FALSE(undo->isEnabled());EXPECT_FALSE(redo->isEnabled());
}

/** @brief L-5：部件与根引用必须一起回退，未确认表单不能覆盖恢复后的工作集。 */
TEST(ModelingUiModuleHostWiring, DraftHistoryRestoresComponentAndCancelsPending_PM18_L5)
{
    IRD_TEST_INFO("PM-18",{},std::nullopt);
    ModelingUiModule module;module.seedTemplateSession();
    std::unique_ptr<QWidget> widget(module.createPanel());
    auto* panel=qobject_cast<ModelingPanelWidget*>(widget.get());ASSERT_NE(panel,nullptr);
    bool dirty=false;
    QObject::connect(panel,&ModelingPanelWidget::sessionDirtyChanged,widget.get(),
                     [&](bool value){dirty=value;});
    auto* undo=widget->findChild<QPushButton*>("ird_modeling_draft_undo");
    auto* redo=widget->findChild<QPushButton*>("ird_modeling_draft_redo");
    ASSERT_NE(undo,nullptr);ASSERT_NE(redo,nullptr);
    const auto original=module.session().draft;
    // 真实传动表单接受链同时新建部件和根引用，不能只撤销表格里的数值。
    QWidget* host=nullptr;QTableWidget* table=nullptr;int row=-1;
    for(auto* candidate:widget->findChildren<QWidget*>("ird_param_table_panel")){
        auto* t=candidate->findChild<QTableWidget*>("ird_param_table");
        if(!t){continue;}
        for(int r=0;r<t->rowCount();++r){
            if(t->item(r,0)->data(Qt::UserRole).toString()=="dt-ratio-0"){
                host=candidate;table=t;row=r;break;
            }
        }
        if(host){break;}
    }
    ASSERT_NE(host,nullptr);ASSERT_NE(table,nullptr);ASSERT_GE(row,0);
    table->item(row,1)->setText("100");QApplication::processEvents();
    host->findChild<QPushButton*>("ird_param_apply")->click();
    host->findChild<QPushButton*>("ird_param_confirm_yes")->click();
    const auto accepted=module.session().draft;
    ASSERT_TRUE(accepted.drivetrainObject.has_value());
    ASSERT_TRUE(accepted.design.drivetrainRef.has_value());
    EXPECT_TRUE(dirty);
    // 编辑但不确认，再撤销：清除暂存和确认区，避免重做/后续确认写回旧输入。
    table->item(row,1)->setText("200");QApplication::processEvents();
    host->findChild<QPushButton*>("ird_param_apply")->click();
    ASSERT_FALSE(host->findChild<QWidget*>("ird_param_confirm_region")->isHidden());
    undo->click();EXPECT_EQ(module.session().draft,original);
    EXPECT_FALSE(dirty);
    EXPECT_TRUE(host->findChild<QWidget*>("ird_param_confirm_region")->isHidden());
    redo->click();EXPECT_EQ(module.session().draft,accepted);
    EXPECT_DOUBLE_EQ(table->item(row,1)->text().toDouble(),100.0);
    EXPECT_TRUE(dirty);
}

// =====================================================================
// UI-T43：生产装配序下的文案解析回归（审核建议 4 的实质面——
// DomainAssembly::assembleDomainPlugins 对建模域先 bindTextResolver 后经
// 面板工厂 createPanel 的顺序，必须产出全中文按钮文本；titleKey 原文＝
// "cmd." 前缀内部键，泄漏即 UX-02 违约）。此前该绑定零测试钉住。
// =====================================================================

TEST(ModelingUiModuleHostWiring, TextResolverBoundBeforePanelCreation_NoInternalKeyLeak_UX02_UI_T43)
{
    IRD_TEST_INFO("UX-02", {}, std::nullopt);

    ModelingUiModule module;
    module.seedTemplateSession();
    // 生产装配序复刻（DomainAssembly.cpp 装配段同序）：解析器先于面板
    // 工厂——走模块暂存面（m_textResolver→createPanel 应用），装配序
    // 无关性同时被本用例覆盖。
    module.bindTextResolver([](const std::string& key) {
        const std::string text = ui::resolveText(key);
        return QString::fromStdString(text.empty() ? key : text);
    });
    module.bindCommandSubmit([](const ui::CommandId&) {});

    QWidget* widget = module.createPanel();
    ASSERT_NE(widget, nullptr);
    const auto catalog = modelingDomainCommands();
    const auto buttons = commandButtonsOf(*widget, catalog);
    ASSERT_EQ(buttons.size(), catalog.size())
        << "命令按钮与目录错位（构造序漂移）";

    for (std::size_t i = 0; i < buttons.size(); ++i) {
        // 标题＝UiText 解析值（键已登记非空——解析空回退键名原文的兜底
        // 形态在本套目录下不应出现，出现即 UiText 登记缺失）。
        const std::string expected = ui::resolveText(catalog[i].titleKey);
        ASSERT_FALSE(expected.empty()) << "UiText 缺标题登记：" << catalog[i].titleKey;
        EXPECT_EQ(buttons[i]->text(), QString::fromStdString(expected))
            << "标题未按宿主解析器呈现工程中文：" << catalog[i].id;
        EXPECT_FALSE(buttons[i]->text().startsWith(QStringLiteral("cmd.")))
            << "按钮文本泄漏内部命令键（UX-02 违约）：" << catalog[i].id;
    }
    // tooltip 同源复验（B4 反断言在"模块暂存解析器"生产序下的回归形态）：
    // 悬停文案不得是裸命令 id。
    for (const QPushButton* btn : buttons) {
        for (const auto& desc : catalog) {
            EXPECT_NE(btn->toolTip(), QString::fromStdString(desc.id))
                << "tooltip 呈现裸命令 id：" << desc.id;
        }
    }
}

// =====================================================================
// UI-T43：模块可写链路（新增 setWritable 面）——①面板创建前切换只读＝
// 暂存初值（createPanel 按只读创建——装配序无关，杜绝面板恒按可写创建
// 的初值失实）；②面板创建后切换＝即时转发（双向）。可用性快照恒全放行
// ——使能态只随 writable 翻转，即链路转发的直接观测面。
// =====================================================================

TEST(ModelingUiModuleHostWiring, WritableChain_PendingInitialAndLiveForward_L7_UI_T43)
{
    IRD_TEST_INFO("ERR-01", {}, std::nullopt);

    ModelingUiModule module;
    module.seedTemplateSession();
    module.setWritable(false);  // 面板创建前——暂存初值形态
    module.bindCommandSubmit([](const ui::CommandId&) {});
    module.bindCommandAvailability([](const ui::CommandId&) {
        return ui::CommandAvailability{true, true, true, ui::DisableReason{}};
    });

    QWidget* widget = module.createPanel();
    ASSERT_NE(widget, nullptr);
    const auto catalog = modelingDomainCommands();
    const auto buttons = commandButtonsOf(*widget, catalog);
    ASSERT_EQ(buttons.size(), catalog.size());

    // ①初始态兑现：写命令按只读禁用、只读命令可用（修复前面板恒按
    //   可写创建，全部亮起）。
    for (std::size_t i = 0; i < buttons.size(); ++i) {
        EXPECT_EQ(buttons[i]->isEnabled(), catalog[i].readOnlyAllowed)
            << "面板初始可写态未兑现模块暂存值：" << catalog[i].id;
    }

    // ②创建后即时转发（恢复→再降级双向）——面板指针在模块侧，宿主经
    //   门面 setWritable 驱动（本用例直驱模块同语义，转发链少一层）。
    module.setWritable(true);
    for (const QPushButton* btn : buttons) {
        EXPECT_TRUE(btn->isEnabled()) << "恢复可写未即时转发面板";
    }
    module.setWritable(false);
    for (std::size_t i = 0; i < buttons.size(); ++i) {
        EXPECT_EQ(buttons[i]->isEnabled(), catalog[i].readOnlyAllowed)
            << "再降级只读未即时转发面板：" << catalog[i].id;
    }
}

// =====================================================================
// UI-T59：appliedPreview 推送接线缺口修复——UI-T41 A3 残留（模块有存有清
// 但从未推送面板，预览页恒空态）。本组用例钉扎预览态三个变迁点。
// =====================================================================

/**
 * 预览态变迁推送链（UI-T59）：createPanel 即接线（构造后空态）→
 * noteAppliedRevision 定格即推送（摘要呈现——修复前恒空态的缺口面）→
 * onRevisionCommitted（撤销/重做形态）失效即推送（空态诚实回落）；
 * bindWorkCellPreview 晚绑定＝面板供给器即时补线（XML 类请求达宿主）。
 */
TEST(ModelingUiModuleHostWiring, AppliedPreviewPushChain_Fix_UI_T59)
{
    IRD_TEST_INFO("D-MDL-10", {}, std::nullopt);

    ModelingUiModule module;
    module.seedTemplateSession();

    // 预览供给替身（宿主形态——缺席轨迹返回 false；应答轨迹返回 stub 文本
    // ＋来源头行。本用例直调模块绑定面（module.bindWorkCellPreview）——
    // 装配门面转发器的接线语义由 UiPlugin 装配面承载，模块级用例不经过
    // 门面（F-519③ 表述修正：原注"经装配门面绑定"与实际直调不符））。
    bool asked = false;
    module.bindWorkCellPreview(
        [&asked](const std::string& kind, std::string& headerLine,
                 std::string& sourceObject, std::string& text,
                 std::string&) -> bool {
            asked = true;
            headerLine = "来源修订 stub｜模型身份 stub｜生成于 stub";
            sourceObject = "stub=1";
            text = "<ird-" + kind + "-stub/>";
            return true;
        });

    QWidget* widget = module.createPanel();
    ASSERT_NE(widget, nullptr);
    QPlainTextEdit* preview =
        widget->findChild<QPlainTextEdit*>(QStringLiteral("ird_modeling_preview_text"));
    ASSERT_NE(preview, nullptr) << "预览页承载（区⑤）";
    QComboBox* combo =
        widget->findChild<QComboBox*>(QStringLiteral("ird_modeling_preview_kind"));
    ASSERT_NE(combo, nullptr);

    // ①创建后即空态（种子会话无已应用修订——D-MDL-10 占位）。
    EXPECT_EQ(combo->currentIndex(), 0);
    EXPECT_TRUE(preview->toPlainText().contains(QStringLiteral("D-MDL-10")));

    // ②XML 类经供给器（晚绑定转发器已补线——请求达宿主替身）。
    combo->setCurrentIndex(1);
    QApplication::processEvents();
    EXPECT_TRUE(asked) << "bindWorkCellPreview 应转发面板供给器";
    EXPECT_TRUE(preview->toPlainText().contains(QStringLiteral("<ird-serial-device-xml-stub/>")));

    // ③定格即推送（修复核心断言——修复前 appliedPreview 只存不推，预览页
    //   恒停留构造初态）：切回摘要轨→noteAppliedRevision→摘要呈现。
    combo->setCurrentIndex(0);
    QApplication::processEvents();
    const core::RevisionId appliedRev = core::RevisionId::generate();
    module.noteAppliedRevision(appliedRev, std::nullopt);
    QApplication::processEvents();
    EXPECT_FALSE(preview->toPlainText().contains(QStringLiteral("D-MDL-10")))
        << "已应用修订定格后预览页不应停留空态（UI-T41 A3 残留缺口修复）";
    EXPECT_TRUE(preview->toPlainText().contains(QStringLiteral("已应用修订快照")))
        << "摘要轨呈现定格内容（generic-6r 种子草稿）";

    // ④失效即推送（撤销/重做形态——锚定分支事件触发复位，空态诚实回落）。
    const core::BranchId branch = core::BranchId::generate();
    module.bindSessionAnchor(branch, appliedRev);
    module.onRevisionCommitted(branch, core::RevisionId::generate());
    QApplication::processEvents();
    EXPECT_TRUE(preview->toPlainText().contains(QStringLiteral("D-MDL-10")))
        << "修订内容未知＝预览页空态诚实呈现（不虚构）";
}

/**
 * 规范包清单预览链（UI-T61——F-498 余项）：combo 第六项走模块本地应答
 * （数据源＝会话基线定格内容，不经宿主编译快照回调——asked 替身保持
 * 未触状态）；未应用＝诚实缺席；noteAppliedRevision 定格后（推送触发
 * 重渲）清单文本呈现（标题＋修订头行）；撤销/重做失效回落缺席态。
 */
TEST(ModelingUiModuleHostWiring, PackageChecklistPreviewChain_UI_T61)
{
    IRD_TEST_INFO("MDL-20", {}, std::nullopt);

    ModelingUiModule module;
    module.seedTemplateSession();
    // 宿主 XML 类替身（不被本链触碰——规范包清单走模块本地应答）。
    bool hostAsked = false;
    module.bindWorkCellPreview(
        [&hostAsked](const std::string&, std::string&, std::string&,
                     std::string&, std::string&) -> bool {
            hostAsked = true;
            return false;
        });

    QWidget* widget = module.createPanel();
    ASSERT_NE(widget, nullptr);
    QComboBox* combo =
        widget->findChild<QComboBox*>(QStringLiteral("ird_modeling_preview_kind"));
    ASSERT_NE(combo, nullptr);
    ASSERT_EQ(combo->count(), 6) << "六类预览（含规范包清单——UI-T61）";
    QPlainTextEdit* preview =
        widget->findChild<QPlainTextEdit*>(QStringLiteral("ird_modeling_preview_text"));
    ASSERT_NE(preview, nullptr);

    // 未应用修订：切第六项→诚实缺席（不虚构清单）。缺席文案按 kind 分流
    // （F-521——清单轨闭包语义措辞，与 XML 轨"编译产物"措辞区分）。
    combo->setCurrentIndex(5);
    QApplication::processEvents();
    EXPECT_TRUE(preview->toPlainText().contains(QStringLiteral("预览不可用")));
    EXPECT_TRUE(preview->toPlainText().contains(QStringLiteral("对象级逐项清单")))
        << "清单轨缺席文案未按 kind 分流（F-521）";
    EXPECT_FALSE(hostAsked) << "规范包清单不经宿主回调（模块本地应答）";

    // 应用定格（noteAppliedRevision＝基线定格＋预览推送→重渲）：清单文本
    // 呈现（标题行＋来源修订头行——generic-6r 种子的 authority 组行在场）。
    const core::RevisionId appliedRev = core::RevisionId::generate();
    module.noteAppliedRevision(appliedRev, std::nullopt);
    QApplication::processEvents();
    EXPECT_TRUE(preview->toPlainText().contains(
                    QStringLiteral("规范包 roundtrip 逐项清单")))
        << "清单文本呈现（基线定格内容——generic-6r 草稿）";
    EXPECT_TRUE(preview->toPlainText().contains(QStringLiteral("authority | ")))
        << "authority 组行在场（关节 DH 逐项）";
    QLabel* header =
        widget->findChild<QLabel*>(QStringLiteral("ird_modeling_preview_source"));
    ASSERT_NE(header, nullptr);
    EXPECT_TRUE(header->text().contains(
                    QStringLiteral("内容定格于应用时刻")))
        << "来源头行（修订＋定格语义）";

    // 撤销/重做形态失效→回落缺席态（D-MDL-10——不虚构内容）。
    const core::BranchId branch = core::BranchId::generate();
    module.bindSessionAnchor(branch, appliedRev);
    module.onRevisionCommitted(branch, core::RevisionId::generate());
    QApplication::processEvents();
    EXPECT_TRUE(preview->toPlainText().contains(QStringLiteral("预览不可用")))
        << "基线失效后清单缺席（诚实语义）";
}

// =====================================================================
// UI-T63：custom-chain 六轴声明从零创建（模块 reseedCustomChain 真实域
// 原语组合——createDraft(custom-chain 1 轴种子)→addJointAt 补齐→
// applyJointFieldEdit 逐轴声明 UserProvided）
// =====================================================================

/**
 * 声明重种子组合链（UI-T63）：未接线声明（null deps 形态＝flows 侧装配
 * 缺陷，模块直调面 null decls 不适用——本用例直调 reseedCustomChain）：
 * ①恰六轴校验（非六拒绝会话不变）；②合法声明→草稿六轴全 Revolute＋
 * 声明值逐项 UserProvided 落位（轴线/Origin XYZ＋RPY 逐字）＋变更账面
 * ＝待应用编辑＋基线/已应用预览失效；③rootObjectId 保留。
 */
TEST(ModelingUiModuleHostWiring, ReseedCustomChainComposition_UI_T63)
{
    IRD_TEST_INFO("MDL-02", {}, std::nullopt);

    ModelingUiModule module;
    module.seedTemplateSession();
    const auto rootBefore = module.session().draft.rootObjectId;

    // ①轴数拒绝（非六——R1 合规创建面；会话字节不变）。
    CustomChainDeclaration bad;
    bad.joints.assign(4, CustomChainJointSpec{});
    std::string summary;
    EXPECT_FALSE(module.reseedCustomChain(bad, summary));
    EXPECT_NE(summary.find("恰六轴"), std::string::npos);
    EXPECT_EQ(module.session().draft.design.joints.size(), std::size_t{6})
        << "拒绝路径会话不变（generic-6r 种子仍在）";

    // ②合法六轴声明：J2 轴 z 反向＋J4 Origin 位置/RPY 逐字（从零定义核验）。
    CustomChainDeclaration decls;
    decls.joints.assign(6, CustomChainJointSpec{});
    decls.joints[1].axisZ = -1.0;
    decls.joints[3].x = 0.25;
    decls.joints[3].z = 0.10;
    decls.joints[3].yaw = 0.5;
    decls.joints[5].upper = 1.2;
    EXPECT_TRUE(module.reseedCustomChain(decls, summary)) << summary;

    const auto& ws = module.session().draft;
    ASSERT_EQ(ws.design.joints.size(), std::size_t{6});
    for (const auto& j : ws.design.joints) {
        EXPECT_EQ(j.type, JointType::Revolute) << "R1 合规创建面＝全旋转";
    }
    // 声明逐项 UserProvided 落位（轴/Origin 逐字——域 applyJointFieldEdit
    // 组合的真实效果，插件零计算逻辑）。
    EXPECT_DOUBLE_EQ((*ws.design.joints[1].axis.tryValue())[2], -1.0);
    // Origin：JointPose d()/r() 面——roll/pitch＝0、yaw＝0.5 的旋转核验
    // （R(1,0)＝sin yaw／R(0,0)＝cos yaw——与域 ZYX 正解互证，无外联符号）。
    const auto& o4 = ws.design.joints[3].origin.tryValue()->d();
    EXPECT_DOUBLE_EQ(o4[0], 0.25);
    EXPECT_DOUBLE_EQ(o4[2], 0.10);
    const auto& R4 = ws.design.joints[3].origin.tryValue()->r();
    EXPECT_NEAR(R4(1, 0), std::sin(0.5), 1e-12);
    EXPECT_NEAR(R4(0, 0), std::cos(0.5), 1e-12);
    EXPECT_DOUBLE_EQ(ws.design.joints[5].bounds.tryValue()->second, 1.2);
    EXPECT_FALSE(ws.changes.empty()) << "声明编辑＝待应用账面";
    EXPECT_EQ(ws.rootObjectId, rootBefore) << "根身份保留（种子替换语义）";
    // 基线/已应用预览失效（D-MDL-10——预览页回落空态）。
    EXPECT_FALSE(module.session().baselineSnapshot.has_value());
    EXPECT_FALSE(module.session().appliedPreview.has_value());
}

/** @brief F13/MDL-17：命名位姿页必须提供独立的会话恢复入口，未装配时禁用。 */
TEST(ModelingUiModuleHostWiring, NamedPoseRestoreEntryIsPresent_MDL17_AT04)
{
    IRD_TEST_INFO("MDL-17",{},std::nullopt);
    ModelingUiModule module;module.seedTemplateSession();
    std::unique_ptr<QWidget> widget(module.createPanel());
    auto* restore=widget->findChild<QPushButton*>("ird_modeling_pose_restore");
    ASSERT_NE(restore,nullptr)<<"F13：命名位姿恢复入口缺失";
    EXPECT_FALSE(restore->isEnabled());
}

/** @brief MDL-17/AT-04：真实恢复按钮取保存条目，使用已应用零偏，且只读也可恢复会话。 */
TEST(ModelingUiModuleHostWiring, NamedPoseRestoreUsesAppliedOffsetsAndIgnoresStaging_MDL17_AT04)
{
    IRD_TEST_INFO("MDL-17",{},std::nullopt);
    ModelingUiModule module;module.seedTemplateSession();
    const auto branch=core::BranchId::generate();const auto rev=core::RevisionId::generate();
    module.bindSessionAnchor(branch,rev);
    module.session().draft.design.joints[0].zeroOffset=0.2; // rad，已应用基线原始零偏。
    PoseSet poses;poses.objectId=core::ObjectId::generate();
    poses.entries.push_back(PoseSetEntry{"inspection",std::vector<double>(6,0.5),"检查姿态"});
    module.session().draft.poseSetObject=poses;
    module.session().draft.design.poseSetRef=poses.objectId;
    module.noteAppliedRevision(rev,core::ObjectId::generate());
    int restored=0;ModelingSessionPoseRequest captured;
    module.bindSessionPoseWriter([&](const ModelingSessionPoseRequest& request,std::string&){++restored;captured=request;return true;});
    std::unique_ptr<QWidget> widget(module.createPanel());
    auto* panel=qobject_cast<ModelingPanelWidget*>(widget.get());ASSERT_NE(panel,nullptr);
    panel->focusObject(poses.objectId);
    auto* restore=widget->findChild<QPushButton*>("ird_modeling_pose_restore");ASSERT_NE(restore,nullptr);
    ASSERT_TRUE(restore->isEnabled());
    // 当前草稿零偏已改，尚未应用；会话恢复只能按冻结基线的0.2换算。
    EXPECT_EQ(submitJointFieldEdit(module.session().draft,*panel,0,JointEditField::ZeroOffset,
              JointEditValue{0.8}),EditSubmitOutcome::Applied);
    panel->focusObject(poses.objectId);
    auto* poseArea=widget->findChild<QWidget*>("ird_modeling_pose_area");ASSERT_NE(poseArea,nullptr);
    auto* table=poseArea->findChild<QTableWidget*>("ird_param_table");ASSERT_NE(table,nullptr);
    for(int row=0;row<table->rowCount();++row){
            if(table->item(row,0)->data(Qt::UserRole).toString()=="pose-config-0"){
                table->item(row,1)->setText("0.9"); // 未确认，不能改变本次恢复目标。
            }
    }
    QApplication::processEvents();const auto before=module.session().draft;
    module.setWritable(false);EXPECT_TRUE(restore->isEnabled());restore->click();
    ASSERT_EQ(restored,1);ASSERT_EQ(captured.jointConfiguration.size(),6U);
    EXPECT_NEAR(captured.jointConfiguration[0],0.3,1e-15);
    EXPECT_EQ(captured.branch,branch);EXPECT_EQ(captured.rootObjectId,*before.rootObjectId);
    EXPECT_EQ(module.session().draft,before);EXPECT_EQ(module.session().baseRevision,rev);
    module.session().draft.poseSetObject->entries[0].jointConfiguration.pop_back();
    restore->click();EXPECT_EQ(restored,1)<<"维度失配不能调用宿主写入";
    module.bindSessionPoseWriter({});EXPECT_FALSE(restore->isEnabled());
}

/** @brief MDL-17：空位姿集也能从可见模式入口进入管理页，不依赖已存在对象。 */
TEST(ModelingUiModuleHostWiring, NamedPosePageReachableWithoutExistingObject_MDL17_AT04)
{
    IRD_TEST_INFO("MDL-17",{},std::nullopt);
    ModelingUiModule module;module.seedTemplateSession();
    std::unique_ptr<QWidget> widget(module.createPanel());widget->show();QApplication::processEvents();
    auto* mode=widget->findChild<QPushButton*>("ird_modeling_edit_pose_mode");
    ASSERT_NE(mode,nullptr)<<"空位姿集没有树锚，必须有可见入口创建首个命名姿态";
    ASSERT_TRUE(mode->isVisible());mode->click();
    auto* add=widget->findChild<QPushButton*>("ird_modeling_pose_add");ASSERT_NE(add,nullptr);
    EXPECT_TRUE(add->isVisible());add->click();
    ASSERT_TRUE(module.session().draft.poseSetObject.has_value());
    EXPECT_EQ(module.session().draft.poseSetObject->entries.size(),1U);
}

/** @brief MDL-17：关闭重开后经当前可见表格保存首个姿态，不访问旧表单模型。 */
TEST(ModelingUiModuleHostWiring, NamedPoseCanSaveAfterSessionReopen_MDL17_AT04)
{
    IRD_TEST_INFO("MDL-17",{},std::nullopt);
    ModelingUiModule module;module.seedTemplateSession();
    std::unique_ptr<QWidget> widget(module.createPanel());widget->show();QApplication::processEvents();
    module.onSessionDetached();module.seedTemplateSession();QApplication::processEvents();
    auto* mode=widget->findChild<QPushButton*>("ird_modeling_edit_pose_mode");ASSERT_NE(mode,nullptr);
    mode->click();widget->findChild<QPushButton*>("ird_modeling_pose_add")->click();
    QApplication::processEvents();
    auto* area=widget->findChild<QWidget*>("ird_modeling_pose_area");ASSERT_NE(area,nullptr);
    auto* table=area->findChild<QTableWidget*>("ird_param_table");ASSERT_NE(table,nullptr);
    widget->findChild<QLineEdit*>("ird_modeling_pose_key")->setText("reopened-inspection");
    ASSERT_NO_THROW(table->item(0,1)->setText("0.45"));
    QApplication::processEvents();
    ASSERT_NO_THROW(widget->findChild<QPushButton*>("ird_modeling_pose_apply")->click());
    const auto& entries=module.session().draft.poseSetObject->entries;
    const auto saved=std::find_if(entries.begin(),entries.end(),[](const auto& e){return e.key=="reopened-inspection";});
    ASSERT_NE(saved,entries.end());EXPECT_DOUBLE_EQ(saved->jointConfiguration[0],0.45);
}


namespace {
/**
 * @brief MDL-05模态窗口超时守卫；仅本用例UI线程拥有定时器，析构停止。
 * 失败时关闭真实窗口避免阻塞后续串行GUI，不代替窗口校验。
 */
class PropertyModalWatchdog {
public:
    PropertyModalWatchdog()
    {
        QObject::connect(&timer,&QTimer::timeout,[]{
            ADD_FAILURE()<<"生产物性窗口未按预期响应";
            if(auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget())){dialog->reject();}
        });
        timer.setSingleShot(true);timer.start(5000);
    }
private:
    QTimer timer; ///< 本用例UI线程管理，不保留窗口指针。
};
/** @brief 保存当前真实物性窗口；[in] name为唯一图片名，目录由runner指定。 */
void propertySnapshot(QWidget& dialog,const QString& name)
{
    const auto out=qEnvironmentVariable("IRD_MODELING_GUI_EVIDENCE_OUT");
    if(out.isEmpty()){return;}
    ASSERT_TRUE(QDir().mkpath(out));
    ASSERT_TRUE(dialog.grab().save(QDir(out).filePath(name+".png")));
}
}

/** @brief F09/MDL-05：零关节原点的法兰也能录入独立几何，取消不改完整草稿。 */
TEST(ModelingImportGui, PropertySegmentsOnFlangeProductionEntry_MDL05)
{
    IRD_TEST_INFO("MDL-05",{},std::nullopt);
    ModelingUiModule module;module.seedTemplateSession();
    const auto before=module.session().draft;
    std::unique_ptr<QWidget> panel(module.createPanel());
    // 精确业务身份交给既有面板定位入口，不假设树标签装饰格式。
    auto* modelingPanel=qobject_cast<ModelingPanelWidget*>(panel.get());ASSERT_NE(modelingPanel,nullptr);
    modelingPanel->focusObject(before.design.links.back().objectId);
    bool seen=false;PropertyModalWatchdog watchdog;
    QTimer::singleShot(0,panel.get(),[&]{
        auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if(!dialog){return;}seen=true;
        EXPECT_EQ(dialog->objectName(),"ird_modeling_property_segments");
        EXPECT_NE(dialog->findChild<QTableWidget*>("ird_modeling_segments_table"),nullptr);
        dialog->reject();
    });
    EXPECT_FALSE(module.executeDomainCommand("modeling.estimate-properties"));
    EXPECT_TRUE(seen)<<"F09：生产估算尚未打开完整段元表单";
    EXPECT_EQ(module.session().draft,before);
}

/** @brief F09/MDL-05：真实表单校验、三类段元/密度/位姿及草稿一次撤销。 */
TEST(ModelingImportGui, PropertySegmentWindowValidationAndLocalHistory_MDL05)
{
    IRD_TEST_INFO("MDL-05",{},std::nullopt);
    ModelingUiModule module;module.seedTemplateSession();
    const auto branch=sdurws::ird::core::BranchId::generate();
    const auto revision=sdurws::ird::core::RevisionId::generate();
    module.bindSessionAnchor(branch,revision);module.noteAppliedRevision(revision,sdurws::ird::core::ObjectId::generate());
    std::unique_ptr<QWidget> panel(module.createPanel());
    auto* view=qobject_cast<ModelingPanelWidget*>(panel.get());ASSERT_NE(view,nullptr);
    const auto before=module.session().draft;
    view->focusObject(before.design.links.back().objectId);
    int submits=0;module.bindCommandSubmit([&](const sdurws::ird::ui::CommandId&){++submits;});
    PropertyModalWatchdog watchdog;
    QTimer::singleShot(0,panel.get(),[&]{
        auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());ASSERT_NE(dialog,nullptr);
        auto* table=dialog->findChild<QTableWidget*>("ird_modeling_segments_table");ASSERT_NE(table,nullptr);
        auto* confirm=dialog->findChild<QPushButton*>("ird_modeling_segments_confirm");ASSERT_NE(confirm,nullptr);
        auto* feedback=dialog->findChild<QLabel*>("ird_modeling_segments_feedback");ASSERT_NE(feedback,nullptr);
        auto* add=dialog->findChild<QPushButton*>("ird_modeling_segments_add");ASSERT_NE(add,nullptr);
        auto* remove=dialog->findChild<QPushButton*>("ird_modeling_segments_remove");ASSERT_NE(remove,nullptr);
        EXPECT_EQ(table->columnCount(),12);EXPECT_EQ(table->rowCount(),1);
        confirm->click();EXPECT_TRUE(dialog->isVisible());EXPECT_TRUE(feedback->text().contains(QStringLiteral("第1段")));
        auto set=[&](int row,int column,const char* value){table->item(row,column)->setText(QString::fromLatin1(value));};
        auto type=[&](int row){return qobject_cast<QComboBox*>(table->cellWidget(row,0));};
        auto material=[&](int row){return qobject_cast<QComboBox*>(table->cellWidget(row,4));};
        // 非有限原文保留；密度默认只读，显式切自定义后必须用户填写。
        set(0,1,"nan");set(0,3,"0.4");confirm->click();
        EXPECT_TRUE(feedback->text().contains(QStringLiteral("有限")));EXPECT_EQ(table->item(0,1)->text(),"nan");
        EXPECT_FALSE(table->item(0,5)->flags().testFlag(Qt::ItemIsEditable));
        set(0,1,"0.2");type(0)->setCurrentIndex(1);set(0,2,"0.3");confirm->click();
        EXPECT_TRUE(dialog->isVisible());EXPECT_TRUE(feedback->text().contains(QStringLiteral("估算未通过")));
        set(0,2,"0.1");type(0)->setCurrentIndex(0); // 第1段实心圆柱。
        add->click();type(1)->setCurrentIndex(1);set(1,1,"0.3");set(1,2,"0.1");set(1,3,"0.5");
        add->click();type(2)->setCurrentIndex(2);set(2,1,"0.2");set(2,2,"0.4");set(2,3,"0.6");
        material(2)->setCurrentIndex(5);EXPECT_TRUE(table->item(2,5)->text().isEmpty());
        confirm->click();EXPECT_TRUE(feedback->text().contains(QStringLiteral("第3段")));
        set(2,5,"1000");set(2,6,"1");set(2,9,"0.1");set(2,10,"0.2");set(2,11,"0.3");
        // 删除中间行后控制回调必须按当前行重定位，不能写到旧第三行。
        add->click();table->setCurrentCell(1,1);remove->click();
        type(1)->setCurrentIndex(2);material(1)->setCurrentIndex(1);
        EXPECT_EQ(table->item(1,5)->text(),"2700");material(1)->setCurrentIndex(5);set(1,5,"1000");
        type(2)->setCurrentIndex(1);set(2,1,"0.3");set(2,2,"0.1");set(2,3,"0.5");
        EXPECT_GT(table->horizontalScrollBar()->maximum(),0);
        propertySnapshot(*dialog,"property-segments-complete");confirm->click();
    });
    ASSERT_TRUE(module.executeDomainCommand("modeling.estimate-properties"));
    const auto after=module.session().draft;const auto& body=after.design.links.back().body;
    ASSERT_TRUE(body.mass.tryValue());EXPECT_GT(body.mass.value(),48);
    // 独立NumPy矩阵乘积Rz(.3)Ry(.2)Rx(.1)及闭式段惯量/平行轴参考值；
    // 不调用被测估算器求期望，能发现表单漏消费密度、段平移或任一姿态角。
    EXPECT_NEAR(body.mass.value(),1429.044130518073,1e-9);
    EXPECT_NEAR(body.centerOfMass.value()[0],0.033588885727831586,1e-12);
    EXPECT_NEAR(body.inertia.value().ixy,0.136621186,1e-8);
    EXPECT_NEAR(body.inertia.value().ixz,-0.259629403,1e-8);
    EXPECT_NEAR(body.inertia.value().iyz,0.00121206902,1e-10);
    EXPECT_EQ(body.mass.provenance().kind,sdurws::ird::core::ProvenanceKind::GeometricEstimate);
    EXPECT_EQ(after.changes.size(),before.changes.size()+1);
    auto* undo=panel->findChild<QPushButton*>("ird_modeling_draft_undo");ASSERT_NE(undo,nullptr);
    auto* redo=panel->findChild<QPushButton*>("ird_modeling_draft_redo");ASSERT_NE(redo,nullptr);
    ASSERT_TRUE(undo->isEnabled());undo->click();EXPECT_EQ(module.session().draft,before);
    redo->click();EXPECT_EQ(module.session().draft,after);
    module.setWritable(false);EXPECT_FALSE(module.executeDomainCommand("modeling.estimate-properties"));
    EXPECT_EQ(module.session().draft,after);EXPECT_EQ(module.session().baseRevision,revision);EXPECT_EQ(submits,0);
}

/** @brief F10/MDL-05/V-17：选中连杆须有可写物性内联页，不止只读摘要。 */
TEST(ModelingUiModuleHostWiring, LinkBodyEditorReachable_MDL05_V17)
{
    IRD_TEST_INFO("MDL-05",{},std::nullopt);
    ModelingUiModule module;module.seedTemplateSession();
    std::unique_ptr<QWidget> panel(module.createPanel());panel->show();QApplication::processEvents();
    auto* view=qobject_cast<ModelingPanelWidget*>(panel.get());ASSERT_NE(view,nullptr);
    view->focusObject(module.session().draft.design.links.back().objectId);QApplication::processEvents();
    auto* editor=panel->findChild<QWidget*>("ird_modeling_body_editor");
    ASSERT_NE(editor,nullptr)<<"F10：连杆质量/质心/完整惯量缺内联可写入口";
    EXPECT_TRUE(editor->isVisible());
}

/** @brief F12/MDL-15/AT-28：没有场景对象时也必须能进入真实新增页。 */
TEST(ModelingUiModuleHostWiring, EmptySceneManagementReachable_MDL15_AT28)
{
    IRD_TEST_INFO("MDL-15",{"AT-28"},std::nullopt);
    ModelingUiModule module;module.seedTemplateSession();
    ASSERT_TRUE(module.session().draft.sceneObjects.empty());
    std::unique_ptr<QWidget> panel(module.createPanel());panel->show();QApplication::processEvents();
    auto* entry=panel->findChild<QPushButton*>("ird_modeling_edit_scene_mode");
    ASSERT_NE(entry,nullptr)<<"F12：空场景无新增固定帧/障碍物入口";
    entry->click();QApplication::processEvents();
    auto* manager=panel->findChild<QWidget*>("ird_modeling_scene_editor");
    ASSERT_NE(manager,nullptr);EXPECT_TRUE(manager->isVisible());
}

/** @brief F10/MDL-05：真实模块仅记一次局部历史，恢复清暂存且零命令/修订。 */
TEST(ModelingUiModuleHostWiring, ManualBodyLocalUndoRedoAndNoRevision_MDL05_V17)
{
    IRD_TEST_INFO("MDL-05",{},std::nullopt);
    ModelingUiModule module;module.seedTemplateSession();
    const auto branch=core::BranchId::generate();const auto revision=core::RevisionId::generate();
    module.bindSessionAnchor(branch,revision);
    std::unique_ptr<QWidget> panel(module.createPanel());
    auto* view=qobject_cast<ModelingPanelWidget*>(panel.get());ASSERT_NE(view,nullptr);
    view->focusObject(module.session().draft.design.links.back().objectId);
    int submits=0;module.bindCommandSubmit([&](const ui::CommandId&){++submits;});
    const auto before=module.session().draft;
    // 工具复用相同物性子控件名，必须先定位连杆页；全窗口递归查找会命中
    // 另一页的禁用控件，把测试定位错误误报为连杆编辑产品故障。
    auto* body=panel->findChild<QWidget*>("ird_modeling_body_editor");ASSERT_NE(body,nullptr);
    auto* mass=body->findChild<QCheckBox*>("ird_body_mass_edit");ASSERT_NE(mass,nullptr);
    auto* field=body->findChild<QLineEdit*>("ird_body_value_0");ASSERT_NE(field,nullptr);
    auto* apply=body->findChild<QPushButton*>("ird_body_apply");ASSERT_NE(apply,nullptr);
    mass->setChecked(true);field->setText("4.5");apply->click();
    const auto after=module.session().draft;
    EXPECT_EQ(after.changes.size(),before.changes.size()+1);
    ASSERT_TRUE(after.design.links.back().body.mass.tryValue());
    EXPECT_DOUBLE_EQ(after.design.links.back().body.mass.value(),4.5);
    auto* undo=panel->findChild<QPushButton*>("ird_modeling_draft_undo");
    auto* redo=panel->findChild<QPushButton*>("ird_modeling_draft_redo");
    ASSERT_TRUE(undo->isEnabled());mass->setChecked(true);field->setText("99");undo->click();
    EXPECT_EQ(module.session().draft,before);EXPECT_FALSE(mass->isChecked());EXPECT_NE(field->text(),"99");
    ASSERT_TRUE(redo->isEnabled());redo->click();EXPECT_EQ(module.session().draft,after);
    EXPECT_EQ(field->text(),"4.5");EXPECT_FALSE(mass->isChecked());
    EXPECT_EQ(module.session().baseRevision,revision);EXPECT_EQ(submits,0);
    module.setWritable(false);EXPECT_FALSE(apply->isEnabled());apply->click();
    EXPECT_EQ(module.session().draft,after);
}

/** @brief F12/MDL-15/AT-28：真实场景管理增改删、局部历史、位姿与零项目修订。 */
TEST(ModelingUiModuleHostWiring, SceneCrudLocalHistoryAndWorldPose_MDL15_AT28)
{
    IRD_TEST_INFO("MDL-15",{"AT-28"},std::nullopt);
    ModelingUiModule module;module.seedTemplateSession();
    const auto revision=core::RevisionId::generate();module.bindSessionAnchor(core::BranchId::generate(),revision);
    int submits=0;module.bindCommandSubmit([&](const ui::CommandId&){++submits;});
    std::unique_ptr<QWidget> panel(module.createPanel());panel->resize(1100,760);panel->show();QApplication::processEvents();
    auto* view=qobject_cast<ModelingPanelWidget*>(panel.get());ASSERT_NE(view,nullptr);
    auto* entry=panel->findChild<QPushButton*>("ird_modeling_edit_scene_mode");ASSERT_NE(entry,nullptr);entry->click();
    auto* name=panel->findChild<QLineEdit*>("ird_scene_name");ASSERT_NE(name,nullptr);
    auto* apply=panel->findChild<QPushButton*>("ird_scene_apply");ASSERT_NE(apply,nullptr);
    auto* remove=panel->findChild<QPushButton*>("ird_scene_remove");ASSERT_NE(remove,nullptr);
    auto* feedback=panel->findChild<QLabel*>("ird_scene_feedback");ASSERT_NE(feedback,nullptr);
    auto* confirm=panel->findChild<QCheckBox*>("ird_scene_confirm_remove");ASSERT_NE(confirm,nullptr);
    auto* undo=panel->findChild<QPushButton*>("ird_modeling_draft_undo");
    auto* redo=panel->findChild<QPushButton*>("ird_modeling_draft_redo");ASSERT_NE(undo,nullptr);ASSERT_NE(redo,nullptr);
    const auto before=module.session().draft;
    apply->click();EXPECT_EQ(module.session().draft,before);EXPECT_FALSE(feedback->text().isEmpty());
    name->setText("fixture");apply->click();const auto added=module.session().draft;
    ASSERT_EQ(added.sceneObjects.size(),1U);EXPECT_EQ(added.design.sceneRefs.front(),added.sceneObjects.front().objectId);
    EXPECT_EQ(added.changes.size(),before.changes.size()+1);EXPECT_TRUE(undo->isEnabled());
    name->setText("pending");module.recomputeReadiness();
    EXPECT_EQ(name->text(),"pending");EXPECT_TRUE(panel->findChild<QWidget*>("ird_modeling_scene_editor")->isVisible());
    panel->findChild<QPushButton*>("ird_scene_cancel")->click();EXPECT_EQ(name->text(),"fixture");
    EXPECT_EQ(module.session().draft,added);
    name->setText("fixture_changed");apply->click();const auto updated=module.session().draft;
    EXPECT_EQ(updated.sceneObjects.front().localName,"fixture_changed");
    EXPECT_EQ(updated.sceneObjects.front().objectId,added.sceneObjects.front().objectId);
    undo->click();EXPECT_EQ(module.session().draft,added);EXPECT_EQ(name->text(),"fixture");
    redo->click();EXPECT_EQ(module.session().draft,updated);
    remove->click();EXPECT_EQ(module.session().draft,updated);EXPECT_TRUE(feedback->text().contains(QStringLiteral("确认")));
    // 所有对象均保留世界系位姿；原六分量编辑页仍可进入，不被新管理页遮蔽。
    panel->findChild<QPushButton*>("ird_scene_pose")->click();
    EXPECT_TRUE(panel->findChild<QWidget*>("ird_modeling_scene_area")->isVisible());entry->click();
    propertySnapshot(*panel,"scene-manager-updated");
    confirm->setChecked(true);remove->click();const auto removed=module.session().draft;
    EXPECT_TRUE(removed.sceneObjects.empty());EXPECT_TRUE(removed.design.sceneRefs.empty());
    undo->click();EXPECT_EQ(module.session().draft,updated);redo->click();EXPECT_EQ(module.session().draft,removed);
    undo->click();module.setWritable(false);EXPECT_FALSE(apply->isEnabled());EXPECT_FALSE(remove->isEnabled());
    // 直接派发信号也必须守住只读出口，不能仅依赖按钮灰显。
    const auto readOnly=module.session().draft;name->setText("must_not_write");
    ASSERT_TRUE(QMetaObject::invokeMethod(apply,"clicked",Qt::DirectConnection));
    ASSERT_TRUE(QMetaObject::invokeMethod(remove,"clicked",Qt::DirectConnection));
    EXPECT_EQ(module.session().draft,readOnly);EXPECT_EQ(submits,0);EXPECT_EQ(module.session().baseRevision,revision);
}

/** @brief F08/MDL-13：空模板须可进入工具管理，不能依赖预先存在的工具树锚。 */
TEST(ModelingUiModuleHostWiring, EmptyToolManagerEntry_MDL13_AT01)
{
    IRD_TEST_INFO("MDL-13",{"AT-01"},std::nullopt);
    ModelingUiModule module;module.seedTemplateSession();
    ASSERT_TRUE(module.session().draft.toolObjects.empty());
    std::unique_ptr<QWidget> panel(module.createPanel());panel->resize(1120,800);panel->show();
    QApplication::processEvents();
    auto* entry=panel->findChild<QPushButton*>("ird_modeling_edit_tool_mode");
    ASSERT_NE(entry,nullptr)<<"空工具集缺少管理入口";
    entry->click();QApplication::processEvents();
    auto* editor=panel->findChild<QWidget*>("ird_modeling_tool_editor");
    ASSERT_NE(editor,nullptr);EXPECT_TRUE(editor->isVisible());
}

/** @brief F12/MDL-15：真实Windows文件选择/外部网格来源、确认前零写入与资源摘要。 */
TEST(ModelingUiModuleHostWiring, SceneExternalGeometryFileDialog_MDL15_AT28)
{
    IRD_TEST_INFO("MDL-15",{"AT-28"},std::nullopt);
    QTemporaryDir dir;ASSERT_TRUE(dir.isValid());const auto path=dir.filePath("obstacle.stl");
    QFile file(path);ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    ASSERT_GT(file.write("solid obstacle\nfacet normal 0 0 1\nouter loop\nvertex 0 0 0\nvertex 1 0 0\nvertex 0 1 0\nendloop\nendfacet\nendsolid obstacle\n"),0);file.close();
    ModelingUiModule module;module.seedTemplateSession();std::unique_ptr<QWidget> panel(module.createPanel());
    panel->resize(1100,760);panel->show();QApplication::processEvents();
    panel->findChild<QPushButton*>("ird_modeling_edit_scene_mode")->click();
    auto* name=panel->findChild<QLineEdit*>("ird_scene_name");ASSERT_NE(name,nullptr);name->setText("obstacle");
    auto* source=panel->findChild<QComboBox*>("ird_scene_source");ASSERT_NE(source,nullptr);source->setCurrentIndex(2);
    auto* browse=panel->findChild<QPushButton*>("ird_scene_browse");ASSERT_NE(browse,nullptr);
    // 仍运行windows平台的真实QFileDialog，只把native外壳替换成Qt控件以可重复输入。
    // RAII恢复进程属性；定时器仅在真实对话边界模拟用户文件选择，不替代io或计算。
    struct DialogModeRestore {
        bool old=QApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
        DialogModeRestore(){QApplication::setAttribute(Qt::AA_DontUseNativeDialogs,true);}
        ~DialogModeRestore(){QApplication::setAttribute(Qt::AA_DontUseNativeDialogs,old);}
    } restore;
    const auto before=module.session().draft;bool seen=false;QTimer select;select.setInterval(150);
    QObject::connect(&select,&QTimer::timeout,[&]{
        auto* dialog=qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
        if(!dialog){return;}
        if(!seen){seen=true;dialog->setDirectory(dir.path());dialog->selectFile("obstacle.stl");return;}
        // 文件系统模型异步装载目录；下一轮事件后再输入并确认，避免过早accept仅导航目录。
        if(auto* input=dialog->findChild<QLineEdit*>("fileNameEdit")){input->setText("obstacle.stl");}
        EXPECT_TRUE(QMetaObject::invokeMethod(dialog,"accept",Qt::DirectConnection));
    });
    QTimer watchdog;watchdog.setSingleShot(true);
    QObject::connect(&watchdog,&QTimer::timeout,[]{
        ADD_FAILURE()<<"场景几何文件对话未响应";
        if(auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget())){dialog->reject();}
    });
    select.start();watchdog.start(5000);browse->click();select.stop();watchdog.stop();ASSERT_TRUE(seen);
    EXPECT_EQ(module.session().draft,before);
    EXPECT_EQ(panel->findChild<QLineEdit*>("ird_scene_path")->text(),path);
    panel->findChild<QPushButton*>("ird_scene_apply")->click();
    const auto& after=module.session().draft;ASSERT_EQ(after.sceneObjects.size(),1U);
    ASSERT_TRUE(after.sceneObjects.front().geometry);ASSERT_EQ(after.design.resourceManifest.size(),before.design.resourceManifest.size()+1);
    const auto& resource=after.design.resourceManifest.back();EXPECT_EQ(resource.state,ResourceState::Recorded);
    ASSERT_TRUE(resource.externalRecord);EXPECT_EQ(resource.contentDigest,resource.externalRecord->recordedDigest);
    EXPECT_EQ(after.sceneObjects.front().geometry->resourceRefId,resource.resourceId);
    propertySnapshot(*panel,"scene-manager-external");
    // 文件选择的嵌套事件循环中可能切换项目/草稿；返回的旧文件不能套用到新目标。
    source->setCurrentIndex(2);seen=false;QObject::disconnect(&select,nullptr,nullptr,nullptr);
    QObject::connect(&select,&QTimer::timeout,[&]{
        auto* dialog=qobject_cast<QFileDialog*>(QApplication::activeModalWidget());if(!dialog){return;}
        if(!seen){seen=true;module.seedTemplateSession();dialog->setDirectory(dir.path());dialog->selectFile("obstacle.stl");return;}
        if(auto* input=dialog->findChild<QLineEdit*>("fileNameEdit")){input->setText("obstacle.stl");}
        EXPECT_TRUE(QMetaObject::invokeMethod(dialog,"accept",Qt::DirectConnection));
    });
    select.start();watchdog.start(5000);browse->click();select.stop();watchdog.stop();ASSERT_TRUE(seen);
    EXPECT_TRUE(panel->findChild<QLineEdit*>("ird_scene_path")->text().isEmpty())
        <<"旧目标文件选择结果不得污染新草稿";
    EXPECT_TRUE(module.session().draft.sceneObjects.empty());
    // 同样核对权限切换及显式取消。代际守卫应覆盖“值未变，但用户已终止本次编辑”。
    for(const bool readOnly:{true,false}){
        module.setWritable(true);source->setCurrentIndex(2);seen=false;
        const auto stable=module.session().draft;
        QObject::disconnect(&select,nullptr,nullptr,nullptr);
        QObject::connect(&select,&QTimer::timeout,[&]{
            auto* dialog=qobject_cast<QFileDialog*>(QApplication::activeModalWidget());if(!dialog){return;}
            if(!seen){
                seen=true;
                if(readOnly){module.setWritable(false);}
                else {panel->findChild<QPushButton*>("ird_scene_cancel")->click();}
                dialog->setDirectory(dir.path());dialog->selectFile("obstacle.stl");return;
            }
            if(auto* input=dialog->findChild<QLineEdit*>("fileNameEdit")){input->setText("obstacle.stl");}
            EXPECT_TRUE(QMetaObject::invokeMethod(dialog,"accept",Qt::DirectConnection));
        });
        select.start();watchdog.start(5000);browse->click();select.stop();watchdog.stop();ASSERT_TRUE(seen);
        EXPECT_TRUE(panel->findChild<QLineEdit*>("ird_scene_path")->text().isEmpty());
        EXPECT_TRUE(panel->findChild<QLabel*>("ird_scene_feedback")->text().contains(QStringLiteral("丢弃")));
        EXPECT_EQ(module.session().draft,stable);
    }
}
/** @brief F08：真实模块控件驱动工具CRUD、物性、安装TCP页及完整局部历史，项目零修订。 */
TEST(ModelingUiModuleHostWiring, ToolManagerHistoryBodyAndNavigation_MDL13_AT01)
{
    IRD_TEST_INFO("MDL-13",{"AT-01"},std::nullopt);
    ModelingUiModule module;module.seedTemplateSession();const auto before=module.session().draft;
    const auto revision=module.session().baseRevision;std::unique_ptr<QWidget> panel(module.createPanel());
    int submits=0;module.bindCommandSubmit([&](const ui::CommandId&){++submits;});
    panel->resize(1140,840);panel->show();QApplication::processEvents();
    auto* entry=panel->findChild<QPushButton*>("ird_modeling_edit_tool_mode");ASSERT_NE(entry,nullptr);entry->click();
    auto* editor=panel->findChild<QWidget*>("ird_modeling_tool_editor");ASSERT_NE(editor,nullptr);
    auto* name=editor->findChild<QLineEdit*>("ird_tool_name");auto* apply=editor->findChild<QPushButton*>("ird_tool_apply");
    auto* undo=panel->findChild<QPushButton*>("ird_modeling_draft_undo");
    auto* redo=panel->findChild<QPushButton*>("ird_modeling_draft_redo");ASSERT_NE(undo,nullptr);ASSERT_NE(redo,nullptr);
    name->setText("gripper");editor->findChild<QCheckBox*>("ird_tool_default")->setChecked(true);apply->click();
    const auto added=module.session().draft;ASSERT_EQ(added.toolObjects.size(),1U);ASSERT_TRUE(added.design.defaultTcp);
    undo->click();EXPECT_EQ(module.session().draft,before);redo->click();EXPECT_EQ(module.session().draft,added);
    entry->click();auto* body=editor->findChild<QWidget*>("ird_tool_body_editor");ASSERT_NE(body,nullptr);EXPECT_TRUE(body->isEnabled());
    body->findChild<QCheckBox*>("ird_body_mass_edit")->setChecked(true);
    body->findChild<QLineEdit*>("ird_body_value_0")->setText("2.5");
    EXPECT_EQ(module.session().draft,added);body->findChild<QPushButton*>("ird_body_apply")->click();
    const auto weighted=module.session().draft;EXPECT_DOUBLE_EQ(weighted.toolObjects.front().body.mass.value(),2.5);
    EXPECT_EQ(weighted.toolObjects.front().body.mass.provenance().kind,core::ProvenanceKind::UserProvided);
    undo->click();EXPECT_EQ(module.session().draft,added);redo->click();EXPECT_EQ(module.session().draft,weighted);
    // 工具局部系也必须显式选择质心处置；完整覆盖后再迁移，独立数值核对真实写回对象。
    const auto field=[&](int i){return body->findChild<QLineEdit*>(QStringLiteral("ird_body_value_%1").arg(i));};
    auto* com=body->findChild<QCheckBox*>("ird_body_com_edit");
    auto* choice=body->findChild<QComboBox*>("ird_body_centroid_resolution");
    com->setChecked(true);for(int i=1;i<4;++i){field(i)->setText("0");}
    body->findChild<QPushButton*>("ird_body_apply")->click();EXPECT_EQ(module.session().draft,weighted);
    choice->setCurrentIndex(2);for(int i=4;i<10;++i){field(i)->setText(i<7?"1":"0");}
    body->findChild<QPushButton*>("ird_body_apply")->click();const auto tensor=module.session().draft;
    ASSERT_TRUE(tensor.toolObjects.front().body.inertia.tryValue());
    EXPECT_DOUBLE_EQ(tensor.toolObjects.front().body.inertia.value().iyy,1);
    com->setChecked(true);field(1)->setText("0.1");choice->setCurrentIndex(1);
    body->findChild<QPushButton*>("ird_body_apply")->click();const auto migrated=module.session().draft;
    const auto& inertia=migrated.toolObjects.front().body.inertia.value();
    EXPECT_DOUBLE_EQ(inertia.ixx,1);EXPECT_DOUBLE_EQ(inertia.iyy,1.025);EXPECT_DOUBLE_EQ(inertia.izz,1.025);
    undo->click();EXPECT_EQ(module.session().draft,tensor);redo->click();EXPECT_EQ(module.session().draft,migrated);
    // 禁用父控件仍必须由子页执行端拒绝直接信号，不能只靠灰显测试空转。
    module.setWritable(false);body->findChild<QCheckBox*>("ird_body_mass_edit")->setChecked(true);field(0)->setText("99");
    ASSERT_TRUE(QMetaObject::invokeMethod(body->findChild<QPushButton*>("ird_body_apply"),"clicked",Qt::DirectConnection));
    EXPECT_EQ(module.session().draft,migrated);module.setWritable(true);
    entry->click();name->setText("gripper_renamed");apply->click();const auto renamed=module.session().draft;
    EXPECT_EQ(renamed.toolObjects.front().localName,"gripper_renamed");EXPECT_EQ(renamed.toolObjects.front().objectId,added.toolObjects.front().objectId);
    editor->findChild<QPushButton*>("ird_tool_pose")->click();EXPECT_TRUE(panel->findChild<QWidget*>("ird_modeling_tool_area")->isVisible());
    entry->click();propertySnapshot(*panel,"tool-manager-body");
    // 模拟用户把滚动条移到底端，再核对可见性与真实鼠标命中；不能用
    // ensureWidgetVisible的程序化滚动结果推断用户是否能操作嵌套子页。
    auto* ancestor=editor->parentWidget();QScrollArea* scroll=nullptr;
    while(ancestor&&!scroll){scroll=qobject_cast<QScrollArea*>(ancestor);ancestor=ancestor->parentWidget();}
    ASSERT_NE(scroll,nullptr);auto* bodyApply=body->findChild<QPushButton*>("ird_body_apply");
    QApplication::processEvents();
    scroll->verticalScrollBar()->triggerAction(QAbstractSlider::SliderToMaximum);
    QApplication::processEvents();
    EXPECT_EQ(scroll->verticalScrollBar()->value(),scroll->verticalScrollBar()->maximum());
    EXPECT_TRUE(scroll->viewport()->rect().contains(bodyApply->mapTo(scroll->viewport(),bodyApply->rect().center())));
    EXPECT_EQ(body->childAt(bodyApply->mapTo(body,bodyApply->rect().center())),bodyApply);
    const auto local=QPointF(bodyApply->rect().center());
    const auto global=QPointF(bodyApply->mapToGlobal(bodyApply->rect().center()));
    QMouseEvent press(QEvent::MouseButtonPress,local,global,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease,local,global,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
    QApplication::sendEvent(bodyApply,&press);QApplication::sendEvent(bodyApply,&release);
    EXPECT_EQ(module.session().draft,renamed);
    propertySnapshot(*panel,"tool-manager-body-bottom");
    editor->findChild<QPushButton*>("ird_tool_remove")->click();EXPECT_EQ(module.session().draft,renamed);
    editor->findChild<QCheckBox*>("ird_tool_confirm_remove")->setChecked(true);editor->findChild<QPushButton*>("ird_tool_remove")->click();
    EXPECT_TRUE(module.session().draft.toolObjects.empty());EXPECT_FALSE(module.session().draft.design.defaultTcp);
    undo->click();EXPECT_EQ(module.session().draft,renamed);redo->click();EXPECT_TRUE(module.session().draft.toolObjects.empty());
    EXPECT_EQ(module.session().baseRevision,revision)<<"工具管理不能越过项目应用入口写修订";
    EXPECT_EQ(submits,0);
}
/** @brief B4b：真实管理控件产生完整草稿，恢复后的树/物性/选择和新局部历史均可继续操作。 */
TEST(ModelingUiModuleHostWiring, CompleteDraftRestoresTreeBodyAndFreshHistory_PM04_AT21)
{
    IRD_TEST_INFO("PM-04",{"AT-21"},std::nullopt);
    ModelingUiModule source;source.seedTemplateSession();std::unique_ptr<QWidget> first(source.createPanel());
    first->resize(1140,840);first->show();QApplication::processEvents();
    first->findChild<QPushButton*>("ird_modeling_edit_tool_mode")->click();
    auto* tool=first->findChild<QWidget*>("ird_modeling_tool_editor");ASSERT_NE(tool,nullptr);
    tool->findChild<QLineEdit*>("ird_tool_name")->setText("restored-gripper");
    tool->findChild<QCheckBox*>("ird_tool_default")->setChecked(true);tool->findChild<QPushButton*>("ird_tool_apply")->click();
    auto* body=tool->findChild<QWidget*>("ird_tool_body_editor");
    body->findChild<QCheckBox*>("ird_body_mass_edit")->setChecked(true);body->findChild<QLineEdit*>("ird_body_value_0")->setText("3.75");
    body->findChild<QPushButton*>("ird_body_apply")->click();
    first->findChild<QPushButton*>("ird_modeling_edit_scene_mode")->click();
    auto* scene=first->findChild<QWidget*>("ird_modeling_scene_editor");ASSERT_NE(scene,nullptr);
    scene->findChild<QLineEdit*>("ird_scene_name")->setText("restored-fixture");scene->findChild<QPushButton*>("ird_scene_apply")->click();
    ASSERT_EQ(source.session().draft.toolObjects.size(),1U);ASSERT_EQ(source.session().draft.sceneObjects.size(),1U);
    const auto expected=source.session().draft;const auto document=source.buildDraftDocument();first.reset();
    ModelingUiModule target;target.seedTemplateSession();std::unique_ptr<QWidget> panel(target.createPanel());
    panel->resize(1140,840);panel->show();target.adoptRestoredDocument(document);QApplication::processEvents();
    ASSERT_EQ(target.session().draft,expected);EXPECT_FALSE(target.session().baseRevision);
    auto* undo=panel->findChild<QPushButton*>("ird_modeling_draft_undo");ASSERT_NE(undo,nullptr);
    EXPECT_FALSE(undo->isEnabled())<<"恢复只建立新起点，不能回退到上次进程的草稿";
    auto* view=qobject_cast<ModelingPanelWidget*>(panel.get());ASSERT_NE(view,nullptr);
    view->focusObject(expected.toolObjects.front().objectId);panel->findChild<QPushButton*>("ird_modeling_edit_tool_mode")->click();
    auto* editor=panel->findChild<QWidget*>("ird_modeling_tool_editor");
    EXPECT_EQ(editor->findChild<QLineEdit*>("ird_tool_name")->text(),"restored-gripper");
    auto* mass=editor->findChild<QWidget*>("ird_tool_body_editor")->findChild<QLineEdit*>("ird_body_value_0");
    ASSERT_NE(mass,nullptr);EXPECT_EQ(mass->text(),"3.75");
    // 面板还有就绪明细树，须按既有导航锚定位，避免把首个递归子控件当结构树。
    auto* tree=panel->findChild<QTreeWidget*>("ird_modeling_struct_tree");ASSERT_NE(tree,nullptr);
    EXPECT_FALSE(tree->findItems("restored-fixture",Qt::MatchContains|Qt::MatchRecursive).isEmpty());
    propertySnapshot(*panel,"full-draft-restored-tool");
    editor->findChild<QLineEdit*>("ird_tool_name")->setText("continued-gripper");editor->findChild<QPushButton*>("ird_tool_apply")->click();
    EXPECT_TRUE(undo->isEnabled());undo->click();EXPECT_EQ(target.session().draft,expected);
    auto bad=document;bad.payload="{broken";const auto stable=target.session().draft;target.adoptRestoredDocument(bad);
    EXPECT_EQ(target.session().draft,stable);EXPECT_TRUE(target.buildDraftCommand("modeling"));
    target.setWritable(false);target.adoptRestoredDocument(document);EXPECT_EQ(target.session().draft,expected);
}

/** @brief F08：真实Windows文件窗口的正常选择及嵌套切草稿/只读/取消均不能污染新目标。 */
TEST(ModelingUiModuleHostWiring, ToolRealFileDialogAndStaleGuard_MDL13_AT28)
{
    IRD_TEST_INFO("MDL-13",{"AT-28"},std::nullopt);
    QTemporaryDir dir;ASSERT_TRUE(dir.isValid());const auto path=dir.filePath(QStringLiteral("工具网格.stl"));
    QFile file(path);ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    ASSERT_GT(file.write("solid tool\nfacet normal 0 0 1\nouter loop\nvertex 0 0 0\nvertex 1 0 0\nvertex 0 1 0\nendloop\nendfacet\nendsolid tool\n"),0);file.close();
    ModelingUiModule module;module.seedTemplateSession();std::unique_ptr<QWidget> panel(module.createPanel());
    panel->resize(1140,840);panel->show();QApplication::processEvents();panel->findChild<QPushButton*>("ird_modeling_edit_tool_mode")->click();
    auto* editor=panel->findChild<QWidget*>("ird_modeling_tool_editor");ASSERT_NE(editor,nullptr);
    auto* source=editor->findChild<QComboBox*>("ird_tool_source");auto* browse=editor->findChild<QPushButton*>("ird_tool_browse");
    struct DialogModeRestore {
        bool old=QApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
        DialogModeRestore(){QApplication::setAttribute(Qt::AA_DontUseNativeDialogs,true);}
        ~DialogModeRestore(){QApplication::setAttribute(Qt::AA_DontUseNativeDialogs,old);}
    } restore;
    // 模拟操作仅位于文件对话边界；资源读取/摘要、页面代际和模块基线均真实执行。
    const auto choose=[&](const std::function<void()>& during){
        bool seen=false;QTimer select;select.setInterval(150);QTimer watchdog;watchdog.setSingleShot(true);
        QObject::connect(&select,&QTimer::timeout,[&]{
            auto* dialog=qobject_cast<QFileDialog*>(QApplication::activeModalWidget());if(!dialog){return;}
            if(!seen){seen=true;if(during){during();}dialog->setDirectory(dir.path());dialog->selectFile(QFileInfo(path).fileName());return;}
            if(auto* input=dialog->findChild<QLineEdit*>("fileNameEdit")){input->setText(QFileInfo(path).fileName());}
            EXPECT_TRUE(QMetaObject::invokeMethod(dialog,"accept",Qt::DirectConnection));
        });
        QObject::connect(&watchdog,&QTimer::timeout,[]{ADD_FAILURE()<<"工具文件对话未响应";
            if(auto* d=qobject_cast<QDialog*>(QApplication::activeModalWidget())){d->reject();}});
        select.start();watchdog.start(5000);browse->click();select.stop();watchdog.stop();EXPECT_TRUE(seen);
    };
    const auto before=module.session().draft;source->setCurrentIndex(2);choose({});
    EXPECT_EQ(editor->findChild<QLineEdit*>("ird_tool_path")->text(),path);EXPECT_EQ(module.session().draft,before);
    editor->findChild<QLineEdit*>("ird_tool_name")->setText("welder");editor->findChild<QCheckBox*>("ird_tool_default")->setChecked(true);
    editor->findChild<QPushButton*>("ird_tool_apply")->click();ASSERT_EQ(module.session().draft.toolObjects.size(),1U);
    ASSERT_TRUE(module.session().draft.toolObjects.front().geometry);propertySnapshot(*panel,"tool-manager-external");
    source->setCurrentIndex(2);choose([&]{module.seedTemplateSession();});
    EXPECT_TRUE(editor->findChild<QLineEdit*>("ird_tool_path")->text().isEmpty());EXPECT_TRUE(module.session().draft.toolObjects.empty());
    for(const bool readOnly:{true,false}){
        module.setWritable(true);source->setCurrentIndex(2);const auto stable=module.session().draft;
        choose([&]{if(readOnly){module.setWritable(false);}else{editor->findChild<QPushButton*>("ird_tool_cancel")->click();}});
        EXPECT_TRUE(editor->findChild<QLineEdit*>("ird_tool_path")->text().isEmpty());EXPECT_EQ(module.session().draft,stable);
        EXPECT_TRUE(editor->findChild<QLabel*>("ird_tool_feedback")->text().contains(QStringLiteral("丢弃")));
    }
}

/** @brief PM-04/AT-21：真实结构编辑及局部历史均通知保存链，同值/只读/关闭不通知。 */
TEST(ModelingUiModuleHostWiring, DraftChangeNotificationFromRealControls_PM04_AT21)
{
    IRD_TEST_INFO("PM-04",{"AT-21"},std::nullopt);
    ModelingUiModule module;module.seedTemplateSession();
    module.bindSessionAnchor(core::BranchId::generate(),core::RevisionId::generate());
    std::unique_ptr<QWidget> panel(module.createPanel());panel->resize(1140,840);panel->show();
    QApplication::processEvents();int changes=0;module.bindDraftChanged([&]{++changes;});
    module.recomputeReadiness();EXPECT_EQ(changes,0)<<"同值就绪刷新不能制造编辑";
    auto* add=panel->findChild<QPushButton*>("ird_modeling_struct_add");ASSERT_NE(add,nullptr);
    add->click();QApplication::processEvents();EXPECT_EQ(changes,1);
    auto* undo=panel->findChild<QPushButton*>("ird_modeling_draft_undo");ASSERT_NE(undo,nullptr);
    undo->click();QApplication::processEvents();EXPECT_EQ(changes,2)<<"局部撤销也改变待保存内容";
    auto* redo=panel->findChild<QPushButton*>("ird_modeling_draft_redo");ASSERT_NE(redo,nullptr);
    redo->click();QApplication::processEvents();EXPECT_EQ(changes,3);
    propertySnapshot(*panel,"draft-change-redo");
    module.setWritable(false);const auto before=module.session().draft;
    for(const auto* name:{"ird_modeling_struct_add","ird_modeling_struct_remove",
        "ird_modeling_struct_up","ird_modeling_struct_down","ird_modeling_struct_reset"}){
        auto* button=panel->findChild<QPushButton*>(name);ASSERT_NE(button,nullptr);
        EXPECT_FALSE(button->isEnabled())<<name;button->click();
    }
    EXPECT_EQ(module.session().draft,before);EXPECT_EQ(changes,3);
    propertySnapshot(*panel,"draft-change-readonly");
    module.onSessionDetached();EXPECT_EQ(changes,3)<<"关闭清理不保存跨项目空草稿";
}

/** @brief PM-04/PM-18：真实结构控件undo回基线为clean，redo为dirty；无摘要字段/部件差异也不能漏判。 */
TEST(ModelingUiModuleHostWiring, CompletePendingStateTracksUndoRedo_PM04_AT21)
{
    IRD_TEST_INFO("PM-04",{"AT-21"},std::nullopt);
    ModelingUiModule module;module.seedTemplateSession();
    const auto revision=core::RevisionId::generate();
    module.bindSessionAnchor(core::BranchId::generate(),revision);
    module.noteAppliedRevision(revision,core::ObjectId::generate());
    ASSERT_FALSE(module.hasPendingDraft());
    std::unique_ptr<QWidget> panel(module.createPanel());panel->resize(1140,840);panel->show();
    QApplication::processEvents();std::vector<bool> pending;
    module.bindDraftChanged([&]{pending.push_back(module.hasPendingDraft());});
    auto* add=panel->findChild<QPushButton*>("ird_modeling_struct_add");ASSERT_NE(add,nullptr);
    auto* undo=panel->findChild<QPushButton*>("ird_modeling_draft_undo");ASSERT_NE(undo,nullptr);
    auto* redo=panel->findChild<QPushButton*>("ird_modeling_draft_redo");ASSERT_NE(redo,nullptr);
    add->click();QApplication::processEvents();EXPECT_TRUE(module.hasPendingDraft());
    undo->click();QApplication::processEvents();EXPECT_FALSE(module.hasPendingDraft());
    ASSERT_TRUE(redo->isEnabled());redo->click();QApplication::processEvents();EXPECT_TRUE(module.hasPendingDraft());
    undo->click();QApplication::processEvents();ASSERT_FALSE(module.hasPendingDraft());
    EXPECT_EQ(pending,(std::vector<bool>{true,false,true,false}));
    // 直接声明无摘要的完整内容反例：被测规范比较必须覆盖根字段和活动部件，
    // 不能仅以命令可提交/摘要列表为空推断没有修改。
    const auto baseline=module.session().draft;ASSERT_TRUE(baseline.changes.empty());
    module.session().draft.design.displayName="unsummarized-field";EXPECT_TRUE(module.hasPendingDraft());
    module.session().draft=baseline;
    ToolDefinition tool;tool.objectId=core::ObjectId::generate();tool.localName="unsummarized-tool";
    tool.tcpList={{"tcp",rw::math::Transform3D<double>::identity(),""}};
    module.session().draft.toolObjects={tool};module.session().draft.design.toolRefs={tool.objectId};
    EXPECT_TRUE(module.hasPendingDraft());module.session().draft=baseline;EXPECT_FALSE(module.hasPendingDraft());
    propertySnapshot(*panel,"draft-undo-clean-redo-retained");
}

/** @brief PM-04/AT-21：真实面板重建完整当前基线，并清除旧未确认表单和局部历史。 */
TEST(ModelingUiModuleHostWiring, ConflictRebuildRefreshesTreeAndCancelsPending_PM04_AT21)
{
    IRD_TEST_INFO("PM-04",{"AT-21"},std::nullopt);
    ModelingUiModule target;target.seedTemplateSession();const auto branch=core::BranchId::generate();
    target.bindSessionAnchor(branch,core::RevisionId::generate());
    const auto old=target.buildDraftDocument();
    ModelingUiModule authoritative;authoritative.seedTemplateSession();
    authoritative.session().draft.design.displayName="当前修订的权威模型";
    // 固定修订输入夹具：直接声明完整合法工具；被测重建/Codec/树投影仍走产品实现。
    ToolDefinition tool;tool.objectId=core::ObjectId::generate();tool.localName="current-gripper";
    tool.tcpList={{"tcp",rw::math::Transform3D<double>::identity(),""}};
    authoritative.session().draft.toolObjects={tool};authoritative.session().draft.design.toolRefs={tool.objectId};
    authoritative.session().draft.design.defaultTcp=TcpRef{tool.objectId,"tcp"};
    const auto revision=core::RevisionId::generate();
    authoritative.bindSessionAnchor(branch,revision);authoritative.noteAppliedRevision(revision,core::ObjectId::generate());
    const auto current=authoritative.buildDraftDocument();
    target.bindRevisionDraftReader([&](const core::BranchId& b,const core::RevisionId& r)
        ->std::optional<ui::DraftDocumentProjection>{EXPECT_EQ(b,branch);EXPECT_EQ(r,revision);return current;});
    std::unique_ptr<QWidget> panel(target.createPanel());panel->resize(1140,840);panel->show();QApplication::processEvents();
    panel->findChild<QPushButton*>("ird_modeling_edit_tool_mode")->click();
    auto* pending=panel->findChild<QLineEdit*>("ird_tool_name");ASSERT_NE(pending,nullptr);pending->setText("unconfirmed-old-tool");
    target.rebuildOnRevision(revision.toCanonical());QApplication::processEvents();
    EXPECT_EQ(target.buildDraftDocument().payload,current.payload);ASSERT_TRUE(target.retainedConflictDraft());
    EXPECT_EQ(target.retainedConflictDraft()->payload,old.payload);EXPECT_EQ(target.retainedConflictDraft()->baseRevisionId,old.baseRevisionId);
    EXPECT_FALSE(target.buildDraftCommand("modeling"));
    EXPECT_FALSE(panel->findChild<QPushButton*>("ird_modeling_draft_undo")->isEnabled());
    // 重建清除了旧选中目标；空管理入口合法地进入新增态。先核对旧暂存
    // 输入已消失，再按真实导航选择新工具，不能把“自动选第一件”当成需求。
    panel->findChild<QPushButton*>("ird_modeling_edit_tool_mode")->click();QApplication::processEvents();
    EXPECT_NE(panel->findChild<QLineEdit*>("ird_tool_name")->text(),"unconfirmed-old-tool");
    auto* view=qobject_cast<ModelingPanelWidget*>(panel.get());ASSERT_NE(view,nullptr);view->focusObject(tool.objectId);
    panel->findChild<QPushButton*>("ird_modeling_edit_tool_mode")->click();QApplication::processEvents();
    auto* currentName=panel->findChild<QLineEdit*>("ird_tool_name");ASSERT_NE(currentName,nullptr);
    EXPECT_EQ(currentName->text(),"current-gripper")<<"旧未确认输入不能回写到新修订的工具";
    bool found=false;for(auto* tree:panel->findChildren<QTreeWidget*>()){
        QTreeWidgetItemIterator it(tree);for(;*it;++it){if((*it)->text(0).contains("current-gripper")){found=true;}}
    }
    EXPECT_TRUE(found);propertySnapshot(*panel,"conflict-rebuilt-current-tool");
    // PM-04/AT-29：旧稿必须有真实可见入口，不能只靠内部getter声称“可迁移”。
    auto* review=panel->findChild<QPushButton*>("ird_modeling_retained_review");ASSERT_NE(review,nullptr);
    target.setWritable(false);EXPECT_TRUE(review->isEnabled())<<"只读仍允许核对旧稿";
    const auto stable=target.buildDraftDocument();bool reviewed=false;QTimer watchdog;watchdog.setSingleShot(true);
    QObject::connect(&watchdog,&QTimer::timeout,[]{ADD_FAILURE()<<"旧稿对照未响应";
        if(auto* d=qobject_cast<QDialog*>(QApplication::activeModalWidget())){d->reject();}});
    QTimer::singleShot(50,[&]{auto* d=qobject_cast<QDialog*>(QApplication::activeModalWidget());
        ASSERT_NE(d,nullptr);EXPECT_EQ(d->objectName(),"ird_modeling_diff");
        auto* purpose=d->findChild<QLabel*>("ird_modeling_diff_context");ASSERT_NE(purpose,nullptr);
        EXPECT_TRUE(purpose->text().contains(QStringLiteral("当前工作集")));
        EXPECT_TRUE(purpose->text().contains(QStringLiteral("旧稿")));
        auto* table=d->findChild<QTableWidget*>("ird_modeling_diff_table");ASSERT_NE(table,nullptr);
        EXPECT_GE(table->columnWidth(3)-12,
            table->fontMetrics().horizontalAdvance(table->horizontalHeaderItem(3)->text()))
            <<"比较方向必须完整可见，不能截断成含糊的表头";
        bool oldName=false,newName=false;
        for(int row=0;row<table->rowCount();++row){
            newName|=table->item(row,5)->text().contains(QStringLiteral("当前修订的权威模型"));
            oldName|=table->item(row,6)->text().contains(QStringLiteral("demo"));
        }
        EXPECT_TRUE(newName);EXPECT_TRUE(oldName);reviewed=true;propertySnapshot(*d,"retained-draft-review");d->reject();
    });
    watchdog.start(5000);review->click();watchdog.stop();EXPECT_TRUE(reviewed);
    EXPECT_EQ(target.buildDraftDocument().payload,stable.payload);
    EXPECT_EQ(target.retainedConflictDraft()->payload,old.payload);
    // PM-04：旧对照跨关闭/同项目重开后也不能定位新会话。这里只验证关闭
    // 边界，不把面板仍存活等同于原项目仍有效，且禁止对已清空模型解引用。
    QTimer::singleShot(50,[&]{auto* d=qobject_cast<QDialog*>(QApplication::activeModalWidget());
        ASSERT_NE(d,nullptr);target.onSessionDetached();EXPECT_FALSE(review->isEnabled());
        auto* jump=d->findChild<QPushButton*>("ird_modeling_diff_locate");ASSERT_NE(jump,nullptr);jump->click();
        auto* feedback=d->findChild<QLabel*>("ird_modeling_diff_feedback");ASSERT_NE(feedback,nullptr);
        EXPECT_TRUE(feedback->text().contains(QStringLiteral("项目已变化")));d->reject();
    });
    watchdog.start(5000);review->click();watchdog.stop();EXPECT_FALSE(review->isEnabled());
}
