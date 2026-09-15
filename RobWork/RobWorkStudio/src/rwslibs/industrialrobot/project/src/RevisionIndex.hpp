/**
 * @file   RevisionIndex.hpp
 * @brief  修订索引（RevisionIndex）——修订 DAG／元数据谱系／分支表的进程内
 *         语义模型：提交闭包双通道（D-4）、分支表防回退（INV-M3）、谱系
 *         无环与单调（INV-M2）、修订单调序号分配（O-15）、权威元数据底本
 *         拷贝更新（D-5）。私有实现头，不出 include/（R-2 纪律）。
 *
 * 设计依据：
 *   - units/project.md §4.3（对象/修订/元数据/HEAD 引用关系图——四个身份
 *     概念互不混用）、§4.5（修订 DAG、分支模型与提交闭包——三种"修订
 *     指针"的区别、闭包双通道伪码、INV-M3 防分支表回退不变量、切换分支
 *     零写入）、§4.5.1（分支走查七步——本类语义的走查基准场景）、§4.4.1/
 *     §4.4.2/§4.4.3（HeadRecord/RevisionManifest/ProjectMetadataRecord
 *     字段级契约与 INV-M1/M2 不变量）、§15.1 D-4（闭包＝修订 DAG∪元数据
 *     谱系双通道——单通道会把 CreateBranch 修订判为孤岛）、D-5（新元数据
 *     一律以 HEAD 引用权威版本为底本拷贝更新——INV-M3 的机制化）、D-06
 *     （HEAD.revisionSeq 服务断点恢复）、§6.1（修订序号在 S6 内分配＝权威
 *     HEAD 的 seq+1）、§3.4（源码目录布局 src/RevisionIndex.{hpp,cpp}＝
 *     "修订闭包/分支表/元数据谱系"）；
 *   - 需求 ARC-01（修订历史可追溯——seq 单调、闭包完整）、PM-12（分支/
 *     元数据模型——baseRevisionId 引用不复制对象）、PA-2（不可变历史——
 *     D-5 是其机制化：写路径只增新元数据版本，绝不经父修订元数据重建）；
 *   - 任务契约 tasks/foundation/PRJ-T06.json acceptance 1～4（≙WP-04-T06）。
 *
 * 背景说明（本类在单元内的位置——"模型部分"边界）：
 *   本类是**纯进程内语义模型**：只维护"从磁盘装载了什么"的索引与在其上
 *   的推导/校验，**不做任何磁盘 I/O**——文件读写归 TxEngine（PRJ-T07）与
 *   ProjectStoreImpl（PRJ-T08），canonical 编解码归 Codec（PRJ-T04）。调用
 *   方（打开协议/恢复扫描/事务引擎）把从磁盘 parse 出的 RevisionManifest
 *   与 ProjectMetadataRecord 喂入 registerRevision/registerMetadata，再消费
 *   闭包/校验/分配三类语义。这样切分使 §4.5 的全部不变量可以在无磁盘
 *   env 的模型测试中逐条走查（PRJ-TX-5 模型部分/PRJ-TX-12 的验证载体）。
 *
 * 三类语义与验收条目的对应（契约 acceptance 1～4）：
 *   1. 提交闭包 committedClosure()——D-4 双通道（acceptance 2 前半：孤岛
 *      判定不出现；§4.5.1 结论③④）；
 *   2. 发布边界校验 validateMetadataPublish()——INV-M1/M2/M3（acceptance 2
 *      后半：谱系成环/分支表回退注入在发布边界拒绝，稳定码
 *      branch-metadata-regression）；
 *   3. 权威元数据底本拷贝更新 deriveNextMetadata()＋修订单调序号分配
 *      allocateRevisionSeq()（acceptance 3：O-15/D-5 落地）。
 *   acceptance 1（§4.5.1 七步走查、PM-12 baseRevisionId 不复制对象）由
 *   test/RevisionIndexTest.cpp 的走查用例组在本类 API 上逐步断言。
 *
 * P-PR-1 处置（acceptance 4）：身份类型消费以 core.md v0.1 为基线——本类
 *   的 RevisionId/BranchId/ObjectId/ContentVersion 全部来自 core 公共契约
 *   头（Identity.hpp/Digest.hpp，经 PersistenceFormat.hpp 的 using 声明），
 *   零 core 修改、零本地第二身份格式化；core 冻结出 diff 后按影响面增量
 *   同步（include 面红线由 BuildRedLineTest/LinkageContractTest 持续扫描）。
 *
 * P-PR-8 处置（acceptance 4）：分支 label 创建时一次写入、不可改名——本类
 *   **不提供任何改名入口**（deriveNextMetadata 逐字拷贝既有条目的 label/
 *   baseRevisionId/createdAtUtc；validateMetadataPublish 把"既有条目这三
 *   字段被改动"判为 branch-metadata-regression 拒绝——双重防线）。PM 明确
 *   不做分支改名；若需可编辑方案名走需求变更裁决，本类不私裁。
 *
 * 错误语义（错误二分，AGENTS §3/单元卡 §5.0）：
 *   - 调用方错误 → std::invalid_argument fail-fast：无效（全零）身份、
 *     修订清单与元数据记录的字段构造违约、deriveNextMetadata 的增量引用
 *     了分支表中不存在的分支、validateMetadataPublish 的喂入顺序违约
 *     （新修订/权威版本未按契约先注册）。
 *   - 数据侧错误 → StoreError(StoreCorrupt)：闭包行走遇到的悬挂引用
 *     （parent/metadataRef/supersedes/committedBy 指向未注册条目——调用方
 *     已声称装载了全部磁盘内容，缺失即损坏）、同一身份的冲突重复注册。
 *   - 不变量违约（发布边界防御）→ StoreError(BranchMetadataRegression)：
 *     INV-M1（branchId 唯一/tip 指向已存在修订）、INV-M2（谱系无环/版本
 *     单调）、INV-M3（分支表超集且既有 tip 不回退）、D-5 链约束（新版本
 *     必须以权威版本为 supersedes 前驱）。§4.4.8 将
 *     branch-metadata-regression 登记为"防御性，§4.5 不变量"专用码。
 *
 * 线程约束：读路径（tryRevision/tryMetadata/committedClosure/
 *   validateMetadataPublish/lastRevisionSeq）并发安全（内部互斥——§4.7
 *   "查询端口全部方法线程安全"的模型层同款纪律）；写路径（register 系、
 *   allocateRevisionSeq/adoptHeadSeq）由调用方串行化——写权威唯一归命令
 *   执行槽（§6.1"每存储上下文一个命令执行槽"），互斥仅保证记账一致。
 *
 * 所有权与生命周期：由存储上下文持有（每上下文一个实例，PRJ-T08 载体）；
 *   不持有任何磁盘句柄与外部资源，析构无副作用。注册进索引的清单/记录
 *   一律深拷贝存储——调用方随后修改自己的副本不影响索引（索引内容一经
 *   注册不可变，PA-2）。
 */

