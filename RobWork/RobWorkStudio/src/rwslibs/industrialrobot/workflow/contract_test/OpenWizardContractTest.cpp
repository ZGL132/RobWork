/**
 * @file   OpenWizardContractTest.cpp
 * @brief  打开向导的契约测试（WF-VER-205/206——units/workflow.md §11.2
 *         生命周期主线；PM-02/AT-20 的真实落盘承载半区）。
 *
 * 设计依据：
 *   - units/workflow.md §7.2（打开协议五步——入口三来源识别分流、失败
 *     显示具体文件且不动当前项目〔AT-20/PM-02〕）、§11.2（用例 205 打开
 *     五步协议〔观测点＝步骤诊断；命令行/拖放/对话框三入口识别〕、206
 *     打开失败不动当前项目〔观测点＝错误定位；契约（Fault）〕——两用例
 *     类型均为"契约"）、§10.3（接口属性表——环境/对端错误透传对端稳定
 *     码＋UX-03 字段齐备）
 *   - REQUIREMENTS.md §17 PM-02 原文（五步协议；命令行打开 .rwdesign
 *     目录、拖放打开与打开对话框格式识别；失败显示具体文件且不动当前
 *     项目）、AT-20
 *   - project.md §5.1/§8.7（factory.open＝PM-02②③⑤服务侧——"激活前
 *     失败不影响当前项目：open 完整构造候选上下文后才返回；任何失败
 *     路径不触碰已打开项目的存储上下文"）、§13.2 workflow 行（open 服务
 *     调用序列＝向导编排）
 *   - 任务契约 tasks/foundation/WP-22-T05.json acceptance 1（失败显示
 *     具体文件且不动当前项目用例通过——AT-20）
 *
 * 契约测试形态（§11.0——"契约测试＝跨单元联合"）：本文件与 project
 * 真实存储实现联合（ProjectStoreFactory::createNew/open 真实落盘临时
 * 目录）——五步协议的②③⑤服务侧由 project 真实执行，workflow 侧编排
 * 核（入口分流→open→失败呈现/激活材料移交）被端到端驱动。"不动当前
 * 项目"不是声明：以当前项目存储上下文可用性＋目录树字节面快照双重
 * 复核（损坏注入项目打开失败前后，当前项目目录树逐文件一致）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯登记（testkit §7.3）

#include <sdurws/ird/project/QueryPort.hpp>  // branchTips（修订观测点——ProjectStore.hpp 只前向声明端口）
#include <sdurws/ird/workflow/Lifecycle.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace {

using namespace sdurws::ird;
using workflow::OpenSource;
using workflow::OpenTargetKind;

// =====================================================================
// 夹具：临时目录（套件级总根＋用例级独立项目目录——T04 契约测试同型）
// =====================================================================

class OpenWizardContract : public ::testing::Test {
protected:
    static void SetUpTestSuite()
    {
        std::error_code ec;
        s_base = fs::temp_directory_path(ec) / "ird_wf_open_contract_test";
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
        // 用例级项目目录（自增子目录——用例间零共享状态）。
        m_dir = s_base / ("case" + std::to_string(++s_caseCounter));
        std::error_code ec;
        fs::create_directories(m_dir, ec);
        ASSERT_FALSE(ec);
    }

    /// 目录树字节面快照（相对路径 UTF-8 → 文件大小——"不动当前项目"的
    /// 逐文件复核面）。排除 lock 文件：持有方心跳线程按固定周期原地
    /// 重写心跳时间戳（project.md §9.4——内容变化是持有中的正常事实，
    /// 不属"打开失败动了我"）；大小不变但字节可能变，故整文件排除。
    static std::map<std::string, std::uintmax_t> snapshotTree(const fs::path& root)
    {
        std::map<std::string, std::uintmax_t> snapshot;
        std::error_code ec;
        for (auto it = fs::recursive_directory_iterator(root, ec);
             it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (ec || !it->is_regular_file(ec)) {
                continue;
            }
            const std::string name = it->path().filename().string();
            if (name == "lock") {
                continue;  // 心跳重写面排除（见函数注）
            }
            snapshot[it->path().lexically_relative(root).u8string()]
                = it->file_size(ec);
        }
        return snapshot;
    }

    /// 以 createNew 产出黄金项目并返回结果（调用方持有 store——释放写
    /// 锁须 requestClose＋reset，见各用例）。
    static project::OpenStoreResult createGolden(const fs::path& dir)
    {
        return project::ProjectStoreFactory::createNew(dir, "打开协议黄金项目");
    }

    static fs::path s_base;   ///< 套件级总根（SetUpTestSuite 建/拆）
    static int s_caseCounter; ///< 用例自增计数（目录唯一性）
    fs::path m_dir;           ///< 本用例工作目录
};

fs::path OpenWizardContract::s_base;
int OpenWizardContract::s_caseCounter = 0;

// =====================================================================
// WF-VER-205 打开五步协议（PM-02/AT-20——观测点：步骤诊断＋三入口识别）
// =====================================================================

/**
 * 三入口（命令行/拖放/对话框）逐一驱动五步协议：黄金项目经 createNew
 * 落盘后，三来源各执行一次打开编排——五步全过（②③⑤服务侧成功＝store
 * 非空可写、身份一致、恢复报告闭包完整）＋激活材料移交（projectId/
 * canonicalPath/recovery）。来源回读逐一正确（三入口识别的编排面证据）。
 */
