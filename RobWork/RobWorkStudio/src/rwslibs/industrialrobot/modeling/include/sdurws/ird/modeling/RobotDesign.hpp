/**
 * @file   RobotDesign.hpp
 * @brief  RobotDesign 聚合值模型——权威参数化的业务编辑模型（§4.3 根对象
 *         字段表／§4.3-A 关节表／§4.3-B 连杆表）＋合法实例不变量层
 *         （I-MDL-1～I-MDL-12，§4.10）＋双权威编辑权限守卫（§7.3 C-1/C-2）。
 *
 * 设计依据：
 *   - units/modeling.md §4.2～§4.10（对象分解、根对象字段表、身份/版本/
 *     引用约束、三态辨析、不变量表）、§7.1～§7.3（双权威关系、参数来源
 *     表、冲突矩阵 C-1/C-2 与非法组合清单）、§3.3（公共头表 RobotDesign.hpp
 *     行："RobotDesign 聚合值模型（关节/连杆/基座/权威模式/引用表）、
 *     合法实例不变量"）、§14.2 D-MDL-1/D-MDL-2/D-MDL-5
 *   - units/core.md §4.3（SourcedValue 四态/ValueProvenance——经 core 公共
 *     头消费）、§4.6（T_ab 位姿约定："b 系相对 a 系"）
 *   - units/runtime.md §4.2（InstallationPresetToken/BasePlacement 编辑
 *     表示——预设轴向以 runtime installationPresetRotation() 为唯一权威
 *     产出点，P-RT-4；modeling 只存参数不存矩阵）
 *   - 需求 MDL-01（参数化建模）、MDL-02（双权威互斥）、MDL-09（显式参数
 *     权威一等字段）、ARC-04（对象身份跨修订稳定）、CON-01（身份/版本
 *     包络）、NFR-COR-03（非法值不静默修复）
 *   - 任务契约 tasks/foundation/WP-13-T03.json acceptance 1/2/3/5
 *
 * 背景说明（本类型在产品里的位置）：RobotDesign 是七阶段工作流第一阶段
 * 的**权威参数化业务编辑模型**（modeling.md §4.1 三形态链的第一段）——
 * 它既不是内存第二真值（编辑态是"已应用基线＋未应用编辑差值"的演算
 * 视图），也不是 runtime 消费的 CanonicalModel（那是编译派生物）。本头
 * 只承载值模型与合法性判定：编辑器（T08）、DH 转换（T09）、reader 映射
 * （T12）都在这组类型上工作。
 *
 * 双权威语义（MDL-02/09，§7.1 原文纪律）：authority 单一开关裁定
 * axis/origin 与 DhParameters 哪一侧是权威——Explicit 态 axis/origin 可
 * 编辑、dhDerived 只是展示用派生缓存；StandardDH 态正好相反。**派生侧
 * 不入 canonical 编码身份（D-MDL-5）**：编码只含权威字段＋模式开关，
 * 派生值在读取时按需确定性重算（重算函数随 T09 IDhExplicitConverter 落位）。
 *
 * 所有权/生命周期：全部为纯值类型（无堆共享、无回调）——随编辑器工作集/
 * 草稿载荷/编解码返回值按值持有；SourcedValue 内部经 optional 承载载荷，
 * 拷贝语义平凡。线程安全：纯值类型不可变共享安全；**有意的可变编辑**
 * （改字段、push_back）不是线程安全的——编辑态只允许 UI 线程访问
 * （§3.4 总约定 2，TASK-02 语义外的 UI 约束）。
 * 确定性（NFR-COR-01/02）：本头全部函数为纯函数——不读时钟/环境/locale，
 * 同输入同输出；校验与 token 转换与浮点运算次序固定，同对象重跑逐字节
 * 一致（canonical 编码见 Codec.hpp）。
 */

#ifndef IRD_MODELING_ROBOTDESIGN_HPP
#define IRD_MODELING_ROBOTDESIGN_HPP

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <rw/math/Rotation3D.hpp>    // Rotation3D<double>——Transform3D 位姿的旋转半部（显式包含：头内默认成员初始化直接构造）
#include <rw/math/Transform3D.hpp>   // rw::math::Transform3D<double>——T_ab 位姿（core.md §4.6 约定）
#include <rw/math/Vector3D.hpp>      // rw::math::Vector3D<double>——轴向/位置/欧拉轴角

#include <sdurws/ird/core/Digest.hpp>      // Digest256/ContentVersion（资源摘要/固化引用）
#include <sdurws/ird/core/Identity.hpp>    // ObjectId（对象/关节/连杆稳定身份）
#include <sdurws/ird/core/Provenance.hpp>  // SourcedValue/ValueProvenance/ProvenanceKind
#include <sdurws/ird/modeling/Errors.hpp>  // ModelingError/ModelingErrorCode（错误面）
#include <sdurws/ird/modeling/ObjectTypes.hpp>  // kRobotDesignSchemaVersion 等——schema 主版本单一权威（本头字段缺省值引用，禁写字面量）
#include <sdurws/ird/runtime/Description.hpp>  // runtime::InstallationPresetToken（预设词表——单一权威）

