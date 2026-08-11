# Exception Diagnostics Design

RobWorkStudio now records every exception caught at the Qt event boundary in a per-session log under the Qt local application-data directory. Each record contains the timestamp, lifecycle phase, operation, exception category, `what()` text, receiver class/object name, thread id, and the exact log path. The dialog remains short but exposes the full record through Qt's expandable details section. Startup, event-loop, and shutdown are tracked separately so an exception raised while closing cannot be mislabeled as a startup failure.

For development, `--developer` remains the supported launch option and is enabled by default in `QT_DEBUG` builds. In this mode `QApplication::notify` does not catch exceptions, so Qt Creator can stop at the original throw site. In Qt Creator, add a `C++ Exception Breakpoint` and select `Thrown`/`Break on throw`; use a Debug kit with symbols. This is the only reliable way to locate a non-standard exception's throw line after stack unwinding would otherwise erase it.

The formatter and append operation are isolated in `ExceptionDiagnostics` so their output is regression-tested without starting the full GUI. Unknown exceptions are reported explicitly as unknown; the old fixed “This is likely a bug.” message is removed.
