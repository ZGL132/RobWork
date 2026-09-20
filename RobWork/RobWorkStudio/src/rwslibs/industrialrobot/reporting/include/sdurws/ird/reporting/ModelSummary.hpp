/**
 * @file   ModelSummary.hpp
 * @brief  模型摘要投影 schema（ModelSummary）与 runtime 只读摘要注入接口
 *         （IModelSummaryProvider）——runtime §13.2 交接的承接冻结（RPT-T12
 *         产物，卡行"schema 冻结留痕（runtime 交接）"）。
 *
 * 设计依据：
 *   - units/reporting.md §9.7（IModelSummaryProvider 契约原文——本头逐字段
 *     落位的唯一权威：字段名/类型/注释行一一对应）、§3.3（runtime 摘要＝
 *     注入式最小接口＋值传递——公共头零 runtime 类型）、§9.9 runtime 行
 *     （摘要引用＝身份值〔modelIdentity 等〕；不触 WC/DWC）、§12.1 runtime.md
 *     §13.2 行（"已冻结（L5 适配）"的承接答复）、§2.1 C-6（IModelSummaryProvider
 *     消费行——RPT-01-B 输入摘要章节）
 *   - runtime.md §10.12（与 reporting 协作口径——P-RPT-9 基线：runtime.md
 *     v0.1 Draft-Structured；冻结 diff 后按影响面增量同步）：只读投影不含
 *     RobWork 对象；快照已释放→以归档的快照身份元数据呈现；reporting 不
 *     触发编译、不直接读 WC/DWC；runtime 不依赖 reporting（单向）
 *   - 需求 RPT-01-B（输入摘要章节的数据源契约——本 schema 的服务对象）、
 *     CON-05/06（内容身份值传递）、TASK-02 关联（快照身份呈现）
 *   - 任务契约 tasks/foundation/RPT-T12.json acceptance 1/2/4/5
 *
 * 背景说明（本头在报告链路中的位置——为什么它是"投影"而非"封装"）：
 *   报告的 input-summary 章节要呈现"这份评估针对的是哪一个模型快照、它有
 *   哪些能力、用了哪些资源"。这些事实的真值归 runtime（编译产出的快照），
 *   而 reporting 与 runtime 之间没有编译期依赖边（ARCH §3.5 未登记——
 *   P-RPT-2 注入形态）。解决方式与 evidence.md §3.3（IObjectBytesSource）、
 *   io.md IO-D02（IoRuntime 注入）同一模式：reporting 自有值类型定义
 *   "摘要长什么样"（本头 ModelSummary），runtime 侧的事实经 L5 装配的适配
 *   器（IRuntimeModelView→IModelSummaryProvider）以**值拷贝**进入 reporting
 *   ——公共头零 runtime 类型（acceptance 3 同款纪律：表外边＝构建失败，
 *   SA-10），适配代码归 L5 装配，本单元不建 runtime 编译边。
 *
 * schema 冻结声明（acceptance 1——"schema 冻结留痕（runtime 交接）"）：
 *   本结构字段集与 §9.7 契约原文逐字段一致（核对结论登记
 *   traceability/builds/wp12-t12/schema-freeze-note.md——runtime 侧按该
 *   凭据实现 IRuntimeModelView→IModelSummaryProvider 适配）；字段**只增不改**
 *   （与 §5.1 词表同纪律）：任何字段调整＝runtime 交接契约变更，必须走
 *   单元卡变更记录（§14.4）并同步 runtime 侧交接登记，不允许静默修改。
 *   installPresetToken 为呈现字段、不入任何身份判定（§9.7 维度表"非法调用"
 *   行——本头因此不提供任何以它为键的查询/判定面）。
 *
 * P-RPT-2 处置（acceptance 4）：维持注入式——签名（trySummary 的形参/返回
 *   类型/const 限定）在架构所有者确认注入为正式形态或补边裁决前**冻结不变**
 *   （契约测试 TrySummarySignatureFrozen 的 static_assert 常驻自证）；裁决
 *   补边后签名零改动直连 runtime。
 *
 * 线程安全：实现方承诺并发只读安全（§9.7 线程行"并发只读安全（runtime
 *   承诺）"）；本头全部类型为纯值类型（无共享可变状态）。
 * 确定性：同修订同投影（值语义——§9.7 确定性行；单元测试
 *   TrySummaryDeterministic 钉住）。
 */

