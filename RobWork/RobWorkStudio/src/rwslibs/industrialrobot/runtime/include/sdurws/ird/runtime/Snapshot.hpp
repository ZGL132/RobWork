/**
 * @file   Snapshot.hpp
 * @brief  RuntimeSnapshot——编译链唯一发布产物（§9.1 字段表）；并发只读
 *         共享约定（§9.2）；项目切换后的旧快照与迟到结果（§9.3）；
 *         IRuntimeModelView 统一只读入口（§8.3）；快照身份与工厂
 *         （IRuntimeSnapshotFactory）；worker 物化编码（§9.5）。
 *
 * 设计依据：
 *   - units/runtime.md §9.1（RuntimeSnapshot 字段表逐行——"入"身份列/
 *     合法与非法实例列）、§9.2（并发与共享规则——shared_ptr<const> 发布/
 *     WC 跨线程只读/State 每线程拷贝/worker 隔离物化）、§9.3（零外部依赖
 *     ——旧快照迟到反解用绑定映射；上下文释放后重解析请求 ContextReleased）、
 *     §9.5（MaterializedSnapshotCodec——magic IRDMAT1/内容面/worker 身份
 *     核对 D-13）、§8.3（IRuntimeModelView 原文接口）、§5.2 S10（唯一发布
 *     点——原子交接）、§9.6（发布门禁组合表——诊断无 error 级）
 *   - 需求 ARC-03（确定性编译产物）、ARC-04（名称经⑥端口）、CON-02（快照
 *     身份进结果绑定——历史证据可追溯）、CON-06（nameMapIdentity 随快照）、
 *     MDL-06（编译原子性）、NFR-COR-02（确定性——快照身份跨进程一致）、
 *     NFR-DEP-05（robworkBaselineVersion 基线不可比语义）、TASK-03（项目
 *     切换——AT-10 旧快照存活）
 *   - CR-05（跨单元红线）：本快照暴露 modelIdentity/nameMapIdentity/
 *     robworkBaselineVersion 三个**值供给**入口——组装方值传递录入 evidence
 *     切片 Environment 条目（runtime.model-identity / runtime.robwork-baseline
 *     ＋nameMapRef），evidence 侧禁止重编码 CanonicalModel（同一样例的联合
 *     断言口径见 test/SnapshotTest.cpp 与 units/evidence.md §4.2.1）
 *   - 任务契约 tasks/foundation/RT-T09.json（产物：RuntimeSnapshot/
 *     IRuntimeModelView 实现/工厂/materialize；acceptance：RT-SNAP-1～4、
 *     RT-CPX-4、CR-05、materialize §9.5 往返）
 *
 * 分阶段落位登记（RT-T09，§15.4 v0.10）：
 *   - §3.1 本头行实体全量落位（RuntimeSnapshot、SnapshotIdentity、
 *     IRuntimeSnapshotFactory、MaterializedSnapshotCodec）；
 *   - Compiler.hpp 的 §10.0 契约面（CompileRequest/CompileOutcome/
 *     ICanonicalModelCompiler）随本任务先行冻结（工厂签名依赖）——十段链
 *     产品实现仍归 RT-T11；
 *   - §8.4 RobWorkBaselineVersion（Adapter.hpp 原列）随本任务落位（基线
 *     版本记录随快照身份块——v0.8① 预留）；DeviceView/IRobWorkAdapterFactory
 *     仍随 RT-T11（编译器适配面，快照不消费）；
 *   - §9.5 内容面的"对象字节"承载形态＝CanonicalModel 的 IRDCANO 全字段
 *     编码（worker 无 reader——编译后物化而非字节重编译；D-01"可经
 *     RT-Codec 序列化供 worker 物化"）；"资源字节"随资源几何挂接任务
 *     （v0.8⑦ 分阶段）与 S4 复查（RT-T11）落位，本版载荷不含。
 *
 * 线程安全（§9.2 规则表逐行）：快照构造后只读（shared_ptr<const> 共享，
 * 无 setter）；同一切片的多入口共享同一实例；跨线程共享允许（不可变值＋
 * 只读视图）；WC/DWC 结构跨线程只读共享、State 每线程 makeState 拷贝；
 * RobWorkBaselineVersion/身份编码为纯函数。
 *
 * 两模式编译口径（§15.4 v0.10）：本头与实现文件 src/Snapshot.cpp 消费 S6/
 * S7 编译器（真实 rw/rwsim 非模板类），仅在集成模式编译（TARGET
 * sdurw_kinematics 条件增列）——冒烟模式不被任何 TU include（v0.8②/v0.9⑧
 * 同款分工，冒烟口径"目标注册＋include 路径"不受影响）。
 */

#ifndef SDURWS_IRD_RUNTIME_SNAPSHOT_HPP
#define SDURWS_IRD_RUNTIME_SNAPSHOT_HPP

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <rw/math/Transform3D.hpp>
#include <rw/math/Vector3D.hpp>