namespace sdurws::ird::modeling {

// =====================================================================
// 基础枚举与轻量值类型（§4.3 字段表序）
// =====================================================================

/**
 * @brief 双权威模式开关（§4.3 authority 字段；MDL-02 互斥）。
 *
 * 枚举顺序＝卡面词序（Explicit|StandardDH）；持久化契约面纪律：只允许
 * 表尾追加并走单元卡增量修订（数值进入编解码 wire format，不重排）。
 */
enum class AuthorityMode {
    Explicit,    ///< 显式参数权威：axis/origin 为可编辑权威一等字段（MDL-09）
    StandardDH,  ///< 标准 DH 权威：DhParameters 权威，axis/origin 派生只读
};

/**
 * @brief 权威模式稳定 token（"Explicit"/"StandardDH"——诊断与编码承载）。
 * @param mode [in] 权威模式（全枚举值均有 token——switch 全枚举无 default，
 *              新增值漏登记时编译器告警暴露）
 * @return 静态存储期串。纯函数；线程安全；确定性（NFR-COR-02）。
 */
std::string_view authorityModeToken(AuthorityMode mode) noexcept;

/**
 * @brief 关节类型（§4.3-A type 字段词表四值；runtime Description 同词表）。
 *
 * 枚举顺序＝卡面词序（Revolute|Continuous|Prismatic|Fixed）；表尾追加纪律
 * 同 AuthorityMode。R1 正式链限全旋转（§6.4）是导入/模板层的建链规则，
 * 不是类型词表本身的约束——Fixed 可存在于链上（I-MDL-1）。
 */
enum class JointType {
    Revolute,    ///< 旋转关节（有限限位——bounds 必填态见 I-MDL-4）
    Continuous,  ///< 连续旋转（无限位；工程工作范围 workingRange 专用）
    Prismatic,   ///< 移动关节（限位单位 m）
    Fixed,       ///< 固定连接（无可动轴；axis/origin 语义见字段表注）
};

/**
 * @brief 关节类型稳定 token（"Revolute"/"Continuous"/"Prismatic"/"Fixed"）。
 * @param type [in] 关节类型（switch 全枚举，同上）
 * @return 静态存储期串。纯函数；线程安全；确定性。
 */
std::string_view jointTypeToken(JointType type) noexcept;

/**
 * @brief 关节限位/工程工作范围对（rad 或 m——随关节类型；见各字段注）。
 *
 * first＝qmin、second＝qmax（I-MDL-4：有限限位可动关节 qmin<qmax）。
 * pair 而非自定义结构：字段表原文即 {qmin,qmax} 两元，且 pair 的字典序/
 * 相等语义正是本场景语义——不再包一层同名结构（无信息增益）。
 */
using JointLimits = std::pair<double, double>;

/**
 * @brief 标准 DH 参数四元组（§4.3-A dhDerived／§7.4 约定：标准/远置）。
 *
 * 连杆变换（§7.4 原文）：T_{i-1,i} = Rot_z(θᵢ+qᵢ)·Trans_z(dᵢ)·Trans_x(aᵢ)·Rot_x(αᵢ)。
 *
 * 归属说明（卡片 §3.3 DhConvert.hpp 行"DH 参数结构"的落位选择）：该结构
 * 首次被 §4.3-A JointEntry.dhDerived 字段需要（T03 schema），故定义于本头；
 * T09 的 DhConvert.hpp 直接复用本定义（含注释出处），不另立第二 DH 结构——
 * 两处各写一份会在转换边界引入字段序漂移风险（偏差登记见单元卡 §15 增量）。
 *
 * 单位：thetaOffset/alpha 为 rad（θ 定义为**含 zeroOffset 偏置前**的几何
 * 参数——零位语义由 zeroOffset 承载，§7.4"θ_offset 与 zeroOffset 显式分离"）；
 * d/a 为 m。全部字段必须有限（I-MDL-3）。
 */
struct DhParameters {
    double thetaOffset = 0.0;  ///< θᵢ 偏置，单位 rad（DH 态下为权威可编辑）
    double d = 0.0;            ///< 连杆偏距，单位 m
    double a = 0.0;            ///< 连杆长度，单位 m
    double alpha = 0.0;        ///< 扭转角，单位 rad

