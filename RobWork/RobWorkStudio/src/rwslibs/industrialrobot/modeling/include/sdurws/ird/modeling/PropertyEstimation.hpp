/**
 * @file   PropertyEstimation.hpp
 * @brief  物性估算唯一公式表（版本 mdl-property-formula/1）——连杆段元
 *         逐段估算与合成（IPropertyEstimator）、平行轴迁移、质心修改
 *         确认流域级判定、材料密度默认表（§5.3/§9.4.6）。
 *
 * 设计依据：
 *   - units/modeling.md §5.3（物性估算唯一公式表——三段元闭式公式、合成式、
 *     规则 1~5）、§9.4.6（IPropertyEstimator 接口签名——estimateLink/
 *     migrateByParallelAxis/formulaVersion 三方法与 @pre/@post/@错误 契约）、
 *     §3.3（公共头表 PropertyEstimation.hpp 行——T04）、§4.3-B（BodyData
 *     惯量基准＝质心＋连杆系姿态，M-2）、§14.4（新增语义登记第 5 项——
 *     唯一公式表 mdl-property-formula/1 在 MDL-05 授权范围内）、§14.4 第 3 项
 *     （材料密度默认表＝设计默认值，D-MDL-7，黄金数据集锁定随 WP-13-T16）
 *   - 需求 MDL-05（几何物性估算层：实心/空心圆截面、矩形截面；惯量基准
 *     M-2；质心修改的平行轴确认——不允许质心与惯量基准静默脱钩）、
 *     MDL-16（动力学参数与限值层——来源标记；缺失标记 DataInsufficient）、
 *     DYN-06（物性/摩擦数据不足→可信等级判定与降级——**判定权归
 *     dynamics/evidence，本单元只提供来源标记与 NotProvided 事实**）
 *   - 任务契约 tasks/foundation/WP-13-T04.json acceptance 1~4
 *
 * 背景说明（估算器在产品里的位置——为什么"估算不等于证据"）：连杆物性
 * 是动力学评估（WP-17）的输入之一。用户不手填质量/惯量时，本单元按
 * 解析几何从段元（圆柱/盒）估算——结果是**几何估算值**（来源标记
 * GeometricEstimate），其可信等级判定与 DataInsufficient 降级唯一归
 * dynamics/evidence（DYN-06，卡 §5.3 规则 5"估算质量不当作动力学证据"）。
 * 本头只做三件事：①算（唯一公式表，确定性）；②标（来源标记不丢失）；
 * ③守基准（质心与惯量不静默脱钩——MDL-05 的硬性交互约束）。
 *
 * 确定性（NFR-COR-01/02，卡 §5.3 规则 4）：全部函数为纯函数——无共享
 * 可变状态、不读时钟/环境/locale；合成按输入序单遍固定运算次序，同输入
 * 逐字节同输出。全 SI（m/kg/rad/N·m）、双精度、不舍入（显示舍入归 ui）。
 * 线程安全：纯函数（§3.4 总约定 1），并发只读安全。
 */

#ifndef IRD_MODELING_PROPERTYESTIMATION_HPP
#define IRD_MODELING_PROPERTYESTIMATION_HPP

#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <rw/math/Rotation3D.hpp>   // Rotation3D<double>——段元位姿旋转半部（逐元素恒等默认构造用）
#include <rw/math/Transform3D.hpp>  // rw::math::Transform3D<double>——T_link_seg（m/rad）
#include <rw/math/Vector3D.hpp>     // rw::math::Vector3D<double>——质心/位移（m）

#include <sdurws/ird/core/DiagData.hpp>     // core::DiagnosticRecord（diags 输出参数元素类型）
#include <sdurws/ird/core/Provenance.hpp>   // core::ValueProvenance（来源标记载体）
#include <sdurws/ird/modeling/Errors.hpp>   // ModelingError/ModelingErrorCode（CentroidEditUnresolved 值面）
#include <sdurws/ird/modeling/RobotDesign.hpp>  // InertiaTensor/MaterialRef/BodyData（物性值模型）
#include <sdurws/ird/runtime/Errors.hpp>    // runtime::Expected 模板（两态结果载体——登记边 runtime）

