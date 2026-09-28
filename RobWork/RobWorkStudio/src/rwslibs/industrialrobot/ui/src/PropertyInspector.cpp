/**
 * @file   PropertyInspector.cpp
 * @brief  共享属性检查器实现（UI-T22）——模型半区（Provider 注册编排＋
 *         L1 刷新＋D5/D6 分野哨兵＋D6 激活编排；零 Qt）＋面板半区（模型
 *         状态的 Widget 呈现；FormEditCommon 参数表面板构件复用）。
 *
 * 设计依据：见 PropertyInspector.hpp 文件头（契约 acceptance 1~3、
 *   B1-SPEC §2 D5/D6/§4.2 L1/§5.1、units/ui.md §4.2/§13 UI-T22 行；
 *   knownPitfalls P-UI-6/O-43）。本 TU 只补实现侧的纪律注释，语义以
 *   契约头为准。
 *
 * 线程模型：全部入口 UI 线程（ui.md §3.4 M-1）；面板构建经工厂在 UI
 *   线程执行。非线程安全。
 */

#include <sdurws/ird/ui/PropertyInspector.hpp>

#include <algorithm>
#include <stdexcept>
#include <utility>

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

namespace sdurws {
namespace ird {
namespace ui {

namespace {

/// 显示名解析失败的标题占位（UX-02：解析失败显示占位、不拼接名称——
/// IndustrialProjectTree.cpp kUnnamedNodeLabel 同款文案单点；过渡文案随
/// P-DIAG-9 迁资源文件时只换值源）。
constexpr const char* kUnnamedObjectTitle = "（未命名对象）";

/// Dev 留痕的通道前缀（同单元 TU 惯例——检索定位用，非用户文本）。
constexpr const char* kDevChannel = "ird.ui.property-inspector";

}  // namespace

// =====================================================================
// 模型半区：构造与 Provider 注册
// =====================================================================

PropertyInspectorModel::PropertyInspectorModel(Deps deps)
    : m_deps(std::move(deps))
{
}

void PropertyInspectorModel::addProvider(
    std::shared_ptr<IUiPropertyPagesProvider> provider)
{
    // 调用方契约违约 fail-fast（AGENTS §3 二分）：空 Provider 与重复
    // domainKey 都是装配期缺陷——同域双供给源的"页面合并语义"未定义
    // （两页都收会双份字段、取一会静默丢数据），注册边界直接拒绝，
    // 与 ProjectTreeModel::addProvider 同款纪律。
    if (!provider) {
        throw std::invalid_argument(
            "PropertyInspectorModel::addProvider: provider 为空");
    }
    const std::string key = provider->domainKey();
    const bool duplicated = std::any_of(
        m_providers.begin(), m_providers.end(),
        [&key](const std::shared_ptr<IUiPropertyPagesProvider>& registered) {
            return registered->domainKey() == key;
        });
    if (duplicated) {
        throw std::invalid_argument(
            "PropertyInspectorModel::addProvider: domainKey 重复注册——" + key);
    }
    // 注册序＝询问序（first-wins 的裁决序，NFR-COR-02 稳定）。
    m_providers.push_back(std::move(provider));
}

std::size_t PropertyInspectorModel::providerCount() const noexcept
{
    return m_providers.size();
}

// =====================================================================
// 模型半区：L1 刷新（选中→字段刷新——acceptance 1 的全核查基线）
// =====================================================================

void PropertyInspectorModel::onSelectionChanged(const SelectionChange& change)
{
    // 仅运行时对象选中（L3 反解失败暂态）：业务选中集未变——呈现状态
    // 零触碰。运行时对象没有业务编辑字段（B1-SPEC §4.2 L3"不伪造业务
    // 对象"），既有业务对象的字段页不应被一个运行时暂态剥夺；运行时
    // 选中的呈现归状态呈现侧（状态栏），检查器只认业务选中。
    if (change.runtimeOnly) {
        return;
    }

    // 无业务选中：右栏常驻空态（合法态，不是错误）；宿装残留由面板
    // refresh() 按 kind 清空——模型只管事实源。
    if (change.selectedObjectIds.empty()) {
        m_view = PropertyInspectorView{};
        m_view.kind = InspectorContentKind::NoSelection;
        return;
    }

    // 多选中：D5 呈现语义是"当前选中对象"（单数）——多选不展开任何
    // 字段（零字段渲染），提示收窄选择。多选"挑第一个展开"会制造
    // "呈现了没选的那个"的歧义，保守占位是唯一无歧义动作。
    if (change.selectedObjectIds.size() > 1) {
        m_view = PropertyInspectorView{};
        m_view.kind = InspectorContentKind::MultiSelection;
        return;
    }

    // 业务选中恰一对象：按注册序询问 Provider 组装（first-wins——
    // 域判定在域，检查器零判定表）。
    assembleForObject(change.selectedObjectIds.front());
}

void PropertyInspectorModel::assembleForObject(const core::ObjectId& object)
{
    // 重置呈现态（对象切换必须整体替换——上一对象的字段/入口/宿装事实
    // 不残留，INV-B3"零选中留存"的呈现面延伸）。
    PropertyInspectorView next;
    next.objectId = object;

    // 按注册序询问：首个"应答"的 Provider 胜出。应答＝常用字段页有值
    // **或**复杂编辑入口非空——允许"无常用字段但有复杂编辑入口"的对象
    // 形态（如纯配置对象只有求解配置收口页）；两问皆空＝非本域对象，
    // 顺延询问下一注册者。
    IUiPropertyPagesProvider* winner = nullptr;
    std::vector<ComplexPageEntry> entries;
    for (const auto& provider : m_providers) {
        std::optional<CommonFieldsPage> page = provider->commonFieldsPage(object);
        std::vector<ComplexPageEntry> providerEntries =
            provider->complexPageEntries(object);
        if (page.has_value() || !providerEntries.empty()) {
            winner = provider.get();
            m_answerIndex =
                static_cast<std::size_t>(&provider - m_providers.data());
            // 胜出域的入口集重取一次？不需要——上面现取的即胜出者供给
            // （现取现拼，同一次询问的两次调用间域侧状态不变是 Provider
            // 契约的应有之义；为对账留痕只在重复键检查用）。
            entries = std::move(providerEntries);
            next.domainKey = winner->domainKey();
            if (page.has_value()) {
                next.title = page->title;
                next.readOnly = page->readOnly;
                next.fields = std::move(page->fields);
                next.values = std::move(page->values);
                // 只读页不提供编辑移交面（保守收口——两事实矛盾时以
                // 只读为准，P-UI-6：检查器呈现事实、不调和语义）。
                next.editOutlet = page->readOnly ? nullptr : page->editOutlet;
            }
            break;
        }
    }

    // 无任何域应答：迁移期常态（B1-SPEC §5.2 双形态并存——未迁移域的
    // 对象如实呈现"暂无"，不虚构字段不报错）。宿主挂位后随域迁移逐批
    // 充实，占位文案即迁移进度的呈现面。
    if (winner == nullptr) {
        next.kind = InspectorContentKind::NoProviderAnswer;
        m_hasAnswer = false;
        m_view = std::move(next);
        return;
    }
    m_hasAnswer = true;

    // 入口键唯一性检查（声明违约——同键双入口的激活寻址不确定，整页
    // 拒绝优于随机命中）。
    bool entryKeysUnique = true;
    for (std::size_t i = 0; entryKeysUnique && i < entries.size(); ++i) {
        for (std::size_t j = i + 1; j < entries.size(); ++j) {
            if (entries[i].pageKey == entries[j].pageKey) {
                entryKeysUnique = false;
                break;
            }
        }
    }
    if (!entryKeysUnique) {
        next.kind = InspectorContentKind::PageRejected;
        next.rejectReason = "duplicate-page-key";
        emitDevLine("page rejected: duplicate-page-key (object="
                    + object.toCanonical() + ", domain=" + next.domainKey + ")");
        m_view = std::move(next);
        return;
    }
    next.complexEntries = std::move(entries);

    // 常用字段页通道的完整性三查（acceptance 2 分野哨兵的数据面；任一
    // 违约即整页拒绝——拒绝优于残缺，原因 token 走 Dev 留痕，用户面只见
    // 工程化占位。拒绝**不剥夺**复杂编辑入口：分野两通道互不遮蔽，
    // 大批量编辑始终有 D6 通道可去）。
    if (next.fields.empty()) {
        // 无字段：仅"纯入口形态"合法（Provider 以空页＋非空入口应答——
        // 页面数据全默认，纯收口页对象）；声称有页面（title/values/
        // outlet/readOnly 任一有值）却给不出字段＝空页供给违约。
        const bool pureEntriesForm =
            next.title.empty() && next.values.empty()
            && next.editOutlet == nullptr && !next.readOnly;
        if (pureEntriesForm) {
            // 合法：kind 取 ObjectFields（字段区由面板对空 fields 隐藏，
            // 入口区照常呈现）。
            next.kind = InspectorContentKind::ObjectFields;
        } else {
            next.kind = InspectorContentKind::PageRejected;
            next.rejectReason = "empty-common-fields";
            next.editOutlet = nullptr;
            emitDevLine("page rejected: empty-common-fields (object="
                        + object.toCanonical() + ", domain="
                        + next.domainKey + ")");
        }
    } else if (next.fields.size() > kMaxCommonFieldsPerObject) {
        // D5/D6 分野哨兵：大批量字段塞进常用字段通道＝违约——大批量
        // 编辑必须走 D6 复杂编辑页通道（acceptance 2 分野断言的触发面）。
        next.kind = InspectorContentKind::PageRejected;
        next.rejectReason = "common-fields-over-limit";
        next.fields.clear();
        next.values.clear();
        next.editOutlet = nullptr;
        emitDevLine("page rejected: common-fields-over-limit (object="
                    + object.toCanonical() + ", domain="
                    + next.domainKey + ")");
    } else {
        // 基线键闭合检查（基线无法寻址注入＝数据不一致，整页拒绝）。
        std::string reject;
        for (const InspectorFieldValue& value : next.values) {
            const bool known = std::any_of(
                next.fields.begin(), next.fields.end(),
                [&value](const QuantityFieldSpec& spec) {
                    return spec.key == value.key;
                });
            if (!known) {
                reject = "baseline-key-unknown";
                break;
            }
        }
        if (reject.empty()) {
            next.kind = InspectorContentKind::ObjectFields;
        } else {
            next.kind = InspectorContentKind::PageRejected;
            next.rejectReason = reject;
            next.fields.clear();
            next.values.clear();
            next.editOutlet = nullptr;
            emitDevLine("page rejected: " + reject + " (object="
                        + object.toCanonical() + ", domain="
                        + next.domainKey + ")");
        }
    }

    m_view = std::move(next);
}

const PropertyInspectorView& PropertyInspectorModel::view() const noexcept
{
    return m_view;
}

// =====================================================================
// 模型半区：D6 激活编排（入口按钮→模型→应答域 Provider）
// =====================================================================

ComplexPageActivationReport PropertyInspectorModel::activateComplexPage(
    const core::ObjectId& object, const std::string& pageKey, QWidget* parent)
{
    ComplexPageActivationReport report;

    // 编排①：呈现态须为对象应答态且对象一致——占位态没有应答域可转调；
    // 对象错配说明调用方持的是过期按钮（选中已切换），一律拒绝不按旧页
    // 存根激活（激活必须落在当前呈现事实上）。
    const bool objectPresent =
        (m_view.kind == InspectorContentKind::ObjectFields
         || m_view.kind == InspectorContentKind::PageRejected)
        && m_view.objectId == object && m_hasAnswer;
    if (!objectPresent) {
        report.reason = "activation-object-not-owned";
        emitDevLine("activation rejected: activation-object-not-owned (key="
                    + pageKey + ")");
        return report;
    }

    // 编排②：pageKey 须在该对象已声明入口集内——面板只可能传递入口按钮
    // 绑定的键，未知键＝调用方违约（防御性拒绝，不越声明集转调）。
    const ComplexPageEntry* entry = nullptr;
    for (const ComplexPageEntry& declared : m_view.complexEntries) {
        if (declared.pageKey == pageKey) {
            entry = &declared;
            break;
        }
    }
    if (entry == nullptr) {
        report.reason = "activation-unknown-page";
        emitDevLine("activation rejected: activation-unknown-page (key="
                    + pageKey + ")");
        return report;
    }

    // 编排③：转调应答域 Provider，并核对宿装/自持形态与返回值一致——
    // 声明与行为错位属 Provider 契约违约（呈现侧拒绝采纳，Dev 留痕）。
    IUiPropertyPagesProvider& provider = *m_providers[m_answerIndex];
    report = provider.activateComplexPage(object, pageKey, parent);
    if (entry->hosted && report.hostedWidget == nullptr) {
        report.ok = false;
        report.hostedWidget = nullptr;
        report.reason = "activation-hosted-null-widget";
        emitDevLine("activation rejected: activation-hosted-null-widget (key="
                    + pageKey + ")");
        return report;
    }
    if (!entry->hosted && report.hostedWidget != nullptr) {
        // 自持页却回传视图：检查器不宿装不接管（视图由 Provider 以
        // parent 创建的话已挂父子树——呈现面零采纳，违约交由 Dev 留痕
        // 与验收对抗项处理，不做内存善后——善后即接管，恰恰越界）。
        report.ok = false;
        report.hostedWidget = nullptr;
        report.reason = "activation-unexpected-widget";
        emitDevLine("activation rejected: activation-unexpected-widget (key="
                    + pageKey + ")");
        return report;
    }
    return report;
}

void PropertyInspectorModel::emitDevLine(const std::string& message) const
{
    if (m_deps.devLog) {
        m_deps.devLog->logDev(kDevChannel, message);
    }
}

// =====================================================================
// 面板半区：模型状态的 Widget 呈现（Qt——R-3 例外面）
// =====================================================================

namespace {

/**
 * @brief 面板实现（工厂外封闭——R-2；零 Q_OBJECT：信号接线全经 lambda，
 *        AUTOMOC 口径不变——IndustrialProjectTreePanel 同款）。
 *
 * 布局（objectName 锚——GUI 测试与样式定位）：
 *   - 根容器 ird_property_inspector（纵排）；
 *   - 标题行：显示名（ird_property_inspector_title，nameResolver 现取）
 *     ＋只读徽标（ird_property_inspector_readonly，事实透传按需可见）；
 *   - 占位标签 ird_property_inspector_placeholder（分态文案；
 *     PageRejected 时附拒绝提示）；
 *   - 常用字段区容器 ird_property_inspector_fields（ObjectFields 时内嵌
 *     FormEditCommon 参数表面板——acceptance 3 构件复用：公共编辑规则
 *     零第二实现）；
 *   - 复杂编辑区（ird_property_inspector_complex）：节标题＋入口按钮列
 *     （ird_property_inspector_complex_entries）＋宿装容器
 *     （ird_property_inspector_complex_host——hosted 页视图挂位）。
 */
class PropertyInspectorPanelImpl final : public PropertyInspectorPanel {
public:
    PropertyInspectorPanelImpl(const PropertyInspectorPanelDeps& deps,
                               QWidget* parent)
        : m_deps(deps),
          m_root(new QWidget(parent)),
          m_title(new QLabel(m_root)),
          m_readOnlyBadge(new QLabel(m_root)),
          m_pageTitle(new QLabel(m_root)),
          m_placeholder(new QLabel(m_root)),
          m_fieldsHost(new QWidget(m_root)),
          m_complexSection(new QLabel(m_root)),
          m_entriesHost(new QWidget(m_root)),
          m_pageHost(new QWidget(m_root))
    {
        // 纵排主布局：标题行/占位/字段区/复杂区自上而下（右栏窄面版式
        // ——O-43：不动 Dock 拓扑，本面板只是右栏内容的一个构件）。
        auto* mainLayout = new QVBoxLayout(m_root);
        mainLayout->setContentsMargins(8, 8, 8, 8);
        mainLayout->setObjectName(QStringLiteral("ird_property_inspector"));
        m_root->setObjectName(QStringLiteral("ird_property_inspector"));

        // 标题行：显示名＋只读徽标（徽标默认隐藏——事实透传，非对象态
        // 与可编辑页都不呈现）。
        auto* titleRow = new QWidget(m_root);
        auto* titleLayout = new QHBoxLayout(titleRow);
        titleLayout->setContentsMargins(0, 0, 0, 0);
        m_title->setObjectName(QStringLiteral("ird_property_inspector_title"));
        m_readOnlyBadge->setObjectName(
            QStringLiteral("ird_property_inspector_readonly"));
        m_readOnlyBadge->setText(QString::fromUtf8(kInspectorReadOnlyBadgeText));
        m_readOnlyBadge->hide();
        titleLayout->addWidget(m_title, /*stretch=*/1);
        titleLayout->addWidget(m_readOnlyBadge);
        mainLayout->addWidget(titleRow);

        // 占位标签：分态呈现的唯一文本位（自动换行——右栏窄面长文案）。
        m_placeholder->setObjectName(
            QStringLiteral("ird_property_inspector_placeholder"));
        m_placeholder->setWordWrap(true);
        mainLayout->addWidget(m_placeholder);

        // 页面标题标签（页面供给的 title 原文——零加工呈现于字段区上方；
        // 空标题〔纯入口形态〕不呈现——零虚构）。
        m_pageTitle->setObjectName(
            QStringLiteral("ird_property_inspector_page_title"));
        m_pageTitle->setWordWrap(true);
        mainLayout->addWidget(m_pageTitle);
        m_pageTitle->hide();

        // 常用字段区容器：ObjectFields 时内嵌参数表面板（每次 refresh
        // 重建——ParamEditModel 与页面值绑定，对象切换必须整体换装）。
        m_fieldsHost->setObjectName(
            QStringLiteral("ird_property_inspector_fields"));
        m_fieldsLayout = new QVBoxLayout(m_fieldsHost);
        m_fieldsLayout->setContentsMargins(0, 0, 0, 0);
        mainLayout->addWidget(m_fieldsHost);

        // 复杂编辑区：节标题＋入口按钮列＋宿装容器（区级默认隐藏——
        // 无入口的对象不呈现空节，零虚构）。
        m_complexSection->setObjectName(
            QStringLiteral("ird_property_inspector_complex"));
        m_complexSection->setText(QString::fromUtf8(kInspectorComplexSectionText));
        mainLayout->addWidget(m_complexSection);
        m_entriesHost->setObjectName(
            QStringLiteral("ird_property_inspector_complex_entries"));
        m_entriesLayout = new QVBoxLayout(m_entriesHost);
        m_entriesLayout->setContentsMargins(0, 0, 0, 0);
        mainLayout->addWidget(m_entriesHost);
        m_pageHost->setObjectName(
            QStringLiteral("ird_property_inspector_complex_host"));
        m_pageLayout = new QVBoxLayout(m_pageHost);
        m_pageLayout->setContentsMargins(0, 0, 0, 0);
        mainLayout->addWidget(m_pageHost);

        m_complexSection->hide();
        m_entriesHost->hide();
        m_pageHost->hide();
        mainLayout->addStretch(/*stretch=*/1);
    }

