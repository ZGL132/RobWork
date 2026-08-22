#include "FourPluginAcceptanceFixture.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QElapsedTimer>

#include <rw/loaders/WorkCellLoader.hpp>
#include <rw/models/SerialDevice.hpp>
#include <rw/models/WorkCell.hpp>

#include <cmath>
#include <exception>

namespace {

QString sourcePath (const QString& relativePath)
{
    return QDir (QStringLiteral (FOURPLUGIN_ACCEPTANCE_SOURCE_DIR)).filePath (relativePath);
}

QString sha256File (const QString& path)
{
    QFile file (path);
    if (!file.open (QIODevice::ReadOnly))
        return QString ();
    return QString::fromLatin1 (
        QCryptographicHash::hash (file.readAll (), QCryptographicHash::Sha256).toHex ());
}

bool finiteBounds (const rw::models::Device::QBox& bounds)
{
    if (bounds.first.size () != 6 || bounds.second.size () != 6)
        return false;
    for (size_t i = 0; i < 6; ++i) {
        if (!std::isfinite (bounds.first [i]) || !std::isfinite (bounds.second [i]) ||
            bounds.first [i] >= bounds.second [i])
            return false;
    }
    return true;
}

rw::kinematics::Frame* findScopedFrame (const rw::models::WorkCell& workcell,
                                        const std::string& name)
{
    rw::kinematics::Frame* frame = workcell.findFrame (name);
    if (frame == NULL)
        frame = workcell.findFrame (std::string ("GenericSixAxis.") + name);
    return frame;
}

QString writeArtifacts (const sdurws::fourpluginacceptance::FixtureResult& result,
                        const QString& workcellSha,
                        const QString& sceneSha,
                        bool workcellExists,
                        bool sceneExists,
                        bool deviceOk,
                        bool jointsOk,
                        bool tcpOk,
                        bool limitsOk)
{
    const QString csvPath = QStringLiteral ("four-plugin-cases.csv");
    QFile csv (csvPath);
    if (csv.open (QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        QTextStream stream (&csv);
        stream << "case-id,status,duration-ms,error-code,artifact-path\n";
        stream << "S-B1-FIXTURE," << (result.passed ? "PASS" : "FAIL") << ','
               << result.durationMs << ',' << result.errorCode << ',' << csvPath << '\n';
    }

    QJsonObject root;
    root.insert (QStringLiteral ("schema"), QStringLiteral ("four-plugin-acceptance-v1"));
    root.insert (QStringLiteral ("fixture"), QStringLiteral ("GenericSixAxis"));
    root.insert (QStringLiteral ("status"), result.passed ? QStringLiteral ("PASS")
                                                           : QStringLiteral ("FAIL"));
    root.insert (QStringLiteral ("error-code"), result.errorCode);
    root.insert (QStringLiteral ("duration-ms"), static_cast<qint64> (result.durationMs));
    QJsonArray resources;
    QJsonObject workcell;
    workcell.insert (QStringLiteral ("path"),
                     QStringLiteral ("RobWork/example/ModelData/XMLDevices/GenericSixAxis/GenericSixAxis.wc.xml"));
    workcell.insert (QStringLiteral ("exists"), workcellExists);
    workcell.insert (QStringLiteral ("sha256"), workcellSha);
    resources.append (workcell);
    QJsonObject scene;
    scene.insert (QStringLiteral ("path"),
                  QStringLiteral ("RobWork/example/ModelData/XMLDevices/GenericSixAxis/GenericSixAxisScene.wc.xml"));
    scene.insert (QStringLiteral ("exists"), sceneExists);
    scene.insert (QStringLiteral ("sha256"), sceneSha);
    resources.append (scene);
    root.insert (QStringLiteral ("resources"), resources);
    QJsonObject structure;
    structure.insert (QStringLiteral ("device"), QStringLiteral ("GenericSixAxis"));
    structure.insert (QStringLiteral ("dof"), 6);
    structure.insert (QStringLiteral ("tcp"), QStringLiteral ("TCP"));
    structure.insert (QStringLiteral ("joint-count"), 6);
    structure.insert (QStringLiteral ("device-ok"), deviceOk);
    structure.insert (QStringLiteral ("joints-ok"), jointsOk);
    structure.insert (QStringLiteral ("tcp-ok"), tcpOk);
    structure.insert (QStringLiteral ("finite-limits"), limitsOk);
    root.insert (QStringLiteral ("structure"), structure);

    QFile json (QStringLiteral ("four-plugin-summary.json"));
    if (json.open (QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        json.write (QJsonDocument (root).toJson (QJsonDocument::Indented));
    return QStringLiteral ("four-plugin-summary.json");
}

}    // namespace

namespace sdurws { namespace fourpluginacceptance {

FixtureResult runFourPluginAcceptanceFixture ()
{
    QElapsedTimer timer;
    timer.start ();
    FixtureResult result { false, QStringLiteral ("unknown"), QString (), 0 };
    const QString workcellRelative =
        QStringLiteral ("RobWork/example/ModelData/XMLDevices/GenericSixAxis/GenericSixAxis.wc.xml");
    const QString sceneRelative = QStringLiteral (
        "RobWork/example/ModelData/XMLDevices/GenericSixAxis/GenericSixAxisScene.wc.xml");
    const QString workcellPath = sourcePath (workcellRelative);
    const QString scenePath = sourcePath (sceneRelative);
    const bool workcellExists = QFile::exists (workcellPath);
    const bool sceneExists = QFile::exists (scenePath);
    const QString workcellSha = sha256File (workcellPath);
    const QString sceneSha = sha256File (scenePath);
    bool deviceOk = false;
    bool jointsOk = false;
    bool tcpOk = false;
    bool limitsOk = false;

    try {
        if (!workcellExists || !sceneExists) {
            result.errorCode = QStringLiteral ("fixture_resource_missing");
            result.errorMessage = QStringLiteral ("GenericSixAxis resource is missing");
        }
        else {
            const rw::models::WorkCell::Ptr workcell =
                rw::loaders::WorkCellLoader::Factory::load (workcellPath.toStdString ());
            const rw::models::WorkCell::Ptr scene =
                rw::loaders::WorkCellLoader::Factory::load (scenePath.toStdString ());
            if (workcell.isNull () || scene.isNull ()) {
                result.errorCode = QStringLiteral ("fixture_workcell_load_failed");
                result.errorMessage = QStringLiteral ("GenericSixAxis WorkCell load returned null");
            }
            else {
                const rw::models::Device::Ptr device = workcell->findDevice ("GenericSixAxis");
                const rw::models::Device::Ptr sceneDevice = scene->findDevice ("GenericSixAxis");
                deviceOk = !device.isNull () && !sceneDevice.isNull () &&
                           device->getDOF () == 6 && sceneDevice->getDOF () == 6;
                jointsOk = true;
                for (int i = 1; i <= 6; ++i) {
                    const std::string name = std::string ("Joint") + std::to_string (i);
                    jointsOk = jointsOk && findScopedFrame (*workcell, name) != NULL &&
                               findScopedFrame (*scene, name) != NULL;
                }
                tcpOk = findScopedFrame (*workcell, "TCP") != NULL &&
                        findScopedFrame (*scene, "TCP") != NULL;
                limitsOk = deviceOk && finiteBounds (device->getBounds ()) &&
                           finiteBounds (sceneDevice->getBounds ());
                if (deviceOk && jointsOk && tcpOk && limitsOk) {
                    result.passed = true;
                    result.errorCode = QStringLiteral ("none");
                    result.errorMessage = QStringLiteral ("GenericSixAxis structure validated");
                }
                else {
                    result.errorCode = QStringLiteral ("fixture_structure_mismatch");
                    result.errorMessage = QStringLiteral (
                        "Expected GenericSixAxis, six joints, TCP and finite six-dimensional limits "
                        "(device=%1,joints=%2,tcp=%3,limits=%4)")
                                               .arg (deviceOk)
                                               .arg (jointsOk)
                                               .arg (tcpOk)
                                               .arg (limitsOk);
                }
            }
        }
    }
    catch (const std::exception& error) {
        result.errorCode = QStringLiteral ("fixture_exception");
        result.errorMessage = QString::fromLocal8Bit (error.what ());
    }
    catch (...) {
        result.errorCode = QStringLiteral ("fixture_unknown_exception");
        result.errorMessage = QStringLiteral ("Unknown exception while loading fixture");
    }

    result.durationMs = timer.elapsed ();
    writeArtifacts (result,
                    workcellSha,
                    sceneSha,
                    workcellExists,
                    sceneExists,
                    deviceOk,
                    jointsOk,
                    tcpOk,
                    limitsOk);
    return result;
}

}}    // namespace sdurws::fourpluginacceptance
