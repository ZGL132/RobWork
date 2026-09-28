/**
 * @file   HostMigrationTest.cpp
 * @brief  运动学域宿主迁移三接入面测试（模型层——QCoreApplication 级）——
 *         任务契约 WP-15-T18 acceptance 1/4（v1.2，O-44 接入面 v1 诚实
 *         边界）与 acceptance 2/3（Jog/Playback 会话姿态承接零修订）的
 *         具名自证面。
 *
 * 设计依据：
 *   - B1-SPEC §5.1（TreeNodes/PropertyPages/SelectionAdapter 三接入面）、
 *     §5.2（迁移期双形态并存——标记保留可用）、§5.3（共享 UI 文件互斥
 *     ——本测试只消费 ui 冻结协议公共头，零共享面改动）；
 *   - DTB §4.2 O-44（2026-09-29 所有者"解决阻塞"口令裁决——出路②契约
 *     树面收窄：本域无持有 ObjectId 的对象，树/页面两面按结构接缝注册
 *     ＋诚实空供给/无应答；端到端链路"任务点选择→结果面板高亮→D6 求解
 *     配置页经面板直达"）；
 *   - ui 注册协议（UI-T21/T22 冻结形状）：IndustrialProjectTree.hpp
 *     （ProjectTreeModel 重建/校验）、PropertyInspector.hpp
 *     （PropertyInspectorModel first-wins 询问/NoProviderAnswer 占位）、
 *     SelectionService.hpp（selectBusiness 唯一写入口/handleTreeViewFrame
 *     Selected 反解编排/runtimeOnly 两态）、View3DContract.hpp
 *     （view3DSessionPoseContract——D8/D9 语义钉住）；
 *   - requirements 先例：WP-14-T10 HostMigrationTest（同型测试——本文件
 *     为 kinematics 侧同构，域语义替换为 v1 诚实边界）。
 *
 * 测试范围声明：三接入面为模型层（零 Qt 控件）——面板 widget 半区
 *   （focusTaskPoint 定位/迁移状态标记 objectName）归 harness GUI 冒烟
 *   留痕承载（requirements 先例同款范围声明）；本文件验证"域供给值 →
 *   共享模型 → 联动编排"的全链语义＋Jog/Playback 承接缝的零修订结构
 *   断言。AC 6 的构建/门禁/留痕项在任务验证流程执行（ird_gates＋双模式
 *   构建＋traceability/builds/wp15-t18/），不在本测试文件内。
 */

#include <gtest/gtest.h>

#include <QCoreApplication>

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯登记（testkit §7.3）

#include <cmath>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <sdurws/ird/kinematics/Commands.hpp>   // KinSessionPose（会话姿态唯一写点）
#include <sdurws/ird/kinematics/KinematicsPluginAssembly.hpp>  // 装配门面（宿主承接转发面）
#include <sdurws/ird/ui/IndustrialProjectTree.hpp>  // ui::ProjectTreeModel（共享树模型）
#include <sdurws/ird/ui/PropertyInspector.hpp>      // ui::PropertyInspectorModel（共享检查器模型）
#include <sdurws/ird/ui/SelectionService.hpp>       // ui::SelectionService（选择唯一汇聚点）
#include <sdurws/ird/ui/View3DContract.hpp>         // view3DSessionPoseContract（D8/D9 语义钉住）
#include "plugin/KinHostMigrationProviders.hpp"     // 被测三接入面（插件私有头——同单元可含）
#include "plugin/KinematicsUiModule.hpp"            // 被测模块（暴露面/承接缝——完整类型）
#include "plugin/KinPanelTypes.hpp"                 // 会话态/服务缝（模块构造面）

// ---- 使用声明（与被测头同一命名空间——用例可读性）--------------------
using namespace sdurws::ird;
using namespace sdurws::ird::kinematics;

namespace core = sdurws::ird::core;
namespace ui = sdurws::ird::ui;

namespace {

// =====================================================================
// 夹具（requirements HostMigrationTest 同款形态——最小替身＋记录器）
// =====================================================================

/// 名称映射替身（双向恒 nullopt——测试场景无已应用对象，L2/L3 反例分支）。
class NullNameMap final : public ui::IUiRuntimeNameMapPort {
public:
    std::optional<core::ObjectId> resolveObjectIdFromRuntimeName(
        const std::string&) const override
    {
        return std::nullopt;
    }

