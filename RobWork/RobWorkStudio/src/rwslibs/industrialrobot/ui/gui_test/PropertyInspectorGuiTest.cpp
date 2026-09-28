/**
 * @file   PropertyInspectorGuiTest.cpp
 * @brief  UI-T22 GUI 层用例（QApplication＋真实 Widget 树——§12.1 第三
 *         层）：共享属性检查器面板的呈现行为——分态占位文案（冻结词表）、
 *         标题显示名经名称端口现取（失败占位零拼接——UX-02）、常用字段
 *         区内嵌参数表面板（FormEditCommon 构件复用——acceptance 3）、
 *         复杂编辑入口按钮与宿装挂位（D6）、D5/D6 分野的呈现面证据
 *         （拒绝页占位＋入口照常）、运行时暂态零剥夺。
 *
 * 设计依据：
 *   - 任务契约 tasks/foundation/UI-T22.json acceptance 1~3（GUI 冒烟
 *     留痕的具名用例面）；
 *   - B1-SPEC §2 D5（常用字段呈现面——单对象语义）、D6（复杂编辑页
 *     宿装/自持两形态）、§4.2 L1（选中→检查器刷新的呈现末端）；
 *   - units/ui.md §13 UI-T22 行、§12.2 执行纪律、§3.1（替身承载惯例）；
 *   - 同构先例：IndustrialProjectTreeGuiTest.cpp（夹具形态/替身清单/
 *     名称现取断言）。
 *
 * 模型/GUI 分工：编排逻辑与分野词表在 PropertyInspectorModelTest.cpp
 * 逐条断言；本文件断言真实控件树的行为面（占位/标题/入口按钮/宿装
 * 容器/参数表面板挂位）。产视图的激活编排用例在本层（QWidget 构造
 * 前置 QApplication——模型层 QCoreApplication 级禁建 Widget）。
 */

#include <gtest/gtest.h>

#include <QApplication>
#include <QLabel>
#include <QObject>
#include <QPushButton>
#include <QWidget>

#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/diagnostics/Catalog.hpp>
#include <sdurws/ird/ui/FormEditCommon.hpp>
#include <sdurws/ird/ui/PropertyInspector.hpp>
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
using sdurws::ird::ui::InspectorContentKind;
using sdurws::ird::ui::InspectorFieldValue;
using sdurws::ird::ui::PropertyInspectorModel;
using sdurws::ird::ui::QuantityFieldSpec;
using sdurws::ird::ui::SelectionChange;
using sdurws::ird::ui::SelectionSource;
using core::ObjectId;

// =====================================================================
// 测试替身（各 TU 独立声明——模型套件同款纪律）
// =====================================================================

/// 确定性身份派生（SelectionServiceTest 同款）。
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

/// 名称解析替身（C-11——标题显示名现取的数据源；未命中＝解析失败分支）。
class StubNameResolver final : public sdurws::ird::ui::IUiNameResolver {
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

/// 页面供给替身（模型套件同形——应答表/入口表/激活旋钮可编程）。
class StubPagesProvider final : public sdurws::ird::ui::IUiPropertyPagesProvider {
public:
    explicit StubPagesProvider(std::string key)
        : m_key(std::move(key))
    {
    }

    std::string domainKey() const override { return m_key; }

    std::map<std::string, CommonFieldsPage> pages;
    std::map<std::string, std::vector<ComplexPageEntry>> entries;
    bool hostedReturnsWidget = true;
    bool externalMisbehaves = false;
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
            report.hostedWidget = new QWidget(parent);
        } else if (externalMisbehaves) {
            report.hostedWidget = new QWidget(parent);
        }
        return report;
    }

private:
    std::string m_key;
};

// =====================================================================
// 夹具
// =====================================================================

class PropertyInspectorGuiTest : public ::testing::Test {
protected:
    /// 生成一个有效 ObjectId（固定种子流）。
    static ObjectId makeId()
    {
        static int counter = 1;
        return idFrom<ObjectId>("pin-gui-id-" + std::to_string(counter++));
    }

    void SetUp() override
    {
        m_model = std::make_shared<PropertyInspectorModel>(
            PropertyInspectorModel::Deps{});
        m_resolver = std::make_shared<StubNameResolver>();
    }

