/**
 * @file   ObjectTypesTest.cpp
 * @brief  modeling 对象类型登记用例组（MdlObjTypes）——五对象
 *         objectTypeToken 与 SceneObjectRole 词表（任务契约 WP-13-T02
 *         acceptance 4 的 ObjectTypes.hpp 行具名自证）。
 *
 * 设计依据：
 *   - units/modeling.md §4.2（五对象 token 权威表——"根对象 token 与
 *     runtime 已落位常量 kRobotDesignObjectType 字面一致，不得另设第二
 *     常量"）、§4.5（SceneObjectRole 词表五值——词表所有者＝modeling）、
 *     §14.4（建模侧正式登记）
 *   - units/policy.md §6.1（policy 侧消费用镜像词表——跨单元 token 一致
 *     性在本文件钉住：词表所有者 modeling，policy 消费 token）
 *   - 任务契约 tasks/foundation/WP-13-T02.json acceptance 4
 */

#include <sdurws/ird/modeling/ObjectTypes.hpp>

#include <sdurws/ird/policy/PolicyParsing.hpp>  // policy 消费侧冻结词表 sceneObjectRoleTokens()——跨单元 token 一致性核对（六边登记边 policy 的公共头；PolicyParsing.cpp 双模式编译，冒烟可解析）
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯登记（testkit §7.3）

#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <vector>

using sdurws::ird::modeling::SceneObjectRole;
using sdurws::ird::modeling::kNamedPoseSetObjectType;
using sdurws::ird::modeling::kRobotDesignObjectType;
using sdurws::ird::modeling::kRobotDrivetrainObjectType;
using sdurws::ird::modeling::kSceneObjectObjectType;
using sdurws::ird::modeling::kToolDefinitionObjectType;
using sdurws::ird::modeling::sceneObjectRoleToken;
using sdurws::ird::modeling::trySceneObjectRole;

/// policy 单元命名空间别名（消费侧冻结词表的引用面——测试可读性）。
namespace policy = sdurws::ird::policy;

namespace {

/// §4.2 表的 token 字面清单（独立于实现——测试内自持字面量，与实现常量
/// 机械比对，任一侧漂移即失败：失同步防线，io §9.12 比对同款）。
constexpr const char* kCardSection42Tokens[] = {
    "robot-design", "tool-definition", "scene-object",
    "named-pose-set", "robot-drivetrain",
};

}  // namespace

/**
 * robot-design token 与 runtime 权威常量**实体同一**（acceptance 4——
 * "不另设第二常量"的机器可证形态）：modeling::kRobotDesignObjectType 是
 * using 声明引入的 runtime::kRobotDesignObjectType 本体，二者地址相等
 * ＝同一常量对象，不存在值拷贝出来的第二份（值相等不足以证明实体同一
 * ——地址相等才是"同一存储"的判据）。
 */
TEST(MdlObjTypes, RobotDesignTokenIsRuntimeConstantEntity_WP13T02_ACC4)
{
    // 追溯登记（ird-test-report.json 需求/AT 字段——testkit §7.3）。
    IRD_TEST_INFO(std::vector<std::string>{"ARC-04", "MDL-14"},
                  std::vector<std::string>{});

    // 实体同一性：地址相等（using 引入的是同一实体，不是第二常量——
    // modeling.md §4.2 原文纪律的运行期证据）。
    EXPECT_EQ(&sdurws::ird::modeling::kRobotDesignObjectType,
              &sdurws::ird::runtime::kRobotDesignObjectType)
        << "modeling 的 robot-design token 必须是 runtime 权威常量本体"
           "（using 引入，§4.2 不得另设第二常量）";
    // 字面一致（路由键语义——runtime S2 以 "robot-design" 定位根对象）。
    EXPECT_STREQ(sdurws::ird::runtime::kRobotDesignObjectType, "robot-design");
}

/**
 * 五对象 token 全表机械比对（§4.2 权威表）：实现常量逐值等于卡面字面
 * 清单——五对象类型登记的封闭性（多登记/漏登记/改拼均在此暴露）。
 */
TEST(MdlObjTypes, FiveObjectTypeTokensMatchCardSection42_WP13T02_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"ARC-04", "CON-01"},
                  std::vector<std::string>{});

    EXPECT_EQ(std::string_view(kRobotDesignObjectType), std::string_view(kCardSection42Tokens[0]));
    EXPECT_EQ(kToolDefinitionObjectType, std::string_view(kCardSection42Tokens[1]));
    EXPECT_EQ(kSceneObjectObjectType, std::string_view(kCardSection42Tokens[2]));
    EXPECT_EQ(kNamedPoseSetObjectType, std::string_view(kCardSection42Tokens[3]));
    EXPECT_EQ(kRobotDrivetrainObjectType, std::string_view(kCardSection42Tokens[4]));
}

