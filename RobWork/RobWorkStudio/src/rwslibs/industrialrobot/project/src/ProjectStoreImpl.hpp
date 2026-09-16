/**
 * @file   ProjectStoreImpl.hpp
 * @brief  存储上下文实现（ProjectStoreImpl）——§5.1 ProjectStore 契约的
 *         实现载体＋写权限门卫与部件所有权容器（私有实现头，不出
 *         include/——R-2 纪律）。
 *
 * 设计依据：
 *   - units/project.md §5.1（存储上下文契约：writable 唯一依据＝OS 排他
 *     句柄；生命周期 requestClose/closed/subscribeClose——Draining 排空
 *     协议）、§9.6（失权写入防护三道防线在上下文层的承接：①状态机门卫
 *     归本类、②③归 StoreLock——StoreLock.hpp 范围声明的明确分工）、
 *     §9.7（引用持有与最终释放：命令/归档/草稿/L5 关闭控制器持引用，
 *     pending 归零才 Closed）、§9.8（线程模型：writer 互斥＝全部变更性
 *     文件操作的串行化点；查询并发只读）、§8.7（激活前失败不影响当前
 *     项目——open 完整构造候选后才返回）；
 *   - 任务契约 tasks/foundation/PRJ-T08.json acceptance 1～3（PRJ-TX-9
 *     迟到写拒绝＋闭包外不可见、生命周期排空语义、失权后写拒绝）。
 *
 * 背景说明（本类在单元内的位置）：
 *   ProjectStoreImpl 是**所有权与门卫容器**，不是业务编排者：它持有
 *   ObjectStore/RevisionIndex/TxEngine（PRJ-T05/T06/T07 部件）与写锁
 *   （PRJ-T03 原语），把"谁可以写"（状态机＋锁门卫——§9.6①②）收敛到
 *   单一写通道 executeCommit()；未来的命令服务（PRJ-T10）、草稿服务
 *   （PRJ-T12）、归档端口（PRJ-T14）全部经本通道进入磁盘写路径，从结构
 *   上保证"全写入口统一防线"（PRJ-TX-9 的机制化前提）。查询端口
 *   （PRJ-T09）将消费本类持有的 RevisionIndex/ObjectStore（闭包内可见性
 *   由装载纪律结构性保证：闭包外修订不注册进会话索引）。
 *
 * 增量落位说明（DTB §5.4 口径登记）：
 *   1. executeCommit() 是**内部写通道**（不进公共头）：§6.1 的 S1～S5
 *      （断言/确认/双编译）与 S6 决策归命令服务（PRJ-T10），届时
 *      CommandServiceImpl 调用本通道完成 S6 后半（七步文件事务）。本
 *      任务先落通道与门卫——PRJ-TX-9 的"Closed 后写拒绝"在当前落位
 *      面上以本通道为写入口代表测试（当时唯一存在的写路径），全写入口
 *      （archive.begin/submit/draft.save）随 T10/T12/T14 挂载后各自经
 *      同一门卫，PRJ-TX-9 全量用例归 PRJ-T15 复验（§12 PRJ-T15 行）。
 *   2. 在途引用以 shared_ptr<void> 票据表达（acquireInFlight）——RAII
 *      所有权与"引用计数"语义等价且免手工配对错误；命令/归档/草稿服务
 *      落位时各自在进入在途态时持票、终结时析构释放（§9.7 引用持有者
 *      清单的机制化形态）。
 *
 * 线程模型（§9.8）：
 *   - m_lifecycleMutex：保护状态机（Active/Draining/Closed）、在途计数
 *     与订阅者表——身份查询面/关闭协议/写门卫的状态半边在此汇合；
 *   - m_writerMutex：writer 互斥（§9.8 原文）——executeCommit 的磁盘
 *     写段与未来的归档/草稿写在此外串行；
 *   - StoreLock/ObjectStore/TxEngine 各自的内部互斥按其契约工作；
 *   - 心跳线程在 StoreLock 内部（本类不感知）。
 *
 * 错误语义（错误二分，AGENTS §3）：
 *   - 调用方错误 → std::invalid_argument fail-fast（工厂的目录/参数
 *     前置违约——向导应先行校验的场景）；
 *   - 环境与状态错误 → StoreError 携带稳定码（§5.0 封闭集），同时经
 *     IDiagnosticsSink 产出对应用户级诊断（PRJ-* 码值＝diagnostics.md
 *     §4.6 收编清单——P-PR-6 处置，不直链 diagnostics 库）。
 */

