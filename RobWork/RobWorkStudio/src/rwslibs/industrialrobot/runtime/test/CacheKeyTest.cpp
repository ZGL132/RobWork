/**
 * @file   CacheKeyTest.cpp
 * @brief  RT-T10 测试——编译缓存纯判定（RT-CACHE-1～4＋判定表反例）。
 *
 * 设计依据（用例↔需求/验收标准追溯——§11 反例矩阵与任务契约 acceptance）：
 *   - RT-CACHE-1 键完备（CON-04）：逐分量扰动（选项/编译器版本/基线版本/
 *     规则版本/编码版本/能力级别）→键变；内容类扰动（几何资源字节变/
 *     质量值变/工具 TCP 变）各自经 modelIdentity 变键且全部落在
 *     model-changed；**策略内容身份变化→键不变**（策略归 evidence 切片，
 *     CON-06——以指纹表分量集合钉住"键中无策略条目"的结构事实）；
 *     reasons 精确（逐例断言差异清单恰为该分量词）。
 *   - RT-CACHE-2 WC 复用 DWC 不可（CON-04/§9.4 判定表）：cached 物性缺失
 *     （DWC 键 nullopt）、requested 要求 DWC→WorkCellOnlyReuse（显式非
 *     FullReuse——"不得作为完整命中上报"）。
 *   - RT-CACHE-3 旧版本拒绝（CON-04）：compilerContractVersion 不等→
 *     Incompatible＋contract-changed reason——不存在"版本接近可凑用"。
 *   - RT-CACHE-4 命中≠当前性（CON-05/AT-05）：机器人设计对象未变→键不变
 *     →FullReuse（运行时缓存不失效）；当前性变化归 evidence
 *     computeCurrentness——本测试只断言 runtime 侧行为（键路径无 HEAD/
 *     时钟输入；单元卡 RT-CACHE-4 行口径）。
 *   - acceptance 3（部分/失败产物不得作为完整命中）：未 finalize 键
 *     （workCellKey 全零）与伪造部分键（在场 DWC 键全零）恒 Incompatible
 *     ——§9.4 判定表"部分模型/失败产物永不构成命中"行。
 *   - acceptance 2（存储归 execution，只做纯判定）：被测面仅 buildKey/
 *     judge 纯函数（无存储 API 可测——模块边界由实现面与 §15.4 v0.11
 *     登记承载）；judge 纯函数确定性在本文件逐例隐含断言。
 *
 * 工程事实（两模式差异，CMake 同步登记）：被测面复用 src/Snapshot.cpp 的
 * snapshotidentity::computeWorkCellCompileIdentity 单点（集成模式编译单元）
 * ——本文件只在集成模式编译（§15.4 v0.11 登记）。
 *
 * 确定性：夹具 id/摘要由固定种子派生（CanonicalModelFixture 同款）；键与
 * 判定全部为纯函数——无随机/locale 依赖。
 */

#include <sdurws/ird/runtime/CacheKey.hpp>     // 被测：分层键＋judge（§9.4）

#include <sdurws/ird/runtime/Adapter.hpp>             // RobWorkBaselineVersion（单点默认）
#include <sdurws/ird/runtime/NameMap.hpp>             // kNameMapRuleVersion（单点）
#include <sdurws/ird/runtime/Snapshot.hpp>            // computeWorkCellCompileIdentity
                                                      // （单点互证——键复用断言）

