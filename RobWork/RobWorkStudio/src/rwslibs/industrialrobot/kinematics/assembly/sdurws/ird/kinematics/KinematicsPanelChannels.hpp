/**
 * @file   KinematicsPanelChannels.hpp
 * @brief  kinematics 面板装配通道值面（UI-T64——覆盖评估执行通道产品
 *         装配的公共契约；F-490① 上游批）。
 *
 * 设计依据：
 *   - 任务契约 tasks/foundation/UI-T64.json（kinematics 覆盖评估执行
 *     通道产品装配——触发缝绑定＋结构化结果回路）；
 *   - units/kinematics.md §9.8 数据流 L-K3/L-K6（批量验证/区域覆盖运行
 *     ——">1 s 全部转 execution"，提交缝与完成回执的装配半区）；
 *   - units/kinematics.md §14.3 P-KIN-7（对端契约按当前文本基线实现
 *     ——真实 execution 提交协议归装配层）；
 *   - findings F-490①（上游前置登记：KinPanelServices 产品侧零调用
 *     ＝SampleResultSet 无生产者，F-495 消费卡无源可接）。
 *
 * 背景说明（第一读者须知——为什么需要独立通道值面）：
 *   面板服务缝（KinPanelServices）及其往返值类型
 *   （KinBackgroundRequest/KinBackgroundAck/KinBackgroundResultNote/
 *   KinTaskPointRow/KinTaskStatusRow）是 kinematics **插件私有类型**
 *   （plugin/KinPanelTypes.hpp）。产品装配层位于 ui 单元
 *   （ui/plugin/DomainAssembly.cpp / UiPlugin.cpp）——R-2 红线禁止
 *   跨单元消费私有头，装配层因此既看不见也构造不了这些值（这正是
 *   F-490① 登记的"setServices 产品侧零调用"僵局的根源）。
 *
 *   本头是破局面：在 kinematics 单元自己的 **assembly 公共目录**（装配
 *   契约头落位域——KinematicsPluginAssembly.hpp 同域先例，"插件目标的
 *   PUBLIC include 面"）定义与私有类型**同构**的通道投影值。装配层只
 *   include 本头（公共面），门面实现 TU（KinematicsPluginAssembly.cpp
 *   ——kinematics 插件目标自身，同单元消费私有头合法）做**逐字段翻译**
 *   ——翻译单点纪律：同构投影的任何字段漂移都会在翻译编译点暴露。
 *
 * 权威边界（PA 纪律的通道侧声明）：
 *   本头的值全部是"装配投影"——面板私有类型仍是面板内部呈现的真值，
 *   域权威仍在 kinematics 域与 requirements 工作集；本头不引入第二状态
 *   源（通道值即传即用，无缓存语义）。
 *
 * 线程约束：通道 function 的调用线程与面板缝一致——提交/数据源仅 UI
 *   线程调用（面板流在 UI 线程触发）；ResultNote 回投由装配层保证回
 *   UI 线程（QMetaObject::invokeMethod——执行器侧义务）。
 */
#ifndef IRD_KINEMATICS_ASSEMBLY_KINEMATICSPANELCHANNELS_HPP
#define IRD_KINEMATICS_ASSEMBLY_KINEMATICSPANELCHANNELS_HPP

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <sdurws/ird/core/Identity.hpp>       // core::ObjectId/ContentIdentity（身份值面）
#include <sdurws/ird/kinematics/Evaluators.hpp>  // IKinRuntimeView/IIkSolver/IFkEvaluator（缝的公共类型——门面直传）

