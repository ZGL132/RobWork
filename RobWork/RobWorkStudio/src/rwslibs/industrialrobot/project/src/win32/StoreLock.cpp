/**
 * @file   StoreLock.cpp
 * @brief  写锁（StoreLock）实现——Win32LockOps 薄封装＋获取/心跳/失权状态机。
 *
 * 设计依据：
 *   - units/project.md §9.1（D-02 独占句柄机制；构造＝获取＋写 PID 记录；
 *     析构＝关闭）、§9.2（第二实例读 PID 不破坏互斥；固定宽度记录容忍
 *     撕裂读）、§9.4（文件永不删除重建；原地重写；残留读取作恢复诊断；
 *     残留心跳过期不触发接管）、§9.5（三类失败的不同诊断——本文件透传
 *     原始码，StoreErrorCode 映射归 T08）、§9.6（失权三道防线）、§9.8
 *     （心跳线程/writer 互斥）；
 *   - §7.2 行 5（互斥＝CreateFileW 共享模式：其他写访问→
 *     ERROR_SHARING_VIOLATION；读访问＋FILE_SHARE_WRITE 共享声明→成功
 *     ——本文件 openExclusive/openReadShared 是该行 4/5 的实现载体，
 *     文档口径复核留痕 traceability/builds/wp04-t02/s72-win32-docs-
 *     crosscheck.md，PRJ-T02 先行复核登记）；
 *   - diagnostics.md §4.6（PRJ-LOCK-HELD＝权限/Warning、
 *     PRJ-WRITE-AUTHORITY-LOST＝权限/Error——码值收编权威）；
 *   - 任务契约 PRJ-T03.json acceptance 1/2/4。
 */

#include "StoreLock.hpp"

#include <sdurws/ird/core/DiagData.hpp>

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <stdexcept>

