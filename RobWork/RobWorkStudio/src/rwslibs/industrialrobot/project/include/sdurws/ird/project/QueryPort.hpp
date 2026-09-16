/**
 * @file   QueryPort.hpp
 * @brief  查询端口（IProjectQueryPort，②端口所有者）——修订/元数据/分支/
 *         对象/草稿/结果六类只读视图的唯一合法入口（PRJ-T09 落位）。
 *
 * 设计依据：
 *   - units/project.md §5.2（查询端口接口原文——本头的类型与方法签名逐条
 *     锚定该节代码块与契约表）、§4.5（修订 DAG/分支模型/提交闭包——
 *     "仅闭包内修订可取"与"权威＝HEAD 引用版本"两条纪律的出处）、
 *     §4.6（对象库与内容编址——tryObject 引用存在性语义、LRU ObjectCache、
 *     "查询快照与写入隔离"）、§4.7（只读查询的线程与生命周期契约——全部
 *     方法线程安全、context-closed 拒绝、只读实例同样提供查询）、§6.3
 *     （历史修订中未知命令类型 → hasUnresolvedPayload=true 的语义出处）、
 *     §9.8（线程模型总表——"调用方线程：查询端口并发只读"）；
 *   - 需求 ARC-02（端口协作——②查询端口的所有者在本单元，业务单元之间
 *     不私有互访，跨单元协作只走端口）、CON-01（分析从不可变快照运行——
 *     本端口是"快照读取"的持久化侧承接，§2.2 CON-01 行）、PA-2（不可变
 *     历史——修订视图一经返回不可变）、PA-3（读写无共享可变状态——返回
 *     视图全部为进程内值拷贝）；
 *   - 任务契约 tasks/foundation/PRJ-T09.json acceptance 1～4（并发只读、
 *     闭包外不可见隔离、②端口所有者语义齐备＋缓存预算生效、P-PR-1 处置）。
 *
 * 背景说明（为什么查询必须是独立端口——ARC-02 端口协作语义）：
 *   业务单元（modeling/evidence/reporting 等）需要读项目状态时，**不得**
 *   触碰存储内部（对象库/修订索引/HEAD 文件），只能经本端口取"值语义的
 *   只读快照"。这使"分析从不可变快照运行"（CON-01）成为结构事实：拿到的
 *   RevisionView/ProjectMetadataView 是深拷贝，此后任何提交都不改变已返回
 *   的视图；写入只增新内容，既有读者持有的引用永不被覆盖（PA-2/PA-3）。
 *
 * 增量落位说明（DTB §5.4 口径登记，三处）：
 *   1. `InverseRef`：§5.2 原文 `std::optional<InverseRef> inverse` 未给
 *      字段级定义。落位为磁盘契约 InverseCommand（PersistenceFormat.hpp
 *      §4.4.4 冻结类型）的 using 别名——逆命令表达只有一份契约（type/
 *      payloadFormatVersion/payloadCanonical），视图与磁盘不重复定义
 *      （与 PersistenceFormat.hpp 对 core 身份类型的 using 先例同口径）；
 *      撤销服务（PRJ-T13）消费 tip 修订视图的 inverse 组装 undo 信封
 *      （§5.5——"undo 取当前 tip 修订的 inverse 记录"）。
 *   2. `ProjectMetadataView`：§5.2 原文未给字段级定义。落位为
 *      {ObjectRefPair ref; ProjectMetadataRecord record;} 二字段——ref 是
 *      该元数据版本的对象引用键（(oid,cv) 编址，evidence 做内容比对用），
 *      record 复用 §4.4.3 冻结读取类型（不重定义字段，防两处漂移）。
 *      currentMetadata() 返回权威版本（ref＝HEAD 引用键）；metadataAt()
 *      返回指定历史修订引用的版本（历史浏览 R2——展示该修订时点的分支表
 *      快照，不是权威查询；权威纪律见 currentMetadata 注释）。
 *   3. `hasUnresolvedPayload` 判定：§6.3"未知命令类型时 payload 不解析"
 *      的判据是命令注册表（HandlerRegistry，PRJ-T10 落位）。本端口构造时
 *      接受可选判据（knownCommandType 判定函数，可空）；空＝注册表尚未
 *      装配，按 §5.2 字段默认值语义置 false（"无法判定"不等于"已知未知"
 *      ——不虚报 unresolved；T10 装配注册表后注入判据，届时真判定）。
 *      实现口径已随本任务登记于 units/project.md §12 v0.11。
 *
 * 线程模型（§4.7/§9.8）：全部方法可从任意线程并发调用（线程安全）；
 *   与提交（写路径）并发安全——HEAD/权威元数据的一致性窗口在实现内部与
 *   writer 互斥串行（可见性语义：查询只看到"已切换 HEAD"的完整提交，
 *   绝无"对象已发布但 HEAD 未切换"的中间态视图——§4.5.1 结论④）。
 *   返回值均为深拷贝/值语义，调用方自由持有，无共享可变状态（PA-3）。
 *
 * 生命周期：端口实例随存储上下文（ProjectStore::query() 返回的引用与
 *   上下文同生命周期）；上下文进入 Closed 后全部方法进入拒绝态（抛
 *   StoreError(ContextClosed)；noexcept 的 tryObject 以 nullopt 表达拒绝，
 *   见其注释）。上下文析构后本引用不可再用（常规 C++ 生存期纪律）。
 */

