/**
 * @file   ObjectStore.cpp
 * @brief  对象库（ObjectStore）实现——内容编址/只增发布/摘要校验/LRU 缓存。
 *
 * 设计依据：见 ObjectStore.hpp 文件头（units/project.md §4.6/§4.4.6/§7.1、
 * 需求 CON-01、任务契约 PRJ-T05.json）。本实现文件只做一件事：把 §4.6 的
 * 五段语义（编址/只增/校验/缓存/引用存在性）逐段落到 Win32 原语
 * （AtomicFile——PRJ-T02 产出）与 core 摘要契约（ContentDigester——经
 * codec::contentVersionOf 唯一入口，CR-02）之上。
 */

#include "ObjectStore.hpp"

#include "Codec.hpp"
#include "win32/AtomicFile.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace fs = std::filesystem;

namespace sdurws::ird::project::objstore {

namespace {

// ---------------------------------------------------------------------
// 错误映射（环境错误 → 稳定码）
// ---------------------------------------------------------------------

/// std::errc → std::error_code 的比较辅助（read/ec 路径用；Win32 原生码
/// 路径直接比 ERROR_* 常量，两套来源不同不能混比）。
bool isNotFound(const std::error_code& ec) noexcept
{
    // 条件＝目录项不存在（对象未发布/已被外部删除）——这是"对象缺失"的
    // 唯一判定，其余错误不得混入（AGENTS §2.5 错误语义：数据缺失≠不可读）。
    return ec == std::errc::no_such_file_or_directory;
}

/// 读路径 std::error_code → StoreError（存在却读不了的归类）。
///
/// 归类口径：permission_denied＝环境错误 AccessDenied（OS 拒绝）；其余
/// ＝StoreCorrupt——理由：对象文件不可变且已通过编址，读取阶段无法完成
/// ＝无法证明完整性，按数据侧拒绝比伪装成"不存在"安全（缺失与损坏是
/// 两种事实，静默合并会掩盖损坏——tryObject 契约的注释同源）。
StoreError readErrorToStoreError(const std::error_code& ec,
                                 const std::string& path)
{
    if (ec == std::errc::permission_denied) {
        return StoreError(StoreErrorCode::AccessDenied,
                          "project/object-store: access-denied path=" + path);
    }
    return StoreError(StoreErrorCode::StoreCorrupt,
                      "project/object-store: unreadable path=" + path
                          + " ec=" + std::to_string(ec.value()));
}

/// 写路径 Win32 原生错误码 → 稳定码（§7.1 第 2/4 步失败口径）。
///
/// 映射表（AtomicFile 的 FileResult.osError＝GetLastError 原始码）：
///   ERROR_DISK_FULL/ERROR_DISK_QUOTA_EXCEEDED → DiskFull（磁盘满——
///       §7.1 第 2 步"写中途失败/磁盘满 → Failed(disk-full)"）；
///   ERROR_ACCESS_DENIED → AccessDenied（OS 拒绝访问）；
///   其余 → WriteRejected（§7.1 第 4 步"rename 失败 → Failed(write-rejected)"
///       的兜底——写路径环境失败的稳定归类）。
StoreError writeOsErrorToStoreError(unsigned long osError,
                                    const std::string& what,
                                    const std::string& path)
{
    constexpr unsigned long kDiskFull = 70;              // ERROR_DISK_FULL
    constexpr unsigned long kQuotaExceeded = 395;        // ERROR_DISK_QUOTA_EXCEEDED
    constexpr unsigned long kAccessDenied = 5;           // ERROR_ACCESS_DENIED
    const std::string detail = "project/object-store: " + what
        + " path=" + path + " osError=" + std::to_string(osError);
    if (osError == kDiskFull || osError == kQuotaExceeded) {
        return StoreError(StoreErrorCode::DiskFull, detail);
    }
    if (osError == kAccessDenied) {
        return StoreError(StoreErrorCode::AccessDenied, detail);
    }
    return StoreError(StoreErrorCode::WriteRejected, detail);
}

/// 目录创建的 std::error_code → StoreError（§7.1 第 1 步"建目录失败
/// （权限/磁盘）→ Failed(write-rejected/disk-full)"口径；权限细分
/// AccessDenied——与写路径映射同源）。
StoreError dirErrorToStoreError(const std::error_code& ec,
                                const std::string& path)
{
    const std::string detail = "project/object-store: mkdir path=" + path
        + " ec=" + std::to_string(ec.value());
    if (ec == std::errc::no_space_on_device) {
        return StoreError(StoreErrorCode::DiskFull, detail);
    }
    if (ec == std::errc::permission_denied) {
        return StoreError(StoreErrorCode::AccessDenied, detail);
    }
    return StoreError(StoreErrorCode::WriteRejected, detail);
}

/// 判定目录项名是否为 64 个小写十六进制字符（对象文件名规则——§4.1
/// "<cv>＝内容版本 64 hex（无 tag）"；扫描解析的准入判定）。
bool isHex64(std::string_view name) noexcept
{
    if (name.size() != 64) {
        return false;
    }
    for (char c : name) {
        const bool digit = (c >= '0' && c <= '9');
        const bool lowerHex = (c >= 'a' && c <= 'f');
        if (!digit && !lowerHex) {
            return false;  // 大写/非十六进制字符都不合规（core 规范文本小写口径）
        }
    }
    return true;
}

/// 扫描报告桶的确定性排序键：oid 字典序 → cv 字典序（core 两类型均有
/// operator< 字节字典序）。排序目的：directory_iterator 顺序未指定，
/// 不排序则报告与测试断言不可复现（NFR-COR-02 确定性精神在诊断面的延伸）。
bool objectKeyLess(const ObjectKey& a, const ObjectKey& b) noexcept
{
    if (a.oid.bytes != b.oid.bytes) {
        return a.oid.bytes < b.oid.bytes;
    }
    return a.cv.bytes < b.cv.bytes;
}

}  // namespace

// =====================================================================
// 构造与路径派生
// =====================================================================

ObjectStore::ObjectStore(std::filesystem::path objectsDir,
                         std::filesystem::path stagingDir,
                         std::size_t cacheBudgetBytes,
                         IDiagnosticsSink* sink)
    : m_objectsDir(std::move(objectsDir)),
      m_stagingDir(std::move(stagingDir)),
      m_budgetBytes(cacheBudgetBytes),
      m_sink(sink)
{
    // 目录创建延迟到首次发布（构造不做 I/O——对象区可以是尚未创建的
    // 新项目路径，扫描/读取对不存在目录按"空/缺失"处理而非报错）。
    // 预算为 0 合法＝缓存整体停用（每条目都超预算→不缓存），语义自洽。
}

std::filesystem::path ObjectStore::objectDir(const core::ObjectId& oid) const
{
    // 目录名＝core 规范文本 "obj-<32hex>"（§4.1 命名规则——与 core 格式化
    // 同源，不经本地第二套拼接，P-PR-1 消费基线）。
    return m_objectsDir / oid.toCanonical();
}

std::filesystem::path ObjectStore::objectFile(
    const core::ObjectId& oid, const core::ContentVersion& cv) const
{
    // 文件名＝cv 规范文本剥离 "cv-" tag 得 64 hex（§4.1"无 tag"）。
    // 剥离而非本地重排 hex：与 core::formatDigest 同源同序，杜绝本地第二
    // 套格式化漂移（CR-02"不私设第二哈希路径"的格式化半边）。
    const std::string canonical = cv.toCanonical();
    // 防御性契约声明（不可达——core toCanonical 契约恒带 "cv-" 前缀；
    // 若失真属 core 契约破坏，logic_error fail-fast 优先于产生错误编址）。
    if (canonical.size() != 67 || canonical.compare(0, 3, "cv-") != 0) {
        throw std::logic_error("project/object-store: core ContentVersion "
                               "canonical contract violated");
    }
    return objectDir(oid) / canonical.substr(3);
}

// =====================================================================
// 发布（只增）
// =====================================================================

core::ContentVersion ObjectStore::publishObject(const core::ObjectId& oid,
                                                std::string_view payloadBytes)
{
    // ---- 调用方契约校验（fail-fast）：全零 ObjectId＝保留值，不是身份 ----
    if (!oid.isValid()) {
        throw std::invalid_argument(
            "project/object-store: publishObject requires valid ObjectId");
    }

    // ---- 第 1 步：内容版本＝收到的字节经唯一哈希入口（CR-02/D-10） ----
    // 先算 cv 再落盘：文件名即 cv，任何写入动作都必须已知编址目标。
    const core::ContentVersion cv = codec::contentVersionOf(payloadBytes);
    const std::filesystem::path target = objectFile(oid, cv);
    const std::string targetName = target.string();

    // ---- 第 2 步：确保对象区/对象目录/暂存区存在（§7.1 第 1 步口径） ----
    // create_directories 对已存在目录＝无错误返回（幂等）；仅真实失败进
    // 错误映射。三者都失败于同一个动作（mkdir），错误映射一致。
    std::error_code ec;
    fs::create_directories(m_objectsDir, ec);
    if (ec && !fs::is_directory(m_objectsDir)) {
        throw dirErrorToStoreError(ec, m_objectsDir.string());
    }
    fs::create_directories(m_stagingDir, ec);
    if (ec && !fs::is_directory(m_stagingDir)) {
        throw dirErrorToStoreError(ec, m_stagingDir.string());
    }
    fs::create_directories(objectDir(oid), ec);
    if (ec && !fs::is_directory(objectDir(oid))) {
        throw dirErrorToStoreError(ec, objectDir(oid).string());
    }

    // ---- 第 3 步：暂存写（持久性闸门——§7.1 第 2 步） ----
    // 临时名含进程内单调序号：同批次多次发布互不冲突（publishNew 的 temp
    // 不得撞名；序号为原子递增，跨线程唯一）。
    const std::uint64_t seq =
        m_tempSeq.fetch_add(1, std::memory_order_relaxed);
    const std::string tempName = "ostore-" + std::to_string(seq) + "-"
        + cv.toCanonical().substr(3) + ".tmp";
    const std::filesystem::path temp = m_stagingDir / tempName;

    win32::AtomicFile atomic;
    // writeThrough＝写穿透＋flush 闸门：成功即数据已落盘，才允许进入
    // 第 4 步可见性切换。失败时部分文件留暂存区（设计行为——恢复扫描
    // 处置，AtomicFile.hpp 文件头"失败残留语义"），此处只映射错误上抛。
    const char* data = payloadBytes.size() > 0 ? payloadBytes.data() : nullptr;
    const win32::FileResult wr =
        atomic.writeThrough(temp.wstring(), data, payloadBytes.size());
    if (!wr.ok) {
        throw writeOsErrorToStoreError(wr.osError, "staging-write",
                                       temp.string());
    }

    // ---- 第 4 步：publishNew 只增发布（§7.1 第 4 步；有界重试 2 次） ----
    // 重试只覆盖一种竞态：目标在 publishNew 报 EXISTS 之后、读回校验之前
    // 被外部删除（读回得"不存在"）——此时重发一次；其余失败一律按原
    // 语义立即传播，不做无限循环。
    for (int attempt = 0; attempt < 2; ++attempt) {
        const win32::FileResult pub =
            atomic.publishNew(temp.wstring(), target.wstring());
        if (pub.ok) {
            // 发布成功：不预热读取缓存——§4.6 缓存语义＝读取侧惰性加载
            // （"首次加载执行 size＋SHA-256 校验"），预热会让发布后的首次
            // 读取跳过校验，违背读校验语义；缓存只由读路径填充。
            return cv;
        }
        if (pub.osError != ERROR_ALREADY_EXISTS) {
            // 普通发布失败（权限/磁盘/路径）——§7.1 第 4 步 write-rejected
            // 口径；已就位项保留语义归事务引擎，本原语只上抛。
            throw writeOsErrorToStoreError(pub.osError, "publish",
                                           targetName);
        }

        // ---- 目标已存在：同内容共享去重判定（§4.6 原文分支） ----
        // 读回既有文件做 size＋SHA-256 校验（readVerified 内部含已校验集
        // 免复检——但发布校验必须强制实算：此处判定的是"磁盘既有字节是否
        // 等于本次待发布内容"，不能信任历史校验结论）。实现方式：构造
        // 局部"绕过已校验集"的读回——直接读文件＋core 摘要比对。
        std::optional<std::vector<std::uint8_t>> existing;
        {
            std::error_code szEc;
            const auto existingSize = fs::file_size(target, szEc);
            if (!szEc) {
                std::ifstream in(target, std::ios::binary);
                if (in) {
                    std::vector<std::uint8_t> bytes{
                        std::istreambuf_iterator<char>(in),
                        std::istreambuf_iterator<char>()};
                    if (bytes.size() == existingSize) {
                        existing = std::move(bytes);
                    }
                }
            }
        }
        if (!existing) {
            // 既有文件消失/不可读——前者的重试窗口（attempt<1 时回到
            // publishNew 重发）；后者视为目标态异常，按 publish 失败传播。
            if (attempt == 0 && !fs::exists(target)) {
                continue;  // EXISTS→消失竞态：有界重试一次
            }
            throw StoreError(StoreErrorCode::StoreCorrupt,
                             "project/object-store: publish-target-unreadable path="
                                 + targetName);
        }
        // 摘要判定只经 core ContentDigester（CR-02）——对既有文件字节
        // 重算并与编址名比对：一致＝同内容共享（磁盘零写入）；不一致＝
        // 磁盘内容与编址不符（被外部篡改/损坏）→ store-corrupt，且既有
        // 文件绝不被覆盖（只增不改的结构保证）。
        const core::ContentVersion existingCv =
            codec::contentVersionOf(std::string_view{
                reinterpret_cast<const char*>(existing->data()),
                existing->size()});
        if (existingCv != cv) {
            throw StoreError(StoreErrorCode::StoreCorrupt,
                             "project/object-store: publish-target-digest-mismatch path="
                                 + targetName);
        }
        // 共享成立：登记进"已校验集"（本上下文对该 (oid,cv) 的读盘免复检
        // ——刚才已实算校验），尽力清除临时文件（失败仅残留，不抛——
        // 暂存残留由恢复扫描处置，删除失败分支不值得新的错误语义）。
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_verified.insert(ObjectKey{oid, cv});
        }
        std::error_code rmEc;
        fs::remove(temp, rmEc);  // 错误码吸收——残留无害（§7.1 第 7 步同口径）
        if (m_sink != nullptr) {
            // 开发诊断：同内容去重事件（运维观测共享命中率；非用户级——
            // 用户可见文案归 diagnostics，§5.0）。
            m_sink->reportDev("project/object-store",
                              "publish-share oid=" + oid.toCanonical()
                                  + " cv=" + cv.toCanonical());
        }
        auto bytes = std::make_shared<const std::vector<std::uint8_t>>(
            payloadBytes.begin(), payloadBytes.end());
        cacheInsert(ObjectKey{oid, cv}, std::move(bytes));
        return cv;
    }
    // 不可达防御：2 次尝试均落入"EXISTS→消失"竞态的极端窗口。按写失败
    // 传播（不做第三次——协议有界性优先，重试残留由恢复扫描处置）。
    throw StoreError(StoreErrorCode::WriteRejected,
                     "project/object-store: publish-retry-exhausted path="
                         + targetName);
}