    bool operator==(const DhParameters& o) const noexcept
    {
        return thetaOffset == o.thetaOffset && d == o.d && a == o.a && alpha == o.alpha;
    }
    bool operator!=(const DhParameters& o) const noexcept { return !(*this == o); }
};

/**
 * @brief 几何引用类别（§4.3-B shape 行 kind 词表两值）。
 */
enum class GeometryKind {
    Mesh,       ///< 网格（经 resourceManifest 资源承载）
    Primitive,  ///< 解析图元（圆柱/盒等——经 resourceManifest 或参数承载）
};

/**
 * @brief 几何类别稳定 token（"Mesh"/"Primitive"）。纯函数；确定性。
 */
std::string_view geometryKindToken(GeometryKind kind) noexcept;

/**
 * @brief 几何引用（§4.3-B LinkEntry.shape／§4.4 ToolDefinition.geometry／
 *        §4.5 SceneObject.geometry 共用形态）。
 *
 * resourceRefId 指向根对象 resourceManifest 的 resourceId（I-MDL-10 状态机
 * 的载体清单）——几何本体不内嵌对象字节（对象字节只存引用，CON-03 资源
 * 三段边界）。
 */
struct GeometryRef {
    /// 指向 resourceManifest[].resourceId 的引用键（模型内作用域；非 ObjectId
    /// ——资源清单以字符串 id 编址，见 §4.3 resourceManifest 行）。
    std::string resourceRefId;

    /// 几何局部位姿 T_link_geom＝"geom 系相对 link 系"（m/rad；core.md §4.6）。
    rw::math::Transform3D<double> localTransform{
        rw::math::Vector3D<double>(0.0, 0.0, 0.0),
        rw::math::Rotation3D<double>(1.0, 0.0, 0.0,
                                     0.0, 1.0, 0.0,
                                     0.0, 0.0, 1.0)};

    GeometryKind kind = GeometryKind::Mesh;  ///< 几何类别（词表两值）

    bool operator==(const GeometryRef& o) const;
    bool operator!=(const GeometryRef& o) const { return !(*this == o); }
};

/**
 * @brief 惯量张量六分量（§4.3-B body.inertia 行）。
 *
 * 基准＝**质心**、参考姿态＝**连杆坐标系**（M-2；§5.3 平行轴确认流的
 * 前提）。六分量表示天然对称（ixy/ixz/iyz 各存一份）——I-MDL-5 的"对称
 * （相对 1×10⁻¹²）"检查由表示层结构性满足，不重复数值校验（见
 * checkInvariants 实现注释）。
 *
 * 单位：主对角 kg·m²，惯量积 kg·m²。
 */
struct InertiaTensor {
    double ixx = 0.0;  ///< 绕 x 轴主项，kg·m²
    double iyy = 0.0;  ///< 绕 y 轴主项，kg·m²
    double izz = 0.0;  ///< 绕 z 轴主项，kg·m²
    double ixy = 0.0;  ///< xy 惯量积，kg·m²
    double ixz = 0.0;  ///< xz 惯量积，kg·m²
    double iyz = 0.0;  ///< yz 惯量积，kg·m²

    bool operator==(const InertiaTensor& o) const noexcept
    {
        return ixx == o.ixx && iyy == o.iyy && izz == o.izz
            && ixy == o.ixy && ixz == o.ixz && iyz == o.iyz;
    }
    bool operator!=(const InertiaTensor& o) const noexcept { return !(*this == o); }
};

/**
 * @brief 材料引用（§4.3-B body.material 行：{materialId, densitySourced}）。
 *
 * materialId＝材料表键（词表随物性估算唯一公式表 mdl-property-formula/1
 * 落位——T04）；density 为估算输入（kg/m³）。
 */
struct MaterialRef {
    std::string materialId;  ///< 材料表键（模型内/公式表作用域）
    /// 参考密度，单位 kg/m³（估算输入 §5.3；SourcedValue——来源可溯）。
    core::SourcedValue<double> density;

    bool operator==(const MaterialRef& o) const
    {
        return materialId == o.materialId && density == o.density;
    }
    bool operator!=(const MaterialRef& o) const { return !(*this == o); }
};

/**
 * @brief 连杆体物性组（§4.3-B body 列；§4.4 ToolDefinition 复用同形组）。
 *
 * 全部 SourcedValue：缺失＝NotProvided——不触发断言，走 DataInsufficient
 * 降级（MDL-06/V15-01；DYN-06 闭环）。已提供值受 I-MDL-3（有限）与
 * I-MDL-5（m>0；惯量 SPD＋三角不等式）约束。
 */
struct BodyData {
    /// 质量，单位 kg；已提供则 m>0（断言①）。
    core::SourcedValue<double> mass;
    /// 质心位置，单位 m，连杆坐标系下表示；修改触发平行轴确认流（§5.3，M-2）。
    core::SourcedValue<rw::math::Vector3D<double>> centerOfMass;
    /// 惯量张量（质心基准、连杆系参考姿态），kg·m²；断言②③见 I-MDL-5。
    core::SourcedValue<InertiaTensor> inertia;
    /// 材料引用（§4.3-B body.material 行；估算输入 §5.3）。
    std::optional<MaterialRef> material;

