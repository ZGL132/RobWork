/**
 * @file   PackageServiceTest.cpp
 * @brief  PRJ-T18 项目包服务用例组（§8.10 导出/导入协议——roundtrip
 *         逐字节还原、D-17 快照一致性、取消清理、编解码失败诊断透传、
 *         调用方错误 fail-fast）。
 *
 * 设计依据：
 *   - units/project.md §8.10（导出＝开始时快照一致数据视图〔D-17〕＋
 *     后台进度可取消〔取消即清理临时输出〕；导入＝解包临时目录全量
 *     校验→打开协议校验→整体 rename 发布，失败/取消不留目标目录、
 *     清理临时区）、§8.7（导入的打开协议校验）、§10.4（io 分工——
 *     编解码/预算/穿越防护由注入实现承担，project 透传不解释）、
 *     D-17（快照一致视图——后台写入并存仍一致）；
 *   - units/io.md §7/§9.12（协议语义与 io 域诊断 token 的只读参考——
 *     fake 直书收编 token，零 io 头消费）；
 *   - 需求 PM-05、NFR-SEC-01/02（包通道路径/预算防护由编解码承担）、
 *     AT-13、AT-20；
 *   - 任务契约 tasks/foundation/PRJ-T18.json acceptance 3。
 *
 * 观测点纪律：编解码侧行为经 fake 的注入面/观测面承载（取消计数/
 *   预算/失败码）；project 侧行为经结果结构（ok/canceled/diagnostics
 *   透传）与磁盘事实（目标产出/临时区残量/导入项目字节）断言。
 */

#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/project/DraftService.hpp>
#include <sdurws/ird/project/PackageService.hpp>
#include <sdurws/ird/project/PersistenceFormat.hpp>
#include <sdurws/ird/project/ProjectStore.hpp>
#include <sdurws/ird/project/QueryPort.hpp>
#include <sdurws/ird/testkit/Fixture.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include "PackageStubs.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// 测试侧命名空间别名（core 为 sdurws::ird 的嵌套命名空间——显式别名
// 让断言代码保持与产品代码同形的限定名）。
namespace core = sdurws::ird::core;

using namespace sdurws::ird::project;
using sdurws::ird::project::stub::PackageFixture;

namespace {

/// 读取整个文件（磁盘事实观测辅助——SaveAsServiceTest 同款）。
std::string readAllFile(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

/// 目标父目录下的 rwpack-import 临时位残存量（PM-05 清理观测点）。
std::size_t countImportResidue(const fs::path& parent)
{
    std::size_t n = 0;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(parent, ec)) {
        const std::string name = entry.path().filename().string();
        if (name.rfind(".rwpack-import-", 0) == 0) {
            ++n;
        }
    }
    return n;
}

}  // namespace

// =====================================================================
// 用例组：PackageRoundtrip（§8.10——导出导入逐字节还原）
// =====================================================================

/**
 * 锚定：PRJ-T18 acceptance 3／§8.10／AT-20——包＝项目的整体搬运：
 * 导入不改写 projectId；逐字节还原（HEAD/对象/草稿字节与源全等）；
 * 引用一致（修订闭包可解析、运行清单与源全等——五元组原样）。
 *
 * 前置：健康源项目（修订＋运行＋草稿＋报告——PackageFixture）。
 * 操作：全勾选导出 → 导入到新目录 → 打开进入。
 * 预期：
 *   - 导出 ok（fileCount＝快照清单数）；导入 ok 且 store 非空；
 *   - importedProjectId ＝ 源 projectId（身份保持——与另存为换新相反）；
 *   - HEAD 文件字节逐字节相同（导入零改写的字节级口径）；
 *   - 闭包逐修订可解析；对象字节逐字节相同；
 *   - 草稿可加载且负载逐字节相同、归属＝源 projectId；
 *   - 运行清单与源全等（五元组原样）；
 *   - 报告树复制（rep-with-run/rep-no-ref 在；rep-missing-ref 被过滤——
 *     与另存为同源过滤语义）；
 *   - 临时位零残留。
 * 观测点：磁盘字节＋查询端口互证。
 */
