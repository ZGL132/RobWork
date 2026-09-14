/**
 * @file   Logging.hpp
 * @brief  两级日志（Logging）——LogRecord/两级分流管线/采样节流/轮转/
 *         WorkerLogBatch 重放合并与 ILogFileOps 文件接缝。
 *
 * 设计依据：
 *   - units/diagnostics.md §7.1（两级日志模型——Tier-U 用户级/Tier-D 开发级
 *     由同一设施产出、Tier-U ⊆ Tier-D）、§7.2（LogRecord 字段表）、§7.3（管线
 *     五步：格式化→脱敏→级别过滤→节流采样→异步写入＋flush 窗口）、§7.4（采样
 *     仅 Debug/Trace、节流按 (code+channel) 10 s 窗、全局稳定序＝入队序）、
 *     §7.5（worker 批量回传 WorkerLogBatch 与按序号重放合并、断裂补条）、
 *     §7.6（崩溃前保留——flush 窗口 N=256 行/T=2 s/Error 即时）、§7.8（日志
 *     非权威——禁用日志不影响正式诊断）、§9.6（ILogger 接口原文与契约表）
 *   - 需求 NFR-REL-05（日志分用户诊断与开发诊断；用户诊断不含无意义调用栈
 *     或内部哈希）、UX-02（用户级不显示哈希/Schema/内部插件名）
 *   - 任务契约 tasks/foundation/DIAG-T07.json（≙WP-09-T04）：acceptance 1
 *     （DT-LOG-1~5）、acceptance 2（P-DIAG-7）、acceptance 3（P-EX-8）
 *
 * 背景说明（为什么两级日志由"一条记录＋分流"而不是两套 API 产出）：
 *   NFR-REL-05 要求日志分用户诊断与开发诊断两级，但若提供两套记录入口，
 *   产生方很容易只写开发级而漏掉用户级（或反之），两级语义漂移。§7.1 冻结
 *   "两 Tier 由同一设施产出（一个 LogRecord，分流到两个 sink 链）"：调用方
 *   只声明 tier 与 level，分流/过滤/脱敏/采样全部由本管线统一执行——Tier-U
 *   行可由 Tier-D 行重建（Tier-U ⊆ Tier-D 信息，脱敏后）。日志是运维快照
 *   （§7.8 非权威），绝不参与项目事实重建，因此失败语义全部"尽力而为＋
 *   降级计数"，绝不阻塞调用方、绝不向调用方抛环境错误。
 *
 * 陷阱处置锚点（契约 knownPitfalls 逐项，acceptance 2/3）：
 *   - P-DIAG-7：轮转（默认 5×2 MiB/文件）与 flush 窗口（N=256/T=2 s）是
 *     **可配工程默认**（LogSinkConfig 默认值登记于 §14.3），WP-23 性能验收
 *     时校准，不作为需求语义——需求只要求"有界丢失窗口"（§7.6）与轮转覆盖
 *     生命周期（§7.8），参数值可调。
 *   - P-EX-8：worker 日志回传只定 WorkerLogBatch **载荷语义**（§7.5：字段、
 *     序号重放、断裂补条）；帧类型/通道帧表归 execution §6（§12.2 交接）——
 *     本头不定义任何帧类型，不 include execution 侧头（R-1/R-2，测试以源码
 *     扫描钉住）。
 *
 * 线程安全：ILogger 全部方法任意线程可调（内部互斥＋日志线程串行化——§9.6
 * 契约表）；LoggingPipeline 的析构除外（须保证调用方已停止使用——进程级
 * 设施关闭语义）。日志行全局稳定序＝入队序（单调序号，§7.4）。
 * 确定性：节流窗口判定用记录自身时间戳（注入 IClock）——同输入序列同输出
 * 行序（NFR-COR-02 精神）；有界等待（flush/deadline）用 steady_clock 墙钟
 * （安全属性不依赖可替换时钟，防止测试时钟冻结导致永久等待）。
 */

#ifndef SDURWS_IRD_DIAGNOSTICS_LOGGING_HPP
#define SDURWS_IRD_DIAGNOSTICS_LOGGING_HPP

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sdurws/ird/core/DiagData.hpp>    // core::DiagCode（日志 code 字段——稳定码承载）
#include <sdurws/ird/core/Identity.hpp>    // core::TaskIdentity/RunId/AttemptId/RevisionId（关联 ID）
#include <sdurws/ird/diagnostics/Catalog.hpp>      // DiagEntryId/IClock/IDevLogSink（§4.2/§9.7）
#include <sdurws/ird/diagnostics/Confirmable.hpp>  // FindingId（§7.2 correlationIds.findingId）

