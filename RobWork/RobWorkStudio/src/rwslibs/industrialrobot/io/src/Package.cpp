/**
 * @file   Package.cpp
 * @brief  .rwpack 包导入导出 io 侧设施的真实实现——导入九步协议
 *         （§7.3：①选择→②临时区→③版本与路径预检〔展开前〕→④展开＋
 *         预算/磁盘→⑤逐文件哈希＋镜像结构→⑥引用完整性→⑦报告＋目标
 *         预检〔⑧发布归 project——本文件无 rename〕→⑨清理）与导出六
 *         步协议（§7.2：一致视图→逐文件暂存→canonical 元数据→压缩→
 *         完整性自检→原子替换）。
 *
 * 设计依据：
 *   - units/io.md §7.1~§7.8（包协议全链）、§9.9（接口契约表）、§4.3.1
 *     （SP-2/SP-4/SP-8/SP-9 包条目规则）、§4.5.2（包通道预算：四维强制、
 *     声明与实际双重计数、饱和算法）、§5.9.3（canonical JSON——manifest
 *     与 rwpack.json 的写出/复检口径）、§9.13（线程与取消总则）、§7.4
 *     （威胁处置矩阵——本文件各步骤的码面即矩阵列）
 *   - 需求 PM-05、NFR-SEC-01/02、SEL-02（文件层支撑）、UX-03（取消非
 *     错误）、AT-20
 *   - 任务契约 tasks/foundation/IO-T06.json acceptance 1~5
 *
 * 实现边界说明（责任切分——§7.7/acceptance 4 的执行面）：
 *   - **io 侧 rename 发布不存在**：本文件没有任何把 payload 树落到
 *     targetDir 的代码路径——targetDir 只被"存在性预检"（⑧前 io 侧，
 *     §7.4 目标目录已存在行）读取；发布＝project 对 payloadRoot() 的
 *     同卷 rename（N-7）。"失败不留目标目录"（V22）由此是结构性质：
 *     失败路径的全部写入只发生在隐藏前缀临时区与原子写出的暂存位。
 *   - **不读 .staging**：包导入的全部内容来自 ZIP 容器；导出的全部内
 *     容来自 ISnapshotFileSource 快照——两条通道都不触达 .rwdesign 内
 *     部条目（§2.3 非目标 6）。
 *   - **目录包（selection CSV 目录包）不在此**（P-IO-7）：§7.8 的文件
 *     名/清单 schema 由 selection 卡注册（阶段 C 接入，§13）——本文件
 *     只承载 .rwpack 协议，不预写 selection 字段字典。
 *   - libzip 写侧消费（DTB §5.4 登记，io.md §15.5 v0.9）：导出压缩侧
 *     经 libzip（P-IO-3 冻结选型）——读侧消费点仍是 ZipChannel.cpp
 *     （§3.3 登记行），本文件新增写侧消费点；libzip 类型不出公共面。
 *     归档打开/创建沿用 ZipChannel.cpp 的 FILE* 宽路径口径（Windows 侧
 *     _wfopen——全名空间唯一无损入口；与 IO-T04 登记形态一致）。
 *   - 展开读取经 IZipChannel::readEntryBytes 且传 budgetScope=0（通道
 *     自开内部 scope——逐条目隔离）：会话级累计记账（ArchiveExpanded/
 *     Ratio/Total/Temp/Count）由本文件的会话 scope 承载；两层互为纵深
 *     （内部 scope＝产品默认硬顶、条目级；会话 scope＝调用方收紧规格、
 *     会话级——§4.5.2"声明与实际双重计数以较大者入账"的分账落点）。
 *
 * 线程约束：会话单线程（§9.9）；exporter 无状态。io 不创建线程。
 */

#include <sdurws/ird/io/Package.hpp>

#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/io/Budget.hpp>
#include <sdurws/ird/io/IoDiagnostics.hpp>
#include <sdurws/ird/io/IoError.hpp>
#include <sdurws/ird/io/IoFwd.hpp>
#include <sdurws/ird/io/Json.hpp>
#include <sdurws/ird/io/SafePath.hpp>
#include <sdurws/ird/io/ZipChannel.hpp>

#include <zip.h>

#include "IoPlatform.hpp"   // displayOf/utf8ToWide/wideToUtf8/makeOsError/randomHex8

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>   // CreateFileW/WriteFile——展开落盘的错误码保真写面

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace sdurws::ird::io {

// =====================================================================
// 内部助手（匿名命名空间——不出翻译单元）
// =====================================================================

namespace {

/// 包过程/格式错误的统一构造（码＋params＋detail；参数序＝构造序——
/// IoError 确定性要求）。
IoError packError(IoErrorCode code, std::string detail,
                  std::vector<std::pair<std::string, std::string>> params = {})
{
    IoError e;
    e.code = code;
    e.params = std::move(params);
    e.detail = std::move(detail);
    return e;
}

/// IoResult 失败快写（错误轨道组装的语法糖——保持主流程缩进平坦）。
template <typename T>
IoResult<T> fail(IoError error)
{
    IoResult<T> out;
    out.error = std::move(error);
    return out;
}

/// 取消检查点：命中即返回 true（调用方立刻转取消路径——UX-03 状态非
/// 错误，不产诊断）。检查点密度＝§7.3 步注"每条目/每文件"。
bool cancelHit(IoCancelToken* cancel)
{
    return cancel != nullptr && cancel->isCancelled();
}

/// 64 位小写十六进制 → 32 字节摘要（manifest sha256/totalDigest 解析；
/// 非 64 位或含非小写 hex 字符＝false——格式契约面只接受 canonical 小写
/// 形式，稳定拒绝不猜测）。
bool parseHexDigest(const std::string& hex, core::Digest256* out)
{
    if (hex.size() != 64) {
        return false;
    }
    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') {
            return c - '0';
        }
        if (c >= 'a' && c <= 'f') {
            return c - 'a' + 10;
        }
        return -1;   // 大写/非法字符——非 canonical 形式
    };
    for (std::size_t i = 0; i < 32; ++i) {
        const int hi = nibble(hex[i * 2]);
        const int lo = nibble(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) {
            return false;
        }
        (*out)[i] = static_cast<std::uint8_t>((hi << 4) | lo);
    }
    return true;
}

/// 32 字节摘要 → 小写十六进制（导出 manifest/totalDigest 写出面）。
std::string digestToHex(const core::Digest256& d)
{
    static const char* kDigits = "0123456789abcdef";
    std::string out;
    out.reserve(64);
    for (std::uint8_t b : d) {
        out.push_back(kDigits[b >> 4]);
        out.push_back(kDigits[b & 0x0F]);
    }
    return out;
}

/// 当前 UTC 时刻的 ISO-8601（导出 createdAtUtc 缺省值——IO-D11 的记录
/// 位；仅作记录不作判定，§9.0 确定性注）。
std::string nowUtcIso8601()
{
    const std::time_t now =
        std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm utc{};
#ifdef _MSC_VER
    gmtime_s(&utc, &now);
#else
    gmtime_r(&now, &utc);
#endif
    char buf[32] = {};
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &utc);
    return buf;
}

/// ISO-8601 UTC → epoch 秒（导出 zip 条目时间戳统一取 createdAtUtc——
/// IO-D11 文件层可复现；解析失败返回 -1，调用方回退当前时刻——时间戳
/// 不作判定，回退只影响记录位）。
std::time_t iso8601ToEpoch(const std::string& iso)
{
    if (iso.size() != 20 || iso[4] != '-' || iso[7] != '-' || iso[10] != 'T'
        || iso[13] != ':' || iso[16] != ':' || iso[19] != 'Z') {
        return std::time_t(-1);
    }
    std::tm t{};
    t.tm_year = std::atoi(iso.substr(0, 4).c_str()) - 1900;
    t.tm_mon = std::atoi(iso.substr(5, 2).c_str()) - 1;
    t.tm_mday = std::atoi(iso.substr(8, 2).c_str());
    t.tm_hour = std::atoi(iso.substr(11, 2).c_str());
    t.tm_min = std::atoi(iso.substr(14, 2).c_str());
    t.tm_sec = std::atoi(iso.substr(17, 2).c_str());
#ifdef _MSC_VER
    return _mkgmtime(&t);
#else
    return timegm(&t);
#endif
}

// ---------------------------------------------------------------------
// 包内相对名的防御校验（导出暂存/导入落盘共用——SP-2 的轻量词法半边）
// ---------------------------------------------------------------------

/**
 * @brief 校验包内相对名（非 SafePath 全量规则——那是导入器③的批量预
 *        检（resolver 承载）；此处是导出暂存与落盘前的**词法护栏**：
 *        绝对形态/盘符/UNC/`..` 段/反斜杠/空段一律拒绝，保证拼接出的
 *        磁盘路径不可能逃出承载根。
 *
 * 防御对象是实现方/源方契约违约（ISnapshotFileSource 给出越权路径、
 * manifest 给出未预检名字）——返回 false 时调用方按防御性内部错误拒绝
 * （fail-fast，不尝试"修复"路径——修复即语义裁决，归所有者单元）。
 */
bool isSafePackRelativeName(const std::string& name)
{
    if (name.empty() || name.front() == '/' || name.back() == '/') {
        return false;   // 空/根成分/目录形态（包无目录条目——§7.1）
    }
    if (name.find('\\') != std::string::npos) {
        return false;   // 反斜杠＝Windows 形态——包内恒正斜杠（§7.1）
    }
    if (name.size() >= 2 && (std::isalpha(static_cast<unsigned char>(name[0])) != 0)
        && name[1] == ':') {
        return false;   // 盘符形态
    }
    if (name.rfind("//", 0) == 0) {
        return false;   // UNC 形态
    }
    // 逐段校验：空段/“.”/“..”段拒绝（词法消解前的原始形态即危险——
    // SP-2"规范化后无 .. 段"与本护栏的组合＝纵深）。
    std::vector<std::string> segments;
    std::string segment;
    for (const char ch : name) {
        if (ch == '/') {
            segments.push_back(segment);
            segment.clear();
            continue;
        }
        segment.push_back(ch);
    }
    segments.push_back(segment);
    for (const std::string& s : segments) {
        if (s.empty() || s == "." || s == "..") {
            return false;
        }
    }
    return true;
}

