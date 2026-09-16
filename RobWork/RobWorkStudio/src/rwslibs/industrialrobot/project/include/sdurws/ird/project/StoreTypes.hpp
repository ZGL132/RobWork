/**
 * @file   StoreTypes.hpp
 * @brief  project 存储层公共值类型（增量落位）——锁持有者记录与诊断 sink 契约。
 *
 * 设计依据：
 *   - units/project.md §3.1（组成表：`StoreTypes.hpp`——OpenMode/OpenStoreRequest、
 *     StoreError/StoreErrorCode、RecoveryReport、LockInfo、SchemaInfo、StorePath，
 *     详见 §4、§5.0/§5.1。**本文件按 §12 任务节奏增量落位**：PRJ-T03 落 §9.2
 *     锁持有者记录与 §5.0 IDiagnosticsSink；PRJ-T04 增补 §5.0 StoreError/
 *     StoreErrorCode（canonical 编解码的错误通道——格式解析的稳定拒绝码
 *     format-legacy/schema-future/store-corrupt 由此承载）；PRJ-T08 增补
 *     §5.1 打开协议类型（OpenMode/ObjectCacheBudget/SchemaInfo/LockInfo/
 *     RecoveryReport/OpenStoreRequest/OpenStoreResult）；
 *     不预建无消费者接口（NFR-MNT-04））；
 *   - §5.0（错误类型、诊断码与 diagnostics 适配——IDiagnosticsSink 定义原文）；
 *   - §5.1（打开与存储上下文——OpenStoreRequest/OpenStoreResult/RecoveryReport
 *     定义原文）；
 *   - §9.2（第二实例读取 PID：固定宽度记录，容忍撕裂读）；
 *   - §4.7（只读查询的线程与生命周期契约——上下文 Closed 后查询拒绝）；
 *   - 任务契约 tasks/foundation/PRJ-T03.json acceptance 2/4（PRJ-LOCK-HELD 含
 *     持有 PID；锁诊断经 IDiagnosticsSink 适配器产出 core::DiagnosticRecord
 *     ——P-PR-6 处置：注入式先行、不直链 diagnostics 库）、tasks/foundation/
 *     PRJ-T08.json acceptance 1～3（打开五步协议②③⑤、恢复报告、生命周期）。
 *
 * 增量落位说明（PRJ-T08，DTB §5.4 口径登记两处）：
 *   1. §3.1 组成表点名的 `StorePath` 类型本任务**不落位**——§5.1 公共接口
 *      全部使用 std::filesystem::path（canonicalPath() 原文签名）与宽字符串
 *      规范形态（win32::canonicalStorePath 返回值），StorePath 当前零消费者；
 *      后续出现真实消费者（如最近项目列表的路径键类型）时随其任务增量登记。
 *   2. `ObjectCacheBudget` 原文见于 §5.1 OpenStoreRequest.cacheBudget 字段
 *      与 §3.1 QueryPort.hpp 行"ObjectCache 预算参数"——因 OpenStoreRequest
 *      随本任务落位，预算类型先行落于本头（QueryPort.hpp 归 PRJ-T09，届时
 *      直接消费本类型，不重复定义）。
 *
 * 背景说明（P-PR-6 处置口径，为什么 sink 在 project 而实现在外部）：
 *   ARCH §3.5 登记 project→diagnostics 边，但 sink 的统一形态与归属归
 *   P-PR-6/P-EX-8 裁决（governance-log 两项均 open）。裁决前的实现形态＝
 *   **注入式先行**：project 自有接口（本文件），diagnostics（或 L5 装配）
 *   提供实现经构造注入；project 不链接 diagnostics 目标、不 include 其任何
 *   头（ird_gates 白名单预登记 project->diagnostics ≠ 要求链接）。因此本
 *   头只依赖 core 公共契约（core::DiagnosticRecord——DiagData.hpp），这是
 *   本单元首个 core 头消费点（此前 PRJ-T01/T02 按 P-PR-1 仅建链接边；本
 *   消费面由任务契约 PRJ-T03.json acceptance 4 明文要求，core.md §4.8 的
 *   DiagnosticRecord 已随 DIAG-T03/T04 在 diagnostics 侧钉住 v0.1 基线）。
 *
 * 线程安全：LockHolderRecord 为纯值类型（可任意复制）；IDiagnosticsSink
 * 实现的线程约束由实现方声明（project 侧调用点：命令/心跳线程与调用方
 * 线程——实现方须按 §9.8 线程模型自行串行化）。
 */

