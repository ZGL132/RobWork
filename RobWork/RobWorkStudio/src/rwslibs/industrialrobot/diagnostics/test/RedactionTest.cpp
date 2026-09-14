/**
 * @file   RedactionTest.cpp
 * @brief  脱敏与崩溃诊断文件用例组（DT-SEC-1~4、DT-LIFE-4、AT-11 观测点）
 *         与 P-DIAG-1 处置钉住——凭据零记录、路径四策略、脱敏失败降级、
 *         导出不泄敏、崩溃文件写出设施与磁盘不足降级。
 *
 * 设计依据：
 *   - units/diagnostics.md §10 DT-SEC-1~4/DT-LIFE-4 行（§11 任务分工：本套件
 *     承接 DIAG-T08 全部矩阵行；DT-SEC-1 的 log 半区经 LoggingPipeline 接线
 *     用例自证，DT-LIFE-4 的日志半区＝DT-LOG-3 既有面）、§7.6（崩溃诊断文
 *     件内容六段/用户目录/失败降级——AT-11 观测点"写出且脱敏"）、§7.7（脱
 *     敏规则表）、§9.5（IRedactionService 契约表——纯函数/绝不抛出/降级）
 *   - 需求 NFR-SEC-07（凭据一律不记录/敏感路径按配置）、PM-17（崩溃诊断文
 *     件）、NFR-REL-01 精神（DT-LIFE-4 降级不崩溃不阻塞）
 *   - 任务契约 tasks/foundation/DIAG-T08.json acceptance 1~4（逐条自证：
 *     acceptance 1→DtSec1/DtSec2/DtSec3/DtSec4 组；acceptance 2→DtLife4 组；
 *     acceptance 3→CrashReport/DevTail 组；acceptance 4→DtPdiag1 组＋编译期
 *     static_assert 钉住）
 *   - 用例名后缀＝矩阵行编号（DT-xxx-y 语义组），与 ird-test-report.json 的
 *     trace 追溯字段呼应（AGENTS.md §4.2 验证留痕）。
 *
 * 故障注入面（testkit D-10 形态自持替身——T-1 不链 testkit）：DT-SEC-3 经
 * applyRules 虚缝确定性触发"脱敏器自身失败"；DT-LIFE-4 经 ILogFileOps 接缝
 * （返回 false／抛异常两轨）触发磁盘满。
 *
 * 线程约束：全部用例单线程（管线用例以析构排空代替并发等待——DT-LOG 既有
 * 用例同款时序；并发面归 LoggingTest DT-LOG-2）。
 */

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <sdurws/ird/core/DiagData.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/diagnostics/Catalog.hpp>
#include <sdurws/ird/diagnostics/CrashReport.hpp>
#include <sdurws/ird/diagnostics/DiagCodes.hpp>
#include <sdurws/ird/diagnostics/Factory.hpp>
#include <sdurws/ird/diagnostics/Logging.hpp>
#include <sdurws/ird/diagnostics/Redaction.hpp>

namespace {

using namespace sdurws::ird::diagnostics;
// 测试文件位于全局匿名 ns：`core` 是 sdurws::ird 的成员，using-directive 不
// 引入兄弟命名空间——以别名使 core::X 限定名可见（T04~T07 套件同款处理面）。
namespace core = sdurws::ird::core;
namespace fs = std::filesystem;
using sdurws::ird::core::AttemptId;
using sdurws::ird::core::BranchId;
using sdurws::ird::core::ObjectId;
using sdurws::ird::core::ProjectId;
using sdurws::ird::core::RevisionId;
using sdurws::ird::core::RunId;
using sdurws::ird::core::TaskIdentity;

// ---------------------------------------------------------------------
// P-DIAG-1 编译期契约基线钉住（acceptance 4——core 冻结 diff 后增量同步、
// 不私改 core）。崩溃文件输入的 core 契约承载类型在编译期钉死：core.md
// v0.1（Draft 未冻结）基线的任何漂移会使本断言编译失败——"基线漂移被机器
// 看见"的机制（DIAG-T04 信封/T06 成员明细钉住同先例）。
// ---------------------------------------------------------------------
static_assert(
    std::is_same_v<decltype(CrashReportInput::openProject), std::optional<core::ProjectId>>,
    "P-DIAG-1: openProject 必须内嵌 core::ProjectId（core.md v0.1 基线；core 冻结"
    " diff 后增量同步，不私改 core）");
static_assert(
    std::is_same_v<decltype(CrashReportInput::activeTasks)::value_type, core::TaskIdentity>,
    "P-DIAG-1: activeTasks 必须内嵌 core::TaskIdentity 五元组（TASK-03；不另造"
    " 字符串形态——写出时经 toCanonical 规范文本）");
static_assert(
    std::is_same_v<decltype(CrashReportActiveFinding::id), FindingId>,
    "活动 finding 身份＝FindingId（§5.2——会话内稳定身份，P-DIAG-4 自持解析）");

// ---------------------------------------------------------------------
// 夹具辅助（与 LoggingTest/CatalogFactoryTest 同风格——自持不共享）
// ---------------------------------------------------------------------

/// 确定性测试时钟（§4.2 IClock 注释——ManualClock 兼容形态）。
class ManualClock final : public IClock {
public:
    std::chrono::system_clock::time_point nowUtc() const override { return m_now; }

    /// epoch 毫秒（崩溃文件名/时间戳断言用——与 CrashReport.cpp 同一口径）。
    std::uint64_t epochMs() const
    {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(m_now.time_since_epoch())
                .count());
    }

private:
    std::chrono::system_clock::time_point m_now{std::chrono::seconds{2000000}};
};

/// 每用例独立临时目录（RAII 清理——真实文件 I/O 用例的隔离面）。
class TempDirGuard {
public:
    TempDirGuard()
    {
        std::error_code ec;
        m_dir = fs::temp_directory_path(ec) / ("ird-redtest-" + std::to_string(++s_seq));
        fs::create_directories(m_dir, ec);
    }
    ~TempDirGuard()
    {
        std::error_code ec;
        fs::remove_all(m_dir, ec);  // 尽力而为清理——失败不遮蔽用例结论
    }
    const fs::path& path() const { return m_dir; }

private:
    inline static int s_seq = 0;
    fs::path m_dir;
};

