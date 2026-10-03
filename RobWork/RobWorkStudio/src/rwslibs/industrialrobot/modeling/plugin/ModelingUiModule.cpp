/**
 * @file   ModelingUiModule.cpp
 * @brief  建模插件界面模块实现——§11.2 buildDraftCommand 的域侧组装。
 *
 * 设计依据：units/ui.md §11.2/§8.5、modeling.md §9.7.2 L-3/§9.3/§6.4；
 * 契约 WP-13-T15 acceptance 3/4。实现纪律：信封组装＝数据搬运＋确定性
 * 编码（合法性判定零参与——解码门/断言分域在命令 prepare 现场重估）。
 */

#include "ModelingUiModule.hpp"

#include "ModelingCommandFlows.hpp"           // executeModelingCommand（UI-T41 A2——域命令 UI 流）
#include "ModelingPanelWidget.hpp"
#include "PolicyNameContexts.hpp"             // RuntimeMapPolicyNameContext（T03b——映射转发形名称上下文，退役草稿桩）

#include <stdexcept>
#include <utility>

#include <sdurws/ird/modeling/Template.hpp>  // RobotDesignTemplateFactory/kTemplateIdGeneric6R（首版装配会话种子——真实域路径）

#include <sdurws/ird/modeling/Codec.hpp>       // RobotDesignCodec/kCurrentFormatVersion（根对象确定性编码——§4.8）
#include <sdurws/ird/modeling/ObjectTypes.hpp> // kRobotDesignObjectType（根对象 token——runtime 单一权威的 using 重导出）

