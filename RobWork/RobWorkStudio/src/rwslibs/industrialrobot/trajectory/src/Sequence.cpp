/**
 * @file   Sequence.cpp
 * @brief  任务序列展开的实现翻译单元——I-REQ-7 顺序校验（悬空/重复/环）、
 *         Kahn 拓扑排序与站级段链展开（WP-16-T04 批）。
 *
 * 设计依据：
 *   - units/trajectory.md §5.4（展开算法四步骤）、§7.2（零长段/无启用点）、
 *     §7.5（构型选择）、§7.6（失败语义表——各行处置与语义归类）、§15.0
 *     （错误二分：调用方违约 fail-fast／用户数据失败走素材）
 *   - units/requirements.md §5.1/I-REQ-7（sequenceKey＝前驱条目名；分支＝
 *     重复、回边＝环、缺席前驱＝悬空——与 requirements checkSequence
 *     实现同口径，两域一致性由各自测试锚定同一卡面语义）
 *   - 需求 TRJ-01、NFR-COR-02/03
 *
 * 确定性：拓扑并列与无键站序按 name 字典序（std::set 迭代序；实现决策
 * ——NFR-COR-02 确定性补全）；失败素材构造序固定；同输入恒同产物。
 */

#include <sdurws/ird/trajectory/Sequence.hpp>

#include <sdurws/ird/trajectory/DiagCodes.hpp>
#include <sdurws/ird/trajectory/Errors.hpp>

#include <algorithm>
#include <map>
#include <set>
#include <utility>

