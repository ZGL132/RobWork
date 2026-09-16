/**
 * @file   ArchiveServiceImpl.hpp
 * @brief  归档端口实现（ArchiveServiceImpl）——§5.6 IResultArchivePort
 *         契约的实现载体：begin 预留与运行目录创建/分批写入（只增＋
 *         批次幂等）/finalize（manifest 原子发布＝完整判据＋D-14 摘要
 *         幂等与冲突拒绝）/abandon（全路径责任终结）＋会话引用持有
 *         （私有实现头，不出 include/——R-2 纪律）。
 *
 * 设计依据：
 *   - units/project.md §5.6（归档端口四方法契约与 begin 校验清单）、
 *     §10.1（归档协作要素表——绑定原修订/分批写入与最终发布/重投递
 *     幂等与冲突/失败即 abandon/当前性与历史归档）、§10.2（迟到结果
 *     归档流程——finalize→ResultArchived 事件→引用释放→Closed）、
 *     §9.6～§9.8（失权防护/writer 互斥/在途票据/线程模型——P-PR-4
 *     单侧冻结的实现面）、§4.1 results 行（目录命名/可变性/manifest
 *     原子替换）、§4.4.7（RunManifest 字段级契约——manifestDigest
 *     ＝幂等判据）；
 *   - 需求 TASK-03/PM-13（迟到结果归属）、CON-04（部分/失败不作缓存
 *     命中——D-13 存储侧承接）、CON-02（归档不因 HEAD 前进拒绝）；
 *   - 任务契约 tasks/foundation/PRJ-T14.json acceptance 1～4。
 *
 * 背景说明（实现形态——为什么薄、为什么依赖宿主）：
 *   与 DraftServiceImpl 同款：本类**不持有项目状态**。身份事实（projectId
 *   归属校验）、互斥与门卫（m_lifecycleMutex/m_writerMutex——§9.8
 *   writer 互斥＝归档写与事务/草稿写的同一串行化点，P-PR-4 单侧冻结）、
 *   在途票据（acquireInFlight——§9.7"归档会话"引用持有者）、事件总线
 *  （ResultArchived 发布——§3.2 消费清单）全部来自宿主。磁盘操作经
 *   AtomicFile 原语（writeThrough 持久性闸门＋publishNew 只增发布＋
 *   replaceFile 原子替换——§7.2 的保证面）。本类的自身可变状态只有
 *   会话注册表（m_sessions），互斥内部实现。
 *
 * 会话生命周期（acceptance 3"闭包双通道归 project"的机制化）：
 *   begin 成功即建立会话状态（shared_ptr<ArchiveSessionState>）：
 *     ①引用持有通道——状态内持宿主在途票据（inFlight），存活至终结
 *      （finalize/abandon/宿主析构终结）——requestClose 排空等待覆盖
 *      在途归档（PRJ-TX-8"上下文存活至归档 finalize、完成后才 Closed"
 *      的结构前提）；
 *     ②失权防护通道——每笔写操作经 StoreLock::requireWriteAuthority
 *      权威探测（§9.6②），失权即拒绝。
 *   会话注册表以"状态原始指针→状态"登记：终结时摘除登记＋置 ended，
 *   残留句柄的后续调用 fail-fast（调用方契约违约——ArchivePort.hpp
 *   错误语义口径）。宿主析构时（~ProjectStoreImpl 先 finishClose(false)
 *   置 Closed）本类析构强制终结全部会话——票据删除器观测 Closed 不再
 *   触发排空回调（析构静默终局，口径与宿主一致）。
 *
 * 实现口径登记（DTB §5.4，§5.6/§10.1 未定义判据的最小收敛）：
 *   ①begin 的 manifest 存在性比对＝**身份比对**（五元组＋runKind/
 *     evaluationKey 逐字段相等）——§5.6"同 runId 已有 manifest：与本次
 *     请求摘要比对"的请求侧落点：请求不携摘要（摘要在 finalize 侧才
 *     可计算），语义等价的幂等判据前置到身份面；内容级摘要幂等仍由
 *     finalize 的 D-14 比对承载。同一 run 目录绑定一份登记事实：身份
 *     不符（含同 runId 不同 attempt）＝ArchiveConflict。
 *   ②runDir 白名单判据＝weakly_canonical(runDir) 与 weakly_canonical(
 *     <项目根>/results/<task.run 规范文本>) 相等——白名单内＋绑定
 *     run-id 一并落在一个判据（A8：不重新推导——一致即通过，不一致即
 *     拒绝，不静默重定向）。
 *   ③manifestDigest 的被摘要对象＝**除 manifestDigest 外全部字段的
 *     canonical 编码**（实现为：digest 置空串后 codec::dump 的字节经
 *     contentVersionOf——CR-02 唯一哈希路径；空字段字节参与摘要，两侧
 *     计算同一函数故判定自洽）。编码归 project（§4.8），finalize 时
 *     由 project 计算回填，调用方该字段不参与。
 *   ④finalize 的磁盘核验＝逐 items 对照（存在＋sizeBytes＋sha256）——
     manifest 是"完整"的声明（D-13），不发布与磁盘不符的声明（O-12
     完整性发布义务）；不符＝StoreCorrupt（数据侧）。
 *   ⑤批次临时文件后缀 ".ird-part" 为保留名（relPath 校验拒绝该后缀），
 *     避免"调用方合法文件名与暂存名碰撞"的第二失败面。
 *   ⑥幂等 finalize 命中不重复发布 ResultArchived（进程内总线恰一次
 *     语义——core Events.hpp"至少一次/进程内恰一次"；重复投递由
 *     execution 完成事件的 at-least-once 承接，存储侧不放大）。
 *   ⑦已终结句柄的 writeBatch/finalize 调用＝fail-fast（invalid_argument
 *     ——ArchiveStatus 注释口径：调用方契约错误不走返回值轨）；abandon
 *     对已终结会话幂等 no-op（全路径终结的容忍形态）。
 *
 * P-PR-1 处置（acceptance 4）：ResultArchived 事件经 core IDomainEventBus
 *   （Events.hpp v0.1 基线——DomainEvent::make 工厂，零 core 修改）；
 *   身份类型消费 core 公共契约（Identity.hpp），冻结 diff 后增量同步。
 * P-PR-4 处置（acceptance 4）：调用线程不设限（任意线程进入——execution
 *   调度线程模型未定稿前不预设）；全部变更性文件操作经宿主 writer 互斥
 *   （§9.8"submit/archive/save 可从任意线程进入（内部转串行）"）；
 *   RunRegistry 对接细节（登记项映射/重复投递上限/强杀 abandon 时机）
 *   待 execution 详设定稿后二次对齐，本实现不私改对端契约。
 *
 * 线程模型（§9.8）：四方法任意线程进入；锁序＝宿主 m_lifecycleMutex →
 *   宿主 m_writerMutex → m_sessionsMutex（不反向）；在途票据的释放
 *   （删除器取宿主 lifecycle 锁）一律在本类锁外执行（锁序纪律的兑现：
 *   删除器不得在 writer/sessions 锁内触发）。
 */

