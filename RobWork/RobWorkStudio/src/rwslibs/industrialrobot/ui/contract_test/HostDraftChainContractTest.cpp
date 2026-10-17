/**
 * @file   HostDraftChainContractTest.cpp
 * @brief  宿主草稿链契约两段式（UI-T84——F-911 修复批 acceptance 4）。
 *
 * 设计依据：
 *   - 契约 tasks/foundation/UI-T84.json acceptance 4（修订后两段式口径，
 *     修订依据 DTB §5.4——原"requirements 逐字节全链"文本与下述 F-917/
 *     F-918 事实冲突）；
 *   - 处置文档 docs/superpowers/2026-10-17-host-assembly-lifecycle-gaps-
 *     disposition.md §4 子批 B（挂接后全链验证：保存→查询→恢复）。
 *
 * 背景说明——本批执行中发现两个**草稿链格式级缺陷**（均已登记
 * findings.json，跨域契约归所有者裁决，本任务不私有越权修复）：
 *
 *   F-917（载荷内容编码冲突）：requirements 域草稿载荷为二进制帧
 *   （encodeRequirementCommandPayload：magic+u32+长度前缀字节串，
 *   CommandHandlers.cpp），而 project 工程草稿文件格式的 payload 字段经
 *   appendJsonString 强制 UTF-8 校验（Codec.cpp）——二进制帧必被拒绝。
 *   modeling 域载荷同 CommandHandlers 帧（encodeModelCommandPayload），
 *   同款嫌疑。裁决方向：载荷 base64/hex 文本化，或草稿格式二进制安全化。
 *
 *   F-918（schemaVersion 权威倒置）：ui 侧 DraftDocumentProjection 契约
 *   写"草稿格式版本（域组装方填写）"，域侧遂填各自载荷内版本
 *   （requirements=1 / modeling=2）；而 project 草稿文件把该字段按
 *   §8.11 主.次版本编码解释（kSchemaVersionScale=10000，当前支持
 *   10000＝v1.0）——主版本 0（1、2 都落此区间）即 FormatLegacy 拒读。
 *   且 DraftServiceImpl::save 不校验该字段：**每份草稿都"保存成功"落盘
 *   却永远读不回**（list/tryLoad 稳定跳过）。裁决方向：格式版本权威归
 *   project（save 归一化为 kSchemaVersionCurrent 或落盘前校验拒写），
 *   ui 投影字段改义为"域版本仅呈现"。
 *
 * 因此本文件两段式拆解：
 *   段 A（F-911 修复面正向证明）——真实工程存储（ProjectStoreFactory 临时
 *     目录）＋生产写半区（StorePortAdapter 双面）＋生产查询适配器
 *     （StoreDraftQueryPortAdapter→DraftService::list）＋端口合规的文本
 *     载荷模块源，跑通 编辑→落盘→"重开"→restoreOnOpen→adopt 的完整
 *     机制链，断言查询行映射与载荷逐字节保真。文本源是 IModuleDraftSource
 *     端口的合规测试替身（端口契约是被测对象）；其 schemaVersion 消费
 *     project 公共常量 kSchemaVersionCurrent（现行事实下唯一可读回的
 *     取值——F-918 裁决后随生产语义同步修订）。
 *   段 B（F-917 事实钉）——生产 RequirementsDraftSource 的二进制载荷在
 *     现行格式下的稳定拒绝（payload 字段 UTF-8 校验 invalid_argument
 *     fail-fast 逃逸），作为缺陷现状的可执行证据；所有者裁决方案落地后
 *     本段应翻转为与段 A 同构的正向全链断言（翻转即 F-917 关闭验收面）。
 * 集成树专属（requirements_plugin 链接面经 if(TARGET sdurws) 门控——
 * PresentationBridge 同因先例）。
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
#include <sdurws/ird/requirements/Editor.hpp>           // RequirementEditor（域裁决唯一入口）
#include <sdurws/ird/requirements/ObjectTypes.hpp>      // 五对象 token
#include <sdurws/ird/requirements/RequirementTypes.hpp>  // 值模型＋sortEntriesByObjectId
#include <sdurws/ird/requirements/RequirementsPluginAssembly.hpp>  // requirements::kModuleHandle（挂接首参）
#include "RequirementsUiModule.hpp"  // RequirementsDraftSource（段 B 生产被测源——插件私有头经门控 include 面授权，仅本测试目标）
#include <sdurws/ird/project/ProjectStore.hpp>          // ProjectStoreFactory（真实工程存储；project 域类型全限定——WP-14-T11 ::project 兜底注）
#include <sdurws/ird/project/PersistenceFormat.hpp>     // kSchemaVersionCurrent（草稿文件格式版本权威——段 A 消费，F-918 注）
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
 * schemaVersion 消费 project 公共常量 kSchemaVersionCurrent 而非域内
 * 载荷版本：F-918 登记的现行事实——草稿文件该字段按工程格式主.次版本
 * 解释（10000=1.0），域内版本（1/2）会被 FormatLegacy 拒读；生产域源
 * 修订随 F-918 裁决走，本替身只取"现行格式下唯一可读回"的权威常量。
 */
