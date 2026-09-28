/**
 * @file   RuntimePublishBridge.hpp
 * @brief  宿主运行时发布桥（RuntimePublishBridge）——已应用 WorkCell 的
 *         发布/重编译/项目切换 → 宿主呈现视图与官方 TreeView 刷新编排
 *         （UI-T20，方案 B.1 迁移链，SA-18 D10 消费侧落位）。
 *
 * 设计依据：
 *   - 任务契约 tasks/foundation/UI-T20.json acceptance 1~6（本头＋同名实现
 *     ＋两测试套件承载）；
 *   - units/ui.md §13 UI-T20 行（"RuntimePublishBridge：…刷新编排＋呈现
 *     视图生命周期管理；重编译/切换后 TreeView 与三维一致（INV-B4）；呈现
 *     刷新零修订零缓存写入"）、§3.1（ui 产品面零对 runtime 链接/include
 *     ——本头对 runtime 零类型知识，协作全部经自有最小端口注入）、
 *     §3.5（新增稳定码 UI-PRESENTATION-REFRESH-FAILED——登记行同批）；
 *   - B1-SPEC §4.3（D10 详述：呈现/计算隔离与复用；三类身份绑定
 *     modelIdentity/appliedRevisionId/presentationIdentity 的对账与"旧
 *     呈现身份失效"规则；反向隔离）、§3.3（INV-B4：重建后 TreeView 与三维
 *     一致、零修订、业务选中"存在性校验后保持或置空，不静默换选"）、§4.2
 *     L1~L4（联动契约——本桥只承载选中处置规则半区，树/检查器联动归
 *     UI-T21/T22）、§7 验收映射（D10 细则的 UI-T20 半区＝发布/重编译/
 *     切换的 appliedRevisionId 对账＋旧呈现身份失效）；
 *   - runtime 公共契约 HostPresentationView（RT-T14——呈现视图唯一构造
 *     入口 createHostPresentationView；三类身份只读对账面。**本头不 include
 *     该头**——适配归 L5 装配层，形状由契约测试对真实面钉住）；
 *   - ARCHITECTURE §7.12/SA-18（宿主唯一、双树边界、呈现与计算隔离——
 *     官方 TreeView/中央三维为宿主既有行为，桥只编排刷新不改宿主交互，
 *     O-43 边界）；
 *   - knownPitfalls：P-RT-4（呈现侧构造复用同一变换——桥对呈现构造零知识，
 *     只消费 L5 适配后的完整投影，结构性杜绝私建第二构造路径）；O-43
 *     （宿主融合边界——刷新编排经 outlet 端口出线，桥零 Qt、零宿主句柄）；
 *   - 需求 ARC-02（框架集成边界）、CON-02（当前性——呈现刷新不掩盖
 *     Superseded：桥依赖闭包零当前性表面，结构承载）。
 *
 * 背景说明（D10 的消费侧落法）：
 *   计算侧快照由 runtime 编译链发布（Publish 事件）；宿主呈现（TreeView/
 *   三维）消费的是同一编译产物的只读呈现视图（RT-T14）。桥的职责是把
 *   "已应用修订变化"这一事实编排为一次**呈现刷新事务**：
 *   完整构造（source 取新呈现）→ 身份对账（三类绑定关系核对）→
 *   原子替换（outlet 单入口应用到宿主）→ 提交（更新当前呈现＋通知观察者）。
 *   任一步失败：保留旧宿主 WorkCell 与旧三维场景、出稳定诊断
 *   UI-PRESENTATION-REFRESH-FAILED（"项目已应用但呈现刷新失败"）、
 *   不回滚已经合法产生的项目修订（桥依赖闭包零修订写面——结构承载）。
 *
 * 端口纪律（O-31 注入面——与 UiPorts.hpp 同款）：
 *   ui 产品面对 runtime 零链接零 include（ARCH §3.5 白名单只有 ui→core/
 *   diagnostics）；呈现构造与宿主刷新都由 L5 装配层适配——source 适配器
 *   包装 runtime::createHostPresentationView 的产物（RT-T14 契约面唯一
 *   构造路径），outlet 适配器经框架公开 API 单入口刷新宿主（TreeView＋
 *   三维场景同源——INV-B4 一致性的承载半区）。事件由 L5 从 runtime ⑤
 *   事件端口转发（Marshal 回 UI 线程后调 handlePresentationEvent）。
 *
 * 线程模型：桥自身零同步——全部入口（handlePresentationEvent/attach/
 *   switch/detach/查询）只在 UI 线程调用（§3.4 M-1 纪律的 L5 消费点）；
 *   跨线程到达的 runtime 事件由 L5 负责 Marshal（迟到事件按项目归属
 *   拒绝，见 handlePresentationEvent 注释）。
 *
 * 生命周期/所有权：由 L5 装配层持有（unique_ptr 惯例同其他控制器）；
 *   端口实现与观察者的生命周期由装配层保证覆盖桥的存活期；桥持有
 *   source/outlet 的 shared_ptr（共享——装配层可同时用于其他装配面），
 *   观察者以 weak_ptr 弱持有（观察者析构自动退订，无悬挂）。
 */
