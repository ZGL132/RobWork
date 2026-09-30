/**
 * @file   IndustrialProjectTreeGuiTest.cpp
 * @brief  UI-T21 GUI 层用例（QApplication＋真实 Widget 树——§12.1 第三层）：
 *         工业项目树面板的呈现行为——五分组固定组行（封闭清单可视不变量）、
 *         显示名经名称端口现取（零第二套命名缓存的呈现面证据）、树选中
 *         写入选择服务（L1 联动基线）、L3 反解成功的树定位落点与反解
 *         失败的零触碰（UI-TRE-6/7/8 登记——ui.md §12.3）。
 *
 * 设计依据：
 *   - 任务契约 tasks/foundation/UI-T21.json acceptance 1/3/4/5/6（GUI
 *     冒烟留痕的具名用例面）；
 *   - B1-SPEC §3.1/§3.3（五分组封闭/INV-B1 呈现面——组数恒为 5；INV-B2
 *     反解失败不产生虚假节点＝行数不变）、§4.2（L1/L3 的呈现半区）；
 *   - units/ui.md §13 UI-T21 行、§12.2 执行纪律、§3.1（替身承载惯例）；
 *   - 同构先例：PolicySummaryCardGuiTest.cpp（夹具形态/替身清单）。
 *
 * 模型/GUI 分工：重建校验/序/定位的数据面在模型层
 * （IndustrialProjectTreeModelTest.cpp）与选择服务分支在
 * SelectionServiceTest.cpp 逐条断言；本文件断言真实控件树的行为面
 * （选中信号回流/名称渲染/定位高亮）。
 */

#include <gtest/gtest.h>

#include <QApplication>
#include <QComboBox>   // 分组过滤下拉（UI-T26 检索用例驱动面）
#include <QLineEdit>   // 搜索框（UI-T26 检索用例驱动面）
#include <QTreeWidget>
#include <QWidget>

#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/diagnostics/Catalog.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>
#include <sdurws/ird/ui/IndustrialProjectTree.hpp>
#include <sdurws/ird/ui/SelectionService.hpp>
#include <sdurws/ird/ui/UiPorts.hpp>

#include <algorithm>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace {

using namespace sdurws::ird;
using sdurws::ird::ui::IUiNameResolver;
using sdurws::ird::ui::IUiRuntimeNameMapPort;
using sdurws::ird::ui::IUiSelectionObserver;
using sdurws::ird::ui::IndustrialProjectTreePanel;
using sdurws::ird::ui::IndustrialProjectTreePanelDeps;
using sdurws::ird::ui::ProjectTreeGroup;
using sdurws::ird::ui::ProjectTreeNode;
using sdurws::ird::ui::ProjectTreeModel;
using sdurws::ird::ui::SelectionChange;
using sdurws::ird::ui::SelectionService;
using sdurws::ird::ui::SelectionSource;
using core::ObjectId;

// =====================================================================
// 测试替身（各 TU 独立声明——模型套件同款纪律）
// =====================================================================

template <typename Id>
Id idFrom(const std::string& seed)
{
    core::ContentDigester d;
    d.update(seed.data(), seed.size());
    const core::Digest256 digest = d.finalize();
    Id id;
    std::copy(digest.begin(), digest.begin() + 16, id.bytes.begin());
    return id;
}

/// 名称解析替身（渲染显示名的可控源——映射可运行期改写以断言"零缓存：
/// 名称变化经 refresh 即见新值"）。
class StubNameResolver final : public IUiNameResolver {
public:
    std::map<std::string, std::string> names;  ///< ObjectId 规范文本→显示名

    std::optional<std::string> resolveObjectId(core::ObjectId id) const override
    {
        auto it = names.find(id.toCanonical());
        if (it == names.end()) {
            return std::nullopt;
        }
        return it->second;
    }
};

