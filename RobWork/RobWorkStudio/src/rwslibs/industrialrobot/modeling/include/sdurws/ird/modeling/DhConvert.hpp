/**
 * @file   DhConvert.hpp
 * @brief  DH↔显式转换器（IDhExplicitConverter，§9.4.7）——DH 权威→显式
 *         无损展开、显式→DH 五状态判定、编译链 FK 对照等价验证，以及
 *         权威切换（SetAuthorityModeEdit 命令变体）的 prepare 域门。
 *
 * 设计依据：
 *   - units/modeling.md §7.4（DH 约定与转换公式：标准/远置约定
 *     T_{i-1,i}=Rot_z(θᵢ+qᵢ)·Trans_z(dᵢ)·Trans_x(aᵢ)·Rot_x(αᵢ)；零位对齐
 *     ——θ_offset 与 zeroOffset 显式分离；基座与工具变换不在 DH 参数内）、
 *     §7.5（显式→DH 五状态判定：两阶段互斥化——结构适用性检查先行〔解析，
 *     先于一切数值求解〕；数值求解确定性〔固定初值/固定迭代上限与收敛判据/
 *     字典序选解，NFR-COR-02〕；判定按附录 D 第 5 项逐关节逐项上界——C3）、
 *     §7.6（等价验证与权威切换：仅 Exact/ExactNonUnique 且编译链 FK 对照
 *     通过方可置 authority=StandardDH；基座—世界变换隔离 M-11；切换＝独立
 *     领域命令，prepare 内执行转换判定＋等价验证，验证失败不产生修订）、
 *     §9.4.7（IDhExplicitConverter 原文签名：dhToExplicit/explicitToDh/
 *     verifyEquivalent；@错误 DhErrorCode〔ChainEmpty|DegenerateBase〕）、
 *     §9.5（T09 行三码 MDL-DH-NOT-EXPRESSIBLE/MDL-DH-APPROXIMATE/
 *     MDL-DH-ANALYSIS-FAILED）、§3.3（公共头表 DhConvert.hpp 行——T09）、
 *     §3.4（纯函数服务：无共享可变状态、可重入、确定性 NFR-COR-02）
 *   - units/runtime.md §5.2（十段编译逐步表——buildCanonicalModel S1～S5
 *     分段只读入口；不发布快照）、§4.2（RobotDesignDescription 中性值类型）
 *   - REQUIREMENTS.md §25 附录 D 第 4 项（FK/编译链等价验证容差：位置
 *     1×10⁻⁹ m／姿态 1×10⁻⁹ rad——固定）与第 5 项（DH 转换 Exact 判定
 *     容差：轴线方向角偏差 ≤1×10⁻⁹ rad 且原点位置偏差 ≤1×10⁻⁹ m，
 *     逐关节逐项上界——C3，固定）；MDL-10（五状态完整定义）、MDL-02
 *     （双权威互斥与切换条件）、AT-16（V-11 承载）
 *   - 任务契约 tasks/foundation/WP-13-T09.json acceptance 1～5
 *
 * 背景说明（第一读者须知——三个方法各自动什么）：
 *   1. dhToExplicit＝权威展开（MDL-10"DH→通用关节必须无损"，C-5 无条件
 *      允许）：把 DH 参数链按 §7.4 公式逐级累乘展开为显式关节（origin=
 *      T_parent_joint 相对变换、axis=T_{0,i}·(0,0,1) 归一化）——§7.1 变换
 *      方向图左侧"展开"边；
 *   2. explicitToDh＝受控求解（§7.5 五状态）：显式权威关节链→结构适用性
 *      检查（终判）→确定性非线性最小二乘→逐关节逐项上界判定→Exact/
 *      ExactNonUnique/Approximate/AnalysisFailed 四态——§7.1 右侧"求解"边；
 *   3. verifyEquivalent＝等价验证（§7.6）：对两套权威参数化（同链）各构
 *      Description，经调用方注入的 CompileProbe 走 runtime 编译链 S1～S5
 *      只读分段入口（不发布快照、不产生修订——PA-1 写路径唯一归 project），
 *      取零位构型 FK 对照（附录 D 第 4 项）。
 *
 * ★ 本转换器不执行权威切换本身：Exact/ExactNonUnique 须再经等价验证，且
 *   切换动作是独立领域命令（apply-robot-design 的权威切换变体，prepare 内
 *   执行判定＋验证）——域门入口为 prepareAuthoritySwitch()（本头尾段），
 *   由 CommandHandlers 的 prepare 公共段消费（T08 处理器协作面）。
 *
 * 容差纪律（§2.4 高危信息——不私设阈值）：一切工程判定阈值唯一来自
 * REQUIREMENTS 附录 D（第 4/5 项，固定类）；本文件的两个公开常量即其
 * 唯一书写点。求解器内部的过程常数（数值微分步长/迭代上限/阻尼界限/
 * 线性求解主元容差）是数值**程序**参数而非工程判定阈值——它们不影响
 * "过/不过"的判定结论（判定只用附录 D 阈值），只影响求解的确定性与
 * 精度，来源登记于各常量注。
 *
 * 确定性（NFR-COR-01/02）：全部函数为纯函数——固定初值（单位参数）、
 * 固定迭代上限与收敛判据、字典序选解；不读时钟/环境变量/locale/文件
 * 系统；同输入字节→同输出字节（含诊断记录与浮点结果的逐位一致）。
 * 线程安全：无共享可变状态、可重入（§3.4 总约定 1）。
 */

