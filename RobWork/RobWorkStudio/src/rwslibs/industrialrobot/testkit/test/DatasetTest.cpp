/**
 * @file   DatasetTest.cpp
 * @brief  黄金数据集清单与完整性用例组——TK-MAN/TK-INT（units/testkit.md §8）。
 *
 * 设计依据：
 *   - units/testkit.md §4.2.2（manifest 字段表）/§4.5（size＋SHA-256）/§8
 *     TK-MAN·TK-INT 行/附录 A.2（全字段示例）
 *   - 需求 NFR-COR-01；任务契约 tasks/foundation/TK-T03.json acceptance 四条
 *
 * 测试环境：env 覆写 SDURWS_IRD_TESTDATA_DIR 指向临时 fixture 根（TestPaths env
 * 态）；每用例自建 fixture 数据集（manifest＋文件＋integrity 实算），不依赖仓库
 * testdata 实体（实体随首个业务数据集任务建立）。串行纪律：env 为进程级状态。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/testkit/Dataset.hpp>
#include <sdurws/ird/testkit/JsonLite.hpp>

#include <chrono>
#include <fstream>
#include <random>
#include <sstream>
#include <string>

namespace fs = std::filesystem;
namespace {
using namespace sdurws::ird::testkit;
namespace core = sdurws::ird::core;   // 别名：sha256HexOf 消费 core ContentDigester

constexpr const char* kEnvVar = "SDURWS_IRD_TESTDATA_DIR";

/// SHA-256 十六进制（与生产装载同源——core ContentDigester）。
std::string sha256HexOf(const std::string& bytes)
{
    core::ContentDigester d;
    d.update(bytes.data(), bytes.size());
    const auto digest = d.finalize();
    static constexpr char kHex[] = "0123456789abcdef";
    std::string s;
    for (const auto b : digest) {
        s.push_back(kHex[b >> 4]);
        s.push_back(kHex[b & 0x0F]);
    }
    return s;
}

/// RAII fixture 根：临时目录＋env 覆写；析构恢复 env 并删除目录。
class FixtureRoot {
public:
    FixtureRoot()
    {
        const auto now = std::chrono::steady_clock::now().time_since_epoch();
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
        std::random_device rd;
        std::ostringstream name;
        name << "ird-tk-t03-" << ms << "-" << std::hex << rd();
        root_ = fs::temp_directory_path() / name.str();
        fs::create_directories(root_ / "golden");
        previous_ = std::getenv(kEnvVar) != nullptr ? std::getenv(kEnvVar) : "";
#ifdef _MSC_VER
        (void)_putenv_s(kEnvVar, root_.string().c_str());
#else
        setenv(kEnvVar, root_.string().c_str(), 1);
#endif
    }
    ~FixtureRoot()
    {
#ifdef _MSC_VER
        (void)_putenv_s(kEnvVar, previous_.c_str());
#else
        setenv(kEnvVar, previous_.c_str(), 1);
#endif
        std::error_code ec;
        fs::remove_all(root_, ec);
    }
    const fs::path& root() const noexcept { return root_; }

private:
    fs::path root_;
    std::string previous_;
};

/// 全字段合法 manifest（附录 A.2 形态；完整性占位由 writeDataset 实算替换）。
std::string validManifest()
{
    return R"({
  "schemaVersion": "ird-golden-manifest/1",
  "datasetId": "kin-fk-planar-2r",
  "version": "1.0.0",
  "kind": "analytic-case",
  "scenarioCategory": "kin/fk-forward",
  "coveredRequirements": ["KIN-01", "ARC-03"],
  "coveredAt": ["AT-16"],
  "toleranceProfile": { "id": "kin-fk", "version": "1.0.0" },
  "inputs":  ["inputs/in.json"],
  "expected": ["expected/out.json"],
  "parameters": { "link1": 0.5, "link2": 0.4, "q2-samples": 9 },
  "units":  { "position": "m", "angle": "rad" },
  "frames": { "base": "link0" },
  "referenceSource": {
    "method": "closed-form",
    "scope": "平面 2R 正运动学闭式解",
    "independentOfProductionImpl": true,
    "description": "推导见 generate/derivation.md"
  },
  "producer": {
    "softwareVersion": "python-3.11",
    "algorithmId": "fk-planar-2r-closed-form",
    "solverConfig": {},
    "seed": null, "threadCount": 1,
    "generatedAtUtc": "2026-09-11T00:00:00Z"
  },
  "edgeCases": {
    "zeroValue": true, "nearZero": true, "signCancellation": true,
    "sampleRefs": ["expected/out.json#zero"]
  },
  "integrity": [
    { "path": "inputs/in.json", "sha256": "__IN_SHA__", "sizeBytes": __IN_SIZE__ },
    { "path": "expected/out.json", "sha256": "__OUT_SHA__", "sizeBytes": __OUT_SIZE__ }
  ],
  "generator": { "script": "generate/make_fk.py", "invocation": "python make_fk.py", "committed": true },
  "history": [ { "version": "1.0.0", "date": "2026-09-11", "change": "首版",
                 "reReviewedBy": "WP-02", "supersededBy": "" } ]
})";
}

/// 在 fixture 根写数据集：文件先落、哈希实算、渲染 manifest。
/// manifestText 可为变异版本（反例构造）。
void writeDataset(const fs::path& root, const std::string& id,
                  const std::string& version, std::string manifest)
{
    const auto dir = root / "golden" / id / version;
    fs::create_directories(dir / "inputs");
    fs::create_directories(dir / "expected");
    const std::string inContent = "{\"q1\":0.0}";
    const std::string outContent = "{\"tcp\":{\"x\":0.5}}";
    std::ofstream(dir / "inputs" / "in.json", std::ios::binary) << inContent;
    std::ofstream(dir / "expected" / "out.json", std::ios::binary) << outContent;
    const auto replaceAll = [&manifest](const std::string& k, const std::string& v) {
        std::string::size_type pos = 0;
        while ((pos = manifest.find(k, pos)) != std::string::npos) {
            manifest.replace(pos, k.size(), v);
            pos += v.size();
        }
    };
    replaceAll("__IN_SHA__", sha256HexOf(inContent));
    replaceAll("__OUT_SHA__", sha256HexOf(outContent));
    replaceAll("__IN_SIZE__", std::to_string(inContent.size()));
    replaceAll("__OUT_SIZE__", std::to_string(outContent.size()));
    std::ofstream(dir / "manifest.json", std::ios::binary) << manifest;
}

/// 装载失败断言：必须抛 TestKitError 且消息含字段路径片段。
void expectReject(const std::string& id, const std::string& version,
                  const std::string& pathFragment)
{
    try {
        (void)GoldenDataset::load({id, version});
        FAIL() << "必须拒绝: " << id << "/" << version;
    } catch (const TestKitError& e) {
        EXPECT_NE(std::string(e.what()).find(pathFragment), std::string::npos)
            << "字段路径缺失（期望含 " << pathFragment << "）: " << e.what();
    }
}

/** TK-MAN：附录 A.2 形态全字段清单装载通过（acceptance 1）。 */
TEST(DatasetManifest, FullFieldLoad_UT_MAN)
{
    FixtureRoot root;
    writeDataset(root.root(), "kin-fk-planar-2r", "1.0.0", validManifest());
    const auto ds = GoldenDataset::load({"kin-fk-planar-2r", "1.0.0"});
    EXPECT_EQ(ds.manifest().datasetId, "kin-fk-planar-2r");
    EXPECT_EQ(ds.manifest().kind, DatasetKind::AnalyticCase);
    EXPECT_EQ(ds.manifest().coveredRequirements.size(), 2u);
    EXPECT_EQ(ds.manifest().toleranceProfileId, "kin-fk");
    // resolve 走清单白名单（inputs/expected 各自登记才可解析）。
    EXPECT_NO_THROW((void)ds.resolveInput("inputs/in.json"));
    EXPECT_NO_THROW((void)ds.resolveExpected("expected/out.json"));
}

