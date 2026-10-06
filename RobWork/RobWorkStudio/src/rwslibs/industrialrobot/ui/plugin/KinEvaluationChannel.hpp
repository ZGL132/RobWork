/**
 * @file   KinEvaluationChannel.hpp
 * @brief  kinematics 覆盖评估执行通道的产品装配半区（UI-T64——F-490①
 *         上游批；ui 插件私有，编入 ui 插件目标）。
 *
 * 设计依据：
 *   - 任务契约 tasks/foundation/UI-T64.json（触发缝绑定＋结构化结果回路
 *     ——L-K6 区域覆盖运行的产品落位）；
 *   - findings F-490①（上游前置：KinPanelServices 产品侧零调用＝
 *     SampleResultSet 无生产者——本文件即生产者装配面）；
 *   - units/kinematics.md §9.8（L-K6 数据流；">1 s 全部转 execution"
 *     线程纪律）、§14.3 P-KIN-7/P-KIN-2（协议归装配层；宿主闭包注入）。
 *
 * 背景说明（第一读者须知——本文件在产品里的位置）：
 *   评估的**域计算面**在 kinematics 计算库（runRegionCoverageComputation
 *   纯函数——采样→逐样本评估→覆盖率，零项目写入）；面板的**受理呈现
 *   面**在 kinematics 插件（KinPanelFlows 经 backgroundSubmit 缝提交）。
 *   两者之间缺的正是装配层执行器：受理校验→需求工作集切片→后台线程
 *   执行→结构化结果账面→完成通知回投 UI 线程。本文件承载该执行器。
 *
 * 权威边界（PA-1 纪律的执行器侧声明——review 重点核对项）：
 *   - 零修订面写入：评估结果持于本执行器的**会话级账面**（最近一次，
 *     按 snapshotId+epoch 锚定），不进 project/evidence——正式评估归档
 *     链（AnalysisSnapshot 冻结对账/envelope/workflow 任务注册）留后续
 *     任务（契约诚实边界）；
 *   - 非第二状态源：账面随快照换绑（attachSnapshot 重置）——消费方必须
 *     核对 latestCoverageResult().snapshotId 与当前会话绑定一致，不一致
 *     即跨快照误投影（拒绝消费）；
 *   - 零语义复判：切片只做"宿主解析投影"（参考系变换/域类型→域投影型
 *     的字段搬运），不裁剪、不推断、不校验业务合法性（域校验链在
 *     requirements 编辑器与 kinematics 装配期 fail-fast 面）。
 *
 * 线程模型：
 *   - 本类全部公开方法 **仅 UI 线程**调用（面板缝的调用线程纪律传递）；
 *   - 评估在 std::async 后台线程执行（任务闭包持切片值拷贝＋快照
 *     shared_ptr——零共享可变态）；
 *   - 完成回投经 QMetaObject::invokeMethod(routeContext, lambda,
 *     Qt::QueuedConnection) 回 UI 线程后才写账面/投 note（UI 线程写点
 *     纪律——harness 的同步直调不可照抄）。
 */
#ifndef IRD_UI_PLUGIN_KINEVALUATIONCHANNEL_HPP
#define IRD_UI_PLUGIN_KINEVALUATIONCHANNEL_HPP

#include <atomic>
#include <cstdint>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <QObject>
#include <QString>
#include <rw/math/Transform3D.hpp>