#ifndef IRD_MODELING_DHCONVERT_HPP
#define IRD_MODELING_DHCONVERT_HPP

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <rw/math/Transform3D.hpp>   // rw::math::Transform3D<double>——基座混入拒绝面的承载类型
#include <rw/math/Vector3D.hpp>      // rw::math::Vector3D<double>——轴线（单位向量）

#include <sdurws/ird/core/DiagData.hpp>     // DiagnosticRecord（诊断输出——§9.5 T09 行三码）
#include <sdurws/ird/core/Identity.hpp>     // ObjectId（关节稳定身份——展开产物回填）
#include <sdurws/ird/modeling/Errors.hpp>   // ModelingError/ModelingErrorCode（域错误值面——切换域门）
#include <sdurws/ird/modeling/RobotDesign.hpp>  // JointEntry/DhParameters/JointType/AuthorityMode（值模型）
#include <sdurws/ird/modeling/Template.hpp>  // ModelingWorkingSet（verifyEquivalent/切换域门的输入视图）
#include <sdurws/ird/runtime/CanonicalModel.hpp>  // runtime::CanonicalModel（CompileProbe 产物——只读消费）
#include <sdurws/ird/runtime/Description.hpp>     // runtime::RobotDesignDescription（等价验证的构造产物）
#include <sdurws/ird/runtime/Errors.hpp>          // runtime::Expected/RuntimeError（探针两态轨）

namespace sdurws::ird::modeling {

// =====================================================================
// 附录 D 容差常量（固定类——唯一书写点；修改走需求变更，REQUIREMENTS §25）
// =====================================================================

/// 附录 D 第 5 项（C3）：DH Exact 判定——轴线方向角偏差逐关节上界，
/// 单位 rad（与第 4 项同尺度；逐关节逐项判定，禁用总和阈值防误差互相掩盖）。
inline constexpr double kDhExactAxisAngleToleranceRad = 1e-9;

/// 附录 D 第 5 项（C3）：DH Exact 判定——原点位置偏差逐关节上界，单位 m。
inline constexpr double kDhExactOriginPositionToleranceM = 1e-9;

/// 附录 D 第 4 项：FK/编译链等价验证——位置容差，单位 m（AT-16）。
inline constexpr double kFkEquivalencePositionToleranceM = 1e-9;

/// 附录 D 第 4 项：FK/编译链等价验证——姿态容差，单位 rad（AT-16）。
inline constexpr double kFkEquivalenceOrientationToleranceRad = 1e-9;

// =====================================================================
// DhErrorCode——DH 转换接口局部错误码（§9.4.7 @错误 行；EstimateErrorCode
// 同款"接口局部载体，不进域级错误轨道"先例）
// =====================================================================

/**
 * @brief DH 转换接口局部错误码（§9.4.7"@错误 DhErrorCode（ChainEmpty|
 *        DegenerateBase——基座变换混入即拒绝）"的枚举承载）。
 *
 * 与五状态判定结果的边界：DhErrorCode 是**输入契约违约**（调用方传入的
 * 链本身不可转换——空链/基座变换混入），走值面错误；五状态（Exact～
 * AnalysisFailed）是**工程判定结论**（输入合法前提下的求解结果），走
 * DhConversionResult。两者互斥——展开失败时不产出判定，判定产出时不
 * 存在展开错误。
 *
 * 枚举顺序＝§9.4.7 行文序；表尾追加纪律同 ModelingErrorCode（接口局部
 * 契约面，数值不重排）。
 */
enum class DhErrorCode {
    /// "ChainEmpty"——链为空（零关节）：空模型没有转换意义（§9.4.7 行文；
    /// 与"空链→NotExpressible"的判定语义分属两轨——本码是输入契约违约）。
    ChainEmpty,
    /// "DegenerateBase"——基座—世界变换混入（§7.6 隔离声明：转换只作用于
    /// 关节链；R_world_base 由 runtime 从 BasePlacement 编译产生，modeling
    /// 在任何转换路径中不得计算/缓存/二次叠加基座—世界旋转——M-11）。
    DegenerateBase,
};

/**
 * @brief 取接口局部错误码的稳定 token（枚举成员名原文串——UT 判别与
 *        日志承载；switch 全枚举无 default，新增值漏登记时编译器告警）。
 * @param code [in] 错误码（全表值均有 token）
 * @return 静态存储期串。纯函数；线程安全；确定性。
 */
std::string_view dhErrorCodeToken(DhErrorCode code) noexcept;

// =====================================================================
// DhChain——DH 参数链（dhToExplicit 的输入；§7.4 参数逐关节四元组）
// =====================================================================

/**
 * @brief DH 链的单关节条目（§7.4"参数（逐关节 i）"的四元组＋零位与身份）。
 *
 * 单位与分离纪律：dh.thetaOffset/alpha 为 rad、d/a 为 m（DhParameters 注）；
 * zeroOffset 为 rad（转动关节）——θ_offset 与 zeroOffset 显式分离（§7.4
 * 零位对齐原文）：θ_offset 是几何参数、zeroOffset 是权威零位语义
 * （q_authoritative = q_zeroOffset + q_rw，runtime 口径），展开时两者都
 * 进入几何（origin 含 Rot_z(θ_offset＋zeroOffset)——q=0 零位对齐的落点），
 * 但字段保持分离、zeroOffset 原样传入展开产物（权威零位不随权威模式切换，
 * §7.2 参数来源表"type/zeroOffset 两态均权威"行）。
 */
struct DhChainJoint {
    DhParameters dh;          ///< DH 四元组（θ 偏置 rad／d m／a m／α rad）
    double zeroOffset = 0.0;  ///< 零位偏置，单位 rad（权威零位语义——显式分离纪律见结构注）
    JointType type = JointType::Revolute;  ///< 关节类型（DH 链只参数化旋转关节——
                                           ///<  Revolute|Continuous 合法；Prismatic|Fixed
                                           ///<  ＝调用方契约违约 fail-fast）
    core::ObjectId objectId;  ///< 关节稳定身份（展开产物回填——ARC-04 跨修订稳定）
    std::string localName;    ///< 关节局部名（展开产物回填——运行时名称源）

