/**
 * @file   Workspace.hpp
 * @brief  工作空间探索式采样与指标可视化浏览（KIN-09/R2）——探索式采样
 *         查询值、逐样本评估结果（解级指标投影）、采样运行产物、点云
 *         渲染数据（七维着色/三面投影/图例/导出值行）与可视化点会话
 *         回写的**数据与计算面**。
 *
 * 设计依据：
 *   - units/kinematics.md §7.3（KIN-09/10/11 R2 边界——探索式工作空间
 *     采样是 Quick/估算性质，其结果**不得**进入 Must 区域判定或必验
 *     覆盖，§7.2 冻结样本集是唯一正式口径；接口在 §11 T14~T16 预留，
 *     复用 §7.2 方向集生成器与解集设施，R1 不留实现桩——本头即该预留
 *     边界的 T14 兑现）、§7.2（采样黄金设施复用面——splitmix64 派生/
 *     网格体心/斐波那契螺旋方向集，D-KIN-6 黄金锁定）、§9.8（结果与
 *     可视化面板——"渲染数据本单元产、呈现归 ui View3D"分工；L-K4
 *     可视化点回写行）、§4.4/§4.5（AnalysisConfiguration 求解配置
 *     KIN-13；可视化着色模式＝ui 呈现偏好不入身份）、§8.3/§8.4
 *     （协作取消/检查点/进度/分批的执行协作语义）、§10.2（V-11/22
 *     取消口径、V-27 设施失败值面）、§9.6（诊断码纪律——探索式通道
 *     不产新 KIN-* 码，校验面走 fail-fast，素材面走状态/原因文本）
 *   - REQUIREMENTS KIN-09（P1/R2/D——随机均匀/关节网格采样（样本数/
 *     种子/网格步数可配，可取消、有进度；碰撞证据按统一工程策略配置
 *     ——经工程策略入口修改〔UX-08/ARC-05〕，不另存第二布尔值）；结果
 *     点云按状态/可操作度/条件数/最小余量/位置误差/姿态误差/碰撞等着
 *     色，支持 XY/XZ/YZ 投影、散点/包络渲染、标签/图例与 PNG 导出；
 *     可视化点可回写三维场景（仅改会话姿态，遵循 KIN-06））、AT-25
 *     （采样可取消有进度；着色/投影/图例/PNG 导出；点回写仅改会话
 *     姿态）、KIN-06/AT-04（回写只改会话姿态，不修改设计模型、不触发
 *     结果失效）、UX-08/ARC-05（碰撞统一策略——本查询无碰撞布尔开关，
 *     会话在场即启用）、V13-01（同则）
 *   - 任务契约 tasks/foundation/WP-15-T14.json acceptance 1~6 全条
 *
 * 背景说明（第一读者须知——KIN-09 与 KIN-04 的分立，§2.5 旧代码对照
 * 表"分期承接"行的语义落值）：两者共享 §7.2 采样与逐样本评估设施，
 * 但**通道性质相反**——
 *   - KIN-04 区域覆盖（Sampling.hpp/Evaluators.cpp WorkspaceSampler）：
 *     **正式通道**——requirements 拥有采样计划、样本集经
 *     sampleSetIdentity 对快照 SamplingPlanRef 冻结对账、产物是覆盖率
 *     证据（进 evidence 汇总、可作 Must 判定素材）；
 *   - KIN-09 工作空间采样（本头）：**探索式通道**——无 requirements
 *     计划（参数由用户在面板直接给出）、无冻结对账（计划身份仅作会话
 *     内溯源回显）、**零覆盖率产物**（WorkspaceSampleRun 结构上没有
 *     CoverageResult 字段——R2 边界"结果不进 Must 区域判定或必验覆盖"
 *     的结构性保证，而非纪律约定）、不进 evidence 评估器/汇总通道
 *     （结果不入 results 归档，呈现于结果与可视化面板）。
 *   因此本头**不**实现 IEngineeringEvaluator/评估器工厂/descriptor
 *   ——探索式采样不是 evidence 词表注册的评估通道（§12 R2 承接行
 *   "PA-7 无新机制"：复用采样/解集/方向集设施＋execution 检查点，
 *   不新增评估器族）。
 *
 * "样本数/种子/网格步数经 AnalysisConfiguration 配置（KIN-13 语义）"
 * 的落位口径（契约 acceptance 1；登记随卡 §14.6 v0.20）：种子的权威
 * 载体＝AnalysisConfiguration（regionBudget.seed＋seed 两槽位——I-KIN-4
 * 的 seed=0 非法校验语义经 validateAnalysisConfiguration 与
 * validateWorkspaceSamplingQuery 双面承载，绝不做 0→1 静默替换）；
 * 容差/迭代上限/初值策略/去重阈值同经 AnalysisConfiguration 直投查询。
 * 样本数/网格步数属探索式采样**定义**（§4.4 schema 的明文字段清单无
 * 此二者——扩 schema 须升 canonical 版本并触动 configDigest 全链身份，
 * 不属本卡义务；KIN-04 先例同构：样本计数在采样定义面
 * （PositionSamplingDefinition）而非配置面），经 WorkspaceSamplingQuery
 * 的采样定义字段承载——"可配"语义由面板编辑面（§9.8 求解配置高级
 * 面板）保证。此落点偏差（"经 AnalysisConfiguration 配置"实现为"种子
 * 经配置、计数/步数经采样定义"）按 DTB §5.4 随卡 §14.6 登记。
 *
 * 红线自查锚点（review 对照）：
 *   - R2 边界（acceptance 5）：WorkspaceSampleRun 无覆盖率/无证据行/
 *     无 verdictInputs 字段——探索式样本集身份仅作会话内溯源回显
 *     （workspaceSamplingPlan 注），不对账任何快照 SamplingPlanRef；
 *   - 碰撞统一策略（acceptance 5/V13-01）：查询面只有
 *     collisionSession 指针（在场即启用——装配层自已发布策略注入），
 *     **零碰撞布尔字段**；缺会话而采样定义无碰撞要求＝检查不在范围
 *     （collisionNotEvaluated 如实标记，绝不解读为无碰撞——KIN-05）；
 *   - 回写仅会话姿态（acceptance 4/KIN-06）：workspacePointWriteback
 *     产出 RenderPointWriteback（Render.hpp——结构性只有关节向量＋
 *     构型签名两成员），消费口＝writebackToSessionPose（KinSessionPose
 *     唯一写点）——零修订/零失效/不入缓存身份；
 *   - L2 零 Qt（NFR-MNT-01）：本头纯值＋纯函数＋标准库/rw::math/
 *     core/evidence 头，零任何绘图设施（PNG 文件生成归 ui View3D/
 *     绘图契约——本单元交付投影/图例/导出值行数据面，REQUIREMENTS
 *     附录 A"视图截图 PNG＝RWStudioView3D.cpp"分工同源）；
 *   - R-4 零名称拼接：本头零显示名字段——对象标注一律 ObjectId；
 *   - 包络边界（KIN-10 分工）：WorkspaceRenderStyle::Envelope 只是
 *     渲染模式词表的承载（ui 呈现偏好）——包络几何估算（射线 Rmax）
 *     归 KIN-10/WP-15-T15，本头不提前实现（§7.3"包络≠样本全覆盖"
 *     与卡面 §11 T15 行分工）。
 *
 * 线程安全：本头全部实体为纯值/纯函数/无状态服务（无共享可变状态）；
 * runWorkspaceSampling 内部并行分片同 T05/T06 口径（各线程只写全序
 * 槽位）。确定性：同 (query, context 行为) 同样本集同序同结果（D-KIN-6
 * 复用＋全序归并）；协作取消经 context（evidence::IEvaluationContext
 * ——EX-T03 取消协议与 EX-T05 进度分发的直接消费面，P-KIN-7 处置
 * 同款：宿主注入，本单元不注册任务）。
 */

