#include "KinematicAnalysisJson.hpp"

#include <QString>

#include <cmath>

namespace rws {

// =============================================================================
//  jsonValueFromDouble:统一的浮点数→JSON 值转换器
// =============================================================================
//
// Qt 的 QJsonValue::fromDouble 不支持 ±Inf / NaN:
//   - 调用 .toDouble() 在 Python 标准 JSON 解析时会抛错;
//   - 写 NaN / Inf 到 QJsonObject::insert(name, value) 时,Qt 会自动序列化为
//     null 或抛警告,导致下游解析失败。
//
// 因此我们集中处理非有限数:
//   1) NaN → "nan" 字符串;
//   2) +Inf → "inf" 字符串;
//   3) -Inf → "-inf" 字符串;
//   4) 其它 → 原 QJsonValue(double),即 JSON 数字。
//
// JSON 字符串命名与 IEEE 754 标准符号一致,Python 解析时:
//
//   import json, math
//   parsed = json.loads('{"v": "inf"}')["v"]
//   # 用户需自行从 "inf" / "nan" 转回 float('inf') / float('nan')
//
// 此 helper 让 exportReportJson() 中所有"可能非有限"的浮点数(manipulability /
// conditionNumber / positionErrorMeters / orientationErrorDeg / coverage)统一
// 转换,避免散落的 std::isnan / std::isinf 重复检查。
QJsonValue jsonValueFromDouble (double value)
{
    if (std::isnan (value))
        return QJsonValue (QStringLiteral ("nan"));
    if (std::isinf (value))
        return QJsonValue (value > 0.0 ? QStringLiteral ("inf")
                                       : QStringLiteral ("-inf"));
    return QJsonValue (value);
}

}    // namespace rws
