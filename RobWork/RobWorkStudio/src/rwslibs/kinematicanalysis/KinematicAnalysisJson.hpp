#ifndef RWS_KINEMATICANALYSIS_KINEMATICANALYSISJSON_HPP
#define RWS_KINEMATICANALYSIS_KINEMATICANALYSISJSON_HPP

// QJsonValue 是 Qt 的 JSON 值类型,可统一表示 null/布尔/数字/字符串/数组/对象。
#include <QJsonValue>

namespace rws {

// =============================================================================
//  jsonValueFromDouble:统一的浮点数→JSON 值转换器
// =============================================================================
//
// 背景:Qt 的 QJsonValue::fromDouble 不支持 ±Inf / NaN,
// 强写会抛异常或被静默截断,导致 report JSON 在奇异姿态下被截断。
// 此 helper 集中处理非有限数:
//   - NaN  → 字符串 "nan";
//   - +Inf → 字符串 "inf";
//   - -Inf → 字符串 "-inf";
//   - 其它 → 数字(JSON 数字类型,消费者用 .toDouble() 取回)。
//
// 使用方式:把所有"可能来自分析结果中的浮点数"(manipulability / cond /
// positionErrorMeters / orientationErrorDeg / cog / coverage 等)统一用此函数
// 转 QJsonValue,避免散落的 std::isnan / std::isinf 重复检查。
//
// 字符串形式("nan"/"inf"/"-inf")与 IEEE 754 标准命名一致,
// 下游 Python/JS 解析 JSON 时可直接识别并解析为 None/Infinity。
QJsonValue jsonValueFromDouble (double value);

}    // namespace rws

#endif    // RWS_KINEMATICANALYSIS_KINEMATICANALYSISJSON_HPP
