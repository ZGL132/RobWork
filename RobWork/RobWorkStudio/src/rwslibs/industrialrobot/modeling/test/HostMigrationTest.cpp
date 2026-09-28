/**
 * @file   HostMigrationTest.cpp
 * @brief  宿主迁移三接入面测试（模型层——QCoreApplication 级）——任务契约
 *         WP-13-T20 acceptance 1/2/3 的具名自证面。
 *
 * 设计依据：
 *   - B1-SPEC §5.1（TreeNodes/PropertyPages/SelectionAdapter 三接入面）、
 *     §5.2（迁移期双形态并存）、§5.3（共享 UI 文件互斥——本测试只消费
 *     ui 冻结协议公共头，零共享面改动）、§4.2（联动 L1~L3——L2 正向
 *     存在性→三维高亮；L3 反解→树定位）；
 *   - ui 注册协议（UI-T21/T22 冻结形状）：IndustrialProjectTree.hpp
 *     （ProjectTreeModel 重建/校验/定位）、PropertyInspector.hpp
 *     （PropertyInspectorModel first-wins 询问/分野哨兵/D6 激活编排）、
 *     SelectionService.hpp（selectBusiness 唯一写入口/L2 高亮判定/
 *     handleTreeViewFrameSelected 反解编排）、FormEditCommon.hpp
 *     （IFormEditOutlet 编辑移交面）；
 *   - 需求 MDL-07（参数化编辑界面的宿主形态迁移）；任务契约 acceptance
 *     2 迁移链路（树导航→检查器常用字段→复杂编辑页→三维高亮端到端）
 *     ——L2/L3 在建模对象上的可演示性由本端到端用例承载（GUI 呈现面
 *     按 V-30 惯例登记 GUI 冒烟留痕，本目标不启动 GUI）。
 *
 * 测试范围声明：三接入面为模型层（零 Qt 控件）——树面板/检查器面板的
 *   Qt 渲染半区由 ui 单元自身 GUI 测试覆盖（UI-T21/T22 交付面）；本文件
 *   验证"域供给值 → 共享模型 → 联动编排"的全链语义（九例，无条件编入
 *   ——冒烟可用；模块接线面一例见 HostMigrationModuleTest.cpp——集成
 *   门控 TU，policy 符号依据）。AC 4 的构建/门禁/留痕项在任务验证流程
 *   执行（ird_gates＋双模式构建＋traceability/builds/wp13-t20/），不在
 *   本测试文件内。
 */

#include <gtest/gtest.h>

#include <QCoreApplication>

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯登记（testkit §7.3）

#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <sdurws/ird/modeling/Template.hpp>        // RobotDesignTemplateFactory/createDraft（六轴草稿夹具）
#include "plugin/HostMigrationProviders.hpp"       // 被测三接入面（插件私有头——同单元可含）
#include "plugin/PanelEditFlow.hpp"                // IPanelEditSink（编辑分流替身接口）
#include <sdurws/ird/ui/FormEditCommon.hpp>        // ui::ParamEditSet/IFormEditOutlet（编辑移交面）
#include <sdurws/ird/ui/IndustrialProjectTree.hpp> // ui::ProjectTreeModel（共享树模型）
#include <sdurws/ird/ui/PropertyInspector.hpp>     // ui::PropertyInspectorModel（共享检查器模型）
#include <sdurws/ird/ui/SelectionService.hpp>      // ui::SelectionService（选择唯一汇聚点）

// ---- 使用声明（与被测头同一命名空间——用例可读性）--------------------
using namespace sdurws::ird;
using namespace sdurws::ird::modeling;

namespace {

/// generic-6r 草稿的便捷创建（PluginPanelTest 同款——成功前置起步夹具）。
ModelingWorkingSet makeSixAxisDraft()
{
    const RobotDesignTemplateFactory factory;
    std::vector<core::DiagnosticRecord> diags;
    const TemplateOutcome outcome = factory.createDraft(
        TemplateId{kTemplateIdGeneric6R},
        runtime::InstallationPresetToken::Ground, "demo", diags);
    return outcome.get();  // 成功前置——失败即测试自身装配错误（logic_error）
}

/// 确定性占位锚（闭包外对象/根身份注入用——非全零、字节可控）。
core::ObjectId fixedOid(std::uint8_t tag)
{
    core::ObjectId oid;
    oid.bytes[0] = tag;
    return oid;
}

/**
 * @brief 编辑流记录替身（IPanelEditSink——三路回调捕获面，PluginPanelTest
 *        同款；本测试另用它验证出口转译的分流落点）。
 */
struct RecordingEditSink final : IPanelEditSink {
    std::vector<std::string> appliedPaths;  ///< onEditApplied 捕获
    std::vector<EditRejection> rejections;  ///< onEditRejected 捕获
    int dirtyCount = 0;                     ///< notifySessionDirty 计数

