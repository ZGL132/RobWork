/**
 * @file   ArchiveServiceImpl.cpp
 * @brief  归档端口实现——§5.6 begin/writeBatch/finalize/abandon 与
 *         §10.1 归档协作要素（两段式发布、重投递幂等、冲突拒绝、责任
 *         终结）的执行体（PRJ-T14；契约与实现口径见 ArchivePort.hpp/
 *         ArchiveServiceImpl.hpp 文件头，本文件注释聚焦"每一步在做什么、
 *         为什么"）。
 *
 * 设计依据：
 *   - units/project.md §5.6/§10.1/§10.2（语义锚点逐段对应——各方法注释
 *     标注出处）、§9.6～§9.8（门卫/互斥/票据）、§7.2（AtomicFile 原语
 *     的持久性/原子可见性保证——发布序列建立在"writeThrough 失败绝不
 *     进入可见性切换"的原语契约之上）、§4.1 results 行、§4.4.7（manifest
 *     字段契约与 manifestDigest 幂等判据）；
 *   - 需求 TASK-03/PM-13、CON-02/CON-04；
 *   - 任务契约 tasks/foundation/PRJ-T14.json acceptance 1～4。
 */

#include "ArchiveServiceImpl.hpp"

#include <fstream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <sdurws/ird/core/Events.hpp>
#include <sdurws/ird/core/Identity.hpp>

#include "Codec.hpp"
#include "DiagRecords.hpp"
#include "ProjectStoreImpl.hpp"

// Win32 原始错误码（mapOsError 的映射键——IFileOps 契约：FileResult.osError
// 携带 GetLastError 原始值）。本翻译单元经 AtomicFile.hpp→IFileOps.hpp→
// windows.h 已可见，显式列出依赖的码值便于对照（DraftServiceImpl.cpp
// 同款自检）。
#ifndef ERROR_DISK_FULL
#error "windows.h 未可见——ArchiveServiceImpl 的错误映射依赖 Win32 错误码"
#endif

