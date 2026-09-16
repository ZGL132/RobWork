/**
 * @file   DraftServiceTest.cpp
 * @brief  草稿服务单元测试（PRJ-T12）——acceptance 四条逐条落位为具名
 *         用例：PRJ-TX-6 草稿用例（定时落盘→破坏→重开恢复/保留＋放弃
 *         语义）、StaleRevisionRejected 冲突路径＋多模块汇总投影、
 *         生命周期边界（§8.1 数据结构区别/§8.6 只读与关闭/60 s 周期
 *         边界）、P-PR-1 来源标记透传自证。
 *
 * 设计依据：
 *   - units/project.md §5.4（DraftService 契约表）、§8.1～§8.6（草稿
 *     生命周期语义——用例锚点逐条标注）、§4.1 drafts 行／§4.4.5（磁盘
 *     编址与归属三元组）、§9.6～§9.7（写门卫/在途票据）；
 *   - 需求 PM-04（保存与应用分离/StaleRevisionRejected）、PM-07（只读
 *     禁编辑）、PM-15（恢复横幅数据面）、NFR-COR-02（确定性投影）；
 *   - 任务契约 tasks/foundation/PRJ-T12.json acceptance 1～4。
 *
 * 测试域语义约定：payload 一律为测试自有的 canonical 字节（域语义测试
 *   内部自洽——project 不解释）；损坏注入用"截断 JSON／垃圾字节"裸写
 *   磁盘（前置数据不经被测代码，保证前置不依赖被测正确性——既定惯例）。
 */

#include <windows.h>

#include <gtest/gtest.h>

#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/project/CommandService.hpp>
#include <sdurws/ird/project/DraftService.hpp>
#include <sdurws/ird/project/PersistenceFormat.hpp>
#include <sdurws/ird/project/ProjectStore.hpp>
#include <sdurws/ird/project/QueryPort.hpp>
#include <sdurws/ird/project/StoreTypes.hpp>

#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

#include "Codec.hpp"
#include "ProjectStoreImpl.hpp"
#include "StubHandlers.hpp"
#include "TxEngine.hpp"

namespace fs = std::filesystem;

using namespace sdurws::ird;                       // NOLINT——测试域惯例
using namespace sdurws::ird::project;              // NOLINT
namespace pd = sdurws::ird::project;               // StoreTypes/工厂短名
using sdurws::ird::project::codec::parseDraftDocument;

namespace {

// ---------------------------------------------------------------------
// fake：诊断 sink（用户级码与开发级消息捕获——P-PR-6 断言面；与
// ProjectStoreTest 同款形态，匿名命名空间内独立定义）
// ---------------------------------------------------------------------

class CapturingSink : public IDiagnosticsSink {
public:
    std::vector<core::DiagnosticRecord> reports;
    std::vector<std::pair<std::string, std::string>> devs;

    void report(const core::DiagnosticRecord& record) override
    {
        reports.push_back(record);
    }

    void reportDev(const std::string& channel,
                   const std::string& message) override
    {
        devs.emplace_back(channel, message);
    }

    /// 码值逐字匹配（P-PR-6：码值＝diagnostics.md §4.6 收编清单）。
    bool hasUserCode(const std::string& code) const
    {
        for (const auto& r : reports) {
            if (r.code == code) {
                return true;
            }
        }
        return false;
    }

    bool devContains(const std::string& needle) const
    {
        for (const auto& d : devs) {
            if (d.second.find(needle) != std::string::npos) {
                return true;
            }
        }
        return false;
    }
};

// ---------------------------------------------------------------------
// 磁盘与断言辅助（ProjectStoreTest 同款惯例）
// ---------------------------------------------------------------------

/// 二进制整读；读失败显性失败（不留"读不到＝内容不符"的假阳性通道）。
std::string readAll(const fs::path& file)
{
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        ADD_FAILURE() << "无法读取文件: " << file.string();
        return {};
    }
    return std::string{std::istreambuf_iterator<char>(in),
                       std::istreambuf_iterator<char>()};
}

/// 裸写前置文件（损坏注入/残留构造——前置不经被测代码）。
bool writeRaw(const fs::path& file, const std::string& bytes)
{
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    if (ec) {
        ADD_FAILURE() << "建前置目录失败: " << ec.message();
        return false;
    }
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (!out) {
        ADD_FAILURE() << "无法写前置文件: " << file.string();
        return false;
    }
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(out);
}

/// 目录一级子目录计数（revisions/ 只增断言的观测点——§8.1）。
std::size_t subdirectoryCount(const fs::path& dir)
{
    std::error_code ec;
    if (!fs::exists(dir, ec) || ec) {
        return 0;
    }
    std::size_t n = 0;
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (entry.is_directory()) {
            ++n;
        }
    }
    return n;
}

/// 目录一级常规文件计数（drafts/ 覆盖写断言的观测点——§8.1）。
std::size_t regularFileCount(const fs::path& dir)
{
    std::error_code ec;
    if (!fs::exists(dir, ec) || ec) {
        return 0;
    }
    std::size_t n = 0;
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (entry.is_regular_file()) {
            ++n;
        }
    }
    return n;
}

/// 断言动作抛出指定稳定码的 StoreError 并返回异常（ProjectStoreTest 同款）。
template <class Fn>
StoreError expectStoreError(StoreErrorCode code, Fn&& action)
{
    try {
        action();
        ADD_FAILURE() << "期望 StoreError("
                      << static_cast<int>(code) << ") 未抛出";
    } catch (const StoreError& e) {
        EXPECT_EQ(e.code(), code) << "稳定码不符: " << e.what();
        return e;
    }
    throw std::runtime_error("expectStoreError: action did not throw");
}

}  // namespace

// ---------------------------------------------------------------------
// 用例组：DraftService（套件名登记入 ird-test-report.json）
// ---------------------------------------------------------------------

/**
 * 测试夹具：套件级临时总根（进程级隔离）；各用例独立项目目录，项目
 * 一律经 createNew 产出（可写上下文）；需要只读/关闭态的用例在用例内
 * 显式重开或 requestClose。
 */
