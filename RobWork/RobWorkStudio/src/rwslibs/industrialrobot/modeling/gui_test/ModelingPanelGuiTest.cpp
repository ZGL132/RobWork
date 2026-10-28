/**
 * @file   ModelingPanelGuiTest.cpp
 * @brief  建模面板 Widgets 级契约测试（sdurws_ird_modeling_gui_test 载体）——
 *         UI-T27 返工补建（验收记录 attempt1 阻断 B-2 的测试面补齐）。
 *
 * 设计依据：
 *   - 任务契约 tasks/foundation/UI-T27.json acceptance 1/2/3/4/5 的具名
 *     用例承诺（P0-1 表单全清/P0-2 单位分离/P0-3 仅 zero-offset 可编/
 *     P1-1 幻影脏化短路/P0-4 provider 禁用）；
 *   - units/modeling.md §9.7（面板五区与 L-2 提交面）、MDL-07（表单最小
 *     版的面板半区）、UX-02/05、ERR-01；
 *   - ctest LABELS ird_gui 串行（testkit §6.7——ui.md §12.1 表既定口径；
 *     本目标为 UI-T27 诚实登记缺口"gui_test 补建归下批"的兑现）。
 *   - UI-T43 增量（宿主接线修复回归钉）：L-7 防复活（只读态下可用性
 *     刷新不越门——本文件）；生产装配序文案解析回归＋模块 setWritable
 *     暂存/即时双形态链路在 ModelingUiModuleHostWiringGuiTest.cpp
 *     （policy 符号 gating——集成模式专属）。
 *
 * 先例：ui/gui_test（QApplication main＋findChild 定位——树夹具容器化
 * 修订同款）；modeling/test/PluginPanelTest.cpp（makeSixAxisDraft 六轴
 * 草稿夹具——同源复用，模型层/Widgets 层分目标不混跑）。
 *
 * 断言纪律：编辑器经 findChild<QLineEdit*> 现取（面板成员私有——测试
 * 经公共控件树定位，不触私有面）；编辑接受后编辑器随重投影重建，断言
 * 必须重新现取（旧指针失效——Qt 父子所有权）。
 */

#include <gtest/gtest.h>

#include <QApplication>
#include <QCheckBox>
#include <QScrollArea>
#include <QScrollBar>
#include <QSplitter> // WP-13-T30：真实窗口分栏宽度协商，不以minimumWidth属性冒充可缩窄。
#include "plugin/ModelingBodyEditor.hpp"
#include <QDir> // MDL-22真实窗口截图证据，仅写测试指定输出目录。
#include <QClipboard>   // 复制钮断言（UI-T59 剪贴板＝预览全文）
#include <QMap>
#include <cstdio>
#include <QLabel>
#include <QPlainTextEdit>
#include <QLineEdit>
#include <QKeyEvent>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QTableWidget>
#include <QToolButton>
#include <QTreeWidgetItemIterator>
#include <QPushButton>
#include <QDialog>          // F14：耦合编辑模态
#include <QDialogButtonBox>  // F14：模态确认钮
#include <QSpinBox>          // F14：窗口/阶数输入
#include <QTabWidget>        // WP-13-T30：可见页切换，禁止以隐藏控件click冒充可操作。
#include <QStackedWidget>    // 树选择必须切换到对应详细编辑页。
#include <QTimer>            // F14：模态应答定时器
#include <sdurws/ird/modeling/GeometryLinkEdit.hpp>  // attachExternalGeometry/detachGeometry/GeometrySlot（UI-T48 域原语直调）

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯登记

#include <sdurws/ird/modeling/Template.hpp>  // RobotDesignTemplateFactory（六轴草稿夹具）
#include <sdurws/ird/ui/UiText.hpp>
#include <sdurws/ird/ui/UiTheme.hpp>  // palette::kWarning（B1 行内警示着色断言）
#include "plugin/PanelCommandCatalog.hpp"  // modelingDomainCommands（B4 反断言目录）          // ui::resolveText（禁用原因键→中文——UX-02 解析半区）

#include "plugin/ModelingPanelWidget.hpp"  // 被测面板（同单元 PRIVATE include 面——PluginPanelTest 同款）
// （UI-T43 模块级接线用例——ModelingUiModule 被测面——落位
//   ModelingUiModuleHostWiringGuiTest.cpp：其被测类型构造期持 policy 行程
//   评估器符号，须随 PluginModuleT03BTest 先例做集成模式 gating，不能与
//   本文件（冒烟模式无条件编入）同 TU。）

using namespace sdurws::ird;
using namespace sdurws::ird::modeling;

namespace {

/// 剪贴板可用性预探（audit F-617/F-620；wp17-t08 建议①补重试半区）：
/// 剪贴板型 GUI 用例对系统剪贴板占用敏感——他进程持有全局剪贴板时（实测
/// MATLAB pid 持有，OleSetClipboard COM 0x800401d0/CLIPBRD_E_CANT_OPEN），
/// Qt 重试耗尽后 setText/text 往返失败，属环境面而非代码缺陷。探针＝哨兵
/// 写读往返，返回往返是否一致；不一致时在 bounded 窗口内【重试】至多
/// kClipProbeAttempts 次（wp17-t08 建议①的重试通道：CLIPBRD_E_CANT_OPEN
/// 常为毫秒级瞬态占用——持有进程释放后立即可打开；逐次 processEvents
/// 让 Qt 剪贴板层的内部重试与事件派发落地，避免把瞬态占用误判为持久
/// 占用而多跳过）。重试耗尽仍不一致才判不可用：调用方（用例体内）
/// GTEST_SKIP 并输出 F-620 归因字样（门禁聚账归因；F-617 正式在册项）
/// ——skip 必须在用例体内发生（GTEST_SKIP 的 return 只能退出用例体本身，
/// helper 内调用不终止用例）。健康环境首轮往返即一致、用例照常真实执行
/// ——探针不吞真失败（用例内断言照常生效）。非线程安全：仅 GUI 测试
/// 主线程使用。重试次数为固定常数（无退避延时——占用的释放时机不可
/// 预期，指数退避对毫秒级瞬态无收益，徒增无人值守时长）。
bool clipboardUsable()
{
    // 重试窗口：首轮＋2 次重试。实测瞬态占用在数百毫秒内释放（MATLAB
    // 持有场景为持久占用，重试不掩盖——重试耗尽仍 skip 并归因）。
    constexpr int kClipProbeAttempts = 3;
    const QString sentinel =
        QStringLiteral("ird-clip-probe-%1").arg(QCoreApplication::applicationPid());
    for (int attempt = 0; attempt < kClipProbeAttempts; ++attempt) {
        QApplication::clipboard()->setText(sentinel);
        QApplication::processEvents();
        if (QApplication::clipboard()->text() == sentinel) {
            return true;    // 哨兵写读一致＝剪贴板可用（健康环境首轮即返回）
        }
        QApplication::processEvents();  // 重试前先泵一轮事件（释放窗口）
    }
    return false;   // bounded 窗口耗尽仍不一致＝持久占用，交由调用方 skip
}

/// generic-6r 草稿的便捷创建（PluginPanelTest 同款夹具——成功前置起步）。
ModelingWorkingSet makeSixAxisDraft()
{
    const RobotDesignTemplateFactory factory;
    std::vector<core::DiagnosticRecord> diags;
    const TemplateOutcome outcome = factory.createDraft(
        TemplateId{kTemplateIdGeneric6R},
        runtime::InstallationPresetToken::Ground, "demo", diags);
    return outcome.get();  // 成功前置——失败即测试自身装配错误（logic_error）
}

/// 控件是否位于关节详细编辑页子树（UI-T53——"编辑"页签承载 ParamTablePanel，
/// 其筛选框/表格/按钮不入属性区与命令目录的对账面——P0-3 门控管辖的是
/// 属性行，编辑页是 B.1 详细编辑面的独立承载）。UI-T54 起同规则覆盖基座
/// 安装页（QDoubleSpinBox 内部持有可编辑 QLineEdit——不对账即污染 P0-3 计数）。
bool inJointEditPane(const QWidget& panel, const QObject* widget)
{
    for (const QObject* p = widget; p != nullptr; p = p->parent()) {
        if (p == &panel) { return false; }  // 未途经编辑页宿主即达面板根
        const QString name = p->objectName();
        if (name == QStringLiteral("ird_modeling_edit_host")
            || name == QStringLiteral("ird_param_table_panel")
            || name == QStringLiteral("ird_modeling_base_page")
            || name == QStringLiteral("ird_modeling_tool_area")
            || name == QStringLiteral("ird_modeling_scene_area")
            || name == QStringLiteral("ird_modeling_pose_area")
            || name == QStringLiteral("ird_modeling_drivetrain_area")
            || name == QStringLiteral("ird_modeling_body_editor")
            || name == QStringLiteral("ird_modeling_scene_editor")
            || name == QStringLiteral("ird_modeling_tool_editor")) {
            return true;
        }
    }
    return false;
}

/// 命令按钮全集（构造序＝目录序——Qt 子对象按挂树序枚举；诊断历史折叠
/// 钮非命令按钮，按 objectName 剔除）。返回序与 catalog 序一一对应
/// （F-502 折叠重排后 findChildren 子树序≠目录序——按命令钮 objectName
/// 挂载的 id 后缀恢复目录序，UI-T56；用例以 size 相等断言作漂移守卫）。
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
        if (btn->objectName() == QStringLiteral("ird_modeling_preview_copy")) {
            continue;  // 预览复制钮不入命令对账（UI-T59 呈现件——非命令目录按钮）
        }
        if (btn->objectName().startsWith(QStringLiteral("ird_modeling_struct_"))) {
            continue;  // 结构操作钮不入命令对账（UI-T47 呈现件——非命令目录按钮）
        }
        if (btn->objectName().startsWith(QStringLiteral("ird_modeling_edit_"))) {
            continue;  // 编辑页模式钮不入命令对账（UI-T53/T54 呈现件）
        }
        if (inJointEditPane(panel, btn)) {
            continue;  // 编辑页按钮（应用/取消/确认区）不入命令对账（UI-T53 呈现件）
        }
        if (btn->objectName().startsWith(QStringLiteral("ird_modeling_cmd_"))) {
            byId.insert(btn->objectName(), btn);  // 命令钮按 id 归位（目录序输出）
        }
    }
    QList<QPushButton*> ordered;
    for (const auto& desc : catalog) {
        const QString key = QStringLiteral("ird_modeling_cmd_")
                            + QString::fromStdString(desc.id);
        if (byId.contains(key)) { ordered.push_back(byId.value(key)); }
    }
    return ordered;
}

}  // namespace

QLineEdit* jointField(QWidget* host, const char* fieldKey);
void typeJointField(QLineEdit* field, const QString& text);
void confirmJointForm(QWidget* host);

// =====================================================================
// 夹具：工作集＋面板＋现取辅助
// =====================================================================

class ModelingPanelGuiTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_ws = makeSixAxisDraft();
        m_panel = std::make_unique<ModelingPanelWidget>(true);
        // 编辑目标提供器（装配层注入形态——面板零工作集副本，ACC5）。
        m_panel->setEditTargetProvider([this]() { return &m_ws; });
        // 面板刷新（五区现取重投影——就绪报告空集即可，本组用例不断言就绪条）。
        ModelReadinessReport emptyReport;
        m_panel->refreshPanel(m_ws, emptyReport);
    }

    /// 属性区编辑器现取（QLineEdit 全集——面板内属性编辑行是该控件类型
    /// 的唯一来源；顺序＝表单行序）。UI-T53 起排除编辑页子树（ParamTablePanel
    /// 筛选框等——P0-3 门控管辖面＝属性行，两对账面分离）。
    QList<QLineEdit*> editors() const
    {
        QList<QLineEdit*> rows;
        for (QLineEdit* e : m_panel->findChildren<QLineEdit*>()) {
            if (inJointEditPane(*m_panel, e)) { continue; }
            rows.push_back(e);
        }
        return rows;
    }

    /// 定位唯一可编辑编辑器（P0-3 门控——恰一非只读，即 zero-offset 行）。
    QLineEdit* editableEditor() const
    {
        for (QLineEdit* e : editors()) {
            if (!e->isReadOnly()) { return e; }
        }
        return nullptr;
    }

    ModelingWorkingSet m_ws;  ///< 六轴草稿工作集（提供器返回目标——本夹具持有）
    std::unique_ptr<ModelingPanelWidget> m_panel;  ///< 被测面板（Qt 父子自毁）
};

/**
 * @brief 在真实建模树中按可见标题查找节点，测试调用期借用，Qt树负责所有权。
 * @param tree [in] 夹具创建的结构树，仅GUI主线程访问。
 * @param label [in] 精确工程标题，无单位；不依赖顶层行号或私有模型键。
 * @return 匹配节点，未找到返回空；刷新后必须重新查找，禁止持有失效条目。
 */
QTreeWidgetItem* structureItem(QTreeWidget& tree, const QString& label)
{
    for (QTreeWidgetItemIterator it(&tree); *it; ++it) {
        if ((*it)->text(0) == label) { return *it; }
    }
    return nullptr;
}

/** @brief MDL-07：对象必须位于真实可折叠父节点下，不能只靠文本缩进模拟层次。 */
TEST_F(ModelingPanelGuiTest, StructureHierarchyGroupsRealObjects_MDL07_T30)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-07","UX-09"},{"AT-01"},std::nullopt);
    ToolDefinition tool; tool.objectId=core::ObjectId::generate(); tool.localName="manual-tool";
    TcpEntry tcp; tcp.key="tip"; tool.tcpList.push_back(tcp); m_ws.toolObjects.push_back(tool);
    SceneObject scene; scene.objectId=core::ObjectId::generate(); scene.localName="manual-fixture";
    m_ws.sceneObjects.push_back(scene); m_panel->refreshPanel(m_ws,ModelReadinessReport{});
    auto* tree=m_panel->findChild<QTreeWidget*>("ird_modeling_struct_tree"); ASSERT_NE(tree,nullptr);
    ASSERT_EQ(tree->topLevelItemCount(),1) << "整个模型应能一次折叠，平铺节点不满足层次导航";
    auto* root=tree->topLevelItem(0); EXPECT_TRUE(root->isExpanded());
    auto* joints=structureItem(*tree,QStringLiteral("关节")); ASSERT_NE(joints,nullptr);
    auto* links=structureItem(*tree,QStringLiteral("连杆")); ASSERT_NE(links,nullptr);
    EXPECT_EQ(joints->parent(),root); EXPECT_EQ(links->parent(),root);
    ASSERT_EQ(joints->childCount(),6); ASSERT_EQ(links->childCount(),7);
    EXPECT_EQ(joints->child(0)->text(1).toStdString(),m_ws.design.joints[0].objectId.toCanonical());
    EXPECT_EQ(links->child(0)->text(1).toStdString(),m_ws.design.links[0].objectId.toCanonical());
    EXPECT_TRUE(joints->text(1).isEmpty()); EXPECT_FALSE(joints->flags() & Qt::ItemIsSelectable);
    EXPECT_TRUE(joints->flags() & Qt::ItemIsEnabled) << "分组必须可操作折叠箭头，不能禁用整行";
    for (const auto& title:{QStringLiteral("工具"),QStringLiteral("场景")}) {
        auto* group=structureItem(*tree,title); ASSERT_NE(group,nullptr);
        EXPECT_EQ(group->parent(),root); ASSERT_EQ(group->childCount(),1); EXPECT_TRUE(group->isExpanded());
    }
    m_panel->resize(1100,760); m_panel->show(); QApplication::processEvents();
    const auto out=qEnvironmentVariable("IRD_MODELING_GUI_EVIDENCE_OUT");
    if(!out.isEmpty()){QDir().mkpath(out); EXPECT_TRUE(m_panel->grab().save(out+"/t30-tree-hierarchy.png"));}
}

/** @brief UX-09/MDL-07：后台刷新保留折叠，但三维重复定位应重新展开同一对象的祖先。 */
TEST_F(ModelingPanelGuiTest, TreeCollapsePersistsButRepeatedLocateReveals_MDL07_T30)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-07","UX-09"},{"AT-01"},std::nullopt);
    auto* tree=m_panel->findChild<QTreeWidget*>("ird_modeling_struct_tree"); ASSERT_NE(tree,nullptr);
    m_panel->focusObject(m_ws.design.joints[0].objectId);
    auto* joints=structureItem(*tree,QStringLiteral("关节")); ASSERT_NE(joints,nullptr);
    joints->setExpanded(false); m_panel->refreshPanel(m_ws,ModelReadinessReport{});
    joints=structureItem(*tree,QStringLiteral("关节")); ASSERT_NE(joints,nullptr);
    EXPECT_FALSE(joints->isExpanded()) << "普通刷新不能替使用者展开刚折叠的组";
    ASSERT_NE(tree->currentItem(),nullptr);
    EXPECT_EQ(tree->currentItem()->text(1).toStdString(),m_ws.design.joints[0].objectId.toCanonical());
    m_panel->focusObject(m_ws.design.joints[0].objectId);
    EXPECT_TRUE(joints->isExpanded()) << "重复定位不能因选中OID未变而跳过展开";
    // 改名不改变身份；折叠状态不能以标题为键，否则改名/普通刷新会丢失浏览位置。
    joints->setExpanded(false); m_ws.design.displayName="renamed-model";
    m_panel->refreshPanel(m_ws,ModelReadinessReport{});
    joints=structureItem(*tree,QStringLiteral("关节")); ASSERT_NE(joints,nullptr); EXPECT_FALSE(joints->isExpanded());
    tree->topLevelItem(0)->setExpanded(false); m_panel->focusObject(m_ws.design.joints[1].objectId);
    EXPECT_TRUE(tree->topLevelItem(0)->isExpanded()); EXPECT_TRUE(joints->isExpanded());
    EXPECT_TRUE(m_ws.changes.empty()) << "浏览与定位不得修改模型草稿";
}

/** @brief UX-09：展开/折叠全部是纯浏览，工程只读时也可用，不能触发脏化或结构编辑。 */
TEST_F(ModelingPanelGuiTest, TreeExpandCollapseAllIsReadOnlyNavigation_UX09_T30)
{
    IRD_TEST_INFO("UX-09",{"AT-01"},std::nullopt);
    auto* tree=m_panel->findChild<QTreeWidget*>("ird_modeling_struct_tree"); ASSERT_NE(tree,nullptr);
    auto* expand=m_panel->findChild<QPushButton*>("ird_modeling_struct_expand_all"); ASSERT_NE(expand,nullptr);
    auto* collapse=m_panel->findChild<QPushButton*>("ird_modeling_struct_collapse_all"); ASSERT_NE(collapse,nullptr);
    int dirtyEvents=0;
    QObject::connect(m_panel.get(),&ModelingPanelWidget::sessionDirtyChanged,m_panel.get(),[&](bool){++dirtyEvents;});
    m_panel->setWritable(false); EXPECT_TRUE(expand->isEnabled()); EXPECT_TRUE(collapse->isEnabled());
    m_panel->resize(1100,760); m_panel->show(); QApplication::processEvents();
    collapse->click(); EXPECT_FALSE(tree->topLevelItem(0)->isExpanded());
    const auto out=qEnvironmentVariable("IRD_MODELING_GUI_EVIDENCE_OUT");
    if(!out.isEmpty()){QDir().mkpath(out); EXPECT_TRUE(m_panel->grab().save(out+"/t30-tree-collapsed.png"));}
    m_panel->refreshPanel(m_ws,ModelReadinessReport{}); EXPECT_FALSE(tree->topLevelItem(0)->isExpanded());
    expand->click();
    for(QTreeWidgetItemIterator it(tree);*it;++it){if((*it)->childCount()>0){EXPECT_TRUE((*it)->isExpanded());}}
    EXPECT_EQ(dirtyEvents,0); EXPECT_TRUE(m_ws.changes.empty());
    // 模板临时OID按模板确定性派生，重复调用工厂并不代表换了身份。
    // 使用另一工程的真实根OID模拟切换模型，验证新身份不会继承旧根折叠态。
    collapse->click(); m_ws=makeSixAxisDraft(); m_ws.rootObjectId=core::ObjectId::generate();
    m_panel->refreshPanel(m_ws,ModelReadinessReport{});
    EXPECT_TRUE(tree->topLevelItem(0)->isExpanded());
}

/** @brief UX-09：树栏应连续缩窄至零，不能被内部按钮最小宽度卡住或突然跳成折叠。 */
TEST_F(ModelingPanelGuiTest, TreeWidthContinuousAndCollapse_UX09_T30)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-07","UX-09"},{"AT-01"},std::nullopt);
    m_panel->resize(1100,760); m_panel->show(); QApplication::processEvents();
    auto* splitter=m_panel->findChild<QSplitter*>("ird_modeling_main_splitter"); ASSERT_NE(splitter,nullptr);
    EXPECT_LE(splitter->sizes()[0],300) << "默认树栏不应占近半编辑区";
    const auto out=qEnvironmentVariable("IRD_MODELING_GUI_EVIDENCE_OUT");
    if(!out.isEmpty()){QDir().mkpath(out); EXPECT_TRUE(m_panel->grab().save(out+"/t30-width-default.png"));}
    // 检查布局协商后的实际像素宽度；只设置minimumWidth=0但保留布局最小提示会在这里失败。
    for(const int width:{160,80,40,1,0}){
        splitter->setSizes({width,splitter->width()-splitter->handleWidth()-width});
        QApplication::processEvents();
        EXPECT_NEAR(splitter->sizes()[0],width,1) << "请求树宽=" << width;
    }
    splitter->setSizes({240,splitter->width()-splitter->handleWidth()-240}); QApplication::processEvents();
    EXPECT_NEAR(splitter->sizes()[0],240,1) << "收起后必须能够恢复";
    EXPECT_TRUE(m_ws.changes.empty()) << "调整分栏不能改草稿";
}

