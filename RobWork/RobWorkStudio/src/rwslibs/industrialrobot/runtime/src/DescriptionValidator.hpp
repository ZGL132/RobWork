/**
 * @file   DescriptionValidator.hpp
 * @brief  Description 结构与单位校验器（编译链 S3 段）——私有实现接口。
 *
 * 设计依据：
 *   - units/runtime.md §5.2 S3 行（"结构与单位校验"段原文：链断裂/数量
 *     失配→StructureInvalid；零轴/非有限/反射旋转/qmin≥qmax/m≤0/非 SPD
 *     →InputInvalid 逐条定位对象与字段；耦合矩阵奇异/病态→InputInvalid
 *     ＋比较型诊断 M-6/M-12）、§4.4（单位与数值纪律——非有限拒绝
 *     NFR-COR-03；量级抽样警告不阻断）、§4.2（RobotDesignDescription
 *     字段契约）
 *   - 需求 MDL-21（耦合矩阵病态/奇异阻止——M-6/M-12 比较型诊断）、
 *     MDL-06（断言分域：建模侧断言、编译器复核已提供值合法性）、
 *     NFR-COR-03（非有限/非法单位不静默转 0）、NFR-COR-02（确定性）
 *   - 任务契约 tasks/foundation/RT-T03.json（产物 2：结构与单位校验器
 *     含耦合矩阵校验；acceptance 1～3 的被测主体）
 *
 * ★ 为什么是私有头（src/ 而非 include/）：§3.1 模块清单把"校验器"列入
 *   src 非模板实现（公共头 12 模块之外）——S3 是编译链内部段，其问题
 *   报告类型（ValidationReport 等）不对外承诺；RT-T11 编译器经本头消费
 *   （单元内私有头，R-2 只禁跨单元私有头），并把 ValidationIssue 转译为
 *   CompileOutcome 的 core::DiagnosticRecord（码值登记归 diagnostics）。
 *
 * 线程安全：全部纯函数/纯值——无共享可变状态，可重入（§5.5 同款约束）。
 * 确定性：同一输入在同一二进制内产生逐项一致的问题清单（迭代次序固定、
 * 阈值为编译期常量；NFR-COR-02）。不抛异常（预条件：输入为合法构造的
 * Description 值；向量访问全部经下标界限检查）。
 */

#ifndef SDURWS_IRD_RUNTIME_DESCRIPTION_VALIDATOR_HPP
#define SDURWS_IRD_RUNTIME_DESCRIPTION_VALIDATOR_HPP

#include <optional>
#include <string>
#include <vector>

#include <sdurws/ird/runtime/Description.hpp>
#include <sdurws/ird/runtime/Errors.hpp>

