/**
 * @file   StoreLock.hpp
 * @brief  写锁（StoreLock）——存储写权限的 RAII 独占句柄封装（SA-17 落地）。
 *
 * 设计依据：
 *   - units/project.md §9.1（Windows 锁机制与句柄封装：机制选择 D-02＝对
 *     lock 文件的**独占打开句柄**，内核原子裁决；崩溃释放＝OS 关闭句柄；
 *     句柄封装 StoreLock（RAII）——构造＝获取＋写 PID 记录，析构＝关闭；
 *     内部心跳线程）、§9.2（第二实例读取 PID 而不破坏互斥）、§9.4（锁
 *     文件生命周期——文件永不删除重建 D-03，原地重写固定宽度记录，心跳
 *     周期 30 s，残留心跳过期不触发接管）、§9.5（三类失败的不同诊断）、
 *     §9.6（失权写入防护三道防线）、§9.8（线程模型——心跳线程/写通道）；
 *   - 需求 PM-07（只读打开：锁被持时 writable=false＋PID 提示，第二写者
 *     不阻塞等待）、PM-08（崩溃与恢复：锁残留报告）；SA-17（架构决策：
 *     写权限唯一依据＝OS 排他句柄，心跳/PID 仅诊断）；
 *   - 任务契约 tasks/foundation/PRJ-T03.json acceptance 1/2/4（PRJ-TX-7
 *     锁用例；PRJ-TX-13 锁部分——写前权威检查与失权三道防线；锁诊断经
 *     IDiagnosticsSink 产出 core::DiagnosticRecord）。
 *
 * 背景说明（权限即锁——本类是"谁可写"这一问题的唯一事实来源）：
 *   本软件的写权限模型不是文件系统 ACL、不是 advisory 标志位，而是
 *   "本进程是否持有 lock 文件的独占打开句柄"这一 OS 级事实（SA-17）。
 *   由此推出三条铁律，全部结构化在本类中：
 *   1. **获取的原子性**归内核——两个实例同时获取，CreateFileW 的共享
 *      模式裁决天然只产生一个胜者（D-02，§9.9 流程图）；
 *   2. **释放的即时性**归 OS——进程崩溃/退出时句柄被 OS 关闭，排他性
 *      即时消失，无超时无接管仲裁（心跳陈旧**不**触发任何接管——D-03）；
 *   3. **失权的不可逆性**归状态机——句柄一旦异常失效（探测失败/写冲突
 *      等三码错误），本对象锁存 LostWrite，后续写一律拒绝
 *      （PRJ-WRITE-AUTHORITY-LOST），不得凭"心跳还在跳"自证清白。
 *
 * 失权三道防线（§9.6，acceptance 2 的具名落点）：
 *   ① 状态机——写入口 requireWriteAuthority() 先查对象状态（未获取＝
 *      只读上下文；已释放；已失权），非 Active 一律拒绝（句柄由 RAII
 *      独占，"句柄悬空"态结构性不存在：关闭路径同步置位状态）；
 *   ② 权威探测——每笔写事务开始时对 lock 句柄执行 GetHandleInformation
 *      轻量探测，失败即失权（覆盖句柄被外部异常关闭的实现层边缘）；
 *   ③ I/O 分类——任何写操作遇到 ERROR_SHARING_VIOLATION /
 *      ERROR_INVALID_HANDLE / ERROR_ACCESS_DENIED，经 classifyIoError()
 *      分类并锁存 LostWrite（纵深防御：句柄异常失效、上下文结束后的
 *      迟到写）。
 *
 * 范围声明（本原语与存储上下文的边界）：ProjectStore 上下文状态机
 * （Active/Draining/Closed，§9.7）与"句柄关闭和在途事务同步"（writer
 * 通道串行化）归 PRJ-T08 的 ProjectStoreImpl——本类只承载锁本身的获取/
 * 持有/失权/释放，是 PRJ-T08 写路径的权威门卫（每个写入口调用
 * requireWriteAuthority()，§9.6 ①）。
 *
 * 线程约束：本类**内部互斥**（心跳线程与调用方线程在写入口汇合——§9.8
 * writer 互斥在锁原语层的最小形态），同一实例的公开方法可从多线程调用；
 * 但语义上每存储上下文一个实例（§9.1"持锁范围：存储上下文存续期间持续
 * 持有"），不设计多上下文共享。
 *
 * 错误语义：调用方契约违约（空 ILockOps/空路径/负心跳周期）→ 异常
 * fail-fast；环境错误（共享冲突/权限不足/其他 OS 错误）→ 值语义的获取
 * 结果（AcquireStatus），不抛异常——锁竞争与介质故障是**预期业务分支**
 * （§9.5 三类失败各有诊断路径），不是编程错误。
 */

