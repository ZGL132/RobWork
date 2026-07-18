#include "KinematicAnalysisJson.hpp"

#include <QString>

#include <cmath>

namespace rws {

QJsonValue jsonValueFromDouble (double value)
{
    if (std::isnan (value))
        return QJsonValue (QStringLiteral ("nan"));
    if (std::isinf (value))
        return QJsonValue (value > 0.0 ? QStringLiteral ("inf") : QStringLiteral ("-inf"));
    return QJsonValue (value);
}

}    // namespace rws
