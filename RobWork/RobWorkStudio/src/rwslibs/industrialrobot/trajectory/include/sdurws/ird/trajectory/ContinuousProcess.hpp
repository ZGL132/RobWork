/**
 * @file   ContinuousProcess.hpp
 * @brief  连续工艺段边界行为校验（WP-16-T16——TRJ-08-S3"连续（不停驻）
 *         段表达与段间边界行为"的领域执行面：时间参数化产物上对段间边
 *         界做速度连续断言与驻留/不停驻切换明确断言）。
 *
 * 设计依据：
 *   - REQUIREMENTS §17.1 TRJ-08-S3 行（P1/R2——"连续工艺轨迹：连续（不
 *     停驻）段表达与段间边界行为"；验收标准＝"连续段边界行为断言（段
 *     间速度连续、驻留/不停驻切换明确）"；v1.17 R2 解冻注 2026-10-10——
 *     需求 ID/优先级/验收标准不变，交付绑 R2 独立验收）、TRJ-06（连续
 *     性破坏给出具体段落与原因——比较型素材轨）、TRJ-05（时间参数化
 *     产物——本头的输入面，S3 是其上的边界行为维度校验）
 *   - units/trajectory.md §19.1（R2 接口交接——TRJ-08-S3 行交接物
 *     "SegmentSpaceType 词表扩展＋段间边界行为定义"，承接点 §9.1 边界
 *     表）、§9.1（连续性维度表——速度连续＝"时间参数化后结点两侧 qd
 *     差 ≤ 容差（速度 1×10⁻⁹ 相对公式——附录 D 第 10 项同源）"必检；
 *     时间边界行＝"段边界时刻衔接；驻留段两端速度/加速度为零"；驻留/
 *     不停驻边界 R2 行本批落值）、§9.2（合法/非法连接与诊断示例——
 *     "样条结点两侧导数差超容差→TRJ-CONTINUITY-BROKEN"、"驻留段后首
 *     样本速度非零→非法→同上（维度=速度）"——本头两反例族的码面出
 *     处）、§9.3（检查执行点——时间连续性在时间参数化完成后全链执行，
 *     结果入证据）、§12.1（驻留结点两侧导数为零——驻留切换断言的构造
 *     侧依据）、§12.3（驻留段＝零速零加速度常值段）、§6.5（范围守护
 *     ——S3 启用经词表扩展承载，SegmentConstraint/TimedSample 本体零
 *     扩展）、§6.6（版本化机制——payload/Profile 递进承载新证据维
 *     度）、§14.4（TRJ-CONTINUITY-BROKEN 比较型码复用——零新增码）、
 *     §15.0（错误二分/取消/确定性/纯函数纪律）
 *   - 需求 TRJ-08-S3（P1/R2——本头即其领域承载面）、TRJ-08（接口约束
 *     ——扩展经 §6.6 版本化机制不破坏）、NFR-COR-01（黄金算例独立书
 *     写）、NFR-COR-02（确定性——采样序/素材组装序全确定）、NFR-COR-03
 *     （非法输入拒绝，不钳制不静默）、ARC-05（容差零私有副本——判定
 *     容差全部有协议出处或请求面显式携带）
 *   - 任务契约 tasks/foundation/WP-16-T16.json（acceptance 1——连续段
 *     边界行为断言：段间速度连续、驻留/不停驻切换明确；acceptance 2—
 *     —扩展点消费经 EvaluationDims 词表、TRJ-08 接口约束不破坏、零空
 *     字段/零占位）
 *
 * 背景说明（"连续（不停驻）段"与边界行为——第一读者须知）：
 *   工艺轨迹（焊接/涂胶/切割等）常要求路径**中途不停顿**：停顿会在工件
 *   上留下热影响/刀痕/断弧。R1 的时间参数化（TRJ-05）默认形态是"逐段
 *   独立起停"——每段端点零速度零加速度（§12.1 边界条件），段间有天然
 *   停顿；多段拼接时结点两侧导数由中心差分连续化（C² 样条），但**段序
 *   列里哪些边界是"明确停顿"（驻留切换）、哪些是"明确不停顿"（连续切
 *   换），R1 没有表达面**。TRJ-08-S3 把该维度分期交付（R2），两半：
 *
 *   - **段表达**（§19.1 S3 交接物之一）：SegmentSpaceType 词表扩展第三
 *     值 `ContinuousProcess`（TrjTypes.hpp——工艺语义标记"该段期间不停
 *     驻"；不是第三种几何构造器，段几何仍由冻结几何求值器承载）；
 *   - **段间边界行为断言**（本头——交接物之二）：对时间参数化产物的
 *     段间边界逐个断言其切换行为符合声明——
 *       · **段间速度连续**（§9.1 速度连续维）：边界两侧关节速度差在附
 *         录 D 第 10 项容差内（相对 1×10⁻⁹＋C7 ε_abs）——对两类切换
 *         统一断言（驻留切换两侧同为零，连续性平凡成立；连续切换两侧
 *         非零且相等）；
 *       · **驻留/不停驻切换明确**（§9.1 驻留/不停驻边界 R2 行）：每个
 *         边界的切换语义必须与声明的期望一致——
 *           期望＝连续切换（DwellAbsent）⇒ 两侧关节速度峰值均达"不停
 *             驻"阈值（请求面显式携带的工艺阈值；低于阈值＝**隐式停
 *             驻**——声称连续实际停顿，工艺违约）；
 *           期望＝驻留切换（DwellPresent）⇒ 两侧关节速度/加速度均为零
 *             （C7 ε_abs 内——§12.1"驻留结点两侧导数为零"的断言面；
 *             非零＝驻留切换语义破坏，§9.2 行 5 反例族）。
 *         第三态不存在：一个边界要么声明连续要么声明驻留，断言面拒绝
 *         "无声明"边界（调用方投影义务）与"期望与事实不符"边界。
 *
 *   **落位性质（防"占位/越权"误读）——本头是校验面，不是时间化维**：
 *   连续切换的"不停顿"性质由 C² 样条的中心差分结点纪律在 timeParame-
 *   terize 内结构性成立（§12.1/§12.5——T08 批既有管线零改动）；本头是
 *   该性质之上的**独立断言面**（§9.3"时间连续性在时间化完成后全链执
 *   行"的 S3 执行形态），不做时间轴缩放（与 T15"校验面非缩放维"同款
 *   落位性质——§19.1 S3 行交接物未含缩放环扩展）。违例产物是**诊断素
 *   材**（边界行为与声明不符的事实披露），不是自动改写时间轴。
 *
 *   **测量口径（边界两侧速度极限的一阶回推估计——实现登记）**：ITime-
 *   Curve 契约在结点时刻取左段极限（TimeParam.hpp——"C² 保证左右极限
 *   位级相等"），无法直接取右极限；而内点导数直接对照会引入 qdd·δ 一
 *   阶偏差（内点 qd 与结点极限差 ≈ qdd·δ，rad/s——qdd 常规量级下远大
 *   于判定容差 1×10⁻⁹，不可用）。本头以**一阶泰勒回推**承载两侧极限估
 *   计：
 *       qd⁻ ≈ qd(t−δ) + qdd(t−δ)·δ   （左极限——内点值回推一阶项）
 *       qd⁺ ≈ qd(t+δ) − qdd(t+δ)·δ   （右极限——同上）
 *   qd/qdd 均取该内点的样条解析值（无差分误差），回推残差为二阶项
 *   O(jerk·δ²)——δ 为请求面显式携带的邻域半步（s；调用方按结点间距与
 *   运动律 jerk 量级给出，使残差远小于判定容差；黄金算例声明所用值—
 *   —§21.4 开放问题同款处理，本域零自设默认）。δ 收缩纪律与 T15 tcp-
 *   SpeedAt 同款（邻域恒在求值域 [0, totalDurationS] 内）。速度连续判
 *   定＝两侧估计的无穷范数差对附录 D 第 10 项容差（相对 1×10⁻⁹＋C7
 *   ε_abs）判定；Dwell 零速判定＝两侧估计的无穷范数对 C7 ε_abs 判定。
 *
 *   **断言维与验收标准的对应（防扩大）**：S3 断言面只承载验收标准原文
 *   的两维——"段间速度连续"（速度维）与"驻留/不停驻切换明确"（切换
 *   语义词，速度承载）。驻留切换的**加速度零**断言不入本面（§9.1 时间
 *   边界行的加速度半区属 T08 运动律构造承诺——§12.1"驻留结点两侧导
 *   数为零"在 timeParameterize 构造下结构性成立且 T08 黄金算例已覆盖；
 *   且加速度的内点测量残差 O(jerk·δ) 为一阶——无本头速度维的二阶回
 *   推手段，强行断言必误报）。§9.2 行 5 反例（"驻留段后首样本速度非
 *   零"）原文即速度维——本面的 Dwell 零速断言是其忠实承接。
 *
 *   **"不停驻"阈值（工艺语义量——零自设默认）**：隐式停驻判定需要
 *   "速度低到多少算停驻"的工程阈值——附录 D 无该条目（非数值容差，
 *   是工艺规格），本域零自设默认，请求面显式携带（minContinuousSpeed，
 *   关节速度无穷范数下限，rad/s|m/s 逐轴按关节类型——混合关节链以无
 *   穷范数计量并声明口径）；T15 恒定带 constantBandRelative 同款纪律
 *   （"无默认且无配置来源的量不得引入产品侧相对校验"）。驻留切换的
 *   零速判定不消费该阈值（用 C7 ε_abs——数值容差非工艺阈值）。
 *
 *   **§12.3 驻留口径**：驻留段＝时间轴常值段（§9.1——两端速度/加速度
 *   恒 0）。驻留切换边界的两侧零速断言对"驻留段端点＋相邻段端点"执
 *   行——相邻段端点在 timeParameterize 的驻留结点纪律下同为零（§12.1
 *   "驻留结点两侧导数为零"），断言面是其守卫（黄金回归拦截面）。
 *
 * 头文件依赖纪律（冒烟模式安全——ProcessSpeed.hpp 同款）：本头对 rw
 * 零直接类型消费（请求/结果值面全部为 std/core/域内类型；ITimeCurve
 * 经 TimeParam.hpp 引入——域内头）。定义在实现 TU——src/Continuous-
 * Process.cpp 为集成模式条件源（消费 ITimeCurve::sampleAt 的 Q 构造面
 * ——TimeParam.cpp/ProcessSpeed.cpp 同款 gating，冒烟口径不受影响）。
 *
 * 线程安全：全部值类型并发只读安全；checkProcessBoundaries 为纯函数
 * （零副作用/零修订/零写盘——§15.0），单线程使用。确定性：同请求等
 * 价输出（采样序/素材组装序全确定——NFR-COR-02）。
 */