#include <sdurws/ird/core/DiagData.hpp>        // DiagnosticRecord（警告级诊断集）
#include <sdurws/ird/core/Digest.hpp>          // ContentIdentity（身份产物）
#include <sdurws/ird/core/Identity.hpp>        // ProjectId/BranchId/RevisionId/ObjectId
#include <sdurws/ird/runtime/Adapter.hpp>      // WorkCellConstView/DynamicWorkCellConstView
#include <sdurws/ird/runtime/CanonicalModel.hpp>  // CanonicalModel/RuntimeCapability
#include <sdurws/ird/runtime/Compiler.hpp>     // CompileRequest/CompileOutcome/
                                               // ICanonicalModelCompiler（§10.0 契约面）
#include <sdurws/ird/runtime/Errors.hpp>       // Expected/RuntimeError
#include <sdurws/ird/runtime/NameMap.hpp>      // RuntimeNameMap/IRuntimeNameResolver/
                                               // BoundRuntimeNameResolver
#include <sdurws/ird/runtime/Resource.hpp>     // ResourceRef（resourceManifest）

// rw/rwsim 基线类型在公共面只做前置声明（§8.2 只读包装纪律同 Adapter.hpp；
// 完整类型仅在实现文件与编译器私有头内可达）。★ 声明必须位于全局命名
// 空间——放进 sdurws::ird::runtime 内会声明出 runtime::rwsim 假名空间，
// 遮蔽基线的全局 rwsim（Adapter.hpp 同款位置纪律）。
namespace rwsim { namespace dynamics { class DynamicWorkCell; } }

// 单元内 S6/S7 编译器产物（src/WorkCellCompiler.hpp、src/DynamicWorkCellCompiler.hpp
// 定义——R-2 私有头不入公共 include；此处仅前置声明供工厂装配尾段的参数
// 透传，公共面不依赖其定义。与 Adapter.hpp 前置声明 rw 类型同风格）。
struct WorkCellCompileOutcome;
struct DynamicWorkCellCompileOutcome;

namespace sdurws::ird::runtime {

// =====================================================================
// 快照级值类型（§9.1 字段表的枚举/聚合承载——声明序即编码序）。
// =====================================================================

/**
 * @brief DWC 编译事实状态（§9.1 dynamicWorkCellState 行——enum{Compiled,
 *        SkippedNoPhysics}"Skipped 是能力事实，非失败"）。
 *
 * 与能力位的关系（§9.1"与 capability 一致（不一致构造拒绝）"的执行规则，
 * 工厂发布门禁实现）：
 *   - Compiled ⟹ capabilities().hasDynamicWorkCell 为 true（有 DWC 必因
 *     物性齐备——反向由 S7 门控保证）；
 *   - SkippedNoPhysics 的两个成因：被消费 Body 物性 NotProvided（此时
 *     skippedDynamicObjects 非空且能力位必 false——S7 门控）或本次编译未
 *     请求 DWC（CompileOptions.requestDynamicWorkCell=false，§9.4
 *     capabilityLevel——此时缺失清单为空，能力位与物性事实一致）。
 * 枚举序进入快照身份编码域，一经交付不得改动。
 */
enum class DwcSnapshotState {
    Compiled,        ///< DWC 已构造（快照持有 DynamicWorkCellConstView 可达）
    SkippedNoPhysics ///< 未构造 DWC（物性缺失跳过，或本次编译未请求——见类型注释）
};

/**
 * @brief 快照创建来源（§9.1 createdFrom 行——enum{Command, EvaluationPrepare,
 *        Worker}；观测性要素，**不入快照身份**）。
 */
enum class SnapshotOrigin {
    Command,            ///< 命令路径编译（ui/命令服务触发）
    EvaluationPrepare,  ///< 评估准备段编译（execution Preparing——ARCH §4.5）
    Worker,             ///< worker 物化重建（IRuntimeSnapshotFactory::materialize）
};

/**
 * @brief 编码器版本三元组（§9.1 codecVersions 行——{canonical-model,
 *        name-map, snapshot} 三元；"与编码头一致"）。
 *
 * 取值纪律：三元分别取自 rtcodec 编码头常量（kVersionMajor/Minor、
 * kNameMapVersionMajor/Minor）与 MaterializedSnapshotCodec 编码头常量——
 * 消费方不得另写字面量（编码升版＝全体身份变化，§4.5"身份域"行；单一
 * 权威＝各 Codec 头常量）。值语义纯结构；operator== 确定性逐字段比较
 * （身份/缓存键编码前提）。
 */
struct SnapshotCodecVersions {
    std::uint16_t canonicalModelMajor = 0; ///< IRDCANO 结构版本 major
    std::uint16_t canonicalModelMinor = 0; ///< IRDCANO 结构版本 minor
    std::uint16_t nameMapMajor = 0;        ///< IRDNAME 结构版本 major
    std::uint16_t nameMapMinor = 0;        ///< IRDNAME 结构版本 minor
    std::uint16_t snapshotMajor = 0;       ///< IRDMAT1 结构版本 major
    std::uint16_t snapshotMinor = 0;       ///< IRDMAT1 结构版本 minor