/// 拆分正斜杠相对名为段（落盘目录逐级拼接用；输入已过 isSafePackRelative-
/// Name——本函数不再校验，单一职责）。
std::vector<std::string> splitPackPath(const std::string& name)
{
    std::vector<std::string> segments;
    std::string segment;
    for (const char ch : name) {
        if (ch == '/') {
            segments.push_back(segment);
            segment.clear();
            continue;
        }
        segment.push_back(ch);
    }
    segments.push_back(segment);
    return segments;
}

/// 目录深度（§4.5.1 DirDepth 判据——"payload/a/b/c" 段数 4 → 深度 3：
/// 目录层数不含文件名；payload/ 前缀本身计入（包内布局面））。
std::size_t dirDepthOf(const std::string& packPath)
{
    return splitPackPath(packPath).size() - 1;
}

// ---------------------------------------------------------------------
// JSON DOM 构造小件（导出侧——canonical 写出前的程序化 DOM 装配）
// ---------------------------------------------------------------------

/// 字符串值节点。
JsonValue jsonString(std::string s)
{
    JsonValue v;
    v.type = JsonValue::Type::String;
    v.stringValue = std::move(s);
    return v;
}

/// 整数值节点（大小/计数——非负，int64 无损承载）。
JsonValue jsonInt(std::int64_t i)
{
    JsonValue v;
    v.type = JsonValue::Type::Integer;
    v.integerValue = i;
    return v;
}

/// 对象成员追加（保出现序——canonical 写出时 profile=null 按键名字典序
/// 重排，成员序此处不必预排序）。
void addMember(JsonValue& obj, std::string key, JsonValue value)
{
    JsonMember m;
    m.key = std::move(key);
    m.value = std::move(value);
    obj.members.push_back(std::move(m));
}

// ---------------------------------------------------------------------
// manifest 运行时形态（导入③解析/导出③装配共用的行集）
// ---------------------------------------------------------------------

struct ManifestRow {
    std::string path;                 ///< 条目名（payload/ 前缀——§7.1）
    std::uint64_t size = 0;           ///< 声明未压缩大小（字节）
    core::Digest256 sha256{};         ///< 声明内容摘要
};

/// 会话级预算 scope 的 RAII 守卫（ZipChannel.cpp BudgetScopeSession 同款
/// 形态——verifyThrough/export_ 各自开合；规格拷贝随 scope 保留——调用方
/// 收紧后的限额是步骤内"极值语义"预算维（SingleFileBytes 等）的比较基准。
/// 开立失败＝规格非法（>硬上限），scope 置空，首笔记账即 IO-FORMAT-
/// INTERNAL 失败中止——不静默放行）。
struct PackScopeSession {
    IBudgetGuard* guard = nullptr;
    BudgetScopeId scope{};
    BudgetSpec spec;   ///< scope 生效规格（拷贝——比较基准；guard 空时仍可用）

    PackScopeSession(IBudgetGuard* g, const BudgetSpec& s)
        : guard(g)
        , spec(s)
    {
        if (guard != nullptr) {
            const IoResult<BudgetScopeId> r = guard->openScope(spec);
            if (r) {
                scope = r.value;
            }
        }
    }

    ~PackScopeSession()
    {
        if (guard != nullptr && scope.value != 0) {
            (void)guard->closeScope(scope);   // 无父场景＝纯关闭（§9.2）
        }
    }

    /// 记账入口（ArchiveRatio 误走此方法由 guard 层显式拒绝——§9.2）。
    IoResult<void> charge(BudgetDimension dim, std::uint64_t amount)
    {
        if (guard == nullptr || scope.value == 0) {
            return fail<void>(packError(IoErrorCode::FormatInternal,
                                        "预算 scope 不可用（规格非法或未装配）"));
        }
        return guard->charge(scope, dim, amount);
    }

    /// 压缩包双侧重账（§4.5.2 ArchiveRatio——比较型维的唯一入账通道）。
    IoResult<void> chargeArchive(std::uint64_t compressed, std::uint64_t expanded)
    {
        if (guard == nullptr || scope.value == 0) {
            return fail<void>(packError(IoErrorCode::FormatInternal,
                                        "预算 scope 不可用（规格非法或未装配）"));
        }
        return guard->chargeArchive(scope, compressed, expanded);
    }
};

/// 字节写盘（展开落盘/导出暂存共用——Win32 直写保证错误码保真：
/// ERROR_DISK_FULL 等可判码在 std::ofstream 面会被吞成 failbit）。
bool writeBytesToFile(const std::filesystem::path& dest, const std::string& bytes,
                      std::error_code& ec)
{
    HANDLE h = ::CreateFileW(dest.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        ec = std::error_code(static_cast<int>(::GetLastError()), std::system_category());
        return false;
    }
    // 单次 WriteFile（≤DWORD 上限——调用方按预算 SingleFileBytes 有界）。
    // 前置契约（findings F-198 处置②——注释钉死，IO-T07）：调用方必须
    // 保证 bytes.size() ≤ SingleFileBytes 硬上限 2 GiB（Budget.cpp 维表；
    // 展开落盘/导出暂存两条通路的字节均先经该维预检与 ZipChannel budget
    // scope 读取面），2 GiB < 4 GiB 截断阈值——越界输入属调用方契约违例，
    // 本函数不为不可达分支付截断语义（静默截断 4 GiB 写为部分写）。
    const DWORD want = static_cast<DWORD>(bytes.size() > 0xFFFFFFFFull
                                              ? 0xFFFFFFFFull
                                              : bytes.size());
    DWORD written = 0;
    const BOOL ok =
        bytes.empty() ? TRUE : ::WriteFile(h, bytes.data(), want, &written, nullptr);
    if (!ok || written != want) {
        ec = std::error_code(static_cast<int>(::GetLastError()), std::system_category());
        ::CloseHandle(h);
        return false;
    }
    ::CloseHandle(h);
    return true;
}

/// 写失败分类：磁盘满特判（ERROR_DISK_FULL/ERROR_HANDLE_DISK_FULL →
/// IO-PACK-DISK-FULL——§7.4"写入失败兜底"行），其余走 §4.2.5 四分类。
IoError osOrDiskFull(const std::error_code& ec, const std::filesystem::path& target,
                     const char* context)
{
    const DWORD value = static_cast<DWORD>(ec.value());
    if (value == ERROR_DISK_FULL || value == ERROR_HANDLE_DISK_FULL) {
        return packError(IoErrorCode::PackDiskFull, std::string(context) + "（磁盘满）",
                         {{"path", platform::displayOf(target)},
                          {"os-error", std::to_string(value)}});
    }
    return platform::makeOsError(value, true, target, context);
}

/// 进度回调（驱动线程内同步——§9.13.4；stage＝稳定英文短语）。
void emitProgress(const IoProgressCallback& cb, std::uint64_t done, std::uint64_t total,
                  const char* stage)
{
    if (cb) {
        const IoProgress p{done, total, stage};
        cb(p);
    }
}

} // namespace

// =====================================================================
// 导入会话承载（PackageImportSession::Impl——头文件前向声明的完整定义）
// =====================================================================

struct PackageImportSession::Impl {
    // ---- 身份与装配 ----
    std::filesystem::path packFile;        ///< 步骤①选定的包文件
    PackageImportOptions options;          ///< 导入选项（预算规格在 begin 强化）
    ITempAreaManagerPtr tempAreas;         ///< 临时区管理器（清理用）
    TempAreaSession area;                  ///< 步骤②的临时区会话
    PackageImportState state = PackageImportState::Staging;
    PackageImportReport lastReport;        ///< 最近一次报告（⑦终态填充）

    // ---- 步骤③~⑥的工作数据（verifyThrough 间复用；会话单线程） ----
    std::shared_ptr<IZipChannel> zip;      ///< 容器层会话（③打开——⑥前存活）
    std::vector<ZipEntryInfo> archiveEntries;        ///< 归档条目（③枚举）
    std::vector<ManifestRow> manifest;     ///< manifest 行集（③解析）
    std::vector<std::string> missingEntries;         ///< manifest 引用但归档缺失（⑥）
    std::vector<ZipEntryVerifyReport> entryResults;  ///< 条目级校验结论（⑤）
    std::set<std::string> archiveNames;    ///< 归档名精确集合（③建、⑤⑥消费）
    core::Digest256 totalDigest{};         ///< rwpack.json 声明的 totalDigest（③装载）
    std::uint64_t totalBytes = 0;          ///< 累计展开字节（⑤累计、⑦报告）

    /// RAII 兜底（§9.9"session RAII 兜底清理"）：临时区仍活动时尽力清理
    /// （失败静默——残留由 §7.5 标记机制回收；显式 cleanup 才有报告面）。
    ~Impl()
    {
        if (area.isActive() && tempAreas) {
            (void)tempAreas->cleanup(area);
        }
    }

    Impl() = default;
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
};

PackageImportSession::~PackageImportSession() = default;
PackageImportSession::PackageImportSession(PackageImportSession&& other) noexcept = default;
PackageImportSession&
PackageImportSession::operator=(PackageImportSession&& other) noexcept = default;

PackageImportState PackageImportSession::state() const noexcept
{
    return m_impl != nullptr ? m_impl->state : PackageImportState::Staging;
}

const std::filesystem::path& PackageImportSession::stagingRoot() const noexcept
{
    static const std::filesystem::path kEmpty;
    return m_impl != nullptr ? m_impl->area.rootPath() : kEmpty;
}

std::filesystem::path PackageImportSession::payloadRoot() const noexcept
{
    if (m_impl == nullptr || m_impl->area.rootPath().empty()) {
        return {};
    }
    return m_impl->area.rootPath() / L"payload";
}

const PackageImportReport& PackageImportSession::report() const noexcept
{
    static const PackageImportReport kEmpty{};   // 零值哨兵（标量成员定为零——空报告判定面）
    return m_impl != nullptr ? m_impl->lastReport : kEmpty;
}

PackageImportSession PackageImportSession::adopt(std::shared_ptr<Impl> impl)
{
    PackageImportSession out;
    out.m_impl = std::move(impl);
    return out;
}

