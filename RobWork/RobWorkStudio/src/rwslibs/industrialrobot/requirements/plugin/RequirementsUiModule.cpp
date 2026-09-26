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

}  // namespace sdurws::ird::requirements
