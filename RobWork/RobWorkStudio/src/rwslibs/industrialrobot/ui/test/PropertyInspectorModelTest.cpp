/**
 * @file   PropertyInspectorModelTest.cpp
 * @brief  UI-T22 模型层用例（QCoreApplication 级，§12.1 第一层）——共享
 *         属性检查器模型的编排行为：Provider 注册纪律（空/重复键
 *         fail-fast）、L1 全核查基线（选中→字段刷新——acceptance 1）、
 *         D5/D6 分野哨兵（超量字段整页拒绝而复杂编辑入口照常——
 *         acceptance 2）、域判定零入检查器（应答在域/first-wins/零命令
 *         网关——acceptance 3）、D6 激活编排（宿装/自持/违约词表）。
 *
 * 设计依据：
 *   - 任务契约 tasks/foundation/UI-T22.json acceptance 1~3（逐条对应，
 *     用例名带语义标注——AGENTS §2.7）；
 *   - B1-SPEC §2 D5（常用字段呈现面——单对象语义＋分野哨兵）、D6（复杂
 *     编辑不经检查器展开大批量字段——入口协议零字段内容）、§4.2 L1
 *     （项目树选中→检查器必须刷新）、§5.1（PropertyPagesProvider 协议
 *     形状——本套件即其冻结面的行为钉）；
 *   - units/ui.md §3.1（ui 测试以可控替身承载）、§4.2（右栏红线——
 *     检查器零命令网关的结构面）、§13 UI-T22 行；
 *   - 同构先例：SelectionServiceTest.cpp（idFrom 确定性身份/替身清单）、
 *     IndustrialProjectTreeModelTest.cpp（注册协议/拒绝词表用例形）。
 *
 * 模型/GUI 分工：编排逻辑与分野词表在本文件逐条断言；真实控件树的
 * 呈现行为（标题现取/入口按钮/宿装挂位）在
 * PropertyInspectorGuiTest.cpp 断言——两层对同一模型事实互证。
 */

#include <gtest/gtest.h>

#include <QWidget>  // 替身激活体 new QWidget(parent) 的完整类型——运行期
                    // 不在 QCoreApplication 级构造（产视图用例在 GUI 套件）

#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/diagnostics/Catalog.hpp>
#include <sdurws/ird/ui/FormEditCommon.hpp>
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
using sdurws::ird::ui::CommonFieldsPage;
using sdurws::ird::ui::ComplexPageActivationReport;
using sdurws::ird::ui::ComplexPageEntry;
using sdurws::ird::ui::IFormEditOutlet;
using sdurws::ird::ui::IUiPropertyPagesProvider;
using sdurws::ird::ui::InspectorContentKind;
using sdurws::ird::ui::InspectorFieldValue;
using sdurws::ird::ui::PropertyInspectorModel;
using sdurws::ird::ui::PropertyInspectorView;
using sdurws::ird::ui::QuantityFieldSpec;
using sdurws::ird::ui::SelectionChange;
using sdurws::ird::ui::SelectionSource;
using core::ObjectId;

// =====================================================================
// 测试替身（§3.1"ui 测试以可控替身承载"惯例——各 TU 独立声明）
// =====================================================================

/// 确定性身份派生（SelectionServiceTest 同款纪律——固定种子摘要前
/// 16 字节；测试值非产品路径，可复现优先）。
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

/// Dev 日志替身（留痕记录——分野哨兵/激活违约的 Dev 面断言锚）。
class RecordingDevLog final : public diagnostics::IDevLogSink {
public:
    std::vector<std::string> lines;

    void logDev(std::string_view, std::string message) override
    {
        lines.push_back(std::move(message));
    }
};

/// 编辑出口替身（域编辑器的最小测试面——确认应用移交登记）。
class RecordingOutlet final : public IFormEditOutlet {
public:
    int applyCalls = 0;

