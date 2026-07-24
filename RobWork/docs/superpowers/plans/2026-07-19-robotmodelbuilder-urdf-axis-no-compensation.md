# RobotModelBuilder URDF Axis No-Compensation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Import URDF non-Z joint axes without adding `*_axis_compensation` frames, while preserving geometry/dynamics poses and keeping collision adjacency based on real robot joints.

**Architecture:** Keep RobWork's existing convention that Revolute/Prismatic joints move along the local Z axis. During URDF import, rotate each non-Z-axis movable joint frame so its local Z matches the URDF `<axis>`, then convert the child link's visual, collision, and inertial local data into that rotated joint frame instead of inserting a fixed compensation frame. Existing writer and UI code should then see only real URDF joints/frames.

**Tech Stack:** C++17-style code in `RobWorkStudio/src/rwslibs/robotmodelbuilder`, Qt XML/string/file APIs, existing command-line test executable `sdurws_robotmodelbuilder_xmltest`.

---

## File Structure

- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelUrdfImporter.cpp`
  - Remove the `*_axis_compensation` insertion path.
  - Add small local helpers for applying a rotation to URDF child-link poses and inertial data.
  - Keep helper functions inside the existing anonymous namespace.
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriterTest.cpp`
  - Replace the old non-Z-axis test that expected a compensation frame.
  - Add coverage for transformed visual/collision/inertial data.
  - Add coverage that collision adjacency does not include compensation frames and does include the real adjacent joint pair.
- No planned changes: `RobotModelSpec.hpp`, `RobotModelXmlWriter.cpp`, `RobotModelBuilderWidget.cpp`
  - The preferred fix avoids adding metadata fields and avoids teaching downstream systems about hidden helper frames.

---

### Task 1: Update the Existing Non-Z Axis URDF Test

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriterTest.cpp`

- [ ] **Step 1: Replace the old compensation-frame expectations**

Find the block beginning near the comment for the axis alignment URDF test. Replace the assertions after successful import with this logic:

```cpp
        if (result.warnings.join ("; ").contains ("does not re-orient non-Z axes"))
            return fail ("Non-Z axis joints should be re-oriented instead of warning.");
        if (result.spec.transformJoints.size () != 1)
            return fail ("Non-Z axis import should not add a compensation frame.");
        const JointTransformSpec& joint = result.spec.transformJoints[0];
        if (joint.name != "joint_y" || joint.type != "Revolute")
            return fail ("Axis-aligned movable joint should keep its URDF joint name/type.");
        if (QString::fromStdString (joint.name).contains ("axis_compensation"))
            return fail ("Imported transform joints must not expose axis compensation frames.");
        if (result.spec.drawables.empty () ||
            result.spec.drawables.front ().refFrame != "joint_y")
            return fail ("Child link visual geometry should attach to the real joint frame.");
        const std::array< double, 3 > jointAxis = rpyRotatedZ (joint.rpyDeg);
        if (!nearlyEqual (jointAxis[0], 0) || !nearlyEqual (jointAxis[1], 1) ||
            !nearlyEqual (jointAxis[2], 0))
            return fail ("Y-axis revolute joint should rotate local Z onto URDF Y.");
```

- [ ] **Step 2: Add visual pose assertions to the same test**

Update the URDF string in that same test so the child link visual has a non-zero local pose:

```cpp
            << "<link name=\"link1\"><visual><origin xyz=\"1 2 3\" rpy=\"0 0 0\" />"
            << "<geometry><box size=\"0.1 0.2 0.3\" /></geometry></visual></link>\n"
```

Then add these assertions after the joint-axis assertion:

```cpp
        const DrawableSpec& visual = result.spec.drawables.front ();
        if (!nearlyEqual (visual.pos[0], 1) || !nearlyEqual (visual.pos[1], -3) ||
            !nearlyEqual (visual.pos[2], 2))
            return fail ("Visual pose should be converted from child-link frame to reoriented joint frame.");
