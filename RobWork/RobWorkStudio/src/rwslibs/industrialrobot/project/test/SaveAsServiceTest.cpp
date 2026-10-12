/**
 * @file   SaveAsServiceTest.cpp
 * @brief  PRJ-T18 另存为服务用例组（§8.8 三处改写复制协议／§8.9 选择性
 *         复制与引用完整性／PM-05 失败清理不留半成品／调用方错误
 *         fail-fast）。
 *
 * 设计依据：
 *   - units/project.md §8.8（另存为协议八步——改写点仅三处：project.
 *     json.projectId/HEAD.projectId/drafts 逐文件 projectId；换新
 *     projectId 且对象/修订/分支 ID 原样保留；results 已归档 RunManifest
 *     的 taskIdentity 保持原五元组不改写；失败清理临时位不留半成品）、
 *     §8.9（选择性复制：引用 run 不在勾选集→报告不复制＋诊断列出；
 *     drafts 独立可复制；对象库与修订历史无条件全量）、§8.7（闭包校验
 *     ＝对副本执行打开协议②③⑤）、D-16/D-17；
 *   - 需求 PM-05、AT-13（失败不留半成品）、AT-20（复制后引用一致）；
 *   - 任务契约 tasks/foundation/PRJ-T18.json acceptance 1～2。
 *
 * 观测点纪律：磁盘事实（project.json/HEAD 字节解析、目录残存量）为
 *   第一观测面；查询端口视图为第二观测面——两者互证。诊断断言面＝
 *   StubSink（开发级 dev 消息）＋结果结构（skippedReports 机器可读面
 *   ——稳定码不为此私造，CR-08）。
 */

#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/project/DraftService.hpp>
#include <sdurws/ird/project/PackageService.hpp>
#include <sdurws/ird/project/PersistenceFormat.hpp>
#include <sdurws/ird/project/ProjectStore.hpp>
#include <sdurws/ird/project/QueryPort.hpp>
#include <sdurws/ird/testkit/Fixture.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include "Codec.hpp"  // codec::parse*（磁盘字节解析断言面——src/ 私有头
                                  // 同单元测试消费既定形态，R-2 先例）
#include "PackageStubs.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// 测试侧命名空间别名（core/codec 均为 sdurws::ird 与 project 的嵌套
// 命名空间——using namespace sdurws::ird::project 不使其左段可见，
// 显式别名让断言代码保持与产品代码同形的限定名）。
namespace core = sdurws::ird::core;
namespace codec = sdurws::ird::project::codec;

using namespace sdurws::ird::project;
using sdurws::ird::project::stub::PackageFixture;
using sdurws::ird::project::stub::ScriptedFileOps;

namespace {

/// 读取整个文件（磁盘事实观测辅助）。
std::string readAllFile(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

/// 目标父目录下的 saveas 临时位残存量（PM-05"不留半成品"观测点）。
std::size_t countSaveAsResidue(const fs::path& parent)
{
    std::size_t n = 0;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(parent, ec)) {
        const std::string name = entry.path().filename().string();
        if (name.rfind(".saveas-", 0) == 0) {
            ++n;
        }
    }
    return n;
}

}  // namespace

// =====================================================================
// 用例组：SaveAsRewire（§8.8——三处改写点与身份保留）
// =====================================================================