    bool operator==(const BodyData& o) const;
    bool operator!=(const BodyData& o) const { return !(*this == o); }
};

// =====================================================================
// RobotDesign 根对象（§4.3 字段表——字段声明序＝表行序＝编解码字段序，
// Codec.hpp 依赖此一致性：调整顺序即破坏性变更，走单元卡增量修订）
// =====================================================================

/**
 * @brief 默认 TCP 引用（§4.3 defaultTcp 行：工具 ObjectId＋该工具 tcpList 键）。
 */
struct TcpRef {
    core::ObjectId toolOid;  ///< 指向 tool-definition 对象（须在 toolRefs 内——I-MDL-9）
    std::string tcpKey;      ///< 该工具 tcpList 中某条目的 key（存在性核查归就绪校验 L7）

    bool operator==(const TcpRef& o) const noexcept
    {
        return toolOid == o.toolOid && tcpKey == o.tcpKey;
    }
    bool operator!=(const TcpRef& o) const noexcept { return !(*this == o); }
};

/**
 * @brief 基座安装布置（§4.3 basePlacement 行；MDL-22）。
 *
 * ★ modeling 只存参数不存矩阵（§4.3 原文/P-RT-4）：预设轴向矩阵的唯一
 * 权威产出点＝runtime BaseWorldTransform.hpp（installationPresetRotation()/
 * rotationFromCustomEaa()）——本结构不缓存任何旋转矩阵（M-11 基座—世界
 * 单一不变量的 modeling 侧形态）。
 */
struct BasePlacement {
    /// 安装预设（四值词表——runtime::InstallationPresetToken 单一权威，直接复用）。
    runtime::InstallationPresetToken preset = runtime::InstallationPresetToken::Ground;

    /// custom 预设必填的旋转矢量（EAA：方向＝轴、模长＝角，单位 rad；
    /// runtime::rotationFromCustomEaa 的输入形态——I-MDL-7）。
    core::SourcedValue<rw::math::Vector3D<double>> customEaa;

    /// 基座位置，单位 m，世界坐标系下表示（runtime §4.3.2 base 同口径）。
    core::SourcedValue<rw::math::Vector3D<double>> basePosition;

    bool operator==(const BasePlacement& o) const;
    bool operator!=(const BasePlacement& o) const { return !(*this == o); }
};

/**
 * @brief 导入资源记录（外部引用记录，NFR-SEC-01 口径——路径只在此层出现，
 *        不入 CanonicalModel 身份，runtime §4.3.5 同口径）。
 */
struct ExternalResourceRecord {
    std::string absPath;         ///< 外部资源绝对路径（敏感值——入诊断前须经脱敏）
    core::Digest256 recordedDigest{};  ///< 登记时内容摘要（SHA-256 原始 32 字节）

    bool operator==(const ExternalResourceRecord& o) const noexcept
    {
        return absPath == o.absPath && recordedDigest == o.recordedDigest;
    }
    bool operator!=(const ExternalResourceRecord& o) const noexcept { return !(*this == o); }
};

/**
 * @brief 固化引用（Recorded→Solidified 后指向闭包内 resource 对象，CON-03）。
 */
struct SolidifiedRef {
    core::ObjectId objectId;               ///< 固化 resource 对象身份
    core::ContentVersion contentVersion;   ///< 固化内容版本（身份/版本包络 CON-01）

    bool operator==(const SolidifiedRef& o) const noexcept
    {
        return objectId == o.objectId && contentVersion == o.contentVersion;
    }
    bool operator!=(const SolidifiedRef& o) const noexcept { return !(*this == o); }
};

/**
 * @brief 资源状态（§4.3 resourceManifest 行 state 词表两值；I-MDL-10 状态机：
 *        只可 Recorded→Solidified，不可逆）。
 */
enum class ResourceState {
    Recorded,    ///< 已登记未固化（必须带 externalRecord——I-MDL-10）
    Solidified,  ///< 已固化入项目（必须带 solidifiedObject——I-MDL-10）
};

/**
 * @brief 资源状态稳定 token（"Recorded"/"Solidified"）。纯函数；确定性。
 */
std::string_view resourceStateToken(ResourceState state) noexcept;

/**
 * @brief 资源清单条目（§4.3 resourceManifest 行；几何/网格资源的登记表）。
 *
 * contentDigest 是身份要素（表行原文）；路径不是（§4.8"导入文件路径不是
 * 身份"）。状态机约束见 I-MDL-10。
 */
struct ResourceRef {
    std::string resourceId;            ///< 清单内唯一键（模型内作用域编址——GeometryRef 引用它）
    core::Digest256 contentDigest{};   ///< 内容摘要（身份要素；SHA-256 原始 32 字节）
    ResourceState state = ResourceState::Recorded;  ///< 状态机当前态
    std::optional<ExternalResourceRecord> externalRecord;  ///< Recorded 态必填
    std::optional<SolidifiedRef> solidifiedObject;         ///< Solidified 态必填