#ifndef IRD_TRAJECTORY_CONTINUOUSPROCESS_HPP
#define IRD_TRAJECTORY_CONTINUOUSPROCESS_HPP

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <sdurws/ird/core/Identity.hpp>        // core::ObjectId（素材 subject——ARC-04）
#include <sdurws/ird/trajectory/TimeParam.hpp>  // ITimeCurve（T08 产物连续求值面——输入形态）
#include <sdurws/ird/trajectory/TrjTypes.hpp>   // CancelSignal/FailedSegmentRecord（域内值面）

namespace sdurws::ird::trajectory {

// =====================================================================
// 边界切换语义（§9.1 驻留/不停驻边界 R2 行的词表化——封闭两值）
// =====================================================================

/**
 * @brief 段间边界的切换语义（TRJ-08-S3"驻留/不停驻切换明确"的期望声
 *        明词表——封闭两值；扩展新值＝需求变更＋本卡增量修订，不预留
 *        空值）。
 *
 * "明确"的语义：每个段间边界的切换行为由调用方装配面按段分类投影为
 * 期望声明（连续工艺段×连续工艺段→Continuous；任一侧驻留段→Dwell），
 * 校验面断言时间参数化产物在边界处的实际行为与声明一致。无声明的边
 * 界（不在请求 boundaries 中）＝调用方投影义务未履行，不属于"第三种
 * 切换"——本面不校验未投影的边界（投影完备性归装配面，本域不代判）。
 */
enum class BoundarySwitchKind : std::uint8_t {
    /// 不停驻切换：边界两侧均达"不停驻"阈值且速度连续——连续工艺段序
    /// 列的内部边界（"段间速度连续"断言的主承载形态）。
    Continuous,
    /// 驻留切换：边界两侧速度/加速度为零——驻留段端点与相邻段的交界
    /// （§12.1"驻留结点两侧导数为零"、§9.1 时间边界行的断言面形态）。
    Dwell,
};

// =====================================================================
// 边界投影条目与校验请求/结果（"请求-结果结构体＋自由函数入口"——
// ProcessSpeed/Smooth/FailureLocate 同款形态，P-TRJ-11 签名漂移规避）
// =====================================================================

/**
 * @brief 单个段间边界的投影条目（装配面把"段序列＋段空间类型"投影为
 *        "边界时刻＋期望切换语义"后注入——与 T15 ProcessSpeedSegmentSpan
 *        的区间投影同精神：本域不重推段边界，单一来源＝T08 产物的分段
 *        口径 segmentDurationsS 逐条目累加）。
 *
 * 投影义务：边界 i＝段 i 末点与段 i+1 首点的交界，tBoundaryS＝前段时
 * 长累加和（s）；期望切换语义由两侧段的空间类型决定（ContinuousProcess
 * ×ContinuousProcess→Continuous；含驻留段→Dwell；其他组合由装配面按
 * 工艺语义裁决——本域不限定组合规则，只断言声明与事实一致）。
 * 值语义纯结构；线程安全。
 */
struct ProcessBoundarySpan {
    /// 边界序（0 基——段 i 与段 i+1 之间＝边界 i；§6.3 定位键同源，
    /// 素材定位与观测对账共用）。
    std::uint32_t boundaryIndex = 0;
    /// 前段序号（0 基——§6.3 定位键；素材 segmentIndex 携带本值）。
    std::uint32_t beforeSegmentIndex = 0;
    /// 后段序号（0 基——§6.3 定位键）。
    std::uint32_t afterSegmentIndex = 0;
    /// 边界时刻（s；自轨迹起点起算——TimedSample.t 同一时间轴；严格内
    /// (0, totalDurationS)——轨迹端点不是"段间"边界）。
    double tBoundaryS = 0.0;
    /// 期望切换语义（封闭词表两值——枚举注）。
    BoundarySwitchKind expectedSwitch = BoundarySwitchKind::Continuous;
    /// 失败素材 subject 投影（可空——§6.2 FailedSegmentRecord.subject；
    /// 通常＝前段来源任务点，装配面透传）。
    std::optional<core::ObjectId> subject;

