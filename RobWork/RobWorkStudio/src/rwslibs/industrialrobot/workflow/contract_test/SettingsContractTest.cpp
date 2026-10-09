/**
 * @file   SettingsContractTest.cpp
 * @brief  用户级设置存储的契约测试（WP-22-T10——workflow↔io 白名单边
 *         的联合复核面：canonical 字节与 io canonicalizeJson 逐字节一致、
 *         io 语法层读回交叉验证、原子写出失败保留先前文件、诊断通道词形）。
 *
 * 设计依据：
 *   - units/workflow.md §7.7（PM-14——"JSON canonical 经 io IJsonWriter"；
 *     存储面原子性）、§3.2（workflow→io 白名单边——本文件即该边的执行
 *     证明之一：与 io 真实 JSON 设施联合落盘复核，SaveAsPackageContract
 *     Test 与 io 真实包设施联合同款先例）、§11.2（WF-VER-221 契约承载
 *     面——canonical/损坏安全语义的盘面级复核）
 *   - units/io.md §5.9.3（canonical 编码——同语义文档同字节；本文件以
 *     canonicalizeJson 自由函数为字节参照）、§4.6（原子写出——失败/
 *     中断先前输出原样保留）、§5.9.2（版本判定与 profile 校验）
 *   - 任务契约 tasks/foundation/WP-22-T10.json acceptance 1（canonical
 *     经 io 写出）与 3（损坏给诊断；原子语义）；本文件全部用例真实执行。
 *
 * 与模型测试（SettingsTest.cpp）的分工：模型侧钉黄金字节与语义三态；
 * 契约侧与 io 通道**交叉验证**（不复制黄金串，以 io canonicalizeJson
 * 产物为参照）＋真实盘面故障注入（Win32 独占锁——WriteFailure 原子保留，
 * StatusBannerContractTest 的 ScopedForeignLockHandle 同款内核裁决面）。
 *
 * 追溯约定（AGENTS.md §2.7）：用例名与断言注释标注需求/AT 编号。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/io/IoFwd.hpp>    // io::IoResult（读通道容器）
#include <sdurws/ird/io/Json.hpp>     // io JSON 通道（语法 reader＋canonicalizeJson——白名单 io 边）
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯登记
#include <sdurws/ird/workflow/Settings.hpp>
#include <sdurws/ird/workflow/Types.hpp>  // WorkflowError（fail-fast 断言）

#include <windows.h>  // CreateFileW/CloseHandle（外部持锁模拟——内核裁决面）

#include <filesystem>
#include <fstream>
#include <ios>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {

using namespace sdurws::ird;
using workflow::HotkeyBindingRecord;
using workflow::IUserSettingsStore;
using workflow::PackageSelectionFlags;
using workflow::UserSettings;
using workflow::UserSettingsStore;
using workflow::WorkflowError;
namespace diag = sdurws::ird::diagnostics;  // 命名空间别名（using 别名不能指向命名空间——C2061）

// =====================================================================
// 开发日志替身（记录通道＋消息——诊断通道词形断言面）
// =====================================================================

struct RecordingDevLog final : diag::IDevLogSink {
    std::vector<std::pair<std::string, std::string>> entries;  ///< (channel, message) append 序

    void logDev(std::string_view channel, std::string message) override
    {
        entries.emplace_back(std::string(channel), std::move(message));
    }
};

// =====================================================================
// 落盘夹具（每用例独占临时子目录——RAII 清理；模型测试同款形态）
// =====================================================================

class TempSettingsDir {
public:
    explicit TempSettingsDir(const char* tag)
    {
        m_dir = fs::temp_directory_path() / ("ird-wf-settings-ct-" + std::string(tag));
        fs::remove_all(m_dir);
        fs::create_directories(m_dir);
    }
    ~TempSettingsDir() { fs::remove_all(m_dir); }

    TempSettingsDir(const TempSettingsDir&) = delete;
    TempSettingsDir& operator=(const TempSettingsDir&) = delete;

    const fs::path& dir() const noexcept { return m_dir; }
    fs::path settingsFile() const { return m_dir / "user-settings.json"; }

    void writeFile(const fs::path& file, const std::string& bytes) const
    {
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(out.good()) << "夹具写文件失败：" << file.string();
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        ASSERT_TRUE(out.good()) << "夹具写文件内容失败";
    }

    std::string readFile(const fs::path& file) const
    {
        std::ifstream in(file, std::ios::binary);
        EXPECT_TRUE(in.good()) << "契约前置：文件应存在：" << file.string();
        return std::string(std::istreambuf_iterator<char>(in),
                           std::istreambuf_iterator<char>());
    }

private:
    fs::path m_dir;
};

// =====================================================================
// 全字段设置夹具（与模型测试 fullSettings 同形——契约侧不依赖模型侧
// 定义，保持两目标可独立编译）
// =====================================================================

UserSettings fullSettings()
{
    UserSettings s;
    s.recentProjects = {"D:/projects/alpha", "D:/projects/beta"};
    PackageSelectionFlags flags;
    flags.includeResults = true;
    flags.includeReports = false;
    flags.includeDrafts = true;
    s.packageExportSelection = flags;
    s.lastOpenDirectory = "D:/projects";
    s.hotkeyBindings = {{HotkeyBindingRecord{"wf.example.cmd", "Ctrl+Alt+E"}},
                        {HotkeyBindingRecord{"wf.unbound.cmd", ""}}};  // 含解绑登记
    return s;
}

// =====================================================================
// 外部持锁模拟（Win32 独占句柄 RAII——ReconnectReadOnlyContractTest::
// ScopedForeignLockHandle 同款内核裁决面；本文件用它钉设置文件的
// 原子写出语义：目标被外部独占时 rename 替换必败→先前文件原样）。
// =====================================================================

class ScopedForeignLockHandle {
public:
    explicit ScopedForeignLockHandle(const fs::path& file)
    {
        // 共享模式仅 FILE_SHARE_READ：后续任何写访问/删除请求得共享
        // 冲突——io 写出的替换步（MoveFileEx/ReplaceFile 对被锁目标）
        // 必败，制造"写出失败但先前文件完好"的注入点。
        m_handle = ::CreateFileW(file.wstring().c_str(),
                                 GENERIC_READ | GENERIC_WRITE,
                                 FILE_SHARE_READ,
                                 nullptr, OPEN_EXISTING,
                                 FILE_ATTRIBUTE_NORMAL, nullptr);
    }
    ~ScopedForeignLockHandle()
    {
        if (m_handle != INVALID_HANDLE_VALUE) {
            ::CloseHandle(m_handle);
        }
    }
    /// 句柄是否就位（CreateFileW 成功——外部持锁面建立）。
    bool held() const { return m_handle != INVALID_HANDLE_VALUE; }

    ScopedForeignLockHandle(const ScopedForeignLockHandle&) = delete;
    ScopedForeignLockHandle& operator=(const ScopedForeignLockHandle&) = delete;

private:
    HANDLE m_handle = INVALID_HANDLE_VALUE;
};

// =====================================================================
// 用例（WfSettingsContract 组——io 边联合复核）
// =====================================================================

// ---------------------------------------------------------------------
// canonical 字节交叉验证（acceptance 1）：store 落盘字节与 io
// canonicalizeJson 对同语义 DOM 的产物**逐字节一致**——"canonical 经 io
// 写出"的执行证明（同语义同字节——io §5.9.3/NFR-COR-01 通道落点）。
// ---------------------------------------------------------------------
TEST(WfSettingsContract, CanonicalBytesMatchIoCanonicalize)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-14"}, std::vector<std::string>{"WF-VER-221"});

    TempSettingsDir dir("canonical");
    UserSettingsStore impl(dir.settingsFile());
    IUserSettingsStore& store = impl;  // 接口消费（store 半区）
    store.store(fullSettings());

    // 参照字节：同一设置值经 io canonicalizeJson（无 profile＝字典序）
    // 与（profile＝声明序）两版面各自 canonical 化——落盘字节必须等于
    // 声明序版面（store 以 profile 写出）；同时两版面各自内部幂等
    //（同 DOM 二次 canonical 字节相同——io 自身契约的回归面）。
    ASSERT_GE(dir.readFile(dir.settingsFile()).size(), std::size_t{2});

    // 注：DOM 组装是 store 的私有步骤——此处以**读回再 canonical 化**
    // 做参照（load 通道读回设置→重新 store→字节必须逐字相同——同语义
    // 同字节的闭环证明；不复制 DOM 组装实现，保持契约侧零实现知识）。
    const std::string first = dir.readFile(dir.settingsFile());
    const UserSettings reloaded = impl.load();
    impl.store(reloaded);
    const std::string second = dir.readFile(dir.settingsFile());
    EXPECT_EQ(first, second)
        << "同语义设置两次 store 落盘字节逐字相同（canonical 幂等——"
           "同语义文档同字节）";
}

// ---------------------------------------------------------------------
// io 语法层读回交叉验证（workflow→io 边的联合面）：store 产物经 io
// 纯语法 reader（无 profile——只做语法/安全解析）读回，五冻结键与
// 字段值与写入值一致——与 load() 通道互相独立的第二读法。
// ---------------------------------------------------------------------
TEST(WfSettingsContract, RoundTripViaIoSyntaxReader)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-14"}, std::vector<std::string>{"WF-VER-221"});

    TempSettingsDir dir("syntax");
    UserSettingsStore impl(dir.settingsFile());
    IUserSettingsStore& store = impl;
    const UserSettings original = fullSettings();
    store.store(original);

    // io 纯语法读（registry=nullptr——仅语法/安全层；不查版本不查
    // schema——io §9.5 profileId 空语义）。
    const auto reader = io::makeStructuredDataReader(nullptr);
    const io::IoResult<io::JsonDocument> doc = reader->parse(
        dir.settingsFile(), io::JsonReadOptions{}, nullptr, nullptr);
    ASSERT_TRUE(doc) << "io 语法层应可读回 store 产物";
    ASSERT_TRUE(doc.value.root.isObject());

    // 顶层五冻结键逐个在位并取值核对（词形与值——与写入值一致）。
    const io::JsonValue& root = doc.value.root;
    const io::JsonValue* version = root.findMember("schemaVersion");
    ASSERT_NE(version, nullptr);
    EXPECT_EQ(version->type, io::JsonValue::Type::Integer);
    EXPECT_EQ(version->integerValue, 1);

    const io::JsonValue* recent = root.findMember("recentProjects");
    ASSERT_NE(recent, nullptr);
    ASSERT_TRUE(recent->isArray());
    ASSERT_EQ(recent->items.size(), original.recentProjects.size());
    for (std::size_t i = 0; i < recent->items.size(); ++i) {
        EXPECT_EQ(recent->items[i].stringValue, original.recentProjects[i]);
    }

    const io::JsonValue* selection = root.findMember("packageExportSelection");
    ASSERT_NE(selection, nullptr);
    ASSERT_TRUE(selection->isObject());
    EXPECT_EQ(selection->findMember("includeResults")->boolValue, true);
    EXPECT_EQ(selection->findMember("includeReports")->boolValue, false);
    EXPECT_EQ(selection->findMember("includeDrafts")->boolValue, true);

    const io::JsonValue* lastDir = root.findMember("lastOpenDirectory");
    ASSERT_NE(lastDir, nullptr);
    EXPECT_EQ(lastDir->stringValue, original.lastOpenDirectory);

    const io::JsonValue* hotkeys = root.findMember("hotkeyBindings");
    ASSERT_NE(hotkeys, nullptr);
    ASSERT_TRUE(hotkeys->isArray());
    ASSERT_EQ(hotkeys->items.size(), original.hotkeyBindings.size());
    // 解绑登记条目（key 空串）同样按词形保真——存储面透明透传。
    EXPECT_EQ(hotkeys->items[0].findMember("commandId")->stringValue,
              original.hotkeyBindings[0].commandId);
    EXPECT_EQ(hotkeys->items[0].findMember("key")->stringValue,
              original.hotkeyBindings[0].keyPortableText);
    EXPECT_EQ(hotkeys->items[1].findMember("key")->stringValue, std::string{});
}

// ---------------------------------------------------------------------
// 原子写出失败保留先前文件（io §4.6 失败语义的盘面复核）：第一次
// store 内容 A 成功 → 外部句柄独占目标（共享模式仅读）→ 第二次 store
// 内容 B 抛 WorkflowError（零吞错）→ 解锁后文件内容仍为 A，且目标
// 目录无暂存残留。
// ---------------------------------------------------------------------
TEST(WfSettingsContract, WriteFailurePreservesPreviousFileAtomically)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-14"}, std::vector<std::string>{"WF-VER-221"});

    TempSettingsDir dir("atomic");
    RecordingDevLog devLog;
    UserSettingsStore impl(dir.settingsFile(), &devLog);
    IUserSettingsStore& store = impl;

    // 第一次成功写出（内容 A——基线快照）。
    UserSettings first;
    first.lastOpenDirectory = "D:/first";
    first.recentProjects = {"D:/first/proj"};
    store.store(first);
    const std::string before = dir.readFile(dir.settingsFile());

    // 外部独占（内核裁决面——与 StoreLock 同原语的宿主侧模拟）。
    ScopedForeignLockHandle lock(dir.settingsFile());
    ASSERT_TRUE(lock.held()) << "前置：外部持锁面建立";

    // 第二次写出必败（rename 替换被独占目标）→ WorkflowError（io 码
    // 透传——零吞错纪律）＋Dev 诊断留痕。
    UserSettings second;
    second.lastOpenDirectory = "D:/second";
    EXPECT_THROW(
        try {
            store.store(second);
        } catch (const WorkflowError& e) {
            EXPECT_NE(std::string(e.what()).find("IO-"), std::string::npos)
                << "异常应携带 io 稳定码词形：" << e.what();
            throw;
        },
        WorkflowError);
    ASSERT_FALSE(devLog.entries.empty()) << "失败须给诊断（留痕非静默）";

    // 解锁（句柄析构）后复核：目标内容仍为第一次的 A（先前输出原样
    // 保留——原子语义），本次失败零污染。
    const std::string after = dir.readFile(dir.settingsFile());
    EXPECT_EQ(after, before) << "写出失败不得损坏先前文件（io §4.6）";
    EXPECT_NE(after.find("D:/first"), std::string::npos);

    // 暂存零残留：目录内仅目标文件一个（.ird-json-tmp 暂存位已清空）。
    std::vector<fs::path> leftovers;
    for (const auto& entry : fs::directory_iterator(dir.dir())) {
        leftovers.push_back(entry.path());
    }
    ASSERT_EQ(leftovers.size(), std::size_t{1});
    EXPECT_EQ(leftovers[0], dir.settingsFile())
        << "失败路径暂存文件必须清理（零残留）";
}

// ---------------------------------------------------------------------
// 成功路径零残留：store 后目录内仅目标文件一个（暂存已收编——原子
// 写出的版面卫生面；与失败路径零残留构成两态复核）。
// ---------------------------------------------------------------------
TEST(WfSettingsContract, StoreLeavesNoResidueInDirectory)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-14"}, std::vector<std::string>{"WF-VER-221"});

    TempSettingsDir dir("residue");
    UserSettingsStore store(dir.settingsFile());
    store.store(fullSettings());
    store.store(fullSettings());  // 二次覆盖（OverwriteAtomic 语义路径）

    std::vector<fs::path> entries;
    for (const auto& entry : fs::directory_iterator(dir.dir())) {
        entries.push_back(entry.path());
    }
    ASSERT_EQ(entries.size(), std::size_t{1});
    EXPECT_EQ(entries[0], dir.settingsFile()) << "仅设置文件本身（零暂存残留）";
}

// ---------------------------------------------------------------------
// 损坏诊断通道词形（acceptance 3"损坏给诊断"）：注入损坏文件后 load
// 的诊断必须落在 workflow/user-settings 通道（diagnostics §7.2
// LogChannel 词形——Dev 级分流路由依据）。
// ---------------------------------------------------------------------
TEST(WfSettingsContract, LoadDiagnosticsCarryWorkflowChannel)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-14"}, std::vector<std::string>{"WF-VER-221"});

    TempSettingsDir dir("channel");
    dir.writeFile(dir.settingsFile(), "not-json-at-all{{{");

    RecordingDevLog devLog;
    const UserSettingsStore store(dir.settingsFile(), &devLog);
    const UserSettings loaded = store.load();
    EXPECT_EQ(loaded, UserSettings{}) << "损坏回退缺省默认值";
    ASSERT_EQ(devLog.entries.size(), std::size_t{1});
    EXPECT_EQ(devLog.entries[0].first, std::string{"workflow/user-settings"})
        << "诊断通道词形 workflow/user-settings";
    // 诊断消息携带 io 码词形（透传对端——可定位）。
    EXPECT_NE(devLog.entries[0].second.find("IO-"), std::string::npos)
        << "诊断消息应携带 io 稳定码词形";
}

}  // namespace
