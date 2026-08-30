// Fixture name-concatenation: a business plugin concatenates runtime names by hand
// (WP-01 plan §2.1 runtime module owns all name building; §7 rule on business plugins).
// Expected diagnostic keyword: 运行时名称拼接.
#include <QString>

QString buildRuntimeName(const QString& robotName, const QString& localName)
{
    return robotName + "." + localName;
}
