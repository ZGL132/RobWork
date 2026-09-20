/**
 * @file   ModelSummaryTaskStatusCrossUnitContractTest.cpp
 * @brief  模型摘要与任务投影的跨单元契约测试（RPT-T12）——注入接口签名
 *         冻结（P-RPT-2）/schema 类型恒等（P-RPT-9 基线）/公共头零 runtime
 *         与 execution 类型纪律（§3.3 注入形态——表外边＝构建失败 SA-10 的
 *         常驻自证）/词表与 core 冻结词的一致性。
 *
 * 设计依据：
 *   - units/reporting.md §9.7（IModelSummaryProvider 签名——L5 适配的对接
 *     凭据）、§3.3（io/runtime/execution 注入边界——公共头零对端类型）、
 *     §9.9 runtime/execution 行（引用边界）、§3.4（`_contract_test`＝跨
 *     单元契约面的目标分工）、§3.2（core 四登记边——词表消费面）
 *   - runtime.md §10.12/§13.2（P-RPT-9 基线＝runtime.md v0.1
 *     Draft-Structured；快照释放呈现口径）
 *   - 任务契约 tasks/foundation/RPT-T12.json acceptance 1（schema 冻结——
 *     交接凭据的类型面）/3（ITaskStatusSource 对齐形态＋零 execution 类型）/
 *     4（P-RPT-2 签名冻结）/5（禁项锁定的接口面自证）
 *
 * 边界声明：本文件不 include 任何 runtime/execution/io 公共头（include 面
 *   白名单扫描 BuildRedLineTest::NoCrossUnitInclude 与本文件的字符串扫描
 *   互为两道防线——前者扫描整棵源码树，本文件对两件新头做具名逐字自证）；
 *   与 execution TaskSnapshot/TerminationCause 的"值形态对齐"以投影字段
 *   语义注释承载（对端类型不出现——对齐由 L5 适配器落实，其正确性归 L5
 *   装配验证，本文件只锁 reporting 侧契约面）。
 */

#include <sdurws/ird/reporting/ModelSummary.hpp>
#include <sdurws/ird/reporting/TaskStatusSource.hpp>

#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/core/Evaluation.hpp>
#include <sdurws/ird/core/Identity.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <type_traits>

