/**
 * @file   HostDraftChainContractTest.cpp
 * @brief  宿主草稿链契约两段式（UI-T84 acceptance 4 立段——F-911 修复批；
 *         UI-T85 acceptance 3 段 B 翻正向——F-917/F-918 裁决落地批）。
 *
 * 设计依据：
 *   - 契约 tasks/foundation/UI-T84.json acceptance 4（两段式口径，修订
 *     依据 DTB §5.4）＋ tasks/foundation/UI-T85.json acceptance 3（段 B
 *     翻转即 F-917 关闭验收面）；
 *   - 处置文档 docs/superpowers/2026-10-17-host-assembly-lifecycle-gaps-
 *     disposition.md §4 子批 B（挂接后全链验证：保存→查询→恢复）；
 *   - findings.json F-917/F-918 owner 行（所有者裁决 2026-10-18：
 *     requirements hex 铠装对称＋project save schemaVersion 归一化）。
 *
 * 背景说明——本链曾有两个**草稿链格式级缺陷**（UI-T84 批发现登记
 * findings.json，均已于 UI-T85 批按所有者裁决修复落地，此处保留叙事供
 * 追溯）：
 *
 *   F-917（载荷内容编码冲突→hex 铠装）：requirements 域草稿载荷为二进制
 *   帧（encodeRequirementCommandPayload：magic+u32+长度前缀字节串），而
 *   project 工程草稿文件格式的 payload 字段经 appendJsonString 强制 UTF-8
 *   校验——二进制帧保存必被拒。裁决＝域草稿源边界 hexArmor/unhexArmor
 *   对称（ASCII 过 UTF-8 校验，与 modeling 建模域既有铠装惯例同型；铠装
 *   仅在域插件边界内侧——CR-02 ui 只搬运不解析不动）。
 *
 *   F-918（schemaVersion 权威倒置→save 归一化）：ui 域草稿源携带各自
 *   域内载荷版本（requirements=1/modeling=2），而 project 草稿文件按
 *   §8.11 主.次版本编码解释该字段（kSchemaVersionScale=10000）——主版
 *   本 0 区间即 FormatLegacy 拒读，每份草稿"保存成功"却永远读不回。裁决
 *   ＝DraftServiceImpl::save 落盘前 schemaVersion := kSchemaVersionCurrent
 *   （格式所有者写自己的版本，调用方值忽略并留 dev 诊断）；ui 投影字段
 *   改义"域内版本仅呈现/遥测——不落盘"。
 *
 * 因此本文件两段式拆解（两段同构——仅载荷内容源不同）：
 *   段 A（F-911 修复面正向证明）——真实工程存储（ProjectStoreFactory 临时
 *     目录）＋生产写半区（StorePortAdapter 双面）＋生产查询适配器
 *     （StoreDraftQueryPortAdapter→DraftService::list）＋端口合规的文本
 *     载荷模块源，跑通 编辑→落盘→"重开"→restoreOnOpen→adopt 的完整
 *     机制链，断言查询行映射与载荷逐字节保真。文本源是 IModuleDraftSource
 *     端口的合规测试替身（端口契约是被测对象）；其 schemaVersion 取域内
 *     版本值（1——UI-T85 起：F-918 归一化后调用方值在 save 被忽略且不
 *     影响可读回，本替身随之回归生产域源的取值形态，端到端钉住归一化）。
 *   段 B（F-917 关闭验收面——UI-T85 acceptance 3 翻正向）——生产
 *     RequirementsDraftSource 的 hex 铠装载荷全链正向：编辑→saveAll 落
 *     真实工程存储→查询半区可见→重开→restoreOnOpen→adopt 重建工作集，
 *     恢复侧五对象逐一重编码与原帧槽字节**逐字节一致**（曾经的稳定拒绝
 *     钉已随裁决移除——翻转本身即 F-917 的关闭证据）。集成树专属
 *     （requirements_plugin 链接面经 if(TARGET sdurws) 门控——
 *     PresentationBridge 同因先例）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯登记

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

#include <sdurws/ird/requirements/Codec.hpp>            // RequirementCodec（MapClosure 编码面）
#include <sdurws/ird/requirements/CommandHandlers.hpp>  // tryDecodeRequirementCommandPayload（段 B 逐字节对照的帧解码——公共契约头）
#include <sdurws/ird/requirements/Editor.hpp>           // RequirementEditor（域裁决唯一入口）
#include <sdurws/ird/requirements/ObjectTypes.hpp>      // 五对象 token
#include <sdurws/ird/requirements/RequirementTypes.hpp>  // 值模型＋sortEntriesByObjectId
#include <sdurws/ird/requirements/RequirementsPluginAssembly.hpp>  // requirements::kModuleHandle（挂接首参）
#include "RequirementsUiModule.hpp"  // RequirementsDraftSource（段 B 生产被测源——插件私有头经门控 include 面授权，仅本测试目标）
#include <sdurws/ird/project/ProjectStore.hpp>          // ProjectStoreFactory（真实工程存储；project 域类型全限定——WP-14-T11 ::project 兜底注）
#include <sdurws/ird/ui/IDraftController.hpp>           // createDraftController/DraftSessionBinding/SaveTrigger/IModuleDraftSource
#include <sdurws/ird/ui/UiProjections.hpp>              // DraftDocumentProjection/DraftRowProjection（草稿文档/查询行投影）
#include "app/PortAdapters.hpp"                         // StorePortAdapter（写半区双面）＋StoreDraftQueryPortAdapter（读半区真实适配——F-911 修复面本体）

namespace {

namespace core = sdurws::ird::core;
namespace project = sdurws::ird::project;
namespace requirements = sdurws::ird::requirements;
namespace ui = sdurws::ird::ui;
namespace app = sdurws::ird::ui::app;

// =====================================================================
// 夹具 A：端口合规文本载荷源（段 A 专用——IModuleDraftSource 测试替身）
// =====================================================================

/**
 * @brief 段 A 的模块草稿源替身：UTF-8 文本载荷（可过 project 草稿格式
 *        payload 字段 UTF-8 校验的最小保真载体）。
 *
 * 为什么不是直接用 RequirementsDraftSource：requirements 载荷是二进制帧，
 * 与草稿文件格式的兼容性缺陷（F-917）未裁决——若用它，段 A 会死在
 * 载荷编码而非 F-911 修复面（查询半区）上。本替身把变量隔离：载荷内容
 * 换成合法 UTF-8 文本后，机制链（写半区落盘→list→tryLoad→adopt）上的
 * 每一环都是生产真实件。中文文本充当逐字节保真探针（多字节序列任何
 * 转码/截断都会破坏全等断言）。
 *
 * schemaVersion 取域内版本值 1（UI-T85 起随 F-918 归一化同步）：F-918
 * 裁决后调用方传入的 schemaVersion 在 DraftServiceImpl::save 落盘前被
 * 归一化为 kSchemaVersionCurrent（格式所有者写自己的版本——调用方值忽
 * 略），域内版本不再被 FormatLegacy 拒读——本替身随之回归生产域源
 * （requirements=1/modeling=2 同形态）的取值形态，段 A 端到端钉住"域内
 * 版本输入落盘后可读回"的归一化语义（project 单元守卫钉另以字节面直证
 * ——DraftServiceTest.DomainVersionInputNormalizedReadableBackRoundtripPin）。
 */
