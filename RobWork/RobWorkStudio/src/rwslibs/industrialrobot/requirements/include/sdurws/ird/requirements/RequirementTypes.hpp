/**
 * @file   RequirementTypes.hpp
 * @brief  requirements 值模型——任务点/工作区域/工况/采样计划四类领域对象
 *         与需求根对象的纯值承载，附 I-REQ-1~10 不变量校验层。
 *
 * 设计依据：
 *   - units/requirements.md §4.2~§4.5（根对象/TaskPoint/WorkRegion/
 *     OperatingCondition 字段表）、§4.7（合法/非法实例与不变量 I-REQ-1~10
 *     ——本头的校验层即该节的实现面）、§4.8（身份/版本/RequirementProfile
 *     派生档）、§5.1~§5.3（任务点三段/区域采样定义/五姿态规则）、§6.1
 *     （工况字段语义）、§6.2（必验冻结 schema——RequiredCaseEntry 权威
 *     形态）、§3.3（公共头表 RequirementTypes.hpp 行——T03）、§3.4（线程
 *     与确定性总约定——纯值类型、物理量恒 SI）
 *   - core.md §4.1/§4.2/§4.3（ObjectId/Digest256/SourcedValue 值语义）、
 *     §6.3（"对什么做摘要归各所有者"——canonical 编码在 Codec.hpp）
 *   - 需求 REQ-01（任务点位姿约束）、REQ-02（三段）、REQ-03（区域与采样
 *     定义）、REQ-04（工况要求值）、REQ-09（姿态规则五规则）、NFR-COR-01/02
 *     （确定性）、NFR-COR-03（不静默改写）
 *   - 任务契约 tasks/foundation/WP-14-T03.json acceptance 1（I-REQ-1~10
 *     不变量）、4（构造校验错误语义）与 acceptance 5（O-36 子条目身份口径）
 *
 * 背景说明（为什么值模型全部"哑"、校验独立成层）：五个持久化对象
 * （req-set＋四集合）的 canonical 字节是内容身份（CON-05）的唯一输入，
 * 要求"结构体＝字段的忠实载体，任何业务判定都不改变字节"。因此本头把
 * 非法实例**拒之门外**的职责放在两处构造边界——领域服务（Services.hpp
 * 的 createXxx，编辑期）与编解码校验链（Codec.hpp decode 第④步，字节
 * 期）——两处都调用本头的同一批 check 函数（语义单源，NFR-MNT-04）；
 * 结构体自身不设构造校验（聚合初始化必须保持平凡，否则编解码装配与
 * 测试夹具无法逐字段构造）。
 *
 * O-36 口径（acceptance 5——2026-09-22 已裁决）：条目级 objectId 是
 * **模型内标识**（诊断 subjectObjectId 锚＋集合内定位键），不是独立存储
 * 对象——objectRefs 复核范围仅五个真实存储对象（req-set/req-point-set/
 * req-region-set/req-condition-set/req-plan-set）。子条目 ObjectId 经
 * project HandlerContext.objectId() 分配（编辑期为服务产出的"临时句柄"，
 * 命令 prepare 阶段重绑正式分配）、跨修订稳定、内嵌于集合对象字节
 * （§4.1 原文，与 P-MDL-1 同源）。
 *
 * 线程安全：本头全部实体为纯值类型/纯函数（无共享可变状态），并发只读
 * 安全（§3.4 总约定 1）。
 * 确定性（NFR-COR-01/02）：排序键＝ObjectId 规范文本字典序（I-REQ-1）；
 * 全部比较为字节/值精确比较（附录 D 第 12 项——身份无容差）；无环境/
 * 时钟/locale 依赖。
 */

#ifndef IRD_REQUIREMENTS_REQUIREMENTTYPES_HPP
#define IRD_REQUIREMENTS_REQUIREMENTTYPES_HPP

#include <sdurws/ird/core/Digest.hpp>       // Digest256——导入溯源内容摘要（I-REQ-8）
#include <sdurws/ird/core/Identity.hpp>     // ObjectId——条目模型内标识（O-36 口径）
#include <sdurws/ird/core/Provenance.hpp>   // SourcedValue/ValueProvenance——四态字段值
#include <sdurws/ird/requirements/Errors.hpp>      // RequirementErrorCode——校验层错误面
#include <sdurws/ird/requirements/ObjectTypes.hpp> // 五对象 schema 版本常量＋ProcessTag 词表

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <rw/math/Vector3D.hpp>  // rw::math::Vector3D<double>——位置/尺寸/欧拉角（SI：m/rad）

