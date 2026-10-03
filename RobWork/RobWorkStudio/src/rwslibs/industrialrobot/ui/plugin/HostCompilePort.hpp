/**
 * @file   HostCompilePort.hpp
 * @brief  宿主编译端口（HostModelCompilePort）——project::IModelCompilePort
 *         的 runtime 十段链产品适配（UI-T46 呈现装配——编译链真数据源）。
 *
 * 设计依据：
 *   - 任务契约 tasks/foundation/UI-T46.json（宿主呈现装配批次——发布桥
 *     接线＋编译链装配＋名称映射真值）；
 *   - units/project.md §5.3.6（IModelCompilePort——"runtime 实现、L5 注入；
 *     project 侧仅编排"，P-PR-7 单侧冻结）；§6.6（S5 编译输入＝计划闭包
 *     ——baseSnapshot 引用集＋plannedWrites 合成视图，"编译先于 S6：失败
 *     时无任何已发布内容需要回退"）；
 *   - units/runtime.md §3.3（注入边界——适配器归 L5 装配期提供）、§5.2
 *     （十段链各注入点）、§10.0（CompileRequest/CompileOutcome/快照工厂
 *     契约面）；
 *   - units/modeling.md §9.1/§9.2/§9.4.5（CanonicalBridge——
 *     ObjectClosureView 闭包域字节源＋RobotDesignReader 产品实现，L5 注入
 *     runtime S2；"闭包域字节源在 L5 组装 CompileRequest 时与
 *     CompileRequest.objects 同源绑定"）；
 *   - findings F-461（owner 列 modeling 半区：registerModelingCommandHandlers
 *     需 HandlerServices 服务集装配——本文件同时承载该装配面，消账登记
 *     随 UI-T46 收口）；
 *   - B1-SPEC §4.3 D10（呈现视图与快照隔离但复用同一编译链——本适配器
 *     缓存的快照即编译链原始产物，呈现 source 直接消费，**结构性杜绝**
 *     呈现侧第二构造路径）。
 *
 * 背景说明（第一读者须知——为什么需要本文件）：
 *   project 命令服务的 S5 段经 IModelCompilePort 调双编译（MDL-06 原子性
 *   ——编译失败不提交），而端口契约（P-PR-7）不回传快照（CompileResult
 *   只有 ok＋诊断）。宿主呈现装配需要"已发布快照"构造呈现视图（RT-T14
 *   工厂唯一入口），若呈现侧另行发起编译即"第二构造路径"违 D10。本适
 *   配器的解法：编译端口实现内部**顺带保留**十段链发布的快照句柄（同一
 *   产物——传递而非重建），呈现 source 经 lastPublishedSnapshot() 取用。
 *   端口语义对 project 完全不变（CompileResult 形态零改），快照缓存是
 *   L5 装配层自有状态。
 *
 * 合成闭包口径（S5 编译输入的关键时序）：S5 发生在修订提交（S6）之前，
 * 计划写入的对象只存在于 plan.objectWrites（暂存区未创建）——本适配器
 * 的闭包源/字节源对"计划写入对象"以**载荷字节 SHA-256** 充当内容版本与
 * 完整性摘要（内容寻址语义同源；编译调用期内自洽——同一适配器实例的
 * tryRevision 引用集与 tryObject 键空间一致，编译器 CM-0 校验闭合）。
 * 基线对象直接转发存储引用（store objectRefs 的 (oid,cv) 与 tryObject
 * 逐项一致——零转译）。快照 header 携带的合成 cv 仅存在于本次编译产物
 * 身份面，与提交后存储自算的 cv 无耦合（呈现链消费快照对象，不经 cv）。
 *
 * 线程模型：全部入口仅 UI 线程（编译发生在命令执行槽内——ARCH §4.2
 * "编译类命令不与下一命令并发"；§3.4 M-1 纪律的命令路径形态）。
 * 生命周期：由 L5 装配层（IrdWorkbenchHostPlugin）按"每次项目打开成功"
 * 构造、随会话关闭销毁（Deps.query 为打开上下文的端口——存活期由
 * ProjectStore 保证）。
 */

