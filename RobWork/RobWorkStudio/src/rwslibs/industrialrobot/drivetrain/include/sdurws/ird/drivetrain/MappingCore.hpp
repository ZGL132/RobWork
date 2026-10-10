/**
 * @file   MappingCore.hpp
 * @brief  传动映射核心契约（units/drivetrain.md §13.1/§13.2/§13.3/§13.4/
 *         §13.5）——IDriveTrainMappingEvaluator（映射核心注入契约：归一化
 *         输入→电机侧序列/工作点/统计）、输入快照校验器
 *         ITransmissionInputValidator、矩阵良态校验器
 *         ICouplingMatrixValidator（R1 阻断面＋P-RT-7 条件数）、虚功/功率
 *         一致性检查器 IVirtualWorkConsistencyChecker、反射惯量评估器
 *         IReflectedInertiaEvaluator、效率折算器 IEfficiencyEvaluator、
 *         电机工作点统计器 IMotorOperatingPointEvaluator 与唯一实现
 *         DriveTrainMappingCore。
 *
 * 设计依据：
 *   - units/drivetrain.md §6.2/§6.3（R1 映射定义与阻断面——顺序检查、
 *     首个命中即阻止）、§7.2/§7.3（矩阵形态与良态阈值——P-RT-7 设计默认
 *     1×10⁸，裁决前同源对齐并留痕）、§8（虚功/功率一致性四口径——运行
 *     侧不私设数值阈值）、§9（反射惯量）、§10.2/§10.3（效率折算）、
 *     §13.0～§13.5（通用约定与步骤接口）、§13.9（线程与生命周期）
 *   - 需求 DYN-04（M-12：τ_motor＝Cᵀ·τ_joint＋J_rotor·θ̈ 精确虚功映射，
 *     不对角化/准静态近似、不丢交叉耦合项）、NFR-COR-01（解析算例＋独立
 *     参考实现对照）、NFR-COR-02（确定性——纯函数零副作用）、NFR-COR-03
 *     （非有限拒绝）、SEL-09（移动关节范围外）、P-DT-2（惯量比阈值不内嵌）
 *   - 任务契约 tasks/foundation/WP-18-T03.json（acceptance 1/2/3）
 *
 * 错误语义（卡 §13.0，本头全部接口遵循）：
 *   - 调用方契约违约（空输入/维度不匹配/非法矩阵/非有限值/轴类型范围外/
 *     轴序违约/能力阻断）＝fail-fast 抛 std::invalid_argument（what() 以
 *     DT-* 码开头——§13.8 示例形态）；
 *   - 环境/数据类（效率缺失、转子缺失、负载惯量缺失、时间非单调、循环
 *     不完整）＝返回诊断＋完整性降级素材（DataInsufficient 路径），不抛
 *     异常、不吞错。
 *
 * 接口的边界价值（D-DT-9——§13.5）：六个步骤接口各为映射管线的独立对照
 *   点（黄金数据集按步骤独立断言）与 R2 替换缝（如直线传动映射）——非
 *   转发包装器。唯一实现 DriveTrainMappingCore 以纯函数形态提供全部步骤
 *   （无隐藏状态、可重入），评估器适配层（Evaluator.hpp）与模型测试
 *   （NFR-MNT-01 直调）经同一实现调用，保证③端口形态与注入形态同一算法。
 *
 * 线程安全：全部接口的可重入纯函数（ConcurrentReadOnly——卡 §13.0/
 *   §13.9）；ICancellation 由调用方实现并保证其自身线程约束。
 */

#ifndef IRD_DRIVETRAIN_MAPPINGCORE_HPP
#define IRD_DRIVETRAIN_MAPPINGCORE_HPP

#include <sdurws/ird/core/DiagData.hpp>
#include <sdurws/ird/drivetrain/MappingTypes.hpp>
#include <sdurws/ird/drivetrain/Series.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace sdurws::ird::drivetrain {

// =====================================================================
// 能力位与取消查询（§13.3/§13.1）
// =====================================================================