namespace sdurws::ird::project::win32 {

namespace {

// ---------------------------------------------------------------------
// Win32 值结果辅助（与 AtomicFile.cpp 同型的小工具；两文件各自持有
// 匿名命名空间副本——共享须新增内部头，20 行工具的共享成本高于副本，
// NFR-MNT-04 最小实现面）。
// ---------------------------------------------------------------------

/// CreateFileW 返回值 → FileResult（INVALID_HANDLE_VALUE 哨兵归一化）。
FileResult handleToResult(HANDLE raw, HANDLE* handle)
{
    if (raw == INVALID_HANDLE_VALUE) {
        // 失败路径：句柄出参归零（防调用方误用残留栈值）。
        *handle = nullptr;
        FileResult r;
        r.ok = false;
        r.osError = ::GetLastError();
        return r;
    }
    *handle = raw;
    FileResult r;
    r.ok = true;
    r.osError = 0;  // 成功态错误码恒 0（FileResult 契约）
    return r;
}

/// BOOL → FileResult（仅失败时读 GetLastError；成功显式置 0）。
FileResult boolToResult(BOOL okFlag)
{
    FileResult r;
    r.ok = (okFlag != FALSE);
    r.osError = r.ok ? 0UL : static_cast<unsigned long>(::GetLastError());
    return r;
}

// ---------------------------------------------------------------------
// 防线③的三码表（§9.6 原文：SHARING_VIOLATION / INVALID_HANDLE /
// ACCESS_DENIED → LostWrite）。集中定义为具名谓词——三处消费点（分类
// 入口/心跳失败路径）共用同一事实来源，避免散落字面量。
// ---------------------------------------------------------------------

/// 判定一个 Win32 错误码是否属于"失权三码"（§9.6 防线③）。
bool isAuthorityLossError(unsigned long winError)
{
    // 32＝ERROR_SHARING_VIOLATION（他人以冲突共享模式打开——写权限事实
    // 已转移）；6＝ERROR_INVALID_HANDLE（句柄失效——OS 已关闭）；5＝
    // ERROR_ACCESS_DENIED（ACL 收紧/介质转只读——写入被系统拒绝）。
    constexpr unsigned long kSharingViolation = 32;
    constexpr unsigned long kInvalidHandle = 6;
    constexpr unsigned long kAccessDenied = 5;
    return winError == kSharingViolation || winError == kInvalidHandle
        || winError == kAccessDenied;
}

// ---------------------------------------------------------------------
// 固定宽度记录的编码与撕裂容忍解析（§9.2/§9.4——布局见 StoreLock.hpp
// 常量注释；改动布局须同步 kLockRecordSize 与本文件两函数）。
// ---------------------------------------------------------------------

/// 把文本截断/空白填充到定宽（host 字段——诊断数据，截断可接受）。
std::string padOrTruncate(const std::string& text, std::size_t width)
{
    std::string out = text.substr(0, width);
    out.resize(width, ' ');  // 空白填充至定宽
    return out;
}

/// 定宽十进制整数（pid 字段——uint32 十进制最长 10 位，零填充保持定宽）。
std::string fixedPidDigits(std::uint32_t pid)
{
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%010u", static_cast<unsigned>(pid));
    return buf;  // 恰 kLockPidDigits 字节（格式串位宽即 10）
}

/// 序列化一条完整记录（三行定宽；总长恒 kLockRecordSize 字节）。
std::string serializeRecord(const LockHolderRecord& rec)
{
    std::string out;
    out.reserve(kLockRecordSize);
    out += "pid=";
    out += fixedPidDigits(rec.pid);
    out += '\n';
    out += "host=";
    out += padOrTruncate(rec.host, kLockHostWidth);
    out += '\n';
    out += "hb=";
    out += padOrTruncate(rec.heartbeatUtc, kLockHeartbeatWidth);
    out += '\n';
    return out;
}

/// 从定宽文本提取恰好 width 字节并剔除空白填充（越界安全——撕裂读时
/// 字段可能不完整，有多少取多少）。
std::string takeField(const std::string& bytes, std::size_t offset,
                      std::size_t width)
{
    if (offset >= bytes.size()) { return {}; }
    const auto avail = std::min(width, bytes.size() - offset);
    std::string field = bytes.substr(offset, avail);
    // 剔除填充与控制符（撕裂读可能在字段尾混入下一轮写入的残迹——
    // 诊断字段的呈现洁净度处理，不参与任何语义判定）。
    while (!field.empty()
           && (field.back() == ' ' || field.back() == '\r')) {
        field.pop_back();
    }
    return field;
}

/// 定宽字段中前导数字的十进制解析（撕裂容忍：非数字截断，无数＝0）。
std::uint32_t parsePidField(const std::string& digits)
{
    std::uint32_t pid = 0;
    bool any = false;
    for (const char c : digits) {
        if (c < '0' || c > '9') { break; }
        // uint32 十进制至多 10 位——定宽字段不溢出；乘 10 累加安全。
        pid = pid * 10u + static_cast<std::uint32_t>(c - '0');
        any = true;
    }
    return any ? pid : 0u;
}

/**
 * @brief 撕裂容忍解析（§9.2"固定宽度记录，容忍撕裂读"）。
 *
 * 定位策略＝按字段前缀搜索而非按绝对偏移：truncate→write 的重写窗口内
 * 读取方可能看到空文件/半文件/交错字节，绝对偏移会采到错位内容；前缀
 * 定位 + 定宽提取在两种布局态下都给出"最可能的字段值"。任一字段缺失
 * 不算失败——记录只作诊断（心跳仅诊断 D-03），解析结果零值字段按
 * "未知"呈现。
 */
bool parseRecordTolerant(const std::string& bytes, LockHolderRecord* out)
{
    bool any = false;
    // pid 行："pid=" + 10 位数字 + '\n'——首个字段，写入序最先（撕裂读
    // 中最可能完整）。
    if (auto pos = bytes.find("pid="); pos != std::string::npos) {
        out->pid = parsePidField(takeField(bytes, pos + 4, kLockPidDigits));
        any = true;
    }
    if (auto pos = bytes.find("host="); pos != std::string::npos) {
        out->host = takeField(bytes, pos + 5, kLockHostWidth);
        any = true;
    }
    if (auto pos = bytes.find("hb="); pos != std::string::npos) {
        out->heartbeatUtc = takeField(bytes, pos + 3, kLockHeartbeatWidth);
        any = true;
    }
    return any;
}

/// 用户级诊断通用出口（sink 可空＝丢弃——§5.1 装配可空口径）。
void emitReport(IDiagnosticsSink* sink, const core::DiagnosticRecord& rec)
{
    if (sink != nullptr) { sink->report(rec); }
}

/**
 * @brief 构造 PRJ-LOCK-HELD 记录（码值＝diagnostics.md §4.6 收编清单；
 *        paramSchema {pid,host} 的载荷按 §9.2 调用示例口径写入上下文
 *        文本——core::DiagnosticRecord 无参数字段）。
 *
 * subject 为空：锁竞争是**项目级**事件（diagnostics.md §7.7 映射表：
 * lock-held-by-other，subject=∅，非比较型）。文案权威归 diagnostics/ui
 * （§10.3）——此处文本是诊断载荷数据，不含呈现决策。
 */
core::DiagnosticRecord makeLockHeldRecord(const LockHolderRecord& holder)
{
    const std::string pidText = (holder.pid == 0)
        ? "未知（锁记录不可读）"
        : std::to_string(holder.pid);
    return core::DiagnosticRecord::make(
        std::string{"PRJ-LOCK-HELD"},
        std::nullopt,  // subject：项目级事件（§8.2/§8.5 记法＝∅）
        std::nullopt, std::nullopt,
        "项目以可写方式打开被拒绝：存储写锁被其他实例持有（PID=" + pidText
            + "，主机=" + (holder.host.empty() ? "未知" : holder.host) + "）",
        "另一实例通过 OS 独占句柄持有 .rwdesign/lock（内核裁决，ERROR_"
        "SHARING_VIOLATION）；其心跳=" + (holder.heartbeatUtc.empty()
            ? "未知"
            : holder.heartbeatUtc) + "（仅诊断——不作为接管或权限判据，D-03）",
        "以只读方式查看项目（PM-07：第二写者不阻塞等待）；或关闭持有实例后"
        "重试可写打开");
}

/**
 * @brief 构造 PRJ-WRITE-AUTHORITY-LOST 记录（权限/Error——diagnostics.md
 *        §4.6；§5.0 映射：write-rejected/context-closed 同落此码）。
 */
core::DiagnosticRecord makeWriteAuthorityLostRecord(
    const std::string& lossDetail)
{
    return core::DiagnosticRecord::make(
        std::string{"PRJ-WRITE-AUTHORITY-LOST"},
        std::nullopt,  // 项目级事件（同 PRJ-LOCK-HELD 口径）
        std::nullopt, std::nullopt,
        "写权限已丢失：本存储上下文的后续写入将被全部拒绝（SA-17：写权限"
        "唯一依据＝OS 独占句柄，失权不可逆）",
        lossDetail,
        "重新打开项目以重新取得写权限；中断前的未提交内容由恢复流程处置"
        "（PM-08）——不得凭心跳存活自行恢复写入（D-03）");
}

}  // namespace

// ---------------------------------------------------------------------
// Win32LockOps——ILockOps 的真实实现（§7.2 行 5 互斥语义的载体；无状态）。
// ---------------------------------------------------------------------

FileResult Win32LockOps::openExclusive(const std::wstring& path,
                                       HANDLE* handle)
{
    // §9.1 D-02 固定形态：读＋写访问，共享声明**仅** FILE_SHARE_READ，
    // OPEN_ALWAYS（已存在＝接管打开；不存在＝首次创建——文件永不删除
    // 重建，D-03，本接口无删除动作）。
    // 权限语义：共享模式缺 FILE_SHARE_WRITE ⇒ 其他实例的写访问请求得
    // ERROR_SHARING_VIOLATION（内核原子裁决）；含 FILE_SHARE_READ ⇒
    // 第二实例 GENERIC_READ 可共存（§9.2 读 PID）。
    HANDLE raw = ::CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
                               FILE_SHARE_READ,
                               nullptr /*默认安全属性*/, OPEN_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL,
                               nullptr /*无模板文件*/);
    return handleToResult(raw, handle);
}