#ifndef IRD_KINEMATICS_WORKSPACE_HPP
#define IRD_KINEMATICS_WORKSPACE_HPP

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <rw/math/Transform3D.hpp>
#include <rw/math/Vector3D.hpp>

#include <sdurws/ird/core/Digest.hpp>          // ContentIdentity（计划身份/溯源回显）
#include <sdurws/ird/core/Identity.hpp>        // ObjectId（对象标注唯一形态——R-4）
#include <sdurws/ird/evidence/Evaluator.hpp>   // IEvaluationContext（协作取消/进度——EX-T03/05）
#include <sdurws/ird/kinematics/AnalysisConfig.hpp>  // AnalysisConfiguration（KIN-13 配置直投）
#include <sdurws/ird/kinematics/Evidence.hpp>  // IBatchCheckpointSink（批 watermark 最小端口——EX-T08）
#include <sdurws/ird/kinematics/Ik.hpp>        // IIkSolver/IkOutcome/JointInterval（逐样本求解复用面）
#include <sdurws/ird/kinematics/KinTypes.hpp>  // IKinRuntimeView/TcpRef/KinematicSolution/CollisionStatus
#include <sdurws/ird/kinematics/Render.hpp>    // RenderPointWriteback（回写值——KIN-06 出口复用）
#include <sdurws/ird/kinematics/Sampling.hpp>  // SamplingPlan/RegionBox/PositionSamplingDefinition/
                                               //   OrientationSamplingDefinition/RegionSamplingBudget/
                                               //   SampleSet/SampleState/IkOutcomeKind 投影（§7.2 黄金设施）
#include <sdurws/ird/runtime/Description.hpp>  // runtime::CanonicalModel（模型视图消费）
#include <sdurws/ird/runtime/Errors.hpp>       // runtime::Expected（非抛出查询轨载体——T04 先例）