#ifndef SDURWS_IRD_PROJECT_QUERYPORT_HPP
#define SDURWS_IRD_PROJECT_QUERYPORT_HPP

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/project/PersistenceFormat.hpp>
#include <sdurws/ird/project/StoreTypes.hpp>

namespace sdurws::ird::project {

// =====================================================================
// 视图值类型（§5.2 原文形态；全部为纯值类型——深拷贝、可任意持有）
// =====================================================================

/**
 * @brief 逆命令引用（§5.2 RevisionView.inverse 的元素类型）。
 *
 * 落位口径（见文件头"增量落位说明 1"）：直接复用磁盘契约类型
 * InverseCommand（§4.4.4 冻结——commandType/payloadFormatVersion/
 * payloadCanonical 三元组，域所有、project 透传不解释——D-10）。
 * 语义：该修订的命令**可逆**时携带逆命令表达（撤销＝提交逆命令产生新
 * 修订，D-11——不改写历史）；不可逆命令（如 CreateBranch）为 nullopt
 * （可逆性是处理器声明的事实，§6.9）。
 */
using InverseRef = InverseCommand;

/**
 * @brief 修订视图（§5.2 原文形态）——一个已提交修订的只读值快照。
 *
 * 背景说明（"一经返回不可变"的两层保证，acceptance 1/PA-2）：
 *   1. 值语义：本结构所有字段都是深拷贝，调用方持有副本，与存储后续
 *      变化完全隔离（进程内值拷贝——§4.7 原文"返回视图为进程内值拷贝"）；
 *   2. 底层不可变：修订清单/命令留痕一经发布只增不改（revisions/ 目录
 *      只增不改——§4.1），同一修订在任何时刻查询结果全等。
 *
 * 线程安全：纯值类型（可任意拷贝/移动，无共享状态）。
 */
struct RevisionView {
    /// 修订身份（rev- 规范文本；core 强类型——P-PR-1 消费基线 v0.1 §4.1）。
    core::RevisionId id{};
    /// 修订单调序号（项目内严格递增，O-15；无单位——纯计数）。
    std::uint64_t seq = 0;
    /// 分支 tip 维度的父修订（§4.5）；首修订无（nullopt）。
    std::optional<core::RevisionId> parent;
    /// 提交时活动分支（brn- 规范文本）。
    core::BranchId branch{};
    /// 完整对象引用集（§4.4.2 ≥1，全量非增量——任一修订自足描述其完整
    /// 可见状态，"修订视图不可变"的数据基础；含对象类型 token 与负载
    /// 摘要 digest256）。
    std::vector<ObjectRef> objectRefs;
    /// 元数据对象的完整引用（§4.4.2 约束 objectRefs 必含 metadataRef
    /// 指向的元数据对象——本字段是其在 objectRefs 中的完整引用形态，
    /// 由实现从清单解析装配；结构缺失＝清单违约，实现按 store-corrupt
    /// 拒绝——防御性，正常装载路径不可达）。
    ObjectRef metadataRef{};
    /// 人读命令摘要（§4.4.4 CommandRecord.summary，处理器生成、project
    /// 原样持久化；PM-12-S1 历史浏览展示面）。
    std::string commandSummary;
    /// 逆命令表达（可逆命令才有——见 InverseRef 注释；§6.9 撤销语义）。
    std::optional<InverseRef> inverse;
    /// 未知命令类型标记（§6.3 末行：历史修订中的未知命令类型 → 修订
    /// 可读、payload 不解析、本标志置 true——历史浏览 PM-12-S1 不受
    /// 影响，浏览仅消费 summary 与对象集）。判定与注入口径见文件头
    /// "增量落位说明 3"。
    bool hasUnresolvedPayload = false;