#ifndef IRD_UI_PLUGIN_HOSTCOMPILEPORT_HPP
#define IRD_UI_PLUGIN_HOSTCOMPILEPORT_HPP

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <sdurws/ird/core/Identity.hpp>             // ProjectId/RevisionId/ObjectId/ContentVersion
#include <sdurws/ird/modeling/CanonicalBridge.hpp>  // modeling::ObjectClosureView（闭包域字节源——R-1 经公共头）
#include <sdurws/ird/modeling/DhConvert.hpp>        // modeling::CompileProbe（等价验证分段探针抽象）/RobotDesignDescription
#include <sdurws/ird/policy/Contexts.hpp>           // policy::IPolicyNameContext（行程评估名称上下文抽象）
#include <sdurws/ird/project/CommandService.hpp>    // project::IModelCompilePort/CompileRequest/CompileResult
#include <sdurws/ird/project/QueryPort.hpp>         // project::IProjectQueryPort（只读取数面）
#include <sdurws/ird/runtime/Compiler.hpp>          // runtime::CompileRequest/CompileOutcome/ICanonicalModelCompiler
#include <sdurws/ird/runtime/Description.hpp>       // runtime::RobotDesignDescription/IRobotDesignReader（探针 reader 基类）
#include <sdurws/ird/runtime/Snapshot.hpp>          // runtime::RuntimeSnapshot/RuntimeSnapshotFactory
#include <sdurws/ird/runtime/Sources.hpp>           // runtime::IObjectBytesSource/IRevisionClosureSource/ObjectRefEntry

namespace sdurws {
namespace ird {
namespace ui {

class HostRuntimeNameMapPort;  // 前置声明（真值端口——HostPresentationAdapters.hpp 完整类型；本头成员以指针持有）

// =====================================================================
// HostProjectObjectBytesSource——对象字节只读源（runtime IObjectBytesSource
// ← project ②端口 tryObject＋计划写入覆盖层）
// =====================================================================

/**
 * @brief 编译期对象字节源（基线对象→存储现读；计划写入对象→载荷直供）。
 *
 * 定址语义：(oid, cv) 精确匹配——基线对象的 cv 来自存储 objectRefs（转发
 * 一致）；计划写入对象的 cv 为本批适配器合成的载荷摘要（见文件头"合成
 * 闭包口径"）。确定性：同 (oid,cv) 重复取回相同字节（存储首读摘要校验
 * ＋载荷不可变——§5.5 注入源确定性约束）。
 */
class HostProjectObjectBytesSource final : public runtime::IObjectBytesSource {
public:
    /**
     * @brief 构造（编译调用期内全部指针必须有效——CompileRequest 注入
     *        指针同款约定）。
     *
     * @param query        [in] 存储查询端口（非 owning——打开上下文保证）
     * @param plannedBytes [in] 计划写入字节表（键＝(oid, 合成 cv)——由
     *                     编排侧装配；非 owning，编译调用期有效）
     */
    HostProjectObjectBytesSource(
        const project::IProjectQueryPort& query,
        const std::vector<std::pair<core::ObjectId, core::ContentVersion>>& plannedKeys,
        const std::vector<std::vector<std::uint8_t>>& plannedBytes);

