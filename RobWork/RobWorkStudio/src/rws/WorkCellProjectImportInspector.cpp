#include "WorkCellProjectImportInspector.hpp"

#include <rw/kinematics/Frame.hpp>
#include <rw/loaders/WorkCellLoader.hpp>
#include <rw/models/Device.hpp>
#include <rw/models/JointDevice.hpp>
#include <rw/models/WorkCell.hpp>

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>

#include <exception>

namespace {

void setError (QString* error, const QString& message)
{
    if (error != nullptr)
        *error = message;
}

bool belongsToDevice (const rw::kinematics::Frame* frame,
                      const rw::kinematics::Frame* base)
{
    for (const rw::kinematics::Frame* current = frame; current != nullptr;
         current = current->getParent ()) {
        if (current == base)
            return true;
    }
    return false;
}

QString companionKind (const QString& fileName)
{
    if (fileName.contains (QStringLiteral ("CollisionSetup"), Qt::CaseInsensitive))
        return QStringLiteral ("collision-setup");
    if (fileName.contains (QStringLiteral ("ProximitySetup"), Qt::CaseInsensitive))
        return QStringLiteral ("proximity-setup");
    if (fileName.endsWith (QStringLiteral (".dwc.xml"), Qt::CaseInsensitive))
        return QStringLiteral ("dynamic-workcell");
    return QString ();
}

}    // namespace

namespace rws {

bool WorkCellProjectImportInspection::isValid (QString* error) const
{
    return WorkCellProjectImportInspector::validateSelection (
        *this, selectedDeviceName, selectedTcpFrameName, error);
}

bool WorkCellProjectImportInspector::inspect (const QString& sourcePath,
                                               WorkCellProjectImportInspection& inspection,
                                               QString* error)
{
    inspection = WorkCellProjectImportInspection {};
    const QFileInfo sourceInfo (sourcePath);
    if (!sourceInfo.exists () || !sourceInfo.isFile ()) {
        setError (error, QStringLiteral ("The selected WorkCell file does not exist: %1").arg (sourcePath));
        return false;
    }

    rw::models::WorkCell::Ptr workcell;
    try {
        workcell = rw::loaders::WorkCellLoader::Factory::load (
            sourceInfo.absoluteFilePath ().toStdString ());
    }
    catch (const std::exception& exception) {
        setError (error, QStringLiteral ("Unable to load WorkCell: %1").arg (
                             QString::fromUtf8 (exception.what ())));
        return false;
    }
    if (workcell.isNull ()) {
        setError (error, QStringLiteral ("Unable to load the selected WorkCell."));
        return false;
    }

    inspection.sourcePath = sourceInfo.absoluteFilePath ();
    inspection.frameCount = static_cast< int > (workcell->getFrames ().size ());
    inspection.deviceCount = static_cast< int > (workcell->getDevices ().size ());
    for (const rw::core::Ptr< rw::models::Device >& device : workcell->getDevices ()) {
        const rw::core::Ptr< rw::models::JointDevice > jointDevice =
            device.cast< rw::models::JointDevice > ();
        if (jointDevice.isNull () || jointDevice->getBase () == nullptr ||
            jointDevice->getEnd () == nullptr)
            continue;

        WorkCellImportDeviceInfo info;
        info.name = QString::fromStdString (jointDevice->getName ());
        info.baseFrameName = QString::fromStdString (jointDevice->getBase ()->getName ());
        info.endFrameName = QString::fromStdString (jointDevice->getEnd ()->getName ());
        info.dof = static_cast< int > (jointDevice->getDOF ());
        for (rw::kinematics::Frame* frame : workcell->getFrames ()) {
            if (belongsToDevice (frame, jointDevice->getBase ()))
                info.tcpFrameNames << QString::fromStdString (frame->getName ());
        }
        if (!info.tcpFrameNames.contains (info.endFrameName))
            info.tcpFrameNames << info.endFrameName;
        inspection.devices.push_back (info);
    }
    if (inspection.devices.isEmpty ()) {
        setError (error, QStringLiteral ("The WorkCell contains no convertible serial robot device."));
        return false;
    }

    inspection.selectedDeviceName = inspection.devices.front ().name;
    inspection.selectedTcpFrameName = inspection.devices.front ().endFrameName;
    QDirIterator iterator (sourceInfo.absolutePath (), QDir::Files, QDirIterator::NoIteratorFlags);
    while (iterator.hasNext ()) {
        const QString path = iterator.next ();
        const QString kind = companionKind (QFileInfo (path).fileName ());
        if (!kind.isEmpty ())
            inspection.companions.push_back ({kind, path, true, true});
    }
    return true;
}

bool WorkCellProjectImportInspector::validateSelection (
    const WorkCellProjectImportInspection& inspection,
    const QString& deviceName,
    const QString& tcpFrameName,
    QString* error)
{
    for (const WorkCellImportDeviceInfo& device : inspection.devices) {
        if (device.name != deviceName)
            continue;
        if (device.tcpFrameNames.contains (tcpFrameName))
            return true;
        setError (error, QStringLiteral ("The selected TCP frame is not part of the target device."));
        return false;
    }
    setError (error, QStringLiteral ("Select a valid serial robot device before continuing."));
    return false;
}

}    // namespace rws