#ifndef SDURWS_IRD_PROJECT_SRC_REVISIONINDEX_HPP
#define SDURWS_IRD_PROJECT_SRC_REVISIONINDEX_HPP

#include <cstdint>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <sdurws/ird/project/PersistenceFormat.hpp>
#include <sdurws/ird/project/StoreTypes.hpp>

namespace sdurws::ird::project::revindex {

// =====================================================================
// 值类型：元数据引用键 / 闭包结果 / 元数据增量
// =====================================================================

/**
 * @brief 元数据引用键的哈希子（std::unordered_map/set 键用）。
 *
 * ObjectRefPair（{ObjectId, ContentVersion}）作为元数据谱系的注册键：
 * 元数据以对象文件形态存于 objects/，(oid,cv) 即其唯一编址（§4.3 关系图
 * metadataRef/supersedes 的指向形态）。组合式与 ObjectStore::ObjectKeyHash
 * 同款：两成员各取 core 提供的 std::hash，按 boost::hash_combine 黄金比例
 * 常数 0x9e3779b97f4a7c15（2^64/φ）移位相乘后异或。该常数只影响散列
 * 分布，不影响语义。
 */
struct MetadataRefHash {
    std::size_t operator()(const ObjectRefPair& ref) const noexcept
    {
        std::size_t h1 = std::hash<core::ObjectId>{}(ref.objectId);
        std::size_t h2 = std::hash<core::ContentVersion>{}(ref.contentVersion);
        return h1 ^ (h2 + 0x9e3779b97f4a7c15ull + (h1 << 6) + (h1 >> 2));
    }
};

/**
 * @brief 提交闭包结果（§4.5 闭包规则的双通道可达集）。
 *
 * 背景说明：调用方（打开协议/恢复扫描 PRJ-T08、历史浏览 PRJ-T09）拿
 * revisions 与磁盘 revisions/ 目录清单做差——差集＝未提交残留（§4.5.1
 * 结论④）；拿 metadata 与对象引用装配做差——差集内的对象＝悬挂候选。
 * 两个列表均为**确定性排序**（NFR-COR-02 稳定序）：revisions 按
 * (revisionSeq 升序, id 规范文本升序)；metadata 按 (oid 规范文本, cv 规范
 * 文本) 升序——同输入必得同输出，测试与调用方都可直接逐元素比对。
 *
 * 线程安全：纯值类型。
 */
struct CommittedClosure {
    /// 闭包覆盖的修订集（通道一 parent 链∪通道二谱系 committedBy 回连；
    /// 按上文确定性序）。
    std::vector<RevisionId> revisions;
    /// 闭包覆盖的元数据版本集（沿 supersedes 谱系；按上文确定性序）。
    std::vector<ObjectRefPair> metadata;
};

/**
 * @brief 活动分支 tip 更新（deriveNextMetadata 增量的显式声明部分）。
 *
 * 背景说明：§4.5 三种"修订指针"表——"分支当前修订（tip）随该分支上的
 * 每次提交更新（经新元数据版本）"。提交命令在 S3 由处理器声明本次提交
 * 的活动分支与新 tip（＝新修订）；显式声明制使"哪些字段变了"成为调用方
 * 契约的一部分，derive 只按声明更新、绝不推测（D-5 底本拷贝的字面执行）。
 *
 * 线程安全：纯值类型。
 */
struct TipUpdate {
    /// 被更新 tip 的分支（brn- 规范文本；必须存在于权威分支表——§6.1 S1
    /// "branch 存在于权威元数据"已先行校验，此处再验属防御）。
    BranchId branchId{};
    /// 该分支的新 tip（rev- 规范文本；惯例＝本次新修订）。
    RevisionId newTip{};
};

/**
 * @brief 元数据增量（deriveNextMetadata 的变更声明；§4.5 INV-M3 原文
 *        "仅更新活动分支的 tipRevisionId（及命令显式声明的分支表变更，
 *        如 CreateBranch 增加条目）"的参数化形态）。
 *
 * 线程安全：纯值类型。
 */
struct MetadataDelta {
    /// 活动分支 tip 更新（每次业务提交必有；纯元数据命令如 CreateBranch
    /// 的 tip 语义见 addedBranches——新条目自带 base/tip。可空＝本次提交
    /// 不更新任何既有分支 tip，如首个修订的创建场景）。
    std::optional<TipUpdate> tipUpdate;
    /// 显式声明的分支表新增条目（CreateBranch；条目的 label 在此一次写入
    /// ——P-PR-8。可为空列表）。
    std::vector<BranchRecord> addedBranches;
};

// =====================================================================
// RevisionIndex——修订/元数据/分支表语义门面
// =====================================================================

/**
 * @brief 修订索引——修订 DAG、元数据谱系与分支表的唯一进程内语义实现
 *        （§4.3/§4.5；事务引擎/打开协议/查询端口的内部部件，不进公共头
 *        ——消费者在本单元内部，R-2）。
 *
 * 生命周期与所有权：由 ProjectStoreImpl（PRJ-T08）按存储上下文持有——
 * 每存储上下文一个实例；打开/恢复时由调用方装载（register*），提交时由
 * 事务引擎（PRJ-T07）按 S6 时序注册新修订、derive＋validate 后切换权威。
 */
class RevisionIndex {
public:
    /**
     * @brief 构造空索引（序号水位 0；打开协议随后 adoptHeadSeq 恢复）。
     *
     * @throws 无（不做任何 I/O）
     */
    RevisionIndex() = default;

