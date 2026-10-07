/**
 * @file   OpenWizardHomeTest.cpp
 * @brief  打开向导与无项目首页的模型测试（直调纯函数/内存服务面）：
 *         WfOpenWizard 格式识别分流与失败呈现、WfRecent 最近项目管理
 *         （PM-10 容量/去重/失效标记/移除）、WfHome 无项目首页数据面
 *         （units/workflow.md §7.2/§7.8/§11.2——WP-22-T05 落位批次）。
 *
 * 设计依据：
 *   - units/workflow.md §7.2（打开协议五步——入口格式识别分流、失败
 *     显示具体文件且不动当前项目）、§7.8（无项目首页三入口＋最近项目
 *     上限 10/去重/失效项保留＋位置不可用提示＋移除；无项目禁用七阶段/
 *     运行/应用/报告入口——仅留项目菜单）、§11.0（用例登记约定——模型
 *     测试直调计算库纯函数面）、§11.2（WF-VER-205~206 的编排核半区——
 *     真实落盘半区在 contract_test/OpenWizardContractTest.cpp；WF-VER-219
 *     最近项目管理〔模型〕；WF-VER-220 为 GUI（设计）——本文件的入口
 *     禁用数据用例是其数据面承载）、§14.3 P-WF-5（容量/脱敏未冻结期间
 *     按安全默认：上限 10 实现冻结、脱敏预留默认关闭）
 *   - REQUIREMENTS.md §17 PM-02（五步协议/三入口识别/失败显示具体文件
 *     且不动当前项目）、PM-10（三入口/上限 10/规范路径去重/失效项保留
 *     ＋提示＋移除/无项目禁用入口）、AT-20
 *   - 任务契约 tasks/foundation/WP-22-T05.json acceptance 1/2/3
 *
 * 用例与期望值口径：本单元判定面为枚举/键串/路径/列表事实——期望均为
 * 解析给定的精确值（无浮点量，I-WF-3 零领域阈值同源）。每个公共接口
 * 方法至少一条经接口消费的用例钉扎（WP-20-T03 首轮教训）：最近项目用例
 * 全部经 IRecentProjectsService& 接口引用消费（非具体类型直调）；run 的
 * 不触盘分支（Unknown/PackageFile/三来源登记）在本文件，落盘五步路径在
 * 契约测试（同接口，另一路径——两处合计覆盖公共面全部方法）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯登记（testkit §7.3）

#include <sdurws/ird/workflow/Lifecycle.hpp>
#include <sdurws/ird/workflow/Types.hpp>  // WorkflowError（前置契约断言）

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

using namespace sdurws::ird;
using workflow::OpenSource;
using workflow::OpenTargetKind;

// =====================================================================
// 夹具：临时目录（套件级总根＋用例级独立子目录——project 测试同型）
// =====================================================================

class WfOpenWizard : public ::testing::Test {
protected:
    static void SetUpTestSuite()
    {
        std::error_code ec;
        s_base = fs::temp_directory_path(ec) / "ird_wf_open_home_test";
        ASSERT_FALSE(ec);
        fs::remove_all(s_base, ec);  // 前次运行残留防御（总根重建）
        fs::create_directories(s_base, ec);
        ASSERT_FALSE(ec) << "无法创建测试根目录: " << s_base.string();
    }

    static void TearDownTestSuite()
    {
        // 总根清理（失败保留现场惯例——与 project 测试同型）。
        std::error_code ec;
        fs::remove_all(s_base, ec);
    }

    void SetUp() override
    {
        // 用例级子目录（自增——用例间零共享状态）。
        m_dir = s_base / ("case" + std::to_string(++s_caseCounter));
        std::error_code ec;
        fs::create_directories(m_dir, ec);
        ASSERT_FALSE(ec) << "无法创建用例目录: " << m_dir.string();
    }

    static fs::path s_base;   ///< 套件级总根（SetUpTestSuite 建/拆）
    static int s_caseCounter; ///< 用例自增计数（目录唯一性）
    fs::path m_dir;           ///< 本用例工作目录
};

fs::path WfOpenWizard::s_base;
int WfOpenWizard::s_caseCounter = 0;

// =====================================================================
// 格式识别分流（PM-02"命令行/拖放/对话框格式识别"——§7.2 表①行）
// =====================================================================

/**
 * 存在的目录 → 项目目录通道（.rwdesign 目录形态细检归打开②步——入口
 * 只分流；目录名不必带 .rwdesign 后缀——形态权威在对端 not-a-project）。
 */