#ifndef RWS_IRD_PROJECT_SRC_WIN32_STORELOCK_HPP
#define RWS_IRD_PROJECT_SRC_WIN32_STORELOCK_HPP

#include "ILockOps.hpp"

#include <sdurws/ird/project/StoreTypes.hpp>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

namespace sdurws::ird::project::win32 {

/// 心跳重写周期（§9.4：心跳周期 30 s，由 StoreLock 内心跳线程执行——
/// 纯诊断，非权限判据）。公开常量＝文档口径与实现共用同一事实来源；
/// 构造参数可注入更小值（测试观察心跳行为），0 表示禁用心跳线程。
inline constexpr std::chrono::milliseconds kLockHeartbeatPeriod{30000};

// ---- 固定宽度锁记录编码（§9.2/§9.4）----------------------------------
// 记录为三行定宽文本；"固定宽度"是撕裂读容忍的前提（读取方按偏移定位
// 字段，心跳重写不改变文件长度——D-03"原地重写"的观测量）。编码布局：
//   "pid="<10 位十进制>"\n"          ——15 字节
//   "host="<64 字节空白填充>"\n"     ——70 字节
//   "hb="<24 字节 ISO-8601 毫秒UTC>"\n" ——28 字节
// 合计 113 字节。只增字段不扩宽：PID 十进制最长 10 位（uint32 上限
// 4294967295 恰 10 位），超宽主机名截断——记录是诊断数据，截断可接受。

constexpr std::size_t kLockPidDigits = 10;    ///< PID 十进制位宽（uint32 上限）
constexpr std::size_t kLockHostWidth = 64;    ///< 主机名字段字节宽（含填充）
constexpr std::size_t kLockHeartbeatWidth = 24;  ///< ISO-8601 带毫秒 UTC 宽度
/// 单条记录总字节数（三行定宽之和；重写前后长度恒等）。
constexpr std::size_t kLockRecordSize = (4 + kLockPidDigits + 1)
                                      + (5 + kLockHostWidth + 1)
                                      + (3 + kLockHeartbeatWidth + 1);

/**
 * @brief 获取时的自我身份（写入锁记录的 PID 记录内容，§9.1"构造＝获取
 *        ＋写 PID 记录"）。
 *
 * initialHeartbeatUtc 由**调用方时钟**供给（装配点持时钟注入——诊断
 * 字段非权限判据，交由调用方有利于测试构造陈旧心跳验证 D-03；心跳线程
 * 启动后的每次重写改用本类内置时钟）。
 */
struct LockSelfRecord {
    /// 本进程 PID；单位＝OS PID。StoreLock 不代取（ GetCurrentProcessId
    /// 的读取点归装配方——身份来源单一，测试可注入假 PID 驱动 isSelf 判定）。
    std::uint32_t pid = 0;
    /// 本机主机名（诊断呈现）；超宽截断到 kLockHostWidth。
    std::string host;
    /// 初始心跳时间戳，ISO-8601 UTC 带毫秒（kLockHeartbeatWidth 定宽）；
    /// 心跳线程每次重写以当时系统时间覆盖。
    std::string initialHeartbeatUtc;
};

/// 获取结果的三值状态（§9.5 三类失败中锁相关的两类＋成功）。
enum class AcquireStatus {
    Held,         ///< 获取成功：本对象独占持有句柄，写权限成立
    HeldByOther,  ///< 锁被其他实例持有（ERROR_SHARING_VIOLATION）——按 PM-07
                  ///< 转只读路径，不阻塞等待；lastHolder() 携带持有者 PID
    OsError       ///< 其他 OS 错误（权限不足/介质只读等，§9.5 前两类；
                  ///< 原始码在 acquireOsError()，StoreErrorCode 映射归 T08）
};

/// 读取锁文件记录的结果（readHolderRecord 的返回值）。
struct LockRecordRead {
    bool ok = false;        ///< true＝读取本身成功（内容可能无法解析）
    unsigned long osError = 0;  ///< 失败时的 Win32 原始错误码（如文件不存在）
    LockHolderRecord record;    ///< 解析出的记录；撕裂/空文件→字段为零值
    bool parsed = false;        ///< 是否至少解析出一个字段（撕裂读＝false）
};

/**
 * @brief 读取向-other-实例-可见的锁持有者记录（§9.2 第二实例流程②）。
 *
 * 以只读共享方式打开（允许与持有者的写访问共存），读取后**立即关闭**
 * （§9.2："读句柄随即关闭，不长期持有；PID 信息已取"）。内容按撕裂
 * 容忍解析（§9.2"固定宽度记录，容忍撕裂读——心跳仅诊断"）。
 *
 * @param ops     [in] 文件操作实现（非 owning；不得为空——空接缝＝调用方
 *                契约违约，fail-fast）
 * @param lockPath [in] 锁文件路径（宽字符）
 * @return ok==false 时（如文件尚不存在）record 为全零值——调用方按
 *         "无持有者信息"处置（轮询重试归调用方，本函数不等待）
 */
LockRecordRead readHolderRecord(ILockOps* ops, const std::wstring& lockPath);

/**
 * @brief 任意 UTC 时刻的 ISO-8601 带毫秒文本（定宽 24 字节）。
 *
 * 格式 YYYY-MM-DDTHH:MM:SS.mmmZ——本单元时间戳的**唯一编码形式**（PRJ-T11
 * 增量：确认凭据 confirmedAtUtc 的 time_point→磁盘文本转换载体——§4.4.4
 * ConfirmationCredentialRecord"互转归确认流编排"的落位点），锁记录心跳
 * 与命令提交时间同用此格式。确定性说明：结果随时钟输入变化，**不得**参与
 * 任何内容寻址/身份计算（CON-05）——它只进诊断与留痕展示通道。
 *
 * @param tp [in] UTC 时刻（system_clock 时间点；精度高于毫秒时截断到毫秒）
 * @return 定宽 24 字节 ISO-8601 文本（gmtime_s 失败的实现层边缘回退为
 *         全零时刻 "1970-01-01T00:00:00.000Z"——与 utcNowIsoMilli 同口径）
 */
std::string formatIsoMilli(std::chrono::system_clock::time_point tp);

/**
 * @brief 当前 UTC 时刻的 ISO-8601 带毫秒文本（定宽 24 字节）。
 *
 * 格式 YYYY-MM-DDTHH:MM:SS.mmmZ——锁记录心跳字段的唯一编码形式；初始
 * 心跳由调用方经本函数（或自有时钟）取得后传入 LockSelfRecord。
 * 确定性说明：本函数结果随时钟变化，**不得**参与任何内容寻址/身份计算
 * （CON-05）——它只进诊断通道。
 */
std::string utcNowIsoMilli();

/**
 * @brief 存储写锁——RAII 独占句柄（SA-17 的类形态）。
 *
 * 生命周期与所有权：构造即获取（§9.1"构造＝获取＋写 PID 记录"）——
 * 获取被拒不是异常而是正常业务分支（PM-07：第二写者转只读，不阻塞），
 * 对象进入只读上下文形态（held()==false，lastHolder() 携带持有者信息，
 * 一切写入口拒绝）；析构＝释放（关闭句柄，OS 层面写权限即时失效）。
 * 一实例至多一次获取（构造即获取，无二次 acquire——重新获取＝构造新
 * 实例，状态机无复用歧义）。
 *
 * 诊断产出（acceptance 4：经 IDiagnosticsSink 适配器产出
 * core::DiagnosticRecord——注入式先行，不直链 diagnostics 库）：
 *   - 获取被拒（HeldByOther）→ report("PRJ-LOCK-HELD")，上下文文本含
 *     持有 PID/主机（§9.2"项目被 PID=<n> 持有"提示的数据面）；
 *   - 失权（防线②③触发）→ report("PRJ-WRITE-AUTHORITY-LOST")；
 *   - 接管时的残留记录（§9.4/PM-08）→ reportDev("project/lock-recovery",
 *     "接管…残留 pid=…")；
 *   - 心跳写失败（非失权类错误）→ reportDev（心跳是诊断通道，失败只记
 *     开发诊断；失权类错误走防线③分类）。
 */
class StoreLock {
public:
    /**
     * @brief 构造即获取（RAII；§9.1）。
     *
     * 执行序：契约校验 → 独占打开（内核原子裁决 D-02）→ 失败分支：
     * 共享冲突＝HeldByOther（读持有者记录＋PRJ-LOCK-HELD），其他错误＝
     * OsError（原始码留存，§9.5 映射归 T08）；成功分支：读残留记录作
     * 恢复诊断（§9.4 接管）→ 写自我 PID 记录（固定宽度）→ 启动心跳
     * 线程（period>0 时）。
     *
     * @param ops             [in] 文件操作实现（非 owning；空＝契约违约抛
     *                        std::invalid_argument）
     * @param sink            [in] 诊断 sink（非 owning；可空＝丢弃诊断，
     *                        §5.1"可空：退化为开发诊断"）
     * @param lockPath        [in] 锁文件路径（宽字符；空＝契约违约）
     * @param self            [in] 自我身份（PID 记录内容，见 LockSelfRecord）
     * @param heartbeatPeriod [in] 心跳周期；默认 kLockHeartbeatPeriod（30 s，
     *                        §9.4）；0＝禁用心跳线程（测试/无诊断场景）；
     *                        负值＝契约违约抛 std::invalid_argument
     *
     * @throws std::invalid_argument ops 为空、lockPath 为空或周期为负
     *         （调用方契约违约，fail-fast——不产生半构造对象）
     */
    StoreLock(ILockOps* ops, IDiagnosticsSink* sink,
              const std::wstring& lockPath, const LockSelfRecord& self,
              std::chrono::milliseconds heartbeatPeriod = kLockHeartbeatPeriod);