#ifndef SDURWS_IRD_REPORTING_MODELSUMMARY_HPP
#define SDURWS_IRD_REPORTING_MODELSUMMARY_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <sdurws/ird/core/Digest.hpp>      // core::ContentIdentity/core::Digest256（内容身份值——core.md §4.2；P-RPT-9 基线＝core.md v0.1 Draft）
#include <sdurws/ird/core/Identity.hpp>    // core::RevisionId（修订身份——core.md §4.1）

namespace sdurws::ird::reporting {

// =====================================================================
// 资源状态投影词表（CON-03 固化记录的两态）
// =====================================================================

/**
 * @brief 资源状态投影（§9.7 resources 行 "state(Solidified/Recorded)" 的
 *        词面承载——落位增量，§14.4 v0.15 登记）。
 *
 * 为什么是 reporting 自有枚举而不是复用某类型：runtime 的 ResourceState
 * 属 runtime 公共面，本头受"零 runtime 类型"纪律约束（§3.3 注入形态——
 * 公共头出现对端类型即表外边）。两态词值与 runtime §8.6 CON-03 词表同名
 * 一一对应（Solidified＝固化入库；Recorded＝登记引用），由 L5 适配器做
 * 一一映射；token 冻结（小写连字符，与 core 词表风格一致）。
 */
enum class ResourceStateKind {
    Solidified,  ///< 固化：资源字节已固化入库（内容摘要即其身份）
    Recorded,    ///< 登记：资源以引用登记（读取走 provider——呈现如实标注）
};

/**
 * @brief 资源状态 token（持久化/呈现词面——只增不改名，§5.1 词表纪律同源）。
 * @param v [in] 资源状态投影值
 * @return "solidified" / "recorded"（kebab 小写；未知值不可达——穷举 switch 无 default 防漏）
 */
inline const char* toToken(ResourceStateKind v) noexcept
{
    switch (v) {
    case ResourceStateKind::Solidified: return "solidified";
    case ResourceStateKind::Recorded:   return "recorded";
    }
    return "recorded";  // 不可达（穷举 switch 已覆盖全值域）——兜底返回后值，防 MSVC 警告 C4715
}

// =====================================================================
// ModelSummary schema（§9.7 逐字段——冻结）
// =====================================================================

/**
 * @brief 单条资源摘要（§9.7 resources 行 "{resourceId, state(Solidified/
 *        Recorded), digest}" 的承载）。
 *
 * resourceId 的类型说明：runtime 侧资源以 ResourceRef 标识，本投影以规范
 * 文本承载（零 runtime 类型纪律——L5 适配器负责把 ResourceRef 格式化为
 * 规范文本）；digest 为资源内容摘要（CON-03 固化记录的呈现面——全零＝
 * 未提供，保留值纪律与 core 同源）。
 */
struct ResourceSummary {
    std::string resourceId;  ///< 资源标识规范文本投影（非空——runtime ResourceRef 规范形）
    ResourceStateKind state = ResourceStateKind::Recorded;  ///< 资源状态（CON-03 两态词表——见上）
    core::Digest256 digest{};  ///< 资源内容摘要（SHA-256 原始字节；全零＝未提供——保留值）

