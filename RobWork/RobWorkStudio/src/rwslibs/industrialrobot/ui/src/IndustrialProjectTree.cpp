/**
 * @file   IndustrialProjectTree.cpp
 * @brief  工业项目树实现——五分组封闭模型（Provider 驱动＋ObjectId 身份
 *         索引＋零名称缓存）与 Qt 渲染面板（契约头 IndustrialProjectTree.hpp
 *         的实现 TU）。
 *
 * 设计依据：见契约头文件头（B1-SPEC §3.1/§3.3/§4.2/§5.1、units/ui.md
 * §13 UI-T21 行、§6.6 UX-02——本 TU 不重复语义论证，只承载实现）。
 *
 * Qt 纪律：面板零 Q_OBJECT（信号接线全经 QObject::connect lambda——
 * ui 库本体 AUTOMOC 关闭口径不变，DTB §5.1）；分组/节点行以 Qt::UserRole
 * 携带节点 ObjectId 规范文本（组行无该数据＝"分组是结构不是对象"的
 * 数据面区分，选中信号按此过滤）。
 */

#include <sdurws/ird/ui/IndustrialProjectTree.hpp>

#include <algorithm>
#include <stdexcept>
#include <unordered_map>
#include <utility>

#include <QTreeWidget>
#include <QTreeWidgetItem>

namespace sdurws {
namespace ird {
namespace ui {

namespace {

/// 节点行 Qt::UserRole 数据键语义：存储节点 ObjectId 规范文本（"obj-<32
/// 位小写 hex>"——parse/format 往返稳定的机器可读形态；组行无此数据）。
constexpr int kNodeIdRole = Qt::UserRole;

/// 防御深度上限（与契约头 ProjectTreeNode::depth 注释一致——B1-SPEC
/// §3.1 五分组自然深度≤5，8 为防御值，超限在重建边界整体拒绝）。
constexpr std::uint8_t kMaxNodeDepth = 8;

/// 显示名解析失败的占位文案（UX-02：解析失败显示占位、不拼接名称——
/// 占位是"未命名"的诚实呈现；过渡文案随 P-DIAG-9 迁资源文件时只换值源）。
constexpr const char* kUnnamedNodeLabel = "（未命名对象）";

}  // namespace

// =====================================================================
// ProjectTreeModel——模型实现
// =====================================================================

void ProjectTreeModel::addProvider(
    std::shared_ptr<IUiTreeNodesProvider> provider)
{
    // 调用方错误 fail-fast：空指针登记是装配缺陷（重建时无法现调）。
    if (!provider) {
        throw std::invalid_argument(
            "ProjectTreeModel: provider must not be null");
    }
    // 同域双供给源的合并语义未定义（谁先谁后都是私裁）——登记边界拒绝。
    const std::string key = provider->domainKey();
    for (const auto& existing : m_providers) {
        if (existing->domainKey() == key) {
            throw std::invalid_argument(
                "ProjectTreeModel: duplicate provider domainKey '" + key + "'");
        }
    }
    // 注册序＝组内贡献序（NFR-COR-02 稳定序的登记半区）。
    m_providers.push_back(std::move(provider));
}

std::size_t ProjectTreeModel::providerCount() const noexcept
{
    return m_providers.size();
}

TreeRebuildReport ProjectTreeModel::rebuild()
{
    // 第一步：现调全部 Provider 供给（值拷贝拼集——域侧状态自持，重建
    // 即见域修订后内容；注册序拼装＝组内贡献序）。
    std::vector<ProjectTreeNode> candidates;
    for (const auto& provider : m_providers) {
        std::vector<ProjectTreeNode> supplied = provider->treeNodes();
        candidates.insert(candidates.end(),
                          std::make_move_iterator(supplied.begin()),
                          std::make_move_iterator(supplied.end()));
    }

    // 第二步：整体校验（任一违约→保持旧内容整体拒绝——导航呈现不允许
    // 半新半旧，拒绝优于残缺；reason 为封闭词表，Dev 排查入口）。
    std::unordered_map<std::string, std::size_t> indexByCanonical;
    indexByCanonical.reserve(candidates.size());
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        const ProjectTreeNode& node = candidates[i];

        // ①身份有效性：全零保留值不是任何真实对象——INV-B1 的重建边界
        // 强制（运行时结构对象没有业务 ObjectId，伪造身份在此被拦截）。
        if (!node.objectId.isValid()) {
            return TreeRebuildReport{false, "invalid-object-id", 0};
        }
        // ②跨 Provider 全局唯一：CON-01——同一对象两次供给＝域侧装配
        // 缺陷（谁覆盖谁都是私裁），撞号即整体拒绝。
        const std::string canonical = node.objectId.toCanonical();
        if (!indexByCanonical.emplace(canonical, i).second) {
            return TreeRebuildReport{false, "duplicate-object-id", 0};
        }
        // ④深度防御上限（先于③做——成本 O(1)，违约时省去子引用闭合
        // 扫描；上限语义见契约头 depth 注释）。
        if (node.depth > kMaxNodeDepth) {
            return TreeRebuildReport{false, "depth-limit-exceeded", 0};
        }
    }
    // ③子引用闭合：childObjectIds 必须指向本轮供给集内节点（悬空引用
    // 的树行点了没反应＝静默失效，拒绝优于残缺）。
    for (const ProjectTreeNode& node : candidates) {
        for (const core::ObjectId& child : node.childObjectIds) {
            if (indexByCanonical.find(child.toCanonical())
                == indexByCanonical.end()) {
                return TreeRebuildReport{false, "dangling-child-reference", 0};
            }
        }
    }