namespace sdurws::ird::modeling {

// =====================================================================
// 唯一公式表版本（§5.3——methodTag 与 formulaVersion() 的单一来源；
// 版本演进＝公式表变更，须随单元卡增量修订换版本号，旧版本数值冻结）
// =====================================================================

/// @brief 唯一公式表版本（§5.3 标题原文串；来源标记 methodTag 用同一常量，
///        禁止第二处写字面量——拼写漂移在源头切断，NFR-COR-02）。
inline constexpr std::string_view kPropertyFormulaVersion = "mdl-property-formula/1";

// =====================================================================
// 段元（segment）输入类型（§5.3 输入节——每段 { primitive; T_link_seg;
// material }；段局部坐标系原点在段几何中心、z 轴沿段轴向）
// =====================================================================

/**
 * @brief 实心圆柱段元参数（§5.3 公式表第 1 行；尺寸单位 m，>0 且有限）。
 */
struct SolidCylinderSpec {
    double radius = 0.0;  ///< 截面半径 r，单位 m（>0 且有限——@pre）
    double length = 0.0;  ///< 轴向长度 L，单位 m（>0 且有限——@pre；沿段系 z 轴）
};

/**
 * @brief 空心圆柱段元参数（§5.3 公式表第 2 行；尺寸单位 m）。
 *
 * 约束：rOut > rIn > 0 且有限（壁厚为正是质量与惯量公式的数学前提——
 * rOut ≤ rIn 时"空心"退化为零/负质量，属调用方错误，走 IllegalDimension）。
 */
struct HollowCylinderSpec {
    double radiusOuter = 0.0;  ///< 外半径 rOut，单位 m（>0 且有限）
    double radiusInner = 0.0;  ///< 内半径 rIn，单位 m（≥0 的语义由 rOut>rIn>0 蕴含）
    double length = 0.0;       ///< 轴向长度 L，单位 m（>0 且有限；沿段系 z 轴）
};

/**
 * @brief 长方体段元参数（§5.3 公式表第 3 行；尺寸单位 m）。
 *
 * 轴向指派（§5.3 表行括注"z 沿 L"）：段系 x 沿 a、y 沿 b、z 沿 L。
 * a/b/L 全部 >0 且有限。
 */
struct BoxSpec {
    double a = 0.0;       ///< x 向边长 a，单位 m
    double b = 0.0;       ///< y 向边长 b，单位 m
    double length = 0.0;  ///< z 向边长 L，单位 m
};

/// @brief 段元图元三选一（§5.3 输入节原文三段元；variant 备择序＝公式表
///        行序——表尾追加新段元走单元卡增量修订，不重排既有备择）。
using SegmentPrimitive = std::variant<SolidCylinderSpec, HollowCylinderSpec, BoxSpec>;

/**
 * @brief 连杆段元规格（§5.3 输入节——估算的最小输入单元）。
 *
 * linkFromSegment＝T_link_seg："段坐标系相对连杆坐标系"的位姿（m/rad，
 * core.md §4.6 T_ab 约定）。段系原点在段几何中心＝段质心（均质材料的
 * 解析前提），因此段质心在连杆系下的位置即该位姿的平移部分。
 *
 * 默认构造＝恒等位姿（逐元素构造——零框架库外联符号；JointPose 同款
 * 冒烟 header-only 纪律，RobotDesign.hpp 类注）。
 */
struct SegmentSpec {
    SegmentPrimitive primitive;  ///< 段元图元（三选一——公式表行）

    /// 段系在连杆系下的位姿 T_link_seg（m/rad）。
    rw::math::Transform3D<double> linkFromSegment{
        rw::math::Vector3D<double>(0.0, 0.0, 0.0),
        rw::math::Rotation3D<double>(1.0, 0.0, 0.0,
                                     0.0, 1.0, 0.0,
                                     0.0, 0.0, 1.0)};

