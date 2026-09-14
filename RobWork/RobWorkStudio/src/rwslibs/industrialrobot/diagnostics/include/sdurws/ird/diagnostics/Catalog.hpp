/**
 * @file   Catalog.hpp
 * @brief  诊断目录与信封——DiagEntryId/DedupKey/DiagContext/DiagnosticEntry、
 *         DiagCatalog（会话态目录＋只读投影）与 IDiagnosticSink（统一 sink
 *         标准接口）＋DiagnosticsSinkImpl（report/reportDev 形态兼容）。
 *
 * 设计依据：
 *   - units/diagnostics.md §4.1（双层诊断模型：core record 内嵌＋本单元信封）、
 *     §4.2（DiagnosticEntry/DiagContext 字段表——全部字段构造后不可变）、
 *     §6.4（去重键 DedupKey 与"不同作用对象绝不合并"、稳定排序 orderKey）、
 *     §6.2（目录生命周期——容量清理随 DIAG-T09）、§9.7（IDiagnosticSink/
 *     DiagProjectionItem/DiagnosticsSinkImpl 契约）、§4.4（actionKind 动作族）
 *   - 需求 ERR-01（诊断字段/绑定对象——稳定诊断项以 subjectObjectId 绑定
 *     对象）、UX-03（三要素；正常取消非错误）、PM-15（统一诊断目录）、
 *     PM-08（恢复诊断数据源）、TASK-02（取消/失败/中断分类区分）、TASK-03
 *     （五元组关联——DiagContext.task）、NFR-COR-02（orderKey 稳定排序）
 *   - 任务契约 tasks/foundation/DIAG-T04.json（≙WP-09-T04 部分）：`Catalog.*`
 *     （DiagContext/Entry/DedupKey/DiagCatalog——§11 DIAG-T04 行产物）
 *
 * 背景说明（为什么目录条目是"信封"而不是继承 core——SA-12/P-DIAG-1）：
 *   ARCH §7.8 冻结诊断数据契约（DiagnosticRecord）归 core，注册表与目录设施
 *   归 diagnostics。ERR-01 的语义字段随宿主对象（envelope/CommandRecord/
 *   RecoveryReport）持久化；而会话运行还需要分类/严重、关联身份（运行/修订/
 *   命令）、时间/线程、原因链、去重键、稳定排序等**目录面**信息——这些由本
 *   信封承载（组合内嵌 core::DiagnosticRecord，不继承、不扩充 core 契约）。
 *   信封本体不序列化（会话态）；其 record 部分随宿主持久化。
 *
 * 陷阱处置锚点（契约 knownPitfalls 逐项）：
 *   - P-DIAG-1：信封内嵌 core::DiagnosticRecord 等消费契约以 core.md v0.1
 *     （Draft 未冻结）为基线（编译期类型钉住见测试）；core 冻结 diff 后增量
 *     同步，不私改 core。
 *   - P-DIAG-5：IDiagnosticSink（§9.7）为统一 sink 标准接口；DiagnosticsSinkImpl
 *     以 project §5.0 IDiagnosticsSink / execution §3.3 IExecutionDiagnosticsSink
 *     的同形签名（report(record)／reportDev(channel,message)）实现三态语义
 *     兼容——对端接口的名称/归属统一待其详设修订（P-PR-6/P-EX-8），本实现
 *     不私改对端契约（对端头文件不在本单元依赖面内，R-1/R-2）。
 *
 * subject 边界口径（ERR-01/core §4.8/DT-DIAG-2——实现登记，见单元卡 v0.5）：
 *   用户级码（severity != Dev）的条目必须携带合法（非全零）subjectObjectId，
 *   Dev 码可空——core.md §4.8"稳定诊断项必须携带合法 subjectObjectId；瞬时
 *   开发诊断可空"的边界强制（完整性强制归 diagnostics 边界，core §10.3 交接）。
 *   本单元卡 §9.2 调用示例与 §8.2/§8.5 映射表注的 subject=∅ 记法与该权威链
 *   不符，在该等映射落地任务时按此口径修正（登记于单元卡变更记录）。
 *
 * 线程安全：DiagCatalog 的 append/snapshot/subscribe 并发安全（内部互斥；
 * §9.7 契约表）；订阅回调在调用方线程同步派发（阶段 A 口径——§9.8"目录通知
 * 线程复用日志线程或独立"，异步化随 DIAG-T07 日志线程落地）；其余类型为纯值。
 */

