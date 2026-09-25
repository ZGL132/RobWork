/**
 * @file   CompileBridgeContractTest.cpp
 * @brief  编译桥接契约测试（MdlCompileBridgeContract，集成模式 gating——
 *         TARGET sdurw_kinematics；消费 runtime 公共 CanonicalModelBuilder
 *         装配 S5）——契约 tasks/foundation/WP-13-T12.json acceptance 3
 *         的具名自证（与 runtime RT-T12 协作面）：
 *
 *   reader 产出 Description → runtime S5 真实构造面（CanonicalModelBuilder
 *   全部构造不变量：轴规格化/正交性/objectRefs 复核/身份唯一/内容身份）
 *   端到端消费成功，且 CanonicalModel 字段与 reader 输出逐项对齐
 *     （descriptionContractVersion/robotLocalName/关节链/资源清单/传动块）
 *   reader 失败面：缺陷闭包下 reader 恒经值面错误返回（无异常越界），
 *     S5 面不接收未校验输入（bridge 两道闸口——Codec 解码门＋映射断言面）
 *
 * ★ R-2 装配取舍（T09 DhConvertEquivalenceTest RuntimeCompileProbe 同款
 *   纪律，登记单元卡 §15）：runtime 产品编译器类（CanonicalModelCompiler）
 *   声明于 runtime/src/CompilerImpl.hpp——单元私有头，跨单元 include 被红
 *   线禁止；S1～S4 管道（修订锚定/解码/校验/资源——runtime 自身已验面）
 *   不在本测试复算，S5 段走真实公共 builder。生产装配（CompileRequest 的
 *   designReader 槽注入本 reader——§9.2 同源绑定）归 L5 应用壳。
 * ★ objectRefs 装配形态注记（O-36 裁决 2026-09-22）：复核范围＝真实存储
 *   对象；runtime S5 builder 的"模型内对象∈objectRefs"实现尚未按裁决收窄
 *   （runtime 卡增量义务——P-MDL-1 关闭面），本测试替身按收窄前兼容形态
 *   把关节/连杆子 ObjectId 一并登记入 objectRefs。该装配不裁决 O-36 口径
 *   （建模侧口径由 CanonicalBridgeTest.ClosureScopeRealStoredObjectsOnly
 *   钉住——builder 不要求子 id∈闭包）。
 *
 * 设计依据：units/modeling.md §9.1/§9.2、units/runtime.md §4.2/§4.3/§5.2。
 * 线程安全：全部用例单线程（builder 非线程安全——仅构造线程使用）。
 */

#include "../test/BridgeFixtures.hpp"

#include <sdurws/ird/runtime/CanonicalModel.hpp>  // CanonicalModelBuilder（S5 公共构造面）
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace modeling = ::sdurws::ird::modeling;
namespace core = ::sdurws::ird::core;
namespace runtime = ::sdurws::ird::runtime;
using namespace modeling;
using BridgeClosure = modeling::testbridge::BridgeClosure;
using modeling::testbridge::encode;
using modeling::testbridge::makeFullDesign;
using modeling::testbridge::makeOid;

namespace {

/// 测试定位三元组＋根对象身份（S5 header 装配值——project 替身面）。
struct IdentityValues {
    core::ProjectId project = core::ProjectId::generate();
    core::BranchId branch = core::BranchId::generate();
    core::RevisionId revision = core::RevisionId::generate();
    core::ObjectId robotOid = core::ObjectId::generate();
    core::ContentVersion cv{};