    QWidget* widget() override { return m_root; }

    void refresh() override
    {
        // 全量重渲染：字段区/入口区/宿装区先清空（对象切换不残留上一
        // 对象的任何呈现——INV-B3 零留存的呈现面纪律），再按模型当前
        // 状态分态装配。
        clearWidgets(*m_fieldsLayout);
        clearWidgets(*m_entriesLayout);
        clearWidgets(*m_pageLayout);
        m_paramModel.reset();
        m_readOnlyBadge->hide();
        m_pageTitle->hide();

        const PropertyInspectorView& view = m_deps.model->view();

        // 分态：占位四态只呈现文案；对象应答态呈现标题/字段/入口/宿装。
        switch (view.kind) {
        case InspectorContentKind::NoSelection:
            showPlaceholder(QString::fromUtf8(kInspectorNoSelectionText));
            return;
        case InspectorContentKind::MultiSelection:
            showPlaceholder(QString::fromUtf8(kInspectorMultiSelectionText));
            return;
        case InspectorContentKind::RuntimeOnly:
            showPlaceholder(QString::fromUtf8(kInspectorRuntimeOnlyText));
            return;
        case InspectorContentKind::NoProviderAnswer:
            showPlaceholder(QString::fromUtf8(kInspectorNoProviderAnswerText));
            return;
        case InspectorContentKind::PageRejected:
            // 防御占位＋复杂编辑入口照常（分野两通道互不遮蔽）；对象标题
            // 保留可见（呈现面仍指明当前对象——拒绝的是页，不是选中）。
            showPlaceholder(QString::fromUtf8(kInspectorPageRejectedText));
            m_title->show();
            m_title->setText(displayTitleFor(view.objectId));
            m_complexSection->setVisible(!view.complexEntries.empty());
            m_entriesHost->setVisible(!view.complexEntries.empty());
            populateComplexEntries(view);
            m_pageHost->setVisible(false);
            return;
        case InspectorContentKind::ObjectFields:
            break;
        }

        // 对象应答态：标题行（显示名现取＋只读徽标）。
        m_placeholder->hide();
        m_title->show();
        m_title->setText(displayTitleFor(view.objectId));
        if (view.readOnly) {
            m_readOnlyBadge->show();
        }
        if (!view.title.empty()) {
            m_pageTitle->setText(QString::fromUtf8(view.title.c_str()));
            m_pageTitle->show();
        }

        // 常用字段区：FormEditCommon 参数表面板构件复用（acceptance 3）
        // ——页面 fields+values 装配编辑会话模型，页面 editOutlet 作移交
        // 出口；公共编辑规则（同显/就地错误/确认应用/取消恢复）零第二
        // 实现。空 fields（纯入口形态）不装表格——字段区整体隐藏。
        if (!view.fields.empty()) {
            m_fieldsHost->show();
            std::vector<QuantityFieldSpec> specs = view.fields;
            m_paramModel = std::make_unique<ParamEditModel>(std::move(specs));
            for (const InspectorFieldValue& value : view.values) {
                m_paramModel->setBaseline(value.key, value.siValue);
            }
            QWidget* table = createParamTablePanel(
                *m_paramModel, view.editOutlet, ParamTablePanelOptions{},
                m_fieldsHost);
            m_fieldsLayout->addWidget(table);
            // 动态加入"已可见父链"的子控件须显式 show（同入口按钮纪律）。
            table->show();
        } else {
            m_fieldsHost->hide();
        }

        // 复杂编辑区：入口按钮列＋宿装容器（有入口才呈现——零虚构空节）。
        m_complexSection->setVisible(!view.complexEntries.empty());
        m_entriesHost->setVisible(!view.complexEntries.empty());
        m_pageHost->setVisible(false);  // 新对象呈现不携带旧宿装页
        populateComplexEntries(view);
    }

private:
    /// @brief 清空一个纵排布局内的全部控件（Qt 父子树接管删除——
    ///        deleteLater 经事件循环回收，重建即从布局摘除）。
    static void clearWidgets(QVBoxLayout& layout)
    {
        while (layout.count() > 0) {
            QLayoutItem* item = layout.takeAt(0);
            if (item->widget() != nullptr) {
                item->widget()->deleteLater();
            }
            delete item;
        }
    }