    /// 析构＝释放（§9.1"析构＝关闭"）；join 心跳线程； noexcept（RAII 惯例）。
    ~StoreLock();

    // 拷贝/移动禁用：独占句柄与心跳线程的所有权唯一（RAII 语义不可分身）。
    StoreLock(const StoreLock&) = delete;
    StoreLock& operator=(const StoreLock&) = delete;
    StoreLock(StoreLock&&) = delete;
    StoreLock& operator=(StoreLock&&) = delete;

    /// @brief 当前获取状态（构造后恒定——本类无二次获取）。
    AcquireStatus status() const noexcept;

    /// @brief 是否持有写权限（等价 status()==Held 且未失权；写入口判据①）。
    bool held() const noexcept;

    /// @brief 最后观测的持有者记录：被拒时＝他方持有者（PRJ-LOCK-HELD 载荷）；
    ///        持有时＝自我身份。撕裂读容忍——字段可能为零值。
    LockHolderRecord lastHolder() const;

    /// @brief OsError 分支的 Win32 原始错误码（§9.5 前两类失败的检测点数据；
    ///        Held/HeldByOther 时为 0）。
    unsigned long acquireOsError() const noexcept;

    /// @brief 锁文件路径（构造参数原样；供 T08 关联诊断上下文）。
    const std::wstring& lockPath() const noexcept;

