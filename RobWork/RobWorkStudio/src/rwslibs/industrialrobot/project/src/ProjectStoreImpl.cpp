/**
 * @file   ProjectStoreImpl.cpp
 * @brief  存储上下文与工厂实现——打开协议（PM-02②③⑤）、新建项目组装
 *         （PM-01 存储侧）、恢复报告汇编（PM-08/§7.4）与生命周期排空
 *         （§9.6/§9.7）的落地（PRJ-T08 实现载体）。
 *
 * 设计依据：
 *   - units/project.md §5.1（open/createNew 契约与前置/后置/副作用表）、
 *     §8.7（打开协议服务侧逐步表：②目录形态与版本检查/③加载与读校验/
 *     ⑤激活与恢复诊断；"激活前失败不影响当前项目"）、§7.4（恢复顺序
 *     ①③④⑤——①④⑤经 TxEngine::scanForRecovery、②锁残留经 StoreLock、
 *     ③孤儿草稿本文件扫描）、§7.3（闭包外＝未提交残留，查询不可见——
 *     装载纪律只认闭包成员）、§9.2～§9.5（第二实例只读/路径规范化/锁
 *     文件生命周期/三类失败的不同诊断）、§9.6（失权写入防护——上下文
 *     状态机门卫＋StoreLock 防线②③）、§9.7（引用持有与最终释放）、
 *     §4.1（project.json 缺失＝非项目目录；projectId 与 HEAD 不一致＝
 *     CorruptStoreDetected）、§8.4/§8.11（草稿恢复处置/版本拒绝）；
 *   - 需求 PM-01/02/03/06/07/08、SA-17（写权限唯一依据＝OS 排他句柄）、
 *     NFR-COR-02（报告确定性排序）、NFR-REL-05（用户可见文案不在
 *     project 生成——诊断记录只载数据面）；
 *   - 任务契约 tasks/foundation/PRJ-T08.json acceptance 1～4。
 *
 * P-PR-1 处置（acceptance 4）：身份类型（ProjectId/RevisionId/BranchId/
 *   ObjectId::generate）与 DiagnosticRecord 全部消费 core 公共契约头
 *   （Identity.hpp/DiagData.hpp——core.md v0.1 基线），零 core 修改；
 *   core 冻结出 diff 后按影响面增量同步（单元卡 §15.3 口径）。
 *
 * P-PR-6 处置（acceptance 4）：恢复/只读诊断码值全部取自 diagnostics.md
 *   §4.6 收编清单（PRJ-FORMAT-LEGACY/PRJ-SCHEMA-FUTURE/PRJ-STORE-CORRUPT/
 *   PRJ-RECOVERY-IGNORED-UNCOMMITTED/PRJ-RECOVERY-ORPHAN-DRAFT/
 *   PRJ-LOCK-HELD/PRJ-WRITE-AUTHORITY-LOST——不私造码），产出经 §5.0
 *   IDiagnosticsSink 适配器注入（sink 名称/归属统一归 P-PR-6/P-EX-8
 *   裁决，本单元零 diagnostics 链接边）。
 *
 * 实现口径登记（DTB §5.4——单元卡未定义判据的落值决定，随本任务留痕）：
 *   ① open 步骤顺序＝②版本检查→锁半步→③校验装载→⑤孤儿草稿扫描；
 *     PM-06 拒绝（format-legacy/schema-future）不触碰目标锁（"原文件
 *     不动"的扩展语义：不取得任何写面资源）。
 *   ② 锁 OsError 的 §9.5 映射：ERROR_ACCESS_DENIED→access-denied（打开
 *     失败）；ERROR_WRITE_PROTECT→media-read-only（Writable 请求降级
 *     只读＋PRJ-LOCK-HELD 诊断 kind=media-read-only——diagnostics.md
 *     "存储错误→诊断码映射"行）；其余原始码→access-denied（含码值
 *     detail）。显式 ReadOnly 请求不取锁、不抛（读持有者记录作
 *     lockInfo——§9.2②）。
 *   ③ createNew 的组装区＝目标父目录下"<目标名>.new-<pid>-<seq>"（同卷
 *     ——rename 约束；PM-01"先在 .staging 组装再整体就位"在新建场景的
 *     落点：项目尚不存在，白名单 .staging 随项目树在组装区一并建立）。
 *     就位＝逐顶层条目同卷 rename；任一失败→清理组装区与已就位条目
 *     （目标目录非预存时整树删除）→不留半成品（§5.1 表）。
 *   ④ 闭包装载采用"扫描预检＋正式搬运"两段：TxEngine::scanForRecovery
 *     先行完成④完整性（自持临时索引——不污染会话索引）；其结论为
 *     true 时闭包成员清单/元数据必然完好，正式装载跳过
 *     scan.uncommittedRevisions 后的重读/重解析只是搬运（遇到失败＝
 *     扫描后竞态损坏，如实按 store-corrupt 抛——防御分支，非正常路径）。
 *     闭包外修订**不注册**进会话索引（§7.3/PRJ-TX-9"闭包外修订不可见"
 *     的结构性保证——T09 查询端口消费同一索引，可见性纪律无需其重复
 *     实现）。
 *   ⑤ 孤儿草稿判定（§7.4③/§8.4 的保守版）：.new/.bak 后缀残留直接报告
 *     （崩溃现场语义）；.draft.json 解析失败/跨项目归属/分支不存在/
 *     目录与内容不一致四类计孤儿。模块 token 注册表校验归 DraftService
 *     （PRJ-T12——§4.4.5 注释口径），本扫描不代行。
 *   ⑥ 草稿 flush 钩子：requestClose 收尾序列中的"末次草稿 flush"（§9.7）
 *     随 DraftService（PRJ-T12）挂载；T08 阶段无在途草稿生产者，排空
 *     等待面只有在途引用票据。
 *   ⑦ 析构为静默终局（释放锁＋注销进程内注册表，**不发**关闭回调——
 *     上下文正在销毁，回调引用即将悬空；回调仅由 requestClose 完成路径
 *     触发）。在途票据的生存期必须短于上下文（§9.7 持有者清单的既有
 *     约束；本类无法从票据侧撤销回调目标）。
 *   ⑧ 部件装配序（指针共享纪律）：工厂先构造 ObjectStore/RevisionIndex
 *     （自由区），再以裸指针构造 TxEngine（引擎持有 index 裸指针——S6
 *     时序的语义载体），最后把三个 unique_ptr 连同引擎一并交付
 *     ProjectStoreImpl——索引对象自始至终唯一，引擎与上下文共享使用，
 *     不存在拷贝/移动分裂。
 */

#include "ProjectStoreImpl.hpp"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include <utility>

#include "Codec.hpp"
#include "win32/AtomicFile.hpp"
#include "win32/ILockOps.hpp"
#include "win32/PathCanonical.hpp"