namespace sdurws::ird::diagnostics {

// =====================================================================
// 词表与常量（§7.2/§9.6 原文）
// =====================================================================

/**
 * @brief 日志层级（§9.6 原文 enum class LogTier { User, Dev }）。
 *
 * User＝用户级日志（Tier-U：稳定码＋标题键＋脱敏参数＋对象定位；禁止调用
 * 栈/内存地址/内容哈希/内部 token/未脱敏路径——NFR-REL-05/UX-02，管线在
 * Tier-U 侧强制模式过滤）；Dev＝开发级（Tier-D：全量技术细节，与 Tier-U
 * 同一脱敏管线——NFR-SEC-07 全量脱敏随 DIAG-T08 接线）。
 */
enum class LogTier : std::uint8_t {
    User,  ///< user——用户级（Tier-U）
    Dev,   ///< dev——开发级（Tier-D）
};

/**
 * @brief 日志级别（§9.6 原文五值；≠诊断 severity——§7.2 映射：诊断
 *        severity→日志 level 一一对应＋Dev→Debug）。
 *
 * 采样与过滤语义（§7.4/§7.3③）：Error/Warning/Info 永不采样（用户语义
 * 事件不丢）；Debug/Trace 可采样；Tier-U 侧只收 Error/Warning/Info。
 */
enum class LogLevel : std::uint8_t {
    Error,    ///< error——错误（即时 flush——§7.6 崩溃前保留）
    Warning,  ///< warn——警告
    Info,     ///< info——信息
    Debug,    ///< debug——调试（可采样）
    Trace,    ///< trace——跟踪（可采样，量最大）
};

/// 取层级 token（"user"/"dev"；日志行与观测面用；switch 全枚举——确定性）。
std::string_view logTierToken(LogTier tier) noexcept;

/// 取级别 token（"ERROR"/"WARN"/"INFO"/"DEBUG"/"TRACE"；switch 全枚举）。
std::string_view logLevelToken(LogLevel level) noexcept;

/// 级别重要性秩（Error=0 最重 → Trace=4 最轻；过滤与节流比较用，纯函数）。
std::uint8_t logLevelRank(LogLevel level) noexcept;

/// 单条日志消息上限（§7.2：≤4 KiB，超出截断并标注——管线第①步执行）。
inline constexpr std::size_t kLogMessageMaxBytes = 4096;

/// 通道 token 上限（§7.2 LogChannel ≤48 字符——超限＝调用方违约）。
inline constexpr std::size_t kLogChannelMaxChars = 48;

/// 线程标记上限（§7.2 threadTag "同 DiagnosticEntry"——§4.2 ≤32 字符）。
inline constexpr std::size_t kLogThreadTagMaxChars = 32;

/// flush 有界等待默认时限（§9.6：排空默认 3 s；超时放弃＋DIAG-LOG-WRITE-FAILED）。
inline constexpr std::chrono::milliseconds kLogDefaultFlushDeadline{3000};

// =====================================================================
// CorrelationIds / LogRecord（§7.2 字段表）
// =====================================================================

/**
 * @brief 日志关联 ID 块（§7.2 correlationIds——§6.5 关联图的日志侧锚点）。
 *
 * 字段集＝§7.2 原文六项（entryId?/findingId?/taskId?/runId?/attemptId?/
 * revisionId?），全部可空；至少一个非空才可跨行关联——无关联 ID 的行为
 * 纯开发输出（§7.2 行注）。类型与目录/工厂同源（core 值类型＋本单元
 * DiagEntryId/FindingId），不另造字符串形态（日志不以文本代替稳定身份）。
 *
 * 值语义；纯值（并发只读安全）。
 */
struct CorrelationIds {
    /// 关联的目录条目身份（§4.2 entryId——日志行引用诊断时的锚点）。
    std::optional<DiagEntryId> entryId;
    /// 关联的可确认诊断身份（FindingId 规范形态承载——Confirmable.hpp）。
    std::optional<FindingId> findingId;
    /// 关联的任务五元组（TASK-03；worker 批次回传时由批载荷注入——§7.5）。
    std::optional<core::TaskIdentity> taskId;
    /// 关联的运行身份（五元组的 run 分量可单独锚定）。
    std::optional<core::RunId> runId;
    /// 关联的尝试序号（同上）。
    std::optional<core::AttemptId> attemptId;
    /// 关联的修订身份（命令路径日志行锚定修订）。
    std::optional<core::RevisionId> revisionId;