    bool operator==(const RevisionView& o) const
    {
        return id == o.id && seq == o.seq && parent == o.parent
            && branch == o.branch && objectRefs == o.objectRefs
            && metadataRef == o.metadataRef && commandSummary == o.commandSummary
            && inverse == o.inverse && hasUnresolvedPayload == o.hasUnresolvedPayload;
    }
    bool operator!=(const RevisionView& o) const { return !(*this == o); }
};

/**
 * @brief 权威元数据视图（§5.2 ProjectMetadataView；字段级落位口径见
 *        文件头"增量落位说明 2"）。
 *
 * 背景说明（权威纪律 INV-M3）：分支 tip 的查询一律取自 HEAD 引用的权威
 * 元数据版本，禁止经历史修订引用的旧元数据重建分支表（§4.5.1 结论②——
 * "若用它即发生读取旧修订回退整个项目分支表"）。currentMetadata() 返回
 * 的永远是 HEAD 引用版本；metadataAt() 返回的**历史快照**只供该修订时点
 * 的展示（R2 历史浏览），消费方不得将其分支表当作当前权威使用（本注释
 * 即契约锚点）。
 *
 * 线程安全：纯值类型（record 为深拷贝——与权威元数据的后续前进隔离）。
 */
struct ProjectMetadataView {
    /// 元数据版本的对象引用键 {(oid),(cv)}——该版本元数据在对象库中的
    /// 编址（§4.3 关系图；evidence 内容比对、谱系追溯的关联键）。
    ObjectRefPair ref{};
    /// 元数据记录值拷贝（§4.4.3 冻结字段——分支表/显示名/主分支/
    /// schemeLabels；复用磁盘读取类型，不重定义——防两处契约漂移）。
    ProjectMetadataRecord record{};

    bool operator==(const ProjectMetadataView& o) const
    {
        return ref == o.ref && record == o.record;
    }
    bool operator!=(const ProjectMetadataView& o) const { return !(*this == o); }
};

/**
 * @brief 分支 tip 视图（§5.2 原文形态）——分支表条目的只读投影。
 *
 * 线程安全：纯值类型。
 */
struct BranchTip {
    /// 分支身份（brn- 规范文本）。
    core::BranchId id{};
    /// 分支显示名（创建时一次写入，P-PR-8——本端口无任何改名入口）。
    std::string label;
    /// 分支创建时的起点修订（rev-；创建后不变）。
    core::RevisionId base{};
    /// 该分支当前 tip 修订（rev-；取自 HEAD 引用的权威元数据——INV-M3）。
    core::RevisionId tip{};

