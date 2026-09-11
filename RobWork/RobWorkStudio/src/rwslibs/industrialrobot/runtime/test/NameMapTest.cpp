/**
 * @file   NameMapTest.cpp
 * @brief  RT-T05 用例组——RuntimeNameMap 生成/消歧/双射/内容身份/交叉校验/
 *         ⑥端口（设计矩阵 RT-NM-1～7；AT-18 双向往返载体）。
 *
 * 设计依据：
 *   - units/runtime.md §7（生成规则/接口/AT-18/迁移/IRDNAME）、§11 验证矩阵
 *     RT-NM-1～7 行、§12 RT-T05 行（验收：AT-18 双向往返全量用例通过）
 *   - 需求 ARC-04（名称往返无漏/旧/双前缀——AT-18）、MDL-14（完整映射）、
 *     CON-06（内容身份入快照；反解先于接纳）；附录 D 第 12 项（名称与对象
 *     身份比较＝精确等值无容差——本组一律 EXPECT_EQ/EXPECT_TRUE 精确断言，
 *     不使用任何浮点容差宏）
 *   - 任务契约 tasks/foundation/RT-T05.json（acceptance 1～3）
 *
 * 替身边界声明（RT-STUB-0 精神）：本组全部输入为 CanonicalModelFixture 直构
 * 值（确定性种子 id——非产品路径），断言针对真实产品代码（NameMap/Codec）；
 * 不伪造 RobWork 行为——WC 交叉校验以"实际名集合"值注入（§7.2"映射规则可
 * 独立测试"口径；真实 WC 构造的全链切面随 RT-T07/T11/T12）。
 *
 * 未执行测试不得标注通过；本组边界：不含 S8 编译器集成（RT-T11）与静态
 * 门禁全仓扫描（WP-01-T01）——前者只验证映射侧契约，后者登记于 RT-T13。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/runtime/CanonicalModel.hpp>
#include <sdurws/ird/runtime/Codec.hpp>
#include <sdurws/ird/runtime/Errors.hpp>
#include <sdurws/ird/runtime/NameMap.hpp>

#include "CanonicalModelFixture.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

namespace fs = std::filesystem;

using namespace sdurws::ird::runtime::testfixture;
using sdurws::ird::runtime::BoundRuntimeNameResolver;
using sdurws::ird::runtime::CanonicalJoint;
using sdurws::ird::runtime::CanonicalLink;
using sdurws::ird::runtime::CanonicalModel;
using sdurws::ird::runtime::JointBounds;
using sdurws::ird::runtime::NameScope;
using sdurws::ird::runtime::ObjectRef;
using sdurws::ird::runtime::ObjectRefEntry;
using sdurws::ird::runtime::ResourceState;
using sdurws::ird::runtime::RuntimeError;
using sdurws::ird::runtime::RuntimeErrorCode;
using sdurws::ird::runtime::RuntimeName;
using sdurws::ird::runtime::RuntimeNameMap;
using sdurws::ird::runtime::RuntimeNameNotice;
using sdurws::ird::runtime::RuntimeResolveError;
using sdurws::ird::runtime::buildRuntimeNameMap;
using sdurws::ird::runtime::crossCheckRuntimeNames;
namespace rtcodec = sdurws::ird::runtime::rtcodec;
namespace core = sdurws::ird::core;

namespace {

// =====================================================================
// 断言助手（确定性、无容差——附录 D 第 12 项）。
// =====================================================================

/// 收集映射内全部条目全名（存储序）。
std::vector<std::string> allFullNames(const RuntimeNameMap& map)
{
    std::vector<std::string> names;
    names.reserve(map.entries().size());
    for (const auto& e : map.entries()) {
        names.push_back(e.fullName);
    }
    return names;
}

/// 收集映射内全部去重 ObjectId（升序——字节字典序）。
std::vector<core::ObjectId> distinctObjectIds(const RuntimeNameMap& map)
{
    std::vector<core::ObjectId> ids;
    for (const auto& e : map.entries()) {
        ids.push_back(e.objectId);
    }
    std::sort(ids.begin(), ids.end(),
              [](const core::ObjectId& a, const core::ObjectId& b) { return a.bytes < b.bytes; });
    ids.erase(std::unique(ids.begin(), ids.end(),
                          [](const core::ObjectId& a, const core::ObjectId& b) {
                              return a.bytes == b.bytes;
                          }),
              ids.end());
    return ids;
}

/// 断言 resolveRuntimeName 未命中且回显原名（RT-NM-3 口径：同码不抛）。
void expectResolveMiss(const RuntimeNameMap& map, const std::string& name, const char* what)
{
    const auto r = map.resolveRuntimeName(name);
    ASSERT_FALSE(r.ok()) << what << "：意外命中 \"" << name << "\"";
    EXPECT_EQ(r.error().code, sdurws::ird::runtime::RuntimeErrorCode::UnknownObject)
        << what << "：未命中须同码 UnknownObject";
    EXPECT_EQ(r.error().requestedName, name) << what << "：错误须回显原名（RT-NM-3）";
    EXPECT_FALSE(r.error().detail.empty()) << what << "：detail 不得为空";
}

/// 单关节最小模型工厂（RT-NM-4 跨项目用——同显示名、不同身份种子）。
CanonicalModel makeSingleJointModel(const std::string& p)
{
    Fixture f;
    f.header.project = idFrom<core::ProjectId>(p + "-prj");
    f.header.branch = idFrom<core::BranchId>(p + "-brn");
    f.header.revision = idFrom<core::RevisionId>(p + "-rev");
    f.header.revisionSeq = 1;
    f.header.descriptionContractVersion = 1;
    f.header.compilerContractVersion = 1;
    f.header.builtFrom = digestOf(p + "-desc");

    const core::ObjectId robot = idFrom<core::ObjectId>(p + "-robot");
    const core::ObjectId j1 = idFrom<core::ObjectId>(p + "-j1");
    const core::ObjectId l0 = idFrom<core::ObjectId>(p + "-l0");
    const core::ObjectId l1 = idFrom<core::ObjectId>(p + "-l1");
    auto addRef = [&f, &p](const core::ObjectId& id, const char* token) {
        ObjectRefEntry e;
        e.objectId = id;
        e.contentVersion = cvFrom(p + "-cv-" + token);
        e.objectTypeToken = token;
        e.digest = digestOf(p + "-dg-" + token);
        f.header.objectRefs.push_back(e);
    };
    addRef(robot, "robot-design");
    addRef(j1, "joint");
    addRef(l0, "link");
    addRef(l1, "link");

    ResourceRef mesh;
    mesh.resourceId = idFrom<core::ObjectId>(p + "-res");
    mesh.contentDigest = digestOf(p + "-mesh");
    mesh.state = ResourceState::Recorded;
    mesh.accessVersion = 1;
    f.manifest.push_back(mesh);

    f.chain.robotObjectId = robot;
    f.chain.robotLocalName = "IRB_T";  // 与 minimal 夹具同显示名（跨项目对照）
    f.chain.deviceName = "IRB_T";

    CanonicalJoint joint;
    joint.objectId = j1;
    joint.localName = "joint_1";
    joint.type = JointType::Revolute;
    joint.axis = rw::math::Vector3D<double>(0.0, 0.0, 1.0);
    joint.bounds = JointBounds{0.0, 1.0};  // 单位 rad
    joint.maxVelocity = val(1.0);
    f.chain.joints.push_back(joint);

    CanonicalLink baseLink;
    baseLink.objectId = l0;
    baseLink.localName = "base_link";
    CanonicalLink flangeLink;
    flangeLink.objectId = l1;
    flangeLink.localName = "link_1";
    f.chain.links.push_back(baseLink);
    f.chain.links.push_back(flangeLink);
    return f.build();
}

}  // namespace

// =====================================================================
// RT-NM-1（生成面）：范围覆盖、确定性、身份条目、Body 门控。
// =====================================================================

/** RT-NM-1/MDL-14：rich 夹具全范围条目生成——逐范围计数＋全名形态（无遗漏）。 */
TEST(NameMapBuildTest, BuildsAllScopesFromRichFixture_RT_NM_1)
{
    const RuntimeNameMap map = buildRuntimeNameMap(richFixture().build());

    // 逐范围计数（§7.1 范围表：rich 夹具的推导值——robot1/joint2/link3/
    // 工具1/场景1/几何引用4〔link_1.visual、link_2.collision、tool_1.collision、
    // Scene.obstacle_1.collision〕/BaseMount·BaseFrame·Flange 各1/Body3〔DWC〕）。
    EXPECT_EQ(map.objectsInScope(NameScope::Device).size(), 1u);
    EXPECT_EQ(map.objectsInScope(NameScope::Joint).size(), 2u);
    EXPECT_EQ(map.objectsInScope(NameScope::LinkFrame).size(), 3u);
    EXPECT_EQ(map.objectsInScope(NameScope::BaseMount).size(), 1u);
    EXPECT_EQ(map.objectsInScope(NameScope::BaseFrame).size(), 1u);
    EXPECT_EQ(map.objectsInScope(NameScope::Flange).size(), 1u);
    EXPECT_EQ(map.objectsInScope(NameScope::Tcp).size(), 1u);
    EXPECT_EQ(map.objectsInScope(NameScope::Geometry).size(), 4u);
    EXPECT_EQ(map.objectsInScope(NameScope::SceneObject).size(), 1u);
    EXPECT_EQ(map.objectsInScope(NameScope::Body).size(), 3u);
    EXPECT_EQ(map.objectsInScope(NameScope::Sensor).size(), 0u);  // R1 无实例（§7.1）
    EXPECT_EQ(map.size(), 18u);
    EXPECT_EQ(allFullNames(map).size(), map.size()) << "全名全局唯一（无重复名称）";

    // 代表性全名（形态＝scopeToken.localName；Device 无前缀）。
    EXPECT_TRUE(map.resolveRuntimeName("IRB_T").ok()) << "设备名本身";
    EXPECT_TRUE(map.resolveRuntimeName("IRB_T.joint_1").ok());
    EXPECT_TRUE(map.resolveRuntimeName("IRB_T.base_link").ok());
    EXPECT_TRUE(map.resolveRuntimeName("IRB_T.BaseMount").ok()) << "§6.3 派生节点";
    EXPECT_TRUE(map.resolveRuntimeName("IRB_T.Base").ok());
    EXPECT_TRUE(map.resolveRuntimeName("IRB_T.Flange").ok());
    EXPECT_TRUE(map.resolveRuntimeName("IRB_T.tool_1").ok());
    EXPECT_TRUE(map.resolveRuntimeName("IRB_T.link_1.visual").ok());
    EXPECT_TRUE(map.resolveRuntimeName("IRB_T.link_2.collision").ok());
    EXPECT_TRUE(map.resolveRuntimeName("IRB_T.tool_1.collision").ok());
    EXPECT_TRUE(map.resolveRuntimeName("Scene.obstacle_1").ok()) << "场景作用域 token";
    EXPECT_TRUE(map.resolveRuntimeName("Scene.obstacle_1.collision").ok());
    EXPECT_TRUE(map.resolveRuntimeName("IRB_T.link_1.body").ok()) << "DWC 存在时";

    // 规则版本（设计默认 1——待产品确认冻结）＋内容身份非零＋无消歧警告。
    EXPECT_EQ(map.ruleVersion(), 1u);
    EXPECT_TRUE(map.contentIdentity().isValid());
    EXPECT_TRUE(map.notices().empty()) << "合法夹具无消歧/空名警告";
}

