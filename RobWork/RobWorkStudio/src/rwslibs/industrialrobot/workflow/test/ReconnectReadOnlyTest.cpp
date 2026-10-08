/**
 * @file   ReconnectReadOnlyTest.cpp
 * @brief  旧格式升级指引/只读会话/外部源重关联的模型测试（WP-22-T08——
 *         units/workflow.md §7.5 编排核的直调半区；PM-06 升级指引三键
 *         提取、PM-07 只读会话数据面、PM-09 重关联编排核全分支）。
 *
 * 设计依据：
 *   - units/workflow.md §7.5（旧格式/未来版本＝稳定只读拒绝＋诊断码＋
 *     升级指引数据〔显示当前版本、项目版本、升级工具入口，不自动升级〕
 *     ——拒绝判定归 project ②步、workflow 承接升级指引数据面；只读打开
 *     ＝锁被持提示 PID、writable=false 禁编辑与应用提交、第二写者不阻塞
 *     等待——降级裁决归 project、workflow 承接呈现数据面；重关联＝重新
 *     关联入口＋显式提交产生新修订＋失败不动当前项目——检测数据由 io
 *     提供、提交经 project）、§10.3（错误二分：调用方错误 WorkflowError
 *     fail-fast；环境/对端错误值轨道；取消非错误）、§14.1 D-WF-6/D-WF-7
 *     （宿主面归 ui；零新增稳定码——对端透传）
 *   - REQUIREMENTS.md §17 PM-06/PM-07/PM-09 原文、AT-21（重关联显式提交
 *     产生新修订）、UX-03（取消是状态非错误——取消不产诊断；失败三字段）
 *   - 任务契约 tasks/foundation/WP-22-T08.json acceptance 1/2/3（旧格式
 *     稳定拒绝＋PRJ-FORMAT-LEGACY＋升级指引＋原文件不动；只读打开 PID
 *     提示＋禁编辑与应用提交＋不阻塞等待；重关联显式提交产生新修订；
 *     P-PR-9 未裁决——编排核与端口零命令 token 知识）
 *
 * 测试形态（§11.0——模型测试＝直调计算库面）：parseUpgradeGuidance/
 * buildReadOnlySessionNotice 为纯函数直调（黄金 detail 词形取自 project
 * Codec 侧 detail 组装口径）；RelinkFlow::run 为静态编排核，检测/提交/
 * 决策三端口全部脚本化桩（状态/成败/异常可编程，调用轨迹可观测）。
 * 真实落盘半区（旧格式稳定拒绝＋PRJ-FORMAT-LEGACY 诊断＋原文件字节
 * 不动、双实例降级＋PID 提示＋提交/草稿写轨拒绝、重关联真实提交产生
 * 新修订＋失败零写）在契约测试 ReconnectReadOnlyContractTest.cpp
 * （跨单元联合——与 project 真实存储联合）。
 *
 * 追溯约定（AGENTS.md §2.7）：用例名与断言注释标注需求/AT 编号。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/core/Identity.hpp>              // core::ObjectId/RevisionId（资源/修订强类型——isValid/toCanonical）
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯登记
#include <sdurws/ird/workflow/Lifecycle.hpp>

#include <stdexcept>
#include <string>
#include <utility>

namespace {

using namespace sdurws::ird;
using workflow::ExternalSourceState;
using workflow::ExternalSourceStatus;
using workflow::ReadOnlySessionNotice;
using workflow::RelinkDisposition;
using workflow::RelinkOutcome;
using workflow::RelinkRequest;
using workflow::WorkflowError;

// =====================================================================
// 黄金 detail 词形（project 侧 Codec 打开②步失败 detail 的组装口径——
// UpgradeGuidance 提取函数的消费面词形；改词形须两单元同步）
// =====================================================================

/// schema-future detail（三键齐备——PM-06 升级指引的完整形态）。
constexpr const char* kSchemaFutureDetail =
    "project/codec: schema-future document=20000 supported=10000 "
    "upgrade=ISchemaUpgrader-stage-b type=project-static-identity";

/// format-legacy detail（版本旧于当前——document/supported 两键，无
/// upgrade 键：旧格式无"升级到新版本"语义，修复路径是旧版本导出）。
constexpr const char* kLegacyVersionDetail =
    "project/codec: format-legacy document=0 supported=10000 "
    "type=project-static-identity";

/// format-legacy detail（formatId 不符——"document-format-id=" 键形，
/// 无 document/upgrade 键）。
constexpr const char* kLegacyFormatIdDetail =
    "project/codec: format-legacy document-format-id=rwproj supported=rwdesign";

// =====================================================================
// 桩：重关联检测/提交端口（IExternalRelinkPort 脚本化替身——状态/成败/
// 异常可编程，调用轨迹可观测；编排核零命令 token 知识的对接面）
// =====================================================================

class RelinkPortStub final : public workflow::IExternalRelinkPort {
public:
    ExternalSourceStatus probeStatus;    ///< probe() 返回值（检测结论脚本位）
    bool probeThrows = false;            ///< true＝probe 抛（环境失败注入）
    RelinkExecution relinkResult;        ///< relink() 返回值（执行结果脚本位）
    bool relinkThrows = false;           ///< true＝relink 抛（环境失败注入）
    int probeCalls = 0;                  ///< probe 调用计数（NotNeeded 零触达断言的对照面）
    int relinkCalls = 0;                 ///< relink 调用计数（取消/NotNeeded 零提交断言）
    RelinkRequest lastProbeRequest{};    ///< 最近 probe 请求（透传断言）
    ExternalSourceStatus lastRelinkStatus{};///< 最近 relink 收到的状态（probe→relink 同源断言）

    ExternalSourceStatus probe(const RelinkRequest& request) override
    {
        ++probeCalls;
        lastProbeRequest = request;
        if (probeThrows) {
            throw std::runtime_error("stub: 外部源检测失败（环境注入）");
        }
        return probeStatus;
    }

    RelinkExecution relink(const RelinkRequest& /*request*/,
                           const ExternalSourceStatus& status) override
    {
        ++relinkCalls;
        lastRelinkStatus = status;  // 录制（编排核必须原样回传 probe 产物）
        if (relinkThrows) {
            throw std::runtime_error("stub: 重关联提交失败（环境注入）");
        }
        return relinkResult;
    }
};