#include "CanonicalModelFixture.hpp"  // minimal/rich 夹具（确定性种子）

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace {

using namespace sdurws::ird::runtime;
using sdurws::ird::runtime::testfixture::Fixture;
using sdurws::ird::runtime::testfixture::digestOf;
using sdurws::ird::runtime::testfixture::idFrom;
using sdurws::ird::runtime::testfixture::richFixture;
using sdurws::ird::runtime::testfixture::val;
namespace core = sdurws::ird::core;

// =====================================================================
// 助手（判定结论的可读输出与批量断言——失败信息面向人工 review）。
// =====================================================================

/// verdict 的可读名（失败信息用——枚举直打数字不可读）。
const char* verdictName(CompileCacheCompatibility::Verdict v)
{
    switch (v) {
    case CompileCacheCompatibility::Verdict::FullReuse: return "FullReuse";
    case CompileCacheCompatibility::Verdict::WorkCellOnlyReuse: return "WorkCellOnlyReuse";
    case CompileCacheCompatibility::Verdict::Incompatible: return "Incompatible";
    }
    return "?";
}

/// 断言判定结论＝期望 verdict 且 reasons 恰为期望清单（顺序敏感——reasons
/// 按分量编码序排列，顺序即契约面）。
void expectVerdict(const char* what, const CompileCacheCompatibility& r,
                   CompileCacheCompatibility::Verdict verdict,
                   std::vector<std::string> reasons)
{
    EXPECT_TRUE(r.verdict == verdict)
        << what << "：verdict 实得 " << verdictName(r.verdict) << "，期望 " << verdictName(verdict)
        << "（reasons=" << r.reasons.size() << " 条）";
    ASSERT_EQ(r.reasons.size(), reasons.size())
        << what << "：reasons 条数不符——实得首条="
        << (r.reasons.empty() ? std::string{"<空>"} : r.reasons.front());
    for (std::size_t i = 0; i < reasons.size(); ++i) {
        EXPECT_EQ(r.reasons[i], reasons[i])
            << what << "：reasons[" << i << "] 不符（精确性断言——RT-CACHE-1）";
    }
}

/// 断言两键的三个组成部分逐一相等（分层键的完整等值——复合键＋指纹表）。
void expectKeysEqual(const char* what, const CompileCacheKey& a, const CompileCacheKey& b)
{
    EXPECT_TRUE(a.workCellKey == b.workCellKey) << what << "：workCellKey 不等";
    EXPECT_EQ(a.dynamicWorkCellKey.has_value(), b.dynamicWorkCellKey.has_value())
        << what << "：DWC 键在场性不等";
    if (a.dynamicWorkCellKey.has_value() && b.dynamicWorkCellKey.has_value()) {
        EXPECT_TRUE(*a.dynamicWorkCellKey == *b.dynamicWorkCellKey) << what << "：DWC 键不等";
    }
    ASSERT_EQ(a.components.size(), b.components.size()) << what << "：指纹条数不等";
    for (std::size_t i = 0; i < a.components.size(); ++i) {
        EXPECT_EQ(a.components[i].component, b.components[i].component)
            << what << "：指纹[" << i << "] 分量名不等";
        EXPECT_TRUE(a.components[i].digest == b.components[i].digest)
            << what << "：指纹[" << i << "] 摘要不等";
    }
}

/// 键生成器（RT-T10 默认面：契约版本 1＋实现版本 impl-cache-1.0＋其余
/// 分量取各单点默认——产品装配口径；扰动用例按需传参覆盖）。
RuntimeCompileCacheKeyBuilder makeBuilder()
{
    return RuntimeCompileCacheKeyBuilder(1u, std::string{"impl-cache-1.0"});
}

/// 把资源 res-1 的内容摘要整体换新（"几何资源字节变"的模型层承载——
/// 资源内容变化→清单与全部引用处的摘要同步更新；只改清单会让引用
/// 失配而触发 builder 拒绝，故逐引用按 resourceId 回填）。
void mutateResourceBytes(Fixture& f)
{
    const core::ObjectId res1 = idFrom<core::ObjectId>("res-1");
    const core::Digest256 fresh = digestOf("mesh-1-bytes-v2");  // 资源字节变化后的新摘要
    auto rebind = [&res1, &fresh](ResourceRef& ref) {
        if (ref.resourceId == res1) { ref.contentDigest = fresh; }
    };
    auto rebindOpt = [&rebind](std::optional<ResourceRef>& ref) {
        if (ref.has_value()) { rebind(*ref); }
    };
    for (ResourceRef& r : f.manifest) { rebind(r); }
    for (CanonicalLink& l : f.chain.links) {
        rebindOpt(l.visual);
        rebindOpt(l.collision);
    }
    for (CanonicalTool& t : f.tools) { rebindOpt(t.geometry); }
    for (CanonicalSceneObject& s : f.scene) { rebind(s.geometry); }
}

}  // namespace

// =====================================================================
// RT-CACHE-1（前半）：分层键生成——D-04 分层、确定性、单点复用互证。
// =====================================================================

