/**
 * @file   TxEngine.hpp
 * @brief  事务引擎（TxEngine）——七步提交协议（§7.1）、.staging 暂存区与
 *         恢复扫描（§7.3/§7.4）的实现载体（私有实现头，不入 include/——
 *         R-2 纪律）。
 *
 * 设计依据：
 *   - units/project.md §7.1（七步协议逐步表——本类是其唯一实现载体：每一步
 *     的动作/持锁状态/可见性/失败结果/恢复动作逐项落地）、§7.2（Windows
 *     文件操作保证——经 win32::AtomicFile 原语消费，不超诺）、§7.3（已发布
 *     但未提交内容的识别＝§4.5 闭包规则）、§7.4（事务提交与恢复状态机＋
 *     PM-08 恢复顺序①④⑤——②锁残留归 StoreLock/PRJ-T03、③孤儿草稿归
 *     DraftService/PRJ-T12，本类只承接事务与闭包相关的部分）、§7.5（崩溃
 *     一致性要点）、§7.6（故障注入矩阵 F1～F8——注入经 IFileOps 接缝）、
 *     §4.1（.staging/.staging/tmp 目录契约）、§6.1（修订序号 O-15 在 S6
 *     分配）、D-09（临时产物限 .staging/tmp）、D-18（事件发布失败不回滚）；
 *   - 需求 NFR-REL-01（多文件事务：任一步失败旧版本保持完整）、PM-08
 *     （崩溃后已提交修订与 HEAD 字节不变；未提交内容忽略＋恢复诊断）、
 *     ARC-01（聚合根＋命令原子产生修订）、NFR-COR-02（报告确定性排序）；
 *   - 任务契约 tasks/foundation/PRJ-T07.json acceptance 1～4（≙WP-04-T07）。
 *
 * 背景说明（本类在单元内的位置——"引擎"边界）：
 *   事务引擎是**协议执行者**，不是业务编排者：§6.1 的 S1～S5（断言/确认/
 *   双编译等命令阶段）与 S6 的入口决策归 CommandServiceImpl（PRJ-T10），
 *   本类承接"决策通过后"的七步文件事务。调用方装配一份 CommitPlan（新
 *   对象＋元数据增量＋命令留痕），引擎负责：生成 tx-id 与修订身份、派生
 *   并校验候选元数据（RevisionIndex 的 S6 时序：先注册新修订、后发布边界
 *   校验）、暂存（write-through＋flush 闸门）、验证（重读＋SHA-256 全量）、
 *   只增发布（对象先行、修订清单最后）、HEAD 原子切换（唯一提交点）、
 *   事件发布（失败重试一次不回滚——D-18）与清理（失败仅开发诊断）。
 *   任何一步失败都不回滚已就位内容——失败残留是**设计行为**（悬挂内容
 *   处于 §4.5 闭包之外，对查询不可见，由恢复扫描忽略＋报告，PM-08）。
 *
 * 为什么对象发布不经 ObjectStore::publishObject（实现口径登记，DTB §5.4）：
 *   §7.1 第 2 步原文要求"对象字节……全部写入 .staging/<tx-id>/"（事务私有
 *   暂存区），且 §7.6 表头要求 F2/F4 类注入"经 IFileOps 接缝"；而
 *   ObjectStore::publishObject 绑定对象库级暂存目录并默认构造真实
 *   AtomicFile（PRJ-T05 落位形态），既不能落到事务目录也不可注入故障。
 *   因此本类自行完成"暂存写→publishNew 就位"，"目标已存在"的共享判定
 *   复用 ObjectStore 的读回校验通道（object()——摘要不符抛 StoreCorrupt，
 *   与 publishObject 的共享语义同源同码）；publishObject 保留给非事务
 *   发布路径（如项目创建组装）。
 *
 * P-PR-1 处置（acceptance 4，interUnit）：
 *   修订清单摘要与对象内容版本一律经本单元唯一哈希入口
 *   codec::contentVersionOf（内部即 core::ContentDigester——core.md v0.1
 *   §4.2/§5.2 契约），身份类型全部来自 core 公共契约头（Identity.hpp/
 *   Digest.hpp），事件发布消费 core::IDomainEventBus（v0.1 §4.9/§5.8）。
 *   零 core 修改、零本地第二套格式化/哈希路径；core 冻结出 diff 后按
 *   影响面增量同步（单元卡 §15.3 P-PR-1 处置口径，留痕随任务登记）。
 *
 * 错误语义（错误二分，AGENTS §3/单元卡 §5.0）：
 *   - 调用方错误 → std::invalid_argument fail-fast：CommitPlan 字段违约
 *     （无效身份/空分支/空时间戳/命令 token 违约）、authoritativeRef 未按
 *     RevisionIndex 时序契约注册——全部发生在**任何磁盘写之前**（内存段
 *     先行＝"顺序即原子性"的 D-09 前置精神：可失败的操作先于不可失败的
 *     发布动作）。
 *   - 不变量违约（发布边界）→ StoreError(BranchMetadataRegression)：由
 *     RevisionIndex::validateMetadataPublish 抛出，引擎透传（零磁盘副作用）。
 *   - 环境错误（磁盘/权限）→ StoreError 携带稳定码：磁盘满＝DiskFull、
 *     拒绝访问＝AccessDenied、其余写路径失败＝WriteRejected（与 ObjectStore
 *     PRJ-T05 登记口径①同表；§7.1 第 1 步"write-rejected/disk-full"、第 4
 *     步"write-rejected"的归类落点）。验证步失败（重读不符）＝数据侧
 *     StoreCorrupt（§7.1 第 3 步口径，附文件路径定位）。
 *
 * 线程约束（非线程安全——AGENTS §2.5 高危信息）：
 *   同一实例的 commit() 必须串行调用（串行化责任＝§6.1 命令执行槽，本类
 *   不加锁）；scanForRecovery() 与 commit()/对象发布不得并发（扫描归打开/
 *   恢复时点，写锁语义覆盖——§7.3）。tx-id 计数器为原子变量（仅保证编号
 *   唯一，不构成对 commit 并发的许可）。
 *
 * 所有权与生命周期：五个注入指针（IFileOps/ObjectStore/RevisionIndex/
 *   IDomainEventBus/IDiagnosticsSink）全部**非 owning**，生存期由持有方
 *   （未来的 ProjectStoreImpl/测试夹具）保证覆盖本对象；除 fileOps 外均可
 *   为空——fileOps 为空＝装配违约（invalid_argument）；ObjectStore/-
 *   RevisionIndex 为空＝commit/scan 无法工作（同样 invalid_argument，构造
 *   期即拒绝）；eventBus 为空＝跳过第 6 步（§5.1 OpenStoreRequest"可空：
 *   测试/只读场景"口径）；sink 为空＝丢弃诊断（§5.0 sink 约定）。本类不
 *   持有磁盘句柄，析构无副作用。
 */

