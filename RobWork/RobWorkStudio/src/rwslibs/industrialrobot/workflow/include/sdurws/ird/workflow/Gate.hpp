/**
 * @file   Gate.hpp
 * @brief  七阶段门控模型（O1 门控规则＋O2 级联失效提示）——门控输入板、
 *         GateDecision/StageGatingState 数据形状、级联提示与门控服务接口。
 *
 * 设计依据：
 *   - units/workflow.md §4（七阶段门控模型——D-WF-1 四段职责链、I-WF-1~4
 *     门控规则要点、§4.3 门控状态词表与级联失效提示、§4.4 门控状态机）、
 *     §5（状态投影消费与事件协作——§5.1 投影消费纪律、§5.2 事件消费面、
 *     §5.3 刷新时序）、§10.2（接口签名设计基线 IStageGateService——本头
 *     按实现口径增补输入形状，偏差已登记单元卡 §14.5）、§11.1（验证用例
 *     WF-VER-101~107/112）
 *   - 需求 UX-12（七阶段导航按就绪条件解锁/锁定；上游完成态或结果失效时
 *     级联提示下游需重算或失效——提示数据不自动触发重算）、UX-10（门控
 *     状态词与 ui StageViewStatus 词表对齐，不发明第八种状态）
 *   - ui.md §6.4/§6.5（P-WF-2 谈判起点：StageReadinessSnapshot/
 *     DomainReadinessItem 单侧冻结形状、StageViewStatus 六值词表——经
 *     UiTypes.hpp 消费）、core.md §4.9（DomainEventKind 事件词表——
 *     事件不携带数据 D-09）
 *   - 任务契约 tasks/foundation/WP-22-T03.json acceptance 1/2/3
 *
 * 背景说明（门控在数据流中的位置——D-WF-1 四段职责链）：各业务域**自报**
 * 就绪（各域计算自身 readiness）→ ui StageStatusModel **汇聚**（不判定）
 * → workflow **判定门控**（消费快照＋事件，不重算领域就绪）→ ui **呈现**。
 * 本单元拥有第三段的判定规则：阶段 N 解锁要求前序链通过（I-WF-1），本域
 * 就绪与否完全采信 DomainReadinessItem 投影（I-WF-2 不重算），门控不含
 * "多少算合格"的数值判定（I-WF-3 零领域阈值），判定绑定快照 epoch（I-WF-4
 * 旧投影不覆盖新事件）。阶段状态投影的**呈现**归 ui（六态合成中的
 * view-only/in-progress 会话面）——本单元不双权威（DTB 禁止项）。
 *
 * 确定性（D-WF-3 纯函数面）：evaluate/mapInvalidation 全部入参 const 引用、
 * 无共享可变状态——同（快照板, 事件窗口）必同输出（NFR-COR-02 同型）；
 * 事件仅作判定输入，不携带任何触发副作用（重算是用户动作，§4.3）。
 *
 * 线程安全：全部类型为纯值；PureStageGateService 无成员状态，const 方法
 * 可并发调用（§10.3 接口属性表"生命周期"行）。
 */

#ifndef SDURWS_IRD_WORKFLOW_GATE_HPP
#define SDURWS_IRD_WORKFLOW_GATE_HPP

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <sdurws/ird/core/Events.hpp>          // core::DomainEventKind（GateEvent.kind 复用 core 事件词表——零新增枚举）
#include <sdurws/ird/evidence/Currentness.hpp> // evidence::InvalidationReason（失效原因结构化词表——§4.3"原因 token 来自 evidence 词表"）
#include <sdurws/ird/ui/UiTypes.hpp>           // ui::StageId/StageViewStatus/StageReadinessSnapshot/TextKey（P-WF-2 谈判起点形状）
#include <sdurws/ird/workflow/Types.hpp>       // WorkflowError/域↔阶段映射/文案键构造

