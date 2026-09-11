/**
 * @file   Fixture.cpp
 * @brief  夹具原语实现：TempDir 生命周期／ReproRecord JSON 往返／DeterministicEnv。
 *
 * 设计依据：units/testkit.md §6.2（临时目录与资源隔离）、§6.3（确定性环境
 * 与重放）；任务契约 tasks/foundation/TK-T08.json（≙WP-02-T08）。
 *
 * 实现要点（与头文件契约一一对应）：
 *   - TempDir 目录名＝<pid>-<tag>-<随机后缀>（§6.2 并行隔离三要素）；
 *   - 析构删除失败只告警不抛（§6.2 原文——析构路径抛错会遮蔽测试失败）；
 *   - ReproRecord 序列化经 JsonLite（TK-T02，受限 JSON 唯一入口——D-02），
 *     键序固定保证确定性输出（NFR-COR-02 精神）。
 */

#include <sdurws/ird/testkit/Fixture.hpp>

#include <sdurws/ird/testkit/JsonLite.hpp>  // parseJson/dumpJson（D-02：唯一 JSON 入口）

#include <cstdio>       // std::fprintf（析构告警输出——不用 std::cerr，析构期 iostream 全局对象可能已析构）
#include <sstream>      // std::ostringstream（目录名拼装/错误消息拼装）
#include <random>       // std::random_device＋mt19937_64（随机后缀——并行隔离三要素之一）
#include <vector>

#ifdef _WIN32
#include <process.h>  // _getpid（Windows：pid 三要素之一）
#else
#include <unistd.h>   // getpid（POSIX）
#endif