#ifndef SDURWS_IRD_PROJECT_SRC_TXENGINE_HPP
#define SDURWS_IRD_PROJECT_SRC_TXENGINE_HPP

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <sdurws/ird/core/Events.hpp>
#include <sdurws/ird/project/PersistenceFormat.hpp>
#include <sdurws/ird/project/StoreTypes.hpp>

#include "RevisionIndex.hpp"
#include "ObjectStore.hpp"
#include "win32/IFileOps.hpp"

namespace sdurws::ird::project::tx {

// =====================================================================
// 值类型：提交计划 / 提交结果 / 恢复扫描报告
// =====================================================================

/**
 * @brief 事务待发布的新对象（域负载＋身份＋类型 token 的三元组）。
 *
 * 背景说明：oid 由调用方分配（HandlerContext——§6.5，域对象身份分配归
 * 命令阶段）；负载为处理器交付的 canonical 字节（D-10：引擎原样存储，
 * 零解释零信封）；objectTypeToken 随引用登记进修订清单（§4.4.2，
 * 对象文件内不自述类型）。
 *
 * 线程安全：纯值类型。
 */
struct PlannedObject {
    /// 对象身份（obj- 规范文本；须有效——全零保留值＝调用方契约违约）。
    core::ObjectId oid{};
    /// 对象类型 token（域登记，非空；如 "RobotDesign"；元数据对象除外——
    /// 元数据对象的引用由引擎自行装配，不经本结构）。
    std::string objectTypeToken;
    /// 对象负载原样字节（canonical；长度 0 合法＝空负载对象）。
    std::vector<std::uint8_t> payload;
};

/**
 * @brief 元数据对象的稳定类型 token（引擎登记进 objectRefs 的
 *        objectTypeToken 字段值）。
 *
 * 实现口径登记（DTB §5.4）：§4.4.2 的 objectTypeToken 为"域登记的稳定
 * token"，元数据对象是 project 自有对象（§4.2 右列"既是对象负载又是自有
 * 格式"），其 token 由本单元登记为 "ProjectMetadata"（PascalCase 与域
 * token 示例同形态）。该值随修订清单持久化，一经发布不可更改（清单只增
 * 不改——PA-2），后续如需调整属格式演进议题。
 */
inline constexpr const char* kMetadataTypeToken = "ProjectMetadata";

/**
 * @brief 提交计划——一次事务的全部调用方输入（命令服务 S6 入口装配）。
 *
 * 背景说明：candidate 元数据**不在计划内**——按 RevisionIndex 的 S6 时序
 * 契约（"提交时由事务引擎按 S6 时序注册新修订、derive＋validate 后切换
 * 权威"），派生（deriveNextMetadata）与发布边界校验由引擎在事务内存段
 * 执行，计划只携带**显式声明的增量**（MetadataDelta：活动分支 tip 更新
 * ＋新分支条目）。这保证了 INV-M3 的机制化入口唯一（D-5：底本拷贝只经
 * deriveNextMetadata）。
 *
 * 线程安全：纯值类型。
 */
struct CommitPlan {
    /// 本次提交的新修订身份（rev- 规范文本；调用方经 RevisionId::generate()
    /// 生成——实现口径登记（DTB §5.4）：修订身份的分配点＝命令服务 S6
    /// 计划装配（单一分配点），引擎第 1 步消费该身份并分配单调序号；
    /// 派生/校验/清单/HEAD 四处引用同一身份，由引擎提前校验一致性
    /// （tipUpdate.newTip 必须等于本字段，validateMetadataPublish 为
    /// 二道防线）。须有效且未随任何已注册修订出现过。
    RevisionId revisionId{};
    /// 本次提交的活动分支（brn- 规范文本；必须存在于权威元数据分支表——
    /// deriveNextMetadata 的底本约束）。
    BranchId branchId{};
    /// 分支 tip 维度的父修订（§4.5；首修订无＝nullopt。惯例＝活动分支
    /// 当前 tip，由调用方自 HEAD 引用的权威元数据读取——禁止经旧修订
    /// 元数据重建，§4.5.1 结论②）。
    std::optional<RevisionId> parentRevisionId;
    /// 提交时间，ISO-8601 UTC 文本（§4.4 约定；时钟供给归调用方——测试
    /// 可注入固定时刻保证确定性）。非空。
    std::string committedAtUtc;
    /// 本次新引入的域对象（可为空列表——纯元数据命令如 CreateBranch；
    /// 元数据对象不计入，由引擎自行生成与发布）。
    std::vector<PlannedObject> newObjects;
    /// 元数据增量（D-5 显式声明制：tip 更新＋新增分支；CreateBranch 的
    /// tip 语义经 addedBranches 条目自带 base/tip 表达）。
    revindex::MetadataDelta metadataDelta;
    /// 命令留痕（§4.4.4；project 原样持久化不解释——D-10）。commandType
    /// 须匹配 ^[a-z0-9-]{3,64}（Codec 解析同级校验——测试样本不含点，
    /// P-PR-9 裁决前口径）。
    CommandRecord command;
};

/**
 * @brief 提交结果——事务成功后的身份与持久化锚点（供命令服务组装事件
 *        与返回值；PRJ-T10 消费）。
 *
 * 线程安全：纯值类型。
 */
struct CommitResult {
    /// 本次提交的新修订身份（rev- 规范文本；引擎第 1 步生成）。
    RevisionId revisionId{};
    /// 本次修订的单调序号（＝权威 HEAD 的 seq+1；O-15）。
    std::uint64_t revisionSeq = 0;
    /// 新权威元数据的对象引用键（＝修订清单 metadataRef）。
    ObjectRefPair metadataRef{};
    /// 修订清单字节摘要（64 小写 hex；已随 HEAD 持久化——§4.4.1）。
    std::string manifestDigest;
    /// 已切换的 HEAD 内容（切换后重读自校验通过的字节级事实）。
    HeadRecord newHead;
};

/**
 * @brief 恢复扫描报告（§7.4 恢复顺序①④⑤的事务相关产出）。
 *
 * 背景说明：§5.1 的 RecoveryReport（含 orphanDraftFiles 等草稿字段）归
 * PRJ-T08 打开协议落位（StoreTypes.hpp 增量登记纪律：不预建无消费者
 * 接口）；本结构是其**事务半区**的数据源——ignoredStagingTxs 字段名与
 * RecoveryReport.ignoredStagingTxs 一一对应（acceptance 1 的恢复诊断
 * 载体），PRJ-T08 装配时逐字段搬运。三个清单均为确定性排序
 * （NFR-COR-02：staging/uncommitted 按名称字典序）——同磁盘状态必得
 * 同报告，验收可逐元素比对。
 *
 * 线程安全：纯值类型。
 */
struct TxRecoveryScan {
    /// 未提交事务的 .staging/<tx-id> 目录名清单（§7.4①：忽略不删＋恢复
    /// 诊断 PRJ-RECOVERY-IGNORED-UNCOMMITTED；现场保留供诊断——§4.1
    /// .staging 行"启动扫描忽略"口径）。
    std::vector<std::string> ignoredStagingTxs;
    /// 闭包外已发布修订目录的修订身份清单（§7.3：发布后未切 HEAD 的
    /// 残留——对查询/历史浏览不可见；只报告不删除）。
    std::vector<RevisionId> uncommittedRevisions;
    /// 悬挂对象计数（§7.4⑤：闭包外对象——开发诊断只读计数，不删；
    /// GC 范围外，PM-08-S1/R2 只读报告的计数源）。
    std::uint64_t danglingObjectCount = 0;
    /// HEAD 引用闭包完整性结论（§7.4④）：true＝闭包全部清单/对象可读
    /// 且 size＋SHA-256 校验通过；false＝存在缺失/损坏（定位细节经开发
    /// 诊断上报，用户级诊断码 PRJ-STORE-CORRUPT）。
    bool headIntegrityVerified = false;
};

// =====================================================================
// TxEngine——七步事务引擎门面
// =====================================================================

/**
 * @brief 事务引擎——七步提交协议、.staging 暂存与恢复扫描的唯一实现
 *        （§7；CommandServiceImpl/ProjectStoreImpl 的内部部件，不进公共
 *        头——消费者在本单元内部，R-2）。
 *
 * 生命周期与所有权：由存储上下文持有（每上下文一个实例，与 ObjectStore/
 * RevisionIndex 同生命周期——三个部件绑定同一项目根，构造期约定）。
 */
class TxEngine {
public:
    /**
     * @brief 构造事务引擎（绑定项目根与三个协作部件，注入事件总线与
     *        诊断 sink）。
     *
     * @param fileOps      [in] 文件/目录操作接缝（非 owning；**不得为空**
     *                     ——空接缝＝装配违约，fail-fast）
     * @param projectDir   [in] 项目存储根（.rwdesign 目录绝对路径；HEAD/
     *                     objects/revisions/.staging 均在其下）
     * @param objects      [in] 对象库（非 owning；不得为空——共享判定与
     *                     恢复扫描的读回校验通道）。须与 projectDir 绑定
     *                     同一项目（objects 目录＝projectDir/objects）
     * @param index        [in] 修订索引（非 owning；不得为空——S6 注册/
     *                     校验/序号分配的语义载体）。调用方须已完成打开
     *                     装载（权威元数据与闭包修订已注册、序号水位已
     *                     adoptHeadSeq）
     * @param eventBus     [in] 事件总线（非 owning；可空＝跳过第 6 步——
     *                     §5.1 装配可空口径）
     * @param sink         [in] 诊断 sink（非 owning；可空＝丢弃诊断——
     *                     §5.0 sink 约定）
     *
     * @throws std::invalid_argument fileOps/objects/index 之一为空
     */
    TxEngine(win32::IFileOps* fileOps,
             std::filesystem::path projectDir,
             objstore::ObjectStore* objects,
             revindex::RevisionIndex* index,
             core::IDomainEventBus* eventBus = nullptr,
             IDiagnosticsSink* sink = nullptr);