namespace sdurws::ird::project {

namespace {

// ---- 磁盘编址常量（§4.1 results 行命名规则）----
// 目录名与查询端口（QueryPortImpl.cpp 内字面量）同值——两处消费同一
// §4.1 契约；未收敛为共享常量的原因：各文件匿名命名空间内的私有字面量，
// 值变更属格式契约变更（须同步修订 §4.1），字面量就地可读性优先（登记
// 口径，DraftServiceImpl.cpp kDraftsDirName 注释同源）。
constexpr const char* kResultsDirName = "results";
/// manifest 的固定发布名（§4.1："manifest.json 最后发布"）。
constexpr std::string_view kManifestFileName = "manifest.json";
/// 批次/manifest 暂存文件保留后缀（实现口径⑤——写路径临时名，relPath
/// 校验拒绝该后缀防止"暂存名/正式名"碰撞）。
constexpr std::string_view kWriteTempSuffix = ".ird-part";

/**
 * @brief 非抛整读一个小文件（批次/manifest 文件——工件量级，MB 内）。
 *
 * 与 DraftServiceImpl::readDraftFile 同形态：返回 nullopt＝I/O 层失败；
 * 空字节串＝合法的零字节文件（批次可为空文件——ArchiveItem 契约）。
 *
 * @param file [in] 目标文件路径
 * @return 文件字节；I/O 失败＝nullopt
 */
std::optional<std::string> readSmallFile(const std::filesystem::path& file)
{
    std::error_code ec;
    // 存在性先行：ifstream 打开失败无法区分"不存在"与"权限拒绝"，两
    // 者对校验流程的处置一致（＝该候选不可用），合并为 nullopt。
    if (!std::filesystem::exists(file, ec) || ec) {
        return std::nullopt;
    }
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    std::string bytes{std::istreambuf_iterator<char>(in),
                      std::istreambuf_iterator<char>()};
    if (in.bad()) {
        // 读中途 I/O 错误（网络盘抖动等）——半截内容不能当完整文件用。
        return std::nullopt;
    }
    return bytes;
}

/**
 * @brief Win32 原始错误码 → 稳定错误码映射（归档 I/O 面）。
 *
 * 映射规则与草稿服务（DraftServiceImpl::mapOsError）逐字一致——同一
 * 单元的写路径错误归类必须同表（§5.0 封闭集内就近归类，不私扩）：
 *   - ERROR_DISK_FULL → DiskFull（磁盘满）；
 *   - ERROR_ACCESS_DENIED → AccessDenied（介质/权限拒绝）；
 *   - 其余 → WriteRejected（写被环境拒绝的兜底类，原始码进 detail）。
 *
 * @param osError [in] FileResult / std::error_code 携带的 Win32 原始码
 * @return 稳定错误码
 */
StoreErrorCode mapOsError(unsigned long osError)
{
    if (osError == ERROR_DISK_FULL) {
        return StoreErrorCode::DiskFull;
    }
    if (osError == ERROR_ACCESS_DENIED) {
        return StoreErrorCode::AccessDenied;
    }
    return StoreErrorCode::WriteRejected;
}

/**
 * @brief Windows 保留设备名判定（relPath 末分量防线之一——写入 "CON"
 *        等名字会被 Win32 重定向到设备而非文件，属白名单拒绝项）。
 *
 * @param name [in] 路径末分量（已判非空；大小写不敏感——NTFS 惯例）
 * @return true＝保留设备名（含带扩展名形态 "CON.txt"——设备名重定向
 *         对带扩展名形态同样生效）
 */
bool isWindowsReservedDeviceName(const std::string& name)
{
    // 取主名（首个 '.' 之前）——"CON.txt" 的主名仍是 CON。
    const std::size_t dot = name.find('.');
    std::string stem
        = name.substr(0, dot == std::string::npos ? name.size() : dot);
    if (stem.empty()) {
        return false;
    }
    // 小写归一（ASCII 逐字符——设备名匹配与 locale 无关）。
    for (char& c : stem) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    static constexpr const char* kReserved[] = {
        "con",  "prn",  "aux",  "nul",  "com1", "com2", "com3", "com4",
        "com5", "com6", "com7", "com8", "com9", "lpt1", "lpt2", "lpt3",
        "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9"};
    for (const char* r : kReserved) {
        if (stem == r) {
            return true;
        }
    }
    return false;
}

/**
 * @brief sha256 字段的 64 小写 hex 校验（§4.4.7 items[].sha256 字段口径；
 *        与 Codec::requireHex64Field 同规则但走调用方错误通道——字段由
 *        execution 侧登记，违约属调用方错误而非磁盘损坏）。
 *
 * @param sha256 [in] 待校验摘要文本
 * @return true＝64 个 [0-9a-f]
 */
bool isHex64(std::string_view sha256)
{
    if (sha256.size() != 64) {
        return false;
    }
    for (const char c : sha256) {
        const bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        if (!hex) {
            return false;
        }
    }
    return true;
}

/**
 * @brief 登记透传串的 token 校验（runKind/evaluationKey/finalizedAtUtc
 *        ——与 Codec::requireTokenField 同规则非空/可打印 ASCII/≤128，
 *        走调用方错误通道；§4.4.7 对应字段均为该口径）。
 *
 * @param value [in] 待校验串
 * @return true＝token 合法
 */
bool isToken(std::string_view value)
{
    if (value.empty() || value.size() > 128) {
        return false;
    }
    for (const char c : value) {
        const auto u = static_cast<unsigned char>(c);
        if (u <= 0x20 || u >= 0x7F) {
            return false;
        }
    }
    return true;
}

/**
 * @brief 运行目录内相对路径 → 文件系统路径（窄字符直构）。
 *
 * relPath 已由 validateRelPath 限定为可打印 ASCII（见该函数的字符集
 * 规则）——窄字符串到 path 的本地码页转换对 ASCII 字节是恒等映射，
 * 无需宽字符/UTF-8 转换面（u8path 的 UTF-8 解码语义反而会把越界字节
 * 推入替换符路径——字符集收口后该面不存在）。
 *
 * @param relPath [in] 已过白名单的相对路径
 * @return 运行目录内的目标路径（调用方拼接运行目录）
 */
std::filesystem::path relPathToPath(const std::string& relPath)
{
    return std::filesystem::path(relPath);
}

/**
 * @brief 暂存文件路径（正式路径＋保留后缀——实现口径⑤；参数为宽路径
 *        直拼，后缀为 ASCII 字面量恒等转宽）。
 */
std::wstring tempPathOf(const std::filesystem::path& target)
{
    return target.wstring()
           + std::wstring{kWriteTempSuffix.begin(), kWriteTempSuffix.end()};
}

}  // namespace

// =====================================================================
// toToken（ArchiveEndReason——公共头声明、实现同址）
// =====================================================================

const char* toToken(ArchiveEndReason reason) noexcept
{
    switch (reason) {
    case ArchiveEndReason::Completed:       return "completed";
    case ArchiveEndReason::Canceled:        return "canceled";
    case ArchiveEndReason::Failed:          return "failed";
    case ArchiveEndReason::ForceTerminated: return "force-terminated";
    }
    return "unknown";  // 防御：switch 全覆盖后不可达（DraftOrigin 同款）
}

// =====================================================================
// 构造/析构与私有设施
// =====================================================================

ArchiveServiceImpl::ArchiveServiceImpl(ProjectStoreImpl& host) : m_host(host)
{
}

ArchiveServiceImpl::~ArchiveServiceImpl()
{
    // 强制终结全部在途会话（§9.7 析构静默终局口径的归档面兑现）：
    // 析构时点宿主已完成 finishClose（~ProjectStoreImpl 函数体先于成员
    // 析构执行、m_archive 声明序最末＝成员逆序析构中最先销毁）——票据
    // 删除器观测宿主已 Closed 不再触发排空回调；本方法只摘除登记＋置
    // 失效＋释放票据引用。析构路径无 Observer 回调（上下文正在销毁）。
    std::vector<std::shared_ptr<ArchiveSessionState>> dying;
    {
        std::lock_guard<std::mutex> guard(m_sessionsMutex);
        dying.reserve(m_sessions.size());
        for (auto& kv : m_sessions) {
            kv.second->ended = true;  // 残留句柄自此失效（fail-fast 面）
            dying.push_back(std::move(kv.second));
        }
        m_sessions.clear();
    }
    // 锁外释放票据（锁序纪律：删除器取宿主 lifecycle 锁——不在本类
    // 锁内触发）。
    for (auto& state : dying) {
        state->inFlight.reset();
    }
}

std::size_t ArchiveServiceImpl::activeSessionCount() const
{
    std::lock_guard<std::mutex> guard(m_sessionsMutex);
    return m_sessions.size();
}

std::shared_ptr<ArchiveSessionState> ArchiveServiceImpl::resolveActiveSession(
    const ArchiveSessionRef& session) const
{
    // 空句柄＝调用方契约违约（拿 begin 的返回值是唯一合法来源）。
    if (!session) {
        throw std::invalid_argument(
            "project/archive: 空会话句柄（须使用 begin 返回的句柄）");
    }
    // 句柄底层存储指针即状态指针（begin 以别名 shared_ptr 构造——
    // ArchiveSessionRef 私有构造通道；本类是其唯一友元）。注册表查无
    // ＝两种形态之一：非 begin 产物拼造的句柄（契约违约）或已终结
    // 会话（已摘登记）——两者对写方法都是 fail-fast（实现口径⑦；
    // abandon 的幂等面由其自行按"查无即幂等"处置）。
    const auto* key = static_cast<const ArchiveSessionState*>(session.m_state.get());
    std::lock_guard<std::mutex> guard(m_sessionsMutex);
    const auto found = m_sessions.find(key);
    if (found == m_sessions.end()) {
        throw std::invalid_argument(
            "project/archive: 会话句柄已失效或非本端口签发"
            "（finalize/abandon 终结后不得复用）");
    }
    return found->second;
}

bool ArchiveServiceImpl::requireWriteAuthority(std::string_view action) const
{
    // 只读上下文（从未持锁）＝无锁对象可探测——begin 门卫第②道已拒绝
    // 该类上下文建立会话，此处防御性短路（会话存续期上下文不会从可写
    // 降为只读——写权限只随锁对象存在）。
    if (m_host.m_lock == nullptr) {
        return false;
    }
    if (!m_host.m_lock->requireWriteAuthority()) {
        // 失权＝环境类状态错误：PRJ-WRITE-AUTHORITY-LOST（diagnostics.md
        // §4.6 收编清单——diagrec 唯一装配点工厂），调用方按各自错误
        // 通道（异常/ArchiveStatus）处置。
        if (m_host.m_sink != nullptr) {
            try {
                m_host.m_sink->report(diagrec::makeWriteAuthorityLost(
                    "project/archive: 写拒绝（锁失权探测失败） action="
                    + std::string{action}));
            } catch (...) {
                // 诊断通道故障不改变失权判定（失败语义由错误通道承载）。
            }
        }
        return false;
    }
    return true;
}

void ArchiveServiceImpl::validateRelPath(const std::string& relPath)
{
    // 长度预算（实现口径：防失控字段——§4.4.7 relPath 为自由路径串，
    // 运行目录内相对名的合理预算取 256，覆盖"子目录/文件名"两级命名）。
    if (relPath.empty() || relPath.size() > 256) {
        throw std::invalid_argument(
            "project/archive: relPath 长度越界（1~256）");
    }
    // 字符集收口：可打印 ASCII（0x20~0x7E）——窄字符串到 path 的本地
    // 码页转换对 ASCII 恒等（relPathToPath 注释）；控制字符与多字节
    // 序列一并排除（路径格式的 ASCII 磁盘面——§4.8 同源纪律）。
    for (const char c : relPath) {
        const auto u = static_cast<unsigned char>(c);
        if (u < 0x20 || u >= 0x7F) {
            throw std::invalid_argument(
                "project/archive: relPath 须为可打印 ASCII");
        }
    }
    // 绝对形态/盘符/反斜杠拒绝（'/' 为唯一分隔——§4.4.7 relPath 相对
    // 路径口径的结构面；'\\' 与 ':' 同时封锁 Windows 备用路径语法）。
    if (relPath.front() == '/') {
        throw std::invalid_argument(
            "project/archive: relPath 须为运行目录内相对路径（禁绝对形态）");
    }
    if (relPath.find('\\') != std::string::npos
        || relPath.find(':') != std::string::npos) {
        throw std::invalid_argument(
            "project/archive: relPath 含非法字符（'\\' 与 ':' 拒绝）");
    }
    // 逐分量校验：按 '/' 切分，禁空/"."/".."（穿越防护——NFR-SEC-01
    // 消费侧：manifest 声明路径与写入路径共用本校验，声明面同样不得
    // 指向运行目录之外）。
    std::string_view sv{relPath};
    std::string_view last;
    std::size_t components = 0;
    while (!sv.empty()) {
        const std::size_t slash = sv.find('/');
        const std::string_view part = sv.substr(0, slash);
        if (part.empty() || part == "." || part == "..") {
            throw std::invalid_argument(
                "project/archive: relPath 分量非法（禁空/./..——穿越防护）");
        }
        last = part;
        ++components;
        if (slash == std::string_view::npos) {
            break;
        }
        sv = sv.substr(slash + 1);
    }
    // 保留名：manifest.json 是 finalize 的发布名（批次占用会与完整性
    // 发布冲突）；.ird-part 后缀是写路径暂存名（实现口径⑤）；Windows
    // 设备名（重定向防线）。
    if (components == 1 && last == kManifestFileName) {
        throw std::invalid_argument(
            "project/archive: relPath 不得为保留名 manifest.json（finalize"
            " 发布名）");
    }
    if (last.size() >= kWriteTempSuffix.size()
        && last.compare(last.size() - kWriteTempSuffix.size(),
                        kWriteTempSuffix.size(), kWriteTempSuffix)
               == 0) {
        throw std::invalid_argument(
            "project/archive: relPath 不得以保留后缀 .ird-part 结尾（暂存名）");
    }
    if (isWindowsReservedDeviceName(std::string{last})) {
        throw std::invalid_argument(
            "project/archive: relPath 末分量为 Windows 保留设备名");
    }
}

void ArchiveServiceImpl::validateManifestFields(const RunManifest& manifest)
{
    // 五元组有效性（core 契约：缺一即 false——attempt 0/全零身份拒绝）。
    if (!manifest.taskIdentity.isValid()) {
        throw std::invalid_argument(
            "project/archive: manifest.taskIdentity 五元组无效");
    }
    // items ≥1（§4.4.7 必填约束——空运行不发布 manifest；解析侧同规则）。
    if (manifest.items.empty()) {
        throw std::invalid_argument(
            "project/archive: manifest.items 不得为空（§4.4.7 ≥1）");
    }
    for (const RunManifestItem& item : manifest.items) {
        validateRelPath(item.relPath);
        if (!isHex64(item.sha256)) {
            throw std::invalid_argument(
                "project/archive: items[].sha256 须为 64 个小写十六进制字符");
        }
    }
    // 登记透传串的 token 口径（runKind/evaluationKey/finalizedAtUtc）。
    // finalizedAtUtc 由调用方供给——D-14 幂等要求重投递携带与首次相同
    // 的 manifest（含该时刻），project 不代填（代填会使重投递摘要必
    // 不一致，与幂等判据矛盾——实现口径，ArchiveServiceImpl.hpp 头）。
    if (!isToken(manifest.runKind) || !isToken(manifest.evaluationKey)
        || !isToken(manifest.finalizedAtUtc)) {
        throw std::invalid_argument(
            "project/archive: runKind/evaluationKey/finalizedAtUtc 须为"
            "非空可打印 ASCII token（≤128）");
    }
}

std::string computeManifestDigestHex(const RunManifest& content)
{
    // 被摘要对象＝除 manifestDigest 外全部字段的 canonical 编码（实现
    // 口径③）：digest 置空串后走 codec::dump（固定字段序＝§4.4.7 表
    // 列序——同值必同字节，NFR-COR-02），字节经 contentVersionOf（CR-02
    // 唯一哈希路径＝core ContentDigester，P-PR-1 消费基线）。空字段字节
    // 参与摘要——发布侧/重投递比对侧计算同一函数，判定自洽。
    RunManifest probe = content;
    probe.manifestDigest.clear();
    const std::string canonical = codec::dump(probe);
    std::string hex = codec::contentVersionOf(canonical).toCanonical();
    // "cv-<64hex>" → 64hex（§4.4.7 manifestDigest 字段口径与解析侧
    // requireHex64Field 对齐）。
    return hex.substr(3);
}

void ArchiveServiceImpl::reportDev(const std::string& channel,
                                   const std::string& message) const noexcept
{
    // sink 可空＝丢弃（§5.0）；诊断通道自身故障静默（业务结果由返回
    // 值/异常承载——DraftServiceImpl::reportDev 同款）。
    if (m_host.m_sink == nullptr) {
        return;
    }
    try {
        m_host.m_sink->reportDev(channel, message);
    } catch (...) {
    }
}

void ArchiveServiceImpl::endSession(
    const std::shared_ptr<ArchiveSessionState>& state, bool finalized)
{
    // 终结收尾（ArchiveServiceImpl.hpp 契约）：锁内置态＋摘登记，锁外
    // 释放票据——删除器可能使宿主排空归零并完成关闭（PRJ-TX-8 的
    // "完成后才 Closed"触发点），该路径不得在本类锁内发生（锁序纪律）。
    std::shared_ptr<void> ticket;
    {
        std::lock_guard<std::mutex> guard(m_sessionsMutex);
        state->finalized = finalized;
        state->ended = true;
        m_sessions.erase(state.get());
        ticket = std::move(state->inFlight);
    }
    ticket.reset();  // 释放＝引用归零（排空完成的触发点之一，§9.7）
}

void ArchiveServiceImpl::publishResultArchived(
    const core::TaskIdentity& task) const noexcept
{
    // 总线可空＝跳过事件步（§5.1 装配口径——测试/无装配场景）。
    if (m_host.m_eventBus == nullptr) {
        return;
    }
    // ResultArchived 载荷只携五元组（core §4.9：事件不携路径——消费方
    // 经登记记录/端口取数）。make 工厂补 id/emittedAtUtc（core 契约——
    // P-PR-1 消费基线 v0.1，零 core 修改）。
    core::ResultArchivedPayload payload;
    payload.task = task;
    const core::DomainEvent event = core::DomainEvent::make(payload);
    // 失败重试一次＋开发诊断（D-18 同源口径——事件非数据源，归档完成
    // 事实不受影响；首败即诊断，重试再败注明不回滚）。绝不抛出。
    for (int attempt = 0; attempt < 2; ++attempt) {
        try {
            m_host.m_eventBus->publish(event);
            return;
        } catch (const std::exception& e) {
            reportDev("project/archive",
                      std::string{"event-publish-failed（"}
                          + (attempt == 0 ? "首败——按协议重试一次"
                                          : "重试后仍失败——不影响归档完成，"
                                            "D-18")
                          + "） event=result-archived error=" + e.what());
        }
    }
}

// =====================================================================
// begin——归档预留与运行目录创建（§5.6 校验清单＋§4.1 results 行）
// =====================================================================

ArchiveSessionRef ArchiveServiceImpl::begin(const ArchiveRequest& request)
{
    // ---- 调用方契约校验（先于一切锁与 I/O；fail-fast 不产出稳定码）：
    // ①五元组有效（core 契约——attempt 0/空身份无归档意义）。
    if (!request.task.isValid()) {
        throw std::invalid_argument(
            "project/archive: request.task 五元组无效");
    }
    // ②项目归属（AT-10 反例防线：跨项目写入在 begin 即拒绝——PRJ-TX-8
    //   "跨项目事件不写入"的第一道闸；开发诊断留观察面，异常面 fail-
    //   fast 与草稿服务跨项目拒绝同口径）。
    if (!(request.task.project == m_host.projectId())) {
        reportDev("project/archive",
                  "begin rejected（跨项目归档请求） task-project="
                      + request.task.project.toCanonical()
                      + " context=" + m_host.projectId().toCanonical());
        throw std::invalid_argument(
            "project/archive: request.task.project 与存储上下文不一致"
            "（拒绝跨项目写入——AT-10）");
    }
    // ③登记透传串 token 口径（runKind/evaluationKey——随 manifest 持
    //   久化的字段，违约即调用方错误）。
    if (!isToken(request.runKind) || !isToken(request.evaluationKey)) {
        throw std::invalid_argument(
            "project/archive: runKind/evaluationKey 须为非空可打印 ASCII"
            " token（≤128）");
    }
    // ④runDir 白名单＋run 绑定（实现口径②：weakly_canonical 相等——
    //   A8"归档位置不重新推导"的落点：位置取自登记记录，本端口只裁决
    //   "登记位置＝本存储 results/<run-id>"，不一致即拒绝，不静默重定
    //   向；weakly_canonical 不要求路径存在，跨拼写（大小写/相对段）
    //   收敛）。
    std::error_code canonEc;
    const std::filesystem::path requested
        = std::filesystem::weakly_canonical(request.runDir, canonEc);
    if (canonEc) {
        throw std::invalid_argument(
            "project/archive: runDir 不可解析（weakly_canonical 失败："
            + canonEc.message() + "）");
    }
    const std::filesystem::path expected
        = std::filesystem::weakly_canonical(m_host.m_canonicalDirFs
                                                / kResultsDirName
                                            / request.task.run.toCanonical());
    if (requested != expected) {
        throw std::invalid_argument(
            "project/archive: runDir 不在本存储 results/<run-id> 白名单内"
            "（§10.1 A8——拒绝，不重定向） run="
            + request.task.run.toCanonical());
    }

    // ---- 写门卫第①道：上下文状态机（§9.6①；与 executeCommit 同序
    // 同码——begin 无返回值轨错误通道，走异常，§5 章约定）。Draining
    // 拒绝**新** begin（§9.7：排空期不再接受新归档预留——已开始的会话
    // 不受影响，PRJ-TX-8 存活机制的关键分野）；Closed 同码（context-
    // closing 并入 ContextClosed 语义——§5.1 表）。用户级诊断
    // PRJ-WRITE-AUTHORITY-LOST 与 executeCommit 装配点同源。
    {
        std::lock_guard<std::mutex> guard(m_host.m_lifecycleMutex);
        if (m_host.m_state != StoreLifecycleState::Active) {
            const char* stateName
                = m_host.m_state == StoreLifecycleState::Draining ? "draining"
                                                                  : "closed";
            if (m_host.m_sink != nullptr) {
                try {
                    m_host.m_sink->report(diagrec::makeWriteAuthorityLost(
                        "project/archive: 写拒绝（归档 begin——上下文非 "
                        "Active） state=" + std::string{stateName}));
                } catch (...) {
                }
            }
            throw StoreError(StoreErrorCode::ContextClosed,
                             "project/archive: context not writable state="
                                 + std::string{stateName});
        }
    }
    // ---- 写门卫第②道：从未持锁＝只读上下文（PM-07——归档是写面，
    // 拒绝＋PRJ-LOCK-HELD，诊断装配点与 executeCommit 同源）。
    if (m_host.m_lock == nullptr) {
        if (m_host.m_sink != nullptr) {
            try {
                m_host.m_sink->report(diagrec::makeLockHeld(
                    "project/archive: 写拒绝（归档 begin——只读上下文）"));
            } catch (...) {
            }
        }
        throw StoreError(StoreErrorCode::LockHeldByOther,
                         "project/archive: readonly context（未持有写锁）");
    }
    // ---- 写门卫第③道：权威探测（§9.6①②防线③——失权即拒绝）。
    if (!requireWriteAuthority("begin")) {
        throw StoreError(StoreErrorCode::WriteRejected,
                         "project/archive: write authority check failed"
                         "（句柄已释放或已失权）");
    }

    // ---- 在途票据（§9.7"归档会话"引用持有通道——会话存续期持票，
    // requestClose 排空等待覆盖在途归档）。Closed 后取票为空＝关闭窗口
    // （门卫①与取票之间的竞争在此闭合——拒绝而非继续写）。
    auto inFlight = m_host.acquireInFlight();
    if (!inFlight) {
        throw StoreError(StoreErrorCode::ContextClosed,
                         "project/archive: context closed（票据获取失败——"
                         "关闭窗口）");
    }

    // ---- writer 互斥段（§9.8：目录创建/manifest 存在性检查/会话登记
    // 串行于同一写通道——与事务/草稿/其他会话写互斥）。
    const std::lock_guard<std::mutex> writerGuard(m_host.m_writerMutex);

    // 第 1 步：运行目录就位（§4.1 results 行"何时创建＝归档 begin 时"；
    // 非抛形态——失败按环境码抛出，语义与草稿目录创建同表）。
    std::error_code ec;
    std::filesystem::create_directories(requested, ec);
    if (ec) {
        throw StoreError(
            mapOsError(static_cast<unsigned long>(ec.value())),
            "project/archive: create results dir failed path="
                + requested.string() + " osError=" + std::to_string(ec.value()));
    }

    // 第 2 步：同 runId 已有 manifest 的身份比对（§5.6 begin 校验清单
    // 第 4 项；实现口径①——身份比对是"请求侧幂等判据"的落点：请求
    // 不携摘要（摘要在 finalize 侧才可计算），内容级 D-14 摘要幂等仍
    // 由 finalize 承载）。解析失败＝数据侧损坏（既有 manifest 不可解读，
    // 拒绝归档而非覆盖——store-corrupt）。
    const std::filesystem::path manifestFile
        = requested / std::string{kManifestFileName};
    if (std::filesystem::exists(manifestFile, ec) && !ec) {
        const auto bytes = readSmallFile(manifestFile);
        if (!bytes.has_value()) {
            throw StoreError(StoreErrorCode::StoreCorrupt,
                             "project/archive: 既有 manifest 不可读 path="
                                 + manifestFile.string());
        }
        RunManifest existing;
        try {
            existing = codec::parseRunManifest(*bytes);
        } catch (const StoreError& e) {
            throw StoreError(StoreErrorCode::StoreCorrupt,
                             "project/archive: 既有 manifest 解析拒绝 path="
                                 + manifestFile.string() + " detail="
                                 + e.what());
        }
        // 身份比对：五元组（含 attempt——同 runId 不同 attempt 即另一
        // 份登记事实）＋登记透传两串。不符＝ArchiveConflict（同一 run
        // 目录绑定一份登记事实——重登记即冲突）＋用户级诊断。
        if (!(existing.taskIdentity == request.task)
            || existing.runKind != request.runKind
            || existing.evaluationKey != request.evaluationKey) {
            if (m_host.m_sink != nullptr) {
                try {
                    m_host.m_sink->report(diagrec::makeArchiveConflict(
                        "project/archive: begin 身份冲突 run="
                            + request.task.run.toCanonical()));
                } catch (...) {
                }
            }
            throw StoreError(
                StoreErrorCode::ArchiveConflict,
                "project/archive: 同 runId 已有不同身份的 manifest（幂等"
                "身份比对不符——§10.1） run="
                    + request.task.run.toCanonical());
        }
        // 一致＝放行（幂等重投递的 begin——后续 finalize 走 D-14 摘要
        // 幂等；批次写入走"摘要一致跳过/不一致冲突"）。
        reportDev("project/archive",
                  "begin idempotent（同 runId 身份一致的既有 manifest——"
                  "重投递放行） run=" + request.task.run.toCanonical());
    } else if (ec) {
        // stat 失败（权限类）——按环境错误拒绝（不能把"查不到"当"没有"）。
        throw StoreError(
            mapOsError(static_cast<unsigned long>(ec.value())),
            "project/archive: stat manifest failed path="
                + manifestFile.string() + " osError="
                + std::to_string(ec.value()));
    }

    // 第 3 步：会话登记（同 run 并发重复会话拒绝——单写者纪律；持
    // writer 互斥下检查与登记原子）。
    auto state = std::make_shared<ArchiveSessionState>();
    state->task = request.task;
    state->runDir = requested;
    state->inFlight = std::move(inFlight);
    {
        std::lock_guard<std::mutex> guard(m_sessionsMutex);
        for (const auto& kv : m_sessions) {
            if (kv.second->task.run == request.task.run) {
                throw StoreError(
                    StoreErrorCode::ArchiveConflict,
                    "project/archive: 同 runId 已有活跃归档会话 run="
                        + request.task.run.toCanonical());
            }
        }
        m_sessions.emplace(state.get(), state);
    }

    reportDev("project/archive",
              "begin ok run=" + request.task.run.toCanonical() + " dir="
                  + requested.string());
    // 句柄构造通道＝别名 shared_ptr 类型擦除（存储指针即状态指针——
    // resolveActiveSession 据此反查注册表；所有权与状态共享）。
    return ArchiveSessionRef{std::shared_ptr<void>(state, state.get())};
}

// =====================================================================
// writeBatch——分批写入（§10.1"分批写入与最终完整发布"/"批次级重投递"）
// =====================================================================

ArchiveStatus ArchiveServiceImpl::writeBatch(ArchiveSessionRef session,
                                             const ArchiveBatch& batch)
{
    // 会话解析（空/未知/已终结＝fail-fast——实现口径⑦）。
    const std::shared_ptr<ArchiveSessionState> state
        = resolveActiveSession(session);

    // relPath 白名单全量前置校验（调用方契约——任一违约即整体失败，
    // 零磁盘副作用：批次内不出现"前几个已写、后一个拒"的半批形态）。
    for (const ArchiveItem& item : batch.items) {
        validateRelPath(item.relPath);
    }

    // 失权防护（§9.6②联动——每笔写操作开始时探测；失权走返回值轨，
    // PRJ-WRITE-AUTHORITY-LOST 已在探测内产出）。
    if (!requireWriteAuthority("writeBatch")) {
        return ArchiveStatus{
            false,
            StoreError{StoreErrorCode::WriteRejected,
                       "project/archive: write authority check failed"
                       "（batch——句柄已释放或已失权）"}};
    }

    // writer 互斥段（§9.8——批次写与事务/草稿/其他会话写在同一串行化
    // 点汇合；目录内单写者假设由此成立）。
    const std::lock_guard<std::mutex> writerGuard(m_host.m_writerMutex);

    for (const ArchiveItem& item : batch.items) {
        const std::filesystem::path target
            = state->runDir / relPathToPath(item.relPath);
        // 深层分量目录就位（relPath 可含子目录——"批次文件任意名"的
        // 目录面；非抛形态，失败按环境码返回）。
        std::error_code ec;
        std::filesystem::create_directories(target.parent_path(), ec);
        if (ec) {
            return ArchiveStatus{
                false,
                StoreError{mapOsError(static_cast<unsigned long>(ec.value())),
                           "project/archive: create batch dir failed path="
                               + target.parent_path().string() + " osError="
                               + std::to_string(ec.value())}};
        }

        // 入库摘要（内容寻址——CR-02 唯一哈希路径）：幂等比对与暂存
        // 共用同一次计算（批次字节通常远小于重算成本）。
        const core::ContentVersion incoming = codec::contentVersionOf(
            std::string_view{reinterpret_cast<const char*>(item.bytes.data()),
                             item.bytes.size()});

        if (std::filesystem::exists(target, ec) && !ec) {
            // 目标已存在（批次级重投递）：摘要一致→跳过（幂等，磁盘零
            // 写入）；不一致→冲突拒绝＋PRJ-ARCHIVE-CONFLICT（§10.1"批次
            // 级重投递"行——数据分歧不掩盖、既有文件不覆盖）。
            const auto existing = readSmallFile(target);
            if (!existing.has_value()) {
                return ArchiveStatus{
                    false,
                    StoreError{StoreErrorCode::StoreCorrupt,
                               "project/archive: 既有批次文件不可读 path="
                                   + target.string()}};
            }
            if (codec::contentVersionOf(*existing) != incoming) {
                if (m_host.m_sink != nullptr) {
                    try {
                        m_host.m_sink->report(diagrec::makeArchiveConflict(
                            "project/archive: 批次内容冲突 relPath="
                                + item.relPath));
                    } catch (...) {
                        // 诊断通道故障不改变冲突判定。
                    }
                }
                return ArchiveStatus{
                    false,
                    StoreError{StoreErrorCode::ArchiveConflict,
                               "project/archive: 批次重投递内容不一致"
                               "（§10.1 冲突拒绝） relPath=" + item.relPath}};
            }
            continue;  // 幂等跳过。
        }
        if (ec) {
            // stat 失败（权限类）——按环境错误返回。
            return ArchiveStatus{
                false,
                StoreError{mapOsError(static_cast<unsigned long>(ec.value())),
                           "project/archive: stat batch file failed path="
                               + target.string() + " osError="
                               + std::to_string(ec.value())}};
        }

        // 目标不存在：暂存写（writeThrough＝写穿透＋FlushFileBuffers 持
        // 久性闸门——§7.2 行 1）→ 只增发布（publishNew——批次文件只增，
        // §4.1 results 行；rename 原子，"存在的批次文件必完整"由此成立，
        // 幂等摘要比对才有意义——半写文件会被误判为内容分歧）。
        const std::wstring temp = tempPathOf(target);
        const auto written = m_file.writeThrough(
            temp, reinterpret_cast<const char*>(item.bytes.data()),
            item.bytes.size());
        if (!written.ok) {
            // 持久性闸门失败：暂存件不可信——删除（尽力而为，失败经
            // 开发诊断可见）并返回环境错误；正式路径未被触碰。
            std::error_code rmEc;
            std::filesystem::remove(temp, rmEc);
            reportDev("project/archive",
                      "batch temp cleanup after write-failure file="
                          + target.string() + " osError="
                          + std::to_string(written.osError));
            return ArchiveStatus{
                false,
                StoreError{mapOsError(written.osError),
                           "project/archive: batch writeThrough failed"
                           " relPath=" + item.relPath}};
        }
        const auto published
            = m_file.publishNew(temp, target.wstring());
        if (!published.ok) {
            // 只增发布失败（目标被外部创建/环境拒绝）：删除暂存件，返回
            // 环境错误（同进程内 writer 互斥下无自碰撞——外部干预按
            // write-rejected 兜底归类，原始码进 detail）。
            std::error_code rmEc;
            std::filesystem::remove(temp, rmEc);
            reportDev("project/archive",
                      "batch publish failed file=" + target.string()
                          + " osError=" + std::to_string(published.osError));
            return ArchiveStatus{
                false,
                StoreError{mapOsError(published.osError),
                           "project/archive: batch publishNew failed"
                           " relPath=" + item.relPath}};
        }
    }

    return ArchiveStatus{true, std::nullopt};
}

// =====================================================================
// finalize——manifest 原子发布＝完整判据（D-13；D-14 幂等/冲突）
// =====================================================================

ArchiveStatus ArchiveServiceImpl::finalize(ArchiveSessionRef session,
                                           const RunManifest& manifest)
{
    // 会话解析（空/未知/已终结＝fail-fast）＋调用方契约校验：
    const std::shared_ptr<ArchiveSessionState> state
        = resolveActiveSession(session);
    // taskIdentity 与会话五元组一致（会话绑定 begin 登记事实——finalize
    // 不得改写；不符＝调用方契约违约）。
    if (!(manifest.taskIdentity == state->task)) {
        throw std::invalid_argument(
            "project/archive: manifest.taskIdentity 与会话五元组不一致");
    }
    validateManifestFields(manifest);

    // 失权防护（§9.6②——同 writeBatch）。
    if (!requireWriteAuthority("finalize")) {
        return ArchiveStatus{
            false,
            StoreError{StoreErrorCode::WriteRejected,
                       "project/archive: write authority check failed"
                       "（finalize——句柄已释放或已失权）"}};
    }

    // ---- 第 1 步（锁外，纯 CPU）：manifestDigest 计算（实现口径③——
    // 编码归 project，§4.8；调用方该字段不参与）。摘要同时用于发布
    // 字节与幂等判定；canonical 编码的 UTF-8 契约违约（runKind 等
    // 理论上 ASCII——防御面）在此抛 invalid_argument 透传。
    RunManifest out = manifest;
    out.manifestDigest = computeManifestDigestHex(manifest);
    const std::string canonical = codec::dump(out);

    // ---- writer 互斥段（§9.8——磁盘核验与发布原子于互斥内：并发
    // finalize/abandon/批次写在互斥外排队，终局以本段为准）。
    const std::lock_guard<std::mutex> writerGuard(m_host.m_writerMutex);

    // 第 2 步：同 run 已有 manifest 的 D-14 摘要比对（§10.1"重投递幂等
    // 与内容冲突"行）。比对先于磁盘核验（顺序语义）：manifest 在＝该
    // 运行已完整（D-13），重投递只与"已发布事实"比对——摘要一致＝幂等、
    // 不一致＝冲突；磁盘核验仅把守发布路径（无 manifest 时才需要——
    // 否则"声明了未写入文件"的分歧重投递会被误判为 store-corrupt 而非
    // archive-conflict）。
    const std::filesystem::path manifestFile
        = state->runDir / std::string{kManifestFileName};
    std::error_code ec;
    if (std::filesystem::exists(manifestFile, ec) && !ec) {
        const auto bytes = readSmallFile(manifestFile);
        if (!bytes.has_value()) {
            return ArchiveStatus{
                false,
                StoreError{StoreErrorCode::StoreCorrupt,
                           "project/archive: 既有 manifest 不可读 path="
                               + manifestFile.string()}};
        }
        RunManifest existing;
        try {
            existing = codec::parseRunManifest(*bytes);
        } catch (const StoreError& e) {
            return ArchiveStatus{
                false,
                StoreError{StoreErrorCode::StoreCorrupt,
                           "project/archive: 既有 manifest 解析拒绝 path="
                               + manifestFile.string() + " detail="
                               + e.what()}};
        }
        // 幂等判据＝既有摘要 vs 本次重算摘要（同一摘要函数下等价于
        // "内容全同"）。一致＝幂等成功：不重写磁盘、不重复发布事件
        // （实现口径⑥——进程内总线恰一次），会话终结释放引用。
        if (existing.manifestDigest == out.manifestDigest) {
            reportDev("project/archive",
                      "finalize idempotent（D-14 摘要一致——不重写） run="
                          + state->task.run.toCanonical());
            endSession(state, true);
            return ArchiveStatus{true, std::nullopt};
        }
        // 不一致＝冲突拒绝＋PRJ-ARCHIVE-CONFLICT（数据分歧不掩盖，既有
        // manifest 不覆盖）；会话不终结（调用方按任务状态机处置）。
        if (m_host.m_sink != nullptr) {
            try {
                m_host.m_sink->report(diagrec::makeArchiveConflict(
                    "project/archive: manifest 摘要冲突 run="
                        + state->task.run.toCanonical()));
            } catch (...) {
            }
        }
        return ArchiveStatus{
            false,
            StoreError{StoreErrorCode::ArchiveConflict,
                       "project/archive: 同 attempt 重投递 manifest 内容"
                       "不一致（D-14 冲突拒绝） run="
                           + state->task.run.toCanonical()}};
    }
    if (ec) {
        return ArchiveStatus{
            false,
            StoreError{mapOsError(static_cast<unsigned long>(ec.value())),
                       "project/archive: stat manifest failed path="
                           + manifestFile.string() + " osError="
                           + std::to_string(ec.value())}};
    }

    // 第 3 步：items 逐条对照磁盘（实现口径④——manifest 是"完整"的
    // 声明（D-13），不发布与磁盘不符的声明（O-12 完整性发布义务）；
    // 不符＝StoreCorrupt（数据侧），会话不终结（调用方可修复后重试或
    // abandon）。仅发布路径到达此处（manifest 缺席——第 2 步已分流）。
    for (const RunManifestItem& item : out.items) {
        const std::filesystem::path file
            = state->runDir / relPathToPath(item.relPath);
        const auto bytes = readSmallFile(file);
        if (!bytes.has_value()) {
            reportDev("project/archive",
                      "finalize verify failed（批次文件缺失/不可读） relPath="
                          + item.relPath);
            return ArchiveStatus{
                false,
                StoreError{StoreErrorCode::StoreCorrupt,
                           "project/archive: manifest 条目缺失或不可读"
                           " relPath=" + item.relPath}};
        }
        if (bytes->size() != item.sizeBytes) {
            reportDev("project/archive",
                      "finalize verify failed（size 不符） relPath="
                          + item.relPath + " disk="
                          + std::to_string(bytes->size()) + " declared="
                          + std::to_string(item.sizeBytes));
            return ArchiveStatus{
                false,
                StoreError{StoreErrorCode::StoreCorrupt,
                           "project/archive: manifest 条目 size 与磁盘不符"
                           " relPath=" + item.relPath}};
        }
        const core::ContentVersion diskDigest
            = codec::contentVersionOf(*bytes);
        // 声明 hex 与磁盘字节实算摘要比对（cv- 前缀剥离后逐字符——
        // CR-02：声明的摘要只透传不重算，实算对象是磁盘字节）。
        if (diskDigest.toCanonical().substr(3) != item.sha256) {
            reportDev("project/archive",
                      "finalize verify failed（sha256 不符） relPath="
                          + item.relPath);
            return ArchiveStatus{
                false,
                StoreError{StoreErrorCode::StoreCorrupt,
                           "project/archive: manifest 条目 sha256 与磁盘不符"
                           " relPath=" + item.relPath}};
        }
    }

    // 第 4 步：原子发布（§4.1 results 行"manifest 原子替换"；§7.2 行 3
    // ——write-through 暂存→replaceFile 原子就位＝D-13 提交点：发布前
    // 目录"不完整"、发布后"完整"，读者只见其一）。失败即返回环境错误，
    // 会话不终结（manifest 缺失＝仍不完整——失败不产生"假完整"）。
    const std::wstring temp = tempPathOf(manifestFile);
    const auto written
        = m_file.writeThrough(temp, canonical.data(), canonical.size());
    if (!written.ok) {
        std::error_code rmEc;
        std::filesystem::remove(temp, rmEc);
        reportDev("project/archive",
                  "finalize temp cleanup after write-failure file="
                      + manifestFile.string() + " osError="
                      + std::to_string(written.osError));
        return ArchiveStatus{
            false,
            StoreError{mapOsError(written.osError),
                       "project/archive: manifest writeThrough failed"}};
    }
    const auto published
        = m_file.replaceFile(temp, manifestFile.wstring());
    if (!published.ok) {
        std::error_code rmEc;
        std::filesystem::remove(temp, rmEc);
        reportDev("project/archive",
                  "finalize publish failed file=" + manifestFile.string()
                      + " osError=" + std::to_string(published.osError));
        return ArchiveStatus{
            false,
            StoreError{mapOsError(published.osError),
                       "project/archive: manifest 原子发布失败"}};
    }

    // ---- 第 5 步：终结→事件→票据释放（§10.2 流程序：manifest 发布＝
    // 完成事实→ResultArchived 事件→引用释放→（排空归零则）Closed）。
    // 锁内置态摘登记（幂等面收敛），事件在终结态下发布（观察者看到的
    // 已是"已归档"事实），票据最后锁外释放（排空触发点——锁序纪律）。
    {
        std::lock_guard<std::mutex> guard(m_sessionsMutex);
        state->finalized = true;
        state->ended = true;
        m_sessions.erase(state.get());
    }
    reportDev("project/archive",
              "finalize ok run=" + state->task.run.toCanonical()
                  + " manifest=" + manifestFile.string());
    publishResultArchived(state->task);
    state->inFlight.reset();  // 锁外释放（§9.7——排空归零的触发点之一）

    return ArchiveStatus{true, std::nullopt};
}

// =====================================================================
// abandon——全路径责任终结（§10.1"不存在永久等待"）
// =====================================================================

void ArchiveServiceImpl::abandon(ArchiveSessionRef session,
                                 ArchiveEndReason reason)
{
    // 空句柄＝调用方契约违约（fail-fast——与其他写方法同面）。
    if (!session) {
        throw std::invalid_argument(
            "project/archive: 空会话句柄（abandon）");
    }
    // 查表裁决：在册＝终结之；查无＝两种形态（非本端口签发的拼造句柄
    // 不可达——私有构造唯一友元；已终结会话）＝幂等 no-op（实现口径⑦
    // ——重复终结通知不产生第二次副作用，"全路径终结"的容忍形态）。
    const auto* key
        = static_cast<const ArchiveSessionState*>(session.m_state.get());
    std::shared_ptr<ArchiveSessionState> state;
    {
        std::lock_guard<std::mutex> guard(m_sessionsMutex);
        const auto found = m_sessions.find(key);
        if (found == m_sessions.end()) {
            return;  // 已终结——幂等。
        }
        state = found->second;
    }
    // 终结记录（开发观察面——四值原因只记录不解释：报告准入/当前性
    // 归 evidence/reporting，§10.1"取消/失败"行）；批次残留目录保留为
    // "未完成"（无 manifest＝D-13 不完整——不删除，CON-04 承接）。
    reportDev("project/archive",
              std::string{"abandon run="} + state->task.run.toCanonical()
                  + " reason=" + toToken(reason));
    endSession(state, false);
}

}  // namespace sdurws::ird::project
