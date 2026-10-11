/**
 * @file   ProcessSpeed.hpp
 * @brief  工艺速度约束校验（WP-16-T15——TRJ-08-S2"指定段（如作业段）
 *         TCP 速度受限或恒定"的领域执行面：时间参数化产物上对指定段
 *         做工艺速度的满足断言与超限诊断）。
 *
 * 设计依据：
 *   - REQUIREMENTS §17.1 TRJ-08-S2 行（P1/R2——"工艺速度约束：指定段
 *     （如作业段）TCP 速度受限或恒定"；验收标准＝"工艺速度约束满足断
 *     言＋超限诊断"；R2 解冻注 2026-10-10——需求 ID/优先级/验收标准不
 *     变，交付绑 R2 独立验收）、TRJ-06（限制超标定位——比较型素材轨）、
 *     TRJ-05（时间参数化产物——本头的输入面，TRJ-08-S2 是其上的工艺
 *     维度校验）、KIN-05 家族（校验不可执行≠校验通过——端口层故障显
 *     性失败，绝不以"未校验"冒充"满足"）
 *   - units/trajectory.md §19.1（R2 接口交接——TRJ-08-S2 行交接物
 *     "SegmentConstraint 扩展版本（指定段 TCP 速度受限/恒定）＋节拍分
 *     解细化"，承接点 §6.6 版本化机制）、§6.5（范围守护——**不建工艺
 *     速度字段：SegmentConstraint 无工艺速度槽位；S2 启用时以段约束扩
 *     展版本承载**——本头 ProcessSpeedConstraint 即"扩展版本"结构，
 *     SegmentConstraint 本体三成员保持不变）、§12.2（限值校验唯一判定
 *     点——关节限值判定仍在时间参数化单点；本头是工艺维度的**独立校验
 *     面**，不进 §12.2 缩放环，见"落位性质"注）、§12.3（驻留口径——
 *     驻留＝零速零加速度常值段，见请求面注）、§12.7（可扩展维度——
 *     S2 维度项 trj.process-speed-record 经 Profile 版本递进承载）、
 *     §14.4（TRJ-LIMIT-EXCEEDED 比较型码复用——"限制超标"语义本体，
 *     零新增码）、§15.4（③端口 FK 消费——evaluateFk 透传 kin.pose-
 *     metrics 语义）、§15.0（错误二分/取消/确定性/纯函数纪律）
 *   - 需求 TRJ-08-S2（P1/R2——本头即其领域承载面）、TRJ-08（接口约束
 *     ——扩展经 §6.6 版本化机制不破坏）、NFR-COR-01（黄金算例独立书
 *     写）、NFR-COR-02（确定性——采样计划/差分口径/素材序全确定）、
 *     NFR-COR-03（非法输入拒绝，不钳制不静默）、ARC-05（限值零私有副
 *     本——判定容差全部有协议出处或请求面显式携带）
 *   - 任务契约 tasks/foundation/WP-16-T15.json（acceptance 1——工艺速
 *     度约束满足断言＋超限诊断；acceptance 2——扩展点消费经 Evaluation-
 *     Dims 词表、TRJ-08 接口约束不破坏、零空字段/零占位）
 *
 * 背景说明（工艺速度约束在轨迹链路中的位置——第一读者须知）：
 *   T08 时间参数化产出"几何路径＋时间轴"后，关节域的动态量（速度/加
 *   速度/jerk——TRJ-05/TRJ-08-S1）已在 §12.2 唯一判定点校验达标。但工
 *   艺作业（焊接/涂胶/切割等）约束的是**TCP 在任务空间的线速度**——这
 *   是关节限值无法表达的维度（同一关节速度组合在不同构型下 TCP 线速
 *   度可差数倍），必须经 FK 把关节轨迹映射回 TCP 位置后单独校验。
 *   TRJ-08-S2 把该维度分期交付（R2）：指定段（如作业段）的 TCP 速度
 *   规格以两种模式表达——
 *
 *   - **受限（Limited）**：该段 TCP 速度峰值不得超过上限 v_max（m/s）；
 *   - **恒定（Constant）**：该段 TCP 速度应恒定为设定值 v_set（m/s），
 *     容差带＝v_set·(1±band)（band 为请求面显式携带的相对半宽，无量纲
 *     ——附录 D 无"恒定带"默认条目，"无默认且无配置来源的量不得引入
 *     产品侧相对校验"〔附录 D 头部口径〕，故本域零自设默认、强制调用
 *     方显式给出）。
 *
 *   校验的执行点在**时间参数化之后**（TCP 速度是时间导数量，没有时间
 *   轴无速度可言）：输入＝T08 产物的连续求值面 ITimeCurve（TimeParam-
 *   Result.timeCurve——T08 产物形态即本头输入形态，在既有形态上扩展
 *   不推翻）＋受检段时间区间＋段约束＋FK 端口。TCP 速度的**测量**＝时
 *   间邻域差分：v(t) ≈ ‖P(t+δ) − P(t−δ)‖/(2δ)，P 经
 *   IKinematicsComputePort::evaluateFk 取 TCP 位姿（基座系 {B} 平移
 *   部）——TCP 速度消费经 kinematics 端口（R-1 红线：业务域互链禁止，
 *   本域零 kinematics 编译依赖，端口形态＝WP-16-T05 落位的③端口镜像
 *   透传契约）。
 *
 *   **落位性质（防"占位/越权"误读）——本头是校验面，不是缩放维**：
 *   工艺速度约束**不进入** §12.2 时间缩放环（受限模式虽在数学上可缩放
 *   达标——TCP 速度 ∝ 1/α 与关节速度同性质，但恒定模式是形状约束，缩
 *   放只等比缩放速度、改变不了速度曲线的非恒定性，缩放维对两种模式无
 *   统一语义；且 §19.1 S2 行交接物为"段约束扩展版本＋节拍分解细化"，
 *   未含缩放环扩展）。超限时的产物是**诊断**（工艺规格与轨迹不匹配的
 *   事实披露——用户调整限值/轨迹后重评），不是自动时间轴放大——这与
 *   验收标准"工艺速度约束满足断言＋超限诊断"逐字对应。关节域限值判定
 *   仍在 §12.2 唯一判定点（TRJ-05/S1 口径不变）——本头与该判定点的分
 *   工＝关节维 vs 任务维，零双口径。
 *
 *   **§12.3 驻留口径**：驻留段＝时间轴常值段（§9.1——TCP 速度恒 0）。
 *   受检段（ProcessSpeedSegmentSpan）只应覆盖**运动段**区间——工艺约
 *   束是运动语义，驻留是工况事件语义（其时长已由 TimeParamResult.seg-
 *   mentDurationsS 逐段对齐承载，节拍分解的驻留阶段不受工艺速度调制）。
 *   校验器对区间**原样**测量不特判（驻留区间上 TCP 速度恒 0：受限模式
 *   0 ≤ 限值恒满足、恒定模式 0 越出容差带即如实产出越带诊断——两种都
 *   是区间上速度事实的诚实披露）；把恒定约束误投到驻留区间的调用方错
 *   误由诊断暴露而非静默吞掉（黄金用例钉扎该文档化行为）。
 *
 *   **节拍分解细化（§19.1 S2 交接物之二）**：校验结果携带逐受检段观测
 *   （ProcessSpeedSegmentObservation——段序号＋时间区间＋TCP 峰值速度
 *   ＋满足标记），即 trj.cycle-phase-breakdown 证据项（节拍分阶段分解
 *   ——§13.3 行 4）素材的工艺段可观测面：工艺段的时长（区间）与速度
 *   峰值逐段可辨识，装配面据此细化节拍分解，本域不代判阶段归属。
 *
 * 头文件依赖纪律（冒烟模式安全）：本头对 rw 零直接类型消费（请求/结
 * 果值面全部为 std/core/域内类型；FK 端口仅前向声明——裸指针借持）；
 * timeParameterize 产物的 ITimeCurve 经 TimeParam.hpp 引入（域内头）。
 * 定义在实现 TU——src/ProcessSpeed.cpp 为集成模式条件源（消费
 * ITimeCurve::sampleAt 的 Q 构造面——TimeParam.cpp 同款 gating，冒烟
 * 口径不受影响）。
 *
 * 线程安全：全部值类型并发只读安全；checkProcessSpeed 为纯函数（零副
 * 作用/零修订/零写盘——§15.0），单线程使用。确定性：同请求等价输出
 * （采样计划/差分口径/素材组装序全确定——NFR-COR-02）。
 */

