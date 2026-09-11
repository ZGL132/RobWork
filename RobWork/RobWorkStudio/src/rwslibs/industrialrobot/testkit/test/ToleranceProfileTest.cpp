/**
 * @file   ToleranceProfileTest.cpp
 * @brief  容差档案用例组——TK-TOL（units/testkit.md §8）：resolve 命中/未命中/
 *         超限三态＋C7 对照＋示例档案装载（TK-T04）。
 *
 * 设计依据：
 *   - units/testkit.md §4.3.1/§4.3.2/§4.3.3（字段表/allowedMax 规则/比较语义）、
 *     §8 TK-TOL 行、§9 TK-T04 行（示例档案 testdata/tolerance/kin-fk/）
 *   - 需求 NFR-COR-01；任务契约 tasks/foundation/TK-T04.json acceptance 三条
 */

#include <gtest/gtest.h>

#include <sdurws/ird/core/Compare.hpp>
#include <sdurws/ird/core/Errors.hpp>
#include <sdurws/ird/testkit/ToleranceProfile.hpp>

#include <filesystem>
#include <fstream>
#include <random>
#include <chrono>
#include <string>

namespace fs = std::filesystem;
namespace {
using namespace sdurws::ird::testkit;
namespace core = sdurws::ird::core;   // 别名：C7 对照消费 core runtimeAbsoluteTolerance

/// RAII 临时目录（反例档案用——与 DatasetTest 的 FixtureRoot 同型但独立最小实现）。
class TempDir {
public:
    TempDir()
    {
        const auto now = std::chrono::steady_clock::now().time_since_epoch();
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
        std::random_device rd;
        std::ostringstream name;
        name << "ird-tk-t04-" << ms << "-" << std::hex << rd();
        path_ = fs::temp_directory_path() / name.str();
        fs::create_directories(path_);
    }
    ~TempDir()
    {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }
    const fs::path& path() const noexcept { return path_; }

private:
    fs::path path_;
};

/// 仓库内示例档案路径（testdata/tolerance/kin-fk/v1.0.0.json）。
fs::path sampleProfilePath()
{
    // testdata 与 testkit 平铺（testkit/../testdata）——由 CMake 注入。
    return fs::path{IRD_TESTDATA_ROOT} / "tolerance" / "kin-fk" / "v1.0.0.json";
}

/** 示例档案（附录 D 第 4/9 项条目）装载通过＋resolve 三态（acceptance 1/2）。 */
TEST(ToleranceProfileLoad, SampleProfileResolveThreeStates_UT_TOL)
{
    const auto profile = ToleranceProfile::load(sampleProfilePath());
    EXPECT_EQ(profile.profileId, "kin-fk");
    EXPECT_EQ(profile.version, "1.0.0");

    // 命中：'*' 模板展开索引（fk[*] ↔ fk[3]）。
    const auto& hit = profile.resolve("fk[3].tcp.position.x");
    EXPECT_EQ(hit.source, ToleranceSource::AppendixDFixed);
    // 另一索引同模板命中。
    EXPECT_TRUE(profile.resolve("fk[7].tcp.position.y").source
                == ToleranceSource::AppendixDFixed);

    // 未命中：tolerance-undefined（不默认、不通过——C4）。
    EXPECT_THROW(profile.resolve("fk[3].tcp.position.zeta"),
                 TestKitError);
    try {
        (void)profile.resolve("不存在的路径");
        FAIL() << "必须抛出";
    } catch (const TestKitError& e) {
        EXPECT_EQ(std::string(e.what()).find("tolerance-undefined: "), 0u);
    }
}

/** 超限态：装载期拒绝 tolerance > allowedMax（§4.3.2"档案非法"）。 */
TEST(ToleranceProfileLoad, DeclaredExceedingAllowedMaxRejected_UT_TOL)
{
    // fixture：合法档案＋一条 dataset-declared 超限条目（5e-12 > allowedMax 1e-12）。
    TempDir root;
    const auto dir = root.path() / "tolerance" / "kin-fk";
    fs::create_directories(dir);
    std::ofstream(dir / "v1.0.0.json", std::ios::binary) << R"({
  "schemaVersion": "ird-tolerance-profile/1",
  "profileId": "kin-fk",
  "version": "1.0.0",
  "basis": "test",
  "entries": [
    { "fieldPath": "fk[*].tcp.position.x", "quantityKind": "length", "unit": "m",
      "tolerance": { "relative": 0.0, "absolute": 5e-12 },
      "source": "dataset-declared",
      "allowedMax": { "relative": 0.0, "absolute": 1e-12 } }
  ]
})";
    EXPECT_THROW(ToleranceProfile::load(dir / "v1.0.0.json"), TestKitError);
    // 消息含字段路径（entries[0].tolerance）——失败时输出实际消息以便定位。
    try {
        (void)ToleranceProfile::load(dir / "v1.0.0.json");
        FAIL() << "必须抛出";
    } catch (const TestKitError& e) {
        EXPECT_NE(std::string(e.what()).find("entries[0].tolerance"), std::string::npos)
            << "实际消息: " << e.what();
    }
}

