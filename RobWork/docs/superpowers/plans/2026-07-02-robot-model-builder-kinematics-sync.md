# RobotModelBuilder Kinematics Sync Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the DH table and explicit joint-transform table stay immediately synchronized in both directions.

**Architecture:** Add widget-local conversion helpers between standard-DH rows and explicit zero-pose transforms, then connect both tables' edit signals through guarded sync slots. Verify the behavior with widget-level tests that drive the UI tables programmatically.

**Tech Stack:** C++/Qt Widgets, RobWorkStudio plugin code, CMake test target `sdurws_robotmodelbuilder_xmltest`.

---

### Task 1: Add Failing Widget Sync Tests

**Files:**
- Modify: `D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWorkStudio/src/rwslibs/robotmodelbuilder/CMakeLists.txt`
- Modify: `D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriterTest.cpp`

- [ ] Add `RobotModelBuilderWidget.cpp/.hpp` to the XML test target so the widget can be instantiated.
- [ ] Switch the test app to `QApplication`.
- [ ] Add a DH-to-transform sync test.
- [ ] Add a transform-to-DH projection test.
- [ ] Build and run the test target to confirm the new assertions fail first.

### Task 2: Implement Bidirectional Sync

**Files:**
- Modify: `D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidget.hpp`
- Modify: `D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidget.cpp`

- [ ] Add guarded sync slots and internal state to avoid recursive signal loops.
- [ ] Add conversion helpers `DH -> explicit transform` and `explicit transform -> normalized DH`.
- [ ] Wire `_dhTable` and `_transformTable` edit signals to the new sync slots.
- [ ] Tag the relevant widgets with stable `objectName`s for test discovery.
- [ ] Rebuild the test target and verify all tests pass.

### Task 3: Scope Check

**Files:**
- Modify: `D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidget.cpp`
- Modify: `D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriterTest.cpp`

- [ ] Review the diff to ensure only sync wiring, conversion logic, and tests changed.

