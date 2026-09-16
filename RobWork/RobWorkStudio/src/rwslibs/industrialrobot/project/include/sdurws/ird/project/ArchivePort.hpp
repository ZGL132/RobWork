/**
 * @file   ArchivePort.hpp
 * @brief  归档端口（IResultArchivePort）——任务运行结果/检查点写入
 *         `results/`·`checkpoints/` 的唯一存储入口（PRJ-T14 落位）。
 *
 * 设计依据：
 *   - units/project.md §5.6（归档端口接口原文：ArchiveRequest/
 *     ArchiveBatch/ArchiveStatus/IResultArchivePort 四方法与 begin 的
 *     校验清单）、§10.1（与 execution 的归档协作要素表——绑定原修订/
 *     引用持有/分批写入与最终发布/重投递幂等与冲突/失败即 abandon）、
 *     §10.2（项目切换后迟到结果归档流程——归档绑定原修订、不因 HEAD
 *     前进拒绝）、§3.1（组成表行：本头＝ArchiveRequest/ArchiveBatch/
 *     RunManifest/ArchiveStatus、IResultArchivePort、ArchiveSessionRef）；
 *   - 需求 TASK-03/PM-13（迟到结果归属：五元组核对归 execution，project
 *     接收已核验请求）、CON-04（部分/失败结果不作缓存命中——存储侧承接
 *     ＝D-13"无 manifest 即不完整"）、CON-02（旧结果保留为原快照历史
 *     证据——归档不因 HEAD 前进而拒绝）；
 *   - 任务契约 tasks/foundation/PRJ-T14.json acceptance 1～4。
 *
 * 背景说明（归档端口在单元间的位置）：
 *   运行完成事件（execution 侧）触发 begin→writeBatch*→finalize 序列，
 *   manifest 原子发布＝"该运行完整"的唯一标志（D-13）；任一环节失败或
 *   取消走 abandon——四种终结原因一律结束归档责任，**不存在永久等待**
 *  （§10.1"运行完成 ≠ 归档完成"）。RunRegistry 登记与完整五元组校验归
 *   execution；本端口接收**已核验**的存储请求，但仍验证项目上下文与写
 *   权限（§10.1 要素表首行）。RunManifest 类型冻结于 PersistenceFormat.hpp
 *   （§4.4.7 字段级契约——results 写入口唯一归 project，O-12）。
 *
 * P-PR-4 处置口径（任务契约 acceptance 4）：本端口调用线程按 §5.6/§9.8
 *   单侧冻结——任意线程可进入（execution 调度线程/ui 线程/测试线程），
 *   内部一律经宿主 writer 互斥串行（§9.8"submit/archive/save 可从任意
 *   线程进入（内部转串行）"）；与 RunRegistry 的对接细节（登记项映射、
 *   完成事件重复投递上限、强杀后 abandon 调用时机）待 execution 详设
 *  （WP-08-T01 P-PR-4 冻结点）定稿后二次对齐，本头不私改对端契约。
 *
 * P-PR-1 处置口径（任务契约 acceptance 4）：归档完成事件经 core
 *   IDomainEventBus 发布（core.md v0.1 §4.9/§5.8 消费基线——§3.2 消费
 *   清单行"IDomainEventBus/DomainEvent（…ResultArchived）"）；core 冻结
 *   出 diff 后按影响面增量同步，不私改 core。
 *
 * 线程模型（§9.8）：四方法全部可从任意线程调用（内部经宿主门卫＋writer
 *   互斥串行）；ArchiveSessionRef 为值语义句柄（可拷贝——同一会话可被
 *   execution 侧多个组件转手），底层会话状态的并发安全归本端口实现。
 */

#ifndef SDURWS_IRD_PROJECT_ARCHIVEPORT_HPP
#define SDURWS_IRD_PROJECT_ARCHIVEPORT_HPP

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <sdurws/ird/core/Identity.hpp>      // TaskIdentity 五元组（§3.2 消费清单）
#include <sdurws/ird/project/PersistenceFormat.hpp>  // RunManifest（§4.4.7 冻结类型）
#include <sdurws/ird/project/StoreTypes.hpp>  // StoreError（错误通道 §5.0）

