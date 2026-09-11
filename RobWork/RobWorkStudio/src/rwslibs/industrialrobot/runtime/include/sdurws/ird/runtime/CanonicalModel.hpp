/**
 * @file   CanonicalModel.hpp
 * @brief  CanonicalModel——从修订对象闭包确定性编译出的不可变 SE(3) 关节链
 *         规范模型（§4.3 字段表逐项承载）＋CanonicalModelBuilder（S5 构造
 *         不变量）＋RuntimeCapability（§9.6 能力声明）。
 *
 * 设计依据：
 *   - units/runtime.md §4.1～§4.4（四个模型对象的区别、§4.3 字段表逐列、
 *     §4.3.6 三种"等价"冻结、§4.4 SI 单位纪律）、§4.6（对象/资源只读索引）、
 *     §9.6（RuntimeCapability 字段与派生规则）
 *   - 需求 ARC-03（唯一规范模型——派生的运行时规范真值）、CON-05（内容
 *     寻址——contentIdentity＝SHA-256 over RT-Codec 身份域编码）、CON-01
 *     （CM-0 防混入——objectRefs 经闭包校验，闭包判定归 S1/S2/RT-T11）、
 *     NFR-COR-03（缺失不伪造——SourcedValue 四态保留）、MDL-06/09/11/12/
 *     13/14/15/16/21/22（各字段语义出处，随成员注释标注）
 *   - 任务契约 tasks/foundation/RT-T04.json（产物 1：CanonicalModel.hpp/.cpp
 *     含 RuntimeCapability、builder 不变量）
 *
 * 背景说明（本类型在四个模型对象中的位置——§4.1 表）：RobotDesign 是权威
 * 参数化真值（modeling/project 拥有、持久化）；CanonicalModel 是从某修订
 * 的对象闭包确定性编译出的**派生运行时规范真值**——SI 单位、显式关节表示、
 * 单一 T_world_base、资源内容绑定。它**不可变（构造后无 setter）、瞬态
 * （不持久化——决策 D-01）、值语义拷贝**，随 RuntimeSnapshot 存活；其内容
 * 身份（contentIdentity）进入评估切片 Environment 条目与编译缓存键。
 *
 * 不变量总览（构造即成立，build() 是唯一执行点）：
 *   - CM-0 真值唯一（§4.1）：objectRefs 全部来自同一修订闭包（闭包成员判定
 *     归 S1/S2，本类型在值层面要求引用一致性：链/工具/场景/机器人的
 *     ObjectId ∈ header.objectRefs 且全模型唯一）；
 *   - §4.3 各字段表"合法与非法实例"列的值级约束（正交旋转、有限数值、
 *     qmin＜qmax、 Provided 物性为正、SPD 惯量等）；
 *   - capabilities 与内容一致（§4.3.5/§9.6——由 build() 从内容**派生**而非
 *     调用方申报，不一致状态在类型层不可能出现，强于"构造拒绝"）；
 *   - diagnostics 仅警告级（§4.3.5——error 级稳定码出现＝编译已失败却试图
 *     发布，违反 MDL-06 原子性，构造拒绝）；
 *   - contentIdentity 非零且由 builder 计算（§4.3.5——"builder 计算非调用
 *     方申报"，经 Codec.hpp 的 rtcodec::computeContentIdentity，CR-02）。
 *
 * 等值语义（§4.3.6 冻结——RT-ID-2 钉住）：operator== 对身份域字段按 IEEE754
 * **位模式**比较（+0.0 与 −0.0 不等、1×10⁻¹⁵ 差异不等）——与
 * rtcodec::encodeIdentityDomain 字节等值同口径；"数值容差内等价"严禁用于
 * 身份/等值判断（浮点近似无传递性，仅限数值校验断言）。诊断块按 core 精确
 * 等值比较（诊断不入身份域——决策 D-12，位级差异对身份无影响）。
 *
 * 分阶段落位登记（RT-T04）：§4.6 的三类只读索引中，对象索引与资源索引随本
 * 类型落地；层级索引（规范侧 Frame 树路径）依赖 §7.2 名称生成与 §6 层级
 * 命名的消费语义，随其消费任务（RT-T05/RT-T07）落位——已在 units/runtime.md
 * §15.4 增量修订登记。
 *
 * 线程安全：CanonicalModel 构造完成后只读（全部访问器 const、无 setter），
 * 可并发共享只读；CanonicalModelBuilder 非线程安全（仅构造线程使用）。
 * rw::math 说明：成员按 §4.2/§4.3 使用 rw::math 值类型（模板，header-only
 * 使用——冒烟模式不链接框架库；不调用 Rotation3D::identity() 等外联符号，
 * 恒等初始化经 Description.hpp 的 detail::identityTransform3D()）。
 */

#ifndef SDURWS_IRD_RUNTIME_CANONICALMODEL_HPP
#define SDURWS_IRD_RUNTIME_CANONICALMODEL_HPP

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <rw/math/InertiaMatrix.hpp>
#include <rw/math/Transform3D.hpp>
#include <rw/math/Vector3D.hpp>