TEST(CacheKeyTest, BuildsTwoLayerKeysDeterministic_RT_CACHE_1)
{
    const CanonicalModel model = richFixture().build();
    ASSERT_TRUE(model.contentIdentity().isValid()) << "夹具模型身份应非零（前置自检）";
    const RuntimeCompileCacheKeyBuilder builder = makeBuilder();

    // 要求 DWC 的请求→两层键齐备（WC 非零＋DWC 非零＋8 条指纹）。
    const CompileCacheKey k1 = builder.buildKey(model, CompileOptions{});
    EXPECT_TRUE(k1.workCellKey.isValid()) << "workCellKey 应非零（§9.1 合法列）";
    ASSERT_TRUE(k1.dynamicWorkCellKey.has_value()) << "要求 DWC→DWC 键应在场";
    EXPECT_TRUE(k1.dynamicWorkCellKey->isValid()) << "DWC 键应非零";
    EXPECT_EQ(k1.components.size(), std::size_t{8}) << "WC 层分量指纹应 8 条";

    // D-04 分层：requestDynamicWorkCell（capabilityLevel）不入 WC 键——
    // 关掉 DWC 后 WC 键不变、DWC 键转 nullopt（§9.4 分量表"能力级别"行）。
    CompileOptions noDwc;
    noDwc.requestDynamicWorkCell = false;
    const CompileCacheKey k2 = builder.buildKey(model, noDwc);
    EXPECT_TRUE(k2.workCellKey == k1.workCellKey)
        << "capabilityLevel 只入 DWC 层键（DWC 无关子集——§9.4/D-04）";
    EXPECT_FALSE(k2.dynamicWorkCellKey.has_value())
        << "未请求 DWC→请求侧 DWC 键 nullopt（§9.4 草图语义）";

    // 确定性（NFR-COR-02）：同输入重复生成→三部分逐一相等——键是纯内容
    // 函数，无会话/时钟状态。
    const CompileCacheKey k1Again = builder.buildKey(model, CompileOptions{});
    expectKeysEqual("重复生成应逐字节等值", k1Again, k1);

    // 单点复用互证（§15.4 v0.10②"RT-T10 复用该函数避免第二套键编码"的
    // 行为钉住）：buildKey 的 WC 键必须与快照侧公式单点的同参数输出
    // 逐字节相等——若有人在本模块重写第二套 WC 编码，此处即红。
    const core::ContentIdentity viaFormula = snapshotidentity::computeWorkCellCompileIdentity(
        model.contentIdentity(), 1u, std::string{"impl-cache-1.0"},
        RobWorkBaselineVersion::capture().text, kNameMapRuleVersion,
        codecVersionsFromCodecHeaders(), CompileOptions{});
    EXPECT_TRUE(k1.workCellKey == viaFormula)
        << "WC 键应等于 §9.1 公式单点输出（复用而非第二套编码）";
}

// =====================================================================
// RT-CACHE-1（后半）：逐分量扰动→键变＋reasons 精确；内容类变化全部
// 落在 model-changed。
// =====================================================================

