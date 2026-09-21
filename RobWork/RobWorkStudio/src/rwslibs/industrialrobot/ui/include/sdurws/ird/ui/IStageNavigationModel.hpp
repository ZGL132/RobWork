/**
 * @file   IStageNavigationModel.hpp
 * @brief  阶段导航投影（IStageNavigationModel）——七阶段呈现状态、导航
 *         门控时序的 ui 侧承载，及供 workflow 消费的 StageStatusModel
 *         汇聚出口（§10.2 接口详设＋§6.4 阶段导航与门控＋§6.5 汇聚投影）。
 *
 * 设计依据：
 *   - units/ui.md §10.2（IStageNavigationModel 接口详设——StageId/
 *     StageViewStatus/StageView/NavigateResult/五方法契约表；本头按
 *     "首消费冻结"机制随 UI-T09 落位，代码块逐条兑现）；
 *   - §6.4（阶段呈现状态表六态与数据源、阶段切换时序图——允许更新
 *     currentStage＋面板切换、拒绝就地提示不弹模态不改 currentStage、
 *     只读模式"可查看不可执行"以命令层禁用体现）；
 *   - §6.5（StageReadinessSnapshot/DomainReadinessItem 单侧冻结形状——
 *     值类型落位 UiTypes.hpp 以避免接口头互 include 成环，见其头注释）；
 *   - §3.4（UI 线程消费点 M-1——投影变更通知 UI 线程分发）；
 *   - 需求 UX-12（七阶段导航）、UX-01（下一步建议呈现——文本由 workflow
 *     提供）、UX-02（工程用语——键经 UiText、对象定位经名称端口）；
 *   - 任务契约 tasks/foundation/UI-T09.json acceptance 1/2/3（UI-STG-1/2、
 *     汇聚投影单侧冻结、投影不拥有门控规则 N-11、O-31/P-UI-1/P-UI-6
 *     处置）。
 *
 * 背景说明（为什么导航是"投影＋会话态"而不是状态机）：六态呈现中只有
 * in-progress 是 ui 自有的会话事实（用户所在阶段），其余五态的数据源全部
 * 在权威方（workflow 门控输出/writable 位）——本模型因此只做三件事：
 * ①把门控输出与七态投影按 §6.4 表合成为 StageView；②按 §6.4 时序图执行
 * requestNavigate（评估→允许改会话态/拒绝原样返回）；③把域就绪投影汇聚
 * 成快照供 workflow 拉取。门控规则、解锁条件、下一步建议文本**全部不在
 * 本单元**（N-11：在 workflow 门控之外自行"解锁"阶段＝第二状态机，非法）。
 * WP-22-T03 门控数据源未产出前，门控经 IUiStageGate 桩注入（契约卡行
 * "未产出→桩"口径；真实数据源接入时按 ui.md v0.4 §10 引导注经自有端口
 * 复核——O-31 处置登记）。
 *
 * 线程模型（§10.2 线程行）：stageViews/currentStage/requestNavigate/
 * subscribe 全部 UI 线程调用（M-1）；readinessSnapshot 供 workflow 在
 * 任意线程拉取（值拷贝——实现不缓存快照、现取现拷贝，不触碰 UI 线程
 * 专属状态，故无需锁）。
 */

#ifndef SDURWS_IRD_UI_ISTAGENAVIGATIONMODEL_HPP
#define SDURWS_IRD_UI_ISTAGENAVIGATIONMODEL_HPP

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <sdurws/ird/core/Events.hpp>       // core::IEventSubscription（订阅句柄契约——§10.2 subscribe 返回类型）
#include <sdurws/ird/ui/UiPorts.hpp>        // IUiStageGate/IUiDomainReadinessSource（C-12 门控与域就绪端口——O-31 注入面）
#include <sdurws/ird/ui/UiProjections.hpp>  // StatusFacts（每阶段七态触发数据源——evaluateStatusWord 的输入）
#include <sdurws/ird/ui/UiTypes.hpp>        // StageId/StageViewStatus/UiStateToken/StageReadinessSnapshot/TextKey（§3.3 公共值类型）