TEST(PackageRoundtrip, ExportImport_ByteRestore)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-05"},
                  std::vector<std::string>{"AT-20"});
    PackageFixture f("ird-prj-t18-pack-roundtrip");

    const core::ProjectId sourceId = f.store().projectId();
    const RevisionView sourceHead = f.store().query().head();
    const std::vector<RunInfo> sourceRuns
        = f.store().query().listRuns(sourceHead.id);
    ASSERT_EQ(sourceRuns.size(), std::size_t{1});

    // ---- 导出（全勾选）。
    const fs::path packFile = f.dir.parent_path() / "roundtrip.rwpack";
    PackageExportOptions exportOptions;
    exportOptions.targetFile = packFile;
    exportOptions.includeResults = true;
    exportOptions.includeReports = true;
    exportOptions.includeDrafts = true;
    const PackageExportResult exported = f.packageService->exportPackage(exportOptions);
    ASSERT_TRUE(exported.ok) << (exported.error ? exported.error->what() : "ok==false");
    EXPECT_TRUE(fs::exists(packFile)) << "导出目标应就位（原子替换成功）";
    EXPECT_GT(exported.fileCount, std::uint64_t{0});

    // ---- 导入（发布到新目录并打开进入）。
    const fs::path targetDir = f.dir.parent_path() / (f.dir.filename().string() + "-imported");
    PackageImportResult imported = f.packageService->importPackage(packFile, targetDir);
    ASSERT_TRUE(imported.ok) << (imported.error ? imported.error->what() : "ok==false");
    ASSERT_NE(imported.store, nullptr);
    ProjectStore& importStore = *imported.store;
    // 装配对齐（hasUnresolvedPayload 判据的会话面——同 SaveAsServiceTest
    // 口径：两侧注册同形后视图全等断言才反映数据本身）。
    importStore.handlerRegistry().registerHandler(
        std::make_unique<stub::AppendObjectHandler>());

    // 身份保持（导入不改写 projectId——项目整体搬运）。
    EXPECT_TRUE(imported.importedProjectId.isValid());
    EXPECT_EQ(imported.importedProjectId, sourceId);
    EXPECT_EQ(importStore.projectId(), sourceId);

    // 逐字节还原：HEAD 文件字节与源全等（导入零改写）。
    EXPECT_EQ(readAllFile(targetDir / "HEAD"), readAllFile(f.dir / "HEAD"));

    // 引用一致：闭包逐修订可解析＋对象字节逐字节相同。
    std::optional<core::RevisionId> cursor = sourceHead.id;
    while (cursor.has_value()) {
        const std::optional<RevisionView> sourceRev
            = f.store().query().tryRevision(*cursor);
        ASSERT_TRUE(sourceRev.has_value());
        const std::optional<RevisionView> importRev
            = importStore.query().tryRevision(*cursor);
        ASSERT_TRUE(importRev.has_value())
            << "导入闭包必须可解析: " << cursor->toCanonical();
        EXPECT_TRUE(*importRev == *sourceRev);
        for (const ObjectRef& ref : sourceRev->objectRefs) {
            EXPECT_TRUE(importStore.query().object(ref.objectId, ref.contentVersion)
                        == f.store().query().object(ref.objectId, ref.contentVersion))
                << "导入对象字节失真: " + ref.objectId.toCanonical();
        }
        cursor = sourceRev->parent;
    }

    // 草稿：可加载、归属＝源 projectId、负载逐字节相同。
    const core::BranchId importBranch
        = importStore.query().currentMetadata().record.primaryBranchId;
    std::vector<core::DiagnosticRecord> draftDiags;
    const std::optional<DraftDocument> importedDraft
        = importStore.drafts().tryLoad(importBranch, "modeling", draftDiags);
    ASSERT_TRUE(importedDraft.has_value());
    EXPECT_TRUE(draftDiags.empty()) << "导入草稿归属校验应零诊断";
    EXPECT_EQ(importedDraft->projectId, sourceId);
    EXPECT_EQ(importedDraft->payload, "{\"draft\":1}");

    // 运行清单与源全等（五元组原样——D-13 完整运行跨包保持）。
    const std::vector<RunInfo> importRuns
        = importStore.query().listRuns(importStore.query().head().id);
    ASSERT_EQ(importRuns.size(), std::size_t{1});
    EXPECT_TRUE(importRuns[0] == sourceRuns[0]);

    // 报告树：引用完整性过滤同源（rep-missing-ref 不入包）。
    EXPECT_TRUE(fs::exists(targetDir / "reports" / "rep-with-run"));
    EXPECT_TRUE(fs::exists(targetDir / "reports" / "rep-no-ref"));
    EXPECT_FALSE(fs::exists(targetDir / "reports" / "rep-missing-ref"));

    // 临时位零残留＋恢复诊断面健康。
    EXPECT_EQ(countImportResidue(f.dir.parent_path()), std::size_t{0});
    EXPECT_TRUE(imported.diagnostics.empty());

    importStore.requestClose();
}