    /// 精确等值（逐字段；确定性纯函数、不抛）。
    bool operator==(const SnapshotCodecVersions& o) const noexcept
    {
        return canonicalModelMajor == o.canonicalModelMajor
            && canonicalModelMinor == o.canonicalModelMinor
            && nameMapMajor == o.nameMapMajor
            && nameMapMinor == o.nameMapMinor
            && snapshotMajor == o.snapshotMajor
            && snapshotMinor == o.snapshotMinor;
    }
    bool operator!=(const SnapshotCodecVersions& o) const noexcept { return !(*this == o); }
};

// =====================================================================
// IRuntimeModelView——统一只读入口（§8.3 原文契约；kinematics/trajectory/
// dynamics/policy 的共同消费点；由 RuntimeSnapshot 实现）。
// =====================================================================

/**
 * @brief 快照统一只读视图（§8.3 原文接口——全部 const、并发只读安全）。
 *
 * 接口属性（§8.3 属性表）：前置＝快照已发布；返回只读引用生命周期随快照
 * shared_ptr；tryDynamicWorkCell 无 DWC→Expected 错误（非崩溃）；全部查询
 * 为字段/结构直读（gravityBase 为纯矩阵乘——经 BaseWorldTransform 规则
 * 函数单点）；无写权限、无副作用。
 *
 * 非法调用（§8.3 属性表）：保存返回的 Frame 指针跨快照使用；对 view 的
 * const 结果 const_cast 修改（评审＋测试锁定——RT-SNAP 组只读纪律）。
 */
class IRuntimeModelView {
public:
    virtual ~IRuntimeModelView() = default;

    /// 规范模型（§9.1 私有 model_ 的只读暴露——快照身份的值源头）。
    virtual const CanonicalModel& model() const = 0;
    /// 名称映射（⑥端口真值——§7；旧快照迟到反解用绑定映射，§9.3）。
    virtual const RuntimeNameMap& nameMap() const = 0;
    /// 能力声明（§9.6——与内容绑定；下游按声明处置，缺失不升级为失败）。
    virtual const RuntimeCapability& capabilities() const = 0;
    /// WorkCell 只读视图（hasWorkCell 恒 true——否则无快照，§8.3 原文注释）。
    virtual const WorkCellConstView& workCell() const = 0;
    /// DWC 只读视图（无 DWC→错误＋定位能力项——§8.3 原文；非崩溃路径）。
    virtual Expected<DynamicWorkCellConstView, RuntimeError> tryDynamicWorkCell() const = 0;

    // —— 基座—世界唯一读取点（§6.4 消费契约；规则实现＝BaseWorldTransform
    //    纯函数单点，快照不另写第二套投影——§6.3）——

    /// T_world_base（世界系→基座系；§9.1 唯一存储在 CanonicalModel.world）。
    virtual rw::math::Transform3D<double> worldToBase() const = 0;
    /// T_base_world＝inverse(T_world_base)（§6.1 反解）。
    virtual rw::math::Transform3D<double> baseToWorld() const = 0;
    /// 世界系重力（单位 m/s²——恒定不随安装变化，MDL-22）。
    virtual rw::math::Vector3D<double> gravityWorld() const = 0;
    /// 基座系重力＝R_world_baseᵀ·g_world（DYN-01 投影公式单点）。
    virtual rw::math::Vector3D<double> gravityBase() const = 0;

    // —— 每线程 State 工厂（跨线程绝不共享 State——§8.7/§9.2）——

    /// WC 默认状态值拷贝（＝workCell().defaultState()——调用方线程私有）。
    virtual rw::kinematics::State makeState() const = 0;

