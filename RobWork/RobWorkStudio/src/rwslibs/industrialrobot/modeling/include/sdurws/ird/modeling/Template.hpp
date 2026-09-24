/**
 * @file   Template.hpp
 * @brief  模板创建与参数化编辑——IRobotDesignTemplateFactory（模板清单/
 *         草稿创建，§5.1/§9.4.2）、六轴默认参数表 T-MDL-1（设计默认值）、
 *         建模工作集 ModelingWorkingSet（v1 值模型）、创建入口链型守卫
 *         （§6.4 维度二判定复用）、逐轴编辑流（§5.2 字段级/批量变体）、
 *         基座安装姿态编辑流（§5.2 SetBasePlacement——WP-13-T11）与
 *         几何生成辅助两条（§5.2 v0.2 增补）。
 *
 * 设计依据：
 *   - units/modeling.md §5.1（模板清单三行/表 T-MDL-1/创建流程——"纯函数
 *     产出 ModelingWorkingSet 草稿…不触达 project、不产生修订"）、§9.4.2
 *     （IRobotDesignTemplateFactory 签名契约：listTemplates/createDraft、
 *     @pre/@post/@错误）、§5.2（参数化编辑：字段级/批量变体/变更摘要；
 *     几何生成辅助两条）、§6.4 尾段（链型判定"同一判定被模板创建入口
 *     复用"）、§3.3（公共头表 Template.hpp 行——T07）
 *   - 需求 MDL-01（模板创建与逐轴定义）、MDL-04（基座/几何默认承接）、
 *     MDL-22（基座未显式配置＝地面——默认值在模板层填入并带来源标记，
 *     V15-04）、PM-01（模板→草稿→创建确认）、ARC-05/NFR-MNT-07（行程
 *     上限阈值归 policy——本地不设第二 4π 常量）、P-03/O-27（七轴模板
 *     工程数值未冻结——仅登记不启用，DTB §4.3）
 *   - 任务契约 tasks/foundation/WP-13-T07.json acceptance 1~5
 *
 * 背景说明（模板工厂在产品里的位置）：新建向导（workflow/ui，PM-01）的
 * 建模侧入口——向导从 listTemplates() 取清单呈现给用户；用户选定模板与
 * 安装预设并命名后，createDraft 纯函数产出初始工作集（草稿），用户在
 * 编辑器中逐轴编辑、最后经"创建确认"走 apply-robot-design 命令端口产生
 * 首个可编辑基线修订（§5.1 创建流程图）。本单元职责止于草稿：**不触达
 * project、不产生修订、不留半成品**（取消＝丢弃草稿，无事务残留）。
 *
 * P-03/O-27 纪律（knownPitfalls 处置，DTB §4.3）：七轴模板 generic-7r
 * **仅登记不启用**（enabled=false，创建入口阻止并提示——AT-20 向导语义
 * 建模侧）；本单元任何代码不使用、不猜测七轴工程数值（P-03 冻结前不存在
 * 可用数值——拒绝路径先于任何建链动作，无半成品）。
 *
 * 线程安全：工厂与全部自由函数为纯函数服务（§3.4 总约定 1）——无共享
 * 可变状态、可重入、多线程并发调用安全。确定性（NFR-COR-01/02）：不读
 * 环境/时钟/locale/文件系统；对象身份用确定性派生的临时句柄（§5.2，见
 * ModelingWorkingSet 注）——同输入字节→同工作集。
 */

#ifndef IRD_MODELING_TEMPLATE_HPP
#define IRD_MODELING_TEMPLATE_HPP

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <rw/math/Rotation3D.hpp>   // Rotation3D<double>——几何辅助的旋转半部（逐元素解析式构造）
#include <rw/math/Transform3D.hpp>  // Transform3D<double>——GeometryRef.localTransform（m/rad）
#include <rw/math/Vector3D.hpp>     // Vector3D<double>——轴线/占位段端点（m；无量纲轴线）

#include <sdurws/ird/core/DiagData.hpp>     // core::DiagnosticRecord（diags 输出参数元素类型）
#include <sdurws/ird/core/Identity.hpp>    // core::ObjectId（rootObjectId/部件对象身份——v0.9 增量显式引用）
#include <sdurws/ird/core/Provenance.hpp>   // core::ValueProvenance（来源标记——GeometricEstimate 等）
#include <sdurws/ird/modeling/Errors.hpp>   // ModelingError/ModelingErrorCode（TemplateDisabled|IllegalName 值面）
#include <sdurws/ird/modeling/Import.hpp>   // ChainCapability（创建入口守卫的结论承载——§6.4 判定复用）
#include <sdurws/ird/modeling/Parts.hpp>   // ToolDefinition/SceneObject/PoseSet/DrivetrainDesign（工作集闭包部件视图——v0.9 增量）
#include <sdurws/ird/modeling/RobotDesign.hpp>  // RobotDesign/JointEntry/JointLimits 等（工作集值模型）
#include <sdurws/ird/runtime/Errors.hpp>    // runtime::Expected 模板（两态结果载体——登记边 runtime）