namespace sdurws::ird::requirements {

// =====================================================================
// 基础词表（§4.3/§5.3/§6.1 字段表——枚举顺序＝卡面原文序；持久化契约面
// 纪律：只允许表尾追加并走单元卡增量修订，不重排既有值——数值进入
// canonical 字节）
// =====================================================================

/**
 * @brief 需求等级词表（§4.3 level 字段："上游词表仅两级（Info 不承接，
 *        信息归 note——§2.5）"）。
 *
 * 两级语义下游消费面：Must＋enabled 参与硬判定（必经状态/必验工况/优化
 * 硬约束）；Should 未满足只告警（REQ-06 分级）。I-REQ-9：mandatory ≡
 * (level==Must)——不存在独立的第三"必验"开关（派生唯一）。
 *
 * 线程安全：纯值枚举；确定性：同值同 token（NFR-COR-02）。
 */
enum class RequirementLevel : std::uint8_t {
    Must,    ///< "Must"——必须满足（硬判定参与级）
    Should,  ///< "Should"——应满足（未满足告警级）
};

/// @brief 等级的稳定 token（§4.3 括注原文串；静态存储期）。
std::string_view requirementLevelToken(RequirementLevel level) noexcept;

/// @brief 词表串 → 等级（try 轨——词表外（含 "Info"）返回 nullopt 不猜测；
///        精确等值比较，无大小写折叠）。
std::optional<RequirementLevel> tryRequirementLevel(std::string_view token) noexcept;

/**
 * @brief 三段进退轴词表（§4.3 TaskSegment.axis："ToolZ（工具系 Z）|
 *        ReferenceZ（参考系 Z）"——§5.1 进入/离开语义行）。
 *
 * 轴的几何方向（该轴在世界系的方向）由 runtime/下游统一计算（§5.1 隔离
 * 声明——本单元不做坐标变换）；本词表只登记"沿哪个系的 Z"这一事实。
 */
enum class SegmentAxis : std::uint8_t {
    ToolZ,       ///< "ToolZ"——工具坐标系 Z 轴（工具系）
    ReferenceZ,  ///< "ReferenceZ"——参考坐标系 Z 轴（refFrame 系）
};

/// @brief 三段轴的稳定 token（§4.3 括注原文串；静态存储期）。
std::string_view segmentAxisToken(SegmentAxis axis) noexcept;

/// @brief 词表串 → 三段轴（try 轨——词表外返回 nullopt）。
std::optional<SegmentAxis> trySegmentAxis(std::string_view token) noexcept;

/**
 * @brief 姿态规则五规则词表（§5.3 表 kind 列：Fixed/AlignFrame/
 *        AlignGeometryNormal/PointAtTarget/ToolRollFree——REQ-09）。
 *
 * 枚举顺序＝§5.3 表行序；各规则的参数载荷见 OrientationRule 结构体
 * （kind 与载荷字段的对应关系在该类注释逐行登记）。持久化契约面纪律：
 * 表尾追加（第五规则 ToolRollFree 即历史增列先例——AT-23 五规则样例）。
 */
enum class OrientationRuleKind : std::uint8_t {
    Fixed,                ///< "Fixed"——固定姿态（Z-Y-X 欧拉参数字面）
    AlignFrame,           ///< "AlignFrame"——对齐目标参考系姿态
    AlignGeometryNormal,  ///< "AlignGeometryNormal"——对齐几何法向（MDL-15 场景对象）
    PointAtTarget,        ///< "PointAtTarget"——工具轴指向目标点
    ToolRollFree,         ///< "ToolRollFree"——允许工具滚转（第五规则）
};

/// @brief 姿态规则种类的稳定 token（§5.3 kind 列原文串；静态存储期）。
std::string_view orientationRuleKindToken(OrientationRuleKind kind) noexcept;

/// @brief 词表串 → 姿态规则种类（try 轨——词表外返回 nullopt）。
std::optional<OrientationRuleKind> tryOrientationRuleKind(std::string_view token) noexcept;

/**
 * @brief 几何特征词表（§5.3 AlignGeometryNormal 行：feature＝
 *        FrameOrigin | FramePlaneNormal——对齐目标的特征面）。
 */
enum class OrientationFeature : std::uint8_t {
    FrameOrigin,      ///< "FrameOrigin"——坐标系原点（点特征）
    FramePlaneNormal, ///< "FramePlaneNormal"——坐标平面法向（面特征）
};

/// @brief 几何特征的稳定 token（§5.3 括注原文串；静态存储期）。
std::string_view orientationFeatureToken(OrientationFeature feature) noexcept;

/// @brief 词表串 → 几何特征（try 轨——词表外返回 nullopt）。
std::optional<OrientationFeature> tryOrientationFeature(std::string_view token) noexcept;

/**
 * @brief 工况事件类型词表（§4.5 events 行：type: Grasp|Release|Dwell）。
 */
enum class ConditionEventType : std::uint8_t {
    Grasp,   ///< "Grasp"——夹取
    Release, ///< "Release"——释放
    Dwell,   ///< "Dwell"——驻留
};

/// @brief 事件类型的稳定 token（§4.5 括注原文串；静态存储期）。
std::string_view conditionEventTypeToken(ConditionEventType type) noexcept;

/// @brief 词表串 → 事件类型（try 轨——词表外返回 nullopt）。
std::optional<ConditionEventType> tryConditionEventType(std::string_view token) noexcept;

/**
 * @brief 工况适用范围词表（§4.5 appliesTo 行：scope: AllStations |
 *        Stations[oid…] | None——绑定方向唯一，D-REQ-5：工况侧声明）。
 *
 * None＋enabled 合法（§6.1："声明'当前不适用'的工况，ERR-01 不适用
 * 标记的存储形态"）。
 */
enum class AppliesToScope : std::uint8_t {
    AllStations,  ///< "AllStations"——适用全部任务点（工位）
    Stations,     ///< "Stations"——适用显式清单（station ObjectIds）
    None,         ///< "None"——当前不适用（显式标记）
};

/// @brief 适用范围的稳定 token（§4.5 括注原文串；静态存储期）。
std::string_view appliesToScopeToken(AppliesToScope scope) noexcept;

/// @brief 词表串 → 适用范围（try 轨——词表外返回 nullopt）。
std::optional<AppliesToScope> tryAppliesToScope(std::string_view token) noexcept;

// =====================================================================
// RequirementReference——跨聚合浅引用（§4.3 refFrame/tcpRef 字段；
// §8.1 浅校验边界：仅 token 匹配核对，不解码 modeling 对象内容——D-REQ-10）
// =====================================================================

/**
 * @brief 需求侧引用种类（§4.3：refFrame 的 {World | ModelFrame{oid} |
 *        SceneObject{oid}} 与 tcpRef 的 {Tool{oid, tcpKey} | DefaultTcp}）。
 *
 * 五种类合一枚举的原因：refFrame/tcpRef 同为 RequirementReference 类型
 * （字段表原文），World/DefaultTcp 是无载荷缺省种——合并后"哪字段允许
 * 哪 kind"由使用处的 allowedKinds 表约束（validateRequirementReference
 * 按使用场景核对），引用结构自身不限制（同一结构两用，字段表原文口径）。
 */
enum class RequirementRefKind : std::uint8_t {
    World,        ///< "World"——世界坐标系（refFrame 缺省；无载荷）
    ModelFrame,   ///< "ModelFrame"——模型坐标系（载荷＝robot-design 对象 ObjectId）
    SceneObject,  ///< "SceneObject"——场景对象（载荷＝scene-object 对象 ObjectId）
    Tool,         ///< "Tool"——工具定义及其 TCP（载荷＝tool-definition ObjectId＋tcpKey）
    DefaultTcp,   ///< "DefaultTcp"——工具默认 TCP（无 oid 载荷——工具缺省面）
};

/// @brief 引用种类的稳定 token（§4.3 括注原文串；静态存储期）。
std::string_view requirementRefKindToken(RequirementRefKind kind) noexcept;

/// @brief 词表串 → 引用种类（try 轨——词表外返回 nullopt）。
std::optional<RequirementRefKind> tryRequirementRefKind(std::string_view token) noexcept;

/**
 * @brief 跨聚合浅引用值（§4.3 RequirementReference——ObjectId＋token
 *        元数据的 requirements 侧承载；§12 modeling 行"跨聚合浅引用规范"）。
 *
 * token 匹配表（I-REQ-4——expectedTargetToken 单点实现；token 字面为
 * modeling/runtime 侧对象 token 的**元数据交叉引用**，R-1 禁 include 其
 * 头文件，字符串字面同卡 §9.7 关系图/§12 交接行口径）：
 *   - World/DefaultTcp：无目标（不参与 token 匹配——恒合法缺省）；
 *   - ModelFrame→"robot-design"（模型坐标系承载对象）；
 *   - SceneObject→"scene-object"（MDL-15 场景对象）；
 *   - Tool→"tool-definition"（工具定义对象）。
 *
 * 所有权：纯值；ObjectId 由调用方（导入映射/编辑器/捕获流）自 modeling
 * 对象引用表取得，本单元不分配不解析其内容（§8.1 浅校验边界）。
 */
struct RequirementReference {
    RequirementRefKind kind = RequirementRefKind::World;  ///< 引用种类（缺省 World）
    std::optional<core::ObjectId> objectId;               ///< 目标对象（ModelFrame/SceneObject/Tool 必有；其余须空）
    std::string tcpKey;                                   ///< 工具 TCP 键（仅 Tool 有语义——tool-definition 对象内 tcpList 键）

    /**
     * @brief 结构自洽校验（载荷与 kind 匹配；I-REQ-4 的结构半区）。
     *
     * 有载荷种（ModelFrame/SceneObject/Tool）必须携带有效 ObjectId
     * （Tool 另要求 tcpKey 非空）；无载荷种（World/DefaultTcp）不得携带
     * ObjectId/tcpKey——违约返回 false（构造边界拒绝，不静默裁剪——
     * NFR-COR-03）。
     */
    bool wellFormed() const noexcept;