#ifndef IRD_TRAJECTORY_PROCESSSPEED_HPP
#define IRD_TRAJECTORY_PROCESSSPEED_HPP

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <sdurws/ird/core/Identity.hpp>      // core::ObjectId（失败素材 subject——ARC-04）
#include <sdurws/ird/trajectory/TimeParam.hpp>  // ITimeCurve（T08 产物连续求值面——输入形态）
#include <sdurws/ird/trajectory/TrjTypes.hpp>   // CancelSignal/FailedSegmentRecord（域内值面）

namespace sdurws::ird::trajectory {

/// FK 端口前向声明（完整类型 KinematicsPort.hpp——本头仅以裸指针形态
/// 借持，实现 TU include；头文件依赖最小面——Smooth.hpp 同款先例）。
class IKinematicsComputePort;

// =====================================================================
// 段约束扩展版本（§6.5/§19.1 S2 交接物——SegmentConstraint 本体零扩
// 展，扩展以独立结构承载）
// =====================================================================

/**
 * @brief 工艺速度约束模式（TRJ-08-S2 行原文"TCP 速度受限或恒定"的封
 *        闭词表两值；扩展新值＝需求变更＋本卡增量修订，不预留空值）。
 */
enum class ProcessSpeedMode : std::uint8_t {
    /// 受限：该段 TCP 速度峰值 ≤ 上限（ProcessSpeedConstraint.tcpSpeedMps
    /// ＝上限，m/s）——"受限"词形。
    Limited,
    /// 恒定：该段 TCP 速度应恒定为设定值（tcpSpeedMps＝设定值，m/s；
    /// 容差带＝constantBandRelative 相对半宽）——"恒定"词形。
    Constant,
};

/**
 * @brief 段级工艺速度约束（§6.5"S2 启用时以段约束扩展版本承载"的扩展
 *        版本结构——**SegmentConstraint 本体保持三成员不变**：§6.5"不
 *        建工艺速度字段"的范围守护不因 S2 启用而解除，扩展以独立值类
 *        型经请求面逐段指定；T14 批 jerk 的承载同精神——jerk 走限值视
 *        图 JointDynamicLimits 扩展而非 TimedSample 成员）。
 *
 * 字段语义：
 *   - mode==Limited：tcpSpeedMps＝TCP 速度上限（m/s）；判定＝段 TCP 峰
 *     值速度 ≤ 上限·(1+附录 D 第 10 项相对 1×10⁻⁹)+ε_abs（限值校验的
 *     单向达标语义——TimeParam.cpp withinLimit 同公式同源容差；ε_abs
 *     ＝core C7 线速度量纲默认 1×10⁻⁹ m/s 经 runtimeAbsoluteTolerance
 *     转写——D-06 单点，本域零自设数值）；constantBandRelative 恒 0
 *     （不消费——携带非零值＝调用方契约违约，校验入口 fail-fast：静默
 *     忽略会使"约束写了什么"与"验了什么"脱钩，NFR-COR-03；T14 批
 *     quintic 拒绝有限 jerkLimit 同款纪律）。
 *   - mode==Constant：tcpSpeedMps＝TCP 速度设定值（m/s）；判定＝段内
 *     全部采样点 TCP 速度 ∈ [v_set·(1−band)−ε_abs, v_set·(1+band)+ε_abs]
 *     （band＝constantBandRelative 相对半宽，无量纲 ∈(0,1)——**强制
 *     调用方显式给出**：附录 D 无"恒定带"默认条目，"无默认且无配置来
 *     源的量不得引入产品侧相对校验"，本域零自设默认值；ε_abs 同上——
 *     浮点吸收下限，不改变工程容差的量级由 band 决定）。
 *
 * 单位/量纲（AGENTS §2.5）：tcpSpeedMps 单位 m/s（TCP 线速度，基座系
 * {B} 下的空间速度幅值——逐采样 FK 位姿平移部的时间导数模长；SI 真值
 * ——KIN-12 显示单位不进校验）；constantBandRelative 无量纲。
 *
 * 值语义纯结构；线程安全（并发只读）。
 */
struct ProcessSpeedConstraint {
    /// 约束模式（封闭词表两值——枚举注）。
    ProcessSpeedMode mode = ProcessSpeedMode::Limited;
    /// TCP 速度规格值（m/s；Limited＝上限、Constant＝设定值；合法域
    /// >0 且有限——0 或负值无工程意义，NaN/±inf 拒绝）。
    double tcpSpeedMps = 0.0;
    /// 恒定带相对半宽（无量纲；Constant 模式必填 ∈(0,1)；Limited 模
    /// 式恒 0——非零即违约，见结构注）。
    double constantBandRelative = 0.0;

