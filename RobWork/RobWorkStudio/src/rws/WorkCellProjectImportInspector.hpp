#ifndef RWS_WORKCELLPROJECTIMPORTINSPECTOR_HPP
#define RWS_WORKCELLPROJECTIMPORTINSPECTOR_HPP

#include <QString>
#include <QVector>

namespace rws {

struct WorkCellImportDeviceInfo
{
    QString name;
    QString baseFrameName;
    QString endFrameName;
    QStringList tcpFrameNames;
    int dof = 0;
};

struct WorkCellCompanionFile
{
    QString kind;
    QString path;
    bool selected = true;
    bool autoDiscovered = true;
};

struct WorkCellProjectImportInspection
{
    QString sourcePath;
    int frameCount = 0;
    int deviceCount = 0;
    QVector< WorkCellImportDeviceInfo > devices;
    QVector< WorkCellCompanionFile > companions;
    QString selectedDeviceName;
    QString selectedTcpFrameName;
    QStringList warnings;

    bool isValid (QString* error = nullptr) const;
};

class WorkCellProjectImportInspector
{
  public:
    static bool inspect (const QString& sourcePath,
                         WorkCellProjectImportInspection& inspection,
                         QString* error = nullptr);
    static bool validateSelection (const WorkCellProjectImportInspection& inspection,
                                   const QString& deviceName,
                                   const QString& tcpFrameName,
                                   QString* error = nullptr);
};

}    // namespace rws

#endif    // RWS_WORKCELLPROJECTIMPORTINSPECTOR_HPP