FileResult Win32LockOps::openReadShared(const std::wstring& path,
                                        HANDLE* handle)
{
    // §9.2 第二实例流程②：只读访问；共享声明 READ|WRITE＝显式允许与
    // 持有者的写访问共存（不破坏互斥）；OPEN_EXISTING＝锁未创建过即
    // 失败（ERROR_FILE_NOT_FOUND——"无持有者信息"的合法观察）。
    HANDLE raw = ::CreateFileW(path.c_str(), GENERIC_READ,
                               FILE_SHARE_READ | FILE_SHARE_WRITE,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                               nullptr);
    return handleToResult(raw, handle);
}

FileResult Win32LockOps::readAll(HANDLE handle, std::string* bytes)
{
    if (bytes == nullptr) {
        // 调用方契约违约：出参指针为空——fail-fast 不静默（原语层惯例）。
        return FileResult{false, 87UL /*ERROR_INVALID_PARAMETER*/};
    }
    // 从文件头读到文件尾。读取期间持有方可能并发重写（心跳）——内容
    // 撕裂容忍归解析层（parseRecordTolerant），本方法如实交付字节。
    LARGE_INTEGER zero;
    zero.QuadPart = 0;
    if (::SetFilePointerEx(handle, zero, nullptr, FILE_BEGIN) == 0) {
        FileResult r;
        r.ok = false;
        r.osError = static_cast<unsigned long>(::GetLastError());
        return r;
    }
    std::string out;
    char chunk[4096];
    for (;;) {
        DWORD got = 0;
        if (::ReadFile(handle, chunk, sizeof(chunk), &got, nullptr) == FALSE) {
            FileResult r;
            r.ok = false;
            r.osError = static_cast<unsigned long>(::GetLastError());
            *bytes = std::move(out);
            return r;
        }
        if (got == 0) { break; }  // EOF——正常终止（got==0 且无错误）
        out.append(chunk, got);
    }
    *bytes = std::move(out);
    FileResult r;
    r.ok = true;
    return r;
}

