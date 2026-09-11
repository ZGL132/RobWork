/**
 * @file   FixtureTest.cpp
 * @brief  夹具与确定性环境用例组——TK-FIX（units/testkit.md §8）。
 *
 * 设计依据：
 *   - units/testkit.md §6.2（TempDir 建立即存在/析构即消失/keepOnFailure/
 *     并行隔离）、§6.3（ReproRecord 默认值与 JSON 往返/DeterministicEnv
 *     唯一来源）、§6.1（GoldenFixture 六步生命周期）；§8 TK-FIX 行
 *   - 任务契约 tasks/foundation/TK-T08.json acceptance 两条：
 *     ①TK-FIX 生命周期六步用例（TempDir/ReproRecord/DeterministicEnv/
 *     GoldenFixture）；②keepOnFailure 现场保留用例
 *
 * 测试环境：GoldenFixture 两用例经 env 覆写 SDURWS_IRD_TESTDATA_DIR 指向
 * 临时 fixture 根（DatasetTest 同款模式——实体数据集不入库，随用例自建）；
 * env 为进程级状态，相关用例天然串行（gtest 同文件默认串行）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/testkit/Dataset.hpp>
#include <sdurws/ird/testkit/Fixture.hpp>
#include <sdurws/ird/testkit/JsonLite.hpp>
#include <sdurws/ird/testkit/ToleranceProfile.hpp>
#include <sdurws/ird/testkit/gtest/GoldenFixture.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _MSC_VER
#include <cstdlib>  // _putenv_s（MSVC CRT 无 unsetenv）
#endif

namespace fs = std::filesystem;
namespace {
using namespace sdurws::ird::testkit;
namespace core = sdurws::ird::core;  // 别名：sha256HexOf 消费 core ContentDigester（R-2 公共头）

constexpr const char* kEnvVar = "SDURWS_IRD_TESTDATA_DIR";

/// SHA-256 十六进制（DatasetTest 同款辅助——与生产装载同源）。
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

/// RAII fixture 根：临时目录＋env 覆写；析构恢复 env 并删除目录（进程级 env
/// 串行纪律——本测试文件内 gtest 用例天然串行）。
class EnvRedirectRoot {
public:
    EnvRedirectRoot()
    {
        const auto now = std::chrono::steady_clock::now().time_since_epoch();
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
        std::random_device rd;
        std::ostringstream name;
        name << "ird-tk-t08-" << ms << "-" << std::hex << rd();
        root_ = fs::temp_directory_path() / name.str();
        fs::create_directories(root_ / "golden");
        previous_ = std::getenv(kEnvVar) != nullptr ? std::getenv(kEnvVar) : "";
#ifdef _MSC_VER
        (void)_putenv_s(kEnvVar, root_.string().c_str());
#else
        setenv(kEnvVar, root_.string().c_str(), 1);
#endif
    }
    ~EnvRedirectRoot()
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

/// 全字段合法 manifest（DatasetTest 同款附录 A.2 形态；integrity 占位由
/// writeDataset 实算替换——保持与 TK-MAN 已钉住的合法形态逐字一致）。
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

/// 在 fixture 根写数据集：文件先落、哈希实算、渲染 manifest（TK-T03 模式）。
void writeDataset(const fs::path& root)
{
    const auto dir = root / "golden" / "kin-fk-planar-2r" / "1.0.0";
    fs::create_directories(dir / "inputs");
    fs::create_directories(dir / "expected");
    const std::string inContent = "{\"q1\":0.0}";
    const std::string outContent = "{\"tcp\":{\"x\":0.5}}";
    std::ofstream(dir / "inputs" / "in.json", std::ios::binary) << inContent;
    std::ofstream(dir / "expected" / "out.json", std::ios::binary) << outContent;
    // manifest 占位符替换：哈希与字节数实算（§4.5 完整性在装载端全量复核）。
    std::string manifest = validManifest();
    const auto swap = [&manifest](const std::string& k, const std::string& v) {
        std::string::size_type pos = 0;
        while ((pos = manifest.find(k, pos)) != std::string::npos) {
            manifest.replace(pos, k.size(), v);
            pos += v.size();
        }
    };
    swap("__IN_SHA__", sha256HexOf(inContent));
    swap("__OUT_SHA__", sha256HexOf(outContent));
    swap("__IN_SIZE__", std::to_string(inContent.size()));
    swap("__OUT_SIZE__", std::to_string(outContent.size()));
    std::ofstream(dir / "manifest.json", std::ios::binary) << manifest;
}

/// 六步用例用：把仓库示例容差档案（TK-T04 交付实体）复制进 fixture 根——
/// 档案装载走与生产一致的 <root>/tolerance/<id>/v<version>.json 路径。
void copyKinFkProfile(const fs::path& root)
{
    const auto src = fs::path{IRD_TESTDATA_ROOT} / "tolerance" / "kin-fk" / "v1.0.0.json";
    const auto dstDir = root / "tolerance" / "kin-fk";
    fs::create_directories(dstDir);
    fs::copy_file(src, dstDir / "v1.0.0.json", fs::copy_options::overwrite_existing);
}

/// 六步用例环境：env 重定向＋fixture 数据资产（golden 数据集＋容差档案）。
/// env 重定向须在 GoldenFixture::SetUp（读 env）之前生效——挂 SetUpTestSuite
/// （gtest 在套件首个用例之前调用一次）；EnvRedirectRoot 为函数级静态，
/// 析构于进程退出时恢复 env。
struct SixStepEnv {
    EnvRedirectRoot root;
    SixStepEnv()
    {
        writeDataset(root.root());       // golden/kin-fk-planar-2r/1.0.0
        copyKinFkProfile(root.root());   // tolerance/kin-fk/v1.0.0.json
    }
};

SixStepEnv& sixStepEnv()
{
    static SixStepEnv env;  // 首个 GoldenFixture 套件进入时建立（构造即重定向）
    return env;
}

/// 六步生命周期演示夹具：要求数据集 kin-fk-planar-2r@1.0.0（ acceptance① 载体）。
class DemoFixture : public GoldenFixture {
protected:
    static void SetUpTestSuite() { sixStepEnv(); }
    DatasetRef datasetRef() const override { return {"kin-fk-planar-2r", "1.0.0"}; }
};

}  // namespace

// ---- TK-FIX：TempDir 建立即存在/析构即消失（§6.2） --------------------

/** 生命周期：构造后目录存在且位于 ird-test 隔离层下；析构后目录消失。 */
TEST(TempDirLife, ExistsOnConstructGoneOnDestruct_UT_FIX)
{
    fs::path p;
    {
        TempDir d{"life"};                    // keepOnFailure 默认 true——但无失败信号
        p = d.path();
        EXPECT_TRUE(fs::exists(p)) << "构造后目录必须立即存在（§6.2）";
        // 目录形态：<系统临时>/ird-test/<pid>-life-<后缀>——父层 ird-test 为隔离层。
        EXPECT_NE(p.string().find("ird-test"), std::string::npos)
            << "目录必须位于 ird-test 隔离层下，实际: " << p.string();
    }
    EXPECT_FALSE(fs::exists(p)) << "析构后目录必须消失（§6.2 RAII）";
}