namespace sdurws::ird::modeling {

// =====================================================================
// 模板登记（§5.1 清单——id 常量唯一书写点；描述符；清单函数）
// =====================================================================

/**
 * @brief 模板登记 id（§9.4.2 createDraft 首参类型——静态目录的稳定
 *        token，取值＝kTemplateId* 常量词表）。
 *
 * ★ 有意不用 core 强类型 Id128：模板 id 是**编译期固定目录键**（清单三行
 * 词表），不是持久化对象身份（无 obj- 语义、不经 ObjectId 分配/解析）——
 * std::string 承载即可，词表约束由 kTemplateId* 常量登记簿承担（ARC-04
 * 强类型纪律覆盖的是持久身份，不扩到静态目录 token）。
 */
using TemplateId = std::string;

/// @brief 模板登记 id："generic-6r"（六轴通用串联——R1 可用）。
///        唯一书写点：描述符清单与 UT 共用本常量，禁第二处字面量
///        （NFR-COR-02 拼写漂移在源头切断；ObjectTypes.hpp 登记簿同款）。
inline constexpr std::string_view kTemplateIdGeneric6R = "generic-6r";

/// @brief 模板登记 id："generic-7r"（七轴串联——P-03 冻结前 enabled=false，
///        仅登记不启用；O-27 处置口径，DTB §4.3）。
inline constexpr std::string_view kTemplateIdGeneric7R = "generic-7r";

/// @brief 模板登记 id："custom-chain"（自定义链——R1 可用，用户逐轴定义）。
inline constexpr std::string_view kTemplateIdCustomChain = "custom-chain";

/**
 * @brief 模板描述符（§5.1 清单表行——listTemplates() 的元素；新建向导的
 *        清单页数据源）。
 *
 * 字段＝§5.1 表列的逐列落地：模板 id／呈现名／轴数／权威模式／安装预设
 * 选项／启用状态与冻结登记注记。axisCount 用 optional 表达"逐轴定义"：
 * generic-6r=6、generic-7r=7、custom-chain=nullopt（用户逐轴添加——清单
 * 面不承诺固定轴数，而非 0；0 会被误读为"零轴模型"，optional 无此歧义）。
 *
 * enabled 的语义（P-03/O-27）：true＝R1 可用；false＝仅登记不启用——
 * 清单页可展示但创建入口必须阻止（createDraft 返回 TemplateDisabled＋
 * 提示诊断），**不得静默替换为其它模板**（V-02）。
 *
 * 生命周期：纯值类型；listTemplates() 每次调用返回新清单（调用方所有）。
 */
struct TemplateDescriptor {
    TemplateId templateId;  ///< 模板登记 id（kTemplateId* 常量之一；清单内唯一）
    std::string displayName;  ///< 呈现名（§5.1 表第 1 列中文名；UX-02 呈现面）

    /// 固定轴数（6/7）；nullopt＝逐轴定义（custom-chain——见类型注）。
    std::optional<std::uint32_t> axisCount;

    AuthorityMode authority = AuthorityMode::Explicit;  ///< 权威模式（§5.1 表第 3 列：三模板均 Explicit——MDL-02 七轴默认且锁定显式）

    /// 安装预设选项（§5.1 表第 4 列：ground/inverted/wall 三值；custom 不
    /// 在模板预设内——Custom 需用户给 customEaa，属创建后编辑，见 createDraft @pre）。
    std::vector<runtime::InstallationPresetToken> installationPresets;

    bool enabled = true;  ///< 是否启用（false＝仅登记不启用——P-03 冻结门）
    std::string note;     ///< 冻结状态登记注记（§5.1 表第 5 列；如 P-03 未冻结说明——呈现/追溯面）

    bool operator==(const TemplateDescriptor& o) const
    {
        return templateId == o.templateId && displayName == o.displayName
               && axisCount == o.axisCount && authority == o.authority
               && installationPresets == o.installationPresets
               && enabled == o.enabled && note == o.note;
    }
    bool operator!=(const TemplateDescriptor& o) const { return !(*this == o); }
};

// =====================================================================
// 六轴默认参数表 T-MDL-1（§5.1——设计默认值，D-MDL-7；非上游冻结需求值；
// 数值锁定随 WP-13-T16 mdl-template-6r 黄金数据集）
// =====================================================================

/**
 * @brief 六轴模板逐轴默认参数（表 T-MDL-1 的一行——§3.3 头表"六轴默认
 *        参数表"的载体；D-MDL-7 设计默认值）。
 *
 * 字段＝表列逐列：type/axis/zeroOffset/bounds/maxVel/maxAcc。两点落位说明：
 *   - **workingRange 无字段**：表 T-MDL-1 该列全行为"—"（不适用）——
 *     Revolute 关节的 workingRange 语义即 NotApplicable（§4.3-A"仅
 *     Continuous"），建链时直接落 NotApplicable 态，本结构不携带空列；
 *   - **maxVel/maxAcc 在 JointEntry schema 中无落点**（§4.3-A 未登记该
 *     二字段——单元卡 §15 v0.4 ③c"不发明 schema"纪律），数值随本表
 *     暴露给消费方（向导预填/黄金数据集核对），**不静默丢弃**；其 schema
 *     归属随消费方任务按卡面修订澄清（v0.4 ③c 已登记的边界）。
 *
 * 单位：zeroOffset/bounds 为 rad；maxVelocity 为 rad·s⁻¹；maxAcceleration
 * 为 rad·s⁻²；axis 为连杆系下的单位轴（无量纲）。全部数值必须有限。
 */
struct SixAxisJointSpec {
    JointType type = JointType::Revolute;  ///< 关节类型（表 T-MDL-1 六行全 Revolute）
    rw::math::Vector3D<double> axis{0.0, 0.0, 1.0};  ///< 轴线（连杆系下单位轴，无量纲）
    double zeroOffset = 0.0;  ///< 零位偏置，单位 rad（表值全 0）
    JointLimits bounds{0.0, 0.0};  ///< 限位 {qmin,qmax}，单位 rad（I-MDL-4：qmin<qmax）
    double maxVelocity = 0.0;      ///< 最大速度，单位 rad·s⁻¹（schema 落点说明见类型注）
    double maxAcceleration = 0.0;  ///< 最大加速度，单位 rad·s⁻²（同上）

    bool operator==(const SixAxisJointSpec& o) const noexcept
    {
        return type == o.type && axis == o.axis && zeroOffset == o.zeroOffset
               && bounds == o.bounds && maxVelocity == o.maxVelocity
               && maxAcceleration == o.maxAcceleration;
    }
    bool operator!=(const SixAxisJointSpec& o) const noexcept { return !(*this == o); }
};

/**
 * @brief 六轴模板默认参数表 T-MDL-1（§5.1 表——D-MDL-7 设计默认值）。
 *
 * 表值（单位 rad／rad·s⁻¹／rad·s⁻²；axis 为连杆系下单位轴）：
 *   J1 (0,0,1) [−π,+π]   v=π  a=2π
 *   J2 (0,1,0) [−π/2,+π/2] v=π  a=2π
 *   J3 (0,1,0) [−π,+π/2] v=π  a=2π
 *   J4 (1,0,0) [−π,+π]   v=2π a=4π
 *   J5 (0,1,0) [−π/2,+π/2] v=2π a=4π
 *   J6 (1,0,0) [−2π,+2π] v=2π a=4π
 *
 * J6 行程恰为 4π＝行程上限阈值边界（附录 D 第 11 项/D-08：行程≤阈值含于
 * 合规侧）——默认通过行程上限策略校验，兼作边界用例。★ 阈值常量本身归
 * policy（ARC-05/NFR-MNT-07：本地不设第二 4π 常量）——本单元只保证表值
 * 的行程**等于** 4π 这一事实，合规判定归就绪校验（T08，阈值经④端口只读）。
 *
 * @return 六行默认参数（数组下标 i＝第 i+1 轴——链序；每次调用返回同值
 *         新数组——纯函数）
 *
 * 纯函数；线程安全；确定性（NFR-COR-02：固定表，同序同值）。
 */
std::array<SixAxisJointSpec, 6> sixAxisTemplateDefaults();

// =====================================================================
// 建模工作集 ModelingWorkingSet（v1 值模型——§9.4.1/§9.4.2 的共享承载；
// WP-13-T07 首个消费者）
// =====================================================================

/**
 * @brief 一条变更摘要记录（§5.2/§9.4.1 buildChangeSummary 的承载单元）。
 *
 * 产生规则（UX-05）：一次字段级编辑＝一条记录；一次批量变体调用＝**一条**
 * 记录（"批量产出单条变更摘要"——批量行明细在记录 summary 内说明）。
 * 记录按编辑发生序追加（确定性序），只增不改（草稿内存态的 append-only
 * ——与 PA-2 修订只增不改同纪律的编辑态投影）。
 */
struct ModelingChangeRecord {
    std::string subject;  ///< 定位路径（如 "joints[1]"——字段下标精确到位）
    std::string summary;  ///< 人读中文摘要（一行；命令提交时并入命令摘要——§9.4.1）

