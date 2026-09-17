/**
 * @file   Package.hpp
 * @brief  .rwpack 包导入导出的 io 侧设施（IPackageImporter/
 *         IPackageExporter）＋包格式契约常量（PackFormat）＋快照源注入
 *         接口（ISnapshotFileSource）。
 *
 * 设计依据：
 *   - units/io.md §7.1（包格式契约：rwpack.json/manifest.json/payload/
 *     镜像布局、totalDigest、条目约束）、§7.2（导出协议：一致数据视图→
 *     逐文件暂存→manifest/rwpack canonical 写出→压缩→完整性自检→原子
 *     替换）、§7.3（导入九步协议与事务状态图）、§7.4（导入威胁处置矩阵
 *     ——本文件是矩阵中"检测点③④⑤⑥"的执行体）、§7.5/§7.6（临时区与
 *     清理状态图）、§7.7（与 project 的责任切分：io ①~⑦＋⑨，project
 *     ⑥领域校验＋⑧发布——**io 侧 rename 发布被明确禁止**，N-7）、
 *     §7.8（目录包文件层校验——P-IO-7：字段字典/文件名 schema 归
 *     selection 注册，本文件不预写）、§9.9（IPackageImporter/
 *     IPackageExporter/ISnapshotFileSource 接口契约）、§11.2（V12~V14/
 *     V18~V23/V29/V31/V32 的执行载体）
 *   - 需求 PM-05（.rwpack 导入导出 io 侧实现责任方）、NFR-SEC-01/02
 *     （包内路径校验/展开预算——SA-14 统一入口防护）、SEL-02（文件层
 *     支撑——V29 引用完整性；字段字典/单位/唯一性归 selection）、
 *     AT-20（包导出/导入主线、取消不留半成品）、UX-03（取消是状态非
 *     错误——取消不产诊断）
 *   - 任务契约 tasks/foundation/IO-T06.json（≙WP-11-T05/T07）acceptance
 *     1~5 全条
 *
 * 背景说明（为什么 io 只有"已验证临时目录"没有"发布"）：PM-05 的导入
 * 承诺是"失败不留目标目录"——其结构性保证＝**发布（rename 到目标）只
 * 在全量校验之后由 project 执行**（§7.3 步骤⑧、§7.7 责任切分）。io 的
 * verifyThrough 通过后，已验证树躺在同卷临时区（payload/＝逐字节镜像
 * 的 .rwdesign 布局），session 交还调用方；project 做目标占用二次预检→
 * rename→按打开协议进入，然后调用 cleanup 清理临时区外殻。本头**不提
 * 供任何 rename/发布入口**（N-7 不拥有——acceptance 4 的结构性约束，
 * 类型层面无此 API 可误用）；同样**不读 .staging**（§2.1 N-7——包内容
 * 只来自 ISnapshotFileSource 快照与包本体）。
 *
 * 注入式协作（P-IO-1/O-09 处置——acceptance 5）：快照源
 * ISnapshotFileSource 由 io 定义接口、project 实现并注入（§7.2 图；
 * ARCH §3.5 未登记 io→project 编译边——本头零 project/runtime 类型与
 * 头依赖，纯 io 自有类型；P-PR-5/O-09 裁决申请已登记待架构所有者，
 * 注入形态与裁决结果无关）。测试以 project fake 注入（V18/V32）。
 *
 * 线程约束（§9.9 契约表）：ImportSession 会话单线程（begin/verifyThrough/
 * cleanup 同一驱动线程）；不同会话可并行（各自临时区）；exporter 无状
 * 态——单次 export_ 调用单线程，多导出并行（不同目标）。io 不创建线程
 * （§9.13——长操作由调用方线程驱动，V31 的"后台任务"即调用方线程）。
 *
 * 确定性（§9.9/IO-D11）：同包同判定（哈希全量复算）；导出 zip 条目时
 * 间戳统一取 createdAtUtc（可复现导出）；manifest 条目按 path 字典序；
 * canonical JSON 写出（§5.9.3）。
 */