/**
 * 锚定：PRJ-T18 acceptance 1／§8.8／D-16——另存为＝全量复制＋三处改
 * 写（project.json/HEAD/drafts 的 projectId）；对象/修订/分支 ID 原样
 * 保留；results 已归档 RunManifest 的 taskIdentity 保持原五元组不改写；
 * 闭包校验＝副本按 PM-02 打开协议进入。
 *
 * 前置：健康源项目（修订 r0/r1＋一个完整运行＋一份草稿＋报告树——
 * PackageFixture 组装）。
 * 操作：全勾选另存为到新目录；对结果上下文与磁盘字节逐面走查。
 * 预期：
 *   - ok==true；newProjectId 有效且 ≠ 源 projectId（core 生成）；
 *   - project.json：projectId＝新，其余字段逐字保留（整体重写≠改字段）；
 *   - HEAD：projectId＝新；revisionId/revisionSeq/branchId/manifestDigest
 *     与源逐字相同（改写点仅 projectId——清单字节未动）；
 *   - drafts：新项目 tryLoad 解析成功且 projectId＝新、payload 逐字节
 *     相同（逐文件重写）；
 *   - 分支表：branchTips 与源全等（ID/label/base/tip 原样）；
 *   - 修订：HEAD 闭包逐修订在新项目 tryRevision 解析且视图全等；
 *   - 对象：全部引用的字节经新项目查询逐字节相同（内容编址不变）；
 *   - results：RunInfo.task 五元组与源全等（**保持原 projectId**——
 *     历史运行事实不改写；runId 相同）；
 *   - 恢复诊断面：result.diagnostics 为空（健康副本打开零恢复项）。
 * 观测点：磁盘字节解析＋查询端口视图互证（ird-test-report.json trace）。
 */