/** tag 句法护栏：空串与含路径分隔符的 tag 拒绝（Usage——防目录名注入）。 */
TEST(TempDirLife, TagSyntaxRejected_UT_FIX)
{
    EXPECT_THROW(TempDir{""}, TestKitError);
    EXPECT_THROW(TempDir{"a/b"}, TestKitError);
    EXPECT_THROW(TempDir{"白 名"}, TestKitError);
}

/** 并行隔离：多线程并发建立目录——路径两两不同且同时全部存在（§8 TK-FIX 行）。 */
TEST(TempDirLife, ParallelIsolation_UT_FIX)
{
    constexpr int kThreads = 8;
    std::vector<std::unique_ptr<TempDir>> dirs(kThreads);
    std::vector<std::string> errors(kThreads);
    std::vector<std::thread> workers;
    // 各线程独立构造（并行 create_directories 的隔离名互不相撞——pid+随机后缀）。
    for (int i = 0; i < kThreads; ++i) {
        workers.emplace_back([&dirs, &errors, i] {
            try {
                dirs[i] = std::make_unique<TempDir>("par");
            } catch (const std::exception& e) {
                errors[i] = e.what();
            }
        });
    }
    for (auto& w : workers) { w.join(); }
    for (int i = 0; i < kThreads; ++i) {
        ASSERT_NE(dirs[i], nullptr) << "线程 " << i << " 建目录失败: " << errors[i];
        EXPECT_TRUE(fs::exists(dirs[i]->path())) << "并发目录必须同时存在: " << i;
    }
    std::set<std::string> unique;
    for (const auto& d : dirs) { unique.insert(d->path().string()); }
    EXPECT_EQ(unique.size(), static_cast<std::size_t>(kThreads))
        << "并行目录必须两两不同（隔离三要素 pid-tag-随机后缀）";
}