/**
 * SceneObjectRole 词表五值与 token 串（§4.5 词表原文序与五串）：
 * 枚举恰五值（编译期数组全列——新增枚举值会使计数断言暴露）、逐值
 * token＝词表串（"RobotLink"/"Tool"/"Payload"/"EnvironmentObject"/
 * "Workpiece"）。
 */
TEST(MdlObjTypes, SceneObjectRoleVocabularyFiveValues_WP13T02_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-15"},
                  std::vector<std::string>{});

    // 词表五值 token（§4.5 括注五串，顺序＝枚举声明序）。
    const std::string_view kExpected[] = {
        "RobotLink", "Tool", "Payload", "EnvironmentObject", "Workpiece"};
    const SceneObjectRole kAll[] = {
        SceneObjectRole::RobotLink, SceneObjectRole::Tool,
        SceneObjectRole::Payload, SceneObjectRole::EnvironmentObject,
        SceneObjectRole::Workpiece};
    for (std::size_t i = 0; i < 5; ++i) {
        EXPECT_EQ(sceneObjectRoleToken(kAll[i]), kExpected[i])
            << "角色枚举序 " << i << " 的 token 与 §4.5 词表不符";
    }
}

/**
 * 角色 token 与 policy 消费侧冻结词表逐值同串（跨单元一致性）：词表
 * 所有者＝modeling（§4.5 原文），policy.md §4.3 注明"归建模语义，本文
 * 消费"——两侧登记若失同步，策略规则核对与建模对象字节将各说各话。
 * 比对面＝policy::sceneObjectRoleTokens()（PolicyParsing 的解析期冻结
 * 词表，五值顺序＝枚举声明序；PolicyParsing.cpp 双模式编译——冒烟模式
 * 同样可解析，不消费仅集成编译的 CollisionEvaluator TU）。
 */
TEST(MdlObjTypes, RoleTokensAgreeWithPolicyConsumerMirror_WP13T02_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-15"},
                  std::vector<std::string>{});

    const std::string_view kModeled[] = {
        sceneObjectRoleToken(SceneObjectRole::RobotLink),
        sceneObjectRoleToken(SceneObjectRole::Tool),
        sceneObjectRoleToken(SceneObjectRole::Payload),
        sceneObjectRoleToken(SceneObjectRole::EnvironmentObject),
        sceneObjectRoleToken(SceneObjectRole::Workpiece)};
    const auto kPolicyTokens = policy::sceneObjectRoleTokens();
    ASSERT_EQ(kPolicyTokens.size(), 5U)
        << "policy 冻结词表应恰五值（§4.3 同源词表）";
    for (std::size_t i = 0; i < 5; ++i) {
        EXPECT_EQ(kModeled[i], kPolicyTokens[i])
            << "角色序 " << i << "：modeling 词表串与 policy 消费侧冻结词表"
               "不同串（词表所有者 modeling——policy 侧镜像须随建模侧同步）";
    }
}

/**
 * 角色词表 try 轨（往返＋词表外不猜测）：五值逐一 token→枚举往返一致；
 * 空串/大小写变体/未知串一律 nullopt（ARC-04"不猜测"——无大小写折叠、
 * 无空白剥离）。
 */
TEST(MdlObjTypes, RoleTokenRoundTripAndUnknownRejected_WP13T02_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"ARC-04", "MDL-15"},
                  std::vector<std::string>{});

    const SceneObjectRole kAll[] = {
        SceneObjectRole::RobotLink, SceneObjectRole::Tool,
        SceneObjectRole::Payload, SceneObjectRole::EnvironmentObject,
        SceneObjectRole::Workpiece};
    // 往返半区：token(role) 经 trySceneObjectRole 还原为同一枚举值。
    for (const SceneObjectRole role : kAll) {
        const auto back = trySceneObjectRole(sceneObjectRoleToken(role));
        ASSERT_TRUE(back.has_value()) << "词表内 token 必须可还原";
        EXPECT_EQ(*back, role) << "token 往返不一致（确定性破坏——NFR-COR-02）";
    }
    // 词表外半区：空串/大小写变体/未知串→nullopt（不猜测）。
    EXPECT_FALSE(trySceneObjectRole("").has_value());
    EXPECT_FALSE(trySceneObjectRole("robotlink").has_value())
        << "大小写折叠被禁止——拼写变体一律词表外";
    EXPECT_FALSE(trySceneObjectRole("RobotLink ").has_value())
        << "前后空白不剥离——精确等值比较";
    EXPECT_FALSE(trySceneObjectRole("Fixture").has_value());
}