namespace sdurws::ird::trajectory {

// 展开阶段 token（FailedSegmentRecord.phaseToken——序列展开终止发生在
// 一切段规划之前；§14.1.6 进度词表六 token 均为段级阶段，本 token 为
// 全轨迹级展开阶段的域内承载，词表增补随本批登记单元卡 §21.5）。
static const char kPhasePlanSequence[] = "plan-sequence";

namespace {

/// 有限性判定（NaN 与 ±inf 均拒绝——NFR-COR-03）。
bool isFiniteDouble(double v)
{
    return v == v && v - v == 0.0;
}

/**
 * @brief 构造全轨迹级输入非法素材（TRJ-INPUT-INVALID——汇总①级，§14.4
 *        行 1）。
 *
 * 全轨迹级失败＝segmentIndex 特殊值 0xFFFFFFFF（§6.2 原文）；phase=展开
 * 阶段（kPhasePlanSequence）；素材不携带比较型字段（非法性不是量值
 * 超限——UX-03 比较型仅超限类必填）。
 *
 * @param cause    [in] 中文原因（含定位信息——环/悬空/重复的具体键值）
 * @param action   [in] 中文建议动作
 */
FailedSegmentRecord makeInputInvalidRecord(std::string cause, std::string action)
{
    FailedSegmentRecord rec;
    rec.segmentIndex = 0xFFFFFFFFu;              // 全轨迹级失败特殊值（§6.2）
    rec.phaseToken = kPhasePlanSequence;         // 展开阶段（非段级）
    rec.reasonToken = std::string(kTrjInputInvalid);  // 码常量——禁字符串拼码
    rec.cause = std::move(cause);
    rec.recommendedAction = std::move(action);
    return rec;
}

}  // namespace

// =====================================================================
// 序列展开（执行序见 Sequence.hpp 函数注——七步）
// =====================================================================

SequencePlan expandSequence(const SequenceRequest& request)
{
    SequencePlan plan;

    // ---- 第 1 步：前置校验（调用方契约违约 → fail-fast）----
    const std::size_t dof = static_cast<std::size_t>(request.startQ.size());
    if (dof == 0U) {
        throw TrajectoryError("trajectory/sequence/empty-q",
                              "起始构型维度为 0（无自由度链无序列语义）");
    }
    for (std::size_t i = 0; i < dof; ++i) {
        if (!isFiniteDouble(request.startQ[i])) {
            throw TrajectoryError("trajectory/sequence/nonfinite-start",
                                  "起始构型含非有限分量（轴 " + std::to_string(i) + "）");
        }
        // 评价区间逐轴核验（lower<upper——与 PTP 守卫同一判定基准）。
        if (static_cast<std::size_t>(request.lowerBoundQ.size()) != dof
            || static_cast<std::size_t>(request.upperBoundQ.size()) != dof) {
            throw TrajectoryError("trajectory/sequence/bounds-dim",
                                  "评价区间维度与起始构型不一致（lower="
                                      + std::to_string(request.lowerBoundQ.size())
                                      + "，upper="
                                      + std::to_string(request.upperBoundQ.size())
                                      + "，startQ=" + std::to_string(dof) + "）");
        }
        if (!(request.lowerBoundQ[i] < request.upperBoundQ[i])) {
            throw TrajectoryError("trajectory/sequence/bounds-order",
                                  "评价区间上下界须逐轴 lower<upper（轴 "
                                      + std::to_string(i) + "）");
        }
    }
    if (!request.tcpRef.isValid()) {
        throw TrajectoryError("trajectory/sequence/tcp-ref",
                              "tcpRef 必须为合法对象身份（§6.3——ARC-04）");
    }
    if (!(request.constraint.limitsScaleFactor > 0.0
          && request.constraint.limitsScaleFactor <= 1.0)) {
        throw TrajectoryError("trajectory/sequence/constraint-scale",
                              "段约束 limitsScaleFactor 须 ∈(0,1]，实际 "
                                  + std::to_string(request.constraint.limitsScaleFactor));
    }
    if (!isFiniteDouble(request.ikContinuityThreshold) || request.ikContinuityThreshold <= 0.0) {
        throw TrajectoryError("trajectory/sequence/ik-threshold",
                              "ikContinuityThreshold 须有限且 >0（rad|m），实际 "
                                  + std::to_string(request.ikContinuityThreshold));
    }

    // 站输入核验：身份合法＋语义名非空且两两互异（requirements I-REQ-3
    // 集合级唯一性——sequenceKey 以名引用，重名即顺序歧义，复验防误用）。
    {
        std::set<std::string> names;
        for (const SequenceStationInput& st : request.stations) {
            if (!st.point.pointOid.isValid()) {
                throw TrajectoryError("trajectory/sequence/station-oid",
                                      "站输入含非法对象身份（名 " + st.point.name + "）");
            }
            if (st.point.name.empty()) {
                throw TrajectoryError("trajectory/sequence/station-name",
                                      "站输入含空语义名（顺序键以名引用——"
                                      "I-REQ-3 唯一性前提）");
            }
            if (!names.insert(st.point.name).second) {
                throw TrajectoryError("trajectory/sequence/station-name",
                                      "站语义名重复（名 " + st.point.name
                                          + "——I-REQ-3 集合级唯一性违约）");
            }
            // 启用的进退段距离必须 >0（requirements 启用校验的复验）。
            if (st.point.approach.enabled && !(st.point.approach.distanceM > 0.0)) {
                throw TrajectoryError("trajectory/sequence/approach-distance",
                                      "approach 段启用时 distanceM 须 >0（m；站 "
                                          + st.point.name + "，实际 "
                                          + std::to_string(st.point.approach.distanceM) + "）");
            }
            if (st.point.retract.enabled && !(st.point.retract.distanceM > 0.0)) {
                throw TrajectoryError("trajectory/sequence/retract-distance",
                                      "retract 段启用时 distanceM 须 >0（m；站 "
                                          + st.point.name + "，实际 "
                                          + std::to_string(st.point.retract.distanceM) + "）");
            }
            // 驻留时长投影合法域（§5.4 步骤 3——常值段时长非正无语义）。
            if (st.dwellDurationS.has_value() && !(*st.dwellDurationS > 0.0)) {
                throw TrajectoryError("trajectory/sequence/dwell-duration",
                                      "驻留时长须 >0（s；站 " + st.point.name
                                          + "，实际 " + std::to_string(*st.dwellDurationS)
                                          + "）");
            }
        }
    }

    // ---- 第 2 步：取消轮询（入口一次——取消不是错误，零素材）。
    if (request.cancel && request.cancel()) {
        plan.canceled = true;
        return plan;
    }

    // ---- 第 3 步：启用站计数（§7.2——序列无启用任务点→TRJ-INPUT-INVALID
    // 素材终止；这是"非不可行"的输入形态问题——不判任务不可行）。
    std::vector<const SequenceStationInput*> enabledStations;
    for (const SequenceStationInput& st : request.stations) {
        if (st.point.enabled) {
            enabledStations.push_back(&st);
        }
    }
    if (enabledStations.empty()) {
        plan.failures.push_back(makeInputInvalidRecord(
            "序列无启用任务点（§7.2——展开无目标站）",
            "启用至少一个任务点后重评（无需判任务不可行——输入形态问题）"));
        return plan;
    }

    // ---- 第 4 步：顺序关系校验（I-REQ-7 三类，全集语义——与 requirements
    // checkSequence 同口径：悬空＝引用缺席名、重复＝同一前驱被两条目
    // 声明、环＝Kahn 消去后剩余节点；任一命中即 TRJ-INPUT-INVALID 终止
    // ——§5.4"不自行修复顺序"）。
    {
        // 全集名表（含禁用站——顺序是全集语义，requirements 先例）。
        std::set<std::string> allNames;
        for (const SequenceStationInput& st : request.stations) {
            allNames.insert(st.point.name);
        }
        std::map<std::string, std::size_t> predecessorUseCount;  // 前驱名→被声明数
        for (const SequenceStationInput& st : request.stations) {
            if (!st.point.sequenceKey.has_value()) {
                continue;  // 无顺序键＝不参与顺序约束（并行/无序条目）
            }
            if (allNames.find(*st.point.sequenceKey) == allNames.end()) {
                // 悬空键：引用的前驱名不在集合（§5.4"悬空键给素材终止"）。
                plan.failures.push_back(makeInputInvalidRecord(
                    "顺序键悬空：站 " + st.point.name + " 引用的前驱名 "
                        + *st.point.sequenceKey + " 不在任务点集合中",
                    "修正该站 sequenceKey 为既有任务点名，或清除顺序键后重评"));
                return plan;
            }
            ++predecessorUseCount[*st.point.sequenceKey];
        }
        for (const auto& [key, count] : predecessorUseCount) {
            if (count > 1) {
                // 重复键：同一前驱被多条目声明（分支＝顺序歧义——I-REQ-7）。
                plan.failures.push_back(makeInputInvalidRecord(
                    "顺序键重复：前驱 " + key + " 被 " + std::to_string(count)
                        + " 个站声明（分支＝顺序歧义——I-REQ-7）",
                    "每个前驱名至多被一个站引用；拆解分支为链后重评"));
                return plan;
            }
        }
    }

    // ---- 第 5 步：Kahn 拓扑排序（边 prev→this；并列/无键站按 name 字典
    // 序消解——std::set 迭代序保证确定性全序，NFR-COR-02 实现决策）。
    std::vector<const SequenceStationInput*> orderedStations;  // 拓扑序全集
    {
        // 邻接表与入度表：以站名为主键（顺序边以名引用）。
        std::map<std::string, std::set<std::string>> adjacency;    // prev→后继名集
        std::map<std::string, std::size_t> inDegree;               // 名→入度
        for (const SequenceStationInput& st : request.stations) {
            inDegree[st.point.name];  // 确保全部节点入表（链首/孤立站入度 0）
        }
        for (const SequenceStationInput& st : request.stations) {
            if (st.point.sequenceKey.has_value()) {
                // 第 4 步已排除悬空键——此处前驱必在集合中。
                if (adjacency[*st.point.sequenceKey].insert(st.point.name).second) {
                    ++inDegree[st.point.name];
                }
            }
        }
        // 就绪集（入度 0）＝std::set——取出序恒 name 字典序（确定性消解）。
        std::set<std::string> ready;
        for (const auto& [name, deg] : inDegree) {
            if (deg == 0U) {
                ready.insert(name);
            }
        }
        // 按名表建快速索引（名→站输入指针）。
        std::map<std::string, const SequenceStationInput*> byName;
        for (const SequenceStationInput& st : request.stations) {
            byName[st.point.name] = &st;
        }
        while (!ready.empty()) {
            const std::string cur = *ready.begin();  // 字典序最小——确定性
            ready.erase(ready.begin());
            orderedStations.push_back(byName[cur]);
            const auto adjIt = adjacency.find(cur);
            if (adjIt == adjacency.end()) {
                continue;
            }
            for (const std::string& next : adjIt->second) {
                if (--inDegree[next] == 0U) {
                    ready.insert(next);
                }
            }
        }
        // 环检测：消去后剩余节点即环上成员（Kahn 性质——入度永不为 0）。
        if (orderedStations.size() != request.stations.size()) {
            // 定位环上站名（升序——确定性文案）。
            std::set<std::string> orderedNames;
            for (const SequenceStationInput* st : orderedStations) {
                orderedNames.insert(st->point.name);
            }
            std::string cycleMembers;
            for (const SequenceStationInput& st : request.stations) {
                if (orderedNames.find(st.point.name) == orderedNames.end()) {
                    if (!cycleMembers.empty()) {
                        cycleMembers += "、";
                    }
                    cycleMembers += st.point.name;
                }
            }
            plan.failures.push_back(makeInputInvalidRecord(
                "顺序关系成环：消去后剩余站 " + cycleMembers + "（回边＝环——I-REQ-7）",
                "打断任务点间的循环前驱引用后重评（展开器不自行修复顺序——§5.4）"));
            return plan;
        }
    }

    // 展开站序＝拓扑序中的 enabled 子序（禁用站不产生段但占据顺序位置
    // ——顺序是全集语义；enabledStations 的相对序以 orderedStations 为准）。
    std::vector<const SequenceStationInput*> expandOrder;
    for (const SequenceStationInput* st : orderedStations) {
        if (st->point.enabled) {
            expandOrder.push_back(st);
        }
    }
    for (const SequenceStationInput* st : expandOrder) {
        plan.stationOrder.push_back(st->point.pointOid);
    }

    // ---- 第 6/7 步：逐站展开段链（构型游标推进——§7.2"起点：上一段
    // 终点构型"；approach 启用站的到达构型依赖 T05 笛卡尔 IK，游标置
    // 无效并如实标记待填段）。
    rw::math::Q cursorQ = request.startQ;   // 构型游标（权威角 rad|m）
    bool cursorValid = true;                // false＝游标依赖 T05 笛卡尔 IK
    std::uint32_t nextSegmentIndex = 0;     // 段序号分配器（0 基连续）
    bool firstTransfer = true;              // 序列首段起点 kind=Start（§6.2）
    core::ObjectId prevStationOid;          // 上一站对象（起点路点来源——§6.2；
                                            //   全零＝无上一站）

    for (const SequenceStationInput* st : expandOrder) {
        const SequenceTaskPoint& pt = st->point;

        // 站级取消轮询（§15.0——站边界＝展开循环边界）。
        if (request.cancel && request.cancel()) {
            plan.canceled = true;
            return plan;  // 已完成段保留（可追溯中间态——§6.7 语义）
        }

        // 第 7 步前置：站候选空集→TRJ-NO-PATH 素材终止（§7.6 行 5"IK 无解
        // →段失败定位"——用户数据面失败，不是异常，不是不可行结论）。
        if (st->candidates.empty()) {
            FailedSegmentRecord rec;
            rec.segmentIndex = nextSegmentIndex;  // 定位到中断处的下一段位
            rec.phaseToken = kPhasePlanSequence;
            rec.reasonToken = std::string(kTrjNoPath);
            rec.subject = pt.pointOid;
            rec.cause = "站 " + pt.name + " 的 IK 候选解集为空（无解或解被上游"
                        "硬过滤——TRJ-NO-PATH 素材；不判任务不可行）";
            rec.recommendedAction =
                "检查任务点位姿可达性（KIN-02 多初值 IK 解集）与硬过滤视图，"
                "调整位姿/限位后重评";
            plan.failures.push_back(std::move(rec));
            return plan;
        }

        if (!pt.approach.enabled && !cursorValid) {
            // ---- 游标失效的纯关节站：起点构型依赖 T05 笛卡尔段终点
            // （approach/retract 后的游标未解析）——构型选择（§7.5 延续性
            // 规则需起点参照）与 PTP 几何均无法在本批给出：产结构完整、
            // 几何待填的 Transfer 条目（不伪造端点；候选集已核非空）。
            PlannedSegment seg;
            seg.segmentIndex = nextSegmentIndex++;
            seg.role = SequenceSegmentRole::TransferPtp;
            seg.spaceType = SegmentSpaceType::JointLinear;
            seg.sourceTaskPoint = pt.pointOid;
            seg.constraint = request.constraint;
            seg.ptpGeometry = std::nullopt;     // 待 T05 笛卡尔链补填
            plan.segments.push_back(std::move(seg));
            firstTransfer = false;
            continue;                            // 游标保持失效（构型未解析）
        }

        if (!pt.approach.enabled) {
            // ---- 纯关节站（游标有效）：站间 PTP 段几何可直接规划
            //（§7 全语义——构型选择三键＋限位守卫＋插值端点）。
            PtpRequest ptp;
            ptp.startQ = cursorQ;
            ptp.lowerBoundQ = request.lowerBoundQ;
            ptp.upperBoundQ = request.upperBoundQ;
            ptp.candidates = st->candidates;
            ptp.ikContinuityThreshold = request.ikContinuityThreshold;
            ptp.constraint = request.constraint;
            ptp.tcpRef = request.tcpRef;
            ptp.frameRef = request.frameRef;
            ptp.sourceTaskPoint = pt.pointOid;  // 转移段归属到达站（§6.2）
            ptp.startKind = firstTransfer ? WaypointKind::Start : WaypointKind::TaskPoint;
            // 非首段起点＝上一站任务点构型——其路点级来源＝上一站对象
            // （§6.2 Waypoint.sourceTaskPoint"TaskPoint kind 必填"）。
            if (!firstTransfer) {
                ptp.startSourceTaskPoint = prevStationOid;
            }
            ptp.endKind = WaypointKind::TaskPoint;
            ptp.segmentIndex = nextSegmentIndex;
            ptp.cancel = request.cancel;

            const PtpPlanResult ptpOut = planPtpSegment(ptp);
            if (ptpOut.status == PtpStatus::Canceled) {
                plan.canceled = true;  // 取消不是错误——零素材（UX-03）
                return plan;
            }
            if (ptpOut.status != PtpStatus::Ok) {
                // PTP 守卫拒绝（端点越限）——素材透传并终止展开（§7.6
                // 行 6"起终点不一致/非法输入→素材，评估终止"）。
                plan.failures.push_back(*ptpOut.failure);
                return plan;
            }

            PlannedSegment seg;
            seg.segmentIndex = nextSegmentIndex++;
            seg.role = SequenceSegmentRole::TransferPtp;
            seg.spaceType = SegmentSpaceType::JointLinear;
            seg.sourceTaskPoint = pt.pointOid;
            seg.constraint = request.constraint;
            seg.ptpGeometry = ptpOut.segment;   // 几何已规划（纯关节可达）
            plan.segments.push_back(std::move(seg));
            firstTransfer = false;

            // 游标推进＝选定任务点构型（PTP 段终点路点——§7.2）。
            cursorQ = ptpOut.segment.waypoints.back().q.value();
            cursorValid = true;
            prevStationOid = pt.pointOid;   // 下一站起点路点的来源＝本站
        } else {
            // ---- 笛卡尔站（approach 启用）：到达段终点＝接近点构型——
            // 依赖笛卡尔段 IK（WP-16-T05 §8.2 逐采样），本批结构完整、
            // 几何如实待填（不伪造端点）。
            PlannedSegment transfer;
            transfer.segmentIndex = nextSegmentIndex++;
            transfer.role = SequenceSegmentRole::TransferPtp;
            transfer.spaceType = SegmentSpaceType::JointLinear;
            transfer.sourceTaskPoint = pt.pointOid;
            transfer.constraint = request.constraint;
            transfer.ptpGeometry = std::nullopt;  // 待 T05（接近点构型未解析）
            plan.segments.push_back(std::move(transfer));
            firstTransfer = false;
            cursorValid = false;  // 游标失效——后续构型依赖 T05
        }

        // ---- approach 段条目（仅启用站；方向语义归 requirements——
        // 本域只承载轴向/距离解析产物，T05 消费构造几何）。
        if (pt.approach.enabled) {
            PlannedSegment seg;
            seg.segmentIndex = nextSegmentIndex++;
            seg.role = SequenceSegmentRole::Approach;
            seg.spaceType = SegmentSpaceType::CartesianLine;
            seg.sourceTaskPoint = pt.pointOid;
            seg.constraint = request.constraint;
            seg.lineSpec = pt.approach;
            plan.segments.push_back(std::move(seg));
        }

        // ---- work 条目（§5.2"作业段＝任务点本身"——任务点＋驻留标记；
        // 非几何段：spaceType 字段对 Work 条目无语义，取枚举默认值，消费
        // 方以 role 区分——§5.4 段链"作业"环节的展开承载）。
        {
            PlannedSegment seg;
            seg.segmentIndex = nextSegmentIndex++;
            seg.role = SequenceSegmentRole::Work;
            seg.spaceType = SegmentSpaceType::JointLinear;
            seg.sourceTaskPoint = pt.pointOid;
            seg.constraint = request.constraint;
            if (st->dwellDurationS.has_value()) {
                seg.dwellDurationS = st->dwellDurationS;  // 驻留（s，>0）
            }
            plan.segments.push_back(std::move(seg));
        }

        // ---- retract 段条目（仅启用站；撤离点构型同样依赖 T05）。
        if (pt.retract.enabled) {
            PlannedSegment seg;
            seg.segmentIndex = nextSegmentIndex++;
            seg.role = SequenceSegmentRole::Retract;
            seg.spaceType = SegmentSpaceType::CartesianLine;
            seg.sourceTaskPoint = pt.pointOid;
            seg.constraint = request.constraint;
            seg.lineSpec = pt.retract;
            plan.segments.push_back(std::move(seg));
        }
    }

    // 展开成功收尾：游标终态只作断言用（终止状态＝序列终点构型——§5.6
    // 必经状态 2；其值已由最后一段承载，此处不重复持有）。
    (void)cursorValid;
    return plan;
}

}  // namespace sdurws::ird::trajectory