    /// 名称端口（⑥——绑定本快照映射；迟到反解的权威路径，§9.3）。
    virtual const IRuntimeNameResolver& nameResolver() const = 0;
};

// =====================================================================
// SnapshotIdentity——快照身份计算（§9.1 snapshotIdentity 行＋§3.1 模块
// 清单实体：SHA-256 over 全部"入"字段编码；CR-02 摘要边界同 Codec）。
// =====================================================================

namespace snapshotidentity {

/**
 * @brief 快照身份"入"字段值包（§9.1 字段表身份列为"入"的全集——值面）。
 *
 * 排除面（§9.1"不入"列，编码域强制不含）：diagnostics（过程记录）、
 * createdAtUtc/createdFrom（观测性要素——与 evidence §4.1.1 同口径）、
 * snapshotIdentity 自身（身份不自我引用）。
 *
 * 注意 revisionSeq：快照身份四元组含 revisionSeq（§9.1 第一行"入"——
 * 来源定位）——与 CanonicalModel 的 modelIdentity 不同（模型层
 * revisionSeq 不入身份、§4.3.1；快照是"某修订的产物"，其定位含序号）。
 * 字段序即编码序（§9.1 表行序）；线程安全（纯值）。
 */
struct Fields {
    core::ProjectId project;                       ///< 项目身份（来源定位）
    core::BranchId branch;                         ///< 方案分支身份
    core::RevisionId revision;                     ///< 修订身份
    std::uint64_t revisionSeq = 0;                 ///< 修订序号（快照层入身份——见类型注释）
    core::ContentIdentity modelIdentity;           ///< ＝CanonicalModel.contentIdentity
    core::ContentIdentity nameMapIdentity;         ///< ＝RuntimeNameMap.contentIdentity
    std::uint32_t nameMapRuleVersion = 0;          ///< 生成规则版本（§7.1）
    core::ContentIdentity workCellCompileIdentity; ///< WC 层缓存键（§9.1 公式）
    DwcSnapshotState dynamicWorkCellState = DwcSnapshotState::SkippedNoPhysics; ///< DWC 事实
    std::vector<core::ObjectId> skippedDynamicObjects; ///< Skipped 缺失对象清单（链序）
    std::uint32_t compilerContractVersion = 0;     ///< 编译器契约版本
    std::string compilerVersion;                   ///< 编译器实现版本（非空）
    std::string robworkBaselineVersion;            ///< RobWork 基线版本（非空）
    SnapshotCodecVersions codecVersions;           ///< 编码器版本三元组
    CompileOptions compileOptions;                 ///< 编译选项（全集）
    std::vector<ResourceRef> resourceManifest;     ///< 资源清单（与 model 一致）
    RuntimeCapability capabilities;                ///< 能力声明（与内容一致）
};

/**
 * @brief 身份域确定性编码（§9.1 snapshotIdentity＝SHA-256 over 本编码）。
 *
 * 布局（RT-Codec 家族同规则：大端/长度前缀/无填充/声明序；字段序＝§9.1
 * 表行序；ContentIdentity 为 32 字节裸摘要；枚举 1 字节；bool 1 字节；
 * 字符串 len(8)+UTF-8 字节；计数 len(4)；ResourceRef 逐条
 * resourceId(16)+digest(32)+pathHint{presence(1)[+len+s]}+state(1)+
 * accessVersion(4)；capabilities 块与 rtcodec 能力块同构）。
 *
 * @param fields [in] 身份"入"字段包（只读）
 * @return 确定性编码字节（同字段同字节——NFR-COR-02；调用方持有）
 *
 * 线程/确定性：纯函数、可重入、无 I/O、无隐藏状态。
 */
std::vector<std::uint8_t> encodeIdentityDomain(const Fields& fields);

/**
 * @brief 计算快照身份（§9.1 snapshotIdentity 派生行——SHA-256 over 编码域）。
 *
 * CR-02 摘要边界：只调用 core::ContentDigester（runtime 不实现第二套
 * SHA-256——与 Codec/NameMap 同纪律）；"对什么字节做摘要"由
 * encodeIdentityDomain 声明。
 *
 * @param fields [in] 身份"入"字段包（只读）
 * @return 快照身份（非零——合法字段集编码非空）
 *
 * 确定性：同字段→同身份（跨进程一致——RT-ID-1 快照层）；无环境/时钟依赖
 * （观测性字段不在 Fields 中，结构层排除）。
 */
core::ContentIdentity compute(const Fields& fields);

/**
 * @brief 计算 WC 层编译缓存键（§9.1 workCellCompileIdentity 行的原文公式：
 *        SHA-256 over (modelIdentity, compilerContractVersion,
 *        compilerVersion, robworkBaselineVersion, nameMapRuleVersion,
 *        baseWorldRuleVersion, codecVersions, compileOptions〔DWC 无关
 *        子集〕)）。
 *
 * 实现口径（§15.4 v0.10 登记）：
 *   - baseWorldRuleVersion 不作参数——单一权威取 kBaseWorldRuleVersion
 *     （§6 规则版本常量单点，§9.4"随规则修改递增"）；
 *   - compileOptions 取 DWC 无关子集（includeCollisionGeometry＋
 *     geometryDetail）——requestDynamicWorkCell 只入 DWC 层键（D-04 分层）；
 *   - 编码＝各分量按上述公式的声明序拼接（大端/长度前缀，与
 *     encodeIdentityDomain 同规则）；RT-T10（CacheKey.hpp）的 workCellKey
 *     复用本函数（同一公式单点实现，避免第二套键编码）。
 *
 * @param modelIdentity           [in] 模型内容身份（非零）
 * @param compilerContractVersion [in] 编译器契约版本（≥1）
 * @param compilerVersion         [in] 编译器实现版本（非空）
 * @param robworkBaselineVersion  [in] RobWork 基线版本（非空）
 * @param nameMapRuleVersion      [in] 名称规则版本
 * @param codecVersions           [in] 编码器版本三元组
 * @param options                 [in] 编译选项（本函数只取 WC 无关子集）
 * @return WC 层缓存键（非零）
 *
 * @throws RuntimeError 码＝InputInvalid：modelIdentity 为全零保留值或版本
 *         串为空（键的合法域——非法键无缓存语义；fail-fast 不产出占位键，
 *         NFR-COR-03）
 *
 * 确定性：同分量→同键（纯函数；NFR-COR-02）。
 */
core::ContentIdentity computeWorkCellCompileIdentity(
    const core::ContentIdentity& modelIdentity,
    std::uint32_t compilerContractVersion,
    const std::string& compilerVersion,
    const std::string& robworkBaselineVersion,
    std::uint32_t nameMapRuleVersion,
    const SnapshotCodecVersions& codecVersions,
    const CompileOptions& options);

}  // namespace snapshotidentity

// =====================================================================
// RuntimeSnapshot——快照本体（§9.1 字段表；§3.1 模块清单实体）。
// =====================================================================

/**
 * @brief 运行时快照（§9.1——十段编译链 S10 的唯一对外发布产物）。
 *
 * 生命周期与所有权（§9.1 结构级约定）：由 IRuntimeSnapshotFactory 构造，
 * 经 shared_ptr<const RuntimeSnapshot> 共享；最后一个消费者释放即析构
 * （§9.3——DWC→WC 顺序 §8.5，私有成员声明序强制：m_dynamicWorkCell 在
 * m_workCell 之前，析构逆序保证 WC 实际释放不早于 DWC——D-14/v0.9⑤）。
 *
 * 不可变性：构造完成后只读——无任何修改途径；所有字段构造期一次性赋值
 * （§9.1 原文）；禁拷贝/禁移动（成员含绑定映射的解析器地址——实例地址
 * 即身份，共享一律经 shared_ptr）。
 *
 * 零外部依赖（§9.3）：不持 project 存储上下文/查询端口句柄——编译完成后
 * 注入接口不再被引用；项目切换后在途运行持有的旧快照继续支持迟到结果的
 * 名称反解（用结果绑定快照的 RuntimeNameMap——绝不用当前映射）与证据补录。
 *
 * 并发（§9.2）：构造后只读＋只读视图→跨线程共享安全；makeState 每线程
 * 独立值拷贝；发布＝shared_ptr 原子交接（happens-before 由其保证）。
 *
 * CR-05 值供给：modelIdentity()/nameMapIdentity()/robworkBaselineVersion()
 * （＋compilerContractVersion()）是 evidence 侧的**唯一**取值入口——组装方
 * 值传递录入切片 Environment 条目，evidence 禁止重算/重编码 CanonicalModel
 * （units/evidence.md §4.1.1 CR-05 裁决；联合断言样例见 SnapshotTest）。
 */
class RuntimeSnapshot final : public IRuntimeModelView {
public:
    /// 析构（§8.5 销毁顺序——DWC 先于 WC 释放；声明序承载，见类注释）。
    ~RuntimeSnapshot() override;

