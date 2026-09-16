/**
 * @file   ProjectStore.hpp
 * @brief  存储上下文（ProjectStore）与工厂（ProjectStoreFactory）——打开/
 *         新建项目的唯一入口与上下文生命周期契约（PRJ-T08 落位）。
 *
 * 设计依据：
 *   - units/project.md §5.1（打开与存储上下文——ProjectStore/ProjectStoreFactory
 *     /ICloseObserver 的接口原文：open/createNew 五步协议②③⑤、生命周期
 *     requestClose/closed/subscribeClose、身份查询面 writable/lockInfo/
 *     projectId/schema/canonicalPath）、§8.7（打开协议服务侧：激活前失败
 *     不影响当前项目；正常打开与崩溃恢复的区别）、§9.6（失权写入防护——
 *     全部写入口的统一门卫语义在上下文层裁决）、§9.7（存储上下文的引用
 *     持有与最终释放——Draining 排空协议）、§5.1 表（open/createNew/
 *     requestClose 的前置/后置/副作用契约）；
 *   - 需求 PM-02（打开五步协议 project 部分②③⑤）、PM-06（旧格式/未来
 *     版本稳定拒绝＋升级指引数据）、PM-07（只读打开）、PM-08（崩溃与恢复
 *     →RecoveryReport）、PM-03（关闭排空存储侧——等待在途归档/草稿完成）、
 *     PM-01（新建项目存储侧：空项目骨架）、SA-17（写权限唯一依据＝OS
 *     排他句柄）；
 *   - 任务契约 tasks/foundation/PRJ-T08.json acceptance 1～4（PRJ-TX-9
 *     迟到写拒绝与闭包外不可见、恢复报告断言、生命周期语义、P-PR-1/
 *     P-PR-6 处置）。
 *
 * 背景说明（存储上下文与界面会话分离——ARCH §6.8 A7 承接）：
 *   本软件中"项目被打开"是一种 OS 级事实（写锁句柄），不是界面状态。
 *   ProjectStore 封装这一事实：一个实例＝一次打开＝至多一份写权限；
 *   界面会话可以来去，存储上下文存活到显式关闭（requestClose 排空）或
 *   析构。上下文内部持有对象库/修订索引/事务引擎（PRJ-T05/T06/T07 落位
 *   的部件），并是后续各端口（查询/命令/草稿/归档——PRJ-T09/T10/T12/T14）
 *   的所有权容器。
 *
 * 增量落位说明（DTB §5.4 口径登记）：§5.1 原文的五个端口访问器
 *   query()/commands()/drafts()/undoRedo()/archive() 随其实现任务增量
 *   增补（PRJ-T09/T10/T12/T13/T14——各端口实现类尚未存在，先落访问器
 *   只能 stub，违反"禁止空实现"纪律；与 StoreTypes.hpp/§3.1 组成表
 *   "随任务节奏增量落位"同一口径）。本头先落**生命周期与身份子集**：
 *   打开/新建协议、关闭排空、身份查询——它们正是 PRJ-T08 的契约产物，
 *   且是后续端口挂载的基座（端口实现经 ProjectStoreImpl 的内部通道
 *   获得写权限门卫与部件访问）。前四个访问器已分别随 T09/T10/T12/T13
 *   挂载，第五个访问器 archive() 已随 PRJ-T14 挂载（归档端口——
 *   ArchivePort.hpp）。
 *
 * 线程模型（§9.8）：身份查询面（writable/lockInfo/projectId/schema/
 * canonicalPath/closed）并发安全（内部互斥）；requestClose 幂等、可与
 * 在途操作并发；写路径的串行化归各写入口内部（writer 互斥——§9.8）。
 */

#ifndef SDURWS_IRD_PROJECT_PROJECTSTORE_HPP
#define SDURWS_IRD_PROJECT_PROJECTSTORE_HPP

#include <cstdint>
#include <filesystem>
#include <string>

#include <sdurws/ird/project/StoreTypes.hpp>