#ifndef SDURWS_IRD_IO_PACKAGE_HPP
#define SDURWS_IRD_IO_PACKAGE_HPP

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <vector>

#include <sdurws/ird/core/Digest.hpp>   // core::Digest256——manifest 摘要（SA-12）
#include <sdurws/ird/io/AtomicFile.hpp>   // IAtomicFileWriter/ReplacePolicy（§4.6）
#include <sdurws/ird/io/Budget.hpp>       // BudgetSpec/IBudgetGuard（§4.5）
#include <sdurws/ird/io/IoFwd.hpp>        // IoString/IoResult/IoCancelToken/IoProgress
#include <sdurws/ird/io/IoError.hpp>      // IoError——错误轨道
#include <sdurws/ird/io/SafePath.hpp>     // ISafePathResolverPtr——③条目名预检注入
#include <sdurws/ird/io/TempArea.hpp>     // ITempAreaManager/TempAreaSession（§7.5）
#include <sdurws/ird/io/ZipChannel.hpp>   // ZipEntryVerifyReport——条目级校验结论

namespace sdurws::ird::io {

// =====================================================================
// PackFormat（§7.1 包格式契约——常量单点，导出写入与导入校验共用）
// =====================================================================

/**
 * @brief 包格式契约常量（§7.1 原文逐字——变更即格式语义变更，走单元卡
 *        增量修订；导入按此判定、导出按此写出，单一事实来源防漂移）。
 *
 * 镜像布局（§7.1"不是第二权威格式"行）：包内树逐字节镜像 .rwdesign，
 * 恒不含 lock/.staging/任何"当前状态"语义；objects/、revisions/、HEAD、
 * project.json 恒在（payload/ 前缀形态）。
 */
namespace PackFormat {
    /// rwpack.json 的 formatId 字段值（§7.1 格式标识）。
    inline constexpr char kFormatId[] = "rwpack";
    /// 包 schema 版本（§7.1 schemaVersion:1；>当前＝IO-FORMAT-VERSION-
    /// FUTURE 稳定拒绝，<当前且未注册升级步距＝IO-FORMAT-VERSION-LEGACY）。
    inline constexpr std::uint64_t kSchemaVersion = 1;
    /// manifest.json 的 schemaVersion 字段值（§7.1 manifest 契约行）。
    inline constexpr char kManifestSchemaVersion[] = "ird-pack-manifest/1";
    /// 包内两个 JSON 条目名（包根——payload/ 之外仅此两件）。
    inline constexpr char kRwpackEntryName[] = "rwpack.json";
    inline constexpr char kManifestEntryName[] = "manifest.json";
    /// payload 树前缀（manifest 条目 path 恒以此开头——§7.1 entries 行）。
    inline constexpr char kPayloadPrefix[] = "payload/";
    /// 镜像必备条目（§7.1"objects/、revisions/、HEAD、project.json 恒在"
    /// 的文件形态；revisions/ 前缀至少一条修订文件——HEAD 指向的对象）。
    inline constexpr char kRequiredHead[] = "payload/HEAD";
    inline constexpr char kRequiredProjectJson[] = "payload/project.json";
    inline constexpr char kRevisionsPrefix[] = "payload/revisions/";
} // namespace PackFormat

// =====================================================================
// ISnapshotFileSource（§7.2/§9.9——io 定义、project 实现注入的一致视图）
// =====================================================================

/**
 * @brief 快照清单条目（§9.9 enumerate 产物：path＋size——一致数据视图
 *        的最小事实；§7.2"objects/revisions 不可变 → 枚举自当前 HEAD
 *        闭包（不扫描目录！）"由实现方 project 保证，io 不复核其来源）。
 */
struct PackFileEntry {
    IoString path;                  ///< 包内相对路径（.rwdesign 布局相对名，UTF-8）
    std::uint64_t size = 0;         ///< 声明大小（字节——TempAreaBytes 记账依据）
};

/**
 * @brief 一致数据视图源（§9.9 原文两方法；io 定义、project 实现并注入
 *        ——P-IO-1 注入形态的接口面）。
 *
 * 稳定版本语义（§7.2 场景表原文）：导出期间后台写入并存时，"同 key 重
 * 读返回稳定版本"——drafts 单文件原子替换使 read 要么读到完整旧版要么
 * 完整新版；io 在单文件读取失败（source 报"已变化"的极窄窗口）时按快
 * 照重读一次（V18 的机制落点），仍失败→导出失败清理，**不产出混合版
 * 本包**。"文件存在≠已提交"（V32/D-17）由实现方的清单语义保证：枚举
 * 之外的磁盘文件 io 一概不读（§7.2②"清单外文件一概不读"）。
 *
 * 线程：单次 export_ 内由 io 在驱动线程串行调用；实现方无须并发安全。
 */
class ISnapshotFileSource {
public:
    virtual ~ISnapshotFileSource() = default;