    // ---- 七步提交协议（§7.1；写路径——调用方串行化，§6.1 命令执行槽） ----

    /**
     * @brief 执行一次完整的七步提交事务（§7.1 逐步表的本类落地）。
     *
     * 执行序（各步失败语义见 @return/@throws 与类头"错误语义"）：
     *   - 内存段（零磁盘副作用，先于一切发布动作——可失败操作前置）：
     *       计划字段校验（含 plan.revisionId 与 tipUpdate.newTip 的一致性）
     *       → tx-id/序号分配（第 1 步内存半边，O-15：序号分配在 S6 决策
     *       点＝进入本事务之后）→ deriveNextMetadata 派生候选（D-5）→
     *       index.registerRevision（PRJ-T06 时序契约：先注册后校验）→
     *       validateMetadataPublish（INV-M1/M2/M3 发布边界）→
     *       registerMetadata（"切换权威"——S6 时序第三步，闭包可达前提）
     *       → 清单/引用集装配与摘要计算；
     *   - 第 1 步磁盘半边：建 `.staging/<tx-id>/`（含 tmp 子目录——D-09
     *     临时产物落点，§7.1 第 7 步"含 tmp"）；失败→环境码（无残留或仅
     *     空目录，启动清理兜底）；
     *   - 第 2 步暂存：对象/元数据/清单/命令全量写入暂存区（write-through
     *     ＋flush 闸门；失败残留于 .staging——设计行为，恢复扫描忽略）；
     *   - 第 3 步验证：重读全部暂存文件，size＋SHA-256 全量校验（对象
     *     字节→ContentVersion 名一致性含在内）；失败→StoreCorrupt（含
     *     路径定位＋开发诊断）；
     *   - 第 4 步内容发布：对象先行、修订清单最后（command.json 先于
     *     manifest.json——清单是目录内容的引用者，最后就位；"清单在而
     *     对象缺"的中间可见态因此不可达）；目标已存在→共享判定（读回
     *     校验一致＝共享，不一致＝StoreCorrupt）；某项失败→已就位项保留
     *     （悬挂，不回滚——旧版本完整性不受影响）；
     *   - 第 5 步提交指针切换：head.new 暂存写→MoveFileExW 原子替换
     *     HEAD（**唯一提交点**）→重读 HEAD 自校验 manifestDigest；
     *   - 第 6 步事件发布：RevisionCommitted→DependencyInvalidated
     *     （FIFO；单事件失败重试一次＋开发诊断，再失败不回滚——D-18）；
     *   - 第 7 步清理：删除 `.staging/<tx-id>/`（含 tmp）；失败仅开发
     *     诊断（已提交状态不受影响）。
     *
     * @param plan              [in] 提交计划（字段约束见 CommitPlan）
     * @param currentHead       [in] 当前 HEAD 内容（调用方自磁盘读取——
     *                          经 readHead()；projectId/revisionSeq 的
     *                          权威来源。引擎不重读以保持"打开时装载的
     *                          序号水位与 HEAD 一致"的会话纪律）
     * @param authoritative     [in] HEAD 引用的权威元数据版本（§4.5.1
     *                          结论②：禁止传父修订引用的旧版本）
     * @param authoritativeRef  [in] 权威版本的注册键；须已注册进 index
     *                          且内容与 authoritative 全等（RevisionIndex
     *                          时序契约，违者 invalid_argument）
     *
     * @return 提交结果（CommitResult；此时 HEAD 已切换、暂存区已清理）
     *
     * @throws std::invalid_argument 计划字段违约 / authoritativeRef 未
     *         注册或内容不符 / candidateRef 撞已注册键（内存段，零磁盘
     *         副作用）；fileOps 空接缝已由构造期拒绝
     * @throws StoreError BranchMetadataRegression（发布边界拒绝——INV-M1/
     *         M2/M3；零磁盘副作用）；DiskFull/AccessDenied/WriteRejected
     *         （第 1/2/4/5 步环境失败——失败残留按各步语义保留）；
     *         StoreCorrupt（第 3 步验证不符 / 第 5 步 HEAD 自校验失败
     *         ——后者属"已提交但自校验失败"的极端态，D-18 不回滚，恢复
     *         扫描④兜底）
     *
     * 复杂度：O(总暂存字节)（一遍写＋一遍校验读）＋ O(对象数) 次 rename。
     */
    CommitResult commit(const CommitPlan& plan,
                        const HeadRecord& currentHead,
                        const ProjectMetadataRecord& authoritative,
                        const ObjectRefPair& authoritativeRef);