    RuntimeSnapshot(const RuntimeSnapshot&) = delete;            ///< 不可拷贝（见类注释）
    RuntimeSnapshot& operator=(const RuntimeSnapshot&) = delete; ///< 不可赋值

    // ---- §9.1 字段访问器（声明序＝字段表行序；构造后不变）----

    /// 项目身份（来源定位四元组之一）。
    const core::ProjectId& project() const noexcept { return m_fields.project; }
    /// 方案分支身份。
    const core::BranchId& branch() const noexcept { return m_fields.branch; }
    /// 修订身份。
    const core::RevisionId& revision() const noexcept { return m_fields.revision; }
    /// 修订序号（快照层入身份——见 snapshotidentity::Fields 注释）。
    std::uint64_t revisionSeq() const noexcept { return m_fields.revisionSeq; }
    /// 模型内容身份（＝CanonicalModel.contentIdentity——CR-05 值供给①；
    /// evidence Environment 条目 `runtime.model-identity` 的值＝其规范文本）。
    const core::ContentIdentity& modelIdentity() const noexcept
    {
        return m_fields.modelIdentity;
    }
    /// 映射内容身份（＝RuntimeNameMap.contentIdentity——CR-05 值供给②；
    /// CON-06 接纳核对的绑定凭据）。
    const core::ContentIdentity& nameMapIdentity() const noexcept
    {
        return m_fields.nameMapIdentity;
    }
    /// 名称生成规则版本（§7.1——缓存键分量）。
    std::uint32_t nameMapRuleVersion() const noexcept { return m_fields.nameMapRuleVersion; }
    /// WC 层编译缓存键（§9.1 公式——RT-T10 复用同一计算单点）。
    const core::ContentIdentity& workCellCompileIdentity() const noexcept
    {
        return m_fields.workCellCompileIdentity;
    }
    /// DWC 编译事实状态（§9.1 dynamicWorkCellState——Compiled/SkippedNoPhysics）。
    DwcSnapshotState dynamicWorkCellState() const noexcept
    {
        return m_fields.dynamicWorkCellState;
    }
    /// Skipped 缺失对象清单（§9.1"Skipped 时的缺失对象清单"；链序；
    /// Compiled 或"未请求 DWC"时为空）。
    const std::vector<core::ObjectId>& skippedDynamicObjects() const noexcept
    {
        return m_fields.skippedDynamicObjects;
    }
    /// 编译器契约版本（CR-05 值供给④——evidence 复现块字段）。
    std::uint32_t compilerContractVersion() const noexcept
    {
        return m_fields.compilerContractVersion;
    }
    /// 编译器实现版本（非空——§9.1 合法列）。
    const std::string& compilerVersion() const noexcept { return m_fields.compilerVersion; }
    /// RobWork 基线版本（CR-05 值供给③——Environment 条目
    /// `runtime.robwork-baseline` 的值；基线变化＝产物不可比，NFR-DEP-05）。
    const std::string& robworkBaselineVersion() const noexcept
    {
        return m_fields.robworkBaselineVersion;
    }
    /// 编码器版本三元组（与各编码头一致——§9.1 合法列）。
    const SnapshotCodecVersions& codecVersions() const noexcept { return m_fields.codecVersions; }
    /// 编译选项（全集——编译时的实际请求记录）。
    const CompileOptions& compileOptions() const noexcept { return m_fields.compileOptions; }
    /// 资源清单（与 model.resourceManifest 一致——§9.1 合法列；冗余直查面）。
    const std::vector<ResourceRef>& resourceManifest() const noexcept
    {
        return m_fields.resourceManifest;
    }
    /// 警告级诊断集（模型警告＋编译链警告合并；**不入身份**——§9.1"过程记录"）。
    const std::vector<core::DiagnosticRecord>& diagnostics() const noexcept
    {
        return m_diagnostics;
    }
    /// 创建时刻（UTC；观测性要素不入身份——§9.1"不入"列）。
    std::chrono::system_clock::time_point createdAtUtc() const noexcept
    {
        return m_createdAtUtc;
    }
    /// 创建来源（Command/EvaluationPrepare/Worker；不入身份）。
    SnapshotOrigin createdFrom() const noexcept { return m_createdFrom; }
    /// 快照身份（＝SHA-256 over 全部"入"字段编码——§9.1 派生行；CON-02
    /// 结果绑定的关联键）。
    const core::ContentIdentity& snapshotIdentity() const noexcept
    {
        return m_snapshotIdentity;
    }

