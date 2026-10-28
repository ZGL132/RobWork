/** @file OverlappedReadProbe.cpp
 * @brief EXR-08：真实挂起管道读，观察取消→等待完成→关闭事件的严格生命周期。
 * 旧实现若跳过等待，测试记录失败并代为收敛 IRP，避免故意制造释放后写。
 */
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <gtest/gtest.h>
#include <iostream>

namespace io_probe {
HANDLE pendingHandle = nullptr;
OVERLAPPED* pending = nullptr;
int waits = 0;
int prematureCloses = 0;
}
/** @brief 调用实际 CancelIoEx 并记住其借持存储；不伪造完成。 */
BOOL WINAPI auditCancelIoEx(HANDLE handle, LPOVERLAPPED overlapped)
{
    io_probe::pendingHandle = handle;
    io_probe::pending = overlapped;
    return ::CancelIoEx(handle, overlapped);
}
/** @brief 观察真实完成等待；返回后内核不再借持本次 OVERLAPPED。 */
BOOL WINAPI auditGetOverlappedResult(HANDLE handle, LPOVERLAPPED overlapped,
                                    LPDWORD transferred, BOOL wait)
{
    const BOOL result = ::GetOverlappedResult(handle, overlapped, transferred, wait);
    if (io_probe::pending == overlapped && wait) {
        ++io_probe::waits;
        io_probe::pending = nullptr;
    }
    return result;
}
/** @brief 记录提前关闭；仅负例由探针补等，保护本轮测试进程内存。 */
BOOL WINAPI auditCloseHandle(HANDLE handle)
{
    if (io_probe::pending != nullptr && handle == io_probe::pending->hEvent) {
        ++io_probe::prematureCloses;
        DWORD transferred = 0;
        ::GetOverlappedResult(io_probe::pendingHandle, io_probe::pending, &transferred, TRUE);
        io_probe::pending = nullptr;
    }
    return ::CloseHandle(handle);
}
#define ChannelPair AuditChannelPair
#define CancelIoEx auditCancelIoEx
#define GetOverlappedResult auditGetOverlappedResult
#define CloseHandle auditCloseHandle
#ifdef EXR_BASELINE
#include "baseline/ChannelPair.cpp"
#else
#include "../../RobWork/RobWorkStudio/src/rwslibs/industrialrobot/execution/src/win32/ChannelPair.cpp"
#endif
#undef CloseHandle
#undef GetOverlappedResult
#undef CancelIoEx
#undef ChannelPair

/** @brief EXR-08：对端保持开启，析构必须主动取消并收敛真实挂起读。 */
TEST(OverlappedLifecycleProbe, PendingReadCompletesBeforeStorageDies_EXR08Probe)
{
    auto channel = sdurws::ird::execution::win32::AuditChannelPair::create(
        L"ird-exr08-probe-" + std::to_wstring(::GetCurrentProcessId()));
    ASSERT_NE(channel, nullptr);
    ASSERT_TRUE(channel->beginRead());
    EXPECT_EQ(channel->waitFor(nullptr, 0),
              sdurws::ird::execution::win32::AuditChannelPair::WaitResult::Timeout);
    channel.reset();
    std::cout << "completion waits=" << io_probe::waits
              << " premature event closes=" << io_probe::prematureCloses << '\n';
    EXPECT_EQ(io_probe::waits, 1);
    EXPECT_EQ(io_probe::prematureCloses, 0);
}