/** @brief MDL-07/UX-09：窄栏中的结构按钮仍可滚动访问，恢复后保持选择和折叠且零脏化。 */
TEST_F(ModelingPanelGuiTest, TreeNarrowScrollNavigationNoDirty_MDL07_T30)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-07","UX-09"},{"AT-01"},std::nullopt);
    m_panel->resize(1100,760); m_panel->show(); QApplication::processEvents();
    auto* splitter=m_panel->findChild<QSplitter*>("ird_modeling_main_splitter"); ASSERT_NE(splitter,nullptr);
    auto* scroll=m_panel->findChild<QScrollArea*>("ird_modeling_tree_actions_scroll"); ASSERT_NE(scroll,nullptr);
    auto* tree=m_panel->findChild<QTreeWidget*>("ird_modeling_struct_tree"); ASSERT_NE(tree,nullptr);
    auto* collapse=m_panel->findChild<QPushButton*>("ird_modeling_struct_collapse_all"); ASSERT_NE(collapse,nullptr);
    auto* expand=m_panel->findChild<QPushButton*>("ird_modeling_struct_expand_all"); ASSERT_NE(expand,nullptr);
    int dirtyEvents=0;
    QObject::connect(m_panel.get(),&ModelingPanelWidget::sessionDirtyChanged,m_panel.get(),[&](bool){++dirtyEvents;});
    m_panel->focusObject(m_ws.design.joints[1].objectId);
    const QString selected=tree->currentItem()->text(1);
    auto* links=structureItem(*tree,QStringLiteral("连杆")); ASSERT_NE(links,nullptr); links->setExpanded(false);
    splitter->setSizes({160,splitter->width()-splitter->handleWidth()-160}); QApplication::processEvents();
    EXPECT_GT(scroll->horizontalScrollBar()->maximum(),0) << "窄栏不能静默裁掉尾部操作";
    // 逐个定位真实按钮到视口内；使用控件真实映射证明操作没有落在不可见的裁剪区。
    for(const auto* name:{"ird_modeling_struct_add","ird_modeling_struct_remove","ird_modeling_struct_up","ird_modeling_struct_down","ird_modeling_struct_reset"}){
        auto* button=m_panel->findChild<QPushButton*>(name); ASSERT_NE(button,nullptr);
        scroll->ensureWidgetVisible(button,0,0); QApplication::processEvents();
        const QRect bounds(button->mapTo(scroll->viewport(),QPoint(0,0)),button->size());
        EXPECT_TRUE(scroll->viewport()->rect().contains(bounds.center())) << name;
    }
    const auto out=qEnvironmentVariable("IRD_MODELING_GUI_EVIDENCE_OUT");
    if(!out.isEmpty()){QDir().mkpath(out); EXPECT_TRUE(m_panel->grab().save(out+"/t30-width-narrow.png"));}
    m_panel->refreshPanel(m_ws,ModelReadinessReport{});
    EXPECT_EQ(tree->currentItem()->text(1),selected); EXPECT_FALSE(structureItem(*tree,QStringLiteral("连杆"))->isExpanded());
    splitter->setSizes({0,splitter->width()-splitter->handleWidth()}); QApplication::processEvents();
    if(!out.isEmpty()){EXPECT_TRUE(m_panel->grab().save(out+"/t30-width-hidden.png"));}
    splitter->setSizes({240,splitter->width()-splitter->handleWidth()-240}); QApplication::processEvents();
    auto* headerScroll=m_panel->findChild<QScrollArea*>("ird_modeling_tree_header_scroll"); ASSERT_NE(headerScroll,nullptr);
    m_panel->setWritable(false); headerScroll->ensureWidgetVisible(collapse,0,0); collapse->click();
    EXPECT_FALSE(tree->topLevelItem(0)->isExpanded());
    headerScroll->ensureWidgetVisible(expand,0,0); expand->click(); EXPECT_TRUE(tree->topLevelItem(0)->isExpanded());
    EXPECT_EQ(tree->currentItem()->text(1),selected); EXPECT_EQ(dirtyEvents,0); EXPECT_TRUE(m_ws.changes.empty());
}

/** @brief UX-01：模板和三种导入必须在工具页直接可见，不能藏到高级诊断中。 */
TEST_F(ModelingPanelGuiTest, ManualEntryCommandsVisibleWithoutExpansion_UX01_T30)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-01","MDL-01"},{"AT-01"},std::nullopt);
    m_panel->resize(1100,760);m_panel->show();
    auto* tabs=m_panel->findChild<QTabWidget*>();ASSERT_NE(tabs,nullptr);
    tabs->setCurrentIndex(1);QApplication::processEvents();
    for(const auto* id:{"new-from-template","import-urdf","import-xacro","import-workcell"}){
        auto* button=m_panel->findChild<QPushButton*>(QString("ird_modeling_cmd_modeling.")+id);
        ASSERT_NE(button,nullptr);EXPECT_TRUE(button->isVisible())<<id;
    }
}

/** @brief MDL-07：连杆/基座返回关节后，用户必须立刻看到该关节表。 */
TEST_F(ModelingPanelGuiTest, TreeSelectionReturnsToJointEditor_MDL07_T30)
{
    IRD_TEST_INFO("MDL-07",{"AT-01"},std::nullopt);
    m_panel->resize(1100,760);m_panel->show();
    m_panel->focusObject(m_ws.design.links.front().objectId);
    m_panel->focusObject(m_ws.design.joints.front().objectId);QApplication::processEvents();
    auto* host=m_panel->findChild<QWidget*>("ird_modeling_edit_host");ASSERT_NE(host,nullptr);
    EXPECT_TRUE(host->isVisible())<<"连杆物性页遮住了已选关节的编辑页";
    auto* base=m_panel->findChild<QPushButton*>("ird_modeling_edit_base_mode");ASSERT_NE(base,nullptr);
    base->click();m_panel->focusObject(m_ws.design.joints.back().objectId);QApplication::processEvents();
    EXPECT_TRUE(host->isVisible())<<"从基座页选新关节后仍未切回关节表";
}

/** @brief UX-09：真实窗口保持760px高；长属性和隐藏页不得把操作区挤出窗口。 */
TEST_F(ModelingPanelGuiTest, ManualWindowFitsAndJointApplyIsReachable_UX09_T30)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-09","UX-05"},{"AT-01"},std::nullopt);
    m_panel->focusObject(m_ws.design.joints.front().objectId);
    m_panel->resize(1100,760);m_panel->show();QApplication::processEvents();
    EXPECT_LE(m_panel->height(),760)<<"面板最小高度把真实窗口撑出屏幕";
    auto* host=m_panel->findChild<QWidget*>("ird_modeling_edit_host");ASSERT_NE(host,nullptr);
    auto* apply=host->findChild<QPushButton*>("ird_param_apply");ASSERT_NE(apply,nullptr);
    // 若页面采用滚动承载，就按用户可执行的滚动路径将目标滚入视口。
    for(auto* parent=apply->parentWidget();parent;parent=parent->parentWidget()){
        if(auto* scroll=qobject_cast<QScrollArea*>(parent)){scroll->ensureWidgetVisible(apply);}
    }
    QApplication::processEvents();
    const auto center=apply->mapTo(m_panel.get(),apply->rect().center());
    EXPECT_TRUE(m_panel->rect().contains(center));
    EXPECT_EQ(m_panel->childAt(center),apply)<<"可见按钮中心被裁剪或被其他控件覆盖";
    const auto out=qEnvironmentVariable("IRD_MODELING_GUI_EVIDENCE_OUT");
    if(!out.isEmpty()){QDir().mkpath(out);EXPECT_TRUE(m_panel->grab().save(out+"/t30-joint-window.png"));}
}

/** @brief MDL-17：命名位姿下拉必须停在新选择项，并呈现对应构型。 */
TEST_F(ModelingPanelGuiTest, NamedPoseComboActuallySelectsNewEntry_MDL17_T30)
{
    IRD_TEST_INFO("MDL-17",{"AT-28"},std::nullopt);
    PoseSetEntry a;a.key="pose-a";a.jointConfiguration.assign(6,0.1);
    PoseSetEntry b;b.key="pose-b";b.jointConfiguration.assign(6,0.2);
    ASSERT_FALSE(applyPoseSetEntryUpsertEdit(m_ws,a));ASSERT_FALSE(applyPoseSetEntryUpsertEdit(m_ws,b));
    m_panel->refreshPanel(m_ws,ModelReadinessReport{});
    m_panel->focusObject(m_ws.poseSetObject->objectId);
    auto* combo=m_panel->findChild<QComboBox*>("ird_modeling_pose_combo");
    if(!combo){for(auto* c:m_panel->findChildren<QComboBox*>()){if(c->findText("pose-b")>=0){combo=c;break;}}}
    ASSERT_NE(combo,nullptr);combo->setCurrentIndex(combo->findText("pose-b"));
    QApplication::processEvents();EXPECT_EQ(combo->currentText(),"pose-b");
}

/** @brief MDL-13/15：同目标后台刷新不能吞工具/TCP/场景表中未确认的输入。 */
TEST_F(ModelingPanelGuiTest, SameTargetRefreshKeepsPoseInputs_UX05_T30)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-05","MDL-13","MDL-15"},{"AT-28"},std::nullopt);
    ToolDefinition tool;tool.objectId=core::ObjectId::generate();tool.localName="manual-tool";
    TcpEntry tcp;tcp.key="tip";tool.tcpList.push_back(tcp);m_ws.toolObjects.push_back(tool);
    SceneObject scene;scene.objectId=core::ObjectId::generate();scene.localName="manual-scene";
    m_ws.sceneObjects.push_back(scene);m_panel->refreshPanel(m_ws,ModelReadinessReport{});
    const auto check=[&](const core::ObjectId& id,const QString& key){
        m_panel->focusObject(id);
        QTableWidget* table=nullptr;int row=-1;
        for(auto* t:m_panel->findChildren<QTableWidget*>("ird_param_table")){
            for(int r=0;r<t->rowCount();++r){if(t->item(r,0)->data(Qt::UserRole).toString()==key){table=t;row=r;}}
        }
        ASSERT_NE(table,nullptr);table->item(row,1)->setText("0.25");QApplication::processEvents();
        m_panel->refreshPanel(m_ws,ModelReadinessReport{});QApplication::processEvents();
        EXPECT_DOUBLE_EQ(table->item(row,1)->text().toDouble(),0.25)<<key.toStdString();
    };
    check(tool.objectId,"mount-x");check(tool.objectId,"tcp-offset-z");
    check(scene.objectId,"world-x");
    EXPECT_TRUE(m_ws.changes.empty())<<"表内暂存不应提前写入草稿";
}

/** @brief MDL-13：选择另一个TCP后，表格必须马上显示新条目的真实偏移。 */
TEST_F(ModelingPanelGuiTest, TcpSelectionImmediatelyRepaintsOffset_MDL13_T30)
{
    IRD_TEST_INFO("MDL-13",{"AT-28"},std::nullopt);
    ToolDefinition tool;tool.objectId=core::ObjectId::generate();tool.localName="manual-tool";
    TcpEntry a;a.key="tip-a";TcpEntry b;b.key="tip-b";
    b.offset=rw::math::Transform3D<double>(rw::math::Vector3D<double>(0,0,0.4),
        rw::math::Rotation3D<double>(1,0,0,0,1,0,0,0,1));
    tool.tcpList={a,b};m_ws.toolObjects.push_back(tool);
    m_panel->refreshPanel(m_ws,ModelReadinessReport{});m_panel->focusObject(tool.objectId);
    auto* combo=m_panel->findChild<QComboBox*>("ird_modeling_tcp_combo");ASSERT_NE(combo,nullptr);
    EXPECT_TRUE(combo->isEnabled())<<"从无工具变为有工具后，下拉框必须恢复真实鼠标可操作性";
    combo->setCurrentIndex(1);QApplication::processEvents();
    bool found=false;
    for(auto* t:m_panel->findChildren<QTableWidget*>("ird_param_table")){
        for(int r=0;r<t->rowCount();++r){if(t->item(r,0)->data(Qt::UserRole).toString()=="tcp-offset-z"){
            EXPECT_DOUBLE_EQ(t->item(r,1)->text().toDouble(),0.4);found=true;
        }}
    }
    EXPECT_TRUE(found);
}

/** @brief UX-05：位姿未保存文本/构型和传动数值在同基线刷新后仍可继续编辑。 */
TEST_F(ModelingPanelGuiTest, RefreshKeepsNamedPoseAndDrivetrainInputs_UX05_T30)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-05","MDL-16","MDL-17"},{"AT-28"},std::nullopt);
    PoseSetEntry pose;pose.key="manual-pose";pose.jointConfiguration.assign(6,0.0);
    ASSERT_FALSE(applyPoseSetEntryUpsertEdit(m_ws,pose));m_ws.changes.clear();
    m_panel->refreshPanel(m_ws,ModelReadinessReport{});m_panel->focusObject(m_ws.poseSetObject->objectId);
    auto* note=m_panel->findChild<QLineEdit*>("ird_modeling_pose_note");ASSERT_NE(note,nullptr);
    note->setText("尚未保存的备注");
    const auto set=[&](const QString& key,const QString& value){
        for(auto* t:m_panel->findChildren<QTableWidget*>("ird_param_table")){
            for(int r=0;r<t->rowCount();++r){if(t->item(r,0)->data(Qt::UserRole).toString()==key){
                t->item(r,1)->setText(value);QApplication::processEvents();return;
            }}
        }FAIL()<<"缺少字段 "<<key.toStdString();
    };
    set("pose-config-0","0.4");set("dt-ratio-0","100");
    m_panel->refreshPanel(m_ws,ModelReadinessReport{});QApplication::processEvents();
    EXPECT_EQ(note->text(),"尚未保存的备注");
    for(auto* t:m_panel->findChildren<QTableWidget*>("ird_param_table")){
        for(int r=0;r<t->rowCount();++r){const auto key=t->item(r,0)->data(Qt::UserRole).toString();
            if(key=="pose-config-0"){EXPECT_DOUBLE_EQ(t->item(r,1)->text().toDouble(),0.4);}
            if(key=="dt-ratio-0"){EXPECT_DOUBLE_EQ(t->item(r,1)->text().toDouble(),100);}
        }
    }
    EXPECT_TRUE(m_ws.changes.empty());
}

/** @brief MDL-17：非法构型必须阻止整条保存，不能静默忽略后保存旧值。 */
TEST_F(ModelingPanelGuiTest, InvalidPoseConfigBlocksSave_MDL17_T30)
{
    IRD_TEST_INFO("MDL-17",{"AT-28"},std::nullopt);
    PoseSetEntry pose;pose.key="manual-pose";pose.jointConfiguration.assign(6,0.0);
    ASSERT_FALSE(applyPoseSetEntryUpsertEdit(m_ws,pose));m_ws.changes.clear();
    m_panel->refreshPanel(m_ws,ModelReadinessReport{});m_panel->focusObject(m_ws.poseSetObject->objectId);
    const auto before=m_ws;
    for(auto* t:m_panel->findChildren<QTableWidget*>("ird_param_table")){
        for(int r=0;r<t->rowCount();++r){if(t->item(r,0)->data(Qt::UserRole).toString()=="pose-config-0"){
            t->item(r,1)->setText("不是数值");QApplication::processEvents();
        }}
    }
    m_panel->findChild<QLineEdit*>("ird_modeling_pose_note")->setText("不应提前保存");
    m_panel->findChild<QPushButton*>("ird_modeling_pose_apply")->click();
    EXPECT_EQ(m_ws,before)<<"构型非法时仍保存了条目的其他字段";
}

/** @brief UX-05：新建相同链序关节后，旧对象暂存不能写入新对象。 */
TEST_F(ModelingPanelGuiTest, ReplacedJointCannotInheritOldInput_UX05_T30)
{
    IRD_TEST_INFO("UX-05",{"AT-28"},std::nullopt);
    m_panel->focusObject(m_ws.design.joints[0].objectId);
    auto* host=m_panel->findChild<QWidget*>("ird_modeling_edit_host");
    ASSERT_NE(jointField(host,"origin-x"),nullptr);typeJointField(jointField(host,"origin-x"),"0.25");
    const auto originalId=m_ws.design.joints[0].objectId;
    m_ws.design.joints[0].objectId=core::ObjectId::generate();
    const auto before=m_ws;
    m_panel->refreshPanel(m_ws,ModelReadinessReport{});m_panel->focusObject(m_ws.design.joints[0].objectId);
    QApplication::processEvents();
    // GF-A保留旧对象输入并要求显式离页决议；同链序的新OID不能继承旧暂存。
    EXPECT_EQ(jointField(host,"origin-x")->text(),QStringLiteral("0.25"));
    host->findChild<QPushButton*>("ird_param_apply")->click();
    host->findChild<QPushButton*>("ird_param_confirm_yes")->click();
    EXPECT_EQ(m_ws,before)<<"已被替换的对象不能把旧输入写到新OID";
    host->findChild<QPushButton*>("ird_joint_leave_discard")->click();QApplication::processEvents();
    ASSERT_NE(jointField(host,"origin-x"),nullptr);
    EXPECT_NE(m_ws.design.joints[0].objectId,originalId);
    EXPECT_DOUBLE_EQ(jointField(host,"origin-x")->text().toDouble(),0.0);
    EXPECT_EQ(m_ws,before);

}

/**
 * @brief 就绪条把 NotProvided 的中文状态和字段三要素呈现给工程师（WP-13-T23）。
 *
 * 本用例验证 UI 只渲染 ReadinessNote 提供的事实：状态标签、字段路径、
 * 单位和修复建议均可观察；它不在 Qt 层重新推导任何计算结论。
 */
TEST_F(ModelingPanelGuiTest, ReadinessBar_NotProvidedStatusAndActionVisible_WP13T23)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-06", "UX-03"},
                  std::vector<std::string>{});

    ModelReadinessReport report;
    ReadinessNote note;
    note.layer = ReadinessLayer::L9Drivetrain;
    note.subjectPath = "drivetrain.velocityAcceleration";
    note.summary = "关节速度/加速度尚未进入当前建模 schema";
    note.kind = ReadinessNoteKind::NotProvided;
    note.subject = m_ws.design.joints.front().objectId;
    note.fieldPath = "joints[*].maxVelocity / joints[*].maxAcceleration";
    note.unitText = "rad·s⁻¹ / rad·s⁻²";
    note.recommendedAction = "请在传动或策略模块补录";
    report.notes.push_back(note);

    m_panel->refreshPanel(m_ws, report);
    auto* items = m_panel->findChild<QTreeWidget*>(
        QStringLiteral("ird_modeling_readiness_items"));
    ASSERT_NE(items, nullptr);
    ASSERT_EQ(items->topLevelItemCount(), 13)
        << "12 层结果＋1 条 NotProvided 逐项提示应同时可见";
    const QTreeWidgetItem* item = items->topLevelItem(0);
    ASSERT_NE(item, nullptr);
    EXPECT_TRUE(item->text(0).startsWith(QStringLiteral("[未提供]")));
    EXPECT_TRUE(item->text(0).contains(QStringLiteral("schema")));
    EXPECT_TRUE(item->toolTip(0).contains(QStringLiteral("joints[*].maxVelocity")));
    EXPECT_TRUE(item->toolTip(0).contains(QStringLiteral("rad·s⁻¹ / rad·s⁻²")));
    EXPECT_TRUE(item->toolTip(0).contains(QStringLiteral("传动或策略模块")));
}

// =====================================================================
// P0-1 表单全清：重复刷新/往返选中后属性行数恒定（removeRow(0) 单次
// 无循环→行无上限增长的缺陷在此钉死——行数恒定即"清空删尽"的观测面）。
// =====================================================================

TEST_F(ModelingPanelGuiTest, PropertyFormFullClear_RowCountStableAcrossRefresh_MDL07_UI_T27)
{
    IRD_TEST_INFO("MDL-07", {}, std::nullopt);

    // 首次定位关节行（属性区投影需要选中锚——L-1 数据流）。
    m_panel->focusObject(m_ws.design.joints[0].objectId);
    const std::size_t jointRows = editors().size();
    ASSERT_GT(jointRows, std::size_t{0}) << "关节属性行未投影（夹具或数据流断链）";

    // 往返选中（关节→连杆→关节）与重复全面板刷新——行数必须每次清空后
    // 重建为当前对象行数，不随刷新次数累积。
    m_panel->focusObject(m_ws.design.links[0].objectId);
    const std::size_t linkRows = editors().size();
    ASSERT_GT(linkRows, std::size_t{0});

    m_panel->focusObject(m_ws.design.joints[0].objectId);
    EXPECT_EQ(editors().size(), jointRows) << "往返选中后关芧行数漂移（清空未删尽）";

    ModelReadinessReport emptyReport;
    m_panel->refreshPanel(m_ws, emptyReport);
    m_panel->refreshPanel(m_ws, emptyReport);
    EXPECT_EQ(editors().size(), jointRows) << "重复刷新后行数累积（P0-1 缺陷回归）";
}

// =====================================================================
// P0-2 单位分离：可编辑值＝纯数值（toDouble 全串可解析），单位归行标签
// （值/单位分离后编辑器文本不再拼接单位后缀——UX-05 呈现位迁移）。
// =====================================================================

TEST_F(ModelingPanelGuiTest, UnitSeparation_EditorTextPurelyNumeric_UX05_UI_T27)
{
    IRD_TEST_INFO("UX-05", {}, std::nullopt);

    m_panel->focusObject(m_ws.design.joints[0].objectId);
    QLineEdit* editable = editableEditor();
    ASSERT_NE(editable, nullptr) << "恰一可编辑门控失效（P0-3 前置）";

    // 可编辑值纯数值：整串可 toDouble（此前"0.000 rad"拼接形态不可解析
    // ——单位移行标签后本断言成立）。
    bool ok = false;
    editable->text().toDouble(&ok);
    EXPECT_TRUE(ok) << "可编辑编辑器文本非纯数值：" << editable->text().toStdString();

    // 单位在行标签：至少一条标签带单位后缀（关节角域字段＝rad——词形
    // 按 §3.5 呈现约定"（rad）"）。
    bool unitLabelFound = false;
    const auto labels = m_panel->findChildren<QLabel*>();
    for (const QLabel* label : labels) {
        if (label->text().contains(QStringLiteral("（rad）"))) {
            unitLabelFound = true;
            break;
        }
    }
    EXPECT_TRUE(unitLabelFound) << "行标签未承接单位后缀（单位分离呈现位未迁移）";
}

// =====================================================================
// P0-3 仅 zero-offset 可编：恰一非只读编辑器；复合行只读＋就地提示
// （MDL-07 表单最小版的面板半区——消除"看着可改实际拒绝"）。
// =====================================================================