    bool operator==(const ProcessSpeedConstraint& o) const noexcept
    {
        return mode == o.mode && tcpSpeedMps == o.tcpSpeedMps
            && constantBandRelative == o.constantBandRelative;
    }
    bool operator!=(const ProcessSpeedConstraint& o) const noexcept
    {
        return !(*this == o);
    }
};

/**
 * @brief 受检段条目（工艺约束的时间区间投影——调用方装配面把 Trajec-
 *        torySegment 的段级约束投影为"时间轴上的区间"后注入）。
 *
 * 区间来源（装配面投影义务）：T08 产物 segmentDurationsS 与请求段序列
 * 逐条目对齐（§12.4——含驻留段），逐条目时长累加即各段的 [tStart, tEnd]
 * 时间区间；本域不重推段边界（单一来源——TimeParamResult 的分段口径）。
 *
 * 区间语义：[tStartS, tEndS] ⊆ [0, totalDurationS] 且 tStartS < tEndS
 * （正时长——零长/逆序区间无校验意义）；区间应覆盖运动段（§12.3 驻留
 * 口径——见 ProcessSpeed.hpp 文件头注，驻留区间的速度事实由校验原样
 * 披露不特判）。
 *
 * subject：失败素材 subject 投影（段来源任务点对象——可空；与 TimePa-
 * ramSegment.sourceTaskPoint 同源——装配面透传，素材组装时绑定）。
 * 值语义纯结构；线程安全。
 */
struct ProcessSpeedSegmentSpan {
    /// 段序号（0 基——§6.3 定位键；与 TrajectorySegment.segmentIndex 同
    /// 源，超限素材定位与节拍分解观测共用）。
    std::uint32_t segmentIndex = 0;
    /// 受检区间起点（s；自轨迹起点起算——TimedSample.t 同一时间轴）。
    double tStartS = 0.0;
    /// 受检区间终点（s；> tStartS 且 ≤ 请求 totalDurationS）。
    double tEndS = 0.0;
    /// 该段的工艺速度约束（扩展版本结构——结构注）。
    ProcessSpeedConstraint constraint;
    /// 失败素材 subject 投影（可空——§6.2 FailedSegmentRecord.subject）。
    std::optional<core::ObjectId> subject;