    bool operator==(const BranchTip& o) const noexcept
    {
        return id == o.id && label == o.label && base == o.base && tip == o.tip;
    }
    bool operator!=(const BranchTip& o) const noexcept { return !(*this == o); }
};

/**
 * @brief 草稿清单条目（§5.2 listDrafts 返回元素；§5.2 注释"含 baseRevision
 *        与过期标记"）。
 *
 * 背景说明：草稿落盘不产生修订（PM-04"保存与应用分离"），本条目是
 * drafts/<branch-id>/ 目录下**当前有效**草稿文件（<module>.draft.json——
 * .new/.bak 崩溃残留不是当前草稿，§8.4，不列入）的只读信息投影。
 * stale＝草稿基线落后于分支当前 tip（应用会被拒——PM-04 stale 判据）。
 *
 * 与 §5.4 DraftProjectionItem 的关系：那是草稿服务（PRJ-T12）的多模块
 * 汇总投影形态（含 present 汇总位）；本条目是查询端口的磁盘清单投影，
 * 二者语义同源、各自落位（T12 消费本端口或磁盘自取随其任务登记）。
 *
 * 线程安全：纯值类型。
 */
struct DraftInfo {
    /// 模块 token（文件名 <module>.draft.json 的 module 部分；§4.1
    /// drafts 行命名规则；注册表校验归草稿服务 T12——本端口透传）。
    std::string moduleId;
    /// 草稿基线修订（rev-；≠分支 tip 时 stale=true）。
    core::RevisionId baseRevision{};
    /// 过期标记：true＝baseRevision 与分支当前 tip 不一致（应用被拒——
    /// PM-04）；分支不存在于权威元数据时也置 true（孤儿草稿保守标记，
    /// 不私裁丢弃——§7.4③ 的处置权在草稿服务/恢复流程）。
    bool stale = false;
    /// 草稿落盘时间，ISO-8601 UTC 文本（§4.4.5 savedAtUtc）。
    std::string savedAtUtc;
    /// 草稿来源（autosave/manual/apply-retained——§4.4.5 origin 冻结
    /// 三值；恢复横幅按此决定提示语，PM-15）。
    DraftOrigin origin = DraftOrigin::Autosave;

    bool operator==(const DraftInfo& o) const
    {
        return moduleId == o.moduleId && baseRevision == o.baseRevision
            && stale == o.stale && savedAtUtc == o.savedAtUtc && origin == o.origin;
    }
    bool operator!=(const DraftInfo& o) const { return !(*this == o); }
};

/**
 * @brief 运行清单条目（§5.2 listRuns 返回元素）。
 *
 * 背景说明（D-13"只认完整运行"）：results/<run-id>/ 目录只有 finalize
 * （manifest.json 原子发布）后才算完整运行（§10.1——分批写入期目录可见
 * 但不完整）；无 manifest 的目录＝未完成归档，查询不列入（§4.1 results
 * 行口径）。本条目字段取自 RunManifest（§4.4.7）——五元组与登记信息
 * 原样（execution 核验后归档的透传数据，project 不解释）。
 *
 * 线程安全：纯值类型。
 */
struct RunInfo {
    /// 运行身份（run- 规范文本；与所在目录名一致）。
    core::RunId runId{};
    /// 任务身份五元组（project/branch/revision/run/attempt——§4.4.7
    /// taskIdentity 原样；core 强类型，P-PR-1 消费基线）。
    core::TaskIdentity task{};
    /// 任务类型 token（execution 登记透传，如 "evaluation"）。
    std::string runKind;
    /// 评估键（execution 登记透传——evidence 当前性判定的关联键）。
    std::string evaluationKey;
    /// finalize 时间，ISO-8601 UTC 文本（manifest 发布时刻）。
    std::string finalizedAtUtc;

    bool operator==(const RunInfo& o) const
    {
        return runId == o.runId && task == o.task && runKind == o.runKind
            && evaluationKey == o.evaluationKey && finalizedAtUtc == o.finalizedAtUtc;
    }
    bool operator!=(const RunInfo& o) const { return !(*this == o); }
};

// =====================================================================
// IProjectQueryPort——②端口契约（§5.2 原文方法集）
// =====================================================================

/**
 * @brief 查询端口（②端口所有者，§5.2）——项目只读状态的唯一合法视图
 *        入口（经 ProjectStore::query() 获取实例引用）。
 *
 * 生命周期与所有权：实现实例由存储上下文持有并随其消亡；本接口不提供
 *   任何生命周期管理入口（上下文关闭/销毁语义见 ProjectStore）。消费方
 *   （evidence/reporting/modeling/ui）只持引用调用。
 *
 * 错误语义（错误二分，AGENTS §3/单元卡 §5.0）：
 *   - 调用方错误 → std::invalid_argument fail-fast（如 branchHistory 的
 *     未知分支——§5.2 表"未知分支抛"；无效全零身份）；
 *   - 状态错误 → StoreError(ContextClosed)（上下文 Closed 后全部方法
 *     拒绝——§4.7）；Draining（关闭排空中）**不拒**——查询是只读操作，
 *     关闭对话框场景（PM-03）需要在排空期呈现项目状态；
 *   - 数据侧错误 → StoreError(StoreCorrupt)（清单/命令留痕缺失或损坏、
 *     HEAD 悬挂指向、对象引用缺失〔§5.2 表"引用缺失＝store-corrupt"〕）；
 *   - 唯一例外：tryObject 为 noexcept（§5.2 签名原文），一切失败以
 *     nullopt 表达（含 ContextClosed 与数据侧损坏——损坏经开发诊断
 *     reportDev 上报，不静默；见其注释）。
 */
class IProjectQueryPort {
public:
    /// 虚析构：经接口引用消费、实现随存储上下文销毁的常规保障。
    virtual ~IProjectQueryPort() = default;