    // ---- 恢复扫描（§7.3/§7.4①④⑤；打开/恢复时点调用——与 commit 互斥） ----

    /**
     * @brief 恢复扫描：未提交暂存识别＋闭包完整性校验＋悬挂计数
     *        （§7.4 恢复顺序的事务半区；PM-08 的数据源）。
     *
     * 扫描内容（②锁残留归 StoreLock/PRJ-T03、③孤儿草稿归 DraftService/
     * PRJ-T12，不在本方法）：
     *   - ① `.staging/` 子目录扫描：每个 tx 目录＝未提交事务 → 记入
     *     ignoredStagingTxs＋用户级诊断 PRJ-RECOVERY-IGNORED-UNCOMMITTED
     *     （**忽略不删**——现场保留，§4.1 .staging 行口径）；`.staging/tmp`
     *     → 启动清理（整树删除，按需重建——§4.1 tmp 行"启动清理"，D-09
     *     临时物不得逸出该目录）；
     *   - ④ HEAD 引用闭包完整性：读 HEAD → 装载磁盘全部修订清单入临时
     *     索引 → committedClosure（悬挂引用/起点缺失＝损坏）→ 对闭包内
     *     全部对象引用做 size＋SHA-256 校验（scanObjects verifyDigest 档）
     *     → 任一失败＝headIntegrityVerified=false＋PRJ-STORE-CORRUPT＋
     *     开发诊断（定位到文件）；
     *   - §7.3 识别：磁盘修订目录集 ＼ 闭包集 ＝ uncommittedRevisions
     *     （已发布但未提交——对查询不可见的机制化清单）；
     *   - ⑤ 悬挂对象计数（scanObjects 磁盘→引用方向）→
     *     danglingObjectCount＋开发诊断（只读不删）。
     *
     * 不抛数据侧错误（损坏进报告与诊断——一次扫描产出完整清单）；仅
     * projectDir 缺失等装配级问题以返回"空报告＋headIntegrityVerified=
     * false"表达（HEAD 缺失＝§4.1"损坏/缺失＝CorruptStoreDetected"，
     * 诊断码同落 PRJ-STORE-CORRUPT）。
     *
     * @return 恢复扫描报告（清单确定性排序——NFR-COR-02）
     */
    TxRecoveryScan scanForRecovery();