// =====================================================================
// 用例组：PackageExportD17（D-17——快照一致数据视图）
// =====================================================================

/**
 * 锚定：PRJ-T18 acceptance 3／§8.10／D-17——导出开始时快照（HEAD 闭
 * 包＋所选树清单），后台写入并存期间导出仍一致：并发提交产生的新修
 * 订/对象不入包，包内容锚定快照 HEAD。
 *
 * 前置：健康源项目（HEAD＝r1）；fake 编解码挂"枚举完成"钩子。
 * 操作：钩子内对源项目提交 r2（后台写入并存的进程内模拟）→ 导出全
 * 勾选 → 导入。
 * 预期：
 *   - 导出成功（读取全部命中不可变文件——快照清单自洽）；
 *   - 快照清单（fake lastEnumerated）不含 r2 的任何文件；
 *   - 导入项目的 HEAD＝r1（快照锚——非提交后状态）；r2 在源磁盘存在
 *     （提交真实发生——并发面真实性）；
 *   - 源项目零写副作用（导出结构性只读——HEAD 前进来自钩子提交而非
 *     导出本身）。
 * 观测点：fake 清单观测面＋导入项目 HEAD＋源 HEAD 时序。
 */
TEST(PackageExportD17, SnapshotAnchoredDuringConcurrentCommit)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-05"},
                  std::vector<std::string>{});
    PackageFixture f("ird-prj-t18-pack-d17");

    const RevisionView headBefore = f.store().query().head();

    // 钩子：枚举完成后对源项目提交 r2（真实命令提交——写路径并发面）。
    core::RevisionId committedDuringExport{};
    f.codec.afterEnumerate = [&] {
        const CommandResult committed
            = f.store().commands().submit(f.appendEnvelope("r2-during-export"));
        ASSERT_TRUE(committed.status.committed())
            << "D-17 钩子内的并发提交失败（预期 Committed）";
        ASSERT_TRUE(committed.newRevision.has_value());
        committedDuringExport = *committed.newRevision;
    };

    const fs::path packFile = f.dir.parent_path() / "d17.rwpack";
    PackageExportOptions exportOptions;
    exportOptions.targetFile = packFile;
    exportOptions.includeResults = true;
    exportOptions.includeDrafts = true;
    const PackageExportResult exported = f.packageService->exportPackage(exportOptions);
    ASSERT_TRUE(exported.ok) << (exported.error ? exported.error->what() : "ok==false");

    // 快照清单锚定：不含并发提交引入的任何文件（清单在提交前冻结）。
    for (const PackFileEntry& entry : f.codec.lastEnumerated) {
        EXPECT_EQ(entry.packPath.find(committedDuringExport.toCanonical()),
                  std::string::npos)
            << "快照清单不得含并发提交的修订: " + entry.packPath;
    }

    // 导入：包内容＝快照（HEAD＝r1），而非提交后状态（HEAD≠r2）。
    const fs::path targetDir = f.dir.parent_path() / (f.dir.filename().string() + "-d17");
    PackageImportResult imported = f.packageService->importPackage(packFile, targetDir);
    ASSERT_TRUE(imported.ok) << (imported.error ? imported.error->what() : "ok==false");
    EXPECT_EQ(imported.store->query().head().id, headBefore.id)
        << "包内容必须锚定快照 HEAD（D-17）——并发提交不入包";

    // 并发面真实性：源磁盘 HEAD 已前进到 r2（提交真实发生）。
    EXPECT_EQ(f.store().query().head().id, committedDuringExport);
    EXPECT_NE(committedDuringExport, headBefore.id);

    imported.store->requestClose();
}

