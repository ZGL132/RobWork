#ifndef RWS_EXCEPTIONDIAGNOSTICS_HPP_
#define RWS_EXCEPTIONDIAGNOSTICS_HPP_

#include <string>

namespace rws {

// 一条完整的事件循环异常诊断记录。每个字段用于定位异常发生的上下文：
// phase(操作阶段，如 close/workcell 切换)、receiver 事件接收者类型、objectName 接收者
// 对象名、threadId 线程号、logPath 全局日志路径等。
struct ExceptionDiagnostic
{
    std::string timestamp;    // 异常发生时间（ISO-8601）。
    std::string phase;        // 所处操作阶段（用于区分关闭/加载/切换等）。
    std::string operation;    // 正在执行的操作描述。
    std::string category;     // 异常类别（如 std::exception / unknown）。
    std::string message;      // 异常消息（std::exception 的 what()）。
    std::string receiver;     // 事件接收者的类名。
    std::string objectName;   // 事件接收者的对象名（objectName）。
    std::string threadId;     // 抛出异常的线程标识。
    std::string logPath;      // 全局日志文件路径（便于结合日志排查）。
};

// 把诊断结构格式化为多行 key=value 文本，供写入异常日志与弹窗展示。
std::string formatExceptionDiagnostic (const ExceptionDiagnostic& diagnostic);

// 以追加方式把一条诊断写入日志文件；写入失败返回 false。
bool appendExceptionDiagnostic (const std::string& logPath,
                                const ExceptionDiagnostic& diagnostic);

}    // namespace rws

#endif    // RWS_EXCEPTIONDIAGNOSTICS_HPP_
