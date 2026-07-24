# Plugin Icons Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add visually consistent, grey three-dimensional icons to the RobotModelBuilder, KinematicAnalysis, and StructureOptimizer RobWorkStudio plugins.

**Architecture:** Each plugin owns a 64 px transparent PNG icon and registers it in its existing Qt resource collection. Its plugin constructor creates the icon from a plugin-specific `:/` resource path, making the icon available wherever RobWorkStudio displays the plugin.

**Tech Stack:** PNG raster assets, Qt Resource Collection (`.qrc`), Qt `QIcon`, CMake Qt resource generation.

---

### Task 1: Create the icon asset set

**Files:**
- Create: `RobWorkStudio/src/rwslibs/robotmodelbuilder/robotmodelbuilder_icon.png`
- Create: `RobWorkStudio/src/rwslibs/kinematicanalysis/kinematicanalysis_icon.png`
- Create: `RobWorkStudio/src/rwslibs/structureoptimizer/structureoptimizer_icon.png`

- [ ] **Step 1: Generate the RobotModelBuilder asset**

Create a 64 px square transparent PNG showing a grey, articulated three-link robot arm on a square base. Use soft upper-left highlights, dark lower-right shading, and a compact centered silhouette.

- [ ] **Step 2: Generate the KinematicAnalysis asset**

Create a 64 px square transparent PNG showing a grey articulated linkage over a subtle translucent quarter-sector workspace envelope. Keep the envelope large enough to read at 32 px and avoid labels or fine graph marks.

- [ ] **Step 3: Generate the StructureOptimizer asset**

Create a 64 px square transparent PNG showing a grey triangular truss block with a raised, integrated convergence arrow. Preserve open negative space between truss members so the object remains legible at 32 px.

- [ ] **Step 4: Inspect assets at full and half scale**

Open the three generated PNGs and confirm that they have transparent backgrounds, a neutral grey palette, and distinct silhouettes at 64 px and 32 px.

### Task 2: Register the Qt resources

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/resources.qrc`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/resources.qrc`
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/resources.qrc`

- [ ] **Step 1: Add the RobotModelBuilder resource entry**

Replace the empty resource block with:

```xml
<qresource prefix="/robotmodelbuilder">
    <file>robotmodelbuilder_icon.png</file>
</qresource>
```

- [ ] **Step 2: Add the KinematicAnalysis resource entry**

Replace the self-closing resource element with:

```xml
<qresource prefix="/kinematicanalysis">
    <file>kinematicanalysis_icon.png</file>
</qresource>
```

- [ ] **Step 3: Add the StructureOptimizer resource entry**

Replace the self-closing resource element with:

```xml
<qresource prefix="/structureoptimizer">
    <file>structureoptimizer_icon.png</file>
</qresource>
```

### Task 3: Load icons in plugin constructors

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderPlugin.cpp:21`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisPlugin.cpp:11`
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizerPlugin.cpp:9`

- [ ] **Step 1: Load the RobotModelBuilder icon**

Change the base constructor argument to:

```cpp
RobWorkStudioPlugin("RobotModelBuilder", QIcon(":/robotmodelbuilder/robotmodelbuilder_icon.png"))
```

- [ ] **Step 2: Load the KinematicAnalysis icon**

Change the base constructor argument to:

```cpp
RobWorkStudioPlugin("KinematicAnalysis", QIcon(":/kinematicanalysis/kinematicanalysis_icon.png"))
```

- [ ] **Step 3: Load the StructureOptimizer icon**

Change the base constructor argument to:

```cpp
RobWorkStudioPlugin("StructureOptimizer", QIcon(":/structureoptimizer/structureoptimizer_icon.png"))
```

### Task 4: Verify resource integration

**Files:**
- Verify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/resources.qrc`
- Verify: `RobWorkStudio/src/rwslibs/kinematicanalysis/resources.qrc`
- Verify: `RobWorkStudio/src/rwslibs/structureoptimizer/resources.qrc`

- [ ] **Step 1: Check QRC paths and constructors**

Run:

```powershell
rg -n 'robotmodelbuilder_icon|kinematicanalysis_icon|structureoptimizer_icon|QIcon\(' RobWorkStudio/src/rwslibs/robotmodelbuilder RobWorkStudio/src/rwslibs/kinematicanalysis RobWorkStudio/src/rwslibs/structureoptimizer
```

Expected: each PNG is declared once in its matching QRC file and loaded by the corresponding plugin constructor.

- [ ] **Step 2: Build the affected RobWorkStudio target**

Run:

```powershell
cmake --build build/Desktop_Qt_6_11_1_MSVC2022_64bit-Release --target RobWorkStudio
```

Expected: CMake's Qt resource compiler accepts all three QRC files and the plugin targets compile without resource-path errors.