// =====================================================================
// 用例组：PackageCancel（PM-05——取消＝正常路径，目标零产出）
// =====================================================================

/**
 * 锚定：PRJ-T18 acceptance 3／§8.10"取消即清理临时输出"／PM-05——
 * 取消＝正常路径非错误（canceled 位承载，error 必空）；目标路径无任何
 * 产出；临时区清理。
 *
 * 前置：健康源项目；fake 取消注入（导出第 1 个检查点、导入第 2 个
 * 检查点各一路）。
 * 操作：注入后导出/导入。
 * 预期（两路同构）：canceled==true、ok==false、error nullopt、
 * diagnostics 空；目标零产出；.rwpack-import-* 零残留。
 * 观测点：结果结构＋磁盘事实。
 */
TEST(PackageCancel, CheckpointCancel_NoTargetOutput_NoResidue)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-05"},
                  std::vector<std::string>{"AT-13"});
    PackageFixture f("ird-prj-t18-pack-cancel");
    const std::string sourceHeadBefore = readAllFile(f.dir / "HEAD");

    // ---- 路 1：导出取消（首个检查点即取消——取消回调恒真）。
    {
        f.codec.cancelAfterFile = 0;
        const fs::path packFile = f.dir.parent_path() / "cancel-export.rwpack";
        PackageExportOptions options;
        options.targetFile = packFile;
        options.includeDrafts = true;
        options.onCancel = [] { return true; };
        const PackageExportResult result = f.packageService->exportPackage(options);
        EXPECT_TRUE(result.canceled);
        EXPECT_FALSE(result.ok);
        EXPECT_FALSE(result.error.has_value()) << "取消非错误——error 必空";
        EXPECT_TRUE(result.diagnostics.empty());
        EXPECT_FALSE(fs::exists(packFile)) << "取消后目标零产出";
    }

    // ---- 路 2：导入取消（第 2 个条目后取消——解包中途边界）。
    {
        // 先取得一份健康包（取消注入复位）。
        f.codec.cancelAfterFile = -1;
        const fs::path packFile = f.dir.parent_path() / "cancel-import.rwpack";
        PackageExportOptions exportOptions;
        exportOptions.targetFile = packFile;
        exportOptions.includeDrafts = true;
        ASSERT_TRUE(f.packageService->exportPackage(exportOptions).ok);

        f.codec.cancelAfterFile = 1;
        const fs::path targetDir
            = f.dir.parent_path() / (f.dir.filename().string() + "-cancel-import");
        const PackageImportResult result
            = f.packageService->importPackage(packFile, targetDir, {}, [] { return true; });
        EXPECT_TRUE(result.canceled);
        EXPECT_FALSE(result.ok);
        EXPECT_FALSE(result.error.has_value());
        EXPECT_FALSE(fs::exists(targetDir)) << "取消不留目标目录（PM-05）";
        EXPECT_EQ(countImportResidue(f.dir.parent_path()), std::size_t{0})
            << "取消清理临时区";
    }

    // 源项目零影响（导出结构性只读）。
    EXPECT_EQ(readAllFile(f.dir / "HEAD"), sourceHeadBefore);
    f.codec.cancelAfterFile = -1;
}