#ifndef SDURWS_IRD_PROJECT_STORETYPES_HPP
#define SDURWS_IRD_PROJECT_STORETYPES_HPP

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <sdurws/ird/core/DiagData.hpp>

// core::IDomainEventBus 仅以指针形式出现于 OpenStoreRequest（注入面），
// 前向声明即可（完整接口契约见 core 公共头 Events.hpp——P-PR-1 消费
// 基线 v0.1 §4.9/§5.8；include 链上不引入本头不消费的接口定义）。
// 注意位置：须在 sdurws::ird::project 命名空间之外声明（限定名的
// namespace 声明语法在嵌套位置会落错命名空间层级）。
namespace sdurws::ird::core {
class IDomainEventBus;
}

namespace sdurws::ird::project {

// ProjectStore 完整类型定义于 ProjectStore.hpp（本头只前向声明——
// OpenStoreResult 持有其 unique_ptr；特殊成员函数 out-of-line 定义于
// 实现文件，保证仅包含本头的翻译单元也能安全析构结果对象）。
class ProjectStore;

/**
 * @brief 存储层稳定错误码（§5.0 原文形态；token 表 §4.4.8）。
 *
 * 背景说明（错误语义二分，AGENTS §3/各任务卡）：StoreError 携带稳定 code
 * ＋机器可读 detail，**用户可见文案不在 project 生成**（NFR-REL-05——码表
 * 经 diagnostics 注册后由其供文案，PRJ-* 码值清单见 §5.0/diagnostics.md
 * §4.6）。枚举值与 §4.4.8 稳定 token 一一对应（LockHeldByOther ↔
 * "lock-held-by-other" 等），detail 面向开发诊断、前缀 "project/<域>:"。
 * 本枚举为封闭集：新增错误码＝单元卡增量修订（不私扩）。
 */
enum class StoreErrorCode {
    LockHeldByOther,           ///< lock-held-by-other（写锁被其他实例持有）
    MediaReadOnly,             ///< media-read-only（介质只读）
    AccessDenied,              ///< access-denied（OS 拒绝访问）
    NotAProject,               ///< not-a-project（目录缺失/非项目目录——PM-02 步骤①）
    FormatLegacy,              ///< format-legacy（旧格式稳定拒绝——§8.11 行 1/PM-06）
    SchemaFuture,              ///< schema-future（未来版本拒绝＋升级指引——§8.11 行 2）
    StoreCorrupt,              ///< store-corrupt（读校验失败/结构损坏——PM-02 读校验）
    WriteRejected,             ///< write-rejected（写权威缺失——§9.6 门卫）
    DiskFull,                  ///< disk-full（磁盘空间不足）
    ContextClosed,             ///< context-closed（存储上下文已关闭——§4.7）
    StaleRevisionRejected,     ///< stale-revision-rejected（expectedRevision 失配——§6.2/PM-04）
    UnknownCommand,            ///< unknown-command（未注册命令 token——§6.3）
    InvalidPayload,            ///< invalid-payload（命令载荷非法——§6.3）
    ConfirmationsUnresolved,   ///< confirmations-unresolved（待确认集未确认——§6.7）
    InteractionLost,           ///< interaction-lost（交互回调失效——§5.3.3）
    CommandAborted,            ///< command-aborted（命令中止——§6.7 取消/关闭路径）
    CompileFailed,             ///< compile-failed（双编译失败——RT 链，§6.6）
    ArchiveConflict,           ///< archive-conflict（归档重投递内容冲突——§10.1/D-14）
    ArchiveTargetMissing,      ///< archive-target-missing（归档目标缺失——§10.1）
    DraftCorrupt,              ///< draft-corrupt（草稿损坏/归属不符——§4.4.5/§8.4）
    BranchMetadataRegression,  ///< branch-metadata-regression（INV-M3 防回退——§4.5，防御性）
};

/**
 * @brief 存储层统一异常（§5.0 原文形态）：稳定码＋开发诊断 detail。
 *
 * 背景说明：project 全部错误一律 StoreError（§5 章约定——错误一律
 * StoreError，携带稳定 code＋机器可读 detail）。detail 约定为
 * "project/<域>: <key>=<value> ..." 形态的机器可读键值串（如 canonical
 * 编解码域为 "project/codec: schema-future document=20000 supported=10000
 * upgrade=ISchemaUpgrader"——schema-future 的升级指引数据随 detail 携带，
 * §8.11/PM-06：显示当前支持版本/项目版本/升级工具入口，不自动升级）；
 * 人读文案归 diagnostics 供文案（P-PR-6 链路）。
 *
 * 线程安全：异常对象按值抛出/捕获（what() 串为对象自带，无共享状态）。
 */
class StoreError : public std::runtime_error {
public:
    /**
     * @brief 构造携带稳定码与开发诊断明细的存储异常。
     *
     * @param code   [in] 稳定错误码（§4.4.8 token 对应的枚举值）
     * @param detail [in] 开发诊断明细，前缀 "project/<域>:"（§5.0 原文
     *               约定）；机器可读键值随域约定（如 codec 域的版本判定
     *               键值对）。经 runtime_error 基类持有拷贝。
     */
    StoreError(StoreErrorCode code, std::string detail)
        : std::runtime_error(detail), code_(code)
    {
    }

