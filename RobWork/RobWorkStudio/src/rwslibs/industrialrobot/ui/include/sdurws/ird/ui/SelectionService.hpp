/**
 * @file   SelectionService.hpp
 * @brief  选择服务（SelectionService）——业务选择的唯一汇聚点：维护"当前
 *         选中 ObjectId 集合＋来源标记"，广播选择变更事件（⑤事件端口形态）；
 *         三维拾取/TreeView 名称反解唯一经 RuntimeNameMap 端口（SA-05）。
 *
 * 设计依据：
 *   - 任务契约 tasks/foundation/UI-T21.json acceptance 2~4（选择服务落位/
 *     联动 L1~L3/L3 反解失败分支具名验收）；
 *   - B1-SPEC §4.1（选择服务＝ui 单元唯一业务选择汇聚点——选中 ObjectId
 *     集合＋来源标记〔项目树/三维拾取/搜索/命令四值封闭〕＋事件广播〔⑤
 *     事件端口形态〕；三维拾取产生的 ObjectId 反解与名称呈现经
 *     RuntimeNameMap〔SA-05〕，不在选择服务内私建名称映射）、§4.2（联动
 *     契约 L1~L4——本服务承载 L2 正向存在性判定与 L3 反解编排）、§3.3
 *     （INV-B3 两树互不持有对方选中状态——本服务是唯一持有者；INV-B4
 *     呈现重建后选中"存在性校验后保持或置空，不静默换选"）；
 *   - units/ui.md §13 UI-T21 行（任务卡）、§6.6（UX-02 工程用语——对象
 *     定位统一经名称端口，零第二套命名）、§3.1（ui 产品面零对 runtime
 *     链接/include——本头对 runtime 零类型知识，协作全部经自有最小端口）；
 *   - ARCHITECTURE §7.12/SA-18（D3 选择服务是业务选择唯一汇聚点；D4
 *     TreeView 是运行时辅助树）；
 *   - RuntimePublishBridge.hpp 的 IUiPresentationRefreshObserver（UI-T20
 *     落位登记注："selectionDispositionAfterRefresh 纯规则消费时机归
 *     UI-T21 SelectionService"——本服务实现该观察者，在呈现替换回调中
 *     按新投影 objectExists 求值选中处置）；
 *   - knownPitfalls：O-38（TreeView 为框架组件零修改——本服务只消费其
 *     Select Frame 事件值〔运行时名字符串〕，不触碰框架选中 API——L4
 *     防过度交付）；O-43（宿主 Dock 拓扑冻结——本服务零 Widget 知识）。
 *
 * 背景说明（为什么业务选择需要一个"唯一汇聚点"）：
 *   方案 B.1 冻结的双树形态下，"当前用户选中了什么"有两个天然来源面
 *   （工业项目树的业务节点、官方 TreeView/三维场景的运行时对象）与多个
 *   触发面（点击/搜索/命令）。若各面板自持选中状态，则属性检查器、三维
 *   高亮、域面板高亮会各看各的真相（INV-B3 禁止）。本服务把选中收敛为
 *   一份状态＋一条事件：任何来源的选中变更都经本服务写入并广播，任何
 *   消费者（UI-T22 检查器/域 SelectionAdapter/状态呈现）都只订阅本服务。
 *
 * 端口纪律（O-31 注入面——与 UiPorts.hpp/RuntimePublishBridge.hpp 同款）：
 *   ui 产品面对 runtime 零链接零 include（ARCH §3.5 白名单只有 ui→core/
 *   diagnostics）。名称反解（L3）与正向存在性（L2）由 L5 装配层把
 *   runtime RuntimeNameMap（SA-05 双射）适配为 IUiRuntimeNameMapPort 注
 *   入；三维高亮动作由 L5 适配宿主呈现出口为 IUiHighlightOutlet 注入；
 *   TreeView 的 Select Frame 事件由 L5 从框架信号转发为运行时名字符串
 *   （框架零修改——O-38/O-43 边界）。
 *
 * 线程模型：全部入口只在 UI 线程调用（ui.md §3.4 M-1 纪律——与
 *   UiSessionController/RuntimePublishBridge 同口径）；L5 负责把框架信号
 *   Marshal 回 UI 线程后再调本服务。非线程安全：仅 UI 线程访问。
 *
 * 生命周期/所有权：由 L5 装配层持有（unique_ptr 惯例）；端口 shared_ptr
 *   共享持有；订阅者以裸指针登记（RAII 句柄析构即退订——core ⑤事件
 *   总线同款语义）；观察者回调同步执行，回调内禁止再调本服务的写入口
 *   （重入禁令——与 RuntimePublishBridge 观察者纪律同款）。
 */