    bool operator==(const RequirementReference& o) const;
    bool operator!=(const RequirementReference& o) const { return !(*this == o); }
};

/**
 * @brief 引用种类 → 目标对象类型 token（I-REQ-4 匹配表的单点实现）。
 *
 * @param kind [in] 引用种类（全表 5 值均可入参）
 * @return 目标对象 token（ModelFrame→"robot-design"、SceneObject→
 *         "scene-object"、Tool→"tool-definition"；World/DefaultTcp 返回
 *         空串——无目标、不参与 token 匹配）
 *
 * 纯函数；线程安全；确定性（NFR-COR-02）。就绪校验器（T05 R1/R8）以本
 * 函数产出的 token 与闭包 objectRefs 登记值核对；本单元内构造边界以
 * validateRequirementReference 做结构半区核对（跨闭包半区归 T05——本
 * 单元拿不到闭包，§8.1）。
 */
std::string_view expectedTargetToken(RequirementRefKind kind) noexcept;

// =====================================================================
// 位姿约束/容差/姿态规则（§4.3 PoseConstraint/ToleranceSpec＋§5.3 五规则）
// =====================================================================

/**
 * @brief 六分量受约束掩码（§4.3 constrainedDof："false＝该分量自由，供
 *        下游解空间解释；全 false 非法——任务点至少约束一个分量，
 *        I-REQ-5"）。
 *
 * 分量语义：position 三分量（x/y/z，refFrame 系，m）＋orientation 三欧拉
 * 分量（roll/pitch/yaw，rad）。纯位集合（无序——掩码语义）。
 */
struct ConstrainedDof {
    bool x = false;      ///< 位置 X 分量受约束（m）
    bool y = false;      ///< 位置 Y 分量受约束（m）
    bool z = false;      ///< 位置 Z 分量受约束（m）
    bool roll = false;   ///< 姿态 roll 分量受约束（rad）
    bool pitch = false;  ///< 姿态 pitch 分量受约束（rad）
    bool yaw = false;    ///< 姿态 yaw 分量受约束（rad）

    /// @brief 至少一真（I-REQ-5 前半——任一 true 即 true）。
    bool any() const noexcept { return x || y || z || roll || pitch || yaw; }

    bool operator==(const ConstrainedDof& o) const noexcept
    {
        return x == o.x && y == o.y && z == o.z && roll == o.roll
            && pitch == o.pitch && yaw == o.yaw;
    }
    bool operator!=(const ConstrainedDof& o) const noexcept { return !(*this == o); }
};

/**
 * @brief 工具滚转允许区间（§5.3 ToolRollFree 行：rollRange {min,max}
 *        rad，默认 [−π,π]）。
 *
 * 单位 rad；有序约束 min < max（逆序＝非法参数——构造边界拒绝，
 * acceptance 4"rollRange 逆序"反例；等值 min==max 亦拒绝——区间为空）。
 */
struct RollRange {
    double min = -3.14159265358979323846;  ///< 下界（rad；默认 −π）
    double max = 3.14159265358979323846;   ///< 上界（rad；默认 ＋π）

    /// @brief 有序且非空区间（min < max；两侧均须有限）。
    bool wellFormed() const noexcept;

    bool operator==(const RollRange& o) const noexcept
    {
        return min == o.min && max == o.max;
    }
    bool operator!=(const RollRange& o) const noexcept { return !(*this == o); }
};

/**
 * @brief 姿态与方向规则（§5.3 五规则——REQ-09；kind＋参数载荷同体结构）。
 *
 * kind→有效载荷字段对照（其余字段必须保持缺省值——validateOrientationRule
 * 核对，违约即非法参数；canonical 编码按 kind 分支写对应载荷，编码确定性
 * 不受未用字段缺省值影响）：
 *   - Fixed：fixedRpy（rad，refFrame 系；Z-Y-X 欧拉序显式登记——分量
 *     (x,y,z)=(roll,pitch,yaw)，应用序 Rz·Ry·Rx；不设四元数编辑态，
 *     D-REQ-3）；
 *   - AlignFrame：targetFrame（RequirementReference，wellFormed 且 kind
 *     须为 ModelFrame/SceneObject——对齐目标是一个参考系）；
 *   - AlignGeometryNormal：targetSceneObject（ObjectId）＋feature（必须
 *     显式给出——缺 feature＝非法参数，acceptance 4"缺 feature"反例）
 *     ＋invertNormal；
 *   - PointAtTarget：targetPoint（m，refFrame 系；零向量＝非法——
 *     acceptance 4"零向量目标"反例）；
 *   - ToolRollFree：rollRange（rad，有序非空——逆序＝非法）。
 *
 * 解析语义边界（§5.3 隔离声明）：本单元不解析规则到姿态（"应用时解析"
 * 归 T06/下游）；非法参数的**构造边界拒绝**在本单元（validateOrientationRule）。
 *
 * 等价姿态不做归一（§5.3"等价姿态与内容身份"行——NFR-COR-03 不改写
 * 用户输入）：RPY 等效角按参数字面存储与编码，参数编辑即内容变更。
 */
struct OrientationRule {
    OrientationRuleKind kind = OrientationRuleKind::Fixed;  ///< 规则种类（§5.3 kind 列）
    rw::math::Vector3D<double> fixedRpy{0.0, 0.0, 0.0};     ///< Fixed 载荷（rad；Z-Y-X 欧拉序）
    RequirementReference targetFrame;                       ///< AlignFrame 载荷（对齐目标参考系）
    std::optional<core::ObjectId> targetSceneObject;        ///< AlignGeometryNormal 载荷（场景对象）
    std::optional<OrientationFeature> feature;              ///< AlignGeometryNormal 载荷（几何特征——必须显式）
    bool invertNormal = false;                              ///< AlignGeometryNormal 载荷（法向取反）
    rw::math::Vector3D<double> targetPoint{0.0, 0.0, 0.0};  ///< PointAtTarget 载荷（m；refFrame 系）
    RollRange rollRange;                                    ///< ToolRollFree 载荷（rad；默认 [−π,π]）

    bool operator==(const OrientationRule& o) const;
    bool operator!=(const OrientationRule& o) const { return !(*this == o); }
};

/**
 * @brief 容差规格（§4.3 ToleranceSpec——位置/姿态两分量）。
 *
 * 设计默认（黄金数据集锁定，§4.3 原文）：positionTolerance=1×10⁻³ m、
 * orientationTolerance=π/180 rad。容差是"要求值"（评估按此校验残差），
 * 不是判定阈值（§6.3 分离——判定阈值权威归 policy，本单元零私设）。
 * 比较口径遵循附录 D 通用比较公式（逐元素、零参考退化 ε_abs）——比较
 * 实现归 core/testkit 比较设施，本结构只承载数值。
 */
struct ToleranceSpec {
    double positionTolerance = 1.0e-3;                       ///< 位置容差（m；>0 且有限——I-REQ-5）
    double orientationTolerance = 3.14159265358979323846 / 180.0;  ///< 姿态容差（rad；>0 且有限——I-REQ-5）

    bool operator==(const ToleranceSpec& o) const noexcept
    {
        return positionTolerance == o.positionTolerance
            && orientationTolerance == o.orientationTolerance;
    }
    bool operator!=(const ToleranceSpec& o) const noexcept { return !(*this == o); }
};

/**
 * @brief 受约束位姿（§4.3 PoseConstraint——任务点位姿的约束表达）。
 *
 * position 为四态字段（core::SourcedValue）——"未提供该分量值"是合法态
 * （MDL-06 缺失不转零）；Provided 时三分量必须有限（§4.3 约束行）。
 * constrainedDof 至少一真（I-REQ-5）；orientation 见 OrientationRule。
 */
struct PoseConstraint {
    core::SourcedValue<rw::math::Vector3D<double>> position;  ///< 受约束位置（m；refFrame 系）
    ConstrainedDof constrainedDof{};                          ///< 六分量受约束掩码（至少一真——I-REQ-5）
    OrientationRule orientation{};                            ///< 姿态规则（§5.3 五规则）