FileResult Win32LockOps::rewriteRecord(HANDLE handle, const char* data,
                                       std::size_t length)
{
    if (length == 0 || data == nullptr) {
        // 固定宽度记录不可能为空——空载荷属调用方契约违约（§9.4 原地
        // 重写语义的输入不变量）。
        return FileResult{false, 87UL /*ERROR_INVALID_PARAMETER*/};
    }
    // §9.4 原文序：truncate→write→flush。
    // 第 1 步：定位文件头（所有写都从 0 开始——记录定宽，覆盖旧内容）。
    LARGE_INTEGER zero;
    zero.QuadPart = 0;
    if (::SetFilePointerEx(handle, zero, nullptr, FILE_BEGIN) == 0) {
        FileResult r;
        r.ok = false;
        r.osError = static_cast<unsigned long>(::GetLastError());
        return r;
    }
    // 第 2 步：截断至当前偏移（0）——记录定宽时长度实际不变，但显式
    // 截断保证"缩短的重写"不会残留旧尾字节（防御性：布局演进时的安全网）。
    if (::SetEndOfFile(handle) == 0) {
        FileResult r;
        r.ok = false;
        r.osError = static_cast<unsigned long>(::GetLastError());
        return r;
    }
    if (::SetFilePointerEx(handle, zero, nullptr, FILE_BEGIN) == 0) {
        FileResult r;
        r.ok = false;
        r.osError = static_cast<unsigned long>(::GetLastError());
        return r;
    }
    // 第 3 步：整条写出（记录 113 字节，单次 WriteFile 足够；部分写按
    // 失败处理——同 AtomicFile 的部分写口径）。
    DWORD written = 0;
    if (::WriteFile(handle, data, static_cast<DWORD>(length), &written,
                    nullptr) == FALSE
        || written != length) {
        FileResult r;
        r.ok = false;
        r.osError = static_cast<unsigned long>(::GetLastError());
        return r;
    }
    // 第 4 步：刷盘（FlushFileBuffers——心跳内容落盘，诊断可信度闸门；
    // 心跳本身无持久性承诺语义，D-03：丢失无害）。
    return boolToResult(::FlushFileBuffers(handle));
}