#ifndef SDURWS_IRD_UI_RUNTIMEPUBLISHBRIDGE_HPP
#define SDURWS_IRD_UI_RUNTIMEPUBLISHBRIDGE_HPP

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <sdurws/ird/core/Identity.hpp>        // core::ProjectId/RevisionId/ObjectId（事件与三类身份承载——表内登记边直用）
#include <sdurws/ird/core/Digest.hpp>          // core::ContentIdentity（modelIdentity 承载——CON-05 内容身份）
#include <sdurws/ird/diagnostics/Catalog.hpp>  // diagnostics::IDiagnosticSink/IDevLogSink（用户级/Dev 级出线——表内登记边）
#include <sdurws/ird/diagnostics/Factory.hpp>  // diagnostics::IDiagnosticFactory（create 唯一入口——§9.2 同款纪律）

namespace sdurws {
namespace ird {
namespace ui {

// =====================================================================
// 呈现事件词表与事实（⑤事件端口的 ui 侧值形态）
// =====================================================================

/**
 * @brief 已应用 WorkCell 呈现事件的种类词表（封闭——新增种类须 B1-SPEC
 *        增量修订，CompileStage 语义稳定第一）。
 */
enum class PresentationEventKind : std::uint8_t {
    /// 首次发布（打开协议后已应用修订首次可呈现——S10 Published 的呈现面）。
    Publish,
    /// 重编译（同一修订重新发布——草稿应用/重算后的呈现刷新）。
    Recompile,
    /// 项目切换（§5.4 S2——A→B 绑定迁移后 B 侧首次发布）。
    ProjectSwitch,
};

/**
 * @brief 呈现事件事实（L5 从 runtime 事件转发时携带的对账第二源）。
 *
 * 为什么要事件侧再带一份身份：三类身份对账（B1-SPEC §4.3 v1.1）需要
 * "呈现对象之外"的对照基准——appliedRevisionId 对照**本次发布修订**、
 * modelIdentity 对照**来源规范模型内容**。这两个事实由 L5 从项目会话态
 * （已应用修订）与编译事件（模型身份）取得后随事件供给；桥把事件事实与
 * 呈现投影逐项核对，任何一项失配即判"身份对账失败"（刷新失败路径）。
 * "不要求字段值相等"指三类身份彼此异型异值（模型身份≠修订≠呈现实例）；
 * 对账核对的是**绑定关系**（投影字段与事件事实的同源一致性）。
 *
 * 值语义聚合体；事件归 L5 构造（转发行）——桥不校验字段完整性以外的
 * 业务语义。
 */
struct PresentationEventFacts {
    /// 事件种类（决定刷新编排的呈现面语义——三类走同一事务，词表只做
    /// 留痕/诊断定位，不改变编排路径）。
    PresentationEventKind kind = PresentationEventKind::Publish;
    /// 事件归属项目（迟到事件过滤键——与桥绑定会话不等即拒绝）。
    core::ProjectId project;
    /// 本次发布修订（对账②基准——与呈现投影 appliedRevisionId 核对）。
    core::RevisionId appliedRevision;
    /// 来源规范模型内容身份（对账①基准——与呈现投影 modelIdentity 核对）。
    core::ContentIdentity modelIdentity;
};

// =====================================================================
// 呈现载荷投影（RT-T14 三类身份的 ui 侧值形态——O-31 载体）
// =====================================================================

/**
 * @brief 呈现视图的 ui 侧投影（L5 适配器把 RT-T14 HostPresentationView
 *        折叠为本值——ui 产品面零 runtime 类型知识）。
 *
 * 字段与 RT-T14 面的对应（形状由契约测试对真实视图钉住）：
 *   - modelIdentity        ← HostPresentationView::modelIdentity()
 *     （＝快照 modelIdentity——对照来源规范模型内容可验证）；
 *   - appliedRevisionId    ← HostPresentationView::appliedRevisionId()
 *     （＝工厂入参——对照本次发布修订可验证）；
 *   - presentationIdentity ← HostPresentationView::presentationIdentity()
 *     （每次构造新生成——"重建即新身份"；词表复用 core::ObjectId 生成
 *     机制，不表示业务对象身份）；
 *   - hostPayload          ← 适配器持有的呈现本体句柄（**不透明载体**——
 *     ui 不解引用；L5 outlet 适配器以其把已应用 WorkCell 交给宿主单入口
 *     刷新。shared_ptr<const void> 形态保证： deleter 随原始句柄存活——
 *     ui 拷贝投影即持有呈现视图存活期，与 RT-T14"持视图即持快照"一致）；
 *   - objectExists         ← 适配器绑定到该呈现视图 NameMap 的 ObjectId
 *     存在性查询（resolveObjectId 同源——INV-B4"选中存在性经 ObjectId
 *     校验"的判定输入；桥侧零名称拼装，R-4）。
 *
 * isComplete() 是桥侧契约面完整位：任一身份无效或载体/查询缺失即"不
 * 完整投影"——完整构造前置（acceptance 3）在桥侧的最后防线。
 */
struct PresentationViewProjection {
    /// 绑定①：来源规范模型内容身份（对照快照/事件事实可验证）。
    core::ContentIdentity modelIdentity;
    /// 绑定②：来源已应用修订（对照本次发布修订可验证）。
    core::RevisionId appliedRevisionId;
    /// 绑定③：本次呈现构造实例身份（每次构造新生成——重建即新身份）。
    core::ObjectId presentationIdentity;
    /// 宿主刷新载体（不透明——L5 持有真实类型；空＝适配缺陷，桥拒绝）。
    std::shared_ptr<const void> hostPayload;
    /// 新视图 ObjectId 存在性查询（呈现视图 NameMap 同源；空＝适配缺陷）。
    std::function<bool(const core::ObjectId&)> objectExists;