    bool operator==(const ResourceSummary& o) const
    {
        return resourceId == o.resourceId && state == o.state && digest == o.digest;
    }
    bool operator!=(const ResourceSummary& o) const { return !(*this == o); }
};

/**
 * @brief 单条配置引用摘要（§9.7 configurations 行 "{configKindToken,
 *        contentIdentity}（AnalysisConfiguration 引用）"的承载）。
 *
 * 背景：评估配置（种子/线程数/容差等）的 canonical 字节归域单元 schema
 * 所有（N-5/N-9——reporting 不解包配置内容）；报告只呈现"用了哪类配置、
 * 其内容身份是什么"的**引用**（与 Bundle.hpp IEvidenceBundleSource::
 * tryConfigurationRefs 同一口径——§7.6）。种子等具体值不在本投影内呈现。
 */
struct ConfigSummary {
    std::string configKindToken;  ///< 配置种类 token（域词面投影——非空）
    core::ContentIdentity contentIdentity{};  ///< 配置内容身份（CON-06——canonical 字节的复合摘要）

    bool operator==(const ConfigSummary& o) const
    {
        return configKindToken == o.configKindToken && contentIdentity == o.contentIdentity;
    }
    bool operator!=(const ConfigSummary& o) const { return !(*this == o); }
};

/**
 * @brief 模型摘要投影 schema（§9.7 冻结——runtime §13.2 交接的承接物；
 *        RPT-01-B input-summary 章节的数据源）。
 *
 * 值语义：构造后即快照值（调用方持有拷贝——§9.7 "返回值归调用方"行），
 * 后续快照释放/修订演化不影响已取回的投影（报告内容只绑定明确 ID 的
 * 冻结事实——§3.3）。
 *
 * "可空"的承载说明（§9.7 后置行"其余字段可空"）：core::ContentIdentity
 * 为全零字节＝空（保留值纪律——core.md §4.2，isValid()==false 即"此身份
 * 未提供"）。**不伪造**：缺什么身份就留全零，配 provenanceNote 注记说明，
 * 绝不以猜测值填充（runtime §10.12"快照已释放→以归档的快照身份元数据
 * 呈现"——快照在案时全字段填充，已释放时仅 snapshotIdentity 等归档元数据
 * 在案的字段非空）。
 */
struct ModelSummary {
    /// 摘要所针对的修订身份（§9.7 revision——权威闭包内存在，前置保证非空）。
    core::RevisionId revision{};
    /// 快照身份块（§9.7 snapshotIdentity——快照在案/已释放均呈现的锚：已
    /// 释放场景按归档元数据填充，恒 isValid——§9.7 后置行"snapshotIdentity
    /// 非空"）。
    core::ContentIdentity snapshotIdentity{};
    /// 模型身份（runtime 计算——含编译器契约版本，evidence v0.2 口径；快照
    /// 已释放时可为空＝全零）。
    core::ContentIdentity modelIdentity{};
    /// RuntimeNameMap 内容身份（CON-06 呈现——名称映射的集合身份；快照已
    /// 释放时可为空）。
    core::ContentIdentity nameMapIdentity{};
    /// 已解析策略内容身份（快照 policyRef 值传递——§9.7 原文；策略内容身份
    /// 归 evidence 切片域，runtime 经快照携带；快照已释放时可为空）。
    core::ContentIdentity policyContentIdentity{};
    /// 轴数（关节计数——链型摘要；无量纲计数。快照已释放时可为 0＝未提供）。
    std::uint32_t jointCount = 0;
    /// 全旋转关节标记（链型摘要——false 含"未提供"语义，配注记消歧；
    /// §9.7 "轴数与全旋转"行）。
    bool allRevolute = false;
    /// 安装预设呈现 token：Ground/Inverted/Wall/Custom（§9.7 原文"呈现用，
    /// 不入身份"——**禁止**参与任何身份判定/缓存键/等价判定；本头不提供以
    /// 它为键的任何查询面——§9.7 非法调用行"以 installPreset 参与任何判定"
    /// ＝禁）。
    std::string installPresetToken;