#ifndef SDURWS_IRD_PROJECT_SRC_ARCHIVESERVICEIMPL_HPP
#define SDURWS_IRD_PROJECT_SRC_ARCHIVESERVICEIMPL_HPP

#include <filesystem>
#include <map>
#include <mutex>
#include <string>

#include <sdurws/ird/project/ArchivePort.hpp>
#include <sdurws/ird/project/PersistenceFormat.hpp>

#include "win32/AtomicFile.hpp"

namespace sdurws::ird::project {

// 前向声明：宿主存储上下文（完整定义于 ProjectStoreImpl.hpp——friend
// 访问收敛，本头不 include 宿主私有头，防环）。
class ProjectStoreImpl;

/**
 * @brief manifestDigest 计算（单元内自由函数——私有头，不出 include/；
 *        实现口径③：digest 置空后 canonical 编码的字节经 contentVersionOf
 *        ——CR-02 唯一哈希路径；hex 形态＝剥 "cv-" 前缀的 64 小写 hex，
 *        §4.4.7 字段口径）。端口 finalize 与测试独立复算共用同一函数
 *        （幂等判据两侧自洽的单一事实源）。
 *
 * @param content [in] 待摘要内容（其余字段全部参与；入参的 manifestDigest
 *                不参与——本函数自行置空）
 * @return 64 个小写十六进制字符
 */
[[nodiscard]] std::string computeManifestDigestHex(const RunManifest& content);

/**
 * @brief 归档会话状态（ArchiveSessionRef 的共享载体；实现私有形态——
 *        公共句柄以类型擦除持有，R-2）。
 *
 * 背景说明：ended/finalized 是会话状态机的两个终局面（finalized＝以
 *   完整发布终结；ended without finalized＝abandon 终结）；inFlight 是
 *   宿主在途票据（§9.7 引用持有通道——存活至终结）。纯数据聚合，无
 *   互斥（并发裁决归服务层的 m_sessionsMutex＋宿主锁）。
 */
struct ArchiveSessionState {
    /// 会话绑定的任务五元组（begin 登记事实——finalize 的 taskIdentity
    /// 一致性判据）。
    core::TaskIdentity task{};
    /// 运行目录（白名单校验通过的编址——批次/manifest 全部落此）。
    std::filesystem::path runDir;
    /// true＝会话已终结（finalize 或 abandon；此后写调用 fail-fast）。
    bool ended = false;
    /// true＝以 manifest 发布终结（与 ended 同时置位——终结形态记录）。
    bool finalized = false;
    /// 宿主在途票据（§9.7；非空＝引用持有通道活跃；终结时释放——锁外）。
    std::shared_ptr<void> inFlight;
};

/**
 * @brief §5.6 IResultArchivePort 契约的实现（final——不再派生；消费者
 *        只面向公共抽象，本类型不出公共头——R-2）。
 *
 * 生命周期与所有权：由 ProjectStoreImpl 构造体末尾创建（宿主引用构造
 *   完成后装配、五端口中构造序最末——§5.1 访问器挂载纪律）、unique_ptr
 *   持有、经 ProjectStore::archive() 以接口引用交付；与上下文同生命
 *   周期。析构强制终结全部在途会话（引用持有通道随宿主消亡收口——
 *   票据删除器观测宿主已 Closed，不触发排空回调）。
 */
class ArchiveServiceImpl final : public IResultArchivePort {
public:
    /**
     * @brief 构造归档端口实现（仅宿主可达——装配于宿主构造末尾）。
     *
     * @param host [in] 宿主存储上下文（非 owning 引用——提供身份事实/
     *             锁/在途票据/事件总线/诊断 sink；生存期覆盖本对象）
     */
    explicit ArchiveServiceImpl(ProjectStoreImpl& host);