    /// 构建面板（deps 校验经工厂——替身齐备的常规路径）；根容器 show
    /// 后断言（isVisible 依赖祖先链显式呈现——ParamTablePanelGuiTest
    /// 夹具同款纪律）。
    std::unique_ptr<sdurws::ird::ui::PropertyInspectorPanel> makePanel()
    {
        sdurws::ird::ui::PropertyInspectorPanelDeps deps;
        deps.model = m_model;
        deps.nameResolver = m_resolver;
        auto panel = sdurws::ird::ui::createPropertyInspectorPanel(deps, nullptr);
        panel->widget()->show();
        return panel;
    }

    /// 业务选中一步（直接驱动模型——订阅链路由模型套件覆盖）。
    void select(std::vector<ObjectId> ids)
    {
        SelectionChange change;
        change.selectedObjectIds = std::move(ids);
        change.source = SelectionSource::ProjectTree;
        m_model->onSelectionChanged(change);
    }

    /// 单字段页面（长度量纲 m/mm——FormEditModelTest 同款最小形）。
    static CommonFieldsPage pageWithFields(std::size_t n)
    {
        CommonFieldsPage page;
        page.title = "常用参数";
        for (std::size_t i = 0; i < n; ++i) {
            const std::string key = "field-" + std::to_string(i);
            page.fields.push_back(sdurws::ird::ui::makeQuantityFieldSpec(
                key, "字段-" + key, core::QuantityKind::Length,
                *core::UnitToken::find("m"), *core::UnitToken::find("mm")));
        }
        page.values.push_back(InspectorFieldValue{"field-0", 0.5});
        return page;
    }

    /// 根容器内按 objectName 找控件（GUI 断言的寻址原语）。
    QWidget* findWidget(QWidget* root, const QString& name) const
    {
        return root->findChild<QWidget*>(name);
    }

    std::shared_ptr<PropertyInspectorModel> m_model;
    std::shared_ptr<StubNameResolver> m_resolver;
};

// =====================================================================
// 分态占位呈现（冻结文案词表的呈现面证据）
// =====================================================================

/// 无选中：占位标签呈现"未选中对象"（常驻态——零字段区）。
TEST_F(PropertyInspectorGuiTest, EmptySelectionShowsNoSelectionPlaceholder)
{
    auto panel = makePanel();
    QWidget* root = panel->widget();
    panel->refresh();

    auto* placeholder = root->findChild<QLabel*>(
        QStringLiteral("ird_property_inspector_placeholder"));
    ASSERT_NE(placeholder, nullptr);
    EXPECT_EQ(placeholder->text().toStdString(),
              sdurws::ird::ui::kInspectorNoSelectionText);
    EXPECT_FALSE(findWidget(root, "ird_property_inspector_fields")->isVisible());
}

/// 多选中：收窄提示占位（D5 单对象语义的呈现面——零字段渲染）。
TEST_F(PropertyInspectorGuiTest, MultiSelectionShowsNarrowHintPlaceholder)
{
    auto panel = makePanel();
    QWidget* root = panel->widget();
    select({makeId(), makeId()});
    panel->refresh();

    auto* placeholder = root->findChild<QLabel*>(
        QStringLiteral("ird_property_inspector_placeholder"));
    ASSERT_NE(placeholder, nullptr);
    EXPECT_EQ(placeholder->text().toStdString(),
              sdurws::ird::ui::kInspectorMultiSelectionText);
}

/// 仅运行时对象选中：暂态提示占位（L3 反解失败透传——业务呈现由模型
/// 零触碰，面板按"零选中后的暂态"呈现；本用例从空态进入）。
TEST_F(PropertyInspectorGuiTest, RuntimeOnlyFromEmptyShowsRuntimePlaceholder)
{
    auto panel = makePanel();
    QWidget* root = panel->widget();
    SelectionChange runtimeOnly;
    runtimeOnly.runtimeOnly = true;
    runtimeOnly.runtimeObjectName = "Frame7";
    m_model->onSelectionChanged(runtimeOnly);
    panel->refresh();

    // 模型零触碰：空态保持——占位仍是"未选中对象"（运行时暂态不改写
    // 业务呈现态，模型套件已断言零触碰；此处钉呈现面一致性）。
    auto* placeholder = root->findChild<QLabel*>(
        QStringLiteral("ird_property_inspector_placeholder"));
    ASSERT_NE(placeholder, nullptr);
    EXPECT_EQ(placeholder->text().toStdString(),
              sdurws::ird::ui::kInspectorNoSelectionText);
}

