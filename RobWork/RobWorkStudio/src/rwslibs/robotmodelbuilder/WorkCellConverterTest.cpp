#include "RobotModelXmlWriter.hpp"
#include "WorkCellConverter.hpp"

#include <rw/loaders/WorkCellLoader.hpp>
#include <rw/models/WorkCell.hpp>

#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>

#include <iostream>

static int fail (const QString& message)
{
    std::cerr << message.toStdString () << std::endl;
    return 1;
}

static QString sourceRoot ()
{
    QDir dir (QFileInfo (__FILE__).absolutePath ());
    if (!dir.cdUp () || !dir.cdUp () || !dir.cdUp () || !dir.cdUp ())
        return QString ();
    return dir.absolutePath ();
}

int main ()
{
    QTemporaryDir dir;
    if (!dir.isValid ())
        return fail ("Could not create temporary directory.");

    rws::RobotModelSpec original =
        rws::RobotModelXmlWriter::makeDefaultSixAxisModel (dir.path ());
    original.robotName = "RoundTripBot";
    original.proximitySetup.enabled = true;
    original.proximitySetup.useExcludeStaticPairs = true;
    {
        rws::ProximityRuleSpec rule;
        rule.kind = rws::ProximityRuleKind::Exclude;
        rule.patternA = "Joint.*";
        rule.patternB = "Table";
        original.proximitySetup.rules.push_back (rule);
    }
    original.collisionSetup.excludeStaticPairs = true;

    QStringList saveErrors;
    if (!rws::RobotModelXmlWriter::saveFiles (original, saveErrors))
        return fail ("Could not save generated XML: " + saveErrors.join ("; "));

    rw::models::WorkCell::Ptr wc =
        rw::loaders::WorkCellLoader::Factory::load (
            rws::RobotModelXmlWriter::sceneFilePath (original).toStdString ());
    if (wc == NULL)
        return fail ("WorkCellLoader returned null.");

    QStringList warnings;
    rws::RobotModelSpec imported =
        rws::WorkCellConverter::convert (*wc, wc->getDefaultState (),
                                         dir.path ().toStdString (), warnings);

    if (imported.robotName != original.robotName)
        return fail ("Robot name was not recovered.");
    if (imported.transformJoints.size () != original.transformJoints.size ())
        return fail ("Joint count was not recovered.");
    if (imported.limits.size () != original.limits.size ())
        return fail ("Joint limits were not recovered.");
    if (imported.generateScene != true)
        return fail ("Scene generation flag should be true for loaded scene.");
    if (!imported.proximitySetup.enabled)
        return fail ("ProximitySetup enable flag was not recovered.");
    if (imported.proximitySetup.file != original.proximitySetup.file)
        return fail ("ProximitySetup filename was not recovered.");
    if (!imported.proximitySetup.useExcludeStaticPairs)
        return fail ("ProximitySetup UseExcludeStaticPairs flag was not recovered.");
    if (imported.proximitySetup.rules.empty ())
        return fail ("ProximitySetup companion rules were not recovered.");
    if (!imported.collisionSetup.enabled)
        return fail ("CollisionSetup enable flag was not recovered.");
    if (imported.collisionSetup.file != original.collisionSetup.file)
        return fail ("CollisionSetup filename was not recovered.");
    if (!imported.collisionSetup.excludeStaticPairs)
        return fail ("CollisionSetup ExcludeStaticPairs flag was not recovered.");

    rws::RobotModelSpec sidecarSpec = original;
    sidecarSpec.generateDrawables = false;
    if (!rws::RobotModelXmlWriter::saveSpecSidecar (sidecarSpec, saveErrors))
        return fail ("Could not save generated sidecar: " + saveErrors.join ("; "));

    rws::RobotModelSpec importedWithSidecar =
        rws::WorkCellConverter::convert (*wc, wc->getDefaultState (),
                                         dir.path ().toStdString (), warnings);
    if (importedWithSidecar.generateDrawables)
        return fail ("Sidecar metadata was not used as the authoritative editable spec.");

    const QString urFile =
        QDir (sourceRoot ()).filePath ("RobWork/example/ModelData/XMLDevices/"
                                      "UR-6-85-5-A/UR.wc.xml");
    rw::models::WorkCell::Ptr ur =
        rw::loaders::WorkCellLoader::Factory::load (urFile.toStdString ());
    if (ur == NULL)
        return fail ("Could not load UR-6-85-5-A fixture.");

    QStringList urWarnings;
    rws::RobotModelSpec urSpec =
        rws::WorkCellConverter::convert (*ur, ur->getDefaultState (),
                                         QFileInfo (urFile).absolutePath ().toStdString (),
                                         urWarnings);
    rws::RobotModelXmlWriter::applyLinkGeometry (urSpec);
    QStringList validationErrors;
    if (!rws::RobotModelXmlWriter::validate (urSpec, validationErrors))
        return fail ("Imported UR fixture should validate without unknown frame errors: " +
                     validationErrors.join ("; "));

    std::cout << "WorkCellConverter smoke test passed." << std::endl;
    return 0;
}