/// 开发诊断捕获替身（IDevLogSink 窄接口——降级诊断的观测面）。
class CaptureSink final : public IDevLogSink {
public:
    void logDev(std::string_view channel, std::string message) override
    {
        rows.emplace_back(std::string(channel), std::move(message));
    }

    std::vector<std::pair<std::string, std::string>> rows;  ///< (channel, message) 序
};

/// 真实落盘文件操作（LoggingTest FaultFileOps 的 Ok 轨同款——std::ofstream）。
class SimpleFileOps final : public ILogFileOps {
public:
    bool appendLine(const fs::path& file, std::string_view line) override
    {
        std::ofstream out(file, std::ios::binary | std::ios::app);
        if (!out) { return false; }
        out.write(line.data(), static_cast<std::streamsize>(line.size()));
        out.put('\n');
        return static_cast<bool>(out);
    }
    bool flushFile(const fs::path&) override { return true; }
    bool rotateFile(const fs::path&, std::uint32_t) override { return true; }
    std::uint64_t fileSize(const fs::path& file) override
    {
        std::error_code ec;
        const auto s = fs::file_size(file, ec);
        return ec ? 0u : static_cast<std::uint64_t>(s);
    }
};

/// 恒失败文件操作（DT-LIFE-4 磁盘满注入——返回 false 轨）。
class FailingFileOps final : public ILogFileOps {
public:
    bool appendLine(const fs::path&, std::string_view) override { return false; }
    bool flushFile(const fs::path&) override { return false; }
    bool rotateFile(const fs::path&, std::uint32_t) override { return false; }
    std::uint64_t fileSize(const fs::path&) override { return 0; }
};

/// 抛异常文件操作（DT-LIFE-4 磁盘满注入——抛异常轨；接缝契约允许，消化
/// 责任在使用方——CrashReportWriter 须不外溢）。
class ThrowingFileOps final : public ILogFileOps {
public:
    bool appendLine(const fs::path&, std::string_view) override
    {
        throw std::runtime_error("injected disk fault (crash report append)");
    }
    bool flushFile(const fs::path&) override { return false; }
    bool rotateFile(const fs::path&, std::uint32_t) override { return false; }
    std::uint64_t fileSize(const fs::path&) override { return 0; }
};