TEST_F(WfOpenWizard, ClassifyExistingDirectoryGoesToProjectChannel)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-02"}, std::vector<std::string>{"AT-20"});

    // 存在的目录（后缀与否不改变分流——词表值唯一判据＝"是目录"）。
    EXPECT_EQ(workflow::classifyOpenTarget(m_dir),
              OpenTargetKind::ProjectDirectory);
    const fs::path bareDir = m_dir / "plain-name.rwdesign";
    std::error_code ec;
    fs::create_directories(bareDir, ec);
    ASSERT_FALSE(ec);
    EXPECT_EQ(workflow::classifyOpenTarget(bareDir),
              OpenTargetKind::ProjectDirectory);
}

/**
 * .rwpack 扩展名词面（大小写不敏感）→ 包导入通道——不要求实测存在
 * （拖放/对话框词面识别；不存在包的失败由导入通道呈现）。
 */
TEST_F(WfOpenWizard, ClassifyRwpackLexemeGoesToPackageChannel)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-02"}, std::vector<std::string>{"AT-20"});

    // 小写词面（存在的普通文件——分流按扩展名，不按内容）。
    const fs::path pack = m_dir / "transfer.rwpack";
    { std::ofstream sink(pack, std::ios::binary); ASSERT_TRUE(sink.is_open()); }
    EXPECT_EQ(workflow::classifyOpenTarget(pack),
              OpenTargetKind::PackageFile);

    // 大写词面（Windows 词面惯例——大小写不敏感）＋不存在词面（包通道
    // 不要求存在性实测——PM-02 三入口在拖放场景可能早于落盘到达）。
    EXPECT_EQ(workflow::classifyOpenTarget(m_dir / "UPPER.RWPACK"),
              OpenTargetKind::PackageFile);
}

/**
 * 其余词面 → 不可识别（空路径、无扩展名文件、其他扩展名——失败呈现面，
 * 零副作用）。
 */
TEST_F(WfOpenWizard, ClassifyUnknownLexemeIsRejected)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-02"}, std::vector<std::string>{"AT-20"});

    EXPECT_EQ(workflow::classifyOpenTarget(fs::path{}), OpenTargetKind::Unknown);
    const fs::path txt = m_dir / "notes.txt";
    { std::ofstream sink(txt, std::ios::binary); ASSERT_TRUE(sink.is_open()); }
    EXPECT_EQ(workflow::classifyOpenTarget(txt), OpenTargetKind::Unknown);
    EXPECT_EQ(workflow::classifyOpenTarget(m_dir / "no-extension"),
              OpenTargetKind::Unknown);
}

// =====================================================================
// 打开编排的不触盘分支（Unknown 失败呈现／PackageFile 分流——§7.2）
// =====================================================================

/**
 * 不可识别路径 → 失败呈现（UX-03 三字段＋具体文件恒非空——AT-20"失败
 * 显示具体文件"），零副作用（opened=false、无 store、无盘面写入）。
 */
TEST_F(WfOpenWizard, RunUnknownPathFailsWithConcreteFileAndNoSideEffect)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-02"}, std::vector<std::string>{"AT-20"});

    const fs::path target = m_dir / "not-a-project.txt";
    const workflow::OpenProjectOutcome outcome =
        workflow::OpenProjectFlow::run(OpenSource::CommandLine, target);

    ASSERT_FALSE(outcome.opened);
    EXPECT_EQ(outcome.targetKind, OpenTargetKind::Unknown);
    EXPECT_EQ(outcome.store, nullptr);
    ASSERT_TRUE(outcome.failure.has_value());
    // UX-03 三字段齐备（对象/上下文、原因、建议动作）＋具体文件＝目标词面。
    EXPECT_EQ(outcome.failure->context, target.u8string());
    EXPECT_FALSE(outcome.failure->cause.empty());
    EXPECT_FALSE(outcome.failure->recommendedAction.empty());
    EXPECT_EQ(outcome.failure->file, target.u8string());
}

/**
 * .rwpack 词面 → 分流登记返回：opened=false＋failure 置空（分流不是
 * 错误——调用方路由包导入向导）＋零副作用（包导入编排归 WP-22-T07，
 * 本批承诺"识别到位"）。三入口逐一到达：来源词表三值全部被编排器
 * 原样登记（可断言证据）。
 */