TEST(CacheKeyTest, ComponentPerturbationPreciseReasons_RT_CACHE_1)
{
    const CanonicalModel model = richFixture().build();
    const RuntimeCompileCacheKeyBuilder base = makeBuilder();
    const CompileOptions opts;  // 默认选项（要求 DWC）
    const CompileCacheKey cached = base.buildKey(model, opts);
    const auto judge = [](const CompileCacheKey& r, const CompileCacheKey& c) {
        return judgeCompileCacheCompatibility(r, c);
    };

    // —— 编译器/环境侧分量逐项扰动（builder 传参覆盖；默认值＝各单点，
    // 扰动值只在此测试注入——产品装配恒用单点）——
    // 契约版本（RT-CACHE-3 的分量面；此处验证键变＋精确词）。
    {
        const CompileCacheKey req
            = RuntimeCompileCacheKeyBuilder(2u, std::string{"impl-cache-1.0"})
                  .buildKey(model, opts);
        EXPECT_FALSE(req.workCellKey == cached.workCellKey) << "契约版本变→WC 键变";
        expectVerdict("契约版本扰动", judge(req, cached),
                      CompileCacheCompatibility::Verdict::Incompatible, {"contract-changed"});
    }
    // 实现版本（"契约或实现变化＝新键"——§9.4 分量表）。
    {
        const CompileCacheKey req
            = RuntimeCompileCacheKeyBuilder(1u, std::string{"impl-cache-1.1"})
                  .buildKey(model, opts);
        EXPECT_FALSE(req.workCellKey == cached.workCellKey) << "实现版本变→WC 键变";
        expectVerdict("实现版本扰动", judge(req, cached),
                      CompileCacheCompatibility::Verdict::Incompatible, {"compiler-changed"});
    }
    // 基线版本（基线升级＝新键——NFR-DEP-05/§9.4）。
    {
        const CompileCacheKey req
            = RuntimeCompileCacheKeyBuilder(1u, std::string{"impl-cache-1.0"},
                                            std::string{"vendored-other"})
                  .buildKey(model, opts);
        EXPECT_FALSE(req.workCellKey == cached.workCellKey) << "基线版本变→WC 键变";
        expectVerdict("基线版本扰动", judge(req, cached),
                      CompileCacheCompatibility::Verdict::Incompatible, {"baseline-changed"});
    }
    // 名称规则版本（规则变化＝名称可能变＝新键——§9.4）。
    {
        const CompileCacheKey req
            = RuntimeCompileCacheKeyBuilder(1u, std::string{"impl-cache-1.0"},
                                            RobWorkBaselineVersion::capture().text, 2u)
                  .buildKey(model, opts);
        EXPECT_FALSE(req.workCellKey == cached.workCellKey) << "名称规则版本变→WC 键变";
        expectVerdict("名称规则版本扰动", judge(req, cached),
                      CompileCacheCompatibility::Verdict::Incompatible,
                      {"namemap-rule-changed"});
    }
    // 编码版本（编码升版＝全体身份变化——§4.5；geometryDetail 为阶段 A
    // 单值枚举不扰动，与 RT-T09 键公式用例同口径）。
    {
        SnapshotCodecVersions codec2 = codecVersionsFromCodecHeaders();
        codec2.canonicalModelMajor = 2;
        const CompileCacheKey req
            = RuntimeCompileCacheKeyBuilder(1u, std::string{"impl-cache-1.0"},
                                            RobWorkBaselineVersion::capture().text,
                                            kNameMapRuleVersion, codec2)
                  .buildKey(model, opts);
        EXPECT_FALSE(req.workCellKey == cached.workCellKey) << "编码版本变→WC 键变";
        expectVerdict("编码版本扰动", judge(req, cached),
                      CompileCacheCompatibility::Verdict::Incompatible, {"codec-changed"});
    }
    // WC 层选项子集（includeCollisionGeometry 入 WC 键——D-04）。
    {
        CompileOptions noCollision;
        noCollision.includeCollisionGeometry = false;
        const CompileCacheKey req = base.buildKey(model, noCollision);
        EXPECT_FALSE(req.workCellKey == cached.workCellKey) << "选项变→WC 键变";
        expectVerdict("WC 选项扰动", judge(req, cached),
                      CompileCacheCompatibility::Verdict::Incompatible,
                      {"wc-options-changed"});
    }
    // 能力级别（requestDynamicWorkCell true→false）：WC 键**不变**、
    // DWC 缺性变→WorkCellOnlyReuse（有向：请求侧无 DWC/缓存侧有）。
    {
        CompileOptions noDwc;
        noDwc.requestDynamicWorkCell = false;
        const CompileCacheKey req = base.buildKey(model, noDwc);
        EXPECT_TRUE(req.workCellKey == cached.workCellKey)
            << "能力级别不入 WC 键（§9.4 分量表——DWC 或 WC 键变化的分层归属）";
        expectVerdict("能力级别扰动（请求侧关闭）", judge(req, cached),
                      CompileCacheCompatibility::Verdict::WorkCellOnlyReuse,
                      {"dwc-not-requested"});
        expectVerdict("能力级别扰动（缓存侧缺失方向）", judge(cached, req),
                      CompileCacheCompatibility::Verdict::WorkCellOnlyReuse,
                      {"dwc-missing-in-cached"});
    }

    // —— 内容类分量扰动（全部经 modelIdentity 变键；reasons 全部恰为
    // model-changed——RT-CACHE-1"内容类变化全部落在 model-changed"）——
    // ① 质量值变（数值位模式入身份——§4.5）。
    {
        Fixture f = richFixture();
        f.chain.links.at(1).mass = val(4.5);  // 单位 kg（夹具原值 4.0）
        const CompileCacheKey req = base.buildKey(f.build(), opts);
        EXPECT_FALSE(req.workCellKey == cached.workCellKey) << "质量值变→modelIdentity 变→键变";
        expectVerdict("质量值扰动", judge(req, cached),
                      CompileCacheCompatibility::Verdict::Incompatible, {"model-changed"});
    }
    // ② 工具 TCP 变（法兰→TCP 变换入身份——KIN-14 权威来源）。
    {
        Fixture f = richFixture();
        f.tools.at(0).tcpOffset = rw::math::Transform3D<double>(
            rw::math::Vector3D<double>(0.05, 0.0, 0.0),   // 单位 m（平移分量变化）
            rw::math::Rotation3D<double>(1, 0, 0, 0, 1, 0, 0, 0, 1));
        const CompileCacheKey req = base.buildKey(f.build(), opts);
        EXPECT_FALSE(req.workCellKey == cached.workCellKey) << "工具 TCP 变→modelIdentity 变→键变";
        expectVerdict("工具 TCP 扰动", judge(req, cached),
                      CompileCacheCompatibility::Verdict::Incompatible, {"model-changed"});
    }
    // ③ 几何资源字节变（资源内容摘要入身份、路径不入——§8.6/§9.4 分量表
    // "资源内容身份：经 modelIdentity，不单列第二键"）。
    {
        Fixture f = richFixture();
        mutateResourceBytes(f);
        const CompileCacheKey req = base.buildKey(f.build(), opts);
        EXPECT_FALSE(req.workCellKey == cached.workCellKey) << "资源字节变→modelIdentity 变→键变";
        expectVerdict("资源字节扰动", judge(req, cached),
                      CompileCacheCompatibility::Verdict::Incompatible, {"model-changed"});
    }
}