    MaterialRef material;  ///< 段材料（密度解析序见 estimateLink 注——先取
                           ///  density 已提供值，再查默认表）
};

// =====================================================================
// 材料密度默认表（§5.3 输入节——设计默认值；黄金数据集锁定随 WP-13-T16，
// D-MDL-7；锁定后数值不变，键词表随卡面冻结）
// =====================================================================

/**
 * @brief 查询材料密度默认表（§5.3——设计默认值，非上游冻结需求值）。
 *
 * 键词表（§14.4 第 3 项登记的设计默认值；单位 kg/m³）：
 *   - "steel"                钢        7850
 *   - "aluminum"             铝        2700
 *   - "cast-iron"            铸铁      7200
 *   - "titanium-alloy"       钛合金    4430
 *   - "engineering-plastic"  工程塑料  1200
 *
 * @param materialId [in] 材料表键（MaterialRef.materialId 同一作用域——
 *                公式表键词表；精确等值匹配，无大小写折叠/空白剥离——
 *                ARC-04"不猜测"纪律）
 * @return 命中返回密度（kg/m³）；键外（含空串/大小写变体/未知键）返回
 *         nullopt——调用方（estimateLink）按 MaterialRef.density 是否已
 *         提供决定走已提供值还是 MaterialDensityMissing 失败，本函数
 *         不自行产出诊断（PA-1：产码唯一经 IDiagnosticFactory）
 *
 * 纯函数；线程安全；确定性（NFR-COR-02：固定表线性扫描，同键同值）。
 */
std::optional<double> defaultMaterialDensity(std::string_view materialId);

// =====================================================================
// 估算结果两态（§9.4.6 EstimateOutcome 的落位形态）
// =====================================================================

/**
 * @brief 估算失败载荷（§9.4.6 @错误 行——EstimateErrorCode 专用局部错误
 *        枚举的承载）。
 *
 * ★ 为什么不并入 ModelingErrorCode 全表（Errors.hpp 头注原文）：这是本
 * 接口的局部错误载体（"两族专用错误枚举"之一），随接口落位，不进域级
 * 错误轨道。诊断码纪律：当前 §9.5 无 T04 行码（分批注册纪律——不预建），
 * 估算失败一律经本值面返回，不入诊断目录；就绪校验 Warning（物性缺失→
 * DataInsufficient 预告）的登记面归 T08（卡 §5.3 规则 3）。
 */
enum class EstimateErrorCode {
    /// "IllegalDimension"——段元尺寸非法（≤0/非有限/空心壁厚非正；@pre 违约）
    IllegalDimension,
    /// "MaterialDensityMissing"——材料密度不可解析（density 未提供且默认表未命中）
    MaterialDensityMissing,
    /// "SynthesisFailed"——合成自检失败（§9.4.6 @post：内部错误码，不输出
    ///    非法张量——物理上合法输入的合成结果恒 SPD，触发即内部实现错误）
    SynthesisFailed,
};

/**
 * @brief 估算错误码稳定 token（枚举成员名原文串；§9.4.6 @错误 行以成员名
 *        指称错误）。实现侧唯一映射点——头注释与本函数失同步时全表机械
 *        比对测试即刻暴露（modelingErrorCodeToken 同款防线）。
 *
 * @param code [in] 错误码（全表 3 值均有 token——switch 全枚举、无 default，
 *              新增枚举值漏登记时编译器告警暴露）
 * @return 静态存储期串。纯函数；线程安全；确定性。
 */
std::string_view estimateErrorCodeToken(EstimateErrorCode code) noexcept;

/**
 * @brief 估算成功载荷（§9.4.6 @post 全量面：结果张量定义在连杆合成质心、
 *        参考姿态＝连杆坐标系（M-2）；来源标记 GeometricEstimate＋methodTag）。
 */
struct EstimatedLinkProperties {
    /// 合成质量 m_link＝Σmᵢ，单位 kg（>0——自检通过的前置）。
    double massKg = 0.0;

    /// 合成质心 C＝Σmᵢcᵢ/m_link，单位 m，连杆坐标系下表示。
    rw::math::Vector3D<double> centerOfMass;