    bool operator==(const ProcessSpeedSegmentSpan& o) const
    {
        return segmentIndex == o.segmentIndex && tStartS == o.tStartS
            && tEndS == o.tEndS && constraint == o.constraint
            && subject == o.subject;
    }
    bool operator!=(const ProcessSpeedSegmentSpan& o) const
    {
        return !(*this == o);
    }
};

// =====================================================================
// 校验请求/结局/结果（"请求-结果结构体＋自由函数入口"——FailureLocate/
// Smooth 同款形态，P-TRJ-11 签名漂移规避）
// =====================================================================

/**
 * @brief 工艺速度校验请求（全部值语义；前置校验序见 checkProcessSpeed）。
 *
 * 前置（调用方契约违约 → TrajectoryError fail-fast）：
 *   - timeCurve 非空（T08 Ok 态产物的连续求值面——TimeParamResult.time-
 *     Curve；无时间化产物就没有工艺速度可言——§12.4"不伪造节拍"同源
 *     纪律：调用方不得以默认构造请求蒙混）；
 *   - totalDurationS 有限且 >0（s——与 timeCurve 的求值域一致，越界区
 *     间在逐段校验拒绝）；
 *   - spans 可空（空＝零受检段——合法空结果：零校验零素材零观测，与
 *     T09"空素材序列＝合法空结果"同口径）；spans 非空时 fkPort 必须非
 *     空（TCP 位置消费面——缺失＝调用方装配违约，fail-fast 不静默跳过
 *     校验：**未校验绝不冒充满足**，KIN-05 家族纪律）；
 *   - 逐 span 合法域（ProcessSpeedSegmentSpan/ProcessSpeedConstraint 注
 *     ——区间有限有序、速度 >0 有限、恒定带 ∈(0,1)、Limited 零带）；
 *   - sampleStepS 有限且 >0（s——段内采样步长上限；采样计划确定性：
 *     端点必含＋均匀划分，峰值在同计划下检测与复检同口径——§6.3"采样
 *     计划入身份"精神）；
 *   - cancel 可空（空＝不可取消）。
 *
 * 单位：时间一律 s；速度规格 m/s（见各字段注）。值语义纯结构；线程安
 * 全（fkPort 为借持裸指针——调用期存活即可，所有权归装配面）。
 */
struct ProcessSpeedCheckRequest {
    /// 时间参数化产物的连续求值面（T08 TimeParamResult.timeCurve 透传；
    /// 空＝调用方违约——fail-fast）。
    std::shared_ptr<const ITimeCurve> timeCurve;
    /// FK 端口（TCP 位置消费面——§15.4 evaluateFk 透传 kin.pose-metrics
    /// 语义；R-1：经③端口镜像消费，零 kinematics 编译依赖。spans 非空
    /// 时必填）。
    IKinematicsComputePort* fkPort = nullptr;
    /// 轨迹总时长（s；与 timeCurve 求值域一致——区间越界校验基准；来
    /// 源＝TimeParamResult.timeParam->totalDurationS 透传）。
    double totalDurationS = 0.0;
    /// 受检段序列（可空——空＝合法空结果；多段按序列序逐段校验，素材
    /// 序＝span 序×段内时刻序——确定性组装序）。
    std::vector<ProcessSpeedSegmentSpan> spans;
    /// 段内采样步长上限（s；>0 有限——端点必含＋均匀划分的采样计划）。
    double sampleStepS = 0.01;
    /// 取消观测（可空＝不可取消；span 级/采样级循环边界轮询——§15.1 纪
    /// 律：每轮必查，不做逐元素轮询）。
    CancelSignal cancel;