// =====================================================================
// RT-CACHE-1（结构性钉住）：指纹表分量集合恰为 WC 层 8 分量——策略/
// 种子/线程/HEAD **不在键中**（§9.4 FullReuse 行：它们经 evidence 切片
// 身份进入评估缓存，CON-06）。分量集合是实现面，此处把它钉成可断言面：
// 任何把策略类输入混入键的改动即红。
// =====================================================================

TEST(CacheKeyTest, FingerprintSetPinsPureContentKey_RT_CACHE_1)
{
    const CanonicalModel model = richFixture().build();
    const CompileCacheKey key = makeBuilder().buildKey(model, CompileOptions{});

    ASSERT_EQ(key.components.size(), std::size_t{8}) << "WC 层分量应恰 8 条";
    // 期望序＝WC 键编码域分量序（§9.1 公式声明序——与实现注释一致）。
    const std::vector<std::string> expected = {
        "model",           // CanonicalModel 内容身份（含资源摘要/数值位模式/契约版本）
        "contract",        // 编译器契约版本
        "compiler",        // 编译器实现版本
        "baseline",        // RobWork 基线版本
        "namemap-rule",    // 名称生成规则版本
        "base-world-rule", // 基座—世界变换规则版本（单点常量）
        "codec",           // 编码器版本三元组
        "wc-options",      // 编译选项 DWC 无关子集
    };
    for (std::size_t i = 0; i < expected.size(); ++i) {
        EXPECT_EQ(key.components[i].component, expected[i])
            << "分量[" << i << "] 应为 " << expected[i]
            << "（策略/种子/线程/HEAD 无条目——键纯内容，CON-06/CON-05）";
        EXPECT_TRUE(key.components[i].digest.isValid()) << "分量摘要应非零";
    }
}

// =====================================================================
// RT-CACHE-2：cached 物性缺失（DWC 键 nullopt）、requested 要求 DWC→
// WorkCellOnlyReuse（非 FullReuse）——"WorkCell 可复用而 DynamicWorkCell
// 不可"的显式表达；调用方处置＝复用 WC 层产物、重编 DWC。
// =====================================================================