std::shared_ptr<PackageImportSession::Impl> PackageImportSession::impl() const noexcept
{
    return m_impl;
}

// =====================================================================
// 导入器实现（§7.3 九步协议）
// =====================================================================

class PackageImporterImpl final : public IPackageImporter {
public:
    explicit PackageImporterImpl(PackageIoFacilities facilities)
        : m_facilities(std::move(facilities))
        , m_jsonReader(makeStructuredDataReader(nullptr))   // profile=null——包契约
    {
        // 缺省设施＝产品真实实现（§9.11 落位前的直接装配形态——
        // PackageIoFacilities 类注；测试经 facilities 注入 fake）。
        if (!m_facilities.safePath) {
            m_facilities.safePath = makeSafePathResolver();
        }
        if (!m_facilities.budgetFactory) {
            m_facilities.budgetFactory = [] { return makeBudgetGuard(); };
        }
        if (!m_facilities.tempAreas) {
            m_facilities.tempAreas = makeTempAreaManager();
        }
    }

    // -----------------------------------------------------------------
    // 步骤①②：选择包＋建立临时区（§7.3）
    // -----------------------------------------------------------------
    IoResult<PackageImportSession> begin(const std::filesystem::path& packFile,
                                         const PackageImportOptions& options,
                                         IoCancelToken* cancel,
                                         IoProgressCallback progress) override
    {
        IoResult<PackageImportSession> out;
        if (cancelHit(cancel)) {
            out.error.code = IoErrorCode::Cancelled;
            return out;
        }
        emitProgress(progress, 0, 1, "probe");

        // ① P-1 预检：存在＋可读＋扩展名＋魔数（§7.3①）。魔数 PK\x03\x04
        // ＝ZIP 本地文件头签名——扩展名可伪造、魔数不可（防"改名的非 ZIP"
        // 进入容器层；容器层损坏的深检在③经 openZipChannel）。
        std::error_code ec;
        if (!std::filesystem::exists(packFile, ec)) {
            return fail<PackageImportSession>(platform::makeOsError(
                ERROR_FILE_NOT_FOUND, false, packFile, "包文件不存在（步骤①）"));
        }
        if (packFile.extension() != L".rwpack") {
            return fail<PackageImportSession>(packError(
                IoErrorCode::FormatPackZip, "扩展名非 .rwpack（步骤①）",
                {{"path", platform::displayOf(packFile)}}));
        }
        {
            std::ifstream in(packFile, std::ios::binary);
            if (!in.is_open()) {
                return fail<PackageImportSession>(platform::makeOsError(
                    ERROR_ACCESS_DENIED, false, packFile, "包文件打不开（步骤①）"));
            }
            char magic[4] = {};
            in.read(magic, 4);
            if (!in || std::memcmp(magic, "PK\x03\x04", 4) != 0) {
                return fail<PackageImportSession>(packError(
                    IoErrorCode::FormatPackZip, "魔数非 ZIP（PK\\x03\\x04）——步骤①",
                    {{"path", platform::displayOf(packFile)}}));
            }
        }

        // 目标父目录预检（§9.9 前置"targetDir 父目录存在可写且与临时区
        // 同卷"——临时区建在父目录旁＝同卷结构性满足，IO-D08）。
        const std::filesystem::path targetParent = options.targetDir.parent_path();
        if (options.targetDir.empty() || targetParent.empty()
            || !std::filesystem::is_directory(targetParent, ec)) {
            return fail<PackageImportSession>(platform::makeOsError(
                ERROR_PATH_NOT_FOUND, true, targetParent,
                "发布目标父目录不存在（步骤②前置——§9.9）"));
        }

        // ② 建立临时区：`.rwpack-import-<8hex>/`（§7.5 创建行；同卷——
        // 发布 rename 的原子性前提）。互斥/崩溃残留回收在管理器内。
        IoResult<TempAreaSession> area = m_facilities.tempAreas->create(
            TempAreaSpec{TempAreaRole::PackImport, targetParent, std::string()}, cancel);
        if (!area) {
            out.error = std::move(area.error);
            return out;
        }

        // 会话就位（Staging）。预算规格做包通道强化拷贝（§4.5.2"包导入
        // 通道禁用放宽"——对四维置放宽禁用位：即使调用方误传未强化规格，
        // relaxToHardLimit 也不可能生效；tighten 恒合法不受影响）。
        auto impl = std::make_shared<PackageImportSession::Impl>();
        impl->packFile = packFile;
        impl->options = options;
        impl->options.budget.m_relaxForbidden[static_cast<std::size_t>(
            BudgetDimension::ArchiveExpandedBytes)] = true;
        impl->options.budget.m_relaxForbidden[static_cast<std::size_t>(
            BudgetDimension::ArchiveRatio)] = true;
        impl->options.budget.m_relaxForbidden[static_cast<std::size_t>(
            BudgetDimension::FileCount)] = true;
        impl->options.budget.m_relaxForbidden[static_cast<std::size_t>(
            BudgetDimension::DirDepth)] = true;
        impl->tempAreas = m_facilities.tempAreas;
        impl->area = std::move(area.value);
        out.value = PackageImportSession::adopt(std::move(impl));
        emitProgress(progress, 1, 1, "probe");
        return out;
    }

    // -----------------------------------------------------------------
    // 步骤③~⑦：全量校验（§7.3；全通过→Verified；§7.6 失败/取消状态机）
    // -----------------------------------------------------------------
    IoResult<PackageImportReport> verifyThrough(PackageImportSession& session,
                                                IoCancelToken* cancel,
                                                IoProgressCallback progress) override
    {
        // 卡面 §9.9 原文签名为 verifyThrough(PackageImportSession&)；等价
        // 增补（io.md §15.5 v0.9 登记）：取消令牌与进度回调后置——九步
        // "逐步可取消"（§7.3 标题原文）需要令牌在③~⑤逐条目检查点生效，
        // 会话构造时锁死令牌会令同一会话无法被两个阶段分别驱动。
        if (session.impl() == nullptr) {
            return fail<PackageImportReport>(
                packError(IoErrorCode::FormatInternal, "verifyThrough 前置违约（空会话）"));
        }
        PackageImportSession::Impl& s = *session.impl();
        // 重复调用语义：Verified 幂等回放（只读观测）；其余非 Staging 态
        // ＝防御性拒绝（终态会话不可再校验——§7.3 状态图单向性）。
        if (s.state == PackageImportState::Verified) {
            IoResult<PackageImportReport> out;
            out.value = s.lastReport;
            return out;
        }
        if (s.state != PackageImportState::Staging) {
            return fail<PackageImportReport>(packError(
                IoErrorCode::FormatInternal, "verifyThrough 前置违约（会话已终态）"));
        }

        // 会话级预算 scope（§4.5.2——包通道四维强制；规格在 begin 已强化）。
        IBudgetGuardPtr guard = m_facilities.budgetFactory();
        PackScopeSession budget(guard.get(), s.options.budget);

        IoResult<PackageImportReport> out;
        IoError failure = runSteps(s, guard.get(), budget, cancel, progress);
        if (failure.code == IoErrorCode::Ok) {
            // ⑦ 定稿＋⑧前目标预检（io 侧——§7.4"目标目录已存在"行；发布
            // 时 project 仍二次校验——TOCTOU 收口归 project，§7.4 同行）。
            std::error_code ec;
            if (std::filesystem::exists(s.options.targetDir, ec)) {
                failure = packError(IoErrorCode::PackTargetExists,
                                    "发布目标已存在（⑧前预检——§7.4）",
                                    {{"target", platform::displayOf(s.options.targetDir)}});
            }
        }
        if (failure.code == IoErrorCode::Ok) {
            s.state = PackageImportState::Verified;
            finalizeReport(s, budget.guard, budget.scope);
            out.value = s.lastReport;
            return out;
        }
        // 失败/取消路径（§7.6 状态图）：取消＝Canceled（无诊断——UX-03）；
        // 其余＝Failed（诊断入报告）。随后自动清理；清理失败→CleanupFailed
        // （可重试——acceptance 2 的"二次 cleanup 幂等可成功"）。
        if (failure.code != IoErrorCode::Cancelled) {
            s.lastReport.diagnostics.push_back(failure);
        }
        finalizeReport(s, guard.get(), budget.scope);
        IoResult<void> cleaned = m_facilities.tempAreas->cleanup(s.area);
        if (cleaned) {
            s.state = (failure.code == IoErrorCode::Cancelled)
                          ? PackageImportState::Canceled
                          : PackageImportState::Failed;
        } else {
            // §7.6 CleanupFailed 终态：返回错误＝IO-PACK-CLEANUP-FAILED
            // （残留清单已由 TempArea 填入 params/detail）；原失败码经
            // phase 参数保留（不掩盖原语义，也不冒充原语义——§2.5 正交）。
            s.state = PackageImportState::CleanupFailed;
            s.lastReport.finalState = s.state;
            IoError cleanupError = cleaned.error;
            cleanupError.params.emplace_back("phase", std::string(errorCodeToken(failure.code)));
            out.error = std::move(cleanupError);
            return out;
        }
        s.lastReport.finalState = s.state;
        out.error = std::move(failure);
        return out;
    }

    // -----------------------------------------------------------------
    // 步骤⑨：清理（幂等；Verified 发布后 / CleanupFailed 重试）
    // -----------------------------------------------------------------
    IoResult<void> cleanup(PackageImportSession& session) override
    {
        if (session.impl() == nullptr) {
            return fail<void>(
                packError(IoErrorCode::FormatInternal, "cleanup 前置违约（空会话）"));
        }
        PackageImportSession::Impl& s = *session.impl();
        if (!s.area.isActive()) {
            // 已清理（Cleaned/失败终态已清理）——幂等成功（§9.9 原文）。
            if (s.state != PackageImportState::Cleaned) {
                s.state = PackageImportState::Cleaned;
                s.lastReport.finalState = s.state;
            }
            return IoResult<void>{};
        }
        IoResult<void> out = m_facilities.tempAreas->cleanup(s.area);
        if (out) {
            s.state = PackageImportState::Cleaned;
        } else {
            s.state = PackageImportState::CleanupFailed;   // §7.6：可重试
        }
        s.lastReport.finalState = s.state;
        return out;
    }

private:
    // -----------------------------------------------------------------
    // ③~⑦ 步骤链（单点返回失败码；每步失败即停——早失败早清理）
    // -----------------------------------------------------------------
    IoError runSteps(PackageImportSession::Impl& s, IBudgetGuard* guard,
                     PackScopeSession& budget, IoCancelToken* cancel,
                     const IoProgressCallback& progress)
    {
        IoError err = step3VersionAndNames(s, guard);
        if (err.code != IoErrorCode::Ok) {
            return err;
        }
        err = step4ExtractAndHash(s, guard, budget, cancel, progress);
        if (err.code != IoErrorCode::Ok) {
            return err;
        }
        err = step5MirrorStructure(s);
        if (err.code != IoErrorCode::Ok) {
            return err;
        }
        return step6ReferenceIntegrity(s);
    }