    /// @brief 契约面完整位（三类身份有效＋载体与存在性查询非空）。
    bool isComplete() const noexcept
    {
        return modelIdentity.isValid() && appliedRevisionId.isValid()
               && presentationIdentity.isValid() && hostPayload != nullptr
               && static_cast<bool>(objectExists);
    }
};

/**
 * @brief 宿主应用报告（outlet 单入口的执行结果——ok=false 时宿主未动）。
 *
 * outlet 契约（L5 实现方义务）：applyPresentation 要么完整成功（TreeView
 * 与三维场景都已从同一呈现载体刷新——宿主单入口的原子半区），要么整体
 * 失败且宿主呈现保持原状（保留旧 WorkCell 与旧三维场景——acceptance 3
 * 的宿主半区）；部分应用后报失败属实现违约。failureToken 为 L5 侧稳定
 * token（透传进诊断明细，不吞错不改义）。
 */
struct PresentationApplyReport {
    bool ok = false;            ///< true＝宿主呈现已完整刷新
    std::string failureToken;   ///< 稳定 token（ok=false 时非空——L5 词表）
    std::string failureDetail;  ///< 失败明细（透传——诊断 cause 的素材）
};

/**
 * @brief 呈现刷新事务结果（handlePresentationEvent 的返回值）。
 *
 * failureToken 为**桥侧封闭词表**（诊断留痕与测试断言用）：
 *   - "no-session"       ：无绑定会话时的迟到事件（Dev 留痕，非刷新失败
 *                          ——不出用户级诊断）；
 *   - "stale-event"      ：事件归属项目与绑定会话不符（§6.2 迟到事件
 *                          过滤同型——旧项目事件不得刷新新项目呈现，
 *                          Dev 留痕）；
 *   - "construct-failed" ：source 未能构造完整呈现（acceptance 3 构造
 *                          失败半区——出稳定诊断）；
 *   - "incomplete-view"  ：投影契约面不完整（isComplete()==false——出
 *                          稳定诊断）；
 *   - "identity-mismatch": 三类身份对账失败（含"重建未换新实例身份"
 *                          ——出稳定诊断）；
 *   - "apply-failed"     ：宿主应用失败（acceptance 3 发布失败半区——
 *                          出稳定诊断，L5 token/明细透传）。
 */
struct PresentationRefreshOutcome {
    bool applied = false;       ///< true＝原子替换完成（当前呈现已更新）
    std::string failureToken;   ///< applied=false 时非空（上注封闭词表）
};

// =====================================================================
// 端口族（O-31 注入面——L5 装配层实现，ui 零对端类型知识）
// =====================================================================

/**
 * @brief 呈现构造的 ui 自有最小端口（L5 适配 runtime RT-T14 契约面）。
 *
 * 语义冻结（不改义——冻结基准 B1-SPEC §4.3"同一构造规则"＋runtime
 * HostPresentationView 工厂契约）：实现侧**只**经 createHostPresentationView
 * (已发布快照, 已应用修订) 构造呈现（工厂唯一入口——呈现侧私建第二构造
 * 路径在适配面即被禁止，P-RT-4 的执行点）；把产物折叠为投影返回。
 *
 * 完整构造前置（acceptance 3）：返回 ok 投影＝构造已完整（半成品不存在
 * ——工厂对非法输入整体失败）；构造不可达（快照缺失/工厂拒绝等）返回
 * nullopt——桥按"construct-failed"处置。装配缺陷（非本修订不可呈现的
 * 契约违约）应以异常穿透（禁止吞错），桥不捕获。
 *
 * 线程约束：UI 线程调用（桥事件路径；适配器内部如需跨线程取数自行
 * Marshal——桥不感知）。
 */
class IUiPresentationSource {
public:
    virtual ~IUiPresentationSource() = default;