    /// 是否至少一个非空（§7.2"至少一个非空才可跨行关联"的判定原语）。
    bool any() const noexcept;
    /// 精确等值（测试/重放路径核对用）。
    bool operator==(const CorrelationIds& o) const;
    bool operator!=(const CorrelationIds& o) const { return !(*this == o); }
};

/**
 * @brief 一条日志记录（§7.2 字段表——两级日志的唯一载荷形态）。
 *
 * 生命周期：调用方构造后按值交予 ILogger::log（管线接管拷贝——返回即入队
 * 确认，调用方可立即复用/销毁原对象）。message 传入为**原文**（脱敏在管线
 * 内强制执行——调用方无需也无法预脱敏，§9.6 契约表）；截断/过滤后的呈现
 * 形态由管线在写入时派生，不回写本结构。
 *
 * worker 回传路径（§7.5）：批次内记录保留 worker 侧原始 timestampUtc 与
 * correlationIds（"每行保留原始时间戳可重排"——§7.4），workerId/task 由
 * 批载荷补注（replayWorkerBatch 注释）。
 *
 * 值语义；纯值（并发只读安全）。
 */
struct LogRecord {
    /// 写入时间（UTC；注入 IClock 打点——§7.2。本地路径由管线入队时打点；
    /// worker 回传路径保留原始值不重打）。
    std::chrono::system_clock::time_point timestampUtc{};
    /// 层级（Tier-U/Tier-D——分流依据；必填显式设置，无语义默认）。
    LogTier tier = LogTier::User;
    /// 日志级别（≠诊断 severity——映射见 LogLevel 注释）。
    LogLevel level = LogLevel::Info;
    /// 通道＝来源子系统 token（≤48 字符；如 "project/store"、"diag/logging"
    /// ——词表随单元登记，非空校验在入口）。
    std::string channel;
    /// 关联 ID 块（可空——纯开发输出行）。
    CorrelationIds correlationIds;
    /// 关联诊断时的稳定码（"日志不以文本代替稳定码——凡对应诊断的事件必带
    /// code"——§7.2；无关联诊断时为空。码值权威＝StableCodeRegistry，日志面
    /// 只承载不校验注册——注册校验归工厂/目录路径）。
    std::optional<core::DiagCode> code;
    /// 消息原文（≤4 KiB，超出由管线截断并标注 "[trunc]"；空串＝调用方违约）。
    std::string message;
    /// 产生线程标记（≤32 字符 ASCII，可空——"同 DiagnosticEntry"§7.2）。
    std::string threadTag;
    /// 产生 workerId（worker 侧产生/回传时必填、主进程产生时空——§7.5 回传
    /// 路径由 replayWorkerBatch 强制补注）。
    std::optional<std::uint64_t> workerId;

