/**
 * @file   ResourceIo.cpp
 * @brief  资源导入服务实现——IResourceReader（open/snapshot/identify/
 *         dependencyTree）：SafePath 规范化→预算预检→Win32 打开（reparse
 *         拒绝＋final-path 复核）→流式读取（每块预算/取消检查点）→SHA-256
 *         摘要（core ContentDigester）→快照产出；XML 依赖树经 expat
 *         （P-IO-3 冻结选型）抽取 include/mesh 边并执行循环/深度/缺失检测。
 *
 * 设计依据：
 *   - units/io.md §4.1~§4.5（角色/规范化/四分类/防护流程②→⑦）、§6.1~
 *     §6.6（文件层读取/规模预检/依赖树/变化检测）、§8.2~§8.3（快照/检测）、
 *     §9.6（IResourceReader 契约表）、§9.13（线程与取消总则）
 *   - 需求 NFR-REL-04（缺失/变化可检测）、NFR-SEC-01/02（P-1 例外＋预算）、
 *     MDL-19（展开护栏文件层）、SA-12/NFR-MNT-03（SHA-256 唯一算法）、
 *     SA-14（统一入口防护——本文件即资源通道的防护核）
 *   - 任务契约 tasks/foundation/IO-T05.json（≙WP-11-T06）acceptance 1/3
 *     （V15 环与深度/V16 网格预算/V25 权限四分类/V26 缺失清单/V27 摘要稳定）
 *
 * 实现边界说明：
 *   - 本文件（IO-T05 提交 1/3）承载 IResourceReader 全量；IResourceSnapshotter
 *     （probe/solidifyToStaging）与 IRuntimeResourceAdapter（§8.6 五条）随
 *     提交 2/3 在本文件续承载（同名任务文件——§3.4 布局）。
 *   - Win32 使用口径（§4.2 按 Microsoft Learn 公开文档）：CreateFileW +
 *     FILE_FLAG_OPEN_REPARSE_POINT（P-4/P-5 拒绝打开 reparse point 本身）、
 *     GetFinalPathNameByHandleW（final-path 复核 SP-6＋P-1 实体路径记录）、
 *     GetFileInformationByHandleEx（大小/mtime——NT FILETIME）；读取句柄
 *     共享模式 READ|WRITE——复制窗口内源可被改写是 §8.4 变化检测的语义
 *     前提（拒绝共享写会把该问题转移为 LOCK-CONFLICT，与检测语义相悖）。
 *   - XML 解析经 expat 2.8.3（P-IO-3 冻结——io.md §15.3；expat 消费点
 *     ＝ResourceIo.cpp，§3.3 登记行）；expat 类型不出公共面。
 *   - 零 Qt（R-3/NFR-MNT-01）；异常仅在内部被捕获转换（§1.4——对外
 *     IoResult 错误轨道）。
 */

#include <sdurws/ird/io/ResourceIo.hpp>

#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/io/Budget.hpp>
#include <sdurws/ird/io/IoDiagnostics.hpp>
#include <sdurws/ird/io/IoError.hpp>
#include <sdurws/ird/io/IoFwd.hpp>
#include <sdurws/ird/io/SafePath.hpp>

#include <expat.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
// WC_ERR_INVALID_CHARS/GetFileInformationByHandleEx/GetFinalPathNameBy-
// HandleW 等 API 需要 Vista+ 目标宏——RobWork 构建树全局把 _WIN32_WINNT
// 钉在旧值（XP 基线），此处强制抬到 Win7（仍在 NFR-DEP-01 Windows x64
// 口径内；仅本翻译单元生效，不影响其他目标）。
#ifdef _WIN32_WINNT
#undef _WIN32_WINNT
#endif
#define _WIN32_WINNT 0x0601
#ifdef WINVER
#undef WINVER
#endif
#define WINVER 0x0601
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace sdurws::ird::io {

// 前向声明：OS 错误构造助手（定义于下方助手区——ResourceStreamHandle::read
// 成员先于其定义点使用；签名/契约见其定义处注释）。
IoError makeResourceOsError(DWORD win32Error, bool writeDirection,
                            const std::filesystem::path& target, std::string context);

// =====================================================================
// ResourceStreamImpl——句柄实现承载（头文件前向声明的不完整类型；
// 本翻译单元给出完整定义——Win32 句柄与元数据不出公共面）
// =====================================================================

struct ResourceStreamImpl {
    HANDLE handle = INVALID_HANDLE_VALUE;   ///< Win32 文件句柄（唯一所有权）
    std::filesystem::path finalPath;        ///< final-path 复核后的实体路径
    std::uint64_t sizeBytes = 0;            ///< 打开时 stat 大小（提示性）
    std::uint64_t mtimeUtc = 0;             ///< LastWriteTime（NT FILETIME——预筛）

    ~ResourceStreamImpl()
    {
        if (handle != INVALID_HANDLE_VALUE) {
            ::CloseHandle(handle);          // RAII 关闭（§9.6 句柄会话级）
        }
    }

    ResourceStreamImpl() = default;
    ResourceStreamImpl(const ResourceStreamImpl&) = delete;
    ResourceStreamImpl& operator=(const ResourceStreamImpl&) = delete;
};

// ---------------------------------------------------------------------
// ResourceStreamHandle 成员（须在完整类型可见处定义——移动/析构/adopt）
// ---------------------------------------------------------------------

ResourceStreamHandle::~ResourceStreamHandle() = default;
ResourceStreamHandle::ResourceStreamHandle(ResourceStreamHandle&& other) noexcept = default;
ResourceStreamHandle& ResourceStreamHandle::operator=(ResourceStreamHandle&& other) noexcept = default;

ResourceStreamHandle ResourceStreamHandle::adopt(std::unique_ptr<ResourceStreamImpl> impl)
{
    ResourceStreamHandle h;
    h.m_impl = std::move(impl);
    return h;
}

bool ResourceStreamHandle::isOpen() const noexcept
{
    return m_impl != nullptr && m_impl->handle != INVALID_HANDLE_VALUE;
}

const std::filesystem::path& ResourceStreamHandle::finalPath() const noexcept
{
    static const std::filesystem::path kEmpty;
    return m_impl != nullptr ? m_impl->finalPath : kEmpty;
}

std::uint64_t ResourceStreamHandle::sizeBytes() const noexcept
{
    return m_impl != nullptr ? m_impl->sizeBytes : 0;
}

IoResult<std::size_t> ResourceStreamHandle::read(void* dst, std::size_t maxBytes)
{
    IoResult<std::size_t> out;
    if (!isOpen() || dst == nullptr || maxBytes == 0) {
        // 调用方契约违约（未 open 即 read/空缓冲/零长）——防御性内部错误。
        out.error.code = IoErrorCode::FormatInternal;
        out.error.detail = "ResourceStreamHandle::read 前置违约（句柄/缓冲/长度）";
        return out;
    }
    // 单次 ReadFile（≤DWORD 上限；调用方以块大小 64 KiB 驱动，不会触顶）。
    DWORD got = 0;
    const DWORD want = static_cast<DWORD>(std::min<std::size_t>(maxBytes, 0xFFFFFFFFu));
    if (!::ReadFile(m_impl->handle, dst, want, &got, nullptr)) {
        const DWORD err = ::GetLastError();
        out.error = makeResourceOsError(err, false, m_impl->finalPath,
                                        "ReadFile 失败（流式读取——§4.4⑤）");
        return out;
    }
    out.value = static_cast<std::size_t>(got);      // 0＝EOF（§9.6 read 契约）
    return out;
}

// =====================================================================
// 内部助手（匿名命名空间——不出翻译单元）
// =====================================================================
namespace {

// ---------------------------------------------------------------------
// UTF-8 ↔ UTF-16 转换（接口面 UTF-8/宽路径并存——§4.2；Win32 转换 API，
// 失败返回空串由调用方按防御性内部错误拒绝）
// ---------------------------------------------------------------------

std::wstring utf8ToWide(const std::string& utf8)
{
    if (utf8.empty()) {
        return {};
    }
    const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(),
                                      static_cast<int>(utf8.size()), nullptr, 0);
    if (n <= 0) {
        return {};
    }
    std::wstring out(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(),
                        static_cast<int>(utf8.size()), out.data(), n);
    return out;
}

std::string wideToUtf8(const std::wstring& wide)
{
    if (wide.empty()) {
        return {};
    }
    const int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(),
                                      static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    if (n <= 0) {
        return {};
    }
    std::string out(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(),
                        static_cast<int>(wide.size()), out.data(), n, nullptr, nullptr);
    return out;
}

/// 去除 \\?\ / \\?\UNC\ 扩展前缀（诊断 display 形态——§4.3.2）。
std::wstring stripExtendedPrefix(const std::wstring& native)
{
    if (native.rfind(L"\\\\?\\UNC\\", 0) == 0) {
        return L"\\\\" + native.substr(8);
    }
    if (native.rfind(L"\\\\?\\", 0) == 0) {
        return native.substr(4);
    }
    return native;
}

/// 诊断用脱敏呈现路径（display——UTF-8、去扩展前缀；敏感值进用户文案前
/// 仍须经 diagnostics 脱敏——§10.3）。
std::string displayOf(const std::filesystem::path& p)
{
    return wideToUtf8(stripExtendedPrefix(p.wstring()));
}

// ---------------------------------------------------------------------
// 字节十六进制（摘要/魔数呈现——十六进制编码不是哈希，SA-12 不受影响）
// ---------------------------------------------------------------------

std::string bytesToHex(const std::uint8_t* data, std::size_t n)
{
    static const char* kDigits = "0123456789abcdef";
    std::string out;
    out.reserve(n * 2);
    for (std::size_t i = 0; i < n; ++i) {
        out.push_back(kDigits[data[i] >> 4]);
        out.push_back(kDigits[data[i] & 0x0F]);
    }
    return out;
}

std::string digestToHex(const core::Digest256& d)
{
    return bytesToHex(d.data(), d.size());
}

// ---------------------------------------------------------------------
// OS 错误四分类（§4.2.5——互斥稳定错误，不把一切失败都报"找不到"）
// ---------------------------------------------------------------------

/// 目标路径是否带只读属性（写方向 ACCESS_DENIED 的 READONLY 判别源——
/// §4.2.5 READONLY 行"目标卷只读属性/写探测失败"的落点：OS 对只读实体
/// 的写打开以 ERROR_ACCESS_DENIED 拒绝，属性复核把两者分开——V24 码区分）。
bool hasReadonlyAttribute(const std::wstring& nativePath)
{
    const DWORD attr = ::GetFileAttributesW(nativePath.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_READONLY) != 0;
}

/// Win32 错误码 → io 资源错误码（§4.2.5 四分类；写方向做 READONLY 判别）。
IoErrorCode classifyWin32Error(DWORD win32Error, bool writeDirection,
                               const std::wstring& nativePath)
{
    switch (win32Error) {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
        return IoErrorCode::ResNotFound;    // §4.2.5 分类一（用户修正后重试）
    case ERROR_WRITE_PROTECT:               // 介质写保护——READONLY 直判（§4.2.5 表）
    case ERROR_NOT_READY:                   // 介质不可用（软只读族）归 READONLY——换介质语义
        return IoErrorCode::ResReadonly;
    case ERROR_SHARING_VIOLATION:
    case ERROR_LOCK_VIOLATION:
        return IoErrorCode::ResLockConflict; // §4.2.5 分类四（io 绝不删对方锁）
    case ERROR_ACCESS_DENIED:
    case ERROR_PRIVILEGE_NOT_HELD:
        // 写方向＋目标实体带只读属性 → IO-RES-READONLY（与 ACCESS-DENIED
        // 区分——V24 观测点"码区分断言"；两码诊断建议动作各异：换介质/
        // 位置 vs 调整权限——§4.2.5 两行）。读方向一律 ACCESS-DENIED
        //（V25：不降级为 NOT-FOUND）。
        if (writeDirection && hasReadonlyAttribute(nativePath)) {
            return IoErrorCode::ResReadonly;
        }
        return IoErrorCode::ResAccessDenied;
    default:
        // 四分类外的 OS 错误保守归 ACCESS-DENIED（"无法访问"语义最近——
        // Csv.cpp mapSystemError 同款兜底），OS 原码进 params 供开发定位。
        return IoErrorCode::ResAccessDenied;
    }
}

} // namespace