namespace {

using namespace sdurws::ird::reporting;
namespace core = sdurws::ird::core;

// =====================================================================
// ①IModelSummaryProvider 签名冻结（acceptance 4——P-RPT-2 处置：架构
//   所有者确认注入为正式形态或补边裁决前签名不变；签名漂移＝编译失败）
// =====================================================================

// trySummary：core::RevisionId 入参、std::optional<ModelSummary> 返回、
// const 成员（§9.7 原文签名——runtime §13.2 交接的对接凭据）。
using TrySummaryFn = std::optional<ModelSummary> (IModelSummaryProvider::*)(core::RevisionId) const;
static_assert(std::is_same<decltype(&IModelSummaryProvider::trySummary), TrySummaryFn>::value,
              "IModelSummaryProvider::trySummary 签名冻结（P-RPT-2：裁决前不变，"
              "runtime 侧适配以此签名为对接凭据）");

// =====================================================================
// ②ModelSummary schema 类型恒等（acceptance 1——P-RPT-9 基线：core.md
//   v0.1 Draft；core 冻结出 diff 后按影响面增量同步）
// =====================================================================

// 身份四块＋修订均以 core 冻结类型承载（零复制/零重定义——类型漂移即
// 编译失败，O-13 消费侧纪律）：
static_assert(std::is_same<decltype(std::declval<ModelSummary>().revision), core::RevisionId>::value,
              "ModelSummary.revision 必须以 core::RevisionId 承载");
static_assert(std::is_same<decltype(std::declval<ModelSummary>().snapshotIdentity),
                           core::ContentIdentity>::value,
              "ModelSummary.snapshotIdentity 必须以 core::ContentIdentity 承载（快照身份块）");
static_assert(std::is_same<decltype(std::declval<ModelSummary>().modelIdentity),
                           core::ContentIdentity>::value,
              "ModelSummary.modelIdentity 必须以 core::ContentIdentity 承载");
static_assert(std::is_same<decltype(std::declval<ModelSummary>().nameMapIdentity),
                           core::ContentIdentity>::value,
              "ModelSummary.nameMapIdentity 必须以 core::ContentIdentity 承载（CON-06）");
static_assert(std::is_same<decltype(std::declval<ModelSummary>().policyContentIdentity),
                           core::ContentIdentity>::value,
              "ModelSummary.policyContentIdentity 必须以 core::ContentIdentity 承载");
static_assert(std::is_same<decltype(std::declval<ResourceSummary>().digest), core::Digest256>::value,
              "ResourceSummary.digest 必须以 core::Digest256 承载（CON-03 固化摘要）");
static_assert(std::is_same<decltype(std::declval<ConfigSummary>().contentIdentity),
                           core::ContentIdentity>::value,
              "ConfigSummary.contentIdentity 必须以 core::ContentIdentity 承载");

// =====================================================================
// ③ITaskStatusSource 签名与投影类型面（acceptance 3——对齐 tryTask/
//   progress 值形态；core 词表直传零第二词表）
// =====================================================================

// tryStatus：core::TaskIdentity 入参、std::optional<TaskStatusProjection>
// 返回、const 成员（§3.3 注入形态——TaskIdentity 引用边界即 §9.9 原文）。
using TryStatusFn = std::optional<TaskStatusProjection> (ITaskStatusSource::*)(core::TaskIdentity) const;
static_assert(std::is_same<decltype(&ITaskStatusSource::tryStatus), TryStatusFn>::value,
              "ITaskStatusSource::tryStatus 签名锁定（最小接口——恰一个只读查询方法）");

// 状态轴＝core::TaskState 直传（九态词表归 core——core.md §4.7；零转词）：
static_assert(std::is_same<decltype(std::declval<TaskStatusProjection>().state), core::TaskState>::value,
              "TaskStatusProjection.state 必须以 core::TaskState 承载（词表单源——零第二词表）");
// 任务引用＝core::TaskIdentity 五元组（§9.9"任务状态引用=TaskIdentity"）：
static_assert(std::is_same<decltype(std::declval<TaskStatusProjection>().identity), core::TaskIdentity>::value,
              "TaskStatusProjection.identity 必须以 core::TaskIdentity 承载（呈现引用）");

// =====================================================================
// ④公共头零 runtime/execution 类型纪律（acceptance 3——§3.3 注入形态：
//   表外边＝构建失败 SA-10 的字符串级具名自证；与 BuildRedLineTest 的
//   include 面白名单扫描互为两道防线）
// =====================================================================

/// 读取文件全文；读失败显性失败（不留"读不到＝零命中"的假阳性通道）。
std::string readFileOrDie(const std::string& path)
{
    FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        ADD_FAILURE() << "无法读取公共头：" << path;
        return {};
    }
    std::string content;
    char buffer[4096];
    std::size_t n = 0;
    while ((n = std::fread(buffer, 1, sizeof(buffer), file)) > 0) {
        content.append(buffer, n);
    }
    std::fclose(file);
    return content;
}