    bool operator==(const PoseConstraint& o) const;
    bool operator!=(const PoseConstraint& o) const { return !(*this == o); }
};

/**
 * @brief 三段之一（§4.3 TaskSegment：approach/work/retract——REQ-02）。
 *
 * distanceM 单位 m（>0）；方向语义（§5.1）：approach 沿轴**负向**趋近、
 * retract 沿轴**正向**离开——语义约定归本单元登记，几何解释归下游。
 * work 段即任务点本身（enabled 恒 true，占位表达段序——§4.3 字段表行）。
 */
struct TaskSegment {
    bool enabled = false;   ///< 段启用（work 段恒 true）
    SegmentAxis axis = SegmentAxis::ToolZ;  ///< 进退轴（§4.3 词表）
    double distanceM = 0.0; ///< 段距离（m；>0——启用时校验）

    bool operator==(const TaskSegment& o) const noexcept
    {
        return enabled == o.enabled && axis == o.axis && distanceM == o.distanceM;
    }
    bool operator!=(const TaskSegment& o) const noexcept { return !(*this == o); }
};

/**
 * @brief 任务级"要求值"（§4.3 demands 字段——REQ-04 要求值/策略阈值分离，
 *        §6.3：本单元只存储传递，零判定阈值私设）。
 */
struct RequirementDemand {
    bool collisionFreeRequired = false;          ///< 无碰撞要求（布尔要求值）
    std::optional<double> minimumJointMargin;    ///< 最小关节裕量（rad/m 依关节类型；可选）

    bool operator==(const RequirementDemand& o) const;
    bool operator!=(const RequirementDemand& o) const { return !(*this == o); }
};

// =====================================================================
// 溯源载荷（§4.3 generation/importProvenance 字段——I-REQ-8/I-REQ-10 的
// 结构化落实：导入溯源无路径字段、生成溯源无源条目引用字段）
// =====================================================================

/**
 * @brief 模板/阵列/镜像生成溯源（§4.3 generation 字段——REQ-07/11）。
 *
 * ★ I-REQ-10（派生不回写）的结构化落实：本结构只存"一次性参数快照"
 * （§7.2 循环派生行）——**没有源条目 ObjectId/名称字段**：派生条目对源
 * 条目零引用、零回写（产物为普通独立条目，D-REQ-4）；再次派生＝以新
 * 参数生成新条目（溯源链参数留痕），不存在"源变→派生自动变"的活性
 * 传播。
 *
 * linked=false 语义（§4.3）：已解除关联（重生成不再替换该条目）。
 * instanceId＝生成批次实例标识（重生成定位键；§7.2 重生成行）。
 * parameters＝生成参数键值对（构造序保序——canonical 编码确定性）。
 */
struct GenerationProvenance {
    std::string generatorId;    ///< 生成器标识（如 "mirror"/模板 kind token——词表归产生服务）
    std::string instanceId;     ///< 生成批次实例标识（同批多条目同值）
    bool linked = true;         ///< 是否仍与生成批次关联（false＝已解除）
    std::vector<std::pair<std::string, std::string>> parameters;  ///< 参数快照（保序——确定性）

    bool operator==(const GenerationProvenance& o) const;
    bool operator!=(const GenerationProvenance& o) const { return !(*this == o); }
};

/**
 * @brief 导入溯源（§4.3 importProvenance 字段——源文件内容摘要＋行号）。
 *
 * ★ I-REQ-8（路径不作身份）的结构化落实：字段只有 sourceDigest（源文件
 * **内容** SHA-256 摘要——core::Digest256）与 recordNumber（行号/记录号）
 * ——**没有路径字段**：绝对路径仅入草稿 externalRefs 与诊断文案（§4.7
 * I-REQ-8 原文），绝不进入条目字节＝不进入内容身份。
 */
struct ImportProvenance {
    core::Digest256 sourceDigest{};  ///< 源文件内容摘要（SHA-256 原始 32 字节；全零＝空保留值——调用方保证非零）
    std::uint64_t recordNumber = 0;  ///< 源记录号（CSV 行号等；≥1 合法，0＝未登记）

    bool operator==(const ImportProvenance& o) const noexcept
    {
        return sourceDigest == o.sourceDigest && recordNumber == o.recordNumber;
    }
    bool operator!=(const ImportProvenance& o) const noexcept { return !(*this == o); }
};

// =====================================================================
// 采样定义（§5.2——位置/姿态采样**定义**；样本生成归 KIN-04，本单元零
// 枚举出口：类型面没有任何"样本点集"载体）
// =====================================================================

/**
 * @brief 位置采样方法词表（§5.2 PositionSampling 行：Grid{counts} |
 *        GridBySpacing{spacing} | Random{count}）。
 */
enum class PositionSamplingMethod : std::uint8_t {
    Grid,           ///< "Grid"——显式计数网格（counts[3] 即权威）
    GridBySpacing,  ///< "GridBySpacing"——间距式（构建期规范化为 counts——D-REQ-2）
    Random,         ///< "Random"——随机计数（count；种子/生成归 KIN-04）
};

/// @brief 位置采样方法的稳定 token（§5.2 括注原文串；静态存储期）。
std::string_view positionSamplingMethodToken(PositionSamplingMethod method) noexcept;

/// @brief 词表串 → 位置采样方法（try 轨——词表外返回 nullopt）。
std::optional<PositionSamplingMethod> tryPositionSamplingMethod(std::string_view token) noexcept;

/**
 * @brief 位置采样定义（§5.2——区域盒内位置样本的**计划定义**，非样本）。
 *
 * Grid：counts[3] 即权威计数（每轴采样数，≥0；乘积=0 合法存储——V-02
 * 零样本判定归评估）；GridBySpacing：spacing[3]（m，三分量>0 有限），
 * 构建期经 normalizeSampling 规范化为 counts（counts[i]=floor(size[i]/
 * spacing[i])+1，D-REQ-2）——**计划内容身份以规范化后的计数计算**；
 * Random：count（≥0；随机种子与生成算法归 KIN-13/KIN-04，本单元不持）。
 *
 * 规范化语义边界：GridBySpacing 条目是**编辑态**表达；进入 SamplingPlan
 * 条目（req-plan-set）的恒为规范化形态（buildPlan 强制）——两形态在
 * WorkRegion.positionSampling（区域侧定义）与 SamplingPlan（计划侧落位）
 * 各就各位，§5.2 冻结语义图。
 */
struct PositionSampling {
    PositionSamplingMethod method = PositionSamplingMethod::Grid;  ///< 采样方法
    std::array<std::uint32_t, 3> counts{1, 1, 1};                  ///< Grid 载荷（每轴计数；≥0）
    std::array<double, 3> spacing{0.0, 0.0, 0.0};                  ///< GridBySpacing 载荷（m；三分量>0 有限）
    std::uint32_t count = 0;                                       ///< Random 载荷（总计数；≥0）

    bool operator==(const PositionSampling& o) const;
    bool operator!=(const PositionSampling& o) const { return !(*this == o); }
};

/**
 * @brief 姿态采样定义（§5.2——方向/滚转两组计数；供 KIN-04 姿态覆盖率的
 *        全局口径采样）。
 *
 * directionSamples/rollSamples 均 ≥1（§5.2 行"≥1"——姿态采样无零样本
 * 形态：零样本仅位置侧 counts 乘积=0 表达，V-02）；methodToken＝方法
 * token（词表归 KIN-04 姿态覆盖率口径——本单元原样存储不解释，§1.4
 * 立场"不私设采样方法语义"）。
 */
struct OrientationSampling {
    std::uint32_t directionSamples = 1;  ///< 方向采样数（≥1）
    std::uint32_t rollSamples = 1;       ///< 滚转采样数（≥1）
    std::string methodToken;             ///< 方法 token（KIN-04 口径词表；原样存储）

