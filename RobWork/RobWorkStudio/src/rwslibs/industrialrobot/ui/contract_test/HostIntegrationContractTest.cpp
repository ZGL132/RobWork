/**
 * @file   HostIntegrationContractTest.cpp
 * @brief  宿主三域集成契约测试（UI-T23）——draft.apply 多模块遍历、
 *         三域共享面集成回归（L1/L2/L3 联动）、单域失败隔离状态面、
 *         项目关闭清理组件级核查（QCoreApplication 级模型层——GUI 呈现
 *         面由 gui_test/HostIntegrationGuiTest 与 GUI 冒烟承载）。
 *
 * 设计依据：
 *   - 任务契约 tasks/foundation/UI-T23.json acceptance 1~5（三域集成
 *     回归/动态模块覆盖/失败隔离/关闭清理/INV 与联动全量核查）；
 *   - units/ui.md §11.2（IPluginUiModule 三方法）、§11.3（失败隔离——
 *     UI-PLUGIN-ASSEMBLY-FAILED）、§8.5（应用与无草稿二态）；
 *   - B1-SPEC §3.3（INV-B1~B4）、§4.2（联动契约 L1~L3＋反例）、§5.1
 *     （三域 Provider 接入面）；
 *   - 先例：WP-14-T10/WP-15-T18 的 HostMigrationTest（QCoreApplication
 *     级模型层＋域 harness --auto 承载 GUI 呈现面）同构形态。
 *
 * 测试纪律（AGENTS §2.7）：用例名与断言处中文说明对应的需求/验收条目；
 * 替身实现（FakeDomainModule/测试 NameMap/测试高亮出口）随用例注明
 * 承载面——产品代码零触碰。
 */

#include <gtest/gtest.h>

#include <QCoreApplication>

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求追溯登记（ASM-UI 用例起接入）

#include <algorithm>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <sdurws/ird/core/Identity.hpp>            // core::ObjectId::generate 等（身份值面）
#include <sdurws/ird/project/CommandService.hpp>   // project::CommandEnvelope/CommandResult（提交面值）
#include <sdurws/ird/ui/IPluginUiModule.hpp>       // ui::IPluginUiModule（测试注册模块实现的接口）
#include <sdurws/ird/ui/IPluginUiRegistrar.hpp>    // ui::createPluginUiRegistrar/RegistrationOutcome（ASM-UI 注册快照钉扎）
#include <sdurws/ird/ui/IndustrialProjectTree.hpp> // 树模型/SelectionService（共享面集成）
#include <sdurws/ird/ui/PropertyInspector.hpp>     // 检查器模型（L1 呈现末端）
#include <sdurws/ird/ui/UiProjections.hpp>         // CommandResultProjection（回执投影值）

// 被测编排（ui/plugin 层——测试目标直编源码：CMake target_sources 同源
// 编入，与 modeling_test 先例〔plugin 源入测试目标〕同型；头经 ui 目录
// 根 include 面（CMake PRIVATE——同单元装配面头，不外溢消费者）解析）。
#include "plugin/DomainModuleRunner.hpp"

// 三域装配门面（O-31 装配层特权边的测试面消费——R-2 门面纪律：仅公共
// 门面头，零域私有头）。
#include <sdurws/ird/modeling/ModelingPluginAssembly.hpp>
#include <sdurws/ird/requirements/RequirementsPluginAssembly.hpp>
#include <sdurws/ird/kinematics/KinematicsPluginAssembly.hpp>

namespace {

using namespace sdurws::ird;

// =====================================================================
// 测试替身（契约 acceptance 2"含一个测试注册模块"的承载面）
// =====================================================================

/**
 * @brief 测试注册模块（IPluginUiModule 假实现——draft.apply 遍历的受控
 *        域模块：信封可注入、域键可限定、组装可注入异常）。
 */
class FakeDomainModule final : public ui::IPluginUiModule {
public:
    void onShellReady(ui::IWorkbenchShell&) override {}

    std::vector<ui::DomainReadinessItem> readonlyProjections() const override
    {
        return {};  // 遍历路径不消费投影——恒空集合法
    }

    std::optional<project::CommandEnvelope> buildDraftCommand(
        const std::string& moduleId) override
    {
        ++buildCalls;
        if (throwOnBuild) {
            throw std::runtime_error("fake build failure（隔离用例注入）");
        }
        // 域键限定：非目标域请求如实 nullopt（§11.2 域外语义——遍历对
        // 每模块以其 moduleId 征询）。
        if (!respondsToModule.empty() && moduleId != respondsToModule) {
            return std::nullopt;
        }
        return nextEnvelope;
    }

