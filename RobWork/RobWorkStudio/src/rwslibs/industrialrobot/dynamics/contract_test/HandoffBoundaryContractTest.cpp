/**
 * @file   HandoffBoundaryContractTest.cpp
 * @brief  关节侧序列交接面对账契约（DynHandoffBoundary）——WP-17-T10
 *         acceptance 3 的"与 selection/drivetrain 卡的载荷对齐（P-DYN-2/
 *         P-DYN-5：对端卡已落盘 v0.1，消费键以卡内登记为准）"机器钉扎面。
 *
 * 设计依据：
 *   - units/dynamics.md §8.2（drivetrain 交接——DYN-04：dynamics 只交付关
 *     节侧结果与消费契约，映射唯一实现归 drivetrain；D-DYN-5 不内嵌映射
 *     调用）、§9.3（selection 协作——P-DYN-2）、§8.4（评估键
 *     dyn-rnea-analysis／payload kindToken dyn.rnea-analysis.v1 登记值）、
 *     §13.1（交接形态＝JointSideSeriesPack DTO——落位随交接任务，本批零
 *     预建，NFR-MNT-04）
 *   - 对端卡登记（v0.1 已落盘——消费键以卡内登记为准）：
 *     units/drivetrain.md §12.1/P-DT-6：UpstreamResult 依赖声明键
 *     `dyn.joint-series`（"对齐前本卡评估器的 UpstreamResult 依赖声明使
 *     用本表提议键……dynamics 卡产出时如更名按注册清单同步"——dynamics
 *     卡接受该键、不更名）；R1 依赖声明＝model.drivetrain(Object,Required)
 *     ＋dyn.joint-series(UpstreamResult,Required) 两条（§18.3 实现偏差行）。
 *     units/selection.md §9.1/§9.2：组合校核切片条目 UpstreamResult
 *     `dyn.joint-series`（引用 drivetrain 卡 §12.1 提议契约）。
 *   - 需求 DYN-04（关节侧结果与候选传动无关）、AT-38（R1 交接面）；任务
 *     契约 tasks/foundation/WP-17-T10.json acceptance 3
 *
 * 对账模式（T08 View3D 契约先例——R-1 禁止跨单元 include，对端值以测试
 * 自持常量按同一文档出处字面冻结，任一侧漂移即两侧测试之一显性失败）：
 * 本文件冻结对端卡的登记键与本卡 §8.4 的产出身份 token，并登记 §4.4
 * JointSideSeriesPack 字段面与对端 §12.1 DynamicsJointSeries 提议 DTO 字段
 * 面的字段级对应声明（载荷对齐登记——DTO 物化随交接任务，本批零产品代
 * 码，仅契约面冻结）。
 *
 * 线程约束：纯编译期/值断言，无共享状态。
 */

#include <sdurws/ird/dynamics/EvidenceBuilder.hpp>  // kDynProfileId/dyn Profile 登记值（同卡 §8.4 出处）
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp> // IRD_TEST_INFO（追溯登记——展开于消费 TU）

#include <gtest/gtest.h>

#include <cstddef>
#include <string_view>

namespace dyn = sdurws::ird::dynamics;
using namespace sdurws::ird::testkit;  // IRD_TEST_INFO 展开为无命名空间限定的 irdTestInfo

// =====================================================================
// 对端卡登记键与本卡产出身份（同一文档出处字面冻结——对账锚）
// =====================================================================

namespace {

/// 关节侧序列的消费入口键（units/drivetrain.md §12.1/P-DT-6 登记提议键；
/// units/selection.md §9.1/§9.2 消费同键——对端卡 v0.1 已落盘，消费键以
/// 卡内登记为准，dynamics 卡接受该键、不更名〔P-DYN-2/P-DYN-5 对齐裁决〕）。
constexpr std::string_view kPeerUpstreamKey = "dyn.joint-series";

/// 对端 R1 依赖声明的另一条目（units/drivetrain.md §18.3 实现偏差行：
/// "R1 评估器依赖声明只含 model.drivetrain(Object,Required)＋
/// dyn.joint-series(UpstreamResult,Required) 两条"）。
constexpr std::string_view kPeerModelEntry = "model.drivetrain";

/// 本卡评估键（units/dynamics.md §8.4 登记值——kebab 词形）。
constexpr std::string_view kDynEvaluationKey = "dyn-rnea-analysis";

/// 本卡 payload kindToken（units/dynamics.md §8.4 登记值——canonical magic
/// IRDDYNA1 的 v1 载荷）。
constexpr std::string_view kDynPayloadKindToken = "dyn.rnea-analysis.v1";

/// 评估键词形闸门（evidence §9：kebab 词形 [a-z][a-z0-9-]{1,63}）。
constexpr bool isKebabToken(std::string_view s)
{
    if (s.size() < 2 || s.size() > 64) {
        return false;
    }
    if (s.front() < 'a' || s.front() > 'z') {
        return false;
    }
    for (const char c : s) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
        if (!ok) {
            return false;
        }
    }
    return true;
}