    bool operator==(const OrientationSampling& o) const
    {
        return directionSamples == o.directionSamples && rollSamples == o.rollSamples
            && methodToken == o.methodToken;
    }
    bool operator!=(const OrientationSampling& o) const { return !(*this == o); }
};

/**
 * @brief 覆盖率目标（§4.4 coverageTargets 字段——KIN-04 判定输入）。
 *
 * minPositionCoverage 必填（REQ-03；默认 0.8 登记为设计默认，黄金锁定）；
 * minOrientationCoverage 可选。两值 ∈[0,1]（I-REQ-6）。
 */
struct CoverageTargets {
    double minPositionCoverage = 0.8;                 ///< 位置覆盖率下限（∈[0,1]；REQ-03 默认 0.8）
    std::optional<double> minOrientationCoverage;     ///< 姿态覆盖率下限（∈[0,1]；可选）

    bool operator==(const CoverageTargets& o) const;
    bool operator!=(const CoverageTargets& o) const { return !(*this == o); }
};

/**
 * @brief 区域盒几何（§4.4 box 字段——R1 仅 Box，P-REQ-7：其他几何走需求
 *        变更不预留桩）。
 *
 * center/size 单位 m（refFrame 系）；size 三分量 >0 且有限（I-REQ-6 非
 * 退化）；边界即盒面（§5.2 区域几何行）。
 */
struct BoundingBox {
    rw::math::Vector3D<double> center{0.0, 0.0, 0.0};  ///< 盒中心（m；refFrame 系）
    rw::math::Vector3D<double> size{0.0, 0.0, 0.0};    ///< 盒三向尺寸（m；三分量>0 有限——I-REQ-6）

    bool operator==(const BoundingBox& o) const;
    bool operator!=(const BoundingBox& o) const { return !(*this == o); }
};

// =====================================================================
// 四类条目值模型（§4.3/§4.4/§4.5/§5.2 字段表——字段序＝表行序，canonical
// 编码字段定序同序）
// =====================================================================

/**
 * @brief 任务点条目（§4.3 字段表全量；REQ-01/02/09/10 语义载体）。
 *
 * 生命周期：由领域服务 createPoint 构造（临时句柄）→编辑器工作集→命令
 * prepare 重绑正式 ObjectId→req-point-set 集合对象字节（随对象版本化）。
 * objectId 为模型内标识（O-36——诊断 subjectObjectId 锚，跨修订稳定）。
 *
 * 位姿参考系：refFrame 参考系下表达（默认 World）；与机器人基座的关系由
 * runtime 编译/解析统一解释（§4.3 头行——本单元不做坐标变换）。
 * 全部物理量 SI（m/rad），§3.4 总约定 3。
 */
struct TaskPoint {
    core::ObjectId objectId;                  ///< 模型内标识（O-36；创建时一次分配，跨修订稳定）
    std::string name;                         ///< 语义名（集合内唯一——I-REQ-3；改名＝内容变更，D-REQ-6）
    ProcessTag processTag = ProcessTag::Generic;  ///< 工艺标签（11 值词表——ObjectTypes.hpp）
    RequirementLevel level = RequirementLevel::Must;  ///< 需求等级（两值词表；I-REQ-9 派生源）
    bool enabled = true;                      ///< 启用（未启用条目不进入正式就绪判定——§4.3）
    core::ValueProvenance source{};           ///< 来源标记（UserProvided/ImportMapped——§4.3 source 行）
    std::optional<GenerationProvenance> generation;        ///< 生成溯源（模板/阵列/镜像——I-REQ-10 零回写）
    std::optional<ImportProvenance> importProvenance;      ///< 导入溯源（摘要＋行号——I-REQ-8 无路径）
    RequirementReference refFrame;            ///< 参考坐标系（默认 World；I-REQ-4 token 匹配）
    std::optional<RequirementReference> tcpRef;            ///< 工具/TCP 引用（REQ-01；跨聚合浅引用）
    PoseConstraint pose{};                    ///< 受约束位姿（REQ-01）
    ToleranceSpec tolerance{};                ///< 容差（要求值；I-REQ-5 >0 有限）
    TaskSegment approach{};                   ///< 接近段（沿轴负向趋近——§5.1）
    TaskSegment work{};                       ///< 作业段（即任务点本身——enabled 恒 true）
    TaskSegment retract{};                    ///< 撤离段（沿轴正向离开）
    RequirementDemand demands{};              ///< 任务级要求值（REQ-04；§6.3 分离）
    std::optional<std::string> sequenceKey;   ///< 顺序键（前驱条目名——I-REQ-7 拓扑输入；见 checkSequence）
    std::string note;                         ///< 备注（吸收旧 Info 级信息——§2.5）

    bool operator==(const TaskPoint& o) const;
    bool operator!=(const TaskPoint& o) const { return !(*this == o); }
};

/**
 * @brief 工作区域条目（§4.4 字段表；REQ-03 语义载体——区域几何＋采样
 *        **定义**＋覆盖率目标；样本生成归 KIN-04，§5.2 边界）。
 */
struct WorkRegion {
    core::ObjectId objectId;                  ///< 模型内标识（O-36 同口径）
    std::string name;                         ///< 语义名（集合内唯一——I-REQ-3）
    RequirementLevel level = RequirementLevel::Must;  ///< 需求等级
    bool enabled = true;                      ///< 启用
    core::ValueProvenance source{};           ///< 来源标记
    std::optional<GenerationProvenance> generation;        ///< 生成溯源（I-REQ-10）
    std::optional<ImportProvenance> importProvenance;      ///< 导入溯源（I-REQ-8）
    RequirementReference refFrame;            ///< 参考坐标系（默认 World）
    std::optional<RequirementReference> tcpRef;            ///< 工具/TCP 引用
    BoundingBox box{};                        ///< 区域盒（m；非退化——I-REQ-6）
    PositionSampling positionSampling{};      ///< 位置采样定义（§5.2）
    OrientationSampling orientationSampling{};///< 姿态采样定义（§5.2）
    CoverageTargets coverageTargets{};        ///< 覆盖率目标（∈[0,1]——I-REQ-6）
    RequirementDemand demands{};              ///< 要求值（同任务点）
    std::optional<std::string> sequenceKey;   ///< 顺序键（同任务点语义；R7 校验面归 T05 就绪层）
    std::string note;                         ///< 备注

    bool operator==(const WorkRegion& o) const;
    bool operator!=(const WorkRegion& o) const { return !(*this == o); }
};

/**
 * @brief 工况负载（§4.5 payloads 条目——物性全 SourcedValue 四态；
 *        物性缺失＝NotProvided，DataInsufficient 降级预告，判定归 DYN-06）。
 */
struct ConditionPayload {
    RequirementReference toolRef;             ///< 负载挂载工具（Tool 引用——wellFormed）
    core::SourcedValue<double> mass;          ///< 质量（kg）
    core::SourcedValue<rw::math::Vector3D<double>> com;    ///< 质心（m；工具 TCP 系）
    core::SourcedValue<double> inertia;       ///< 惯量标量（kg·m²；R1 标量口径——张量扩展走需求变更）

    bool operator==(const ConditionPayload& o) const;
    bool operator!=(const ConditionPayload& o) const { return !(*this == o); }
};

/**
 * @brief 工况事件（§4.5 events 条目——夹取/释放/驻留，绑定任务点）。
 */
struct ConditionEvent {
    ConditionEventType type = ConditionEventType::Grasp;  ///< 事件类型（三值词表）
    core::ObjectId stationRef;                ///< 绑定任务点（TaskPoint 子条目 ObjectId——模型内锚，O-36 同口径）
    std::optional<double> durationS;          ///< 持续时间（s；Dwell 有语义，可选）