    /**
     * @brief 为本次发布事件构造完整呈现投影（事务第一步——完整构造）。
     *
     * @param facts [in] 事件事实（适配器按其定位已发布快照与已应用修订；
     *              桥保证 facts 身份字段已过有效性校验）
     * @return 完整投影（isComplete()==true——桥侧不再二次修补）；
     *         nullopt＝本修订不可呈现（无半成品——构造失败轨）
     */
    virtual std::optional<PresentationViewProjection>
    fetchPresentation(const PresentationEventFacts& facts) = 0;
};

/**
 * @brief 宿主呈现刷新的 ui 自有最小端口（L5 适配宿主公开 API——O-43
 *        "桥只编排刷新不改宿主交互"的出线面）。
 *
 * 语义冻结（不改义——B1-SPEC §3.3 INV-B4＋acceptance 3/4）：实现侧经
 * 宿主**单入口**（框架公开 API；如 RobWorkStudio 的 WorkCell 设置面）把
 * 呈现载体交给宿主——TreeView 与三维场景从同一载体刷新（一致性的宿主
 * 半区）；框架私有树行/选中行为不在适配面触碰（SA-02 零框架修改）。
 * 失败路径见 PresentationApplyReport 契约（整体失败＋宿主保持原状）。
 *
 * 线程约束：UI 线程调用（桥事务第三步）。
 */
class IUiPresentationOutlet {
public:
    virtual ~IUiPresentationOutlet() = default;

