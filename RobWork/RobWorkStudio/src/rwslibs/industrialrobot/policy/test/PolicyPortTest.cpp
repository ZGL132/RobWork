/**
 * @file   PolicyPortTest.cpp
 * @brief  ④策略端口用例组（POL-T05）——注入接口契约（Contexts.hpp）、
 *         端口契约（§9.1 错误矩阵/装配契约）与记忆化缓存一致性（POL-ID-1）。
 *
 * 设计依据：
 *   - units/policy.md §9.1（IPolicyProvider 契约表——本套件断言的逐行权威）、
 *     §3.3/§9.7（注入接口契约——CR-04 映射与并发只读行）、§12 POL-T05 行
 *     （完成条件＝注入接口与端口契约用例通过；POL-ID-1 缓存一致性）、§11
 *     （POL-ID-1 端口级观测点：cid 相等、重复解析逐字段相等）
 *   - traceability/foundation-api-diff.md CR-04（IPolicyNameContext↔
 *     IRuntimeNameResolver：Expected 错误→nullopt——不可解析不猜测）
 *   - 需求 ARC-05（策略单一权威）、CON-05/06（内容编址——缓存键确定性）、
 *     NFR-COR-02（并发只读一致）、UX-08（显示单位等非语义变化不进端口结果）
 *   - 任务契约 tasks/foundation/POL-T05.json acceptance 1～3：
 *     ①注入接口与端口契约用例（套件 PolicyPortContexts/PolicyPort——用例名
 *     带 _PortContract/_CR_04）；②缓存一致 POL-ID-1（套件 PolicyPortCache
 *     ——用例名带 _POL_ID_1）；③CR-04/P-POL-9 处置约束＝零 runtime/project
 *     编译依赖（BuildRedLineTest NoCrossUnitInclude 对本头自动扫描——白名单
 *     {core, policy}；Contexts.hpp 最小接口面由头文件契约约束＋评审核对）
 *
 * 用例追溯命名（DTB §5.5）：用例名尾部带需求/用例组编号，正文断言处注明
 * 验证的条款。替身说明（POL-TD-1 精神——本套件内的受控替身只替代"对象
 * 字节存储/修订闭包查询/名称映射/取消存活"四个注入面，不冒充碰撞算法/
 * 场景语义；评估器半区在 POL-T06 前无完整类型，本套件只断言装配守卫的
 * fail-fast 面——正向实例同一性断言归 POL-SHARE-1，POL-T11 套件）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/core/DiagData.hpp>
#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/policy/Contexts.hpp>
#include <sdurws/ird/policy/Errors.hpp>
#include <sdurws/ird/policy/PolicyInput.hpp>
#include <sdurws/ird/policy/PolicyParsing.hpp>
#include <sdurws/ird/policy/PolicyPort.hpp>
#include <sdurws/ird/policy/PolicySet.hpp>

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace sdurws::ird::policy;

/// core 契约类型的短别名（测试可读性）。
namespace core = sdurws::ird::core;

namespace {

// =====================================================================
// 受控替身（test-local，POL-T11 套件的完整替身库之外的轻量实现）。
// =====================================================================

/// double π 字面量（与 PolicySet.hpp/PolicyParsingTest 同源——期望值不另
/// 引入第二常量源）。
constexpr double kPi = 3.141592653589793;

/// 手工构造的有效对象身份（首字节打标——非零有效且字节字典序可控）。
core::ObjectId taggedObjectId(std::uint8_t tag)
{
    core::ObjectId id;
    id.bytes[0] = tag;
    return id;
}

/// 手工构造的有效内容版本（首字节打标——非全零即 isValid；端口不校验
/// 版本↔字节绑定，CR-02：对象字节摘要归 project 编址，policy 不计算）。
core::ContentVersion taggedContentVersion(std::uint8_t tag)
{
    core::ContentVersion cv;
    cv.bytes[0] = tag;
    return cv;
}

/// 手工构造的非全零内容身份（名称映射替身的内容身份——CON-06）。
core::ContentIdentity taggedContentIdentity(std::uint8_t tag)
{
    core::ContentIdentity cid;
    cid.bytes[0] = tag;
    return cid;
}

/**
 * @brief 可编程对象字节存储替身（IPolicyBytesSource 测试实现——只替代
 *        "取数"注入面；应答即查表，无任何猜测/回退逻辑）。
 *
 * 线程安全：并发只读测试以 const 查表访问（构建后不再修改）——满足
 * §9.7"并发只读"实现契约。
 */