/// 读整个文本文件（内容断言用）。
std::string readTextFile(const fs::path& file)
{
    std::error_code ec;
    EXPECT_TRUE(fs::exists(file, ec)) << "目标文件应存在: " << file.string();
    std::ifstream in(file, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

/// 子串存在性（读断言的可读包装）。
bool contains(const std::string& text, std::string_view needle)
{
    return text.find(needle) != std::string::npos;
}

/// 合法五元组（TASK-03——五字段全 isValid；崩溃文件/工厂用例共用）。
TaskIdentity makeTaskIdentity()
{
    TaskIdentity task;
    task.project = ProjectId::generate();
    task.branch = BranchId::generate();
    task.revision = RevisionId::generate();
    task.run = RunId::generate();
    task.attempt = AttemptId::fromCanonical("att-1");
    return task;
}

/// 五元组规范串（与 Logging.cpp taskCanonical／CrashReport.cpp 组装同形——
/// P-DIAG-1 运行期逐值断言的期望值构造）。
std::string taskCanonical(const TaskIdentity& t)
{
    return t.project.toCanonical() + "/" + t.branch.toCanonical() + "/"
         + t.revision.toCanonical() + "/" + t.run.toCanonical() + "/"
         + t.attempt.toCanonical();
}

/// 组装并 seal 内置全量注册表（工厂按"运行期只读"消费——T04 套件同款）。
void sealBuiltinRegistry(StableCodeRegistry& registry)
{
    registerBuiltinCodes(registry);
    registry.seal();
}

/// 合法五元组十六进制形态检查（路径 Hash token 的形状断言用）。
bool is16LowerHex(std::string_view s)
{
    if (s.size() != 16) { return false; }
    for (const char ch : s) {
        const bool hex = (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f');
        if (!hex) { return false; }
    }
    return true;
}

// =====================================================================
// DT-SEC-1——凭据不记录（NFR-SEC-07；§7.7 凭据/令牌行）
// =====================================================================

TEST(RedactionRules, DtSec1_CredentialPatternsNeverRecorded)
{
    // 契约：消息含 token/password 形态→输出无原文（模式扫描零命中）＋替换
    // 计数保留（§7.3②"[REDACTED:<kind>:n] 保留计数不保留原文"）。
    const RedactionService red;
    const std::string raw =
        "connect password=hunter2 api_key: s3cr3t-key token\t=\ttok-9f8e7d6c5d "
        "Authorization: Bearer eyJhbGciOiJIUzI1NiJ9.eyJzdWIiOiIxMjM0NTY3ODkwIn0"
        ".SflKxwRJSMeKKF2QT4fwpMeJf36POk6yJVadQssw5c private_key=MiIePtKey123";
    const std::string out = red.redact(raw, LogTier::Dev);

    // 模式扫描零命中——原文值段一个不剩（DT-SEC-1 观测点）。
    EXPECT_FALSE(contains(out, "hunter2")) << "密码原文泄露: " << out;
    EXPECT_FALSE(contains(out, "s3cr3t-key")) << "API 键原文泄露";
    EXPECT_FALSE(contains(out, "tok-9f8e7d6c5d")) << "令牌原文泄露";
    EXPECT_FALSE(contains(out, "eyJhbGciOiJIUzI1NiJ9")) << "JWT 原文泄露";
    EXPECT_FALSE(contains(out, "MiIePtKey123")) << "私钥原文泄露";
    // 替换计数保留（键名是结构不是秘密——保留可读性；§14.4 v0.9 口径）。
    EXPECT_TRUE(contains(out, "password=[REDACTED:credential:1]"));
    EXPECT_TRUE(contains(out, "[REDACTED:token:"));
    // 命中计数可观测（R-7"模式命中计数可观测（DT-SEC-1 观测点）"）。
    EXPECT_GE(red.stats().credentialHits, 3u);
    EXPECT_GE(red.stats().tokenHits, 1u);
}

TEST(RedactionRules, DtSec1_DeterministicAndIdempotent)
{
    // 契约：纯函数——同输入同输出（§9.5"幂等/纯函数"行，DT-SEC-1 依据
    // NFR-COR-01 的确定性面）；已脱敏文本再过脱敏不二次改写（管线双保险
    // 链路的不变式——CrashReport 依赖此性质）。
    const RedactionService red;
    const std::string raw = "login user=me password=abc123";

    const std::string first = red.redact(raw, LogTier::Dev);
    const std::string second = red.redact(raw, LogTier::Dev);
    EXPECT_EQ(first, second) << "同输入必须同输出（确定性，§9.5）";

    const std::string again = red.redact(first, LogTier::Dev);
    EXPECT_EQ(again, first) << "已脱敏文本再过脱敏须逐字节稳定（双保险链前提）";
    EXPECT_TRUE(contains(again, "[REDACTED:credential:1]"));
}

// =====================================================================
// DT-SEC-2——路径按配置脱敏（NFR-SEC-07；§7.7 路径行四策略）
// =====================================================================

TEST(RedactionRules, DtSec2_PathPolicyKeep)
{
    // Keep＝原文保留（默认开发机口径）——其余规则（凭据/用户名）仍生效，
    // Keep 只豁免路径策略本身。
    RedactionService red;
    red.setPolicy(RedactionPolicy{PathPolicy::Keep, 32});
    const std::string path = R"(D:\data\public\arm.stl)";
    EXPECT_EQ(red.redactPath(path), path);
    // 用户名不因 Keep 泄露（§7.7 用户名行无策略条件——无条件脱敏）。
    const std::string out = red.redact("cfg from C:\\Users\\bob\\docs\\x.txt", LogTier::Dev);
    EXPECT_FALSE(contains(out, "bob")) << "Keep 策略下用户名仍不得泄露: " << out;
    EXPECT_TRUE(contains(out, "[USER]\\docs\\x.txt"));
    // Keep 不改写路径但命中计数可见（R-7 观测面——redactPath 命中即计）。
    EXPECT_GE(red.stats().pathHits, 1u);
}

TEST(RedactionRules, DtSec2_PathPolicyHash)
{
    // Hash＝SHA-256 截断 token——同路径同 token（可关联）、不同路径异 token、
    // 不可逆推（§14.4 v0.9 口径 3：[PATH-<16hex>]）。
    RedactionService red;
    red.setPolicy(RedactionPolicy{PathPolicy::Hash, 32});
    const std::string a = red.redactPath(R"(D:\data\secret\model.stl)");
    const std::string a2 = red.redactPath(R"(D:\data\secret\model.stl)");
    const std::string b = red.redactPath(R"(D:\data\other\model.stl)");

    EXPECT_NE(a, R"(D:\data\secret\model.stl)") << "Hash 必须改写原文";
    EXPECT_EQ(a, a2) << "同路径同 token（可关联）";
    EXPECT_NE(a, b) << "不同路径异 token";
    ASSERT_EQ(a.size(), 6u + 16u + 1u) << "token 形态＝[PATH-<16hex>]: " << a;
    EXPECT_EQ(a.substr(0, 6), "[PATH-") << "token 前缀: " << a;
    EXPECT_TRUE(is16LowerHex(a.substr(6, 16))) << "16 位小写 hex: " << a;
    EXPECT_EQ(a.back(), ']');
    // 消息内嵌路径同样被 Hash（§7.7"本机路径按配置脱敏"的整条消息承载）。
    const std::string out = red.redact("load from D:\\data\\secret\\model.stl done",
                                       LogTier::Dev);
    EXPECT_FALSE(contains(out, "secret"));
    EXPECT_TRUE(contains(out, "[PATH-"));
}

TEST(RedactionRules, DtSec2_PathPolicyRootOnly)
{
    // RootOnly＝保留盘符＋一级目录＋"…"＋文件名（§7.7 文本口径"保留盘符＋
    // 一级目录"；默认策略——§9.5 RedactionPolicy 默认值）。
    const RedactionService red;  // 默认 RootOnly
    EXPECT_EQ(red.policy().pathPolicy, PathPolicy::RootOnly);

    // 盘符路径：一级目录保留、中间层级隐藏、文件名保留。
    EXPECT_EQ(red.redactPath(R"(D:\data\project\mesh\arm.stl)"),
              R"(D:\data\…\arm.stl)");
    // 仅根级文件名：无目录结构可隐藏——原样保留（§14.4 v0.9 口径 4）。
    EXPECT_EQ(red.redactPath(R"(D:\arm.stl)"), R"(D:\arm.stl)");
    // UNC：server\share 即"根"，jobs 为一级目录（盘符/UNC 根之后的第一个
    // 段——与盘符路径同一"保留盘符＋一级目录"口径）。
    EXPECT_EQ(red.redactPath(R"(\\srv\share\jobs\deep\out.txt)"),
              R"(\\srv\share\jobs\…\out.txt)");
    // POSIX 绝对：根＋一级目录＋"…"＋文件名。
    EXPECT_EQ(red.redactPath("/opt/rob/data/mesh.stl"), "/opt/…/mesh.stl");
    // 相对路径不处理（口径 5——非"本机路径"，处理即误伤 R-7）。
    EXPECT_EQ(red.redactPath("models/arm.stl"), "models/arm.stl");
}

TEST(RedactionRules, DtSec2_PathPolicyStrip)
{
    // Strip＝仅文件名（§7.7 路径行原文）。
    RedactionService red;
    red.setPolicy(RedactionPolicy{PathPolicy::Strip, 32});
    EXPECT_EQ(red.redactPath(R"(D:\data\secret\arm.stl)"), "arm.stl");
    EXPECT_EQ(red.redactPath("/opt/rob/mesh.stl"), "mesh.stl");
    // 消息内路径仅剩文件名。
    const std::string out = red.redact("cannot read D:\\data\\secret\\arm.stl", LogTier::Dev);
    EXPECT_FALSE(contains(out, "D:"));
    EXPECT_FALSE(contains(out, "secret"));
    EXPECT_TRUE(contains(out, "arm.stl"));
}

// =====================================================================
// DT-SEC-3——脱敏失败降级（§7.7 降级行/§7.3②；绝不放行原文、不抛）
// =====================================================================

/// 故障子类（applyRules 虚缝覆写抛出——确定性复现"脱敏器自身失败"；产品
/// 路径不覆写——Redaction.hpp 模板方法注释）。
class FaultRedactionService final : public RedactionService {
public:
    using RedactionService::RedactionService;

protected:
    void applyRules(std::string&, LogTier, RedactionCounters&) const override
    {
        throw std::runtime_error("injected redaction engine fault (DT-SEC-3)");
    }
};

TEST(RedactionRules, DtSec3_DegradationNoRawNoThrow)
{
    // 契约（§10 DT-SEC-3 行）：构造超限/非法输入→整段
    // [REDACTED:redaction-failed]＋DIAG-REDACTION-FAILED；不抛、不放行原文
    // （观测点＝输出字面量＋Dev 诊断存在）。
    CaptureSink sink;
    const FaultRedactionService red{RedactionPolicy{}, &sink};
    const std::string raw = "must never leak password=hunter2 path=D:\\x\\y\\z.stl";

    std::string out;
    EXPECT_NO_THROW({ out = red.redact(raw, LogTier::Dev); })
        << "redact 契约 noexcept——内部失败必须降级而非外溢";
    EXPECT_EQ(out, "[REDACTED:redaction-failed]") << "整条降级字面量（§7.3② 原文）";
    EXPECT_FALSE(contains(out, "hunter2")) << "降级路径绝不放行原文";

    // safeSummary 同一降级出口（§9.5 双保险入口不得成为泄露面）。
    EXPECT_EQ(red.safeSummary(raw, 512), "[REDACTED:redaction-failed]");

    // Dev 诊断存在（§7.3②"＋DIAG-REDACTION-FAILED 开发诊断"；DIAG-T03 已
    // 注册的稳定码——文本承载于诊断消息，channel＝脱敏设施保留通道）。
    ASSERT_GE(sink.rows.size(), 1u) << "降级必须伴随 DIAG-REDACTION-FAILED 开发诊断";
    EXPECT_EQ(sink.rows[0].first, "diag/redaction");
    EXPECT_TRUE(contains(sink.rows[0].second, "DIAG-REDACTION-FAILED"));
    // 诊断消息为常量文本——绝不回显原文（放行原文即防线失效）。
    EXPECT_FALSE(contains(sink.rows[0].second, "hunter2"));

    // 失败计数可观测；连续失败稳定降级（第二次调用同样字面量——无递归副作用）。
    EXPECT_EQ(red.stats().failures, 2u);
    EXPECT_EQ(red.redact(raw, LogTier::User), "[REDACTED:redaction-failed]");
    EXPECT_EQ(red.stats().failures, 3u);
}

TEST(RedactionRules, DtSec3_SilentDegradeWithoutSink)
{
    // failureSink 为空＝静默降级（装配前合法降态——Redaction.hpp 构造注释；
    // 字面量语义不变）。
    const FaultRedactionService red;
    EXPECT_EQ(red.redact("password=x", LogTier::Dev), "[REDACTED:redaction-failed]");
    EXPECT_EQ(red.stats().failures, 1u);
}

// =====================================================================
// 环境变量/用户名规则与摘要形状（§7.7 环境变量行/用户名行/资源内容行）
// =====================================================================

TEST(RedactionRules, EnvAndUserNameRules)
{
    const RedactionService red;
    // 环境变量：IRD_* 白名单保留；用户身份集→[USER]；其余整体不记录。
    const std::string out =
        red.redact("tmp=%TEMP% home=%USERPROFILE% other=%SOME_SECRET_VAR% "
                   "prod=${IRD_WORKER_TMP} posix=${HOME_DIR}",
                   LogTier::Dev);
    EXPECT_TRUE(contains(out, "[USER]")) << "用户身份变量→[USER]（§7.7 用户名行）";
    EXPECT_FALSE(contains(out, "SOME_SECRET_VAR")) << "白名单外环境变量整体不记录";
    EXPECT_TRUE(contains(out, "${IRD_WORKER_TMP}"))
        << "产品自用白名单保留（含定界符整引用回填——§7.7 环境变量行）";
    EXPECT_TRUE(contains(out, "[REDACTED:env:1]")) << "非白名单→[REDACTED:env:n]";
    EXPECT_EQ(red.stats().envHits, 2u);
    EXPECT_GE(red.stats().userHits, 2u);

    // 用户名路径段无条件脱敏（先于路径策略，Keep 下用户名也不泄露）。
    const std::string win = red.redact("profile C:\\Users\\alice\\app\\cfg.ini", LogTier::Dev);
    EXPECT_FALSE(contains(win, "alice"));
    EXPECT_TRUE(contains(win, "[USER]\\app\\cfg.ini"));
    // /home/<名> 替换后，剩余 POSIX 路径仍会整体过路径策略（RootOnly 把
    // "/home/…"折叠为根＋一级目录——[USER] 与后续段一并隐藏，保守方向）。
    const std::string posix = red.redact("profile /home/bob/work/x.stl", LogTier::Dev);
    EXPECT_FALSE(contains(posix, "bob"));
    EXPECT_TRUE(contains(posix, "/home/…/x.stl")) << "实际输出: " << posix;
}

TEST(RedactionRules, R7_CanonicalIdsAndDigestsNotMangledOnDev)
{
    // R-7 防误伤（§14.2）：规范身份（<tag>-<32hex>）是关联锚点——Dev 档原样
    // 保留；纯十六进制摘要串不是凭证形态——不按令牌替换（口径 2）。
    const RedactionService red;
    const ProjectId pid = ProjectId::generate();
    const std::string out =
        red.redact("run for " + pid.toCanonical() + " digest "
                       + std::string("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"),
                   LogTier::Dev);
    EXPECT_TRUE(contains(out, pid.toCanonical()))
        << "规范身份必须逐字保留（Dev 档）: " << out;
    EXPECT_TRUE(contains(out, "0123456789abcdef"))
        << "纯十六进制摘要不按凭证串替换（R-7）";

    // User 档（报告面）语义相反：NFR-REL-05/UX-02"用户级无哈希/地址"——
    // ≥32 位十六进制→[HASH]、0x 地址→[ADDR]（§7.3② Tier-U 额外过滤）。
    const std::string user = red.redact("digest 0123456789abcdef0123456789abcdef"
                                        "0123456789abcdef0123456789abcdef addr 0x7ff6a000",
                                        LogTier::User);
    EXPECT_FALSE(contains(user, "0123456789abcdef0123456789abcdef"));
    EXPECT_TRUE(contains(user, "[HASH]"));
    EXPECT_TRUE(contains(user, "[ADDR]"));
}

TEST(RedactionRules, SafeSummaryTruncatesUtf8Boundary)
{
    // safeSummary＝User 档脱敏＋maxBytes UTF-8 边界安全截断＋"[trunc]" 标注
    // （§9.5"安全摘要导出"；§7.6 异常消息 512 B 的执行入口）。
    const RedactionService red;
    const std::string ascii(1000, 'x');  // 'x' 非十六进制字符——不会被 [HASH] 遮蔽
    const std::string out = red.safeSummary(ascii, 100);
    ASSERT_LE(out.size(), 100u + std::string("[trunc]").size());
    EXPECT_TRUE(contains(out, "[trunc]"));

    // 中文（多字节 UTF-8）切点不劈开码位——截断后仍是合法 UTF-8 前缀。
    std::string utf8;
    for (int i = 0; i < 300; ++i) {
        utf8 += "机";  // 3 字节 UTF-8
    }
    const std::string cut = red.safeSummary(utf8, 100);
    EXPECT_TRUE(contains(cut, "[trunc]"));
    // 100/3=33 个完整"机"（99 字节）＋"[trunc]"——总长 99+7。
    EXPECT_EQ(cut.size(), 99u + std::string("[trunc]").size());

    // maxBytes=0＝不截断，只脱敏（§9.5 签名注释）。
    const std::string raw = "password=topsecret";
    const std::string full = red.safeSummary(raw, 0);
    EXPECT_FALSE(contains(full, "topsecret"));
    EXPECT_TRUE(contains(full, "[REDACTED:credential:1]"));
}

// =====================================================================
// DT-SEC-1/2 的管线半区（log＋redact——§7.3② 步骤②接线；DT-SEC-4 的
// 目录导出半区在 RedactionExport 夹具）
// =====================================================================

class RedactionPipeline : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_red = std::make_shared<RedactionService>();  // 默认 RootOnly
    }

    /// 配置日志到临时目录（两 Tier 全开——默认值）。
    void configureToTemp(LoggingPipeline& logger) const
    {
        LogSinkConfig cfg;
        cfg.directory = m_dir.path();
        logger.configure(cfg);
    }

    TempDirGuard m_dir;
    ManualClock m_clock;
    SimpleFileOps m_ops;
    std::shared_ptr<RedactionService> m_red;
};

TEST_F(RedactionPipeline, DtSec1_PipelineScanZeroHitsBothTiers)
{
    // 契约（§10 DT-SEC-1 行"操作＝log＋redact"）：脱敏服务接线后，管线步骤
    // ②强制对每条消息执行 NFR-SEC-07 全量脱敏——两 Tier 文件模式扫描零命中
    // （Tier-U ⊆ Tier-D 同一脱敏管线，§7.1）。
    {
        LoggingPipeline logger(m_clock, m_ops);
        configureToTemp(logger);
        logger.attachRedactionService(m_red);

        LogRecord rec;
        rec.tier = LogTier::User;
        rec.level = LogLevel::Warning;
        rec.channel = "test/sec";
        rec.message = "connect password=hunter2 to D:\\data\\secret\\arm.stl";
        logger.log(std::move(rec));
        // 离开作用域＝析构排空（文件写完再读——DT-LOG 既有用例同款时序）。
    }
    const std::string userText = readTextFile(m_dir.path() / "user-diagnostics.log");
    const std::string devText = readTextFile(m_dir.path() / "dev-diagnostics.log");

    // 两级文件模式扫描零命中（凭据原文＋敏感路径段）。
    // 注意：日志行编码对 '\' 转义为 '\\'（escapeLineMessage——行完整性契约），
    // 文件内断言按转义后形态比对。
    for (const std::string* text : {&userText, &devText}) {
        EXPECT_FALSE(contains(*text, "hunter2")) << "凭据原文入文件（Tier 面）";
        EXPECT_FALSE(contains(*text, "secret")) << "敏感路径段入文件";
        EXPECT_TRUE(contains(*text, "[REDACTED:credential:1]")) << "替换计数保留";
        EXPECT_TRUE(contains(*text, "D:\\\\data\\\\…\\\\arm.stl")) << "路径按 RootOnly 策略";
    }
}

TEST_F(RedactionPipeline, DevTailRingKeepsLast512Lines)
{
    // 崩溃前快照环形缓冲（§7.6"最近 512 行开发日志快照（重放内存环形缓冲）"
    // ——acceptance 3 的数据源面；容量＝kLogDevTailRingLines）。
    std::vector<std::string> snapshot;
    std::vector<std::string> last3;
    {
        LoggingPipeline logger(m_clock, m_ops);
        configureToTemp(logger);
        logger.attachRedactionService(m_red);
        for (int i = 1; i <= 600; ++i) {
            // Dev＋Warning：不触及采样（仅 Debug/Trace 可采样）与节流（无
            // code 不参与节流）——行数确定性。
            logger.logDev("test/ring", LogLevel::Warning, "line " + std::to_string(i));
        }
        logger.flush(std::chrono::milliseconds(3000));  // 排空——环形已写满 600→512
        snapshot = logger.devTailSnapshot(kCrashReportDevTailLines);
        last3 = logger.devTailSnapshot(3);
    }
    ASSERT_EQ(snapshot.size(), 512u) << "环形覆盖＝只保最近 512 行（§7.6）";
    EXPECT_TRUE(contains(snapshot.front(), "msg=line 89")) << "600 行只留 89~600";
    EXPECT_TRUE(contains(snapshot[1], "msg=line 90")) << "快照按写入序返回（重放语义）";
    EXPECT_TRUE(contains(snapshot.back(), "msg=line 600"));
    // maxLines 截取最近 N 行（调用方按 §7.6 传 512 或更小）。
    ASSERT_EQ(last3.size(), 3u);
    EXPECT_TRUE(contains(last3.back(), "msg=line 600"));
    EXPECT_TRUE(contains(last3.front(), "msg=line 598"));
}

// =====================================================================
// DT-SEC-4——目录导出不泄敏感路径（RPT/AT-11 精神；§7.7 与 reporting 行）
// =====================================================================

class RedactionExport : public ::testing::Test {
protected:
    void SetUp() override
    {
        sealBuiltinRegistry(m_registry);
        m_factory = std::make_unique<DiagnosticsFactory>(m_registry, m_clock);
        m_red = std::make_shared<RedactionService>();
        m_factory->attachRedactionService(m_red);
    }

    /// 造一条 params 内嵌敏感内容的条目（PRJ-LOCK-HELD＝码表登记 schema
    /// ["pid","host"] 的两参数码——参数键/数量必须匹配，值占位自由）。
    DiagnosticEntry appendSensitiveEntry()
    {
        const ObjectId subject = ObjectId::generate();
        const core::DiagnosticRecord record = core::DiagnosticRecord::make(
            "PRJ-LOCK-HELD", subject, std::string("project_x"),
            std::string("demo"), std::string("项目被其他进程锁定"),
            std::string("PID 4242 持有写锁"), std::string("等待或接管（陈旧锁）"));
        DiagContext context;
        context.sourceUnit = "project";
        context.sourceInterface = "store.open";
        context.params = {{"pid", "password=hunter2 home=%USERPROFILE%"},
                          {"host", R"(D:\data\secret\arm.stl)"}};
        const DiagnosticEntry entry = m_factory->create(record, context);
        m_catalog.append(entry);
        return entry;
    }

    StableCodeRegistry m_registry;
    ManualClock m_clock;
    std::unique_ptr<DiagnosticsFactory> m_factory;
    DiagCatalog m_catalog;
    std::shared_ptr<RedactionService> m_red;
};

TEST_F(RedactionExport, DtSec4_ExportNoSensitivePathsOrUsernames)
{
    // 契约（§10 DT-SEC-4 行）：目录含路径类内容→exportSafeSummary 输出过
    // 脱敏（无原始路径/用户名——双保险，§7.7 与 reporting 行）。
    const DiagnosticEntry entry = appendSensitiveEntry();
    m_catalog.attachRedactionService(m_red);

    DiagQuery query;
    const std::string exported = m_catalog.exportSafeSummary(query, 0);

    EXPECT_FALSE(contains(exported, "secret")) << "原始路径段泄露: " << exported;
    EXPECT_FALSE(contains(exported, "hunter2")) << "凭据值泄露";
    EXPECT_FALSE(contains(exported, "USERPROFILE")) << "用户目录变量泄露";
    EXPECT_TRUE(contains(exported, "D:\\data\\…\\arm.stl")) << "路径按 RootOnly 呈现";
    EXPECT_TRUE(contains(exported, "[USER]")) << "用户目录变量替换为 [USER]";
    // 机器可读锚点保留（R-7 防误伤）：subject 规范身份逐字在案——Dev 档
    // 不做内部十六进制遮蔽（§14.4 v0.9 接线口径）。
    ASSERT_TRUE(entry.record.subject.has_value());
    EXPECT_TRUE(contains(exported, entry.record.subject->toCanonical()));
    // 稳定码字段完整（§8.10 导出字段面）。
    EXPECT_TRUE(contains(exported, "PRJ-LOCK-HELD"));
}

TEST_F(RedactionExport, DtSec4_UnattachedExportKeepsBaselineShape)
{
    // 未挂接＝DIAG-T04 原语义（装配前合法降态——接线是显式装配动作；
    // 本用例同时证明"接线是载重的"：同一内容挂接前后输出不同）。
    appendSensitiveEntry();
    DiagQuery query;
    const std::string baseline = m_catalog.exportSafeSummary(query, 0);
    EXPECT_TRUE(contains(baseline, "D:\\data\\secret\\arm.stl")) << "基线＝未脱敏原样";

    m_catalog.attachRedactionService(m_red);
    const std::string wired = m_catalog.exportSafeSummary(query, 0);
    EXPECT_FALSE(contains(wired, "secret")) << "挂接后双保险生效";
    EXPECT_NE(baseline, wired);
}

// =====================================================================
// 工厂接线：redactedContextSnapshot（§4.2"构造时经 IRedactionService 产出
// ——接线随该任务落地"）与 translate 异常消息脱敏整备（v0.4 口径④）
// =====================================================================

class RedactionFactory : public ::testing::Test {
protected:
    void SetUp() override
    {
        sealBuiltinRegistry(m_registry);
        m_factory = std::make_unique<DiagnosticsFactory>(m_registry, m_clock);
        m_red = std::make_shared<RedactionService>();
    }

    StableCodeRegistry m_registry;
    ManualClock m_clock;
    std::unique_ptr<DiagnosticsFactory> m_factory;
    std::shared_ptr<RedactionService> m_red;
};

TEST_F(RedactionFactory, SnapshotRedactedWhenAttached)
{
    // PRJ-LOCK-HELD＝码表登记 schema ["pid","host"] 的两参数码（参数键/数量
    // 必须匹配——占位一致性校验；值占位自由，用来承载敏感内容）。
    DiagContext context;
    context.sourceUnit = "project";
    context.sourceInterface = "store.open";
    context.params = {{"pid", "password=hunter2 home=%USERPROFILE%"},
                      {"host", R"(D:\data\secret\arm.stl)"}};
    const ObjectId subject = ObjectId::generate();
    const core::DiagnosticRecord record = core::DiagnosticRecord::make(
        "PRJ-LOCK-HELD", subject, std::string("project_x"), std::string("demo"),
        std::string("项目被其他进程锁定"), std::string("PID 4242 持有写锁"),
        std::string("等待或接管（陈旧锁）"));

    // 挂接前＝nullopt（v0.4 原语义——装配前合法降态）。
    const DiagnosticEntry before = m_factory->create(record, context);
    EXPECT_FALSE(before.redactedContextSnapshot.has_value());

    // 挂接后＝脱敏快览（§4.2"脱敏后的开发级上下文快览……原文不保留"）。
    m_factory->attachRedactionService(m_red);
    const DiagnosticEntry after = m_factory->create(record, context);
    ASSERT_TRUE(after.redactedContextSnapshot.has_value());
    EXPECT_FALSE(contains(*after.redactedContextSnapshot, "secret"));
    EXPECT_FALSE(contains(*after.redactedContextSnapshot, "hunter2"));
    EXPECT_FALSE(contains(*after.redactedContextSnapshot, "USERPROFILE"));
    EXPECT_TRUE(contains(*after.redactedContextSnapshot, "D:\\data\\…\\arm.stl"));
    EXPECT_TRUE(contains(*after.redactedContextSnapshot, "[USER]"));
    EXPECT_TRUE(contains(*after.redactedContextSnapshot, "[REDACTED:credential:1]"));

    // 空参数＝无可快览内容，保持 nullopt（不伪造空串——core §4.8 口径）。
    // 用 0 参数码 RT-INPUT-INVALID（PRJ-LOCK-HELD schema 强制 2 参数——空
    // params 会在占位一致性校验被拒，走不到快照面）。
    const ObjectId subject2 = ObjectId::generate();
    const core::DiagnosticRecord record0 = core::DiagnosticRecord::make(
        "RT-INPUT-INVALID", subject2, std::string("joint_5"), std::string("Robot.joint_5"),
        std::string("评估路径输入非法"), std::string("启用 Must 条目取值越域"),
        std::string("修正输入后重新评估"));
    DiagContext emptyContext;
    emptyContext.sourceUnit = "runtime";
    emptyContext.sourceInterface = "compile.workcell";
    const DiagnosticEntry noParams = m_factory->create(record0, emptyContext);
    EXPECT_FALSE(noParams.redactedContextSnapshot.has_value());
}

TEST_F(RedactionFactory, TranslateMessageRedactedBeforeTruncation)
{
    // v0.4 口径④落地（§14.4 v0.9）：转译条目的异常消息先过 NFR-SEC-07 再
    // 走 512 字节截断——DT-REG-4 兜底条目的"安全摘要"语义自此完备。
    m_factory->attachRedactionService(m_red);
    // 未登记任何转译规则（空规则表）＋runtime_error 体系未注册→兜底路径
    // DIAG-REGISTRY-UNKNOWN-CODE（消息保留＋安全摘要——DT-REG-4 原语义面）。
    DiagContext context;
    context.sourceUnit = "runtime";
    context.sourceInterface = "compile.workcell";

    // 短消息：凭据脱敏、路径按策略（RootOnly）。
    const std::runtime_error shortErr("compile failed: password=hunter2 at D:\\x\\y\\z.stl");
    const DiagnosticEntry entry = m_factory->translate(shortErr, context);
    EXPECT_EQ(entry.record.code, "DIAG-REGISTRY-UNKNOWN-CODE");
    EXPECT_FALSE(contains(entry.record.cause, "hunter2")) << "异常消息凭据泄露";
    EXPECT_TRUE(contains(entry.record.cause, "[REDACTED:credential:1]"));
    EXPECT_TRUE(contains(entry.record.cause, "D:\\x\\…\\z.stl")) << "路径按 RootOnly 呈现";

    // 长消息：脱敏后再截断——既有 512 字节标注形态保持（DT-REG-4 零回归）。
    const std::runtime_error longErr("head password=leakme " + std::string(1000, 'y'));
    const DiagnosticEntry longEntry = m_factory->translate(longErr, context);
    EXPECT_FALSE(contains(longEntry.record.cause, "leakme")) << "凭据先于截断脱敏";
    EXPECT_TRUE(contains(longEntry.record.cause, "password=[REDACTED:credential:1]"));
    EXPECT_TRUE(contains(longEntry.record.cause, "已截断")) << "截断标注形态不变";
}

// =====================================================================
// 崩溃诊断文件（§7.6——AT-11 观测点"写出且脱敏"＋DT-LIFE-4 磁盘不足降级）
// =====================================================================

class CrashReportWrite : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_red = std::make_unique<RedactionService>();  // 默认 RootOnly
        m_sink = std::make_unique<CaptureSink>();
    }

    /// 组装覆盖全部六段§7.6 内容的输入（敏感内容铺满每个自由文本面）。
    CrashReportInput makeFullInput()
    {
        CrashReportInput in;
        in.processInfo = "RobWorkStudio 25.7 host=D:\\build\\private\\bin";
        in.processId = 4242;  // 固定 PID——文件名可断言（确定性）
        in.exceptionType = "std::runtime_error";
        in.exceptionMessage =
            "open failed: password=hunter2 at D:\\data\\secret\\model.stl";
        in.devLogTail = {"<1> D WARN test/sec msg=login password=abc123",
                         "<2> D WARN test/sec msg=load D:\\data\\secret\\mesh.stl"};
        in.activeTasks = {makeTaskIdentity()};
        in.activeFindings = {{FindingId::generate(), FindingState::Confirmed}};
        in.openProject = ProjectId::generate();
        return in;
    }

    TempDirGuard m_dir;
    ManualClock m_clock;
    SimpleFileOps m_ops;
    std::unique_ptr<RedactionService> m_red;
    std::unique_ptr<CaptureSink> m_sink;
};

