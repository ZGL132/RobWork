/**
 * @file   Objective.hpp
 * @brief  目标指标配置与八项指标分层（OPT-07）——指标词表物化（方向/单位/
 *         来源评估键/阶段可算性）、激活目标集（含逐目标支配容差）、config.opt
 *         目标段 canonical 序列化、默认激活分期、研究定义目标校验（阶段锁/
 *         评估器注册）与三项静态指标计算（尺寸包络/结构质量/最小关节裕量——
 *         缺失即"—"，不显示零、不判不可行）。任务 WP-20-T05。
 *
 * 设计依据：
 *   - units/optimization.md §7.1（八项比较指标全量定义表——单位/方向/阶段
 *     可算/数据来源唯一口径/缺失语义逐行）、§7.2（指标计算与展示规则——
 *     "全部展示；不可算列显示'—'（token not-computable）——不显示零、不用
 *     估算冒充、不参与 Pareto"、"指标缺失不是零；不适用不是失败"、"指标值
 *     非有限→候选评估失败处理，不静默转 0"）、§7.3（目标激活规则——阶段/
 *     评估器/证据/一致四条件与默认激活分期）、§4.3（config.opt canonical
 *     的"目标集（按 MetricId 枚举序位图＋每目标容差三元组）"段）、§6.6
 *     （OPT-METRIC-NOT-COMPUTABLE 警告码——指标不可算的呈现）、§6.7
 *     （R1 域评估键 opt-static-screen——三项静态指标的来源评估键）、
 *     §8.3 第 8 步（指标事实记录面：{MetricId, 值(SI)或 not-computable,
 *     来源评估键, ValueProvenance}）、§16.3 P-OPT-5（尺寸包络/成本货币/
 *     驱动裕量口径未冻结——保留接口与字段，暂定口径展示并在留痕标注，
 *     不自行冻结）
 *   - 需求 OPT-07（八项全部展示；默认激活分期：阶段 B 三项静态可算指标，
 *     全量版本 OPT-D 为节拍/结构质量/器件成本）、OPT-04 的目标面（无权重
 *     ——禁止单一加权总分，需求附录 B 裁决排除；本头零权重字段）、
 *     NFR-COR-03（缺失不静默转 0；非有限不静默通过）、ERR-01（"不因空判
 *     动力学/器件不可行"——"—"是数据面缺失语义，绝不上升工程不可行）
 *   - 任务契约 tasks/foundation/WP-20-T05.json acceptance 2（三项静态指标
 *     可算、其余五项显示"—"、不显示为零、不参与 Pareto、不因空判不可行）
 *
 * 背景说明（第一读者须知——三件事）：
 *   ① **指标计算与候选判定分离**（PA-1 同源纪律，T04 头注②同款）：本头
 *      的 computeStaticMetrics 只产出指标事实（值或"—"＋缺失原因 token），
 *      候选级状态（Feasible/DataInsufficient/Infeasible）唯一归管线侧
 *      evidence::aggregateVerdict 判定（§7.5 判定流）——本头不产候选状态，
 *      仅提供管线联动的素材标志（StaticMetricResult.partialMarginDataInsufficient，
 *      §7.1 行 3"部分点数据不足→候选整体 DataInsufficient"）。这样"缺
 *      指标"与"工程不可行"两条语义永不混同（ERR-01/OPT-07）。
 *   ② **输入是投影接缝**（P-OPT-2 裁决前形态，与 T04 探针接缝同款）：
 *      三项静态指标的数据来源（候选 RuntimeSnapshot 几何/候选模型连杆质量/
 *      kin.task-points-batch 裕量证据）依赖 P-OPT-2 候选物化通道与域聚合
 *      评估器 opt-static-screen 的编排落位（WP-20-T06/T08 面）；裁决前由
 *      调用方/测试以 StaticMetricFacts 投影供给——指标计算本身是纯函数
 *      （O16——"optimization 指标计算纯函数"），算法面本任务交付，数据
 *      通道随装配落位。
 *   ③ **暂定口径诚实登记**（P-OPT-5）：尺寸包络的精确定义（外形包围盒 vs
 *      KIN-10 工作空间包络 Rmax）需求侧未冻结——本实现采用暂定口径
 *      "基座系几何包围盒（AABB）三向尺寸之和（m）"，在卡 §16.3 P-OPT-5
 *      允许范围内（"暂定口径展示并在留痕标注"），裁决后随卡面 §7 增量
 *      修订切换；最小关节裕量的归一化口径唯一归 kinematics（D-KIN-2 黄金
 *      锁定）——本单元零裕量算法（R-1），只合成最小值。
 *
 * 线程约束：全部纯值/纯函数；IOptimizationObjectiveProvider 只读无状态
 *   （可并发——卡 §12.3 buildFront/validate 同款纪律）。
 * 确定性：同输入同输出（canonical 段序列化按 MetricId 枚举序定宽编码，
 *   NFR-COR-02；容差显式配置进 config.opt——acceptance 1 承载面）。
 */