```

This expected value is for URDF axis `(0, 1, 0)` with the importer's deterministic alignment basis. It uses the transpose of the axis-alignment matrix because the pose is being re-expressed from the original child-link frame into the reoriented RobWork joint frame.

- [ ] **Step 3: Run the test and verify it fails before implementation**

Run from the repository root with the existing `build` directory:

```powershell
cmake --build build --target sdurws_robotmodelbuilder_xmltest
ctest --test-dir build -R sdurws_robotmodelbuilder_xmltest --output-on-failure
```

Expected failure before implementation:

```text
Non-Z axis import should not add a compensation frame.
```

or an equivalent failure from the new visual `refFrame`/pose assertions.

---

### Task 2: Add Collision and Inertial Coverage for Folded Axis Transforms

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriterTest.cpp`

- [ ] **Step 1: Extend the same URDF test with collision and inertial data**

Use this complete robot body for the non-Z-axis test so visual, collision, and inertial are all exercised:

```cpp
        out << "<robot name=\"axis_alignment_robot\">\n"
            << "<link name=\"base\" />\n"
            << "<link name=\"link1\">"
            << "<visual><origin xyz=\"1 2 3\" rpy=\"0 0 0\" />"
            << "<geometry><box size=\"0.1 0.2 0.3\" /></geometry></visual>"
            << "<collision><origin xyz=\"4 5 6\" rpy=\"0 0 0\" />"
            << "<geometry><box size=\"0.4 0.5 0.6\" /></geometry></collision>"
            << "<inertial><origin xyz=\"7 8 9\" rpy=\"0 0 0\" />"
            << "<mass value=\"2\" />"
            << "<inertia ixx=\"1\" ixy=\"0\" ixz=\"0\" iyy=\"2\" iyz=\"0\" izz=\"3\" />"
            << "</inertial>"
            << "</link>\n"
            << "<joint name=\"joint_y\" type=\"revolute\"><parent link=\"base\" />"
            << "<child link=\"link1\" /><origin xyz=\"0 0 0.2\" rpy=\"0 0 0\" />"
            << "<axis xyz=\"0 1 0\" />"
            << "<limit lower=\"-1\" upper=\"1\" velocity=\"1\" effort=\"1\" /></joint>\n"
            << "</robot>\n";
```

- [ ] **Step 2: Add collision model assertions**

After the visual assertions, add:

```cpp
        if (result.spec.collisionModels.empty () ||
            result.spec.collisionModels.front ().refFrame != "joint_y")
            return fail ("Child link collision geometry should attach to the real joint frame.");
        const CollisionModelSpec& collision = result.spec.collisionModels.front ();
        if (!nearlyEqual (collision.pos[0], 4) || !nearlyEqual (collision.pos[1], -6) ||
            !nearlyEqual (collision.pos[2], 5))
            return fail ("Collision pose should be converted from child-link frame to reoriented joint frame.");
```

- [ ] **Step 3: Add inertial assertions**

Add:

```cpp
        if (result.spec.dynamics.links.empty () ||
            result.spec.dynamics.links.front ().objectName != "joint_y")
            return fail ("URDF inertial data should remain attached to the real movable joint.");
        const LinkDynamicsSpec& dyn = result.spec.dynamics.links.front ();
        if (!nearlyEqual (dyn.cog[0], 7) || !nearlyEqual (dyn.cog[1], -9) ||
            !nearlyEqual (dyn.cog[2], 8))
            return fail ("Inertial COG should be converted from child-link frame to reoriented joint frame.");
        if (!nearlyEqual (dyn.inertia[0], 1) || !nearlyEqual (dyn.inertia[1], 3) ||
            !nearlyEqual (dyn.inertia[2], 2))
            return fail ("Inertial tensor diagonal should be rotated into the reoriented joint frame.");
```

For the `(0, 1, 0)` axis alignment, the old child-link inertia diagonal `(1, 2, 3)` becomes `(1, 3, 2)` in the joint frame.

- [ ] **Step 4: Run the focused test executable and verify failure**

```powershell
cmake --build build --target sdurws_robotmodelbuilder_xmltest
ctest --test-dir build -R sdurws_robotmodelbuilder_xmltest --output-on-failure
```

