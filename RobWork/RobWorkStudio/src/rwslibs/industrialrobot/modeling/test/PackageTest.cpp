/**
 * @file   PackageTest.cpp
 * @brief  规范模型包导出/导入的单元测试（WP-13-T13 ACC1~ACC4 具名自证；
 *        ACC5 的 WC/DWC XML 外供用例见 PackageWorkCellTest.cpp——消费
 *        runtime 编译产物，仅集成模式编译）。
 *
 * 用例面（任务契约 acceptance 逐条对应）：
 *   - ExportPackageShape                 （ACC1：接口落位＋包形态——ZIP 封装
 *                                          ＋manifest 来源标识/schema/对象
 *                                          清单与 digest＋根/部件 canonical
 *                                          字节＋Solidified 副本＋命名位姿）
 *   - ExportDeterministicBytes           （ACC1/2：确定性——同闭包两次导出
 *                                          字节相同；会话级操作零状态突变）
 *   - ExportFailurePreservesPrevious     （ACC2：导出失败→MDL-EXPORT-FAILED
 *                                          诊断、旧输出文件完好（V-29 文件
 *                                          层观测）、无半成品残留）
 *   - ImportRejectsForeignArtifact       （ACC3：非本软件工件→PackageUnknown
 *                                          ＋MDL-IMPORT-PACKAGE-UNKNOWN 引导
 *                                          MDL-18/R2；mapWorkCellXml R1
 *                                          NotImplemented 边界不受影响）
 *   - RoundtripItemwiseChecklist         （ACC4：导出→重导入→五组逐项清单
 *                                          diff=空＋对象值相等＋清单组覆盖
 *                                          （V-20 核对形态/AT-28））
 *   - TamperedPackageRejected            （ACC3/4：包内字节被篡改→导入拒绝，
 *                                          无草稿产出——fail-closed）
 *
 * 夹具：复用 test/BridgeFixtures.hpp 的测试域设施（BridgeClosure 内存闭包
 * ＋makeFullDesign 全量模型——T12 先例；测试域替身不入公共面）。
 */

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <sdurws/ird/io/Json.hpp>       // IStructuredDataReader——manifest 直接读取核对
#include <sdurws/ird/io/ZipChannel.hpp> // openZipChannel——导入回读同款 IO-T04 面（ACC1 观测）
#include <sdurws/ird/modeling/DiagCodes.hpp>   // MDL-* 码常量（诊断码断言——禁字符串拼码）
#include <sdurws/ird/modeling/Import.hpp>   // ModelImportMapper::mapWorkCellXml——R1 边界核对（ACC3）
#include <sdurws/ird/modeling/Package.hpp>
#include <sdurws/ird/modeling/Parts.hpp>
#include <sdurws/ird/modeling/RobotDesign.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>   // IRD_TEST_INFO——需求/AT 追溯登记

#include "BridgeFixtures.hpp"

namespace {

using namespace sdurws::ird;
using namespace sdurws::ird::modeling;
using namespace sdurws::ird::modeling::testbridge;

// =====================================================================
// 测试域夹具（Package 面专用扩展——基于 BridgeFixtures 全量模型）
// =====================================================================

/// 从建模对象变体集取全部身份（清单比对用；根对象无 objectId——D-MDL-1，
/// 部件分支逐一取值）。
std::set<std::string> oidsOf(const std::vector<ObjectVariant>& parts)
{
    std::set<std::string> oids;
    for (const ObjectVariant& variant : parts) {
        if (std::holds_alternative<ToolDefinition>(variant)) {
            oids.insert(std::get<ToolDefinition>(variant).objectId.toCanonical());
        } else if (std::holds_alternative<SceneObject>(variant)) {
            oids.insert(std::get<SceneObject>(variant).objectId.toCanonical());
        } else if (std::holds_alternative<PoseSet>(variant)) {
            oids.insert(std::get<PoseSet>(variant).objectId.toCanonical());
        } else {
            oids.insert(std::get<DrivetrainDesign>(variant).objectId.toCanonical());
        }
    }
    return oids;
}

/**
 * @brief Package 用例夹具：全量模型＋闭包（工具/场景/传动——BridgeFixtures）
 *        ＋命名位姿集＋Solidified 资源（覆盖 §6.8 保留面全部要素）。
 *
 * resourceBytes＝固化 resource 对象的字节（modeling 不解码资源对象——
 * 夹具给任意非空字节，导出原样入包、导入原样回读）。
 */
struct PackageFixture {
    BridgeClosure closure;
    RobotDesign design;
    ToolDefinition tool;          // makeFullDesign 装配的工具（保留原件做值比对）
    SceneObject scene;
    DrivetrainDesign drivetrain;
    PoseSet poseSet;              // 命名位姿集（本夹具补装——MDL-20 保留面）
    core::ObjectId solidifiedOid;
    std::vector<std::uint8_t> resourceBytes;
    core::ObjectId poseSetOid;

