/**
 * @file   KinPanelTypes.hpp
 * @brief  kinematics 插件面板的零 Qt 值类型——四面板行集投影、会话态、
 *         后台执行缝的请求/回执值（卡 §9.8 面板表与十二条数据流的承载值）。
 *
 * 设计依据：
 *   - units/kinematics.md §9.8（插件界面设计：四面板＋求解配置高级面板、
 *     线程与刷新约束"面板不缓存权威结果——消费 results 归档与会话对象"）；
 *   - units/modeling.md §9.7 先例（PanelModel 五区投影的"零 Qt 呈现模型"
 *     分层——WP-13-T15 落位形态，本头同构）；P-KIN-7 处置：全部对端契约
 *     以已落位的 ui/kinematics 公共头为承载，本头不自造对端类型；
 *   - 需求 UX-04/UX-05；任务契约 tasks/foundation/WP-15-T12.json
 *     acceptance 1~4（四面板落位/插件零计算逻辑/域命令注册/消费面核验）。
 *
 * 背景说明（为什么行集是"文本已投影"形态）：呈现行的数值文本一律经
 * kinematics::DisplayUnitProjection（KIN-12 唯一投影出口）或 ui 表单公共
 * 件（formatFieldValueText）在行构建期换算——widget 只渲染文本不做任何
 * 换算/排序/筛选算术（acceptance 2"插件零计算逻辑"的行集半区：排序唯一
 * 经 IKinematicSolutionSet 视图、筛选唯一经规范谓词工厂、换算唯一经
 * DisplayUnitProjection，三者的产出在本层只是"搬运成行"）。
 *
 * 线程约束：全部值类型纯值（并发只读安全）；KinModuleSessionState 非线程
 * 安全——仅 UI 线程访问（§9.4 会话对象行同口径）。
 */

#ifndef IRD_KINEMATICS_PLUGIN_KINPANELTYPES_HPP
#define IRD_KINEMATICS_PLUGIN_KINPANELTYPES_HPP

#include <array>
#include <cstdint>
#include <sdurws/ird/ui/View3DPreviewContract.hpp> // 只读三维呈现值；无Qt依赖。
#include <sdurws/ird/kinematics/Render.hpp> // 域诊断渲染值，阈值来源唯一。
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <rw/math/Transform3D.hpp>              // 后台请求目标位姿值（KIN-P4 表尾追加）

#include <sdurws/ird/core/Digest.hpp>       // core::ContentIdentity（绑定身份）
#include <sdurws/ird/core/Identity.hpp>     // core::ObjectId（点/关节锚）
#include <sdurws/ird/kinematics/AnalysisConfig.hpp>  // AnalysisConfiguration（求解配置值）
#include <sdurws/ird/kinematics/Commands.hpp>        // KinSessionPose（会话姿态唯一写点）
#include <sdurws/ird/kinematics/Ik.hpp>              // IIkSolver（单点求解消费面）
#include <sdurws/ird/kinematics/SolutionSet.hpp>     // IKinematicSolutionSet（会话解集视图）
#include <sdurws/ird/kinematics/Workspace.hpp>       // WorkspaceSamplingMode/WorkspaceSampleRun（★ KIN-R2 任务3——探索式采样面板值的域载体）

class QObject;  // 前置声明（★ G3 异步内联求解缝的回投 context——Qt 对象；仅指针形态）