    /// 合成惯量张量 I_C，单位 kg·m²；基准＝连杆合成质心 C、参考姿态＝
    /// 连杆坐标系（M-2——卡 §5.3 合成节原文；§4.3-B body.inertia 同基准，
    /// 估算结果可直接落位 BodyData）。
    InertiaTensor inertia;

    /// 来源标记：kind=GeometricEstimate、methodTag="mdl-property-formula/1"
    /// （§5.3 规则 1——估算结果一律此标记；无 sourceObject/sourceVersion，
    /// 来源是公式表本身而非某个存储对象）。
    core::ValueProvenance provenance;
};

/**
 * @brief 估算失败载荷（EstimateOutcome 错误侧——码＋定位细节）。
 *
 * detail 面向内部诊断链/日志（段序号、字段名、原始值定位），不直接呈现
 * 给用户（敏感性约束同 ModelingError.detail——呈现前经 diagnostics 脱敏）。
 */
struct EstimateError {
    EstimateErrorCode code = EstimateErrorCode::IllegalDimension;  ///< 稳定错误码
    std::string detail;  ///< 定位细节（UTF-8；失败段序/字段/原因）
};

/**
 * @brief 估算结果两态（§9.4.6 "EstimateOutcome"的落位形态）。
 *
 * 复用 runtime::Expected 模板（登记边 runtime 的公共两态设施——成功侧
 * EstimatedLinkProperties、错误侧本接口局部 EstimateError；错误侧类型
 * 不同故不能复用 Codec.hpp 的 modeling::Expected 别名，但模板同一份，
 * 不存在"第二份两态机制"）。语义同其契约：恰持一侧；对错误侧 get()／
 * 对成功侧 error()＝调用方契约违约，抛 std::logic_error fail-fast
 * （不返回默认值静默吞错——NFR-COR-03）。
 */
using EstimateOutcome = runtime::Expected<EstimatedLinkProperties, EstimateError>;

// =====================================================================
// IPropertyEstimator——物性估算器接口（§9.4.6 签名原文）与唯一产品实现
// =====================================================================

/**
 * @brief 物性估算器接口（§9.4.6 原文契约；MDL-05）。
 *
 * 纯函数服务（§3.4 总约定 1）：无共享可变状态、可重入、多线程并发安全；
 * 确定性：同输入→同输出（§5.3 规则 4"估算算法确定性"）。
 */
class IPropertyEstimator {
public:
    virtual ~IPropertyEstimator() = default;

    /**
     * @brief 逐段估算并合成为连杆物性（质心系、连杆系姿态——M-2）。
     *
     * 算法（§5.3 唯一公式表；实现分四步，逐步注释见 PropertyEstimation.cpp）：
     *   步① 逐段解析图元尺寸并做 @pre 校验（尺寸>0 且有限；空心须 rOut>rIn>0）；
     *   步② 逐段解析密度（先 MaterialRef.density 已提供值〔须 >0 有限〕，
     *        再查默认表；两路皆不命中→MaterialDensityMissing 失败）并按
     *        公式表行算段质量与段质心系主轴惯量；
     *   步③ 合成：m_link=Σmᵢ；C=Σmᵢcᵢ/m_link；
     *        I_C=Σ[Rᵢ·Iᵢ·Rᵢᵀ+mᵢ((dᵢ·dᵢ)E−dᵢdᵢᵀ)]（平行轴定理，dᵢ=cᵢ−C）；
     *   步④ 输出自检（§9.4.6 @post）：输出全有限＋对称（相对 1×10⁻¹²）＋
     *        SPD（严格>0）＋惯性椭球三角不等式——失败返回 SynthesisFailed
     *        内部错误码，不输出非法张量。
     *
     * @param segments [in] 段元列表（非空——@pre；遍历按输入序，确定性）
     * @param diags    [out] 诊断记录输出（追加不清空）。★ 当前分批注册
     *                纪律下本实现不追加记录：§9.5 无 T04 行码，估算失败经
     *                返回值面（EstimateError）定位；物性缺失的 Warning 登记
     *                与 DataInsufficient 预告落点＝T08 就绪校验（卡 §5.3
     *                规则 3）。参数按卡面签名保留（diagnostics.md §8.8：
     *                产码唯一经 IDiagnosticFactory，禁字符串拼码）。
     * @return ok＝估算物性（来源标记 GeometricEstimate＋methodTag——§5.3
     *         规则 1）；err＝IllegalDimension|MaterialDensityMissing|
     *         SynthesisFailed（detail 携段序定位）
     *
     * @pre segments 非空；各段尺寸>0 且有限；密度>0（材料表查询失败→
     *      密度 NotProvided 报告——即 MaterialDensityMissing 失败，不猜测
     *      不回退第二材料，NFR-COR-03）
     * @post 成功时输出满足自检三条件（对称/SPD/三角不等式）；全 SI、
     *       双精度、不舍入（显示舍入归 ui——卡 §5.3 合成节原文）
     *
     * 纯函数；线程安全；确定性（NFR-COR-02）。
     */
    virtual EstimateOutcome estimateLink(const std::vector<SegmentSpec>& segments,
                                         std::vector<core::DiagnosticRecord>& diags) const = 0;