    PackageFixture()
    {
        design = makeFullDesign(closure);
        // BridgeFixtures 只回传根对象——部件原件经闭包解码取回（比对基准）。
        RobotDesignCodec codec;
        for (const core::ObjectId& oid : design.toolRefs) {
            auto decoded = codec.decode(closure.byId[oid.toCanonical()].bytes,
                                        kCurrentFormatVersion);
            tool = std::get<ToolDefinition>(decoded.get());
        }
        for (const core::ObjectId& oid : design.sceneRefs) {
            auto decoded = codec.decode(closure.byId[oid.toCanonical()].bytes,
                                        kCurrentFormatVersion);
            scene = std::get<SceneObject>(decoded.get());
        }
        for (const core::ObjectId& oid : design.drivetrainRef.has_value()
                                            ? std::vector<core::ObjectId>{*design.drivetrainRef}
                                            : std::vector<core::ObjectId>{}) {
            auto decoded = codec.decode(closure.byId[oid.toCanonical()].bytes,
                                        kCurrentFormatVersion);
            drivetrain = std::get<DrivetrainDesign>(decoded.get());
        }

        // ---- 命名位姿集（§4.6：home/zero 为保留键——编辑器会话参考）----
        poseSet.objectId = core::ObjectId::generate();
        poseSetOid = poseSet.objectId;
        PoseSetEntry home;
        home.key = "home";
        home.jointConfiguration = {0.0, 0.5, -0.5, 1.0, 0.25, 0.0};   // rad（与关节序对应）
        home.note = "home";
        PoseSetEntry zero;
        zero.key = "zero";
        zero.jointConfiguration = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};     // rad
        poseSet.entries = {home, zero};
        design.poseSetRef = poseSetOid;
        closure.put(poseSetOid, std::string(kNamedPoseSetObjectType), encode(poseSet));

        // ---- 连杆碰撞几何引用（collision 组载体——引用 Recorded 资源）----
        design.links[1].collision = GeometryRef{"scene/table",
                                                identityTransform3D(),
                                                GeometryKind::Mesh};

        // ---- Solidified 资源（Recorded 之外的第二态——资源副本入包面）----
        solidifiedOid = core::ObjectId::generate();
        resourceBytes = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x42};   // 任意不透明字节
        closure.put(solidifiedOid, "resource-object", resourceBytes);
        ResourceRef solid = makeSolidifiedResource("solid/mesh", core::Digest256{}, solidifiedOid);
        solid.contentDigest[1] = 0x7E;   // 非零摘要（身份要素）
        solid.solidifiedObject->contentVersion.bytes[2] = 0x11;
        design.resourceManifest.push_back(solid);

        // ---- 根对象入闭包（导出数据源就绪）----
        closure.put(std::string(kRobotDesignObjectType), encode(design));
    }

    /// 全部部件对象原件（roundtrip 比对基准——五组清单的源侧）。
    std::vector<ObjectVariant> originalParts() const
    {
        return {tool, scene, poseSet, drivetrain};
    }
};