// =====================================================================
// 桩：用户确认决策端口（IRelinkDecisionPort 脚本化替身——决策可编程，
// 呈现材料录制可断言）
// =====================================================================

class DecisionPortStub final : public workflow::IRelinkDecisionPort {
public:
    RelinkDisposition disposition = RelinkDisposition::Proceed;///< 决策脚本位
    int calls = 0;                     ///< 决策收集次数（NotNeeded 零确认断言）
    ExternalSourceStatus lastStatus{}; ///< 最近呈现材料（检测状态透传断言）

    RelinkDisposition confirmRelink(const ExternalSourceStatus& status) override
    {
        ++calls;
        lastStatus = status;
        return disposition;
    }
};

/// 检测状态夹具（变化形态——登记/现内容对照材料齐备）。
ExternalSourceStatus changedStatus()
{
    ExternalSourceStatus status;
    status.state = ExternalSourceState::Changed;
    status.externalRefId = "ext-robot-mesh";
    status.absolutePath = "D:/assets/robot-mesh.stl";
    status.recordedHash256 = std::string(64, 'a');
    status.recordedSizeBytes = 1024;  // 登记基准：1024 字节
    status.currentHash256 = std::string(64, 'b');
    status.currentSizeBytes = 2048;   // 现内容：2048 字节（已变化）
    return status;
}

// =====================================================================
// PM-06：升级指引三键提取（parseUpgradeGuidance——对端 detail 零加工
// 透传；黄金期望值即 project Codec 组装词形）
// =====================================================================