FileResult Win32LockOps::probe(HANDLE handle)
{
    // §9.6 防线②：GetHandleInformation＝轻量句柄有效性探测（仅读句柄表
    // 标志，不发起 I/O——写前权威检查每笔写事务都要走，必须廉价）。
    DWORD flags = 0;
    return boolToResult(::GetHandleInformation(handle, &flags));
}

FileResult Win32LockOps::closeHandle(HANDLE handle)
{
    return boolToResult(::CloseHandle(handle));
}

// ---------------------------------------------------------------------
// 自由函数：记录读取与时钟文本。
// ---------------------------------------------------------------------

LockRecordRead readHolderRecord(ILockOps* ops, const std::wstring& lockPath)
{
    LockRecordRead out;
    if (ops == nullptr || lockPath.empty()) {
        // 调用方契约违约：环境错误值语义不适用——fail-fast（本函数无
        // 异常路径的文档口径被违约破坏时，宁可崩溃也不返回假数据）。
        throw std::invalid_argument(
            "project/store-lock: readHolderRecord 契约违约（空 ops/空路径）");
    }
    // §9.2 流程②：只读共享打开（与持有者写访问共存，不破坏互斥）。
    HANDLE h = nullptr;
    const auto openRes = ops->openReadShared(lockPath, &h);
    if (!openRes.ok) {
        out.osError = openRes.osError;
        return out;  // ok==false：文件不存在/不可读——record 保持全零值
    }
    std::string bytes;
    const auto readRes = ops->readAll(h, &bytes);
    // 读毕立即关闭（§9.2："读句柄随即关闭，不长期持有；PID 信息已取"）。
    // 关闭失败不改变"不再使用"的事实——吞掉结果只留错误码可观测性。
    static_cast<void>(ops->closeHandle(h));
    if (!readRes.ok) {
        out.osError = readRes.osError;
        return out;
    }
    out.ok = true;
    out.parsed = parseRecordTolerant(bytes, &out.record);
    return out;
}

std::string utcNowIsoMilli()
{
    using namespace std::chrono;
    const auto now = system_clock::now();
    // 毫秒分量＝自纪元毫秒数对 1000 取模（负值不会出现——系统时钟单调
    // 起点为正；即便回拨，取模语义仍给出 0..999 的合法展示值）。
    const auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
    const std::time_t t = system_clock::to_time_t(now);
    std::tm tmUtc{};
    if (::gmtime_s(&tmUtc, &t) != 0) {
        // gmtime_s 失败（实现层边缘）：回退为全零时刻——心跳是诊断字段，
        // 错误文本优于抛异常打断锁获取路径。
        return std::string{"1970-01-01T00:00:00.000Z"};
    }
    char buf[32];
    // 定宽 24 字节格式（kLockHeartbeatWidth）——YYYY-MM-DDTHH:MM:SS.mmmZ。
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
                  tmUtc.tm_year + 1900, tmUtc.tm_mon + 1, tmUtc.tm_mday,
                  tmUtc.tm_hour, tmUtc.tm_min, tmUtc.tm_sec,
                  static_cast<int>(ms.count()));
    return buf;
}

// ---------------------------------------------------------------------
// StoreLock——RAII 独占句柄＋失权状态机。
// ---------------------------------------------------------------------