    std::optional<project::CommandEnvelope> nextEnvelope;  ///< 受控信封（nullopt＝无草稿）
    std::string respondsToModule;                          ///< 域键限定（空＝全域应答）
    bool throwOnBuild = false;                             ///< 组装异常注入（隔离用例）
    int buildCalls = 0;                                    ///< 征询计数（遍历完整性观测）
};

/**
 * @brief 测试名称映射端口（IUiRuntimeNameMapPort 假实现——L2/L3 联动
 *        用例的双向受控映射；产品端口注入面见 SelectionService::Deps）。
 */
class FakeNameMapPort final : public ui::IUiRuntimeNameMapPort {
public:
    std::optional<core::ObjectId> resolveObjectIdFromRuntimeName(
        const std::string& runtimeName) const override
    {
        const auto it = byName.find(runtimeName);
        return it != byName.end() ? std::optional<core::ObjectId>(it->second)
                                  : std::nullopt;
    }

    std::optional<std::string> resolveRuntimeName(
        const core::ObjectId& id) const override
    {
        const auto it = byId.find(id);
        return it != byId.end() ? std::optional<std::string>(it->second)
                                : std::nullopt;
    }

    std::map<std::string, core::ObjectId> byName;  ///< 反解向（运行时名→身份）
    std::map<core::ObjectId, std::string> byId;    ///< 正向向（身份→运行时名）
};

/**
 * @brief 测试高亮出口（IUiHighlightOutlet 假实现——L2 动作出线的观测
 *        面：记录高亮/清除调用序，供正反例断言）。
 */
class FakeHighlightOutlet final : public ui::IUiHighlightOutlet {
public:
    void highlightRuntimeObject(const std::string& runtimeName) override
    {
        highlighted.push_back(runtimeName);
    }
    void clearHighlight() override { clearCalls++; }

    std::vector<std::string> highlighted;  ///< 高亮动作记录（序＝调用序）
    int clearCalls = 0;                    ///< 清除动作计数
};

/**
 * @brief 真树定位替身（SelectionService Deps.treeLocator 的受控实现——
 *        L3 成功分支的"树确实定位"返回值面；**回流语义**：服务的成功
 *        分支不直接写选中——选中经"面板 Qt 信号回流 selectBusiness"单一
 *        写入路径（SelectionService.cpp 分支①注释原文），本替身在命中时
 *        模拟该回流（真实形态＝locateAndHighlight 的 Qt 选中信号），
 *        服务层断言才能观测到 L3 落点选中的完整链路；面板层落点由
 *        gui_test 承载）。
 */
struct FakeTreeLocator {
    bool locate(const core::ObjectId& id)
    {
        ++callCount;
        if (hit && service != nullptr) {
            service->selectBusiness({id}, ui::SelectionSource::ProjectTree);
        }
        return hit;
    }
    bool hit = false;                      ///< 受控命中位（L3 分支④的未命中形态注入）
    int callCount = 0;                     ///< 调用计数（L3 失败分支"树定位不调用"的断言面）
    ui::SelectionService* service = nullptr;  ///< 回流目标（真实形态＝面板选中信号；非 owning）
};

// =====================================================================
// draft.apply 多模块遍历（acceptance 2——动态模块覆盖）
// =====================================================================

/// @brief 测试夹具：QCoreApplication 级（模型层——域 harness HostMigration
///        Test 先例同构；零 Widget 构造）。
class HostIntegrationContractTest : public ::testing::Test {
protected:
    /// 信封工厂（测试模块返回值——token 为替身面，替身 submit 不校验）。
    static project::CommandEnvelope makeEnvelope()
    {
        project::CommandEnvelope envelope;
        envelope.commandType = "fake.apply";       // 替身 token（非注册表真值——不经真 store）
        envelope.payloadFormatVersion = 1;
        envelope.payloadCanonical = {0x01};        // 非空负载（域 canonical 字节——不解析）
        return envelope;
    }