#ifndef SDURWS_IRD_PROJECT_SRC_PROJECTSTOREIMPL_HPP
#define SDURWS_IRD_PROJECT_SRC_PROJECTSTOREIMPL_HPP

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <sdurws/ird/core/DiagData.hpp>
#include <sdurws/ird/core/Events.hpp>
#include <sdurws/ird/project/PersistenceFormat.hpp>
#include <sdurws/ird/project/ProjectStore.hpp>
#include <sdurws/ird/project/StoreTypes.hpp>

#include "ObjectStore.hpp"
#include "RevisionIndex.hpp"
#include "TxEngine.hpp"
#include "win32/StoreLock.hpp"

namespace sdurws::ird::project {

// 前向声明：查询端口实现（PRJ-T09 落位——完整定义于 QueryPortImpl.hpp，
// 仅实现文件消费；m_query 的 unique_ptr 成员以不完整类型持有，析构在
// ProjectStoreImpl.cpp 的 out-of-line 定义处完成——那里 include 全量头）。
class QueryPortImpl;

// 前向声明：命令服务实现（PRJ-T10 落位——完整定义于 CommandServiceImpl.
// hpp；m_commands 同款不完整类型持有，装配与析构在实现文件完成）。
class CommandServiceImpl;

// 前向声明：草稿服务实现（PRJ-T12 落位——完整定义于 DraftServiceImpl.
// hpp；m_drafts 同款不完整类型持有，装配与析构在实现文件完成）。
class DraftServiceImpl;

// 前向声明：撤销/重做服务实现（PRJ-T13 落位——完整定义于
// UndoRedoServiceImpl.hpp；m_undoRedo 同款不完整类型持有，装配与析构
// 在实现文件完成）。
class UndoRedoServiceImpl;

/**
 * @brief 存储上下文状态机（§9.6①的载体；Draining/Closed/LostWrite 一律
 *        拒绝新写——LostWrite 不单列状态：锁失权锁存在 StoreLock 内部，
 *        上下文状态仍 Active 但写门卫拒绝，两道防线串联）。
 */
enum class StoreLifecycleState {
    /// 正常状态：写入口可进入门卫（锁与写权限随后裁决）。
    Active,
    /// 关闭已请求：新写一律拒绝（context-closing 并入 ContextClosed
    /// 语义——§5.1 表）；等待在途引用归零后排空关闭。
    Draining,
    /// 排空完成：锁已释放、回调已发出；全部写入口拒绝，上下文对象
    /// 仍可安全存在（查询面可读——诊断/呈现用）。
    Closed,
};

/**
 * @brief ProjectStore 契约的实现（final——不再派生；消费者只面向公共
 *        抽象，实现细节（部件指针/门卫）不外泄——R-2）。
 *
 * 生命周期与所有权：仅由 ProjectStoreFactory 的工厂函数构造（构造函数
 * 私有＋friend），经 OpenStoreResult.store（unique_ptr）交付调用方独占
 * 持有。内部五个部件（锁/对象库/修订索引/事务引擎）与注入指针（事件
 * 总线/诊断 sink）全部非 owning——注入方的生存期义务见 OpenStoreRequest
 * 契约（覆盖上下文整个生命周期）。
 */
class ProjectStoreImpl final : public ProjectStore {
public:
    // ---- ProjectStore 契约（§5.1；语义详见公共头注释） ----

    ~ProjectStoreImpl() override;

    [[nodiscard]] bool writable() const noexcept override;
    [[nodiscard]] LockInfo lockInfo() const override;
    [[nodiscard]] core::ProjectId projectId() const noexcept override;
    [[nodiscard]] SchemaInfo schema() const override;
    [[nodiscard]] std::filesystem::path canonicalPath() const override;
    [[nodiscard]] std::uint32_t requestClose() override;
    [[nodiscard]] bool closed() const noexcept override;
    void subscribeClose(ICloseObserver& observer) override;

    /**
     * @brief 查询端口访问器（§5.1 原文形态——PRJ-T09 增量挂载；契约见
     *        ProjectStore::query() 公共头注释）。
     *
     * 返回装配期创建的 QueryPortImpl 实例（同一上下文恒同一实例——
     * 端口无独立生命周期）；noexcept 纯指针返回，任何状态下可调
     * （Closed 后端口自身拒绝——拒绝语义在端口方法内）。
     */
    [[nodiscard]] IProjectQueryPort& query() const noexcept override;