    std::optional<std::vector<std::uint8_t>>
        tryObjectBytes(core::ObjectId objectId, core::ContentVersion version) const override;

private:
    const project::IProjectQueryPort* m_query;              ///< 存储取数面（非 owning）
    const std::vector<std::pair<core::ObjectId, core::ContentVersion>>*
        m_plannedKeys;                                      ///< 计划写入键表（并行于字节表）
    const std::vector<std::vector<std::uint8_t>>* m_plannedBytes; ///< 计划写入载荷（非 owning）
};

// =====================================================================
// HostProjectClosureSource——修订闭包只读源（runtime IRevisionClosureSource
// ← RevisionView 合成计划写入引用）
// =====================================================================

/**
 * @brief 编译期闭包源（基线 RevisionView.objectRefs＋计划写入引用的合成
 *        投影——§6.6"计划闭包"的直接实现）。
 *
 * CM-0 一致性：objectInRevision 与 tryRevision 同一判定源（本类的引用
 * 表成员）——"引用在 objectRefs 内而 objectInRevision 为 false"的自相
 * 矛盾在结构上不可达（RT-CONT-1 防混入判定的合法输入面）。
 */
class HostProjectClosureSource final : public runtime::IRevisionClosureSource {
public:
    /**
     * @brief 构造（合成引用表一次性备妥——编排侧装配，编译期只读）。
     *
     * @param summary [in] 合成修订摘要（基线投影＋计划写入引用——seq/
     *                branch 沿基线；值拷贝持有）
     */
    explicit HostProjectClosureSource(runtime::RevisionSummary summary);

    std::optional<runtime::RevisionSummary> tryRevision(core::RevisionId revision) const override;
    bool objectInRevision(core::RevisionId revision, core::ObjectId objectId,
                          core::ContentVersion version) const override;

private:
    runtime::RevisionSummary m_summary;  ///< 合成修订摘要（值持有——编译期只读）
};

// =====================================================================
// HostProjectClosureView——modeling 闭包域字节源（ObjectClosureView ←
// 同一存储取数面——§9.2"与 CompileRequest.objects 同源绑定"）
// =====================================================================

/**
 * @brief reader 部件解引用源（根对象按 token 路由＋部件按 ObjectId 解引
 *        用；取数面＝基线存储现读＋计划写入覆盖——与字节源同源，同一
 *        编译调用期内的合成视图）。
 *
 * 同源纪律的落点：本视图与 HostProjectObjectBytesSource 消费同一份计划
 * 写入表与同一 query——reader 解引用到的部件对象字节与编译链 S2 取到的
 * 根对象字节出自同一键空间（§9.2"同源绑定"的结构执行）。
 */
class HostProjectClosureView final : public modeling::ObjectClosureView {
public:
    /**
     * @brief 构造（编译调用期内指针有效——reader 构造期绑定本视图）。
     *
     * @param query        [in] 存储查询端口（非 owning）
     * @param designToken  [in] 根对象类型 token（路由键——runtime
     *                     kRobotDesignObjectType 同源常量）
     * @param plannedBytes [in] 计划写入载荷表（非 owning——按 ObjectId
     *                     线性查找；计划写入集规模＝单命令写入数，小）
     */
    HostProjectClosureView(
        const project::IProjectQueryPort& query,
        std::string designToken,
        const std::vector<std::pair<core::ObjectId, std::vector<std::uint8_t>>>*
            plannedBytes);

