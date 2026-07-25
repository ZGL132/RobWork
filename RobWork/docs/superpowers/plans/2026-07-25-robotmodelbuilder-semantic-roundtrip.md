# RobotModelBuilder Semantic Round-Trip Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Import a main-window WorkCell into RobotModelBuilder without changing its supported model semantics, and save a normalized equivalent document to the imported targets.

**Architecture:** `RobotModelSpec` carries editable model data plus non-UI import provenance and opaque extension XML. `WorkCellConverter` parses the source XML before using runtime data, so it can recover supported geometry exactly and preserve unsupported elements. The widget retains provenance while collecting edits, and the writer uses it to select output paths and append validated extensions.

**Tech Stack:** C++17, Qt Core/XML (`QDomDocument`, `QXmlStreamReader`), RobWork `WorkCellLoader`, RobotModelBuilder model/writer tests, CTest.

---

## File Structure

- `RobotModelSpec.hpp`: holds imported output names and scoped opaque extension XML.
- `RobotModelSpecJson.cpp/.hpp`: persists provenance and extensions in the existing sidecar.
- `WorkCellConverter.cpp/.hpp`: reads source documents, maps supported geometry, and avoids synthesis during import.
- `RobotModelXmlWriter.cpp/.hpp`: resolves imported targets and writes validated extensions.
- `RobotModelBuilderWidget.cpp/.hpp`: retains non-UI import metadata while the visible tables are edited.
- `WorkCellConverterTest.cpp`: verifies model-level import and save/reload semantic equivalence.
- `RobotModelSpecJsonTest.cpp`: verifies that sidecar serialization does not drop imported metadata.

### Task 1: Establish the semantic round-trip contract in tests

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/WorkCellConverterTest.cpp`

- [ ] **Step 1: Write a failing fixture round-trip test**

Create a `QTemporaryDir` fixture containing a device XML, scene XML, setup files, and a mesh file reference. The device XML must include a Box Drawable, Cylinder Drawable, Polytope Drawable, a CollisionModel, and an unknown `<ImportedExtension semantic="keep"/>`; the scene XML must contain a Sphere scene Drawable and include the device file. Load it through `WorkCellLoader`, convert it, save the spec, reload the saved scene, and assert the supported geometry values and extension are present:

```cpp
if (imported.drawables.size () != 3 ||
    imported.drawables[0].shape != "Box" ||
    imported.drawables[0].dimensions != std::array<double, 3>{{1.0, 2.0, 3.0}} ||
    imported.drawables[1].shape != "Cylinder" ||
    imported.drawables[1].radius != 0.25 ||
    imported.drawables[2].filePath != "mesh.stl")
    return fail ("Imported drawable geometry was not preserved.");

if (!RobotModelXmlWriter::saveFiles (imported, errors))
    return fail ("Could not save imported model: " + errors.join ("; "));

if (!QFile (RobotModelXmlWriter::serialDeviceFilePath (imported)).open (QFile::ReadOnly))
    return fail ("Imported target device XML was not written.");
```

- [ ] **Step 2: Run the new test and confirm RED**

Run:

```powershell
cmake --build build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release --target sdurws_robotmodelbuilder_workcellconvertertest --config Release
ctest --test-dir build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release -C Release -R sdurws_robotmodelbuilder_workcellconvertertest --output-on-failure
```

Expected: failure reporting that imported drawable geometry or imported target identity was not preserved.

### Task 2: Add import provenance and opaque extensions to the model

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelSpec.hpp`
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelSpecJson.cpp`
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelSpecJsonTest.cpp`

- [ ] **Step 1: Write the failing JSON round-trip assertions**

Add an imported-spec test with explicit metadata:

```cpp
spec.imported.sceneFile = "CustomScene.wc.xml";
spec.imported.deviceFile = "vendor/Robot.wc.xml";
spec.imported.workcellExtensions.push_back ("<ImportedExtension semantic=\"keep\" />");
```

After `RobotModelSpecJson::fromJson`, assert all three values are unchanged. Expected failure: the JSON lacks `imported` data.

- [ ] **Step 2: Add focused metadata types**

Add before `RobotModelSpec`:

```cpp
struct ImportedDocumentSpec {
    bool active = false;
    std::string sceneFile;
    std::string deviceFile;
    std::vector<std::string> workcellExtensions;
    std::vector<std::string> deviceExtensions;
};
```

Add `ImportedDocumentSpec imported;` to `RobotModelSpec`. Do not put editable geometry or table data in this structure.

