/**
 * @file   DescriptionBridgeContractTest.cpp
 * @brief  Description 桥接契约测试（MdlDescriptionBridgeContract）——契约
 *         tasks/foundation/WP-13-T12.json acceptance 3 的具名自证（与
 *         runtime RT-T12 协作的契约面；两模式均编译——被测面零框架符号）：
 *
 *   Description 契约面字段集与 runtime.md §4.2 RobotDesignDescription 对齐：
 *     十类字段（descriptionContractVersion/robotLocalName/joints/links/
 *     tools/scene/base/drivetrain/friction/resourceRefs）全部有承载面且随
 *     映射赋值；RT-T11 增量 objectId 字段（JointDescription/LinkDescription/
 *     ToolDescription/SceneObjectDescription）随映射透传（S5/builder 复核
 *     CM-0 的输入面）；预设词表类型级钉住（InstallationPresetToken 为
 *     runtime 单一权威枚举实体——modeling 值模型复用不另设，P-RT-4）
 *   schemaVersion↔descriptionContractVersion 同源（初值 1）：reader 输出
 *     契约版本＝建模 schemaVersion＝kRobotDesignSchemaVersion（R-MDL-3
 *     单点演进——直解方案否决的隔离层语义，§9.2）
 *   reader 失败轨契约：失败恒 Expected err（RuntimeError InputInvalid），
 *     异常不越过单元边界（§9.4 公共契约前提——值面两态轨）
 *
 * 设计依据：units/modeling.md §9.2/§9.4.5、units/runtime.md §4.2/§5.2 S2；
 * 共享夹具＝../test/BridgeFixtures.hpp（与命令管线契约测试同款跨目录
 * include 形态——测试域替身不入公共面）。
 *
 * 线程安全：全部用例单线程（纯函数服务语义）。
 */

#include "../test/BridgeFixtures.hpp"

#include <sdurws/ird/io/IoFwd.hpp>                  // kAccessVersion（io 单点对照）
#include <sdurws/ird/modeling/CanonicalBridge.hpp>  // 被测契约面：builder＋reader
#include <sdurws/ird/modeling/ObjectTypes.hpp>
#include <sdurws/ird/runtime/Description.hpp>       // 接收侧契约（§4.2 字段集）
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

namespace modeling = ::sdurws::ird::modeling;
namespace core = ::sdurws::ird::core;
namespace runtime = ::sdurws::ird::runtime;
namespace io = ::sdurws::ird::io;
using namespace modeling;
using BridgeClosure = modeling::testbridge::BridgeClosure;
using modeling::testbridge::encode;
using modeling::testbridge::makeFullDesign;

// =====================================================================
// 契约面：字段集对齐＋同源版本（acceptance 3）
// =====================================================================

/**
 * @brief Description 契约面字段集对齐（runtime.md §4.2）：十类字段全部有
 *        承载面且随映射赋值；RT-T11 增量 objectId 随四个子结构透传；预设
 *        词表类型级钉住（runtime 枚举实体复用）；资源读取契约版本＝io 单点。
 */
