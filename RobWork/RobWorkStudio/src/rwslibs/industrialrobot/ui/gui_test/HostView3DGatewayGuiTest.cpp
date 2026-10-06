/**
 * @file   HostView3DGatewayGuiTest.cpp
 * @brief  宿主三维网关 Widgets 级测试（UI-T45——集成树专属门控增列）：
 *         上行拾取链／呈现出口事务／TCP 会话态源／构造降级四面。
 *
 * 设计依据：
 *   - 方案 spec docs/superpowers/specs/2026-10-03-view3d-stage-b-design.md
 *     （两桥一源——高亮由既有 HostHighlightOutlet 承载不在本测面）；
 *   - units/ui.md §4.3/§6.5/§11.2；KIN-06（会话姿态零修订——TCP 源断言锚）；
 *   - 先例：PolicySummaryCardGuiTest（QApplication main 形态）；
 *     HostIntegrationContractTest（假实现＋同源编入形态）。
 *
 * 门控说明：本文件与 plugin/HostView3DGateway.cpp 同源编入 ui_gui_test
 * （TARGET sdurws 判别——rw 框架链接面集成树专属，冒烟树不注册；与
 * ui contract 三域增列同款纪律）。
 *
 * 断言纪律：全部经替身函数缝驱动（零真实 RWStudioView3D——框架视图
 * 需宿主窗口系统；Deps 函数缝即测试面）；帧＝StateStructure 真实构造。
 */

#include <gtest/gtest.h>

#include <QApplication>
#include <QPoint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <rw/kinematics/FixedFrame.hpp>
#include <rw/kinematics/MovableFrame.hpp>
#include <rw/kinematics/StateStructure.hpp>

#include <sdurws/ird/core/Digest.hpp>                 // ContentIdentity 构造（呈现视图身份）
#include <sdurws/ird/core/Identity.hpp>               // ObjectId/RevisionId::generate
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯登记
#include <sdurws/ird/ui/SelectionService.hpp>         // SelectionService/观察者/来源词表
#include "plugin/HostView3DGateway.hpp"               // 被测网关（同单元 PRIVATE 面——插件目标同源）
#include <sdurws/ird/ui/View3DPreviewContract.hpp>    // 会话预览协议值（UI-T33——预览半区替身面）

using namespace sdurws::ird;
using namespace sdurws::ird::ui;

namespace {

/// 名称映射替身（SA-05 双射的最小实现——正反两向同表）。
struct StubNameMap final : IUiRuntimeNameMapPort {
    std::map<std::string, core::ObjectId> forward;  ///< 运行时名→业务身份

    std::optional<core::ObjectId> resolveObjectIdFromRuntimeName(
        const std::string& runtimeName) const override
    {
        const auto it = forward.find(runtimeName);
        return it != forward.end()
                   ? std::optional<core::ObjectId>(it->second)
                   : std::nullopt;
    }
    std::optional<std::string> resolveRuntimeName(
        const core::ObjectId& id) const override
    {
        for (const auto& [name, mapped] : forward) {
            if (mapped == id) { return name; }
        }
        return std::nullopt;
    }
};

/// 选中观察替身（记录变更事实——来源/选中集断言面）。
struct RecordingObserver final : IUiSelectionObserver {
    std::optional<SelectionChange> last;  ///< 最近一次变更（空＝未广播）