    /// @brief 标题显示名（nameResolver 现取；失败占位——UX-02/R-4）。
    QString displayTitleFor(const core::ObjectId& id) const
    {
        const auto name = m_deps.nameResolver->resolveObjectId(id);
        const std::string text = name.value_or(kUnnamedObjectTitle);
        return QString::fromUtf8(text.c_str());
    }

    /// @brief 呈现占位文案并隐藏字段/复杂两区（占位态的统一收口）。
    void showPlaceholder(const QString& text)
    {
        m_placeholder->setText(text);
        m_placeholder->show();
        m_title->hide();
        m_pageTitle->hide();
        m_fieldsHost->hide();
        m_complexSection->hide();
        m_entriesHost->hide();
        m_pageHost->hide();
    }

    /// @brief 装配复杂编辑入口按钮列（D6——每入口一钮，点击经模型编排
    ///        转调应答域 Provider；hosted 页返回视图挂入宿装容器）。
    void populateComplexEntries(const PropertyInspectorView& view)
    {
        const core::ObjectId object = view.objectId;
        for (const ComplexPageEntry& entry : view.complexEntries) {
            auto* button = new QPushButton(QString::fromUtf8(entry.title.c_str()),
                                           m_entriesHost);
            button->setObjectName(
                QStringLiteral("ird_property_inspector_complex_entry_%1")
                    .arg(QString::fromUtf8(entry.pageKey.c_str())));
            // 捕获按值拷贝 entry/pageKey/object（按钮存活期可能长于
            // view 引用——refresh 结束后 view 仍由模型持有，但拷贝自证
            // 无悬挂）；点击→模型激活编排→宿装。
            const ComplexPageEntry captured = entry;
            QWidget* pageHost = m_pageHost;
            QVBoxLayout* pageLayout = m_pageLayout;
            PropertyInspectorModel* model = m_deps.model.get();
            QObject::connect(button, &QPushButton::clicked, m_root,
                             [model, object, captured, pageHost, pageLayout]() {
                                 // 宿装区先清上一页（切换页面不堆叠——同
                                 // 一时刻至多一个宿装页可见）。
                                 clearWidgets(*pageLayout);
                                 pageHost->setVisible(false);
                                 ComplexPageActivationReport report =
                                     model->activateComplexPage(
                                         object, captured.pageKey, pageHost);
                                 if (report.ok
                                     && report.hostedWidget != nullptr) {
                                     pageLayout->addWidget(report.hostedWidget);
                                     report.hostedWidget->show();
                                     pageHost->setVisible(true);
                                 } else if (!report.ok && captured.hosted) {
                                     // 宿装页激活失败的防御占位（Provider
                                     // 契约违约的呈现面；原因 token 已由
                                     // 模型 Dev 留痕）。自持页失败不出
                                     // 宿装占位——激活结局属域自持面。
                                     auto* missing =
                                         new QLabel(QString::fromUtf8(
                                                        kInspectorHostedPageMissingText),
                                                    pageHost);
                                     missing->setWordWrap(true);
                                     pageLayout->addWidget(missing);
                                     missing->show();
                                     pageHost->setVisible(true);
                                 }
                                 // 自持页成功（ok 且零视图）：域已自行
                                 // 打开编辑视图/对话框——检查器呈现面不动。
                             });
            m_entriesLayout->addWidget(button);
            // 动态加入"已可见父链"的子控件不会自动呈现（Qt 呈现纪律：
            // 父可见后新增子控件须显式 show）——面板构建晚于 root->show()
            // 的常规装配序下必须补 show。
            button->show();
        }
    }

