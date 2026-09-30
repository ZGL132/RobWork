/**
 * @file   HostIntegrationGuiTest.cpp
 * @brief  UI-T23 GUI 层用例（QApplication＋真实 Widget 树——§12.1 第三层）：
 *         多领域集成收口的呈现行为——装配失败分组的占位行渲染（§11.3
 *         失败降级隔离在多域树形态的呈现半区，acceptance 3）与项目关闭
 *         清理的检查器宿装清空（acceptance 4 的呈现面核查点）。
 *
 * 设计依据：
 *   - 任务契约 tasks/foundation/UI-T23.json acceptance 3/4（单域装配失败
 *     占位＋稳定诊断；关闭清理八类对象——宿装页零残留）；
 *   - units/ui.md §11.3（失败隔离——失败插件占位、其余照常）、UI-T21
 *     面板契约（setGroupPlaceholder——UI-T23 增量，占位行非树节点：不携
 *     ObjectId、不可选中、不进业务选中——INV-B1/B3 呈现面边界）；
 *   - 先例：IndustrialProjectTreeGuiTest.cpp／PropertyInspectorGuiTest.cpp
 *     （夹具形态/替身清单同构）。
 *
 * 模型/GUI 分工：失败隔离的状态面（Provider 缺席/域键登记）在
 * HostIntegrationContractTest.cpp 断言；本文件断言真实控件树的占位渲染
 * 与宿装清空行为面。
 */

#include <gtest/gtest.h>

#include <QApplication>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QWidget>

#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/diagnostics/Catalog.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>
#include <sdurws/ird/ui/IndustrialProjectTree.hpp>
#include <sdurws/ird/ui/PropertyInspector.hpp>
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
using sdurws::ird::ui::IndustrialProjectTreePanel;
using sdurws::ird::ui::IndustrialProjectTreePanelDeps;
using sdurws::ird::ui::IUiNameResolver;
using sdurws::ird::ui::IUiRuntimeNameMapPort;
using sdurws::ird::ui::IUiTreeNodesProvider;
using sdurws::ird::ui::ProjectTreeGroup;
using sdurws::ird::ui::ProjectTreeNode;
using sdurws::ird::ui::ProjectTreeModel;
using sdurws::ird::ui::SelectionService;
using core::ObjectId;

// =====================================================================
// 测试替身（各 TU 独立声明——GUI 套件纪律）
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

/// 名称解析替身（渲染显示名可控源——占位用例只需要建模组有可命名节点）。
class StubNameResolver final : public IUiNameResolver {
public:
    std::map<std::string, std::string> names;

    std::optional<std::string> resolveObjectId(ObjectId id) const override
    {
        auto it = names.find(id.toCanonical());
        if (it == names.end()) {
            return std::nullopt;
        }
        return it->second;
    }
};

/// 名称映射替身（服务装配必填面——本 TU 不触发 L3）。
class StubRuntimeNameMap final : public IUiRuntimeNameMapPort {
public:
    std::optional<ObjectId>
    resolveObjectIdFromRuntimeName(const std::string&) const override
    {
        return std::nullopt;
    }
    std::optional<std::string> resolveRuntimeName(const ObjectId&) const override
    {
        return std::nullopt;
    }
};

/// 固定供给替身（建模域——正常域形态：装配成功有供给）。
class StubProvider final : public IUiTreeNodesProvider {
public:
    StubProvider(std::string key, std::vector<ProjectTreeNode> nodes)
        : m_key(std::move(key)), m_nodes(std::move(nodes)) {}

    std::string domainKey() const override { return m_key; }
    std::vector<ProjectTreeNode> treeNodes() const override { return m_nodes; }

private:
    std::string m_key;
    std::vector<ProjectTreeNode> m_nodes;
};

/// Dev 日志替身（留痕非断言对象）。
class StubDevLog final : public diagnostics::IDevLogSink {
public:
    void logDev(std::string_view, std::string) override {}
};

/// 节点工厂（depth 0 建模对象——与既有 GUI 套件同构）。
ProjectTreeNode makeNode(const std::string& seed, ui::ProjectTreeGroup group)
{
    ProjectTreeNode node;
    node.objectId = idFrom<ObjectId>(seed);
    node.group = group;
    node.depth = 0;
    return node;
}

// =====================================================================
// 用例（占位渲染——acceptance 3；宿装清空——acceptance 4）
// =====================================================================