    void onSelectionChanged(const SelectionChange& change) override
    {
        last = change;
    }
};

/// 树定位替身（SelectionService Deps 必填缝——记录调用即够）。
struct RecordingTreeLocator {
    int calls = 0;
    bool locate(const core::ObjectId&)
    {
        ++calls;
        return true;
    }
};

/// 呈现对象替身（挂接/摘除计数＋apply 可配置失败——事务语义观测面）。
struct RecordingPresentationObject final
    : HostView3DGateway::HostPresentationObject {
    mutable int applyCalls = 0;
    mutable int removeCalls = 0;
    bool applyResult = true;  ///< false＝模拟挂接失败（保旧断言面）

    bool apply() const override { ++applyCalls; return applyResult; }
    void remove() const override { ++removeCalls; }
};

/// 构造有效 ObjectId（generate——非零保证，Identity 保留值纪律）。
core::ObjectId anyObjectId()
{
    return core::ObjectId::generate();
}

/// 构造有效内容身份（字节非零——Digest256 保留值纪律）。
core::ContentIdentity anyContentIdentity()
{
    const std::string seed = "ui-t45-test-content";
    core::ContentDigester digester;
    digester.update(seed.data(), seed.size());
    core::ContentIdentity identity;
    identity.bytes = digester.finalize();
    return identity;
}

/// 网关夹具（required 缝齐备；TCP 缝留空——TCP 用例自建全缝网关）。
struct GatewayHarness {
    StubNameMap nameMap;
    RecordingTreeLocator treeLocator;
    RecordingObserver observer;
    rw::kinematics::StateStructure tree;   ///< 帧结构（真实构造——拾取帧来源）
    rw::kinematics::Frame* pickedFrame = nullptr;
    int dispatchModelingCalls = 0;
    int dispatchRequirementsCalls = 0;
    std::shared_ptr<SelectionService> selection;
    std::unique_ptr<core::IEventSubscription> subscription;  ///< 订阅句柄（RAII——存活期＝夹具，过早起销＝观察者收不到广播）

    GatewayHarness()
    {
        SelectionService::Deps sdeps;
        sdeps.nameMap = std::make_shared<StubNameMap>(nameMap);
        auto* locator = &treeLocator;
        sdeps.treeLocator = [locator](const core::ObjectId& oid) {
            return locator->locate(oid);
        };
        selection = std::make_shared<SelectionService>(sdeps);
        subscription = selection->subscribe(observer);
    }

    std::unique_ptr<HostView3DGateway> make()
    {
        HostView3DGateway::Deps deps;
        deps.pickFrame = [this](int, int) { return pickedFrame; };
        deps.selection = selection.get();
        deps.nameMap = &nameMap;
        deps.dispatchToModeling = [this](const core::ObjectId&) {
            ++dispatchModelingCalls;
            return true;
        };
        deps.dispatchToRequirements = [this](const core::ObjectId&) {
            ++dispatchRequirementsCalls;
            return true;
        };
        // 预览后端替身（UI-T33——previewDraw 空＝后端缺位的降级形态；
        // 非空＝记录绘制并按返回值编排——测试以字段操控编排分支）。
        if (previewDraw) {
            deps.previewBackend.draw = [this](const ui::View3DPreviewUpdate& u) {
                lastDrawn = u;
                return previewDraw(u);
            };
            deps.previewBackend.clear = [this]() { ++previewClears; };
        }
        return std::make_unique<HostView3DGateway>(std::move(deps));
    }

    // ---- 预览后端替身字段（UI-T33 用例——操控编排分支的记录面）--------
    std::function<bool(const ui::View3DPreviewUpdate&)> previewDraw;  ///< 空＝后端缺位
    std::optional<ui::View3DPreviewUpdate> lastDrawn;                 ///< 最近一次绘制值
    int previewClears = 0;                                            ///< 清除计数
};

}  // namespace

// =====================================================================
// 上行拾取链（反解→域分发→View3DPick 选中；分支②零伪造）
// =====================================================================

/// 命中帧＋反解成功 → 两域分发＋View3DPick 来源选中（链路有产出）。
TEST(HostView3DGateway, UpstreamPick_ResolvesSelectsAndDispatches_UI_T45)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-11", "KIN-06"},
                  std::vector<std::string>{});

    GatewayHarness fx;
    auto l1 = rw::core::ownedPtr(new rw::kinematics::FixedFrame("l1", rw::math::Transform3D<>{}));
    rw::kinematics::Frame* l1raw = l1.get();
    fx.tree.addFrame(l1);
    fx.pickedFrame = l1raw;
    const core::ObjectId oid = anyObjectId();
    fx.nameMap.forward["l1"] = oid;
    auto gateway = fx.make();

    EXPECT_TRUE(gateway->handleViewDoubleClick(QPoint(10, 10)))
        << "链路有产出（反解成功）却返回 false";
    EXPECT_EQ(fx.dispatchModelingCalls, 1) << "建模域未分发";
    EXPECT_EQ(fx.dispatchRequirementsCalls, 1) << "需求域未分发";
    ASSERT_TRUE(fx.observer.last.has_value()) << "选中未广播";
    ASSERT_EQ(fx.observer.last->selectedObjectIds.size(), std::size_t{1})
        << "选中集形态失实";
    EXPECT_EQ(fx.observer.last->selectedObjectIds.front(), oid)
        << "选中的业务身份与反解结果不符";
    EXPECT_EQ(fx.observer.last->source, SelectionSource::View3DPick)
        << "选中来源词表失实（三维拾取必须记 View3DPick）";
}