namespace sdurws::ird::project {

// IProjectQueryPort 完整定义于 QueryPort.hpp（本头只前向声明——query()
// 以引用返回接口；消费者调用端口方法需自行 include QueryPort.hpp，
// 存储上下文头不承载端口契约——头职责单一）。
class IProjectQueryPort;

// ProjectCommandService 完整定义于 CommandService.hpp（同款前向声明
// 纪律——commands() 以引用返回接口，PRJ-T10 增量挂载）。
class ProjectCommandService;

// DraftService 完整定义于 DraftService.hpp（同款前向声明纪律——drafts()
// 以引用返回接口，PRJ-T12 增量挂载）。
class DraftService;

// UndoRedoService 完整定义于 UndoRedo.hpp（同款前向声明纪律——undoRedo()
// 以引用返回接口，PRJ-T13 增量挂载）。
class UndoRedoService;

// IResultArchivePort 完整定义于 ArchivePort.hpp（同款前向声明纪律——
// archive() 以引用返回接口，PRJ-T14 增量挂载）。
class IResultArchivePort;

/**
 * @brief 关闭完成观察者（§5.1 ProjectStore::subscribeClose 的回调契约）。
 *
 * 背景说明（一次性回调语义）：存储上下文完成排空（全部在途引用释放＋
 * 草稿 flush＋锁句柄释放）进入 Closed 后，向已订阅的观察者回调**恰好
 * 一次**（§5.1"全部在途完成并 flush 草稿、释放锁句柄后回调（一次性）"）。
 * 典型消费者＝L5 关闭控制器（ui/workflow）——它据此解除对上下文的等待。
 * 观察者为非 owning 引用：订阅方必须保证观察者对象的生存期覆盖回调
 * 发生点（建议＝观察者与订阅方同生命周期，或 close 完成后取消订阅需求
 * 不存在——本契约无退订，登记为口径：上下文是短生命周期对象，回调至多
 * 一次，悬挂风险由"订阅者先于上下文消亡须自行保证"承担）。
 *
 * 回调内约束：不得回调进本上下文的任何方法（回调发生时上下文已 Closed，
 * 写入口必拒、查询面已冻结）——实现只应做"解除等待/通知界面"类动作。
 */
struct ICloseObserver {
    /// 虚析构：经接口引用多态消费的常规保障。
    virtual ~ICloseObserver() = default;

    /**
     * @brief 存储上下文进入 Closed（排空完成、锁已释放）后的回调。
     *
     * @param store [in] 完成关闭的上下文（引用仅回调期有效——此后调用方
     *              通常释放其 unique_ptr）。
     */
    virtual void onStoreClosed(ProjectStore& store) = 0;
};

/**
 * @brief 存储上下文（ARCH §6.8 A7）——一次项目打开的运行时形态，与界面
 *        会话分离；写权限的唯一持有者（SA-17）。
 *
 * 生命周期与所有权：仅经 ProjectStoreFactory::open/createNew 构造，以
 * unique_ptr 交付调用方（OpenStoreResult.store）；调用方独占持有。终止
 * 路径两条：析构（隐式排空——若尚 Active，等同于 requestClose 的同步
 * 收尾）与 requestClose 完成后的 Closed。Closed 后实例仍可安全存在
 * （查询面返回关闭态），析构释放残余资源。
 *
 * 状态机（§9.6/§9.7）：Active（正常）→ requestClose → Draining（拒绝
 * 新写、等待在途归零）→ pending==0 → Closed（锁释放＋一次性回调）。
 * 另有锁失权态（StoreLock 防线②③锁存）：上下文仍 Active 但写权限
 * 丢失——全部写入口拒绝（PRJ-WRITE-AUTHORITY-LOST），状态机不回退。
 */
class ProjectStore {
public:
    /// 虚析构：经 unique_ptr<ProjectStore>（OpenStoreResult.store）删除
    /// 实现对象是多态所有权的唯一路径；实现侧析构完成锁释放等终局动作。
    virtual ~ProjectStore() = default;

    // ---- 身份与权限查询面（并发安全；Closed 后仍可读——诊断/呈现用） ----

    /**
     * @brief 是否持有写权限——唯一依据：本实例持有 OS 排他句柄（§5.1/
     *        SA-17）。不查心跳、不查 PID、不做任何推测（卡顿不接管——
     *        D-03 的结构性落实：本方法只读上下文内的锁状态位）。
     */
    [[nodiscard]] virtual bool writable() const noexcept = 0;