StoreLock::StoreLock(ILockOps* ops, IDiagnosticsSink* sink,
                     const std::wstring& lockPath, const LockSelfRecord& self,
                     std::chrono::milliseconds heartbeatPeriod)
    : m_ops(ops)
    , m_sink(sink)
    , m_lockPath(lockPath)
    , m_handle(nullptr)
    , m_status(AcquireStatus::OsError)
    , m_acquireOsError(0)
    , m_everHeld(false)
    , m_authorityLost(false)
    , m_heartbeatPeriod(heartbeatPeriod)
    , m_heartbeatStop(false)
{
    // ---- 调用方契约违约：fail-fast（§3 约定——空接缝/空路径不静默）。
    if (ops == nullptr) {
        throw std::invalid_argument(
            "project/store-lock: ILockOps 为空（注入接缝缺失）");
    }
    if (lockPath.empty()) {
        throw std::invalid_argument("project/store-lock: 锁路径为空");
    }
    if (heartbeatPeriod < std::chrono::milliseconds::zero()) {
        throw std::invalid_argument("project/store-lock: 心跳周期为负");
    }

    // ---- 第一步：独占打开（内核原子裁决——D-02/§9.9 流程图起点）。
    HANDLE h = nullptr;
    const auto openRes = ops->openExclusive(lockPath, &h);
    if (!openRes.ok) {
        if (openRes.osError == 32UL /*ERROR_SHARING_VIOLATION*/) {
            // ---- 分支 A：锁被持有（§9.2 流程①的必然后续）——按 PM-07
            // 立即转只读路径，不阻塞等待、不重试轮询（等待归调用方策略）。
            m_status = AcquireStatus::HeldByOther;
            // §9.2 流程②：读取持有者记录（只读共享打开，用毕即关）。
            const auto holderRead = readHolderRecord(ops, lockPath);
            m_lastHolder = holderRead.record;  // 撕裂容忍——零值字段按未知呈现
            // PM-07 提示数据面：PRJ-LOCK-HELD（含持有 PID）经 sink 产出
            // （acceptance 2/4——稳定诊断＋注入式适配器形态）。
            reportLockHeldLocked();
            return;  // 只读上下文形态（held()==false）
        }
        // ---- 分支 B：其他 OS 错误（§9.5 前两类：介质只读/权限不足——
        // 原始码留存，StoreErrorCode 映射归 T08 打开协议）。
        m_status = AcquireStatus::OsError;
        m_acquireOsError = openRes.osError;
        reportDev("project/lock",
                  "锁获取失败（非共享冲突）: path 错误码=" + std::to_string(openRes.osError));
        return;
    }

    // ---- 第二步：获取成功——进入持有态（§9.1"构造＝获取"）。
    m_handle = h;
    m_status = AcquireStatus::Held;
    m_everHeld = true;
    m_self.pid = self.pid;
    m_self.host = self.host;
    m_self.heartbeatUtc = self.initialHeartbeatUtc;
    m_lastHolder = m_self;

    // ---- 第三步（§9.4 接管恢复诊断）：读取残留记录。能成功独占打开
    // 意味着原持有者已退出/崩溃（OS 已关其句柄）——残留内容是 PM-08
    // 恢复诊断的数据面（"锁残留"报告项）。读到异于自身的 PID 才算残留。
    std::string residualBytes;
    const auto residualRead = ops->readAll(m_handle, &residualBytes);
    if (residualRead.ok) {
        LockHolderRecord residual;
        if (parseRecordTolerant(residualBytes, &residual)
            && residual.pid != 0 && residual.pid != m_self.pid) {
            reportDev("project/lock-recovery",
                      "接管写锁：发现残留锁记录（PM-08 恢复诊断）——残留 PID="
                          + std::to_string(residual.pid) + "，心跳="
                          + (residual.heartbeatUtc.empty() ? "未知"
                                                           : residual.heartbeatUtc)
                          + "（原持有者已退出；残留心跳陈旧不参与接管判定——"
                            "接管唯一途径＝OS 锁可被获取，D-03）");
        }
    }
    // 残留读取失败不阻断接管（新记录即将覆盖全部内容）。

    // ---- 第四步（§9.1"＋写 PID 记录"）：写自我身份记录（固定宽度；
    // 初始心跳＝调用方时钟供给）。此写路径不走门卫——本对象尚在构造，
    // 权威刚刚由内核授予并握在本对象手中（探测自己刚拿到的句柄是冗余）。
    const std::string record = serializeRecord(m_self);
    const auto writeRes = ops->rewriteRecord(m_handle, record.data(),
                                             record.size());
    if (!writeRes.ok) {
        // 获取成功但记录写失败：权威未落地（他人无法得知持有者）——按
        // OsError 处置并释放句柄（获取动作原子性不被破坏：失败＝回到
        // 无主状态，其他实例可接管）。
        m_acquireOsError = writeRes.osError;
        m_status = AcquireStatus::OsError;
        m_everHeld = false;
        m_lastHolder = LockHolderRecord{};  // 持有者信息归零（获取未成立，
                                            // 诊断按"未知"呈现而非自我 PID）
        static_cast<void>(ops->closeHandle(m_handle));
        m_handle = nullptr;
        reportDev("project/lock",
                  "锁记录写入失败（获取回退）: 错误码="
                      + std::to_string(writeRes.osError));
        return;
    }

    // ---- 第五步：心跳线程（§9.4——周期 30 s 默认；0＝禁用）。纯诊断
    // 通道：线程只做记录重写，绝不参与任何权限判定（SA-17 结构性落实
    // ——心跳线程无权限状态写通路，见 rewriteHeartbeatLocked 仅改文件）。
    if (m_heartbeatPeriod > std::chrono::milliseconds::zero()) {
        m_heartbeat = std::thread([this] {
            std::unique_lock<std::mutex> lk(m_mutex);
            while (!m_heartbeatStop) {
                // 定时等待：到期→重写；被 notify（release）→退出。
                if (m_cv.wait_for(lk, m_heartbeatPeriod)
                    == std::cv_status::timeout) {
                    if (m_heartbeatStop) { break; }
                    // 持锁重写——与调用方写入口在同一互斥下串行（§9.8
                    // writer 互斥的最小形态：锁文件的两条写通道汇合）。
                    static_cast<void>(rewriteHeartbeatLocked());
                }
            }
        });
    }
}