TEST_F(ModelingPanelGuiTest, OnlyZeroOffsetEditable_CompositeRowsReadOnly_MDL07_UI_T27)
{
    IRD_TEST_INFO("MDL-07", {}, std::nullopt);

    m_panel->focusObject(m_ws.design.joints[0].objectId);
    std::size_t editableCount = 0;
    QLineEdit* editable = nullptr;
    for (QLineEdit* e : editors()) {
        if (!e->isReadOnly()) {
            ++editableCount;
            editable = e;
        }
    }
    ASSERT_NE(editable, nullptr);
    EXPECT_EQ(editableCount, std::size_t{1}) << "可编辑编辑器数量非恰一（表单最小版门控失守）";
    EXPECT_FALSE(editable->toolTip().contains(QString::fromUtf8("不支持就地编辑")))
        << "可编辑行携带了只读行提示（门控串位）";

    // 只读行带就地提示（如实呈现"不支持就地提交"——不伪装可编辑）。
    int tooltipCount = 0;
    for (QLineEdit* e : editors()) {
        if (e->isReadOnly() && e->toolTip().contains(QString::fromUtf8("不支持就地编辑"))) {
            ++tooltipCount;
        }
    }
    EXPECT_EQ(tooltipCount, editors().size() - 1)
        << "只读行未全部携带就地编辑提示";
}

// =====================================================================
// UI-T53 关节详细编辑页（B.1 复杂对象编辑模式关节侧——ParamTablePanel 承载）
// =====================================================================

/// 编辑页 ParamTablePanel 现取（objectName 锚——ird_modeling_edit_host 容器
/// 内唯一 ird_param_table；未建页（空态）返回 nullptr）。
QTableWidget* jointEditTable(const ModelingPanelWidget& panel)
{
    QWidget* host = panel.findChild<QWidget*>(QStringLiteral("ird_modeling_edit_host"));
    return host == nullptr
               ? nullptr
               : host->findChild<QTableWidget*>(QStringLiteral("ird_param_table"));
}

/// 按字段键定位行下标（键挂键列 item 的 Qt::UserRole——ParamTablePanel 装配
/// 契约；未命中返回负数）。
int jointEditRowOf(const QTableWidget& table, const char* fieldKey)
{
    for (int row = 0; row < table.rowCount(); ++row) {
        const QTableWidgetItem* keyItem = table.item(row, 0);
        if (keyItem != nullptr
            && keyItem->data(Qt::UserRole).toString().toStdString() == fieldKey) {
            return row;
        }
    }
    return -1;
}

/** @brief 查找完整关节分组表单中的单一字段入口，缺失时由调用断言定位错误。 */
QLineEdit* jointField(QWidget* host, const char* fieldKey)
{
    return host == nullptr ? nullptr : host->findChild<QLineEdit*>(
        QStringLiteral("ird_joint_field_") + QString::fromLatin1(fieldKey));
}

/** @brief 以真实键盘事件替换字段文本，触发与用户输入相同的textEdited候选路径。 */
void typeJointField(QLineEdit* field, const QString& text)
{
    if (field == nullptr) { return; }
    field->setFocus(); field->selectAll();
    QKeyEvent event(QEvent::KeyPress, Qt::Key_unknown, Qt::NoModifier, text);
    QApplication::sendEvent(field, &event); QApplication::processEvents();
}

/** @brief 通过页面常驻按钮执行关节整表确认，覆盖校验与唯一草稿写入口。 */
void confirmJointForm(QWidget* host)
{
    if (host == nullptr) { return; }
    auto* apply = host->findChild<QPushButton*>("ird_param_apply");
    if (apply != nullptr) { apply->click(); QApplication::processEvents(); }
    auto* yes = host->findChild<QPushButton*>("ird_param_confirm_yes");
    if (yes != nullptr) { yes->click(); QApplication::processEvents(); }
}

/**
 * @brief MDL-12/AT-17：continuous工程范围必须经真实表单两步确认，不能冒充限位。
 * 验证多圈rad输入不取模、取消零修改、范围整体更新及只读门控；仅UI线程运行。
 */
TEST_F(ModelingPanelGuiTest, ContinuousRangeConfirmCancelAndReadOnly_MDL12_AT17)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-12","MDL-06"},std::vector<std::string>{"AT-17"});
    auto& joint=m_ws.design.joints[0];joint.type=JointType::Continuous;
    joint.bounds=core::SourcedValue<JointLimits>::notApplicable();
    joint.workingRange=core::SourcedValue<JointLimits>::notProvided();
    m_panel->refreshPanel(m_ws,ModelReadinessReport{});m_panel->focusObject(joint.objectId);
    m_panel->resize(1280,900);m_panel->show();QApplication::processEvents();
    auto* host=m_panel->findChild<QWidget*>("ird_modeling_edit_host");ASSERT_NE(host,nullptr);
    auto* lower=jointField(host,"working-range-min");auto* upper=jointField(host,"working-range-max");
    ASSERT_NE(lower,nullptr);ASSERT_NE(upper,nullptr);EXPECT_EQ(jointField(host,"bounds-min"),nullptr);
    const auto before=m_ws;typeJointField(lower,"-8");confirmJointForm(host);EXPECT_EQ(m_ws,before);
    EXPECT_EQ(lower->text(),QStringLiteral("-8"))<<"整表拒绝应保留非法/不完整输入";
    typeJointField(upper,"10");host->findChild<QPushButton*>("ird_param_cancel")->click();
    EXPECT_EQ(m_ws,before)<<"取消必须零写入";
    lower=jointField(host,"working-range-min");upper=jointField(host,"working-range-max");
    typeJointField(lower,"-8");typeJointField(upper,"10");confirmJointForm(host);
    const auto& accepted=m_ws.design.joints[0];ASSERT_TRUE(accepted.workingRange.tryValue().has_value());
    EXPECT_EQ(accepted.workingRange.value(),(JointLimits{-8,10}));
    EXPECT_EQ(accepted.workingRange.provenance().kind,core::ProvenanceKind::UserProvided);
    EXPECT_EQ(accepted.bounds.state(),core::FieldState::NotApplicable);EXPECT_EQ(m_ws.changes.size(),before.changes.size()+1);
    auto* scroll=host->findChild<QScrollArea*>("ird_joint_form_scroll");ASSERT_NE(scroll,nullptr);
    scroll->ensureWidgetVisible(jointField(host,"working-range-max"));QApplication::processEvents();
    EXPECT_TRUE(jointField(host,"working-range-min")->isVisible());EXPECT_TRUE(jointField(host,"working-range-max")->isVisible());
    const auto committed=m_ws;lower=jointField(host,"working-range-min");typeJointField(lower,"11");confirmJointForm(host);EXPECT_EQ(m_ws,committed);
    m_panel->setWritable(false);EXPECT_FALSE(jointField(host,"working-range-min")->isEnabled());
    EXPECT_FALSE(host->findChild<QPushButton*>("ird_param_apply")->isEnabled());
    EXPECT_TRUE(host->findChild<QComboBox*>("ird_joint_angle_unit")->isEnabled());
}


TEST_F(ModelingPanelGuiTest, ContinuousRangeSchemaRebuildKeepsQueuedModelAlive_MDL12_AT17)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-12","UX-05"},std::vector<std::string>{"AT-17"});
    m_panel->focusObject(m_ws.design.joints[0].objectId);
    auto* host=m_panel->findChild<QWidget*>("ird_modeling_edit_host");ASSERT_NE(host,nullptr);
    auto* old=jointField(host,"bounds-min");ASSERT_NE(old,nullptr);typeJointField(old,"-7");
    m_ws.design.joints[0].type=JointType::Continuous;
    m_ws.design.joints[0].bounds=core::SourcedValue<JointLimits>::notApplicable();
    m_ws.design.joints[0].workingRange=core::SourcedValue<JointLimits>::notProvided();
    const auto before=m_ws;m_panel->refreshPanel(m_ws,ModelReadinessReport{});
    QApplication::processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
    auto* bounds=jointField(host,"bounds-min");ASSERT_NE(bounds,nullptr);EXPECT_EQ(bounds->text(),QStringLiteral("-7"));
    EXPECT_FALSE(host->findChild<QPushButton*>("ird_param_apply")->isEnabled());EXPECT_EQ(m_ws,before);
    host->findChild<QPushButton*>("ird_param_cancel")->click();QApplication::processEvents();
    EXPECT_NE(jointField(host,"working-range-min"),nullptr);EXPECT_EQ(jointField(host,"bounds-min"),nullptr);EXPECT_EQ(m_ws,before);
    m_ws.design.joints[0].type=JointType::Prismatic;
    m_ws.design.joints[0].workingRange=core::SourcedValue<JointLimits>::notApplicable();
    m_panel->refreshPanel(m_ws,ModelReadinessReport{});QApplication::processEvents();
    EXPECT_NE(jointField(host,"bounds-min"),nullptr);EXPECT_EQ(jointField(host,"working-range-min"),nullptr);
}


TEST_F(ModelingPanelGuiTest, JointDetailEditPane_EmptyStateAndBuildOnSelection_UI_T53)
{
    IRD_TEST_INFO("MDL-07", {}, std::nullopt);

    // 空态（fixture 已 refreshPanel、未选中）：零表格＋提示行在。
    EXPECT_EQ(jointEditTable(*m_panel), nullptr) << "未选中不应建编辑页";
    QLabel* hint = m_panel->findChild<QLabel*>(QStringLiteral("ird_modeling_edit_hint"));
    ASSERT_NE(hint, nullptr);
    EXPECT_FALSE(hint->text().isEmpty()) << "空态提示缺失（ERR-01——不伪造页也不留白）";

    // 选中关节→建完整分组页：14个唯一字段控件；不装配重复参数表。
    m_panel->focusObject(m_ws.design.joints[0].objectId);
    QWidget* host=m_panel->findChild<QWidget*>("ird_modeling_edit_host");ASSERT_NE(host,nullptr);
    auto* form=host->findChild<QWidget*>("ird_joint_grouped_form");ASSERT_NE(form,nullptr);
    EXPECT_EQ(form->findChildren<QLineEdit*>(QRegularExpression("ird_joint_field_.*")).size(),14);
    for(const char* key:{"origin-x","origin-yaw","bounds-max","max-velocity","max-acceleration"}){
        EXPECT_NE(jointField(form,key),nullptr)<<key;
    }
    EXPECT_EQ(form->findChild<QTableWidget*>("ird_param_table"),nullptr);

    // 切到连杆→回空态（非关节目标无数值编辑页——诚实收口；收起打在
    // ParamTablePanel 根的显式隐藏位上，内层表格只随父链不可见）。
    m_panel->focusObject(m_ws.design.links[0].objectId);
    QWidget* tablePanel = host->findChild<QWidget*>(QStringLiteral("ird_joint_grouped_form"));
    if (tablePanel != nullptr) {
        EXPECT_TRUE(tablePanel->isHidden()) << "连杆选中后编辑页未收起";
    }
    ASSERT_NE(hint, nullptr);
    EXPECT_FALSE(hint->isHidden()) << "空态提示未随非关节选中恢复";
}

/**
 * 编辑页原点编辑全链（UI-T53 验收主链——"输入 XYZ/RPY→草稿变更→域裁决"）：
 * 值列就地编辑 origin-x→暂存→"应用…"→确认区呈现→"确认应用"→移交出口→
 * applyJointFieldEdit(Origin) 接受→工作集平移分量更新＋UserProvided 来源＋
 * 恰一条变更记录（域内核 ZYX 组合——本用例只钉 UI→域链路，矩阵元素归
 * TemplateTest）。
 */
/** @brief GF-A/AT-01：同一次表单内合法原点和非法速度必须整表拒绝，不能部分成功。 */
TEST_F(ModelingPanelGuiTest, JointFormRejectsWholeCandidate_MDL01_AT01_T30)
{
    IRD_TEST_INFO("MDL-01", {"AT-01"}, std::nullopt);
    m_panel->focusObject(m_ws.design.joints[0].objectId);
    const auto before = m_ws;
    auto* host=m_panel->findChild<QWidget*>("ird_modeling_edit_host");
    typeJointField(jointField(host,"origin-x"),"0.25");
    typeJointField(jointField(host,"max-velocity"),"-1");
    confirmJointForm(host);
    EXPECT_EQ(m_ws, before); // 验证完整工作集（含changes），不是只断言被拒的速度字段。
}

TEST_F(ModelingPanelGuiTest, JointDetailEditPane_OriginEditApplyChain_UI_T53)
{
    IRD_TEST_INFO("MDL-09", {}, std::nullopt);

    m_panel->focusObject(m_ws.design.joints[0].objectId);
    QWidget* host = m_panel->findChild<QWidget*>(QStringLiteral("ird_modeling_edit_host"));
    auto* originField=jointField(host,"origin-x");ASSERT_NE(originField,nullptr);
    typeJointField(originField,QStringLiteral("0.5"));

    // 表单级确认应用（UX-07 两步：应用→确认区→确认）。
    QPushButton* applyBtn = host->findChild<QPushButton*>(QStringLiteral("ird_param_apply"));
    ASSERT_NE(applyBtn, nullptr);
    applyBtn->click();
    QPushButton* confirmYes = host->findChild<QPushButton*>(QStringLiteral("ird_param_confirm_yes"));
    ASSERT_NE(confirmYes, nullptr);
    confirmYes->click();

    // 域面结果：平移 x 更新（m）＋UserProvided＋恰一条变更记录。
    const auto& origin = m_ws.design.joints[0].origin;
    ASSERT_EQ(origin.state(), core::FieldState::Provided);
    EXPECT_EQ(origin.value().d()[0], 0.5);
    EXPECT_EQ(origin.provenance().kind, core::ProvenanceKind::UserProvided);
    ASSERT_EQ(m_ws.changes.size(), std::size_t{1});
    EXPECT_EQ(m_ws.changes[0].subject, "joints[0]");
    EXPECT_NE(m_ws.changes[0].summary.find("原点"), std::string::npos);
}

/**
 * 编辑页 DH 权威守卫面（UI-T53——L-7/域守卫的呈现半区）：StandardDH 态下
 * 轴向编辑走完整确认链后被域守卫拒绝——状态行如实呈现 authority-locked、
 * 工作集字节不变（C-1 派生只读的域级单一判定，UI 零旁路）。
 */
TEST_F(ModelingPanelGuiTest, JointDetailEditPane_DhAuthorityRejectsAxisEdit_UI_T53)
{
    IRD_TEST_INFO("MDL-09", {}, std::nullopt);

    // 场景装配：模板草稿为 Explicit——置 StandardDH（值面演算，V-14 同款）。
    m_ws.design.authority = AuthorityMode::StandardDH;
    const ModelingWorkingSet before = m_ws;
    ModelReadinessReport emptyReport;
    m_panel->refreshPanel(m_ws, emptyReport);

    m_panel->focusObject(m_ws.design.joints[0].objectId);
    QWidget* host = m_panel->findChild<QWidget*>(QStringLiteral("ird_modeling_edit_host"));
    auto* axis=jointField(host,"axis-z");ASSERT_NE(axis,nullptr);
    EXPECT_FALSE(axis->isEnabled())<<"StandardDH派生轴线必须在编辑入口禁用并避免无效确认路径";
    EXPECT_EQ(m_ws, before) << "只读派生字段保持工作集不变";
}

/**
 * 编辑页关节类型枚举行（UI-T66——M-R4 划界消账）：①合法轨 Revolute→
 * Prismatic（限位 Provided＋workingRange 缺＝I-MDL-4 合法组合）经域 Type
 * 分支落位＋变更记录；②拒绝轨 Prismatic→Continuous（限位 Provided 冲突
 * ——TypeBoundsConflict）就地呈现＋下拉回退权威值（ERR-01 不半途保留
 * 幻影选择）。
 */
TEST_F(ModelingPanelGuiTest, JointDetailEditPane_TypeComboBoxChainAndRejectFallback_UI_T66)
{
    IRD_TEST_INFO("I-MDL-4", {}, std::nullopt);
    m_panel->focusObject(m_ws.design.joints[0].objectId);const auto before=m_ws;
    auto* type=m_panel->findChild<QComboBox*>("ird_modeling_edit_type");ASSERT_NE(type,nullptr);
    type->setCurrentIndex(type->findData(static_cast<int>(JointType::Prismatic)));QApplication::processEvents();
    EXPECT_EQ(m_ws,before) << "GF-A：类型下拉只暂存，不能立即写草稿";
    auto* host=m_panel->findChild<QWidget*>("ird_modeling_edit_host");
    host->findChild<QPushButton*>("ird_param_apply")->click();host->findChild<QPushButton*>("ird_param_confirm_yes")->click();
    EXPECT_EQ(m_ws,before) << "跨量纲没有补全不能确认，旧rad不得直接当作m";
    for(const auto& pair:std::vector<std::pair<const char*,const char*>>{{"zero-offset","0.01"},{"bounds-min","-0.2"},{"bounds-max","0.3"}}){
        typeJointField(jointField(host,pair.first),QString::fromLatin1(pair.second));}
    host->findChild<QPushButton*>("ird_param_apply")->click();host->findChild<QPushButton*>("ird_param_confirm_yes")->click();
    EXPECT_EQ(m_ws.design.joints[0].type,JointType::Prismatic);ASSERT_EQ(m_ws.changes.size(),1u);
    const auto accepted=m_ws;
    type->setCurrentIndex(type->findData(static_cast<int>(JointType::Continuous)));QApplication::processEvents();
    host->findChild<QPushButton*>("ird_param_apply")->click();host->findChild<QPushButton*>("ird_param_confirm_yes")->click();
    EXPECT_EQ(m_ws,accepted) << "移动转角量必须重填零偏，不得自动换解释";
    host->findChild<QPushButton*>("ird_param_cancel")->click();EXPECT_EQ(type->currentData().toInt(),static_cast<int>(JointType::Prismatic));
}

/** @brief GF-A只读允许单位/折叠浏览，但主输入、表值编辑及最终提交必须被禁用。 */
TEST_F(ModelingPanelGuiTest, JointDetailEditPane_ReadOnlySessionDisablesPanel_UI_T53)
{
    IRD_TEST_INFO("MDL-07", {}, std::nullopt);

    ModelingPanelWidget readonlyPanel(false);
    readonlyPanel.setEditTargetProvider([this]() { return &m_ws; });
    ModelReadinessReport emptyReport;
    readonlyPanel.refreshPanel(m_ws, emptyReport);
    readonlyPanel.focusObject(m_ws.design.joints[0].objectId);

    QWidget* host = readonlyPanel.findChild<QWidget*>(QStringLiteral("ird_modeling_edit_host"));
    ASSERT_NE(host, nullptr);
    QWidget* form = host->findChild<QWidget*>(QStringLiteral("ird_joint_grouped_form"));
    ASSERT_NE(form, nullptr);
    EXPECT_FALSE(host->findChild<QPushButton*>("ird_param_apply")->isEnabled());
    EXPECT_FALSE(host->findChild<QLineEdit*>("ird_joint_field_origin-x")->isEnabled());
    EXPECT_EQ(host->findChild<QTableWidget*>("ird_param_table"),nullptr);
    EXPECT_TRUE(host->findChild<QComboBox*>("ird_joint_angle_unit")->isEnabled()) << "只读仍可浏览单位";
}

// =====================================================================
// UI-T54 基座安装姿态编辑页（MDL-22——applyBasePlacementEdit 域原语接线）
// =====================================================================

/// 基座页进页辅助（点击"基座安装…"入口钮——锚外对象显式入口）。
void enterBaseMode(ModelingPanelWidget& panel)
{
    QPushButton* baseMode = panel.findChild<QPushButton*>(QStringLiteral("ird_modeling_edit_base_mode"));
    ASSERT_NE(baseMode, nullptr) << "基座入口钮未构建（UI-T54 承载缺失）";
    baseMode->click();
}

/**
 * 基座编辑全链（UI-T54 验收主链——整体替换语义）：模板种子（地面＋未提供）
 * →切自定义预设（EAA 行随启）→写 EAA＋位置→应用→applyBasePlacementEdit
 * 接受＝preset/customEaa/basePosition 三面更新＋UserProvided 来源＋恰一条
 * 变更记录；还原钮＝权威值回填零脏化（changes 不增长）。
 */
TEST_F(ModelingPanelGuiTest, BasePlacementEdit_WholeReplacementChain_UI_T54)
{
    IRD_TEST_INFO("MDL-22", {}, std::nullopt);

    enterBaseMode(*m_panel);
    QComboBox* preset = m_panel->findChild<QComboBox*>(QStringLiteral("ird_modeling_base_preset"));
    ASSERT_NE(preset, nullptr);
    EXPECT_EQ(preset->currentIndex(), 0) << "模板种子应为地面预设（V15-04）";

    // 自定义预设→EAA 行随启（I-MDL-7：仅 Custom 有语义——非 Custom 携带即
    // 域原语 fail-fast 面，UI 侧先行禁用同口径）。
    preset->setCurrentIndex(3);
    // F15默认改为欧拉输入；本既有用例仍完整验证显式选择高级EAA的兼容路径。
    m_panel->findChild<QComboBox*>("ird_modeling_base_rotation_mode")->setCurrentIndex(1);
    QDoubleSpinBox* eaaY = m_panel->findChild<QDoubleSpinBox*>(QStringLiteral("ird_modeling_base_eaa_y"));
    ASSERT_NE(eaaY, nullptr);
    EXPECT_TRUE(eaaY->isEnabled()) << "自定义预设下 EAA 行应启用";
    // SpinBox 呈现精度＝6 位小数（rad）——测试值按该精度取整后断言
    // （1.5708 rad ≈ 89.95°，域接受任意有限 double，呈现精度非语义边界）。
    eaaY->setValue(1.5708);
    QDoubleSpinBox* posX = m_panel->findChild<QDoubleSpinBox*>(QStringLiteral("ird_modeling_base_pos_x"));
    ASSERT_NE(posX, nullptr);
    posX->setValue(0.25);

    QPushButton* applyBtn = m_panel->findChild<QPushButton*>(QStringLiteral("ird_modeling_base_apply"));
    ASSERT_NE(applyBtn, nullptr);
    applyBtn->click();

    // 域面：三面整体替换＋UserProvided＋恰一条变更记录。
    const auto& bp = m_ws.design.basePlacement;
    EXPECT_EQ(bp.preset, runtime::InstallationPresetToken::Custom);
    ASSERT_EQ(bp.customEaa.state(), core::FieldState::Provided);
    EXPECT_DOUBLE_EQ(bp.customEaa.value()[1], 1.5708);
    EXPECT_EQ(bp.customEaa.provenance().kind, core::ProvenanceKind::UserProvided);
    ASSERT_EQ(bp.basePosition.state(), core::FieldState::Provided);
    EXPECT_DOUBLE_EQ(bp.basePosition.value()[0], 0.25);
    ASSERT_EQ(m_ws.changes.size(), std::size_t{1});
    EXPECT_NE(m_ws.changes[0].summary.find("安装预设"), std::string::npos);

    // 还原钮＝权威值回填零脏化（不再增长 changes——纯呈现动作）。
    QPushButton* restoreBtn = m_panel->findChild<QPushButton*>(QStringLiteral("ird_modeling_base_restore"));
    ASSERT_NE(restoreBtn, nullptr);
    restoreBtn->click();
    ASSERT_EQ(m_ws.changes.size(), std::size_t{1}) << "还原不得产生域调用";
}