/// 关节侧交接包 §4.4 字段面 ↔ 对端 §12.1 DynamicsJointSeries 提议 DTO 字
/// 段面的对应声明（载荷对齐登记——字段级映射表；JointSideSeriesPack DTO
/// 物化随交接任务落位，本表为两侧卡文本对账的机器承载）。
struct FieldAlignment {
    std::string_view peerField;  ///< 对端卡提议 DTO 字段（drivetrain.md §12.1）
    std::string_view dynField;   ///< 本卡 §4.4 JointSideSeriesPack 对应字段
};
constexpr FieldAlignment kFieldAlignment[6] = {
    {"jointIds/jointKinds", "joints[].jointObjectId+jointType"},   // 串联序/类型化（§5.3）
    {"caseGroups", "sourceSeriesId+conditionContentId"},           // 工况分组身份承载
    {"samples[t/q/qd/qdd/tauJoint/P/segmentId/驻留]",
     "joints[].t/q/qd/qdd/generalizedForce/payloadVariant"},       // 逐时刻列（类型化）
    {"loadRef", "trajectoryPayloadId+driveTrainDesignRef"},        // 负载/传动引用（不复制语义）
    {"identity", "sourceSeriesId+algorithmVersion"},               // 切片身份＋运行身份
    {"（R2 耦合矩阵引用位）", "couplingMatrixContentId"},           // R1 恒空、零消费代码（§8.2.4）
};

}  // namespace

// =====================================================================
// 消费键对账（acceptance 3——"消费键以卡内登记为准"的机器判读面）
// =====================================================================

/**
 * 对端卡登记键冻结：drivetrain/selection 消费 dynamics 关节侧序列的
 * UpstreamResult 入口键＝dyn.joint-series（对端卡内登记值）；本卡产出身
 * 份＝评估键 dyn-rnea-analysis＋payload kindToken dyn.rnea-analysis.v1
 * （本卡 §8.4 登记值）。两个平面不同（消费入口键 ≠ 评估键——前者是对端
 * 依赖声明条目、后者是本卡评估器注册键），词面各自冻结、任一侧漂移即
 * 显性失败并强制跨卡对齐复核（P-DYN-2/P-DYN-5 关闭路径的守门面）。
 */
TEST(DynHandoffBoundary, UpstreamKeyFrozenToPeerCardRegistration_WP17T10_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-04"}, std::vector<std::string>{"AT-38"});
    // 词面冻结（对端卡登记值——更名须经对端卡增量修订并同步本测试）。
    EXPECT_EQ(kPeerUpstreamKey, "dyn.joint-series");
    EXPECT_EQ(kPeerModelEntry, "model.drivetrain");
    EXPECT_EQ(kDynEvaluationKey, "dyn-rnea-analysis");
    EXPECT_EQ(kDynPayloadKindToken, "dyn.rnea-analysis.v1");
    // 词形闸门（evidence §9 kebab 词形——评估键登记语法）。
    EXPECT_TRUE(isKebabToken(kDynEvaluationKey));
    // 两个平面的分离性：消费入口键（点分对端提议键）≠ 本卡评估键（kebab）。
    EXPECT_NE(kPeerUpstreamKey, kDynEvaluationKey);
    // 本卡 dyn Profile 登记值仍在册（同卡 §8.4 出处——证据面 token 不因交
    // 接对齐漂移）。
    EXPECT_EQ(dyn::kDynProfileId, "dyn");
}

/**
 * 载荷字段面对应声明（acceptance 3——载荷对齐的登记面）：对端 §12.1
 * DynamicsJointSeries 提议 DTO 六字段面与本卡 §4.4 JointSideSeriesPack 字
 * 段面逐行对应（表行序固定、行行非空——对任一侧的字段面修订，本表即
 * diff 首站；DTO 物化随交接任务落位后，以本表为字段映射的登记基线）。
 */
TEST(DynHandoffBoundary, PayloadFieldFaceAlignmentRegistered_WP17T10_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-04"}, std::vector<std::string>{});
    static_assert(sizeof(kFieldAlignment) / sizeof(kFieldAlignment[0]) == 6,
                  "载荷字段面对应表须恰为六行（对端 §12.1 提议 DTO 字段面）");
    for (const FieldAlignment& row : kFieldAlignment) {
        EXPECT_FALSE(row.peerField.empty()) << "对端字段名不得为空";
        EXPECT_FALSE(row.dynField.empty()) << "本卡对应字段名不得为空";
    }
    // 关键对应行抽钉（类型化与 R2 引用位——DYN-04/§8.2.4 红线字面）。
    EXPECT_NE(kFieldAlignment[0].dynField.find("jointType"), std::string_view::npos)
        << "类型化字段面（转动 N·m/移动 N——DYN-03/04）必须在对应表中";
    EXPECT_NE(kFieldAlignment[5].dynField.find("couplingMatrixContentId"),
              std::string_view::npos)
        << "R2 耦合矩阵引用位必须在对应表中（R1 恒空、零消费代码——§8.2.4）";
}