    /**
     * @brief 写前权威检查——§9.6 防线①＋②；全部写入口的统一门卫。
     *
     * 检查序（短路）：状态机（未获取/已释放/已失权→拒绝并产出对应稳定
     * 诊断）→ 权威探测（GetHandleInformation 失败→锁存失权→拒绝）。
     * **心跳内容不是判据**（SA-17 承接：本方法零心跳读取——结构性保证）。
     * 拒绝时经 sink 产出：只读上下文（从未获取）→ PRJ-LOCK-HELD（含持有
     * PID）；已释放/已失权 → PRJ-WRITE-AUTHORITY-LOST。
     *
     * @return true＝写权限成立（可执行本笔写事务）；false＝拒绝（写入口
     *         必须放弃写入并按诊断处置——T08 映射 StoreErrorCode 后上抛）
     */
    bool requireWriteAuthority();

    /**
     * @brief 心跳/接管写入——锁文件的原地重写入口（§9.4）。
     *
     * 先经 requireWriteAuthority 门卫（防线①②），再以当前系统时间重写
     * 固定宽度记录（truncate→write→flush，经 ILockOps）。心跳线程周期
     * 调用本逻辑（私有锁内路径）；调用方亦可显式调用（接管后立即刷新）。
     * 写失败经防线③分类：三码错误锁存失权；非三码（如磁盘满）只记开发
     * 诊断——心跳丢失无害（D-03：心跳仅诊断），不因此失权。
     *
     * @return true＝重写成功；false＝被门卫拒绝或写入失败（诊断已产出）
     */
    bool rewriteHeartbeat();