    // 第三步：整体替换——按五分组固定序分桶（B1-SPEC §3.1 行序＝呈现
    // 序，与 Provider 注册序无关；组内保持"注册序→供给序"拼装序——
    // NFR-COR-02 稳定序的重建半区）。
    std::vector<std::vector<std::size_t>> groupRows(kProjectTreeGroupCount);
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        const std::uint8_t group = projectTreeGroupIndex(candidates[i].group);
        // 枚举封闭——group 值域 0..4，恒在桶表范围内（防御分支仅为
        // 消除静态分析疑虑，不可达）。
        if (group < kProjectTreeGroupCount) {
            groupRows[group].push_back(i);
        }
    }

    m_nodes = std::move(candidates);
    m_groupRows = std::move(groupRows);

    TreeRebuildReport report;
    report.ok = true;
    report.nodeCount = m_nodes.size();
    return report;
}

std::size_t ProjectTreeModel::nodeCount() const noexcept
{
    return m_nodes.size();
}

std::vector<core::ObjectId>
ProjectTreeModel::nodesInGroup(ProjectTreeGroup group) const
{
    // 桶序直读（组内节点序＝重建时拼装序的权威面——呈现与定位共用此序，
    // (group,row) 二元组在全树内唯一寻址一个节点）。重建前（未 rebuild）
    // 桶表为空——查询合法返回空序（空模型查询是常态而非违约）。
    std::vector<core::ObjectId> ids;
    const std::uint8_t index = projectTreeGroupIndex(group);
    if (index >= m_groupRows.size()) {
        return ids;  // 未重建＝零内容——五组皆空（合法空态，不越界）
    }
    const std::vector<std::size_t>& rows = m_groupRows[index];
    ids.reserve(rows.size());
    for (std::size_t row : rows) {
        ids.push_back(m_nodes[row].objectId);
    }
    return ids;
}

std::optional<ProjectTreeNode> ProjectTreeModel::node(const core::ObjectId& id) const
{
    // 线性扫描（树规模＝单项目业务对象量级——百~千级，线性足够；索引
    // 哈希表属重建内部临时态，不作为查询面常驻——呈现路径零分配优先）。
    for (const ProjectTreeNode& n : m_nodes) {
        if (n.objectId == id) {
            return n;
        }
    }
    return std::nullopt;
}

std::optional<TreeLocation> ProjectTreeModel::locate(const core::ObjectId& id) const
{
    // L3 定位数据（acceptance 3/4）：分组＋组内序位＋逻辑父——面板按此
    // (group,row) 展开并选中；不存在→nullopt（调用方以返回值自证命中，
    // 未命中不伪造选中——SelectionService 分支④）。重建前（未 rebuild）
    // 桶表为空＝零内容——直接未命中（合法空态，不越界）。
    if (m_groupRows.size() < kProjectTreeGroupCount) {
        return std::nullopt;
    }
    for (std::uint8_t g = 0; g < kProjectTreeGroupCount; ++g) {
        const std::vector<std::size_t>& rows = m_groupRows[g];
        for (std::size_t row = 0; row < rows.size(); ++row) {
            const ProjectTreeNode& n = m_nodes[rows[row]];
            if (n.objectId == id) {
                TreeLocation location;
                location.group = static_cast<ProjectTreeGroup>(g);
                location.rowInGroup = row;
                // 逻辑父：childObjectIds 反查（供给面父子即呈现父子；
                // 多父引用在重建③闭合校验下不可能存在歧义——一个子身份
                // 可被多个父引用，取首个声明父为呈现路径，声明序稳定）。
                for (const ProjectTreeNode& candidate : m_nodes) {
                    const bool declares = std::any_of(
                        candidate.childObjectIds.begin(),
                        candidate.childObjectIds.end(),
                        [&id](const core::ObjectId& c) { return c == id; });
                    if (declares) {
                        location.parentId = candidate.objectId;
                        break;
                    }
                }
                return location;
            }
        }
    }
    return std::nullopt;
}

// =====================================================================
// Qt 面板实现（零 Q_OBJECT——信号接线全经 lambda）
// =====================================================================

namespace {

/// 面板实现（契约头的 IndustrialProjectTreePanel——R-2 封闭在库内）。
class IndustrialProjectTreePanelImpl final : public IndustrialProjectTreePanel {
public:
    IndustrialProjectTreePanelImpl(const IndustrialProjectTreePanelDeps& deps,
                                   QWidget* parent)
        : m_deps(deps),
          m_tree(new QTreeWidget(parent))
    {
        configureTree();
        connectSelectionSignal();
        refresh();
    }