namespace sdurws::ird::project {

// =====================================================================
// §5.6 归档请求与批次
// =====================================================================

/**
 * @brief 归档请求（§5.6 ArchiveRequest 原文形态）——execution 按
 *        RunRegistry 五元组核验后传入的登记信息。
 *
 * 背景说明（A8 承接）：runDir 归档位置**取自登记记录、project 不重新
 *   推导**（§10.1"归档请求绑定原项目与原修订"）——project 只校验其落
 *   在本存储的 results/ 白名单内且与本请求的 run 身份一致（§5.6 begin
 *   校验清单第 3 项）；runKind/evaluationKey 为登记透传（project 不解释，
 *   原样进 manifest——§4.4.7）。task.revision 允许是任何已提交修订
 *   （含非 HEAD 的历史修订——归档绑定原修订，HEAD 前进无关，§10.2）。
 *
 * 线程安全：纯值类型。
 */
struct ArchiveRequest {
    /// 任务身份五元组（TASK-03/ARCH §4.5；execution 已核验——project
    /// 仍校验 task.project 与本上下文一致，防跨项目写入，AT-10 反例）。
    core::TaskIdentity task{};
    /// 归档位置（登记记录原文；须等于 <项目根>/results/<run-id 规范文本>
    /// ——§4.1 results 行命名规则＋§10.1 白名单校验）。
    std::filesystem::path runDir;
    /// 任务类型 token（execution 登记透传；非空可打印 ASCII ≤128——
    /// §4.4.7 token 口径，随 manifest 持久化）。
    std::string runKind;
    /// 评估键（execution 登记透传；口径同 runKind）。
    std::string evaluationKey;
};

/**
 * @brief 批次文件条目（§5.6 ArchiveBatch 注释"{relPath, bytes}"）。
 *
 * 背景说明：bytes 为批次文件完整字节（execution 侧工件——结果/检查点
 *   数据，project 原样存储不解释，D-10 同源）；relPath 为运行目录内
 *   相对路径（§4.4.7 items[].relPath 同名同义——写入路径与 manifest
 *   声明路径一一对应）。保留名与穿越防护见 IResultArchivePort::writeBatch
 *   契约（NFR-SEC-01 消费侧：一切写入路径落在 §4.1 白名单内）。
 *
 * 线程安全：纯值类型。
 */
struct ArchiveItem {
    /// 运行目录内相对路径（'/' 分隔；禁 ".."/"."、绝对形态、保留名——
    /// 详见 writeBatch 契约）。
    std::string relPath;
    /// 文件完整字节（原样存储；长度可为 0——空文件是合法工件）。
    std::vector<std::uint8_t> bytes;
};

/**
 * @brief 分批写入请求（§5.6 ArchiveBatch 原文形态）。
 *
 * 线程安全：纯值类型。
 */
struct ArchiveBatch {
    /// 本批工件（可为空＝空批 no-op 成功——幂等重投递下的边界形态）。
    std::vector<ArchiveItem> items;
};

/**
 * @brief 归档写操作结果（§5.6 ArchiveStatus 原文形态）。
 *
 * 背景说明（错误语义二分，AGENTS §3）：ok==false 时 error 携带稳定码
 *   （环境/数据错误——disk-full/write-rejected/store-corrupt/
 *   archive-conflict 等，§10.1"写入失败/磁盘不足/进程异常报告"行）；
 *   调用方契约错误（空/失效会话句柄、内容身份不符）不走本结构——
 *   fail-fast 异常（std::invalid_argument），与草稿服务白名单同口径。
 *   失败后调用方（execution）按任务状态机处置；**失败即 abandon 释放
 *   引用**——不因"失败结果不进正式报告"而使关闭流程永久等待（A-4）。
 *
 * 线程安全：纯值类型。
 */
struct ArchiveStatus {
    /// true＝本操作已完成（批次全部就位/manifest 已发布或幂等命中）。
    bool ok = false;
    /// 失败时的稳定错误（ok==true 恒为空；ok==false 必有值）。
    std::optional<StoreError> error;
};

// =====================================================================
// §10.1 归档终结原因（abandon 的四值词表）
// =====================================================================

/**
 * @brief 归档终结原因（§5.6 abandon 注释原文：Completed | Canceled |
 *        Failed | ForceTerminated——一律结束归档责任，§10.1）。
 *
 * 背景说明（§10.1"成功/取消/失败/强制终止的结束"行）：Completed＝已完成
 *   责任终结（配 finalize 语义；不经 finalize 而以 Completed abandon 是
 *   execution 侧声明"以 abandon 终结且原因记完成"的形态——批次残留仍按
 *   未完成处置）；Canceled/Failed＝用户取消/运行失败——批次残留目录保留
 *   为"未完成"（无 manifest＝不作为正式缓存命中，CON-04；恢复诊断列出）；
 *   ForceTerminated＝进程被杀、无完成事件的强杀路径（execution 侧以
 *   Interrupted 终结登记后的收尾）。project 对四值只做责任终结＋开发
 *   诊断记录，不解释差异（当前性/报告准入归 evidence/reporting）。
 */
enum class ArchiveEndReason {
    Completed,       ///< 已完成——责任以完成终结
    Canceled,        ///< 用户取消——批次残留为"未完成"
    Failed,          ///< 运行失败——批次残留为"未完成"
    ForceTerminated, ///< 强制终止（进程被杀路径的收尾）——同上
};

/// 枚举→冻结 token（开发诊断记录用；词表＝上文四值）。
const char* toToken(ArchiveEndReason reason) noexcept;

// =====================================================================
// 归档会话句柄（ArchiveSessionRef）
// =====================================================================

/**
 * @brief 归档会话句柄（§3.1 组成表行点名的公共类型）——begin 返回、
 *        writeBatch/finalize/abandon 消费的会话凭据。
 *
 * 背景说明（生命周期与"闭包双通道归 project"，任务契约 acceptance 3）：
 *   本句柄是**值语义的不透明凭据**（可拷贝/可判空/可比较），归档期间的
 *   两条生命周期通道全部由 project 侧持有、调用方不接触：
 *     ①引用持有通道——会话自 begin 起持宿主存储上下文的在途票据
 *      （§9.7"归档会话（execution 注册的活跃运行）"引用持有者），
 *      requestClose 的排空等待覆盖在途归档：上下文存活至归档 finalize/
 *      abandon（PRJ-TX-8 的机制前提）；
 *     ②失权防护通道——会话期间的每笔写入经 §9.6 权威探测（锁失权即
 *      拒绝），迟到/失效句柄的调用 fail-fast。
 *   句柄在会话终结（finalize 成功或 abandon）后即失效：此后经其调用
 *   写方法＝调用方契约违约（fail-fast，ArchiveStatus 注释口径）。
 *
 * 生命周期与所有权：句柄共享持有底层会话状态的存活（shared_ptr 语义），
 *   但**不延长归档责任**——责任终结只由 finalize/abandon 触发；宿主
 *   存储上下文析构时强制终结全部在途会话（§9.7 析构静默终局口径），
 *   此后残留句柄同失效形态。
 *
 * 线程安全：句柄本身的拷贝/判空/比较并发安全（shared_ptr 控制块）；
 *   经句柄的端口方法调用由实现内部串行。
 */
class ArchiveSessionRef final {
public:
    /// 默认构造＝空句柄（判空 false——不可用于任何端口方法）。
    ArchiveSessionRef() noexcept = default;