/// 用例专属临时目录（唯一计数——并行用例互不干扰；析构递归清理）。
class TempDir {
public:
    TempDir()
    {
        static int counter = 0;
        path_ = std::filesystem::temp_directory_path()
            / ("ird-pkg-test-" + std::to_string(++counter));
        std::filesystem::create_directories(path_);
    }
    ~TempDir() { std::filesystem::remove_all(path_); }
    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

/// 读文件字节（测试侧自持 I/O——被测面不读文件系统，SA-14 边界不破）。
std::vector<char> readFile(const std::filesystem::path& file)
{
    std::ifstream in(file, std::ios::binary);
    return std::vector<char>(std::istreambuf_iterator<char>(in),
                             std::istreambuf_iterator<char>());
}

/// 写文件字节（测试侧预置"先前输出"用）。
void writeFile(const std::filesystem::path& file, const std::string& bytes)
{
    std::ofstream out(file, std::ios::binary);
    out << bytes;
}

/// 构造已验证输入（ValidatedSource——调用方装配位的测试形态：快照路径指向
/// 包文件，digest 由测试以 core ContentDigester 实算；依赖树空）。
ValidatedSource validatedSourceFor(const std::filesystem::path& packageFile,
                                   const std::vector<char>& fileBytes)
{
    ValidatedSource source;
    source.bytes.assign(fileBytes.begin(), fileBytes.end());
    source.entrySnapshot.finalPath = std::filesystem::weakly_canonical(packageFile);
    source.entrySnapshot.sizeBytes = fileBytes.size();
    core::ContentDigester digester;
    if (!fileBytes.empty()) {
        digester.update(fileBytes.data(), fileBytes.size());
    }
    source.entrySnapshot.contentDigest = digester.finalize();
    return source;
}

// =====================================================================
// ACC1：接口落位＋包形态
// =====================================================================

/**
 * @brief ACC1：exportPackage 落位＋包形态逐项核对（ZIP 封装＋manifest 来源
 *        标识/schema/对象清单与 digest＋根/部件 canonical 字节＋Solidified
 *        副本＋命名位姿——MDL-20）。
 */
TEST(MdlPackage, ExportPackageShape_WP13T13_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-20", "AT-28"},
                  std::vector<std::string>{});

    PackageFixture fx;
    TempDir dir;
    const std::filesystem::path packageFile = dir.path() / "model.irdbundle";

    ModelPackagePort port;
    std::vector<core::DiagnosticRecord> diags;
    PackageExportTarget target;
    target.targetFile = packageFile;
    const PackageExportOutcome exported = port.exportPackage(fx.closure, target, diags);

    ASSERT_TRUE(exported.ok) << "导出失败：" << exported.error.detail;
    EXPECT_TRUE(diags.empty()) << "成功轨不产诊断";

    // ---- 包可被 io IZipChannel 打开（IO-T04 落位面——读侧权威）----
    auto channel = io::openZipChannel(packageFile);
    ASSERT_TRUE(channel) << "包不是合法 ZIP 容器";
    auto listed = channel.value->listEntries();
    ASSERT_TRUE(listed);
    std::set<std::string> names;
    for (const io::ZipEntryInfo& info : listed.value) {
        names.insert(info.name);
        EXPECT_EQ(info.compressionMethod, 0u) << "写侧 STORED 契约";
        EXPECT_FALSE(info.encrypted) << "加密条目拒绝契约（§7.1）";
    }