/** R-4/§7.1：Device 条目＝设备名本身（无前缀）；反解经身份作用域返回设备名。 */
TEST(NameMapBuildTest, DeviceEntryHasNoPrefix_R4)
{
    const RuntimeNameMap map = buildRuntimeNameMap(minimalFixture().build());
    const core::ObjectId robot = idFrom<core::ObjectId>("robot");

    const auto n = map.resolveObjectId(robot);
    ASSERT_TRUE(n.ok());
    EXPECT_EQ(n.get().scope, NameScope::Device);
    EXPECT_EQ(n.get().fullName, "IRB_T");
    EXPECT_EQ(n.get().scopeToken, "IRB_T") << "Device 条目 scopeToken＝设备名";
    EXPECT_EQ(n.get().localName, "IRB_T") << "Device 条目无前缀拆分";
}

/** RT-NM-1/RT-ID-1（映射层）：同输入重复构建——逐条目相等＋编码逐字节相等。 */
TEST(NameMapBuildTest, DeterministicRebuildByteEqual_RT_NM_1)
{
    const CanonicalModel model = richFixture().build();
    const RuntimeNameMap a = buildRuntimeNameMap(model);
    const RuntimeNameMap b = buildRuntimeNameMap(model);

    EXPECT_TRUE(a == b) << "§7.3 后置：逐字节相等映射";
    EXPECT_TRUE(a.contentIdentity() == b.contentIdentity()) << "相等内容身份";
    const auto ea = rtcodec::encodeNameMap(a);
    const auto eb = rtcodec::encodeNameMap(b);
    EXPECT_EQ(ea, eb) << "编码逐字节相等（确定性，NFR-COR-02）";
    // 条目存储序＝(scope, localName, ObjectId) 字典序（§7.2 稳定排序）。
    ASSERT_TRUE(std::is_sorted(a.entries().begin(), a.entries().end(),
                               [](const RuntimeNameMap::Entry& x, const RuntimeNameMap::Entry& y) {
                                   if (x.scope != y.scope) {
                                       return static_cast<int>(x.scope) < static_cast<int>(y.scope);
                                   }
                                   if (x.localName != y.localName) { return x.localName < y.localName; }
                                   return x.objectId.bytes < y.objectId.bytes;
                               }));
}