TEST(SaveAsRewire, ThreeRewritePointsOnly_IdsPreserved)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-05"},
                  std::vector<std::string>{"AT-20", "AT-13"});
    PackageFixture f("ird-prj-t18-saveas-full");

    const core::ProjectId sourceId = f.store().projectId();
    const RevisionView sourceHead = f.store().query().head();
    const std::vector<BranchTip> sourceTips = f.store().query().branchTips();
    const std::vector<RunInfo> sourceRuns
        = f.store().query().listRuns(f.store().query().head().id);
    ASSERT_EQ(sourceRuns.size(), std::size_t{1});

    SaveAsOptions options;
    options.targetDir = f.dir.parent_path()
        / (f.dir.filename().string() + "-copy");
    options.copyResults = true;
    options.copyReports = true;
    options.copyDrafts = true;
    const SaveAsResult result = f.saveAsService->saveAs(options);

    // ---- 结果面：发布并按 PM-02 进入（store 非空；新身份有效且不同）。
    ASSERT_TRUE(result.ok) << (result.error ? result.error->what() : "ok==false");
    ASSERT_NE(result.store, nullptr);
    EXPECT_TRUE(result.newProjectId.isValid());
    EXPECT_NE(result.newProjectId, sourceId);
    EXPECT_EQ(result.store->projectId(), result.newProjectId);
    EXPECT_TRUE(result.diagnostics.empty())
        << "健康副本的打开恢复诊断应为空（无孤儿草稿/无暂存残留）";

    // ---- 改写点 1/3：project.json（磁盘字节解析——projectId＝新，
    // 其余字段逐字保留；§4.2"另存为整体重写"的字节级口径）。
    const ProjectStaticIdentity copiedIdentity
        = codec::parseStaticIdentity(readAllFile(options.targetDir / "project.json"));
    const ProjectStaticIdentity sourceIdentity
        = codec::parseStaticIdentity(readAllFile(f.dir / "project.json"));
    EXPECT_EQ(copiedIdentity.projectId, result.newProjectId);
    EXPECT_EQ(copiedIdentity.formatId, sourceIdentity.formatId);
    EXPECT_EQ(copiedIdentity.schemaVersion, sourceIdentity.schemaVersion);
    EXPECT_EQ(copiedIdentity.createdAtUtc, sourceIdentity.createdAtUtc);
    EXPECT_EQ(copiedIdentity.createdWithToolVersion,
              sourceIdentity.createdWithToolVersion);

    // ---- 改写点 2/3：HEAD（projectId＝新；修订指针字段逐字相同——
    // 修订清单字节未动，manifestDigest 保持自校验语义）。
    const HeadRecord copiedHead
        = codec::parseHeadRecord(readAllFile(options.targetDir / "HEAD"));
    const HeadRecord sourceHeadRecord
        = codec::parseHeadRecord(readAllFile(f.dir / "HEAD"));
    EXPECT_EQ(copiedHead.projectId, result.newProjectId);
    EXPECT_EQ(copiedHead.revisionId, sourceHeadRecord.revisionId);
    EXPECT_EQ(copiedHead.revisionSeq, sourceHeadRecord.revisionSeq);
    EXPECT_EQ(copiedHead.branchId, sourceHeadRecord.branchId);
    EXPECT_EQ(copiedHead.manifestDigest, sourceHeadRecord.manifestDigest);
    EXPECT_EQ(copiedHead.formatId, sourceHeadRecord.formatId);
    EXPECT_EQ(copiedHead.schemaVersion, sourceHeadRecord.schemaVersion);

    // ---- 改写点 3/3：drafts 逐文件（新项目 tryLoad 解析成功且归属
    // ＝新 projectId、负载逐字节相同——§8.9"drafts 独立可复制"）。
    ProjectStore& copyStore = *result.store;
    // 装配对齐：副本会话的处理器注册表与源同形（hasUnresolvedPayload
    // 判据的会话面——"test-append-object" 注册后两侧判据一致，视图
    // 全等断言才反映数据本身）。
    copyStore.handlerRegistry().registerHandler(
        std::make_unique<stub::AppendObjectHandler>());
    const core::BranchId copyBranch
        = copyStore.query().currentMetadata().record.primaryBranchId;
    // tryLoad 三参形态（diags 追加面——副本草稿归属校验的健康路径应零诊断）。
    std::vector<core::DiagnosticRecord> draftDiags;
    const std::optional<DraftDocument> copiedDraft
        = copyStore.drafts().tryLoad(copyBranch, "modeling", draftDiags);
    ASSERT_TRUE(copiedDraft.has_value()) << "副本草稿必须可加载（逐文件重写后）";
    EXPECT_TRUE(draftDiags.empty()) << "副本草稿归属校验应零诊断";
    EXPECT_EQ(copiedDraft->projectId, result.newProjectId);
    EXPECT_EQ(copiedDraft->payload, "{\"draft\":1}");
    EXPECT_EQ(copiedDraft->baseRevisionId, sourceHead.id);

    // ---- 分支表：ID/label/base/tip 原样（分支 ID 全局随机唯一——
    // 跨副本引用语义不变，§8.8 原文）。
    const std::vector<BranchTip> copyTips = copyStore.query().branchTips();
    ASSERT_EQ(copyTips.size(), sourceTips.size());
    for (std::size_t i = 0; i < sourceTips.size(); ++i) {
        EXPECT_TRUE(copyTips[i] == sourceTips[i]) << "分支表条目 " << i << " 失真";
    }

    // ---- 修订闭包：自 HEAD 沿 parent 链逐修订在新项目 tryRevision
    // 解析且视图全等（修订 ID 原样——闭包可解析，AT-20 引用一致半区）。
    std::optional<core::RevisionId> cursor = sourceHead.id;
    std::size_t walked = 0;
    while (cursor.has_value()) {
        const std::optional<RevisionView> sourceRev
            = f.store().query().tryRevision(*cursor);
        ASSERT_TRUE(sourceRev.has_value());
        const std::optional<RevisionView> copyRev
            = copyStore.query().tryRevision(*cursor);
        ASSERT_TRUE(copyRev.has_value())
            << "副本闭包必须可解析: " << cursor->toCanonical();
        EXPECT_TRUE(*copyRev == *sourceRev)
            << "副本修订视图失真: " << cursor->toCanonical();
        // 对象引用逐个可解析且字节逐字节相同（内容编址不变——对象库
        // 复制是"无条件全量"面）。
        for (const ObjectRef& ref : sourceRev->objectRefs) {
            const std::vector<std::uint8_t> sourceBytes
                = f.store().query().object(ref.objectId, ref.contentVersion);
            const std::vector<std::uint8_t> copyBytes
                = copyStore.query().object(ref.objectId, ref.contentVersion);
            EXPECT_TRUE(sourceBytes == copyBytes)
                << "副本对象字节失真: " << ref.objectId.toCanonical();
        }
        cursor = sourceRev->parent;
        ++walked;
    }
    EXPECT_GE(walked, std::size_t{2}) << "闭包走查应覆盖 r0 与 r1";

    // ---- results：RunManifest 五元组保持原样（§8.8——历史运行事实
    // 不因复制改写；读取侧不校验 manifest 的 projectId 归属）。
    const std::vector<RunInfo> copyRuns
        = copyStore.query().listRuns(copyStore.query().head().id);
    ASSERT_EQ(copyRuns.size(), std::size_t{1});
    EXPECT_TRUE(copyRuns[0] == sourceRuns[0])
        << "RunInfo（含五元组 taskIdentity）必须与源全等——原 projectId 保持";
    EXPECT_EQ(copyRuns[0].task.project, sourceId);

    // 目标父目录零临时位残留（成功发布后临时位已改名消失）。
    EXPECT_EQ(countSaveAsResidue(f.dir.parent_path()), std::size_t{0});

    copyStore.requestClose();
    EXPECT_EQ(copyStore.closed(), true);
}