// ---- TK-FIX acceptance②：keepOnFailure 现场保留（§6.2） ----------------

/** 失败信号＋默认开关 → 析构保留现场（保留路径由析构打印至 stderr）。 */
TEST(TempDirKeep, KeepsSceneOnFailureSignal_UT_FIX)
{
    fs::path kept;
    {
        TempDir d{"keep"};
        std::ofstream{d.path() / "evidence.txt"} << "现场证据文件";
        d.noteTestFailure(true);   // GoldenFixture::TearDown 注入 HasFailure() 的同款信号
        kept = d.path();
    }
    EXPECT_TRUE(fs::exists(kept)) << "keepOnFailure 默认 true＋失败信号→现场必须保留";
    std::error_code ec;
    fs::remove_all(kept, ec);      // 用例自清理（保留现场不等于泄漏）
    EXPECT_FALSE(fs::exists(kept));
}

/** 显式关闭开关 → 即使有失败信号也删除（对无排查价值的纯临时目录省磁盘）。 */
TEST(TempDirKeep, DisabledRemovesEvenOnFailure_UT_FIX)
{
    fs::path p;
    {
        TempDir d{"nokeep"};
        d.keepOnFailure(false);
        d.noteTestFailure(true);
        p = d.path();
    }
    EXPECT_FALSE(fs::exists(p)) << "开关关闭→无条件删除";
}

/** 默认态（无失败信号）→ 正常删除（keepOnFailure(true) 不影响通过用例的清理）。 */
TEST(TempDirKeep, NoSignalRemovesByDefault_UT_FIX)
{
    fs::path p;
    {
        TempDir d{"clean"};
        p = d.path();
    }
    EXPECT_FALSE(fs::exists(p)) << "无失败信号→默认删除";
}

/** 删除失败只告警不抛（§6.2 原文）：句柄被占时析构不抛、目录暂存，关句柄后可清理。
 *
 * Windows 语义：被占文件（无 FILE_SHARE_DELETE）使 remove_all 删除失败——
 * 析构若抛出，本用例以未捕获异常终止即判死；"不抛＋目录暂存"即断言目标。
 * （POSIX 下 unlink 对打开文件恒成功，该场景不存在——本项目交付口径为
 * Windows/MSVC，§4.1 工具链约定。） */