/// schema-future detail 三键齐备：document/supported/upgrade 逐字段
/// 黄金值断言（PM-06"显示当前版本、项目版本、升级工具入口"）。
TEST(WfReconnect, ParseGuidance_SchemaFuture_ThreeKeys_P06)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-06"}, std::vector<std::string>{});

    const workflow::UpgradeGuidance guidance =
        workflow::parseUpgradeGuidance(kSchemaFutureDetail);
    // 三键黄金值：document=20000（项目版本）/supported=10000（当前支持）
    // /upgrade=ISchemaUpgrader-stage-b（升级工具入口）——零加工透传。
    EXPECT_EQ(guidance.documentVersion, "20000");
    EXPECT_EQ(guidance.supportedVersion, "10000");
    EXPECT_EQ(guidance.upgradeToolEntry, "ISchemaUpgrader-stage-b");
}

/// format-legacy（版本旧）detail：document/supported 提取，upgrade 留空
/// （旧格式无升级入口——字段空串如实呈现，PM-06 不伪造）。
TEST(WfReconnect, ParseGuidance_LegacyVersion_TwoKeys_P06)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-06"}, std::vector<std::string>{});

    const workflow::UpgradeGuidance guidance =
        workflow::parseUpgradeGuidance(kLegacyVersionDetail);
    EXPECT_EQ(guidance.documentVersion, "0");
    EXPECT_EQ(guidance.supportedVersion, "10000");
    EXPECT_EQ(guidance.upgradeToolEntry, "");  // 无 upgrade 键——空串
}

/// format-legacy（formatId 不符）detail："document-format-id=" 键形不得
/// 被误读为 document（键探针 " document=" 与 " document-format-id=" 是
/// 不同词形——前导空格＋'=' 防同形子串误配）。
TEST(WfReconnect, ParseGuidance_LegacyFormatId_NotMisread_P06)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-06"}, std::vector<std::string>{});

    const workflow::UpgradeGuidance guidance =
        workflow::parseUpgradeGuidance(kLegacyFormatIdDetail);
    EXPECT_EQ(guidance.documentVersion, "");  // 不误读 document-format-id
    EXPECT_EQ(guidance.supportedVersion, "rwdesign");  // 对端词形原样
    EXPECT_EQ(guidance.upgradeToolEntry, "");
}

/// 无升级键的 detail（如 not-a-project）：三字段全空——尽力呈现，
/// 不伪造数值（PM-06 数据面只服务于旧格式/未来版本失败面）。
TEST(WfReconnect, ParseGuidance_NoKeys_EmptyFields_P06)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-06"}, std::vector<std::string>{});

    const workflow::UpgradeGuidance guidance = workflow::parseUpgradeGuidance(
        "project/open: project.json 缺失（非项目目录） path=D:/somewhere");
    EXPECT_EQ(guidance.documentVersion, "");
    EXPECT_EQ(guidance.supportedVersion, "");
    EXPECT_EQ(guidance.upgradeToolEntry, "");
}

// =====================================================================
// PM-07：只读会话数据面（buildReadOnlySessionNotice——LockInfo 折叠）
// =====================================================================

/// 锁被持形态：持有者三值透传＋lockHeldByOther=true＋双禁用恒真＋
/// lock-held 提示键（PM-07"锁被持提示 PID；禁编辑与应用提交"）。
TEST(WfReconnect, ReadOnlyNotice_LockHeld_FieldsAndKeys_P07)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-07"}, std::vector<std::string>{});

    project::LockInfo info;
    info.holder.pid = 4321;  // 他方持有者 PID（单位＝OS PID）
    info.holder.host = "workstation-b";
    info.holder.heartbeatUtc = "2026-10-09T08:00:00.000Z";
    info.isSelf = false;  // 只读上下文（他方持锁）

    const ReadOnlySessionNotice notice = workflow::buildReadOnlySessionNotice(info);
    EXPECT_TRUE(notice.readOnly);            // 只读会话自明位
    EXPECT_EQ(notice.holderPid, 4321u);      // PID 透传（提示数据源）
    EXPECT_EQ(notice.holderHost, "workstation-b");
    EXPECT_EQ(notice.holderHeartbeatUtc, "2026-10-09T08:00:00.000Z");
    EXPECT_TRUE(notice.lockHeldByOther);     // "锁被持"形态
    EXPECT_TRUE(notice.editingDisabled);     // 禁编辑（恒真——PM-07）
    EXPECT_TRUE(notice.applyCommitDisabled); // 禁应用提交（恒真——PM-07）
    EXPECT_EQ(notice.noticeKey, workflow::kReadOnlyLockHeldKey);  // lock-held 键
}