    bool operator==(const ProcessBoundarySpan& o) const
    {
        return boundaryIndex == o.boundaryIndex
            && beforeSegmentIndex == o.beforeSegmentIndex
            && afterSegmentIndex == o.afterSegmentIndex
            && tBoundaryS == o.tBoundaryS && expectedSwitch == o.expectedSwitch
            && subject == o.subject;
    }
    bool operator!=(const ProcessBoundarySpan& o) const
    {
        return !(*this == o);
    }
};

/**
 * @brief 连续工艺段边界行为校验请求（全部值语义；前置校验序见
 *        checkProcessBoundaries）。
 *
 * 前置（调用方契约违约 → TrajectoryError fail-fast，token 前缀族
 * "trajectory/boundary/..."）：
 *   - timeCurve 非空（T08 Ok 态产物的连续求值面——TimeParamResult.time-
 *     Curve 透传；无时间化产物无速度可言——§12.4"不伪造节拍"同源纪
 *     律）；
 *   - totalDurationS 有限且 >0（s——与 timeCurve 求值域一致）；
 *   - neighborDeltaS 有限且 >0（s——边界邻域半步；调用方按结点间距与
 *     运动律 jerk 量级给出使邻域漂移远小于判定容差——测量口径见文件
 *     头注；本域零自设默认）；
 *   - minContinuousSpeed 有限且 >0（rad/s|m/s——"不停驻"工艺阈值；零
 *     /负值＝无不停驻语义可断言，强制显式——T15 恒定带同款纪律）；
 *   - boundaries 可空（空＝零受检边界——合法空结果：零断言零素材零观
 *     测）；逐条目：tBoundaryS 有限且 ∈(0, totalDurationS)、序号合法域
 *     （无约束——定位键透传）、expectedSwitch 两值词表。
 *   - cancel 可空（空＝不可取消）。
 *
 * 单位：时间一律 s；速度阈值 rad/s|m/s（关节速度无穷范数同纲——见
 * minContinuousSpeed 注）。值语义纯结构；线程安全（timeCurve 为借持
 * shared_ptr，调用期存活即可）。
 */
struct ProcessBoundaryCheckRequest {
    /// 时间参数化产物的连续求值面（T08 TimeParamResult.timeCurve 透传；
    /// 空＝调用方违约——fail-fast）。
    std::shared_ptr<const ITimeCurve> timeCurve;
    /// 轨迹总时长（s；与 timeCurve 求值域一致——邻域收缩/边界时刻校验
    /// 基准；来源＝TimeParamResult.timeParam->totalDurationS 透传）。
    double totalDurationS = 0.0;
    /// 受检边界序列（可空——空＝合法空结果；多边界按序列序逐个断言，
    /// 素材序＝边界序×维度序——确定性组装序）。
    std::vector<ProcessBoundarySpan> boundaries;
    /// 边界邻域半步（s；>0 有限——两侧内点采样 t±δ'，δ'＝min(本值，
    /// 到时轴端点距离)——邻域恒在求值域内的收缩口径，T15 tcpSpeedAt
    /// 同款）。
    double neighborDeltaS = 0.0;
    /// "不停驻"工艺阈值（rad/s|m/s，关节速度无穷范数下限；>0 有限——
    /// 连续切换边界的两侧速度峰值均须 ≥ 本值；隐式停驻违例的比较型期
    /// 望值。工艺语义量——本域零自设默认，调用方按工艺规格显式给出）。
    double minContinuousSpeed = 0.0;
    /// 取消观测（可空＝不可取消；边界级/采样级循环边界轮询——§15.1 纪
    /// 律：每轮必查，不做逐元素轮询）。
    CancelSignal cancel;