namespace sdurws {
namespace ird {
namespace workflow {

// =====================================================================
// 门控输入（acceptance 2 唯二输入面：StageReadinessSnapshot＋事件）
// =====================================================================

/**
 * @brief 门控事件的最小身份载体（调用方从⑤平台事件端口预整理）。
 *
 * 为什么不是 core::DomainEvent 原样：core 事件**不携带数据**（core.md
 * D-09——防 DTO 膨胀，消费者经端口取数），DependencyInvalidated 载荷只有
 * project/branch/revision 身份，不指明受影响域；而失效范围判定归 evidence
 * （按切片计算，§4.3）——workflow 只消费其判定结果。因此调用方（L5 适配
 * 层）把"事件身份＋evidence 给出的关联域键＋原因"折叠为本值对象，门控
 * 重放事件窗口时即可确定性推导各阶段的过期事实。
 *
 * kind 复用 core::DomainEventKind 四类词表（零新增枚举——D-WF-4）：仅
 * ResultArchived（结果归档——本阶段"有结果"事实）与 DependencyInvalidated
 * （依赖失效——"结果过期"事实）参与门控重放；RevisionCommitted/
 * TaskStatusChanged 对门控的可见效果已反映在下一次汇聚快照（hasActiveTask/
 * inputComplete 由域自报更新），无需事件重放（§5.2 处置表）。
 */
struct GateEvent {
    /// 事件类别（core 词表；域键与原因的关联语义见上）。
    core::DomainEventKind kind = core::DomainEventKind::ResultArchived;
    /// 事件关联域键清单（§4.2 域注册词表：ResultArchived＝产出结果的域；
    /// DependencyInvalidated＝evidence 失效范围判定给出的受影响域——
    /// workflow 经 stageForDomain 做域→阶段导航映射，不重算范围）。
    std::vector<std::string> domainKeys;
    /// 逐条失效原因（evidence §8.1 步骤 4 词表值——仅 DependencyInvalidated
    /// 携带；进 StaleHint.reasons 供呈现层解析"旧→新差异"，门控不加工作）。
    std::vector<evidence::InvalidationReason> reasons;
};

/**
 * @brief 门控判定的完整输入板（七阶段快照＋会话事件窗口＋epoch 水位）。
 *
 * 为什么是"板"而不是单快照：ui 汇聚出口 IStageNavigationModel::
 * readinessSnapshot(stage) 按阶段取数（§6.5 每阶段一份），而门控状态机
 * 是序贯的（前序链 I-WF-1——阶段 N 的判定依赖阶段 1..N-1 的判定结果），
 * 必须把七阶段快照按 StageId 序组板一次交付。本形状是对 §10.2 设计基线
 * evaluate(snapshot) 单参签名的实现口径增补（偏差与理由已登记单元卡
 * §14.5 v0.4——单快照无法表达前序链，七板聚合是"汇聚投影"的完整形态）。
 *
 * 事件窗口：会话内按发布序（core 事件契约 FIFO）排列的 GateEvent 清单；
 * 批量事件风暴由调用方合并去抖（同一 epoch 只判一次，§5.3 不阻塞规则），
 * 门控自身对窗口做幂等重放。
 */
struct GateInputs {
    /// 七阶段汇聚快照（下标 i 对应 stageIdSequence()[i]——stage 字段必须
    /// 与序位一致，evaluate 校验；epoch 为各快照构建时刻的会话纪元）。
    std::array<ui::StageReadinessSnapshot, 7> snapshots;
    /// 会话事件窗口（发布序；可空——无事件时门控只按投影判定）。
    std::vector<GateEvent> events;
    /// 上次判定接受的 epoch 水位（I-WF-4：任一快照 epoch 低于本水位即
    /// 拒绝判定——旧投影不覆盖新事件；0＝无水位，首次判定）。
    std::uint64_t lastEpoch = 0;
};

// =====================================================================
// 级联失效提示（O2——提示数据，不自动触发重算）
// =====================================================================

/**
 * @brief 单条级联失效提示（§4.3 级联映射产出——"需重算对象定位＋原因＋
 *        建议动作"三要素）。
 *
 * 红线：本结构是**提示数据**，不是任务指令——产出方（mapInvalidation/
 * evaluate）不派发任何重算任务，重算由用户经"重新计算"命令入口发起
 * （§4.3"计算是用户动作"）。消费者（ui）据此渲染过期徽标与提示横幅。
 */
struct StaleHint {
    /// 受影响阶段（stageForDomain 映射结果——§4.2 域→阶段导航映射）。
    ui::StageId stage = ui::StageId::Modeling;
    /// 受影响域注册键（提示的对象定位半区；对象名经⑥名称端口呈现局部名
    /// ——UX-02：不显示哈希/内部标识）。
    std::string domainKey;
    /// 失效原因清单（evidence §8.1 步骤 4 词表值透传——含涉事依赖键与
    /// 旧→新差异摘要；本单元零加工，D-WF-2 不复制失效计算）。
    std::vector<evidence::InvalidationReason> reasons;
    /// 建议动作 token（§6.3 R4——恒为 kActionRecomputeDownstream：提示
    /// 用户"复算下游"，可跳过、不自动执行，D-WF-8）。
    std::string actionKey = kActionRecomputeDownstream;