    /**
     * @brief 平行轴迁移（质心修改确认流选项 a；§5.3 规则 2 的实现）。
     *
     * 数学（卡 §5.3 规则 2 原文式）：I' = I + m((d·d)E − ddᵀ)，d＝质心位移
     * （新参考点相对原质心的位移，单位 m）。分量展开：
     *   Ixx' = ixx + m(dy²+dz²)、Iyy' = iyy + m(dx²+dz²)、Izz' = izz + m(dx²+dy²)；
     *   Ixy' = ixy − m·dx·dy、Ixz' = ixz − m·dx·dz、Iyz' = iyz − m·dy·dz。
     * 迁移不改变张量的来源事实（GeometricEstimate 迁移后仍是估算衍生物、
     * UserProvided 迁移后仍是用户提供衍生物）——provenance 由调用方保留
     * 原值，本函数只算数值。
     *
     * @param atCom  [in] 原质心基准下的张量（kg·m²；六分量，须有限）
     * @param massKg [in] 质量（kg，>0——0 质量下迁移项恒零，属调用方错误，
     *               本函数 noexcept 不校验，前置由 applyCentroidEdit 把关）
     * @param deltaM [in] 质心位移 d（新参考点−原质心），单位 m，连杆系下表示
     * @return 迁移后的张量（对新参考点的质心系张量；kg·m²）
     *
     * 纯函数；noexcept（纯算术无失败路径）；确定性。
     */
    virtual InertiaTensor migrateByParallelAxis(const InertiaTensor& atCom,
                                                double massKg,
                                                const rw::math::Vector3D<double>& deltaM) const noexcept = 0;