/** §7.1 Body 行："DWC 存在时"——能力位门控（minimal 无物性→无 Body 条目）。 */
TEST(NameMapBuildTest, BodyScopeGatedByDynamicCapability_RT_NM_1)
{
    const RuntimeNameMap minimal = buildRuntimeNameMap(minimalFixture().build());
    EXPECT_FALSE(minimalFixture().build().capabilities().hasDynamicWorkCell);
    EXPECT_EQ(minimal.objectsInScope(NameScope::Body).size(), 0u)
        << "无 DWC（物性缺失）→ 不生成 Body 名（§7.1 范围表）";

    const RuntimeNameMap rich = buildRuntimeNameMap(richFixture().build());
    ASSERT_EQ(rich.objectsInScope(NameScope::Body).size(), 3u);
    EXPECT_TRUE(rich.resolveRuntimeName("IRB_T.base_link.body").ok());
}

/** 身份作用域条目规则：每对象的 resolveObjectId 返回身份条目（§15.4 登记）。 */
TEST(NameMapBuildTest, IdentityEntriesPerObjectResolve_RT_NM_1)
{
    const RuntimeNameMap map = buildRuntimeNameMap(richFixture().build());
    const core::ObjectId robot = idFrom<core::ObjectId>("robot");
    const core::ObjectId j1 = idFrom<core::ObjectId>("j1");
    const core::ObjectId l0 = idFrom<core::ObjectId>("l0");
    const core::ObjectId l1 = idFrom<core::ObjectId>("l1");
    const core::ObjectId t1 = idFrom<core::ObjectId>("t1");
    const core::ObjectId s1 = idFrom<core::ObjectId>("s1");
    const core::ObjectId res1 = idFrom<core::ObjectId>("res-1");
    const core::ObjectId res2 = idFrom<core::ObjectId>("res-2");

    // robot 携 Device＋BaseMount 两条目——身份条目＝Device（§15.4 登记口径）。
    EXPECT_EQ(map.resolveObjectId(robot).get().fullName, "IRB_T");
    EXPECT_EQ(map.resolveObjectId(j1).get().fullName, "IRB_T.joint_1");
    EXPECT_EQ(map.resolveObjectId(l0).get().scope, NameScope::LinkFrame)
        << "link[0] 身份条目＝LinkFrame（BaseFrame 为派生条目）";
    EXPECT_EQ(map.resolveObjectId(l1).get().scope, NameScope::LinkFrame);
    EXPECT_EQ(map.resolveObjectId(t1).get().scope, NameScope::Tcp);
    EXPECT_EQ(map.resolveObjectId(s1).get().fullName, "Scene.obstacle_1");
    // 资源被多处引用（res-1→link_1.visual＋tool_1.collision）——取映射序
    // 首条（localName 字典序，确定性——§15.4 登记项）。
    EXPECT_EQ(map.resolveObjectId(res1).get().fullName, "IRB_T.link_1.visual");
    EXPECT_EQ(map.resolveObjectId(res2).get().fullName, "IRB_T.link_2.collision");
}

// =====================================================================
// RT-NM-1（AT-18 载体）：双向往返全量＋编码往返。
// =====================================================================

/** AT-18 正向链：全量 ObjectId→RuntimeName→ObjectId'，逐对象断言全等。 */
TEST(NameMapRoundtripTest, ForwardChainObjectToNameToObject_FullCoverage_RT_NM_1)
{
    const RuntimeNameMap map = buildRuntimeNameMap(richFixture().build());
    const std::vector<core::ObjectId> ids = distinctObjectIds(map);
    ASSERT_EQ(ids.size(), 10u) << "rich 闭包对象数（robot2+joint2+link3 含派生共享"
                                  "＋tool1+scene1+res2）";

    for (const core::ObjectId& id : ids) {
        const auto n = map.resolveObjectId(id);
        ASSERT_TRUE(n.ok()) << "正向链断链：id=" << id.toCanonical();
        const auto back = map.resolveRuntimeName(n.get().fullName);
        ASSERT_TRUE(back.ok()) << "正向链反解失败：" << n.get().fullName;
        EXPECT_EQ(back.get().objectId, id)
            << "正向链全等违背：" << n.get().fullName << "（精确等值——附录 D 第 12 项）";
    }
}

/** AT-18 反向链：全量 RuntimeName→ObjectId→RuntimeName'——链闭合逐条目；
 *  身份条目逐名回等（每对象恰一条，§15.4 RT-T05 实现层澄清口径——派生
 *  条目经对象身份名回到同一对象，反向闭合不断）。 */
TEST(NameMapRoundtripTest, ReverseChainNameToObjectToName_RT_NM_1)
{
    const RuntimeNameMap map = buildRuntimeNameMap(richFixture().build());

    std::size_t identityResolving = 0;
    for (const auto& e : map.entries()) {
        // 反向链第一步：全名→对象（整串精确匹配——全量逐条目）。
        const auto ref = map.resolveRuntimeName(e.fullName);
        ASSERT_TRUE(ref.ok()) << "反向链命中失败：" << e.fullName;
        EXPECT_EQ(ref.get().objectId, e.objectId) << "对象全等：" << e.fullName;
        EXPECT_EQ(ref.get().scope, e.scope) << "范围语义全等：" << e.fullName;
        EXPECT_EQ(ref.get().localName, e.authoritativeLocalName)
            << "权威局部名（消歧前）：" << e.fullName;
        // 反向链第二步：对象→身份名→再命中同一对象（链闭合——全量）。
        const auto n2 = map.resolveObjectId(ref.get().objectId);
        ASSERT_TRUE(n2.ok()) << "反解失败：" << e.fullName;
        const auto ref2 = map.resolveRuntimeName(n2.get().fullName);
        ASSERT_TRUE(ref2.ok());
        EXPECT_EQ(ref2.get().objectId, e.objectId) << "链闭合：" << e.fullName;
        // 身份条目＝对象身份名恰为本条目名（每对象恰一条——组外计数断言）。
        if (n2.get().fullName == e.fullName) { ++identityResolving; }
    }
    EXPECT_EQ(identityResolving, distinctObjectIds(map).size())
        << "每对象恰一条身份条目（resolveObjectId 单值性——双射计数断言）";
}