#ifndef SDURWS_IRD_OPTIMIZATION_OBJECTIVE_HPP
#define SDURWS_IRD_OPTIMIZATION_OBJECTIVE_HPP

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sdurws/ird/core/Compare.hpp>         // core::Tolerance——支配容差二元组
                                                // （C4 公式分量；零容差默认）
#include <sdurws/ird/evidence/Evaluator.hpp>   // evidence::EvaluatorRegistry——
                                                // 条件 2 注册检查的查询面（③端口注册表）
#include <sdurws/ird/optimization/Types.hpp>   // OptimizationStage/MetricId/
                                                // OptimizationError——阶段与指标词表

namespace sdurws::ird::optimization {

// =====================================================================
// 指标词表物化（卡 §7.1 表——八项的方向/单位/来源/可算性）
// =====================================================================

/**
 * @brief 指标优化方向（卡 §7.2——"方向在 MetricDefinition 词表冻结"）。
 *
 * Minimize＝值越小越好（包络/质量/节拍/成本/器件质量/正机械功）；
 * Maximize＝值越大越好（关节裕量/驱动裕量）。方向全运行稳定——消费方
 * （Pareto 支配判定/排序/UI 列头）不得按运行期参数改向。
 */
enum class MetricDirection { Minimize, Maximize };

/**
 * @brief 指标方向稳定 token（"min"/"max"——导出/候选表列头的确定性书写）。
 * @param d [in] 方向枚举值
 * @return 稳定 token 视图（编译期字面量）
 */
std::string_view toToken(MetricDirection d) noexcept;

/// 指标定义条目数（八项——词表长度常数；测试与位图编码消费）。
inline constexpr std::size_t kMetricCount = 8;

/**
 * @brief 单项指标定义（卡 §7.1 表一行的物化——"八项＋方向＋来源键"，
 *        §12.2 metricDefinitions() 的返回条目）。
 *
 * 值语义；线程安全：纯值。单位口径（unitToken）随卡 §7.1"单位"列：
 *   - Envelope："m"——暂定口径单位（P-OPT-5：三向尺寸 vs 体积待裁决；
 *     本实现暂定口径＝三向尺寸之和，单位 m——裁决后随卡面 §7 增量切换）；
 *   - DeviceCost：空串——货币单位随字段字典未冻结（P-OPT-5；不发明）；
 *   - MinJointMargin/MinDriveMargin："1"——无量纲（关节裕量为 kinematics
 *     D-KIN-2 归一化口径；驱动裕量比值定义待裁决——P-OPT-5）。
 */
struct MetricDefinition {
    MetricId metricId = MetricId::Envelope; ///< 指标 ID（词表值）
    MetricDirection direction = MetricDirection::Minimize; ///< 优化方向（冻结）
    std::string_view unitToken = {};  ///< SI 单位符号（"m"/"kg"/"s"/"J"/"1"/
                                      ///  空串＝货币口径未冻结——见类注）
    std::string_view sourceKey = {};  ///< 来源评估键（§8.3 第 8 步"来源评估键"）：
                                      ///  R1 三项＝kOptStaticScreenKey（§6.7 域聚合
                                      ///  评估器）；R2 五项＝§6.8 图 R2 消费链示意键
                                      ///  （注册登记随 WP-21-T02——DOPT-4 不预登记；
                                      ///  全部指向静态字面量——生命周期静态）
    std::uint32_t requiredContractVersion = 0; ///< 来源评估器契约版本要求
                                      ///  （1＝opt-static-screen §6.7 明文；
                                      ///  0＝版本未随卡面冻结——R2 键只查注册
                                      ///  命中不校验版本，见 validateObjectives 注）
    bool computableInStageB = false;  ///< 阶段 B 可算（§7.1"阶段可算"列——
                                      ///  前三项 true；后五项 false＝StageB 恒"—"）
};

/**
 * @brief optimization 域聚合评估键（§6.7 表——"opt-static-screen"，OPT-B
 *        静态子集聚合评估器；三项静态指标的唯一来源评估键。kebab 词形与
 *        evidence isValidEvaluationKey 一致——T04 评估键同款口径；卡面
 *        点形书写 "opt.static-screen.v1" 是 payload kindToken，非注册键）。
 */
inline constexpr std::string_view kOptStaticScreenKey = "opt-static-screen";

/**
 * @brief 八项指标定义词表（卡 §7.1 表全量物化——纯函数，同调用恒同值）。
 *
 * 顺序＝MetricId 枚举序（§7.1 表行序——确定性）；逐项取值依据见各字段
 * 注释与卡 §7.1 表（方向：包络/质量/节拍/成本/器件质量/正机械功＝min；
 * 关节裕量/驱动裕量＝max——§7.2 明文）。
 *
 * @return 八项定义（每次调用返回新值——确定性 NFR-COR-02）
 *
 * 纯函数；线程安全（可重入）。
 */
std::vector<MetricDefinition> metricDefinitions();

/**
 * @brief 指标优化方向直接查询（词表方向的实现单点——metricDefinitions()
 *        的 direction 列消费同一 switch，防两处口径漂移；Pareto 支配判定/
 *        排序键以本函数零分配取向——纯函数热路径，noexcept 安全）。
 *
 * @param id [in] 指标 ID
 * @return 该指标的冻结方向（§7.2——方向在 MetricDefinition 词表冻结）
 *
 * 纯函数 noexcept；线程安全（可重入）。
 */
MetricDirection metricDirectionOf(MetricId id) noexcept;

// =====================================================================
// 激活目标集（§7.3/§7.4——目标＋可选支配容差）
// =====================================================================

/**
 * @brief 单个激活目标（卡 §4.3——"激活目标＋方向＋可选支配容差"）。
 *
 * 方向不在此重复：方向唯一来源＝metricDefinitions() 词表（§7.2"方向在
 * MetricDefinition 词表冻结"——目标条目只挑指标与容差，改向无门）。
 * tolerance 为该目标的**支配比较容差**（§7.4——aᵢ 不劣于 bᵢ 判定改用
 * core::closeWithin(aᵢ, bᵢ, tᵢ) 的 tᵢ）：默认 {0,0}＝零容差（浮点全序
 * ——DOPT-5"支配比较默认零容差，不发明默认阈值"；P-OPT-5 登记默认值
 * 待裁决，零容差为安全默认）。显式配置的容差经 canonicalizeObjectiveSegment
 * 进 config.opt canonical（⇒ 进 sliceId——§4.5"目标配置（激活/方向/
 * 支配容差）→ config.opt 变化 ⇒ 新运行"）。
 *
 * 值语义；线程安全：纯值。
 */
struct ObjectiveEntry {
    MetricId metricId = MetricId::Envelope; ///< 激活的指标
    core::Tolerance tolerance = {};  ///< 支配比较容差（C4 二元组：relative×
                                     ///  |参考值|＋absolute；默认零容差——DOPT-5）
    bool operator==(const ObjectiveEntry& o) const noexcept
    {
        return metricId == o.metricId && tolerance == o.tolerance;
    }
    bool operator!=(const ObjectiveEntry& o) const noexcept { return !(*this == o); }
};

/**
 * @brief 激活目标集（卡 §4.3 ObjectiveSet——config.opt.objectives）。
 *
 * entries 保持**调用方声明序**（§7.4 稳定排序键②"目标次序＝
 * config.opt.objectives 声明序"的消费序）；canonical 序列化不消费声明序
 * （按 MetricId 枚举序重排——位图语义，见 canonicalizeObjectiveSegment），
 * 故声明序只影响 Pareto 输出排序键、不影响身份（同一目标集不同声明序
 * 的 canonical 段字节相同——身份稳定；排序键面不同——呈现序不同。两
 * 语义分层，测试钉住）。
 *
 * 构造唯一入口＝makeObjectiveSet（fail-fast 校验空集/重复/容差非法）；
 * 直接聚合初始化仅供序列化回读等已校验场景。
 *
 * 值语义；线程安全：纯值。
 */
struct ObjectiveSet {
    std::vector<ObjectiveEntry> entries = {}; ///< 激活目标（声明序；可空——
                                              ///  空集合法性由 validateObjectives
                                              ///  按研究定义场景判定）