    /// 稳定错误码（noexcept 纯读取——错误分类判据，调用方 switch 用）。
    StoreErrorCode code() const noexcept { return code_; }

private:
    StoreErrorCode code_;  ///< §4.4.8 稳定 token 对应的枚举值（构造后不变）
};

/**
 * @brief 写锁持有者记录（`.rwdesign/lock` 文件内容的一行式固定宽度编码，
 *        §4.1 lock 行＋§9.2）。
 *
 * 背景说明：lock 文件由持有进程以固定宽度记录原地重写（§9.4——文件本身
 * 永不删除重建，防锁对象分裂 D-03），内容含持有者 PID、主机名与心跳时间
 * 戳。第二实例在获取写锁被内核拒绝后读取本记录用于"项目被 PID=<n> 持有"
 * 提示（PM-07）；记录只作诊断呈现，**不参与任何权限判定**（SA-17：写权限
 * 唯一依据＝OS 独占句柄；心跳陈旧不得触发接管或失权——ARCH §6.8 D-03）。
 *
 * 撕裂容忍（§9.2）：读取方可能与持有方的心跳原地重写并发——字段可能读到
 * 半截/垃圾内容。解析方必须容忍（缺失字段留空、垃圾数字取 0），不得视为
 * 存储损坏。
 */
struct LockHolderRecord {
    /// 持有进程 ID；单位＝OS PID（Windows 进程标识符，无物理单位）。
    /// 0 表示未知（撕裂读/记录为空）——诊断文案须按"未知"呈现而非显示 PID=0。
    std::uint32_t pid = 0;
    /// 持有者主机名（诊断用途；固定宽度字段内的空白填充由解析方剔除）。
    std::string host;
    /// 心跳时间戳，ISO-8601 UTC 带毫秒（如 2026-09-15T08:30:45.123Z）。
    /// **仅诊断用途**（§9.4：心跳周期 30 s，由 StoreLock 内心跳线程原地重写）；
    /// 不作写权限判据（SA-17/D-03），不作活性推测依据（卡顿不接管）。
    std::string heartbeatUtc;