// =====================================================================
// 读取（校验＋缓存）
// =====================================================================

std::optional<std::vector<std::uint8_t>> ObjectStore::readVerified(
    const core::ObjectId& oid, const core::ContentVersion& cv)
{
    const std::filesystem::path file = objectFile(oid, cv);

    // ---- size 探测：不存在的对象在此分流为 nullopt（引用存在性语义） ----
    std::error_code ec;
    const auto fileSize = fs::file_size(file, ec);
    if (ec) {
        if (isNotFound(ec)) {
            return std::nullopt;  // 对象文件不存在＝调用方自行决定语义
        }
        throw readErrorToStoreError(ec, file.string());
    }

    // ---- 全量读入：读得字节数必须等于探测 size（size 校验半边——部分读
    //      /读中途截断在此暴露为损坏，而非静默短读） ----
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        throw StoreError(StoreErrorCode::AccessDenied,
                         "project/object-store: open-failed path="
                             + file.string());
    }
    std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(in),
                                    std::istreambuf_iterator<char>()};
    if (bytes.size() != fileSize) {
        // 读得字节数与目录项 size 不符＝读取不完整（介质/并发异常）——
        // 数据侧拒绝（无法证明完整性，理由同 readErrorToStoreError）。
        throw StoreError(StoreErrorCode::StoreCorrupt,
                         "project/object-store: size-mismatch path="
                             + file.string() + " expected="
                             + std::to_string(fileSize) + " got="
                             + std::to_string(bytes.size()));
    }

    // ---- 摘要校验：每上下文每键首次读盘实算（§4.6"首次加载执行 size＋
    //      SHA-256 校验"）；已校验集命中则免复检（对象文件不可变是前提；
    //      校验结论不随负载缓存逐出失效） ----
    const ObjectKey key{oid, cv};
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_verified.count(key) > 0) {
            return bytes;  // 免复检通道：直接交付读得字节
        }
    }
    // 摘要计算在锁外（纯计算无共享状态；并发重复实算无害，结论幂等）。
    const core::ContentVersion actual = codec::contentVersionOf(
        std::string_view{reinterpret_cast<const char*>(bytes.data()),
                         bytes.size()});
    if (!(actual == cv)) {
        // 篡改/位翻转＝磁盘字节与编址名不符——数据侧 store-corrupt（数据
        // 错误按 AGENTS §2.5 走稳定码，绝不伪装成"不存在"）。
        throw StoreError(StoreErrorCode::StoreCorrupt,
                         "project/object-store: digest-mismatch path="
                             + file.string());
    }
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_verified.insert(key);
    }
    return bytes;
}