class TestBytesSource final : public IPolicyBytesSource {
public:
    /// 存储表：键＝(对象身份, 内容版本)，值＝对象字节（encode 产物）。
    std::map<std::pair<core::ObjectId, core::ContentVersion>, std::vector<std::uint8_t>> store;

    std::optional<std::vector<std::uint8_t>>
    tryObjectBytes(core::ObjectId object, core::ContentVersion version) const override
    {
        const auto it = store.find({object, version});
        if (it == store.end()) {
            return std::nullopt;   // 存储侧缺失——④端口错误矩阵第 3 项的应答源
        }
        return it->second;
    }
};

/**
 * @brief 可编程修订闭包替身（IPolicyValidationContext 测试实现——与
 *        PolicyParsingTest 同款三张应答表；单线程用例使用）。
 */
class TestValidationContext final : public IPolicyValidationContext {
public:
    std::map<core::ObjectId, bool> existingObjects;    ///< objectExists 应答表
    std::map<core::ObjectId, std::string> roles;       ///< objectRole 应答表
    std::map<std::string, bool> definedGroups;         ///< groupDefined 应答表

    bool objectExists(core::ObjectId object) const override
    {
        const auto it = existingObjects.find(object);
        return it != existingObjects.end() && it->second;
    }

    std::optional<std::string> objectRole(core::ObjectId object) const override
    {
        const auto it = roles.find(object);
        return it == roles.end() ? std::nullopt : std::optional<std::string>(it->second);
    }

    bool groupDefined(std::string_view groupName) const override
    {
        const auto it = definedGroups.find(std::string{groupName});
        return it != definedGroups.end() && it->second;
    }
};

/**
 * @brief 名称映射替身（IPolicyNameContext 测试实现——CR-04 适配映射的
 *        契约演示：内部表模拟 runtime IRuntimeNameResolver 的应答，Expected
 *        错误→nullopt，不做任何前缀拼拆——R-4 红线）。
 */
class TestNameContext final : public IPolicyNameContext {
public:
    std::map<std::string, core::ObjectId> byName;      ///< 运行时名→对象
    std::map<core::ObjectId, std::string> byId;        ///< 对象→运行时名
    core::ContentIdentity mapIdentity;                 ///< nameMapContentIdentity 应答

    std::optional<core::ObjectId> tryObjectId(const std::string& runtimeName) const override
    {
        // CR-04：映射中无该名称＝Expected 错误→nullopt（不可解析不猜测——
        // ARC-04；适配器只转发，不编造身份）。
        const auto it = byName.find(runtimeName);
        return it == byName.end() ? std::nullopt : std::optional<core::ObjectId>(it->second);
    }

    std::optional<std::string> tryRuntimeName(core::ObjectId object) const override
    {
        const auto it = byId.find(object);
        return it == byId.end() ? std::nullopt : std::optional<std::string>(it->second);
    }

    core::ContentIdentity nameMapContentIdentity() const override { return mapIdentity; }
};

/**
 * @brief 取消/存活标志替身（IPolicyCallContext 测试实现——宿主状态的
 *        只读投影；消费方＝评估实现（POL-T07 evaluate），本套件钉住其
 *        可实现性与标志转发语义）。
 */
class TestCallContext final : public IPolicyCallContext {
public:
    bool cancelFlag = false;   ///< 宿主取消标志（cancellationRequested 应答）
    bool aliveFlag = true;     ///< 宿主存活事实（alive 应答）

    bool cancellationRequested() const override { return cancelFlag; }
    bool alive() const override { return aliveFlag; }
};

// =====================================================================
// 夹具装配辅助（与 PolicyParsingTest baseInput 同源——单点变异保证诊断
// 可归因）。
// =====================================================================

/// 标准闭包对象（语义见 PolicyParsingTest standardContext——同款装配）。
core::ObjectId objA() { return taggedObjectId(0x01); }
core::ObjectId objB() { return taggedObjectId(0x02); }
core::ObjectId toolObj() { return taggedObjectId(0x03); }
core::ObjectId workpieceObj() { return taggedObjectId(0x04); }