    bool operator==(const LockHolderRecord& o) const noexcept
    {
        return pid == o.pid && host == o.host && heartbeatUtc == o.heartbeatUtc;
    }
    bool operator!=(const LockHolderRecord& o) const noexcept { return !(*this == o); }
};

/**
 * @brief project 诊断 sink 契约（§5.0 原文形态）——project 定义，
 *        diagnostics（或 L5 装配）提供实现。
 *
 * 背景说明（两级日志的分流规则）：report() 承载**用户级**稳定诊断（码值
 * 已收编 diagnostics StableCodeRegistry——PRJ-* 全量 10 项见 diagnostics.md
 * §4.6，P-PR-6 码值部分已消账）；reportDev() 承载**开发级**诊断（通道名
 * 自由 token，如 "project/lock-recovery"——接管残留报告、心跳写失败等不
 * 面向用户的观察点）。sink 指针在装配注入时可空（§5.1 OpenStoreRequest：
 * "可空：退化为开发诊断"）——可空时 project 侧丢弃诊断产出（装配方失去
 * 观察面是其自身选择，存储行为不受影响）。
 *
 * 生命周期与所有权：实现对象由装配方（L5/测试）持有，注入的裸指针为
 * **非 owning**，其生存期必须覆盖全部消费它的 project 对象。
 *
 * 线程约束：project 会在多线程调用（心跳线程＋调用方线程）——实现方必须
 * 自行保证线程安全（diagnostics 侧 §9.8 承诺单管线串行）。
 */
class IDiagnosticsSink {
public:
    /// 虚析构：经接口指针删除实现对象是多态所有权的常规路径。
    virtual ~IDiagnosticsSink() = default;

    /**
     * @brief 上报用户级稳定诊断记录。
     *
     * @param record [in] 已通过 core::DiagnosticRecord::make() 工厂校验
     *               （C-3：码句法＋必填串非空）的记录；project 侧产出点
     *               使用的码值限于 diagnostics.md §4.6 收编的 PRJ-* 清单
     *               （码值分配权威在 diagnostics——CR-08，project 不私造码）。
     */
    virtual void report(const core::DiagnosticRecord& record) = 0;

    /**
     * @brief 上报开发级诊断（不进入用户可见诊断目录的观察点）。
     *
     * @param channel [in] 通道 token（如 "project/lock-recovery"）；调用方
     *                约定前缀 "project/" 以便 diagnostics 侧分流/脱敏路由。
     * @param message [in] 自由文本（面向开发诊断，可含路径/PID 等明细——
     *                用户级脱敏归 diagnostics 的 NFR-SEC-07 处置）。
     */
    virtual void reportDev(const std::string& channel, const std::string& message) = 0;
};

// =====================================================================
// §5.1 打开协议类型（PRJ-T08 增量落位）
// =====================================================================

/**
 * @brief 打开模式（§5.1 OpenMode 原文）。
 *
 * 背景说明（PM-07 只读打开的两种来源）：Writable＝请求写权限（被其他
 * 实例持锁时**降级为只读**并携带持有者信息，不阻塞等待——§9.2）；ReadOnly
 * ＝用户显式只要读（同样不尝试获取写锁、可查看禁编辑）。两种来源的最终
 * 形态一致：OpenStoreResult.writable 报告实际取得的权限（唯一依据＝本
 * 实例是否持有 OS 排他句柄，SA-17）。
 */
enum class OpenMode {
    Writable,   ///< 请求写权限；失败降级只读（PM-07 不阻塞等待）
    ReadOnly,   ///< 显式只读打开（可查看、禁编辑与应用提交——§9.2③）
};

/**
 * @brief 对象缓存预算参数（§5.1 OpenStoreRequest.cacheBudget 字段类型；
 *        §3.1 QueryPort.hpp 行"ObjectCache 预算参数"——随本任务先行落位）。
 *
 * 背景说明：包装而非裸 size_t，是为了让"预算"语义在打开协议签名上自明
 * （§4.6：LRU 缓存默认 256 MiB 可配——字节预算是打开时一次性注入的
 * 上下文级参数，存续期不变）。默认值与 ObjectStore::kDefaultCacheBudgetBytes
 * 同源（§4.6 原文 256 MiB）。
 */
struct ObjectCacheBudget {
    /// 缓存预算，单位：字节；0＝使用实现默认（256 MiB，§4.6）。
    std::size_t budgetBytes = 0;