namespace sdurws {
namespace ird {
namespace ui {

class IStageNavigationModel;  // 前置声明——观察者回调签名引用模型本身（pull-on-event）

// =====================================================================
// 七阶段固定顺序（§6.4 UX-12 序——stageViews"七项，固定顺序"的序权威）
// =====================================================================

/**
 * @brief 七阶段固定呈现顺序（§6.4 原文：modeling→requirements→kinematics
 *        →trajectory-dynamics→selection→optimization→reporting）。
 *
 * @return 七元素清单（枚举序＝呈现序——实现返回编译期固定表，调用方可
 *         用于遍历/定位；顺序变更属需求语义变更，必须走单元卡修订）
 */
const std::vector<StageId>& stageIdSequence();

/**
 * @brief 阶段冻结 token（§6.4 括注名的小写连字符形态——"modeling"/…
 *        "trajectory-dynamics"/…"reporting"）。
 *
 * 唯一映射点（NFR-MNT-03）：文案键 stage.<token>.title（§3.5）与呈现
 * 日志的 token 段一律经本函数构造，禁止调用方各自拼写 token 字面量
 * （两处手写必漂移）。@param stage [in] 阶段值（封闭词表——全七值皆有
 * 登记 token；越界值不可构造，防御性返回空串便于日志显性暴露）
 * @return 冻结 token（同 statusWordToken 的词表风格——小写连字符）
 */
const char* stageToken(StageId stage) noexcept;

// =====================================================================
// StageView 与 NavigateResult（§10.2 代码块逐条兑现）
// =====================================================================

/**
 * @brief 单阶段呈现视图（§10.2 StageView 原文形状——六字段逐一冻结）。
 *
 * 合成规则（§6.4 表"数据源"列的落地——呈现合成，不是新判定）：
 *   1. 门控输出（IUiStageGate::presentStage）给出四态之一的呈现态与
 *      原因/缺项/建议键——数据源权威；
 *   2. writable==false（§5.5/§6.4 view-only 行）→ 全阶段 ViewOnly 覆盖
 *      （只读会话"可查看、不可执行"，导航不整体锁死——区别于 unavailable；
 *      用户所在阶段的"在位"语义由中央区面板呈现承担，不依赖本词表）；
 *   3. 可写会话且 stage==currentStage → InProgress（§6.4 in-progress 行
 *      数据源＝currentStage 会话态——用户所在阶段优先呈现"进行中"）；
 *   4. 其余阶段透传门控呈现态。
 * blockingReasonKeys/nextStepKey 在所有状态下都透传门控输出（投影不加工
 * ——门控给什么呈现什么；blocked 行的"附原因＋下一步建议"由数据自身
 * 携带，不因合成状态被裁剪）。
 *
 * 值语义；快照快照——stageViews() 每次返回合成时刻的完整七项，消费方
 * 不得跨快照拼接字段（§6.1 快照一致性纪律同源）。
 */
struct StageView {
    /// 所属阶段（§10.2 原文字段）。
    StageId stage = StageId::Modeling;
    /// 呈现状态（合成规则见类型注释；§10.2 原文字段）。
    StageViewStatus status = StageViewStatus::NotStarted;
    /// 该阶段七态（§6.3"七态按评估域/阶段分别投影"——token 形态经
    /// statusWordToken 取得；nullopt＝该阶段无七态数据源——呈现占位，
    /// 不虚构状态词。§10.2 原文字段 sevenState）。
    std::optional<UiStateToken> sevenState;
    /// 门控/缺项文案键（数据源 workflow/evidence——§10.2 原文注释；呈现
    /// 时经 UiText 解析，UX-02：键进用户可见面前必须过唯一解析出口）。
    std::vector<TextKey> blockingReasonKeys;
    /// 下一步建议文案键（workflow 提供——UX-01；nullopt＝未提供，ui 不
    /// 生成建议文本。§10.2 原文字段 nextStepKey）。
    std::optional<TextKey> nextStepKey;
    /// 构建纪元（§6.2 会话纪元——迟到快照由消费方按 epoch 丢弃）。
    std::uint64_t epoch = 0;
};

/**
 * @brief 导航请求结果（§10.2 原文注释："NavigateResult = Allowed{stage} |
 *        Rejected{reasonKeys, unlockHintKey} | RejectedReadOnly{
 *        viewOnlyStage}"——三选一的值承载）。
 *
 * 三态语义与后置条件（§10.2 契约表"后置条件"行＋§6.4 时序图/补充规则）：
 *   - Allowed：门控放行——currentStage 已更新＋投影变更事件已分发
 *     （中央区面板切换由壳层经事件驱动装配，IPluginUiRegistrar 注册的
 *     面板——本模型只发事件不直接操纵 GUI）；
 *   - Rejected：门控拒绝——currentStage **不变**、无事件；就地提示数据
 *     ＝reasonKeys（原因＋缺项键）＋unlockHintKey（解锁条件/下一步建议），
 *     由呈现层就地渲染（§6.4 时序图注"就地提示（不弹模态）"）；
 *   - RejectedReadOnly：只读会话——§6.4 补充规则"阶段导航全部可点击
 *     查看（历史结果/对象）"，导航照常生效（currentStage 更新＋事件，
 *     消费方按 viewOnlyStage 装配只读面板、执行/应用类命令按 §5.5 禁用）；
 *     此路径**不经门控评估**（门控判定的是"进入执行"的准入，只读浏览
 *     不构成解锁——可执行性由命令层承担，N-11 不受影响）。
 */
struct NavigateResult {
    /// 结果三态（§10.2 variant 注释的枚举承载——显式返回无异常，§10.2
    /// 契约表"错误类型"行）。
    enum class Kind : std::uint8_t {
        Allowed,          ///< 门控放行（已切换——currentStage==stage）
        Rejected,         ///< 门控拒绝（currentStage 不变；就地提示数据在 reasonKeys/unlockHintKey）
        RejectedReadOnly, ///< 只读会话查看路径（已切换；viewOnlyStage 记录目标）
    };