    /**
     * @brief 把新呈现应用到宿主（事务第三步——原子替换）。
     *
     * @param view [in] 已过身份对账的呈现投影（hostPayload 为适配器自己
     *              放入的载体——实现按原类型取回，禁止跨适配器解释）
     * @return 应用报告（ok=false 时宿主呈现保持原状——桥保留旧当前呈现）
     */
    virtual PresentationApplyReport
    applyPresentation(const PresentationViewProjection& view) = 0;

    /**
     * @brief 释放宿主呈现（会话拆除——呈现视图生命周期随宿主会话销毁的
     *        宿主半区；尽力而为——桥对失败仅 Dev 留痕，无恢复动作）。
     */
    virtual void releasePresentation() = 0;
};

/**
 * @brief 呈现刷新结果观察者（替换/失败两事件——UI-T21 SelectionService
 *        等后续消费者的接缝；本任务以测试替身承载消费语义）。
 *
 * 回调纪律：UI 线程同步调用（桥事务内）；观察者内不得回调桥（重入
 * 禁令——状态机中途改写）；抛出即装配缺陷（穿透，不吞）。
 */
class IUiPresentationRefreshObserver {
public:
    virtual ~IUiPresentationRefreshObserver() = default;

    /**
     * @brief 呈现已原子替换（事务提交后通知——view 即新的当前呈现）。
     *
     * @param view [in] 新当前呈现投影（值拷贝；INV-B4 消费者在此执行
     *              选中处置等呈现面联动——桥自身零业务树选中知识）
     */
    virtual void onPresentationReplaced(const PresentationViewProjection& view) = 0;

