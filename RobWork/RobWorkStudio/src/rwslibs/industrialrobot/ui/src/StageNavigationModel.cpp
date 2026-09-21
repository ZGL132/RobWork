/**
 * @file   StageNavigationModel.cpp
 * @brief  阶段导航投影的实现——七阶段 StageView 合成、§6.4 导航时序、
 *         §6.5 StageStatusModel 汇聚与观察者分发。
 *
 * 设计依据：
 *   - units/ui.md §10.2（接口契约表：前置/后置/错误类型/线程/生命周期/
 *     副作用/合法与非法用法——"在 workflow 门控之外自行'解锁'阶段＝第二
 *     状态机（N-11）"是本实现的首要禁令）、§6.4（阶段呈现状态表＋阶段
 *     切换时序图）、§6.5（汇聚红线："只汇聚……不计算门控、不判定就绪"）；
 *   - §6.3（七态求值唯一实现点 evaluateStatusWord——本实现只按阶段取
 *     StatusFacts 并调用它，不复制任何求值规则）；
 *   - 需求 UX-12（七阶段导航）、UX-01（下一步建议——透传不生成）、
 *     NFR-COR-02（快照域序稳定＝注册序）；
 *   - 任务契约 UI-T09.json acceptance 1/2/3（UI-STG-1/2、单侧冻结形状、
 *     投影不拥有门控规则、P-UI-6 桩门控口径）。
 *
 * 背景说明（实现策略——为什么无锁且无快照缓存）：除 readinessSnapshot
 * 外的方法全部约定 UI 线程调用（§3.4 M-1），观察者清单/currentStage 等
 * 可变状态只在 UI 线程触碰，天然串行无需锁；readinessSnapshot 被 workflow
 * 在任意线程拉取（§10.2 线程行），其实现**不读任何模型可变状态**——
 * 每次调用现取域源投影＋现取纪元值拼装新快照返回（值拷贝语义），因此
 * 与 UI 线程方法并发安全无需互斥（域源实现的线程契约归其自身，见
 * IUiDomainReadinessSource 注释）。缓存快照反而制造陈旧数据与锁需求，
 * 与 §6.1"投影最终一致、读取方只见完整快照"的简单化取向相悖。
 *
 * 线程安全：UI 线程方法串行假设（M-1）；readinessSnapshot 并发安全
 * （不触碰共享可变状态）。
 */

#include <sdurws/ird/ui/IStageNavigationModel.hpp>

#include <algorithm>
#include <deque>
#include <stdexcept>
#include <utility>

#include <sdurws/ird/ui/UiProjections.hpp>  // evaluateStatusWord/statusWordToken（七态唯一求值实现点与冻结 token 表）

namespace sdurws {
namespace ird {
namespace ui {

namespace {

// ---------------------------------------------------------------------
// 七阶段固定顺序（§6.4 UX-12 序——编译期固定表）
// ---------------------------------------------------------------------

/// 固定顺序表（枚举序＝呈现序——§6.4 七阶段原文；stageIdSequence 返回它）。
const std::vector<StageId>& stageOrderTable()
{
    static const std::vector<StageId> kOrder{
        StageId::Modeling,       StageId::Requirements, StageId::Kinematics,
        StageId::TrajectoryDynamics, StageId::Selection,  StageId::Optimization,
        StageId::Reporting,
    };
    return kOrder;
}

// ---------------------------------------------------------------------
// 订阅句柄（§10.2"观察者弱引用，退订幂等"的 RAII 载体）
// ---------------------------------------------------------------------

/// 前置声明（句柄实现引用模型私有登记面——见下方友元/公有注册方法）。
class StageNavigationModel;

/**
 * @brief 观察者登记记录（UI 线程专属——登记/移除/通知全部 UI 线程执行）。
 */
struct ObserverRecord
{
    IStageViewObserver* observer = nullptr;  ///< 弱引用——观察者自管生命期（§10.2）
    bool removed = false;                    ///< 幂等退订标记（重复 unsubscribe 不二次移除）
};

/**
 * @brief 订阅句柄实现（core::IEventSubscription 契约形态——析构＝退订）。
 *
 * 句柄持有模型指针与自身登记记录指针：unsubscribe 把记录标记 removed 并
 * 从模型清单摘除；幂等性由 removed 位保证（重复调用直接返回）。模型先于
 * 全部存活句柄析构是装配契约（句柄由观察者/壳持有，模型归壳——壳析构
 * 顺序必须先清句柄；模型析构时若有未退订句柄属装配违约，句柄后续
 * unsubscribe 经 removed 兜底不崩）。
 */
class StageViewSubscription final : public core::IEventSubscription
{
public:
    StageViewSubscription(StageNavigationModel* model, ObserverRecord* record)
        : model_(model), record_(record) {}