Expected before implementation: failures from collision/inertial pose or transform-joint count.

---

### Task 3: Remove Compensation Frames and Fold Child-Link Poses in the Importer

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelUrdfImporter.cpp`

- [ ] **Step 1: Add small rotation helpers near existing rotation helpers**

Place these in the anonymous namespace near `multiplyRotation`, `transposeRotation`, and `rotationToPluginRpyDeg`:

```cpp
std::array< double, 3 > multiplyRotationVector (const Rotation& rotation,
                                                const std::array< double, 3 >& value)
{
    return {{rotation[0] * value[0] + rotation[1] * value[1] + rotation[2] * value[2],
             rotation[3] * value[0] + rotation[4] * value[1] + rotation[5] * value[2],
             rotation[6] * value[0] + rotation[7] * value[1] + rotation[8] * value[2]}};
}

std::array< double, 6 > rotateInertiaTensor (const std::array< double, 6 >& inertia,
                                             const Rotation& rotation)
{
    const double in[9] = {
        inertia[0], inertia[3], inertia[4],
        inertia[3], inertia[1], inertia[5],
        inertia[4], inertia[5], inertia[2]};

    double tmp[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0};
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            for (int k = 0; k < 3; ++k)
                tmp[3 * r + c] += rotation[3 * r + k] * in[3 * k + c];
        }
    }

    double out[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0};
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            for (int k = 0; k < 3; ++k)
                out[3 * r + c] += tmp[3 * r + k] * rotation[3 * c + k];
        }
    }

    return {{out[0], out[4], out[8], out[1], out[2], out[5]}};
}

std::array< double, 3 > transformedOriginPos (const UrdfOrigin& origin,
                                              const Rotation& inverseAxisAlignment)
{
    return multiplyRotationVector (inverseAxisAlignment, origin.xyz);
}

std::array< double, 3 > transformedOriginRpyDeg (const UrdfOrigin& origin,
                                                 const Rotation& inverseAxisAlignment)
{
    const Rotation poseRotation =
        pluginRpyDegToRotation (urdfRpyToPluginRpyDeg (origin.rpyRad));
    return rotationToPluginRpyDeg (multiplyRotation (inverseAxisAlignment, poseRotation));
}
```

Important: `transformedOriginRpyDeg()` returns plugin-ready `rpyDeg`, not URDF `rpyRad`. Keep this distinction explicit so callers do not convert the order twice.

- [ ] **Step 2: Track each child link's inverse axis alignment**

In `importFile`, add a map next to the existing child-link maps:

```cpp
    std::map< QString, Rotation > childLinkToInverseAxisAlignment;
```

Inside the `for (const UrdfJoint& urdfJoint : orderedJoints)` loop, after computing `axisAlignment`, assign:

```cpp
        childLinkToInverseAxisAlignment[urdfJoint.childLink] =
            transposeRotation (axisAlignment);
```

Also look up the parent link's inverse axis alignment before filling the current `JointTransformSpec`, and apply it to the current joint origin. This preserves serial-chain kinematics when a non-Z-axis joint is followed by another joint:

```cpp
        const auto parentInvIt =
            childLinkToInverseAxisAlignment.find (urdfJoint.parentLink);
        const Rotation parentInverseAxisAlignment =
            parentInvIt != childLinkToInverseAxisAlignment.end () ? parentInvIt->second :
                                                                    identityRotation ();

        joint.pos = multiplyRotationVector (parentInverseAxisAlignment,
                                            urdfJoint.origin.xyz);
        const Rotation originRotation =
            pluginRpyDegToRotation (urdfRpyToPluginRpyDeg (urdfJoint.origin.rpyRad));
        joint.rpyDeg = rotationToPluginRpyDeg (
            multiplyRotation (multiplyRotation (parentInverseAxisAlignment, originRotation),
                              axisAlignment));