/**
 * @brief 阶段能力位（卡 §13.3 ICouplingMatrixValidator stage 参数）。
 *
 * R1Capability＝阶段 C 承诺面（旋转对角传动；窗口/非对角输入阻断——
 * R1 阻断反例保留，AT-38）；R2Capability＝阶段 D（MDL-21 耦合矩阵全链——
 * §7 数值路径已随 WP-18-T05 落位：窗口输入经 §7.2 全表矩阵检查后进入
 * 块对角精确映射，交叉耦合逐元素保留）。能力由装配清单与算法版本决定，
 * UI/配置中的 R2 标签不改变计算能力（卡 §6.3 行 1 原文纪律）；本枚举是
 * 该能力的注入面（③端口适配层按装配清单选择构造参数——默认 R1）。
 */
enum class StageCapability : std::uint8_t {
    R1Capability, ///< 阶段 C：旋转对角传动（R1 阻断面全量生效——含窗口拒绝）
    R2Capability, ///< 阶段 D：耦合矩阵数值路径（§7 全表检查＋块对角映射——WP-18-T05）
};

/**
 * @brief 取消查询接口（卡 §13.1 ICancellation——批次边界协作取消）。
 *
 * 映射核心无阻塞等待（纯计算）；取消查询发生在工况批次边界（NFR-PERF-02
 * 协作取消）。实现由宿主（execution/评估器适配层）注入；nullptr＝不可
 * 取消。观测到取消后映射核心自主选择退出路径：以 DiagCancelled 诊断码
 * 结束（返回已完成的逐样本映射素材＋取消诊断，不发布完整统计——卡
 * §12.4"取消后不发布完整结果"）。
 */
class ICancellation {
public:
    virtual ~ICancellation() = default;
    /// 宿主已请求取消 true（协作取消——评估器必须周期性查询）。
    virtual bool cancellationRequested() const = 0;
};

/// 取消路径使用的稳定诊断码＝DiagCodes.hpp 的 kDtEvaluationCancelled
/// （WP-18-T03 表尾追加——唯一书写点纪律，此处不重复定义；消费方 include
/// DiagCodes.hpp 获取）。

// =====================================================================
// 输入快照校验（§13.2）
// =====================================================================

/**
 * @brief 输入快照校验结果（卡 §13.2 TransmissionValidationResult）。
 *
 * ok==false 时 diagnostics 逐项给出 DT-INPUT-／DT-RATIO-／DT-SERIES- 码族
 * 稳定码（比较型字段带单位——ERR-01）；校验器不抛异常（try 轨——调用方
 * 可先收集全部结构问题再决定阻断；映射入口 evaluate() 对其中"调用方
 * 错误类"问题另行 fail-fast，见 DriveTrainMappingCore::evaluate 契约）。
 */
struct TransmissionValidationResult {
    bool ok = false;                                   ///< 全部检查通过
    std::vector<core::DiagnosticRecord> diagnostics{}; ///< 逐项诊断（DT-INPUT-／DT-RATIO-／DT-SERIES- 码族）
};

/**
 * @brief 输入快照结构校验器（§13.2——轴表/序列/单位/有限性/轴序一致性；
 *        精确判据无数值阈值）。
 *
 * 卡 §13.2 签名承载：validate(model, series) 返回逐项诊断（非抛）。
 * 检查集＝卡 §6.3 表后三行（结构有效性/空输入）＋§12.1 序列规则（时间
 * 严格单调【升序——违例按数据类返回 DT-INPUT-TIME-NONMONOTONIC 诊断】、
 * 等长数组【JointSeriesView 结构保证，此处核对 jointIds 与模型轴表一致
 * →DT-SERIES-LENGTH-MISMATCH】、样本值非有限【→诊断＋调用方错误双轨：
 * 诊断列表记录，evaluate() 路径 fail-fast】）。
 */
struct ITransmissionInputValidator {
    virtual ~ITransmissionInputValidator() = default;
    virtual TransmissionValidationResult validate(const DriveTrainModel& model,
                                                  const JointSeriesView& series) const = 0;
};