std::optional<std::vector<std::uint8_t>> ObjectStore::tryObject(
    const core::ObjectId& oid, const core::ContentVersion& cv)
{
    // 调用方契约校验（fail-fast）：全零保留值不是合法键。
    if (!oid.isValid() || !cv.isValid()) {
        throw std::invalid_argument(
            "project/object-store: tryObject requires valid ObjectId and "
            "ContentVersion");
    }
    const ObjectKey key{oid, cv};

    // ---- 快路径：缓存命中＝零触盘（LRU 触碰＋深拷贝返回） ----
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto it = m_index.find(key);
        if (it != m_index.end()) {
            // 触碰：移到链表尾（最新）——splice 是节点搬移，不复制负载。
            m_lru.splice(m_lru.end(), m_lru, it->second);
            return *it->second->second;  // 深拷贝（PA-3：值语义交付）
        }
    }
    // ---- 慢路径：读盘＋首次校验 ----
    auto bytes = readVerified(oid, cv);
    if (!bytes) {
        return std::nullopt;  // 对象不存在——不抛（引用存在性语义）
    }
    auto shared = std::make_shared<const std::vector<std::uint8_t>>(
        std::move(*bytes));
    cacheInsert(key, shared);
    return *shared;  // 深拷贝交付（缓存内是独立不可变副本）
}