/// 组装"全部校验通过"的基线输入（解析管线确认可发布的形态）。
RawPolicyInput baseInput()
{
    RawPolicyInput in;
    in.schemaVersion = PolicySchema::currentVersion;
    in.policyObject = taggedObjectId(0x10);   // 策略对象自身身份（非规则对象）

    // 碰撞规则：启用三域＋安全间距 0.02 m＋一对必检＋一对排除（互不覆盖）。
    in.collision.enabled = true;
    in.collision.enabledDomains = {CollisionDomain::Self, CollisionDomain::Environment,
                                   CollisionDomain::Tool};
    in.collision.safetyClearance = RawThresholdInput{0.02, "m"};
    in.collision.excludeAdjacentLinksByDefault = true;
    in.collision.mandatoryPairs = {PairRule::make(ScopeTarget::makeObject(objA()),
                                                  ScopeTarget::makeObject(objB()),
                                                  PolicyRuleLevel::Must, "结构干涉必检")};
    in.collision.excludedPairs = {PairRule::make(ScopeTarget::makeObject(toolObj()),
                                                 ScopeTarget::makeObject(workpieceObj()),
                                                 PolicyRuleLevel::Should, "夹持接触豁免")};

    // 阈值：全部显式提供（无 Info 级默认告知——基线判定纯净）。
    in.jointThresholds.nearLimitRatio = RawThresholdInput{0.05, ""};
    in.jointThresholds.conditionNumberWarning = RawThresholdInput{10.0, ""};
    in.jointThresholds.finiteRotationTravelLimit = RawThresholdInput{4.0 * kPi, "rad"};
    in.jointThresholds.travelLimitCheckEnabled = true;

    // 适用范围：限定两模式＋一个存在的模型对象。
    in.applicability.modes = {core::EvaluationMode::Quick, core::EvaluationMode::Verified};
    in.applicability.modelObjects = {objA()};

    in.origin.kind = PolicyOriginKind::UserEdited;
    return in;
}

/// 组装标准闭包应答表（baseInput 规则/范围对象的完整存在性＋角色）。
TestValidationContext standardContext()
{
    TestValidationContext ctx;
    for (const core::ObjectId id : {objA(), objB(), toolObj(), workpieceObj()}) {
        ctx.existingObjects[id] = true;
    }
    ctx.roles = {{toolObj(), "Tool"}, {workpieceObj(), "Workpiece"}};
    ctx.definedGroups = {{"jigs", true}};
    return ctx;
}

/**
 * @brief 装配"标准可用"端口：bytesSource 内放 baseInput 的编码字节，键＝
 *        (策略对象身份, 版本 0xA0)；validationContext＝标准闭包。
 */
struct StandardAssembly {
    TestBytesSource bytes;
    TestValidationContext context;
    std::unique_ptr<PolicyProvider> provider;

    StandardAssembly()
    {
        context = standardContext();
        const RawPolicyInput in = baseInput();
        bytes.store[{in.policyObject, taggedContentVersion(0xA0)}] = PolicyCodec::encode(in);
        provider = std::make_unique<PolicyProvider>(bytes, context, nullptr,
                                                    backendDescriptor());
    }

    /// 请求基线策略对象的便捷入口（键与构造时的存储一致）。
    PolicyResolutionRequest baseRequest() const
    {
        return PolicyResolutionRequest{baseInput().policyObject,
                                       taggedContentVersion(0xA0)};
    }

    /// 后端描述符（字段值任意——端口只做值转发，本套件钉住转发与相等语义）。
    static CollisionBackendDescriptor backendDescriptor()
    {
        return CollisionBackendDescriptor{"rw.proximity.builtin-rw", "test-baseline",
                                          "unit-test-resolution"};
    }
};

/// 取诊断码串是否存在（断言可读性辅助，与 PolicyParsingTest hasCode 同款）。
bool hasCode(const std::vector<core::DiagnosticRecord>& diags, const std::string& code)
{
    for (const auto& d : diags) {
        if (d.code == code) {
            return true;
        }
    }
    return false;
}