IoError makeResourceOsError(DWORD win32Error, bool writeDirection,
                            const std::filesystem::path& target, std::string context)
{
    IoError e;
    e.code = classifyWin32Error(win32Error, writeDirection, target.wstring());
    e.params.emplace_back("path", displayOf(target));
    e.params.emplace_back("direction", writeDirection ? "write" : "read");
    e.params.emplace_back("os-error", std::to_string(static_cast<unsigned long>(win32Error)));
    e.detail = std::move(context);
    return e;
}

// =====================================================================
// 值类型成员（ResourceContentId/ResourceSnapshot——头文件契约的实现；
// 置于助手区后——十六进制编码助手已可见）
// =====================================================================

bool ResourceContentId::isValid() const noexcept
{
    // 非全零判定（core Digest256 保留值纪律——全零＝空，不作身份）。
    for (const std::uint8_t b : digest) {
        if (b != 0) {
            return true;
        }
    }
    return false;
}

std::string ResourceContentId::toHex() const
{
    return bytesToHex(digest.data(), digest.size());
}

ResourceContentId ResourceSnapshot::contentId() const
{
    ResourceContentId id;
    id.digest = contentDigest;
    return id;
}

namespace {

// ---------------------------------------------------------------------
// 路径比较工具（依赖树管辖判定——折叠/分隔符归一/包含边界）
// ---------------------------------------------------------------------

/// 折叠键（比较用小写折叠——§4.2.4 A-Z→a-z；仅 ASCII 折叠与 SafePath 同款口径）。
std::wstring foldForCompare(std::wstring s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) {
        return (c >= L'A' && c <= L'Z') ? static_cast<wchar_t>(c - L'A' + L'a') : c;
    });
    return s;
}

/// 统一分隔符为 '\'（比较前归一——§4.2.1 步骤 2）。
void normalizeSeparators(std::wstring* s)
{
    std::replace(s->begin(), s->end(), L'/', L'\\');
}

/// child 是否落在 root 内（分隔符边界精确比较——防 "D:\proj-evil" 前缀
/// 误判命中 "D:\proj"——§6.2 越界判定的实现核）。
bool isInsideRoot(const std::wstring& root, const std::wstring& child)
{
    if (child.size() <= root.size() || child.compare(0, root.size(), root) != 0) {
        return false;
    }
    return child[root.size()] == L'\\';             // 必须整段命中（边界为分隔符）
}

/// weakly_canonical 的宽字符包装（失败退化为原词法路径——§4.2.4 降级口径）。
std::wstring weaklyCanonicalWide(const std::filesystem::path& p)
{
    std::error_code ec;
    const std::filesystem::path c = std::filesystem::weakly_canonical(p, ec);
    return (ec ? p : c).wstring();
}

/// 取消检查点（命中→IO-CANCELLED——状态非错误，§9.0/UX-03）。
bool checkCancelled(IoCancelToken* cancel, IoError& sink)
{
    if (cancel != nullptr && cancel->isCancelled()) {
        sink.code = IoErrorCode::Cancelled;
        sink.detail = "协作取消命中（资源读取检查点——§4.4）";
        return true;
    }
    return false;
}

IoError internalError(std::string detail)
{
    IoError e;
    e.code = IoErrorCode::FormatInternal;
    e.detail = std::move(detail);
    return e;
}

// ---------------------------------------------------------------------
// 预算 scope 会话（Csv.cpp 同款等价调整执行件——自开自关/采用调用方 scope）
// ---------------------------------------------------------------------

class BudgetScopeSession
{
public:
    BudgetScopeSession() = default;
    BudgetScopeSession(const BudgetScopeSession&) = delete;
    BudgetScopeSession& operator=(const BudgetScopeSession&) = delete;

    /// 析构兜底：内部 scope 若尚未显式关闭则强制关闭（自开 scope 为根
    /// scope——closeScope 无父回收路径，关闭是不可失败的记账动作，兜底
    /// 不吞可观察错误；外部 scope 不归本会话管）。
    ~BudgetScopeSession()
    {
        if (m_guard != nullptr && !m_external && m_scope.value != 0) {
            m_guard->closeScope(m_scope);
        }
    }

    /// 进入会话：guard 空＝自管内部 guard（独立实例，账目不外溢）；句柄
    /// 非零＝采用调用方 scope（不越权关闭——§9.3 同款语义）。
    IoResult<void> enter(IBudgetGuard* guard, BudgetScopeId callerScope)
    {
        if (guard == nullptr) {
            m_owned = makeBudgetGuard();
            guard = m_owned.get();
            if (guard == nullptr) {
                IoResult<void> r;
                r.error = internalError("内部预算守卫创建失败（防御性）");
                return r;
            }
        }
        m_guard = guard;
        if (callerScope.value != 0) {
            m_scope = callerScope;
            m_external = true;
            return {};
        }
        const IoResult<BudgetScopeId> r = m_guard->openScope(BudgetSpec::productDefault());
        if (!r) {
            IoResult<void> rr;
            rr.error = r.error;                     // 缺省规格恒合法——触及即实现缺陷
            return rr;
        }
        m_scope = r.value;
        return {};
    }

    /// 显式离开（成功路径卫生关闭；错误路径由析构兜底）。
    IoResult<void> leave()
    {
        if (m_guard != nullptr && !m_external && m_scope.value != 0) {
            const IoResult<void> r = m_guard->closeScope(m_scope);
            m_scope = BudgetScopeId{};
            return r;
        }
        return {};
    }

    IBudgetGuard* guard() const { return m_guard; }
    BudgetScopeId scope() const { return m_scope; }

    /// 记账一笔（失败状态不变，调用方中止——§9.2）。
    IoResult<void> charge(BudgetDimension dim, std::uint64_t amount)
    {
        if (m_guard == nullptr || m_scope.value == 0) {
            return {};
        }
        return m_guard->charge(m_scope, dim, amount);
    }