    /// 结果形态（默认 Rejected＝"未发生任何切换"的安全空值——构造后未
    /// 显式赋值的 NavigateResult 不携带放行语义）。
    Kind kind = Kind::Rejected;
    /// 放行/只读的目标阶段（kind ∈ {Allowed, RejectedReadOnly} 时有意义；
    /// Rejected 时无导航语义）。
    StageId stage = StageId::Modeling;
    /// 拒绝原因＋缺项文案键（kind==Rejected 时非空——就地提示数据；
    /// 内容契约见 IUiStageGate::evaluate 注释）。
    std::vector<TextKey> reasonKeys;
    /// 解锁条件/下一步建议文案键（kind==Rejected 时可选——§6.4 时序
    /// "拒绝(原因/解锁条件)"的解锁半区）。
    std::optional<TextKey> unlockHintKey;

    /// @brief 便捷判定：是否已切换 currentStage（Allowed/RejectedReadOnly）。
    bool navigated() const noexcept
    {
        return kind == Kind::Allowed || kind == Kind::RejectedReadOnly;
    }
};

// =====================================================================
// 观察者与订阅（§10.2 subscribe——投影变更通知，UI 线程分发）
// =====================================================================

/**
 * @brief 阶段导航投影变更观察者（§10.2 IStageViewObserver——currentStage
 *        切换或门控/会话事实变化导致 views 重算后通知）。
 *
 * 通知语义（§6.1 pull-on-event——事件不携带数据）：回调只送达"变了"的
 * 身份信号，收方经入参模型拉取 stageViews()/currentStage() 快照——推送
 * 明细会放大拷贝面且可能中途过期，与 UiProjectionStore 的刷新模型一致。
 * 回调在 UI 线程同步执行（§3.4 M-1）；回调内不得调用 requestNavigate
 * （重入导航＝时序图外路径），可调用只读面。
 */
class IStageViewObserver {
public:
    virtual ~IStageViewObserver() = default;

    /// @brief 导航投影变更通知（无载荷——收方经 model 拉取快照）。
    /// @param model [in] 通知来源模型（收方借此拉取 stageViews/currentStage；
    ///              不交出所有权——观察者弱引用语义见 subscribe 注释）
    virtual void onStageViewsChanged(const IStageNavigationModel& model) = 0;
};

// =====================================================================
// IStageNavigationModel（§10.2 接口原文——五方法契约）
// =====================================================================

/**
 * @brief 阶段导航投影接口（§10.2 原文——顶部导航条/左栏阶段任务列表的
 *        数据面，及 StageStatusModel 供 workflow 消费的汇聚出口）。
 *
 * 生命周期/所有权（§10.2 契约表"生命周期/所有权"行）：模型归壳（L5
 * 装配期经 createStageNavigationModel 构造并注入依赖端口）；订阅句柄
 * RAII（观察者弱引用——观察者析构前必须退订或保证存活至句柄析构；
 * 退订幂等）。
 *
 * 线程（§10.2 线程行）：除 readinessSnapshot（任意线程拉取、值拷贝）外
 * 全部 UI 线程。
 */
class IStageNavigationModel {
public:
    virtual ~IStageNavigationModel() = default;

