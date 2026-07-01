# RobotModelBuilder Plugin Design

## Goal

Build a new independent RobWorkStudio plugin named `RobotModelBuilder`.
The plugin provides a graphical form-based workflow for creating an industrial
six-axis serial robot model and automatically generating RobWork XML files.

The first version focuses on reliable forward generation from form data to XML.
It does not replace or modify the existing WorkCell Editor plugin.

## Scope

The first version supports:

- One industrial six-axis serial robot.
- Two kinematic input modes:
  - DH parameters using RobWork `DHJoint` with `type="schilling"`.
  - Explicit `Joint` definitions with `RPY` and `Pos`.
- Built-in primitive cylindrical drawables for joint axes and links.
- Position, velocity, and acceleration limits.
- Preset robot poses, including editable default `Zero` and `Home` poses.
- XML preview before saving.
- Saving both a reusable serial device file and a directly loadable scene file.
- Optional loading of the generated scene into RobWorkStudio.

The first version does not support:

- Reverse parsing existing XML back into the form.
- CAD/STL import for visual geometry.
- Arbitrary joint count in the UI.
- Advanced collision model settings.
- Live 3D preview while typing.

## Repository Integration

The plugin will be added as an internal RobWorkStudio plugin:

- Directory:
  `RobWorkStudio/src/rwslibs/robotmodelbuilder`
- CMake target:
  `sdurws_robotmodelbuilder`
- Plugin display name:
  `RobotModelBuilder`
- CMake integration:
  add `add_subdirectory(robotmodelbuilder)` in
  `RobWorkStudio/src/rwslibs/CMakeLists.txt`.

The plugin follows the existing `TreeView`, `Jog`, and `WorkcellEditorPlugin`
patterns:

- Use `rws_add_plugin`.
- Link against `sdurws`, required RobWork libraries, and Qt libraries.
- Provide `plugin.json`, `resources.qrc`, and plugin metadata.
- Register default plugin loading details with `rws_plugin_load_details`.

## Architecture

The implementation is split into three layers.

### RobotModelBuilderPlugin

This is the RobWorkStudio plugin entry point. It owns the main widget, registers
the plugin with RobWorkStudio, and handles integration actions such as loading
the generated scene file.

Responsibilities:

- Construct and install the main widget.
- Provide the RobWorkStudio plugin metadata.
- Call RobWorkStudio APIs to load a generated workcell scene.
- Keep RobWorkStudio integration separate from form and XML generation logic.

### RobotModelBuilderWidget

This is the Qt Widgets user interface.

Responsibilities:

- Present form fields and editable tables.
- Switch between DH and `Joint + RPY + Pos` input modes.
- Collect user input into a structured model specification.
- Show validation messages.
- Show generated XML previews.
- Trigger save and save-and-load actions.

### RobotModelXmlWriter and Data Structures

This layer is independent from RobWorkStudio UI code.

Responsibilities:

- Store robot modeling data in explicit C++ structs.
- Validate the model before generation.
- Generate the serial device XML.
- Generate the scene XML.
- Convert pose values from degrees to radians for `<Q>` output.

Keeping this layer independent makes it easier to test and extend.

## User Interface

The main plugin panel uses Qt Widgets and is organized as a practical tool panel.

### Top Section

Fields and controls:

- Robot name, default `GenericSixAxis`.
- Save directory.
- Modeling mode selector:
  - `DH Parameters`
  - `Joint + RPY + Pos`
- Generation options:
  - Show frame axes.
  - Generate default drawables.
  - Generate scene file.
  - Load scene after saving.

### Main Tabs

The central UI uses a `QTabWidget`.

#### Kinematics

For DH mode, show a six-row table:

- `Joint`
- `alpha(deg)`
- `a(m)`
- `d(m)`
- `offset(deg)`

For `Joint + RPY + Pos` mode, show a six-row table:

- `Joint`
- `type`
- `r(deg)`
- `p(deg)`
- `y(deg)`
- `x(m)`
- `y(m)`
- `z(m)`

The first version defaults all joints to `Revolute`.

#### Drawables

The first version generates and edits primitive cylinder drawables.

Columns:

- `Name`
- `RefFrame`
- `Shape`
- `Radius`
- `Length`
- `RPY`
- `Pos`
- `RGB`
- `CollisionModel`

Default drawables include one joint-axis cylinder per joint and link cylinders
between adjacent joints.

The first version uses `Cylinder`; the data model can later expand to `Box`,
`Sphere`, and `Cone`.

#### Limits

Use one combined six-row table:

- `Joint`
- `PosMin(deg)`
- `PosMax(deg)`
- `VelMax(deg/s)`
- `AccMax(deg/s^2)`

#### Poses

Use an editable pose table:

- `Name`
- `q1(deg)`
- `q2(deg)`
- `q3(deg)`
- `q4(deg)`
- `q5(deg)`
- `q6(deg)`

Default poses:

- `Zero`: `0 0 0 0 0 0`
- `Home`: editable, default `0 -90 90 0 0 0`

The UI stores pose angles in degrees. The XML writer converts them to radians
when generating `<Q>` elements.