    bool operator==(const ObjectCacheBudget& o) const noexcept
    {
        return budgetBytes == o.budgetBytes;
    }
};

/**
 * @brief 存储格式信息（§5.1 ProjectStore::schema() 返回类型）。
 *
 * 背景说明：projectId 是身份（随对象可能复现），schemaVersion/formatId 是
 * **格式契约**（决定本实现能否解读该存储）——打开成功即表示格式被支持
 * （§8.11：旧格式/未来版本在打开②步稳定拒绝，不会到达此处）。
 */
struct SchemaInfo {
    /// schema 版本编码值（主版本×10000＋次版本，kSchemaVersionCurrent 口径）。
    int schemaVersion = 0;
    /// 格式标识 token（当前恒 "rwdesign"——kFormatId）。
    std::string formatId;

    bool operator==(const SchemaInfo& o) const noexcept
    {
        return schemaVersion == o.schemaVersion && formatId == o.formatId;
    }
};

/**
 * @brief 锁信息视图（§5.1 ProjectStore::lockInfo() 返回类型：
 *        "{pid, host, heartbeatUtc, isSelf}"）。
 *
 * 背景说明（PM-07 提示的数据面）：持有者字段复用 §9.2 固定宽度记录的
 * 撕裂容忍解析结果（零值字段＝"未知"，呈现层须按未知处理而非显示 0）；
 * isSelf＝本上下文自身持有写锁（writable==true 时恒 true）。全部字段仅
 * 诊断用途，不作权限判据（SA-17：写权限唯一依据＝OS 排他句柄）。
 */
struct LockInfo {
    /// 持有者记录（本上下文持有＝自我身份；被拒/只读＝他方或零值）。
    LockHolderRecord holder;
    /// 是否本上下文自身持有（true 时 writable() 亦为 true——同一事实的
    /// 两个观察面）。
    bool isSelf = false;