    bool operator==(const ResourceRef& o) const;
    bool operator!=(const ResourceRef& o) const { return !(*this == o); }
};

/**
 * @brief modeling 关节原点位姿值类型（T_parent_joint＝"joint 系相对
 *        parent 系"）。
 *
 * ★ 为什么不是直接用 rw::math::Transform3D<double>：SourcedValue 的四态
 * 工厂（notProvided/notApplicable）与默认构造必然**默认构造载荷**，而 rw
 * Transform3D 的默认构造引用框架库外联符号 Rotation3D::identity()
 * （Transform3D.hpp 第 70 行——符号实现于 sdurw_math）。独立冒烟模式无
 * 框架库可链（runtime RT-T03 起的冒烟 header-only 纪律——"冒烟可达代码
 * 不得调用 Rotation3D::identity() 等外联符号"），直接入 SourcedValue 会让
 * 全部消费 TU 带上无法解析的外部符号。本类型与 Transform3D 同构（平移
 * m＋3×3 旋转），默认构造走逐元素恒等（与 runtime
 * Description detail::identityTransform3D 同款纪律，逐位等价于 rw 恒等），
 * 与 Transform3D 双向转换——消费侧（reader 映射/编辑器）零语义差异。
 * 单元卡 §4.3-A origin 行的类型字面偏差随 §15 增量登记。
 */
class JointPose {
public:
    /// 默认＝恒等位姿（逐元素构造——零外联符号，冒烟两模式语义一致）。
    JointPose()
        : d_(0.0, 0.0, 0.0),
          r_(1.0, 0.0, 0.0,
             0.0, 1.0, 0.0,
             0.0, 0.0, 1.0) {}

    /// 由 rw 位姿转换（非 explicit——同构类型间转换不设摩擦）。
    JointPose(const rw::math::Transform3D<double>& t) : d_(t.P()), r_(t.R()) {}

    /// 转 rw 位姿（消费侧如 Description 映射——T_ab 语义原样传递）。
    operator rw::math::Transform3D<double>() const
    {
        return rw::math::Transform3D<double>(d_, r_);
    }

    /// 平移部分（m——joint 系相对 parent 系原点）。
    const rw::math::Vector3D<double>& d() const noexcept { return d_; }
    /// 旋转部分（3×3 正交阵——无量纲）。
    const rw::math::Rotation3D<double>& r() const noexcept { return r_; }

    /// 逐元素精确相等（附录 D 第 12 项：位姿身份比较无容差）。
    bool operator==(const JointPose& o) const noexcept
    {
        for (std::size_t i = 0; i < 3; ++i) {
            if (!(d_[i] == o.d_[i])) { return false; }
        }
        for (std::size_t row = 0; row < 3; ++row) {
            for (std::size_t col = 0; col < 3; ++col) {
                if (!(r_(row, col) == o.r_(row, col))) { return false; }
            }
        }
        return true;
    }
    bool operator!=(const JointPose& o) const noexcept { return !(*this == o); }

private:
    rw::math::Vector3D<double> d_;     ///< 平移（m）
    rw::math::Rotation3D<double> r_;   ///< 旋转（无量纲）
};

/**
 * @brief 关节条目（表 4.3-A；字段声明序＝表行序＝编解码字段序）。
 *
 * 双权威语义逐字段见各注（MDL-02/09）；authority 开关在根对象上，
 * 不在关节上——整链一个权威模式。
 */
struct JointEntry {
    /// 对象内稳定标识（创建时一次分配、跨修订稳定——ARC-04；不是独立存储
    /// 对象，O-36 裁决口径：诊断 subject 与名称映射锚，内嵌根对象字节）。
    core::ObjectId objectId;
    /// 运行时名称 localName 源（合法字符集 [A-Za-z0-9_.-] 以 runtime 消歧
    /// 规则兜底；改名＝新内容——runtime §4.3.6）。
    std::string localName;
    JointType type = JointType::Revolute;  ///< 关节类型（两模式均权威）

    /// 轴向，单位向量（无量纲；模长偏差按附录 D C7 以 1×10⁻¹² 无量纲绝对
    /// 容差归一化）。Explicit：可编辑权威一等字段（MDL-09）；StandardDH：
    /// 只读派生（DerivedReadOnly 来源；编解码后为 NotProvided 待重算——D-MDL-5）。
    core::SourcedValue<rw::math::Vector3D<double>> axis;

    /// 关节原点位姿 T_parent_joint＝"joint 系相对 parent 系"（m/rad）。
    /// 权威/派生语义同 axis。载荷类型 JointPose＝Transform3D 同构值类型
    /// （冒烟 header-only 纪律——见 JointPose 类注；单元卡 §15 增量登记）。
    core::SourcedValue<JointPose> origin;

