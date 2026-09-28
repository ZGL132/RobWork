/**
 * @file   IndustrialProjectTreeModelTest.cpp
 * @brief  工业项目树模型（ProjectTreeModel——UI-T21，方案 B.1 §3.1/§5.1）
 *         模型层用例（QCoreApplication 级零 Widget——§12.1 第一层分工）：
 *         ①五分组封闭清单与固定序（INV-B1 分组半区——第六分组在类型面
 *         不可表达）；②节点身份一律 ObjectId 与 Provider 注册协议（域键
 *         查重/空指针拒绝——协议形状冻结面）；③重建边界整体校验（无效
 *         身份/跨域撞号/悬空子引用/深度超限四拒绝词表——拒绝优于残缺）；
 *         ④定位与查询（L3 落点数据 (group,row,parent)——acceptance 3/4
 *         载体）；⑤稳定序与零名称缓存的结构承载。
 *
 * 设计依据：
 *   - 任务契约 tasks/foundation/UI-T21.json acceptance 1/3/5；
 *   - B1-SPEC §3.1（五分组封闭清单/ObjectId 身份/零第二套命名缓存）、
 *     §3.3（INV-B1 项目树零运行时结构节点）、§5.1（TreeNodesProvider
 *     协议——本任务冻结形状）、§4.2 L3（树定位落点）；
 *   - units/ui.md §13 UI-T21 行、§3.1（替身承载惯例）；
 *   - 先例：HostControllerModelTest/SelectionServiceTest 的独立替身声明
 *     形态（不跨 TU 共享私有件）。
 *
 * 为什么放在模型层：模型是零 Qt 纯数据结构（契约头线程约束行）——重建
 *   校验/序/定位可在无事件循环环境穷举；名称解析与选中联动的呈现半区由
 *   IndustrialProjectTreeGuiTest 以真实控件承载。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>
#include <sdurws/ird/ui/IndustrialProjectTree.hpp>

#include <algorithm>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace {

using namespace sdurws::ird;
using sdurws::ird::ui::IUiTreeNodesProvider;
using sdurws::ird::ui::ProjectTreeGroup;
using sdurws::ird::ui::ProjectTreeNode;
using sdurws::ird::ui::ProjectTreeModel;
using sdurws::ird::ui::TreeLocation;
using sdurws::ird::ui::TreeRebuildReport;
using sdurws::ird::ui::kProjectTreeGroupCount;
using core::ObjectId;

// =====================================================================
// 测试替身与辅助（各 TU 独立声明——SelectionServiceTest 同款纪律）
// =====================================================================

/// 确定性身份派生（固定种子摘要前 16 字节——可复现优先）。
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

/// 固定供给替身（treeNodes 返回构造时给定的集合——域侧状态自持的最小
/// 镜像；rebuild 现调语义由"每次返回同集"承载）。
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

/// 可重配供给替身（重建拒绝用例——先给合法集建立基线，再改违约集触发
/// 拒绝路径，断言旧内容保持）。
class MutableProvider final : public IUiTreeNodesProvider {
public:
    explicit MutableProvider(std::string key) : m_key(std::move(key)) {}

    std::string domainKey() const override { return m_key; }
    std::vector<ProjectTreeNode> treeNodes() const override { return m_nodes; }

    std::string m_key;                     ///< 域注册键（构造时固定）
    std::vector<ProjectTreeNode> m_nodes;  ///< 测试用例直接改写
};

/// 造一个有效节点（ObjectId 由种子派生——分组/深度/子引用按需给）。
static ProjectTreeNode makeNode(const std::string& seed, ProjectTreeGroup group,
                                std::vector<ObjectId> children = {},
                                std::uint8_t depth = 0)
{
    ProjectTreeNode node;
    node.objectId = idFrom<ObjectId>("tre-test-" + seed);
    node.group = group;
    node.childObjectIds = std::move(children);
    node.depth = depth;
    return node;
}

// =====================================================================
// ①五分组封闭清单（INV-B1 分组半区——acceptance 1/5）
// =====================================================================

/// UI-TRE-1：分组词表封闭（五值——kProjectTreeGroupCount 编译期钉住；
/// 第六分组的引入必须走 B1-SPEC 增量修订，词表常量是评审锚点）。
TEST(IndustrialProjectTreeModelTest, UI_TRE_1_FiveGroupClosure)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-02", "ARC-04"}, {});
    static_assert(kProjectTreeGroupCount == 5,
                  "B1-SPEC §3.1 五分组封闭——扩值须规格增量修订（评审锚点）");
    // 枚举值域＝0..4（呈现序固定——与 Provider 注册序无关的封闭清单）。
    EXPECT_EQ(projectTreeGroupIndex(ProjectTreeGroup::ProjectStructure), 0);
    EXPECT_EQ(projectTreeGroupIndex(ProjectTreeGroup::ModelingObjects), 1);
    EXPECT_EQ(projectTreeGroupIndex(ProjectTreeGroup::RequirementObjects), 2);
    EXPECT_EQ(projectTreeGroupIndex(ProjectTreeGroup::AnalysisConfigurations), 3);
    EXPECT_EQ(projectTreeGroupIndex(ProjectTreeGroup::ResultsEvidenceReports), 4);
}

/// UI-TRE-1：空模型五组查询（空内容下各分组查询合法且为空——分组是
/// 结构不是内容，封闭清单不随数据存在性漂移）。
TEST(IndustrialProjectTreeModelTest, UI_TRE_1_EmptyModelQueriesByGroup)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-02"}, {});
    ProjectTreeModel model;
    for (std::uint8_t g = 0; g < kProjectTreeGroupCount; ++g) {
        const auto ids = model.nodesInGroup(static_cast<ProjectTreeGroup>(g));
        EXPECT_TRUE(ids.empty()) << "group index " << static_cast<int>(g);
    }
    EXPECT_EQ(model.nodeCount(), std::size_t{0});
}

// =====================================================================
// ②Provider 注册协议（B1-SPEC §5.1 冻结形状——acceptance 1）
// =====================================================================

/// UI-TRE-2：注册协议契约面（空指针拒绝/域键查重/计数自证——同域双
/// 供给源的合并语义未定义，登记边界拒绝）。
TEST(IndustrialProjectTreeModelTest, UI_TRE_2_ProviderRegistrationProtocol)
{
    IRD_TEST_INFO(std::vector<std::string>{"ARC-04"}, {});
    ProjectTreeModel model;

    // 空指针＝装配缺陷（重建时无法现调）——fail-fast。
    EXPECT_THROW(model.addProvider(nullptr), std::invalid_argument);

    model.addProvider(std::make_shared<StubProvider>(
        "modeling", std::vector<ProjectTreeNode>{}));
    EXPECT_EQ(model.providerCount(), std::size_t{1});

    // 同域键重复注册＝装配缺陷——拒绝（谁先谁后都是私裁）。
    EXPECT_THROW(model.addProvider(std::make_shared<StubProvider>(
                     "modeling", std::vector<ProjectTreeNode>{})),
                 std::invalid_argument);
    EXPECT_EQ(model.providerCount(), std::size_t{1});  // 拒绝后计数不变

    // 不同域键正常登记（三域迁移的注册面——B1-SPEC §5.1）。
    model.addProvider(std::make_shared<StubProvider>(
        "requirements", std::vector<ProjectTreeNode>{}));
    EXPECT_EQ(model.providerCount(), std::size_t{2});
}

// =====================================================================
// ③重建边界整体校验（四拒绝词表——拒绝优于残缺）
// =====================================================================

/// UI-TRE-3：合法重建（跨域节点入各自分组、注册序稳定、计数一致——
/// acceptance 1 的正路径）。
TEST(IndustrialProjectTreeModelTest, UI_TRE_3_RebuildAcrossGroupsStableOrder)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-02", "ARC-04"}, {});
    ProjectTreeModel model;
    // 建模域两个对象（注册序：mdl-a 先于 mdl-b）。
    const ProjectTreeNode a = makeNode("mdl-a", ProjectTreeGroup::ModelingObjects);
    const ProjectTreeNode b = makeNode("mdl-b", ProjectTreeGroup::ModelingObjects);
    // 需求域一个对象（分组独立——五分组承载域的呈现归属）。
    const ProjectTreeNode r = makeNode("req-1", ProjectTreeGroup::RequirementObjects);
    model.addProvider(std::make_shared<StubProvider>(
        "modeling", std::vector<ProjectTreeNode>{a, b}));
    model.addProvider(std::make_shared<StubProvider>(
        "requirements", std::vector<ProjectTreeNode>{r}));

    const TreeRebuildReport report = model.rebuild();

    ASSERT_TRUE(report.ok);
    EXPECT_EQ(report.nodeCount, std::size_t{3});
    EXPECT_EQ(model.nodeCount(), std::size_t{3});
    // 组内序＝注册序→供给序（NFR-COR-02 稳定序——呈现与定位共用）。
    const auto modeling = model.nodesInGroup(ProjectTreeGroup::ModelingObjects);
    ASSERT_EQ(modeling.size(), std::size_t{2});
    EXPECT_TRUE(modeling[0] == a.objectId);
    EXPECT_TRUE(modeling[1] == b.objectId);
    const auto requirements =
        model.nodesInGroup(ProjectTreeGroup::RequirementObjects);
    ASSERT_EQ(requirements.size(), std::size_t{1});
    EXPECT_TRUE(requirements[0] == r.objectId);
    // 其余分组为空（五分组封闭——未供给域不产生占位节点）。
    EXPECT_TRUE(model.nodesInGroup(ProjectTreeGroup::ProjectStructure).empty());
    EXPECT_TRUE(
        model.nodesInGroup(ProjectTreeGroup::AnalysisConfigurations).empty());
    EXPECT_TRUE(model.nodesInGroup(ProjectTreeGroup::ResultsEvidenceReports)
                    .empty());
}

/// UI-TRE-3：重建确定性（同供给集两次重建——枚举逐位一致；NFR-COR-02
/// 的重建半区自证）。
TEST(IndustrialProjectTreeModelTest, UI_TRE_3_RebuildDeterministic)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-02"}, {});
    ProjectTreeModel model;
    const ProjectTreeNode a = makeNode("mdl-a", ProjectTreeGroup::ModelingObjects);
    const ProjectTreeNode b = makeNode("mdl-b", ProjectTreeGroup::ModelingObjects);
    model.addProvider(std::make_shared<StubProvider>(
        "modeling", std::vector<ProjectTreeNode>{a, b}));

    // 首轮重建建立基线，再重建一次对照（同供给集两次重建——枚举逐位
    // 一致；NFR-COR-02 的重建半区自证）。
    const TreeRebuildReport firstReport = model.rebuild();
    ASSERT_TRUE(firstReport.ok);
    const auto first = model.nodesInGroup(ProjectTreeGroup::ModelingObjects);
    const TreeRebuildReport secondReport = model.rebuild();
    const auto second = model.nodesInGroup(ProjectTreeGroup::ModelingObjects);

    ASSERT_TRUE(secondReport.ok);
    ASSERT_EQ(first.size(), second.size());
    for (std::size_t i = 0; i < first.size(); ++i) {
        EXPECT_TRUE(first[i] == second[i]) << "row " << i;
    }
}

/// UI-TRE-3（拒绝①）：无效 ObjectId（全零保留值）→整体拒绝且旧内容
/// 保持（INV-B1 的重建边界强制——运行时结构对象没有业务身份，伪造在
/// 此被拦截；导航呈现不允许半新半旧）。
TEST(IndustrialProjectTreeModelTest, UI_TRE_3_RebuildRejectInvalidObjectId)
{
    IRD_TEST_INFO(std::vector<std::string>{"ARC-04"}, {});
    ProjectTreeModel model;
    const ProjectTreeNode good = makeNode("mdl-good", ProjectTreeGroup::ModelingObjects);
    model.addProvider(std::make_shared<StubProvider>(
        "modeling", std::vector<ProjectTreeNode>{good}));
    ASSERT_TRUE(model.rebuild().ok);

    auto mutableProvider = std::make_shared<MutableProvider>("requirements");
    ProjectTreeNode invalid = makeNode("req-x", ProjectTreeGroup::RequirementObjects);
    invalid.objectId = ObjectId{};  // 全零保留值＝伪造身份
    mutableProvider->m_nodes.push_back(invalid);
    model.addProvider(mutableProvider);

    const TreeRebuildReport report = model.rebuild();

    EXPECT_FALSE(report.ok);
    EXPECT_EQ(report.reason, "invalid-object-id");
    // 旧内容保持（拒绝优于残缺——建模域节点原样可查）。
    EXPECT_EQ(model.nodeCount(), std::size_t{1});
    ASSERT_TRUE(model.node(good.objectId).has_value());
}

/// UI-TRE-3（拒绝②）：跨 Provider 撞号→整体拒绝（CON-01 唯一性——
/// 同一对象两次供给＝域侧装配缺陷）。
TEST(IndustrialProjectTreeModelTest, UI_TRE_3_RebuildRejectDuplicateObjectId)
{
    IRD_TEST_INFO(std::vector<std::string>{"ARC-04"}, {});
    ProjectTreeModel model;
    const ProjectTreeNode shared = makeNode("mdl-shared", ProjectTreeGroup::ModelingObjects);
    model.addProvider(std::make_shared<StubProvider>(
        "modeling", std::vector<ProjectTreeNode>{shared}));
    model.addProvider(std::make_shared<StubProvider>(
        "requirements",
        std::vector<ProjectTreeNode>{
            makeNode("req-shared-dup", ProjectTreeGroup::RequirementObjects)}));
    // 第二供给者改供与第一供给者同一 ObjectId（撞号场景）。
    const auto dupProvider = std::make_shared<MutableProvider>("kinematics");
    ProjectTreeNode dup = makeNode("kin-x", ProjectTreeGroup::AnalysisConfigurations);
    dup.objectId = shared.objectId;
    dupProvider->m_nodes.push_back(dup);
    model.addProvider(dupProvider);

    const TreeRebuildReport report = model.rebuild();

    EXPECT_FALSE(report.ok);
    EXPECT_EQ(report.reason, "duplicate-object-id");
    // 拒绝后保持重建前内容（本用例首次重建即违约——保持空集，无半更新）。
    EXPECT_EQ(model.nodeCount(), std::size_t{0});
}

/// UI-TRE-3（拒绝③）：悬空子引用→整体拒绝（childObjectIds 必须指向
/// 本轮供给集内节点——点了没反应的树行＝静默失效，拒绝优于残缺）。
TEST(IndustrialProjectTreeModelTest, UI_TRE_3_RebuildRejectDanglingChildReference)
{
    IRD_TEST_INFO(std::vector<std::string>{"ARC-04"}, {});
    ProjectTreeModel model;
    const ProjectTreeNode parent = makeNode(
        "mdl-parent", ProjectTreeGroup::ModelingObjects,
        std::vector<ObjectId>{idFrom<ObjectId>("tre-test-ghost")});  // 幽灵子
    model.addProvider(std::make_shared<StubProvider>(
        "modeling", std::vector<ProjectTreeNode>{parent}));

    const TreeRebuildReport report = model.rebuild();

    EXPECT_FALSE(report.ok);
    EXPECT_EQ(report.reason, "dangling-child-reference");
}

/// UI-TRE-3（拒绝③补）：闭合子引用合法（父→子都在本轮供给集——树内
/// 父子关系的正路径；L3 定位的 parentId 语义基础）。
TEST(IndustrialProjectTreeModelTest, UI_TRE_3_RebuildAcceptsClosedChildReference)
{
    IRD_TEST_INFO(std::vector<std::string>{"ARC-04"}, {});
    ProjectTreeModel model;
    const ProjectTreeNode parent = makeNode(
        "mdl-parent", ProjectTreeGroup::ModelingObjects,
        std::vector<ObjectId>{}, 0);
    ProjectTreeNode child = makeNode("mdl-joint", ProjectTreeGroup::ModelingObjects,
                                     std::vector<ObjectId>{}, 1);
    // 闭合：父的 childObjectIds 指向子（供给面父子即呈现父子）。
    const ProjectTreeNode parentClosed = makeNode(
        "mdl-parent", ProjectTreeGroup::ModelingObjects,
        std::vector<ObjectId>{child.objectId}, 0);
    model.addProvider(std::make_shared<StubProvider>(
        "modeling", std::vector<ProjectTreeNode>{parentClosed, child}));

    const TreeRebuildReport report = model.rebuild();

    ASSERT_TRUE(report.ok);
    EXPECT_EQ(report.nodeCount, std::size_t{2});
}

/// UI-TRE-3（拒绝④）：深度超防御上限→整体拒绝（上限语义见契约头
/// depth 注释——B1-SPEC §3.1 五分组自然深度≤5，8 为防御值）。
TEST(IndustrialProjectTreeModelTest, UI_TRE_3_RebuildRejectDepthLimitExceeded)
{
    IRD_TEST_INFO(std::vector<std::string>{"ARC-04"}, {});
    ProjectTreeModel model;
    ProjectTreeNode deep = makeNode("mdl-deep", ProjectTreeGroup::ModelingObjects,
                                    std::vector<ObjectId>{}, 9);  // >8
    model.addProvider(std::make_shared<StubProvider>(
        "modeling", std::vector<ProjectTreeNode>{deep}));

    const TreeRebuildReport report = model.rebuild();

    EXPECT_FALSE(report.ok);
    EXPECT_EQ(report.reason, "depth-limit-exceeded");
}

// =====================================================================
// ④定位与查询（L3 落点数据——acceptance 3/4 载体）
// =====================================================================

/// UI-TRE-4：定位命中（分组＋组内序位＋逻辑父三元组——面板 (group,row)
/// 展开的数据源；acceptance 3/4 的 L3 落点）。
TEST(IndustrialProjectTreeModelTest, UI_TRE_4_LocateReturnsGroupRowParent)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-02", "ARC-04"}, {});
    ProjectTreeModel model;
    const ProjectTreeNode child = makeNode("mdl-joint", ProjectTreeGroup::ModelingObjects);
    const ProjectTreeNode parent = makeNode(
        "mdl-parent", ProjectTreeGroup::ModelingObjects,
        std::vector<ObjectId>{child.objectId}, 0);
    model.addProvider(std::make_shared<StubProvider>(
        "modeling", std::vector<ProjectTreeNode>{parent, child}));
    ASSERT_TRUE(model.rebuild().ok);

    const auto location = model.locate(child.objectId);

    ASSERT_TRUE(location.has_value());
    EXPECT_EQ(location->group, ProjectTreeGroup::ModelingObjects);
    EXPECT_EQ(location->rowInGroup, std::size_t{1});  // 注册序第二位
    ASSERT_TRUE(location->parentId.has_value());
    EXPECT_TRUE(*location->parentId == parent.objectId);

    // 组直属顶层：parentId 为 nullopt。
    const auto top = model.locate(parent.objectId);
    ASSERT_TRUE(top.has_value());
    EXPECT_FALSE(top->parentId.has_value());
    EXPECT_EQ(top->rowInGroup, std::size_t{0});
}

/// UI-TRE-4：定位未命中返回 nullopt（未入树/已重建移除——调用方以返回
/// 值自证命中，未命中不伪造选中）。
TEST(IndustrialProjectTreeModelTest, UI_TRE_4_LocateMissReturnsNullopt)
{
    IRD_TEST_INFO(std::vector<std::string>{"ARC-04"}, {});
    ProjectTreeModel model;
    model.addProvider(std::make_shared<StubProvider>(
        "modeling", std::vector<ProjectTreeNode>{
                        makeNode("mdl-a", ProjectTreeGroup::ModelingObjects)}));
    ASSERT_TRUE(model.rebuild().ok);

    EXPECT_FALSE(model.locate(idFrom<ObjectId>("tre-test-not-in-tree")).has_value());
}

/// UI-TRE-4：按身份查节点值（寻址原语——不存在→nullopt 不虚构）。
TEST(IndustrialProjectTreeModelTest, UI_TRE_4_NodeLookupByIdentity)
{
    IRD_TEST_INFO(std::vector<std::string>{"ARC-04"}, {});
    ProjectTreeModel model;
    const ProjectTreeNode a = makeNode("mdl-a", ProjectTreeGroup::ModelingObjects);
    model.addProvider(std::make_shared<StubProvider>(
        "modeling", std::vector<ProjectTreeNode>{a}));
    ASSERT_TRUE(model.rebuild().ok);

    const auto found = model.node(a.objectId);
    ASSERT_TRUE(found.has_value());
    EXPECT_TRUE(found->objectId == a.objectId);
    EXPECT_EQ(found->group, ProjectTreeGroup::ModelingObjects);
    EXPECT_FALSE(model.node(idFrom<ObjectId>("tre-test-miss")).has_value());
}

// =====================================================================
// ⑤INV-B1 的结构承载面（零运行时结构节点——类型面不可表达）
// =====================================================================

/// UI-TRE-5：节点值零名称字段（零第二套命名缓存的结构承载——模型 API
/// 无任何名称出口：查询返回的是 ObjectId 序与节点值，显示名归渲染时刻
/// 的名称端口现取〔UX-02〕；本断言钉住"模型面无名称可取"）。
TEST(IndustrialProjectTreeModelTest, UI_TRE_5_ModelSurfaceCarriesNoDisplayName)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-02"}, {});
    ProjectTreeModel model;
    const ProjectTreeNode a = makeNode("mdl-a", ProjectTreeGroup::ModelingObjects);
    model.addProvider(std::make_shared<StubProvider>(
        "modeling", std::vector<ProjectTreeNode>{a}));
    ASSERT_TRUE(model.rebuild().ok);

    // nodesInGroup/node/locate 三查询的返回值类型都不含显示名（编译期
    // 事实——运行期以"返回值可解析字段无 string 名称"复核）。
    const auto ids = model.nodesInGroup(ProjectTreeGroup::ModelingObjects);
    ASSERT_EQ(ids.size(), std::size_t{1});
    static_assert(std::is_same_v<decltype(ids)::value_type, ObjectId>,
                  "分组查询返回 ObjectId 序——模型面零名称承载");
    const auto node = model.node(ids.front());
    ASSERT_TRUE(node.has_value());
    static_assert(
        std::is_same_v<decltype(node->objectId), ObjectId>,
        "节点值身份字段＝ObjectId（CON-01）——无名称字段的结构承载");
}

/// UI-TRE-5：重建整体替换（内容更新可见——rebuild 是唯一内容写点，
/// 域修订后重建即见新内容；旧对象移除后查询失效）。
TEST(IndustrialProjectTreeModelTest, UI_TRE_5_RebuildReplacesContentWholesale)
{
    IRD_TEST_INFO(std::vector<std::string>{"ARC-04"}, {});
    ProjectTreeModel model;
    auto provider = std::make_shared<MutableProvider>("modeling");
    const ProjectTreeNode a = makeNode("mdl-a", ProjectTreeGroup::ModelingObjects);
    provider->m_nodes.push_back(a);
    model.addProvider(provider);
    ASSERT_TRUE(model.rebuild().ok);
    EXPECT_EQ(model.nodeCount(), std::size_t{1});

    // 域侧修订：旧对象移除、新对象入树。
    const ProjectTreeNode b = makeNode("mdl-b", ProjectTreeGroup::ModelingObjects);
    provider->m_nodes = {b};
    ASSERT_TRUE(model.rebuild().ok);

    EXPECT_EQ(model.nodeCount(), std::size_t{1});
    EXPECT_TRUE(model.node(b.objectId).has_value());
    EXPECT_FALSE(model.node(a.objectId).has_value());  // 旧内容整体移除
}

}  // namespace