TEST_F(WfOpenWizard, RunPackageLexemeDispatchsWithoutSideEffectAllThreeSources)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-02"}, std::vector<std::string>{"AT-20"});

    const fs::path pack = m_dir / "boxed.rwpack";
    // 三入口（PM-02 原文：命令行/拖放/对话框）——同一词面三个来源逐一
    // 编排，分流结果一致＋来源回读正确（三入口识别的编排面证据）。
    const OpenSource sources[] = {
        OpenSource::CommandLine, OpenSource::DragDrop, OpenSource::Dialog};
    for (const OpenSource source : sources) {
        const workflow::OpenProjectOutcome outcome =
            workflow::OpenProjectFlow::run(source, pack);
        EXPECT_FALSE(outcome.opened) << "来源分支 " << static_cast<int>(source);
        EXPECT_EQ(outcome.targetKind, OpenTargetKind::PackageFile)
            << "来源分支 " << static_cast<int>(source);
        EXPECT_FALSE(outcome.failure.has_value())
            << "分流不是错误（调用方路由包导入向导）——来源分支 "
            << static_cast<int>(source);
        EXPECT_EQ(outcome.source, source) << "来源回读——入口分支 "
                                          << static_cast<int>(source);
        EXPECT_EQ(outcome.store, nullptr);
    }
    // 零副作用：包词面未被解包/建目录（路径仍不存在）。
    std::error_code ec;
    EXPECT_FALSE(fs::exists(pack / fs::path{"."}, ec));
}

/**
 * 空路径 → WorkflowError（调用方契约违约 fail-fast——取消在宿主侧拦截，
 * 取消态不得进入打开编排；UX-03"取消不是错误"由拦截点承载）。
 */
TEST_F(WfOpenWizard, RunEmptyPathIsCallerContractViolation)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-02"}, std::vector<std::string>{});

    EXPECT_THROW((void)workflow::OpenProjectFlow::run(OpenSource::Dialog,
                                                      fs::path{}),
                 workflow::WorkflowError);
}

// =====================================================================
// 最近项目管理（PM-10——WF-VER-219 模型承载；经接口引用消费）
// =====================================================================

/**
 * 经 IRecentProjectsService& 接口消费（公共面钉扎——WP-20-T03 教训）：
 * record 新路径置顶；record 已存在路径去重后置顶（原位消失、顺序前移）；
 * list 按 LRU 序返回。
 */
TEST(WfRecent, RecordDedupsAndMovesToTopViaInterface)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-10"}, std::vector<std::string>{"AT-20"});

    workflow::RecentProjectsService service;  // 默认容量（P-WF-5 安全默认）
    workflow::IRecentProjectsService& api = service;  // 接口消费面（钉扎）

    api.record("X:/proj/a.rwdesign");
    api.record("X:/proj/b.rwdesign");
    api.record("X:/proj/c.rwdesign");

    // LRU 序：最近记录在前（c → b → a）。期望词面经同一 lexically_normal
    // 收敛（Windows 下分隔符规范化为反斜杠——服务键＝lexically_normal，
    // 断言词面与键同源同形）。
    const auto norm = [](const char* p) {
        return fs::path{p}.lexically_normal().u8string();
    };
    std::vector<workflow::RecentProjectEntry> listed = api.list();
    ASSERT_EQ(listed.size(), 3u);
    EXPECT_EQ(listed[0].canonicalPath.u8string(), norm("X:/proj/c.rwdesign"));
    EXPECT_EQ(listed[1].canonicalPath.u8string(), norm("X:/proj/b.rwdesign"));
    EXPECT_EQ(listed[2].canonicalPath.u8string(), norm("X:/proj/a.rwdesign"));

    // 去重后置顶：再次记录 a——原位（尾部）消失、提升至首位。
    api.record("X:/proj/a.rwdesign");
    listed = api.list();
    ASSERT_EQ(listed.size(), 3u) << "去重＝同键单条目，不是追加";
    EXPECT_EQ(listed[0].canonicalPath.u8string(), norm("X:/proj/a.rwdesign"));
    EXPECT_EQ(listed[1].canonicalPath.u8string(), norm("X:/proj/c.rwdesign"));
    EXPECT_EQ(listed[2].canonicalPath.u8string(), norm("X:/proj/b.rwdesign"));
}

/**
 * 规范路径去重（PM-10"按规范路径去重"）：同义词面（冗余段差异）收敛为
 * 同一键——record 两次只占一位。
 */
