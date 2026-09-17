/**
 * @file   TempArea.cpp
 * @brief  临时区管理器的真实实现——等价键互斥（EquivKeyMutex）、崩溃残留
 *         回收（io-session.json 标记识别）、幂等清理与残留清单报告。
 *
 * 设计依据：
 *   - units/io.md §7.5（创建/互斥/清理/残留报告/崩溃残留五行全表）、
 *     §7.6（CleanupFailed 终态可重试、残留不可被误识别）、§4.2.4（等价
 *     类键——同项目不同拼写单会话）、§4.2.5（错误四分类）、§9.10（契约
 *     表＋等价增补 availableBytes）
 *   - 需求 PM-05（取消即清理）、CON-03（固化中转位授权口径）
 *   - 任务契约 tasks/foundation/IO-T06.json acceptance 2（V20/V21 注入面
 *     ——availableBytes/cleanup 虚方法）
 *
 * 会话命名（§7.5 创建行原文形态）：
 *   PackImport → `.rwpack-import-<8hex>`（baseDir＝发布目标父目录）
 *   PackExport → `.<nameHint>.<8hex>.tmp`（baseDir＝目标文件目录）
 *   Solidify   → `solidify-<8hex>`（baseDir＝project 授权的 .staging/tmp；
 *                授权段校验归调用方——本类不越权复核 .rwdesign 布局，
 *                §2.1 N-1：.rwdesign 写权归 project）
 *
 * 崩溃残留回收（§7.5"崩溃残留"行）：create 时扫描 baseDir 下同前缀目
 * 录；**仅当**目录内含 io-session.json（pid＋时间戳）才 remove_all——
 * 该标记只能由本管理器写入，用户目录不会有（防误删）；回收失败不阻断
 * （新会话用新 8hex 后缀，与残留无碰撞）。
 *
 * 清理顺序（§7.5"清理"行）：自底向上（先文件后目录逐层）——
 * std::filesystem::remove_all 一次完成等价语义；本实现按"逐项删除＋
 * 失败即停并登记剩余"驱动，以支撑 §7.6 残留清单（哪些路径删不掉）。
 */

#include <sdurws/ird/io/TempArea.hpp>

#include <sdurws/ird/io/SafePath.hpp>   // EquivKeyMutex——§4.2.4/§9.10 会话互斥原语

#include "IoPlatform.hpp"   // utf8ToWide/wideToUtf8/displayOf/makeOsError/randomHex8

#include <chrono>
#include <ctime>
#include <fstream>
#include <mutex>
#include <process.h>   // _getpid——io-session.json 标记的 pid 字段（§7.5）
#include <string>
#include <vector>

namespace sdurws::ird::io {

// =====================================================================
// TempAreaSession（pimpl 成员——头文件前向声明的完整定义落位）
// =====================================================================

struct TempAreaSession::Impl {
    TempAreaRole role = TempAreaRole::PackImport;
    std::filesystem::path root;          ///< 会话根（隐藏前缀目录）
    bool active = false;                 ///< create 成功→cleanup 成功前为真
    std::shared_ptr<EquivKeyMutex> mutexTable;   ///< 进程级租约表（cleanup 时释放）
    std::string equivKey;                ///< 本会话持有的等价键（release 用）

    ~Impl()
    {
        // RAII 兜底：调用方漏调 cleanup 时尽力删除会话根（失败无补偿面
        // ——残留由 §7.5 标记机制在下次同前缀会话回收）；租约必然释放
        // （否则进程内互斥泄漏）。
        if (active) {
            std::error_code ec;
            std::filesystem::remove_all(root, ec);
        }
        if (mutexTable) {
            mutexTable->release(equivKey);
        }
    }