    // ---- IRuntimeModelView（§8.3——统一只读入口；语义见接口注释）----

    const CanonicalModel& model() const override;
    const RuntimeNameMap& nameMap() const override;
    const RuntimeCapability& capabilities() const override;
    const WorkCellConstView& workCell() const override;
    Expected<DynamicWorkCellConstView, RuntimeError> tryDynamicWorkCell() const override;
    rw::math::Transform3D<double> worldToBase() const override;
    rw::math::Transform3D<double> baseToWorld() const override;
    rw::math::Vector3D<double> gravityWorld() const override;
    rw::math::Vector3D<double> gravityBase() const override;
    rw::kinematics::State makeState() const override;
    const IRuntimeNameResolver& nameResolver() const override;

private:
    // RuntimeSnapshotFactory 与 SnapshotAssembler（实现文件的装配尾段类，
    // create/materialize 的 S8＋S10 公共路径）是唯一构造路径——"构造后
    // 只读"由私有构造＋friend 成立。
    friend class RuntimeSnapshotFactory;
    friend class SnapshotAssembler;

    /**
     * @brief 私有构造（工厂装配尾段调用——§9.1 全字段构造期一次性赋值＋
     *        发布门禁校验，见实现）。
     *
     * @param fields          [in] §9.1"入"字段包（身份计算在成员初始化内完成）
     * @param model           [in] 规范模型（拷贝入快照——§9.1 私有 model_）
     * @param nameMap         [in] 名称映射（拷贝入快照；m_resolver 绑定其地址）
     * @param dynamicWorkCell [in] DWC 产物（SkippedNoPhysics 时为空——
     *                        引用计数借持转换为 const 视图面）
     * @param workCell        [in] WC 产物（非空——hasWorkCell 恒 true）
     * @param diagnostics     [in] 警告级诊断合并集（不入身份）
     * @param createdAtUtc    [in] 创建时刻（观测性要素）
     * @param createdFrom     [in] 创建来源（观测性要素）
     */
    RuntimeSnapshot(snapshotidentity::Fields fields, const CanonicalModel& model,
                    const RuntimeNameMap& nameMap,
                    rw::core::Ptr<const rwsim::dynamics::DynamicWorkCell> dynamicWorkCell,
                    rw::core::Ptr<const rw::models::WorkCell> workCell,
                    std::vector<core::DiagnosticRecord> diagnostics,
                    std::chrono::system_clock::time_point createdAtUtc,
                    SnapshotOrigin createdFrom);

    // ---- 私有产物持有（★ 声明序＝§8.5 销毁顺序契约：m_dynamicWorkCell
    //      必须先于 m_workCell 声明——析构逆序使 WC 实际释放不早于 DWC；
    //      v0.9⑤ RT-T09 强制项。类型用 rw::core::Ptr<const T> 模板实例——
    //      基线的 DynamicWorkCell::Ptr 嵌套别名要求完整类型，公共面以
    //      前置声明＋模板指针持有（§8.2 只读包装纪律））----
    /// DWC 编译产物（引用计数持有；SkippedNoPhysics 时为空——capability 表达缺失）。
    rw::core::Ptr<const rwsim::dynamics::DynamicWorkCell> m_dynamicWorkCell;
    /// WC 编译产物（引用计数持有；恒非空——hasWorkCell 恒 true）。
    rw::core::Ptr<const rw::models::WorkCell> m_workCell;
    /// WC 只读视图（借持 m_workCell——构造期一次建立，随快照存活）。
    WorkCellConstView m_workCellView;