namespace sdurws {
namespace ird {
namespace kinematics {

class IFkEvaluator;       // 前置声明（位姿指标消费面——Fk.hpp 完整类型仅实现侧 TU 需要）
class IKinRuntimeView;    // 前置声明（模型只读视图——KinTypes.hpp）
class IKinematicsCommandHandler;  // 前置声明（设默认门面——Commands.hpp）

// =====================================================================
// 行集投影值（四面板的呈现行——widget 渲染的唯一输入形态）
// =====================================================================

/**
 * @brief 命名数值行（位姿指标面板的指标行——FK 位姿汇总/奇异值/条件数/
 *        可操作度/关节裕量逐项一行）。
 *
 * displayText 已按当前显示单位投影（KIN-12——构建期经 DisplayUnitProjection
 * 换算完成）；siValue 恒为 SI 真值（权威值——投影不篡改权威，V-17）。
 * 值语义纯结构；线程安全。
 */
struct KinNamedValueRow {
    /// 行稳定键（面板行定位锚——小写连字符词形，与 §6.3 token 同风格）。
    std::string key;
    /// 中文标签（工程用语——UX-02）。
    std::string label;
    /// SI 真值（权威值；无量纲量同值承载——单位语义见 unitToken）。
    double siValue = 0.0;
    /// 显示文本（已投影＋单位同显——"300 mm"形态；widget 零换算）。
    std::string displayText;
    /// 单位 token（core Units 词表——"m"/"rad"/"1"；呈现与测试断言锚）。
    std::string unitToken;
};

/**
 * @brief 任务点消费视图行（任务点验证面板——只读投影＋结果列）。
 *
 * ★ 真值边界（acceptance 4）：任务点定义的真值归 requirements 单元——
 * 本行是宿主装配层从 req.points 切片投影来的**只读消费行**，插件零写回
 * 入口（结构上无 setter、无编辑路径——消费面核验的静态半区）；结果列
 * （outcomeState/outcomeText）来自批量计算的 per-item 状态投影。
 * requiredCoverageDone＝必验覆盖完成标记列（§7.1 完成矩阵素材的呈现位）。
 * 值语义纯结构；线程安全。
 */
struct KinTaskPointRow {
    /// 任务点对象身份（宿主投影的锚——面板选中联动的定位键）。
    core::ObjectId pointOid;
    /// 显示标签（宿主投影的工程用语名——UX-02）。
    std::string label;
    /// 是否启用（停用点仍在表中——批量通道以 NotApplicable 显式标记，
    /// ERR-01；表内如实呈现不停删）。
    bool enabled = true;
    /// 结果状态词（批量 per-item 状态投影——BatchItemStatus 的统一状态词
    /// 投影（Render.hpp renderTaskStateToken 同源词表）；nullopt＝未计算）。
    std::optional<std::string> outcomeState;
    /// 结果摘要文本（已投影——如"3 解/残差 0.2 mm"；nullopt＝未计算）。
    std::optional<std::string> outcomeText;
    /// 必验覆盖完成标记（true＝该点全部必验工况有计算终态——完成矩阵列）。
    std::optional<bool> requiredCoverageDone;
};

/**
 * @brief 区域覆盖结果行（区域覆盖面板——位置/姿态分别呈现＋降级标识）。
 *
 * ratioText 已按"分子/分母"整数计数投影（覆盖率是计数比——不引入浮点
 * 百分比换算的第二口径，§7.2 覆盖率算法唯一实现点 computeCoverage 的
 * 产出值由调用方格式化）；degraded＝数据不足降级标识（分母含未冻结样本
 * ——如实降级不冒充完整）；zeroSamples＝零样本提示（DataInsufficient
 * 呈现——不虚构 0%）。
 */
struct KinCoverageRow {
    /// 行稳定键（区域/方向组定位锚）。
    std::string key;
    /// 显示标签（区域名/方向组名——工程用语）。
    std::string label;
    /// 覆盖比文本（"60/100"形态——计数比直投，零第二口径）。
    std::string ratioText;
    /// 数据不足降级标识（true＝行尾"数据不足"标记——KIN-04 降级语义）。
    bool degraded = false;
    /// 零样本提示（true＝"零样本"标记——覆盖率无意义不虚构）。
    bool zeroSamples = false;
};

/**
 * @brief 解行（结果与可视化面板的解表行——IKinematicSolutionSet sorted()
 *        稳定序的逐解投影）。
 *
 * rank 即稳定序位置（KIN-02 四键排序的视图产出——面板零自排序，
 * acceptance 2"排序全部消费域设施"）；数值文本已按显示单位投影。
 * collisionToken＝碰撞标记词（"无"/"碰撞"/"未评价"——CollisionStatus
 * 三态直投，KIN-05：未评价绝不呈现为"无碰撞"）。
 */
struct KinSolutionRow {
    /// 稳定序位置（0 基——sorted() 序下标；无量纲）。
    std::uint64_t rank = 0;
    /// 构型签名（记录键——I-KIN-3；同位姿异构型的呈现区分锚）。
    std::string signature;
    /// 关节向量文本（已按显示单位投影——deg/mm 制式；逗号分隔）。
    std::string qText;
    /// 最小关节裕量显示文本（无量纲——已格式化）。
    std::string minJointMarginText;
    /// 条件数显示文本（无量纲；+∞ 承载为文本——不截断）。
    std::string conditionNumberText;
    /// 位置残差显示文本（长度单位投影）。
    std::string positionResidualText;
    /// 碰撞标记词（三态直投——见结构注）。
    std::string collisionToken;
};

/**
 * @brief 任务状态行（L-K12 任务状态投影的零 Qt 承载——从 ui
 *        ITaskPresentationModel 的 TaskRow 投影而来，投影动作在 Qt 层）。
 *
 * stateLabelKey 原样透传 ui 词表键（含"已中断"——NFR-REL-03：中断如实
 * 呈现，插件零改写零美化）；percent 为已合并展示进度（回退值不会出现
 * ——ui §9.4 三规则已在模型内裁定，本行只搬运）。
 */
struct KinTaskStatusRow {
    /// 任务标识文本（呈现用——ui TaskRow 快照的标识投影）。
    std::string taskRefText;
    /// 九态短标签文案键（ui 词表——taskStateLabelKey 产出值原样透传）。
    std::string stateLabelKey;
    /// 是否中断态（呈现侧加粗/着色的语义锚——值权威仍在 stateLabelKey）。
    bool interrupted = false;
    /// 展示进度（0~100；nullopt＝该态无进度语义）。
    std::optional<int> percent;
};

// =====================================================================
// 后台执行缝（P-KIN-7 最小端口口径——execution 通道归装配层）
// =====================================================================

/**
 * @brief 后台执行请求类别（§9.8 线程约束">1 s 全部转 execution"的三类
 *        请求面）。
 */
enum class KinBackgroundKind : std::uint8_t {
    /// 会话级单点求解超内联预算转后台（L-K2 超界分支——不写 results）。
    SessionSolve,
    /// 批量任务点验证提交（L-K3——正式评估，写 results）。
    TaskPointsBatch,
    /// 区域覆盖评估提交（L-K6——正式评估，写 results）。
    RegionCoverage,
};

/**
 * @brief 后台执行请求值（面板→装配层缝的提交载荷）。
 *
 * 载荷刻意最小（P-KIN-7：真实 execution 任务注册/检查点/归档协议归装配
 * 层——面板只表达"哪类评估、绑哪个快照与配置"）：批量点集/覆盖计划等
 * 切片细节由装配层从 requirements 切片与计划构造（面板零切片解析——R-1）。
 * epoch＝提交时会话纪元（完成回执比对用——迟到结果不进当前会话）。
 *
 * ★ KIN-P4 表尾追加 solveTargetInBase（与通道值面 KinChannelBackground-
 * Request.solveTargetInBase 同构同拍——翻译单点逐字段透传）：SessionSolve
 * 后台通道的目标位姿载荷。修复前最小载荷不含目标——后台执行器无法还原
 * "求解什么"，SessionSolve 通道只能整体拒绝（审查报告 §4 第二条的根因
 * 半区；另一半在执行器）。缺省 nullopt＝请求缺目标（执行器如实拒绝）；
 * 仅 SessionSolve 消费，另两类 kind 忽略。基座系 {B} TCP 目标（m——§5.3）。
 */
struct KinBackgroundRequest {
    /// 请求类别（三值词表——见枚举注）。
    KinBackgroundKind kind = KinBackgroundKind::TaskPointsBatch;
    /// 结果绑定快照内容身份（§5.6 绑定六要素之一——装配层校验对齐面）。
    core::ContentIdentity snapshotId;
    /// 求解配置摘要（config.ik——AnalysisConfig.hpp analysisConfigurationDigest
    /// 产出；入结果身份，装配层据此对齐评估请求）。
    core::ContentIdentity configDigest;
    /// 提交时会话纪元（迟到判定锚——面板模块现值，见 KinModuleSessionState）。
    std::uint64_t epoch = 0;
    /// 单点求解目标位姿（KIN-P4 表尾追加——仅 SessionSolve 消费）。
    std::optional<rw::math::Transform3D<double>> solveTargetInBase;
};

/**
 * @brief 后台提交回执（缝的受理语义——accepted=false 时 reason 如实给出
 *        拒绝原因，如"执行通道未装配"——不虚构已提交，ERR-01）。
 */
struct KinBackgroundAck {
    /// 是否受理（true＝装配层已接收并转 execution——进度经任务投影刷新）。
    bool accepted = false;
    /// 拒绝/受理说明（呈现于面板状态行——如实文本）。
    std::string reason;
    /// 任务引用文本（受理时装配层回填——任务投影行的对齐键；可空）。
    std::string taskRef;
};

/**
 * @brief 后台完成结果的通知值（装配层在任务归档/终止后经
 *        noteBackgroundResult 投递——L-K12 迟到判定与中断如实呈现）。
 *
 * acceptedEpoch 用于迟到比对：面板模块现纪元≠acceptedEpoch（期间发生过
 * 会话切换/新提交推进纪元）→结果丢弃并给状态反馈，不进当前会话视图。
 */
struct KinBackgroundResultNote {
    /// 提交时纪元（与请求.epoch 同值——装配层原样回传）。
    std::uint64_t acceptedEpoch = 0;
    /// 类别（与请求.kind 同值）。
    KinBackgroundKind kind = KinBackgroundKind::TaskPointsBatch;
    /// 是否中断/取消终态（true＝"已中断"如实呈现——NFR-REL-03）。
    bool interrupted = false;
    /// 结果摘要文本（已投影——如"42/60 点可达"；呈现于状态行）。
    std::string summaryText;
};

// =====================================================================
// 会话态与注入服务缝（面板数据的权威落点——零缓存纪律的边界面）
// =====================================================================

/**
 * @brief 任务点数据源缝（只读提供器——宿主装配层从 requirements 切片
 *        现取投影；面板零缓存，每次刷新现调）。
 */
using KinTaskPointsProvider = std::function<std::vector<KinTaskPointRow>()>;

/**
 * @brief 任务状态数据源缝（L-K12——Qt 层从 ui ITaskPresentationModel 现取
 *        TaskRow 投影为 KinTaskStatusRow 后注入；空向量＝无在途/历史任务）。
 */
using KinTaskRowsProvider = std::function<std::vector<KinTaskStatusRow>()>;

/**
 * @brief 后台提交缝（P-KIN-7 最小端口——真实 execution 提交协议归装配层；
 *        harness/测试注入脚本化替身）。
 */
using KinBackgroundSubmitFn = std::function<KinBackgroundAck(const KinBackgroundRequest&)>;

/**
 * @brief 导出写出缝（io 通道投影——写出经 io AtomicFile 归装配层适配；
 *        参数＝目标路径规范文本＋编码完成的内容文本；false＝写出失败，
 *        面板给状态反馈且旧文件语义由 io 层保证）。
 */
using KinExportWriterFn = std::function<bool(const std::string& targetPath,
                                             const std::string& content)>;

/**
 * @brief 用户级配置保存缝（PM-14 通道投影——P-KIN-4：存储载体归 ui 侧，
 *        接口待 ui 卡冻结；未装配＝配置保存禁用并如实提示，编辑/提示流
 *        不受影响）。
 */
using KinConfigPersistFn = std::function<bool(const AnalysisConfiguration&)>;

/**
 * @brief 命令提交出口（装配层注入——绑定 ui ICommandRegistry.submit；
 *        未注入＝写类/会话类命令按钮禁用，不虚构可达性——modeling 先例
 *        CommandSubmitFn 同形态）。
 */
using KinCommandSubmitFn = std::function<void(const std::string& commandId)>;

/**
 * @brief 具名 Home 姿态来源缝（KIN-P3/审查 A09——复位 Home 的唯一目标
 *        来源；"经名称/端口解析"的端口半区）。
 *
 * 语义：返回**具名 Home 构型**（rad|m 链序 SI 真值）——装配层从模型/
 * 会话定义的命名位姿集解析（建模侧保留键 homeConfiguration——MDL-17/
 * V-27；本单元不解析位姿集对象，units/kinematics.md §14.6 v0.8 登记
 * 口径），经端口投影为关节向量。返回值三态：
 *   - 有值＝Home 在场（复位 Home 以该值为目标覆写会话姿态——零修订）；
 *   - nullopt＝模型/会话未定义 Home（面板给明确提示，绝不写回）；
 *   - 缝为空（bool(f)==false）＝来源未装配（按钮显式禁用——能力降级
 *     如实呈现，与其他缝同纪律）。
 *
 * ★ 审查 A09 根因与修复口径：修复前 widget 复位 Home 槽把当前会话姿态
 * 原样写回（读 q → reset(q)——值不变的假成功，无目标来源语义）。修复
 * 后 Home 只能来自本缝（具名来源）；来源缺失＝显式禁用/明确提示，绝
 * 不静默把当前姿态充当 Home（KIN-06/AT-04：会话回写零修订的语义前提
 * 是"有明确目标"——无目标写回既非复位也不可追溯）。纯查询（幂等无副
 * 作用）——面板每次点击现调零缓存（§9.8"面板不缓存权威"同则）。
 *
 * 线程约束：仅 UI 线程调用（§3.4——缝面同则）。
 */
using KinHomePoseProvider = std::function<std::optional<std::vector<double>>()>;

/**
 * @brief 探索式采样面板参数（★ KIN-R2 任务3/KIN-09——采样模式编辑入口
 *        的零 Qt 值面；widget 编辑字段 → 域 WorkspaceSamplingQuery 的
 *        装配前置值）。
 *
 * 字段语义（全部 SI 真值——显示制式换算在 widget 提交面完成，与目标
 * 编辑器同纪律 KIN-12）：
 *   - mode 三值（区域盒 IK 采样/关节随机 FK 采样/关节网格 FK 采样——
 *     WorkspaceSamplingMode 词表，域权威语义随枚举注）；
 *   - 区域模式消费 regionCenter/regionSize（基座系 {B} 轴对齐盒，单位
 *     m；size 三分量须 >0）与位置采样定义（随机＝positionRandom＋
 *     regionRandomCount / 网格＝regionGridCounts 三轴计数）；姿态定义
 *     域面退化单方向单 roll（KIN-09 主径为位置点云——面板不暴露姿态
 *     采样编辑，登记为呈现批次边界）；
 *   - 关节模式消费 jointRandomCount（随机样本数）与 jointGridPerAxis
 *     （网格逐轴统一取点数——面板以单一数值施加全轴，逐轴异值编辑留
 *     后续批次；域面仍收 per-axis 向量，此处仅是 UI 参数化子集）。
 *
 * 值语义纯结构；线程安全。
 */
struct KinWorkspaceSamplingParams {
    /// 采样模式（三值词表——缺省区域盒保持旧径行为）。
    WorkspaceSamplingMode mode = WorkspaceSamplingMode::CartesianRegion;
    /// 区域盒中心（基座系 {B}，单位 m——仅区域模式消费）。
    double regionCenterX = 0.0;
    double regionCenterY = 0.0;
    double regionCenterZ = 0.0;
    /// 区域盒三边长（基座系 {B}，单位 m；须 >0 有限——RegionBox 契约）。
    double regionSizeX = 0.2;
    double regionSizeY = 0.2;
    double regionSizeZ = 0.2;
    /// 区域模式位置采样：true＝随机均匀（randomCount 撒点）、false＝体
    /// 心网格（gridCounts 三轴计数——§7.2 两方法探索式直用）。
    bool positionRandom = true;
    /// 区域随机样本数（无量纲；0＝零样本合法——与域零样本口径一致）。
    std::uint32_t regionRandomCount = 64;
    /// 区域网格逐轴计数（x/y/z 链序；0 计数轴＝零样本合法——域校验面）。
    std::array<std::uint32_t, 3> regionGridCounts{2, 2, 2};
    /// 关节空间随机样本数（无量纲——mode=JointSpaceRandom 消费）。
    std::uint32_t jointRandomCount = 64;
    /// 关节空间网格逐轴取点数（无量纲、每轴 ≥1——mode=JointSpaceGrid
    /// 消费；面板以统一值施加全部自由度——域面收 per-axis 向量）。
    std::uint32_t jointGridPerAxis = 2;
};

/**
 * @brief 面板服务缝聚合（构造注入——装配层/harness/测试的一次性接线点）。
 *
 * 所有权：全部指针/缝为非 owning（调用方保证存活期覆盖面板使用期——
 * §9.4 注入面同口径）；任一缝为空＝对应能力降级（按钮禁用/缺省态呈现），
 * **不虚构可用性**（modeling 先例 EditTargetProvider 空语义同案）。
 *
 * 线程约束：仅 UI 线程访问（§3.4）。
 */
struct KinPanelServices {
    /// 模型只读视图（位姿指标/单点求解的模型输入；空＝四面板呈空态）。
    const IKinRuntimeView* modelView = nullptr;
    /// 单点求解器（L-K2 内联执行面——产品注入 IkSolver，测试注入替身）。
    const IIkSolver* ikSolver = nullptr;
    /// 位姿指标评估器（位姿指标面板——产品注入 FkEvaluator）。
    const IFkEvaluator* fkEvaluator = nullptr;
    /// 设默认门面（L-K9——Commands.hpp IKinematicsCommandHandler；空＝
    /// 设默认命令禁用）。
    IKinematicsCommandHandler* commandHandler = nullptr;
    /// 会话姿态（L-K4/L-K5——宿主持有的 KinSessionPose 唯一写点；空＝
    /// 会话姿态区禁用）。
    KinSessionPose* sessionPose = nullptr;
    /// 任务点数据源（任务点表只读消费——空＝表呈空态）。
    KinTaskPointsProvider taskPoints;
    /// 任务状态数据源（L-K12——空＝任务区呈空态）。
    KinTaskRowsProvider taskRows;
    /// 后台提交缝（L-K2 超界/L-K3/L-K6——空＝提交按钮禁用＋如实提示）。
    KinBackgroundSubmitFn backgroundSubmit;
    /// 导出写出缝（L-K10——空＝导出按钮禁用）。
    KinExportWriterFn exportWriter;
    /// 用户级配置保存缝（L-K7——空＝保存禁用，编辑与提示不受影响）。
    KinConfigPersistFn configPersist;
    /// 命令提交出口（域命令激活的统一出口——空＝命令按钮禁用）。
    KinCommandSubmitFn commandSubmit;
    /// 具名 Home 姿态来源（L-K4 复位 Home 唯一目标来源——KIN-P3/审查
    /// A09；空＝复位 Home 按钮禁用，绝不以当前姿态充当 Home）。
    KinHomePoseProvider homePose;
    /// 内联求解异步执行缝（★ 审查 G3——WP-15-T20 第三批；插件零自建
    /// 线程红线〔acceptance 2〕的通道化落位：异步能力由装配层线程设施
    /// 承载——job 在装配层工作线程恰执行一次，完成后经 context 对象队列
    /// 回投 onDone 于 UI 线程，context 析构＝回调弃置）。空＝回退同步
    /// 内联（runSinglePointSolve 原径——诚实降级）。
    std::function<void(std::function<IkOutcome()>,
                       std::function<void(IkOutcome)>,
                       const QObject*)> inlineSolveAsync;
    /// KIN-09：宿主执行job一次，再在context存活时于UI线程调用onDone。
    /// job只访问冻结值与独占临时会话，须自行将异常转换为回执；空缝保留同步诊断路径。
    std::function<void(std::function<void()>, std::function<void()>,
                       const QObject*)> workspaceAsync;
    /// UI线程三维投放（世界系m）及对象高亮；false代表宿主呈现设施不可用。
    std::function<bool(const ui::View3DPreviewUpdate&,
                       const std::vector<core::ObjectId>&)> previewSink;
    /// 已发布policy阈值投影；缺省＝显式不适用，禁止面板发明阈值。
    std::function<WeakZoneThresholds()> weakThresholds;
    /// 同快照批量渲染事实；空表示尚无有效产物。
    std::function<std::optional<BatchRenderData>()> batchRender;
};

/**
 * @brief 插件模块会话态（会话权威态本身——非投影副本；modeling 先例
 *        ModuleSessionState 同案：工作集/解集的唯一权威载体在此，面板
 *        每次投影经它现取）。
 *
 * "面板不缓存权威结果"（§9.8）的边界说明：lastSessionSolutionSet 持有的是
 * **会话级解集会话对象本身**（单点求解的会话产物——未归档、不进 results），
 * 与"面板 widget 不另存副本"是同一纪律的两半：权威在会话态，视图零缓存。
 *
 * 线程约束：非线程安全——仅 UI 线程访问（§9.4）。
 */
struct KinModuleSessionState {
    /// 结果绑定快照内容身份（§5.6 绑定六要素——会话建立时由装配层注入
    /// 的会话事实；面板零自造身份）。
    core::ContentIdentity snapshotId;
    /// 当前显示单位投影（KIN-12——R1 冻结子集；nullopt＝未配置即缺省
    /// SI 制式直显）。
    std::optional<DisplayUnitProjection> displayUnits;
    /// 会话级解集原始值（最近一次单点求解的域产出——nullopt＝尚未求解）。
    /// 持有 IkSolutionSet 原值：导出打包（buildKinExportPackage 族）与
    /// 结果绑定（requestIdentity）消费具体值面。
    std::shared_ptr<const IkSolutionSet> lastSessionSolutionSet;
    /// 会话级解集视图（构造时完成一次四键稳定排序——解表/检查器/渲染
    /// 数据的消费面；与原值同批构造，双面同源零二次求解）。
    std::shared_ptr<const KinematicSolutionSet> lastSessionSolutionSetView;
    /// 当前已保存的用户级求解配置（L-K7 的 before 侧——编辑提示的比较
    /// 基线；**会话纪律：必须为合法配置**——装配层注入或保存流更新后
    /// 恒过 validateAnalysisConfiguration，编排函数对其摘要计算零防御）。
    AnalysisConfiguration savedConfig{};
    /// 最近一次探索式采样运行产物（★ KIN-R2 任务3/KIN-09——结果与可视
    /// 化面板点云/导出/回写的数据源；nullopt＝尚未采样。持有域冻结值
    /// WorkspaceSampleRun 本体（会话权威态——面板投影经
    /// buildWorkspacePointCloud 现取零缓存）；R2 边界：探索式产物不入
    /// results 归档/证据通道（结构上无覆盖率字段——域类型级保证），
    /// 快照换绑时随会话整体失效（attachSnapshot 账面清空——迟到/跨
    /// 快照误投影由呈现侧以会话锚纪律排除）。
    std::shared_ptr<const WorkspaceSampleRun> lastWorkspaceRun;
    /// 会话纪元（单调递增——后台提交时随请求携带，完成回执比对；
    /// 会话切换/新提交推进该值）。
    std::uint64_t epoch = 1;
    /// 会话可写性（L-K11 只读门控输入——ui 会话投影同源事实）。
    bool writable = true;
};

}  // namespace kinematics
}  // namespace ird
}  // namespace sdurws

#endif  // IRD_KINEMATICS_PLUGIN_KINPANELTYPES_HPP