    std::optional<std::string> resolveRuntimeName(const core::ObjectId&) const override
    {
        return std::nullopt;
    }
};

/// 树定位替身（恒 false——测试场景共享树无内容可定位，一致性自证值）。
bool neverLocate(const core::ObjectId&)
{
    return false;
}

/// 下行高亮记录器（deps.panelHighlight 绑定面——联动语义的观测点）。
class HighlightRecorder {
public:
    void operator()(const std::optional<core::ObjectId>& oid) { calls.push_back(oid); }

    std::vector<std::optional<core::ObjectId>> calls;
};

/// 最小选择服务工厂（nameMap/treeLocator 必填——其余可空显式缺省）。
ui::SelectionService makeSelectionService()
{
    return ui::SelectionService(ui::SelectionService::Deps{
        std::make_shared<NullNameMap>(), neverLocate, nullptr, nullptr});
}

/// 命令提交记录缝（零修订断言的观测点——Jog/Playback 承接不得触发提交）。
struct SubmitRecorder {
    int commands = 0;   ///< 命令提交次数（修订写面的代理观测）
    int backgrounds = 0; ///< 后台提交次数（评估/失效面的代理观测）

    KinCommandSubmitFn commandSink()
    {
        return [this](const std::string&) { ++commands; };
    }

    KinBackgroundSubmitFn backgroundSink()
    {
        return [this](const KinBackgroundRequest&) {
            ++backgrounds;
            KinBackgroundAck ack;
            ack.accepted = true;
            ack.taskRef = "recorder";
            return ack;
        };
    }
};

}  // namespace

// =====================================================================
// 接入面一：TreeNodesProvider——结构接缝＋v1 恒空集（AC1 v1——O-44）
// =====================================================================

TEST(HostMigration, TreeProvider_DomainKeyAndHonestEmptySupply_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-12", "UX-12"},
                  std::vector<std::string>{"AC1-v1"});
    // 域键＝三协议统一词表（注册边界查重锚——与 modeling/requirements 同表）。
    KinematicsSharedSurfaceDeps deps;
    KinematicsTreeNodesProvider provider(deps);
    EXPECT_EQ(provider.domainKey(), "kinematics");

    // v1 恒空集且无状态（重复询问同形——"恒空"是类型面事实不是运行期巧合）。
    EXPECT_TRUE(provider.treeNodes().empty());
    EXPECT_TRUE(provider.treeNodes().empty());
}

TEST(HostMigration, TreeProvider_EmptySupplyPassesSharedRebuild_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-12"},
                  std::vector<std::string>{"AC1-v1"});
    // 注册进共享树模型：重建全通、零节点贡献、五组全空（协议"空集不产生
    // 组内占位节点"的模型级兑现——接缝落位与内容空缺同时自证）。
    ui::ProjectTreeModel model;
    model.addProvider(std::make_shared<KinematicsTreeNodesProvider>(
        KinematicsSharedSurfaceDeps{}));
    const ui::TreeRebuildReport report = model.rebuild();
    ASSERT_TRUE(report.ok);
    EXPECT_EQ(report.nodeCount, 0u);
    EXPECT_EQ(model.nodeCount(), 0u);
    for (const auto group : {
             ui::ProjectTreeGroup::ProjectStructure,
             ui::ProjectTreeGroup::ModelingObjects,
             ui::ProjectTreeGroup::RequirementObjects,
             ui::ProjectTreeGroup::AnalysisConfigurations,
             ui::ProjectTreeGroup::ResultsEvidenceReports,
         }) {
        EXPECT_TRUE(model.nodesInGroup(group).empty());
    }
}

TEST(HostMigration, TreeProvider_CoexistsWithForeignProvider_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-12"},
                  std::vector<std::string>{"AC1-v1"});
    // 与外域 Provider 共存：本域零贡献不干扰他域供给（domainKey 查重全通
    // ——结构接缝的共存语义；跨域代供反向自证：本域节点集恒空，不存在
    // 与 requirements Provider 撞号的任何可能）。
    class ForeignProvider final : public ui::IUiTreeNodesProvider {
    public:
        std::string domainKey() const override { return "requirements"; }
        std::vector<ui::ProjectTreeNode> treeNodes() const override
        {
            ui::ProjectTreeNode node;
            node.objectId = core::ObjectId::generate();
            node.group = ui::ProjectTreeGroup::RequirementObjects;
            return {node};
        }
    };
    ui::ProjectTreeModel model;
    model.addProvider(std::make_shared<ForeignProvider>());
    model.addProvider(std::make_shared<KinematicsTreeNodesProvider>(
        KinematicsSharedSurfaceDeps{}));
    const ui::TreeRebuildReport report = model.rebuild();
    ASSERT_TRUE(report.ok);
    EXPECT_EQ(report.nodeCount, 1u);  // 恰为外域一节点——本域零贡献
}