    // ---- 装载（打开协议/恢复扫描/事务提交喂入；写路径，调用方串行化） ----

    /**
     * @brief 注册一份修订清单（索引修订 DAG 的节点）。
     *
     * 幂等语义：同 id 且内容全等的重复注册＝静默成功（打开协议与恢复
     * 扫描可能重复喂入同一清单）；同 id 内容不同＝数据侧冲突（同一修订
     * 身份出现两个版本——存储损坏），抛 StoreCorrupt。
     *
     * @param manifest [in] 修订清单（须已经 Codec::parseRevisionManifest
     *                 校验——本方法只做身份/结构防御，不重复词法校验）。
     *                 深拷贝入索引。
     *
     * @throws std::invalid_argument revisionId 无效（全零）或 objectRefs
     *         为空（§4.4.2 ≥1 约束）或 metadataRef 身份无效——构造错误
     * @throws StoreError StoreCorrupt（同 id 已注册但内容不同——数据冲突）
     */
    void registerRevision(const RevisionManifest& manifest);

    /**
     * @brief 注册一份元数据记录（索引元数据谱系的节点）。
     *
     * @param ref    [in] 该元数据的对象引用键 {objectId, contentVersion}
     *               （与修订清单 metadataRef/supersedes 的指向形态一致）；
     *               两身份均须有效
     * @param record [in] 元数据记录（须已经 Codec::parseMetadataRecord
     *               校验）。深拷贝入索引。
     *
     * @throws std::invalid_argument ref 或 record.committedBy 含无效身份、
     *         record.branches 为空（§4.4.3 ≥1 约束）——构造错误
     * @throws StoreError StoreCorrupt（同 ref 已注册但内容不同——数据冲突）
     */
    void registerMetadata(const ObjectRefPair& ref, const ProjectMetadataRecord& record);