#include <sdurws/ird/core/DiagData.hpp>     // DiagnosticRecord（§4.3.5 诊断块）
#include <sdurws/ird/core/Digest.hpp>       // Digest256/ContentIdentity（内容身份）
#include <sdurws/ird/core/Identity.hpp>     // ProjectId/BranchId/RevisionId/ObjectId
#include <sdurws/ird/core/Provenance.hpp>   // SourcedValue<T>（四态承载——缺失≠非法）
#include <sdurws/ird/runtime/Description.hpp>  // JointType/WorkingRange/
                                               // InstallationPresetToken/CouplingMatrix
                                               // ＋detail::identityTransform3D()
#include <sdurws/ird/runtime/Errors.hpp>    // RuntimeError（builder 违约抛出轨）
#include <sdurws/ird/runtime/Resource.hpp>  // ResourceRef（§4.3.5 资源清单）
#include <sdurws/ird/runtime/Sources.hpp>   // ObjectRefEntry（§4.3.1 来源块）

namespace sdurws::ird::runtime {

// =====================================================================
// 逐关节/逐连杆的子值类型（§4.3.3 字段表——声明序即 RT-Codec 编码序）。
// =====================================================================

/**
 * @brief 关节限位区间（§4.3.3 bounds 行——"SourcedValue 意义下的 optional
 *        区间"在规范侧的解析形态）。
 *
 * 背景：Description 侧限位以 SourcedValue<double> 承载四态与来源；编译产物
 * （本结构）只保留**解析后的存在性＋值**——存在性进身份（presence 字节）、
 * 值按位模式进身份；来源标记经 header.builtFrom（Description canonical 字节
 * 摘要）间接进入身份，不在逐关节处重复承载。
 * 单位：rad（旋转/连续关节）或 m（移动关节）——随所属关节 type 量纲。
 * 值语义纯结构；线程安全。
 */
struct JointBounds {
    double lower = 0.0; ///< 下界 qmin（单位 rad 或 m；须＜ upper 且有限）
    double upper = 0.0; ///< 上界 qmax（单位 rad 或 m；qmin≥qmax→构造拒绝）
};

/**
 * @brief 逐关节摩擦三元组（§4.3.3 friction 行——MDL-16 模型的规范侧承载）。
 *
 * 语义：各分量独立四态（SourcedValue）；全 NotProvided＝模型不含摩擦模型
 * （dynamics 侧 DataInsufficient 降级 DYN-06——判定归 dynamics，runtime 只
 * 承载事实，§5.6 正交表）。Provided 值须有限且＞0（§4.3.3"合法：NotProvided
 * 或有限正值"）。
 * 值语义纯结构；线程安全。
 */
struct CanonicalJointFriction {
    /// 粘性摩擦系数 fv。单位 N·m·s/rad（移动关节量纲对应 N·s/m）。
    core::SourcedValue<double> viscous;
    /// 库仑摩擦力矩 fc。单位 N·m（移动关节：N）。
    core::SourcedValue<double> coulomb;
    /// 摩擦偏置。单位 N·m（移动关节：N）。
    core::SourcedValue<double> bias;
};

/**
 * @brief 规范关节（§4.3.3 CanonicalJoint 字段表逐项）。
 *
 * 与 JointDescription（§4.2 编辑/注入侧）的区别：已解析/规格化——axis 已
 * 单位化（"编译器规格化"）、限位已解析为 optional 区间、来源四态仅保留在
 * SourcedValue 字段（限速/加速度/摩擦）。全部字段入内容身份（§4.3.3 身份列
 * 全"入"），编码按本声明序（§4.5"字段按声明序"）。
 * 值语义纯结构；线程安全。
 */
struct CanonicalJoint {
    /// 关节对象稳定身份（∈ header.objectRefs；全模型唯一——重复→StructureInvalid）。
    core::ObjectId objectId;
    /// 权威局部名（解析字段——进入 RuntimeNameMap 与模型身份，MDL-14；
    /// 重命名＝设计变更。非法：空/含 '/'；同名消歧归 S8/RT-T05，非构造拒绝）。
    std::string localName;
    /// 关节类型（保留不回写——MDL-12/V12-01；枚举值经 RT-Codec 编码面，
    /// 一经交付不得改动/插入）。
    JointType type = JointType::Revolute;
    /// 关节轴向，**单位向量**（无量纲；MDL-09 权威一等字段、MDL-11 不要求
    /// 为 Z）。build() 就地规格化为单位长度；零向量/非有限→构造拒绝。
    rw::math::Vector3D<double> axis{};
    /**
     * T_parent_joint：父连杆系→关节系变换（读法 core §4.6——T_ab＝b 相对 a）。
     * 旋转须正交（逐元素容差 1×10⁻¹²，§4.3.2/§6.6）且非反射（det＞0）；
     * 平移单位 m、有限。
     */
    rw::math::Transform3D<double> origin = detail::identityTransform3D();
    /// 零位偏置：q_authoritative = q_zeroOffset + q_rw（RobWork 侧 q 从零起算）。
    /// 单位 rad（移动关节 m）；有限。
    double zeroOffset = 0.0;
    /// 关节限位（存在性与值都入身份）。Revolute/Prismatic 必填且 lower＜upper；
    /// Continuous 必无（工作范围替代——MDL-12）；Fixed 未约束（提供则按
    /// 有限＋有序复核——与 S3 校验器同口径）。
    std::optional<JointBounds> bounds;
    /// 连续关节的工程工作范围（有限区间，MDL-12 分析消费属性；不回写权威
    /// 模型）。仅 Continuous 可有（§4.3.3"仅 Continuous"）；无限区间非法。
    std::optional<WorkingRange> workingRange;
    /// 最大角速度。单位 rad/s（移动关节 m/s）；缺失＝能力缺失（§5.6 降级，
    /// 非非法）；Provided 时须有限且≥0（负数→构造拒绝）。
    core::SourcedValue<double> maxVelocity;
    /// 最大角加速度。单位 rad/s²（移动关节 m/s²）；约束同 maxVelocity。
    core::SourcedValue<double> maxAcceleration;
    /// 逐关节摩擦三元组（MDL-16——缺失走 dynamics 降级，runtime 只承载）。
    CanonicalJointFriction friction;
};

/**
 * @brief 规范连杆（§4.3.3 CanonicalLink 字段表逐项）。
 *
 * 数量恒＝joints.size()+1（含基座连杆，下标 0；失配→StructureInvalid）。
 * 物性三元组四态语义：NotProvided＝能力缺失（§5.6，非非法）；Provided 时
 * 合法性由 build() 复核（m≤0/非对称/非正定→InputInvalid——与 MDL-06 断言
 * 分域一致的就地拦截）。
 * 值语义纯结构；线程安全。
 */
struct CanonicalLink {
    /// 连杆对象稳定身份（∈ header.objectRefs；全模型唯一）。
    core::ObjectId objectId;
    /// 权威局部名（解析字段——同 CanonicalJoint.localName 口径）。
    std::string localName;
    /// 视觉几何资源引用（可空；资源以**内容摘要**入身份、路径不入——§8.6；
    /// 引用须命中 resourceManifest（resourceId＋contentDigest 匹配））。
    std::optional<ResourceRef> visual;
    /// 碰撞几何资源引用（可空；约束同 visual）。
    std::optional<ResourceRef> collision;
    /// 连杆质量，单位 kg。Provided 时须有限且＞0（RT-CPX-3 口径）。
    core::SourcedValue<double> mass;
    /// 质心位置，连杆系下表示（单位 m；惯量基准＝质心/连杆参考姿态——M-2）。
    core::SourcedValue<rw::math::Vector3D<double>> centerOfMass;
    /// 惯量张量，质心系下表示（单位 kg·m²；MDL-05 惯量基准）。Provided 时
    /// 须对称（容差 1×10⁻¹²，附录 D 第 6 项）且正定（SPD）。
    core::SourcedValue<rw::math::InertiaMatrix<double>> inertia;
};

/**
 * @brief 规范工具（§4.3.4 tools 行——MDL-13：几何引用 ToolDefinition 资源
 *        不复制；首项语义见 CanonicalModelBuilder::setDefaultTcpIndex）。
 *
 * 与 ToolDescription（§4.2）的区别：几何已固化为内容绑定引用（ResourceRef），
 * 对象身份已锚定（工具对象 ObjectId 重复→构造拒绝）。
 * 值语义纯结构；线程安全。
 */
struct CanonicalTool {
    /// 工具对象稳定身份（∈ header.objectRefs；全模型唯一——工具间重复非法）。
    core::ObjectId objectId;
    /// 工具局部名（解析字段——空名/含 '/' 构造拒绝）。
    std::string localName;
    /// 工具几何资源引用（可空；须命中 resourceManifest——同连杆口径）。
    std::optional<ResourceRef> geometry;
    /// 工具质量，单位 kg。Provided 时须有限且＞0。
    core::SourcedValue<double> mass;
    /// 工具质心，工具系下表示（单位 m）。
    core::SourcedValue<rw::math::Vector3D<double>> centerOfMass;
    /// 工具惯量张量，质心系下表示（单位 kg·m²；约束同连杆惯量）。
    core::SourcedValue<rw::math::InertiaMatrix<double>> inertia;
    /**
     * 法兰→TCP 变换（T_flange_tcp，读法 core §4.6；KIN-14 默认 TCP 权威
     * 来源）。旋转须正交非反射、平移有限；平移单位 m。
     */
    rw::math::Transform3D<double> tcpOffset = detail::identityTransform3D();
};

/**
 * @brief 规范场景（环境）对象（§4.3.4 scene 行——MDL-15 世界系固连）。
 *
 * ★ worldPose 为**世界系**位姿——不得预乘安装旋转（§6.4 禁止项 3：环境
 *   固连世界系，只有机器人链带着 T_world_base）；该约束的编译期检测在
 *   S6/S9 一致性检查，本类型只承载合法变换值。
 * 值语义纯结构；线程安全。
 */
struct CanonicalSceneObject {
    /// 场景对象稳定身份（∈ header.objectRefs；全模型唯一）。
    core::ObjectId objectId;
    /// 对象局部名（解析字段——空名/含 '/' 构造拒绝）。
    std::string localName;
    /// 世界系固连位姿（T_world_scene；平移单位 m；正交非反射＋有限，否则
    /// 构造拒绝——RT-BW-6"场景位姿非有限"反例的字段）。
    rw::math::Transform3D<double> worldPose = detail::identityTransform3D();
    /// 环境几何资源引用（必有；须命中 resourceManifest）。
    ResourceRef geometry;
};

/**
 * @brief 规范传动块（§4.3.4 drivetrain 行——阶段 B 消费；MDL-21 矩阵为 R2）。
 *
 * 值语义纯结构；线程安全。
 */
struct CanonicalDrivetrain {
    /// 逐关节对角传动比（无量纲比值；OPT-02 StageB 连续变量）。向量长度
    /// 要么为 0（全缺省），要么＝joints.size()（逐关节口径）；Provided 项
    /// 须为正有限值。
    std::vector<core::SourcedValue<double>> ratioPerJoint;
    /// 线性耦合常矩阵（R2——MDL-21）。奇异/病态（κ＞1×10⁸，P-RT-7 设计
    /// 默认）的阻止判定归 S3 校验器（RT-CPL-1/RT-T03 交付）；build() 只
    /// 复核结构自洽（方阵、维度＝适用关节数、范围在链内）。
    std::optional<CouplingMatrix> coupling;
};

// =====================================================================
// 世界与基座块（§4.3.2——§6 详述的承载面）。
// =====================================================================

/**
 * @brief 世界与基座块（§4.3.2 WorldPlacement 字段表）。
 *
 * ★ T_world_base 是基座安装的**唯一存储位置**（§6.3 单字段；无第二副本）
 *   ——一切消费者（§6.4 四类）只经此字段读取，"二次叠加"即一致性检查失败
 *   （RT-BW-4）。基座姿态修改＝模型身份变化＝下游依赖变化（MDL-22/AT-05）。
 * 值语义纯结构；线程安全。
 */
struct WorldPlacement {
    /**
     * T_world_base：世界系→基座系变换（读法 core §4.6——基座相对世界）。
     * 默认＝平移 0、旋转 I（地面安装默认，V15-04）。合法：R 正交（逐元素
     * 容差 1×10⁻¹²）、det＞0、t 有限；非法（非正交/反射/非有限）→InputInvalid。
     * 平移单位 m。
     */
    rw::math::Transform3D<double> T_world_base = detail::identityTransform3D();
    /**
     * 安装预设（编辑表示/来源记录——§6.2；**不入内容身份**：T_world_base 已
     * 承载结果、§4.3.6"预设 token 不入身份"）。默认 NotProvided＝未显式配置
     * →按 Ground 解释（RT-BW-3 口径；presetToken() 便捷访问器承载该语义）。
     * 唯一构造拒绝面：preset=Custom 而 R≈I（§4.3.2——Custom 必须携带非恒等
     * 旋转，一致性校验失败）。
     */
    core::SourcedValue<InstallationPresetToken> installPreset;
    /**
     * 世界系重力加速度（单位 m/s²；世界系恒定——DYN-01/MDL-22 口径；写入以
     * 显式值为准，不沿用 RobWork 默认——§8.2 显式设值纪律）。默认
     * (0,0,−9.81)。非法（全零/非有限）→构造拒绝。
     */
    rw::math::Vector3D<double> gravityWorld{0.0, 0.0, -9.81};