// =====================================================================
// 接入面二：PropertyPagesProvider——结构接缝＋v1 无应答面（AC1 v1——O-44）
// =====================================================================

TEST(HostMigration, PagesProvider_NoAnswerSurface_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-05"},
                  std::vector<std::string>{"AC1-v1"});
    KinematicsPropertyPagesProvider provider(KinematicsSharedSurfaceDeps{});
    EXPECT_EQ(provider.domainKey(), "kinematics");

    // 任意选中对象（含外域任务点）→ 恒无应答（nullopt/空集合法二态）。
    const core::ObjectId anyOid = core::ObjectId::generate();
    EXPECT_FALSE(provider.commonFieldsPage(anyOid).has_value());
    EXPECT_TRUE(provider.complexPageEntries(anyOid).empty());

    // 激活＝诚实拒绝（封闭词表 token——不伪造打开成功；D6 收口位在域面板
    // 高级面板，不经检查器展开）。
    const ui::ComplexPageActivationReport report =
        provider.activateComplexPage(anyOid, "solve-config", nullptr);
    EXPECT_FALSE(report.ok);
    EXPECT_EQ(report.reason, "activation-object-not-owned");
    EXPECT_EQ(report.hostedWidget, nullptr);
}

TEST(HostMigration, PagesProvider_InspectorShowsHonestNoProviderAnswer_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-05"},
                  std::vector<std::string>{"AC1-v1"});
    // 共享检查器装配本域 Provider 后选中任意对象：first-wins 询问全空 →
    // NoProviderAnswer 占位（协议"迁移期常态"语义——诚实占位非缺陷）。
    ui::PropertyInspectorModel inspector;
    inspector.addProvider(
        std::make_shared<KinematicsPropertyPagesProvider>(KinematicsSharedSurfaceDeps{}));
    ui::SelectionChange change;
    change.selectedObjectIds = {core::ObjectId::generate()};
    change.source = ui::SelectionSource::ProjectTree;
    inspector.onSelectionChanged(change);
    EXPECT_EQ(inspector.view().kind, ui::InspectorContentKind::NoProviderAnswer);
    EXPECT_TRUE(inspector.view().complexEntries.empty());
}

// =====================================================================
// 接入面三：SelectionAdapter——下行联动全功能（AC4 v1 链路第二棒）
// =====================================================================

TEST(HostMigration, Adapter_DownlinkSingleSelectionHighlights_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-06", "UX-12"},
                  std::vector<std::string>{"AC4-v1"});
    HighlightRecorder recorder;
    KinematicsSelectionAdapter adapter(
        KinematicsSharedSurfaceDeps{[&recorder](const std::optional<core::ObjectId>& oid) {
            recorder(oid);
        }});
    ui::SelectionService selection = makeSelectionService();
    adapter.attach(selection);

    // 任务点选择（requirements 域对象经共享树选中——业务单选）→ 面板高亮
    // 执行器收到该点身份（"任务点选择→结果面板高亮"的模型级链路证明）。
    const core::ObjectId taskPoint = core::ObjectId::generate();
    selection.selectBusiness({taskPoint}, ui::SelectionSource::ProjectTree);
    ASSERT_EQ(recorder.calls.size(), 1u);
    ASSERT_TRUE(recorder.calls.back().has_value());
    EXPECT_EQ(recorder.calls.back()->toCanonical(), taskPoint.toCanonical());
}

TEST(HostMigration, Adapter_DownlinkClearAndMultiSymmetric_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-12"},
                  std::vector<std::string>{"AC4-v1"});
    HighlightRecorder recorder;
    KinematicsSelectionAdapter adapter(
        KinematicsSharedSurfaceDeps{[&recorder](const std::optional<core::ObjectId>& oid) {
            recorder(oid);
        }});
    ui::SelectionService selection = makeSelectionService();
    adapter.attach(selection);

    // 先建立选中（清空在空集上是"无变化"——服务不广播，先给清除一个
    // 真实的前态）。
    const core::ObjectId taskPoint = core::ObjectId::generate();
    selection.selectBusiness({taskPoint}, ui::SelectionSource::ProjectTree);
    ASSERT_EQ(recorder.calls.size(), 1u);

    // 清空选中 → 对称清除（nullopt——高亮的对称收口）。
    selection.clearSelection(ui::SelectionSource::ProjectTree);
    ASSERT_EQ(recorder.calls.size(), 2u);
    EXPECT_FALSE(recorder.calls.back().has_value());

    // 多选 → 无单点定位语义，对称清除（D5"当前选中对象"单数语义的域面板落点）。
    selection.selectBusiness({core::ObjectId::generate(), core::ObjectId::generate()},
                             ui::SelectionSource::ProjectTree);
    ASSERT_EQ(recorder.calls.size(), 3u);
    EXPECT_FALSE(recorder.calls.back().has_value());
}