/// 无域应答：诚实占位"所属域未接入"（迁移期常态——不虚构字段）。
TEST_F(PropertyInspectorGuiTest, UnansweredObjectShowsHonestPlaceholder)
{
    auto panel = makePanel();
    QWidget* root = panel->widget();
    m_model->addProvider(std::make_shared<StubPagesProvider>("modeling"));
    select({makeId()});
    panel->refresh();

    auto* placeholder = root->findChild<QLabel*>(
        QStringLiteral("ird_property_inspector_placeholder"));
    ASSERT_NE(placeholder, nullptr);
    EXPECT_EQ(placeholder->text().toStdString(),
              sdurws::ird::ui::kInspectorNoProviderAnswerText);
}

// =====================================================================
// 对象应答态呈现（标题现取＋参数表面板构件复用——acceptance 3）
// =====================================================================

/// 选中对象：标题显示名经名称端口现取＋字段区内嵌参数表面板（FormEdit
/// Common 构件——ird_param_table objectName 即其复用锚）。
TEST_F(PropertyInspectorGuiTest,
       SelectionShowsResolvedTitleAndEmbeddedParamTable)
{
    const ObjectId link = makeId();
    m_resolver->names[link.toCanonical()] = "连杆-1";
    auto provider = std::make_shared<StubPagesProvider>("modeling");
    provider->pages[link.toCanonical()] = pageWithFields(2);
    m_model->addProvider(provider);
    select({link});

    auto panel = makePanel();
    QWidget* root = panel->widget();
    panel->refresh();

    auto* title = root->findChild<QLabel*>(
        QStringLiteral("ird_property_inspector_title"));
    ASSERT_NE(title, nullptr);
    EXPECT_EQ(title->text().toUtf8().toStdString(), "连杆-1");
    EXPECT_TRUE(title->isVisible());

    // 字段区已挂参数表面板（UI-T08 构件复用——公共编辑规则零第二实现）。
    auto* table = root->findChild<QWidget*>(
        QStringLiteral("ird_param_table"));
    ASSERT_NE(table, nullptr);
    EXPECT_TRUE(table->isVisible());
}

/// 名称解析失败：标题呈现占位、零拼接（UX-02/R-4——"（未命名对象）"
/// 与树面板同款诚实呈现）。
TEST_F(PropertyInspectorGuiTest, UnresolvableObjectShowsUnnamedTitlePlaceholder)
{
    const ObjectId unknown = makeId();  // 名称表未登记——解析失败分支
    auto provider = std::make_shared<StubPagesProvider>("modeling");
    provider->pages[unknown.toCanonical()] = pageWithFields(1);
    m_model->addProvider(provider);
    select({unknown});

    auto panel = makePanel();
    QWidget* root = panel->widget();
    panel->refresh();

    auto* title = root->findChild<QLabel*>(
        QStringLiteral("ird_property_inspector_title"));
    ASSERT_NE(title, nullptr);
    EXPECT_EQ(title->text().toUtf8().toStdString(), "（未命名对象）");
}

/// 只读页：只读徽标呈现（P-UI-6 事实透传——检查器呈现不判定）。
TEST_F(PropertyInspectorGuiTest, ReadOnlyPageShowsReadOnlyBadge)
{
    const ObjectId locked = makeId();
    auto provider = std::make_shared<StubPagesProvider>("modeling");
    CommonFieldsPage page = pageWithFields(1);
    page.readOnly = true;
    provider->pages[locked.toCanonical()] = page;
    m_model->addProvider(provider);
    select({locked});

    auto panel = makePanel();
    QWidget* root = panel->widget();
    panel->refresh();

    auto* badge = root->findChild<QLabel*>(
        QStringLiteral("ird_property_inspector_readonly"));
    ASSERT_NE(badge, nullptr);
    EXPECT_TRUE(badge->isVisible());
    EXPECT_EQ(badge->text().toStdString(),
              sdurws::ird::ui::kInspectorReadOnlyBadgeText);
}

// =====================================================================
// 复杂编辑入口与宿装（D6——acceptance 2 的呈现/宿装半区）
// =====================================================================