class DraftServiceTest : public ::testing::Test {
protected:
    static void SetUpTestSuite()
    {
        std::error_code ec;
        s_base = fs::temp_directory_path(ec) / "ird_prj_draft_test"
                 / std::to_string(::GetCurrentProcessId());
        ASSERT_FALSE(ec);
        fs::create_directories(s_base, ec);
        if (ec) {
            std::cerr << "无法创建测试根目录: " << s_base.string() << " ("
                      << ec.message() << ")\n";
            std::exit(2);
        }
    }

    static void TearDownTestSuite()
    {
        // 总根清理：失败保留现场（"TempDir 失败保留"惯例），不静默掩盖。
        std::error_code ec;
        fs::remove_all(s_base, ec);
        if (ec) {
            std::cerr << "警告：测试根目录清理失败（保留现场）: "
                      << s_base.string() << " (" << ec.message() << ")\n";
        }
    }

    void SetUp() override
    {
        std::error_code ec;
        m_dir = s_base / ("case" + std::to_string(++s_caseCounter))
                / "proj.rwdesign";
        ASSERT_FALSE(ec);
    }

    /// 新建项目并返回打开结果（注入捕获 sink——诊断断言面）。
    pd::OpenStoreResult createNew(std::string_view name = "Draft Test Project")
    {
        return ProjectStoreFactory::createNew(m_dir, name, nullptr,
                                              m_sink.get());
    }

    /// 存储上下文实现句柄（内部通道——executeCommit/注册表）。
    [[nodiscard]] ProjectStoreImpl& impl(pd::ProjectStore& store) const
    {
        auto* p = dynamic_cast<ProjectStoreImpl*>(&store);
        EXPECT_NE(p, nullptr);
        return *p;
    }

    /// 从磁盘 HEAD 读取提交指针（地面事实——不依赖被测内存状态）。
    [[nodiscard]] HeadRecord readHeadFromDisk() const
    {
        return sdurws::ird::project::codec::parseHeadRecord(
            readAll(m_dir / "HEAD"));
    }

    /// 经内部写通道提交一次合法修订（分支/父修订自磁盘 HEAD 读取——
    /// tip 前进的驱动器；stale/投影用例与"恰一修订"断言共用）。
    tx::CommitResult commitOne(pd::ProjectStore& store,
                               std::size_t payloadBytes = 32)
    {
        const HeadRecord head = readHeadFromDisk();
        tx::CommitPlan plan;
        plan.revisionId = RevisionId::generate();
        plan.branchId = head.branchId;            // 活动分支＝HEAD 所在分支
        plan.parentRevisionId = head.revisionId;  // 父＝当前 tip
        plan.committedAtUtc = "2026-09-16T02:00:00Z";
        tx::PlannedObject obj;
        obj.oid = ObjectId::generate();
        obj.objectTypeToken = "RobotDesign";
        obj.payload.assign(payloadBytes, 'D');
        plan.newObjects.push_back(std::move(obj));
        revindex::TipUpdate tip;
        tip.branchId = head.branchId;
        tip.newTip = plan.revisionId;  // 每次提交 tip 恰推进（§4.5）
        plan.metadataDelta.tipUpdate = tip;
        plan.command.commandType = "add-mass-point";  // ^[a-z0-9-]{3,64}
        plan.command.payloadFormatVersion = 1;
        plan.command.payloadCanonical = "{\"point\":1}";
        plan.command.summary = "draft test commit";
        return impl(store).executeCommit(plan);
    }

    /// 草稿文档构造辅助：归属取自上下文（projectId/主分支），基线默认
    /// ＝当前磁盘 HEAD 修订（初始态 r0——"新鲜草稿"）；origin/savedAtUtc
    /// 由调用方给足（§4.4.5 必填字段全量）。
    [[nodiscard]] DraftDocument makeDraft(pd::ProjectStore& store,
                                          const std::string& moduleId,
                                          const std::string& payload,
                                          DraftOrigin origin,
                                          core::RevisionId base) const
    {
        DraftDocument doc;
        doc.projectId = store.projectId();
        doc.branchId
            = store.query().currentMetadata().record.primaryBranchId;
        doc.moduleId = moduleId;
        doc.baseRevisionId = base;
        doc.payload = payload;
        doc.savedAtUtc = "2026-09-16T03:00:00Z";
        doc.origin = origin;
        return doc;
    }

    /// 主分支快捷（信封/保存的目标分支）。
    [[nodiscard]] core::BranchId primaryBranch(pd::ProjectStore& store) const
    {
        return store.query().currentMetadata().record.primaryBranchId;
    }

    /// drafts/ 目录路径（残留/文件计数断言的锚点）。
    [[nodiscard]] fs::path draftsRoot() const { return m_dir / "drafts"; }

    /// 草稿文件路径（§4.1 编址：<module>.draft.json）。
    [[nodiscard]] fs::path draftFile(const core::BranchId& branch,
                                     const std::string& moduleId) const
    {
        return draftsRoot() / branch.toCanonical()
            / (moduleId + ".draft.json");
    }

    /// revisions/ 目录子目录计数（"保存不产生修订"断言的观测点——PM-04）。
    [[nodiscard]] std::size_t revisionDirCount() const
    {
        return subdirectoryCount(m_dir / "revisions");
    }

    /// 磁盘 HEAD 修订身份（保存前后不变断言——HEAD 未被触碰）。
    [[nodiscard]] core::RevisionId headRevisionOnDisk() const
    {
        return readHeadFromDisk().revisionId;
    }

    fs::path m_dir;  ///< 用例专属项目目录
    std::shared_ptr<CapturingSink> m_sink{std::make_shared<CapturingSink>()};

    static fs::path s_base;
    static int s_caseCounter;
};

fs::path DraftServiceTest::s_base;
int DraftServiceTest::s_caseCounter = 0;

// =====================================================================
// acceptance 1（PRJ-TX-6）：定时落盘（保存与应用分离）→破坏→重开恢复
// ／.new 残留清理／旧草稿保留；放弃草稿语义（DiscardResult）
// =====================================================================