/// 未命中帧 / 反解失败 → false 且零分发零选中（分支②——零伪造语义）。
TEST(HostView3DGateway, UpstreamPick_MissOrUnresolved_NoSideEffect_UI_T45)
{
    IRD_TEST_INFO(std::vector<std::string>{"ERR-01"}, std::vector<std::string>{});

    GatewayHarness fx;
    auto l1 = rw::core::ownedPtr(new rw::kinematics::FixedFrame("l1", rw::math::Transform3D<>{}));
    fx.tree.addFrame(l1);
    fx.pickedFrame = l1.get();  // 帧在 NameMap 无映射＝反解失败形态
    auto gateway = fx.make();

    EXPECT_FALSE(gateway->handleViewDoubleClick(QPoint(1, 1)))
        << "反解失败应返回 false（放行框架默认处理）";
    EXPECT_EQ(fx.dispatchModelingCalls, 0) << "反解失败却分发了建模域";
    EXPECT_EQ(fx.dispatchRequirementsCalls, 0) << "反解失败却分发了需求域";
    EXPECT_FALSE(fx.observer.last.has_value()) << "反解失败却广播了选中（伪造语义）";

    // 未命中形态（空白处双击）：拾取返回空帧——同样零副作用。
    fx.pickedFrame = nullptr;
    EXPECT_FALSE(gateway->handleViewDoubleClick(QPoint(2, 2)))
        << "未命中应返回 false";
    EXPECT_FALSE(fx.observer.last.has_value()) << "未命中却广播了选中";
}

// =====================================================================
// 下行呈现出口（事务语义：原子替换/失败保旧/释放整组摘除）
// =====================================================================

/// 完整视图 → 挂接；换放＝旧摘除新挂接；挂接失败＝旧保持原状。
TEST(HostView3DGateway, PresentationOutlet_TransactionSemantics_UI_T45)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-11"}, std::vector<std::string>{});

    auto first = std::make_shared<RecordingPresentationObject>();
    auto second = std::make_shared<RecordingPresentationObject>();
    second->applyResult = false;  // 模拟挂接失败（保旧断言面）

    GatewayHarness fx;
    auto gateway = fx.make();

    const auto makeView =
        [](const std::shared_ptr<HostView3DGateway::HostPresentationObject>&
               payload) {
            ui::PresentationViewProjection view;
            view.modelIdentity = anyContentIdentity();
            view.appliedRevisionId = core::RevisionId::generate();
            view.presentationIdentity = anyObjectId();
            view.hostPayload = payload;
            view.objectExists = [](const core::ObjectId&) { return true; };
            return view;
        };

    // 首放：挂接成功＝attached。
    const auto firstReport = gateway->applyPresentation(makeView(first));
    ASSERT_TRUE(firstReport.ok) << "首放失败：" << firstReport.failureDetail;
    EXPECT_TRUE(gateway->presentationAttached());
    EXPECT_EQ(first->applyCalls, 1);

    // 换放且新挂接失败：旧呈现保持原状（不摘除——"失败保持原状"事务语义）。
    const auto failReport = gateway->applyPresentation(makeView(second));
    EXPECT_FALSE(failReport.ok) << "挂接失败应如实报告";
    EXPECT_EQ(failReport.failureToken, "ui-t45.attach-failed");
    EXPECT_EQ(first->removeCalls, 0) << "失败路径误摘了旧呈现（保旧语义违约）";
    EXPECT_TRUE(gateway->presentationAttached()) << "失败后旧呈现丢失";

    // 换放成功：旧摘除（1 次）＋新挂接。
    auto third = std::make_shared<RecordingPresentationObject>();
    const auto okReport = gateway->applyPresentation(makeView(third));
    EXPECT_TRUE(okReport.ok);
    EXPECT_EQ(first->removeCalls, 1) << "成功换放未摘除旧呈现";
    EXPECT_EQ(third->applyCalls, 1);
    EXPECT_TRUE(gateway->presentationAttached());

    // 释放：整组摘除＋挂接态清零（幂等——二次释放零调用）。
    gateway->releasePresentation();
    EXPECT_EQ(third->removeCalls, 1);
    EXPECT_FALSE(gateway->presentationAttached());
    gateway->releasePresentation();
    EXPECT_EQ(third->removeCalls, 1) << "二次释放重复摘除（非幂等）";

    // 场景清除拍＝呈现残留清理（close() 挂钩的直驱等价面）。
    gateway->applyPresentation(makeView(first));
    EXPECT_TRUE(gateway->presentationAttached());
    gateway->onSceneCleared();
    EXPECT_FALSE(gateway->presentationAttached()) << "场景清除拍残留呈现";
}