    /// 非零测试摘要（FNV-1a 128 双实例拼合——S5 只核非零与唯一性，真实性
    /// 归 project 的 SHA-256 计算面；算法族与 core §4.1 std::hash 同规）。
    static core::Digest256 digestOf(const std::string& tag)
    {
        std::uint64_t hi = 0xcbf29ce484222325ull;
        std::uint64_t lo = 0x84222325cbf29ce4ull;
        for (const char ch : tag) {
            hi = (hi ^ static_cast<unsigned char>(ch)) * 0x100000001b3ull;
            lo = (lo ^ static_cast<unsigned char>(ch)) * 0x100000001b3ull;
        }
        core::Digest256 d{};
        for (int i = 0; i < 8; ++i) {
            d[static_cast<std::size_t>(i)] =
                static_cast<std::uint8_t>((hi >> (8 * i)) & 0xFFu);
            d[static_cast<std::size_t>(i) + 8] =
                static_cast<std::uint8_t>((lo >> (8 * i)) & 0xFFu);
        }
        return d;
    }
};

/**
 * @brief reader 产出 Description → S5 装配（CanonicalModelBuilder 全不变量
 *        真实执行）。objectRefs 登记：根＋关节＋连杆＋工具＋场景（收窄前
 *        兼容形态——文件头注记）；资源清单随 Description 透传；默认 TCP
 *        下标 0（首项＝默认工具——reader 排序契约的 S5 侧核对条件）。
 */
runtime::CanonicalModel assembleS5(const IdentityValues& ids,
                                   const runtime::RobotDesignDescription& desc)
{
    // ---- 身份块（§4.3.1——descriptionContractVersion 同源自 reader 输出）。
    runtime::CanonicalModelHeader header;
    header.project = ids.project;
    header.branch = ids.branch;
    header.revision = ids.revision;
    header.revisionSeq = 1;
    header.descriptionContractVersion = desc.descriptionContractVersion;
    header.compilerContractVersion = 1;
    header.builtFrom = IdentityValues::digestOf("bridge-robot");
    const auto pushRef = [&header, &ids](const core::ObjectId& oid,
                                         const char* token) {
        runtime::ObjectRefEntry e;
        e.objectId = oid;
        e.contentVersion = ids.cv;
        e.objectTypeToken = token;
        e.digest = IdentityValues::digestOf(std::string(token));
        header.objectRefs.push_back(std::move(e));
    };
    pushRef(ids.robotOid, runtime::kRobotDesignObjectType);
    for (const runtime::JointDescription& j : desc.joints) { pushRef(j.objectId, "joint"); }
    for (const runtime::LinkDescription& l : desc.links) { pushRef(l.objectId, "link"); }
    for (const runtime::ToolDescription& t : desc.tools) { pushRef(t.objectId, "tool"); }
    for (const runtime::SceneObjectDescription& s : desc.scene) {
        pushRef(s.objectId, "scene-object");
    }

    // ---- 链块（Description→Canonical 同式拷贝——zeroOffset=0 显式表示
    // 已折叠：CompilerImpl S5 v0.12 登记的同一语义）。
    runtime::RobotChain chain;
    chain.robotObjectId = ids.robotOid;
    chain.robotLocalName = desc.robotLocalName;
    chain.deviceName = desc.robotLocalName;  // 消歧前事实名（runtime §15.4 v0.12）
    chain.joints.reserve(desc.joints.size());
    for (const runtime::JointDescription& j : desc.joints) {
        runtime::CanonicalJoint cj;
        cj.objectId = j.objectId;
        cj.localName = j.localName;
        cj.type = j.type;
        cj.axis = j.axis;      // 规格化在 builder（§4.3.3）
        cj.origin = j.origin;  // 已含零位折叠（Description 契约）
        cj.zeroOffset = 0.0;
        if (j.lower.tryValue().has_value() && j.upper.tryValue().has_value()) {
            runtime::JointBounds b;
            b.lower = j.lower.value();
            b.upper = j.upper.value();
            cj.bounds = b;
        }
        cj.workingRange = j.workingRange;
        cj.maxVelocity = j.maxVelocity;
        cj.maxAcceleration = j.maxAcceleration;
        chain.joints.push_back(std::move(cj));
    }
    chain.links.reserve(desc.links.size());
    for (const runtime::LinkDescription& l : desc.links) {
        runtime::CanonicalLink cl;
        cl.objectId = l.objectId;
        cl.localName = l.localName;
        cl.mass = l.mass;
        cl.centerOfMass = l.centerOfMass;
        cl.inertia = l.inertia;
        chain.links.push_back(std::move(cl));
    }

    // ---- 工具/场景块（首项默认 TCP——MDL-13/04；世界系固连位姿原样。
    // Canonical 侧几何引用粒度＝ResourceRef——Description GeometryRef 的
    // resource 半部对位拷贝）。
    std::vector<runtime::CanonicalTool> tools;
    tools.reserve(desc.tools.size());
    for (const runtime::ToolDescription& t : desc.tools) {
        runtime::CanonicalTool ct;
        ct.objectId = t.objectId;
        ct.localName = t.localName;
        ct.geometry = std::nullopt;  // 缺省无几何；有则取 resource 半部对位
        if (t.geometry.has_value()) { ct.geometry = t.geometry->resource; }
        ct.mass = t.mass;
        ct.centerOfMass = t.centerOfMass;
        ct.inertia = t.inertia;
        ct.tcpOffset = t.tcpOffset;
        tools.push_back(std::move(ct));
    }
    std::vector<runtime::CanonicalSceneObject> scene;
    scene.reserve(desc.scene.size());
    for (const runtime::SceneObjectDescription& s : desc.scene) {
        runtime::CanonicalSceneObject cs;
        cs.objectId = s.objectId;
        cs.localName = s.localName;
        cs.worldPose = s.worldPose;
        cs.geometry = s.geometry.resource;
        scene.push_back(std::move(cs));
    }

    // ---- S5 装配（公共 builder——全部构造不变量真实执行；失败以
    // RuntimeError 抛出，调用方按值面转译）。世界块取缺省＝地面安装恒等
    // （与 fixture 预设 Ground 一致——基座矩阵归 runtime 编译产物，M-11：
    // Description 只携带编辑表示，装配面按预设规则解析）。传动块按
    // Canonical 侧同形结构对位拷贝（ratioPerJoint/coupling——R1 无耦合）。
    runtime::CanonicalDrivetrain drivetrain;
    drivetrain.ratioPerJoint = desc.drivetrain.ratioPerJoint;
    drivetrain.coupling = desc.drivetrain.coupling;
    runtime::CanonicalModelBuilder builder;
    builder.setHeader(header);
    builder.setWorld(runtime::WorldPlacement{});
    builder.setChain(chain);
    builder.setTools(std::move(tools));
    builder.setDefaultTcpIndex(0);  // 首项＝默认工具（reader 排序契约）
    builder.setScene(std::move(scene));
    builder.setDrivetrain(drivetrain);
    builder.setResourceManifest(desc.resourceRefs);  // §4.3.5（按 resourceId 规范化）
    return builder.build();
}

}  // namespace