/// 名称映射替身（SA-05 反解——L3 编排输入；正向表未用＝GUI 套件只走
/// L3 路径）。
class StubRuntimeNameMap final : public IUiRuntimeNameMapPort {
public:
    std::map<std::string, ObjectId> reverse;

    std::optional<ObjectId>
    resolveObjectIdFromRuntimeName(const std::string& runtimeName) const override
    {
        auto it = reverse.find(runtimeName);
        if (it == reverse.end()) {
            return std::nullopt;
        }
        return it->second;
    }
    std::optional<std::string> resolveRuntimeName(const ObjectId&) const override
    {
        return std::nullopt;  // GUI 套件不触发 L2 高亮（模型套件已覆盖）
    }
};

/// 选择变更记录观察者（L1 基线的"检查器消费"替身——收到事件即登记）。
class RecordingObserver final : public IUiSelectionObserver {
public:
    std::vector<SelectionChange> changes;

    void onSelectionChanged(const SelectionChange& change) override
    {
        changes.push_back(change);
    }
};

/// 固定供给替身（工业项目树面板的两个域——建模/需求）。
class StubProvider final : public sdurws::ird::ui::IUiTreeNodesProvider {
public:
    StubProvider(std::string key, std::vector<ProjectTreeNode> nodes)
        : m_key(std::move(key)), m_nodes(std::move(nodes)) {}

    std::string domainKey() const override { return m_key; }
    std::vector<ProjectTreeNode> treeNodes() const override { return m_nodes; }

private:
    std::string m_key;
    std::vector<ProjectTreeNode> m_nodes;
};

/// Dev 日志替身（服务装配必填面之外的兜底——留痕非断言对象）。
class StubDevLog final : public diagnostics::IDevLogSink {
public:
    void logDev(std::string_view, std::string) override {}
};

// =====================================================================
// 夹具（面板＋模型＋服务＋两替身的装配——每用例独立）
// =====================================================================

class IndustrialProjectTreeGuiTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_model = std::make_shared<ProjectTreeModel>();
        m_resolver = std::make_shared<StubNameResolver>();
        m_nameMap = std::make_shared<StubRuntimeNameMap>();
        m_devLog = std::make_shared<StubDevLog>();

        // 服务装配（树定位＝面板把手转接——生产装配形态的最小镜像；
        // m_panel 在 buildPanel 时回填）。
        SelectionService::Deps deps;
        deps.nameMap = m_nameMap;
        deps.devLog = m_devLog;
        deps.treeLocator = [this](const ObjectId& id) {
            return m_panel ? m_panel->locateAndHighlight(id) : false;
        };
        m_selection = std::make_shared<SelectionService>(std::move(deps));
    }

    /// 装配面板（两域供给：建模关节对象＋需求任务点对象；名称映射齐备）。
    void buildPanelWithTwoDomains()
    {
        const ObjectId j1 = idFrom<ObjectId>("gui-joint-1");
        const ObjectId j2 = idFrom<ObjectId>("gui-joint-2");
        const ObjectId taskPoint = idFrom<ObjectId>("gui-taskpoint-1");
        m_ids = {j1, j2, taskPoint};

        m_model->addProvider(std::make_shared<StubProvider>(
            "modeling",
            std::vector<ProjectTreeNode>{nodeOf(j1, ProjectTreeGroup::ModelingObjects, 0),
                                         nodeOf(j2, ProjectTreeGroup::ModelingObjects, 0)}));
        m_model->addProvider(std::make_shared<StubProvider>(
            "requirements",
            std::vector<ProjectTreeNode>{nodeOf(taskPoint, ProjectTreeGroup::RequirementObjects, 0)}));
        m_nameMap->reverse["Frame_J1"] = j1;
        m_resolver->names[j1.toCanonical()] = "关节 1";
        m_resolver->names[j2.toCanonical()] = "关节 2";
        m_resolver->names[taskPoint.toCanonical()] = "任务点 A";
        ASSERT_TRUE(m_model->rebuild().ok);

        IndustrialProjectTreePanelDeps deps;
        deps.model = m_model;
        deps.selection = m_selection;
        deps.nameResolver = m_resolver;
        m_panel = sdurws::ird::ui::createIndustrialProjectTreePanel(deps, nullptr);
        // UI-T26 容器化：widget() 出口＝检索行＋树的容器（此前直返树本体
        // ——本行随容器化同步修订，树经 findChild 定位，语义不变）。
        m_panelWidget = m_panel->widget()->findChild<QTreeWidget*>();
        ASSERT_NE(m_panelWidget, nullptr);
    }

    /// 造节点值（分组/深度给死——GUI 套件不触模型校验分支；显示名不在
    /// 节点值上〔零第二套命名〕——名称由 resolver 替身供给）。
    static ProjectTreeNode nodeOf(const ObjectId& id, ProjectTreeGroup group,
                                  std::uint8_t depth)
    {
        ProjectTreeNode node;
        node.objectId = id;
        node.group = group;
        node.depth = depth;
        return node;
    }

    /// 按显示文本查节点行（组内顶层扫描——本套件节点均为组直属）。
    QTreeWidgetItem* findRowByLabel(const QString& label) const
    {
        for (int g = 0; g < m_panelWidget->topLevelItemCount(); ++g) {
            QTreeWidgetItem* group = m_panelWidget->topLevelItem(g);
            for (int i = 0; i < group->childCount(); ++i) {
                if (group->child(i)->text(0) == label) {
                    return group->child(i);
                }
            }
        }
        return nullptr;
    }

    std::shared_ptr<ProjectTreeModel> m_model;
    std::shared_ptr<StubNameResolver> m_resolver;
    std::shared_ptr<StubRuntimeNameMap> m_nameMap;
    std::shared_ptr<StubDevLog> m_devLog;
    std::shared_ptr<SelectionService> m_selection;
    std::unique_ptr<IndustrialProjectTreePanel> m_panel;
    QTreeWidget* m_panelWidget = nullptr;
    std::vector<ObjectId> m_ids;
};