namespace sdurws::ird::modeling {

// =====================================================================
// ui::IUiDomainReadinessSource——域就绪汇聚源（T03b 收口；§6.5 端口半区）
// =====================================================================

std::vector<ui::DomainReadinessItem> ModelingUiModule::domainReadiness(
    ui::StageId stage) const
{
    // 非建模阶段＝该阶段无本源投影（§6.5 端口契约"空清单不计入快照"）。
    if (stage != ui::StageId::Modeling) {
        return {};
    }
    // 跨线程拉取路径（§6.5 线程契约——readinessSnapshot 供 workflow 任意
    // 线程调用）：互斥锁保护的快照值拷贝、短临界区；UI 线程的重算写入与
    // 此并发安全。零判定——投影行自就绪报告直投（N-11 无第二套）。
    std::lock_guard<std::mutex> lock(m_readinessMutex);
    if (!m_readinessSnapshot.has_value()) {
        ui::DomainReadinessItem empty;
        empty.domainKey = "modeling";
        empty.verdict = core::EngineeringStatus::DataInsufficient;
        empty.inputComplete = false;
        return {empty};
    }
    return modelingReadinessProjection(*m_readinessSnapshot);
}

std::optional<project::CommandEnvelope> ModelingUiModule::buildDraftCommand(
    const std::string& moduleId)
{
    m_guard.assertOnUiThread();  // §3.4——命令组装读取会话权威态（仅 UI 线程）

    // ①域外请求（§11.2 参数语义：moduleId＝域注册键）。
    if (moduleId != "modeling") { return std::nullopt; }

    // ②无草稿变更＝无可应用内容（§8.5"无草稿可应用"态——不产生空修订；
    //   变更记录＝域编辑流的 append-only 留痕，空即"自基线以来零编辑"）。
    if (m_session.draft.changes.empty()) { return std::nullopt; }

    // ③根对象确定性编码（Codec 单一实现——§4.8 canonical 序列化；同草稿
    //   重复组装同字节，NFR-COR-02）。编码失败＝模型非法（schema 违约），
    //   属实现/上游缺陷——fail-fast 上抛，不产出畸形信封。
    const RobotDesignCodec codec;
    const auto encoded = codec.encode(
        ObjectVariant{m_session.draft.design}, kCurrentFormatVersion);
    if (!encoded.ok()) {
        throw std::runtime_error("modeling 面板：草稿编码失败（"
                                 + encoded.error().detail + "）——不产出畸形命令信封");
    }

    // ④信封组装（§6.4 版本三元组：commandType＋payloadFormatVersion＋
    //   负载字节；expectedRevision=会话基线，nullopt＝提交期解析 tip）。
    project::CommandEnvelope envelope;
    envelope.branch = m_session.branch;
    envelope.expectedRevision = m_session.baseRevision;
    envelope.commandType = std::string(kCmdApplyRobotDesign);  // project 词表 token（无点——两套命名空间纪律）
    envelope.payloadFormatVersion = kCommandPayloadVersion;    // 处理器自有版本戳（v2）

    // ⑤根对象槽：allocateNew＝根身份未回填（模板初始草稿——prepare 取号
    //   回填，PA-1 身份分配唯一归 project）；已回填＝既有对象替换。
    //   （PayloadObjectSlot 归 modeling::CommandHandlers——处理器域内载
    //    面而非 project 类型。）
    PayloadObjectSlot slot;
    slot.allocateNew = !m_session.draft.rootObjectId.has_value();
    slot.objectId = m_session.draft.rootObjectId.value_or(core::ObjectId{});
    slot.objectTypeToken = std::string(kRobotDesignObjectType);  // 根对象 token（runtime 单一权威）
    slot.objectBytes = encoded.get();
    envelope.payloadCanonical = encodeCommandPayload(CommandPayload{
        CommandPayload::Mode::Apply, {std::move(slot)}, {}});

    // ⑥面板引用消费说明：组装路径不触面板（面板是呈现半区——命令组装的
    //   权威输入只有会话态；m_panel 仅供 readonlyProjections 的联动刷新，
    //   此处显式消费避免"未使用成员"误读）。
    (void)m_panel;
    return envelope;
}

// =====================================================================
// 首版装配 API（WP-24-T03——装配门面 ModelingPluginAssembly 转发目标）
// =====================================================================

ModelingUiModule::ModelingUiModule()
    : m_nameContext(std::make_unique<RuntimeMapPolicyNameContext>(nullptr))
{
    // T03b 收口：名称上下文＝映射转发形适配器（nullptr＝未绑定映射——
    // 如实 nullopt/全零，可观测行为与退役的草稿桩一致；映射经
    // bindRuntimeNameMap 绑定后 L 行名称解析全量可用——PA-1 零第二构造）。
}

void ModelingUiModule::bindRuntimeNameMap(const runtime::RuntimeNameMap* map)
{
    m_guard.assertOnUiThread();
    // 绑定切换＝重建适配器（上下文不可变面——指针构造后不改写；装配期
    // 一次，SA-01 同纪律。重复绑定仅在显式重装配场景出现）。
    m_nameContext = std::make_unique<RuntimeMapPolicyNameContext>(map);
    // 映射在位改变名称解析事实——就绪重算（新映射下的 L 行口径）。
    recomputeReadiness();
}

// =====================================================================
// 宿主迁移三接入面（WP-13-T20——B1-SPEC §5.1；只消费 UI-T21/T22 冻结协议）
// =====================================================================

ModelingUiModule::SharedSurfaceHandles ModelingUiModule::sharedSurfaceProviders()
{
    m_guard.assertOnUiThread();  // §3.4——会话态绑定与面板指针读取（UI 线程）

    // 惰性构造＋缓存（shared_ptr 稳定地址——宿主注册进共享模型后模型持
    // 强引用，本模块缓存同序；Deps 一次性绑定：工作集现取入口指向会话
    // 权威工作集〔ACC5 零缓存〕，编辑分流出口与高亮/激活执行器绑定面板
    // 指针——时序契约见头注"须在 createPanel 之后调用"）。
    if (!m_treeProvider) {
        ModelingSharedSurfaceDeps deps;
        deps.workingSet = [this]() -> ModelingWorkingSet* {
            return &m_session.draft;  // 会话权威工作集（唯一载体——§3.4）
        };
        deps.editSink = m_panel;  // 面板即 IPanelEditSink（L-2 三路分流落点）
        deps.panelHighlight = [this](const std::optional<core::ObjectId>& oid) {
            if (m_panel != nullptr) { m_panel->focusObject(oid); }  // 树选→面板高亮
        };
        deps.complexPageActivator = [this](const core::ObjectId& oid) {
            if (m_panel != nullptr) { m_panel->focusObject(oid); }  // D6 域自持打开＝定位目标对象
        };
        m_treeProvider = std::make_shared<ModelingTreeNodesProvider>(deps);
        m_pageProvider = std::make_shared<ModelingPropertyPagesProvider>(deps);
    }
    SharedSurfaceHandles handles;
    handles.treeNodes = m_treeProvider;
    handles.propertyPages = m_pageProvider;
    return handles;
}

void ModelingUiModule::attachSelectionService(ui::SelectionService& service)
{
    m_guard.assertOnUiThread();  // §3.4——订阅是 UI 线程交互面
    if (!m_treeProvider) {
        sharedSurfaceProviders();  // 适配器与三接入面同 deps——惰性齐备
    }
    if (!m_adapter) {
        // 适配器 Deps 与 Provider 同源（工作集现取＋面板执行器——同一
        // 装配语义，零第二份绑定面）。
        ModelingSharedSurfaceDeps deps;
        deps.workingSet = [this]() -> ModelingWorkingSet* {
            return &m_session.draft;
        };
        deps.panelHighlight = [this](const std::optional<core::ObjectId>& oid) {
            if (m_panel != nullptr) { m_panel->focusObject(oid); }
        };
        m_adapter = std::make_unique<ModelingSelectionAdapter>(std::move(deps));
    }
    m_adapter->attach(service);
}

void ModelingUiModule::detachSelectionService()
{
    m_guard.assertOnUiThread();
    if (m_adapter) { m_adapter->detach(); }
}

bool ModelingUiModule::reportView3DPick(const core::ObjectId& oid)
{
    m_guard.assertOnUiThread();  // §3.4——选中写入口只允许 UI 线程
    if (!m_adapter) { return false; }  // 未接线＝无上报通道（诚实 false）
    return m_adapter->reportView3DPick(oid);
}

void ModelingUiModule::bindPolicyProvider(
    std::function<const policy::EngineeringPolicySet*()> provider)
{
    m_guard.assertOnUiThread();
    m_policyProvider = std::move(provider);
    // 策略装载事实变化——就绪重算（已装载→L 行真判定；卸载→L11 如实
    // Blocking 回落，不缓存旧判定）。
    recomputeReadiness();
}

void ModelingUiModule::bindCommandSubmit(CommandSubmitFn submitFn)
{
    m_guard.assertOnUiThread();
    if (m_panel != nullptr) {
        // 面板已创建——即时转发（与面板 setCommandSubmit 同语义）。
        m_panel->setCommandSubmit(std::move(submitFn));
        return;
    }
    // 面板未创建——暂存，createPanel 时应用（装配序无关的绑定面）。
    m_pendingSubmit = std::move(submitFn);
}

void ModelingUiModule::bindCommandAvailability(CommandAvailabilityFn availability)
{
    m_guard.assertOnUiThread();
    if (m_panel != nullptr) {
        m_panel->setCommandAvailability(std::move(availability));
        return;
    }
    m_pendingAvailability = std::move(availability);
}

void ModelingUiModule::bindTextResolver(std::function<QString(const std::string&)> resolve)
{
    m_guard.assertOnUiThread();
    if (m_panel != nullptr) {
        m_panel->setCommandTitleResolver(std::move(resolve));
        return;
    }
    m_textResolver = std::move(resolve);
}

void ModelingUiModule::setWritable(bool writable)
{
    // L-7 门控输入转发（UI-T43——宿主按打开报告的真实 writable 驱动；
    // 本转发面零判定，需求域 RequirementsUiModule::setWritable 同构）。
    // 双形态与 bindCommandSubmit 同一时序语义：面板已创建＝即时转发（面板
    // 内即时降级属性行与写命令按钮）；未创建＝暂存初值（createPanel 时
    // 应用——装配序无关，杜绝面板恒按可写创建的初值失实）。
    m_guard.assertOnUiThread();
    m_writable = writable;
    if (m_panel != nullptr) {
        m_panel->setWritable(writable);
    }
}

void ModelingUiModule::seedTemplateSession()
{
    m_guard.assertOnUiThread();
    // 真实域路径：RobotDesignTemplateFactory::createDraft（§9.4.2——纯函数、
    // 仅内存、不触 project、不产生修订）。安装预设 Ground（MDL-22 缺省）。
    // 失败 fail-fast（模板目录静态登记 generic-6r 恒在——装配错误面）。
    const RobotDesignTemplateFactory factory;
    std::vector<core::DiagnosticRecord> diags;
    const TemplateOutcome outcome = factory.createDraft(
        TemplateId{kTemplateIdGeneric6R},
        runtime::InstallationPresetToken::Ground, "demo", diags);
    // 审核修正（UI-T41 批次D R1——恰一根不变量）：重种子＝既有模型内容的
    // 模板化替换——已回填的项目根身份保留（下次 draft.apply 走同根替换，
    // 不按 allocateNew 重复建根——requirements 域 F-461 同型缺陷的预防）；
    // 无根（纯草稿会话/会话脱离后）保持 nullopt＝应用时分配。
    const std::optional<core::ObjectId> previousRoot = m_session.draft.rootObjectId;
    m_session.draft = outcome.get();
    m_session.draft.rootObjectId = previousRoot;
    // 种子即重置应用侧快照（UI-T41——基线快照/预览视图随草稿重建失效）。
    m_session.baselineSnapshot.reset();
    m_session.appliedPreview.reset();
    // baseRevision 留空（nullopt＝提交期解析 tip——§6.2）；种子后就绪
    // 真判定即刻重算（T03b-2b——就绪条不再空态，如实呈现阻塞/缺项）。
    recomputeReadiness();
}

QWidget* ModelingUiModule::createPanel()
{
    m_guard.assertOnUiThread();
    // 面板与接线一次完成：可写初值取模块当前会话可写性（UI-T43——此前
    // 硬编码 true：宿主在面板创建前已打开只读项目时，面板初始态失实；
    // setWritable 暂存语义保证装配序无关）。编辑目标提供器现取会话权威
    // 工作集（ACC5 零缓存——提供器每次经 session() 入口取指针）。
    auto* panel = new ModelingPanelWidget(m_writable);
    panel->setEditTargetProvider([this]() -> ModelingWorkingSet* {
        return &m_session.draft;
    });
    if (m_pendingSubmit) {
        panel->setCommandSubmit(std::move(m_pendingSubmit));
    }
    if (m_pendingAvailability) {
        panel->setCommandAvailability(std::move(m_pendingAvailability));
    }
    if (m_textResolver) {
        panel->setCommandTitleResolver(m_textResolver);
    }
    m_panel = panel;  // attachPanel 同义（公开方法语义一致，直接落成员）
    // 编辑后钩子（T03b-2b）：每次 L-2 接受后重算真就绪并刷新就绪条。
    panel->setPostEditAction([this] { recomputeReadiness(); });
    // 首刷（§9.7.2 L-4 同款入口——装配层创建面板后必须立即呈现当前会话）：
    // 树/属性区投影自会话工作集（种子草稿即刻可见），就绪条取会话已载报告
    // （未载＝空报告，呈现空态——不伪造判定）。
    panel->refreshPanel(m_session.draft,
                        m_session.readiness.value_or(ModelReadinessReport{}));
    return panel;
}

void ModelingUiModule::refreshFromSession()
{
    m_guard.assertOnUiThread();
    if (m_panel != nullptr) {
        m_panel->refreshPanel(m_session.draft,
                              m_session.readiness.value_or(ModelReadinessReport{}));
    }
}

// =====================================================================
// 域命令 UI 流程执行（UI-T41 A2——宿主注册表处理器的建模侧落点）
// =====================================================================

bool ModelingUiModule::executeDomainCommand(const std::string& commandId)
{
    m_guard.assertOnUiThread();
    // 流程依赖（宿主侧回调全量接线——flows 零模块类型依赖的测试缝形态）：
    // reseedTemplate＝模板重种子；recomputeReadiness＝草稿变更后真判定；
    // selectedAnchor＝面板会话选中锚（estimate/diff 的目标解析输入面）。
    ModelingFlowDeps deps;
    deps.reseedTemplate = [this] { seedTemplateSession(); };
    deps.recomputeReadiness = [this] { recomputeReadiness(); };
    deps.selectedAnchor = [this]() -> std::optional<core::ObjectId> {
        return m_panel != nullptr ? m_panel->selectedAnchor() : std::nullopt;
    };
    std::string summary;
    const bool executed = executeModelingCommand(commandId, m_session, deps,
                                                 qtModelingDialogHost(), summary);
    if (m_panel != nullptr && !summary.empty()) {
        // 回执/原因就地呈现（非模态——UX-03/07；命令级反馈与编辑反馈同口）。
        m_panel->setOutcomeMessage(QString::fromStdString(summary));
    }
    return executed;
}

void ModelingUiModule::recomputeReadiness()
{
    m_guard.assertOnUiThread();
    // 真判定（T03b-2b 起，T03b 收口全量化）：真实 ModelReadinessChecker＋
    // policy 行程评估器。套件/检查器须为具名局部量（checker 持套件指针——
    // 临时量悬挂风险）。
    // 策略来源（T03b 收口）：提供器现取已装载策略——nullptr＝未装载，
    // CheckContext.resolvedPolicy 置空，L11 层对"策略不可解析"如实产出
    // Blocking（行程校验未执行的事实随行），不伪造可行（UX-02/ERR-01）。
    const AssertionSuite::Ports ports{m_evaluator.get(), m_nameContext.get()};
    const AssertionSuite suite{ports};
    const ModelReadinessChecker checker{suite};
    CheckContext context;  // 草稿态预检口径（baseRevision 留空——§8.2 注）
    if (m_policyProvider) {
        context.resolvedPolicy = m_policyProvider();
    }
    m_session.readiness = checker.check(m_session.draft, context);
    {
        // 跨线程快照同步（domainReadiness 任意线程拉取——§6.5 线程契约；
        // 值拷贝写入，短临界区）。
        std::lock_guard<std::mutex> lock(m_readinessMutex);
        m_readinessSnapshot = m_session.readiness;
    }
    if (m_panel != nullptr) {
        m_panel->refreshPanel(m_session.draft, *m_session.readiness);
    }
}

void ModelingUiModule::onSessionDetached()
{
    m_guard.assertOnUiThread();
    // §8.6 表"模块表/局部栈清空"的模块状态半区：会话内存态整体复位——
    // 分支锚清空（Stale 判据随锚消失，旧会话不可能污染新会话——§6.2
    // 纪元过滤的模块对位）、草稿工作集复位、就绪报告作废。磁盘草稿零
    // 触碰（落盘/丢弃归 DraftController 处置链——§5.4 决议执行半区）。
    m_session.branch = core::BranchId{};
    m_session.baseRevision.reset();
    m_session.draft = ModelingWorkingSet{};
    m_session.readiness.reset();
    m_session.baselineSnapshot.reset();   // 应用侧快照随会话作废（UI-T41）
    m_session.appliedPreview.reset();
    {
        std::lock_guard<std::mutex> lock(m_readinessMutex);
        m_readinessSnapshot.reset();
    }
    if (m_panel != nullptr) {
        // 面板复位空态（空工作集＋空报告——不虚构任何会话事实）。
        m_panel->refreshPanel(m_session.draft, ModelReadinessReport{});
    }
}

void ModelingUiModule::onRevisionCommitted(const core::BranchId& branch,
                                           const core::RevisionId& newTip)
{
    m_guard.assertOnUiThread();
    // 分支过滤（§6.2 纪元过滤的模块对位）：非当前锚定分支的提交不触碰
    // 编辑基线——未锚定（branch 全零）时同样忽略（无会话无同步对象）。
    if (!(m_session.branch == branch)) {
        return;
    }
    // 非 draft.apply 路径的修订提交（撤销/重做/其他入口——§8.5 重建基线
    // 的会话对位）：编辑基线前移到新 tip。与 noteAppliedRevision 的差异：
    // 编辑记录不清零（撤销/重做消费的是修订历史，未应用的域编辑仍归
    // 用户处置——草稿继续以新基线编辑，再应用不误报 Stale）。
    m_session.baseRevision = newTip;
    // 应用侧快照失效（UI-T41——撤销/重做后的修订内容本模块未知：预览页/
    // diff 基线以空态诚实呈现，不虚构内容；再次应用即重新定格）。
    m_session.baselineSnapshot.reset();
    m_session.appliedPreview.reset();
    recomputeReadiness();
}

// =====================================================================
// 会话锚定与应用回执（T03b-2——apply 网关的模块半区）
// =====================================================================

void ModelingUiModule::bindSessionAnchor(const core::BranchId& branch,
                                         const core::RevisionId& base)
{
    m_guard.assertOnUiThread();
    m_session.branch = branch;
    m_session.baseRevision = base;
    // 锚定后重算＋重投影（新会话上下文——呈现与信封组装同源）。
    recomputeReadiness();
}

void ModelingUiModule::noteAppliedRevision(
    const core::RevisionId& newBase,
    const std::optional<core::ObjectId>& rootObjectId)
{
    m_guard.assertOnUiThread();
    // 应用成功（§8.5 onCommandResult Committed 的域侧对账）：编辑基线前移
    // （Stale 判据锚更新）；根对象身份回填（下一轮 apply 走"既有对象替换"
    // ——allocateNew 不再重复分配）；已应用的编辑记录清零（changes 是
    // "未应用编辑"的账面——应用即消费）。
    m_session.baseRevision = newBase;
    if (rootObjectId.has_value()) {
        m_session.draft.rootObjectId = rootObjectId;
    }
    m_session.draft.changes.clear();
    // 应用侧快照定格（UI-T41——此时草稿内容＝已应用修订内容）：diff-baseline
    // 的 baseline 侧与预览页 AppliedRevisionView 同源（D-MDL-10——预览仅
    // 呈现已应用修订；撤销/重做事件使快照失效，见 onRevisionCommitted）。
    m_session.baselineSnapshot = m_session.draft;
    {
        AppliedRevisionView view;
        view.revision = newBase;
        view.summaryText =
            "已应用修订快照：关节 " + std::to_string(m_session.draft.design.joints.size())
            + " 个、连杆 " + std::to_string(m_session.draft.design.links.size())
            + " 个（内容定格于应用时刻）";
        m_session.appliedPreview = std::move(view);
    }
    recomputeReadiness();  // 应用后基线前移——就绪状态同步刷新
}

// =====================================================================
// 草稿负载 ASCII 铠装（T03b-1——DraftDocument.payload 契约要求 UTF-8；
// RobotDesign canonical 字节为二进制，hex 编码满足文本承载。与 demo 工具
// Demo6R.cpp 使用同一算法——两端一致性由恢复链路互测保证；如需更换铠
// 装，两处同步修改）。
// =====================================================================

std::string modelingHexEncode(const modeling::Bytes& bytes)
{
    static const char* kDigits = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (const std::uint8_t b : bytes) {
        out.push_back(kDigits[b >> 4]);
        out.push_back(kDigits[b & 0xF]);
    }
    return out;
}

std::optional<modeling::Bytes> modelingHexDecode(const std::string& text)
{
    if (text.size() % 2 != 0) {
        return std::nullopt;
    }
    modeling::Bytes out;
    out.reserve(text.size() / 2);
    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') { return c - '0'; }
        if (c >= 'a' && c <= 'f') { return c - 'a' + 10; }
        if (c >= 'A' && c <= 'F') { return c - 'A' + 10; }
        return -1;
    };
    for (std::size_t i = 0; i < text.size(); i += 2) {
        const int hi = nibble(text[i]);
        const int lo = nibble(text[i + 1]);
        if (hi < 0 || lo < 0) {
            return std::nullopt;
        }
        out.push_back(static_cast<std::uint8_t>((hi << 4) | lo));
    }
    return out;
}

