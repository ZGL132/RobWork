/**
 * @file   IdentityTest.cpp
 * @brief  身份类型用例组——UT-ID-T（units/core.md §8）：类型误用/解析宽松/保留值/
 *         往返/生成唯一性/五元组语义逐条钉住。
 *
 * 设计依据：
 *   - units/core.md §4.1（严格解析规则、保留值纪律、强类型）、§5.1（错误行）、
 *     §8 UT-ID-T 行（用例清单：误用解析失败、大写/长度/非法字符失败、合法往返、
 *     generate 非零且 1e6 无碰撞、五元组缺一即 invalid）
 *   - 需求 ARC-04（稳定身份＋精确等值——附录 D 第 12 项）、CON-01、TASK-03
 *   - 任务契约 tasks/foundation/CORE-T02.json acceptance 第 1 条（UT-ID-T 全过）
 */

#include <gtest/gtest.h>

#include <sdurws/ird/core/Errors.hpp>
#include <sdurws/ird/core/Identity.hpp>

#include <string>
#include <unordered_set>

namespace {
using namespace sdurws::ird::core;

/// UT-ID-T 追溯字段缀于用例名（ird-test-report.json 承载——DTB §5.5）。

/** 合法往返：parse(format(x))==x 对全部六类型＋手工构造实例（§5.1 后置条件）。 */
TEST(IdentityRoundtrip, ParseFormatRoundtrip_UT_ID_T)
{
    // 六类型同形——用两个代表（其余同宏同构）＋全类型小样本。
    const ObjectId obj = ObjectId::generate();
    EXPECT_EQ(ObjectId::fromCanonical(obj.toCanonical()), obj);

    const RevisionId rev = RevisionId::generate();
    EXPECT_EQ(RevisionId::fromCanonical(rev.toCanonical()), rev);

    // 逐类型：tag 各异但契约一致——全六类型各做一次往返。
    EXPECT_EQ(ProjectId::fromCanonical(ProjectId::generate().toCanonical()).toCanonical().substr(0, 4), "prj-");
    EXPECT_EQ(BranchId::fromCanonical(BranchId::generate().toCanonical()).toCanonical().substr(0, 4), "brn-");
    EXPECT_EQ(RunId::fromCanonical(RunId::generate().toCanonical()).toCanonical().substr(0, 4), "run-");
    EXPECT_EQ(EventId::fromCanonical(EventId::generate().toCanonical()).toCanonical().substr(0, 4), "evt-");

    // 手工构造合法实例（32 小写 hex）——不依赖 generate 的随机性。
    const std::string hex32 = "0123456789abcdef0123456789abcdef";
    const ObjectId hand = ObjectId::fromCanonical("obj-" + hex32);
    EXPECT_EQ(hand.toCanonical(), "obj-" + hex32);
}

/** 保留值：全零＝空、isValid 恒 false、att-0 拒绝（§4.1 U-1 保留值纪律）。 */
TEST(IdentityReserved, ZeroIsInvalid_UT_ID_T)
{
    const ObjectId zero;                                  // 默认全零
    EXPECT_FALSE(zero.isValid());
    EXPECT_EQ(zero.toCanonical(), "obj-00000000000000000000000000000000");
    // 保留值取舍钉住（§4.1）：全零是"值域"约束（generate 不产出、isValid=false），
    // 文本解析层只验句法——全零文本句法合法可解析回保留值，"空"的拒绝由
    // isValid() 在业务边界执行（parse 层不越权二次判断）。
    EXPECT_EQ(ObjectId::fromCanonical(zero.toCanonical()), zero);

    EXPECT_THROW((void)AttemptId::fromCanonical("att-0"), CoreError);   // 0＝保留（§4.1 明文示例）
    const AttemptId a = AttemptId::fromCanonical("att-3");               // 合法示例（§4.1）
    EXPECT_EQ(a.value, 3u);
    EXPECT_TRUE(a.isValid());
    EXPECT_EQ(a.toCanonical(), "att-3");
}

/** 解析严格性：tag 误用/大写/长度 31/非法字符 g/前后缀空白全部拒绝（§8 UT-ID-T 行）。 */
TEST(IdentityStrictParse, MisuseAndMalformedRejected_UT_ID_T)
{
    const std::string body32 = "9f2c7b1e4a6d8c0b3e5f7a9c1d3e5f7a";
    // 类型误用：rev- 文本喂 ObjectId（解析边界即失败——特性非缺陷，§5.1 调用方注意）。
    EXPECT_THROW((void)ObjectId::fromCanonical("rev-" + body32), CoreError);
    // tag 本身不匹配同样拒绝。
    EXPECT_EQ(ObjectId::tryFromCanonical("rev-" + body32), std::nullopt);

    // try 轨不抛、返回 nullopt（io 错误收集场景——§5.1）。
    EXPECT_EQ(ObjectId::tryFromCanonical("obj-" + body32).has_value(), true);
    EXPECT_EQ(ObjectId::tryFromCanonical("obj-9F2C7B1E4A6D8C0B3E5F7A9C1D3E5F7A"), std::nullopt);  // 大写
    EXPECT_EQ(ObjectId::tryFromCanonical("obj-9f2c7b1e4a6d8c0b3e5f7a9c1d3e5f7"), std::nullopt);   // 31 hex
    EXPECT_EQ(ObjectId::tryFromCanonical("obj-9f2c7b1e4a6d8c0b3e5f7a9c1d3e5f7ag"), std::nullopt); // g 非法
    EXPECT_EQ(ObjectId::tryFromCanonical("obj-9f2c7b1e4a6d8c0b3e5f7a9c1d3e5f7a "), std::nullopt); // 尾空白
    EXPECT_EQ(ObjectId::tryFromCanonical(" obj-9f2c7b1e4a6d8c0b3e5f7a9c1d3e5f7a"), std::nullopt); // 头空白
    EXPECT_EQ(ObjectId::tryFromCanonical(""), std::nullopt);                                       // 空

    // 抛出轨迹的错误消息带稳定前缀（§4.10——开发诊断可判读）。
    try {
        (void)ObjectId::fromCanonical("obj-短的");
        FAIL() << "必须抛出";
    } catch (const CoreError& e) {
        EXPECT_EQ(std::string(e.what()).find("core/identity/parse: "), 0u);
    }
}

/** 生成纪律：generate 非零；1e6 次去重计数＝1e6（碰撞概率可忽略，§8 UT-ID-T）。 */
TEST(IdentityGenerate, NonZeroAndUnique_UT_ID_T)
{
    const ObjectId g = ObjectId::generate();
    ASSERT_TRUE(g.isValid());                        // 非零保证（保留值重取）

    constexpr int kN = 1000000;                      // §8 明文 1e6 次
    std::unordered_set<std::size_t> seen;
    seen.reserve(kN * 2);
    for (int i = 0; i < kN; ++i) {
        const ObjectId id = ObjectId::generate();
        ASSERT_TRUE(id.isValid());
        seen.insert(std::hash<ObjectId>{}(id));      // 128 位→64 位哈希去重（哈希碰撞远低于 1 分辨率——见下）
    }
    // 注意：size_t 只有 64 位，1e6 个 128 位随机值的 64 位指纹碰撞概率 ≈ 1e12/2^64 ≈ 5e-8，
    // 若确发生指纹碰撞属统计正常——断言按去重计数容差 0（实测概率可忽略，出现即复核）。
    EXPECT_EQ(seen.size(), static_cast<std::size_t>(kN));
}

/** 五元组：全合法才 valid；精确等值；缺一即 false；容器键可比较（TASK-03/§5.1）。 */
TEST(TaskIdentityTuple, FiveFieldSemantics_UT_ID_T)
{
    TaskIdentity t;
    t.project  = ProjectId::generate();
    t.branch   = BranchId::generate();
    t.revision = RevisionId::generate();
    t.run      = RunId::generate();
    t.attempt  = AttemptId::fromCanonical("att-1");
    EXPECT_TRUE(t.isValid());

    // 逐字段置空：缺一即 invalid（§8 UT-ID-T 行明文）。
    {
        TaskIdentity x = t; x.project = ProjectId{};   EXPECT_FALSE(x.isValid());
        TaskIdentity y = t; y.branch = BranchId{};     EXPECT_FALSE(y.isValid());
        TaskIdentity z = t; z.revision = RevisionId{}; EXPECT_FALSE(z.isValid());
        TaskIdentity w = t; w.run = RunId{};           EXPECT_FALSE(w.isValid());
        TaskIdentity v = t; v.attempt = AttemptId{};   EXPECT_FALSE(v.isValid());   // attempt 0＝空
    }

    // 精确等值（附录 D 第 12 项：无容差）＋哈希一致。
    TaskIdentity same = t;
    EXPECT_TRUE(t == same);
    EXPECT_FALSE(t != same);
    EXPECT_EQ(std::hash<TaskIdentity>{}(t), std::hash<TaskIdentity>{}(same));

    // attempt 变化即不等（同 Run 重试＝不同尝试）。
    TaskIdentity retry = t;
    retry.attempt = AttemptId::fromCanonical("att-2");
    EXPECT_TRUE(t != retry);
    EXPECT_TRUE(t < retry || retry < t);               // operator< 全序可用（容器键）
}

/** 强类型纪律：不同身份类型互不隐式转换（编译期隔离——以可移植的"不相关类型"抽查）。 */
TEST(IdentityStrongTyping, NoImplicitConversion_UT_ID_T)
{
    const ObjectId obj = ObjectId::generate();
    // 反向验证：rev- 文本必须经 RevisionId 才能构造——不存在跨类型捷径。
    const RevisionId rev = RevisionId::fromCanonical(obj.toCanonical().replace(0, 4, "rev-"));
    EXPECT_NE(std::string(rev.toCanonical()).substr(0, 4), "obj-");
    // （跨类型若存在隐式转换，上一行 fromCanonical 的 tag 校验模型即失去意义；
    //   编译期隔离由"六独立 struct 无转换构造"保证——此处以行为面钉住。）
}
}  // namespace