/// §9.6 建议码常量（与 Errors.cpp registryCode 同源串面）。
const std::string kCodeObjectMissing = "POLICY-OBJECT-MISSING";
const std::string kCodeVersionFuture = "POLICY-SCHEMA-VERSION-FUTURE";
const std::string kCodeEncodingInvalid = "POLICY-ENCODING-INVALID";
const std::string kCodeNonPositive = "POLICY-THRESHOLD-NON-POSITIVE";

}  // namespace

// =====================================================================
// 注入接口契约（acceptance 1——Contexts.hpp 三接口的可实现性与语义钉住；
// bytesSource/validationContext 的消费面贯穿 PolicyPort 套件，此处钉住
// nameContext（CR-04 映射）与 callContext（宿主标志投影））。
// =====================================================================

/**
 * CR-04 适配映射契约：名称上下文对"映射中无此名称/对象"返回 nullopt
 * （runtime IRuntimeNameResolver 的 Expected 错误→nullopt，不可解析不
 * 猜测——ARC-04）；解析幂等（同输入重复查询同应答——§9.7 后置行）；
 * 映射内容身份原样转发（CON-06）。正向映射（名称→对象）与反向映射
 * （对象→名称）双向钉住。
 */
TEST(PolicyPortContexts, NameContextExpectedErrorYieldsNulloptIdempotent_CR_04)
{
    TestNameContext names;
    names.mapIdentity = taggedContentIdentity(0x5A);
    names.byName["Robot/Link1"] = objA();
    names.byId[objB()] = "Tool/GraspFrame";

    // 正向：映射内名称→对象身份；映射外名称→nullopt（不猜测）。
    EXPECT_EQ(names.tryObjectId("Robot/Link1"), std::optional<core::ObjectId>(objA()));
    EXPECT_EQ(names.tryObjectId("NoSuch/Frame"), std::nullopt);

    // 反向：对象→运行时名；映射外对象→nullopt。
    EXPECT_EQ(names.tryRuntimeName(objB()), std::optional<std::string>("Tool/GraspFrame"));
    EXPECT_EQ(names.tryRuntimeName(taggedObjectId(0x7F)), std::nullopt);

    // 幂等：重复查询同应答（§9.7"解析只读、幂等"行）。
    EXPECT_EQ(names.tryObjectId("Robot/Link1"), names.tryObjectId("Robot/Link1"));
    EXPECT_EQ(names.tryObjectId("NoSuch/Frame"), names.tryObjectId("NoSuch/Frame"));

    // 内容身份转发（CON-06——映射身份随会话输出）。
    EXPECT_EQ(names.nameMapContentIdentity(), taggedContentIdentity(0x5A));
}

/**
 * 调用上下文契约：cancellationRequested()/alive() 反映宿主状态（§9.7 表
 * IPolicyCallContext 行）——未取消/存活时双 false/true，标志置位后查询
 * 如实投影（消费方＝POL-T07 evaluate 的样本边界协作检查）。
 */
TEST(PolicyPortContexts, CallContextReflectsHostState)
{
    TestCallContext ctx;
    EXPECT_FALSE(ctx.cancellationRequested());
    EXPECT_TRUE(ctx.alive());

    ctx.cancelFlag = true;
    ctx.aliveFlag = false;
    EXPECT_TRUE(ctx.cancellationRequested());
    EXPECT_FALSE(ctx.alive());
}

// =====================================================================
// 端口契约（acceptance 1——§9.1 契约表逐行；错误矩阵六项落点）。
// =====================================================================

/** §9.1 后置条件（成功路径）：解析发布策略——与解析管线直连结果逐字段
 *  相等（端口是管线的忠实转发，无第二口径），内容身份＝codec 重算。 */
TEST(PolicyPort, ResolvePublishesPipelineEqualPolicy_PortContract)
{
    StandardAssembly asm_;
    const PolicyResolution res = asm_.provider->resolvePolicy(asm_.baseRequest());

    // 发布成功：policy 非空，且与管线直调结果逐字段相等（ARC-05 单一权威
    // ——端口不引入第二解析口径）。
    ASSERT_TRUE(res.policy.has_value());
    const RawPolicyInput in = baseInput();
    const TestValidationContext ctx = standardContext();
    const PolicyParseResult direct = resolvePolicy(in, ctx);
    ASSERT_TRUE(direct.policy.has_value());
    EXPECT_EQ(*res.policy, *direct.policy);

    // Info 级诊断随发布对象附带（本基线无默认填充——诊断为空集亦合法，
    // 与管线 validationDiagnostics 一致）。
    EXPECT_EQ(res.diagnostics, direct.diagnostics);
}