    /// ③：容器打开/枚举 → rwpack.json 版本判定 → manifest 解析＋
    /// totalDigest → 条目名/属性预检（全部在**展开前**——§7.3③）。
    IoError step3VersionAndNames(PackageImportSession::Impl& s, IBudgetGuard* guard)
    {
        // 容器层打开＋枚举（客观事实——包语义裁决在后续步骤）。
        IoResult<std::unique_ptr<IZipChannel>> zip = openZipChannel(s.packFile);
        if (!zip) {
            return zip.error;
        }
        s.zip = std::move(zip.value);
        IoResult<std::vector<ZipEntryInfo>> entries = s.zip->listEntries();
        if (!entries) {
            return entries.error;
        }
        s.archiveEntries = std::move(entries.value);
        for (const ZipEntryInfo& e : s.archiveEntries) {
            s.archiveNames.insert(e.name);
        }

        // ③-a rwpack.json：版本判定（§7.1 版本行——FUTURE/LEGACY 稳定拒
        // 绝，params 携带当前/文件版本＝PM-06"升级指引数据"的 io 半边；
        // 升级器归 project）。
        JsonEntry rwpack = readJsonEntry(s, guard, PackFormat::kRwpackEntryName);
        if (!rwpack.ok) {
            return rwpack.error;
        }
        IoError err = validateRwpackJson(rwpack.doc, s.totalDigest);
        if (err.code != IoErrorCode::Ok) {
            return err;
        }
        // ③-b manifest.json：解析＋totalDigest 校验（§7.1 哈希行："对
        // manifest canonical 字节的摘要——防 manifest 自身被改"）。
        JsonEntry manifest = readJsonEntry(s, guard, PackFormat::kManifestEntryName);
        if (!manifest.ok) {
            return manifest.error;
        }
        const IoError manifestErr = parseManifest(manifest.doc, s.manifest, s.totalDigest);
        if (manifestErr.code != IoErrorCode::Ok) {
            return manifestErr;
        }
        // ③-c 条目名/属性预检（目录条目/加密/压缩方法/symlink/SP-2~9/折叠键
        // 重复——V13"展开前零落盘"的机制落点；任何命中即整体拒绝，零解压）。
        return precheckEntryNames(s);
    }

    /// 归档内 JSON 条目读取＋受限解析（profile=null——§5.9.2 内部件仅
    /// 语法/安全层；字节经 ZipChannel 预算面读入）。
    struct JsonEntry {
        bool ok = false;
        JsonDocument doc;
        IoError error;
    };

    JsonEntry readJsonEntry(PackageImportSession::Impl& s, IBudgetGuard* guard,
                            const char* entryName)
    {
        JsonEntry out;
        // 内部 scope（budgetScope=0）——包 JSON 受 JsonDocBytes 产品默认
        // 管辖；会话累计面由⑤的 TotalBytes 承载（文件头注"两层纵深"）。
        IoResult<IoString> bytes =
            s.zip->readEntryBytes(entryName, guard, BudgetScopeId{}, nullptr);
        if (!bytes) {
            // 缺失＝镜像必备缺失（rwpack.json/manifest.json 不在 manifest
            // 清单内，缺失在此直接暴露——IO-PACK-REF-INCOMPLETE，§7.4
            // "镜像缺必备文件"）；其余（预算/容器/取消）按原码透传。
            if (bytes.error.code == IoErrorCode::PackRefIncomplete) {
                out.error = packError(IoErrorCode::PackRefIncomplete,
                                      std::string("镜像必备条目缺失：") + entryName,
                                      {{"entry", entryName}});
            } else {
                out.error = std::move(bytes.error);
            }
            return out;
        }
        IoResult<JsonDocument> doc =
            m_jsonReader->parseBytes(bytes.value, JsonReadOptions{}, guard, nullptr);
        if (!doc) {
            // 包契约文件的 JSON 语法/编码违例归 IO-FORMAT-PACK-MANIFEST
            // （§7.1——manifest/rwpack.json 属包契约面；定位细节进 detail）。
            out.error = packError(IoErrorCode::FormatPackManifest,
                                  std::string(entryName) + " 解析失败：" + doc.error.detail,
                                  {{"entry", entryName}});
            return out;
        }
        out.ok = true;
        out.doc = std::move(doc.value);
        return out;
    }

    /// rwpack.json 契约判定（formatId/schemaVersion/content.totalDigest；
    /// totalDigest 装载到 outDigest 供 manifest 比对）。
    static IoError validateRwpackJson(const JsonDocument& doc, core::Digest256& outDigest)
    {
        const JsonValue& root = doc.root;
        if (!root.isObject()) {
            return packError(IoErrorCode::FormatPackManifest, "rwpack.json 根非对象",
                             {{"entry", PackFormat::kRwpackEntryName}});
        }
        // formatId（§7.1 格式标识——非 rwpack 即非本格式）。
        const JsonValue* formatId = root.findMember("formatId");
        if (formatId == nullptr || !formatId->isString()
            || formatId->stringValue != PackFormat::kFormatId) {
            return packError(IoErrorCode::FormatPackManifest, "formatId 非法",
                             {{"entry", PackFormat::kRwpackEntryName}});
        }
        // schemaVersion（整数比较——未来/旧版本稳定拒绝，PM-06 口径）。
        const JsonValue* version = root.findMember("schemaVersion");
        if (version == nullptr || !version->isInteger()) {
            return packError(IoErrorCode::FormatPackManifest,
                             "schemaVersion 缺失或非整数",
                             {{"entry", PackFormat::kRwpackEntryName}});
        }
        if (version->integerValue > static_cast<std::int64_t>(PackFormat::kSchemaVersion)) {
            return packError(
                IoErrorCode::FormatJsonVersionFuture,
                "包版本高于本软件支持（稳定只读拒绝——PM-06）",
                {{"entry", PackFormat::kRwpackEntryName},
                 {"current", std::to_string(PackFormat::kSchemaVersion)},
                 {"file", std::to_string(version->integerValue)}});
        }
        if (version->integerValue < static_cast<std::int64_t>(PackFormat::kSchemaVersion)) {
            return packError(
                IoErrorCode::FormatJsonVersionLegacy,
                "旧版本包且未注册升级步距（升级器归 project——io 给数据）",
                {{"entry", PackFormat::kRwpackEntryName},
                 {"current", std::to_string(PackFormat::kSchemaVersion)},
                 {"file", std::to_string(version->integerValue)}});
        }
        // content.totalDigest（64 位小写 hex——③-b 的比对基准）。
        const JsonValue* content = root.findMember("content");
        if (content == nullptr || !content->isObject()) {
            return packError(IoErrorCode::FormatPackManifest, "content 缺失或非对象",
                             {{"entry", PackFormat::kRwpackEntryName}});
        }
        const JsonValue* totalDigest = content->findMember("totalDigest");
        if (totalDigest == nullptr || !totalDigest->isString()
            || !parseHexDigest(totalDigest->stringValue, &outDigest)) {
            return packError(IoErrorCode::FormatPackManifest,
                             "content.totalDigest 缺失或非法（须 64 位小写 hex）",
                             {{"entry", PackFormat::kRwpackEntryName}});
        }
        return IoError{};
    }

    /// manifest 解析＋totalDigest 比对＋行集抽取（§7.1 manifest 契约）。
    static IoError parseManifest(const JsonDocument& manifestDoc,
                                 std::vector<ManifestRow>& rows,
                                 const core::Digest256& totalDigest)
    {
        const JsonValue& root = manifestDoc.root;
        if (!root.isObject()) {
            return packError(IoErrorCode::FormatPackManifest, "manifest.json 根非对象",
                             {{"entry", PackFormat::kManifestEntryName}});
        }
        const JsonValue* schema = root.findMember("schemaVersion");
        if (schema == nullptr || !schema->isString()
            || schema->stringValue != PackFormat::kManifestSchemaVersion) {
            return packError(IoErrorCode::FormatPackManifest, "manifest schemaVersion 非法",
                             {{"entry", PackFormat::kManifestEntryName},
                              {"expected", PackFormat::kManifestSchemaVersion}});
        }
        // totalDigest 复核：对 manifest canonical 字节重算摘要（§7.1 哈希
        // 行）——canonical 化吸收键序/空白书写差异；条目内容或顺序被改
        // 即摘要失配（防 manifest 自身被改）。
        IoResult<core::Digest256> actual =
            digestCanonicalJson(manifestDoc, JsonWriteOptions{});
        if (!actual) {
            return packError(IoErrorCode::FormatPackManifest,
                             "manifest canonical 化失败：" + actual.error.detail,
                             {{"entry", PackFormat::kManifestEntryName}});
        }
        if (actual.value != totalDigest) {
            return packError(
                IoErrorCode::FormatPackManifest,
                "manifest 与 rwpack.json.content.totalDigest 不符（防篡改——§7.1）",
                {{"entry", PackFormat::kManifestEntryName},
                 {"expected", digestToHex(totalDigest)},
                 {"actual", digestToHex(actual.value)}});
        }
        // entries 行集抽取（path/size/sha256；结构违例＝FormatPackManifest
        // ——包契约面，path 恒 payload/ 前缀）。
        const JsonValue* entries = root.findMember("entries");
        if (entries == nullptr || !entries->isArray()) {
            return packError(IoErrorCode::FormatPackManifest, "entries 缺失或非数组",
                             {{"entry", PackFormat::kManifestEntryName}});
        }
        rows.reserve(entries->items.size());
        for (const JsonValue& item : entries->items) {
            if (!item.isObject()) {
                return packError(IoErrorCode::FormatPackManifest, "entries 元素非对象",
                                 {{"entry", PackFormat::kManifestEntryName}});
            }
            ManifestRow row;
            const JsonValue* path = item.findMember("path");
            if (path == nullptr || !path->isString()
                || path->stringValue.rfind(PackFormat::kPayloadPrefix, 0) != 0) {
                return packError(IoErrorCode::FormatPackManifest,
                                 "entries[].path 缺失或非 payload/ 前缀",
                                 {{"entry", PackFormat::kManifestEntryName}});
            }
            row.path = path->stringValue;
            const JsonValue* size = item.findMember("size");
            if (size == nullptr || !size->isInteger() || size->integerValue < 0) {
                return packError(IoErrorCode::FormatPackManifest,
                                 "entries[].size 缺失或非非负整数",
                                 {{"entry", row.path}});
            }
            row.size = static_cast<std::uint64_t>(size->integerValue);
            const JsonValue* sha = item.findMember("sha256");
            if (sha == nullptr || !sha->isString()
                || !parseHexDigest(sha->stringValue, &row.sha256)) {
                return packError(IoErrorCode::FormatPackManifest,
                                 "entries[].sha256 缺失或非法（须 64 位小写 hex）",
                                 {{"entry", row.path}});
            }
            rows.push_back(std::move(row));
        }
        return IoError{};
    }