    /**
     * @brief 锁信息视图（PM-07 提示数据：持有者 PID/host/心跳＋isSelf）。
     *        仅诊断用途；撕裂读容忍下的零值字段＝未知（LockHolderRecord
     *        注释口径）。
     */
    [[nodiscard]] virtual LockInfo lockInfo() const = 0;

    /// 项目身份（prj- 规范文本；与 project.json/HEAD 一致——打开③步已校验）。
    [[nodiscard]] virtual core::ProjectId projectId() const noexcept = 0;

    /// 存储格式信息（打开②步校验通过的格式——本实现可解读的格式）。
    [[nodiscard]] virtual SchemaInfo schema() const = 0;

    /**
     * @brief 项目目录的规范形态（§9.3：GetFinalPathNameByHandleW 解析的
     *        最终路径，\\\\?\\ 前缀形态；存储实例身份——进程内重复打开
     *        判定与最近项目去重的键）。
     */
    [[nodiscard]] virtual std::filesystem::path canonicalPath() const = 0;

    // ---- 端口访问器（§5.1 原文形态；随端口实现任务增量挂载） ----

    /**
     * @brief 查询端口（②端口，§5.1 原文"含只读实例"——PRJ-T09 增量
     *        挂载）。项目只读状态的唯一合法视图入口（ARC-02 端口协作：
     *        消费方只经本端口取只读快照，无私有互访——ARCH §7.2）。
     *
     * 语义（§5.2/§4.7）：
     *   - 引用与上下文同生命周期（上下文销毁后引用失效——常规 C++
     *     生存期纪律）；同一上下文多次调用返回同一实例（端口无独立
     *     生命周期——它是上下文的视图面）；
     *   - 只读打开的实例同样提供查询（PM-07 可查看——写入被拒但读取
     *     全量可用）；
     *   - 上下文 Closed 后端口进入拒绝态（各方法抛 ContextClosed；
     *     noexcept 的 tryObject 以 nullopt 表达——§5.2 签名契约），
     *     Draining 排空期查询仍可用（PM-03 关闭对话框的呈现数据源）；
     *   - 本访问器本身 noexcept 不抛、任何状态下可调（Closed 后仍可取
     *     引用再观察拒绝态——诊断/呈现场景）。
     *
     * @return 查询端口接口引用（非 owning——随上下文消亡）
     *
     * 线程安全：并发安全（返回同一实例的引用；实例方法各自线程安全
     * ——§4.7"查询端口全部方法线程安全"）。
     */
    [[nodiscard]] virtual IProjectQueryPort& query() const noexcept = 0;

    /**
     * @brief 命令端口（①端口，§5.1 原文"只读实例上提交即拒绝"——PRJ-T10
     *        增量挂载）。唯一写路径入口（submit）的获取点。
     *
     * 语义（§5.3.1/§6.1）：
     *   - 引用与上下文同生命周期（同 query()——端口无独立生命周期）；
     *     同一上下文多次调用返回同一实例；
     *   - 只读打开的实例同样提供本端口（拒绝发生在 submit 的 S1 形式
     *     校验——Rejected(not-writable)＋门卫稳定码诊断，§6.3/§9.6）；
     *   - Draining/Closed 后提交同样在 S1 拒绝（关闭态先行分流——写
     *     路径拒绝语义与查询端口"Draining 不拒"区分，§4.7）；
     *   - 处理器注册（L5 装配期，§6.5）经实现类型的注册表访问器进行
     *     （本抽象端口不含注册面——§5.3.5 HandlerRegistry 的归属与
     *     装配通道见 CommandService.hpp）。
     *
     * @return 命令端口接口引用（非 owning——随上下文消亡）
     *
     * 线程安全：并发安全（返回同一实例；submit 内部命令执行槽串行——
     * §6.1）。
     */
    [[nodiscard]] virtual ProjectCommandService& commands() const noexcept = 0;