    void applyEdits(const sdurws::ird::ui::ParamEditSet&) override
    {
        ++applyCalls;
    }
};

/// 页面供给替身（IUiPropertyPagesProvider——应答表/入口表/激活行为
/// 全部由用例可编程）。
class StubPagesProvider final : public IUiPropertyPagesProvider {
public:
    explicit StubPagesProvider(std::string key)
        : m_key(std::move(key))
    {
    }

    std::string domainKey() const override { return m_key; }

    // 页面应答表（对象规范文本→页面；查不到＝非本域对象 nullopt——
    // 域判定在域的替身侧承载）。
    std::map<std::string, CommonFieldsPage> pages;
    // 入口声明表（对象规范文本→入口集；查不到＝空集）。
    std::map<std::string, std::vector<ComplexPageEntry>> entries;
    // 激活行为旋钮：hosted 页是否返回视图（false＝契约违约注入）；
    // 自持页是否违约回传视图。
    bool hostedReturnsWidget = true;
    bool externalMisbehaves = false;
    // 激活调用台账（编排面断言锚）。
    int activationCalls = 0;
    std::string lastActivatedKey;

    std::optional<CommonFieldsPage>
    commonFieldsPage(const ObjectId& object) const override
    {
        auto it = pages.find(object.toCanonical());
        if (it == pages.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    std::vector<ComplexPageEntry>
    complexPageEntries(const ObjectId& object) const override
    {
        auto it = entries.find(object.toCanonical());
        if (it == entries.end()) {
            return {};
        }
        return it->second;
    }

    ComplexPageActivationReport
    activateComplexPage(const ObjectId&, const std::string& pageKey,
                        QWidget* parent) override
    {
        ++activationCalls;
        lastActivatedKey = pageKey;
        ComplexPageActivationReport report;
        report.ok = true;
        if (hostedReturnsWidget) {
            // 以入参 parent 为父（Qt 父子树接管——协议原文义务）。
            report.hostedWidget = new QWidget(parent);
        } else if (externalMisbehaves) {
            // 自持页违约回传视图（activation-unexpected-widget 注入）。
            report.hostedWidget = new QWidget(parent);
        }
        return report;
    }

private:
    std::string m_key;
};

// =====================================================================
// 页面构造辅助（字段规格经 makeQuantityFieldSpec——单位一致性装配期
// 保证；长度量纲 m/mm 同 FormEditModelTest 先例）
// =====================================================================

/// 单字段规格（键为工程键——makeQuantityFieldSpec 校验通过的最小形）。
QuantityFieldSpec specForKey(const std::string& key)
{
    return sdurws::ird::ui::makeQuantityFieldSpec(
        key, "字段-" + key, core::QuantityKind::Length,
        *core::UnitToken::find("m"), *core::UnitToken::find("mm"));
}

/// n 字段页面（键 field-0..n-1——超量用例的机械生成）。
CommonFieldsPage pageWithFields(std::size_t n)
{
    CommonFieldsPage page;
    page.title = "常用参数";
    for (std::size_t i = 0; i < n; ++i) {
        page.fields.push_back(specForKey("field-" + std::to_string(i)));
    }
    page.values.push_back(InspectorFieldValue{"field-0", 0.5});
    return page;
}

// =====================================================================
// 夹具
// =====================================================================

class PropertyInspectorModelTest : public ::testing::Test {
protected:
    /// 生成一个有效 ObjectId（固定种子流——非零且用例间不冲突）。
    static ObjectId makeId()
    {
        static int counter = 1;
        return idFrom<ObjectId>("pin-test-id-" + std::to_string(counter++));
    }

    void SetUp() override
    {
        m_devLog = std::make_shared<RecordingDevLog>();
        m_model = std::make_unique<PropertyInspectorModel>(
            PropertyInspectorModel::Deps{m_devLog});
    }

    /// 业务选中一步（模型直接驱动——订阅链路由订阅用例单独覆盖）。
    void select(std::vector<ObjectId> ids)
    {
        SelectionChange change;
        change.selectedObjectIds = std::move(ids);
        change.source = SelectionSource::ProjectTree;
        m_model->onSelectionChanged(change);
    }

    std::shared_ptr<RecordingDevLog> m_devLog;
    std::unique_ptr<PropertyInspectorModel> m_model;
};

// =====================================================================
// Provider 注册纪律（装配期 fail-fast——AGENTS §3 调用方错误二分）
// =====================================================================

/// 空供给者注册被拒（静默收下会制造"注册成功"假象——fail-fast）。
TEST_F(PropertyInspectorModelTest, AddProviderRejectsNull_FailFast)
{
    EXPECT_THROW(m_model->addProvider(nullptr), std::invalid_argument);
    EXPECT_EQ(m_model->providerCount(), 0u);
}

/// 同域双供给源注册被拒（页面合并语义未定义——注册边界整批拒绝）。
TEST_F(PropertyInspectorModelTest, AddProviderRejectsDuplicateDomainKey_FailFast)
{
    m_model->addProvider(std::make_shared<StubPagesProvider>("modeling"));
    EXPECT_THROW(
        m_model->addProvider(std::make_shared<StubPagesProvider>("modeling")),
        std::invalid_argument);
    EXPECT_EQ(m_model->providerCount(), 1u);
}

// =====================================================================
// L1 全核查基线（acceptance 1——选中→字段刷新）
// =====================================================================

/// 无业务选中＝右栏常驻空态（占位词表态，零字段渲染）。
TEST_F(PropertyInspectorModelTest, NoSelectionPresentsPlaceholderState)
{
    select({});
    EXPECT_EQ(m_model->view().kind, InspectorContentKind::NoSelection);
    EXPECT_TRUE(m_model->view().fields.empty());
}

/// 多选中＝收窄提示态（D5 单对象语义——多选零字段渲染，分野第一面）。
TEST_F(PropertyInspectorModelTest, MultiSelectionPresentsNarrowHintWithoutFields)
{
    select({makeId(), makeId()});
    EXPECT_EQ(m_model->view().kind, InspectorContentKind::MultiSelection);
    EXPECT_TRUE(m_model->view().fields.empty());
    EXPECT_TRUE(m_model->view().complexEntries.empty());
}

/// L1 基线：单选中→字段刷新（页面值/出口/域键/标题逐项透传——检查器
/// 零加工零判定，acceptance 1 的主链路）。
TEST_F(PropertyInspectorModelTest, TreeSelectionRefreshesCommonFields_L1Baseline)
{
    const ObjectId link = makeId();
    auto provider = std::make_shared<StubPagesProvider>("modeling");
    RecordingOutlet outlet;
    CommonFieldsPage page;
    page.title = "连杆常用参数";
    page.fields.push_back(specForKey("link-mass"));
    page.values.push_back(InspectorFieldValue{"link-mass", 12.5});
    page.editOutlet = &outlet;
    provider->pages[link.toCanonical()] = page;
    m_model->addProvider(provider);

    select({link});
    const PropertyInspectorView& view = m_model->view();
    EXPECT_EQ(view.kind, InspectorContentKind::ObjectFields);
    EXPECT_TRUE(view.objectId == link);
    EXPECT_EQ(view.domainKey, "modeling");
    EXPECT_EQ(view.title, "连杆常用参数");
    ASSERT_EQ(view.fields.size(), 1u);
    EXPECT_EQ(view.fields[0].key, "link-mass");
    ASSERT_EQ(view.values.size(), 1u);
    ASSERT_TRUE(view.values[0].siValue.has_value());
    EXPECT_DOUBLE_EQ(*view.values[0].siValue, 12.5);
    EXPECT_EQ(view.editOutlet, &outlet);
}

/// 刷新语义：选中切到另一对象→字段页随之刷新（非首刷钉死——L1"必须
/// 刷新为该对象的常用字段视图"）。
TEST_F(PropertyInspectorModelTest, SelectionSwitchRefreshesToNewObjectFields)
{
    const ObjectId a = makeId();
    const ObjectId b = makeId();
    auto provider = std::make_shared<StubPagesProvider>("modeling");
    CommonFieldsPage pageA = pageWithFields(1);
    pageA.title = "对象A";
    CommonFieldsPage pageB = pageWithFields(2);
    pageB.title = "对象B";
    provider->pages[a.toCanonical()] = pageA;
    provider->pages[b.toCanonical()] = pageB;
    m_model->addProvider(provider);

    select({a});
    EXPECT_EQ(m_model->view().objectId, a);
    select({b});
    EXPECT_EQ(m_model->view().kind, InspectorContentKind::ObjectFields);
    EXPECT_EQ(m_model->view().objectId, b);
    EXPECT_EQ(m_model->view().title, "对象B");
    ASSERT_EQ(m_model->view().fields.size(), 2u);
}

/// 仅运行时对象选中（L3 反解失败暂态）→ 呈现状态零触碰（业务选中集
/// 未变——运行时暂态不剥夺既有业务字段呈现）。
TEST_F(PropertyInspectorModelTest, RuntimeOnlySelectionKeepsPresentationUntouched)
{
    const ObjectId link = makeId();
    auto provider = std::make_shared<StubPagesProvider>("modeling");
    provider->pages[link.toCanonical()] = pageWithFields(1);
    m_model->addProvider(provider);
    select({link});
    const PropertyInspectorView before = m_model->view();

    SelectionChange runtimeOnly;
    runtimeOnly.runtimeOnly = true;
    runtimeOnly.runtimeObjectName = "Frame7";
    m_model->onSelectionChanged(runtimeOnly);

    const PropertyInspectorView& after = m_model->view();
    EXPECT_EQ(after.kind, before.kind);
    EXPECT_TRUE(after.objectId == before.objectId);
    EXPECT_EQ(after.fields.size(), before.fields.size());
}

/// 选中对象无域应答＝诚实占位（迁移期常态——不虚构字段不报错，
/// B1-SPEC §5.2 双形态并存的检查器侧承载）。
TEST_F(PropertyInspectorModelTest, ObjectWithoutProviderAnswerPresentsHonestPlaceholder)
{
    const ObjectId unmigrated = makeId();
    auto provider = std::make_shared<StubPagesProvider>("modeling");
    m_model->addProvider(provider);

    select({unmigrated});
    EXPECT_EQ(m_model->view().kind, InspectorContentKind::NoProviderAnswer);
    EXPECT_TRUE(m_model->view().fields.empty());
    EXPECT_TRUE(m_model->view().complexEntries.empty());
}

/// 订阅链路：模型订阅真实 SelectionService 后，selectBusiness 驱动字段
/// 刷新（L1 全链路除面板外段——树面板→服务广播→模型刷新）。
TEST_F(PropertyInspectorModelTest, SubscriptionToSelectionServiceDrivesRefresh)
{
    const ObjectId link = makeId();
    auto provider = std::make_shared<StubPagesProvider>("modeling");
    provider->pages[link.toCanonical()] = pageWithFields(1);
    m_model->addProvider(provider);

    // 具名空替身（两向恒 nullopt——本用例不走 L2/L3，只走选中广播）。
    class NullNameMap final : public sdurws::ird::ui::IUiRuntimeNameMapPort {
    public:
        std::optional<ObjectId> resolveObjectIdFromRuntimeName(
            const std::string&) const override
        {
            return std::nullopt;
        }
        std::optional<std::string>
        resolveRuntimeName(const ObjectId&) const override
        {
            return std::nullopt;
        }
    };
    sdurws::ird::ui::SelectionService::Deps deps;
    deps.nameMap = std::make_shared<NullNameMap>();
    deps.treeLocator = [](const ObjectId&) { return true; };
    sdurws::ird::ui::SelectionService service(deps);
    auto subscription = service.subscribe(*m_model);

    service.selectBusiness({link}, SelectionSource::ProjectTree);
    EXPECT_EQ(m_model->view().kind, InspectorContentKind::ObjectFields);
    EXPECT_TRUE(m_model->view().objectId == link);
}

// =====================================================================
// 域判定零入检查器（acceptance 3——应答在域/first-wins/编排零域表）
// =====================================================================

/// 对象归属由 Provider 自答：首注册者在重叠应答时胜出（检查器零域判定
/// 表——注册序即裁决序，NFR-COR-02）。
TEST_F(PropertyInspectorModelTest, FirstRegisteredProviderWinsOnOverlap)
{
    const ObjectId shared = makeId();
    auto first = std::make_shared<StubPagesProvider>("modeling");
    auto second = std::make_shared<StubPagesProvider>("kinematics");
    CommonFieldsPage page;
    page.title = "建模供给";
    page.fields.push_back(specForKey("field-0"));
    first->pages[shared.toCanonical()] = page;
    CommonFieldsPage page2;
    page2.title = "运动学供给";
    page2.fields.push_back(specForKey("field-1"));
    second->pages[shared.toCanonical()] = page2;
    m_model->addProvider(first);
    m_model->addProvider(second);

    select({shared});
    EXPECT_EQ(m_model->view().kind, InspectorContentKind::ObjectFields);
    EXPECT_EQ(m_model->view().domainKey, "modeling");
    EXPECT_EQ(m_model->view().title, "建模供给");
}

/// 复杂编辑入口只取应答域：非应答域声明的入口不并入（他域入口出现在
/// 本对象呈现面＝域边界泄漏）。
TEST_F(PropertyInspectorModelTest, ComplexEntriesOnlyFromAnsweringProvider)
{
    const ObjectId owned = makeId();
    auto answerer = std::make_shared<StubPagesProvider>("modeling");
    auto outsider = std::make_shared<StubPagesProvider>("kinematics");
    answerer->pages[owned.toCanonical()] = pageWithFields(1);
    ComplexPageEntry own;
    own.pageKey = "solve-config";
    own.title = "求解配置";
    answerer->entries[owned.toCanonical()] = {own};
    ComplexPageEntry foreign;
    foreign.pageKey = "foreign-page";
    foreign.title = "他域页面";
    outsider->entries[owned.toCanonical()] = {foreign};
    m_model->addProvider(answerer);
    m_model->addProvider(outsider);

    select({owned});
    ASSERT_EQ(m_model->view().complexEntries.size(), 1u);
    EXPECT_EQ(m_model->view().complexEntries[0].pageKey, "solve-config");
}

/// 纯入口形态：无常用字段但有复杂编辑入口＝合法对象形态（kind 落
/// ObjectFields、字段集空——面板对空 fields 隐藏表区，入口照常）。
TEST_F(PropertyInspectorModelTest, PureEntriesFormObjectFieldsWithoutTable)
{
    const ObjectId config = makeId();
    auto provider = std::make_shared<StubPagesProvider>("kinematics");
    ComplexPageEntry entry;
    entry.pageKey = "solve-config";
    entry.title = "求解配置";
    entry.hosted = true;
    provider->entries[config.toCanonical()] = {entry};
    m_model->addProvider(provider);

    select({config});
    EXPECT_EQ(m_model->view().kind, InspectorContentKind::ObjectFields);
    EXPECT_TRUE(m_model->view().fields.empty());
    ASSERT_EQ(m_model->view().complexEntries.size(), 1u);
}

// =====================================================================
// D5/D6 分野哨兵（acceptance 2——大批量字段进不了常用字段通道，而
// 复杂编辑入口照常：两通道互不遮蔽）
// =====================================================================

/// 分野哨兵主用例：17 字段页整页拒绝（"复杂编辑不经检查器展开大批量
/// 字段"的数据面）且已声明入口照常呈现（大批量编辑有 D6 通道可去）。
TEST_F(PropertyInspectorModelTest,
       OverLimitCommonFieldsRejectedByDivergenceSentinel_D5D6)
{
    const ObjectId bulk = makeId();
    auto provider = std::make_shared<StubPagesProvider>("modeling");
    provider->pages[bulk.toCanonical()] = pageWithFields(17);  // > 16 哨兵
    ComplexPageEntry entry;
    entry.pageKey = "dh-parameters";
    entry.title = "DH 参数";
    entry.hosted = true;
    provider->entries[bulk.toCanonical()] = {entry};
    m_model->addProvider(provider);

    select({bulk});
    EXPECT_EQ(m_model->view().kind, InspectorContentKind::PageRejected);
    EXPECT_EQ(m_model->view().rejectReason, "common-fields-over-limit");
    // 拒绝页零字段（拒绝优于残缺——不裁剪不补页）。
    EXPECT_TRUE(m_model->view().fields.empty());
    // 复杂编辑入口照常（两通道互不遮蔽——分野不剥夺正确通道）。
    ASSERT_EQ(m_model->view().complexEntries.size(), 1u);
    EXPECT_EQ(m_model->view().complexEntries[0].pageKey, "dh-parameters");
    // Dev 留痕可观测（拒绝不静默）。
    EXPECT_FALSE(m_devLog->lines.empty());
}

/// 哨兵边界内恰好通过：16 字段页正常呈现（上限值本身合法）。
TEST_F(PropertyInspectorModelTest, LimitBoundaryFieldsPageAccepted)
{
    const ObjectId full = makeId();
    auto provider = std::make_shared<StubPagesProvider>("modeling");
    provider->pages[full.toCanonical()] = pageWithFields(16);
    m_model->addProvider(provider);

    select({full});
    EXPECT_EQ(m_model->view().kind, InspectorContentKind::ObjectFields);
    EXPECT_EQ(m_model->view().fields.size(), 16u);
    EXPECT_TRUE(m_devLog->lines.empty());
}

/// 空页供给违约：声称有页面（标题非空）却给不出字段→整页拒绝。
TEST_F(PropertyInspectorModelTest, EmptyFieldsPageWithPageDataRejected)
{
    const ObjectId empty = makeId();
    auto provider = std::make_shared<StubPagesProvider>("modeling");
    CommonFieldsPage page;
    page.title = "空页";
    provider->pages[empty.toCanonical()] = page;
    m_model->addProvider(provider);

    select({empty});
    EXPECT_EQ(m_model->view().kind, InspectorContentKind::PageRejected);
    EXPECT_EQ(m_model->view().rejectReason, "empty-common-fields");
    EXPECT_FALSE(m_devLog->lines.empty());
}

/// 基线键闭合检查：values 出现 fields 未声明的键→整页拒绝（数据无法
/// 寻址注入——不一致比缺数据更危险）。
TEST_F(PropertyInspectorModelTest, BaselineUnknownKeyRejected)
{
    const ObjectId mismatched = makeId();
    auto provider = std::make_shared<StubPagesProvider>("modeling");
    CommonFieldsPage page = pageWithFields(1);
    page.values.push_back(InspectorFieldValue{"ghost-key", 1.0});
    provider->pages[mismatched.toCanonical()] = page;
    m_model->addProvider(provider);

    select({mismatched});
    EXPECT_EQ(m_model->view().kind, InspectorContentKind::PageRejected);
    EXPECT_EQ(m_model->view().rejectReason, "baseline-key-unknown");
}

/// 入口键唯一性：同键双入口＝声明违约（激活寻址不确定——整页拒绝）。
TEST_F(PropertyInspectorModelTest, DuplicateComplexPageKeyRejected)
{
    const ObjectId dup = makeId();
    auto provider = std::make_shared<StubPagesProvider>("modeling");
    ComplexPageEntry a;
    a.pageKey = "same-key";
    a.title = "入口甲";
    ComplexPageEntry b;
    b.pageKey = "same-key";
    b.title = "入口乙";
    provider->pages[dup.toCanonical()] = pageWithFields(1);
    provider->entries[dup.toCanonical()] = {a, b};
    m_model->addProvider(provider);

    select({dup});
    EXPECT_EQ(m_model->view().kind, InspectorContentKind::PageRejected);
    EXPECT_EQ(m_model->view().rejectReason, "duplicate-page-key");
    // 拒绝态入口集清空（违约集不呈现——避免呈现不可寻址的按钮）。
    EXPECT_TRUE(m_model->view().complexEntries.empty());
}

// =====================================================================
// 只读事实透传（P-UI-6——检查器呈现事实不拥有门控规则）
// =====================================================================

/// 只读页：editOutlet 收口（只读页不提供编辑移交面）＋readOnly 事实
/// 原样透传（检查器不判定不调和——放行权威归 project 命令边界）。
TEST_F(PropertyInspectorModelTest, ReadOnlyPageSuppressesEditOutlet)
{
    const ObjectId locked = makeId();
    auto provider = std::make_shared<StubPagesProvider>("modeling");
    RecordingOutlet outlet;
    CommonFieldsPage page = pageWithFields(1);
    page.readOnly = true;
    page.editOutlet = &outlet;
    provider->pages[locked.toCanonical()] = page;
    m_model->addProvider(provider);

    select({locked});
    EXPECT_EQ(m_model->view().kind, InspectorContentKind::ObjectFields);
    EXPECT_TRUE(m_model->view().readOnly);
    EXPECT_EQ(m_model->view().editOutlet, nullptr);
}

/// 纯呈现页：可写但无出口＝合法（apply 由面板禁用——不虚构可用性）。
TEST_F(PropertyInspectorModelTest, WritablePageWithoutOutletIsLegal)
{
    const ObjectId displayOnly = makeId();
    auto provider = std::make_shared<StubPagesProvider>("modeling");
    CommonFieldsPage page = pageWithFields(1);
    page.editOutlet = nullptr;
    provider->pages[displayOnly.toCanonical()] = page;
    m_model->addProvider(provider);

    select({displayOnly});
    EXPECT_EQ(m_model->view().kind, InspectorContentKind::ObjectFields);
    EXPECT_FALSE(m_model->view().readOnly);
    EXPECT_EQ(m_model->view().editOutlet, nullptr);
}

// =====================================================================
// 入口声明序与 D6 激活编排（acceptance 2）
// =====================================================================

/// 入口集按声明序呈现（Provider 供给序＝呈现序——NFR-COR-02）。
TEST_F(PropertyInspectorModelTest, ComplexEntriesListedInDeclarationOrder)
{
    const ObjectId obj = makeId();
    auto provider = std::make_shared<StubPagesProvider>("modeling");
    ComplexPageEntry dh;
    dh.pageKey = "dh-parameters";
    dh.title = "DH 参数";
    ComplexPageEntry mass;
    mass.pageKey = "mass-properties";
    mass.title = "物性";
    ComplexPageEntry region;
    region.pageKey = "region-define";
    region.title = "区域定义";
    provider->pages[obj.toCanonical()] = pageWithFields(1);
    provider->entries[obj.toCanonical()] = {dh, mass, region};
    m_model->addProvider(provider);

    select({obj});
    ASSERT_EQ(m_model->view().complexEntries.size(), 3u);
    EXPECT_EQ(m_model->view().complexEntries[0].pageKey, "dh-parameters");
    EXPECT_EQ(m_model->view().complexEntries[1].pageKey, "mass-properties");
    EXPECT_EQ(m_model->view().complexEntries[2].pageKey, "region-define");
}

/// 自持激活：hosted=false 页返回空视图且 ok（域自行打开编辑视图——
/// 检查器零宿装，D6"域面板内"形态）。不产视图的激活在 QCoreApplication
/// 级可测；产视图的激活用例在 GUI 套件（QApplication 级——QWidget 构造
/// 前置）。
TEST_F(PropertyInspectorModelTest, ExternalActivationReportsOkWithoutWidget)
{
    const ObjectId obj = makeId();
    auto provider = std::make_shared<StubPagesProvider>("modeling");
    ComplexPageEntry entry;
    entry.pageKey = "import-wizard";
    entry.title = "导入向导";
    entry.hosted = false;
    provider->pages[obj.toCanonical()] = pageWithFields(1);
    provider->entries[obj.toCanonical()] = {entry};
    provider->hostedReturnsWidget = false;  // 自持页：不回传视图
    m_model->addProvider(provider);
    select({obj});

    ComplexPageActivationReport report =
        m_model->activateComplexPage(obj, "import-wizard", nullptr);
    EXPECT_TRUE(report.ok);
    EXPECT_EQ(report.hostedWidget, nullptr);
    EXPECT_EQ(provider->activationCalls, 1);
}

/// 未知页键激活被编排面拒绝（不越声明集转调——调用方违约防御）。
TEST_F(PropertyInspectorModelTest, ActivationUnknownPageKeyRejected)
{
    const ObjectId obj = makeId();
    auto provider = std::make_shared<StubPagesProvider>("modeling");
    ComplexPageEntry entry;
    entry.pageKey = "dh-parameters";
    entry.title = "DH 参数";
    entry.hosted = true;
    provider->pages[obj.toCanonical()] = pageWithFields(1);
    provider->entries[obj.toCanonical()] = {entry};
    m_model->addProvider(provider);
    select({obj});

    ComplexPageActivationReport report =
        m_model->activateComplexPage(obj, "not-declared", nullptr);
    EXPECT_FALSE(report.ok);
    EXPECT_EQ(report.reason, "activation-unknown-page");
    EXPECT_EQ(provider->activationCalls, 0);
}

/// 无应答态激活被拒（占位态没有应答域可转调——不按旧页存根激活）。
TEST_F(PropertyInspectorModelTest, ActivationOnUnownedObjectRejected)
{
    const ObjectId ghost = makeId();
    ComplexPageActivationReport report =
        m_model->activateComplexPage(ghost, "any-page", nullptr);
    EXPECT_FALSE(report.ok);
    EXPECT_EQ(report.reason, "activation-object-not-owned");
}

/// 宿装页返回空视图＝Provider 契约违约（声明宿装却给不出视图——
/// 拒绝采纳＋Dev 留痕）。不产视图（本用例 Provider 恒返空）——
/// QCoreApplication 级可测；违约回传视图的用例在 GUI 套件。
TEST_F(PropertyInspectorModelTest, HostedActivationNullWidgetRejectedAsProviderBreach)
{
    const ObjectId obj = makeId();
    auto provider = std::make_shared<StubPagesProvider>("modeling");
    ComplexPageEntry entry;
    entry.pageKey = "dh-parameters";
    entry.title = "DH 参数";
    entry.hosted = true;
    provider->pages[obj.toCanonical()] = pageWithFields(1);
    provider->entries[obj.toCanonical()] = {entry};
    provider->hostedReturnsWidget = false;  // 违约注入：hosted 页给空
    m_model->addProvider(provider);
    select({obj});

    ComplexPageActivationReport report =
        m_model->activateComplexPage(obj, "dh-parameters", nullptr);
    EXPECT_FALSE(report.ok);
    EXPECT_EQ(report.reason, "activation-hosted-null-widget");
    EXPECT_FALSE(m_devLog->lines.empty());
}

}  // namespace
