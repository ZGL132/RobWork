/**
 * @file   DescriptionTest.cpp
 * @brief  Description 值类型契约用例组（RT-T03）——§4.2 中性值类型的默认
 *         语义/值语义/四态承载/编码布局/reader 纯函数契约。
 *
 * 设计依据：
 *   - units/runtime.md §4.2（RobotDesignDescription 及全部子结构原文——
 *     字段默认态、SourcedValue 四态承载、IRobotDesignReader 纯函数）、
 *     §4.4（缺失≠非法——NFR-COR-03 类型层）、§5.2 S2（reader 失败→
 *     InputInvalid 归属）
 *   - 需求 NFR-COR-03（缺失不伪造——NotProvided 无值可取）；MDL-21
 *     （CouplingMatrix 行主序承载——与 §4.5 编码精神一致）
 *   - 任务契约 tasks/foundation/RT-T03.json（acceptance 2/3 的类型层前置；
 *     每用例中文标注验证点）
 *
 * 替身边界声明（RT-STUB-0 精神）：本文件的 ScriptedReader 是 §11 标准替身
 * 的契约级最小形态——仅验证 runtime 侧 reader 契约（纯函数/两态轨），
 * 不构成 modeling 真实 reader 解析行为的证明。
 */

#include <sdurws/ird/runtime/Description.hpp>
#include <sdurws/ird/runtime/Errors.hpp>
#include <sdurws/ird/runtime/Resource.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using namespace sdurws::ird::runtime;
namespace core = sdurws::ird::core;

// =====================================================================
// 夹具工具（Provided 值的来源记录——core SourcedValue 契约要求显式来源）。
// =====================================================================

/// 用户输入来源记录（core::ValueProvenance 工厂——P-1 校验通过的最简形态）。
core::ValueProvenance userProv()
{
    return core::ValueProvenance::make(core::ProvenanceKind::UserProvided);
}

/// 构造 Provided 态 SourcedValue（测试夹具的数值注入入口）。
core::SourcedValue<double> val(double v)
{
    return core::SourcedValue<double>::provided(v, userProv());
}

/// 单位旋转矩阵（逐元素构造——header-only 冒烟约束下不用外联 identity()）。
rw::math::Rotation3D<double> identityRotation()
{
    return rw::math::Rotation3D<double>(1, 0, 0, 0, 1, 0, 0, 0, 1);
}

// =====================================================================
// 类型默认语义（§4.2 聚合体默认构造＝"未显式配置"口径）。
// =====================================================================

/** 默认实例：向量空、耦合空、基座预设 Ground、SourcedValue 全 NotProvided、
 *  契约版本 0（非法值——须由 reader 显式置 ≥1，类型层不替调用方编造）。 */
TEST(DescriptionType, DefaultsMatchContract)
{
    const RobotDesignDescription d;
    EXPECT_EQ(d.descriptionContractVersion, 0u) << "契约版本默认 0＝未置值（非法域，校验器拒绝）";
    EXPECT_TRUE(d.robotLocalName.empty());
    EXPECT_TRUE(d.joints.empty());
    EXPECT_TRUE(d.links.empty());
    EXPECT_TRUE(d.tools.empty());
    EXPECT_TRUE(d.scene.empty());
    EXPECT_TRUE(d.friction.empty());
    EXPECT_TRUE(d.resourceRefs.empty());
    EXPECT_EQ(d.base.preset, InstallationPresetToken::Ground)
        << "未显式配置＝地面安装（V15-04——§4.3.2 默认口径）";
    EXPECT_EQ(d.base.customEaa.state(), core::FieldState::NotProvided);
    EXPECT_FALSE(d.drivetrain.coupling.has_value()) << "耦合矩阵可选（R2）——默认无";

    // 子结构默认态：限位/限速 NotProvided（缺失≠非法——NFR-COR-03 的
    // 类型层承载）；工作范围无；资源引用状态默认 Recorded。
    const JointDescription j;
    EXPECT_EQ(j.type, JointType::Revolute);
    EXPECT_EQ(j.lower.state(), core::FieldState::NotProvided);
    EXPECT_EQ(j.upper.state(), core::FieldState::NotProvided);
    EXPECT_EQ(j.maxVelocity.state(), core::FieldState::NotProvided);
    EXPECT_FALSE(j.workingRange.has_value());
    const ResourceRef r;
    EXPECT_EQ(r.state, ResourceState::Recorded);
    EXPECT_FALSE(r.sourcePathHint.has_value()) << "路径提示可空（不作身份——§8.6）";
}