    bool operator==(const ModelingChangeRecord& o) const noexcept
    {
        return subject == o.subject && summary == o.summary;
    }
    bool operator!=(const ModelingChangeRecord& o) const noexcept { return !(*this == o); }
};

/**
 * @brief 建模工作集（§9.4.1 类注"已应用基线闭包的只读视图＋未应用编辑
 *        差值的演算结果"的 **v1 值模型**；§4.9 编辑态载体）。
 *
 * v1 范围（WP-13-T07 落位）：design＝模板创建/编辑流演算出的权威参数化
 * 结果；changes＝自创建以来的变更摘要记录。T08 编辑器将扩展基线闭包视图/
 * 编辑差值/undo 栈等编辑器内部态（§9.4.1 完整契约）——内部态不入本值，
 * 本结构只承载可移交、可比对、可确定性重放的演算结果。
 *
 * ★ 身份承诺（§4.9 编辑态行）：工作集**无身份承诺**（不得冒充快照——
 * evidence.md §4.1.1）。design 内的关节/连杆 ObjectId 为**确定性派生的
 * 临时句柄**（§5.2"内存编辑态先用临时句柄，提交时回填"——SHA-256 派生，
 * 同输入同句柄、不经随机源；真实 ObjectId 仍由命令 prepare 阶段经
 * HandlerContext.objectId() 分配回填，PA-1——T05 导入路径 v0.6 ②i 同款
 * 纪律）。消费方不得把临时句柄当持久身份外泄（草稿载荷提交时整体回填）。
 *
 * v0.9 增量（WP-13-T08 落位，§14.6 登记——闭包部件对象视图，向后兼容的
 * 表尾追加）：T08 就绪校验（§8.2 L1/L5/L7/L9 层）与命令 prepare 断言需要
 * 修订闭包内**部件对象**（工具/场景/位姿集/传动）参与判定（引用存在性、
 * 工具物性、传动合法性、行程上限的关节表装配），而 v1 只有根对象。本增量
 * 以**类型化值视图**承载（rootObjectId＋partObjects），语义仍是"可移交、
 * 可比对、可确定性重放的演算结果"：模板创建的初始工作集两新字段为缺省值
 * （无根身份/无部件——模板阶段尚无 project 对象身份），既有 v1 消费方
 * （T07 用例与聚合初始化）不受影响；编辑器内部态（undo 栈/编辑差值）仍
 * 不入本值（§9.4.1 边界不变）。
 *
 * 线程约束：**非线程安全**——编辑态仅 UI 线程访问（§3.4 总约定 2；本头
 * 的编辑流函数也只在该约束下使用）。确定性：创建/编辑/摘要全部为确定性
 * 纯函数（同输入序列→同工作集字节）。
 */
struct ModelingWorkingSet {
    RobotDesign design;  ///< 编辑态演算结果（§4.3 值模型；模板路径含 T-MDL-1 种子与 BasePlacement）

    /// 变更摘要记录（编辑发生序；批量变体＝一条——见 ModelingChangeRecord 注）。
    std::vector<ModelingChangeRecord> changes;

    /// 根对象在修订闭包中的身份（v0.9 增量；nullopt＝尚无 project 对象
    /// 身份——模板创建的初始草稿；命令 prepare 回填后恒有值）。
    std::optional<core::ObjectId> rootObjectId;

    /// 修订闭包内已解码的部件对象值视图（v0.9 增量；§4.2 五对象表的后四
    /// 行——工具/场景/位姿集/传动；根对象不入本表，其值即 design）。各值
    /// 自带 objectId（Parts.hpp 值模型字段）；"指向闭包存在对象"的就绪
    /// 判定（L1）以本表＋rootObjectId 为闭包视图。
    std::vector<ToolDefinition> toolObjects;
    std::vector<SceneObject> sceneObjects;
    std::optional<PoseSet> poseSetObject;          ///< 至多一份（§4.6——与根 poseSetRef 对应）
    std::optional<DrivetrainDesign> drivetrainObject;  ///< 至多一份（§4.7——与根 drivetrainRef 对应）