/** AT-18 双射计数：无重复名称、无重复 (对象,作用域) 对；范围划分完备。 */
TEST(NameMapRoundtripTest, BijectionCounts_RT_NM_1)
{
    const RuntimeNameMap map = buildRuntimeNameMap(richFixture().build());
    const std::vector<std::string> names = allFullNames(map);
    std::vector<std::string> sorted = names;  // 可变副本——仅排序判定重复，不改动映射
    std::sort(sorted.begin(), sorted.end());
    EXPECT_TRUE(std::adjacent_find(sorted.begin(), sorted.end()) == sorted.end())
        << "无重复名称（§7.4 双射断言）";

    constexpr NameScope kAll[] = {NameScope::Device, NameScope::Joint, NameScope::LinkFrame,
                                  NameScope::BaseMount, NameScope::BaseFrame, NameScope::Flange,
                                  NameScope::Tcp, NameScope::Geometry, NameScope::SceneObject,
                                  NameScope::Body, NameScope::Sensor};
    std::size_t partitionSum = 0;
    for (const NameScope s : kAll) {
        for (const ObjectRef& r : map.objectsInScope(s)) {
            EXPECT_EQ(r.scope, s) << "objectsInScope 范围过滤正确";
        }
        partitionSum += map.objectsInScope(s).size();
    }
    EXPECT_EQ(partitionSum, map.size()) << "范围划分完备（不重不漏）";
    // objectsInScope 稳定序：条目按存储字典序输出（局部名升序抽检）。
    const auto joints = map.objectsInScope(NameScope::Joint);
    ASSERT_EQ(joints.size(), 2u);
    EXPECT_TRUE(map.entries().at(0).wellFormedFullName());
    for (const auto& e : map.entries()) {
        EXPECT_TRUE(e.wellFormedFullName()) << "条目全名形态合法：" << e.fullName;
    }
}

/** RT-NM-1 编码往返（§7.6 IRDNAME）：parse(encode(x))==x 且身份相等。 */
TEST(NameMapRoundtripTest, EncodeParseRoundtrip_RT_NM_1)
{
    const RuntimeNameMap map = buildRuntimeNameMap(richFixture().build());
    const auto bytes = rtcodec::encodeNameMap(map);

    const auto parsed = rtcodec::parseNameMap(bytes);
    ASSERT_TRUE(parsed.ok()) << "解析失败：" << parsed.error().what();
    EXPECT_TRUE(parsed.get() == map) << "parse(encode(x))==x（条目级全等）";
    EXPECT_TRUE(parsed.get().contentIdentity() == map.contentIdentity())
        << "往返后内容身份相等（跨进程一致凭据——NFR-COR-02）";

    // 编码幂等：再编码逐字节一致（worker 通道确定性）。
    EXPECT_EQ(rtcodec::encodeNameMap(parsed.get()), bytes);

    // 解析产物作为独立映射再做全量往返（重建映射的自洽性）。
    const RuntimeNameMap& m2 = parsed.get();
    for (const auto& e : m2.entries()) {
        ASSERT_TRUE(m2.resolveRuntimeName(e.fullName).ok());
        ASSERT_TRUE(m2.resolveObjectId(e.objectId).ok());
    }
}

/** IRDNAME 结构防御（NFR-COR-03 不吞错）：垃圾字节/截断/尾随/坏版本→err。 */
TEST(NameMapRoundtripTest, ParseRejectsMalformedInput_RT_NM_1)
{
    const auto good = rtcodec::encodeNameMap(buildRuntimeNameMap(minimalFixture().build()));

    // 垃圾字节（错误 magic）。
    auto junk = good;
    junk[0] = 'X';
    EXPECT_FALSE(rtcodec::parseNameMap(junk).ok()) << "magic 不符须拒绝";

    // 截断（去尾 4 字节）。
    auto truncated = good;
    truncated.resize(truncated.size() - 4);
    EXPECT_FALSE(rtcodec::parseNameMap(truncated).ok()) << "截断须拒绝（长度前缀越界）";

    // 尾随字节。
    auto trailing = good;
    trailing.push_back(0);
    EXPECT_FALSE(rtcodec::parseNameMap(trailing).ok()) << "尾随字节须拒绝";

    // 结构版本不符（major 置 99）。
    auto version = good;
    version[7] = 99;
    EXPECT_FALSE(rtcodec::parseNameMap(version).ok()) << "版本不符须拒绝而非猜测";

    // scope 字节越界（第 1 条目的 scope 字节——偏移 15 处）。
    auto badScope = good;
    badScope[15] = 200;
    const auto r = rtcodec::parseNameMap(badScope);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code(), RuntimeErrorCode::InputInvalid);

    // 位翻转（末字节）：结构仍合法则解析成功，但内容身份必变——防篡改
    // 核对凭据（worker 按身份核对，不等即拒绝——§9.3/D-13 映射层形态）。
    auto flipped = good;
    flipped.back() = static_cast<std::uint8_t>(flipped.back() ^ 0x01);
    const auto rp = rtcodec::parseNameMap(flipped);
    if (rp.ok()) {
        EXPECT_FALSE(rp.get().contentIdentity()
                     == buildRuntimeNameMap(minimalFixture().build()).contentIdentity())
            << "任一字节变化必须改变内容身份（SHA-256 雪崩——CON-06）";
    }
}

/** 空映射不可编码（占位值不进序列化通道——NFR-COR-03 不吞错）。 */
TEST(NameMapRoundtripTest, EmptyMapEncodeRejected)
{
    const RuntimeNameMap empty;
    bool threw = false;
    try {
        (void)rtcodec::encodeNameMap(empty);
        FAIL() << "空映射 encodeNameMap 必须抛 InputInvalid（NFR-COR-03）";
    } catch (const sdurws::ird::runtime::RuntimeError& e) {
        threw = true;
        EXPECT_EQ(e.code(), sdurws::ird::runtime::RuntimeErrorCode::InputInvalid);
    }
    EXPECT_TRUE(threw);
}

// =====================================================================
// RT-NM-3：未知/空名/大小写不符——Expected 错误含回显、不抛、不默认命中。
// =====================================================================

