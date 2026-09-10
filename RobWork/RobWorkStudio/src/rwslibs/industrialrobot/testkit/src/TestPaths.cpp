/**
 * @file   TestPaths.cpp
 * @brief  TestPaths 公共头的实现（数据根两级解析＋错误类型）。
 *
 * 设计依据：
 *   - units/testkit.md §4.6（解析规则）、§5（TestKitError 语义）
 *   - 任务契约 tasks/foundation/TK-T01.json（三态解析：env/默认/缺失）
 *
 * 背景说明：本文件是 sdurws_ird_testkit 的首个翻译单元（STATIC 库至少
 * 需要一个翻译单元，与 core/src/Core.cpp 同理）；后续 JsonLite（TK-T02）、
 * Dataset（TK-T03）等实现文件落地时与本文件并列于 src/。
 */

#include <sdurws/ird/testkit/TestPaths.hpp>

#include <cstdlib>  // std::getenv（并发只读安全）

namespace sdurws::ird::testkit {

// 环境变量名单点定义：goldenDataRoot 读取，测试（TestPathsTest）以字面量
// 引用同一名字符串。该名字与 CMake 缓存变量 SDURWS_IRD_TESTDATA_DIR 同名
// ——运行期变量覆盖编译期默认，两端口径一致（testkit.md §3.6）。
namespace {

constexpr const char* kTestDataEnvVar = "SDURWS_IRD_TESTDATA_DIR";

// 编译期默认数据根：由 testkit/CMakeLists.txt 以编译定义注入
// （-DIRD_TESTDATA_DEFAULT_DIR="<仓库内 industrialrobot/testdata 绝对路径>"）。
// CMake 变量值统一为正斜杠绝对路径（CMake 内部路径表示），std::filesystem
// 在 Windows 上原样接受。宏缺失＝构建配置不完整（脱离 testkit/CMakeLists
// 编译本文件），属调用方错误——fail-fast 编译失败，不允许静默使用猜测路径。
#ifndef IRD_TESTDATA_DEFAULT_DIR
#error "IRD_TESTDATA_DEFAULT_DIR 未定义：TestPaths.cpp 必须经 testkit/CMakeLists.txt 编译（数据根编译默认由其注入）"
#endif

}  // namespace

// ---- TestKitError ----

TestKitError::TestKitError(TestKitErrorKind kind, std::string detail)
    // what() 统一为"<助记码>: <detail>"：助记码供机器判读（§7.2 前缀），
    // detail 供人读定位；两类读者各取所需，报告写出不再重新拼装。
    : std::runtime_error(std::string{toToken(kind)} + ": " + detail),
      kind_(kind)
{
}

TestKitErrorKind TestKitError::kind() const noexcept { return kind_; }

const char* TestKitError::toToken(TestKitErrorKind kind) noexcept
{
    // switch 而非数组下标映射：枚举值重新编号/新增时不产生静默错位
    // （数组映射在枚举增删时会静默错位，违背"助记码单点不漂移"意图）。
    switch (kind) {
    case TestKitErrorKind::DatasetInvalid:      return "dataset-invalid";
    case TestKitErrorKind::ToleranceUndefined:  return "tolerance-undefined";
    case TestKitErrorKind::UnitMismatch:        return "unit-mismatch";
    case TestKitErrorKind::EnvUnavailable:      return "env-unavailable";
    case TestKitErrorKind::Usage:               return "usage";
    }
    return "unknown";  // 不可达（五枚举全覆盖）；保留返回值避免 UB
}

// ---- detail::resolveDataRoot ----

std::filesystem::path detail::resolveDataRoot(
    const char* envValue, const std::filesystem::path& compiledDefault)
{
    // 第一级：环境变量。非空即信任并原样返回——不校验目录存在性：
    // env 指向不存在目录属于后续数据装载阶段的失败（Dataset::load 时以
    // EnvUnavailable 暴露），本层不做二次校验以免拒绝"先解析路径、目录由
    // 外部稍后挂载"的 CI 形态（testkit.md §4.6 只对"回落默认"要求存在性）。
    // 空串视为未设置：防御 CI 空值配置；MSVC CRT 无 unsetenv，测试以
    // _putenv_s(name, "") 模拟"清除"，两端在此归一为同一语义。
    if (envValue != nullptr && *envValue != '\0') {
        return std::filesystem::path{envValue};
    }

    // 第二级：编译期默认。目录必须真实存在，否则视为环境不可用
    // （源内默认"开箱可用"前提被破坏——如仓库裁剪或路径配错）；
    // error_code 版本不抛异常（网络盘失联等访问错误按"不存在"处理，
    // 归入同一 EnvUnavailable 分类，消息中保留原始错误描述便于排查）。
    std::error_code ec;
    if (!std::filesystem::is_directory(compiledDefault, ec)) {
        throw TestKitError(
            TestKitErrorKind::EnvUnavailable,
            "编译期默认数据根不存在（未设环境变量 " +
                std::string{kTestDataEnvVar} + " 且默认目录缺失）: "
                + compiledDefault.string()
                + (ec ? "；目录状态检查错误: " + ec.message() : ""));
    }
    return compiledDefault;
}

// ---- 公共 API ----

std::filesystem::path goldenDataRoot()
{
    // 委托纯函数核心：真实 env 值＋编译默认。std::getenv 返回进程环境快照，
    // 与"测试串行修改 env"配合安全（并发写 env 未同步属调用方违约）。
    return detail::resolveDataRoot(std::getenv(kTestDataEnvVar),
                                   std::filesystem::path{IRD_TESTDATA_DEFAULT_DIR});
}

std::filesystem::path datasetDir(std::string_view datasetId)
{
    // 布局见 testkit.md §3.4：<root>/golden/<datasetId>/manifest.json。
    // operator/ 接受 string_view 转换的 string；id 合法性校验归装载层。
    return goldenDataRoot() / "golden" / std::string{datasetId};
}

std::filesystem::path toleranceProfileDir(std::string_view profileId)
{
    // 布局见 testkit.md §3.4：<root>/tolerance/<profileId>/v<version>.json。
    return goldenDataRoot() / "tolerance" / std::string{profileId};
}

}  // namespace sdurws::ird::testkit