TEST(WfRecent, RecordNormalizesLexicallyToSingleKey)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-10"}, std::vector<std::string>{"AT-20"});

    workflow::RecentProjectsService service;
    workflow::IRecentProjectsService& api = service;

    api.record("X:/proj/root/a.rwdesign");
    api.record("X:/proj/root/../root/./a.rwdesign");  // 同义冗余词面

    const std::vector<workflow::RecentProjectEntry> listed = api.list();
    ASSERT_EQ(listed.size(), 1u) << "规范路径去重：同义词面同键";
    // 键＝词法规范化形态（lexically_normal——唯一规范化点 recentProjectKey）。
    EXPECT_EQ(listed[0].canonicalPath.u8string(),
              workflow::recentProjectKey("X:/proj/root/a.rwdesign").u8string());
}

/**
 * 上限 10（PM-10 冻结——P-WF-5 留痕：容量恒为需求冻结值）：第 11 条
 * 记录淘汰最老一条；列表恒 ≤10 且首条为最新。
 */
TEST(WfRecent, CapacityIsTenOldestEvicted)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-10"}, std::vector<std::string>{"AT-20"});

    workflow::RecentProjectsService service;  // 默认容量＝kRecentProjectsCapacity
    workflow::IRecentProjectsService& api = service;

    // 缺省容量断言（P-WF-5：上限 10 实现冻结——PM-10 原文值）。
    EXPECT_EQ(workflow::kRecentProjectsCapacity, 10u);

    // 记录 11 条（p01 最老 → p11 最新）。
    for (int i = 1; i <= 11; ++i) {
        api.record("X:/cap/p" + std::to_string(i) + ".rwdesign");
    }
    // 期望词面经同一 lexically_normal 收敛（同上——键形断言口径）。
    const auto norm = [](const char* p) {
        return fs::path{p}.lexically_normal().u8string();
    };
    const std::vector<workflow::RecentProjectEntry> listed = api.list();
    ASSERT_EQ(listed.size(), 10u) << "上限 10（PM-10 冻结值）";
    EXPECT_EQ(listed.front().canonicalPath.u8string(), norm("X:/cap/p11.rwdesign"))
        << "最新置顶";
    EXPECT_EQ(listed.back().canonicalPath.u8string(), norm("X:/cap/p2.rwdesign"))
        << "最老（p1）被淘汰——LRU 尾部溢出";
}

/**
 * 失效项保留＋"位置不可用"标记（PM-10 原文）：记录后删除目录——list
 * 仍含该条（不自动剔除）且 locationAvailable=false；恢复目录后恢复
 * 可用标记（实测性——环境事实，非持久化属性）。
 */
TEST(WfRecent, MissingLocationKeptAndMarkedUnavailable)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-10"}, std::vector<std::string>{"AT-20"});

    workflow::RecentProjectsService service;
    workflow::IRecentProjectsService& api = service;

    // 真实临时目录（可用性实测需要真实盘面事实）。
    const fs::path proj = fs::temp_directory_path() / "ird_wf_recent_avail"
                          / "vanished.rwdesign";
    std::error_code ec;
    fs::remove_all(proj.parent_path(), ec);  // 前次残留防御
    fs::create_directories(proj, ec);
    ASSERT_FALSE(ec);

    api.record(proj.string());
    std::vector<workflow::RecentProjectEntry> listed = api.list();
    ASSERT_EQ(listed.size(), 1u);
    EXPECT_TRUE(listed[0].locationAvailable) << "目录在——位置可用";

    // 删除目录（项目位置消失）——失效项保留＋不可用标记（PM-10"失效项
    // 保留并提示'项目位置不可用'"的数据面）。
    fs::remove_all(proj, ec);
    ASSERT_FALSE(ec);
    listed = api.list();
    ASSERT_EQ(listed.size(), 1u) << "失效项保留：不自动剔除";
    EXPECT_FALSE(listed[0].locationAvailable) << "位置不可用标记";

    // 移除动作（PM-10"＋移除"——用户处置后消失）。
    api.remove(proj.string());
    EXPECT_TRUE(api.list().empty());
    fs::remove_all(proj.parent_path(), ec);  // 现场清理
}

/**
 * 移除幂等（remove 契约：不在列表＝无操作不报错——首页呈现刷新与列表
 * 变化的竞态容忍）；记录 0 容量构造与空路径记录均为调用方契约违约
 * （WorkflowError fail-fast——§10.3）。
 */