namespace sdurws::ird::project {

namespace {

// =====================================================================
// 常量与工厂级辅助
// =====================================================================

/// 新建项目登记的创建工具版本（§4.2 createdWithToolVersion；诊断/兼容
/// 排查用透传串——阶段 A 首版固定值，版本管理随产品发布流程演进）。
constexpr const char* kCreatedWithToolVersion = "RobWork-IndustrialRobot/0.1";

/// 初始分支显示名（label 创建时一次写入——P-PR-8；单元卡 §4.5.1 走查
/// 与 TxEngineTest 种子同名的惯例主分支名）。
constexpr const char* kPrimaryBranchLabel = "main";

/// 孤儿草稿扫描的目录相对前缀（§4.1 drafts 行；报告条目以此前缀呈现，
/// 消费方（PM-15 横幅）据此定位现场文件）。
constexpr const char* kDraftsDirName = "drafts";

/// 共享无状态真实实现（函数级静态——TxEngineTest realOps() 同款先例：
/// Win32FileOps/Win32LockOps 均无状态，进程级单实例避免空对象泛滥）。
win32::IFileOps& sharedFileOps()
{
    static win32::Win32FileOps s_ops;
    return s_ops;
}

win32::ILockOps& sharedLockOps()
{
    static win32::Win32LockOps s_ops;
    return s_ops;
}

/// 元数据引用键的去重比较子（ObjectRefPair 只契约 operator==——
/// std::set/std::map 需要严格弱序：按 oid/cv 的 core 规范文本字典序。
/// 与 TxEngine.cpp 扫描期的同名比较子同构——它是该处的文件局部实现
/// 细节，本处为打开期装载的自持副本；语义面不进持久化格式）。
struct MetadataRefLess
{
    bool operator()(const ObjectRefPair& a, const ObjectRefPair& b) const
    {
        if (!(a.objectId == b.objectId)) {
            return a.objectId.toCanonical() < b.objectId.toCanonical();
        }
        return a.contentVersion.toCanonical()
               < b.contentVersion.toCanonical();
    }
};

/**
 * @brief 打开期收集型 sink——诊断双通道的装配点（RecoveryReport.
 *        diagnostics 快照与 IDiagnosticsSink 即时上报同源同序）。
 *
 * 背景说明：打开过程产生的用户级诊断既要在结果里随 RecoveryReport
 * 返回（数据面），也要经调用方注入的 sink 即时上报（§5.0 链路）。
 * 工厂把它作为统一 sink 传给打开期的全部部件（StoreLock/TxEngine/
 * ObjectStore）与本地诊断构造点；打开完成后快照拷入 RecoveryReport，
 * 此后上下文直连调用方 sink（RecoveryReport 是打开时点快照——§5.1）。
 * 开发级消息（reportDev）只转发不收集（报告只载用户级记录）。
 */
class CollectingSink final : public IDiagnosticsSink {
public:
    explicit CollectingSink(IDiagnosticsSink* forward) : m_forward(forward) {}

    void report(const core::DiagnosticRecord& record) override
    {
        m_seen.push_back(record);
        if (m_forward != nullptr) {
            m_forward->report(record);
        }
    }

    void reportDev(const std::string& channel, const std::string& message) override
    {
        if (m_forward != nullptr) {
            m_forward->reportDev(channel, message);
        }
    }

    /// 打开完成时点的用户级诊断快照（产出序＝②→①→③→⑤，确定性）。
    const std::vector<core::DiagnosticRecord>& seen() const noexcept
    {
        return m_seen;
    }

private:
    IDiagnosticsSink* m_forward;  ///< 调用方 sink（非 owning，可空）
    std::vector<core::DiagnosticRecord> m_seen;  ///< 收集快照（产出序）
};

/**
 * @brief 二进制整读文件（打开协议的磁盘读取口）。
 * @return 文件字节；不存在/不可读＝nullopt（调用方按各自契约归类——
 *         project.json 缺失＝非项目目录；manifest 缺失＝损坏或残留）。
 */
std::optional<std::string> readAllBytes(const std::filesystem::path& file)
{
    std::error_code ec;
    if (!std::filesystem::is_regular_file(file, ec) || ec) {
        return std::nullopt;
    }
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    std::string bytes{std::istreambuf_iterator<char>(in),
                      std::istreambuf_iterator<char>()};
    if (in.bad()) {
        return std::nullopt;  // 读中途硬件错误——不可作为"内容为空"使用
    }
    return bytes;
}

/// 本进程 PID（LockSelfRecord.pid 的装配点供给——StoreLock 不代取，
/// 身份来源单一；单位＝OS PID）。
std::uint32_t currentProcessId()
{
    return static_cast<std::uint32_t>(::GetCurrentProcessId());
}

/// 本机主机名（窄字符转换后供锁记录；超宽由 StoreLock 截断——§9.2）。
std::string currentHostName()
{
    wchar_t buf[MAX_COMPUTERNAME_LENGTH + 1] = {};
    DWORD size = MAX_COMPUTERNAME_LENGTH + 1;
    if (::GetComputerNameW(buf, &size) == 0 || size == 0) {
        return {};
    }
    // 主机名为系统 NetBIOS 名（ASCII 子集）；显式窄化转换（避免隐式
    // wchar_t→char 的编译警告，非 ASCII 字符截断可接受——仅诊断字段）。
    std::string narrow;
    narrow.reserve(size);
    for (DWORD i = 0; i < size; ++i) {
        narrow.push_back(static_cast<char>(buf[i]));
    }
    return narrow;
}

/**
 * @brief §9.5 锁获取 OsError → 稳定错误码映射（实现口径②）。
 * @param osError [in] CreateFileW 的 GetLastError() 原始码（无单位系统码；
 *                写保护已由调用方先行拦截为降级路径——到达这里的环境
 *                失败统一归权限类拒绝，故原始码仅随 detail 呈现）
 */
StoreErrorCode mapLockOsError(unsigned long /*osError*/)
{
    return StoreErrorCode::AccessDenied;
}

/**
 * @brief 构造 PRJ-FORMAT-LEGACY 记录（PM-06：稳定只读拒绝＋诊断码；
 *        码值＝diagnostics.md §4.6 收编清单——P-PR-6 处置）。
 */
core::DiagnosticRecord makeFormatLegacyRecord(const std::string& detail)
{
    return core::DiagnosticRecord::make(
        std::string{"PRJ-FORMAT-LEGACY"},
        std::nullopt,  // 项目级事件（subject=∅——diagnostics 映射表口径）
        std::nullopt, std::nullopt,
        "项目以旧格式保存，当前版本软件不能打开（已稳定拒绝，原文件"
        "未被改动）",
        detail,
        "使用创建该项目的旧版本软件导出为通用格式，或联系支持人员"
        "获取升级工具；不要手工修改项目文件");
}

/**
 * @brief 构造 PRJ-SCHEMA-FUTURE 记录（PM-06：未来版本拒绝＋升级指引
 *        数据——cause 承载 document/supported/upgrade 三键明细）。
 */
core::DiagnosticRecord makeSchemaFutureRecord(const std::string& detail)
{
    return core::DiagnosticRecord::make(
        std::string{"PRJ-SCHEMA-FUTURE"},
        std::nullopt,
        std::nullopt, std::nullopt,
        "项目的数据格式版本比当前软件更新，不能打开（不自动升级——"
        "PM-06；升级工具入口数据随诊断明细提供）",
        detail,
        "升级到包含该格式支持的新版本软件后重新打开；诊断明细中 "
        "document/supported/upgrade 三键为升级工具的输入指引");
}

/// PRJ-STORE-CORRUPT 记录（打开③读校验失败——定位到文件，PM-02）。
core::DiagnosticRecord makeOpenCorruptRecord(const std::string& detail)
{
    return core::DiagnosticRecord::make(
        std::string{"PRJ-STORE-CORRUPT"},
        std::nullopt,
        std::nullopt, std::nullopt,
        "项目存储完整性校验失败，已停止打开（失败位置见诊断明细）",
        detail,
        "从最近的有效备份恢复项目，或联系支持人员；不要手工修改 "
        "objects/revisions 目录（内容寻址校验会拒绝不一致的改动）");
}

/// PRJ-RECOVERY-ORPHAN-DRAFT 记录（§7.4③孤儿草稿——清单随报告返回）。
core::DiagnosticRecord makeOrphanDraftRecord(std::size_t count)
{
    return core::DiagnosticRecord::make(
        std::string{"PRJ-RECOVERY-ORPHAN-DRAFT"},
        std::nullopt,
        std::nullopt, std::nullopt,
        "打开时发现 " + std::to_string(count)
            + " 个孤立或损坏的草稿文件（清单见恢复报告），已按未提交"
            "内容保留现场",
        "上次会话的草稿保存被中断（.new/.bak 残留）或草稿归属校验"
        "未通过（PM-08：恢复诊断，可恢复旧版——§8.4）",
        "在草稿面板中查看可恢复项；确认不需要的残留可在草稿面板"
        "清理（存储区文件由软件管理，请勿手工删除）");
}

/// PRJ-WRITE-AUTHORITY-LOST 记录（上下文态写拒绝——§5.1 生命周期图
/// "迟到写请求=拒绝+诊断"；锁面拒绝的诊断由 StoreLock 产出）。
core::DiagnosticRecord makeWriteAuthorityLostRecord(const std::string& detail)
{
    return core::DiagnosticRecord::make(
        std::string{"PRJ-WRITE-AUTHORITY-LOST"},
        std::nullopt,
        std::nullopt, std::nullopt,
        "项目存储上下文已不接受写入（关闭中或已关闭/写权限已丢失）",
        detail,
        "如需继续编辑，请重新打开该项目；迟到的保存/提交/归档请求"
        "已被拒绝，数据未受影响");
}

/// PRJ-LOCK-HELD 记录（只读上下文写拒绝＋降级打开场景——含持有 PID）。
core::DiagnosticRecord makeLockHeldRecord(const std::string& detail)
{
    return core::DiagnosticRecord::make(
        std::string{"PRJ-LOCK-HELD"},
        std::nullopt,
        std::nullopt, std::nullopt,
        "项目当前以只读方式打开（写权限由其他实例持有或介质只读）",
        detail,
        "关闭占用该项目的其他窗口/实例后重试；只读模式下可以查看"
        "但不能编辑、提交或保存草稿");
}

/**
 * @brief 工厂级互斥（§5.1"内部互斥"）：open/createNew 的全程串行点＋
 *        进程内注册表的保护。生命周期注销（finishClose）与登记
 *        （openLocked）共用本锁——注册表一致性前提。
 */
std::mutex& factoryMutex()
{
    static std::mutex s_mutex;
    return s_mutex;
}

/**
 * @brief 进程内可写打开注册表（§9.3"同一规范路径的第二次 writable 打开
 *        直接拒绝 lock-held-by-other，防自我双写"）。
 *
 * 背景说明：OS 锁对同进程内的第二次写访问同样会共享冲突，但 §9.3 要求
 * 进程内重复打开**直接拒绝**（不经降级只读——自我双写是程序缺陷，降级
 * 只读会掩盖它）。注册表键＝规范路径（\\\\?\\ 形态，win32::
 * canonicalStorePath 产物；Windows 默认大小写不敏感语义已由规范化收敛）。
 * 调用方须持 factoryMutex。
 */
std::set<std::wstring>& writableRegistry()
{
    static std::set<std::wstring> s_open;
    return s_open;
}

/**
 * @brief 可写注册的 RAII 守卫——③装载/⑤扫描失败路径的注册表注销
 *        （打开半途失败不得泄漏注册项，否则同路径永远打不开第二次）。
 */
class WritableRegistryGuard {
public:
    explicit WritableRegistryGuard(std::wstring canonical)
        : m_canonical(std::move(canonical)), m_armed(true)
    {
    }