TEST(CacheKeyTest, WorkCellOnlyReuseNotFullReuse_RT_CACHE_2)
{
    const CanonicalModel model = richFixture().build();
    const RuntimeCompileCacheKeyBuilder builder = makeBuilder();

    // 缓存侧：一次 DWC 被跳过的编译（物性缺失跳过或未请求——execution
    // 按快照事实落账 nullopt，§15.4 v0.11② 落账规则）在此以"未请求 DWC"
    // 的键面表示（两成因的 WC 键相同——D-04，见 BuildsTwoLayerKeys 用例）。
    CompileOptions noDwc;
    noDwc.requestDynamicWorkCell = false;
    const CompileCacheKey cached = builder.buildKey(model, noDwc);

    // 请求侧：本次要求 DWC（默认选项）。
    const CompileCacheKey requested = builder.buildKey(model, CompileOptions{});

    // 前提：WC 键相等（WC 层产物可复用的键面）＋DWC 缺性差异。
    EXPECT_TRUE(requested.workCellKey == cached.workCellKey) << "WC 键应相等（RT-CACHE-2 前提）";
    ASSERT_TRUE(requested.dynamicWorkCellKey.has_value()) << "请求侧要求 DWC";
    ASSERT_FALSE(cached.dynamicWorkCellKey.has_value()) << "缓存侧 DWC 缺失（SkippedNoPhysics）";

    // 判定：WorkCellOnlyReuse——显式非 FullReuse（"不得作为完整命中上报"，
    // CON-04/acceptance 3 的判定表反例）。
    const CompileCacheCompatibility r = judgeCompileCacheCompatibility(requested, cached);
    EXPECT_TRUE(r.verdict == CompileCacheCompatibility::Verdict::WorkCellOnlyReuse)
        << "应判 WorkCellOnlyReuse，实得 " << verdictName(r.verdict);
    EXPECT_TRUE(r.verdict != CompileCacheCompatibility::Verdict::FullReuse)
        << "WC 复用 DWC 不可不得作为完整命中上报（CON-04）";
    expectVerdict("RT-CACHE-2", r, CompileCacheCompatibility::Verdict::WorkCellOnlyReuse,
                  {"dwc-missing-in-cached"});
}

// =====================================================================
// RT-CACHE-3：旧编译器契约版本拒绝——Incompatible＋contract-changed；
// 不存在"版本接近可凑用"的语义等价复用（§4.3.6 保守方向）。
// =====================================================================

TEST(CacheKeyTest, OldCompilerContractRejected_RT_CACHE_3)
{
    const CanonicalModel model = richFixture().build();
    const CompileOptions opts;

    // 缓存侧＝契约版本 1（当前交付值）；请求侧＝契约版本 2（升级后的
    // 编译器申报值——仅差 1 个版本号也必须拒绝）。
    const CompileCacheKey cached
        = RuntimeCompileCacheKeyBuilder(1u, std::string{"impl-cache-1.0"})
              .buildKey(model, opts);
    const CompileCacheKey requested
        = RuntimeCompileCacheKeyBuilder(2u, std::string{"impl-cache-1.0"})
              .buildKey(model, opts);

    const CompileCacheCompatibility r = judgeCompileCacheCompatibility(requested, cached);
    expectVerdict("RT-CACHE-3", r, CompileCacheCompatibility::Verdict::Incompatible,
                  {"contract-changed"});
    // 旧编译器读新缓存同理被拒（判定对分量差异本身负责——方向无关）。
    const CompileCacheCompatibility reverse = judgeCompileCacheCompatibility(cached, requested);
    expectVerdict("RT-CACHE-3（反向）", reverse,
                  CompileCacheCompatibility::Verdict::Incompatible, {"contract-changed"});
}

// =====================================================================
// RT-CACHE-4：命中≠当前性——"电机成本对象变化"场景（RobotDesign 未变）
// →重建请求键→FullReuse（运行时缓存不失效）；当前性变化归 evidence
// computeCurrentness（CON-05）——本测试只断言 runtime 侧行为（单元卡
// RT-CACHE-4 行口径）。AT-05"电机成本变更不失效运动学"在 runtime 侧的
// 体现＝RobotDesign 对象未变→modelIdentity 不变→FullReuse（§9.4 判定表
// "缓存命中 vs 结果当前性"行）。
// =====================================================================

