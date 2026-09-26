/**
 * @file   ErrorsTest.cpp
 * @brief  kinematics 错误契约用例组（KinErrors）——KinematicsErrorCode
 *         全表 token、KinematicsError 值语义与映射分批纪律（任务契约
 *         WP-15-T02 acceptance 4 的 Errors.hpp 行具名自证）。
 *
 * 设计依据：
 *   - units/kinematics.md §9.2 IFkEvaluator @错误 行（4 值收编出处与
 *     顺序权威）、§5.2 非法输入行（IllegalQ 语义——NFR-COR-03 不钳制
 *     不置零）、§9.1（非异常出口——Expected<T, KinematicsError> 的 E
 *     形态）、§9.6 表头分批纪律（映射行随消费任务登记）
 *   - 先例：requirements/test/ErrorsTest.cpp（WP-14-T02 同款——token
 *     全表机械比对＋值语义＋映射 nullopt 契约）
 *   - 需求 NFR-COR-03（非法输入拒绝）、NFR-COR-02（同码同串确定性）
 *   - 任务契约 tasks/foundation/WP-15-T02.json acceptance 4
 */

#include <sdurws/ird/kinematics/Errors.hpp>

#include <sdurws/ird/kinematics/DiagCodes.hpp>  // kKinNoDevice/kKinNoTcp——映射行同串核对（T03）

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯登记（testkit §7.3）

#include <gtest/gtest.h>

#include <string>
#include <utility>
#include <vector>

using sdurws::ird::kinematics::KinematicsError;
using sdurws::ird::kinematics::KinematicsErrorCode;
using sdurws::ird::kinematics::kinematicsDiagCode;
using sdurws::ird::kinematics::kinematicsErrorCodeToken;

namespace {

/// §9.2 IFkEvaluator @错误 行的**应登记码面**（测试内自持字面清单——与
/// 实现 token 表机械比对，任一侧漂移即失败：失同步防线）。行原文序＝
/// 枚举声明序（文件头收编口径）；T03+ 接口落地新增域错误名时在枚举与
/// 本清单表尾同步追加（表尾追加纪律——不重排既有值）。
struct ExpectedErrorToken {
    KinematicsErrorCode code;
    const char* token;
};

const ExpectedErrorToken kSection92ErrorTokens[] = {
    {KinematicsErrorCode::IllegalQ,        "IllegalQ"},
    {KinematicsErrorCode::NoDevice,        "NoDevice"},
    {KinematicsErrorCode::NoTcp,           "NoTcp"},
    {KinematicsErrorCode::FrameUnresolved, "FrameUnresolved"},
};

}  // namespace

/**
 * token 全表机械比对（acceptance 4——Errors.hpp 行）：全表 4 值逐一与
 * §9.2 @错误 行成员名原文同串；token 是静态存储期字面量（同码跨进程
 * 逐字节一致——NFR-COR-02 的码面子集）。
 */
TEST(KinErrors, ErrorCodeTokensMatchSection92Row_WP15T02_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-02"},
                  std::vector<std::string>{});

    // 规模钉住：§9.2 @错误 行恰 4 值——实现侧多登（收编卡面之外的名字）
    // 或漏登均在此暴露（收编口径"为什么是这 4 值"的机械面）。
    ASSERT_EQ(std::size(kSection92ErrorTokens), 4U);

    for (const auto& expected : kSection92ErrorTokens) {
        EXPECT_EQ(kinematicsErrorCodeToken(expected.code), expected.token)
            << "§9.2 @错误 行 token 失同步: 枚举值 " << static_cast<int>(expected.code);
    }
}

/**
 * KinematicsError 值语义（acceptance 4——§9.1 非异常出口的值承载）：
 * params 保序（构造序＝遍历序，不经哈希/树序重排——确定性 NFR-COR-01/02）、
 * detail 可携带定位上下文、code 显式赋值面可用（缺省值仅供聚合容器留位，
 * 不作为有效错误语义——头注契约的断言面）。
 */
TEST(KinErrors, ErrorValueSemanticsOrderedParams_WP15T02_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-01", "NFR-COR-02"},
                  std::vector<std::string>{});

    // 构造带序参数表与细节的错误值（模拟 §9.2 evaluate 的拒绝出口形态
    // ——IllegalQ：q 维度不符，NFR-COR-03 不钳制不置零的值面）。
    KinematicsError err;
    err.code = KinematicsErrorCode::IllegalQ;
    err.params.emplace_back("expected-dof", "6");
    err.params.emplace_back("actual-dof", "5");
    err.detail = "q 维度与设备自由度不符";

    // 保序断言：vector 遍历序＝构造序（键值对逐一核对，不排序不重排）。
    ASSERT_EQ(err.params.size(), 2U);
    EXPECT_EQ(err.params[0], std::make_pair(std::string("expected-dof"), std::string("6")));
    EXPECT_EQ(err.params[1], std::make_pair(std::string("actual-dof"), std::string("5")));
    EXPECT_EQ(err.detail, "q 维度与设备自由度不符");
    EXPECT_EQ(err.code, KinematicsErrorCode::IllegalQ);

    // 缺省构造＝枚举首值（聚合容器留位语义——头注契约；不作为有效错误）。
    EXPECT_EQ(KinematicsError{}.code, KinematicsErrorCode::IllegalQ);
}

/**
 * 映射行随消费任务登记（§9.6 分批纪律的错误面执行口径——本用例随其
 * 消费者任务同步修订）：T03 落位 IFkEvaluator/KinTypes/Fk 后，NoDevice/
 * NoTcp 两行到站——映射返回 DiagCodes.hpp 同名注册码常量（码面/映射面
 * 同串交叉核对）；IllegalQ/FrameUnresolved 无 §9.6 同义码行维持 nullopt
 * （nullopt 契约＝不得产诊断，错误经值面返回——禁字符串拼码、禁私定
 * 新码值凑数）。
 */
TEST(KinErrors, DiagCodeMappingRowsStagedWithConsumers_WP15T02ACC4_WP15T03)
{
    IRD_TEST_INFO(std::vector<std::string>{"ERR-01"},
                  std::vector<std::string>{});

    // T03 登记的两行：映射值与 DiagCodes.hpp 注册常量逐字同串
    // （码值权威＝diagnostics StableCodeRegistry——映射只消费常量）。
    const auto noDevice = kinematicsDiagCode(KinematicsErrorCode::NoDevice);
    ASSERT_TRUE(noDevice.has_value()) << "NoDevice 映射行应随 T03 登记在案";
    EXPECT_EQ(*noDevice, sdurws::ird::kinematics::kKinNoDevice);
    const auto noTcp = kinematicsDiagCode(KinematicsErrorCode::NoTcp);
    ASSERT_TRUE(noTcp.has_value()) << "NoTcp 映射行应随 T03 登记在案";
    EXPECT_EQ(*noTcp, sdurws::ird::kinematics::kKinNoTcp);

    // 无 §9.6 同义码行的两值维持 nullopt（调用方不得产诊断——值面返回）。
    EXPECT_FALSE(kinematicsDiagCode(KinematicsErrorCode::IllegalQ).has_value());
    EXPECT_FALSE(kinematicsDiagCode(KinematicsErrorCode::FrameUnresolved).has_value());
}