namespace sdurws::ird::kinematics {

// =====================================================================
// 域常量（唯一书写点——计算核/渲染数据/导出/测试共用，禁第二处字面量）
// =====================================================================

/// 逐样本评估的进度 phase token（reportProgress 的 phase 字段落值——
/// §7.2 kRegionCoveragePhase 的探索式通道对应物；事件经 EX-T05 通道
/// 分发，词形保持 kebab 短语风格）。
inline constexpr char kWorkspaceSamplingPhase[] = "solve-workspace-sample";

/// 探索式计划内容身份编码 magic（8 字节 ASCII——IRDWSP1；与 IRDSIG01/
/// IRDCFG01 同款"magic＋版本"编码纪律，恰 8 字符无尾位歧义——F-395
/// 口径：magic 长度以实写字节为准）。
inline constexpr char kWorkspacePlanMagic[] = "IRDWSP1";

/// 点云渲染/导出数据契约版本（导出值行头与图例词表的演进锚——一经
/// 交付不回退，CON-04 同款纪律；呈现侧按其解释行集）。
/// ★ v2（KIN-P5/KIN-09 关节空间采样语义闭合）：点云单点新增采样模式
/// 类别列（samplingMode）＋导出 CSV 表头新增 "sampling-mode" 列（三值
/// token——cartesian-region/joint-random/joint-grid）；行集消费方按
/// 本版本解释列布局。
inline constexpr std::uint32_t kWorkspaceRenderContractVersion = 2U;

/// 关节空间网格采样的总样本数上限（无量纲计数——组合爆炸防护闸）。
/// 依据：全网格样本数＝∏jointGridCounts[i]，6 轴×每轴 10 格即 10⁶——
/// 每样本一次 FK＋指标评估（亚毫秒级），10⁵ 上限把单次运行压在秒级
/// 预算内（与区域覆盖批量黄金批大小 256×常规计划量同量级）；超限属
/// 调用方参数错误——validateWorkspaceSamplingQuery fail-fast（NFR-COR-03
/// 拒绝而非静默截断）。
inline constexpr std::uint32_t kJointGridMaxTotalSamples = 100000U;

// =====================================================================
// WorkspaceSamplingMode——采样模式三值词表（KIN-P5/KIN-09 语义闭合）
// =====================================================================

/**
 * @brief 工作空间采样模式三值词表（KIN-P5 关节空间采样语义闭合——
 *        需求 KIN-09 权威语义"随机均匀/关节网格采样"的**关节空间**
 *        直采落点；旧实现只有"区域盒笛卡尔采样→逐样本 IK"一径（登记
 *        随卡 §14.6 v0.20 的落点偏差），本词表使其与关节空间两模式
 *        并存并在点云/导出中标注来源）。值语义纯结构；线程安全。
 */
enum class WorkspaceSamplingMode : std::uint8_t {
    /// 区域盒位置/姿态采样→逐样本 IK（旧唯一径——行为保持，逐样本
    /// 语义见 runWorkspaceSampling 注；笛卡尔区域定义查询面直用）。
    CartesianRegion,
    /// 关节空间随机均匀采样→逐构型 FK 产点云（KIN-09 权威语义——
    /// seed 派生确定性序列；样本＝权威关节向量 q，点＝FK(q) 的 TCP
    /// 位置；"随机均匀"语义＝评价区间内均匀撒点）。
    JointSpaceRandom,
    /// 关节空间全网格采样→逐构型 FK 产点云（KIN-09 权威语义——
    /// 逐自由度等分取点的**笛卡尔积**全网格；与初值策略 JointGrid
    /// 的对角线取点不同构，见该策略注的分工说明）。
    JointSpaceGrid,
};

/**
 * @brief 采样模式 token（点云/导出行的稳定键——采样模式来源标注的
 *        词表锚；禁第二处字面量）。
 * @param mode [in] 采样模式
 * @return "cartesian-region"/"joint-random"/"joint-grid"
 *
 * 纯函数；线程安全；不抛。
 */
std::string_view workspaceSamplingModeToken(WorkspaceSamplingMode mode) noexcept;

// =====================================================================
// 呈现词表（着色维度/投影面/渲染模式——需求 KIN-09 原文三组词表的
// 枚举落值；token 函数＝图例/导出行的稳定键，禁第二处字面量）
// =====================================================================

/**
 * @brief 点云着色维度七值词表（KIN-09 原文"按状态/可操作度/条件数/
 *        最小余量/位置误差/姿态误差/碰撞等着色"的枚举落值——ui 呈现
 *        偏好（§4.5 身份外清单），不入任何计算身份）。
 *
 * 六个标量维度的取值源＝样本最佳解（WorkspaceSampleResult::bestSolution
 * ——稳定排序首位解）的对应字段；无解样本（Unreachable/
 * DataInsufficient/NotRun）在标量维度无着色值（hasSolution=false——
 * 呈现侧按状态轴降级渲染，绝不虚构标量值）；状态/碰撞两维度全样本
 * 有值（样本级事实，不依赖解）。值语义纯结构；线程安全。
 */
enum class WorkspaceColorMode : std::uint8_t {
    /// 按状态着色（五值类别——SampleState 词表直投）。
    ByState,
    /// 按可操作度着色（无量纲——bestSolution.manipulability，D-KIN-2）。
    ByManipulability,
    /// 按条件数着色（无量纲——bestSolution.conditionNumber，D-KIN-2；
    /// +∞（奇异）为合法承载值）。
    ByConditionNumber,
    /// 按最小关节余量着色（无量纲归一化比——bestSolution.
    /// minimumJointMargin，D-KIN-6；+∞（全无界关节）合法）。
    ByMinJointMargin,
    /// 按位置误差着色（单位 m——bestSolution.positionResidual，≤ 采样
    /// 位置容差）。
    ByPositionResidual,
    /// 按姿态误差着色（单位 rad——bestSolution.orientationResidual，
    /// ≤ 采样姿态容差；位置样本的求解姿态无约束，残差语义见
    /// WorkspaceSampleResult 注）。
    ByOrientationResidual,
    /// 按碰撞着色（三值类别——无碰撞/碰撞/未评价；取值语义见
    /// workspaceCollisionToken 注）。
    ByCollision,
};

/**
 * @brief 着色维度 token（图例/导出行的稳定键——枚举成员名原文 snake
 *        转写前的 kebab 词形；禁第二处字面量）。
 * @param mode [in] 着色维度
 * @return kebab token（如 "by-state"/"by-manipulability"）
 *
 * 纯函数；线程安全；不抛。
 */
std::string_view workspaceColorModeToken(WorkspaceColorMode mode) noexcept;

/**
 * @brief 投影面三值词表（KIN-09 原文"XY/XZ/YZ 投影"——正交投影的
 *        舍轴语义：XY 保留 (x,y) 舍 z、XZ 保留 (x,z) 舍 y、YZ 保留
 *        (y,z) 舍 x；坐标恒基座系 {B}，单位 m）。值语义纯结构。
 */
enum class WorkspaceProjectionPlane : std::uint8_t {
    /// XY 平面（俯视——保留 x/y 分量）。
    XY,
    /// XZ 平面（正视——保留 x/z 分量）。
    XZ,
    /// YZ 平面（侧视——保留 y/z 分量）。
    YZ,
};

/**
 * @brief 投影面 token（图例/导出行的稳定键）。
 * @param plane [in] 投影面
 * @return "xy"/"xz"/"yz"
 *
 * 纯函数；线程安全；不抛。
 */
std::string_view workspaceProjectionPlaneToken(WorkspaceProjectionPlane plane) noexcept;

/**
 * @brief 渲染模式两值词表（KIN-09 原文"散点/包络渲染"——ui 呈现偏好，
 *        §4.5 身份外清单；**包络几何估算归 KIN-10/T15**（射线 Rmax），
 *        本词表只承载模式选择，Envelope 模式下的点集来源由呈现侧声明，
 *        本头不产包络几何）。值语义纯结构。
 */
enum class WorkspaceRenderStyle : std::uint8_t {
    /// 散点渲染（逐点绘制——点云默认模式）。
    Scatter,
    /// 包络渲染（外包络近似呈现——几何归 KIN-10，见枚举注）。
    Envelope,
};

/**
 * @brief 渲染模式 token（图例/导出行的稳定键）。
 * @param style [in] 渲染模式
 * @return "scatter"/"envelope"
 *
 * 纯函数；线程安全；不抛。
 */
std::string_view workspaceRenderStyleToken(WorkspaceRenderStyle style) noexcept;

// =====================================================================
// WorkspaceSamplingQuery——探索式采样查询值（面板参数的域侧承载）
// =====================================================================

/**
 * @brief 工作空间探索式采样查询（KIN-09 的域侧查询值——用户在结果与
 *        可视化面板给出的采样参数＋求解配置的聚合投影）。
 *
 * 与 RegionCoverageQuery（Evaluators.hpp——正式通道）的同构与差异：
 *   - 同构：注入面纪律（solver/checkpointSink/collisionSession 非 owning
 *     可选指针，空语义逐字段同注）；求解配置逐字段直投（与
 *     AnalysisConfiguration 一一同名对应——调用方从配置值构造本查询，
 *     validateWorkspaceSamplingQuery 复用 validateAnalysisConfiguration
 *     的合法域语义）；批大小默认 kBatchDefaultBatchSize=256（D-KIN-6
 *     批量黄金值复用）。
 *   - 差异：单区域（探索式一次一个区域——旧探索式工作空间语义，
 *     §2.5 对照行；KIN-04 的多计划镜像语义不适用）；无 requirements
 *     计划（区域/采样定义由本查询直接携带——R-1 不破：req 对象
 *     schema 仍不进本单元，本查询字段是 §7.2 投影值类型的**探索式
 *     直用**，宿主解析面不涉及）；无 configDigest/sliceId（探索式
 *     不入快照/切片身份面——结果不归档，溯源经 queryEcho）。
 *
 * "网格步数"语义（KIN-09 原文；登记随卡 §14.6 v0.20）：Grid 采样的
 * 逐轴**步长**（单位 m）——经 gridCountsFromSpacing 规范化为逐轴计数
 * （counts[i]=floor(size[i]/spacing[i])＋1，D-REQ-2 同款语义的唯一
 * 实现点；requirements 侧在计划构建期规范化，探索式无该构建期，故
 * 规范化落本单元）；查询携带**计数**（PositionSamplingDefinition.
 * gridCounts——§7.2 投影值类型直用），步长→计数换算由面板侧调用
 * gridCountsFromSpacing 完成（保持"生成器对定义→样本一对一确定"
 * 的 §7.2 不变式）。
 *
 * 值语义纯结构（指针成员浅拷贝——指向对象须比调用活得久）；线程
 * 安全（并发只读）。单位：referenceQ 逐自由度 rad|m；容差 m/rad；
 * 去重阈值 rad|m 逐轴；计数/线程数/批大小无量纲。
 */
struct WorkspaceSamplingQuery {
    // ---- 采样模式与定义（KIN-P5/KIN-09 语义闭合——模式分派面）----
    /// 采样模式（三值词表——缺省 CartesianRegion 保持旧径行为；
    /// JointSpaceRandom/JointSpaceGrid 两模式经 FK 产点云，见枚举注）。
    WorkspaceSamplingMode mode = WorkspaceSamplingMode::CartesianRegion;
    /// 采样区域（基座系 {B} 轴对齐盒；size 三分量 >0 有限——RegionBox
    /// 契约；探索式的区域由面板直接给出，宿主无需解析 refFrame。仅
    /// CartesianRegion 模式消费——关节空间模式下本字段为查询回显、
    /// 不参与校验与生成）。
    RegionBox region;
    /// 位置采样定义（Random{randomCount}＝随机均匀撒点 / Grid{gridCounts}
    /// ＝体心网格——§7.2 两方法的探索式直用；仅 CartesianRegion 模式
    /// 消费。需求原文"关节网格"的语义落位（§14.6 v0.20 登记"区域位置
    /// 网格"偏差）已由 KIN-P5 关节空间模式闭合——需求权威的"关节空间
    /// 随机/网格采样→FK"由 mode=JointSpaceRandom/JointSpaceGrid 承载，
    /// 本字段回归纯笛卡尔区域语义）。
    PositionSamplingDefinition position;
    /// 姿态采样定义（方向集×roll——**可选**：KIN-09 主径为位置点云
    /// （工作空间＝TCP 可达位置集合），位姿样本经 directionSamples=
    /// rollSamples=1 的单方向单 roll 退化配置承载（与 §7.2"位姿样本
    /// 数随位置样本数同零"的零样本判定兼容——本查询不使用该退化路径，
    /// 见 validateWorkspaceSamplingQuery 注）；斐波那契螺旋黄金复用。
    /// 仅 CartesianRegion 模式消费）。
    OrientationSamplingDefinition orientation;

    // ---- 关节空间采样定义（mode=JointSpace* 时消费；KIN-09 权威
    //      语义"样本数/网格步数可配"的落点）----
    /// 关节空间随机样本数（mode=JointSpaceRandom 消费；无量纲计数，
    /// 0 合法＝零样本场景——与区域通道零样本口径一致；"样本数可配"
    /// 的落点。随机序列种子＝config.regionBudget.seed（单一种子纪律
    /// ——与区域模式同源，I-KIN-4 零种子拒绝由配置校验面承载））。
    std::uint32_t jointRandomCount = 0U;
    /// 关节空间网格逐自由度取点数（mode=JointSpaceGrid 消费；链序、
    /// 长度须＝设备自由度，每轴 ≥1——每轴在评价区间 [lo,hi] 上等分取
    /// counts[i] 个点（**含两端**：counts[i]≥2 时端点即限位点，=1 取
    /// 区间中点）；总样本数＝∏counts[i]（笛卡尔积全网格——与初值策略
    /// JointGrid 的对角线取点不同构，分工见该策略注），上限
    /// kJointGridMaxTotalSamples（超限＝调用方参数错误 fail-fast）。
    /// "网格步数可配"的落点。单位：计数无量纲。
    std::vector<std::uint32_t> jointGridCounts;