    QWidget* widget() override { return m_tree; }

    void refresh() override
    {
        // 全量重渲染（渲染层零状态假设——内容以模型为唯一事实源）：
        // 先记住当前业务选中（服务侧状态），重建行后按仍在树中的选中
        // 恢复高亮——重建不打断用户的选择上下文。
        const std::vector<core::ObjectId> previousSelection =
            m_deps.selection->selectedObjectIds();

        m_tree->clear();
        for (std::uint8_t g = 0; g < kProjectTreeGroupCount; ++g) {
            // 组行：五分组封闭清单的可视不变量（组数恒为 5——空组也呈现
            // 组头，用户能看到结构存在；INV-B1 呈现面断言锚）。组行不携带
            // 节点身份数据（kNodeIdRole 缺省）＝"分组是结构不是对象"，
            // 选中信号按此过滤。
            auto* groupItem = new QTreeWidgetItem(m_tree);
            groupItem->setText(0, groupLabel(static_cast<ProjectTreeGroup>(g)));
            groupItem->setFlags(groupItem->flags() & ~Qt::ItemIsSelectable);
            for (const core::ObjectId& id :
                 m_deps.model->nodesInGroup(static_cast<ProjectTreeGroup>(g))) {
                auto* nodeItem = new QTreeWidgetItem(groupItem);
                nodeItem->setText(0, displayLabel(id));
                nodeItem->setData(0, kNodeIdRole,
                                  QString::fromStdString(id.toCanonical()));
                // 层级缩进＝Provider 供给的 depth 字段（域侧最清楚自身
                // 层级——模型/面板不重算父子链，单字段透传）。
                const auto nodeValue = m_deps.model->node(id);
                applyDepthIndent(nodeItem,
                                 nodeValue.has_value()
                                     ? static_cast<int>(nodeValue->depth)
                                     : 0);
            }
            // 装配失败占位行（UI-T23——setGroupPlaceholder 的渲染半区）：
            // 挂在组行下、无节点身份数据（选中信号按 kNodeIdRole 过滤——
            // 占位行永不进业务选中）、不可选中（flags 同组行）；渲染时机
            // ＝每次 refresh 全量重放（登记态零持久于 Qt 控件）。
            if (g < m_groupPlaceholders.size()
                && !m_groupPlaceholders[g].empty()) {
                auto* placeholder = new QTreeWidgetItem(groupItem);
                placeholder->setText(
                    0, QString::fromStdString(m_groupPlaceholders[g]));
                placeholder->setFlags(placeholder->flags()
                                      & ~Qt::ItemIsSelectable);
            }
        }
        m_tree->expandAll();

        // 恢复选中（对象仍在树中才恢复——内容移除后不伪造选中）。
        if (!previousSelection.empty()) {
            selectRowForObject(previousSelection.front(),
                               /*emitSelection=*/false);
        }
    }

