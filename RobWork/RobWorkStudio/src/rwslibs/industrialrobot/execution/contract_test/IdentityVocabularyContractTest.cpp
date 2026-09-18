/**
 * @file   IdentityVocabularyContractTest.cpp
 * @brief  身份与词表跨单元契约（EX-T02）——五元组值类型/九态词表归 core、
 *         execution 零重定义；TaskId 为 execution 自有数据模型。
 *
 * 设计依据：
 *   - units/execution.md §2.2（TASK-03 行"不可越界列"：五元组值类型＝
 *     core TaskIdentity；O-24：四词表归 core）、§4.1（混用防线：类型未
 *     在本单元定义的只消费不重定义；TaskId＝core 未定义、execution 按
 *     core D-03 同纪律新增的自有数据模型）、§5.1（九态零新增——P-EX-4）
 *   - 需求 TASK-03（请求/完成事件携带五元组）、TASK-01（九态词表）
 *   - 任务契约 tasks/foundation/EX-T02.json acceptance 2（TaskId/RunId/
 *     AttemptId 值类型承接纪律——execution 只分配与携带不重定义）
 *
 * 目标归属（§3.4 测试目标分工）：本文件落 `_contract_test`——跨单元
 * 契约面（execution↔core 的词表/身份边界），与 `_test` 内的行为用例
 * 互补。扫描以定义形态（enum class/struct/using ＋类型名）为准：注释
 * 散文中的类型名提及（设计追溯要求）不构成定义，不会误报。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/core/Evaluation.hpp>
#include <sdurws/ird/core/Events.hpp>       // TaskStatusChangedPayload（事件载荷值语义抽查）
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/execution/TaskTypes.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// 本文件在全局域引用 core 类型——以命名空间别名建立短名（"core::" 限定
// 名不随 include/using 到达全局域，命名空间别名是显式且无歧义的形态）。
namespace core = sdurws::ird::core;

namespace {

/// industrialrobot 单元根（IRD_EXECUTION_UNIT_ROOT 注入——与 _test 目标
/// 同源定义，扫描 core 与 execution 两棵树）。
const fs::path& unitRoot()
{
    static const fs::path dir = fs::path{IRD_EXECUTION_UNIT_ROOT};
    return dir;
}

/// 递归收集 .hpp/.cpp（相对路径排序——失败信息确定）。
std::vector<fs::path> collectCppFiles(const fs::path& dir)
{
    std::vector<fs::path> files;
    std::error_code ec;
    if (!fs::exists(dir)) { return files; }
    for (auto it = fs::recursive_directory_iterator(dir, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec || !it->is_regular_file(ec)) continue;
        const auto ext = it->path().extension().string();
        if (ext == ".hpp" || ext == ".h" || ext == ".cpp") {
            files.push_back(fs::relative(it->path(), dir, ec));
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

/// 全文读取（读失败显性失败——不留"读不到＝零命中"假阳性通道）。
std::string readFile(const fs::path& file)
{
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        ADD_FAILURE() << "无法读取: " << file.string();
        return {};
    }
    return std::string{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

/// 文本是否含任一模式。
bool containsAny(const std::string& text, const std::vector<std::string>& patterns)
{
    return std::any_of(patterns.begin(), patterns.end(),
                       [&](const std::string& p) { return text.find(p) != std::string::npos; });
}

}  // namespace

/** execution 全源码面零重定义 core 词表/五元组类型（acceptance 2 的
 *  结构面钉子）：TaskState/TaskOutcome/EvaluationMode/EngineeringStatus
 *  四词表与 RunId/AttemptId/TaskIdentity 三身份类型的任何定义形态
 *  （enum class/struct/using）在 execution 树内零命中。 */