/**
 * acceptance 1 之 PM-04 保存与应用分离＋round-trip：
 *   save 落盘成功且不产生任何修订（revisions/ 计数不变＋HEAD 不变）；
 *   tryLoad 原样读回（全字段一致——落盘即生效，§4.1 drafts 行"保存即
 *   生效（非修订）"）。
 */
TEST_F(DraftServiceTest, SaveThenLoadRoundTripProducesNoRevision)
{
    auto opened = createNew();
    auto& store = *opened.store;
    auto& drafts = store.drafts();
    const auto branch = primaryBranch(store);
    const auto base = headRevisionOnDisk();
    const auto revisionsBefore = revisionDirCount();

    // 保存（autosave——定时落盘的入口形态；定时器归 ui，本服务只提供
    // 落盘——§8.2，60 s 周期边界由 AutosaveAndManual 用例承接）。
    const std::string payload = "{\"model\":\"draft-v1\"}";
    const DraftDocument doc = makeDraft(store, "modeling", payload,
                                        DraftOrigin::Autosave, base);
    const SaveResult saved = drafts.save(doc);
    ASSERT_TRUE(saved.ok) << "保存失败: "
                          << (saved.error ? saved.error->what() : "?");

    // PM-04 断言：保存不产生修订——revisions/ 目录计数不变＋HEAD 不变。
    EXPECT_EQ(revisionDirCount(), revisionsBefore);
    EXPECT_EQ(headRevisionOnDisk(), base);

    // 磁盘编址（§4.1）：drafts/<branch-id>/modeling.draft.json 唯一文件；
    // 崩溃残留 .new/.bak 不应存在（正常保存无残留）。
    EXPECT_EQ(regularFileCount(draftsRoot() / branch.toCanonical()), 1U);
    EXPECT_FALSE(fs::exists(draftFile(branch, "modeling").wstring() + L".new"));
    EXPECT_FALSE(fs::exists(draftFile(branch, "modeling").wstring() + L".bak"));

    // round-trip：tryLoad 原样读回（值语义深拷贝——PA-3）。
    std::vector<core::DiagnosticRecord> diags;
    const auto loaded = drafts.tryLoad(branch, "modeling", diags);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(*loaded, doc);
    EXPECT_TRUE(diags.empty()) << "正常加载不应产出诊断";
}

/**
 * acceptance 1 之破坏→重开：current 被截断（JSON 不可解析）后重开项目，
 * tryLoad 产出 draft-corrupt 用户级诊断（PRJ-RECOVERY-ORPHAN-DRAFT——
 * diagnostics.md §8.2 映射行）并自 .bak 恢复上一版（旧草稿保留——§8.4
 * "任务约束§五.6 D-12 模块粒度单文件＋.bak 轮换"）。
 */
TEST_F(DraftServiceTest, CorruptCurrentRecoversFromBakAfterReopen)
{
    auto opened = createNew();
    auto& store = *opened.store;
    auto& drafts = store.drafts();
    const auto branch = primaryBranch(store);
    const auto base = headRevisionOnDisk();

    // 两代草稿：v1（autosave）→v2（manual）——落盘协议把 v1 轮换进 .bak。
    const std::string v1 = "{\"model\":\"draft-v1\"}";
    const std::string v2 = "{\"model\":\"draft-v2\"}";
    ASSERT_TRUE(drafts
                    .save(makeDraft(store, "modeling", v1,
                                    DraftOrigin::Autosave, base))
                    .ok);
    ASSERT_TRUE(drafts
                    .save(makeDraft(store, "modeling", v2,
                                    DraftOrigin::Manual, base))
                    .ok);

    // 破坏注入：截断 current 的 JSON（前置裸写——不经被测代码）。.bak
    // 仍是完整 v1（上一版轮换事实由前置保存序列保证）。
    ASSERT_TRUE(writeRaw(draftFile(branch, "modeling"), "{\"model\":\"dra"));
    ASSERT_FALSE(fs::exists(draftFile(branch, "modeling").wstring() + L".new"));

    // 重开（close→open 同路径——崩溃/破坏后的恢复场景入口）。
    const auto projectId = store.projectId();
    store.requestClose();
    ASSERT_TRUE(store.closed());
    pd::OpenStoreRequest request;
    request.path = m_dir;
    request.diagnostics = m_sink.get();
    auto reopened = ProjectStoreFactory::open(request);
    ASSERT_TRUE(reopened.writable);
    ASSERT_EQ(reopened.store->projectId(), projectId);
    auto& reopenedDrafts = reopened.store->drafts();

    // 恢复加载：返回 .bak 的 v1（不是 current 残骸）＋损坏诊断双通道。
    std::vector<core::DiagnosticRecord> diags;
    const auto recovered = reopenedDrafts.tryLoad(branch, "modeling", diags);
    ASSERT_TRUE(recovered.has_value()) << "应自 .bak 恢复上一版";
    EXPECT_EQ(recovered->payload, v1);
    EXPECT_EQ(recovered->origin, DraftOrigin::Autosave);
    ASSERT_FALSE(diags.empty()) << "损坏恢复必须产出诊断（diags 通道）";
    ASSERT_TRUE(m_sink->hasUserCode("PRJ-RECOVERY-ORPHAN-DRAFT"))
        << "损坏诊断必须经 sink 上报（双通道同源——RecoveryReport 口径）";

    // 旧草稿保留：.bak 文件在恢复后原样保留（只读恢复不删除现场）。
    EXPECT_TRUE(fs::exists(draftFile(branch, "modeling").wstring() + L".bak"));
}

/**
 * acceptance 1 之 .new 残留清理：保存崩溃现场（.new 残留）在重开后由
 * tryLoad 丢弃——current/.bak 保留并报告（§8.4"丢弃 .new、保留
 * current/.bak 并报告"F10）。残留清理经开发诊断通道保留可见性。
 */