class TextModuleDraftSource final : public ui::IModuleDraftSource {
public:
    TextModuleDraftSource(std::string moduleId, std::string displayName)
        : m_moduleId(std::move(moduleId)), m_displayName(std::move(displayName))
    {
    }

    std::string displayName() const override { return m_displayName; }

    /// 组装草稿文档：payload 即当前待保存文本（UTF-8 字节直通——ui 只
    /// 搬运不解析，CR-02/D-10）；schemaVersion＝域内版本值（F-918 归一化
    /// 后仅呈现不落盘——save 侧忽略）。
    ui::DraftDocumentProjection buildDraftDocument() const override
    {
        ui::DraftDocumentProjection doc;
        doc.moduleId = m_moduleId;
        doc.schemaVersion = 1;
        doc.payload = m_pending;
        return doc;
    }

    /// 承接恢复文档：记录载荷供逐字节保真断言（恢复链终点的可观测面）。
    void adoptRestoredDocument(const ui::DraftDocumentProjection& document) override
    {
        m_adopted = true;
        m_adoptedPayload = document.payload;
    }

    /// 修订重建（§8.5 迁移面）不在本契约范围内——由 DraftControllerModelTest 覆盖。
    void rebuildOnRevision(const std::string& /*tipRevisionCanonical*/) override {}

    /// 编辑注入（模拟域侧用户输入——置脏前的内容变化源）。
    void edit(std::string text) { m_pending = std::move(text); }

