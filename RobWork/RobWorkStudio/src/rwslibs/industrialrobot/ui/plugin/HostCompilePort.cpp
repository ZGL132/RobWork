/**
 * @file   HostCompilePort.cpp
 * @brief  宿主编译端口实现——合成闭包装配＋十段链调用＋快照缓存（契约头
 *         HostCompilePort.hpp；本文件零业务判定——映射为机械转发，编译
 *         语义全部归 runtime 十段链）。
 */

#include "HostCompilePort.hpp"

#include "HostPresentationAdapters.hpp"  // HostRuntimeNameMapPort 完整类型（currentPresentation 现取面）

#include <algorithm>
#include <stdexcept>
#include <utility>

#include <sdurws/ird/core/Digest.hpp>  // ContentDigester（合成 cv/digest——SHA-256 单一摘要路径）

namespace sdurws {
namespace ird {
namespace ui {

namespace {

/// 对字节向量求 SHA-256（合成内容版本/完整性摘要的单一来源——CON-05
/// 内容寻址语义同源；与存储侧对象字节摘要同一算法同一入口）。
core::Digest256 sha256Of(const std::vector<std::uint8_t>& bytes)
{
    core::ContentDigester digester;
    digester.update(bytes.data(), bytes.size());
    return digester.finalize();
}

/// project 修订视图 → runtime 修订摘要（机械投影——字段一一对应，零加工）。
runtime::RevisionSummary toSummary(const project::RevisionView& view)
{
    runtime::RevisionSummary s;
    s.id = view.id;
    s.seq = view.seq;
    s.parent = view.parent;
    s.branch = view.branch;
    s.objectRefs.reserve(view.objectRefs.size());
    for (const project::ObjectRef& ref : view.objectRefs) {
        runtime::ObjectRefEntry e;
        e.objectId = ref.objectId;
        e.contentVersion = ref.contentVersion;
        e.objectTypeToken = ref.objectTypeToken;
        // digest256 为 64 位小写十六进制（§4.3 引用图列——透传不重算）。
        // 逐字节装配为 Digest256。UI-T77（F-563）防御补齐：长度守卫之外
        // 增**字母表校验**——64 字符含非 hex 字符时旧实现逐字符
        // std::stoi 直接抛 invalid_argument（本函数是合成闭包装配第一
        // 步、draft.apply 处理器链无 try/catch→异常穿透 Qt 事件循环＝
        // 红叉对话框＋进程退出，F-557 家族），且注释自称的「解析失败＝
        // 全零摘要如实投递」无实现支撑。现口径与注释一致：非 64 长度或
        // 非小写 hex 字母表＝清单数据违约——全零摘要如实投递（不抛出
        // 不伪造）：违约条目进入 S2④ 摘要核对面（非计划写入的
        // robot-design 根）时按全零≠真实被拒；被计划写入覆盖的条目按
        // 合成口径以计划载荷摘要自洽（§6.6——本文件 compileWorkCellAndDwc
        // 第一步的覆盖语义）。触发面＝外部工具改写/损坏项目 manifest 的
        // 摘要字段。
        const std::string& hex = ref.digest256;
        bool hexWellFormed = hex.size() == 64;
        if (hexWellFormed) {
            for (const char c : hex) {
                const bool lowerHex =
                    (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
                if (!lowerHex) {
                    hexWellFormed = false;  // 大写/其它字符＝违约（契约限定小写）
                    break;
                }
            }
        }
        if (hexWellFormed) {
            for (std::size_t i = 0; i < 32; ++i) {
                const int hi = std::stoi(std::string(hex.substr(i * 2, 1)), nullptr, 16);
                const int lo = std::stoi(std::string(hex.substr(i * 2 + 1, 1)), nullptr, 16);
                e.digest[i] = static_cast<std::uint8_t>((hi << 4) | lo);
            }
        } else {
            // 「全零摘要如实投递」的承载（Digest256＝std::array——默认
            // 初始化值不确定，违约路径必须显式置零；S2 摘要复核按全零
            // ≠真实摘要稳定拒绝，不伪造有效摘要）。
            e.digest.fill(0);
        }
        s.objectRefs.push_back(std::move(e));
    }
    return s;
}

}  // namespace

// =====================================================================
// HostProjectObjectBytesSource
// =====================================================================

HostProjectObjectBytesSource::HostProjectObjectBytesSource(
    const project::IProjectQueryPort& query,
    const std::vector<std::pair<core::ObjectId, core::ContentVersion>>& plannedKeys,
    const std::vector<std::vector<std::uint8_t>>& plannedBytes)
    : m_query(&query), m_plannedKeys(&plannedKeys), m_plannedBytes(&plannedBytes)
{
}

std::optional<std::vector<std::uint8_t>>
    HostProjectObjectBytesSource::tryObjectBytes(core::ObjectId objectId,
                                                 core::ContentVersion version) const
{
    // 先查计划写入覆盖层（键＝(oid, 合成 cv)——同批合成的引用集配对）。
    for (std::size_t i = 0; i < m_plannedKeys->size(); ++i) {
        const auto& key = (*m_plannedKeys)[i];
        if (key.first == objectId && key.second == version) {
            return (*m_plannedBytes)[i];  // 值拷贝（optional<vector>——§3.3）
        }
    }
    // 基线对象→存储现读（noexcept try 轨——nullopt 原样投递，归属表
    // S2 InputInvalid 由编译器产出定位）。
    return m_query->tryObject(objectId, version);
}

// =====================================================================
// HostProjectClosureSource
// =====================================================================

HostProjectClosureSource::HostProjectClosureSource(runtime::RevisionSummary summary)
    : m_summary(std::move(summary))
{
}

std::optional<runtime::RevisionSummary>
    HostProjectClosureSource::tryRevision(core::RevisionId revision) const
{
    if (!(revision == m_summary.id)) {
        return std::nullopt;  // 非合成修订（编排只锚定基线——S1 UnknownObject 轨）
    }
    return m_summary;  // 值拷贝（修订投影——调用方持有）
}

bool HostProjectClosureSource::objectInRevision(core::RevisionId revision,
                                                core::ObjectId objectId,
                                                core::ContentVersion version) const
{
    if (!(revision == m_summary.id)) {
        return false;  // 同上——非合成修订无闭包成员
    }
    // 与 tryRevision 同一引用表（CM-0 一致性——见类注）。
    for (const runtime::ObjectRefEntry& e : m_summary.objectRefs) {
        if (e.objectId == objectId && e.contentVersion == version) {
            return true;
        }
    }
    return false;
}

// =====================================================================
// HostProjectClosureView
// =====================================================================

HostProjectClosureView::HostProjectClosureView(
    const project::IProjectQueryPort& query,
    std::string designToken,
    const std::vector<std::pair<core::ObjectId, std::vector<std::uint8_t>>>* plannedBytes)
    : m_query(&query), m_designToken(std::move(designToken)), m_plannedBytes(plannedBytes)
{
}

std::optional<modeling::ClosureObject>
    HostProjectClosureView::tryObjectByToken(std::string_view objectTypeToken) const
{
    if (objectTypeToken != m_designToken) {
        return std::nullopt;  // 非 robot-design token＝闭包内无该 token 对象
    }
    // 先查计划写入覆盖层（同 token 的计划写入＝本次新建/重写的根对象——
    // 单命令至多一个 robot-design 写入，取首命中；处理器 S3 重复根约束
    // 的既有裁决面）。
    if (m_plannedBytes != nullptr) {
        for (const auto& entry : *m_plannedBytes) {
            if (!entry.second.empty()) {
                // 字节从计划载荷直供（根对象本次必然是计划写入——新建或
                // 整体重写；基线复用不产生 robot-design 写入）。
                modeling::ClosureObject obj;
                obj.objectTypeToken = m_designToken;
                obj.bytes = entry.second;
                return obj;
            }
        }
    }
    // 基线现读：沿存储 HEAD 引用表找同 token 对象（§9.4.5 根路由——
    // "closure 含根对象"前置由 builder 复核，找不到＝RefMissing）。
    const project::RevisionView head = m_query->head();
    for (const project::ObjectRef& ref : head.objectRefs) {
        if (ref.objectTypeToken == m_designToken) {
            const std::optional<std::vector<std::uint8_t>> bytes =
                m_query->tryObject(ref.objectId, ref.contentVersion);
            if (!bytes.has_value()) {
                return std::nullopt;  // 存储不可得＝闭包缺根（builder 拒绝面）
            }
            modeling::ClosureObject obj;
            obj.objectTypeToken = ref.objectTypeToken;
            obj.bytes = std::move(*bytes);
            return obj;
        }
    }
    return std::nullopt;
}

std::optional<modeling::ClosureObject>
    HostProjectClosureView::tryObject(const core::ObjectId& objectId) const
{
    // 计划写入覆盖层优先（部件对象本次重写/新建——同源键空间）。
    if (m_plannedBytes != nullptr) {
        for (const auto& entry : *m_plannedBytes) {
            if (entry.first == objectId && !entry.second.empty()) {
                modeling::ClosureObject obj;
                // token 未知（本视图不持 (oid,token) 映射——计划写入的
                // token 在字节源覆盖层配对表里；部件解引用只消费字节，
                // token 由 builder 复核面自行核对）——以空串投递会让
                // builder 的 token 复核失据。改从字节源同款键表装配时
                // 一并提供 token（编排侧装配 plannedBytes 对时携带）。
                obj.objectTypeToken.clear();
                obj.bytes = entry.second;
                return obj;
            }
        }
    }
    // 基线现读：沿 HEAD 引用表按 ObjectId 定位（取 token＋cv 后读字节）。
    const project::RevisionView head = m_query->head();
    for (const project::ObjectRef& ref : head.objectRefs) {
        if (ref.objectId == objectId) {
            const std::optional<std::vector<std::uint8_t>> bytes =
                m_query->tryObject(ref.objectId, ref.contentVersion);
            if (!bytes.has_value()) {
                return std::nullopt;
            }
            modeling::ClosureObject obj;
            obj.objectTypeToken = ref.objectTypeToken;
            obj.bytes = std::move(*bytes);
            return obj;
        }
    }
    return std::nullopt;
}

// =====================================================================
// HostModelCompilePort
// =====================================================================

HostModelCompilePort::HostModelCompilePort(Deps deps)
    : m_deps(std::move(deps)),
      m_compiler(runtime::createCanonicalModelCompiler())
{
    // 装配校验（fail-fast——禁构造不可编译的端口；编译器/工厂构造零失败面）。
    if (m_deps.query == nullptr || m_deps.designObjectTypeToken.empty()
        || !m_deps.project.isValid()) {
        throw std::invalid_argument(
            "ui/hostcompile: query/designObjectTypeToken/project 必填非空"
            "（装配契约违约——每次项目打开成功构造）");
    }
}

project::CompileResult
    HostModelCompilePort::compileWorkCellAndDwc(const project::CompileRequest& request)
{
    project::CompileResult out;

    // ---- 第一步：合成闭包装配（§6.6 计划闭包——基线投影＋计划写入）。
    // 计划写入对象的内容版本＝载荷字节 SHA-256（合成口径见文件头；编译
    // 调用期内 cv/digest/字节三面自洽——CM-0 校验闭合）。
    const project::RevisionView base = request.query.revision(request.baseRevision);
    runtime::RevisionSummary summary = toSummary(base);

    // 计划写入键/字节/（oid,字节）三表并行装配——字节源/闭包源/reader
    // 闭包视图同源消费（§9.2 同源绑定的编排侧执行）。
    std::vector<std::pair<core::ObjectId, core::ContentVersion>> plannedKeys;
    std::vector<std::vector<std::uint8_t>> plannedByteCopies;
    std::vector<std::pair<core::ObjectId, std::vector<std::uint8_t>>> plannedPairs;
    plannedKeys.reserve(request.plannedWrites.size());
    plannedByteCopies.reserve(request.plannedWrites.size());
    plannedPairs.reserve(request.plannedWrites.size());
    for (const project::ObjectWrite& write : request.plannedWrites) {
        if (!write.objectId.has_value()) {
            continue;  // 申请新对象（project 分配）不会出现在 S5——S3 已解析
        }
        core::ContentVersion cv;
        cv.bytes = sha256Of(write.payloadCanonical);
        plannedKeys.emplace_back(*write.objectId, cv);
        plannedByteCopies.push_back(write.payloadCanonical);
        plannedPairs.emplace_back(*write.objectId, write.payloadCanonical);

        // 计划写入引用进合成修订（同 oid 基线引用被覆盖——对象全量替换
        // 语义；引用表去重以计划写入优先，编译器逐项取数即得新态）。
        runtime::ObjectRefEntry e;
        e.objectId = *write.objectId;
        e.contentVersion = cv;
        e.objectTypeToken = write.objectTypeToken;
        e.digest = cv.bytes;  // 同一摘要（内容寻址——cv 与 digest 同源）
        summary.objectRefs.erase(
            std::remove_if(summary.objectRefs.begin(), summary.objectRefs.end(),
                           [&write](const runtime::ObjectRefEntry& old) {
                               return old.objectId == *write.objectId;
                           }),
            summary.objectRefs.end());
        summary.objectRefs.push_back(std::move(e));
    }

    // ---- 第二步：注入源与 reader 装配（栈上——编译调用期存活）。
    HostProjectClosureSource closureSource(std::move(summary));
    HostProjectObjectBytesSource bytesSource(*m_deps.query, plannedKeys,
                                             plannedByteCopies);
    HostProjectClosureView closureView(*m_deps.query, m_deps.designObjectTypeToken,
                                       &plannedPairs);
    const modeling::RobotDesignReader reader(&closureView);

    // ---- 第三步：runtime 请求装配＋十段链整体事务（§5——S1～S10；DWC
    // 默认请求＝MDL-06 双编译语义；取消令牌不注入——命令执行槽内同步
    // 编译无取消通道，D-11 的可空形态）。
    runtime::CompileRequest rtRequest;
    rtRequest.project = m_deps.project;
    rtRequest.revision = request.baseRevision;
    rtRequest.objects = &bytesSource;
    rtRequest.closure = &closureSource;
    rtRequest.designReader = &reader;
    // options 取默认（requestDynamicWorkCell=true＝双编译；includeCollision
    // Geometry=true——呈现几何与碰撞几何齐备，宿主挂接即完整模型）。

    const runtime::CompileOutcome outcome = m_factory.create(rtRequest, *m_compiler);

    if (outcome.status == runtime::CompileStatus::Published) {
        // Published＝快照已发布（同一产物缓存传递——呈现 source 的供数面；
        // D10"同一编译链"的结构性执行，禁第二构造路径）。
        m_lastSnapshot = outcome.snapshot;
        m_lastBaseRevision = request.baseRevision;
        out.ok = true;
        if (m_deps.devLog) {
            m_deps.devLog("host-compile: 发布快照（基线 "
                          + request.baseRevision.toCanonical()
                          + "；计划写入 " + std::to_string(plannedKeys.size())
                          + " 对象；名称映射条目 "
                          + std::to_string(outcome.snapshot->nameMap().size()) + "）");
        }
    } else {
        // Failed/Cancelled→不提交（MDL-06；诊断透传——失败定位归编译器
        // 全量诊断，端口零加工）。取消在本装配形态不可达（无取消令牌），
        // 如实按失败面呈现。
        out.ok = false;
        out.diagnostics = outcome.diagnostics;
        if (m_deps.devLog) {
            m_deps.devLog("host-compile: 编译失败（基线 "
                          + request.baseRevision.toCanonical()
                          + "；诊断 " + std::to_string(outcome.diagnostics.size())
                          + " 条）——不提交（MDL-06）");
        }
    }
    return out;
}

std::shared_ptr<const runtime::RuntimeSnapshot> HostModelCompilePort::lastPublishedSnapshot() const
{
    return m_lastSnapshot;
}

core::RevisionId HostModelCompilePort::lastPublishedBaseRevision() const
{
    return m_lastBaseRevision;
}

// =====================================================================
// HostRuntimeNameContext——行程评估名称上下文（转发真值端口现取视图；
// 契约注释见头）
// =====================================================================

HostRuntimeNameContext::HostRuntimeNameContext(const HostRuntimeNameMapPort* port)
    : m_port(port)
{
    if (m_port == nullptr) {
        throw std::invalid_argument(
            "ui/hostnamecontext: port 必填非空（装配契约违约）");
    }
}

std::optional<core::ObjectId> HostRuntimeNameContext::tryObjectId(
    const std::string& runtimeName) const
{
    // 现取当前视图（端口 bind/clear 单点演进——本类零第二绑定状态）。
    const std::shared_ptr<const runtime::HostPresentationView> view =
        m_port->currentPresentation();
    if (view == nullptr) {
        return std::nullopt;  // 未绑定＝名称不可解析（如实空——不猜测）
    }
    const auto resolved = view->resolveRuntimeName(runtimeName);
    if (!resolved.ok()) {
        return std::nullopt;  // UnknownObject＝映射中无该名称
    }
    return resolved.get().objectId;
}

std::optional<std::string> HostRuntimeNameContext::tryRuntimeName(core::ObjectId object) const
{
    const std::shared_ptr<const runtime::HostPresentationView> view =
        m_port->currentPresentation();
    if (view == nullptr) {
        return std::nullopt;
    }
    const auto resolved = view->resolveObjectId(object);
    if (!resolved.ok()) {
        return std::nullopt;
    }
    return resolved.get().fullName;
}

core::ContentIdentity HostRuntimeNameContext::nameMapContentIdentity() const
{
    // 未绑定＝全零保留值（与"空映射"在本类型层同态——NameMap §查询入口
    // 4/4 既定口径，非自造哨兵；modeling 版适配器同款语义）。
    const std::shared_ptr<const runtime::HostPresentationView> view =
        m_port->currentPresentation();
    return view != nullptr ? view->nameMap().contentIdentity()
                           : core::ContentIdentity{};
}

// =====================================================================
// HostCompileProbe——编译分段探针（最小闭包装配＋分段入口调用；契约
// 注释见头）
// =====================================================================

namespace {

/**
 * @brief 探针专用恒值 reader（S2 的 Description 注入面——read 恒返回
 *        构造捕获的 Description 值拷贝；不消费字节——字节面仅为通过
 *        编译器 S2 的取数与摘要复核）。
 */
class ProbeConstantReader final : public runtime::IRobotDesignReader {
public:
    explicit ProbeConstantReader(runtime::RobotDesignDescription description)
        : m_description(std::move(description))
    {
    }