#include <sdurws/ird/core/Identity.hpp>          // ContentIdentity/ObjectId
#include <sdurws/ird/evidence/Evaluator.hpp>     // evidence::EvaluationRequest/IEvaluationContext
#include <sdurws/ird/kinematics/AnalysisConfig.hpp>  // AnalysisConfiguration（求解配置基线）
#include <sdurws/ird/kinematics/Evidence.hpp>    // BatchCondition（批量投影型）
#include <sdurws/ird/kinematics/KinematicsPanelChannels.hpp>  // 通道值面（公共契约）
#include <sdurws/ird/kinematics/Sampling.hpp>    // SamplingPlan/RegionSamplingBudget（域投影型）
#include <sdurws/ird/runtime/Snapshot.hpp>       // runtime::RuntimeSnapshot（快照持有）
#include <sdurws/ird/requirements/Editor.hpp>    // IRequirementEditor（工作集切片源）
#include <sdurws/ird/ui/View3DPreviewContract.hpp>  // View3DCellState/View3DTint（三维词表——映射目标）

namespace sdurws {
namespace ird {
namespace ui {

// =====================================================================
// KinSessionModelView——地址稳定的可换绑模型视图（缝注入值）
// =====================================================================

/**
 * @brief 会话级长命模型视图（IKinRuntimeView 转发——P-KIN-2"宿主经
 *        工厂闭包注入"的产品形态）。
 *
 * 两个使用面的调和（第一读者须知）：
 *   - **缝地址稳定性**：面板服务缝（KinPanelServices.modelView）是
 *     install 期一次性注入的裸指针——视图对象必须长命且地址恒定；
 *   - **"每请求构建"语义**：IKinRuntimeView 契约要求视图随快照换绑
 *     （请求结束失效——旧快照视图不得跨发布复用）。
 *
 * 本类以**委托转发**调和两者：对象本体长命（执行器成员——地址稳定），
 * 内部委托随 attachView 换绑（旧委托随在途任务的 shared_ptr 存活到
 * 任务结束——任务闭包捕获的是换绑前的委托值，天然隔离）。
 *
 * 语义逐字对应（无第二语义）：model()/worldToBase() 纯转发——产品
 * 委托是 KinRuntimeSnapshotView（RuntimeSnapshot 的逐字薄壳），测试
 * 委托是替身视图；适配零语义。
 *
 * 线程约束：rebind 仅 UI 线程（会话换绑拍）；model()/worldToBase()
 * 的并发只读消费＝面板内联消费（UI 线程）＋后台任务（经换绑前的
 * 委托值——与本对象无共享）。
 */
class KinSessionModelView final : public kinematics::IKinRuntimeView {
public:
    /// 构造未绑定视图（委托空——快照发布前缝已注入的合法初始态；
    /// 消费前置由面板空态判定保证——快照缺位不取模型）。
    KinSessionModelView() = default;

    /// 换绑委托（会话换绑拍——UI 线程；地址稳定性由本对象保证）。
    void rebind(std::shared_ptr<const kinematics::IKinRuntimeView> delegate)
    {
        m_delegate = std::move(delegate);
    }