TEST(HostMigration, Adapter_RuntimeOnlySelectionLeavesHighlightUntouched_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-12"},
                  std::vector<std::string>{"AC4-v1"});
    HighlightRecorder recorder;
    KinematicsSelectionAdapter adapter(
        KinematicsSharedSurfaceDeps{[&recorder](const std::optional<core::ObjectId>& oid) {
            recorder(oid);
        }});
    ui::SelectionService selection = makeSelectionService();
    adapter.attach(selection);

    // 仅运行时对象选择（L3 反解失败暂态——nameMap 替身恒 nullopt 触发）→
    // 零触碰（业务选中未变，不误清既有高亮——SelectionChange 两态语义）。
    selection.handleTreeViewFrameSelected("Frame.NotInNameMap");
    EXPECT_TRUE(recorder.calls.empty());
}

TEST(HostMigration, Adapter_UplinkView3DPickHonestNoReport_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-12"},
                  std::vector<std::string>{"AC1-v1"});
    // 上行三维拾取上报 v1 恒 false（本域无持有 ObjectId 的业务对象——
    // 诚实不上报，零伪造）。
    HighlightRecorder recorder;
    KinematicsSelectionAdapter adapter(
        KinematicsSharedSurfaceDeps{[&recorder](const std::optional<core::ObjectId>& oid) {
            recorder(oid);
        }});
    ui::SelectionService selection = makeSelectionService();
    adapter.attach(selection);
    EXPECT_FALSE(adapter.reportView3DPick(core::ObjectId::generate()));
    EXPECT_TRUE(recorder.calls.empty());  // 上行不上报即无选中事件回流
}

// =====================================================================
// 模块暴露面——三接入面句柄与适配器接线（AC1 装配形态）
// =====================================================================

TEST(HostMigration, Module_SharedSurfaceHandlesStableAndWired_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-12"},
                  std::vector<std::string>{"AC1-v1"});
    KinematicsUiModule module;
    const auto handles = module.sharedSurfaceProviders();
    ASSERT_TRUE(handles.treeNodes != nullptr);
    ASSERT_TRUE(handles.propertyPages != nullptr);
    EXPECT_EQ(handles.treeNodes->domainKey(), "kinematics");
    // 惰性缓存语义：重复调用同一实例（shared_ptr 稳定地址——requirements
    // 先例同款；共享模型持强引用与模块缓存同序）。
    const auto again = module.sharedSurfaceProviders();
    EXPECT_EQ(handles.treeNodes.get(), again.treeNodes.get());
    EXPECT_EQ(handles.propertyPages.get(), again.propertyPages.get());
}

// =====================================================================
// Jog/Playback 会话姿态承接——零修订/零失效/零缓存（AC2/AC3——KIN-06/AT-04）
// =====================================================================

TEST(HostMigration, HostJointState_AppliesWithZeroRevision_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-06", "AT-04"},
                  std::vector<std::string>{"AC2"});
    KinSessionPose pose;
    SubmitRecorder recorder;
    KinematicsUiModule module;
    KinPanelServices services;
    services.sessionPose = &pose;
    services.commandSubmit = recorder.commandSink();
    services.backgroundSubmit = recorder.backgroundSink();
    module.setServices(services);

    // Jog 关节点动（D8——宿主 State 变化的域侧承接）→ 会话姿态写入 +
    // 返回 true；零命令提交（零修订）、零后台提交（零评估/零失效）。
    // （宏实参不写花括号初始化列表——命名 bool 中转，避免预处理分裂。）
    const bool applied = module.applyHostJointState({0.1, -0.2, 0.3});
    EXPECT_TRUE(applied);
    ASSERT_TRUE(pose.isSet());
    EXPECT_EQ(pose.jointConfiguration().size(), 3u);
    EXPECT_DOUBLE_EQ(pose.jointConfiguration()[0], 0.1);
    EXPECT_EQ(recorder.commands, 0);
    EXPECT_EQ(recorder.backgrounds, 0);
}

