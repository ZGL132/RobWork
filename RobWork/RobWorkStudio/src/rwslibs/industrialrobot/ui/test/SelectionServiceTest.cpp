/**
 * @file   SelectionServiceTest.cpp
 * @brief  选择服务（SelectionService——UI-T21，方案 B.1 §4.1/§4.2）模型层
 *         用例（QCoreApplication 级零 Widget——§12.1 第一层分工）：
 *         ①业务选中唯一写入口（去重/幂等/来源词表/无效身份拒绝——
 *         acceptance 2）；②联动 L2 高亮判定（已应用对象高亮出线/纯草稿
 *         对象无动作不报错——acceptance 3 反例）；③联动 L3 反解编排四
 *         具名分支（反解成功定位选中/反解失败树不动不报错/失败不伪造
 *         业务对象/树未命中不伪造——acceptance 4）；④INV-B4 呈现刷新
 *         选中处置（Keep/Clear/无选中无操作/刷新失败零触碰——B1-SPEC
 *         §3.3）；⑤订阅 RAII 语义（⑤事件端口形态）。
 *
 * 设计依据：
 *   - 任务契约 tasks/foundation/UI-T21.json acceptance 2/3/4/5；
 *   - B1-SPEC §4.1（选择唯一汇聚点＋来源四值封闭＋SA-05 反解端口）、
 *     §4.2（L1~L4——L1 基线＝树选→检查器刷新的联动面，本套件以替身
 *     观察者承载"检查器消费"；L2/L3 全分支）、§3.3（INV-B3/B4）；
 *   - units/ui.md §13 UI-T21 行、§3.1（"ui 测试以可控替身承载"惯例）；
 *   - RuntimePublishBridge.hpp（UI-T20 接缝——selectionDisposition
 *     AfterRefresh 首消费；呈现投影替身按其 isComplete 契约构造）。
 *
 * 为什么放在模型层：服务是零 Qt 纯状态机（契约头线程约束行）——全部
 *   行为分支可在无事件循环环境下穷举；Widget 呈现半区（树定位落点/选中
 *   信号回流）由 IndustrialProjectTreeGuiTest 以真实控件承载（同任务
 *   第三层分工）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/diagnostics/Catalog.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>
#include <sdurws/ird/ui/SelectionService.hpp>

#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace sdurws::ird;
using sdurws::ird::ui::IUiHighlightOutlet;
using sdurws::ird::ui::IUiPresentationRefreshObserver;
using sdurws::ird::ui::IUiRuntimeNameMapPort;
using sdurws::ird::ui::IUiSelectionObserver;
using sdurws::ird::ui::PresentationEventFacts;
using sdurws::ird::ui::PresentationViewProjection;
using sdurws::ird::ui::SelectionChange;
using sdurws::ird::ui::SelectionDisposition;
using sdurws::ird::ui::SelectionService;
using sdurws::ird::ui::SelectionSource;
using core::ObjectId;

// =====================================================================
// 测试替身（§3.1"ui 测试以可控替身承载"惯例——各 TU 独立声明）
// =====================================================================

/// 确定性身份派生（RuntimePublishBridgeTest 同款纪律——固定种子摘要的
/// 前 16 字节；测试值非产品路径，可复现优先）。
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

// =====================================================================
// 测试替身（§3.1"ui 测试以可控替身承载"惯例——各 TU 独立声明）
// =====================================================================

/// 名称映射替身（IUiRuntimeNameMapPort——双向映射由测试显式装配）。
class StubNameMap final : public IUiRuntimeNameMapPort {
public:
    /// 反解表（运行时名→ObjectId——L3 判定输入；查不到即反解失败分支）。
    std::map<std::string, ObjectId> reverse;
    /// 正向表（ObjectId 规范文本→运行时名——L2"已应用"判定输入）。
    std::map<std::string, std::string> forward;

    std::optional<ObjectId>
    resolveObjectIdFromRuntimeName(const std::string& runtimeName) const override
    {
        auto it = reverse.find(runtimeName);
        if (it == reverse.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    std::optional<std::string>
    resolveRuntimeName(const ObjectId& id) const override
    {
        auto it = forward.find(id.toCanonical());
        if (it == forward.end()) {
            return std::nullopt;
        }
        return it->second;
    }
};

/// 选择变更记录观察者（"检查器/域适配器"消费面的替身——UI-T22 消费
/// 形态的最小镜像：收到事件即登记，供断言广播内容与次数）。
class RecordingObserver final : public IUiSelectionObserver {
public:
    std::vector<SelectionChange> changes;

    void onSelectionChanged(const SelectionChange& change) override
    {
        changes.push_back(change);
    }
};

/// 高亮出口替身（L2 动作出线的记录面——高亮/清除调用台账）。
class StubHighlightOutlet final : public IUiHighlightOutlet {
public:
    std::vector<std::string> highlighted;  ///< highlightRuntimeObject 调用台账（运行时名序）
    int clearCalls = 0;                    ///< clearHighlight 调用计数

    void highlightRuntimeObject(const std::string& runtimeName) override
    {
        highlighted.push_back(runtimeName);
    }
    void clearHighlight() override { ++clearCalls; }
};

/// Dev 日志替身（留痕可观测面——通道行为断言非本套件重点，仅兜底非空）。
class StubDevLog final : public diagnostics::IDevLogSink {
public:
    std::vector<std::string> lines;

    void logDev(std::string_view channel, std::string message) override
    {
        lines.emplace_back(channel) += ": " + message;
    }
};

// =====================================================================
// 夹具（服务＋四替身的装配——每用例独立，零跨用例状态）
// =====================================================================

class SelectionServiceTest : public ::testing::Test {
protected:
    /// 生成一个有效 ObjectId（固定种子流——非零且用例间不冲突，失败
    /// 输出可读；计数从 1 起避免全零保留值）。
    static ObjectId makeId()
    {
        static int counter = 1;
        return idFrom<ObjectId>("sel-test-id-" + std::to_string(counter++));
    }

    /// 装配一台服务（nameMap/outlet/locator/devLog 由用例按需预填）。
    void SetUp() override
    {
        m_nameMap = std::make_shared<StubNameMap>();
        m_outlet = std::make_shared<StubHighlightOutlet>();
        m_devLog = std::make_shared<StubDevLog>();
    }

    /// 以可空出口构造服务（nullopt＝显式声明的无三维场景装配）。
    std::unique_ptr<SelectionService> makeService(
        std::shared_ptr<IUiHighlightOutlet> outlet = nullptr,
        std::function<bool(const ObjectId&)> locator = nullptr)
    {
        SelectionService::Deps deps;
        deps.nameMap = m_nameMap;
        deps.highlightOutlet = outlet ? std::move(outlet) : m_outlet;
        deps.devLog = m_devLog;
        if (locator) {
            deps.treeLocator = std::move(locator);
        } else {
            // 默认树定位：命中表内对象（测试按需改写 relocatorReturn）。
            deps.treeLocator = [this](const ObjectId& id) {
                m_locatorCalls.push_back(id);
                return m_locatorReturn;
            };
        }
        return std::make_unique<SelectionService>(std::move(deps));
    }

    /// 装配一台真实 L3 闭环服务（树定位＝直接回流 selectBusiness——
    /// 生产形态中该回调由面板实现并经 Qt 信号回流，此处以最小等价形
    /// 式承载同一写入路径：定位成功即以 ProjectTree 来源写入服务）。
    std::unique_ptr<SelectionService> makeLoopbackService()
    {
        SelectionService::Deps deps;
        deps.nameMap = m_nameMap;
        deps.highlightOutlet = m_outlet;
        deps.devLog = m_devLog;
        deps.treeLocator = [this, &service = m_loopback](const ObjectId& id) {
            m_locatorCalls.push_back(id);
            if (!m_locatorReturn || !service) {
                return false;
            }
            // 面板回流等价形：定位成功→树选中→服务写入（真实面板经 Qt
            // 信号到达同一入口——写入路径单一权威，INV-B3）。
            service->selectBusiness({id}, SelectionSource::ProjectTree);
            return true;
        };
        auto service = std::make_unique<SelectionService>(std::move(deps));
        m_loopback = service.get();
        return service;
    }

    /// 无效 ObjectId（全零保留值——INV-B1 重建/选中边界的拒绝对象）。
    static ObjectId invalidId() { return ObjectId{}; }

    std::shared_ptr<StubNameMap> m_nameMap;
    std::shared_ptr<StubHighlightOutlet> m_outlet;
    std::shared_ptr<StubDevLog> m_devLog;
    std::vector<ObjectId> m_locatorCalls;  ///< 树定位回调调用台账
    bool m_locatorReturn = true;           ///< 树定位回调返回值（分支④可置 false）
    SelectionService* m_loopback = nullptr;  ///< 回流服务（makeLoopbackService 装配）
};

// =====================================================================
// ①装配校验（构造期 fail-fast——acceptance 2 的端口义务面）
// =====================================================================

/// UI-SEL-1：缺名称映射端口的装配被拒绝（SA-05 反解唯一通道缺失＝L3
/// 承诺不可达——禁止构造违反 B1-SPEC §4.2 的服务）。
TEST_F(SelectionServiceTest, UI_SEL_1_DepRejectsMissingNameMap)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-02", "ARC-04"}, {});
    SelectionService::Deps deps;
    deps.nameMap = nullptr;
    deps.treeLocator = [](const ObjectId&) { return true; };
    EXPECT_THROW(
        [&] {
            SelectionService service(std::move(deps));
            (void)service;
        }(),
        std::invalid_argument);
}

/// UI-SEL-1：缺树定位回调的装配被拒绝（反解成功无处落点——B1-SPEC
/// §4.2 L3"项目树定位并选中"承诺不可达）。
TEST_F(SelectionServiceTest, UI_SEL_1_DepRejectsMissingTreeLocator)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-02", "ARC-04"}, {});
    SelectionService::Deps deps;
    deps.nameMap = std::make_shared<StubNameMap>();
    deps.treeLocator = nullptr;
    EXPECT_THROW(
        [&] {
            SelectionService service(std::move(deps));
            (void)service;
        }(),
        std::invalid_argument);
}

// =====================================================================
// ②业务选中写入口（acceptance 2——L1 联动基线）
// =====================================================================

/// UI-SEL-2：业务选中广播（L1 基线——树选→检查器刷新的联动面：替身
/// 观察者即"检查器消费"最小镜像，收到事件即具备刷新依据）。
TEST_F(SelectionServiceTest, UI_SEL_2_SelectBusinessBroadcastsDedupedStable)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-02"}, {});
    auto service = makeService();
    RecordingObserver inspector;
    auto sub = service->subscribe(inspector);

    const ObjectId a = makeId();
    const ObjectId b = makeId();
    // 重复元素（如 ctrl 多击同一对象）去重且保持首现序（NFR-COR-02）。
    service->selectBusiness({a, b, a}, SelectionSource::ProjectTree);

    ASSERT_EQ(inspector.changes.size(), std::size_t{1});
    const SelectionChange& change = inspector.changes.front();
    ASSERT_EQ(change.selectedObjectIds.size(), std::size_t{2});
    EXPECT_TRUE(change.selectedObjectIds[0] == a);
    EXPECT_TRUE(change.selectedObjectIds[1] == b);
    EXPECT_EQ(change.source, SelectionSource::ProjectTree);
    EXPECT_FALSE(change.runtimeOnly);
    // 查询面与事件同源（消费者按需拉取与推送一致）。
    ASSERT_EQ(service->selectedObjectIds().size(), std::size_t{2});
    ASSERT_TRUE(service->selectionSource().has_value());
    EXPECT_EQ(*service->selectionSource(), SelectionSource::ProjectTree);
}

/// UI-SEL-2：幂等选中不广播（同集合同来源＝无变化——订阅者只见真变化；
/// 仅运行时暂态存在时同集合写入仍生效〔作废暂态〕故不构成幂等）。
TEST_F(SelectionServiceTest, UI_SEL_2_SelectIdempotentNoBroadcast)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-02"}, {});
    auto service = makeService();
    RecordingObserver observer;
    auto sub = service->subscribe(observer);

    const ObjectId a = makeId();
    service->selectBusiness({a}, SelectionSource::Command);
    ASSERT_EQ(observer.changes.size(), std::size_t{1});
    service->selectBusiness({a}, SelectionSource::Command);
    EXPECT_EQ(observer.changes.size(), std::size_t{1});  // 幂等——无第二条

    // 来源变化是真变化（消费者可按来源分流）。
    service->selectBusiness({a}, SelectionSource::Search);
    EXPECT_EQ(observer.changes.size(), std::size_t{2});
}

/// UI-SEL-2：显式清空广播空选中且来源查询失效（清空是有意动作——
/// 事件携带空集与来源；无选中后来源查询不虚构残迹）。
TEST_F(SelectionServiceTest, UI_SEL_2_EmptySelectionClearsAndBroadcasts)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-02"}, {});
    auto service = makeService();
    RecordingObserver observer;
    auto sub = service->subscribe(observer);

    const ObjectId a = makeId();
    service->selectBusiness({a}, SelectionSource::View3DPick);
    service->clearSelection(SelectionSource::View3DPick);

    ASSERT_EQ(observer.changes.size(), std::size_t{2});
    const SelectionChange& clear = observer.changes.back();
    EXPECT_TRUE(clear.selectedObjectIds.empty());
    EXPECT_EQ(clear.source, SelectionSource::View3DPick);
    EXPECT_TRUE(service->selectedObjectIds().empty());
    EXPECT_FALSE(service->selectionSource().has_value());
}

/// UI-SEL-2：无效 ObjectId（全零保留值）整批拒绝且状态不变（伪造身份
/// 不收下——部分收下会静默污染选中集，调用方错误 fail-fast）。
TEST_F(SelectionServiceTest, UI_SEL_2_InvalidObjectIdRejected)
{
    IRD_TEST_INFO(std::vector<std::string>{"ARC-04"}, {});
    auto service = makeService();
    const ObjectId a = makeId();
    service->selectBusiness({a}, SelectionSource::ProjectTree);

    EXPECT_THROW(
        service->selectBusiness({invalidId()}, SelectionSource::Command),
        std::invalid_argument);
    // 拒绝后状态保持原选中（整批拒绝——无部分收下）。
    ASSERT_EQ(service->selectedObjectIds().size(), std::size_t{1});
    EXPECT_TRUE(service->selectedObjectIds().front() == a);
}

/// UI-SEL-2：来源词表四值封闭可用（项目树/三维拾取/搜索/命令——B1-SPEC
/// §4.1 原文词表逐值可写入并透传到事件）。
TEST_F(SelectionServiceTest, UI_SEL_2_SourceVocabularyClosed)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-02"}, {});
    auto service = makeService();
    RecordingObserver observer;
    auto sub = service->subscribe(observer);

    const std::pair<SelectionSource, const char*> cases[] = {
        {SelectionSource::ProjectTree, "项目树"},
        {SelectionSource::View3DPick, "三维拾取"},
        {SelectionSource::Search, "搜索"},
        {SelectionSource::Command, "命令"},
    };
    for (const auto& [source, label] : cases) {
        service->selectBusiness({makeId()}, source);
        ASSERT_FALSE(observer.changes.empty());
        EXPECT_EQ(observer.changes.back().source, source) << label;
    }
}

// =====================================================================
// ③联动 L2（acceptance 3——已应用对象高亮/纯草稿对象反例）
// =====================================================================

/// UI-SEL-3：已应用对象单选→高亮出线（正向存在性经 SA-05 端口判定，
/// 定位名随事件透传给出口——服务零三维知识）。
TEST_F(SelectionServiceTest, UI_SEL_3_L2_AppliedObjectHighlightsViaOutlet)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-02"}, {});
    const ObjectId applied = makeId();
    m_nameMap->forward[applied.toCanonical()] = "Frame_RobotBase";
    auto service = makeService();

    service->selectBusiness({applied}, SelectionSource::ProjectTree);

    ASSERT_EQ(m_outlet->highlighted.size(), std::size_t{1});
    EXPECT_EQ(m_outlet->highlighted.front(), "Frame_RobotBase");
}

/// UI-SEL-3（反例）：纯草稿/纯结果对象选中→无三维动作、不报错
/// （B1-SPEC §4.2 L2 原文——正向映射不存在＝合法二态；出口收到的是
/// 清除而非伪造高亮）。
TEST_F(SelectionServiceTest, UI_SEL_3_L2_DraftObjectNoActionNoError)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-02"}, {});
    const ObjectId draftOnly = makeId();  // forward 表无此项＝未进编译链
    auto service = makeService();

    service->selectBusiness({draftOnly}, SelectionSource::ProjectTree);

    EXPECT_EQ(m_outlet->highlighted.size(), std::size_t{0});  // 无高亮动作
    EXPECT_EQ(m_outlet->clearCalls, 1);                       // 保守清除
    SUCCEED();  // 走到此处即无异常抛出（不报错——反例分支的合法常态）
}

/// UI-SEL-3：多选清除高亮（第一版承诺只对单选给高亮语义——词表外形态
/// 保守清除，不擅自扩义）。
TEST_F(SelectionServiceTest, UI_SEL_3_L2_MultiSelectClearsHighlight)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-02"}, {});
    const ObjectId a = makeId();
    const ObjectId b = makeId();
    m_nameMap->forward[a.toCanonical()] = "Frame_A";
    m_nameMap->forward[b.toCanonical()] = "Frame_B";
    auto service = makeService();

    service->selectBusiness({a, b}, SelectionSource::ProjectTree);

    EXPECT_EQ(m_outlet->highlighted.size(), std::size_t{0});
    EXPECT_EQ(m_outlet->clearCalls, 1);
}

/// UI-SEL-3：无三维场景装配（出口显式缺省）→L2 判定照常、动作跳过、
/// 不报错（可空出口的显式声明纪律——不虚构通道）。
TEST_F(SelectionServiceTest, UI_SEL_3_L2_NoOutletAssemblySkipsAction)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-02"}, {});
    const ObjectId applied = makeId();
    m_nameMap->forward[applied.toCanonical()] = "Frame_X";
    // outlet=nullptr＋独立 devLog——显式声明的无三维场景。
    auto service = makeService(nullptr);

    ASSERT_NO_THROW(
        service->selectBusiness({applied}, SelectionSource::ProjectTree));
    EXPECT_FALSE(service->selectedObjectIds().empty());  // 选中照常生效
}

// =====================================================================
// ④联动 L3（acceptance 4——四个具名分支）
// =====================================================================

/// UI-SEL-4 案例①：反解成功→树定位并选中（树定位回调命中后，业务选中
/// 以 ProjectTree 来源生效——回调经面板回流 selectBusiness，本用例以
/// 回流等价形承载同一写入路径）。
TEST_F(SelectionServiceTest, UI_SEL_4_L3_Case1_ReverseSuccessLocatesAndSelects)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-02", "ARC-04"}, {});
    const ObjectId business = makeId();
    m_nameMap->reverse["Frame_J1"] = business;
    auto service = makeLoopbackService();
    RecordingObserver observer;
    auto sub = service->subscribe(observer);

    service->handleTreeViewFrameSelected("Frame_J1");

    // 树定位回调确被调用（树动了——"定位并选中"的回调半区）。
    ASSERT_EQ(m_locatorCalls.size(), std::size_t{1});
    EXPECT_TRUE(m_locatorCalls.front() == business);
    // 业务选中生效且来源为项目树（四值词表收敛点——契约头注释）。
    ASSERT_EQ(service->selectedObjectIds().size(), std::size_t{1});
    EXPECT_TRUE(service->selectedObjectIds().front() == business);
    ASSERT_TRUE(service->selectionSource().has_value());
    EXPECT_EQ(*service->selectionSource(), SelectionSource::ProjectTree);
    // 检查器（替身观察者）收到业务选中事件——L1 联动同步成立。
    ASSERT_EQ(observer.changes.size(), std::size_t{1});
    EXPECT_FALSE(observer.changes.front().runtimeOnly);
}

/// UI-SEL-4 案例②：反解失败→树不动、不报错（树定位回调零调用＝树零
/// 触碰；无异常抛出＝不报错——B1-SPEC §4.2 v1.1 合法二态）。
TEST_F(SelectionServiceTest, UI_SEL_4_L3_Case2_ReverseFailTreeUntouchedNoError)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-02", "ARC-04"}, {});
    auto service = makeLoopbackService();
    RecordingObserver observer;
    auto sub = service->subscribe(observer);

    // "Frame_HostBuilt"不在反解表＝框架自建 Frame、无业务对应。
    ASSERT_NO_THROW(service->handleTreeViewFrameSelected("Frame_HostBuilt"));

    EXPECT_TRUE(m_locatorCalls.empty());  // 树定位回调零调用＝树不动
    EXPECT_TRUE(service->selectedObjectIds().empty());  // 业务选中不变
}

/// UI-SEL-4 案例②续：反解失败保留『仅运行时对象』选择状态（记录运行时
/// 对象引用供状态呈现——查询面可取、事件以 runtimeOnly 形态广播）。
TEST_F(SelectionServiceTest, UI_SEL_4_L3_Case2b_ReverseFailKeepsRuntimeOnlyState)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-02"}, {});
    const ObjectId business = makeId();
    m_nameMap->reverse["Frame_J1"] = business;
    auto service = makeLoopbackService();
    RecordingObserver observer;
    auto sub = service->subscribe(observer);

    // 先有业务选中，再遇无对应物的运行时对象——业务选中保持不变。
    service->selectBusiness({business}, SelectionSource::ProjectTree);
    ASSERT_EQ(observer.changes.size(), std::size_t{1});

    service->handleTreeViewFrameSelected("Frame_HostCamera");

    // 暂态可查询（记录的是 RuntimeNameMap 键——呈现侧经端口取显示名）。
    EXPECT_TRUE(service->hasRuntimeOnlySelection());
    ASSERT_TRUE(service->runtimeOnlyObjectName().has_value());
    EXPECT_EQ(*service->runtimeOnlyObjectName(), "Frame_HostCamera");
    // 业务选中集保持不变（INV-B3：运行时暂态不侵蚀业务选中）。
    ASSERT_EQ(service->selectedObjectIds().size(), std::size_t{1});
    EXPECT_TRUE(service->selectedObjectIds().front() == business);
    // 广播 runtimeOnly 事件（状态呈现消费者据此呈现运行时选中）。
    ASSERT_EQ(observer.changes.size(), std::size_t{2});
    const SelectionChange& runtime = observer.changes.back();
    EXPECT_TRUE(runtime.runtimeOnly);
    EXPECT_EQ(runtime.runtimeObjectName, "Frame_HostCamera");
    EXPECT_TRUE(runtime.selectedObjectIds.empty());  // 非业务选中语义
}

/// UI-SEL-4 案例③：反解失败不创建、不映射到任何业务 ObjectId（结构性
/// 保证：失败路径没有 ObjectId 可写——业务选中集与来源逐位不变）。
TEST_F(SelectionServiceTest, UI_SEL_4_L3_Case3_ReverseFailCreatesNoBusinessObjectId)
{
    IRD_TEST_INFO(std::vector<std::string>{"ARC-04"}, {});
    const ObjectId business = makeId();
    m_nameMap->reverse["Frame_J1"] = business;
    auto service = makeLoopbackService();
    service->selectBusiness({business}, SelectionSource::ProjectTree);

    service->handleTreeViewFrameSelected("Frame_HostBuilt");

    // 无任何业务 ObjectId 被写入/创建（选中集逐位不变——零伪造）。
    ASSERT_EQ(service->selectedObjectIds().size(), std::size_t{1});
    EXPECT_TRUE(service->selectedObjectIds().front() == business);
    ASSERT_TRUE(service->selectionSource().has_value());
    EXPECT_EQ(*service->selectionSource(), SelectionSource::ProjectTree);
}

/// UI-SEL-4 案例④：反解成功但树未命中（内容漂移）→不伪造选中不动树
/// （"选中了不存在的节点"比不动作更糟——Dev 留痕、状态零变化）。
TEST_F(SelectionServiceTest, UI_SEL_4_L3_Case4_ResolvedButTreeMissNoFakeSelection)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-02"}, {});
    const ObjectId business = makeId();
    m_nameMap->reverse["Frame_J1"] = business;
    m_locatorReturn = false;  // 树报告：该对象不在当前树内容中
    auto service = makeLoopbackService();
    RecordingObserver observer;
    auto sub = service->subscribe(observer);

    service->handleTreeViewFrameSelected("Frame_J1");

    EXPECT_EQ(m_locatorCalls.size(), std::size_t{1});  // 定位尝试过
    EXPECT_TRUE(service->selectedObjectIds().empty());  // 未伪造选中
    EXPECT_TRUE(observer.changes.empty());              // 无广播
}

/// UI-SEL-4：空运行时名＝转发方契约违约（TreeView 事件不可能携带空
/// Frame 名——静默收下会掩盖装配缺陷，fail-fast）。
TEST_F(SelectionServiceTest, UI_SEL_4_L3_EmptyFrameNameRejected)
{
    IRD_TEST_INFO(std::vector<std::string>{"ARC-04"}, {});
    auto service = makeService();
    EXPECT_THROW(service->handleTreeViewFrameSelected(""),
                 std::invalid_argument);
}

// =====================================================================
// ⑤INV-B4 呈现刷新处置（UI-T20 接缝——selectionDispositionAfterRefresh
// 首消费；B1-SPEC §3.3"保持或置空，不静默换选"）
// =====================================================================

/// 构造一份完整呈现投影（objectExists 由用例注入——桥对账后的形态；
/// 三类身份以固定种子派生保证 isComplete()==true——对账面契约前提）。
static PresentationViewProjection viewWithExists(
    std::function<bool(const ObjectId&)> exists)
{
    PresentationViewProjection view;
    view.modelIdentity = idFrom<core::ContentIdentity>("sel-test-model");
    view.appliedRevisionId = idFrom<core::RevisionId>("sel-test-rev");
    view.presentationIdentity = ObjectId::generate();
    // 宿主载体（shared_ptr<const void> 契约形态——ui 不解引用；非空即
    // 完整位成立，测试无需真实呈现本体）。
    static int payloadTag = 0;
    view.hostPayload = std::shared_ptr<const void>(&payloadTag, [](const void*) {});
    view.objectExists = std::move(exists);
    return view;
}

/// UI-SEL-5：呈现重建后选中对象仍存在→Keep（保持选中、零广播——
/// "不静默换选"的正向半区）。
TEST_F(SelectionServiceTest, UI_SEL_5_InvB4_KeepWhenObjectExists)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-02"}, {});
    const ObjectId kept = makeId();
    auto service = makeService();
    service->selectBusiness({kept}, SelectionSource::ProjectTree);
    RecordingObserver observer;
    auto sub = service->subscribe(observer);

    service->onPresentationReplaced(
        viewWithExists([&kept](const ObjectId& id) { return id == kept; }));

    ASSERT_EQ(service->selectedObjectIds().size(), std::size_t{1});
    EXPECT_TRUE(service->selectedObjectIds().front() == kept);
    EXPECT_TRUE(observer.changes.empty());  // Keep＝状态零变化零广播
}

/// UI-SEL-5：呈现重建后选中对象消失→Clear（置空并广播空选中；失效呈现
/// 不静默换选——携带原来源便于消费者归因）。
TEST_F(SelectionServiceTest, UI_SEL_5_InvB4_ClearWhenObjectAbsent)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-02"}, {});
    const ObjectId gone = makeId();
    auto service = makeService();
    service->selectBusiness({gone}, SelectionSource::View3DPick);
    RecordingObserver observer;
    auto sub = service->subscribe(observer);

    service->onPresentationReplaced(
        viewWithExists([](const ObjectId&) { return false; }));

    EXPECT_TRUE(service->selectedObjectIds().empty());
    ASSERT_EQ(observer.changes.size(), std::size_t{1});
    const SelectionChange& change = observer.changes.front();
    EXPECT_TRUE(change.selectedObjectIds.empty());
    EXPECT_EQ(change.source, SelectionSource::View3DPick);  // 原来源归因
}

/// UI-SEL-5：呈现重建时本无选中→无操作（置空空集是恒等动作，不产生
/// 任何选中事件——UI-T20 SelectionDisposition 词表注释原文）。
TEST_F(SelectionServiceTest, UI_SEL_5_InvB4_NoSelectionIsNoOp)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-02"}, {});
    auto service = makeService();
    RecordingObserver observer;
    auto sub = service->subscribe(observer);

    service->onPresentationReplaced(
        viewWithExists([](const ObjectId&) { return false; }));

    EXPECT_TRUE(observer.changes.empty());
}

/// UI-SEL-5：呈现刷新失败→选中零触碰（INV-B4"不改变"在失败路径的
/// 延伸——呈现没换，处置无从谈起）。
TEST_F(SelectionServiceTest, UI_SEL_5_RefreshFailedLeavesSelectionUntouched)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-02"}, {});
    const ObjectId kept = makeId();
    auto service = makeService();
    service->selectBusiness({kept}, SelectionSource::ProjectTree);
    RecordingObserver observer;
    auto sub = service->subscribe(observer);

    PresentationEventFacts facts;
    facts.project = core::ProjectId{};
    facts.appliedRevision = core::RevisionId{};
    facts.modelIdentity = core::ContentIdentity{};
    service->onPresentationRefreshFailed(facts, "identity-mismatch");

    ASSERT_EQ(service->selectedObjectIds().size(), std::size_t{1});
    EXPECT_TRUE(service->selectedObjectIds().front() == kept);
    EXPECT_TRUE(observer.changes.empty());
}

// =====================================================================
// ⑥订阅 RAII 与暂态作废
// =====================================================================

/// UI-SEL-6：订阅句柄 RAII（退订后不再投递；句柄析构自动退订——core
/// ⑤事件总线同款语义）。
TEST_F(SelectionServiceTest, UI_SEL_6_SubscribeRaiiUnsubscribe)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-02"}, {});
    auto service = makeService();
    RecordingObserver observer;
    auto sub = service->subscribe(observer);

    service->selectBusiness({makeId()}, SelectionSource::Command);
    ASSERT_EQ(observer.changes.size(), std::size_t{1});

    sub->unsubscribe();
    service->selectBusiness({makeId()}, SelectionSource::Command);
    EXPECT_EQ(observer.changes.size(), std::size_t{1});  // 退订后不再投递
}

/// UI-SEL-6：业务选中生效即作废仅运行时暂态（两态互斥——用户意图的
/// 明确表达优先于运行时侧暂态记录）。
TEST_F(SelectionServiceTest, UI_SEL_6_RuntimeOnlySupersededByBusinessSelection)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-02"}, {});
    const ObjectId business = makeId();
    auto service = makeLoopbackService();

    service->handleTreeViewFrameSelected("Frame_HostCamera");
    ASSERT_TRUE(service->hasRuntimeOnlySelection());

    service->selectBusiness({business}, SelectionSource::Search);
    EXPECT_FALSE(service->hasRuntimeOnlySelection());
    EXPECT_FALSE(service->runtimeOnlyObjectName().has_value());
}

}  // namespace
