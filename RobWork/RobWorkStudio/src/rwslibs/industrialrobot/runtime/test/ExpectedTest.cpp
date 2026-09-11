/**
 * @file   ExpectedTest.cpp
 * @brief  Expected<T,E> 两态语义用例组——成功/失败两态构造、前置违约
 *         fail-fast（不静默吞错）、值语义与 move-only 兼容、查询路径
 *         语义镜像（§7.4 解析接口的错误侧形态）。
 *
 * 设计依据：
 *   - units/runtime.md §3.4（Expected 契约原文：ok()/get()/error()/
 *     ok()/err() 工厂；"查询路径一律提供 Expected 非抛出变体"）、
 *     §7.4（接口属性表——查询"不抛、不默认命中"，错误含回显）
 *   - 需求 NFR-COR-03（非有限/非法单位/引用缺失不得静默转换为 0 或
 *     默认通过——get() 对错误态返回默认值正是该条禁止的静默形态）；
 *     任务契约 tasks/foundation/RT-T02.json（acceptance 3）
 */

#include <sdurws/ird/runtime/Errors.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

namespace {

using namespace sdurws::ird::runtime;
using namespace sdurws::ird::core;

/// 查询路径语义镜像（§7.4 resolveRuntimeName 的最小镜像——不依赖 RT-T05）：
/// 命中表→ok(身份)；未命中→err(UnknownObject＋原名回显)，全程不抛。
Expected<ObjectId, RuntimeResolveError> tryResolve(const std::string& name)
{
    // 夹具表：仅 "IRB6700.joint_1" 一个命中项（表内容与断言分离）。
    if (name == "IRB6700.joint_1") {
        ObjectId id{};
        id.bytes[0] = 0x01;
        return Expected<ObjectId, RuntimeResolveError>::ok(id);
    }
    return Expected<ObjectId, RuntimeResolveError>::err(
        RuntimeResolveError{RuntimeErrorCode::UnknownObject, "未命中", name});
}

}  // namespace

/** EXP-1 成功态：工厂 ok() → ok()==true 且 get() 返回原值（两态之成功侧）。 */
TEST(ExpectedSemantics, OkStateCarriesValue_ACC3)
{
    const auto result = Expected<int, std::string>::ok(42);
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(result.get(), 42) << "成功态必须携带原值（不丢失、不改写）";
}

/** EXP-2 失败态：工厂 err() → ok()==false 且 error() 返回原错误（两态之失败侧）。 */
TEST(ExpectedSemantics, ErrStateCarriesError_ACC3)
{
    const auto result = Expected<int, std::string>::err(std::string{"boom"});
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error(), "boom") << "失败态必须携带原错误（不吞、不改写）";
}

/** EXP-3 不静默吞错（NFR-COR-03）：对错误态 get() 必须 fail-fast 抛出。 */
TEST(ExpectedSemantics, GetOnErrorFailsFast_ACC3_NFR_COR_03)
{
    const auto result = Expected<int, std::string>::err(std::string{"boom"});
    // 若 get() 返回默认 int(0)，调用方会把"查询失败"当"查询结果为 0"——
    // 恰是 NFR-COR-03 禁止的静默转换；故前置违约必须异常终止该误用。
    EXPECT_THROW(result.get(), std::logic_error);
    try {
        (void)result.get();
    } catch (const std::logic_error& ex) {
        // 消息走稳定前缀（runtime/expected/...）——与 RuntimeError 前缀约定同风格。
        EXPECT_STREQ(ex.what(), "runtime/expected/get-on-error: 对错误态调用 get()");
    }
}

/** EXP-4 不静默吞错（对称侧）：对成功态 error() 同样 fail-fast。 */
TEST(ExpectedSemantics, ErrorOnOkFailsFast_ACC3)
{
    const auto result = Expected<int, std::string>::ok(7);
    EXPECT_THROW(result.error(), std::logic_error);
}