    /**
     * @brief 安装预设 token（RT-BW-3 语义：NotProvided→Ground——未显式配置
     *        即地面安装，V15-04）。
     * @return 显式提供的 token 或 Ground（缺省解释；本函数不抛、无副作用）
     */
    InstallationPresetToken presetToken() const noexcept
    {
        const auto v = installPreset.tryValue();
        return v.has_value() ? *v : InstallationPresetToken::Ground;
    }
};

// =====================================================================
// 身份与来源块、机器人链块（§4.3.1/§4.3.3）。
// =====================================================================

/**
 * @brief 身份与来源块（§4.3.1 CanonicalModelHeader 字段表）。
 *
 * 身份语义（进入 RT-Codec 身份域的字段）：project/branch/revision（来源
 * 定位）、objectRefs（经内容身份间接——参与编码）、descriptionContractVersion、
 * compilerContractVersion、builtFrom。**revisionSeq 不入身份**（仅展示排序；
 * 内容身份不依赖修订序号——CON-05 精神、§4.5 排除清单）。
 * 值语义纯结构；线程安全。
 */
struct CanonicalModelHeader {
    /// 项目身份（来源定位三元组之一；空 id→构造拒绝）。
    core::ProjectId project;
    /// 方案分支身份（空 id→构造拒绝）。
    core::BranchId branch;
    /// 修订身份（一次命令提交＝一个修订，ARC-01；闭包内修订——闭包成员
    /// 判定归 S1/RT-T11，值层面仅拒绝空 id）。
    core::RevisionId revision;
    /// 修订序号（单调递增，project 侧分配；无单位；≥0）。仅展示排序——
    /// **不入内容身份**（§4.3.1 身份列"不入"）。
    std::uint64_t revisionSeq = 0;
    /// 修订可见对象引用清单（≥1；每 (oid,cv) 经 objectInRevision 校验——
    /// CM-0，判定归 S2/RT-T11；编码时按 ObjectId 规范文本字典序排序——
    /// §4.5 集合稳定键）。
    std::vector<ObjectRefEntry> objectRefs;
    /// 编译输入契约版本（reader 输出侧；进入编译缓存键 §9.4；合法 ≥1）。
    std::uint32_t descriptionContractVersion = 0;
    /// 本编译器契约版本（进入缓存键与 evidence 复现块；合法 ≥1）。
    std::uint32_t compilerContractVersion = 0;
    /// RobotDesignDescription 的 canonical 字节摘要（reader 输出的身份——
    /// §4.3.1"Description canonical 字节摘要"；非零；由 S2 链计算后传入）。
    core::Digest256 builtFrom{};
};

/**
 * @brief 机器人链块（§4.3.3 RobotChain 字段表）。
 *
 * 串联结构：joints 按基座→法兰链序；links.size()==joints.size()+1（含基座
 * 连杆，下标 i 的连杆是关节 i 的父体）。deviceName 由编译器按 §7 生成规则
 * 写入（S8 交叉校验一致性——映射一致性检查归 RT-T05/S8，本类型只承载
 * 非空合法名）。
 * 值语义纯结构；线程安全。
 */
struct RobotChain {
    /// 机器人对象稳定身份（∈ header.objectRefs；规范对象身份锚）。
    core::ObjectId robotObjectId;
    /// 机器人局部名（进入名称映射设备作用域，MDL-14；非法：空/含 '/'）。
    std::string robotLocalName;
    /// RobWork Device 名（名称端口生成物——§7 生成规则输出；S8 与映射
    /// 交叉校验一致性，本类型承载值本身）。
    std::string deviceName;
    /// 规范关节链（基座→法兰串联序；≥1——空链→构造拒绝）。
    std::vector<CanonicalJoint> joints;
    /// 规范连杆集合（数量＝joints.size()+1；失配→StructureInvalid）。
    std::vector<CanonicalLink> links;
};

// =====================================================================
// RuntimeCapability（§9.6——能力声明：内容的派生投影，不入身份，D-12）。
// =====================================================================

/**
 * @brief 运行时能力声明（§9.6 原文契约——字段与派生规则）。
 *
 * 背景（为什么冗余存储派生量）：能力是内容的**派生投影**（由链/工具/场景/
 * 传动字段推导），写入模型仅为下游 O(1) 直查（dynamics 查 hasDynamicWorkCell、
 * kinematics 查限速位等）；一致性由 CanonicalModelBuilder 从内容**派生**
 * 保证（§4.3.5"与字段一致性由构造器校验"——派生强于拒绝：不一致状态在
 * 类型层不可构造）。能力不入内容身份（D-12——身份＝内容；同输入同能力由
 * 确定性派生间接保证）。
 *
 * 语义边界（§5.6/§9.6 正交表）：能力缺失（NotProvided→位 false＋警告级
 * 诊断）不是输入非法、不是编译失败——缺失不妨碍发布，失败不发布；下游
 * 按声明自行处置（dynamics→DYN-06 DataInsufficient 域判定；runtime 绝不
 * 把能力缺失升级为工程不可行）。
 *
 * 派生规则细则（deriveRuntimeCapability 实现，逐位出处）：
 *   - hasFullMassInertia：全部连杆＋全部工具的 mass/centerOfMass/inertia
 *     均 Provided（§9.6"全连杆＋工具物性齐备"）；
 *   - hasDynamicWorkCell：全部连杆物性 Provided（§9.6"全部被消费 Body 物性
 *     provided"——DWC Body 集合＝链连杆；工具以工具系/RigidDevice 层消费，
 *     其物性缺项不阻止 DWC 构造。S7 实际构造成功与否的最终落位在 RT-T08，
 *     若有出入按 DTB §5.4 登记对齐）；
 *   - hasJointVelocityLimits：全部关节 maxVelocity Provided（§9.6 字面）；
 *   - hasCollisionGeometry：任一连杆 collision / 任一工具 geometry /
 *     场景非空（§9.6"≥1 连杆/工具/场景 collision 几何"——场景对象的几何
 *     即碰撞障碍物）；
 *   - hasFrictionModel：全部关节摩擦三元组均 Provided（DYN-06 消费）；
 *   - hasCouplingMatrix：drivetrain.coupling 存在（"良态"已由 S3 判定）。
 *
 * 值语义纯结构；线程安全。
 */
struct RuntimeCapability {
    bool hasWorkCell = true;   ///< 恒 true（无 WC 即无快照——§9.6 字面）
    bool hasDynamicWorkCell = false;  ///< 全部被消费 Body 物性 provided 才 true（§5.2 S7）
    bool hasFullMassInertia = false;  ///< 全连杆＋工具物性齐备
    bool hasJointVelocityLimits = false;  ///< 全关节 maxVelocity provided
    bool hasCollisionGeometry = false;    ///< ≥1 连杆/工具/场景 collision 几何
    bool hasTools = false;                ///< tools 非空
    bool hasScene = false;                ///< scene 非空
    bool hasFrictionModel = false;        ///< 全关节摩擦 provided（dynamics DYN-06 消费）
    bool hasCouplingMatrix = false;       ///< drivetrain.coupling 存在且良态（R2，MDL-21）
    /// 链上关节类型（按链序；含 Continuous 类型保留事实——V12-01）。
    std::vector<JointType> jointTypesPresent;
    bool hasBidirectionalNameMap = true;  ///< 恒 true（映射随快照必建且双射）
};

// =====================================================================
// CanonicalModel 本体（§4.3——不可变值模型；唯一构造路径＝CanonicalModelBuilder）。
// =====================================================================

/**
 * @brief 规范模型（§4.3——从修订对象闭包确定性编译出的不可变 SE(3) 关节链）。
 *
 * 生命周期：瞬态值对象——由 CanonicalModelBuilder::build() 构造，随
 * RuntimeSnapshot 以值持有并共享（下游只拿到 const 引用或快照 shared_ptr）；
 * **不持久化**（决策 D-01：持久化会引入第二真值风险——ARCH §7.3 三段链
 * 语义＋MDL-06；可经 rtcodec 序列化为瞬态字节供 worker 物化，不入项目
 * 存储格式）。
 *
 * 可变性：构造后无 setter（公共面全 const 访问器）；值语义拷贝/移动。
 * 线程安全：构造完成后并发只读安全。
 *
 * 内容身份：contentIdentity()＝SHA-256 over 身份域编码（§4.5——排除
 * diagnostics/capabilities/contentIdentity 自身/revisionSeq/installPreset；
 * 摘要只经 core::ContentDigester——CR-02），由 builder 计算，非调用方申报。
 */
class CanonicalModel {
public:
    /// 对象类别（§4.6 对象索引的 kind 轴——名称映射与诊断定位的基础）。
    enum class ObjectKind {
        Robot,       ///< 机器人本体（链锚对象）
        Joint,       ///< 规范关节（index＝链序下标）
        Link,        ///< 规范连杆（index＝链序下标）
        Tool,        ///< 规范工具（index＝tools() 下标）
        SceneObject, ///< 场景对象（index＝scene() 下标）
        Resource,    ///< 资源对象（index＝resourceManifest() 下标）
    };