/** 值语义：深拷贝（向量成员独立）＋移动保留内容——编译链内 Description
 *  作为 S2 临时值传递（§5.2），拷贝/移动语义必须可靠。 */
TEST(DescriptionType, ValueSemanticsDeepCopyAndMove)
{
    RobotDesignDescription d;
    d.descriptionContractVersion = 7;
    d.robotLocalName = "IRB_T";
    JointDescription j;
    j.localName = "joint_1";
    j.lower = val(-2.97);
    j.upper = val(2.97);
    d.joints.push_back(j);
    LinkDescription link;
    link.localName = "base_link";
    link.mass = val(5.0);
    d.links.push_back(link);

    // 拷贝独立：改副本不动原件（编译事务回滚安全——§5.3）。
    RobotDesignDescription copy = d;
    copy.joints[0].localName = "mutated";
    copy.links[0].mass = val(9.0);
    EXPECT_EQ(d.joints[0].localName, "joint_1");
    EXPECT_EQ(d.links[0].mass.tryValue(), 5.0);
    EXPECT_EQ(copy.descriptionContractVersion, d.descriptionContractVersion);

    // 移动保留：源可析、目标内容完好（S2→S3 传递形态）。
    RobotDesignDescription moved = std::move(copy);
    EXPECT_EQ(moved.joints[0].localName, "mutated");
    EXPECT_EQ(moved.links[0].mass.tryValue(), 9.0);
}

/** 四态承载（NFR-COR-03 类型层）：缺失无值可取（"缺失＝零"通道被切断）；
 *  Provided 值逐位保留（RT-CPX-3 的 −1 能被原样送抵校验器）。 */
TEST(DescriptionType, SourcedValueCarriesMissingNotZero)
{
    const core::SourcedValue<double> missing;
    EXPECT_FALSE(missing.tryValue().has_value())
        << "NotProvided 态无值——不得隐式得到 0（core UT-MISS 同口径）";

    const core::SourcedValue<double> negative = val(-1.0);
    ASSERT_TRUE(negative.tryValue().has_value());
    EXPECT_EQ(*negative.tryValue(), -1.0)
        << "Provided 负值逐位保留——RT-CPX-3（mass=−1）依赖此行为到达校验器";
}

/** 耦合矩阵行主序布局：元素 (i,j)＝c[i*cols+j]（§4.5 行主序编码精神——
 *  确定性与表示无关；校验器与 RT-T04 编码器共用该读法）。 */
TEST(DescriptionType, CouplingMatrixRowMajorAccess)
{
    CouplingMatrix cm;
    cm.rows = 2;
    cm.cols = 2;
    cm.c = {2.0, 1.0, 0.0, 3.0}; // [[2,1],[0,3]]——良态可逆（RT-CPL-1 第三分支）
    cm.jointRange.firstIndex = 1;
    cm.jointRange.count = 2;
    EXPECT_EQ(cm.c[0 * 2 + 1], 1.0) << "(0,1)＝c[1]";
    EXPECT_EQ(cm.c[1 * 2 + 0], 0.0) << "(1,0)＝c[2]";
    EXPECT_EQ(cm.c[1 * 2 + 1], 3.0) << "(1,1)＝c[3]";
}

/** reader 接口形态与纯函数契约：抽象（类型层强制 modeling 实现）；
 *  同字节→同 Description（确定性——§4.2 纯函数要求）；不识别的格式
 *  版本走 err 轨携 InputInvalid（S2 归属——§5.2，不静默猜测）。 */