    // ---- 查询（读路径，并发安全；§4.7 模型层纪律） ----

    /**
     * @brief try 轨读取修订清单；未注册返回 nullopt 不抛。
     *
     * @param id [in] 修订身份（全零视为"未设置"→ nullopt，不视为错误）
     * @return 清单深拷贝（值语义——PA-3，与索引后续变化隔离）
     */
    std::optional<RevisionManifest> tryRevision(const RevisionId& id) const;

    /**
     * @brief try 轨读取元数据记录；未注册返回 nullopt 不抛。
     *
     * @param ref [in] 元数据对象引用键（任一身份全零 → nullopt）
     * @return 记录深拷贝（值语义）
     */
    std::optional<ProjectMetadataRecord> tryMetadata(const ObjectRefPair& ref) const;

    /**
     * @brief 从**给定元数据**读取分支 tip（§4.5.1 步骤 7 的机制化）。
     *
     * 背景说明（为什么是静态纯函数、为什么参数是"给定元数据"）：
     *   权威纪律（INV-M3/D-5）要求"查询分支 tip 一律取自 HEAD 引用的
     *   权威元数据版本，禁止经旧修订引用的元数据重建"——本方法把"从哪份
     *   元数据读"显式留给调用方，配合注释钉住调用契约：调用方必须传 HEAD
     *   引用版本。本类无法在运行时强制（它不知道哪份是权威），该纪律由
     *   走查用例（步骤 6/7）与单元卡 §4.5.1 结论②背书。
     *
     * @param metadata [in] 被查询的元数据（调用方契约：HEAD 引用权威版本）
     * @param branchId [in] 分支身份
     * @return 该分支 tip；分支不存在返回 nullopt（不抛——§5.2 查询语义）
     */
    static std::optional<RevisionId> branchTip(const ProjectMetadataRecord& metadata,
                                               const BranchId& branchId);