class TextModuleDraftSource final : public ui::IModuleDraftSource {
public:
    TextModuleDraftSource(std::string moduleId, std::string displayName)
        : m_moduleId(std::move(moduleId)), m_displayName(std::move(displayName))
    {
    }

    std::string displayName() const override { return m_displayName; }

    /// 组装草稿文档：payload 即当前待保存文本（UTF-8 字节直通——ui 只
    /// 搬运不解析，CR-02/D-10）。
    ui::DraftDocumentProjection buildDraftDocument() const override
    {
        ui::DraftDocumentProjection doc;
        doc.moduleId = m_moduleId;
        doc.schemaVersion =
            static_cast<std::uint32_t>(project::kSchemaVersionCurrent);
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
 *  排）均为生产真实件，仅载荷内容源为端口合规替身（F-917/F-918 变量隔
 *  离——两缺陷的钉在段 B 与 findings 登记）。 */
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

/** 段 B（F-917 事实钉——缺陷现状的可执行证据）：生产 RequirementsDraft
 *  Source 的二进制载荷帧在现行工程草稿格式（payload 字段 UTF-8 校验）下
 *  必被稳定拒绝，且拒绝形态为调用方违约 fail-fast（invalid_argument 经
 *  落盘 future 通道在 saveAll 调用线程重抛——禁吞错语义的线程边界表达）。
 *  所有者裁决编码方案（base64/hex 文本化或草稿格式二进制安全化）落地后，
 *  本测试应翻转为与段 A 同构的正向全链断言——翻转即 F-917 关闭的验收面。 */
TEST(HostDraftChain, RequirementsBinaryPayloadStableRejection_UI_T84_F917)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-04", "NFR-REL-01"},
                  std::vector<std::string>{"F-917", "UI-T84-ACC4"});

    // ---- 真实工程存储（独立临时目录——与段 A 互不污染）----
    const std::filesystem::path storeDir =
        std::filesystem::temp_directory_path() / "ird-uit84-f917-pin";
    removeDirBestEffort(storeDir);  // 幂等清理（上次运行残留——瞬时锁容忍）
    auto created = project::ProjectStoreFactory::createNew(
        storeDir, "f917-pin-test", nullptr, nullptr);
    ASSERT_TRUE(created.store != nullptr) << "测试装配错误：工程存储创建失败";

    auto storePort = std::make_shared<app::StorePortAdapter>(std::move(created.store));
    auto queryPort = std::make_shared<app::StoreDraftQueryPortAdapter>(storePort);
    const core::ProjectId storeProjectId = storePort->projectStore().projectId();

    // ---- 生产 requirements 装配（真实编辑器＋真实草稿源，零替身）----
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

    // 编辑（P1→P1-草稿）＋脏位上报——组装出的是 requirements 二进制帧载荷。
    requirements::TaskPoint renamed = editor.workingSet().points.entries.front();
    renamed.name = "P1-草稿";
    ASSERT_TRUE(editor.applyEdit(renamed).accepted);
    controller->notifySessionDirty(requirements::kModuleHandle);
    const ui::DraftDocumentProjection doc = source.buildDraftDocument();
    ASSERT_FALSE(doc.payload.empty()) << "前置：二进制帧载荷必须能组装出来";

    // 保存必以 fail-fast 异常收场（对端 appendJsonString 对非 UTF-8 载荷
    // 抛 invalid_argument——调用方违约不吞，经 runSaveBlocking 的 promise
    // 异常通道原样重抛回调用线程）。消息稳定含 payload 字段 UTF-8 标记
    // （F-917 钉子——裁决方案落地后此断言随翻转移除）。
    bool rejected = false;
    std::string rejectDetail;
    try {
        (void)controller->saveAll(ui::SaveTrigger::Manual);
    } catch (const std::exception& error) {
        rejected = true;
        rejectDetail = error.what();
    }
    EXPECT_TRUE(rejected)
        << "二进制载荷应被拒绝（F-917 现状钉）——静默成功意味着钉子失效";
    EXPECT_NE(rejectDetail.find("无效 UTF-8"), std::string::npos)
        << "异常应稳定指向 payload 字段 UTF-8 校验，实际：" << rejectDetail;

    // 磁盘零半成品：拒绝发生在 codec::dump（写门卫之前）——drafts/ 下不得
    // 出现该草稿行（失败可恢复 Q2 语义：不留未知状态文档）。
    EXPECT_TRUE(queryPort->listDrafts().empty())
        << "被拒绝的载荷不应在磁盘留下草稿行";

    controller->unbindSession();
    binding.drafts.reset();  // 写锁句柄释放链（段 A 同因——见收口注）
    binding.store.reset();
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