    runtime::Expected<runtime::RobotDesignDescription, runtime::RuntimeError>
        read(const std::vector<std::uint8_t>& /*objectBytes*/,
             std::uint32_t /*objectTypeFormatVersion*/) const override
    {
        // 恒返回捕获值（探针契约——"恒返回该 Description"的装配形态；
        // 同输入同输出——确定性由值拷贝语义直接成立）。
        return runtime::Expected<runtime::RobotDesignDescription,
                                 runtime::RuntimeError>::ok(m_description);
    }

private:
    runtime::RobotDesignDescription m_description; ///< 捕获的描述值（构造冻结）
};

/**
 * @brief 探针专用最小字节源（robot 根对象字节——探测占位字节 {0x00}；
 *        S2④ 摘要复核的申报值由闭包源以同一字节自算——闭合）。
 */
class ProbeMinimalObjects final : public runtime::IObjectBytesSource {
public:
    std::optional<std::vector<std::uint8_t>>
        tryObjectBytes(core::ObjectId objectId, core::ContentVersion /*version*/) const override
    {
        if (!(objectId == kProbeRobotId)) {
            return std::nullopt;
        }
        return std::vector<std::uint8_t>{0x00};  // 占位字节（确定性——摘要复核配对）
    }

    /// 探针根对象身份（确定性临时句柄——闭包源/字节源/请求三面配对键）。
    static const core::ObjectId& robotId()
    {
        return kProbeRobotId;
    }

private:
    inline static const core::ObjectId kProbeRobotId = core::ObjectId::generate();
};

/**
 * @brief 探针专用最小闭包源（恒锚定探针修订；引用表＝恰一根 robot-design
 *        条目——S2② 定位的构成性前提）。
 */
class ProbeMinimalClosure final : public runtime::IRevisionClosureSource {
public:
    explicit ProbeMinimalClosure(std::string designToken)
        : m_designToken(std::move(designToken))
    {
    }