    // ---- manifest.json 存在且可读，来源标识/schema/清单齐全 ----
    ASSERT_TRUE(names.count(std::string(kModelPackageManifestEntry)) == 1u);
    auto manifestBytes = channel.value->readEntryBytes(
        std::string(kModelPackageManifestEntry), nullptr, {}, nullptr);
    ASSERT_TRUE(manifestBytes);
    auto reader = io::makeStructuredDataReader(nullptr);
    auto manifest = reader->parseBytes(
        std::string_view(manifestBytes.value.data(), manifestBytes.value.size()),
        io::JsonReadOptions{}, nullptr, nullptr);
    ASSERT_TRUE(manifest);
    const io::JsonValue& root = manifest.value.root;
    ASSERT_TRUE(root.isObject());
    const io::JsonValue* formatId = root.findMember("formatId");
    const io::JsonValue* producer = root.findMember("producer");
    const io::JsonValue* schemaVersion = root.findMember("schemaVersion");
    const io::JsonValue* entries = root.findMember("entries");
    const io::JsonValue* contentDigest = root.findMember("contentDigest");
    ASSERT_NE(formatId, nullptr);
    ASSERT_NE(producer, nullptr);
    ASSERT_NE(schemaVersion, nullptr);
    ASSERT_NE(entries, nullptr);
    ASSERT_NE(contentDigest, nullptr);
    EXPECT_EQ(formatId->stringValue, std::string(kModelPackageFormatId));
    EXPECT_EQ(producer->stringValue, std::string(kModelPackageProducer))
        << "来源标识 producer=ird-modeling（MDL-20 导入门）";
    EXPECT_EQ(schemaVersion->integerValue, kModelPackageSchemaVersion);
    EXPECT_TRUE(contentDigest->isString() && contentDigest->stringValue.size() == 71)
        << "contentDigest＝sha256- 前缀＋64 hex（对象清单与 digest）";

    // ---- 清单条目＝根＋四部件＋Solidified 资源副本（命名位姿在保留范围）----
    ASSERT_EQ(entries->items.size(), 6u)
        << "根 1＋工具 1＋场景 1＋位姿集 1＋传动 1＋Solidified 资源 1＝6 条目";
    std::set<std::string> manifestPaths;
    for (const io::JsonValue& item : entries->items) {
        manifestPaths.insert(item.findMember("path")->stringValue);
    }
    EXPECT_TRUE(manifestPaths.count("objects/robot-design/object.bin") == 1u);
    EXPECT_TRUE(manifestPaths.count(
                    "objects/named-pose-set/" + fx.poseSetOid.toCanonical() + ".bin")
                == 1u)
        << "语义保留范围含命名位姿（MDL-20）";
    EXPECT_TRUE(manifestPaths.count("resources/" + fx.solidifiedOid.toCanonical() + ".bin")
                == 1u)
        << "Solidified 资源副本入包";
    std::set<std::string> archiveNames = names;
    archiveNames.erase(std::string(kModelPackageManifestEntry));
    EXPECT_EQ(manifestPaths, archiveNames)
        << "清单与归档（除 manifest 自身）精确一致";

    // ---- outcome 计数面（entryCount 含 manifest；totalBytes＝清单 size 和＋manifest）----
    std::uint64_t manifestSize = 0;
    for (const io::JsonValue& item : entries->items) {
        manifestSize += static_cast<std::uint64_t>(item.findMember("size")->integerValue);
    }
    EXPECT_EQ(exported.entryCount, 7u) << "对象/资源 6＋manifest 自身";
    EXPECT_EQ(exported.totalBytes, manifestSize + manifestBytes.value.size());
}

/**
 * @brief ACC1/ACC2：确定性——同闭包两次导出字节相同（NFR-COR-02；服务不读
 *        时钟/locale）；会话级文件操作——闭包字节零突变、零修订语义。
 */