// =====================================================================
// 用例组：SaveAsSelectiveCopy（§8.9——引用完整性与勾选语义）
// =====================================================================

/**
 * 锚定：PRJ-T18 acceptance 2／§8.9——复制前扫描被勾选 reports 的工件
 * 引用（runId 清单）：引用 run 不在勾选集→该报告不复制＋诊断列出；
 * results 未勾选＝勾选集为空（引用运行的报告同样跳过）；drafts 独立
 * 可复制；对象库与修订历史无条件全量（不受勾选影响）。
 *
 * 前置：健康源项目（含 rep-with-run/rep-no-ref/rep-missing-ref 三报告
 * ——PackageFixture 组装）。
 * 操作与预期（两面）：
 *   - 全勾选：rep-with-run 与 rep-no-ref 复制；rep-missing-ref 跳过；
 *     result.skippedReports＝{"rep-missing-ref"}（确定性）；开发诊断
 *     列出该报告；results/revisions/objects 全量在副本。
 *   - 只勾 reports（results/drafts 关）：rep-with-run 与 rep-missing-ref
 *     都跳过（勾选集为空）；rep-no-ref 复制；副本无 results/drafts
 *     目录，HEAD/revisions/objects 仍在（无条件全量）。
 * 观测点：副本目录树形态＋skippedReports＋sink dev 消息。
 */