/** EXP-5 两态互斥：同一实例任一时刻只持一侧（variant 承载语义）。 */
TEST(ExpectedSemantics, StatesAreMutuallyExclusive_ACC3)
{
    const auto okResult = Expected<int, std::string>::ok(1);
    const auto errResult = Expected<int, std::string>::err(std::string{"e"});
    // ok() 判别与携带内容一一对应：成功态可取值不可取错，失败态相反。
    EXPECT_TRUE(okResult.ok());
    EXPECT_EQ(okResult.get(), 1);
    EXPECT_THROW(okResult.error(), std::logic_error);
    EXPECT_FALSE(errResult.ok());
    EXPECT_EQ(errResult.error(), "e");
    EXPECT_THROW(errResult.get(), std::logic_error);
}

/** EXP-6 值语义：拷贝/移动保留状态与内容（可随值返回、可入容器）。 */
TEST(ExpectedSemantics, CopyAndMovePreserveState_ACC3)
{
    auto original = Expected<int, std::string>::err(std::string{"diagnostic"});
    const auto copied = original;                 // 拷贝——失败态连同错误一起复制
    EXPECT_FALSE(copied.ok());
    EXPECT_EQ(copied.error(), "diagnostic");

    const auto moved = std::move(original);       // 移动——源状态转移到目标
    EXPECT_FALSE(moved.ok());
    EXPECT_EQ(moved.error(), "diagnostic");
}

/** EXP-7 move-only 类型：工厂与访问器无需拷贝（variant emplace 语义）。 */
TEST(ExpectedSemantics, SupportsMoveOnlyTypes_ACC3)
{
    // move-only 成功类型：证明工厂签名（按值＋emplace）不要求 T 可拷贝。
    auto result = Expected<std::unique_ptr<int>, std::string>::ok(
        std::make_unique<int>(123));
    ASSERT_TRUE(result.ok());
    ASSERT_NE(result.get(), nullptr);
    EXPECT_EQ(*result.get(), 123);

    // move-only 错误类型：错误侧同样支持（E 面向未来可携不可拷贝载荷）。
    struct MoveOnlyError {
        explicit MoveOnlyError(int v)
            : value(v)
        {
        }
        MoveOnlyError(MoveOnlyError&&) = default;
        MoveOnlyError& operator=(MoveOnlyError&&) = default;
        MoveOnlyError(const MoveOnlyError&) = delete;
        MoveOnlyError& operator=(const MoveOnlyError&) = delete;
        int value;   ///< 载荷（仅验移动语义，无业务含义）
    };
    auto errResult = Expected<int, MoveOnlyError>::err(MoveOnlyError{9});
    EXPECT_FALSE(errResult.ok());
    EXPECT_EQ(errResult.error().value, 9);
}

/** EXP-8 查询路径语义镜像（§7.4/RT-NM-3）：命中不抛；未命中 err＋回显＋不默认命中。 */
TEST(ExpectedQueryPath, ResolveMirrorNoThrowWithEcho_ACC3)
{
    // 命中：ok 侧携 ObjectId（查询成功不携带任何错误痕迹）。
    const auto hit = tryResolve("IRB6700.joint_1");
    ASSERT_TRUE(hit.ok());
    EXPECT_EQ(hit.get().bytes[0], 0x01);

    // 未命中：不抛、不默认命中（不返回任何伪造 ObjectId）——错误侧携
    // UnknownObject＋原名回显（§7.4 接口属性表）。
    const auto miss = tryResolve("IRB6700.joint_99");
    EXPECT_NO_THROW(miss.ok());
    EXPECT_FALSE(miss.ok());
    const auto& err = miss.error();
    EXPECT_EQ(err.code, RuntimeErrorCode::UnknownObject);
    EXPECT_EQ(err.requestedName, "IRB6700.joint_99") << "错误含原名回显（RT-NM-3）";
}