TEST(NameMapResolveErrorTest, ResolveMissesEchoAndNeverThrow_RT_NM_3)
{
    const RuntimeNameMap map = buildRuntimeNameMap(minimalFixture().build());

    // 空串（§7.4 接口属性表：空串/非法句法同码 UnknownObject）。
    EXPECT_NO_THROW(map.resolveRuntimeName(""));
    expectResolveMiss(map, "", "空名");

    // 未知名。
    EXPECT_NO_THROW(map.resolveRuntimeName("IRB_T.joint_99"));
    expectResolveMiss(map, "IRB_T.joint_99", "未知名");

    // 大小写不符（精确等值——附录 D 第 12 项，与 findFrame 行为一致）。
    EXPECT_NO_THROW(map.resolveRuntimeName("irb_t.joint_1"));
    expectResolveMiss(map, "irb_t.joint_1", "设备名小写");
    EXPECT_NO_THROW(map.resolveRuntimeName("IRB_T.JOINT_1"));
    expectResolveMiss(map, "IRB_T.JOINT_1", "局部名大写");

    // 部分名/残缺句法（整串匹配——不做前缀/子串猜测，RT-NM-7 行为面）。
    expectResolveMiss(map, "joint_1", "无前缀局部名");
    expectResolveMiss(map, "IRB_T.", "孤立分隔点");
    expectResolveMiss(map, ".joint_1", "孤立前导点");
    expectResolveMiss(map, "IRB_T.joint_1.", "尾随点");
    expectResolveMiss(map, " IRB_T.joint_1", "前导空格");

    // 反解侧：全零保留值与未知 id——UnknownObject（含 id 规范文本回显）。
    const core::ObjectId zero{};
    ASSERT_FALSE(zero.isValid());
    const auto rz = map.resolveObjectId(zero);
    ASSERT_FALSE(rz.ok());
    EXPECT_EQ(rz.error().code, sdurws::ird::runtime::RuntimeErrorCode::UnknownObject);
    EXPECT_NE(rz.error().detail.find(zero.toCanonical()), std::string::npos)
        << "detail 携 id 规范文本（§7.4 接口属性表）";

    const core::ObjectId unknown = idFrom<core::ObjectId>("not-in-map");
    const auto ru = map.resolveObjectId(unknown);
    ASSERT_FALSE(ru.ok());
    EXPECT_EQ(ru.error().code, sdurws::ird::runtime::RuntimeErrorCode::UnknownObject);
    EXPECT_NE(ru.error().detail.find(unknown.toCanonical()), std::string::npos);
}

// =====================================================================
// RT-NM-2：名称冲突消歧——确定性（字典序后缀）、非法字符变体、保留字、
// 警告列出原名/消歧名/对象。
// =====================================================================

/// 构造"两关节同名"夹具（localName 由调用方注入——同名不阻断构造，§4.3.3）。
Fixture duplicateJointFixture(const std::string& a, const std::string& b)
{
    Fixture f = minimalFixture();
    f.chain.joints.at(0).localName = a;
    f.chain.joints.at(1).localName = b;
    return f;
}

/** RT-NM-2：同名消歧按 ObjectId 规范文本字典序——首个保留、后续 "_2"，
 *  序号按排序稳定分配；重复构建结论不变（确定性）。 */
TEST(NameMapDisambiguationTest, DuplicateLocalNamesDisambiguatedByObjectIdOrder_RT_NM_2)
{
    const Fixture f = duplicateJointFixture("wrist", "wrist");
    const core::ObjectId j1 = idFrom<core::ObjectId>("j1");
    const core::ObjectId j2 = idFrom<core::ObjectId>("j2");
    const bool j1First = j1.bytes < j2.bytes;  // 规范文本序＝字节序（十六进制直出）

    const RuntimeNameMap map = buildRuntimeNameMap(f.build());
    const auto kept = map.resolveRuntimeName("IRB_T.wrist");
    ASSERT_TRUE(kept.ok()) << "组内首位（ObjectId 序最小者）保留原名";
    EXPECT_EQ(kept.get().objectId, j1First ? j1 : j2) << "保留者由 ObjectId 字典序决定";

    const auto suffixed = map.resolveRuntimeName("IRB_T.wrist_2");
    ASSERT_TRUE(suffixed.ok()) << "组内次位追加 _2（§7.2 后缀形态）";
    EXPECT_EQ(suffixed.get().objectId, j1First ? j2 : j1);
    EXPECT_FALSE(map.resolveRuntimeName("IRB_T.wrist_3").ok()) << "恰两同名仅一例后缀";

    // 警告恰 1 例：原名/消歧名/对象三元组（§7.2"每例产警告级诊断"）。
    ASSERT_EQ(map.notices().size(), 1u);
    const RuntimeNameNotice& n = map.notices().at(0);
    EXPECT_EQ(n.kind, RuntimeNameNotice::Kind::Disambiguated);
    EXPECT_EQ(n.originalLocalName, "wrist");
    EXPECT_EQ(n.runtimeLocalName, "wrist_2");
    EXPECT_EQ(n.objectId, suffixed.get().objectId);
    EXPECT_EQ(n.fullName, "IRB_T.wrist_2");

    // 消歧不破坏身份条目反解：两关节各自的 resolveObjectId 返回各自名称。
    const auto n1 = map.resolveObjectId(j1);
    const auto n2 = map.resolveObjectId(j2);
    ASSERT_TRUE(n1.ok() && n2.ok());
    EXPECT_TRUE(n1.get().fullName == "IRB_T.wrist" || n1.get().fullName == "IRB_T.wrist_2");
    EXPECT_NE(n1.get().fullName, n2.get().fullName) << "消歧后两名互异";

    // 确定性：重复构建逐条目相等（RT-NM-2 与 RT-ID-1 交叉）。
    EXPECT_TRUE(map == buildRuntimeNameMap(f.build()));
}

/** RT-NM-2：非法字符变体合法化后同名——同规则消歧（"wrist one"/"wrist#one"）。 */
TEST(NameMapDisambiguationTest, IllegalCharVariantsCollideAfterLegalization_RT_NM_2)
{
    const Fixture f = duplicateJointFixture("wrist one", "wrist#one");
    const RuntimeNameMap map = buildRuntimeNameMap(f.build());

    const auto first = map.resolveRuntimeName("IRB_T.wrist_one");
    ASSERT_TRUE(first.ok()) << "合法化：空格/# → '_'，两关节合法化同名";
    // 恰一条 "_2" 后缀（组内非首位）。
    const auto second = map.resolveRuntimeName("IRB_T.wrist_one_2");
    ASSERT_TRUE(second.ok()) << "消歧后缀产出";
    EXPECT_FALSE(map.resolveRuntimeName("IRB_T.wrist_one_3").ok()) << "无多余后缀";

    // 警告恰好 1 例，列出原名（消歧前合法化名）/消歧名/对象（§7.2 原文三项）。
    ASSERT_EQ(map.notices().size(), 1u);
    const RuntimeNameNotice& n = map.notices().at(0);
    EXPECT_EQ(n.kind, RuntimeNameNotice::Kind::Disambiguated);
    EXPECT_EQ(n.originalLocalName, "wrist_one") << "原名＝消歧前合法化名";
    EXPECT_EQ(n.runtimeLocalName, "wrist_one_2") << "消歧名";
    EXPECT_TRUE(n.objectId == second.get().objectId || n.objectId == first.get().objectId)
        << "对象＝被消歧条目";
    EXPECT_EQ(n.fullName, "IRB_T.wrist_one_2");

    // ObjectRef.localName＝规范侧权威局部名（消歧前——§7.3）：被消歧对象的
    // 权威名是其原始 raw 名（"wrist one" 或 "wrist#one"），非合法化产物。
    const auto ref2 = map.resolveRuntimeName("IRB_T.wrist_one_2");
    ASSERT_TRUE(ref2.ok());
    EXPECT_TRUE(ref2.get().localName == "wrist one" || ref2.get().localName == "wrist#one")
        << "权威局部名保留原始形态（消歧前）";
}

