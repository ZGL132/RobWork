/**
 * @file   RequirementsUiModule.cpp
 * @brief  需求插件界面模块实现——§11.2 buildDraftCommand 域侧组装＋草稿源
 *         四方法（P-REQ-4 冻结面）。
 *
 * 设计依据：units/ui.md §11.2/§8.5/§10.5、requirements.md §9.8 L-R3/§9.1；
 * 契约 WP-14-T08 acceptance 2/4。实现纪律：信封/文档组装＝数据搬运＋确定性
 * 编码（全部经域公共接口——RequirementCodec/encodeRequirementCommandPayload
 * 单一实现，插件零编码算法）；合法性判定零参与（解码门/就绪断言分域在
 * 命令 prepare 现场重估——P-REQ-6：ui 侧仅呈现拒绝诊断）。
 */

#include "RequirementsUiModule.hpp"

#include <stdexcept>
#include <utility>

#include <sdurws/ird/requirements/Codec.hpp>            // RequirementCodec/kCurrentRequirementFormatVersion（五对象确定性编码）
#include <sdurws/ird/requirements/CommandHandlers.hpp>  // kCmdApplyRequirementSet/kRequirementCommandPayloadVersion/encode·decodeRequirementCommandPayload（命令载荷权威）
#include <sdurws/ird/requirements/ObjectTypes.hpp>      // 五对象 token（槽路由）
#include "RequirementsPanelWidget.hpp"                  // 面板类型（attachPanel 弱语义引用的消费面说明）

namespace sdurws::ird::requirements {
namespace {

/// 模块注册键（§11.2 moduleId 参数形态＋ModuleDraftHandle 词表——单一书写点）。
constexpr const char* kModuleId = "requirements";

// ---- 内存闭包视图（草稿恢复的基线重建源——RequirementObjectClosureView
//      的模块内最小实现；装载解码产物，闭包域纪律＝只应答恢复文档内对象）。
class RestoredClosureView final : public RequirementObjectClosureView {
public:
    /// 按 token 索引（根/集合对象路由——loadBaseline 的 tryObjectByToken 面）。
    std::vector<RequirementClosureObject> byToken;
    /// 按 oid 索引（根引用表解引用——tryObject 面）。
    std::vector<std::pair<core::ObjectId, RequirementClosureObject>> byId;

    std::optional<RequirementClosureObject> tryObjectByToken(
        std::string_view objectTypeToken) const override
    {
        // 线性扫描＝登记序（恢复文档槽序——确定性；对象数 ≤5，无需索引）。
        for (const RequirementClosureObject& o : byToken) {
            if (o.objectTypeToken == objectTypeToken) {
                return o;
            }
        }
        return std::nullopt;
    }