TEST(MdlDescriptionBridgeContract, DescriptionFieldSetAlignment_WP13T12_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-14", "ARC-03"},
                  std::vector<std::string>{});

    BridgeClosure closure;
    RobotDesign design = makeFullDesign(closure);
    closure.put(std::string(kRobotDesignObjectType), encode(design));

    const RobotDesignReader reader(&closure);
    const auto parsed = reader.read(encode(design), 1u);
    ASSERT_TRUE(parsed.ok()) << parsed.error().what();
    const runtime::RobotDesignDescription& desc = parsed.get();

    // ---- 十类字段全量承载（§4.2 字段集——缺类即对齐失配）。
    EXPECT_EQ(desc.descriptionContractVersion, 1u);
    EXPECT_FALSE(desc.robotLocalName.empty());
    ASSERT_EQ(desc.joints.size(), 6u);           // 关节链（串联序基座→法兰）
    ASSERT_EQ(desc.links.size(), 7u);            // joints+1（含基座连杆）
    ASSERT_EQ(desc.tools.size(), 1u);            // 首项默认 TCP（MDL-13/04）
    ASSERT_EQ(desc.scene.size(), 1u);            // 环境对象（世界系固连）
    EXPECT_EQ(desc.base.preset, runtime::InstallationPresetToken::Ground);
    ASSERT_EQ(desc.drivetrain.ratioPerJoint.size(), 6u);
    ASSERT_EQ(desc.friction.size(), 6u);         // 逐关节口径
    ASSERT_EQ(desc.resourceRefs.size(), 1u);     // 清单全量
    // RT-T11 增量 objectId（S5/builder 复核 CM-0 的输入面）——四个子结构
    // 逐一非空随映射透传。
    for (const runtime::JointDescription& j : desc.joints) {
        EXPECT_TRUE(j.objectId.isValid());
    }
    for (const runtime::LinkDescription& l : desc.links) {
        EXPECT_TRUE(l.objectId.isValid());
    }
    EXPECT_TRUE(desc.tools.front().objectId.isValid());
    EXPECT_TRUE(desc.scene.front().objectId.isValid());

    // ---- 词表类型级钉住：安装预设词表为 runtime 单一权威枚举实体
    // （modeling 值模型直接复用该类型——P-RT-4 单一权威，不另设第二词表；
    // 关节类型两枚举独立定义、值序一致，映射 switch 全枚举无 default）。
    static_assert(std::is_same<decltype(design.basePlacement.preset),
                               decltype(desc.base.preset)>::value,
                  "预设词表＝runtime 枚举实体（P-RT-4 单一权威）");
    // 资源引用读取契约版本＝io 单点（P-RT-6 裁决值——modeling 不私写）。
    EXPECT_EQ(desc.resourceRefs.front().accessVersion, io::kAccessVersion);
}

/**
 * @brief schemaVersion↔descriptionContractVersion 同源（初值 1——§9.1 行 1；
 *        R-MDL-3 隔离层：直解方案否决，契约版本以建模 schema 单点演进）。
 */
TEST(MdlDescriptionBridgeContract, SchemaVersionSameSourceAsContractVersion_WP13T12_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"ARC-03"},
                  std::vector<std::string>{});

    // 初值 1：建模 schema 单一权威常量（ObjectTypes.hpp 登记簿——禁写字面量）。
    EXPECT_EQ(kRobotDesignSchemaVersion, 1u);

    BridgeClosure closure;
    RobotDesign design = makeFullDesign(closure);
    ASSERT_EQ(design.schemaVersion, kRobotDesignSchemaVersion);
    closure.put(std::string(kRobotDesignObjectType), encode(design));

    const RobotDesignReader reader(&closure);
    const auto parsed = reader.read(encode(design), 1u);
    ASSERT_TRUE(parsed.ok());
    // 同源：输出契约版本＝根对象 schemaVersion（透传——非第二常量）。
    EXPECT_EQ(parsed.get().descriptionContractVersion, design.schemaVersion);
    EXPECT_EQ(parsed.get().descriptionContractVersion, kRobotDesignSchemaVersion);
}

/**
 * @brief reader 失败轨契约：失败恒 Expected err（RuntimeError InputInvalid
 *        ——§5.2 S2 归属），异常不越过单元边界（§9.4 公共契约前提——值面
 *        两态轨）。触发面＝部件悬空引用（闭包缺该工具对象——RefMissing 的
 *        reader 侧转译；read() 的根来自入参字节，不经闭包 token 位——单根
 *        字节输入契约，§9.2）。
 */
TEST(MdlDescriptionBridgeContract, ReaderFailureTrackIsValueFace_WP13T12_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-06"},
                  std::vector<std::string>{});

    BridgeClosure closure;
    RobotDesign design = makeFullDesign(closure);
    // 悬空工具引用（闭包无该对象——映射期 RefMissing 触发面）。
    const core::ObjectId ghost = core::ObjectId::generate();
    design.toolRefs.push_back(ghost);
    design.defaultTcp = TcpRef{ghost, "tip"};

    const RobotDesignReader reader(&closure);
    const auto missing = reader.read(encode(design), 1u);
    ASSERT_FALSE(missing.ok());
    EXPECT_EQ(missing.error().code(), runtime::RuntimeErrorCode::InputInvalid);
    // 定位随 detail 传导（"reader 失败→InputInvalid 含定位"——§5.2 S2）。
    EXPECT_NE(std::string(missing.error().what()).find("RefMissing"),
              std::string::npos);
    EXPECT_NE(std::string(missing.error().what()).find(ghost.toCanonical()),
              std::string::npos);
}