    bool operator==(const ObjectiveSet& o) const { return entries == o.entries; }
    bool operator!=(const ObjectiveSet& o) const { return !(*this == o); }
};

/**
 * @brief 构造激活目标集（fail-fast 轨——调用方错误抛 OptimizationError）。
 *
 * 校验规则（全部不静默修正——NFR-COR-03）：
 *   ① entries 非空（空目标集无比较基础——Pareto 支配判定在空目标下
 *      退化为"全互不支配"，属研究定义配置错误，fail-fast）；
 *   ② MetricId 无重复（同一指标两条目标语义未定义——支配判定与排序键
 *      都无法确定，fail-fast）；
 *   ③ 容差两分量均 ≥0 且有限（core::Tolerance::make 的同源校验——
 *      负/NaN/Inf 容差会让支配判定失去序语义）。
 *
 * @param entries [in] 激活目标条目（声明序保留；不要求预排序）
 * @return 激活目标集（声明序原样保留）
 *
 * @throws OptimizationError(kOptInputInvalid) ①②③任一违例（消息含定位）
 */
ObjectiveSet makeObjectiveSet(const std::vector<ObjectiveEntry>& entries);

/**
 * @brief config.opt 目标段 canonical 字节（卡 §4.3——"目标集（按 MetricId
 *        枚举序位图＋每目标容差三元组）"段的编码，config.opt canonical
 *        组装器的目标段；显式容差"进 config.opt"（acceptance 1）的承载）。
 *
 * 编码格式（段内；全部多字节量小端；段外层 magic/schemaVersion 由
 * config.opt 组装器承载——随 WP-20-T06 消费面落位，本函数只产目标段）：
 *   [0]     u8 位图（bit i＝MetricId 枚举值 i 被激活；8 项一字节——
 *           "三元组"的激活位）
 *   随后每个激活目标（**按 MetricId 枚举序**，非声明序——位图序遍历）：
 *     f64 ε_rel（IEEE 754 位模式小端——容差相对分量，无量纲）
 *     f64 ε_abs（同上——容差绝对分量，单位随指标 SI 单位）
 *   （每激活目标共 17 字节＝1 激活位〔在位图中〕＋16 容差字节；"三元组"
 *   ＝激活位＋ε_rel＋ε_abs——卡 §4.3 原文读法）
 *
 * 确定性：同目标集（任意声明序）⇒ 同段字节（枚举序重排——身份稳定，
 * 声明序不入身份，I-OPT-8 同源口径）。
 *
 * @param objectives [in] 激活目标集（须经 makeObjectiveSet 构造——空集/
 *                    重复/容差非法即抛）
 * @return 目标段 canonical 字节（1＋17×激活数 字节）
 *
 * @throws OptimizationError(kOptInputInvalid) 空集/重复 MetricId/容差非法
 *         （同 makeObjectiveSet 校验面——最后防线，防绕过构造入口）
 */
std::vector<std::uint8_t> canonicalizeObjectiveSegment(const ObjectiveSet& objectives);

// =====================================================================
// 目标校验（§7.3 表——四条件的静态可查子集；报告轨）
// =====================================================================

/**
 * @brief 单条目标校验问题（报告轨条目——比较型定位：码＋指标＋细节）。
 *
 * code 取值域＝DiagCodes.hpp 常量：kOptStageLocked（条件 1——阶段锁）/
 * kOptEvaluatorMissing（条件 2——来源评估器未注册或契约版本不符）/
 * kOptInputInvalid（目标集本身非法——空/重复）。这些码在研究定义校验
 * 场景是**启动阻塞素材**（§6.5：阶段锁诊断呈现为阻塞横幅＋缺项清单，
 * 绝不静默降级、绝不呈现为候选淘汰）。
 *
 * 值语义；线程安全：纯值。
 */
struct ObjectiveValidationIssue {
    std::string code = {};        ///< OPT-* 稳定码 token（DiagCodes.hpp 常量）
    std::string metricToken = {}; ///< 涉事指标 token（"opt.metric.*"；目标集
                                  ///  整体性问题为空串）
    std::string detail = {};      ///< 中文开发诊断（违规语义＋建议动作——ERR-01）
};

/**
 * @brief 目标校验报告（§12.2 ObjectiveValidationReport——accepted=false
 *        即研究定义含被拒目标，运行启动被阻塞）。
 */
struct ObjectiveValidationReport {
    bool accepted = false;                        ///< 全部通过（四条件静态子集）
    std::vector<ObjectiveValidationIssue> issues = {}; ///< 全部问题（按发现序
                                      ///  ——检查序固定：目标集整体 → 逐条目
                                      ///  声明序先阶段锁后评估器注册，确定性首错）