    void onEditApplied(const std::string& subjectPath) override
    {
        appliedPaths.push_back(subjectPath);
    }
    void notifySessionDirty() override { ++dirtyCount; }
    void onEditRejected(const EditRejection& rejection) override
    {
        rejections.push_back(rejection);
    }
};

/**
 * @brief 面板高亮执行器记录替身（Deps.panelHighlight——nullopt＝清除）。
 */
struct RecordingHighlight {
    std::vector<std::optional<core::ObjectId>> calls;
    std::function<void(const std::optional<core::ObjectId>&)> sink()
    {
        return [this](const std::optional<core::ObjectId>& oid) {
            calls.push_back(oid);
        };
    }
};

/**
 * @brief D6 激活执行器记录替身（Deps.complexPageActivator）。
 */
struct RecordingActivator {
    std::vector<core::ObjectId> calls;
    std::function<void(const core::ObjectId&)> sink()
    {
        return [this](const core::ObjectId& oid) { calls.push_back(oid); };
    }
};

/**
 * @brief 装配依赖构造（三接入面共用——ws/editSink/两执行器一次给全）。
 */
ModelingSharedSurfaceDeps makeDeps(ModelingWorkingSet* ws,
                                   IPanelEditSink* sink,
                                   RecordingHighlight& highlight,
                                   RecordingActivator& activator)
{
    ModelingSharedSurfaceDeps deps;
    deps.workingSet = [ws]() -> ModelingWorkingSet* { return ws; };
    deps.editSink = sink;
    deps.panelHighlight = highlight.sink();
    deps.complexPageActivator = activator.sink();
    return deps;
}

/**
 * @brief 运行时名称映射替身（L5 适配层角色——SA-05 双射的最小演示实现；
 *        真实映射归 runtime RuntimeNameMap，测试只验证服务编排语义）。
 */
struct StubNameMap final : ui::IUiRuntimeNameMapPort {
    std::map<std::string, core::ObjectId> byName;   ///< 反解向（运行时名→ObjectId）
    std::map<core::ObjectId, std::string> byId;     ///< 正向向（ObjectId→运行时名）

    std::optional<core::ObjectId>
    resolveObjectIdFromRuntimeName(const std::string& runtimeName) const override
    {
        const auto it = byName.find(runtimeName);
        return it != byName.end() ? std::optional<core::ObjectId>{it->second}
                                  : std::nullopt;
    }
    std::optional<std::string>
    resolveRuntimeName(const core::ObjectId& id) const override
    {
        const auto it = byId.find(id);
        return it != byId.end() ? std::optional<std::string>{it->second}
                                : std::nullopt;
    }
};

/**
 * @brief 三维高亮出口记录替身（L2 动作出线的捕获面——"高亮可演示"的
 *        观测点：记录每次高亮/清除调用）。
 */
struct RecordingHighlightOutlet final : ui::IUiHighlightOutlet {
    std::vector<std::string> highlighted;  ///< highlightRuntimeObject 捕获序
    int clears = 0;                        ///< clearHighlight 计数

    void highlightRuntimeObject(const std::string& runtimeName) override
    {
        highlighted.push_back(runtimeName);
    }
    void clearHighlight() override { ++clears; }
};

/// 关节/连杆的运行时名填充（"J1..J6"/"L1..L7"——演示映射，确定性）。
void fillStubNames(StubNameMap& map, const ModelingWorkingSet& ws)
{
    for (std::size_t i = 0; i < ws.design.joints.size(); ++i) {
        const std::string name = "J" + std::to_string(i + 1);
        map.byName[name] = ws.design.joints[i].objectId;
        map.byId[ws.design.joints[i].objectId] = name;
    }
    for (std::size_t i = 0; i < ws.design.links.size(); ++i) {
        const std::string name = "L" + std::to_string(i + 1);
        map.byName[name] = ws.design.links[i].objectId;
        map.byId[ws.design.links[i].objectId] = name;
    }
}

/// 按键取基线值（字段值断言辅助——未设返回 nullopt）。
std::optional<double> baselineValue(const ui::CommonFieldsPage& page,
                                    const std::string& key)
{
    for (const ui::InspectorFieldValue& v : page.values) {
        if (v.key == key) { return v.siValue; }
    }
    return std::nullopt;
}

/// 三接入面测试环境（一个用例一套——工作集/替身/供给者生命周期绑定）。
struct MigrationFixture {
    ModelingWorkingSet ws{makeSixAxisDraft()};
    RecordingEditSink sink;
    RecordingHighlight highlight;
    RecordingActivator activator;
    ModelingSharedSurfaceDeps deps{makeDeps(&ws, &sink, highlight, activator)};
    std::shared_ptr<ModelingTreeNodesProvider> tree{
        std::make_shared<ModelingTreeNodesProvider>(deps)};
    std::shared_ptr<ModelingPropertyPagesProvider> pages{
        std::make_shared<ModelingPropertyPagesProvider>(deps)};
};

}  // namespace