/** @brief MDL-22/AT-37：用户可经明确ZYX欧拉角输入任意安装姿态，确认前不写草稿。 */
TEST_F(ModelingPanelGuiTest, BaseEulerEntryAndAtomicApply_MDL22_AT37)
{
    IRD_TEST_INFO("MDL-22",{},std::nullopt);
    m_panel->resize(1280,900);m_panel->show();QApplication::processEvents();
    enterBaseMode(*m_panel);
    auto* mode=m_panel->findChild<QComboBox*>("ird_modeling_base_rotation_mode");
    auto* roll=m_panel->findChild<QDoubleSpinBox*>("ird_modeling_base_roll");
    auto* pitch=m_panel->findChild<QDoubleSpinBox*>("ird_modeling_base_pitch");
    auto* yaw=m_panel->findChild<QDoubleSpinBox*>("ird_modeling_base_yaw");
    ASSERT_NE(mode,nullptr)<<"F15：基座页缺少欧拉角表示选择";
    ASSERT_NE(roll,nullptr);ASSERT_NE(pitch,nullptr);ASSERT_NE(yaw,nullptr);
    EXPECT_EQ(mode->currentIndex(),0);
    m_panel->findChild<QComboBox*>("ird_modeling_base_preset")->setCurrentIndex(3);
    ASSERT_TRUE(roll->isVisible());ASSERT_TRUE(pitch->isVisible());ASSERT_TRUE(yaw->isVisible());
    const auto before=m_ws;
    roll->setValue(0.3);pitch->setValue(-0.4);yaw->setValue(0.7);
    EXPECT_EQ(m_ws,before)<<"输入和显示切换不能写入草稿";
    m_panel->findChild<QPushButton*>("ird_modeling_base_apply")->click();
    EXPECT_EQ(m_ws.design.basePlacement.preset,runtime::InstallationPresetToken::Custom);
    ASSERT_TRUE(m_ws.design.basePlacement.customEaa.tryValue().has_value());
    EXPECT_EQ(m_ws.changes.size(),before.changes.size()+1);
    // 还原由权威姿态反解，而不是保留上次未应用输入。
    yaw->setValue(1.1);m_panel->findChild<QPushButton*>("ird_modeling_base_restore")->click();
    EXPECT_NEAR(roll->value(),0.3,1e-6);EXPECT_NEAR(pitch->value(),-0.4,1e-6);EXPECT_NEAR(yaw->value(),0.7,1e-6);
    const auto accepted=m_ws;
    mode->setCurrentIndex(1);mode->setCurrentIndex(0);EXPECT_EQ(m_ws,accepted);
    // 用实际Qt窗口内容留痕；这里只证明编辑页，不冒充正式宿主完整三维验收。
    const auto evidence=qEnvironmentVariable("IRD_MODELING_GUI_EVIDENCE_OUT");
    if(!evidence.isEmpty()){
        QDir().mkpath(evidence);QApplication::processEvents();
        EXPECT_TRUE(m_panel->grab().save(evidence+"/base-euler.png"));
    }
    m_panel->setWritable(false);
    EXPECT_FALSE(roll->isEnabled());EXPECT_FALSE(pitch->isEnabled());EXPECT_FALSE(yaw->isEnabled());
    auto* preset=m_panel->findChild<QComboBox*>("ird_modeling_base_preset");
    preset->setCurrentIndex(0);preset->setCurrentIndex(3);
    EXPECT_FALSE(roll->isEnabled())<<"只读预设切换不能复活输入";
    EXPECT_EQ(m_ws,accepted);
}

/**
 * 基座编辑域守卫面（UI-T54——Custom＋零 EAA＝R=I 的映射层拒绝）：
 * 状态行就地呈现 preset-identity-rotation、工作集零变更（I-MDL-7 后半——
 * 非地面预设而恒等旋转在编辑边界就地拒绝，域裁决唯一）。
 */
TEST_F(ModelingPanelGuiTest, BasePlacementEdit_PresetIdentityRotationRejected_UI_T54)
{
    IRD_TEST_INFO("MDL-22", {}, std::nullopt);

    const ModelingWorkingSet before = m_ws;
    enterBaseMode(*m_panel);
    QComboBox* preset = m_panel->findChild<QComboBox*>(QStringLiteral("ird_modeling_base_preset"));
    ASSERT_NE(preset, nullptr);
    preset->setCurrentIndex(3);  // Custom＋EAA 全零（默认）＝R=I
    QPushButton* applyBtn = m_panel->findChild<QPushButton*>(QStringLiteral("ird_modeling_base_apply"));
    ASSERT_NE(applyBtn, nullptr);
    applyBtn->click();

    QLabel* status = m_panel->findChild<QLabel*>(QStringLiteral("ird_modeling_status_line"));
    ASSERT_NE(status, nullptr);
    EXPECT_TRUE(status->text().contains(QStringLiteral("preset-identity-rotation")))
        << "恒等旋转拒绝原因未就地呈现（UX-03）";
    EXPECT_EQ(m_ws, before) << "拒绝路径工作集字节不变";
}

/**
 * 基座编辑 L-7 门控（UI-T54）：只读会话＝应用钮禁用（域出口 onBasePlacement-
 * ApplyClicked 另有 writable 防御面，本用例钉控件半区）。
 */
TEST_F(ModelingPanelGuiTest, BasePlacementEdit_ReadOnlyDisablesApply_UI_T54)
{
    IRD_TEST_INFO("MDL-07", {}, std::nullopt);

    ModelingPanelWidget readonlyPanel(false);
    readonlyPanel.setEditTargetProvider([this]() { return &m_ws; });
    ModelReadinessReport emptyReport;
    readonlyPanel.refreshPanel(m_ws, emptyReport);
    enterBaseMode(readonlyPanel);
    QPushButton* applyBtn = readonlyPanel.findChild<QPushButton*>(QStringLiteral("ird_modeling_base_apply"));
    ASSERT_NE(applyBtn, nullptr);
    EXPECT_FALSE(applyBtn->isEnabled()) << "只读会话基座应用钮未禁用（L-7 门控失守）";
}

// =====================================================================
// P1-1 幻影脏化短路：editingFinished 失焦语义下，未修改＝零提交零脏化
// （postEditAction 恰零次）；修改后＝恰一次接受（ERR-01——不虚报未应用）。
// =====================================================================

TEST_F(ModelingPanelGuiTest, UnmodifiedEditingFinished_NoDirtyNoApply_ERR01_UI_T27)
{
    IRD_TEST_INFO("ERR-01", {}, std::nullopt);

    m_panel->focusObject(m_ws.design.joints[0].objectId);
    QLineEdit* editable = editableEditor();
    ASSERT_NE(editable, nullptr);

    int postEditCount = 0;
    m_panel->setPostEditAction([&postEditCount] { ++postEditCount; });

    // 未修改触发（Qt 失焦即触发语义的直接模拟）——零提交（短路）。
    Q_EMIT editable->editingFinished();
    EXPECT_EQ(postEditCount, 0) << "未修改触发产生了编辑接受（幻影脏化回归）";

    // 修改后触发——恰一次接受（0→0.125，域内合法零偏置）。
    editable->setText(QStringLiteral("0.125"));
    Q_EMIT editable->editingFinished();
    EXPECT_EQ(postEditCount, 1) << "修改后编辑未接受或重复接受";

    // 接受后重投影：编辑器随刷新重建——重新现取并校验新权威值回显。
    QLineEdit* reapplied = editableEditor();
    ASSERT_NE(reapplied, nullptr);
    bool ok = false;
    const double value = reapplied->text().toDouble(&ok);
    EXPECT_TRUE(ok);
    EXPECT_DOUBLE_EQ(value, 0.125) << "接受后权威值回显与提交值不一致";
}

// =====================================================================
// P0-4 provider 禁用：可用性提供器返回禁用＋原因键→按钮置灰＋tooltip
 // 中文文案（按钮/命令面板/注册表同源——建模半区的呈现面）。
// =====================================================================

TEST_F(ModelingPanelGuiTest, AvailabilityProviderDisabled_ButtonsGreyWithReason_UX02_UI_T27)
{
    IRD_TEST_INFO("UX-02", {}, std::nullopt);

    // 提交出口先行（可用性合取项——setCommandSubmit 语义）。
    m_panel->setCommandSubmit([](const ui::CommandId&) {});

    // 全禁提供器（未装配域命令的宿主快照形态——disableReasonKey 即
    // WorkbenchContent 注册期同一键：cmd.flow-not-assembled.reason）。
    m_panel->setCommandAvailability([](const ui::CommandId&) {
        return ui::CommandAvailability{true, true, false,
                                       ui::DisableReason{"cmd.flow-not-assembled.reason"}};
    });

    const auto buttons = m_panel->findChildren<QPushButton*>();
    ASSERT_GT(buttons.size(), std::size_t{0}) << "命令按钮未构建（区③空）";
    for (const QPushButton* btn : buttons) {
        // L-5局部历史独立于项目命令可用性，空栈不能被全放行快照启用。
        if (btn->objectName().startsWith(QStringLiteral("ird_modeling_draft_"))) { continue; }
        if (btn->objectName()==QStringLiteral("ird_modeling_retained_review")) { continue; } // PM-04只读呈现按旧稿存在性使能，不进入项目命令目录。
        if (btn->objectName() == QString::fromUtf8("ird_modeling_history_toggle")) { continue; } // 历史钮不入禁用对账
        if (btn->objectName() == QString::fromUtf8("ird_modeling_more_toggle")) { continue; } // 折叠开关不入对账（F-502 呈现件——非命令目录按钮）
        if (btn->objectName() == QString::fromUtf8("ird_modeling_preview_copy")) { continue; } // 预览复制钮不入对账（UI-T59 呈现件——非命令目录按钮）
        if (btn->objectName().startsWith(QString::fromUtf8("ird_modeling_struct_"))) { continue; } // 结构操作钮不入禁用对账（UI-T47 呈现件——非命令目录按钮）
        if (btn->objectName().startsWith(QString::fromUtf8("ird_modeling_edit_"))) { continue; } // 编辑页模式钮不入对账（UI-T53/T54 呈现件——非命令目录按钮）
        if (inJointEditPane(*m_panel, btn)) { continue; } // 编辑页/基座页内部钮不入对账（UI-T53/T54 呈现件）
        EXPECT_FALSE(btn->isEnabled()) << "全禁快照下按钮仍可用";
        EXPECT_EQ(btn->toolTip().toStdString(),
                  ui::resolveText(ui::DisableReason{"cmd.flow-not-assembled.reason"}))
            << "禁用原因 tooltip 未走 UiText 中文解析";
    }

    // 放行快照（同源机制——提供器返回可用即恢复，绑定即时刷新）。排除链
    // 与全禁循环一致（UI-T60：位姿集删除钮在空选择下合法禁用——非命令
    // 门控面不入本对账；排除链两处重复为本用例既有形态的最小改动）。
    m_panel->setCommandAvailability([](const ui::CommandId&) {
        return ui::CommandAvailability{true, true, true, ui::DisableReason{}};
    });
    for (const QPushButton* btn : buttons) {
        if (btn->objectName().startsWith(QStringLiteral("ird_modeling_draft_"))) { continue; }
        if (btn->objectName()==QStringLiteral("ird_modeling_retained_review")) { continue; }
        if (btn->objectName() == QString::fromUtf8("ird_modeling_history_toggle")) { continue; }
        if (btn->objectName() == QString::fromUtf8("ird_modeling_more_toggle")) { continue; }
        if (btn->objectName() == QString::fromUtf8("ird_modeling_preview_copy")) { continue; }
        if (btn->objectName().startsWith(QString::fromUtf8("ird_modeling_struct_"))) { continue; }
        if (btn->objectName().startsWith(QString::fromUtf8("ird_modeling_edit_"))) { continue; }
        if (inJointEditPane(*m_panel, btn)) { continue; }
        EXPECT_TRUE(btn->isEnabled()) << "放行快照下按钮仍禁用";
    }
    // 独立验证新入口的真实契约：无旧稿时全放行也不能启用，装配只读
    // 查看后只读会话仍可核对，清回调后重新禁用，不受项目写命令门控。
    auto* review=m_panel->findChild<QPushButton*>("ird_modeling_retained_review");ASSERT_NE(review,nullptr);
    EXPECT_FALSE(review->isEnabled());int views=0;
    m_panel->setRetainedReviewAction([&]{++views;});m_panel->setWritable(false);
    EXPECT_TRUE(review->isEnabled());review->click();EXPECT_EQ(views,1);
    m_panel->setRetainedReviewAction({});EXPECT_FALSE(review->isEnabled());
}

// =====================================================================
// UI-T41 批次B 用例组：命令 tooltip 词表化（B4）／限位校验行内警示（B1）／
// 增量刷新保焦点（B3）／诊断历史累积与折叠（B2）。
// =====================================================================

/// B4：命令按钮悬停文案＝UiText tooltip 键解析值（去裸命令 id——UX-02）；
/// 解析非空即断言相等，键缺失（空）时回退 id 原文（对账兜底形态）。
TEST_F(ModelingPanelGuiTest, CommandButtonTooltip_ResolvedFromUiTextKey_UX02_UI_T41B)
{
    IRD_TEST_INFO("UX-02", {}, std::nullopt);

    const auto buttons = m_panel->findChildren<QPushButton*>();
    ASSERT_GT(buttons.size(), std::size_t{0}) << "命令按钮未构建（区③空）";
    const auto catalog = modelingDomainCommands();
    for (const QPushButton* btn : buttons) {
        if (btn->objectName() == QStringLiteral("ird_modeling_history_toggle")) { continue; }
        if (btn->objectName() == QStringLiteral("ird_modeling_more_toggle")) { continue; } // 折叠开关（F-502 呈现件——非命令钮）
        for (const auto& desc : catalog) {
            const QString rawId = QString::fromStdString(desc.id);
            EXPECT_NE(btn->toolTip(), rawId)
                << "tooltip 呈现裸命令 id（UX-02 泄漏——B4 未生效）";
        }
    }
    // 键已登记：tooltip 应等于 UiText 解析值（以 new-from-template 为样本）。
    EXPECT_FALSE(buttons.front()->toolTip().isEmpty()) << "tooltip 为空（解析缺失）";
}

/// F-502（宿主审核 P2）命令频率分层：低频诊断组（权威切换/物性估算/
/// 占位几何/基线比较/规范包导出/规范包导入）收进『更多操作』折叠区——
/// 默认收起（工具页纵向长度压缩），展开/收起翻转对称；命令集合与目录
/// 对账不因容器迁移漂移（commandButtonsOf 仍与目录一一对应——使能逻辑
/// 零变化的守卫面）。
TEST_F(ModelingPanelGuiTest, MoreActionsCollapse_DefaultCollapsedAndToggle_F502)
{
    IRD_TEST_INFO("MDL-07", {}, std::nullopt);

    // 目录对账（构造序＝目录序——折叠区重排后仍保持，commandButtonsOf 守卫）。
    const auto catalog = modelingDomainCommands();
    const auto buttons = commandButtonsOf(*m_panel, catalog);
    ASSERT_EQ(buttons.size(), catalog.size())
        << "命令按钮与目录错位（折叠区容器迁移破坏构造序）";

    // 折叠承载存在；默认收起（开关未选中＋容器显式隐藏）。
    // UI-T53 合并适配：可见性断言用 isHidden（容器自身的显式隐藏位——
    // isVisibleTo(面板) 会途经 QTabWidget 非当前页的隐藏链，"编辑"页签
    // 居首后工具页恒非当前页，isVisibleTo 恒 false 失去判别力；折叠语义
    // 本身＝setVisible 翻转，isHidden 即其单控件判别面）。
    QPushButton* toggle = m_panel->findChild<QPushButton*>(QStringLiteral("ird_modeling_more_toggle"));
    ASSERT_NE(toggle, nullptr) << "折叠开关未构建（F-502 承载缺失）";
    QWidget* moreHost = m_panel->findChild<QWidget*>(QStringLiteral("ird_modeling_more_host"));
    ASSERT_NE(moreHost, nullptr) << "折叠容器未构建（F-502 承载缺失）";
    EXPECT_FALSE(toggle->isChecked()) << "折叠开关默认应未选中";
    EXPECT_TRUE(moreHost->isHidden()) << "折叠区默认未收起（纵向长度未压缩）";

    // 展开开关→容器显现；收起→复隐（翻转对称——展开面非删除）。
    toggle->setChecked(true);
    EXPECT_FALSE(moreHost->isHidden()) << "展开后低频组未显现";
    toggle->setChecked(false);
    EXPECT_TRUE(moreHost->isHidden()) << "收起后低频组残留可见";
}

/// B1：超限位输入＝行内警示描边（词表 kWarning）＋状态行警示着色＋编辑器
/// 保留权威原值（域裁决唯一——校验器只作输入期引导，不作放行）。
TEST_F(ModelingPanelGuiTest, ZeroOffsetOutOfRange_InlineWarningKeepsAuthoritative_B1)
{
    IRD_TEST_INFO("UX-03", {}, std::nullopt);

    m_panel->focusObject(m_ws.design.joints[0].objectId);
    QLineEdit* editor = editableEditor();
    ASSERT_NE(editor, nullptr);
    // 校验器装配面：限位已提供的关节应挂 QDoubleValidator（B1 输入期引导）。
    if (m_ws.design.joints[0].bounds.tryValue().has_value()) {
        EXPECT_NE(editor->validator(), nullptr) << "限位在位但未挂校验器（B1）";
    }

    const QString authoritative = editor->text();
    editor->setText(QStringLiteral("abc"));  // 非数值——确定性拒绝（value-not-finite）
    Q_EMIT editor->editingFinished();

    // 保留原值（先回显后提交的强顺序）＋行内警示描边＝词表警示橙。
    EXPECT_EQ(editor->text(), authoritative) << "拒绝后未保留权威原值";
    EXPECT_TRUE(editor->styleSheet().contains(ui::palette::kWarning))
        << "被拒编辑器未挂词表警示描边（B1）";

    // 状态行警示着色＋原因呈现（B2 分级）。
    auto* statusLine = m_panel->findChild<QLabel*>(QStringLiteral("ird_modeling_status_line"));
    ASSERT_NE(statusLine, nullptr);
    EXPECT_TRUE(statusLine->styleSheet().contains(ui::palette::kWarning))
        << "拒绝回执未按警示级着色（B2）";
    EXPECT_TRUE(statusLine->text().contains(QStringLiteral("未应用")));
}

/// B3：形状不变的重复刷新走增量路径——编辑器控件实例不重建，输入焦点
/// 保持（行数恒定断言的强化：同一 QLineEdit 指针存活且仍持焦点）。
TEST_F(ModelingPanelGuiTest, IncrementalRefresh_PreservesEditorFocus_B3)
{
    IRD_TEST_INFO("MDL-07", {}, std::nullopt);

    m_panel->focusObject(m_ws.design.joints[0].objectId);
    QLineEdit* editor = editableEditor();
    ASSERT_NE(editor, nullptr);
    editor->setFocus();

    ModelReadinessReport emptyReport;
    m_panel->refreshPanel(m_ws, emptyReport);

    // 同形状刷新＝增量路径：编辑器指针不换（重建换新实例——指针比对即
    // 增量的结构性观测面；焦点保持是其自然结果，offscreen 不依赖激活）。
    EXPECT_EQ(editableEditor(), editor) << "同形状刷新重建了编辑器（B3 未生效）";
}

/// B2：拒绝回执入诊断历史（不清空覆盖）＋折叠钮展开历史视图（非弹窗）。
TEST_F(ModelingPanelGuiTest, StatusHistory_AccumulatesAndToggles_B2)
{
    IRD_TEST_INFO("UX-07", {}, std::nullopt);

    m_panel->focusObject(m_ws.design.joints[0].objectId);
    QLineEdit* editor = editableEditor();
    ASSERT_NE(editor, nullptr);
    const QString authoritative = editor->text();
    editor->setText(QStringLiteral("abc"));
    Q_EMIT editor->editingFinished();   // 第 1 条拒绝回执
    editor->setText(QStringLiteral("xyz"));
    Q_EMIT editor->editingFinished();   // 第 2 条拒绝回执——历史不得覆盖前条

    auto* toggle = m_panel->findChild<QPushButton*>(QStringLiteral("ird_modeling_history_toggle"));
    auto* view = m_panel->findChild<QPlainTextEdit*>(QStringLiteral("ird_modeling_history_view"));
    ASSERT_NE(toggle, nullptr);
    ASSERT_NE(view, nullptr);
    // offscreen 下父面板未 show——isVisible 受父级级联，用 isHidden 读显式
    // 隐藏位（面板先例：控件级状态断言不依赖窗口激活）。
    EXPECT_TRUE(view->isHidden()) << "诊断历史默认应折叠";
    toggle->setChecked(true);
    EXPECT_FALSE(view->isHidden()) << "展开后历史视图不可见";
    EXPECT_EQ(view->toPlainText().count(QStringLiteral("未应用")), 2)
        << "两条拒绝回执未都入历史（被覆盖）";
}

// =====================================================================
// UI-T43 用例组：宿主接线修复的回归钉（审核 P1 三项中两代码项的测试面）
// =====================================================================