// =====================================================================
// 矩阵良态校验（§13.3——R1 阻断面＋P-RT-7 条件数）
// =====================================================================

/// 良态条件数阈值（无量纲；P-RT-7 设计默认 1×10⁸——卡 §7.3，裁决前与
/// runtime 编译侧同源对齐并留痕；P-DT-2 登记归属待裁决，若改归
/// EngineeringPolicySet 则本常量改经 Policy 条目声明消费）。
inline constexpr double kWellConditionedLimit = 1.0e8;

/**
 * @brief 矩阵校验结果（卡 §13.3 CouplingValidationResult）。
 *
 * accepted==false＝阻止映射（能力/输入类——**不伪装成"传动不可行"**，
 * 判定权在消费域证据规则，卡 §6.3 末段）；conditionNumber 为实测条件数
 * （无量纲；对角矩阵＝max|c|/min|c| 解析值——谱条件数在对角阵上的精确
 * 形态；比较型诊断素材）。
 */
struct CouplingValidationResult {
    bool accepted = false;                             ///< false＝阻止映射
    double conditionNumber = 0.0;                      ///< 实测条件数（无量纲）
    std::vector<core::DiagnosticRecord> diagnostics{}; ///< DT-MATRIX-／DT-COUPLING- 码族（含比较型）
};

/**
 * @brief 矩阵良态校验器（§13.3——§6.3 阻断面全表＋§7.2 矩阵形态表）。
 *
 * 检查序（**首个命中即阻止**——确定性首错，NFR-COR-02）：
 *   公共前段：
 *   ①能力门控（R1 能力下 window 存在→DT-COUPLING-STAGE-LOCKED——R1 阻断
 *     反例保留，AT-38；显示/配置标签不改变能力）；
 *   ②空轴表（→DT-INPUT-EMPTY——轴表空时后续检查不可判定）；
 *   ③链型/关节类型（Prismatic→DT-AXIS-TYPE-OUT-OF-SCOPE——R1/R2 同拒，
 *     直线传动属 §16.2 扩展）；
 *   分支（按 window 是否存在与能力位）：
 *   ④a 对角路径（无窗口——R1/R2 同构检查）：维度/轴序→DT-INPUT-DIMENSION-
 *     MISMATCH/DT-INPUT-AXIS-ORDER-MISMATCH；非有限→DT-MATRIX-NONFINITE；
 *     非对角→DT-MATRIX-NONDIAGONAL-LOCKED（R2 能力下未声明窗口的非对角
 *     结构同样阻断——交叉耦合必须经窗口声明，不静默拆轴）；对角元 0→
 *     DT-RATIO-ZERO；条件数（对角解析式 max|c|/min|c|）→
 *     DT-MATRIX-ILL-CONDITIONED（比较型）；
 *   ④b R2 矩阵路径（窗口存在——仅 R2 能力可达，WP-18-T05）：窗口结构
 *     （方阵→DT-MATRIX-NONSQUARE；关节窗口合法/互斥→DT-INPUT-DIMENSION-
 *     MISMATCH）；chat 维度→DT-INPUT-DIMENSION-MISMATCH；轴序→
 *     DT-INPUT-AXIS-ORDER-MISMATCH；非有限→DT-MATRIX-NONFINITE（先于
 *     一致性比较——NaN 使等值比较无歧义）；chat 与块对角组合一致性→
 *     DT-INPUT-DIMENSION-MISMATCH；自由轴 c=0→DT-RATIO-ZERO（窗口轴
 *     对角投影不适用本码）；奇异（σmin≤σmax×1×10⁻¹²→DT-MATRIX-SINGULAR，
 *     比较型：σmin/σmax 比值——不得以伪逆放行）；病态（κ＞
 *     kWellConditionedLimit→DT-MATRIX-ILL-CONDITIONED，比较型：实际
 *     条件数/阈值/无量纲）。
 *
 * ★ 常矩阵前提（§7.1）与 P-DT-2：条件数阈值单点 kWellConditionedLimit
 *   （P-RT-7 设计默认 1×10⁸，与 modeling/runtime 编译校验同族设计默认，
 *   奇异分界 σmin/σmax＝1×10⁻¹² 同源——裁决前按同一设计默认执行并留痕）；
 *   本值形态（DriveTrainModel 单一常矩阵字段）结构性排除时变矩阵
 *   （§7.2 DT-MATRIX-TIME-VARYING-UNSUPPORTED 在本表示中无触发载体，
 *   码值保留给未来按工况变化矩阵的扩展形态）。
 *
 * @param model [in] 归一化模型（调用方持有）
 * @param stage [in] 阶段能力位（能力门控第一检查——显示/配置标签不改变能力）
 * @return 校验结果（accepted==false 时 diagnostics 非空且首项＝首个命中码；
 *         accepted==true 时 conditionNumber＝实测条件数——对角解析式或
 *         R2 SVD 谱条件数，无量纲）
 *
 * 线程安全：可重入纯函数。
 */