/** §9.1 错误类型行第 3 项：存储侧对象缺失→空 policy＋POLICY-OBJECT-
 *  MISSING（subject 绑定请求对象），不抛（环境事实走诊断轨）。 */
TEST(PolicyPort, MissingObjectBytesYieldsDiagnosticNoThrow_PortContract)
{
    StandardAssembly asm_;
    // 版本在存储侧不存在（未放字节）——对象存在性与版本闭包性对端口不可
    // 区分（CR-03 应答面只有 nullopt），统一 POLICY-OBJECT-MISSING。
    const PolicyResolutionRequest req{taggedObjectId(0x33), taggedContentVersion(0xA1)};

    const PolicyResolution res = asm_.provider->resolvePolicy(req);   // 不抛
    EXPECT_FALSE(res.policy.has_value());
    ASSERT_EQ(res.diagnostics.size(), 1u);
    EXPECT_EQ(res.diagnostics[0].code, kCodeObjectMissing);
    // subject 绑定请求对象（ERR-01 稳定诊断绑定对象）。
    ASSERT_TRUE(res.diagnostics[0].subject.has_value());
    EXPECT_EQ(*res.diagnostics[0].subject, req.policyObject);
    // cause 携带定位要素（对象/版本规范文本——诊断可定位）。
    EXPECT_NE(res.diagnostics[0].cause.find(req.policyObject.toCanonical()),
              std::string::npos);
    EXPECT_NE(res.diagnostics[0].cause.find(req.expectedVersion->toCanonical()),
              std::string::npos);
}

/** §9.1 前置/错误类型行：expectedVersion 未携带（nullopt）＝不可编址取数
 *  （CON-05 内容编址是端口唯一服务形态）→空 policy＋诊断，不抛。 */
TEST(PolicyPort, AbsentExpectedVersionYieldsDiagnosticNoThrow_PortContract)
{
    StandardAssembly asm_;
    const PolicyResolutionRequest req{taggedObjectId(0x33), std::nullopt};

    const PolicyResolution res = asm_.provider->resolvePolicy(req);   // 不抛
    EXPECT_FALSE(res.policy.has_value());
    ASSERT_EQ(res.diagnostics.size(), 1u);
    EXPECT_EQ(res.diagnostics[0].code, kCodeObjectMissing);
    // cause 说明内容编址要求与取得版本的正确入口（§10.1 流程）。
    EXPECT_NE(res.diagnostics[0].cause.find("CON-05"), std::string::npos);
    EXPECT_NE(res.diagnostics[0].cause.find("project ②端口"), std::string::npos);
}

/** §9.1 错误类型行第 1 项：调用方契约违约（全零对象身份/全零内容版本）
 *  fail-fast 抛 PolicyError(PolicyObjectInvalid)——合法编址请求不可产生，
 *  静默转诊断会掩盖调用侧 bug（与解析管线复检同款纪律）。 */
TEST(PolicyPort, CallerContractViolationsFailFast_PortContract)
{
    StandardAssembly asm_;

    // 全零对象身份（保留值纪律——Identity.hpp）。
    PolicyResolutionRequest zeroOid{core::ObjectId{}, taggedContentVersion(0xA0)};
    EXPECT_THROW(asm_.provider->resolvePolicy(zeroOid), PolicyError);

    // 全零内容版本（保留值——无编址意义）。
    PolicyResolutionRequest zeroCv{taggedObjectId(0x33), core::ContentVersion{}};
    EXPECT_THROW(asm_.provider->resolvePolicy(zeroCv), PolicyError);

    // 异常码面钉住：PolicyObjectInvalid（表内既有的发布对象族码——端口
    // 编址违约与其同族）。
    try {
        asm_.provider->resolvePolicy(zeroOid);
        FAIL() << "应抛 PolicyError";
    }
    catch (const PolicyError& e) {
        EXPECT_EQ(e.code(), PolicyErrorCode::PolicyObjectInvalid);
    }
}

/** §9.1 错误类型行第 4 项：字节损坏（decode 抛 EncodingInvalid）→转译为
 *  同码面诊断（cause 携带异常全文），空 policy，不抛。 */