    /**
     * @brief 命令端口访问器（§5.1 原文形态——PRJ-T10 增量挂载；契约见
     *        ProjectStore::commands() 公共头注释）。
     *
     * 返回装配期创建的 CommandServiceImpl 实例（同一上下文恒同一实例
     * ——端口无独立生命周期）；noexcept 纯指针返回，任何状态下可调
     * （只读/Closed 后提交在 S1 形式校验拒绝——§6.1）。
     */
    [[nodiscard]] ProjectCommandService& commands() const noexcept override;

    /**
     * @brief 草稿服务端口访问器（§5.1 原文形态——PRJ-T12 增量挂载；
     *        契约见 ProjectStore::drafts() 公共头注释）。
     *
     * 返回装配期创建的 DraftServiceImpl 实例（同一上下文恒同一实例
     * ——端口无独立生命周期）；noexcept 纯指针返回，任何状态下可调
     * （拒绝语义在草稿服务方法内——写轨返回值轨、读轨 ContextClosed）。
     */
    [[nodiscard]] DraftService& drafts() const noexcept override;

    /**
     * @brief 撤销/重做服务端口访问器（§5.1 原文形态——PRJ-T13 增量挂载；
     *        契约见 ProjectStore::undoRedo() 公共头注释）。
     *
     * 返回装配期创建的 UndoRedoServiceImpl 实例（同一上下文恒同一实例
     * ——会话栈归属该实例，D-11）；noexcept 纯指针返回，任何状态下可调
     * （status 拒绝态降级为稳定空状态；undo/redo 终态经 CommandResult
     * 承载——拒绝语义在服务方法内）。
     */
    [[nodiscard]] UndoRedoService& undoRedo() const noexcept override;

    // ---- 内部通道（同单元后续端口实现/测试消费；不进公共头） ----

    /**
     * @brief 内部写通道：写门卫（§9.6①②）→ writer 互斥（§9.8）→
     *        七步事务（TxEngine::commit）。
     *
     * 执行序（短路）：状态机（Draining/Closed→ContextClosed＋
     * PRJ-WRITE-AUTHORITY-LOST）→锁面（从未持锁＝只读上下文→
     * LockHeldByOther＋PRJ-LOCK-HELD）→StoreLock::requireWriteAuthority
     * （防线①②：已释放/已失权→WriteRejected——诊断由 StoreLock 产出）
     * →writer 互斥下执行 commit（计划字段违约/发布边界拒绝/环境失败
     * 的错误语义随 TxEngine 契约透传）→成功后更新会话 HEAD 与权威
     * 元数据引用（自磁盘读回——地面事实，非内存推导）。
     *
     * @param plan [in] 提交计划（字段约束见 tx::CommitPlan）
     * @return 提交结果（CommitResult）
     *
     * @throws StoreError ContextClosed（Draining/Closed）/LockHeldByOther
     *         （只读上下文）/WriteRejected（失权/已释放）；以及 TxEngine::
     *         commit 的全部错误（invalid_argument/分支回归/环境码/
     *         StoreCorrupt——透传不吞）
     *
     * 线程约束：可从任意线程进入（门卫与 writer 互斥内部串行——§9.8
     * "submit/archive/save 可从任意线程进入（内部转串行）"）。
     */
    tx::CommitResult executeCommit(const tx::CommitPlan& plan);

    /**
     * @brief 获取在途引用票据（§9.7 引用持有者清单的机制化形态）。
     *
     * 票据存活期间 requestClose 的排空等待不完成（Draining 悬置）；
     * 票据析构即释放引用（RAII——归档会话/在途事务/草稿落盘在进入
     * 在途态时持票、终结时析构释放；命令服务/归档端口/草稿服务落位时
     * 各自消费本接口）。Closed 后获取返回空票据（关闭后不再接受新
     * 在途——迟到持有者以写入口拒绝路径处置）。
     *
     * @return 在途票据（shared_ptr<void> 的空指针形态判空；析构自动
     *         释放引用并可能触发排空完成）
     */
    [[nodiscard]] std::shared_ptr<void> acquireInFlight();