TEST(SaveAsSelectiveCopy, ReportReferenceIntegrity_SelectiveTrees)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-05"},
                  std::vector<std::string>{});
    PackageFixture f("ird-prj-t18-saveas-selective");

    // ---- 面 1：全勾选——引用缺失运行的报告跳过，其余复制。
    {
        SaveAsOptions options;
        options.targetDir = f.dir.parent_path() / (f.dir.filename().string() + "-sel-all");
        options.copyResults = true;
        options.copyReports = true;
        options.copyDrafts = true;
        const SaveAsResult result = f.saveAsService->saveAs(options);
        ASSERT_TRUE(result.ok) << (result.error ? result.error->what() : "ok==false");

        // 引用完整性：rep-missing-ref 引用的 run 不在勾选集 → 不复制。
        ASSERT_EQ(result.skippedReports.size(), std::size_t{1});
        EXPECT_EQ(result.skippedReports[0], "rep-missing-ref");
        EXPECT_FALSE(fs::exists(options.targetDir / "reports" / "rep-missing-ref"));
        EXPECT_TRUE(fs::exists(options.targetDir / "reports" / "rep-with-run"));
        EXPECT_TRUE(fs::exists(options.targetDir / "reports" / "rep-no-ref"));
        // 开发诊断列出（"诊断列出"半区——稳定码不为此私造，CR-08）。
        EXPECT_TRUE(f.sink.devContains("rep-missing-ref"));

        // 对象库与修订历史无条件全量：闭包修订两件套＋对象在副本。
        EXPECT_TRUE(fs::exists(options.targetDir / "revisions"));
        EXPECT_TRUE(fs::exists(options.targetDir / "objects"));
        EXPECT_TRUE(fs::exists(options.targetDir / "results"));
        EXPECT_TRUE(fs::exists(options.targetDir / "drafts"));
        result.store->requestClose();
    }

    // ---- 面 2：只勾 reports——勾选集为空，引用运行的报告同跳；
    // results/drafts 不复制，状态本体仍在。
    {
        SaveAsOptions options;
        options.targetDir = f.dir.parent_path() / (f.dir.filename().string() + "-sel-rep");
        options.copyResults = false;
        options.copyReports = true;
        options.copyDrafts = false;
        const SaveAsResult result = f.saveAsService->saveAs(options);
        ASSERT_TRUE(result.ok) << (result.error ? result.error->what() : "ok==false");

        ASSERT_EQ(result.skippedReports.size(), std::size_t{2});
        EXPECT_EQ(result.skippedReports[0], "rep-missing-ref");
        EXPECT_EQ(result.skippedReports[1], "rep-with-run");
        EXPECT_TRUE(fs::exists(options.targetDir / "reports" / "rep-no-ref"));

        // 勾选树缺席（无 results/drafts 目录），状态本体在场。
        EXPECT_FALSE(fs::exists(options.targetDir / "results"));
        EXPECT_FALSE(fs::exists(options.targetDir / "drafts"));
        EXPECT_TRUE(fs::exists(options.targetDir / "HEAD"));
        EXPECT_TRUE(fs::exists(options.targetDir / "revisions"));
        EXPECT_TRUE(fs::exists(options.targetDir / "objects"));
        result.store->requestClose();
    }
}

// =====================================================================
// 用例组：SaveAsFailureCleanup（PM-05/AT-13——失败不留半成品）
// =====================================================================

/**
 * 锚定：PRJ-T18 acceptance 1／§8.8"取消/失败清理临时位，不留半成品"／
 * AT-13。故障注入经 IFileOps 接缝（D-10 形态——AtomicFile 构造注入
 * 先例）：复制中途写失败（F2 型）与发布期目标占用（竞态）两路。
 *
 * 前置：健康源项目；目标目录不存在。
 * 操作：注入失败后另存为。
 * 预期（两路同构）：
 *   - ok==false 且 error 携带稳定码（写故障→WriteRejected）；
 *   - 目标目录不存在（发布未发生——结构性不留半成品）；
 *   - .saveas-* 临时位零残留（护栏整树清理）；
 *   - 源项目不受影响（HEAD 字节不变；上下文可继续提交——零写源）。
 * 观测点：错误码＋目录残存量＋源 HEAD 字节比对。
 */