    /// 恢复资格位（adopt 是否被控制器调用过）。
    bool adopted() const { return m_adopted; }
    /// 恢复落地的载荷（与原稿对照的保真基准）。
    const std::string& adoptedPayload() const { return m_adoptedPayload; }

private:
    std::string m_moduleId;
    std::string m_displayName;
    std::string m_pending;         // 待保存文本（编辑注入值）
    bool m_adopted = false;        // adoptRestoredDocument 是否已到达
    std::string m_adoptedPayload;  // 恢复载荷快照
};

// =====================================================================
// 夹具 B：requirements 域装配（PluginPanelTest EditorTest 同款形态——
// 内存闭包仅承载编辑器基线；磁盘面一律走真实工程存储，零替身）
// =====================================================================

/// 合法任务点（I-REQ-5：至少一约束分量；work 段恒启用）。
requirements::TaskPoint makePoint(const std::string& name)
{
    requirements::TaskPoint p;
    p.objectId = core::ObjectId::generate();
    p.name = name;
    p.pose.constrainedDof.z = true;
    p.work = requirements::TaskSegment{true, requirements::SegmentAxis::ToolZ, 1.0};
    return p;
}

/// 测试用闭包字节源（EditorTest 同款——编辑器 loadBaseline 的内存供数）。
class MapClosure final : public requirements::RequirementObjectClosureView {
public:
    void put(const std::string& token, const requirements::RequirementObjectVariant& object)
    {
        const requirements::RequirementCodec codec;
        auto bytes = codec.encode(object, requirements::kCurrentRequirementFormatVersion);
        if (bytes.ok()) {
            byToken_[token] = requirements::RequirementClosureObject{token, bytes.get()};
        }
    }

    void putById(const core::ObjectId& id, const std::string& token,
                 const requirements::RequirementObjectVariant& object)
    {
        const requirements::RequirementCodec codec;
        auto bytes = codec.encode(object, requirements::kCurrentRequirementFormatVersion);
        if (bytes.ok()) {
            byId_[id] = requirements::RequirementClosureObject{token, bytes.get()};
        }
    }

