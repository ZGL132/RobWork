#include "RobotModelXmlWriter.hpp"
#include "WorkCellConverter.hpp"

#include <rw/loaders/WorkCellLoader.hpp>
#include <rw/models/WorkCell.hpp>

#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>

#include <algorithm>
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
    rws::DrawableSpec importedDrawable;
    importedDrawable.name = "ImportedBox";
    importedDrawable.refFrame = "Joint1";
    importedDrawable.shape = "Box";
    importedDrawable.dimensions = {{1.25, 2.5, 3.75}};
    importedDrawable.rpyDeg = {{11.0, 22.0, 33.0}};
    importedDrawable.pos = {{0.12, 0.23, 0.34}};
    importedDrawable.rgb = {{0.15, 0.45, 0.75}};
    importedDrawable.collisionModel = true;
    original.drawables.push_back (importedDrawable);
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

    const auto importedDrawableIt = std::find_if (
        imported.drawables.begin (), imported.drawables.end (),
        [] (const rws::DrawableSpec& drawable) { return drawable.name == "ImportedBox"; });
    if (importedDrawableIt == imported.drawables.end ())
        return fail ("Imported drawable was not recovered.");
    if (importedDrawableIt->dimensions != importedDrawable.dimensions ||
        importedDrawableIt->rpyDeg != importedDrawable.rpyDeg ||
        importedDrawableIt->pos != importedDrawable.pos ||
        importedDrawableIt->rgb != importedDrawable.rgb ||
        !importedDrawableIt->collisionModel)
        return fail ("Imported drawable geometry was not preserved.");

    if (!imported.imported.active)
        return fail ("Imported document metadata was not set.");
    imported.imported.deviceFile = "imported/RobotDevice.wc.xml";
    imported.imported.sceneFile = "imported/RobotScene.wc.xml";
    if (!rws::RobotModelXmlWriter::saveFiles (imported, saveErrors))
        return fail ("Could not save imported geometry: " + saveErrors.join ("; "));
    if (!QFileInfo::exists (rws::RobotModelXmlWriter::serialDeviceFilePath (imported)) ||
        !QFileInfo::exists (rws::RobotModelXmlWriter::sceneFilePath (imported)))
        return fail ("Imported document targets were not written.");
    rw::models::WorkCell::Ptr reloaded = rw::loaders::WorkCellLoader::Factory::load (
        rws::RobotModelXmlWriter::sceneFilePath (imported).toStdString ());
    if (reloaded == NULL)
        return fail ("Saved imported scene could not be loaded.");

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