    /**
     * @brief 修订索引只读访问（测试/同单元后续查询端口装配消费——
     *        T09 的 QueryPort 实现将以本指针为语义载体；不进公共头）。
     *
     * 可见性纪律：装载已保证索引内只有闭包内修订（闭包外不注册——
     * §7.3/PRJ-TX-9），T09 的 tryRevision 语义据此结构性成立。
     */
    [[nodiscard]] const revindex::RevisionIndex& revisionIndex() const noexcept
    {
        return *m_index;
    }

    /**
     * @brief 对象库只读访问（同单元测试观测面——缓存预算/LRU 逐出的
     *        验收断言经 ObjectStore::cacheStats/cacheLruOrder；不进公共
     *        头，与 revisionIndex() 同款先例：R-2 禁令是跨单元暴露，
     *        同单元测试消费私有头是既定形态）。
     *
     * 可写方法（publishObject 等）不得经此调用——返回 const 引用；
     * 查询端口持有可变访问（读侧缓存推进）走 friend 成员通道。
     */
    [[nodiscard]] const objstore::ObjectStore& objectStore() const noexcept
    {
        return *m_objects;
    }

    /**
     * @brief 命令服务实现访问（同单元装配/测试消费——registry() 注册
     *        入口与 S1～S7 观测；不进公共头，revisionIndex() 同款先例：
     *        R-2 禁令是跨单元暴露，同单元测试消费私有头是既定形态）。
     */
    [[nodiscard]] CommandServiceImpl& commandService() noexcept
    {
        return *m_commands;
    }

    /**
     * @brief 草稿服务实现访问（PRJ-T12——同单元测试消费面，先例同上：
     *        草稿用例的磁盘残留构造/门卫观测经实现类型；不进公共头）。
     */
    [[nodiscard]] DraftServiceImpl& draftService() noexcept
    {
        return *m_drafts;
    }

    /**
     * @brief 撤销/重做服务实现访问（PRJ-T13——同单元测试消费面，先例
     *        同上：undo/redo 用例的会话栈与边界观测经实现类型；不进公
     *        共头。常规消费走 undoRedo() 公共访问器的接口引用，本访问
     *        器只服务实现类型的内部通道需求）。
     */
    [[nodiscard]] UndoRedoServiceImpl& undoRedoService() noexcept
    {
        return *m_undoRedo;
    }

    /**
     * @brief 只读上下文判定（从未持锁＝PM-07 显式只读或降级——与
     *        "持锁但失权"区分；CommandServiceImpl S1 的 not-writable
     *        分类用——§9.6 门卫两道防线的提前面）。
     */
    [[nodiscard]] bool readOnlyContext() const noexcept
    {
        return m_lock == nullptr;
    }

    /**
     * @brief 编译临时产物目录（D-09：.staging/tmp——非事务操作的临时
     *        产物落点；CommandServiceImpl 命令结束清理用，§6.6 实现口径⑤）。
     */
    [[nodiscard]] std::filesystem::path compileTmpDir() const
    {
        return m_canonicalDirFs / ".staging" / "tmp";
    }

private:
    // 查询端口实现是本类的视图面（消费部件/锁/权威快照——窄 friend 访问
    // 收敛于 QueryPortImpl 类整体，不散落到自由函数）。
    friend class QueryPortImpl;

    // 草稿服务实现是本类的草稿写面（写门卫/writer 互斥/在途票据/身份
    // 事实——同款窄 friend 访问，PRJ-T12）。
    friend class DraftServiceImpl;

    // 撤销/重做服务实现消费宿主的开发诊断通道（PRJ-T13——同款窄 friend
    // 访问；查询/命令端口经公共访问器消费，零写通道暴露）。
    friend class UndoRedoServiceImpl;

    friend class ProjectStoreFactory;

    /// 工厂内部打开协议主体（ProjectStoreImpl.cpp 内与公开 open/createNew
    /// 协作的自由函数——createNew 装载段持锁复用打开路径，公开 open 的
    /// 二次加锁入口不可进入；激活装配需要本类私有构造，故登记 friend）。
    friend OpenStoreResult openLocked(const OpenStoreRequest& request);

