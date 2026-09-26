/**
 * @file   ModelingUiModule.cpp
 * @brief  建模插件界面模块实现——§11.2 buildDraftCommand 的域侧组装。
 *
 * 设计依据：units/ui.md §11.2/§8.5、modeling.md §9.7.2 L-3/§9.3/§6.4；
 * 契约 WP-13-T15 acceptance 3/4。实现纪律：信封组装＝数据搬运＋确定性
 * 编码（合法性判定零参与——解码门/断言分域在命令 prepare 现场重估）。
 */

#include "ModelingUiModule.hpp"

#include "ModelingPanelWidget.hpp"

#include <stdexcept>

#include <sdurws/ird/modeling/Template.hpp>  // RobotDesignTemplateFactory/kTemplateIdGeneric6R（首版装配会话种子——真实域路径）

// =====================================================================
// 草稿阶段名称上下文（T03b-2b 诚实边界）
// =====================================================================
// runtime 名在编译前不存在（NameMap 随确定性编译产生——ARC-03），草稿桩
// 恒 nullopt：行程评估在无已装载策略时本就不进入（L11 如实产出"策略不
// 可解析"Blocking），桩不被消费即不撒谎。策略装载＋runtime 名称适配随
// 收口批次接线。
namespace {
class DraftStageNameContext final : public sdurws::ird::policy::IPolicyNameContext {
public:
    std::optional<sdurws::ird::core::ObjectId> tryObjectId(const std::string&) const override
    {
        return std::nullopt;
    }
    std::optional<std::string> tryRuntimeName(sdurws::ird::core::ObjectId) const override
    {
        return std::nullopt;
    }
    sdurws::ird::core::ContentIdentity nameMapContentIdentity() const override
    {
        // 全零保留值＝"无名称映射"（草稿阶段 runtime 名不存在——见上注）；
        // 真身由 runtime ⑥端口计算（CON-06）。
        return sdurws::ird::core::ContentIdentity{};
    }
};
}  // namespace
#include <sdurws/ird/modeling/Codec.hpp>       // RobotDesignCodec/kCurrentFormatVersion（根对象确定性编码——§4.8）
#include <sdurws/ird/modeling/ObjectTypes.hpp> // kRobotDesignObjectType（根对象 token——runtime 单一权威的 using 重导出）

namespace sdurws::ird::modeling {

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
    : m_nameContext(std::make_unique<DraftStageNameContext>())
{
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

void ModelingUiModule::bindTextResolver(std::function<QString(const std::string&)> resolve)
{
    m_guard.assertOnUiThread();
    if (m_panel != nullptr) {
        m_panel->setCommandTitleResolver(std::move(resolve));
        return;
    }
    m_textResolver = std::move(resolve);
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
    m_session.draft = outcome.get();
    // baseRevision 留空（nullopt＝提交期解析 tip——§6.2）；种子后就绪
    // 真判定即刻重算（T03b-2b——就绪条不再空态，如实呈现阻塞/缺项）。
    recomputeReadiness();
}

QWidget* ModelingUiModule::createPanel()
{
    m_guard.assertOnUiThread();
    // 面板与接线一次完成：可写初值 true（L-7 门控）；编辑目标提供器现取
    // 会话权威工作集（ACC5 零缓存——提供器每次经 session() 入口取指针）。
    auto* panel = new ModelingPanelWidget(true);
    panel->setEditTargetProvider([this]() -> ModelingWorkingSet* {
        return &m_session.draft;
    });
    if (m_pendingSubmit) {
        panel->setCommandSubmit(std::move(m_pendingSubmit));
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

void ModelingUiModule::recomputeReadiness()
{
    m_guard.assertOnUiThread();
    // 真判定（T03b-2b）：真实 ModelReadinessChecker＋policy 行程评估器。
    // 套件/检查器须为具名局部量（checker 持套件指针——临时量悬挂风险）；
    // CheckContext 缺省＝草稿阶段无已装载策略（L11 如实产出 Blocking——
    // 携带行程校验未执行的事实），策略装载随收口批次。
    const AssertionSuite::Ports ports{m_evaluator.get(), m_nameContext.get()};
    const AssertionSuite suite{ports};
    const ModelReadinessChecker checker{suite};
    m_session.readiness = checker.check(m_session.draft, CheckContext{});
    if (m_panel != nullptr) {
        m_panel->refreshPanel(m_session.draft, *m_session.readiness);
    }
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
    recomputeReadiness();  // 应用后基线前移——就绪状态同步刷新
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
    document.payload.assign(bytes.begin(), bytes.end());
    return document;
}

void ModelingUiModule::adoptRestoredDocument(
    const ui::DraftDocumentProjection& document)
{
    m_guard.assertOnUiThread();
    // 恢复语义（§8.3-5）：磁盘草稿内容交回域侧。解码失败（版本不符/字节
    // 损坏）＝保持当前草稿不中断打开——诚实降级，不渲染半解析状态；恢复
    // 结果的完整可用性随收口任务的就绪重估呈现。
    try {
        const RobotDesignCodec codec;
        const Bytes bytes(document.payload.begin(), document.payload.end());
        const auto decoded =
            codec.decode(bytes, kCurrentFormatVersion);
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
