/**
 * @file   ZipChannel.cpp
 * @brief  ZIP 通道实现——libzip 容器层读取：条目枚举、逐字节还原（加密
 *         拒绝/压缩方法白名单/预算双记账/协作取消）与 manifest 条目哈希
 *         校验（core ContentDigester——SHA-256 唯一摘要算法）。
 *
 * 设计依据：见 ZipChannel.hpp 文件头（units/io.md §7.1/§12 IO-T04 行/
 * §9.12/§4.5.2/§9.13、需求 PM-05/NFR-SEC-02、SA-12/NFR-MNT-03、P-IO-3
 * 冻结裁决——ZIP＝libzip 1.11.4、任务契约 IO-T04 acceptance 4/5）。
 *
 * libzip 集成形态（P-IO-3 冻结口径的实现面）：
 *   - 会话＝zip_t 句柄（ZIP_RDONLY——通道只读；写包归导出器，阶段 B）；
 *   - 打开经 zip_source_filep_create＋zip_open_from_source：FILE* 由
 *     Windows 侧 _wfopen 以宽路径打开（std::filesystem::path::c_str()——
 *     UTF-8 窄串会经 ACP 转换丢字，宽路径是 Windows 全名空间的唯一无损
 *     入口；POSIX 侧 fopen 窄路径）；失败链的 FILE 指针与 source 所有权
 *     逐级回收（libzip 所有权规则：open_from_source 成功后归 zip_t 所有）；
 *   - zip64：libzip 1.11 原生 64 位 API（P-IO-3 冻结依据之一——zipconf.h
 *     复核），无需显式开关；
 *   - 加密检测＝中央目录 encryption_method ≠ ZIP_EM_NONE（§7.1 加密拒
 *     绝语义只需检测——解压前拒绝，不给解密尝试面）；
 *   - 压缩方法白名单＝{ZIP_CM_STORE, ZIP_CM_DEFLATE}（§7.1 条目约束；
 *     其余方法在解压前以 IO-FORMAT-PACK-ZIP 拒绝）。
 *
 * 线程约束：会话单线程（ZipChannel.hpp 类注）；本文件无共享可变状态。
 * 异常纪律：公共接口非抛出（§1.4）——实现体 try/catch 包裹，标准库异
 * 常转 IO-FORMAT-INTERNAL（防御性内部错误）。
 */

#include <sdurws/ird/io/ZipChannel.hpp>

#include <sdurws/ird/io/Budget.hpp>
#include <sdurws/ird/io/IoDiagnostics.hpp>

#include <zip.h>

#include <array>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace sdurws::ird::io {

namespace {

// =====================================================================
// 常量（实现侧窗口——注明出处）
// =====================================================================

/// 条目读取循环的块大小，单位字节。64 KiB＝常规文件系统缓冲整数倍；取
/// 消检查点与预算记账按块轮询（ZipChannel.hpp readEntryBytes ⑤——§9.13
/// "取消＝协作检查点"的通道落点）。
inline constexpr std::size_t kReadChunkBytes = 64u * 1024;

/// 压缩方法白名单（§7.1"压缩方法仅 STORED/DEFLATE"——libzip 码面）。
inline constexpr bool compressionMethodAllowed(std::uint16_t m) noexcept
{
    return m == ZIP_CM_STORE || m == ZIP_CM_DEFLATE;
}

// =====================================================================
// 预算 scope 会话（Json.cpp precheckDocBytes 同款双轨形态的通道版）
// =====================================================================

/**
 * @brief 会话级预算 scope 的 RAII 守卫（Csv.cpp BudgetScopeSession 同款
 *        等价形态）：guard 非空且调用方未传句柄＝自开内部 scope（产品
 *        默认规格）用毕回收；传入句柄＝只记账不越权关闭（导入器以
 *        packImportHardened 收紧后传入——硬化判定权在调用方，通道不代
 *        行）；guard 为空＝全部记账退化为"对产品默认限额的防御性比较"
 *        （不记账不豁免——测试/轻量场景口径）。
 */
class ZipScopeSession {
public:
    ZipScopeSession() = default;
    ZipScopeSession(const ZipScopeSession&) = delete;
    ZipScopeSession& operator=(const ZipScopeSession&) = delete;

