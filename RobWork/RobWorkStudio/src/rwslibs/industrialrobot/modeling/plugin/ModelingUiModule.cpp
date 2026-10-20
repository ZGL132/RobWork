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
#include "ModelingDraftCodec.hpp" // B4b完整工作集域帧，不再只存根。
#include "ModelingDiffDialog.hpp" // PM-04：复用唯一差异服务/窗口提供旧稿可见核对。
#include <QDebug>
#include "PolicyNameContexts.hpp"             // RuntimeMapPolicyNameContext（T03b——映射转发形名称上下文，退役草稿桩）

#include <stdexcept>
#include <algorithm> // B4b比较ID集合的规范内容，不依赖传递包含。
#include <type_traits>
#include <set> // B4c-2保存基线中已分配部件身份，临时ID不得被当作既有对象。
#include <utility>
#include <cmath>
#include <sdurws/ird/modeling/ModelingPluginAssembly.hpp> // F13：公共纯值请求，不引入运动学业务依赖。

#include <sdurws/ird/modeling/StructureEdit.hpp>  // addJointAt/structureEditErrorCodeToken（UI-T63 声明补轴——域原语）
#include <sdurws/ird/modeling/Template.hpp>  // RobotDesignTemplateFactory/applyJointFieldEdit（首版种子＋UI-T63 声明驱动——真实域路径）

#include <sdurws/ird/modeling/Package.hpp>    // packageChecklistText（UI-T61——规范包清单预览域面）
#include <sdurws/ird/modeling/Codec.hpp>       // RobotDesignCodec/kCurrentFormatVersion（根对象确定性编码——§4.8）
#include <sdurws/ird/modeling/ObjectTypes.hpp> // kRobotDesignObjectType（根对象 token——runtime 单一权威的 using 重导出）
#include <sdurws/ird/modeling/CanonicalBridge.hpp> // A-14：只读闭包，恢复不从运行时逆推参数。
#include <sdurws/ird/modeling/ModelDiff.hpp> // F16：只消费定位锚，不在模块重算差异。

