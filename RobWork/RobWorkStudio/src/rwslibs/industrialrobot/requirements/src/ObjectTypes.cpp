/**
 * @file   ObjectTypes.cpp
 * @brief  requirements 对象类型登记的实现——ProcessTag/TemplateKind/
 *         ArrayKind 三词表的 token 转发表（§4.3/§7.1/§7.2 词表的唯一
 *         映射点）。
 *
 * 设计依据：
 *   - units/requirements.md §4.3（processTag 词表 11 值）、§7.1
 *     （TemplateKind 词表 6 值）、§7.2（ArrayKind 四值）、§14.4（词表
 *     登记制——requirements 所有权）
 *   - 先例：modeling/src/ObjectTypes.cpp（sceneObjectRoleToken/
 *     trySceneObjectRole 同款 switch 全枚举＋try 轨形态）
 *   - 任务契约 tasks/foundation/WP-14-T02.json acceptance 3
 *
 * 确定性（NFR-COR-02）：全部映射为编译期字面量表，switch 全枚举无
 * default——新增枚举值未登记表项时编译器告警暴露遗漏（登记簿纪律的
 * 编译期防线）；try 轨精确等值比较，无 locale 依赖。
 *
 * 线程安全：全部纯函数（无共享可变状态），并发只读安全。
 */

#include <sdurws/ird/requirements/ObjectTypes.hpp>

namespace sdurws::ird::requirements {

std::string_view processTagToken(ProcessTag tag) noexcept
{
    // 词表串＝§4.3 括注原文（"Generic/Pick/Place/MachineLoad/MachineUnload/
    // Inspect/WeldStart/WeldEnd/ToolChange/SafeStandby/Handover"）——与
    // 枚举声明序一一对应（登记簿纪律：两处失同步由测试全表机械比对暴露）。
    switch (tag) {
    case ProcessTag::Generic: return "Generic";
    case ProcessTag::Pick: return "Pick";
    case ProcessTag::Place: return "Place";
    case ProcessTag::MachineLoad: return "MachineLoad";
    case ProcessTag::MachineUnload: return "MachineUnload";
    case ProcessTag::Inspect: return "Inspect";
    case ProcessTag::WeldStart: return "WeldStart";
    case ProcessTag::WeldEnd: return "WeldEnd";
    case ProcessTag::ToolChange: return "ToolChange";
    case ProcessTag::SafeStandby: return "SafeStandby";
    case ProcessTag::Handover: return "Handover";
    }
    // switch 已全枚举（无 default——新增值未登记即编译告警）；到达此处
    // 仅可能是未定义枚举值（UB 防御面），返回空串不猜测。
    return {};
}

std::optional<ProcessTag> tryProcessTag(std::string_view token) noexcept
{
    // try 轨＝词表外的串一律 nullopt（ARC-04"不猜测"）：精确等值比较、
    // 无大小写折叠/空白剥离——拼写变体属词表外，由调用方按其域处置
    // （导入→映射报告；编辑器→就地错误）。
    if (token == "Generic") return ProcessTag::Generic;
    if (token == "Pick") return ProcessTag::Pick;
    if (token == "Place") return ProcessTag::Place;
    if (token == "MachineLoad") return ProcessTag::MachineLoad;
    if (token == "MachineUnload") return ProcessTag::MachineUnload;
    if (token == "Inspect") return ProcessTag::Inspect;
    if (token == "WeldStart") return ProcessTag::WeldStart;
    if (token == "WeldEnd") return ProcessTag::WeldEnd;
    if (token == "ToolChange") return ProcessTag::ToolChange;
    if (token == "SafeStandby") return ProcessTag::SafeStandby;
    if (token == "Handover") return ProcessTag::Handover;
    return std::nullopt;
}

std::string_view templateKindToken(TemplateKind kind) noexcept
{
    // 词表串＝§7.1 原文（"BinPicking/MachineTending/Palletizing/Inspection/
    // ToolChange/Handover"）。
    switch (kind) {
    case TemplateKind::BinPicking: return "BinPicking";
    case TemplateKind::MachineTending: return "MachineTending";
    case TemplateKind::Palletizing: return "Palletizing";
    case TemplateKind::Inspection: return "Inspection";
    case TemplateKind::ToolChange: return "ToolChange";
    case TemplateKind::Handover: return "Handover";
    }
    return {};
}

std::optional<TemplateKind> tryTemplateKind(std::string_view token) noexcept
{
    if (token == "BinPicking") return TemplateKind::BinPicking;
    if (token == "MachineTending") return TemplateKind::MachineTending;
    if (token == "Palletizing") return TemplateKind::Palletizing;
    if (token == "Inspection") return TemplateKind::Inspection;
    if (token == "ToolChange") return TemplateKind::ToolChange;
    if (token == "Handover") return TemplateKind::Handover;
    return std::nullopt;
}

std::string_view arrayKindToken(ArrayKind kind) noexcept
{
    // 词表串＝§7.2 原文（"Linear/Rectangular/Circular/Polyline"）。
    switch (kind) {
    case ArrayKind::Linear: return "Linear";
    case ArrayKind::Rectangular: return "Rectangular";
    case ArrayKind::Circular: return "Circular";
    case ArrayKind::Polyline: return "Polyline";
    }
    return {};
}

std::optional<ArrayKind> tryArrayKind(std::string_view token) noexcept
{
    if (token == "Linear") return ArrayKind::Linear;
    if (token == "Rectangular") return ArrayKind::Rectangular;
    if (token == "Circular") return ArrayKind::Circular;
    if (token == "Polyline") return ArrayKind::Polyline;
    return std::nullopt;
}

}  // namespace sdurws::ird::requirements
