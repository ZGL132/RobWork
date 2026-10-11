/**
 * @file   Playback.hpp
 * @brief  轨迹会话域面三件（WP-16-T10——TRJ-07 领域承载）：动画播放查表
 *         （§14.5.1 trajectory.play/playback-seek 的数据面）、曲线只读投影
 *         （§14.5.1 trajectory.show-curve 的数据面）、路点局部重规划编排
 *         （§14.5.1 trajectory.local-replan 的域内执行面）。
 *
 * 设计依据：
 *   - units/trajectory.md §14.5.1（命令表 play/playback-seek 行——"驱动
 *     会话姿态（KIN-06 语义……四常量契约）……UI 线程（纯插值查表，零规
 *     划/碰撞/时间化计算）"；show-curve 行——"q/qd/qdd/时刻曲线；只读投
 *     影……UI 线程（读归档 payload 投影）"；local-replan 行——"对选中路
 *     段以冻结输入重新求值（新 RunId，新归档结果；不改既有结果、不产生
 *     修订）……worker"）、§14.5.2（"动画数据：Trajectory payload 的
 *     TimedSample/样条系数投影……播放＝按时刻插值查表驱动会话姿态"；"
 *     局部重规划的产品语义：新增一份归档结果并可在会话中与原轨迹比较
 *     （两份结果都是历史运行，不改历史）"）
 *   - units/trajectory.md §16.2（零修订与零计算红线——"UI 线程不得执行
 *     规划/碰撞/时间参数化：曲线/动画只读归档 payload"；AT-04 零修订）
 *   - units/trajectory.md §11.1/§11.2/§11.3（局部重规划的编排语义——平滑
 *     产物必触发复检〔强制边〕；复检面向 collisionCheckApplicable 段；
 *     复检检出→淘汰该平滑结果→回退该段原路径——R-TRJ-3"保留未平滑候选
 *     路径"；零变化零复检）、§6.3（段序号＝"诊断/复检/动画共用同一定位
 *     键"）、§5.5（smoothTolerance 双域/limitsScaleFactor 经请求字段消费
 *     ——调用方自 config.trj 解析传入，本域零默认值）、§12.7（motionLaw
 *     词表）、§15.8 请求面字段口径（sampleStepS/maxIterations——T08 返工
 *     S-2 提示"走请求面字段口径承接，勿另开第二渠道"）
 *   - 先例：dynamics Replay.hpp（WP-17-T08——§9.5 数据面"纯数据重组零动
 *     力学计算，UI 线程安全"的投影器形态；本头同构：查表/投影零规划零碰
 *     撞零时间化）
 *
 * ★ 布局表外增头登记（DTB §5.4——单元卡 §1.2 T10 注同步）：§4.2 布局表
 *   十九头之外增列本头（dynamics Replay.hpp 先例同款——§15.11 Commands
 *   零计算红线与数据面/编排面分离的结构承载；§14.5 三会话域面共用一头的
 *   分组＝命令表行序的自然映射，不再增设第三头）。
 *
 * ★ 零修订与线程口径（AT-04 的执行面落点）：
 *   - TrajectoryPlaybackTable/projectTrajectoryCurves：纯查表/只读投影，
 *     零副作用、零修订、零写盘（§15.0）；UI 线程安全（不执行任何规划/
 *     碰撞/时间化计算——§16.2 红线的结构性承载：本文件动画/曲线半区无
 *     此类设施的消费点）；会话姿态的驱动（三维视图回写）归 ui 装配层
 *     （P-TRJ-8 衔接缝未冻结——本头只交付数据，不虚构 ui 接口）。
 *   - localReplan：worker 计算面（纯函数编排——零写盘、零修订、零项目
 *     触碰；"新 RunId"由装配层接纳时分配，本编排只交付新结果记录）。
 *
 * 头文件依赖纪律（冒烟模式安全——Smooth/Recheck/TimeParam 同款）：本头对
 *   rw 只消费 header-only 数学头（Q）；对 policy 会话仅经 Recheck.hpp 的
 *   前向声明以 shared_ptr 借持；定义在实现 TU——src/Playback.cpp 为集成
 *   模式条件源（Q 构造面——查表插值帧/局部重规划产物构造，Ptp.cpp 同款
 *   gating，冒烟口径不受影响）。
 *
 * 线程安全：全部值类型并发只读安全；TrajectoryPlaybackTable 构造后不可变
 *   （并发只读安全）；projectTrajectoryCurves/localReplan 纯函数（§15.0），
 *   单线程使用。确定性：同输入等价输出（查表二分与插值公式确定——局部
 *   重规划的确定性由其复用的 smooth/recheckSegment/timeParameterize 三
 *   纯函数的确定性传递，NFR-COR-02）。
 */