    // ---- 两态均权威字段的透传位（§7.2 参数来源表"type/zeroOffset/bounds/
    // workingRange 两态均权威"行——不入 DH 几何、原样透传，使展开产物即
    // 完整 JointEntry；调用方免二次合并）----
    core::SourcedValue<JointLimits> bounds;        ///< 限位 {qmin,qmax}（rad；透传）
    core::SourcedValue<JointLimits> workingRange;  ///< 工程工作范围（rad；仅 Continuous；透传）
};

/**
 * @brief DH 参数链（dhToExplicit 输入——只参数化关节链，§7.4"基座与工具
 *        变换不在 DH 参数内"）。
 *
 * ★ mixedInBaseTransform 是**拒绝面**而非可用输入：标准 DH 只参数化关节
 * 链，基座安装走 BasePlacement（runtime 编译产生 R_world_base，§7.6）。
 * 调用方若把基座/世界变换混入链记录（如导入路径把 T_world_base 塞进
 * 本字段），dhToExplicit 立即以 DegenerateBase 拒绝——M-11 隔离声明的
 * 可执行形态（UT：基座变换混入→DegenerateBase 拒绝）。合法调用恒为
 * nullopt。
 */
struct DhChain {
    /// 关节链（链序＝基座→法兰串联序；dhToExplicit 前置：非空——空链＝
    /// 调用方契约违约 ChainEmpty）。
    std::vector<DhChainJoint> joints;

    /// 基座—世界变换混入检测位（见结构注——非 nullopt 即 DegenerateBase）。
    std::optional<rw::math::Transform3D<double>> mixedInBaseTransform;
};

// =====================================================================
// ExpandOutcome——DH→显式展开结果（§9.4.7 dhToExplicit 返回值）
// =====================================================================

/**
 * @brief DH 权威→显式展开结果（两态：ok＝显式关节全集；err＝接口局部
 *        错误码＋已产出诊断）。
 *
 * ok 态后置（§9.4.7 @post 原文）：输出 axis 单位向量（归一化）、origin=
 * T_parent_joint（关节系相对父连杆系，core.md §4.6 读法）；逐关节
 * zeroOffset/type/身份/名称原样透传。轴/原点来源标记＝DerivedReadOnly＋
 * methodTag "dh-to-explicit"（派生来源标记——与 GeometricEstimate"估算
 * 仅为来源标记、同为权威值"同纪律；C-5 展开后调用方置新权威
 * authority=Explicit，axis/origin 变为权威）。
 * err 态：joints 为空、errorCode 有义；诊断经输出参数携带（链定位）。
 *
 * 线程安全：纯值类型。
 */
struct ExpandOutcome {
    bool ok = false;                 ///< true＝展开成功（joints 有效）
    DhErrorCode errorCode = DhErrorCode::ChainEmpty;  ///< err 态的错误码（ok 态无意义）
    std::vector<JointEntry> joints;  ///< ok 态：显式关节（链序；满足 §9.4.7 @post）
};

// =====================================================================
// 五状态判定（§7.5；MDL-10——两阶段互斥化的结果面）
// =====================================================================

/**
 * @brief 显式→DH 五状态判定结论（MDL-10 五态互斥；§7.5 判定树原文）。
 *
 * 枚举顺序＝§7.5 行文序（Exact|ExactNonUnique|Approximate|NotExpressible|
 * AnalysisFailed）；持久化契约面纪律：只允许表尾追加并走单元卡增量修订。
 * 语义边界（MDL-10 原文）：仅 Exact/ExactNonUnique 经等价验证后方可置
 * 权威（§7.6）；Approximate 不得成为权威 DH（C-4/DTB 禁止项）；Analysis
 * Failed 不构成语义结论（另报诊断——求解器数值失败是执行轴不是工程轴）；
 * NotExpressible 是终判（第一阶结构检查产出，不进入求解）。
 */
enum class DhDetermination {
    Exact,            ///< 精确可表达且解唯一（去重容差内无可辨识第二解）
    ExactNonUnique,   ///< 精确可表达但解不唯一（退化族——报告解集＋字典序选定其一）
    Approximate,      ///< 收敛但任一逐项偏差超附录 D 第 5 项容差（附 E 与收敛态）
    NotExpressible,   ///< 结构适用性检查不满足（终判，不进入求解——第一阶）
    AnalysisFailed,   ///< 求解器数值失败（不收敛/发散/资源异常——不构成语义结论）
};

/**
 * @brief 取判定结论的稳定 token（枚举成员名原文串；switch 全枚举）。
 * @param determination [in] 判定结论
 * @return 静态存储期串。纯函数；线程安全；确定性。
 */
std::string_view dhDeterminationToken(DhDetermination determination) noexcept;

/**
 * @brief 求解器收敛状态（Approximate 须随诊断报告——§7.5"须报告达到的
 *        误差值与收敛状态"）。
 */
enum class DhConvergenceState {
    Converged,  ///< 已收敛（达到稳定极小点——固定判据；Exact 判定的前提）
    Diverged,   ///< 数值发散/失败（NaN/Inf/线性求解失败——AnalysisFailed 伴随态）
};

/**
 * @brief 取收敛状态的稳定 token（"Converged"/"Diverged"）。纯函数；确定性。
 */
std::string_view dhConvergenceToken(DhConvergenceState state) noexcept;

/**
 * @brief 单关节逐项偏差明细（附录 D 第 5 项 C3 的数据承载——诊断层的
 *        比较型三要素由此组装：actual/expected/单位随产码点写入记录）。
 *
 * "逐关节逐项"＝每关节两条（轴线方向角偏差 rad＋原点位置偏差 m），
 * 全部独立判定、任一超差即该关节不满足 Exact 上界（不用总和阈值——C3
 * 明文"防多关节误差互相掩盖"）。
 */
struct DhJointDeviation {
    std::size_t jointIndex = 0;        ///< 关节链序下标（0 基）
    double axisAngleDeviation = 0.0;   ///< 轴线方向角偏差，单位 rad（重建 vs 显式权威轴）
    double originPositionDeviation = 0.0;  ///< 原点位置偏差，单位 m（欧氏范数）