// =====================================================================
// TreeNodesProvider（接入面一——建模对象入工业项目树）
// =====================================================================

/**
 * 契约 WP-13-T20 acceptance 1：TreeNodesProvider 供给建模全域对象入
 * 工业项目树——域键、分组、子对象词表（关节/连杆链序）与根子引用闭合。
 */
TEST(HostMigrationTreeNodesProvider, SuppliesModelingObjectsWithClosedChildren)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-07"}, std::vector<std::string>{"B1-ACC-1"});
    MigrationFixture fx;

    // 域注册键＝三协议统一词表（注册边界查重依据）。
    EXPECT_EQ(fx.tree->domainKey(), "modeling");

    // 模板初始草稿根身份未回填（rootObjectId=nullopt）——子对象以组直属
    // 顶层供给（6 关节＋7 连杆＝13 节点，全部 ModelingObjects 分组）。
    const std::vector<ui::ProjectTreeNode> nodes = fx.tree->treeNodes();
    ASSERT_EQ(nodes.size(), fx.ws.design.joints.size() + fx.ws.design.links.size());
    for (const ui::ProjectTreeNode& n : nodes) {
        EXPECT_EQ(n.group, ui::ProjectTreeGroup::ModelingObjects);
        EXPECT_TRUE(n.objectId.isValid());  // 无效身份在重建边界整体拒绝——供给面不出全零
    }
    // 节点序＝投影序（先关节链后连杆链——与自持树同一份信息架构）。
    for (std::size_t i = 0; i < fx.ws.design.joints.size(); ++i) {
        EXPECT_TRUE(nodes[i].objectId == fx.ws.design.joints[i].objectId);
    }
    for (std::size_t i = 0; i < fx.ws.design.links.size(); ++i) {
        const std::size_t row = fx.ws.design.joints.size() + i;
        EXPECT_TRUE(nodes[row].objectId == fx.ws.design.links[i].objectId);
    }

    // 根身份回填后：根节点在首位、子引用闭合（全部子对象入根的
    // childObjectIds）、子对象深度降一级（面板缩进提示）。
    fx.ws.rootObjectId = fixedOid(0xEE);
    const std::vector<ui::ProjectTreeNode> rooted = fx.tree->treeNodes();
    ASSERT_EQ(rooted.size(), nodes.size() + 1);
    EXPECT_TRUE(rooted.front().objectId == *fx.ws.rootObjectId);
    EXPECT_TRUE(rooted.front().childObjectIds.empty()
                || rooted.front().childObjectIds.size() == nodes.size());
    ASSERT_EQ(rooted.front().childObjectIds.size(), nodes.size());
    EXPECT_EQ(rooted.front().depth, 0);
    for (std::size_t i = 1; i < rooted.size(); ++i) {
        EXPECT_EQ(rooted[i].depth, 1);
        EXPECT_TRUE(rooted.front().childObjectIds[i - 1] == rooted[i].objectId);
    }

    // 无会话（工作集出口返回 null）＝空集——协议注释的合法常态。
    ModelingSharedSurfaceDeps nullDeps;
    nullDeps.workingSet = []() -> ModelingWorkingSet* { return nullptr; };
    const ModelingTreeNodesProvider nullProvider(nullDeps);
    EXPECT_TRUE(nullProvider.treeNodes().empty());
}

/**
 * 契约 WP-13-T20 acceptance 1：供给值注册进 ui::ProjectTreeModel（UI-T21
 * 冻结模型）可整体重建通过——分组/唯一性/子引用闭合/深度四查全过，
 * locate 定位命中（L3 落点的模型半区）。
 */