    /// 对象索引条目（§4.6——ObjectId→{kind, 链上位置}）。
    struct ObjectLocation {
        ObjectKind kind = ObjectKind::Robot; ///< 对象类别
        std::uint32_t index = 0;             ///< 所在集合的下标（Robot 恒 0）
    };

    // ---- 只读访问器（构造后不变；命名＝§4.3 字段表行名）----

    /// 身份与来源块（§4.3.1）。
    const CanonicalModelHeader& header() const noexcept { return m_header; }
    /// 世界与基座块（§4.3.2——T_world_base 唯一存储位置）。
    const WorldPlacement& world() const noexcept { return m_world; }
    /// 机器人链块（§4.3.3）。
    const RobotChain& chain() const noexcept { return m_chain; }
    /// 工具集合（§4.3.4；已按 ObjectId 规范文本字典序规范化）。
    const std::vector<CanonicalTool>& tools() const noexcept { return m_tools; }
    /// 默认 TCP 下标（KIN-14；tools 非空时必有值且指向 tools 内项）。
    const std::optional<std::uint32_t>& defaultTcpIndex() const noexcept { return m_defaultTcpIndex; }
    /// 场景对象集合（§4.3.4；已按 ObjectId 规范文本字典序规范化）。
    const std::vector<CanonicalSceneObject>& scene() const noexcept { return m_scene; }
    /// 传动块（§4.3.4）。
    const CanonicalDrivetrain& drivetrain() const noexcept { return m_drivetrain; }
    /// 资源清单（§4.3.5；已按 resourceId 规范文本字典序规范化）。
    const std::vector<ResourceRef>& resourceManifest() const noexcept { return m_resourceManifest; }
    /// 诊断块（§4.3.5——仅警告级；过程记录，不入身份）。
    const std::vector<core::DiagnosticRecord>& diagnostics() const noexcept { return m_diagnostics; }
    /// 能力声明（§9.6——由内容派生，与字段一致性由构造保证）。
    const RuntimeCapability& capabilities() const noexcept { return m_capabilities; }
    /// 内容身份（§4.3.5——SHA-256 over 身份域编码；builder 计算非申报）。
    const core::ContentIdentity& contentIdentity() const noexcept { return m_contentIdentity; }