    // ---- 值成员（声明序：m_nameMap 在 m_resolver 之前——解析器绑定其地址；
    //      实例禁拷贝/移动保证地址稳定）----
    snapshotidentity::Fields m_fields;   ///< §9.1"入"字段包（＝字段访问器真值）
    CanonicalModel m_model;              ///< 私有 model_（§9.1——经 view 暴露）
    RuntimeNameMap m_nameMap;            ///< 私有 nameMap_（⑥端口真值）
    BoundRuntimeNameResolver m_resolver; ///< 绑定 m_nameMap 的解析器（§7.3 端口实现）
    std::vector<core::DiagnosticRecord> m_diagnostics; ///< 警告级诊断（不入身份）
    std::chrono::system_clock::time_point m_createdAtUtc; ///< 创建时刻（不入身份）
    SnapshotOrigin m_createdFrom = SnapshotOrigin::Command; ///< 创建来源（不入身份）
    core::ContentIdentity m_snapshotIdentity; ///< 快照身份（构造期计算）
};

// =====================================================================
// MaterializedSnapshotCodec——worker 物化编码（§9.5；§3.1 模块清单实体；
// RT-Codec 家族子形态，magic IRDMAT1）。
// =====================================================================

namespace snapshotcodec {

/// 魔数 "IRDMAT1"（§9.5 原文——worker 物化编码家族标识）。
inline constexpr std::array<std::uint8_t, 7> kMagic{'I', 'R', 'D', 'M', 'A', 'T', '1'};
/// 结构版本 major（编码升版＝破坏性变更，走设计变更评审——同 RT-Codec 家族）。
inline constexpr std::uint16_t kVersionMajor = 1;
/// 结构版本 minor。
inline constexpr std::uint16_t kVersionMinor = 0;

/**
 * @brief 物化载荷（§9.5 内容面：RevisionSummary＋对象字节＋CompileOptions
 *        ＋编译身份预期值）。
 *
 * 字段语义（§9.5 原文"用途＝execution 派发通道序列化；不是项目持久化
 * 格式（不入 .rwdesign；与 io/project 格式无关）"）：
 *   - revision：修订摘要（execution 通道的定位面；快照四元组以模型头为准）；
 *   - canonicalModelBytes：被消费闭包的编译对象承载——CanonicalModel 的
 *     IRDCANO 全字段编码（分阶段口径见文件头 v0.10 登记：worker 无 reader，
 *     编译后物化而非字节重编译——D-01；rtcodec::parse 重建时叠加 builder
 *     全量复核＋身份复核＝篡改/半传输的模型层防线）；
 *   - nameMapBytes：IRDNAME 编码（映射重建——worker 不重算生成规则，
 *     parseNameMap 结构复核＋身份重算）；
 *   - options：编译选项（worker 侧 S7 请求级别等的一致性输入）；
 *   - compilerContractVersion/compilerVersion：编译器版本面（快照身份与
 *     WC 缓存键的分量——§9.1；worker 不持有编译器实例，版本随载荷值传递
 *     保证重建快照 snapshotIdentity 与主进程产物逐字节相等）；
 *   - expectedModelIdentity/expectedNameMapIdentity：编译身份预期值——
 *     worker 重建后逐项核对（D-13：不相等拒绝执行；§10.0 materialize 行
 *     "身份核对失败返回 Failed（诊断含期望/实得身份）"）。
 *
 * "资源字节"分阶段（v0.10 登记）：资源几何挂接（v0.8⑦）与 S4 复查
 * （RT-T11）落位时扩充本载荷；本版不含。
 * 值语义纯结构；线程安全。
 */
struct MaterializedPayload {
    RevisionSummary revision;                        ///< 修订摘要（§9.5 内容面）
    std::vector<std::uint8_t> canonicalModelBytes;   ///< 模型 IRDCANO 编码（非空）
    std::vector<std::uint8_t> nameMapBytes;          ///< 映射 IRDNAME 编码（非空）
    CompileOptions options;                          ///< 编译选项
    std::uint32_t compilerContractVersion = 0;       ///< 编译器契约版本（快照身份分量）
    std::string compilerVersion;                     ///< 编译器实现版本（非空）
    core::ContentIdentity expectedModelIdentity;     ///< 预期模型身份（非零）
    core::ContentIdentity expectedNameMapIdentity;   ///< 预期映射身份（非零）
};

/**
 * @brief 载荷确定性编码（§9.5——"确定性编码（RT-Codec 家族，magic
 *        IRDMAT1）"的 encode 侧）。
 *
 * 布局（大端/长度前缀/无填充；字段按结构声明序）：
 *   magic(7) | major(2) | minor(2)
 *   revision{id(16) seq(8) parent{presence(1)[+16]} branch(16)
 *            objectRefs{count(4) 逐条{objectId(16) cv(32)
 *            objectTypeToken{len(4)+UTF-8} digest(32)}}}
 *   canonicalModelBytes{len(8)+字节}
 *   nameMapBytes{len(8)+字节}
 *   options{includeCollisionGeometry(1) geometryDetail(4) requestDynamicWorkCell(1)}
 *   compilerContractVersion(4) | compilerVersion{len(8)+UTF-8}
 *   expectedModelIdentity(32) | expectedNameMapIdentity(32)
 *
 * @param payload [in] 物化载荷（只读）
 * @return 编码字节（确定性——同载荷逐字节相等；调用方持有）
 *
 * @throws RuntimeError 码＝InputInvalid：载荷非法（模型/映射字节为空、
 *         预期身份为全零保留值——空载荷无物化语义，NFR-COR-03 不吞错）
 *
 * 线程/确定性：纯函数、可重入、无 I/O、无隐藏状态。
 */
std::vector<std::uint8_t> encode(const MaterializedPayload& payload);

/**
 * @brief 载荷解码（§9.5 的 parse 侧；只做字节面校验——快照级身份核对归
 *        IRuntimeSnapshotFactory::materialize 的 D-13 断言）。
 *
 * 校验链（任一失败返回 err，不抛、不产出半成品——NFR-COR-03）：
 *   ①magic/版本匹配（版本不符＝拒绝而非尽力猜测）；
 *   ②长度前缀逐字段解码——越界/截断/尾随字节/非法 presence 值→
 *     InputInvalid（detail 携字节偏移定位）。
 *
 * @param bytes [in] encode() 产出的编码（只读；可为任意来源——防御性校验）
 * @return ok＝解码载荷；err＝校验失败（RuntimeError 携 InputInvalid 与
 *         字节偏移定位）
 *
 * 线程/确定性：纯函数、可重入；同字节→同结果（NFR-COR-02）。
 */
Expected<MaterializedPayload, RuntimeError> parse(const std::vector<std::uint8_t>& bytes);

}  // namespace snapshotcodec

// =====================================================================
// IRuntimeSnapshotFactory/RuntimeSnapshotFactory——快照工厂（§10.0 原文
// 契约："S1–S10 编排 + worker 物化"）。
// =====================================================================

/**
 * @brief 快照工厂抽象（§10.0 原文契约——create 编排＋materialize 重建）。
 *
 * 接口属性（§10.0 属性表 create/materialize 行）：
 *   - create 前置＝注入源非空且一致（同一修订）＋编译器实例有效；后置＝
 *     Published→快照唯一出口，Failed/Cancelled→无快照＋诊断全量；
 *   - materialize＝worker 侧从物化字节重建独立快照；身份核对失败返回
 *     Failed（诊断含期望/实得身份——D-13）；
 *   - 错误：见 §5.2 各段诊断码；线程：允许后台线程（UI 线程禁止编译——
 *     ARCH §4.2），同一请求不可并发重入；确定性：同输入同身份同名称；
 *   - 写权限/副作用：只读输入、无磁盘写；快照 shared_ptr 归调用方与全部
 *     下游消费者。
 *
 * 非法调用（§10.0 属性表）：发布后继续向编译器写入同请求；忽略 status
 * 直接取 snapshot（Published 判别是契约——Failed 时解引用空句柄未定义）。
 */
class IRuntimeSnapshotFactory {
public:
    virtual ~IRuntimeSnapshotFactory() = default;