    std::optional<requirements::RequirementClosureObject> tryObjectByToken(
        std::string_view objectTypeToken) const override
    {
        const auto it = byToken_.find(std::string{objectTypeToken});
        if (it == byToken_.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    std::optional<requirements::RequirementClosureObject> tryObject(
        const core::ObjectId& objectId) const override
    {
        const auto it = byId_.find(objectId);
        if (it == byId_.end()) {
            return std::nullopt;
        }
        return it->second;
    }

private:
    std::map<std::string, requirements::RequirementClosureObject> byToken_;
    std::map<core::ObjectId, requirements::RequirementClosureObject> byId_;
};

/// 装配"根＋四集合"基线闭包（集合 id 固定——可复现；EditorTest 同款）。
core::ObjectId fillBaseline(MapClosure& closure, requirements::PointSet points)
{
    requirements::sortEntriesByObjectId(points.entries);
    requirements::RequirementSet root;
    const core::ObjectId pointSetId =
        core::ObjectId::fromCanonical("obj-10000000000000000000000000000001");
    const core::ObjectId regionSetId =
        core::ObjectId::fromCanonical("obj-20000000000000000000000000000002");
    const core::ObjectId condSetId =
        core::ObjectId::fromCanonical("obj-30000000000000000000000000000003");
    const core::ObjectId planSetId =
        core::ObjectId::fromCanonical("obj-40000000000000000000000000000004");
    root.name = "基线需求集";
    root.pointSetRef = pointSetId;
    root.regionSetRef = regionSetId;
    root.conditionSetRef = condSetId;
    root.planSetRef = planSetId;
    closure.put(std::string{requirements::kReqSetObjectType},
                requirements::RequirementObjectVariant{root});
    closure.putById(pointSetId, std::string{requirements::kReqPointSetObjectType},
                    requirements::RequirementObjectVariant{points});
    closure.putById(regionSetId, std::string{requirements::kReqRegionSetObjectType},
                    requirements::RequirementObjectVariant{
                        requirements::RegionSet{}});
    closure.putById(condSetId, std::string{requirements::kReqConditionSetObjectType},
                    requirements::RequirementObjectVariant{
                        requirements::ConditionSet{}});
    closure.putById(planSetId, std::string{requirements::kReqPlanSetObjectType},
                    requirements::RequirementObjectVariant{requirements::PlanSet{}});
    return core::ObjectId::fromCanonical("obj-50000000000000000000000000000005");
}

/// 载入基线的编辑器（失败即测试自身装配错误——异常携域错误详情）。
requirements::RequirementEditor makeLoadedEditor(MapClosure& closure)
{
    requirements::RequirementEditor editor;
    const auto load = editor.loadBaseline(closure);
    if (!load.ok) {
        std::string params;
        for (const auto& kv : load.error.params) {
            params += " " + kv.first + "=" + kv.second;
        }
        throw std::logic_error("夹具装配错误：基线载入失败 code="
                               + std::string(requirements::requirementErrorCodeToken(
                                   load.error.code))
                               + params + " detail=" + load.error.detail);
    }
    return editor;
}

/// 草稿控制器装配（DraftControllerModelTest makeDeps 同款——落盘/UI 两
/// 通道内联直执行：testkit §6.7"测试替身可内联"口径；磁盘面经生产适配器
/// 走真实工程存储，内联≠替身）。
std::unique_ptr<ui::IDraftController> makeController(ui::DraftControllerDeps& depsOut)
{
    depsOut.postToDiskThread = [](std::function<void()> task) -> bool {
        task();  // 内联执行（落盘任务按契约不抛——返回值轨；真实磁盘在适配器对端）
        return true;
    };
    depsOut.postToUiThread = [](std::function<void()> task) -> bool {
        task();  // 回执内联（单线程测试——Marshal 语义等价直执行）
        return true;
    };
    return ui::createDraftController(std::move(depsOut));
}

/// 磁盘清理（幂等＋瞬时锁容忍）：目录句柄的释放与进程/杀软扫描存在
/// 竞态窗口（Windows"另一个程序正在使用此文件"）——失败短候重试一次；
/// 仍失败如实上报（不静默吞——残留会污染下次运行的 createNew 前置）。
void removeDirBestEffort(const std::filesystem::path& dir)
{
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    if (ec) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        ec.clear();
        std::filesystem::remove_all(dir, ec);
    }
    if (ec) {
        std::cerr << "[warn] 测试目录清理失败（可能有残留）: " << dir.string()
                  << " error=" << ec.message() << std::endl;
    }
}

}  // namespace

/** 段 A（UI-T84 acceptance 4 修订口径——F-911 修复面正向证明）：文本载荷
 *  模块源 编辑→saveAll(Manual) 经生产写半区落真实工程存储→生产查询适配
 *  器 list 行映射断言→新控制器（模拟重开）restoreOnOpen→adopt 载荷与原
 *  稿**逐字节一致**。机制链每一环（存储/双面适配器/查询适配器/控制器编
 *  排）均为生产真实件，仅载荷内容源为端口合规替身（F-917 变量隔离——
 *  生产 requirements 载荷链由段 B 正向承接；F-918 归一化已随 UI-T85 落
 *  地——本段 schemaVersion 即域内版本值，保存可读回）。 */