    /// 取该维生效限额（深度/网格计数比较判定的 limit 来源——§4.4③）。
    std::uint64_t limitOf(BudgetDimension dim) const
    {
        if (m_guard == nullptr || m_scope.value == 0) {
            return 0;
        }
        const BudgetLedgerSnapshot snap = m_guard->ledger(m_scope);
        for (const auto& entry : snap.dimensions) {
            if (entry.first == dim) {
                return entry.second.limit;
            }
        }
        return 0;
    }

private:
    IBudgetGuard* m_guard = nullptr;        ///< 生效守卫（借用或自管）
    IBudgetGuardPtr m_owned;                ///< null guard 时的自管实例
    BudgetScopeId m_scope;                  ///< 生效 scope
    bool m_external = false;                ///< true＝调用方 scope（不关闭）
};

// ---------------------------------------------------------------------
// 网格流式计数器（§6.3 规模预检的读中复核面——V16"真实超限触发时机读中"）
// ---------------------------------------------------------------------

class MeshStreamCounter
{
public:
    /// 启用条件与统计口径（kind 由识别判定后传入；其余种类不计）。
    void begin(ResourceKind kind)
    {
        m_kind = kind;
        m_word.clear();
        m_wordTooLong = false;
        m_atLineStart = true;
        m_faces = 0;
        m_vertices = 0;
    }

    /**
     * 喂入一块字节（内部累计）：
     *   - AsciiStl：统计词 "facet"（每个 facet 一个三角形面——ASCII STL
     *     语法；字母序列匹配、非字母断词，跨块以 carry 缓冲衔接）；
     *   - WavefrontObj：统计行首 "v "/"f "（顶点/面；行首判定以换行为界，
     *     流首即行首）。
     */
    void feed(const std::uint8_t* data, std::size_t n)
    {
        if (m_kind != ResourceKind::AsciiStl && m_kind != ResourceKind::WavefrontObj) {
            return;
        }
        for (std::size_t i = 0; i < n; ++i) {
            const char c = static_cast<char>(data[i]);
            if (m_kind == ResourceKind::AsciiStl) {
                // 精确词匹配：整个字母串等于 "facet" 才计一面（"endfacet"
                // 等长词不误计——run 超 6 字节置过长标志，结束时不计）。
                if (std::isalpha(static_cast<unsigned char>(c))) {
                    if (m_word.size() < 6) {
                        m_word.push_back(c);
                    } else {
                        m_wordTooLong = true;
                    }
                } else {
                    if (!m_wordTooLong && m_word == "facet") {
                        ++m_faces;
                    }
                    m_word.clear();
                    m_wordTooLong = false;
                }
            } else {
                if (c == '\n' || c == '\r') {
                    m_atLineStart = true;                   // 行界——下一字节为行首
                    m_lineHead.clear();
                    continue;
                }
                if (m_atLineStart) {
                    m_lineHead.push_back(c);
                    if (m_lineHead.size() == 2) {
                        if (m_lineHead == "v " || m_lineHead == "V ") {
                            ++m_vertices;
                        } else if (m_lineHead == "f " || m_lineHead == "F ") {
                            ++m_faces;
                        }
                        m_atLineStart = false;
                    }
                }
            }
        }
    }

    std::uint64_t faces() const { return m_faces; }
    std::uint64_t vertices() const { return m_vertices; }

private:
    ResourceKind m_kind = ResourceKind::Unknown;
    std::string m_word;             ///< STL 当前字母串（≤6 字节——超长即非 facet）
    bool m_wordTooLong = false;     ///< 当前字母串超长（不可能等于 facet）
    bool m_atLineStart = true;      ///< OBJ：当前处于行首（流首亦为行首）
    std::string m_lineHead;         ///< OBJ 行首两字节缓冲
    std::uint64_t m_faces = 0;      ///< 累计面数（facet/f 行）
    std::uint64_t m_vertices = 0;   ///< 累计顶点数（v 行）
};

// ---------------------------------------------------------------------
// 格式识别（§6.3——魔数/扩展名；identify 与读取通道共用的判定核）
// ---------------------------------------------------------------------

std::string lowerExtension(const std::filesystem::path& p)
{
    std::string ext = wideToUtf8(p.extension().wstring());
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

/// STL 二进制/ASCII 判定（§6.3：二进制＝84 字节头含三角数；"solid" 开头
/// 且 84+50×count≠文件大小时按 ASCII——双写头 "solid" 的二进制 STL 以
/// 自洽性等式判回二进制）。
ResourceKind classifyStl(const std::uint8_t* head, std::size_t headLen, std::uint64_t fileSize)
{
    const bool startsSolid = headLen >= 5 && std::memcmp(head, "solid", 5) == 0;
    if (headLen >= 84) {
        std::uint32_t count = 0;
        std::memcpy(&count, head + 80, 4);          // 小端三角数（STL 规范）
        const bool sizeConsistent = (fileSize == 0) || (fileSize == 84ull + 50ull * count);
        if (!startsSolid || sizeConsistent) {
            return ResourceKind::BinaryStl;
        }
    }
    if (startsSolid) {
        return ResourceKind::AsciiStl;
    }
    return headLen >= 84 ? ResourceKind::BinaryStl : ResourceKind::Unknown;
}

/// 扩展名→种类映射（identify 与读取通道的共用判定表；.stl/.xml 返回
/// Unknown＝需要魔数嗅探，由调用方续判）。
ResourceKind kindByExtension(const std::filesystem::path& path)
{
    const std::string ext = lowerExtension(path);
    if (ext == ".obj") {
        return ResourceKind::WavefrontObj;
    }
    if (ext == ".dae") {
        return ResourceKind::ColladaDae;
    }
    if (ext == ".urdf") {
        return ResourceKind::UrdfXml;
    }
    if (ext == ".xacro") {
        return ResourceKind::XacroXml;
    }
    if (ext == ".mtl") {
        return ResourceKind::MaterialLib;
    }
    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga") {
        return ResourceKind::TextureBytes;          // §6.3 纹理按字节资源
    }
    if (ext == ".stl" || ext == ".xml") {
        return ResourceKind::Unknown;               // 需要魔数嗅探
    }
    return ResourceKind::Unknown;                   // 未知扩展——识别失败面
}

/// IO-FORMAT-MESH-UNKNOWN 构造（params：path＋head-hex——§6.3）。
IoError makeMeshUnknown(const std::filesystem::path& path, const std::uint8_t* head,
                        std::size_t headLen)
{
    IoError e;
    e.code = IoErrorCode::FormatMeshUnknown;
    e.params.emplace_back("path", displayOf(path));
    e.params.emplace_back("head-hex", bytesToHex(head, std::min<std::size_t>(headLen, 16)));
    e.detail = "格式识别失败（§6.3——魔数/扩展名均不在登记族）";
    return e;
}

// ---------------------------------------------------------------------
// XML 依赖边抽取（expat——P-IO-3 冻结选型；expat 类型不出公共面）
// ---------------------------------------------------------------------

/// 一个 XML 文档抽取出的引用边（相对目标＋种类——供依赖树遍历器解析）。
struct XmlDeps {
    struct Ref {
        std::string target;             ///< 属性原文（相对路径——遍历器解析）
        ResourceEdgeKind kind;
    };
    std::vector<Ref> refs;
    bool parseError = false;            ///< expat 良构性失败（→IO-FORMAT-XML-SYNTAX）
    int errorLine = 0;                  ///< 解析器定位行（1 起）
    int errorColumn = 0;                ///< 解析器定位列（1 起）
    std::string errorMessage;           ///< 解析器报错文本（detail 用）
};

/// expat 起元素回调：局部名（去命名空间前缀）include/mesh → 引用。
void XMLCALL onStartElement(void* userData, const XML_Char* name, const XML_Char** atts)
{
    XmlDeps* deps = static_cast<XmlDeps*>(userData);
    // 局部名＝最后一个 ':' 之后（xacro:include → include；无前缀原样）。
    const char* colon = std::strchr(name, ':');
    const char* local = colon != nullptr ? colon + 1 : name;

    ResourceEdgeKind kind = ResourceEdgeKind::Include;
    bool known = false;
    if (std::strcmp(local, "include") == 0) {
        kind = ResourceEdgeKind::Include;           // §6.2 include（xacro:include 同款）
        known = true;
    } else if (std::strcmp(local, "mesh") == 0) {
        kind = ResourceEdgeKind::Mesh;              // §6.5 URDF/Xacro 几何引用
        known = true;
    }
    if (!known) {
        return;
    }
    // 属性查找（filename|file|href——Xacro 用 filename，通用 include 变体
    // 收敛三键；其余属性不猜）。
    for (const XML_Char** a = atts; *a != nullptr; a += 2) {
        const char* key = a[0];
        const char* val = a[1];
        if (std::strcmp(key, "filename") == 0 || std::strcmp(key, "file") == 0
            || std::strcmp(key, "href") == 0) {
            if (val != nullptr && *val != '\0') {
                deps->refs.push_back(XmlDeps::Ref{val, kind});
            }
            break;                                  // 首个命中属性即用（文档序）
        }
    }
}