    /**
     * @brief 编排编译并发布快照（S1–S10——§10.0 create 行）。
     *
     * 编排分工（§10.0 原文——工厂经编译器分段入口）：S1–S5 委托
     * compiler.buildCanonicalModel()；S6–S10 由工厂执行（S6/S7 调用编译器
     * 单元、S8 映射＋交叉校验、S10 装配发布——段边界轮询取消令牌）。
     *
     * @param request  [in] 编译请求（注入源＋选项＋取消令牌）
     * @param compiler [in] 分段编译器（S1–S5；调用方保证请求期间存活）
     * @return Published（快照唯一出口）／Failed（诊断全量）／Cancelled
     *         （取消诊断非 error——§9.6 组合表）
     *
     * @throws RuntimeError 码＝UnknownObject/ContextReleased（调用方契约
     *         违约——§3.4 总纲 fail-fast 轨：对不存在修订/已释放上下文的
     *         编译请求不走诊断收集；S1 归属表见 Sources.hpp 文件头）
     */
    virtual CompileOutcome create(const CompileRequest& request,
                                  ICanonicalModelCompiler& compiler) = 0;

    /**
     * @brief worker 侧物化重建（§9.5/§9.2——独立快照，不共享主进程内存）。
     *
     * 重建链：载荷解码→模型/映射重建（rtcodec parse 全量复核）→D-13 身份
     * 核对（modelIdentity/nameMapIdentity 与预期值逐一相等）→S6/S7/S8→
     * S10 发布（createdFrom=Worker）。
     *
     * @param materializedBytes [in] snapshotcodec::encode 产出的物化字节
     * @return Published＝重建快照（身份与主进程产物相等——RT-SNAP-2）；
     *         Failed＝任一校验失败（诊断含期望/实得身份——§10.0 原文）
     *
     * @throws 无（字节面失败全部经返回值表达——worker 通道的输入是外部
     *         数据，属可恢复错误轨）
     */
    virtual CompileOutcome materialize(const std::vector<std::uint8_t>& materializedBytes) = 0;
};

/**
 * @brief 快照工厂唯一实现（S6–S10 编排＋D-13 物化重建）。
 *
 * 状态：无状态、可重入（并发 create 各自请求——§10.0 属性表；多次 create
 * 复用同一实例——RT-SNAP 用例的可重入重试面）。确定性：同输入→同身份/
 * 同名称（时间戳与诊断不入身份——§9.1）。
 * 线程安全：create/materialize 可多线程并发（全部共享量为只读/局部）。
 */
class RuntimeSnapshotFactory final : public IRuntimeSnapshotFactory {
public:
    RuntimeSnapshotFactory() = default;

    CompileOutcome create(const CompileRequest& request,
                          ICanonicalModelCompiler& compiler) override;
    CompileOutcome materialize(const std::vector<std::uint8_t>& materializedBytes) override;
};

}  // namespace sdurws::ird::runtime

#endif  // SDURWS_IRD_RUNTIME_SNAPSHOT_HPP
