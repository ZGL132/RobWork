/**
 * @file   DependencyTest.cpp
 * @brief  依赖类型用例组（EV-DEP）——DependencyKey 语法正反例、声明闭包
 *         校验器正反例、条目语法校验器正反例、注册拒绝原语（异常轨）。
 *
 * 设计依据：
 *   - units/evidence.md §4.2.1（DependencyKey 语法原文与四示例、七类 Kind
 *     载荷行、applied/notAppliedReason 语义）、§4.2.2（entries ≥1、
 *     (kind,key) 字典序稳定存储）、§4.2.3①（闭包规则——referencedKeys ⊆
 *     已声明键 ∪ 快照事实键；D-10 条件输入未入切片注册期拒绝）
 *   - 需求 CON-05（实际消费依赖承载）、CON-06（策略/名称映射身份非空）；
 *     任务契约 tasks/foundation/EV-T02.json（acceptance 1：声明闭包校验
 *     用例（依赖类型正反例）；acceptance 3：Dependency 条目语法与闭包
 *     校验器正反例用例）
 *
 * 组名说明：§11 反例矩阵未设 EV-T02 专项组——本文件组 ID（EV-DEP）为
 * 实现侧组名，对照 §12 EV-T02 行验证方式"单元测试（token/语法/闭包
 * 校验器）"的语法/闭包部分（EV-ERR/EV-BUILD 同例）。
 *
 * 事实键说明：用例以 "policy"/"name-map"/"case-set" 作为快照事实键的
 * 代表串（§4.2.3①"快照事实键（policy/nameMap/caseSet）"）——真实角色键
 * 词表归注册方/组装方，evidence 校验器对该词表不可知（参数注入，N-5）。
 */

#include <sdurws/ird/evidence/Dependency.hpp>
#include <sdurws/ird/evidence/Errors.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