/// 不完整视图 → 失败报告（incomplete-view token）且零挂接。
TEST(HostView3DGateway, PresentationOutlet_IncompleteView_FailsCleanly_UI_T45)
{
    IRD_TEST_INFO(std::vector<std::string>{"ERR-01"}, std::vector<std::string>{});

    GatewayHarness fx;
    auto gateway = fx.make();

    ui::PresentationViewProjection incomplete;  // 全缺省＝不完整（身份/载体/查询缺项）
    const auto report = gateway->applyPresentation(incomplete);
    EXPECT_FALSE(report.ok);
    EXPECT_EQ(report.failureToken, "ui-t45.incomplete-view");
    EXPECT_FALSE(gateway->presentationAttached()) << "不完整视图产生了挂接";
}

// =====================================================================
// TCP 会话态数据源（worldTframe 只读；KIN-06 零修订）
// =====================================================================

/// TCP 帧与 State 在位 → 世界系位姿（逐分量断言）；双缝缺省 → nullopt。
TEST(HostView3DGateway, TcpPose_WorldFrameValueAndDegradedPaths_KIN06_UI_T45)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-06"}, std::vector<std::string>{});

    GatewayHarness fx;

    // 缺省（TCP 双缝未覆写）＝诚实降级 nullopt（不虚构位姿）。
    auto degraded = fx.make();
    EXPECT_FALSE(degraded->currentTcpPose("dev").has_value())
        << "TCP 缝缺省却产出了位姿";

    // 真实帧＋State：MovableFrame（DAF 挂恒等 FixedFrame 下）置于已知世界
    // 位姿——worldTframe 只读取值（l1 恒等 ⇒ 世界位姿＝自身变换）。
    auto l1 = rw::core::ownedPtr(new rw::kinematics::FixedFrame("l1", rw::math::Transform3D<>{}));
    fx.tree.addFrame(l1);
    auto tcp = rw::core::ownedPtr(new rw::kinematics::MovableFrame("Tcp"));
    rw::kinematics::Frame* tcpraw = tcp.get();
    fx.tree.addDAF(tcp, l1.get());
    rw::kinematics::State state = fx.tree.getDefaultState();
    const rw::math::Transform3D<> expected(
        rw::math::Vector3D<>(0.1, 0.2, 0.3),
        rw::math::Rotation3D<>(1, 0, 0, 0, 1, 0, 0, 0, 1));
    tcp->setTransform(expected, state);

    HostView3DGateway::Deps deps;
    deps.pickFrame = [](int, int) { return nullptr; };
    deps.resolveTcpFrame =
        [tcpraw](const std::string&) { return tcpraw; };
    deps.currentState = [&state]() -> const rw::kinematics::State* {
        return &state;
    };
    deps.selection = fx.selection.get();
    deps.nameMap = &fx.nameMap;
    auto gateway = std::make_unique<HostView3DGateway>(std::move(deps));

    const auto pose = gateway->currentTcpPose("dev");
    ASSERT_TRUE(pose.has_value()) << "TCP 帧与 State 在位却降级";
    for (int i = 0; i < 3; ++i) {
        EXPECT_DOUBLE_EQ(pose->P()(i), expected.P()(i))
            << "TCP 平移值失实（分量 " << i << "）";
    }
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            EXPECT_DOUBLE_EQ(pose->R()(r, c), expected.R()(r, c))
                << "TCP 旋转值失实（" << r << "," << c << "）";
        }
    }
}