// =====================================================================
// 用例组：PackageImportFailures（NFR-SEC-01/02——编解码失败透传，
//        失败不留目标目录）
// =====================================================================

/**
 * 锚定：PRJ-T18 acceptance 3／§8.10/PM-05——编解码全量校验失败（损坏
 * 包/路径穿越/预算超限）＝ok=false＋io 域诊断透传（project 不解释不
 * 映射不私造 PRJ 码——CR-08/§10.4）；目标目录从未写入、临时区清理；
 * 导出侧预算同表（IO-SEC-BUDGET-TEMP）。
 *
 * 前置：健康源项目＋一份健康导出包（失败样例经 fake 注入面驱动）。
 * 操作：三路失败注入后导入；一路预算注入后导出。
 * 预期：各自 ok=false＋diagnostics[0].code 为对应 io 收编 token；目标
 * 零写入；临时位零残留。
 * 观测点：诊断码透传（逐字）＋磁盘事实。
 */
TEST(PackageImportFailures, DiagnosticsPassedThrough_NoTargetNoResidue)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-05", "NFR-SEC-01", "NFR-SEC-02"},
                  std::vector<std::string>{"AT-13"});
    PackageFixture f("ird-prj-t18-pack-fail");

    // 健康包（失败样例的载体）。
    const fs::path packFile = f.dir.parent_path() / "sample.rwpack";
    PackageExportOptions exportOptions;
    exportOptions.targetFile = packFile;
    exportOptions.includeDrafts = true;
    ASSERT_TRUE(f.packageService->exportPackage(exportOptions).ok);

    // ---- 路 1：损坏包（哈希不匹配样例——IO-PACK-HASH-MISMATCH）。
    {
        f.codec.forcedFailCode = "IO-PACK-HASH-MISMATCH";
        const fs::path targetDir
            = f.dir.parent_path() / (f.dir.filename().string() + "-corrupt");
        const PackageImportResult result
            = f.packageService->importPackage(packFile, targetDir);
        EXPECT_FALSE(result.ok);
        EXPECT_FALSE(result.canceled);
        ASSERT_FALSE(result.diagnostics.empty());
        EXPECT_EQ(result.diagnostics[0].code, "IO-PACK-HASH-MISMATCH")
            << "io 域码透传（不映射不私造——CR-08）";
        EXPECT_FALSE(fs::exists(targetDir)) << "失败不留目标目录（PM-05）";
        EXPECT_EQ(countImportResidue(f.dir.parent_path()), std::size_t{0});
    }

    // ---- 路 2：路径穿越样例（IO-SEC-PATH-ESCAPE——条目名预检拒绝）。
    {
        f.codec.forcedFailCode = "IO-SEC-PATH-ESCAPE";
        const fs::path targetDir
            = f.dir.parent_path() / (f.dir.filename().string() + "-escape");
        const PackageImportResult result
            = f.packageService->importPackage(packFile, targetDir);
        EXPECT_FALSE(result.ok);
        ASSERT_FALSE(result.diagnostics.empty());
        EXPECT_EQ(result.diagnostics[0].code, "IO-SEC-PATH-ESCAPE");
        EXPECT_FALSE(fs::exists(targetDir));
        EXPECT_EQ(countImportResidue(f.dir.parent_path()), std::size_t{0});
    }

    // ---- 路 3：解包预算超限（IO-SEC-BUDGET-EXPAND——累计展开预算）。
    {
        f.codec.forcedFailCode.clear();
        f.codec.budgetBytes = 1;  // 任意包必超——预算语义的极小注入
        const fs::path targetDir
            = f.dir.parent_path() / (f.dir.filename().string() + "-budget");
        const PackageImportResult result
            = f.packageService->importPackage(packFile, targetDir);
        EXPECT_FALSE(result.ok);
        ASSERT_FALSE(result.diagnostics.empty());
        EXPECT_EQ(result.diagnostics[0].code, "IO-SEC-BUDGET-EXPAND");
        EXPECT_FALSE(fs::exists(targetDir));
        EXPECT_EQ(countImportResidue(f.dir.parent_path()), std::size_t{0});
    }

    // ---- 路 4：导出预算超限（IO-SEC-BUDGET-TEMP——TempAreaBytes 面；
    // 目标文件不产出）。
    {
        f.codec.budgetBytes = 1;
        const fs::path targetFile = f.dir.parent_path() / "budget-export.rwpack";
        PackageExportOptions options;
        options.targetFile = targetFile;
        options.includeDrafts = true;
        const PackageExportResult result = f.packageService->exportPackage(options);
        EXPECT_FALSE(result.ok);
        ASSERT_FALSE(result.diagnostics.empty());
        EXPECT_EQ(result.diagnostics[0].code, "IO-SEC-BUDGET-TEMP");
        EXPECT_FALSE(fs::exists(targetFile));
    }

    f.codec.forcedFailCode.clear();
    f.codec.budgetBytes = ~static_cast<std::uint64_t>(0);
}