    // ---- §4.6 只读索引查询（builder 构建；O(log n)）----

    /**
     * @brief 对象索引查询（§4.6 索引 1——ObjectId→{kind, 链上位置}）。
     * @param id [in] 目标对象稳定身份
     * @return 命中＝类别与集合下标；未命中＝nullopt（不抛、并发安全）
     */
    std::optional<ObjectLocation> findObject(const core::ObjectId& id) const
    {
        const auto it = m_objectIndex.find(id);
        if (it == m_objectIndex.end()) { return std::nullopt; }
        return it->second;
    }

    /**
     * @brief 资源索引查询（§4.6 索引 3——resourceId→ResourceRef；§8.6 资源
     *        复查的枚举来源）。
     * @param resourceId [in] 资源对象身份
     * @return 命中＝清单内 ResourceRef 副本；未命中＝nullopt
     */
    std::optional<ResourceRef> findResource(const core::ObjectId& resourceId) const
    {
        const auto it = m_resourceIndex.find(resourceId);
        if (it == m_resourceIndex.end()) { return std::nullopt; }
        return m_resourceManifest.at(it->second);
    }

    /**
     * @brief 模型等值（§4.3.6"字节相同"等价关系的值面投影）。
     *
     * 身份域字段按 IEEE754 位模式逐字段比较（⟺ encodeIdentityDomain 字节
     * 等值——RT-ID-2 同口径）；诊断/能力块按逐字段/core 精确等值（不入身份
     * 域——D-12）。确定性纯函数、不抛。
     */
    bool operator==(const CanonicalModel& o) const noexcept;
    /// 不等＝非全字段等值（见 operator==）。
    bool operator!=(const CanonicalModel& o) const noexcept { return !(*this == o); }

private:
    // CanonicalModelBuilder 是唯一构造路径（嵌套访问：friend 声明允许其填写
    // 私有成员——构造完成后该路径即关闭，"构造后无 setter"由此成立）。
    friend class CanonicalModelBuilder;