    /// @brief RAII 语义（§10.2"订阅句柄 RAII"）：析构＝退订。
    ~StageViewSubscription() override { unsubscribe(); }

    /// @brief 退订（幂等；析构自动调用）。退订后观察者不再收到通知。
    void unsubscribe() override;

private:
    StageNavigationModel* model_;  ///< 借用——模型归壳（不接管所有权）
    ObserverRecord* record_;       ///< 登记记录（模型持有清单的元素）
};

// ---------------------------------------------------------------------
// 默认实现：StageNavigationModel
// ---------------------------------------------------------------------

/**
 * @brief IStageNavigationModel 默认实现（工厂 createStageNavigationModel
 *        的返回体——无 GUI 依赖，模型层可直接断言）。
 */
class StageNavigationModel final : public IStageNavigationModel
{
public:
    /// 构造：装配契约自检（必注入项缺失 fail-fast——见工厂契约注释）。
    explicit StageNavigationModel(StageNavigationModelDeps deps)
        : deps_(std::move(deps)), current_(deps_.initialStage)
    {
        // 装配契约（fail-fast）：五项必注入缺一不可——缺门控会把"无判定"
        // 伪装成"全部放行"（N-11 红线），缺会话源会把会话态伪装成可写
        // 可导航。宁可装配期崩溃，不带病上线（AGENTS §3 错误语义）。
        if (deps_.gate == nullptr) {
            throw std::invalid_argument(
                "ui/stagenav/deps: 未注入阶段门控端口 IUiStageGate"
                "（WP-22-T03 未产出前须注入桩）");
        }
        if (!deps_.epochSource || !deps_.writableSource
            || !deps_.sessionNavigableSource || !deps_.sevenStateFacts) {
            throw std::invalid_argument(
                "ui/stagenav/deps: 会话事实源缺失（epochSource/writableSource/"
                "sessionNavigableSource/sevenStateFacts 必须全部注入）");
        }
        // 域源指针清单中不得混入空指针（注册面粗心——同样装配期拦截）。
        for (const auto* source : deps_.domainSources) {
            if (source == nullptr) {
                throw std::invalid_argument(
                    "ui/stagenav/deps: domainSources 含空指针（装配契约违约）");
            }
        }
    }

    // ---- IStageNavigationModel（UI 线程面）---------------------------

    std::vector<StageView> stageViews() const override
    {
        // 每次调用现合成（快照语义——消费方不得跨快照拼接字段）。
        const std::uint64_t epoch = deps_.epochSource();
        const bool writable = deps_.writableSource();
        std::vector<StageView> views;
        views.reserve(stageOrderTable().size());
        for (const StageId stage : stageOrderTable()) {
            views.push_back(composeStageView(stage, epoch, writable));
        }
        return views;
    }

    StageId currentStage() const override { return current_; }

