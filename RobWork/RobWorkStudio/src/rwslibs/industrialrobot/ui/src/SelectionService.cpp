/**
 * @file   SelectionService.cpp
 * @brief  选择服务实现——业务选中唯一写入口、L3 反解编排、INV-B4 呈现
 *         刷新处置与 ⑤ 事件广播（契约头 SelectionService.hpp 的实现 TU）。
 *
 * 设计依据：见契约头文件头（B1-SPEC §4.1/§4.2/§3.3、units/ui.md §13
 * UI-T21 行、RuntimePublishBridge.hpp 的 UI-T20 接缝注释——本 TU 不重复
 * 语义论证，只承载实现与实现侧注释）。
 *
 * 线程模型：全部入口仅 UI 线程（契约头线程约束行）——本 TU 零同步原语
 * 是该纪律的结构自证。
 */

#include <sdurws/ird/ui/SelectionService.hpp>

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace sdurws {
namespace ird {
namespace ui {

namespace {

/// Dev 留痕通道（RuntimePublishBridge 的 kPublishBridgeDevChannel 同款
/// 命名口径——单元内通道唯一，日志侧按通道过滤）。
constexpr const char* kSelectionServiceDevChannel = "ird.ui.selection-service";

/// 无效 ObjectId 的判定（全零保留值——core::ObjectId 契约：全零＝保留
/// 值不表示任何真实对象；选中集收下它等于伪造选中，构造边界即拒）。
bool isValidObjectId(const core::ObjectId& id)
{
    return id.isValid();
}

}  // namespace

// =====================================================================
// RAII 订阅句柄（⑤事件端口形态——core 事件总线同款语义）
// =====================================================================

/**
 * @brief 订阅句柄实现：持服务实例指针＋观察者指针；退订＝从服务登记表
 *        摘除（幂等——句柄可重复 unsubscribe，析构路径同款）。
 *
 * 线程约束：仅 UI 线程（服务与其登记表同线程——句柄操作随服务纪律）。
 */
class SelectionService::Subscription final : public core::IEventSubscription {
public:
    Subscription(SelectionService* owner, IUiSelectionObserver* observer)
        : m_owner(owner), m_observer(observer) {}

    ~Subscription() override { unsubscribe(); }

    Subscription(const Subscription&) = delete;
    Subscription& operator=(const Subscription&) = delete;

    /// @brief 退订（幂等——首次摘除后置空回指针，重复调用为空操作）。
    void unsubscribe() override
    {
        if (m_owner != nullptr) {
            m_owner->removeObserver(m_observer);
            m_owner = nullptr;
            m_observer = nullptr;
        }
    }

private:
    SelectionService* m_owner;        ///< 登记表所在服务（退订后置空）
    IUiSelectionObserver* m_observer; ///< 登记的观察者（退订后置空）
};

void SelectionService::removeObserver(IUiSelectionObserver* observer)
{
    // 实现侧辅助（头文件零暴露——句柄退订路径专用）：顺序摘除首个匹配。
    auto it = std::find(m_observers.begin(), m_observers.end(), observer);
    if (it != m_observers.end()) {
        m_observers.erase(it);
    }
}

// =====================================================================
// 构造与装配校验
// =====================================================================

SelectionService::SelectionService(Deps deps) : m_deps(std::move(deps))
{
    // 装配缺陷 fail-fast：无反解通道（L3 承诺不可达）或无树定位回调
    // （反解成功无处落点）都等于"违反 B1-SPEC §4.2 承诺的服务"——构造期
    // 拒绝优于运行期空转（RuntimePublishBridge source/outlet 同款纪律）。
    if (!m_deps.nameMap) {
        throw std::invalid_argument(
            "SelectionService: nameMap port is required (SA-05 反解/正向唯一通道)");
    }
    if (!m_deps.treeLocator) {
        throw std::invalid_argument(
            "SelectionService: treeLocator is required (B1-SPEC §4.2 L3 树定位落点)");
    }
    // highlightOutlet/devLog 允许为空＝显式声明的无三维高亮/无日志场景
    // （L2 判定照常执行、动作跳过；留痕静默——不虚构通道）。
}

// =====================================================================
// 业务选中写入口
// =====================================================================

void SelectionService::selectBusiness(std::vector<core::ObjectId> objectIds,
                                      SelectionSource source)
{
    // 第一步：入参有效性整批校验（任一无效即整批拒绝——部分收下会造成
    // "选中集含伪造身份"的静默污染，调用方错误 fail-fast）。
    for (const auto& id : objectIds) {
        if (!isValidObjectId(id)) {
            throw std::invalid_argument(
                "SelectionService: invalid ObjectId (all-zero reserved value) in selection");
        }
    }

    // 第二步：去重（保持首次出现序——NFR-COR-02 稳定序；重复选择同一
    // 对象的交互输入［如 ctrl 多击］收敛为单条身份）。
    std::vector<core::ObjectId> deduped;
    deduped.reserve(objectIds.size());
    for (auto& id : objectIds) {
        const bool seen = std::any_of(deduped.begin(), deduped.end(),
                                      [&id](const core::ObjectId& s) {
                                          return s == id;
                                      });
        if (!seen) {
            deduped.push_back(std::move(id));
        }
    }

    // 第三步：幂等判定——集合与来源都无变化即无操作（订阅者只见真变化；
    // 空集对空集同样幂等）。
    const bool sameSet = deduped.size() == m_selected.size()
                         && std::equal(deduped.begin(), deduped.end(),
                                       m_selected.begin());
    if (sameSet && m_source == source && !m_runtimeOnlyName.has_value()) {
        return;
    }

    // 第四步：提交状态。业务选中生效即作废仅运行时对象选择态（两态互斥
    // ——SelectionChange 注释：runtimeOnly 是 L3 失败的暂态，任何业务
    // 选中都是用户意图的明确表达）。
    const bool hadRuntimeOnly = m_runtimeOnlyName.has_value();
    m_selected = std::move(deduped);
    m_source = source;
    if (hadRuntimeOnly) {
        emitDevLine("selection: runtime-only state superseded by business selection");
    }
    m_runtimeOnlyName.reset();

    // 第五步：L2 高亮判定（正向存在性经 nameMap 唯一通道——SA-05）。
    applyHighlightDecision();

    // 第六步：广播变更（观察者同步回调——重入禁令见 IUiSelectionObserver 注释）。
    SelectionChange change;
    change.selectedObjectIds = m_selected;
    change.source = m_source;
    change.runtimeOnly = false;
    broadcast(change);
}

void SelectionService::clearSelection(SelectionSource source)
{
    // 显式清空＝selectBusiness 空集形态（语义等价——独立命名面只为调用
    // 点可读；分派回同一入口保证去重/幂等/L2/广播路径单一权威）。
    selectBusiness({}, source);
}

// =====================================================================
// L3 编排入口（acceptance 3/4——四个具名分支见契约头注释）
// =====================================================================

void SelectionService::handleTreeViewFrameSelected(
    const std::string& runtimeFrameName)
{
    // 转发方契约校验：空名无反解语义（TreeView 事件不可能携带空 Frame
    // 名——空串属转发装配缺陷，fail-fast 而非静默忽略）。
    if (runtimeFrameName.empty()) {
        throw std::invalid_argument(
            "SelectionService: empty runtime frame name from TreeView event");
    }

    // 分支①前置：反解（运行时名→ObjectId——SA-05 唯一通道；本服务零
    // 名称映射知识，R-4/UX-02）。
    const std::optional<core::ObjectId> resolved =
        m_deps.nameMap->resolveObjectIdFromRuntimeName(runtimeFrameName);

    if (resolved.has_value()) {
        // 分支①：反解成功 → 树定位并选中（B1-SPEC §4.2 L3"项目树定位
        // 并选中对应业务对象"）。定位回调由面板装配期注册——返回值是
        // "树确实呈现了该对象"的一致性自证。
        const bool located = m_deps.treeLocator(*resolved);
        if (located) {
            // 树已定位选中（选中事件经面板 Qt 信号回流 selectBusiness——
            // 单一写入路径，此处不重复写状态不重复广播）。
            emitDevLine("selection: L3 reverse-resolved frame to business object (tree located)");
            return;
        }
        // 分支④：反解成功但树中无该对象（树内容与映射漂移——重建时序
        // 的边角）。不伪造选中（"选中了不存在的节点"是比不动作更糟的
        // 谎言）、不动树、不报错；Dev 留痕供装配排查。
        emitDevLine("selection: L3 resolved id not present in tree — no action (content drift)");
        return;
    }

    // 分支②③：反解失败（框架自建 Frame、无业务对应——B1-SPEC §4.2
    // v1.1 口径的合法二态，不是错误）。项目树不动（树定位回调根本不被
    // 调用）、业务选中集不变（没有 ObjectId 可写——分支③的结构承载）、
    // 不出用户级诊断；记录仅运行时对象选择态并广播 runtimeOnly 事件，
    // 状态呈现消费者据此呈现"运行时对象选中"（记录的是 RuntimeNameMap
    // 键——呈现侧经端口取显示名，UX-02）。
    m_runtimeOnlyName = runtimeFrameName;
    emitDevLine("selection: L3 reverse-resolution failed — runtime-only selection recorded (tree untouched)");

    SelectionChange change;
    change.runtimeOnly = true;
    change.runtimeObjectName = runtimeFrameName;
    // 业务选中语义字段保持构造默认（空集＋默认来源——消费者按
    // runtimeOnly 分流，不把空集误读为"清除业务选中"）。
    broadcast(change);
}

// =====================================================================
// INV-B4 呈现刷新处置（UI-T20 接缝——IUiPresentationRefreshObserver）
// =====================================================================

void SelectionService::onPresentationReplaced(
    const PresentationViewProjection& view)
{
    // 前置防御：不完整投影（正常路径桥已对账——此处是对直接装配的二次
    // 防线）按"无可判定存在性"处理：保持选中不变＋Dev 留痕（不置空——
    // 缺证据时置空比保持更危险，静默清用户选中属伪动作）。
    if (!view.isComplete() || !static_cast<bool>(view.objectExists)) {
        emitDevLine("selection: presentation replaced with incomplete view — selection untouched");
        return;
    }

    // 刷新存在性闭包（Dev 自证面——L2 判定的唯一事实源始终是 nameMap
    // 端口，见契约头；此处仅留痕记录新呈现规模语义之外不持状态）。
    emitDevLine("selection: presentation replaced — evaluating INV-B4 disposition");

    // INV-B4 处置求值（UI-T20 纯规则的首消费——Keep/Clear 双值封闭，
    // "不静默换选"由词表结构保证）。
    const bool selectionPresent = !m_selected.empty();
    if (!selectionPresent) {
        // "本无选中"输入＝无操作（置空空集是恒等动作，不产生任何选中
        // 事件——UI-T20 SelectionDisposition 注释原文）。
        return;
    }
    const bool objectExists =
        view.objectExists(m_selected.front()) == true;  // 单选语义——多选按首元素判定（L2 同口径）
    const SelectionDisposition disposition =
        selectionDispositionAfterRefresh(selectionPresent, objectExists);

    if (disposition == SelectionDisposition::Keep) {
        // 保持选中——状态零变化不广播（订阅者只见真变化）。
        return;
    }
    // Clear：选中对象已不在新呈现中——置空并广播空选中事件（来源保持
    // 原值——清空不是新来源的选中，携带原来源便于消费者归因；UX 当前性
    // 口径的"失效呈现不静默换选"落点）。
    emitDevLine("selection: INV-B4 clear — selected object absent in new presentation");
    m_selected.clear();
    m_runtimeOnlyName.reset();
    SelectionChange change;
    change.selectedObjectIds.clear();
    change.source = m_source;
    change.runtimeOnly = false;
    broadcast(change);
}

void SelectionService::onPresentationRefreshFailed(
    const PresentationEventFacts& facts, const std::string& reasonToken)
{
    // 呈现刷新失败＝旧呈现保持——选中处置无从谈起（INV-B4"不改变业务树
    // 选中"在失败路径的延伸）：状态零触碰，Dev 留痕可观测。用户级诊断
    // 已由桥出 UI-PRESENTATION-REFRESH-FAILED——选中侧不重复出线。
    emitDevLine("selection: presentation refresh failed (" + reasonToken
                + ") — selection untouched");
}

// =====================================================================
// 状态查询
// =====================================================================

const std::vector<core::ObjectId>&
SelectionService::selectedObjectIds() const noexcept
{
    return m_selected;
}

std::optional<SelectionSource> SelectionService::selectionSource() const noexcept
{
    // 无业务选中＝无来源（不虚构——空集时的 m_source 是上次选中的残迹，
    // 对查询者没有语义）。
    if (m_selected.empty()) {
        return std::nullopt;
    }
    return m_source;
}

bool SelectionService::hasRuntimeOnlySelection() const noexcept
{
    return m_runtimeOnlyName.has_value();
}

const std::optional<std::string>&
SelectionService::runtimeOnlyObjectName() const noexcept
{
    return m_runtimeOnlyName;
}

// =====================================================================
// ⑤事件订阅
// =====================================================================

std::unique_ptr<core::IEventSubscription>
SelectionService::subscribe(IUiSelectionObserver& observer)
{
    // 幂等登记（同一观察者重复订阅按一次计——core 参考总线登记语义）。
    const bool already = std::find(m_observers.begin(), m_observers.end(),
                                   &observer)
                         != m_observers.end();
    if (!already) {
        m_observers.push_back(&observer);
    }
    return std::make_unique<Subscription>(this, &observer);
}

// =====================================================================
// 私有辅助
// =====================================================================

void SelectionService::broadcast(const SelectionChange& change)
{
    // 同步广播（登记序——跨订阅方按订阅序；回调内重入写入口属调用方
    // 违约，纪律面在 IUiSelectionObserver 注释，此处不加锁不改序）。
    for (IUiSelectionObserver* observer : m_observers) {
        observer->onSelectionChanged(change);
    }
}

void SelectionService::applyHighlightDecision()
{
    // L2 判定（B1-SPEC §4.2：选中对象存在已应用 WorkCell 对应物——经
    // nameMap 正向存在性判定——**可以**触发三维高亮；纯草稿/纯结果对象
    // 无对应物→无三维动作、不报错——反例分支与正向分支同为合法常态）。
    if (m_selected.empty() || m_selected.size() > 1) {
        // 多选/清空：第一版承诺只对"单个已应用对象"给高亮语义（词表外
        // 形态不擅自扩义——B1-SPEC §4.2 冻结）——保守清除是唯一无歧义
        // 动作；无出口（显式声明的无三维场景）＝跳过动作。
        if (m_deps.highlightOutlet) {
            m_deps.highlightOutlet->clearHighlight();
        }
        return;
    }

    // 单选：正向解析取运行时名（存在性＋定位名一次取得——L2 判定依据）。
    const std::optional<std::string> runtimeName =
        m_deps.nameMap->resolveRuntimeName(m_selected.front());
    if (!runtimeName.has_value()) {
        // 未应用对象：无三维动作、不报错（反例分支——B1-SPEC §4.2 L2
        // 原文"不产生三维动作，不报错"）；Dev 留痕可观测。
        emitDevLine("selection: L2 no applied counterpart — no 3D action (draft/result-only object)");
        if (m_deps.highlightOutlet) {
            m_deps.highlightOutlet->clearHighlight();
        }
        return;
    }

    // 已应用对象：高亮出线（出口可空＝无三维场景显式声明——跳过动作，
    // L2 判定已完成、事实照常成立）。
    if (m_deps.highlightOutlet) {
        m_deps.highlightOutlet->highlightRuntimeObject(*runtimeName);
    } else {
        emitDevLine("selection: L2 highlight skipped — no highlight outlet in this assembly");
    }
}

void SelectionService::emitDevLine(const std::string& message) const
{
    // Dev 留痕（通道可空＝显式声明的无日志场景——静默跳过，不虚构）。
    if (m_deps.devLog) {
        m_deps.devLog->logDev(kSelectionServiceDevChannel, message);
    }
}

}  // namespace ui
}  // namespace ird
}  // namespace sdurws