    /// ③-c 条目名/属性预检（**展开前**——V13"展开前零落盘"的保证点）。
    IoError precheckEntryNames(PackageImportSession::Impl& s)
    {
        // 容器层客观约束先行（§7.1 条目约束行）：目录条目/加密/压缩方法
        // 白名单/symlink 属性——任何命中即整体拒绝，零解压。
        for (const ZipEntryInfo& e : s.archiveEntries) {
            if (!e.name.empty() && e.name.back() == '/') {
                return packError(IoErrorCode::FormatPackEntry,
                                 "目录条目（§7.1：无目录条目——仅文件）",
                                 {{"entry", e.name}});
            }
            if (e.encrypted) {
                return packError(IoErrorCode::FormatPackEncrypted,
                                 "加密条目拒绝（§7.1——不承载加密语义）",
                                 {{"entry", e.name}});
            }
            if (e.compressionMethod != 0 && e.compressionMethod != 8) {
                return packError(IoErrorCode::FormatPackZip,
                                 "压缩方法非 STORED/DEFLATE（§7.1 白名单）",
                                 {{"entry", e.name},
                                  {"method", std::to_string(e.compressionMethod)}});
            }
            // SP-4 预检半边：UNIX 形态的 S_IFLNK 位＝符号链接条目（展开
            // 产物层的 reparse point 复检是另一半——§7.4"双检"行）。
            if (e.attributeHostSystem == 3   // 3＝UNIX（PKWARE APPNOTE 宿主码）
                && ((e.externalAttributes >> 16) & 0xF000) == 0xA000) {
                return packError(IoErrorCode::SecPathSymlink,
                                 "符号链接条目拒绝（SP-4——展开前预检）",
                                 {{"entry", e.name}});
            }
        }
        // 名字规则批检（SafePath P-4 规则核——穿越/绝对/保留名/尾点空白/
        // 非法字符/折叠键重复；resolver 产出稳定码面——§4.3.3 P-4 表全
        // 行；任一违规＝整批拒绝，errors 全量列出于 detail）。
        const std::vector<ZipEntryInfo>& all = s.archiveEntries;
        IoResult<void> names = m_facilities.safePath->normalizePackEntries(
            all.size(), [&all](std::size_t i) { return all[i].name; });
        if (!names) {
            return names.error;
        }
        return IoError{};
    }

    /// ④⑤：逐 manifest 行展开（预算/磁盘/取消检查点）＋逐文件哈希复算
    /// （展开即验；大小/哈希不符＝条目级定位＋整体拒绝——§7.4 哈希行）。
    IoError step4ExtractAndHash(PackageImportSession::Impl& s, IBudgetGuard* guard,
                                PackScopeSession& budget, IoCancelToken* cancel,
                                const IoProgressCallback& progress)
    {
        const std::filesystem::path payloadRoot = s.area.rootPath() / L"payload";
        s.state = PackageImportState::Extracting;
        s.entryResults.clear();
        s.entryResults.reserve(s.manifest.size());

        for (std::size_t i = 0; i < s.manifest.size(); ++i) {
            const ManifestRow& row = s.manifest[i];
            // 取消检查点（每条目——§7.3 步注；UX-03：状态非错误）。
            if (cancelHit(cancel)) {
                return IoError{IoErrorCode::Cancelled, {}, {}};
            }
            emitProgress(progress, i, s.manifest.size(), "extract");

            // 落盘前词法护栏（manifest 行已过③批量预检——纵深复检；拒绝
            // ＝防御性内部错误：③与④之间数据不可变，触及即缺陷）。
            if (!isSafePackRelativeName(row.path)) {
                return packError(IoErrorCode::FormatInternal,
                                 "manifest 行未通过落盘前词法护栏（③/④不一致）",
                                 {{"entry", row.path}});
            }
            // 定位归档条目（byte-exact——§7.1 manifest 与归档同名）。缺失
            // 不在此中止——收集后于⑥汇总拒绝（§7.4"缺失文件"行的"拒绝
            // ＋缺失清单"形态：继续校验以给出全量缺失清单）。
            if (s.archiveNames.find(row.path) == s.archiveNames.end()) {
                s.missingEntries.push_back(row.path);
                ZipEntryVerifyReport missing;
                missing.path = row.path;
                missing.error = packError(IoErrorCode::PackRefIncomplete,
                                          "manifest 引用的条目在归档中不存在",
                                          {{"entry", row.path}});
                s.entryResults.push_back(std::move(missing));
                continue;
            }

            // ---- 预算检查（§7.3④；③在④前——先拒超限再打开，§4.4 要点）----
            // 单文件上限（调用方收紧规格优先比较；ZipChannel 内部 scope
            // 另有产品默认硬顶——双层纵深，§4.5.2）。
            if (row.size > s.options.budget.limit(BudgetDimension::SingleFileBytes)) {
                return packError(IoErrorCode::SecBudgetFile,
                                 "条目声明大小超单文件预算（比较型三要素）",
                                 {{"entry", row.path},
                                  {"actual", std::to_string(row.size)},
                                  {"limit", std::to_string(s.options.budget.limit(
                                                BudgetDimension::SingleFileBytes))},
                                  {"unit", "bytes"}});
            }
            // 文件数累计（FileCount——§4.5.1 累计维）。
            if (IoResult<void> r = budget.charge(BudgetDimension::FileCount, 1); !r) {
                return prependEntry(r.error, row.path);
            }
            // 目录深度（极值语义直接比较——"深度"不是累计量，charge 的
            // 累计入账模型不适用；限额取规格当前值——§4.5.1）。
            const std::size_t depth = dirDepthOf(row.path);
            if (depth > s.options.budget.limit(BudgetDimension::DirDepth)) {
                return packError(IoErrorCode::SecBudgetDepth,
                                 "条目目录深度超预算（比较型三要素）",
                                 {{"entry", row.path},
                                  {"actual", std::to_string(depth)},
                                  {"limit", std::to_string(s.options.budget.limit(
                                                BudgetDimension::DirDepth))},
                                  {"unit", "levels"}});
            }
            // 压缩炸弹预检（声明面——chargeArchive 双侧重账：压缩/展开
            // 分别累计后比较，§4.5.2；超限＝IO-SEC-BOMB-RATIO 或
            // IO-SEC-BUDGET-EXPAND，状态不变＋整体中止）。压缩侧取归档
            // 头声明压缩量（可能说谎——实际侧由展开后补差与 ZipChannel
            // 内部 scope 复核兜底——§4.5.2 双重计数）。
            std::uint64_t declaredComp = 0;
            for (const ZipEntryInfo& e : s.archiveEntries) {
                if (e.name == row.path) {
                    declaredComp = e.compressedSize;
                    break;
                }
            }
            if (IoResult<void> r = budget.chargeArchive(declaredComp, row.size); !r) {
                return prependEntry(r.error, row.path);
            }
            // 磁盘可用空间预检（§7.3④——所需（本条目声明大小）与可用
            // （临时区卷）比较；提示性探测——写入失败兜底是防线之二，
            // §7.4"磁盘不足"行的"②④预检＋写入失败兜底"双落点之一）。
            IoResult<std::uint64_t> avail = m_facilities.tempAreas->availableBytes(s.area);
            if (avail && avail.value < row.size) {
                return packError(IoErrorCode::PackDiskFull,
                                 "临时区卷可用空间不足（所需/可用比较型——§7.4）",
                                 {{"entry", row.path},
                                  {"required", std::to_string(row.size)},
                                  {"available", std::to_string(avail.value)},
                                  {"unit", "bytes"}});
            }
            // 临时区占用累计（TempAreaBytes——§4.5.1/§7.5）。
            if (IoResult<void> r = budget.charge(BudgetDimension::TempAreaBytes, row.size);
                !r) {
                return prependEntry(r.error, row.path);
            }

            // ---- 展开读取（ZipChannel 内部 scope 逐条目隔离——文件头注
            // "两层纵深"；实际展开超声明/单文件产品硬顶在通道层兜底）----
            IoResult<IoString> bytes = s.zip->readEntryBytes(row.path, guard, BudgetScopeId{},
                                                             cancel);
            if (!bytes) {
                if (bytes.error.code == IoErrorCode::Cancelled) {
                    return IoError{IoErrorCode::Cancelled, {}, {}};
                }
                return prependEntry(bytes.error, row.path);
            }
            // 会话级总量记账（TotalBytes——实际展开量；§4.4⑤ 每条目累计）。
            if (IoResult<void> r =
                    budget.charge(BudgetDimension::TotalBytes,
                                  static_cast<std::uint64_t>(bytes.value.size()));
                !r) {
                return prependEntry(r.error, row.path);
            }

            // ---- ⑤ 逐文件哈希复算（展开即验——早失败早清理；SA-12：
            // core ContentDigester 唯一算法）----
            core::ContentDigester digester;
            digester.update(bytes.value.data(), bytes.value.size());
            const core::Digest256 actual = digester.finalize();
            ZipEntryVerifyReport report;
            report.path = row.path;
            if (bytes.value.size() != row.size) {
                report.error = packError(IoErrorCode::PackHashMismatch,
                                         "条目实际大小与 manifest 声明不符",
                                         {{"entry", row.path},
                                          {"expected", std::to_string(row.size)},
                                          {"actual", std::to_string(bytes.value.size())}});
                s.entryResults.push_back(report);
                return report.error;
            }
            if (actual != row.sha256) {
                report.error = packError(
                    IoErrorCode::PackHashMismatch,
                    "条目内容摘要与 manifest 声明不符（SHA-256 复算——§7.3⑤）",
                    {{"entry", row.path},
                     {"expected", digestToHex(row.sha256)},
                     {"actual", digestToHex(actual)}});
                s.entryResults.push_back(report);
                return report.error;
            }
            report.error = IoError{};
            s.entryResults.push_back(std::move(report));

            // ---- 落盘（payload 树逐字节还原——目录按需创建；写失败含
            // IO-PACK-DISK-FULL 特判兜底——§7.4"写入失败兜底"行）----
            const std::vector<std::string> segments = splitPackPath(
                row.path.substr(std::char_traits<char>::length(PackFormat::kPayloadPrefix)));
            std::filesystem::path dest = payloadRoot;
            std::error_code ec;
            for (std::size_t k = 0; k + 1 < segments.size(); ++k) {
                dest /= platform::utf8ToWide(segments[k]);
            }
            std::filesystem::create_directories(dest, ec);
            if (ec) {
                return osOrDiskFull(ec, dest, "条目目录创建失败（展开落盘）");
            }
            dest /= platform::utf8ToWide(segments.back());
            if (!writeBytesToFile(dest, bytes.value, ec)) {
                return osOrDiskFull(ec, dest, "条目内容写入失败（展开落盘）");
            }
            s.totalBytes += static_cast<std::uint64_t>(bytes.value.size());
        }
        emitProgress(progress, s.manifest.size(), s.manifest.size(), "extract");
        return IoError{};
    }