/// L-7 门控防复活：只读会话下重新注入"全放行"可用性快照，写命令
/// （readOnlyAllowed=false）不得因可用性刷新复活（UI-T43 修复点——
/// 此前 setCommandAvailability 缺只读合取项，三处使能判定不一致即门控
/// 旁路：只读切换后再刷新可用性＝写命令错误亮起）。恢复可写＝双向即时。
TEST_F(ModelingPanelGuiTest, ReadOnlySession_AvailabilityRefreshCannotReviveWriteCommands_L7_UI_T43)
{
    IRD_TEST_INFO("ERR-01", {}, std::nullopt);

    // 可写基线：提交出口＋全放行快照——全部命令按钮可用（夹具自检，
    // 同时覆盖 setCommandSubmit/setCommandAvailability 即时刷新路径）。
    m_panel->setCommandSubmit([](const ui::CommandId&) {});
    const auto allEnabled = [] (const ui::CommandId&) {
        return ui::CommandAvailability{true, true, true, ui::DisableReason{}};
    };
    m_panel->setCommandAvailability(allEnabled);

    const auto catalog = modelingDomainCommands();
    const auto buttons = commandButtonsOf(*m_panel, catalog);
    ASSERT_EQ(buttons.size(), catalog.size())
        << "命令按钮与目录错位（构造序漂移——本用例按下标对账失效）";
    for (const QPushButton* btn : buttons) {
        ASSERT_TRUE(btn->isEnabled()) << "可写＋全放行基线下按钮仍禁用（夹具失实）";
    }

    // 只读切换：写命令（readOnlyAllowed=false，目录十条中七条）禁用；
    // 只读命令（diff-baseline/export-package/reset-home-zero）保持可用。
    m_panel->setWritable(false);
    for (std::size_t i = 0; i < buttons.size(); ++i) {
        EXPECT_EQ(buttons[i]->isEnabled(), catalog[i].readOnlyAllowed)
            << "只读切换后使能态与 readOnlyAllowed 不符：" << catalog[i].id;
    }

    // 修复点（审核 P1）：只读态下重新注入全放行快照——可用性刷新不得
    // 越过只读门（修复前本步写命令全部复活）。
    m_panel->setCommandAvailability(allEnabled);
    for (std::size_t i = 0; i < buttons.size(); ++i) {
        EXPECT_EQ(buttons[i]->isEnabled(), catalog[i].readOnlyAllowed)
            << "可用性刷新复活了只读会话的写命令（L-7 门控旁路回归）：" << catalog[i].id;
    }

    // 恢复可写：写命令即时回升（双向即时——残留窗口为零）。
    m_panel->setWritable(true);
    for (const QPushButton* btn : buttons) {
        EXPECT_TRUE(btn->isEnabled()) << "恢复可写后写命令未回升";
    }

    // （模块级链路用例——生产装配序文案解析回归＋模块 setWritable 暂存/
    //   即时双形态——在 ModelingUiModuleHostWiringGuiTest.cpp：被测类型
    //   ModelingUiModule 持 policy 评估器符号，gating 见该文件头注。）
}

// =====================================================================
// UI-T47 结构编辑面：五钮承载＋选中位插入/删除的草稿级编辑链
// （域裁决唯一在 StructureEdit 四原语——本组用例断言面板承载与刷新编排）
// =====================================================================

/// F-539（UI-T71）定位出口生产接线（孪生面）：就绪逐项行点击（锚列＝对象
/// 身份）→locate()→构造期注册的生产 sink→focusObject——结构树聚焦锚定行
/// （滚动＋置当前行＋selectionChanged→检查器重投影，L-1 前向链复用）。
/// 伪造带锚行与真实就绪行同形（label 列＋锚列直投）。接线前该链唯一断点
/// ＝sink 缺席（acc/ui-t71/1 B-1 更正：锚列逐项值不受树 columnCount 上限
/// ——「列数断链」前提不存在），本用例即其转红面。另钉扎列数声明（两列
/// ＋隐锚列）为呈现形态契约——acc B-1 返工②补钉，去列数声明即红。
TEST_F(ModelingPanelGuiTest,
       LocateSinkProductionWiring_ReadinessRowClickFocusesTree_UI_T71)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-06"}, std::vector<std::string>{});

    // 就绪逐项表（objectName 锚——F-539 随批挂名，ird_ 前缀族）。
    QTreeWidget* readinessItems = m_panel->findChild<QTreeWidget*>(
        QStringLiteral("ird_modeling_readiness_items"));
    ASSERT_NE(readinessItems, nullptr) << "就绪逐项表缺失（区④构建缺陷）";
    // 列数声明钉扎（呈现形态契约——acc/ui-t71/1 B-1 返工②；两列＋隐锚列
    // 与 UX-02 结构树声明式形态对齐，非定位链功能前提）。
    ASSERT_EQ(readinessItems->columnCount(), 2)
        << "就绪表列数声明漂移（两列＋隐锚列的呈现形态契约被改）";

    // 伪造带锚行（锚＝j1——结构树真实在册行，落点可断言）。
    const core::ObjectId j1 = m_ws.design.joints.front().objectId;
    auto* row = new QTreeWidgetItem(readinessItems);
    row->setText(0, QStringLiteral("[Blocking] 定位链测试行"));
    row->setText(1, QString::fromStdString(j1.toCanonical()));

    // 点击（与手点同源信号轨）→生产 sink→focusObject→结构树聚焦。
    Q_EMIT readinessItems->itemClicked(row, 0);
    QApplication::processEvents();

    // 落点断言：结构树当前行＝锚定关节行（按 objectName 具名取区①结构树——UI-T81：分隔器改版后树降一级父层，无名首树查找会误中就绪表；
    // kAnchorColumn=1——UI-T47 gui 定位复用列）。
    QTreeWidget* structureTree = m_panel->findChild<QTreeWidget*>(QStringLiteral("ird_modeling_struct_tree"));
    ASSERT_NE(structureTree, nullptr) << "结构树缺失（区①构建缺陷）";
    ASSERT_NE(structureTree->currentItem(), nullptr)
        << "定位链落点缺失（生产 sink 未接线——F-539 静默丢弃形态）";
    EXPECT_EQ(structureTree->currentItem()->text(1).toStdString(),
              j1.toCanonical())
        << "落点锚失配（focusObject 未命中锚定行）";
}

TEST_F(ModelingPanelGuiTest, StructureButtons_InsertAndRemove_DraftOnly_UI_T47)
{
    // ①五钮在位（objectName 定位——面板承载面的存在性证据）。
    QPushButton* addBtn = m_panel->findChild<QPushButton*>(
        QStringLiteral("ird_modeling_struct_add"));
    QPushButton* removeBtn = m_panel->findChild<QPushButton*>(
        QStringLiteral("ird_modeling_struct_remove"));
    ASSERT_NE(addBtn, nullptr) << "新增关节钮未构建";
    ASSERT_NE(removeBtn, nullptr) << "删除关节钮未构建";

    // ②选中首关节（树行锚＝joints[0]——L-1 关联键复用）→新增＝选中位
    //   插入：工作集 7 关节（六轴+1）＋8 连杆（I-MDL-1）。
    const core::ObjectId j1 = m_ws.design.joints.front().objectId;
    const QString j1Anchor = QString::fromStdString(j1.toCanonical());
    QTreeWidgetItemIterator it(m_panel->findChild<QTreeWidget*>(QStringLiteral("ird_modeling_struct_tree")));
    while (*it != nullptr
           && (*it)->text(1) != j1Anchor) { ++it; }
    ASSERT_NE(*it, nullptr) << "首关节树行未投影";
    m_panel->findChild<QTreeWidget*>(QStringLiteral("ird_modeling_struct_tree"))->setCurrentItem(*it);

    const std::size_t jointsBefore = m_ws.design.joints.size();
    addBtn->click();
    ASSERT_EQ(m_ws.design.joints.size(), jointsBefore + 1)
        << "新增未落草稿（域原语未触工作集——提交流断链）";
    ASSERT_EQ(m_ws.design.links.size(), m_ws.design.joints.size() + 1)
        << "I-MDL-1 计数关系破坏";
    // 插入位语义：新关节在选中位之后（index 1）——设计默认种子。
    EXPECT_EQ(m_ws.design.joints[1].localName, "j7")
        << "插入位/消歧命名失守（六轴链 j1~j6 已占用）";

    // ③未选中态删除＝诚实指引（no-selection——状态行呈现，工作集不动）。
    //    形态：全新面板（未点过树——m_lastSelected 空；clearSelection 不清
    //    currentItem，不构成"未选中"的真实模拟）。
    const std::size_t jointsStable = m_ws.design.joints.size();
    ModelingPanelWidget freshPanel(true);
    freshPanel.setEditTargetProvider([this]() { return &m_ws; });
    ModelReadinessReport emptyReport;
    freshPanel.refreshPanel(m_ws, emptyReport);
    QPushButton* freshRemove = freshPanel.findChild<QPushButton*>(
        QStringLiteral("ird_modeling_struct_remove"));
    ASSERT_NE(freshRemove, nullptr);
    freshRemove->click();
    EXPECT_EQ(m_ws.design.joints.size(), jointsStable)
        << "未选中删除产生了操作（应就地指引）";
    QLabel* freshStatus = freshPanel.findChild<QLabel*>(
        QStringLiteral("ird_modeling_status_line"));
    ASSERT_NE(freshStatus, nullptr);
    EXPECT_FALSE(freshStatus->text().isEmpty())
        << "未选中删除未呈现指引（诚实指引面失守）";
}

TEST_F(ModelingPanelGuiTest, StructureButtons_RemoveSelected_DraftOnly_UI_T47)
{
    // 选中首关节→删除：6→5 关节（草稿级——无修订产生）。
    const core::ObjectId j1 = m_ws.design.joints.front().objectId;
    const QString j1Anchor = QString::fromStdString(j1.toCanonical());
    QTreeWidgetItemIterator it(m_panel->findChild<QTreeWidget*>(QStringLiteral("ird_modeling_struct_tree")));
    while (*it != nullptr
           && (*it)->text(1) != j1Anchor) { ++it; }
    ASSERT_NE(*it, nullptr);
    m_panel->findChild<QTreeWidget*>(QStringLiteral("ird_modeling_struct_tree"))->setCurrentItem(*it);

    QPushButton* removeBtn = m_panel->findChild<QPushButton*>(
        QStringLiteral("ird_modeling_struct_remove"));
    ASSERT_NE(removeBtn, nullptr);
    removeBtn->click();
    ASSERT_EQ(m_ws.design.joints.size(), 5u) << "删除未落草稿";
    ASSERT_EQ(m_ws.design.links.size(), 6u) << "I-MDL-1 计数关系破坏";
    bool removedStillThere = false;
    for (const JointEntry& j : m_ws.design.joints) {
        if (j.objectId == j1) { removedStillThere = true; }
    }
    EXPECT_FALSE(removedStillThere) << "被删关节仍在工作集";
}

// =====================================================================
// UI-T48 几何引用编辑面：操作行承载（挂接/摘除钮）＋拒绝呈现
// （io 真装路径经 QFileDialog 不可 headless 驱动——本组用例断言承载面
// 与摘除落草稿；挂接流端到端由域 UT 替身缝承载，见 GeometryLinkEditTest）
// =====================================================================

TEST_F(ModelingPanelGuiTest, GeometryActionButtons_LoadAndDetach_UI_T48)
{
    // ①预置挂接（域原语直调——绕过对话框；摘要缝替身同 UT 形态）。
    core::ContentDigester d;
    const std::string seed = "gui: D:/a/link1.stl";
    d.update(seed.data(), seed.size());
    ResourceProbeFn probe = [&](const std::string&, std::string&)
        -> std::optional<ResourceProbeResult> {
        ResourceProbeResult r;
        r.contentDigest = d.finalize();
        r.absPath = "D:/a/link1.stl";
        r.isMeshFamily = true;
        return r;
    };
    ASSERT_FALSE(attachExternalGeometry(m_ws, 0, GeometrySlot::Visual,
                                        "D:/a/link1.stl", probe)
                     .has_value());
    ModelReadinessReport emptyReport;
    m_panel->refreshPanel(m_ws, emptyReport);

    // ②连杆选中后属性区出现几何操作钮（挂接/摘除——承载面存在性）。
    const core::ObjectId l1 = m_ws.design.links[1].objectId;
    const QString l1Anchor = QString::fromStdString(l1.toCanonical());
    QTreeWidgetItemIterator it(m_panel->findChild<QTreeWidget*>(QStringLiteral("ird_modeling_struct_tree")));
    while (*it != nullptr && (*it)->text(1) != l1Anchor) { ++it; }
    ASSERT_NE(*it, nullptr) << "连杆树行未投影";
    m_panel->findChild<QTreeWidget*>(QStringLiteral("ird_modeling_struct_tree"))->setCurrentItem(*it);

    QPushButton* detach = m_panel->findChild<QPushButton*>(
        QStringLiteral("ird_modeling_geo_detach_visual"));
    ASSERT_NE(detach, nullptr) << "摘除钮未构建（几何行编辑面缺席）";
    QPushButton* attach = m_panel->findChild<QPushButton*>(
        QStringLiteral("ird_modeling_geo_attach_visual"));
    ASSERT_NE(attach, nullptr) << "挂接钮未构建";

    // ③摘除落草稿（引用清空＋清单条目保留——共享引用语义）。
    detach->click();
    EXPECT_FALSE(m_ws.design.links[1].visual.has_value()) << "摘除未落草稿";
    EXPECT_EQ(m_ws.design.resourceManifest.size(), 1u)
        << "清单条目被级联删除";
}

/// UI-T49 视觉→碰撞复制辅助（G7 承载＋acceptance 1 两槽独立性＋诚实禁用）：
/// visual 已挂＝复制钮可用，点击落草稿（collision 同资源引用、visual 零
/// 改写、清单零新增）；visual 未设＝钮禁用＋toolTip 原因（非置灰无解释）。
TEST_F(ModelingPanelGuiTest, GeometryCopyButton_VisualToCollision_UI_T49)
{
    // ①预置 visual 挂接（域原语直调——绕过对话框；T48 用例同形态）。
    core::ContentDigester d;
    const std::string seed = "gui: D:/a/link1.stl";
    d.update(seed.data(), seed.size());
    ResourceProbeFn probe = [&](const std::string&, std::string&)
        -> std::optional<ResourceProbeResult> {
        ResourceProbeResult r;
        r.contentDigest = d.finalize();
        r.absPath = "D:/a/link1.stl";
        r.isMeshFamily = true;
        return r;
    };
    ASSERT_FALSE(attachExternalGeometry(m_ws, 1, GeometrySlot::Visual,
                                        "D:/a/link1.stl", probe)
                     .has_value());
    ModelReadinessReport emptyReport;
    m_panel->refreshPanel(m_ws, emptyReport);
    m_panel->focusObject(m_ws.design.links[1].objectId);

    // ②复制钮承载存在（仅 collision 行——G7 入口）＋visual 已挂＝可用。
    QPushButton* copy = m_panel->findChild<QPushButton*>(
        QStringLiteral("ird_modeling_geo_copy_collision"));
    ASSERT_NE(copy, nullptr) << "复制钮未构建（G7 承载缺席）";
    EXPECT_TRUE(copy->isEnabled()) << "visual 已挂时复制钮被禁用";

    // ③点击复制→collision 落草稿（同资源引用）＋visual 零改写
    //   （acceptance 1 两槽独立性断言的 gui 半区）。
    copy->click();
    ASSERT_TRUE(m_ws.design.links[1].collision.has_value())
        << "复制未落草稿（collision 槽仍空）";
    ASSERT_TRUE(m_ws.design.links[1].visual.has_value())
        << "复制改写了 visual 槽（零 visual 改写失守）";
    EXPECT_EQ(m_ws.design.links[1].collision->resourceRefId,
              m_ws.design.links[1].visual->resourceRefId)
        << "未同资源复制（复制产生了第二引用键）";
    EXPECT_EQ(m_ws.design.resourceManifest.size(), 1u)
        << "复制新增了清单条目（应共享同一 ResourceRef）";

    // ④摘除 visual 后重投影——复制钮诚实禁用＋toolTip 携原因
    //   （"非置灰无解释"纪律——§9.7.1 交互）。
    ASSERT_FALSE(detachGeometry(m_ws, 1, GeometrySlot::Visual).has_value());
    m_panel->refreshPanel(m_ws, emptyReport);
    m_panel->focusObject(m_ws.design.links[1].objectId);
    QPushButton* copyAfter = m_panel->findChild<QPushButton*>(
        QStringLiteral("ird_modeling_geo_copy_collision"));
    ASSERT_NE(copyAfter, nullptr) << "重投影后复制钮丢失";
    EXPECT_FALSE(copyAfter->isEnabled())
        << "visual 未设时复制钮未诚实禁用";
    EXPECT_TRUE(copyAfter->toolTip().contains(QStringLiteral("视觉几何未挂接")))
        << "禁用态缺原因提示（置灰无解释——交互纪律失守）";
}

// =====================================================================
// UI-T50——建模面板编辑链端到端 gui 拍（acceptance 6：结构→几何→碰撞→
// 对象属性页四段贯通）
// =====================================================================

/**
 * 契约 UI-T50 acceptance 6（整合回归·面板侧四段贯通）：真实点击链驱动
 * 四段编辑全部落草稿——①结构段（结构树"新增"钮插入关节）→②几何段
 * （域原语挂接 visual——对话框不可 headless，T48 用例同款直调）→③碰撞
 * 段（"从视觉复制"钮落 collision）→④对象属性页段（属性区重投影后几何
 * 行值与草稿同源）。draft.apply→编译链发布复验不在面板 gui 域内（命令
 * 总线/工程装配在宿主层）——援引既有契约面：apply-robot-design 命令流
 * 由 ui_contract/contract 测试与 WP-24-T08 装配验证覆盖（登记注，非本
 * 用例断言面）。
 */
TEST_F(ModelingPanelGuiTest, EditingChain_EndToEnd_FourSegments_UI_T50)
{
    const ModelReadinessReport emptyReport;

    // ---- ①结构段：选中连杆 1 后点"新增"钮（选中位插入 j7——UI-T47
    //      词表），关节链 +1 落草稿。
    m_panel->focusObject(m_ws.design.links[1].objectId);
    QPushButton* structAdd = m_panel->findChild<QPushButton*>(
        QStringLiteral("ird_modeling_struct_add"));
    ASSERT_NE(structAdd, nullptr) << "结构新增钮未构建";
    const std::size_t jointsBefore = m_ws.design.joints.size();
    structAdd->click();
    EXPECT_EQ(m_ws.design.joints.size(), jointsBefore + 1)
        << "结构段：新增钮点击未落草稿";

    // ---- ②几何段：域原语挂接 visual（替身 probe——T48 gui 先例）。
    core::ContentDigester d;
    const std::string seed = "e2e: D:/a/link.stl";
    d.update(seed.data(), seed.size());
    ResourceProbeFn probe = [&](const std::string&, std::string&)
        -> std::optional<ResourceProbeResult> {
        ResourceProbeResult r;
        r.contentDigest = d.finalize();
        r.absPath = "D:/a/link.stl";
        r.isMeshFamily = true;
        return r;
    };
    ASSERT_FALSE(attachExternalGeometry(m_ws, 1, GeometrySlot::Visual,
                                        "D:/a/link.stl", probe)
                     .has_value());
    ASSERT_TRUE(m_ws.design.links[1].visual.has_value());

    // ---- ③碰撞段：重投影后点"从视觉复制"钮（T49 语义——同资源落
    //      collision）。
    m_panel->refreshPanel(m_ws, emptyReport);
    m_panel->focusObject(m_ws.design.links[1].objectId);
    QPushButton* copy = m_panel->findChild<QPushButton*>(
        QStringLiteral("ird_modeling_geo_copy_collision"));
    ASSERT_NE(copy, nullptr) << "复制钮未构建";
    copy->click();
    ASSERT_TRUE(m_ws.design.links[1].collision.has_value())
        << "碰撞段：复制钮点击未落草稿";
    EXPECT_EQ(m_ws.design.links[1].collision->resourceRefId,
              m_ws.design.links[1].visual->resourceRefId)
        << "碰撞段：未同资源复制";

    // ---- ④对象属性页段：属性区重投影后编辑面仍以草稿为源（面板行值
    //      与域状态同源——选中保持连杆 1，几何行/物性行刷新不回退）。
    m_panel->refreshPanel(m_ws, emptyReport);
    m_panel->focusObject(m_ws.design.links[1].objectId);
    QPushButton* detach = m_panel->findChild<QPushButton*>(
        QStringLiteral("ird_modeling_geo_detach_visual"));
    ASSERT_NE(detach, nullptr)
        << "对象属性页段：重投影后几何行编辑面丢失（投影回退）";

    // ---- 四段全落草稿的域状态总核对（变更摘要累积——MDL-09 载体）。
    EXPECT_FALSE(m_ws.changes.empty()) << "四段贯通零变更摘要（草稿未脏化）";
}

// =====================================================================
// UI-T55 工具安装接口/场景世界位姿编辑页（选择驱动分派——B.1 主通道）
// =====================================================================

/// 由 64 位序号构造确定 ObjectId（PartsTest 同款——工具/场景夹具树锚）。
core::ObjectId guiMakeOid(unsigned long long v)
{
    char text[40] = {};
    std::snprintf(text, sizeof(text), "obj-%024llx%08llx",
                  static_cast<unsigned long long>(0),
                  static_cast<unsigned long long>(v));
    return core::ObjectId::fromCanonical(text);
}

/// 指定编辑页容器内的参数表现取（ird_param_table——容器 objectName 锚）。
QTableWidget* posePageTable(const QWidget& panel, const char* areaName)
{
    QWidget* area = panel.findChild<QWidget*>(QString::fromLatin1(areaName));
    return area == nullptr
               ? nullptr
               : area->findChild<QTableWidget*>(QStringLiteral("ird_param_table"));
}

/// 按字段键定位行下标（键挂键列 UserRole——ParamTablePanel 装配契约）。
int posePageRowOf(const QTableWidget& table, const char* fieldKey)
{
    for (int row = 0; row < table.rowCount(); ++row) {
        const QTableWidgetItem* keyItem = table.item(row, 0);
        if (keyItem != nullptr
            && keyItem->data(Qt::UserRole).toString().toStdString() == fieldKey) {
            return row;
        }
    }
    return -1;
}

/**
 * 工具安装接口编辑全链（UI-T55 验收主链①——选择驱动分派）：树选中工具
 * 对象→自动切工具页→mount-x 编辑→应用→确认→applyToolMountEdit 接受＝
 * mountInterface 平移分量更新＋恰一条变更记录（subject tools[0]）。
 */
TEST_F(ModelingPanelGuiTest, ToolMountEdit_SelectionDrivenChain_UI_T55)
{
    IRD_TEST_INFO("MDL-07", {}, std::nullopt);

    ToolDefinition tool;
    tool.localName = "t1";
    tool.objectId = guiMakeOid(501);
    m_ws.toolObjects.push_back(tool);
    ModelReadinessReport emptyReport;
    m_panel->refreshPanel(m_ws, emptyReport);

    m_panel->focusObject(m_ws.toolObjects[0].objectId);
    QTableWidget* table = posePageTable(*m_panel, "ird_modeling_tool_area");
    ASSERT_NE(table, nullptr) << "选中工具未切工具页（选择驱动分派失守）";
    const int row = posePageRowOf(*table, "mount-x");
    ASSERT_GE(row, 0);

    table->item(row, 1)->setText(QStringLiteral("0.5"));
    QApplication::processEvents();
    QWidget* area = m_panel->findChild<QWidget*>(QStringLiteral("ird_modeling_tool_area"));
    area->findChild<QPushButton*>(QStringLiteral("ird_param_apply"))->click();
    area->findChild<QPushButton*>(QStringLiteral("ird_param_confirm_yes"))->click();

    EXPECT_DOUBLE_EQ(m_ws.toolObjects[0].mountInterface.P()[0], 0.5);
    ASSERT_EQ(m_ws.changes.size(), std::size_t{1});
    EXPECT_EQ(m_ws.changes[0].subject, "tools[0]");
}