namespace sdurws {
namespace ird {
namespace kinematics {

// =====================================================================
// 后台执行通道（与 plugin/KinPanelTypes.hpp 的 KinBackgroundKind/Request/
// Ack/ResultNote 同构——翻译单点：KinematicsPluginAssembly.cpp 逐字段拷贝）
// =====================================================================

/**
 * @brief 后台执行请求类别（三值——与插件私有 KinBackgroundKind 同构；
 *        §9.8 线程约束">1 s 全部转 execution"的三类请求面）。
 */
enum class KinChannelBackgroundKind : std::uint8_t {
    /// 会话级单点求解超内联预算转后台（L-K2 超界分支——不写 results）。
    SessionSolve,
    /// 任务点批量验证（L-K3——正式评估通道）。
    TaskPointsBatch,
    /// 区域覆盖评估（L-K6——本批产品装配的核心通道）。
    RegionCoverage,
};

/**
 * @brief 后台执行请求值（与插件私有 KinBackgroundRequest 同构）。
 *
 * 载荷刻意最小（P-KIN-7：切片细节——采样计划/任务点集/求解配置的
 * 具体值——由装配层执行器从 requirements 工作集与求解配置现取组装，
 * 面板零切片解析，R-1）：面板只表达"哪类评估、绑哪个快照与配置"。
 */
struct KinChannelBackgroundRequest {
    /// 请求类别（三值词表——见枚举注）。
    KinChannelBackgroundKind kind = KinChannelBackgroundKind::TaskPointsBatch;
    /// 结果绑定快照内容身份（§5.6 绑定六要素之一——执行器受理校验面：
    /// 与当前发布快照不一致＝拒绝受理，不虚构评估）。
    core::ContentIdentity snapshotId;
    /// 求解配置摘要（config.ik——analysisConfigurationDigest 产出；执行
    /// 器据此对齐评估查询的配置基线）。
    core::ContentIdentity configDigest;
    /// 提交时会话纪元（迟到判定锚——完成回执按此比对，迟到结果不进
    /// 当前会话视图）。
    std::uint64_t epoch = 0;
};

/**
 * @brief 后台提交回执（与插件私有 KinBackgroundAck 同构）。
 *
 * accepted=false 时 reason 如实给出拒绝原因（如"执行通道未装配"
 * "结果绑定快照缺位"）——不虚构已提交（ERR-01）。
 */
struct KinChannelBackgroundAck {
    /// 是否受理（true＝装配层已接收并转后台执行——进度经任务投影刷新）。
    bool accepted = false;
    /// 拒绝/受理说明（呈现于面板状态行——如实文本）。
    std::string reason;
    /// 任务引用文本（受理时装配层回填——任务投影行的对齐键；可空）。
    std::string taskRef;
};

/**
 * @brief 后台完成结果通知值（与插件私有 KinBackgroundResultNote 同构）。
 *
 * 装配层执行器在任务归档/终止后经门面 noteAssemblyBackgroundResult
 * 投递；acceptedEpoch 用于迟到比对（面板模块现纪元≠提交纪元→结果
 * 丢弃并如实反馈，不进当前会话视图——L-K12）。
 */
struct KinChannelBackgroundResultNote {
    /// 提交时纪元（与请求.epoch 同值——执行器原样回传）。
    std::uint64_t acceptedEpoch = 0;
    /// 类别（与请求.kind 同值）。
    KinChannelBackgroundKind kind = KinChannelBackgroundKind::TaskPointsBatch;
    /// 是否中断/取消终态（true＝"已中断"如实呈现——NFR-REL-03）。
    bool interrupted = false;
    /// 结果摘要文本（已投影——如"42/60 点可达"；呈现于状态行）。
    std::string summaryText;
};

// =====================================================================
// 任务点/任务状态投影（与插件私有 KinTaskPointRow/KinTaskStatusRow 同构）
// =====================================================================

/**
 * @brief 任务点投影行（与插件私有 KinTaskPointRow 同构——宿主从
 *        requirements 工作集 points 现取投影；面板零缓存每次刷新现调）。
 */
struct KinChannelTaskPointRow {
    /// 任务点对象身份（面板选中联动的定位锚）。
    core::ObjectId pointOid;
    /// 显示标签（工程用语——UX-02）。
    std::string label;
    /// 是否启用（停用点仍在表中——批量通道以 NotApplicable 显式标记，
    /// 表内如实呈现不停删）。
    bool enabled = true;
    /// 结果状态词（批量 per-item 状态投影；nullopt＝未计算）。
    std::optional<std::string> outcomeState;
    /// 结果摘要文本（已投影；nullopt＝未计算）。
    std::optional<std::string> outcomeText;
    /// 必验覆盖完成标记（true＝该点全部必验工况有计算终态；nullopt＝
    /// 未计算）。
    std::optional<bool> requiredCoverageDone;
};

/**
 * @brief 任务状态投影行（与插件私有 KinTaskStatusRow 同构——装配层
 *        从自己的在途/历史任务账本投影；空向量＝无任务）。
 */
struct KinChannelTaskStatusRow {
    /// 任务标识文本（呈现用）。
    std::string taskRefText;
    /// 九态短标签文案键（ui 词表 taskStateLabelKey 产出值原样透传）。
    std::string stateLabelKey;
    /// 是否中断态（呈现侧加粗/着色的语义锚）。
    bool interrupted = false;
    /// 展示进度（0~100；nullopt＝该态无进度语义）。
    std::optional<int> percent;
};

// =====================================================================
// 结构化覆盖结果投影（KinChannelCoverageResultView——结构化结果回路的
// 出口值面；F-495 需求三维采样着色闭环消费卡的输入）
// =====================================================================

/**
 * @brief 单样本评估状态五值投影（与域 SampleState 五值词表同构——
 *        Sampling.hpp 枚举注的映射口径；通道头零域头依赖面收拢在此：
 *        消费方经本枚举读状态，不直达域头——值语义逐字一致）。
 */
enum class KinChannelSampleState : std::uint8_t {
    /// 存在 ≥1 个通过全部硬过滤的解（域 Reached——分子态）。
    Reached,
    /// 解析界限确定性证明（域 Unreachable——唯一确定性不可达路径）。
    Unreachable,
    /// 数据不足（域 DataInsufficient——搜索未果/缺碰撞检测器；保留
    /// 分母不计分子＋整体降级）。
    DataInsufficient,
    /// 不适用（域 NotApplicable——词表完备性保留）。
    NotApplicable,
    /// 未运行（域 NotRun——协作取消后未派发；partial 如实标记）。
    NotRun,
};

/**
 * @brief 单样本结果投影记录（与域 SampleResultRecord 同构——F-495
 *        消费卡按 sampleIndex 与需求侧采样格对齐着色）。
 */
struct KinChannelSampleRecord {
    /// 对齐键（＝域 SampleRecord::sampleIndex——分母完整性核查键）。
    std::uint64_t sampleIndex = 0;
    /// 评估状态（五值——枚举注）。
    KinChannelSampleState state = KinChannelSampleState::NotRun;
    /// 原因文本（NotApplicable/NotRun 必填非空——ERR-01；缺检测器的
    /// DataInsufficient 也携带）。
    std::string reason;