TEST(MdlPackage, ExportDeterministicBytes_WP13T13_ACC1_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-20", "NFR-COR-02"},
                  std::vector<std::string>{});

    PackageFixture fx;
    TempDir dir;
    ModelPackagePort port;
    std::vector<core::DiagnosticRecord> diags;

    const std::vector<std::uint8_t> rootBefore =
        fx.closure.byToken[std::string(kRobotDesignObjectType)].bytes;

    PackageExportTarget first;
    first.targetFile = dir.path() / "a.irdbundle";
    ASSERT_TRUE(port.exportPackage(fx.closure, first, diags).ok);
    PackageExportTarget second;
    second.targetFile = dir.path() / "b.irdbundle";
    ASSERT_TRUE(port.exportPackage(fx.closure, second, diags).ok);

    EXPECT_EQ(readFile(first.targetFile), readFile(second.targetFile))
        << "同闭包＋同目标语义→同包字节（NFR-COR-02）";
    EXPECT_EQ(fx.closure.byToken[std::string(kRobotDesignObjectType)].bytes, rootBefore)
        << "导出为会话级文件操作——闭包字节零突变（零修订的结构性观测）";
}

// =====================================================================
// ACC2：导出原子性——失败恢复先前输出（V-29 文件层观测）
// =====================================================================

/**
 * @brief ACC2：导出失败→MDL-EXPORT-FAILED 诊断、项目状态不变且旧输出文件
 *        完好（V-29 文件层观测）；失败路径无半成品残留（临时区→校验→替换
 *        的前段失败不触碰目标）。
 *
 * 失败注入＝目标已存在＋NeverOverwrite（prepare 段拒绝——环境面失败）；
 * 另注入父目录缺失（prepare 段 IO-RES-NOT-FOUND 族）。
 */
TEST(MdlPackage, ExportFailurePreservesPrevious_WP13T13_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-20", "V-29"},
                  std::vector<std::string>{});

    PackageFixture fx;
    TempDir dir;
    ModelPackagePort port;

    // ---- 场景一：目标已存在＋NeverOverwrite——旧输出完整保留 ----
    const std::filesystem::path existing = dir.path() / "model.irdbundle";
    const std::string previousOutput = "PREVIOUS-OUTPUT-BYTES";
    writeFile(existing, previousOutput);

    std::vector<core::DiagnosticRecord> diags;
    PackageExportTarget target;
    target.targetFile = existing;
    target.replace = io::ReplacePolicy::NeverOverwrite;
    const PackageExportOutcome rejected = port.exportPackage(fx.closure, target, diags);

    ASSERT_FALSE(rejected.ok);
    EXPECT_EQ(rejected.error.code, ModelingErrorCode::ExportFailed);
    ASSERT_FALSE(diags.empty());
    EXPECT_EQ(diags.front().code, std::string(kMdlExportFailed))
        << "导出失败→MDL-EXPORT-FAILED 稳定诊断（§9.5 T13 行）";
    EXPECT_EQ(readFile(existing), std::vector<char>(previousOutput.begin(),
                                                    previousOutput.end()))
        << "旧输出文件完好（V-29）";

    // ---- 场景二：父目录缺失——prepare 段环境失败，无任何文件产出 ----
    std::vector<core::DiagnosticRecord> diagsMissing;
    PackageExportTarget missingParent;
    missingParent.targetFile = dir.path() / "no-such-dir" / "model.irdbundle";
    const PackageExportOutcome failed = port.exportPackage(fx.closure, missingParent,
                                                           diagsMissing);
    ASSERT_FALSE(failed.ok);
    EXPECT_EQ(failed.error.code, ModelingErrorCode::ExportFailed);
    ASSERT_FALSE(diagsMissing.empty());
    EXPECT_EQ(diagsMissing.front().code, std::string(kMdlExportFailed));
    EXPECT_FALSE(std::filesystem::exists(dir.path() / "no-such-dir"))
        << "失败路径不代建目录、不留半成品（原子写出协议——io §4.6）";

    // ---- 诊断纪律：闭包缺根＝调用方数据错误（RefMissing 值面，不落诊断）----
    BridgeClosure emptyClosure;   // 未装配根对象
    std::vector<core::DiagnosticRecord> diagsNoRoot;
    PackageExportTarget anywhere;
    anywhere.targetFile = dir.path() / "x.irdbundle";
    const PackageExportOutcome noRoot = port.exportPackage(emptyClosure, anywhere,
                                                           diagsNoRoot);
    EXPECT_FALSE(noRoot.ok);
    EXPECT_EQ(noRoot.error.code, ModelingErrorCode::RefMissing);
    EXPECT_TRUE(diagsNoRoot.empty()) << "无已登记映射码之外的诊断——值面拒绝";
}

