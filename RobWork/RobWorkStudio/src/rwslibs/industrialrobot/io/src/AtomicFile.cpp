/**
 * @file   AtomicFile.cpp
 * @brief  导出文件原子写出的真实实现——§4.6 协议逐步骤（同目录暂存→
 *         写入→FlushFileBuffers→ReplaceFile/MoveFileEx 原子替换/放弃）。
 *
 * 设计依据：
 *   - units/io.md §4.6（协议原文与失败分段）、§9.10（IAtomicFileWriter
 *     契约表）、§4.2.5（OS 错误四分类）、§7.5（8hex 会话标签的命名形态
 *     ——暂存文件 `<target>.<8hex>.tmp` 同族）
 *   - 需求 MDL-20（导出失败恢复先前输出）、PM-05（V23 结构性保证）
 *   - 任务契约 tasks/foundation/IO-T06.json acceptance 2（V23）
 *
 * Windows 语义口径（按 Microsoft Learn 公开文档；§4.2 同源纪律）：
 *   - CreateFileW 以 CREATE_NEW 创建暂存文件——同名碰撞（8hex 撞名的
 *     概率可忽略但仍防御）重掷标签重试；
 *   - FlushFileBuffers 把暂存内容压到介质（commit 的断电语义步骤）；
 *   - 目标已存在：ReplaceFileW 原子替换（保留目标属性语义、无窗口期）；
 *     目标不存在：MoveFileExW（MOVEFILE_REPLACE_EXISTING |
 *     MOVEFILE_WRITE_THROUGH）——同目录暂存保证同卷，二者均为原子操作。
 *   - 替换后暂存文件已不存在（被顶替/挪用）——无备份残留面，§4.6
 *     "删除 .tmp 残留"步骤天然满足，不产生"替换后失败"分支。
 *
 * 线程约束：会话单线程（§9.10）——Impl 无锁；different targets 并行安全。
 */

#include <sdurws/ird/io/AtomicFile.hpp>

#include "IoPlatform.hpp"   // displayOf/randomHex8/makeOsError/utf8ToWide——私有平台助手

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <random>
#include <string>

namespace sdurws::ird::io {

// =====================================================================
// randomHex8（IoPlatform.hpp 声明的唯一定义——进程级 PRNG＋原子计数）
// =====================================================================

namespace platform {

std::string randomHex8()
{
    // 进程级状态（函数级 static——首次调用初始化；并发安全由互斥与原子
    // 计数共同保证）：标签只求唯一性不求不可预测性（§7.5 命名形态——
    // 隐藏前缀＋随机后缀的防碰撞目的），故 PRNG 播种一次、计数器混入。
    static std::mutex s_mutex;
    static std::mt19937_64 s_rng(
        std::random_device{}() ^ static_cast<std::uint64_t>(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    static std::atomic<std::uint64_t> s_counter{0};

    std::uint64_t v = 0;
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        v = s_rng();
    }
    // 计数器异或混入——同 tick 内的两次调用也必然不同（64 位空间内）。
    v ^= s_counter.fetch_add(1, std::memory_order_relaxed) * 0x9E3779B97F4A7C15ull;

    static const char* kDigits = "0123456789abcdef";
    std::string out(8, '0');
    for (int i = 7; i >= 0; --i) {
        out[static_cast<std::size_t>(i)] = kDigits[v & 0xF];
        v >>= 4;
    }
    return out;
}

} // namespace platform

// =====================================================================
// AtomicTarget 会话承载（pimpl——句柄/状态不出公共头）
// =====================================================================

struct AtomicTarget::Impl {
    std::filesystem::path target;    ///< 发布目标（prepare 规范化后）
    std::filesystem::path temp;      ///< 暂存文件（目标同目录·同卷）
    HANDLE handle = INVALID_HANDLE_VALUE;   ///< 暂存文件写句柄（唯一所有权）
    bool active = false;             ///< 会话状态（prepare 成功→commit/abort 前为真）

    ~Impl()
    {
        // RAII 兜底：调用方漏调 commit/abort 时关闭句柄并尽力清理暂存
        // 文件（目标零接触——abort 语义的静默版；§9.10 会话型 RAII 同款）。
        if (handle != INVALID_HANDLE_VALUE) {
            ::CloseHandle(handle);
        }
        if (active) {
            std::error_code ec;
            std::filesystem::remove(temp, ec);   // 尽力而为——失败无补偿面
        }
    }