TEST(HostMigrationTreeNodesProvider, FeedsSharedProjectTreeModelRebuild)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-07"}, std::vector<std::string>{"B1-ACC-1"});
    MigrationFixture fx;

    ui::ProjectTreeModel model;
    model.addProvider(fx.tree);
    EXPECT_EQ(model.providerCount(), std::size_t{1});

    // 整体重建通过（四查全过——域供给值与协议校验面的对账）。
    const ui::TreeRebuildReport report = model.rebuild();
    ASSERT_TRUE(report.ok) << "reason=" << report.reason;
    EXPECT_EQ(report.nodeCount, fx.ws.design.joints.size() + fx.ws.design.links.size());
    EXPECT_EQ(model.nodesInGroup(ui::ProjectTreeGroup::ModelingObjects).size(),
              fx.ws.design.joints.size() + fx.ws.design.links.size());

    // L3 定位原语：链上任一关节可定位（分组＋组内序位）。
    const auto loc = model.locate(fx.ws.design.joints[0].objectId);
    ASSERT_TRUE(loc.has_value());
    EXPECT_EQ(loc->group, ui::ProjectTreeGroup::ModelingObjects);

    // 同域重复注册＝装配缺陷 fail-fast（协议契约——供给者侧不可绕过）。
    EXPECT_THROW(model.addProvider(fx.tree), std::invalid_argument);
}

// =====================================================================
// PropertyPagesProvider（接入面二——常用字段入共享检查器；D6 复杂页）
// =====================================================================

/**
 * 契约 WP-13-T20 acceptance 1（D5/D6 分野）：关节常用字段页＝零位＋限位
 * （少量高频编辑字段，≤哨兵上限），携带编辑移交出口；基线值与工作集
 * 同源；复杂页入口＝DH 参数（hosted=false——大批量字段收口域面板）。
 */
TEST(HostMigrationPropertyPages, JointCommonFieldsWithEditableOutlet)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-07"}, std::vector<std::string>{"B1-ACC-1"});
    MigrationFixture fx;

    EXPECT_EQ(fx.pages->domainKey(), "modeling");
    const core::ObjectId j0 = fx.ws.design.joints[0].objectId;

    const auto page = fx.pages->commonFieldsPage(j0);
    ASSERT_TRUE(page.has_value());
    EXPECT_FALSE(page->readOnly);
    EXPECT_NE(page->editOutlet, nullptr);  // 可编辑页必须携带移交出口
    EXPECT_LE(page->fields.size(), ui::kMaxCommonFieldsPerObject);  // D5/D6 分野哨兵
    ASSERT_EQ(page->fields.size(), std::size_t{3});  // 零位＋限位下/上限
    EXPECT_EQ(page->fields[0].key, "zero-offset");
    EXPECT_EQ(page->fields[1].key, "joint-lower-limit");
    EXPECT_EQ(page->fields[2].key, "joint-upper-limit");
    // 基线值与工作集同源（零位恒填；限位＝模板必填面）。
    ASSERT_TRUE(baselineValue(*page, "zero-offset").has_value());
    EXPECT_EQ(*baselineValue(*page, "zero-offset"), fx.ws.design.joints[0].zeroOffset);
    const auto bounds = fx.ws.design.joints[0].bounds.tryValue();
    ASSERT_TRUE(bounds.has_value());
    EXPECT_EQ(*baselineValue(*page, "joint-lower-limit"), bounds->first);
    EXPECT_EQ(*baselineValue(*page, "joint-upper-limit"), bounds->second);

    // D6 入口：恰一"DH 参数"入口，hosted=false（域自持打开——D6"域面板
    // 内"形态），入口零字段内容（分野的协议级承载）。
    const auto entries = fx.pages->complexPageEntries(j0);
    ASSERT_EQ(entries.size(), std::size_t{1});
    EXPECT_EQ(entries[0].pageKey, "dh-parameters");
    EXPECT_EQ(entries[0].title, std::string("DH 参数"));
    EXPECT_FALSE(entries[0].hosted);
}

/**
 * 契约 WP-13-T20 acceptance 1（D5/D6 分野）：连杆常用字段页＝只读事实
 * （质量；物性编辑的 L-8 平行轴确认流收口域面板——复杂页入口 properties，
 * 只读页不携带编辑移交出口）。
 */