    /**
     * @brief 七阶段呈现视图（§10.2 "七项，固定顺序"）。
     * @return 七元素清单（stageIdSequence 序——每次调用现合成，快照语义
     *         见 StageView 注释）
     *
     * @note UI 线程调用。
     */
    virtual std::vector<StageView> stageViews() const = 0;

    /**
     * @brief 当前激活阶段（§10.2 "会话态"——用户所在；in-progress 合成
     *        的数据源）。
     * @return 当前阶段（初值由装配给定——StageNavigationModelDeps.
     *         initialStage）
     *
     * @note UI 线程调用。
     */
    virtual StageId currentStage() const = 0;

    /**
     * @brief 请求切换阶段（§10.2 "门控经 workflow（图 §6.4）"——阶段
     *        切换时序的模型侧执行体）。
     *
     * 时序（§6.4 图逐步落地）：
     *   ① 前置自检（§10.2 契约表"前置条件"行：requestNavigate 需会话非
     *      Opening/Closed——违约属调用方契约错误，fail-fast 抛出而非返回
     *      Rejected，避免把装配/时序缺陷伪装成正常拒绝）；
     *   ② 只读会话（writable==false）→ RejectedReadOnly（不经门控——
     *      见 NavigateResult 注释；currentStage 更新＋事件）；
     *   ③ 目标==当前 → Allowed 幂等返回（不重评估、不发事件——状态未
     *      变，重复评估徒增门控调用且事件语义失真）；
     *   ④ 现取目标阶段汇聚快照（readinessSnapshot——epoch 标注构建时刻）
     *      交门控评估（§6.4 时序"navigate(stage)→门控评估"）；
     *   ⑤ 允许 → currentStage=stage＋投影变更事件（§6.4 "允许：面板切换
     *      事件"——壳层据此装配中央区面板/刷新导航条/左栏列表）；拒绝 →
     *      原样返回门控判定（currentStage 不变、无事件——就地提示数据随
     *      结果值走）。
     *
     * @param stage [in] 目标阶段（七阶段词表值）
     * @return 显式结果（无异常路径——门控拒绝是正常业务结果不是错误，
     *         §10.2 契约表"错误类型"行）
     *
     * @throws std::logic_error 会话处于 Opening/Closed（前置条件违约——
     *         调用方错误 fail-fast）
     * @throws std::invalid_argument stage 非七阶段词表值（越界枚举——
     *         调用方错误 fail-fast）
     *
     * @note UI 线程调用；副作用＝currentStage 更新＋观察者通知，不改任何
     *       权威对象（§10.2 契约表"副作用"行）。
     */
    virtual NavigateResult requestNavigate(StageId stage) = 0;

    /**
     * @brief 订阅投影变更（§10.2 subscribe——观察者弱引用，句柄 RAII）。
     *
     * @param observer [in] 观察者（不取得所有权——观察者析构前必须退订
     *                 （析构句柄）或保证存活至句柄析构；重复退订幂等）
     * @return 订阅句柄（core::IEventSubscription 契约形态——析构＝退订；
     *         unsubscribe 幂等；退订后不再投递）
     *
     * @note UI 线程调用（订阅与回调同线程——§3.4 M-1，无并发登记面）。
     */
    virtual std::unique_ptr<core::IEventSubscription>
    subscribe(IStageViewObserver& observer) = 0;