    /**
     * @brief 析构＝强制终结全部在途会话（abandon 语义——责任随宿主
     *        消亡收口，无泄漏引用；§9.7 析构静默终局口径）。
     */
    ~ArchiveServiceImpl() override;

    // ---- IResultArchivePort 契约（§5.6；逐方法契约见公共头注释） ----

    [[nodiscard]] ArchiveSessionRef begin(const ArchiveRequest& request) override;
    [[nodiscard]] ArchiveStatus writeBatch(ArchiveSessionRef session,
                                           const ArchiveBatch& batch) override;
    [[nodiscard]] ArchiveStatus finalize(ArchiveSessionRef session,
                                         const RunManifest& manifest) override;
    void abandon(ArchiveSessionRef session, ArchiveEndReason reason) override;

    /**
     * @brief 在途会话数观测面（同单元测试消费——排空计数的间接观测；
     *        不进公共头，revisionIndex() 同款先例）。
     */
    [[nodiscard]] std::size_t activeSessionCount() const;

private:
    /**
     * @brief 会话句柄解析（writeBatch/finalize 的公共前置）。
     *
     * 校验链：空句柄→invalid_argument；注册表查无（未知句柄）→
     * invalid_argument；ended→invalid_argument（失效句柄——调用方契约
     * 违约 fail-fast，实现口径⑦）。
     *
     * @param session [in] 待解析句柄
     * @return 活跃会话状态（shared_ptr——解析后状态存活不受并发终结
     *         影响；终结标志以注册表为准）
     *
     * @throws std::invalid_argument 空/未知/已终结句柄
     */
    [[nodiscard]] std::shared_ptr<ArchiveSessionState> resolveActiveSession(
        const ArchiveSessionRef& session) const;