    std::optional<modeling::ClosureObject>
        tryObjectByToken(std::string_view objectTypeToken) const override;
    std::optional<modeling::ClosureObject>
        tryObject(const core::ObjectId& objectId) const override;

private:
    const project::IProjectQueryPort* m_query;   ///< 存储取数面（非 owning）
    std::string m_designToken;                   ///< 根对象 token（路由键）
    const std::vector<std::pair<core::ObjectId, std::vector<std::uint8_t>>>*
        m_plannedBytes;                          ///< 计划写入载荷（非 owning）
};

// =====================================================================
// HostModelCompilePort——project IModelCompilePort 的 runtime 十段链适配
// （＋最近发布快照缓存——呈现 source 的唯一供数面）
// =====================================================================

/**
 * @brief 宿主编译端口（S5 双编译的真实执行面；快照缓存＝同一产物的传递
 *        ——D10"同一编译链"结构性执行，见文件头背景说明）。
 *
 * 编译路径：project CompileRequest{query, plannedWrites, baseRevision} →
 * 合成闭包（基线＋计划写入）→ runtime CompileRequest{project, revision=
 * 基线, objects/closure/designReader} → RuntimeSnapshotFactory::create
 * （十段链 S1–S10）→ Published：缓存快照＋CompileResult{ok=true}；
 * Failed：诊断透传＋CompileResult{ok=false}；契约违约（UnknownObject/
 * ContextReleased）按 runtime §3.4 原样上抛（project S5 调用方对装配
 * 违约的 fail-fast 语义——禁吞）。
 *
 * designReader 装配：modeling::RobotDesignReader（产品实现）＋
 * HostProjectClosureView（本文件）——§9.2"同源绑定"落点。编译器/快照
 * 工厂实例：构造期建妥、跨调用复用（无状态、可重入——§5.5）。
 */
class HostModelCompilePort final : public project::IModelCompilePort {
public:
    /**
     * @brief 端口装配依赖（每次项目打开成功构造一份——随会话销毁）。
     */
    struct Deps {
        /// 存储查询端口（required——基线引用集与对象字节取数面；非 owning，
        /// 存活期由 ProjectStore 保证覆盖本端口使用期）。
        const project::IProjectQueryPort* query = nullptr;
        /// 归属项目（S5 header.project 的权威来源——快照身份的来源定位
        /// 三元组之一；全零＝调用方装配违约，编译链 S1 拒绝）。
        core::ProjectId project{};
        /// 根对象类型 token（modeling kRobotDesignObjectType 同源——经
        /// 构造注入避免 ui 对 modeling 内部常量的拼写第二源）。
        std::string designObjectTypeToken;
        /// 开发日志通道（可空＝静默——编译终态/快照身份的 Dev 留痕）。
        std::function<void(const std::string&)> devLog;
    };

    /**
     * @brief 构造端口（装配校验 fail-fast——query 空＝装配违约）。
     */
    explicit HostModelCompilePort(Deps deps);

    /**
     * @brief 双编译（S5 编排面——project 契约签名，P-PR-7 不改义）。
     *
     * @param request [in] 编译请求（计划闭包＋基线——引用须在调用期有效）
     * @return ok=false＝任一半边失败（诊断透传；MDL-06 不提交）；ok=true
     *         ＝双编译通过且快照已发布缓存（lastPublishedSnapshot 可取）
     */
    project::CompileResult
        compileWorkCellAndDwc(const project::CompileRequest& request) override;

    /// 最近一次发布的快照（nullopt＝本会话尚无成功编译——呈现 source
    /// 据此诚实拒绝构造，不虚构呈现）。
    std::shared_ptr<const runtime::RuntimeSnapshot> lastPublishedSnapshot() const;

