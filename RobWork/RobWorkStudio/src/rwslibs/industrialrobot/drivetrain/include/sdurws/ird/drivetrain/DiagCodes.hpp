/**
 * @file   DiagCodes.hpp
 * @brief  drivetrain 稳定诊断码登记表——DT-* 码值常量（19 码：卡 §6.3
 *         阻断面 8 码＋§7.2 矩阵形态 4 码＋§9 惯量 2 码＋§10 效率/统计
 *         4 码＋§12.1 序列契约 1 码）与登记清单函数。
 *
 * 设计依据：
 *   - units/drivetrain.md §6.3（R1 阻断面表——8 码逐条触发条件）、§7.2
 *     （必须阻止的矩阵形态表——4 新增码）、§9.2/§9.3（反射惯量两码）、
 *     §10.1/§10.4/§10.7（效率与统计四码）、§12.1（序列契约一码）、§1.3
 *     （诊断码 DT-* 为建议值——码值分配与合法性权威＝diagnostics 的
 *     StableCodeRegistry，core 仅承载 DiagCode 句法）、D-DT-15（诊断码
 *     前缀 DT-；注册归 diagnostics；命名空间清单补登 P-DT-7）
 *   - units/core.md §4.8（DiagCode 句法 ^[A-Z0-9]+(-[A-Z0-9]+)*$ 且 ≤64
 *     ——core 仅承载；CR-08 码值权威在 diagnostics StableCodeRegistry）
 *     ＋core/DiagData.hpp（core::DiagCode＝std::string token、
 *     DiagnosticRecord 产码承载）
 *   - 需求 ERR-01（稳定诊断码＋subjectObjectId＋原因＋建议动作）、
 *     NFR-MNT-03（码/文案单一权威——码值权威唯一在 diagnostics）、
 *     NFR-COR-03（非有限/非法输入不得静默转 0 或默认通过——阻断面码的
 *     依据）
 *   - 任务契约 tasks/foundation/WP-18-T02.json acceptance 2（"DT-* 域
 *     诊断码按 units/drivetrain.md 登记表装配期注册"）
 *
 * ★ 传动比口径声明（契约 note 落笔——P-DT-10 本卡采用口径，D-DT-4 全文
 * 唯一约定）：本单元全部公共面对传动比统一采用
 *     c ＝ Δq_joint / Δθ_motor
 * （关节位移增量／电机位移增量，无量纲 rad/rad 或 m/m；减速器 n:1 对应
 * c＝1/n；对角情形 C＝diag(c_1..c_n)；虚功对偶 τ_motor＝Cᵀ·τ_joint；
 * "J·i²" 记法按 i＝1/c 换算）。禁用第二种未声明约定（如 n＝θ/q 记法）
 * ——上游卡（modeling/runtime/optimization）若冻结为 n 口径，换算规则
 * 随组装方契约登记（映射核心只见 c，身份不受呈现口径影响——P-DT-10
 * 卡面处置原文）；本头码值语义中"传动比"一律指 c 口径。
 *
 * 背景说明（码值权威链——为什么 drivetrain 只产出"登记表"不产出
 * "注册函数"）：稳定码的注册表唯一权威＝diagnostics::StableCodeRegistry
 * （PA-1/NFR-MNT-03），码值文本的合法性权威＝units/drivetrain.md 各节
 * 登记值（建议值——D-DT-15）。本头把卡面**全部 19 码**的登记值物化为
 * 码值常量（inline constexpr string_view——T03+ 产码路径与 L5 装配共用
 * 唯一书写点）与登记行清单（码值＋卡面出处＋语义原文）。装配期注册的
 * 执行面与 kinematics（registerKinematicsCodes）不同，差异来自依赖白
 * 名单的硬约束（诚实登记，非遗漏）：
 *   1. drivetrain 依赖白名单仅 core＋evidence（卡 §3.2——ARCH §3.5
 *      原始两边；卡面点名 drivetrain→policy/runtime/**diagnostics**＝
 *      表外边，公共头也不可 include）——本头不可消费
 *      diagnostics::CodeDescriptor/IDiagnosticRegistry，登记行以纯
 *      std 类型承载；
 *   2. 真实 StableCodeRegistry 注册要求码首段在前缀-所有权表内
 *      （diagnostics/src/DiagCodes.cpp kPrefixOwners——当前 17 前缀，
 *      无 DT）；DT 前缀补登（diagnostics.md §4.5 命名空间清单＋代码
 *      前缀表同步）＝diagnostics 所有者治理动作（P-DT-7"随本卡收编"
 *      ——登记状态，两处文件均不在本任务 allowedFiles）。
 *   因此本单元的"装配期注册"落地为：登记表物化（本头/本实现）＋L5
 *   装配与 diagnostics 收编时的唯一数据源契约——L5 装配清单在 DT 前缀
 *   收编后，按本登记表逐行构造 CodeDescriptor 注册进
 *   StableCodeRegistry（码值/出处/语义三列即卡面登记值的完备承载；
 *   级别/分类/paramSchema 等 diagnostics 词表字段的落值裁决随 P-DT-7
 *   收编动作执行——卡面未给级别列，本实现不私造，NFR-MNT-03 单一权威
 *   纪律）。
 *
 * 线程安全：drivetrainCodeEntries() 纯函数（可重入）；码值常量
 * constexpr（编译期常量，无共享可变状态）。确定性：清单序＝卡面章节序
 * （§6.3 → §7.2 → §9 → §10 → §12.1，同章按表行序——确定性序，
 * NFR-COR-02），后续任务追加码只允许表尾追加、既有行不重排（清单序
 * 进入登记契约——kinematics §9.6 行序纪律同款）。
 */