    bool operator==(const DhJointDeviation& o) const noexcept
    {
        return jointIndex == o.jointIndex
            && axisAngleDeviation == o.axisAngleDeviation
            && originPositionDeviation == o.originPositionDeviation;
    }
    bool operator!=(const DhJointDeviation& o) const noexcept { return !(*this == o); }
};

/**
 * @brief 显式→DH 五状态判定结果（§9.4.7 explicitToDh 返回值）。
 *
 * 字段与判定态的对应（互斥纪律——MDL-10 五态互斥的数据面投影）：
 *   - Exact/ExactNonUnique：parameters＝选定解（ExactNonUnique＝字典序
 *     规则定值后的稳定选解）；solutionSet＝解集报告（ExactNonUnique 非空：
 *     首解＋各自由坐标发现序的约束解证人集——连续解集族的有限证人承载，
 *     禁止随机挑选）；freeCoordinates＝自由参数坐标（ExactNonUnique 非空，
 *     字典序）；convergence＝Converged；deviations＝逐关节逐项偏差（全部
 *     在附录 D 第 5 项容差内）。
 *   - Approximate：parameters＝达到的最小误差近似解（不得成为权威——C4）；
 *     errorMetricE＝E＝Σᵢ(轴线角偏差[rad]＋原点位置偏差[m])——**仅为呈现
 *     指标、不参与判定**（MDL-10/C3 原文）；convergence＝Converged（近似
 *     判定的前提是收敛——数值失败走 AnalysisFailed 而非 Approximate）；
 *     deviations 至少一项超容差。
 *   - NotExpressible：parameters/solutionSet 为空（终判不进入求解——第一
 *     阶解析检查产出）；errorMetricE＝0；convergence 无义（Converged 占位）。
 *   - AnalysisFailed：全部参数字段为空（不构成语义结论——不产出任何
 *     可用参数）；convergence＝Diverged。
 *
 * 线程安全：纯值类型。
 */
struct DhConversionResult {
    DhDetermination determination = DhDetermination::NotExpressible;  ///< 五态判定结论
    /// 选定解（逐关节参数，链序；Exact/ExactNonUnique/Approximate 有值）。
    std::vector<DhParameters> parameters;
    /// 解集报告（ExactNonUnique：首解＋约束重解证人集；其余态为空）。
    std::vector<std::vector<DhParameters>> solutionSet;
    /// 自由参数坐标（ExactNonUnique；坐标＝4×关节序＋{0:θ,1:d,2:a,3:α}
    /// 字典序——自由参数按字典序规则定中性值 0，§7.5"字典序规则定值"）。
    std::vector<std::size_t> freeCoordinates;
    /// 误差度量 E＝Σᵢ(轴线角偏差[rad]＋原点位置偏差[m])——仅 Approximate
    /// 的呈现指标（不参与判定，MDL-10）；其余态为 0。
    double errorMetricE = 0.0;
    DhConvergenceState convergence = DhConvergenceState::Converged;  ///< 收敛状态
    /// 逐关节逐项偏差明细（C3——诊断携带的比较型三要素数据源）。
    std::vector<DhJointDeviation> deviations;