    /**
     * @brief 草稿服务端口（§5.1 原文"定时落盘/恢复/投影/放弃"——PRJ-T12
     *        增量挂载）。未应用草稿的唯一读写入口（PM-04"保存与应用
     *        分离"：本端口落盘不产生修订；"应用"走 commands()）。
     *
     * 语义（§5.4/§8）：
     *   - 引用与上下文同生命周期（同 query()/commands()——端口无独立
     *     生命周期）；同一上下文多次调用返回同一实例；
     *   - 只读打开的实例可读不可写（tryLoad/summarize/list 可用——
     *     PM-07"可查看"；save/discard 经门卫拒绝并产 PRJ-LOCK-HELD——
     *     拒绝走返回值轨，ui 定时器安全）；
     *   - Draining 排空期读轨可用（PM-03 关闭对话框的草稿恢复态）、
     *     写轨拒绝（ContextClosed 返回值）；Closed 后读轨抛 ContextClosed；
     *   - save 执行期间持在途票据（§9.7——requestClose 排空等待在途
     *     草稿落盘，"末次草稿 flush 确认"）。
     *
     * @return 草稿服务接口引用（非 owning——随上下文消亡；方法集契约
     *         见 DraftService.hpp）
     *
     * 线程安全：并发安全（返回同一实例；写方法内部经门卫＋writer 互斥
     * 串行——§9.8）。
     */
    [[nodiscard]] virtual DraftService& drafts() const noexcept = 0;

    /**
     * @brief 撤销/重做服务端口（§5.1 原文五端口访问器之最后挂载位——
     *        PRJ-T13 增量挂载）。项目命令级撤销/重做的唯一入口（撤销＝
     *        逆命令提交产生新修订——PM-18/PA-2；D-11 会话栈归属本端口）。
     *
     * 语义（§5.5/§6.9——契约详见 UndoRedo.hpp）：
     *   - 引用与上下文同生命周期（同 query()/commands()/drafts()——端口
     *     无独立生命周期）；同一上下文多次调用返回同一实例——**会话栈
     *     归属该实例**（D-11"会话内按分支"的机制边界：上下文消亡＝会话
     *     消亡，redo 记录不持久化；undo 可用性由 tip 修订 inverse 推导，
     *     重启后照常推导——§5.5）；
     *   - 只读打开的实例同样提供本端口：status() 全量可用（读轨）；
     *     undo/redo 经命令端口 S1 形式校验拒绝（Rejected(not-writable)＋
     *     门卫稳定码——§6.1/§9.6，与直接提交同表）；
     *   - Draining/Closed 后 undo/redo 同样在 S1/查询段拒绝（CommandResult
     *     终态承载——Aborted(context-closing)/Failed；status() noexcept
     *     降级为稳定空状态）；
     *   - 与草稿局部撤销的边界（§2.3/§6.9 末行）：本端口只承接项目命令
     *     级撤销（产生修订）；草稿内编辑级撤销归 ui＋业务域，不经本端口。
     *
     * @return 撤销/重做服务接口引用（非 owning——随上下文消亡；方法集
     *         契约见 UndoRedo.hpp）
     *
     * 线程安全：并发安全（返回同一实例；三方法各自线程安全——会话栈
     * 内部互斥，提交段经命令端口执行槽串行）。
     */
    [[nodiscard]] virtual UndoRedoService& undoRedo() const noexcept = 0;

    /**
     * @brief 归档端口（§5.1 原文五端口访问器之 T14 位——PRJ-T14 增量
     *        挂载）。任务运行结果/检查点写入 results/·checkpoints/ 的
     *        唯一存储入口（O-12；供 execution 经 ARCH §3.5 反向服务边
     *        消费——IResultArchivePort 契约详见 ArchivePort.hpp）。
     *
     * 语义（§5.6/§10.1）：
     *   - 引用与上下文同生命周期（同 query()/commands()/drafts()/
     *     undoRedo()——端口无独立生命周期）；同一上下文多次调用返回
     *     同一实例；
     *   - 只读打开的实例同样提供本端口（begin 的锁面门卫拒绝——
     *     LockHeldByOther＋PRJ-LOCK-HELD，与草稿写轨同表）；
     *   - Draining 拒绝**新** begin（§9.7 排空期不再接受新归档预留），
     *     已开始的会话继续 writeBatch/finalize/abandon——上下文存活至
     *     归档终结（PRJ-TX-8 的存活机制：会话持宿主在途票据）；
     *   - 归档位置绑定原修订（A8）：HEAD 前进不影响写入合法性（当前性
     *     归 evidence，CON-02——§10.1"当前性与历史归档"行）；
     *   - 调用线程按 §5.6/§9.8 单侧冻结（任意线程进入、内部 writer
     *     互斥串行——P-PR-4 处置口径，RunRegistry 对接细节待 execution
     *     详设二次对齐）。
     *
     * @return 归档端口接口引用（非 owning——随上下文消亡；方法集契约
     *         见 ArchivePort.hpp）
     *
     * 线程安全：并发安全（返回同一实例；四方法各自经门卫＋writer 互斥
     *   串行——§9.8）。
     */
    [[nodiscard]] virtual IResultArchivePort& archive() const noexcept = 0;