    /**
     * @brief HEAD 修订视图（§5.2 原文"重读 HEAD 文件"——每次调用读磁盘
     *        HEAD 取当前提交点的地面事实，不依赖端口缓存）。
     *
     * @return HEAD 修订的值快照（含命令摘要/逆命令/完整引用集）
     *
     * @throws StoreError ContextClosed（上下文已关闭）；StoreCorrupt
     *        （HEAD 文件缺失/损坏，或其指向修订不在会话闭包内——外部
     *         篡改/装载违约，数据侧）
     *
     * 线程安全：并发只读安全（与提交互斥的一致性窗口在实现内部）。
     */
    [[nodiscard]] virtual RevisionView head() const = 0;

    /**
     * @brief try 轨读取修订视图；闭包外/不存在返回 nullopt 不抛。
     *
     * 可见性纪律（§5.2 注释"仅闭包内修订可取"＋自审项 A-6"防误认已
     * 提交"）：会话索引只装载 HEAD 闭包内修订（打开协议装载纪律——
     * §7.3/PRJ-TX-9），提交事务中"已发布未切 HEAD"的修订被实现内部的
     * 一致性互斥挡住——二者结构性保证闭包外修订一律 nullopt（残留修订
     * 绝不以"可查到"的形态误认已提交）。
     *
     * @param id [in] 修订身份（全零＝未设置 → nullopt，不视为错误）
     * @return 修订视图值快照；闭包外/不存在＝nullopt
     *
     * @throws StoreError ContextClosed（上下文已关闭）；StoreCorrupt
     *         （命中修订的清单结构违约——防御性，正常装载路径不可达）
     *
     * 线程安全：并发只读安全。
     */
    [[nodiscard]] virtual std::optional<RevisionView> tryRevision(
        core::RevisionId id) const = 0;

    /**
     * @brief 读取修订视图；闭包外/不存在抛（§5.2 原文"不存在抛
     *        StoreError(store-corrupt)"——调用方已断言该修订存在，
     *        查不到即数据侧异常而非"没有"）。
     *
     * @param id [in] 修订身份（rev- 规范文本）
     * @return 修订视图值快照（同 tryRevision）
     *
     * @throws std::invalid_argument id 为全零保留值（调用方契约违约）
     * @throws StoreError ContextClosed／StoreCorrupt（闭包外或不存在——
     *         §5.2 错误表；含 tryRevision 的全部数据侧语义）
     *
     * 线程安全：并发只读安全。
     */
    [[nodiscard]] virtual RevisionView revision(core::RevisionId id) const = 0;

    /**
     * @brief 权威元数据视图（§5.2 原文"权威＝HEAD 引用版本，INV-M3"）。
     *
     * @return {权威引用键, 记录值拷贝}（分支 tip 查询的唯一合法数据源
     *         ——branchTips 即由它投影）
     *
     * @throws StoreError ContextClosed
     *
     * 线程安全：并发只读安全。
     */
    [[nodiscard]] virtual ProjectMetadataView currentMetadata() const = 0;

    /**
     * @brief 历史浏览：指定修订提交时点的元数据快照（§5.2 注释"历史
     *        浏览（R2）"——PM-12-S1 展示"当时分支表长什么样"）。
     *
     * 注意：返回的是**历史快照**，不是权威（其分支表不得当当前权威
     * 使用——ProjectMetadataView 注释的契约锚点）。
     *
     * @param id [in] 修订身份（全零＝未设置 → nullopt）
     * @return 该修订清单 metadataRef 指向的元数据版本；修订闭包外/
     *         不存在＝nullopt（可见性纪律同 tryRevision）
     *
     * @throws StoreError ContextClosed；StoreCorrupt（清单 metadataRef
     *         指向未注册元数据——悬挂引用，数据侧）
     *
     * 线程安全：并发只读安全。
     */
    [[nodiscard]] virtual std::optional<ProjectMetadataView> metadataAt(
        core::RevisionId id) const = 0;