```

- [ ] **Step 3: Remove the compensation frame insertion block**

Delete the block that creates:

```cpp
        QString linkFrameNameForJoint = urdfJoint.name;
        if (reorientAxis) {
            JointTransformSpec compensation;
            const QString compensationName =
                axisCompensationFrameName (urdfJoint.name, usedTransformNames);
            compensation.name   = compensationName.toStdString ();
            compensation.type   = "FixedFrame";
            compensation.pos    = {{0, 0, 0}};
            compensation.rpyDeg = rotationToPluginRpyDeg (transposeRotation (axisAlignment));
            spec.transformJoints.push_back (compensation);
            usedTransformNames.insert (compensationName);
            linkFrameNameForJoint = compensationName;
        }
```

Replace the child link frame assignment with:

```cpp
        childLinkToFrameName[urdfJoint.childLink]         = urdfJoint.name;
        childLinkToDynamicsJointName[urdfJoint.childLink] = urdfJoint.name;
        childLinkToJointType[urdfJoint.childLink]         = pluginJointType;
```

After this change `axisCompensationFrameName()` should become unused. Remove that helper to avoid keeping dead code.

- [ ] **Step 4: Convert visual poses before writing `DrawableSpec`**

In the link visual loop, before constructing `DrawableSpec`, get the inverse rotation:

```cpp
            const Rotation inverseAxisAlignment =
                childLinkToInverseAxisAlignment.count (item.first) > 0
                    ? childLinkToInverseAxisAlignment[item.first]
                    : identityRotation ();
```

Then replace:

```cpp
            drawable.rpyDeg          = urdfRpyToPluginRpyDeg (visual.origin.rpyRad);
            drawable.pos             = visual.origin.xyz;
```

with:

```cpp
            drawable.rpyDeg = transformedOriginRpyDeg (visual.origin, inverseAxisAlignment);
            drawable.pos    = transformedOriginPos (visual.origin, inverseAxisAlignment);
```

- [ ] **Step 5: Convert collision poses before writing `CollisionModelSpec`**

In the collision loop, use the same inverse alignment lookup and rotation conversion. Replace:

```cpp
            collision.rpyDeg      =
                urdfRpyToPluginRpyDeg (collisionGeometry.origin.rpyRad);
            collision.pos         = collisionGeometry.origin.xyz;
```

with:

```cpp
            const Rotation inverseAxisAlignment =
                childLinkToInverseAxisAlignment.count (item.first) > 0
                    ? childLinkToInverseAxisAlignment[item.first]
                    : identityRotation ();
            collision.rpyDeg = transformedOriginRpyDeg (collisionGeometry.origin,
                                                        inverseAxisAlignment);
            collision.pos    = transformedOriginPos (collisionGeometry.origin,
                                                     inverseAxisAlignment);
```

- [ ] **Step 6: Convert inertial COG and inertia tensor**

In the inertial loop, before filling `LinkDynamicsSpec`, look up the inverse alignment:

```cpp
        const Rotation inverseAxisAlignment =
            childLinkToInverseAxisAlignment.count (item.first) > 0
                ? childLinkToInverseAxisAlignment[item.first]
                : identityRotation ();
```

Replace:

```cpp
        dyn.cog             = link.inertial.origin.xyz;
        dyn.inertia         = link.inertial.inertia;
```

with:

```cpp
        dyn.cog             = transformedOriginPos (link.inertial.origin,
                                                    inverseAxisAlignment);
        dyn.inertia         = rotateInertiaTensor (link.inertial.inertia,
                                                   inverseAxisAlignment);
```

- [ ] **Step 7: Run tests**

```powershell
cmake --build build --target sdurws_robotmodelbuilder_xmltest
ctest --test-dir build -R sdurws_robotmodelbuilder_xmltest --output-on-failure
```

Expected: the test executable passes.

- [ ] **Step 8: Commit**

```powershell
git add RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelUrdfImporter.cpp RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriterTest.cpp
git commit -m "fix: fold URDF joint axis transforms into child poses"
```

---

### Task 4: Add a Serial-Chain Collision Adjacency Regression Test

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriterTest.cpp`

- [ ] **Step 1: Add a two-joint URDF test**

Add a new block after the axis alignment test. It must import a two-joint serial chain where the first joint has axis `(0, 1, 0)` and the second joint has default axis `(0, 0, 1)`:

```cpp
    {
        const QString dir = QDir::tempPath () + "/robotmodelbuilder_urdf_axis_adjacency";
        QDir ().mkpath (dir);
        const QString urdfPath = dir + "/axis_adjacency.urdf";
        QFile file (urdfPath);
        if (!file.open (QFile::WriteOnly | QFile::Text))
            return fail ("Could not create axis adjacency URDF test file.");
        QTextStream out (&file);
        out << "<robot name=\"axis_adjacency_robot\">\n"
            << "<link name=\"base\" />\n"
            << "<link name=\"link1\"><collision><geometry><box size=\"0.1 0.1 0.1\" /></geometry></collision></link>\n"
            << "<link name=\"link2\"><collision><geometry><box size=\"0.1 0.1 0.1\" /></geometry></collision></link>\n"
            << "<joint name=\"joint_y\" type=\"revolute\"><parent link=\"base\" />"
            << "<child link=\"link1\" /><origin xyz=\"0 0 0.2\" rpy=\"0 0 0\" />"
            << "<axis xyz=\"0 1 0\" />"
            << "<limit lower=\"-1\" upper=\"1\" velocity=\"1\" effort=\"1\" /></joint>\n"
            << "<joint name=\"joint_z\" type=\"revolute\"><parent link=\"link1\" />"
            << "<child link=\"link2\" /><origin xyz=\"0 0.2 0.3\" rpy=\"0 0 0\" />"
            << "<axis xyz=\"0 0 1\" />"
            << "<limit lower=\"-1\" upper=\"1\" velocity=\"1\" effort=\"1\" /></joint>\n"
            << "</robot>\n";
        file.close ();

        UrdfImportOptions options;
        options.saveDirectory = dir;
        UrdfImportResult result;
        QStringList importErrors;
        if (!RobotModelUrdfImporter::importFile (urdfPath, options, result, importErrors))
            return fail ("Axis adjacency URDF import failed: " + importErrors.join ("; "));

        if (result.spec.transformJoints.size () != 2)
            return fail ("Axis adjacency import should contain exactly the two real URDF joints.");
        if (result.spec.transformJoints[0].name != "joint_y" ||
            result.spec.transformJoints[1].name != "joint_z")
            return fail ("Axis adjacency import should preserve real serial joint order.");
        const JointTransformSpec& childJoint = result.spec.transformJoints[1];
        if (!nearlyEqual (childJoint.pos[0], 0) || !nearlyEqual (childJoint.pos[1], -0.3) ||
            !nearlyEqual (childJoint.pos[2], 0.2))
            return fail ("Child joint origin should be converted into the reoriented parent link frame.");
        if (!nearlyEqual (childJoint.rpyDeg[0], 0) ||
            !nearlyEqual (childJoint.rpyDeg[1], 0) ||
            !nearlyEqual (childJoint.rpyDeg[2], 90))
            return fail ("Child joint orientation should cancel the parent link axis alignment.");

        const QString collisionXml = RobotModelXmlWriter::makeCollisionSetupXml (result.spec);
        if (!contains (collisionXml, "<FramePair first=\"joint_y\" second=\"joint_z\"/>"))
            return fail ("Automatic adjacent collision exclusion should use real neighboring joints.");
        if (collisionXml.contains ("axis_compensation"))
            return fail ("Collision setup must not contain axis compensation frames.");
    }
```

- [ ] **Step 2: Run the test**

```powershell
cmake --build build --target sdurws_robotmodelbuilder_xmltest
ctest --test-dir build -R sdurws_robotmodelbuilder_xmltest --output-on-failure
```

Expected: pass.

- [ ] **Step 3: Commit**

```powershell
git add RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriterTest.cpp
git commit -m "test: cover URDF axis collision adjacency"
```

---