    bool operator==(const KinChannelSampleRecord& o) const
    {
        return sampleIndex == o.sampleIndex && state == o.state
            && reason == o.reason;
    }
    bool operator!=(const KinChannelSampleRecord& o) const { return !(*this == o); }
};

/**
 * @brief 单口径覆盖计数投影（与域 CoverageTotals 同构——整数计数，
 *        无比率字段：覆盖率是计数比，百分比换算归呈现层且不引入第二
 *        口径——§7.2 computeCoverage 唯一实现点的产出直投）。
 */
struct KinChannelCoverageTotals {
    /// 分母＝计划样本总数（不可达/数据不足样本一律保留——R8）。
    std::uint64_t planned = 0;
    /// 分子＝Reached 样本数。
    std::uint64_t reached = 0;
    /// Unreachable 样本数。
    std::uint64_t unreachable = 0;
    /// DataInsufficient 样本数（>0 ⇒ 整体降级）。
    std::uint64_t dataInsufficient = 0;
    /// NotRun 样本数（>0 ⇒ 不产正式覆盖率）。
    std::uint64_t notRun = 0;
    /// NotApplicable 样本数（生成面不产，恒 0）。
    std::uint64_t notApplicable = 0;

    bool operator==(const KinChannelCoverageTotals& o) const
    {
        return planned == o.planned && reached == o.reached
            && unreachable == o.unreachable
            && dataInsufficient == o.dataInsufficient && notRun == o.notRun
            && notApplicable == o.notApplicable;
    }
    bool operator!=(const KinChannelCoverageTotals& o) const { return !(*this == o); }
};

/**
 * @brief 一次区域覆盖评估的结构化结果投影（结构化结果回路的出口值）。
 *
 * 消费面：①kinematics 覆盖面板（覆盖率结果呈现——L-K6"覆盖率呈现
 * （降级/零样本标识）"）；②F-495 消费卡（需求三维采样着色——逐样本
 * 状态→View3DCellState 映射）。账面纪律：装配层按 snapshotId+epoch
 * 锚定持有**最近一次**结果；消费方必须核对 snapshotId 与当前会话绑定
 * 一致——不一致＝跨快照误投影，消费方拒绝呈现（PA 权威纪律的呈现侧
 * 镜像：结果随快照换绑，非独立状态源）。
 *
 * 诚实边界：本值来自纯计算面（runRegionCoverageComputation）——零
 * evidence 对账轨/零 envelope（正式归档链留后续任务）；identityChecks
 * 恒空（对账在评估器 evaluate 面执行——本批不产对账记录）。
 */
struct KinChannelCoverageResult {
    /// 结果绑定快照内容身份（与请求.snapshotId 同值——消费核对键）。
    core::ContentIdentity snapshotId;
    /// 提交时纪元（迟到判定面）。
    std::uint64_t epoch = 0;
    /// 求解配置摘要（与请求.configDigest 同值——配置对齐核对键）。
    core::ContentIdentity configDigest;
    /// 执行成败（false＝执行失败——errorText 如实给出原因，samples/
    /// totals 无效不消费）。
    bool ok = false;
    /// 失败原因（ok=false 时非空——如切片缺失/装配校验拒绝；ERR-01）。
    std::string errorText;
    /// 逐样本结果（与样本集按 sampleIndex 双射对齐——分母完整性由域
    /// 纯计算面保证）。
    std::vector<KinChannelSampleRecord> samples;
    /// 位置覆盖率计数（存在性口径——∃≥1 有效解即达）。
    KinChannelCoverageTotals position;
    /// 姿态覆盖率计数（全局口径——(位置×姿态) 逐组合计数）。
    KinChannelCoverageTotals orientation;
    /// 位置覆盖率已定义（分母>0；false＝位置轴零样本——比率不存在）。
    bool positionDefined = false;
    /// 姿态覆盖率已定义（分母>0）。
    bool orientationDefined = false;
    /// 整体降级标记（数据不足样本>0 或任一轴零样本）。
    bool downgraded = false;
    /// 不完整标记（NotRun>0——partial 不产正式覆盖率）。
    bool incomplete = false;