    /**
     * @brief 装配构造（仅工厂可达）：全部部件已由工厂打开协议组装就绪
     *        （装载/校验/恢复扫描完成），构造只做所有权接管与状态置位。
     *
     * 装配纪律（指针共享）：index 交付的 unique_ptr 所指对象与 engine
     * 内部持有的 RevisionIndex* 是**同一对象**（工厂装配序保证）——本类
     * 接管其所有权后，引擎与本类共享使用；构造期校验该约定（engine 与
     * index 的 projectDir/绑定一致性由工厂保证，此处防御空指针）。
     *
     * @param lock           [in] 写锁（可空＝只读上下文——PM-07 显式
     *                       ReadOnly 或 Writable 降级路径；RAII 所有权
     *                       移交本类）
     * @param lockView       [in] 打开时点的锁视图（PM-07 提示数据基线；
     *                       持有期 lockInfo() 动态读取锁对象的心跳刷新）
     * @param canonicalDir   [in] 规范项目目录（宽字符——§9.3 存储实例
     *                       身份；\\\\?\\ 前缀形态）
     * @param objects        [in] 对象库部件（所有权移交）
     * @param index          [in] 修订索引部件（所有权移交；须与 engine
     *                       绑定同一对象——装配纪律见上）
     * @param engine         [in] 事务引擎部件（所有权移交）
     * @param head           [in] 当前 HEAD 内容（装载校验通过的字节事实；
     *                       commit 的 currentHead 权威来源）
     * @param authoritative  [in] HEAD 引用的权威元数据记录（§4.5.1 结论②）
     * @param authoritativeRef [in] 权威元数据的注册键（须已注册）
     * @param eventBus       [in] 事件总线（非 owning，可空）
     * @param sink           [in] 诊断 sink（非 owning，可空）
     * @param collectorLifetime [in] 打开期收集型 sink 的生命周期锚（wp04-t10
     *                          缺陷修复登记：TxEngine/ObjectStore/StoreLock
     *                          长寿命部件绑定的 sink 指针指向该对象——锚随
     *                          上下文存活，部件的 sink 不得悬空；类型擦除
     *                          形态 shared_ptr<void>，删除器在创建点捕获）
     *
     * @throws std::invalid_argument objects/index/engine 之一为空
     *         （工厂装配违约——fail-fast，不产生半构造上下文）
     */
    ProjectStoreImpl(std::unique_ptr<win32::StoreLock> lock,
                     const LockInfo& lockView,
                     std::wstring canonicalDir,
                     std::unique_ptr<objstore::ObjectStore> objects,
                     std::unique_ptr<revindex::RevisionIndex> index,
                     std::unique_ptr<tx::TxEngine> engine,
                     const HeadRecord& head,
                     const ProjectMetadataRecord& authoritative,
                     const ObjectRefPair& authoritativeRef,
                     core::IDomainEventBus* eventBus,
                     IDiagnosticsSink* sink,
                     std::shared_ptr<void> collectorLifetime);

    /**
     * @brief 排空收尾（Draining 且 pending==0 的唯一路径；锁内判定、
     *        锁外执行副作用）。
     *
     * 执行序：m_lifecycleMutex 下确认状态与 pending→置 Closed（此后
     * 写入口拒绝——§9.6①"关闭路径同步置位状态"）→释放锁句柄（OS 层
     * 写权限即时失效）→拷贝订阅者→锁外逐个回调（一次性——回调里再进
     * 本类任何方法都安全：状态已 Closed）。析构路径复用本方法但不回调
     * （上下文正在销毁，回调引用即将悬空——登记口径：回调仅由
     * requestClose 完成路径触发）。
     *
     * @param invokeObservers [in] true＝requestClose 完成路径（回调）；
     *                        false＝析构静默终局。
     */
    void finishClose(bool invokeObservers);

    /**
     * @brief 查询端口的开发诊断通道（QueryPortImpl friend 窄访问器——
     *        容错路径的 reportDev 上报口；可空＝丢弃，§5.0 sink 约定）。
     *
     * 用户级稳定码不经此产出（查询期无对应收编码——不私造，CR-08；
     * 恢复/打开期诊断的产出点在工厂协议）。
     */
    [[nodiscard]] IDiagnosticsSink* queryDevSink() const noexcept
    {
        return m_sink;
    }

    /**
     * @brief 撤销/重做服务的开发诊断通道（UndoRedoServiceImpl friend 窄
     *        访问器——非对称声明族不入 redo 栈等观察点的 reportDev 上报
     *        口，PRJ-T13；可空＝丢弃，§5.0 sink 约定。用户级稳定码不经
     *        此产出——CR-08 同上）。
     */
    [[nodiscard]] IDiagnosticsSink* undoredoDevSink() const noexcept
    {
        return m_sink;
    }