namespace sdurws::ird::testkit {
namespace {

/// 当前进程 id（§6.2 并行隔离三要素之一：不同测试进程目录必不同）。
int currentPid()
{
#ifdef _WIN32
    return static_cast<int>(::_getpid());
#else
    return static_cast<int>(::getpid());
#endif
}

/// 64 位随机后缀（16 进制；std::random_device 非确定性播种——基础设施层的
/// 随机性与测试语义无关，测试语义随机性一律走 ReproRecord.seed，§6.3）。
std::string randomSuffix()
{
    thread_local std::mt19937_64 rng{std::random_device{}()};
    std::ostringstream os;
    os << std::hex << rng();
    return os.str();
}

/// tag 合法性：[A-Za-z0-9._-]+（防路径分隔符/空字节注入目录名）。
bool tagSyntaxOk(std::string_view tag)
{
    if (tag.empty()) { return false; }
    for (const char c : tag) {
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
            || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
        if (!ok) { return false; }
    }
    return true;
}

/// ReproRecord → JsonValue 对象（键序固定——toJson 契约的确定性输出）。
JsonValue reproToJsonValue(const ReproRecord& r)
{
    JsonValue dataset;
    dataset.kind = JsonValue::Kind::Object;
    dataset.members.emplace_back("datasetId", JsonValue{});
    dataset.members.back().second.kind = JsonValue::Kind::String;
    dataset.members.back().second.text = r.dataset.datasetId;
    dataset.members.emplace_back("version", JsonValue{});
    dataset.members.back().second.kind = JsonValue::Kind::String;
    dataset.members.back().second.text = r.dataset.version;

    JsonValue v;
    v.kind = JsonValue::Kind::Object;
    auto putString = [&v](const char* key, const std::string& s) {
        JsonValue sv;
        sv.kind = JsonValue::Kind::String;
        sv.text = s;
        v.members.emplace_back(key, std::move(sv));
    };
    JsonValue seed;
    seed.kind = JsonValue::Kind::Number;
    seed.number = static_cast<double>(r.seed);   // JsonLite 数字为 double——
                                                 // 2^53 内整数按位精确（20260909 远小于界）
    v.members.emplace_back("seed", std::move(seed));
    JsonValue threads;
    threads.kind = JsonValue::Kind::Number;
    threads.number = static_cast<double>(r.threadCount);
    v.members.emplace_back("threadCount", std::move(threads));
    putString("softwareVersion", r.softwareVersion);
    putString("gitCommit", r.gitCommit);
    v.members.emplace_back("dataset", std::move(dataset));
    putString("toleranceProfile", r.toleranceProfile);
    putString("notes", r.notes);
    return v;
}

/// 取对象字段（缺失/类型不符即抛——消息含字段路径，§5 错误契约）。
const JsonValue& requireField(const JsonValue& obj, const char* key,
                              JsonValue::Kind kind)
{
    const JsonValue* f = obj.find(key);
    if (f == nullptr || f->kind != kind) {
        throw TestKitError(TestKitErrorKind::DatasetInvalid,
                           std::string{"repro-json: 字段缺失或类型不符: "} + key);
    }
    return *f;
}

/// 数字字段 → 整数值（非整数或超界抛——防静默截断，F-018③ 教训内化）。
std::int64_t requireIntegral(const JsonValue& obj, const char* key)
{
    const double d = requireField(obj, key, JsonValue::Kind::Number).number;
    const auto asInt = static_cast<std::int64_t>(d);
    if (static_cast<double>(asInt) != d) {
        throw TestKitError(TestKitErrorKind::DatasetInvalid,
                           std::string{"repro-json: 数字字段必须为整数: "} + key);
    }
    return asInt;
}

}  // namespace

// ---------------------------------------------------------------- TempDir --

TempDir::TempDir(std::string_view tag)
{
    // 第一步：tag 句法校验（调用方契约违约＝Usage，fail-fast）。
    if (!tagSyntaxOk(tag)) {
        throw TestKitError(TestKitErrorKind::Usage,
                           "usage: TempDir tag 不得为空且仅允许 [A-Za-z0-9._-]: "
                               + std::string{tag});
    }
    // 第二步：定位系统临时目录并拼装隔离名（§6.2：ird-test/<pid>-<tag>-<随机>）。
    std::error_code ec;
    const std::filesystem::path base = std::filesystem::temp_directory_path(ec);
    if (ec) {
        throw TestKitError(TestKitErrorKind::EnvUnavailable,
                           std::string{"env-unavailable: 系统临时目录不可得: "}
                               + ec.message());
    }
    std::ostringstream name;
    name << "ird-test/" << currentPid() << '-' << tag << '-' << randomSuffix();
    path_ = base / name.str();
    // 第三步：建立目录（父层 ird-test/ 一并创建；失败＝环境错误，fail-fast）。
    std::filesystem::create_directories(path_, ec);
    if (ec) {
        throw TestKitError(TestKitErrorKind::EnvUnavailable,
                           std::string{"env-unavailable: 临时目录建立失败: "}
                               + path_.string() + ": " + ec.message());
    }
}

TempDir::~TempDir()
{
    // 失败保留决策（§6.2 keepOnFailure 默认 true）：开关开且收到失败信号 → 保留现场。
    if (keepOnFailure_ && testFailed_) {
        // 保留现场并打印路径（"报告 artifacts 登记"的字段化输出随 TK-T10 落地；
        // 当前以 stderr 行为过渡形态，人工排查按此行定位）。
        std::fprintf(stderr, "ird-tempdir-kept: %s\n", path_.string().c_str());
        return;
    }
    // 常规路径：递归删除；error_code 重载保证不抛（§6.2 原文——析构不抛出）。
    std::error_code ec;
    std::filesystem::remove_all(path_, ec);
    if (ec) {
        // 删除失败只告警不抛：典型场景＝子进程/防病毒软件短期持有文件句柄。
        std::fprintf(stderr, "ird-tempdir-remove-failed: %s: %s\n",
                     path_.string().c_str(), ec.message().c_str());
    }
}

const std::filesystem::path& TempDir::path() const noexcept { return path_; }

void TempDir::keepOnFailure(bool keep) noexcept { keepOnFailure_ = keep; }

void TempDir::noteTestFailure(bool failed) noexcept { testFailed_ = failed; }

// ----------------------------------------------------------- ReproRecord --

bool ReproRecord::operator==(const ReproRecord& o) const noexcept
{
    return seed == o.seed && threadCount == o.threadCount
        && softwareVersion == o.softwareVersion && gitCommit == o.gitCommit
        && dataset.datasetId == o.dataset.datasetId
        && dataset.version == o.dataset.version
        && toleranceProfile == o.toleranceProfile && notes == o.notes;
}

std::string ReproRecord::toJson() const
{
    // dumpJson(..., false)：最短往返表示（同值同串——JsonLite 契约）。
    return dumpJson(reproToJsonValue(*this), false);
}

ReproRecord ReproRecord::fromJson(std::string_view text)
{
    // 解析失败（非 JSON/超限）由 JsonLite 抛 TestKitError(dataset-invalid)，
    // 消息带 "json-parse:" 前缀——此处统一在字段层补 "repro-json:" 定位信息。
    const JsonValue v = parseJson(text);
    if (!v.isObject()) {
        throw TestKitError(TestKitErrorKind::DatasetInvalid,
                           "repro-json: 顶层必须是 JSON 对象");
    }
    ReproRecord r;
    // 逐字段提取（缺失/类型不符即抛，消息含字段路径——拒绝必须可定位）。
    const auto seedValue = requireIntegral(v, "seed");
    if (seedValue < 0) {
        throw TestKitError(TestKitErrorKind::DatasetInvalid,
                           "repro-json: seed 不得为负: seed");
    }
    r.seed = static_cast<std::uint64_t>(seedValue);
    const auto threads = requireIntegral(v, "threadCount");
    if (threads < 1) {
        throw TestKitError(TestKitErrorKind::DatasetInvalid,
                           "repro-json: threadCount 必须 ≥1: threadCount");
    }
    r.threadCount = static_cast<int>(threads);
    r.softwareVersion = requireField(v, "softwareVersion", JsonValue::Kind::String).text;
    r.gitCommit = requireField(v, "gitCommit", JsonValue::Kind::String).text;
    const JsonValue& dataset = requireField(v, "dataset", JsonValue::Kind::Object);
    r.dataset.datasetId
        = requireField(dataset, "datasetId", JsonValue::Kind::String).text;
    r.dataset.version = requireField(dataset, "version", JsonValue::Kind::String).text;
    r.toleranceProfile
        = requireField(v, "toleranceProfile", JsonValue::Kind::String).text;
    r.notes = requireField(v, "notes", JsonValue::Kind::String).text;
    return r;
}

// -------------------------------------------------------- DeterministicEnv --

DeterministicEnv::DeterministicEnv(ReproRecord r) noexcept : record_(std::move(r)) {}

const ReproRecord& DeterministicEnv::record() const noexcept { return record_; }

}  // namespace sdurws::ird::testkit