    /// ⑤ 结构镜像核对（§7.3⑤"目录结构 vs §7.1 镜像规则"）：归档→manifest
    /// 方向（未清单化条目/多余顶层条目）＋必备条目齐全。
    static IoError step5MirrorStructure(PackageImportSession::Impl& s)
    {
        std::set<std::string> manifestNames;
        for (const ManifestRow& row : s.manifest) {
            manifestNames.insert(row.path);
        }
        // 归档条目全覆盖：rwpack.json/manifest.json 之外的每个条目必须
        // payload/ 前缀且在 manifest 内（多余顶层条目拒绝——§7.3⑤ 原文；
        // 镜像纪律的导入侧对称面）。
        for (const std::string& name : s.archiveNames) {
            if (name == PackFormat::kRwpackEntryName
                || name == PackFormat::kManifestEntryName) {
                continue;
            }
            if (name.rfind(PackFormat::kPayloadPrefix, 0) != 0) {
                return packError(IoErrorCode::FormatPackEntry,
                                 "多余顶层条目（镜像布局外——§7.3⑤）",
                                 {{"entry", name}});
            }
            if (manifestNames.find(name) == manifestNames.end()) {
                return packError(IoErrorCode::FormatPackEntry,
                                 "归档条目未在 manifest 清单内（镜像纪律）",
                                 {{"entry", name}});
            }
        }
        // 必备条目（§7.1"objects/、revisions/、HEAD、project.json 恒在"
        // 的文件形态——缺失＝镜像缺必备文件，IO-PACK-REF-INCOMPLETE，
        // §7.4"缺失文件"行的"镜像必备缺失"半边）。
        if (s.archiveNames.find(PackFormat::kRequiredHead) == s.archiveNames.end()) {
            return packError(IoErrorCode::PackRefIncomplete,
                             "镜像必备条目缺失（§7.1 恒在集）",
                             {{"entry", PackFormat::kRequiredHead}});
        }
        if (s.archiveNames.find(PackFormat::kRequiredProjectJson)
            == s.archiveNames.end()) {
            return packError(IoErrorCode::PackRefIncomplete,
                             "镜像必备条目缺失（§7.1 恒在集）",
                             {{"entry", PackFormat::kRequiredProjectJson}});
        }
        bool anyRevision = false;
        for (const std::string& name : s.archiveNames) {
            if (name.rfind(PackFormat::kRevisionsPrefix, 0) == 0) {
                anyRevision = true;
                break;
            }
        }
        if (!anyRevision) {
            return packError(IoErrorCode::PackRefIncomplete,
                             "revisions/ 无任何条目（§7.1 恒在集）",
                             {{"entry", PackFormat::kRevisionsPrefix}});
        }
        return IoError{};
    }

    /// ⑥ 引用完整性（§7.3⑥"清单内文件间引用——文件层核对"）：manifest
    /// 行对归档条目的引用闭合（缺失清单汇总——V29"断裂链定位"；修订闭
    /// 包等领域校验归 project 于临时目录执行——§7.3⑥ 括注，io 不越界
    /// 判定对象存在性的业务语义，N-5）。
    static IoError step6ReferenceIntegrity(PackageImportSession::Impl& s)
    {
        if (s.missingEntries.empty()) {
            return IoError{};
        }
        // 断裂链定位：首个缺失条目入 params（确定性——manifest 序），全
        // 量清单入 detail（§7.4"缺失文件"行"拒绝＋缺失清单"）。
        IoError e = packError(IoErrorCode::PackRefIncomplete,
                              "manifest 引用完整性断裂（缺失条目清单见 detail）",
                              {{"entry", s.missingEntries.front()},
                               {"missing-count", std::to_string(s.missingEntries.size())}});
        for (const std::string& missing : s.missingEntries) {
            e.detail += missing + "\n";
        }
        return e;
    }

    // -----------------------------------------------------------------
    // 报告定稿与杂项
    // -----------------------------------------------------------------

    /// 报告定稿（⑦——终态/计数/条目结论/账本快照；V12 的 ledger 观测面）。
    static void finalizeReport(PackageImportSession::Impl& s, IBudgetGuard* guard,
                               BudgetScopeId scope)
    {
        s.lastReport.finalState = s.state;
        s.lastReport.manifestEntries = s.manifest.size();
        std::uint64_t verified = 0;
        for (const ZipEntryVerifyReport& r : s.entryResults) {
            if (r.error.code == IoErrorCode::Ok) {
                ++verified;
            }
        }
        s.lastReport.verifiedEntries = verified;
        s.lastReport.totalBytes = s.totalBytes;
        s.lastReport.entryResults = s.entryResults;
        s.lastReport.stagingRootDisplay = platform::displayOf(s.area.rootPath());
        if (guard != nullptr && scope.value != 0) {
            s.lastReport.ledgerSnapshot = guard->ledger(scope);
        }
    }

    /// 错误参数前置条目名（定位要素——§7.4 各行的"定位"列）。
    static IoError prependEntry(IoError e, const std::string& entry)
    {
        e.params.emplace(e.params.begin(), "entry", entry);
        return e;
    }

    PackageIoFacilities m_facilities;
    std::unique_ptr<IStructuredDataReader> m_jsonReader;   ///< 包契约 JSON 解析（profile=null）
};

// =====================================================================
// 导出器实现（§7.2 六步协议）
// =====================================================================

class PackageExporterImpl final : public IPackageExporter {
public:
    explicit PackageExporterImpl(PackageIoFacilities facilities)
        : m_facilities(std::move(facilities))
    {
        // 缺省设施＝产品真实实现（同导入器）。
        if (!m_facilities.safePath) {
            m_facilities.safePath = makeSafePathResolver();
        }
        if (!m_facilities.budgetFactory) {
            m_facilities.budgetFactory = [] { return makeBudgetGuard(); };
        }
        if (!m_facilities.tempAreas) {
            m_facilities.tempAreas = makeTempAreaManager();
        }
        if (!m_facilities.atomicFiles) {
            m_facilities.atomicFiles = makeAtomicFileWriter();
        }
    }