    bool operator==(const ProcessBoundaryCheckRequest& o) const
    {
        return timeCurve == o.timeCurve && totalDurationS == o.totalDurationS
            && boundaries == o.boundaries
            && neighborDeltaS == o.neighborDeltaS
            && minContinuousSpeed == o.minContinuousSpeed;
    }
    bool operator!=(const ProcessBoundaryCheckRequest& o) const
    {
        return !(*this == o);
    }
};

/**
 * @brief 校验结局（封闭词表三态——§15.0 错误二分的返回值面；Process-
 *        SpeedStatus 同款三态形态）。
 */
enum class ProcessBoundaryStatus : std::uint8_t {
    /// 全部受检边界行为与声明一致（零违例素材；逐边界观测有效——满足
    /// 断言的载体，TRJ-08-S3 验收"边界行为断言"的 Ok 面）。
    Ok,
    /// 至少一边界违例（violationRecords 携带比较型素材——TRJ-CONTINUITY-
    /// BROKEN 同码复用，§9.2 速度维/驻留维反例族；逐边界观测仍然完整
    /// ——违例边界与满足边界都可观测，TRJ-06"具体段落与原因"）。
    Violated,
    /// 取消观测命中（UX-03——取消不是错误：零错误素材、零违例结论、观
    /// 测为空；调用方以取消语义收尾）。
    Canceled,
};

/**
 * @brief 单边界观测（逐边界校验事实——连续工艺段边界行为记录的证据素
 *        材承载面：trj.continuous-process-boundary 证据项（Evaluation-
 *        Dims Profile v4 行）的 artifact 语义来源——逐边界的切换事实与
 *        判定可回放；评估器组装面据其组装证据项，本域不代判证据状态
 *        ——PA-1 权威唯一）。
 *
 * Violated 态下违例边界同样携带观测（事实如实）——诊断素材（violation-
 * Records）与逐边界观测（本结构）互补：素材是"哪条边界/差多少"（比较
 * 型），观测是"每条边界的速度事实"（统计面）。值语义纯结构；线程安全。
 */
struct ProcessBoundaryObservation {
    /// 边界序（0 基——与 ProcessBoundarySpan.boundaryIndex 同源）。
    std::uint32_t boundaryIndex = 0;
    /// 边界时刻（s——请求 span 时刻原样回填）。
    double tBoundaryS = 0.0;
    /// 边界前侧关节速度无穷范数（rad/s|m/s——左极限的一阶回推估计的
    /// ‖·‖_∞；测量口径见 ContinuousProcess.hpp 文件头注——内点解析值
    /// 回推 qdd·δ 一阶项，残差 O(jerk·δ²)）。
    double speedBefore = 0.0;
    /// 边界后侧关节速度无穷范数（rad/s|m/s——右极限的同口径估计）。
    double speedAfter = 0.0;
    /// 两侧速度差的无穷范数（rad/s|m/s——两侧估计向量差的 ‖·‖_∞；速
    /// 度连续判定的实测值来源）。
    double speedDelta = 0.0;
    /// 速度连续判定（true＝两侧估计的无穷范数差在附录 D 第 10 项容差
    /// 内——相对 1×10⁻⁹＋C7 ε_abs；两类切换统一断言）。
    bool speedContinuousOk = false;
    /// 切换语义判定（true＝实际行为与 expectedSwitch 声明一致——
    /// Continuous＝两侧 ‖qd‖_∞ ≥ minContinuousSpeed；Dwell＝两侧
    /// ‖qd‖_∞ 在 C7 ε_abs 内为零——断言维＝速度〔验收标准原文口径〕，
    /// 加速度半区不入本面见文件头注"断言维"段）。
    bool switchSemanticsOk = false;
    /// 该边界全部判定是否通过（speedContinuousOk && switchSemanticsOk
    /// ——观测级结论；结局级 Violated/Ok 由全量边界汇总）。
    bool satisfied = false;