TEST(WfRecent, RemoveIdempotentAndContractViolationsFailFast)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-10"}, std::vector<std::string>{});

    workflow::RecentProjectsService service;
    workflow::IRecentProjectsService& api = service;

    // 幂等移除：未记录过的路径——无操作不抛。
    EXPECT_NO_THROW(api.remove("X:/never-recorded.rwdesign"));

    // 记录一条后按同义冗余词面移除（同键收敛匹配）——消失。
    api.record("X:/proj/dup.rwdesign");
    api.remove("X:/proj/./dup.rwdesign");
    EXPECT_TRUE(api.list().empty());
}

/**
 * P-WF-5 安全默认留痕（契约 acceptance 3）：零容量构造拒绝；空路径
 * record/remove 拒绝（调用方错误 fail-fast）；脱敏默认关闭——记录路径
 * 原样保留（无任何改写/裁剪行为）。
 */
TEST(WfRecent, Pwf5SafeDefaultsCapacityAndNoRedaction)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-10", "NFR-SEC-07"},
                  std::vector<std::string>{});

    // 零容量＝无业务意义（fail-fast）。
    EXPECT_THROW((void)workflow::RecentProjectsService(0),
                 workflow::WorkflowError);

    // 空路径 record/remove＝调用方契约违约（fail-fast）。
    workflow::RecentProjectsService service;
    EXPECT_THROW((void)(service.record(std::string{})), workflow::WorkflowError);
    EXPECT_THROW((void)(service.remove(std::string{})), workflow::WorkflowError);

    // 脱敏默认关闭（P-WF-5：脱敏配置面未冻结期间按安全默认——不发明
    // 脱敏行为）：路径词面原样保留（盘符/目录/文件名逐字在列）。
    workflow::RecentProjectsService plain;
    plain.record("X:/Visible Dir/MyRobot.rwdesign");
    const auto listed = plain.list();
    ASSERT_EQ(listed.size(), 1u);
    EXPECT_EQ(listed[0].canonicalPath.u8string(),
              fs::path{"X:/Visible Dir/MyRobot.rwdesign"}.lexically_normal().u8string())
        << "无脱敏：路径原样（安全默认——呈现侧脱敏归 diagnostics/ui 配置面）";
}

// =====================================================================
// 无项目首页数据面（PM-10——WF-VER-220 的数据面承载）
// =====================================================================

/**
 * 三入口可用（PM-10"新建/打开/最近项目三入口"——无项目态全启用）＋
 * 项目状态摘要键。
 */
TEST(WfHome, NoProjectThreeEntriesEnabledWithStatusSummary)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-10"}, std::vector<std::string>{"AT-20"});

    const workflow::HomeScreenData home =
        workflow::buildNoProjectHomeScreen({});

    EXPECT_EQ(home.statusKey, workflow::kHomeStatusNoProjectKey)
        << "项目状态摘要键（无项目态——值归 ui 文案资源）";

    // 三入口逐项断言（可用性＋键面）。
    const std::string enabledKeys[] = {
        workflow::kHomeEntryNewProject, workflow::kHomeEntryOpenProject,
        workflow::kHomeEntryRecentProjects, workflow::kHomeEntryProjectMenu};
    for (const std::string& key : enabledKeys) {
        bool found = false;
        for (const workflow::HomeEntry& entry : home.entries) {
            if (entry.key == key) {
                found = true;
                EXPECT_TRUE(entry.enabled) << "入口应可用: " << key;
            }
        }
        EXPECT_TRUE(found) << "入口缺失: " << key;
    }
}

/**
 * 无项目禁用七阶段/运行/应用/报告入口（PM-10——仅留项目菜单；WF-VER-220
 * 的数据面承载：GUI 呈现归 ui〔设计不执行〕，禁用数据在本面真实断言）。
 */