    bool operator==(const DhConversionResult& o) const;
    bool operator!=(const DhConversionResult& o) const { return !(*this == o); }
};

// =====================================================================
// CompileProbe——编译分段入口注入面（§7.6/§9.4.7：由调用方注入）
// =====================================================================

/**
 * @brief 编译分段探针（§9.4.7 verifyEquivalent 第三参——"编译分段入口由
 *        调用方注入（只读、不发布快照，§7.6）"的抽象）。
 *
 * 职责（单一）：把一份 RobotDesignDescription 送入 runtime 编译链
 * buildCanonicalModel 分段入口（S1～S5——units/runtime.md §5.2），返回
 * 不可变规范模型或错误。语义红线（实现方必须遵守）：
 *   - 只读：不修改项目历史、不产生修订（编译输入全部经只读注入接口）；
 *   - 不发布快照：S1～S5 产物止于 CanonicalModel 值（S10 发布点不可达
 *     ——§5.2"唯一对外发布点"在整体事务入口）；
 *   - 确定性：同 Description→同模型（ARC-03；探针内装配的
 *     IRobotDesignReader 必须原样返回注入的 Description——不得二次加工）。
 *
 * 生产装配（L5）：以 runtime::CanonicalModelCompiler 的 buildCanonicalModel
 * 实现探针（CompileRequest 的 reader 槽装配"恒返回该 Description"的
 * IRobotDesignReader 适配器；objects/closure 槽以该 Description 的对象
 * 身份装配最小闭包）。测试装配：同构替身（DhConvertEquivalenceTest 以
 * 真实产品编译器装配探针——端到端 S1～S5 消费面自证）。
 *
 * 所有权：非 owning 接口——调用方持有实现，verifyEquivalent 只用其
 * const 接口（调用期存活保证由调用方负责）。线程安全：实现方须保证
 * const buildCanonicalModel 并发只读安全（§5.5 注入接口约定）。
 */
class CompileProbe {
public:
    virtual ~CompileProbe() = default;

    /**
     * @brief 对一份 Description 执行 S1～S5 分段编译（只读、不发布快照）。
     *
     * @param description [in] 中性描述值（等价验证侧构造；只读——函数
     *                    期间不得被外部修改）
     * @return ok＝不可变规范模型（S5 产物）；err＝分段失败（RuntimeError
     *         携 §5.2 S1～S5 归属码——调用方转译为等价验证失败报告）
     *
     * @throws 实现方可按 runtime §3.4 对调用方契约违约 fail-fast（正常
     *         失败面全部经返回值表达）
     */
    virtual runtime::Expected<runtime::CanonicalModel, runtime::RuntimeError>
        buildCanonicalModel(const runtime::RobotDesignDescription& description) const = 0;
};

// =====================================================================
// EquivalenceReport——等价验证报告（§9.4.7 verifyEquivalent 返回值）
// =====================================================================

/**
 * @brief 编译链 FK 对照等价验证报告（附录 D 第 4 项判定＋逐关节明细）。
 *
 * 字段语义：
 *   - inputsValid＝false：输入工作集不满足前置（权威态与可用数据不匹配，
 *     如 Explicit 态 axis/origin 缺失）——其余字段无义（未进入编译对照）；
 *   - compileOk＝false：探针分段编译失败（至少一侧）——failureDetail 携
 *     RuntimeError 定位文本；模型间未对照；
 *   - equivalent＝逐关节 FK 对照（位置 ≤附录 D 第 4 项位置容差 **且**
 *     姿态 ≤姿态容差）的全称结论；jointDeviations＝逐关节位置/姿态偏差
 *     （比较型三要素数据源）；maxPositionDeviation/maxOrientationDeviation
 *     ＝全链最大偏差（m/rad——报告与诊断的呈现值）。
 *
 * 线程安全：纯值类型。
 */
struct EquivalenceReport {
    bool inputsValid = false;   ///< 输入前置满足（两侧权威态与可用数据匹配）
    bool compileOk = false;     ///< 两侧探针分段编译均成功
    bool equivalent = false;    ///< FK 对照结论（compileOk 前提下有效）
    /// 全链最大位置偏差，单位 m（compileOk 前提下有效；inputsValid/compileOk
    /// 失败时为 0 占位——未对照不伪造数值，NFR-COR-03）。
    double maxPositionDeviation = 0.0;
    /// 全链最大姿态偏差，单位 rad（同上）。
    double maxOrientationDeviation = 0.0;
    /// 逐关节偏差明细（下标对齐链序；jointIndex 复用 DhJointDeviation 承载
    /// ——axisAngleDeviation 字段名在本报告中语义为"姿态偏差 rad"）。
    std::vector<DhJointDeviation> jointDeviations;
    /// 失败定位文本（inputsValid=false 的前置违例说明／compileOk=false 的
    /// RuntimeError detail；成功为空）。
    std::string failureDetail;