TEST(HostMigrationPropertyPages, LinkReadOnlyMassWithPropertiesEntry)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-07"}, std::vector<std::string>{"B1-ACC-1"});
    MigrationFixture fx;

    const core::ObjectId l0 = fx.ws.design.links[0].objectId;
    const auto page = fx.pages->commonFieldsPage(l0);
    ASSERT_TRUE(page.has_value());
    EXPECT_TRUE(page->readOnly);           // 只读事实（P-UI-6 单侧纪律）
    EXPECT_EQ(page->editOutlet, nullptr);  // 只读页不提供移交面（保守收口）
    ASSERT_EQ(page->fields.size(), std::size_t{1});
    EXPECT_EQ(page->fields[0].key, "mass");

    const auto entries = fx.pages->complexPageEntries(l0);
    ASSERT_EQ(entries.size(), std::size_t{1});
    EXPECT_EQ(entries[0].pageKey, "properties");
    EXPECT_FALSE(entries[0].hosted);
}

/**
 * 契约 WP-13-T20 acceptance 1（域判定在域）：闭包外对象零应答
 * （commonFieldsPage=nullopt 且 complexPageEntries=空集——协议的"非本域
 * 对象"二态；检查器顺延询问下一注册者）。
 */
TEST(HostMigrationPropertyPages, NonDomainObjectGetsNoAnswer)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-07"}, std::vector<std::string>{"B1-ACC-1"});
    MigrationFixture fx;

    const core::ObjectId alien = fixedOid(0x99);
    EXPECT_FALSE(fx.pages->commonFieldsPage(alien).has_value());
    EXPECT_TRUE(fx.pages->complexPageEntries(alien).empty());
}

/**
 * 契约 WP-13-T20 acceptance 2（迁移链路——检查器编辑半区）：常用字段页
 * 出口把 ParamEditSet 转译为域编辑流（submitJointFieldEdit 域裁决唯一）
 * ——零位单值直投、限位单侧替换整对提交、域拒绝就地呈现（工作集字节
 * 不变的域内强保证）。
 */
TEST(HostMigrationPropertyPages, OutletTranslatesEditsToDomainFlow)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-07"}, std::vector<std::string>{"B1-ACC-1"});
    MigrationFixture fx;

    const core::ObjectId j0 = fx.ws.design.joints[0].objectId;
    const auto page = fx.pages->commonFieldsPage(j0);
    ASSERT_TRUE(page.has_value());
    ASSERT_NE(page->editOutlet, nullptr);

    // 零位编辑：出口→域接受（工作集更新＋变更记录追加＋sink 三路分流）。
    ui::ParamEditSet edits;
    ui::ParamChange zero;
    zero.key = "zero-offset";
    zero.label = "零位偏置";
    zero.newSi = 0.25;
    edits.changes.push_back(zero);
    const std::size_t changesBefore = fx.ws.changes.size();
    page->editOutlet->applyEdits(edits);
    EXPECT_EQ(fx.ws.design.joints[0].zeroOffset, 0.25);
    EXPECT_EQ(fx.ws.changes.size(), changesBefore + 1);  // append-only 一条
    EXPECT_EQ(fx.sink.appliedPaths.size(), std::size_t{1});
    EXPECT_EQ(fx.sink.dirtyCount, 1);

    // 限位下限编辑：单侧替换整对（当前 qmax 保持——域裁决 qmin<qmax）。
    ui::ParamEditSet limitEdits;
    ui::ParamChange lower;
    lower.key = "joint-lower-limit";
    lower.label = "限位下限";
    lower.newSi = fx.ws.design.joints[0].bounds.value().first + 0.1;
    limitEdits.changes.push_back(lower);
    const double upperBefore = fx.ws.design.joints[0].bounds.value().second;
    page->editOutlet->applyEdits(limitEdits);
    EXPECT_EQ(fx.ws.design.joints[0].bounds.value().first, lower.newSi);
    EXPECT_EQ(fx.ws.design.joints[0].bounds.value().second, upperBefore);  // 单侧语义

    // 非法限位（qmin≥qmax）＝域拒绝：工作集不变＋就地拒绝呈现。
    ui::ParamEditSet badEdits;
    ui::ParamChange bad;
    bad.key = "joint-lower-limit";
    bad.label = "限位下限";
    bad.newSi = fx.ws.design.joints[0].bounds.value().second + 1.0;
    badEdits.changes.push_back(bad);
    const auto boundsBefore = fx.ws.design.joints[0].bounds.value();
    page->editOutlet->applyEdits(badEdits);
    EXPECT_EQ(fx.ws.design.joints[0].bounds.value(), boundsBefore);  // 字节不变
    ASSERT_FALSE(fx.sink.rejections.empty());
    EXPECT_EQ(fx.sink.rejections.back().codeToken, "limit-interval-invalid");
}