    /**
     * @brief 枚举快照清单（§9.9；export 开始时锁定——D-17 快照闭包）。
     * @return 成功＝清单（实现方保证无重复 path、路径为包内相对名）；
     *         失败＝IO-RES-*（视图不可得——导出放弃，无任何副作用）
     */
    virtual IoResult<std::vector<PackFileEntry>> enumerate() const = 0;

    /**
     * @brief 读取一个快照文件的完整字节（§9.9；稳定版本语义——见类注）。
     * @param packPath [in] 清单中的 path（io 只读清单内路径——越权读取
     *                 是实现方违约，io 侧仍做包内相对名防御校验）
     * @return 成功＝完整字节（暂存期内存有界：单文件 ≤ 预算 SingleFileBytes）
     */
    virtual IoResult<IoString> read(const IoString& packPath) = 0;
};

// =====================================================================
// 导入：选项/会话/报告（§9.9 左列）
// =====================================================================

/**
 * @brief 包导入选项（§9.9 原文字段）。
 *
 * budget 语义（§4.5.2 包导入行）：四维强制硬限——调用方传入的 spec 经
 * packImportHardened() 语义校验（ArchiveExpandedBytes/ArchiveRatio/
 * FileCount/DirDepth 禁用放宽）；推荐直接传 BudgetSpec::packImportHardened()
 * 的收紧版。targetDir＝发布目标（project ⑧二次校验后 rename 的目的地；
 * io 侧仅在 ⑧前预检其不存在——§7.4"目标目录已存在"行，绝不创建）。
 *
 * replace 字段（卡面形态保留）：对**目录目标不适用**（§7.4 ReplacePolicy
 * 行原文"对目录不适用"）——导入器恒按 NeverOverwrite 语义执行目标预检；
 * 字段仅作契约面回显，不改变行为（防止调用方误以为可覆盖既有项目目录）。
 */
struct PackageImportOptions {
    std::filesystem::path targetDir;   ///< 发布目标（不存在才可通过预检——§7.4）
    BudgetSpec budget;                 ///< 预算规格（四维强制硬限——§4.5.2）
    ReplacePolicy replace = ReplacePolicy::NeverOverwrite;   ///< 目录不适用（类注）
};

/**
 * @brief 导入会话状态（§7.3 事务状态图＋§7.6 清理状态图的机器可判形态；
 *        枚举序＝状态图出现序——持久化契约面纪律）。
 *
 * 状态语义：Staging＝临时区已建（②完成）；Extracting＝③通过、④展开中
 * （同一 verifyThrough 调用内部态，外部观测多为终态——保留以支撑进度
 * 回调的阶段标签对齐）；Verified＝③~⑦全通过（等待 project 发布⑧）；
 * Canceled＝取消终态（非错误——UX-03）；Failed＝失败终态（清理成功）；
 * CleanupFailed＝清理失败终态（可重试 cleanup——§7.6）；Cleaned＝临时区
 * 已清理（发布后⑨或失败清理完成）。
 */
enum class PackageImportState : std::uint8_t {
    Staging,
    Extracting,
    Verified,
    Canceled,
    Failed,
    CleanupFailed,
    Cleaned,
};

/**
 * @brief 包导入校验报告（§7.3⑦：结构化、可脱敏呈现——O-11）。
 *
 * 所有路径字段均为脱敏 display 形态（§4.3.2）；诊断明细中不出现 \\?\
 * 原生形式（NFR-SEC-07 两级脱敏的第一层——用户级呈现前仍经 diagnostics
 * 脱敏设施）。取消路径上 diagnostics 恒为空（UX-03：取消不是错误，
 * V19"无 Canceled 诊断"的观测面）。
 */
struct PackageImportReport {
    PackageImportState finalState = PackageImportState::Staging;  ///< 终态/当前态
    std::uint64_t manifestEntries = 0;    ///< manifest 条目总数
    std::uint64_t verifiedEntries = 0;    ///< 哈希复算通过条目数
    std::uint64_t totalBytes = 0;         ///< 累计展开字节（成功路径＝全量）
    std::vector<ZipEntryVerifyReport> entryResults;   ///< 条目级哈希结论（清单序）
    std::vector<IoError> diagnostics;     ///< 结构化诊断（取消恒空——UX-03）
    std::vector<IoString> relaxedDimensions;   ///< 放宽项维度名（§4.5.2——包通道恒空）
    std::string stagingRootDisplay;       ///< 临时区根（脱敏 display——project 发布定位用）
    BudgetLedgerSnapshot ledgerSnapshot;  ///< 账本终值快照（V12 观测点——§9.2 ledger）
};

/**
 * @brief 包导入会话（§9.9"会话型——ImportSession 独占一个临时区"）。
 *
 * 值语义句柄（pimpl——拷贝共享同一底层会话；会话单线程，§9.9）。RAII
 * 兜底（§9.9 生命周期行"session RAII 兜底清理"）：底层析构时若临时区
 * 仍活动则尽力清理（失败静默——残留由 §7.5 标记机制回收，显式 cleanup
 * 才产生 CleanupFailed 报告面）。
 *
 * 责任切分锚点（acceptance 4）：本类型只暴露**只读观测**与 cleanup——
 * 没有 rename/发布 API（N-7）；payloadRoot() 是 project ⑧发布（同卷
 * rename）的操作对象，project 发布成功后调用 cleanup() 清理会话外殻。
 */
class PackageImportSession {
public:
    PackageImportSession() = default;
    ~PackageImportSession();
    PackageImportSession(PackageImportSession&& other) noexcept;
    PackageImportSession& operator=(PackageImportSession&& other) noexcept;
    PackageImportSession(const PackageImportSession&) = default;
    PackageImportSession& operator=(const PackageImportSession&) = default;