#### XML Preview

Show read-only previews for:

- Serial device XML.
- Scene XML.

The preview is refreshed by the `Generate Preview` action.

### Bottom Actions

Buttons:

- `Generate Preview`
- `Save XML`
- `Save and Load`
- `Reset to Default Six Axis`

## XML Generation

The data flow is:

`Qt form input -> RobotModelSpec -> validation -> RobotModelXmlWriter -> XML text -> files -> optional RobWorkStudio load`

### Serial Device File

The serial device file is named:

`RobotName.wc.xml`

It has this root element:

```xml
<SerialDevice name="RobotName">
  ...
</SerialDevice>
```

### Scene File

The scene file is named:

`RobotNameScene.wc.xml`

It has this root element:

```xml
<WorkCell name="RobotNameScene">
  <Frame name="RobotBase" refframe="WORLD">
    <RPY>0 0 0</RPY>
    <Pos>0 0 0</Pos>
    <Property name="ShowFrameAxis">true</Property>
  </Frame>

  <Include file="RobotName.wc.xml" />
</WorkCell>
```

The serial device file is reusable. The scene file is intended for direct
opening in RobWorkStudio.

### DH Mode

Each joint row generates:

```xml
<DHJoint name="Joint1" alpha="0" a="0" d="0.35" offset="0" type="schilling"/>
```

Units:

- `alpha` and `offset`: degrees in the UI and XML.
- `a` and `d`: meters.

### Joint + RPY + Pos Mode

Each joint row generates:

```xml
<Joint name="Joint1" type="Revolute">
  <RPY>0 0 0</RPY>
  <Pos>0 0 0.35</Pos>
  <Property name="ShowFrameAxis">true</Property>
</Joint>
```

Units:

- `RPY`: degrees.
- `Pos`: meters.

### Drawables

Drawable rows generate primitive geometry such as:

```xml
<Drawable name="Link2To3" refframe="Joint2" colmodel="Enabled">
  <RPY>0 90 0</RPY>
  <Pos>0.26 0 0</Pos>
  <RGB>0.6 0.6 0.6</RGB>
  <Cylinder radius="0.05" z="0.52" />
</Drawable>
```

### Limits

Limit rows generate:

```xml
<PosLimit refjoint="Joint1" min="-180" max="180" />
<VelLimit refjoint="Joint1" max="120" />
<AccLimit refjoint="Joint1" max="360" />
```

Units follow RobWork XML conventions used by the existing examples.

### Poses

Pose rows generate:

```xml
<Q name="Zero">0 0 0 0 0 0</Q>
```

The UI accepts degrees. The XML writer converts each value to radians.

## Validation

Validation runs before preview generation, saving, and loading.

Rules:

- Robot name must not be empty and must be safe for file names.
- The first version always contains exactly six joints.
- Joint names must not be empty or duplicated.
- DH values must be numeric.
- `RPY` and `Pos` values must be numeric.
- Drawable names and reference frames must not be empty.
- Drawable radius and length must be greater than zero.
- Drawable RGB values must be in the range `0..1`.
- Position minimum must be less than position maximum.
- Velocity and acceleration limits must be greater than zero.
- Pose names must not be empty.
- Each pose must contain exactly six numeric values.
- Save directory must exist.

If validation fails, the plugin prevents saving/loading and shows an explicit
message.

## Save and Load Behavior

`Generate Preview`:

- Collect the form data.
- Validate it.
- Generate serial device XML and scene XML.
- Display both previews without writing files.

`Save XML`:

- Generate and validate XML.
- Write `RobotName.wc.xml`.
- Write `RobotNameScene.wc.xml`.
- Report success or file I/O errors.

`Save and Load`:

- Run the same save process.
- Attempt to load `RobotNameScene.wc.xml` through RobWorkStudio.
- If loading fails, keep the generated files and show an error.

## Testing Plan

Build and integration checks:

- Configure RobWorkStudio with Qt Creator.
- Build `sdurws_robotmodelbuilder`.
- Build and run `RobWorkStudio`.
- Confirm the `RobotModelBuilder` plugin loads.

Functional checks:

- Reset to the default six-axis model.
- Generate preview.
- Confirm XML includes:
  - `SerialDevice`
  - six joints
  - cylindrical drawables
  - `PosLimit`, `VelLimit`, and `AccLimit`
  - `Zero` and `Home` poses
- Save XML and confirm both expected files exist.
- Use `Save and Load` and confirm RobWorkStudio displays the generated model.
- Verify both DH mode and `Joint + RPY + Pos` mode can generate and load.

Validation checks:

- Duplicate a joint name and confirm saving is blocked.
- Set a drawable radius to zero and confirm saving is blocked.
- Set invalid limits and confirm saving is blocked.
- Remove a pose value and confirm saving is blocked.
- Use a missing save directory and confirm saving is blocked.

## Success Criteria

The first version is successful when a user can open RobWorkStudio, open the
`RobotModelBuilder` plugin, enter or adjust six-axis robot parameters, preview
the generated XML, save both XML files, and load the generated scene so the
robot appears in the 3D view without manually writing XML.