    /**
     * @brief 唯一公式表版本（§9.4.6 签名；与 kPropertyFormulaVersion 同源）。
     * @return 版本串（静态存储期）。纯函数；noexcept。
     */
    virtual std::string_view formulaVersion() const noexcept = 0;
};

/**
 * @brief IPropertyEstimator 的唯一产品实现（无状态——可默认构造，随处
 *        持有；拷贝/移动平凡；RobotDesignCodec 同款形态）。
 */
class PropertyEstimator final : public IPropertyEstimator {
public:
    EstimateOutcome estimateLink(
        const std::vector<SegmentSpec>& segments,
        std::vector<core::DiagnosticRecord>& diags) const override;
    InertiaTensor migrateByParallelAxis(const InertiaTensor& atCom,
                                        double massKg,
                                        const rw::math::Vector3D<double>& deltaM) const noexcept override;
    std::string_view formulaVersion() const noexcept override;
};

// =====================================================================
// 质心修改确认流（§5.3 规则 2——MDL-05 硬性交互约束；V-17）
// =====================================================================

/**
 * @brief 质心修改的惯量处置二选一（§5.3 规则 2——用户必须显式选择的分支）。
 *
 * 编辑器（T08）把用户选择映射为本枚举并连同结果写入编辑差值与命令摘要
 * （卡原文"选择随编辑差值与命令摘要留痕"——留痕载体在编辑器/命令层，
 * 本单元提供判定的域级实现与结果值）。两值均有 token；表尾追加纪律同
 * 其他持久化契约面枚举。
 */
enum class CentroidEditResolution {
    /// "migrate-inertia"——分支 (a)：惯量按平行轴定理迁移（d＝质心位移）
    MigrateInertia,
    /// "overwrite-inertia"——分支 (b)：强制覆盖完整张量（用户输入全部六分量）
    OverwriteInertia,
};

/**
 * @brief 处置分支稳定 token（"migrate-inertia"/"overwrite-inertia"——编辑
 *        差值与命令摘要留痕的机器判别串）。纯函数；确定性。
 */
std::string_view centroidEditResolutionToken(CentroidEditResolution resolution) noexcept;

/**
 * @brief 应用一次质心修改（§5.3 规则 2 的域级判定点；V-17 三分支的实现）。
 *
 * ★ 域规则（MDL-05/M-2，DTB 禁止项原文"不静默脱钩质心与惯量基准"）：
 *   - 分支 (a) MigrateInertia：惯量按平行轴迁移 I'=I+m((d·d)E−ddᵀ)，
 *     d＝newCom−旧 com；接受。
 *   - 分支 (b) OverwriteInertia：replacementTensor 整体覆盖（InertiaTensor
 *     六分量即"完整张量"——表示层面不可只覆盖主项）；接受。
 *   - 分支 (c) resolution==nullopt（既不迁移也不覆盖）：返回
 *     ModelingError{CentroidEditUnresolved} 拒绝，body 保持原值不变——
 *     质心与惯量基准不静默脱钩。
 *
 * 接受后的 provenance（§5.3 规则 1"用户直接修改物性→UserProvided 覆盖"）：
 *   - com：UserProvided（用户改的就是它）；
 *   - (a) 分支 inertia：保留原 provenance（迁移是基准点移动，不改变张量
 *     数值的来源事实——估算衍生物仍是估算衍生物）；
 *   - (b) 分支 inertia：UserProvided（用户输入的新张量）。
 *
 * @param body              [in,out] 待修改的连杆物性组。拒绝时保证不变
 *                          （先算后提交的强异常安全序）；接受时 com/inertia
 *                          已更新（mass/material 不动）。
 * @param newCenterOfMass   [in] 新质心位置，单位 m，连杆坐标系下表示
 * @param resolution        [in] 用户选择的处置分支；nullopt＝未选择（分支 c）
 * @param replacementTensor [in] 分支 (b) 的新张量（kg·m²，质心基准/连杆系
 *                          姿态）；分支 (a)/(c) 下忽略
 * @return nullopt＝接受（body 已更新）；非空＝拒绝（当前仅分支 c 的
 *         CentroidEditUnresolved——值面返回，调用方呈现并保持原状）
 *
 * @pre body.mass/centerOfMass/inertia 均 Provided（V-17 前置"已估算连杆"
 *      ——无既有基准则"迁移/覆盖"语义不成立）；违约＝调用方契约错误，
 *      抛 std::invalid_argument fail-fast（AGENTS 错误语义：调用方错误
 *      fail-fast；正常业务拒绝才走值面）。分支 (b) 还要求
 *      replacementTensor 已带值（覆盖必须有新张量输入），缺省同上 fail-fast。
 *
 * 纯函数（输入输出均为调用方持有数据，无共享状态）；线程安全；确定性。
 */
std::optional<ModelingError> applyCentroidEdit(
    BodyData& body,
    const rw::math::Vector3D<double>& newCenterOfMass,
    std::optional<CentroidEditResolution> resolution,
    const std::optional<InertiaTensor>& replacementTensor);

}  // namespace sdurws::ird::modeling

#endif  // IRD_MODELING_PROPERTYESTIMATION_HPP