    /// 当前状态（§7.3 状态图——begin 后 Staging，verifyThrough 推进）。
    PackageImportState state() const noexcept;

    /// 临时区会话根（隐藏前缀目录；§7.5——io 全权管理，project 不触碰）。
    const std::filesystem::path& stagingRoot() const noexcept;

    /// 已验证树根＝`<stagingRoot>/payload`（§7.3⑧ project rename 的对象；
    /// Verified 前内容不可信——调用方仅在 state()==Verified 后使用）。
    std::filesystem::path payloadRoot() const noexcept;

    /// 最近一次报告（verifyThrough 终态时填充；取消/失败路径同样可用
    /// ——观测面，diagnostics 语义见 PackageImportReport 注）。
    const PackageImportReport& report() const noexcept;

    /// 实现承载（pimpl——zip 会话/manifest 解析产物不出公共头）。
    struct Impl;

    /// pimpl 通道（TempAreaSession::adopt 同款形态）：导入器实现装配/
    /// 访问底层会话的唯一入口——m_impl 保持私有，接口实现经本通道读写。
    static PackageImportSession adopt(std::shared_ptr<Impl> impl);
    std::shared_ptr<Impl> impl() const noexcept;

private:
    std::shared_ptr<Impl> m_impl;   ///< 会话承载（shared——值语义拷贝共享）
};

// =====================================================================
// IPackageImporter（§9.9——九步协议的 io 执行体）
// =====================================================================

/**
 * @brief 包导入器（§9.9 原文三方法——步骤①②/③~⑦/⑨ 的分界）。
 *
 * 行为契约（§9.9 契约表＋§7.3/§7.4/§7.6，逐条冻结）：
 *   - begin（①②）：包文件 P-1 预检（存在/可读/扩展名与魔数）＋目标父
 *     目录预检（存在可写——临时区同卷锚点，IO-D08）＋临时区会话建立。
 *     成功＝Staging 态会话；任何失败＝无会话产物（临时区不留）。
 *   - verifyThrough（③~⑦）：③版本与条目名预检（展开前！）→④逐条目
 *     展开＋预算/磁盘检查＋⑤逐文件哈希复算（展开即验，早失败早清理）
 *     →⑤目录结构镜像核对→⑥引用完整性（V29）→⑦报告＋目标目录占用
 *     预检（⑧前 io 侧——§7.4）。全通过＝Verified＋报告；任何失败/取消
 *     ＝终态（§7.6：自动清理，清理失败转 CleanupFailed）。重复调用：
 *     Verified 态幂等返回同一报告；其余非 Staging 态＝防御性拒绝。
 *   - cleanup（⑨/失败清理）：幂等（§9.10 TempArea 同款）；Verified 会
 *     话在 project 发布后调用（清理外殻）；CleanupFailed 会话重试。
 *   - 错误类型：§7.4 威胁矩阵全表＋IO-CANCELLED（状态非错误——调用方
 *     转 Canceled 不落诊断）＋IO-PACK-CLEANUP-FAILED。
 *   - 后置：Verified＝临时区含已验证树＋报告全绿；任何失败/取消→
 *     targetDir 零写入（结构性——发布归 project，acceptance 4）。
 *   - **非法调用**（§9.9 原文）：io 侧 rename 到 targetDir——本类型无
 *     此 API（类型层面排除）；未 verifyThrough 即要求发布数据；cleanup
 *     中删除非本会话文件——TempArea 契约保证不会发生。
 */
class IPackageImporter {
public:
    virtual ~IPackageImporter() = default;