    bool operator==(const ConditionEvent& o) const;
    bool operator!=(const ConditionEvent& o) const { return !(*this == o); }
};

/**
 * @brief 工况适用范围（§4.5 appliesTo——绑定方向唯一，D-REQ-5：工况侧声明，
 *        点侧不反向登记）。
 */
struct AppliesTo {
    AppliesToScope scope = AppliesToScope::AllStations;  ///< 范围种类（三值词表）
    std::vector<core::ObjectId> stations;     ///< 显式工位清单（仅 Stations 有语义；须非空——§4.7 非法组合行）

    bool operator==(const AppliesTo& o) const;
    bool operator!=(const AppliesTo& o) const { return !(*this == o); }
};

/**
 * @brief 工况条目（§4.5 字段表全量；REQ-04 语义载体——环境/工具/负载/
 *        事件/节拍/要求值/适用范围）。
 */
struct OperatingCondition {
    core::ObjectId objectId;                  ///< 模型内标识（O-36 同口径）
    std::string name;                         ///< 语义名（集合内唯一——I-REQ-3）
    RequirementLevel level = RequirementLevel::Must;  ///< 需求等级（必验派生源——I-REQ-9/§6.2）
    bool enabled = true;                      ///< 启用（必验集合＝enabled∧Must——§6.2 冻结规则）
    std::vector<core::ObjectId> environmentRefs;          ///< 环境障碍引用（scene-object 对象——浅引用）
    std::vector<RequirementReference> toolRefs;           ///< 工具引用（Tool 引用——wellFormed）
    std::vector<ConditionPayload> payloads;   ///< 负载（物性四态——NotProvided 合法）
    std::vector<ConditionEvent> events;       ///< 事件序列（绑定任务点）
    std::optional<double> targetCycleTimeS;   ///< 目标节拍（s；可选——§4.5 processParams）
    RequirementDemand demands{};              ///< 要求值（碰撞/裕量——评估硬条件解释）
    std::optional<std::uint32_t> verificationOrderHint;   ///< 必验顺序建议（纯呈现元数据——D-REQ-8）
    AppliesTo appliesTo{};                    ///< 适用范围（工况侧声明——D-REQ-5）
    std::string note;                         ///< 备注（note 变更也保守失效——R-REQ-2）

    bool operator==(const OperatingCondition& o) const;
    bool operator!=(const OperatingCondition& o) const { return !(*this == o); }
};

/**
 * @brief 采样计划条目（§4.1 req-plan-set 条目＝"逐区域计划（regionRef＋
 *        采样参数）"；§5.2 冻结语义图——planContentIdentity 的载体）。
 *
 * positionSampling 恒为**规范化形态**（Grid 计数即权威——GridBySpacing
 * 在 buildPlan 时规范化，D-REQ-2：同义计划同身份）；regionRef 指向
 * WorkRegion 子条目 ObjectId（模型内锚——plan-set 与 region-set 跨集合
 * 引用，O-36 口径：子条目互引不入 objectRefs）。
 */
struct SamplingPlan {
    core::ObjectId objectId;                  ///< 模型内标识（O-36 同口径）
    core::ObjectId regionRef;                 ///< 目标区域条目（WorkRegion 子条目 ObjectId）
    PositionSampling positionSampling{};      ///< 规范化位置采样（Grid 权威计数——D-REQ-2）
    OrientationSampling orientationSampling{};///< 姿态采样定义（§5.2）
    std::string note;                         ///< 备注

    bool operator==(const SamplingPlan& o) const;
    bool operator!=(const SamplingPlan& o) const { return !(*this == o); }
};

// =====================================================================
// 五对象值模型（§4.1/§4.2——canonical 编码的编码对象全集；variant 备择
// 序＝§4.1 表行序——Codec wireType 依此编号，表尾追加纪律同 token）
// =====================================================================

/**
 * @brief 需求集根对象（§4.2 字段表——编译入口对象/修订闭包锚）。
 *
 * 四个集合引用只持 ObjectId（不含 cv——§4.1 闭包形态行）；无重复（§4.2
 * 字段表行）；note 不入内容身份语义但随对象字节（§4.2——保守失效取舍
 * 同 R-REQ-2）。
 */
struct RequirementSet {
    std::uint32_t schemaVersion = kReqSetSchemaVersion;   ///< 对象 schema 主版本（唯一取值点＝ObjectTypes.hpp 常量）
    std::string name;                                      ///< 需求集语义名（进报告）
    std::optional<core::ObjectId> pointSetRef;             ///< 任务点集合对象引用
    std::optional<core::ObjectId> regionSetRef;            ///< 区域集合对象引用
    std::optional<core::ObjectId> conditionSetRef;         ///< 工况集合对象引用
    std::optional<core::ObjectId> planSetRef;              ///< 采样计划集合对象引用
    std::string note;                                      ///< 备注（随字节、参与保守失效）

    bool operator==(const RequirementSet& o) const;
    bool operator!=(const RequirementSet& o) const { return !(*this == o); }
};

/**
 * @brief 任务点集合对象（§4.2——"四个集合对象结构对称：{entries}"）。
 *
 * 条目按 objectId 规范文本字典序存放（I-REQ-1——canonical 编码确定性）；
 * makeCanonical* 系列工厂负责排序（构造边界规范化，不静默——validate*
 * 校验函数对未排序输入报错，编码器只接受已规范化集合）。
 */
struct PointSet {
    std::uint32_t schemaVersion = kReqPointSetSchemaVersion;  ///< 对象 schema 主版本
    std::vector<TaskPoint> entries;                            ///< 任务点条目（字典序存放——I-REQ-1）

    bool operator==(const PointSet& o) const;
    bool operator!=(const PointSet& o) const { return !(*this == o); }
};

/// @brief 区域集合对象（§4.2——与点集结构对称；条目字典序同 I-REQ-1）。
struct RegionSet {
    std::uint32_t schemaVersion = kReqRegionSetSchemaVersion;  ///< 对象 schema 主版本
    std::vector<WorkRegion> entries;                            ///< 区域条目（字典序——I-REQ-1）

    bool operator==(const RegionSet& o) const;
    bool operator!=(const RegionSet& o) const { return !(*this == o); }
};

/// @brief 工况集合对象（§4.2——结构对称；必验集合解析输入，§6.2）。
struct ConditionSet {
    std::uint32_t schemaVersion = kReqConditionSetSchemaVersion;  ///< 对象 schema 主版本
    std::vector<OperatingCondition> entries;                       ///< 工况条目（字典序——I-REQ-1）

    bool operator==(const ConditionSet& o) const;
    bool operator!=(const ConditionSet& o) const { return !(*this == o); }
};

/// @brief 采样计划集合对象（§4.2——结构对称；planContentIdentity 来源）。
struct PlanSet {
    std::uint32_t schemaVersion = kReqPlanSetSchemaVersion;  ///< 对象 schema 主版本
    std::vector<SamplingPlan> entries;                        ///< 计划条目（字典序——I-REQ-1）