/// 入口按钮按声明集呈现（钮面＝入口 title——零字段内容可渲染）。
TEST_F(PropertyInspectorGuiTest, ComplexEntriesRenderedAsButtonsWithTitles)
{
    const ObjectId obj = makeId();
    auto provider = std::make_shared<StubPagesProvider>("modeling");
    provider->pages[obj.toCanonical()] = pageWithFields(1);
    ComplexPageEntry dh;
    dh.pageKey = "dh-parameters";
    dh.title = "DH 参数";
    dh.hosted = true;
    ComplexPageEntry mass;
    mass.pageKey = "mass-properties";
    mass.title = "物性";
    mass.hosted = false;
    provider->entries[obj.toCanonical()] = {dh, mass};
    m_model->addProvider(provider);
    select({obj});

    auto panel = makePanel();
    QWidget* root = panel->widget();
    panel->refresh();

    auto* dhButton = root->findChild<QPushButton*>(
        QStringLiteral("ird_property_inspector_complex_entry_dh-parameters"));
    auto* massButton = root->findChild<QPushButton*>(
        QStringLiteral("ird_property_inspector_complex_entry_mass-properties"));
    ASSERT_NE(dhButton, nullptr);
    ASSERT_NE(massButton, nullptr);
    EXPECT_EQ(dhButton->text().toUtf8().toStdString(), "DH 参数");
    EXPECT_EQ(massButton->text().toUtf8().toStdString(), "物性");
}

/// 宿装激活：点击 hosted 入口→视图挂入宿装容器（容器可见）。
TEST_F(PropertyInspectorGuiTest, HostedEntryClickMountsWidgetIntoHostArea)
{
    const ObjectId obj = makeId();
    auto provider = std::make_shared<StubPagesProvider>("modeling");
    provider->pages[obj.toCanonical()] = pageWithFields(1);
    ComplexPageEntry dh;
    dh.pageKey = "dh-parameters";
    dh.title = "DH 参数";
    dh.hosted = true;
    provider->entries[obj.toCanonical()] = {dh};
    m_model->addProvider(provider);
    select({obj});

    auto panel = makePanel();
    QWidget* root = panel->widget();
    panel->refresh();

    auto* dhButton = root->findChild<QPushButton*>(
        QStringLiteral("ird_property_inspector_complex_entry_dh-parameters"));
    ASSERT_NE(dhButton, nullptr);
    dhButton->click();  // 编排链路：按钮→模型→Provider→宿装容器

    auto* host = findWidget(
        root, QStringLiteral("ird_property_inspector_complex_host"));
    ASSERT_NE(host, nullptr);
    EXPECT_TRUE(host->isVisible());
    EXPECT_EQ(provider->activationCalls, 1);
    EXPECT_EQ(provider->lastActivatedKey, "dh-parameters");
}

/// 自持激活：点击非宿装入口→Provider 被调、宿装容器保持隐藏（域自行
/// 打开编辑视图——检查器零宿装）。
TEST_F(PropertyInspectorGuiTest, ExternalEntryClickLeavesHostAreaHidden)
{
    const ObjectId obj = makeId();
    auto provider = std::make_shared<StubPagesProvider>("modeling");
    provider->pages[obj.toCanonical()] = pageWithFields(1);
    ComplexPageEntry wizard;
    wizard.pageKey = "import-wizard";
    wizard.title = "导入向导";
    wizard.hosted = false;
    provider->entries[obj.toCanonical()] = {wizard};
    provider->hostedReturnsWidget = false;  // 自持页不回传视图
    m_model->addProvider(provider);
    select({obj});

    auto panel = makePanel();
    QWidget* root = panel->widget();
    panel->refresh();

    auto* wizardButton = root->findChild<QPushButton*>(
        QStringLiteral("ird_property_inspector_complex_entry_import-wizard"));
    ASSERT_NE(wizardButton, nullptr);
    wizardButton->click();

    EXPECT_EQ(provider->activationCalls, 1);
    auto* host = findWidget(
        root, QStringLiteral("ird_property_inspector_complex_host"));
    ASSERT_NE(host, nullptr);
    EXPECT_FALSE(host->isVisible());
}

/// 自持页违约回传视图：编排拒绝采纳（activation-unexpected-widget——
/// 模型层词表在 GUI 面的表现＝宿装容器不挂任何视图、保持隐藏）。
TEST_F(PropertyInspectorGuiTest,
       ExternalActivationReturningWidgetRejectedAsProviderBreach)
{
    const ObjectId obj = makeId();
    auto provider = std::make_shared<StubPagesProvider>("modeling");
    provider->pages[obj.toCanonical()] = pageWithFields(1);
    ComplexPageEntry wizard;
    wizard.pageKey = "import-wizard";
    wizard.title = "导入向导";
    wizard.hosted = false;
    provider->entries[obj.toCanonical()] = {wizard};
    provider->hostedReturnsWidget = false;
    provider->externalMisbehaves = true;  // 违约注入：自持页回传视图
    m_model->addProvider(provider);
    select({obj});

    auto panel = makePanel();
    QWidget* root = panel->widget();
    panel->refresh();

    auto* wizardButton = root->findChild<QPushButton*>(
        QStringLiteral("ird_property_inspector_complex_entry_import-wizard"));
    ASSERT_NE(wizardButton, nullptr);
    wizardButton->click();

    EXPECT_FALSE(
        m_model->activateComplexPage(obj, "import-wizard", nullptr).ok);
    auto* host = findWidget(
        root, QStringLiteral("ird_property_inspector_complex_host"));
    ASSERT_NE(host, nullptr);
    EXPECT_FALSE(host->isVisible());
}