#ifndef SDURWS_IRD_UI_SELECTIONSERVICE_HPP
#define SDURWS_IRD_UI_SELECTIONSERVICE_HPP

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <sdurws/ird/core/Events.hpp>        // core::IEventSubscription（subscribe 返回的 RAII 句柄——⑤事件端口形态，表内登记边）
#include <sdurws/ird/core/Identity.hpp>      // core::ObjectId（选中集元素身份——CON-01，表内登记边直用）
#include <sdurws/ird/diagnostics/Catalog.hpp>  // diagnostics::IDevLogSink（Dev 级留痕通道——表内登记边；可空）
#include <sdurws/ird/ui/RuntimePublishBridge.hpp>  // ui::IUiPresentationRefreshObserver（INV-B4 选中处置的消费时机——UI-T20 接缝）＋PresentationViewProjection

namespace sdurws {
namespace ird {
namespace ui {

// =====================================================================
// 来源标记词表（B1-SPEC §4.1 四值封闭——新增值须 B1-SPEC 增量修订）
// =====================================================================

/**
 * @brief 选中变更的来源标记（B1-SPEC §4.1 原文词表：项目树/三维拾取/
 *        搜索/命令——四值封闭）。
 *
 * 词表纪律：封闭词表的消费者（域 SelectionAdapter/检查器）可以按来源
 * 分流行为，但不得假设词表外的来源存在。L3（TreeView Select Frame 反解
 * 成功）的选中以项目树为最终落点（B1-SPEC §4.2 L3"项目树**定位并选中**
 * 对应业务对象"）——该路径的来源标记记 ProjectTree（语义收敛点：对消费
 * 者而言它与一次项目树选中不可区分，这正是"树定位并选中"的呈现语义；
 * TreeView 事件本身不是词表内的来源值——它是运行时侧触发面，经反解
 * 成功后落点在树）。
 */
enum class SelectionSource : std::uint8_t {
    /// 项目树（用户在工业项目树点击/键盘选择；含 L3 反解成功后的树定位
    /// 选中——见枚举注释的收敛说明）。
    ProjectTree,
    /// 三维拾取（宿主中央 RWStudioView3D 点选——ObjectId 反解经
    /// IUiRuntimeNameMapPort，SA-05；正向存在性判定即 L2 依据）。
    View3DPick,
    /// 搜索（对象搜索结果定位——经树定位路径呈现）。
    Search,
    /// 命令（命令处理器设置的选中——如"定位到失败对象"类命令）。
    Command,
};

// =====================================================================
// 选择变更事件值（⑤事件端口的 ui 侧值形态——广播载体）
// =====================================================================

/**
 * @brief 一次选择变更的完整事实（广播给全部订阅者的只读值）。
 *
 * 两种变更形态（互斥——由 runtimeOnly 区分）：
 *   - 业务选中变更（runtimeOnly==false）：selectedObjectIds 携带变更后
 *     的完整选中集（去重、保持选择序——NFR-COR-02 稳定序）；空集＝清除
 *     选中（合法变更，不是"无变化"）；source 为本次变更来源。
 *   - 仅运行时对象选择（runtimeOnly==true）：L3 反解失败分支（B1-SPEC
 *     §4.2 v1.1 口径）——选中了一个没有任何业务 ObjectId 对应的运行时
 *     对象；业务选中集**保持不变**（项目树不动），runtimeObjectName 记
 *     录该运行时对象名供状态呈现（呈现侧经名称端口取显示名，本值只是
 *     RuntimeNameMap 键——UX-02：不进用户文本，零内部名拼接）。
 *
 * 值语义聚合体；字段完整性由服务自身构造保证（订阅者零校验义务）。
 */
struct SelectionChange {
    /// 变更后的业务选中集（runtimeOnly==false 时有意义；去重＋选择序）。
    std::vector<core::ObjectId> selectedObjectIds;
    /// 本次变更来源（runtimeOnly==true 时无业务选中语义——保持构造值）。
    SelectionSource source = SelectionSource::ProjectTree;
    /// true＝仅运行时对象选择（L3 反解失败——业务选中不变，见类注释）。
    bool runtimeOnly = false;
    /// 仅运行时对象的运行时名（runtimeOnly==true 时非空——RuntimeNameMap
    /// 键值，供状态呈现侧再经端口取显示名；UX-02 零内部名直显）。
    std::string runtimeObjectName;
};

/**
 * @brief 选择变更观察者（⑤事件端口形态的订阅面——检查器 UI-T22/域
 *        SelectionAdapter/状态呈现等消费者的统一接缝）。
 *
 * 回调纪律：UI 线程同步调用（服务全部入口都在 UI 线程）；回调内禁止
 * 再调用 SelectionService 的写入口（selectBusiness/handleTreeViewFrame
 * Selected/clearSelection——重入会造成广播中途状态改写）；抛出即装配
 * 缺陷（穿透，不吞——与 RuntimePublishBridge 观察者纪律同款）。
 */
class IUiSelectionObserver {
public:
    virtual ~IUiSelectionObserver() = default;

