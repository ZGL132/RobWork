/**
 * @file   ConventionValueSemanticsTest.cpp
 * @brief  约定锁定与值语义用例组——UT-CONV/UT-VAL（units/core.md §8，CORE-T09）。
 *
 * 设计依据：
 *   - units/core.md §4.6（位姿与坐标系基础语义——无新类型，约定＋测试锁定）、
 *     §8 UT-CONV/UT-VAL 行、§9 CORE-T09 行（产物＝core/test/*，验证＝直接运行）
 *   - 需求 NFR-COR-02（确定性）；依据 ARCH §7.3、MDL-22 notation（UT-CONV 追溯）
 *   - 任务契约 tasks/foundation/CORE-T09.json（acceptance：已知算例与值语义断言通过）
 *
 * 锁定的三件事（§4.6 原文——框架约定误读会传染全产品，故以测试钉死）：
 *   ①位姿方向读法：T_ab＝"b 相对 a"，p_a = T_ab * p_b（UT-CONV-1 绕 Z 90° 已知算例）；
 *   ②组合顺序：T_ac = T_ab * T_bc 左乘链式＋inverse 回零（UT-CONV-2）；
 *   ③值语义：core 公共数据类型拷贝/移动/相等/哈希一致性＋容器可用性（UT-VAL）。
 *
 * 条件编译说明：UT-CONV 消费 rw::math（框架 L1 数学库）——集成模式经
 * sdurws_ird_core 的 PUBLIC sdurw_math 边取得头与库；独立冒烟模式无框架目标
 * （CORE-T01 先例），故以 __has_include 门控：冒烟模式只编译 UT-VAL（core 自有
 * 类型），UT-CONV 用例在集成模式执行（ird-test-report.json 的 modes 字段如实
 * 分记，不冒称两模式全绿）。
 *
 * 容差口径：纯约定数值断言用 1e-9（rad/m 量纲下双精度的保守下限；core.md
 * 附录 D C7 的运行容差语义属于产品运行校验，与锁定测试的数值比较容差不同源，
 * 此处 1e-9 仅服务"绕 Z 90°/正交回零"的浮点装配误差）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/core/Identity.hpp>

#include <map>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#if __has_include(<rw/math/Transform3D.hpp>)
#define IRD_HAS_RW_MATH 1
#include <rw/math/Transform3D.hpp>
#include <rw/math/Vector3D.hpp>
#include <rw/math/Rotation3D.hpp>
#include <rw/math/RPY.hpp>
#endif

namespace {
using namespace sdurws::ird::core;

// =====================================================================
// UT-VAL 值语义（core 自有公共类型——两模式均执行）
// =====================================================================

/** 拷贝语义：拷贝实例与原件相等且哈希一致；改拷贝不影响原件（值类型纪律）。 */
TEST(ValueSemantics, CopyIndependenceAndHashConsistency_UT_VAL)
{
    const ObjectId orig = ObjectId::generate();
    const ObjectId copy = orig;                     // 拷贝构造
    EXPECT_TRUE(orig == copy);
    EXPECT_EQ(std::hash<ObjectId>{}(orig), std::hash<ObjectId>{}(copy));

    // 深独立性：core 身份是纯值（16 字节数组），无共享状态可泄漏。
    ObjectId mutated = orig;
    mutated.bytes[0] = static_cast<std::uint8_t>(orig.bytes[0] ^ 0xFFu);
    EXPECT_FALSE(mutated == orig);

    // 摘要类型同纪律（§4.2 值语义）。
    ContentDigester d;
    const std::string s = "value-semantics";
    d.update(s.data(), s.size());
    const ContentIdentity cid;
    const Digest256 digest = d.finalize();
    ContentVersion cv; cv.bytes = digest;
    const ContentVersion cvCopy = cv;
    EXPECT_TRUE(cv == cvCopy);
    EXPECT_EQ(std::hash<ContentVersion>{}(cv), std::hash<ContentVersion>{}(cvCopy));
}