    /// 零位偏置：转动 rad／移动 m；q_authoritative = q_zeroOffset + q_rw
    /// （runtime 口径）。两模式均为权威（零位语义不随权威模式切换）。
    double zeroOffset = 0.0;

    /// 限位 {qmin,qmax}：rad（转动）／m（移动）。Revolute/Prismatic 必填且
    /// qmin<qmax（硬断言 MDL-06④ 前半）；Continuous＝NotApplicable（I-MDL-4）。
    core::SourcedValue<JointLimits> bounds;

    /// 工程工作范围 {qmin',qmax'}，单位 rad；仅 Continuous（"prismatic 带
    /// workingRange"＝非法组合，§7.3），必须有限区间 qmin'<qmax'。分析消费
    /// 属性，不回写 bounds（MDL-12/V12-01）。
    core::SourcedValue<JointLimits> workingRange;

    /// DH 参数缓存（θ 偏置/d/a/α，rad·m）。仅 StandardDH 态填写＝权威
    /// （随字节编码）；Explicit 态如存在则为只读派生展示值（**不参与编码
    /// 身份**——D-MDL-5，读取时按需确定性重算）。
    std::optional<DhParameters> dhDerived;

    bool operator==(const JointEntry& o) const;
    bool operator!=(const JointEntry& o) const { return !(*this == o); }
};

/**
 * @brief 自碰撞排除配置（§4.3-B selfCollisionHints 行承载值）。
 *
 * excludedPairs＝手动排除对（link 名对；基座-首关节/相邻/静态三开关与
 * Manual/Auto/Imported 来源标记的展开承载随导入报告结构——T05）。
 * ★ 本值不入根对象编码权威语义（表行原文——见 LinkEntry.selfCollisionHints 注）。
 */
struct SelfCollisionSetup {
    /// 手动排除对（link 名对——名称为 LinkEntry.localName，同模型作用域）。
    std::vector<std::pair<std::string, std::string>> excludedPairs;

    bool operator==(const SelfCollisionSetup& o) const
    {
        return excludedPairs == o.excludedPairs;
    }
    bool operator!=(const SelfCollisionSetup& o) const { return !(*this == o); }
};

/**
 * @brief 连杆条目（表 4.3-B；字段声明序＝表行序＝编解码字段序）。
 *
 * ★ 连杆不独立成对象（D-MDL-1/D-MDL-2，O-36 裁决口径）：物性/几何内嵌
 * 根对象字节，objectId 为模型内标识（诊断 subject/名称映射锚），跨修订
 * 稳定（I-MDL 身份唯一）。失效粒度取舍＝字段级分层＋对象级保守失效
 * （P-MDL-2 登记行，evidence 侧确认前按卡 D-MDL-2 执行）。
 */
struct LinkEntry {
    core::ObjectId objectId;  ///< 模型内稳定标识（同 JointEntry.objectId 口径）
    std::string localName;    ///< 同 JointEntry.localName
    BodyData body;            ///< 物性组（质量/质心/惯量/材料——I-MDL-3/5）

    /// 视觉几何（evidence 失效矩阵的语义基础字段之一；非碰撞判定依据）。
    std::optional<GeometryRef> visual;
    /// 碰撞几何——不等于碰撞判定（判定唯一归 policy 评估，§4.3-B 注）。
    std::optional<GeometryRef> collision;

    /// 自碰撞排除配置（基座-首关节/相邻/静态三开关＋手动对＋来源标记的
    /// 承载值——§4.3-B 行）。★ 不入根对象编码权威语义（表行原文）：仅
    /// 导入映射中间产物，经导入报告转策略草稿输入（§6.3/P-MDL-3）——
    /// Codec 不编码本字段，往返不保真（设计使然，非缺陷）。
    std::optional<SelfCollisionSetup> selfCollisionHints;

    bool operator==(const LinkEntry& o) const;
    bool operator!=(const LinkEntry& o) const { return !(*this == o); }
};

/**
 * @brief RobotDesign 根/聚合对象（§4.3 字段表全集）。
 *
 * 字段声明序＝§4.3 表行序＝Codec 编解码字段序（§4.8 canonical 序列化
 * "字段定序"的落点）——表头注释与各字段注为契约原文的逐行落地。
 *
 * 生命周期：编辑态由 IRobotDesignEditor 工作集持有（仅 UI 线程可变）；
 * 已应用态的字节形态（canonical 编码）不可变（CON-01/PA-2）。
 */
struct RobotDesign {
    /// 对象 schema 主版本（≥1；单一权威＝ObjectTypes.hpp kRobotDesignSchemaVersion，
    /// 禁止写字面量；只增不减）。
    std::uint32_t schemaVersion = kRobotDesignSchemaVersion;