TEST(DescriptionType, ReaderAbstractAndPureFunction)
{
    // 抽象性：类型层阻断"不带解析语义的实例化"（接口契约的编译期证据）。
    static_assert(std::is_abstract<IRobotDesignReader>::value,
                  "IRobotDesignReader 必须为抽象接口（§4.2——modeling 实现）");

    /// 脚本化 reader（§11 ScriptedReader 契约级最小形态）。
    class ScriptedReader : public IRobotDesignReader {
    public:
        Expected<RobotDesignDescription, RuntimeError>
            read(const std::vector<std::uint8_t>& objectBytes,
                 std::uint32_t objectTypeFormatVersion) const override
        {
            // 不识别的格式版本→err（InputInvalid 含定位——reader 侧拒绝口径）。
            if (objectTypeFormatVersion != 1u) {
                return Expected<RobotDesignDescription, RuntimeError>::err(
                    RuntimeError(RuntimeErrorCode::InputInvalid,
                                 "reader/format: 不识别的对象格式版本 "
                                     + std::to_string(objectTypeFormatVersion)));
            }
            if (objectBytes.empty()) {
                return Expected<RobotDesignDescription, RuntimeError>::err(
                    RuntimeError(RuntimeErrorCode::InputInvalid,
                                 "reader/bytes: 对象字节为空"));
            }
            // 固定输出（无环境/时钟依赖——纯函数）。
            RobotDesignDescription d;
            d.descriptionContractVersion = 1;
            d.robotLocalName = "IRB_Scripted";
            JointDescription j;
            j.localName = "joint_1";
            j.axis = rw::math::Vector3D<double>(0.0, 0.0, 1.0);
            d.joints.push_back(j);
            return Expected<RobotDesignDescription, RuntimeError>::ok(d);
        }
    };

    const ScriptedReader reader;
    const std::vector<std::uint8_t> bytes = {0x01, 0x02, 0x03};

    // 同字节两次读取→逐字段一致（纯函数确定性的行为面）。
    const auto first = reader.read(bytes, 1u);
    const auto second = reader.read(bytes, 1u);
    ASSERT_TRUE(first.ok());
    ASSERT_TRUE(second.ok());
    EXPECT_EQ(first.get().robotLocalName, second.get().robotLocalName);
    ASSERT_EQ(first.get().joints.size(), second.get().joints.size());
    EXPECT_EQ(first.get().joints[0].axis[2], second.get().joints[0].axis[2]);

    // 格式版本不识别→err 轨携稳定码（不抛、不返回默认 Description 静默吞错）。
    const auto bad = reader.read(bytes, 99u);
    EXPECT_FALSE(bad.ok());
    EXPECT_EQ(bad.error().code(), RuntimeErrorCode::InputInvalid);
}

/** 枚举面锚定（ErrorsTest 同款纪律）：数值进入 RT-Codec 编码面（§4.5），
 *  插入/重排＝身份破坏——用序号锚定防无意改动。 */
TEST(DescriptionType, EnumSurfaceAnchored)
{
    EXPECT_EQ(static_cast<int>(JointType::Revolute), 0);
    EXPECT_EQ(static_cast<int>(JointType::Continuous), 1);
    EXPECT_EQ(static_cast<int>(JointType::Prismatic), 2);
    EXPECT_EQ(static_cast<int>(JointType::Fixed), 3);
    EXPECT_EQ(static_cast<int>(InstallationPresetToken::Ground), 0);
    EXPECT_EQ(static_cast<int>(InstallationPresetToken::Inverted), 1);
    EXPECT_EQ(static_cast<int>(InstallationPresetToken::Wall), 2);
    EXPECT_EQ(static_cast<int>(InstallationPresetToken::Custom), 3);
    EXPECT_EQ(static_cast<int>(ResourceState::Recorded), 0);
    EXPECT_EQ(static_cast<int>(ResourceState::Solidified), 1);
}

}  // namespace