    std::optional<RequirementClosureObject> tryObject(const core::ObjectId& oid) const override
    {
        for (const auto& entry : byId) {
            if (entry.first == oid) {
                return entry.second;
            }
        }
        return std::nullopt;
    }
};

// 单对象解码（槽字节→五变体——解码闸口唯一经 RequirementCodec；失败＝
// 草稿文件级数据缺陷，fail-fast 不吞）。
RequirementObjectVariant decodeSlotObject(const RequirementPayloadSlot& slot,
                                          const RequirementCodec& codec)
{
    const auto decoded = codec.decode(slot.objectBytes, kCurrentRequirementFormatVersion);
    if (!decoded.ok()) {
        throw std::runtime_error("requirements 草稿：对象解码失败（"
                                 + decoded.error().detail + "）——不虚构可用工作集");
    }
    return std::move(decoded.get());
}

}  // namespace

void RequirementsUiModule::bindCommandSubmit(CommandSubmitFn submitFn)
{
    m_guard.assertOnUiThread();
    if (m_panel != nullptr) {
        m_panel->setCommandSubmit(std::move(submitFn));
        return;
    }
    m_pendingSubmit = std::move(submitFn);
}

void RequirementsUiModule::bindCommandAvailability(CommandAvailabilityFn availability)
{
    m_guard.assertOnUiThread();
    if (m_panel != nullptr) {
        m_panel->setCommandAvailability(std::move(availability));
        return;
    }
    m_pendingAvailability = std::move(availability);
}

void RequirementsUiModule::setPostEditAction(PostEditAction action)
{
    m_guard.assertOnUiThread();
    m_hostPostEdit = std::move(action);
    wirePanelPostEdit();
}

void RequirementsUiModule::setWritable(bool writable)
{
    // L-R12 门控输入转发（UI-T39——宿主按打开报告的真实 writable 驱动；
    // 本转发面零判定，门控事实源＝ui 只读横幅同源的宿主报告）。
    // 只读初始化修复：面板缺位（Dock 尚未首次创建）不再丢弃该态——缓存
    // 本模块（与 bindCommandSubmit 面板前暂存同一时序语义），由装配工厂
    // 经 initialWritable() 作面板构造初值＋attachPanel 回放兜底；否则
    // "只读项目先开、需求 Dock 后开"装配序下面板恒按可写创建（L-R12 违约）。
    m_guard.assertOnUiThread();
    m_writable = writable;
    if (m_panel != nullptr) {
        m_panel->setWritable(writable);
    }
}

void RequirementsUiModule::refreshFromSession()
{
    // 会话刷新呈现收口（UI-T39——宿主在 bindReadiness 后调用；面板以
    // 会话态最新报告全面板重投影。面板缺位＝装配间隙，空操作不虚构）。
    m_guard.assertOnUiThread();
    if (m_panel == nullptr || m_editor == nullptr
        || !m_session.readiness.has_value()) {
        return;
    }
    m_panel->refreshPanel(m_editor->workingSet(), *m_session.readiness);
}

void RequirementsUiModule::noteBaselineReloaded()
{
    // 基线重载登记（UI-T39——编辑器栈随 loadBaseline 复位，撤销记账同步
    // 归零后刷新撤销键；面板缺位＝空操作）。
    m_guard.assertOnUiThread();
    if (m_panel != nullptr) {
        m_panel->noteBaselineReloaded();
    }
}

void RequirementsUiModule::noteAppliedRevision(
    const core::RevisionId& newBase,
    const std::optional<core::ObjectId>& rootObjectId)
{
    m_guard.assertOnUiThread();

    // ①会话态回填（原门面直写三字段——语义原样收编进模块方法，与
    //   modeling 门面 noteAppliedRevision 的"模块级回执"同构）：基线前移
    //   （下一轮信封 expectedRevision＝新 tip）、根对象身份回填（下一轮
    //   根槽走"既有对象替换"——allocateNew 不再重复建根，恰一根不变量）、
    //   恢复草稿资格位清位（应用即消费——RequirementsModuleSessionState
    //   字段注释的装配层动作落点随转发链到达此处）。
    m_session.baseRevision = newBase;
    m_session.rootObjectId = rootObjectId;
    m_session.restoredDraftPending = false;

    // ②工作集根引用表回填（F-460 主线——缺陷机理：首应用回执只回填会话
    //   态，编辑器工作集根引用表四槽仍停留在首应用前的"未挂载"态；第二
    //   次 draft.apply 组装时集合槽按 allocateNew 取号，与存储端已挂载事
    //   实失配，被命令 prepare 挂载核对拒绝——需求域二连 draft.apply 必
    //   拒。回填归属：refs 的权威载体是编辑器工作集（§4.2），故修正落在
    //   载体内，不在会话态另设第二份根引用表副本——结构性防第二真值）。
    rewireWorksetRootRefsAfterApply();
}

void RequirementsUiModule::rewireWorksetRootRefsAfterApply()
{
    // ---- 前置守卫（诚实降级面——两态，零虚构零剥蚀）----
    // 编辑器未注入＝装配间隙（无会话即无工作集——零回填对象）；三维缝
    // 未绑定或缝内闭包引用元数据缺省＝宿主装配降级形态（UI-T33 缺省＝
    // 诚实降级语义）——四集合落库身份无从读取，跳过回填。此降级态下二
    // 连应用维持修复前的拒绝行为（不 worse），且不抛：缝缺位是装配事实，
    // 不是数据缺陷。
    if (m_editor == nullptr) {
        return;
    }
    if (!m_view3dSeamsBound || !m_view3dSeams.sessionClosureRefs) {
        return;
    }

    // ---- 会话当前基线的根引用表读取 ----
    // 数据来源＝宿主绑定的会话闭包引用元数据缝（sessionClosureRefs——
    // 生产绑定＝查询端口 head().objectRefs，F-558 修复后延迟现取：绑定
    // 时机无关）。回执时刻 HEAD＝本次应用落库的新修订，objectRefs 即
    // "应用后修订中五对象（根＋四集合）的实际 ObjectId"——正是回填内容
    // 所需。域内既有同源消费先例：RequirementsCommandFlows 拾取写回的
    // CheckContext 组装（本缝即"会话闭包引用元数据"的现取事实源）。
    const std::vector<project::ObjectRef> closureRefs =
        m_view3dSeams.sessionClosureRefs();
    if (closureRefs.empty()) {
        // 空闭包（无打开项目等）＝无核对基准——跳过：拿空表当权威会误
        // 剥蚀工作集既有挂载（防剥蚀优先于回填）。
        return;
    }

    // 按 token 路由四集合落库身份（对象类型 token 是修订视图内对象种类
    // 的唯一路由键——与 buildDraftCommand 槽路由、宿主根扫描同源）。
    auto refOfToken = [&closureRefs](std::string_view token)
        -> std::optional<core::ObjectId> {
        for (const project::ObjectRef& ref : closureRefs) {
            if (ref.objectTypeToken == token) {
                return ref.objectId;
            }
        }
        return std::nullopt;
    };
    const std::optional<core::ObjectId> appliedPointsRef =
        refOfToken(kReqPointSetObjectType);
    const std::optional<core::ObjectId> appliedRegionsRef =
        refOfToken(kReqRegionSetObjectType);
    const std::optional<core::ObjectId> appliedConditionsRef =
        refOfToken(kReqConditionSetObjectType);
    const std::optional<core::ObjectId> appliedPlansRef =
        refOfToken(kReqPlanSetObjectType);

    // ---- 一致性门（免重建）----
    // 四槽逐一比对：全一致＝工作集挂载已与落库修订同态（宿主重导线路径
    // 的回执、第二次及以后的回执——refs 不随内容替换变化），零重建返回；
    // 存在"工作集未挂载（或挂载他值）而落库已挂载"的缺口＝F-460 失配态，
    // 进入回填。方向性说明：只对"闭包有身份"的槽判失配——闭包缺席（集
    // 合对象未落库）而工作集有挂载的组合属外部修订删除场景，归 Rewire
    // FromHead 重导线语义（RevisionSyncPolicy 三分岔），不在本回执回填
    // 职责内（回填只升级、不剥蚀）。
    const RequirementWorkingSet& ws = m_editor->workingSet();
    const bool needsRewire =
        (appliedPointsRef.has_value() && ws.root.pointSetRef != appliedPointsRef)
        || (appliedRegionsRef.has_value()
            && ws.root.regionSetRef != appliedRegionsRef)
        || (appliedConditionsRef.has_value()
            && ws.root.conditionSetRef != appliedConditionsRef)
        || (appliedPlansRef.has_value() && ws.root.planSetRef != appliedPlansRef);
    if (!needsRewire) {
        return;
    }

    // ---- 回填闭包装配（既有 ws 更新入口的输入面）----
    // 字节源＝编辑器当前工作集自身（同一内容刚在命令提交时刻通过编码与
    // 校验链；重编码确定性 NFR-COR-02）。编码失败＝模型非法（schema 违
    // 约），fail-fast 不吞——不产出残缺闭包冒充回填基线。
    const RequirementCodec codec;
    auto encodeOrThrow = [&codec](const RequirementObjectVariant& object) {
        const auto encoded = codec.encode(object, kCurrentRequirementFormatVersion);
        if (!encoded.ok()) {
            throw std::runtime_error(
                "requirements 应用回执：工作集回填闭包编码失败（"
                + encoded.error().detail + "）——不虚构可用工作集");
        }
        return encoded.get();
    };

    // 候选根＝工作集根副本＋四槽身份升级（只升级不剥蚀：闭包缺席的槽保
    // 持工作集现值——理由同一致性门的方向性说明）。挂载增量的权威对齐：
    // 落库修订事实在此刻覆盖工作集旧值，工作集仍是根引用表的唯一权威载
    // 体（PA-1——修正载体内的值，不另设第二真值）。
    RequirementSet patchedRoot = ws.root;
    if (appliedPointsRef.has_value()) {
        patchedRoot.pointSetRef = appliedPointsRef;
    }
    if (appliedRegionsRef.has_value()) {
        patchedRoot.regionSetRef = appliedRegionsRef;
    }
    if (appliedConditionsRef.has_value()) {
        patchedRoot.conditionSetRef = appliedConditionsRef;
    }
    if (appliedPlansRef.has_value()) {
        patchedRoot.planSetRef = appliedPlansRef;
    }

    // 内存闭包视图（本 TU 匿名域 RestoredClosureView 同形复用——byToken
    // 路由根＋四集合、byId 承载根引用表解引用）。集合 oid 取"落库身份优
    // 先、工作集现值兜底"；双缺席槽不登记——引用缺席＝空集合对象未建
    // （loadBaseline 合法跳过该槽，工作集槽位保持默认空——与首应用前该
    // 集合本就为空的语义一致）。
    RestoredClosureView closure;
    // 聚合类型初始化沿用 adoptRestoredDocument 既有写法（push_back＋大括
    // 号——RequirementClosureObject 无双参构造，emplace 转发不可用）。
    closure.byToken.push_back(
        RequirementClosureObject{std::string(kReqSetObjectType),
                                 encodeOrThrow(RequirementObjectVariant{patchedRoot})});
    auto pushSet = [&](std::string_view token,
                       const std::optional<core::ObjectId>& appliedRef,
                       const std::optional<core::ObjectId>& wsRef,
                       const RequirementObjectVariant& object) {
        const std::vector<std::uint8_t> bytes = encodeOrThrow(object);
        closure.byToken.push_back(
            RequirementClosureObject{std::string(token), bytes});
        const std::optional<core::ObjectId>& oid =
            appliedRef.has_value() ? appliedRef : wsRef;
        if (oid.has_value()) {
            closure.byId.emplace_back(
                oid.value(), RequirementClosureObject{std::string(token), bytes});
        }
    };
    pushSet(kReqPointSetObjectType, appliedPointsRef, ws.root.pointSetRef,
            RequirementObjectVariant{ws.points});
    pushSet(kReqRegionSetObjectType, appliedRegionsRef, ws.root.regionSetRef,
            RequirementObjectVariant{ws.regions});
    pushSet(kReqConditionSetObjectType, appliedConditionsRef,
            ws.root.conditionSetRef, RequirementObjectVariant{ws.conditions});
    pushSet(kReqPlanSetObjectType, appliedPlansRef, ws.root.planSetRef,
            RequirementObjectVariant{ws.plans});

    // ---- 经既有 ws 更新入口重建（loadBaseline——唯一能改根引用表的公
    //      开入口；工作集/撤销栈/编辑计数重置为基线态）----
    // 为什么栈清零可接受：应用即消费（modeling 回执清 changes 账面同构）
    // ——已应用编辑的撤销归项目级"撤销上次应用"，局部栈的历史使命随修
    // 订落库终结；编辑计数归零同时修正"应用后无新编辑仍可组装同内容空
    // 修订"的边缘（buildDraftCommand 门随 edits==0 如实 nullopt）。快照
    // 内容安全性：回填闭包取自当前工作集，重建前后五对象内容逐字节一致
    // （仅根引用表身份升级），无任何用户内容丢失。
    const RequirementLoadOutcome load = m_editor->loadBaseline(closure);
    if (!load.ok) {
        // 失败＝闭包违约（字节源来自同一工作集，正常流不可达）——按实
        // 现/数据缺陷 fail-fast：会话与存储已失配时宁可崩溃暴露，不允许
        // 静默停留在失配态（loadBaseline 失败时编辑器保持原状，回执语义
        // 其余半区不受影响——上抛交宿主装配层处置）。
        throw std::runtime_error("requirements 应用回执：工作集根引用表回填失败（"
                                 + load.error.detail + "）——不吞基线级失败");
    }

    // 撤销记账随栈复位归零（面板撤销键同步——漏登记＝"幽灵可撤销"）。
    // 面板内容刷新不在此处：回填只改根引用表身份、内容与回执前一致，宿
    // 主 SkipSelfApplied/committed 收口的 refreshRequirementsFromSession
    // 随后覆盖呈现面（事件驱动刷新的应答半区归宿主编排——PA-1）。
    noteBaselineReloaded();
}

void RequirementsUiModule::resetPanelForDetach()
{
    // 会话脱离的面板复位转发（UI-T39——项目关闭/切换的旧态清理）。
    m_guard.assertOnUiThread();
    if (m_panel != nullptr) {
        m_panel->resetForSessionDetached();
    }
}

void RequirementsUiModule::wirePanelPostEdit()
{
    if (m_panel == nullptr) {
        return;  // 面板未创建——宿主动作已暂存，attachPanel 时组合子补挂
    }
    // 组合子（UI-T29 最小校验的模块承载）：宿主重估（bindReadiness→会话
    // 态刷新）先行，随后以会话最新报告 refreshPanel——校验页由『尚未执行』
    // 静态实时化为编辑态即时预检的呈现收口（判定权威在域侧 checker——
    // P-REQ-6 边界不变，本组合子零判定只做编排）。
    m_panel->setPostEditAction([this]() {
        if (m_hostPostEdit) {
            m_hostPostEdit();
        }
        if (m_editor != nullptr && m_session.readiness.has_value()) {
            m_panel->refreshPanel(m_editor->workingSet(), *m_session.readiness);
        }
    });
}

void RequirementsUiModule::attachPanel(RequirementsPanelWidget* panel)
{
    m_panel = panel;
    if (m_panel != nullptr && m_pendingSubmit) {
        m_panel->setCommandSubmit(std::move(m_pendingSubmit));
    }
    if (m_panel != nullptr && m_pendingAvailability) {
        m_panel->setCommandAvailability(std::move(m_pendingAvailability));
    }
    if (m_panel != nullptr && m_pendingMarkersSink) {
        m_panel->setStationMarkersSink(m_pendingMarkersSink);  // UI-T33——预览/标记双暂存的创建后应用
    }
    if (m_panel != nullptr && m_pendingRegionSink) {
        m_panel->setRegionPreviewSink(m_pendingRegionSink);
    }
    // 可写性缓存回放（只读初始化修复——工厂构造初值经 initialWritable()
    // 已对齐，此处幂等再拉齐一次：即便面板以其他路径构造/构造与挂接之间
    // 又有 setWritable 到达，attachPanel 完成后面板必等于模块缓存态）。
    if (m_panel != nullptr) {
        m_panel->setWritable(m_writable);
    }
    wirePanelPostEdit();
}

// =====================================================================
// §11.2 三方法
// =====================================================================

std::vector<ui::DomainReadinessItem> RequirementsUiModule::readonlyProjections() const
{
    // 无报告（会话未校验）＝输入不完整空态行（零判定——默认值面；判定
    // 权威在 IRequirementReadinessChecker，报告就位后直投）。
    if (!m_session.readiness.has_value()) {
        ui::DomainReadinessItem empty;
        empty.domainKey = "requirements";
        empty.verdict = core::EngineeringStatus::DataInsufficient;
        empty.inputComplete = false;
        return {empty};
    }
    return requirementsReadinessProjection(*m_session.readiness);
}

std::optional<project::CommandEnvelope> RequirementsUiModule::buildDraftCommand(
    const std::string& moduleId)
{
    m_guard.assertOnUiThread();  // §3.4——命令组装读取会话权威态与工作集

    // ①域外请求（§11.2 参数语义：moduleId＝域注册键）。
    if (moduleId != kModuleId) {
        return std::nullopt;
    }

    // ②可应用资格（§8.5"无草稿可应用"态——不产生空修订）：编辑器未注入＝
    //   装配未就绪（nullopt——不虚构）；自基线零编辑且无恢复草稿＝无变更。
    if (m_editor == nullptr) {
        return std::nullopt;
    }
    const RequirementDraftStatus status = m_editor->draftStatus();
    if (status.edits == 0 && !m_session.restoredDraftPending) {
        return std::nullopt;
    }

    // ③五对象确定性编码（RequirementCodec 单一实现——§4.8 canonical 序列
    //   化；同工作集重复组装同字节，NFR-COR-02）。编码失败＝模型非法
    //   （schema 违约），属实现/上游缺陷——fail-fast 上抛，不产出畸形信封。
    const RequirementCodec codec;
    const RequirementWorkingSet& ws = m_editor->workingSet();
    auto encodeOrThrow = [&codec](const RequirementObjectVariant& object) {
        const auto encoded = codec.encode(object, kCurrentRequirementFormatVersion);
        if (!encoded.ok()) {
            throw std::runtime_error("requirements 面板：草稿编码失败（"
                                     + encoded.error().detail + "）——不产出畸形命令信封");
        }
        return encoded.get();
    };

    // ④五对象槽（§9.1 槽形状语义——Apply 模式）：
    //   根槽：会话根身份未回填＝首次应用（allocateNew——prepare 取号回填，
    //         PA-1 身份分配唯一归 project）；已回填＝既有对象替换。
    //   集合槽：allocateNew＝根引用表该槽未挂载（根 refs 是集合 oid 的
    //         权威——§4.2 字段表；挂载态取 refs 值直写）。
    RequirementCommandPayload payload;
    payload.mode = RequirementCommandPayload::Mode::Apply;

    RequirementPayloadSlot rootSlot;
    rootSlot.allocateNew = !m_session.rootObjectId.has_value();
    rootSlot.objectId = m_session.rootObjectId.value_or(core::ObjectId{});
    rootSlot.objectTypeToken = std::string(kReqSetObjectType);
    rootSlot.objectBytes = encodeOrThrow(RequirementObjectVariant{ws.root});
    payload.objects.push_back(std::move(rootSlot));

    // 四集合槽（槽序＝§4.1 表行序——确定性处理序）。
    auto pushSetSlot = [&](const std::string_view token,
                           const std::optional<core::ObjectId>& mountedRef,
                           const RequirementObjectVariant& object) {
        RequirementPayloadSlot slot;
        slot.allocateNew = !mountedRef.has_value();   // 未挂载＝首挂载（取号）
        slot.objectId = mountedRef.value_or(core::ObjectId{});
        slot.objectTypeToken = std::string(token);
        slot.objectBytes = encodeOrThrow(object);
        payload.objects.push_back(std::move(slot));
    };
    pushSetSlot(kReqPointSetObjectType, ws.root.pointSetRef,
                RequirementObjectVariant{ws.points});
    pushSetSlot(kReqRegionSetObjectType, ws.root.regionSetRef,
                RequirementObjectVariant{ws.regions});
    pushSetSlot(kReqConditionSetObjectType, ws.root.conditionSetRef,
                RequirementObjectVariant{ws.conditions});
    pushSetSlot(kReqPlanSetObjectType, ws.root.planSetRef, RequirementObjectVariant{ws.plans});

    // ⑤信封组装（§6.4 版本三元组：commandType＋payloadFormatVersion＋
    //   负载字节；expectedRevision=会话基线，nullopt＝提交期解析 tip）。
    //   ★ P-REQ-6 边界：本组装零就绪判定——Blocking 的现场重估唯一在命令
    //   prepare（就绪报告只进 readonlyProjections/校验面板呈现面）。
    project::CommandEnvelope envelope;
    envelope.branch = m_session.branch;
    envelope.expectedRevision = m_session.baseRevision;
    envelope.commandType = std::string(kCmdApplyRequirementSet);  // project 词表 token（无点——O-35）
    envelope.payloadFormatVersion = kRequirementCommandPayloadVersion;
    envelope.payloadCanonical = encodeRequirementCommandPayload(payload);

    // ⑥面板引用消费说明：组装路径不触面板（面板是呈现半区——命令组装的
    //   权威输入只有会话态与工作集；m_panel 仅供联动刷新，显式消费避免
    //   "未使用成员"误读）。
    (void)m_panel;
    return envelope;
}

// =====================================================================
// 草稿源四方法（ui.md §10.5 冻结面——P-REQ-4）
// =====================================================================

std::string RequirementsDraftSource::displayName() const
{
    return "任务需求";  // 工程用语固定词（UX-02——零哈希/内部名）
}

ui::DraftDocumentProjection RequirementsDraftSource::buildDraftDocument() const
{
    // 前置：编辑器未载入基线＝装配违约（控制器不会对未载入模块落盘——
    // 到达即 fail-fast，不产出空文档冒充草稿）。
    const RequirementDraftStatus status = m_editor.draftStatus();
    if (status.baseRevisionId.empty() && status.edits == 0 && !m_module.session().restoredDraftPending) {
        // 宽松面：未载入（baseRevisionId 空）且零编辑且无恢复草稿——按未
        // 载入处置（基线未标注是常态——Capture.hpp 空串口径；零编辑＋无
        // 恢复位说明工作集从未就绪）。
        if (m_editor.workingSet().root.name.empty() && m_editor.workingSet().points.entries.empty()
            && m_editor.workingSet().regions.entries.empty()
            && m_editor.workingSet().conditions.entries.empty()
            && m_editor.workingSet().plans.entries.empty()) {
            throw std::logic_error("requirements 草稿源：编辑器未载入基线即请求落盘（装配违约）");
        }
    }

    // 载荷＝五对象 Apply 载荷（与 draft.apply 命令同构同版本——一套编码
    // 两处消费；编码确定性 NFR-COR-02）。编码失败＝模型非法，fail-fast。
    const RequirementCodec codec;
    const RequirementWorkingSet& ws = m_editor.workingSet();
    auto encodeOrThrow = [&codec](const RequirementObjectVariant& object) {
        const auto encoded = codec.encode(object, kCurrentRequirementFormatVersion);
        if (!encoded.ok()) {
            throw std::runtime_error("requirements 草稿源：草稿编码失败（"
                                     + encoded.error().detail + "）——不产出畸形文档");
        }
        return encoded.get();
    };

    // 根槽 allocateNew 恒按会话根身份（未回填＝首次应用形态——语义与
    // buildDraftCommand 槽④一致）；集合槽按根引用表挂载态。
    RequirementCommandPayload payload;
    payload.mode = RequirementCommandPayload::Mode::Apply;
    const RequirementsModuleSessionState& session = m_module.session();
    RequirementPayloadSlot rootSlot;
    rootSlot.allocateNew = !session.rootObjectId.has_value();
    rootSlot.objectId = session.rootObjectId.value_or(core::ObjectId{});
    rootSlot.objectTypeToken = std::string(kReqSetObjectType);
    rootSlot.objectBytes = encodeOrThrow(RequirementObjectVariant{ws.root});
    payload.objects.push_back(std::move(rootSlot));

    auto pushSetSlot = [&](const std::string_view token,
                           const std::optional<core::ObjectId>& mountedRef,
                           const RequirementObjectVariant& object) {
        RequirementPayloadSlot slot;
        slot.allocateNew = !mountedRef.has_value();
        slot.objectId = mountedRef.value_or(core::ObjectId{});
        slot.objectTypeToken = std::string(token);
        slot.objectBytes = encodeOrThrow(object);
        payload.objects.push_back(std::move(slot));
    };
    pushSetSlot(kReqPointSetObjectType, ws.root.pointSetRef, RequirementObjectVariant{ws.points});
    pushSetSlot(kReqRegionSetObjectType, ws.root.regionSetRef, RequirementObjectVariant{ws.regions});
    pushSetSlot(kReqConditionSetObjectType, ws.root.conditionSetRef,
                RequirementObjectVariant{ws.conditions});
    pushSetSlot(kReqPlanSetObjectType, ws.root.planSetRef, RequirementObjectVariant{ws.plans});

    // 文档投影装配（ui 只搬运不解析——CR-02/D-10；savedAtUtc 留空＝控制器
    // 在分派时刻填写，§10.5 契约）。
    const std::vector<std::uint8_t> payloadBytes = encodeRequirementCommandPayload(payload);
    ui::DraftDocumentProjection doc;
    doc.schemaVersion = kRequirementCommandPayloadVersion;  // 域组装方填写（0＝未填保留值——不落盘 0）
    doc.moduleId = kModuleId;
    doc.baseRevisionId =
        core::RevisionId::tryFromCanonical(status.baseRevisionId).value_or(core::RevisionId{});
    doc.payload.assign(payloadBytes.begin(), payloadBytes.end());
    return doc;
}

void RequirementsDraftSource::adoptRestoredDocument(const ui::DraftDocumentProjection& document)
{
    // ①载荷框架解码（try 轨域函数——破损/版本不受理＝nullopt；恢复场景
    //   到达即草稿文件级数据缺陷，fail-fast 不吞：不虚构可用工作集）。
    const std::vector<std::uint8_t> bytes(document.payload.begin(), document.payload.end());
    const auto payload = tryDecodeRequirementCommandPayload(bytes);
    if (!payload.has_value()) {
        throw std::runtime_error("requirements 草稿源：恢复载荷破损或版本不受理——"
                                 "不虚构可用工作集");
    }

    // ②逐槽对象解码（RequirementCodec 解码闸口——四步校验链；失败 fail-fast）。
    const RequirementCodec codec;
    RestoredClosureView view;
    std::optional<RequirementSet> root;
    std::vector<std::pair<std::string_view, RequirementObjectVariant>> sets;
    for (const RequirementPayloadSlot& slot : payload.value().objects) {
        RequirementObjectVariant object = decodeSlotObject(slot, codec);
        if (slot.objectTypeToken == kReqSetObjectType) {
            root = std::get<RequirementSet>(std::move(object));
        } else {
            sets.emplace_back(slot.objectTypeToken, std::move(object));
        }
    }
    if (!root.has_value()) {
        throw std::runtime_error("requirements 草稿源：恢复载荷缺少 req-set 根——不虚构基线");
    }

    // ③闭包装配：按 token 登记＋按 oid 登记（oid 来源＝根引用表——集合
    //   oid 的权威，§4.2；未挂载槽的集合对象在闭包内无 oid 入口，编辑器
    //   解引用语义下不可达——按根引用表完整性核对其余四槽）。
    view.byToken.push_back(RequirementClosureObject{std::string(kReqSetObjectType), {}});
    auto registerBytes = [&](std::string_view token, const RequirementObjectVariant& object) {
        const auto bytesEncoded = codec.encode(object, kCurrentRequirementFormatVersion);
        if (!bytesEncoded.ok()) {
            throw std::runtime_error("requirements 草稿源：恢复对象重编码失败——数据缺陷");
        }
        view.byToken.push_back(RequirementClosureObject{std::string(token), bytesEncoded.get()});
        return bytesEncoded.get();
    };
    // 根字节（byToken 第 0 位补字节——上方占位无字节）。
    {
        const auto rootBytes = codec.encode(RequirementObjectVariant{root.value()},
                                            kCurrentRequirementFormatVersion);
        if (!rootBytes.ok()) {
            throw std::runtime_error("requirements 草稿源：恢复根重编码失败——数据缺陷");
        }
        view.byToken[0].bytes = rootBytes.get();
    }
    for (auto& entry : sets) {
        const std::vector<std::uint8_t> setBytes = registerBytes(entry.first, entry.second);
        // oid 入口：根引用表该槽的挂载值（未挂载＝该集合不在闭包——
        // 首应用前的恢复载荷不完整，交 loadBaseline 的闭包违约拒绝面）。
        std::optional<core::ObjectId> oid;
        if (entry.first == kReqPointSetObjectType) {
            oid = root.value().pointSetRef;
        } else if (entry.first == kReqRegionSetObjectType) {
            oid = root.value().regionSetRef;
        } else if (entry.first == kReqConditionSetObjectType) {
            oid = root.value().conditionSetRef;
        } else if (entry.first == kReqPlanSetObjectType) {
            oid = root.value().planSetRef;
        }
        if (oid.has_value()) {
            view.byId.emplace_back(oid.value(),
                                   RequirementClosureObject{std::string(entry.first), setBytes});
        }
    }

    // ④以恢复内容重建编辑器基线（loadBaseline——工作集/栈/计数重置为
    //   恢复态；失败＝闭包违约，fail-fast 不吞）。
    const RequirementLoadOutcome load = m_editor.loadBaseline(view);
    if (!load.ok) {
        throw std::runtime_error("requirements 草稿源：恢复基线重建失败（"
                                 + load.error.detail + "）");
    }

    // ⑤应用资格位置位（恢复的工作集即待应用草稿——draft.apply 资格随此
    //   位；draft.apply 后由装配层清位）。
    m_module.session().restoredDraftPending = true;
}

void RequirementsDraftSource::rebuildOnRevision(const std::string& tipRevisionCanonical)
{
    // 更新会话基线锚（tip 规范文本→RevisionId；不可解析＝保持 nullopt——
    // 提交期解析 tip 语义，§6.2）。工作集重建由装配层闭包提供器在下一轮
    // 刷新协调器重演中完成（PA-1：闭包权威在装配层——本方法不代取）。
    auto& session = m_module.session();
    session.baseRevision = core::RevisionId::tryFromCanonical(tipRevisionCanonical);
    session.restoredDraftPending = false;  // 基于当前版本重新编辑——恢复草稿的独立应用资格随之终止
}

// =====================================================================
// 宿主迁移三接入面（WP-14-T10——B1-SPEC §5.1；只消费 UI-T21/T22 冻结协议）
// =====================================================================

RequirementsUiModule::SharedSurfaceHandles RequirementsUiModule::sharedSurfaceProviders()
{
    m_guard.assertOnUiThread();  // §3.4——会话态绑定与面板指针读取（UI 线程）

    // 惰性构造＋缓存（shared_ptr 稳定地址——宿主注册进共享模型后模型持
    // 强引用，本模块缓存同序；Deps 一次性绑定：编辑器现取入口指向模块
    // attachEditor 注入的权威指针〔PA-1 零缓存〕，根身份指向会话态，
    // 编辑分流出口与高亮/激活执行器绑定面板指针——时序契约见头注
    // "须在 attachPanel 之后调用"）。
    if (!m_treeProvider) {
        RequirementsSharedSurfaceDeps deps;
        deps.editor = [this]() -> IRequirementEditor* {
            return m_editor;  // 会话权威编辑器（工作集唯一载体——§3.4）
        };
        deps.rootObjectId = [this]() -> std::optional<core::ObjectId> {
            return m_session.rootObjectId;  // 会话态权威根身份（修订闭包锚）
        };
        deps.editSink = m_panel;  // 面板即 IRequirementEditSink（L-R2 三路分流落点）
        deps.panelHighlight = [this](const std::optional<core::ObjectId>& oid) {
            if (m_panel != nullptr) { m_panel->focusObject(oid); }  // 树选→面板高亮
        };
        deps.complexPageActivator = [this](const core::ObjectId& oid) {
            if (m_panel != nullptr) { m_panel->focusObject(oid); }  // D6 域自持打开＝定位目标对象
        };
        m_treeProvider = std::make_shared<RequirementsTreeNodesProvider>(deps);
        m_pageProvider = std::make_shared<RequirementsPropertyPagesProvider>(deps);
    }
    SharedSurfaceHandles handles;
    handles.treeNodes = m_treeProvider;
    handles.propertyPages = m_pageProvider;
    return handles;
}

void RequirementsUiModule::attachSelectionService(ui::SelectionService& service)
{
    m_guard.assertOnUiThread();  // §3.4——订阅是 UI 线程交互面
    if (!m_treeProvider) {
        sharedSurfaceProviders();  // 适配器与三接入面同 deps——惰性齐备
    }
    if (!m_adapter) {
        // 适配器 Deps 与 Provider 同源（编辑器/根身份现取＋面板执行器——
        // 同一装配语义，零第二份绑定面）。
        RequirementsSharedSurfaceDeps deps;
        deps.editor = [this]() -> IRequirementEditor* {
            return m_editor;
        };
        deps.rootObjectId = [this]() -> std::optional<core::ObjectId> {
            return m_session.rootObjectId;
        };
        deps.panelHighlight = [this](const std::optional<core::ObjectId>& oid) {
            if (m_panel != nullptr) { m_panel->focusObject(oid); }
        };
        m_adapter = std::make_unique<RequirementsSelectionAdapter>(std::move(deps));
    }
    m_adapter->attach(service);
}

void RequirementsUiModule::detachSelectionService()
{
    m_guard.assertOnUiThread();
    if (m_adapter) { m_adapter->detach(); }
}

bool RequirementsUiModule::reportView3DPick(const core::ObjectId& oid)
{
    m_guard.assertOnUiThread();  // §3.4——选中写入口只允许 UI 线程
    if (!m_adapter) { return false; }  // 未接线＝无上报通道（诚实 false）
    const bool accepted = m_adapter->reportView3DPick(oid);
    if (accepted) {
        // UI-T33：本域拾取成功登记（拾取命令的输入面——flowPickFeature
        // 经缝取用；零修订会话态）。
        m_lastView3DPick = oid;
    }
    return accepted;
}

void RequirementsUiModule::bindView3DSeams(RequirementsView3DSeams seams)
{
    m_guard.assertOnUiThread();
    // lastPicked 缝重写为自引用闭包（本模块即拾取登记面——宿主透传的
    // 其余缝保持原样）。
    seams.lastPickedObjectId = [this]() { return m_lastView3DPick; };
    m_view3dSeams = std::move(seams);
    m_view3dSeamsBound = true;
}

void RequirementsUiModule::bindStationMarkersSink(
    RequirementsPanelWidget::StationMarkersSink sink)
{
    m_guard.assertOnUiThread();
    // 双形态（bindCommandSubmit 同一时序语义——UI-T43 可写链同构先例）：
    // 面板已创建＝即时注入；未创建＝暂存（attachPanel 时应用——装配序
    // 无关，杜绝"宿主先绑、面板后建"的初值失实）。
    m_pendingMarkersSink = std::move(sink);
    if (m_panel != nullptr) {
        m_panel->setStationMarkersSink(m_pendingMarkersSink);
    }
}

void RequirementsUiModule::bindRegionPreviewSink(
    RequirementsPanelWidget::RegionPreviewSink sink)
{
    m_guard.assertOnUiThread();
    m_pendingRegionSink = std::move(sink);
    if (m_panel != nullptr) {
        m_panel->setRegionPreviewSink(m_pendingRegionSink);
    }
}

bool RequirementsUiModule::executeDomainCommand(const std::string& commandId)
{
    m_guard.assertOnUiThread();
    if (m_panel == nullptr) {
        return false;  // 面板缺位（装配间隙）——命令不可达，不虚构执行
    }
    // UI-T33：三维缝透传（绑定在位＝capture-tcp/pick-feature 走真数据源；
    // 未绑定＝空指针透传，面板/命令流保持诚实降级原文）。
    return m_panel->executeDomainCommand(
        commandId, m_view3dSeamsBound ? &m_view3dSeams : nullptr);
}

}  // namespace sdurws::ird::requirements