TEST(HostMigration, HostJointState_PlaybackFramesDriveSameSeam_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-06", "TRJ-07"},
                  std::vector<std::string>{"AC3"});
    KinSessionPose pose;
    SubmitRecorder recorder;
    KinematicsUiModule module;
    KinPanelServices services;
    services.sessionPose = &pose;
    services.commandSubmit = recorder.commandSink();
    services.backgroundSubmit = recorder.backgroundSink();
    module.setServices(services);

    // 播放帧序列（D9——TimedStatePath 播放驱动的逐帧承接，同缝零修订）：
    // 32 帧连写后姿态＝末帧值，提交/后台计数仍为零（零修订播放驱动）。
    for (int frame = 0; frame < 32; ++frame) {
        const double q = 0.01 * static_cast<double>(frame);
        const bool frameApplied = module.applyHostJointState({q, -q});
        ASSERT_TRUE(frameApplied);
    }
    ASSERT_TRUE(pose.isSet());
    EXPECT_DOUBLE_EQ(pose.jointConfiguration()[0], 0.31);
    EXPECT_DOUBLE_EQ(pose.jointConfiguration()[1], -0.31);
    EXPECT_EQ(recorder.commands, 0);
    EXPECT_EQ(recorder.backgrounds, 0);
}

TEST(HostMigration, HostJointState_NonFiniteFailsFast_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-03"},
                  std::vector<std::string>{"AC2"});
    KinSessionPose pose;
    KinematicsUiModule module;
    KinPanelServices services;
    services.sessionPose = &pose;
    module.setServices(services);

    // 非有限分量（NaN——宿主桥投影违约）→ fail-fast（NFR-COR-03 不钳制
    // 不置零——非法值不入会话态）。（EXPECT_THROW 实参同样经命名函数
    // 中转，避免花括号列表进宏。）
    const auto nonFiniteApply = [&module]() {
        return module.applyHostJointState({0.0, std::nan("")});
    };
    EXPECT_THROW(nonFiniteApply(), std::invalid_argument);
    EXPECT_FALSE(pose.isSet());  // 异常路径不入会话态
}

TEST(HostMigration, HostJointState_MissingSeamHonestDegradation_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-06"},
                  std::vector<std::string>{"AC2"});
    // 会话姿态缝未装配（sessionPose 空——宿主桥未接线）→ 诚实降级
    // 返回 false，零虚构写入成功。
    KinematicsUiModule module;
    const bool degraded = module.applyHostJointState({0.1});
    EXPECT_FALSE(degraded);
}

TEST(HostMigration, SessionPoseContractBitsFrozen_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-06", "AT-04"},
                  std::vector<std::string>{"AC2", "AC3"});
    // D8/D9 语义钉住（View3DSessionPoseContract 四布尔位——会话姿态回写
    // 仅会话级：零模型修改/零修订/零失效。位值恒定，改动即测试失败＝
    // 必须走单元卡增量修订而非代码私改）。
    const ui::View3DSessionPoseContract contract = ui::view3DSessionPoseContract();
    EXPECT_TRUE(contract.sessionStateOnly);
    EXPECT_FALSE(contract.writesDesignModel);
    EXPECT_FALSE(contract.producesRevision);
    EXPECT_FALSE(contract.invalidatesResults);
}

// =====================================================================
// 装配门面——宿主消费转发面（AC2/AC6 装配形态）
// =====================================================================

TEST(HostMigration, Facade_ForwardsHostJointStateAndHandles_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-06"},
                  std::vector<std::string>{"AC2"});
    KinematicsPluginAssembly bundle = createKinematicsPluginAssembly();
    KinModuleSessionState& session = bundle.session();
    session.savedConfig.seed = 1;  // 会话基线合法化（seed=0 非法——I-KIN-4）
    session.savedConfig.regionBudget.seed = 1;

    // 门面三接入面句柄转发（UI-T23 集成收口的消费面形状自证）。
    const auto handles = bundle.sharedSurfaceProviders();
    ASSERT_TRUE(handles.treeNodes != nullptr);
    ASSERT_TRUE(handles.propertyPages != nullptr);
    EXPECT_EQ(handles.treeNodes->domainKey(), "kinematics");

    // 承接缝未装配（会话姿态缝为空）→ 门面转发后仍诚实降级返回 false。
    const bool facadeDegraded = bundle.applyHostJointState({0.0});
    EXPECT_FALSE(facadeDegraded);
}