    /**
     * @brief 步骤①②：选择包＋建立临时区（§9.9 原文签名）。
     *
     * @param packFile [in] 包文件路径（P-1 角色——用户显式选择）
     * @param options  [in] 导入选项（targetDir/budget——类注）
     * @param cancel   [in] 取消令牌（null＝不可取消；§7.3 逐步可取消）
     * @param progress [in] 进度回调（驱动线程内同步调用——§9.13.4）
     * @return 成功＝Staging 态会话；失败＝IO-RES-×（环境四分类）或
     *         IO-FORMAT-PACK-ZIP（魔数预检）/IO-RES-LOCK-CONFLICT（临时区
     *         互斥）/IO-CANCELLED
     */
    virtual IoResult<PackageImportSession>
        begin(const std::filesystem::path& packFile, const PackageImportOptions& options,
              IoCancelToken* cancel = nullptr, IoProgressCallback progress = {}) = 0;

    /**
     * @brief 步骤③~⑦：全量校验（卡面 §9.9 原文签名
     *        verifyThrough(PackageImportSession&) ＋两处登记等价增补）。
     *
     * 等价增补（§9.0"签名均为实现建议……实现期允许等价调整，语义不变"；
     * DTB §5.4 增量修订，io.md §15.5 v0.9 登记）：补尾随参数 cancel/
     * progress——§7.3 九步协议"逐步可取消/可失败"需要令牌在③~⑤的逐条
     * 目检查点生效；会话构造时锁死令牌会令同一会话无法被后续阶段分别
     * 驱动（begin 已同形态携带——对称）。缺省 null/空＝不可取消、无进度。
     *
     * @param session [in,out] begin 产出的会话（终态写入 session 与报告）
     * @param cancel  [in] 取消令牌（null＝不可取消；检查点＝每条目）
     * @param progress [in] 进度回调（驱动线程内同步调用——§9.13.4）
     * @return 成功＝校验报告（Verified）；失败＝对应威胁码（报告同样经
     *         session.report() 可观测；取消＝IO-CANCELLED 且无诊断）
     */
    virtual IoResult<PackageImportReport>
        verifyThrough(PackageImportSession& session, IoCancelToken* cancel = nullptr,
                      IoProgressCallback progress = {}) = 0;