#ifndef IRD_TRAJECTORY_PLAYBACK_HPP
#define IRD_TRAJECTORY_PLAYBACK_HPP

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <rw/math/Q.hpp>

#include <sdurws/ird/core/Identity.hpp>        // core::ObjectId（段来源投影——ARC-04）
#include <sdurws/ird/trajectory/Errors.hpp>    // TrajectoryError（fail-fast 载体）
#include <sdurws/ird/trajectory/KinematicsPort.hpp>  // IKinematicsComputePort 前向完整面（平滑 TCP 维可选消费）
#include <sdurws/ird/trajectory/Recheck.hpp>   // RecheckParameters/RecheckOutcome/IRepresentPointSampler/JointTypeKind＋policy 会话前向
#include <sdurws/ird/trajectory/Smooth.hpp>    // IPathGeometry/smooth/SmoothStatus/SmoothDeviationReport
#include <sdurws/ird/trajectory/TimeParam.hpp> // TimeParameterization/TimedSample/JointDynamicLimits/TimeParamResult
#include <sdurws/ird/trajectory/TrjTypes.hpp>  // CancelSignal/TrjSequenceIdentity

namespace sdurws::ird::trajectory {

// =====================================================================
// 动画播放查表（§14.5.1 play/playback-seek 数据面——UI 线程纯插值查表）
// =====================================================================

/**
 * @brief 单帧动画查询结果（§14.5.2"播放＝按时刻插值查表驱动会话姿态"的
 *        帧值载体）。
 *
 * 单位/坐标系（AGENTS §2.5）：q/qd/qdd 与 TimedSample 同源同单位（权威
 * 关节向量：转动 rad／移动 m；速度 rad/s 或 m/s；加速度 rad/s² 或 m/s²；
 * 逐轴按关节类型）；t 为自轨迹起点起算的时刻（s）。tcpPose 不在帧值内
 * ——TimedSample 注"动画的 TCP 视图归 T10 导出/动画批消费 FK 端口后另行
 * 产出"的承接口径：本查表面零 FK 消费（§16.2 UI 线程红线——FK 是计算
 * 设施，归 worker 面；曲线/姿态呈现直接以关节空间帧驱动）。值语义纯结构；
 * 线程安全。
 */
struct PlaybackFrame {
    /// 帧时刻（s；＝查询入参原值——非样本重整时刻）。
    double t = 0.0;
    /// 关节位置（插值查表产物；SI——rad|m 逐轴）。
    rw::math::Q q;
    /// 关节速度（插值查表产物；SI——rad/s 或 m/s）。
    rw::math::Q qd;
    /// 关节加速度（插值查表产物；SI——rad/s² 或 m/s²）。
    rw::math::Q qdd;
    /// 帧所属段序号（0 基——§6.3"诊断/复检/动画共用同一定位键"；仅当查
    /// 表构造时提供了段时长投影非空，nullopt＝未提供段归投影）。
    std::optional<std::uint32_t> segmentIndex;
    /// 是否命中精确样本（true＝t 与某样本时刻位级相等，帧值＝该样本位级
    /// 值——零插值漂移；false＝区间内线性插值）。
    bool atSample = false;
};

/**
 * @brief 轨迹动画播放查表（§14.5.2"TimedSample 投影＋按时刻插值查表"的
 *        域内承载——UI 线程安全的只读面）。
 *
 * 为什么是查表而不是样条求值（ITimeCurve）：§16.1/§16.2 红线明文"UI 线程
 * 只做投影、插值查表（动画）""UI 线程不得执行规划/碰撞/时间参数化"——
 * ITimeCurve::sampleAt 是时间化样条的连续求值（时间化计算设施，worker 面）；
 * 播放走归档 payload 的离散样本表做**相邻样本线性插值**（采样计划步长
 * sampleStepS 上界内插值误差受采样密度约束——payload 质量，非查表职责），
 * 本类结构性不消费任何求值器。检查对象与呈现对象的分工：C² 严格验证用
 * ITimeCurve（T08/复检面），会话呈现用本查表面——两视图同源（同一样本表）。
 *
 * 段归投影：构造可选携带 segmentDurationsS（§12.4 分段时长——与请求段序
 * 列逐条目对齐含驻留，TimeParamResult 同名输出透传）；提供时每帧计算
 * segmentIndex（§6.3 动画与诊断/复检共用定位键），缺席时 segmentIndex
 * 显式 nullopt（不伪造段归）。
 *
 * 生命周期：构造期深拷贝样本表与段归投影（值语义——UI 表可安全脱离原
 * payload 存活）；构造后不可变（并发只读安全）。
 */
class TrajectoryPlaybackTable {
public:
    /**
     * @brief 构造播放查表（payload 完整性校验——违约 fail-fast）。
     *
     * 校验序（确定性——同一坏 payload 必报同一首错，NFR-COR-02/COR-03）：
     *   1. timeParam.samples 非空（§6.2 不变量——空轨迹无动画语义）；
     *   2. 样本时刻非递减（§6.2 原文）；
     *   3. 首样本 t==0.0 且末样本 t==timeParam.totalDurationS（位级——
     *      T08 采样计划不变量"事件对齐点〔全部结点〕必含"的承接：首末结
     *      点 0/total 必为样本；payload 缺端点样本＝归档数据完整性违约）；
     *   4. 全样本 q/qd/qdd 维度一致且与首样本相同（变维＝payload 损坏）；
     *   5. segmentDurationsS 提供时：全部分量 >0（s）且 Σ==totalDurationS
     *      （位级——§12.4"Σ本向量＝totalDurationS"自洽；不等于＝投影与
     *      payload 不同源，拒绝混装）。
     *
     * @param timeParam         [in] 已归档 Completed 结果的时间参数化
     *                          payload（不从会话态构造——§15.13 前置同源
     *                          纪律；本构造深拷贝所需投影）
     * @param segmentDurationsS [in] 段时长投影（s；可空——见类注段归投影；
     *                          TimeParamResult.segmentDurationsS 原样透传）
     *
     * @throws TrajectoryError token "trajectory/playback/payload-*"——payload
     *         完整性违约（归档数据损坏属调用方数据契约违约，fail-fast）
     */
    TrajectoryPlaybackTable(
        const TimeParameterization& timeParam,
        const std::vector<double>& segmentDurationsS = {});