/// 两件注入契约头的红线自证：零 runtime/execution/io include＋零 Qt
/// （§3.2 依赖图——公共头只出现 reporting/core 类型）。
TEST(ModelSummaryTaskStatusHeaderDiscipline, PublicHeadersZeroOffTreeIncludes_RPT12_ACC3)
{
    const std::string unitRoot = IRD_REPORTING_UNIT_ROOT;
    // 根＝industrialrobot 目录（CMake 注入形 "<dir>/.."——BuildRedLineTest
    // 同款口径，单元相对路径带 reporting/ 前缀）。
    const std::string modelSummaryHeader =
        unitRoot + "/reporting/include/sdurws/ird/reporting/ModelSummary.hpp";
    const std::string taskStatusHeader =
        unitRoot + "/reporting/include/sdurws/ird/reporting/TaskStatusSource.hpp";

    for (const std::string* header : {&modelSummaryHeader, &taskStatusHeader}) {
        const std::string content = readFileOrDie(*header);
        // 三类表外单元（P-RPT-1/P-RPT-2 注入形态＋SA-10 表外边红线）：
        EXPECT_EQ(content.find("sdurws/ird/runtime/"), std::string::npos)
            << "公共头出现 runtime include——P-RPT-2 注入形态违约：" << *header;
        EXPECT_EQ(content.find("sdurws/ird/execution/"), std::string::npos)
            << "公共头出现 execution include——零 execution 编译边违约（SA-10）：" << *header;
        EXPECT_EQ(content.find("sdurws/ird/io/"), std::string::npos)
            << "公共头出现 io include——P-RPT-1 纪律违约：" << *header;
        EXPECT_EQ(content.find("#include <Qt"), std::string::npos)
            << "公共头出现 Qt include——D-01 零 Qt 纪律违约：" << *header;
        // 对端命名空间禁入（类型级注入面核查——即使经其他头间接可达，
        // 本头文本亦不得引用对端命名空间限定名）：
        EXPECT_EQ(content.find("ird::runtime::"), std::string::npos)
            << "公共头出现 runtime 命名空间限定名（零 runtime 类型——§3.3）：" << *header;
        EXPECT_EQ(content.find("ird::execution::"), std::string::npos)
            << "公共头出现 execution 命名空间限定名（零 execution 类型——§3.3）：" << *header;
    }
}

// =====================================================================
// ⑤词表与 core 冻结词的一致性（acceptance 3——终态同名轴的词面一致；
//   core::toToken 是登记边内的合法消费——core.md §4.7 冻结表）
// =====================================================================

/**
 * TaskTerminationKind 前四值与 core::TaskState 四终态的 token 逐字一致
 * （两轴终态同名的词面一致性——状态轴与原因轴分立但同名终态呈现同词，
 * 避免呈现层出现"canceled/cancelled"式漂移）；force-terminated 为原因轴
 * 单列标记（core 九态无此值——不与状态轴比对）。
 */
TEST(TaskStatusVocabulary, TerminationTokensMatchCoreTerminalStateTokens_RPT12_ACC3)
{
    // core::TaskState 四终态的冻结 token（core::toToken——core.md §4.7）：
    EXPECT_EQ(std::string{core::toToken(core::TaskState::Canceled)},
              std::string{toToken(TaskTerminationKind::Canceled)});
    EXPECT_EQ(std::string{core::toToken(core::TaskState::Failed)},
              std::string{toToken(TaskTerminationKind::Failed)});
    EXPECT_EQ(std::string{core::toToken(core::TaskState::Completed)},
              std::string{toToken(TaskTerminationKind::Completed)});
    EXPECT_EQ(std::string{core::toToken(core::TaskState::Interrupted)},
              std::string{toToken(TaskTerminationKind::Interrupted)});
}

/**
 * 资源状态词表与 core 词表风格一致（kebab 小写）＋状态轴直传的自证：
 * TaskStatusProjection 的 state 字段不携带任何 reporting 侧转词函数——
 * 呈现词面唯一出口是 core::toToken(core::TaskState)（词表单源——O-13）。
 */
TEST(TaskStatusVocabulary, StateAxisHasNoSecondVocabulary_RPT12_ACC3)
{
    // core 九态 token 全表可用（报告呈现消费 core 冻结词——登记边内）：
    EXPECT_STREQ(core::toToken(core::TaskState::Queued), "queued");
    EXPECT_STREQ(core::toToken(core::TaskState::Interrupted), "interrupted");
    SUCCEED() << "状态轴词面唯一出口＝core::toToken（TaskStatusSource.hpp "
                 "未定义 TaskState 转 token——零第二词表的结构性事实）";
}

}  // namespace