TEST_F(DraftServiceTest, StaleNewResidueDiscardedCurrentKeptOnReopen)
{
    auto opened = createNew();
    auto& store = *opened.store;
    auto& drafts = store.drafts();
    const auto branch = primaryBranch(store);
    const auto base = headRevisionOnDisk();

    // 正常两代保存（current=v2、.bak=v1），再构造 .new 残留（保存中途
    // 崩溃的磁盘现场——前置裸写垃圾字节）。
    ASSERT_TRUE(drafts
                    .save(makeDraft(store, "modeling", "{\"v\":1}",
                                    DraftOrigin::Autosave, base))
                    .ok);
    ASSERT_TRUE(drafts
                    .save(makeDraft(store, "modeling", "{\"v\":2}",
                                    DraftOrigin::Manual, base))
                    .ok);
    const std::wstring newFile
        = draftFile(branch, "modeling").wstring() + L".new";
    ASSERT_TRUE(writeRaw(newFile, "{\"half-writ"));
    ASSERT_TRUE(fs::exists(newFile));

    // 重开后加载：current（v2）完好返回——.new 残留不参与、不干扰。
    store.requestClose();
    pd::OpenStoreRequest request;
    request.path = m_dir;
    request.diagnostics = m_sink.get();
    auto reopened = ProjectStoreFactory::open(request);
    std::vector<core::DiagnosticRecord> diags;
    const auto loaded = reopened.store->drafts().tryLoad(branch, "modeling",
                                                         diags);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->payload, "{\"v\":2}");

    // .new 已被丢弃；current/.bak 原样保留（§8.4 处置规则三件套）。
    EXPECT_FALSE(fs::exists(newFile)) << "崩溃残留 .new 应被丢弃";
    EXPECT_TRUE(fs::exists(draftFile(branch, "modeling")));
    EXPECT_TRUE(fs::exists(draftFile(branch, "modeling").wstring() + L".bak"));
    EXPECT_TRUE(diags.empty()) << "current 完好时无用户级诊断（.new 清理"
                                 "经开发诊断通道报告）";
    EXPECT_TRUE(m_sink->devContains("discarded"))
        << ".new 丢弃必须留开发诊断可见性（不静默）";
}

/**
 * acceptance 1 之放弃草稿语义（DiscardResult）：删除 current 与 .bak；
 * 幂等（对无草稿目标放弃＝ok 且两位明细 false）；放弃后 tryLoad 为空。
 */
TEST_F(DraftServiceTest, DiscardRemovesCurrentAndBakIdempotent)
{
    auto opened = createNew();
    auto& store = *opened.store;
    auto& drafts = store.drafts();
    const auto branch = primaryBranch(store);
    const auto base = headRevisionOnDisk();

    // 两代保存→放弃：两位明细如实（removedCurrent＋removedBackup）。
    ASSERT_TRUE(drafts
                    .save(makeDraft(store, "modeling", "{\"v\":1}",
                                    DraftOrigin::Autosave, base))
                    .ok);
    ASSERT_TRUE(drafts
                    .save(makeDraft(store, "modeling", "{\"v\":2}",
                                    DraftOrigin::Manual, base))
                    .ok);
    const DiscardResult discarded = drafts.discard(branch, "modeling");
    ASSERT_TRUE(discarded.ok);
    EXPECT_TRUE(discarded.removedCurrent);
    EXPECT_TRUE(discarded.removedBackup);
    EXPECT_FALSE(fs::exists(draftFile(branch, "modeling")));
    EXPECT_FALSE(fs::exists(draftFile(branch, "modeling").wstring() + L".bak"));

    // 放弃后不可加载（恢复横幅消失的数据面）。
    std::vector<core::DiagnosticRecord> diags;
    EXPECT_FALSE(drafts.tryLoad(branch, "modeling", diags).has_value());

    // 幂等：对已无草稿的目标再次放弃＝目标态已成立（ok=true、两位
    // false——DiscardResult 注释口径；PM-15"放弃"按钮双击不报错）。
    const DiscardResult again = drafts.discard(branch, "modeling");
    EXPECT_TRUE(again.ok);
    EXPECT_FALSE(again.removedCurrent);
    EXPECT_FALSE(again.removedBackup);
}

// =====================================================================
// acceptance 2：StaleRevisionRejected 冲突路径（应用＝恰一新修订、
// 过期拒绝并保留草稿）＋多模块草稿汇总投影（DraftProjection）
// =====================================================================

/**
 * acceptance 2 之应用成功路径（§8.3）：草稿基线＝分支 tip 时以草稿负载
 * 构造命令 submit(expectedRevision=base)——恰好产生一个新修订（PM-04；
 * PRJ-TX-6/AT-29 关联）。
 */
TEST_F(DraftServiceTest, ApplyCommittedExactlyOneRevisionFromDraft)
{
    auto opened = createNew();
    auto& store = *opened.store;
    auto& drafts = store.drafts();
    const auto branch = primaryBranch(store);
    const auto base = headRevisionOnDisk();
    auto& registry = impl(store).commandService().registry();
    registry.registerHandler(
        std::make_unique<stub::AppendObjectHandler>());

    // 草稿（base=当前 tip）→应用信封（域组装形态：负载入 payload）。
    const std::string payload = "{\"model\":\"applied\"}";
    ASSERT_TRUE(drafts
                    .save(makeDraft(store, "modeling", payload,
                                    DraftOrigin::Manual, base))
                    .ok);
    const auto revisionsBefore = revisionDirCount();
    CommandEnvelope envelope;
    envelope.branch = branch;
    envelope.expectedRevision = base;  // §8.3：expectedRevision=draft.base
    envelope.commandType = "test-append-object";
    envelope.payloadFormatVersion = 1;
    envelope.payloadCanonical.assign(payload.begin(), payload.end());

    const CommandResult result = store.commands().submit(envelope);
    ASSERT_TRUE(result.committed()) << "新鲜基线应用必须成功";
    ASSERT_TRUE(result.newRevision.has_value());

    // "应用＝恰好一个新修订"：磁盘修订目录恰 +1；HEAD 前进到新修订。
    EXPECT_EQ(revisionDirCount(), revisionsBefore + 1);
    EXPECT_EQ(headRevisionOnDisk(), *result.newRevision);
}

/**
 * acceptance 2 之冲突路径（§8.3/§6.2）：草稿基线过期（tip 已前进）时
 * submit 被拒（Rejected stale-revision＋PRJ-STALE-REVISION-REJECTED 稳定
 * 诊断），草稿文件原样保留（数据完整）；拒绝后以 origin=apply-retained
 * 保留重存可读回（PM-04 保留语义的标记面）。
 */