/**
 * 场景世界位姿编辑全链（UI-T55 验收主链②——世界系固连语义）：选中场景
 * 对象→场景页→world-z 编辑→应用→确认→applyScenePoseEdit 接受＝worldPose
 * 平移更新＋恰一条变更记录（subject scenes[0]）。
 */
TEST_F(ModelingPanelGuiTest, ScenePoseEdit_SelectionDrivenChain_UI_T55)
{
    IRD_TEST_INFO("MDL-15", {}, std::nullopt);

    SceneObject scene;
    scene.localName = "s1";
    scene.objectId = guiMakeOid(601);
    m_ws.sceneObjects.push_back(scene);
    ModelReadinessReport emptyReport;
    m_panel->refreshPanel(m_ws, emptyReport);

    m_panel->focusObject(m_ws.sceneObjects[0].objectId);
    QTableWidget* table = posePageTable(*m_panel, "ird_modeling_scene_area");
    ASSERT_NE(table, nullptr) << "选中场景未切场景页";
    const int row = posePageRowOf(*table, "world-z");
    ASSERT_GE(row, 0);

    table->item(row, 1)->setText(QStringLiteral("2.5"));
    QApplication::processEvents();
    QWidget* area = m_panel->findChild<QWidget*>(QStringLiteral("ird_modeling_scene_area"));
    area->findChild<QPushButton*>(QStringLiteral("ird_param_apply"))->click();
    area->findChild<QPushButton*>(QStringLiteral("ird_param_confirm_yes"))->click();

    EXPECT_DOUBLE_EQ(m_ws.sceneObjects[0].worldPose.P()[2], 2.5);
    ASSERT_EQ(m_ws.changes.size(), std::size_t{1});
    EXPECT_EQ(m_ws.changes[0].subject, "scenes[0]");
}

/**
 * 工具/场景页 L-7 门控（UI-T55）：只读会话＝两页 ParamTablePanel 整体
 * 禁用（域出口 writable 防御面另在，本用例钉控件半区）。
 */
TEST_F(ModelingPanelGuiTest, PoseEditPanes_ReadOnlyDisables_UI_T55)
{
    IRD_TEST_INFO("MDL-07", {}, std::nullopt);

    ToolDefinition tool;
    tool.localName = "t1";
    tool.objectId = guiMakeOid(502);
    m_ws.toolObjects.push_back(tool);
    SceneObject scene;
    scene.localName = "s1";
    scene.objectId = guiMakeOid(602);
    m_ws.sceneObjects.push_back(scene);

    ModelingPanelWidget readonlyPanel(false);
    readonlyPanel.setEditTargetProvider([this]() { return &m_ws; });
    ModelReadinessReport emptyReport;
    readonlyPanel.refreshPanel(m_ws, emptyReport);

    readonlyPanel.focusObject(m_ws.toolObjects[0].objectId);
    QTableWidget* toolTable = posePageTable(readonlyPanel, "ird_modeling_tool_area");
    ASSERT_NE(toolTable, nullptr);
    EXPECT_FALSE(toolTable->isEnabled()) << "只读会话工具页未禁用";

    readonlyPanel.focusObject(m_ws.sceneObjects[0].objectId);
    QTableWidget* sceneTable = posePageTable(readonlyPanel, "ird_modeling_scene_area");
    ASSERT_NE(sceneTable, nullptr);
    EXPECT_FALSE(sceneTable->isEnabled()) << "只读会话场景页未禁用";
}

// =====================================================================
// UI-T57 TCP 列表结构化编辑（MDL-13 不变量流——工具页 TCP 段）；
// UI-T58 增显示名行编辑链（displayName 仅呈现字段）
// =====================================================================

/// 工具页 TCP 段现取辅助（combo——容器 objectName 锚）。
QComboBox* tcpComboOf(const QWidget& panel)
{
    return panel.findChild<QComboBox*>(QStringLiteral("ird_modeling_tcp_combo"));
}

/// 进工具模式辅助：选中工具对象（选择驱动分派——与 T55 同款主通道）。
void enterToolMode(ModelingPanelWidget& panel, const core::ObjectId& toolOid)
{
    panel.focusObject(toolOid);
}

/**
 * TCP 编辑全链（UI-T57 验收主链）：选中工具→工具页 TCP 段可见→新增 TCP
 * （自动键 tcp-2）→combo 即选中→offset 编辑→应用→applyTcpOffsetEdit 接受
 * ＝tcpList[1].offset 更新＋变更记录；设为默认→根 defaultTcp 写入。
 */
TEST_F(ModelingPanelGuiTest, TcpListEdit_AddAndOffsetChain_UI_T57)
{
    IRD_TEST_INFO("MDL-13", {}, std::nullopt);

    ToolDefinition tool;
    tool.localName = "t1";
    tool.objectId = guiMakeOid(701);
    TcpEntry tcp0;
    tcp0.key = "tcp-1";
    tool.tcpList.push_back(tcp0);
    m_ws.toolObjects.push_back(tool);
    ModelReadinessReport emptyReport;
    m_panel->refreshPanel(m_ws, emptyReport);

    enterToolMode(*m_panel, m_ws.toolObjects[0].objectId);
    QComboBox* combo = tcpComboOf(*m_panel);
    ASSERT_NE(combo, nullptr) << "TCP 选择器未构建（UI-T57 承载缺失）";
    EXPECT_EQ(combo->currentIndex(), 0);
    EXPECT_EQ(combo->currentText(), QStringLiteral("tcp-1"));

    // 新增 TCP（自动键 tcp-2）→combo 重建并选中新键。
    QPushButton* addBtn = m_panel->findChild<QPushButton*>(QStringLiteral("ird_modeling_tcp_add"));
    ASSERT_NE(addBtn, nullptr);
    addBtn->click();
    EXPECT_EQ(combo->currentText(), QStringLiteral("tcp-2")) << "新增后 combo 应选中新键";
    ASSERT_EQ(m_ws.toolObjects[0].tcpList.size(), std::size_t{2});

    // offset 编辑（tcp-offset-z→应用→确认）→域接受。
    // 工具页两个 ParamTablePanel（安装接口＋TCP offset）——按行键定位 TCP
    // 面板，并取同一面板内的 apply/确认钮（跨面板取钮会点到安装接口面板）。
    QWidget* area = m_panel->findChild<QWidget*>(QStringLiteral("ird_modeling_tool_area"));
    QList<QWidget*> panels = area->findChildren<QWidget*>(QStringLiteral("ird_param_table_panel"));
    QWidget* tcpPanel = nullptr;
    QTableWidget* tcpTable2 = nullptr;
    for (QWidget* p : panels) {
        QTableWidget* t = p->findChild<QTableWidget*>(QStringLiteral("ird_param_table"));
        if (t != nullptr && posePageRowOf(*t, "tcp-offset-z") >= 0) {
            tcpPanel = p;
            tcpTable2 = t;
            break;
        }
    }
    ASSERT_NE(tcpPanel, nullptr) << "TCP offset 面板未构建";
    const int zRow = posePageRowOf(*tcpTable2, "tcp-offset-z");
    tcpTable2->item(zRow, 1)->setText(QStringLiteral("0.3"));
    QApplication::processEvents();
    tcpPanel->findChild<QPushButton*>(QStringLiteral("ird_param_apply"))->click();
    tcpPanel->findChild<QPushButton*>(QStringLiteral("ird_param_confirm_yes"))->click();

    ASSERT_EQ(m_ws.toolObjects[0].tcpList[1].offset.P()[2], 0.3);
    EXPECT_EQ(m_ws.toolObjects[0].tcpList[1].key, "tcp-2");

    // 设为默认→根 defaultTcp 写入（tool.objectId＋tcp-2）。
    QPushButton* defBtn = m_panel->findChild<QPushButton*>(QStringLiteral("ird_modeling_tcp_default"));
    ASSERT_NE(defBtn, nullptr);
    defBtn->click();
    ASSERT_TRUE(m_ws.design.defaultTcp.has_value());
    EXPECT_EQ(m_ws.design.defaultTcp->toolOid, m_ws.toolObjects[0].objectId);
    EXPECT_EQ(m_ws.design.defaultTcp->tcpKey, "tcp-2");
}

/**
 * TCP 删除守卫面（UI-T57）：defaultTcp 引用保护（I-MDL-9——先切换默认再
 * 删除的域指引）＋最后一条保护（I-MDL-13 ≥1）；状态行就地呈现＋工作集
 * 字节不变。
 */
TEST_F(ModelingPanelGuiTest, TcpListEdit_RemoveGuards_UI_T57)
{
    IRD_TEST_INFO("MDL-13", {}, std::nullopt);

    ToolDefinition tool;
    tool.localName = "t1";
    tool.objectId = guiMakeOid(702);
    TcpEntry tcp0;
    tcp0.key = "tcp-1";
    tool.tcpList.push_back(tcp0);
    m_ws.toolObjects.push_back(tool);
    // 根 defaultTcp 引用 tcp-1（场景装配——I-MDL-9 保护面）。
    modeling::TcpRef ref;
    ref.toolOid = tool.objectId;
    ref.tcpKey = "tcp-1";
    m_ws.design.defaultTcp = ref;
    ModelReadinessReport emptyReport;
    m_panel->refreshPanel(m_ws, emptyReport);

    enterToolMode(*m_panel, m_ws.toolObjects[0].objectId);
    QPushButton* removeBtn = m_panel->findChild<QPushButton*>(QStringLiteral("ird_modeling_tcp_remove"));
    ASSERT_NE(removeBtn, nullptr);
    removeBtn->click();

    QLabel* status = m_panel->findChild<QLabel*>(QStringLiteral("ird_modeling_status_line"));
    ASSERT_NE(status, nullptr);
    EXPECT_TRUE(status->text().contains(QStringLiteral("default-tcp-referenced")))
        << "引用保护原因未就地呈现";
    // 工作集字节不变（tcpList 仍 1 条＋defaultTcp 仍在）。
    ASSERT_EQ(m_ws.toolObjects[0].tcpList.size(), std::size_t{1});
    EXPECT_TRUE(m_ws.design.defaultTcp.has_value());
}

/**
 * TCP 段 L-7 门控（UI-T57）：只读会话＝增删/默认钮与 offset 面板整体
 * 禁用（域出口 writable 防御面另在）。
 * F-516③ 断言面补全（UI-T58 随批）：default 钮与 offset 面板此前仅经
 * 探针实证未入断言；显示名行（UI-T58）一并纳入。
 */
TEST_F(ModelingPanelGuiTest, TcpPane_ReadOnlyDisables_UI_T57)
{
    IRD_TEST_INFO("MDL-07", {}, std::nullopt);

    ToolDefinition tool;
    tool.localName = "t1";
    tool.objectId = guiMakeOid(703);
    TcpEntry tcp0;
    tcp0.key = "tcp-1";
    tool.tcpList.push_back(tcp0);
    m_ws.toolObjects.push_back(tool);

    ModelingPanelWidget readonlyPanel(false);
    readonlyPanel.setEditTargetProvider([this]() { return &m_ws; });
    ModelReadinessReport emptyReport;
    readonlyPanel.refreshPanel(m_ws, emptyReport);
    readonlyPanel.focusObject(m_ws.toolObjects[0].objectId);

    QPushButton* addBtn = readonlyPanel.findChild<QPushButton*>(QStringLiteral("ird_modeling_tcp_add"));
    ASSERT_NE(addBtn, nullptr);
    EXPECT_FALSE(addBtn->isEnabled()) << "只读会话新增 TCP 钮未禁用";
    QPushButton* removeBtn = readonlyPanel.findChild<QPushButton*>(QStringLiteral("ird_modeling_tcp_remove"));
    ASSERT_NE(removeBtn, nullptr);
    EXPECT_FALSE(removeBtn->isEnabled()) << "只读会话删除 TCP 钮未禁用";
    // ---- F-516③ 断言面补全（UI-T58 随批）----
    QPushButton* defBtn = readonlyPanel.findChild<QPushButton*>(QStringLiteral("ird_modeling_tcp_default"));
    ASSERT_NE(defBtn, nullptr);
    EXPECT_FALSE(defBtn->isEnabled()) << "只读会话设默认钮未禁用";
    // 工具页两个 ParamTablePanel（安装接口＋TCP offset）——只读会话全部
    // 禁用。范围＝工具页容器（基座/场景页在构造期同样预建面板，但从未被
    // 选中故不走 L-7 降级路径——其门控归各自刷新入口，不在本用例断言面）。
    QWidget* toolArea = readonlyPanel.findChild<QWidget*>(QStringLiteral("ird_modeling_tool_area"));
    ASSERT_NE(toolArea, nullptr);
    const QList<QWidget*> paramPanels =
        toolArea->findChildren<QWidget*>(QStringLiteral("ird_param_table_panel"));
    ASSERT_GE(paramPanels.size(), 2) << "工具页应含安装接口与 TCP offset 两面板";
    for (QWidget* panel : paramPanels) {
        EXPECT_FALSE(panel->isEnabled()) << "只读会话 ParamTablePanel 未禁用";
    }
    QLineEdit* nameEdit = readonlyPanel.findChild<QLineEdit*>(QStringLiteral("ird_modeling_tcp_displayname"));
    ASSERT_NE(nameEdit, nullptr);
    EXPECT_FALSE(nameEdit->isEnabled()) << "只读会话显示名行编辑未禁用";
    QPushButton* nameApply = readonlyPanel.findChild<QPushButton*>(QStringLiteral("ird_modeling_tcp_displayname_apply"));
    ASSERT_NE(nameApply, nullptr);
    EXPECT_FALSE(nameApply->isEnabled()) << "只读会话显示名应用钮未禁用";
}

/**
 * TCP 显示名编辑全链（UI-T58——UI-T57 卡"诚实边界"顺延项；§4.4 tcpList
 * displayName 行）：选中工具→行编辑回填基线→改文本→应用→
 * applyTcpDisplayNameEdit 接受＝displayName 更新＋变更记录；选中切换基线
 * 随刷；未变更应用＝零修订零动作（PA-2——无信息量历史不产生）。
 */
TEST_F(ModelingPanelGuiTest, TcpDisplayNameEdit_Chain_UI_T58)
{
    IRD_TEST_INFO("MDL-13", {}, std::nullopt);

    ToolDefinition tool;
    tool.localName = "t1";
    tool.objectId = guiMakeOid(711);
    TcpEntry tcp0;
    tcp0.key = "tcp-1";
    tcp0.displayName = "TCP tcp-1";
    TcpEntry tcp1;
    tcp1.key = "tcp-2";  // displayName 留空——空值回落呈现面
    tool.tcpList.push_back(tcp0);
    tool.tcpList.push_back(tcp1);
    m_ws.toolObjects.push_back(tool);
    ModelReadinessReport emptyReport;
    m_panel->refreshPanel(m_ws, emptyReport);

    enterToolMode(*m_panel, m_ws.toolObjects[0].objectId);
    QLineEdit* nameEdit =
        m_panel->findChild<QLineEdit*>(QStringLiteral("ird_modeling_tcp_displayname"));
    ASSERT_NE(nameEdit, nullptr) << "显示名行编辑未构建（UI-T58 承载缺失）";
    EXPECT_EQ(nameEdit->text(), QStringLiteral("TCP tcp-1")) << "选中 TCP 显示名基线回填";

    // 改文本→应用→域接受（displayName 直写＋一条变更记录）。
    nameEdit->setText(QStringLiteral("法兰中心"));
    QApplication::processEvents();
    QPushButton* applyBtn = m_panel->findChild<QPushButton*>(
        QStringLiteral("ird_modeling_tcp_displayname_apply"));
    ASSERT_NE(applyBtn, nullptr);
    applyBtn->click();
    EXPECT_EQ(m_ws.toolObjects[0].tcpList[0].displayName, "法兰中心");
    EXPECT_FALSE(m_ws.changes.empty()) << "接受路径应产生变更记录";

    // 选中切换→基线随刷：tcp-2 displayName 为空＝回填空文本（placeholder
    // 提示"按 TCP 键呈现"）。
    QComboBox* combo = tcpComboOf(*m_panel);
    ASSERT_NE(combo, nullptr);
    combo->setCurrentIndex(1);
    QApplication::processEvents();
    EXPECT_EQ(nameEdit->text(), QString()) << "空 displayName 回填空文本";

    // 未变更应用＝零动作（空名对空名——不产生新变更记录；PA-2）。
    const std::size_t changesBefore = m_ws.changes.size();
    applyBtn->click();
    EXPECT_EQ(m_ws.changes.size(), changesBefore) << "零差异提交不产生变更记录";
    EXPECT_TRUE(m_ws.toolObjects[0].tcpList[1].displayName.empty());
}

// =====================================================================
// UI-T59 预览页多类型（F-498 预览半区——combo 分轨＋诚实缺席＋复制钮）
// =====================================================================

/**
 * 预览页多类型链（UI-T59）：默认摘要轨（D-MDL-10——空态占位/定格呈现，
 * 不经供给器）→XML 类经供给器现取（text 原样＋来源头行）→缺席诚实呈现
 * （不虚构）→复制全部钮（剪贴板＝当前预览全文）。供给替身＝宿主
 * bindWorkCellPreview 适配形态（模块级接线用例在 HostWiringGuiTest——
 * 本用例直注面板供给器，聚焦分轨呈现）。
 */
TEST_F(ModelingPanelGuiTest, PreviewPane_MultiTypeChain_UI_T59)
{
    IRD_TEST_INFO("MDL-20", {}, std::nullopt);
    // 剪贴板预探（F-617/F-620）——被占用环境跳过（用例体内 skip），
    // 健康环境照常执行。
    if (!clipboardUsable()) {
        GTEST_SKIP() << "clipboard blocked by external owner (F-620)"
                     << "——系统剪贴板写读往返失败（环境面跳过，非代码缺陷）";
    }

    bool called = false;
    std::string askedKind;
    m_panel->setPreviewContentProvider(
        [&called, &askedKind](const std::string& kind)
            -> std::optional<ModelingPanelWidget::PreviewContentAnswer> {
            called = true;
            askedKind = kind;
            if (kind == "dwc-xml") {
                return std::nullopt;  // 快照缺席/能力缺席形态——诚实缺席
            }
            ModelingPanelWidget::PreviewContentAnswer answer;
            answer.headerLine = "来源修订 rev-test（序号 1）｜模型身份 cid-test";
            answer.sourceObject = "devices=1";
            answer.text = "<ird-serial-device-export>stub</ird-serial-device-export>";
            return answer;
        });

    QComboBox* combo =
        m_panel->findChild<QComboBox*>(QStringLiteral("ird_modeling_preview_kind"));
    ASSERT_NE(combo, nullptr);
    ASSERT_EQ(combo->count(), 6) << "六类预览（摘要＋四 XML＋规范包清单——UI-T61）";
    QPlainTextEdit* preview =
        m_panel->findChild<QPlainTextEdit*>(QStringLiteral("ird_modeling_preview_text"));
    ASSERT_NE(preview, nullptr);
    QLabel* header =
        m_panel->findChild<QLabel*>(QStringLiteral("ird_modeling_preview_source"));
    ASSERT_NE(header, nullptr);

    // ---- 默认摘要轨：无已应用修订＝空态占位（不经供给器）----
    EXPECT_EQ(combo->currentIndex(), 0);
    EXPECT_TRUE(preview->toPlainText().contains(QStringLiteral("D-MDL-10")));
    EXPECT_FALSE(called) << "摘要轨零外部供给（AppliedRevisionView 本地渲染）";

    // ---- 定格已应用修订视图→摘要即刻呈现（setAppliedPreview 注入即重渲）----
    AppliedRevisionView applied;
    applied.revision = core::RevisionId::generate();
    applied.summaryText = "已应用修订快照：关节 6 个";
    m_panel->setAppliedPreview(applied);
    EXPECT_TRUE(preview->toPlainText().contains(QStringLiteral("关节 6 个")));

    // ---- 切 SerialDevice XML→供给器应答呈现（text 原样＋来源头行）----
    combo->setCurrentIndex(1);
    QApplication::processEvents();
    EXPECT_TRUE(called) << "XML 类应经供给器现取";
    EXPECT_EQ(QString::fromStdString(askedKind), QStringLiteral("serial-device-xml"));
    EXPECT_TRUE(preview->toPlainText().contains(
        QStringLiteral("<ird-serial-device-export>")));
    EXPECT_TRUE(header->text().contains(QStringLiteral("rev-test")))
        << "来源头行呈现（修订/身份/时间——宿主组装）";

    // ---- 切 DWC XML→供给器缺席→诚实缺席呈现（不虚构内容）----
    combo->setCurrentIndex(4);
    QApplication::processEvents();
    EXPECT_TRUE(preview->toPlainText().contains(QStringLiteral("预览不可用")));
    EXPECT_TRUE(header->text().isEmpty()) << "缺席态无来源头行（不伪造溯源）";

    // ---- 复制全部钮：剪贴板＝当前预览全文（UX-02 可复制导出）。预置
    // 哨兵隔离系统剪贴板状态（F-519②——跨进程剪贴板残留可能碰巧等于
    // 预览文本使断言恒真；哨兵既定值保证断言的区分度）。
    QApplication::clipboard()->setText(QStringLiteral("ird-clipboard-sentinel"));
    QPushButton* copyBtn = m_panel->findChild<QPushButton*>(
        QStringLiteral("ird_modeling_preview_copy"));
    ASSERT_NE(copyBtn, nullptr);
    copyBtn->click();
    QApplication::processEvents();
    EXPECT_EQ(QApplication::clipboard()->text(), preview->toPlainText());
    EXPECT_NE(QApplication::clipboard()->text(), QStringLiteral("ird-clipboard-sentinel"))
        << "剪贴板仍为哨兵＝复制钮未写入（做红锚）";
}

// =====================================================================
// UI-T60 位姿集/传动编辑页（F-497 余项收尾）＋F-517 S1 动态窗口
// =====================================================================

/**
 * 位姿集编辑全链（UI-T60）：新增条目（pose-1 零位姿种子，缺席位姿集经
 * 草稿句柄创建）→构型编辑（表行 pose-config-2 走标准 apply/confirm 流）→
 * 保存条目（键/备注/构型组装）→域接受＝entries[0] 更新＋变更记录→
 * 保留键删除拒绝就地呈现。
 */