struct ICouplingMatrixValidator {
    virtual ~ICouplingMatrixValidator() = default;
    virtual CouplingValidationResult validate(const DriveTrainModel& model,
                                              StageCapability stage) const = 0;
};

// =====================================================================
// 虚功/功率一致性检查（§13.4——§8 四口径）
// =====================================================================

/**
 * @brief 单条一致性发现（§8.4——实际值/期望值/单位＋逐元素定位）。
 *
 * 逐元素定位＝样本时刻＋轴（样本下标）；actual/expected 为**独立供给值
 * 与公式期望值**的裸 double 承载（量纲由 field 字段标注——"tau[N*m]"/
 * "power[W]"）。比较判定使用精确位等（映射公式为封闭代数、同一表达式
 * 路径位等成立；无阈值——§8.4"运行侧只做精确判据"；数值容差比较归测试
 * 侧黄金对照，附录 D 第 9 项相对 1×10⁻⁹）。
 */
struct ConsistencyFinding {
    std::size_t sampleIndex = 0; ///< 样本下标（序列内序——定位面）
    std::size_t axisIndex = 0;   ///< 轴下标（电机轴序——定位面）
    double t = 0.0;              ///< 样本时刻（s）
    std::string field{};         ///< 字段标注（量纲语义——如 "tau_motor[N*m]"）
    double actual = 0.0;         ///< 实际值（独立供给）
    double expected = 0.0;       ///< 期望值（映射公式期望）

    bool operator==(const ConsistencyFinding& o) const noexcept
    {
        return sampleIndex == o.sampleIndex && axisIndex == o.axisIndex && t == o.t
            && field == o.field && actual == o.actual && expected == o.expected;
    }
    bool operator!=(const ConsistencyFinding& o) const noexcept { return !(*this == o); }
};

/**
 * @brief 虚功/功率一致性检查器（§13.4——黄金对照与逐元素自检）。
 *
 * 口径（§8.1/§8.2——**理想**几何/力矩映射，不含效率、不含转子项）：
 *   δW_motor＝τ_mᵀ·δθ_motor＝τ_jointᵀ·C·δθ_motor＝δW_joint（逐样本恒等）。
 * checkVirtualWork：actual 为独立供给的电机侧力矩扁平数组（按样本×轴
 *   行主序），期望＝c_j·τ_joint,j（对角理想映射）；逐元素**精确位等**
 *   比较（无阈值——文件头 ConsistencyFinding 注）。
 * checkPowerBalance：actualMotorPower 为独立供给的电机侧功率扁平数组，
 *   期望＝理想功率 τ_m·θ̇＝τ_joint·q̇（逐元素恒等——口径②，防正负抵消
 *   的逐元素断言不以总和替代）。
 */
struct IVirtualWorkConsistencyChecker {
    virtual ~IVirtualWorkConsistencyChecker() = default;
    virtual std::vector<ConsistencyFinding> checkVirtualWork(
        const DriveTrainModel& model, const JointSeriesView& series,
        const std::vector<double>& actual) const = 0;
    virtual std::vector<ConsistencyFinding> checkPowerBalance(
        const DriveTrainModel& model, const JointSeriesView& series,
        const std::vector<double>& actualMotorPower) const = 0;
};