TEST(HostDraftChain, TextPayloadSaveReopenRestoreByteEqual_UI_T84_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-04", "NFR-REL-01"},
                  std::vector<std::string>{"UI-T84-ACC4", "F-911", "draft-full-chain"});

    // ---- 真实工程存储（临时目录——createNew 失败不留半成品契约）----
    const std::filesystem::path storeDir =
        std::filesystem::temp_directory_path() / "ird-uit84-draft-chain";
    removeDirBestEffort(storeDir);  // 幂等清理（上次运行残留——瞬时锁容忍）
    auto created = project::ProjectStoreFactory::createNew(
        storeDir, "draft-chain-test", nullptr, nullptr);
    ASSERT_TRUE(created.store != nullptr) << "测试装配错误：工程存储创建失败";

    // ---- 生产端口半区（真实适配器——F-911 修复面本体，零替身）----
    auto storePort = std::make_shared<app::StorePortAdapter>(std::move(created.store));
    auto queryPort = std::make_shared<app::StoreDraftQueryPortAdapter>(storePort);
    // 归属项目身份（落盘一致性锚——文档 projectId 失配＝对端拒绝跨项目
    // 落盘；生产 open 流由打开报告直供，此处从存储现取同源事实）。
    const core::ProjectId storeProjectId =
        storePort->projectStore().projectId();  // 生产同源（open 流经 store 直取）

    // ---- 会话①：编辑→保存（生产写半区落盘）----
    ui::DraftControllerDeps deps;
    auto controller = makeController(deps);

    ui::DraftSessionBinding binding;
    binding.projectId = storeProjectId;
    binding.writable = true;
    binding.drafts = queryPort;
    binding.store = storePort;  // 双面适配器：IUiDraftStorePort 写半区同源
    controller->bindSession(binding);

    // 挂接段：attachModule 在 bindSession 之后（§10.5 契约次序）。
    // 载荷用含中文的 UTF-8 文本——多字节序列的全等断言是最严的保真探针。
    const std::string kModuleId = "text-demo";
    TextModuleDraftSource source(kModuleId, "文本演示模块");
    controller->attachModule(kModuleId, source);
    source.edit("草稿编辑：P1-草稿（UTF-8 中文保真探针）");
    // 模块侧脏位上报（§10.5 契约——生产由面板编辑流调用；控制器据此
    // 判定 saveAll 的组装范围，未上报＝模块被视为干净、零落盘）。
    controller->notifySessionDirty(kModuleId);
    const ui::DraftDocumentProjection originalDoc = source.buildDraftDocument();
    ASSERT_FALSE(originalDoc.payload.empty());

    // 保存（Manual 触发——真实磁盘落盘经 StorePortAdapter→DraftService；
    // runSaveBlocking 同步等待——返回即落盘完成）。
    const ui::SaveOutcome saved = controller->saveAll(ui::SaveTrigger::Manual);
    ASSERT_EQ(saved.failedCount, std::size_t(0))
        << "全链前置：保存必须零失败（失败=适配器/存储装配错误；首错="
        << (saved.firstError ? saved.firstError->detail : "") << "）";
    ASSERT_EQ(saved.savedCount, std::size_t(1));

    // 查询半区直证（F-911 修复面本体）：生产适配器 list 必须能看到刚落
    // 盘的草稿，且行映射字段正确（修复前 DraftQueryPortStub 恒返空）。
    // 端口无参口径：项目归属由适配器经存储现场自取（open 一会话一存储）。
    const std::vector<ui::DraftRowProjection> rows = queryPort->listDrafts();
    ASSERT_EQ(rows.size(), std::size_t(1))
        << "查询半区断裂：磁盘已有草稿但 list 返空（F-911 原缺陷形态）";
    EXPECT_EQ(rows.front().documentKey, kModuleId);
    EXPECT_EQ(rows.front().moduleId, kModuleId);
    // displayName 留空＝适配器契约（磁盘行自身无显示名权威，零虚构——
    // 域模块显示名由 DraftController 汇总时按挂接模块补齐）。
    EXPECT_TRUE(rows.front().displayName.empty())
        << "查询行不得虚构显示名（UX-02 零哈希/零虚构）";

    // ---- 会话②（模拟重开）：新控制器＋新源＋同源端口组→恢复----
    controller->unbindSession();  // 会话终结（模块表清空——生产关闭路径同款）

    ui::DraftControllerDeps deps2;
    auto controller2 = makeController(deps2);
    controller2->bindSession(binding);  // 同源端口组（真实"重开"——磁盘已带草稿）
    TextModuleDraftSource restoreSource(kModuleId, "文本演示模块");
    controller2->attachModule(kModuleId, restoreSource);
    const ui::RestoreOutcome restored =
        controller2->restoreOnOpen();  // list（生产查询适配器）→tryLoad→adopt

    // 恢复结局断言①：目标模块在恢复名单、无损坏无孤儿（链路完整性）。
    EXPECT_NE(std::find(restored.restoredModules.begin(),
                        restored.restoredModules.end(), kModuleId),
              restored.restoredModules.end())
        << "restoreOnOpen 未恢复磁盘草稿——查询半区断裂（F-911 原缺陷形态）";
    EXPECT_TRUE(restored.corruptModules.empty());
    EXPECT_TRUE(restored.orphanModuleIds.empty());
    // 数据保真断言②：adopt 到达且载荷与原稿**逐字节一致**（std::string
    // 比较为字节级全等——moduleId/schemaVersion/payload 端到端确定性，
    // NFR-COR-01）。
    ASSERT_TRUE(restoreSource.adopted()) << "adoptRestoredDocument 未到达域侧";
    EXPECT_EQ(restoreSource.adoptedPayload(), originalDoc.payload)
        << "恢复载荷与原稿非逐字节一致";

    controller2->unbindSession();
    // 句柄先于磁盘清理释放：storePort 持工程写锁句柄（SA-17）——存活期间
    // remove_all 必然失锁失败（Windows 句柄独占语义）。引用持有人共三处
    // （测试局部 binding／两控制器内部绑定拷贝／端口组变量），全部断开后
    // StorePortAdapter 才真正析构（ProjectStore 隐式排空释放锁）。
    binding.drafts.reset();
    binding.store.reset();
    controller2.reset();
    controller.reset();
    queryPort.reset();
    storePort.reset();
    removeDirBestEffort(storeDir);
}