    // ---- 读取辅助（打开协议/测试消费；并发只读安全） ----

    /**
     * @brief 读取并解析 HEAD 文件（§4.4.1）。
     *
     * @return HEAD 记录（parseHeadRecord 校验后的结构）
     *
     * @throws StoreError StoreCorrupt（HEAD 缺失——§4.1"损坏/缺失＝
     *         CorruptStoreDetected"；或字节非法/版本不符——解析拒绝）
     */
    HeadRecord readHead() const;

    /// 项目存储根（构造固定；.rwdesign 目录绝对路径）。
    const std::filesystem::path& projectDir() const noexcept { return m_projectDir; }

private:
    // ---- 路径派生（全部落点在 §4.1 白名单内——NFR-SEC-01 消费侧） ----

    /// 暂存区根：projectDir/.staging。
    std::filesystem::path stagingRoot() const;
    /// 对象区根：projectDir/objects（与 ObjectStore 的绑定约定一致）。
    std::filesystem::path objectsRoot() const;
    /// 修订区根：projectDir/revisions。
    std::filesystem::path revisionsRoot() const;
    /// 修订目录：revisions/<rev-id 规范文本>（§4.1 命名规则，含 rev- tag）。
    std::filesystem::path revisionDir(const RevisionId& id) const;
    /// 对象文件：objects/<oid 规范文本>/<cv hex 64>（与 ObjectStore 编址
    /// 同源——core toCanonical 派生，本地零第二格式化）。
    std::filesystem::path objectFile(const core::ObjectId& oid,
                                     const core::ContentVersion& cv) const;