    ~ZipScopeSession()
    {
        // 兜底回收（早退/异常路径——leave 的返回值丢失可接受：清理路径
        // 只求不泄漏 scope；正常路径以显式 leave() 取错误）。
        if (m_guard != nullptr && !m_external && m_scope.value != 0) {
            (void)m_guard->closeScope(m_scope);
        }
    }

    /// 进入会话：开 scope（内部模式）或采用调用方句柄（外部模式）。
    IoResult<void> enter(IBudgetGuard* guard, BudgetScopeId callerScope)
    {
        m_guard = guard;
        if (guard == nullptr) {
            return {};   // null＝不记账（防御性比较路径——charge 内分支）
        }
        if (callerScope.value != 0) {
            m_scope = callerScope;   // 外部 scope：调用方所有，不关闭
            m_external = true;
            return {};
        }
        const IoResult<BudgetScopeId> r = guard->openScope(BudgetSpec::productDefault());
        if (!r) {
            // 缺省规格恒不超硬上限——触及即实现缺陷（防御性内部错误）。
            IoResult<void> out;
            out.error = r.error;
            return out;
        }
        m_scope = r.value;
        return {};
    }

    /// 离开会话：内部 scope 回收关闭（父超限的错误向上传播——§4.5.2）。
    IoResult<void> leave()
    {
        if (m_guard != nullptr && !m_external && m_scope.value != 0) {
            const IoResult<void> r = m_guard->closeScope(m_scope);
            m_scope = BudgetScopeId{};
            return r;
        }
        return {};
    }

    /// 记账一笔（饱和加法在 guard 内——失败状态不变，调用方中止）。
    IoResult<void> charge(BudgetDimension dim, std::uint64_t amount)
    {
        if (m_guard == nullptr || m_scope.value == 0) {
            return {};   // 无守卫＝防御性比较路径（不记账）
        }
        return m_guard->charge(m_scope, dim, amount);
    }

    /**
     * @brief 压缩包双侧重账（§4.5.2——压缩侧累计；展开侧先按
     *        ArchiveExpandedBytes 检查再按比例检查，全过才双侧入账）。
     */
    IoResult<void> chargeArchive(std::uint64_t compressedDelta, std::uint64_t expandedDelta)
    {
        if (m_guard == nullptr || m_scope.value == 0) {
            return {};
        }
        return m_guard->chargeArchive(m_scope, compressedDelta, expandedDelta);
    }

    /// 该维生效限额（防御性比较的 limit 值——guard 空时取产品默认规格）。
    std::uint64_t limitOf(BudgetDimension dim) const
    {
        if (m_guard != nullptr && m_scope.value != 0) {
            const BudgetLedgerSnapshot snap = m_guard->ledger(m_scope);
            for (const auto& [d, st] : snap.dimensions) {
                if (d == dim) {
                    return st.limit;
                }
            }
            return 0;   // 维缺失＝实现缺陷（防御性——比较恒失败方向安全）
        }
        const BudgetSpec spec = BudgetSpec::productDefault();
        return spec.limit(dim);
    }

    /// 是否挂接了预算守卫（null 守卫时读取走防御性比较路径——readByIndex 注）。
    bool guarded() const { return m_guard != nullptr; }

private:
    IBudgetGuard* m_guard = nullptr;   ///< 预算守卫（调用方所有；可空）
    BudgetScopeId m_scope{};           ///< 生效 scope（内部或外部）
    bool m_external = false;           ///< true＝调用方句柄（leave 不关闭）
};

// =====================================================================
// 错误构造与 hex 渲染（诊断面——与 IoDiagnostics paramSchema 对齐）
// =====================================================================

/// 摘要 hex 渲染（小写 64 字符——manifest 文本口径；hex 编码不是摘要算
/// 法，SA-12 约束的是哈希原语唯一性——复算恒经 core ContentDigester）。
IoString renderDigestHex(const core::Digest256& d)
{
    static const char* kHex = "0123456789abcdef";
    IoString out;
    out.reserve(d.size() * 2);
    for (const std::uint8_t b : d) {
        out.push_back(kHex[b >> 4]);
        out.push_back(kHex[b & 0x0F]);
    }
    return out;
}

/// 包通道错误（params 按 ioCodeDescriptors 声明的 paramSchema 装配）。
IoError packError(IoErrorCode code, std::string detail)
{
    IoError e;
    e.code = code;
    e.detail = std::move(detail);
    return e;
}

} // namespace