    NavigateResult requestNavigate(StageId stage) override
    {
        // ---- ① 前置自检（§10.2 前置条件：会话非 Opening/Closed）--------
        // 越界枚举同样属调用方违约：七阶段封闭词表之外没有合法目标。
        if (!isKnownStage(stage)) {
            throw std::invalid_argument(
                "ui/stagenav/stage: requestNavigate 目标非七阶段词表值");
        }
        if (!deps_.sessionNavigableSource()) {
            // Opening/Closed 会话没有导航语义（§5.2 会话态机）——调用方
            // 契约违约 fail-fast（伪装成 Rejected 会掩盖时序缺陷，且
            // Rejected 的就地提示语义是给用户的，不是给调用方的）。
            throw std::logic_error(
                "ui/stagenav/session: requestNavigate 要求会话非 Opening/Closed");
        }

        // ---- ② 只读会话：查看路径，不经门控（§6.4 补充规则）------------
        // "阶段导航全部可点击查看（历史结果/对象）"——可执行性由命令层
        // 禁用承担（§5.5），导航不整体锁死；门控判定的是"进入执行"的
        // 准入，对只读浏览无语义。RejectedReadOnly 仍切换 currentStage
        // （消费方按 viewOnlyStage 装配只读面板）并分发事件（见
        // NavigateResult 注释的三态后置条件）。
        if (!deps_.writableSource()) {
            switchTo(stage);
            NavigateResult out;
            out.kind = NavigateResult::Kind::RejectedReadOnly;
            out.stage = stage;
            return out;
        }

        // ---- ③ 幂等：目标==当前（状态未变——不评估不发事件）------------
        if (stage == current_) {
            NavigateResult out;
            out.kind = NavigateResult::Kind::Allowed;
            out.stage = stage;
            return out;
        }

        // ---- ④ 汇聚快照→门控评估（§6.4 时序"navigate(stage)→门控评估"）--
        // 快照现取现传（epoch 标注构建时刻）——门控从快照读域就绪事实，
        // 这是 §6.4 时序图"门控评估（消费 ui 的 StageStatusModel 汇聚投影）"
        // 的数据流本体；ui 对判定结果零加工（acceptance 2：投影只消费、
        // 不拥有门控规则）。
        const StageGateDecision decision = deps_.gate->evaluate(readinessSnapshot(stage));
        if (decision.allowed) {
            // ---- ⑤a 允许：切换会话态＋投影变更事件（§6.4 时序"允许"支）--
            // currentStage=stage（会话态）；面板装配/导航条刷新/左栏列表
            // 切换由观察者（壳层）经事件驱动完成——模型只发身份信号，
            // 不直接操纵 GUI（§6.1 pull-on-event）。
            switchTo(stage);
            NavigateResult out;
            out.kind = NavigateResult::Kind::Allowed;
            out.stage = stage;
            return out;
        }
        // ---- ⑤b 拒绝：currentStage 不变、无事件（§6.4 时序"拒绝"支）----
        // 就地提示三要素（原因＋缺项＋下一步建议）随结果值走——呈现层
        // 就地渲染，不弹模态（§6.4 时序图注）。
        NavigateResult out;
        out.kind = NavigateResult::Kind::Rejected;
        out.stage = stage;
        out.reasonKeys = decision.reasonKeys;
        out.unlockHintKey = decision.unlockHintKey;
        return out;
    }

    std::unique_ptr<core::IEventSubscription>
    subscribe(IStageViewObserver& observer) override
    {
        // 登记弱引用（观察者自管生命期——§10.2"观察者弱引用"）；登记与
        // 通知同在 UI 线程（M-1），无并发登记面。登记容器为 deque：尾部
        // 追加不失效元素引用——句柄持有的 ObserverRecord 指针因此保持
        // 有效（vector 增长会重排元素使句柄悬垂，禁用）。
        records_.push_back(ObserverRecord{&observer, false});
        return std::unique_ptr<core::IEventSubscription>(
            new StageViewSubscription(this, &records_.back()));
    }

    // ---- IStageNavigationModel（任意线程面）--------------------------