    ~WritableRegistryGuard()
    {
        if (m_armed) {
            writableRegistry().erase(m_canonical);
        }
    }

    /// 上下文成功构造后调用——所有权已随上下文的 finishClose/析构注销
    /// 路径接管，本守卫解除武装。
    void disarm() noexcept { m_armed = false; }

    WritableRegistryGuard(const WritableRegistryGuard&) = delete;
    WritableRegistryGuard& operator=(const WritableRegistryGuard&) = delete;

private:
    std::wstring m_canonical;
    bool m_armed;
};

// =====================================================================
// 孤儿草稿扫描（§7.4③——实现口径⑤）
// =====================================================================

/**
 * @brief 扫描 drafts/ 产出孤儿/损坏草稿清单（§8.4 处置规则的数据面）。
 *
 * 判定（保守版，登记为口径⑤）：
 *   - `<file>.new` / `<file>.bak`：保存崩溃现场/上一版残留——直接报告
 *     （§8.4".new 残留＝崩溃现场→丢弃 .new、保留 current/.bak 并报告"；
 *     现场文件不删，处置归 DraftService/ui）；
 *   - `<module>.draft.json`：读不到＝损坏；解析失败＝损坏；projectId 与
 *     本项目不符＝跨项目残留；branchId 不在权威分支表＝无对应分支孤儿；
 *     所在目录名与文档 branchId 不一致＝路径与内容不一致（§8.4）。
 *
 * @param projectDir    [in] 项目根（drafts/ 在其下）
 * @param authoritative [in] 权威元数据（分支表判定依据）
 * @param projectId     [in] 本项目身份（归属校验依据）
 * @param sink          [in] 开发诊断出口（可空）
 * @return 孤儿文件相对路径清单（相对项目根；字典序——NFR-COR-02）
 */
std::vector<std::string> scanOrphanDrafts(const std::filesystem::path& projectDir,
                                          const ProjectMetadataRecord& authoritative,
                                          const core::ProjectId& projectId,
                                          IDiagnosticsSink* sink)
{
    std::vector<std::string> orphans;
    const std::filesystem::path draftsRoot = projectDir / kDraftsDirName;
    std::error_code ec;
    if (!std::filesystem::exists(draftsRoot, ec) || ec) {
        return orphans;  // 无草稿区＝无孤儿（正常项目常态）
    }

    // 权威分支表键集（一次构建——分支不存在的草稿＝孤儿，§8.4）。
    std::set<std::string> knownBranches;
    for (const BranchRecord& branch : authoritative.branches) {
        knownBranches.insert(branch.branchId.toCanonical());
    }

    // 一层目录遍历：drafts/<branch-dir>/<file>（§4.1 命名规则；散落文件
    // 不在草稿命名规则内——跳过，开发观测面之外（同 TxEngine 扫描口径）。
    for (std::filesystem::directory_iterator branchIt(draftsRoot, ec), end;
         branchIt != end && !ec; branchIt.increment(ec)) {
        std::error_code dirEc;
        if (!branchIt->is_directory(dirEc) || dirEc) {
            continue;
        }
        const std::string branchDirName = branchIt->path().filename().string();
        for (std::filesystem::directory_iterator fileIt(branchIt->path(), ec), fEnd;
             fileIt != fEnd && !ec; fileIt.increment(ec)) {
            std::error_code fEc;
            if (!fileIt->is_regular_file(fEc) || fEc) {
                continue;
            }
            const std::string fileName = fileIt->path().filename().string();
            const std::string relPath = (std::filesystem::path(kDraftsDirName)
                                         / branchDirName / fileName)
                                            .string();

            // .new/.bak 残留：崩溃现场语义，直接报告（§8.4）。
            if (fileName.size() > 4
                && (fileName.compare(fileName.size() - 4, 4, ".new") == 0
                    || fileName.compare(fileName.size() - 4, 4, ".bak")
                           == 0)) {
                orphans.push_back(relPath);
                continue;
            }

            // 当前版草稿：四类归属校验（口径⑤）。
            const auto bytes = readAllBytes(fileIt->path());
            if (!bytes) {
                orphans.push_back(relPath);  // 读不到＝损坏现场
                continue;
            }
            DraftDocument draft;
            try {
                draft = codec::parseDraftDocument(*bytes);
            } catch (const StoreError&) {
                orphans.push_back(relPath);  // 解析失败＝损坏（§8.4）
                continue;
            }
            const bool foreignProject = !(draft.projectId == projectId);
            const bool unknownBranch
                = knownBranches.count(draft.branchId.toCanonical()) == 0;
            const bool pathMismatch
                = branchDirName != draft.branchId.toCanonical();
            if (foreignProject || unknownBranch || pathMismatch) {
                orphans.push_back(relPath);
                if (sink != nullptr) {
                    // 开发明细（定位排查用；用户面只收清单＋汇总记录）。
                    sink->reportDev("project/open",
                                    "orphan-draft（草稿归属校验未通过） "
                                        + relPath
                                        + (foreignProject ? " foreign-project"
                                                          : "")
                                        + (unknownBranch ? " unknown-branch"
                                                         : "")
                                        + (pathMismatch ? " path-mismatch"
                                                        : ""));
                }
            }
        }
    }

    // 确定性排序（NFR-COR-02：directory_iterator 顺序未指定）。
    std::sort(orphans.begin(), orphans.end());
    return orphans;
}

// =====================================================================
// 打开协议共享装载（③步：HEAD 一致性→闭包装载）——open 与 createNew
// 就位后共用的装配路径（差异只在②的版本检查时机：createNew 的
// project.json 是刚组装的当前版本，天然通过）。
// =====================================================================

/**
 * @brief 装载闭包成员入会话索引（实现口径④的两段式第二段）。
 *
 * 前置：scanForRecovery 已通过（headIntegrityVerified==true——闭包内
 * 清单/对象全部完好）；本函数跳过 uncommittedRevisions 后搬运装载，
 * 遇到失败＝扫描后竞态损坏（防御分支，如实按 store-corrupt 抛）。
 *
 * @param engine         [in] 事务引擎（HEAD 读取与项目根绑定）
 * @param objects        [in] 对象库（元数据经校验读通道读取）
 * @param index          [in] 会话索引（装载目标——闭包外不注册）
 * @param scan           [in] 恢复扫描结论（uncommittedRevisions 名单）
 * @param projectId      [in] project.json 声明的身份（跨文件一致性）
 * @param schemaVersion  [in] project.json 声明的版本（跨文件一致性）
 * @param sink           [in] 诊断出口（可空）
 * @param outHead        [out] 装载校验通过的 HEAD 内容
 * @param outAuthoritative [out] HEAD 引用的权威元数据记录
 * @param outAuthoritativeRef [out] 权威元数据注册键
 *
 * @throws StoreError StoreCorrupt（跨文件不一致——§4.1"projectId 与
 *         HEAD 不一致＝CorruptStoreDetected"；或扫描后竞态损坏）
 */
void loadCommittedState(tx::TxEngine& engine,
                        objstore::ObjectStore& objects,
                        revindex::RevisionIndex& index,
                        const tx::TxRecoveryScan& scan,
                        const core::ProjectId& projectId,
                        int schemaVersion,
                        IDiagnosticsSink* sink,
                        HeadRecord& outHead,
                        ProjectMetadataRecord& outAuthoritative,
                        ObjectRefPair& outAuthoritativeRef)
{
    // HEAD 读取（scan 已确认存在且可解析——此处失败只可能是竞态改写）。
    const HeadRecord head = engine.readHead();

    // 跨文件一致性（§4.1 project.json/HEAD 行——打开协议 PRJ-T08 的明确
    // 义务；PersistenceFormat.hpp HeadRecord 注释同口径）。
    if (!(head.projectId == projectId) || head.schemaVersion != schemaVersion
        || head.formatId != std::string{kFormatId}) {
        const std::string detail
            = "project/open: project.json 与 HEAD 跨文件不一致 projectId(json)="
              + projectId.toCanonical() + " projectId(head)="
              + head.projectId.toCanonical() + " schemaVersion(json)="
              + std::to_string(schemaVersion) + " schemaVersion(head)="
              + std::to_string(head.schemaVersion);
        if (sink != nullptr) {
            sink->report(makeOpenCorruptRecord(detail));
        }
        throw StoreError(StoreErrorCode::StoreCorrupt, detail);
    }

    // 闭包外名单（§7.3：已发布但未提交——装载跳过，"查询不可见"的
    // 结构性保证，PRJ-TX-9）。
    std::set<std::string> uncommitted;
    for (const RevisionId& rid : scan.uncommittedRevisions) {
        uncommitted.insert(rid.toCanonical());
    }

    // ---- 清单搬运装载（跳过闭包外；遇到失败＝竞态损坏，防御性抛）。
    std::set<ObjectRefPair, MetadataRefLess> metadataRefs;
    RevisionManifest headManifest;
    bool headManifestSeen = false;
    std::error_code ec;
    const std::filesystem::path revRoot
        = std::filesystem::path(engine.projectDir()) / "revisions";
    if (std::filesystem::exists(revRoot, ec) && !ec) {
        for (std::filesystem::directory_iterator it(revRoot, ec), end;
             it != end && !ec; it.increment(ec)) {
            std::error_code dirEc;
            if (!it->is_directory(dirEc) || dirEc) {
                continue;
            }
            const std::string dirName = it->path().filename().string();
            if (!core::RevisionId::tryFromCanonical(dirName).has_value()) {
                continue;  // 不合规目录名（scan 已开发观测——搬运侧跳过）
            }
            if (uncommitted.count(dirName) != 0) {
                continue;  // 闭包外＝未提交残留——不装载（PRJ-TX-9）
            }
            const auto bytes = readAllBytes(it->path() / "manifest.json");
            if (!bytes) {
                throw StoreError(StoreErrorCode::StoreCorrupt,
                                 "project/open: manifest 缺失（扫描后竞态"
                                 "损坏） path="
                                     + (it->path() / "manifest.json").string());
            }
            RevisionManifest manifest;
            try {
                manifest = codec::parseRevisionManifest(*bytes);
            } catch (const StoreError& e) {
                throw StoreError(StoreErrorCode::StoreCorrupt,
                                 "project/open: manifest 解析失败（扫描后"
                                 "竞态损坏） path="
                                     + (it->path() / "manifest.json").string()
                                     + " detail=" + std::string(e.what()));
            }
            if (manifest.revisionId == head.revisionId) {
                headManifest = manifest;  // HEAD 修订清单（权威引用来源）
                headManifestSeen = true;
            }
            index.registerRevision(manifest);
            metadataRefs.insert(manifest.metadataRef);
        }
    }
    if (!headManifestSeen) {
        // HEAD 指向的修订不在磁盘（scan 闭包行走已验证过——到达这里＝
        // 目录被并发移除的竞态）。防御性损坏。
        throw StoreError(StoreErrorCode::StoreCorrupt,
                         "project/open: HEAD 引用的修订目录缺失（扫描后"
                         "竞态损坏） revision="
                             + head.revisionId.toCanonical());
    }

    // ---- 元数据装载（经 ObjectStore 校验读通道——闭包内引用缺失/篡改
    // 在此被 store-corrupt 拒绝；§5.2 错误表"引用缺失＝store-corrupt"）。
    std::map<ObjectRefPair, ProjectMetadataRecord, MetadataRefLess>
        loaded;
    for (const ObjectRefPair& ref : metadataRefs) {
        const auto payloadBytes = objects.object(ref.objectId,
                                                ref.contentVersion);
        const std::string payload{
            reinterpret_cast<const char*>(payloadBytes.data()),
            payloadBytes.size()};
        ProjectMetadataRecord record = codec::parseMetadataRecord(payload);
        index.registerMetadata(ref, record);
        loaded.emplace(ref, std::move(record));
    }

    // 序号水位断点恢复（D-06/O-15——只升不降；会话提交的 seq 起点）。
    index.adoptHeadSeq(head.revisionSeq);

    outHead = head;
    outAuthoritativeRef = headManifest.metadataRef;
    outAuthoritative = loaded.at(outAuthoritativeRef);  // 权威底本（D-5）
}

}  // namespace

// =====================================================================
// OpenStoreResult——out-of-line 特殊成员（ProjectStore 完整类型可见处；
// 见 StoreTypes.hpp 该结构的注释）
// =====================================================================

OpenStoreResult::~OpenStoreResult() = default;
OpenStoreResult::OpenStoreResult(OpenStoreResult&& other) noexcept = default;
OpenStoreResult& OpenStoreResult::operator=(OpenStoreResult&& other) noexcept
    = default;

// =====================================================================
// ProjectStoreImpl——生命周期与门卫
// =====================================================================

ProjectStoreImpl::ProjectStoreImpl(std::unique_ptr<win32::StoreLock> lock,
                                   const LockInfo& lockView,
                                   std::wstring canonicalDir,
                                   std::unique_ptr<objstore::ObjectStore> objects,
                                   std::unique_ptr<revindex::RevisionIndex> index,
                                   std::unique_ptr<tx::TxEngine> engine,
                                   const HeadRecord& head,
                                   const ProjectMetadataRecord& authoritative,
                                   const ObjectRefPair& authoritativeRef,
                                   core::IDomainEventBus* eventBus,
                                   IDiagnosticsSink* sink)
    : m_lock(std::move(lock))
    , m_lockView(lockView)
    , m_canonicalDir(std::move(canonicalDir))
    , m_canonicalDirFs(m_canonicalDir)
    , m_objects(std::move(objects))
    , m_index(std::move(index))
    , m_engine(std::move(engine))
    , m_head(head)
    , m_authoritative(authoritative)
    , m_authoritativeRef(authoritativeRef)
    , m_eventBus(eventBus)
    , m_sink(sink)
{
    // 装配纪律（实现口径⑧）：m_index 接管的对象必须正是 engine 内部
    // 持有的那个（指针共享而非拷贝——否则引擎提交注册的修订对本类
    // 不可见）。空部件＝工厂装配违约，fail-fast。
    if (m_objects == nullptr || m_index == nullptr || m_engine == nullptr) {
        throw std::invalid_argument(
            "project/store: 装配违约——objects/index/engine 不得为空");
    }
}

bool ProjectStoreImpl::writable() const noexcept
{
    // 唯一依据＝本实例持有的 OS 排他句柄（SA-17）：无锁对象＝只读上下文；
    // 有锁对象以其 held()（Held 且未失权）为准——心跳/PID 不参与判定
    // （卡顿不接管——D-03 的结构性落实）。
    if (m_lock == nullptr) {
        return false;
    }
    return m_lock->held();
}

LockInfo ProjectStoreImpl::lockInfo() const
{
    LockInfo info = m_lockView;
    if (m_lock != nullptr) {
        // 持有期以锁对象的最后观测记录为准（自我身份——心跳字段由心跳
        // 线程实时重写；被拒场景＝他方记录）。
        info.holder = m_lock->lastHolder();
    }
    info.isSelf = writable();
    return info;
}

core::ProjectId ProjectStoreImpl::projectId() const noexcept
{
    return m_head.projectId;  // 打开③跨文件一致性已保证三处同源
}

SchemaInfo ProjectStoreImpl::schema() const
{
    return SchemaInfo{m_head.schemaVersion, m_head.formatId};
}

std::filesystem::path ProjectStoreImpl::canonicalPath() const
{
    return m_canonicalDirFs;
}

bool ProjectStoreImpl::closed() const noexcept
{
    const std::lock_guard<std::mutex> guard(m_lifecycleMutex);
    return m_state == StoreLifecycleState::Closed;
}

std::uint32_t ProjectStoreImpl::requestClose()
{
    {
        std::lock_guard<std::mutex> guard(m_lifecycleMutex);
        // 幂等：Closed 后仅返回 0（§5.1 表）；Draining 重复请求返回当前
        // 在途数，不重复收尾。
        if (m_state == StoreLifecycleState::Closed) {
            return 0;
        }
        m_state = StoreLifecycleState::Draining;  // 新写自此拒绝（§9.6①）
        if (m_pending > 0) {
            return m_pending;  // 排空等待：在途归零由票据释放路径收尾
        }
    }
    finishClose(true);  // pending==0：同步走完收尾（锁外副作用＋回调）
    return 0;
}

void ProjectStoreImpl::subscribeClose(ICloseObserver& observer)
{
    bool invokeNow = false;
    {
        std::lock_guard<std::mutex> guard(m_lifecycleMutex);
        if (m_state == StoreLifecycleState::Closed) {
            invokeNow = true;  // 关闭事实已发生——立即补发（一次性语义）
        } else if (std::find(m_observers.begin(), m_observers.end(), &observer)
                   == m_observers.end()) {
            m_observers.push_back(&observer);  // 指针去重——幂等订阅
        }
    }
    if (invokeNow) {
        observer.onStoreClosed(*this);  // 锁外回调（回调内禁重入——头注）
    }
}

std::shared_ptr<void> ProjectStoreImpl::acquireInFlight()
{
    std::lock_guard<std::mutex> guard(m_lifecycleMutex);
    if (m_state == StoreLifecycleState::Closed) {
        return {};  // Closed 后不再接受新在途（§9.6——迟到持有者走拒绝）
    }
    ++m_pending;
    // RAII 票据：析构即释放引用；Draining 且归零时在此线程完成收尾
    // （§9.7——排空完成的触发点之一；另一触发点＝requestClose 的
    // pending==0 同步路径）。票据生存期必须短于上下文（口径⑦）。
    return std::shared_ptr<void>(reinterpret_cast<void*>(1), [this](void*) {
        bool finish = false;
        {
            std::lock_guard<std::mutex> g(m_lifecycleMutex);
            if (m_pending > 0) {
                --m_pending;
            }
            finish = m_state == StoreLifecycleState::Draining && m_pending == 0;
        }
        if (finish) {
            finishClose(true);
        }
    });
}

void ProjectStoreImpl::finishClose(bool invokeObservers)
{
    // 第一段（锁下）：置 Closed（写入口自此拒绝——关闭路径同步置位，
    // "句柄悬空"态结构性不存在，§9.6①）。
    std::vector<ICloseObserver*> toNotify;
    {
        std::lock_guard<std::mutex> guard(m_lifecycleMutex);
        if (m_state == StoreLifecycleState::Closed) {
            return;  // 幂等（并发收尾只走一次）
        }
        m_state = StoreLifecycleState::Closed;
        m_observers.swap(toNotify);
    }

    // 第二段（锁外副作用）：释放锁句柄（OS 层写权限即时失效——§9.1；
    // 心跳线程 join 于 release 内部）＋注销进程内注册表（注册表互斥＝
    // factory 串行锁）。
    if (m_lock != nullptr) {
        m_lock->release();
    }
    {
        const std::lock_guard<std::mutex> factoryGuard(factoryMutex());
        writableRegistry().erase(m_canonicalDir);
    }

    // 第三段：一次性回调（仅 requestClose 完成路径；析构静默终局——
    // 口径⑦）。回调内禁重入本上下文（头注契约）。
    if (invokeObservers) {
        for (ICloseObserver* observer : toNotify) {
            observer->onStoreClosed(*this);
        }
    }
}

ProjectStoreImpl::~ProjectStoreImpl()
{
    // 静默终局：Active/Draining 析构＝放弃等待——释放锁＋注销注册表，
    // 不发回调（口径⑦）；已 Closed 则收尾早已完成。
    finishClose(false);
}

tx::CommitResult ProjectStoreImpl::executeCommit(const tx::CommitPlan& plan)
{
    // ---- 门卫第①道：上下文状态机（§9.6①）。Draining/Closed 一律拒绝
    // （context-closing 并入 ContextClosed 语义——§5.1 表）；诊断
    // PRJ-WRITE-AUTHORITY-LOST（diagnostics.md 映射行"context-closed →
    // PRJ-WRITE-AUTHORITY-LOST"——PRJ-TX-9 的稳定诊断断言点）。
    {
        std::lock_guard<std::mutex> guard(m_lifecycleMutex);
        if (m_state != StoreLifecycleState::Active) {
            const char* stateName
                = m_state == StoreLifecycleState::Draining ? "draining"
                                                           : "closed";
            if (m_sink != nullptr) {
                m_sink->report(makeWriteAuthorityLostRecord(
                    "project/store: 写拒绝（上下文非 Active） state="
                    + std::string{stateName} + " branch="
                    + plan.branchId.toCanonical()));
            }
            throw StoreError(StoreErrorCode::ContextClosed,
                             "project/store: context not writable state="
                                 + std::string{stateName});
        }
    }

    // ---- 门卫第①道（锁半边）：从未持锁＝只读上下文（PM-07 禁编辑的
    // 存储侧落实——§8.6）。诊断 PRJ-LOCK-HELD（§9.5 锁竞争行）。
    if (m_lock == nullptr) {
        if (m_sink != nullptr) {
            m_sink->report(makeLockHeldRecord(
                "project/store: 写拒绝（只读上下文——写权限由其他实例"
                "持有）"));
        }
        throw StoreError(StoreErrorCode::LockHeldByOther,
                         "project/store: readonly context（未持有写锁）");
    }

    // ---- 门卫第②道：StoreLock 权威探测（§9.6①②——状态＋句柄有效性；
    // 已释放/已失权的诊断 PRJ-WRITE-AUTHORITY-LOST 由 StoreLock 产出）。
    if (!m_lock->requireWriteAuthority()) {
        throw StoreError(StoreErrorCode::WriteRejected,
                         "project/store: write authority check failed"
                         "（句柄已释放或已失权）");
    }

    // ---- writer 互斥段（§9.8：全部变更性文件操作的串行化点）。事务
    // 本体与提交后的权威切换在同一临界区内完成——并发读者看到的是
    // "HEAD 切换前"或"权威已前进"两个稳态之一。
    const std::lock_guard<std::mutex> writerGuard(m_writerMutex);
    const tx::CommitResult result
        = m_engine->commit(plan, m_head, m_authoritative, m_authoritativeRef);

    // 提交成功：会话权威前进——HEAD 取切换后字节事实（newHead）；权威
    // 元数据自对象库读回（地面事实＋顺带走发布完整性校验通道，不靠
    // 内存推导）。读回失败＝已提交但读不回（StoreCorrupt 硬错误——
    // 与 TxEngine 第 5 步自校验同口径的 D-18 不回滚路径，如实上抛）。
    m_head = result.newHead;
    const auto metaBytes = m_objects->object(result.metadataRef.objectId,
                                             result.metadataRef.contentVersion);
    m_authoritative = codec::parseMetadataRecord(std::string{
        reinterpret_cast<const char*>(metaBytes.data()), metaBytes.size()});
    m_authoritativeRef = result.metadataRef;
    return result;
}

// =====================================================================
// ProjectStoreFactory——打开与新建（openLocked＝打开协议主体；与公开
// 入口同层级——它需要访问 ProjectStoreImpl 的私有构造，friend 关系
// 登记于 ProjectStoreImpl.hpp）
// =====================================================================

/**
 * @brief 打开协议主体（前置约定：调用方已持 factoryMutex——open 的
 *        公开入口与 createNew 的装载段共用；拆分原因：std::mutex 非递归，
 *        createNew 在持锁状态下复用打开路径，公开 open 的加锁入口不能
 *        被再次进入，否则自死锁）。
 *
 * 步骤语义与公开 open 一致（①兜底校验→②版本检查→锁半步→③校验装载
 * →⑤孤儿草稿扫描→激活）；参数与错误契约见 ProjectStoreFactory::open。
 */
OpenStoreResult openLocked(const OpenStoreRequest& request)
{
    // ---- ① 兜底路径校验＋规范化（§8.7①兜底口径；§9.3 实例身份）。
    std::error_code ec;
    if (request.path.empty() || !std::filesystem::exists(request.path, ec)
        || ec) {
        throw StoreError(StoreErrorCode::NotAProject,
                         "project/open: 路径不存在或不可达 path="
                             + request.path.string());
    }
    const win32::PathCanonicalResult canon
        = win32::canonicalStorePath(request.path.wstring());
    if (!canon.ok) {
        // 规范化失败（属性句柄打开被拒）：权限类拒绝；其余按"不可达＝
        // 非项目目录"处置（§9.5 前两行的打开期映射）。
        if (canon.osError == ERROR_ACCESS_DENIED) {
            throw StoreError(StoreErrorCode::AccessDenied,
                             "project/open: 路径规范化被拒 osError="
                                 + std::to_string(canon.osError));
        }
        throw StoreError(StoreErrorCode::NotAProject,
                         "project/open: 路径规范化失败 osError="
                             + std::to_string(canon.osError));
    }
    const std::filesystem::path projectDir(canon.canonical);

    // 打开期统一诊断出口（收集型包装——RecoveryReport 快照与 sink 上报
    // 同源同序；见 CollectingSink 头注）。工厂串行由调用方负责
    // （openLocked 的前置约定——公开 open/createNew 均已持锁）。
    CollectingSink collector(request.diagnostics);

    // ---- ② 目录形态与版本检查（PM-02②/PM-06）：project.json 缺失＝
    // 非项目目录（§4.1 行）；版本判定由 Codec（FormatLegacy/
    // SchemaFuture——升级指引数据随 detail 三键，§8.11）。本步失败不
    // 触碰锁（实现口径①）。
    const auto identityBytes = readAllBytes(projectDir / "project.json");
    if (!identityBytes) {
        throw StoreError(StoreErrorCode::NotAProject,
                         "project/open: project.json 缺失（非项目目录） path="
                             + projectDir.string());
    }
    ProjectStaticIdentity identity;
    try {
        identity = codec::parseStaticIdentity(*identityBytes);
    } catch (const StoreError& e) {
        // 稳定只读拒绝＋诊断码（PM-06）。原文件不动——打开路径零写入。
        if (e.code() == StoreErrorCode::FormatLegacy) {
            collector.report(makeFormatLegacyRecord(e.what()));
        } else if (e.code() == StoreErrorCode::SchemaFuture) {
            collector.report(makeSchemaFutureRecord(e.what()));
        } else {
            collector.report(makeOpenCorruptRecord("project/open: "
                                                   "project.json 解析拒绝 "
                                                   + std::string(e.what())));
        }
        throw;  // 稳定码随异常上抛（FormatLegacy/SchemaFuture/StoreCorrupt）
    }

    // ---- 锁半步（§9.1～§9.3；实现口径②）。Writable 进程内重复打开
    // 直接拒绝（防自我双写——§9.3）；OS 层竞争裁决交 StoreLock。
    std::unique_ptr<win32::StoreLock> lock;
    LockInfo lockView;
    if (request.mode == OpenMode::Writable) {
        if (writableRegistry().count(canon.canonical) != 0) {
            throw StoreError(StoreErrorCode::LockHeldByOther,
                             "project/open: 进程内重复可写打开（防自我双写"
                             "——§9.3） path=" + projectDir.string());
        }
        win32::LockSelfRecord self;
        self.pid = currentProcessId();
        self.host = currentHostName();
        self.initialHeartbeatUtc = win32::utcNowIsoMilli();
        // 构造即获取（RAII——D-02 内核原子裁决）；被拒不是异常而是业务
        // 分支（PM-07 降级只读＋PRJ-LOCK-HELD 由 StoreLock 内产出——
        // 其 sink 参数给 collector，诊断同时进报告快照）。
        auto acquired = std::make_unique<win32::StoreLock>(
            &sharedLockOps(), &collector, (projectDir / "lock").wstring(),
            self);
        if (acquired->status() == win32::AcquireStatus::HeldByOther) {
            // 降级只读（不阻塞等待）；lockView 保留他方持有者（PM-07）。
            lockView.holder = acquired->lastHolder();
            lockView.isSelf = false;
        } else if (acquired->status() == win32::AcquireStatus::OsError) {
            if (acquired->acquireOsError() == ERROR_WRITE_PROTECT) {
                // 介质只读：Writable 降级只读可继续（§9.5 行 1"PM-07
                // writable=false 同路径"；诊断 PRJ-LOCK-HELD kind=
                // media-read-only——diagnostics 映射表行）。
                lockView.holder = acquired->lastHolder();
                lockView.isSelf = false;
                collector.report(makeLockHeldRecord(
                    "project/open: 介质只读，已降级只读打开 kind="
                    "media-read-only osError="
                        + std::to_string(acquired->acquireOsError())));
                acquired.reset();  // 未持有——RAII 析构无副作用
            } else {
                // 权限不足/其他：打开失败（§9.5 行 2——access-denied 含
                // 原始码 detail）。
                throw StoreError(mapLockOsError(acquired->acquireOsError()),
                                 "project/open: 锁获取失败（§9.5） osError="
                                     + std::to_string(
                                         acquired->acquireOsError()));
            }
        }
        if (acquired != nullptr
            && acquired->status() == win32::AcquireStatus::Held) {
            lockView.holder = acquired->lastHolder();
            lockView.isSelf = true;
            lock = std::move(acquired);
            writableRegistry().insert(canon.canonical);
        }
    } else {
        // 显式只读：不取锁（§9.2②——读持有者记录供提示，读句柄随即
        // 关闭；记录不存在＝无持有者信息，零值容忍）。
        const win32::LockRecordRead holder
            = win32::readHolderRecord(&sharedLockOps(),
                                      (projectDir / "lock").wstring());
        lockView.holder = holder.record;
        lockView.isSelf = false;
    }

    // 注册表 RAII 守卫：③/⑤失败路径注销可写登记（防注册项泄漏——
    // 泄漏会让同路径永远打不开第二次；成功路径 disarm 后由上下文的
    // finishClose/析构接管注销）。
    WritableRegistryGuard registryGuard(lock != nullptr ? canon.canonical
                                                        : std::wstring{});

    // ---- ③ 加载与读校验（恢复扫描→一致性→闭包装载）。两段式装载见
    // 实现口径④；扫描④失败＝store-corrupt（定位到文件——PM-02"失败
    // 显示具体文件"；诊断已由扫描经 collector 产出）。
    auto objects = std::make_unique<objstore::ObjectStore>(
        projectDir / "objects", projectDir / ".staging",
        request.cacheBudget.budgetBytes != 0
            ? request.cacheBudget.budgetBytes
            : objstore::ObjectStore::kDefaultCacheBudgetBytes,
        &collector);
    auto index = std::make_unique<revindex::RevisionIndex>();
    auto engine = std::make_unique<tx::TxEngine>(&sharedFileOps(), projectDir,
                                             objects.get(), index.get(),
                                             request.eventBus, &collector);

    const tx::TxRecoveryScan scan = engine->scanForRecovery();
    if (!scan.headIntegrityVerified) {
        // ④失败：打开中止（激活前失败——构造中的部件随 unique_ptr 析构，
        // 锁随 RAII 释放＋注册表随守卫注销；当前项目不受影响——§8.7）。
        throw StoreError(StoreErrorCode::StoreCorrupt,
                         "project/open: HEAD 引用闭包完整性校验失败（定位"
                         "细节见恢复诊断） path=" + projectDir.string());
    }

    HeadRecord head;
    ProjectMetadataRecord authoritative;
    ObjectRefPair authoritativeRef;
    loadCommittedState(*engine, *objects, *index, scan, identity.projectId,
                       identity.schemaVersion, &collector, head, authoritative,
                       authoritativeRef);

    // ---- ⑤ 孤儿草稿扫描（§7.4③）＋用户级汇总诊断（报告快照同步收集）。
    const std::vector<std::string> orphans
        = scanOrphanDrafts(projectDir, authoritative, identity.projectId,
                           &collector);
    if (!orphans.empty()) {
        collector.report(makeOrphanDraftRecord(orphans.size()));
    }

    // ---- 激活（完整构造成功后才交付——§8.7"激活前失败不影响当前
    // 项目"；装配序实现口径⑧）。
    OpenStoreResult result;
    std::unique_ptr<ProjectStoreImpl> impl(new ProjectStoreImpl(
        std::move(lock), lockView, canon.canonical, std::move(objects),
        std::move(index), std::move(engine), head, authoritative,
        authoritativeRef, request.eventBus, request.diagnostics));
    registryGuard.disarm();  // 注册表注销责任移交 impl 的关闭路径
    result.store = std::move(impl);
    result.writable = result.store->writable();
    result.lockInfo = result.store->lockInfo();
    result.recovery.headIntegrityVerified = scan.headIntegrityVerified;
    result.recovery.ignoredStagingTxs = scan.ignoredStagingTxs;
    result.recovery.orphanDraftFiles = orphans;
    result.recovery.danglingObjectCount = scan.danglingObjectCount;
    result.recovery.diagnostics = collector.seen();
    return result;
}

OpenStoreResult ProjectStoreFactory::open(const OpenStoreRequest& request)
{
    // 工厂全程串行（§5.1 表"线程安全（内部互斥）"；进程内注册表一致性
    // 前提）；主体逻辑在 openLocked（假定持锁——createNew 的装载段以
    // 同一约定复用）。
    const std::lock_guard<std::mutex> factoryGuard(factoryMutex());
    return openLocked(request);
}

OpenStoreResult ProjectStoreFactory::createNew(
    const std::filesystem::path& dir,
    std::string_view displayName,
    core::IDomainEventBus* eventBus,
    IDiagnosticsSink* diagnostics,
    ObjectCacheBudget cacheBudget)
{
    // 前置契约（§5.1 表"目标目录不存在或为空"）：向导应先行校验，违反
    // ＝调用方错误 fail-fast（不产出稳定码——错误二分）。
    if (displayName.empty()) {
        throw std::invalid_argument("project/createNew: displayName 为空");
    }

    const std::lock_guard<std::mutex> factoryGuard(factoryMutex());

    std::error_code ec;
    const bool preExisted = std::filesystem::exists(dir, ec);
    if (preExisted && !std::filesystem::is_directory(dir, ec)) {
        throw std::invalid_argument("project/createNew: 目标已存在且不是目录"
                                    " path="
                                    + dir.string());
    }
    if (preExisted && !std::filesystem::is_empty(dir, ec)) {
        throw std::invalid_argument("project/createNew: 目标目录非空 path="
                                    + dir.string());
    }
    // 创建目标目录（空目录形态——失败清理按 preExisted 区分：预存目录
    // 恢复为空，自建目录整树删除——§5.1"失败清理目标目录"）。
    std::filesystem::create_directories(dir, ec);
    if (!std::filesystem::is_directory(dir, ec)) {
        throw StoreError(StoreErrorCode::AccessDenied,
                         "project/createNew: 目标目录创建失败 path="
                             + dir.string() + " detail=" + ec.message());
    }
    const win32::PathCanonicalResult canon
        = win32::canonicalStorePath(dir.wstring());
    if (!canon.ok) {
        throw StoreError(StoreErrorCode::AccessDenied,
                         "project/createNew: 路径规范化失败 osError="
                             + std::to_string(canon.osError));
    }
    if (writableRegistry().count(canon.canonical) != 0) {
        throw StoreError(StoreErrorCode::LockHeldByOther,
                         "project/createNew: 进程内已打开同路径（§9.3）");
    }
    const std::filesystem::path projectDir(canon.canonical);

    // 失败清理（PM-01"取消/失败不留半成品"）：组装区整树删除＋目标
    // 目录恢复原状（非预存＝删除；预存空目录＝清空我们写入的条目）。
    auto cleanupTarget = [&] {
        std::error_code rmEc;
        if (!preExisted) {
            std::filesystem::remove_all(projectDir, rmEc);
        } else {
            for (std::filesystem::directory_iterator it(projectDir, rmEc), end;
                 it != end && !rmEc; it.increment(rmEc)) {
                std::filesystem::remove_all(it->path(), rmEc);
            }
        }
    };

    // ---- 同卷组装区（实现口径③）：目标父目录下"<目标名>.new-<pid>-
    // <seq>"。序号计数器为进程内静态（组装区存活期＝本函数栈；同名
    // 并发创建已由工厂互斥排除）。
    static std::atomic<std::uint64_t> s_assemblySeq{0};
    std::string baseName = dir.filename().string();
    if (baseName.empty()) {
        baseName = "project";  // 根路径/尾分隔符等退化形态的兜底名
    }
    std::ostringstream assemblyName;
    assemblyName << baseName << ".new-" << currentProcessId() << "-"
                 << s_assemblySeq.fetch_add(1);
    const std::filesystem::path assembly
        = dir.parent_path() / assemblyName.str();
    std::filesystem::create_directories(assembly, ec);
    if (ec) {
        throw StoreError(StoreErrorCode::WriteRejected,
                         "project/createNew: 组装区创建失败 path="
                             + assembly.string());
    }

    try {
        // ---- 组装（全部写入组装区；对象发布经 ObjectStore 校验通道
        // ——TxEngine.hpp 头注："publishObject 保留给非事务发布路径
        // （如项目创建组装）"；暂存目录＝组装区 .staging 同卷）。
        objstore::ObjectStore assemblyObjects(
            assembly / "objects", assembly / ".staging",
            cacheBudget.budgetBytes != 0
                ? cacheBudget.budgetBytes
                : objstore::ObjectStore::kDefaultCacheBudgetBytes,
            diagnostics);

        // 身份分配（core §4.1——身份分配落地点＝project 创建路径；
        // P-PR-1 消费基线：core 公共 API generate()，零本地第二格式化）。
        const core::ProjectId projectId = core::ProjectId::generate();
        const RevisionId r0 = RevisionId::generate();
        const BranchId branchId = BranchId::generate();
        const ObjectId metaOid = ObjectId::generate();
        const std::string nowUtc = win32::utcNowIsoMilli();

        // 初始权威元数据 M0（§4.5.1 步骤 0：主分支 main，base=tip=r0；
        // label 一次写入——P-PR-8）。
        ProjectMetadataRecord m0;
        m0.schemaVersion = kSchemaVersionCurrent;
        m0.committedBy = r0;
        m0.projectDisplayName = std::string(displayName);
        m0.primaryBranchId = branchId;
        BranchRecord mainBranch;
        mainBranch.branchId = branchId;
        mainBranch.label = kPrimaryBranchLabel;
        mainBranch.baseRevisionId = r0;
        mainBranch.tipRevisionId = r0;
        mainBranch.createdAtUtc = nowUtc;
        m0.branches.push_back(mainBranch);

        // 元数据对象发布（内容寻址——cv 由发布返回）。
        const std::string metaPayload = codec::dump(m0);
        const core::ContentVersion metaCv
            = assemblyObjects.publishObject(metaOid, metaPayload);
        const ObjectRefPair m0Ref{metaOid, metaCv};

        // 初始修订 r0 清单（§4.4.2：引用集＝[元数据对象]；seq=1——
        // 首修订；无 parent）。
        RevisionManifest r0Manifest;
        r0Manifest.revisionId = r0;
        r0Manifest.revisionSeq = 1;
        r0Manifest.branchId = branchId;
        r0Manifest.committedAtUtc = nowUtc;
        r0Manifest.metadataRef = m0Ref;
        ObjectRef metaRef;
        metaRef.objectId = metaOid;
        metaRef.contentVersion = metaCv;
        metaRef.objectTypeToken = tx::kMetadataTypeToken;
        metaRef.digest256 = metaCv.toCanonical().substr(3);  // 剥 cv- tag
        r0Manifest.objectRefs.push_back(metaRef);
        r0Manifest.introducedObjects.push_back(metaCv);

        // 初始命令留痕（§4.4.4；project-init＝新建存储侧的占位留痕——
        // 元数据零增量，随 r0 持久化供历史浏览）。
        CommandRecord initCommand;
        initCommand.commandType = "project-init";
        initCommand.payloadFormatVersion = 1;
        initCommand.payloadCanonical = "{}";
        initCommand.summary = "createNew: 初始修订（M0/main）";
        const std::string manifestBytes = codec::dump(r0Manifest);
        const std::string manifestDigest
            = codec::contentVersionOf(manifestBytes).toCanonical().substr(3);

        // 静态标识 project.json（§4.2：创建期一次写入，不参与事务）。
        ProjectStaticIdentity staticIdentity;
        staticIdentity.formatId = kFormatId;
        staticIdentity.schemaVersion = kSchemaVersionCurrent;
        staticIdentity.projectId = projectId;
        staticIdentity.createdAtUtc = nowUtc;
        staticIdentity.createdWithToolVersion = kCreatedWithToolVersion;

        // HEAD（§4.4.1；manifestDigest＝打开后自校验键）。
        HeadRecord head;
        head.formatId = kFormatId;
        head.schemaVersion = kSchemaVersionCurrent;
        head.projectId = projectId;
        head.revisionId = r0;
        head.revisionSeq = 1;
        head.branchId = branchId;
        head.manifestDigest = manifestDigest;

        // 磁盘就位（组装区内；win32 原语——write-through 持久性闸门，
        // §7.2 行 1；发布原语保留给 rename 步）。
        std::filesystem::create_directories(
            assembly / "revisions" / r0.toCanonical(), ec);
        std::filesystem::create_directories(assembly / ".staging", ec);
        win32::AtomicFile fileOps;
        auto writeOrThrow = [&](const std::filesystem::path& file,
                                const std::string& bytes) {
            const win32::FileResult r
                = fileOps.writeThrough(file.wstring(), bytes.data(),
                                       bytes.size());
            if (!r.ok) {
                throw StoreError(StoreErrorCode::WriteRejected,
                                 "project/createNew: 组装写入失败 path="
                                     + file.string() + " osError="
                                     + std::to_string(r.osError));
            }
        };
        writeOrThrow(assembly / "revisions" / r0.toCanonical() / "manifest.json",
                     manifestBytes);
        writeOrThrow(assembly / "revisions" / r0.toCanonical() / "command.json",
                     codec::dump(initCommand));
        writeOrThrow(assembly / "project.json", codec::dump(staticIdentity));
        writeOrThrow(assembly / "HEAD", codec::dump(head));

        // ---- 整体就位（§5.1 表"先在 .staging 组装再整体就位"：逐顶层
        // 条目同卷 rename——每条目原子可见；全部成功后删组装区空壳）。
        const wchar_t* topEntries[] = {L"objects", L"revisions", L".staging",
                                       L"project.json", L"HEAD"};
        for (const wchar_t* name : topEntries) {
            const win32::FileResult r = fileOps.publishNew(
                (assembly / name).wstring(), (projectDir / name).wstring());
            if (!r.ok) {
                throw StoreError(StoreErrorCode::WriteRejected,
                                 "project/createNew: 就位失败 entry="
                                     + std::filesystem::path(name).string()
                                     + " osError="
                                     + std::to_string(r.osError));
            }
        }
        std::filesystem::remove_all(assembly, ec);  // 空壳清理（尽力）
    } catch (...) {
        // 组装/就位失败：清理组装区与目标已就位内容——不留半成品
        // （§5.1 表"失败清理目标目录"；PM-01）。
        std::error_code rmEc;
        std::filesystem::remove_all(assembly, rmEc);
        cleanupTarget();
        throw;
    }

    // ---- 按 open 协议装载激活（创建者即首个写权限持有者；组装内容
    // 刚经自身校验通道写入——②③⑤照走一遍，零捷径；此步失败同样按
    // "不留半成品"清理目标后如实上抛。openLocked＝持锁复用——本函数
    // 全程已持 factoryMutex，公开 open 的二次加锁入口不可进入）。
    try {
        OpenStoreRequest request;
        request.path = projectDir;
        request.mode = OpenMode::Writable;
        request.eventBus = eventBus;
        request.diagnostics = diagnostics;
        request.cacheBudget = cacheBudget;
        return openLocked(request);
    } catch (...) {
        cleanupTarget();
        throw;
    }
}

}  // namespace sdurws::ird::project