/**
 * 契约 WP-13-T20 acceptance 1/2（D6 激活编排的 Provider 侧）：激活核对
 * 三分支——合法键执行域自持打开（ok＋零宿装视图）；未知键拒绝；
 * 执行器缺位诚实拒绝（不伪造打开成功）。
 */
TEST(HostMigrationPropertyPages, ComplexPageActivationThreeBranches)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-07"}, std::vector<std::string>{"B1-ACC-1"});
    MigrationFixture fx;

    const core::ObjectId j0 = fx.ws.design.joints[0].objectId;

    // ①合法激活：域自持打开执行器被调（聚焦目标对象），返回 ok＋空宿装。
    const auto ok = fx.pages->activateComplexPage(j0, "dh-parameters", nullptr);
    ASSERT_TRUE(ok.ok);
    EXPECT_TRUE(ok.reason.empty());
    EXPECT_EQ(ok.hostedWidget, nullptr);  // hosted=false 恒空——契约一致性
    ASSERT_EQ(fx.activator.calls.size(), std::size_t{1});
    EXPECT_TRUE(fx.activator.calls.back() == j0);

    // ②未知键/非页面对象：寻址拒绝（封闭词表 token）。
    const auto unknown = fx.pages->activateComplexPage(j0, "bogus-page", nullptr);
    EXPECT_FALSE(unknown.ok);
    EXPECT_EQ(unknown.reason, "activation-unknown-page");
    const auto alien = fx.pages->activateComplexPage(fixedOid(0x99), "dh-parameters", nullptr);
    EXPECT_FALSE(alien.ok);
    EXPECT_EQ(alien.reason, "activation-unknown-page");

    // ③执行器缺位＝诚实拒绝（装配层的可空性声明语义——不虚构打开）。
    RecordingHighlight unusedHighlight;
    RecordingActivator unusedActivator;
    ModelingWorkingSet ws2{makeSixAxisDraft()};
    ModelingSharedSurfaceDeps deps2{makeDeps(&ws2, nullptr, unusedHighlight, unusedActivator)};
    deps2.complexPageActivator = nullptr;  // 显式置空＝无域面板场景
    ModelingPropertyPagesProvider barePages(deps2);  // 非 const——激活是非 const 编排
    const auto unavailable = barePages.activateComplexPage(ws2.design.joints[0].objectId,
                                                           "dh-parameters", nullptr);
    EXPECT_FALSE(unavailable.ok);
    EXPECT_EQ(unavailable.reason, "domain-surface-unavailable");
}

// =====================================================================
// SelectionAdapter（接入面三——选择联动）
// =====================================================================

/**
 * 契约 WP-13-T20 acceptance 1/2（选择联动）：下行——业务单选命中建模
 * 闭包→面板高亮；多选/清空→对称清除；runtimeOnly（L3 反解失败暂态）
 * 零触碰；他域对象不动面板。上行——本域拾取经 View3DPick 汇入唯一写
 * 入口，闭包外/无效身份拒绝上报。
 */