/// 内存 XML 解析（良构性失败→parseError＋行列——IO-FORMAT-XML-SYNTAX 码面）。
XmlDeps parseXmlDeps(const std::uint8_t* data, std::size_t n)
{
    XmlDeps deps;
    XML_Parser parser = XML_ParserCreate(nullptr);
    if (parser == nullptr) {
        deps.parseError = true;
        deps.errorMessage = "expat parser 创建失败";
        return deps;
    }
    XML_SetUserData(parser, &deps);
    XML_SetElementHandler(parser, onStartElement, nullptr);
    const XML_Status st = XML_Parse(parser, reinterpret_cast<const char*>(data),
                                    static_cast<int>(n), 1);
    if (st == XML_STATUS_ERROR) {
        deps.parseError = true;
        deps.errorLine = static_cast<int>(XML_GetCurrentLineNumber(parser));
        deps.errorColumn = static_cast<int>(XML_GetCurrentColumnNumber(parser));
        deps.errorMessage = XML_ErrorString(XML_GetErrorCode(parser));
    }
    XML_ParserFree(parser);
    return deps;
}

// ---------------------------------------------------------------------
// 文本依赖抽取（OBJ mtllib→Material、MTL map_Kd→Texture——§6.5 边行）
// ---------------------------------------------------------------------

struct TextDeps {
    std::vector<std::pair<std::string, ResourceEdgeKind>> refs;   ///< (目标, 边种类)
};

/// 逐行扫描文本引用（ASCII 关键词；UTF-8 多字节内容不影响关键词列判定）。
TextDeps parseTextDeps(const std::uint8_t* data, std::size_t n, ResourceKind kind)
{
    TextDeps out;
    std::size_t i = 0;
    while (i < n) {
        // 取一行（找换行；行尾不含换行符本身）。
        const std::uint8_t* nl =
            static_cast<const std::uint8_t*>(std::memchr(data + i, '\n', n - i));
        const std::size_t eol = (nl != nullptr) ? static_cast<std::size_t>(nl - data) : n;
        std::string line(reinterpret_cast<const char*>(data) + i, eol - i);
        i = (nl != nullptr) ? eol + 1 : n;
        // 去行尾 CR 与首部空白。
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) {
            line.pop_back();
        }
        std::size_t b = 0;
        while (b < line.size() && (line[b] == ' ' || line[b] == '\t')) {
            ++b;
        }
        // OBJ：mtllib <名...>（§6.5 material 边）；MTL：map_Kd <名>（texture 边）。
        const auto startsWith = [&](const char* kw) {
            const std::size_t k = std::strlen(kw);
            return line.size() >= b + k + 1 && line.compare(b, k, kw) == 0
                   && (line[b + k] == ' ' || line[b + k] == '\t');
        };
        const auto valueAfter = [&](std::size_t keyLen) {
            std::size_t t = b + keyLen;
            while (t < line.size() && (line[t] == ' ' || line[t] == '\t')) {
                ++t;
            }
            return line.substr(t);
        };
        if (kind == ResourceKind::WavefrontObj && startsWith("mtllib")) {
            const std::string target = valueAfter(6);
            if (!target.empty()) {
                out.refs.emplace_back(target, ResourceEdgeKind::Material);
            }
        } else if (kind == ResourceKind::MaterialLib && startsWith("map_Kd")) {
            const std::string target = valueAfter(6);
            if (!target.empty()) {
                out.refs.emplace_back(target, ResourceEdgeKind::Texture);
            }
        }
    }
    return out;
}

// ---------------------------------------------------------------------
// 依赖树遍历器状态（折叠键/相对键/环清单工具——ResourceReaderImpl 复用）
// ---------------------------------------------------------------------

/// 管辖根内相对键（正斜杠＋折叠——ResourceNode::relPath 契约；Windows 大
/// 小写不敏感，折叠键反查真实实体可达——§4.2.4）。
std::string relativeKeyOf(const std::filesystem::path& root, const std::filesystem::path& abs)
{
    std::error_code ec;
    const std::filesystem::path rc = std::filesystem::weakly_canonical(root, ec);
    const std::filesystem::path ac = std::filesystem::weakly_canonical(abs, ec);
    std::error_code relEc;
    const std::filesystem::path rel = std::filesystem::relative(ac, rc, relEc);
    std::wstring w = (relEc ? ac : rel).wstring();
    std::replace(w.begin(), w.end(), L'\\', L'/');
    std::string utf8 = wideToUtf8(w);
    std::transform(utf8.begin(), utf8.end(), utf8.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return utf8;
}

} // namespace

// =====================================================================
// ResourceReaderImpl——IResourceReader 实现（无状态：resolver 不可变，
// 并发只读安全——§9.6 线程约束行）
// =====================================================================

namespace {

class ResourceReaderImpl final : public IResourceReader {
public:
    explicit ResourceReaderImpl(ISafePathResolverPtr resolver) : m_resolver(std::move(resolver)) {}

    // ---- open（§9.6 原文签名） ------------------------------------
    IoResult<ResourceStreamHandle> open(const std::filesystem::path& path,
                                        const ResourceOpenSpec& spec,
                                        IBudgetGuard* budget, IoCancelToken* cancel) override
    {
        IoResult<ResourceStreamHandle> out;

        // ①调用方契约检查：资源读取通道只接受 P-1/P-5/P-6（ResourceOpenSpec 注）。
        if (!isResourceRole(spec.role)) {
            out.error = internalError("open 角色非法（资源读取仅 P-1/P-5/P-6——ResourceOpenSpec 契约）");
            return out;
        }
        // ②SafePath 规范化（角色规则——§4.4②；P-1 存在性检查在此发生）。
        const IoResult<NormalizedPath> norm = normalizeForSpec(path, spec);
        if (!norm) {
            out.error = norm.error;
            return out;
        }
        if (checkCancelled(cancel, out.error)) {
            return out;
        }
        // ③④单文件大小预检＋打开（内部会话——预算挂钩的 open 侧承载）。
        BudgetScopeSession session;
        if (IoResult<void> r = session.enter(budget, BudgetScopeId{}); !r) {
            out.error = r.error;
            return out;
        }
        if (norm.value.fileSize.has_value()) {
            if (IoResult<void> r = session.charge(BudgetDimension::SingleFileBytes, *norm.value.fileSize); !r) {
                out.error = r.error;                // 比较型三要素已由 guard 携带
                return out;                         // scope 由析构兜底关闭
            }
        }
        IoResult<std::unique_ptr<ResourceStreamImpl>> opened =
            openNormalized(norm.value.native, spec.role, norm.value.equivKey);
        if (!opened) {
            out.error = opened.error;
            return out;
        }
        // 内部 scope 卫生关闭（根 scope 关闭无父回收路径——不可失败；失败
        // 即实现缺陷，如实上报不吞）。
        if (IoResult<void> r = session.leave(); !r) {
            out.error = r.error;
            return out;                             // 已打开的句柄随 opened 析构关闭
        }
        out.value = ResourceStreamHandle::adopt(std::move(opened.value));
        return out;
    }