TEST_F(DraftServiceTest, StaleBaselineRejectedAndDraftRetained)
{
    auto opened = createNew();
    auto& store = *opened.store;
    auto& drafts = store.drafts();
    const auto branch = primaryBranch(store);
    const auto staleBase = headRevisionOnDisk();  // r0——即将过期
    auto& registry = impl(store).commandService().registry();
    registry.registerHandler(
        std::make_unique<stub::AppendObjectHandler>());

    // 草稿基于 r0 保存；随后另一次提交把 tip 推进到 r1（并发编辑方）。
    const std::string payload = "{\"model\":\"stale-draft\"}";
    ASSERT_TRUE(drafts
                    .save(makeDraft(store, "modeling", payload,
                                    DraftOrigin::Autosave, staleBase))
                    .ok);
    const auto commit = commitOne(store);
    ASSERT_NE(commit.revisionId, staleBase);

    // 过期基线应用：Rejected(stale-revision)＋稳定诊断随结果回传。
    const auto revisionsBefore = revisionDirCount();
    CommandEnvelope envelope;
    envelope.branch = branch;
    envelope.expectedRevision = staleBase;  // 过期——tip 已是 r1
    envelope.commandType = "test-append-object";
    envelope.payloadFormatVersion = 1;
    envelope.payloadCanonical.assign(payload.begin(), payload.end());
    const CommandResult result = store.commands().submit(envelope);
    ASSERT_TRUE(result.rejected());
    EXPECT_EQ(result.status.rejection, CommandStatus::Rejection::StaleRevision);
    EXPECT_FALSE(result.newRevision.has_value()) << "拒绝不产生修订";
    EXPECT_EQ(revisionDirCount(), revisionsBefore)
        << "拒绝路径零修订落盘（§5.3.1 表后置）";
    bool hasStaleDiag = false;
    for (const auto& d : result.diagnostics) {
        if (d.code == "PRJ-STALE-REVISION-REJECTED") {
            hasStaleDiag = true;
        }
    }
    EXPECT_TRUE(hasStaleDiag) << "过期基线必须附稳定码诊断（§6.3 表）";

    // 草稿保留：拒绝后草稿文件与数据完整（用户可继续编辑——§8.3）。
    std::vector<core::DiagnosticRecord> diags;
    const auto retained = drafts.tryLoad(branch, "modeling", diags);
    ASSERT_TRUE(retained.has_value()) << "拒绝后草稿必须保留";
    EXPECT_EQ(retained->payload, payload);

    // apply-retained 标记面：拒绝后重存（数据不变、origin 标记保留
    // 语义）→读回 origin==apply-retained（恢复横幅提示语依据——PM-15）。
    DraftDocument retainedDoc = *retained;
    retainedDoc.origin = DraftOrigin::ApplyRetained;
    const SaveResult reSaved = drafts.save(retainedDoc);
    ASSERT_TRUE(reSaved.ok);
    std::vector<core::DiagnosticRecord> diags2;
    const auto reread = drafts.tryLoad(branch, "modeling", diags2);
    ASSERT_TRUE(reread.has_value());
    EXPECT_EQ(reread->origin, DraftOrigin::ApplyRetained);
    EXPECT_EQ(reread->payload, payload);
}

/**
 * acceptance 2 之多模块汇总投影（§8.5 DraftProjection）：条目按 moduleId
 * 字典序；stale＝base≠权威 tip 的逐条判定（新鲜的 false、过期的 true）；
 * present/savedAtUtc/origin 透传；损坏文件条目 present=false＋开发诊断
 * （投影面保守不虚报可用草稿）。
 */
TEST_F(DraftServiceTest, SummarizeProjectsMultiModuleWithStaleAndSortOrder)
{
    auto opened = createNew();
    auto& store = *opened.store;
    auto& drafts = store.drafts();
    const auto branch = primaryBranch(store);
    const auto r0 = headRevisionOnDisk();

    // alpha 基于 r0（随后过期）；提交推进 tip 到 r1 后 beta 基于 r1
    // （新鲜）——两模块一旧一新，覆盖 stale 两态。
    ASSERT_TRUE(drafts
                    .save(makeDraft(store, "alpha", "{\"a\":1}",
                                    DraftOrigin::Autosave, r0))
                    .ok);
    const auto r1 = commitOne(store).revisionId;
    ASSERT_TRUE(drafts
                    .save(makeDraft(store, "beta", "{\"b\":2}",
                                    DraftOrigin::Manual, r1))
                    .ok);
    // 损坏注入：gamma 文件裸写垃圾（投影的 present=false 面）。
    ASSERT_TRUE(writeRaw(draftFile(branch, "gamma"), "not-json"));

    const DraftProjection projection = drafts.summarize(branch);
    EXPECT_EQ(projection.branch, branch);

    // 排序（NFR-COR-02）：alpha < beta < gamma（moduleId 字典序）。
    ASSERT_EQ(projection.items.size(), 3U);
    EXPECT_EQ(projection.items[0].moduleId, "alpha");
    EXPECT_EQ(projection.items[1].moduleId, "beta");
    EXPECT_EQ(projection.items[2].moduleId, "gamma");

    // alpha：present＋过期（base=r0≠tip=r1）＋元数据透传。
    EXPECT_TRUE(projection.items[0].present);
    EXPECT_TRUE(projection.items[0].stale);
    EXPECT_EQ(projection.items[0].baseRevision, r0);
    EXPECT_EQ(projection.items[0].origin, "autosave");
    EXPECT_EQ(projection.items[0].savedAtUtc, "2026-09-16T03:00:00Z");

    // beta：present＋新鲜（base=r1==tip）。
    EXPECT_TRUE(projection.items[1].present);
    EXPECT_FALSE(projection.items[1].stale);
    EXPECT_EQ(projection.items[1].baseRevision, r1);
    EXPECT_EQ(projection.items[1].origin, "manual");

    // gamma：损坏文件条目 present=false（不虚报可用草稿）＋开发诊断。
    EXPECT_FALSE(projection.items[2].present);
    EXPECT_FALSE(projection.items[2].stale);
    EXPECT_TRUE(m_sink->devContains("gamma"))
        << "投影跳过损坏条目必须留开发诊断";

    // 与查询端口 DraftInfo 同源：list 与投影的可用面一致（两个可用）。
    const auto infos = drafts.list(branch);
    ASSERT_EQ(infos.size(), 2U);
    EXPECT_EQ(infos[0].moduleId, "alpha");
    EXPECT_TRUE(infos[0].stale);
    EXPECT_EQ(infos[1].moduleId, "beta");
    EXPECT_FALSE(infos[1].stale);
}