    /// 值相等（确定性重放与提示比对面——reasons 复用 evidence 值相等）。
    bool operator==(const StaleHint& o) const
    {
        return stage == o.stage && domainKey == o.domainKey
            && reasons == o.reasons && actionKey == o.actionKey;
    }
    bool operator!=(const StaleHint& o) const { return !(*this == o); }
};

/**
 * @brief 失效信号（mapInvalidation 的当前性投影输入——§4.3"失效事件＋
 *        当前性投影"的后半区）。
 *
 * 调用方从 evidence 当前性投影（CurrentnessIndex 的 Superseded 记录）与
 * 失效范围判定取数组装：受影响域键＋逐条原因。本单元不解析
 * evidence::CurrentnessResult（那是 evidence 的判定出口——workflow 只做
 * 域→阶段导航映射，§9 协作表 evidence 行）。
 */
struct InvalidationSignal {
    /// 受影响域键清单（§4.2 域注册词表；evidence 失效范围判定结果）。
    std::vector<std::string> domainKeys;
    /// 失效原因清单（evidence 词表值透传——进 StaleHint.reasons）。
    std::vector<evidence::InvalidationReason> reasons;
};

// =====================================================================
// 门控判定产出（P-WF-2 谈判起点形状——GateDecision/StageGatingState）
// =====================================================================

/**
 * @brief 单阶段门控判定（§4.3 GateDecision 字段清单＋unlocked 便利位）。
 *
 * status 词表＝ui::StageViewStatus 六值（零新增枚举——D-WF-4）；本单元
 * evaluate 产出其中五值（Completed/Blocked/Unavailable/NotStarted/
 * InProgress），ViewOnly 不产（只读是 writable 会话事实，门控无此输入——
 * 由 ui 按会话合成覆盖，ui §6.4 表 view-only 行数据源＝writable）。
 *
 * P-WF-2 张力登记（诚实记录）：ui 呈现端口 IUiStageGate::presentStage 的
 * 契约注记"门控适配器输出 in-progress 属实现违约"（ui 侧把 in-progress
 * 词保留给"用户所在阶段"会话态）；本单元按 workflow.md §4.3/§4.4 权威
 * 口径产出 InProgress（hasActiveTask 计算中——门控状态机一态）。两者的
 * 映射归 P-WF-2 裁决后的 L5 适配器处理，本单元不预判呈现折叠规则。
 *
 * unlocked 字段（§14.5 v0.4 增补登记）：UX-12 的"解锁/锁定入口"判定位
 * ——status 之外的可导航语义（NotStarted 有两义：前序未过的锁定态与
 * "可进入但评估未开始"的解锁态，二者 status 同词；unlocked 把两者区分，
 * 供 L5 适配器直读组装 IUiStageGate 的 allowed，避免适配器从 reasonKeys
 * 反推门控语义）。词表零新增不受影响（bool 位，非枚举）。
 */
struct GateDecision {
    /// 所属阶段（ui 七值词表）。
    ui::StageId stage = ui::StageId::Modeling;
    /// 门控状态（ui::StageViewStatus 六值承载——§4.3 词表；见类型注释的
    /// 产出五值与 P-WF-2 张力登记）。
    ui::StageViewStatus status = ui::StageViewStatus::NotStarted;
    /// 本阶段是否解锁（UX-12"解锁/锁定入口"判定位：true＝前序链通过且
    /// 本域投影就绪——可进入；false＝前序未过/缺项/域未装配——锁定）。
    bool unlocked = false;
    /// 原因文案键清单（Blocked＝缺项键＋阻塞键；NotStarted 前序未过＝
    /// 前序 not-reached 键；Unavailable＝逐域 missing-domain 键；其余态
    /// 为空——键构造唯一经 Types.hpp 映射点，UX-02）。
    std::vector<ui::TextKey> reasonKeys;
    /// 解锁条件文案键（锁定态的"如何解锁"提示——§6.4 时序解锁半区；
    /// nullopt＝已解锁无提示）。
    std::optional<ui::TextKey> unlockHintKey;
    /// 缺项文案键清单（**透传**本阶段各域自报 missingItemKeys——不重算
    /// 不增删，按快照域序拼接＝分组语义，I-WF-2/WF-VER-102）。
    std::vector<ui::TextKey> missingItemKeys;
    /// 级联失效提示（本阶段结果过期时非空——§4.3"Completed＋results-stale
    /// 并存呈现"的提示数据；reasons 自事件窗口透传）。
    std::vector<StaleHint> staleHints;
    /// 判定所依据的会话纪元（＝七快照 epoch 的最大值——I-WF-4 对齐面；
    /// 单调 uint64，无单位）。
    std::uint64_t epoch = 0;