    StageReadinessSnapshot readinessSnapshot(StageId stage) const override
    {
        // §6.5 汇聚红线：只汇聚、不计算门控、不判定就绪——域项按注册序
        // 拼接（NFR-COR-02 稳定序），空域源不计入。本方法不读任何模型
        // 可变状态（current_/records_/deps_ 只读段均不可变），与 UI 线程
        // 方法并发安全无需互斥（文件头注释）。
        StageReadinessSnapshot snapshot;
        snapshot.stage = stage;
        snapshot.epoch = deps_.epochSource();
        for (const auto* source : deps_.domainSources) {
            std::vector<DomainReadinessItem> items = source->domainReadiness(stage);
            // 逐项搬运（拼接而非移动语义——域源返回的是临时清单，直接
            // 追加到快照尾；stable 顺序＝注册序内嵌于遍历序）。
            snapshot.domains.insert(snapshot.domains.end(),
                                    std::make_move_iterator(items.begin()),
                                    std::make_move_iterator(items.end()));
        }
        return snapshot;
    }

    // ---- 退订与通知（StageViewSubscription 协作面）-------------------

    /// @brief 摘除登记记录（幂等——removed 位；仅 UI 线程调用）。
    void removeRecord(ObserverRecord* record)
    {
        if (record->removed) {
            return;  // 重复退订幂等（§10.2 契约表）
        }
        record->removed = true;
        const auto it = std::find_if(records_.begin(), records_.end(),
                                     [record](const ObserverRecord& r) {
                                         return &r == record;
                                     });
        if (it != records_.end()) {
            records_.erase(it);  // 向量元素地址失效——句柄此后只依赖 removed 兜底
        }
    }

private:
    /// @brief 合成单阶段呈现视图（§6.4 表数据源的落地——见 StageView 注释）。
    StageView composeStageView(StageId stage, std::uint64_t epoch,
                               bool writable) const
    {
        StageView view;
        view.stage = stage;
        view.epoch = epoch;

        // 第 1 步：门控呈现输出（数据源权威——§6.4 表"workflow 门控输出"）。
        const StageGateView gateView = deps_.gate->presentStage(stage);

        // 防御兜底：门控适配器若违约输出 in-progress/view-only（非门控
        // 四态——StageGateView 契约注释），降级为 not-started 呈现并保留
        // 其键（不虚构 completed/unavailable 语义；不抛——呈现路径的
        // 容错以"保守默认"表达，适配器缺陷经桩测试/验收面追责）。
        StageViewStatus status = gateView.status;
        if (status == StageViewStatus::InProgress
            || status == StageViewStatus::ViewOnly) {
            status = StageViewStatus::NotStarted;
        }

        // 第 2 步：会话态合成（§6.4 表行 2/6——in-progress/view-only 不是
        // 门控输出，按会话事实覆盖）。优先级：view-only（只读会话全阶段
        // ——writable=false 是会话级事实）＞ in-progress（用户所在）＞
        // 门控四态透传。
        if (!writable) {
            view.status = StageViewStatus::ViewOnly;
        } else if (stage == current_) {
            view.status = StageViewStatus::InProgress;
        } else {
            view.status = status;
        }

        // 第 3 步：七态投影（§6.3"七态按评估域/阶段分别投影"——每阶段
        // 取触发数据源快照，经唯一求值实现点 evaluateStatusWord 求值，
        // 输出冻结 token）。无数据源的阶段输出 nullopt（呈现占位，
        // 不虚构状态词）。
        const std::optional<StatusFacts> facts = deps_.sevenStateFacts(stage);
        if (facts.has_value()) {
            view.sevenState = statusWordToken(evaluateStatusWord(*facts).word);
        }

        // 第 4 步：门控键透传（原因/缺项/下一步建议——投影不加工：
        // 门控给什么呈现什么；blocked 的"附原因＋下一步建议"由数据自身
        // 携带，不因合成状态被裁剪——UX-01 建议文本权威在 workflow）。
        view.blockingReasonKeys = gateView.blockingReasonKeys;
        view.nextStepKey = gateView.nextStepKey;
        return view;
    }