    /// Committed 结果工厂（替身 submit 返回值——newRevision 生成合法身份）。
    static project::CommandResult makeCommitted()
    {
        project::CommandResult result;
        result.status.kind = project::CommandStatus::Kind::Committed;
        result.newRevision = core::RevisionId::generate();
        return result;
    }
};

/// acceptance 2：无草稿模块返回空——遍历记 NoDraft 零提交（不产生空修订）。
TEST_F(HostIntegrationContractTest, RunDomainApply_SkipsNoDraftModules_AndCounts)
{
    FakeDomainModule emptyModule;  // nextEnvelope=nullopt＝无草稿域（kinematics 同形态）
    std::vector<ui::DomainModuleEntry> entries;
    ui::DomainModuleEntry entry;
    entry.moduleId = "kinematics";
    entry.module = &emptyModule;
    entries.push_back(entry);

    int submitCalls = 0;
    const ui::DomainApplyReport report = ui::runDomainApply(
        entries, std::nullopt,
        [&submitCalls](project::CommandEnvelope, project::ICommandInteraction*) {
            ++submitCalls;
            return project::CommandResult{};
        },
        nullptr);

    ASSERT_EQ(report.entries.size(), std::size_t{1});
    EXPECT_EQ(report.entries.front().outcome,
              ui::DomainApplyEntryReport::Outcome::NoDraft);
    EXPECT_EQ(report.noDraftCount(), std::size_t{1});
    EXPECT_EQ(report.submittedCount(), std::size_t{0});
    EXPECT_FALSE(report.anyCommitted());
    EXPECT_EQ(submitCalls, 0);   // 无草稿＝零提交（§8.5 不产生空修订）
    EXPECT_EQ(emptyModule.buildCalls, 1);  // 每模块恰征询一次（遍历完整性）
}

/// acceptance 2：有草稿模块返回领域命令——提交 Committed＋锚绑定/锚前移/
/// 回执投影回写全链（闭包序＝锚同步先于组装）。
TEST_F(HostIntegrationContractTest, RunDomainApply_SubmitsDraftModule_FullChain)
{
    FakeDomainModule module;
    module.nextEnvelope = makeEnvelope();
    int anchorCalls = 0;
    int committedCalls = 0;
    std::optional<ui::CommandResultProjection> lastProjection;
    std::vector<ui::DomainModuleEntry> entries;
    ui::DomainModuleEntry entry;
    entry.moduleId = "modeling";
    entry.module = &module;
    entry.bindAnchor =
        [&anchorCalls](const core::BranchId&, const core::RevisionId&) {
            ++anchorCalls;  // 锚同步（组装前——T03b-2c 纪律的遍历推广）
        };
    entry.onCommitted =
        [&committedCalls](const project::CommandResult& result) {
            EXPECT_TRUE(result.committed());  // 域回执处理只对 Committed 调用
            ++committedCalls;
        };
    entry.onResult =
        [&lastProjection](const ui::CommandResultProjection& projection) {
            lastProjection = projection;  // DraftController 回执投影回写面
        };
    entries.push_back(entry);

    const core::BranchId branch = core::BranchId::generate();
    const core::RevisionId tip = core::RevisionId::generate();
    const ui::DomainApplyReport report = ui::runDomainApply(
        entries, std::make_pair(branch, tip),
        [this](project::CommandEnvelope envelope, project::ICommandInteraction*) {
            // 提交函数收到的信封＝模块组装产物（遍历零改写）。
            EXPECT_EQ(envelope.commandType, "fake.apply");
            return makeCommitted();
        },
        nullptr);

    EXPECT_EQ(anchorCalls, 1);  // 锚同步恰一次（先于 buildDraftCommand）
    ASSERT_EQ(report.entries.size(), std::size_t{1});
    EXPECT_TRUE(report.entries.front().committed);
    EXPECT_FALSE(report.entries.front().revision.empty());  // 新修订 canonical 非空
    EXPECT_EQ(committedCalls, 1);
    ASSERT_TRUE(lastProjection.has_value());
    EXPECT_EQ(lastProjection->status,
              ui::CommandResultProjection::Status::Committed);
    EXPECT_TRUE(report.anyCommitted());
}

/// acceptance 2：多域组合——真实建模门面（种子草稿信封非空）＋测试注册
/// 模块（有草稿）＋恒空模块（无草稿）同轮遍历，域间零互知。
TEST_F(HostIntegrationContractTest, RunDomainApply_MultiDomainCombination_WithTestModule)
{
    // 真实建模门面（模板种子→信封非空——域侧真实组装面）。
    modeling::ModelingPluginAssembly modelingDomain =
        modeling::createModelingPluginAssembly();
    modelingDomain.seedTemplateSession();

    // 测试注册模块（acceptance 2 明示承载——有草稿可应用）与恒空模块
    // （无草稿域形态）：三者同置于用例顶层作用域——遍历同步完成，指针
    // 存活期覆盖（Runner 只存裸指针，调用方保证存活期）。
    FakeDomainModule testModule;
    testModule.nextEnvelope = makeEnvelope();
    FakeDomainModule emptyModule;  // nextEnvelope=nullopt＝无草稿域

    std::vector<ui::DomainModuleEntry> entries;
    {
        ui::DomainModuleEntry modelingEntry;
        modelingEntry.moduleId = modeling::kModuleHandle;
        modelingEntry.module = modelingDomain.module.get();
        entries.push_back(modelingEntry);  // 建模：真实信封（无锚闭包——跳过锚同步）
    }
    {
        ui::DomainModuleEntry testEntry;
        testEntry.moduleId = "test-domain";
        testEntry.module = &testModule;
        entries.push_back(testEntry);      // 测试域：受控信封
    }
    {
        ui::DomainModuleEntry emptyEntry;
        emptyEntry.moduleId = "kinematics";
        emptyEntry.module = &emptyModule;
        entries.push_back(emptyEntry);     // 恒空域：NoDraft 路径
    }

    // 替身提交：全部提交 Committed（焦点＝遍历编排与多域组合，非对端）。
    const ui::DomainApplyReport report = ui::runDomainApply(
        entries, std::nullopt,
        [this](project::CommandEnvelope, project::ICommandInteraction*) {
            return makeCommitted();
        },
        nullptr);

    // 逐域恰一行（遍历序＝登记序）：建模域种子后零编辑＝NoDraft 诚实二态
    // （域语义：buildDraftCommand 以编辑记录非空为组装前置——模板种子只
    // 载入草稿不产生编辑，如实 nullopt；测试域受控信封→提交；恒空域
    // NoDraft）。多域组合断言＝每域恰一行＋域间零互知＋提交/跳过分域正确。
    ASSERT_EQ(report.entries.size(), std::size_t{3});
    EXPECT_EQ(report.entries[0].outcome,
              ui::DomainApplyEntryReport::Outcome::NoDraft);  // 建模：零编辑诚实二态
    EXPECT_TRUE(report.entries[1].committed);  // 测试域受控信封→提交
    EXPECT_EQ(report.entries[2].outcome,
              ui::DomainApplyEntryReport::Outcome::NoDraft);  // 恒空域无草稿
    EXPECT_EQ(report.committedCount(), std::size_t{1});
    EXPECT_EQ(report.noDraftCount(), std::size_t{2});
}

/// acceptance 2/3：单域组装异常被隔离——该域按提交失败承载，其余域照常
/// （§11.3 失败隔离在应用编排面的延伸）。
TEST_F(HostIntegrationContractTest, RunDomainApply_DomainExceptionIsolated_OthersProceed)
{
    FakeDomainModule brokenModule;
    brokenModule.throwOnBuild = true;  // 注入组装异常
    FakeDomainModule healthyModule;
    healthyModule.nextEnvelope = makeEnvelope();

    std::vector<ui::DomainModuleEntry> entries;
    ui::DomainModuleEntry brokenEntry;
    brokenEntry.moduleId = "broken";
    brokenEntry.module = &brokenModule;
    entries.push_back(brokenEntry);
    ui::DomainModuleEntry healthyEntry;
    healthyEntry.moduleId = "healthy";
    healthyEntry.module = &healthyModule;
    entries.push_back(healthyEntry);

    const ui::DomainApplyReport report = ui::runDomainApply(
        entries, std::nullopt,
        [this](project::CommandEnvelope, project::ICommandInteraction*) {
            return makeCommitted();
        },
        nullptr);

    ASSERT_EQ(report.entries.size(), std::size_t{2});
    EXPECT_FALSE(report.entries[0].committed);  // 异常域＝失败承载（不传染）
    EXPECT_NE(report.entries[0].rejectionReason.find("build-draft-command-failed"),
              std::string::npos);
    EXPECT_TRUE(report.entries[1].committed);   // 其余域照常提交
}

// =====================================================================
// 三域共享面集成回归（acceptance 1/5——L1/L2/L3 联动＋INV 边界）
// =====================================================================

/// acceptance 1：三域门面的迁移接入面注册进共享树/检查器——重建成功且
/// 建模组有种子节点（域数据经共享面可达；INV-B1 类型面＝树节点值类型
/// 无运行时结构字段——ProjectTreeNode 零 Frame/Joint 概念）。
TEST_F(HostIntegrationContractTest, ThreeDomainSharedSurface_TreeRebuildWithDomainNodes)
{
    modeling::ModelingPluginAssembly modelingDomain =
        modeling::createModelingPluginAssembly();
    modelingDomain.seedTemplateSession();
    requirements::RequirementsPluginAssembly requirementsDomain =
        requirements::createRequirementsPluginAssembly();
    requirementsDomain.attachEditor(nullptr);  // 显式无会话（诚实二态）
    kinematics::KinematicsPluginAssembly kinematicsDomain =
        kinematics::createKinematicsPluginAssembly();

    ui::ProjectTreeModel treeModel;
    treeModel.addProvider(modelingDomain.sharedSurfaceProviders().treeNodes);
    treeModel.addProvider(requirementsDomain.sharedSurfaceProviders().treeNodes);
    treeModel.addProvider(kinematicsDomain.sharedSurfaceProviders().treeNodes);
    EXPECT_EQ(treeModel.providerCount(), std::size_t{3});

    const ui::TreeRebuildReport rebuild = treeModel.rebuild();
    ASSERT_TRUE(rebuild.ok) << rebuild.reason;
    EXPECT_GT(treeModel.nodeCount(), std::size_t{0});  // 建模种子对象入树
    // 建模组节点非空（种子域数据经共享面供给——集成回归的域可达性）。
    EXPECT_FALSE(
        treeModel.nodesInGroup(ui::ProjectTreeGroup::ModelingObjects).empty());
    // 需求/运动学组：无会话/恒空集二态如实（不伪造域数据——WP-14-T10/
    // WP-15-T18 诚实边界）。
    EXPECT_TRUE(
        treeModel.nodesInGroup(ui::ProjectTreeGroup::RequirementObjects).empty());
}

/// acceptance 1（L1 基线）：树选建模对象→选择服务广播→检查器应答常用
/// 字段页（域 PropertyPagesProvider 消费同一选中——端到端链路的模型层）。
TEST_F(HostIntegrationContractTest, L1_TreeSelectToInspector_E2EPerDomain)
{
    modeling::ModelingPluginAssembly modelingDomain =
        modeling::createModelingPluginAssembly();
    modelingDomain.seedTemplateSession();

    ui::ProjectTreeModel treeModel;
    treeModel.addProvider(modelingDomain.sharedSurfaceProviders().treeNodes);
    ASSERT_TRUE(treeModel.rebuild().ok);

    FakeNameMapPort nameMap;
    FakeTreeLocator locator;
    ui::SelectionService selection(ui::SelectionService::Deps{
        std::make_shared<FakeNameMapPort>(),
        [&locator](const core::ObjectId& oid) { return locator.locate(oid); },
        nullptr,  // 高亮出口可空（本用例聚焦 L1——L2 见下用例）
        nullptr});

    ui::PropertyInspectorModel inspectorModel;
    inspectorModel.addProvider(
        modelingDomain.sharedSurfaceProviders().propertyPages);

    // 树选建模组首节点（模拟项目树选中——L1 基线的写入面）。
    const auto modelingIds =
        treeModel.nodesInGroup(ui::ProjectTreeGroup::ModelingObjects);
    ASSERT_FALSE(modelingIds.empty());
    selection.selectBusiness({modelingIds.front()},
                             ui::SelectionSource::ProjectTree);
    // 检查器按选中重询问（装配层编排形——harness 同款刷新序）。
    ui::SelectionChange current;
    current.selectedObjectIds = selection.selectedObjectIds();
    current.source = ui::SelectionSource::ProjectTree;
    inspectorModel.onSelectionChanged(current);
    // 建模域应答常用字段页（对象应答态——L1 的呈现末端）。
    EXPECT_EQ(inspectorModel.view().kind,
              ui::InspectorContentKind::ObjectFields);
    EXPECT_FALSE(inspectorModel.view().fields.empty());
}

/// acceptance 1/5（L2 正反例＋L3 双分支）：已应用对象高亮/未应用草稿不
/// 高亮；TreeView 反解成功定位/失败树不动＋runtimeOnly 暂态（测试端口
/// 注入的受控映射——产品空映射二态的对面验证）。
TEST_F(HostIntegrationContractTest, L2L3_LinkageContract_FullCases)
{
    FakeNameMapPort nameMap;
    const core::ObjectId applied = core::ObjectId::generate();
    const core::ObjectId draftObj = core::ObjectId::generate();
    nameMap.byId[applied] = "TP-P1";      // 已应用对象（正向存在）
    nameMap.byName["TP-P1"] = applied;
    // draftObj 无映射＝纯草稿/纯结果对象（L2 反例分支）。

    auto outlet = std::make_shared<FakeHighlightOutlet>();
    FakeTreeLocator locator;
    locator.hit = true;  // L3 成功分支的树定位命中
    ui::SelectionService selection(ui::SelectionService::Deps{
        std::make_shared<FakeNameMapPort>(nameMap),
        [&locator](const core::ObjectId& oid) { return locator.locate(oid); },
        outlet, nullptr});
    locator.service = &selection;  // 回流接线（真实形态＝面板选中信号）

    // L2 正例：选中已应用对象→高亮动作出线（运行时名定位）。
    selection.selectBusiness({applied}, ui::SelectionSource::ProjectTree);
    ASSERT_EQ(outlet->highlighted.size(), std::size_t{1});
    EXPECT_EQ(outlet->highlighted.back(), "TP-P1");

    // L2 反例：选中未应用草稿对象→无三维动作不报错（多选保守清除）。
    selection.selectBusiness({draftObj}, ui::SelectionSource::ProjectTree);
    EXPECT_EQ(outlet->highlighted.size(), std::size_t{1});  // 无新增高亮
    EXPECT_EQ(outlet->clearCalls, 1);  // 切换到无对应物对象→对称清除

    // L3 成功分支：TreeView Select Frame 反解成功→树定位回调调用＋以
    // ProjectTree 来源写入业务选中（落点语义——B1-SPEC §4.2）。
    locator.hit = true;
    selection.clearSelection(ui::SelectionSource::Command);
    selection.handleTreeViewFrameSelected("TP-P1");
    ASSERT_EQ(selection.selectedObjectIds().size(), std::size_t{1});
    EXPECT_EQ(selection.selectedObjectIds().front(), applied);
    EXPECT_EQ(selection.selectionSource(), ui::SelectionSource::ProjectTree);

    // L3 失败分支：反解失败→树定位回调不调用（callCount 零增——分支②）、
    // 不报错、记录仅运行时对象选择态（runtimeOnly 暂态）、业务选中不变。
    selection.selectBusiness({draftObj}, ui::SelectionSource::Command);
    const int locatorCallsBefore = locator.callCount;
    locator.hit = false;  // 未命中形态（树内容漂移——分支④）
    selection.handleTreeViewFrameSelected("World.UnknownFrame");
    EXPECT_EQ(locator.callCount, locatorCallsBefore);  // 反解失败＝树零触碰
    // 业务选中不变＋runtimeOnly 暂态在位（分支②/③的结构承载）。
    EXPECT_EQ(selection.selectedObjectIds().size(), std::size_t{1});
    EXPECT_TRUE(selection.hasRuntimeOnlySelection());
    EXPECT_EQ(selection.runtimeOnlyObjectName().value_or(""), "World.UnknownFrame");
}

/// acceptance 3：单域装配失败的状态面——失败域不注册 Provider（其分组
/// 零节点供给）、其余域照常注册（域键查重语义不受缺席影响）；占位呈现
/// 的渲染半区由 gui_test 承载。
TEST_F(HostIntegrationContractTest, DomainAssemblyFailure_IsolationStateSurface)
{
    // 正常域（建模）＋缺席域（需求——装配失败形态的 Provider 缺席）。
    modeling::ModelingPluginAssembly modelingDomain =
        modeling::createModelingPluginAssembly();
    modelingDomain.seedTemplateSession();

    ui::ProjectTreeModel treeModel;
    treeModel.addProvider(modelingDomain.sharedSurfaceProviders().treeNodes);
    // 需求域缺席：不注册任何 Provider（§11.3——失败域不参与共享面）。
    // 重建照常成功（缺席域＝分组空，不是违约）。
    const ui::TreeRebuildReport rebuild = treeModel.rebuild();
    ASSERT_TRUE(rebuild.ok) << rebuild.reason;
    EXPECT_FALSE(
        treeModel.nodesInGroup(ui::ProjectTreeGroup::ModelingObjects).empty());
    EXPECT_TRUE(
        treeModel.nodesInGroup(ui::ProjectTreeGroup::RequirementObjects).empty());
    // 运动学域（v1 恒空集供给——协议"可空＝合法常态"）注册不违约：
    kinematics::KinematicsPluginAssembly kinematicsDomain =
        kinematics::createKinematicsPluginAssembly();
    treeModel.addProvider(kinematicsDomain.sharedSurfaceProviders().treeNodes);
    EXPECT_TRUE(treeModel.rebuild().ok);  // 恒空集 Provider 共存零干扰
}

/// acceptance 4：项目关闭清理组件级核查——选中清空/高亮对称清除/检查器
/// 空选占位/订阅释放后广播不达（八类清理编排的组件级等价断言；宿主侧
/// teardown 由 GUI 冒烟步 6 承载）。
TEST_F(HostIntegrationContractTest, ProjectCloseTeardown_ComponentLevelAssertions)
{
    modeling::ModelingPluginAssembly modelingDomain =
        modeling::createModelingPluginAssembly();
    modelingDomain.seedTemplateSession();

    auto nameMap = std::make_shared<FakeNameMapPort>();
    auto outlet = std::make_shared<FakeHighlightOutlet>();
    FakeTreeLocator locator;
    ui::SelectionService selection(ui::SelectionService::Deps{
        nameMap,
        [&locator](const core::ObjectId& oid) { return locator.locate(oid); },
        outlet, nullptr});
    ui::PropertyInspectorModel inspectorModel;
    inspectorModel.addProvider(
        modelingDomain.sharedSurfaceProviders().propertyPages);
    auto subscription = selection.subscribe(inspectorModel);

    // 打开态基线：有选中＋检查器应答。
    ui::ProjectTreeModel treeModel;
    treeModel.addProvider(modelingDomain.sharedSurfaceProviders().treeNodes);
    ASSERT_TRUE(treeModel.rebuild().ok);
    const auto ids =
        treeModel.nodesInGroup(ui::ProjectTreeGroup::ModelingObjects);
    ASSERT_FALSE(ids.empty());
    selection.selectBusiness({ids.front()}, ui::SelectionSource::ProjectTree);
    ui::SelectionChange current;
    current.selectedObjectIds = selection.selectedObjectIds();
    inspectorModel.onSelectionChanged(current);
    ASSERT_EQ(inspectorModel.view().kind,
              ui::InspectorContentKind::ObjectFields);

    // 关闭清理（组件级等价序——teardownSharedSurfacesForClose 的编排面）：
    // ⑧订阅释放→②选中清空＋高亮清除→③检查器空选占位。
    subscription.reset();  // 运行中订阅释放（先断消费链）
    const int highlightClearsBeforeTeardown = outlet->clearCalls;
    selection.clearSelection(ui::SelectionSource::Command);
    outlet->clearHighlight();
    ui::SelectionChange cleared;
    inspectorModel.onSelectionChanged(cleared);  // 空选中询问→占位态

    EXPECT_TRUE(selection.selectedObjectIds().empty());   // ②选中归零
    EXPECT_FALSE(selection.selectionSource().has_value());
    // 高亮对称清除（清理段发生过清除——clearSelection 的清空路径与显式
    // clearHighlight 都是对称收口动作，次数取"不低于清理前基线"的增量
    // 断言；基线段高亮切换的清除次数不进断言）。
    EXPECT_GT(outlet->clearCalls, highlightClearsBeforeTeardown);
    EXPECT_EQ(inspectorModel.view().kind,
              ui::InspectorContentKind::NoSelection);     // ③检查器占位
    // ⑧退订后广播不达（释放句柄的 RAII 语义）。
    const auto viewBefore = inspectorModel.view();
    selection.selectBusiness({ids.front()}, ui::SelectionSource::Command);
    EXPECT_EQ(inspectorModel.view().kind, viewBefore.kind);  // 无刷新（未订阅）
}

// =====================================================================
// 宿主注册端口装配序列（ASM-UI 收口批——注册快照/描述符断言）
// =====================================================================

/**
 * ASM-UI 验收项②（asm-plug 建议级 2 承接）：宿主注册端口（真实
 * IPluginUiRegistrar——§10.9）的装配序列语义钉扎：
 *   1. 白名单八 token 全在册（§11.1 词表——六业务域〔modeling/requirements/
 *      kinematics/dynamics/selection/optimization〕＋trajectory/workflow
 *      占位；六域注册的词表承载面）；
 *   2. 三旧域门面经宿主同机制（registerPluginUi(descriptor, module)——
 *      UI-T23 装配机制）注册返回 Ok，装配报告快照逐条对位（pluginId/
 *      ok/panelsLoaded/commandsRegistered 与门面描述符一致——注册快照/
 *      描述符断言）；
 *   3. 重复注册被宿主权威拒绝（DuplicatePlugin——§7.2 冲突规则；快照
 *      不增长不覆盖）；
 *   4. 未注册域不入报告（§11.3 缺位占位语义——快照只含已 Ok 域）。
 *
 * 测试面链接边约束（诚实登记，units/ui.md §13 ASM-UI 行）：本目标已登记
 * 的跨单元测试边仅覆盖三旧域 plugin（IRD_TEST_TARGET_EDGES——UI-T23 三
 * 条）；三新域（dynamics/selection/optimization）门面的真实注册激活钉
 * 扎在三域各自 contract_test（ASM-PLUG 批 ActivationPassesRealRegistrar
 * Port 系用例——真实 registrar 传经 registerWithHostRegistrar 激活路径
 * ＋selection InvalidDescriptor 诚实拒绝断言〔P-SEL-3 不本地绕过〕），
 * ui_plugin 目标自身的六域装配承接（ExtraDomainAssembly TU）为集成树
 * MODULE 面，无人值守测试目标不可链接（CMake 禁链接 MODULE）——运行验
 * 证通道＝开发期插件实机装载与 GUI 冒烟（ui.md §12.2 通道，不在本门禁）。
 */
TEST_F(HostIntegrationContractTest, HostRegistrarAssemblySequence_Snapshot_ASMUI_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"SA-01", "UX-14", "ARC-04"},
                  std::vector<std::string>{});

    // ①真实注册端口（§10.9 工厂——白名单由本测试充当装配层固定，与
    //   DomainAssembly.cpp kPluginWhitelist 同一 §11.1 词表字面）。
    auto registrar = ui::createPluginUiRegistrar(
        {"modeling", "requirements", "kinematics", "trajectory",
         "dynamics", "selection", "optimization", "workflow"});
    ASSERT_NE(registrar, nullptr) << "注册端口工厂返回空（ui 库装配违约）";

    // 白名单承载断言：六业务域 token 全在册（六域注册的词表面——ASM-UI
    // 三新域承接的宿主词表前提）＋恰八条（SA-01 静态白名单）。
    const std::vector<std::string> whitelist = registrar->whitelist();
    ASSERT_EQ(whitelist.size(), std::size_t{8});
    for (const char* const token :
         {"modeling", "requirements", "kinematics", "dynamics",
          "selection", "optimization"}) {
        EXPECT_NE(std::find(whitelist.begin(), whitelist.end(),
                            std::string(token)),
                  whitelist.end())
            << "白名单缺少业务域 token: " << token;
    }

    // ②三旧域门面经宿主同机制注册（真实门面符号——已登记测试边）。
    modeling::ModelingPluginAssembly modelingDomain =
        modeling::createModelingPluginAssembly();
    requirements::RequirementsPluginAssembly requirementsDomain =
        requirements::createRequirementsPluginAssembly();
    requirementsDomain.attachEditor(nullptr);  // 显式无会话（诚实二态）
    kinematics::KinematicsPluginAssembly kinematicsDomain =
        kinematics::createKinematicsPluginAssembly();

    EXPECT_EQ(registrar->registerPluginUi(modelingDomain.descriptor,
                                          *modelingDomain.module),
              ui::RegistrationOutcome::Ok);
    EXPECT_EQ(registrar->registerPluginUi(requirementsDomain.descriptor,
                                          *requirementsDomain.module),
              ui::RegistrationOutcome::Ok);
    EXPECT_EQ(registrar->registerPluginUi(kinematicsDomain.descriptor,
                                          *kinematicsDomain.module),
              ui::RegistrationOutcome::Ok);

    // ③注册快照逐条对位（§10.9 后置条件——Ok 才入列；panelsLoaded/
    //    commandsRegistered 与描述符一致＝描述符断言的快照面）。
    const std::vector<ui::PluginAssemblyReport> reports =
        registrar->assemblyReports();
    ASSERT_EQ(reports.size(), std::size_t{3})
        << "装配报告恰三条（三域 Ok 各一；未注册域不入列——§11.3 缺位）";
    const ui::PluginAssemblyReport* reportOf[3] = {
        &reports[0], &reports[1], &reports[2]};
    const std::string expectedIds[3] = {"modeling", "requirements",
                                        "kinematics"};
    const std::size_t expectedPanels[3] = {
        modelingDomain.descriptor.panels.size(),
        requirementsDomain.descriptor.panels.size(),
        kinematicsDomain.descriptor.panels.size()};
    const std::size_t expectedCommands[3] = {
        modelingDomain.descriptor.commands.size(),
        requirementsDomain.descriptor.commands.size(),
        kinematicsDomain.descriptor.commands.size()};
    for (std::size_t i = 0; i < 3; ++i) {
        EXPECT_EQ(reportOf[i]->pluginId, expectedIds[i]) << "第 " << i << " 行";
        EXPECT_TRUE(reportOf[i]->ok) << expectedIds[i] << " 应 ok";
        EXPECT_EQ(reportOf[i]->panelsLoaded, expectedPanels[i])
            << expectedIds[i] << " 面板计数与描述符一致";
        EXPECT_EQ(reportOf[i]->commandsRegistered, expectedCommands[i])
            << expectedIds[i] << " 命令计数与描述符一致";
        EXPECT_TRUE(reportOf[i]->failureDiagnostics.empty())
            << expectedIds[i] << " 无失败诊断";
    }

    // ④重复注册被宿主权威拒绝（§7.2 冲突规则——不覆盖不静默）。
    EXPECT_EQ(registrar->registerPluginUi(modelingDomain.descriptor,
                                          *modelingDomain.module),
              ui::RegistrationOutcome::DuplicatePlugin);
    EXPECT_EQ(registrar->assemblyReports().size(), std::size_t{3})
        << "拒绝注册不增长快照（静态白名单无运行期改写）";

    // ⑤未注册域缺席（§11.3 缺位＝占位呈现的数据面——快照只含已 Ok 域；
    //   ASM-UI 三新域在 ui_plugin 装配面的承接由 ExtraDomainAssembly TU
    //   承载，其快照语义即本用例钉扎的同机制延伸）。
    for (const ui::PluginAssemblyReport& report : reports) {
        EXPECT_NE(report.pluginId, "dynamics");
        EXPECT_NE(report.pluginId, "selection");
        EXPECT_NE(report.pluginId, "optimization");
    }
}

}  // namespace
