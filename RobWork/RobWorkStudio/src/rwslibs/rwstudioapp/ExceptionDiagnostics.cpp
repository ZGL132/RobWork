#include "ExceptionDiagnostics.hpp"

#include <fstream>

namespace rws {
namespace {

// 空字段回退为 <unknown>，保证日志行仍可解析、缺字段不导致空输出。
std::string valueOrUnknown (const std::string& value)
{
    return value.empty () ? std::string ("<unknown>") : value;
}

}    // namespace

// 把诊断结构格式化为多行 key=value 文本，便于人读与后续 grep 定位。
std::string formatExceptionDiagnostic (const ExceptionDiagnostic& diagnostic)
{
    std::string result;
    result += "timestamp=" + valueOrUnknown (diagnostic.timestamp) + "\n";
    result += "phase=" + valueOrUnknown (diagnostic.phase) + "\n";
    result += "operation=" + valueOrUnknown (diagnostic.operation) + "\n";
    result += "category=" + valueOrUnknown (diagnostic.category) + "\n";
    result += "message=" + valueOrUnknown (diagnostic.message) + "\n";
    result += "receiver=" + valueOrUnknown (diagnostic.receiver) + "\n";
    result += "object=" + valueOrUnknown (diagnostic.objectName) + "\n";
    result += "thread=" + valueOrUnknown (diagnostic.threadId) + "\n";
    result += "log=" + valueOrUnknown (diagnostic.logPath) + "\n";
    return result;
}

// 以追加模式把一条诊断写入日志，记录以 "---" 分隔；写入失败返回 false。
bool appendExceptionDiagnostic (const std::string& logPath,
                                const ExceptionDiagnostic& diagnostic)
{
    if (logPath.empty ())
        return false;

    std::ofstream output (logPath.c_str (), std::ios::out | std::ios::app);
    if (!output.is_open ())
        return false;

    output << formatExceptionDiagnostic (diagnostic) << "---\n";
    output.flush ();
    return output.good ();
}

}    // namespace rws