    std::optional<runtime::RevisionSummary> tryRevision(core::RevisionId revision) const override
    {
        if (!(revision == m_revision)) {
            return std::nullopt;
        }
        runtime::RevisionSummary s;
        s.id = m_revision;
        s.seq = 1;
        s.branch = core::BranchId::generate();
        runtime::ObjectRefEntry e;
        e.objectId = ProbeMinimalObjects::robotId();
        e.contentVersion = m_cv;
        e.objectTypeToken = m_designToken;
        const std::vector<std::uint8_t> probeBytes{0x00};
        e.digest = [&probeBytes] {
            core::ContentDigester d;
            d.update(probeBytes.data(), probeBytes.size());
            return d.finalize();
        }();
        s.objectRefs.push_back(std::move(e));
        return s;
    }

    bool objectInRevision(core::RevisionId revision, core::ObjectId objectId,
                          core::ContentVersion version) const override
    {
        return revision == m_revision && objectId == ProbeMinimalObjects::robotId()
            && version == m_cv;
    }

    /// 探针修订身份（每次探针构造新生成——确定性临时身份，不入任何持久面）。
    const core::RevisionId& revision() const
    {
        return m_revision;
    }

private:
    std::string m_designToken;              ///< 根对象 token（路由键）
    core::RevisionId m_revision = core::RevisionId::generate(); ///< 探针修订（临时）
    /// 探针内容版本（确定性种子摘要——ContentVersion 无 generate 工厂；
    /// 全零＝保留值不合法进引用表，以种子摘要充当合成 cv——与字节源/
    /// 闭包源三面配对自洽）。
    core::ContentVersion m_cv = [] {
        core::ContentVersion cv;
        const std::string seed = "ui-host-compile-probe";
        core::ContentDigester d;
        d.update(seed.data(), seed.size());
        cv.bytes = d.finalize();
        return cv;
    }();
};

}  // namespace

HostCompileProbe::HostCompileProbe(Deps deps)
    : m_deps(std::move(deps)), m_compiler(runtime::createCanonicalModelCompiler())
{
    if (m_deps.designObjectTypeToken.empty()) {
        throw std::invalid_argument(
            "ui/hostprobe: designObjectTypeToken 必填非空（装配契约违约）");
    }
}

runtime::Expected<runtime::CanonicalModel, runtime::RuntimeError>
    HostCompileProbe::buildCanonicalModel(const runtime::RobotDesignDescription& description) const
{
    // 最小闭包装配（探针契约——对象身份为确定性临时句柄，不入快照不入
    // 修订；分段入口不发布快照——S10 不可达）。
    ProbeMinimalObjects objects;
    ProbeMinimalClosure closure(m_deps.designObjectTypeToken);
    ProbeConstantReader reader(description);

    runtime::CompileRequest request;
    request.project = core::ProjectId::generate();  // S5 header.project 前置——探针产物不入任何注册面
    request.revision = closure.revision();
    request.objects = &objects;
    request.closure = &closure;
    request.designReader = &reader;
    // options 取默认（分段入口不触及 S6/S7——DWC 选项无消费面）。

    // 分段入口（S1～S5——只读、不发布快照；失败 err 轨透传——归建模侧
    // 等价验证报告的 compileOk=false 面）。
    return m_compiler->buildCanonicalModel(request);
}

// =====================================================================
// HostDraftAwareNameContext——草稿名感知装饰上下文（UI-T74/F-546 出路①）
// =====================================================================
HostDraftAwareNameContext::HostDraftAwareNameContext(Deps deps)
    : m_deps(std::move(deps))
{
}
std::optional<core::ObjectId> HostDraftAwareNameContext::tryObjectId(
    const std::string& runtimeName) const
{
    // 反解向只转发发布真值（草稿对象无运行时名可反解——类注释差异面）。
    return m_deps.runtime->tryObjectId(runtimeName);
}
std::optional<std::string> HostDraftAwareNameContext::tryRuntimeName(
    core::ObjectId object) const
{
    // 第一优先：发布真值（已发布对象——重应用场景的身份标签与发布态一致）。
    if (const auto published = m_deps.runtime->tryRuntimeName(object)) {
        return published;
    }
    // 第二优先：草稿名源（发布前窗口期——模块草稿工作集自身命名；
    // 闭包外身份 nullopt 原样透传——不猜测，ARC-04）。
    return m_deps.draftName ? m_deps.draftName(object) : std::nullopt;
}
core::ContentIdentity HostDraftAwareNameContext::nameMapContentIdentity() const
{
    // 恒转发真值（草稿补位不参与映射身份——头文件差异面登记；行程路径
    // 不消费本身份，CollisionEvaluator 会话账本才是其消费面）。
    return m_deps.runtime->nameMapContentIdentity();
}

}  // namespace ui
}  // namespace ird
}  // namespace sdurws