    /**
     * @brief 步骤⑨/失败清理（§9.9 原文签名；幂等）。
     *
     * 清理失败＝IO-PACK-CLEANUP-FAILED（残留清单脱敏——§7.6；会话转
     * CleanupFailed 可重试——acceptance 2 的二次 cleanup 幂等可成功）。
     */
    virtual IoResult<void> cleanup(PackageImportSession& session) = 0;
};

// =====================================================================
// 导出：选项/报告（§9.9 右列）
// =====================================================================

/**
 * @brief 包导出选项（§9.9 原文字段＋三处登记等价增补）。
 *
 * 等价增补（DTB §5.4 增量修订，io.md §15.5 v0.9 登记）：§7.1 的
 * rwpack.json 契约要求 createdAtUtc/createdWithToolVersion/
 * sourceProjectId/content.headRevisionId 四项元数据，而 §9.9 卡面选项
 * 只有 targetFile/勾选/replace——快照源接口（§9.9 enumerate 仅 path+
 * size）不携带项目元数据，io 亦不得扫描源项目目录获取（§2.3 非目标 6：
 * 不绕过 project 存储端口）。增补四个调用方传入字段（project
 * PackageService 从其权威状态取得；测试显式传固定值＝IO-D11 可复现导
 * 出的注入面）。createdAtUtcIso8601 为空＝取当前 UTC 时间（唯一"时间
 * 戳作记录"位——§9.0 确定性注：不作判定）。
 */
struct PackageExportOptions {
    std::filesystem::path targetFile;   ///< .rwpack 目标（P-7——§4.1）
    bool includeResults = false;        ///< results/ 勾选（§7.1 可选树）
    bool includeReports = false;        ///< reports/ 勾选
    bool includeDrafts = false;         ///< drafts/ 勾选（勾选记忆归 ui——PM-05）
    ReplacePolicy replace = ReplacePolicy::OverwriteAtomic;   ///< 导出默认原子覆盖
    std::string sourceProjectId;        ///< 增补：源项目 id（§7.1 rwpack.json）
    std::string headRevisionId;         ///< 增补：HEAD 修订 id（§7.1 content）
    std::string createdAtUtcIso8601;    ///< 增补：创建时刻（空＝当前 UTC；IO-D11）
    std::string createdWithToolVersion; ///< 增补：工具版本（空＝产品默认串）
};

/**
 * @brief 包导出报告（§7.2⑥ 完整性自检的结论载体）。
 */
struct PackageExportReport {
    std::uint64_t entryCount = 0;     ///< 打包文件数（快照清单全量）
    std::uint64_t totalBytes = 0;     ///< 累计字节（清单 size 和——快照面）
    core::Digest256 manifestDigest{}; ///< manifest canonical 字节摘要（totalDigest 同源）
    bool integritySelfCheckPassed = false;   ///< ⑤重开包复检全绿
    std::string targetDisplay;        ///< 目标路径（脱敏 display）
};

// =====================================================================
// IPackageExporter（§9.9——§7.2 导出协议的 io 执行体）
// =====================================================================

/**
 * @brief 包导出器（§9.9 原文单方法；无状态服务——每次 export 自建临
 *        时区，§9.9 生命周期行）。
 *
 * 行为契约（§7.2 协议①~⑥，逐条冻结）：
 *   - enumerate 锁定快照→临时区（目标同目录旁 `.<name>.<8hex>.tmp/`）→
 *     逐文件 read→摘要→暂存（清单外一概不读；单文件读失败按快照重读
 *     一次——稳定版本语义/V18；仍失败→导出失败清理）→manifest+rwpack
 *     canonical 写出→压缩（zip64；进度/取消每文件）→⑤完整性自检（重
 *     开包全量哈希复算）→IAtomicFileWriter 原子替换到目标→清理临时区。
 *   - 失败/取消（⑥替换前）：目标不存在或为先前完整版本（V23——原子替
 *     换未发生）；零写源项目（只读——§7.2 场景表）。
 *   - 错误类型：IO-SEC-BUDGET-TEMP、IO-RES-*、IO-PACK-TARGET-EXISTS
 *     （NeverOverwrite）、IO-CANCELLED；取消不落诊断（UX-03）。
 *   - 非法调用（§9.9 原文）：传入目录扫描型 source（违反一致视图契约
 *     ——实现方义务，io 侧无法机器判定，依赖注入方契约）；导出中改
 *     targetFile（选项按值传入——调用方违约面）。
 */
class IPackageExporter {
public:
    virtual ~IPackageExporter() = default;