    /// 默认构造（私有）——全成员取各结构默认值；仅 CanonicalModelBuilder 可达。
    CanonicalModel() = default;

    // ---- 字段（声明序＝RT-Codec 全字段编码序；注释详见各结构定义）----
    CanonicalModelHeader m_header;             ///< §4.3.1 身份与来源块
    WorldPlacement m_world;                    ///< §4.3.2 世界与基座块
    RobotChain m_chain;                        ///< §4.3.3 机器人链块
    std::vector<CanonicalTool> m_tools;        ///< §4.3.4 工具集合（规范化序）
    std::optional<std::uint32_t> m_defaultTcpIndex; ///< §4.3.4 默认 TCP
    std::vector<CanonicalSceneObject> m_scene; ///< §4.3.4 场景集合（规范化序）
    CanonicalDrivetrain m_drivetrain;          ///< §4.3.4 传动块
    std::vector<ResourceRef> m_resourceManifest; ///< §4.3.5 资源清单（规范化序）
    std::vector<core::DiagnosticRecord> m_diagnostics; ///< §4.3.5 诊断块（警告级）
    RuntimeCapability m_capabilities;          ///< §9.6 能力声明（派生投影）
    core::ContentIdentity m_contentIdentity;   ///< §4.3.5 内容身份（builder 计算）