    /// 规范模型只读真值（纯转发——语义同委托 model()）。
    const runtime::CanonicalModel& model() const override
    {
        return m_delegate->model();  // 未绑定调用＝装配缺陷（前置违约——
                                     // 面板空态判定挡在消费之前）
    }
    /// T_world_base（世界系→基座系——纯转发，快照 §6.4 唯一读取点）。
    rw::math::Transform3D<double> worldToBase() const override
    {
        return m_delegate->worldToBase();
    }

private:
    /// 当前委托（空＝未绑定——attachView 前的合法初始态）。
    std::shared_ptr<const kinematics::IKinRuntimeView> m_delegate;
};

// =====================================================================
// NullEvaluationContext——纯计算面的宿主上下文最小实现
// =====================================================================

/**
 * @brief 评估上下文的会话级最小实现（evidence::IEvaluationContext）。
 *
 * 三方法语义（与正式执行通道的差异即本批诚实边界）：
 *   - cancellationRequested()：恒 false——本批无取消设施（协作取消的
 *     宿主信号面随正式归档链任务落位；评估不可取消＝如实语义，不是
 *     吞信号）；
 *   - reportProgress()：吞掉（进度设施归宿主——本批任务行的进度值
 *     缺省呈现，不伪造百分比）；
 *   - tryObjectBytes()：恒 nullopt——纯计算面不读物化对象（worker
 *     物化形态归 execution 分发面，本批零分发）。
 *
 * 线程约束：仅后台评估线程在 evaluate 期间使用（引用传入——调用期
 * 存活由任务闭包栈保证）；无状态可共享。
 */
class NullEvaluationContext final : public evidence::IEvaluationContext {
public:
    bool cancellationRequested() const override { return false; }
    void reportProgress(std::uint8_t percent, std::string_view phase) override
    {
        (void)percent;
        (void)phase;  // 吞掉——进度设施未装配（见类注；不伪造进度）
    }
    std::optional<std::vector<std::uint8_t>> tryObjectBytes(
        core::ObjectId objectId, core::ContentVersion contentVersion) const override
    {
        (void)objectId;
        (void)contentVersion;
        return std::nullopt;  // 纯计算面零物化读取（见类注）
    }
};

// =====================================================================
// 呈现对照映射（F-495 消费卡的纯值词表翻译——判定零参与：spec §2.4
// "着色判定归域侧，呈现只分色"口径的两函数落位）
// =====================================================================

/**
 * @brief 域样本五值 → 三维格元词表映射（inline 纯函数——逐值词表翻译
 *        零判定；语义：Reached→Good 绿／Unreachable→Failed 红／
 *        DataInsufficient→Weak 黄〔数据不足＝警告档〕／NotRun·
 *        NotApplicable→NotSampled 灰〔未采样诚实态〕）。
 */
inline View3DCellState mapSampleStateToCell(kinematics::KinChannelSampleState state)
{
    switch (state) {
    case kinematics::KinChannelSampleState::Reached:
        return View3DCellState::Good;
    case kinematics::KinChannelSampleState::Unreachable:
        return View3DCellState::Failed;
    case kinematics::KinChannelSampleState::DataInsufficient:
        return View3DCellState::Weak;
    case kinematics::KinChannelSampleState::NotApplicable:
    case kinematics::KinChannelSampleState::NotRun:
        return View3DCellState::NotSampled;
    }
    return View3DCellState::NotSampled;  // 不可达（switch 全覆盖——防御面）
}

/**
 * @brief 覆盖率框色档位（inline 纯函数——呈现对照：位置口径计数比 ×
 *        目标下限的三档映射；**非工程判定**——达标语义权威归域/evidence，
 *        本映射只服务框色呈现〔View3DTint 注〕）。
 *
 * @param reached          [in] 位置口径 Reached 计数（无量纲）
 * @param planned          [in] 位置口径分母计数（无量纲；0＝零样本）
 * @param minTargetCoverage [in] 覆盖率目标下限（∈[0,1]——REQ-03；
 *                          nullopt＝区域未设目标→None）
 * @return Good＝比率≥目标／Weak＝比率<目标／None＝未评估·零分母·无目标
 */
inline View3DTint view3DTintFromCoverage(std::uint64_t reached,
                                         std::uint64_t planned,
                                         std::optional<double> minTargetCoverage)
{
    // 对照输入不完整＝无映射（未评估/零分母/无目标——不虚构档位）。
    if (planned == 0 || !minTargetCoverage.has_value()) {
        return View3DTint::None;
    }
    const double ratio = static_cast<double>(reached)
                         / static_cast<double>(planned);
    return ratio >= *minTargetCoverage ? View3DTint::Good : View3DTint::Weak;
}

/**
 * @brief 逐区域位置口径计数（inline 纯函数——UI-T65 返工把合并段计数
 *        逻辑抽出的可测半区；区域过滤＋kind 分轴计数一体）。
 *
 * 口径权威（PA-1 工程判定——与域 KIN-04 位置轴同定义，acc/ui-t65/1
 * 阻断 A 的口径修正本体）：
 *   分母＝该区域（regionObjectId 相等）kind==Position 的样本数。域
 *         generateSampleSet 的不变式"样本总数＝plannedPositionSamples＋
 *         plannedPoseSamples"保证逐 kind 计数与计划分母逐区域同构；
 *   分子＝其中 kind==Position 且 state==Reached。
 * 位姿（Pose）样本**不入**框色对照输入：UI 评估流恒产位姿样本（需求
 * 侧 orientationSampling 缺省 1×1 且域装配保证 ≥1），全样本混计比与域
 * 位置覆盖率系统性分叉——同屏面板已消费域 computation.coverage.position
 * （位置口径），框色不得是另一口径（契约 acceptance 3"位置口径"一字
 * 未改，所有者 2026-10-06 裁决）。
 *
 * @param samples        [in] 覆盖结果逐样本账面（执行器投影出口——
 *                       含 kind 双口径判定键；只读，本函数不接管所有权）
 * @param regionObjectId [in] 目标区域对象身份（区域过滤键——精确相等）
 * @return 位置轴计数对（reached/planned；零样本区域＝0/0——由
 *         view3DTintFromCoverage 映射为 None）
 */
struct RegionPositionTally {
    std::uint64_t reached = 0;  ///< 位置轴分子（该区域 Position∧Reached；无量纲）
    std::uint64_t planned = 0;  ///< 位置轴分母（该区域 Position 样本数；无量纲）
};

inline RegionPositionTally tallyRegionPositionCoverage(
    const std::vector<kinematics::KinChannelSampleRecord>& samples,
    const core::ObjectId& regionObjectId)
{
    RegionPositionTally tally;
    for (const auto& record : samples) {
        if (!(record.regionObjectId == regionObjectId)) {
            continue;  // 他区域样本——预览是单选区域面（过滤不入计数）
        }
        if (record.kind != kinematics::KinChannelSampleKind::Position) {
            continue;  // 位姿样本不入位置轴（双口径分轴——类注口径权威）
        }
        ++tally.planned;  // 位置轴分母（不可达/数据不足一律保留——域 R8）
        if (record.state == kinematics::KinChannelSampleState::Reached) {
            ++tally.reached;  // 位置轴分子
        }
    }
    return tally;
}

/**
 * @brief 区域框色对照一体入口（inline 纯函数——UI-T65 返工：逐区域
 *        位置口径计数＋比率分档的合并段唯一调用点；可测性承载——
 *        双口径发散用例直接断言本函数）。
 *
 * 与调用方（UiPlugin 合并段）的分工：着色点层（全 kind 样本的
 * samples/cellStates 投递）留在合并段；框色（位置口径对照）收拢于
 * 本函数——计数逻辑全部进 ui_test 可编译单元，合并段回归"透传＋
 * 调用"薄面。
 *
 * @param samples           [in] 覆盖结果逐样本账面（同 tallyRegionPositionCoverage）
 * @param regionObjectId    [in] 目标区域对象身份（区域过滤键）
 * @param minTargetCoverage [in] 位置覆盖率目标下限（∈[0,1]——REQ-03；
 *                          nullopt＝区域未设目标→None）
 * @return Good/Weak/None 三档（语义同 view3DTintFromCoverage——内部
 *         复用其分档，比率计算单一实现）
 */
inline View3DTint view3DRegionTintFromSamples(
    const std::vector<kinematics::KinChannelSampleRecord>& samples,
    const core::ObjectId& regionObjectId,
    std::optional<double> minTargetCoverage)
{
    const RegionPositionTally tally =
        tallyRegionPositionCoverage(samples, regionObjectId);
    return view3DTintFromCoverage(tally.reached, tally.planned,
                                  std::move(minTargetCoverage));
}

// =====================================================================
// KinEvaluationExecutor——覆盖评估执行器（装配层受理/切片/后台执行/账面）
// =====================================================================

/**
 * @brief kinematics 覆盖评估执行器（UI-T64 产品装配半区的主体）。
 *
 * 职责链（acceptance 2 的结构面）：
 *   受理（UI 线程）→ 切片（UI 线程——工作集现取值拷贝）→ 后台执行
 *   （std::async——纯计算面）→ 回投（QMetaObject::invokeMethod）→
 *   账面更新＋note 投递（UI 线程）。
 *
 * 生命周期与所有权：由 UiPlugin 持有（成员——与三域 bundle 同存活
 * 期）；注入的 editor 指针非 owning（存活期契约＝UiPlugin 成员序，
 * editor 先于本执行器构造、后于其析构——成员声明序保证）。
 *
 * 线程约束：全部公开方法仅 UI 线程（类注职责链的线程分格——后台
 * 任务只读捕获值，零共享可变态回写）。
 */
class KinEvaluationExecutor {
public:
    /**
     * @brief 执行器依赖注入（构造一次性绑定）。
     */
    struct Deps {
        /// 需求工作集切片源（非 owning——受理时经 workingSet() 现取；
        /// 空＝受理恒拒"需求工作集未装配"——不虚构切片）。
        requirements::IRequirementEditor* editor = nullptr;
        /// 参考系解析缝（模型帧对象 ObjectId→宿主 WorkCell 帧的世界系
        /// 位姿；nullopt＝帧不可解析——该计划诚实拒绝。装配层从需求三
        /// 维投影的帧名映射同源闭包接线，零第二解析路径）。空缝＝非
        /// World 参考系恒拒（World 缺省面不依赖本缝）。
        std::function<std::optional<rw::math::Transform3D<double>>(
            const core::ObjectId&)>
            resolveFrame;
        /// 完成回投的 QObject 上下文（QMetaObject::invokeMethod 目标——
        /// 其线程亲和＝UI 线程；空＝回投降级为主线程直调不可用，任务
        /// 结果仅弃置——装配缺陷在装配期暴露）。
        QObject* routeContext = nullptr;
        /// 完成通知投递槽（回 UI 线程后调用——门面
        /// noteAssemblyBackgroundResult 的消费闭包；空＝note 丢弃仅
        /// 账面更新）。
        kinematics::KinChannelResultRouteFn resultRoute;
    };

