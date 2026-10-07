/**
 * @file   Sequence.hpp
 * @brief  任务序列展开（§5.4——TRJ-01"由任务点组成的有序作业序列"的领域
 *         语义）：sequenceKey 前驱名拓扑排序＋段链展开（approach→work→
 *         retract＋站间 PTP）＋构型选择接入（WP-16-T04 批）。
 *
 * 设计依据：
 *   - units/trajectory.md §5.2（需求投影——任务点集消费面与 R-1 合规：
 *     "解码实现落位口径随 kinematics 既有消费先例……trajectory 跟随同一
 *     裁决"）、§5.4（任务序列展开算法四步骤原文）、§5.6（起始/终止状态
 *     为必经状态）、§7.2（PTP 起点/终点来源）、§7.6（失败语义——环/悬空
 *     键/无启用点→TRJ-INPUT-INVALID；站无解→TRJ-NO-PATH）
 *   - units/requirements.md §5.1（sequenceKey＝前驱条目**名**引用；顺序
 *     边 prev→this）、I-REQ-7/R7（顺序无环、无重复键——分支＝重复、
 *     回边＝环、缺席前驱＝悬空）、§13 行"trajectory 消费：sequenceKey
 *     顺序＋approach/work/retract 段"
 *   - 需求 TRJ-01、NFR-COR-02（展开序确定性）、REQ-02（approach/retract
 *     方向语义归 requirements——本域只消费解析投影）
 *   - 先例：kinematics Evidence.hpp O-37 注入值纪律（R-1 禁 include——
 *     本头定义自有最小投影值 SequenceTaskPoint，宿主把 req-point-set
 *     对象解析后注入，单元侧只消费投影值）
 *   - 任务契约 tasks/foundation/WP-16-T04.json（acceptance 1——任务点
 *     有序作业序列模型测试）
 *
 * 背景说明（展开的产出是什么、什么在 T04 不可达——如实登记）：
 *   - 展开产物＝§5.4 第 4 条的**段计划**（PlannedSegment 序列）：只含
 *     几何与约束，不含时间（时间在 §12/WP-16-T08）。
 *   - 站间 PTP 段的几何在"纯关节序列"（全部站 approach/retract 禁用）
 *     下由本展开器直接规划填充（构型选择 §7.5＋planPtpSegment 语义）；
 *     站 approach 启用时，该站前 PTP 段的终点＝approach 起点（接近点）
 *     构型——依赖笛卡尔段 IK（WP-16-T05 §8.2），该 PTP 段与 approach/
 *     retract 段的计划条目结构完整（序号/类型/来源/约束/lineSpec 齐备）
 *     而几何留待 T05 填充——本批如实标记几何未填充，不伪造端点。
 *   - 取消（UX-03）：展开循环边界轮询取消观测，命中＝cancelled 语义
 *     （零错误素材），不是失败。
 *
 * 线程安全：纯函数/值类型；展开器无跨调用状态（§15.0）。确定性：拓扑
 * 序并列消解与无键站排序均按 name 字典序（实现决策——卡面未定的确定性
 * 补全，登记于单元卡 §21.5；同输入恒同展开序——NFR-COR-02）。
 */

#ifndef IRD_TRAJECTORY_SEQUENCE_HPP
#define IRD_TRAJECTORY_SEQUENCE_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <rw/math/Q.hpp>

#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/trajectory/Ptp.hpp>
#include <sdurws/ird/trajectory/TrjTypes.hpp>

namespace sdurws::ird::trajectory {

// =====================================================================
// 投影注入值（O-37 纪律——R-1 禁 include requirements，词表镜像）
// =====================================================================

/**
 * @brief 需求等级两值词表镜像（requirements::RequirementLevel——REQ-06；
 *        枚举值序一致，语义权威归 requirements 卡 §4.3）。
 */
enum class SequenceTaskLevel : std::uint8_t {
    /// Must——违例候选进 mustViolations 素材（判定归 evidence）。
    Must,
    /// Should——违例候选进 shouldViolations 素材。
    Should,
};

/**
 * @brief 进退轴两值词表镜像（requirements SegmentAxis §4.3；§5.1 进退
 *        方向语义归 requirements：approach 沿轴负向趋近、retract 沿轴
 *        正向离开——本域只承载方向投影，不重解释）。
 */
enum class SequenceSegmentAxis : std::uint8_t {
    /// 工具 Z 轴（ToolZ）。
    ToolZ,
    /// 参考系 Z 轴（ReferenceZ）。
    ReferenceZ,
};

/**
 * @brief 接近/撤离段解析投影（requirements TaskSegment{enabled, axis,
 *        distanceM} 的域内镜像——§5.2 投影表行）。
 *
 * 笛卡尔直线段的几何要素（轴向＋段长）；直线几何构造与沿途 IK 归
 * WP-16-T05（§8）——本结构只承载展开所需的解析产物。
 *
 * 合法域：enabled==true 时 distanceM >0（m；requirements 启用校验已保证，
 * 展开器复验——NFR-COR-03）。值语义纯结构；线程安全。
 */
struct SequenceTaskSegment {
    /// 段启用（false＝该站无此段——展开跳过，不产生计划条目）。
    bool enabled = false;
    /// 进退轴（词表镜像——枚举注）。
    SequenceSegmentAxis axis = SequenceSegmentAxis::ToolZ;
    /// 段距离（m；启用时必须 >0）。
    double distanceM = 0.0;