    bool operator==(const ProcessBoundaryObservation& o) const noexcept
    {
        return boundaryIndex == o.boundaryIndex && tBoundaryS == o.tBoundaryS
            && speedBefore == o.speedBefore && speedAfter == o.speedAfter
            && speedDelta == o.speedDelta
            && speedContinuousOk == o.speedContinuousOk
            && switchSemanticsOk == o.switchSemanticsOk
            && satisfied == o.satisfied;
    }
    bool operator!=(const ProcessBoundaryObservation& o) const noexcept
    {
        return !(*this == o);
    }
};

/**
 * @brief 连续工艺段边界行为校验结果（值语义纯结构；线程安全）。
 *
 * 状态—字段联动（实现保证）：
 *   - Ok：violationRecords 恒空；boundaryObservations 与请求 boundaries
 *     逐条目对齐（同序——确定性），全部 satisfied==true；
 *   - Violated：violationRecords 非空（每违例边界每违例维度至多一条素
 *     材——比较型三要素齐备）；boundaryObservations 仍与 boundaries 逐
 *     条目对齐（满足与违例边界都可观测——TRJ-06 边界事实全披露）；
 *   - Canceled：violationRecords 恒空、boundaryObservations 恒空（零错
 *     误素材——取消不是错误，UX-03）。
 *
 * 诊断边界：本结构承载域内素材（FailedSegmentRecord——TRJ-CONTINUITY-
 * BROKEN 比较型素材轨）；DiagnosticRecord 实例（经 IDiagnosticFactory、
 * snapshotId/sliceId 必填）归评估器组装面（WP-16-T09 形态先例——
 * FailureLocate 消费素材轨组装条目）。
 */
struct ProcessBoundaryCheckResult {
    /// 校验结局（ProcessBoundaryStatus 三态）。
    ProcessBoundaryStatus status = ProcessBoundaryStatus::Canceled;
    /// 违例素材（比较型三要素齐备——UX-03；仅 Violated 非空）。
    std::vector<FailedSegmentRecord> violationRecords;
    /// 逐受检边界观测（仅 Ok/Violated 非空——与请求 boundaries 同序对
    /// 齐）。
    std::vector<ProcessBoundaryObservation> boundaryObservations;