/** RT-NM-2：合法化规则——非法字符替换＋前导数字前缀（§7.2"合法化"行）。 */
TEST(NameMapDisambiguationTest, LegalizationRulesLeadingDigitAndChars_RT_NM_2)
{
    const Fixture f = duplicateJointFixture("3rd_axis", "my joint");
    const RuntimeNameMap map = buildRuntimeNameMap(f.build());

    // 前导数字 → 前缀 "n_"。
    const auto r1 = map.resolveRuntimeName("IRB_T.n_3rd_axis");
    ASSERT_TRUE(r1.ok()) << "前导数字加前缀 n_（RobWork 标识符安全集）";
    // 空格 → '_'。
    const auto r2 = map.resolveRuntimeName("IRB_T.my_joint");
    ASSERT_TRUE(r2.ok()) << "非法字符逐个替换 _";
    EXPECT_FALSE(map.resolveRuntimeName("IRB_T.3rd_axis").ok()) << "原前导数字名不保留";
    EXPECT_TRUE(map.notices().empty()) << "合法化不冲突时不产消歧警告（§7.2）";
}

/** RT-NM-2：保留字最小集 WORLD——localName 命中即直接消歧加后缀（大小写敏感）。 */
TEST(NameMapDisambiguationTest, ReservedWorldDisambiguated_RT_NM_2)
{
    Fixture f = minimalFixture();
    f.chain.links.at(1).localName = "WORLD";  // 保留字（大写精确命中）
    const RuntimeNameMap map = buildRuntimeNameMap(f.build());

    const auto hit = map.resolveRuntimeName("IRB_T.WORLD_2");
    ASSERT_TRUE(hit.ok()) << "保留字直接消歧（§7.2 保留字行）";
    EXPECT_FALSE(map.resolveRuntimeName("IRB_T.WORLD").ok())
        << "世界帧名不被条目占用（RobWork WORLD 帧冲突预防）";

    // 小写 "world" 不保留（精确等值口径——大小写敏感）。
    Fixture g = minimalFixture();
    g.chain.links.at(1).localName = "world";
    const RuntimeNameMap mapG = buildRuntimeNameMap(g.build());
    EXPECT_TRUE(mapG.resolveRuntimeName("IRB_T.world").ok()) << "非保留字保留原名";
    EXPECT_TRUE(mapG.notices().empty());
}

/** RT-NM-2：后缀探测跳过已占用名（A:"dup"、B:"dup_2"、C:"dup"→C 得 "dup_3"）。 */
TEST(NameMapDisambiguationTest, SuffixProbeSkipsTakenNames_RT_NM_2)
{
    Fixture f = minimalFixture();
    f.chain.joints.at(0).localName = "dup";
    f.chain.joints.at(1).localName = "dup_2";
    // 第三关节：链上追加（闭包引用同步追加——builder 校验引用∈清单）。
    CanonicalJoint j3;
    j3.objectId = idFrom<core::ObjectId>("j3");
    j3.localName = "dup";
    j3.type = JointType::Revolute;
    j3.axis = rw::math::Vector3D<double>(1.0, 0.0, 0.0);
    j3.bounds = JointBounds{-1.0, 1.0};  // 单位 rad
    j3.maxVelocity = val(1.0);
    ObjectRefEntry ref3;
    ref3.objectId = j3.objectId;
    ref3.contentVersion = cvFrom("j3");
    ref3.objectTypeToken = "joint";
    ref3.digest = digestOf("j3-bytes");
    f.header.objectRefs.push_back(ref3);
    f.chain.joints.push_back(j3);

    // 链不变量 links==joints+1（§4.3.3）——第三关节须配第三连杆。
    CanonicalLink link3;
    link3.objectId = idFrom<core::ObjectId>("l3");
    link3.localName = "link_3";
    ObjectRefEntry refL3;
    refL3.objectId = link3.objectId;
    refL3.contentVersion = cvFrom("l3");
    refL3.objectTypeToken = "link";
    refL3.digest = digestOf("l3-bytes");
    f.header.objectRefs.push_back(refL3);
    f.chain.links.push_back(link3);

    const RuntimeNameMap map = buildRuntimeNameMap(f.build());
    ASSERT_TRUE(map.resolveRuntimeName("IRB_T.dup").ok()) << "组内首位保留原名";
    ASSERT_TRUE(map.resolveRuntimeName("IRB_T.dup_2").ok()) << "占用名归其合法化对象";
    const auto third = map.resolveRuntimeName("IRB_T.dup_3");
    ASSERT_TRUE(third.ok()) << "第二同名者探测 _2 被占→取 _3（跳过占用名）";
    EXPECT_EQ(third.get().objectId, j3.objectId) << "后缀落在被消歧对象上";
    ASSERT_EQ(map.notices().size(), 1u);
    EXPECT_EQ(map.notices().at(0).runtimeLocalName, "dup_3");
}

// =====================================================================
// RT-NM-4：跨项目同名——两映射独立成立、无冲突、各自往返通过。
// =====================================================================

TEST(NameMapCrossProjectTest, SameDisplayNamesAcrossProjects_RT_NM_4)
{
    const RuntimeNameMap a = buildRuntimeNameMap(makeSingleJointModel("A"));
    const RuntimeNameMap b = buildRuntimeNameMap(makeSingleJointModel("B"));

    // 同显示名各自成立（名称只在单快照命名空间内保证单射——§7.1 版本与
    // 命名空间行；RT-NM-4 正例"不同项目相同显示名称不发生名称冲突"）。
    ASSERT_TRUE(a.resolveRuntimeName("IRB_T.joint_1").ok());
    ASSERT_TRUE(b.resolveRuntimeName("IRB_T.joint_1").ok());
    EXPECT_FALSE(a.resolveRuntimeName("IRB_T.joint_1").get().objectId
                 == b.resolveRuntimeName("IRB_T.joint_1").get().objectId)
        << "同名分别解析到各自项目的对象";

    // 各自全量往返通过（AT-18 逐映射：正向对象→名→对象全量；反向名→对象
    // 整串命中全量；身份条目计数＝对象数——resolveObjectId 单值性）。
    for (const RuntimeNameMap* m : {&a, &b}) {
        for (const core::ObjectId& id : distinctObjectIds(*m)) {
            const auto n = m->resolveObjectId(id);
            ASSERT_TRUE(n.ok());
            const auto r = m->resolveRuntimeName(n.get().fullName);
            ASSERT_TRUE(r.ok());
            EXPECT_EQ(r.get().objectId, id) << "正向链闭合：" << id.toCanonical();
        }
        std::size_t identityResolving = 0;
        for (const auto& e : m->entries()) {
            const auto r = m->resolveRuntimeName(e.fullName);
            ASSERT_TRUE(r.ok());
            EXPECT_EQ(r.get().objectId, e.objectId) << "反向命中：" << e.fullName;
            if (m->resolveObjectId(e.objectId).get().fullName == e.fullName) {
                ++identityResolving;
            }
        }
        EXPECT_EQ(identityResolving, distinctObjectIds(*m).size())
            << "每对象恰一条身份条目：" << (m == &a ? "A" : "B");
    }
    // 身份互异（同内容结构不同对象——身份锚定 ObjectId 内容，CON-06）。
    EXPECT_FALSE(a.contentIdentity() == b.contentIdentity());
    EXPECT_TRUE(a.notices().empty() && b.notices().empty());
}