    /**
     * @brief 呈现刷新失败（事务任一步失败——旧当前呈现保持不变）。
     *
     * @param facts       [in] 触发刷新的事件事实
     * @param reasonToken [in] 桥侧失败词表 token（PresentationRefreshOutcome
     *                    封闭词表——与诊断同源）
     */
    virtual void onPresentationRefreshFailed(const PresentationEventFacts& facts,
                                             const std::string& reasonToken) = 0;
};

// =====================================================================
// INV-B4 选中处置规则（纯函数——业务选中语义的呈现半区）
// =====================================================================

/**
 * @brief 呈现重建后的业务选中处置词表（**双值封闭**——词表中不存在
 *        "改选其他对象"值，"不静默换选"由词表结构保证）。
 */
enum class SelectionDisposition : std::uint8_t {
    /// 保持选中（选中对象在新呈现中仍然存在——ObjectId 存在性通过）。
    Keep,
    /// 置空选中（选中对象已不存在；对"本无选中"输入为无操作——置空
    /// 空集是恒等动作，不产生任何选中事件）。
    Clear,
};

/**
 * @brief 呈现重建后的业务选中处置（INV-B4 纯规则——B1-SPEC §3.3"业务
 *        选中对象存在性经 ObjectId 校验后决定保持或置空——失效呈现按
 *        UX 当前性口径，不静默换选"的规则承载）。
 *
 * 为什么是纯函数而不是桥内状态机：业务选中状态归 SelectionService
 * （UI-T21，§4.1 唯一汇聚点——PA-1）；本任务先于其落位，把可验证的
 * 规则半区以纯函数交付（消费时机＝UI-T21 的选择服务在
 * onPresentationReplaced 中按新投影 objectExists 求值）。存在性查询
 * 输入＝新呈现投影的 objectExists（呈现视图 NameMap 同源——与三维/
 * TreeView 内容一致的事实源）。
 *
 * @param selectionPresent       [in] 当前是否存在业务选中
 * @param objectExistsInNewPresentation [in] 选中对象在新呈现中是否存在
 *                                      （经 ObjectId 校验——NameMap 同源）
 * @return Keep＝保持；Clear＝置空（含"本无选中"的无操作形态）
 */
SelectionDisposition selectionDispositionAfterRefresh(
    bool selectionPresent, bool objectExistsInNewPresentation);

// =====================================================================
// RuntimePublishBridge——宿主运行时发布桥（刷新事务唯一编排者）
// =====================================================================

/**
 * @brief 宿主运行时发布桥（已应用 WorkCell 发布/重编译/项目切换 →
 *        宿主呈现与官方 TreeView 刷新的事务编排器）。
 *
 * 编排路径（acceptance 1/3——四步事务，任一步失败即入失败路径）：
 *   ①事件过滤（绑定会话核对——迟到/无会话事件 Dev 拒绝，不进事务）；
 *   ②完整构造（source.fetchPresentation——nullopt 即 construct-failed）；
 *   ③身份对账（投影完整位＋三类绑定关系＋"重建即新身份"——任一失配即
 *     identity-mismatch/incomplete-view；对账在应用**之前**——失配对象
 *     永远到不了宿主）；
 *   ④原子替换＋提交（outlet.applyPresentation 单入口；ok 才更新当前
 *     呈现并通知观察者——失败保留旧当前呈现）。
 *
 * 失败路径（acceptance 3——四步中②~④任一失败）：保留旧宿主 WorkCell
 * 与旧三维场景（outlet 未被调用或报告整体失败——宿主半区）、出稳定诊断
 * UI-PRESENTATION-REFRESH-FAILED（"项目已应用但呈现刷新失败"——码值
 * 登记 units/ui.md §3.5＋uiDiagnosticCodeDescriptors 注册）、不回滚已经
 * 合法产生的项目修订（桥依赖闭包零修订写面——项目修订在命令边界产生，
 * 呈现刷新对它零知识，结构承载）。
 *
 * 会话生命周期（acceptance 1）：attachHostSession（宿主会话建立——
 * 打开协议成功后）/ switchHostSession（项目切换绑定——旧呈现保留到
 * 新呈现替换成功，切换失败保留旧宿主画面）/ detachHostSession（宿主
 * 会话销毁——释放宿主呈现＋清空当前呈现）。项目切换后旧项目事件按
 * 归属比对拒绝（旧 presentationIdentity 随旧呈现废弃，不得继续冒充
 * 当前呈现——B1-SPEC §4.3 v1.1 失效规则）。
 *
 * 零越界声明（结构承载，验收对抗项）：桥依赖闭包＝source＋outlet＋
 * 诊断三件——零命令网关（零修订写面）、零缓存表面（零缓存写入）、
 * 零当前性表面（CON-02 不被呈现刷新掩盖）、零业务选中状态（INV-B4
 * "不改变"半区；规则半区见 selectionDispositionAfterRefresh）。
 *
 * 错误语义（AGENTS §3 二分）：
 *   - 调用方契约违约 → std::logic_error/std::invalid_argument fail-fast
 *     （重复挂接/未绑定拆绑/事件事实身份字段无效/必填端口缺失等——
 *     静默降级会制造"呈现已是最新"的假象，禁吞错）；
 *   - 呈现刷新失败（构造/对账/应用）→ 环境轨：返回值词表＋稳定诊断
 *     ＋Dev 留痕，旧呈现保持——不抛（刷新失败是 D10 事务的正常分支）。
 *
 * 线程约束：非线程安全——仅 UI 线程访问（§3.4 M-1；与 UiSessionController
 * 同口径）。生命周期：L5 装配层持有；观察者 weak_ptr 弱持有（析构自动
 * 退订）；端口 shared_ptr 共享持有。
 */
class RuntimePublishBridge {
public:
    /**
     * @brief 桥的装配依赖（L5 装配期一次性给出——与其他控制器同款纪律）。
     *
     * source/outlet 必填非空（构造期 fail-fast——无刷新通道的桥是装配
     * 缺陷）；诊断三件允许为空＝无目录/无日志场景（须显式声明——失败
     * 路径在该场景以返回值＋Dev 通道外的承载呈现，不虚构条目，UI-T11
     * 同款纪律）。
     */
    struct Deps {
        /// 呈现构造端口（必填——RT-T14 契约面的唯一注入缝）。
        std::shared_ptr<IUiPresentationSource> source;
        /// 宿主刷新端口（必填——宿主单入口的唯一出线缝）。
        std::shared_ptr<IUiPresentationOutlet> outlet;
        /// 用户级诊断 sink（稳定码目录通道；允许为空）。
        std::shared_ptr<diagnostics::IDiagnosticSink> diagSink;
        /// 诊断工厂（create 唯一入口；允许为空＝无目录测试场景）。
        std::shared_ptr<diagnostics::IDiagnosticFactory> diagFactory;
        /// 开发日志通道（Dev 级事实唯一出线；允许为空＝无日志场景）。
        std::shared_ptr<diagnostics::IDevLogSink> devLog;
    };