StoreLock::~StoreLock()
{
    // RAII 释放（§9.1"析构＝关闭——关闭即失权"）。release 不抛异常；
    // 析构中的 join 保证心跳线程不悬空（§9.8：关闭时 join）。
    release();
}

AcquireStatus StoreLock::status() const noexcept
{
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_status;
}

bool StoreLock::held() const noexcept
{
    std::lock_guard<std::mutex> lk(m_mutex);
    // 写权限判据①（状态机）：句柄在握且未失权、未释放。心跳内容零参与
    // （SA-17：权限即锁——结构性排除心跳判据）。
    return m_handle != nullptr && !m_authorityLost;
}

LockHolderRecord StoreLock::lastHolder() const
{
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_lastHolder;
}

unsigned long StoreLock::acquireOsError() const noexcept
{
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_acquireOsError;
}

const std::wstring& StoreLock::lockPath() const noexcept
{
    // 构造后恒定——无需加锁（const 引用，无写通路）。
    return m_lockPath;
}

bool StoreLock::requireWriteAuthority()
{
    std::lock_guard<std::mutex> lk(m_mutex);
    return requireWriteAuthorityLocked();
}

bool StoreLock::requireWriteAuthorityLocked()
{
    // ---- 防线①（状态机）：三态拒绝，各有稳定诊断（§9.6"非 Active 一律
    // 拒绝"）。
    if (m_authorityLost) {
        // 已失权（②③先前锁存）：后续写全拒＋PRJ-WRITE-AUTHORITY-LOST
        // （§9.6 原文"后续写全拒＋该码"——每次拒绝都伴随诊断，去重归
        // diagnostics 信封的 DedupKey）。
        reportAuthorityLostLocked("失权锁存（先前探测或写分类已判定）");
        return false;
    }
    if (m_handle == nullptr) {
        if (m_everHeld) {
            // 已释放/已关闭（上下文 Closed 语义）：§5.0 映射
            // context-closed → PRJ-WRITE-AUTHORITY-LOST。
            reportAuthorityLostLocked("写锁已释放（上下文关闭）");
        } else {
            // 只读上下文（从未获取——锁被他人持有）：PRJ-LOCK-HELD 含
            // 持有 PID（acceptance 2 的"全部写入口一律拒绝＋稳定诊断"）。
            reportLockHeldLocked();
        }
        return false;
    }
    // ---- 防线②（权威探测）：轻量句柄有效性检查——失败即失权（§9.6
    // "每笔写事务开始时对 lock 句柄执行轻量有效性探测；失败即失权"）。
    const auto probeRes = m_ops->probe(m_handle);
    if (!probeRes.ok) {
        m_authorityLost = true;  // 失权锁存——不可逆（SA-17：句柄事实已变）
        reportAuthorityLostLocked("权威探测失败（句柄异常失效）: 错误码="
                                  + std::to_string(probeRes.osError));
        return false;
    }
    return true;
}