    // ---- snapshot（§9.6＋等价调整 1：budgetScope 尾参） -------------
    IoResult<ResourceSnapshot> snapshot(const std::filesystem::path& path,
                                        const ResourceOpenSpec& spec,
                                        IBudgetGuard* budget, IoCancelToken* cancel,
                                        BudgetScopeId budgetScope) override
    {
        IoResult<ResourceSnapshot> out;

        if (!isResourceRole(spec.role)) {
            out.error = internalError("snapshot 角色非法（资源读取仅 P-1/P-5/P-6）");
            return out;
        }
        // ②规范化（P-1 存在性→IO-RES-NOT-FOUND——§4.2.5 分类一）。
        const IoResult<NormalizedPath> norm = normalizeForSpec(path, spec);
        if (!norm) {
            out.error = norm.error;
            return out;
        }
        // 预算会话（等价调整 1：调用方 scope 或自管——§9.3 CSV 同款）。
        BudgetScopeSession session;
        if (IoResult<void> r = session.enter(budget, budgetScope); !r) {
            out.error = r.error;
            return out;
        }
        if (checkCancelled(cancel, out.error)) {
            return out;
        }
        // ③SingleFileBytes 预检（stat 大小——先拒超限再打开，§4.4"③在④前"）。
        if (norm.value.fileSize.has_value()) {
            if (IoResult<void> r = session.charge(BudgetDimension::SingleFileBytes, *norm.value.fileSize); !r) {
                out.error = r.error;
                return out;
            }
        }
        // ④打开（reparse/final-path 复核随角色——SP-4/SP-6）。内部以原始
        // 句柄驱动读取循环（读头/回卷/元数据直调）；open() 出口才装配
        // ResourceStreamHandle（公共句柄对象）。
        IoResult<std::unique_ptr<ResourceStreamImpl>> opened =
            openNormalized(norm.value.native, spec.role, norm.value.equivKey);
        if (!opened) {
            out.error = opened.error;
            return out;
        }
        ResourceStreamImpl* impl = opened.value.get();

        // 格式识别（句柄上读头——避免 TOCTOU 二次打开；回卷后读体）。
        // 识别失败（FormatMeshUnknown）不阻断快照：识别面是网格规模护栏的
        // 判定源（§6.3），非网格字节资源（纹理等按字节处理——§6.3"大小预
        // 算＋摘要"）不适用网格预算，字节照常受路径/大小/总量预算管辖；
        // 格式族门禁归消费通道（modeling 消费树/网格时凭 identify 判定）。
        IoResult<ResourceKind> kind = identifyOpened(impl->handle, impl->finalPath, impl->sizeBytes);
        if (!kind) {
            if (kind.error.code != IoErrorCode::FormatMeshUnknown) {
                out.error = kind.error;                 // 读面环境错误如实上抛
                return out;
            }
            kind.value = ResourceKind::Unknown;         // 非网格字节——无网格护栏
        }

        // 网格读前预检（§6.3：二进制 STL 头三角数——V16"谎报即拒，不读体"；
        // 触发时机①读前）。仅二进制 STL 有头声明；ASCII/OBJ 计数在读中。
        if (kind.value == ResourceKind::BinaryStl && impl->sizeBytes >= 84) {
            std::uint8_t head[84] = {};
            IoResult<std::size_t> hr = rawRead(impl->handle, impl->finalPath, head, sizeof(head));
            if (!hr || hr.value != sizeof(head)) {
                out.error = hr ? makeResourceOsError(ERROR_READ_FAULT, false, impl->finalPath,
                                                     "STL 头读取不足 84 字节")
                               : hr.error;
                return out;
            }
            rewindHandle(impl->handle);
            std::uint32_t declared = 0;
            std::memcpy(&declared, head + 80, 4);
            // 读前①：声明面/顶点数 vs 网格限额（IO-SEC-BUDGET-MESH——比较型）。
            if (IoError meshErr = checkMeshBudgets(session, declared, declared * 3ull,
                                                   impl->finalPath, "读前（声明面预检）");
                meshErr.code != IoErrorCode::Ok) {
                out.error = std::move(meshErr);
                return out;
            }
            // 读前②：声明跨度 vs SingleFileBytes（声明与实际双重计数，以较大
            // 者入账——§4.5.2；③已入实际 stat，此处补声明超出部分）。
            const std::uint64_t declaredSpan = 84ull + 50ull * declared;
            if (declaredSpan > impl->sizeBytes) {
                if (IoResult<void> r = session.charge(BudgetDimension::SingleFileBytes,
                                                      declaredSpan - impl->sizeBytes); !r) {
                    out.error = r.error;
                    return out;
                }
            }
            // 读前③：实际大小反推真实面数 vs 网格限额（谎报偏小的复核——
            // 打开后 stat 已知，先于读体拒绝；§6.3"以实际计数触发预算"）。
            const std::uint64_t impliedFaces =
                impl->sizeBytes >= 84 ? (impl->sizeBytes - 84ull) / 50ull : 0;
            if (IoError meshErr = checkMeshBudgets(session, impliedFaces, impliedFaces * 3ull,
                                                   impl->finalPath, "读中（实际大小复核）");
                meshErr.code != IoErrorCode::Ok) {
                out.error = std::move(meshErr);
                return out;
            }
        }

        // ⑤⑥流式读取＋摘要（每块：取消/TotalBytes 合并检查点——§4.4⑤"一次
        // 分支两查"；网格流式计数超限读中中止——V16"真实超限"触发面）。
        MeshStreamCounter counter;
        counter.begin(kind.value);
        core::ContentDigester digester;
        std::uint64_t totalRead = 0;
        std::vector<std::uint8_t> block(kResourceReadBlockBytes);
        for (;;) {
            if (checkCancelled(cancel, out.error)) {
                return out;
            }
            IoResult<std::size_t> r = rawRead(impl->handle, impl->finalPath, block.data(),
                                              block.size());
            if (!r) {
                out.error = r.error;
                return out;
            }
            if (r.value == 0) {
                break;                              // EOF
            }
            counter.feed(block.data(), r.value);
            if (IoError meshErr = checkMeshBudgets(session, counter.faces(), counter.vertices(),
                                                   impl->finalPath, "读中（流式计数）");
                meshErr.code != IoErrorCode::Ok) {
                out.error = std::move(meshErr);
                return out;
            }
            if (IoResult<void> c = session.charge(BudgetDimension::TotalBytes, r.value); !c) {
                out.error = c.error;
                return out;
            }
            digester.update(block.data(), r.value); // ⑥SHA-256（SA-12 唯一算法）
            totalRead += r.value;
        }

        // ⑦快照产出（finalPath＝实体路径；mtime＝打开句柄 FILETIME 预筛；
        // digest 权威——§8.2 四字段；V27 确定性：同字节序列同摘要）。
        if (IoResult<void> r = session.leave(); !r) {
            out.error = r.error;                    // 内部根 scope 关闭不可失败——防御
            return out;
        }
        out.value.finalPath = impl->finalPath;
        out.value.sizeBytes = totalRead;
        out.value.mtimeUtc = impl->mtimeUtc;
        out.value.contentDigest = digester.finalize();
        return out;
    }

    // ---- identify（§9.6：const；扩展名＋魔数；轻量——无预算/取消点） ----
    IoResult<ResourceKind> identify(const std::filesystem::path& path) const override
    {
        IoResult<ResourceKind> out;
        // 存在性预检（不可达→IO-RES-\* 四分类——identify 的错误面同 §9.6
        // 错误类型行）。
        std::error_code ec;
        const std::filesystem::file_status st = std::filesystem::status(path, ec);
        if (ec || !std::filesystem::exists(st)) {
            out.error.code = IoErrorCode::ResNotFound;
            out.error.params.emplace_back("path", displayOf(path));
            out.error.params.emplace_back("direction", "read");
            out.error.detail = "identify 目标不可达（§4.2.5 分类一）";
            return out;
        }
        const std::string ext = lowerExtension(path);
        if (ext == ".stl" || ext == ".xml") {
            // 魔数嗅探族：STL（二进制/ASCII 判定）与 .xml（COLLADA 根嗅探）。
            std::uint8_t head[84] = {};
            std::size_t headLen = 0;
            std::uint64_t fileSize = 0;
            if (IoError e = sniffHead(path, head, sizeof(head), &headLen, &fileSize);
                e.code != IoErrorCode::Ok) {
                out.error = std::move(e);
                return out;
            }
            if (ext == ".stl") {
                out.value = classifyStl(head, headLen, fileSize);
            } else {
                out.value = (headLen >= 8 && std::memcmp(head, "<COLLADA", 8) == 0)
                                ? ResourceKind::ColladaDae
                                : ResourceKind::GenericXml;   // R2 文件层形态（§6.4）
            }
            if (out.value == ResourceKind::Unknown) {
                out.error = makeMeshUnknown(path, head, headLen);
            }
            return out;
        }
        // 纯扩展名族。
        const ResourceKind byExt = kindByExtension(path);
        if (byExt != ResourceKind::Unknown) {
            out.value = byExt;
            return out;
        }
        // 识别失败：读首 16 字节做 hex 摘要（§6.3"附首 16 字节十六进制摘要，
        // 脱敏无虞"）。
        std::uint8_t head[16] = {};
        std::size_t headLen = 0;
        std::uint64_t fileSize = 0;
        if (IoError e = sniffHead(path, head, sizeof(head), &headLen, &fileSize);
            e.code != IoErrorCode::Ok) {
            out.error = std::move(e);
            return out;
        }
        out.error = makeMeshUnknown(path, head, headLen);
        return out;
    }