    // ---- 修订序号分配（O-15；§6.1 S6——写路径，命令执行槽内调用） ----

    /**
     * @brief 当前修订单调序号水位（已分配的最大 seq；无单位——纯计数）。
     */
    std::uint64_t lastRevisionSeq() const;

    /**
     * @brief 打开协议断点恢复：以 HEAD 记录的 revisionSeq 恢复水位（D-06）。
     *
     * 只升不降（取 max）：水位单调是 O-15 的结构前提；重开场景下新索引
     * 水位为 0，重复调用以更大者生效。
     *
     * @param headSeq [in] HEAD 记录的 revisionSeq（无单位——纯计数）
     */
    void adoptHeadSeq(std::uint64_t headSeq);

    /**
     * @brief 分配下一个修订序号（严格递增：返回值＝水位+1 并推进水位）。
     *
     * 背景说明（O-15 落地口径，§6.1）：修订序号在 S6（事务提交）内分配
     * ＝"权威 HEAD 的 seq+1"。事务失败路径不得消费序号——调用方（事务
     * 引擎）必须在提交决策点之后调用本方法；一旦调用即推进水位（进程内
     * 不回收）。跨会话的序号断点恢复归 adoptHeadSeq：崩溃残留修订（发布
     * 后未切 HEAD）的序号可能与后续成功提交的序号重合——二者不同时在
     * 闭包内，无歧义（§4.5.1 结论④），committed 历史的单调性不受影响。
     *
     * @return 新分配的序号（≥1）
     */
    std::uint64_t allocateRevisionSeq();

    // ---- D-5：权威元数据底本拷贝更新（静态纯函数，无锁无状态） ----

    /**
     * @brief 以权威元数据为底本拷贝出下一版本（D-5 的机制化入口）。
     *
     * 执行语义（§4.5 INV-M3 原文的逐字机制化）：
     *   1. 深拷贝 authoritative（分支表/显示名/主分支/schemeLabels 全部
     *      原样——既有条目的 label/base/createdAt 逐字保留，P-PR-8）；
     *   2. committedBy ← committedBy（＝本次新修订）；
     *   3. supersedes ← authoritativeRef（谱系链向权威版本收口——发布
     *      校验按同值核对）；
     *   4. 按 delta.tipUpdate 更新活动分支 tip（分支必须存在于底本）；
     *   5. 按 delta.addedBranches 追加新分支条目（branchId 不得与底本或
     *      本批次重复——CreateBranch 语义）。
     *
     * **不存在**"从父修订引用的元数据重建"的入口——那是 INV-M3 明令禁止
     * 的回退路径（§4.5.1 结论②）；若调用方绕过本函数手工以旧元数据为底
     * 本拼装候选，validateMetadataPublish 会以 branch-metadata-regression
     * 拒绝（走查用例以反证钉住）。
     *
     * @param authoritative   [in] HEAD 引用的权威元数据版本（调用方契约：
     *                        不得传父修订引用的旧版本——见上）
     * @param authoritativeRef [in] 权威版本的对象引用键（写入新版本
     *                        supersedes；须与权威版本实际注册键一致）
     * @param delta           [in] 显式声明的增量（见 MetadataDelta）
     * @param committedBy     [in] 提交本版本的修订 id（惯例＝本次新修订，
     *                        须有效；发布校验强制相等）
     *
     * @return 下一版本元数据（值语义；未做 INV-M1/M2/M3 校验——那是发布
     *         边界 validateMetadataPublish 的职责，本函数只做构造防御）
     *
     * @throws std::invalid_argument committedBy 无效；tipUpdate.branchId
     *         不在底本分支表中；tipUpdate.newTip 无效；addedBranches 含
     *         无效 branchId、与底本重复的 branchId 或本批次内重复——
     *         全部为调用方构造错误，fail-fast
     */
    static ProjectMetadataRecord deriveNextMetadata(
        const ProjectMetadataRecord& authoritative,
        const ObjectRefPair& authoritativeRef,
        const MetadataDelta& delta,
        const RevisionId& committedBy);