TEST_F(OpenWizardContract, WF_VER_205_FiveStepProtocolAllThreeSources)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-02"}, std::vector<std::string>{"AT-20"});

    // 黄金项目落盘（①入口分流之前的材料准备——createNew＝PM-01 存储
    // 侧，与被测打开协议正交）。
    const fs::path goldenDir = m_dir / "golden.rwdesign";
    project::OpenStoreResult created = createGolden(goldenDir);
    ASSERT_TRUE(created.store != nullptr);
    const core::ProjectId goldenId = created.store->projectId();
    created.store->requestClose();  // 显式释放写锁（创建会话结束）
    created.store.reset();

    // 三入口逐一打开（PM-02 原文：命令行/拖放/对话框）。
    const OpenSource sources[] = {
        OpenSource::CommandLine, OpenSource::DragDrop, OpenSource::Dialog};
    for (const OpenSource source : sources) {
        workflow::OpenProjectOutcome outcome =
            workflow::OpenProjectFlow::run(source, goldenDir);

        // 五步全过：opened＋可写（无他方持锁——Writable 请求不被降级）。
        ASSERT_TRUE(outcome.opened) << "来源分支 " << static_cast<int>(source);
        ASSERT_TRUE(outcome.store != nullptr);
        EXPECT_FALSE(outcome.readonly) << "来源分支 " << static_cast<int>(source);
        EXPECT_FALSE(outcome.failure.has_value());

        // 激活材料：身份与 createNew 一致（打开③步读校验的编排面观测）、
        // 规范路径非空（最近项目记录键）、恢复报告闭包完整（⑤步诊断）。
        ASSERT_TRUE(outcome.projectId.has_value());
        EXPECT_EQ(outcome.projectId->toCanonical(), goldenId.toCanonical());
        EXPECT_FALSE(outcome.canonicalPath.empty());
        EXPECT_TRUE(outcome.recovery.headIntegrityVerified);
        EXPECT_EQ(outcome.targetKind, OpenTargetKind::ProjectDirectory);
        EXPECT_EQ(outcome.source, source) << "来源回读——分支 "
                                          << static_cast<int>(source);

        // 分流正确性旁证：打开请求送达的是目录通道（PackageFile 分流
        // 不触达 store——opened=false，见模型测试）。
        outcome.store->requestClose();  // 释放写锁（下一入口的打开前置）
        outcome.store.reset();
    }
}

// =====================================================================
// WF-VER-206 打开失败不动当前项目（PM-02——契约（Fault）；观测点＝错误定位）
// =====================================================================

/**
 * 故障注入：HEAD 文件改写为垃圾字节（损坏项目）。打开失败呈现具体
 * 定位（cause 含 HEAD 词面、file/三字段齐备），且当前项目原状——存储
 * 上下文可用性（可写/身份/查询面）＋目录树字节面快照逐文件一致双重
 * 复核；损坏目标的原文件不动（打开路径零写入——PM-06 同源口径）。
 */