- [ ] **Step 3: Serialize the metadata**

In `RobotModelSpecJson::toJson`, emit an `imported` object with `active`, `sceneFile`, `deviceFile`, `workcellExtensions`, and `deviceExtensions`; in `fromJson`, read the same object and default absent fields to an inactive empty struct. Reject non-string entries in either extension array with the existing error-return convention.

- [ ] **Step 4: Verify GREEN**

Run:

```powershell
cmake --build build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release --target sdurws_robotmodelbuilder_jsontest --config Release
ctest --test-dir build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release -C Release -R sdurws_robotmodelbuilder_jsontest --output-on-failure
```

Expected: `sdurws_robotmodelbuilder_jsontest` passes.

- [ ] **Step 5: Commit the data-model change**

```powershell
git add RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelSpec.hpp RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelSpecJson.cpp RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelSpecJsonTest.cpp
git commit -m "feat: retain imported robot model metadata"
```

### Task 3: Parse source XML geometry without generating defaults

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/WorkCellConverter.hpp`
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/WorkCellConverter.cpp`
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/WorkCellConverterTest.cpp`

- [ ] **Step 1: Add source-parser declarations**

Add private converter helpers:

```cpp
static bool mergeSourceDocuments (const rw::models::WorkCell& workcell,
                                  RobotModelSpec& spec, QStringList& warnings);
static bool mergeDeviceXml (const QString& file, RobotModelSpec& spec,
                            QStringList& warnings);
static bool mergeSceneXml (const QString& file, RobotModelSpec& spec,
                           QStringList& warnings);
```

- [ ] **Step 2: Implement supported geometry parsing**

Use `QDomDocument` to parse each source file. For every supported `Drawable` and `CollisionModel`, read `name`, `refframe`, `colmodel`, optional `RPY`, `Pos`, `RGB`, and exactly one shape child. Map shape attributes as follows:

```cpp
Box:      x, y, z -> dimensions
Cylinder: radius, z -> radius, length
Sphere:   radius -> radius
Cone:     radius, z -> radius, length
Plane:    x, y -> dimensions[0], dimensions[1]
Polytope/Mesh/STL: file -> filePath
```

Replace the old hard-coded-Box `extractDrawables` result with source-derived entries when a readable source device is available. Preserve runtime-only objects as warnings and do not fabricate a geometry entry for them.

- [ ] **Step 3: Preserve source extension elements**

For child elements of `WorkCell` and `SerialDevice` that are not in the supported writer vocabulary, append `element.ownerDocument().toString()`-equivalent element XML to the matching `spec.imported.*Extensions` collection. Keep only elements that parse as exactly one XML element when wrapped in a temporary root; otherwise issue a warning and exclude them.

- [ ] **Step 4: Set target identity and stop default generation**

Set `spec.imported.active`, `sceneFile`, and resolved device-file name during source merge. In `convert`, remove the import-time call to `RobotModelXmlWriter::applyDefaultDrawables`; that function remains only in new-model creation workflows. Call `mergeSourceDocuments` before companion XML metadata so companion paths use the actual imported scene.

- [ ] **Step 5: Run the converter test and confirm GREEN**

Run the commands from Task 1. Expected: the fixture’s primitive values, mesh path, collision model, scene geometry, and imported target files pass their assertions.

- [ ] **Step 6: Commit the source converter change**

```powershell
git add RobWorkStudio/src/rwslibs/robotmodelbuilder/WorkCellConverter.hpp RobWorkStudio/src/rwslibs/robotmodelbuilder/WorkCellConverter.cpp RobWorkStudio/src/rwslibs/robotmodelbuilder/WorkCellConverterTest.cpp
git commit -m "feat: preserve imported robot geometry"
```

### Task 4: Write normalized XML to imported targets and retain extensions

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriter.hpp`
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriter.cpp`
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/WorkCellConverterTest.cpp`

- [ ] **Step 1: Add failing output-path and extension assertions**

Extend the fixture test to assert that `saveFiles(imported, errors)` writes `CustomScene.wc.xml` and `vendor/Robot.wc.xml`, rather than the robot-name-derived filenames, and that each output still contains `<ImportedExtension semantic="keep"/>`.

- [ ] **Step 2: Resolve imported output paths**

Change the path helpers to prefer imported relative targets when `spec.imported.active`:

```cpp
static QString importedOrGeneratedPath (const RobotModelSpec& spec,
                                        const std::string& imported,
                                        const QString& generated)
{
    const QString value = QString::fromStdString (imported).trimmed ();
    return spec.imported.active && !value.isEmpty ()
        ? QDir (QString::fromStdString (spec.saveDirectory)).filePath (value)
        : generated;
}
```

Use it in `serialDeviceFilePath` and `sceneFilePath`. Ensure `saveFiles` creates the parent directory of every selected output before opening it.

- [ ] **Step 3: Append only validated opaque extensions**

Add a private writer helper that wraps each extension in `<Root>`, parses it with `QDomDocument`, and writes its element XML only if valid. Call it before the closing `</SerialDevice>` and `</WorkCell>` respectively. If validation fails, append an error and make `saveFiles` return false rather than silently dropping semantic content.

- [ ] **Step 4: Verify GREEN**

Run the Task 1 converter test. Expected: saved files use imported targets; reloading the saved scene preserves the asserted geometry and setup semantics; the unsupported extension remains present.

- [ ] **Step 5: Commit the writer change**

```powershell
git add RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriter.hpp RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriter.cpp RobWorkStudio/src/rwslibs/robotmodelbuilder/WorkCellConverterTest.cpp
git commit -m "feat: save imported robot models semantically"
```

### Task 5: Keep import metadata through the plugin UI

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidget.hpp`
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidget.cpp`
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderPlugin.cpp`

- [ ] **Step 1: Add a failing widget-level metadata preservation test or testable helper**

Extract a non-GUI helper from `collectSpec` if necessary and test this behavior: an imported spec with `imported.active=true` is passed through `syncFromWorkCellSpec`, visible fields are edited, and the collected save spec retains the imported paths and extensions. The test must fail before the widget stores hidden metadata.

- [ ] **Step 2: Retain only hidden import data in the widget**

Add:

```cpp
ImportedDocumentSpec _importedDocument;
```

In `syncFromWorkCellSpec`, copy `spec.imported` before filling controls. In `collectSpec`, set `spec.imported = _importedDocument` after collecting all visible fields. In `resetDefaults` and successful URDF import, reset `_importedDocument` to an inactive default so a new model cannot overwrite a previously imported file.

- [ ] **Step 3: Report unsafe preservation warnings**

When the converter reports a source/extension warning, retain it in the sync status and do not clear it by preview generation. Saving remains enabled only if writer validation succeeds; errors are shown with the existing `showErrors` path.

- [ ] **Step 4: Verify plugin and model targets**

Run:

```powershell
cmake --build build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release --target sdurws_robotmodelbuilder sdurws_robotmodelbuilder_workcellconvertertest sdurws_robotmodelbuilder_jsontest --config Release
ctest --test-dir build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release -C Release -R 'sdurws_robotmodelbuilder_(xmltest|jsontest|workcellconvertertest)' --output-on-failure
```

Expected: all three tests pass and the plugin target compiles.

- [ ] **Step 5: Commit the UI retention change**

```powershell
git add RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidget.hpp RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidget.cpp RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderPlugin.cpp
git commit -m "feat: retain imported model metadata in builder UI"
```

### Task 6: Final verification and documentation

**Files:**
- Modify: `docs/RobotModelBuilder.md`

- [ ] **Step 1: Document semantic round-trip behavior**

Add a short “Imported WorkCells” section stating that RobotModelBuilder normalizes XML formatting while preserving supported model semantics and retained unsupported extensions; byte-for-byte formatting preservation is not promised.

- [ ] **Step 2: Run final verification**

Run the Task 5 build and CTest commands. Inspect the fixture output to confirm it contains the expected custom filenames and extension element.

- [ ] **Step 3: Commit documentation**

```powershell
git add docs/RobotModelBuilder.md docs/superpowers/specs/2026-07-25-robotmodelbuilder-semantic-roundtrip-design.md docs/superpowers/plans/2026-07-25-robotmodelbuilder-semantic-roundtrip.md
git commit -m "docs: describe robot model semantic round trip"
```

## Plan Self-Review

- Spec coverage: Tasks 2–5 cover provenance, supported geometry, extensions, imported output paths, UI retention, and tests; Task 6 documents the intended semantic—not byte—guarantee.
- TDD: each production task begins with a specific failing assertion and requires observing RED before implementation.
- Scope: no changes are proposed to RobWorkStudio’s WorkCell loading pipeline; plugin `open(WorkCell*)` remains the integration boundary.