    // ---- dependencyTree（§6.5/§6.2——expat 边抽取＋环/深度/缺失检测） ----
    IoResult<ResourceDependencyTree> dependencyTree(const std::filesystem::path& importRoot,
                                                    IBudgetGuard* budget, IoCancelToken* cancel,
                                                    IoProgressCallback progress,
                                                    BudgetScopeId budgetScope) override
    {
        IoResult<ResourceDependencyTree> out;

        // ②入口文档规范化（P-1——用户显式选择的 .urdf/.xacro/.xml，§6.2 读取入口）。
        ResourceOpenSpec entrySpec;
        entrySpec.role = PathRole::UserSource;
        const IoResult<NormalizedPath> entry = normalizeForSpec(importRoot, entrySpec);
        if (!entry) {
            out.error = entry.error;
            return out;
        }
        // 管辖根＝入口文档父目录（等价调整 2——§6.2"以用户选择的文件所在
        // 目录为基解析"）。canonical 化＋折叠（比较用）。
        const std::filesystem::path entryNative(entry.value.native);
        const std::filesystem::path rootPath =
            std::filesystem::path(weaklyCanonicalWide(entryNative)).parent_path();
        std::wstring rootFolded = foldForCompare(rootPath.wstring());
        normalizeSeparators(&rootFolded);
        while (rootFolded.size() > 3 && rootFolded.back() == L'\\') {
            rootFolded.pop_back();                      // 尾分隔符归一（保留 "X:\" 根形态）
        }

        // 预算会话（等价调整 1）。
        BudgetScopeSession session;
        if (IoResult<void> r = session.enter(budget, budgetScope); !r) {
            out.error = r.error;
            return out;
        }

        // 遍历状态：nodes（折叠相对键→节点；在表＝已完成）；onStack（进行中
        // ——回边指向进行中节点＝环）；stackFolded（环路径清单窗口）；缺失
        // 清单汇总（§6.5）。显式栈迭代 DFS：节点完成以"出口标记"出栈表达
        // ——保证"进行中"窗口覆盖整棵子树（A→B→A 的回边在 A 未完成时命中
        // onStack→环；而 B→D、C→D 的菱形共享在 D 完成后按已处理跳过——
        // 环与 DAG 共享的正确分界）。
        std::map<std::string, ResourceNode> nodes;
        std::set<std::pair<std::string, std::string>> edgeKeys;
        std::vector<ResourceEdge> edges;
        std::vector<std::string> missing;
        std::vector<std::wstring> stackFolded;
        std::set<std::wstring> onStack;
        std::uint64_t processed = 0;
        std::optional<IoError> failure;                 // 首个致命失败（环/深度/语法/预算/取消）

        /// DFS 工作项（显式栈——深度预算先比后展开，防深链栈溢出；
        /// isExit＝出口标记：子树全部完成时出栈撤销"进行中"）。
        struct WorkItem {
            std::wstring foldedPath;                    ///< 折叠绝对路径（环/去重键）
            std::string relKey;                         ///< 管辖根相对键（节点 id）
            int depth;                                  ///< include 链深度（入口＝1）
            bool isExit;                                ///< true＝出口标记（非工作）
        };

        const std::string entryRel = relativeKeyOf(rootPath, entryNative);
        std::deque<WorkItem> work;
        work.push_back(WorkItem{foldForCompare(weaklyCanonicalWide(entryNative)), entryRel, 1, false});

        while (!work.empty() && !failure.has_value()) {
            // 取消检查点（每工作项——§9.6 取消行为行；经 failure 单出口走清理）。
            if (checkCancelled(cancel, out.error)) {
                failure = out.error;
                out.error = IoError{};
                break;
            }
            const WorkItem item = work.back();
            work.pop_back();

            if (item.isExit) {
                // 出口标记：子树完成——撤销"进行中"（环窗口收窄一层）。
                onStack.erase(item.foldedPath);
                if (!stackFolded.empty() && stackFolded.back() == item.foldedPath) {
                    stackFolded.pop_back();
                }
                continue;
            }

            // 环检测（§6.2：include 图有环→IO-FORMAT-XML-CYCLE＋环路径清单
            // ——回边指向**进行中**节点；params cycle＝清单，V15 观测点）。
            if (onStack.count(item.foldedPath) != 0) {
                IoError e;
                e.code = IoErrorCode::FormatXmlCycle;
                e.params.emplace_back("cycle", buildCycleList(stackFolded, item.foldedPath, nodes));
                e.detail = "include 图检测到环（§6.2——循环与深度双保险；清单为环路径序列）";
                failure = std::move(e);
                break;
            }
            // DAG 共享（已完成节点——非环，跳过；§6.2 环判据是回边指向进行中）。
            if (nodes.count(item.relKey) != 0) {
                continue;
            }
            // FileCount 入账（每唯一文件一次——§4.5.1 文件数维）。
            if (IoResult<void> r = session.charge(BudgetDimension::FileCount, 1); !r) {
                failure = r.error;
                break;
            }
            // 深度预算（§4.5.1 IncludeDepth：入口=1；超限 IO-SEC-BUDGET-INCLUDE
            // 比较型三要素——V15"17 层深度链"触发面：默认 16）。
            const std::uint64_t depthLimit = session.limitOf(BudgetDimension::IncludeDepth);
            if (depthLimit != 0 && static_cast<std::uint64_t>(item.depth) > depthLimit) {
                failure = makeComparativeError(IoErrorCode::SecBudgetInclude,
                                               static_cast<std::uint64_t>(item.depth), depthLimit,
                                               "levels",
                                               "include 链深度超限（§6.2——循环与深度双保险）");
                break;
            }
            // 每节点进度（done＝已处理节点数；total=0 未知——§9.0 IoProgress）。
            if (progress) {
                const IoProgress p{processed, 0, "resource-dependency-tree"};
                progress(p);
            }
            ++processed;

            // 读取当前文件（快照通道：预算＋摘要＋四分类错误面；传入会话
            // scope——树内 TotalBytes/FileCount 账目汇入同一 ledger）。
            const std::filesystem::path absPath = absoluteOf(item.foldedPath);
            ResourceOpenSpec fileSpec;
            fileSpec.role = PathRole::UserSource;
            IoResult<ResourceSnapshot> snap = snapshot(absPath, fileSpec, session.guard(),
                                                       cancel, session.scope());
            ResourceNode node;
            node.relPath = item.relKey;
            if (snap) {
                node.exists = true;
                node.snapshot = snap.value;
            } else if (snap.error.code == IoErrorCode::ResNotFound) {
                // 缺失叶（§6.5"节点缺席"）：入树（exists=false）＋缺失清单，
                // 继续遍历——全图汇总后 IO-RES-MISSING（V26 缺失清单语义；
                // 资源事实——§2.5 正交，无任何工程结论）。
                node.exists = false;
                missing.push_back(item.relKey);
            } else {
                // 其他错误（权限/预算/取消/语法）即时失败——环境错误与缺失
                // 事实分轨（§2.5 正交）。
                failure = snap.error;
                break;
            }
            nodes.emplace(item.relKey, std::move(node));
            onStack.insert(item.foldedPath);
            stackFolded.push_back(item.foldedPath);
            // 先压出口标记（LIFO——全部子树处理完才出）。
            work.push_back(WorkItem{item.foldedPath, item.relKey, item.depth, true});

            // 边抽取（按扩展名分流：XML 族经 expat；OBJ/MTL 文本行）。
            if (nodes.at(item.relKey).exists) {
                const std::string ext = lowerExtension(absPath);
                const bool xmlFamily = (ext == ".xacro" || ext == ".urdf" || ext == ".xml");
                if (xmlFamily || ext == ".obj" || ext == ".mtl") {
                    // 全量字节读入（受同一 scope TotalBytes 约束——快照接口
                    // 不保留字节，边抽取需内容）。
                    std::vector<std::uint8_t> bytes;
                    if (IoResult<std::vector<std::uint8_t>> r =
                            readAllBytes(absPath, session, cancel);
                        r) {
                        bytes = std::move(r.value);
                    } else {
                        failure = r.error;
                        break;
                    }
                    std::vector<std::pair<std::string, ResourceEdgeKind>> refs;
                    if (xmlFamily) {
                        const XmlDeps deps = parseXmlDeps(bytes.data(), bytes.size());
                        if (deps.parseError) {
                            // 良构性违例（IO-FORMAT-XML-SYNTAX——表尾追加码
                            // v0.8；定位行列进 params——§6.1 XML 良构检查）。
                            IoError e;
                            e.code = IoErrorCode::FormatXmlSyntax;
                            e.params.emplace_back("path", item.relKey);
                            e.params.emplace_back("row", std::to_string(deps.errorLine));
                            e.params.emplace_back("column", std::to_string(deps.errorColumn));
                            e.detail = deps.errorMessage;
                            failure = std::move(e);
                            break;
                        }
                        for (const XmlDeps::Ref& ref : deps.refs) {
                            refs.emplace_back(ref.target, ref.kind);
                        }
                    } else {
                        const ResourceKind tk =
                            (ext == ".obj") ? ResourceKind::WavefrontObj : ResourceKind::MaterialLib;
                        for (auto& r : parseTextDeps(bytes.data(), bytes.size(), tk).refs) {
                            refs.emplace_back(std::move(r));
                        }
                    }

                    // 引用解析（相对**所在文件目录**；必须落在管辖根内——
                    // §6.2"include 目标必须落在导入根内"）。
                    for (const auto& ref : refs) {
                        const std::wstring wideTarget = utf8ToWide(ref.first);
                        if (wideTarget.empty()) {
                            failure = internalError("引用目标 UTF-8 非法（依赖树边抽取）");
                            break;
                        }
                        const std::filesystem::path joined =
                            absPath.parent_path() / std::filesystem::path(wideTarget);
                        std::wstring joinedNorm = foldForCompare(weaklyCanonicalWide(joined));
                        normalizeSeparators(&joinedNorm);
                        if (!isInsideRoot(rootFolded, joinedNorm)) {
                            IoError e;
                            e.code = IoErrorCode::SecPathEscape;
                            e.params.emplace_back("role", "dependency-include");
                            e.params.emplace_back("path", wideToUtf8(stripExtendedPrefix(joinedNorm)));
                            e.detail = "引用目标逃逸管辖根（§6.2——导入根即临时管辖根）";
                            failure = std::move(e);
                            break;
                        }
                        const std::string childRel = relativeKeyOf(rootPath, joined);
                        ResourceEdge edge;
                        edge.fromRel = item.relKey;
                        edge.toRel = childRel;
                        edge.kind = ref.second;
                        if (edgeKeys.insert({edge.fromRel, edge.toRel}).second) {
                            edges.push_back(std::move(edge));
                        }
                        work.push_back(WorkItem{joinedNorm, childRel, item.depth + 1, false});
                    }
                    if (failure.has_value()) {
                        break;
                    }
                }
            }
            // "进行中"撤销由出口标记完成（子树全完成时出栈）——本处不再退栈。
        }

        // 单出口：会话关闭（内部 scope 兜底语义见 BudgetScopeSession 析构注），
        // 再翻译失败/缺失/成功三态。
        IoResult<void> left = session.leave();
        if (failure.has_value()) {
            out.error = std::move(*failure);
            return out;
        }
        if (!left) {
            out.error = left.error;                 // 根 scope 关闭不可失败——防御
            return out;
        }
        // 缺失汇总（§6.5"节点缺席 → IO-RES-MISSING（缺失清单）"——全图遍历
        // 完成后一次失败上报；清单在 detail，计数/首项在 params）。
        if (!missing.empty()) {
            IoError e;
            e.code = IoErrorCode::ResMissing;
            e.params.emplace_back("missing-count", std::to_string(missing.size()));
            e.params.emplace_back("path", missing.front());
            std::string list;
            for (const std::string& m : missing) {
                list += m + "\n";
            }
            e.detail = "依赖树缺失清单（资源事实——§2.5 正交；不产业务结论）:\n" + list;
            out.error = std::move(e);
            return out;
        }

        // 树产出（稳定序：节点按折叠相对键字典序；边按 (from,to,kind) 字典序
        // ——§9.6 确定性行"依赖树遍历序＝字典序稳定"）。
        out.value.rootRel = entryRel;
        for (auto& kv : nodes) {
            out.value.nodes.push_back(std::move(kv.second));
        }
        std::sort(edges.begin(), edges.end(), [](const ResourceEdge& a, const ResourceEdge& b) {
            if (a.fromRel != b.fromRel) {
                return a.fromRel < b.fromRel;
            }
            if (a.toRel != b.toRel) {
                return a.toRel < b.toRel;
            }
            return static_cast<int>(a.kind) < static_cast<int>(b.kind);
        });
        out.value.edges = std::move(edges);
        return out;
    }

private:
    /// 资源读取通道合法角色（P-1/P-5/P-6——ResourceOpenSpec 契约）。
    static bool isResourceRole(PathRole role)
    {
        return role == PathRole::UserSource || role == PathRole::ProjectResourceRef
               || role == PathRole::StagingTmp;
    }