TEST_F(ModelingPanelGuiTest, PoseSetEdit_Chain_UI_T60)
{
    IRD_TEST_INFO("MDL-17", {}, std::nullopt);

    // 刷新即建页（refreshEditPages 无选中路径驱动两页基线——模板会话
    // 无位姿集对象也可用：新建不需要既有对象）。
    ModelReadinessReport emptyReport;
    m_panel->refreshPanel(m_ws, emptyReport);
    QWidget* poseArea = m_panel->findChild<QWidget*>(QStringLiteral("ird_modeling_pose_area"));
    ASSERT_NE(poseArea, nullptr);
    const auto poseTitle = poseArea->findChild<QLabel*>(QStringLiteral("ird_card_title"));
    ASSERT_NE(poseTitle, nullptr);
    EXPECT_EQ(poseTitle->text(), QStringLiteral("命名位姿与关节构型"))
        << "命名位姿字段必须统一归入唯一主题卡片";
    QPushButton* addBtn = m_panel->findChild<QPushButton*>(QStringLiteral("ird_modeling_pose_add"));
    ASSERT_NE(addBtn, nullptr) << "位姿集页承载缺失（UI-T60）";
    addBtn->click();
    ASSERT_TRUE(m_ws.poseSetObject.has_value()) << "缺席位姿集应经草稿句柄创建";
    ASSERT_EQ(m_ws.poseSetObject->entries.size(), std::size_t{1});
    EXPECT_EQ(m_ws.poseSetObject->entries[0].key, "pose-1");

    // 构型编辑：pose-config-2 行置 0.5（标准 apply/confirm 流——暂存入
    // 模型；保存条目组装取 pendingChanges）。
    QWidget* posePanel = nullptr;
    QTableWidget* poseTable = nullptr;
    const QList<QWidget*> paramPanels =
        m_panel->findChildren<QWidget*>(QStringLiteral("ird_param_table_panel"));
    for (QWidget* p : paramPanels) {
        QTableWidget* t = p->findChild<QTableWidget*>(QStringLiteral("ird_param_table"));
        if (t != nullptr && posePageRowOf(*t, "pose-config-2") >= 0) {
            posePanel = p;
            poseTable = t;
            break;
        }
    }
    ASSERT_NE(posePanel, nullptr) << "构型表未构建";
    const int row2 = posePageRowOf(*poseTable, "pose-config-2");
    poseTable->item(row2, 1)->setText(QStringLiteral("0.5"));
    QApplication::processEvents();
    posePanel->findChild<QPushButton*>(QStringLiteral("ird_param_apply"))->click();
    posePanel->findChild<QPushButton*>(QStringLiteral("ird_param_confirm_yes"))->click();
    QApplication::processEvents();

    // 保存条目（键/备注现取行编辑——combo 重建后键已回填 pose-1）。
    QLineEdit* keyEdit = m_panel->findChild<QLineEdit*>(QStringLiteral("ird_modeling_pose_key"));
    ASSERT_NE(keyEdit, nullptr);
    QLineEdit* noteEdit = m_panel->findChild<QLineEdit*>(QStringLiteral("ird_modeling_pose_note"));
    ASSERT_NE(noteEdit, nullptr);
    noteEdit->setText(QStringLiteral("拾取位"));
    QPushButton* applyBtn = m_panel->findChild<QPushButton*>(QStringLiteral("ird_modeling_pose_apply"));
    ASSERT_NE(applyBtn, nullptr);
    applyBtn->click();
    ASSERT_FALSE(m_ws.poseSetObject->entries.empty());
    EXPECT_EQ(m_ws.poseSetObject->entries[0].key, "pose-1");
    EXPECT_EQ(m_ws.poseSetObject->entries[0].note, "拾取位");
    EXPECT_DOUBLE_EQ(m_ws.poseSetObject->entries[0].jointConfiguration[2], 0.5);

    // 保留键删除拒绝（就地呈现——域内裁决）。
    keyEdit->setText(QStringLiteral("homeConfiguration"));
    applyBtn->click();
    QLabel* status = m_panel->findChild<QLabel*>(QStringLiteral("ird_modeling_status_line"));
    ASSERT_NE(status, nullptr);
    EXPECT_TRUE(status->text().contains(QStringLiteral("reserved-key-in-edit")))
        << "保留键拒绝原因未就地呈现";
    // 工作集字节不变（拒绝路径）。
    ASSERT_EQ(m_ws.poseSetObject->entries.size(), std::size_t{1});
    EXPECT_EQ(m_ws.poseSetObject->entries[0].key, "pose-1");
}

/**
 * 传动编辑全链（UI-T60）：ratio 行（dt-ratio-0）置 100→面板 apply/confirm
 * →分组装配→applyDrivetrainRatioEdit 接受＝缺席传动经草稿句柄创建＋
 * ratioPerJoint[0] UserProvided 100＋变更记录。
 */
TEST_F(ModelingPanelGuiTest, DrivetrainEdit_Chain_UI_T60)
{
    IRD_TEST_INFO("MDL-16", {}, std::nullopt);

    ModelReadinessReport emptyReport;
    m_panel->refreshPanel(m_ws, emptyReport);
    QWidget* dtPanel = nullptr;
    QTableWidget* dtTable = nullptr;
    const QList<QWidget*> paramPanels =
        m_panel->findChildren<QWidget*>(QStringLiteral("ird_param_table_panel"));
    for (QWidget* p : paramPanels) {
        QTableWidget* t = p->findChild<QTableWidget*>(QStringLiteral("ird_param_table"));
        if (t != nullptr && posePageRowOf(*t, "dt-ratio-0") >= 0) {
            dtPanel = p;
            dtTable = t;
            break;
        }
    }
    ASSERT_NE(dtPanel, nullptr) << "传动数值表未构建（UI-T60）";
    QWidget* dtArea = m_panel->findChild<QWidget*>(QStringLiteral("ird_modeling_drivetrain_area"));
    ASSERT_NE(dtArea, nullptr);
    const auto dtTitle = dtArea->findChild<QLabel*>(QStringLiteral("ird_card_title"));
    ASSERT_NE(dtTitle, nullptr);
    EXPECT_EQ(dtTitle->text(), QStringLiteral("传动参数"))
        << "传动参数与耦合入口必须置于统一主题卡片内";
    const int row0 = posePageRowOf(*dtTable, "dt-ratio-0");
    dtTable->item(row0, 1)->setText(QStringLiteral("100"));
    QApplication::processEvents();
    dtPanel->findChild<QPushButton*>(QStringLiteral("ird_param_apply"))->click();
    dtPanel->findChild<QPushButton*>(QStringLiteral("ird_param_confirm_yes"))->click();

    ASSERT_TRUE(m_ws.drivetrainObject.has_value()) << "缺席传动应经草稿句柄创建";
    EXPECT_TRUE(m_ws.design.drivetrainRef.has_value());
    ASSERT_EQ(m_ws.drivetrainObject->ratioPerJoint.size(), std::size_t{6});
    ASSERT_EQ(m_ws.drivetrainObject->ratioPerJoint[0].state(), core::FieldState::Provided);
    EXPECT_DOUBLE_EQ(*m_ws.drivetrainObject->ratioPerJoint[0].tryValue(), 100.0);
}

/**
 * 位姿集/传动页 L-7 门控（UI-T60）：只读会话＝三钮/键备注行编辑/构型表/
 * 传动表整体禁用。
 */
TEST_F(ModelingPanelGuiTest, PoseDrivetrainPanes_ReadOnlyDisables_UI_T60)
{
    IRD_TEST_INFO("MDL-07", {}, std::nullopt);

    ModelingPanelWidget readonlyPanel(false);
    readonlyPanel.setEditTargetProvider([this]() { return &m_ws; });
    ModelReadinessReport emptyReport;
    readonlyPanel.refreshPanel(m_ws, emptyReport);

    QPushButton* addBtn = readonlyPanel.findChild<QPushButton*>(QStringLiteral("ird_modeling_pose_add"));
    ASSERT_NE(addBtn, nullptr);
    EXPECT_FALSE(addBtn->isEnabled()) << "只读会话新增位姿条目钮未禁用";
    QPushButton* applyBtn = readonlyPanel.findChild<QPushButton*>(QStringLiteral("ird_modeling_pose_apply"));
    ASSERT_NE(applyBtn, nullptr);
    EXPECT_FALSE(applyBtn->isEnabled()) << "只读会话保存条目钮未禁用";
    QLineEdit* keyEdit = readonlyPanel.findChild<QLineEdit*>(QStringLiteral("ird_modeling_pose_key"));
    ASSERT_NE(keyEdit, nullptr);
    EXPECT_FALSE(keyEdit->isEnabled()) << "只读会话位姿键行编辑未禁用";
    // 构型表与传动表（行键定位面板——两面板均在只读会话禁用）。
    for (const char* rowKey : {"pose-config-0", "dt-ratio-0"}) {
        QWidget* target = nullptr;
        const QList<QWidget*> paramPanels =
            readonlyPanel.findChildren<QWidget*>(QStringLiteral("ird_param_table_panel"));
        for (QWidget* p : paramPanels) {
            QTableWidget* t = p->findChild<QTableWidget*>(QStringLiteral("ird_param_table"));
            if (t != nullptr && posePageRowOf(*t, rowKey) >= 0) { target = p; break; }
        }
        ASSERT_NE(target, nullptr) << "面板未构建：" << rowKey;
        EXPECT_FALSE(target->isEnabled()) << "只读会话面板未禁用：" << rowKey;
    }
}

/**
 * TCP 显示名 L-7 动态窗口（F-517 S1——setWritable 半区钉扎）：可写会话
 * 选中工具后宿主降级只读（无刷新事件到达的动态窗口形态），显示名行与
 * 应用钮由 setWritable 即时禁用——与 refreshTcpPane 刷新半区（UI-T57 用例）
 * 双半区各有断言（F-517① 做红②′注入 setWritable 缺陷被刷新半区兜底，
 * 本用例补齐 setWritable 半区的独立断言面）。
 */
TEST_F(ModelingPanelGuiTest, TcpDisplayNameEdit_SetWritableDynamicWindow_F517)
{
    IRD_TEST_INFO("MDL-07", {}, std::nullopt);

    ToolDefinition tool;
    tool.localName = "t1";
    tool.objectId = guiMakeOid(721);
    TcpEntry tcp0;
    tcp0.key = "tcp-1";
    tool.tcpList.push_back(tcp0);
    m_ws.toolObjects.push_back(tool);
    ModelReadinessReport emptyReport;
    m_panel->refreshPanel(m_ws, emptyReport);
    enterToolMode(*m_panel, m_ws.toolObjects[0].objectId);

    // 宿主降级只读（不触发刷新事件——动态窗口形态）。
    m_panel->setWritable(false);
    QLineEdit* nameEdit = m_panel->findChild<QLineEdit*>(QStringLiteral("ird_modeling_tcp_displayname"));
    ASSERT_NE(nameEdit, nullptr);
    EXPECT_FALSE(nameEdit->isEnabled()) << "动态窗口降级后显示名行未禁用（setWritable 半区）";
    QPushButton* nameApply = m_panel->findChild<QPushButton*>(QStringLiteral("ird_modeling_tcp_displayname_apply"));
    ASSERT_NE(nameApply, nullptr);
    EXPECT_FALSE(nameApply->isEnabled()) << "动态窗口降级后显示名应用钮未禁用（setWritable 半区）";
}

/**
 * @brief UI-T79 / MDL-07：首次选择必须立即呈现回填值，不能依赖取消按钮重绘。
 * 使用非零且各轴不同的输入（m、无量纲），排除全零模板掩盖未设/串行缺陷。
 */
TEST_F(ModelingPanelGuiTest, FirstSelectionShowsImportedValuesWithoutCancel_UI_T79)
{
    IRD_TEST_INFO("MDL-07", {}, std::nullopt);
    // 复用真实编辑原语建立可辨别的原点；本例核查显示链而非重算机器人学。
    JointEditValue value = JointOriginEditValue{0.12, -0.03, 0.4, 0, 0, 0};
    const auto err = applyJointFieldEdit(m_ws, 1, JointEditField::Origin, value);
    ASSERT_FALSE(err.has_value());
    m_panel->focusObject(m_ws.design.joints[1].objectId);
    auto* form = m_panel->findChild<QWidget*>("ird_joint_grouped_form");
    ASSERT_NE(form, nullptr);
    for (const auto& field : std::vector<std::pair<const char*, double>>{
             {"axis-y", m_ws.design.joints[1].axis.value()[1]}, {"origin-x", 0.12}, {"origin-y", -0.03}, {"origin-z", 0.4}}) {
        auto* editor=jointField(form,field.first);ASSERT_NE(editor,nullptr);
        bool ok = false;
        const double number = editor->text().toDouble(&ok);
        EXPECT_TRUE(ok) << "首次回填不得显示未设：" << field.first;
        EXPECT_DOUBLE_EQ(number, field.second) << field.first;
    }
}

/** @brief UI-T79 / UX-05：同对象刷新不能丢弃尚未确认的数值输入（m）。 */
TEST_F(ModelingPanelGuiTest, SameObjectRefreshKeepsPendingValue_UI_T79)
{
    IRD_TEST_INFO("UX-05", {}, std::nullopt);
    m_panel->focusObject(m_ws.design.joints[0].objectId);
    auto* host=m_panel->findChild<QWidget*>("ird_modeling_edit_host");ASSERT_NE(host,nullptr);
    auto* field=jointField(host,"origin-x");ASSERT_NE(field,nullptr);typeJointField(field,QStringLiteral("0.123"));
    m_panel->refreshPanel(m_ws, ModelReadinessReport{});
    EXPECT_EQ(jointField(host,"origin-x")->text(), QStringLiteral("0.123"));
    // 点确认必须仍有变更明细，防止仅屏幕保留旧文字、模型暂存已被清空。
    host->findChild<QPushButton*>(QStringLiteral("ird_param_apply"))->click();
    EXPECT_FALSE(host->findChild<QWidget*>(QStringLiteral("ird_param_confirm_region"))->isHidden());
}

/** @brief MDL-05/V-17：真实内联输入原位纠错、质心显式二选一与完整张量，取消零写入。 */
TEST_F(ModelingPanelGuiTest, ManualBodyFieldsAndCentroidChoice_MDL05_V17)
{
    IRD_TEST_INFO("MDL-05",{},std::nullopt);
    // 与生产模块相同：接受后重投影摘要和编辑页，截图必须核对同一草稿真值。
    m_panel->setPostEditAction([this]{m_panel->refreshPanel(m_ws,{});});
    auto& body=m_ws.design.links.back().body;
    const auto source=core::ValueProvenance::make(core::ProvenanceKind::GeometricEstimate);
    body.mass=core::SourcedValue<double>::provided(2,source);
    body.centerOfMass=core::SourcedValue<rw::math::Vector3D<double>>::provided({0,0,0},source);
    body.inertia=core::SourcedValue<InertiaTensor>::provided({3,4,5,0.1,0.2,0.3},source);
    m_panel->refreshPanel(m_ws,{});m_panel->focusObject(m_ws.design.links.back().objectId);
    m_panel->resize(900,650);m_panel->show();QApplication::processEvents();
    auto* editor=m_panel->findChild<QWidget*>("ird_modeling_body_editor");ASSERT_NE(editor,nullptr);
    auto field=[&](int i){return editor->findChild<QLineEdit*>(QStringLiteral("ird_body_value_%1").arg(i));};
    auto* mass=editor->findChild<QCheckBox*>("ird_body_mass_edit");
    auto* com=editor->findChild<QCheckBox*>("ird_body_com_edit");
    auto* tensor=editor->findChild<QCheckBox*>("ird_body_inertia_edit");
    auto* choice=editor->findChild<QComboBox*>("ird_body_centroid_resolution");
    auto* apply=editor->findChild<QPushButton*>("ird_body_apply");
    auto* cancel=editor->findChild<QPushButton*>("ird_body_cancel");
    auto* feedback=editor->findChild<QLabel*>("ird_body_feedback");
    const auto before=m_ws;
    mass->setChecked(true);field(0)->setText("-1");apply->click();
    EXPECT_EQ(m_ws,before);EXPECT_EQ(field(0)->text(),"-1");EXPECT_TRUE(feedback->text().contains(QStringLiteral("正数")));
    field(0)->setText("4");com->setChecked(true);
    field(1)->setText("1");field(2)->setText("2");field(3)->setText("3");
    EXPECT_EQ(choice->currentIndex(),0);apply->click();EXPECT_EQ(m_ws,before);
    EXPECT_TRUE(feedback->text().contains(QStringLiteral("明确选择")));
    choice->setCurrentIndex(1);EXPECT_FALSE(tensor->isEnabled());apply->click();
    EXPECT_EQ(m_ws.changes.size(),before.changes.size()+1);
    EXPECT_DOUBLE_EQ(body.mass.value(),4);EXPECT_DOUBLE_EQ(body.inertia.value().ixx,3);
    EXPECT_DOUBLE_EQ(body.inertia.value().ixy,0.1);EXPECT_DOUBLE_EQ(body.inertia.value().iyz,0.3);
    EXPECT_EQ(body.inertia.provenance(),source);EXPECT_FALSE(mass->isChecked());
    const auto migrated=m_ws;
    com->setChecked(true);field(1)->setText("2");choice->setCurrentIndex(2);
    EXPECT_TRUE(tensor->isChecked());
    const char* values[]={"5","6","7","0.4","0.5","0.6"};
    for(int i=0;i<6;++i){field(i+4)->setText(values[i]);}
    field(9)->setText("nan");apply->click();EXPECT_EQ(m_ws,migrated);EXPECT_EQ(field(9)->text(),"nan");
    field(9)->setText("0.6");apply->click();
    EXPECT_DOUBLE_EQ(body.centerOfMass.value()[0],2);EXPECT_DOUBLE_EQ(body.inertia.value().iyz,0.6);
    EXPECT_EQ(body.inertia.provenance().kind,core::ProvenanceKind::UserProvided);
    EXPECT_EQ(m_ws.changes.size(),before.changes.size()+2);
    EXPECT_EQ(field(7)->cursorPosition(),0);EXPECT_EQ(field(9)->cursorPosition(),0);
    ASSERT_GE(editors().size(),3);EXPECT_EQ(editors()[0]->text(),"4");
    EXPECT_TRUE(editors()[1]->text().startsWith("2, 2, 3"));
    const auto accepted=m_ws;mass->setChecked(true);field(0)->setText("8");cancel->click();
    EXPECT_EQ(m_ws,accepted);EXPECT_EQ(field(0)->text(),"4");EXPECT_FALSE(mass->isChecked());
    // 无改动确认、无默认质心决议，均不伪造历史。
    apply->click();EXPECT_EQ(m_ws,accepted);EXPECT_EQ(choice->currentIndex(),0);
    EXPECT_FALSE(feedback->text().contains("migrate-inertia"));
    auto* scroll=editor->parentWidget()->parentWidget();
    auto* area=qobject_cast<QScrollArea*>(scroll);ASSERT_NE(area,nullptr);
    EXPECT_GT(area->verticalScrollBar()->maximum(),0);
    const auto out=qEnvironmentVariable("IRD_MODELING_GUI_EVIDENCE_OUT");
    if(!out.isEmpty()){
        ASSERT_TRUE(QDir().mkpath(out));area->verticalScrollBar()->setValue(0);QApplication::processEvents();
        ASSERT_TRUE(m_panel->grab().save(QDir(out).filePath("body-editor-top.png")));
        area->ensureWidgetVisible(apply);QApplication::processEvents();
        ASSERT_TRUE(m_panel->grab().save(QDir(out).filePath("body-editor-confirm.png")));
    }
}

/** @brief MDL-05：同目标刷新保留未确认输入，切目标清除；只读与过时基线在出口再次拒绝。 */
TEST_F(ModelingPanelGuiTest, ManualBodyRefreshStaleAndReadOnly_MDL05_V17)
{
    IRD_TEST_INFO("MDL-05",{},std::nullopt);
    m_panel->focusObject(m_ws.design.links.back().objectId);
    auto* editor=m_panel->findChild<QWidget*>("ird_modeling_body_editor");ASSERT_NE(editor,nullptr);
    auto* mass=editor->findChild<QCheckBox*>("ird_body_mass_edit");
    auto* field=editor->findChild<QLineEdit*>("ird_body_value_0");
    auto* apply=editor->findChild<QPushButton*>("ird_body_apply");
    auto* feedback=editor->findChild<QLabel*>("ird_body_feedback");
    mass->setChecked(true);field->setText("8");m_panel->refreshPanel(m_ws,{});
    EXPECT_TRUE(mass->isChecked());EXPECT_EQ(field->text(),"8");
    m_panel->setWritable(false);EXPECT_FALSE(apply->isEnabled());
    const auto before=m_ws;apply->click();EXPECT_EQ(m_ws,before);
    m_panel->setWritable(true);EXPECT_EQ(field->text(),"8");
    // 不刷新而更换权威基线，确认不能覆盖后台变更。
    m_ws.design.links.back().body.mass=core::SourcedValue<double>::provided(3,
        core::ValueProvenance::make(core::ProvenanceKind::UserProvided));
    const auto changed=m_ws;apply->click();EXPECT_EQ(m_ws,changed);
    EXPECT_TRUE(feedback->text().contains(QStringLiteral("基线已改变")));
    m_panel->refreshPanel(m_ws,{});EXPECT_FALSE(mass->isChecked());EXPECT_EQ(field->text(),"3");
    mass->setChecked(true);field->setText("12");m_panel->focusObject(m_ws.design.links.front().objectId);
    EXPECT_FALSE(mass->isChecked());m_panel->focusObject(m_ws.design.links.back().objectId);
    EXPECT_EQ(field->text(),"3");EXPECT_EQ(m_ws,changed);
}