/** allowedMax 条件规则：appendixD-fixed 缺 allowedMax → 装载失败。 */
TEST(ToleranceProfileLoad, FixedRequiresAllowedMax_UT_TOL)
{
    TempDir root;
    const auto dir = root.path() / "tolerance" / "kin-fk";
    fs::create_directories(dir);
    std::ofstream(dir / "v1.0.0.json", std::ios::binary) << R"({
  "schemaVersion": "ird-tolerance-profile/1",
  "profileId": "kin-fk",
  "version": "1.0.0",
  "basis": "test",
  "entries": [
    { "fieldPath": "fk[*].tcp.position.x", "quantityKind": "length", "unit": "m",
      "tolerance": { "relative": 0.0, "absolute": 1e-12 },
      "source": "appendixD-fixed" }
  ]
})";
    EXPECT_THROW(ToleranceProfile::load(dir / "v1.0.0.json"), TestKitError);
}

/** C7 对照：core runtimeAbsoluteTolerance（CORE-T05 转写）与档案条目一致——
 *  两套数值同源（core Compare/Units 唯一实现点）。 */
TEST(ToleranceProfileC7, CrossCheckWithCore_UT_TOL)
{
    const auto coreLen = core::runtimeAbsoluteTolerance(core::QuantityKind::Length);
    ASSERT_TRUE(coreLen.has_value());
    const auto profile = ToleranceProfile::load(sampleProfilePath());
    // 档案中 appendixD-fixed 的 length 条目 ε_abs 应等于 core C7 转写值（1e-12）。
    const auto& hit = profile.resolve("fk[3].tcp.position.x");
    ASSERT_TRUE(hit.allowedMax.has_value());
    EXPECT_DOUBLE_EQ(hit.allowedMax->absolute, *coreLen);
}

/** source 白名单与 kind/unit 量纲一致性反例。 */
TEST(ToleranceProfileLoad, SourceWhitelistAndKindMismatch_UT_TOL)
{
    TempDir root;
    const auto dir = root.path() / "tolerance" / "kin-fk";
    fs::create_directories(dir);
    // 反例①：source 未知。
    std::ofstream(dir / "v1.0.0.json", std::ios::binary) << R"({
  "schemaVersion": "ird-tolerance-profile/1",
  "profileId": "kin-fk", "version": "1.0.0", "basis": "t",
  "entries": [ { "fieldPath": "a", "quantityKind": "length", "unit": "m",
                 "tolerance": {"relative":0,"absolute":1e-3}, "source": "made-up" } ]
})";
    EXPECT_THROW(ToleranceProfile::load(dir / "v1.0.0.json"), TestKitError);

    // 反例②：quantityKind=length 但 unit=N（量纲不符）。
    std::ofstream(dir / "v1.0.0.json", std::ios::binary) << R"({
  "schemaVersion": "ird-tolerance-profile/1",
  "profileId": "kin-fk", "version": "1.0.0", "basis": "t",
  "entries": [ { "fieldPath": "a", "quantityKind": "length", "unit": "N",
                 "tolerance": {"relative":0,"absolute":1e-3}, "source": "dataset-declared",
                 "allowedMax": {"relative":0,"absolute":1.0} } ]
})";
    EXPECT_THROW(ToleranceProfile::load(dir / "v1.0.0.json"), TestKitError);

    // 文件名与 version 不一致（v2.0.0.json 但 version=1.0.0）。
    std::ofstream(dir / "v2.0.0.json", std::ios::binary) << R"({
  "schemaVersion": "ird-tolerance-profile/1",
  "profileId": "kin-fk", "version": "1.0.0", "basis": "t",
  "entries": [ { "fieldPath": "a", "quantityKind": "length", "unit": "m",
                 "tolerance": {"relative":0,"absolute":1e-3}, "source": "dataset-declared" } ]
})";
    EXPECT_THROW(ToleranceProfile::load(dir / "v2.0.0.json"), TestKitError);
}
}  // namespace