// =====================================================================
// ①五分组固定组行（INV-B1 呈现面——封闭清单可视不变量）
// =====================================================================

/// UI-TRE-6：组数恒为五（封闭清单的可视不变量——空组也呈现组头；组行
/// 不可选中＝"分组是结构不是对象"，INV-B1 呈现面断言锚）。
TEST_F(IndustrialProjectTreeGuiTest, UI_TRE_6_PanelShowsFiveFixedGroupRows)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-02"}, {});
    buildPanelWithTwoDomains();

    ASSERT_EQ(m_panelWidget->topLevelItemCount(), 5);
    // 组行不可选中（flags 无 ItemIsSelectable——分组不是业务对象）。
    for (int g = 0; g < 5; ++g) {
        QTreeWidgetItem* group = m_panelWidget->topLevelItem(g);
        EXPECT_EQ(group->flags() & Qt::ItemIsSelectable, 0) << "group " << g;
    }
    // 需求分组下有一行（域供给入组——分组承载域的呈现归属）。
    QTreeWidgetItem* requirementGroup = m_panelWidget->topLevelItem(2);
    ASSERT_NE(requirementGroup, nullptr);
    EXPECT_EQ(requirementGroup->childCount(), 1);
}

// =====================================================================
// ②树选中写服务（L1 联动基线——acceptance 3）
// =====================================================================