#ifndef SDURWS_IRD_DIAGNOSTICS_CATALOG_HPP
#define SDURWS_IRD_DIAGNOSTICS_CATALOG_HPP

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include <sdurws/ird/core/DiagData.hpp>      // core::DiagnosticRecord（P-DIAG-1 基线）
#include <sdurws/ird/core/Identity.hpp>      // core::ObjectId/ProjectId/BranchId/RevisionId/TaskIdentity
#include <sdurws/ird/core/Digest.hpp>        // core::ContentIdentity（snapshot/slice/policy/command 身份）
#include <sdurws/ird/diagnostics/DiagCodes.hpp>  // DiagnosticCategory/DiagnosticSeverity（分类/严重词表）

namespace sdurws::ird::diagnostics {

// IDiagnosticFactory 前置声明（DiagnosticsSinkImpl 构造形参仅用引用——定义
// 在 Factory.hpp，其 include 本头；前置声明切断循环包含）。
class IDiagnosticFactory;

// =====================================================================
// 基础类型（§4.2 字段表的承载原语）
// =====================================================================

/// 目录条目身份（§4.2：目录内单调 ≥1；0＝空。仅会话态排序与引用用，不持久
/// 化为跨会话身份——持久化诊断的身份＝宿主对象＋序内偏移）。由工厂分配
/// （§9.2 后置"entryId/dedupKey/orderKey 已赋"）。
using DiagEntryId = std::uint64_t;

/**
 * @brief 时钟抽象（§4.2：emittedAtUtc 时钟来源＝进程内注入的 IClock——测试
 *        可替换，testkit ManualClock 兼容形态）。
 *
 * 为什么注入：诊断产生时间进入 orderKey 稳定排序与目录呈现——测试需要确定性
 * 时间（NFR-COR-02 精神）；本单元零 testkit 依赖（T-1 红线），故自定义最小
 * 接口，形态与 testkit ManualClock 兼容（测试侧以固定时刻实现本接口）。
 * 线程安全：实现方自行保证 nowUtc 可并发调用（SystemClock 天然安全）。
 */
struct IClock {
    virtual ~IClock() = default;

    /**
     * @brief 当前 UTC 时刻。
     * @return system_clock 时间点（§4.2 emittedAtUtc 类型；仅用于会话态排序
     *         与呈现，不承诺跨平台同值）
     */
    virtual std::chrono::system_clock::time_point nowUtc() const = 0;
};

/// 进程真实时钟（L5 装配默认注入；nowUtc 直通 system_clock——无单调保证，
/// 序列稳定性由 orderKey 的 entryId 分量兜底，见 OrderKey 注释）。
class SystemClock final : public IClock {
public:
    std::chrono::system_clock::time_point nowUtc() const override
    {
        return std::chrono::system_clock::now();
    }
};

/**
 * @brief 去重作用域类别（§6.4：scopeKind∈{None, Task, Case, Object, Batch}）。
 *
 * 目录去重键的派生口径（实现登记，单元卡 v0.5）：条目创建时由工厂从
 * DiagContext 推导——task 存在→Task（scopeId＝五元组规范串）；否则 subject
 * 存在→Object（scopeId＝subject 规范串）；二者皆无→None（用户级条目受
 * subject 边界强制实际不可达，Dev 码不入目录）。Case/Batch 为聚合/分批场景
 * 保留值：聚合归 DIAG-T06（§6.4 聚合键），分批回传标注随 DIAG-T07（§7.5）
 * ——阶段 A 不产生，枚举值保留不预建行为（NFR-MNT-04）。
 */
enum class ScopeKind : std::uint8_t {
    None,    ///< none——无作用域（Dev 级/无锚定条目；不入用户目录）
    Task,    ///< task——任务运行作用域（DiagContext.task 五元组）
    Case,    ///< case——工况作用域（聚合视图派生，DIAG-T06 消费）
    Object,  ///< object——对象作用域（条目 subject）
    Batch,   ///< batch——批次作用域（worker 分批回传，DIAG-T07 消费）
};

/// 取作用域 token（§6.4 词表小写；目录导出/测试观测用；全枚举 switch）。
std::string_view scopeKindToken(ScopeKind kind) noexcept;

/**
 * @brief 去重键（§6.4：DedupKey = {code, subject, scopeKind, scopeId}）。
 *
 * 去重纪律（§6.4 冻结，DT-DUP-1/2 钉住）：
 *   - 同一错误重复出现（同 code＋同 subject＋同 scope）→ 目录保留首条，后续
 *     命中计数（occurrences），不重复占位；
 *   - **不同作用对象绝不合并**（code 相同、subject 不同 → 不同 DedupKey，
 *     各自成条——DT-DUP-2 反例钉住，"不同作用对象不能错误去重"）；
 *   - 去重只作用于目录呈现，绝不改写发送给 envelope/命令结果的诊断集合。
 *
 * 值语义；operator==/operator< 供目录内 std::map 键使用（字段序＝比较序：
 * code→subject→scopeKind→scopeId——字典序确定性，NFR-COR-02）。
 */
struct DedupKey {
    std::string code;         ///< 稳定码（已注册——条目经工厂产出）
    core::ObjectId subject;   ///< 作用对象（用户级条目必为合法值——subject 边界）
    ScopeKind scopeKind = ScopeKind::None;  ///< 作用域类别（派生口径见 ScopeKind 注释）
    std::string scopeId;      ///< 作用域规范串（Task＝五元组串/Object＝subject 串/None＝空）