    /**
     * @brief 分支表视图（§5.2 原文"自权威元数据"——HEAD 引用版本的
     *        branches 投影，INV-M3 纪律）。
     *
     * @return 分支 tip 列表（保持权威元数据的分支表磁盘序——该序确定
     *         且随提交稳定，满足 NFR-COR-02 同状态同输出）
     *
     * @throws StoreError ContextClosed
     *
     * 线程安全：并发只读安全。
     */
    [[nodiscard]] virtual std::vector<BranchTip> branchTips() const = 0;

    /**
     * @brief 分支历史（§5.2 原文"沿 parent 链"；PM-12-S1 历史浏览）。
     *
     * 行走语义：自**权威元数据**中该分支的 tip 起（INV-M3——不取自历史
     * 元数据），沿分支 tip 维度的 parentRevisionId 逐级回溯（§4.5——
     * 分支 A 的历史只含 A 上的提交，不含其他分支），至首修订（无父）或
     * 条数达 maxCount 止。返回顺序＝新→旧（tip 在前）。
     *
     * @param branch   [in] 分支身份（必须存在于权威元数据分支表）
     * @param maxCount [in] 最多返回条数（无单位——纯计数；0＝返回空
     *                 列表的平凡请求，字面语义）
     * @return 修订视图列表（新→旧；≤maxCount 条）
     *
     * @throws std::invalid_argument 分支不存在于权威元数据（§5.2 表
     *         "未知分支抛"——调用方违约，fail-fast；StoreErrorCode 为
     *         封闭集不私扩稳定码，实现口径登记于 §12 v0.11）
     * @throws StoreError ContextClosed；StoreCorrupt（parent 链悬挂——
     *         数据侧）
     *
     * 线程安全：并发只读安全。
     */
    [[nodiscard]] virtual std::vector<RevisionView> branchHistory(
        core::BranchId branch, std::uint32_t maxCount) const = 0;

    /**
     * @brief try 轨读取对象负载（§5.2 原文签名 noexcept；§4.6"引用不
     *        存在于清单的请求返回空 optional（tryObject），不抛"）。
     *
     * noexcept 契约下的失败表达（实现口径，登记于 §12 v0.11）：
     *   - 对象不存在（含从未发布/清单无引用）→ nullopt（§4.6 存在性
     *     语义——try 轨对"没有"返回空）；
     *   - 上下文已关闭 → nullopt（拒绝态的 try 轨表达——noexcept 下
     *     无法抛 ContextClosed）；
     *   - 数据侧损坏（size/摘要校验失败——篡改/位翻转）→ nullopt＋
     *     开发诊断 reportDev（noexcept 下不可抛 StoreCorrupt，但损坏
     *     不静默——开发诊断通道保留可见性；需要强校验语义的调用方走
     *     object()，其保留"损坏＝抛"的二分）。
     *
     * 完整性：对象首读执行 size＋SHA-256 校验（core ContentDigester），
     * 摘要缓存后免复检（§4.6）；经 LRU ObjectCache（预算随打开请求
     * 注入——acceptance 3"ObjectCache 预算参数生效"）。
     *
     * @param oid [in] 对象身份（obj- 规范文本）
     * @param cv  [in] 内容版本（cv- 规范文本）
     * @return 负载字节深拷贝（值语义，PA-3）；不存在/关闭/损坏＝nullopt
     *
     * 线程安全：并发只读安全（缓存互斥内部实现——§4.6）。
     */
    [[nodiscard]] virtual std::optional<std::vector<std::uint8_t>> tryObject(
        core::ObjectId oid, core::ContentVersion cv) const noexcept = 0;