TEST(PolicyPort, CorruptBytesForwardedAsDiagnosticNoThrow_PortContract)
{
    StandardAssembly asm_;
    // 存入被截断的字节（载荷长度与实际不符——decode 的 EncodingInvalid 面）。
    std::vector<std::uint8_t> truncated = PolicyCodec::encode(baseInput());
    truncated.pop_back();
    const PolicyResolutionRequest req{taggedObjectId(0x10), taggedContentVersion(0xA2)};
    asm_.bytes.store[{req.policyObject, *req.expectedVersion}] = truncated;

    const PolicyResolution res = asm_.provider->resolvePolicy(req);   // 不抛
    EXPECT_FALSE(res.policy.has_value());
    ASSERT_FALSE(res.diagnostics.empty());
    EXPECT_EQ(res.diagnostics[0].code, kCodeEncodingInvalid);
    // 原文保留（NFR-COR-03 不吞错——what() 全文进 cause）。
    EXPECT_NE(res.diagnostics[0].cause.find("policy/encoding-invalid"), std::string::npos);
}

/** §9.1 错误类型行"POLICY-SCHEMA-* 转发"：字节层未来版本（帧内
 *  schemaVersion=当前代+1）→空 policy＋POLICY-SCHEMA-VERSION-FUTURE，
 *  不抛、不前向猜测解析（与解析管线版本门处置语义一致——POL-PARSE-1
 *  的端口级延续）。 */
TEST(PolicyPort, FutureSchemaBytesForwardSchemaDiagnosticNoThrow_PortContract)
{
    StandardAssembly asm_;
    // 手工改帧内 schemaVersion 字节（帧头布局：magic 7B＋形态 1B＋版本 4B
    // 大端＋长度 4B——PolicyInput.cpp kHeaderSize=16）。
    std::vector<std::uint8_t> future = PolicyCodec::encode(baseInput());
    future[11] = 0x02;   // 大端最低位字节：当前代 1 → 2
    const PolicyResolutionRequest req{taggedObjectId(0x10), taggedContentVersion(0xA3)};
    asm_.bytes.store[{req.policyObject, *req.expectedVersion}] = future;

    const PolicyResolution res = asm_.provider->resolvePolicy(req);   // 不抛
    EXPECT_FALSE(res.policy.has_value());
    ASSERT_FALSE(res.diagnostics.empty());
    EXPECT_EQ(res.diagnostics[0].code, kCodeVersionFuture);
}

/** §9.1 错误类型行第 5 项：策略内容非法（解码成功、解析管线产出 Error 级
 *  诊断）→空 policy＋管线全量诊断原样转发（不短路——POL-T04 管线契约在
 *  端口的延续），不抛。 */
TEST(PolicyPort, InvalidPolicyContentForwardsPipelineDiagnosticsNoThrow_PortContract)
{
    StandardAssembly asm_;
    // 内容变异：安全间距改负值（解析②域窗拒绝——ThresholdNonPositive）。
    RawPolicyInput bad = baseInput();
    bad.collision.safetyClearance = RawThresholdInput{-0.5, "m"};
    const PolicyResolutionRequest req{bad.policyObject, taggedContentVersion(0xA4)};
    asm_.bytes.store[{req.policyObject, *req.expectedVersion}] = PolicyCodec::encode(bad);

    const PolicyResolution res = asm_.provider->resolvePolicy(req);   // 不抛
    EXPECT_FALSE(res.policy.has_value());
    EXPECT_FALSE(res.diagnostics.empty());
    EXPECT_TRUE(hasCode(res.diagnostics, kCodeNonPositive));
    // 端口转发＝管线直调的全量诊断（不增不减——单一口径）。
    const TestValidationContext ctx = standardContext();
    const PolicyParseResult direct = resolvePolicy(bad, ctx);
    EXPECT_EQ(res.diagnostics, direct.diagnostics);
}

/** §9.1 collisionBackend 行：复现要素按装配期注入值返回（值语义；
 *  CollisionBackendDescriptor 相等/不等算子逐字段精确）。 */