TEST_F(CrashReportWrite, At11_WritesRedactedReportToDirectory)
{
    // 契约（AT-11 观测点）：崩溃诊断文件写出且脱敏（不含凭据类；路径按配
    // 置）——六段内容就位、身份内嵌逐值（P-DIAG-1 运行期半区）、位置＝指定
    // 目录（D-10：不入 .rwdesign——调用方给用户目录/临时目录）。
    CrashReportWriter writer(*m_red, m_clock, m_ops, m_sink.get());
    const CrashReportInput input = makeFullInput();
    const std::uint64_t writtenMs = m_clock.epochMs();

    std::optional<fs::path> written;
    EXPECT_NO_THROW({ written = writer.write(input, m_dir.path()); })
        << "崩溃写出路径绝不允许次生异常（noexcept 铁律）";
    ASSERT_TRUE(written.has_value());
    // 文件名口径（§14.4 v0.9）：crash-<epochMs>-<pid>.log。
    EXPECT_EQ(written->parent_path(), m_dir.path());
    EXPECT_TRUE(contains(written->filename().string(),
                         "crash-" + std::to_string(writtenMs) + "-4242.log"));

    const std::string text = readTextFile(*written);
    // 首行自描述（§1.4 键值文本；实现口径——findingDigest 自识别同惯例）。
    EXPECT_TRUE(contains(text, "format=ird-crash-report/1"));
    EXPECT_TRUE(contains(text, "writtenEpochMs=" + std::to_string(writtenMs)));
    EXPECT_TRUE(contains(text, "pid=4242"));
    // 脱敏面：凭据零命中、敏感路径段零命中（RootOnly 呈现）。
    EXPECT_FALSE(contains(text, "hunter2"));
    EXPECT_FALSE(contains(text, "abc123"));
    EXPECT_FALSE(contains(text, "private"));
    EXPECT_FALSE(contains(text, "secret"));
    EXPECT_TRUE(contains(text, "D:\\data\\…\\model.stl"));
    // 五段内容就位（§7.6 内容六段：进程/异常/任务/finding/项目/快照）。
    EXPECT_TRUE(contains(text, "process=RobWorkStudio 25.7 host=D:\\build\\…\\bin"));
    EXPECT_TRUE(contains(text, "exceptionType=std::runtime_error"));
    EXPECT_TRUE(contains(text, "exceptionMessage=open failed: "
                                   "password=[REDACTED:credential:1]"));
    EXPECT_TRUE(contains(text, "activeTasks=1"));
    // P-DIAG-1 运行期半区：core 契约身份逐值内嵌（canonical 逐字在案）。
    EXPECT_TRUE(contains(text, "task[0]=" + taskCanonical(input.activeTasks[0])));
    EXPECT_TRUE(contains(text, "finding[0]=" + input.activeFindings[0].id.toCanonical()
                                 + " state=confirmed"));
    ASSERT_TRUE(input.openProject.has_value());
    EXPECT_TRUE(contains(text, "project=" + input.openProject->toCanonical()));
    // 开发日志快照行内嵌且再脱敏（双保险）。
    EXPECT_TRUE(contains(text, "devLogTail=2"));
    EXPECT_TRUE(contains(text, "dev[0]=<1> D WARN test/sec msg=login "
                                   "password=[REDACTED:credential:1]"));
    EXPECT_TRUE(contains(text, "dev[1]=<2> D WARN test/sec msg=load D:\\data\\…\\mesh.stl"));
    // 无（崩溃文件不得引用未脱敏内容——递归检查一次性覆盖）。
    EXPECT_FALSE(contains(text, "D:\\data\\secret"));
    EXPECT_FALSE(contains(text, "D:\\build\\private"));
}