TEST(CacheKeyTest, HeadAdvanceDoesNotInvalidate_RT_CACHE_4)
{
    const RuntimeCompileCacheKeyBuilder builder = makeBuilder();
    const CompileOptions opts;

    // 缓存侧：HEAD 处于修订 R 时登记的编译产物键。
    const CanonicalModel modelAtHeadR = richFixture().build();
    const CompileCacheKey cached = builder.buildKey(modelAtHeadR, opts);

    // 场景推进：HEAD 前进到修订 R+1——项目里"电机成本"对象内容变化；
    // 该对象不经编译链消费（S2 解析 RobotDesign、成本归评估参数面），
    // 被消费闭包内容未变→重编译产出的 CanonicalModel 与缓存侧字节等值。
    // 测试承载：同夹具独立二次 build（两次构造之间无共享状态——等值即
    // "消费面未变"的模型层事实，RT-ID-2 位模式等值口径）。
    const CanonicalModel modelAtHeadR1 = richFixture().build();
    EXPECT_TRUE(modelAtHeadR1 == modelAtHeadR)
        << "消费面未变→模型字节等值（RT-ID-2）——场景承载前提";
    ASSERT_TRUE(modelAtHeadR1.contentIdentity() == modelAtHeadR.contentIdentity())
        << "内容未变→modelIdentity 不变（键纯内容的直接推论）";

    // 重建请求键（execution 缓存查询前的例行生成——§10.0 buildKey 行）
    // →判定 FullReuse：HEAD 前进本身不使编译缓存失效；键路径无任何
    // HEAD/时钟/会话输入（judge 的输入只有两个键——纯函数）。
    const CompileCacheKey requested = builder.buildKey(modelAtHeadR1, opts);
    expectKeysEqual("HEAD 前进而消费面未变→请求键应与缓存键全等", requested, cached);
    expectVerdict("RT-CACHE-4", judgeCompileCacheCompatibility(requested, cached),
                  CompileCacheCompatibility::Verdict::FullReuse, {});

    // 文档断言（对照面）：只有被消费对象内容变化→modelIdentity 变→新键
    // ——质量值扰动（被消费内容）即 Incompatible（详细扰动矩阵见
    // ComponentPerturbationPreciseReasons_RT_CACHE_1）。"当前性"判断（该键
    // 相对当前输入是否仍适用）不在本判定职责内——归 evidence
    // computeCurrentness（CON-05/SA-07 键链一致性）。
}

// =====================================================================
// acceptance 3：部分/失败产物不得作为完整命中——未 finalize 键
// （workCellKey 全零）与伪造部分键（在场 DWC 键全零）恒 Incompatible
// （§9.4 判定表"部分模型/失败产物"行：Failed/Cancelled 产物不进入缓存；
// 本判定接口对"未 finalize/身份为空"输入直接 Incompatible）。
// =====================================================================

TEST(CacheKeyTest, UnfinalizedKeyNeverHits_RT_CACHE_1)
{
    const CanonicalModel model = richFixture().build();
    const CompileCacheKey valid = makeBuilder().buildKey(model, CompileOptions{});
    ASSERT_TRUE(valid.workCellKey.isValid()) << "前置：合法键非零";

    // 未 finalize 键＝workCellKey 全零（buildKey 对无效模型的防御性产出——
    // CanonicalModel 公共构造路径恒产出非零身份，故该键面只能来自
    // "未 finalize"通道；此处以默认构造直接表示该判定面输入）。
    const CompileCacheKey unfinalized;

    // 双向都拒绝：部分/失败产物不存在作为完整命中的路径（CON-04）——
    // 判定门先于一切分量比对（key-invalid 词）。
    expectVerdict("未 finalize 键（请求侧）",
                  judgeCompileCacheCompatibility(unfinalized, valid),
                  CompileCacheCompatibility::Verdict::Incompatible, {"key-invalid"});
    expectVerdict("未 finalize 键（缓存侧）",
                  judgeCompileCacheCompatibility(valid, unfinalized),
                  CompileCacheCompatibility::Verdict::Incompatible, {"key-invalid"});

    // 伪造部分键：WC 键合法但"在场 DWC 键"全零（半成品登记的防御面）——
    // 同样恒 Incompatible，不给部分产物留任何命中通道。
    CompileCacheKey forged;
    forged.workCellKey = valid.workCellKey;
    forged.dynamicWorkCellKey = core::ContentIdentity{};  // 全零保留值
    expectVerdict("伪造部分键", judgeCompileCacheCompatibility(valid, forged),
                  CompileCacheCompatibility::Verdict::Incompatible, {"key-invalid"});
}