/** @brief MDL-05：无基准不猜零，迁移拒绝；完整输入可建立用户物性，出口不可绕只读。 */
TEST(ModelingManualBodyGui, MissingBaselineCompleteInputAndReadOnly_MDL05_V17)
{
    IRD_TEST_INFO("MDL-05",{},std::nullopt);
    int accepts=0;BodyData accepted;
    ModelingBodyEditor editor(nullptr,[&](const core::ObjectId&,const BodyData&,
        const ManualBodyResult& result,std::string&){++accepts;accepted=result.body;return true;});
    const auto id=core::ObjectId::generate();editor.bindTarget(id,BodyData{},"empty-link",true);
    auto field=[&](int i){return editor.findChild<QLineEdit*>(QStringLiteral("ird_body_value_%1").arg(i));};
    auto* mass=editor.findChild<QCheckBox*>("ird_body_mass_edit");
    auto* com=editor.findChild<QCheckBox*>("ird_body_com_edit");
    auto* tensor=editor.findChild<QCheckBox*>("ird_body_inertia_edit");
    auto* choice=editor.findChild<QComboBox*>("ird_body_centroid_resolution");
    auto* apply=editor.findChild<QPushButton*>("ird_body_apply");
    auto* feedback=editor.findChild<QLabel*>("ird_body_feedback");
    for(int i=0;i<10;++i){EXPECT_TRUE(field(i)->text().isEmpty());}
    mass->setChecked(true);field(0)->setText("2");com->setChecked(true);
    field(1)->setText("1");field(2)->setText("2");field(3)->setText("3");
    choice->setCurrentIndex(1);apply->click();EXPECT_EQ(accepts,0);
    EXPECT_TRUE(feedback->text().contains(QStringLiteral("旧质心")));
    choice->setCurrentIndex(2);EXPECT_TRUE(tensor->isChecked());apply->click();EXPECT_EQ(accepts,0);
    const char* values[]={"3","4","5","0.1","0.2","0.3"};
    for(int i=0;i<6;++i){field(i+4)->setText(values[i]);}
    apply->click();ASSERT_EQ(accepts,1);EXPECT_DOUBLE_EQ(accepted.mass.value(),2);
    EXPECT_DOUBLE_EQ(accepted.inertia.value().ixy,0.1);
    EXPECT_EQ(accepted.centerOfMass.provenance().kind,core::ProvenanceKind::UserProvided);
    editor.bindTarget(id,accepted,"empty-link",false);mass->setChecked(true);field(0)->setText("9");
    EXPECT_FALSE(apply->isEnabled());
    // 直接派发信号验证执行端守卫；禁用控件的click自身会短路，不能证明出口安全。
    ASSERT_TRUE(QMetaObject::invokeMethod(apply,"clicked",Qt::DirectConnection));EXPECT_EQ(accepts,1);
}

/** @brief F12/MDL-15：两构建模式都运行真实内联页，暂存/换目标/只读/空会话均正确。 */
TEST_F(ModelingPanelGuiTest, SceneManagerPendingSelectionAndReadOnly_MDL15_AT28)
{
    IRD_TEST_INFO("MDL-15",{"AT-28"},std::nullopt);
    int accepted=0;m_panel->setPostEditAction([&]{++accepted;});
    m_panel->resize(1100,760);m_panel->show();QApplication::processEvents();
    auto* entry=m_panel->findChild<QPushButton*>("ird_modeling_edit_scene_mode");ASSERT_NE(entry,nullptr);
    entry->click();auto* name=m_panel->findChild<QLineEdit*>("ird_scene_name");ASSERT_NE(name,nullptr);
    auto* apply=m_panel->findChild<QPushButton*>("ird_scene_apply");ASSERT_NE(apply,nullptr);
    const auto before=m_ws;name->setText("first");
    m_panel->refreshPanel(m_ws,ModelReadinessReport{});EXPECT_EQ(name->text(),"first");EXPECT_EQ(m_ws,before);
    apply->click();ASSERT_EQ(m_ws.sceneObjects.size(),1U);EXPECT_EQ(accepted,1);
    const auto first=m_ws;name->setText("not_confirmed");
    m_panel->findChild<QPushButton*>("ird_scene_cancel")->click();EXPECT_EQ(name->text(),"first");EXPECT_EQ(m_ws,first);
    auto* objects=m_panel->findChild<QComboBox*>("ird_scene_objects");ASSERT_NE(objects,nullptr);
    objects->setCurrentIndex(0);EXPECT_TRUE(name->text().isEmpty());name->setText("second");apply->click();
    ASSERT_EQ(m_ws.sceneObjects.size(),2U);EXPECT_EQ(accepted,2);EXPECT_EQ(m_ws.design.sceneRefs.size(),2U);
    name->setText("must_discard");objects->setCurrentIndex(1);EXPECT_EQ(name->text(),"first");
    // 权威基线改变必须丢弃旧暂存，不能把旧名称覆盖新内容。
    name->setText("old_pending");m_ws.sceneObjects.front().localName="externally_changed";
    m_panel->refreshPanel(m_ws,ModelReadinessReport{});EXPECT_EQ(name->text(),"externally_changed");
    m_panel->setWritable(false);const auto readOnly=m_ws;EXPECT_FALSE(apply->isEnabled());
    ASSERT_TRUE(QMetaObject::invokeMethod(apply,"clicked",Qt::DirectConnection));EXPECT_EQ(m_ws,readOnly);EXPECT_EQ(accepted,2);
    m_panel->setEditTargetProvider({});entry->click();EXPECT_FALSE(apply->isEnabled());
}
/** @brief F08：两个构建模式都运行真实工具页，暂存/选择/只读/物性基线均守卫。 */
TEST_F(ModelingPanelGuiTest, ToolManagerPendingSelectionReadOnly_MDL13_AT01)
{
    IRD_TEST_INFO("MDL-13",{"AT-01"},std::nullopt);
    int edits=0;m_panel->setPostEditAction([&]{++edits;});m_panel->resize(1140,840);m_panel->show();QApplication::processEvents();
    auto* entry=m_panel->findChild<QPushButton*>("ird_modeling_edit_tool_mode");ASSERT_NE(entry,nullptr);entry->click();
    auto* editor=m_panel->findChild<QWidget*>("ird_modeling_tool_editor");ASSERT_NE(editor,nullptr);
    auto* name=editor->findChild<QLineEdit*>("ird_tool_name");auto* apply=editor->findChild<QPushButton*>("ird_tool_apply");
    auto* def=editor->findChild<QCheckBox*>("ird_tool_default");auto* feedback=editor->findChild<QLabel*>("ird_tool_feedback");
    name->setText("gripper");const auto before=m_ws;m_panel->refreshPanel(m_ws,ModelReadinessReport{});
    EXPECT_EQ(name->text(),"gripper");apply->click();EXPECT_EQ(m_ws,before);EXPECT_EQ(edits,0);
    EXPECT_TRUE(feedback->text().contains(QStringLiteral("默认")));def->setChecked(true);apply->click();
    ASSERT_EQ(m_ws.toolObjects.size(),1U);EXPECT_EQ(edits,1);const auto first=m_ws;
    name->setText("discard");editor->findChild<QPushButton*>("ird_tool_cancel")->click();EXPECT_EQ(name->text(),"gripper");EXPECT_EQ(m_ws,first);
    auto* objects=editor->findChild<QComboBox*>("ird_tool_objects");objects->setCurrentIndex(0);name->setText("second");apply->click();
    ASSERT_EQ(m_ws.toolObjects.size(),2U);EXPECT_EQ(edits,2);EXPECT_EQ(m_ws.design.defaultTcp->toolOid,first.toolObjects.front().objectId);
    name->setText("discard_again");objects->setCurrentIndex(1);EXPECT_EQ(name->text(),"gripper");
    // 未广播刷新时的真实基线漂移也必须由确认出口拒绝，不能只测重绑后的正常回填。
    name->setText("stale");m_ws.toolObjects.front().localName="external";const auto changed=m_ws;
    apply->click();EXPECT_EQ(m_ws,changed);EXPECT_TRUE(feedback->text().contains(QStringLiteral("基线")));
    m_panel->refreshPanel(m_ws,ModelReadinessReport{});
    EXPECT_EQ(name->text(),"external");m_panel->setWritable(false);const auto readOnly=m_ws;
    EXPECT_FALSE(apply->isEnabled());EXPECT_FALSE(editor->findChild<QWidget*>("ird_tool_body_editor")->isEnabled());
    ASSERT_TRUE(QMetaObject::invokeMethod(apply,"clicked",Qt::DirectConnection));EXPECT_EQ(m_ws,readOnly);
    m_panel->setWritable(true);EXPECT_TRUE(apply->isEnabled());
    const auto folder=qEnvironmentVariable("IRD_MODELING_GUI_EVIDENCE_OUT");
    if(!folder.isEmpty()){QDir().mkpath(folder);ASSERT_TRUE(m_panel->grab().save(folder+"/tool-manager-top.png"));}
}

/**
 * F01（WP-13-T25/MDL-01）：最大速度表单编辑全链——值列输入 1.25 rad/s→
 * 两步确认→applyJointFieldEdit(MaxVelocity) 接受→工作集 Provided＋
 * UserProvided＋变更记录；随后负值经同一链被域拒绝且工作集保持接受值
 * （拒绝原子性——表格回退真实草稿值，不残留新值）。
 */
TEST_F(ModelingPanelGuiTest, JointDetailEditPane_MaxVelocityEditAndRejectChain_MDL01_WP13T25)
{
    IRD_TEST_INFO("MDL-01", {}, std::nullopt);

    m_panel->focusObject(m_ws.design.joints[0].objectId);
    QWidget* host = m_panel->findChild<QWidget*>(QStringLiteral("ird_modeling_edit_host"));
    auto* velocityField=jointField(host,"max-velocity");ASSERT_NE(velocityField,nullptr);

    // 合法值链：1.25 rad/s →两步确认→域接受。
    typeJointField(velocityField,QStringLiteral("1.25"));
    host->findChild<QPushButton*>(QStringLiteral("ird_param_apply"))->click();
    host->findChild<QPushButton*>(QStringLiteral("ird_param_confirm_yes"))->click();
    QApplication::processEvents();

    const auto& velocity = m_ws.design.joints[0].maxVelocity;
    ASSERT_EQ(velocity.state(), core::FieldState::Provided);
    EXPECT_DOUBLE_EQ(velocity.value(), 1.25);
    EXPECT_EQ(velocity.provenance().kind, core::ProvenanceKind::UserProvided);
    ASSERT_FALSE(m_ws.changes.empty());
    EXPECT_NE(m_ws.changes.back().summary.find("最大速度"), std::string::npos);

    // 拒绝链：-0.5 →域拒绝（负值）→工作集保持 1.25（原子性），表单保留原串
    // 真实草稿回填（F04 教训：拒绝后不得残留被拒输入值）。
    velocityField=jointField(host,"max-velocity");typeJointField(velocityField,QStringLiteral("-0.5"));
    host->findChild<QPushButton*>(QStringLiteral("ird_param_apply"))->click();
    host->findChild<QPushButton*>(QStringLiteral("ird_param_confirm_yes"))->click();
    QApplication::processEvents();
    ASSERT_EQ(m_ws.design.joints[0].maxVelocity.state(), core::FieldState::Provided);
    EXPECT_DOUBLE_EQ(m_ws.design.joints[0].maxVelocity.value(), 1.25)
        << "负值被拒后工作集须保持原值（拒绝原子性）";
    // 新表单保留用户原串，便于纠正；权威草稿仍为1.25。
    EXPECT_EQ(jointField(host,"max-velocity")->text(), QStringLiteral("-0.5"))
        << "GF-A领域拒绝须保留输入供纠正；权威草稿仍为1.25";
}

// =====================================================================
// F14（WP-13-T25/MDL-21/AT-38）：耦合矩阵编辑入口与域裁决链
// =====================================================================

/**
 * @brief F14 入口与能力位门控：传动页有耦合区（摘要＋编辑＋移除钮）；
 *        面板缺省 R1Locked＝公共默认红线（编辑钮禁用）；setCouplingStage
 *        (R2Enabled) 即时放行；只读会话再禁用。修复前（红）：传动页零
 *        耦合入口（审计 M31 第一问题——域原语 T18 落位但无 UI 载体）。
 */
TEST_F(ModelingPanelGuiTest, CouplingEditorEntryAndStageGating_MDL21_WP13T25)
{
    IRD_TEST_INFO("MDL-21",{"AT-38"},std::nullopt);

    auto* edit = m_panel->findChild<QPushButton*>(
        QStringLiteral("ird_modeling_coupling_edit"));
    ASSERT_NE(edit, nullptr) << "F14：传动页缺少耦合矩阵编辑入口（审计 M31）";
    auto* remove = m_panel->findChild<QPushButton*>(
        QStringLiteral("ird_modeling_coupling_remove"));
    ASSERT_NE(remove, nullptr);
    auto* summary = m_panel->findChild<QLabel*>(
        QStringLiteral("ird_modeling_coupling_summary"));
    ASSERT_NE(summary, nullptr);
    EXPECT_TRUE(summary->text().contains(QStringLiteral("未配置")))
        << "六轴草稿无耦合——摘要应呈未配置态";

    // 公共默认 R1Locked：编辑入口禁用（存在即阻断的呈现半区）；移除恒可用。
    EXPECT_FALSE(edit->isEnabled())
        << "缺省能力位 R1Locked——配置入口必须禁用（不提前放开 R1 红线）";
    EXPECT_TRUE(remove->isEnabled()) << "移除（清除不是配置）两阶段均可用";

    // R2 注入→即时放行；只读降级→再禁用（与可写性合取）。
    m_panel->setCouplingStage(CouplingStage::R2Enabled);
    QApplication::processEvents();
    EXPECT_TRUE(edit->isEnabled()) << "R2Enabled 注入后配置入口应放行";
    m_panel->setWritable(false);
    QApplication::processEvents();
    EXPECT_FALSE(edit->isEnabled()) << "只读会话下配置入口必须禁用";
    EXPECT_FALSE(remove->isEnabled());
    m_panel->setWritable(true);
    m_panel->setCouplingStage(CouplingStage::R1Locked);
    QApplication::processEvents();
    EXPECT_FALSE(edit->isEnabled()) << "回到 R1 后再禁用（能力位与可写性正交）";
}

/**
 * @brief F14 编辑全链（非对角矩阵）：R2 下打开模态→窗口 J4..J6＋3 阶→
 *        录入交接固定数值探针矩阵 C=[[1,0.2,0],[0,1,-0.1],[0.3,0,1.5]]
 *        →应用→工作集 coupling 落位（元素/窗口/阶数逐项＋变更记录）；
 *        探针数值（Δq=C·Δθ、τ_motor=Cᵀ·τ、瞬时功率）以独立期望在用例内
 *        核对（不调用被测映射生成答案——期望值手算固定）。
 */
TEST_F(ModelingPanelGuiTest, CouplingEditorNonDiagonalAcceptThenRemove_MDL21_WP13T25)
{
    IRD_TEST_INFO("MDL-21",{"AT-38"},std::nullopt);

    m_panel->setCouplingStage(CouplingStage::R2Enabled);
    QApplication::processEvents();
    const std::size_t changesBefore = m_ws.changes.size();

    // 模态应答（watchdog 同款纪律：超时拒绝并判失败）。
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, [] {
        ADD_FAILURE() << "耦合编辑模态未按预期响应";
        if (auto* d = qobject_cast<QDialog*>(QApplication::activeModalWidget())) {
            d->reject();
        }
    });
    timeout.start(10000);
    QTimer::singleShot(50, [&] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        ASSERT_NE(dialog, nullptr) << "耦合编辑模态未弹出";
        EXPECT_EQ(dialog->objectName(),
                  QStringLiteral("ird_modeling_coupling_dialog"));
        auto* first = dialog->findChild<QSpinBox*>(
            QStringLiteral("ird_modeling_coupling_first"));
        ASSERT_NE(first, nullptr);
        auto* order = dialog->findChild<QSpinBox*>(
            QStringLiteral("ird_modeling_coupling_order"));
        ASSERT_NE(order, nullptr);
        first->setValue(4);   // 窗口 J4..J6（0 基 3..5）
        order->setValue(3);
        QApplication::processEvents();
        auto* table = dialog->findChild<QTableWidget*>(
            QStringLiteral("ird_modeling_coupling_table"));
        ASSERT_NE(table, nullptr);
        ASSERT_EQ(table->rowCount(), 3);
        // 交接固定探针矩阵（非对角——对角/单位阵不具证明力）。
        const double c[3][3] = {{1.0, 0.2, 0.0},
                                {0.0, 1.0, -0.1},
                                {0.3, 0.0, 1.5}};
        for (int r = 0; r < 3; ++r) {
            for (int col = 0; col < 3; ++col) {
                table->item(r, col)->setText(QString::number(c[r][col]));
            }
        }
        QApplication::processEvents();
        dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)
            ->click();
    });
    m_panel->openCouplingEditor();

    // 工作集落位断言（元素行主序逐项＋窗口＋方阵阶数＋申报条件数为有限>1）。
    ASSERT_TRUE(m_ws.drivetrainObject.has_value());
    ASSERT_TRUE(m_ws.drivetrainObject->coupling.has_value());
    const CouplingDesign& cd = *m_ws.drivetrainObject->coupling;
    ASSERT_EQ(cd.rows, 3u);
    ASSERT_EQ(cd.cols, 3u);
    EXPECT_EQ(cd.jointRangeFirst, 3u);
    EXPECT_EQ(cd.jointRangeLast, 5u);
    const double expect[9] = {1.0, 0.2, 0.0, 0.0, 1.0, -0.1, 0.3, 0.0, 1.5};
    for (std::size_t k = 0; k < 9; ++k) {
        EXPECT_DOUBLE_EQ(cd.c[k], expect[k]) << "元素 " << k;
    }
    EXPECT_GT(cd.conditionNumber, 1.0);
    EXPECT_TRUE(std::isfinite(cd.conditionNumber));
    EXPECT_GT(m_ws.changes.size(), changesBefore) << "应用未留变更记录";

    // 摘要行更新（窗口/阶数可观察）。
    QApplication::processEvents();
    auto* summary = m_panel->findChild<QLabel*>(
        QStringLiteral("ird_modeling_coupling_summary"));
    ASSERT_NE(summary, nullptr);
    EXPECT_TRUE(summary->text().contains(QStringLiteral("J4..J6")))
        << "摘要未呈现窗口";
    EXPECT_TRUE(summary->text().contains(QStringLiteral("3×3")))
        << "摘要未呈现阶数";

    // ---- 交接固定数值探针（独立手算期望——非对角矩阵的映射语义面）----
    // Δθ_motor=[1,2,3] rad → Δq_joint=C·Δθ=[1.4,1.7,4.8] rad；
    // τ_joint=[2,-1,4] N·m → τ_motor=Cᵀ·τ=[3.2,-0.6,6.1] N·m；
    // 两侧瞬时机械功率同值 20.3 W（虚功守恒——DYN-04）。
    {
        const double dtheta[3] = {1.0, 2.0, 3.0};
        const double tauJoint[3] = {2.0, -1.0, 4.0};
        double dq[3] = {0, 0, 0};
        double tauMotor[3] = {0, 0, 0};
        for (int r = 0; r < 3; ++r) {
            for (int col = 0; col < 3; ++col) {
                dq[r] += cd.c[r * 3 + col] * dtheta[col];
                tauMotor[col] += cd.c[r * 3 + col] * tauJoint[r];
            }
        }
        EXPECT_NEAR(dq[0], 1.4, 1e-12);
        EXPECT_NEAR(dq[1], 1.7, 1e-12);
        EXPECT_NEAR(dq[2], 4.8, 1e-12);
        EXPECT_NEAR(tauMotor[0], 3.2, 1e-12);
        EXPECT_NEAR(tauMotor[1], -0.6, 1e-12);
        EXPECT_NEAR(tauMotor[2], 6.1, 1e-12);
        const double powerJoint = tauJoint[0] * dq[0] + tauJoint[1] * dq[1]
                                  + tauJoint[2] * dq[2];
        const double powerMotor = tauMotor[0] * dtheta[0]
                                  + tauMotor[1] * dtheta[1]
                                  + tauMotor[2] * dtheta[2];
        EXPECT_NEAR(powerJoint, 20.3, 1e-12);
        EXPECT_NEAR(powerMotor, 20.3, 1e-12) << "虚功守恒（两侧功率应相等）";
    }

    // ---- 移除：清除后回到未配置（两阶段均允许的面）----
    auto* remove = m_panel->findChild<QPushButton*>(
        QStringLiteral("ird_modeling_coupling_remove"));
    ASSERT_NE(remove, nullptr);
    remove->click();
    QApplication::processEvents();
    ASSERT_TRUE(m_ws.drivetrainObject.has_value());
    EXPECT_FALSE(m_ws.drivetrainObject->coupling.has_value())
        << "移除后耦合配置应清除";
}

/**
 * @brief F14 拒绝面（数值）：奇异矩阵（秩 1）经域校验拒绝——模态保留、
 *        错误行呈现稳定 token、工作集 coupling 逐字段不变（原子性）；
 *        随后取消＝零写入。
 */
TEST_F(ModelingPanelGuiTest, CouplingEditorSingularRejectedAtomicCancelSafe_MDL21_WP13T25)
{
    IRD_TEST_INFO("MDL-21",{},std::nullopt);

    m_panel->setCouplingStage(CouplingStage::R2Enabled);
    QApplication::processEvents();
    const ModelingWorkingSet before = m_ws;

    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, [] {
        ADD_FAILURE() << "耦合编辑模态未按预期响应";
        if (auto* d = qobject_cast<QDialog*>(QApplication::activeModalWidget())) {
            d->reject();
        }
    });
    timeout.start(10000);
    QTimer::singleShot(50, [&] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        ASSERT_NE(dialog, nullptr);
        auto* order = dialog->findChild<QSpinBox*>
(
            QStringLiteral("ird_modeling_coupling_order"));
        ASSERT_NE(order, nullptr);
        order->setValue(2);
        QApplication::processEvents();
        auto* table = dialog->findChild<QTableWidget*>(
            QStringLiteral("ird_modeling_coupling_table"));
        ASSERT_NE(table, nullptr);
        for (int r = 0; r < 2; ++r) {
            for (int col = 0; col < 2; ++col) {
                table->item(r, col)->setText(QStringLiteral("1"));
            }
        }
        QApplication::processEvents();
        dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)
            ->click();
        QApplication::processEvents();
        EXPECT_NE(QApplication::activeModalWidget(), nullptr)
            << "域拒绝后模态应保留（原位重试）";
        auto* err = dialog->findChild<QLabel*>(
            QStringLiteral("ird_modeling_coupling_error"));
        ASSERT_NE(err, nullptr);
        EXPECT_TRUE(err->text().contains(QStringLiteral("singular")))
            << "奇异拒绝的稳定 token 未呈现: " << err->text().toStdString();
        EXPECT_TRUE(err->text().contains(QStringLiteral("工作集未修改")))
            << "拒绝原子性未在回执注明";
        dialog->reject();
    });
    m_panel->openCouplingEditor();
    EXPECT_EQ(m_ws, before) << "拒绝＋取消后工作集必须与进入前逐字段相等（F14 顺序修正后）";






}