    // ---- §4.6 只读索引（builder 构建；键比较用 core 强类型的字节字典序）----
    std::map<core::ObjectId, ObjectLocation> m_objectIndex;   ///< ObjectId→位置
    std::map<core::ObjectId, std::uint32_t> m_resourceIndex;  ///< resourceId→清单下标
};

// =====================================================================
// 能力派生纯函数（§9.6——builder 与测试共用同一实现，规则见结构体注释）。
// =====================================================================

/**
 * @brief 从模型内容派生能力声明（§9.6 派生规则；纯函数、不抛、确定性）。
 *
 * @param chain      [in] 机器人链块（连杆物性/关节限速/摩擦/类型序列来源）
 * @param tools      [in] 工具集合（物性/几何来源）
 * @param scene      [in] 场景集合（碰撞几何来源）
 * @param drivetrain [in] 传动块（耦合矩阵存在性来源）
 * @return 与内容一致的能力声明（hasWorkCell/hasBidirectionalNameMap 恒 true）
 *
 * 确定性：只读输入的纯投影——同内容同结果（NFR-COR-02）；无环境/时钟依赖。
 * 复杂度：O(连杆＋工具＋关节)。
 */
RuntimeCapability deriveRuntimeCapability(const RobotChain& chain,
                                          const std::vector<CanonicalTool>& tools,
                                          const std::vector<CanonicalSceneObject>& scene,
                                          const CanonicalDrivetrain& drivetrain);

// =====================================================================
// CanonicalModelBuilder（§3.1 模块表命名——S5 构造不变量的唯一执行点）。
// =====================================================================

/**
 * @brief CanonicalModel 的唯一构造入口（§4.3 结构级约定"不可变（无 setter，
 *        构造后只读）"的构造侧承载）。
 *
 * 职责（build() 内按序执行，任一违约抛 RuntimeError fail-fast——调用方
 * 错误语义：编译链 S1～S3 已拦截的输入若仍带病到达 S5，属调用方契约违约，
 * 不产出半成品模型）：
 *   1. 值级不变量校验（§4.3 各字段表"合法与非法实例"列——空 id、契约版本、
 *      正交/反射、有限性、qmin＜qmax、Provided 物性为正、SPD、引用∈清单、
 *      全模型 ObjectId 唯一、资源摘要非零、诊断无 error 级稳定码等）；
 *   2. 确定性规范化（§4.5 集合稳定键）：objectRefs/tools/scene/resourceManifest
 *      按 ObjectId 规范文本字典序排序（关节/连杆保持链序不排序）；关节轴
 *      向单位化（§4.3.3"编译器规格化"）；
 *   3. 能力派生（deriveRuntimeCapability——§9.6）；
 *   4. §4.6 索引构建（对象/资源）；
 *   5. 内容身份计算（rtcodec::computeContentIdentity——CR-02：摘要只调
 *      core ContentDigester；排除字段声明在 Codec.hpp）。
 *
 * 用法（阶段 A 夹具直构；RT-T11 编译器 S5 段同款调用）：
 * @code
 *   CanonicalModel m = CanonicalModelBuilder()
 *       .setHeader(h).setWorld(w).setChain(c).setTools(ts)
 *       .setDefaultTcpIndex(0).setScene(sc).setDrivetrain(dt)
 *       .setResourceManifest(rm).setDiagnostics(diag)
 *       .build();
 * @endcode
 *
 * 线程安全：非线程安全——每个 builder 实例仅限构造线程使用（共享状态纪律
 * 随 AGENTS.md 错误语义表）；build() 为 const（可重复调用，每次产出独立
 * 等值模型——重复构造稳定性 RT-ID-1 的模型层基础）。
 */
class CanonicalModelBuilder {
public:
    /// 设置身份与来源块（§4.3.1；拷贝入 builder——调用方持有原件）。
    CanonicalModelBuilder& setHeader(const CanonicalModelHeader& header)
    {
        m_header = header;
        return *this;
    }