/**
 * @brief reader→S5 端到端：六轴全量模型经 reader 映射后，S5 真实构造面
 *        全不变量通过，CanonicalModel 字段与 reader 输出逐项对齐。
 */
TEST(MdlCompileBridgeContract, ReaderOutputFeedsCanonicalBuilder_WP13T12_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-14", "ARC-03"},
                  std::vector<std::string>{});

    BridgeClosure closure;
    RobotDesign design = makeFullDesign(closure);
    closure.put(std::string(kRobotDesignObjectType), encode(design));

    const RobotDesignReader reader(&closure);
    const auto parsed = reader.read(encode(design), 1u);
    ASSERT_TRUE(parsed.ok()) << parsed.error().what();

    const IdentityValues ids;
    const runtime::CanonicalModel model = assembleS5(ids, parsed.get());

    // ---- 身份/链字段对齐（reader 输出＝编译输入＝模型事实）。
    EXPECT_EQ(model.header().descriptionContractVersion,
              parsed.get().descriptionContractVersion);
    EXPECT_EQ(model.chain().robotLocalName, parsed.get().robotLocalName);
    ASSERT_EQ(model.chain().joints.size(), parsed.get().joints.size());
    EXPECT_EQ(model.chain().joints.front().localName,
              parsed.get().joints.front().localName);
    EXPECT_EQ(model.chain().links.size(), parsed.get().links.size());
    // 资源清单对齐（reader 声明面＝模型清单——规范化序由 builder 执行）。
    ASSERT_EQ(model.resourceManifest().size(), parsed.get().resourceRefs.size());
    EXPECT_EQ(model.resourceManifest().front().state, runtime::ResourceState::Recorded);
    // 传动块对齐（ratioPerJoint 透传——OPT StageB 连续变量消费面）。
    EXPECT_EQ(model.drivetrain().ratioPerJoint.size(),
              parsed.get().drivetrain.ratioPerJoint.size());
}

/**
 * @brief reader→S5 失败面：缺陷闭包（工具悬空）下 reader 值面拒绝——
 *        S5 面不接收未校验输入（bridge 两道闸口：Codec 解码门＋映射断言
 *        面；异常不越过单元边界）。
 */
TEST(MdlCompileBridgeContract, ReaderFailureNeverReachesS5_WP13T12_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-06"},
                  std::vector<std::string>{});

    BridgeClosure closure;
    RobotDesign design = makeFullDesign(closure);
    const core::ObjectId ghost = makeOid();
    design.toolRefs.push_back(ghost);  // 悬空工具引用（闭包无此对象）
    design.defaultTcp = modeling::TcpRef{ghost, "tip"};
    closure.put(std::string(kRobotDesignObjectType), encode(design));

    const RobotDesignReader reader(&closure);
    const auto parsed = reader.read(encode(design), 1u);
    ASSERT_FALSE(parsed.ok());
    EXPECT_EQ(parsed.error().code(), runtime::RuntimeErrorCode::InputInvalid);
    EXPECT_NE(std::string(parsed.error().what()).find(ghost.toCanonical()),
              std::string::npos)
        << "失败定位随 detail 传导（悬空对象 ObjectId——S2 归属表）";
}