    bool operator==(const LockInfo& o) const noexcept
    {
        return holder == o.holder && isSelf == o.isSelf;
    }
};

/**
 * @brief 恢复报告（§5.1 原文形态；PM-08 恢复诊断数据——呈现归 PM-15/ui）。
 *
 * 背景说明（§7.4 恢复顺序的数据落点）：headIntegrityVerified＝④闭包
 * 完整性校验结论；ignoredStagingTxs＝①未提交事务清单（忽略不删＋
 * PRJ-RECOVERY-IGNORED-UNCOMMITTED）；orphanDraftFiles＝③孤儿/损坏草稿
 * 清单（PRJ-RECOVERY-ORPHAN-DRAFT）；danglingObjectCount＝⑤悬挂对象
 * 只读计数（不删——GC 范围外）；diagnostics＝本次打开产出的全部用户级
 * 诊断记录快照（与经 IDiagnosticsSink 逐条上报的内容同源同序——报告
 * 随结果返回＋sink 即时上报双通道，消费方按需取用）。
 *
 * 清单确定性排序（NFR-COR-02）：字符串清单按字典序；诊断按产出时序
 * （①→③→⑤）。同磁盘状态必得同报告。
 *
 * 线程安全：纯值类型（由 factory.open 一次性产出，此后不可变）。
 */
struct RecoveryReport {
    /// true＝HEAD 引用闭包的全部清单/对象可读且 size＋SHA-256 校验通过
    /// （§7.4④；false＝存储损坏——open 语义下伴随 StoreCorrupt 失败，
    /// 不产生可用的存储上下文）。
    bool headIntegrityVerified = false;
    /// 未提交事务的 .staging/<tx-id> 目录名清单（忽略不删，现场保留）。
    std::vector<std::string> ignoredStagingTxs;
    /// 孤儿/损坏草稿文件清单（相对 drafts/ 的路径，含 .new/.bak 残留——
    /// §7.4③/§8.4；草稿恢复入口数据，DraftService/PRJ-T12 消费）。
    std::vector<std::string> orphanDraftFiles;
    /// 悬挂对象计数（闭包外对象——只读计数不删，PM-08-S1/R2 报告源）。
    std::uint64_t danglingObjectCount = 0;
    /// 本次打开产出的用户级诊断记录快照（码值限于 diagnostics.md §4.6
    /// 收编的 PRJ-* 清单——CR-08 不私造码；P-PR-6：经 IDiagnosticsSink
    /// 注入上报，本字段是同步回执）。
    std::vector<core::DiagnosticRecord> diagnostics;
};

/**
 * @brief 打开请求（§5.1 OpenStoreRequest 原文形态）。
 *
 * 背景说明：path 指向 .rwdesign 项目目录（包文件形态归阶段 B——workflow
 * 解包后转目录打开，§5.1 字段注释）。eventBus/diagnostics 为装配注入的
 * 非 owning 指针，**可空**：空总线＝跳过提交第 6 步事件发布（测试/只读
 * 场景，§7.1）；空 sink＝诊断退化为丢弃（§5.0——装配方失去观察面是其
 * 自身选择，存储行为不受影响）。
 *
 * 所有权与生存期：两个注入指针非 owning，其生存期必须覆盖返回的存储
 * 上下文（ProjectStore）的整个生命周期——上下文在其存续期随时可能使用
 * （提交事件/诊断上报）。
 */
struct OpenStoreRequest {
    /// .rwdesign 项目目录（任意拼写——打开协议内部先规范化为最终路径，
    /// §9.3；不存在＝not-a-project，§8.7①兜底口径）。
    std::filesystem::path path;
    /// 打开模式（默认可写——PM-07 降级语义见 OpenMode 注释）。
    OpenMode mode = OpenMode::Writable;
    /// 事件总线（§3.2 消费清单 core v0.1 §4.9/§5.8；可空＝跳过事件步）。
    core::IDomainEventBus* eventBus = nullptr;
    /// 诊断 sink（§5.0；可空＝丢弃诊断）。
    IDiagnosticsSink* diagnostics = nullptr;
    /// 对象缓存预算（默认 256 MiB，§4.6）。
    ObjectCacheBudget cacheBudget;
};

/**
 * @brief 打开结果（§5.1 OpenStoreResult 原文形态）。
 *
 * 背景说明：失败以异常表达（§5 章约定——错误一律 StoreError），本结构
 * 只承载成功打开；store 失败时为空的原文语义由"异常路径不构造本结构"
 * 承接（等价表达：能拿到本结构则 store 必非空）。writable＝实际取得的
 * 写权限（Writable 请求可能降级 ReadOnly）；lockInfo＝锁视图（PM-07
 * "项目被 PID=<n> 持有"提示数据）；recovery＝恢复报告（⑤步产出）。
 *
 * 特殊成员函数 out-of-line（实现文件内定义，那里 ProjectStore 为完整
 * 类型）：unique_ptr<ProjectStore> 成员的删除器实例化需要完整类型，
 * out-of-line 让仅包含 StoreTypes.hpp 的翻译单元也能安全移动/析构。
 */
struct OpenStoreResult {
    /// 析构（实现于 ProjectStoreImpl.cpp——ProjectStore 完整类型可见处）。
    ~OpenStoreResult();
    /// 默认构造（store 为空——测试占位/移动目标）。
    OpenStoreResult() = default;
    /// 移动构造/赋值（unique_ptr 所有权转移；实现同上 out-of-line）。
    OpenStoreResult(OpenStoreResult&& other) noexcept;
    OpenStoreResult& operator=(OpenStoreResult&& other) noexcept;

    /// 打开的存储上下文（成功打开必非空；调用方独占持有）。
    std::unique_ptr<ProjectStore> store;
    /// 实际取得的写权限（Writable 请求可能降级 ReadOnly——PM-07）。
    bool writable = false;
    /// 锁视图（持有者 PID/host/心跳＋isSelf——仅诊断用途，SA-17）。
    LockInfo lockInfo;
    /// 恢复报告（§7.4 恢复顺序①③④⑤的产出；正常打开全空/false 之外
    /// 均为默认值——headIntegrityVerified 恒 true，否则打开已失败）。
    RecoveryReport recovery;
};

}  // namespace sdurws::ird::project

#endif  // SDURWS_IRD_PROJECT_STORETYPES_HPP