    bool operator==(const DedupKey& o) const
    {
        return code == o.code && subject == o.subject && scopeKind == o.scopeKind
            && scopeId == o.scopeId;
    }
    bool operator!=(const DedupKey& o) const { return !(*this == o); }
    bool operator<(const DedupKey& o) const
    {
        if (code != o.code) { return code < o.code; }
        if (!(subject == o.subject)) { return subject < o.subject; }
        if (scopeKind != o.scopeKind) { return scopeKind < o.scopeKind; }
        return scopeId < o.scopeId;
    }
};

/**
 * @brief 稳定排序键（§4.2/§6.4：orderKey ＝ tuple<时间戳计数, entryId, code>）。
 *
 * 目录与聚合视图的输出序＝orderKey 升序；同输入同序（NFR-COR-02 精神，
 * DT-AGG-2 观测点）。分量语义：
 *   - 时间戳计数：emittedAtUtc 的时钟计数（system_clock duration count——
 *     进程内可比；系统时钟回拨不破坏稳定性，entryId 单调分量兜底）；
 *   - entryId：目录内单调身份（同刻条目按产生序）；
 *   - code：码字典序（终局确定性分量）。
 */
using OrderKey = std::tuple<std::uint64_t, DiagEntryId, std::string>;

// =====================================================================
// DiagContext（§4.2 关联身份块——全部为 core 值类型或受限 token）
// =====================================================================

/**
 * @brief 诊断关联身份块（§4.2 字段表＋§9.2 调用示例的 params 字段）。
 *
 * 上下文把一条诊断锚定到项目/分支/修订/任务运行/快照切片/策略内容/命令——
 * 各字段的"不可混淆边界"见成员注释（身份计算归各所有者单元，本块只承载
 * 已核验方传入的值）。可空但**正式诊断不得为空块**（§4.2 context 行：至少
 * 携带其产生路径要求的锚定身份）。
 *
 * 值语义；默认构造＝空块（仅供工厂内部组装/容器占位）。
 */
struct DiagContext {
    /// 项目身份（命令路径诊断必填；与 TaskIdentity.project 同源不二次推导）。
    std::optional<core::ProjectId> project;
    /// 方案分支身份（同上）。
    std::optional<core::BranchId> branch;
    /// 修订上下文（命令路径诊断必填——命令边界诊断可追溯到修订，DT-COLLAB-1）。
    std::optional<core::RevisionId> revision;
    /// 运行五元组（execution 路径必填——§8.10 工厂校验 execution 来源码的
    /// task 非空；TASK-03 五元组关联。核对归 execution，本块只承载已核验值）。
    std::optional<core::TaskIdentity> task;
    /// 快照上下文（评估路径诊断；身份计算归 evidence——本块不计算）。
    std::optional<core::ContentIdentity> snapshotId;
    /// 切片上下文（同上）。
    std::optional<core::ContentIdentity> sliceId;
    /// 已解析策略内容身份（CON-06；策略相关诊断与 FindingBinding 复用同一来源；
    /// 策略解析归 policy）。
    std::optional<core::ContentIdentity> policyContentId;
    /// 命令类型 token（命令路径诊断；命令服务归 project）。
    std::optional<std::string> commandType;
    /// 命令载荷摘要（SHA-256 over payloadCanonical 经 core ContentDigester——
    /// 计算归 project/工厂，本块承载身份；日志/目录不携带载荷原文）。
    std::optional<core::ContentIdentity> commandDigest;
    /// 来源单元 token（§4.2：必填 ≤32 字符——如 "project"/"runtime"/"policy"/
    /// "execution"/"io"/"evidence"/"ui"；词表开放、随码注册，须与码 ownerUnit
    /// 的所有权域一致的使用场景由调用方保证）。
    std::string sourceUnit;
    /// 来源接口/通道 token（§4.2：必填 ≤64 字符——如 "commands.submit"、
    /// "channel.error-report"）。
    std::string sourceInterface;
    /// 契约/算法版本标注（§4.2：版本权威归各所有者，此处仅标注；工厂按 §4.5
    /// 追加 {"diag-code", registryVersion} 标注码表版本——报告侧文案/语义演进
    /// 检测用）。
    std::vector<std::pair<std::string, std::uint32_t>> contractVersions;
    /// 参数占位（§9.2 调用示例明示字段：{"pid",…},{"host",…} 形态；键集必须
    /// 与码的 paramSchema 参数名完全一致——工厂校验占位一致性，ParamSchemaMismatch）。
    std::map<std::string, std::string> params;
};

// =====================================================================
// DiagnosticEntry（§4.2 目录信封——全部字段构造后不可变，无 setter）
// =====================================================================

/**
 * @brief 诊断目录条目（§4.2 字段表 14 字段——内嵌 core::DiagnosticRecord 的
 *        会话态信封）。
 *
 * 生命周期与可变性（§4.1 双层模型表）：目录内条目构造后不可修改（无 setter，
 * "类型层面杜绝"）；"修改"＝新条目＋supersedes 链接（§6.3——不改写旧条目，
 * PA-2 精神）；条目本体不持久化（会话态），其 record 部分随宿主对象持久化。
 *
 * 构造唯一入口＝IDiagnosticFactory::create（§9.2——分类/严重自码表解析，调用
 * 方不可覆盖，NFR-MNT-03 单一权威；category-mismatch 的阻断机制即"直接传入
 * 字段不存在于工厂签名"）。聚合聚合体为值语义；Dev 级条目只走日志不入目录
 * （§6.2——DiagCatalog::append 拒绝 Dev）。
 *
 * 线程安全：纯值（不可变共享安全）。
 */
struct DiagnosticEntry {
    /// 目录内唯一身份（≥1；0＝空——工厂分配，DiagCatalog 拒绝空 id 条目）。
    DiagEntryId entryId = 0;
    /// ERR-01 语义字段（core 契约，工厂已校验码已注册/subject 边界/三要素/
    /// 占位一致——P-DIAG-1：core.md v0.1 基线）。
    core::DiagnosticRecord record;
    /// 分类（§4.3 词表；创建时从码表元数据解析，不允许调用方覆盖）。
    DiagnosticCategory category = DiagnosticCategory::InputInvalid;
    /// 严重级别（同上；归属码表不随实例变化——§4.3 传播规则）。
    DiagnosticSeverity severity = DiagnosticSeverity::Error;
    /// 关联身份块（可空但正式诊断不得为空——§4.2 边界）。
    DiagContext context;
    /// 产生时间（UTC；时钟来源＝注入 IClock——测试可替换）。
    std::chrono::system_clock::time_point emittedAtUtc{};
    /// 产生线程标记（§4.2：≤32 字符 ASCII，词表开放——"main"/"cmd"/"sched"/
    /// "wk-<n>"；阶段 A create 路径不采集，留空——采集点随各单元产码路径登记）。
    std::string threadTag;
    /// execution WorkerId（§4.2：worker 侧产生时必填、主进程产生时空——标注
    /// 归 worker 日志回传路径 §7.5，随 DIAG-T07 消费；create 路径恒空）。
    std::optional<std::uint64_t> workerId;
    /// 根因链（§6.3：指向目录内另一条目——单父；DiagCatalog::append 校验
    /// 指向已存在条目。单调 entryId 下"只指向更早条目"⇒ 无环，DAG 由构造保证）。
    std::optional<DiagEntryId> causedBy;
    /// 关联诊断（§6.3：无方向语义仅导航；多值；同样校验指向已存在条目）。
    std::vector<DiagEntryId> relatedTo;
    /// 取代关系（§6.3：重复发现的更新表达——新条目 supersedes 旧条目，旧条目
    /// 保留不删；校验指向已存在条目）。
    std::optional<DiagEntryId> supersedes;
    /// 去重键（§6.4；构造时由工厂计算——不同 subject 绝不同键，DT-DUP-2）。
    DedupKey dedupKey;
    /// 稳定排序键（§6.4：{时间戳计数, entryId, code 字典序}——同输入同序）。
    OrderKey orderKey{};
    /// 脱敏后的开发级上下文快览（§4.2：构造时经 IRedactionService 产出，原文
    /// 不保留在目录。阶段 A 置空——IRedactionService 为 §11 DIAG-T08 行产物，
    /// 接线随该任务落地并升单元卡登记；原文本就不入目录条目）。
    std::optional<std::string> redactedContextSnapshot;