    /// 精确等值（测试断言用；message 逐字节比对）。
    bool operator==(const LogRecord& o) const;
    bool operator!=(const LogRecord& o) const { return !(*this == o); }
};

// =====================================================================
// LogSinkConfig（§9.6 configure 载荷；P-DIAG-7 工程默认登记）
// =====================================================================

/**
 * @brief 日志 sink 配置（§9.6 configure(LogSinkConfig)——L5 装配期设置：
 *        文件路径/轮转/flush 窗口/采样参数/级别过滤）。
 *
 * ★ P-DIAG-7 处置（契约 acceptance 2）：rotateFiles/rotateMaxBytes/
 *   flushEveryLines/flushInterval/sampleEveryDebug/queueCapacity 均为
 *   **可配工程默认**（§14.3 登记：轮转 5×2 MiB/文件、flush 窗口 N=256/T=2 s
 *   ——上游需求只定义"有界丢失窗口"（§7.6：崩溃丢 ≤2 s 的 Dev 行、0 行
 *   Error）与"轮转覆盖"生命周期（§7.8），未定义参数值），WP-23 性能验收
 *   时校准，不作为需求语义。修改默认值不构成需求偏差。
 *
 * 值语义；configure 按值传入（管线在日志线程串行生效——切换瞬间的在途行
 * 仍按旧配置写完，切换语义＝下一条记录起生效）。
 */
struct LogSinkConfig {
    /// 总开关（false＝两级文件均不写——DT-LOG-4：禁用日志不影响正式诊断，
    /// §7.8；管线仍接收/排空记录，只在写步丢弃）。
    bool enabled = true;
    /// 用户级文件开关（Tier-U；与 enabled 相与）。
    bool userEnabled = true;
    /// 开发级文件开关（Tier-D；与 enabled 相与）。
    bool devEnabled = true;
    /// 两文件共同目录（用户目录——D-10 不入 .rwdesign，§9.6 副作用行；
    /// L5 装配给产品路径，测试给临时目录。空路径＝写失败计数——装配未完成
    /// 时的安全降态，不抛）。
    std::filesystem::path directory;
    /// 用户级文件名（§7.3：user-diagnostics.log）。
    std::string userFileName = "user-diagnostics.log";
    /// 开发级文件名（§7.3：dev-diagnostics.log）。
    std::string devFileName = "dev-diagnostics.log";
    /// 轮转保留文件数（含活动文件；≥1。P-DIAG-7 工程默认 5——§7.6/§7.8）。
    std::uint32_t rotateFiles = 5;
    /// 单文件轮转阈值（字节；0＝禁用轮转。P-DIAG-7 工程默认 2 MiB＝2×1024×1024）。
    std::uint64_t rotateMaxBytes = 2u * 1024u * 1024u;
    /// flush 行窗 N（每写 N 行强制 flush 一次；0＝禁用行窗。P-DIAG-7 工程默认 256——§7.3⑤）。
    std::uint32_t flushEveryLines = 256;
    /// flush 时间窗 T（距上次 flush 超过 T 的下一次写入强制 flush；0＝禁用时间窗。
    /// P-DIAG-7 工程默认 2 s——§7.3⑤"崩溃前保留窗口 ≤2 s"）。
    std::chrono::milliseconds flushInterval{2000};
    /// 采样 N 默认值（仅 Debug/Trace：首条＋每 N 条＋末条——§7.4；≥1；
    /// sampleEveryByChannel 可按通道覆盖）。
    std::uint32_t sampleEveryDebug = 100;
    /// 按通道覆盖采样 N（空表＝全部用 sampleEveryDebug——"N 随通道配置，
    /// 默认 100"§7.4 的配置承载）。
    std::map<std::string, std::uint32_t> sampleEveryByChannel;
    /// Tier-D 级别过滤下限（§7.3③"Tier-D 按 level 配置"：秩 ≤ 该值的级别
    /// 才写开发文件；默认 Trace＝全量镜像——Tier-U ⊆ Tier-D，§7.1）。
    LogLevel devMinLevel = LogLevel::Trace;
    /// 节流窗宽（秒；同 (code+channel) 高频重复首条全量＋每窗汇总一条——
    /// §7.4；工程默认 10 s，上游原文锚定值）。
    std::uint32_t throttleWindowSeconds = 10;
    /// 异步队列容量（行；满时丢弃最旧 Debug/Trace＋Dev 计数诊断——§9.6；
    /// 工程默认 4096，登记于 §14.4 实现口径）。
    std::uint32_t queueCapacity = 4096;
};

// =====================================================================
// WorkerLogBatch（§7.5 载荷——P-EX-8：只定载荷语义，不定帧格式）
// =====================================================================

/**
 * @brief worker 日志批量回传播荷（§7.5：WorkerLogBatch{workerId, task 五元组,
 *        序号, LogRecord[]}）。
 *
 * ★ P-EX-8 处置（契约 acceptance 3）：本结构只定义**载荷语义**（字段、批次
 * 序号的重放合并规则、断裂补条——见 LoggingPipeline::replayWorkerBatch）；
 * 帧类型/通道帧表（诊断批与日志批的区分、帧序号防断裂的通道级语义）归
 * execution §6（§12.2 交接："WorkerLogBatch 帧契约……其通道 §6 帧表登记时
 * 对齐本文 §7.5"）——本头不定义帧类型、不 include execution 侧头（R-1/R-2），
 * 不私定对端帧格式。
 *
 * 载荷字段语义（§7.5 原文）：
 *   - workerId：产生批次的 worker 进程身份（合并入主进程文件时补注各行的
 *     workerId 字段——"correlationIds 保留 workerId"）；
 *   - task：任务五元组（TASK-03；批次内未携带 taskId 的记录以此补注）；
 *   - seq：批次序号，在 (workerId, task) 流内**从 0 起连续编号**（重放合并
 *     的唯一排序键——"按序号重放合并"§7.5；主进程据此恢复全局稳定序）；
 *   - final：收尾标记（载荷语义的组成部分：true＝该流此批之后不再有批次，
 *     主进程据此把 [期望序号, seq) 的缺口判定为通道断裂并补条——§7.5
 *     "批次丢失→主进程补一条 Dev 诊断"；无收尾批的静默丢失在排空/关闭时
 *     同规则补条）；
 *   - records：本批日志记录（worker 侧原始 timestampUtc/correlationIds
 *     保留——§7.4"每行保留原始时间戳可重排"）。
 *
 * 值语义；纯值。
 */
struct WorkerLogBatch {
    std::uint64_t workerId = 0;      ///< 产生批次的工作进程身份（execution WorkerId 承载）
    core::TaskIdentity task;         ///< 任务五元组（TASK-03——批次记录的默认关联）
    std::uint64_t seq = 0;           ///< 批次序号（流内从 0 连续——重放排序键）
    bool final = false;              ///< 收尾标记（其后无更多批次——断裂判定触发之一）
    std::vector<LogRecord> records;  ///< 批内记录（保序；可为空＝纯收尾批）