    bool operator==(const ProcessBoundaryCheckResult& o) const
    {
        return status == o.status && violationRecords == o.violationRecords
            && boundaryObservations == o.boundaryObservations;
    }
    bool operator!=(const ProcessBoundaryCheckResult& o) const
    {
        return !(*this == o);
    }
};

// =====================================================================
// §19.1 S3 校验入口
// =====================================================================

/**
 * @brief 对时间参数化产物的段间边界做连续工艺边界行为校验（TRJ-08-S3
 *        唯一执行点——"连续段边界行为断言：段间速度连续、驻留/不停驻
 *        切换明确"的领域承载）。
 *
 * 执行序（每步语义见行内注释；实现细节登记见 ContinuousProcess.cpp 头
 * 注）：
 *   1. 前置校验（调用方契约违约 → TrajectoryError fail-fast，token
 *      "trajectory/boundary/..."——请求面全表见 ProcessBoundaryCheck-
 *      Request 注）；
 *   2. 取消轮询（命中 → Canceled，零素材——UX-03）；
 *   3. 逐受检边界（请求 boundaries 序——确定性）：
 *      a. 邻域内点采样与一阶回推：t±δ'（δ'＝min(neighborDeltaS, 到时轴
 *         端点距离)——邻域恒在求值域内；t±δ' 为段内点，sampleAt 的
 *         qd/qdd 为样条解析导数）→ 左右极限估计 qd∓ = qd(t∓δ) ± qdd(t∓
 *         δ)·(∓δ)（残差 O(jerk·δ'²)——测量口径见文件头注）；
 *      b. 速度连续断言（两类切换统一）：两侧估计的无穷范数差对附录 D
 *         第 10 项容差（相对 1×10⁻⁹＋C7 ε_abs）判定；违例 → TRJ-CONTI-
 *         NUITY-BROKEN 比较型素材（实际差/容差/单位＋边界定位——§9.2
 *         速度维反例族）；
 *      c. 切换语义断言（expectedSwitch 分派——零空分支；断言维＝速度，
 *         见文件头注"断言维"段）：
 *         Continuous：两侧 ‖qd‖_∞ ≥ minContinuousSpeed（低于阈值＝隐
 *           式停驻——声称连续实际停顿的工艺违约）→违例素材（实际范数
 *           取严侧/阈值/单位）；
 *         Dwell：两侧 ‖qd‖_∞ 在 C7 ε_abs 内为零（§12.1 驻留结点两侧速
 *           度为零的断言面——§9.2 行 5"驻留段后首样本速度非零"反例族
 *           的边界侧承接）→违例素材（实际范数/零容差/单位）；
 *      d. 边界观测入列（范数/差/两判定——证据素材面）；
 *   4. 组装结局（全部满足 → Ok；任一违例 → Violated——**违例不中断后
 *      续边界断言**：全量诊断面，素材序＝边界序×维度序）。
 *
 * 本函数不消费 policy（碰撞/阈值零触碰）、不做时间轴缩放（落位性质见
 * 文件头注——校验面非时间化维；T08/T14 既有管线零改动）、不判任务可
 * 行性（N7）、不产出证据状态（观测面交付，判定归评估器/汇总层——PA-1
 * 权威唯一）。
 *
 * @param request [in] 校验请求（ProcessBoundaryCheckRequest 前置见其注；
 *                违约抛 TrajectoryError——fail-fast）
 *
 * @return 校验结果（ProcessBoundaryCheckResult 三态联动；值语义）
 *
 * @throws TrajectoryError 调用方契约违约（token "trajectory/boundary/
 *         ..."——timeCurve 缺失/时长非法/邻域半步非法/阈值非法/边界时
 *         刻越界等）
 *
 * 纯函数（零副作用/零修订/零写盘）；线程安全（单线程使用）；确定性
 * （采样序/素材组装序全确定——NFR-COR-02，同请求等价输出）。
 */
ProcessBoundaryCheckResult checkProcessBoundaries(
    const ProcessBoundaryCheckRequest& request);

}  // namespace sdurws::ird::trajectory

#endif  // IRD_TRAJECTORY_CONTINUOUSPROCESS_HPP