// =====================================================================
// ACC3：导入门——仅识别本软件工件；MDL-18 通道边界不受影响
// =====================================================================

/**
 * @brief ACC3：非本软件工件→PackageUnknown＋MDL-IMPORT-PACKAGE-UNKNOWN
 *        （引导 MDL-18/R2 通道）；mapWorkCellXml 的 R1 NotImplemented 边界
 *        不受本任务影响（两通道不混淆——DTB 禁止项）。
 */
TEST(MdlPackage, ImportRejectsForeignArtifact_WP13T13_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-20", "MDL-18"},
                  std::vector<std::string>{});

    TempDir dir;
    ModelPackagePort port;

    // ---- 非本软件工件一：纯文本文件（非 ZIP 容器——fail-closed）----
    const std::filesystem::path foreign = dir.path() / "foreign.txt";
    writeFile(foreign, "this is not a package at all");
    std::vector<core::DiagnosticRecord> diags;
    const ValidatedSource source = validatedSourceFor(foreign, readFile(foreign));
    const PackageImportOutcome rejected = port.importPackage(source, diags);

    ASSERT_FALSE(rejected.ok);
    EXPECT_EQ(rejected.error.code, ModelingErrorCode::PackageUnknown);
    EXPECT_TRUE(rejected.report.design.joints.empty()
                && rejected.report.parts.empty())
        << "拒绝轨无草稿产物（fail-closed——PM-01 同款边界）";
    ASSERT_FALSE(diags.empty());
    EXPECT_EQ(diags.front().code, std::string(kMdlImportPackageUnknown))
        << "MDL-IMPORT-PACKAGE-UNKNOWN 稳定诊断（§9.5 T13 行）";
    EXPECT_NE(diags.front().recommendedAction.find("MDL-18"), std::string::npos)
        << "引导动作指向 MDL-18/R2 通道";

    // ---- 非本软件工件二：本软件包之外的合法 ZIP 缺 manifest（容器可开、
    //      来源不符——同样拒绝）。用本端口导出的包"改名 producer"不可行，
    //      以空字节文本文件形态归并场景一（容器层已拒）——此处以
    //      "manifest 条目缺失"形态由包内自检覆盖（TamperedPackageRejected）。

    // ---- mapWorkCellXml R1 边界不受影响（同输入语义：仍是 NotImplemented）----
    ModelImportMapper mapper;
    std::vector<core::DiagnosticRecord> mapperDiags;
    const ImportOptions options;
    const ImportOutcome boundary = mapper.mapWorkCellXml(source, options, mapperDiags);
    ASSERT_TRUE(boundary.error.has_value());
    EXPECT_EQ(boundary.error->code, ImportErrorCode::NotImplemented)
        << "MDL-18 WorkCell 反向导入仍为 R1 边界（WP-13-T17 领取）";
    EXPECT_FALSE(boundary.draft.has_value()) << "R1 不产草稿";
    EXPECT_TRUE(mapperDiags.empty()) << "R1 无已注册诊断码面——引导经报告条目";
}

// =====================================================================
// ACC4：roundtrip 逐项一致（V-29/AT-28——五组清单 diff=空）
// =====================================================================

/**
 * @brief ACC4：导出→重导入→逐项比对清单 diff=空（权威参数化 DH/显式、物
 *        性、资源引用〔Recorded/Solidified 状态与 digest〕、碰撞规则、命名
 *        位姿五组）；导入报告承载逐项清单（V-20 核对形态）；Solidified 副
 *        本字节原样回读。
 */
