/**
 * @file   ObjectTypes.hpp
 * @brief  modeling 对象类型登记——五对象 objectTypeToken 常量与场景对象
 *         角色（SceneObjectRole）词表。
 *
 * 设计依据：
 *   - units/modeling.md §4.2（对象分解与对象类型登记——五对象 token 权威
 *     表；"根对象 token 与 runtime 已落位常量 kRobotDesignObjectType=
 *     "robot-design" 字面一致，不得另设第二常量"原文）、§4.5（SceneObject
 *     对象 role 字段——"词表所有者＝modeling"）、§3.3（公共头表
 *     ObjectTypes.hpp 行）、§14.4（新增语义登记第 2 项——SceneObjectRole
 *     词表的建模侧正式登记）
 *   - ARCHITECTURE.md §3.3（业务域单元二分结构——本头属计算库公共面）、
 *     §7.4（名称语义归 runtime——本头登记的是对象**类型** token，不是
 *     RobWork 名称，两者不同层）
 *   - 需求 ARC-04（对象稳定身份的类型维度——token 是路由键）、MDL-15
 *     （场景对象角色）、CON-01（对象闭包与类型路由）
 *   - 任务契约 tasks/foundation/WP-13-T02.json acceptance 4（公共头落位
 *     ——ObjectTypes.hpp 行）
 *
 * 背景说明（为什么对象类型 token 如此重要）：modeling 拥有的持久化对象
 * 以 objectTypeToken 声明类型，project 修订闭包（RevisionView.objectRefs）
 * 与 runtime 编译链 S2 都按该 token 路由到对应的解析器/读取器——拼写漂移
 * ＝路由失联（对象在闭包里却没人认领）。因此本头是 token 的**登记簿**：
 *   - robot-design 的 token 唯一权威＝runtime::kRobotDesignObjectType
 *     （runtime/Sources.hpp 已落位常量，编译入口对象——runtime S2 以此
 *     定位恰一个根对象）。modeling 经 using 声明引入**同一实体**，不另设
 *     第二常量（§4.2 原文纪律；测试以地址相等断言实体同一性）；
 *   - 其余四对象 token 由 modeling 登记（§14.4 新增语义登记第 1 项——
 *     modeling 对象所有权，ARC-04/CON-01 框架内）。
 *
 * 对象 schema 版本常量**不在本任务落位**：§3.3 把该内容行登记为 T02/T03
 * 双任务承载，其消费者（codec 的 schema 主版本检查）随 WP-13-T03 落地，
 * 按"无消费者不预建"纪律（§9.5 同款）留给 T03——本头只登记 token 与
 * 角色词表（契约 acceptance 4 明示范围）。
 *
 * 线程安全：本头全部实体为 constexpr 常量/纯值类型/纯函数，并发只读安全。
 * 确定性（NFR-COR-02）：token 与词表串均为编译期固定字面量，同值同串、
 * 无 locale 依赖。
 */

#ifndef IRD_MODELING_OBJECTTYPES_HPP
#define IRD_MODELING_OBJECTTYPES_HPP

#include <optional>
#include <string_view>

#include <sdurws/ird/runtime/Sources.hpp>  // runtime::kRobotDesignObjectType——根对象 token 唯一权威（§4.2"不另设第二常量"）