    Impl() = default;
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
};

TempAreaSession::~TempAreaSession() = default;
TempAreaSession::TempAreaSession(TempAreaSession&& other) noexcept = default;
TempAreaSession& TempAreaSession::operator=(TempAreaSession&& other) noexcept = default;

bool TempAreaSession::isActive() const noexcept
{
    return m_impl != nullptr && m_impl->active;
}

const std::filesystem::path& TempAreaSession::rootPath() const noexcept
{
    static const std::filesystem::path kEmpty;
    return m_impl != nullptr ? m_impl->root : kEmpty;
}

TempAreaRole TempAreaSession::role() const noexcept
{
    return m_impl != nullptr ? m_impl->role : TempAreaRole::PackImport;
}

TempAreaSession TempAreaSession::adopt(std::shared_ptr<Impl> impl)
{
    TempAreaSession out;
    out.m_impl = std::move(impl);
    return out;
}

std::shared_ptr<TempAreaSession::Impl> TempAreaSession::impl() const noexcept
{
    return m_impl;
}

// =====================================================================
// 内部助手（匿名命名空间）
// =====================================================================

namespace {

/// 会话根的目录名前缀（§7.5 创建行形态——role 决定）。
std::wstring prefixForRole(TempAreaRole role, const std::string& nameHint)
{
    switch (role) {
    case TempAreaRole::PackImport:
        return L".rwpack-import-";
    case TempAreaRole::PackExport:
        // `.<name>.<8hex>.tmp`——nameHint 为导出目标文件名去扩展名（调用
        // 方提供；空提示退化为 "rwpack"——命名仍满足隐藏前缀约束）。
        return L"." + platform::utf8ToWide(
                           nameHint.empty() ? std::string("rwpack") : nameHint) + L".";
    case TempAreaRole::Solidify:
        return L"solidify-";
    }
    return L".rwpack-import-";   // 防御分支（枚举封闭——不可达）
}

/// 等价键（§4.2.4：折叠＋分隔符归一＋词法消解；本处消费方只有互斥表与
/// 残留识别——不写回、不替换调用方路径，SP-10）。键＝baseDir 规范化折叠
/// ＋角色前缀——同一 baseDir 的同类会话互斥，不同 baseDir/角色互不干扰。
std::string equivKeyFor(TempAreaRole role, const std::filesystem::path& baseDir,
                        const std::wstring& prefix)
{
    std::error_code ec;
    std::filesystem::path canon = std::filesystem::weakly_canonical(baseDir, ec);
    if (canon.empty()) {
        canon = baseDir;   // canonical 不可得（网络盘等）——退化为字面折叠（§4.2.4）
    }
    std::wstring wide = canon.wstring();
    for (wchar_t& ch : wide) {
        if (ch == L'/') {
            ch = L'\\';   // 分隔符归一（§4.2.1 步骤 2 比较面）
        }
        if (ch >= L'A' && ch <= L'Z') {
            ch = static_cast<wchar_t>(ch - L'A' + L'a');   // 大小写折叠（NTFS 口径）
        }
    }
    return platform::wideToUtf8(wide) + "|" + platform::wideToUtf8(prefix);
}

/// io-session.json 标记内容（§7.5 互斥行：pid＋时间戳——崩溃残留识别
/// 依据；内容只作识别不作契约数据，格式固定最小化）。
std::string markerContent()
{
    const std::time_t now =
        std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm utc{};
#ifdef _MSC_VER
    gmtime_s(&utc, &now);
#else
    gmtime_r(&now, &utc);
#endif
    char buf[64] = {};
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &utc);
    return std::string("{\"pid\":") + std::to_string(static_cast<long long>(::_getpid()))
           + std::string(",\"createdUtc\":\"") + buf + std::string("\"}");
}

/// 残留是否为本 io 会话（含 io-session.json 标记——§7.5"仅当…才清"）。
bool hasIoSessionMarker(const std::filesystem::path& dir)
{
    std::error_code ec;
    return std::filesystem::exists(dir / L"io-session.json", ec) && !ec;
}

/// 递归清理会话根：逐项删除，遇失败即停并返回剩余残留（§7.6 残留清单
/// 的数据源）；全部成功返回空表。删除顺序＝remove_all 语义（自底向上），
/// 失败项与其未触达的祖先都会出现在残留表——满足"列出残留绝对路径"
/// 的报告语义。
std::vector<std::filesystem::path> removeTreeRecordingResidue(const std::filesystem::path& root)
{
    std::vector<std::filesystem::path> residues;
    std::error_code ec;
    // 先整体 remove_all：失败时再逐项枚举登记残留（常态一次成功——
    // 逐项枚举仅失败路径走，成本不进快路径）。
    std::filesystem::remove_all(root, ec);
    if (!ec) {
        return residues;   // 全部清除（含根）
    }
    // remove_all 失败：枚举根下未删净的条目登记（脱敏 display 由调用方
    // 统一处理——此处收集文件系统事实）。根自身保留（活动会话语义）。
    std::filesystem::recursive_directory_iterator it(
        root, std::filesystem::directory_options::skip_permission_denied, ec);
    std::filesystem::recursive_directory_iterator end;
    while (!ec && it != end) {
        residues.push_back(it->path());
        it.increment(ec);   // 失败即停——枚举不到的项无法登记，如实缺报
    }
    residues.push_back(root);
    return residues;
}

// =====================================================================
// 真实管理器
// =====================================================================

/// §7.5 协议的真实实现（V20/V21 故障注入经 PackageIoFacilities 换 fake）。
class TempAreaManagerImpl final : public ITempAreaManager {
public:
    TempAreaManagerImpl()
        : m_mutexTable(std::make_shared<EquivKeyMutex>())   // 进程级租约表
    {
    }