// =====================================================================
// 反射惯量评估（§13.4——§9）
// =====================================================================

/**
 * @brief 反射惯量评估器（§13.4——R1 对角 J/c²；R2 (C⁻¹)ᵀ·J_rotor·C⁻¹ 完整
 *        矩阵＋对角视图（交叉项保留不对角化输出——§9.3）＋窗口投影标记
 *        （§9.5）——WP-18-T05 落位）。
 *
 * @param model [in] 归一化模型（须已过矩阵校验——§13.4 @throws 语义）
 * @param load  [in] 负载折算惯量输入（逐电机轴下标配对；缺失条目→该轴
 *              惯量比不适用）
 * @return 逐轴反射惯量（kg·m²，关节轴系）＋惯量比（数值事实——无阈值判定，
 *         P-DT-2）
 *
 * @throws std::invalid_argument rotor 条目值非法（DT-INERTIA-INVALID——
 *         卡 §13.4 @throws 契约）或矩阵未过校验（阻断面码——本实现经
 *         ICouplingMatrixValidator 同路径检查）
 *
 * 线程安全：可重入纯函数。
 */
struct IReflectedInertiaEvaluator {
    virtual ~IReflectedInertiaEvaluator() = default;
    virtual ReflectedInertiaResult evaluate(const DriveTrainModel& model,
                                            const std::vector<LoadInertiaEntry>& load) const = 0;
};

// =====================================================================
// 效率折算（§13.4——§10.2/§10.3）
// =====================================================================

/**
 * @brief 效率折算器（§13.4——方向选择〔P_joint 符号精确判据〕＋η⁺/η⁻
 *        折算＋零功率分支）。
 *
 * 输出承载：折算后的逐样本传动功率写入 motorSeries 各样本的
 * pTransmission 字段（口径③——文件头 Series.hpp"口径声明"）；η 缺失/
 * 非法的轴走 DataInsufficient 降级素材（诊断＋缺失清单，不伪造、不以
 * η＝1 静默替代——卡 §10.1）。实现为纯函数（输出经出参写回——值语义
 * 调用方持有）。
 */
struct IEfficiencyEvaluator {
    virtual ~IEfficiencyEvaluator() = default;
    virtual void apply(const DriveTrainModel& model,
                       const JointSeriesView& series,
                       std::vector<MotorSeries>& motorSeries,
                       std::vector<core::DiagnosticRecord>& diagnostics,
                       std::vector<std::string>& missingItems,
                       bool& estimatedSeen) const = 0;
};

// =====================================================================
// 电机工作点统计（§13.4——§10.4/§10.6/§11）
// =====================================================================

/**
 * @brief 电机工作点统计器（§13.4——峰值〔带时刻/段/工况〕、RMS〔完整
 *        循环含驻留〕、负载率〔参考值〕、四象限统计、能量分项）。
 *
 * 循环不完整（样本数＜2/时间非单调）→统计标 DataInsufficient 素材
 * （DT-INPUT-SAMPLE-MISSING/DT-INPUT-TIME-NONMONOTONIC 诊断），不输出
 * "部分 RMS 冒充完整循环 RMS"（卡 §10.4 数据不足行）。
 */
struct IMotorOperatingPointEvaluator {
    virtual ~IMotorOperatingPointEvaluator() = default;
    virtual std::vector<MotorOperatingPoint> summarize(
        const DriveTrainModel& model,
        const JointSeriesView& series,
        const std::vector<MotorSeries>& motorSeries,
        std::vector<core::DiagnosticRecord>& diagnostics,
        std::vector<std::string>& missingItems) const = 0;
};

// =====================================================================
// 映射核心注入契约（§13.1）与唯一实现
// =====================================================================

/**
 * @brief 传动映射核心的稳定契约（卡 §13.1——归一化输入→电机侧序列/
 *        工作点/统计/诊断）。
 *
 * 唯一实现 DriveTrainMappingCore；模型测试直调（NFR-MNT-01）；评估器
 * 适配层（Evaluator.hpp）经本接口调用核心，保证③端口形态与注入形态
 * 同一算法（D-DT-2 唯一映射实现）。
 */
