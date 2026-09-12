/**
 * @file   CompilerImpl.cpp
 * @brief  十段编译链产品实现（CompilerImpl.hpp 契约）——S1～S5 共享实现、
 *         compile() 整体事务（S1～S10＋事务状态机＋取消轮询＋§5.4 资源
 *         摘要复查）、buildCanonicalModel() 分段入口。
 *
 * 设计依据：
 *   - units/runtime.md §5.2（十段逐步表——每段输入/输出/失败条件/诊断码
 *     归属，本文件逐步注释逐段对应）、§5.3（事务状态机与回滚规则）、
 *     §5.4（临时对象与资源复查——S4 记录摘要、S10 发布前对 Recorded 资源
 *     复读比对）、§5.5（可重入/取消/无内部超时——D-11）、§5.6（能力缺失
 *     降级与输入非法正交）、§4.2→§4.3（Description→Canonical 部件装配）、
 *     §6（T_world_base 唯一产生入口 resolveWorldBaseTransform＋S5 一致性
 *     校验——v0.7④"全量接线随编译器任务 RT-T11"）、§8.6（资源规则总表
 *     ——Recorded 固化警告不代阻断）
 *   - 需求 ARC-03/MDL-06/CON-01/TASK-01/NFR-REL-04/NFR-COR-03（逐段注释
 *     标注出处）
 *   - 任务契约 tasks/foundation/RT-T11.json（acceptance：事务状态机全转移、
 *     D-11 无内部超时、S1～S10 装配完整＋失败回滚、资源摘要复查——测试
 *     面 test/CompilerTest.cpp 逐条对应）
 *
 * 实现纪律：
 *   - 摘要只经 core::ContentDigester（CR-02——本单元一切 SHA-256 路径的
 *     唯一底层；本文件局部 wrapper 仅是调用收口，非第二实现）；
 *   - 失败在内部以 RuntimeError 异常传递（携带 §5.2 归属码＋段号定位），
 *     由 compile()/buildCanonicalModel() 的 catch 分别转译为诊断终态/err
 *     ——两入口共享同一 S1～S5 函数，失败语义单一源（NFR-COR-02）；
 *   - 取消＝Cancelled 异常（isCancellation 判别）→ cancelledOutcome 终态
 *     （非 error 诊断——UX-03）；编译器**无任何内部超时机制**（无时钟读取
 *     /无线程中断/无期限检查——D-11：取消只经外部令牌，2 s 协作窗由
 *     execution 通道保证，§5.5）；
 *   - 线程安全：全部状态在调用栈上的 CompileTransaction（§5.5 可重入）。
 */

#include "CompilerImpl.hpp"

#include "DescriptionValidator.hpp"  // S3 结构与单位校验器（RT-T03 交付面）
#include "SnapshotAssembler.hpp"     // S8+S10 装配尾段＋取消/失败终态工具（RT-T11 提升）

#include <sdurws/ird/core/Digest.hpp>                // ContentDigester（CR-02 单点）
#include <sdurws/ird/core/Provenance.hpp>            // ValueProvenance（SourcedValue 来源）
#include <sdurws/ird/runtime/Adapter.hpp>            // translateRobWorkError（§8.4 转译——
                                                     //  catch(std::exception) 分支）
#include <sdurws/ird/runtime/BaseWorldTransform.hpp>  // resolveWorldBaseTransform（§6.3 唯一
                                                      //  产生入口）＋S5 一致性校验（v0.7④ 接线）

#include <exception>
#include <new>
#include <utility>