    bool operator==(const ModelingWorkingSet& o) const
    {
        return design == o.design && changes == o.changes
            && rootObjectId == o.rootObjectId
            && toolObjects == o.toolObjects && sceneObjects == o.sceneObjects
            && poseSetObject == o.poseSetObject && drivetrainObject == o.drivetrainObject;
    }
    bool operator!=(const ModelingWorkingSet& o) const { return !(*this == o); }
};

/**
 * @brief 生成自创建以来的变更摘要（§9.4.1 buildChangeSummary 的域级实现
 *        ——人读中文；命令提交时并入命令摘要）。
 *
 * 格式（确定性——NFR-COR-02）：每条记录一行（"[序号] subject：summary"，
 * 序号从 1 起、按记录序）；无变更返回空串（调用方自行决定"无变更"呈现）。
 *
 * @param ws [in] 工作集（只读）
 * @return 摘要文本（UTF-8 中文；行数＝changes.size()）
 *
 * 纯函数；线程安全；确定性。
 */
std::string buildChangeSummary(const ModelingWorkingSet& ws);

// =====================================================================
// IRobotDesignTemplateFactory——模板工厂接口（§9.4.2）与唯一产品实现
// =====================================================================

/**
 * @brief 模板创建结果两态（§9.4.2 createDraft 的落位形态）。
 *
 * 复用 runtime::Expected 模板（登记边 runtime 的公共两态设施；Codec.hpp
 * modeling::Expected/PropertyEstimation EstimateOutcome 同款先例——同一份
 * 模板，不存在"第二份两态机制"）。错误侧＝ModelingError（域错误面：
 * TemplateDisabled|IllegalName——§9.4.2 @错误 行）；成功侧＝完整工作集。
 * 卡面签名的直返形态随单元卡 §15 v0.8 增量修订为本两态形态（§9.4 前言
 * "一切接口方法非异常出口（返回结果对象＋诊断列表）"的落位——错误经值
 * 面返回，不抛越过单元边界的异常；正常业务拒绝走值面，调用方契约违约才
 * fail-fast，各 @pre 注逐条标明）。
 */
using TemplateOutcome = runtime::Expected<ModelingWorkingSet, ModelingError>;

/**
 * @brief 模板工厂：纯函数服务（无状态、可重入、确定性——§9.4.2 类注原文）。
 *
 * 调用方＝新建向导（workflow/ui，PM-01）。副作用边界：仅内存、不触达
 * project、不产生修订（§9.4.2 @post——PA-1 权威唯一：修订与写路径归
 * project，本工厂只产出草稿工作集）。
 */
class IRobotDesignTemplateFactory {
public:
    virtual ~IRobotDesignTemplateFactory() = default;

    /**
     * @brief 模板清单（§9.4.2 签名——§5.1 清单表的三行）。
     *
     * @return 三行描述符（generic-6r/generic-7r/custom-chain；清单序＝
     *         §5.1 表行序——确定性）。七轴行 enabled=false（P-03 冻结前
     *         仅登记不启用——O-27 处置；向导可展示但创建入口阻止）。
     *
     * 纯函数；线程安全；确定性（NFR-COR-02：同调用同清单）。
     */
    virtual std::vector<TemplateDescriptor> listTemplates() const = 0;

