/**
 * @file   DiagCodes.cpp
 * @brief  drivetrain 稳定诊断码登记表的实现——DT-* 全表 19 码登记行清单
 *         （逐码出处与语义登记）。
 *
 * 设计依据：
 *   - units/drivetrain.md §6.3（R1 阻断面表——顺序执行、首个命中即阻止；
 *     阻断结果不伪装成"传动不可行"）、§7.2（矩阵形态全表）、§9.2/§9.3、
 *     §10.1/§10.4/§10.7、§12.1（各码登记值与处置口径的唯一权威）、
 *     §1.3/D-DT-15（DT-* 建议值；注册归 diagnostics；P-DT-7 前缀补登）
 *   - units/core.md §4.8（DiagCode 句法——core 仅承载；本清单码值经
 *     DiagCodesTest 以 core::DiagnosticRecord 句法权威校验）
 *   - 先例：kinematics/src/DiagCodes.cpp（码值登记物化的同款形态——
 *     drivetrain 按依赖白名单以纯 std 类型承载，差异见头文件说明）
 *   - 任务契约 tasks/foundation/WP-18-T02.json acceptance 2
 *
 * 确定性（NFR-COR-02）：清单序＝卡面章节序（§6.3 → §7.2 → §9 → §10 →
 * §12.1，同章按表行序）；每次调用返回同序同值新清单（登记行为纯值聚合）；
 * 码值经 DiagCodes.hpp 常量引用（唯一书写点）。
 */

#include <sdurws/ird/drivetrain/DiagCodes.hpp>

