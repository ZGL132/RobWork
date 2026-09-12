/**
 * @file   Snapshot.hpp
 * @brief  正式分析快照（AnalysisSnapshot）——评估输入的不可变闭包载体：
 *         身份三元组＋对象引用闭包＋配置/策略/名称映射引用＋必验工况集＋
 *         采样计划＋复现块，以及其组装（SnapshotBuilder）、最小注入接口
 *         （IObjectBytesSource/IRevisionClosureSource）与规范编码
 *         （SnapshotCodec，refs-only＋materialized 双形态）。
 *
 * 设计依据：
 *   - units/evidence.md §4.1（AnalysisSnapshot 全节：§4.1.1 五概念区分、
 *     §4.1.2 字段表、§4.1.3 RequiredCaseSet、§4.1.4 SamplingPlanRef、
 *     §4.1.5 组装/一致读取/冻结协议）、§3.1 组成表（Snapshot.hpp 行）、
 *     §3.3（注入边界——IObjectBytesSource/IRevisionClosureSource 原文签名）、
 *     §5.1（snapshotId＝SnapshotCodec(refs-only) 的 SHA-256，身份纪律）、
 *     §5.2（canonical 编码规则：magic/大端/长度前缀/presence 字节/版本化）
 *   - 需求 CON-01（分析从完整不可变快照运行）、CON-03（外部资源三段边界——
 *     快照侧状态记录）、CON-05（内容寻址）、CON-06（策略/名称映射内容身份
 *     非空）、NFR-COR-02（确定性）、NFR-COR-03（缺失不伪造）
 *   - 任务契约 tasks/foundation/EV-T03.json（≙WP-05-T03）acceptance 1～3：
 *     EV-ID-1/3 用例、修订闭包拒绝（混入反例）用例、RequiredCaseSet 冻结
 *     承载（caseId 唯一/enabled＋mandatory 标记，O-14 保守字面）用例
 *
 * 背景说明（本头在证据链上的位置——为什么需要"快照"这个概念）：
 *   正式评估的一切结果（证据/判定/包络/缓存/当前性）都必须能回答"当时
 *   消费的是什么输入"。AnalysisSnapshot 就是这个答案的载体：请求方组装、
 *   evidence 校验冻结（§4.1.5 协议），冻结后无任何修改途径——后续所有
 *   身份（sliceId/inputBaselineId/证据摘要）都从它派生。Preview 草稿输入
 *   不走本协议（§4.1.1），不产生快照、无内容身份承诺。
 *
 * 实现形态说明：值类型为聚合结构（公共成员＋成对 ==/!=，无 setter——
 * "构造后不可变"由冻结协议保证：SnapshotBuilder::build() 是唯一合法
 * 生产者，产出后调用方按只读对待；EV-T02 DependencyEntry 同款纪律）；
 * builder/codec 的非平凡逻辑在 src/Snapshot.cpp（本头只放契约与纯声明）。
 *
 * 消费的 core 契约（core.md v0.1 基线——P-EV-1 状态锚点）：
 *   ProjectId/BranchId/RevisionId/ObjectId（§4.1/§5.1，Identity.hpp）、
 *   ContentVersion/ContentIdentity/Digest256/ContentDigester（§4.2/§5.2，
 *   Digest.hpp——"对什么字节做摘要"的 evidence 侧承接＝本单元 SnapshotCodec）。
 *
 * 线程安全：SnapshotBuilder 非线程安全（§4.1.5 原文"单线程、有限时长"——
 * 仅组装线程持有）；其余类型为纯值/纯函数（可重入）。AnalysisSnapshot
 * 冻结后只读，可跨线程共享（不可变承诺）。
 */

#ifndef SDURWS_IRD_EVIDENCE_SNAPSHOT_HPP
#define SDURWS_IRD_EVIDENCE_SNAPSHOT_HPP