    // ---- 状态（锁序：m_lifecycleMutex 先于 m_writerMutex；不反向） ----

    /// 打开期收集型 sink 的生命周期锚（wp04-t10 缺陷修复——声明序在全部
    /// 持 sink 指针的部件**之前**＝析构最后，保证 TxEngine/ObjectStore/
    /// StoreLock 的 sink 指针在部件析构前恒有效；类型擦除持有——见构造
    /// 参数注释）。
    std::shared_ptr<void> m_collectorLifetime;

    mutable std::mutex m_lifecycleMutex;  ///< 状态/在途/订阅者汇合点
    StoreLifecycleState m_state{StoreLifecycleState::Active};  ///< 状态机
    std::uint32_t m_pending{0};           ///< 在途引用计数（票据存活数）
    std::vector<ICloseObserver*> m_observers;  ///< 关闭回调订阅者（非 owning）

    /// writer 互斥（§9.8）：全部变更性文件操作的串行化点。
    std::mutex m_writerMutex;

    std::unique_ptr<win32::StoreLock> m_lock;  ///< 写锁（空＝只读上下文）
    LockInfo m_lockView;     ///< 打开时点锁视图（lockInfo 的持有期动态化基座）
    std::wstring m_canonicalDir;  ///< 规范项目目录（存储实例身份，§9.3）
    std::filesystem::path m_canonicalDirFs;  ///< 同上（fs 形态——路径接口用）

    std::unique_ptr<objstore::ObjectStore> m_objects;  ///< 对象库（T05）
    /// 修订索引（T06——闭包内装载）。unique_ptr 而非值成员的原因：工厂
    /// 构造 TxEngine 时已把同一对象的裸指针交给引擎（S6 时序操作的语义
    /// 载体），本类必须接管**同一对象**（指针共享而非拷贝/移动）——否则
    /// 引擎与索引分裂为两份状态（提交注册不可见）。unique_ptr 形态的
    /// 所有权：工厂创建、本类独占持有、与引擎共享使用。
    std::unique_ptr<revindex::RevisionIndex> m_index;
    std::unique_ptr<tx::TxEngine> m_engine;  ///< 事务引擎（T07）

    HeadRecord m_head;  ///< 会话 HEAD（成功提交后更新为 newHead）
    ProjectMetadataRecord m_authoritative;  ///< 权威元数据（D-5 底本）
    ObjectRefPair m_authoritativeRef;  ///< 权威元数据注册键

    core::IDomainEventBus* m_eventBus{nullptr};  ///< 事件总线（非 owning）
    IDiagnosticsSink* m_sink{nullptr};  ///< 诊断 sink（非 owning，可空）

    /// 命令服务实现（PRJ-T10——§5.1 端口访问器 commands() 的交付物）。
    /// 声明序在 m_query 之前：构造先于查询端口（hasUnresolvedPayload
    /// 判据注入注册表查询），析构后于查询端口（判据 lambda 消费注册表
    /// ——先析构消费方再析构被消费方，避免悬空捕获）。
    std::unique_ptr<CommandServiceImpl> m_commands;

    /// 查询端口实现（PRJ-T09——§5.1 端口访问器 query() 的交付物）。声明
    /// 序在全部部件之后：构造于装配末尾（部件就绪后创建），析构先于部件
    /// （QueryPortImpl 析构不触碰宿主成员——host 引用不悬空使用，安全）。
    std::unique_ptr<QueryPortImpl> m_query;

    /// 草稿服务实现（PRJ-T12——§5.1 端口访问器 drafts() 的交付物）。声明
    /// 序在 m_query 之后＝构造序最末、析构序最先（DraftServiceImpl 只持
    /// 宿主引用、析构无操作——不触碰宿主成员，逆序安全）。
    std::unique_ptr<DraftServiceImpl> m_drafts;

    /// 撤销/重做服务实现（PRJ-T13——§5.1 端口访问器 undoRedo() 的交付
    /// 物）。声明序在 m_drafts 之后＝构造序最末、析构序最先（服务只持
    /// 宿主引用、调用期才解引用 query()/commands()——构造时点两端口已
    /// 就绪；析构无操作，逆序安全）。
    std::unique_ptr<UndoRedoServiceImpl> m_undoRedo;
};

}  // namespace sdurws::ird::project

#endif  // SDURWS_IRD_PROJECT_SRC_PROJECTSTOREIMPL_HPP