// =====================================================================
// acceptance 3：生命周期边界——§8.1 数据结构区别、§8.6 只读打开与关闭、
// 60 s 默认落盘周期（PM-04-S1 归 R2/WP-04-T20，本任务不提前实现——
// 边界即"project 仅接收调用"，定时器不存在于本服务）
// =====================================================================

/**
 * acceptance 3 之 §8.1 草稿与已应用修订的数据结构区别：草稿身份＝
 * (branchId, moduleId) 二元组、覆盖写（同目标重复保存仍是同一文件）；
 * 修订身份＝全局唯一 RevisionId、只增（两次提交两个修订目录）。另断言
 * 草稿可携带非法中间态负载（编辑中语义——§8.1"可含非法中间态"）。
 */
TEST_F(DraftServiceTest, DraftOverwriteVsRevisionAppendOnly)
{
    auto opened = createNew();
    auto& store = *opened.store;
    auto& drafts = store.drafts();
    const auto branch = primaryBranch(store);
    const auto base = headRevisionOnDisk();

    // 草稿覆盖写：同 (branch, module) 两次保存——目录内 .draft.json 名义
    // 文件仍只有这一个（.bak 是 §4.1 契约内的上一版轮换保留，不算第二
    // 份草稿；不出现别的模块文件）。
    ASSERT_TRUE(drafts
                    .save(makeDraft(store, "modeling", "{\"v\":1}",
                                    DraftOrigin::Autosave, base))
                    .ok);
    ASSERT_TRUE(drafts
                    .save(makeDraft(store, "modeling", "{\"v\":2}",
                                    DraftOrigin::Autosave, base))
                    .ok);
    {
        // 只数 "<module>.draft.json" 形态（.new/.bak 后缀天然不匹配）。
        std::size_t nominalDrafts = 0;
        for (const auto& entry
             : fs::directory_iterator(draftsRoot() / branch.toCanonical())) {
            const std::string name = entry.path().filename().string();
            if (name.size() > std::strlen(".draft.json")
                && name.compare(name.size() - std::strlen(".draft.json"),
                                std::strlen(".draft.json"), ".draft.json")
                       == 0) {
                ++nominalDrafts;
            }
        }
        EXPECT_EQ(nominalDrafts, 1U)
            << "草稿＝模块粒度单文件覆盖写（D-12；§8.1 身份二元组）";
    }
    std::vector<core::DiagnosticRecord> diags;
    const auto latest = drafts.tryLoad(branch, "modeling", diags);
    ASSERT_TRUE(latest.has_value());
    EXPECT_EQ(latest->payload, "{\"v\":2}");

    // 非法中间态：编辑中的半成品负载也允许落盘（草稿可变、不校验域
    // 语义——project 不解释 payload；与修订"已通过断言/确认/编译"的
    // 权威状态形成 §8.1 的内容面对比）。
    ASSERT_TRUE(drafts
                    .save(makeDraft(store, "modeling", "{{{not-json",
                                    DraftOrigin::Autosave, base))
                    .ok);

    // 修订只增：两次提交产生两个修订目录（全局唯一身份、PA-2 不可变
    // ——与草稿"同一目标覆盖"的结构区别即 §8.1 表的行 1/行 3）。
    const auto before = revisionDirCount();
    ASSERT_TRUE(commitOne(store).revisionId.isValid());
    ASSERT_TRUE(commitOne(store).revisionId.isValid());
    EXPECT_EQ(revisionDirCount(), before + 2);
}

/**
 * acceptance 3 之 §8.6 只读打开下的草稿行为：只读上下文可查看（tryLoad/
 * list/summarize）但不可落盘/放弃——save/discard 门卫拒绝（返回值轨
 * LockHeldByOther＋PRJ-LOCK-HELD，PM-07 禁编辑的存储侧落实），磁盘字节
 * 不变（"只读不落盘"）。
 */
TEST_F(DraftServiceTest, ReadOnlyOpenRejectsSaveAndDiscardAllowsRead)
{
    auto opened = createNew();
    auto& store = *opened.store;
    const auto branch = primaryBranch(store);
    const auto base = headRevisionOnDisk();
    const std::string payload = "{\"v\":1}";
    ASSERT_TRUE(store.drafts()
                    .save(makeDraft(store, "modeling", payload,
                                    DraftOrigin::Autosave, base))
                    .ok);
    const auto draftBytes = readAll(draftFile(branch, "modeling"));
    const auto projectId = store.projectId();

    // 显式只读重开（PM-07 ReadOnly——不取写锁）。
    store.requestClose();
    pd::OpenStoreRequest request;
    request.path = m_dir;
    request.mode = OpenMode::ReadOnly;
    request.diagnostics = m_sink.get();
    auto readonly = ProjectStoreFactory::open(request);
    ASSERT_FALSE(readonly.writable);
    ASSERT_EQ(readonly.store->projectId(), projectId);
    auto& drafts = readonly.store->drafts();

    // 只读可查看：tryLoad/list/summarize 全量可用（PM-07"禁编辑"不禁读）。
    std::vector<core::DiagnosticRecord> diags;
    const auto loaded = drafts.tryLoad(branch, "modeling", diags);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->payload, payload);
    EXPECT_EQ(drafts.list(branch).size(), 1U);
    EXPECT_EQ(drafts.summarize(branch).items.size(), 1U);

    // 只读拒绝保存（返回值轨——ui 定时器安全；诊断 PRJ-LOCK-HELD）。
    // 文档经只读实例构造（查询面全量可用——身份/元数据读取不涉写权限）。
    const SaveResult saved
        = drafts.save(makeDraft(*readonly.store, "modeling", "{\"v\":2}",
                                DraftOrigin::Manual, base));
    EXPECT_FALSE(saved.ok);
    ASSERT_TRUE(saved.error.has_value());
    EXPECT_EQ(saved.error->code(), StoreErrorCode::LockHeldByOther);
    EXPECT_TRUE(m_sink->hasUserCode("PRJ-LOCK-HELD"));

    // 只读拒绝放弃（§8.6"放弃＝写操作，只读模式拒绝＋诊断"）。
    const DiscardResult discarded = drafts.discard(branch, "modeling");
    EXPECT_FALSE(discarded.ok);
    ASSERT_TRUE(discarded.error.has_value());
    EXPECT_EQ(discarded.error->code(), StoreErrorCode::LockHeldByOther);

    // 只读不落盘：草稿文件字节与保存前完全一致。
    EXPECT_EQ(readAll(draftFile(branch, "modeling")), draftBytes);
}

