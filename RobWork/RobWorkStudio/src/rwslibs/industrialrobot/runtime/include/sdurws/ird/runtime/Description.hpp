/**
 * @file   Description.hpp
 * @brief  RobotDesignDescription——注入输入的中性值类型（§4.2 原文契约）＋
 *         IRobotDesignReader（modeling 实现、L5 装配注入的解析接口）。
 *
 * 设计依据：
 *   - units/runtime.md §4.2（本头全部类型的原文契约——字段/单位/四态承载/
 *     "为什么需要它"的方案说明）、§4.4（单位与数值纪律——Description 内
 *     只有 SI 真值）、§6.2（安装预设 token 表——InstallationPresetToken）、
 *     §4.3.4（CouplingMatrix 形态——MDL-21 耦合矩阵）、§5.2 S2（reader
 *     在十段编译链中的位置）
 *   - 需求 MDL-12/V12-01（关节类型保留）、MDL-09（轴向权威一等字段）、
 *     MDL-13/04（工具与默认 TCP）、MDL-15（环境对象）、MDL-21（耦合矩阵，
 *     R2）、MDL-22（基座布置）、KIN-14（默认 TCP 权威来源）、NFR-COR-03
 *     （缺失不伪造——全 SourcedValue 化）
 *   - 任务契约 tasks/foundation/RT-T03.json（产物 1）
 *
 * 背景说明（为什么需要它——§4.2 开篇原文）：RobotDesign 对象 schema 归
 * modeling（N-2），runtime 不解析其对象内码；但编译需要完整的参数化语义。
 * 方案＝runtime 公共头定义中性描述值类型（本头），modeling 实现
 * IRobotDesignReader（对象字节→Description；纯函数），L5 装配注入。
 * 阶段 A 以契约夹具直接构造 Description 值测试编译链（§12）；真实 reader
 * 随 modeling（阶段 B）交付。
 *
 * 单位纪律（§4.2/§4.4，逐字段单位见各成员注释）：Description 中数值字段
 * 一律 SI 真值（m/rad/kg/s/…）；调用方（reader 实现）负责在生成
 * Description 前经 core 唯一换算入口完成 SI 化——编译器对 Description
 * 入口做抽样单位一致性复核不可行（值已无量纲），故硬单位错误在 reader
 * 侧拒绝、编译器侧（S3 校验器，RT-T03 的 DescriptionValidator）以数值
 * 合法性（有限性、量级期望）复核。
 *
 * ★ 分阶段落位登记（RT-T03，实现与 §3.1 模块表的偏差按 DTB §5.4 登记）：
 *   InstallationPresetToken 在 §3.1 模块表列于 BaseWorldTransform.hpp
 *   （§6，RT-T06 交付）。因 §4.2 BasePlacementDescription.preset 的字段
 *   类型即该枚举、而 RT-T03 先于 RT-T06 交付，故枚举随本头先行落位；
 *   RT-T06 的预设→矩阵纯函数经 include 本头消费（单一权威，不重复定义）。
 *   已在 units/runtime.md §3.1/§15.4 增量修订登记。
 *
 * 线程安全：全部纯值类型（无共享可变状态）；IRobotDesignReader 实现方
 * 约定并发只读安全（§5.5——纯函数：同字节→同 Description）。
 * rw::math 说明：字段按 §4.2 原文使用 rw::math 值类型（模板，header-only
 * 使用——冒烟模式经源码树 include 路径获得、不链接框架库；约束：不得
 * 调用 Rotation3D::identity() 等外联符号，构造一律用逐元素构造函数）。
 */

#ifndef SDURWS_IRD_RUNTIME_DESCRIPTION_HPP
#define SDURWS_IRD_RUNTIME_DESCRIPTION_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <rw/math/InertiaMatrix.hpp>
#include <rw/math/Transform3D.hpp>
#include <rw/math/Vector3D.hpp>