    IoResult<TempAreaSession> create(const TempAreaSpec& spec,
                                     IoCancelToken* cancel) override
    {
        IoResult<TempAreaSession> out;
        if (spec.baseDir.empty()) {
            out.error.code = IoErrorCode::FormatInternal;
            out.error.detail = "TempAreaManager::create 前置违约（baseDir 为空）";
            return out;
        }
        // 取消检查点（创建流程入口——协作语义）。
        if (cancel != nullptr && cancel->isCancelled()) {
            out.error.code = IoErrorCode::Cancelled;
            return out;
        }
        std::error_code ec;
        if (!std::filesystem::is_directory(spec.baseDir, ec)) {
            // baseDir 缺失/不可达＝环境错误四分类（调用方负责保证存在——
            // PackImport 的 baseDir＝发布目标父目录，§9.9 前置行）。
            out.error = platform::makeOsError(ERROR_PATH_NOT_FOUND, true, spec.baseDir,
                                              "create 前置失败（baseDir 不存在）");
            return out;
        }

        const std::wstring prefix = prefixForRole(spec.role, spec.nameHint);
        const std::string key = equivKeyFor(spec.role, spec.baseDir, prefix);

        // 步骤 1：等价键互斥（§4.2.4/§9.10——同一项目同类会话至多一个；
        // V10"单会话；无重复临时区"的机制落点）。tryAcquire 失败＝另一
        // 会话在场——不排队不等待（等待策略归调用方，§9.10 原语注）。
        if (!m_mutexTable->tryAcquire(key)) {
            out.error.code = IoErrorCode::ResLockConflict;
            out.error.params.emplace_back("equivKey", key);
            out.error.detail = "同前缀临时区会话已在场（等价键互斥——§7.5 互斥行）";
            return out;
        }

        // 步骤 2：崩溃残留回收（§7.5"崩溃残留"行）——扫描同前缀目录，
        // 含标记者清理。回收失败不阻断（新 8hex 无碰撞）；无标记者绝不
        // 触碰（"绝不误删用户目录"）。
        for (std::filesystem::directory_iterator it(spec.baseDir, ec), end;
             !ec && it != end; it.increment(ec)) {
            if (cancel != nullptr && cancel->isCancelled()) {
                m_mutexTable->release(key);   // 取消——先归还租约再退出
                out.error.code = IoErrorCode::Cancelled;
                return out;
            }
            const std::wstring name = it->path().filename().wstring();
            // 前缀匹配＝本 io 家族的候选残留（隐藏前缀约束保证不误配）。
            if (name.size() > prefix.size() && name.compare(0, prefix.size(), prefix) == 0
                && it->is_directory(ec)) {
                if (hasIoSessionMarker(it->path())) {
                    // 含本 io 标记＝崩溃残留——清理（§7.5 唯一授权情形）。
                    std::error_code rmEc;
                    std::filesystem::remove_all(it->path(), rmEc);
                    // rmEc 置位＝回收失败：保留现场（下次会话再试），不阻断。
                }
                // 无标记＝非本 io 目录（即使前缀巧合相同）——绝不触碰。
            }
        }

        // 步骤 3：创建新会话根＋写入 io-session.json 标记（§7.5 创建行；
        // PackExport 的目录名带 `.tmp` 后缀——`.<name>.<8hex>.tmp` 原文
        // 形态；残留扫描按前缀匹配，后缀不影响回收识别）。
        auto impl = std::make_shared<TempAreaSession::Impl>();
        impl->role = spec.role;
        std::wstring leaf = prefix + platform::utf8ToWide(platform::randomHex8());
        if (spec.role == TempAreaRole::PackExport) {
            leaf += L".tmp";
        }
        impl->root = spec.baseDir / leaf;
        if (!std::filesystem::create_directories(impl->root, ec) || ec) {
            m_mutexTable->release(key);
            out.error = platform::makeOsError(ec ? static_cast<DWORD>(ec.value())
                                                 : ERROR_ALREADY_EXISTS,
                                              true, impl->root,
                                              "会话根创建失败（§7.5 创建行）");
            return out;
        }
        {
            // 标记写入（std::ofstream——二进制安全、UTF-8 内容、短生命周期）。
            std::ofstream marker(impl->root / L"io-session.json", std::ios::binary | std::ios::trunc);
            if (!marker.is_open()) {
                // 标记写不出＝会话不可回收（崩溃残留机制失效）——按环境
                // 错误拒绝创建（fail-fast：宁可不开会话，不留不可识别残留）。
                std::error_code rmEc;
                std::filesystem::remove_all(impl->root, rmEc);
                m_mutexTable->release(key);
                out.error = platform::makeOsError(ERROR_ACCESS_DENIED, true, impl->root,
                                                  "io-session.json 标记写入失败");
                return out;
            }
            marker << markerContent();
        }
        impl->active = true;
        impl->mutexTable = m_mutexTable;
        impl->equivKey = key;
        out.value = TempAreaSession::adopt(std::move(impl));
        return out;
    }