    // ---- 发布边界校验（INV-M1/M2/M3；const——并发安全） ----

    /**
     * @brief 元数据发布的边界校验（§4.5"发布期校验"的唯一实现）。
     *
     * 调用时序契约（事务引擎 S6 内，先注册后校验）：
     *   1. registerRevision(新修订清单)——校验需要沿新修订的 parent 链
     *      做"tip 不回退"的祖先判定、按其 revisionSeq 做谱系单调判定；
     *   2. 本方法（通过＝候选元数据可随 HEAD 切换成为新权威）。
     *
     * 校验清单（任一不过即抛 StoreError(BranchMetadataRegression)，detail
     * 携带 invariant 键值供开发诊断）：
     *   - INV-M1a：候选分支表非空（§4.4.3 ≥1）且 branchId 唯一；
     *   - INV-M1b：每个 tipRevisionId 指向已注册修订（含新修订——步骤 1
     *     已注册；§4.4.3"必须指向已存在修订"）；
     *   - committedBy 绑定：candidate.committedBy == newRevisionId（§4.4.3
     *     "提交本元数据版本的修订"——发布时即新修订）；
     *   - 无新版本拒绝：candidateRef == authoritativeRef（同内容同编址＝
     *     没有新元数据版本可发布——病态调用）；
     *   - D-5 链：candidate.supersedes 必须恰为 authoritativeRef（谱系向
     *     权威收口；缺失或指向他处＝谱系断裂）；
     *   - INV-M3 超集：权威分支表的每个 branchId 在候选中仍存在（分支
     *     丢失＝§4.5.1 结论①反例）；
     *   - P-PR-8/不可变条目：既有条目的 label/baseRevisionId/createdAtUtc
     *     与权威版本逐字相等（label 一次写入；base 创建时记录后不变；
     *     createdAt 同理）；
     *   - INV-M3 不回退：既有条目 tip 未变→通过；变更→必须恰为新修订、
     *     且该分支＝新修订所属分支、且权威旧 tip 是新修订的**真祖先**
     *     （沿 parent 链可达——推进而非回退/平移到无关修订）；
     *   - INV-M2 无环：候选 supersedes 谱系链（经注册记录行走）不得回到
     *     已访问版本（含 candidateRef 自身——自环/间接环一律拒绝）；
     *   - INV-M2 版本单调：谱系链上各版本的 committedBy 修订 seq 向权威
     *     方向严格递减（等价：越新的元数据由越新的修订提交——§4.4.3
     *     "supersedes 链全局无环、版本单调"）。
     *
     * @param authoritative    [in] HEAD 引用的权威元数据版本
     * @param authoritativeRef [in] 权威版本的注册键；必须已注册且内容与
     *                         authoritative 全等（喂入顺序违约＝调用方错误）
     * @param candidate        [in] 待发布的新元数据版本
     * @param candidateRef     [in] 候选版本的发布键（对象已发布的编址）；
     *                         不得已注册（同内容重复发布＝调用方错误）
     * @param newRevisionId    [in] 本次提交的新修订（须已注册）
     *
     * @throws std::invalid_argument 候选条目含无效（全零）身份；或时序
     *         契约违约：newRevisionId/authoritativeRef 未按契约先注册、
     *         authoritativeRef 注册内容与 authoritative 不全等、
     *         candidateRef 已注册——全部为调用方错误，fail-fast
     * @throws StoreError BranchMetadataRegression（上文校验清单任一不过
     *         ——不变量违约，发布边界拒绝）；StoreCorrupt（谱系行走遇到
     *         未注册的悬挂引用——喂入不完整的调用方错误按数据侧拒）
     */
    void validateMetadataPublish(const ProjectMetadataRecord& authoritative,
                                 const ObjectRefPair& authoritativeRef,
                                 const ProjectMetadataRecord& candidate,
                                 const ObjectRefPair& candidateRef,
                                 const RevisionId& newRevisionId) const;

