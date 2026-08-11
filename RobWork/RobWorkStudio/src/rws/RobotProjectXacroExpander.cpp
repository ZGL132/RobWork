#include "RobotProjectXacroExpander.hpp"

#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

namespace {

QString findPythonExecutable ()
{
    QString python = QStandardPaths::findExecutable (QStringLiteral ("python"));
    if (python.isEmpty ())
        python = QStandardPaths::findExecutable (QStringLiteral ("python3"));
    return python;
}

bool pythonCanImportXacro (const QString& python)
{
    static QString checkedPython;
    static bool available = false;
    if (python == checkedPython)
        return available;

    checkedPython = python;
    available = false;
    QProcess probe;
    probe.setProgram (python);
    probe.setArguments ({QStringLiteral ("-c"), QStringLiteral ("import xacro")});
    probe.start ();
    if (probe.waitForStarted (3000) && probe.waitForFinished (5000) &&
        probe.exitStatus () == QProcess::NormalExit && probe.exitCode () == 0)
        available = true;
    return available;
}

bool buildCommand (const QString& requestedExecutable,
                   const QString& sourcePath,
                   const QStringList& userArguments,
                   QString& program,
                   QStringList& arguments,
                   QString& description,
                   QString* error)
{
    const QString configured = requestedExecutable.trimmed ().isEmpty ()
        ? qEnvironmentVariable ("RWS_XACRO_EXECUTABLE").trimmed ()
        : requestedExecutable.trimmed ();
    if (!configured.isEmpty ()) {
        program = configured;
        arguments = {sourcePath};
        arguments.append (userArguments);
        description = program;
        return true;
    }

    const QString rosXacro = QStandardPaths::findExecutable (QStringLiteral ("xacro"));
    if (!rosXacro.isEmpty ()) {
        program = rosXacro;
        arguments = {sourcePath};
        arguments.append (userArguments);
        description = QStringLiteral ("xacro");
        return true;
    }

    const QString python = findPythonExecutable ();
    if (!python.isEmpty () && pythonCanImportXacro (python)) {
        program = python;
        arguments = {QStringLiteral ("-m"), QStringLiteral ("xacro"), sourcePath};
        arguments.append (userArguments);
        description = QStringLiteral ("python -m xacro");
        return true;
    }

    if (error != nullptr)
        *error = QStringLiteral ("No Xacro command was found. Configure one explicitly, install ROS xacro, or install Python xacro.");
    return false;
}

}    // namespace

namespace rws {

bool RobotProjectXacroExpander::canExpand (const QString& executable)
{
    QString program;
    QStringList arguments;
    QString description;
    return buildCommand (executable, QStringLiteral ("source.xacro"), QStringList {},
                         program, arguments, description, nullptr);
}

bool RobotProjectXacroExpander::expand (const QString& sourcePath,
                                        const QString& executable,
                                        XacroExpansionResult& result,
                                        QString* error)
{
    return expand (sourcePath, executable, QStringList {}, result, error);
}

bool RobotProjectXacroExpander::expand (const QString& sourcePath,
                                        const QString& executable,
                                        const QStringList& arguments,
                                        XacroExpansionResult& result,
                                        QString* error)
{
    result = XacroExpansionResult {};
    if (!QFileInfo (sourcePath).isFile ()) {
        if (error != nullptr)
            *error = QStringLiteral ("Xacro source is not an ordinary file: %1").arg (sourcePath);
        return false;
    }
    QString program;
    QStringList commandArguments;
    QString description;
    if (!buildCommand (executable, sourcePath, arguments, program, commandArguments, description,
                       error))
        return false;
    QProcess process;
    process.setProgram (program);
    process.setArguments (commandArguments);
    process.setProcessChannelMode (QProcess::SeparateChannels);
    process.start ();
    if (!process.waitForStarted (5000)) {
        if (error != nullptr)
            *error = QStringLiteral ("Could not start Xacro executable '%1': %2")
                         .arg (description, process.errorString ());
        return false;
    }
    if (!process.waitForFinished (120000) || process.exitStatus () != QProcess::NormalExit ||
        process.exitCode () != 0) {
        if (error != nullptr)
            *error = QStringLiteral ("Xacro expansion failed: %1")
                         .arg (QString::fromLocal8Bit (process.readAllStandardError ()).trimmed ());
        return false;
    }
    result.urdf = process.readAllStandardOutput ();
    if (result.urdf.trimmed ().isEmpty ()) {
        if (error != nullptr)
            *error = QStringLiteral ("Xacro executable produced an empty URDF.");
        return false;
    }
    result.command = description + QStringLiteral (" \"") + sourcePath + QStringLiteral ("\"") +
                     (arguments.isEmpty () ? QString () :
                         QStringLiteral (" ") + arguments.join (QLatin1Char (' ')));
    return true;
}

}    // namespace rws