    /// @brief 执行切换（更新会话态＋分发投影变更事件——pull-on-event）。
    void switchTo(StageId stage)
    {
        current_ = stage;
        // 通知纪律（回调内退订安全的实现方式）：先取登记记录指针快照，
        // 逐个回调前**重查该记录仍在登记清单**——回调内若发生退订（乃至
        // 观察者随句柄析构），已摘除/已失效的记录被跳过；记录本体由
        // deque 持有，未摘除的记录地址恒稳定（见 subscribe 注释）。
        std::vector<ObserverRecord*> snapshot;
        snapshot.reserve(records_.size());
        for (auto& record : records_) {
            snapshot.push_back(&record);
        }
        for (ObserverRecord* record : snapshot) {
            const bool stillRegistered
                = std::find_if(records_.begin(), records_.end(),
                               [record](const ObserverRecord& r) {
                                   return &r == record;
                               }) != records_.end();
            if (!stillRegistered) {
                continue;  // 回调内已退订（观察者可能已析构——不得触达）
            }
            record->observer->onStageViewsChanged(*this);
        }
    }

    /// @brief 七阶段封闭词表成员判定（越界枚举防御——requestNavigate 用）。
    static bool isKnownStage(StageId stage)
    {
        const auto& order = stageOrderTable();
        return std::find(order.begin(), order.end(), stage) != order.end();
    }

    StageNavigationModelDeps deps_;  ///< 装配依赖（端口/会话源借用——装配方持有）
    StageId current_;                ///< 当前激活阶段（会话态——仅 UI 线程读写）
    /// 观察者登记（仅 UI 线程触碰；deque 保证尾部追加不失效元素引用——
    /// 订阅句柄长期持有记录指针，见 subscribe 注释）。
    std::deque<ObserverRecord> records_;
};

void StageViewSubscription::unsubscribe()
{
    // 幂等兜底：模型可能已析构（装配契约要求壳先清句柄——违约场景下
    // record_ 指针随之失效，此处无法安全兜底；removed 位覆盖的是"模型
    // 存活、重复退订"的正常幂等路径，§10.2 契约表）。
    if (model_ != nullptr && record_ != nullptr) {
        model_->removeRecord(record_);
    }
    model_ = nullptr;
    record_ = nullptr;
}

}  // namespace

// =====================================================================
// 工厂与自由函数（公共面）
// =====================================================================

const std::vector<StageId>& stageIdSequence()
{
    // §6.4 固定顺序的公共只读出口（测试/壳/门控桩共用一个序权威）。
    return stageOrderTable();
}

const char* stageToken(StageId stage) noexcept
{
    // 七值封闭词表逐字对应（§6.4 括注名——"轨迹/动力学"的键段为
    // "trajectory-dynamics"，分隔符以连字符形态进键，§3.5 键词形）。
    // 唯一映射点（NFR-MNT-03）——文案键 stage.<token>.title 的 token 段
    // 一律经此构造，调用方禁止手写 token 字面量（两处手写必漂移）。
    switch (stage) {
        case StageId::Modeling:           return "modeling";
        case StageId::Requirements:       return "requirements";
        case StageId::Kinematics:         return "kinematics";
        case StageId::TrajectoryDynamics: return "trajectory-dynamics";
        case StageId::Selection:          return "selection";
        case StageId::Optimization:       return "optimization";
        case StageId::Reporting:          return "reporting";
    }
    return "";  // 不可达（非法枚举值防御——空串便于日志显性暴露）
}

std::unique_ptr<IStageNavigationModel>
createStageNavigationModel(StageNavigationModelDeps deps)
{
    // 构造期装配自检在 StageNavigationModel 构造函数（fail-fast 清单见彼处）。
    return std::unique_ptr<IStageNavigationModel>(new StageNavigationModel(std::move(deps)));
}

}  // namespace ui
}  // namespace ird
}  // namespace sdurws