    /**
     * @brief 选择已变更（同步广播——change 为变更后完整事实，见
     *        SelectionChange 注释的两态语义）。
     *
     * @param change [in] 变更事实（值拷贝；回调返回后服务可能再次变更）
     */
    virtual void onSelectionChanged(const SelectionChange& change) = 0;
};

// =====================================================================
// 端口族（O-31 注入面——L5 装配层实现，ui 零对端类型知识）
// =====================================================================

/**
 * @brief 运行时名称映射的 ui 自有最小端口（L5 适配 runtime RuntimeNameMap
 *        ——SA-05 双射；B1-SPEC §4.1"三维拾取产生的 ObjectId 反解与名称
 *        呈现经 RuntimeNameMap，不在选择服务内私建名称映射"的端口承载）。
 *
 * 语义冻结（不改义——runtime.md §7.3/§7.4 双射两向）：
 *   - resolveObjectIdFromRuntimeName＝反解向（运行时名→ObjectId）——L3
 *     的判定依据；运行时名不在映射中（框架自建 Frame、无业务对应）或
 *     对象无业务身份 → nullopt（**合法二态，不是错误**——L3 反解失败
 *     分支的触发面，B1-SPEC §4.2 v1.1）；
 *   - resolveRuntimeName＝正向向（ObjectId→运行时名）——L2"已应用"判定
 *     依据：ObjectId 在当前已应用呈现的 NameMap 中存在时返回其运行时名
 *     （存在性＋高亮定位名一次取得）；纯草稿/纯结果对象 → nullopt（L2
 *     反例分支——不产生三维动作不报错）。
 *
 * 实现侧义务：两端查询必须基于**同一**当前已应用呈现的 NameMap（B1-SPEC
 * §4.3"同一 RuntimeNameMap 双射"——与 RuntimePublishBridge 当前呈现投影
 * 的 objectExists 同源；适配器绑定错位属装配缺陷）。快速返回（UI 线程
 * 呈现路径——NFR-PERF-01）；解析失败以 nullopt 表达，不抛环境错误。
 *
 * 为什么正向与反向在一个端口：两者是同一 SA-05 双射的两向，拆成两个端
 * 口会允许装配层绑定两份不同步的映射（双射一致性在类型上就保不住）；
 * 单端口让"同源"成为实现侧的一个绑定动作而非两个。
 */
class IUiRuntimeNameMapPort {
public:
    virtual ~IUiRuntimeNameMapPort() = default;