bool StoreLock::rewriteHeartbeat()
{
    std::lock_guard<std::mutex> lk(m_mutex);
    return rewriteHeartbeatLocked();
}

bool StoreLock::rewriteHeartbeatLocked()
{
    // 门卫（防线①②）先行——心跳重写也是写入口（§9.6 覆盖"锁重写"，
    // §9.8 writer 互斥清单显式含"锁重写"）。
    if (!requireWriteAuthorityLocked()) { return false; }

    // 以当前系统时间刷新自我记录的心跳字段（§9.4：原地重写固定宽度
    // 记录——文件路径与长度不变，D-03 防锁对象分裂）。
    m_self.heartbeatUtc = utcNowIsoMilli();
    m_lastHolder = m_self;
    const std::string record = serializeRecord(m_self);
    const auto writeRes = m_ops->rewriteRecord(m_handle, record.data(),
                                               record.size());
    if (!writeRes.ok) {
        // ---- 防线③（I/O 分类）：心跳写失败同通道分类。三码＝失权
        // （句柄事实已变——与心跳语义无关的底层事实）；非三码（磁盘满等
        // 环境错误）只记开发诊断：心跳丢失无害（D-03：心跳仅诊断），
        // 不得作为失权或权限判据。
        // 走锁内核（本函数已持 m_mutex——公开版 classifyIoError 内部
        // 会二次加锁，非递归互斥上即死锁）。
        classifyIoErrorLocked(writeRes.osError);
        if (!m_authorityLost) {
            reportDev("project/lock",
                      "心跳重写失败（非失权类错误，不触发失权）: 错误码="
                          + std::to_string(writeRes.osError));
        }
        return false;
    }
    return true;
}

void StoreLock::classifyIoError(unsigned long winError)
{
    // 三码表判定（§9.6 防线③原文码集）；非三码＝其他环境错误，不由本
    // 失权通道处置（磁盘满/路径类错误归各写入口自己的诊断语义）。
    if (!isAuthorityLossError(winError)) { return; }
    std::lock_guard<std::mutex> lk(m_mutex);
    classifyIoErrorLocked(winError);
}

void StoreLock::classifyIoErrorLocked(unsigned long winError)
{
    // 前置：调用方已持 m_mutex。
    if (m_authorityLost || m_handle == nullptr) {
        return;  // 已失权/已释放——状态机只进不退，重复分类是幂等空操作
    }
    m_authorityLost = true;
    reportAuthorityLostLocked("写操作遇内核冲突/句柄失效错误: 错误码="
                              + std::to_string(winError));
}

void StoreLock::release()
{
    std::thread heartbeat;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (m_handle != nullptr) {
            // 关闭即失权（§9.1：OS 关闭句柄→排他性即时消失）。关闭失败
            // 不改变"本对象弃用该句柄"的事实——错误码留开发诊断可观测。
            const auto closeRes = m_ops->closeHandle(m_handle);
            if (!closeRes.ok) {
                reportDev("project/lock",
                          "锁句柄关闭异常（对象仍进入释放态）: 错误码="
                              + std::to_string(closeRes.osError));
            }
            m_handle = nullptr;
        }
        m_heartbeatStop = true;  // 心跳线程退出通知（未启动过也安全）
        m_cv.notify_all();
        heartbeat = std::move(m_heartbeat);
    }
    // 锁外 join（§9.8"关闭时 join"——持锁 join 会与线程内的锁获取死锁）。
    if (heartbeat.joinable()) { heartbeat.join(); }
}

void StoreLock::reportLockHeldLocked() const
{
    emitReport(m_sink, makeLockHeldRecord(m_lastHolder));
}

void StoreLock::reportAuthorityLostLocked(const std::string& detail) const
{
    emitReport(m_sink, makeWriteAuthorityLostRecord(detail));
}

void StoreLock::reportDev(const std::string& channel,
                          const std::string& message) const
{
    if (m_sink != nullptr) { m_sink->reportDev(channel, message); }
}

}  // namespace sdurws::ird::project::win32