/// 撕裂读/显式只读形态（pid=0）：lockHeldByOther=false（无可提示 PID）＋
/// explicit 键；PID 原样 0 透传——呈现层按"未知"呈现而非显示 PID=0
/// （LockHolderRecord 注释口径；数据面不伪造 PID）。
TEST(WfReconnect, ReadOnlyNotice_ZeroPid_ExplicitKey_P07)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-07"}, std::vector<std::string>{});

    project::LockInfo info;  // 默认＝pid 0/host 空/hb 空/isSelf false
    const ReadOnlySessionNotice notice = workflow::buildReadOnlySessionNotice(info);
    EXPECT_EQ(notice.holderPid, 0u);       // 原样透传（未知语义归呈现层）
    EXPECT_FALSE(notice.lockHeldByOther);  // 无他方可提示
    EXPECT_TRUE(notice.editingDisabled);
    EXPECT_TRUE(notice.applyCommitDisabled);
    EXPECT_EQ(notice.noticeKey, workflow::kReadOnlyExplicitKey);  // explicit 键
}

/// 本上下文自持锁形态（isSelf=true）：lockHeldByOther=false——自持不
/// 提示"被他人持有"（本面只在只读会话消费；自持形态是防御性输入）。
TEST(WfReconnect, ReadOnlyNotice_SelfHeld_NotReportedAsOther_P07)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-07"}, std::vector<std::string>{});

    project::LockInfo info;
    info.holder.pid = 99;
    info.isSelf = true;  // 本上下文持有（writable 形态——防御性输入）
    const ReadOnlySessionNotice notice = workflow::buildReadOnlySessionNotice(info);
    EXPECT_FALSE(notice.lockHeldByOther);
    EXPECT_EQ(notice.noticeKey, workflow::kReadOnlyExplicitKey);
}

// =====================================================================
// PM-09：重关联编排核全分支（RelinkFlow::run——三端口桩直调）
// =====================================================================

/// 前置 fail-fast：资源身份无效（全零保留值）→ WorkflowError（调用方
/// 契约违约——重关联入口须携带有效资源 ObjectId）。
TEST(WfReconnect, RelinkFlow_InvalidResource_FailFast_P09)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-09"}, std::vector<std::string>{"AT-21"});

    RelinkPortStub port;
    DecisionPortStub decisions;
    RelinkRequest request;  // resource 默认全零——无效
    EXPECT_THROW(workflow::RelinkFlow::run(request, port, decisions),
                 WorkflowError);
    EXPECT_EQ(port.probeCalls, 0);  // 前置拦截——零触达
}

/// 无事实早退：检测 Ok（无缺失无变化）→ NotNeeded＋零确认＋零提交
/// （无重关联事实不产生无意义修订——"显式提交"语义的边界）。
TEST(WfReconnect, RelinkFlow_Ok_NotNeeded_ZeroCommit_P09)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-09"}, std::vector<std::string>{});

    RelinkPortStub port;
    port.probeStatus.state = ExternalSourceState::Ok;
    DecisionPortStub decisions;

    RelinkRequest request;
    request.resource = core::ObjectId::generate();
    const RelinkOutcome outcome =
        workflow::RelinkFlow::run(request, port, decisions);

    EXPECT_EQ(outcome.result, RelinkOutcome::Result::NotNeeded);
    EXPECT_FALSE(outcome.revisionId.has_value());  // 零修订
    EXPECT_FALSE(outcome.failure.has_value());     // 非错误
    EXPECT_EQ(port.probeCalls, 1);                 // 检测恰好一次
    EXPECT_EQ(decisions.calls, 0);                 // 零确认（无事实不烦用户）
    EXPECT_EQ(port.relinkCalls, 0);                // 零提交
}