    bool operator==(const ProcessSpeedCheckRequest& o) const
    {
        return timeCurve == o.timeCurve && fkPort == o.fkPort
            && totalDurationS == o.totalDurationS && spans == o.spans
            && sampleStepS == o.sampleStepS;
    }
    bool operator!=(const ProcessSpeedCheckRequest& o) const
    {
        return !(*this == o);
    }
};

/**
 * @brief 校验结局（封闭词表三态——§15.0 错误二分的返回值面）。
 */
enum class ProcessSpeedStatus : std::uint8_t {
    /// 全部受检段约束满足（零违例素材；逐段观测有效——满足断言的载体，
    /// TRJ-08-S2 验收"约束满足断言"的 Ok 面）。
    Ok,
    /// 至少一段约束违例（violationRecords 携带比较型素材——TRJ-LIMIT-
    /// EXCEEDED 同码复用，"限制超标"语义本体；逐段观测仍然完整——违例
    /// 段与满足段都可观测，TRJ-06"给出具体段落与原因"）。
    Violated,
    /// 取消观测命中（UX-03——取消不是错误：零错误素材、零违例结论、观
    /// 测为空；调用方以取消语义收尾）。
    Canceled,
};

/**
 * @brief 单受检段观测（逐段校验事实——"节拍分解细化"交接物的可观测
 *        承载：工艺段时长（区间）与 TCP 速度峰值逐段可辨识，装配面据
 *        此细化 trj.cycle-phase-breakdown 节拍分解素材；本域不代判阶
 *        段归属——§19.1 S2 行交接物之二）。
 *
 * Violated 态下违例段同样携带观测（peak/satisfied 如实）——诊断素材
 * （violationRecords）与逐段观测（本结构）互补：素材是"哪里超了/超多
 * 少"（比较型），观测是"每段的速度事实"（统计面）。值语义纯结构；线
 * 程安全。
 */
struct ProcessSpeedSegmentObservation {
    /// 段序号（0 基——与 ProcessSpeedSegmentSpan.segmentIndex 同源）。
    std::uint32_t segmentIndex = 0;
    /// 受检区间（s——请求 span 区间原样回填）。
    double tStartS = 0.0;
    double tEndS = 0.0;
    /// 该段 TCP 速度峰值（m/s——同采样计划下的检测口径；数值为差分测
    /// 量值，非解析保证值——测量语义见实现 TU 头注）。
    double peakTcpSpeedMps = 0.0;
    /// 该段约束是否满足（Limited＝峰值≤限值；Constant＝全部采样点在
    /// 容差带内——判定公式见 ProcessSpeedConstraint 注）。
    bool satisfied = false;