/** 反例矩阵（acceptance 2）：字段路径逐项命中。
 *  各变体写入独立目录且 manifest.datasetId 同步为变体 id——避免"目录一致性"
 *  检查先于目标校验触发（装载器校验顺序：schema→datasetId 一致→kind→…）。 */
TEST(DatasetRejections, RejectionMatrixWithFieldPaths_UT_MAN)
{
    FixtureRoot root;
    const auto& r = root.root();

    // 变体写入 helper：manifest.datasetId 同步为变体 id（目录一致检查通过，
    // 使目标校验真正可达）。
    auto writeVariant = [&](const std::string& id, std::string manifest) {
        // datasetId 已被变体改写（如句法反例）时跳过同步——find 失败容错。
        const auto pos = manifest.find("kin-fk-planar-2r");
        if (pos != std::string::npos) {
            manifest.replace(pos, 16, id);
        }
        writeDataset(r, id, "1.0.0", manifest);
    };

    // schemaVersion 未知主版本（ird-golden-manifest/2）。
    std::string badSchema = validManifest();
    badSchema.replace(badSchema.find("ird-golden-manifest/1"), 20, "ird-golden-manifest/2");
    writeVariant("rej-schema", badSchema);
    expectReject("rej-schema", "1.0.0", "schemaVersion");

    // 字段越界：datasetId 句法非法（含大写）——句法校验先于目录一致检查。
    std::string badId = validManifest();
    {
        const auto pos = badId.find("\"kin-fk-planar-2r\"");
        badId.replace(pos, 18, "\"Kin-FK\"");
    }
    writeVariant("rej-id", badId);
    expectReject("rej-id", "1.0.0", "datasetId");

    // 单位未注册（units.position = "meter"——core 单位表无此 token）。
    std::string badUnit = validManifest();
    {
        const auto pos = badUnit.find("\"position\": \"m\"");
        badUnit.replace(pos, 15, "\"position\": \"meter\"");
    }
    writeVariant("rej-unit", badUnit);
    expectReject("rej-unit", "1.0.0", "units.position");

    // analytic-case 缺 edgeCases（附录 D C4 lint 强制）。
    std::string noEdge = validManifest();
    {
        const auto start = noEdge.find("\"edgeCases\"");
        const auto end = noEdge.find("},", start);
        noEdge.erase(start, end - start + 2);
    }
    writeVariant("rej-edge", noEdge);
    expectReject("rej-edge", "1.0.0", "edgeCases");

    // integrity 未覆盖 expected 文件。
    std::string noOut = validManifest();
    {
        const auto start = noOut.find("{ \"path\": \"expected/out.json\"");
        const auto end = noOut.find("},", start);
        noOut.erase(start, end - start + 2);
    }
    writeVariant("rej-integrity", noOut);
    expectReject("rej-integrity", "1.0.0", "integrity");
}