    /**
     * @brief 构造桥（装配期——依赖校验 fail-fast）。
     *
     * @param deps [in] 装配依赖（source/outlet 必填非空，缺失抛
     *             std::invalid_argument——禁止构造不可刷新的桥）
     */
    explicit RuntimePublishBridge(Deps deps);

    // ---- 宿主会话生命周期（acceptance 1——呈现视图生命周期的桥半区）----

    /**
     * @brief 绑定宿主会话（宿主会话建立——打开协议成功后的呈现侧起点）。
     *
     * 绑定后 currentPresentation() 为空（尚无呈现——等待首次发布事件）。
     * 重复绑定（已绑定时再 attach）抛 std::logic_error（状态机界面侧
     * 不接纳叠绑——UiSessionController bindSession 同款纪律）。
     *
     * @param project [in] 会话归属项目（全零保留值＝无效——抛
     *                std::invalid_argument）
     */
    void attachHostSession(const core::ProjectId& project);

    /**
     * @brief 切换绑定项目（项目切换——B1-SPEC §5.4 S2 的呈现侧承载）。
     *
     * 与 attach 的区别：**不释放宿主呈现**——旧项目的宿主画面保留到新
     * 项目呈现替换成功（acceptance 3"切换失败保留旧宿主 WorkCell 与旧
     * 三维场景"的桥半区）；绑定变更后旧项目事件按归属比对拒绝
     * （"stale-event"——旧呈现身份不得继续冒充当前呈现）。未绑定时
     * 切换抛 std::logic_error（切换只在会话内有意义）。
     *
     * @param project [in] 新绑定项目（有效性校验同 attach）
     */
    void switchHostSession(const core::ProjectId& project);

    /**
     * @brief 解绑宿主会话（宿主会话销毁——项目关闭/切换拆除的呈现侧收口）。
     *
     * 执行：outlet.releasePresentation()（宿主呈现释放——尽力而为）→
     * 清空当前呈现与绑定。此后事件按"no-session"拒绝。未绑定时抛
     * std::logic_error（对称纪律）。
     */
    void detachHostSession();

    // ---- ⑤事件端口消费入口（L5 从 runtime 事件转发——UI 线程）----