    /**
     * @brief 由模板创建初始工作集（§9.4.2 签名——含 BasePlacement 预设、
     *        默认参数表 T-MDL-1、来源标记；MDL-01/22/PM-01）。
     *
     * 行为（按检查序——序的设计意图见各步内注释）：
     *   步① templateId 存在性（@pre"templateId 存在"）：清单外 id＝调用方
     *       契约违约（向导只允许呈现清单内 id）——抛 std::invalid_argument
     *       fail-fast（AGENTS 错误语义：调用方错误 fail-fast；
     *       PropertyEstimation 同款先例）。
     *   步② enabled 门（P-03/O-27）：disabled 模板→拒绝并追加
     *       MDL-TEMPLATE-DISABLED 提示诊断（§9.5 T07 行；附定位诊断——
     *       §9.4.2 @错误 行），返回 TemplateDisabled 值面错误；**不静默
     *       替换为六轴、不产出半成品**（V-02/AT-20 建模侧）。
     *   步③ localName 校验（§9.4.2 @pre"非空且合法字符集"）：空串或含
     *       [A-Za-z0-9_.-] 之外字符→IllegalName 值面错误（无定位诊断——
     *       §9.5 尚无该域错误的独立码行，Errors.hpp 阶段纪律：nullopt
     *       映射＝不得产诊断；定位信息在错误 params 内）。
     *   步④ 建链（§5.1）：generic-6r 按 T-MDL-1 六行建 6×Revolute＋7 连杆；
     *       custom-chain 产 1 轴种子（T-MDL-1 J1 行为种子默认——用户逐轴
     *       编辑/增轴经编辑器 T08）；BasePlacement 填预设＋零位（MDL-22
     *       V15-04 默认值在模板层填入并带来源标记）。模板提供的全部
     *       SourcedValue 来源标记＝UserProvided＋methodTag
     *       "template/<templateId>"（用户选定模板即其基线选择——§5.1
     *       "BasePlacement 来源=UserProvided/Template"）。连杆材料种子＝
     *       钢 7850 kg/m³（§5.1"连杆几何默认"句的材料半句；密度值取
     *       §5.3 材料密度默认表——单一权威，不写第二处字面量）。
     *   步⑤ 出口守卫：对已建链跑维度二判定（judgeChainCapability——§6.4
     *       单一实现）作内部一致性检查：generic-6r 必须 FullTemplateRange
     *       （否则＝实现缺陷，fail-fast）；custom-chain 种子轴数不在
     *       六/七轴范围属**预期**（链未完成——创建入口的链型阻断发生在
     *       创建确认时对完整声明链调用 creationEntryGuard，见该函数注）。
     *
     * @param id        [in] 模板登记 id（listTemplates() 清单内的 id——
     *                  清单外＝调用方契约违约，见步①）
     * @param preset    [in] 安装预设（须在该模板描述符 installationPresets
     *                  词表内——即 Ground/Inverted/Wall 之一；Custom 需要
     *                  用户输入 customEaa，模板路径不接纳：违约抛
     *                  std::invalid_argument fail-fast——I-MDL-7 前置）
     * @param localName [in] 模型基名（根 displayName 与呈现源；非空且仅
     *                  [A-Za-z0-9_.-]——违约走 IllegalName 值面，见步③）
     * @param diags     [out] 诊断记录输出（追加不清空）。本实现只在步②
     *                  追加 MDL-TEMPLATE-DISABLED 一条（码已注册——
     *                  DiagCodes.hpp T07 行；禁字符串拼码）；成功路径不
     *                  追加（§9.4.2 @post 无诊断承诺）。
     * @return ok＝完整工作集（满足 I-MDL-1~12——§9.4.2 @post；不触达
     *         project、不产生修订）；err＝TemplateDisabled|IllegalName
     *         （§9.4.2 @错误 行）
     *
     * @throws std::invalid_argument 步①清单外 id／步④前 Custom 预设
     *               （调用方契约违约——fail-fast，不产半成品）
     *
     * 纯函数；线程安全；确定性（NFR-COR-02：同输入→同工作集字节——对象
     * 身份为确定性派生临时句柄，见 ModelingWorkingSet 注）。
     */
    virtual TemplateOutcome createDraft(const TemplateId& id,
                                        runtime::InstallationPresetToken preset,
                                        std::string_view localName,
                                        std::vector<core::DiagnosticRecord>& diags) const = 0;
};

/**
 * @brief IRobotDesignTemplateFactory 无状态实现（§3.4 总约定 1——Codec/
 *        PropertyEstimator/ModelImportMapper 同款"接口＋final 实现"形态）。
 */
class RobotDesignTemplateFactory final : public IRobotDesignTemplateFactory {
public:
    std::vector<TemplateDescriptor> listTemplates() const override;
    TemplateOutcome createDraft(const TemplateId& id,
                                runtime::InstallationPresetToken preset,
                                std::string_view localName,
                                std::vector<core::DiagnosticRecord>& diags) const override;
};

// =====================================================================
// 创建入口链型守卫（§2.1 创建列 ❌ 的实现面；§6.4 维度二判定复用）
// =====================================================================

/**
 * @brief 创建入口链型能力守卫（acceptance 4"4/5 轴与含 prismatic 链创建
 *        入口阻止＋提示"——§6.4 能力矩阵"模板创建"列的实现）。
 *
 * 调用时机（PM-01 创建流程）：向导在**创建确认**时以用户已声明/编辑完成
 * 的完整关节类型序列调用本函数——FullTemplateRange→放行（进入草稿落盘/
 * apply-robot-design）；BeyondTemplateRange→创建入口阻止（阻止动作由
 * 调用方执行：不放行、不落盘）并得到提示诊断（TEMPLATE-RANGE info，已
 * 追加入 diags）。custom-chain 的**种子**工作集不受本守卫约束（链未完成
 * ——逐轴添加期间轴数天然不在六/七轴范围；§5.1 custom-chain 为 R1 可用
 * ——守卫只作用于"以该链进入正式创建"的时点）。
 *
 * 判定复用（§6.4 尾段原文）：结论唯一来自 Import.hpp 的
 * judgeChainCapability（单一实现——不存在第二份判定）；提示诊断复用
 * §9.5 T05 行 MDL-IMPORT-TEMPLATE-RANGE（同码同语义"超出首版产品模板
 * 范围"——不私定第二码）。
 *
 * @param declaredChain [in] 用户声明的完整关节类型序列（链序）
 * @param diags         [out] 诊断记录输出（追加不清空）。阻断时追加一条
 *                      MDL-IMPORT-TEMPLATE-RANGE（创建入口尚无对象——
 *                      subject 为空 optional，context 以"template-entry"
 *                      定位；放行时不追加）。
 * @return nullopt＝放行（六/七轴全旋转）；非空＝维度二结论（调用方据此
 *         阻止创建入口；kind/ movableAxes/containsPrismatic/reason 供
 *         向导提示呈现）
 *
 * 纯函数；线程安全；确定性。
 */
std::optional<ChainCapability> creationEntryGuard(
    const std::vector<JointType>& declaredChain,
    std::vector<core::DiagnosticRecord>& diags);

// =====================================================================
// 逐轴编辑流（§5.2 字段级/批量变体——T07 编辑域内核；T08 编辑器在此之上
// 组装 IRobotDesignEditor 的 ModelingEdit 轨道）
// =====================================================================

/**
 * @brief 逐轴可编辑字段（§5.1 custom-chain"逐轴定义"列与 §5.2 编辑面的
 *        schema 交集——§4.3-A JointEntry 已登记字段）。
 *
 * 范围说明（速度/加速度的边界）：acceptance 4 的编辑面列有"速度/加速度"
 * ——该二者在 §4.3-A 关节 schema 无落点（单元卡 §15 v0.4 ③c"不发明
 * schema"纪律），数值面由 SixAxisJointSpec 承载（见其类型注）；其字段级
 * 编辑随 schema 澄清后表尾追加本枚举（持久化契约面纪律：只允许表尾追加
 * 并走单元卡增量修订）。
 */
enum class JointEditField {
    Type,        ///< 关节类型（Revolute|Continuous|Prismatic|Fixed——I-MDL-4 组合约束见 applyJointFieldEdit）
    Axis,        ///< 轴线（连杆系下单位向量；Explicit 权威一等字段——MDL-09/C-1）
    ZeroOffset,  ///< 零位偏置（rad/m——两态均权威）
    Bounds,      ///< 限位 {qmin,qmax}（rad/m——Revolute/Prismatic 必填面）
};

/**
 * @brief 编辑字段稳定 token（"type"/"axis"/"zeroOffset"/"bounds"——变更
 *        摘要与 UT 判别串）。纯函数；确定性。
 */
std::string_view jointEditFieldToken(JointEditField field) noexcept;

/**
 * @brief 逐轴编辑的局部错误码（T07 局部载体——PropertyEstimation 的
 *        EstimateErrorCode 同款"接口局部错误枚举，不进域级错误轨道"先例；
 *        T08 编辑器组装 EditOutcome 时映射到 ModelingErrorCode：
 *        AuthorityLocked→AuthorityViolation，其余→编辑器断言/校验族）。
 */
enum class JointEditErrorCode {
    /// "authority-locked"——权威互斥拒绝（C-1：StandardDH 态编辑 axis——
    /// 派生只读，§7.3；判定复用 RobotDesign.hpp authorityEditGuard）
    AuthorityLocked,
    /// "value-not-finite"——输入含 NaN/Inf（I-MDL-3：非法值不静默置 0）
    ValueNotFinite,
    /// "axis-not-normalizable"——零轴/次正规轴（I-MDL-6：非零、有限、可归一化）
    AxisNotNormalizable,
    /// "limit-interval-invalid"——qmin≥qmax（I-MDL-4 前半；rad/m 随类型）
    LimitIntervalInvalid,
    /// "type-bounds-conflict"——Continuous 与已提供 bounds 并存（I-MDL-4
    /// 后半：Continuous＝bounds NotApplicable；不静默清除已有值——NFR-COR-03）
    TypeBoundsConflict,
};

/**
 * @brief 局部错误码稳定 token（枚举成员名连字符串——见各值注；UT 判别与
 *        变更摘要承载）。纯函数；确定性。
 */
std::string_view jointEditErrorCodeToken(JointEditErrorCode code) noexcept;

/**
 * @brief 逐轴编辑拒绝值（局部错误面：码＋定位细节）。
 *
 * detail 面向内部诊断链/日志（下标、字段、原始值定位）；呈现文案归
 * T08 编辑器按码映射（diagnostics/ui 文案层）。
 */
struct JointEditError {
    JointEditErrorCode code = JointEditErrorCode::ValueNotFinite;  ///< 稳定错误码
    std::string detail;  ///< 定位细节（UTF-8；subject/字段/原因）