    /**
     * @brief 能力声明摘要（§9.7 Capabilities——runtime RuntimeCapability
     *        八布尔位的值投影；MDL-06/DYN-06 能力正交语义的呈现面——
     *        false＝该能力缺失（降级非阻断），消费方按 §5.6 口径呈现）。
     *
     * 八位与 runtime §9.6 RuntimeCapability 声明位一一对应（L5 适配器
     * 映射）；本结构不设默认判定——bool 缺省 false 仅为聚合初始化便利，
     * 语义上"未提供"与"声明缺失"均如实按 false 呈现并由注记消歧。
     */
    struct Capabilities {
        bool hasDynamicWorkCell = false;      ///< 有动力学工作单元（DWC 可构造——物性齐备）
        bool hasFullMassInertia = false;      ///< 质量与惯量数据齐备
        bool hasJointVelocityLimits = false;  ///< 关节速度限值在案
        bool hasCollisionGeometry = false;    ///< 碰撞几何在案
        bool hasTools = false;                ///< 工具（TCP/工具几何）在案
        bool hasScene = false;                ///< 场景几何在案
        bool hasFrictionModel = false;        ///< 摩擦模型在案
        bool hasCouplingMatrix = false;       ///< 耦合矩阵在案（MDL-21）
    } capabilities;  ///< 能力声明八布尔（§9.7 原文名 Capabilities 内嵌——字段序即声明序）

    /// 资源清单投影（CON-03——逐条资源的状态与摘要；顺序＝runtime 提供序，
    /// 投影不重排——确定性由"同修订同投影"约束实现方保证）。
    std::vector<ResourceSummary> resources;
    /// 配置引用投影（§9.7 configurations——AnalysisConfiguration 引用；
    /// 顺序同上）。
    std::vector<ConfigSummary> configurations;

    /**
     * @brief 呈现注记（**落位增量**——§14.4 v0.15 登记，DTB §5.4）。
     *
     * §9.7 结构体原文无注记位，而后置行明确"快照已释放→……＋注记（不
     * 伪造）"——注记须有 schema 承载位。约定：空串＝快照在案、全字段完整
     * 投影（无注记必要）；非空＝如实注记（典型即"快照已释放，按归档身份
     * 元数据呈现"场景——哪些字段为空、为何为空，由注记人读说明承载）。
     * 注记参与值相等比较（同修订同投影确定性的组成部分）。
     */
    std::string provenanceNote;

    /// 全字段精确相等（值语义确定性——同修订同投影的比较面；NFR-COR-02）。
    bool operator==(const ModelSummary& o) const
    {
        return revision == o.revision
            && snapshotIdentity == o.snapshotIdentity
            && modelIdentity == o.modelIdentity
            && nameMapIdentity == o.nameMapIdentity
            && policyContentIdentity == o.policyContentIdentity
            && jointCount == o.jointCount
            && allRevolute == o.allRevolute
            && installPresetToken == o.installPresetToken
            && capabilities.hasDynamicWorkCell == o.capabilities.hasDynamicWorkCell
            && capabilities.hasFullMassInertia == o.capabilities.hasFullMassInertia
            && capabilities.hasJointVelocityLimits == o.capabilities.hasJointVelocityLimits
            && capabilities.hasCollisionGeometry == o.capabilities.hasCollisionGeometry
            && capabilities.hasTools == o.capabilities.hasTools
            && capabilities.hasScene == o.capabilities.hasScene
            && capabilities.hasFrictionModel == o.capabilities.hasFrictionModel
            && capabilities.hasCouplingMatrix == o.capabilities.hasCouplingMatrix
            && resources == o.resources
            && configurations == o.configurations
            && provenanceNote == o.provenanceNote;
    }
    bool operator!=(const ModelSummary& o) const { return !(*this == o); }
};

// =====================================================================
// IModelSummaryProvider（§9.7 注入接口——P-RPT-2 处置形态）
// =====================================================================

/**
 * @brief runtime 只读摘要提供方（注入接口——L5 装配适配 runtime 的
 *        IRuntimeModelView 只读投影）。
 *
 * 生命周期与所有权：实现方（L5 装配）持有 runtime 侧上下文；本接口指针
 * 由消费方（构建器 Resolving 步④——§9.7 调用示例行）以借用语义在会话内
 * 使用，不接管实现对象所有权。适配器随存储上下文（或 L5）存活（§9.7
 * "取消/生命周期/所有权"行）。
 *
 * 非法调用（§9.7 维度表原文——acceptance 5 禁项锁定）：
 *   - 经本接口要求编译/读取 WC/DWC 实例＝禁（runtime §10.12——本接口
 *     是只读摘要投影，不存在触发编译或返回 WC/DWC 对象的通道；类型层
 *     即无此形态——返回值是纯值 ModelSummary）；
 *   - 以 installPresetToken 参与任何判定＝禁（呈现字段——本接口面亦无
 *     以其为参的方法）；
 *   - DH/显式权威模式呈现不经本接口（§9.7 前置/后置行——CanonicalModel
 *     为显式规范化，权威模式呈现归 model 章节的 modeling 提供方，§9.2
 *     域注册通道）。
 */
class IModelSummaryProvider {
public:
    virtual ~IModelSummaryProvider() = default;