    bool operator==(const EquivalenceReport& o) const;
    bool operator!=(const EquivalenceReport& o) const { return !(*this == o); }
};

// =====================================================================
// IDhExplicitConverter——DH↔显式转换器（§9.4.7 原文抽象；纯函数服务）
// =====================================================================

/**
 * @brief DH↔显式转换器抽象（§9.4.7 原文契约——"纯函数；五状态判定
 *        （§7.5）；容差唯一来自附录 D 第 4/5 项（不私设）"）。
 *
 * 三个方法对应 §7.1 变换方向图的三条边：dhToExplicit＝展开（无损，允许）；
 * explicitToDh＝求解（五状态，受控）；verifyEquivalent＝FK 对照（切换的
 * 必要条件）。本接口不执行权威切换——切换是独立领域命令（§7.6），域门
 * 见 prepareAuthoritySwitch()。
 *
 * 线程安全：全部方法 const＋无共享可变状态——并发只读可重入（§3.4 总
 * 约定 1）。确定性：同输入同输出（含诊断记录序与浮点逐位——NFR-COR-02）。
 */
class IDhExplicitConverter {
public:
    virtual ~IDhExplicitConverter() = default;

    /**
     * @brief DH 权威→显式表示（无损展开；§7.4——MDL-10"DH→通用关节必须
     *        无损"、C-5 无条件允许）。
     *
     * 算法（§7.4 原文逐步）：
     *   步① 输入契约检查：链空→ChainEmpty；mixedInBaseTransform 非空→
     *       DegenerateBase（基座—世界隔离，M-11）；
     *   步② 逐级累乘：T_{0,i} = Π_{k≤i} [Rot_z(θ_k＋zeroOffset_k)·
     *       Trans_z(d_k)·Trans_x(a_k)·Rot_x(α_k)]——Rot_z 内的
     *       (θ_offset＋zeroOffset) 即"q=0 零位对齐"的落点（q_rw=0 时
     *       显式位姿＝DH 派生位姿——权威零位旋转已入几何，zeroOffset
     *       字段原样透传保持分离）；
     *   步③ 构造产物：origin_i = T_parent_joint（当前步的相对变换——
     *       Rot_z(θᵢ＋q0ᵢ)·Trans_z(dᵢ)·Trans_x(aᵢ)·Rot_x(αᵢ)，core.md
     *       §4.6"joint 系相对 parent 系"读法）；axis_i = 归一化
     *       (T_{0,i}·(0,0,1))——§7.4"轴线 z_i"原文；单位向量后置
     *       （§9.4.7 @post）。
     *
     * 数值纪律：数值误差仅浮点累乘（§7.4——实测远优于第 4 项容差）；
     * 展开不引入迭代、不读环境。
     *
     * @param chain  [in] DH 参数链（只读；前置见步①——违者走错误态，
     *               不抛、不产出半成品）
     * @param diags  [out] 诊断输出（追加不清空）。★ 本方法两个状态都
     *               **不产诊断**：步①的输入契约违约是调用方错误——其
     *               机器判别面＝DhErrorCode 值面；§9.5 无该违约的登记码，
     *               按 Errors.hpp 阶段纪律"无已登记映射码＝不得产诊断，
     *               错误经值面返回"执行（禁私定/拼码——§9.5 产码纪律）。
     *               参数为 §9.4.7 原文签名形状的契约预留位。
     * @return 展开结果（两态——见 ExpandOutcome 注）
     *
     * 纯函数；线程安全；确定性。
     */
    virtual ExpandOutcome dhToExplicit(const DhChain& chain,
                                       std::vector<core::DiagnosticRecord>& diags) const = 0;

