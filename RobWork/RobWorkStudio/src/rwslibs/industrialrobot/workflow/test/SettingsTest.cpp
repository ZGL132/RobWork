/**
 * @file   SettingsTest.cpp
 * @brief  用户级设置存储的模型测试（WP-22-T10——units/workflow.md §7.7
 *         存储面的直调半区；WF-VER-221 设置分离持久化：用户设置入用户
 *         目录、无分析配置字段（I-WF-5）、损坏给诊断不崩溃）。
 *
 * 设计依据：
 *   - units/workflow.md §7.7（PM-14 持有内容四项——最近项目/包导出默认
 *     勾选/上次打开目录/快捷键绑定；"不入 .rwdesign；JSON canonical 经
 *     io"；"不含任何分析配置字段（I-WF-5）"）、§10.2（IUserSettingsStore
 *     签名——load 缺省默认值/损坏给诊断不崩溃，store 原子写出）、§10.3
 *     （UserSettings schema "user-settings/1"；调用方错误 fail-fast）、
 *     §11.2（WF-VER-221＝模型测试——设置分离持久化）、§14.3 P-WF-5
 *     （容量/脱敏未冻结——上限 10 实现冻结＋零脱敏安全默认＋留痕）
 *   - REQUIREMENTS.md §17 PM-14 原文（用户级设置不入 .rwdesign；求解
 *     配置等分析设置独立持久化（KIN-13）不混入本条）、KIN-13 原文
 *     （"独立于用户级设置（PM-14）持久化"——I-WF-5 的需求出处）
 *   - 任务契约 tasks/foundation/WP-22-T10.json acceptance 1~3 逐条
 *     （本文件全部用例真实执行——未执行测试不得标注通过）
 *
 * 测试形态（§11.0——模型测试＝直调计算库）：UserSettingsStore 的
 * load/store 直调（真实落盘临时目录——存储面本体即 IO 行为，落盘是
 * 被测语义的一部分；canonical 字节黄金串与损坏注入同文件承载）。
 * 经 IUserSettingsStore& 接口引用消费的用例钉扎接口路径（WP-20-T03
 * 首轮漏检教训——公共接口的每个公共方法至少一条经接口消费的用例）。
 *
 * 真实落盘与 canonical 字节的 io 通道交叉复核在契约测试
 * SettingsContractTest.cpp（io canonicalizeJson 逐字节比对＋Win32 锁
 * 注入的原子保留面）。
 *
 * 追溯约定（AGENTS.md §2.7）：用例名与断言注释标注需求/AT 编号。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯登记
#include <sdurws/ird/workflow/Settings.hpp>
#include <sdurws/ird/workflow/Types.hpp>  // WorkflowError（fail-fast 断言）

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <ios>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace sdurws::ird;
namespace fs = std::filesystem;
using workflow::HotkeyBindingRecord;
using workflow::IUserSettingsStore;
using workflow::PackageSelectionFlags;
using workflow::UserSettings;
using workflow::UserSettingsStore;
using workflow::WorkflowError;
namespace diag = sdurws::ird::diagnostics;  // 命名空间别名（using 别名不能指向命名空间——C2061）

// =====================================================================
// 开发日志替身（IDevLogSink 窄接口——记录通道与消息供断言；状态栏契约
// 测试 CapturingDevLog 同款形态的记录版）
// =====================================================================

struct RecordingDevLog final : diag::IDevLogSink {
    /// 已记录条目（channel, message）——append 序＝调用序。
    std::vector<std::pair<std::string, std::string>> entries;

    void logDev(std::string_view channel, std::string message) override
    {
        entries.emplace_back(std::string(channel), std::move(message));
    }
};

// =====================================================================
// 落盘夹具（每用例独占临时子目录——真实落盘隔离；RAII 清理）
// =====================================================================

class TempSettingsDir {
public:
    explicit TempSettingsDir(const char* tag)
    {
        // 临时根下按用例标签建独占子目录（ASCII 词面——规避 MSVC 窄串
        // 编码歧义，与 Lifecycle.cpp recentProjectKey 注同款纪律）。
        m_dir = fs::temp_directory_path() / ("ird-wf-settings-" + std::string(tag));
        fs::remove_all(m_dir);   // 残留防御（上次异常中断的清理兜底）
        fs::create_directories(m_dir);
    }
    ~TempSettingsDir() { fs::remove_all(m_dir); }

    TempSettingsDir(const TempSettingsDir&) = delete;
    TempSettingsDir& operator=(const TempSettingsDir&) = delete;

    /// 独占子目录根。
    const fs::path& dir() const noexcept { return m_dir; }
    /// 常用目标：目录内设置文件路径。
    fs::path settingsFile() const { return m_dir / "user-settings.json"; }

    /// 以字节写入文件（损坏注入面——绕过被测 store 直写盘面）。
    void writeFile(const fs::path& file, const std::string& bytes) const
    {
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(out.good()) << "夹具写文件失败：" << file.string();
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        ASSERT_TRUE(out.good()) << "夹具写文件内容失败";
    }

    /// 以字节读回文件（canonical 断言面）。
    std::string readFile(const fs::path& file) const
    {
        std::ifstream in(file, std::ios::binary);
        EXPECT_TRUE(in.good()) << "（readFile 前置）文件应存在：" << file.string();
        return std::string(std::istreambuf_iterator<char>(in),
                           std::istreambuf_iterator<char>());
    }

private:
    fs::path m_dir;
};

// =====================================================================
// 黄金 canonical 字节（io §5.9.3：2 空格缩进＋LF＋声明序键序＋UTF-8
// 无 BOM；空数组单 token "[]"；成员冒号后单空格；文档末尾以 LF 收束
// ——io 写出器在根对象闭括号后补 LF。改版面须 io.md §5.9.3 与本串
// 双向同步）
// =====================================================================

/// 缺省设置（四键，无勾选记忆键——nullopt 不写键）的黄金字节。
constexpr const char* kGoldenDefaultBytes =
    "{\n"
    "  \"schemaVersion\": 1,\n"
    "  \"recentProjects\": [],\n"
    "  \"lastOpenDirectory\": \"\",\n"
    "  \"hotkeyBindings\": []\n"
    "}\n";

/// 全字段设置（两条最近项目＋勾选记忆＋目录＋一条改绑）的黄金字节。
/// 缩进推演：根成员 2 空格；packageExportSelection 子成员 4 空格；
/// hotkeyBindings 数组项 4 空格、条目对象成员 6 空格、条目闭括号 4 空格。
constexpr const char* kGoldenFullBytes =
    "{\n"
    "  \"schemaVersion\": 1,\n"
    "  \"recentProjects\": [\n"
    "    \"D:/projects/alpha\",\n"
    "    \"D:/projects/beta\"\n"
    "  ],\n"
    "  \"packageExportSelection\": {\n"
    "    \"includeResults\": true,\n"
    "    \"includeReports\": false,\n"
    "    \"includeDrafts\": true\n"
    "  },\n"
    "  \"lastOpenDirectory\": \"D:/projects\",\n"
    "  \"hotkeyBindings\": [\n"
    "    {\n"
    "      \"commandId\": \"wf.example.cmd\",\n"
    "      \"key\": \"Ctrl+Alt+E\"\n"
    "    }\n"
    "  ]\n"
    "}\n";

// =====================================================================
// 夹具构造（设置值的可复用 builder——用例只写差异面）
// =====================================================================

/// 全字段设置值（与 kGoldenFullBytes 一一对应——canonical 黄金的内存侧）。
UserSettings fullSettings()
{
    UserSettings s;
    s.recentProjects = {"D:/projects/alpha", "D:/projects/beta"};
    PackageSelectionFlags flags;
    flags.includeResults = true;
    flags.includeReports = false;   // 非缺省值——钉"记忆了部分取消勾选"
    flags.includeDrafts = true;
    s.packageExportSelection = flags;
    s.lastOpenDirectory = "D:/projects";
    s.hotkeyBindings = {{HotkeyBindingRecord{"wf.example.cmd", "Ctrl+Alt+E"}}};
    return s;
}

/// 缺省设置值（与 kGoldenDefaultBytes 对应——UserSettings 默认构造）。
UserSettings defaultSettings()
{
    return UserSettings{};
}

// =====================================================================
// 用例（WfSettings 组——WF-VER-221 全语义）
// =====================================================================

// ---------------------------------------------------------------------
// 首次使用：文件不存在 → 缺省默认值＋零诊断（PM-14"缺省默认值"半区；
// 损坏诊断只在真损坏时出现——缺文件不是错误）。
// ---------------------------------------------------------------------
TEST(WfSettings, MissingFileYieldsDefaultsWithoutDiagnostic)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-14"}, std::vector<std::string>{"WF-VER-221"});

    TempSettingsDir dir("missing");
    RecordingDevLog devLog;
    const UserSettingsStore store(dir.settingsFile(), &devLog);

    // 期望：load 返回缺省默认值（四字段空态）且开发日志零条目。
    const UserSettings loaded = store.load();
    EXPECT_EQ(loaded, defaultSettings());
    EXPECT_TRUE(loaded.recentProjects.empty());
    EXPECT_FALSE(loaded.packageExportSelection.has_value());
    EXPECT_TRUE(loaded.lastOpenDirectory.empty());
    EXPECT_TRUE(loaded.hotkeyBindings.empty());
    EXPECT_TRUE(devLog.entries.empty())
        << "文件不存在＝首次使用，不得产生诊断（缺文件非错误）";
}

// ---------------------------------------------------------------------
// 回路：全字段 store→load 一致（经 IUserSettingsStore& 接口引用消费
// ——接口路径钉扎：WP-20-T03 首轮漏检教训的常设对正面）。
// ---------------------------------------------------------------------
TEST(WfSettings, RoundTripAllFieldsViaInterface)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-14"}, std::vector<std::string>{"WF-VER-221"});

    TempSettingsDir dir("roundtrip");
    RecordingDevLog devLog;
    UserSettingsStore impl(dir.settingsFile(), &devLog);

    // ★ 接口消费：以基类引用调用——虚分派路径（非具体类直调）。
    IUserSettingsStore& store = impl;
    // 全字段值（非常量——解绑登记条目在此追加后一并写入）。
    UserSettings original = fullSettings();

    // 解绑登记（key 空串）同批钉住——空串词形必须原样往返（不持久化
    // 解绑会令默认键复活，违背用户意图——HotkeyBindingRecord 注）。
    original.hotkeyBindings.push_back(HotkeyBindingRecord{"wf.unbound.cmd", ""});

    store.store(original);
    const UserSettings loaded = store.load();
    EXPECT_EQ(loaded, original) << "全字段回路一致（含解绑登记条目）";
    ASSERT_EQ(loaded.hotkeyBindings.size(), std::size_t{2});
    EXPECT_EQ(loaded.hotkeyBindings[1].keyPortableText, std::string{});
}

// ---------------------------------------------------------------------
// canonical 黄金字节：缺省设置写出（PM-14"JSON canonical 经 io 写出"
// ——声明序键序＋2 空格缩进＋LF＋无 BOM 的逐字节黄金；nullopt 不产
// packageExportSelection 键）。
// ---------------------------------------------------------------------
TEST(WfSettings, StoredFileIsCanonicalGoldenBytes_Defaults)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-14"}, std::vector<std::string>{"WF-VER-221"});

    TempSettingsDir dir("golden-default");
    UserSettingsStore store(dir.settingsFile());
    store.store(defaultSettings());

    const std::string bytes = dir.readFile(dir.settingsFile());
    EXPECT_EQ(bytes, std::string(kGoldenDefaultBytes))
        << "缺省设置 canonical 黄金字节逐字节一致（LF 行尾、声明序、末尾 LF 收束）";
    // UTF-8 无 BOM 的字面复核（EF BB BF 前缀不得出现——io §5.9.3）。
    ASSERT_GE(bytes.size(), std::size_t{2});
    EXPECT_NE(static_cast<unsigned char>(bytes[0]), 0xEF)
        << "canonical 字节无 BOM（EF BB BF 前缀禁现）";
}

// ---------------------------------------------------------------------
// canonical 黄金字节：全字段设置写出（可选键在位/数组折行/条目缩进
// ——版面黄金的"满配"半区）。
// ---------------------------------------------------------------------
TEST(WfSettings, StoredFileIsCanonicalGoldenBytes_FullFields)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-14"}, std::vector<std::string>{"WF-VER-221"});

    TempSettingsDir dir("golden-full");
    UserSettingsStore store(dir.settingsFile());
    store.store(fullSettings());

    const std::string bytes = dir.readFile(dir.settingsFile());
    EXPECT_EQ(bytes, std::string(kGoldenFullBytes))
        << "全字段 canonical 黄金字节逐字节一致";
}

// ---------------------------------------------------------------------
// 损坏安全（垃圾字节）：默认值＋Dev 诊断一条（PM-14/acceptance 3
// "设置文件损坏给诊断不崩溃"的核心半区——load 零异常零崩溃）。
// ---------------------------------------------------------------------
TEST(WfSettings, CorruptedGarbageYieldsDefaultsWithDiagnostic)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-14"}, std::vector<std::string>{"WF-VER-221"});

    TempSettingsDir dir("garbage");
    RecordingDevLog devLog;
    dir.writeFile(dir.settingsFile(), "\x00\x01garbage not json\xff\xfe");
    const UserSettingsStore store(dir.settingsFile(), &devLog);

    // 不崩溃（无异常逃逸——本语句若抛即用例失败）＋默认值＋诊断一条。
    const UserSettings loaded = store.load();
    EXPECT_EQ(loaded, defaultSettings());
    ASSERT_EQ(devLog.entries.size(), std::size_t{1})
        << "损坏必须给诊断（留痕非静默）";
    EXPECT_EQ(devLog.entries[0].first, std::string{"workflow/user-settings"})
        << "诊断通道词形 workflow/user-settings（diagnostics §7.2 LogChannel）";
    EXPECT_FALSE(devLog.entries[0].second.empty()) << "诊断消息非空（可定位）";
}

// ---------------------------------------------------------------------
// 损坏安全（变体矩阵）：未知键/schemaVersion 未来/必填缺失/类型错/
// 重复键——五变体全部默认值＋诊断（版本判定先于 schema、未知键 Reject、
// 必填不注入默认值——io §5.9.2 各纪律经设置通道的逐项兑现）。
// ---------------------------------------------------------------------
TEST(WfSettings, CorruptedVariantsAllSafe)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-14"}, std::vector<std::string>{"WF-VER-221"});

    // 变体注入表（每项＝一份损坏文件字节＋定位说明）：
    //   ①未知键：未来字段混入（schema 演进未升版——NFR-DEP-04 拒绝面）
    //   ②未来版本：schemaVersion=2（写入方版本高于本实现——FUTURE）
    //   ③必填缺失：lastOpenDirectory 缺失（REQUIRED——不注入默认值）
    //   ④类型错：recentProjects 为字符串而非数组（TYPE）
    //   ⑤重复键：schemaVersion 出现两次（DUPKEY——受限 DOM 拒绝）
    const std::string variants[] = {
        R"({"schemaVersion": 1, "recentProjects": [], "lastOpenDirectory": "",)"
        R"("hotkeyBindings": [], "unknownFutureKey": true})",
        R"({"schemaVersion": 2, "recentProjects": [], "lastOpenDirectory": "",)"
        R"("hotkeyBindings": []})",
        R"({"schemaVersion": 1, "recentProjects": [], "hotkeyBindings": []})",
        R"({"schemaVersion": 1, "recentProjects": "not-array", "lastOpenDirectory": "",)"
        R"("hotkeyBindings": []})",
        R"({"schemaVersion": 1, "schemaVersion": 1, "recentProjects": [],)"
        R"("lastOpenDirectory": "", "hotkeyBindings": []})",
    };

    for (std::size_t i = 0; i < 5; ++i) {
        TempSettingsDir dir("variant");
        const fs::path file = dir.settingsFile();
        dir.writeFile(file, variants[i]);

        RecordingDevLog devLog;
        const UserSettingsStore store(file, &devLog);
        const UserSettings loaded = store.load();
        EXPECT_EQ(loaded, defaultSettings())
            << "损坏变体 #" << i << " 必须回退缺省默认值（不崩溃不部分采用）";
        EXPECT_EQ(devLog.entries.size(), std::size_t{1})
            << "损坏变体 #" << i << " 必须给诊断一条（留痕非静默）";
    }
}

// ---------------------------------------------------------------------
// I-WF-5（acceptance 2）：注入含 KIN-13 分析配置词形字段的设置文件——
// 未知键策略必须拒绝（分析配置字段无论从哪一侧都进不了本存储；
// 求解配置独立持久化不混入本条——PM-14 原文）。
// ---------------------------------------------------------------------
TEST(WfSettings, AnalysisConfigFieldRejected_IWF5)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-14", "KIN-13"},
                  std::vector<std::string>{"WF-VER-221"});

    TempSettingsDir dir("iwf5");
    // 混入两个 KIN-13 词形（求解迭代上限/采样种子——REQUIREMENTS KIN-13
    // 字段词族）＋其余字段合法——期望整体拒绝（未知键 Reject），回退
    // 默认值，而非忽略未知键采用其余字段（吞字段＝静默混入，禁止）。
    dir.writeFile(dir.settingsFile(),
                  R"({"schemaVersion": 1, "recentProjects": [],)"
                  R"("iterationLimit": 500, "samplingSeed": 42,)"
                  R"("lastOpenDirectory": "", "hotkeyBindings": []})");

    RecordingDevLog devLog;
    const UserSettingsStore store(dir.settingsFile(), &devLog);
    const UserSettings loaded = store.load();
    EXPECT_EQ(loaded, defaultSettings())
        << "分析配置字段混入必须整体拒绝（I-WF-5——零分析配置字段）";
    EXPECT_EQ(devLog.entries.size(), std::size_t{1})
        << "拒绝须给诊断（不静默吞字段）";
}

// ---------------------------------------------------------------------
// I-WF-5 正向：store 产物的顶层键集**精确等于**冻结五键（schema 本体
// 零分析配置字段——写侧封闭性的字面证明）。
// ---------------------------------------------------------------------
TEST(WfSettings, TopLevelKeySetFrozen_IWF5)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-14", "KIN-13"},
                  std::vector<std::string>{"WF-VER-221"});

    TempSettingsDir dir("keyset");
    UserSettingsStore store(dir.settingsFile());
    store.store(fullSettings());

    // 纯文本键名提取（黄金字节用例已钉精确版面——此处只清点键集）。
    const std::string bytes = dir.readFile(dir.settingsFile());
    const char* frozenKeys[] = {
        "\"schemaVersion\":",
        "\"recentProjects\":",
        "\"packageExportSelection\":",
        "\"lastOpenDirectory\":",
        "\"hotkeyBindings\":",
    };
    for (const char* key : frozenKeys) {
        EXPECT_NE(bytes.find(key), std::string::npos)
            << "冻结键应在位：" << key;
    }
    // KIN-13 词形族抽样禁现（写侧零分析配置字段的负向复核）。
    const char* forbiddenTokens[] = {
        "iteration", "tolerance", "seed", "budget", "initial", "dedup",
    };
    for (const char* token : forbiddenTokens) {
        EXPECT_EQ(bytes.find(token), std::string::npos)
            << "设置 schema 不得含分析配置词形：" << token;
    }
}

// ---------------------------------------------------------------------
// P-WF-5（acceptance 3）：持久化列表超需求冻结容量 → 截断保留最近
// 10 条（LRU 头部——与溢出淘汰同语义的安全默认）＋诊断留痕。
// ---------------------------------------------------------------------
TEST(WfSettings, RecentProjectsTruncatedToCapacityWithDiagnostic_PWF5)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-14"}, std::vector<std::string>{"P-WF-5"});

    TempSettingsDir dir("capacity");
    // 直写盘面注入 12 条（绕过会话面——持久化值越界的注入点）。
    std::string json = R"({"schemaVersion": 1, "recentProjects": [)";
    for (int i = 1; i <= 12; ++i) {
        json += (i > 1 ? ", " : "");
        json += "\"D:/p/proj-" + std::to_string(i) + "\"";
    }
    json += R"(], "lastOpenDirectory": "", "hotkeyBindings": []})";
    dir.writeFile(dir.settingsFile(), json);

    RecordingDevLog devLog;
    const UserSettingsStore store(dir.settingsFile(), &devLog);
    const UserSettings loaded = store.load();

    // 上限＝PM-10 冻结值 10（kRecentProjectsCapacity 同源；不发明第二容量）。
    ASSERT_EQ(loaded.recentProjects.size(), workflow::kRecentProjectsCapacity);
    // 头部保留＝最近使用优先（proj-1..10 留、proj-11/12 淘汰——LRU 序）。
    EXPECT_EQ(loaded.recentProjects.front(), "D:/p/proj-1");
    EXPECT_EQ(loaded.recentProjects.back(), "D:/p/proj-10");
    // 截断事实留痕（安全默认不静默——acceptance 3"按安全默认＋留痕"）。
    ASSERT_EQ(devLog.entries.size(), std::size_t{1});
    EXPECT_NE(devLog.entries[0].second.find("10"), std::string::npos)
        << "截断诊断应携带容量值 10";
}

// ---------------------------------------------------------------------
// P-WF-5：路径零脱敏（原样记录——含多字节字符与混合分隔符的路径词面
// round-trip 不变；脱敏配置未冻结期间不发明行为）。
// ---------------------------------------------------------------------
TEST(WfSettings, PathsVerbatimNoRedaction_PWF5)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-14"}, std::vector<std::string>{"P-WF-5"});

    TempSettingsDir dir("verbatim");
    RecordingDevLog devLog;
    UserSettingsStore impl(dir.settingsFile(), &devLog);
    IUserSettingsStore& store = impl;  // 接口消费（回路半区同钉）

    UserSettings s;
    // 多字节字符＋正反斜杠混合——词面原样（存储面不规范化不脱敏；
    // 规范化归 RecentProjectsService::record 的 recentProjectKey）。
    s.recentProjects = {"D:/项目/焊接线A", "D:\\backup\\legacy\\proj"};
    s.lastOpenDirectory = "D:/项目";
    store.store(s);

    const UserSettings loaded = store.load();
    EXPECT_EQ(loaded.recentProjects, s.recentProjects)
        << "路径词面原样（零脱敏零转义改写——P-WF-5 安全默认）";
    EXPECT_EQ(loaded.lastOpenDirectory, s.lastOpenDirectory);
}

// ---------------------------------------------------------------------
// 勾选记忆三态：nullopt（无记忆）/全 false（显式记忆取消勾选）/部分
// 值——load 侧三态区分（nullopt 与"记忆了全选/全不选"是不同事实；
// PM-05 记忆默认的存储半区）。
// ---------------------------------------------------------------------
TEST(WfSettings, SelectionMemoryTriState)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-14"}, std::vector<std::string>{"WF-VER-221"});

    TempSettingsDir dir("tri-state");
    UserSettingsStore store(dir.settingsFile());

    // 态 1：无记忆（nullopt）——文件里无 packageExportSelection 键。
    {
        UserSettings s;
        s.packageExportSelection = std::nullopt;
        store.store(s);
        const UserSettings loaded = store.load();
        EXPECT_FALSE(loaded.packageExportSelection.has_value())
            << "键缺失＝无记忆（nullopt——defaultSelectionOf 届时回全选缺省）";
    }
    // 态 2：显式记忆全 false（与 nullopt 可区分——全不选也是用户决策）。
    {
        UserSettings s;
        PackageSelectionFlags flags;
        flags.includeResults = false;
        flags.includeReports = false;
        flags.includeDrafts = false;
        s.packageExportSelection = flags;
        store.store(s);
        const UserSettings loaded = store.load();
        ASSERT_TRUE(loaded.packageExportSelection.has_value())
            << "显式记忆不得塌缩为无记忆";
        EXPECT_FALSE(loaded.packageExportSelection->includeResults);
        EXPECT_FALSE(loaded.packageExportSelection->includeReports);
        EXPECT_FALSE(loaded.packageExportSelection->includeDrafts);
    }
    // 态 3：部分勾选（true/false/true——逐位保真）。
    {
        UserSettings s = fullSettings();  // includeReports=false 其余 true
        store.store(s);
        const UserSettings loaded = store.load();
        ASSERT_TRUE(loaded.packageExportSelection.has_value());
        EXPECT_TRUE(loaded.packageExportSelection->includeResults);
        EXPECT_FALSE(loaded.packageExportSelection->includeReports);
        EXPECT_TRUE(loaded.packageExportSelection->includeDrafts);
    }
}

// ---------------------------------------------------------------------
// 父目录自持：目标文件父目录链不存在 → store 建齐后写出成功（缺省
// 工厂深路径 %APPDATA%/RobWork/industrialrobot/ 首次写入的可用性面）。
// ---------------------------------------------------------------------
TEST(WfSettings, StoreCreatesMissingParentDirectories)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-14"}, std::vector<std::string>{"WF-VER-221"});

    TempSettingsDir dir("deep");
    const fs::path file = dir.dir() / "RobWork" / "industrialrobot" / "user-settings.json";
    ASSERT_FALSE(fs::exists(file.parent_path())) << "前置：父目录链不存在";

    UserSettingsStore store(file);
    store.store(fullSettings());
    EXPECT_TRUE(fs::exists(file)) << "store 后设置文件就位（父目录已自持建齐）";
    EXPECT_EQ(store.load(), fullSettings());
}

// ---------------------------------------------------------------------
// store 失败零吞错（§10.3 环境错误上抛）：目标路径是一个**目录**——
// io 写出的替换步必败（Windows 同名目录占位），期望 WorkflowError
// （io 码词形透传）而非静默返回。
// ---------------------------------------------------------------------
TEST(WfSettings, StoreFailureThrowsWorkflowError)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-14"}, std::vector<std::string>{"WF-VER-221"});

    TempSettingsDir dir("fail");
    // 目标"文件"实为目录——io 暂存可成、rename 到目录名必败。
    const fs::path target = dir.settingsFile();
    fs::create_directories(target);

    UserSettingsStore store(target);
    RecordingDevLog devLog;  // 失败诊断面（构造后注入不可变——本用例以
                             // 无 devLog 构造再断言异常，诊断留痕在
                             // WriteFailure 契约用例复核）
    (void)devLog;
    EXPECT_THROW(
        try {
            store.store(fullSettings());
        } catch (const WorkflowError& e) {
            // 异常消息透传 io 码词形（对端零加工——非裸"写出失败"）。
            EXPECT_NE(std::string(e.what()).find("IO-"), std::string::npos)
                << "异常应携带 io 稳定码词形：" << e.what();
            throw;
        },
        WorkflowError);
}

// ---------------------------------------------------------------------
// 调用方错误 fail-fast：空文件路径构造即 WorkflowError（无文件可绑定
// 的存储没有存在意义——不静默落到当前目录）。
// ---------------------------------------------------------------------
TEST(WfSettings, EmptyPathConstructorFailsFast)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-14"}, std::vector<std::string>{"WF-VER-221"});

    EXPECT_THROW(UserSettingsStore(fs::path{}), WorkflowError);
}

// ---------------------------------------------------------------------
// 缺省工厂：环境变量可得时路径非空且以 user-settings.json 结尾（生产
// 装配缺省绑定的可用性面；两变量皆无的空路径语义已在构造 fail-fast
// 半区钉住）。
// ---------------------------------------------------------------------
TEST(WfSettings, DefaultFilePathPrefersUserDirectory)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-14"}, std::vector<std::string>{"WF-VER-221"});

    const fs::path p = workflow::defaultUserSettingsFilePath();
    if (const bool hasEnv = std::getenv("APPDATA") != nullptr
                            || std::getenv("HOME") != nullptr;
        hasEnv) {
        EXPECT_FALSE(p.empty()) << "环境变量可得时缺省路径非空";
        EXPECT_EQ(p.filename().string(), std::string{"user-settings.json"})
            << "缺省文件名词形（用户目录下的设置文件）";
    } else {
        EXPECT_TRUE(p.empty())
            << "两环境变量皆无时返回空路径（装配层注入显式路径——不静默落当前目录）";
    }
    // 两平台词形均含产品自有目录段（与 .rwdesign 项目目录零交集——
    // PM-14"不入 .rwdesign"的路径面佐证；结构保证＝签名零 store 参数。
    // 注：目录段词形为 industrialrobot——产品面源码零 RobWork 字面量
    // 是 R-4 红线扫描的登记口径，目录段命名同守该口径）。
    if (!p.empty()) {
        const std::string text = p.string();
        EXPECT_NE(text.find("industrialrobot"), std::string::npos)
            << "缺省路径含产品自有目录段";
    }
}

}  // namespace