TEST(TempDirKeep, DeletionFailureWarnsNotThrows_UT_FIX)
{
    fs::path dirPath;
    std::ofstream holder;  // 作用域跨越 TempDir 析构——保证删除时刻句柄仍被占用
    {
        TempDir d{"locked"};
        dirPath = d.path();
        holder.open(dirPath / "held.txt", std::ios::binary);
        ASSERT_TRUE(holder.is_open()) << "测试前置：占用文件建立失败";
        holder << "held";
        holder.flush();
        d.keepOnFailure(false);  // 关闭保留开关——走纯删除路径
    }  // ← 析构：remove_all 失败（句柄占用）→ 告警不抛（本行不抛即通过）
    EXPECT_TRUE(fs::exists(dirPath))
        << "句柄占用下删除失败→目录暂存（Windows 共享冲突语义）";
    // 收尾：关闭占用句柄后目录可正常清理（不向 %TEMP% 泄漏现场）。
    holder.close();
    std::error_code ec;
    fs::remove_all(dirPath, ec);
    EXPECT_FALSE(fs::exists(dirPath)) << "句柄释放后可清理";
}

// ---- TK-FIX：ReproRecord 默认值与 JSON 往返（§6.3） --------------------

/** 默认值：seed=20260909（固定值）、threadCount=1（§6.3 原文口径）。 */
TEST(ReproRecordData, DefaultsMatchDesign_UT_FIX)
{
    const ReproRecord r;
    EXPECT_EQ(r.seed, 20260909ULL);
    EXPECT_EQ(r.threadCount, 1);
    EXPECT_TRUE(r.softwareVersion.empty());
    EXPECT_TRUE(r.gitCommit.empty());
    EXPECT_TRUE(r.notes.empty());
}

/** JSON 往返：全字段 record → toJson → fromJson 深相等；键序固定（确定性输出）。 */
TEST(ReproRecordData, JsonRoundTrip_UT_FIX)
{
    ReproRecord r;
    r.seed = 42;
    r.threadCount = 4;
    r.softwareVersion = "ird-0.1.0";
    r.gitCommit = "abc1234";
    r.dataset = {"kin-fk-planar-2r", "1.0.0"};
    r.toleranceProfile = "kin-fk@1.0.0";
    r.notes = "往返用例";
    const auto text = r.toJson();
    const auto back = ReproRecord::fromJson(text);
    EXPECT_EQ(back, r) << "往返必须深相等（附录 A.4 跨进程复现载体）";
    // 键序固定（toJson 契约）：seed→threadCount→softwareVersion→gitCommit
    // →dataset→toleranceProfile→notes。
    const auto posOf = [&text](const char* k) { return text.find(k); };
    EXPECT_LT(posOf("\"seed\""), posOf("\"threadCount\""));
    EXPECT_LT(posOf("\"threadCount\""), posOf("\"softwareVersion\""));
    EXPECT_LT(posOf("\"softwareVersion\""), posOf("\"gitCommit\""));
    EXPECT_LT(posOf("\"gitCommit\""), posOf("\"dataset\""));
    EXPECT_LT(posOf("\"dataset\""), posOf("\"toleranceProfile\""));
    EXPECT_LT(posOf("\"toleranceProfile\""), posOf("\"notes\""));
}

/** 拒绝矩阵：非对象/字段缺失/类型不符/越界——消息含 repro-json 前缀（§5）。 */
TEST(ReproRecordData, FromJsonRejectsInvalid_UT_FIX)
{
    const ReproRecord base;
    const auto expectReject = [](const std::string& text, const char* fragment) {
        try {
            (void)ReproRecord::fromJson(text);
            FAIL() << "必须拒绝: " << text;
        } catch (const TestKitError& e) {
            EXPECT_NE(std::string{e.what()}.find("repro-json"), std::string::npos)
                << "消息必须含 repro-json 前缀: " << e.what();
            EXPECT_NE(std::string{e.what()}.find(fragment), std::string::npos)
                << "消息必须含字段定位: " << e.what();
        }
    };
    expectReject("[]", "顶层");                                    // 非对象
    expectReject(R"({"threadCount":1})", "seed");                  // 缺 seed
    expectReject(R"({"seed":1})", "threadCount");                  // 缺 threadCount
    expectReject(R"({"seed":1,"threadCount":0})", "threadCount");  // 越界 <1
    expectReject(R"({"seed":1.5,"threadCount":1})", "seed");       // 非整数 seed
    expectReject(R"({"seed":1,"threadCount":-2})", "threadCount"); // 负线程数
    expectReject(R"({"seed":"x","threadCount":1})", "seed");       // 类型不符
    // dataset 子字段缺失（嵌套定位）——顶层其余字段齐全，缺失恰好落在嵌套层。
    expectReject(R"({"seed":1,"threadCount":1,"softwareVersion":"","gitCommit":"",)"
                 R"("toleranceProfile":"","notes":"","dataset":{"datasetId":"a"}})",
                 "version");
    // 合法基线对照：完整对象必须通过（防拒绝矩阵误伤合法形态）。
    EXPECT_NO_THROW((void)ReproRecord::fromJson(base.toJson()));
}