    /**
     * @brief 消费一条呈现事件并执行刷新事务（acceptance 1 编排入口）。
     *
     * 事件过滤（①步）：未绑定会话→"no-session"；facts.project 与绑定
     * 不符→"stale-event"。两态均 Dev 留痕返回、**不出用户级诊断**
     * （迟到事件是分派时序的正常形态——§6.2 迟到事件过滤同型；事务未
     * 进入即无"刷新失败"语义）。
     *
     * 事实校验：appliedRevision/modelIdentity 任一为全零保留值＝转发方
     * 契约违约 → std::invalid_argument fail-fast（对账基准残缺的对账
     * 是伪验证，禁吞）。
     *
     * @param facts [in] 事件事实（含对账②①基准）
     * @return 事务结果（applied=false 时 failureToken 为封闭词表值）
     */
    PresentationRefreshOutcome
    handlePresentationEvent(const PresentationEventFacts& facts);

    // ---- 当前呈现查询（失效语义——旧呈现身份不可冒充）----

    /**
     * @brief 当前呈现投影（最近一次成功原子替换的呈现；nullopt＝会话
     *        尚无成功呈现或已拆绑——查询不虚构）。
     *
     * 返回值拷贝（hostPayload shared_ptr 共享——持返回值即持呈现视图
     * 存活期）。修订切换成功后本查询只返回新呈现——旧 presentation
     * Identity 无任何桥面可再取得（"旧呈现身份失效"的查询半区）。
     */
    std::optional<PresentationViewProjection> currentPresentation() const;

    /// @brief 当前绑定会话（nullopt＝未绑定）。
    std::optional<core::ProjectId> boundSession() const;

    // ---- 观察者登记（UI-T21+ 接缝）----

    /**
     * @brief 登记刷新结果观察者（弱持有——观察者析构自动退订）。
     *
     * @param observer [in] 观察者（shared_ptr 由调用方持有保活；空指针
     *                 ＝调用方违约，抛 std::invalid_argument）
     */
    void addObserver(std::weak_ptr<IUiPresentationRefreshObserver> observer);

private:
    /**
     * @brief 刷新失败路径（acceptance 3——诊断双通道＋观察者通知）。
     *
     * @param facts       [in] 触发事件（诊断 context/通知入参）
     * @param reasonToken [in] 桥侧失败词表 token
     * @param causeDetail [in] 诊断 cause 素材（L5 token/明细透传或桥侧
     *                    对账失配定位——不吞错不改义）
     * @return 事务失败结果（applied=false＋token——供入口直接返回）
     */
    PresentationRefreshOutcome
    failRefresh(const PresentationEventFacts& facts, const std::string& reasonToken,
                const std::string& causeDetail);

    /// @brief Dev 留痕（允许为空＝显式声明的无日志场景——UI-T11 同款）。
    void emitDevLine(const std::string& message) const;

    /// @brief UI-PRESENTATION-REFRESH-FAILED 双通道出线（目录 Error＋Dev
    ///        明文；空目录/空日志场景显式声明跳过——不虚构条目）。
    void emitRefreshFailedDiagnostic(const PresentationEventFacts& facts,
                                     const std::string& reasonToken,
                                     const std::string& causeDetail) const;

    /// @brief 通知全部存活观察者的替换事件（过期 weak_ptr 顺带剪除）。
    void notifyReplaced(const PresentationViewProjection& view);

    /// @brief 通知全部存活观察者的失败事件（过期 weak_ptr 顺带剪除）。
    void notifyFailed(const PresentationEventFacts& facts,
                      const std::string& reasonToken);

    /// 装配依赖（source/outlet 构造期校验非空；诊断三件可空）。
    Deps m_deps;
    /// 当前绑定会话（nullopt＝未绑定——attach/switch/detach 唯一写点）。
    std::optional<core::ProjectId> m_bound;
    /// 当前呈现（nullopt＝会话尚无成功呈现/已拆绑——事务④唯一写点）。
    std::optional<PresentationViewProjection> m_current;
    /// 刷新结果观察者（弱持有——notify 时剪除过期项）。
    std::vector<std::weak_ptr<IUiPresentationRefreshObserver>> m_observers;
};

}  // namespace ui
}  // namespace ird
}  // namespace sdurws

#endif  // SDURWS_IRD_UI_RUNTIMEPUBLISHBRIDGE_HPP