    /**
     * @brief 显式→DH 五状态判定（§7.5 两阶段互斥化；确定性求解）。
     *
     * 第一阶·结构适用性检查（解析，先于一切数值求解——MDL-10 原文）：
     *   纯串联（向量输入的链序即串联结构）∧ 每关节单自由度旋转
     *   （Revolute；Continuous 按 MDL-12 **已确认工程工作范围**后视同
     *   旋转——未确认/非有限区间不视同；Prismatic/Fixed 不满足）∧ 轴
     *   有效（非零、有限、可归一化——I-MDL-6 同口径）。任一不满足→
     *   NotExpressible（终判，不进入求解；诊断 MDL-DH-NOT-EXPRESSIBLE，
     *   error 级，subject＝首个违规关节）。
     *
     * 第二阶·数值求解（仅结构适用链）：
     *   未知量 x＝逐关节 (θ_offset, d, a, α)（zeroOffset 固定输入不入
     *   未知量）；目标＝DH 链重建的轴线/原点 vs 显式权威轴线/原点；
     *   求解＝确定性非线性最小二乘（Levenberg-Marquardt——固定初值＝
     *   单位参数〔中性恒等：θ=d=a=α=0〕、固定数值微分步长/迭代上限/
     *   阻尼界限、不读时钟/环境——NFR-COR-02；过程常数见实现注，非
     *   工程阈值）。
     *   判定（附录 D 第 5 项逐关节逐项上界——C3）：
     *   - 全部关节轴线方向角 ≤1×10⁻⁹ rad 且原点位置 ≤1×10⁻⁹ m：
     *     唯一性检查（解处雅可比秩＝未知量数→Exact；秩亏→退化族
     *     ExactNonUnique——自由参数按字典序规则定值〔逐坐标钉中性值 0
     *     重解，收敛且可分辨即定值〕、报告解集证人、禁止随机挑选）；
     *   - 收敛但任一项超容差→Approximate（诊断 MDL-DH-APPROXIMATE，
     *     Warning 级——附 E 度量与收敛状态；不得成为权威 DH，C-4）；
     *   - 求解器数值失败（发散/NaN/Inf/线性求解失败）→AnalysisFailed
     *     （诊断 MDL-DH-ANALYSIS-FAILED，error 级——不构成语义结论）。
     *
     * @post Approximate/NotExpressible/AnalysisFailed 均不产生可置权威
     *       的参数（parameters 空——Approximate 的 parameters 是**近似
     *       参考值**，C-4 禁止切换语义下不具权威资格）；Exact/
     *       ExactNonUnique 须再经等价验证（§7.6）方可切换权威——本方法
     *       不执行切换（§9.4.7 @post 原文）。
     *
     * @param explicitJoints [in] 显式权威关节序列（链序；前置：每关节
     *                       axis/origin 为 Provided 且值合法——结构检查
     *                       违例走 NotExpressible 轨，不抛）
     * @param diags          [out] 诊断输出（追加不清空）。三码产码点：
     *                       NOT-EXPRESSIBLE（终判）/APPROXIMATE（近似，
     *                       比较型三要素＝逐关节最坏偏差）/ANALYSIS-
     *                       FAILED（数值失败）；判定明细随记录 context/
     *                       cause 携带（逐关节逐项偏差表——C3 三要素）。
     * @return 判定结果（五态——见 DhConversionResult 注）
     *
     * 纯函数；线程安全；确定性。
     */
    virtual DhConversionResult explicitToDh(
        const std::vector<JointEntry>& explicitJoints,
        std::vector<core::DiagnosticRecord>& diags) const = 0;