    // ---- 求解配置直投（AnalysisConfiguration 的逐字段投影——KIN-13；
    //      种子权威面见 struct 注）----
    /// 求解配置原值（KIN-13——种子/容差/迭代/初值策略/去重阈值的权威
    /// 载体；validateWorkspaceSamplingQuery 先经
    /// validateAnalysisConfiguration 校验其合法域——I-KIN-4 seed=0
    /// 非法不静默替换）。config 内 regionBudget.seed 即采样种子、
    /// regionBudget.threadCount 即并行分片数（单一权威，查询面不另设
    /// 种子/线程字段——防"同查询两种子"的歧义）。
    AnalysisConfiguration config{};
    /// 排序参考构型（rad|m——D-KIN-4 显式输入；维度须＝设备自由度；
    /// 稳定排序第 3 键的排序中心＋SeededRandom 初值序列的消费伴随值）。
    std::vector<double> referenceQ;
    /// 默认 TCP（快照内解析——TcpRef 契约；探索式采样的求解目标面）。
    TcpRef tcp;

    // ---- 执行协作注入（P-KIN-7 最小端口口径——RegionCoverageQuery 同构）----
    /// 碰撞会话句柄（非 owning；**统一策略注入面——查询零碰撞布尔
    /// 字段**（V13-01/UX-08/ARC-05）：策略启用碰撞→装配层注入已解析
    /// 会话；空＝碰撞检查不在范围（逐样本 collisionNotEvaluated 如实
    /// 标记，绝不解读为无碰撞——KIN-05））。
    const IKinCollisionSession* collisionSession = nullptr;
    /// 样本批大小上限（≥1；默认 kBatchDefaultBatchSize=256——D-KIN-6
    /// 复用）。
    std::uint32_t maxBatchSize = 256U;
    /// 求解器替身注入口（非 owning；空＝内置 IkSolver——生产路径；
    /// 测试以确定性替身注入——§10.1 可控求解器替身先例）。
    const IIkSolver* solver = nullptr;
    /// 样本批检查点 watermark 通道（非 owning；空＝无检查点通道——
    /// IBatchCheckpointSink 复用（T05 端口——EX-T08 检查点治理的
    /// 消费面）；watermark 语义＝"第 N 批样本已完成"，续跑归属宿主）。
    IBatchCheckpointSink* checkpointSink = nullptr;

    bool operator==(const WorkspaceSamplingQuery& o) const
    {
        return mode == o.mode && region == o.region && position == o.position
            && orientation == o.orientation && jointRandomCount == o.jointRandomCount
            && jointGridCounts == o.jointGridCounts && config == o.config
            && referenceQ == o.referenceQ && tcp == o.tcp
            && collisionSession == o.collisionSession
            && maxBatchSize == o.maxBatchSize && solver == o.solver
            && checkpointSink == o.checkpointSink;
    }
    bool operator!=(const WorkspaceSamplingQuery& o) const { return !(*this == o); }
};

/**
 * @brief 区域尺寸→Grid 逐轴计数的步长规范化（"网格步数"语义的唯一
 *        实现点——D-REQ-2 同款语义：counts[i]＝floor(size[i]/spacing[i])
 *        ＋1（体心规则下含两端半格——格数＝区间能容纳的步数＋1 个格心）。
 *
 * 探索式模式无 requirements 构建期（D-REQ-2 的规范化发生在 requirements
 * 侧），规范化落本单元；面板编辑面把用户输入的步长经本函数换算为
 * 计数后填入 WorkspaceSamplingQuery::position.gridCounts——生成器
 * （generateGridPositions，§7.2 黄金锁定）只消费计数，语义单一。
 *
 * @param boxSize [in] 区域盒三边长（基座系 {B}，单位 m；三分量须 >0
 *                有限——RegionBox 契约，违例 std::invalid_argument）
 * @param spacing [in] 逐轴步长（基座系 {B} 各轴，单位 m；三分量须 >0
 *                有限，违例 std::invalid_argument——"步数可配"的
 *                良构下界；不钳制不置零，NFR-COR-03）
 * @return 逐轴计数（x/y/z 链序；每轴 ≥1——size>0∧spacing>0 保证
 *         floor≥0；无量纲）
 *
 * @throws std::invalid_argument 盒尺寸或步长非正/非有限
 *
 * 纯函数；线程安全；确定性。
 */
std::array<std::uint32_t, 3>
gridCountsFromSpacing(const rw::math::Vector3D<double>& boxSize,
                      const rw::math::Vector3D<double>& spacing);

/**
 * @brief 由探索式查询组装采样计划投影并计算其内容身份（会话内溯源
 *        面——§7.2 sampleSetIdentity 消费的第一输入）。
 *
 * 组装规则（确定性）：regionObjectId＝全零保留值（探索式无 req 区域
 * 对象——"未绑定"语义，与 IkTargetRef.pointOid 保留值同规）；区域/
 * 位置/姿态采样定义逐字段拷贝；demands 为默认值（探索式零要求值
 * ——碰撞检查在场性只由 collisionSession 注入决定，V13-01）；mirrorOf
 * 为空。planContentIdentity＝SHA-256 over canonical 字节（布局见
 * Workspace.cpp 的 workspacePlanContentIdentity 函数注——magic
 * IRDWSP1＋区域几何/采样参数全字段定宽小端），**每次调用重算**——
 * 同查询同身份（确定性），身份只作会话内溯源回显与重跑核对（R2
 * 边界：不对账任何快照凭据，不冻结）。
 *
 * @param query [in] 探索式采样查询（采样定义字段被消费；config 不入
 *              计划身份——样本集身份的预算/种子面由 sampleSetIdentity
 *              的 budget 参数承载，与 §7.2 公式同构）
 * @return 采样计划投影（planContentIdentity 已填）
 *
 * 纯函数；线程安全；确定性。
 */
SamplingPlan workspaceSamplingPlan(const WorkspaceSamplingQuery& query);

// =====================================================================
// WorkspaceSampleResult——逐样本结果（解级指标投影＋点云坐标）
// =====================================================================

/**
 * @brief 单个探索式采样样本的评估结果（§7.2 SampleResultRecord 的探索
 *        式扩展——状态/结局/原因同构面＋点云坐标＋最佳解指标投影）。
 *
 * 字段语义分层（着色维度的取值源，acceptance 3）：
 *   - 样本级事实（全样本有值）：state（五值——SampleState 词表与映射
 *     口径与 KIN-04 完全同源：1/4→Reached、5→Unreachable、2/3→
 *     DataInsufficient、取消未派发→NotRun）、collisionNotEvaluated、
 *     tcpPosition（点云坐标——Reached/DataInsufficient（含部分解态）
 *     样本为采样点位置本身（区域定义点——探索式点云横轴；与解的
 *     TCP 复算位置的差异正是"采样点 vs 可达点"的呈现事实）；NotRun
 *     样本同样携带（采样点的存在先于评估））；
 *   - 解级投影（仅 Reached 样本非空）：bestSolution＝该样本多初值
 *     求解解集的稳定排序首位解（§6.3 四键——与解集视图 sorted()[0]
 *     同语义；着色六标量与回写 q 的唯一来源；探索式不携带全解集——
 *     点云浏览是"每采样点一个代表解"的估算语义，全解集消费归单点
 *     求解/批量通道）。
 *
 * 姿态误差的语义边界（诚实登记，随卡 §14.6 v0.20；KIN-P2/A02 更新落值
 * 机制）：位置样本的姿态无约束（§7.2 存在性口径——求解以
 * IkRequest.taskSpace=PositionOnly 承载，3 维任务子空间：姿态项不进目
 * 标函数；旧"姿态容差 π 落值"机制已废——审查 20261016 A02），其
 * bestSolution.orientationResidual 是"该构型对采样位置的 IK 复算姿态
 * 残差"（数值有界但非位姿目标误差）——着色呈现时呈现侧应按"位置样本
 * 姿态无约束"事实解释；位姿样本（完整位姿 IK）的残差才是完整位姿
 * 误差。本结构不另设"有效/无意义"标记——维度语义由采样定义
 * （position.kind）决定，呈现侧可查。
 *
 * 值语义纯结构；线程安全（并发只读）。
 */
struct WorkspaceSampleResult {
    /// 对齐键（＝SampleRecord::sampleIndex——样本集双射核查键）。
    std::uint64_t sampleIndex = 0;
    /// 评估状态（五值——SampleState；映射口径见 struct 注）。
    SampleState state = SampleState::NotRun;
    /// 求解结局（可空——实际求解的样本携带；预终结轨为空）。
    std::optional<IkOutcomeKind> outcomeKind;
    /// 碰撞未评价标记（检查不在范围/评价未完成——绝不解读为无碰撞，
    /// KIN-05；着色 ByCollision 的"未评价"类别取值源）。
    bool collisionNotEvaluated = false;
    /// 原因文本（NotRun 必填非空——ERR-01；缺碰撞会话/设施失败轨的
    /// DataInsufficient 携带原因；其余状态为空）。
    std::string reason;
    /// 采样点位置（基座系 {B}，单位 m——点云坐标；区域模式＝采样点
    /// 位置本身（区域定义点，NotRun 样本同样携带——采样点存在先于
    /// 评估）；关节模式＝FK(采样构型) 的 TCP 位置（KIN-P5/关节空间
    /// 采样——点即"该构型可达"的构造性凭据；NotRun 槽位无位置事实
    /// ——保持默认占位且不入点云/值域，见 buildWorkspacePointCloud 注）。
    rw::math::Vector3D<double> tcpPosition = rw::math::Vector3D<double>(0.0, 0.0, 0.0);
    /// 采样构型（rad|m 逐自由度链序——**仅关节空间模式**的 Reached
    /// 样本携带（采样定义本身）；区域模式恒空（区域样本的定义是位置
    /// 点不是构型）。FK 样本的该构型即"可达性凭据"——点云回写出口
    /// （workspacePointWriteback）据此返回采样构型本身）。
    std::vector<double> jointConfiguration;
    /// 最佳解（可空——Reached 样本携带稳定排序首位解；着色六标量与
    /// 回写 q 的唯一来源，见 struct 注）。
    std::optional<KinematicSolution> bestSolution;
    /// 样本是否发生碰撞过滤（true＝该样本求解中有解被硬过滤③碰撞
    /// 过滤——outcomeKind==PartialCollision 的等价事实投影；着色
    /// ByCollision 的"碰撞"类别取值源之一——样本级碰撞事实，不依赖
    /// 最佳解的碰撞状态）。
    bool collisionFiltered = false;