// =====================================================================
// ZipChannelSession（IZipChannel 实现——libzip 句柄 RAII）
// =====================================================================

namespace {

/**
 * @brief ZIP 通道会话实现（一个实例一个 zip_t；单线程）。
 *
 * 生命周期：openZipChannel 成功即绑定包文件；析构 zip_close（libzip 释放
 * 全部关联资源——含 source/FILE*）；打开失败链的回收见 openZipChannel 注。
 */
class ZipChannelSession final : public IZipChannel {
public:
    explicit ZipChannelSession(zip_t* za) : m_za(za) {}
    ~ZipChannelSession() override
    {
        if (m_za != nullptr) {
            zip_close(m_za);   // 释放句柄＋关联 source/FILE*（libzip 所有权）
        }
    }

    IoResult<std::vector<ZipEntryInfo>> listEntries() override
    {
        IoResult<std::vector<ZipEntryInfo>> out;
        try {
            const std::int64_t n = zip_get_num_entries(m_za, 0);
            if (n < 0) {
                out.error = packError(IoErrorCode::FormatPackZip,
                                      "zip list: cannot get entry count");
                return out;
            }
            out.value.reserve(static_cast<std::size_t>(n));
            for (std::int64_t i = 0; i < n; ++i) {
                zip_stat_t st;
                zip_stat_init(&st);
                if (zip_stat_index(m_za, static_cast<zip_uint64_t>(i), ZIP_FL_ENC_UTF_8, &st)
                    != 0) {
                    out.error = packError(IoErrorCode::FormatPackZip,
                                          "zip list: stat failed at index "
                                              + std::to_string(i));
                    return out;
                }
                ZipEntryInfo info;
                if ((st.valid & ZIP_STAT_NAME) != 0 && st.name != nullptr) {
                    info.name = st.name;   // 条目原样名（UTF-8/正斜杠——容器层不改写）
                }
                if ((st.valid & ZIP_STAT_SIZE) != 0) {
                    info.uncompressedSize = st.size;
                }
                if ((st.valid & ZIP_STAT_COMP_SIZE) != 0) {
                    info.compressedSize = st.comp_size;
                }
                if ((st.valid & ZIP_STAT_CRC) != 0) {
                    info.crc32 = st.crc;
                }
                if ((st.valid & ZIP_STAT_COMP_METHOD) != 0) {
                    info.compressionMethod = st.comp_method;
                }
                // 加密标记：双源检测（§7.1 加密拒绝语义只需检测）——①中
                // 央目录加密方法 ≠ ZIP_EM_NONE（ZIP_EM_AES_*/TRAD_PKWARE
                // 不区分）；②一般标志 bit0（TRAD_PKWARE 形态的容器层标
                // 记——libzip 未置 encryption_method 有效位时的兜底）。
                if ((st.valid & ZIP_STAT_ENCRYPTION_METHOD) != 0
                    && st.encryption_method != ZIP_EM_NONE) {
                    info.encrypted = true;
                }
                if ((st.valid & ZIP_STAT_FLAGS) != 0 && (st.flags & 0x0001) != 0) {
                    info.encrypted = true;
                }
                // 条目外部属性（IO-T06 表尾增补字段的填充——原始透传不做
                // 解释；symlink 判定＝导入器步骤③预检，§4.3.1 SP-4）。
                {
                    zip_uint8_t opsys = 0;
                    zip_uint32_t attr = 0;
                    if (zip_file_get_external_attributes(m_za, static_cast<zip_uint64_t>(i),
                                                         0, &opsys, &attr) == 0) {
                        info.attributeHostSystem = opsys;
                        info.externalAttributes = attr;
                    }
                    // 取属性失败不阻断枚举（字段保持缺省 0——FAT 宿主形
                    // 态；导入器预检按"无 symlink 属性"处理，展开产物层
                    // 的 SP-4 双检仍是纵深防线，§7.4 双检行）。
                }
                info.index = static_cast<std::uint64_t>(i);
                out.value.push_back(std::move(info));
            }
            return out;
        } catch (const std::exception& e) {
            out.error = packError(IoErrorCode::FormatInternal,
                                  std::string("zip list: ") + e.what());
            return out;
        } catch (...) {
            out.error = packError(IoErrorCode::FormatInternal, "zip list: unknown exception");
            return out;
        }
    }