/**
 * acceptance 3 之关闭边界：requestClose 排空完成后草稿写入口以返回值
 * 拒绝（ContextClosed——门卫①），读轨抛 ContextClosed（§4.7 同查询
 * 端口）；关闭对磁盘草稿无副作用。
 */
TEST_F(DraftServiceTest, ClosedContextRejectsDraftWritesAndReads)
{
    auto opened = createNew();
    auto& store = *opened.store;
    auto& drafts = store.drafts();
    const auto branch = primaryBranch(store);
    const auto base = headRevisionOnDisk();
    ASSERT_TRUE(drafts
                    .save(makeDraft(store, "modeling", "{\"v\":1}",
                                    DraftOrigin::Autosave, base))
                    .ok);
    const auto draftBytes = readAll(draftFile(branch, "modeling"));

    // 待保存文档在关闭**前**构造（关闭后宿主查询面冻结——被测对象是
    // 草稿写入口的拒绝语义，不是查询端口的拒绝）。
    const DraftDocument afterClose = makeDraft(store, "modeling", "{\"v\":2}",
                                               DraftOrigin::Manual, base);

    ASSERT_EQ(store.requestClose(), 0U);  // 无在途——同步排空完成
    ASSERT_TRUE(store.closed());

    // 写轨：门卫①拒绝进返回值（ContextClosed＋PRJ-WRITE-AUTHORITY-LOST
    // ——§9.6①迟到写拒绝的草稿面）。
    const SaveResult saved = drafts.save(afterClose);
    EXPECT_FALSE(saved.ok);
    ASSERT_TRUE(saved.error.has_value());
    EXPECT_EQ(saved.error->code(), StoreErrorCode::ContextClosed);
    EXPECT_TRUE(m_sink->hasUserCode("PRJ-WRITE-AUTHORITY-LOST"));
    const DiscardResult discarded = drafts.discard(branch, "modeling");
    EXPECT_FALSE(discarded.ok);
    EXPECT_EQ(discarded.error->code(), StoreErrorCode::ContextClosed);

    // 读轨：Closed 后抛 ContextClosed（异常轨——§4.7）。
    std::vector<core::DiagnosticRecord> diags;
    expectStoreError(StoreErrorCode::ContextClosed,
                     [&] { std::ignore = drafts.tryLoad(branch, "modeling", diags); });
    expectStoreError(StoreErrorCode::ContextClosed,
                     [&] { std::ignore = drafts.list(branch); });

    // 关闭对磁盘草稿无副作用（拒绝路径零写入）。
    EXPECT_EQ(readAll(draftFile(branch, "modeling")), draftBytes);
}

/**
 * acceptance 3 之 60 s 默认落盘周期边界（PM-04/PM-04-S1）：定时器归 ui
 * 会话层、周期可调归 R2/WP-04-T20——本服务无任何定时器，autosave 与
 * manual 是**同一 save 入口**的两个 origin 标记（§8.2"两者均不产生
 * 修订"）。边界断言：两种 origin 的保存行为完全一致（落盘/无修订/
 * 元数据如实），落盘只发生在显式调用时。
 */
TEST_F(DraftServiceTest, AutosaveAndManualShareSingleSavePath)
{
    auto opened = createNew();
    auto& store = *opened.store;
    auto& drafts = store.drafts();
    const auto branch = primaryBranch(store);
    const auto base = headRevisionOnDisk();
    const auto revisionsBefore = revisionDirCount();

    // 两种 origin 各保存一个模块：入口行为一致（都成功、都只落 drafts/）。
    ASSERT_TRUE(drafts
                    .save(makeDraft(store, "alpha", "{\"a\":1}",
                                    DraftOrigin::Autosave, base))
                    .ok);
    ASSERT_TRUE(drafts
                    .save(makeDraft(store, "beta", "{\"b\":2}",
                                    DraftOrigin::Manual, base))
                    .ok);

    // origin 如实透传（恢复横幅按 origin 决定提示语——PM-15）。
    const auto infos = drafts.list(branch);
    ASSERT_EQ(infos.size(), 2U);
    EXPECT_EQ(infos[0].moduleId, "alpha");
    EXPECT_EQ(infos[0].origin, DraftOrigin::Autosave);
    EXPECT_EQ(infos[1].moduleId, "beta");
    EXPECT_EQ(infos[1].origin, DraftOrigin::Manual);

    // 两种 origin 都不产生修订（PM-04 定时落盘＝保存与应用分离）。
    EXPECT_EQ(revisionDirCount(), revisionsBefore);
    EXPECT_EQ(headRevisionOnDisk(), base);
}

// =====================================================================
// acceptance 4：P-PR-1 处置——草稿负载中 SourcedValue/ValueProvenance
// 来源标记透传（不解释——§3.2 消费清单）；以 core.md v0.1 为基线，
// 冻结 diff 后增量同步、不私改 core
// =====================================================================