    /**
     * @brief 反解：运行时名→业务对象身份（L3 判定依据——SA-05 反解向）。
     *
     * @param runtimeName [in] 运行时对象名（TreeView Select Frame 事件/
     *                    三维拾取携带的 RuntimeNameMap 键；UTF-8）
     * @return 对应业务 ObjectId；无业务对应（框架自建 Frame 等）→
     *         nullopt——L3 反解失败分支的**合法**触发面（不报错）
     *
     * @note UI 线程调用；快速返回（呈现路径——NFR-PERF-01）。
     */
    virtual std::optional<core::ObjectId>
    resolveObjectIdFromRuntimeName(const std::string& runtimeName) const = 0;

    /**
     * @brief 正向：业务对象身份→运行时名（L2"已应用"判定依据——SA-05
     *        正向向；存在性＋定位名一次取得）。
     *
     * @param id [in] 业务对象 ObjectId
     * @return 该对象在当前已应用呈现 NameMap 中的运行时名；不存在（纯
     *         草稿/纯结果对象——未进编译链）→ nullopt——L2 反例分支的
     *         **合法**触发面（不产生三维动作不报错）
     *
     * @note UI 线程调用；与反解向同一 NameMap（实现侧绑定纪律见类注释）。
     */
    virtual std::optional<std::string>
    resolveRuntimeName(const core::ObjectId& id) const = 0;
};

/**
 * @brief 三维高亮出口的 ui 自有最小端口（L5 适配宿主三维呈现——L2 的
 *        动作出线缝）。
 *
 * 语义冻结（不改义——B1-SPEC §4.2 L2"选中对象若存在已应用 WorkCell
 * 对应物，**可以**触发三维高亮"）：高亮的呈现本体是宿主中央
 * RWStudioView3D 的事（D7 唯一三维视图；框架能力经框架公开 API）——
 * 本端口只把"高亮哪个运行时对象"这一**动作**交给 L5，服务侧零三维
 * 知识（O-43/零 Qt）。入参为运行时名（服务从 IUiRuntimeNameMapPort
 * 正向取得）——L5 按名定位呈现对象，不做二次解析（R-4 名称语义归
 * runtime，适配器不改义）。
 *
 * 可空注入（Deps.highlightOutlet）：空＝本装配形态无三维高亮场景（须
 * 显式声明——L2 判定照常执行、动作跳过并 Dev 留痕，不报错不降级），
 * 与诊断三件可空同款纪律（UI-T11 显式声明先例）。
 */
class IUiHighlightOutlet {
public:
    virtual ~IUiHighlightOutlet() = default;

    /**
     * @brief 高亮指定运行时对象（L2 正向分支的动作出线）。
     *
     * @param runtimeName [in] 要高亮的运行时对象名（NameMap 正向取得——
     *                    实现按名定位呈现对象；空串＝调用方违约，实现
     *                    侧 fail-fast 或忽略均属装配缺陷，服务保证非空）
     */
    virtual void highlightRuntimeObject(const std::string& runtimeName) = 0;