namespace sdurws::ird::modeling {

// =====================================================================
// 五对象 objectTypeToken（§4.2 权威表；表行序＝本头声明序——登记簿纪律：
// 后续新增对象类型只允许表尾追加并走单元卡增量修订，不重排既有 token）
// =====================================================================

/**
 * @brief robot-design 对象的类型 token（"robot-design"）。
 *
 * 经 using 声明引入 runtime::kRobotDesignObjectType——**同一实体**（同一
 * 常量对象，地址相同），不是值拷贝出来的第二常量（§4.2"不得另设第二
 * 常量"的落地面：两处名字指向同一个存储，拼写漂移在类型层面不可能发生）。
 * 消费点：project 修订闭包按它标记根对象引用；runtime 编译链 S2 以它
 * 定位恰一个 RobotDesign 权威对象（Sources.hpp 常量注释原文）。
 */
using runtime::kRobotDesignObjectType;

/// @brief tool-definition 对象的类型 token（§4.2 表第 2 行——工具定义，
///        每工具一个；被任务/负载经 ObjectId 引用，不复制工具几何 MDL-13）。
inline constexpr std::string_view kToolDefinitionObjectType = "tool-definition";

/// @brief scene-object 对象的类型 token（§4.2 表第 3 行——场景对象，
///        每对象一个；固定帧/环境几何/世界系位姿/角色，MDL-15）。
inline constexpr std::string_view kSceneObjectObjectType = "scene-object";

/// @brief named-pose-set 对象的类型 token（§4.2 表第 4 行——命名位姿集，
///        每模型一份；纯参考数据，变更不触发重算、不进任何计算切片 MDL-17）。
inline constexpr std::string_view kNamedPoseSetObjectType = "named-pose-set";

/// @brief robot-drivetrain 对象的类型 token（§4.2 表第 5 行——传动设计，
///        每模型一份；selection 回填与 OPT StageB 传动比编辑的独立写对象
///        MDL-16/21、SEL-10 目标）。
inline constexpr std::string_view kRobotDrivetrainObjectType = "robot-drivetrain";

// =====================================================================
// SceneObjectRole——场景对象角色词表（§4.5 role 字段；词表所有者＝modeling，
// policy.md §4.3 注明"场景对象角色词表归建模语义，policy 本文消费"）
// =====================================================================

/**
 * @brief 场景对象角色（§4.5 词表五值；MDL-15）。
 *
 * 所有权口径（§4.5 原文）：**词表所有者＝modeling**——本枚举即建模侧的
 * 正式登记（§14.4 第 2 项）；policy 侧在 CollisionEvaluator.hpp 持有同名
 * 五值枚举属于**消费方镜像**（其卡注明"归建模语义，本文消费"），策略
 * 作用域矩阵（policy.md §7.1）按本词表的 token 取值。两侧枚举在不同命名
 * 空间（modeling::SceneObjectRole / policy::SceneObjectRole），跨单元协作
 * 以 token 串为界（sceneObjectRoleToken 产出、消费方按串核对）——本头
 * 与 policy 头若失同步，单元测试的角色 token 逐值比对会即刻暴露。
 *
 * 枚举顺序＝§4.5 词表原文序（RobotLink|Tool|Payload|EnvironmentObject|
 * Workpiece）；持久化契约面纪律：只允许表尾追加并走单元卡增量修订。
 *
 * 线程安全：纯值枚举；确定性：同值同 token（NFR-COR-02）。
 */
enum class SceneObjectRole {
    RobotLink,           ///< "RobotLink"——机器人连杆（自碰撞 Self/Tool 域的机器人侧）
    Tool,                ///< "Tool"——工具（安装于法兰，经 tool-definition 引用）
    Payload,             ///< "Payload"——负载（被末端拾取的工件/托盘等）
    EnvironmentObject,   ///< "EnvironmentObject"——环境对象（固定帧/环境几何）
    Workpiece,           ///< "Workpiece"——工件（被加工/装配对象；REQ-04 等跨域引用）
};

/**
 * @brief 场景对象角色的稳定 token（词表串转发表）。
 *
 * token＝词表原文（§4.5 括注五串："RobotLink"/"Tool"/"Payload"/
 * "EnvironmentObject"/"Workpiece"）——与 policy 侧
 * policy::sceneObjectRoleToken 的产出逐值同串（同一词表的两侧登记，
 * 单元测试钉住一致性）。返回值指向静态存储期字面量。
 *
 * @param role [in] 场景对象角色（全枚举五值均有 token——switch 全枚举、
 *              无 default，新增枚举值未登记表项时编译器告警暴露遗漏）
 * @return 词表串（静态存储期；canonical 编码与策略规则核对共用）
 *
 * 纯函数；线程安全；确定性（NFR-COR-02：同角色同串）。
 */
std::string_view sceneObjectRoleToken(SceneObjectRole role) noexcept;

/**
 * @brief 词表串 → 场景对象角色（try 轨——词表外返回 nullopt 不猜测）。
 *
 * @param token [in] 待核对的角色 token（项目对象字节/策略规则承载的串）；
 *              精确等值比较（无大小写折叠、无前后空白剥离——ARC-04
 *              "不猜测"纪律：拼写变体一律词表外）
 * @return 对应角色；词表外（含空串/大小写变体/未知串）→ nullopt。
 *         nullopt 的业务含义由调用方按其所在域处置（导入映射→映射报告
 *         不支持项；策略规则→规则解析拒绝），本函数不自行产出诊断
 *         （PA-1：诊断文案权威归 core/diagnostics，产码唯一经工厂）。
 *
 * 纯函数；线程安全；确定性（NFR-COR-02）。
 */
std::optional<SceneObjectRole> trySceneObjectRole(std::string_view token) noexcept;

}  // namespace sdurws::ird::modeling

#endif  // IRD_MODELING_OBJECTTYPES_HPP