/**
 * acceptance 4 之透传自证：含 SourcedValue/ValueProvenance 域语义标记的
 * payload（单位/scope/sourceVersion 等域自有键——project 无从解释）经
 * save→磁盘→tryLoad 全程逐字节一致；从磁盘直接解析（不经服务层）的
 * payload 同样一致——"project 未改写、未解释"的磁盘面证据（CR-02/D-10
 * 透传纪律；P-PR-1 消费基线＝core.md v0.1，构建红线由 BuildRedLineTest
 * 的零 core 修改扫描与 LinkageContractTest 的唯一单元边共同钉住）。
 */
TEST_F(DraftServiceTest, PayloadWithSourcedValuePassthroughUnexplained)
{
    auto opened = createNew();
    auto& store = *opened.store;
    auto& drafts = store.drafts();
    const auto branch = primaryBranch(store);
    const auto base = headRevisionOnDisk();

    // 测试自有"域负载"：SourcedValue（值＋单位）与 ValueProvenance
    // （来源/scope/版本）的典型形态——键面与语义归 core §4.3/域单元，
    // project 侧只当不透明 canonical 字节（透传契约的被测面）。
    const std::string payload =
        "{\"parameter\":\"baseHeight\",\"value\":{\"kind\":\"sourced\","
        "\"sourcedValue\":{\"raw\":1420.5,\"unit\":\"mm\"}},"
        "\"provenance\":{\"kind\":\"value-provenance\",\"source\":"
        "\"catalog\",\"scope\":\"selection\",\"sourceVersion\":"
        "\"cv-3f2a\",\"recordedAt\":\"2026-09-16T00:00:00Z\"}}";

    DraftDocument doc = makeDraft(store, "modeling", payload,
                                  DraftOrigin::Manual, base);
    // externalRefs 一并 round-trip（§4.4.5 可选字段——NFR-SEC-01 登记
    // 面；透传纪律对整个 DraftDocument 成立）。
    ExternalRefRecord ref;
    ref.externalRefId = "ext-1";
    ref.absolutePath = "D:/assets/motor.step";
    ref.contentHash256 = std::string(64, 'a');
    ref.sizeBytes = 1234;
    ref.recordedAtUtc = "2026-09-16T00:00:00Z";
    ref.state = "recorded";
    doc.externalRefs.push_back(ref);

    const SaveResult saved = drafts.save(doc);
    ASSERT_TRUE(saved.ok) << "保存失败: "
                          << (saved.error ? saved.error->what() : "?");

    // 服务层 round-trip：全字段一致（含来源标记负载与外部引用记录）。
    std::vector<core::DiagnosticRecord> diags;
    const auto loaded = drafts.tryLoad(branch, "modeling", diags);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(*loaded, doc);

    // 磁盘面自证：不经服务层、直接解析磁盘字节——payload/externalRefs
    // 逐字节一致（project 在落盘链路上没有改写/解释负载的任何环节）。
    const DraftDocument onDisk
        = parseDraftDocument(readAll(draftFile(branch, "modeling")));
    EXPECT_EQ(onDisk.payload, payload);
    ASSERT_EQ(onDisk.externalRefs.size(), 1U);
    EXPECT_EQ(onDisk.externalRefs[0], ref);
}

// =====================================================================
// 门卫与输入契约补充面（acceptance 1/3 的错误边界——moduleId 白名单、
// 跨项目拒绝；错误二分的 fail-fast 半区）
// =====================================================================

/**
 * moduleId 白名单（路径安全面——§4.1"注册的模块 token"）：路径分隔符/
 * 点/空串等 fail-fast（invalid_argument），不落盘不静默净化。
 */
TEST_F(DraftServiceTest, ModuleIdWhitelistRejectsUnsafeTokens)
{
    auto opened = createNew();
    auto& store = *opened.store;
    auto& drafts = store.drafts();
    const auto branch = primaryBranch(store);
    const auto base = headRevisionOnDisk();

    // 恶意/畸形 token 全集：穿越、分隔符、后缀歧义、空串、超长。
    const std::vector<std::string> bad = {
        "../escape", "a/b", "a\\b", "a.b", ".hidden", "", std::string(65, 'x'),
        "mod ule", "模块"};
    for (const auto& moduleId : bad) {
        EXPECT_THROW(drafts.save(makeDraft(store, moduleId, "{}",
                                          DraftOrigin::Manual, base)),
                     std::invalid_argument)
            << "应拒绝 moduleId: [" << moduleId << "]";
        std::vector<core::DiagnosticRecord> diags;
        EXPECT_THROW(drafts.tryLoad(branch, moduleId, diags),
                     std::invalid_argument);
        EXPECT_THROW(drafts.discard(branch, moduleId), std::invalid_argument);
    }

    // 全部拒绝后无任何落盘痕迹（fail-fast 不留半套数据）。
    EXPECT_EQ(subdirectoryCount(draftsRoot()), 0U);

    // 合法边界：1 字符与 64 字符 token、下划线/连字符可用。
    EXPECT_NO_THROW(drafts.save(makeDraft(store, "a", "{}",
                                         DraftOrigin::Manual, base)));
    EXPECT_NO_THROW(drafts.save(makeDraft(store, std::string(64, 'x'), "{}",
                                         DraftOrigin::Manual, base)));
    EXPECT_NO_THROW(drafts.save(makeDraft(store, "ok-module_1", "{}",
                                         DraftOrigin::Manual, base)));
}

/**
 * 跨项目归属拒绝（save 的 fail-fast 半区——doc.projectId 与上下文不符
 * 即装配错乱，落盘即制造跨项目脏数据）。
 */
TEST_F(DraftServiceTest, SaveRejectsForeignProjectDocument)
{
    auto opened = createNew();
    auto& store = *opened.store;
    auto& drafts = store.drafts();
    const auto base = headRevisionOnDisk();

    DraftDocument doc = makeDraft(store, "modeling", "{}",
                                  DraftOrigin::Manual, base);
    // 全零保留值 ProjectId（core U-1：isValid() 恒 false）——与上下文的
    // 真实身份必不相等，代表"他项目文档"的最小构造。
    doc.projectId = ProjectId{};
    EXPECT_THROW(drafts.save(doc), std::invalid_argument);

    // 归属不符的文档无落盘痕迹。
    EXPECT_EQ(subdirectoryCount(draftsRoot()), 0U);
}