    /// 仅呈现名（UX-02：不进 Description 编译身份——变更产生修订但不改变
    /// Description；P-MDL-2 粗粒度注记指本字段也会换根对象 cv）。
    std::string displayName;

    /// 双权威模式开关（MDL-02 互斥；切换受 §7.6 条件约束——判定与等价
    /// 验证随 T09 落位，本头只承载模式值本身）。
    AuthorityMode authority = AuthorityMode::Explicit;

    BasePlacement basePlacement;  ///< 基座安装布置（MDL-22；I-MDL-7）

    /// 关节链（串联有序——数组下标即链序；≥1，I-MDL-1）。
    std::vector<JointEntry> joints;
    /// 连杆链（links.size()==joints.size()+1——runtime StructureInvalid 同口径）。
    std::vector<LinkEntry> links;

    std::optional<TcpRef> defaultTcp;      ///< 默认 TCP（有 tools 时须已设置——KIN-14/I-MDL-9）
    std::vector<core::ObjectId> toolRefs;  ///< 工具引用表（指向 tool-definition 对象；无重复）
    std::vector<core::ObjectId> sceneRefs; ///< 场景引用表（指向 scene-object 对象；无重复）
    std::optional<core::ObjectId> poseSetRef;      ///< 命名位姿集引用（至多一份——optional 表达）
    std::optional<core::ObjectId> drivetrainRef;   ///< 传动设计引用（回填目标 SEL-10）

    /// 资源清单（几何/网格登记表；编解码按 resourceId 字典序规范化——§4.8）。
    std::vector<ResourceRef> resourceManifest;

    /// 纯备注（不入编译身份；变更产生修订但不改变 Description——同 displayName）。
    std::string notes;

    bool operator==(const RobotDesign& o) const;
    bool operator!=(const RobotDesign& o) const { return !(*this == o); }
};

// =====================================================================
// 双权威编辑权限守卫（§7.3 冲突矩阵 C-1/C-2；MDL-09）
// =====================================================================

/**
 * @brief 受权威模式管辖的关节字段（C-1/C-2 的字段轴——只列两侧互斥字段；
 *        type/zeroOffset/bounds/workingRange 两态均权威，不在管辖内）。
 */
enum class AuthorityLockedField {
    Axis,        ///< 轴向：Explicit 权威／StandardDH 派生只读
    Origin,      ///< 原点：同上
    DhDerived,   ///< DH 参数：StandardDH 权威／Explicit 派生只读（展示）
};

/**
 * @brief 受管字段稳定 token（"axis"/"origin"/"dhDerived"）。纯函数；确定性。
 */
std::string_view authorityLockedFieldToken(AuthorityLockedField field) noexcept;

/**
 * @brief 权威编辑权限判定（§7.3 C-1/C-2——"Axis 编辑权限随权威模式，
 *        MDL-09"的域级规则函数；V-14 场景的拒绝判定点）。
 *
 * 规则（§7.2 参数来源表）：
 *   - Explicit 态：axis/origin 可编辑（权威）；dhDerived 拒绝（C-2——
 *     派生只读，编辑它＝双真值写入，I-MDL-8 精神）；
 *   - StandardDH 态：dhDerived 可编辑（权威）；axis/origin 拒绝（C-1——
 *     派生只读，V-14 原文场景）。
 *
 * 调用方契约：编辑器（T08）/导入映射/模板层在接受一次关节字段编辑前
 * 调用本函数；返回非空即拒绝该编辑且**不得改动任何状态**（V-14"工作集
 * 字节不变"——本函数为纯函数，拒绝时无副作用，调用方放弃应用即保持
 * 原状）。UI 呈现文案"该字段为派生只读，切换权威模式需经转换判定"
 * （§7.3 C-1 行）由编辑器按返回错误的 detail 组织。
 *
 * @param authority [in] 当前权威模式
 * @param field     [in] 待编辑的受管字段
 * @return nullopt＝可编辑（该字段在 authority 下为权威）；非空＝拒绝的
 *         ModelingError（code=AuthorityViolation，params 含 field/mode，
 *         detail 含"派生只读"提示——供 UI 与日志，不产诊断记录；诊断
 *         产码唯一经 IDiagnosticFactory，PA-1）
 *
 * 纯函数；线程安全；确定性。
 */
std::optional<ModelingError> authorityEditGuard(AuthorityMode authority,
                                                AuthorityLockedField field);

// =====================================================================
// 合法实例不变量层（§4.10 I-MDL-1～I-MDL-12）
// =====================================================================

/**
 * @brief 不变量编号（I-MDL-1～I-MDL-12——§4.10 逐条编号的枚举承载；
 *        UT 用例名与本枚举绑定，验收可机械核对逐条注册）。
 *
 * 枚举顺序＝§4.10 编号序；表尾追加纪律同 AuthorityMode。
 * I-MDL-12（R1 传动耦合阶段锁）在 §4.10 以 I-MDL-11 行括注定义
 * （"R1 下 coupling 不得配置（I-MDL-12）"）——本枚举给它独立编号位，
 * 单元卡 §4.10 增补显式行随本任务文档同步登记（§15 增量）。
 */
enum class InvariantId {
    IMdl1,   ///< 结构：joints≥1；links==joints+1；链单串联无环（§4.10）
    IMdl2,   ///< 身份唯一：同模型 ObjectId/localName 不得重复
    IMdl3,   ///< 单位合法：Provided 值全部有限（NaN/Inf 非法，不静默置 0）
    IMdl4,   ///< 限位有序/适用：qmin<qmax；continuous 无有限 bounds；workingRange 仅 continuous
    IMdl5,   ///< 物性合法：m>0；惯量 SPD＋三角不等式（对称由六分量表示满足）
    IMdl6,   ///< 轴有效：可动关节 axis 非零、有限、可归一化
    IMdl7,   ///< 基座合法：custom 必填 customEaa 且旋转正交（1×10⁻¹²）
    IMdl8,   ///< 权威互斥：StandardDH 态 axis/origin 不得为非派生来源
    IMdl9,   ///< 引用完整：defaultTcp∈toolRefs；引用表无重复（闭包存在性归 T08）
    IMdl10,  ///< 资源状态机：Recorded 带 externalRecord；Solidified 带 solidifiedObject
    IMdl11,  ///< 传动合法：ratio 有限>0；R2 下 C 方阵且条件数 ≤1×10⁸
    IMdl12,  ///< R1 传动耦合阶段锁：coupling 不得配置（I-MDL-11 行括注定义）
    IMdl13,  ///< 工具 TCP 完整：tcpList ≥1 且键非空唯一（§4.4 tcpList 行"≥1"——WP-13-T10 落位补行）
};

/**
 * @brief 不变量编号稳定 token（"I-MDL-1"～"I-MDL-12"——卡面编号原文；
 *        诊断 subject 与 UT 名绑定用）。纯函数；确定性。
 */
std::string_view invariantIdToken(InvariantId id) noexcept;

/**
 * @brief 一次不变量违例的定位记录。
 *
 * id＝违反的不变量编号（机器判别）；subject＝字段/对象定位路径
 * （如 "joints[2].axis"——比较型定位的最小形态；用户可见文案由诊断层
 * 组织，本结构只承载定位事实）。相等＝两字段全等。
 */
struct InvariantViolation {
    InvariantId id;   ///< 违反的不变量编号（I-MDL-x）
    std::string subject;  ///< 定位路径（字段下标精确到位，UTF-8）