// =====================================================================
// 构造降级（required 缝缺失 fail-fast；TCP 缝可空＝源降级合法形态）
// =====================================================================

/// required 缝缺失＝构造抛（禁止构造违反拾取承诺的网关）；TCP 缝可空。
TEST(HostView3DGateway, Constructor_MissingRequiredSeams_Throws_UI_T45)
{
    IRD_TEST_INFO(std::vector<std::string>{"ERR-01"}, std::vector<std::string>{});

    GatewayHarness fx;
    HostView3DGateway::Deps deps;
    deps.selection = fx.selection.get();
    deps.nameMap = &fx.nameMap;
    // pickFrame 缺失＝拾取承诺违约。
    EXPECT_THROW({ HostView3DGateway gw(deps); (void)gw; }, std::invalid_argument);
    deps.pickFrame = [](int, int) { return nullptr; };
    EXPECT_NO_THROW({ HostView3DGateway gw(deps); (void)gw; })
        << "required 缝齐备不应抛（TCP 缺省＝源降级合法形态）";
}

// =====================================================================
// 会话预览出口（UI-T33 收口——acceptance 1/2/3 的网关编排半区）
// =====================================================================

/// 预览编排三面：后端缺位＝诚实降级 false；apply 成功＝挂接态＋观测值
/// 同源；removePreview 幂等＋onSceneCleared 对称收口（零残留纪律）。
TEST(HostView3DGateway, PreviewOutlet_DegradedApplyAndClear_UI_T33)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-02", "ERR-01"},
                  std::vector<std::string>{"UI-T33-ACC1", "UI-T33-ACC2"});
    GatewayHarness fx;
    // ①后端缺位＝诚实降级（呈现/TCP/预览三面独立降级的显式形态——
    // 不虚构预览，ERR-01）。gateway 在替身字段前置后构造（Deps 冻结面
    // ——后段切替身需重建网关）。
    ui::View3DPreviewUpdate update;
    update.frameMarkers.push_back(ui::View3DFrameMarker{"工位 A", "World"});
    {
        auto degraded = fx.make();
        EXPECT_FALSE(degraded->applyPreview(update))
            << "渲染后端缺位却报成功（虚构预览）";
        EXPECT_FALSE(degraded->previewAttached());
    }

    // ②后端在位＝原子应用（挂接态＋观测值同源——currentPreview 观测面）。
    fx.previewDraw = [](const ui::View3DPreviewUpdate&) { return true; };
    auto gateway = fx.make();  // 替身字段前置后构造（Deps 冻结面）
    update.boxOutline = ui::View3DBoxOutline{};  // 空框占位（层存在性断言载体）
    ui::View3DSampleGrid grid;
    grid.gridLines.emplace_back(rw::math::Vector3D<double>(0, 0, 0),
                                rw::math::Vector3D<double>(1, 0, 0));
    grid.samples.push_back(rw::math::Vector3D<double>(0.5, 0, 0));
    // UI-T65——四态词表＋框 tint 透传断言载体（F-495 消费卡：协议面
    // 逐点状态经网关原样达渲染层；渲染分色在词表映射面）。
    grid.cellStates = {ui::View3DCellState::Good, ui::View3DCellState::NotSampled};
    grid.samples.push_back(rw::math::Vector3D<double>(0.7, 0, 0));
    update.boxOutline->tint = ui::View3DTint::Good;
    update.sampleGrid = grid;  // UI-T52——格线/采样点整组透传断言载体
    ASSERT_TRUE(gateway->applyPreview(update));
    EXPECT_TRUE(gateway->previewAttached());
    ASSERT_TRUE(fx.lastDrawn.has_value());
    ASSERT_EQ(fx.lastDrawn->frameMarkers.size(), std::size_t{1});
    EXPECT_EQ(fx.lastDrawn->frameMarkers[0].label, "工位 A");
    ASSERT_TRUE(fx.lastDrawn->sampleGrid.has_value());
    EXPECT_EQ(fx.lastDrawn->sampleGrid->gridLines.size(), std::size_t{1})
        << "格线段透传失真（UI-T52 acceptance 2）";
    ASSERT_EQ(fx.lastDrawn->sampleGrid->samples.size(), std::size_t{2});
    ASSERT_EQ(fx.lastDrawn->sampleGrid->cellStates.size(), std::size_t{2})
        << "逐点状态透传失真（F-495 三态面断裂）";
    EXPECT_EQ(fx.lastDrawn->sampleGrid->cellStates[0], ui::View3DCellState::Good);
    EXPECT_EQ(fx.lastDrawn->sampleGrid->cellStates[1],
              ui::View3DCellState::NotSampled)
        << "NotSampled 灰态透传失真（UI-T65 四态词表）";
    ASSERT_TRUE(fx.lastDrawn->boxOutline.has_value());
    ASSERT_TRUE(fx.lastDrawn->boxOutline->tint.has_value());
    EXPECT_EQ(*fx.lastDrawn->boxOutline->tint, ui::View3DTint::Good)
        << "框色映射档透传失真（UI-T65 覆盖率映射）";
    EXPECT_EQ(gateway->currentPreview().sampleGrid->gridLines.size(),
              std::size_t{1})
        << "观测值与绘制值不同源（currentPreview 观测面失守）";
    EXPECT_EQ(gateway->currentPreview().frameMarkers.size(), std::size_t{1});

    // ③removePreview 幂等（两次清除＝两次后端调用——语义直译）。
    gateway->removePreview();
    gateway->removePreview();
    EXPECT_FALSE(gateway->previewAttached());
    EXPECT_EQ(fx.previewClears, 2);

    // ④场景清除拍联动（onSceneCleared 对称收口——零残留纪律）。
    ASSERT_TRUE(gateway->applyPreview(update));
    gateway->onSceneCleared();
    EXPECT_FALSE(gateway->previewAttached())
        << "场景清除后预览残留（零残留纪律失守）";
}