    /// 精确等值（测试断言用）。
    bool operator==(const WorkerLogBatch& o) const;
    bool operator!=(const WorkerLogBatch& o) const { return !(*this == o); }
};

// =====================================================================
// ILogFileOps——文件写入接缝（§10 DT-LOG-3：故障注入经本接缝，
// testkit D-10 形态；产品默认实现 FileLogFileOps）
// =====================================================================

/**
 * @brief 日志文件操作接缝（两级日志与磁盘的唯一 I/O 边界）。
 *
 * 为什么接缝化：DT-LOG-3 要求注入磁盘写失败验证"DIAG-LOG-WRITE-FAILED 且
 * 不阻塞调用方、无异常逃逸"（§10 行："ILogSinkOps 注入失败……经 ILogFileOps
 * 接缝，testkit D-10 形态"）；测试注入故障实现（返回 false 或抛异常——
 * 两类都必须被管线消化），产品装配注入 FileLogFileOps。
 *
 * 实现契约：全部方法**允许抛异常**（管线捕获消化——"无异常逃逸"由管线
 * 保证而非实现方）；应快速返回（日志线程串行调用本接口，阻塞会推迟全队）。
 * 单实例可能被 flush() 调用方与日志线程并发触碰——实现方自行串行化
 * （FileLogFileOps 内部互斥）。
 */
struct ILogFileOps {
    virtual ~ILogFileOps() = default;

    /**
     * @brief 追加一行（实现负责补换行与打开句柄——惰性 append 语义）。
     * @param file [in] 目标文件完整路径（管线按 config 解析）
     * @param line [in] 行内容（不含换行符；UTF-8 字节）
     * @return true＝写入成功；false＝失败（磁盘/权限/路径不可用——管线计数降级）
     */
    virtual bool appendLine(const std::filesystem::path& file, std::string_view line) = 0;

    /**
     * @brief 冲刷文件缓冲到磁盘（§7.6 flush 窗口的执行原语）。
     * @param file [in] 目标文件路径
     * @return true＝已冲刷（或本就无脏缓冲）；false＝冲刷失败
     */
    virtual bool flushFile(const std::filesystem::path& file) = 0;

    /**
     * @brief 轮转（§7.6/§7.8：旧档移位、开新文件——移位方案由实现定义，
     *        FileLogFileOps 为 "<base>.1..(keep-1)" 后缀移位）。
     * @param file      [in] 活动文件路径
     * @param keepFiles [in] 保留文件总数（含新活动文件；≥1）
     * @return true＝轮转成功；false＝部分/全部失败（管线继续写——尽力而为）
     */
    virtual bool rotateFile(const std::filesystem::path& file, std::uint32_t keepFiles) = 0;