    IoResult<IoString> readEntryBytes(const IoString& name, IBudgetGuard* budget,
                                      BudgetScopeId budgetScope, IoCancelToken* cancel) override
    {
        IoResult<IoString> out;
        try {
            // 步骤①：按名定位（字节级精确；未命中＝清单引用完整性语义
            // IO-PACK-REF-INCOMPLETE——V29；同名列取首个命中，重复条目裁
            // 决归导入器——容器层不代行 V13）。
            const std::int64_t index =
                zip_name_locate(m_za, name.c_str(), ZIP_FL_ENC_UTF_8);
            if (index < 0) {
                out.error = packError(IoErrorCode::PackRefIncomplete,
                                      "zip read: entry not found: " + name);
                out.error.params.emplace_back("ref", name);
                return out;
            }
            ZipScopeSession scope;
            if (const IoResult<void> r = scope.enter(budget, budgetScope); !r) {
                out.error = r.error;
                return out;
            }
            IoResult<IoString> bytes = readByIndex(static_cast<std::uint64_t>(index), scope,
                                                   cancel, name);
            // leave 的父超限错误只在读取成功时向上传播（读取已败则原错
            // 误优先——失败原子性：一次只报一个根因）。
            if (!bytes) {
                return bytes;   // scope 析构兜底回收
            }
            if (const IoResult<void> lr = scope.leave(); !lr) {
                out.error = lr.error;
                return out;
            }
            out.value = std::move(bytes.value);
            return out;
        } catch (const std::exception& e) {
            out.error = packError(IoErrorCode::FormatInternal,
                                  std::string("zip read: ") + e.what());
            return out;
        } catch (...) {
            out.error = packError(IoErrorCode::FormatInternal, "zip read: unknown exception");
            return out;
        }
    }