    /// @brief 是否存在指定稳定码的问题（测试与编排定位用）。
    bool hasCode(std::string_view code) const noexcept;
};

/**
 * @brief 目标激活集校验（§7.3 四条件中**可在研究定义静态面执行的两条**；
 *        纯只读检查——不改任何状态，可重复调用）。
 *
 * 检查序（固定——确定性）：
 *   ① 目标集整体合法（非空/无重复——kOptInputInvalid；容差合法性已由
 *      makeObjectiveSet 挡在构造，此处防御性复验——非有限容差
 *      kOptInputInvalid）；
 *   ② 逐条目（声明序）条件 1·阶段允许（§7.3 行 1：StageB 仅 MetricId
 *      1~3；StageD 全部八项）——违例 kOptStageLocked（**不静默降级、
 *      不剔除**——§6.5 拒绝即终止该研究启动）；
 *   ③ 逐条目（声明序）条件 2·已注册合法评估器（§7.3 行 2：来源评估键
 *      在注册表中且契约版本匹配）——registry.find(sourceKey) 未命中
 *      kOptEvaluatorMissing；requiredContractVersion>0 时另查
 *      contractVersionMatches 不符同码（0＝版本未冻结面——只查命中，
 *      R2 键口径）；registry 为空指针视为"注册表未装配"——逐条目
 *      kOptEvaluatorMissing（不抛——校验是报告轨）。
 *
 * 条件 3（具备完整证据）归评估期逐候选检查（§7.3 行 3 检查点"评估期"）；
 * 条件 4（比较基准一致，checkComparisonBaselinesConsistent）归 Pareto
 * 前置（§7.3 行 4"检查点 Pareto 前置"；evidence::Verdict.hpp:663 唯一
 * 实现——消费不复制）——两者均不在本静态面（范围诚实登记）。
 *
 * @param objectives [in] 待校验激活目标集
 * @param stage      [in] 优化阶段（条件 1 的阶段可算性判定）
 * @param registry   [in] ③端口评估器注册表（条件 2 查询面；调用方持有，
 *                    调用期间有效；可为 nullptr——见 ③ 口径）
 * @return 校验报告（accepted＝静态子集全过；逐问题定位见 issues）
 *
 * 线程安全：只读纯函数面（可并发）。
 */
ObjectiveValidationReport validateObjectives(const ObjectiveSet& objectives,
                                             OptimizationStage stage,
                                             const evidence::EvaluatorRegistry* registry);

/**
 * @brief 阶段默认激活目标集（卡 §7.3 默认激活行——OPT-07/§15.0 分期口径）。
 *
 * StageB ＝ {Envelope, StructuralMass, MinJointMargin}（三项静态可算指标
 * ——REQUIREMENTS §15.0"三项静态指标"）；StageD ＝ {CycleTime,
 * StructuralMass, DeviceCost}（全量版本默认三项——RV-03/F-02 修订口径）。
 * 其余指标由用户显式激活（八项之外无目标；本函数产出可直接过
 * validateObjectives 的阶段面——R1 装配含 opt-static-screen 注册时）。
 *
 * @param stage [in] 优化阶段
 * @return 默认激活目标集（声明序＝上列序；容差全零——DOPT-5）
 *
 * 纯函数；线程安全（可重入）。
 */
ObjectiveSet defaultObjectives(OptimizationStage stage);

// =====================================================================
// 三项静态指标计算（§7.1 行 1~3——OPT-B 可算；投影接缝＋纯函数）
// =====================================================================

/**
 * @brief 指标缺失原因 token（§8.3 第 8 步 not-computable 位的展开——
 *        **记录面稳定 token，非诊断码**：§6.6 登记表封闭（15 码），指标
 *        缺失的呈现诊断唯一为 OPT-METRIC-NOT-COMPUTABLE（warning），本
 *        词表只承载"为什么不可算"的记录语义（导出/审计/测试观测点），
 *        不进诊断目录、不私造 OPT-* 码（NFR-MNT-03）。词形沿用
 *        Constraint.hpp kReject* 的域记录 token 惯例（"opt.metric-gap.*"）；
 *        值一经交付不得改动（登记于单元卡 §7 增量修订——DTB §5.4 面）。
 */
/// 阶段不可算（StageB 的五项 D-only 指标恒此——§7.1"阶段可算"列）。
inline constexpr std::string_view kMetricGapStage = "opt.metric-gap.stage";
/// 来源数据缺失（缺几何/连杆质量 NotProvided/Must 工位裕量证据整体缺席
/// 或全部数据不足——§7.1 各行"缺失语义"列）。
inline constexpr std::string_view kMetricGapSourceMissing
    = "opt.metric-gap.source-missing";
/// 部分工位数据不足（Must 点部分缺裕量——候选整体 DataInsufficient 素材，
/// §7.1 行 3；指标值本身不输出）。
inline constexpr std::string_view kMetricGapPartialData = "opt.metric-gap.partial-data";
/// 输入非有限（NaN/Inf/负质量/包围盒面序反转——上游事实缺陷；不静默转 0，
/// NFR-COR-03；§7.2"指标值非有限→评估失败处理"的指标侧素材）。
inline constexpr std::string_view kMetricGapNonFinite = "opt.metric-gap.non-finite";

/**
 * @brief 尺寸包络几何投影（候选 RuntimeSnapshot 几何的包围度量——§7.1
 *        行 1 数据来源；P-OPT-2 裁决前由调用方/测试供给）。
 *
 * 坐标系：**基座系 {B}**（候选机器人基座坐标系——卡 §7.1 行 1"在基座系
 * 的包围度量"；基座在编译产物中的世界位姿归 runtime 编译链，本投影不涉
 * 世界系）。单位：m（米，SI——六面坐标同单位）。
 *
 * 暂定口径（P-OPT-5 登记中，裁决后随卡面 §7 增量切换）：包络值＝三向
 * 尺寸之和 (xMax−xMin)＋(yMax−yMin)＋(zMax−zMin)，单位 m。
 *
 * 值语义；线程安全：纯值。
 */
struct EnvelopeFacts {
    double xMin = 0.0; ///< 包围盒 x 下界（基座系，m）
    double yMin = 0.0; ///< 包围盒 y 下界（基座系，m）
    double zMin = 0.0; ///< 包围盒 z 下界（基座系，m）
    double xMax = 0.0; ///< 包围盒 x 上界（基座系，m；≥xMin 为合法——反转
                       ///  视为非有限同型输入缺陷）
    double yMax = 0.0; ///< 包围盒 y 上界（基座系，m；≥yMin 同上）
    double zMax = 0.0; ///< 包围盒 z 上界（基座系，m；≥zMin 同上）
};

/**
 * @brief 连杆质量事实投影（候选模型连杆 body.mass 的 SourcedValue 投影
 *        ——§7.1 行 2 数据来源"SourcedValue Provided 项"）。
 *
 * provenanceToken 为 core::FieldState/ValueProvenance 的稳定 token 投影
 * （"provided"等——core::SourcedValue token 词表；估算值带估算标记，
 * DYN-06 同型**不包装为精确**——本字段只记录，本单元不判来源等级）。
 *
 * 值语义；线程安全：纯值。
 */
struct LinkMassFact {
    std::string linkSubject = {};      ///< 连杆对象定位（ObjectId 规范文本
                                       ///  "obj-<32hex>"——ERR-01 定位面）
    std::optional<double> massKg = {}; ///< 质量（kg，SI；nullopt＝NotProvided
                                       ///  ——缺失语义见 computeStaticMetrics）
    std::string provenanceToken = {};  ///< 来源记录 token（"provided"/估算标记；
                                       ///  仅 massKg 有值时有语义）
};

/**
 * @brief Must 工位关节裕量事实投影（kin.task-points-batch 证据的逐点
 *        最小裕量——§7.1 行 3 数据来源"Must 工位 IK 解集的关节限位裕量
 *        最小值"）。
 *
 * 裕量数值的**计算与归一化口径唯一归 kinematics**（D-KIN-2 统一尺度规则
 * 黄金锁定；本单元零裕量算法——R-1 红线）；本投影是证据值的机械承载。
 * minMargin 为该 Must 工位 IK 解集的逐解最小裕量（若批量评估输出逐解
 * 列表，上游取 min 后填入——聚合规则"解集最小值"属 §7.1 行 3 口径）。
 *
 * 值语义；线程安全：纯值。
 */
struct PointMarginFact {
    std::string caseIdText = {};       ///< Must 工位 id 文本（工况追溯面——
                                       ///  evidence::CaseId 的书写形态）
    std::optional<double> minMargin = {}; ///< 该点最小关节限位裕量（无量纲
                                       ///  ——kin D-KIN-2 归一化口径，unitSymbol
                                       ///  恒 "1"；nullopt＝该点数据不足）
    std::string unitSymbol = "1";      ///< 单位符号（归一化裕量无量纲——"1"；
                                       ///  卡 §7.1 行 3 单位列的 rad/m 为
                                       ///  kinematics 原始量纲、归一化后无量纲）
};

/**
 * @brief 三项静态指标的候选事实投影（computeStaticMetrics 输入——评估
 *        编排产出；P-OPT-2 裁决前由调用方/测试供给——文件头注②）。
 *
 * 五项 D-only 指标（节拍/成本/器件质量/正机械功/驱动裕量）的输入面
 * **不在本结构**：其可算性归 OPT-D 联合评估面（WP-21-T04——§7.1 行
 * 4~8 数据来源 trj/sel/dyn 证据），R1 阶段恒"—"（StageNotComputable），
 * 不预建输入占位（NFR-MNT-04）。
 *
 * 值语义；线程安全：纯值（各调用持各自输入）。
 */
struct StaticMetricFacts {
    /// 尺寸包络几何投影（nullopt＝缺几何——§7.1 行 1 缺失语义→"—"）
    std::optional<EnvelopeFacts> envelope = {};
    /// 参与合成的连杆质量表（空表＝无连杆事实→"—"；任一 NotProvided→
    /// "—"，不按 0 合成——NFR-COR-03/§7.1 行 2）
    std::vector<LinkMassFact> linkMasses = {};
    /// Must 工位裕量表（空表＝Must 点全部数据不足→"—"；部分点 nullopt→
    /// PartialData＋候选 DataInsufficient 素材标志——§7.1 行 3）
    std::vector<PointMarginFact> pointMargins = {};
};

/**
 * @brief 单指标计算结果（§8.3 第 8 步指标事实的记录条目——{MetricId,
 *        值(SI)或 not-computable, 来源评估键〔由消费方补——词表面〕,
 *        ValueProvenance〔投影携带〕}的最小承载）。
 *
 * 值语义；线程安全：纯值。
 */
struct MetricComputation {
    MetricId metricId = MetricId::Envelope; ///< 指标 ID
    std::optional<double> valueSi = {}; ///< 计算值（SI 单位随指标——m/kg/
                                        ///  无量纲；nullopt＝not-computable，
                                        ///  显示"—"——**绝不输出 0 冒充**，
                                        ///  OPT-07/UX-03）
    std::string_view gapToken = {}; ///< 缺失原因 token（kMetricGap* 词表；
                                    ///  有值时为空视图）
    std::string detail = {};        ///< 中文说明（缺失定位/暂定口径标注——
                                    ///  审计与测试观测面）
};

/**
 * @brief 三项静态指标计算结果（八项全量——OPT-07"全部展示"的产出形态；
 *        五项 D-only 恒带 gap，候选表八列固定呈现）。
 */
struct StaticMetricResult {
    /// 八项指标事实（**MetricId 枚举序**——候选表列序＝词表序，确定性）
    std::vector<MetricComputation> metrics = {};
    /// 部分工位裕量缺失→候选整体 DataInsufficient 素材（§7.1 行 3——
    /// 管线联动输入；候选状态判定仍归管线 aggregateVerdict，PA-1）
    bool partialMarginDataInsufficient = false;