    /// 值相等（全字段——确定性重放 WF-VER-107 的比较面）。
    bool operator==(const GateDecision& o) const;
    bool operator!=(const GateDecision& o) const { return !(*this == o); }
};

/**
 * @brief 七阶段门控状态（一次 evaluate 的完整产出——按 StageId 序）。
 *
 * 呈现消费（归 ui，不双权威）：ui IStageNavigationModel 的 stageViews 把
 * 本状态与七态投影/会话事实按 §6.4 表合成为 StageView——本状态是"数据源"，
 * 不是呈现态本身（§4.1 数据流契约第四段）。
 */
struct StageGatingState {
    /// 七阶段判定（下标 i 对应 stageIdSequence()[i]——ui §6.4 UX-12 冻结序）。
    std::array<GateDecision, 7> decisions;
    /// 本轮判定纪元（max 快照 epoch——I-WF-4；调用方回填 lastEpoch 水位）。
    std::uint64_t epoch = 0;

    /// 值相等（全字段）。
    bool operator==(const StageGatingState& o) const { return decisions == o.decisions && epoch == o.epoch; }
    bool operator!=(const StageGatingState& o) const { return !(*this == o); }
};

// =====================================================================
// 门控服务接口（§10.2 设计基线 IStageGateService——实现口径见各方法注）
// =====================================================================

/**
 * @brief 七阶段门控服务（门控规则唯一权威——O1/O2；ARCH §3.4）。
 *
 * 实现纪律（acceptance 2 运行断言锁定面）：
 *   - 只消费 GateInputs（StageReadinessSnapshot 板＋事件窗口）——不调用
 *     任何领域服务、不读领域对象、不推导工程含义（I-WF-2/R-1）；
 *   - 零领域阈值——实现无任何"多少算合格"的数值判定（I-WF-3）；
 *   - 纯函数面——无成员可变状态，同输入同输出（D-WF-3）；
 *   - 级联失效只产提示数据——不派发重算任务（UX-12/§4.3）。
 */
class IStageGateService {
public:
    virtual ~IStageGateService() = default;

    /**
     * @brief 按汇聚投影板计算全七阶段门控状态（纯函数面——§10.2 基线）。
     *
     * 判定序（每阶段，按 StageId 序——§4.4 状态机的纯函数重放形态）：
     *   1 前序链（I-WF-1）：存在 Blocked 前序 → NotStarted（锁定——前序
     *     未过，reasonKeys 指向前序 not-reached；§4.3 词表"前序未过"）；
     *   2 域未装配：聚合域词表（stageDomains）中任一域无投影条目 →
     *     Unavailable（区别于 Blocked——WF-VER-103）；
     *   3 缺项锁定：任一域 inputComplete==false 或 missingItemKeys 非空 →
     *     Blocked（missingItemKeys 透传；§4.2"投影缺项"）；
     *   4 计算中：缺项已清且任一域 hasActiveTask → InProgress（§4.3 词表
     *     "有活动任务"）；
     *   5 结果事实（事件窗口重放——§5.2 ResultArchived/DependencyInvalidated
     *     处置）：有归档结果且未被后续失效 → Completed；有归档结果但已失效
     *     → Completed＋staleHints（"Completed＋results-stale 并存呈现"）；
     *     无归档结果 → NotStarted（解锁可进入、评估未开始——unlocked=true，
     *     下一步由建议引擎 R2 引导）。
     *
     * @param inputs [in] 门控输入板（七快照 stage 字段须与序位一致、epoch
     *               不得低于 lastEpoch——违约抛 WorkflowError，调用方错误
     *               fail-fast）
     * @return 七阶段判定（decisions 按序；epoch＝max 快照 epoch）
     *
     * @throws WorkflowError 快照序位/epoch 水位违约（I-WF-4）
     *
     * @threadSafe const 只读，可并发（§10.3）。
     * @determinism 同（快照板, 事件窗口）同输出（NFR-COR-02 同型）。
     */
    virtual StageGatingState evaluate(const GateInputs& inputs) const = 0;