/** TK-INT：篡改一字节→datasetInvalid；size 不符同理（acceptance 3）。 */
TEST(DatasetIntegrity, TamperAndSizeMismatch_UT_INT)
{
    FixtureRoot root;
    writeDataset(root.root(), "kin-fk-planar-2r", "1.0.0", validManifest());

    // 篡改一字节（内容与登记哈希不符）。
    const auto inFile = root.root() / "golden" / "kin-fk-planar-2r" / "1.0.0"
                      / "inputs" / "in.json";
    {
        std::ofstream out(inFile, std::ios::binary);
        out << "{\"q1\":0.1}";   // 原为 0.0——一字节之差
    }
    expectReject("kin-fk-planar-2r", "1.0.0", "SHA-256");

    // size 不符（追加字节后哈希与 size 双不符——先暴露 size 检查亦被钉住）。
    writeDataset(root.root(), "kin-fk-planar-2r", "1.0.0", validManifest());
    {
        std::ofstream out(inFile, std::ios::binary | std::ios::app);
        out << " ";
    }
    bool sawSize = false;
    try {
        (void)GoldenDataset::load({"kin-fk-planar-2r", "1.0.0"});
        FAIL() << "必须拒绝";
    } catch (const TestKitError& e) {
        sawSize = std::string(e.what()).find("size 不符") != std::string::npos;
    }
    EXPECT_TRUE(sawSize) << "size 检查应先于 SHA 报告";
}

/** datasetId 与目录名不一致拒绝（lint 口径的装载侧）。 */
TEST(DatasetLint, DatasetIdDirMismatch_UT_MAN)
{
    FixtureRoot root;
    writeDataset(root.root(), "kin-fk-planar-2r", "1.0.0", validManifest());
    // 把同一 manifest 复制到 kin-fk 目录（manifest.datasetId=kin-fk-planar-2r ≠ 目录
    // kin-fk）——装载时"目录一致"检查即触发（先于其他校验，§4.2.2 datasetId 行）。
    const auto src = root.root() / "golden" / "kin-fk-planar-2r" / "1.0.0" / "manifest.json";
    const auto dstDir = root.root() / "golden" / "kin-fk" / "1.0.0";
    fs::create_directories(dstDir);
    fs::copy_file(src, dstDir / "manifest.json", fs::copy_options::overwrite_existing);
    expectReject("kin-fk", "1.0.0", "datasetId");
}
}  // namespace