/// UI-TRE-6：节点行选中→服务以 ProjectTree 来源写入并广播（检查器
/// 替身观察者收到事件＝L1"树选→检查器刷新"的联动基线成立）。
TEST_F(IndustrialProjectTreeGuiTest, UI_TRE_6_NodeSelectionWritesService)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-02"}, {});
    buildPanelWithTwoDomains();
    RecordingObserver inspector;
    auto sub = m_selection->subscribe(inspector);

    QTreeWidgetItem* row = findRowByLabel(QStringLiteral("关节 2"));
    ASSERT_NE(row, nullptr);
    m_panelWidget->setCurrentItem(row);  // Qt 选中信号→selectBusiness 回流

    // 服务状态（唯一汇聚点——树自身零选中留存，INV-B3）。
    ASSERT_EQ(m_selection->selectedObjectIds().size(), std::size_t{1});
    EXPECT_TRUE(m_selection->selectedObjectIds().front() == m_ids[1]);
    ASSERT_TRUE(m_selection->selectionSource().has_value());
    EXPECT_EQ(*m_selection->selectionSource(), SelectionSource::ProjectTree);
    // 检查器（替身）收到刷新依据（L1 基线——UI-T22 检查器本体订阅同面）。
    ASSERT_EQ(inspector.changes.size(), std::size_t{1});
    EXPECT_EQ(inspector.changes.front().source, SelectionSource::ProjectTree);
}

// =====================================================================
// ③显示名经名称端口现取（UX-02 零第二套命名缓存的呈现面证据）
// =====================================================================

/// UI-TRE-6：渲染名＝端口现取（解析命中显示名；未解析显示占位；名称
/// 变化经 refresh 即见新值——面板零名称缓存）。
TEST_F(IndustrialProjectTreeGuiTest, UI_TRE_6_DisplayNameResolvedViaNamePort)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-02"}, {});
    buildPanelWithTwoDomains();

    // 解析命中：显示端口提供的名称（零哈希/规范文本进界面——UX-02）。
    QTreeWidgetItem* row = findRowByLabel(QStringLiteral("关节 1"));
    ASSERT_NE(row, nullptr);

    // 未解析对象：占位呈现（解析失败→占位而非伪造名称——UX-02）。
    const ObjectId unnamed = idFrom<ObjectId>("gui-unnamed-1");
    ProjectTreeNode unnamedNode =
        nodeOf(unnamed, ProjectTreeGroup::AnalysisConfigurations, 0);
    m_model->addProvider(std::make_shared<StubProvider>(
        "kinematics", std::vector<ProjectTreeNode>{unnamedNode}));
    ASSERT_TRUE(m_model->rebuild().ok);
    m_panel->refresh();

    QTreeWidgetItem* placeholder = findRowByLabel(QStringLiteral("（未命名对象）"));
    ASSERT_NE(placeholder, nullptr);  // 占位文案出现（解析失败→占位）

    // 零缓存：改写端口名称输出→refresh 后显示跟随（面板未存旧名）。
    m_resolver->names[unnamed.toCanonical()] = "求解配置 X";
    m_panel->refresh();
    EXPECT_EQ(findRowByLabel(QStringLiteral("（未命名对象）")), nullptr);
    EXPECT_NE(findRowByLabel(QStringLiteral("求解配置 X")), nullptr);
}

// =====================================================================
// ④L3 定位落点与反解失败零触碰（acceptance 4——GUI 半区）
// =====================================================================

/// UI-TRE-7（案例①）：TreeView Select Frame 反解成功→面板定位并选中
/// 对应行（L3 全链：事件→服务反解→树定位回调→Qt 行选中→选中回流
/// 服务——真实控件承载的落点证据）。
TEST_F(IndustrialProjectTreeGuiTest, UI_TRE_7_L3_ReverseSuccessLocatesAndSelectsRow)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-02", "ARC-04"}, {});
    buildPanelWithTwoDomains();
    RecordingObserver inspector;
    auto sub = m_selection->subscribe(inspector);

    m_selection->handleTreeViewFrameSelected("Frame_J1");

    // 对应行已选中（"关节 1"＝Frame_J1 反解目标）。
    QTreeWidgetItem* current = m_panelWidget->currentItem();
    ASSERT_NE(current, nullptr);
    EXPECT_EQ(current->text(0), QStringLiteral("关节 1"));
    // 选中回流服务（单一写入路径——落点行选中经 Qt 信号到达汇聚点）。
    ASSERT_EQ(m_selection->selectedObjectIds().size(), std::size_t{1});
    EXPECT_TRUE(m_selection->selectedObjectIds().front() == m_ids[0]);
    // 检查器同步收到（L1 联动随 L3 落点成立）。
    ASSERT_EQ(inspector.changes.size(), std::size_t{1});
    EXPECT_FALSE(inspector.changes.front().runtimeOnly);
}