/// 主线：检测 Missing＋用户确认 Proceed＋提交成功 → Relinked＋新修订
/// 回传（AT-21 观测点"显式提交产生新修订"）＋检测材料透传到决策端口
/// 与提交端口（呈现材料同源——probe 产物未经篡改）。
TEST(WfReconnect, RelinkFlow_Missing_Proceed_Committed_P09_AT21)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-09"}, std::vector<std::string>{"AT-21"});

    RelinkPortStub port;
    port.probeStatus = changedStatus();
    port.probeStatus.state = ExternalSourceState::Missing;  // 外部源缺失
    port.relinkResult.relinked = true;
    port.relinkResult.revisionId = core::RevisionId::generate();
    DecisionPortStub decisions;
    decisions.disposition = RelinkDisposition::Proceed;

    RelinkRequest request;
    request.resource = core::ObjectId::generate();
    request.newPath = "D:/assets/moved/mesh.stl";  // 重定向新路径
    const RelinkOutcome outcome =
        workflow::RelinkFlow::run(request, port, decisions);

    EXPECT_EQ(outcome.result, RelinkOutcome::Result::Relinked);
    ASSERT_TRUE(outcome.revisionId.has_value());  // 新修订回传（AT-21）
    EXPECT_EQ(outcome.revisionId.value(), port.relinkResult.revisionId);
    EXPECT_FALSE(outcome.failure.has_value());
    // 检测材料透传：决策端口与提交端口收到的都是 probe 产物（对照
    // 材料呈现/提交载荷同源——编排核零加工）。
    EXPECT_EQ(decisions.lastStatus, port.probeStatus);
    EXPECT_EQ(port.lastRelinkStatus, port.probeStatus);
    EXPECT_EQ(port.lastProbeRequest, request);  // 请求原样触达检测端口
    EXPECT_EQ(port.relinkCalls, 1);             // 显式提交恰好一次
}

/// 取消分支：用户 Cancel → Canceled＋零提交＋failure 空（取消非错误
/// ——UX-03；无修订无诊断）。
TEST(WfReconnect, RelinkFlow_Cancel_NoCommit_UX03_P09)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-09"}, std::vector<std::string>{"AT-21"});

    RelinkPortStub port;
    port.probeStatus = changedStatus();  // Changed——有事实，走到确认点
    DecisionPortStub decisions;
    decisions.disposition = RelinkDisposition::Cancel;

    RelinkRequest request;
    request.resource = core::ObjectId::generate();
    const RelinkOutcome outcome =
        workflow::RelinkFlow::run(request, port, decisions);

    EXPECT_EQ(outcome.result, RelinkOutcome::Result::Canceled);
    EXPECT_FALSE(outcome.revisionId.has_value());
    EXPECT_FALSE(outcome.failure.has_value());  // 取消不产诊断（UX-03）
    EXPECT_EQ(decisions.calls, 1);              // 确认点恰好一次
    EXPECT_EQ(port.relinkCalls, 0);             // 零提交
}