namespace sdurws::ird::runtime {

namespace {

// =====================================================================
// 局部工具（编译链内部——无共享状态，可重入）。
// =====================================================================

/// SHA-256 摘要收口（CR-02：唯一摘要底层＝core::ContentDigester；本 wrapper
/// 仅把"对什么字节做摘要"的调用点收拢在一段）。
core::Digest256 sha256(const std::vector<std::uint8_t>& bytes)
{
    core::ContentDigester digester;
    digester.update(bytes.data(), bytes.size());
    return digester.finalize();
}

/// 事务阶段→十段表段号定位串（失败诊断 context 的段定位面——§5.2 归属）。
const char* stageName(CompileStage stage)
{
    switch (stage) {
    case CompileStage::Idle: return "S0 前置校验";
    case CompileStage::Anchored: return "S1";
    case CompileStage::Parsed: return "S2";
    case CompileStage::Validated: return "S3";
    case CompileStage::ResourcesReady: return "S4";
    case CompileStage::CanonicalBuilt: return "S5";
    case CompileStage::WorkCellCompiled: return "S6";
    case CompileStage::DynReady: return "S7";
    case CompileStage::NameMapBuilt: return "S8";
    case CompileStage::ConsistencyChecked: return "S9";
    case CompileStage::Published: return "S10";
    case CompileStage::RollingBack:
    case CompileStage::RolledBack: return "回滚";
    }
    return "未知段";
}

/// ObjectId 规范文本（诊断定位用——ARC-04 稳定身份的可读形态）。
std::string oidText(const core::ObjectId& id)
{
    return id.toCanonical();
}

/// S3/S4 警告级诊断的统一转译（码面取 registryCode 单点——PA-1 不私裁码值；
/// 比较型三要素的 warning 面仅量级抽样〔无 comparison〕，故不携带比较字段）。
core::DiagnosticRecord warningRecord(const std::string& code, const core::ObjectId& subject,
                                     std::string context, std::string cause,
                                     std::string action)
{
    return core::DiagnosticRecord::make(code, subject, std::nullopt, std::nullopt,
                                        std::move(context), std::move(cause),
                                        std::move(action));
}

/**
 * @brief 关节限位装配（Description SourcedValue 四态 → Canonical optional
 *         区间——§4.3.3 CanonicalJoint.bounds 行"必填/必无"复核）。
 *
 * 装配规则（与 S3 校验器、builder 不变量同口径——三道防线的本侧执行点）：
 *   - Continuous：lower/upper 必为 NotProvided（§4.2——工作范围另载
 *     workingRange，MDL-12）；任一 Provided→InputInvalid（"类型与限位
 *     矛盾"的输入非法，与 §4.3.3"Continuous 必无"一致）；
 *   - Revolute/Prismatic：双侧必填（MDL-06④——硬断言归 modeling，编译器
 *     复核一致性；缺失＝输入未完成→InputInvalid 而非能力缺失——限位是
 *     关节语义的必要部分，§5.6 正交表"输入非法"列）；
 *   - Fixed：未约束（无 q 语义——v0.8⑥"zeroOffset 不消费"同精神）；
 *     双侧 NotProvided→nullopt；双侧 Provided→照映射（builder 按有限＋
 *     有序复核——§4.3.3"提供则按有限＋有序复核"）；单侧 Provided＝区间
 *     不完整→InputInvalid。
 *
 * @param j [in] 关节描述（只读）
 * @return 规范限位（nullopt＝无界——Continuous/Fixed 合法形态）
 *
 * @throws RuntimeError 码＝InputInvalid：类型/限位组合矛盾或区间不完整
 *         （detail 定位 localName——建模侧修复后重编译可恢复）
 */
std::optional<JointBounds> assembleJointBounds(const JointDescription& j)
{
    const std::optional<double> lo = j.lower.tryValue();
    const std::optional<double> up = j.upper.tryValue();

    if (j.type == JointType::Continuous) {
        // Continuous 带限位＝类型与数据矛盾（§4.3.3"必无"——工作范围替代）。
        if (lo.has_value() || up.has_value()) {
            throw RuntimeError(RuntimeErrorCode::InputInvalid,
                               std::string{"S5: Continuous 关节 ["} + j.localName
                                   + "] 携带限位（Continuous 必无 bounds——工程工作范围应载于 "
                                     "workingRange，MDL-12）");
        }
        return std::nullopt;
    }
    if (lo.has_value() && up.has_value()) {
        // 双侧 Provided——区间值合法性（qmin<qmax/有限）由 builder 复核（S5
        // 构造不变量），此处只做形态装配，不重复实现判定（PA-1 唯一执行点）。
        return JointBounds{*lo, *up};
    }
    if (!lo.has_value() && !up.has_value()) {
        if (j.type == JointType::Fixed) {
            return std::nullopt;  // Fixed 无 q 语义——无界合法（§4.3.3"未约束"）
        }
        throw RuntimeError(RuntimeErrorCode::InputInvalid,
                           std::string{"S5: "} + (j.type == JointType::Prismatic ? "Prismatic"
                                                                                  : "Revolute")
                               + std::string{" 关节 ["} + j.localName
                               + "] 限位必填（MDL-06④——Revolute/Prismatic 必填，编译器复核）");
    }
    // 单侧 Provided——限位区间不完整（输入未完成，不能默认补齐——NFR-COR-03
    // 不静默：缺失面必须显式失败而不是伪造对称区间）。
    throw RuntimeError(RuntimeErrorCode::InputInvalid,
                       std::string{"S5: 关节 ["} + j.localName
                           + "] 限位仅有单侧 Provided（区间不完整——须双侧同时提供）");
}

}  // namespace

// =====================================================================
// CanonicalModelCompiler——版本面（§9.1 快照身份/缓存键/evidence 复现块的
// 编译器分量；单一权威定义点）。
// =====================================================================

std::uint32_t CanonicalModelCompiler::contractVersion() const noexcept
{
    // 契约版本＝1（初始契约；CompileRequest/CompileOutcome/十段语义的契约
    // 面变化＝升版＝全体缓存键/快照身份变化——升版走设计变更评审，§9.4）。
    return 1u;
}

std::string CanonicalModelCompiler::implementationVersion() const noexcept
{
    // 实现版本（非空约定——§9.1 合法列；实现内部演进标识，"契约或实现
    // 变化＝新键"（§9.4）中的实现侧分量：算法修正/装配规则调整时递增）。
    return std::string{"ird-runtime-canonical-compiler/1.0"};
}

// =====================================================================
// S1–S5 共享实现（compile() 与 buildCanonicalModel() 的单一语义源）。
// 每段的输入/输出/失败条件与 §5.2 十段逐步表逐行对应——段注释首列表行号。
// =====================================================================

CanonicalModel CanonicalModelCompiler::runStagesOneToFive(const CompileRequest& request,
                                                          CompileTransaction& tx,
                                                          bool pollCancellations)
{
    // ---- S0 前置校验（调用方输入完整性——§5.1 前置"注入源非空且一致"）----
    // InputInvalid（可恢复——补全请求后重编译），非契约违约轨（不抛异常
    // 终止会话；请求面不完整是调用方可自修的输入错误）。
    if (!request.project.isValid()) {
        throw RuntimeError(RuntimeErrorCode::InputInvalid,
                           std::string{"S0: CompileRequest.project 为全零（目标修订所属项目"
                                       "身份必填——§4.3.1 header.project 来源，RT-T11 增量字段）"});
    }
    if (request.closure == nullptr) {
        throw RuntimeError(RuntimeErrorCode::InputInvalid,
                           std::string{"S0: IRevisionClosureSource 未注入（S1 锚定修订必需）"});
    }
    if (request.objects == nullptr) {
        // 产品编译链的对象字节来源必填；worker 物化路径不经本入口——
        // §9.5 materialize（载荷已带 IRDCANO 模型字节，无对象读取面）。
        throw RuntimeError(RuntimeErrorCode::InputInvalid,
                           std::string{"S0: IObjectBytesSource 未注入（产品编译链 S2 需要对象"
                                       "字节来源；worker 物化路径走 IRuntimeSnapshotFactory::"
                                       "materialize——§9.5）"});
    }
    if (request.designReader == nullptr) {
        throw RuntimeError(RuntimeErrorCode::InputInvalid,
                           std::string{"S0: IRobotDesignReader 未注入（S2 解析必需——modeling "
                                       "注入，阶段 A 为测试替身）"});
    }

    // =================================================================
    // S1 锚定修订（§5.2 S1 行：RevisionId → RevisionSummary，一次读取，
    // 之后一切解析只对该视图）。失败条件：修订不存在/闭包外/上下文已关闭
    // → UnknownObject/ContextReleased（调用方契约违约——异常轨 fail-fast，
    // §3.4 总纲；可恢复＝换修订重试）。
    // =================================================================
    const std::optional<RevisionSummary> summary = request.closure->tryRevision(request.revision);
    if (!summary.has_value()) {
        // 归属表（Sources.hpp 文件头）：tryRevision nullopt→S1→UnknownObject
        // （修订不存在）；"上下文已关闭"的 ContextReleased 形态同经本通道
        // （来源实现无法从 nullopt 区分两成因，取主成因码——detail 说明；
        // 两码同属违约轨，处置语义一致）。
        throw RuntimeError(RuntimeErrorCode::UnknownObject,
                           std::string{"S1: 修订不存在或来源上下文已关闭（tryRevision 未命中"
                                       "）——换有效修订重试（§5.2 S1 可恢复列）"});
    }
    tx.m_summary = *summary;
    tx.advance(CompileStage::Anchored);

    // ---- 段边界取消轮询（S2 前——§5.5"每段边界……检查"；compile() 路径
    // 启用；分段入口不轮询——取消检查点归编排方，§10.0）。
    if (pollCancellations && cancelRequested(request.cancel)) {
        throw RuntimeError(RuntimeErrorCode::Cancelled,
                           std::string{"S2 段边界取消轮询命中——协作取消（非错误，UX-03）"});
    }

    // =================================================================
    // S2 规范模型解析（§5.2 S2 行：objectRefs 中 robot-design 对象字节＋
    // IRobotDesignReader → RobotDesignDescription）。
    // =================================================================
    const RevisionSummary& rev = *tx.m_summary;

    // ---- S2①：修订闭包全量成员校验（CM-0 防混入——RT-CONT-1 的产品执行点）。
    // header.objectRefs＝修订投影全量（§4.3.1"每 (oid,cv) ∈ revision 闭包"），
    // 故对每个引用逐条判定，不止 robot-design 对象。
    for (const ObjectRefEntry& entry : rev.objectRefs) {
        if (!request.closure->objectInRevision(request.revision, entry.objectId,
                                               entry.contentVersion)) {
            throw RuntimeError(RuntimeErrorCode::StructureInvalid,
                               std::string{"S2: 对象 ("} + oidText(entry.objectId)
                                   + ", cv) 不在目标修订闭包内——混入其他修订版本被拒绝"
                                     "（CM-0 防混入，RT-CONT-1；§5.2 S2 失败条件列）");
        }
    }

    // ---- S2②：robot-design 对象定位（kRobotDesignObjectType 单点路由；
    // 恰一个——零个＝无可编译对象；多个＝修订闭包歧义。计数口径为实现层
    // 登记：§5.2 S2 行"objectRefs 中 robot-design 对象"的确定性取位）。
    const ObjectRefEntry* robotEntry = nullptr;
    for (const ObjectRefEntry& entry : rev.objectRefs) {
        if (entry.objectTypeToken == kRobotDesignObjectType) {
            if (robotEntry != nullptr) {
                throw RuntimeError(RuntimeErrorCode::StructureInvalid,
                                   std::string{"S2: 修订闭包含多个 robot-design 对象（"} + oidText(robotEntry->objectId)
                                       + " 与 " + oidText(entry.objectId)
                                       + "）——编译输入歧义（StructureInvalid）");
            }
            robotEntry = &entry;
        }
    }
    if (robotEntry == nullptr) {
        throw RuntimeError(RuntimeErrorCode::InputInvalid,
                           std::string{"S2: 修订闭包无 robot-design 对象（objectTypeToken 未命中"
                                       "）——无可编译的机器人设计输入"});
    }

    // ---- S2③：对象字节读取（IObjectBytesSource；nullopt→InputInvalid 含
    // 对象定位——Sources.hpp 归属表）。
    const std::optional<std::vector<std::uint8_t>> objectBytes =
        request.objects->tryObjectBytes(robotEntry->objectId, robotEntry->contentVersion);
    if (!objectBytes.has_value()) {
        throw RuntimeError(RuntimeErrorCode::InputInvalid,
                           std::string{"S2: robot-design 对象字节不可得（oid="}
                               + oidText(robotEntry->objectId)
                               + "）——对象不存在或该内容版本不可用");
    }

    // ---- S2④：对象字节摘要复核（ObjectRefEntry.digest 是"完整性校验/
    // 防混入比对"值——§3.3；实得字节与修订登记不符＝字节被替换/损坏，
    // 按防混入同码 StructureInvalid 拒绝——与 CM-0 同一防线的字节面）。
    {
        const core::Digest256 actual = sha256(*objectBytes);
        if (!(actual == robotEntry->digest)) {
            throw RuntimeError(RuntimeErrorCode::StructureInvalid,
                               std::string{"S2: robot-design 对象字节摘要与修订登记不符（oid="}
                                   + oidText(robotEntry->objectId)
                                   + "）——内容完整性校验失败（CM-0 字节面）");
        }
    }

    // ---- S2⑤：reader 解析（对象字节→中性描述值；reader 失败→InputInvalid
    // 含定位——§5.2 S2 失败条件列）。格式版本传 1（实现层登记：ObjectRefEntry
    // 契约无格式版本字段，阶段 A 固定首版；版本协商随 modeling 阶段 B 接入
    // 任务细化——P-RT-5 交叉核对面）。
    const Expected<RobotDesignDescription, RuntimeError> parsed =
        request.designReader->read(*objectBytes, 1u);
    if (!parsed.ok()) {
        throw RuntimeError(RuntimeErrorCode::InputInvalid,
                           std::string{"S2: reader 解析失败（oid="}
                               + oidText(robotEntry->objectId) + "）：" + parsed.error().what());
    }
    tx.m_description = parsed.get();
    // builtFrom＝robot-design 对象字节的 SHA-256（§4.3.1"Description canonical
    // 字节摘要——reader 输出的身份"的落点：reader 消费的权威对象字节＝其
    // 输出 Description 的身份凭据；与修订登记摘要一致——S2④ 已核）。
    tx.m_builtFrom = sha256(*objectBytes);
    tx.advance(CompileStage::Parsed);

    // =================================================================
    // S3 结构与单位校验（§5.2 S3 行：链断裂/重复/数量失配→StructureInvalid；
    // 零轴/非有限/反射/qmin≥qmax/m≤0/非 SPD/耦合病态→InputInvalid 逐条
    // 定位——RT-T03 DescriptionValidator 交付面）。
    // =================================================================
    if (pollCancellations && cancelRequested(request.cancel)) {
        throw RuntimeError(RuntimeErrorCode::Cancelled,
                           std::string{"S3 段边界取消轮询命中——协作取消（非错误，UX-03）"});
    }
    const ValidationReport report = validateRobotDesignDescription(*tx.m_description);
    for (const ValidationIssue& issue : report.issues) {
        if (issue.severity == ValidationSeverity::Error) {
            continue;  // 硬失败统一收集后抛出（见下——全量列出，不短路）
        }
        // 警告级（§4.4 量级抽样——给警告不阻断）：随模型诊断块发布
        // （§4.3.5"诊断块仅警告级"），code 取归属码的注册码单点。
        tx.m_warnings.push_back(warningRecord(
            std::string{registryCode(issue.code)}, robotEntry->objectId,
            std::string{"S3 结构与单位校验警告（不阻断——§4.4 量级抽样纪律）"},
            issue.detail,
            std::string{"复核建模输入的单位制（SI 真值）后重编译"}));
    }
    if (!report.ok()) {
        // 硬失败：全部 Error 级问题拼入 detail（ERR-01"全量列出"精神——
        // 修一处重编译不至于逐个撞出下一处）；异常码取首条问题归属码
        // （§5.2 S3 行的码面，单异常单码——全量文本在 detail）。
        std::string joined;
        for (const ValidationIssue& issue : report.issues) {
            if (issue.severity != ValidationSeverity::Error) {
                continue;
            }
            if (!joined.empty()) {
                joined += "；";
            }
            joined += issue.detail;
        }
        for (const ValidationIssue& issue : report.issues) {
            if (issue.severity == ValidationSeverity::Error) {
                throw RuntimeError(issue.code, "S3: " + joined);
            }
        }
    }
    tx.advance(CompileStage::Validated);

    // =================================================================
    // S4 资源读取与完整性校验（§5.2 S4 行：resourceManifest＋
    // IRuntimeResourceProvider → 已验证资源字节集；缺失/摘要不符/预算超限
    // 三码分立＋resourceId 定位——RT-RES-1；§8.6 规则总表）。
    // =================================================================
    for (const ResourceRef& ref : tx.m_description->resourceRefs) {
        // ---- 资源逐项循环取消检查（§5.5"每段边界与资源逐项循环检查"——
        // 取消密度契约的资源面；compile() 路径启用，分段入口不轮询）。
        if (pollCancellations && cancelRequested(request.cancel)) {
            throw RuntimeError(RuntimeErrorCode::Cancelled,
                               std::string{"S4 资源逐项循环取消轮询命中——协作取消（非错误）"});
        }
        if (request.resources == nullptr) {
            // 清单声明了资源却无 provider——按缺失归属（定位 resourceId）。
            throw RuntimeError(RuntimeErrorCode::ResourceMissing,
                               std::string{"S4: 资源缺失（resourceId="}
                                   + oidText(ref.resourceId)
                                   + "）——未注入 IRuntimeResourceProvider 而清单非空");
        }
        const Expected<ResourceBytes, ResourceReadError> read =
            request.resources->tryResourceBytes(ref.resourceId);
        if (!read.ok()) {
            // 三码透传（io 原始检测→runtime 就地定位转译——§8.6"四类诊断
            // 严格区分"，RT-RES-1 断言面；detail 双侧携带）。
            const ResourceReadError& err = read.error();
            throw RuntimeError(err.code,
                               std::string{"S4: 资源读取失败（resourceId="}
                                   + oidText(ref.resourceId) + "，码="
                                   + std::string{token(err.code)}
                                   + "）：" + err.detail);
        }
        // 摘要复核（读取内容 vs 清单申报——§5.2 S4"摘要一致"；不符即
        // ResourceChanged——"读取时刻与登记时刻之间被替换"）。
        if (!(read.get().digest == ref.contentDigest)) {
            throw RuntimeError(RuntimeErrorCode::ResourceChanged,
                               std::string{"S4: 资源内容摘要与清单申报不符（resourceId="}
                                   + oidText(ref.resourceId)
                                   + "）——内容在读取前已被替换（ResourceChanged）");
        }
        // 记录已消费资源（S10 发布前复查的输入——§5.4；记录仅编译期内存活，
        // 绝不跨编译复用——防陈旧）。Solidified 的免复查标记随 ref.state 携带。
        tx.m_consumedResources.push_back(ref);
        // Recorded 资源的固化提醒（§8.6 规则总表：runtime 产出警告级诊断
        // "正式评估前须固化"，不代为阻断——阻断判定归 evidence validateForMode；
        // 建议码 RT-RESOURCE-RECORDED 待 diagnostics 收编〔PA-1 登记面〕）。
        if (ref.state == ResourceState::Recorded) {
            tx.m_warnings.push_back(warningRecord(
                std::string{"RT-RESOURCE-RECORDED"}, ref.resourceId,
                std::string{"S4 资源状态提醒（Recorded——警告不阻断，CON-03）"},
                std::string{"资源已登记引用、内容仍在外部源（未固化）——正式评估前须固化"},
                std::string{"在项目内固化该资源（PM-09/CON-03）后进入正式评估"}));
        }
    }
    tx.advance(CompileStage::ResourcesReady);

    // =================================================================
    // S5 CanonicalModel 构造（§5.2 S5 行：Description＋资源引用＋身份块 →
    // CanonicalModel〔含 contentIdentity/capabilities/警告级 diagnostics〕；
    // 构造不变量违反→InputInvalid；能力缺失在此降级为 capability 而非失败
    // ——§5.6，由 builder 的能力派生承载，RT-T04）。
    // =================================================================
    if (pollCancellations && cancelRequested(request.cancel)) {
        throw RuntimeError(RuntimeErrorCode::Cancelled,
                           std::string{"S5 段边界取消轮询命中——协作取消（非错误，UX-03）"});
    }
    const RobotDesignDescription& d = *tx.m_description;

    // ---- 身份与来源块（§4.3.1）----
    CanonicalModelHeader header;
    header.project = request.project;            // RT-T11 增量字段的唯一消费点
    header.branch = rev.branch;                  // 来源定位（§4.3.1"来自注入的修订视图"）
    header.revision = rev.id;
    header.revisionSeq = rev.seq;                // 仅展示排序——不入内容身份（§4.3.1）
    header.objectRefs = rev.objectRefs;          // S2① 已全量过闭包校验（CM-0）
    header.descriptionContractVersion = d.descriptionContractVersion; // ≥1 由 builder 复核
    header.compilerContractVersion = contractVersion();
    header.builtFrom = tx.m_builtFrom;           // reader 输出的身份（§4.3.1）

    // ---- 世界与基座块（§4.3.2）——T_world_base 唯一产生入口（§6.3）＋
    // 全量一致性校验（v0.7④：S5 接线 checkWorldBaseTransform＋
    // checkPresetConsistency——builder 只保留"Custom 而 R≈I"拒绝面，
    // 全量规则经本调用执行，PA-1 单点）。
    WorldPlacement world;
    const Expected<rw::math::Transform3D<double>, RuntimeError> tWorldBase =
        resolveWorldBaseTransform(d.base);
    if (!tWorldBase.ok()) {
        // 正解失败（非有限 basePosition/Custom 缺 EAA）→InputInvalid 定位
        // base 字段（§5.2 S6 行"T_world_base 非法旋转→InputInvalid"的
        // 产生侧归属——在 S5 构造期即拦截，不让带病值进入模型）。
        throw RuntimeError(tWorldBase.error().code(),
                           std::string{"S5: 基座布置解析失败——"} + tWorldBase.error().what());
    }
    world.T_world_base = tWorldBase.get();
    // installPreset 是编辑表示/来源记录（不入身份——§4.3.2/§4.5 排除清单）；
    // 来源标记＝用户输入（编辑器预设选择——core §4.3 ProvenanceKind 语义）。
    world.installPreset = core::SourcedValue<InstallationPresetToken>::provided(
        d.base.preset, core::ValueProvenance::make(core::ProvenanceKind::UserProvided));
    // gravityWorld 不显式设置——canonical 默认 (0,0,−9.81) m/s²（Description
    // 无重力输入面；默认值即规范值，与 RobWork 构造默认 −9.82 可区分——
    // v0.9⑦ 同口径）。
    if (const std::optional<RuntimeError> bad = checkWorldBaseTransform(world.T_world_base)) {
        throw RuntimeError(bad->code(), std::string{"S5: T_world_base 矩阵层校验失败——"}
                                              + bad->what());
    }
    if (const std::optional<RuntimeError> bad =
            checkPresetConsistency(d.base.preset, world.T_world_base)) {
        throw RuntimeError(bad->code(), std::string{"S5: 预设与旋转矩阵不一致——"} + bad->what());
    }

    // ---- 机器人链块（§4.3.3）——Description 逐字段 → Canonical 部件。
    RobotChain chain;
    chain.robotObjectId = robotEntry->objectId;   // S2② 定位的 robot-design 对象
    chain.robotLocalName = d.robotLocalName;
    // deviceName＝名称端口生成物（§4.3.3"＝§7 生成规则输出"）——顶层 Device
    // 条目的生成规则输出即机器人局部名的合法化形态（NameMap §7.2：Device
    // 行"设备名本身"）；此处承载原值，消歧后事实名以映射/S6 产物为准
    // （S8 交叉校验兜底一致性——实现层登记，units/runtime.md §15.4 v0.12）。
    chain.deviceName = d.robotLocalName;

    chain.joints.reserve(d.joints.size());
    for (std::size_t i = 0; i < d.joints.size(); ++i) {
        const JointDescription& j = d.joints.at(i);
        CanonicalJoint cj;
        cj.objectId = j.objectId;         // RT-T11 增量字段（∈objectRefs——builder 复核）
        cj.localName = j.localName;
        cj.type = j.type;
        cj.axis = j.axis;                 // 单位化规格化在 builder（§4.3.3"编译器规格化"）
        cj.origin = j.origin;             // T_parent_joint（正交/反射复核在 builder）
        cj.zeroOffset = 0.0;              // §4.2 无零位偏置承载——显式表示已折叠进
                                          //  限位/原点（q_authoritative 即 q_rw）；
                                          //  units/runtime.md §15.4 v0.12 登记
        cj.bounds = assembleJointBounds(j);
        cj.workingRange = j.workingRange; // 仅 Continuous（§4.3.3——builder/S3 复核）
        cj.maxVelocity = j.maxVelocity;   // NotProvided＝能力缺失降级（§5.6）
        cj.maxAcceleration = j.maxAcceleration;
        if (!d.friction.empty()) {
            // 逐关节摩擦（数量失配已由 S3 拦截——§5.2 S3"逐关节向量失配"）。
            const JointFrictionDescription& f = d.friction.at(i);
            cj.friction.viscous = f.viscous;
            cj.friction.coulomb = f.coulomb;
            cj.friction.bias = f.bias;
        }
        chain.joints.push_back(std::move(cj));
    }

    chain.links.reserve(d.links.size());
    for (const LinkDescription& l : d.links) {
        CanonicalLink cl;
        cl.objectId = l.objectId;
        cl.localName = l.localName;
        // 几何引用固化（GeometryRef→ResourceRef——资源以内容摘要入身份，
        // §4.3.3"引用须命中 resourceManifest"由 builder 复核）。
        if (l.visual.has_value()) {
            cl.visual = l.visual->resource;
        }
        if (l.collision.has_value()) {
            cl.collision = l.collision->resource;
        }
        cl.mass = l.mass;               // NotProvided＝能力缺失（DYN-06 降级面）
        cl.centerOfMass = l.centerOfMass;
        cl.inertia = l.inertia;
        // l.material 不映射——显示/追溯属性不入规范模型（§4.3.6"显示性字段
        // 不入身份"；CanonicalLink 无对应字段）。
        chain.links.push_back(std::move(cl));
    }

    // ---- 工具/场景/传动块（§4.3.4）----
    std::vector<CanonicalTool> tools;
    tools.reserve(d.tools.size());
    for (const ToolDescription& t : d.tools) {
        CanonicalTool ct;
        ct.objectId = t.objectId;
        ct.localName = t.localName;
        if (t.geometry.has_value()) {
            ct.geometry = t.geometry->resource;
        }
        ct.mass = t.mass;
        ct.centerOfMass = t.centerOfMass;
        ct.inertia = t.inertia;
        ct.tcpOffset = t.tcpOffset;     // T_flange_tcp（KIN-14 权威来源）
        tools.push_back(std::move(ct));
    }
    // 默认 TCP＝首项（MDL-13/04：tools 首项为默认 TCP——确定性下标 0；
    // tools 为空时 nullopt（builder 约束：与 tools 空判一致）。
    const std::optional<std::uint32_t> defaultTcpIndex =
        tools.empty() ? std::optional<std::uint32_t>{} : std::optional<std::uint32_t>{0u};

    std::vector<CanonicalSceneObject> scene;
    scene.reserve(d.scene.size());
    for (const SceneObjectDescription& s : d.scene) {
        CanonicalSceneObject cs;
        cs.objectId = s.objectId;
        cs.localName = s.localName;
        cs.worldPose = s.worldPose;     // 世界系固连（禁止预乘安装旋转——§6.4 禁止项 3）
        cs.geometry = s.geometry.resource;
        scene.push_back(std::move(cs));
    }

    CanonicalDrivetrain drivetrain;
    drivetrain.ratioPerJoint = d.drivetrain.ratioPerJoint;  // 逐关节四态直传
    drivetrain.coupling = d.drivetrain.coupling;            // 同型常矩阵（MDL-21，R2）

    // ---- builder 全量构造（值级不变量＋确定性规范化＋能力派生＋索引＋
    // 内容身份——RT-T04 交付面；违约 RuntimeError 原码透传，detail 补 S5
    // 段号定位〔契约违约码包装后仍为违约码——compile() 的违约轨判别不受
    // 前缀影响〕。经立即调用 lambda 承载 try/catch——CanonicalModel 私有
    // 默认构造，不能先默认构造成再赋值）。
    CanonicalModel model = [&]() {
        try {
            return CanonicalModelBuilder()
                .setHeader(std::move(header))
                .setWorld(world)
                .setChain(std::move(chain))
                .setTools(std::move(tools))
                .setDefaultTcpIndex(defaultTcpIndex)
                .setScene(std::move(scene))
                .setDrivetrain(drivetrain)
                .setResourceManifest(d.resourceRefs)
                .setDiagnostics(tx.m_warnings)
                .build();
        } catch (const RuntimeError& e) {
            if (isContractViolation(e.code())) {
                throw;  // 违约轨原样上抛（detail 前缀不影响码判别）
            }
            throw RuntimeError(e.code(), std::string{"S5: 模型构造不变量违约——"} + e.what());
        }
    }();

    tx.m_model = model;
    tx.advance(CompileStage::CanonicalBuilt);
    return model;
}

// =====================================================================
// §5.4 资源摘要复查（S10 发布前——RT-T11 交付核心之一）。
// =====================================================================

namespace {

/**
 * @brief 对 S4 已消费资源执行发布前摘要复查（§5.4 原文："S10 发布前对已被
 *         消费的资源复查摘要（IRuntimeResourceProvider 复读）——任一不符→
 *         ResourceChanged→整体失败"）。
 *
 * 复查规则：
 *   - 仅 Recorded 资源参与（Solidified 免复查——项目内不可变副本的存储
 *     不可变保证，§5.4/§8.6 规则总表）；
 *   - 复读摘要与 S4 记录比对，任一不符→ResourceChanged 整体失败（防止
 *     "读取时刻与发布时刻之间被替换"的窗口——RT-RES-2 的产品执行点）；
 *   - 复读失败（资源消失/不可得）同归 ResourceChanged：S4 时点存在、
 *     S10 前不可得本质是"内容已变化"（实现层登记——§5.4 只定义变化码，
 *     缺失码的检测归属 S4 时点）；
 *   - 逐项循环内轮询取消令牌（§5.5 取消密度契约的资源面延续）。
 *
 * @param request [in] 编译请求（provider/取消令牌来源）
 * @param tx      [in] 事务（S4 记录的已消费资源清单）
 *
 * @throws RuntimeError 码＝Cancelled（取消轮询命中）／ResourceChanged
 *         （任一 Recorded 资源摘要不符或复读失败——resourceId 定位）
 */
void recheckConsumedResources(const CompileRequest& request, const CompileTransaction& tx)
{
    for (const ResourceRef& ref : tx.m_consumedResources) {
        // 逐项取消轮询（§5.5——与 S4 读取循环同密度）。
        if (cancelRequested(request.cancel)) {
            throw RuntimeError(RuntimeErrorCode::Cancelled,
                               std::string{"S10 前资源复查循环取消轮询命中——协作取消（非错误）"});
        }
        if (ref.state != ResourceState::Recorded) {
            continue;  // Solidified 免复查（§5.4——存储不可变保证）
        }
        const Expected<ResourceBytes, ResourceReadError> read =
            request.resources->tryResourceBytes(ref.resourceId);
        if (!read.ok() || !(read.get().digest == ref.contentDigest)) {
            throw RuntimeError(RuntimeErrorCode::ResourceChanged,
                               std::string{"S10 发布前复查：资源内容在编译期间被替换或不可得"
                                           "（resourceId="}
                                   + oidText(ref.resourceId)
                                   + "）——读取时刻与发布时刻之间存在替换窗口（§5.4，RT-RES-2）");
        }
    }
}

}  // namespace

// =====================================================================
// compile()——整体事务入口（S1–S10；§10.0"MDL-06 原子性的唯一产品入口"）。
// =====================================================================

CompileOutcome CanonicalModelCompiler::compile(const CompileRequest& request)
{
    // 事务对象＝本函数的全部瞬态产物持有者（§5.3）；返回即析构——失败/
    // 取消路径的产物经 rollback() 显式释放＋作用域析构兜底。
    CompileTransaction tx;
    try {
        // ---- 段边界取消轮询（S1 前——§3.3 令牌可空＝不可取消）----
        if (cancelRequested(request.cancel)) {
            return cancelledOutcome();
        }

        // ---- S1–S5：与分段入口共享的实现（内部轮询取消——§5.5）----
        runStagesOneToFive(request, tx, /*pollCancellations=*/true);
        const CanonicalModel& model = *tx.m_model;

        // ---- 段边界取消轮询（S6 前）----
        if (cancelRequested(request.cancel)) {
            return cancelledOutcome();
        }
        // ---- S6：WorkCell 编译（BaseMount 唯一写入＋S9 防御自检内置——RT-T07；
        // 异常经 §8.4 转译以 RuntimeError 抛出→catch 统一转译）。
        tx.advance(CompileStage::WorkCellCompiled);
        tx.m_wcOutcome = compileWorkCell(model);

        // ---- 段边界取消轮询（S7 前）----
        if (cancelRequested(request.cancel)) {
            return cancelledOutcome();
        }
        // ---- S7：DynamicWorkCell 编译（能力门控；requestDynamicWorkCell=
        // false 时跳过——§9.4 capabilityLevel"是否要求 DWC"；门控降级
        // SkippedNoPhysics 不是失败——§5.6 正交表）。
        tx.advance(CompileStage::DynReady);
        if (request.options.requestDynamicWorkCell) {
            tx.m_dwcOutcome = compileDynamicWorkCell(model, *tx.m_wcOutcome);
        }

        // ---- 段边界取消轮询（S8 前）----
        if (cancelRequested(request.cancel)) {
            return cancelledOutcome();
        }
        // ---- S8：名称映射建立（§7 纯函数；消歧耗尽→NameConflict——S8 硬
        // 失败通道；与 S6 写入名的交叉校验在装配尾段执行）。
        tx.advance(CompileStage::NameMapBuilt);
        tx.m_nameMap = buildRuntimeNameMap(model);

        // ---- S9：基座—世界一致性检查——通过标记。检查本体随 S6 执行
        //（compileWorkCell 内置 BaseMount 读回值 checkBaseMountConsistency
        // 复核，不一致即 BaseWorldInconsistent fail-fast——v0.8⑦/RT-T07
        // 交付面；本状态标记其已通过，编译器不重复实现第二套判定——PA-1）。
        tx.advance(CompileStage::ConsistencyChecked);

        // ---- §5.4 资源摘要复查（S10 发布前——Recorded 逐项复读比对；
        // 不符→ResourceChanged 整体失败。这是"读取时刻与发布时刻之间"
        // 替换窗口的唯一封闭点，RT-RES-2 的产品执行点）。
        recheckConsumedResources(request, tx);

        // ---- 段边界取消轮询（S10 装配前）----
        if (cancelRequested(request.cancel)) {
            return cancelledOutcome();
        }

        // ---- S10：快照装配＋原子发布（与工厂 create/materialize 共用尾段
        // ——S8 交叉校验＋身份装配＋§9.6 发布门禁的单一实现；shared_ptr<const>
        // 交接＝原子发布语义，§9.2。此前一切产物对外不可见——§5.2 S10。
        // extraWarnings＝S8 名称生成期警告的转译——v0.5⑥ 落位，RT-T11）。
        CompileOutcome out = SnapshotAssembler::assemble(
            model, *tx.m_nameMap, *tx.m_wcOutcome,
            tx.m_dwcOutcome.has_value() ? &*tx.m_dwcOutcome : nullptr,
            request.options, contractVersion(), implementationVersion(),
            SnapshotOrigin::Command, translateNameMapNotices(*tx.m_nameMap));
        tx.advance(CompileStage::Published);
        return out;
    } catch (const RuntimeError& e) {
        // 调用方契约违约保持 fail-fast 轨（§3.4——UnknownObject/ContextReleased
        // 不转为诊断静默；与工厂 create 同轨）。
        if (isContractViolation(e.code())) {
            throw;
        }
        // 先取失败段定位（rollback 会改写状态机——§5.3 RolledBack 终态），
        // 再回滚（幂等；瞬态产物显式析构），最后按取消/失败分派终态。
        const char* failedStage = stageName(tx.stage());
        tx.rollback();
        if (isCancellation(e.code())) {
            // 取消是正常控制流（UX-03/D-11）——Cancelled 终态＋非 error 诊断，
            // 无半成品（rollback 已清空瞬态产物），可直接重试（§5.5）。
            return cancelledOutcome();
        }
        // 编译硬失败→Failed＋稳定码诊断（不短路；瞬态 RobWork 对象已由
        // rollback/RAII 析构——MDL-06"失败不发布半成品"）。
        return failedOutcome(toDiagnostic(e, failedStage));
    } catch (const std::bad_alloc&) {
        // 内存不足→ResourceBudget＋对象清理（§5.5——RT-CPX-4 分支；RAII 兜底）。
        const char* failedStage = stageName(tx.stage());
        tx.rollback();
        return failedOutcome(core::DiagnosticRecord::make(
            std::string{registryCode(RuntimeErrorCode::ResourceBudget)},
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::string{"runtime 编译链失败（段: "} + failedStage
                + "；内存不足——§5.5 bad_alloc 转译）",
            std::string{token(RuntimeErrorCode::ResourceBudget)},
            std::string{"缩减模型规模/释放内存后重编译（瞬态对象已由 RAII 清理）"}));
    } catch (const std::exception& e) {
        // 其余异常（RobWork 基线异常等）→事件码转译＋Failed（§8.4 转译表）。
        const char* failedStage = stageName(tx.stage());
        tx.rollback();
        return failedOutcome(translateRobWorkError(e, failedStage, std::nullopt));
    }
}

// =====================================================================
// buildCanonicalModel()——分段入口（S1–S5；§10.0"编译链内部使用/测试替身
// 注入点"——工厂 create 经它取得模型后自行编排 S6–S10）。
// =====================================================================

Expected<CanonicalModel, RuntimeError>
CanonicalModelCompiler::buildCanonicalModel(const CompileRequest& request)
{
    // 分段入口同样以事务承载瞬态产物（§5.3——"独立于 compile 的整体事务"：
    // 无 S6–S10/S9/资源复查面；取消轮询归编排方在段边界执行——§10.0 原文，
    // 故 pollCancellations=false，内部不抢编排方的取消检查点）。
    CompileTransaction tx;
    try {
        return Expected<CanonicalModel, RuntimeError>::ok(
            runStagesOneToFive(request, tx, /*pollCancellations=*/false));
    } catch (const RuntimeError& e) {
        // 契约违约保持 fail-fast 轨（§3.4——消费方〔工厂〕按同轨重抛；
        // 其余归属码走 err 查询轨——§10.0 属性表"返回不可变模型或诊断"）。
        if (isContractViolation(e.code())) {
            throw;
        }
        tx.rollback();
        return Expected<CanonicalModel, RuntimeError>::err(e);
    } catch (const std::bad_alloc&) {
        // 内存不足→ResourceBudget（§5.5 同款转译——err 轨形态）。
        tx.rollback();
        return Expected<CanonicalModel, RuntimeError>::err(RuntimeError(
            RuntimeErrorCode::ResourceBudget,
            std::string{"S1-S5: 内存不足（§5.5 bad_alloc 转译）——缩减模型规模后重试"}));
    } catch (const std::exception& e) {
        // 其余 std 异常→RobWorkError 事件码（§8.4；err 轨不抛出——查询
        // 路径的两态契约）。
        tx.rollback();
        return Expected<CanonicalModel, RuntimeError>::err(
            RuntimeError(RuntimeErrorCode::RobWorkError,
                         std::string{"S1-S5: "} + e.what()));
    }
}

}  // namespace sdurws::ird::runtime