### Task 5: Clean Comments and Documentation

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelUrdfImporter.cpp`
- Modify: `docs/RobotModelBuilder.md`

- [ ] **Step 1: Update stale importer comments**

In `RobotModelUrdfImporter.cpp`, replace the stale comment around `isDefaultJointAxis()`:

```cpp
/// URDF default joint axis is (0,0,1). Non-default movable axes are supported
/// by rotating the RobWork joint frame so its local Z axis matches the URDF
/// axis, then folding the inverse rotation into child-link visual/collision/
/// inertial data.
```

Also update the comment above the visual/collision import loop to say geometry is attached to the real imported joint frame, with pose converted when the joint axis was reoriented.

- [ ] **Step 2: Update URDF import docs**

In `docs/RobotModelBuilder.md`, find the URDF support bullet that says non-default axes warn or are not re-oriented. Replace it with:

```markdown
- `<axis xyz="...">`: URDF 默认轴 `(0,0,1)` 直接导入；非默认轴会通过旋转 RobWork joint frame 对齐到本地 Z 轴来表示，不再生成 `*_axis_compensation` 辅助帧。导入器会把 child link 的 visual / collision / inertial 局部位姿转换到新的 joint frame 下，因此可动关节数量和 URDF 保持一致，自动相邻碰撞排除也只基于真实关节。
```

- [ ] **Step 3: Run the test executable after docs/comment cleanup**

```powershell
cmake --build build --target sdurws_robotmodelbuilder_xmltest
ctest --test-dir build -R sdurws_robotmodelbuilder_xmltest --output-on-failure
```

Expected: pass.

- [ ] **Step 4: Commit**

```powershell
git add RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelUrdfImporter.cpp docs/RobotModelBuilder.md
git commit -m "docs: describe URDF non-Z axis import"
```

---

### Task 6: Final Verification

**Files:**
- No additional modifications expected.

- [ ] **Step 1: Search for leaked compensation behavior**

```powershell
rg -n "axis_compensation|does not re-orient non-Z axes|compensation frame|compensation" RobWorkStudio/src/rwslibs/robotmodelbuilder docs/RobotModelBuilder.md
```

Expected: `axis_compensation` is absent from importer generation logic and generated XML. It may still appear in negative test assertions, this plan, and user-facing docs that explain the old behavior is no longer generated.

- [ ] **Step 2: Run the robotmodelbuilder test**

```powershell
cmake --build build --target sdurws_robotmodelbuilder_xmltest
ctest --test-dir build -R sdurws_robotmodelbuilder_xmltest --output-on-failure
```

Expected: pass.

- [ ] **Step 3: Optional full build smoke test**

```powershell
cmake --build build
```

Expected: build completes. If this checkout's full build is too expensive or has unrelated failures, record the exact failure and keep the focused `sdurws_robotmodelbuilder_xmltest` result as the required verification for this change.

- [ ] **Step 4: Final commit if Task 6 required any changes**

Only commit if Task 6 caused edits:

```powershell
git add RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelUrdfImporter.cpp RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriterTest.cpp docs/RobotModelBuilder.md
git commit -m "fix: remove URDF axis compensation frame leakage"
```

---

## Notes for Implementers

- Do not add a new `RobotModelSpec` metadata field unless the no-compensation approach proves impossible. The design goal is to avoid exposing helper frames to downstream code.
- Do not special-case names ending in `_axis_compensation` in collision setup generation. That would hide the symptom while leaving the extra frame in the model.
- Preserve existing behavior for default-axis URDF joints: no pose conversion beyond the existing URDF RPY conversion.
- Preserve movable joint counting: only `Revolute` and `Prismatic` rows count toward Q, limits, and force limits.
- Be careful with coordinate order:
  - URDF `origin rpy` input is roll/pitch/yaw in radians.
  - The plugin stores `rpyDeg` in its existing RobWork Z/Y/X order.
  - `Rotation` helpers in `RobotModelUrdfImporter.cpp` should remain the single place where these conversions happen.

## Self-Review

- Spec coverage: the plan removes compensation frames, folds child-link poses into real joint frames, preserves non-Z joint axes, and verifies collision adjacency against real joints.
- Placeholder scan: no incomplete placeholders remain.
- Type consistency: all referenced types already exist in `RobotModelSpec.hpp` or `RobotModelUrdfImporter.cpp`; new helpers are local to `RobotModelUrdfImporter.cpp`.