    bool operator==(const SequenceTaskSegment& o) const noexcept
    {
        return enabled == o.enabled && axis == o.axis && distanceM == o.distanceM;
    }
    bool operator!=(const SequenceTaskSegment& o) const noexcept { return !(*this == o); }
};

/**
 * @brief 任务点投影注入值（req-point-set 启用/停用条目的宿主解析投影
 *        ——O-37 同款纪律：本域不消费 requirements 头，宿主解析后注入；
 *        kinematics Evidence.hpp 先例同构）。
 *
 * 位姿/容差/TCP 解析等字段不在本结构（它们是 IK 求解面 WP-15 的输入；
 * 展开面只消费顺序与段链所需子集）——投影最小化，防"顺手解码"越权。
 * 值语义纯结构；线程安全。
 */
struct SequenceTaskPoint {
    /// 任务点对象身份（req.points 闭包内；段来源追溯——NFR-COR-04）。
    core::ObjectId pointOid;
    /// 语义名（集合内唯一；sequenceKey 的引用目标——requirements §5.1）。
    std::string name;
    /// 需求等级（词表镜像——Must/Should）。
    SequenceTaskLevel level = SequenceTaskLevel::Must;
    /// 启用标记（false＝不参与展开，但仍是顺序关系/悬空判定的全集成员
    /// ——顺序是全集语义，requirements checkSequence 先例）。
    bool enabled = true;
    /// 顺序键＝前驱条目名（nullopt＝不参与顺序约束——并行/无序条目；
    /// 悬空/重复/环校验见 expandSequence）。
    std::optional<std::string> sequenceKey;
    /// 接近段投影（approach 沿轴负向趋近——方向语义归 requirements）。
    SequenceTaskSegment approach;
    /// 撤离段投影（retract 沿轴正向离开）。
    SequenceTaskSegment retract;

