/**
 * @file   TestPathsTest.cpp
 * @brief  数据根三态解析用例组——TK-T01 验收标准"数据根解析（env/默认/
 *         缺失三态）用例通过"的载体。
 *
 * 设计依据：
 *   - units/testkit.md §4.6（三态语义：env 优先→回落编译默认→两者皆无则
 *     env-unavailable）、§5（TestKitError 语义）
 *   - 任务契约 tasks/foundation/TK-T01.json acceptance 第 1 条
 *   - 需求 NFR-COR-01（黄金数据对照的数据载体定位入口）
 *
 * 背景说明：编译默认目录的存在性无法在测试运行期改写（宏注入于编译期），
 * 因此三态用例分两层：
 *   ① detail::resolveDataRoot 纯函数层——用测试自建的临时目录/不存在路径
 *      构造三态全矩阵，不依赖仓库现状（testdata/ 实体目录随 TK-T03 建立，
 *      届时本组用例无需修改）；
 *   ② 公共 API goldenDataRoot 集成层——真实读改进程环境变量，验证 env 态
 *      与"未设 env"行为；默认回落断言按编译默认的当前存在性双分支自适配
 *      （两个分支都是强断言，实现损坏时任一世界必红——防恒真）。
 *
 * 测试串行性：环境变量是进程级全局状态，本文件全部用例依赖并修改同一
 * 变量，gtest 默认串行执行即满足；不得将本套件并入并行分片（sharding）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/testkit/TestPaths.hpp>

#include <chrono>      // 随机目录名的时间熵源
#include <cstdio>      // std::fprintf：清理失败告警输出
#include <cstdlib>     // _putenv_s（MSVC）/setenv·unsetenv（POSIX）
#include <filesystem>  // 临时目录建立/递归删除/存在性断言
#include <random>      // random_device：目录随机后缀（防并行/重跑碰撞）
#include <sstream>     // 十六进制后缀格式化
#include <string>

namespace fs = std::filesystem;

// 被测符号位于 sdurws::ird::testkit；测试体以非限定名书写（与产品代码
// 调用形态一致），此 using 打开命名空间。测试文件是唯一允许此写法的
// 场景（产品代码禁止 using namespace，AGENTS.md 风格约束的测试侧豁免）。
using namespace sdurws::ird::testkit;

namespace {

/// 环境变量名（与 TestPaths.cpp 内 kTestDataEnvVar 同名同义；测试无法
/// include 实现文件的内部常量〔匿名命名空间〕，此处以字面量对齐，名字
/// 变更会由用例失败暴露——两处必须同步修改）。
constexpr const char* kEnvVar = "SDURWS_IRD_TESTDATA_DIR";

/**
 * @brief 设置（或清除）进程环境变量；空值＝清除语义归一点。
 *
 * MSVC CRT 提供 _putenv_s 而无 unsetenv：设为空串后 getenv 返回空串而非
 * nullptr，实现侧已把"空串"归一为"未设置"（TestPaths.cpp 规则 1），因此
 * 本helper用空串表达清除在两平台语义一致。
 *
 * @param name  [in] 变量名
 * @param value [in] 值；空串表示清除（视为未设置）
 */
void setEnvVar(const char* name, const char* value)
{
#ifdef _MSC_VER
    ASSERT_EQ(0, _putenv_s(name, value)) << "设置环境变量失败: " << name;
#else
    if (*value == '\0') {
        unsetenv(name);
    } else {
        ASSERT_EQ(0, setenv(name, value, /*overwrite=*/1))
            << "设置环境变量失败: " << name;
    }
#endif
}

/**
 * @brief RAII 临时目录：系统临时目录下建立唯一子目录，析构递归删除。
 *
 * TK-T01 阶段 testkit 尚无 TempDir 夹具（随 TK-T08 落地），本类是测试内
 * 最小替代：目录名＝ird-tk-t01-<时间熵>-<随机熵>，防重跑/并行碰撞；
 * 删除失败仅告警不抛（清理失败不应放大为测试错误）。
 *
 * 线程安全：单线程构造析构（本套件串行）。
 */
class ScopedTempDir {
public:
    ScopedTempDir()
    {
        // 双熵源：毫秒时间＋random_device（碰撞概率可忽略；目录名不进入
        // 断言，随机性不影响测试确定性判据）。
        const auto now = std::chrono::steady_clock::now().time_since_epoch();
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
        std::random_device rd;
        std::ostringstream name;
        name << "ird-tk-t01-" << ms << "-" << std::hex << rd();
        auto base = fs::temp_directory_path() / name.str();
        fs::create_directories(base);  // 系统临时目录下建目录，失败即抛（测试环境自身损坏）
        path_ = base;
    }
    ~ScopedTempDir()
    {
        std::error_code ec;
        fs::remove_all(path_, ec);  // 清理失败不抛：保留现场反而利于排查
        if (ec) {
            std::fprintf(stderr, "ScopedTempDir 清理失败（保留现场）: %s (%s)\n",
                         path_.string().c_str(), ec.message().c_str());
        }
    }
    const fs::path& path() const noexcept { return path_; }

private:
    fs::path path_;  ///< 建立的目录（构造成功后有效）
};

}  // namespace