    IoResult<std::vector<ZipEntryVerifyReport>>
        verifyManifestEntries(const std::vector<ZipManifestEntry>& entries, IBudgetGuard* budget,
                              BudgetScopeId budgetScope, IoCancelToken* cancel) override
    {
        IoResult<std::vector<ZipEntryVerifyReport>> out;
        try {
            ZipScopeSession scope;
            if (const IoResult<void> r = scope.enter(budget, budgetScope); !r) {
                out.error = r.error;
                return out;
            }
            std::vector<ZipEntryVerifyReport> reports;
            reports.reserve(entries.size());
            for (const ZipManifestEntry& entry : entries) {
                // 条目级失败不中断整表（V14"定位条目"——报告集即结果；
                // 接口级失败仅预算/取消——见接口注）。
                ZipEntryVerifyReport report;
                report.path = entry.path;
                report.error = verifyOne(entry, scope, cancel);
                reports.push_back(std::move(report));
            }
            // leave 错误＝父 scope 超限（§4.5.2 多阶段累计）——整表失败。
            if (const IoResult<void> lr = scope.leave(); !lr) {
                out.error = lr.error;
                return out;
            }
            out.value = std::move(reports);
            return out;
        } catch (const std::exception& e) {
            out.error = packError(IoErrorCode::FormatInternal,
                                  std::string("zip verify: ") + e.what());
            return out;
        } catch (...) {
            out.error = packError(IoErrorCode::FormatInternal, "zip verify: unknown exception");
            return out;
        }
    }

private:
    /**
     * @brief 读取单条目原文（readEntryBytes 与校验的共用执行体）。
     *
     * 检查/记账序（ZipChannel.hpp readEntryBytes 注）：定位（调用方已
     * 完成）→加密拒绝→压缩方法白名单→声明双记账（预检笔）→块循环
     * （取消＋SingleFileBytes/TotalBytes 记账）→实际超声明补差笔。
     */
    IoResult<IoString> readByIndex(std::uint64_t index, ZipScopeSession& scope,
                                   IoCancelToken* cancel, const IoString& nameForDetail)
    {
        IoResult<IoString> out;
        // 步骤②③：条目 stat——加密与压缩方法在解压前拒绝。
        zip_stat_t st;
        zip_stat_init(&st);
        if (zip_stat_index(m_za, index, ZIP_FL_ENC_UTF_8, &st) != 0) {
            out.error = packError(IoErrorCode::FormatPackZip,
                                  "zip read: stat failed for: " + nameForDetail);
            return out;
        }
        const std::uint16_t encMethod =
            (st.valid & ZIP_STAT_ENCRYPTION_METHOD) != 0 ? st.encryption_method : ZIP_EM_NONE;
        const bool flagEncrypted = (st.valid & ZIP_STAT_FLAGS) != 0 && (st.flags & 0x0001) != 0;
        if (encMethod != ZIP_EM_NONE || flagEncrypted) {
            // §7.1 加密拒绝（解压前——不给解密尝试面；params 空——paramSchema [])，
            // 条目名入 detail（脱敏前仅开发级）。
            out.error = packError(IoErrorCode::FormatPackEncrypted,
                                  "zip read: encrypted entry rejected: " + nameForDetail);
            return out;
        }
        const std::uint16_t compMethod =
            (st.valid & ZIP_STAT_COMP_METHOD) != 0 ? st.comp_method : ZIP_CM_STORE;
        if (!compressionMethodAllowed(compMethod)) {
            // §7.1"压缩方法仅 STORED/DEFLATE"——容器层白名单（防把算法
            // 攻击面（bzip2/xz/zstd 解码器）引入导入路径）。
            out.error = packError(IoErrorCode::FormatPackZip,
                                  "zip read: compression method not allowed ("
                                      + std::to_string(compMethod) + "): " + nameForDetail);
            return out;
        }
        const std::uint64_t declaredSize = (st.valid & ZIP_STAT_SIZE) != 0 ? st.size : 0;
        const std::uint64_t declaredComp = (st.valid & ZIP_STAT_COMP_SIZE) != 0 ? st.comp_size : 0;
        // 步骤④：声明双记账（预检笔——§4.5.2"条目声明大小先入账"；zip
        // 炸弹比例在声明量上先拦）。无守卫＝防御性比较（产品默认规格——
        // 不记账不豁免：展开量限额与压缩比在声明量上同判，攻击面不因
        // 轻量调用形态放开）。
        if (!scope.guarded()) {
            const BudgetSpec def = BudgetSpec::productDefault();
            const std::uint64_t expandLimit = def.limit(BudgetDimension::ArchiveExpandedBytes);
            if (declaredSize > expandLimit) {
                out.error = makeComparativeError(IoErrorCode::SecBudgetExpand, declaredSize,
                                                 expandLimit, "bytes",
                                                 "zip read: declared size over budget (defensive)");
                return out;
            }
            const std::uint64_t ratioLimit = def.limit(BudgetDimension::ArchiveRatio);
            // 饱和乘法（无除法比较——Budget.cpp chargeArchive 同款语义）。
            std::uint64_t bound = 0;
            if (declaredComp != 0
                && (ratioLimit > std::numeric_limits<std::uint64_t>::max() / declaredComp)) {
                bound = std::numeric_limits<std::uint64_t>::max();   // 乘积溢出＝上界无穷
            } else {
                bound = ratioLimit * declaredComp;
            }
            if (declaredSize > bound || (declaredComp == 0 && declaredSize > 0)) {
                out.error = makeComparativeError(
                    IoErrorCode::SecBombRatio, declaredSize, bound, "bytes",
                    "zip read: declared compression ratio over limit (defensive)");
                return out;
            }
        }
        if (const IoResult<void> r = scope.chargeArchive(declaredComp, declaredSize); !r) {
            out.error = r.error;   // IO-SEC-BUDGET-EXPAND / IO-SEC-BOMB-RATIO（三要素）
            return out;
        }
        // 步骤⑤：解压块循环（每块取消检查点＋记账）。
        zip_file_t* zf = zip_fopen_index(m_za, index, 0);
        if (zf == nullptr) {
            out.error = packError(IoErrorCode::FormatPackZip,
                                  std::string("zip read: fopen_index failed: ")
                                      + zip_strerror(m_za));
            return out;
        }
        IoString bytes;
        bytes.reserve(static_cast<std::size_t>(declaredSize) + 1);
        std::array<char, kReadChunkBytes> chunk{};
        std::uint64_t actualSize = 0;
        for (;;) {
            if (cancel != nullptr && cancel->isCancelled()) {
                zip_fclose(zf);
                out.error = IoError{IoErrorCode::Cancelled, {},
                                    "zip read: cancelled at chunk checkpoint"};
                return out;
            }
            const std::int64_t got = zip_fread(zf, chunk.data(), chunk.size());
            if (got < 0) {
                // 错误文本先取（zf 关闭后 zip_file_strerror 失效——句柄
                // 生存期纪律）。
                const std::string msg = zip_file_strerror(zf);
                zip_fclose(zf);
                out.error = packError(IoErrorCode::FormatPackZip,
                                      "zip read: fread failed: " + msg);
                return out;
            }
            if (got == 0) {
                break;   // 条目数据读完（EOF）
            }
            bytes.append(chunk.data(), static_cast<std::size_t>(got));
            actualSize += static_cast<std::uint64_t>(got);
            // 单文件/会话总量记账（块粒度——展开侧实际量推进）。
            if (const IoResult<void> r = scope.charge(BudgetDimension::SingleFileBytes,
                                                      static_cast<std::uint64_t>(got));
                !r) {
                zip_fclose(zf);
                out.error = r.error;
                return out;
            }
            if (const IoResult<void> r =
                    scope.charge(BudgetDimension::TotalBytes, static_cast<std::uint64_t>(got));
                !r) {
                zip_fclose(zf);
                out.error = r.error;
                return out;
            }
            // 实际展开超声明部分补差（§4.5.2"以较大者入账"——zip 头谎报
            // 小尺寸的攻击形态；chargeArchive 比例复核同步生效）。
            if (actualSize > declaredSize) {
                if (const IoResult<void> r = scope.chargeArchive(0, static_cast<std::uint64_t>(got));
                    !r) {
                    zip_fclose(zf);
                    out.error = r.error;
                    return out;
                }
            }
        }
        const int closeErr = zip_fclose(zf);
        if (closeErr != 0) {
            // CRC 等容器级校验失败在 fclose 报告（libzip 语义：关闭返回
            // 首个累积错误码）——逐字节还原的完整性兜底（与 SHA-256 内
            // 容身份互不替代——ZipEntryInfo 注）。
            out.error = packError(IoErrorCode::FormatPackZip,
                                  "zip read: container integrity check failed (zip error "
                                      + std::to_string(closeErr) + ")");
            return out;
        }
        out.value = std::move(bytes);
        return out;
    }