#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/evidence/Dependency.hpp>
#include <sdurws/ird/evidence/Errors.hpp>

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sdurws::ird::evidence {

// =====================================================================
// 必验工况集合（§4.1.3 RequiredCaseSet——冻结的工况清单与凭据）
// =====================================================================

/// 工况身份（§4.1.3 caseId(core::ObjectId)——工况对象即普通 project 对象，
/// schema 归 requirements（REQ-04）；语义别名不引入新类型，强类型纪律不变）。
using CaseId = core::ObjectId;

/**
 * @brief 单条工况承载（§4.1.3 entries[] 行：{caseId, label, enabled,
 *        mandatory(必验标记)}）。
 *
 * O-14/P-EV-9 保守字面（任务契约 acceptance 3 编入）：enabled/mandatory
 * 的权威 schema 归 requirements 卡（WP-14-T01 冻结后回接本文 §4.1.3）——
 * evidence **只承载标记、不解释标记**（由需求对象在快照冻结时解析好的
 * 布尔值原样入集），不私裁字段语义、不派生任何判定。label 为展示文案
 * （非身份载体——进编码但不进判定语义）。
 *
 * 值语义；线程安全：纯值。
 */
struct CaseEntry {
    CaseId caseId;              ///< 工况对象身份（须 isValid——保留值拒绝）
    std::string label;          ///< 展示文案（UTF-8、禁 NUL；可空——设计未禁止）
    bool enabled = false;       ///< 启用标记（需求对象冻结时解析的字面承载）
    bool mandatory = false;     ///< 必验标记（enabled∧mandatory 工况必须被覆盖
                                ///  矩阵覆盖——EVI-02/EV-COV-1 消费，属 EV-T06）

    bool operator==(const CaseEntry& o) const
    {
        return caseId == o.caseId && label == o.label && enabled == o.enabled
            && mandatory == o.mandatory;
    }
    bool operator!=(const CaseEntry& o) const { return !(*this == o); }
};

/**
 * @brief 必验工况集合的冻结（§4.1.3）。
 *
 * 冻结凭据 requiredCaseSetId＝entries 规范编码摘要（builder 计算，非调用方
 * 申报——§6.6 覆盖矩阵与它核对，EV-COV-1/2 消费）。entries 允许空集
 * （P-EV-7：空必验工况集是"覆盖平凡完备、判定项不满足"场景的合法入口，
 * 保守处置在汇总判定层——DataInsufficient，不在组装层拒绝）。
 *
 * 值语义；线程安全：纯值（冻结后按不可变对待）。
 */
struct RequiredCaseSet {
    /// 工况条目（caseId 唯一——重复即组装拒绝，§4.1.2 非法实例原文；
    /// 存储序＝caseId 字典序（builder 冻结时规范化——同内容同序，NFR-COR-02）。
    std::vector<CaseEntry> entries;
    /// 冻结凭据＝entries 规范编码摘要（SHA-256；builder/codec 计算——非申报值）。
    core::ContentIdentity requiredCaseSetId;

    bool operator==(const RequiredCaseSet& o) const
    {
        return entries == o.entries && requiredCaseSetId == o.requiredCaseSetId;
    }
    bool operator!=(const RequiredCaseSet& o) const { return !(*this == o); }
};

// =====================================================================
// 采样计划引用（§4.1.4 SamplingPlanRef——冻结采样计划与样本集表达）
// =====================================================================

/**
 * @brief 冻结采样计划引用（§4.1.4 字段表）。
 *
 * 语义：sampleSetIdentity＝SHA-256 over（planContentIdentity ‖ 采样预算/
 * 种子参数 canonical）——身份对**计划参数**计算而非对枚举列表（KIN-04 确定性
 * 生成；大样本集不枚举入快照）。它随快照冻结即"复评不得增删更换样本"的
 * 凭据；求解类配置改变不碰它（D-04 双层身份），采样预算/种子改变则它变
 * （＝新一轮研究基准，比较基准检查拦截，§4.1.4 末段）。
 *
 * plannedPositionSamples/plannedPoseSamples：计划样本数（无量纲计数——
 * **分母来源**，KIN-04 R8）；0 合法（零样本场景，§6.6）。
 *
 * 值语义；线程安全：纯值。
 */
struct SamplingPlanRef {
    core::ObjectId regionObjectId;           ///< 工作区域对象（REQ-03；须 isValid）
    core::ContentIdentity planContentIdentity; ///< 采样计划（区域定义＋采样参数）
                                             ///  canonical 内容身份（须 isValid）
    std::uint64_t plannedPositionSamples = 0; ///< 计划位置样本数（计数，无量纲；0 合法）
    std::uint64_t plannedPoseSamples = 0;     ///< 计划位姿样本数（计数，无量纲；0 合法）
    core::ContentIdentity sampleSetIdentity;  ///< 样本集身份（对计划参数计算；须 isValid）

    bool operator==(const SamplingPlanRef& o) const
    {
        return regionObjectId == o.regionObjectId
            && planContentIdentity == o.planContentIdentity
            && plannedPositionSamples == o.plannedPositionSamples
            && plannedPoseSamples == o.plannedPoseSamples
            && sampleSetIdentity == o.sampleSetIdentity;
    }
    bool operator!=(const SamplingPlanRef& o) const { return !(*this == o); }
};

// =====================================================================
// 外部资源固化状态（§4.1.2 externalResources 行——CON-03/PM-01 三段边界
// 的快照侧记录；Verified 前置校验消费——§6.2，归 EV-T05）
// =====================================================================

/// 外部资源状态（§4.1.2 原文两态：Recorded｜Solidified）。
/// 状态机 Recorded→Solidified 单向（CON-03；反向不合法）。
enum class ExternalResourceStatus : std::uint8_t {
    Recorded,   ///< 已记录未固化（资源仍可被上游改写——Verified 模式阻断消费）
    Solidified, ///< 已固化（内容被钉住——solidifiedContentVersion 必须在场）
};

/**
 * @brief 单条外部资源固化状态记录（§4.1.2：{resourceId, state,
 *        solidifiedContentVersion?}）。
 *
 * presence 语义（§5.2"optional 缺失与空值不等价"）：Solidified 必须携带
 * solidifiedContentVersion 且非零；Recorded 必须不携带——"有值空串/零值"
 * 噪声在 builder 即时验证拒绝（§4.1.5④a 状态机合法）。
 *
 * 值语义；线程安全：纯值。
 */
struct ExternalResourceState {
    core::ObjectId resourceId; ///< 外部资源对象身份（须 isValid）
    ExternalResourceStatus state = ExternalResourceStatus::Recorded;
    /// 固化内容版本（仅 state==Solidified 允许存在且须非零；Recorded 必须缺席）。
    std::optional<core::ContentVersion> solidifiedContentVersion;

    bool operator==(const ExternalResourceState& o) const
    {
        return resourceId == o.resourceId && state == o.state
            && solidifiedContentVersion == o.solidifiedContentVersion;
    }
    bool operator!=(const ExternalResourceState& o) const { return !(*this == o); }
};

// =====================================================================
// 复现块（§4.1.2 reproduction 行——NFR-COR-02/RPT-03"必要复现信息"）
// =====================================================================

/**
 * @brief 复现要素块（§4.1.2 原文：{productVersion, evidenceContractVersion,
 *        codecVersions, compilerContractVersion?, collisionBackendVersion?}）。
 *
 * 这些版本作为 Environment 依赖进入切片身份（§4.2.1 Environment 行，EV-T04
 * 消费）——"算法/契约版本兼容"（CON-04）的快照侧源头。compilerContractVersion
 * 对消费 CanonicalModel 的评估必填、collisionBackendVersion 对策略启用碰撞
 * 的评估必填（谁必填由评估器依赖声明表达，evidence 不域判）——本结构只做
 * presence 承载与编码安全校验。
 *
 * 值语义；线程安全：纯值。
 */
struct ReproductionBlock {
    /// 产品版本（软件版本——复现的第一要素；非空、UTF-8、禁 NUL）。
    std::string productVersion;
    /// 评估契约版本（本单元证据契约版本；非空、UTF-8、禁 NUL）。
    std::string evidenceContractVersion;
    /// codec 版本族（snapshot/slice 等编码器版本——§5.2 版本化行的快照侧
    /// 登记；各元素非空禁 NUL，列表可空，保序承载——版本族的给出顺序即
    /// 组装方声明顺序，evidence 不排序、不解释）。
    std::vector<std::string> codecVersions;
    /// 编译器契约版本（可选——消费 CanonicalModel 的评估必填，CR-05）。
    std::optional<std::string> compilerContractVersion;
    /// 碰撞后端版本（可选——策略启用碰撞的评估必填）。
    std::optional<std::string> collisionBackendVersion;

    bool operator==(const ReproductionBlock& o) const
    {
        return productVersion == o.productVersion
            && evidenceContractVersion == o.evidenceContractVersion
            && codecVersions == o.codecVersions
            && compilerContractVersion == o.compilerContractVersion
            && collisionBackendVersion == o.collisionBackendVersion;
    }
    bool operator!=(const ReproductionBlock& o) const { return !(*this == o); }
};

// =====================================================================
// 引用条目（§4.1.2 字段表的 objectClosure/configurationRefs/policyRef/
// nameMapRef 行——"引用而非副本"，载荷物化经 IObjectBytesSource）
// =====================================================================

/**
 * @brief 对象引用闭包条目（§4.1.2 原文：{objectId, contentVersion,
 *        objectTypeToken, digest}）。
 *
 * 引用而非副本：对象字节不进闭包，物化经 IObjectBytesSource 按需取。
 * digest＝对象载荷字节的 SHA-256 原始摘要（core §4.2：ContentVersion 即
 * 内容摘要版本戳——digest 与 contentVersion 同源，前者裸字节、后者带
 * cv- tag 的强类型；builder 校验二者一致，冗余承载的防错一致性闸门，
 * 实现口径登记单元卡 v0.4）。
 *
 * 值语义；线程安全：纯值。
 */
struct ObjectRefEntry {
    core::ObjectId objectId;             ///< 逻辑对象身份（跨修订稳定——ARC-04）
    core::ContentVersion contentVersion; ///< 对象内容版本（修订内解析锚——CON-01）
    std::string objectTypeToken;         ///< 对象类型 token（归域；非空＋无 NUL）
    core::Digest256 digest;              ///< 对象载荷字节 SHA-256（与 contentVersion
                                         ///  同源——见类注释一致性闸门）

    bool operator==(const ObjectRefEntry& o) const
    {
        return objectId == o.objectId && contentVersion == o.contentVersion
            && objectTypeToken == o.objectTypeToken && digest == o.digest;
    }
    bool operator!=(const ObjectRefEntry& o) const { return !(*this == o); }
};

/**
 * @brief 分析配置引用条目（§4.1.2 configurationRefs 行：{configKindToken,
 *        canonicalBytes, contentIdentity}）。
 *
 * AnalysisConfiguration（KIN-13）等配置的**不透明** canonical 承载——schema
 * 归 kinematics 等域（N-5/N-9：配置语义不进 evidence），内容身份由 evidence
 * 计算（对 canonicalBytes 摘要——组装方也可申报，builder 校验一致性）。
 *
 * 值语义；线程安全：纯值。
 */
struct ConfigEntry {
    std::string configKindToken;              ///< 配置种类 token（归域；非空＋无 NUL）
    std::vector<std::uint8_t> canonicalBytes; ///< 域 canonical 字节（非空——CON-06
                                              ///  非空纪律同款；不透明承载）
    core::ContentIdentity contentIdentity;    ///< 配置内容身份（须 isValid）

    bool operator==(const ConfigEntry& o) const
    {
        return configKindToken == o.configKindToken && canonicalBytes == o.canonicalBytes
            && contentIdentity == o.contentIdentity;
    }
    bool operator!=(const ConfigEntry& o) const { return !(*this == o); }
};

/// 策略引用（§4.1.2 policyRef 行：已解析 EngineeringPolicySet 内容身份——
/// 解析归 policy（④端口），组装方值传递传入；evidence 永不解析策略内容）。
struct PolicyRef {
    core::ContentIdentity policyContentIdentity; ///< 须 isValid（CON-06 非空）

    bool operator==(const PolicyRef& o) const
    {
        return policyContentIdentity == o.policyContentIdentity;
    }
    bool operator!=(const PolicyRef& o) const { return !(*this == o); }
};

/// 名称映射引用（§4.1.2 nameMapRef 行：执行时 RuntimeNameMap 内容身份——
/// 生成归 runtime（⑥端口）；evidence 永不拼接/剥离名称，R-4）。
struct NameMapRef {
    core::ContentIdentity nameMapContentIdentity; ///< 须 isValid（CON-06 非空）

    bool operator==(const NameMapRef& o) const
    {
        return nameMapContentIdentity == o.nameMapContentIdentity;
    }
    bool operator!=(const NameMapRef& o) const { return !(*this == o); }
};

// =====================================================================
// AnalysisSnapshot——正式分析快照本体（§4.1.2 字段表，冻结后不可变）
// =====================================================================

/**
 * @brief 正式分析快照（§4.1.1："本次正式评估消费的完整不可变输入闭包"）。
 *
 * 生命周期与冻结纪律（§4.1.5③）：由 SnapshotBuilder::build() 一次性校验、
 * 计算 snapshotId 并返回；此后**无任何修改途径**——本结构不提供 setter，
 * 调用方按只读值对待（快照不可变性是证据链与缓存正确性的前提，PA-2 同源
 * 精神）。snapshotId 由 builder 计算（非调用方申报——§4.1.2 原文）。
 *
 * 字段含义逐项见各成员注释（＝§4.1.2 字段表原文）；非法实例清单（builder
 * 拒绝）见 SnapshotBuilder::build() 契约。
 *
 * 值语义（拷贝即深拷贝——全部成员为值/定长数组）；冻结后只读，可跨线程
 * 共享（不可变承诺）。确定性：同内容组装（任意插入序）必得同 snapshotId
 * （builder 冻结时对集合类成员做规范化排序——NFR-COR-02，登记单元卡 v0.4）。
 */
struct AnalysisSnapshot {
    // ---- 身份三元组（§4.1.2 行 1：project/branch/revision——revision 是
    // ---- 唯一对象解析锚，全部 (oid,cv) 闭包校验对它进行，§4.1.5①）----
    core::ProjectId project;   ///< 项目身份（须 isValid）
    core::BranchId branch;     ///< 方案分支身份（须 isValid）
    core::RevisionId revision; ///< 锚定修订身份（须 isValid；对象解析只对该修订）
    /// 项目内修订序号（project 分配；仅展示/排序，**不参与内容身份语义判断**
    /// ——§4.1.2 原文：不进 SnapshotCodec 编码、不进 snapshotId；parse 恢复
    /// 为默认 0，往返等值断言排除本字段——实现口径登记单元卡 v0.4）。
    std::uint64_t revisionSeq = 0;

    RequiredCaseSet caseSet;                   ///< 冻结必验工况集合（§4.1.3）
    /// 对象引用闭包（≥1；引用而非副本；存储序＝objectId 字典序——builder
    /// 冻结时规范化）。每条 (oid,cv) 必须属于锚定修订（防混入，§4.1.5①）。
    std::vector<ObjectRefEntry> objectClosure;
    /// 分析配置引用（可空集；存储序＝configKindToken 字典序——规范化）。
    std::vector<ConfigEntry> configurationRefs;
    PolicyRef policyRef;                       ///< 已解析策略内容身份（CON-06）
    NameMapRef nameMapRef;                     ///< RuntimeNameMap 内容身份（CON-06）
    /// 外部资源固化状态（可空集；存储序＝resourceId 字典序——规范化）。
    std::vector<ExternalResourceState> externalResources;
    /// 冻结采样计划引用（可空集；存储序＝regionObjectId 字典序——规范化）。
    std::vector<SamplingPlanRef> samplingPlans;
    ReproductionBlock reproduction;            ///< 复现要素（NFR-COR-02/RPT-03）
    /// 快照内容身份＝SHA-256 over SnapshotCodec(refs-only)（§5.1；builder
    /// 计算。revisionSeq 不参与——见其成员注释）。
    core::ContentIdentity snapshotId;

    /// 全字段成员精确等值（测试/codec 往返核对用；身份判定以 snapshotId 为准
    /// ——§5.1 字节等值。注意：仅 revisionSeq 不同的两实例 == 为 false 但
    /// snapshotId 相等——这正是"序号不参与内容身份"的直接推力）。
    bool operator==(const AnalysisSnapshot& o) const
    {
        return project == o.project && branch == o.branch && revision == o.revision
            && revisionSeq == o.revisionSeq && caseSet == o.caseSet
            && objectClosure == o.objectClosure && configurationRefs == o.configurationRefs
            && policyRef == o.policyRef && nameMapRef == o.nameMapRef
            && externalResources == o.externalResources && samplingPlans == o.samplingPlans
            && reproduction == o.reproduction && snapshotId == o.snapshotId;
    }
    bool operator!=(const AnalysisSnapshot& o) const { return !(*this == o); }
};

// =====================================================================
// 最小注入接口（§3.3——值传递＋最小只读端口；适配器归 L5 装配或请求方，
// 不归 evidence；运行时注入、零编译依赖——evidence 不链 project）
// =====================================================================

/**
 * @brief 对象字节来源（§3.3 原文签名——适配 project ②端口 tryObject）。
 *
 * 语义：按（对象身份＋内容版本）取对象 canonical 字节；取不到返回 nullopt
 * （不抛——调用方按"依赖不可解析"处置，§8.1 消费）。实现方负责字节与
 * contentVersion 的对应承诺；载荷级完整性复核（重算 SHA-256 比对，§4.1.5④b）
 * 由 evidence 在物化时执行——实现方错配会被 SnapshotIntegrity 拒绝。
 *
 * 生命周期：调用方持有并保证快照组装/物化期间存活；本单元不接管所有权。
 * 线程约束：实现方自行保证（组装协议单线程——§4.1.5）。
 */
class IObjectBytesSource {
public:
    virtual ~IObjectBytesSource() = default;
    virtual std::optional<std::vector<std::uint8_t>>
    tryObjectBytes(core::ObjectId, core::ContentVersion) const = 0;
};

/**
 * @brief 修订闭包来源（§3.3 原文签名——适配 project ②端口 revision()/head()
 *        的修订对象清单）。
 *
 * 语义：回答"(oid,cv) 是否属于该修订的对象清单"（§4.1.5① 防混入校验的
 * 唯一事实来源）。一致性读取保证：修订不可变（PA-2），组装期间 HEAD 前进
 * 不影响已取视图——实现方对同一 RevisionId 的回答必须稳定。
 *
 * 生命周期：调用方持有并保证 build() 调用期间存活；本单元不接管所有权。
 * 线程约束：build() 单线程调用（§4.1.5 组装协议）。
 */
class IRevisionClosureSource {
public:
    virtual ~IRevisionClosureSource() = default;
    virtual bool objectInRevision(core::RevisionId, core::ObjectId,
                                  core::ContentVersion) const = 0;
};

// =====================================================================
// SnapshotBuilder——组装/一致读取/冻结协议（§4.1.5：①锚定修订→②逐类录入
// →③冻结→④a builder 即时验证）
// =====================================================================

/**
 * @brief 快照组装器（§4.1.5 协议承载——单线程、有限时长）。
 *
 * 使用协议（与设计原文一一对应）：
 *   ①锚定修订：setIdentity() 一次给定 (project, branch, revision, seq)——
 *     之后一切对象解析只对该 revision（组装期间 HEAD 前进不影响——一致
 *     读取视图由不可变修订保证，PA-2）；
 *   ②逐类录入：对象闭包/工况集/配置引用/策略与名称映射身份/外部资源状态/
 *     采样计划/复现块（setter 链式，纯累加不做校验——全部校验集中在
 *     build()，保证单一确定性失败路径）；
 *   ③冻结：build(closureSource) 即时验证（④a）→ 计算 requiredCaseSetId 与
 *     snapshotId → 返回不可变 AnalysisSnapshot。冻结时刻＝build() 返回时。
 *
 * 错误语义：build() 的全部拒绝（字段完整性 CON-06 非空、修订闭包包含性
 * 防混入、工况唯一性、采样参数合法性、外部资源状态机、复现块完整性）抛
 * EvidenceError(SnapshotIncomplete)——调用方组装契约违约，fail-fast
 * （AGENTS 错误语义；detail 携带就地定位信息）。校验顺序固定（确定性——
 * 同一坏快照必报同一首错；顺序见 .cpp 实现头注释）。
 *
 * 线程约束：非线程安全——仅组装线程持有（§4.1.5 原文"单线程、有限时长"）。
 */
class SnapshotBuilder {
public:
    SnapshotBuilder() = default;

    /// ①锚定修订（身份三元组＋修订序号）。序号仅展示/排序——不参与身份。
    /// \return *this（链式）
    SnapshotBuilder& setIdentity(core::ProjectId project, core::BranchId branch,
                                 core::RevisionId revision, std::uint64_t revisionSeq);

    /// ②录入对象引用闭包条目（累加；闭包包含性在 build() 对锚定修订校验）。
    SnapshotBuilder& addObjectRef(ObjectRefEntry entry);

    /// ②录入分析配置引用（累加；configKindToken 在 build() 查重）。
    SnapshotBuilder& addConfiguration(ConfigEntry entry);

    /// ②设定已解析策略内容身份（④端口取得——覆盖式，一次组装一个策略集）。
    SnapshotBuilder& setPolicyRef(PolicyRef ref);

    /// ②设定 RuntimeNameMap 内容身份（⑥端口取得——覆盖式）。
    SnapshotBuilder& setNameMapRef(NameMapRef ref);

    /// ②录入工况条目（累加；caseId 唯一性在 build() 校验——§4.1.2 非法实例）。
    SnapshotBuilder& addCase(CaseEntry entry);

    /// ②录入外部资源固化状态（累加；resourceId 在 build() 查重）。
    SnapshotBuilder& addExternalResource(ExternalResourceState state);

    /// ②录入冻结采样计划（累加；regionObjectId 在 build() 查重）。
    SnapshotBuilder& addSamplingPlan(SamplingPlanRef plan);

    /// ②设定复现块（覆盖式，一次组装一个复现块）。
    SnapshotBuilder& setReproduction(ReproductionBlock block);

    /**
     * @brief ③冻结：即时验证（④a）＋计算身份＋返回不可变快照。
     *
     * @param closureSource [in] 修订闭包事实来源（适配 project ②端口——
     *                      §3.3）；全部 (oid,cv) 逐条对锚定修订校验，任一
     *                      不属于即拒绝（防混入——混入其他修订数据是 CON-01
     *                      闭包完整性的根本破坏）。调用方持有，本函数仅在
     *                      调用期使用，不保存引用。
     *
     * @return 冻结快照（snapshotId/requiredCaseSetId 均为计算值；集合类
     *         成员已规范化排序——同内容同序同身份）
     *
     * @throws EvidenceError（EvidenceErrorCode::SnapshotIncomplete）任一④a
     *         校验失败（含混入反例）；（EvidenceErrorCode::SnapshotIntegrity）
     *         不由本函数产生——载荷级校验在物化时（§4.1.5④b，codec 侧）。
     *
     * 复杂度：O(C log C)（排序）＋O(N)（闭包校验，N＝闭包条数——逐条查询
     * 事实来源；组装期一次性，非热点）。
     */
    AnalysisSnapshot build(const IRevisionClosureSource& closureSource) const;

private:
    // 组装中间态（②逐类录入的累加缓冲——保持插入序，冻结时规范化排序）。
    core::ProjectId m_project;     ///< 身份三元组·项目（build() 验非零）
    core::BranchId m_branch;       ///< 身份三元组·分支（build() 验非零）
    core::RevisionId m_revision;   ///< 身份三元组·锚定修订（build() 验非零）
    std::uint64_t m_revisionSeq = 0; ///< 修订序号（不参与身份——仅透传承载）
    std::vector<ObjectRefEntry> m_objectClosure;         ///< 闭包累加缓冲
    std::vector<ConfigEntry> m_configurations;           ///< 配置引用累加缓冲
    std::optional<PolicyRef> m_policyRef;                ///< 策略引用（未设定即 CON-06 拒绝）
    std::optional<NameMapRef> m_nameMapRef;              ///< 名称映射引用（同上）
    std::vector<CaseEntry> m_cases;                      ///< 工况累加缓冲
    std::vector<ExternalResourceState> m_externalResources; ///< 外部资源累加缓冲
    std::vector<SamplingPlanRef> m_samplingPlans;        ///< 采样计划累加缓冲
    std::optional<ReproductionBlock> m_reproduction;     ///< 复现块（未设定即拒绝）
};

// =====================================================================
// SnapshotCodec——规范编码（§5.2 canonical 规则＋§4.1.5⑤ worker 投递双形态）
// =====================================================================

/**
 * @brief 快照规范编码器（§5.2 编码规则表在快照面的落点）。
 *
 * 编码形态（字节布局见 src/Snapshot.cpp 实现头注释）：
 *   - magic "IRDSNAP1"（§5.2 原文）＋codec 版本号（版本化行——升版＝全体
 *     快照身份变化，破坏性变更走设计变更评审）＋形态字节（refs-only/
 *     materialized——双形态共用一套 refs-only 体，实现口径登记单元卡 v0.4）；
 *   - 确定性二进制：定宽整型大端、字段长度前缀、无填充；集合按冻结序
 *     （builder 已规范化——同内容同字节，NFR-COR-02）；可选值 presence
 *     字节（0x00/0x01——缺失与空值不等价，NFR-COR-03）；
 *   - revisionSeq 不参与编码（不参与内容身份语义判断——§4.1.2 原文）。
 *
 * 双形态与身份（§4.1.5⑤ 原文）：refs-only 与 materialized（含被评估切片的
 * 对象字节）的 snapshotId **相同**——身份取 refs-only（D-01：载荷字节已由
 * contentVersion 承诺，物化不改变身份）。
 *
 * 载荷级校验（§4.1.5④b，惰性）：materialized 形态在**物化时**重算对象字节
 * SHA-256 与 contentVersion 比对，不符抛 EvidenceError(SnapshotIntegrity)
 * ——检测传输/磁盘损坏（NFR-COR-03 不静默通过）。encodeMaterialized（物化
 * 入口）与 parseMaterialized（worker 接收入口）两端都执行（接收端复核是
 * 传输损坏的检测点——同一码面，登记单元卡 v0.4）。
 *
 * 线程安全：全部静态纯函数——可重入（§5.2"纯函数：同输入同字节；无隐藏
 * 状态；线程安全（可重入）"原文）。
 */
class SnapshotCodec {
public:
    /// 编码 magic（§5.2 原文 "IRDSNAP1"——8 字节 ASCII）。
    static constexpr std::string_view kMagic{"IRDSNAP1"};
    /// 本实现 codec 版本（写入编码第 9 字节；升版须走设计变更评审并留痕）。
    static constexpr std::uint8_t kCodecVersion = 1;

    /// materialized 形态的载荷集：objectId → 对象 canonical 字节
    /// （必须是快照 objectClosure 的子集——超集/错对象即拒绝；std::map 以
    /// ObjectId 字典序为序，编码确定性由冻结序＋map 序双保证）。
    using MaterializedPayloads = std::map<core::ObjectId, std::vector<std::uint8_t>>;

    /**
     * @brief refs-only 形态编码（身份形态——snapshotId 对它计算）。
     *
     * @param snapshot [in] 冻结快照（builder 产出；未冻结手改值编码出的
     *                 字节与其 snapshotId 不再对应——契约：只编码冻结快照）
     * @return 规范字节（同内容恒同字节——NFR-COR-02）
     *
     * @throws EvidenceError（SnapshotIncomplete）snapshot 内部字段与编码
     *         不变量冲突（如字符串含 NUL——理论上是 builder 已挡的再入
     *         防线，属调用方契约违约）
     */
    static std::vector<std::uint8_t> encodeRefsOnly(const AnalysisSnapshot& snapshot);

    /**
     * @brief refs-only 形态解析（恒计算 snapshotId/requiredCaseSetId——
     *        身份是编码的函数，不是编码的载荷：解析结果不可能携带与内容
     *        不符的申报身份）。
     *
     * @param encoding [in] encodeRefsOnly 的输出（或同规范的字节序列）
     * @return 解析出的快照（revisionSeq 恢复为默认 0——不在编码内，§4.1.2）
     *
     * @throws EvidenceError（SnapshotIncomplete）magic/版本/形态不符、截断、
     *         字符串含 NUL 等结构性非法（detail 带就地偏移）
     */
    static AnalysisSnapshot parseRefsOnly(const std::vector<std::uint8_t>& encoding);

    /**
     * @brief materialized 形态编码（§4.1.5⑤ worker 投递——含被评估切片的
     *        对象字节；物化入口，执行④b 载荷级校验）。
     *
     * @param snapshot  [in] 冻结快照（同 encodeRefsOnly 契约）
     * @param payloads  [in] 物化载荷（objectClosure 的子集；每条字节重算
     *                  SHA-256 必须等于其 contentVersion——不符即损坏/错配）
     *
     * @return 规范字节（refs-only 体＋载荷段；snapshotId 仍为 refs-only 摘要）
     *
     * @throws EvidenceError（SnapshotIntegrity）任一载荷字节摘要与
     *         contentVersion 不符（§4.1.5④b 原文码面）；（SnapshotIncomplete）
     *         payloads 含闭包外对象（物化超出"被评估切片"边界——调用方
     *         组装契约违约）
     */
    static std::vector<std::uint8_t>
    encodeMaterialized(const AnalysisSnapshot& snapshot, const MaterializedPayloads& payloads);

    /// materialized 解析产物：快照（身份已重算）＋物化载荷。
    struct MaterializedSnapshot {
        AnalysisSnapshot snapshot;   ///< 解析出的冻结快照（refs-only 部分重算身份）
        MaterializedPayloads payloads; ///< 载荷段（已逐条④b 复核）
    };

    /**
     * @brief materialized 形态解析（worker 接收入口——④b 载荷级校验在
     *        接收端复核：传输/磁盘损坏在此暴露为 SnapshotIntegrity）。
     *
     * @param encoding [in] encodeMaterialized 的输出（形态字节必须为
     *                 materialized——refs-only 字节请走 parseRefsOnly）
     * @return 快照＋载荷
     *
     * @throws EvidenceError（SnapshotIntegrity）任一载荷摘要与 contentVersion
     *         不符；（SnapshotIncomplete）结构性非法（magic/版本/形态/截断）
     *         或载荷含闭包外对象
     */
    static MaterializedSnapshot parseMaterialized(const std::vector<std::uint8_t>& encoding);
};

}  // namespace sdurws::ird::evidence

#endif  // SDURWS_IRD_EVIDENCE_SNAPSHOT_HPP