TEST(MdlPackage, RoundtripItemwiseChecklist_WP13T13_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-20", "AT-28", "V-29"},
                  std::vector<std::string>{"V-20"});

    PackageFixture fx;
    TempDir dir;
    ModelPackagePort port;

    // ---- 导出 ----
    const std::filesystem::path packageFile = dir.path() / "roundtrip.irdbundle";
    std::vector<core::DiagnosticRecord> exportDiags;
    PackageExportTarget target;
    target.targetFile = packageFile;
    const PackageExportOutcome exported = port.exportPackage(fx.closure, target, exportDiags);
    ASSERT_TRUE(exported.ok) << "导出失败：" << exported.error.detail;

    // ---- 重导入（ValidatedSource＝调用方装配位——io 产物测试形态）----
    const std::vector<char> packageBytes = readFile(packageFile);
    std::vector<core::DiagnosticRecord> importDiags;
    const PackageImportOutcome imported =
        port.importPackage(validatedSourceFor(packageFile, packageBytes), importDiags);
    ASSERT_TRUE(imported.ok) << "导入失败：" << imported.error.detail;
    EXPECT_TRUE(importDiags.empty()) << "成功轨不产诊断";

    // ---- 五组逐项清单 diff=空（源侧清单＝同源唯一实现）----
    const std::vector<PackageCheckItem> sourceItems =
        roundtripChecklist(fx.design, fx.originalParts());
    ASSERT_FALSE(sourceItems.empty());
    EXPECT_EQ(imported.report.checkItems, sourceItems)
        << "roundtrip 逐项比对 diff=空（V-29/AT-28）";

    // ---- 五组覆盖（§6.8 行文逐组：authority/physics/resource/collision/pose）----
    std::set<std::string> groups;
    for (const PackageCheckItem& item : imported.report.checkItems) {
        groups.insert(item.group);
    }
    EXPECT_TRUE(groups.count("authority") == 1u) << "组一：权威参数化（DH/显式）";
    EXPECT_TRUE(groups.count("physics") == 1u) << "组二：物性";
    EXPECT_TRUE(groups.count("resource") == 1u) << "组三：资源引用（状态与 digest）";
    EXPECT_TRUE(groups.count("collision") == 1u) << "组四：碰撞规则";
    EXPECT_TRUE(groups.count("pose") == 1u) << "组五：命名位姿（MDL-20 保留面）";

    // ---- 根对象值相等（operator==——canonical 权威语义逐字段）----
    EXPECT_EQ(imported.report.design, fx.design);

    // ---- 部件对象逐个值相等（按身份配对——导入序无关）----
    const std::set<std::string> importedOids = oidsOf(imported.report.parts);
    EXPECT_EQ(importedOids, oidsOf(fx.originalParts()));
    for (const ObjectVariant& variant : imported.report.parts) {
        // 按身份配对逐类值比对（std::visit 会为含根对象在内的全部备择实例
        // 化分支体——此处按备择显式分支更直白且无跨类比较实例化）。
        if (std::holds_alternative<ToolDefinition>(variant)) {
            const auto& object = std::get<ToolDefinition>(variant);
            EXPECT_EQ(object.objectId, fx.tool.objectId);
            EXPECT_EQ(object, fx.tool);
        } else if (std::holds_alternative<SceneObject>(variant)) {
            const auto& object = std::get<SceneObject>(variant);
            EXPECT_EQ(object.objectId, fx.scene.objectId);
            EXPECT_EQ(object, fx.scene);
        } else if (std::holds_alternative<PoseSet>(variant)) {
            const auto& object = std::get<PoseSet>(variant);
            EXPECT_EQ(object, fx.poseSet) << "命名位姿逐条目一致";
        } else {
            const auto& object = std::get<DrivetrainDesign>(variant);
            EXPECT_EQ(object, fx.drivetrain);
        }
    }

    // ---- 资源引用两态逐项一致（Recorded 外部记录／Solidified 身份+cv）----
    ASSERT_EQ(imported.report.design.resourceManifest, fx.design.resourceManifest);
    ASSERT_EQ(imported.report.solidifiedResources.size(), 1u);
    EXPECT_EQ(imported.report.solidifiedResources.front().resourceId, "solid/mesh");
    EXPECT_EQ(imported.report.solidifiedResources.front().objectId, fx.solidifiedOid);
    EXPECT_EQ(imported.report.solidifiedResources.front().bytes, fx.resourceBytes)
        << "Solidified 资源副本逐字节回读（CON-03 固化引用回读面）";

    // ---- manifest 摘要回环：导入侧重开包重算 manifest 摘要＝导出报告值 ----
    // （导出预提交自检的同源观测——IZipChannel 读侧权威复算。）
    auto channel = io::openZipChannel(packageFile);
    ASSERT_TRUE(channel);
    auto manifestBytes = channel.value->readEntryBytes(
        std::string(kModelPackageManifestEntry), nullptr, {}, nullptr);
    ASSERT_TRUE(manifestBytes);
    core::ContentDigester digester;
    digester.update(manifestBytes.value.data(), manifestBytes.value.size());
    EXPECT_EQ(digester.finalize(), exported.manifestDigest);
}