TEST(HostMigrationSelectionAdapter, DownstreamHighlightAndUpstreamPick)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-07"}, std::vector<std::string>{"B1-ACC-1"});
    MigrationFixture fx;

    // 选择服务装配（L5 替身：名称映射＋树定位回调——高亮出口供 L2）。
    auto nameMap = std::make_shared<StubNameMap>();
    fillStubNames(*nameMap, fx.ws);
    RecordingHighlightOutlet outlet;
    ui::SelectionService::Deps serviceDeps;
    serviceDeps.nameMap = nameMap;
    serviceDeps.treeLocator = [](const core::ObjectId&) { return true; };
    serviceDeps.highlightOutlet = std::shared_ptr<ui::IUiHighlightOutlet>(
        &outlet, [](ui::IUiHighlightOutlet*) {});  // 非 owning 共享（栈对象）
    ui::SelectionService service(serviceDeps);

    ModelingSelectionAdapter adapter(fx.deps);
    adapter.attach(service);

    // 下行①：本域单选→面板高亮执行器收到该对象。
    const core::ObjectId j0 = fx.ws.design.joints[0].objectId;
    service.selectBusiness({j0}, ui::SelectionSource::ProjectTree);
    ASSERT_EQ(fx.highlight.calls.size(), std::size_t{1});
    ASSERT_TRUE(fx.highlight.calls.back().has_value());
    EXPECT_TRUE(*fx.highlight.calls.back() == j0);

    // 下行②：多选→对称清除（nullopt）。
    service.selectBusiness({j0, fx.ws.design.joints[1].objectId},
                           ui::SelectionSource::ProjectTree);
    ASSERT_EQ(fx.highlight.calls.size(), std::size_t{2});
    EXPECT_FALSE(fx.highlight.calls.back().has_value());

    // 下行③：runtimeOnly（L3 反解失败）→零触碰（既有高亮态不变）。
    const std::size_t beforeRuntimeOnly = fx.highlight.calls.size();
    service.handleTreeViewFrameSelected("World.BaseFrame-Unknown");
    EXPECT_EQ(fx.highlight.calls.size(), beforeRuntimeOnly);

    // 下行④：他域对象（闭包外）单选→不动面板（清除——域间零串扰）。
    service.selectBusiness({fixedOid(0x88)}, ui::SelectionSource::Search);
    ASSERT_EQ(fx.highlight.calls.size(), beforeRuntimeOnly + 1);
    EXPECT_FALSE(fx.highlight.calls.back().has_value());

    // 上行①：本域三维拾取→View3DPick 来源汇入（服务唯一写入口），
    // 且随 selectBusiness 触发 L2 高亮判定（正向命中→出口收到运行时名）。
    const core::ObjectId l1 = fx.ws.design.links[0].objectId;
    EXPECT_TRUE(adapter.reportView3DPick(l1));
    EXPECT_EQ(service.selectedObjectIds().size(), std::size_t{1});
    EXPECT_TRUE(service.selectedObjectIds().front() == l1);
    ASSERT_TRUE(service.selectionSource().has_value());
    EXPECT_EQ(*service.selectionSource(), ui::SelectionSource::View3DPick);
    ASSERT_FALSE(outlet.highlighted.empty());
    EXPECT_EQ(outlet.highlighted.back(), "L1");  // L2：名称端口正向取得

    // 上行②：闭包外/无效身份拒绝上报（零伪造——不出诊断的常态分支）。
    EXPECT_FALSE(adapter.reportView3DPick(fixedOid(0x77)));
    EXPECT_FALSE(adapter.reportView3DPick(core::ObjectId{}));  // 全零保留值
    EXPECT_EQ(service.selectedObjectIds().size(), std::size_t{1});  // 选中未变

    // 退订后：上行通道关闭（诚实 false），服务事件不再驱动面板。
    adapter.detach();
    EXPECT_FALSE(adapter.reportView3DPick(l1));
}

// =====================================================================
// 迁移链路端到端（acceptance 2——树导航→检查器→复杂页→L2/L3 可演示）
// =====================================================================

/**
 * 契约 WP-13-T20 acceptance 2：全链端到端（模型层）——建模对象经共享
 * 项目树供给与重建→树选中（selectBusiness 汇聚）→检查器常用字段刷新
 * （first-wins 应答）→L2 三维高亮（名称端口正向→出口动作）→D6 复杂
 * 页激活→L3 反解定位（TreeView 事件→反解→树定位命中）；L3 反解失败
 * 分支＝runtimeOnly 暂态（树不动、业务选中不变、检查器呈现不变）。
 */