/** 段 B（UI-T85 acceptance 3——F-917 关闭验收面，段 B 翻正向）：生产
 *  RequirementsDraftSource 的 hex 铠装载荷跑完整草稿链——编辑→saveAll
 *  落真实工程存储（曾经的 F-917 稳定拒绝点：二进制帧被 payload 字段
 *  UTF-8 校验 invalid_argument 拒绝；hex 铠装后必过）→生产查询适配器
 *  list 可见（曾经的 F-918 稳定跳过点：域内版本被 FormatLegacy 拒读；
 *  save 归一化后必现）→新控制器模拟重开 restoreOnOpen→adopt 重建工作
 *  集→恢复侧五对象逐一重编码，与原帧槽字节**逐字节一致**（全链内容零
 *  损——铠装/归一化/存储/适配器/控制器各环端到端确定性）。两处"曾经
 *  稳定失败"翻转为"必须成功"，翻转本身即 F-917/F-918 的关闭证据。 */
TEST(HostDraftChain, RequirementsDraftFullChainByteEqual_UI_T85_F917)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-04", "NFR-REL-01"},
                  std::vector<std::string>{"UI-T85-ACC3", "F-917", "F-918",
                                           "draft-full-chain"});

    // ---- 真实工程存储（独立临时目录——与段 A 互不污染）----
    const std::filesystem::path storeDir =
        std::filesystem::temp_directory_path() / "ird-uit85-req-draft-chain";
    removeDirBestEffort(storeDir);  // 幂等清理（上次运行残留——瞬时锁容忍）
    auto created = project::ProjectStoreFactory::createNew(
        storeDir, "req-draft-chain-test", nullptr, nullptr);
    ASSERT_TRUE(created.store != nullptr) << "测试装配错误：工程存储创建失败";

    auto storePort = std::make_shared<app::StorePortAdapter>(std::move(created.store));
    auto queryPort = std::make_shared<app::StoreDraftQueryPortAdapter>(storePort);
    const core::ProjectId storeProjectId = storePort->projectStore().projectId();

    // ---- 会话①：生产 requirements 装配＋编辑→saveAll（零替身）----
    MapClosure closure;
    requirements::RequirementEditor editor = [&closure]() {
        requirements::PointSet points;
        points.entries.push_back(makePoint("P1"));
        fillBaseline(closure, std::move(points));
        return makeLoadedEditor(closure);
    }();
    requirements::RequirementsUiModule module;
    module.attachEditor(&editor);
    module.session().rootObjectId =
        core::ObjectId::fromCanonical("obj-50000000000000000000000000000005");
    requirements::RequirementsDraftSource source(module, editor);

    ui::DraftControllerDeps deps;
    auto controller = makeController(deps);
    ui::DraftSessionBinding binding;
    binding.projectId = storeProjectId;
    binding.writable = true;
    binding.drafts = queryPort;
    binding.store = storePort;
    controller->bindSession(binding);
    controller->attachModule(requirements::kModuleHandle, source);

    // 编辑（P1→P1-草稿）＋脏位上报——组装出 hex 铠装载荷（F-917 裁决形
    // 态：帧字节的小写 hex 文本，纯 ASCII）。
    requirements::TaskPoint renamed = editor.workingSet().points.entries.front();
    renamed.name = "P1-草稿";
    ASSERT_TRUE(editor.applyEdit(renamed).accepted);
    controller->notifySessionDirty(requirements::kModuleHandle);
    const ui::DraftDocumentProjection originalDoc = source.buildDraftDocument();
    ASSERT_FALSE(originalDoc.payload.empty()) << "前置：铠装载荷必须能组装出来";
    // 铠装形态直证（F-917 的正像——曾经的二进制帧必含非 ASCII 字节）：
    // payload 全部字符落在小写 hex 词表内（ASCII 文本，必过 UTF-8 校验）。
    const bool allHex =
        !originalDoc.payload.empty()
        && originalDoc.payload.find_first_not_of("0123456789abcdef")
               == std::string::npos;
    EXPECT_TRUE(allHex) << "payload 应为小写 hex 铠装文本（F-917 裁决形态），实际前缀: "
                        << originalDoc.payload.substr(0, 32);

    // 保存必成功（F-917 翻转点：曾经此调用稳定抛 invalid_argument——
    // 对端 appendJsonString 拒绝非 UTF-8 载荷；铠装后为合法 ASCII 文本）。
    const ui::SaveOutcome saved = controller->saveAll(ui::SaveTrigger::Manual);
    EXPECT_EQ(saved.failedCount, std::size_t(0))
        << "全链前置：保存必须零失败（失败=铠装/归一化/适配器装配回归；首错="
        << (saved.firstError ? saved.firstError->detail : "") << "）";
    ASSERT_EQ(saved.savedCount, std::size_t(1));

    // 查询半区可见（F-918 翻转点：曾经域内版本 schemaVersion=1 落盘即
    // FormatLegacy 被稳定跳过、list 返空；save 归一化后草稿行必现）。
    const std::vector<ui::DraftRowProjection> rows = queryPort->listDrafts();
    ASSERT_EQ(rows.size(), std::size_t(1))
        << "归一化草稿必须在查询半区可见（F-918 原缺陷形态＝list 返空）";
    EXPECT_EQ(rows.front().moduleId, requirements::kModuleHandle);

    // ---- 会话②（模拟重开）：新控制器＋新模块/编辑器/源＋同源端口组----
    controller->unbindSession();  // 会话终结（模块表清空——生产关闭路径同款）

    ui::DraftControllerDeps deps2;
    auto controller2 = makeController(deps2);
    controller2->bindSession(binding);
    // 恢复侧装配（PluginPanelTest DraftSourceRoundtrip_ACC4 同款形态——
    // 先以空集合闭包载入基线，adopt 再以恢复载荷重建）。
    MapClosure restoreClosure;
    fillBaseline(restoreClosure, requirements::PointSet{});
    requirements::RequirementEditor restoredEditor =
        makeLoadedEditor(restoreClosure);
    requirements::RequirementsUiModule restoreModule;
    restoreModule.attachEditor(&restoredEditor);
    requirements::RequirementsDraftSource restoreSource(restoreModule,
                                                        restoredEditor);
    controller2->attachModule(requirements::kModuleHandle, restoreSource);
    const ui::RestoreOutcome restored = controller2->restoreOnOpen();

    // 恢复结局断言①：目标模块在恢复名单、无损坏无孤儿（链路完整性——
    // adopt 的解铠装/帧解码/对象解码/基线重建四步全过）。
    EXPECT_NE(std::find(restored.restoredModules.begin(),
                        restored.restoredModules.end(),
                        requirements::kModuleHandle),
              restored.restoredModules.end())
        << "restoreOnOpen 未恢复 requirements 草稿——链路在某环断裂";
    EXPECT_TRUE(restored.corruptModules.empty());
    EXPECT_TRUE(restored.orphanModuleIds.empty());
    // 恢复资格位（adoptRestoredDocument 到达且完成——应用资格随此位）。
    EXPECT_TRUE(restoreModule.session().restoredDraftPending);
    // 语义锚（诊断可读性——逐字节断言失败时先看这里定位是哪个对象走样）。
    ASSERT_EQ(restoredEditor.workingSet().points.entries.size(), std::size_t(1));
    EXPECT_EQ(restoredEditor.workingSet().points.entries.front().name, "P1-草稿");

    // 数据保真断言②（本测试核心——"内容逐字节一致"全链断言）：恢复侧
    // 五对象逐一以 RequirementCodec 重编码（确定性编码 NFR-COR-02——同
    // 内容必得同字节），与原帧对应槽的 canonical 字节全等对照。槽序＝
    // §4.1 表行序（根/点集/区域集/条件集/计划集——buildDraftDocument
    // 的确定性槽序）。测试自备 hex 解码作观测仪器（生产铠装私有于域插
    // 件边界内侧——CR-02：观测不改变被产行为）。
    auto unhex = [](const std::string& text) -> std::vector<std::uint8_t> {
        auto nibble = [](char c) -> int {
            if (c >= '0' && c <= '9') { return c - '0'; }
            if (c >= 'a' && c <= 'f') { return c - 'a' + 10; }
            return -1;
        };
        std::vector<std::uint8_t> out;
        out.reserve(text.size() / 2);
        for (std::size_t i = 0; i + 1 < text.size(); i += 2) {
            out.push_back(static_cast<std::uint8_t>(
                (nibble(text[i]) << 4) | nibble(text[i + 1])));
        }
        return out;
    };
    const auto originalFrame = requirements::tryDecodeRequirementCommandPayload(
        unhex(originalDoc.payload));
    ASSERT_TRUE(originalFrame.has_value())
        << "前置：原稿载荷必须能解铠装解帧（组装面自洽）";
    ASSERT_EQ(originalFrame->objects.size(), std::size_t(5))
        << "前置：Apply 载荷＝五对象槽（根＋四集合）";
    const requirements::RequirementCodec codec;
    const requirements::RequirementWorkingSet& ws = restoredEditor.workingSet();
    auto expectSlotByteEqual =
        [&codec, &originalFrame](std::size_t slot,
                                 const requirements::RequirementObjectVariant& object,
                                 const char* what) {
            const auto encoded =
                codec.encode(object, requirements::kCurrentRequirementFormatVersion);
            ASSERT_TRUE(encoded.ok())
                << "恢复侧重编码失败（" << what << "）——闭包装配缺陷";
            EXPECT_EQ(encoded.get(), originalFrame->objects[slot].objectBytes)
                << "恢复内容与原稿槽字节不一致（" << what
                << "，槽位 " << slot << "）——全链保真断裂";
        };
    expectSlotByteEqual(0, requirements::RequirementObjectVariant{ws.root}, "根");
    expectSlotByteEqual(1, requirements::RequirementObjectVariant{ws.points}, "点集");
    expectSlotByteEqual(2, requirements::RequirementObjectVariant{ws.regions}, "区域集");
    expectSlotByteEqual(3, requirements::RequirementObjectVariant{ws.conditions}, "条件集");
    expectSlotByteEqual(4, requirements::RequirementObjectVariant{ws.plans}, "计划集");

    controller2->unbindSession();
    // 句柄先于磁盘清理释放（段 A 同因——见其收口注）。
    binding.drafts.reset();
    binding.store.reset();
    controller2.reset();
    controller.reset();
    queryPort.reset();
    storePort.reset();
    removeDirBestEffort(storeDir);
}