    /**
     * @brief 校验单条 manifest 条目（定位→读原文→复算→比对）。
     *
     * 码面（ioCodeDescriptors 对齐）：缺失→IO-PACK-REF-INCOMPLETE
     * （params ref）；大小不符/哈希不符→IO-PACK-HASH-MISMATCH（params
     * entry/expected/actual）。SHA-256 复算经 core ContentDigester
     * （SA-12/NFR-MNT-03——唯一摘要算法，无第二哈希实现）。
     */
    IoError verifyOne(const ZipManifestEntry& entry, ZipScopeSession& scope, IoCancelToken* cancel)
    {
        const std::int64_t index = zip_name_locate(m_za, entry.path.c_str(), ZIP_FL_ENC_UTF_8);
        if (index < 0) {
            IoError e = packError(IoErrorCode::PackRefIncomplete,
                                  "zip verify: manifest entry missing in archive: " + entry.path);
            e.params.emplace_back("ref", entry.path);
            return e;
        }
        const IoResult<IoString> bytes = readByIndex(static_cast<std::uint64_t>(index), scope,
                                                     cancel, entry.path);
        if (!bytes) {
            return bytes.error;   // 加密/压缩方法/预算/取消——容器层事实原样上抛
        }
        // 大小比对（先于哈希——快速失败；expected/actual＝字节数文本）。
        if (bytes.value.size() != entry.size) {
            IoError e = packError(IoErrorCode::PackHashMismatch,
                                  "zip verify: size mismatch for: " + entry.path);
            e.params.emplace_back("entry", entry.path);
            e.params.emplace_back("expected", std::to_string(entry.size));
            e.params.emplace_back("actual", std::to_string(bytes.value.size()));
            return e;
        }
        // 哈希复算（SHA-256 唯一算法——core ContentDigester；对未压缩
        // 原文计算——§7.1 哈希行口径）。
        core::ContentDigester digester;
        digester.update(bytes.value.data(), bytes.value.size());
        const core::Digest256 actual = digester.finalize();
        if (actual != entry.sha256) {
            IoError e = packError(IoErrorCode::PackHashMismatch,
                                  "zip verify: sha256 mismatch for: " + entry.path);
            e.params.emplace_back("entry", entry.path);
            e.params.emplace_back("expected", renderDigestHex(entry.sha256));
            e.params.emplace_back("actual", renderDigestHex(actual));
            return e;
        }
        return IoError{};   // 通过（Ok）
    }