    /// 装配依赖（model/nameResolver——工厂校验非空）。
    PropertyInspectorPanelDeps m_deps;
    /// 根容器与各级控件（所有权随 Qt 父子树——m_root 持全体）。
    QWidget* m_root;
    QLabel* m_title;
    QLabel* m_readOnlyBadge;
    QLabel* m_pageTitle;
    QLabel* m_placeholder;
    QWidget* m_fieldsHost;
    QLabel* m_complexSection;
    QWidget* m_entriesHost;
    QWidget* m_pageHost;
    /// 三个动态区的纵排布局（refresh 反复清空重建）。
    QVBoxLayout* m_fieldsLayout;
    QVBoxLayout* m_entriesLayout;
    QVBoxLayout* m_pageLayout;
    /// 当前字段的编辑会话模型（与内嵌参数表面板同寿命——对象切换整体
    /// 换装；unique_ptr 保证面板控件先于模型析构【clearWidgets 先于
    /// reset 执行】——createParamTablePanel 持引用的存活期纪律）。
    std::unique_ptr<ParamEditModel> m_paramModel;
};

}  // namespace

std::unique_ptr<PropertyInspectorPanel> createPropertyInspectorPanel(
    const PropertyInspectorPanelDeps& deps, QWidget* parent)
{
    // 装配期 fail-fast：无模型的面板没有呈现语义、无名称端口的标题行
    // 只能拼名称（R-4 禁止）——均属装配缺陷，构造期拒绝优于运行期空转
    // （IndustrialProjectTreePanelDeps 同款纪律）。
    if (!deps.model || !deps.nameResolver) {
        throw std::invalid_argument(
            "createPropertyInspectorPanel: deps.model/nameResolver 为空");
    }
    return std::make_unique<PropertyInspectorPanelImpl>(deps, parent);
}

}  // namespace ui
}  // namespace ird
}  // namespace sdurws