    bool operator==(const ProcessSpeedSegmentObservation& o) const noexcept
    {
        return segmentIndex == o.segmentIndex && tStartS == o.tStartS
            && tEndS == o.tEndS && peakTcpSpeedMps == o.peakTcpSpeedMps
            && satisfied == o.satisfied;
    }
    bool operator!=(const ProcessSpeedSegmentObservation& o) const noexcept
    {
        return !(*this == o);
    }
};

/**
 * @brief 工艺速度校验结果（值语义纯结构；线程安全）。
 *
 * 状态—字段联动（实现保证）：
 *   - Ok：violationRecords 恒空；segmentObservations 与请求 spans 逐条
 *     目对齐（同序——确定性），全部 satisfied==true；
 *   - Violated：violationRecords 非空（违例素材——Limited 每违例段一条
 *     峰值超限素材；Constant 每违例段一条首个越差点素材〔首违时刻的
 *     确定性定位〕）；segmentObservations 仍与 spans 逐条目对齐（满足
 *     段与违例段都可观测——TRJ-06 段落事实全披露）；
 *   - Canceled：violationRecords 恒空、segmentObservations 恒空（零错
 *     误素材——取消不是错误，UX-03）。
 *
 * 诊断边界：本结构承载域内素材（FailedSegmentRecord——TRJ-LIMIT-
 * EXCEEDED 比较型素材轨）；DiagnosticRecord 实例（经 IDiagnostic-
 * Factory、snapshotId/sliceId 必填）归评估器组装面（WP-16-T09 形态先
 * 例——FailureLocate 消费素材轨组装条目）。
 */
struct ProcessSpeedCheckResult {
    /// 校验结局（ProcessSpeedStatus 三态）。
    ProcessSpeedStatus status = ProcessSpeedStatus::Canceled;
    /// 违例素材（比较型三要素齐备——UX-03；仅 Violated 非空）。
    std::vector<FailedSegmentRecord> violationRecords;
    /// 逐受检段观测（仅 Ok/Violated 非空——与请求 spans 同序对齐）。
    std::vector<ProcessSpeedSegmentObservation> segmentObservations;

