/**
 * @file   Report.cpp
 * @brief  机器可读测试结果实现：六类结果/记录序列化/聚合统计/写出/登记表。
 *
 * 设计依据：units/testkit.md §7.2（字段表与四类状态）、§7.5（CI 消费与
 * "不可判定"判据）；任务契约 tasks/foundation/TK-T10.json（≙WP-02-T10）。
 *
 * 实现要点：序列化全部经 JsonLite（D-02）；键序固定（确定性输出——
 * NFR-COR-02）；登记表操作互斥（防御性——gtest 主线程回调为当前事实）。
 */

#include <sdurws/ird/testkit/Report.hpp>

#include <cstdlib>       // std::getenv（机器标签）
#include <fstream>       // std::ofstream（报告写出）
#include <mutex>
#include <system_error>  // std::error_code（目录建立）

namespace sdurws::ird::testkit {
namespace report {
namespace {

/// 登记表全局互斥（gtest 主线程为当前唯一写者；互斥为防御性设计）。
std::mutex& storeMutex()
{
    static std::mutex m;
    return m;
}

/// JSON 值构造小工具：字符串字段。
JsonValue jsonString(std::string s)
{
    JsonValue v;
    v.kind = JsonValue::Kind::String;
    v.text = std::move(s);
    return v;
}

/// JSON 值构造小工具：数字字段（double——JsonLite 数字唯一形态）。
JsonValue jsonNumber(double d)
{
    JsonValue v;
    v.kind = JsonValue::Kind::Number;
    v.number = d;
    return v;
}

/// JSON 值构造小工具：字符串数组（空数组照写——键恒在，消费端无双态解析）。
JsonValue jsonStringArray(const std::vector<std::string>& items)
{
    JsonValue v;
    v.kind = JsonValue::Kind::Array;
    v.items.reserve(items.size());
    for (const auto& s : items) { v.items.push_back(jsonString(s)); }
    return v;
}

/// ReportSummary → JsonValue 对象（键序固定）。
JsonValue summaryToJson(const ReportSummary& s)
{
    JsonValue v;
    v.kind = JsonValue::Kind::Object;
    v.members.emplace_back("total", jsonNumber(s.total));
    v.members.emplace_back("passed", jsonNumber(s.passed));
    v.members.emplace_back("failed", jsonNumber(s.failed));
    v.members.emplace_back("skipped", jsonNumber(s.skipped));
    v.members.emplace_back("notRun", jsonNumber(s.notRun));
    v.members.emplace_back("envUnavailable", jsonNumber(s.envUnavailable));
    v.members.emplace_back("datasetInvalid", jsonNumber(s.datasetInvalid));
    // "不可判定"判据内联进报告——CI 聚合脚本与人工排查共用同一结论（§7.5）。
    JsonValue decisive;
    decisive.kind = JsonValue::Kind::Bool;
    decisive.boolean = s.decisive();
    v.members.emplace_back("decisive", std::move(decisive));
    return v;
}

}  // namespace

// ---------------------------------------------------------------- Outcome --

const char* toToken(Outcome o) noexcept
{
    switch (o) {
    case Outcome::Passed: return "passed";
    case Outcome::Failed: return "failed";
    case Outcome::Skipped: return "skipped";
    case Outcome::NotRun: return "notRun";
    case Outcome::EnvUnavailable: return "envUnavailable";
    case Outcome::DatasetInvalid: return "datasetInvalid";
    }
    return "notRun";  // 不可达（枚举全覆盖）——防御性回落
}

// ------------------------------------------------------------- TestRecord --

JsonValue TestRecord::toJson() const
{
    JsonValue v;
    v.kind = JsonValue::Kind::Object;
    auto put = [&v](const char* key, JsonValue value) {
        v.members.emplace_back(key, std::move(value));
    };
    // 键序＝§7.2 字段表序（确定性输出）。
    put("testId", jsonString(testId));
    put("requirementIds", jsonStringArray(requirementIds));
    put("atIds", jsonStringArray(atIds));
    // dataset：条件字段——hasDataset=false 时写 null（键恒在，无双态解析）。
    if (hasDataset) {
        JsonValue d;
        d.kind = JsonValue::Kind::Object;
        d.members.emplace_back("id", jsonString(datasetId));
        d.members.emplace_back("version", jsonString(datasetVersion));
        put("dataset", std::move(d));
    } else {
        put("dataset", JsonValue{});  // Kind::Null
    }
    put("toleranceProfile", jsonString(toleranceProfile));
    // repro：经公共 toJson()（确定性单行对象）解析回 JsonValue 嵌入——
    // 复用 ReproRecord 自身的序列化契约（Fixture.hpp），不重复键序定义。
    put("repro", parseJson(repro.toJson()));
    JsonValue env;
    env.kind = JsonValue::Kind::Object;
    env.members.emplace_back("compiler", jsonString(environment.compiler));
    env.members.emplace_back("qtVersion", jsonString(environment.qtVersion));
    env.members.emplace_back("arch", jsonString(environment.arch));
    env.members.emplace_back("os", jsonString(environment.os));
    env.members.emplace_back("machineTag", jsonString(environment.machineTag));
    put("environment", std::move(env));
    put("outcome", jsonString(toToken(outcome)));
    put("reason", jsonString(reason));
    JsonValue comps;
    comps.kind = JsonValue::Kind::Array;
    comps.items.reserve(comparisons.size());
    for (const auto& c : comparisons) {
        JsonValue item;
        item.kind = JsonValue::Kind::Object;
        item.members.emplace_back("fieldPath", jsonString(c.fieldPath));
        item.members.emplace_back("elementIndex", jsonNumber(static_cast<double>(c.elementIndex)));
        item.members.emplace_back("hasElement", [&c] {
            JsonValue b; b.kind = JsonValue::Kind::Bool; b.boolean = c.hasElement; return b;
        }());
        item.members.emplace_back("actual", jsonNumber(c.actual));
        item.members.emplace_back("expected", jsonNumber(c.expected));
        item.members.emplace_back("diff", jsonNumber(c.diff));
        item.members.emplace_back("unit", jsonString(c.unit));
        JsonValue tol;
        tol.kind = JsonValue::Kind::Object;
        tol.members.emplace_back("relative", jsonNumber(c.tolerance.relative));
        tol.members.emplace_back("absolute", jsonNumber(c.tolerance.absolute));
        item.members.emplace_back("tolerance", std::move(tol));
        comps.items.push_back(std::move(item));
    }
    put("comparisons", std::move(comps));
    // failureLocation：path 为空＝未置位 → null。
    if (!failureLocation.path.empty()) {
        JsonValue f;
        f.kind = JsonValue::Kind::Object;
        f.members.emplace_back("path", jsonString(failureLocation.path));
        f.members.emplace_back("elementIndex", jsonNumber(static_cast<double>(failureLocation.elementIndex)));
        JsonValue hasEl;
        hasEl.kind = JsonValue::Kind::Bool;
        hasEl.boolean = failureLocation.hasElement;
        f.members.emplace_back("hasElement", std::move(hasEl));
        f.members.emplace_back("setRole", jsonString(failureLocation.setRole));
        put("failureLocation", std::move(f));
    } else {
        put("failureLocation", JsonValue{});
    }
    JsonValue arts;
    arts.kind = JsonValue::Kind::Array;
    arts.items.reserve(artifacts.size());
    for (const auto& a : artifacts) {
        JsonValue item;
        item.kind = JsonValue::Kind::Object;
        item.members.emplace_back("path", jsonString(a.path));
        item.members.emplace_back("kind", jsonString(a.kind));
        arts.items.push_back(std::move(item));
    }
    put("artifacts", std::move(arts));
    put("durationMs", jsonNumber(static_cast<double>(durationMs)));
    return v;
}

std::string TestRecord::toJsonText(bool pretty) const
{
    return dumpJson(toJson(), pretty);
}

// ---------------------------------------------------------- ReportSummary --

bool ReportSummary::decisive() const noexcept
{
    // §7.5：envUnavailable/datasetInvalid/notRun 非零 → CI"不可判定"。
    return envUnavailable == 0 && datasetInvalid == 0 && notRun == 0;
}

// ----------------------------------------------------------------- Report --

void Report::add(TestRecord record) { records_.push_back(std::move(record)); }

const std::vector<TestRecord>& Report::records() const noexcept { return records_; }

ReportSummary Report::summary() const
{
    ReportSummary s;
    s.total = static_cast<int>(records_.size());
    for (const auto& r : records_) {
        switch (r.outcome) {
        case Outcome::Passed: ++s.passed; break;
        case Outcome::Failed: ++s.failed; break;
        case Outcome::Skipped: ++s.skipped; break;
        case Outcome::NotRun: ++s.notRun; break;
        case Outcome::EnvUnavailable: ++s.envUnavailable; break;
        case Outcome::DatasetInvalid: ++s.datasetInvalid; break;
        }
    }
    return s;
}

JsonValue Report::toJson() const
{
    JsonValue v;
    v.kind = JsonValue::Kind::Object;
    v.members.emplace_back("schemaVersion", jsonString("ird-test-report/1"));
    v.members.emplace_back("summary", summaryToJson(summary()));
    JsonValue tests;
    tests.kind = JsonValue::Kind::Array;
    tests.items.reserve(records_.size());
    for (const auto& r : records_) { tests.items.push_back(r.toJson()); }
    v.members.emplace_back("tests", std::move(tests));
    return v;
}

std::string Report::toJsonText(bool pretty) const
{
    return dumpJson(toJson(), pretty);
}

void Report::writeFile(const std::filesystem::path& filePath, bool pretty) const
{
    std::error_code ec;
    // 父目录不存在视为环境错误（CI 归档目录未建立——fail-fast 不静默）。
    std::filesystem::create_directories(filePath.parent_path(), ec);
    ec.clear();
    std::ofstream out(filePath, std::ios::binary);
    if (!out.is_open()) {
        throw TestKitError(TestKitErrorKind::EnvUnavailable,
                           "env-unavailable: 报告文件无法打开: " + filePath.string());
    }
    out << toJsonText(pretty);
    out.flush();
    if (!out.good()) {
        throw TestKitError(TestKitErrorKind::EnvUnavailable,
                           "env-unavailable: 报告文件写入失败: " + filePath.string());
    }
}

// --------------------------------------------------------- TestRecordStore --

TestRecordStore& TestRecordStore::instance()
{
    static TestRecordStore store;   // 进程级单例（静态存储期——listener 泄漏式注册同型）
    return store;
}

void TestRecordStore::beginRecord(std::string testId, Environment environment)
{
    const std::lock_guard<std::mutex> lock(storeMutex());
    auto record = std::make_unique<TestRecord>();
    record->testId = std::move(testId);
    record->environment = std::move(environment);
    stack_.push_back(std::move(record));   // 嵌套会话＝压栈（listener/测试各自闭环）
}

TestRecord* TestRecordStore::current() noexcept
{
    const std::lock_guard<std::mutex> lock(storeMutex());
    return stack_.empty() ? nullptr : stack_.back().get();
}

void TestRecordStore::endRecord(Outcome computedOutcome, std::string computedReason,
                                int durationMs)
{
    auto finalized = endAndTake(std::move(computedOutcome), std::move(computedReason),
                                durationMs);
    if (!finalized) { return; }
    const std::lock_guard<std::mutex> lock(storeMutex());
    finished_.push_back(std::move(*finalized));
}

std::unique_ptr<TestRecord> TestRecordStore::endAndTake(Outcome computedOutcome,
                                                        std::string computedReason,
                                                        int durationMs)
{
    const std::lock_guard<std::mutex> lock(storeMutex());
    if (stack_.empty()) { return nullptr; }  // 无活动记录（未 begin 即 end 为 no-op）
    auto record = std::move(stack_.back());
    stack_.pop_back();
    // 夹具预置 outcome 优先（§7.2：环境不可用/数据集非法不得因 GTEST_SKIP 计为 skipped）。
    if (record->outcome == Outcome::NotRun) {
        record->outcome = computedOutcome;
        record->reason = std::move(computedReason);
    }
    record->durationMs = durationMs;
    return record;
}

std::vector<TestRecord> TestRecordStore::takeAll()
{
    const std::lock_guard<std::mutex> lock(storeMutex());
    std::vector<TestRecord> out;
    out.reserve(finished_.size());
    for (auto& r : finished_) { out.push_back(std::move(r)); }
    finished_.clear();
    // 未定稿的活动记录（异常中断残留）按 NotRun 一并交付——发布检查表逐条消账。
    for (auto& r : stack_) { out.push_back(std::move(*r)); }
    stack_.clear();
    return out;
}

// ------------------------------------------------- 自由函数（测试体/夹具入口）--
// 实现口径：一律经 TestRecordStore::instance().current() 取栈顶（互斥在
// store 内部——本层不外持锁，避免非递归互斥重入死锁）；无活动记录＝no-op。

void irdTestInfo(std::vector<std::string> requirementIds,
                 std::vector<std::string> atIds,
                 std::optional<DatasetRef> dataset,
                 std::optional<std::string> toleranceProfile)
{
    TestRecord* record = TestRecordStore::instance().current();
    if (record == nullptr) { return; }  // 独立使用（无 listener）——no-op 不抛
    record->requirementIds = std::move(requirementIds);
    record->atIds = std::move(atIds);
    if (dataset.has_value()) {
        record->hasDataset = true;
        record->datasetId = dataset->datasetId;
        record->datasetVersion = dataset->version;
    }
    if (toleranceProfile.has_value()) { record->toleranceProfile = *toleranceProfile; }
}

void irdTestInfo(const std::string& requirementId,
                 std::vector<std::string> atIds,
                 std::optional<DatasetRef> dataset,
                 std::optional<std::string> toleranceProfile)
{
    irdTestInfo(std::vector<std::string>{requirementId}, std::move(atIds),
                std::move(dataset), std::move(toleranceProfile));
}

void bindFixtureContext(const ReproRecord& repro, const DatasetRef* dataset,
                        const std::string* toleranceProfile)
{
    TestRecord* record = TestRecordStore::instance().current();
    if (record == nullptr) { return; }
    record->repro = repro;   // §6.1 步骤⑥：复现上下文随记录登记
    if (dataset != nullptr) {
        record->hasDataset = true;
        record->datasetId = dataset->datasetId;
        record->datasetVersion = dataset->version;
    }
    if (toleranceProfile != nullptr) { record->toleranceProfile = *toleranceProfile; }
}

void setOutcome(Outcome outcome, std::string reason)
{
    TestRecord* record = TestRecordStore::instance().current();
    if (record == nullptr) { return; }
    record->outcome = outcome;
    record->reason = std::move(reason);
}

void addArtifact(std::string path, std::string kind)
{
    TestRecord* record = TestRecordStore::instance().current();
    if (record == nullptr) { return; }
    record->artifacts.push_back(Artifact{std::move(path), std::move(kind)});
}

void appendComparisons(const std::vector<CompareDetail>& failures)
{
    TestRecord* record = TestRecordStore::instance().current();
    if (record == nullptr) { return; }
    record->comparisons.insert(record->comparisons.end(), failures.begin(),
                               failures.end());
}

Environment defaultEnvironment()
{
    Environment env;
#if defined(_MSC_VER)
    env.compiler = "MSVC " + std::to_string(_MSC_VER);
#elif defined(__clang__)
    env.compiler = "clang";
#elif defined(__GNUC__)
    env.compiler = "gcc";
#else
    env.compiler = "unknown";
#endif
#if defined(_M_X64)
    env.arch = "x64";
#elif defined(_M_ARM64)
    env.arch = "arm64";
#elif defined(__x86_64__)
    env.arch = "x86_64";
#else
    env.arch = "unknown";
#endif
#if defined(_WIN32)
    env.os = "Windows";
#elif defined(__linux__)
    env.os = "Linux";
#elif defined(__APPLE__)
    env.os = "macOS";
#else
    env.os = "unknown";
#endif
    // 机器标签：Windows COMPUTERNAME / POSIX HOSTNAME（运行期环境，CI 可覆写）。
    if (const char* tag = std::getenv("COMPUTERNAME")) {
        env.machineTag = tag;
    } else if (const char* tag2 = std::getenv("HOSTNAME")) {
        env.machineTag = tag2;
    } else {
        env.machineTag = "unknown";
    }
    return env;
}

}  // namespace report
}  // namespace sdurws::ird::testkit