TEST_F(CrashReportWrite, ExceptionMessageTruncatedTo512Bytes)
{
    // §7.6"异常类型与消息摘要（截断 512 B）"——经 safeSummary（User 档脱敏
    // ＋UTF-8 边界截断＋[trunc] 标注）。
    CrashReportWriter writer(*m_red, m_clock, m_ops, m_sink.get());
    CrashReportInput input = makeFullInput();
    input.exceptionMessage = "head password=leakme ";
    input.exceptionMessage += std::string(2000, 'x');

    const std::optional<fs::path> written = writer.write(input, m_dir.path());
    ASSERT_TRUE(written.has_value());
    const std::string text = readTextFile(*written);

    const std::size_t begin = text.find("exceptionMessage=");
    ASSERT_NE(begin, std::string::npos);
    const std::size_t end = text.find('\n', begin);
    const std::string line = text.substr(begin, end - begin);
    // 512 字节截断以脱敏后文本计（键前缀除外）——上限＝512＋[trunc] 标注。
    const std::string value = line.substr(std::string("exceptionMessage=").size());
    EXPECT_LE(value.size(), 512u + std::string("[trunc]").size());
    EXPECT_TRUE(contains(value, "[trunc]")) << "超限事实告知（§7.2 同款约定）";
    EXPECT_FALSE(contains(value, "leakme")) << "凭据先于截断脱敏";
    EXPECT_TRUE(contains(value, "password=[REDACTED:credential:1]"));
}