    /// 判空：非空＝指向一个会话状态（不保证会话仍活跃——活跃性由
    /// 端口方法校验，失效句柄走 fail-fast）。
    explicit operator bool() const noexcept { return m_state != nullptr; }

    /// 同一会话判定（底层状态指针相等——容器/日志关联用）。
    bool operator==(const ArchiveSessionRef& o) const noexcept
    {
        return m_state == o.m_state;
    }
    bool operator!=(const ArchiveSessionRef& o) const noexcept
    {
        return !(*this == o);
    }

private:
    // 实现侧独占的构造通道：会话状态的类型形态（内部结构体）不出公共
    // 头——本句柄以类型擦除形态持有（R-2 纪律：公共头零实现细节）。
    friend class ArchiveServiceImpl;
    explicit ArchiveSessionRef(std::shared_ptr<void> state) noexcept
        : m_state(std::move(state))
    {
    }

    /// 会话状态（类型擦除；空＝默认构造的无效句柄）。
    std::shared_ptr<void> m_state;
};

// =====================================================================
// §5.6 归档端口接口
// =====================================================================

/**
 * @brief 归档端口（§5.6 原文形态）——results/checkpoints 写入口的唯一
 *        归属（O-12），供 execution 消费（ARCH §3.5 反向服务边）。
 *
 * 生命周期与所有权：实现随存储上下文创建（ProjectStore::archive() 访问
 *   器交付接口引用）；引用与上下文同生命周期（非 owning——端口是上下文
 *   的写面视图）。只读打开的实例同样提供本端口（begin 的锁面门卫拒绝
 *   ——LockHeldByOther＋PRJ-LOCK-HELD，与草稿写轨同表）。
 */
class IResultArchivePort {
public:
    /// 虚析构：经接口引用多态消费的常规保障。
    virtual ~IResultArchivePort() = default;