/** 移动语义：移动后目标值等于原值（core 类型均为平凡可拷贝——移动即拷贝，不残留句柄）。 */
TEST(ValueSemantics, MovePreservesValue_UT_VAL)
{
    const RevisionId a = RevisionId::generate();
    const RevisionId b = std::move(a);              // 移动构造（平凡类型＝按位拷贝）
    EXPECT_TRUE(b.isValid());
    EXPECT_EQ(b.bytes, a.bytes);                    // a 仍可读（平凡拷贝语义的必然结果）
    EXPECT_EQ(std::hash<RevisionId>{}(a), std::hash<RevisionId>{}(b));

    // 移动赋值同路径。
    AttemptId m1 = AttemptId::fromCanonical("att-7");
    AttemptId m2 = AttemptId::fromCanonical("att-1");
    m2 = std::move(m1);
    EXPECT_EQ(m2.value, 7u);
}

/** 相等↔哈希一致性：相等实例哈希必相等；不等实例哈希以极大概率不等（抽查）。 */
TEST(ValueSemantics, EqualityHashConsistency_UT_VAL)
{
    const auto p1 = ProjectId::generate();
    const auto p2 = ProjectId::generate();
    ASSERT_TRUE(p1 != p2);
    EXPECT_EQ(std::hash<ProjectId>{}(p1), std::hash<ProjectId>{}(p1));
    EXPECT_NE(std::hash<ProjectId>{}(p1), std::hash<ProjectId>{}(p2));

    // 五元组：等值⇒等哈希；attempt 差异⇒不等。
    TaskIdentity t1; t1.project = p1; t1.branch = BranchId::generate();
    t1.revision = RevisionId::generate(); t1.run = RunId::generate();
    t1.attempt = AttemptId::fromCanonical("att-1");
    TaskIdentity t2 = t1;
    EXPECT_EQ(std::hash<TaskIdentity>{}(t1), std::hash<TaskIdentity>{}(t2));
    TaskIdentity t3 = t1; t3.attempt = AttemptId::fromCanonical("att-2");
    EXPECT_NE(std::hash<TaskIdentity>{}(t1), std::hash<TaskIdentity>{}(t3));
}

/** 容器可用性：无序集/有序映射/无序映射对公共身份类型开箱可用（§8 编译断言＋运行抽查）。 */
TEST(ValueSemantics, ContainerUsability_UT_VAL)
{
    std::unordered_set<ObjectId> objSet;
    objSet.insert(ObjectId::generate());
    const ObjectId probe = ObjectId::generate();
    objSet.insert(probe);
    EXPECT_EQ(objSet.count(probe), 1u);             // hash+== 协作正确

    std::map<RevisionId, std::string> revMap;       // operator< 全序可用
    const RevisionId r1 = RevisionId::generate();
    revMap[r1] = "head";
    EXPECT_EQ(revMap.at(r1), "head");

    std::unordered_map<TaskIdentity, int> taskMap;  // 五元组作键（execution 登记表的容器形态）
    TaskIdentity t; t.project = ProjectId::generate(); t.branch = BranchId::generate();
    t.revision = RevisionId::generate(); t.run = RunId::generate();
    t.attempt = AttemptId::fromCanonical("att-1");
    taskMap[t] = 42;
    EXPECT_EQ(taskMap.at(t), 42);
}
}  // namespace（UT-VAL 段闭合；UT-CONV 段在下方条件块内自开命名空间）

// =====================================================================
// UT-CONV 位姿约定锁定（消费 rw::math——仅集成模式编译执行，见文件头说明）
// =====================================================================
#ifdef IRD_HAS_RW_MATH

using rw::math::Transform3D;
using rw::math::Vector3D;
using rw::math::Rotation3D;
using rw::math::RPY;