/// 提交被拒（relinked=false＋cause/action）：Failed＋UX-03 三字段透传
/// ＋revisionId 恒空（失败不带病报成功）。
TEST(WfReconnect, RelinkFlow_SubmitRejected_UX03Folded_P09)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-09"}, std::vector<std::string>{"AT-21"});

    RelinkPortStub port;
    port.probeStatus = changedStatus();
    port.relinkResult.relinked = false;
    port.relinkResult.cause = "project/command: 提交被拒（模拟对端拒绝）";
    port.relinkResult.action = "请检查项目写权限后重试";
    DecisionPortStub decisions;  // 默认 Proceed

    RelinkRequest request;
    request.resource = core::ObjectId::generate();
    const RelinkOutcome outcome =
        workflow::RelinkFlow::run(request, port, decisions);

    EXPECT_EQ(outcome.result, RelinkOutcome::Result::Failed);
    ASSERT_TRUE(outcome.failure.has_value());
    // UX-03 三字段：context＝资源定位词形（ObjectId＋登记路径组合——
    // relinkContextText 组装口径）；cause/action＝端口半区透传（零加工）。
    EXPECT_EQ(outcome.failure->context,
              request.resource.toCanonical() + "（"
                  + port.probeStatus.absolutePath + "）");
    EXPECT_EQ(outcome.failure->cause, "project/command: 提交被拒（模拟对端拒绝）");
    EXPECT_EQ(outcome.failure->recommendedAction, "请检查项目写权限后重试");
    EXPECT_FALSE(outcome.revisionId.has_value());  // 无修订
}

/// 端口违约形态（relinked=true 但修订身份无效）：如实转 Failed——
/// "提交成功必须有修订"的编排承诺被破坏时不带病报成功（同 T07 清理
/// 观测位纪律）。
TEST(WfReconnect, RelinkFlow_SuccessWithoutRevision_TransparentFail_P09)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-09"}, std::vector<std::string>{"AT-21"});

    RelinkPortStub port;
    port.probeStatus = changedStatus();
    port.relinkResult.relinked = true;
    port.relinkResult.revisionId = core::RevisionId{};  // 全零——无效修订
    DecisionPortStub decisions;

    RelinkRequest request;
    request.resource = core::ObjectId::generate();
    const RelinkOutcome outcome =
        workflow::RelinkFlow::run(request, port, decisions);

    EXPECT_EQ(outcome.result, RelinkOutcome::Result::Failed);  // 不带病报成功
    ASSERT_TRUE(outcome.failure.has_value());
    EXPECT_FALSE(outcome.failure->recommendedAction.empty());  // 三字段齐
    EXPECT_FALSE(outcome.revisionId.has_value());
}

/// 端口环境异常折叠：probe 抛 → Failed（cause=what() 透传；status 保持
/// 默认——无检测材料）；relink 抛 → Failed（同轨）。两段都不产生修订。
TEST(WfReconnect, RelinkFlow_PortThrows_FoldedToFailed_P09)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-09"}, std::vector<std::string>{"AT-21"});

    // 半区一：probe 抛。
    {
        RelinkPortStub port;
        port.probeThrows = true;
        DecisionPortStub decisions;
        RelinkRequest request;
        request.resource = core::ObjectId::generate();
        const RelinkOutcome outcome =
            workflow::RelinkFlow::run(request, port, decisions);
        EXPECT_EQ(outcome.result, RelinkOutcome::Result::Failed);
        ASSERT_TRUE(outcome.failure.has_value());
        EXPECT_EQ(outcome.failure->cause,
                  "stub: 外部源检测失败（环境注入）");  // what() 透传
        EXPECT_FALSE(outcome.revisionId.has_value());
        EXPECT_EQ(port.relinkCalls, 0);  // 检测失败——零提交
    }
    // 半区二：确认后 relink 抛。
    {
        RelinkPortStub port;
        port.probeStatus = changedStatus();
        port.relinkThrows = true;
        DecisionPortStub decisions;  // Proceed
        RelinkRequest request;
        request.resource = core::ObjectId::generate();
        const RelinkOutcome outcome =
            workflow::RelinkFlow::run(request, port, decisions);
        EXPECT_EQ(outcome.result, RelinkOutcome::Result::Failed);
        ASSERT_TRUE(outcome.failure.has_value());
        EXPECT_EQ(outcome.failure->cause,
                  "stub: 重关联提交失败（环境注入）");
        EXPECT_FALSE(outcome.revisionId.has_value());
    }
}

}  // namespace
