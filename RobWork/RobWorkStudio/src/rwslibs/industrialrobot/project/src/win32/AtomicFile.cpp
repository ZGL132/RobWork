/**
 * @file   AtomicFile.cpp
 * @brief  存储原语（AtomicFile）实现——Win32FileOps 薄封装＋门面编排。
 *
 * 设计依据：
 *   - units/project.md §7.1（七步协议第 2/4/5 步中本原语承担的动作）、
 *     §7.2（Windows 文件操作保证——行 1 暂存写持久化、行 2 正式发布、
 *     行 3 HEAD 原子替换、行 6 不使用 ReplaceFileW）；
 *   - 需求 NFR-REL-01（单文件"暂存＋原子替换"原语层）；
 *   - 任务契约 PRJ-T02.json acceptance 1/3（publishNew/原子替换/
 *     write-through 语义；IFileOps 接缝）。
 *
 * 不超诺口径（§7.2，实现与文档一致）：本文件只用 MoveFileExW 的文档
 * 声明语义（含 MOVEFILE_WRITE_THROUGH 的"返回前刷盘"），不用
 * ReplaceFileW（保留目标 ACL/属性语义——HEAD/草稿无需保留属性，少一个
 * 依赖面，§7.2 行 6）；不对 NTFS rename 的崩溃原子性作超文档宣称。
 */

#include "AtomicFile.hpp"

#include <stdexcept>