    // ---- 关闭协议（PM-03"等待"选项的实现锚点；§9.7） ----

    /**
     * @brief 请求关闭：Active → Draining（拒绝新写），等待在途引用归零
     *        后自动完成收尾（锁释放→Closed→一次性回调）。
     *
     * 语义（§5.1 表）：幂等——已 Draining/Closed 时再次调用只返回当前
     * 在途数，不重复收尾；可与在途操作并发（在途计数保护内部互斥）。
     * pending==0 时同步走完收尾（本调用返回即 Closed，回调已发出）。
     * 超时强制收尾（abandonAll）归 L5 关闭控制器（§9.7——本契约不设
     * 超时参数，Draining 可能长期等待是"等待"选项的语义本身；强杀路径
     * 由 execution 的 abandon 保证不饿死，A-4）。
     *
     * @return 发起关闭时点的在途引用数（归档会话/在途事务/草稿落盘——
     *         0 表示已同步完成关闭）。
     */
    [[nodiscard]] virtual std::uint32_t requestClose() = 0;

    /// 是否已进入 Closed（排空完成、锁已释放；之后写入口一律拒绝）。
    [[nodiscard]] virtual bool closed() const noexcept = 0;

    /**
     * @brief 订阅关闭完成回调（一次性；§5.1）。幂等订阅——同一观察者
     *        重复订阅至多收到一次回调（指针去重）；Closed 后订阅立即
     *        回调（关闭事实已发生，观察者不应错过）。
     *
     * @param observer [in] 非 owning 引用；生存期须覆盖回调发生点
     *                 （ICloseObserver 头注口径）。
     */
    virtual void subscribeClose(ICloseObserver& observer) = 0;
};

/**
 * @brief 存储上下文工厂——open/createNew 的唯一入口（§5.1 原文形态；
 *        全静态方法，无实例状态）。
 *
 * 背景说明（PM-02 五步的 project 部分）：①路径预检与④领域校验由调用方
 * （workflow/各域处理器）编排，本工厂承接②目录形态与版本检查、③加载与
 * 读校验、⑤激活与恢复诊断（§8.7 表）。**激活前失败不影响当前项目**：
 * open 完整构造候选上下文后才返回；任何失败路径不触碰已打开项目的存储
 * 上下文（open 从不写当前项目；对目标目录的探测与锁获取仅作用于目标）。
 *
 * 进程内互斥（§9.3）：同一规范路径的第二次 Writable 打开直接拒绝
 * （lock-held-by-other，防自我双写）——进程内注册表在工厂内部维护；
 * 进程外的互斥由 OS 锁（StoreLock/D-02）裁决。
 */
class ProjectStoreFactory {
public:
    ProjectStoreFactory() = delete;