    /**
     * @brief 构造执行器（UI 线程——装配期）。
     * @param deps [in] 依赖聚合（值拷贝——editor 指针非 owning）
     */
    explicit KinEvaluationExecutor(Deps deps);

    // ---- 会话事实接线（UiPlugin 快照发布消费点调用——UI 线程）----

    /**
     * @brief 换绑发布快照（快照发布消费点——零第二编译路径纪律的同
     *        源消费：UiPlugin 把 lastPublishedSnapshot() 的同一值传给
     *        本执行器与其它消费面）。
     *
     * 换绑语义：快照指针与内容身份整体替换＋**纪元推进**（epoch+1
     * ——在途任务的完成回执按旧纪元比对即"迟到"丢弃，L-K12：跨快照
     * 结果不进新会话视图）；覆盖结果账面同拍清空（旧快照结果对新
     * 快照不可投影——PA 权威纪律的呈现侧镜像）。
     *
     * @param snapshot   [in] 已发布快照（shared_ptr——任务存活期锚；
     *                   空＝解绑〔无项目/编译失败态〕，受理恒拒）
     * @param snapshotId [in] 快照内容身份（＝snapshot->modelIdentity()
     *                   的装配层直投——受理一致性核对键）
     */
    void attachSnapshot(std::shared_ptr<const runtime::RuntimeSnapshot> snapshot,
                        core::ContentIdentity snapshotId);