std::vector<std::uint8_t> ObjectStore::object(const core::ObjectId& oid,
                                              const core::ContentVersion& cv)
{
    // 与 tryObject 的唯一语义差：不存在＝store-corrupt（§5.2 错误表
    // "引用缺失＝store-corrupt"——调用方已断言引用存在）。
    auto bytes = tryObject(oid, cv);
    if (!bytes) {
        throw StoreError(StoreErrorCode::StoreCorrupt,
                         "project/object-store: referenced-object-missing oid="
                             + oid.toCanonical() + " cv=" + cv.toCanonical());
    }
    return std::move(*bytes);
}

// =====================================================================
// 扫描（引用存在性）
// =====================================================================

ObjectScanReport ObjectStore::scanObjects(
    const std::vector<ObjectKey>& references, bool verifyDigest) const
{
    // ---- 引用集装配（校验＋去重集：磁盘→引用方向的成员判定用） ----
    std::unordered_set<ObjectKey, ObjectKeyHash> refSet;
    refSet.reserve(references.size());
    for (const ObjectKey& ref : references) {
        if (!ref.oid.isValid() || !ref.cv.isValid()) {
            // 调用方契约违约（引用表混入保留值）——fail-fast 而非静默跳过
            // （跳过会把坏引用伪装成"无此引用"）。
            throw std::invalid_argument(
                "project/object-store: scanObjects references contain "
                "invalid key");
        }
        refSet.insert(ref);
    }

    ObjectScanReport report;

    // ---- 方向一：引用→磁盘（有引用无对象/有对象但损坏） ----
    for (const ObjectKey& ref : references) {
        const std::filesystem::path file = objectFile(ref.oid, ref.cv);
        std::error_code ec;
        const auto fileSize = fs::file_size(file, ec);
        if (ec) {
            if (isNotFound(ec)) {
                // 有引用无对象——报告桶由调用方映射 store-corrupt（§4.6/
                // §7.4 ④）；扫描不抛＝一次扫描产出完整清单。
                report.missingReferenced.push_back(ref);
            } else {
                // 存在却探测失败（权限等）——归入损坏桶（无法证明完整性，
                // 归类理由同 readErrorToStoreError）。
                report.corruptReferenced.push_back(ref);
            }
            continue;
        }
        if (!verifyDigest) {
            continue;  // 廉价档：只查存在性（PM-08 悬挂计数场景）
        }
        // 全量校验档（§7.4 ④"校验 HEAD 引用闭包完整性"）：size＋SHA-256。
        // 注意本方法为 const——不查/不写已校验集、不触缓存（扫描结论
        // 独立于运行期校验状态，报告必须可复现）。
        std::ifstream in(file, std::ios::binary);
        if (!in) {
            report.corruptReferenced.push_back(ref);
            continue;
        }
        std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(in),
                                        std::istreambuf_iterator<char>()};
        if (bytes.size() != fileSize) {
            report.corruptReferenced.push_back(ref);
            continue;
        }
        const core::ContentVersion actual = codec::contentVersionOf(
            std::string_view{reinterpret_cast<const char*>(bytes.data()),
                             bytes.size()});
        if (!(actual == ref.cv)) {
            report.corruptReferenced.push_back(ref);
        }
    }

    // ---- 方向二：磁盘→引用（悬挂对象＝有对象无引用——开发诊断） ----
    std::error_code dirEc;
    fs::directory_iterator it(m_objectsDir, dirEc);
    if (!dirEc) {
        // 对象区不存在＝空库，方向二自然无发现（不报错——新项目合法态）。
        // 迭代采用 error_code 版 increment（异常轨道会以 filesystem_error
        // 中断扫描——本方法契约是"损坏进报告桶不抛"）。
        for (; it != fs::directory_iterator(); it.increment(dirEc)) {
            if (dirEc) {
                break;  // 迭代中途中错：停止方向二（已收集结果仍有效）
            }
            const fs::directory_entry& entry = *it;
            const std::string name = entry.path().filename().string();
            std::error_code typeEc;
            if (!entry.is_directory(typeEc) || typeEc) {
                // 对象区根下的非目录条目——不合规（§4.1 只定义 <oid> 目录）。
                report.malformedEntries.push_back(name);
                continue;
            }
            // 目录名须为 core 规范文本 "obj-<32hex>"（try 轨解析——不合规
            // 进 malformed，不中断扫描）。
            const auto oid = core::ObjectId::tryFromCanonical(name);
            if (!oid) {
                report.malformedEntries.push_back(name);
                continue;
            }
            std::error_code fileEc;
            fs::directory_iterator fit(entry.path(), fileEc);
            if (fileEc) {
                continue;  // 子目录枚举失败：方向二跳过该 oid（方向一已覆盖引用）
            }
            for (; fit != fs::directory_iterator(); fit.increment(fileEc)) {
                if (fileEc) {
                    break;
                }
                const fs::directory_entry& fentry = *fit;
                const std::string fname = fentry.path().filename().string();
                std::error_code ftypeEc;
                if (!fentry.is_regular_file(ftypeEc) || ftypeEc
                    || !isHex64(fname)) {
                    // 文件名非 64 小写 hex（或非普通文件）——不合规条目。
                    report.malformedEntries.push_back(name + "/" + fname);
                    continue;
                }
                // "cv-"＋文件名 → core 解析（读回路径与写入路径同源——
                // 本地无第二套编址格式）。
                const auto fcv = core::ContentVersion::tryFromCanonical(
                    "cv-" + fname);
                if (!fcv) {
                    report.malformedEntries.push_back(name + "/" + fname);
                    continue;
                }
                const ObjectKey key{*oid, *fcv};
                if (refSet.count(key) == 0) {
                    // 有对象无引用＝悬挂对象——只报告不删除（GC 明确不做，
                    // §4.1/§7.3；删除决策永不属于对象库）。
                    report.danglingOnDisk.push_back(key);
                }
            }
        }
    }

    // ---- 确定性排序（directory_iterator 顺序未指定——报告必须可复现，
    //      测试断言与跨次扫描比对才有稳定语义） ----
    std::sort(report.missingReferenced.begin(),
              report.missingReferenced.end(), objectKeyLess);
    std::sort(report.corruptReferenced.begin(),
              report.corruptReferenced.end(), objectKeyLess);
    std::sort(report.danglingOnDisk.begin(), report.danglingOnDisk.end(),
              objectKeyLess);
    std::sort(report.malformedEntries.begin(), report.malformedEntries.end());

    // ---- 开发诊断上报（悬挂对象计数＋不合规条目——§4.6"开发诊断"落点；
    //      sink 可空＝丢弃，存储行为不受影响） ----
    if (m_sink != nullptr) {
        if (!report.danglingOnDisk.empty()) {
            m_sink->reportDev(
                "project/object-store",
                "dangling-objects count="
                    + std::to_string(report.danglingOnDisk.size()));
        }
        if (!report.malformedEntries.empty()) {
            m_sink->reportDev(
                "project/object-store",
                "malformed-entries count="
                    + std::to_string(report.malformedEntries.size()));
        }
    }
    return report;
}