    bool operator==(const DiagnosticEntry& o) const;
    bool operator!=(const DiagnosticEntry& o) const { return !(*this == o); }
};

/**
 * @brief 分类→处理动作族 token（§4.4 矩阵"处理动作族（actionKind）"列——
 *        机器锚点，供 ui 呈现与投影消费）。
 *
 * 每分类取该行动作族的**首动作**为机器锚点（多动作行如 "retry-task /
 * inspect-log" 的第二动作为呈现层可选细分，不进机器字段）；映射与
 * DiagCodes.cpp 的 categoryDefaultRetry（§4.4 动作族→可重试性）同源同表。
 * token 与 §4.4 反引号原文逐字一致（词表冻结——只允许随 §4.4 修订演进）。
 *
 * @param category [in] 诊断分类（§4.3 全 15 值——switch 全枚举）
 * @return 稳定 token（静态存储期；确定性——同分类同串，NFR-COR-02）
 */
std::string_view actionKindToken(DiagnosticCategory category) noexcept;

// =====================================================================
// 只读投影与 sink 接口（§9.7——ui/reporting 消费面）
// =====================================================================

/**
 * @brief 目录查询过滤器（§9.7 snapshot 的 DiagQuery——"过滤：任务/修订/
 *        类别/严重"）。
 *
 * 语义：各字段为可选等值过滤，全部命中方纳入结果；全空＝不过滤（全量）。
 * 任务过滤按 DiagContext.task 五元组精确等值（TASK-03——可追溯到运行/尝试）；
 * 修订过滤按 context.revision。值语义。
 */
struct DiagQuery {
    /// 按运行五元组过滤（nullopt＝不过滤；五元组核对归 execution——此处为
    /// 已核验值的等值匹配）。
    std::optional<core::TaskIdentity> task;
    /// 按修订过滤（nullopt＝不过滤）。
    std::optional<core::RevisionId> revision;
    /// 按分类过滤（nullopt＝不过滤）。
    std::optional<DiagnosticCategory> category;
    /// 按严重级别过滤（nullopt＝不过滤）。
    std::optional<DiagnosticSeverity> severity;
};

/**
 * @brief 诊断只读投影项（§9.7——ui/reporting 消费的值拷贝）。
 *
 * 投影边界（§9.7 结构体注释原文）：**不含** redactedContextSnapshot（开发级
 * 专属）、**不含**原始 context 文本字段（record.context/cause/recommendedAction
 * 的呈现文本由 ui/文案资源按键与码表渲染——UX-02 不显示内部细节；投影只带
 * 文案键与结构化数据）。ui 不得反写目录（无写路径接口）。
 *
 * 字段集为 ui/reporting 契约（§9.7 契约表"稳定性"行——新增字段＝次版本）。
 */
struct DiagProjectionItem {
    DiagEntryId entryId = 0;                    ///< 目录条目身份（会话态）
    std::string code;                           ///< 稳定码（登记表权威）
    std::string titleKey;                       ///< 用户文案键（P-DIAG-9 键/值分离）
    std::string detailKey;                      ///< 用户文案键（同上）
    DiagnosticCategory category = DiagnosticCategory::InputInvalid;  ///< 分类（码表来源）
    DiagnosticSeverity severity = DiagnosticSeverity::Error;         ///< 严重（码表来源）
    std::optional<core::ObjectId> subject;      ///< 作用对象（用户级条目必有）
    std::optional<std::string> localName;       ///< 局部名（UX-10 对象定位）
    std::optional<std::string> runtimeName;     ///< 运行时名（⑥名称端口取得）
    std::string actionKind;                     ///< 处理动作族（§4.4 机器锚点）
    std::optional<core::ComparativeFields> comparison;  ///< 比较型三要素（UX-03）
    DiagContext context;                        ///< 关联身份块（只读值拷贝）
    std::size_t occurrences = 1;                ///< 去重计数（§6.4：同键重复命中数）
    std::optional<DiagEntryId> aggregatedUnder; ///< 聚合导航（§6.4——聚合视图随 DIAG-T06，阶段 A 恒空）
};

/**
 * @brief 目录变更观察者（§9.7 subscribe 的回调面）。
 *
 * 通知语义：目录发生追加/去重计数变化后回调一次；**无载荷**——订阅方经
 * snapshot(DiagQuery) 拉取（投影为值拷贝，推送明细会放大拷贝面）。回调在
 * 目录通知线程执行（§9.7：订阅方自行 Marshal——阶段 A＝触发 append 的调用
 * 方线程同步派发，见类注释）；回调内不得调用本目录的写路径（append——
 * 递归加锁死锁面；读路径 snapshot 允许）。
 */
struct IDiagObserver {
    virtual ~IDiagObserver() = default;