    /**
     * @brief 查询当前文件大小（字节；不存在→0——轮转判定的基线，管线内部
     *        计数增量，避免每行 stat）。
     * @param file [in] 目标文件路径
     * @return 当前大小（字节）
     */
    virtual std::uint64_t fileSize(const std::filesystem::path& file) = 0;
};

/**
 * @brief 产品默认文件操作实现（L5 装配注入；真实磁盘 I/O）。
 *
 * 打开策略：append 惰性打开（首写时 fopen "ab"），句柄常驻直到 rotate 或
 * 析构（两文件各一——日志线程串行写）；flushFile＝fflush 不关句柄。
 * 轮转方案：活动文件移位为 ".1"，旧 ".1"→".2"……超出 keepFiles-1 的最老档
 * 删除（标准 generations 方案——§7.8"轮转覆盖"生命周期）。
 * 线程安全：内部互斥（flush() 调用方与日志线程并发触碰同一句柄面）。
 */
class FileLogFileOps final : public ILogFileOps {
public:
    /// 构造（分配句柄表；定义于 src/Logging.cpp——pimpl 需要完整实现体）。
    FileLogFileOps();

    ~FileLogFileOps() override;

    // 禁拷贝/禁移动：持有打开句柄（FILE* 非拥有语义复制无意义）。
    FileLogFileOps(const FileLogFileOps&) = delete;
    FileLogFileOps& operator=(const FileLogFileOps&) = delete;

    // ---- ILogFileOps（行为契约见接口注释）----
    bool appendLine(const std::filesystem::path& file, std::string_view line) override;
    bool flushFile(const std::filesystem::path& file) override;
    bool rotateFile(const std::filesystem::path& file, std::uint32_t keepFiles) override;
    std::uint64_t fileSize(const std::filesystem::path& file) override;

private:
    struct Impl;
    /// pimpl：隔离 FILE*/mutex 细节于头文件之外（公共头最小依赖面——R-2 纪律）。
    std::unique_ptr<Impl> m_impl;  ///< 实现体（句柄表＋互斥；Impl 完整定义于 src/Logging.cpp）
};

// =====================================================================
// ILogger（§9.6 接口原文——签名逐字承载，不增不删）
// =====================================================================

/**
 * @brief 两级日志接口（§9.6 原文四方法）。
 *
 * 行为契约（§9.6 签名注释＋契约表，逐行冻结）：
 *   - log：记录（任意线程；异步管线——§7.3；返回即入队确认，不等待落盘）。
 *     前置：record.message 为原文（脱敏在管线内强制执行）。后置：按
 *     tier/level 分流；Error 级即时 flush 请求。错误：队列满→丢弃最旧
 *     Debug/Trace＋Dev 计数诊断（不阻塞调用方、不抛）。
 *   - logDev：Dev 快捷入口（对齐 project reportDev 语义）。
 *   - flush：排空（关闭/崩溃前）：有界等待（默认 3 s；超时放弃＋
 *     DIAG-LOG-WRITE-FAILED——不永久等待）。
 *   - configure：配置（L5：文件路径/轮转/采样参数）。
 *
 * 非法调用（§9.6 契约表）：在 message 中放置凭据（管线兜底替换——防线不
 * 依赖调用方自觉；全量凭据/路径脱敏随 DIAG-T08 IRedactionService 接线，
 * Tier-U 内部模式过滤本管线内建）；UI 线程长阻塞等待 flush（flush 仅关闭
 * 路径）。调用方结构性违约（空通道/超长 token/tier 与 level 组合非法）按
 * AGENTS.md 错误语义 fail-fast——抛 DiagnosticsError(Usage)。
 */
class ILogger {
public:
    virtual ~ILogger() = default;

    /**
     * @brief 记录一条日志（§9.6 原文签名；异步——返回即入队确认）。
     *
     * @param record [in] 日志记录（按值接管；timestampUtc 由管线以注入
     *               IClock 打点覆盖——本地路径的写入时间权威在管线）
     *
     * @throws DiagnosticsError Usage（channel 空/超 48 字符、message 空、
     *         threadTag 超 32 字符、tier=User 且 level∈{Debug,Trace}——
     *         §7.3③"Tier-U sink 只收 Error/Warning/Info"的结构性违约）
     *         ——仅入口校验抛；队列/磁盘/脱敏等环境面一律降级不抛。
     */
    virtual void log(LogRecord record) = 0;

    /**
     * @brief Dev 快捷入口（§9.6 原文签名；对齐 project reportDev 语义）。
     *
     * @param channel [in] 通道 token（≤48 字符）
     * @param level   [in] 日志级别（Dev 层任意级别合法——含 Debug/Trace）
     * @param message [in] 消息原文（管线截断/脱敏）
     * @param ids     [in] 关联 ID 块（默认空＝纯开发输出行）
     *
     * @throws DiagnosticsError Usage（同 log 的通道/消息校验）
     */
    virtual void logDev(std::string_view channel, LogLevel level, std::string message,
                        CorrelationIds ids = {}) = 0;