namespace sdurws::ird::project::win32 {

namespace {

/**
 * @brief 把"成功＋句柄"包装为 FileResult（open 步骤专用）。
 *
 * CreateFileW 失败返回 INVALID_HANDLE_VALUE——把它与"成功"统一成
 * FileResult 形态，调用方不直接接触 Win32 哨兵值。
 *
 * @param raw    [in] CreateFileW 的返回值
 * @param handle [out] 成功时写入原始句柄；失败时写 nullptr（防调用方
 *               误用残留栈值）
 * @return ok＝raw 是否为有效句柄；失败时 osError＝GetLastError()
 */
FileResult handleToResult(HANDLE raw, HANDLE* handle)
{
    if (raw == INVALID_HANDLE_VALUE) {
        // 失败路径：句柄出参归零——调用方（含 RAII 守卫）无须再判断
        // 哨兵值，"handle 仅在 ok==true 时有效"的契约得以成立。
        *handle = nullptr;
        FileResult r;
        r.ok = false;
        r.osError = ::GetLastError();
        return r;
    }
    *handle = raw;
    FileResult r;
    r.ok = true;
    r.osError = 0;  // 成功态错误码恒 0（FileResult 契约，消除残留歧义）
    return r;
}

/** @brief 由"API 是否成功"与 GetLastError 组装 FileResult（各步骤通用）。 */
FileResult boolToResult(BOOL okFlag)
{
    FileResult r;
    r.ok = (okFlag != FALSE);
    // 仅失败时读取 GetLastError：成功态显式置 0——GetLastError 在成功
    // 路径上的返回值是未定义的残留（Win32 契约"仅失败时有意义"）。
    r.osError = r.ok ? 0UL : static_cast<unsigned long>(::GetLastError());
    return r;
}

}  // namespace

// ---------------------------------------------------------------------
// Win32FileOps——IFileOps 的真实实现（全部为 Win32 API 薄封装；无状态，
// 各方法可并发调用——共享的只有 OS 句柄表，由句柄本身保证隔离）。
// ---------------------------------------------------------------------

FileResult Win32FileOps::openWriteThrough(const std::wstring& path,
                                          HANDLE* handle)
{
    // 固定打开形态（§7.2 行 1）：GENERIC_WRITE＋CREATE_ALWAYS＋
    // FILE_FLAG_WRITE_THROUGH。FILE_FLAG_WRITE_THROUGH＝写穿透：系统
    // 缓存旁路、写请求直达介质（Microsoft Learn：CreateFileW 的
    // dwFlagsAndAttributes 说明）；它是 flush 闸门之外的持久性半边——
    // 两者并用才构成 §7.1 第 2 步的完整暂存写持久化。
    // 不附加 FILE_FLAG_OVERLAPPED：同步 I/O 模型，句柄不设事件语义。
    HANDLE raw = ::CreateFileW(path.c_str(), GENERIC_WRITE, 0 /*不共享——
                               暂存写独占打开，防并发读者见半文件*/,
                               nullptr /*默认安全属性*/, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH,
                               nullptr /*无模板文件*/);
    return handleToResult(raw, handle);
}

FileResult Win32FileOps::writeChunk(HANDLE handle, const char* data,
                                    std::size_t length)
{
    if (length == 0) {
        // 空块＝调用方边界（空文件暂存）：直接成功，不发起 WriteFile
        // （WriteFile 对 0 长度行为不作为持久性口径依赖）。
        FileResult r;
        r.ok = true;
        return r;
    }
    // Win32 WriteFile 以 DWORD 计数（32 位上限 4 GiB-1）：kWriteChunkSize
    // 分块保证单次调用量恒小于该上限，length 参数无需再分段。
    DWORD written = 0;
    const BOOL okFlag = ::WriteFile(handle, data,
                                    static_cast<DWORD>(length), &written,
                                    nullptr /*同步 I/O，无 OVERLAPPED*/);
    if (okFlag == FALSE || written != length) {
        // 部分写（written < length 且无错误码）按失败处理：暂存写的
        // 契约是"flush 前 n 个字节全部在介质上"，部分写无法对 flush
        // 语义作出承诺——保守报错（§7.2 行 1 使用口径）。此时
        // GetLastError 可能为 NO_ERROR（部分写无错误码），透传 0 并
        // 以 ok=false 表达失败，事务层按写失败归类。
        FileResult r;
        r.ok = false;
        r.osError = static_cast<unsigned long>(::GetLastError());
        return r;
    }
    FileResult r;
    r.ok = true;
    return r;
}

FileResult Win32FileOps::flush(HANDLE handle)
{
    // FlushFileBuffers："将指定文件的缓冲区刷写到磁盘"（Microsoft Learn）
    // ——持久性闸门（§7.2 行 1）；对写穿透句柄仍需调用：写穿透旁路的是
    // 写路径缓存，flush 承诺的是"本句柄此前全部写入已落盘"。
    return boolToResult(::FlushFileBuffers(handle));
}

FileResult Win32FileOps::closeHandle(HANDLE handle)
{
    return boolToResult(::CloseHandle(handle));
}

FileResult Win32FileOps::publishNew(const std::wstring& tempPath,
                                    const std::wstring& targetPath)
{
    // 只增发布（§7.2 行 2）：MoveFileExW 不带 MOVEFILE_REPLACE_EXISTING
    // ——目标已存在时失败（ERROR_ALREADY_EXISTS），绝不覆盖；同卷 rename
    // 为文件系统元数据操作，对其他进程呈现原子可见（旧名或新名，无中间态）。
    return boolToResult(::MoveFileExW(tempPath.c_str(), targetPath.c_str(),
                                      0 /*无标志＝不替换，只增*/));
}

FileResult Win32FileOps::replaceExisting(const std::wstring& tempPath,
                                         const std::wstring& targetPath)
{
    // 原子替换（§7.2 行 3）：REPLACE_EXISTING＝目标存在则整体替换；
    // WRITE_THROUGH＝"函数在文件实际移动到磁盘后才返回"（Microsoft Learn：
    // MoveFileEx）——覆盖以复制＋删除方式执行时的刷盘承诺。同卷 NTFS
    // rename 自身的崩溃原子性依赖 NTFS 元数据日志，本层不超诺宣称
    // （残余窗口由 §7 恢复协议兜底——PM-08）。
    return boolToResult(
        ::MoveFileExW(tempPath.c_str(), targetPath.c_str(),
                      MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH));
}

// ---------------------------------------------------------------------
// AtomicFile——门面编排（唯一职责：把接口调用编排成"失败即中止、闸门
// 先行"的时序；所有业务语义见头文件各方法注释）。
// ---------------------------------------------------------------------

AtomicFile::AtomicFile()
    : m_ops(nullptr)
{
    // 函数级静态：无状态实现对象的进程级共享实例（C++11 magic static
    // 保证并发初始化安全）；m_ops 在此装配，绕过非静态成员初始化次序问题。
    static Win32FileOps sharedRealOps;
    m_ops = &sharedRealOps;
}

AtomicFile::AtomicFile(IFileOps* ops) : m_ops(ops)
{
    // 空接缝＝调用方契约违约（测试装配错误），fail-fast 而非空指针解引用。
    // 项目诊断码体系面向环境错误；本分支属编程错误，直接异常终止。
    if (ops == nullptr) {
        throw std::invalid_argument(
            "AtomicFile: IFileOps 注入不得为空（接缝契约——testkit.md D-10）");
    }
}

FileResult AtomicFile::writeThrough(const std::wstring& path,
                                    const char* data, std::size_t length)
{
    // ---- 第 1 步：打开（故障点 open；失败即无文件，调用方路径上无残留）。
    HANDLE handle = nullptr;
    const FileResult openResult = m_ops->openWriteThrough(path, &handle);
    if (!openResult.ok) {
        return openResult;  // 打开失败：错误码透传，无任何半写（连文件都没有）
    }

    // ---- RAII 句柄守卫：后续任何步骤失败/提前返回都必须关句柄。
    // closeHandle 的返回值在守卫内被记住：若 flush 已成功而关闭失败，
    // 整体仍报失败（保守方向——数据可能已落盘但本原语不作成功承诺，
    // 调用方按失败处置＝放弃发布，安全侧出错；不会出现"报成功但句柄
    // 异常"的误导性成功）。
    struct HandleGuard {
        IFileOps* ops;
        HANDLE handle;
        FileResult closeResult{};
        bool dismissed = false;  // 正常收尾时抑制重复关闭
        ~HandleGuard()
        {
            if (!dismissed && handle != nullptr) {
                closeResult = ops->closeHandle(handle);
            }
        }
    } guard{m_ops, handle, {}, false};

    // ---- 第 2 步：分块写出全部字节（故障点 write-chunk——F2 写中途失败
    // 的注入边界；块大小见 kWriteChunkSize 注释）。
    std::size_t offset = 0;
    while (offset < length) {
        // 尾块不足 kWriteChunkSize 时取剩余量（min 手写避免 windows.h
        // 的 min/max 宏与本编译单元内 STL 头的交互）。
        const std::size_t chunk =
            (length - offset < kWriteChunkSize) ? (length - offset)
                                                : kWriteChunkSize;
        const FileResult writeResult =
            m_ops->writeChunk(handle, data + offset, chunk);
        if (!writeResult.ok) {
            // 写中途失败：直接返回（守卫关句柄）。已写入部分**有意留在
            // 磁盘上**＝暂存残留语义（§7.1 第 2 步"残留部分文件"）——
            // 由启动恢复扫描忽略＋报告，本原语不删除（删除是新的失败面）。
            return writeResult;
        }
        offset += chunk;
    }

    // ---- 第 3 步：持久性闸门（故障点 flush）——FlushFileBuffers 成功
    // 才允许调用方进入发布/替换（原子/持久性分离的时序表达：闸门失败
    // 绝不进入可见性切换）。
    const FileResult flushResult = m_ops->flush(handle);
    if (!flushResult.ok) {
        return flushResult;  // 闸门失败：字节已在文件里但未承诺持久——按失败处置
    }

    // ---- 第 4 步：正常收尾关闭（守卫抑制，取真实关闭结果参与成败判定）。
    guard.dismissed = true;
    const FileResult closeResult = m_ops->closeHandle(handle);
    if (!closeResult.ok) {
        // flush 后关闭失败：极异常环境（句柄表损坏类），保守报失败——
        // 事务层按暂存失败处置（残留被恢复扫描忽略），不会误提交。
        return closeResult;
    }

    FileResult r;
    r.ok = true;
    return r;
}

FileResult AtomicFile::publishNew(const std::wstring& tempPath,
                                  const std::wstring& targetPath)
{
    // 直通接口（§7.2 行 2 语义全部在实现与接口注释中）；不校验同卷——
    // 发布发生在项目根内的同卷约束由调用方协议承担（§7.1 第 4 步），
    // 跨卷时 MoveFileExW 返回失败并透传错误码，同样安全（不覆盖目标）。
    return m_ops->publishNew(tempPath, targetPath);
}

FileResult AtomicFile::replaceFile(const std::wstring& tempPath,
                                   const std::wstring& targetPath)
{
    // 直通接口（§7.2 行 3）；本调用成功即调用方协议中的"唯一提交点"
    // 成立——门面不加任何额外动作，保证提交点语义的单一性。
    return m_ops->replaceExisting(tempPath, targetPath);
}

}  // namespace sdurws::ird::project::win32
