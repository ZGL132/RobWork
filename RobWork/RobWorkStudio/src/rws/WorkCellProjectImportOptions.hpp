#ifndef RWS_WORKCELLPROJECTIMPORTOPTIONS_HPP
#define RWS_WORKCELLPROJECTIMPORTOPTIONS_HPP

#include <QStringList>

namespace rws {

struct WorkCellProjectImportOptions
{
    QString targetDeviceName;
    QString tcpFrameName;
    // Companion files selected by the wizard. They are always copied into the project.
    QStringList companionFiles;
};

}    // namespace rws

#endif    // RWS_WORKCELLPROJECTIMPORTOPTIONS_HPP