    /**
     * @brief 清除三维高亮（选中清空/切换到无对应物对象时的对称收口）。
     *
     * 尽力而为（呈现面动作；失败不回传——服务对失败仅 Dev 留痕路径
     * 开放给实现自身，不在端口签名上放大）。
     */
    virtual void clearHighlight() = 0;
};

// =====================================================================
// SelectionService——业务选择唯一汇聚点（INV-B3 的状态持有者）
// =====================================================================

/**
 * @brief 业务选择的唯一汇聚点（B1-SPEC §4.1；D3 的状态半区）。
 *
 * 状态（唯一写点全部在本类公有入口——INV-B3"两树都不持有对方的选中
 * 状态；业务选中唯一汇聚点是 SelectionService"的承载）：
 *   - 业务选中集（ObjectId 向量，去重＋选择序）＋其来源标记；
 *   - 仅运行时对象选择态（L3 反解失败时记录的运行时对象名——供状态
 *     呈现；不进业务树、不产生业务节点，INV-B1/B2 的结构性边界）；
 *   - 最新一次成功呈现刷新的存在性查询闭包（INV-B4 处置输入——来自
 *     RuntimePublishBridge 观察者回调的新投影 objectExists）。
 *
 * 行为契约（acceptance 2~4 逐条对应）：
 *   - selectBusiness：业务选中唯一写入口（树/搜索/命令/三维拾取反解后
 *     的共同汇聚面）；去重保序、广播 SelectionChange；
 *   - handleTreeViewFrameSelected：L3 编排入口——①反解（经
 *     IUiRuntimeNameMapPort，唯一通道）成功→树定位回调＋以 ProjectTree
 *     来源选中广播；②反解失败→树不动、不报错、记录仅运行时对象选择
 *     态并广播 runtimeOnly 事件；③全程零业务节点创建/零 ObjectId 伪造；
 *   - onPresentationReplaced（IUiPresentationRefreshObserver——UI-T20
 *     接缝）：按新投影 objectExists 对当前选中执行
 *     selectionDispositionAfterRefresh 求值——Keep＝保持（不重复广播，
 *     状态零变化）；Clear＝置空并广播空选中；"本无选中"＝无操作（置空
 *     空集是恒等动作——UI-T20 词表注释原文）。呈现刷新失败回调＝零触碰
 *     （旧呈现保持语义的选中侧延伸——选中不得因失败而改变）；
 *   - L2 高亮：selectBusiness 后对单选对象做正向存在性判定（经
 *     IUiRuntimeNameMapPort）——存在→IUiHighlightOutlet 高亮；不存在
 *     （纯草稿/纯结果）→无动作不报错。多选/清空选中→清除高亮（L2 第一
 *     版承诺只对"单个已应用对象"给高亮语义——多选高亮不在词表内，保守
 *     清除是唯一无歧义动作）。
 *
 * 零越界声明（结构承载，验收对抗项）：本服务依赖闭包＝nameMap＋
 *   highlightOutlet＋treeLocator＋treeModel 查询＋Dev 日志——零命令网关
 *   （零修订写面）、零缓存表面、零当前性表面、零框架 TreeView 选中 API
 *   （L4 防过度交付——树行反向选中联动在类型面不可表达：本头不存在任何
 *   "选中 TreeView 行"形态的端口或回调）。
 *
 * 错误语义（AGENTS §3 二分）：
 *   - 调用方契约违约 → std::invalid_argument fail-fast（必填端口缺失、
 *     传入无效 ObjectId〔全零保留值〕等——静默收下会制造"选中已生效"
 *     的假象，禁吞）；
 *   - 反解失败/正向不存在 → 环境轨正常分支（optional nullopt——L2/L3
 *     的既定词表行为，不抛不诊断；Dev 留痕可观测）。
 *
 * 线程约束：非线程安全——仅 UI 线程访问（ui.md §3.4 M-1）。生命周期：
 *   L5 装配层持有；订阅者 RAII 句柄退订（core ⑤事件总线同款）。
 */
class SelectionService final : public IUiPresentationRefreshObserver {
public:
    /**
     * @brief 装配依赖（L5 装配期一次性给出——与其他控制器同款纪律）。
     *
     * nameMap/treeLocator 必填（构造期 fail-fast——无反解通道/无树定位
     * 回调的服务违反 L2/L3 承诺，属装配缺陷）；highlightOutlet 与
     * devLog 允许为空＝无三维高亮/无日志场景（须显式声明——L2 动作跳过
     * 照常判定，Dev 留痕静默）。
     */
    struct Deps {
        /// 运行时名称映射端口（必填——SA-05 反解/正向唯一通道）。
        std::shared_ptr<IUiRuntimeNameMapPort> nameMap;
        /// L3 反解成功后的树定位回调（必填——B1-SPEC §4.2 L3"项目树
        /// 定位并选中"的落点缝；入参＝目标 ObjectId，由 IndustrialProject
        /// Tree 面板装配期注册。bool 返回＝树确实定位到了（一致性自证
        /// 用——false 说明树内容与反解结果漂移，Dev 留痕不阻断）。
        std::function<bool(const core::ObjectId&)> treeLocator;
        /// 三维高亮出口（可空＝无三维场景——L2 动作跳过，见端口注释）。
        std::shared_ptr<IUiHighlightOutlet> highlightOutlet;
        /// 开发日志通道（可空＝显式声明的无日志场景——UI-T11 同款）。
        std::shared_ptr<diagnostics::IDevLogSink> devLog;
    };