    // ---- 提交闭包（D-4 双通道；读路径，并发安全） ----

    /**
     * @brief 计算自 headRevisionId 出发的提交闭包（§4.5 闭包规则）。
     *
     * 行走语义（§4.5 伪码的迭代化——深链安全，不用递归）：
     *   通道一：visit_rev(r) 标记 r，沿 parentRevisionId 上行；
     *   通道二：visit_rev 顺路 visit_meta(r.metadataRef)；visit_meta(M)
     *   标记 M 并**将其 committedBy 修订作为修订继续访问**（其 parent 链
     *   随之上行——§4.5.1 结论③"M3→M2(r2)→r2→r0"的行走形态），再沿
     *   M.supersedes 继续谱系。
     *
     * 双通道的必要性（D-4）：CreateBranch 修订（如走查中的 r1）既非任何
     * 分支 tip 也无后续修订以它为 parent——仅通道一它将成为孤岛被误判
     * 未提交；谱系通道（supersedes→committedBy）保证其可达。 acceptance 2
     * "孤岛判定不出现"即由本方法＋走查历史承载。
     *
     * 终止与环：访问集（revisions/metadata 各一）保证终止；已访问节点
     * 再次到达＝合流（DAG 菱形）而非错误，静默去重。环本身在发布边界被
     * INV-M2 拒绝、不可能进入已提交历史——闭包对"带环损坏存储"的行为
     * 是按访问集截断（不死循环），由此产生的残缺闭包会随后续的覆盖率
     * 核对暴露（调用方差异比对），此处不重复判定。
     *
     * @param headRevisionId [in] 闭包起点（HEAD.revisionId；须已注册）
     *
     * @return 双通道可达集（确定性排序，见 CommittedClosure 注释）
     *
     * @throws std::invalid_argument headRevisionId 无效（全零）
     * @throws StoreError StoreCorrupt：起点未注册，或行走遇到悬挂引用
     *         （parent/metadataRef/supersedes/committedBy 指向未注册条目
     *         ——调用方声称已装载全部磁盘内容，缺失即损坏）
     */
    CommittedClosure committedClosure(const RevisionId& headRevisionId) const;

private:
    /**
     * @brief 新修订是否为某修订的真祖先判定之逆：oldTip 是否沿 parent 链
     *        可达 newTip（INV-M3"不回退"的几何判据）。
     *
     * 调用方须持锁（私有约定：本方法不加锁，由公有入口包住）。行走遇
     * 未注册 parent → 抛 StoreCorrupt（悬挂引用）。
     *
     * @param oldTip [in] 权威分支表记录的旧 tip（已注册）
     * @param newTip [in] 候选 tip（已注册；等于 oldTip 时返回 false——
     *               "真祖先"不含自身，相等情形由调用方先行分流）
     * @return oldTip 出现在 newTip 的 parent 链上（深度 ≥1）
     */
    bool isProperAncestorLocked(const RevisionId& oldTip, const RevisionId& newTip) const;

    /// 修订 DAG 节点（id→清单；深拷贝存储，一经注册不可变——PA-2）。
    std::unordered_map<RevisionId, RevisionManifest, std::hash<RevisionId>> m_revisions;
    /// 元数据谱系节点（(oid,cv)→记录；深拷贝存储）。
    std::unordered_map<ObjectRefPair, ProjectMetadataRecord, MetadataRefHash> m_metadata;
    /// 修订单调序号水位（O-15； adoptHeadSeq 恢复/allocateRevisionSeq 推进）。
    std::uint64_t m_lastRevisionSeq = 0;

    /// 内部互斥（保护三成员；读路径并发安全、写路径调用方串行化＋本锁
    /// 保证记账一致——与 ObjectStore 同款纪律，文件头"线程约束"）。
    mutable std::mutex m_mutex;
};

}  // namespace sdurws::ird::project::revindex

#endif  // SDURWS_IRD_PROJECT_SRC_REVISIONINDEX_HPP