    /**
     * @brief 排空（§9.6 原文签名；关闭/崩溃前调用——有界等待，不永久等待）。
     *
     * @param deadline [in] 有界等待时限（kLogDefaultFlushDeadline＝3 s 口径；
     *                 steady_clock 墙钟计量——不依赖注入时钟）
     * @return true＝队列排空且文件冲刷成功；false＝超时放弃或冲刷失败
     *         （并补一条 DIAG-LOG-WRITE-FAILED Dev 行——§9.6 注释原文）
     */
    virtual bool flush(std::chrono::milliseconds deadline) = 0;

    /**
     * @brief 配置（§9.6 原文签名；L5：文件路径/轮转/采样参数）。
     *
     * 后置：配置在日志线程串行生效（下一条记录起）；目录/轮转阈值等即时
     * 影响写路径。默认构造配置即可运行（enabled＋空目录＝写失败计数降态，
     * 不崩不抛——装配未完成的安全语义）。
     *
     * @throws DiagnosticsError Usage（rotateFiles==0、sampleEveryDebug==0、
     *         sampleEveryByChannel 含 0 值、queueCapacity==0——配置不变量
     *         违约 fail-fast）
     */
    virtual void configure(LogSinkConfig config) = 0;
};

// =====================================================================
// LoggingPipeline（§7.3 管线实现——ILogger＋IDevLogSink＋worker 重放）
// =====================================================================

/**
 * @brief 两级日志管线实现（§7.3 LoggingPipeline——单一管线，串行化于日志
 *        线程；diagnostics 单元唯一 ILogger 实现）。
 *
 * 管线步骤（§7.3 流程图，每条记录在日志线程上顺序执行）：
 *   ①格式化：message 定长截断（4 KiB UTF-8 边界安全＋"[trunc]"标注）、
 *     关联 ID 规范化、行编码（换行转义保证行完整性——DT-LOG-2）；
 *   ②脱敏：Tier-U 内部模式过滤（0x 地址/[HASH] 64-hex/栈帧模式——NFR-REL-05
 *     "无调用栈/内部哈希"的本管线内建承载；NFR-SEC-07 全量脱敏
 *     （凭据/路径/环境变量）随 DIAG-T08 IRedactionService 接线，接线点＝
 *     本类构造的 redaction 钩子，登记于单元卡变更记录）；
 *   ③级别过滤：Tier-U 结构性只收 Error/Warning/Info（入口校验）＋Tier-D
 *     按 devMinLevel 配置；
 *   ④节流与采样：同 (code+channel) 首条全量＋每窗汇总（含 occurrences）；
 *     Debug/Trace 首/每 N/末条采样——§7.4；
 *   ⑤写入：经 ILogFileOps 写两级文件（Tier-U 行同时镜像入 Tier-D 文件——
 *     "Tier-U ⊆ Tier-D"§7.1）；flush 窗口 N 行/T 时/Error 即时——§7.3⑤。
 *
 * worker 重放（§7.5）：replayWorkerBatch 按 (workerId, task) 流序号合并；
 * 乱序缓冲至序号补齐按序落盘（"恢复全局稳定序"），收尾/关闭时对缺口补
 * EX-CHANNEL-PROTOCOL-ERROR Dev 行（"断裂补条"）。
 *
 * 失败语义（§7.6/§9.6）：写失败→计数＋DIAG-LOG-WRITE-FAILED Dev 行（节流
 * 汇总；内部行写失败不再递归补条）；队列满→丢弃最旧 Debug/Trace＋Dev 计数
 * 行；flush 超时→放弃＋DIAG-LOG-WRITE-FAILED。全部环境错误不抛、不阻塞
 * 调用方——日志非权威（§7.8），尽力而为。
 *
 * 线程安全：log/logDev/replayWorkerBatch/flush/configure 任意线程并发；
 * 内部状态由队列互斥＋日志线程串行化承载（节流/采样/轮转/重放状态仅日志
 * 线程触碰——无锁化）。析构须在调用方停止使用之后（进程级设施关闭语义）。
 */
class LoggingPipeline final : public ILogger, public IDevLogSink {
public:
    /**
     * @brief 构造（L5 装配期注入时钟与文件接缝；启动日志线程）。
     *
     * @param clock   [in] 时钟（记录时间戳/节流窗/flush 时间窗的来源；调用方
     *                持有，引用须在生命周期内有效——测试注入 ManualClock 同款）
     * @param fileOps [in] 文件操作接缝（同上；DT-LOG-3 故障注入点）
     */
    LoggingPipeline(IClock& clock, ILogFileOps& fileOps);