    /// 等值比较（逐成员；bestSolution 为可选值——两侧同有无、有则逐
    /// 字段比较（KinematicSolution 无等值算子——其等值语义在此封闭：
    /// q/残差/裕量/指标/碰撞状态/初值下标/迭代数/签名/契约版本全集，
    /// 禁数值容差——身份面比较口径）。
    bool operator==(const WorkspaceSampleResult& o) const
    {
        if (sampleIndex != o.sampleIndex || state != o.state
            || outcomeKind != o.outcomeKind
            || collisionNotEvaluated != o.collisionNotEvaluated
            || reason != o.reason || tcpPosition != o.tcpPosition
            || jointConfiguration != o.jointConfiguration
            || collisionFiltered != o.collisionFiltered
            || bestSolution.has_value() != o.bestSolution.has_value()) {
            return false;
        }
        if (!bestSolution.has_value()) {
            return true;
        }
        const KinematicSolution& a = *bestSolution;
        const KinematicSolution& b = *o.bestSolution;
        return a.q == b.q && a.positionResidual == b.positionResidual
            && a.orientationResidual == b.orientationResidual
            && a.jointMargins == b.jointMargins
            && a.minimumJointMargin == b.minimumJointMargin
            && a.manipulability == b.manipulability
            && a.conditionNumber == b.conditionNumber
            && a.collisionStatus.evaluated == b.collisionStatus.evaluated
            && a.collisionStatus.inCollision == b.collisionStatus.inCollision
            && a.collisionStatus.objectIdPairs == b.collisionStatus.objectIdPairs
            && a.sourceInitIndex == b.sourceInitIndex && a.iterations == b.iterations
            && a.signature == b.signature
            && a.solverContractVersion == b.solverContractVersion;
    }
    bool operator!=(const WorkspaceSampleResult& o) const { return !(*this == o); }
};

// =====================================================================
// WorkspaceSampleRun——探索式采样运行产物（零覆盖率——R2 边界结构保证）
// =====================================================================

/**
 * @brief 一次工作空间探索式采样的运行产物（结果与可视化面板的点云
 *        数据源；**结构上无覆盖率字段**——R2 边界"探索式结果不进 Must
 *        区域判定或必验覆盖"（acceptance 5/§7.3）的类型级保证：产物
 *        不含 CoverageResult/证据行/verdictInputs，调用方无从把本产物
 *        送入证据通道——那需要 evidence 评估器形态，而探索式采样没有）。
 *
 * partial 语义（acceptance 2/V-11/22 同口径）：cancelled=true ⇒ 必有
 * NotRun 样本（reason 非空"协作取消"）⇒ **不完整结果**——呈现侧不得
 * 把 cancelled 运行当作完整点云（图例计数如实含 NotRun 类目）；调用
 * 方以 completedCount＋notRunCount 判读完整性。取消/失败不产"完整
 * 结果"由本结构的字段组合如实承载（无"伪造完成"通道）。
 *
 * 值语义；冻结后只读；线程安全（并发只读）。
 */
struct WorkspaceSampleRun {
    // ---- 参数溯源回显（会话内——R2 边界：不入快照/不冻结/不入身份）----
    /// 采样模式（KIN-P5/KIN-09——三值词表原样回显；点云/导出的模式
    /// 标注取值源）。
    WorkspaceSamplingMode mode = WorkspaceSamplingMode::CartesianRegion;
    /// 采样计划投影（workspaceSamplingPlan 产出——含会话内计划内容
    /// 身份；回显与重跑核对用，绝不对账快照凭据。关节空间模式下计划
    /// 身份的 canonical 含模式与关节采样定义（编码见 Workspace.cpp
    /// workspacePlanContentIdentityImpl 注），box/position/orientation
    /// 字段为查询回显、生成面不消费）。
    SamplingPlan plan;
    /// 样本集身份（sampleSetIdentity(plan.planContentIdentity, budget)
    /// ——§7.2 公式复用；会话内重跑一致性核对键——acceptance 1"同
    /// 种子重跑样本集一致"的比对锚）。
    core::ContentIdentity sampleSetIdentity;
    /// 采样预算回显（种子/线程——种子来自 AnalysisConfiguration.
    /// regionBudget.seed，KIN-13 权威面）。
    RegionSamplingBudget budget;

    // ---- 样本与结果 ----
    /// 确定性样本集（§7.2 生成设施复用——D-KIN-6：同 (定义,种子) 同
    /// 集同序。仅 CartesianRegion 模式填充——关节空间模式样本定义是
    /// 构型不是 SampleRecord，样本集恒空、逐样本定义走 jointConfigurations）。
    SampleSet samples;
    /// 关节空间采样构型集（mode=JointSpace* 时与 results 按下标对齐——
    /// 每样本的权威关节向量（采样定义原值，rad|m 链序）；CartesianRegion
    /// 模式恒空。确定性：同 (模式参数, 种子, 区间) 同集同序（随机＝
    /// seed 派生序列、网格＝笛卡尔积黄金序））。
    std::vector<std::vector<double>> jointConfigurations;
    /// 逐样本结果（与 samples/jointConfigurations 按 sampleIndex 双射
    /// 对齐——完整性自检见 runWorkspaceSampling 注）。
    std::vector<WorkspaceSampleResult> results;