    bool operator==(const SequenceTaskPoint& o) const
    {
        return pointOid == o.pointOid && name == o.name && level == o.level
            && enabled == o.enabled && sequenceKey == o.sequenceKey
            && approach == o.approach && retract == o.retract;
    }
    bool operator!=(const SequenceTaskPoint& o) const { return !(*this == o); }
};

/**
 * @brief 单站的展开输入（任务点投影＋该站 IK 候选解集＋驻留时长投影）。
 *
 * 候选解集（PtpCandidate）由 KinematicsPort 适配层投影注入（WP-16-T05
 * 落位端口；本批测试直构）；空集＝该站无解——展开终止并产出 TRJ-NO-PATH
 * 素材（§7.6 行 5"IK 无解→段失败定位"，用户数据面失败——不是异常）。
 * 驻留时长＝工况 Dwell{stationRef, durationS} 事件的投影（§5.4 步骤 3；
 * nullopt＝无驻留；有值必须 >0，单位 s）。
 */
struct SequenceStationInput {
    /// 任务点投影（SequenceTaskPoint——O-37 注入值）。
    SequenceTaskPoint point;
    /// 该站 IK 候选解集（可为空＝无解；构型选择三键——PtpCandidate 注）。
    std::vector<PtpCandidate> candidates;
    /// 驻留时长（s，>0；nullopt＝无驻留——工况 Dwell 事件投影）。
    std::optional<double> dwellDurationS;
};

// =====================================================================
// 展开产物（§5.4 第 4 条段计划——有序段列表，只含几何与约束）
// =====================================================================

/**
 * @brief 展开段的角色词表（§5.4 步骤 2 段链四形态——域内承载词表）。
 */
enum class SequenceSegmentRole : std::uint8_t {
    /// 站间关节空间 PTP 连接段（§7——JointLinear）。
    TransferPtp,
    /// 接近段（approach——沿轴负向趋近；CartesianLine，WP-16-T05 几何）。
    Approach,
    /// 作业段＝任务点本身（§5.2"作业段＝任务点本身"——以 Work 条目承载
    /// 任务点路点与驻留时长，不产生独立几何段）。
    Work,
    /// 撤离段（retract——沿轴正向离开；CartesianLine，WP-16-T05 几何）。
    Retract,
};

/**
 * @brief 展开产物条目（§5.4"段序号、空间类型、几何要素、来源任务点
 *        ObjectId、约束"的值承载）。
 *
 * 不变量（expandSequence 产出保证）：segmentIndex 全序列 0 基连续；
 * role/spaceType/lineSpec/ptpGeometry/dwellDurationS 的组合一致：
 *   - TransferPtp ⇒ spaceType==JointLinear、lineSpec 恒空；
 *     ptpGeometry 有值＝几何已规划（纯关节序列），nullopt＝端点构型
 *     依赖笛卡尔段 IK 产出（WP-16-T05 填充——如实标记不伪造）；
 *   - Approach/Retract ⇒ spaceType==CartesianLine、lineSpec 必填、
 *     ptpGeometry 恒空（T05 填充）；
 *   - Work ⇒ dwellDurationS 有值＝该站驻留（s，>0）、nullopt＝无驻留。
 *
 * 值语义纯结构；线程安全（并发只读）。
 */
struct PlannedSegment {
    /// 段序号（0 基，全序列连续——§6.3 定位键）。
    std::uint32_t segmentIndex = 0;
    /// 段角色（段链四形态——SequenceSegmentRole）。
    SequenceSegmentRole role = SequenceSegmentRole::TransferPtp;
    /// 空间类型（§6.2 封闭词表——由 role 决定：TransferPtp⇒JointLinear，
    /// Approach/Retract⇒CartesianLine；Work 条目随站间段类型对齐——
    /// 纯关节序列恒 JointLinear）。
    SegmentSpaceType spaceType = SegmentSpaceType::JointLinear;
    /// 来源任务点（approach/work/retract/到达站归属——§6.2 sourceTaskPoint；
    /// Work 条目恒填，其余条目填其服务的站）。
    core::ObjectId sourceTaskPoint;
    /// 段级约束（config.trj/policy 投影——SegmentConstraint 零副本纪律）。
    SegmentConstraint constraint;
    /// 笛卡尔段几何要素（仅 Approach/Retract——轴向＋段长，T05 消费）。
    std::optional<SequenceTaskSegment> lineSpec;
    /// 已规划的 PTP 段几何（仅 TransferPtp 且端点构型可解析时——§7
    /// planPtpSegment 语义产物；nullopt＝待 T05 填充）。
    std::optional<TrajectorySegment> ptpGeometry;
    /// 驻留时长（仅 Work；s，>0；nullopt＝无驻留——§5.4 步骤 3）。
    std::optional<double> dwellDurationS;