    /// 最近发布对应的编译基线修订（对账面——呈现编排据此核对提交序）。
    core::RevisionId lastPublishedBaseRevision() const;

private:
    Deps m_deps;                                        ///< 装配依赖（构造冻结）
    std::unique_ptr<runtime::ICanonicalModelCompiler> m_compiler; ///< 十段链产品（跨调用复用）
    runtime::RuntimeSnapshotFactory m_factory;          ///< 快照工厂（无状态复用）
    std::shared_ptr<const runtime::RuntimeSnapshot> m_lastSnapshot; ///< 最近发布（同产物传递）
    core::RevisionId m_lastBaseRevision;                ///< 最近发布的编译基线
};

// =====================================================================
// HostRuntimeNameContext——行程评估名称上下文（policy::IPolicyNameContext
// ← 当前呈现视图 NameMap；HandlerServices.assertionPorts 注入面）
// =====================================================================

/**
 * @brief modeling 断言套件的名称解析源（R-4 装配注入——行程上限校验的
 *        runtimeName 解析；绑定**当前呈现视图**的 RuntimeNameMap）。
 *
 * 为什么是本文件自有适配器而不是复用 modeling 的 RuntimeMapPolicyNameContext：
 * 该类型在 modeling/plugin（单元私有装配面——R-2 跨单元私有头禁止）。
 * policy::IPolicyNameContext 是 policy 公共接口，装配层自有实现＝O-31
 * 特权边的合法形态（与 HostProjectClosureView 同位）。语义与 modeling
 * 版逐字一致（转发 RuntimeNameMap 两 resolve＋内容身份）——"两套判定"
 * 禁令不适用于转发适配器（NFR-MNT-04 管的是判定数学，映射查询唯一
 * 权威仍是 RuntimeNameMap 本体）。
 *
 * 未绑定视图（首编译前）＝三方法如实空值——行程评估按"名称不可解析"
 * 诊断轨 Failed（不猜测，ARC-04；评估失败不阻断编译——如实降级形态，
 * 后续修订呈现就位后恢复完整校验）。
 */
class HostRuntimeNameContext final : public policy::IPolicyNameContext {
public:
    /**
     * @brief 构造（绑定真值端口的现取面——视图态随端口 bind/clear 单点
     *        演进，本类零第二绑定状态）。
     * @param port [in] 名称映射真值端口（非 owning——装配层保证存活期）
     */
    explicit HostRuntimeNameContext(const HostRuntimeNameMapPort* port);

    std::optional<core::ObjectId> tryObjectId(const std::string& runtimeName) const override;
    std::optional<std::string> tryRuntimeName(core::ObjectId object) const override;
    core::ContentIdentity nameMapContentIdentity() const override;

private:
    const HostRuntimeNameMapPort* m_port; ///< 真值端口（非 owning——现取当前视图）
};

// =====================================================================
// HostCompileProbe——编译分段探针（modeling::CompileProbe 产品实现——
// 等价验证的 S1～S5 只读注入面）
// =====================================================================

/**
 * @brief DH 等价验证的编译探针（生产装配形态——units/modeling.md §9.4.7
 *        "生产装配（L5）：以 runtime CanonicalModelCompiler 的
 *        buildCanonicalModel 实现探针"的直接执行）。
 *
 * 实现口径（探针契约原文）：
 *   - reader 槽装配"恒返回该 Description"的适配器（探针输入即 Description
 *     ——reader 不消费字节，最小字节面仅为通过 S2 摘要复核）；
 *   - objects/closure 槽以该 Description 的对象身份装配最小闭包（确定性
 *     临时身份——探针编译产物不入快照、不入修订，S10 发布点不可达）；
 *   - 只读＋不发布快照＋确定性（同 Description→同模型）——分段入口
 *     buildCanonicalModel 的既有契约承载，本类零附加语义。
 *
 * 线程安全：编译器无状态可重入；适配器为调用栈局部——const 方法并发
 * 安全（§5.5 注入接口约定）。
 */
class HostCompileProbe final : public modeling::CompileProbe {
public:
    /**
     * @brief 装配依赖。
     * @param designObjectTypeToken [in] 根对象 token（最小闭包的路由键——
     *                              kRobotDesignObjectType 同源常量）
     */
    struct Deps {
        std::string designObjectTypeToken;
        std::function<void(const std::string&)> devLog;
    };

    explicit HostCompileProbe(Deps deps);

    runtime::Expected<runtime::CanonicalModel, runtime::RuntimeError>
        buildCanonicalModel(const runtime::RobotDesignDescription& description) const override;

private:
    Deps m_deps;                                        ///< 装配依赖（构造冻结）
    std::unique_ptr<runtime::ICanonicalModelCompiler> m_compiler; ///< 分段编译器（无状态复用）
};

}  // namespace ui
}  // namespace ird
}  // namespace sdurws

#endif  // IRD_UI_PLUGIN_HOSTCOMPILEPORT_HPP