// =====================================================================
// 模块草稿源（T03b-1——ui::IModuleDraftSource 四方法；§10.5）
// =====================================================================

std::string ModelingUiModule::displayName() const
{
    // 工程用语（UX-02）——与 UiText 键族 stage.modeling.title 同词的呈现值；
    // 模块侧显示名注册时刻定格（§10.5），不经文案键二次解析（少一层装配
    // 依赖，值同源不变）。
    return "建模";
}

ui::DraftDocumentProjection ModelingUiModule::buildDraftDocument() const
{
    m_guard.assertOnUiThread();
    // 域负载＝根对象 canonical 字节（与命令 payload 同源同编码——Codec 单
    // 一权威；归属三元组 projectId/branchId/时刻/origin 由控制器在落盘
    // 分派时刻补齐，§8.1 分工红线：域侧只产域负载）。
    ui::DraftDocumentProjection document;
    document.schemaVersion = kCommandPayloadVersion;
    document.moduleId = kModuleHandle;
    document.baseRevisionId = m_session.baseRevision.value_or(core::RevisionId{});
    RobotDesignCodec codec;
    const auto encoded =
        codec.encode(ObjectVariant{m_session.draft.design}, kCurrentFormatVersion);
    // 编码失败＝工作集状态违约（合法草稿必可编码——CodecTest 已钉）；取值
    // 违约抛 logic_error fail-fast，不落半截负载。
    const auto& bytes = encoded.get();
    document.payload = modelingHexEncode(bytes);  // ASCII 铠装（payload 契约 UTF-8）
    return document;
}