namespace sdurws::ird::runtime {

/**
 * @brief 问题级别（§4.4 两级纪律：硬校验拒绝 vs 量级抽样警告）。
 *
 * Error   硬校验失败——编译必须阻止（StructureInvalid/InputInvalid）；
 * Warning 警告级——不阻断（§4.4：量级抽样"给警告——不阻断"；如 rad 位
 *         置上出现 deg 量级的限位值）。警告进入诊断清单供用户复核。
 */
enum class ValidationSeverity {
    Error,   ///< 阻断级（校验失败——report.ok() 为 false）
    Warning, ///< 警告级（不阻断——§4.4 抽样纪律）
};

/**
 * @brief 比较型三要素的校验器侧承载（M-6/M-12——实际值/阈值/量名）。
 *
 * 语义对齐 core::ComparativeFields（UX-03 三要素），但用裸 double 而非
 * SourcedValue——本结构是 S3 内部产物，转译为 DiagnosticRecord 时由
 * RT-T11 按诊断码语境映射（条件数等无量纲量的单位 token 选择归该转译点）。
 * 线程安全：纯值。
 */
struct ValidationComparison {
    double actual;       ///< 实际值（如实测条件数；无量纲/字段单位见 quantity）
    double threshold;    ///< 阈值/期望值（如 1×10⁸——P-RT-7 设计默认）
    std::string quantity; ///< 量名标识（如 "coupling-condition-number"——转译
                          ///<  为诊断文案的键，稳定 token 风格）
};

/**
 * @brief 单条校验问题（定位到字段路径＋稳定错误码＋中文细节）。
 *
 * fieldPath 语义：以 Description 字段树为根的定位串，如 "joints[0].axis"、
 * "links[1].mass"、"drivetrain.coupling.C"——数组下标 0 基、链序。
 * detail 约定：以 fieldPath 开头（acceptance 2"消息含字段路径"的字面
 * 承载），后接中文原因与实测值。
 * 线程安全：纯值。
 */
struct ValidationIssue {
    ValidationSeverity severity;   ///< 级别（Error 阻断 / Warning 不阻断）
    RuntimeErrorCode code;         ///< 稳定错误码（StructureInvalid/InputInvalid/
                                   ///<  UnitMismatch——S3 归属码面，§3.4/§5.2）
    std::string fieldPath;         ///< 字段定位路径（见结构注释）
    std::string detail;            ///< 中文细节（含字段路径与实测值——review
                                   ///<  者不看设计文档也能定位问题）
    std::optional<ValidationComparison> comparison; ///< 比较型三要素
                                   ///<  （比较型诊断必带——M-6/M-12/正交性偏差）
};

/**
 * @brief 一次校验的问题清单（§5.2 S3 输出"校验通过或诊断集"）。
 *
 * 全量收集（不短路）：一次校验列出全部可判问题——与 §5.2 表头"诊断
 * 全量返回"精神一致（ERR-01 同源），修一处重编译不至于逐个撞出下一处。
 * ok() ＝ 不含 Error 级问题（Warning 不阻断——§4.4）。
 * 线程安全：纯值。
 */
struct ValidationReport {
    std::vector<ValidationIssue> issues; ///< 全部问题（收集顺序＝检查顺序，确定性）
    /// 是否通过硬校验（无 Error 级问题——Warning 不算失败）。
    bool ok() const noexcept;
};

/**
 * @brief 对 Description 执行 S3 结构与单位校验（含耦合矩阵校验）。
 *
 * 覆盖面（§5.2 S3 行原文的 Description 侧切面）：
 *   - 结构（StructureInvalid）：链空/连杆数量失配/逐关节向量失配/耦合
 *     适用范围越界；
 *   - 数值与变换（InputInvalid）：空名与非法名字符、零轴/非有限、旋转
 *     非正交（容差 1×10⁻¹²）与反射（det≤0）、qmin≥qmax、m≤0、惯量非
 *     对称/非正定、负限速、Custom 缺 EAA、传动比/摩擦非正、耦合矩阵
 *     非方阵/维度失配/奇异/病态（条件数 >1×10⁸——P-RT-7 设计默认）；
 *   - 单位量级抽样（UnitMismatch 警告，不阻断——§4.4）：旋转限位
 *     |q|>4π×10 rad、移动限位 |q|>1×10² m（典型 deg/mm 误作 SI 的量级）。
 *
 * 不覆盖面（归属其他段，防止越权）：
 *   - 资源清单与摘要一致性→S4（RT-T11）；重复 ObjectId/引用闭包→S2/S5
 *     （Description 无 ObjectId 字段）；重复 localName 不在此拒绝（名称
 *     消歧归 S8/RT-T05——RT-NM-2 口径：消歧＋警告，非硬失败）；preset
 *     与矩阵一致性→S5 构造校验（RT-T04/RT-T06）；WorldPlacement 的
 *     T_world_base 校验→§6/RT-T06。
 *
 * @param description [in] 待校验的中性描述值（只读；本函数不修改、不抛）
 * @return 全量问题清单（ok() 判硬校验是否通过）
 *
 * 复杂度：O(N) 为主（N＝关节/连杆数）；耦合矩阵条件数为对称 Jacobi
 * 特征值迭代（见实现文件常量注释，阶数＝适用关节数，工程上 ≤ 数阶）。
 */
ValidationReport validateRobotDesignDescription(const RobotDesignDescription& description);

}  // namespace sdurws::ird::runtime

#endif  // SDURWS_IRD_RUNTIME_DESCRIPTION_VALIDATOR_HPP