    bool operator==(const KinChannelCoverageResult& o) const
    {
        return snapshotId == o.snapshotId && epoch == o.epoch
            && configDigest == o.configDigest && ok == o.ok
            && errorText == o.errorText && samples == o.samples
            && position == o.position && orientation == o.orientation
            && positionDefined == o.positionDefined
            && orientationDefined == o.orientationDefined
            && downgraded == o.downgraded && incomplete == o.incomplete;
    }
    bool operator!=(const KinChannelCoverageResult& o) const { return !(*this == o); }
};

// =====================================================================
// 通道聚合与缝别名（装配层一次注入——门面翻译进插件私有 KinPanelServices）
// =====================================================================

/// 任务点数据源缝（仅 UI 线程调用——面板每次刷新现调，零缓存）。
using KinChannelTaskPointsFn = std::function<std::vector<KinChannelTaskPointRow>()>;

/// 任务状态数据源缝（仅 UI 线程调用——从装配层任务账本投影）。
using KinChannelTaskRowsFn = std::function<std::vector<KinChannelTaskStatusRow>()>;

/// 后台提交缝（仅 UI 线程调用——面板流受理面；实现＝装配层执行器受理）。
using KinChannelBackgroundSubmitFn =
    std::function<KinChannelBackgroundAck(const KinChannelBackgroundRequest&)>;

/// 完成通知回投槽（实现方义务：保证回 UI 线程再消费——QMetaObject
/// ::invokeMethod 或等价机制；槽体消费＝面板状态行/会话态刷新）。
using KinChannelResultRouteFn =
    std::function<void(const KinChannelBackgroundResultNote&)>;

/**
 * @brief 覆盖评估执行通道装配值面（UI-T64——installAssemblyChannels
 *        的一次性注入聚合）。
 *
 * 所有权：指针缝全部非 owning（调用方保证存活期覆盖面板使用期——
 * §9.4 注入面同口径）；function 缝由本结构值持有。任一缝缺省＝对应
 * 能力降级（按钮禁用/空态如实呈现，不虚构可用性）——与面板私有
 * KinPanelServices 的空缝语义逐字一致（翻译后行为不变）。
 *
 * 本批范围声明：仅 modelView/taskPoints/taskRows/backgroundSubmit/
 * resultRoute 五缝构成覆盖评估执行通道；commandHandler/sessionPose/
 * exportWriter/configPersist/ikSolver/fkEvaluator 不在本通道（维持
 * 降级语义——契约诚实边界）。
 */
struct KinematicsAssemblyChannels {
    /// 模型只读视图（评估的模型输入——KinRuntimeSnapshotView 薄壳的
    /// 地址；空＝四面板呈空态〔面板私有缝同款语义〕）。
    const IKinRuntimeView* modelView = nullptr;
    /// 任务点数据源（requirements 工作集 points 投影闭包）。
    KinChannelTaskPointsFn taskPoints;
    /// 任务状态数据源（装配层任务账本投影闭包）。
    KinChannelTaskRowsFn taskRows;
    /// 后台提交缝（执行器受理面——覆盖评估/批量验证两 kind）。
    KinChannelBackgroundSubmitFn backgroundSubmit;
    /// 完成通知回投槽（执行器任务终态投递——回 UI 线程义务在实现方）。
    KinChannelResultRouteFn resultRoute;
};

}  // namespace kinematics
}  // namespace ird
}  // namespace sdurws

#endif  // IRD_KINEMATICS_ASSEMBLY_KINEMATICSPANELCHANNELS_HPP