void ModelingUiModule::adoptRestoredDocument(
    const ui::DraftDocumentProjection& document)
{
    m_guard.assertOnUiThread();
    // 恢复语义（§8.3-5）：磁盘草稿内容交回域侧。先解 hex 铠装再解 canonical；
    // 解码失败（版本不符/字节损坏）＝保持当前草稿不中断打开——诚实降级，
    // 不渲染半解析状态；恢复结果的完整可用性随收口任务的就绪重估呈现。
    const auto bytes = modelingHexDecode(document.payload);
    if (!bytes.has_value() || bytes->empty()) {
        return;  // 非本铠装形态（空/损坏）——保持现状
    }
    try {
        const RobotDesignCodec codec;
        const auto decoded = codec.decode(*bytes, kCurrentFormatVersion);
        const auto* design = std::get_if<RobotDesign>(&decoded.get());
        if (design == nullptr) {
            return;  // 非根对象负载——恢复面约定外，保持现状
        }
        m_session.draft.design = *design;
        m_session.draft.changes.clear();  // 恢复的草稿不带编辑史（§8.6 表处置）
    } catch (const std::exception&) {
        // 解码失败＝保持现状（文件头诚实边界；打开协议不因域负载损坏中断）
    }
}

void ModelingUiModule::rebuildOnRevision(const std::string& tipRevisionCanonical)
{
    m_guard.assertOnUiThread();
    // §8.5[基于当前版本重新编辑]的域侧半区：编辑基线改写为分支 tip。
    // 非法 canonical＝调用方违约（控制器只传合法 tip）——解析失败忽略
    // （基线不变，不虚构已重建）。
    const auto tip =
        core::RevisionId::tryFromCanonical(tipRevisionCanonical);
    if (tip.has_value()) {
        m_session.baseRevision = *tip;
    }
}

}  // namespace sdurws::ird::modeling