    bool operator==(const JointEditError& o) const noexcept
    {
        return code == o.code && detail == o.detail;
    }
    bool operator!=(const JointEditError& o) const noexcept { return !(*this == o); }
};

/**
 * @brief 字段编辑的值载荷（与 JointEditField 一一对应的 variant——四可
 *        编辑字段各一备择；备择序＝枚举声明序）。
 *
 * Axis 备择不预先归一化（不静默改写输入——NFR-COR-03；I-MDL-6 只要求
 * 非零/有限/可归一化，单位向量语义在消费侧解读）。
 */
using JointEditValue = std::variant<JointType, rw::math::Vector3D<double>, double, JointLimits>;

/**
 * @brief 应用一次字段级逐轴编辑（§5.2 字段级变体；acceptance 4 编辑链）。
 *
 * 规则（按检查序；拒绝时工作集**字节不变**——§9.4.1 拒绝语义的域内核）：
 *   ①下标越界＝调用方契约违约→抛 std::invalid_argument fail-fast；
 *   ②field 与 value 备择不匹配＝调用方契约违约→同上 fail-fast；
 *   ③Axis：先过权威守卫（authorityEditGuard——StandardDH 态拒绝，
 *     AuthorityLocked），再查有限性（ValueNotFinite）与可归一化
 *     （AxisNotNormalizable——零/次正规轴，I-MDL-6 同口径判定）；
 *   ④ZeroOffset：查有限性（rad/m——随类型）；
 *   ⑤Bounds：查有限性与 qmin<qmax（LimitIntervalInvalid）；当前关节为
 *     Continuous 时拒绝（TypeBoundsConflict——不静默清除/覆盖 NotApplicable）；
 *   ⑥Type：改为 Continuous 时若既有 bounds 为 Provided 拒绝
 *     （TypeBoundsConflict——同样不静默清除）；其余类型转换接受（转换后
 *     的 bounds 缺失面归就绪校验待确认——导入路径同口径）。
 *
 * 接受后：design 对应字段更新（axis/bounds 以 UserProvided 来源写入——
 * 用户输入覆盖保留来源标记，§5.3 规则 1 同款语义）＋追加**一条**变更摘要
 * 记录（append-only，见 ModelingChangeRecord 注）。
 *
 * @param ws         [in,out] 目标工作集（拒绝时保证不变——先校验后提交）
 * @param jointIndex [in] 关节下标（链序，0 起；越界＝fail-fast）
 * @param field      [in] 待编辑字段
 * @param value      [in] 新值载荷（备择须与 field 匹配）
 * @return nullopt＝接受（工作集已更新＋一条变更记录）；非空＝拒绝
 *         （局部错误面——工作集不变）
 *
 * @throws std::invalid_argument 越界下标／field-value 备择不匹配（调用方
 *               契约违约）
 *
 * 非线程安全（编辑态仅 UI 线程——ModelingWorkingSet 注）；确定性。
 */
std::optional<JointEditError> applyJointFieldEdit(ModelingWorkingSet& ws,
                                                  std::size_t jointIndex,
                                                  JointEditField field,
                                                  const JointEditValue& value);

/**
 * @brief 批量编辑条目（UX-05 批量变体的行单位——同一字段×多行值，如参数
 *        表整列粘贴）。
 */
struct JointBatchEditItem {
    std::size_t jointIndex = 0;  ///< 行＝关节下标（链序）
    JointEditValue value{};      ///< 该行新值（备择须与批量字段匹配）
};

/**
 * @brief 批量编辑结果（§9.4.1 BatchPartial 语义的批量变体承载）。
 */
struct JointBatchEditOutcome {
    std::size_t appliedCount = 0;          ///< 接受行数（成功提交的行）
    std::vector<std::size_t> rejectedRows;  ///< 拒绝行下标（按输入序；非法行保留原值）
    std::optional<JointEditError> lastError;  ///< 最后一次拒绝的明细（呈现定位用；全行成功为空）
};

/**
 * @brief 应用一次批量字段编辑（§5.2 批量变体＋UX-05——"一次调用一个批量
 *        变体，产出单条变更摘要"）。
 *
 * 语义：批量条目必须为**同一字段**（"按行批量提交同一类 Edit"——混入多
 * 字段＝调用方契约违约，抛 std::invalid_argument fail-fast）；逐行独立
 * 校验并提交（非法行保留原值、合法行不连带回滚——§9.4.1 BatchPartial
 * 括注口径）； appliedCount>0 时追加**恰好一条**变更摘要记录（批量产出
 * 单条变更摘要——行数与拒绝行数在 summary 内说明）；全行拒绝不追加记录
 * （无变更即无摘要——与字段级语义一致）。
 *
 * @param ws    [in,out] 目标工作集（同 applyJointFieldEdit 的强保证）
 * @param field [in] 批量字段（全体条目同一字段）
 * @param items [in] 批量条目（行序＝提交序；重复行=后值覆盖前值——顺序
 *              语义确定性）
 * @return 批量结果（appliedCount/rejectedRows/lastError——见类型注）
 *
 * @throws std::invalid_argument items 含越界下标／备择与 field 不匹配／
 *               （调用方契约违约——fail-fast）
 *
 * 非线程安全（编辑态仅 UI 线程）；确定性（逐行独立、拒绝不回滚已应用行
 * ——结果只依赖输入序列本身）。
 */
JointBatchEditOutcome applyJointFieldEditBatch(ModelingWorkingSet& ws,
                                               JointEditField field,
                                               const std::vector<JointBatchEditItem>& items);

// =====================================================================
// 基座安装姿态编辑流（§5.2 SetBasePlacement 的域内核——WP-13-T11；
// T08 编辑器在此之上组装 IRobotDesignEditor 的 ModelingEdit 轨道，与
// 逐轴编辑流同款分层）
// =====================================================================

/**
 * @brief 基座安装姿态编辑的局部错误码（T11 局部载体——JointEditErrorCode
 *        同款"接口局部错误枚举，不进域级错误轨道"先例；T08 编辑器组装
 *        EditOutcome 时映射到 ModelingErrorCode 校验族）。
 */
enum class BasePlacementEditErrorCode {
    /// "value-not-finite"——customEaa/basePosition 含 NaN/Inf（I-MDL-3：
    /// 非法值不静默置 0/不静默丢弃）
    ValueNotFinite,
    /// "custom-eaa-missing"——custom 预设缺 customEaa（I-MDL-7 前半：
    /// Custom 无预设矩阵，编辑表示必填——runtime §4.2 同口径）
    CustomEaaMissing,
    /// "preset-identity-rotation"——preset≠ground 而旋转为恒等（I-MDL-7
    /// 后半"preset≠ground 而 R=I"的**映射层拒绝**——T03 §15 增量 f) 登记
    /// 的 T11 落位点；判定口径与 runtime InputInvalid 同源：customEaa 经
    /// runtime 唯一换算点产出的 R 逐元素与 I 偏差 ≤1×10⁻¹² 即视为恒等）
    PresetIdentityRotation,
    /// "rotation-not-orthogonal"——customEaa 旋转矩阵正交性违例（容差
    /// 1×10⁻¹²）。判定**复用值模型 I-MDL-7 单一实现**（经 checkInvariants
    /// 过滤 IMdl7 违例——NFR-MNT-04 不得出现两套判定）；Rodrigues 构造下
    /// 数学上不可达，本码是换算实现被污染时的防御闸
    RotationNotOrthogonal,
};

/**
 * @brief 局部错误码稳定 token（枚举成员名连字符串——见各值注；UT 判别与
 *        变更摘要承载）。纯函数；确定性。
 */
std::string_view basePlacementEditErrorCodeToken(BasePlacementEditErrorCode code) noexcept;

/**
 * @brief 基座安装姿态编辑拒绝值（局部错误面：码＋定位细节）。
 *
 * detail 面向内部诊断链/日志（字段、预设 token、实测偏差）；呈现文案归
 * T08 编辑器按码映射（diagnostics/ui 文案层）。
 */
struct BasePlacementEditError {
    /// 稳定错误码（缺省＝ValueNotFinite——首个检查项）。
    BasePlacementEditErrorCode code = BasePlacementEditErrorCode::ValueNotFinite;
    std::string detail;  ///< 定位细节（UTF-8；字段/预设/原因）