    /**
     * @brief 换绑模型视图（attachSnapshot 的视图级供数面——测试注入
     *        替身视图用；产品路径走 attachSnapshot 的包裹面。换绑语义
     *        与 attachSnapshot 逐字一致：整体替换＋纪元推进＋账面清空）。
     *
     * @param view       [in] 模型只读视图（IKinRuntimeView——执行器消费
     *                   面＝model()/worldToBase() 两成员；空＝解绑）
     * @param snapshotId [in] 结果绑定快照内容身份（受理一致性核对键）
     */
    void attachView(std::shared_ptr<const kinematics::IKinRuntimeView> view,
                    core::ContentIdentity snapshotId);

    /**
     * @brief 同步求解配置基线（与 kinematics 会话态 savedConfig 同源
     *        同值——装配层单点写两处；须恒过
     *        validateAnalysisConfiguration，I-KIN-4）。
     */
    void setConfiguration(const kinematics::AnalysisConfiguration& config);

    /// 会话级长命视图（缝地址的持有本体——rebind 随 attachView 同拍）。
    KinSessionModelView m_sessionView;

    /// 当前会话纪元（装配层写 session().epoch 的同源值）。
    std::uint64_t epoch() const { return m_epoch; }

    /// 当前绑定的快照内容身份（装配层写 session().snapshotId 的同源值；
    /// 未绑定＝缺省 ContentIdentity——isValid() false）。
    const core::ContentIdentity& snapshotId() const { return m_snapshotId; }