    /**
     * @brief 等价验证：两套权威参数化（同链）经 Description 构造＋编译链
     *        FK 对照（§7.6；附录 D 第 4 项——AT-16）。
     *
 * 流程（§7.6 原文）：对 a、b 各构 RobotDesignDescription（显式侧直映
 * axis/origin；DH 侧先经 dhToExplicit 展开——同一展开路径保证语义
 * 单一源）→经注入探针走 runtime buildCanonicalModel S1～S5 只读分段
 * 入口（不发布快照、不产生修订——§5.2）→取 RobWork 零位构型（q_rw=0）
 * FK 对照：逐关节位置偏差 ≤1×10⁻⁹ m 且姿态偏差 ≤1×10⁻⁹ rad（附录 D
 * 第 4 项，固定）→全称结论 equivalent。零位语义（§7.4"θ_offset 与
 * zeroOffset 显式分离"）在 Description 映射的零位折叠步接受对照
 * （runtime §4.2 无零位承载——显式表示折叠进原点/限位，CompilerImpl
 * S5 v0.12 登记）：两侧折叠同规，零位错位即 FK 超差。
     *
     * 范围注记（内部构造与 T12 的分工）：本方法内部的链级 Description
     * 构造（关节/连杆身份与名称/基座预设，物性 NotProvided）**仅服务等价
     * 验证**（FK 对照是运动学结论——物性不入对照面）；全量 Description
     * 构造器（工具/场景/资源/物性映射）归 T12 ICanonicalModelInputBuilder
     * ——两者非重复实现（登记单元卡 §15 增量）。
     *
     * @param a     [in] 参数化甲（切换前显式权威工作集——只读）
     * @param b     [in] 参数化乙（候选 DH 权威工作集——只读；与 a 同链：
     *              关节数一致、非几何字段同源）
     * @param probe [in] 编译分段探针（调用方注入——非 owning，调用期
     *              存活；见 CompileProbe 注）
     * @return 等价验证报告（三态面——见 EquivalenceReport 注；任何失败
     *         都不抛——值面报告）
     *
     * 纯函数；线程安全；确定性。
     */
    virtual EquivalenceReport verifyEquivalent(const ModelingWorkingSet& a,
                                               const ModelingWorkingSet& b,
                                               const CompileProbe& probe) const = 0;
};

/**
 * @brief IDhExplicitConverter 无状态产品实现（§3.4 总约定 1——Codec/
 *        TemplateFactory/Mapper 同款"接口＋final 实现"形态）。
 */
class DhExplicitConverter final : public IDhExplicitConverter {
public:
    ExpandOutcome dhToExplicit(const DhChain& chain,
                               std::vector<core::DiagnosticRecord>& diags) const override;
    DhConversionResult explicitToDh(
        const std::vector<JointEntry>& explicitJoints,
        std::vector<core::DiagnosticRecord>& diags) const override;
    EquivalenceReport verifyEquivalent(const ModelingWorkingSet& a,
                                       const ModelingWorkingSet& b,
                                       const CompileProbe& probe) const override;
};

// =====================================================================
// 权威切换域门（§7.6/C-3/C-4/C-6——apply-robot-design 权威切换变体的
 // prepare 执行体；SetAuthorityModeEdit 的命令侧协作面）
// =====================================================================

/**
 * @brief 权威切换域门的裁决结果（§7.6"prepare 内执行转换判定＋等价验证"
 *        的承载值）。
 *
 * 字段语义：
 *   - allowed＝true：四道门（前置/先决断/判定/等价验证）全过——candidate
 *     为 prepare 计算的切换候选（authority=StandardDH＋dhDerived＝判定
 *     选定解＋axis/origin 重算派生缓存），命令层据此写入；diagnostics 中
 *     无 error 级；
 *   - allowed＝false：error＝值面拒绝（ModelingError——码面见各门注）；
 *     determination/conversion/equivalence 携带已执行到哪道门的事实
 *     （未执行的门保持缺省）；diagnostics 携带 MDL-DH-* 终判/警告诊断
 *     （若有）——验证失败不产生修订（§7.6 原文）。
 *
 * 线程安全：纯值类型。
 */
struct AuthoritySwitchDecision {
    bool allowed = false;  ///< 四道门全过（candidate 有效）
    /// 值面拒绝（allowed=false 时有值；码面：C-6 先决断/权威态前置＝
    /// AuthorityViolation；显式数据缺失/等价验证失败＝DhExpandFailed——
    /// 语义扩展登记单元卡 §15 增量）。
    std::optional<ModelingError> error;
    /// 判定结论（显式→DH 五状态——NotExpressible 即 C-3 拒绝面）。
    DhDetermination determination = DhDetermination::NotExpressible;
    /// 判定明细（选定解/偏差表/E 度量/收敛态——呈现与留痕素材）。
    DhConversionResult conversion;
    /// 等价验证报告（第四道门的产出；未执行时 inputsValid=false 占位）。
    EquivalenceReport equivalence;
    /// 切换候选工作集（allowed 时有效——命令层写入面）。
    ModelingWorkingSet candidate;
};

/**
 * @brief 权威切换域门（§7.6——SetAuthorityModeEdit 命令变体的 prepare
 *        执行体：转换判定＋等价验证在 prepare 内执行，验证失败不产生
 *        修订）。
 *
 * 四道门（按序短路；任一拒绝即返回，后续门不执行）：
 *   门① 前置：baseline 必须处于 Explicit 权威态（已是 StandardDH＝重复
 *       切换→AuthorityViolation 值面拒绝）；baseline 与 draft 必须同链
 *       （关节数一致——不同链＝调用方契约错误，fail-fast）。
 *   门② 先决断既有编辑（C-6）：draft 相对 baseline 存在未决断编辑
 *       （changes 非空）→AuthorityViolation 拒绝——"编辑器要求先决断
 *       既有编辑（提交或撤销）再执行切换"。命令层投影：切换为独立命令
 *       （摘要单独留痕），混合编辑的载荷在 CommandHandlers 门上以
 *       invalid-payload 拒绝（见 CommandHandlers.hpp 增量注）。
 *   门③ 转换判定（C-3/C-4）：对 baseline 显式链执行 explicitToDh——
 *       NotExpressible→拒绝＋终判诊断（MDL-DH-NOT-EXPRESSIBLE，error）；
 *       Approximate→拒绝（不得成为权威 DH）＋MDL-DH-APPROXIMATE
 *       （Warning，附 E 与收敛态）；AnalysisFailed→拒绝＋MDL-DH-
 *       ANALYSIS-FAILED（error，不构成语义结论）；
 *   门④ 等价验证（附录 D 第 4 项）：以判定选定解组装候选工作集
 *       （authority=StandardDH＋dhDerived＋axis/origin 重算派生缓存），
 *       verifyEquivalent（显式基线 vs DH 候选，探针注入）——不过→拒绝
 *       （值面；偏差明细在报告内）。
 *
 * @param draft     [in] 编辑态工作集（UI 侧提交视角——用于 C-6 先决断；
 *                  命令路径传基线同值——changes 为空即门②自然通过）
 * @param baseline  [in] 已应用基线工作集（Explicit 权威态；转换判定的
 *                  几何源——axis/origin 权威值）
 * @param converter [in] DH 转换器（判定与展开的唯一实现——注入；非 owning）
 * @param probe     [in] 编译分段探针（等价验证注入面——非 owning）
 * @param diags     [out] 诊断输出（追加不清空；MDL-DH-* 三码的命令侧
 *                  汇集点——产码仍在 converter 内，本函数不二次产码）
 * @return 裁决结果（见 AuthoritySwitchDecision 注）
 *
 * @throws std::invalid_argument draft 与 baseline 链长不一致（调用方契约
 *               违约——fail-fast）
 *
 * 纯函数；线程安全；确定性（四道门全为确定性判定——同输入同裁决）。
 */
AuthoritySwitchDecision prepareAuthoritySwitch(const ModelingWorkingSet& draft,
                                               const ModelingWorkingSet& baseline,
                                               const IDhExplicitConverter& converter,
                                               const CompileProbe& probe,
                                               std::vector<core::DiagnosticRecord>& diags);

}  // namespace sdurws::ird::modeling

#endif  // IRD_MODELING_DHCONVERT_HPP