    /**
     * @brief 构造选择服务（装配期——依赖校验 fail-fast）。
     *
     * @param deps [in] 装配依赖（nameMap/treeLocator 必填非空，缺失抛
     *             std::invalid_argument——禁止构造违反 L2/L3 承诺的服务）
     */
    explicit SelectionService(Deps deps);

    // ---- 业务选中写入口（唯一汇聚面——全部来源经此写入）----

    /**
     * @brief 写入业务选中集并广播（acceptance 2 的唯一写入口）。
     *
     * 去重（保持首次出现序——NFR-COR-02 稳定序）后与既有集比较：集合与
     * 来源都无变化＝幂等无操作（不广播——订阅者只见真变化）；否则更新
     * 状态、清除仅运行时对象选择态（业务选中生效即运行时暂态作废——
     * 两态互斥见 SelectionChange 注释）、按需清除三维高亮（多选/清空）、
     * L2 判定（单选时正向存在性→高亮出线）、广播 SelectionChange。
     *
     * @param objectIds [in] 选中对象集（可空＝清除选中；元素必须为有效
     *                  ObjectId〔非全零保留值〕——无效元素抛
     *                  std::invalid_argument，整批拒绝不部分收下）
     * @param source    [in] 本次变更来源（四值封闭词表——SelectionSource）
     *
     * @throws std::invalid_argument 任一元素为无效 ObjectId（全零保留值）
     */
    void selectBusiness(std::vector<core::ObjectId> objectIds,
                        SelectionSource source);

    /**
     * @brief 清除业务选中（selectBusiness 空集形态的显式命名面——语义
     *        等价 selectBusiness({}, source)；独立成方法是为了调用点
     *        可读性：清空是有意动作，不是"忘传参数"）。
     *
     * @param source [in] 触发清除的来源（事件照常携带——消费者可分流）
     */
    void clearSelection(SelectionSource source);

    // ---- L3 编排入口（TreeView Select Frame 事件——L5 转发，UI 线程）----

    /**
     * @brief 消费一条 TreeView Select Frame 事件并执行 L3 编排
     *        （acceptance 3/4——B1-SPEC §4.2 L3＋§4.2 v1.1 失败分支）。
     *
     * 编排路径（acceptance 4 四个具名分支一一对应）：
     *   ①反解成功（nameMap 反解得 ObjectId 且树定位回调命中）→ 树定位
     *     选中＋以 ProjectTree 来源广播业务选中（树不动＝违反本分支）；
     *   ②反解失败（nullopt）→ 树定位回调**不调用**（树不动）、不报错、
     *     不出用户级诊断；记录仅运行时对象选择态并广播 runtimeOnly 事件
     *     （状态呈现消费者据此呈现"运行时对象选中"）；
     *   ③反解失败全程零业务节点创建、零 ObjectId 伪造（selectedObjectIds
     *     保持不变——结构性保证：失败路径根本没有 ObjectId 可写）；
     *   ④反解成功但树定位回调未命中（树内容漂移——装配时序的边角）→
     *     Dev 留痕、不广播不伪造（**不**落成"选中了不存在的节点"）。
     *
     * @param runtimeFrameName [in] Select Frame 事件携带的运行时对象名
     *                         （RuntimeNameMap 键；空串＝转发方违约，抛
     *                         std::invalid_argument——空名无反解语义）
     *
     * @throws std::invalid_argument runtimeFrameName 为空串
     */
    void handleTreeViewFrameSelected(const std::string& runtimeFrameName);

    // ---- 状态查询（消费者按需拉取——与事件推送互补）----

    /// @brief 当前业务选中集（去重＋选择序；空＝无业务选中）。
    const std::vector<core::ObjectId>& selectedObjectIds() const noexcept;

    /// @brief 当前业务选中来源（无选中＝nullopt——不虚构来源）。
    std::optional<SelectionSource> selectionSource() const noexcept;

    /// @brief 是否处于仅运行时对象选择态（L3 反解失败后的暂态）。
    bool hasRuntimeOnlySelection() const noexcept;