    /**
     * @brief 会话级长命视图地址（KinematicsAssemblyChannels.modelView
     *        缝的注入值——地址稳定性：执行器堆持有，视图随执行器存活；
     *        快照发布拍经 rebind 换绑内部委托，缝消费者无感）。
     *
     * 未绑定态（快照缺位）：视图对象存在但内部委托空——面板能力判定
     * 应以会话态 snapshotId 为准（空态判定不依赖视图解引用）。
     */
    KinSessionModelView* sessionView() { return &m_sessionView; }

    // ---- 面板缝投影（KinematicsAssemblyChannels 的装配闭包消费面）----

    /**
     * @brief 后台提交受理（面板 backgroundSubmit 缝的实现——UI 线程）。
     *
     * 受理校验链（短路，拒绝 reason 如实——ERR-01 不虚构受理）：
     *   ①kind 分流：SessionSolve/TaskPointsBatch 拒绝（本批范围＝
     *     区域覆盖通道——两通道的执行缝未装配，reason 写明）；
     *   ②快照绑定缺位（无已应用模型）拒绝；
     *   ③请求 snapshotId 与当前绑定不一致拒绝（面板会话态与执行器
     *     换绑间的竞态窗口——拒绝而非错评）；
     *   ④切片失败拒绝（工作集无计划/计划悬空/参考系不可解析/旋转
     *     参考系等——reason 带首个违例）。
     *
     * @param request [in] 提交请求（面板流构造）
     * @return 受理回执（accepted＋taskRef 或拒绝 reason）
     */
    kinematics::KinChannelBackgroundAck submit(
        const kinematics::KinChannelBackgroundRequest& request);

    /**
     * @brief 最近一次覆盖评估的结构化结果（F-495 消费卡与覆盖页投影
     *        的出口）。
     *
     * 账面纪律：nullopt＝尚无结果；有值时消费方**必须**核对
     * result.snapshotId 与当前会话绑定一致（不一致＝跨快照误投影——
     * 拒绝消费）。ok=false 的失败结果同样入账（errorText 如实——
     * 失败可见面）。
     */
    std::optional<kinematics::KinChannelCoverageResult> latestCoverageResult() const
    {
        return m_latestCoverage;
    }

    /**
     * @brief 任务状态投影（面板 taskRows 缝的消费面——在途＋最近完成
     *        行；空向量＝无任务）。
     */
    std::vector<kinematics::KinChannelTaskStatusRow> taskRows() const;

private:
    /// 切片产物（RegionCoverageQuery＋绑定面值的受理期值包——后台任务
    /// 按值捕获，零共享可变态）。
    struct CoverageSlice {
        kinematics::RegionCoverageQuery query;   ///< 评估查询（域投影型）
        evidence::EvaluationRequest request;     ///< 评估请求（绑定面——Preview 语义）
        std::string errorText;                   ///< 非空＝切片失败（受理拒绝依据）
    };