    bool operator==(const ProcessSpeedCheckResult& o) const
    {
        return status == o.status && violationRecords == o.violationRecords
            && segmentObservations == o.segmentObservations;
    }
    bool operator!=(const ProcessSpeedCheckResult& o) const
    {
        return !(*this == o);
    }
};

// =====================================================================
// §19.1 S2 校验入口
// =====================================================================

/**
 * @brief 对时间参数化产物的指定段做工艺速度约束校验（TRJ-08-S2 唯一
 *        执行点——"工艺速度约束满足断言＋超限诊断"的领域承载）。
 *
 * 执行序（每步语义见行内注释；实现细节登记见 ProcessSpeed.cpp 头注）：
 *   1. 前置校验（调用方契约违约 → TrajectoryError fail-fast，token
 *      "trajectory/process-speed/..."——请求面全表见 ProcessSpeedCheck-
 *      Request 注）；
 *   2. 取消轮询（命中 → Canceled，零素材——UX-03）；
 *   3. 逐受检段（请求 spans 序——确定性）：
 *      a. 采样计划：区间端点必含＋均匀划分（步长 ≤ sampleStepS）；
 *      b. 逐采样时刻：timeCurve 采样 q(t±δ) → FK 端口取 TCP 位姿（基
 *         座系平移部）→ 时间邻域差分得 TCP 线速度（m/s）；端口层故障
 *         （PortError）→ 原 token 显性抛出（不吞错不转素材——Cartes-
 *         ianLine 同款分流）；端口取消 → Canceled；
 *      c. 段内峰值（Limited）或逐点带判定（Constant）——判定公式见
 *         ProcessSpeedConstraint 注；违例 → TRJ-LIMIT-EXCEEDED 比较型
 *         素材（实际/期望/单位 m/s＋段定位 segmentIndex/pathParameter/
 *         subject——TRJ-06"具体段落与原因"）；
 *      d. 段观测入列（峰值/满足标记——节拍分解细化面）；
 *   4. 组装结局（全部满足 → Ok；任一违例 → Violated——**违例不中断后
 *      续段校验**：全量诊断面，素材序＝span 序×时刻序）。
 *
 * 本函数不消费 policy（碰撞/阈值零触碰）、不做时间轴缩放（落位性质见
 * 文件头注——校验面非缩放维）、不判任务可行性（N7）、不产出节拍结论
 * （观测面交付，判定归装配/汇总层——PA-1 权威唯一）。
 *
 * @param request [in] 校验请求（ProcessSpeedCheckRequest 前置见其注；
 *                违约抛 TrajectoryError——fail-fast）
 *
 * @return 校验结果（ProcessSpeedCheckResult 三态联动；值语义）
 *
 * @throws TrajectoryError 调用方契约违约（token "trajectory/process-
 *         speed/..."——timeCurve 缺失/fkPort 缺失/区间非法/约束值非法/
 *         采样步长非法等）或 FK 端口层故障（原端口 errorToken 透传——
 *         "trajectory/kin-port-*" 前缀，显性失败不吞错）
 *
 * 纯函数（零副作用/零修订/零写盘）；线程安全（单线程使用）；确定性
 * （采样计划/差分口径/素材组装序全确定——NFR-COR-02，同请求等价输出）。
 */
ProcessSpeedCheckResult checkProcessSpeed(const ProcessSpeedCheckRequest& request);

}  // namespace sdurws::ird::trajectory

#endif  // IRD_TRAJECTORY_PROCESSSPEED_HPP