// =====================================================================
// 用例组：PackageContract（调用方错误 fail-fast 与装配违约）
// =====================================================================

/**
 * 锚定：PRJ-T18 契约的调用方错误面——codec 空注入＝装配违约（构造期
 * fail-fast）；仅导入实例（source==nullptr）导出＝fail-fast；扩展名/
 * 父目录/包文件存在性/目标占用＝fail-fast（向导应先行校验的场景）。
 *
 * 前置：健康源项目。
 * 操作：违约矩阵逐项调用。
 * 预期：全部 std::invalid_argument；零磁盘副作用。
 * 观测点：异常类型。
 */
TEST(PackageContract, CallerErrorsFailFast_AssemblyViolations)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-05"},
                  std::vector<std::string>{});
    PackageFixture f("ird-prj-t18-pack-contract");

    // 装配违约：codec 未注入（构造期 fail-fast——装配错误不过夜）。
    EXPECT_THROW(createPackageService(f.opened.store.get(), nullptr),
                 std::invalid_argument);

    // 仅导入实例（source==nullptr）不可导出。
    {
        std::unique_ptr<IPackageService> importOnly
            = createPackageService(nullptr, &f.codec, &f.bus, &f.sink);
        PackageExportOptions options;
        options.targetFile = f.dir.parent_path() / "no-source.rwpack";
        EXPECT_THROW(importOnly->exportPackage(options), std::invalid_argument);
    }

    // 扩展名契约（§5.8 冻结注释".rwpack"）。
    {
        PackageExportOptions options;
        options.targetFile = f.dir.parent_path() / "wrong-ext.zip";
        EXPECT_THROW(f.packageService->exportPackage(options), std::invalid_argument);
    }
    // 导出父目录不存在。
    {
        PackageExportOptions options;
        options.targetFile = f.dir.parent_path() / "no-such-dir" / "x.rwpack";
        EXPECT_THROW(f.packageService->exportPackage(options), std::invalid_argument);
    }

    // 导入：包文件不存在／目标已存在／父目录不存在。
    {
        EXPECT_THROW(f.packageService->importPackage(
                         f.dir.parent_path() / "missing.rwpack",
                         f.dir.parent_path() / "imp-1"),
                     std::invalid_argument);
        EXPECT_THROW(f.packageService->importPackage(f.dir,  // 已存在的路径
                                                     f.dir.parent_path() / "imp-2"),
                     std::invalid_argument);
        EXPECT_THROW(f.packageService->importPackage(
                         f.dir.parent_path() / "missing.rwpack",
                         f.dir.parent_path() / "no-such-dir" / "imp-3"),
                     std::invalid_argument);
    }
}