// =====================================================================
// 缓存记账
// =====================================================================

void ObjectStore::cacheInsert(
    const ObjectKey& key,
    std::shared_ptr<const std::vector<std::uint8_t>> bytes)
{
    const std::size_t size = bytes->size();  // 记账口径＝负载字节数
    std::lock_guard<std::mutex> lock(m_mutex);
    // 单条目超预算＝整条不缓存（预算语义从简：逐出后立即再插会抖动，
    // 且对象读取本来就不依赖缓存存在——缓存是纯加速层）。预算为 0 时
    // 一切条目走本分支＝缓存整体停用，语义自洽。
    if (size > m_budgetBytes) {
        return;
    }
    // 逐出至容纳新条目：从链表头（最旧）开始丢弃，直到预算内或空。
    while (m_cachedBytes + size > m_budgetBytes && !m_lru.empty()) {
        const auto victim = m_index.find(m_lru.front().first);
        m_cachedBytes -= m_lru.front().second->size();
        m_index.erase(victim);
        m_lru.pop_front();
    }
    m_lru.emplace_back(key, std::move(bytes));
    m_index[m_lru.back().first] = std::prev(m_lru.end());
    m_cachedBytes += size;
}

ObjectCacheStats ObjectStore::cacheStats() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    ObjectCacheStats stats;
    stats.entryCount = m_lru.size();
    stats.byteCount = m_cachedBytes;
    stats.budgetBytes = m_budgetBytes;
    return stats;
}

std::vector<ObjectKey> ObjectStore::cacheLruOrder() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<ObjectKey> order;
    order.reserve(m_lru.size());
    for (const auto& node : m_lru) {
        order.push_back(node.first);  // 链表序＝最旧→最新
    }
    return order;
}

}  // namespace sdurws::ird::project::objstore