    /// 目录变更通知（无载荷——订阅方自行拉取投影）。
    virtual void onCatalogChanged() = 0;
};

/// 订阅句柄（§9.7 subscribe 返回——RAII：析构即退订；不可拷贝，可持
/// unique_ptr）。目录被销毁后持有句柄无意义（生命周期短于目录）。
struct ISubscription {
    virtual ~ISubscription() = default;
};

/**
 * @brief 统一诊断 sink 标准接口（§9.7 原文四方法——P-DIAG-5 消费面）。
 *
 * 与既有 sink 形态的统一（§9.7 尾注原文）：project §5.0
 * IDiagnosticsSink{report(record), reportDev(channel,message)} 与 execution
 * §3.3 IExecutionDiagnosticsSink 同形——本单元提供 DiagnosticsSinkImpl 同时
 * 实现三者语义；接口名/归属的最终统一待两消费方详设修订（P-PR-6/P-EX-8），
 * 本接口不私改对端契约。
 *
 * 行为契约（§9.7 签名注释逐条）：
 *   - append：记录入目录（工厂产出后的显式记录动作——创建与记录分离，D-18）。
 *     前置：entry.severity != Dev（Dev 码误入→Usage——Dev 走日志，§6.2）。
 *     后置：目录追加＋去重计数＋变更通知；容量策略执行（§6.2——随 DIAG-T09）。
 *   - snapshot：只读投影（ui 拉取；值拷贝——ui 不持目录引用）。
 *   - subscribe：订阅变更（回调在目录通知线程——订阅方自行 Marshal）。
 *   - exportSafeSummary：安全摘要导出（reporting：稳定码＋安全参数；输出再
 *     过脱敏——双保险，脱敏接线随 DIAG-T08）。
 */
class IDiagnosticSink {
public:
    virtual ~IDiagnosticSink() = default;

