/**
 * @file   ObjectTypes.cpp
 * @brief  modeling 对象类型登记的实现——SceneObjectRole 词表转发表。
 *
 * 设计依据：
 *   - units/modeling.md §4.2（五对象 token 权威表）、§4.5（SceneObjectRole
 *     词表五值与所有者登记）、§14.4（建模侧正式登记）
 *   - 任务契约 tasks/foundation/WP-13-T02.json acceptance 4
 *
 * 背景说明：token 常量本身是头内 inline constexpr（零运行时成本，无需
 * 本 TU 承载）；本翻译单元只承载**函数**——sceneObjectRoleToken 与
 * trySceneObjectRole（词表串↔枚举的双向映射）。映射表是词表的唯一实现
 * 点：与头文件枚举声明若失同步，测试的全枚举机械比对（五值逐一往返）
 * 即刻暴露（io errorCodeToken 同款防线）。五对象 token 不设任何运行时
 * 函数——它们是纯常量登记（§4.2 表），消费者直接引用常量，不设"取
 * token"的间接层（NFR-MNT-04：无消费者的能力不预建）。
 *
 * 确定性（NFR-COR-02）：全部产出为编译期固定字面量 switch，同输入同
 * 串/同枚举，无 locale、无环境读取。
 */

#include <sdurws/ird/modeling/ObjectTypes.hpp>

namespace sdurws::ird::modeling {

std::string_view sceneObjectRoleToken(SceneObjectRole role) noexcept
{
    // 词表串转发表：switch 全枚举、无 default——新增枚举值而漏登记表项
    // 时编译器告警暴露遗漏（-Wswitch/-C4062；枚举顺序＝§4.5 词表原文序，
    // 表尾追加纪律见 ObjectTypes.hpp 枚举注释）。串值＝policy 侧
    // sceneObjectRoleToken 同一词表（词表所有者＝modeling，policy 消费
    // ——两侧一致性由单元测试逐值比对钉住）。
    switch (role) {
    case SceneObjectRole::RobotLink:
        return "RobotLink";
    case SceneObjectRole::Tool:
        return "Tool";
    case SceneObjectRole::Payload:
        return "Payload";
    case SceneObjectRole::EnvironmentObject:
        return "EnvironmentObject";
    case SceneObjectRole::Workpiece:
        return "Workpiece";
    }
    // 全枚举已覆盖，不达此处；无 return 以外的兜底分支（ARC-04：不猜测
    // ——若未来表尾追加新值，编译器在漏表项时即报错，而不是静默兜底）。
    return "RobotLink";
}

std::optional<SceneObjectRole> trySceneObjectRole(std::string_view token) noexcept
{
    // 精确等值比较（无大小写折叠/无空白剥离——ARC-04"不猜测"：拼写变体
    // 一律词表外）。分支顺序＝词表枚举序（确定性遍历序）。
    if (token == "RobotLink") {
        return SceneObjectRole::RobotLink;
    }
    if (token == "Tool") {
        return SceneObjectRole::Tool;
    }
    if (token == "Payload") {
        return SceneObjectRole::Payload;
    }
    if (token == "EnvironmentObject") {
        return SceneObjectRole::EnvironmentObject;
    }
    if (token == "Workpiece") {
        return SceneObjectRole::Workpiece;
    }
    // 词表外（含空串/大小写变体/未知串）→ nullopt：调用方按其所在域
    // 处置（导入→报告不支持项；策略→规则拒绝），本函数不产诊断（PA-1）。
    return std::nullopt;
}

}  // namespace sdurws::ird::modeling