#include <sdurws/ird/core/Provenance.hpp>  // SourcedValue<T>（四态字段值——缺失≠非法）
#include <sdurws/ird/runtime/Errors.hpp>   // Expected<T,E>／RuntimeError（reader 返回轨）
#include <sdurws/ird/runtime/Resource.hpp> // ResourceRef（资源引用——§8.6）

namespace sdurws::ird::runtime {

// ---- 实现细节（非公共契约；core 的 detail 命名空间同例）----
namespace detail {

/// 恒等变换（单位旋转＋零平移）。
/// ★ 为何不用 Transform3D 默认构造/Rotation3D::identity()：rw 的该静态
///   成员是外联符号（定义在框架 .cpp）——冒烟模式 rw 仅模板头 header-only
///   可达、不链接框架库（见 runtime/CMakeLists.txt 冒烟分支），默认构造会
///   产生未解析符号。本函数只用内联的逐元素构造（Vector3D(x,y,z)＋
///   Rotation3D 九参＋Transform3D(d,R)），两模式语义一致＝恒等变换，
///   零外联符号引用（实现纪律随文件头登记）。
inline rw::math::Transform3D<double> identityTransform3D()
{
    return rw::math::Transform3D<double>(
        rw::math::Vector3D<double>(0.0, 0.0, 0.0),
        rw::math::Rotation3D<double>(1, 0, 0, 0, 1, 0, 0, 0, 1));
}

}  // namespace detail

// =====================================================================
// 关节类型（§4.2——类型保留，MDL-12/V12-01：导入识别后不丢失类型与来源；
// runtime 只承载与编译，"continuous 经工程工作范围确认后方可进入正式链"
// 等门控语义归 modeling）。
// =====================================================================

/**
 * @brief 关节运动类型（§4.2 JointDescription.type；MDL-12 支持四型）。
 *
 * 枚举顺序即 §4.2 注释声明顺序（Revolute | Continuous | Prismatic |
 * Fixed）；数值进入 RT-Codec 编码面（§4.5 按声明序编码），一经交付不得
 * 改动/插入。
 */
enum class JointType {
    Revolute,   ///< 旋转关节（有有限限位；lower/upper 必填，单位 rad）
    Continuous, ///< 连续旋转关节（无限位；lower/upper 必为 NotProvided，
                ///<  工程工作范围另载于 workingRange——MDL-12）
    Prismatic,  ///< 移动关节（lower/upper 必填，单位 m）
    Fixed,      ///< 固定关节（刚性连接；无限位/工作范围语义）
};

// =====================================================================
// 工程工作范围（§4.2 JointDescription.workingRange；MDL-12——continuous
// 关节经用户确认后的分析消费属性，不回写权威模型）。
// =====================================================================

/**
 * @brief 连续关节的工程工作范围（有限区间，MDL-06 断言④豁免口径）。
 *
 * 语义：仅对 Continuous 关节有意义（§4.3.3 workingRange"仅 Continuous"）；
 * 必须为有限区间且 qmin＜qmax（校验器复核，非法→InputInvalid）。
 * 单位：rad（若未来出现移动关节连续型，按其量纲对应——当前四型下仅
 * Continuous 旋转语义）。
 * 值语义纯结构；分析消费属性——确认后不丢失类型/来源（MDL-12）。
 */
struct WorkingRange {
    double lower = 0.0; ///< 工作范围下界，单位 rad（须＜ upper 且有限）
    double upper = 0.0; ///< 工作范围上界，单位 rad（须＞ lower 且有限）
};

// =====================================================================
// 几何资源引用（§4.2 visual/collision/geometry——"资源引用（§8.6）"）。
// =====================================================================

/**
 * @brief 几何资源引用（§4.2 LinkDescription.visual/collision 等字段类型）。
 *
 * 语义：指向一条几何资源（§8.6 ResourceRef——以 resourceId＋内容摘要
 * 为键，路径不作身份）。§4.2 注释"几何级权威值可覆盖估算（MDL-05）"
 * 指该引用所指向的几何将用于物性估算、而几何级权威值可覆盖之——覆盖
 * 语义归 modeling 估算链，本类型只承载引用。
 * 显式结构体（而非 using）以保留后续扩展位（如 LOD/几何变换提示）而
 * 不破坏编码布局（§4.5 身份编码面稳定第一）。
 * 值语义纯结构；线程安全。
 */
struct GeometryRef {
    ResourceRef resource; ///< 被引用的几何资源（内容摘要入身份，路径不入——§8.6）
};

// =====================================================================
// 安装预设 token（§6.2 原文 token 表；分阶段落位说明见文件头）。
// =====================================================================

/**
 * @brief 基座安装预设（§6.2——Ground | Inverted | Wall | Custom 四态）。
 *
 * 语义（§6.2 精确定义）：preset 是安装的**编辑表示/来源记录**，本体
 * 旋转存矩阵（CanonicalModel.T_world_base，§4.3.2——preset 不入身份、
 * R 入身份）；preset→旋转矩阵的权威映射与一致性校验（如 Custom 而
 * R=I→InputInvalid）归 §6（RT-T06）与 S5 构造校验（RT-T04）。
 * 阶段 A 语义锚点（§6.2 表）：
 *   Ground    地面安装（默认——未显式配置即此值，V15-04）；
 *   Inverted  倒挂 180°（R_x(π)，轴向选择登记 P-RT-4）；
 *   Wall      壁/侧装 90°（R_y(π/2)，同 P-RT-4）；
 *   Custom    自定义（编辑侧 customEaa 给出旋转矢量，矩阵由换算产生）。
 * 枚举顺序与数值经 RT-Codec 编码进入身份面——一经交付不得改动/插入。
 */
enum class InstallationPresetToken {
    Ground,   ///< 地面安装（默认——V15-04）
    Inverted, ///< 倒挂 180°（R_x(π)——P-RT-4）
    Wall,     ///< 壁/侧装 90°（R_y(π/2)——P-RT-4）
    Custom,   ///< 自定义（customEaa 必填——§4.2）
};

// =====================================================================
// 耦合矩阵及其适用范围（MDL-21，R2——§4.2 DrivetrainDescription.coupling、
 // §4.3.4 字段表形态 CouplingMatrix{C(rows×cols 常矩阵), jointRange,
 // conditionNumber}）。
// =====================================================================

/**
 * @brief 耦合矩阵的适用关节范围（§4.3.4"jointRange 适用边界"）。
 *
 * 语义：0 基链序下标区间 [firstIndex, firstIndex+count)——指向
 * RobotDesignDescription.joints 的连续子段（MDL-21 典型＝腕部关节，
 * 如轴 4～6）。约束（校验器复核，§5.2 S3）：count ≥ 1 且
 * firstIndex+count ≤ joints.size()，矩阵 C 的方阵维度必须等于 count。
 * 值语义纯结构；线程安全。
 */
struct JointIndexRange {
    std::uint32_t firstIndex = 0; ///< 起始关节下标（0 基、链序，无单位）
    std::uint32_t count = 0;      ///< 适用关节数（≥1；＝耦合矩阵 C 的阶）
};

/**
 * @brief 线性耦合常矩阵（MDL-21，R2——关节空间与驱动端的双向线性映射）。
 *
 * 背景说明（MDL-21/M-12/DYN-04）：腕部关节可配置线性耦合/传动比矩阵 C，
 * 位置与力矩经 C 双向映射（Δq_joint＝C·Δθ_motor；τ_motor＝Cᵀ·τ_joint
 * 精确虚功映射）。**本类型只承载常矩阵**——非常矩阵在建模侧就无法进入
 * （§4.3.4）；编译侧校验（§5.2 S3，RT-T03 DescriptionValidator 实现）：
 * 方阵维度＝适用关节数、可逆、条件数 ≤1×10⁸（设计默认，登记 P-RT-7
 * 待策略侧确认归属——runtime 维持该默认并随诊断输出实际条件数，归属
 * 裁决不私裁）；奇异/病态→InputInvalid 阻止编译并给比较型诊断
 * （M-6/M-12）。
 *
 * 存储形态：C 以**行主序扁平数组**承载（rows×cols 个 double；与 §4.5
 * "旋转矩阵 9 分量按行主序编码"同精神——确定性与表示无关性）。元素
 * (i,j) 位于 c[i*cols+j]。
 * 值语义纯结构；线程安全。
 */
struct CouplingMatrix {
    std::uint32_t rows = 0;          ///< 矩阵行数（合法实例＝cols＝适用关节数）
    std::uint32_t cols = 0;          ///< 矩阵列数（合法实例＝rows）
    std::vector<double> c;           ///< 行主序扁平数据（元素 (i,j)＝c[i*cols+j]；须恰 rows*cols 个）
    JointIndexRange jointRange;      ///< 适用关节范围（方阵维度必须等于 count）
    /**
     * 条件数申报值（σmax/σmin，2-范数；无量纲）。
     * 仅为建模侧的申报/缓存参考——**校验器一律以重算值为准**（申报值
     * 不参与合法性判定，防申报失真绕过病态阻止；P-RT-7 处置约束要求
     * 诊断输出实际条件数即指重算值）。可为 nullopt（未申报）。
     */
    std::optional<double> conditionNumber;
};

// =====================================================================
// 各子结构（§4.2 原文契约——字段逐一对应，注释为原文语义＋单位标注）。
// =====================================================================

/**
 * @brief 关节描述（§4.2——权威参数化侧；DH 权威时由 modeling 先转为
 *        显式表示再产出本结构）。
 *
 * 值语义纯结构；串联顺序由 RobotDesignDescription.joints 的下标表达
 * （基座→法兰）。线程安全。
 */
struct JointDescription {
    std::string localName; ///< 权威局部名（进入 RuntimeNameMap 与模型身份——
                           ///<  重命名＝设计变更＝新内容版本；非法：空/含 '/'）
    JointType type = JointType::Revolute; ///< 关节类型（保留不回写——MDL-12/V12-01）
    rw::math::Vector3D<double> axis{};    ///< 轴向，单位向量（无量纲；权威一等字段
                                          ///<  MDL-09；不要求为 Z——MDL-11；零向量/
                                          ///<  非有限→InputInvalid，编译器规格化）
    /**
     * T_parent_joint：父连杆系→关节系变换（读法 core §4.6——T_ab＝b 相对 a）。
     * 旋转部分须正交（max|RᵀR−I| ≤ 1×10⁻¹²，§6.6）且非反射（det＞0）；
     * 平移单位 m。非法（非正交/反射/非有限）→InputInvalid（RT-BW-6 校验器面）。
     */
    rw::math::Transform3D<double> origin = detail::identityTransform3D(); // 单位变换初值（T_parent_joint 必被 reader 覆写——见 detail 注释）
    /// 关节下限。单位 rad（Prismatic：m）；Revolute/Prismatic 必填、
    /// Continuous 必为 NotProvided（§4.2——工作范围另载）。
    core::SourcedValue<double> lower;
    /// 关节上限。单位 rad（Prismatic：m）；须＞ lower（qmin≥qmax→InputInvalid）。
    core::SourcedValue<double> upper;
    /// 最大角速度。单位 rad/s（Prismatic：m/s）；Provided 时须有限且≥0
    /// （负数→InputInvalid，§4.3.3）；缺失＝能力缺失（降级非失败，§5.6）。
    core::SourcedValue<double> maxVelocity;
    /// 最大角加速度。单位 rad/s²（Prismatic：m/s²）；约束同 maxVelocity。
    core::SourcedValue<double> maxAcceleration;
    /// Continuous 的工程工作范围（有限区间，MDL-12；分析消费属性）——
    /// 仅 Continuous 可有（§4.3.3"仅 Continuous"）。
    std::optional<WorkingRange> workingRange;
};

/**
 * @brief 连杆描述（§4.2——links 数量＝joints.size()+1，含基座连杆；
 *        下标 i 的连杆是关节 i 的父体，基座连杆下标 0）。
 *
 * 物性三元的四态语义（§4.3.3/§5.6）：NotProvided＝能力缺失（dynamics
 * 降级 DYN-06，非非法）；Provided 时合法性由编译器复核（m≤0/非对称/
 * 非正定→InputInvalid，与断言一致的就地拦截——MDL-06 断言分域）。
 * 值语义纯结构；线程安全。
 */
struct LinkDescription {
    std::string localName; ///< 权威局部名（链内唯一性归名称映射消歧——§7.2；
                           ///<  空名→InputInvalid）
    /// 视觉几何资源引用（可空；资源以内容摘要入身份——§8.6）。
    std::optional<GeometryRef> visual;
    /// 碰撞几何资源引用（可空；MDL-05 几何级权威值可覆盖估算）。
    std::optional<GeometryRef> collision;
    /// 连杆质量，单位 kg。Provided 时须有限且＞0（RT-CPX-3：m≤0→InputInvalid）。
    core::SourcedValue<double> mass;
    /// 质心位置，连杆系下表示（单位 m；惯量基准＝质心/连杆参考姿态——M-2）。
    core::SourcedValue<rw::math::Vector3D<double>> centerOfMass;
    /// 惯量张量，质心系下表示（单位 kg·m²；MDL-05 惯量基准）。Provided 时
    /// 须对称（容差附录 D 第 6 项：1×10⁻¹²）且正定（SPD）。
    core::SourcedValue<rw::math::InertiaMatrix<double>> inertia;
    /// 材料标识（估算来源用；显示/追溯属性，不入身份——§4.3.6）。
    std::optional<std::string> material;
};

/**
 * @brief 工具描述（§4.2——tools 首项为默认 TCP，MDL-13/04/KIN-14）。
 *
 * 值语义纯结构；线程安全。
 */
struct ToolDescription {
    std::string localName; ///< 工具局部名（空名→InputInvalid）
    /// 工具几何资源引用（可空；MDL-13：几何引用 ToolDefinition 资源，不复制）。
    std::optional<GeometryRef> geometry;
    /// 工具质量，单位 kg。Provided 时须有限且＞0（同连杆口径）。
    core::SourcedValue<double> mass;
    /// 工具质心，工具系下表示（单位 m；约束同连杆质心）。
    core::SourcedValue<rw::math::Vector3D<double>> centerOfMass;
    /// 工具惯量张量，质心系下表示（单位 kg·m²；约束同连杆惯量）。
    core::SourcedValue<rw::math::InertiaMatrix<double>> inertia;
    /**
     * 法兰→TCP 变换（T_flange_tcp，读法 core §4.6；KIN-14 默认 TCP 的
     * 权威来源）。旋转须正交非反射、平移有限——非法→InputInvalid
     * （RT-BW-6 校验器面）。单位：平移 m。
     */
    rw::math::Transform3D<double> tcpOffset = detail::identityTransform3D(); // 单位变换初值（KIN-14 默认形态）
};

/**
 * @brief 场景（环境）对象描述（§4.2——世界系固连，MDL-15）。
 *
 * 注意：worldPose 为**世界系**位姿——不得预乘安装旋转（§6.4 禁止项 3：
 * 环境固连世界系，只有机器人链带着 T_world_base；该约束的编译期检测在
 * S6/S9，本类型只承载）。值语义纯结构；线程安全。
 */
struct SceneObjectDescription {
    std::string localName; ///< 对象局部名（空名→InputInvalid）
    /// 世界系固连位姿（T_world_scene；单位 m；非有限/非正交/反射→InputInvalid——
    ///  RT-BW-6"场景位姿非有限"反例的字段）。
    rw::math::Transform3D<double> worldPose = detail::identityTransform3D(); // 单位变换初值（场景位姿必被赋值）
    GeometryRef geometry; ///< 环境几何资源引用（§4.2 原文为值成员——必有）
};

/**
 * @brief 基座布置描述（§4.2——MDL-22 基座—世界关系的编辑侧输入）。
 *
 * 语义：本结构是编辑表示；编译产物 T_world_base（唯一存储位置，§6.3）
 * 由 §6 预设规则（RT-T06）从 preset＋customEaa＋basePosition 解析。
 * 值语义纯结构；线程安全。
 */
struct BasePlacementDescription {
    InstallationPresetToken preset = InstallationPresetToken::Ground; ///< 安装预设
                                                                     ///<  （默认 Ground——未显式配置即地面，V15-04）
    /// Custom 预设的旋转矢量（EAA 编辑表示，单位 rad；Custom 时必填——§4.2；
    ///  旋转本体存矩阵，此处仅编辑表示——§6.2）。非 Custom 时可不提供。
    core::SourcedValue<rw::math::Vector3D<double>> customEaa;
    /// 基座原点在世界系的位置，单位 m（倒挂吊装时 z 即吊装高度——§6.5 例）。
    rw::math::Vector3D<double> basePosition{};
};

/**
 * @brief 传动描述（§4.2——阶段 B 消费；MDL-21 矩阵为 R2）。
 *
 * 值语义纯结构；线程安全。
 */
struct DrivetrainDescription {
    /// 逐关节对角传动比（无量纲比值；OPT-02 StageB 连续变量）。向量长度
    /// 要么为 0（全缺省），要么＝joints.size()（逐关节口径，§4.3.4）；
    /// Provided 项须为正有限值（§4.3.4"合法：正有限值"）。
    std::vector<core::SourcedValue<double>> ratioPerJoint;
    /// 线性耦合常矩阵＋适用关节范围（R2——MDL-21；校验见 CouplingMatrix
    /// 注释与 §5.2 S3；RT-CPL-1 为其预置用例）。
    std::optional<CouplingMatrix> coupling;
};

/**
 * @brief 逐关节摩擦描述（§4.2——MDL-16；缺失走 DataInsufficient 降级，
 *        判定归 dynamics，runtime 只承载）。
 *
 * 值语义纯结构；Provided 时须有限正值（§4.3.3"合法：NotProvided 或有限
 * 正值"）。线程安全。
 */
struct JointFrictionDescription {
    /// 粘性摩擦系数 fv。单位 N·m·s/rad（移动关节量纲对应 N·s/m）。
    core::SourcedValue<double> viscous;
    /// 库仑摩擦力矩 fc。单位 N·m（移动关节：N）。
    core::SourcedValue<double> coulomb;
    /// 摩擦偏置。单位 N·m（移动关节：N）。
    core::SourcedValue<double> bias;
};

// =====================================================================
// RobotDesignDescription（§4.2 原文契约——注入输入的中性值类型本体）。
// =====================================================================

/**
 * @brief 编译注入输入的中性描述值类型（§4.2——modeling 对象语义到
 *        runtime 编译链的边界投影）。
 *
 * 生命周期：S2 段由 reader 产出的临时值（§5.2——"Description 值"归
 * CompileTransaction 持有，失败即析构）；阶段 A 由测试夹具直接构造。
 * 可变性：值语义（构造后由持有方决定；编译链内只读使用）。
 * 线程安全：纯值；作为编译输入时按 §5.5"注入接口并发只读安全"使用。
 *
 * 默认构造语义：descriptionContractVersion=0（非法值——须由 reader 显式
 * 置 ≥1，§4.3.1）；各向量为空；base.preset=Ground（未显式配置＝地面，
 * V15-04）。默认实例必然校验失败（joints 为空）——"空模型没有评估意义"
 * 的 fail-fast 语义在 S3 校验器落点，不在类型层拒绝（类型层保持聚合体
 * 可默认构造，便于增量装配）。
 */
struct RobotDesignDescription {
    /// 契约版本（进入编译缓存键，§9.4；合法 ≥1——§4.3.1 同口径）。
    std::uint32_t descriptionContractVersion = 0;
    std::string robotLocalName; ///< 机器人局部名（进入名称映射设备作用域，
                                ///<  MDL-14；非法：空/含 '/'——§4.3.3）
    /// 关节链，串联顺序＝基座→法兰（§4.2；合法 ≥1 个——§4.3.3）。
    std::vector<JointDescription> joints;
    /// 连杆集合，数量＝joints.size()+1（含基座连杆，下标 0——§4.2/§4.3.3）。
    std::vector<LinkDescription> links;
    /// 工具集合，≥0；首项为默认 TCP（MDL-13/04）。
    std::vector<ToolDescription> tools;
    /// 场景（环境）对象集合（世界系固连——MDL-15）。
    std::vector<SceneObjectDescription> scene;
    BasePlacementDescription base; ///< 基座布置（MDL-22——编辑表示）
    DrivetrainDescription drivetrain; ///< 传动（阶段 B 消费；耦合矩阵 R2）
    /// 逐关节摩擦；可为空（＝全部 NotProvided——§4.2）；非空时长度必须
    /// ＝joints.size()（逐关节口径）。
    std::vector<JointFrictionDescription> friction;
    /// 全部资源引用清单（§8.6——与 CanonicalModel.resourceManifest 一致性
    ///  校验在 S5/RT-T04；本结构只承载清单）。
    std::vector<ResourceRef> resourceRefs;
};

// =====================================================================
// IRobotDesignReader（§4.2 原文契约——modeling 实现，阶段 B；L5 装配注入）。
// =====================================================================

/**
 * @brief RobotDesign 对象字节→中性描述值的解析接口（§4.2——纯函数）。
 *
 * 契约要点：
 *   - 纯函数：同字节→同 Description（确定性——reader 不得读环境/时钟/
 *     locale；§5.5"注入接口并发只读安全"）；
 *   - 实现方在生成 Description 前经 core 唯一换算入口完成 SI 化（§4.2
 *     单位纪律）；非法单位/不可解析内容在 reader 侧拒绝（reader 失败→
 *     S2 归属 InputInvalid 含定位——§5.2 S2）；
 *   - 适配器归 L5 应用壳装配期提供（NFR-MNT-04）；阶段 A 以 ScriptedReader
 *     测试替身替代（§11——替身边界声明见 RT-STUB-0）。
 *
 * 线程约束：实现方保证 const read() 并发安全（§5.5）。
 */
class IRobotDesignReader {
public:
    virtual ~IRobotDesignReader() = default;

    /**
     * @brief 解析对象字节为中性描述值（只读、非抛出——Expected 两态轨）。
     *
     * @param objectBytes            [in] RobotDesign 对象的 canonical 字节
     *                               （非拥有——函数期间只读）
     * @param objectTypeFormatVersion [in] 对象类型格式版本（无单位；reader
     *                               据此选择解析规则——版本不识别时拒绝而非
     *                               尽力猜测，NFR-COR-03 不静默精神）
     * @return ok＝解析出的 Description（值语义，调用方持有）；err＝reader
     *         失败（RuntimeError 携 InputInvalid 与定位 detail——S2 归属表，
     *         §5.2）；不抛异常、不返回默认构造的 Description 静默吞错
     *         （NFR-COR-03）。
     */
    virtual Expected<RobotDesignDescription, RuntimeError>
        read(const std::vector<std::uint8_t>& objectBytes,
             std::uint32_t objectTypeFormatVersion) const = 0;
};

}  // namespace sdurws::ird::runtime

#endif  // SDURWS_IRD_RUNTIME_DESCRIPTION_HPP