    bool operator==(const PlanSet& o) const;
    bool operator!=(const PlanSet& o) const { return !(*this == o); }
};

/// @brief 需求对象变体（§9.5 IRequirementCodec 编码对象全集；备择序＝
///        §4.1 表行序——wireType 编号依据，表尾追加纪律）。
using RequirementObjectVariant =
    std::variant<RequirementSet, PointSet, RegionSet, ConditionSet, PlanSet>;

// =====================================================================
// 必验冻结 schema（§6.2——P-EV-9 承接的 requirements 侧权威形态；
// 解析实现唯一点＝IOperatingConditionService::resolveRequiredCases）
// =====================================================================

/**
 * @brief 必验集合条目（§6.2 冻结 schema 的 RequiredCaseSet.entries[i]
 *        形态：{caseId: objectId, label: name, enabled, mandatory}）。
 *
 * ★ I-REQ-9（必验派生唯一）的承载面：mandatory 字段只读派生
 * （mandatory ≡ (level==Must)）——本结构没有"设置 mandatory"的构造
 * 通道（resolveRequiredCases 是唯一产出点，P-EV-9 唯一实现点）；无独立
 * 第三"必验"开关。
 */
struct RequiredCaseEntry {
    core::ObjectId caseId;      ///< 工况条目 ObjectId（§6.2 caseId）
    std::string label;          ///< 人读标签（§6.2 label＝工况 name——D-REQ-6 语义字段）
    bool enabled = false;       ///< 用户启用开关原值（§6.2 enabled）
    bool mandatory = false;     ///< 派生必验标记（≡ level==Must——只读派生，I-REQ-9）

    bool operator==(const RequiredCaseEntry& o) const
    {
        return caseId == o.caseId && label == o.label && enabled == o.enabled
            && mandatory == o.mandatory;
    }
    bool operator!=(const RequiredCaseEntry& o) const { return !(*this == o); }
};

/**
 * @brief 必验集合解析产出（§6.2 冻结解析规则的值承载）。
 *
 * entries 序＝输入工况集合的规范序（ObjectId 字典序——确定性）；全量
 * 条目（含 Should/未启用）都入 entries（§6.2 schema：entries 为工况集
 * 合的投影），必验集合＝{enabled ∧ mandatory} 子集（required 面由消费方
 * 过滤或直接看 mandatory∧enabled 组合）。
 */
struct RequiredCaseResolution {
    std::vector<RequiredCaseEntry> entries;   ///< 全量投影（规范序——确定性）
    std::size_t requiredCount = 0;            ///< 必验计数（enabled∧Must——解析确定性的计数面）

    bool operator==(const RequiredCaseResolution& o) const;
    bool operator!=(const RequiredCaseResolution& o) const { return !(*this == o); }
};

/**
 * @brief 需求档派生视图（§4.8——集合级只读投影；DerivedReadOnly 来源，
 *        **不入 canonical 编码**（重算即得），供 evidence 组装与 UI 摘要）。
 *
 * contentIdentity 是派生档自身的"内容指纹"（对派生输入的 canonical 投影
 * 再摘要——core::ContentDigester），随派生输入变化；不是任何持久化对象
 * 的 ContentVersion（那归 project 对对象字节计算，§4.8）。
 */
struct RequirementProfile {
    std::vector<RequiredCaseEntry> requiredCases;  ///< 必验工况清单（enabled∧Must——§6.2 解析复用）
    std::size_t mustCount = 0;                     ///< Must 条目计数（全集合汇总）
    std::size_t shouldCount = 0;                   ///< Should 条目计数
    double minPositionCoverage = 0.0;              ///< 覆盖目标汇总：位置覆盖率下限的最小值（各区域 coverageTargets 汇总）
    core::ContentIdentity contentIdentity{};       ///< 派生档内容指纹（重算即得——不入 canonical 编码）