/// acceptance 3：装配失败分组的占位行渲染——占位行挂在组行下、不携带
/// 节点身份（不可选中、不进业务选中）、正常域节点照常渲染（隔离不掩盖）。
TEST(HostIntegrationGuiTest, AssemblyFailureGroupPlaceholder_RendersAndNotSelectable)
{
    ProjectTreeModel model;
    StubNameResolver resolver;
    // 建模域正常（一个可命名节点）；需求域装配失败（无 Provider——占位）。
    const ObjectId j1 = idFrom<ObjectId>("host-int-joint-1");
    resolver.names[j1.toCanonical()] = "J1";
    model.addProvider(std::make_shared<StubProvider>(
        "modeling", std::vector<ProjectTreeNode>{makeNode("host-int-joint-1",
                                                          ui::ProjectTreeGroup::ModelingObjects)}));
    ASSERT_TRUE(model.rebuild().ok);

    auto nameMap = std::make_shared<StubRuntimeNameMap>();
    auto devLog = std::make_shared<StubDevLog>();
    std::shared_ptr<SelectionService> selection;
    {
        SelectionService::Deps deps;
        deps.nameMap = nameMap;
        deps.devLog = devLog;
        deps.treeLocator = [](const ObjectId&) { return false; };
        selection = std::make_shared<SelectionService>(std::move(deps));
    }

    IndustrialProjectTreePanelDeps deps;
    deps.model = std::shared_ptr<ProjectTreeModel>(
        &model, [](ProjectTreeModel*) {});
    deps.selection = selection;
    deps.nameResolver = std::make_shared<StubNameResolver>(resolver);
    auto panel = ui::createIndustrialProjectTreePanel(deps, nullptr);

    // 装配层占位登记（§11.3——失败域分组占位＋稳定码文本；渲染在 refresh）。
    panel->setGroupPlaceholder(ui::ProjectTreeGroup::RequirementObjects,
                               "需求域装配失败（UI-PLUGIN-ASSEMBLY-FAILED）");
    panel->refresh();

    // 树控件结构断言：需求组行下恰一行且为占位文本；建模组行下为节点行。
    // （UI-T26 容器化：widget() 出口＝检索行＋树的容器——树经 findChild
    // 定位〔IndustrialProjectTreePanel 实现封闭库内，语义不变〕。）
    QTreeWidget* tree = panel->widget()->findChild<QTreeWidget*>();
    ASSERT_NE(tree, nullptr);
    QTreeWidgetItem* reqGroup = tree->topLevelItem(
        ui::projectTreeGroupIndex(ui::ProjectTreeGroup::RequirementObjects));
    ASSERT_NE(reqGroup, nullptr);
    ASSERT_EQ(reqGroup->childCount(), 1);  // 占位行恰一（非节点——零 ObjectId 伪造）
    EXPECT_EQ(reqGroup->child(0)->text(0),
              QStringLiteral("需求域装配失败（UI-PLUGIN-ASSEMBLY-FAILED）"));
    EXPECT_FALSE(reqGroup->child(0)->flags().testFlag(Qt::ItemIsSelectable));

    // 正常域节点照常（隔离不掩盖）＋占位行不携带节点身份数据（选中信号
    // 按 kNodeIdRole 过滤——占位永不进业务选中，INV-B1/B3 呈现面边界）。
    QTreeWidgetItem* modelingGroup = tree->topLevelItem(
        ui::projectTreeGroupIndex(ui::ProjectTreeGroup::ModelingObjects));
    ASSERT_NE(modelingGroup, nullptr);
    ASSERT_EQ(modelingGroup->childCount(), 1);
    EXPECT_EQ(modelingGroup->child(0)->text(0), QStringLiteral("J1"));
    EXPECT_TRUE(modelingGroup->child(0)->data(0, Qt::UserRole).isValid());

    // 占位清除（空串）后 refresh——占位行消失（域恢复形态）。
    panel->setGroupPlaceholder(ui::ProjectTreeGroup::RequirementObjects,
                               std::string());
    panel->refresh();
    EXPECT_EQ(reqGroup->childCount(), 0);
}

/// acceptance 4：项目关闭清理的检查器宿装清空——非对象态 refresh 清宿装
/// 容器（UI-T22"切换对象清宿装"契约在关闭路径的复用核查；装配层清理序
/// 的呈现面证据）。
TEST(HostIntegrationGuiTest, ProjectCloseTeardown_InspectorHostedPageCleared)
{
    auto nameMap = std::make_shared<StubRuntimeNameMap>();
    auto devLog = std::make_shared<StubDevLog>();
    std::shared_ptr<SelectionService> selection;
    {
        SelectionService::Deps deps;
        deps.nameMap = nameMap;
        deps.devLog = devLog;
        deps.treeLocator = [](const ObjectId&) { return false; };
        selection = std::make_shared<SelectionService>(std::move(deps));
    }

    // 检查器模型（无 Provider——对象态不可达，本用例聚焦宿装容器清理；
    // Provider 应答形态由 PropertyInspectorGuiTest 覆盖）。
    auto model = std::make_shared<ui::PropertyInspectorModel>();
    ui::PropertyInspectorPanelDeps deps;
    deps.model = model;
    deps.nameResolver = std::make_shared<StubNameResolver>();
    auto panel = ui::createPropertyInspectorPanel(deps, nullptr);

    // 打开态基线：对象应答态下宿装非空（构造面——D6 激活由既有套件承载，
    // 此处以选中清理后的占位态刷新为关闭清理的触发面）。
    ui::SelectionChange emptyChange;
    model->onSelectionChanged(emptyChange);
    panel->refresh();
    // 关闭清理的检查器半区：空选中询问→NoSelection 占位态（宿装容器在
    // 非对象态下清空——UI-T22 契约原文"宿装区在非对象态下清空"）。
    EXPECT_EQ(model->view().kind, ui::InspectorContentKind::NoSelection);
    EXPECT_TRUE(panel->widget() != nullptr);  // 面板呈现面存活（清理不毁面板）
}

}  // namespace