TEST(PolicyPort, CollisionBackendReturnsInjectedDescriptor_PortContract)
{
    StandardAssembly asm_;
    const CollisionBackendDescriptor backend = asm_.provider->collisionBackend();
    EXPECT_EQ(backend, StandardAssembly::backendDescriptor());

    // 逐字段精确相等语义：任一字段不同即不等（版本串差异即复现要素差异）。
    CollisionBackendDescriptor other = StandardAssembly::backendDescriptor();
    other.backendVersion = "other-baseline";
    EXPECT_NE(backend, other);
}

/** §9.1 前置条件行的 fail-fast 载体：评估器半区未注入即调用
 *  collisionEvaluator()＝装配契约违约→PolicyError(PortAssemblyIncomplete)。
 *  正向"同一实例引用"断言依赖 POL-T06 完整类型，归 POL-SHARE-1（POL-T11）。 */
TEST(PolicyPort, CollisionEvaluatorBeforeAssemblyFailsFast_PortContract)
{
    StandardAssembly asm_;   // 构造时 evaluator=nullptr（装配分步的合法中间态）
    try {
        asm_.provider->collisionEvaluator();
        FAIL() << "应抛 PolicyError(PortAssemblyIncomplete)";
    }
    catch (const PolicyError& e) {
        EXPECT_EQ(e.code(), PolicyErrorCode::PortAssemblyIncomplete);
        EXPECT_NE(std::string{e.what()}.find("policy/port-assembly-incomplete"),
                  std::string::npos);
    }
}

/** §9.1 线程行：全部方法并发只读安全（缓存内部同步）——多线程并发解析
 *  （命中/未命中/失败/缺版本混合负载），各键结果与单线程基准逐字段一致
 *  （NFR-COR-02；解析纯函数＋互斥量保护 memo 表）。 */
TEST(PolicyPort, ConcurrentResolveReadOnlySafety_PortContract_NFR_COR_02)
{
    StandardAssembly asm_;
    const PolicyResolutionRequest base = asm_.baseRequest();
    const PolicyResolutionRequest missing{taggedObjectId(0x33), taggedContentVersion(0xA5)};
    const PolicyResolutionRequest noVersion{taggedObjectId(0x34), std::nullopt};

    // 单线程基准（先行解析，兼作缓存预热——命中/未命中两路径都覆盖）。
    const PolicyResolution refBase = asm_.provider->resolvePolicy(base);
    const PolicyResolution refMissing = asm_.provider->resolvePolicy(missing);
    const PolicyResolution refNoVersion = asm_.provider->resolvePolicy(noVersion);

    // 结果槽位用 optional 承载（PolicyResolution 拷贝赋值被删除——
    // EngineeringPolicySet 全 const 的级联，optional::emplace 就地拷贝构造）。
    std::vector<std::optional<PolicyResolution>> gotBase(4);
    std::vector<std::optional<PolicyResolution>> gotMissing(4);
    std::vector<std::optional<PolicyResolution>> gotNoVersion(4);
    std::vector<std::thread> workers;
    for (int t = 0; t < 4; ++t) {
        workers.emplace_back([&, t]() {
            // 每线程交错发起三类请求——并发命中与并发未命中同时发生。
            gotBase[t].emplace(asm_.provider->resolvePolicy(base));
            gotMissing[t].emplace(asm_.provider->resolvePolicy(missing));
            gotNoVersion[t].emplace(asm_.provider->resolvePolicy(noVersion));
        });
    }
    for (auto& w : workers) {
        w.join();
    }

    for (int t = 0; t < 4; ++t) {
        ASSERT_TRUE(gotBase[t].has_value()) << "线程 " << t;
        ASSERT_TRUE(gotMissing[t].has_value()) << "线程 " << t;
        ASSERT_TRUE(gotNoVersion[t].has_value()) << "线程 " << t;
        EXPECT_EQ(*gotBase[t], refBase) << "线程 " << t;
        EXPECT_EQ(*gotMissing[t], refMissing) << "线程 " << t;
        EXPECT_EQ(*gotNoVersion[t], refNoVersion) << "线程 " << t;
    }
}

// =====================================================================
// 缓存一致性（acceptance 2——POL-ID-1 端口级联动）。
// =====================================================================

/** POL-ID-1（§11 观测点"cid 相等、逐字段相等"的端口级落点；§9.1 后置行）：
 *  同 (policyObject, ContentVersion) 重复解析——结果逐字段相等（policy 与
 *  diagnostics 双向），内容身份相等。 */