    bool operator==(const RequirementProfile& o) const;
    bool operator!=(const RequirementProfile& o) const { return !(*this == o); }
};

// =====================================================================
// 不变量校验层（§4.7 I-REQ-1~10 的实现面——Services 构造边界与 Codec
// 解码校验链共用的语义单源，NFR-MNT-04；全部纯函数、无诊断产出——错误
// 经 RequirementError 值面返回，§9.2 总则）
// =====================================================================

/**
 * @brief I-REQ-5 前半：受约束分量至少一真＋位姿约束全项合法。
 *
 * 校验序（短路——首个失败即返回，错误码定位首个违例）：
 *   ①constrainedDof.any()==false → AllDofFree（I-REQ-5）；
 *   ②orientation 规则参数非法（零向量/逆序/缺 feature/非有限角）→
 *     ZeroVectorTarget 等对应码（validateOrientationRule 逐条）。
 *
 * @param pose [in] 待校验位姿约束
 * @return 合法＝nullopt；非法＝首个违例错误（调用方决定呈现方式）
 *
 * 纯函数；线程安全；确定性。
 */
std::optional<RequirementError> validatePoseConstraint(const PoseConstraint& pose);

/**
 * @brief I-REQ-5 后半：容差两分量 >0 且有限。
 *
 * @param tolerance [in] 待校验容差
 * @return 合法＝nullopt；非法＝IllegalTolerance（params 携字段名与原值）
 *
 * 纯函数；线程安全；确定性。
 */
std::optional<RequirementError> validateTolerance(const ToleranceSpec& tolerance);

/**
 * @brief I-REQ-6 前半：姿态规则参数合法性（五规则逐条——acceptance 4
 *        "五规则姿态字段模型非法参数构造边界拒绝"的实现点）。
 *
 * 逐规则反例面（V-03 构造侧）：
 *   - Fixed：fixedRpy 三分量非有限 → 非法（码 IllegalTolerance 族的参数
 *     面不复用——见返回值约定）；
 *   - AlignFrame：targetFrame 结构自洽失败（wellFormed）→ 非法；
 *   - AlignGeometryNormal：feature 未显式给出 → 非法（"缺 feature"）；
 *   - PointAtTarget：targetPoint 零向量 → ZeroVectorTarget；
 *   - ToolRollFree：rollRange 逆序/空区间 → 非法。
 *
 * @param rule [in] 待校验姿态规则
 * @return 合法＝nullopt；非法＝RequirementError——零向量目标用
 *         ZeroVectorTarget（域错误表既有值）；其余参数非法（非有限角/
 *         引用结构违约/缺 feature/rollRange 逆序）统一用 IllegalTolerance
 *         承载并以 params "field" 定位具体字段（域错误表 8 值内"参数非法"
 *         族的就近承载——精确的用户定位诊断 REQ-READY-POSE-ILLEGAL 归
 *         T05 就绪层登记，本值面只做机器可判拒绝）
 *
 * 纯函数；线程安全；确定性。
 */
std::optional<RequirementError> validateOrientationRule(const OrientationRule& rule);

/**
 * @brief I-REQ-6 后半：区域盒非退化（size 三分量 >0 且有限）。
 *
 * @param box [in] 待校验区域盒（m）
 * @return 合法＝nullopt；非法＝DegenerateRegion（params 携轴名与原值）
 *
 * 纯函数；线程安全；确定性。
 */
std::optional<RequirementError> validateBoundingBox(const BoundingBox& box);

/**
 * @brief I-REQ-6 后半：覆盖率 ∈[0,1]。
 *
 * @param targets [in] 待校验覆盖率目标
 * @return 合法＝nullopt；非法＝DegenerateRegion（params 携字段名——
 *         覆盖率越界属区域参数退化族；域错误表内就近承载）
 *
 * 纯函数；线程安全；确定性。
 */
std::optional<RequirementError> validateCoverageTargets(const CoverageTargets& targets);

/**
 * @brief I-REQ-4 结构半区：引用结构自洽＋kind↔token 匹配表可满足。
 *
 * "结构半区"含义：本函数只核验 RequirementReference 自身（载荷与 kind
 * 匹配、场景允许 kind——scene ∈ {World, ModelFrame, SceneObject}／tcp ∈
 * {Tool, DefaultTcp}）；"跨闭包半区"（目标 ObjectId 存在于修订闭包且
 * objectRefs 登记 token 与 expectedTargetToken 一致）归就绪校验器
 * （T05 R1/R8——§8.1 浅校验，本单元拿不到闭包）。
 *
 * @param ref      [in] 待校验引用
 * @param forScene [in] true＝refFrame 场景（允许 World/ModelFrame/
 *                 SceneObject）；false＝tcpRef 场景（允许 Tool/DefaultTcp）
 * @return 合法＝nullopt；非法＝IllegalTolerance 携 params "ref-kind"
 *         （载荷违约——kind 与载荷字段不匹配）或 "allowed-kinds"（kind
 *         不在该使用场景词表）——域错误表"参数非法"族就近承载；跨闭包
 *         的悬空/token 失配定位诊断 REQ-READY-REF-MISSING 归 T05 就绪层
 *         （§9.6），本值面只做结构可判拒绝
 *
 * 纯函数；线程安全；确定性。
 */
std::optional<RequirementError> validateRequirementReference(const RequirementReference& ref,
                                                             bool forScene);

// =====================================================================
// 集合规范化与集合级不变量（I-REQ-1/2/3——编解码与编辑器共用）
// =====================================================================

/**
 * @brief I-REQ-1：条目按 ObjectId 规范文本字典序排序（就地规范化）。
 *
 * 排序键＝ObjectId::toCanonical() 的字节字典序（"obj-<32hex>"——稳定、
 * 全 ASCII、无 locale 依赖，NFR-COR-02）。稳定排序（同 id 不存在——
 * I-REQ-2 保证唯一；即便调用方违约注入重复 id，stable_sort 保持输入序
 * 可复现，重复检测由 checkEntryIdUniqueness 兜底）。
 *
 * @tparam Entry 条目类型（须含 ObjectId objectId 成员——TaskPoint/
 *         WorkRegion/OperatingCondition/SamplingPlan 四者）
 * @param entries [in,out] 条目数组（就地排序）
 *
 * 纯函数（除就地排序副作用）；线程安全（仅操作入参）；确定性。
 */
template <class Entry>
void sortEntriesByObjectId(std::vector<Entry>& entries)
{
    std::stable_sort(entries.begin(), entries.end(),
                     [](const Entry& a, const Entry& b) {
                         return a.objectId.toCanonical() < b.objectId.toCanonical();
                     });
}

/**
 * @brief I-REQ-1：集合是否已处规范序（canonical 编码前置——编码器只接受
 *        已规范化集合，未排序输入拒绝不静默重排——NFR-COR-03）。
 *
 * @tparam Entry 条目类型（含 ObjectId objectId 成员）
 * @param entries [in] 条目数组
 * @return true＝已按 ObjectId 规范文本字典序升序（含空/单元素——平凡真）
 *
 * 纯函数；线程安全；确定性。
 */
template <class Entry>
bool isCanonicalOrder(const std::vector<Entry>& entries)
{
    for (std::size_t i = 1; i < entries.size(); ++i) {
        if (entries[i - 1].objectId.toCanonical() > entries[i].objectId.toCanonical()) {
            return false;
        }
    }
    return true;
}

/**
 * @brief I-REQ-3＋I-REQ-2（集合内半区）：条目名唯一＋条目 id 唯一。
 *
 * 名称唯一（I-REQ-3）是集合内约束；id 唯一在此为集合内防重（I-REQ-2 的
 * 跨集合半区——同 id 出现在不同集合——归 validateWorkingSet 跨集合核对）。
 * 空名视为非法（名称进报告/覆盖清单定位——空名无定位意义，§8.1 R0
 * "条目 id/name 非空唯一"行）。
 *
 * @tparam Entry 条目类型（含 ObjectId objectId 与 std::string name 成员）
 * @param entries [in] 条目数组（任意序）
 * @return 合法＝nullopt；首个违例＝DuplicateName（重复名/空名）或
 *         MalformedPayload（重复 id——身份冲突属字节级违约；params 携
 *         name/object-id 定位）
 *
 * 纯函数；线程安全；确定性（扫描序＝输入序，报首个）。
 */
template <class Entry>
std::optional<RequirementError> checkEntryNameAndIdUniqueness(const std::vector<Entry>& entries)
{
    // 逐对扫描（O(n²)——集合规模点集数千条时 10⁶ 量级比较可接受；有序
    // 哈希会引入遍历序不确定性，逐对扫描保序报首个违例，确定性优先。
    // 性能实测超 NFR-PERF-01 再优化——R-REQ-3 同源取舍）。
    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (entries[i].name.empty()) {
            RequirementError e;
            e.code = RequirementErrorCode::DuplicateName;
            e.params.emplace_back("index", std::to_string(i));
            e.detail = "requirements/types: 条目名称为空（报告/覆盖清单定位依据——"
                       "空名无定位意义，§8.1 R0）";
            return e;
        }
        for (std::size_t j = 0; j < i; ++j) {
            if (entries[i].name == entries[j].name) {
                RequirementError e;
                e.code = RequirementErrorCode::DuplicateName;
                e.params.emplace_back("name", entries[i].name);
                e.detail = "requirements/types: 集合内名称重复（I-REQ-3——构造边界"
                           "拒绝，不静默加后缀，NFR-COR-03）";
                return e;
            }
        }
    }
    for (std::size_t i = 0; i < entries.size(); ++i) {
        for (std::size_t j = 0; j < i; ++j) {
            if (entries[i].objectId == entries[j].objectId) {
                RequirementError e;
                e.code = RequirementErrorCode::MalformedPayload;
                e.params.emplace_back("object-id", entries[i].objectId.toCanonical());
                e.detail = "requirements/types: 集合内条目 ObjectId 重复（I-REQ-2——"
                           "模型内标识必须唯一）";
                return e;
            }
        }
    }
    return std::nullopt;
}

/**
 * @brief I-REQ-2 跨集合半区：同一 ObjectId 不得出现在两个集合条目中
 *        （身份不混用——token 与所属集合一致）。
 *
 * @param points     [in] 任务点条目（任意序）
 * @param regions    [in] 区域条目
 * @param conditions [in] 工况条目
 * @param plans      [in] 计划条目
 * @return 合法＝nullopt；跨集合重复＝MalformedPayload（params 携
 *         object-id；不定位"哪两个集合"以外的额外语义——定位足矣）
 *
 * 纯函数；线程安全；确定性（扫描序＝参数序）。
 */
std::optional<RequirementError> checkCrossSetIdUniqueness(const std::vector<TaskPoint>& points,
                                                          const std::vector<WorkRegion>& regions,
                                                          const std::vector<OperatingCondition>& conditions,
                                                          const std::vector<SamplingPlan>& plans);

/**
 * @brief 条目级全量校验（Services 构造边界与 Codec 解码校验链共用的
 *        单条目入口——逐字段调 validate* 系列并附集合唯一性）。
 *
 * @param point [in] 待校验任务点（不查集合唯一性——那是集合级入口
 *              checkEntryNameAndIdUniqueness 的职责）
 * @return 合法＝nullopt；非法＝首个违例错误（校验序：引用→位姿→容差
 *         →三段距离；短路——NFR-COR-03 不产出半成品诊断清单）
 *
 * 纯函数；线程安全；确定性。
 */
std::optional<RequirementError> validateTaskPoint(const TaskPoint& point);

/// @brief 区域条目级校验（引用→盒→采样定义→覆盖率；短路同上）。
std::optional<RequirementError> validateWorkRegion(const WorkRegion& region);

/// @brief 工况条目级校验（引用→appliesTo→负载/事件基本面；短路同上）。
std::optional<RequirementError> validateOperatingCondition(const OperatingCondition& condition);

/// @brief 计划条目级校验（位置采样规范化形态＋姿态采样 ≥1——§5.2）。
std::optional<RequirementError> validateSamplingPlan(const SamplingPlan& plan);

}  // namespace sdurws::ird::requirements

#endif  // IRD_REQUIREMENTS_REQUIREMENTTYPES_HPP