    bool locateAndHighlight(const core::ObjectId& id) override
    {
        // L3 落点：定位（模型 (group,row)）→ 展开父路径 → 选中对应行。
        // 选中经 Qt 信号自然回流 selectBusiness（单一写入路径——本方法
        // 不直接调服务，避免双广播）；未命中返回 false（服务分支④的
        // 一致性自证值），树零触碰。
        if (!selectRowForObject(id, /*emitSelection=*/true)) {
            return false;
        }
        return true;
    }

    void setGroupPlaceholder(ProjectTreeGroup group,
                             const std::string& text) override
    {
        // 占位登记（UI-T23——§11.3 多域失败隔离的呈现半区）：登记后由
        // refresh() 重渲染生效；空串＝清除。登记本身不触发渲染（装配层
        // 在域装配完成后统一 refresh——呈现时机归装配编排，与模型 rebuild
        // 同一口径）。
        const std::uint8_t index = projectTreeGroupIndex(group);
        if (m_groupPlaceholders.size() <= index) {
            m_groupPlaceholders.resize(kProjectTreeGroupCount);
        }
        m_groupPlaceholders[index] = text;
    }

private:
    /// @brief 树控件一次性配置（表头/列宽/选择模式——呈现层固定形态）。
    void configureTree()
    {
        m_tree->setColumnCount(1);
        m_tree->setHeaderHidden(true);
        m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
        // 只读呈现（D3 导航树——编辑归域复杂编辑页 D6，树内零就地编辑）。
        m_tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    }

    /// @brief 接选中信号（itemSelectionChanged→过滤组行→写选择服务）。
    void connectSelectionSignal()
    {
        // 选中写入路径（L1 基线——B1-SPEC §4.2：项目树选中业务对象→
        // 检查器刷新。树不持选中状态（INV-B3）——每次 Qt 选中变化都
        // 立即写入 SelectionService（唯一汇聚点），由服务广播给消费者。
        // 组行过滤：组行不可选中（flags 已清），此处按数据键二次防御）。
        QObject::connect(m_tree, &QTreeWidget::itemSelectionChanged,
                         m_tree, [this]() {
                    QList<QTreeWidgetItem*> selected = m_tree->selectedItems();
                    for (QTreeWidgetItem* item : selected) {
                        const QVariant idText = item->data(0, kNodeIdRole);
                        if (!idText.isValid() || idText.toString().isEmpty()) {
                            continue;  // 组行（结构）——不进业务选中
                        }
                        const auto parsed = core::ObjectId::tryFromCanonical(
                            idText.toString().toStdString());
                        if (parsed.has_value()) {
                            // 程序化恢复选中（refresh 路径）不重复广播：
                            // emitSelection=false 时临时阻断信号回流。
                            if (!m_suppressSelectionWrite) {
                                m_deps.selection->selectBusiness(
                                    {*parsed}, SelectionSource::ProjectTree);
                            }
                        }
                    }
                });
    }

    /// @brief 定位并选中目标行（locate→展开→选中；emitSelection=false
    ///        用于 refresh 的选中恢复——不回流服务重复广播）。
    bool selectRowForObject(const core::ObjectId& id, bool emitSelection)
    {
        const auto location = m_deps.model->locate(id);
        if (!location.has_value()) {
            return false;  // 树中无该对象——调用方（服务分支④）自证处理
        }
        QTreeWidgetItem* groupItem = m_tree->topLevelItem(
            projectTreeGroupIndex(location->group));
        if (groupItem == nullptr) {
            return false;  // 组行缺失＝渲染层装配缺陷的防御检出
        }
        QTreeWidgetItem* target =
            groupItem->child(static_cast<int>(location->rowInGroup));
        if (target == nullptr) {
            return false;  // 行位漂移＝模型/渲染失配的防御检出
        }
        // 展开父链（层级节点——从组行到目标行逐级展开，定位即可见）。
        QTreeWidgetItem* ancestor = target->parent();
        while (ancestor != nullptr) {
            ancestor->setExpanded(true);
            ancestor = ancestor->parent();
        }
        m_tree->scrollToItem(target);
        // 程序化选中：阻断信号回流（refresh 恢复场景）＝不触发
        // selectBusiness 重复广播；L3 落点场景（emitSelection=true）＝
        // 放行回流——业务选中以 ProjectTree 来源写入服务（B1-SPEC §4.2
        // L3"定位并选中"的落点语义）。
        m_suppressSelectionWrite = !emitSelection;
        m_tree->setCurrentItem(target);
        m_suppressSelectionWrite = false;
        return true;
    }