    Impl() = default;
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
};

AtomicTarget::~AtomicTarget() = default;
AtomicTarget::AtomicTarget(AtomicTarget&& other) noexcept = default;
AtomicTarget& AtomicTarget::operator=(AtomicTarget&& other) noexcept = default;

bool AtomicTarget::isActive() const noexcept
{
    return m_impl != nullptr && m_impl->active;
}

const std::filesystem::path& AtomicTarget::targetPath() const noexcept
{
    static const std::filesystem::path kEmpty;
    return m_impl != nullptr ? m_impl->target : kEmpty;
}

const std::filesystem::path& AtomicTarget::tempPath() const noexcept
{
    static const std::filesystem::path kEmpty;
    return m_impl != nullptr ? m_impl->temp : kEmpty;
}

AtomicTarget AtomicTarget::adopt(std::shared_ptr<Impl> impl)
{
    AtomicTarget out;
    out.m_impl = std::move(impl);
    return out;
}

std::shared_ptr<AtomicTarget::Impl> AtomicTarget::impl() const noexcept
{
    return m_impl;
}

IoResult<void> AtomicTarget::write(std::string_view bytes)
{
    IoResult<void> out;
    // 调用方契约违约（未 prepare/已 commit/已 abort）——防御性内部错误
    // （fail-fast 面；不静默吞——AGENTS.md 错误语义）。
    if (m_impl == nullptr || !m_impl->active) {
        out.error.code = IoErrorCode::FormatInternal;
        out.error.detail = "AtomicTarget::write 前置违约（会话非活动态）";
        return out;
    }
    if (bytes.empty()) {
        return out;   // 空块 no-op——调用方循环驱动的边界豁免
    }
    // 单次 WriteFile（≤DWORD 上限；调用方以 1 MiB 块驱动，不触顶）。
    // 前置契约（findings F-198 处置②——注释钉死，IO-T07）：单块 size ≤
    // DWORD 上限 4 GiB−1；实际调用面（Package 导出暂存/CSV/JSON 写出通道）
    // 以 1 MiB 块循环驱动且总量受 SingleFileBytes 硬上限 2 GiB 管辖（
    // Budget.cpp 维表）——越界单块属调用方契约违例，不为不可达分支付
    // 截断语义（静默截断 4 GiB 写为部分写）。
    const DWORD want = static_cast<DWORD>(
        bytes.size() > 0xFFFFFFFFull ? 0xFFFFFFFFull : bytes.size());
    DWORD written = 0;
    if (!::WriteFile(m_impl->handle, bytes.data(), want, &written, nullptr)
        || written != want) {
        const DWORD err = ::GetLastError();
        out.error = platform::makeOsError(err != 0 ? err : ERROR_WRITE_FAULT, true,
                                          m_impl->temp, "WriteFile 失败（§4.6 暂存写步骤）");
        return out;
    }
    return out;
}

// =====================================================================
// 真实写出器（makeAtomicFileWriter 的实现类型）
// =====================================================================

namespace {

/// §4.6 协议的真实实现（故障注入经 PackageIoFacilities 换 fake——本类
/// 不感知测试存在）。
class AtomicFileWriterImpl final : public IAtomicFileWriter {
public:
    IoResult<AtomicTarget> prepare(const std::filesystem::path& target,
                                   ReplacePolicy policy) override
    {
        IoResult<AtomicTarget> out;
        if (target.empty()) {
            // 调用方契约违约——空目标路径（装配期/参数准备错误，fail-fast 面）。
            out.error.code = IoErrorCode::FormatInternal;
            out.error.detail = "AtomicFileWriter::prepare 前置违约（目标为空）";
            return out;
        }
        const std::filesystem::path parent = target.parent_path();
        // 父目录必须已存在（本设施不代建目录——§9.10 前置"目标父目录
        // 可写"由调用方装配保证；缺失＝环境错误四分类）。
        std::error_code ec;
        if (!std::filesystem::is_directory(parent, ec)) {
            out.error = platform::makeOsError(ERROR_PATH_NOT_FOUND, true, parent,
                                              "prepare 前置失败（父目录不存在）");
            return out;
        }
        // NeverOverwrite：目标存在即拒（不覆盖未经确认的既有文件——§4.1
        // P-7 行；V23"目标为先前完整版本"的判定入口）。
        if (policy == ReplacePolicy::NeverOverwrite
            && std::filesystem::exists(target, ec)) {
            out.error.code = IoErrorCode::PackTargetExists;
            out.error.params.emplace_back("target", platform::displayOf(target));
            out.error.detail = "目标已存在且策略为 NeverOverwrite（§4.6 ReplacePolicy）";
            return out;
        }

        // 组装会话：暂存名＝`<目标文件名>.<8hex>.tmp`（§4.6 原文形态；
        // 同目录→同卷——原子替换的前提）。CREATE_NEW 保证撞名时显式失败
        // （重掷标签重试三次——防御性，概率可忽略）。
        auto impl = std::make_shared<AtomicTarget::Impl>();
        // 目标按 weakly_canonical 规范化（不可得时保留原样——canonical
        // 失败不阻断用户选择的导出目标，§4.2.4 退化口径同源）。
        impl->target = std::filesystem::weakly_canonical(target, ec);
        if (impl->target.empty()) {
            impl->target = target;
        }
        for (int attempt = 0; attempt < 3; ++attempt) {
            impl->temp = parent / (target.filename().wstring() + L"."
                                   + platform::utf8ToWide(platform::randomHex8()) + L".tmp");
            impl->handle = ::CreateFileW(impl->temp.c_str(), GENERIC_WRITE, 0, nullptr,
                                         CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (impl->handle != INVALID_HANDLE_VALUE) {
                break;
            }
            const DWORD err = ::GetLastError();
            if (err == ERROR_FILE_EXISTS && attempt < 2) {
                continue;   // 8hex 撞名——换标签重来（防御分支）
            }
            out.error = platform::makeOsError(err, true, impl->temp,
                                              "暂存文件创建失败（§4.6 第一步）");
            return out;
        }
        impl->active = true;
        out.value = AtomicTarget::adopt(std::move(impl));
        return out;
    }

    IoResult<void> commit(AtomicTarget& target) override
    {
        IoResult<void> out;
        if (target.impl() == nullptr || !target.impl()->active
            || target.impl()->handle == INVALID_HANDLE_VALUE) {
            out.error.code = IoErrorCode::FormatInternal;
            out.error.detail = "AtomicFileWriter::commit 前置违约（会话非活动态）";
            return out;
        }
        AtomicTarget::Impl& impl = *target.impl();

        // 步骤 1：flush 用户态缓冲＋FlushFileBuffers 压到介质（§4.6 协议
        // 原文——commit 返回成功即目标已在介质上完整）。
        if (!::FlushFileBuffers(impl.handle)) {
            const DWORD err = ::GetLastError();
            out.error = platform::makeOsError(err, true, impl.temp,
                                              "FlushFileBuffers 失败（§4.6 commit）");
            return out;   // 会话保持活动——调用方可重试 commit 或 abort
        }
        ::CloseHandle(impl.handle);
        impl.handle = INVALID_HANDLE_VALUE;

        // 步骤 2：原子替换。目标已存在→ReplaceFileW（同卷原子、无窗口）；
        // 不存在→MoveFileExW 挪入（REPLACE_EXISTING 防 TOCTOU 双保险＋
        // WRITE_THROUGH 落盘语义）。暂存与目标同目录同卷——两类 API 均
        // 为同卷原子操作（类注——MDL-20/V23 的结构性依据）。
        std::error_code ec;
        const bool targetExists = std::filesystem::exists(impl.target, ec);
        BOOL ok = FALSE;
        if (targetExists) {
            ok = ::ReplaceFileW(impl.target.c_str(), impl.temp.c_str(), nullptr,
                                REPLACEFILE_WRITE_THROUGH, nullptr, nullptr);
            if (!ok && ::GetLastError() == ERROR_FILE_NOT_FOUND) {
                // 极窄 TOCTOU：ReplaceFile 探测时存在、执行时被删——回落挪入。
                ok = ::MoveFileExW(impl.temp.c_str(), impl.target.c_str(),
                                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
            }
        } else {
            ok = ::MoveFileExW(impl.temp.c_str(), impl.target.c_str(),
                               MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
        }
        if (!ok) {
            const DWORD err = ::GetLastError();
            // 句柄已关而替换失败：暂存文件仍在（内容完好）——会话转非活
            // 动态并报错；调用方重试 commit 已不可行（句柄关闭），应 abort
            // 清理暂存。此处如实报环境错误，不静默重开（§1.4 失败上抛面）。
            impl.active = false;
            out.error = platform::makeOsError(err, true, impl.target,
                                              "原子替换失败（§4.6 ReplaceFile/MoveFileEx）");
            return out;
        }
        impl.active = false;   // 终态：目标＝本次内容；暂存位已被顶替/挪用
        return out;
    }

    IoResult<void> abort(AtomicTarget& target) override
    {
        IoResult<void> out;
        if (target.impl() == nullptr) {
            return out;   // 幂等：空句柄＝无可放弃（失败路径统一调用形态）
        }
        AtomicTarget::Impl& impl = *target.impl();
        if (!impl.active) {
            return out;   // 幂等：已 commit/abort——重复 abort 合法（V21 重试同款）
        }
        if (impl.handle != INVALID_HANDLE_VALUE) {
            ::CloseHandle(impl.handle);
            impl.handle = INVALID_HANDLE_VALUE;
        }
        std::error_code ec;
        std::filesystem::remove(impl.temp, ec);
        // 清理失败（ec 置位）时如实上报（开发级定位），但目标零接触的
        // 语义不变——暂存文件带随机后缀且在下一次 prepare 时被 CREATE_NEW
        // 排斥，不会污染目标。
        if (ec) {
            out.error.code = IoErrorCode::ResAccessDenied;
            out.error.params.emplace_back("temp", platform::displayOf(impl.temp));
            out.error.detail = "abort 暂存清理失败（目标未受影响；ec=" + ec.message() + "）";
            impl.active = false;
            return out;
        }
        impl.active = false;
        return out;
    }
};

} // namespace

IAtomicFileWriterPtr makeAtomicFileWriter()
{
    return std::make_shared<AtomicFileWriterImpl>();
}

} // namespace sdurws::ird::io