TEST_F(OpenWizardContract, WF_VER_206_CorruptProjectFailsWithConcreteFileAndCurrentUntouched)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-02"}, std::vector<std::string>{"AT-20"});

    // ---- 当前项目 A：打开并保持 active（"当前会话"的承载）。
    const fs::path currentDir = m_dir / "current.rwdesign";
    project::OpenStoreResult current = createGolden(currentDir);
    ASSERT_TRUE(current.store != nullptr);
    ASSERT_TRUE(current.store->writable());
    const core::ProjectId currentId = current.store->projectId();
    const auto tipsBefore = current.store->query().branchTips();
    ASSERT_EQ(tipsBefore.size(), 1u);

    // ---- 损坏项目 B：createNew 落盘 → 关闭 → HEAD 注入垃圾字节。
    const fs::path corruptDir = m_dir / "corrupt.rwdesign";
    {
        project::OpenStoreResult victim = createGolden(corruptDir);
        ASSERT_TRUE(victim.store != nullptr);
        victim.store->requestClose();
        victim.store.reset();
    }
    const fs::path headFile = corruptDir / "HEAD";
    {
        std::ofstream vandal(headFile, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(vandal.is_open());
        vandal << "\x01\x02NOT-A-HEAD-RECORD\xff\xfe";  // 垃圾字节（非法记录）
    }
    // 损坏注入的盘面事实基线（打开失败后逐一比对——目标零写入）。
    const auto corruptBefore = snapshotTree(corruptDir);
    const auto currentBefore = snapshotTree(currentDir);
    const std::string headVandalBytes =
        "\x01\x02NOT-A-HEAD-RECORD\xff\xfe";

    // ---- 打开损坏项目（拖放入口——三入口之一的失败路径）。
    const workflow::OpenProjectOutcome outcome =
        workflow::OpenProjectFlow::run(OpenSource::DragDrop, corruptDir);

    // 失败呈现（观测点＝错误定位）：opened=false＋UX-03 三字段＋具体
    // 定位（cause 含 HEAD 词面——损坏对象；file 提取自 detail 的 path=
    // 定位键，恒非空）＋建议动作。
    ASSERT_FALSE(outcome.opened);
    ASSERT_TRUE(outcome.failure.has_value());
    EXPECT_FALSE(outcome.failure->context.empty());
    EXPECT_NE(outcome.failure->cause.find("HEAD"), std::string::npos)
        << "失败原因应定位到损坏对象（HEAD）——原文: "
        << outcome.failure->cause;
    EXPECT_FALSE(outcome.failure->file.empty()) << "失败显示具体文件（AT-20）";
    EXPECT_FALSE(outcome.failure->recommendedAction.empty());
    EXPECT_EQ(outcome.store, nullptr) << "失败路径无存储上下文（激活前失败）";
    EXPECT_EQ(outcome.source, OpenSource::DragDrop);

    // ---- 当前项目原状（AT-20"不动当前项目"）：
    // ① 存储上下文可用性（写权限/身份/查询面照常）。
    ASSERT_TRUE(current.store != nullptr);
    EXPECT_TRUE(current.store->writable());
    EXPECT_EQ(current.store->projectId().toCanonical(), currentId.toCanonical());
    const auto tipsAfter = current.store->query().branchTips();
    ASSERT_EQ(tipsAfter.size(), 1u);
    EXPECT_EQ(tipsAfter[0].id, tipsBefore[0].id);
    // ② 目录树字节面（逐文件相对路径＋大小——快照恒等；lock 心跳面
    //    排除，见 snapshotTree 注）。
    const auto currentAfter = snapshotTree(currentDir);
    EXPECT_EQ(currentAfter, currentBefore)
        << "打开失败不得触碰当前项目目录树（AT-20/PM-02）";
    // ③ 会话继续可用（失败后仍能正常操作当前项目——再开一次损坏项目
    //    之外的真实工作面不因此中断）。
    EXPECT_NO_THROW((void)current.store->schema());

    // ---- 损坏目标原文件不动（打开路径零写入——失败呈现不是修复）。
    const auto corruptAfter = snapshotTree(corruptDir);
    EXPECT_EQ(corruptAfter, corruptBefore) << "打开失败不得改写目标项目";
    {
        std::ifstream verify(headFile, std::ios::binary);
        const std::string bytes{std::istreambuf_iterator<char>(verify),
                                std::istreambuf_iterator<char>()};
        EXPECT_EQ(bytes, headVandalBytes) << "HEAD 垃圾字节原样（零写入）";
    }

    // 收尾释放写锁（目录随套件总根清理）。
    current.store->requestClose();
    current.store.reset();
}

/**
 * 非 project 目录（空目录）→ not-a-project 失败呈现（②步目录形态检查
 * ——PM-02"失败显示具体文件"的目录形态分支）：cause 定位 project.json
 * 缺失，建议动作指向新建向导（UX-03 三字段齐备）。
 */
TEST_F(OpenWizardContract, WF_VER_206_NotAProjectPresentsContextAndGuidance)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-02"}, std::vector<std::string>{"AT-20"});

    // 空目录（存在、非项目——②步 not-a-project 判定面）。
    const fs::path emptyDir = m_dir / "empty.rwdesign";
    std::error_code ec;
    fs::create_directories(emptyDir, ec);
    ASSERT_FALSE(ec);

    const workflow::OpenProjectOutcome outcome =
        workflow::OpenProjectFlow::run(OpenSource::Dialog, emptyDir);

    ASSERT_FALSE(outcome.opened);
    ASSERT_TRUE(outcome.failure.has_value());
    EXPECT_EQ(outcome.failure->context, emptyDir.u8string());
    EXPECT_NE(outcome.failure->cause.find("project.json"), std::string::npos)
        << "原因应定位缺失文件——原文: " << outcome.failure->cause;
    EXPECT_FALSE(outcome.failure->file.empty());
    EXPECT_FALSE(outcome.failure->recommendedAction.empty());
    EXPECT_EQ(outcome.store, nullptr);
}

}  // namespace