/** 查询半区关闭真值钉（UI-T84——验收建议 3 适配器半区；F-910 隔离防线
 *  的读轨面）：上下文关闭后 listDrafts 如实返空而非抛——关闭决议/Draining
 *  呈现链会在上下文失效后仍触发查询（UI-T84 实测），读轨按 §4.7 拒绝即
 *  "无草稿"真值形态；钉住该语义防重构回退为穿透抛出。 */
TEST(HostDraftChain, ClosedContextListReturnsEmptyTruth_UI_T84)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-04", "NFR-REL-01"},
                  std::vector<std::string>{"UI-T84", "F-910"});

    const std::filesystem::path storeDir =
        std::filesystem::temp_directory_path() / "ird-uit84-closed-truth";
    removeDirBestEffort(storeDir);  // 幂等清理（瞬时锁容忍）
    auto created = project::ProjectStoreFactory::createNew(
        storeDir, "closed-truth-test", nullptr, nullptr);
    ASSERT_TRUE(created.store != nullptr) << "测试装配错误：工程存储创建失败";
    auto storePort = std::make_shared<app::StorePortAdapter>(std::move(created.store));
    auto queryPort = std::make_shared<app::StoreDraftQueryPortAdapter>(storePort);

    // 关闭前：空清单（新存储无草稿——常态空）。
    EXPECT_TRUE(queryPort->listDrafts().empty());

    // 显式关闭（requestClose 排空——零在途即同步完成）后：仍返空不抛
    // （对端读轨此时抛 ContextClosed，适配器折叠为"无草稿"真值——F-910
    // 隔离语义；若回退为穿透抛出，本断言红）。
    (void)storePort->projectStore().requestClose();
    EXPECT_TRUE(queryPort->listDrafts().empty())
        << "关闭上下文的查询必须如实返空（不抛）——呈现链失效期读轨真值";

    queryPort.reset();
    storePort.reset();
    removeDirBestEffort(storeDir);
}