    /**
     * @brief 汇聚目标阶段域就绪快照（§10.2 "ui 侧汇聚（供 workflow 消费，
     *        §6.5）"——StageStatusModel 的出口方法）。
     *
     * 汇聚纪律（§6.5 红线原文）：只汇聚域插件经注册端口上报的只读投影，
     * 不计算门控、不判定就绪——域项按注册序拼接（NFR-COR-02 稳定序），
     * 空域源不计入。epoch 取自纪元源当前值（§6.5 "构建纪元"）。
     *
     * @param stage [in] 目标阶段
     * @return 汇聚快照（值拷贝——workflow 任意线程可安全持有）
     *
     * @note 任意线程调用（§10.2 线程行"readinessSnapshot 供 workflow 在
     *       任意线程拉取（值拷贝）"——实现现取现拷贝、不触碰 UI 线程
     *       专属状态；域源实现须自行保证其线程契约，见
     *       IUiDomainReadinessSource::domainReadiness 注释）。
     */
    virtual StageReadinessSnapshot readinessSnapshot(StageId stage) const = 0;
};

// =====================================================================
// 装配依赖与工厂（L5 应用壳装配期注入——O-31 裁决的 ui 侧最小注入模式）
// =====================================================================

/**
 * @brief 导航模型的装配依赖（createStageNavigationModel 的注入面）。
 *
 * 为什么是端口指针＋函数对象而不是虚接口聚合：会话事实（epoch/可写性/
 * 可导航性）是 ui 自有会话态（UiProjectionStore/UiSessionController——
 * UI-T12 域）的读投影，装配期以轻量函数对象桥接即可；跨单元协作面
 * （门控/域就绪）按 O-31 裁决走 UiPorts.hpp 端口指针。两者都由 L5 应用
 * 壳装配期绑定，产品面零对端链接/include。
 *
 * 所有权：端口对象由注入方持有并保证存活期覆盖模型（模型不接管所有权
 * ——裸指针只读借用）；函数对象捕获的生命期由装配方保证。
 */
struct StageNavigationModelDeps {
    /// 阶段门控端口（必注入——WP-22-T03 未产出前为桩，IUiStageGate；
    /// nullptr＝装配契约违约，工厂 fail-fast）。
    const IUiStageGate* gate = nullptr;

    /// 会话纪元源（§6.2 epoch——必注入；nullptr＝装配契约违约。真实源
    /// 归 UiProjectionStore（§6.1），装配前以桩承载）。
    std::function<std::uint64_t()> epochSource;

    /// 会话可写性源（§5.5/§6.4 view-only 行数据源 writable——必注入；
    /// 真实源归 ProjectContextProjection.writable 的会话读取面）。
    std::function<bool()> writableSource;

    /// 会话可导航性源（§10.2 前置条件"非 Opening/Closed"——必注入；
    /// 真实源归 UiSessionController 会话态机 §5.2）。
    std::function<bool()> sessionNavigableSource;

    /// 每阶段七态触发数据源（§6.3"七态按评估域/阶段分别投影"——返回
    /// nullopt＝该阶段无七态数据源；求值经 evaluateStatusWord 唯一实现
    /// 点完成，模型不自建第二套七态判定。nullptr＝装配契约违约）。
    std::function<std::optional<StatusFacts>(StageId)> sevenStateFacts;

    /// 域就绪只读投影源清单（§6.5 汇聚输入——注册序＝快照 domains 序；
    /// 可为空＝当前无已注册域，快照 domains 为空。元素由注入方持有）。
    std::vector<const IUiDomainReadinessSource*> domainSources;

    /// 初始当前阶段（装配给定——默认建模阶段，UX-12 首阶段）。
    StageId initialStage = StageId::Modeling;
};

/**
 * @brief 构造阶段导航模型（默认实现——StageNavigationModel，L5 装配入口）。
 *
 * @param deps [in] 装配依赖（所有权语义见结构体注释）
 * @return 模型实例（归调用方/壳持有）
 *
 * @throws std::invalid_argument deps 的必注入项缺失（gate/epochSource/
 *         writableSource/sessionNavigableSource/sevenStateFacts 任一为空
 *         ——装配契约违约 fail-fast：缺依赖的模型会把"无门控"伪装成
 *         "全部放行"，违反 N-11 纪律，宁可不装配）
 */
std::unique_ptr<IStageNavigationModel>
createStageNavigationModel(StageNavigationModelDeps deps);

}  // namespace ui
}  // namespace ird
}  // namespace sdurws

#endif  // SDURWS_IRD_UI_ISTAGENAVIGATIONMODEL_HPP