// =====================================================================
// 用例组命名：TestPaths.<行为>_<需求编号>——DTB §5.5 要求测试命名带需求/
// AT 追溯字段；ird-test-report.json 按用例名承载该追溯。
// =====================================================================

/**
 * @brief 三态之一（env 态）：环境变量非空时覆盖一切默认（公共 API 集成）。
 *
 * 追踪：NFR-COR-01（数据载体定位）、testkit.md §4.6 规则 1。
 *
 * 断言：返回值与环境变量值逐字符一致（不归一化——env 态承诺原样返回，
 * 保证 CI 日志中的路径与调用方设置的值可精确比对）；环境变量指向的
 * 目录此时刻意不存在，验证"env 态不校验存在性"（目录缺失由后续装载
 * 阶段暴露，本层不得越权拒绝）。
 */
TEST(TestPaths, EnvVarOverridesDefault_NFR_COR_01)
{
    // 指向刻意不存在的路径：若实现越权校验 env 目录存在性，本用例变红
    const std::string envValue =
        (fs::temp_directory_path() / "ird-tk-t01-env-probe-absent").string();
    setEnvVar(kEnvVar, envValue.c_str());

    EXPECT_EQ(goldenDataRoot().string(), envValue);
}

/**
 * @brief 三态之二（默认回落态）：未设 env 且编译默认存在→返回编译默认。
 *
 * 追踪：NFR-COR-01、testkit.md §4.6 规则 2 前半。
 *
 * 经纯函数接缝构造"存在的默认目录"（测试自建临时目录）：编译默认的
 * 真实存在性无法在运行期控制，此态由 detail 层钉住；公共 API 侧的默认
 * 回落见 DefaultFallback_Adaptive_NFR_COR_01。
 */
TEST(TestPaths, DefaultFallbackWhenRootExists_NFR_COR_01)
{
    ScopedTempDir existing;  // 自建真实存在的目录充当 compiledDefault
    const auto resolved = detail::resolveDataRoot(nullptr, existing.path());
    EXPECT_EQ(resolved, existing.path());  // 存在→原样返回，不抛
}

/**
 * @brief 三态之三（缺失态）：未设 env 且默认目录不存在→env-unavailable。
 *
 * 追溯：NFR-COR-01、testkit.md §4.6 规则 2 后半、§7.2（EnvUnavailable
 * 属"环境不可用"分类，不得计为测试失败）。
 *
 * 双层验证：纯函数层（构造不存在的默认路径）＋公共 API 层（清空 env 后
 * 的真实行为）。公共 API 层断言编译默认的真实路径形态（以 ../testdata
 * 结尾——testkit/CMakeLists.txt 注入的仓库内默认），钉住注入链未被篡改。
 */
TEST(TestPaths, EnvUnsetAndDefaultMissing_Throws_NFR_COR_01)
{
    // ① 纯函数层：不存在的默认路径（双保险：拼一个必然不存在的深路径）
    const fs::path absent = fs::temp_directory_path() / "ird-tk-t01"
        / "absent-root-probe" / "no-such-dir";
    ASSERT_FALSE(fs::exists(absent));  // 锚定：构造的缺失前提必须真的缺失
    EXPECT_THROW(detail::resolveDataRoot(nullptr, absent), TestKitError);

    // 空串 env 等价未设（语义归一点，TestPaths.cpp 规则 1）
    EXPECT_THROW(detail::resolveDataRoot("", absent), TestKitError);

    // ② 公共 API 层：清空 env 后，行为由编译默认的存在性决定
    setEnvVar(kEnvVar, "");
    const bool defaultExists = fs::is_directory(fs::path{IRD_TESTDATA_DEFAULT_DIR});
    if (defaultExists) {
        // testdata/ 实体已建立（TK-T03 之后的世界）：回落默认成功
        EXPECT_EQ(goldenDataRoot(), fs::path{IRD_TESTDATA_DEFAULT_DIR});
    } else {
        // 当前仓库现状（TK-T01 时点 testdata/ 实体未建）：必须抛 env-unavailable
        // 且分类正确（而非笼统 runtime_error）——夹具据此分流为"环境不可用"
        try {
            (void)goldenDataRoot();
            FAIL() << "编译默认不存在且未设 env，goldenDataRoot 不得返回";
        } catch (const TestKitError& e) {
            EXPECT_EQ(e.kind(), TestKitErrorKind::EnvUnavailable);
            EXPECT_STREQ(TestKitError::toToken(e.kind()), "env-unavailable");
        }
    }
}

/**
 * @brief env 空串＝未设置（公共 API 集成）：与 nullptr 同语义的归一验证。
 *
 * 追踪：NFR-COR-01、TestPaths.cpp 规则 1 的空串归一。
 *
 * 场景：setEnvVar(kEnvVar, "")（MSVC 下的"清除"手段）后，公共 API 的
 * 行为必须与"变量从未设置"一致——本用例以纯函数在两种空输入下的结果
 * 相同来钉住归一语义（编译默认存在时两者同返回默认，不存在时两者同抛）。
 */