    /**
     * @brief 取修订的模型摘要投影（§9.7 原文 trySummary——签名冻结，
     *        P-RPT-2 裁决前不变）。
     *
     * 前置：revision 存在于权威闭包（调用方保证——构建器在锚定步已核对，
     * §7.2 步①；传入闭包外修订属调用方违约，实现方按 try 轨返回 nullopt
     * 不抛——§1.4 try* 约定的注入面形态）。
     *
     * 后置：值拷贝投影（零 RobWork 对象——runtime §10.12 口径）；
     *   - revision 在案且快照在案→全字段投影，provenanceNote 为空；
     *   - revision 在案且快照已释放→按 runtime §10.12 以归档的快照身份
     *     元数据呈现：snapshotIdentity 非空（isValid）、其余身份字段可空
     *     （全零＝未提供）＋provenanceNote 非空注记——不伪造（acceptance 2）；
     *   - revision 不存在于权威闭包→nullopt（不伪造空摘要——空模型没有
     *     呈现意义，缺数据≠全零数据）。
     *
     * @param revision [in] 目标修订身份（core 强类型 "rev-<32hex>"）
     * @return 摘要投影值；修订不在权威闭包＝nullopt（不抛）
     *
     * 线程安全：并发只读安全（§9.7 线程行——runtime 承诺）；对同修订幂等
     * （确定性——同修订同投影）。
     * 副作用：零（只读；不触发编译——runtime §10.12 禁项）。
     */
    virtual std::optional<ModelSummary> trySummary(core::RevisionId revision) const = 0;
};

// =====================================================================
// L5 适配建议（卡行产物列"＋L5 适配建议"——适配代码归 L5 装配，
// 本任务不建 runtime 编译边；此处为装配期指引，非本单元代码）
// =====================================================================
/*
 * 适配方向：runtime IRuntimeModelView（只读投影）→ reporting
 * IModelSummaryProvider（§12.2 runtime 行"L5 适配器"登记）。
 *
 * 装配要点（供 L5 装配会话执行，非本单元义务）：
 *   1. 适配器持有 runtime 快照上下文的弱引用/句柄（不延长快照生命周期——
 *      快照释放是正常路径而非错误，见后置第 2 支）；
 *   2. trySummary(rev) 映射：以 rev 向 runtime 查询其只读视图——
 *      快照在案→逐字段填充 ModelSummary（identity 四元 identity 块/
 *      jointCount/allRevolute 链型摘要/installPresetToken 呈现 token/
 *      capabilities 八布尔/resources·configurations 逐条值拷贝）；
 *      快照已释放→以归档的快照身份元数据填充（snapshotIdentity 恒在案、
 *      其余仅归档在案者）＋provenanceNote 注记；修订不在权威闭包→nullopt；
 *   3. 词面映射：runtime ResourceState→ResourceStateKind（同名一一）、
 *      安装预设→installPresetToken 四 token（Ground/Inverted/Wall/Custom）；
 *   4. 纪律：适配器不缓存跨修订摘要（同修订同投影由 runtime 幂等保证）；
 *      不经适配器触发编译（零编译通道）；不把 WC/DWC 对象或指针塞入投影。
 */

}  // namespace sdurws::ird::reporting

#endif  // SDURWS_IRD_REPORTING_MODELSUMMARY_HPP