    /// @brief 记录入目录（§9.7 原文签名；Dev 条目拒绝——Usage）。
    virtual void append(DiagnosticEntry entry) = 0;

    /// @brief 只读投影（§9.7 原文签名；按 DiagQuery 过滤，orderKey 稳定序）。
    virtual std::vector<DiagProjectionItem> snapshot(DiagQuery query) const = 0;

    /// @brief 订阅变更（§9.7 原文签名；返回 RAII 句柄，析构即退订）。
    virtual std::unique_ptr<ISubscription> subscribe(IDiagObserver& observer) = 0;

    /// @brief 安全摘要导出（§9.7 原文签名；键值文本格式——§1.4"自有文本/
    ///        键值格式"；最多 maxEntries 条，0＝不限）。
    virtual std::string exportSafeSummary(DiagQuery query, std::size_t maxEntries) const = 0;
};

// =====================================================================
// DiagCatalog（§9.7 实现——会话级目录）
// =====================================================================

/**
 * @brief 诊断目录实现（§9.7/§6.1/§6.4——会话态诊断集合、只读投影、去重
 *        计数、稳定排序、变更通知）。
 *
 * 行为要点（设计依据逐条）：
 *   - 追加即去重（§6.4/DT-DUP-1）：同 DedupKey 条目不再占位，首条目的
 *     occurrences 递增并通知观察者；不同 subject 绝不合并（DT-DUP-2）。
 *   - 稳定序（§6.4）：snapshot 按条目入目录序（＝orderKey 升序——entryId
 *     单调保证与 orderKey 一致）输出，同输入同序。
 *   - 入口校验（§9.7 前置＋§6.3 链完整性）：Dev 条目拒绝（Usage）；空/重复
 *     entryId 拒绝（Usage——工厂分配唯一身份）；causedBy/relatedTo/supersedes
 *     指向不存在条目拒绝（Usage——§6.3"工厂校验指向已存在条目"；指向性校验
 *     在目录侧落地的理由＝目录持有条目集合，工厂无此知识）。
 *   - 容量与清理（§6.2"诊断清理策略"行）：**不在本任务**——§11 DIAG-T09 行
 *     产物（容量护栏＋分级淘汰＋DIAG-CATALOG-OVERFLOW）；本类结构已为容量
 *     策略预留（条目集合与去重计数分离存储）。
 *
 * 线程安全（§9.7 契约表）：append 并发安全（内部互斥）；snapshot 并发只读；
 * 订阅回调在触发 append 的调用方线程同步派发（阶段 A 口径——§9.8 目录通知
 * 线程"复用日志线程或独立"的独立面随 DIAG-T07 异步化；订阅方负责 Marshal）。
 * 回调在锁外派发（先改状态后通知——观察者可安全调用 snapshot）。
 *
 * 生命周期（§9.7 契约表）：会话级目录（项目打开→关闭回收）；进程内服务壳
 * DiagnosticsSinkImpl 持引用注入。无文件 I/O（日志另走 logger——§9.7 副作用行）。
 */
class DiagCatalog final : public IDiagnosticSink {
public:
    /// @brief 构造（分配实现体；条目集合/去重索引/订阅表初始为空）。
    DiagCatalog();