    /// 样本总数（无量纲计数；≥1——构造校验保证）。
    std::size_t sampleCount() const noexcept { return m_samples.size(); }

    /// 总时长（s；＝payload.totalDurationS——构造期快照）。
    double totalDurationS() const noexcept { return m_totalDurationS; }

    /**
     * @brief 查询时刻 t 的动画帧（相邻样本线性插值查表——UI 线程安全）。
     *
     * 执行序：① 取消无关（查表 O(log n) 短路径无取消面）；② t 合法域
     * 判定（[0, totalDurationS]，NaN/越界＝调用方违约 fail-fast——不钳
     * 制不静默，NFR-COR-03；UI 进度条应在调用前钳位到播放区间）；③ 样
     * 本二分定位（upper_bound 语义——右开区间 [t_i, t_{i+1})，t==末样本
     * 落末样本）；④ t 与 t_i 位级相等→返回样本 i 原值（atSample=true，
     * 零插值漂移——确定性回放锚）；⑤ 否则区间 [i,i+1] 内线性插值
     * α=(t−t_i)/(t_{i+1}−t_i)（分母>0——时刻严格递增段内；q/qd/qdd 逐
     * 轴独立插值）；⑥ 段归投影（提供时长投影时按累积边界半开区间 [cum_j,
     * cum_{j+1}) 判定，t==total 落末段）。
     *
     * @param t [in] 查询时刻，单位 s，∈[0, totalDurationS]
     *
     * @return 动画帧（PlaybackFrame——值语义）
     *
     * @throws TrajectoryError token "trajectory/playback/t-out-of-range"——
     *         t 非有限或越出 [0, totalDurationS]
     *
     * 确定性：同 t → 位级同输出（二分与插值公式全确定——NFR-COR-02）。
     */
    PlaybackFrame sampleAt(double t) const;

private:
    /// 样本表副本（时刻非递减——构造校验保证；值语义深拷贝）。
    std::vector<TimedSample> m_samples;
    /// 段归累积边界（s；仅构造提供段时长投影时非空：m_segmentCum[j]＝前 j
    /// 段时长之和，j=0..N——size=N+1，首元素恒 0，末元素恒 totalDurationS）。
    std::vector<double> m_segmentCum;
    /// 总时长快照（s）。
    double m_totalDurationS = 0.0;
};

// =====================================================================
// 曲线只读投影（§14.5.1 show-curve 数据面——读归档 payload 投影）
// =====================================================================

/**
 * @brief 曲线通道词表（§14.5.1"q/qd/qdd/时刻曲线"的封闭三值——时刻轴是
 *        每条曲线的横轴而非独立通道，故通道词表三值）。
 */
enum class TrajectoryCurveChannel : std::uint8_t {
    /// 关节位置 q（SI：rad|m 逐轴按关节类型）。
    Position,
    /// 关节速度 qd（SI：rad/s 或 m/s）。
    Velocity,
    /// 关节加速度 qdd（SI：rad/s² 或 m/s²）。
    Acceleration,
};

/**
 * @brief 单关节曲线序列（只读投影产物——归档样本表的逐关节视图）。
 *
 * timesS/values 逐点对齐且与样本表同序（时刻非递减——payload 不变量透
 * 传）；值语义纯结构；线程安全。
 */
struct TrajectoryCurveSeries {
    /// 关节序号（0 基链序）。
    std::uint32_t jointIndex = 0;
    /// 时刻轴（s；与 payload 样本逐一对应——非重采样）。
    std::vector<double> timesS;
    /// 通道值（单位随通道——TrajectoryCurveChannel 注）。
    std::vector<double> values;
};

/**
 * @brief 投影全部关节的曲线序列（§14.5.1 show-curve——纯读投影零计算）。
 *
 * 执行序：① payload 完整性校验（样本非空/时刻非递减/端点样本在位/全值
 * 有限——违约 fail-fast token "trajectory/curve/payload-*"；非有限值若
 * 静默进入曲线会沿投影链污染 UI 呈现与导出，NFR-COR-03）；② 逐关节抽取
 * 通道列（jointIndex 升序稳定——NFR-COR-02）。
 *
 * @param timeParam [in] 已归档 payload（只读借用，调用期存活即可）
 * @param channel   [in] 曲线通道（三值词表）
 *
 * @return 逐关节序列（关节数条，升序）
 *
 * @throws TrajectoryError payload 完整性违约（见执行序①）
 *
 * 纯函数；零副作用/零修订/零写盘；确定性。
 */
std::vector<TrajectoryCurveSeries> projectTrajectoryCurves(
    const TimeParameterization& timeParam, TrajectoryCurveChannel channel);

// =====================================================================
// 路点局部重规划（§14.5.1 local-replan 域内执行面——worker 编排）
// =====================================================================

/**
 * @brief 局部重规划的单段冻结输入（选中路段内逐段条目——运动段/驻留段
 *        二选一，TimeParamSegment 同款二分形态）。
 *
 * 字段即"冻结输入"的投影：waypoints/必经保护集/约束适用性/驻留时长/来源
 * 站全部取自原运行段计划（T04~T06 产物），调用方组装后本编排面将其视为
 * 不可变（请求值语义——编排内零回写；"不改既有结果"的结构承载＝全部输
 * 入 const 借用＋输出全新对象）。值语义纯结构；线程安全。
 */
struct LocalReplanSegmentInput {
    /// 段序号（0 基——与原运行 Trajectory.segments 全局定位键对齐，
    /// §6.3；选中路段闭区间内连续）。
    std::uint32_t segmentIndex = 0;
    /// 运动段折线路点（权威角 rad|m；≥2 个——T04~T06 产物投影；驻留段
    /// 恒空）。
    std::vector<rw::math::Q> waypoints;
    /// 必经路点保护集（waypoints 下标，严格升序去重——§5.6 必经状态的简
    /// 化保护投影：Start/TaskPoint/End 由调用方按路点 kind 落入；端点恒
    /// 保护无须列入；驻留段恒空）。
    std::vector<std::size_t> mandatoryWaypointIndices;
    /// 驻留时长（s；>0＝驻留段〔waypoints 必空〕；0＝运动段——§5.4 步
    /// 骤 3 工况 Dwell 投影）。
    double dwellDurationS = 0.0;
    /// 碰撞检查适用性（§6.2 SegmentConstraint.collisionCheckApplicable 投
    /// 影——true 的段平滑若有几何变化必触发复检〔§11.1 强制边〕）。
    bool collisionCheckApplicable = false;
    /// 来源任务点（失败素材/报告的 subject 投影——ARC-04；可空）。
    std::optional<core::ObjectId> sourceTaskPoint;
};

/**
 * @brief 局部重规划请求（§14.5.1 local-replan 的冻结输入面；前置违约一律
 *        fail-fast，token "trajectory/local-replan/..."）。
 *
 * 前置（逐项落点，校验序见 localReplan）：
 *   - 选中路段 [firstSegmentIndex, lastSegmentIndex] 闭区间非空（first≤
 *     last）；segments 与区间逐段对齐（size==last-first+1，segmentIndex
 *     连续无重）；
 *   - 运动段 waypoints ≥2 同维度全有限、必经保护集下标合法；驻留段
 *     waypoints 空且 dwellDurationS>0；相邻运动段端点构型**位级相等**
 *     （链连续性提前守卫——平滑端点强制不变故链性保持；违约即冻结输入
 *     本身损坏，提前拒绝省去无效平滑计算）；
 *   - jointTypes/lowerBoundQ/upperBoundQ/jointLimits 与段几何同维度；
 *     限位区间逐轴 lower<upper；jointLimits 逐轴合法域见 JointDynamicLimits；
 *   - limitsScaleFactor ∈ (0,1]（§5.5）；smoothToleranceJoint/Tcp 有限>0
 *     （§5.5 双域）；smoothMaxIterations ≥1；
 *   - recheckParams 过 validateRecheckParameters（§15.6 R-POL-5 同款——
 *     越顶/非法拒绝）；任一段 collisionCheckApplicable==true 时 collision-
 *     Session 与 representPointSampler 必须非空（协议可执行前置——§11.2；
 *     调用方应改标不适用或提供会话，不允许"声明适用却不给协议"）；
 *   - motionLawToken ∈ 封闭词表（§12.7）；timeParamSampleStepS >0 有限、
 *     timeParamMaxIterations ≥1（§15.8 请求面字段口径——T08 返工 S-2 提
 *     示承接：与 TimeParamRequest 同名同义字段直传，零第二渠道）；
 *   - frozenIdentity 为原运行的冻结身份组（新结果绑定同快照/切片——
 *     §14.5.2"两份结果都是历史运行"；本编排只回显不校验其有效性——快照
 *     判定归 evidence）；
 *   - cancel 可空（空＝不可取消）。
 *
 * 单位/坐标系：构型为权威关节向量（rad|m）；时间一律 s。值语义纯结构；
 * 线程安全（借用指针/共享会话在调用期存活即可）。
 */
struct LocalReplanRequest {
    /// 原运行冻结身份组（回显进结果——新归档结果的身份绑定锚，§14.5.2）。
    TrjSequenceIdentity frozenIdentity;
    /// 选中路段首段序号（0 基，闭区间）。
    std::uint32_t firstSegmentIndex = 0;
    /// 选中路段末段序号（0 基，闭区间；≥first）。
    std::uint32_t lastSegmentIndex = 0;
    /// 选中路段逐段冻结输入（与区间逐段对齐——LocalReplanSegmentInput 注）。
    std::vector<LocalReplanSegmentInput> segments;
    /// 逐轴关节类型（P-06 步长分类键——与构型维度等长）。
    std::vector<JointTypeKind> jointTypes;
    /// 评价区间下界（逐轴 rad|m——限位复检与限值校验共用投影，§5.3）。
    rw::math::Q lowerBoundQ;
    /// 评价区间上界（逐轴 rad|m）。
    rw::math::Q upperBoundQ;
    /// 逐关节限值视图（CanonicalJoint 投影——JointDynamicLimits 注）。
    std::vector<JointDynamicLimits> jointLimits;
    /// 限值使用比例（无量纲，(0,1]——§5.5）。
    double limitsScaleFactor = 1.0;
    /// 关节维平滑几何保持容差（rad|m——§5.5 smoothToleranceJoint 投影）。
    double smoothToleranceJoint = 0.0;
    /// TCP 派生维平滑几何保持容差（m——§5.5 smoothToleranceTcp 投影）。
    double smoothToleranceTcp = 0.0;
    /// 平滑收缩重试上限（无量纲计数 ≥1——§15.7"迭代上限（配置，进身份）"）。
    std::uint32_t smoothMaxIterations = 1;
    /// 复检协议数值（policy/P-06 来源引用——validate 必过；ARC-05 零副本）。
    RecheckParameters recheckParams;
    /// policy 碰撞会话（唯一碰撞权威——R-5；存在适用段时必填）。
    std::shared_ptr<const policy::CollisionEvaluationSession> collisionSession;
    /// 代表点集采样器（P-06 行 9 规则物化——存在适用段时必填）。
    const IRepresentPointSampler* representPointSampler = nullptr;
    /// 运动律 token（§12.7 封闭词表——§15.8 请求面字段口径直传）。
    std::string motionLawToken{kTimeParamMethodQuinticSplineC2};
    /// 时间化采样步长上限（s；>0 有限——§15.8 sampleStepS 同名直传，T08
    /// 返工 S-2 提示"请求面字段口径承接"）。
    double timeParamSampleStepS = 0.0;
    /// 时间化缩放迭代上限（≥1——§15.8 maxIterations 同名直传）。
    std::uint32_t timeParamMaxIterations = 0;
    /// FK 端口（可空——平滑 TCP 派生维检查的消费面；缺席＝检查显式
    /// NotApplicable 不冒充——SmoothDeviationReport.tcpChecked）。
    IKinematicsComputePort* fkPort = nullptr;
    /// 取消观测（可空＝不可取消；段级循环边界轮询——§15.1 纪律）。
    CancelSignal cancel;
};

/**
 * @brief 单段局部重规划报告（段级处置记录——证据/呈现的素材面）。
 *
 * 值语义纯结构；线程安全。
 */
struct LocalReplanSegmentReport {
    /// 段序号（＝对应请求段 segmentIndex）。
    std::uint32_t segmentIndex = 0;
    /// 平滑结局（五态——SmoothStatus；驻留段无平滑语义，取 NoChange 占位
    /// 值并以 recheckSkipped="dwell-segment" 标注承载——见 recheckSkipReason
    /// 注；运动段恒为 smooth 实际结局）。
    SmoothStatus smoothStatus = SmoothStatus::Canceled;
    /// 几何是否变化（Smoothed＝true——§11.1 复检强制边的触发事实）。
    bool geometryChanged = false;
    /// 是否执行了复检（true 时 recheck 有效）。
    bool recheckExecuted = false;
    /// 复检结论（§15.6 三态＋账目——仅 recheckExecuted 时有效）。
    std::optional<RecheckOutcome> recheck;
    /// 是否跳过复检（零变化沿用既有结论／碰撞不适用段——§11.1"零变化零
    /// 复检"与 §11.2 协议范围口径的显式标注，不冒充"已复检"）。
    bool recheckSkipped = false;
    /// 跳过原因（recheckSkipped 时非空："no-geometry-change"＝平滑零变
    /// 化/作废回退原路径〔沿用原运行该段复检结论〕；"collision-not-
    /// applicable"＝无碰撞验证义务段〔安全由线性凸组合性质＋时间化限值
    /// 校验承担——§11.2 范围口径〕；"dwell-segment"＝驻留段无平滑/复检
    /// 语义〔时间轴常值段——§9.1〕；该字段是编排处置的透明账目，非诊断
    /// 码）。
    std::string recheckSkipReason;
    /// 平滑产物是否被复检淘汰而回退原路径（true＝该段最终几何＝原折线
    /// ——§11.1 分支二"淘汰该平滑结果"＋R-TRJ-3"保留未平滑候选路径"）。
    bool smoothRejectedFallback = false;
    /// 平滑几何保持偏差记录（运动段恒携带实测值——§11.4 证据素材面）。
    SmoothDeviationReport deviation;
    /// 该段最终采用的几何结点数（无量纲计数；驻留段 0——报告核对锚）。
    std::size_t adoptedNodeCount = 0;
};

/**
 * @brief 局部重规划编排结局词表（封闭二态）。
 */
enum class LocalReplanStatus : std::uint8_t {
    /// 编排完成（逐段处置＋时间化产物齐备——失败语义随 rangeTimeParam.
    /// status 的四态词表，不另设"失败"顶层态：失败是素材轨非编排错误）。
    Ok,
    /// 取消观测命中（UX-03——零错误素材、零产物；已完成段报告保留）。
    Canceled,
};

/**
 * @brief 局部重规划结局（§14.5.1 local-replan 的域内产物——新结果记录）。
 *
 * 状态—字段联动（实现保证）：
 *   - Ok：segmentReports 全段齐备；rangeTimeParam 为 timeParameterize 的
 *     完整产物（status==Ok 语义随其四态词表——Ok 时含 TimeParameterization/
 *     segmentDurationsS/峰值/曲线；LimitUnreachable/LawFailed 时为失败素
 *     材轨不伪造节拍——§12.4）；anySmoothFallback 如实标注；
 *   - Canceled：segmentReports 为取消前已完成段（UX-03 零错误素材）；
 *     rangeTimeParam 为空产物（零伪造）；其余字段不携带有效值。
 *
 * "新 RunId，新归档结果"的边界（诚实登记）：RunId 分配与归档写盘归
 * execution 接纳层（§14.1.1 提交链），本编排零写盘零项目触碰——交付的是
 * **新结果记录的域内容**（frozenIdentity 回显＋区间定位＋逐段报告＋时间
 * 化产物）；装配层接纳后即与原轨迹在会话中并存比较（§14.5.2）。
 * 值语义纯结构；线程安全。
 */
struct LocalReplanOutcome {
    /// 编排结局（Ok/Canceled 二态——取消非错误，UX-03）。
    LocalReplanStatus status = LocalReplanStatus::Canceled;
    /// 逐段处置报告（与请求段一一对应；取消时为已完成前缀）。
    std::vector<LocalReplanSegmentReport> segmentReports;
    /// 选中路段时间化产物（timeParameterize 直传结果——四态随其词表）。
    TimeParamResult rangeTimeParam;
    /// 任一段发生"平滑产物淘汰回退原路径"（§11.1 分支二的事实汇总——
    /// 证据/呈现面的披露锚；恒 false＝全段平滑产物均被采纳或无平滑变化）。
    bool anySmoothFallback = false;
    /// 冻结身份回显（＝request.frozenIdentity——新结果与原运行同源绑定）。
    TrjSequenceIdentity frozenIdentity;
    /// 区间首段序号（回显 request.firstSegmentIndex）。
    std::uint32_t rangeFirstSegmentIndex = 0;
    /// 区间末段序号（回显 request.lastSegmentIndex）。
    std::uint32_t rangeLastSegmentIndex = 0;
};

/**
 * @brief 路点局部重规划编排（§14.5.1 local-replan 域内执行面——worker
 *        纯函数编排；TRJ-07"路点局部重规划"的领域实现）。
 *
 * 执行序（每步语义见行内注释；复用面＝T07 smooth/recheckSegment＋T08
 * timeParameterize 三纯函数——本编排零新算法，只做 §11.1 强制边与段级
 * 处置的装配）：
 *   1. 前置校验（LocalReplanRequest 前置全表——违约抛 TrajectoryError
 *      fail-fast，token "trajectory/local-replan/..."）；
 *   2. 取消轮询（命中 → Canceled，零素材）；
 *   3. 逐段处置（区间序）：
 *      a. 驻留段→报告标注（无平滑/复检语义），直接进时间化段表；
 *      b. 运动段→smooth（waypoints/必经保护/双域容差/迭代上限/fkPort/
 *         cancel——§15.7）；
 *      c. Smoothed→复检强制边：collisionCheckApplicable==true 时
 *         recheckSegment（被检几何＝平滑产物；必检参数＝保留结点均匀
 *         s_k=k/(m-1)——IPathGeometry 插值性契约；R-POL-5 数值零私设），
 *         结论≠Passed→淘汰该平滑产物回退原折线（smoothRejectedFallback
 *         ——§11.1 分支二/R-TRJ-3），==false 时 skip 标注
 *         "collision-not-applicable"（§11.2 范围口径）；
 *      d. 非 Smoothed（NoChange/作废/放弃）→几何回原折线（零变化沿用
 *         既有复检结论——§11.1），skip 标注 "no-geometry-change"；
 *   4. 组装时间化段表（运动段 geometry＝最终采纳几何＋interiorS＝保留
 *      内部结点 s 参数；驻留段＝dwell 条目）→timeParameterize（§15.8
 *      请求面字段直传）；
 *   5. 组装 LocalReplanOutcome（Ok；frozenIdentity/区间回显）。
 *
 * 本函数不消费 project/io/ui（零写盘/零修订/零会话驱动——AT-04）；不判
 * 任务可行性（N7——复检结论/限值素材随结果交证据面）；"不改既有结果"
 * 由全部输入 const 借用＋输出全新对象结构性承载。
 *
 * @param request [in] 局部重规划请求（前置见 LocalReplanRequest 注；违约
 *                抛 TrajectoryError——fail-fast）
 *
 * @return 局部重规划结局（LocalReplanOutcome 二态）
 *
 * @throws TrajectoryError 调用方契约违约（token "trajectory/local-replan/
 *         ..."——区间/段表/维度/容差/协议数值/词表/步长/迭代上限/链连续
 *         性/协议可执行前置等；见各校验点）
 *
 * 纯函数（零副作用/零修订/零写盘）；线程安全（单线程使用）；确定性（复
 * 用三纯函数的确定性＋本编排的固定处置序——NFR-COR-02）。
 */
LocalReplanOutcome localReplan(const LocalReplanRequest& request);

}  // namespace sdurws::ird::trajectory

#endif  // IRD_TRAJECTORY_PLAYBACK_HPP