    bool operator==(const InvariantViolation& o) const noexcept
    {
        return id == o.id && subject == o.subject;
    }
    bool operator!=(const InvariantViolation& o) const noexcept { return !(*this == o); }
};

/**
 * @brief 前向声明（部件对象的不变量核查在 Parts.hpp——I-MDL-11/12 落在
 *        传动对象上，根对象只有引用位）。
 */
struct DrivetrainDesign;
struct ToolDefinition;

/**
 * @brief RobotDesign 根对象全量不变量核查（§4.10 I-MDL-1～I-MDL-10；
 *        I-MDL-11/12 见 Parts.hpp 的传动对象重载）。
 *
 * 处置原则（§4.10 尾段）：本函数是**校验层**——返回全部违例（不抛、
 * 不截断），由调用方按边界语义处置：构造/编辑边界 fail-fast（调用方
 * 错误），应用（命令）边界阻断＋定位诊断（T08 命令处理器）。本函数
 * 不静默修复/截断/默认填充任何非法值（NFR-COR-03）。
 *
 * 范围注记（与卡面的两处分工，均登记于单元卡 §15 增量）：
 *   - I-MDL-9 的"指向对象存在于闭包且 token 匹配"半段需要修订闭包视图
 *     （②查询端口）——值模型层无闭包上下文，该半段归 T08 就绪校验
 *     L1 层（§8.2），本函数只核查根对象内可判部分（defaultTcp∈toolRefs、
 *     引用表无重复）；
 *   - I-MDL-7 的"preset≠ground 而 R=I 在映射层拒绝"是导入映射层规则
 *     （§7.7/T05/T11），不属于本值模型校验。
 *
 * @param design [in] 待核查的根对象（只读；接受任意状态——违例即输出，
 *               不设前置）
 * @return 违例清单（空＝全部通过；顺序＝I-MDL 编号序→遍历序，确定性；
 *          同输入重复调用同输出——NFR-COR-02）
 *
 * 复杂度：O(joints+links+resources＋引用表)。纯函数；线程安全。
 */
std::vector<InvariantViolation> checkInvariants(const RobotDesign& design);

}  // namespace sdurws::ird::modeling

#endif  // IRD_MODELING_ROBOTDESIGN_HPP