    /// @brief 析构（pimpl——须在实现体完整的翻译单元内定义；契约：订阅句柄
    ///        必须先于目录析构，见 ISubscription 注释）。
    ~DiagCatalog() override;

    // 禁拷贝/禁移动：订阅句柄以原始引用回指目录（§9.7——回调派发需要目录
    // 地址稳定；会话级单例语义同 StableCodeRegistry）。
    DiagCatalog(const DiagCatalog&) = delete;
    DiagCatalog& operator=(const DiagCatalog&) = delete;

    // ---- IDiagnosticSink（§9.7 四方法——行为契约见接口注释）----
    void append(DiagnosticEntry entry) override;
    std::vector<DiagProjectionItem> snapshot(DiagQuery query) const override;
    std::unique_ptr<ISubscription> subscribe(IDiagObserver& observer) override;
    std::string exportSafeSummary(DiagQuery query, std::size_t maxEntries) const override;

    /// @brief 目录内条目数（含去重折叠前的首条目；去重命中不产生新条目）。
    std::size_t size() const;

private:
    struct Impl;
    /// pimpl：隔离 std::mutex/容器细节于头文件之外（公共头最小依赖面——
    /// R-2 纪律；实现见 src/Catalog.cpp）。
    std::unique_ptr<Impl> m_impl;
};

// =====================================================================
// report/reportDev 形态兼容（P-DIAG-5——§9.7 尾注 DiagnosticsSinkImpl）
// =====================================================================

/**
 * @brief 开发日志路由窄接口（§9.7 尾注"reportDev＝logger.logDev"的接缝）。
 *
 * ILogger（§9.6 两级日志）为 §11 DIAG-T07 行产物；本接口只暴露 reportDev
 * 需要的最窄面（channel＋message），DIAG-T07 的 logger 实现类将实现本接口
 * （或经单行适配器桥接）——避免 DIAG-T04 为一个尚未存在的完整日志器预建
 * 依赖（NFR-MNT-04：无消费者的能力不预建行为）。脱敏在日志管线内强制执行
 * （§7.3——调用方无需也无法预脱敏），本接口不重复承担。
 */
struct IDevLogSink {
    virtual ~IDevLogSink() = default;