namespace {

using namespace sdurws::ird::evidence;
// 限定符别名：core 强类型（ContentIdentity 等）在用例中按限定形式书写。
namespace core = sdurws::ird::core;

/// 测试用有效内容身份（cv-/cid- 规范文本解析——core Digest.hpp 工厂）。
/// 注意：Digest.hpp 工厂要求 tag 化 64 位小写十六进制；固定测试值即可。
core::ContentIdentity cid(const char* hex64)
{
    return core::ContentIdentity::fromCanonical(std::string{"cid-"} + hex64);
}

core::ContentVersion cv(const char* hex64)
{
    return core::ContentVersion::fromCanonical(std::string{"cv-"} + hex64);
}

/// 64 个 'a' 的十六进制串（构造合法身份用——'a' 在 [0-9a-f] 内）。
const char* kHex64 = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";

/// 快照事实键代表集（§4.2.3① policy/nameMap/caseSet——词表归注册方）。
const std::vector<std::string>& factKeys()
{
    static const std::vector<std::string> keys{"policy", "name-map", "case-set"};
    return keys;
}

/// 合法声明构造器（Required 无条件——最常用形态）。
DependencyDeclaration requiredDecl(std::string key, DependencyKind kind)
{
    DependencyDeclaration d;
    d.key = std::move(key);
    d.kind = kind;
    d.requiredness = DependencyRequiredness::Required;
    d.resolutionNote = "测试声明";
    return d;
}

/// 合法条件声明构造器（Conditional＋条件引用给定键）。
DependencyDeclaration conditionalDecl(std::string key, DependencyKind kind,
                                      std::vector<std::string> refs)
{
    DependencyDeclaration d;
    d.key = std::move(key);
    d.kind = kind;
    d.requiredness = DependencyRequiredness::Conditional;
    ApplicabilityCondition c;
    c.conditionToken = "collision-enabled";
    c.referencedKeys = std::move(refs);
    d.applicability = std::move(c);
    d.resolutionNote = "测试条件声明";
    return d;
}

/// 合法 Object 条目构造器（其余字段合法默认）。
DependencyEntry objectEntry(std::string key)
{
    DependencyEntry e;
    e.key = std::move(key);
    e.kind = DependencyKind::Object;
    ObjectDependencyPayload p;
    p.objectId = core::ObjectId::generate();
    p.contentVersion = cv(kHex64);
    p.objectTypeToken = "robot-design";
    e.payload = std::move(p);
    e.applied = true;
    return e;
}

// =====================================================================
// EV-DEP-1 DependencyKey 语法正反例（§4.2.1 语法原文 [a-z][a-z0-9.-]{2,63}）
// =====================================================================

/** 语法正例：§4.2.1 四个原文示例＋边界长度（3 与 64 字符）全部合法。 */
TEST(EvidenceDependencyKeySyntax, ValidKeys_EV_DEP)
{
    // §4.2.1 原文示例（设计锚定——不是自造词）。
    EXPECT_TRUE(isValidDependencyKey("model.robot-design"));
    EXPECT_TRUE(isValidDependencyKey("task-points"));
    EXPECT_TRUE(isValidDependencyKey("collision-models"));
    EXPECT_TRUE(isValidDependencyKey("catalog.motor-capability"));
    // 边界：最短合法（首字符＋{2,63} 下限 2 个后续＝3 字符）。
    EXPECT_TRUE(isValidDependencyKey("abc"));
    // 边界：最长合法（64 字符——{2,63} 上限）。
    std::string maxKey = "a" + std::string(63, 'b');
    EXPECT_EQ(maxKey.size(), static_cast<std::size_t>(64));
    EXPECT_TRUE(isValidDependencyKey(maxKey));
    // 词表三类字符混排（点/连字符/数字均合法）。
    EXPECT_TRUE(isValidDependencyKey("a1.b2-c3"));
}

/** 语法反例：空/过短/过长/首字符/非法字符逐类拒绝。 */
TEST(EvidenceDependencyKeySyntax, InvalidKeys_EV_DEP)
{
    // 空（调用方未填）与 2 字符（低于 {2,63} 下限）。
    EXPECT_FALSE(isValidDependencyKey(""));
    EXPECT_FALSE(isValidDependencyKey("ab"));
    // 65 字符（超出上限 1 个——边界必须精确）。
    EXPECT_FALSE(isValidDependencyKey("a" + std::string(64, 'b')));
    // 首字符词表：大写/数字/连字符/点开头均拒（[a-z] 起始）。
    EXPECT_FALSE(isValidDependencyKey("Abc"));
    EXPECT_FALSE(isValidDependencyKey("1bc"));
    EXPECT_FALSE(isValidDependencyKey("-bc"));
    EXPECT_FALSE(isValidDependencyKey(".bc"));
    // 后续字符词表：下划线/空格/斜杠/NUL 拒绝（编码安全——§5.2）。
    EXPECT_FALSE(isValidDependencyKey("abc_def"));
    EXPECT_FALSE(isValidDependencyKey("ab d"));
    EXPECT_FALSE(isValidDependencyKey("ab/d"));
    EXPECT_FALSE(isValidDependencyKey(std::string("ab\0d", 4)));
}

// =====================================================================
// EV-DEP-2 声明闭包校验正例（acceptance 1 正例面）
// =====================================================================

/** 正例：Required 无条件＋Conditional 引用已声明键/快照事实键 → 零 issue。 */
TEST(EvidenceDependencyDeclarationClosure, ValidDeclarations_EV_DEP)
{
    std::vector<DependencyDeclaration> decls;
    // 无条件必需依赖（模型对象——Required 形态基线）。
    decls.push_back(requiredDecl("model.robot-design", DependencyKind::Object));
    // 条件依赖，条件决定键＝同 descriptor 已声明键（碰撞几何由模型对象与
    // 策略共同决定——§4.2.3 场景表"负载几何参与碰撞"同构）。
    decls.push_back(conditionalDecl("collision-models", DependencyKind::Object,
                                    {"model.robot-design", "policy"}));
    // 条件依赖，条件决定键＝快照事实键（name-map/case-set——注册方注入）。
    decls.push_back(conditionalDecl("task-points", DependencyKind::Object,
                                    {"case-set", "name-map"}));
    // 七类 Kind 各补一声明（kind 只是标注，声明级不校验载荷——载荷在冻结期）。
    decls.push_back(requiredDecl("ik-solver-config", DependencyKind::Configuration));
    decls.push_back(requiredDecl("policy", DependencyKind::Policy));
    decls.push_back(requiredDecl("name-map", DependencyKind::NameMap));
    decls.push_back(requiredDecl("region-samples", DependencyKind::SampleSet));
    decls.push_back(requiredDecl("kin-result", DependencyKind::UpstreamResult));
    decls.push_back(requiredDecl("compiler-contract-version", DependencyKind::Environment));

    const auto issues = validateDependencyDeclarations(decls, factKeys());
    EXPECT_TRUE(issues.empty())
        << "合法声明集应零问题（首条: "
        << (issues.empty() ? std::string{} : issues.front().message) << "）";
}

// =====================================================================
// EV-DEP-3 声明闭包校验反例（acceptance 1 反例面——逐规则一反例）
// =====================================================================

/** 反例（规则 4/D-10 核心）：条件输入既未声明也不是快照事实键 → 闭包拒绝。 */
TEST(EvidenceDependencyDeclarationClosure, MissingClosureDeclaration_EV_DEP)
{
    std::vector<DependencyDeclaration> decls;
    decls.push_back(requiredDecl("model.robot-design", DependencyKind::Object));
    // 条件引用了"policy"与"task-points"——但两者都不在声明集与事实键内
    // （事实键集为空模拟漏登记）："漏声明导致错误复用"的系统性防线必须
    // 在注册期拦截（§4.2.3① 原文）。
    decls.push_back(conditionalDecl("collision-models", DependencyKind::Object,
                                    {"policy", "task-points"}));
    const auto issues = validateDependencyDeclarations(decls, {});
    ASSERT_EQ(issues.size(), static_cast<std::size_t>(2)) << "两个引用键都应报闭包违约";
    EXPECT_EQ(issues[0].code, DependencyIssueCode::ReferencedKeyNotInClosure);
    EXPECT_EQ(issues[0].key, "policy");
    EXPECT_EQ(issues[0].index, static_cast<std::size_t>(1));
    EXPECT_EQ(issues[1].code, DependencyIssueCode::ReferencedKeyNotInClosure);
    EXPECT_EQ(issues[1].key, "task-points");
}

/** 反例（规则 2）：Conditional 缺条件、Required 带条件——必需性配对矛盾。 */
TEST(EvidenceDependencyDeclarationClosure, RequirednessPairingViolations_EV_DEP)
{
    // Conditional 未携带适用条件（条件语义无处承载——D-10 违约）。
    DependencyDeclaration missingCond;
    missingCond.key = "collision-models";
    missingCond.kind = DependencyKind::Object;
    missingCond.requiredness = DependencyRequiredness::Conditional;
    missingCond.resolutionNote = "漏填条件";
    const auto issues1 = validateDependencyDeclarations({missingCond}, factKeys());
    ASSERT_EQ(issues1.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(issues1[0].code, DependencyIssueCode::ConditionalMissingCondition);
    EXPECT_EQ(issues1[0].index, static_cast<std::size_t>(0));

    // Required 携带适用条件（必需性与条件互斥——语义不可判定）。
    DependencyDeclaration extraCond = requiredDecl("model.robot-design", DependencyKind::Object);
    ApplicabilityCondition c;
    c.conditionToken = "always";
    c.referencedKeys = {"policy"};
    extraCond.applicability = std::move(c);
    const auto issues2 = validateDependencyDeclarations({extraCond}, factKeys());
    ASSERT_EQ(issues2.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(issues2[0].code, DependencyIssueCode::RequiredWithCondition);
}

/** 反例（规则 3）：条件 token 空、引用键空集、引用键语法非法——逐类拒绝。 */
TEST(EvidenceDependencyDeclarationClosure, ConditionContentViolations_EV_DEP)
{
    // conditionToken 空（归域 token 的编码安全下限）。
    DependencyDeclaration emptyToken = conditionalDecl("collision-models",
                                                       DependencyKind::Object, {"policy"});
    emptyToken.applicability->conditionToken.clear();
    const auto issues1 = validateDependencyDeclarations({emptyToken}, factKeys());
    ASSERT_EQ(issues1.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(issues1[0].code, DependencyIssueCode::ConditionTokenInvalid);

    // referencedKeys 空集（条件不由任何依赖决定——永不重解析通道）。
    DependencyDeclaration emptyRefs = conditionalDecl("collision-models",
                                                      DependencyKind::Object, {});
    const auto issues2 = validateDependencyDeclarations({emptyRefs}, factKeys());
    ASSERT_EQ(issues2.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(issues2[0].code, DependencyIssueCode::ReferencedKeysEmpty);

    // 引用键语法非法（词表外字符——先于闭包检查拦截）。
    DependencyDeclaration badRef = conditionalDecl("collision-models",
                                                   DependencyKind::Object, {"Policy!"});
    const auto issues3 = validateDependencyDeclarations({badRef}, factKeys());
    ASSERT_EQ(issues3.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(issues3[0].code, DependencyIssueCode::ReferencedKeySyntax);
    EXPECT_EQ(issues3[0].key, "Policy!");

    // 声明 key 自身语法非法（规则 1）。
    const auto issues4 = validateDependencyDeclarations(
        {requiredDecl("Model_Robot", DependencyKind::Object)}, factKeys());
    ASSERT_EQ(issues4.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(issues4[0].code, DependencyIssueCode::DeclarationKeySyntax);
}

/** 反例（规则 5）：同一声明集内 key 重复 → 歧义声明拒绝（定位到重复项）。 */
TEST(EvidenceDependencyDeclarationClosure, DuplicateDeclarationKey_EV_DEP)
{
    std::vector<DependencyDeclaration> decls;
    decls.push_back(requiredDecl("model.robot-design", DependencyKind::Object));
    decls.push_back(requiredDecl("task-points", DependencyKind::Object));
    decls.push_back(requiredDecl("model.robot-design", DependencyKind::Policy)); // 重复键
    const auto issues = validateDependencyDeclarations(decls, factKeys());
    ASSERT_EQ(issues.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(issues[0].code, DependencyIssueCode::DuplicateDeclarationKey);
    EXPECT_EQ(issues[0].index, static_cast<std::size_t>(2)) << "下标应指向重复出现的条目";
}

// =====================================================================
// EV-DEP-4 注册拒绝原语（fail-fast 轨——§4.2.3①/§9.4 注册边界语义）
// =====================================================================

/** 拒绝原语：合法集静默通过；非法集抛 EvidenceError(DeclarationInvalid) 且消息聚合。 */
TEST(EvidenceDependencyRequire, ThrowOnClosureViolation_EV_DEP)
{
    // 正例：合法集不抛。
    std::vector<DependencyDeclaration> ok;
    ok.push_back(requiredDecl("model.robot-design", DependencyKind::Object));
    EXPECT_NO_THROW(requireValidDependencyDeclarations(ok, factKeys()));

    // 反例：闭包违约抛 DeclarationInvalid；what() 带 token 前缀＋聚合消息。
    std::vector<DependencyDeclaration> bad;
    bad.push_back(requiredDecl("model.robot-design", DependencyKind::Object));
    bad.push_back(conditionalDecl("collision-models", DependencyKind::Object,
                                  {"undeclared-ref"}));
    bool caught = false;
    try {
        requireValidDependencyDeclarations(bad, factKeys());
    } catch (const EvidenceError& ex) {
        caught = true;
        EXPECT_EQ(ex.code(), EvidenceErrorCode::DeclarationInvalid);
        // what() 前缀＝稳定 token（Errors.hpp 消息约定），消息含涉事键。
        EXPECT_EQ(std::string{ex.what()}.substr(0, std::string{"evidence/"}.size()), "evidence/");
        EXPECT_NE(std::string{ex.what()}.find("undeclared-ref"), std::string::npos)
            << "拒绝消息应聚合涉事键: " << ex.what();
    }
    EXPECT_TRUE(caught) << "闭包违约必须以 EvidenceError fail-fast（§9.4 注册边界）";
}

// =====================================================================
// EV-DEP-5 条目语法校验正例（acceptance 3 正例面——七类 Kind 各一合法条目）
// =====================================================================

/** 正例：七类 Kind 各一条合法条目（字典序存放）→ 零 issue；Kind 名表钉住。 */
TEST(EvidenceDependencyEntrySyntax, ValidEntries_EV_DEP)
{
    // Kind 稳定名表（实现侧词表——枚举声明序；登记单元卡 v0.3）。
    EXPECT_EQ(dependencyKindToken(DependencyKind::Object), "object");
    EXPECT_EQ(dependencyKindToken(DependencyKind::Configuration), "configuration");
    EXPECT_EQ(dependencyKindToken(DependencyKind::Policy), "policy");
    EXPECT_EQ(dependencyKindToken(DependencyKind::NameMap), "name-map");
    EXPECT_EQ(dependencyKindToken(DependencyKind::SampleSet), "sample-set");
    EXPECT_EQ(dependencyKindToken(DependencyKind::UpstreamResult), "upstream-result");
    EXPECT_EQ(dependencyKindToken(DependencyKind::Environment), "environment");

    // 七类各一条，按 (kind,key) 枚举序/字典序排放——合法冻结形态。
    std::vector<DependencyEntry> entries;

    DependencyEntry obj = objectEntry("model.robot-design");                    // Object
    DependencyEntry cfg;                                                        // Configuration
    cfg.key = "ik-solver-config";
    cfg.kind = DependencyKind::Configuration;
    ConfigurationDependencyPayload cp;
    cp.configKindToken = "ik-init-strategy";
    cp.canonicalBytes = {0x01, 0x02, 0x03};
    cp.contentIdentity = cid(kHex64);
    cfg.payload = std::move(cp);

    DependencyEntry pol;                                                        // Policy
    pol.key = "policy";
    pol.kind = DependencyKind::Policy;
    PolicyDependencyPayload pp;
    pp.policyContentIdentity = cid(kHex64);
    pol.payload = std::move(pp);

    DependencyEntry nm;                                                         // NameMap
    nm.key = "name-map";
    nm.kind = DependencyKind::NameMap;
    NameMapDependencyPayload np;
    np.nameMapContentIdentity = cid(kHex64);
    nm.payload = std::move(np);

    DependencyEntry ss;                                                         // SampleSet
    ss.key = "region-samples";
    ss.kind = DependencyKind::SampleSet;
    SampleSetDependencyPayload sp;
    sp.regionObjectId = core::ObjectId::generate();
    sp.sampleSetIdentity = cid(kHex64);
    ss.payload = std::move(sp);

    DependencyEntry up;                                                         // UpstreamResult
    up.key = "kin-result";
    up.kind = DependencyKind::UpstreamResult;
    UpstreamResultRef ur;
    ur.upstreamKey = "kin-output";
    ur.upstreamSliceId = cid(kHex64);
    ur.upstreamRunId = core::RunId::generate();   // 限定运行（可选字段有值形态）
    up.payload = std::move(ur);

    DependencyEntry env;                                                        // Environment
    env.key = "compiler-contract-version";
    env.kind = DependencyKind::Environment;
    EnvironmentDependencyPayload ep;
    ep.token = "compiler-contract-version";
    ep.valueToken = "rw-2022-baseline-1";
    env.payload = std::move(ep);

    // 未适用的条件条目（合法形态：applied=false＋非空原因——D-10 保留条目）。
    DependencyEntry notApplied = objectEntry("collision-models");
    notApplied.applied = false;
    notApplied.notAppliedReason = "策略未启用碰撞（§4.2.3 场景表第 1 行）";
    // 排序：object 组内 "collision-models" < "model.robot-design"。
    entries.push_back(notApplied);
    entries.push_back(std::move(obj));
    entries.push_back(std::move(cfg));
    entries.push_back(std::move(pol));
    entries.push_back(std::move(nm));
    entries.push_back(std::move(ss));
    entries.push_back(std::move(up));
    entries.push_back(std::move(env));

    const auto issues = validateDependencyEntries(entries);
    EXPECT_TRUE(issues.empty())
        << "七类合法条目应零问题（首条: "
        << (issues.empty() ? std::string{} : issues.front().message) << "）";
}

// =====================================================================
// EV-DEP-6 条目语法校验反例（acceptance 3 反例面——载荷/配对/存储逐类）
// =====================================================================

/** 反例：载荷与 kind 错配／monostate——PayloadKindMismatch（两类各一）。 */
TEST(EvidenceDependencyEntrySyntax, PayloadKindMismatch_EV_DEP)
{
    // monostate（默认构造未填载荷——合法条目不得处于保留态）。
    DependencyEntry empty;
    empty.key = "model.robot-design";
    empty.kind = DependencyKind::Object;
    const auto issues1 = validateDependencyEntries({empty});
    ASSERT_EQ(issues1.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(issues1[0].code, DependencyIssueCode::PayloadKindMismatch);

    // 错配：kind=Object 却装 Policy 载荷（冻结期必须拒绝——错配载荷会
    // 产生错误 canonical 身份）。
    DependencyEntry crossed = objectEntry("model.robot-design");
    PolicyDependencyPayload pp;
    pp.policyContentIdentity = cid(kHex64);
    crossed.payload = std::move(pp);
    const auto issues2 = validateDependencyEntries({crossed});
    ASSERT_EQ(issues2.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(issues2[0].code, DependencyIssueCode::PayloadKindMismatch);
}

/** 反例：Object 载荷字段非法——零 oid／零 cv／空 typeToken 逐类拒绝。 */
TEST(EvidenceDependencyEntrySyntax, ObjectPayloadViolations_EV_DEP)
{
    // 零 ObjectId（保留值纪律——core §4.1 isValid=false）。
    DependencyEntry zeroId = objectEntry("model.robot-design");
    std::get<ObjectDependencyPayload>(zeroId.payload).objectId = core::ObjectId{};
    const auto issues1 = validateDependencyEntries({zeroId});
    ASSERT_EQ(issues1.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(issues1[0].code, DependencyIssueCode::ObjectPayloadInvalid);

    // 零 ContentVersion（对象内容版本缺失＝身份凭据缺失）。
    DependencyEntry zeroCv = objectEntry("model.robot-design");
    std::get<ObjectDependencyPayload>(zeroCv.payload).contentVersion = core::ContentVersion{};
    const auto issues2 = validateDependencyEntries({zeroCv});
    ASSERT_EQ(issues2.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(issues2[0].code, DependencyIssueCode::ObjectPayloadInvalid);

    // 空 objectTypeToken（域类型 token 编码安全下限）。
    DependencyEntry emptyToken = objectEntry("model.robot-design");
    std::get<ObjectDependencyPayload>(emptyToken.payload).objectTypeToken.clear();
    const auto issues3 = validateDependencyEntries({emptyToken});
    ASSERT_EQ(issues3.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(issues3[0].code, DependencyIssueCode::ObjectPayloadInvalid);

    // 条目 key 语法非法（与声明同闸门——EntryKeySyntax）。
    DependencyEntry badKey = objectEntry("Model_Robot");
    const auto issues4 = validateDependencyEntries({badKey});
    ASSERT_EQ(issues4.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(issues4[0].code, DependencyIssueCode::EntryKeySyntax);
}

/** 反例：其余五类 Kind 的载荷字段非法——逐类一反例（CON-06 非空纪律）。 */
TEST(EvidenceDependencyEntrySyntax, OtherPayloadViolations_EV_DEP)
{
    // Configuration：空 canonical 字节（配置未承载即占位条目）。
    DependencyEntry cfg;
    cfg.key = "ik-solver-config";
    cfg.kind = DependencyKind::Configuration;
    ConfigurationDependencyPayload cp;
    cp.configKindToken = "ik-init-strategy";
    cp.contentIdentity = cid(kHex64);           // canonicalBytes 留空
    cfg.payload = std::move(cp);
    const auto issues1 = validateDependencyEntries({cfg});
    ASSERT_EQ(issues1.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(issues1[0].code, DependencyIssueCode::ConfigurationPayloadInvalid);

    // Policy：零内容身份（CON-06——策略必须进切片身份）。
    DependencyEntry pol;
    pol.key = "policy";
    pol.kind = DependencyKind::Policy;
    pol.payload = PolicyDependencyPayload{core::ContentIdentity{}};
    const auto issues2 = validateDependencyEntries({pol});
    ASSERT_EQ(issues2.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(issues2[0].code, DependencyIssueCode::PolicyPayloadInvalid);

    // NameMap：零内容身份（CON-06 同源）。
    DependencyEntry nm;
    nm.key = "name-map";
    nm.kind = DependencyKind::NameMap;
    nm.payload = NameMapDependencyPayload{core::ContentIdentity{}};
    const auto issues3 = validateDependencyEntries({nm});
    ASSERT_EQ(issues3.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(issues3[0].code, DependencyIssueCode::NameMapPayloadInvalid);

    // SampleSet：零样本集身份（冻结凭据缺失——KIN-04 分母来源失效）。
    DependencyEntry ss;
    ss.key = "region-samples";
    ss.kind = DependencyKind::SampleSet;
    SampleSetDependencyPayload sp;
    sp.regionObjectId = core::ObjectId::generate();
    sp.sampleSetIdentity = core::ContentIdentity{};   // 身份留零
    ss.payload = std::move(sp);
    const auto issues4 = validateDependencyEntries({ss});
    ASSERT_EQ(issues4.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(issues4[0].code, DependencyIssueCode::SampleSetPayloadInvalid);

    // UpstreamResult：上游键语法非法＋空 sliceId（报一条 Upstream 载荷非法）。
    DependencyEntry up;
    up.key = "kin-result";
    up.kind = DependencyKind::UpstreamResult;
    UpstreamResultRef ur;
    ur.upstreamKey = "Kin_Result";                // 大写下划线——语法非法
    ur.upstreamSliceId = core::ContentIdentity{}; // 身份留零
    up.payload = std::move(ur);
    const auto issues5 = validateDependencyEntries({up});
    ASSERT_EQ(issues5.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(issues5[0].code, DependencyIssueCode::UpstreamResultPayloadInvalid);

    // Environment：valueToken 空（版本要素缺值＝兼容判定失去载体）。
    DependencyEntry env;
    env.key = "compiler-contract-version";
    env.kind = DependencyKind::Environment;
    EnvironmentDependencyPayload ep;
    ep.token = "compiler-contract-version";
    ep.valueToken = "";                            // 值留空
    env.payload = std::move(ep);
    const auto issues6 = validateDependencyEntries({env});
    ASSERT_EQ(issues6.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(issues6[0].code, DependencyIssueCode::EnvironmentPayloadInvalid);
}

/** 反例：applied/notAppliedReason 配对矛盾——未适用缺原因／适用带原因。 */
TEST(EvidenceDependencyEntrySyntax, AppliedReasonPairing_EV_DEP)
{
    // 未适用却无原因（D-10 保留条目必须可解释——呈现"为何不适用"）。
    DependencyEntry missing = objectEntry("collision-models");
    missing.applied = false;                      // notAppliedReason 留 nullopt
    const auto issues1 = validateDependencyEntries({missing});
    ASSERT_EQ(issues1.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(issues1[0].code, DependencyIssueCode::AppliedReasonInconsistent);

    // 未适用且原因为空串（presence 语义：空串不算"有原因"——§5.2）。
    DependencyEntry blank = objectEntry("collision-models");
    blank.applied = false;
    blank.notAppliedReason = std::string{};
    const auto issues2 = validateDependencyEntries({blank});
    ASSERT_EQ(issues2.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(issues2[0].code, DependencyIssueCode::AppliedReasonInconsistent);

    // 已应用却带原因（矛盾噪声——presence 字节会编码出不存在的"未适用"）。
    DependencyEntry extra = objectEntry("model.robot-design");
    extra.applied = true;
    extra.notAppliedReason = "不应有原因";
    const auto issues3 = validateDependencyEntries({extra});
    ASSERT_EQ(issues3.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(issues3[0].code, DependencyIssueCode::AppliedReasonInconsistent);
}

/** 反例：空条目集、乱序、(kind,key) 重复——存储不变量（§4.2.2）。 */
TEST(EvidenceDependencyEntrySyntax, EntrySetInvariants_EV_DEP)
{
    // 空集（entries ≥1——无依赖的评估没有失效语义）。
    const auto issues0 = validateDependencyEntries({});
    ASSERT_EQ(issues0.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(issues0[0].code, DependencyIssueCode::EmptyEntrySet);
    EXPECT_EQ(issues0[0].index, DependencyIssue::npos);

    // 乱序：Object 组内 "model.robot-design" 在 "collision-models" 之前。
    std::vector<DependencyEntry> unsorted;
    unsorted.push_back(objectEntry("model.robot-design"));
    unsorted.push_back(objectEntry("collision-models"));
    const auto issues1 = validateDependencyEntries(unsorted);
    ASSERT_EQ(issues1.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(issues1[0].code, DependencyIssueCode::EntrySetNotSorted);
    EXPECT_EQ(issues1[0].index, static_cast<std::size_t>(1));

    // 跨 kind 乱序：Policy 条目排在 Object 之前（kind 序＝枚举声明序）。
    DependencyEntry pol;
    pol.key = "policy";
    pol.kind = DependencyKind::Policy;
    pol.payload = PolicyDependencyPayload{cid(kHex64)};
    std::vector<DependencyEntry> crossKind;
    crossKind.push_back(std::move(pol));
    crossKind.push_back(objectEntry("model.robot-design"));
    const auto issues2 = validateDependencyEntries(crossKind);
    ASSERT_EQ(issues2.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(issues2[0].code, DependencyIssueCode::EntrySetNotSorted);

    // 重复：(kind,key) 完全相同的两条（稳定存储要求唯一）。
    std::vector<DependencyEntry> duplicated;
    duplicated.push_back(objectEntry("model.robot-design"));
    duplicated.push_back(objectEntry("model.robot-design"));
    const auto issues3 = validateDependencyEntries(duplicated);
    ASSERT_EQ(issues3.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(issues3[0].code, DependencyIssueCode::DuplicateEntry);
    EXPECT_EQ(issues3[0].index, static_cast<std::size_t>(1));
}

/** 值语义抽查：等值比较逐成员生效（payload/applied/原因都参与比较）。 */
TEST(EvidenceDependencyEntrySyntax, ValueSemanticsEquality_EV_DEP)
{
    DependencyEntry a = objectEntry("model.robot-design");
    // 拷贝派生（objectEntry 每次生成新 ObjectId——逐字段相同的条目必须经
    // 拷贝构造，这正是强类型身份"逐字节等值"的语义验证点）。
    DependencyEntry b = a;
    EXPECT_EQ(a, b) << "同字段条目应等值";
    // 载荷差异（对象版本不同——不同内容版本即不同条目）。
    std::get<ObjectDependencyPayload>(b.payload).contentVersion = cv(
        "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    EXPECT_NE(a, b);
    // applied/原因差异（条件解析结果参与等值）。
    DependencyEntry c = a;
    c.applied = false;
    c.notAppliedReason = "未启用";
    EXPECT_NE(a, c);
    // 声明等值（适用条件差异参与比较）。
    DependencyDeclaration d1 = conditionalDecl("collision-models", DependencyKind::Object,
                                               {"policy"});
    DependencyDeclaration d2 = d1;
    d2.applicability->referencedKeys.push_back("case-set");
    EXPECT_NE(d1, d2);
}

}  // namespace