TEST(IdentityVocabularyContract, NoRedefinitionOfCoreTypesInExecution_AC2)
{
    const std::vector<std::string> kCoreOnly = {
        "enum class TaskState",       "enum class TaskOutcome",
        "enum class EvaluationMode",  "enum class EngineeringStatus",
        "struct RunId",               "struct AttemptId",
        "struct TaskIdentity",        "using RunId",
        "using AttemptId",            "using TaskIdentity",
    };
    for (const auto* sub : {"include", "src"}) {
        for (const auto& rel : collectCppFiles(unitRoot() / "execution" / sub)) {
            const auto text = readFile(unitRoot() / "execution" / sub / rel);
            EXPECT_FALSE(containsAny(text, kCoreOnly))
                << "execution 不得重定义 core 词表/五元组类型（§2.2 不可越界列）: "
                << rel.string();
        }
    }
}

/** 扫描器有效性自证（测试真实失败能力的一半）：core 树内这些定义真实
 *  存在——若模式拼错或扫描失效，本用例先红（防止"永远绿"的扫描）。 */
TEST(IdentityVocabularyContract, ScannerDetectsCoreDefinitionsSanity_AC2)
{
    const auto identityText =
        readFile(unitRoot() / "core" / "include" / "sdurws" / "ird" / "core" / "Identity.hpp");
    EXPECT_TRUE(containsAny(identityText, {"struct RunId", "struct AttemptId", "struct TaskIdentity"}))
        << "core Identity.hpp 应含五元组类型定义（扫描器锚点）";
    const auto evaluationText =
        readFile(unitRoot() / "core" / "include" / "sdurws" / "ird" / "core" / "Evaluation.hpp");
    EXPECT_TRUE(containsAny(evaluationText, {"enum class TaskState", "enum class TaskOutcome"}))
        << "core Evaluation.hpp 应含九态/结果词表定义（扫描器锚点）";
}

/** TaskId 是 execution 自有数据模型（§4.1：core 未定义此类型，execution
 *  按 core D-03 同纪律新增——定义必须存在于 TaskTypes.hpp，且 core 树
 *  不含同名定义）。模式带尾随空格做词边界（"struct TaskIdentity" 的
 *  子串包含 "struct TaskId"，裸子串会误报）。 */
TEST(IdentityVocabularyContract, TaskIdIsExecutionOwnedPerSection41_AC2)
{
    const auto taskTypesText = readFile(unitRoot() / "execution" / "include" / "sdurws"
                                        / "ird" / "execution" / "TaskTypes.hpp");
    EXPECT_TRUE(containsAny(taskTypesText, {"struct TaskId "}))
        << "TaskId 应定义于 execution TaskTypes.hpp（§4.1 概念表第一行）";
    const auto coreIdentityText =
        readFile(unitRoot() / "core" / "include" / "sdurws" / "ird" / "core" / "Identity.hpp");
    EXPECT_FALSE(containsAny(coreIdentityText, {"struct TaskId "}))
        << "core 不定义 TaskId（§4.1——TaskId 不进 core 事件五元组）";
}

/** 行为面抽查：链接进来的 core 五元组类型可用且值语义正确（类型同一性
 *  的链接器半区实证——execution 代码使用的 RunId/TaskIdentity 就是 core
 *  公共头导出的那些类型）。 */
TEST(IdentityVocabularyContract, CoreValueTypesUsableFromExecution_AC2)
{
    const auto run = core::RunId::generate();
    EXPECT_TRUE(run.isValid());
    EXPECT_EQ(core::RunId::fromCanonical(run.toCanonical()) == run, true) << "往返严格";

    core::TaskIdentity identity;
    identity.project = core::ProjectId::generate();
    identity.branch = core::BranchId::generate();
    identity.revision = core::RevisionId::generate();
    identity.run = run;
    identity.attempt = core::AttemptId{1};
    EXPECT_TRUE(identity.isValid()) << "五字段全有效（core 契约）";

    core::TaskStatusChangedPayload payload;
    payload.task = identity;
    payload.newState = core::TaskState::Running;
    EXPECT_EQ(payload.task == identity, true) << "事件载荷值语义（core Events 契约）";
}
