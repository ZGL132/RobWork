# Exception Diagnostics Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the unhelpful “This is likely a bug.” dialog with persistent, actionable exception diagnostics while keeping Qt Creator first-chance debugging available.

**Architecture:** Keep exception handling at the existing `QApplication::notify` boundary. Extract deterministic diagnostic formatting into a small helper that can be unit-tested, write the full record to a per-session log, and show only a concise message plus the log path in the modal dialog. Preserve `--developer` so Qt Creator can break on the original throw site.

**Tech Stack:** C++11, Qt, existing RobWorkStudio GoogleTest target, CMake.

---

### Task 1: Add the failing diagnostic-format test

**Files:**
- Create: `RobWorkStudio/gtest/rws/ExceptionDiagnosticsTest.cpp`
- Modify: `RobWorkStudio/gtest/CMakeLists.txt`

- [ ] Add tests for standard and unknown exception records, asserting that the formatted output contains category, message, event receiver, and log path.
- [ ] Add the test source to `sdurws_sdurws-gtest`.
- [ ] Build/run the focused test and confirm it fails because the helper does not yet exist.

### Task 2: Implement the diagnostic helper and runtime logging

**Files:**
- Create: `RobWorkStudio/src/rwslibs/rwstudioapp/ExceptionDiagnostics.hpp`
- Create: `RobWorkStudio/src/rwslibs/rwstudioapp/ExceptionDiagnostics.cpp`
- Modify: `RobWorkStudio/src/rwslibs/rwstudioapp/CMakeLists.txt`
- Modify: `RobWorkStudio/src/rwslibs/rwstudioapp/RobWorkStudioApp.cpp`

- [ ] Add a testable formatter and a session log writer.
- [ ] Log timestamp, exception category, `what()`, receiver class/object, thread id, and log path.
- [ ] For unknown exceptions, preserve the `catch (...)` fallback but record that the type is unavailable instead of discarding the event.
- [ ] Make the modal include the concise error and exact log file path.

### Task 3: Document Qt Creator first-chance debugging

**Files:**
- Create: `docs/superpowers/specs/2026-08-04-exception-diagnostics-design.md`
- Modify: `RobWorkStudio/src/rwslibs/rwstudioapp/RobWorkStudioApp.cpp`

- [ ] Keep `--developer` as the supported Qt Creator launch argument.
- [ ] Document adding a C++ Exception Breakpoint with “Thrown”; include GDB, LLDB, and CDB console equivalents only as reference.

### Task 4: Verify

- [ ] Rebuild the affected targets.
- [ ] Run the RWS GoogleTest executable and focused exception tests.
- [ ] Inspect `git diff` and confirm unrelated user changes remain untouched.