    IoResult<void> cleanup(TempAreaSession& session) override
    {
        IoResult<void> out;
        if (session.impl() == nullptr) {
            return out;   // 幂等：空句柄＝无可清理（失败路径统一调用形态）
        }
        TempAreaSession::Impl& impl = *session.impl();
        if (!impl.active) {
            return out;   // 幂等：已清理——重复 cleanup 合法（§9.10 原文）
        }
        const std::vector<std::filesystem::path> residues =
            removeTreeRecordingResidue(impl.root);
        if (!residues.empty()) {
            // §7.5 残留报告行：列出残留绝对路径（脱敏 display 形态）＋
            // §7.6 CleanupFailed 终态（会话保持活动，重试幂等）。params
            // 至多 16 条（防膨胀）；全量进 detail（内部诊断链消费）。
            out.error.code = IoErrorCode::PackCleanupFailed;
            const std::size_t cap = residues.size() < 16 ? residues.size() : 16;
            for (std::size_t i = 0; i < cap; ++i) {
                out.error.params.emplace_back("residual" + std::to_string(i),
                                              platform::displayOf(residues[i]));
            }
            out.error.params.emplace_back("residual-count", std::to_string(residues.size()));
            for (const std::filesystem::path& r : residues) {
                out.error.detail += platform::displayOf(r) + "\n";
            }
            out.error.detail += "清理失败（§7.6 CleanupFailed——残留均在隐藏前缀内，"
                                "重试 cleanup 幂等）";
            return out;
        }
        // 成功：会话转非活动＋归还等价键租约（下一会话可创建）。
        impl.active = false;
        if (impl.mutexTable) {
            impl.mutexTable->release(impl.equivKey);
            impl.mutexTable.reset();
        }
        return out;
    }

    IoResult<std::uint64_t> availableBytes(const TempAreaSession& session) const override
    {
        IoResult<std::uint64_t> out;
        if (session.impl() == nullptr || !session.impl()->active) {
            out.error.code = IoErrorCode::FormatInternal;
            out.error.detail = "availableBytes 前置违约（会话非活动态）";
            return out;
        }
        // 真实探测＝std::filesystem::space（§7.3 步骤④的可用面来源；
        // 提示性——写入失败的 IO-PACK-DISK-FULL 兜底仍是防线之二）。
        std::error_code ec;
        const auto s = std::filesystem::space(session.impl()->root, ec);
        if (ec) {
            out.error = platform::makeOsError(static_cast<DWORD>(ec.value()), false,
                                              session.impl()->root,
                                              "可用空间探测失败（§7.3④ 提示性探测）");
            return out;
        }
        out.value = s.available;
        return out;
    }

private:
    /// 进程级租约表（§9.10"进程级；并发安全"——本管理器实例的所有会话
    /// 共享；create/cleanup 内部经 EquivKeyMutex 自身的锁并发安全）。
    std::shared_ptr<EquivKeyMutex> m_mutexTable;
};

} // namespace

ITempAreaManagerPtr makeTempAreaManager()
{
    return std::make_shared<TempAreaManagerImpl>();
}

} // namespace sdurws::ird::io