TEST_F(CrashReportWrite, DtLife4_DiskFullDegradesWithoutThrowOrBlock)
{
    // 契约（DT-LIFE-4）：注入 disk-full→降级（Dev 诊断/stderr 摘要）；不崩
    // 溃、不阻塞调用方（观测点＝注入命中＋进程存活——函数如实返回）。
    FailingFileOps failingOps;
    CrashReportWriter writer(*m_red, m_clock, failingOps, m_sink.get());
    const CrashReportInput input = makeFullInput();

    std::optional<fs::path> written;
    EXPECT_NO_THROW({ written = writer.write(input, m_dir.path()); })
        << "磁盘满必须降级而非抛出（§7.6 写出失败→stderr 摘要＋放弃）";
    EXPECT_FALSE(written.has_value()) << "写失败如实报告（不带病返回成功）";

    // 降级面①：Dev 诊断一条（DIAG-LOG-WRITE-FAILED 复用＋crash 前缀——
    // §14.4 v0.9 口径 4；§7.6"不递归诊断"＝只此一条，不重试）。
    ASSERT_EQ(m_sink->rows.size(), 1u);
    EXPECT_EQ(m_sink->rows[0].first, "diag/logging");
    EXPECT_TRUE(contains(m_sink->rows[0].second, "[crash-report-write-failed]"));
    EXPECT_TRUE(contains(m_sink->rows[0].second, "DIAG-LOG-WRITE-FAILED"));

    // 降级面②：writer 可复用（不进入坏状态——再次写出尝试同样安全降级）。
    written.reset();
    EXPECT_NO_THROW({ written = writer.write(input, m_dir.path()); });
    EXPECT_FALSE(written.has_value());
    EXPECT_EQ(m_sink->rows.size(), 2u) << "每次写失败恰好一条降级诊断（不风暴）";
}

TEST_F(CrashReportWrite, DtLife4_OpsExceptionAlsoDegrades)
{
    // 注入轨二：接缝抛异常（ILogFileOps 契约允许——消化责任在使用方；
    // 崩溃路径上尤其不允许外溢）。
    ThrowingFileOps throwingOps;
    CrashReportWriter writer(*m_red, m_clock, throwingOps, m_sink.get());
    const CrashReportInput input = makeFullInput();

    std::optional<fs::path> written;
    EXPECT_NO_THROW({ written = writer.write(input, m_dir.path()); });
    EXPECT_FALSE(written.has_value());
    ASSERT_EQ(m_sink->rows.size(), 1u);
    EXPECT_TRUE(contains(m_sink->rows[0].second, "DIAG-LOG-WRITE-FAILED"));
}

}  // namespace