#ifndef IRD_DRIVETRAIN_DIAGCODES_HPP
#define IRD_DRIVETRAIN_DIAGCODES_HPP

#include <string_view>
#include <vector>

namespace sdurws::ird::drivetrain {

// =====================================================================
// DT-* 码值常量（唯一书写点——DiagCodes.cpp 登记清单与 WP-18-T03+
// 产码路径共用同一常量，禁字符串拼码/第二处字面量；modeling/requirements/
// kinematics 码值常量纪律的 DT 侧同款）。
// 清单序＝卡面章节序（§6.3 → §7.2 → §9 → §10 → §12.1）——登记契约序，
// 追加只允许表尾（见文件头注"确定性"段）。
// =====================================================================

// ---- §6.3 R1 阻断面（映射入口结构检查顺序——首个命中即阻止）----

/// §6.3 行 1：DT-COUPLING-STAGE-LOCKED（R1 能力未启用时收到耦合窗口/
/// 非对角输入——含"UI/配置中出现 R2 标签"的情形，显示/配置标签不改变
/// 计算能力；MDL-21（R2）、M-6、AT-38"R1 阻断反例"）。
inline constexpr std::string_view kDtCouplingStageLocked = "DT-COUPLING-STAGE-LOCKED";
/// §6.3 行 2：DT-MATRIX-NONDIAGONAL-LOCKED（R1 收到非对角矩阵——任一
/// 非对角元素非零；不得对角化绕过、不得静默拆成独立轴）。
inline constexpr std::string_view kDtMatrixNondiagonalLocked = "DT-MATRIX-NONDIAGONAL-LOCKED";
/// §6.3 行 3：DT-AXIS-TYPE-OUT-OF-SCOPE（链型/关节类型范围外——mimic/
/// 闭环/planar/floating 关节、prismatic 轴；SEL-09"范围外"诊断、MDL-12；
/// 不静默套用旋转传动）。
inline constexpr std::string_view kDtAxisTypeOutOfScope = "DT-AXIS-TYPE-OUT-OF-SCOPE";
/// §6.3 行 4：DT-RATIO-ZERO（结构有效性——归一化矩阵对角元素含 0；
/// c＝Δq_joint/Δθ_motor 的 c=0 使除法无意义，非法）。
inline constexpr std::string_view kDtRatioZero = "DT-RATIO-ZERO";
/// §6.3 行 5（§7.2 同码）：DT-MATRIX-NONFINITE（矩阵含 NaN/±Inf 任一
/// 元素——NFR-COR-03 精确判据"非有限即拒绝"）。
inline constexpr std::string_view kDtMatrixNonfinite = "DT-MATRIX-NONFINITE";
/// §6.3 行 6（§7.2 同码）：DT-INPUT-DIMENSION-MISMATCH（维度不匹配——
/// 行列数与适用关节/电机轴集合不一致；R2 窗口与自由轴集合重叠同码）。
inline constexpr std::string_view kDtInputDimensionMismatch = "DT-INPUT-DIMENSION-MISMATCH";
/// §6.3 行 7：DT-INPUT-AXIS-ORDER-MISMATCH（轴序不一致——关节侧/电机侧
/// 排列与约定序不符；不允许静默重排，重排须由组装方显式完成）。
inline constexpr std::string_view kDtInputAxisOrderMismatch = "DT-INPUT-AXIS-ORDER-MISMATCH";
/// §6.3 行 8：DT-INPUT-EMPTY（空输入——关节轴表空/上游序列空；空模型
/// 没有评估意义，fail-fast，不得发布完整结果——EVI/TASK-02）。
inline constexpr std::string_view kDtInputEmpty = "DT-INPUT-EMPTY";

// ---- §7.2 必须阻止的矩阵形态（§6.3 未含的 4 新增码）----

/// §7.2 行 1：DT-MATRIX-NONSQUARE（非方矩阵——电机轴数≠适用关节数，
/// 如差动/冗余驱动；R2 按 MDL-21/runtime 口径不支持）。
inline constexpr std::string_view kDtMatrixNonsquare = "DT-MATRIX-NONSQUARE";
/// §7.2 行 2：DT-MATRIX-SINGULAR（奇异矩阵——det≈0/不可逆；不得以
/// 伪逆放行）。
inline constexpr std::string_view kDtMatrixSingular = "DT-MATRIX-SINGULAR";
/// §7.2 行 3：DT-MATRIX-ILL-CONDITIONED（病态矩阵——条件数超限；比较型：
/// 实际条件数/阈值/无量纲，阈值来源 P-RT-7 对齐〔裁决前设计默认
/// 1×10⁸——卡 §7.3〕）。
inline constexpr std::string_view kDtMatrixIllConditioned = "DT-MATRIX-ILL-CONDITIONED";
/// §7.2 行 6：DT-MATRIX-TIME-VARYING-UNSUPPORTED（时变矩阵——建模期不
/// 可含时变/工况项，映射期发现即明确列为不适用，不得当作常矩阵计算）。
inline constexpr std::string_view kDtMatrixTimeVaryingUnsupported = "DT-MATRIX-TIME-VARYING-UNSUPPORTED";

// ---- §9 反射惯量 ----

/// §9.2：DT-INERTIA-INVALID（转子等效惯量非法——负值/零/非有限；
/// J_rotor 必须＞0 且有限，单位 kg·m²〔电机轴系〕）。
inline constexpr std::string_view kDtInertiaInvalid = "DT-INERTIA-INVALID";
/// §9.3：DT-INERTIA-NOT-POSITIVE-DEFINITE（R2 关节侧反射惯量矩阵
/// Cholesky 正定性判定失败——属输入/矩阵非法，阻止并诊断）。
inline constexpr std::string_view kDtInertiaNotPositiveDefinite = "DT-INERTIA-NOT-POSITIVE-DEFINITE";

// ---- §10 效率、功率、能量与统计 ----

/// §10.1：DT-EFFICIENCY-INVALID（效率值非法——η≤0 或 η＞1；比较型：
/// 实际值/期望范围/无量纲；η 缺失走 DataInsufficient 降级素材非本码）。
inline constexpr std::string_view kDtEfficiencyInvalid = "DT-EFFICIENCY-INVALID";
/// §6.2/§10.7：DT-ROTOR-MISSING（转子惯量缺失——含转子项力矩不可得，
/// 力矩按理想口径输出＋限定标记；不伪造数值）。
inline constexpr std::string_view kDtRotorMissing = "DT-ROTOR-MISSING";
/// §10.4：DT-INPUT-SAMPLE-MISSING（缺样本——循环不完整，统计标
/// DataInsufficient 素材；不输出"部分 RMS 冒充完整循环 RMS"）。
inline constexpr std::string_view kDtInputSampleMissing = "DT-INPUT-SAMPLE-MISSING";
/// §10.4/§12.1：DT-INPUT-TIME-NONMONOTONIC（时间非单调——不排序吞错；
/// 上游序列时间戳必须严格单调递增）。
inline constexpr std::string_view kDtInputTimeNonmonotonic = "DT-INPUT-TIME-NONMONOTONIC";

// ---- §12.1 上游序列契约 ----

/// §12.1：DT-SERIES-LENGTH-MISMATCH（输入长度不一致——逐时间戳对齐的
/// 等长数组契约破坏；本卡不重采样、不插值）。
inline constexpr std::string_view kDtSeriesLengthMismatch = "DT-SERIES-LENGTH-MISMATCH";

// =====================================================================
// DT-* 登记行与全表清单（units/drivetrain.md 登记值的物化——L5 装配期
// 注册进 diagnostics StableCodeRegistry 的数据源；见文件头注"码值权威
// 链"段）
// =====================================================================

/**
 * @brief 单码登记行（卡面登记值的三列承载——纯值聚合，零外部依赖）。
 *
 * 字段取舍口径（诚实边界）：drivetrain.md 的 DT-* 登记值散布于 §6.3/
 * §7.2/§9/§10/§12.1 各节，卡面给出并经本表物化的只有三列——码值、
 * 出处（章节锚点）、语义（卡面原文压缩）。diagnostics 词表字段（级别/
 * 分类/paramSchema/重试族等）卡面未逐码给出，本表不承载（不私造登记值
 * ——NFR-MNT-03 单一权威；落值裁决随 P-DT-7 收编动作由 diagnostics
 * 所有者按其卡 §4.3/§4.4 词表执行）。
 */
struct DiagnosticEntry {
    std::string_view code;          ///< 码值（上方常量——唯一书写点引用）
    std::string_view sourceClause;  ///< 卡面登记出处（units/drivetrain.md §x.y 锚点）
    std::string_view semantics;     ///< 卡面语义（触发条件/处置口径的一句话登记）
};

/**
 * @brief 产出 DT-* 全表（19 码）的登记行清单（units/drivetrain.md 登记表
 *        的物化——装配期注册进 diagnostics StableCodeRegistry 的数据源；
 *        WP-18-T03+ 产码路径的码值语义对照面）。
 *
 * 清单序＝卡面章节序（§6.3 表行 1~8 → §7.2 新增 4 码 → §9 两码 →
 * §10 四码 → §12.1 一码——确定性序，NFR-COR-02）；每次调用返回同序同值
 * 新清单（纯值聚合）。追加纪律：后续任务新增码只允许表尾追加并走单元卡
 * 增量修订（kinematics §9.6 行序纪律同款）——既有行不重排。
 *
 * @return 登记行清单（顺序＝卡面章节序；调用方持有）
 */
std::vector<DiagnosticEntry> drivetrainCodeEntries();

}  // namespace sdurws::ird::drivetrain

#endif  // IRD_DRIVETRAIN_DIAGCODES_HPP