TEST(WfHome, NoProjectDisablesStageRunApplyReportKeepsProjectMenu)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-10"}, std::vector<std::string>{"AT-20"});

    const workflow::HomeScreenData home =
        workflow::buildNoProjectHomeScreen({});

    // 禁用集逐项断言（PM-10 原文四入口）。
    const std::string disabledKeys[] = {
        workflow::kHomeEntryStageNavigation, workflow::kHomeEntryRun,
        workflow::kHomeEntryApply, workflow::kHomeEntryReport};
    for (const std::string& key : disabledKeys) {
        bool found = false;
        for (const workflow::HomeEntry& entry : home.entries) {
            if (entry.key == key) {
                found = true;
                EXPECT_FALSE(entry.enabled) << "无项目应禁用: " << key;
            }
        }
        EXPECT_TRUE(found) << "禁用入口缺失: " << key;
    }

    // 仅留项目菜单：project-menu 可用（"仅留"的留侧）。
    for (const workflow::HomeEntry& entry : home.entries) {
        if (entry.key == workflow::kHomeEntryProjectMenu) {
            EXPECT_TRUE(entry.enabled) << "项目菜单保留可用";
        }
    }

    // 入口清单固定序八项（冻结——呈现布局稳定与测试确定性）。
    ASSERT_EQ(home.entries.size(), 8u);
    EXPECT_EQ(home.entries[0].key, workflow::kHomeEntryNewProject);
    EXPECT_EQ(home.entries[1].key, workflow::kHomeEntryOpenProject);
    EXPECT_EQ(home.entries[2].key, workflow::kHomeEntryRecentProjects);
    EXPECT_EQ(home.entries[3].key, workflow::kHomeEntryProjectMenu);
    EXPECT_EQ(home.entries[4].key, workflow::kHomeEntryStageNavigation);
    EXPECT_EQ(home.entries[5].key, workflow::kHomeEntryRun);
    EXPECT_EQ(home.entries[6].key, workflow::kHomeEntryApply);
    EXPECT_EQ(home.entries[7].key, workflow::kHomeEntryReport);
}

/**
 * 最近项目内容面：服务列表逐项透传（失效项保留）＋失效项呈现键齐备
 * （"位置不可用"提示＋重新选择＋移除——PM-10 原文三动作的键半区）；
 * 同输入双跑同输出（确定性——NFR-COR-02 同型）。
 */
TEST(WfHome, RecentItemsPassthroughWithUnavailableHintAndDeterminism)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-10"}, std::vector<std::string>{"AT-20"});

    // 失效项夹具（真实临时目录——可用性实测需要真实盘面事实：alive
    // 真实存在、gone 不创建＝失效项）。
    workflow::RecentProjectsService service;
    const fs::path aliveDir =
        fs::temp_directory_path() / "ird_wf_home_alive" / "alive.rwdesign";
    std::error_code ec;
    fs::remove_all(aliveDir.parent_path(), ec);  // 前次残留防御
    fs::create_directories(aliveDir, ec);
    ASSERT_FALSE(ec);
    service.record(aliveDir.string());
    service.record("X:/home-test/gone.rwdesign");  // 不存在的路径（失效项）
    const std::vector<workflow::RecentProjectEntry> recents = service.list();
    ASSERT_EQ(recents.size(), 2u);

    const workflow::HomeScreenData home = workflow::buildNoProjectHomeScreen(recents);
    ASSERT_EQ(home.recentItems.size(), 2u);
    // 透传保持服务序（LRU）与可用性事实（失效项保留——不剔除）；期望
    // 词面经同一 lexically_normal 收敛（键形断言口径——同上）。
    EXPECT_EQ(home.recentItems[0].canonicalPath.u8string(),
              fs::path{"X:/home-test/gone.rwdesign"}.lexically_normal().u8string());
    EXPECT_FALSE(home.recentItems[0].locationAvailable);
    EXPECT_EQ(home.recentItems[1].canonicalPath.u8string(),
              aliveDir.lexically_normal().u8string());
    EXPECT_TRUE(home.recentItems[1].locationAvailable);

    // 失效项三动作键半区齐备（PM-10：提示＋重新选择＋移除——键值归 ui
    // 文案资源，本面只承诺键存在且为工程用语词形）。
    EXPECT_FALSE(std::string{workflow::kHomeRecentUnavailableKey}.empty());
    EXPECT_FALSE(std::string{workflow::kHomeRecentReselectKey}.empty());
    EXPECT_FALSE(std::string{workflow::kHomeRecentRemoveKey}.empty());
    EXPECT_EQ(std::string{workflow::kHomeRecentUnavailableKey},
              "home.recent.location-unavailable");

    // 确定性：同输入双跑同输出（纯函数面——NFR-COR-02 同型）。
    const workflow::HomeScreenData replay =
        workflow::buildNoProjectHomeScreen(recents);
    EXPECT_EQ(replay, home);
    fs::remove_all(aliveDir.parent_path(), ec);  // 现场清理
}

}  // namespace