    /**
     * @brief 失权防护探测（§9.6②——每笔写操作开始时对锁句柄的权威
     *        探测；与 executeCommit/草稿门卫第③道同源）。
     *
     * @param action [in] 动作名（进入开发诊断 detail——"begin"/"writeBatch"等）
     * @return true＝权威有效；false＝失权（PRJ-WRITE-AUTHORITY-LOST
     *         用户级诊断已产出，调用方按各自错误通道处置）
     */
    [[nodiscard]] bool requireWriteAuthority(std::string_view action) const;

    /**
     * @brief relPath 白名单校验（NFR-SEC-01 消费侧——一切写入路径落在
     *        §4.1 results 白名单内的归档面防线；规则见 writeBatch 公共
     *        头契约与文件头实现口径⑤）。
     *
     * @param relPath [in] 批次条目相对路径
     *
     * @throws std::invalid_argument 违反白名单（不静默净化——净化会让
     *         调用方误以为写到了它指定的路径下）
     */
    static void validateRelPath(const std::string& relPath);

    /**
     * @brief manifest 项字段的调用方契约校验（relPath 白名单＋sha256
     *        ＝64 小写 hex＋sizeBytes/登记串非空 token——finalize 前置）。
     *
     * @param manifest [in] 待核验清单（§4.4.7 字段级）
     *
     * @throws std::invalid_argument 任一字段违约
     */
    static void validateManifestFields(const RunManifest& manifest);

    /**
     * @brief 会话终结的公共收尾（finalize 成功/幂等命中/abandon 共用）。
     *
     * 执行序：m_sessionsMutex 下置 ended（＋finalized 标志）→摘除注册
     * 表登记→锁外释放在途票据（删除器取宿主 lifecycle 锁——锁序纪律：
     * 不在本类锁内触发；释放可能使排空归零并完成关闭，PRJ-TX-8 的
     * "完成后才 Closed"触发点）。
     *
     * @param state     [in] 终结的会话状态
     * @param finalized [in] true＝以完整发布终结（区别于 abandon 终结）
     */
    void endSession(const std::shared_ptr<ArchiveSessionState>& state,
                    bool finalized);

    /**
     * @brief ResultArchived 事件发布（§10.2 流程——finalize 之后；
     *        D-18 同源：失败重试一次＋开发诊断、不抛出、不影响归档
     *        完成事实）。总线可空＝跳过（§5.1 装配口径）。
     *
     * @param task [in] 已终结会话的五元组（事件载荷——core
     *             ResultArchivedPayload）
     */
    void publishResultArchived(const core::TaskIdentity& task) const noexcept;

    /**
     * @brief 开发诊断上报（宿主 sink 的 friend 通道；可空＝丢弃——
     *        §5.0 sink 约定；包 try，诊断通道故障不终止业务路径）。
     */
    void reportDev(const std::string& channel, const std::string& message) const noexcept;

    /// 宿主存储上下文（身份事实/锁/在途票据/事件总线/sink 的所有者；
    /// 非 owning——同一上下文体成员，生存期覆盖本对象）。
    ProjectStoreImpl& m_host;
    /// 文件原语门面（默认绑定进程级共享 Win32 实现；无自身状态）。
    win32::AtomicFile m_file;
    /// 会话注册表（活跃会话：状态指针→状态——句柄解析与同 run 并发
    /// 重复判定的数据面；m_sessionsMutex 保护）。
    mutable std::mutex m_sessionsMutex;
    std::map<const ArchiveSessionState*, std::shared_ptr<ArchiveSessionState>>
        m_sessions;
};

}  // namespace sdurws::ird::project

#endif  // SDURWS_IRD_PROJECT_SRC_ARCHIVESERVICEIMPL_HPP