class IDriveTrainMappingEvaluator {
public:
    virtual ~IDriveTrainMappingEvaluator() = default;

    /**
     * @brief 执行一次完整映射评估（带能力位——WP-18-T05 起的唯一虚入口）。
     *
     * @param model  [in] 归一化传动模型（构造入口已过结构校验；调用方持有）
     * @param series [in] 关节侧序列（§12.1 契约；调用方持有；本函数不修改）
     * @param ctx    [in] 取消查询回调（可为 nullptr＝不可取消；批次边界查询）
     * @param stage  [in] 阶段能力位——R1＝对角路径（窗口输入阻断，R1 阻断
     *               反例保留）；R2＝§7 矩阵路径（窗口输入经 §7.2 全表检查后
     *               块对角精确映射，交叉耦合逐元素保留）。能力由装配清单与
     *               算法版本决定（卡 §6.3），由调用方（评估器适配层按装配
     *               清单）注入——UI/配置标签不改变能力。
     * @return 映射输出（§11 工作点＋§10 统计＋§9 反射惯量＋诊断；单工况；
     *         R2 时反射惯量含完整矩阵与窗口投影标记）
     *
     * @throws std::invalid_argument 结构非法（§6.3 阻断面/§7.2 矩阵形态/
     *         空输入/维度/轴序/非有限样本值——消息以 DT-* 码开头）
     * @pre  model.identity 与 series 无冲突（不同传动配置混用＝调用方错误）
     * @post 纯函数零副作用；同输入等价输出（NFR-COR-02）
     *
     * 线程安全：可重入纯函数（ConcurrentReadOnly——卡 §13.9）。
     */
    virtual DriveTrainMappingOutput evaluate(const DriveTrainModel& model,
                                             const JointSeriesView& series,
                                             ICancellation* ctx,
                                             StageCapability stage) = 0;

    /**
     * @brief 执行一次完整映射评估（R1 能力——既有三参调用面的便捷形态）。
     *
     * 语义冻结说明：本形态恒按 R1Capability 执行（卡 §15 T05 红线——R1
     * 阻断语义不因 T05 落位而改变；既有调用方行为零变化）。R2 消费必须
     * 显式经四参形态声明能力位——无隐式能力提升。
     */
    DriveTrainMappingOutput evaluate(const DriveTrainModel& model,
                                     const JointSeriesView& series,
                                     ICancellation* ctx)
    {
        return evaluate(model, series, ctx, StageCapability::R1Capability);
    }
};

/**
 * @brief 映射核心唯一实现（卡 §13.1"唯一实现 DriveTrainMappingCore"）。
 *
 * 管线（§13.10 evaluate() 分解——本类实现前四段，统计与适配面由调用方
 * 组合）：结构校验（ICouplingMatrixValidator 同路径检查——首个命中即
 * fail-fast）→ 逐样本映射：
 *   - R1 对角路径（§6.2 全列——位置/速度/加速度/理想力矩/含转子项力矩/
 *     功率，逐轴标量封闭代数）；
 *   - R2 矩阵路径（WP-18-T05，§7.1 公式组冻结口径）：自由轴同 R1 标量
 *     公式；窗口轴 θ＝θ_off＋C_w⁻¹·q、θ̇＝C_w⁻¹·q̇、θ̈＝C_w⁺·q̈（方阵良态下
 *     C⁺≡C⁻¹——§7.3 使用前提）、τ_ideal＝C_wᵀ·τ_joint、τ_motor＝τ_ideal＋
 *     J_rotor·θ̈_motor——交叉耦合项逐元素非零保留，不对角化/准静态等效
 *     绕过（AT-38 M-12）；
 *   → 效率折算（§10.2/§10.3——数据类降级）→ 反射惯量（§9；R2 输出完整
 *     矩阵＋对角视图＋窗口投影标记）→ 一致性自检（τ_ideal 与映射公式
 *     重算位等核对——§8.4 ②映射自检形态；独立数据对照经
 *     IVirtualWorkConsistencyChecker 供测试与诊断定位）→ 工作点统计
 *     （§10.4/§10.6/§11）。
 *
 * 确定性：无隐藏状态、无时钟/随机源；R2 数值核（单侧 Jacobi SVD/Gauss-
 * Jordan 求逆）固定扫描序/主元选择，同输入必得位等输出（NFR-COR-02）。
 * 并行归约（多工况能量汇总，如未来消费方组合）满足附录 D 第 8 项相对
 * 容差 1×10⁻¹²（卡 §13.0）。
 */