    /// @brief 取指定指标的当前值（nullopt＝"—"；越界 MetricId 属调用方
    ///        违约——断言轨，抛 std::invalid_argument）。
    std::optional<double> valueOf(MetricId id) const;
};

/**
 * @brief 计算三项静态指标并产出八项全量记录（§6.2 第 11 步之后的指标
 *        计算面——纯函数；WP-20-T05 交付的 OPT-B 可算指标实现）。
 *
 * 计算规则（逐指标——缺失即"—"，错误语义归类见各分支）：
 *   1. 尺寸包络（暂定口径＝基座系 AABB 三向尺寸之和，m——文件头注③）：
 *      缺几何投影→SourceMissing；任一坐标非有限或任一轴上界<下界
 *      （面序反转）→NonFiniteInput；否则值＝三向尺寸之和。
 *   2. 结构质量（kg）：连杆表空→SourceMissing（无合成对象）；任一连杆
 *      NotProvided→SourceMissing（**不按 0 合成**——NFR-COR-03）；任一
 *      Provided 值非有限或为负→NonFiniteInput（负质量属物理不可能的
 *      上游事实缺陷）；否则值＝Σ mass（kg，求和顺序＝投影表序——浮点
 *      求和的确定性约定，NFR-COR-02）。
 *   3. 最小关节裕量（无量纲——kin D-KIN-2 归一化）：工位表空或全部点
 *      minMargin 缺失→SourceMissing（"Must 点全部数据不足→'—'"）；部分
 *      点缺失→PartialData＋partialMarginDataInsufficient=true（值不输出
 *      ——候选整体 DataInsufficient 素材，§7.1 行 3）；有值点中任一非
 *      有限→NonFiniteInput；否则值＝可算点的最小值。
 *   4~8. 节拍/器件成本/器件质量/正机械功/最小驱动裕量：stage==StageB
 *      →StageNotComputable（§7.1"阶段可算"列——恒"—"，**不因空判动力
 *      学/器件不可行**——数据面缺失语义，绝不上升工程不可行，ERR-01）；
 *      stage==StageD→SourceMissing（detail 标注"OPT-D 联合评估面随
 *      WP-21-T04 落位"——诚实登记未实现，不伪造可算）。
 *
 * 非有限中间量防护：所有输出值经 std::isFinite 复核——理论上不可达
 * （输入已过滤），复核失败按 NonFiniteInput 处理（纵深防御，不吞错）。
 *
 * @param stage  [in] 优化阶段（决定 4~8 号指标的 gap 语义——StageB 恒
 *               stage 类缺失）
 * @param facts  [in] 候选事实投影（调用方持有；调用期间有效）
 * @return 八项全量结果（metrics 按 MetricId 枚举序）
 *
 * 确定性：同 (stage, facts) ⇒ 同结果（纯函数，NFR-COR-02）。
 * 线程安全：纯函数（可并发）。
 */
StaticMetricResult computeStaticMetrics(OptimizationStage stage,
                                        const StaticMetricFacts& facts);

// =====================================================================
// 目标提供接口（§12.2 IOptimizationObjectiveProvider——OPT-07 编排面）
// =====================================================================

/**
 * @brief 目标指标配置与八项指标词表提供接口（§12.2 原文签名——Draft
 *        基线的磁盘实况适配：registry 参数按 evidence 磁盘类型
 *        EvaluatorRegistry 承载〔§12.2 草拟名 EvaluatorRegistryView 在
 *        evidence 磁盘头中不存在——实现口径登记 DTB §5.4〕）。
 *
 * 错误语义（§12.3）：validateObjectives 为报告轨（拒绝不抛——编排面
 * 呈现阻塞清单）；metricDefinitions/defaultObjectives 纯函数无错误轨。
 */
class IOptimizationObjectiveProvider {
public:
    virtual ~IOptimizationObjectiveProvider() = default;