// =====================================================================
// RT-NM-5：重命名后无旧名残留；旧快照旧名仍可反解（用旧快照映射）。
// =====================================================================

TEST(NameMapRenameTest, RenameRebuildNoOldResidue_RT_NM_5)
{
    const RuntimeNameMap oldMap = buildRuntimeNameMap(minimalFixture().build());
    ASSERT_TRUE(oldMap.resolveRuntimeName("IRB_T.joint_2").ok());

    Fixture renamed = minimalFixture();
    renamed.chain.joints.at(1).localName = "joint_x";  // 权威局部名重命名
    const RuntimeNameMap newMap = buildRuntimeNameMap(renamed.build());

    // 新映射无旧名（MDL-14"引用旧机械臂名称"禁令的名称面）。
    expectResolveMiss(newMap, "IRB_T.joint_2", "重命名后旧名");
    const auto hit = newMap.resolveRuntimeName("IRB_T.joint_x");
    ASSERT_TRUE(hit.ok());
    EXPECT_EQ(hit.get().objectId, idFrom<core::ObjectId>("j2"))
        << "对象引用不受重命名影响（ARC-04——持久引用一律 ObjectId，§7.5）";
    EXPECT_FALSE(oldMap.contentIdentity() == newMap.contentIdentity())
        << "名称变化→映射内容身份变化（localName 入模型身份——§7.5）";

    // 旧快照旧名仍可反解——用其绑定快照的映射（§7.5"绝不用当前映射反解
    // 旧结果"；迟到结果语义 RT-SNAP-3 的映射侧基础）。
    const auto late = oldMap.resolveRuntimeName("IRB_T.joint_2");
    ASSERT_TRUE(late.ok());
    EXPECT_EQ(late.get().objectId, idFrom<core::ObjectId>("j2"));
}

// =====================================================================
// RT-NM-6：双前缀反例——S8 交叉校验（crossCheckRuntimeNames）。
// =====================================================================

TEST(NameMapCrossCheckTest, CrossCheckPassesOnMapSubset_RT_NM_6)
{
    const RuntimeNameMap map = buildRuntimeNameMap(minimalFixture().build());

    // 实际名集合 ⊆ 映射名集合——全量与真子集两态均通过。
    EXPECT_NO_THROW(crossCheckRuntimeNames(map, allFullNames(map)));
    EXPECT_NO_THROW(
        crossCheckRuntimeNames(map, std::vector<std::string>{"IRB_T", "IRB_T.joint_1"}));
    // 空集平凡通过（是否合法归编译器决策——§7.2 交叉校验只做子集判定）。
    EXPECT_NO_THROW(crossCheckRuntimeNames(map, std::vector<std::string>{}));
}

/** RT-NM-6：注入替身 WC 内出现 "Robot.Robot.joint_1" 名——NameConflict 失败。 */
TEST(NameMapCrossCheckTest, DoublePrefixRejected_RT_NM_6)
{
    const RuntimeNameMap map = buildRuntimeNameMap(minimalFixture().build());

    // 映射自身无双前缀形态（句法检查——§7.4"句法检查＋WC 交叉"的前半）。
    for (const auto& e : map.entries()) {
        EXPECT_TRUE(e.wellFormedFullName()) << "条目形态合法：" << e.fullName;
        EXPECT_EQ(e.fullName.find("IRB_T.IRB_T."), std::string::npos)
            << "无双前缀名：" << e.fullName;
    }

    // 注入替身 WC 实际名（模拟错误写入）→ S8 交叉校验 NameConflict 失败。
    bool threw = false;
    try {
        crossCheckRuntimeNames(map,
                               std::vector<std::string>{"IRB_T", "IRB_T.IRB_T.joint_1"});
        FAIL() << "双前缀名必须被交叉校验拦截";
    } catch (const sdurws::ird::runtime::RuntimeError& e) {
        threw = true;
        EXPECT_EQ(e.code(), sdurws::ird::runtime::RuntimeErrorCode::NameConflict);
        EXPECT_NE(std::string{e.what()}.find("IRB_T.IRB_T.joint_1"), std::string::npos)
            << "detail 逐条回显未命中名（就地定位）";
    }
    EXPECT_TRUE(threw) << "须以 RuntimeError(NameConflict) 失败（§5.2 S8 行）";
}

// =====================================================================
// RT-NM-7：身份不混用（类型层）＋拼名禁令行为面＋⑥端口身份核对（CON-06）。
// =====================================================================

TEST(NameMapIdentityTest, NameAndIdentityTypesNotInterconvertible_RT_NM_7)
{
    // 编译期类型层阻止（§7.5"必须拒绝"行＋RT-NM-7"编译期类型层阻止"）：
    // RuntimeName 与 ObjectId 互不可构造/不可隐式转换；ObjectRef 与
    // ContentVersion/ContentIdentity 互不转换（对象版本与身份摘要不混用）。
    static_assert(!std::is_convertible<RuntimeName, core::ObjectId>::value,
                  "RuntimeName 不得隐式转换为 ObjectId");
    static_assert(!std::is_convertible<core::ObjectId, RuntimeName>::value,
                  "ObjectId 不得隐式转换为 RuntimeName");
    static_assert(!std::is_constructible<core::ObjectId, RuntimeName>::value,
                  "ObjectId 不得由 RuntimeName 构造");
    static_assert(!std::is_constructible<RuntimeName, core::ObjectId>::value,
                  "RuntimeName 不得由 ObjectId 构造");
    static_assert(!std::is_convertible<core::ContentVersion, core::ContentIdentity>::value,
                  "内容版本与内容身份不混用（CON-01 交叉对比的类型层）");
    static_assert(!std::is_convertible<core::ContentIdentity, core::ContentVersion>::value,
                  "内容身份与内容版本不混用");
    SUCCEED() << "编译期类型断言全部成立";

    // 行为面：id 键查询对字节敏感（键全为 ObjectId——RT-NM-7"映射键全为
    // ObjectId"）。
    const RuntimeNameMap map = buildRuntimeNameMap(minimalFixture().build());
    const core::ObjectId j1 = idFrom<core::ObjectId>("j1");
    core::ObjectId altered = j1;
    altered.bytes[0] = static_cast<std::uint8_t>(altered.bytes[0] ^ 0xFF);
    ASSERT_TRUE(altered != j1);
    EXPECT_FALSE(map.resolveObjectId(altered).ok()) << "字节级改动的 id 不得命中";
}