    // ---- 完整性事实（partial 如实——acceptance 2）----
    /// 协作取消标记（true＝运行被取消中止——results 中存在 NotRun
    /// 样本且 reason="协作取消…"；取消不是结局，无终局判定语义）。
    bool cancelled = false;
    /// 已完成样本数（state≠NotRun 的样本数——与 results 逐一计数的
    /// 守恒式由 runWorkspaceSampling 内检保证）。
    std::uint64_t completedCount = 0;
    /// 未运行样本数（NotRun——取消后未派发；completedCount＋notRunCount
    /// ＝samples.samples.size()）。
    std::uint64_t notRunCount = 0;

    /// 等值比较（逐成员；samples 为 SampleSet（Sampling.hpp 值——无
    /// 等值算子），其等值语义在此封闭：样本向量逐条（全字段）＋分母
    /// 两值；results 逐条（vector ==——元素等值算子已定义）。
    bool operator==(const WorkspaceSampleRun& o) const
    {
        if (mode != o.mode || plan != o.plan || sampleSetIdentity != o.sampleSetIdentity
            || budget != o.budget || results != o.results
            || jointConfigurations != o.jointConfigurations
            || samples.samples.size() != o.samples.samples.size()
            || samples.plannedPositionSamples != o.samples.plannedPositionSamples
            || samples.plannedPoseSamples != o.samples.plannedPoseSamples
            || cancelled != o.cancelled || completedCount != o.completedCount
            || notRunCount != o.notRunCount) {
            return false;
        }
        for (std::size_t i = 0; i < samples.samples.size(); ++i) {
            const SampleRecord& a = samples.samples[i];
            const SampleRecord& b = o.samples.samples[i];
            if (a.sampleIndex != b.sampleIndex || a.kind != b.kind
                || a.position != b.position || a.pose != b.pose
                || !(a.regionObjectId == b.regionObjectId)
                || !(a.planContentIdentity == b.planContentIdentity)) {
                return false;
            }
        }
        return true;
    }
    bool operator!=(const WorkspaceSampleRun& o) const { return !(*this == o); }
};

// =====================================================================
// validateWorkspaceSamplingQuery——装配校验（调用方错误 fail-fast 轨）
// =====================================================================

/**
 * @brief 校验探索式采样查询合法域（违例抛 std::invalid_argument——
 *        fail-fast 不钳制，NFR-COR-03；校验序固定——同一坏查询必报
 *        同一首错，NFR-COR-02）。
 *
 * 校验面（按序；KIN-P5 起按模式分派——CartesianRegion 校验区域/位置/
 * 姿态定义，JointSpace* 校验关节采样定义，两族互不越界）：
 *   1. config 合法域（validateAnalysisConfiguration 复用——I-KIN-4：
 *      config.seed≠0∧config.regionBudget.seed≠0、容差/迭代/计数下界；
 *      KIN-13 语义的权威校验点——"种子经 AnalysisConfiguration 配置"
 *      的落地面）；
 *   2. 采样模式词表值域（WorkspaceSamplingMode 三值防御——uint8 位型
 *      强转可造出词表外值，NFR-COR-03）；
 *   3. CartesianRegion 面：区域盒（size 三分量 >0 有限——RegionBox
 *      契约）；位置采样定义良构（Grid 任一轴计数为 0 → 零样本场景
 *      **合法**（与 §7.2 零样本口径一致——运行产物为空集，不拒）；
 *      Random 的 randomCount=0 同理合法；枚举值域防御）；姿态采样定义
 *      （directionSamples/rollSamples ≥1——requirements 词表原文；
 *      methodToken 允许空）；
 *   4. JointSpaceRandom 面：jointRandomCount 无下界（0＝零样本合法）；
 *   5. JointSpaceGrid 面：jointGridCounts 长度＝设备自由度、每轴 ≥1、
 *      笛卡尔积 ≤ kJointGridMaxTotalSamples（组合爆炸防护——调用方
 *      参数错误 fail-fast，NFR-COR-03 拒绝而非静默截断）；
 *   6. referenceQ 维度＝设备自由度且全有限（D-KIN-4——三模式共同
 *      消费：区域模式的排序参考/初值伴随、关节模式的 FK 排序伴随）；
 *   7. maxBatchSize ≥1；
 *   8. TCP 在场性**不在此拒**（快照工具缺失＝运行期 KIN-NO-TCP 素材
 *      轨——T03/T06 两分口径同源：模型侧缺失≠调用方违约；tcp 悬空
 *      在运行期由求解器/FK 解析面暴露）。
 *
 * @param view  [in] 只读模型视图（自由度来源；调用方保证存活）
 * @param query [in] 探索式采样查询
 *
 * @throws std::invalid_argument 任一校验面违例（文案含首错字段/实际
 *         值/合法域）
 *
 * 纯函数；线程安全。
 */
void validateWorkspaceSamplingQuery(const IKinRuntimeView& view,
                                    const WorkspaceSamplingQuery& query);

// =====================================================================
// runWorkspaceSampling——探索式采样计算核（纯计算面；T05/T06 同构）
// =====================================================================

/**
 * @brief 执行一次工作空间探索式采样（KIN-09 全链：确定性采样计划→
 *        逐样本多初值 IK→解级指标投影→点云结果集；取消/进度/检查点
 *        经宿主上下文——EX-T03/05/08 消费面）。
 *
 * 执行口径（§7.2/§8.3/§8.4 语义复用——T06 runRegionCoverageComputation
 * 同构，差异见各步注；KIN-P5 起按 query.mode 分派）：
 *   - 步骤 1 采样计划与样本集：
 *     · CartesianRegion（旧径，行为保持）——workspaceSamplingPlan(query)
 *       ＋generateSampleSet（{plan}, budget——§7.2 黄金生成设施：Grid
 *       体心/Random splitmix64/斐波那契螺旋×roll 均分；同 (定义,种子)
 *       同集同序——acceptance 1 确定性的结构来源）；
 *     · JointSpaceRandom/JointSpaceGrid（KIN-09 权威语义落点）——
 *       关节空间确定性采样（随机＝seed 派生序列〔streamSeedFor 完整
 *       64 位混合——A06 同源〕、网格＝逐轴等分取点笛卡尔积）生成逐
 *       样本权威关节向量集，**经 FK 产点云**（KIN-09 原文"关节空间
 *       采样"的构造性语义：构型可达由 FK 全序保证，无搜索未果形态）；
 *       计划身份的 canonical 含模式与关节采样定义（codec 版本 2——
 *       Workspace.cpp 编码注），样本集身份公式不变；
 *   - 步骤 2 评估环境：区域模式提取评价区间/初值集（初值种子与采样
 *     种子同源＝config.regionBudget.seed——§3.4 单一种子纪律），求解器
 *     ＝query.solver 或内置 IkSolver；关节模式提取评价区间（采样域）
 *     与 FkEvaluator（§9.2 纯函数服务——KIN-01 权威实现）；
 *   - 步骤 3 逐样本评估：分批（maxBatchSize）＋批间取消查询（每批至
 *     少一次）＋批内并行分片（threadCount——各线程只写全序槽位，合并
 *     按全序；异常按分片序重抛首错）＋逐样本求解：
 *     · 区域模式逐样本 IK（位置样本＝位置存在性 IK——taskSpace=
 *       PositionOnly 任务维度〔KIN-P2/A02，旧姿态容差 π 落值机制已废〕；
 *       位姿样本＝完整位姿 IK；IkRequest 的身份面字段以零值/回显值
 *       填充——探索式不入切片身份，requestIdentity 仅作求解器契约的
 *       形态满足）；结局→样本状态映射同 §7.2 词表；Reached 样本提取
 *       稳定排序首位解为 bestSolution；
 *     · 关节模式逐样本 FK（FkEvaluator.evaluate(view, tcp, q)）——
 *       成功＝Reached（tcpPosition＝FK 的 tcpInBase 平移、jointConfiguration
 *       ＝采样构型、bestSolution＝PoseMetrics 投影：q/零残差〔FK 无
 *       目标误差——构造性可达〕/裕量/可操作度/条件数）、FK 值面错误
 *       （NoTcp 等——模型侧事实）＝DataInsufficient（原因携带错误码
 *       语义，绝不虚构可达）；FK 不可取消（§9.2"可预测 <1 s 内联"）
 *       ——取消粒度仍由批间查询承载。
 *   - 步骤 4 批完成：checkpointSink 批 watermark＋reportProgress
 *     （批粒度，phase=kWorkspaceSamplingPhase——EX-T05 事件分发由
 *     宿主上下文承接）；
 *   - 步骤 5 收尾：取消→剩余样本如实 NotRun（reason 非空）＋cancelled
 *     标记——**partial 产物合法返回**（不抛、不产伪完整——V-11/22）；
 *     完整性自检（双射对齐＋计数守恒）违例 logic_error（内部缺陷不
 *     静默）。
 *
 * 与 KIN-04 的通道分立（重申——R2 边界）：本函数**不产覆盖率/不产
 * 证据行/不进 evaluation 输出面**；产物仅入会话（呈现）。
 *
 * @param view    [in] 宿主注入的只读模型视图（非 owning；调用期间存活）
 * @param query   [in] 探索式采样查询（先经 validateWorkspaceSamplingQuery
 *                     同款校验——违例 fail-fast；替身注入口生效）
 * @param context [in] 宿主调用上下文（协作取消查询＋进度上报——
 *                     evidence::IEvaluationContext 复用：其取消/进度
 *                     通道语义即 EX-T03/EX-T05 的直接消费面；对象物
 *                     化方法本通道不消费）
 * @return 运行产物（cancelled 可能为 true 的 partial 值——调用方按
 *         completedCount/notRunCount 判读完整性）
 *
 * @throws std::invalid_argument 查询校验违例（调用方错误——fail-fast）
 * @throws std::logic_error 内部不变量破坏（双射/守恒——实现缺陷不静默）
 *
 * 纯计算（不派发任务/不写项目/零修订/不入 results 归档）；线程安全
 * （无跨调用状态，并行分片按 config.regionBudget.threadCount 执行）；
 * 确定性（同输入同线程数逐位一致——NFR-COR-01/02）。
 */
WorkspaceSampleRun runWorkspaceSampling(const IKinRuntimeView& view,
                                        const WorkspaceSamplingQuery& query,
                                        evidence::IEvaluationContext& context);

// =====================================================================
// 点云渲染数据（acceptance 3——数据本单元产、呈现归 ui View3D；
// §3.3 Render.hpp 纪律的 KIN-09 扩展面）
// =====================================================================

/**
 * @brief 点云单点（渲染数据的原子值——着色标量六元＋状态/碰撞类别＋
 *        回写定位）。
 *
 * 标量字段语义（WorkspaceSampleResult 的投影直投，零二次换算）：
 * 仅 hasSolution=true 时六标量有值（最佳解字段副本）；hasSolution=false
 * 时六标量恒 0（占位——呈现侧以 hasSolution 判读，绝不把占位 0 当
 * 数据渲染）。collisionFiltered/collisionNotEvaluated 是样本级事实
 * （全样本有值）。值语义纯结构；线程安全。
 */
struct WorkspacePointCloudPoint {
    /// 样本全局序（0 起连续——results 对齐键；导出行与选中联动的
    /// 稳定键）。
    std::uint64_t sampleIndex = 0;
    /// 点位置（基座系 {B}，单位 m——投影/三维散点的坐标源）。
    rw::math::Vector3D<double> position = rw::math::Vector3D<double>(0.0, 0.0, 0.0);
    /// 采样模式类别（KIN-P5/KIN-09——WorkspaceSamplingMode 词表值
    /// （0=cartesian-region/1=joint-random/2=joint-grid）；点云与导出
    /// 的采样模式来源标注（需求 KIN-09 语义闭合——"点从哪来"的强制
    /// 溯源列）。
    std::uint8_t samplingMode = 0U;
    /// 状态类别（五值——ByState 着色的类别键）。
    SampleState state = SampleState::NotRun;
    /// 碰撞三态类别（ByCollision 着色——workspaceCollisionToken 词表；
    /// 0＝无碰撞、1＝碰撞、2＝未评价；取值语义见函数注）。
    std::uint8_t collisionClass = 2U;
    /// 有最佳解（true＝六标量有值）。
    bool hasSolution = false;
    /// 可操作度（无量纲——bestSolution.manipulability）。
    double manipulability = 0.0;
    /// 条件数（无量纲；+∞ 合法）。
    double conditionNumber = 0.0;
    /// 最小关节余量（无量纲归一化比；+∞ 合法）。
    double minimumJointMargin = 0.0;
    /// 位置误差（单位 m——bestSolution.positionResidual）。
    double positionResidual = 0.0;
    /// 姿态误差（单位 rad——bestSolution.orientationResidual；位置
    /// 样本语义见 WorkspaceSampleResult 注）。
    double orientationResidual = 0.0;
    /// 样本发生碰撞过滤（样本级碰撞事实——PartialCollision 等价投影）。
    bool collisionFiltered = false;