    /**
     * @brief 级联失效映射：失效事件＋当前性投影 → 受影响阶段提示数据
     *        （O2——§10.2 基线 mapInvalidation 的实现口径增补版）。
     *
     * 与 §10.2 基线签名的偏差（单元卡 §14.5 v0.4 登记）：基线双参
     * (ev, snapshot) 无法表达两件实现必需事实——①受影响域清单与原因清单
     * 是 evidence 的判定结果（事件仅身份，D-09；失效范围判定归 evidence，
     * §4.3），必须由调用方经 InvalidationSignal 传入；②下游级联提示需要
     * 当前门控状态判断"哪些下游阶段有历史结果可失效"（§4.3"下游阶段门控
     * 重算 Completed → in-progress＋results-stale"）。故增补 signal/gates
     * 两参；事件与快照两参保持基线语义。
     *
     * 映射规则（D-WF-2）：
     *   - 受影响域经 stageForDomain 映射为受影响阶段（多对一）——每个受
     *     影响域产一条 StaleHint（定位＝域＋阶段、原因透传、动作
     *     recompute-downstream）；
     *   - 对每个受影响阶段的**下游**（StageId 序更大者）：门控状态为
     *     Completed 的产 StaleHint（下游历史结果因上游失效而过期——
     *     UX-12"级联提示下游需重算"）；Blocked/NotStarted/Unavailable/
     *     InProgress 的下游不产（无历史结果可失效或重算已在途）。
     *
     * @param ev [in] 失效事件（须为 DependencyInvalidated——仅身份；
     *             其他 kind 属调用方契约违约，fail-fast）
     * @param snapshot [in] 任意阶段的当前汇聚快照（epoch 对齐校验用——
     *                 低于 gates.epoch 抛 WorkflowError，旧投影拒绝）
     * @param signal [in] 失效信号（受影响域＋原因——evidence 判定结果）
     * @param gates [in] 当前门控状态（下游级联的 Completed 判定输入）
     * @return 提示数据清单（按阶段序→域键字典序稳定排列——确定性；
     *         signal.domainKeys 为空时返回空）
     *
     * @throws WorkflowError 事件类别/epoch 对齐/域键词表违约（调用方错误）
     *
     * @note 纯函数——不派发任何重算任务、不调用任务端口（UX-12 红线；
     *       重算是用户动作）。失效率 PresentHints 只表述"需要重算"，
     *       不表述"将会重算"。
     * @threadSafe const 只读，可并发。
     * @determinism 同（事件, 快照, 信号, 门控状态）同输出。
     */
    virtual std::vector<StaleHint> mapInvalidation(const core::DomainEvent& ev,
                                                   const ui::StageReadinessSnapshot& snapshot,
                                                   const InvalidationSignal& signal,
                                                   const StageGatingState& gates) const = 0;
};

/**
 * @brief 门控服务默认实现（纯函数面——无状态，D-WF-3）。
 *
 * 实现即 §4.4 状态机的纯函数重放：全部转移从"投影＋事件窗口"推导，
 * 不持有会话记忆（调用方每次传全量输入板——事件风暴由调用方去抖合并，
 * §5.3 不阻塞规则；门控轻量内联门槛 <1 s，NFR-PERF-01）。
 */
class PureStageGateService final : public IStageGateService {
public:
    StageGatingState evaluate(const GateInputs& inputs) const override;
    std::vector<StaleHint> mapInvalidation(const core::DomainEvent& ev,
                                           const ui::StageReadinessSnapshot& snapshot,
                                           const InvalidationSignal& signal,
                                           const StageGatingState& gates) const override;
};

}  // namespace workflow
}  // namespace ird
}  // namespace sdurws

#endif  // SDURWS_IRD_WORKFLOW_GATE_HPP