    /**
     * @brief 开始归档会话（§5.6 begin 原文）——登记归档预留并创建运行
     *        目录（§4.1 results 行"何时创建＝归档 begin 时"）。
     *
     * 校验清单（§5.6 begin 注释原文逐项；任一失败即整体失败、零磁盘
     * 副作用——目录创建在全部校验之后）：
     *   ①task.project == 本上下文 projectId（不符＝拒绝＋开发诊断，
     *     AT-10 反例——调用方契约违约，fail-fast 异常）；
     *   ②上下文 Active（Draining 拒绝**新** begin——§9.7 排空期不再
     *     接受新归档预留；已开始的会话不受影响，PRJ-TX-8 的存活机制）
     *     ——StoreError(ContextClosed)；只读上下文——StoreError(
     *     LockHeldByOther)；锁失权——StoreError(WriteRejected)；
     *   ③runDir 落在本存储 results/ 白名单内且与 task.run 一致（§10.1
     *   "归档位置不重新推导（A8）"——不一致即拒绝，不静默重定向；
     *     调用方契约违约 fail-fast）；
     *   ④同 runId 已有 manifest：与本次请求做身份比对（幂等 §10.1）——
     *     五元组＋runKind/evaluationKey 一致＝放行（后续 finalize 走
     *     D-14 摘要幂等）；不一致＝StoreError(ArchiveConflict)（同一
     *     run 目录绑定一份登记事实，重登记即冲突）。
     *
     * 成功后：运行目录就位、会话持宿主在途票据（§9.7）、同 runId 的
     * 并发第二会话被拒（ArchiveConflict——单写者纪律）。
     *
     * @param request [in] 归档请求（已由 execution 核验的登记信息）
     * @return 会话句柄（必非空——失败以异常表达，§5 章约定）
     *
     * @throws std::invalid_argument 调用方契约违约（①③的失败形态、
     *         task 五元组无效、runKind/evaluationKey 非 token）
     * @throws StoreError ContextClosed（Draining/Closed）/LockHeldByOther
     *         （只读上下文）/WriteRejected（失权）/ArchiveConflict（④
     *         不一致或并发重复会话）/环境码（目录创建失败——DiskFull/
     *         AccessDenied/WriteRejected）
     *
     * 线程安全：任意线程进入（门卫＋writer 互斥内部串行——§9.8；
     *   P-PR-4 单侧冻结口径）。
     */
    virtual ArchiveSessionRef begin(const ArchiveRequest& request) = 0;

    /**
     * @brief 分批写入批次文件（§5.6 writeBatch 原文"分批写（partial）"
     *        ——目录内可见但**不完整**：无 manifest 即不完整，D-13）。
     *
     * 逐条目语义（§10.1"分批写入与最终完整发布"行＋"批次级重投递"行）：
     *   - relPath 约束（调用方契约，违约 fail-fast）：非空、≤256 字节；
     *     '/' 分隔的相对路径，各分量非空且不为 "."/".."；禁绝对形态、
     *     盘符与 '\\'；末分量不得为保留临时后缀 ".ird-part" 结尾、不得
     *     为保留名 "manifest.json"（finalize 的发布名）与 Windows 设备
     *     名（NFR-SEC-01 消费侧白名单纪律）；
     *   - 目标不存在：暂存写（持久性闸门）→ 只增发布（publishNew——
     *     批次文件只增，§4.1 results 行）；
     *   - 目标已存在且摘要一致（同批重投递）→ 跳过（幂等）；
     *   - 目标已存在且摘要不一致 → ArchiveStatus{false, ArchiveConflict}
     *     ＋PRJ-ARCHIVE-CONFLICT 用户诊断（不覆盖既有文件）。
     *
     * 失权防护（§9.6②联动）：本操作开始时对锁句執行权威探测，失权即
     * 拒绝（ArchiveStatus{false, WriteRejected}＋PRJ-WRITE-AUTHORITY-
     * LOST）——归档期间的引用持有与失权防护双通道（acceptance 3）。
     *
     * @param session [in] begin 返回的会话句柄（空或已终结＝调用方契约
     *                违约，fail-fast）
     * @param batch   [in] 本批工件（可为空＝no-op 成功）
     * @return ok==true＝全部条目就位（或幂等跳过）；ok==false＝error
     *         携带稳定码（环境/冲突类——调用方按任务状态机处置）
     *
     * @throws std::invalid_argument session 失效或 relPath 违约
     *
     * 线程安全：任意线程进入（writer 互斥内部串行——与事务/草稿写同
     *   一串行化点，§9.8）。
     */
    virtual ArchiveStatus writeBatch(ArchiveSessionRef session,
                                     const ArchiveBatch& batch) = 0;