    bool operator==(const PlannedSegment& o) const
    {
        return segmentIndex == o.segmentIndex && role == o.role
            && spaceType == o.spaceType && sourceTaskPoint == o.sourceTaskPoint
            && constraint == o.constraint && lineSpec == o.lineSpec
            && ptpGeometry == o.ptpGeometry && dwellDurationS == o.dwellDurationS;
    }
    bool operator!=(const PlannedSegment& o) const { return !(*this == o); }
};

/**
 * @brief 序列展开产物（§5.4 展开算法的出口——段计划＋站序＋失败素材）。
 *
 * 三态语义（§6.7 失败表达——展开是域中间态构建，不做工程判定）：
 *   - failures 为空＝展开成功（segments 为完整段链）；
 *   - failures 非空＝展开终止（segments 为终止前已完成部分——可追溯；
 *     素材 reasonToken 归 TRJ-* 码常量，判定归 evidence/评估器）。
 * canceled==true＝取消观测命中（UX-03——取消不是错误，failures 恒空）。
 *
 * 值语义纯结构；线程安全（并发只读）。
 */
struct SequencePlan {
    /// 有序段列表（§5.4 第 4 条；成功展开时非空且序号连续）。
    std::vector<PlannedSegment> segments;
    /// 拓扑排序后的启用站序（pointOid 序——可追溯性与站序复核面）。
    std::vector<core::ObjectId> stationOrder;
    /// 展开终止素材（TRJ-INPUT-INVALID/TRJ-NO-PATH——失败语义见
    /// expandSequence；成功展开恒空）。
    std::vector<FailedSegmentRecord> failures;
    /// 取消标记（true＝取消观测命中——零素材，非错误；UX-03）。
    bool canceled = false;
};

// =====================================================================
// 展开请求与入口
// =====================================================================

/**
 * @brief 序列展开请求（值语义；全部关节向量＝权威角 rad|m）。
 *
 * 合法域（fail-fast——调用方契约违约）：startQ/lowerBoundQ/upperBoundQ
 * 同维度且全分量有限；lower<upper 逐轴；tcpRef 合法；constraint 合法；
 * ikContinuityThreshold 有限且 >0；stationInput 内 pointOid 均合法、
 * name 非空且两两互异（语义名唯一性——requirements I-REQ-3 集合级；
 * 违名面由宿主保证，展开器复验防误用）。
 */
struct SequenceRequest {
    /// 起始构型（startStateRef 解析产物——宿主注入；§5.3 不得取会话姿态）。
    rw::math::Q startQ;
    /// 评价区间下界（逐轴；rad|m）。
    rw::math::Q lowerBoundQ;
    /// 评价区间上界（逐轴；rad|m）。
    rw::math::Q upperBoundQ;
    /// 会话 TCP 对象引用（§5.3 TCP/工具——ARC-04 对象 ID）。
    core::ObjectId tcpRef;
    /// 参考系对象（World 时 nullopt——§6.3 不伪造默认）。
    std::optional<core::ObjectId> frameRef;
    /// 段级约束（展开产出的各段统一携带——config.trj/policy 投影）。
    SegmentConstraint constraint;
    /// 构型延续阈值（rad|m 逐轴上界——config.trj.ikContinuityThreshold）。
    double ikContinuityThreshold = 1e-6;
    /// 站输入集（全部任务点投影——含禁用站：顺序关系与悬空判定按全集
    /// 语义，requirements checkSequence 先例；展开只排 enabled 子集）。
    std::vector<SequenceStationInput> stations;
    /// 取消观测（可空＝不可取消；站级循环边界轮询——§15.0/§15.1）。
    CancelSignal cancel;
};

/**
 * @brief 任务序列展开（§5.4 四步骤的 T04 实现——TRJ-01 有序作业序列）。
 *
 * 执行序（逐步语义见行内注释；失败即终止并产出素材——不自行修复顺序，
 * §5.4 步骤 1 原文）：
 *   1. 前置校验（fail-fast——维度/区间/TCP/约束/站名唯一性违约）；
 *   2. 取消轮询（命中→canceled==true 收尾）；
 *   3. 启用站计数（0 站→TRJ-INPUT-INVALID 素材终止——§7.2"序列无启用
 *      任务点"（非不可行））；
 *   4. 顺序关系校验（I-REQ-7 三类，全集语义：悬空键（引用缺席名）/
 *      重复键（同一前驱被两条目声明）/环（Kahn 消去剩余）→任一命中即
 *      TRJ-INPUT-INVALID 素材终止——§5.4"对残余环/悬空键给素材并终止"）；
 *   5. 拓扑排序（Kahn；并列/无键站按 name 字典序消解——确定性补全），
 *      取 enabled 子序为展开站序；
 *   6. 逐站展开段链：站间 PTP（纯关节站可规划几何；approach 启用站的
 *      到达段几何待 T05）→ approach（若启用）→ work（任务点＋驻留）→
 *      retract（若启用）；构型游标＝最近一个"构型已解析"的段端点；
 *   7. 站候选空集→TRJ-NO-PATH 素材终止（§7.6 行 5）。
 *
 * 展开的构型选择过程已由各 PTP 段的 TrajectorySegment 承载（选定构型
 * ＝段终点路点）；候选解引用的进一步记录（解集 stableIndex 入证据）随
 * 评估器组装面（WP-16-T10）。
 *
 * @param request [in] 展开请求（合法域见 SequenceRequest 注）
 * @return 展开产物（成功/终止/取消三态——SequencePlan 注）
 *
 * @throws TrajectoryError 调用方契约违约（token "trajectory/sequence/..."
 *         ——维度/区间/TCP/约束/站名/站身份非法；用户数据面失败走素材
 *         轨不抛——§15.0 错误二分）
 *
 * 纯函数；无副作用/零修订/零写盘；线程安全；确定性（同输入同展开序）。
 */
SequencePlan expandSequence(const SequenceRequest& request);

}  // namespace sdurws::ird::trajectory

#endif  // IRD_TRAJECTORY_SEQUENCE_HPP