    bool operator==(const BasePlacementEditError& o) const noexcept
    {
        return code == o.code && detail == o.detail;
    }
    bool operator!=(const BasePlacementEditError& o) const noexcept { return !(*this == o); }
};

/**
 * @brief 基座安装姿态编辑值（§5.2 SetBasePlacement 的载荷——**整体替换**
 *        语义：一次编辑＝一次完整表达目标安装姿态，与属性面板"预设单选＋
 *        欧拉角输入＋位置输入"一次提交的形态对应）。
 *
 * 字段语义（MDL-22；单位纪律见各注）：
 *   - preset：安装预设（runtime::InstallationPresetToken 四值词表——单一
 *     权威，直接复用；modeling 不另设词表）；
 *   - customEaa：custom 预设必填的旋转矢量（EAA：方向＝轴、模长＝角，
 *     **单位 rad**）；preset≠Custom 时**必须**为 nullopt（携带即调用方
 *     契约违约——fail-fast；EAA 只对 Custom 有语义，静默忽略会掩盖面板
 *     状态错误）；
 *   - basePosition：基座原点世界系位置（**单位 m**；整体替换语义下必填
 *     ——面板提交时携带当前显示值；由本编辑流以 UserProvided 来源写入）。
 *
 * ★ modeling 只存参数不存矩阵（P-RT-4/M-11）：本值与编辑产物
 *   BasePlacement 内**没有任何旋转矩阵字段**——预设轴向矩阵的唯一权威
 *   产出点＝runtime BaseWorldTransform.hpp::installationPresetRotation()
 *   （倒挂=R_x(π)、壁装=R_y(π/2)，runtime.md §6.2 冻结值）；custom 的
 *   EAA→R 换算唯一经 runtime::rotationFromCustomEaa()（本编辑流只在
 *   校验时调用该权威换算做拒绝判定，不缓存、不预乘任何矩阵——V-12
 *   建模侧输入面）。
 */
struct BasePlacementEditValue {
    /// 安装预设（四值词表——runtime::InstallationPresetToken 单一权威）。
    runtime::InstallationPresetToken preset = runtime::InstallationPresetToken::Ground;

    /// custom 预设的 EAA 旋转矢量（单位 rad；仅 preset==Custom 时允许携带
    /// ——见结构注的契约违约面）。
    std::optional<rw::math::Vector3D<double>> customEaa;

