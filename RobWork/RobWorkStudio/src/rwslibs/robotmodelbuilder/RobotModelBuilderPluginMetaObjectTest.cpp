#include "RobotModelBuilderPlugin.hpp"

#include <QMetaMethod>
#include <QMetaType>

#include <iostream>

namespace {
int fail (const char* message)
{
    std::cerr << "FAIL: " << message << std::endl;
    return 1;
}

bool hasQStringOperation (const QMetaObject& metaObject, const char* signature)
{
    const int index = metaObject.indexOfMethod (signature);
    return index >= 0 && metaObject.method (index).returnMetaType ().id () == QMetaType::QString;
}
}    // namespace

int main ()
{
    const QMetaObject& metaObject = rws::RobotModelBuilderPlugin::staticMetaObject;
    if (!hasQStringOperation (
            metaObject, "preflightRobotProjectSource(QString,QString)"))
        return fail ("Robot source preflight must be an invokable QString operation.");
    if (!hasQStringOperation (
            metaObject, "commitRobotProjectSource(QString,QString)"))
        return fail ("Robot source commit must be an invokable QString operation.");
    if (metaObject.indexOfSlot ("importRobotProjectSource(QString)") < 0)
        return fail ("The historical robot source import slot must remain available.");
    return 0;
}