TEST(PolicyPortCache, RepeatedResolveReturnsFieldEqualResult_POL_ID_1)
{
    StandardAssembly asm_;
    const PolicyResolution first = asm_.provider->resolvePolicy(asm_.baseRequest());
    const PolicyResolution second = asm_.provider->resolvePolicy(asm_.baseRequest());

    ASSERT_TRUE(first.policy.has_value());
    ASSERT_TRUE(second.policy.has_value());
    // 逐字段相等（§9.1 后置行原文"逐字段相等"；operator== 逐字段精确）。
    EXPECT_EQ(first, second);
    EXPECT_EQ(second, first);
    // 内容身份必然一致（POL-ID-1——记忆化使其对任意重复次数成立）。
    EXPECT_EQ(first.policy->contentIdentity, second.policy->contentIdentity);
    // 诊断亦逐字段相等（Info 级随发布对象附带——不因重复解析而漂移）。
    EXPECT_EQ(first.diagnostics, second.diagnostics);
}

/** POL-ID-4 联动（§11"阈值变化产生新身份"的缓存键维度）：同一策略对象的
 *  不同内容版本各自独立解析——阈值 0.02→0.03 产生不同内容身份，缓存按
 *  (对象, 版本) 分离条目，互不串值。 */
TEST(PolicyPortCache, CacheKeyIncludesContentVersionSeparatesEntries_POL_ID_1)
{
    StandardAssembly asm_;
    const RawPolicyInput v1 = baseInput();
    RawPolicyInput v2 = baseInput();
    v2.collision.safetyClearance = RawThresholdInput{0.03, "m"};   // 阈值变化（POL-ID-4）
    // 同一策略对象身份、两个内容版本（内容编址下阈值变即版本变——CON-05）。
    const core::ObjectId policyOid = v1.policyObject;
    const core::ContentVersion cv1 = taggedContentVersion(0xB0);
    const core::ContentVersion cv2 = taggedContentVersion(0xB1);
    asm_.bytes.store[{policyOid, cv1}] = PolicyCodec::encode(v1);
    asm_.bytes.store[{policyOid, cv2}] = PolicyCodec::encode(v2);

    const PolicyResolution res1 = asm_.provider->resolvePolicy({policyOid, cv1});
    const PolicyResolution res2 = asm_.provider->resolvePolicy({policyOid, cv2});
    // 再解析一次验证两个条目都命中缓存且保持各自的值。
    const PolicyResolution res1Again = asm_.provider->resolvePolicy({policyOid, cv1});
    const PolicyResolution res2Again = asm_.provider->resolvePolicy({policyOid, cv2});

    ASSERT_TRUE(res1.policy.has_value());
    ASSERT_TRUE(res2.policy.has_value());
    EXPECT_EQ(res1, res1Again);
    EXPECT_EQ(res2, res2Again);
    // 内容身份不同（阈值进入语义闭包——CON-05/06；安全间距 SI 真值 0.02≠0.03）。
    EXPECT_NE(res1.policy->contentIdentity, res2.policy->contentIdentity);
    // 阈值真值分别到位（SI m——KIN-13 阈值经端口传递无损）。
    EXPECT_DOUBLE_EQ(res1.policy->collision.safetyClearance->siValue(), 0.02);
    EXPECT_DOUBLE_EQ(res2.policy->collision.safetyClearance->siValue(), 0.03);
}

/** §9.1 后置行对失败情形的延伸（头文件类注释"成功与失败都缓存"）：
 *  存储缺失键重复解析——空 policy＋诊断逐字段相等（负结果同样满足
 *  "同键重复调用逐字段相等"）。 */
TEST(PolicyPortCache, FailureOutcomeMemoizedFieldEqual_POL_ID_1)
{
    StandardAssembly asm_;
    const PolicyResolutionRequest missing{taggedObjectId(0x33), taggedContentVersion(0xA6)};
    const PolicyResolution first = asm_.provider->resolvePolicy(missing);
    const PolicyResolution second = asm_.provider->resolvePolicy(missing);

    EXPECT_FALSE(first.policy.has_value());
    EXPECT_EQ(first, second);
    ASSERT_FALSE(second.diagnostics.empty());
    EXPECT_EQ(second.diagnostics[0].code, kCodeObjectMissing);
}