namespace {

constexpr double kTol = 1e-9;   ///< 约定锁定断言容差（rad/m 装配误差下限，见文件头）
constexpr double kDeg2Rad = 3.14159265358979323846 / 180.0;  ///< 度转弧度（自持常量——不依赖框架版本的字段名差异）

/// Rz(90°)：显式 9 元素矩阵（行主序）＝[[0,-1,0],[1,0,0],[0,0,1]]——零参数序歧义
/// （不依赖 RPY/EAA 的转换语义，§4.6 锁定对象是 Transform3D 读法而非旋转表示）。
const Rotation3D<double> rz90(0.0, -1.0, 0.0,  1.0, 0.0, 0.0,  0.0, 0.0, 1.0);

/// 期望向量近似相等（逐分量 kTol）。
void expectVecNear(const Vector3D<double>& got, double x, double y, double z)
{
    EXPECT_NEAR(got[0], x, kTol);
    EXPECT_NEAR(got[1], y, kTol);
    EXPECT_NEAR(got[2], z, kTol);
}

/** UT-CONV-1：位姿方向读法——绕 Z 转 90° 后点 (1,0,0)→(0,1,0)（§4.6/§8 已知算例）。 */
TEST(ConventionPose, RotateZ90MapsXToY_UT_CONV)
{
    // Rz(90°) 矩阵自证（构造见上方共享常量）。
    EXPECT_NEAR(rz90(0, 0), 0.0, kTol);
    EXPECT_NEAR(rz90(0, 1), -1.0, kTol);
    EXPECT_NEAR(rz90(1, 0), 1.0, kTol);
    EXPECT_NEAR(rz90(1, 1), 0.0, kTol);

    const Transform3D<double> tWa(Vector3D<double>::zero(), rz90);
    const Vector3D<double> pB(1.0, 0.0, 0.0);       // b 系中的点
    const Vector3D<double> pA = tWa * pB;           // §4.6：p_a = T_ab * p_b
    expectVecNear(pA, 0.0, 1.0, 0.0);               // →(0,1,0)（若框架读法与此相悖，本用例即红）

    // 方向向量不随平移移动（§4.6"点 vs 方向"约定的变换侧：R*dir）。
    const Transform3D<double> tWbT(Vector3D<double>(5.0, -3.0, 2.0), rz90);
    const Vector3D<double> dirMoved = tWbT.R() * pB;
    expectVecNear(dirMoved, 0.0, 1.0, 0.0);
}

/** UT-CONV-2：组合顺序 T_ac = T_ab * T_bc 左乘链式＋inverse 回零（§4.6）。 */
TEST(ConventionPose, CompositionOrderAndInverse_UT_CONV)
{
    // 已知算例：T_ab＝(Rz90°, d=(1,0,0))；T_bc＝(Rz90°, d=(0,1,0))——rz90 见共享常量。
    const Transform3D<double> tAb(Vector3D<double>(1.0, 0.0, 0.0), rz90);
    const Transform3D<double> tBc(Vector3D<double>(0.0, 1.0, 0.0), rz90);

    // 逐点等价验证（组合的语义面）：p_a = T_ab*(T_bc*p) == (T_ab*T_bc)*p。
    const Vector3D<double> p(2.0, -1.0, 0.5);
    const Vector3D<double> viaChain = tAb * (tBc * p);
    const Transform3D<double> tAc = tAb * tBc;      // 左乘链式（§4.6 冻结读法）
    const Vector3D<double> direct = tAc * p;
    expectVecNear(viaChain, direct[0], direct[1], direct[2]);

    // 数值面：复合平移＝R_ab*d_bc + d_ab；复合旋转＝R_ab*R_bc。
    expectVecNear(tAc.P(), tAb.P()[0] + (tAb.R() * tBc.P())[0],
                  tAb.P()[1] + (tAb.R() * tBc.P())[1],
                  tAb.P()[2] + (tAb.R() * tBc.P())[2]);

    // 逆回零：inverse(T_ab)*T_ab == Identity（平移与旋转双面）。
    const Transform3D<double> back = inverse(tAb) * tAb;
    expectVecNear(back.P(), 0.0, 0.0, 0.0);
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            EXPECT_NEAR(back.R()(i, j), (i == j ? 1.0 : 0.0), kTol);
        }
    }
}
}  // namespace
#else
namespace {
/// 冒烟模式占位：UT-CONV 需 rw::math（仅集成模式），保证冒烟构建零错误且不为空转。
TEST(ConventionSmokePlaceholder, UTSmokeCompilesCoreOwnTypes_UT_VAL)
{
    const ObjectId g = ObjectId::generate();
    EXPECT_TRUE(g.isValid());                       // core 自有类型在无框架环境可独立工作
}
}  // namespace
#endif  // __has_include(<rw/math/Transform3D.hpp>)