/** DeterministicEnv：记录＝确定性上下文唯一来源——外部副本改不动已建环境。 */
TEST(DeterministicEnvData, RecordIsSoleSource_UT_FIX)
{
    ReproRecord r;
    r.seed = 7;
    DeterministicEnv env{r};
    r.seed = 999;  // 外部后续修改不得影响环境（按值持有语义）
    EXPECT_EQ(env.record().seed, 7ULL);
    // 同记录两次建立 → 记录相等（重放语义的前提：同记录可复现）。
    const DeterministicEnv again{env.record()};
    EXPECT_EQ(again.record(), env.record());
}

// ---- TK-FIX acceptance①：GoldenFixture 生命周期六步（§6.1） ------------

/** 生命周期六步（acceptance①）：①~⑤ 产物就绪且复现记录登记完整。 */
TEST_F(DemoFixture, SixStepLifecycle_UT_FIX)
{
    // 步骤②产物：数据集已装载且定位正确。
    ASSERT_TRUE(dataset.has_value()) << "步骤②：数据集必须就绪";
    EXPECT_EQ(dataset->manifest().datasetId, "kin-fk-planar-2r");
    // 步骤③产物：容差档案就绪且可解析数据集声明的字段路径。
    ASSERT_TRUE(profile.has_value()) << "步骤③：容差档案必须就绪";
    EXPECT_NO_THROW((void)profile->resolve("fk[*].tcp.position.x"));
    // 步骤④产物：确定性环境——种子默认 20260909，记录登记数据资产。
    ASSERT_TRUE(env.has_value()) << "步骤④：确定性环境必须就绪";
    EXPECT_EQ(env->record().seed, 20260909ULL);
    EXPECT_EQ(env->record().threadCount, 1);
    EXPECT_EQ(env->record().dataset.datasetId, "kin-fk-planar-2r");
    EXPECT_EQ(env->record().dataset.version, "1.0.0");
    EXPECT_EQ(env->record().toleranceProfile, "kin-fk@1.0.0");
    EXPECT_EQ(repro, env->record()) << "repro 成员与 env 记录必须同源";
    // 步骤⑤产物：TempDir 就绪且位于 ird-test 隔离层。
    ASSERT_NE(workDir, nullptr) << "步骤⑤：TempDir 必须就绪";
    EXPECT_TRUE(fs::exists(workDir->path()));
    EXPECT_NE(workDir->path().string().find("ird-test"), std::string::npos);
}

/** 纯算法形态：datasetRef 空 → 步骤②③整体跳过（产物空态），④⑤照常。 */
class PureFixture : public GoldenFixture {
};

TEST_F(PureFixture, SkipsAssetStepsWhenNoDataset_UT_FIX)
{
    EXPECT_FALSE(dataset.has_value()) << "未要求数据集→步骤②跳过";
    EXPECT_FALSE(profile.has_value()) << "未要求数据集→步骤③跳过";
    ASSERT_TRUE(env.has_value()) << "步骤④不依赖数据资产，必须就绪";
    EXPECT_EQ(env->record().seed, 20260909ULL);
    EXPECT_TRUE(env->record().dataset.datasetId.empty());
    EXPECT_TRUE(env->record().toleranceProfile.empty());
    ASSERT_NE(workDir, nullptr) << "步骤⑤不依赖数据资产，必须就绪";
}