    /// 设置世界与基座块（§4.3.2）。
    CanonicalModelBuilder& setWorld(const WorldPlacement& world)
    {
        m_world = world;
        return *this;
    }

    /// 设置机器人链块（§4.3.3）。
    CanonicalModelBuilder& setChain(const RobotChain& chain)
    {
        m_chain = chain;
        return *this;
    }

    /// 设置工具集合（§4.3.4；build 时按 ObjectId 规范文本字典序规范化）。
    CanonicalModelBuilder& setTools(std::vector<CanonicalTool> tools)
    {
        m_tools = std::move(tools);
        return *this;
    }

    /// 设置默认 TCP 下标（§4.3.4 defaultTcp 行——tools 非空时必填且指向
    /// tools 内项；tools 为空时必须为 nullopt）。
    CanonicalModelBuilder& setDefaultTcpIndex(std::optional<std::uint32_t> index)
    {
        m_defaultTcpIndex = index;
        return *this;
    }

    /// 设置场景对象集合（§4.3.4；build 时按 ObjectId 规范文本字典序规范化）。
    CanonicalModelBuilder& setScene(std::vector<CanonicalSceneObject> scene)
    {
        m_scene = std::move(scene);
        return *this;
    }

    /// 设置传动块（§4.3.4）。
    CanonicalModelBuilder& setDrivetrain(const CanonicalDrivetrain& drivetrain)
    {
        m_drivetrain = drivetrain;
        return *this;
    }

    /// 设置资源清单（§4.3.5；build 时按 resourceId 规范文本字典序规范化）。
    CanonicalModelBuilder& setResourceManifest(std::vector<ResourceRef> manifest)
    {
        m_resourceManifest = std::move(manifest);
        return *this;
    }

    /// 设置诊断块（§4.3.5——仅警告级；error 级稳定码出现→构造拒绝）。
    CanonicalModelBuilder& setDiagnostics(std::vector<core::DiagnosticRecord> diagnostics)
    {
        m_diagnostics = std::move(diagnostics);
        return *this;
    }

    /**
     * @brief 校验＋规范化＋派生＋索引＋身份计算，产出不可变模型。
     *
     * @return 构造完成的 CanonicalModel（contentIdentity 非零；值语义独立）
     *
     * @throws RuntimeError 值级不变量违约（code＝InputInvalid［数值/变换/
     *         契约字段非法］或 StructureInvalid［结构/引用/唯一性违约］，
     *         detail 中文定位到字段——§5.2 S5"构造不变量"归属；不返回半成品）
     *
     * 确定性：同输入重复调用产出 operator== 相等且编码逐字节相等的模型
     * （RT-ID-1 模型层——排序/单位化/派生/摘要全链无环境依赖）。
     */
    CanonicalModel build() const;

private:
    // ---- 输入部件（与 setter 一一对应）----
    CanonicalModelHeader m_header;
    WorldPlacement m_world;
    RobotChain m_chain;
    std::vector<CanonicalTool> m_tools;
    std::optional<std::uint32_t> m_defaultTcpIndex;
    std::vector<CanonicalSceneObject> m_scene;
    CanonicalDrivetrain m_drivetrain;
    std::vector<ResourceRef> m_resourceManifest;
    std::vector<core::DiagnosticRecord> m_diagnostics;
};

}  // namespace sdurws::ird::runtime

#endif  // SDURWS_IRD_RUNTIME_CANONICALMODEL_HPP