// =====================================================================
// D5/D6 分野的呈现面证据（acceptance 2）
// =====================================================================

/// 分野主用例（呈现面）：超量字段页拒绝→防御占位呈现且复杂编辑入口
/// 照常（两通道互不遮蔽的可见形态）。
TEST_F(PropertyInspectorGuiTest,
       RejectedPageShowsDefensivePlaceholderAndKeepsEntries)
{
    const ObjectId bulk = makeId();
    auto provider = std::make_shared<StubPagesProvider>("modeling");
    provider->pages[bulk.toCanonical()] = pageWithFields(17);  // > 16 哨兵
    ComplexPageEntry dh;
    dh.pageKey = "dh-parameters";
    dh.title = "DH 参数";
    dh.hosted = true;
    provider->entries[bulk.toCanonical()] = {dh};
    m_model->addProvider(provider);
    select({bulk});

    auto panel = makePanel();
    QWidget* root = panel->widget();
    panel->refresh();

    auto* placeholder = root->findChild<QLabel*>(
        QStringLiteral("ird_property_inspector_placeholder"));
    ASSERT_NE(placeholder, nullptr);
    EXPECT_TRUE(placeholder->isVisible());
    EXPECT_EQ(placeholder->text().toStdString(),
              sdurws::ird::ui::kInspectorPageRejectedText);
    // 无参数表面板（拒绝页零字段渲染——大批量字段进不了检查器）。
    EXPECT_EQ(root->findChild<QWidget*>(QStringLiteral("ird_param_table")),
              nullptr);
    // 入口照常（D6 通道不遮蔽）。
    auto* dhButton = root->findChild<QPushButton*>(
        QStringLiteral("ird_property_inspector_complex_entry_dh-parameters"));
    ASSERT_NE(dhButton, nullptr);
    EXPECT_TRUE(dhButton->isVisible());
}

/// 选中切换清宿装：对象 A 宿装页挂出后切到对象 B→宿装容器清空隐藏
/// （呈现面不残留已失效对象的宿装页——INV-B3 零留存的呈现面延伸）。
TEST_F(PropertyInspectorGuiTest, SelectionSwitchClearsStaleHostedPage)
{
    const ObjectId a = makeId();
    const ObjectId b = makeId();
    auto provider = std::make_shared<StubPagesProvider>("modeling");
    provider->pages[a.toCanonical()] = pageWithFields(1);
    provider->pages[b.toCanonical()] = pageWithFields(1);
    ComplexPageEntry dh;
    dh.pageKey = "dh-parameters";
    dh.title = "DH 参数";
    dh.hosted = true;
    provider->entries[a.toCanonical()] = {dh};
    provider->entries[b.toCanonical()] = {dh};
    m_model->addProvider(provider);
    select({a});

    auto panel = makePanel();
    QWidget* root = panel->widget();
    panel->refresh();
    auto* dhButton = root->findChild<QPushButton*>(
        QStringLiteral("ird_property_inspector_complex_entry_dh-parameters"));
    ASSERT_NE(dhButton, nullptr);
    dhButton->click();
    auto* host = findWidget(
        root, QStringLiteral("ird_property_inspector_complex_host"));
    ASSERT_NE(host, nullptr);
    EXPECT_TRUE(host->isVisible());  // A 的宿装页已挂出

    select({b});  // 选中切换——A 的宿装事实失效
    panel->refresh();
    EXPECT_FALSE(host->isVisible());
}

/// 工厂缺依赖 fail-fast（装配缺陷构造期拒绝——无模型/无名称端口）。
TEST_F(PropertyInspectorGuiTest, FactoryRejectsEmptyDeps_FailFast)
{
    sdurws::ird::ui::PropertyInspectorPanelDeps empty;
    EXPECT_THROW(sdurws::ird::ui::createPropertyInspectorPanel(empty, nullptr),
                 std::invalid_argument);
}

}  // namespace