    /// 规范化入口（ResourceOpenSpec → resolver 调用形态——base 匹配契约
    /// 由 resolver 核检查：P-5/P-6 必须带 base，P-1 必须为空——§9.1）。
    IoResult<NormalizedPath> normalizeForSpec(const std::filesystem::path& path,
                                              const ResourceOpenSpec& spec) const
    {
        return m_resolver->normalize(spec.role, path.wstring(), spec.base);
    }

    /**
     * 打开＋复核公共核（§4.4④）：P-5/P-6 带 FILE_FLAG_OPEN_REPARSE_POINT
     * （拒绝打开 reparse point 本身——§4.2.3 TOCTOU 纵深）＋final-path
     * 复核（SP-6）；P-1 跟随链接（快照按实体路径——GetFinalPathNameBy-
     * HandleW）。共享模式 READ|WRITE——变化检测语义前提（文件头注）。
     */
    static IoResult<std::unique_ptr<ResourceStreamImpl>>
        openNormalized(const std::wstring& native, PathRole role, const std::string& equivKey)
    {
        IoResult<std::unique_ptr<ResourceStreamImpl>> out;
        DWORD flags = FILE_ATTRIBUTE_NORMAL;
        const bool guarded = (role == PathRole::ProjectResourceRef || role == PathRole::StagingTmp);
        if (guarded) {
            flags |= FILE_FLAG_OPEN_REPARSE_POINT;      // §4.2.3：拒绝打开 reparse point 本身
        }
        HANDLE h = ::CreateFileW(native.c_str(), GENERIC_READ,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                 nullptr, OPEN_EXISTING, flags, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            const DWORD err = ::GetLastError();
            out.error = makeResourceOsError(err, false, std::filesystem::path(native),
                                            "CreateFileW 失败（资源打开——§4.4④）");
            return out;
        }
        auto impl = std::make_unique<ResourceStreamImpl>();
        impl->handle = h;
        queryHandleMetadata(h, &impl->sizeBytes, &impl->mtimeUtc);

        // reparse 复核（guarded 角色：带标志打开后属性仍含 REPARSE_POINT＝
        // 目标本身是链接——§4.2.3 P-5/P-6 一律拒绝，不区分目标是否在区内）。
        if (guarded) {
            BY_HANDLE_FILE_INFORMATION info{};
            if (::GetFileInformationByHandle(h, &info)
                && (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
                out.error = makeResourceOsError(0, false, std::filesystem::path(native),
                                                "目标为 reparse point（§4.2.3——一律拒绝）");
                out.error.code = IoErrorCode::SecPathSymlink;
                out.error.params.clear();
                out.error.params.emplace_back("path", displayOf(std::filesystem::path(native)));
                return out;
            }
        }

        // final-path 复核（SP-6——TOCTOU 纵深）：P-5/P-6 折叠终路径必须与
        // 规范化等价键一致（比较前归一 \\?\ 前缀/分隔符/大小写——
        // GetFinalPathNameByHandleW 的 VOLUME_NAME_DOS 形态带 \\?\ 前缀，
        // 而等价键基于 weakly_canonical 词法形态，两者字面不必同形）；
        // P-1 记录实体路径（跟随后的最终形态——§4.2.3）。
        const std::wstring finalWide = queryFinalPath(h);
        if (finalWide.empty()) {
            // 终路径不可查询（网络卷形态等）——P-1 降级为规范化路径记录；
            // 受管辖角色复核不可得＝不可证明在区内，拒绝放行。
            if (guarded) {
                out.error.code = IoErrorCode::SecPathEscape;
                out.error.params.emplace_back("path", displayOf(std::filesystem::path(native)));
                out.error.detail = "final-path 复核不可得（SP-6——拒绝放行）";
                return out;
            }
            impl->finalPath = std::filesystem::path(stripExtendedPrefix(native));
        } else {
            if (guarded) {
                // 比较面归一：去 \\?\ 前缀＋分隔符归一＋折叠（§4.2.4 同源口径）。
                std::wstring expect = stripExtendedPrefix(utf8ToWide(equivKey));
                normalizeSeparators(&expect);
                expect = foldForCompare(expect);
                std::wstring actual = stripExtendedPrefix(foldForCompare(finalWide));
                normalizeSeparators(&actual);
                if (actual != expect) {
                    out.error.code = IoErrorCode::SecPathEscape;
                    out.error.params.emplace_back("path", wideToUtf8(stripExtendedPrefix(actual)));
                    out.error.detail = "final-path 与规范化结果不一致（SP-6——TOCTOU 纵深拒绝）";
                    return out;
                }
            }
            impl->finalPath = std::filesystem::path(finalWide);
        }
        out.value = std::move(impl);                    // 移交实现承载（调用方 adopt 装配句柄）
        return out;
    }

    // ---- 句柄工具（读块/回卷/元数据——openNormalized 与读取循环共用） ----

    static void queryHandleMetadata(HANDLE h, std::uint64_t* sizeOut, std::uint64_t* mtimeOut)
    {
        FILE_STANDARD_INFO stdInfo{};
        FILE_BASIC_INFO basicInfo{};
        if (sizeOut != nullptr) {
            *sizeOut = (::GetFileInformationByHandleEx(h, FileStandardInfo, &stdInfo, sizeof(stdInfo)))
                           ? static_cast<std::uint64_t>(stdInfo.EndOfFile.QuadPart)
                           : 0;
        }
        if (mtimeOut != nullptr) {
            *mtimeOut = (::GetFileInformationByHandleEx(h, FileBasicInfo, &basicInfo, sizeof(basicInfo)))
                            ? static_cast<std::uint64_t>(basicInfo.LastWriteTime.QuadPart)
                            : 0;
        }
    }

    /// GetFinalPathNameByHandleW：实体最终路径（VOLUME_NAME_DOS；失败返回空）。
    static std::wstring queryFinalPath(HANDLE h)
    {
        std::wstring buf(1024, L'\0');
        for (;;) {
            const DWORD n = ::GetFinalPathNameByHandleW(h, buf.data(), static_cast<DWORD>(buf.size()),
                                                        FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
            if (n == 0) {
                return {};                              // 句柄不可查询（网络卷等）——调用方降级
            }
            if (n < buf.size()) {
                buf.resize(n);
                return buf;                             // VOLUME_NAME_DOS 形态（无 \\?\ 前缀）
            }
            buf.resize(n);                              // 缓冲不足——按需扩容重试
        }
    }

    /// 原始句柄单块读取（四分类错误映射——读取循环/读头/识别共用）。
    static IoResult<std::size_t> rawRead(HANDLE h, const std::filesystem::path& path, void* dst,
                                         std::size_t maxBytes)
    {
        IoResult<std::size_t> out;
        DWORD got = 0;
        if (!::ReadFile(h, dst, static_cast<DWORD>(maxBytes), &got, nullptr)) {
            const DWORD err = ::GetLastError();
            out.error = makeResourceOsError(err, false, path, "ReadFile 失败（§4.4⑤ 读取循环）");
            return out;
        }
        out.value = got;                                // 0＝EOF
        return out;
    }

    /// 句柄回卷（读头/识别后零偏移——快照摘要必须覆盖全文件）。
    static void rewindHandle(HANDLE h)
    {
        LARGE_INTEGER zero{};
        zero.QuadPart = 0;
        ::SetFilePointerEx(h, zero, nullptr, FILE_BEGIN);
    }

    /**
     * 已打开句柄上的识别（快照通道内部用——句柄读头避免 TOCTOU 二次打开；
     * 读毕回卷，读体从 0 开始）。
     */
    static IoResult<ResourceKind> identifyOpened(HANDLE h, const std::filesystem::path& path,
                                                 std::uint64_t fileSize)
    {
        IoResult<ResourceKind> out;
        const std::string ext = lowerExtension(path);
        if (ext == ".stl" || ext == ".xml") {
            std::uint8_t head[84] = {};
            IoResult<std::size_t> hr = rawRead(h, path, head, sizeof(head));
            if (!hr) {
                out.error = hr.error;
                return out;
            }
            rewindHandle(h);
            const std::size_t got = hr.value;
            if (ext == ".stl") {
                out.value = classifyStl(head, got, fileSize);
            } else {
                out.value = (got >= 8 && std::memcmp(head, "<COLLADA", 8) == 0)
                                ? ResourceKind::ColladaDae
                                : ResourceKind::GenericXml;
            }
            if (out.value == ResourceKind::Unknown) {
                out.error = makeMeshUnknown(path, head, got);
            }
            return out;
        }
        const ResourceKind byExt = kindByExtension(path);
        if (byExt != ResourceKind::Unknown) {
            out.value = byExt;
            return out;
        }
        // 识别失败：句柄读首 16 字节做 hex（§6.3）。
        std::uint8_t head[16] = {};
        IoResult<std::size_t> hr = rawRead(h, path, head, sizeof(head));
        rewindHandle(h);
        out.error = makeMeshUnknown(path, head, hr ? hr.value : 0);
        return out;
    }

    /// 文件首部嗅探（公共 identify 用——独立打开；失败映射四分类）。
    static IoError sniffHead(const std::filesystem::path& path, std::uint8_t* head,
                             std::size_t headCap, std::size_t* headLen, std::uint64_t* fileSize)
    {
        HANDLE h = ::CreateFileW(path.wstring().c_str(), GENERIC_READ,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                 nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            const DWORD err = ::GetLastError();
            return makeResourceOsError(err, false, path, "嗅探打开失败");
        }
        queryHandleMetadata(h, fileSize, nullptr);
        DWORD got = 0;
        ::ReadFile(h, head, static_cast<DWORD>(headCap), &got, nullptr);
        *headLen = static_cast<std::size_t>(got);
        ::CloseHandle(h);
        return IoError{};                               // 头部数据经出参返回
    }

    /// 网格限额比较（§4.5.1 MeshFaceCount/MeshVertexCount——任一超限即
    /// IO-SEC-BUDGET-MESH 比较型三要素；faces 与 vertices 分别比对；
    /// limit=0 表示 scope 未启用该维判定——不过）。
    static IoError checkMeshBudgets(const BudgetScopeSession& session, std::uint64_t faces,
                                    std::uint64_t vertices, const std::filesystem::path& path,
                                    const char* phase)
    {
        const std::uint64_t faceLimit = session.limitOf(BudgetDimension::MeshFaceCount);
        if (faceLimit != 0 && faces > faceLimit) {
            IoError e = makeComparativeError(IoErrorCode::SecBudgetMesh, faces, faceLimit, "count",
                                             "网格面数超限（" + std::string(phase) + "——§6.3 规模预检）");
            e.params.emplace_back("path", displayOf(path));
            return e;
        }
        const std::uint64_t vertexLimit = session.limitOf(BudgetDimension::MeshVertexCount);
        if (vertexLimit != 0 && vertices > vertexLimit) {
            IoError e = makeComparativeError(IoErrorCode::SecBudgetMesh, vertices, vertexLimit, "count",
                                             "网格顶点数超限（" + std::string(phase) + "——§6.3 规模预检）");
            e.params.emplace_back("path", displayOf(path));
            return e;
        }
        return IoError{};
    }

    /// 全量读入（依赖树边抽取用——受同一 scope TotalBytes 约束）。
    static IoResult<std::vector<std::uint8_t>> readAllBytes(const std::filesystem::path& path,
                                                            BudgetScopeSession& session,
                                                            IoCancelToken* cancel)
    {
        IoResult<std::vector<std::uint8_t>> out;
        HANDLE h = ::CreateFileW(path.wstring().c_str(), GENERIC_READ,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                 nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            const DWORD err = ::GetLastError();
            out.error = makeResourceOsError(err, false, path, "依赖边抽取读入失败");
            return out;
        }
        std::vector<std::uint8_t> block(kResourceReadBlockBytes);
        for (;;) {
            if (checkCancelled(cancel, out.error)) {
                ::CloseHandle(h);
                return out;
            }
            DWORD got = 0;
            if (!::ReadFile(h, block.data(), static_cast<DWORD>(block.size()), &got, nullptr)) {
                const DWORD err = ::GetLastError();
                out.error = makeResourceOsError(err, false, path, "依赖边抽取读取失败");
                ::CloseHandle(h);
                return out;
            }
            if (got == 0) {
                break;
            }
            if (IoResult<void> c = session.charge(BudgetDimension::TotalBytes, got); !c) {
                out.error = c.error;
                ::CloseHandle(h);
                return out;
            }
            out.value.insert(out.value.end(), block.begin(), block.begin() + static_cast<std::ptrdiff_t>(got));
        }
        ::CloseHandle(h);
        return out;
    }

    // ---- 依赖树路径工具（绝对路径重建/环清单） ----------------------

    /// 由折叠绝对路径重建可达路径（Windows 大小写不敏感——折叠键可命中
    /// 真实实体，§4.2.4；快照以 weakly_canonical 复核大小写）。
    static std::filesystem::path absoluteOf(const std::wstring& foldedPath)
    {
        return std::filesystem::path(foldedPath);
    }

    /// 环路径清单（§6.2"列出环路径"——栈内首次命中点到当前点的相对键序列
    /// ＋回边闭合标注；params cycle 的载体，V15 观测点"环路径清单"）。
    static std::string buildCycleList(const std::vector<std::wstring>& stackFolded,
                                      const std::wstring& repeated,
                                      const std::map<std::string, ResourceNode>& nodes)
    {
        std::size_t start = 0;
        for (std::size_t i = 0; i < stackFolded.size(); ++i) {
            if (stackFolded[i] == repeated) {
                start = i;
                break;
            }
        }
        std::vector<std::string> rels;
        for (std::size_t i = start; i < stackFolded.size(); ++i) {
            rels.push_back(foldedToRel(stackFolded[i], nodes));
        }
        rels.push_back(foldedToRel(repeated, nodes));   // 回边（重复出现＝环闭合）
        std::string list;
        for (std::size_t i = 0; i < rels.size(); ++i) {
            if (i != 0) {
                list += " -> ";
            }
            list += rels[i];
        }
        return list;
    }

    /// 折叠绝对路径→相对键（nodes 命中用登记键；未命中按折叠文本呈现）。
    static std::string foldedToRel(const std::wstring& folded,
                                   const std::map<std::string, ResourceNode>& nodes)
    {
        for (const auto& kv : nodes) {
            if (foldForCompare(utf8ToWide(kv.first)) == folded) {
                return kv.first;
            }
        }
        return wideToUtf8(stripExtendedPrefix(folded));
    }

    /// SafePath 解析器（构造后不可变——并发只读安全）。
    ISafePathResolverPtr m_resolver;
};

} // namespace

// =====================================================================
// 工厂（公共接口——声明见 ResourceIo.hpp）
// =====================================================================

IResourceReaderPtr makeResourceReader(ISafePathResolverPtr resolver)
{
    if (!resolver) {
        resolver = makeSafePathResolver();              // 产品规则集自建（头注契约）
    }
    return std::make_shared<ResourceReaderImpl>(std::move(resolver));
}

// IResourceSnapshotter 与 IRuntimeResourceAdapter 的实现随 IO-T05 提交 2/3
// 在本文件续承载（probe/solidifyToStaging——§8.3/§8.4；§8.6 五条裁决）。

} // namespace sdurws::ird::io