    IoResult<PackageExportReport> export_(ISnapshotFileSource& consistentView,
                                          const PackageExportOptions& options,
                                          IBudgetGuard* budget, IoCancelToken* cancel,
                                          IoProgressCallback progress) override
    {
        IoResult<PackageExportReport> out;
        if (cancelHit(cancel)) {
            out.error.code = IoErrorCode::Cancelled;
            return out;
        }
        std::error_code ec;
        if (options.targetFile.empty() || options.targetFile.parent_path().empty()
            || !std::filesystem::is_directory(options.targetFile.parent_path(), ec)) {
            return fail<PackageExportReport>(platform::makeOsError(
                ERROR_PATH_NOT_FOUND, true, options.targetFile.parent_path(),
                "导出目标父目录不存在（§9.9 前置）"));
        }
        // NeverOverwrite 预检（fail-fast——避免全量暂存后才在替换时拒绝；
        // OverwriteAtomic 不预检——V23 的"先前完整版本"场景要求替换前
        // 既有文件不被触碰，预检只读无副作用）。
        if (options.replace == ReplacePolicy::NeverOverwrite
            && std::filesystem::exists(options.targetFile, ec)) {
            return fail<PackageExportReport>(
                packError(IoErrorCode::PackTargetExists,
                          "导出目标已存在且策略为 NeverOverwrite（§4.6）",
                          {{"target", platform::displayOf(options.targetFile)}}));
        }

        // 会话级预算 scope（导出通道——§4.5.2"普通资源读取至少强制
        // SingleFileBytes/TotalBytes"；TempAreaBytes 是本通道主维）。
        IBudgetGuardPtr ownedGuard;
        if (budget == nullptr) {
            ownedGuard = m_facilities.budgetFactory();   // 缺省守卫——防御性限检
            budget = ownedGuard.get();
        }
        PackScopeSession budgetScope(budget, BudgetSpec::productDefault());

        // 步骤①~⑥执行（一致视图锁定在步骤链内——枚举恰一次，快照面
        // 单一；失败/取消统一走收尾——见下）。

        // 步骤①-pre：临时区（§7.2①——目标同目录旁 `.<name>.<8hex>.tmp/`；
        // 同目录→同卷——与原子替换衔接，IO-D08）。
        const std::wstring stem = options.targetFile.stem().wstring();
        IoResult<TempAreaSession> area = m_facilities.tempAreas->create(
            TempAreaSpec{TempAreaRole::PackExport, options.targetFile.parent_path(),
                         platform::wideToUtf8(stem)},
            cancel);
        if (!area) {
            return fail<PackageExportReport>(std::move(area.error));
        }
        TempAreaSession& areaRef = area.value;

        // 失败/取消统一走收尾（abort 原子会话［若已 prepare］＋清理临时
        // 区——目标零写入的结构性保证，§7.2 场景表"取消/失败"行）。
        IoError failure = runExportSteps(consistentView, options, budgetScope, cancel,
                                         progress, areaRef, out.value);
        IoResult<void> cleaned = m_facilities.tempAreas->cleanup(areaRef);
        if (failure.code == IoErrorCode::Ok && cleaned) {
            out.error = IoError{};   // 成功（报告已由步骤链填充）
            return out;
        }
        if (failure.code == IoErrorCode::Ok) {
            // 自检已过而清理失败——包已就位但临时区残留：§4.6"替换后失
            // 败"形态的临时区版——目标已正确，残留清理失败仅开发级诊断
            // （§7.2 场景表"失败"行后半）；此处如实上报清理失败。
            out.error = std::move(cleaned.error);
            return out;
        }
        out.error = std::move(failure);
        return out;
    }

private:
    /// 步骤①~⑥执行链（失败即停；成功时 report 已填充——收尾只管清理）。
    IoError runExportSteps(ISnapshotFileSource& view, const PackageExportOptions& options,
                           PackScopeSession& budget, IoCancelToken* cancel,
                           const IoProgressCallback& progress, TempAreaSession& area,
                           PackageExportReport& report)
    {
        // 步骤①：锁定一致数据视图（§7.2——"枚举自当前 HEAD 闭包（不扫
        // 描目录！）"由实现方 project 保证，io 不复核来源；枚举恰一次）。
        emitProgress(progress, 0, 1, "enumerate");
        IoResult<std::vector<PackFileEntry>> snapshot = view.enumerate();
        if (!snapshot) {
            return snapshot.error;
        }
        emitProgress(progress, 1, 1, "enumerate");

        // 步骤②-pre：快照清单防御校验（io 只读清单内路径——越权名字＝
        // 实现方契约违约，防御性拒绝；折叠键重复同理）。
        std::set<std::string> seen;
        for (const PackFileEntry& e : snapshot.value) {
            if (!isSafePackRelativeName(e.path)) {
                return packError(IoErrorCode::FormatInternal,
                                 "快照清单含非法包内相对名（实现方契约违约）",
                                 {{"entry", e.path}});
            }
            std::string folded = e.path;
            std::transform(folded.begin(), folded.end(), folded.begin(),
                           [](unsigned char c) {
                               return static_cast<char>(
                                   c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
                           });
            if (!seen.insert(folded).second) {
                return packError(IoErrorCode::FormatInternal,
                                 "快照清单含折叠键重复条目（实现方契约违约）",
                                 {{"entry", e.path}});
            }
        }

        // 步骤②：逐文件 read→摘要→暂存（§7.2②；"清单外文件一概不读"——
        // io 只迭代快照清单，V32 的结构性依据）。单文件读失败按快照重读
        // 一次（§7.2 场景表"导出期间后台写入并存"行——稳定版本语义/V18）；
        // 仍失败→导出失败清理，不产出混合版本包。
        const std::filesystem::path areaRoot = area.rootPath();
        std::vector<ManifestRow> manifest;
        manifest.reserve(snapshot.value.size());
        std::uint64_t totalBytes = 0;
        for (std::size_t i = 0; i < snapshot.value.size(); ++i) {
            const PackFileEntry& entry = snapshot.value[i];
            if (cancelHit(cancel)) {
                return IoError{IoErrorCode::Cancelled, {}, {}};
            }
            emitProgress(progress, i, snapshot.value.size(), "stage");

            // 单文件上限预检（§4.5.2——先拒超限再读取；限额取会话规格
            // 当前值——PackScopeSession 保留的收紧后拷贝）。
            if (entry.size > budget.spec.limit(BudgetDimension::SingleFileBytes)) {
                return packError(IoErrorCode::SecBudgetFile,
                                 "快照条目声明大小超单文件预算（比较型三要素）",
                                 {{"entry", entry.path},
                                  {"actual", std::to_string(entry.size)},
                                  {"limit", std::to_string(
                                                budget.spec.limit(
                                                    BudgetDimension::SingleFileBytes))},
                                  {"unit", "bytes"}});
            }
            // 稳定版本重读（一次——§7.2 场景表原文"按快照重读一次"）。
            IoResult<IoString> bytes = view.read(entry.path);
            if (!bytes) {
                bytes = view.read(entry.path);
                if (!bytes) {
                    return prependEntry(bytes.error, entry.path);
                }
            }
            // 会话记账（TempAreaBytes＋TotalBytes——实际字节数；声明与
            // 实际以实际入账——快照 size 是提示面）。
            if (IoResult<void> r = budget.charge(BudgetDimension::TempAreaBytes,
                                                 static_cast<std::uint64_t>(bytes.value.size()));
                !r) {
                return prependEntry(r.error, entry.path);
            }
            if (IoResult<void> r = budget.charge(BudgetDimension::TotalBytes,
                                                 static_cast<std::uint64_t>(bytes.value.size()));
                !r) {
                return prependEntry(r.error, entry.path);
            }
            // 摘要＋暂存（manifest 行＝实际字节的事实——哈希与大小都从
            // 写入内容复算，包自洽由此结构性成立）。
            core::ContentDigester digester;
            digester.update(bytes.value.data(), bytes.value.size());
            ManifestRow row;
            row.path = entry.path;
            row.size = static_cast<std::uint64_t>(bytes.value.size());
            row.sha256 = digester.finalize();
            std::error_code ec;
            std::filesystem::path staged = areaRoot;
            for (const std::string& seg : splitPackPath(entry.path)) {
                staged /= platform::utf8ToWide(seg);
            }
            std::filesystem::create_directories(staged.parent_path(), ec);
            if (ec) {
                return osOrDiskFull(ec, staged.parent_path(), "暂存目录创建失败（§7.2②）");
            }
            if (!writeBytesToFile(staged, bytes.value, ec)) {
                return osOrDiskFull(ec, staged, "暂存写入失败（§7.2②）");
            }
            totalBytes += row.size;
            manifest.push_back(std::move(row));
        }
        emitProgress(progress, snapshot.value.size(), snapshot.value.size(), "stage");

        // 步骤③：manifest.json＋rwpack.json canonical 写出（§7.2③——
        // manifest 条目按 path 字典序〔§7.1 manifest 行〕；rwpack.json
        // 元数据来自选项增补字段——PackageExportOptions 类注）。
        emitProgress(progress, 0, 1, "manifest");
        std::sort(manifest.begin(), manifest.end(),
                  [](const ManifestRow& a, const ManifestRow& b) { return a.path < b.path; });
        JsonDocument manifestDoc;
        manifestDoc.root.type = JsonValue::Type::Object;
        addMember(manifestDoc.root, "schemaVersion",
                  jsonString(PackFormat::kManifestSchemaVersion));
        JsonValue entriesArr;
        entriesArr.type = JsonValue::Type::Array;
        for (const ManifestRow& row : manifest) {
            JsonValue item;
            item.type = JsonValue::Type::Object;
            addMember(item, "path", jsonString(row.path));
            addMember(item, "size", jsonInt(static_cast<std::int64_t>(row.size)));
            addMember(item, "sha256", jsonString(digestToHex(row.sha256)));
            entriesArr.items.push_back(std::move(item));
        }
        addMember(manifestDoc.root, "entries", std::move(entriesArr));
        IoResult<IoString> manifestBytes =
            canonicalizeJson(manifestDoc, JsonWriteOptions{});
        if (!manifestBytes) {
            return packError(IoErrorCode::FormatPackManifest,
                             "manifest canonical 化失败：" + manifestBytes.error.detail);
        }
        IoResult<core::Digest256> manifestDigest =
            digestCanonicalJson(manifestDoc, JsonWriteOptions{});
        if (!manifestDigest) {
            return packError(IoErrorCode::FormatPackManifest,
                             "manifest 摘要失败：" + manifestDigest.error.detail);
        }
        std::error_code ec;
        if (!writeBytesToFile(areaRoot / L"manifest.json", manifestBytes.value, ec)) {
            return osOrDiskFull(ec, areaRoot / L"manifest.json", "manifest 暂存写入失败");
        }

        JsonDocument rwpackDoc;
        rwpackDoc.root.type = JsonValue::Type::Object;
        addMember(rwpackDoc.root, "formatId", jsonString(PackFormat::kFormatId));
        addMember(rwpackDoc.root, "schemaVersion",
                  jsonInt(static_cast<std::int64_t>(PackFormat::kSchemaVersion)));
        addMember(rwpackDoc.root, "createdAtUtc",
                  jsonString(options.createdAtUtcIso8601.empty()
                                 ? nowUtcIso8601()
                                 : options.createdAtUtcIso8601));
        addMember(rwpackDoc.root, "createdWithToolVersion",
                  jsonString(options.createdWithToolVersion.empty()
                                 ? std::string("industrialrobot io/0.1 (IO-T06)")
                                 : options.createdWithToolVersion));
        addMember(rwpackDoc.root, "sourceProjectId", jsonString(options.sourceProjectId));
        JsonValue content;
        content.type = JsonValue::Type::Object;
        addMember(content, "headRevisionId", jsonString(options.headRevisionId));
        addMember(content, "fileCount", jsonInt(static_cast<std::int64_t>(manifest.size())));
        addMember(content, "totalBytes", jsonInt(static_cast<std::int64_t>(totalBytes)));
        addMember(content, "totalDigest", jsonString(digestToHex(manifestDigest.value)));
        addMember(rwpackDoc.root, "content", std::move(content));
        IoResult<IoString> rwpackBytes = canonicalizeJson(rwpackDoc, JsonWriteOptions{});
        if (!rwpackBytes) {
            return packError(IoErrorCode::FormatPackManifest,
                             "rwpack.json canonical 化失败：" + rwpackBytes.error.detail);
        }
        if (!writeBytesToFile(areaRoot / L"rwpack.json", rwpackBytes.value, ec)) {
            return osOrDiskFull(ec, areaRoot / L"rwpack.json", "rwpack.json 暂存写入失败");
        }
        emitProgress(progress, 1, 1, "manifest");

        // 步骤④：压缩为 .rwpack（§7.2④——zip64〔libzip 原生〕；进度/取
        // 消每文件；条目时间戳统一 createdAtUtc——IO-D11）。payload 文件
        // 经 zip_source_file 从暂存盘流式读入——内存有界。
        emitProgress(progress, 0, manifest.size() + 2, "compress");
        const std::filesystem::path zipPath = areaRoot / options.targetFile.filename();
        zip_error_t zipErr;
        zip_error_init(&zipErr);
        // 打开链（写侧——可写 source 才能建归档；filep_create 系只读 ops，
        // 不可用）。Windows 宽路径经 libzip 的 win32 named 源（内部
        // CreateFileW——全名空间无损）；条目文件名仍以窄 UTF-8 传入
        // （ZIP_FL_ENC_UTF_8——§7.1 条目名约定）。POSIX 窄路径 zip_open
        // 由 libzip zip_source_file_create 承载（同 zip_open 本体链路）。
        zip_source_t* src = nullptr;
#ifdef _WIN32
        src = zip_source_win32w_create(zipPath.c_str(), 0, -1, &zipErr);
#else
        src = zip_source_file_create(zipPath.string().c_str(), 0, -1, &zipErr);
#endif
        if (src == nullptr) {
            const char* detail = zip_error_strerror(&zipErr);
            return packError(IoErrorCode::FormatPackZip,
                             std::string("zip 源创建失败（§7.2④）：")
                                 + (detail != nullptr ? detail : "未知"));
        }
        zip_t* za = zip_open_from_source(src, ZIP_CREATE, &zipErr);
        if (za == nullptr) {
            const char* detail = zip_error_strerror(&zipErr);
            zip_source_free(src);   // 失败时 source 由 libzip 契约回收（文件句柄随之）
            return packError(IoErrorCode::FormatPackZip,
                             std::string("zip 归档创建失败（§7.2④）：")
                                 + (detail != nullptr ? detail : "未知"));
        }
        // 归档内条目时间戳（IO-D11：统一取 createdAtUtc——可复现导出）。
        const std::time_t mtimeEpoch = iso8601ToEpoch(
            options.createdAtUtcIso8601.empty() ? std::string() : options.createdAtUtcIso8601);
        auto addEntryFromFile = [&](const char* name,
                                    const std::filesystem::path& file) -> IoError {
            if (cancelHit(cancel)) {
                return IoError{IoErrorCode::Cancelled, {}, {}};
            }
            // 条目源＝只读文件源；窄名按 UTF-8 传入（libzip Windows 侧内部
            // CP_UTF8→UTF-16 无损转换——zip_source_file_win32_utf8.c 链路）。
            const std::string entryPathUtf8 = platform::wideToUtf8(file.wstring());
            zip_source_t* entrySrc = zip_source_file(za, entryPathUtf8.c_str(), 0, -1);
            if (entrySrc == nullptr) {
                return packError(IoErrorCode::FormatPackZip,
                                 std::string("zip 条目源创建失败：") + name);
            }
            const zip_int64_t idx =
                zip_file_add(za, name, entrySrc, ZIP_FL_ENC_UTF_8);
            if (idx < 0) {
                zip_source_free(entrySrc);   // 未入档的 source 手工回收
                return packError(IoErrorCode::FormatPackZip,
                                 std::string("zip 条目添加失败：") + name);
            }
            zip_set_file_compression(za, static_cast<zip_uint64_t>(idx),
                                     ZIP_CM_DEFLATE, 0);
            if (mtimeEpoch >= 0) {
                zip_file_set_mtime(za, static_cast<zip_uint64_t>(idx),
                                   static_cast<zip_int64_t>(mtimeEpoch), 0);
            }
            return IoError{};
        };
        IoError stepErr = addEntryFromFile(PackFormat::kRwpackEntryName,
                                           areaRoot / L"rwpack.json");
        if (stepErr.code == IoErrorCode::Ok) {
            stepErr = addEntryFromFile(PackFormat::kManifestEntryName,
                                       areaRoot / L"manifest.json");
        }
        for (std::size_t i = 0; stepErr.code == IoErrorCode::Ok && i < manifest.size(); ++i) {
            std::filesystem::path staged = areaRoot;
            for (const std::string& seg : splitPackPath(manifest[i].path)) {
                staged /= platform::utf8ToWide(seg);
            }
            stepErr = addEntryFromFile(manifest[i].path.c_str(), staged);
            emitProgress(progress, i + 1, manifest.size() + 2, "compress");
        }
        emitProgress(progress, manifest.size() + 2, manifest.size() + 2, "compress");
        if (stepErr.code == IoErrorCode::Ok) {
            if (cancelHit(cancel)) {
                stepErr = IoError{IoErrorCode::Cancelled, {}, {}};
            }
        }
        if (stepErr.code == IoErrorCode::Ok) {
            if (zip_close(za) != 0) {
                // close 失败＝中央目录写败——包不可用；discard 兜底回收。
                zip_discard(za);
                return packError(IoErrorCode::FormatPackZip,
                                 "zip 收尾失败（中央目录写入——§7.2④）");
            }
        } else {
            zip_discard(za);   // 取消/失败——丢弃半成品归档（临时区收尾清）
            return stepErr;
        }

        // 步骤⑤：完整性自检（§7.2⑤——重新打开包→全量条目哈希复算比对）。
        emitProgress(progress, 0, 1, "verify");
        IoResult<std::unique_ptr<IZipChannel>> verify = openZipChannel(zipPath);
        if (!verify) {
            return verify.error;
        }
        std::vector<ZipManifestEntry> verifyList;
        verifyList.reserve(manifest.size());
        for (const ManifestRow& row : manifest) {
            ZipManifestEntry v;
            v.path = row.path;
            v.size = row.size;
            v.sha256 = row.sha256;
            verifyList.push_back(std::move(v));
        }
        IoResult<std::vector<ZipEntryVerifyReport>> verifyReport =
            verify.value->verifyManifestEntries(verifyList, budget.guard, budget.scope,
                                                cancel);
        if (!verifyReport) {
            return verifyReport.error;
        }
        for (const ZipEntryVerifyReport& r : verifyReport.value) {
            if (r.error.code != IoErrorCode::Ok) {
                // 自检失败＝自产包不自洽（实现缺陷级）——拒绝交付。
                return packError(IoErrorCode::PackHashMismatch,
                                 "导出完整性自检失败（§7.2⑤）：" + r.error.detail,
                                 {{"entry", r.path}});
            }
        }
        emitProgress(progress, 1, 1, "verify");

        // 步骤⑥：原子替换到目标（§7.2⑥/IAtomicFileWriter——分块搬运大
        // 包不整体驻留内存；取消/失败发生在替换前→目标不变，V23）。
        emitProgress(progress, 0, 1, "commit");
        IoResult<AtomicTarget> target =
            m_facilities.atomicFiles->prepare(options.targetFile, options.replace);
        if (!target) {
            return target.error;
        }
        {
            // 分块搬运（1 MiB——读侧 ifstream/写侧 AtomicTarget::write；
            // 每块取消检查点）。
            std::ifstream in(zipPath, std::ios::binary);
            if (!in.is_open()) {
                (void)m_facilities.atomicFiles->abort(target.value);
                return platform::makeOsError(ERROR_ACCESS_DENIED, false, zipPath,
                                             "暂存包回读失败（§7.2⑥ 前）");
            }
            std::string chunk(1 << 20, '\0');
            for (;;) {
                if (cancelHit(cancel)) {
                    (void)m_facilities.atomicFiles->abort(target.value);
                    return IoError{IoErrorCode::Cancelled, {}, {}};
                }
                in.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
                const std::streamsize got = in.gcount();
                if (got > 0) {
                    IoResult<void> w =
                        target.value.write(std::string_view(chunk.data(),
                                                            static_cast<std::size_t>(got)));
                    if (!w) {
                        (void)m_facilities.atomicFiles->abort(target.value);
                        return w.error;
                    }
                }
                if (in.eof()) {
                    break;
                }
                if (!in) {
                    (void)m_facilities.atomicFiles->abort(target.value);
                    return platform::makeOsError(ERROR_READ_FAULT, false, zipPath,
                                                 "暂存包回读失败（§7.2⑥ 前）");
                }
            }
        }
        IoResult<void> committed = m_facilities.atomicFiles->commit(target.value);
        if (!committed) {
            (void)m_facilities.atomicFiles->abort(target.value);
            return committed.error;
        }
        emitProgress(progress, 1, 1, "commit");

        // 报告填充（成功路径——收尾清理由调用方 export_ 完成）。
        report.entryCount = manifest.size();
        report.totalBytes = totalBytes;
        report.manifestDigest = manifestDigest.value;
        report.integritySelfCheckPassed = true;
        report.targetDisplay = platform::displayOf(options.targetFile);
        return IoError{};
    }

    static IoError prependEntry(IoError e, const std::string& entry)
    {
        e.params.emplace(e.params.begin(), "entry", entry);
        return e;
    }

    PackageIoFacilities m_facilities;
};

// =====================================================================
// 装配工厂（§9.11 IoRuntime 落位前的直接装配形态——Package.hpp 类注）
// =====================================================================

std::unique_ptr<IPackageImporter> makePackageImporter(PackageIoFacilities facilities)
{
    return std::make_unique<PackageImporterImpl>(std::move(facilities));
}

std::unique_ptr<IPackageExporter> makePackageExporter(PackageIoFacilities facilities)
{
    return std::make_unique<PackageExporterImpl>(std::move(facilities));
}

} // namespace sdurws::ird::io