    /**
     * @brief 执行导出（§9.9 原文签名；consistentView 为 project 快照）。
     *
     * @param consistentView [in] 一致数据视图源（project 实现注入——
     *                       P-IO-1；本调用期间 io 持引用，不接管所有权）
     * @param options        [in] 导出选项（含 rwpack.json 元数据增补——类注）
     * @param budget         [in] 预算守卫（null＝防御性限检——TempAreaBytes/
     *                       SingleFileBytes 仍不可逾越）
     * @param cancel         [in] 取消令牌（null＝不可取消；检查点＝每文件）
     * @param progress       [in] 进度回调（驱动线程内同步——§9.13.4）
     * @return 成功＝导出报告（自检通过）；失败＝见类注错误类型
     */
    virtual IoResult<PackageExportReport>
        export_(ISnapshotFileSource& consistentView, const PackageExportOptions& options,
                IBudgetGuard* budget = nullptr, IoCancelToken* cancel = nullptr,
                IoProgressCallback progress = {}) = 0;
};

// =====================================================================
// 装配：设施集与工厂（§9.11 IoRuntime 落位前的直接装配形态）
// =====================================================================

/**
 * @brief 导入导出依赖设施集（§9.11 注入面在 IoSession.hpp 落位前的等价
 *        装配形态——各字段缺省＝产品真实实现；契约测试按 §11.1 以 fake
 *        适配层替换对应字段注入故障）。
 *
 * 所有权：各指针为共享句柄——设施可跨多个导入/导出会话复用；factory
 * 每 verifyThrough 调用一次产出会话级守卫（§9.2 guard 会话级）。
 */
struct PackageIoFacilities {
    /// SafePath 解析器（③条目名预检——P-4 批量规则核；缺省＝产品规则集）。
    ISafePathResolverPtr safePath;
    /// 预算守卫工厂（会话级——§9.2；缺省＝makeBudgetGuard）。
    std::function<IBudgetGuardPtr()> budgetFactory;
    /// 临时区管理器（②创建/⑨清理/V20 availableBytes 注入面；缺省＝真实）。
    ITempAreaManagerPtr tempAreas;
    /// 原子文件写出器（⑥目标替换/V23 注入面；缺省＝真实实现）。
    IAtomicFileWriterPtr atomicFiles;
};

/**
 * @brief 创建包导入器（缺省设施＝产品真实实现；测试注入 fake 字段）。
 */
std::unique_ptr<IPackageImporter> makePackageImporter(PackageIoFacilities facilities = {});

/**
 * @brief 创建包导出器（缺省设施＝产品真实实现；测试注入 fake 字段）。
 */
std::unique_ptr<IPackageExporter> makePackageExporter(PackageIoFacilities facilities = {});

} // namespace sdurws::ird::io

#endif // SDURWS_IRD_IO_PACKAGE_HPP