/// 预览失败保持原状（后端 draw 失败＝false 且旧挂接/旧观测保持——
/// 事务语义与修订呈现同款）。
TEST(HostView3DGateway, PreviewOutlet_DrawFailureKeepsOld_UI_T33)
{
    IRD_TEST_INFO(std::vector<std::string>{"ERR-01"},
                  std::vector<std::string>{"UI-T33-ACC2"});
    GatewayHarness fx;
    bool drawOk = true;
    fx.previewDraw = [&drawOk](const ui::View3DPreviewUpdate&) { return drawOk; };
    auto gateway = fx.make();  // 替身字段前置后构造（Deps 冻结面）

    ui::View3DPreviewUpdate first;
    first.frameMarkers.push_back(ui::View3DFrameMarker{"首组", "World"});
    ASSERT_TRUE(gateway->applyPreview(first));

    drawOk = false;  // 第二次绘制失败
    ui::View3DPreviewUpdate second;
    second.frameMarkers.push_back(ui::View3DFrameMarker{"次组", "World"});
    EXPECT_FALSE(gateway->applyPreview(second));
    EXPECT_TRUE(gateway->previewAttached())
        << "失败后挂接态被清除（保持原状失守）";
    ASSERT_FALSE(gateway->currentPreview().frameMarkers.empty());
    EXPECT_EQ(gateway->currentPreview().frameMarkers.front().label, "首组")
        << "失败后观测值被覆盖（保留旧呈现失守）";
}
