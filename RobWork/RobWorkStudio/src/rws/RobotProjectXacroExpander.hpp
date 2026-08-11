#ifndef RWS_ROBOTPROJECTXACROEXPANDER_HPP
#define RWS_ROBOTPROJECTXACROEXPANDER_HPP

#include <QByteArray>
#include <QString>
#include <QStringList>

namespace rws {

struct XacroExpansionResult
{
    QByteArray urdf;
    QString command;
};

class RobotProjectXacroExpander
{
  public:
    //! Whether an explicit, environment-provided, or discoverable Xacro command exists.
    static bool canExpand (const QString& executable = QString ());

    static bool expand (const QString& sourcePath,
                        const QString& executable,
                        XacroExpansionResult& result,
                        QString* error = nullptr);
    static bool expand (const QString& sourcePath,
                        const QString& executable,
                        const QStringList& arguments,
                        XacroExpansionResult& result,
                        QString* error = nullptr);
};

}    // namespace rws

#endif