    /**
     * @brief finalize：manifest 原子发布＝"该运行完整"（§5.6 原文；
     *        D-13 唯一完整性判据——CON-04 部分/失败不作缓存命中的
     *        存储侧承接）。
     *
     * 执行序（§10.1 两段式"分批写入与最终完整发布"）：
     *   ①manifest 内容核验：taskIdentity 与会话五元组一致（不符＝
     *     fail-fast——会话绑定登记事实）；items ≥1（§4.4.7）；逐条目
     *     对照磁盘（存在＋sizeBytes＋sha256 全符）——不符＝
     *     ArchiveStatus{false, StoreCorrupt}（完整性发布义务，O-12：
     *     不发布与磁盘不符的"完整"声明）；
     *   ②manifestDigest 由 project 计算回填（"自身规范化摘要"——canonical
     *     编码归 project，§4.8；调用方该字段不参与）；
     *   ③同 run 已有 manifest：摘要比对（D-14 幂等判据）——一致＝幂等
     *     成功（不重写、不重复发布事件）；不一致＝ArchiveStatus{false,
     *     ArchiveConflict}＋用户诊断；
     *   ④原子发布 manifest.json（write-through 暂存→原子替换）＝提交点；
     *   ⑤发布 ResultArchived 事件（core IDomainEventBus——§10.2 流程；
     *     失败重试一次＋开发诊断、不影响归档完成，D-18 同源）；
     *   ⑥会话终结、释放宿主在途票据（§9.7——排空归零的触发点之一）。
     *
     * @param session  [in] 会话句柄（空或已终结＝fail-fast）
     * @param manifest [in] 运行完整性清单（§4.4.7 字段级契约；除
     *                 manifestDigest 外全部为调用方登记事实）
     * @return ok==true＝已发布或幂等命中（运行自此完整可消费——
     *         listRuns 可见）；ok==false＝error 携带稳定码
     *
     * @throws std::invalid_argument session 失效、taskIdentity 与会话
     *         不符、items 空/字段非法
     *
     * 线程安全：任意线程进入（writer 互斥内部串行）。
     */
    virtual ArchiveStatus finalize(ArchiveSessionRef session,
                                   const RunManifest& manifest) = 0;

    /**
     * @brief abandon：结束归档责任（§5.6 abandon 原文——四种终结原因
     *        一律结束；§10.1"不存在永久等待"）。
     *
     * 语义：会话终结＋释放宿主在途票据（排空归零——A-4 防饿死的保证
     * 面）；批次残留目录**保留为"未完成"**（无 manifest＝不完整，D-13/
     * CON-04——不删除，恢复口径见 §10.1"取消/失败"行）。幂等：已终结
     * 会话再 abandon 为 no-op（全路径终结的容忍形态——重复的终结通知
     * 不产生第二次副作用）。空句柄＝调用方契约违约 fail-fast。
     *
     * @param session [in] 会话句柄
     * @param reason  [in] 终结原因（四值词表——project 只记录不解释，
     *                开发诊断通道留观察面）
     *
     * @throws std::invalid_argument session 为空句柄
     *
     * 线程安全：任意线程进入；与 writeBatch/finalize 并发调用时会话
     *   状态互斥裁决（先到者定终局——后续者按幂等/失效语义处置）。
     */
    virtual void abandon(ArchiveSessionRef session, ArchiveEndReason reason) = 0;
};

}  // namespace sdurws::ird::project

#endif  // SDURWS_IRD_PROJECT_ARCHIVEPORT_HPP