    /**
     * @brief 读取对象负载（强语义：引用缺失＝store-corrupt——§5.2
     *        错误表；调用方已断言引用存在）。
     *
     * @param oid [in] 对象身份（obj- 规范文本）
     * @param cv  [in] 内容版本（cv- 规范文本）
     * @return 负载字节深拷贝（与磁盘一致——首读摘要校验，§4.6）
     *
     * @throws std::invalid_argument oid/cv 为全零保留值（调用方契约违约）
     * @throws StoreError ContextClosed；StoreCorrupt（引用缺失或摘要
     *         校验失败——篡改与缺失都按数据侧拒绝）；AccessDenied 等
     *         环境残余码（存在却不可读）
     *
     * 线程安全：并发只读安全。
     */
    [[nodiscard]] virtual std::vector<std::uint8_t> object(
        core::ObjectId oid, core::ContentVersion cv) const = 0;

    /**
     * @brief 草稿清单（§5.2 原文"含 baseRevision 与过期标记"）。
     *
     * 扫描语义：drafts/<branch-id>/ 下以 ".draft.json" 结尾的当前有效
     * 草稿（.new/.bak 崩溃残留不列入——§8.4）；stale 按权威元数据中该
     * 分支 tip 判定（INV-M3 数据源）；目录或分支不存在＝空列表（无草稿
     * 是常态不是错误——§5.2 表本行无前置）；个别草稿解析失败→跳过＋
     * 开发诊断（清单面保守不虚报可用草稿——实现口径登记于 §12 v0.11；
     * 损坏草稿的恢复处置权在草稿服务/DraftService，PM-15 横幅）。
     *
     * @param branch [in] 分支身份（brn- 规范文本）
     * @return 草稿条目列表（按 moduleId 字典序——NFR-COR-02 确定性，
     *         目录枚举序不保证）
     *
     * @throws StoreError ContextClosed
     *
     * 线程安全：并发只读安全（与草稿保存的并发：草稿文件原子替换——
     * 读到的必是某次完整落盘版本，§5.4 原语保证）。
     */
    [[nodiscard]] virtual std::vector<DraftInfo> listDrafts(
        core::BranchId branch) const = 0;

    /**
     * @brief 运行清单（§5.2 原文"仅 finalize（有 manifest）的运行"；
     *        D-13 只认完整运行）。
     *
     * 扫描语义：results/ 下含 manifest.json 的运行目录，解析 manifest
     * 后按 taskIdentity.revision == 参数 过滤（绑定原修订——§10.1 归档
     * 绑定语义的反向查询）；manifest 缺失（未完成归档）或解析失败
     * （损坏）→不列入（§4.1 results 行"查询不列为完整运行"），损坏另
     * 经开发诊断上报（实现口径登记于 §12 v0.11）。
     *
     * @param revision [in] 被评估修订身份（rev- 规范文本；全零＝未设置
     *                 → 空列表——无运行绑定于"未设置"修订）
     * @return 运行条目列表（按 runId 规范文本字典序——NFR-COR-02）
     *
     * @throws StoreError ContextClosed
     *
     * 线程安全：并发只读安全（与归档写入的并发：manifest 原子发布——
     * 读到的必是完整或不存在的 manifest，§10.1）。
     */
    [[nodiscard]] virtual std::vector<RunInfo> listRuns(
        core::RevisionId revision) const = 0;

    /**
     * @brief 运行工件目录路径（§5.2 原文"供 reporting/evidence 读工件"
     *        ——结果批次文件的读取锚点；本端口不代读工件内容，N-9：
     *        project 只存取不判断证据业务）。
     *
     * 路径规则：results/<run-id 规范文本>/（§4.1 results 行命名规则的
     * 唯一确定性推导；与归档端口"归档位置不重新推导"（A8）不冲突——
     * 那约束的是写入侧沿用登记记录，本方法是读取侧的规范编址）。
     *
     * @param run [in] 运行身份（run- 规范文本；全零保留值＝调用方契约
     *            违约 → fail-fast）
     * @return 运行目录绝对路径（目录可能尚不存在——未完成归档；存在性
     *         由调用方按 listRuns/D-13 判定后消费）
     *
     * @throws std::invalid_argument run 为全零保留值
     * @throws StoreError ContextClosed
     *
     * 线程安全：并发只读安全。
     */
    [[nodiscard]] virtual std::filesystem::path runDir(core::RunId run) const = 0;
};

}  // namespace sdurws::ird::project

#endif  // SDURWS_IRD_PROJECT_QUERYPORT_HPP