    /**
     * @brief 八项指标定义词表（§12.2——"八项＋方向＋来源键"）。
     * @return 八项定义（MetricId 枚举序——确定性）
     *
     * 线程安全：只读纯函数面（可并发）。
     */
    virtual std::vector<MetricDefinition> metricDefinitions() const = 0;

    /// @copydoc validateObjectives
    /// （接口委托同一实现——报告轨语义见自由函数注）
    virtual ObjectiveValidationReport validateObjectives(
        const ObjectiveSet& objectives, OptimizationStage stage,
        const evidence::EvaluatorRegistry* registry) const = 0;
};

/**
 * @brief 目标提供唯一产品实现（§12.2 O8 面；只读无状态——实例进程级
 *        共享安全，全部委托同名自由函数）。
 */
class OptimizationObjectiveProvider final : public IOptimizationObjectiveProvider {
public:
    /// @copydoc IOptimizationObjectiveProvider::metricDefinitions
    std::vector<MetricDefinition> metricDefinitions() const override;

    /// @copydoc IOptimizationObjectiveProvider::validateObjectives
    ObjectiveValidationReport validateObjectives(
        const ObjectiveSet& objectives, OptimizationStage stage,
        const evidence::EvaluatorRegistry* registry) const override;
};

}  // namespace sdurws::ird::optimization

#endif  // SDURWS_IRD_OPTIMIZATION_OBJECTIVE_HPP