namespace sdurws::ird::drivetrain {

std::vector<DiagnosticEntry> drivetrainCodeEntries()
{
    // 清单序＝卡面章节序（登记契约序——追加只允许表尾）；逐码注释给出
    // 卡面出处与语义登记原文（触发条件/处置口径）。
    return {
        // ---- §6.3 行 1：R1 能力门控 ----
        // R1 能力未启用时收到耦合窗口/非对角输入（含"UI/配置中出现 R2
        // 标签"的情形——显示/配置标签不改变计算能力，能力由装配清单与
        // 算法版本决定）。阻断映射，不静默绕过（MDL-21（R2）、M-6、
        // AT-38"R1 阻断反例"）。
        {kDtCouplingStageLocked,
         "units/drivetrain.md §6.3",
         "R1 能力门控：耦合窗口输入被阻止（含 R2 标签不改变能力）；不对角化、不拆轴绕过"},

        // ---- §6.3 行 2：非对角矩阵 ----
        // 归一化矩阵任一非对角元素非零即阻止——不得对角化绕过、不得
        // 静默拆成独立轴（丢失交叉耦合项＝违反 M-6/AT-38 纪律）。
        {kDtMatrixNondiagonalLocked,
         "units/drivetrain.md §6.3",
         "R1 非对角矩阵被阻断：保留交叉耦合语义，禁止对角化近似与静默拆轴"},

        // ---- §6.3 行 3：链型/关节类型范围外 ----
        // mimic/闭环/planar/floating 关节、prismatic 轴——"范围外"诊断，
        // 该轴不产出电机侧结果；不静默套用旋转传动（SEL-09、MDL-12；
        // mimic/闭环成模在 modeling 侧已被 R1 阻断，此处为映射侧第二道
        // 防线）。
        {kDtAxisTypeOutOfScope,
         "units/drivetrain.md §6.3",
         "链型/关节类型范围外（mimic/闭环/planar/floating/prismatic）：不产出电机侧结果，不静默套用旋转传动"},

        // ---- §6.3 行 4：零传动比 ----
        // c＝Δq_joint/Δθ_motor（P-DT-10 采用口径）的 c=0 非法——映射
        // θ=q/c 的除法无意义（结构有效性精确判据，NFR-COR-03）。
        {kDtRatioZero,
         "units/drivetrain.md §6.3",
         "传动比为 0（c＝Δq_joint/Δθ_motor 口径下除法无意义）：结构有效性阻断"},

        // ---- §6.3 行 5＝§7.2 非有限矩阵 ----
        // 矩阵含 NaN/±Inf 任一元素——"非有限即拒绝"精确判据（NFR-COR-03：
        // 非有限数不得静默转 0 或默认通过）。
        {kDtMatrixNonfinite,
         "units/drivetrain.md §6.3/§7.2",
         "矩阵含 NaN/±Inf 元素：非有限即拒绝，不静默转 0"},

        // ---- §6.3 行 6＝§7.2 维度不匹配 ----
        // 行列数与适用关节/电机轴集合不一致（R2 窗口与自由轴集合重叠
        // 同码——卡 §7.1 块对角组合纪律）。
        {kDtInputDimensionMismatch,
         "units/drivetrain.md §6.3/§7.2",
         "维度不匹配（行列数与关节/电机轴集合不符；窗口与自由轴重叠同码）"},

        // ---- §6.3 行 7：轴序不一致 ----
        // 关节轴按串联序、电机轴按对应关节串联序（卡 §5.3 轴序表）——
        // 两侧顺序不一致即阻止；不允许静默重排（重排等价于改输入，必须
        // 由组装方显式完成并通过身份体现）。
        {kDtInputAxisOrderMismatch,
         "units/drivetrain.md §6.3",
         "轴序不一致：不静默重排，重排须由组装方显式完成并通过身份体现"},

        // ---- §6.3 行 8：空输入 ----
        // 关节轴表空/上游序列空——空模型没有评估意义，fail-fast；
        // 空输入不得发布完整结果（EVI/TASK-02）。
        {kDtInputEmpty,
         "units/drivetrain.md §6.3",
         "空输入（关节轴表/上游序列为空）：fail-fast，不发布完整结果"},

        // ---- §7.2 行 1：非方矩阵 ----
        // 电机轴数≠适用关节数（如差动/冗余驱动）——R2 按 MDL-21/runtime
        // 口径不支持（C 须为方阵）。
        {kDtMatrixNonsquare,
         "units/drivetrain.md §7.2",
         "非方矩阵（电机轴数≠适用关节数）：按 MDL-21/runtime 口径不支持"},

        // ---- §7.2 行 2：奇异矩阵 ----
        // det≈0/不可逆——不得以伪逆放行（M-12 红线：伪逆只在矩阵通过
        // 全部结构检查后作为已批准公式的计算表示）。
        {kDtMatrixSingular,
         "units/drivetrain.md §7.2",
         "奇异矩阵（det≈0/不可逆）：不得以伪逆放行"},

        // ---- §7.2 行 3：病态矩阵 ----
        // 条件数超限——比较型诊断（实际条件数/阈值/无量纲）；阈值来源
        // P-RT-7 对齐（裁决前设计默认 1×10⁸，卡 §7.3/P-DT-2 登记留痕；
        // 不私设第二阈值）。
        {kDtMatrixIllConditioned,
         "units/drivetrain.md §7.2",
         "病态矩阵（条件数超限——P-RT-7 对齐阈值；比较型：实际/阈值/无量纲）"},

        // ---- §7.2 行 6：时变矩阵 ----
        // 常矩阵前提破坏（C 不可含时变/工况项，modeling §8.1）——明确
        // 列为不适用，不得当作常矩阵计算。
        {kDtMatrixTimeVaryingUnsupported,
         "units/drivetrain.md §7.2",
         "时变矩阵：明确列为不适用，不得当作常矩阵计算"},

        // ---- §9.2：转子惯量非法 ----
        // J_rotor 负值/零/非有限——反射惯量 J_ref＝J_rotor/c² 的输入
        // 要求（J_rotor＞0 且有限，单位 kg·m²，电机轴系；DYN-04/AT-07
        // "反射惯量"）。
        {kDtInertiaInvalid,
         "units/drivetrain.md §9.2",
         "转子等效惯量非法（负值/零/非有限；须＞0 且有限，kg·m² 电机轴系）"},

        // ---- §9.3：反射惯量矩阵非正定 ----
        // R2 关节侧反射惯量矩阵 J_ref＝(C⁻¹)ᵀ·diag(J_rotor)·(C⁻¹) 的
        // Cholesky 正定性判定失败——属输入/矩阵非法，阻止并诊断（构造
        // 保证对称；J_rotor≻0、C 可逆时理论上恒正定，失败即输入面非法）。
        {kDtInertiaNotPositiveDefinite,
         "units/drivetrain.md §9.3",
         "反射惯量矩阵 Cholesky 正定判定失败：输入/矩阵非法，阻止并诊断"},

        // ---- §10.1：效率值非法 ----
        // η⁺/η⁻ 合法域 (0,1]，越界即本码——比较型（实际值/期望范围/
        // 无量纲，ERR-01/AT-27 单位进入诊断比较字段）；效率**缺失**不走
        // 本码（走 DataInsufficient 降级素材——卡 §10.7，不伪造数值、
        // 不以 η＝1 静默替代）。
        {kDtEfficiencyInvalid,
         "units/drivetrain.md §10.1",
         "效率值非法（η≤0 或 η＞1；比较型：实际/期望范围/无量纲）；缺失另走数据不足降级"},

        // ---- §6.2/§10.7：转子惯量缺失 ----
        // 含转子项力矩不可得——力矩按理想口径（τ_m＝Cᵀτ_j）输出＋限定
        // 标记＋本码诊断；不伪造数值（M-12 全量口径的降级路径）。
        {kDtRotorMissing,
         "units/drivetrain.md §6.2/§10.7",
         "转子惯量缺失：力矩按理想口径输出＋限定标记，不伪造含转子项数值"},

        // ---- §10.4：缺样本 ----
        // 循环不完整（缺样本）——该统计标 DataInsufficient 素材＋本码；
        // 不输出"部分 RMS 冒充完整循环 RMS"（DYN-03 口径的诚实边界）。
        {kDtInputSampleMissing,
         "units/drivetrain.md §10.4",
         "缺样本（循环不完整）：统计标数据不足素材，不以部分 RMS 冒充完整循环"},

        // ---- §10.4/§12.1：时间非单调 ----
        // 上游序列时间戳必须严格单调递增（等长数组＋逐时间戳对齐——
        // 本卡不重采样、不插值）；非单调即阻止，不排序吞错。
        {kDtInputTimeNonmonotonic,
         "units/drivetrain.md §10.4/§12.1",
         "时间戳非严格单调递增：阻止，不排序吞错、不重采样插值"},

        // ---- §12.1：序列长度不一致 ----
        // 逐时间戳对齐的等长数组契约破坏（q/q̇/q̈/τ/P 各数组长度不一致）
        // ——上游序列即对齐基准，不做补齐外推。
        {kDtSeriesLengthMismatch,
         "units/drivetrain.md §12.1",
         "输入序列长度不一致（等长数组契约破坏）：不补齐、不外推"},
    };
}

}  // namespace sdurws::ird::drivetrain