    /// @brief 仅运行时对象选择态记录的运行时对象名（非该态＝nullopt）。
    const std::optional<std::string>& runtimeOnlyObjectName() const noexcept;

    // ---- ⑤事件订阅（RAII 句柄——core 事件总线同款语义）----

    /**
     * @brief 订阅选择变更事件（⑤事件端口形态——unique_ptr 句柄析构即
     *        退订，重复退订幂等）。
     *
     * @param observer [in] 观察者（裸指针登记——调用方保证存活期覆盖
     *                 订阅期；同一观察者重复订阅按一次计〔幂等〕）
     * @return RAII 订阅句柄（unsubscribe 后不再投递）
     *
     * @throws std::invalid_argument observer 为空（调用方违约）
     */
    std::unique_ptr<core::IEventSubscription>
    subscribe(IUiSelectionObserver& observer);

    // ---- IUiPresentationRefreshObserver（UI-T20 接缝——INV-B4 消费时机）----

    /**
     * @brief 呈现已原子替换（桥事务④提交后回调——INV-B4 选中处置时机）。
     *
     * 处置规则（UI-T20 selectionDispositionAfterRefresh 纯规则的首消费）：
     * 以新投影 objectExists 为存在性事实源——Keep＝保持选中（状态零变化
     * 不广播）；Clear＝置空选中并广播空选中事件（来源保持原值——清空
     * 不是新来源的选中，携带原来源便于消费者归因）；"本无选中"＝无操作
     * 不广播。同时刷新存在性查询闭包（供后续 selectBusiness 的 L2 判定
     * 前置语义——闭包仅作 Dev 自证，判定以 nameMap 端口为唯一事实源）。
     *
     * @param view [in] 新当前呈现投影（值拷贝——objectExists 为 NameMap
     *             同源存在性查询）
     */
    void onPresentationReplaced(
        const PresentationViewProjection& view) override;

    /**
     * @brief 呈现刷新失败（桥事务任一步失败——旧呈现保持）。
     *
     * 选中零触碰（INV-B4"不改变业务树选中"在失败路径的延伸——呈现没换，
     * 选中处置无从谈起）；Dev 留痕可观测，不出用户级诊断（呈现失败已由
     * 桥出 UI-PRESENTATION-REFRESH-FAILED——选中侧不重复出线）。
     *
     * @param facts       [in] 触发刷新的事件事实（Dev 留痕素材）
     * @param reasonToken [in] 桥侧失败词表 token（透传留痕，不加工）
     */
    void onPresentationRefreshFailed(const PresentationEventFacts& facts,
                                     const std::string& reasonToken) override;

private:
    /**
     * @brief 订阅句柄实现（RAII——析构/unsubscribe 即从登记表摘除）。
     */
    class Subscription;

    /// @brief 摘除观察者（句柄退订路径专用——头外实现，幂等）。
    void removeObserver(IUiSelectionObserver* observer);

    /// @brief 内部广播（保持序去重后调用；重入保护由调用点纪律保证）。
    void broadcast(const SelectionChange& change);

    /// @brief L2 高亮判定（单选时正向存在性→高亮出线；其余形态清除）。
    void applyHighlightDecision();

    /// @brief Dev 留痕（允许为空＝显式声明的无日志场景）。
    void emitDevLine(const std::string& message) const;

    /// 装配依赖（nameMap/treeLocator 构造期校验非空；outlet/devLog 可空）。
    Deps m_deps;
    /// 业务选中集（去重＋选择序——selectBusiness/clear 唯一写点）。
    std::vector<core::ObjectId> m_selected;
    /// 业务选中来源（m_selected 非空时有意义——随最后一次业务选中更新）。
    SelectionSource m_source = SelectionSource::ProjectTree;
    /// 仅运行时对象选择态（L3 反解失败记录；业务选中生效即作废）。
    std::optional<std::string> m_runtimeOnlyName;
    /// 选择变更订阅者（登记序即广播序；RAII 句柄管理摘除）。
    std::vector<IUiSelectionObserver*> m_observers;
};

}  // namespace ui
}  // namespace ird
}  // namespace sdurws

#endif  // SDURWS_IRD_UI_SELECTIONSERVICE_HPP