    /**
     * @brief 析构（进程级设施关闭：有界排空＋收尾补条＋节流/采样尾条落盘，
     *        然后停止日志线程）。
     *
     * 排空语义：以 kLogDefaultFlushDeadline 等待队列清空（超时放弃——剩余行
     * 丢弃并计数，不永久等待）；随后收尾：worker 重放缓冲的缺口补条＋按序
     * 落盘、节流窗未汇总的 occurrences 汇总行、采样"末条"尾行——全部写完
     * 后线程退出。
     */
    ~LoggingPipeline() override;

    // 禁拷贝/禁移动：拥有日志线程与队列（线程成员不可拷——单例语义同目录）。
    LoggingPipeline(const LoggingPipeline&) = delete;
    LoggingPipeline& operator=(const LoggingPipeline&) = delete;

    // ---- ILogger（§9.6 四方法——行为契约见接口注释）----
    void log(LogRecord record) override;
    void logDev(std::string_view channel, LogLevel level, std::string message,
                CorrelationIds ids = {}) override;
    bool flush(std::chrono::milliseconds deadline) override;
    void configure(LogSinkConfig config) override;

    // ---- IDevLogSink（§9.7 尾注接缝——DiagnosticsSinkImpl::reportDev 路由面）----
    /**
     * @brief 开发日志快捷入口（IDevLogSink 原文双参签名——无级别参数）。
     *
     * 后置：等价 logDev(channel, LogLevel::Debug, message, {})——reportDev
     * 语义为开发诊断快记，级别固定 Debug（可采样——高频 reportDev 由采样
     * 约束行数，§7.4）。经 DiagnosticsSinkImpl 转发的 Dev 诊断由此进入
     * Tier-D 文件（Dev 不入目录——§6.2）。
     *
     * @param channel [in] 通道 token（透传校验）
     * @param message [in] 消息原文（透传校验；脱敏归管线）
     */
    void logDev(std::string_view channel, std::string message) override;

    // ---- worker 日志重放（§7.5——P-EX-8：只定载荷语义）----
    /**
     * @brief 重放合并一个 worker 日志批（§7.5"主进程日志器按序号重放合并"）。
     *
     * 行为（(workerId, task) 流维度，串行化于日志线程）：
     *   - batch.seq == 期望序号：批内记录规范化（补注 workerId/task 关联、
     *     保留原始时间戳）后走完整管线①~⑤按序落盘；期望序号前进，随后
     *     连续就绪的缓冲批依次同法落盘（乱序到达→按序号恢复全局稳定序）；
     *   - batch.seq > 期望序号：整批缓冲（不落盘、不补条——缺口可能仍在途）；
     *     若 batch.final＝收尾批：把 [期望序号, seq) 的每个缺口补一条
     *     EX-CHANNEL-PROTOCOL-ERROR Dev 行（channel "diag/log-merge"，
     *     level Warning——采样不触及）后接受收尾批（"批次丢失→主进程补一条
     *     Dev 诊断"§7.5）；
     *   - batch.seq < 期望序号：陈旧/重复批丢弃＋Dev 计数行（幂等重放——
     *     通道重传不产生重复行）。
     *
     * @param batch [in] 批载荷（按值接管；记录为 worker 侧原文——脱敏/
     *              截断由本进程管线执行）
     *
     * @throws DiagnosticsError Usage（batch.task 五元组不 isValid——TASK-03
     *         载荷边界；批内记录违反 LogRecord 入口不变量同 log 校验）
     */
    void replayWorkerBatch(WorkerLogBatch batch);

private:
    struct Impl;
    /// pimpl：隔离队列/线程/节流表/重放缓冲与互斥细节于头文件之外（公共头
    /// 最小依赖面——R-2 纪律；实现见 src/Logging.cpp）。
    std::unique_ptr<Impl> m_impl;  ///< 实现体（日志线程与其全部串行状态）
};

}  // namespace sdurws::ird::diagnostics

#endif  // SDURWS_IRD_DIAGNOSTICS_LOGGING_HPP