TEST(HostMigrationEndToEnd, TreeToInspectorToComplexToHighlight)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-07"}, std::vector<std::string>{"B1-ACC-2"});
    MigrationFixture fx;

    // ---- 共享面装配（UI-T21/T22 模型＋域三接入面——宿主装配的最小形）。
    auto nameMap = std::make_shared<StubNameMap>();
    fillStubNames(*nameMap, fx.ws);
    RecordingHighlightOutlet outlet;
    ui::ProjectTreeModel treeModel;
    treeModel.addProvider(fx.tree);

    // 树定位回调＝L3 反解成功的落点（面板半区语义等价：locate 命中→
    // 以 Qt 选中信号回流 selectBusiness——IndustrialProjectTreePanel::
    // locateAndHighlight 的"单一写入路径"契约；未命中返回 false 不伪造）。
    // 服务指针两段式接线：回调只在服务构造后被触发（Deps 先于服务构造）。
    std::vector<core::ObjectId> located;
    ui::SelectionService* servicePtr = nullptr;
    ui::SelectionService::Deps serviceDeps;
    serviceDeps.nameMap = nameMap;
    serviceDeps.treeLocator = [&treeModel, &servicePtr, &located](const core::ObjectId& oid) {
        located.push_back(oid);
        if (servicePtr != nullptr && treeModel.locate(oid).has_value()) {
            servicePtr->selectBusiness({oid}, ui::SelectionSource::ProjectTree);
            return true;
        }
        return false;
    };
    serviceDeps.highlightOutlet = std::shared_ptr<ui::IUiHighlightOutlet>(
        &outlet, [](ui::IUiHighlightOutlet*) {});
    ui::SelectionService service(serviceDeps);
    servicePtr = &service;  // 两段式接线第二段（此后 L3 回调可回流选中）

    ui::PropertyInspectorModel inspector;  // 检查器模型（L1 消费者）
    inspector.addProvider(fx.pages);
    auto inspectorSub = service.subscribe(inspector);  // L1：订阅即得刷新

    ModelingSelectionAdapter adapter(fx.deps);  // 域适配器（下行高亮半区）
    adapter.attach(service);

    // ---- ①树导航：重建→关节 1 可定位（树面板选中即 selectBusiness——
    //      与 IndustrialProjectTreePanel 同一写入路径）。
    const ui::TreeRebuildReport rebuild = treeModel.rebuild();
    ASSERT_TRUE(rebuild.ok) << "reason=" << rebuild.reason;
    const core::ObjectId j0 = fx.ws.design.joints[0].objectId;
    ASSERT_TRUE(treeModel.locate(j0).has_value());

    // ---- ②检查器常用字段：树选→服务广播→检查器 first-wins 应答。
    service.selectBusiness({j0}, ui::SelectionSource::ProjectTree);
    EXPECT_EQ(inspector.view().kind, ui::InspectorContentKind::ObjectFields);
    EXPECT_EQ(inspector.view().domainKey, "modeling");
    EXPECT_EQ(inspector.view().fields.size(), std::size_t{3});

    // ---- ③L2 三维高亮：单选已应用对象→名称端口正向命中→出口动作。
    //      （harness 场景名称映射由 L5 绑定——测试以 StubNameMap 承载。）
    ASSERT_EQ(outlet.highlighted.size(), std::size_t{1});
    EXPECT_EQ(outlet.highlighted.front(), "J1");

    // ---- ④D6 复杂编辑页：检查器编排面→域 Provider 域自持打开。
    const auto activation = inspector.activateComplexPage(j0, "dh-parameters", nullptr);
    EXPECT_TRUE(activation.ok);
    ASSERT_EQ(fx.activator.calls.size(), std::size_t{1});
    EXPECT_TRUE(fx.activator.calls.back() == j0);

    // ---- ⑤L3 反解定位：TreeView Select Frame 事件→反解→树定位命中
    //      （业务选中以 ProjectTree 来源收敛——呈现语义与树选一致）。
    service.handleTreeViewFrameSelected("J2");
    ASSERT_EQ(located.size(), std::size_t{1});
    EXPECT_TRUE(located.front() == fx.ws.design.joints[1].objectId);
    EXPECT_EQ(service.selectedObjectIds().size(), std::size_t{1});
    EXPECT_TRUE(service.selectedObjectIds().front() == fx.ws.design.joints[1].objectId);
    ASSERT_TRUE(service.selectionSource().has_value());
    EXPECT_EQ(*service.selectionSource(), ui::SelectionSource::ProjectTree);
    EXPECT_EQ(inspector.view().kind, ui::InspectorContentKind::ObjectFields);
    EXPECT_EQ(inspector.view().fields.size(), std::size_t{3});  // 关节页随选中刷新

    // ---- ⑥L3 反解失败分支：树不动、业务选中不变、检查器呈现不变
    //      （runtimeOnly 暂态——B1-SPEC §4.2 v1.1 口径）。
    const auto viewBefore = inspector.view();
    service.handleTreeViewFrameSelected("World.UnknownFrame");
    EXPECT_TRUE(service.hasRuntimeOnlySelection());
    EXPECT_EQ(service.selectedObjectIds().size(), std::size_t{1});
    EXPECT_TRUE(service.selectedObjectIds().front() == fx.ws.design.joints[1].objectId);
    EXPECT_EQ(inspector.view().kind, viewBefore.kind);  // 呈现零触碰
    EXPECT_TRUE(inspector.view().objectId == viewBefore.objectId);
}