    bool operator==(const WorkspacePointCloudPoint& o) const
    {
        return sampleIndex == o.sampleIndex && position == o.position
            && samplingMode == o.samplingMode && state == o.state
            && collisionClass == o.collisionClass
            && hasSolution == o.hasSolution
            && manipulability == o.manipulability
            && conditionNumber == o.conditionNumber
            && minimumJointMargin == o.minimumJointMargin
            && positionResidual == o.positionResidual
            && orientationResidual == o.orientationResidual
            && collisionFiltered == o.collisionFiltered;
    }
    bool operator!=(const WorkspacePointCloudPoint& o) const { return !(*this == o); }
};

/**
 * @brief 碰撞三态类别 token（ByCollision 图例/导出行的类别键——三值
 *        词表：无碰撞/碰撞/未评价）。
 * @param collisionClass [in] 类别值（0/1/2——WorkspacePointCloudPoint.
 *                       collisionClass 契约域）
 * @return "collision-free"/"in-collision"/"not-evaluated"；越界值→
 *         "not-evaluated"（保守缺省——不虚构类别）
 *
 * 纯函数；线程安全；不抛。
 */
std::string_view workspaceCollisionToken(std::uint8_t collisionClass) noexcept;

/**
 * @brief 着色维度图例（acceptance 3"标签/图例"的数据面——ui 图例
 *        控件的行源；本单元产数据、绘制归 ui）。
 *
 * 类别维度（ByState/ByCollision）：entries＝类别逐条（categoryToken
 * 非空、minValue=maxValue=0——类别图例无数值域）；标量维度：单条
 * 图例（categoryToken 空）＋值域 [minValue,maxValue]（有限实测值域
 * ——全部有效点的 min/max；无有效点（全 hasSolution=false）→
 * hasRange=false——呈现侧呈现"无数据"而非虚构值域，+∞ 参与值域时
 * hasRange 仍 true 但上限为 +∞（如实——不截断））。值语义纯结构；
 * 线程安全。
 */
struct WorkspaceColorLegend {
    /// 图例所属着色维度（token＝workspaceColorModeToken）。
    WorkspaceColorMode mode = WorkspaceColorMode::ByState;
    /// 图例条目（类别维度＝类别逐条、稳定词表序；标量维度＝单条）。
    struct Entry {
        /// 类别 token（类别维度非空——"reached"/"not-run"/
        /// "collision-free"…；标量维度空串）。
        std::string categoryToken;
        /// 值域下界（标量维度有效；无量纲或 m/rad 随维度）。
        double minValue = 0.0;
        /// 值域上界（标量维度有效；+∞ 合法承载）。
        double maxValue = 0.0;