    /**
     * @brief I/O 错误分类——§9.6 防线③；写入口捕获的内核错误在此裁决。
     *
     * ERROR_SHARING_VIOLATION / ERROR_INVALID_HANDLE / ERROR_ACCESS_DENIED
     * → 锁存 LostWrite＋PRJ-WRITE-AUTHORITY-LOST（后续写全拒）；其他错误
     * 码不处理（环境错误归各写入口自己的诊断路径，如 disk-full）。
     *
     * @param winError [in] Win32 GetLastError() 原始码（无符号系统码）
     */
    void classifyIoError(unsigned long winError);

    /**
     * @brief 显式释放（§9.7 requestClose→Closed 的锁释放落点；析构同路径）。
     *
     * 关闭句柄（OS 层面写权限即时失效）＋停止并 join 心跳线程。幂等；
     * 释放后本对象进入"已释放"态——requireWriteAuthority 拒绝并以
     * PRJ-WRITE-AUTHORITY-LOST 诊断（§5.0 映射：context-closed → 该码）。
     */
    void release();

private:
    /**
     * @brief 锁内分类核（classifyIoError 的实现核；调用方须已持 m_mutex）。
     *
     * 与公开版本分体是死锁结构约束：rewriteHeartbeatLocked（已持锁）
     * 的写失败路径必须走本核——公开版内部的二次加锁在非递归互斥上
     * 触发 EDEADLK（"resource deadlock would occur"，实施期实测）。
     */
    void classifyIoErrorLocked(unsigned long winError);
    /**
     * @brief 锁内重写心跳（rewriteHeartbeat 的实现核；调用方须已持 m_mutex）。
     *
     * 心跳线程与显式调用汇合于此——单锁串行（§9.8 writer 互斥的最小形态），
     * 保证两条通道的记录重写不交错。
     */
    bool rewriteHeartbeatLocked();

    /// @brief 锁内门卫核（requireWriteAuthority 的实现核；须已持 m_mutex）。
    bool requireWriteAuthorityLocked();

    /// @brief 产出 PRJ-LOCK-HELD 用户诊断（上下文含持有 PID/主机——§9.2）。
    void reportLockHeldLocked() const;

    /// @brief 产出 PRJ-WRITE-AUTHORITY-LOST 用户诊断（detail 说明失权途径）。
    void reportAuthorityLostLocked(const std::string& detail) const;

    /// @brief 开发诊断（sink 可空时丢弃）。
    void reportDev(const std::string& channel, const std::string& message) const;

    mutable std::mutex m_mutex;  ///< 保护以下全部状态（心跳线程/调用方汇合点）
    std::condition_variable m_cv;  ///< 心跳线程停止通知
    ILockOps* m_ops;             ///< 文件操作实现（非 owning；生存期注入方保证）
    IDiagnosticsSink* m_sink;    ///< 诊断 sink（非 owning；可空＝丢弃）
    std::wstring m_lockPath;     ///< 锁文件路径（构造参数）
    HANDLE m_handle;             ///< 独占句柄（nullptr＝未获取/已释放——RAII 唯一
                                 ///< 释放点：release()；"句柄悬空"态不存在，§9.6①）
    AcquireStatus m_status;      ///< 获取结果（构造后恒定）
    unsigned long m_acquireOsError;  ///< OsError 分支原始码（其余为 0）
    LockHolderRecord m_self;     ///< 自我身份（持有期由心跳重写刷新心跳字段）
    LockHolderRecord m_lastHolder;  ///< 最后观测持有者（被拒＝他方；持有＝自身）
    bool m_everHeld;             ///< 是否曾获取成功（区分"只读上下文"与"已释放"
                                 ///< ——两者写拒绝的诊断码不同：PRJ-LOCK-HELD 对
                                 ///< PRJ-WRITE-AUTHORITY-LOST）
    bool m_authorityLost;        ///< 失权锁存（防线②③置位；单向——失权不可逆，
                                 ///< 重获权限＝构造新实例，SA-17 口径）
    std::chrono::milliseconds m_heartbeatPeriod;  ///< 心跳周期（0＝禁用）
    bool m_heartbeatStop;        ///< 心跳线程停止标志（m_cv 配对）
    std::thread m_heartbeat;     ///< 心跳线程（持锁期存在；release/dtor join）
};

}  // namespace sdurws::ird::project::win32

#endif  // RWS_IRD_PROJECT_SRC_WIN32_STORELOCK_HPP