TEST(TestPaths, EmptyEnvTreatedAsUnset_NFR_COR_01)
{
    ScopedTempDir existing;
    const auto viaNull = detail::resolveDataRoot(nullptr, existing.path());
    const auto viaEmpty = detail::resolveDataRoot("", existing.path());
    EXPECT_EQ(viaNull, viaEmpty);
}

/**
 * @brief 默认回落（公共 API 自适配）：未设 env 时按编译默认存在性落位。
 *
 * 追溯：NFR-COR-01、testkit.md §4.6。
 *
 * 双分支均为强断言（防恒真）：默认存在→返回编译默认；不存在→env-
 * unavailable。若实现损坏（恒返回默认/恒抛错），任一世界下本用例必红。
 * 同时核对返回值以 testdata 结尾，钉住 CMake 注入的是任务卡规定的
 * industrialrobot/testdata 默认（§3.6），防止注入链被误改到别处。
 */
TEST(TestPaths, DefaultFallback_Adaptive_NFR_COR_01)
{
    setEnvVar(kEnvVar, "");
    const fs::path compiledDefault{IRD_TESTDATA_DEFAULT_DIR};
    // 默认路径必须指向 industrialrobot/testdata（尾段钉住；正斜杠形态来自
    // CMake 注入约定，generic_string 统一比较基）
    EXPECT_TRUE(compiledDefault.generic_string().find("testdata")
                != std::string::npos)
        << "编译默认应为 industrialrobot/testdata，实际: "
        << compiledDefault.string();

    if (fs::is_directory(compiledDefault)) {
        EXPECT_EQ(goldenDataRoot(), compiledDefault);
    } else {
        EXPECT_THROW(goldenDataRoot(), TestKitError);
    }
}

/**
 * @brief 数据集/容差档案目录拼装（§3.4 布局的路径投影）。
 *
 * 追溯：NFR-COR-01、testkit.md §3.4（golden/<datasetId>、
 * tolerance/<profileId>）。
 *
 * env 态下拼装结果＝<env>/golden|tolerance/<id>；同时钉住抛错透传：
 * 数据根不可解析时子路径函数不得"吞错返回半截路径"。
 */
TEST(TestPaths, DatasetAndToleranceDirLayout_NFR_COR_01)
{
    ScopedTempDir root;
    const std::string envValue = root.path().string();
    setEnvVar(kEnvVar, envValue.c_str());

    const auto ds = datasetDir("kin-fk-planar-2r");
    ASSERT_TRUE(ds.is_absolute());
    EXPECT_EQ(ds, fs::path{envValue} / "golden" / "kin-fk-planar-2r");

    const auto tol = toleranceProfileDir("kin-fk");
    EXPECT_EQ(tol, fs::path{envValue} / "tolerance" / "kin-fk");

    // 抛错透传：env 清空＋默认缺失时，子路径函数随之抛 env-unavailable
    // （不吞错、不返回部分路径——错误分流语义见 §7.2）
    setEnvVar(kEnvVar, "");
    if (!fs::is_directory(fs::path{IRD_TESTDATA_DEFAULT_DIR})) {
        EXPECT_THROW(datasetDir("x"), TestKitError);
        EXPECT_THROW(toleranceProfileDir("x"), TestKitError);
    }
}

/**
 * @brief TestKitError 载荷语义：kind/助记码/what 前缀三要素齐备。
 *
 * 追溯：testkit.md §5（错误类型契约）、§7.2（机器前缀）。
 *
 * what() 前缀与 toToken 必须一致（报告判读与日志判读不漂移）；detail
 * 内容须保留（定位信息不丢失）。
 */
TEST(TestPaths, ErrorCarriesKindTokenAndDetail_NFR_COR_01)
{
    const TestKitError err{TestKitErrorKind::EnvUnavailable, "定位信息示例"};
    EXPECT_EQ(err.kind(), TestKitErrorKind::EnvUnavailable);
    EXPECT_STREQ(TestKitError::toToken(err.kind()), "env-unavailable");
    const std::string what = err.what();
    // 前缀＝助记码＋冒号；其后携带 detail 原文
    EXPECT_EQ(what.find("env-unavailable: "), 0u);
    EXPECT_NE(what.find("定位信息示例"), std::string::npos);

    // 五枚举全部有稳定助记码（§5 冻结清单逐项钉住，防新增枚举漏登记）
    EXPECT_STREQ(TestKitError::toToken(TestKitErrorKind::DatasetInvalid), "dataset-invalid");
    EXPECT_STREQ(TestKitError::toToken(TestKitErrorKind::ToleranceUndefined), "tolerance-undefined");
    EXPECT_STREQ(TestKitError::toToken(TestKitErrorKind::UnitMismatch), "unit-mismatch");
    EXPECT_STREQ(TestKitError::toToken(TestKitErrorKind::Usage), "usage");
}