        bool operator==(const Entry& o) const
        {
            return categoryToken == o.categoryToken && minValue == o.minValue
                && maxValue == o.maxValue;
        }
        bool operator!=(const Entry& o) const { return !(*this == o); }
    };
    /// 条目集（序＝确定性——类别词表序/标量单条）。
    std::vector<Entry> entries;
    /// 值域在场（标量维度：true＝entries[0] 的 min/max 可用）。
    bool hasRange = false;

    bool operator==(const WorkspaceColorLegend& o) const
    {
        return mode == o.mode && entries == o.entries && hasRange == o.hasRange;
    }
    bool operator!=(const WorkspaceColorLegend& o) const { return !(*this == o); }
};

/**
 * @brief 点云渲染数据（buildWorkspacePointCloud 产物——面板点云呈现/
 *        投影/图例/导出的唯一数据源；KIN-07 渲染数据的 KIN-09 同构面：
 *        数据本单元产、呈现归 ui View3D）。
 *
 * 不变式（组装器保证）：points 按 sampleIndex 升序连续（样本全序
 * ——确定性序）；sizeHints 三分量与 positions 实测极值一致（值域
 * 自检——呈现侧视图缩放的直接依据）。值语义；冻结后只读；线程安全。
 */
struct WorkspacePointCloudRenderData {
    /// 渲染数据契约版本（kWorkspaceRenderContractVersion——导出行头
    /// 与图例解释锚）。
    std::uint32_t contractVersion = kWorkspaceRenderContractVersion;
    /// 点集（样本全序——sampleIndex 升序连续）。
    std::vector<WorkspacePointCloudPoint> points;
    /// 位置值域下界（基座系 {B}，单位 m——逐轴实测 min）。
    rw::math::Vector3D<double> positionMin = rw::math::Vector3D<double>(0.0, 0.0, 0.0);
    /// 位置值域上界（基座系 {B}，单位 m——逐轴实测 max；空点云＝
    /// min=max=0）。
    rw::math::Vector3D<double> positionMax = rw::math::Vector3D<double>(0.0, 0.0, 0.0);

    bool operator==(const WorkspacePointCloudRenderData& o) const
    {
        return contractVersion == o.contractVersion && points == o.points
            && positionMin == o.positionMin && positionMax == o.positionMax;
    }
    bool operator!=(const WorkspacePointCloudRenderData& o) const { return !(*this == o); }
};

/**
 * @brief 从运行产物组装点云渲染数据（KIN-09 渲染数据唯一组装点——
 *        七维着色标量/碰撞类别/状态类别的投影直投，零二次计算语义）。
 *
 * 组装口径（确定性）：逐 results 序投影（sampleIndex 升序——全序）；
 * 碰撞类别映射——collisionNotEvaluated→2（未评价；KIN-05：绝不把
 * 未评价呈现为无碰撞）、否则（有解且 bestSolution 碰撞状态 inCollision）
 * →1、否则→0（无碰撞——注意：仅当碰撞检查在场（非未评价）时 0 才
 * 表示"已检查无碰撞"）＋collisionFiltered=true→1（样本级碰撞事实
 * ——有解被碰撞过滤即"碰撞"类）；标量六元自 bestSolution 直投。
 *
 * @param run [in] 运行产物（冻结值——runWorkspaceSampling 产出）
 * @return 渲染数据（同输入同值；空运行→空点集＋零值域）
 *
 * 纯函数；线程安全；确定性。
 */
WorkspacePointCloudRenderData
buildWorkspacePointCloud(const WorkspaceSampleRun& run);

/**
 * @brief 正交投影坐标值行（acceptance 3"XY/XZ/YZ 投影"的数据面——
 *        舍轴投影唯一实现点：XY→(x,y)、XZ→(x,z)、YZ→(y,z)；坐标恒
 *        基座系 {B}，单位 m）。
 * @param data  [in] 点云渲染数据
 * @param plane [in] 投影面
 * @return 投影坐标对（序＝points 全序；分量单位 m）
 *
 * 纯函数；线程安全；确定性。
 */
std::vector<std::pair<double, double>>
projectWorkspacePoints(const WorkspacePointCloudRenderData& data,
                       WorkspaceProjectionPlane plane);

/**
 * @brief 指定着色维度的图例（acceptance 3"标签/图例"——类别/值域两
 *        形态，语义见 WorkspaceColorLegend 注）。
 * @param data [in] 点云渲染数据
 * @param mode [in] 着色维度
 * @return 图例值（确定性——类别词表序/单条值域）
 *
 * 纯函数；线程安全；确定性。
 */
WorkspaceColorLegend workspaceLegendFor(const WorkspacePointCloudRenderData& data,
                                        WorkspaceColorMode mode);

/**
 * @brief 点云导出值行（acceptance 3"PNG 导出"的本单元数据面——PNG
 *        文件生成归 ui View3D/绘图契约（REQUIREMENTS 附录 A"视图截图
 *        PNG＝RWStudioView3D.cpp"分工），本单元交付导出的**数据行**：
 *        每点一行全部呈现字段（投影坐标三面全量＋七维着色值＋类别
 *        token），呈现侧据此渲染任意投影/着色组合或出图（PNG）。
 *        导出零修订（KIN-08/AT-04 同款——纯值行，写出经 io 通道由
 *        插件侧触发）。
 *
 * 行格式（契约版本 2；首行为表头——呈现侧/测试的解析锚）：
 * @code
 * sample-index,x-m,y-m,z-m,sampling-mode,state,collision,has-solution,
 * manipulability,condition-number,min-joint-margin,position-residual-m,
 * orientation-residual-rad
 * @endcode
 * 数值文本＝最短往返十进制（std::chrono 无关——格式化确定性由
 * snprintf("%.17g") 保证，跨进程逐位一致——NFR-COR-02；+∞ 文本
 * "inf"）；类别列＝稳定 token（workspace*Token 词表；sampling-mode 列
 * ＝workspaceSamplingModeToken 三值——KIN-P5/KIN-09 采样模式来源标注，
 * 契约版本 2 起新增列）。CSV 转义：全字段无逗号/引号字符（token 词表
 * 封闭）——无需引号包裹。
 *
 * @param data [in] 点云渲染数据
 * @return CSV 文本（行序＝points 全序；含表头行；空点云→仅表头）
 *
 * 纯函数；线程安全；确定性。
 */
std::string workspacePointCloudExportCsv(const WorkspacePointCloudRenderData& data);

// =====================================================================
// 可视化点会话回写（acceptance 4——KIN-06/AT-04；L-K4 渲染面板回写）
// =====================================================================

/**
 * @brief 取点云某采样点的会话回写值（L-K4"可视化点回写"的 KIN-09
 *        数据出口——与 Render.hpp writebackOf 同族：产出
 *        RenderPointWriteback（结构性只有关节向量＋构型签名——零端口/
 *        零身份字段），消费口＝writebackToSessionPose（KinSessionPose
 *        唯一写点）：零修订、零失效、不入缓存身份（KIN-06/AT-04/
 *        V-18）。
 *
 * @param run        [in] 运行产物（冻结值）
 * @param sampleIndex [in] 采样点全局序（results 对齐键）
 * @return 回写值（bestSolution.q 副本＋签名）
 *
 * @throws std::invalid_argument sampleIndex 越界、或该样本无 bestSolution
 *         （无解点无可回写构型——调用方应在 UI 层禁用无解点的回写
 *         入口；此处 fail-fast 而非空值——误用即契约违约，NFR-COR-03）
 *
 * 纯函数；线程安全；确定性。
 */
RenderPointWriteback workspacePointWriteback(const WorkspaceSampleRun& run,
                                             std::uint64_t sampleIndex);

}  // namespace sdurws::ird::kinematics

#endif  // IRD_KINEMATICS_WORKSPACE_HPP