    zip_t* m_za = nullptr;   ///< libzip 归档句柄（ZIP_RDONLY；析构 zip_close）
};

} // namespace

IoResult<std::unique_ptr<IZipChannel>> openZipChannel(const std::filesystem::path& packFile)
{
    IoResult<std::unique_ptr<IZipChannel>> out;
    // 打开链（所有权规则逐级回收——libzip：zip_open_from_source 成功后
    // source/FILE* 归 zip_t；失败时 source_free 关闭 FILE*）：
    //   _wfopen（Windows 宽路径——UTF-8 窄串经 ACP 转换会丢字，宽路径是
    //   全名空间唯一无损入口；POSIX fopen 窄路径）→ zip_source_filep_
    //   create（只读 FILE* 源）→ zip_open_from_source（ZIP_RDONLY）。
    FILE* f = nullptr;
#ifdef _WIN32
    f = _wfopen(packFile.c_str(), L"rb");
#else
    f = std::fopen(packFile.string().c_str(), "rb");
#endif
    if (f == nullptr) {
        // 打不开＝环境错误四分类（§4.2.5——fopen 无法细分锁竞争形态，
        // 按 errno 两态映射：不存在→NOT-FOUND，其余→ACCESS-DENIED）。
        out.error = IoError{(errno == ENOENT) ? IoErrorCode::ResNotFound
                                              : IoErrorCode::ResAccessDenied,
                            {},
                            "zip open: cannot open file"};
        return out;
    }
    zip_error_t zipErr;
    zip_error_init(&zipErr);
    zip_source_t* src = zip_source_filep_create(f, 0, -1, &zipErr);
    if (src == nullptr) {
        std::fclose(f);   // source 未建成——FILE* 自收
        out.error = packError(IoErrorCode::FormatPackZip,
                              std::string("zip open: source creation failed: ")
                                  + zip_error_strerror(&zipErr));
        return out;
    }
    zip_t* za = zip_open_from_source(src, ZIP_RDONLY, &zipErr);
    if (za == nullptr) {
        zip_source_free(src);   // 关联 FILE* 随 source 释放（libzip 契约）
        out.error = packError(IoErrorCode::FormatPackZip,
                              std::string("zip open: not a readable zip container: ")
                                  + zip_error_strerror(&zipErr));
        return out;
    }
    out.value = std::unique_ptr<IZipChannel>(new ZipChannelSession(za));
    return out;
}

} // namespace sdurws::ird::io