/// UI-TRE-7（案例②③）：反解失败→面板零触碰、不产生虚假节点、业务
/// 选中不变（行数不变＝INV-B2"不产生虚假项目树节点"的呈现面证据）。
TEST_F(IndustrialProjectTreeGuiTest, UI_TRE_7_L3_ReverseFailKeepsTreeUntouched)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-02", "ARC-04"}, {});
    buildPanelWithTwoDomains();
    // 先经面板行选中建立业务选中（面板写入路径——选中行同时是呈现态）。
    QTreeWidgetItem* taskRow = findRowByLabel(QStringLiteral("任务点 A"));
    ASSERT_NE(taskRow, nullptr);
    m_panelWidget->setCurrentItem(taskRow);
    ASSERT_EQ(m_selection->selectedObjectIds().size(), std::size_t{1});
    const int rowCountBefore = [this]() {
        int count = 0;
        for (int g = 0; g < m_panelWidget->topLevelItemCount(); ++g) {
            count += m_panelWidget->topLevelItem(g)->childCount();
        }
        return count;
    }();
    const QString selectedLabelBefore =
        m_panelWidget->currentItem() != nullptr
            ? m_panelWidget->currentItem()->text(0)
            : QString();
    ASSERT_FALSE(selectedLabelBefore.isEmpty());

    // 框架自建 Frame（反解表无此项）——事件抵达服务。
    m_selection->handleTreeViewFrameSelected("Frame_HostBuilt");

    // 树零触碰：行数不变（无虚假节点）、既有选中行未移动。
    const int rowCountAfter = [this]() {
        int count = 0;
        for (int g = 0; g < m_panelWidget->topLevelItemCount(); ++g) {
            count += m_panelWidget->topLevelItem(g)->childCount();
        }
        return count;
    }();
    EXPECT_EQ(rowCountAfter, rowCountBefore);
    ASSERT_NE(m_panelWidget->currentItem(), nullptr);
    EXPECT_EQ(m_panelWidget->currentItem()->text(0), selectedLabelBefore);
    // 服务面：业务选中不变＋仅运行时暂态记录（案例②）。
    ASSERT_EQ(m_selection->selectedObjectIds().size(), std::size_t{1});
    EXPECT_TRUE(m_selection->selectedObjectIds().front() == m_ids[2]);
    EXPECT_TRUE(m_selection->hasRuntimeOnlySelection());
    EXPECT_EQ(*m_selection->runtimeOnlyObjectName(), "Frame_HostBuilt");
}

/// UI-TRE-8：装配校验（缺依赖的面板工厂拒绝——死面板构造期拦截）。
TEST_F(IndustrialProjectTreeGuiTest, UI_TRE_8_FactoryRejectsIncompleteDeps)
{
    IRD_TEST_INFO(std::vector<std::string>{"ARC-04"}, {});
    IndustrialProjectTreePanelDeps deps;
    deps.model = m_model;
    deps.selection = m_selection;
    deps.nameResolver = nullptr;  // 缺名称端口＝渲染死面
    EXPECT_THROW(
        [&] {
            auto panel = sdurws::ird::ui::createIndustrialProjectTreePanel(
                deps, nullptr);
            (void)panel;
        }(),
        std::invalid_argument);
}

// =====================================================================
// UI-T26——项目树检索（搜索＋分组过滤：呈现层过滤四态）
// =====================================================================