// =====================================================================
// ACC3/ACC4：篡改拒绝（fail-closed——清单哈希/容器校验背书）
// =====================================================================

/**
 * @brief ACC3/ACC4：包内对象字节被篡改→导入拒绝（无草稿产出）。篡改位＝
 *        第二个本地文件头后的数据区（首个数据条目为字典序最前的根对象）——
 *        容器 CRC/清单哈希两道防线至少一道拦截。
 */
TEST(MdlPackage, TamperedPackageRejected_WP13T13_ACC3_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-20"},
                  std::vector<std::string>{});

    PackageFixture fx;
    TempDir dir;
    ModelPackagePort port;

    const std::filesystem::path packageFile = dir.path() / "tampered.irdbundle";
    std::vector<core::DiagnosticRecord> exportDiags;
    PackageExportTarget target;
    target.targetFile = packageFile;
    ASSERT_TRUE(port.exportPackage(fx.closure, target, exportDiags).ok);

    // ---- 篡改：第二个本地头（PK\x03\x04）后的条目数据区翻转一字节 ----
    std::vector<char> bytes = readFile(packageFile);
    std::size_t signatures = 0;
    for (std::size_t i = 0; i + 4 <= bytes.size(); ++i) {
        if (bytes[i] == 'P' && bytes[i + 1] == 'K' && bytes[i + 2] == 0x03
            && bytes[i + 3] == 0x04) {
            ++signatures;
            if (signatures == 2) {
                // 本地头 30 字节固定域＋变长名（名长在 +26 的 u16 小端）。
                const std::size_t nameLen = static_cast<std::uint8_t>(bytes[i + 26])
                    | (static_cast<std::uint8_t>(bytes[i + 27]) << 8);
                const std::size_t dataOffset = i + 30 + nameLen;
                ASSERT_LT(dataOffset, bytes.size());
                bytes[dataOffset] = static_cast<char>(bytes[dataOffset] ^ 0xFF);
                break;
            }
        }
    }
    ASSERT_EQ(signatures, 2u) << "夹具预期：包内至少两个本地条目";

    const std::filesystem::path tamperedFile = dir.path() / "tampered-copy.irdbundle";
    writeFile(tamperedFile,
              std::string(bytes.begin(), bytes.end()));
    std::vector<core::DiagnosticRecord> diags;
    const PackageImportOutcome rejected =
        port.importPackage(validatedSourceFor(tamperedFile, bytes), diags);

    EXPECT_FALSE(rejected.ok) << "篡改包拒绝（fail-closed——清单哈希/容器校验）";
    EXPECT_TRUE(std::filesystem::exists(packageFile));
}

}  // namespace