namespace sdurws::ird::modeling {
namespace {
/**
 * @brief 提取草稿持久内容的规范比较值，避免磁盘往返的集合定序被误判为工程编辑。
 * @param value [in] 完整工作集副本；清摘要、按ObjectId规范工具/场景顺序，原值不变。
 * @return 原Codec规范对象字节封装；SI物理量/来源/根身份仍参与比较，派生缓存沿Codec省略。
 * @throws std::runtime_error 对象编码失败（内部工作集违约），不把失败当相等。
 * 纯值、无共享状态/IO；仅模块UI线程消费。摘要本身由调用方单独检查。
 */
std::string canonicalDraftContents(ModelingWorkingSet value)
{
    value.changes.clear();
    // 外层按身份引用的对象集合不携带编辑顺序语义；对象内部TCP键等集合
    // 完全交给唯一RobotDesignCodec，不能在这里复制物性或位姿编码。
    const auto less=[](const auto& a,const auto& b){return a.objectId.toCanonical()<b.objectId.toCanonical();};
    std::sort(value.toolObjects.begin(),value.toolObjects.end(),less);
    std::sort(value.sceneObjects.begin(),value.sceneObjects.end(),less);
    return encodeModelingDraft(value);
}
}

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
    envelope.commandType = std::string(kCmdApplyModelingWorkingSet);  // project 词表 token（无点——两套命名空间纪律）
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
    CommandPayload payload;payload.objects.push_back(std::move(slot));
    std::set<std::string> persisted;
    if(m_session.baselineSnapshot){
        const auto& saved=*m_session.baselineSnapshot;
        for(const auto& tool:saved.toolObjects){persisted.insert(tool.objectId.toCanonical());}
        for(const auto& scene:saved.sceneObjects){persisted.insert(scene.objectId.toCanonical());}
        if(saved.poseSetObject){persisted.insert(saved.poseSetObject->objectId.toCanonical());}
        if(saved.drivetrainObject){persisted.insert(saved.drivetrainObject->objectId.toCanonical());}
    }
    // 完整部件槽按类别、ObjectId定序；临时ID留在请求中连接根引用，
    // project处理器负责一次取号并回填，模块不分配持久ID、不拆成多个修订。
    const auto append=[&](const auto& value,std::string_view token){
        const auto bytes=codec.encode(ObjectVariant{value},kCurrentFormatVersion);
        if(!bytes.ok()){throw std::runtime_error("完整建模部件编码失败："+bytes.error().detail);}
        PayloadObjectSlot part;part.objectId=value.objectId;
        part.allocateNew=persisted.count(value.objectId.toCanonical())==0;
        part.objectTypeToken=std::string(token);part.objectBytes=bytes.get();
        payload.objects.push_back(std::move(part));
    };
    auto tools=m_session.draft.toolObjects;auto scenes=m_session.draft.sceneObjects;
    const auto less=[](const auto& a,const auto& b){return a.objectId.toCanonical()<b.objectId.toCanonical();};
    std::sort(tools.begin(),tools.end(),less);std::sort(scenes.begin(),scenes.end(),less);
    for(const auto& value:tools){append(value,kToolDefinitionObjectType);}
    for(const auto& value:scenes){append(value,kSceneObjectObjectType);}
    if(m_session.draft.poseSetObject){append(*m_session.draft.poseSetObject,kNamedPoseSetObjectType);}
    if(m_session.draft.drivetrainObject){append(*m_session.draft.drivetrainObject,kRobotDrivetrainObjectType);}
    envelope.payloadCanonical=encodeCommandPayload(payload);

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

// F-546 出路①（UI-T74）：草稿对象名解析——草稿名感知名称上下文的模块侧
// 执行器。行程校验（MDL-06④）的评估器以 runtimeName 作发现/诊断的标识
// 标签（判定本体＝限位区间对策略阈值的纯算术，JointLimits.cpp）；首应用
// 时发布拍未发生、映射为空（F-546 结构性墙）——本执行器以草稿工作集
// 自身命名补位（localName＝草稿的权威命名，确定性；对象经应用入库后
// 身份跨修订稳定，发布后由映射真值接管的语义不变）。仅读投影零修订；
// 闭包外身份如实 false（不猜测——ARC-04）。
bool ModelingUiModule::tryDraftObjectName(const core::ObjectId& object,
                                          std::string& name) const
{
    m_guard.assertOnUiThread();  // 会话态只读（§3.4 UI 线程纪律）
    const auto match = [&object, &name](const core::ObjectId& id,
                                        const std::string& local) {
        if (id == object && !local.empty()) {
            name = local;
            return true;
        }
        return false;
    };
    for (const JointEntry& j : m_session.draft.design.joints) {
        if (match(j.objectId, j.localName)) { return true; }
    }
    for (const LinkEntry& l : m_session.draft.design.links) {
        if (match(l.objectId, l.localName)) { return true; }
    }
    for (const ToolDefinition& t : m_session.draft.toolObjects) {
        if (match(t.objectId, t.localName)) { return true; }
    }
    for (const SceneObject& sc : m_session.draft.sceneObjects) {
        if (match(sc.objectId, sc.localName)) { return true; }
    }
    return false;  // 闭包外身份（不猜测——ARC-04）
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

void ModelingUiModule::bindWorkCellExport(
    std::function<bool(const std::string& targetPath, std::string& summary)> exportFn)
{
    m_guard.assertOnUiThread();
    // 无面板形态差异（面板不经手本回调——命令流经 executeDomainCommand
    // 的 deps 直取成员）；重复绑定＝以最新宿主装配为准（幂等覆盖）。
    m_workCellExport = std::move(exportFn);
}

void ModelingUiModule::bindWorkCellPreview(
    std::function<bool(const std::string& kind, std::string& headerLine,
                       std::string& sourceObject, std::string& text,
                       std::string& reason)> previewFn)
{
    m_guard.assertOnUiThread();
    // 重复绑定＝以最新宿主装配为准（幂等覆盖——bindWorkCellExport 同款）；
    // 面板已创建＝即时转发 provider 适配（晚绑定装配序补齐——面板经
    // setPreviewContentProvider 持适配闭包，XML 类预览请求经此达宿主）。
    m_workCellPreview = std::move(previewFn);
    if (m_panel != nullptr) {
        attachPanelWiring();
    }
}

void ModelingUiModule::pushAppliedPreviewToPanel()
{
    // 预览态变迁的统一推送点（UI-T59 接线缺口修复——UI-T41 A3 残留：
    // m_session.appliedPreview 有存有清但从未推送面板，预览页在产品中恒
    // 停留构造初态）。面板侧 refreshPreview 单一渲染出口自行分辨空态/
    // 摘要/XML 轨——本推送面零判定（当前选中预览类型是什么就重渲什么）。
    if (m_panel != nullptr) {
        m_panel->setAppliedPreview(m_session.appliedPreview);
    }
}

void ModelingUiModule::attachPanelWiring()
{
    if (m_panel == nullptr) { return; }
    // L-5：编辑器局部历史只在本模块内恢复工作集，不提交项目命令。
    // 与预览接线同入口，晚建/外部接入面板也具备相同历史能力。
    m_panel->setDraftHistoryActions([this]{restoreDraftHistory(false);},
                                  [this]{restoreDraftHistory(true);});
    m_panel->setPostEditAction([this]{recomputeReadiness();});
    m_panel->setPoseRestoreAction(m_sessionPoseWriter
        ? std::function<bool(const std::string&,std::string&)>(
            [this](const std::string& key,std::string& why){return restoreNamedPose(key,why);})
        : std::function<bool(const std::string&,std::string&)>{});
    refreshDraftHistoryState();
    // XML 类预览供给适配（宿主回调已绑定时转发——不绑定＝面板 fail-closed
    // 诚实呈现"未接线"；适配闭包零 runtime 类型——纯字符串搬运）。
    if (m_workCellPreview) {
        m_panel->setPreviewContentProvider(
            [this](const std::string& kind)
                -> std::optional<ModelingPanelWidget::PreviewContentAnswer> {
                // "规范包清单"（UI-T61——F-498 余项）本地应答：数据源＝会话
                // 基线定格内容（已应用修订的草稿值视图——D-MDL-10），不经
                // 宿主编译快照（清单面是对象级 MDL-20/V-20 语义，非编译产
                // 物）；文本由域 packageChecklistText 确定性渲染（UI 零拼
                // 装同款纪律）。缺席＝撤销/重做失效或未应用——诚实缺席。
                if (kind == "package-checklist") {
                    if (!m_session.appliedPreview.has_value()
                        || !m_session.baselineSnapshot.has_value()) {
                        return std::nullopt;
                    }
                    const ModelingWorkingSet& baseline = *m_session.baselineSnapshot;
                    std::vector<ObjectVariant> parts;
                    parts.reserve(baseline.toolObjects.size()
                                  + baseline.sceneObjects.size() + 2);
                    for (const ToolDefinition& t : baseline.toolObjects) {
                        parts.push_back(ObjectVariant{t});
                    }
                    for (const SceneObject& s : baseline.sceneObjects) {
                        parts.push_back(ObjectVariant{s});
                    }
                    if (baseline.poseSetObject.has_value()) {
                        parts.push_back(ObjectVariant{*baseline.poseSetObject});
                    }
                    if (baseline.drivetrainObject.has_value()) {
                        parts.push_back(ObjectVariant{*baseline.drivetrainObject});
                    }
                    ModelingPanelWidget::PreviewContentAnswer answer;
                    answer.headerLine =
                        "来源修订 " + m_session.appliedPreview->revision.toCanonical()
                        + "（内容定格于应用时刻——D-MDL-10）｜对象 "
                        + std::to_string(parts.size()) + " 个";
                    answer.sourceObject =
                        "checklist-objects=" + std::to_string(parts.size());
                    answer.text = modeling::packageChecklistText(baseline.design, parts);
                    return answer;
                }
                std::string headerLine;
                std::string sourceObject;
                std::string text;
                std::string reason;
                if (!m_workCellPreview || !m_workCellPreview(kind, headerLine,
                                                             sourceObject, text,
                                                             reason)) {
                    return std::nullopt;  // 快照缺席/能力缺席——诚实缺席
                }
                ModelingPanelWidget::PreviewContentAnswer answer;
                answer.headerLine = std::move(headerLine);
                answer.sourceObject = std::move(sourceObject);
                answer.text = std::move(text);
                return answer;
            });
    }
    // 已定格预览推送（attachPanel/createPanel 装配序补齐——面板晚于定格）。
    pushAppliedPreviewToPanel();
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
    pushAppliedPreviewToPanel();  // 预览态变迁推送（UI-T59 接线缺口修复）
    // baseRevision 留空（nullopt＝提交期解析 tip——§6.2）；种子后就绪
    // 真判定即刻重算（T03b-2b——就绪条不再空态，如实呈现阻塞/缺项）。
    recomputeReadiness();
}

bool ModelingUiModule::reseedCustomChain(const CustomChainDeclaration& decls,
                                         std::string& summary)
{
    m_guard.assertOnUiThread();
    // 声明面校验（恰六项——本向导声明面范围；其他轴数拒绝不虚放——4/5 轴
    // 仍受 creationEntryGuard 阻断〔AT-20/§6.4〕；六/七轴含 prismatic 已随
    // MDL-12-S1（WP-13-T19）放开，但本向导声明面仍全旋转——prismatic 声明
    // UI 扩展随 UI 域任务，混合链经导入通道进入）。
    if (decls.joints.size() != 6) {
        summary = "custom-chain 声明须恰六轴（向导声明面范围）——实得 "
                + std::to_string(decls.joints.size()) + " 轴";
        return false;
    }
    // 种子：custom-chain 1 轴（Revolute J1 行——createDraft 真实域路径，
    // 禁手搓工作集）。fresh 上组合声明——拒绝时整体丢弃、会话字节不变。
    const RobotDesignTemplateFactory factory;
    std::vector<core::DiagnosticRecord> diags;
    auto outcome = factory.createDraft(TemplateId{kTemplateIdCustomChain},
                                       runtime::InstallationPresetToken::Ground,
                                       "demo", diags);
    if (!outcome.ok()) {
        summary = "custom-chain 种子创建失败（装配缺陷——种子恒可产）";
        return false;
    }
    ModelingWorkingSet fresh = outcome.get();
    // 补齐至六轴（addJointAt 尾追加——种子 Revolute 同款，链序 j1..j6；
    // 新关节种子＝T-MDL-1 J1 行表值〔Origin 恒零——随后声明 Origin 覆盖〕）。
    for (std::size_t i = 1; i < decls.joints.size(); ++i) {
        if (const auto err = addJointAt(fresh, i - 1)) {
            summary = "custom-chain 补轴失败（" + std::string(structureEditErrorCodeToken(err->code))
                    + "）：" + err->detail;
            return false;
        }
    }
    // 逐轴声明驱动既有域原语（Axis/ZeroOffset/Bounds/Origin——UserProvided
    // 提交，插件零计算逻辑；域内校验＝权威裁决面：轴线可归一化/限位有序/
    // Origin 有限性——拒绝即整体放弃，不产半成品草稿）。
    for (std::size_t j = 0; j < decls.joints.size(); ++j) {
        const CustomChainJointSpec& d = decls.joints[j];
        const std::string jn = "J" + std::to_string(j + 1);
        auto reject = [&](const JointEditError& err) {
            summary = jn + "：" + err.detail;
        };
        if (const auto err = applyJointFieldEdit(
                fresh, j, JointEditField::Axis,
                JointEditValue{rw::math::Vector3D<double>(d.axisX, d.axisY, d.axisZ)})) {
            reject(*err);
            return false;
        }
        if (const auto err = applyJointFieldEdit(
                fresh, j, JointEditField::ZeroOffset, JointEditValue{d.zero})) {
            reject(*err);
            return false;
        }
        if (const auto err = applyJointFieldEdit(
                fresh, j, JointEditField::Bounds,
                JointEditValue{JointLimits{d.lower, d.upper}})) {
            reject(*err);
            return false;
        }
        if (const auto err = applyJointFieldEdit(
                fresh, j, JointEditField::Origin,
                JointEditValue{JointOriginEditValue{d.x, d.y, d.z,
                                                    d.roll, d.pitch, d.yaw}})) {
            reject(*err);
            return false;
        }
    }
    // 提交：会话替换（seedTemplateSession 同款语义——根身份保留＋基线/
    // 已应用预览失效＋就绪重算驱动面板刷新；声明编辑保留在 changes 账面
    // ＝待应用编辑，应用修订后随基线消费）。
    const std::optional<core::ObjectId> previousRoot = m_session.draft.rootObjectId;
    m_session.draft = std::move(fresh);
    m_session.draft.rootObjectId = previousRoot;
    m_session.baselineSnapshot.reset();
    m_session.appliedPreview.reset();
    pushAppliedPreviewToPanel();
    recomputeReadiness();
    summary = "已按声明创建 custom-chain 六轴草稿（全旋转——向导声明面；"
              "混合链经导入通道——MDL-12-S1；"
              "声明编辑 " + std::to_string(m_session.draft.changes.size())
            + " 项待应用）";
    return true;
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
    attachPanelWiring();  // 预览供给接线＋已定格预览推送（UI-T59——面板
                          // 晚于绑定/定格的装配序补齐）
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
    // F09：只读不仅禁用可见按钮，直接宿主调用也不得打开写入型估算流程。
    if (commandId == "modeling.estimate-properties" && !m_writable) { return false; }
    // 流程依赖（宿主侧回调全量接线——flows 零模块类型依赖的测试缝形态）：
    // reseedTemplate＝模板重种子；reseedCustomChain＝custom-chain 声明重种
    // 子（UI-T63——从零创建落点）；recomputeReadiness＝草稿变更后真判定；
    // selectedAnchor＝面板会话选中锚（estimate/diff 的目标解析输入面）；
    // exportWorkCellXml＝宿主绑定回调（UI-T56——快照取源＋域导出整体）。
    ModelingFlowDeps deps;
    deps.reseedTemplate = [this] { seedTemplateSession(); };
    deps.reseedCustomChain =
        [this](const CustomChainDeclaration& decls, std::string& summary) {
            return reseedCustomChain(decls, summary);
        };
    deps.recomputeReadiness = [this] { recomputeReadiness(); };
    deps.selectedAnchor = [this]() -> std::optional<core::ObjectId> {
        return m_panel != nullptr ? m_panel->selectedAnchor() : std::nullopt;
    };
    deps.exportWorkCellXml = m_workCellExport;
    deps.locateDiffObject=[this](const ModelDiffEntry& entry)->std::string {
        // 差异的数组下标属于报告生成时刻；定位必须现查身份，避免删除/重排
        // 后跳到另一个对象。字段删除但对象还在仍允许定位；资源无树节点拒绝。
        if(entry.field=="resource"){
            return "无法定位：资源清单条目没有独立的结构树节点。";
        }
        const auto target=entry.objectId.isValid()?std::optional<core::ObjectId>(entry.objectId):
            m_session.draft.rootObjectId;
        if(!m_panel){return "无法定位：建模面板尚未打开。";}
        if(!target||!resolveSelection(m_session.draft,*target)){
            return "无法定位：对象已不在当前草稿中，或该引用/资源没有可定位树节点。";
        }
        // 正式宿主必须先写共享选择，再由既有订阅链同步域树/属性和三维。
        // 退订说明宿主正在关闭/重装配，诚实拒绝，不能假报本地定位成功。
        // 从未装配共享服务的独立面板保留原本的本地定位能力。
        if (m_adapter) {
            if (!m_adapter->reportCommandSelection(*target)) {
                return "无法定位：共享选择服务尚未连接，原选中保持不变。";
            }
        } else {
            m_panel->focusObject(target);
        }
        return "已定位到当前草稿中的对象；结构树与属性区已同步。";
    };
    if(m_sessionPoseWriter){
        deps.restoreNamedPose=[this](const std::string& key,std::string& why){return restoreNamedPose(key,why);};
    }
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
    // 所有已接受字段、结构、部件及命令流最终经过这里；比较完整值，
    // 因策略/名称/显示刷新而触发的纯重算不产生额外撤销步（L-5）。
    rememberDraftHistory();
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
    // PM-04/AT-21：观察完整规范域帧，覆盖结构、部件、字段及局部撤销/重做。
    // 先更新观察点，再于锁外通知；纯就绪刷新不应制造保存脏代次。
    if (m_observedDraftFrame) {
        auto frame = encodeModelingDraft(m_session.draft);
        const bool changed = frame != *m_observedDraftFrame;
        m_observedDraftFrame = std::move(frame);
        if (changed && m_session.branch != core::BranchId{} && m_draftChanged) {
            m_draftChanged();
        }
    }
}

void ModelingUiModule::onSessionDetached()
{
    m_guard.assertOnUiThread();
    ++m_reviewSessionGeneration; // 模态对照窗口可能尚在；先作废其定位，再清项目对象。
    // 会话边界先切断保存通知，关闭后的空工作集不能成为下一项目的脏草稿。
    m_draftChanged = {};
    m_observedDraftFrame.reset();
    // UI-T77（F-567）：先退订选择适配器再清状态源（teardown「先退订消
    // 费者再清状态源，防清理过程重入回调」序的对齐面）——requirements
    // 侧 onSessionDetached 显式调 detachSelectionService，本域此前漏调：
    // teardownSharedSurfacesForClose 的 clearSelection 空选中广播会在拆
    // 卸中重入本域适配器→面板 focusObject(nullopt) 树清选/属性重投影恰
    // 发生在拆卸过程内，选中锚只能靠这次意外重入间接清掉。
    detachSelectionService();
    // §8.6 表"模块表/局部栈清空"的模块状态半区：会话内存态整体复位——
    // 分支锚清空（Stale 判据随锚消失，旧会话不可能污染新会话——§6.2
    // 纪元过滤的模块对位）、草稿工作集复位、就绪报告作废。磁盘草稿零
    // 触碰（落盘/丢弃归 DraftController 处置链——§5.4 决议执行半区）。
    m_session.branch = core::BranchId{};
    m_session.baseRevision.reset();
    m_session.draft = ModelingWorkingSet{};
    m_retainedConflictDraft.reset(); // 待迁移素材只属原项目，禁止关闭后泄漏到新会话。
    clearDraftHistory(false); // 关闭后的空态不是可撤销版本，禁止跨项目恢复。
    m_session.readiness.reset();
    m_session.baselineSnapshot.reset();   // 应用侧快照随会话作废（UI-T41）
    m_session.appliedPreview.reset();
    pushAppliedPreviewToPanel();  // 预览态变迁推送（UI-T59 接线缺口修复）
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
    // PM-04/AT-29：事件只报告有新修订，不证明未应用编辑已在新基线上重放。
    // 自身提交的同步回执已经定格基线；重复事件不能使刚建立的预览/基线失效。
    if (m_session.baseRevision == newTip) { return; }
    if (hasPendingDraft()) {
        // 保留工作集、保存基线和局部历史，让后续命令仍以旧锚接受project的
        // Stale裁决。不能只改修订号而把旧内容冒充成已合并的新内容。
        if (m_panel) {
            m_panel->setOutcomeMessage(QString::fromUtf8(
                "项目已有新修订，建模草稿基于旧修订；请先处置草稿后重新编辑。"));
        }
        return;
    }
    // 干净会话无待重放内容，沿原路径推进并使未知的新修订预览失效。
    m_session.baseRevision = newTip;
    clearDraftHistory(); // 项目修订边界与草稿局部历史正交，不跨已提交修订回放。
    // 应用侧快照失效（UI-T41——撤销/重做后的修订内容本模块未知：预览页/
    // diff 基线以空态诚实呈现，不虚构内容；再次应用即重新定格）。
    m_session.baselineSnapshot.reset();
    m_session.appliedPreview.reset();
    pushAppliedPreviewToPanel();  // 预览态变迁推送（UI-T59 接线缺口修复）
    recomputeReadiness();
}

// =====================================================================
// 会话锚定与应用回执（T03b-2——apply 网关的模块半区）
// =====================================================================

bool ModelingUiModule::hasPendingDraft() const
{
    // B4b完整规范比较是唯一的内容等价口径：摘要缺席不能漏掉真实修改，
    // TCP等集合的Codec规范排序也不能把干净磁盘恢复误判为有编辑。
    return !m_session.draft.changes.empty()
        || (m_session.baselineSnapshot
            && canonicalDraftContents(m_session.draft)
                != canonicalDraftContents(*m_session.baselineSnapshot));
}

void ModelingUiModule::bindSessionAnchor(const core::BranchId& branch,
                                         const core::RevisionId& base)
{
    m_guard.assertOnUiThread();
    const bool changed=!(m_session.branch==branch)||m_session.baseRevision!=base;
    if (m_session.branch.isValid() && m_session.baseRevision && hasPendingDraft()) {
        // 首次打开仍可给无锚的种子绑定真实分支；已有草稿必须保留原关联。
        // 跨分支意味着用户尚未处置的内容将写入另一方案，必须显式拒绝。
        if (!(m_session.branch == branch)) {
            throw std::runtime_error("建模草稿有未应用编辑，请先处置后再切换分支");
        }
        // 同分支的新tip不等于成功重放，保留旧expectedRevision及局部撤销栈。
        return;
    }
    m_session.branch = branch;
    m_session.baseRevision = base;
    if(changed){clearDraftHistory();} // 新分支/新基线不能沿用另一会话的回退点。
    // 锚定后重算＋重投影（新会话上下文——呈现与信封组装同源）。
    recomputeReadiness();
}

bool ModelingUiModule::restoreCommittedSession(
    const ObjectClosureView& closure, const core::ObjectId& rootObjectId,
    const core::BranchId& branch, const core::RevisionId& revision,
    std::string& errorText)
{
    return readCommittedSession(closure,rootObjectId,branch,revision,errorText,false);
}

bool ModelingUiModule::readCommittedSession(
    const ObjectClosureView& closure, const core::ObjectId& rootObjectId,
    const core::BranchId& branch, const core::RevisionId& revision,
    std::string& errorText, bool applicationReceipt)
{
    m_guard.assertOnUiThread();
    if (!rootObjectId.isValid() || !branch.isValid() || !revision.isValid()) {
        // 空身份不是磁盘损坏，而是装配调用方没有给出可追溯基线。
        throw std::invalid_argument("恢复已提交建模会话需要有效根、分支和修订身份");
    }
    errorText.clear();
    if (!applicationReceipt && m_session.branch == branch && m_session.baseRevision == revision
        && m_session.draft.rootObjectId == rootObjectId
        && m_session.baselineSnapshot.has_value()) {
        // 同一上下文反复投影时不能把用户正在编辑的值重新覆盖为保存值。
        return true;
    }
    if (!applicationReceipt && hasPendingDraft()) {
        // 普通重开不能覆盖尚未处置的编辑；已成功提交的回执则必须读取完整
        // 持久身份，不能沿普通恢复的脏保护/幂等短路只留下临时部件ID。
        errorText = "建模工作集有未应用编辑，请先处置后再切换已提交基线";
        return false;
    }

    ModelingWorkingSet candidate;
    try {
        // 第一步只读取局部候选。任何缺失、类型或版本错误都在修改会话之前退出；
        // Codec负责规范格式与业务值校验，这里仅核对闭包槽与嵌入身份的关联。
        const auto read = [&](const core::ObjectId& id, std::string_view token) -> ObjectVariant {
            const auto object = closure.tryObject(id);
            const auto location = std::string(token) + " / " + id.toCanonical();
            if (!object) { throw std::runtime_error(location + "：对象缺失"); }
            if (object->objectTypeToken != token) {
                throw std::runtime_error(location + "：闭包类型不符");
            }
            const auto decoded = RobotDesignCodec{}.decode(object->bytes, kCurrentFormatVersion);
            if (!decoded.ok()) { throw std::runtime_error(location + "：" + decoded.error().detail); }
            return decoded.get();
        };
        const auto root = read(rootObjectId, kRobotDesignObjectType);
        const auto* design = std::get_if<RobotDesign>(&root);
        if (!design) { throw std::runtime_error("robot-design：负载类型不符"); }
        candidate.design = *design;
        candidate.rootObjectId = rootObjectId;
        // 根引用顺序就是恢复顺序；SI m/rad、来源信息、对象ID均按原值保留。
        const auto restorePart = [&](const core::ObjectId& id, std::string_view token, auto& target) {
            using Part = std::decay_t<decltype(target)>;
            const auto value = read(id, token);
            const auto* part = std::get_if<Part>(&value);
            if (!part || part->objectId != id) {
                throw std::runtime_error(std::string(token) + " / " + id.toCanonical()
                                         + "：负载类型或嵌入对象身份不符");
            }
            target = *part;
        };
        for (const auto& id : design->toolRefs) {
            ToolDefinition tool;
            restorePart(id, kToolDefinitionObjectType, tool);
            candidate.toolObjects.push_back(std::move(tool));
        }
        for (const auto& id : design->sceneRefs) {
            SceneObject scene;
            restorePart(id, kSceneObjectObjectType, scene);
            candidate.sceneObjects.push_back(std::move(scene));
        }
        if (design->poseSetRef) {
            PoseSet poses;
            restorePart(*design->poseSetRef, kNamedPoseSetObjectType, poses);
            candidate.poseSetObject = std::move(poses);
        }
        if (design->drivetrainRef) {
            DrivetrainDesign drivetrain;
            restorePart(*design->drivetrainRef, kRobotDrivetrainObjectType, drivetrain);
            candidate.drivetrainObject = std::move(drivetrain);
        }
    } catch (const std::exception& error) {
        errorText = std::string("恢复已提交建模工作集失败：") + error.what();
        return false;
    }
    // 第二步完整候选一次采纳，再复用应用回执建立clean baseline和预览。
    // 此入口不提交命令，也不持有闭包引用；关闭仍沿原会话脱离路径清理。
    m_session.draft = std::move(candidate);
    m_session.branch = branch;
    noteAppliedRevision(revision, rootObjectId);
    return true;
}

bool ModelingUiModule::acceptAppliedSession(
    const ObjectClosureView& closure, const core::ObjectId& rootObjectId,
    const core::BranchId& branch, const core::RevisionId& revision, std::string& errorText)
{
    return readCommittedSession(closure, rootObjectId, branch, revision, errorText, true);
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
    pushAppliedPreviewToPanel();  // 定格即推送（UI-T59 接线缺口修复——此前
                                  // 仅存会话未达面板，预览页恒空态）
    clearDraftHistory(); // 应用成功消费全部未应用编辑，本地撤销不得越过该边界。
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
    // 域payload是完整工作集封装；project外层格式/归属元数据仍由控制器负责。
    ui::DraftDocumentProjection document;
    document.schemaVersion = 1; // 私有域帧版本，仅遥测；project写自己的文件版本。
    document.moduleId = kModuleHandle;
    // 冲突详情须按草稿所属分支读取；这里已有权威会话分支，不让宿主从HEAD猜。
    // projectId及保存时刻仍由控制器按绑定填充，保存分支亦由该绑定覆盖。
    document.branchId = m_session.branch;
    document.baseRevisionId = m_session.baseRevision.value_or(core::RevisionId{});
    document.payload = encodeModelingDraft(m_session.draft);
    return document;
}

void ModelingUiModule::adoptRestoredDocument(
    const ui::DraftDocumentProjection& document)
{
    m_guard.assertOnUiThread();
    // 先完整解析到局部候选。坏数据不能部分覆盖根后留下旧部件；只读允许
    // 浏览恢复内容，真正编辑/项目提交的权限仍由现有执行端防御。
    std::string error;
    auto candidate=document.moduleId==kModuleHandle
        ?decodeModelingDraft(document.payload,m_session.draft,error):std::nullopt;
    if(document.moduleId!=kModuleHandle){error="草稿模块不匹配，未恢复建模工作集。";}
    if(candidate&&m_session.baselineSnapshot&&m_session.baselineSnapshot->rootObjectId
       &&candidate->rootObjectId!=m_session.baselineSnapshot->rootObjectId){
        error="草稿根身份与当前已保存模型不匹配，未恢复。";candidate.reset();
    }
    if(!candidate){
        // void端口不能回传恢复失败；保留当前值并显式显示原因，不伪造草稿写入。
        qWarning().noquote()<<QString::fromStdString("建模草稿恢复拒绝："+error);
        if(m_panel){m_panel->setOutcomeMessage(QString::fromStdString("草稿恢复失败："+error));}
        return;
    }
    // 兼容没有摘要的既有调用方：内容差异才是未应用事实。不能因空摘要
    // 把已恢复的新部件误判为NoDraft；相同基线仍不制造空项目修订。
    const auto& saved=m_session.baselineSnapshot?*m_session.baselineSnapshot:m_session.draft;
    if(candidate->changes.empty()&&canonicalDraftContents(*candidate)!=canonicalDraftContents(saved)){
        candidate->changes.push_back({"draft.restore","恢复未应用建模工作集"});
    }
    m_session.draft=std::move(*candidate);
    // 保留磁盘草稿的旧基线，让项目命令服务真实判断Stale；不能用当前tip
    // 偷换原基线。旧载荷无基线时保持已有锚，未知身份不猜测。
    if(document.baseRevisionId.isValid()){m_session.baseRevision=document.baseRevisionId;}
    if(m_panel){m_panel->cancelPendingForHistoryRestore();}
    clearDraftHistory(); // 只清局部栈；未应用changes随完整帧原样保留。
    recomputeReadiness();
}

void ModelingUiModule::rebuildOnRevision(const std::string& tipRevisionCanonical)
{
    m_guard.assertOnUiThread();
    const auto tip=core::RevisionId::tryFromCanonical(tipRevisionCanonical);
    if(!tip||!m_session.branch.isValid()){
        throw std::invalid_argument("重新编辑需要有效分支及修订身份");
    }
    if(!m_revisionDraftReader){throw std::runtime_error("建模固定修订读取未装配，原草稿已保留");}
    // 第一步完整读取到局部值。旧草稿/历史/锚都不能在读错或类型错时先行清除。
    // nullopt由宿主明确表达无根；此时新基线是空项目，不能用旧模板伪造新权威值。
    const auto document=m_revisionDraftReader(m_session.branch,*tip);
    ModelingWorkingSet candidate;
    if(document){
        if(document->moduleId!=kModuleHandle||document->baseRevisionId!=*tip){
            throw std::runtime_error("建模固定修订读取的模块或修订不匹配");
        }
        std::string error;
        const auto decoded=decodeModelingDraft(document->payload,ModelingWorkingSet{},error);
        if(!decoded||!decoded->rootObjectId||!decoded->changes.empty()){
            throw std::runtime_error("建模固定修订读取不是完整干净基线："+error);
        }
        candidate=*decoded;
    }
    const auto previous=buildDraftDocument(); // 保留完整旧值和原base，绝不重标锚后保存。
    // 第二步一次采纳。控制器在本方法成功返回后才清冲突/会话脏标记；这里
    // 不重试提交，用户必须基于新的完整值逐项迁移旧修改。
    m_retainedConflictDraft=previous;
    if(m_panel){m_panel->cancelPendingForHistoryRestore();}
    m_session.draft=std::move(candidate);
    noteAppliedRevision(*tip,m_session.draft.rootObjectId);
    if(m_panel){m_panel->setOutcomeMessage(QString::fromUtf8(
        "已读取当前修订的完整基线；旧草稿已保留，请核对并迁移未应用修改。"));}
}

void ModelingUiModule::bindRevisionDraftReader(
    std::function<std::optional<ui::DraftDocumentProjection>(const core::BranchId&,
        const core::RevisionId&)> reader)
{
    m_guard.assertOnUiThread();m_revisionDraftReader=std::move(reader);
}

std::optional<ui::DraftDocumentProjection> ModelingUiModule::retainedConflictDraft() const
{
    m_guard.assertOnUiThread();return m_retainedConflictDraft;
}

void ModelingUiModule::bindDraftChanged(std::function<void()> callback)
{
    m_guard.assertOnUiThread();
    // 首次接线只建立观察点；既有未应用内容由宿主显式登记，避免绑定回放。
    m_draftChanged = std::move(callback);
    if (m_draftChanged) { m_observedDraftFrame = encodeModelingDraft(m_session.draft); }
    else { m_observedDraftFrame.reset(); }
}

void ModelingUiModule::bindSessionPoseWriter(
    std::function<bool(const ModelingSessionPoseRequest&,std::string&)> writer)
{
    m_guard.assertOnUiThread();
    m_sessionPoseWriter=std::move(writer);
    attachPanelWiring();
}

bool ModelingUiModule::restoreNamedPose(const std::string& key,std::string& summary)
{
    m_guard.assertOnUiThread();summary.clear();
    const auto& ws=m_session.draft;
    if(!m_sessionPoseWriter){summary="会话姿态恢复未装配——未写入";return false;}
    if(!m_session.baselineSnapshot||!ws.rootObjectId||m_session.branch==core::BranchId{}){
        summary="尚无已应用模型基线——请先应用模型，再恢复保存的姿态";return false;
    }
    if(!ws.poseSetObject){summary="命名位姿集缺失——未写入";return false;}
    const PoseSetEntry* target=nullptr;
    for(const auto& entry:ws.poseSetObject->entries){
        if(entry.key==key){
            if(target){summary="命名位姿键重复："+key+"——未写入";return false;}
            target=&entry;
        }
    }
    if(!target){summary="命名位姿不存在："+key+"——未写入";return false;}
    const auto& baseline=*m_session.baselineSnapshot;
    if(baseline.rootObjectId!=ws.rootObjectId||baseline.design.joints.size()!=ws.design.joints.size()
       ||target->jointConfiguration.size()!=baseline.design.joints.size()){
        summary="位姿维度或模型身份与已应用基线不匹配——未写入";return false;
    }
    ModelingSessionPoseRequest request;
    request.branch=m_session.branch;request.rootObjectId=*ws.rootObjectId;
    for(std::size_t i=0;i<baseline.design.joints.size();++i){
        const auto& joint=baseline.design.joints[i];
        if(joint.objectId!=ws.design.joints[i].objectId||joint.type!=ws.design.joints[i].type){
            summary="关节序或类型已改变——请先应用模型，未恢复姿态";return false;
        }
        const double modelQ=target->jointConfiguration[i];
        // CanonicalBridge已把该原始零偏折叠进origin/限位，不能再从canonical
        // 中取通常为0的zeroOffset反推。只用冻结基线，未应用编辑不影响会话。
        const double rawQ=joint.type==JointType::Fixed?0.0:modelQ-joint.zeroOffset;
        if(!std::isfinite(modelQ)||!std::isfinite(rawQ)){
            summary="姿态含非有限分量，关节序号 "+std::to_string(i+1)+"——未写入";return false;
        }
        request.jointIds.push_back(joint.objectId);request.jointConfiguration.push_back(rawQ);
    }
    if(!m_sessionPoseWriter(request,summary)){
        if(summary.empty()){summary="宿主拒绝恢复姿态——未写入";}
        return false;
    }
    summary="已恢复保存姿态 "+key+" 到已应用模型（仅会话姿态，未产生项目修订）";
    return true;
}

void ModelingUiModule::refreshDraftHistoryState()
{
    if(m_panel){
        m_panel->setDraftHistoryState(!m_draftUndo.empty(),!m_draftRedo.empty());
        m_panel->setRetainedReviewAction(m_retainedConflictDraft
            ?std::function<void()>([this]{showRetainedConflictReview();}):std::function<void()>{});
    }
}

void ModelingUiModule::showRetainedConflictReview()
{
    m_guard.assertOnUiThread();
    if(!m_retainedConflictDraft||!m_panel){return;}
    std::string error;const auto old=decodeModelingDraft(
        m_retainedConflictDraft->payload,ModelingWorkingSet{},error);
    if(!old){m_panel->setOutcomeMessage(QString::fromStdString("无法读取保留旧稿："+error));return;}
    // 模态期间可能关闭/切换项目，比较用值副本，禁止借用可变工作集跨事件循环。
    const auto current=m_session.draft;const auto branch=m_session.branch;const auto base=m_session.baseRevision;
    const auto generation=m_reviewSessionGeneration;
    const auto report=ModelDiffService{}.diff(current,*old);
    showModelingDiffDialog(report,current,*old,[this,branch,base,generation](const ModelDiffEntry& entry){
        if(!m_panel||m_reviewSessionGeneration!=generation
            ||m_session.branch!=branch||m_session.baseRevision!=base){
            return std::string("项目已变化，不能使用旧窗口定位，请关闭后重新查看。");
        }
        const auto target=entry.objectId.isValid()?std::optional<core::ObjectId>(entry.objectId):m_session.draft.rootObjectId;
        if(!target||entry.field=="resource"||!resolveSelection(m_session.draft,*target)){
            return std::string("该项仅在旧稿或资源清单中存在，请核对后在当前工作集手工重新编辑。");
        }
        if(m_adapter){
            if(!m_adapter->reportCommandSelection(*target)){return std::string("共享选择服务不可用，原选中保持。");}
        }else{m_panel->focusObject(target);}
        return std::string("已定位当前工作集中的对象；请核对旧值后手工编辑，尚未覆盖任何内容。");
    },true);
}

void ModelingUiModule::clearDraftHistory(bool retainCurrent)
{
    m_guard.assertOnUiThread();
    m_draftUndo.clear();m_draftRedo.clear();
    if(retainCurrent){m_historyCurrent=m_session.draft;}
    else{m_historyCurrent.reset();}
    refreshDraftHistoryState();
}

void ModelingUiModule::rememberDraftHistory()
{
    m_guard.assertOnUiThread();
    if(!m_historyCurrent){m_historyCurrent=m_session.draft;}
    else if(*m_historyCurrent!=m_session.draft){
        // 先保存上个接受版本，再采纳新版本；一次表单/批量操作只触发一次。
        // 根身份、来源徽标、全部资源和部件与changes共同回退，不重算或猜测旧值。
        m_draftUndo.push_back(*m_historyCurrent);
        m_historyCurrent=m_session.draft;m_draftRedo.clear();
    }
    refreshDraftHistoryState();
}

bool ModelingUiModule::restoreDraftHistory(bool redo)
{
    m_guard.assertOnUiThread();
    auto& from=redo?m_draftRedo:m_draftUndo;
    auto& to=redo?m_draftUndo:m_draftRedo;
    if(!m_writable||from.empty()){return false;} // 空栈/只读由执行端再次防御。
    to.push_back(m_session.draft);
    m_session.draft=std::move(from.back());from.pop_back();
    m_historyCurrent=m_session.draft; // 下面的就绪重算不得将恢复本身再作为新编辑入栈。
    if(m_panel){m_panel->cancelPendingForHistoryRestore();}
    recomputeReadiness();
    if(m_panel){
        m_panel->setOutcomeMessage(redo?QStringLiteral("已重做草稿编辑（未产生项目修订）")
                                     :QStringLiteral("已撤销草稿编辑（未产生项目修订）"));
    }
    return true;
}

}  // namespace sdurws::ird::modeling