    /**
     * @brief 打开既有项目（PM-02②③⑤服务侧；§5.1 factory.open 契约）。
     *
     * 执行序（失败即整途中止，不留半构造上下文——激活前失败语义）：
     *   ① 兜底路径校验：目录不存在/不可达 → not-a-project（§8.7①兜底）
     *      →路径规范化（§9.3，subst/符号链接/大小写收敛）；
     *   ② 目录形态与版本：project.json 缺失 → not-a-project（§4.1 行
     *      "缺失＝非项目目录"）；formatId 不符/主版本更旧 → format-legacy
     *      ＋PRJ-FORMAT-LEGACY（原文件不动——PM-06）；主版本更新 →
     *      schema-future＋升级指引数据（detail 键 document/supported/
     *      upgrade）＋PRJ-SCHEMA-FUTURE；
     *   （锁半步，仅 Writable 请求）锁获取：他方持有 → 降级 ReadOnly
     *      ＋PRJ-LOCK-HELD（含持有 PID——PM-07 不阻塞等待）；OS 拒绝 →
     *      §9.5 映射 media-read-only/access-denied；显式 ReadOnly 请求
     *      不取锁（读取持有者记录作 lockInfo——§9.2②）；
     *   ③ 加载与读校验：读 HEAD（缺失/损坏＝store-corrupt）→跨文件
     *      一致性（projectId/schemaVersion 与 project.json 不一致＝
     *      store-corrupt，§4.1 行口径）→恢复扫描（§7.4①④⑤：.staging
     *      残留/闭包完整性/悬挂计数；④失败＝store-corrupt 打开失败，
     *      定位到文件——PM-02"失败显示具体文件"）→闭包内修订/元数据
     *      装载入会话索引（闭包外修订不装载＝查询不可见，§7.3/PRJ-TX-9；
     *      闭包内清单损坏定位到文件）；
     *   ⑤ 孤儿草稿扫描（§7.4③：.new/.bak 残留/损坏/分支不存在）→
     *      RecoveryReport.orphanDraftFiles＋PRJ-RECOVERY-ORPHAN-DRAFT；
     *      汇编 RecoveryReport（诊断同步经 IDiagnosticsSink 上报）。
     *
     * @param request [in] 打开请求（路径/模式/注入——OpenStoreRequest）
     *
     * @return 打开结果（store 必非空；writable/lockInfo/recovery 如实）
     *
     * @throws StoreError not-a-project（NotAProject）／format-legacy
     *         （FormatLegacy）／schema-future（SchemaFuture）／
     *         store-corrupt（StoreCorrupt——含跨文件不一致与④失败）／
     *         lock-held-by-other（LockHeldByOther——进程内重复 Writable
     *         打开，§9.3）／media-read-only（MediaReadOnly）／
     *         access-denied（AccessDenied）；§5.1 factory.open 错误表的
     *         全量落点
     *
     * 复杂度：O(磁盘清单总量)（装载＋闭包校验各一遍——打开是低频操作）。
     *
     * 线程安全：内部互斥（进程内注册表与构造过程串行——§5.1 表"线程
     * 安全（内部互斥）；读校验期间持读句柄"）。
     */
    static OpenStoreResult open(const OpenStoreRequest& request);

    /**
     * @brief 新建空项目（PM-01 存储侧；§5.1 createNew 契约）。
     *
     * 执行序：目标校验（不存在或空目录——违反＝调用方错误 fail-fast）
     * →同卷组装区完整组装（PM-01"先在 .staging 组装再整体就位"：初始
     * 修订 r0＋权威元数据 M0（主分支 main，label 一次写入——P-PR-8）＋
     * project.json＋HEAD＋对象经 ObjectStore 发布校验通道）→整体 rename
     * 就位（同卷原子；失败清理组装区与目标——**取消/失败不留半成品**，
     * §5.1 表"失败清理目标目录"）→按 open 协议装载激活（含写锁获取——
     * 创建即取得写权限）。
     *
     * @param dir           [in] 目标 .rwdesign 目录（须不存在或为空目录）
     * @param displayName   [in] 项目显示名（UTF-8；写入 M0.projectDisplayName）
     * @param eventBus      [in] 事件总线（非 owning，可空——语义同
     *                      OpenStoreRequest.eventBus）
     * @param diagnostics   [in] 诊断 sink（非 owning，可空——同上）
     * @param cacheBudget   [in] 对象缓存预算（默认 256 MiB，§4.6）
     *
     * @return 打开结果（可写上下文——创建者即首个写权限持有者）
     *
     * @throws std::invalid_argument 目录非空已存在／displayName 为空
     *         （调用方契约违约——向导应先行校验，fail-fast 不产出稳定码）
     * @throws StoreError 组装/就位/装载期间的环境失败（DiskFull/
     *         AccessDenied/WriteRejected 等——目标目录已清理，不留半成品）
     *
     * 线程安全：内部互斥（同 open——注册表与组装过程串行）。
     */
    static OpenStoreResult createNew(
        const std::filesystem::path& dir,
        std::string_view displayName,
        core::IDomainEventBus* eventBus = nullptr,
        IDiagnosticsSink* diagnostics = nullptr,
        ObjectCacheBudget cacheBudget = {});
};

}  // namespace sdurws::ird::project

#endif  // SDURWS_IRD_PROJECT_PROJECTSTORE_HPP