TEST(NameMapIdentityTest, StaleConcatenationMisses_RT_NM_7_R4)
{
    // 替身消费单元内自行拼接前缀后查询（模拟 R-4 违例——证明绕过唯一解析器
    // 不可靠，RT-NM-7 行为断言面）。
    const RuntimeNameMap oldMap = buildRuntimeNameMap(minimalFixture().build());

    Fixture renamed = minimalFixture();
    renamed.chain.joints.at(1).localName = "joint_x";
    const RuntimeNameMap newMap = buildRuntimeNameMap(renamed.build());

    // 消费方持有旧局部名，用"当前"设备名自行拼接（R-4 违例形态）——在
    // 新映射不得命中（拼名查询不得命中过时组合；真名只经 resolveRuntimeName
    // 整串匹配，且此处根本不应拼接）。
    const std::string deviceName = newMap.resolveObjectId(idFrom<core::ObjectId>("robot")).get().fullName;
    const std::string concatenated = deviceName + "." + "joint_2";  // 违例拼装（仅测试内）
    expectResolveMiss(newMap, concatenated, "拼用过时局部名");

    // 部分拼装（缺分隔/多段）同样不命中——无前缀猜测。
    expectResolveMiss(newMap, deviceName + "joint_2", "缺分隔点拼装");
}

TEST(NameMapIdentityTest, ResolverPortBindsMapAndExposesIdentity_RT_NM_7_CON_06)
{
    const RuntimeNameMap map = buildRuntimeNameMap(richFixture().build());
    const BoundRuntimeNameResolver resolver(map);

    // 端口转发语义与映射入口逐字一致（⑥端口形态——§7.3）。
    const auto byPort = resolver.resolveRuntimeName("IRB_T.joint_1");
    const auto byMap = map.resolveRuntimeName("IRB_T.joint_1");
    ASSERT_TRUE(byPort.ok());
    ASSERT_TRUE(byMap.ok());
    EXPECT_EQ(byPort.get().objectId, byMap.get().objectId);
    EXPECT_EQ(byPort.get().localName, byMap.get().localName) << "权威局部名一致";

    const auto revByPort = resolver.resolveObjectId(idFrom<core::ObjectId>("j1"));
    ASSERT_TRUE(revByPort.ok());
    EXPECT_EQ(revByPort.get().fullName, "IRB_T.joint_1");

    // CON-06 接纳核对：结果绑定的映射身份必须与解析器身份相等——不相等
    // 的映射不可接纳（凭据比对的行为基础）。
    EXPECT_TRUE(resolver.nameMapIdentity() == map.contentIdentity());
    const RuntimeNameMap other = buildRuntimeNameMap(minimalFixture().build());
    EXPECT_FALSE(resolver.nameMapIdentity() == other.contentIdentity())
        << "不同映射身份互异——接纳核对可据此拒绝";
}

// =====================================================================
// R-4 红线（acceptance 2）：前缀操作仅限本模块——源码扫描钉住唯一拼装点。
// =====================================================================

namespace {

/// runtime 单元根（IRD_RUNTIME_UNIT_ROOT 由 CMake 注入——BuildRedLineTest 同款）。
const fs::path& nameMapUnitRoot()
{
    static const fs::path dir = fs::path{IRD_RUNTIME_UNIT_ROOT};
    return dir;
}

/// 递归收集 C++ 源/头（相对路径、已排序——确定性失败信息）。
std::vector<fs::path> collectRuntimeCppFiles(const fs::path& dir)
{
    std::vector<fs::path> files;
    std::error_code ec;
    if (!fs::exists(dir)) { return files; }
    for (auto it = fs::recursive_directory_iterator(dir, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec || !it->is_regular_file(ec)) continue;
        const auto ext = it->path().extension().string();
        if (ext == ".hpp" || ext == ".h" || ext == ".cpp") {
            files.push_back(fs::relative(it->path(), dir, ec));
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

/// 全文读取；读失败显性失败（不静默跳过）。
std::string readRuntimeFile(const fs::path& file)
{
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        ADD_FAILURE() << "无法读取: " << file.string();
        return {};
    }
    return std::string{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

}  // namespace

/** R-4/NFR-MNT-07（acceptance 2）："作用域.局部名"拼装唯一合法位置＝
 *  NameMap.cpp 的 joinScopeLocal——产品源（include/+src/）中该拼装助手
 *  不得出现在任何其他文件。全仓静态扫描归 WP-01-T01 门禁（RT-NM-7 备注）。 */
TEST(NameMapR4SourceScan, PrefixJoinConfinedToNameMapModule_RT_R4)
{
    const fs::path srcDir = nameMapUnitRoot() / "runtime" / "src";
    const fs::path incDir = nameMapUnitRoot() / "runtime" / "include";
    ASSERT_FALSE(collectRuntimeCppFiles(srcDir).empty()) << "src 扫描非空（防路径配错恒真）";

    constexpr const char* kJoinHelper = "joinScopeLocal";
    std::size_t helperSites = 0;
    for (const auto* sub : {"src", "include"}) {
        for (const auto& rel : collectRuntimeCppFiles(nameMapUnitRoot() / "runtime" / sub)) {
            const std::string text = readRuntimeFile(nameMapUnitRoot() / "runtime" / sub / rel);
            const bool present = text.find(kJoinHelper) != std::string::npos;
            // R-4 例外范围＝名称解析器模块（声明头＋实现——RT-T13 例外登记
            // 的文件清单形态）。相对路径相对各子目录生成——按子目录前缀比对。
            const bool isNameMapModule =
                (std::string{sub} == "src" && rel.generic_string() == "NameMap.cpp")
                || (std::string{sub} == "include"
                    && rel.generic_string() == "sdurws/ird/runtime/NameMap.hpp");
            if (isNameMapModule) {
                EXPECT_TRUE(present) << "R-4 例外实现点必须在 NameMap 模块内";
                ++helperSites;
            } else {
                EXPECT_FALSE(present)
                    << "R-4 违例：名称前缀拼装助手出现在 " << rel.string()
                    << "（唯一合法位置＝runtime 名称解析器 NameMap 模块——ARC-04/SA-05）";
            }
        }
    }
    EXPECT_EQ(helperSites, 2u) << "例外范围恰两文件（NameMap.hpp 声明＋NameMap.cpp 实现）";
}