class DriveTrainMappingCore final
    : public IDriveTrainMappingEvaluator,
      public ITransmissionInputValidator,
      public ICouplingMatrixValidator,
      public IVirtualWorkConsistencyChecker,
      public IReflectedInertiaEvaluator,
      public IEfficiencyEvaluator,
      public IMotorOperatingPointEvaluator {
public:
    DriveTrainMappingCore() = default;

    // ---- IDriveTrainMappingEvaluator（§13.1 主入口——能力位注入）----
    DriveTrainMappingOutput evaluate(const DriveTrainModel& model,
                                     const JointSeriesView& series,
                                     ICancellation* ctx,
                                     StageCapability stage) override;
    /// 三参形态＝R1 能力（语义冻结——基类便捷形态的本类镜像，既有调用
    /// 方行为零变化；R2 消费必须显式走四参形态）。
    DriveTrainMappingOutput evaluate(const DriveTrainModel& model,
                                     const JointSeriesView& series,
                                     ICancellation* ctx);

    // ---- ITransmissionInputValidator（§13.2）----
    TransmissionValidationResult validate(const DriveTrainModel& model,
                                          const JointSeriesView& series) const override;

    // ---- ICouplingMatrixValidator（§13.3——检查序见接口注释）----
    CouplingValidationResult validate(const DriveTrainModel& model,
                                      StageCapability stage) const override;

    // ---- IVirtualWorkConsistencyChecker（§13.4——理想口径逐元素精确对照）----
    std::vector<ConsistencyFinding> checkVirtualWork(
        const DriveTrainModel& model, const JointSeriesView& series,
        const std::vector<double>& actual) const override;
    std::vector<ConsistencyFinding> checkPowerBalance(
        const DriveTrainModel& model, const JointSeriesView& series,
        const std::vector<double>& actualMotorPower) const override;

    // ---- IReflectedInertiaEvaluator（§13.4——§9.2/§9.5）----
    ReflectedInertiaResult evaluate(const DriveTrainModel& model,
                                    const std::vector<LoadInertiaEntry>& load) const override;

    // ---- IEfficiencyEvaluator（§13.4——§10.2/§10.3 方向折算＋降级）----
    void apply(const DriveTrainModel& model,
               const JointSeriesView& series,
               std::vector<MotorSeries>& motorSeries,
               std::vector<core::DiagnosticRecord>& diagnostics,
               std::vector<std::string>& missingItems,
               bool& estimatedSeen) const override;

    // ---- IMotorOperatingPointEvaluator（§13.4——§10.4/§10.6/§11）----
    std::vector<MotorOperatingPoint> summarize(
        const DriveTrainModel& model,
        const JointSeriesView& series,
        const std::vector<MotorSeries>& motorSeries,
        std::vector<core::DiagnosticRecord>& diagnostics,
        std::vector<std::string>& missingItems) const override;

private:
    // ---- 校验分支实现（validate 的两路载体——公共前段后按窗口形态分派；
    // 分拆只为可读性，检查序与诊断语义以 MappingCore.cpp 内注释为准）。
    CouplingValidationResult validateDiagonalModel(const DriveTrainModel& model) const;
    CouplingValidationResult validateCoupledWindow(const DriveTrainModel& model) const;
};

}  // namespace sdurws::ird::drivetrain

#endif  // IRD_DRIVETRAIN_MAPPINGCORE_HPP
