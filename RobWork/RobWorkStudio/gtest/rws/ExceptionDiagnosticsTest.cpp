#include <rwslibs/rwstudioapp/ExceptionDiagnostics.hpp>

#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>

TEST (ExceptionDiagnostics, FormatsStandardExceptionWithEventContext)
{
    const rws::ExceptionDiagnostic diagnostic = {
        "2026-08-04T12:34:56.789",
        "shutdown",
        "RobWorkStudio destructor",
        "std::runtime_error",
        "workcell loader failed",
        "RobotModelBuilder",
        "RobotModelBuilderPlugin",
        "1234",
        "C:/Users/test/.RobWorkStudio/logs/exception.log"};

    const std::string formatted = rws::formatExceptionDiagnostic (diagnostic);

    EXPECT_NE (std::string::npos, formatted.find ("std::runtime_error"));
    EXPECT_NE (std::string::npos, formatted.find ("workcell loader failed"));
    EXPECT_NE (std::string::npos, formatted.find ("RobotModelBuilder"));
    EXPECT_NE (std::string::npos, formatted.find ("RobotModelBuilderPlugin"));
    EXPECT_NE (std::string::npos, formatted.find ("thread=1234"));
    EXPECT_NE (std::string::npos, formatted.find ("exception.log"));
}

TEST (ExceptionDiagnostics, FormatsUnknownExceptionWithoutInventingCause)
{
    const rws::ExceptionDiagnostic diagnostic = {
        "2026-08-04T12:34:56.789",
        "event-loop",
        "QApplication::notify",
        "unknown C++ exception", "The exception type is unavailable", "QApplication", "", "7", ""};

    const std::string formatted = rws::formatExceptionDiagnostic (diagnostic);

    EXPECT_NE (std::string::npos, formatted.find ("unknown C++ exception"));
    EXPECT_NE (std::string::npos, formatted.find ("timestamp=2026-08-04T12:34:56.789"));
    EXPECT_NE (std::string::npos, formatted.find ("phase=event-loop"));
    EXPECT_NE (std::string::npos, formatted.find ("operation=QApplication::notify"));
    EXPECT_NE (std::string::npos, formatted.find ("The exception type is unavailable"));
    EXPECT_NE (std::string::npos, formatted.find ("receiver=QApplication"));
    EXPECT_NE (std::string::npos, formatted.find ("thread=7"));
    EXPECT_EQ (std::string::npos, formatted.find ("This is likely a bug"));
}

TEST (ExceptionDiagnostics, AppendsCompleteRecordToLogFile)
{
    const std::string logPath = "exception-diagnostics-test.log";
    std::remove (logPath.c_str ());
    const rws::ExceptionDiagnostic diagnostic = {
        "2026-08-04T12:34:56.789", "shutdown", "save settings", "std::logic_error", "bad state", "Receiver", "object", "9", logPath};

    ASSERT_TRUE (rws::appendExceptionDiagnostic (logPath, diagnostic));

    std::ifstream input (logPath.c_str ());
    ASSERT_TRUE (input.is_open ());
    const std::string content ((std::istreambuf_iterator< char > (input)),
                               std::istreambuf_iterator< char > ());
    EXPECT_NE (std::string::npos, content.find ("category=std::logic_error"));
    EXPECT_NE (std::string::npos, content.find ("message=bad state"));
    EXPECT_NE (std::string::npos, content.find ("---"));
    input.close ();
    std::remove (logPath.c_str ());
}