    /// 切片需求工作集 → 覆盖评估值包（UI 线程受理期执行；域投影零
    /// 语义复判——类注权威边界）。编辑器缺位→errorText"需求工作集
    /// 未装配"；plans 空→"工作集无采样计划"；逐计划：regionRef 悬空/
    /// 参考系不可解析/参考系相对基座存在旋转（R1 轴对齐盒约束——
    /// 旋转参考系解析留后续任务，reason 如实）→ 首个违例入 errorText。
    CoverageSlice sliceCoverage() const;

    /// 单计划投影（计划条目＋区域条目 → 域 SamplingPlan——盒基座系
    /// 解析＋采样定义直投；失败 reason 追加至 errorText）。
    bool projectPlan(const requirements::SamplingPlan& plan,
                     const requirements::WorkRegion& region,
                     kinematics::SamplingPlan& out, std::string& errorText) const;

    /// 任务账目（在途/完成行的最小账面——taskRows 投影源）。
    struct TaskEntry {
        std::string taskRef;        ///< 任务引用文本（对齐键）
        std::uint64_t epoch = 0;    ///< 提交纪元（迟到判定）
        bool running = true;        ///< true＝在途；false＝终态
        bool interrupted = false;   ///< 终态中断标记
        std::string summary;        ///< 终态摘要（空＝在途）
    };

    /// 完成处理（后台线程经 invokeMethod 回 UI 线程后执行——账面写点
    /// ＋结果入账＋note 投递，全部在本方法内完成。绑定键取**提交时**
    /// 快照/配置值——迟到任务的产物按提交锚丢弃，绝不打上新锚）。
    void onCoverageFinished(const std::string& taskRef,
                            const core::ContentIdentity& snapshotIdAtSubmit,
                            const core::ContentIdentity& configDigestAtSubmit,
                            std::uint64_t epoch,
                            kinematics::KinChannelCoverageResult result,
                            bool interrupted);

    /// 受理后解绑的中止处理（会话换绑/拆除竞态的诚实收口——账目终态
    /// ＋note 投递，零结果入账：中止无产物可消费）。
    void onViewDetached(const std::string& taskRef, std::uint64_t epoch,
                        const std::string& reason);

    Deps m_deps;  ///< 依赖（editor/resolveFrame/routeContext/resultRoute）

    std::shared_ptr<const kinematics::IKinRuntimeView> m_snapshot;  ///< 当前模型视图（空＝未绑定；产品路径＝快照包裹器，测试路径＝替身）
    core::ContentIdentity m_snapshotId;  ///< 当前快照内容身份（受理一致性键）
    kinematics::AnalysisConfiguration m_config{};  ///< 求解配置基线（合法——装配注入）
    std::uint64_t m_epoch = 1;  ///< 会话纪元（换绑推进——L-K12 迟到锚）
    std::uint32_t m_seq = 0;    ///< 任务序号（受理递增——taskRef 组成）

    std::optional<kinematics::KinChannelCoverageResult> m_latestCoverage;  ///< 最近覆盖结果账面
    std::vector<TaskEntry> m_tasks;  ///< 任务账目（在途＋最近完成——投影序＝登记序）
    /// 在途任务 future（std::async 的 future 析构即阻塞等待——必须持有
    /// 至任务终态，否则 submit 会同步等完整个评估；完成回投后由
    /// onCoverageFinished 惰性清理已就绪项）。仅 UI 线程访问。
    std::vector<std::future<void>> m_inflight;
};

}  // namespace ui
}  // namespace ird
}  // namespace sdurws

#endif  // IRD_UI_PLUGIN_KINEVALUATIONCHANNEL_HPP