/**
 * UI-T26 检索四态（契约 acceptance 2）：①搜索命中（"关节"→建模两行可
 * 见、需求行隐藏、无命中组组头隐藏）；②搜索无命中组隐藏组头；③分组
 * 过滤（只看需求对象组——建模组整组隐藏）；④清空回退全量可见。过滤
 * 只隐藏行：组数/组序/选中语义零触碰（检索后 locateAndHighlight 仍可
 * 定位隐藏行＝过滤不改变业务语义的边界证据）。
 */
TEST_F(IndustrialProjectTreeGuiTest, SearchAndGroupFilter_UI_T26)
{
    IRD_TEST_INFO("UX-02", {}, std::nullopt);
    buildPanelWithTwoDomains();

    QWidget* root = m_panel->widget();
    auto* search = root->findChild<QLineEdit*>(QStringLiteral("ird_tree_search"));
    auto* filter =
            root->findChild<QComboBox*>(QStringLiteral("ird_tree_group_filter"));
    ASSERT_NE(search, nullptr) << "搜索框缺失（UI-T26 检索行）";
    ASSERT_NE(filter, nullptr) << "分组过滤下拉缺失";
    ASSERT_EQ(filter->count(), 6) << "下拉项数＝全部＋五分组";

    QTreeWidgetItem* joint1 = findRowByLabel(QStringLiteral("关节 1"));
    QTreeWidgetItem* joint2 = findRowByLabel(QStringLiteral("关节 2"));
    QTreeWidgetItem* taskA = findRowByLabel(QStringLiteral("任务点 A"));
    ASSERT_NE(joint1, nullptr);
    ASSERT_NE(joint2, nullptr);
    ASSERT_NE(taskA, nullptr);
    // 无过滤基线：全部可见。
    EXPECT_FALSE(joint1->isHidden());
    EXPECT_FALSE(taskA->isHidden());

    // ①搜索"关节"：建模两行命中可见、需求行隐藏；需求组组头隐藏
    //   （搜索态无命中组不呈现空组）。
    search->setText(QStringLiteral("关节"));
    EXPECT_FALSE(joint1->isHidden());
    EXPECT_FALSE(joint2->isHidden());
    EXPECT_TRUE(taskA->isHidden());
    QTreeWidgetItem* requirementGroup = taskA->parent();
    ASSERT_NE(requirementGroup, nullptr);
    EXPECT_TRUE(requirementGroup->isHidden()) << "无命中组组头应隐藏";

    // ②搜索无命中（全部隐藏）。
    search->setText(QStringLiteral("不存在的对象"));
    EXPECT_TRUE(joint1->isHidden());
    EXPECT_TRUE(joint2->isHidden());

    // ③分组过滤：清空搜索＋只看需求对象组——建模组整组隐藏（组头＋
    //   子行），需求行恢复可见。
    search->clear();
    const int requirementIndex = filter->findText(QStringLiteral("需求对象"));
    ASSERT_GE(requirementIndex, 1);
    filter->setCurrentIndex(requirementIndex);
    QTreeWidgetItem* modelingGroup = joint1->parent();
    ASSERT_NE(modelingGroup, nullptr);
    EXPECT_TRUE(modelingGroup->isHidden()) << "被滤分组组头应隐藏";
    EXPECT_TRUE(joint1->isHidden());
    EXPECT_FALSE(taskA->isHidden());

    // ④清空回退：全部分组＋空搜索＝全量可见（INV-B1 无过滤态基线）。
    filter->setCurrentIndex(0);
    EXPECT_FALSE(joint1->isHidden());
    EXPECT_FALSE(joint2->isHidden());
    EXPECT_FALSE(taskA->isHidden());
    EXPECT_FALSE(modelingGroup->isHidden());

    // 过滤不改变业务语义：搜索态下 L3 定位隐藏行仍成功（隐藏≠移除）。
    search->setText(QStringLiteral("任务点"));
    const bool located =
            m_panel->locateAndHighlight(m_ids.at(2));
    search->clear();
    EXPECT_TRUE(located) << "检索过滤不得影响 L3 定位语义";
}

}  // namespace