    /// @brief 渲染名称（UX-02：经名称端口现取——零第二套命名缓存；解析
    ///         失败显示占位、零拼接零哈希进界面文本）。
    QString displayLabel(const core::ObjectId& id) const
    {
        const auto name = m_deps.nameResolver->resolveObjectId(id);
        if (name.has_value() && !name->empty()) {
            return QString::fromStdString(*name);
        }
        return QString::fromUtf8(kUnnamedNodeLabel);
    }

    /// @brief 施加层级缩进（Provider 供给 depth 的呈现半区——每层一档）。
    void applyDepthIndent(QTreeWidgetItem* item, int depth) const
    {
        item->setTextAlignment(0, Qt::AlignLeft | Qt::AlignVCenter);
        if (depth > 0) {
            item->setText(0, QString(indentUnit().repeated(depth))
                                 + item->text(0));
        }
    }

    /// @brief 缩进单元（两空格——层级提示的最小可读档）。
    static QString indentUnit() { return QStringLiteral("  "); }

    /// @brief 分组显示名（§3.1 五行的工程用语呈现值——过渡文案随
    ///         P-DIAG-9 迁资源文件时只换值源，键语义不变）。
    static QString groupLabel(ProjectTreeGroup group)
    {
        switch (group) {
        case ProjectTreeGroup::ProjectStructure:
            return QStringLiteral("项目结构");
        case ProjectTreeGroup::ModelingObjects:
            return QStringLiteral("建模对象");
        case ProjectTreeGroup::RequirementObjects:
            return QStringLiteral("需求对象");
        case ProjectTreeGroup::AnalysisConfigurations:
            return QStringLiteral("分析配置");
        case ProjectTreeGroup::ResultsEvidenceReports:
            return QStringLiteral("结果 / 证据 / 报告");
        }
        return QStringLiteral("（未知分组）");
    }

    /// 装配依赖（模型/服务/名称端口——构造期工厂已校验非空）。
    IndustrialProjectTreePanelDeps m_deps;
    /// 树控件（实例存活期＝控件存活期——widget() 出口所有权随实例）。
    QTreeWidget* m_tree;
    /// 程序化选中时的信号回流阻断（refresh 恢复选中场景——防止重复
    /// 广播；真用户交互路径恒为 false）。
    bool m_suppressSelectionWrite = false;
    /// 分组占位文案登记（UI-T23——setGroupPlaceholder 的状态面；下标＝
    /// projectTreeGroupIndex，空串＝无占位。渲染层状态，模型零触碰）。
    std::vector<std::string> m_groupPlaceholders{kProjectTreeGroupCount};
};

}  // namespace

std::unique_ptr<IndustrialProjectTreePanel> createIndustrialProjectTreePanel(
    const IndustrialProjectTreePanelDeps& deps, QWidget* parent)
{
    // 装配缺陷 fail-fast：三依赖任一缺失＝"看不到内容也报不了选中"的
    // 死面板——构造期拒绝优于运行期空转（工厂校验，UI-T11 同款纪律）。
    if (!deps.model) {
        throw std::invalid_argument(
            "createIndustrialProjectTreePanel: model is required");
    }
    if (!deps.selection) {
        throw std::invalid_argument(
            "createIndustrialProjectTreePanel: selection service is required");
    }
    if (!deps.nameResolver) {
        throw std::invalid_argument(
            "createIndustrialProjectTreePanel: nameResolver is required");
    }
    return std::make_unique<IndustrialProjectTreePanelImpl>(deps, parent);
}

}  // namespace ui
}  // namespace ird
}  // namespace sdurws