    // ---- 七步协议的私有步骤（commit() 的线性编排体） ----

    /**
     * @brief 生成事务 id（§4.1：进程内单调＋随机后缀）。
     * @return "tx-<单调序号>-<8 字节随机 hex>"（目录名安全字符集）
     */
    std::string generateTxId();

    /**
     * @brief 第 4 步单对象发布：暂存文件→objects/<oid>/<cv> 只增就位。
     *
     * 目标已存在→经 ObjectStore 读回校验：一致＝共享（暂存件留给第 7 步
     * 一并清理）；不一致＝StoreCorrupt。EXISTS→消失竞态有界重试 1 次
     * （与 ObjectStore::publishObject 同口径）。失败→环境码上抛（已就位
     * 项保留——悬挂语义由调用方协议保证，本方法不回滚）。
     *
     * @param stagedPath [in] 暂存文件全路径（已过第 2/3 步闸门）
     * @param oid        [in] 对象身份（有效）
     * @param cv         [in] 内容版本（有效；与暂存字节一致——第 3 步已验）
     */
    void publishOneObject(const std::filesystem::path& stagedPath,
                          const core::ObjectId& oid,
                          const core::ContentVersion& cv);

    /**
     * @brief 第 6 步单事件发布：失败重试一次＋开发诊断（D-18 不回滚）。
     *
     * @param event [in] 待发布事件（工厂构造，kind 与 payload 一致）
     * @param what  [in] 事件语义名（开发诊断文本用，如 "revision-committed"）
     */
    void publishEventWithRetry(const core::DomainEvent& event, const char* what);

    /**
     * @brief 开发级诊断出口（channel 约定前缀 "project/tx-engine"）。
     */
    void reportDev(const std::string& message) const;

    // ---- 注入协作部件（全部非 owning——生存期由持有方保证，见类头） ----
    win32::IFileOps* m_fileOps;                  ///< 文件/目录操作接缝
    std::filesystem::path m_projectDir;          ///< 项目存储根（.rwdesign）
    objstore::ObjectStore* m_objects;            ///< 对象库（共享判定/读校验）
    revindex::RevisionIndex* m_index;            ///< 修订索引（S6 语义）
    core::IDomainEventBus* m_eventBus;           ///< 事件总线（可空）
    IDiagnosticsSink* m_sink;                    ///< 诊断 sink（可空）

    /// tx-id 单调序号（进程内原子递增；仅保证编号唯一，非并发许可）。
    std::atomic<std::uint64_t> m_txSeq{0};
};

}  // namespace sdurws::ird::project::tx

#endif  // SDURWS_IRD_PROJECT_SRC_TXENGINE_HPP