    /// 基座原点位置（单位 m，世界坐标系下表示）。
    rw::math::Vector3D<double> basePosition{0.0, 0.0, 0.0};
};

/**
 * @brief 应用一次基座安装姿态编辑（§5.2 SetBasePlacement 域内核；
 *        WP-13-T11 acceptance 1/2 的编辑面落点）。
 *
 * 规则（按检查序；拒绝时工作集**字节不变**——与 applyJointFieldEdit 同款
 * 强保证）：
 *   ①preset≠Custom 而携带 customEaa＝调用方契约违约→抛
 *     std::invalid_argument fail-fast（EAA 字段仅 Custom 有语义）；
 *   ②customEaa（Custom 时）/basePosition 逐分量有限性→ValueNotFinite
 *     （I-MDL-3：不静默置 0）；
 *   ③preset==Custom 且 customEaa 缺失→CustomEaaMissing（I-MDL-7 前半：
 *     Custom 无预设矩阵，编辑表示必填——runtime §4.2 同口径）；
 *   ④preset==Custom 且 customEaa 经 runtime::rotationFromCustomEaa()
 *     （EAA→R 唯一权威换算点——P-RT-4）产出的 R 逐元素与恒等阵偏差
 *     ≤1×10⁻¹²→PresetIdentityRotation（I-MDL-7 后半"preset≠ground 而
 *     R=I"的映射层拒绝——T03 §15 增量 f) 登记的 T11 落位点；与 runtime
 *     checkPresetConsistency 的 Custom≠I 校验同口径，编辑边界就地拒绝、
 *     不让非法组合流入命令/编译域）；
 *   ⑤正交性（1×10⁻¹²）→RotationNotOrthogonal——判定经 checkInvariants
 *     过滤 InvariantId::IMdl7（值模型单一实现复用，NFR-MNT-04）；
 *   ⑥提交：basePlacement 整体替换（preset 直写；Custom 时 customEaa 以
 *     UserProvided 来源 Provided；切离 Custom 时 customEaa 复位
 *     NotProvided——EAA 随预设失效是本编辑的显式语义、记录进变更摘要，
 *     非静默清除；basePosition 以 UserProvided 来源 Provided）＋追加
 *     **一条**变更摘要记录（append-only）。
 *
 * 持久化路径（原子性——MDL-22/DTB T11 行）：本函数只改编辑态工作集；
 * 持久化经 apply-robot-design 命令（根对象整体写入）——单命令单修订，
 * 双编译原子性由 project S5 承担（任一编译段失败→修订不产生）。
 *
 * @param ws   [in,out] 目标工作集（拒绝时保证不变——先校验后提交）
 * @param edit [in] 编辑值（整体替换语义——见 BasePlacementEditValue 注）
 * @return nullopt＝接受（工作集已更新＋一条变更记录）；非空＝拒绝
 *         （局部错误面——工作集不变）
 *
 * @throws std::invalid_argument preset≠Custom 而携带 customEaa（调用方
 *               契约违约——fail-fast）
 *
 * 非线程安全（编辑态仅 UI 线程——ModelingWorkingSet 注）；确定性（同
 * 输入同结论；拒绝判定只依赖 runtime 权威换算的纯函数）。
 */
std::optional<BasePlacementEditError> applyBasePlacementEdit(
    ModelingWorkingSet& ws,
    const BasePlacementEditValue& edit);

// =====================================================================
// 几何生成辅助（§5.2 v0.2 增补——承接旧 autoLink/碰撞生成辅助，§2.5）
// =====================================================================

/// @brief 占位圆柱默认半径，单位 m（设计默认值——§14.4 D-MDL-7 增量登记；
///        仅视觉占位、参数可在属性区改写（§5.2），不参与碰撞判定）。
inline constexpr double kPlaceholderCylinderRadius = 0.05;

/**
 * @brief 几何辅助的产物（§5.2"产物均为普通 GeometryRef 同权同校验、来源
 *        GeometricEstimate（methodTag 区分辅助类型）"的承载）。
 *
 * geometry＝普通 GeometryRef（与手编几何同权、同校验——不设辅助专道）；
 * provenance＝GeometricEstimate＋methodTag（"link-placeholder-cylinder" /
 * "collision-copy"——来源徽标数据源，§9.7 属性区投影）。provenance 挂在
 * 产物值上而非 GeometryRef 内（GeometryRef schema 无来源字段——§4.3-B），
 * 由消费方（T08 编辑器/T15 属性区）随字段写入留痕。
 */
struct GeneratedGeometry {
    GeometryRef geometry;              ///< 普通几何引用（同权同校验）
    core::ValueProvenance provenance;  ///< 来源标记（GeometricEstimate＋methodTag 区分辅助）

    bool operator==(const GeneratedGeometry& o) const
    {
        return geometry == o.geometry && provenance == o.provenance;
    }
    bool operator!=(const GeneratedGeometry& o) const { return !(*this == o); }
};

/**
 * @brief 生成连杆占位圆柱（§5.2 辅助①——确定性纯函数；旧 autoLink 的
 *        承接，§2.5）。
 *
 * 几何（相邻关节原点连线）：圆柱轴沿 start→end 连线、长＝|end−start|（m）、
 * 中心在连线中点。localTransform＝T_link_geom（"geom 系相对 link 系"，m/
 * rad——core.md §4.6）：平移＝中点；旋转＝把geom 系 z 轴旋到连线单位方向
 * 的最小旋转（Rodrigues 解析式逐元素构造——冒烟 header-only 纪律：不调用
 * Rotation3D::identity() 等框架外联符号，Import.cpp rpyToRotation 同款；
 * 连线恰为 ±z 时取恒等/绕 x 轴 π 的确定性特例）。kind=Primitive（占位
 * 原语）；resourceRefId 由调用方分配（模型内作用域键——普通 GeometryRef
 * 同权：占位原语经资源清单的持久化承载受 R1 schema 约束〔图元参数无
 * schema 落点——单元卡 §15 v0.6 ②b 同源边界〕，其登记形态随 schema
 * 澄清落位，本函数只产出几何引用本体与来源标记）。
 *
 * @param segmentStart [in] 段起点（相邻关节原点 A），单位 m，连杆系下
 * @param segmentEnd   [in] 段终点（相邻关节原点 B），单位 m，连杆系下
 * @param resourceRefId [in] 资源引用键（调用方分配；空串＝调用方错误）
 * @param radius       [in] 圆柱半径，单位 m（>0 且有限；默认设计值见
 *                     kPlaceholderCylinderRadius）
 * @return 占位圆柱产物（几何引用＋GeometricEstimate 来源标记——methodTag
 *         "link-placeholder-cylinder"）
 *
 * @throws std::invalid_argument 半径非正/非有限、起终点重合（零长度圆柱
 *               无几何意义）或 resourceRefId 为空（调用方契约违约）
 *
 * 纯函数；线程安全；确定性（同输入→同位姿字节）。
 */
GeneratedGeometry makeLinkPlaceholderCylinder(
    const rw::math::Vector3D<double>& segmentStart,
    const rw::math::Vector3D<double>& segmentEnd,
    std::string resourceRefId,
    double radius = kPlaceholderCylinderRadius);

/**
 * @brief 碰撞引用复制辅助（§5.2 辅助②——把视觉几何引用复制为同资源
 *        碰撞引用）。
 *
 * 边界（acceptance 5 原文）：**不引入网格重画/凸包简化算法**（上游未授权
 * ——如需简化代理走需求变更）；复制＝同 resourceRefId、同 localTransform、
 * 同 kind 的普通 GeometryRef（碰撞几何≠碰撞判定——判定唯一归 policy，
 * §4.3-B 注）。
 *
 * @param visual [in] 视觉几何引用（源）
 * @return 碰撞引用产物（GeometricEstimate 来源标记——methodTag
 *         "collision-copy"）
 *
 * 纯函数；线程安全；确定性。
 */
GeneratedGeometry copyVisualToCollision(const GeometryRef& visual);

}  // namespace sdurws::ird::modeling

#endif  // IRD_MODELING_TEMPLATE_HPP