    /**
     * @brief 开发级日志快捷入口（对齐 project §5.0 reportDev 语义——§9.6）。
     * @param channel [in] 通道 token（来源子系统，≤48 字符——§7.2 LogChannel）
     * @param message [in] 消息原文（脱敏由日志管线负责——DIAG-T07）
     */
    virtual void logDev(std::string_view channel, std::string message) = 0;
};

/**
 * @brief 统一 sink 实现（§9.7 尾注 DiagnosticsSinkImpl——同时实现三方语义）。
 *
 * 三方语义的落点（§9.7 尾注原文逐条）：
 *   - IDiagnosticSink（本单元标准接口）：四方法直通注入的 DiagCatalog；
 *   - project §5.0 IDiagnosticsSink 语义：report(record)＝factory.create＋
 *     catalog.append（无上下文时 sourceUnit 取注入的宿主标识）；
 *   - execution §3.3 IExecutionDiagnosticsSink 语义：与 project 同形签名
 *     （两对端卡逐字同形——report/reportDev）。
 *
 * P-DIAG-5 处置（契约 acceptance 3）：对端接口的名称/归属统一待 project/
 * execution 详设修订（P-PR-6/P-EX-8），本类**不私改对端契约**——以同形签名
 * 提供 report/reportDev，L5 装配层以单行适配器（或对端详设修订后的直接继承）
 * 绑定各消费方 sink 指针到本实现；本单元不 include 对端头（R-1/R-2——
 * project/execution→diagnostics 才是登记边）。
 *
 * report 的校验语义：record 经工厂完整校验链（码已注册/subject 边界/三要素/
 * 占位一致）——违约即抛 DiagnosticsError（调用方错误 fail-fast，AGENTS.md
 * 错误语义）。需要丰富上下文（任务五元组/命令/修订等）的产码路径应直接使用
 * factory.create＋catalog.append（§9.2 create 全参形态）；本 report 快捷形态
 * 面向"码＋宿主身份即可锚定"的简单路径。
 */
class DiagnosticsSinkImpl final : public IDiagnosticSink {
public:
    /**
     * @brief 构造（L5 装配期注入）。
     *
     * @param factory       [in] 诊断工厂（report 路径的创建设施；调用方持有，
     *                      本类不接管所有权，引用须在生命周期内有效）
     * @param catalog       [in] 目标目录（append/snapshot/subscribe/export
     *                      的承载；同上）
     * @param devLog        [in] 开发日志路由（reportDev 的承载；同上）
     * @param hostUnit      [in] 宿主单元 token（report 无上下文时 sourceUnit
     *                      取此值——§9.7 尾注；如 "project"/"execution"）
     * @param hostInterface [in] 来源接口 token（默认 "sink.report"；调用方
     *                      可按其通道命名约定给值，≤64 字符）
     *
     * @throws DiagnosticsError Usage（hostUnit 为空或超 32 字符——§4.2 token
     *         边界；构造期 fail-fast，不带病装配）
     */
    DiagnosticsSinkImpl(IDiagnosticFactory& factory, DiagCatalog& catalog,
                        IDevLogSink& devLog, std::string hostUnit,
                        std::string hostInterface = "sink.report");

    // ---- project/execution 同形语义（P-DIAG-5——签名与对端卡逐字一致）----

    /**
     * @brief 用户诊断入口（project §5.0/execution §3.3 report 同形签名）。
     *
     * 后置：record 经 factory.create（上下文＝宿主标识）产出条目并 append 入
     * 目录（去重计数语义同 DiagCatalog）。异常语义＝create/append 的完整链。
     *
     * @param record [in] 用户诊断记录（码已注册、用户级须带合法 subject）
     */
    void report(const core::DiagnosticRecord& record);

    /**
     * @brief 开发诊断入口（project §5.0/execution §3.3 reportDev 同形签名）。
     *
     * 后置：转发注入的 IDevLogSink::logDev（§9.7 尾注"reportDev＝logger.
     * logDev"——两级日志管线随 DIAG-T07 落地；阶段 A 由测试替身/装配方实现
     * 本窄接口）。不产生目录条目（Dev 不入目录——§6.2）。
     *
     * @param channel [in] 通道 token（透传）
     * @param message [in] 消息原文（透传；脱敏归日志管线）
     */
    void reportDev(const std::string& channel, const std::string& message);

    // ---- IDiagnosticSink（直通注入目录——§9.7 四方法）----
    void append(DiagnosticEntry entry) override;
    std::vector<DiagProjectionItem> snapshot(DiagQuery query) const override;
    std::unique_ptr<ISubscription> subscribe(IDiagObserver& observer) override;
    std::string exportSafeSummary(DiagQuery query, std::size_t maxEntries) const override;

private:
    IDiagnosticFactory* m_factory;   ///< 工厂引用（非拥有——调用方持有，见构造注释）
    DiagCatalog* m_catalog;          ///< 目录引用（非拥有）
    IDevLogSink* m_devLog;           ///< 开发日志路由引用（非拥有）
    std::string m_hostUnit;          ///< 宿主单元 token（report 的 sourceUnit 来源）
    std::string m_hostInterface;     ///< 来源接口 token（report 的 sourceInterface）
};

}  // namespace sdurws::ird::diagnostics

#endif  // SDURWS_IRD_DIAGNOSTICS_CATALOG_HPP