TEST(SaveAsFailureCleanup, InjectedFailures_LeaveNoTargetNoResidue)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-05"},
                  std::vector<std::string>{"AT-13"});
    PackageFixture f("ird-prj-t18-saveas-fail");
    const std::string sourceHeadBefore
        = readAllFile(f.dir / "HEAD");

    // ---- 路 1：复制中途写失败（第 2 次 writeChunk 失败——多文件复制
    // 的中途边界；osError 29→WriteRejected 既有映射同表）。
    {
        ScriptedFileOps scripted;
        scripted.failWriteChunkAt = 2;
        SaveAsServiceImpl impl(*f.opened.store, &scripted, &f.bus, &f.sink);
        SaveAsOptions options;
        options.targetDir = f.dir.parent_path() / (f.dir.filename().string() + "-fail-copy");
        options.copyDrafts = true;
        const SaveAsResult result = impl.saveAs(options);
        EXPECT_FALSE(result.ok);
        ASSERT_TRUE(result.error.has_value());
        EXPECT_EQ(result.error->code(), StoreErrorCode::WriteRejected);
        // 目标零写入＋临时位零残留（护栏清理）。
        EXPECT_FALSE(fs::exists(options.targetDir));
        EXPECT_EQ(countSaveAsResidue(f.dir.parent_path()), std::size_t{0});
    }

    // ---- 路 2：发布期目标占用（publishNew 注入失败——ERROR_FILE_
    // EXISTS 竞态的映射面，实现口径⑥）。
    {
        ScriptedFileOps scripted;
        scripted.failPublishNew = true;
        SaveAsServiceImpl impl(*f.opened.store, &scripted, &f.bus, &f.sink);
        SaveAsOptions options;
        options.targetDir = f.dir.parent_path() / (f.dir.filename().string() + "-fail-pub");
        options.copyDrafts = true;
        const SaveAsResult result = impl.saveAs(options);
        EXPECT_FALSE(result.ok);
        ASSERT_TRUE(result.error.has_value());
        EXPECT_EQ(result.error->code(), StoreErrorCode::WriteRejected);
        EXPECT_FALSE(fs::exists(options.targetDir));
        EXPECT_EQ(countSaveAsResidue(f.dir.parent_path()), std::size_t{0});
    }

    // ---- 源项目零影响：HEAD 字节不变（另存为全程零写源——结构性）。
    EXPECT_EQ(readAllFile(f.dir / "HEAD"), sourceHeadBefore);
}

// =====================================================================
// 用例组：SaveAsContract（调用方错误 fail-fast 与状态轨）
// =====================================================================

/**
 * 锚定：PRJ-T18 契约的调用方错误面——目标已存在/父目录缺失/空路径
 * ＝fail-fast 异常（createNew 先例同款，不产稳定码）；源上下文已关闭
 * ＝返回值轨 ContextClosed（状态错误——长操作返回值轨口径）。
 *
 * 前置：健康源项目。
 * 操作：违约矩阵逐项调用。
 * 预期：invalid_argument 三路；关闭后 saveAs ok=false＋ContextClosed。
 * 观测点：异常类型＋错误码；零磁盘副作用（残存量不变）。
 */
TEST(SaveAsContract, CallerErrorsFailFast_ClosedContextViaResult)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-05"},
                  std::vector<std::string>{});
    PackageFixture f("ird-prj-t18-saveas-contract");
    const std::size_t residueBefore = countSaveAsResidue(f.dir.parent_path());

    // 空路径。
    EXPECT_THROW(f.saveAsService->saveAs(SaveAsOptions{}), std::invalid_argument);
    // 目标已存在（PM-05 不覆盖口径——调用方先校验）。
    {
        SaveAsOptions options;
        options.targetDir = f.dir;  // 已存在的源目录本身
        EXPECT_THROW(f.saveAsService->saveAs(options), std::invalid_argument);
    }
    // 父目录不存在（同卷临时位落点前提）。
    {
        SaveAsOptions options;
        options.targetDir = f.dir.parent_path() / "no-such-dir" / "copy.rwdesign";
        EXPECT_THROW(f.saveAsService->saveAs(options), std::invalid_argument);
    }
    EXPECT_EQ(countSaveAsResidue(f.dir.parent_path()), residueBefore)
        << "违约路径零磁盘副作用";

    // 状态轨：源上下文关闭后拒绝（Closed 即不可用——服务消费②端口）。
    EXPECT_EQ(f.store().requestClose(), 0u);
    {
        SaveAsOptions options;
        options.targetDir = f.dir.parent_path() / (f.dir.filename().string() + "-closed");
        const SaveAsResult result = f.saveAsService->saveAs(options);
        EXPECT_FALSE(result.ok);
        ASSERT_TRUE(result.error.has_value());
        EXPECT_EQ(result.error->code(), StoreErrorCode::ContextClosed);
        EXPECT_FALSE(fs::exists(options.targetDir));
    }
}
