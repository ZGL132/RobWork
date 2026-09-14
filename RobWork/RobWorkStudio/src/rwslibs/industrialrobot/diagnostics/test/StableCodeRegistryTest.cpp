/**
 * @file   StableCodeRegistryTest.cpp
 * @brief  稳定码注册表用例组（DT-REG-1~5）——§4.6 码表收编一致性、注册
 *         边界拒绝、未知/异常文本码拒识、参数模式/版本冲突/废弃 tombstone。
 *
 * 设计依据：
 *   - units/diagnostics.md §10 DT-REG-1~DT-REG-5（验证矩阵逐行）、§4.5
 *     （注册表行为冻结表）、§4.5.1（废弃码与迁移）、§4.6（内置码表）、
 *     §9.1（接口契约）、§9.0（错误码面）；
 *   - 需求 ERR-01（诊断码稳定）、NFR-MNT-03（码/文案单一权威——重复定义
 *     注册边界拒绝；titleKey/detailKey 注册期唯一）；
 *   - 任务契约 tasks/foundation/DIAG-T03.json acceptance 1~4（逐条自证：
 *     acceptance 1→DtReg1 组；acceptance 2→DtReg2/3/4/5 组；acceptance 3→
 *     DtPdiag3；acceptance 4→DtPdiag1/DtPdiag9）；
 *   - 用例名后缀＝矩阵行编号（DT-REG-x），与 ird-test-report.json 的
 *     trace 追溯字段呼应（AGENTS.md §4.2 验证留痕）。
 *
 * ◆ 用例覆盖边界声明（§11 任务分工——DIAG-T04 依赖本任务）：
 *   DT-REG-3/4/5 的操作列含"以未注册码调工厂""经未登记类型 translate"
 *   "paramSchema 缺参构造""deprecate 后构造"等**工厂侧**动作；工厂与
 *   ErrorCodeTranslator 是 §11 DIAG-T04 行产物，不在本任务交付面。本套件
 *   对上述行自证其**注册表侧判定原语与错误码面**（find 拒识、前缀冲突
 *   CodeUnknown、句法 Usage、tombstone 只读解析、ParamSchemaMismatch），
 *   并在用例注释逐处标注"完整构造/translate 路径随 DIAG-T04 落地"——
 *   验收对照矩阵时以此为覆盖口径，工厂路径用例在 DIAG-T04/DIAG-T10 套件
 *   补全。此分工为 §11 任务行原文（T03 产物＝Errors.* 与 DiagCodes.*），非
 *   实现方私裁。
 *
 * 线程约束：全部用例单线程（注册期单线程约定——§4.5"线程安全"行）。
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <sdurws/ird/core/DiagData.hpp>
#include <sdurws/ird/diagnostics/DiagCodes.hpp>
#include <sdurws/ird/diagnostics/Errors.hpp>

namespace {

using namespace sdurws::ird::diagnostics;

// ---------------------------------------------------------------------
// 夹具辅助：以最小合法字段构造描述符（注册期验证链的"正例基线"——
// 各用例仅偏离被测字段）。
// ---------------------------------------------------------------------

/// 合法描述符基线：RT 前缀（runtime 域）、Error、无参数、非确认类。
CodeDescriptor makeValidDescriptor(const std::string& code)
{
    CodeDescriptor d;
    d.code = code;
    d.ownerUnit = "runtime";
    d.category = DiagnosticCategory::InputInvalid;
    d.severity = DiagnosticSeverity::Error;
    d.titleKey = "diag." + [&] {
        std::string lower;
        for (const char ch : code) {
            lower.push_back(ch >= 'A' && ch <= 'Z' ? static_cast<char>(ch - 'A' + 'a') : ch);
        }
        return lower;
    }() + ".title";
    d.detailKey = "diag." + [&] {
        std::string lower;
        for (const char ch : code) {
            lower.push_back(ch >= 'A' && ch <= 'Z' ? static_cast<char>(ch - 'A' + 'a') : ch);
        }
        return lower;
    }() + ".detail";
    d.paramSchema = "[]";
    d.confirmable = false;
    d.requiresComparison = false;
    d.retryable = RetryKind::UserRetry;
    d.userVisible = true;
    d.reportable = true;
    d.historical = true;
    d.registryVersion = 1;
    d.deprecated = false;
    return d;
}

/// 断言抛出 DiagnosticsError 且错误码为 expected（错误码面钉住——§9.0
/// token 由 token() 逐值用例另行覆盖，这里断言枚举值）。
template <class Fn>
void expectThrowsWithCode(Fn&& fn, DiagnosticsErrorCode expected, const char* what)
{
    try {
        fn();
        FAIL() << what << "：未抛出异常（应拒绝并抛 DiagnosticsError）";
    } catch (const DiagnosticsError& e) {
        EXPECT_EQ(e.code(), expected) << what << "：错误码面不符（what()=" << e.what() << "）";
    } catch (...) {
        FAIL() << what << "：抛出了非 DiagnosticsError 异常（单元唯一异常类型纪律）";
    }
}

// ---------------------------------------------------------------------
// §4.6 收编清单原文（码值第二来源——与实现表逐位交叉比对，防实现表
// 漂移/重排；拼写逐字来自 units/diagnostics.md §4.6 各行清单）。
// ---------------------------------------------------------------------

/// §4.6 全量 87 码（行序＝§4.6 表行序、行内＝收编清单原文序）。
const std::vector<const char*>& builtinCodeLiterals()
{
    static const std::vector<const char*> kCodes{
        // PRJ 10
        "PRJ-LOCK-HELD", "PRJ-STORE-CORRUPT", "PRJ-RECOVERY-IGNORED-UNCOMMITTED",
        "PRJ-RECOVERY-ORPHAN-DRAFT", "PRJ-RECOVERY-DANGLING-OBJECTS", "PRJ-SCHEMA-FUTURE",
        "PRJ-FORMAT-LEGACY", "PRJ-STALE-REVISION-REJECTED", "PRJ-ARCHIVE-CONFLICT",
        "PRJ-WRITE-AUTHORITY-LOST",
        // RT 14
        "RT-INPUT-INVALID", "RT-STRUCTURE-INVALID", "RT-UNIT-MISMATCH", "RT-RESOURCE-MISSING",
        "RT-RESOURCE-CHANGED", "RT-RESOURCE-BUDGET", "RT-WC-COMPILE-FAILED",
        "RT-DWC-COMPILE-FAILED", "RT-NAME-CONFLICT", "RT-BASE-WORLD-INCONSISTENT",
        "RT-CAPABILITY-MISSING", "RT-ROBWORK-ERROR", "RT-CACHE-INCOMPATIBLE", "RT-CANCELLED",
        // POLICY 23
        "POLICY-SCHEMA-UNKNOWN-FIELD", "POLICY-SCHEMA-VERSION-FUTURE",
        "POLICY-SCHEMA-VERSION-UNKNOWN", "POLICY-THRESHOLD-NON-FINITE",
        "POLICY-THRESHOLD-NON-POSITIVE", "POLICY-THRESHOLD-OUT-OF-RANGE",
        "POLICY-UNIT-MISMATCH", "POLICY-RULE-DUPLICATE", "POLICY-RULE-CONFLICT",
        "POLICY-RULE-CYCLE", "POLICY-SCOPE-OBJECT-MISSING", "POLICY-APPLICABILITY-INVALID",
        "POLICY-VERSION-INCOMPATIBLE", "POLICY-CONTENT-IDENTITY-MISMATCH",
        "POLICY-CLL-DETECTOR-UNAVAILABLE", "POLICY-CLL-SCENE-INVALID",
        "POLICY-CLL-NAME-UNRESOLVED", "POLICY-CLL-CONTEXT-EXPIRED",
        "POLICY-CLL-EVALUATION-FAILED", "POLICY-CLL-GEOMETRY-MISSING",
        "POLICY-JNT-TABLE-INVALID", "POLICY-ENGINEERING-RANGE-INVALID",
        "POLICY-INFO-DEFAULT-APPLIED",
        // EVI 7
        "EVI-SNAPSHOT-INCOMPLETE", "EVI-CASE-COVERAGE-MISSING", "EVI-EVIDENCE-MISSING",
        "EVI-PROOF-INVALID", "EVI-ENVELOPE-ILLEGAL-COMBINATION", "EVI-CACHE-INCOMPATIBLE",
        "EVI-EVALUATOR-DUPLICATE",
        // EX 18
        "EX-TASK-REJECTED", "EX-SNAPSHOT-STALE", "EX-STORE-READ-ONLY",
        "EX-RESOURCE-INSUFFICIENT", "EX-CAPABILITY-UNSUPPORTED", "EX-WORKER-LAUNCH-FAILED",
        "EX-WORKER-CRASHED", "EX-WORKER-HUNG", "EX-FORCE-TERMINATED",
        "EX-CHANNEL-PROTOCOL-ERROR", "EX-REGISTRY-UNKNOWN-RUN", "EX-REGISTRY-MISMATCH",
        "EX-STALE-ATTEMPT", "EX-CHECKPOINT-CORRUPT", "EX-CHECKPOINT-INCOMPATIBLE",
        "EX-ARCHIVE-FAILED", "EX-ARCHIVE-AUTHORITY-LOST", "EX-TASK-INTERRUPTED",
        // RPT 8
        "RPT-SOURCE-MISSING", "RPT-SCOPE-INSUFFICIENT", "RPT-CONSISTENCY-MISMATCH",
        "RPT-ARCHIVE-CONFLICT", "RPT-EXPORT-FAILED", "RPT-ROUNDTRIP-MISMATCH",
        "RPT-CURRENTNESS-UNEVALUABLE", "RPT-SECTION-NOT-APPLICABLE",
        // DIAG 7
        "DIAG-REDACTION-FAILED", "DIAG-REGISTRY-DUPLICATE", "DIAG-REGISTRY-UNKNOWN-CODE",
        "DIAG-CATALOG-OVERFLOW", "DIAG-LOG-WRITE-FAILED", "DIAG-FINDING-BINDING-INVALID",
        "DIAG-FINDING-EXPIRED",
    };
    return kCodes;
}

/// 构造一个已装配 §4.6 全表的注册表（多数用例的前置态——"注册表就绪"）。
/// 返回 unique_ptr：StableCodeRegistry 为进程级单例语义（禁拷贝/禁移动，
/// §9.1 契约表"消费方持引用"）——按值传递/返回不可用，测试以独占指针承载。
std::unique_ptr<StableCodeRegistry> makeBuiltinRegistry()
{
    auto registry = std::make_unique<StableCodeRegistry>();
    registerBuiltinCodes(*registry);
    return registry;
}

}  // namespace

// =====================================================================
// DT-REG-1（acceptance 1）：码表清单与 §4.6 逐项一致＋manifest 摘要稳定。
// =====================================================================

/** DT-REG-1：全量注册成功、find 命中、registeredCodes 计数＝清单、manifest
 *  两次计算稳定（矩阵行"摘要逐字节相等"）。 */
TEST(DiagCodesRegistry, DtReg1_BuiltinTableRegistersFullyAndManifestStable)
{
    // ---- 空注册表＋全量注册（矩阵行前置"空注册表"）----
    StableCodeRegistry registry;
    EXPECT_FALSE(registry.sealed());
    registerBuiltinCodes(registry);   // 不抛＝87 项全部通过注册期验证

    // ---- registeredCodes 计数＝§4.6 清单（10/14/23/7/18/8/7）----
    EXPECT_EQ(registry.registeredCodes("project").size(), 10u);
    EXPECT_EQ(registry.registeredCodes("runtime").size(), 14u);
    EXPECT_EQ(registry.registeredCodes("policy").size(), 23u);
    EXPECT_EQ(registry.registeredCodes("evidence").size(), 7u);
    EXPECT_EQ(registry.registeredCodes("execution").size(), 18u);
    EXPECT_EQ(registry.registeredCodes("reporting").size(), 8u);
    EXPECT_EQ(registry.registeredCodes("diagnostics").size(), 7u);
    // 非 ownerUnit 查询为空（不存在跨域混登——§4.5 前缀-所有权一致性）。
    EXPECT_TRUE(registry.registeredCodes("kinematics").empty());
    EXPECT_TRUE(registry.registeredCodes("").empty());

    // ---- 全表逐码 find 命中且 ownerUnit 归属正确（"全部注册成功；find 命中"）----
    for (const CodeDescriptor& d : builtinCodeDescriptors()) {
        const CodeDescriptor* found = registry.find(d.code);
        ASSERT_NE(found, nullptr) << "码未命中: " << d.code;
        EXPECT_EQ(found->ownerUnit, d.ownerUnit) << "归属漂移: " << d.code;
        EXPECT_FALSE(found->deprecated) << "内置表初始不应含 tombstone: " << d.code;
    }

    // ---- manifest 摘要两次计算稳定（矩阵行"manifest 摘要稳定"）----
    const CodeTableManifest first = registry.manifest();
    const CodeTableManifest second = registry.manifest();
    EXPECT_EQ(first.entries.size(), 87u);
    ASSERT_EQ(first.entries.size(), second.entries.size());
    for (std::size_t i = 0; i < first.entries.size(); ++i) {
        EXPECT_TRUE(first.entries[i] == second.entries[i])
            << "manifest 条目不稳定 @" << i << ": " << first.entries[i].code;
    }
    EXPECT_TRUE(first.digest == second.digest) << "manifest 摘要两次计算不等";
    EXPECT_TRUE(first == second);
    EXPECT_TRUE(first.digest.isValid());   // 非全零——真实摘要
}

/** DT-REG-1 补充：码值集合与 §4.6 收编原文逐位一致（码值不重排——dtb
 *  WP-09-T03 约束；实现表与测试内嵌清单双来源交叉钉住）。 */
TEST(DiagCodesRegistry, DtReg1_BuiltinTableValuesMatchDesignVerbatim)
{
    const std::vector<CodeDescriptor>& table = builtinCodeDescriptors();
    const std::vector<const char*>& literals = builtinCodeLiterals();
    ASSERT_EQ(table.size(), literals.size()) << "内置表条目数与 §4.6 清单不一致";
    for (std::size_t i = 0; i < literals.size(); ++i) {
        ASSERT_EQ(table[i].code, literals[i])
            << "第 " << i << " 位码值漂移（码值一经登记不重排）：实现表="
            << table[i].code << "，§4.6 原文=" << literals[i];
    }
}

/** DT-REG-1 补充：manifest 确定性——注册顺序不同的两个注册表必得同一
 *  清单序与同一摘要（NFR-COR-02；worker 握手比对的前提）。 */
TEST(DiagCodesRegistry, DtReg1_ManifestIndependentOfRegistrationOrder)
{
    const std::vector<CodeDescriptor> table = builtinCodeDescriptors();

    // 顺序 A：原文序（registerBuiltinCodes 同路径）。
    StableCodeRegistry ordered;
    registerBuiltinCodes(ordered);

    // 顺序 B：逆序注册（同一注册集——任意注册顺序必得同一 manifest）。
    StableCodeRegistry reversed;
    for (auto it = table.rbegin(); it != table.rend(); ++it) {
        reversed.registerCode(*it);
    }

    const CodeTableManifest mA = ordered.manifest();
    const CodeTableManifest mB = reversed.manifest();
    ASSERT_EQ(mA.entries.size(), mB.entries.size());
    // 条目按 code 字典序（§9.1 后置）——两表逐位相等。
    for (std::size_t i = 0; i < mA.entries.size(); ++i) {
        EXPECT_EQ(mA.entries[i].code, mB.entries[i].code);
    }
    EXPECT_TRUE(mA == mB) << "同注册集不同注册顺序得到不同 manifest（确定性违约）";
    // 字典序实测：首条目＝DIAG-*（'D' 最小，DIAG- 内部 CATALOG 居首）；
    // 尾条目＝RT-WC-COMPILE-FAILED（RT 为最大前缀，段内 "WC" > "UNIT"）。
    // 显式断言首尾，钉住"按 code 字典序排序"后置。
    EXPECT_EQ(mA.entries.front().code, std::string("DIAG-CATALOG-OVERFLOW"));
    EXPECT_EQ(mA.entries.back().code, std::string("RT-WC-COMPILE-FAILED"));
}

// =====================================================================
// DT-REG-2（acceptance 2）：重复注册拒绝——不覆盖、不静默（NFR-MNT-03）。
// =====================================================================

/** DT-REG-2：同码再注册（含元数据全同）→ DuplicateCode 拒绝；find 返回
 *  首版描述符（矩阵行"抛错码；find 返回首版描述"）。 */
TEST(DiagCodesRegistry, DtReg2_DuplicateRegistrationRejectedNotOverwritten)
{
    StableCodeRegistry registry;
    const CodeDescriptor original = makeValidDescriptor("RT-DUP-TEST");
    registry.registerCode(original);

    // 同码＋元数据全同：仍是重复（注册边界拒绝——"含元数据全同"矩阵原文）。
    expectThrowsWithCode([&] { registry.registerCode(original); },
                         DiagnosticsErrorCode::DuplicateCode,
                         "同码全同元数据再注册应 DuplicateCode");

    // 同码＋不同元数据（severity 改 Warning）：同样拒绝——码是唯一键，
    // 元数据演进走装配清单修订，不走覆盖写（§4.5 行为表"重复注册"行）。
    CodeDescriptor mutated = original;
    mutated.severity = DiagnosticSeverity::Warning;
    expectThrowsWithCode([&] { registry.registerCode(mutated); },
                         DiagnosticsErrorCode::DuplicateCode,
                         "同码不同元数据再注册应 DuplicateCode");

    // find 返回首版（severity 仍 Error——"不覆盖"的观测面）。
    const CodeDescriptor* found = registry.find("RT-DUP-TEST");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->severity, DiagnosticSeverity::Error);
    EXPECT_TRUE(*found == original);
}

/** DT-REG-2/DT-REG-5：同码不同 registryVersion 的注册＝重复注册拒绝
 *  （§4.5"版本冲突"行原文："并发注册＝重复注册拒绝……不存在运行期热替换"）。 */
TEST(DiagCodesRegistry, DtReg5_VersionConflictIsDuplicateRejection)
{
    StableCodeRegistry registry;
    registry.registerCode(makeValidDescriptor("RT-VER-TEST"));

    CodeDescriptor newer = makeValidDescriptor("RT-VER-TEST");
    newer.registryVersion = 2;   // 仅版本号不同——语义与"完全同码"同面
    expectThrowsWithCode([&] { registry.registerCode(newer); },
                         DiagnosticsErrorCode::DuplicateCode,
                         "同码不同 registryVersion 应按重复注册拒绝");
    EXPECT_EQ(registry.find("RT-VER-TEST")->registryVersion, 1u) << "版本不得被覆盖";
}

/** DT-REG-2 补充（NFR-MNT-03 文案单一权威）：titleKey/detailKey 跨码唯一——
 *  键冲突＝重复定义，注册边界拒绝。 */
TEST(DiagCodesRegistry, DtReg2_TextKeyCollisionRejected)
{
    StableCodeRegistry registry;
    registry.registerCode(makeValidDescriptor("RT-KEY-A"));

    // RT-KEY-B 伪造 RT-KEY-A 的 titleKey（detailKey 不同）——键已占用。
    CodeDescriptor thief = makeValidDescriptor("RT-KEY-B");
    thief.titleKey = "diag.rt-key-a.title";
    expectThrowsWithCode([&] { registry.registerCode(thief); },
                         DiagnosticsErrorCode::DuplicateCode,
                         "titleKey 冲突应 DuplicateCode");

    // detailKey 同理。
    CodeDescriptor thief2 = makeValidDescriptor("RT-KEY-C");
    thief2.detailKey = "diag.rt-key-a.detail";
    expectThrowsWithCode([&] { registry.registerCode(thief2); },
                         DiagnosticsErrorCode::DuplicateCode,
                         "detailKey 冲突应 DuplicateCode");

    // 首个登记未被破坏（不覆盖不静默——键反查面仍指向 RT-KEY-A）。
    EXPECT_NE(registry.find("RT-KEY-A"), nullptr);
    EXPECT_EQ(registry.find("RT-KEY-A")->titleKey, std::string("diag.rt-key-a.title"));
}

// =====================================================================
// DT-REG-3（acceptance 2）：未知码拒绝（ERR-01）。
// =====================================================================

/** DT-REG-3：未注册码 find→nullptr（查询非抛）；前缀-所有权不符注册→
 *  CodeUnknown（§9.1"CodeUnknown 前缀冲突"）。
 *
 *  覆盖边界：矩阵行操作"以未注册码调工厂→CodeUnknown"的工厂构造路径随
 *  DIAG-T04 落地（§11 任务分工——见文件头边界声明）；本用例自证工厂将
 *  据以拒绝的注册表原语：未注册＝find nullptr（工厂唯一判据）＋所有权域
 *  违约＝CodeUnknown（与工厂拒绝同一错误码面）。 */
TEST(DiagCodesRegistry, DtReg3_UnknownCodeFindNullptrAndOwnershipConflictRejected)
{
    const auto registry = makeBuiltinRegistry();

    // 未注册码：句法合法但不在表（查询非抛→nullptr）。
    EXPECT_EQ(registry->find("RT-NO-SUCH-CODE"), nullptr);
    EXPECT_EQ(registry->find("PRJ-FUTURE-CODE"), nullptr);
    // 小写/空串（句法非法）同样未注册。
    EXPECT_EQ(registry->find("rt-input-invalid"), nullptr);
    EXPECT_EQ(registry->find(""), nullptr);

    // 前缀不在 §4.5 前缀表（"FOO"非登记前缀）→CodeUnknown。
    StableCodeRegistry writable;
    expectThrowsWithCode(
        [&] { writable.registerCode(makeValidDescriptor("FOO-BAR")); },
        DiagnosticsErrorCode::CodeUnknown,
        "未知前缀注册应 CodeUnknown");

    // 前缀在表但 ownerUnit 声明域不符（PRJ 码声明 runtime 所有）→CodeUnknown
    // （"前缀即所有权声明，跨前缀注册拒绝"——§4.5 命名空间约定原文）。
    CodeDescriptor impostor = makeValidDescriptor("PRJ-LOCK-HELD");
    impostor.titleKey = "diag.prj-lock-held.title";    // 键按码派生——仅 ownerUnit 作弊
    impostor.detailKey = "diag.prj-lock-held.detail";
    impostor.ownerUnit = "runtime";
    expectThrowsWithCode(
        [&] {
            StableCodeRegistry fresh;
            fresh.registerCode(impostor);
        },
        DiagnosticsErrorCode::CodeUnknown,
        "跨前缀注册应 CodeUnknown");
}

// =====================================================================
// DT-REG-4：异常文本不作码（任务约束——"诊断码不能通过异常文本临时生成"）。
// =====================================================================

/** DT-REG-4：异常 message 字符串直接作 code——句法非法、find 拒识、注册
 *  拒绝；DIAG-REGISTRY-UNKNOWN-CODE（translate 兜底条目码）已收编。
 *
 *  覆盖边界：矩阵行"经未登记类型 translate→DIAG-REGISTRY-UNKNOWN-CODE 条目"
 *  的条目生产随 DIAG-T04 工厂落地；本用例钉住其前置面：异常文本不可能
 *  成为码（注册表侧），且兜底码在表（translate 无需运行期登记）。 */
TEST(DiagCodesRegistry, DtReg4_ExceptionTextIsNotACode)
{
    const auto registry = makeBuiltinRegistry();

    // 模拟异常 what() 文本：含小写/空格/标点/冒号——均不满足码句法。
    const std::string exceptionText = "std::runtime_error: lock held by pid 42 (0x2A)";
    EXPECT_FALSE(isValidDiagCodeSyntax(exceptionText));
    EXPECT_EQ(registry->find(exceptionText), nullptr) << "异常文本作码必须拒识";

    // 尝试把异常文本注册为码→Usage（句法拦截——字符串不经登记不可能成为码）。
    CodeDescriptor fake;
    fake = makeValidDescriptor("RT-PLACEHOLDER");
    fake.code = exceptionText;
    fake.titleKey = "diag.rt-placeholder.title";   // 键保持占位合法——仅 code 越轨
    fake.detailKey = "diag.rt-placeholder.detail";
    expectThrowsWithCode(
        [&] {
            StableCodeRegistry fresh;
            fresh.registerCode(fake);
        },
        DiagnosticsErrorCode::Usage,
        "异常文本作码注册应 Usage（句法拒绝）");

    // 兜底码 DIAG-REGISTRY-UNKNOWN-CODE 已收编（translate 未登记类型的
    // 兜底条目码——DT-REG-4 期望"保留来源类型＋安全摘要"的载体）。
    const CodeDescriptor* fallback = registry->find("DIAG-REGISTRY-UNKNOWN-CODE");
    ASSERT_NE(fallback, nullptr);
    EXPECT_EQ(fallback->ownerUnit, std::string("diagnostics"));
    EXPECT_EQ(fallback->category, DiagnosticCategory::Internal);
}

// =====================================================================
// DT-REG-5：参数模式／版本冲突／废弃迁移（§4.5）。
// =====================================================================

/** DT-REG-5：paramSchema 注册期校验——缺省/形非法/参数名非法/重复拒绝；
 *  空数组"[]"是合法的显式无参数声明。 */
TEST(DiagCodesRegistry, DtReg5_ParamSchemaValidation)
{
    const auto schemaOf = [](std::string schema) {
        CodeDescriptor d = makeValidDescriptor("RT-SCHEMA-TEST");
        d.paramSchema = std::move(schema);
        return d;
    };

    // 完全缺省（空串）＝ParamSchemaMismatch（"非空且参数名合法"——无参数
    // 也须显式 "[]"，工厂占位一致性校验需要明确模式边界）。
    expectThrowsWithCode([&] {
        StableCodeRegistry r;
        r.registerCode(schemaOf(""));
    }, DiagnosticsErrorCode::ParamSchemaMismatch, "paramSchema 空串应 ParamSchemaMismatch");

    // 形非法：非数组起始。
    expectThrowsWithCode([&] {
        StableCodeRegistry r;
        r.registerCode(schemaOf("pid"));
    }, DiagnosticsErrorCode::ParamSchemaMismatch, "paramSchema 非数组形应拒绝");

    // 形非法：悬空逗号。
    expectThrowsWithCode([&] {
        StableCodeRegistry r;
        r.registerCode(schemaOf("[\"pid\",]"));
    }, DiagnosticsErrorCode::ParamSchemaMismatch, "paramSchema 悬空逗号应拒绝");

    // 参数名词形非法：大写（须 ^[a-z0-9]+(-[a-z0-9]+)*$）。
    expectThrowsWithCode([&] {
        StableCodeRegistry r;
        r.registerCode(schemaOf("[\"PID\"]"));
    }, DiagnosticsErrorCode::ParamSchemaMismatch, "paramSchema 大写参数名应拒绝");

    // 参数名重复。
    expectThrowsWithCode([&] {
        StableCodeRegistry r;
        r.registerCode(schemaOf("[\"pid\",\"pid\"]"));
    }, DiagnosticsErrorCode::ParamSchemaMismatch, "paramSchema 重复参数名应拒绝");

    // 合法面：空数组（显式无参数）与参数清单。
    {
        StableCodeRegistry r;
        r.registerCode(schemaOf("[]"));
        EXPECT_NE(r.find("RT-SCHEMA-TEST"), nullptr);
    }
    {
        StableCodeRegistry r;
        r.registerCode(schemaOf("[\"pid\",\"host\"]"));
        EXPECT_NE(r.find("RT-SCHEMA-TEST"), nullptr);
    }

    // 内置表逐码 paramSchema 均为合法形（87 码全部通过注册即证明——此处
    // 显式抽查 §9.2 示例码的参数面）。
    const auto builtin = makeBuiltinRegistry();
    EXPECT_EQ(builtin->find("PRJ-LOCK-HELD")->paramSchema, std::string("[\"pid\",\"host\"]"));
}

/** DT-REG-5：deprecate 后 tombstone——只读映射保留、supersededBy 导航、
 *  删除禁止（§4.5.1；PA-2 废弃码只读不改写历史）。 */
TEST(DiagCodesRegistry, DtReg5_DeprecateTombstoneKeptAndNavigable)
{
    StableCodeRegistry registry;
    registerBuiltinCodes(registry);
    const CodeTableManifest before = registry.manifest();

    // 废弃（迁移到已注册的新码）：tombstone 置位＋supersededBy 导航。
    registry.deprecate("EVI-SNAPSHOT-INCOMPLETE", std::string{"EVI-EVIDENCE-MISSING"});

    const CodeDescriptor* tombstone = registry.find("EVI-SNAPSHOT-INCOMPLETE");
    ASSERT_NE(tombstone, nullptr) << "tombstone 须保留（旧持久化产物只读解析——§4.5.1）";
    EXPECT_TRUE(tombstone->deprecated);
    ASSERT_TRUE(tombstone->supersededBy.has_value());
    EXPECT_EQ(*tombstone->supersededBy, std::string("EVI-EVIDENCE-MISSING"));
    // 迁移目标可达（"supersededBy 导航"——呈现侧映射新码文案键的路径）。
    EXPECT_NE(registry.find(*tombstone->supersededBy), nullptr);
    // 其余元数据不变（只置废弃位——"不改写历史"，PA-2）。
    EXPECT_EQ(tombstone->category, DiagnosticCategory::DataInsufficient);
    EXPECT_EQ(tombstone->severity, DiagnosticSeverity::Warning);
    // registeredCodes 仍含（"删除禁止"——持久化兼容）。
    const auto eviCodes = registry.registeredCodes("evidence");
    EXPECT_NE(std::find(eviCodes.begin(), eviCodes.end(),
                        std::string{"EVI-SNAPSHOT-INCOMPLETE"}),
              eviCodes.end());
    // manifest 仍含且摘要变化（废弃状态是跨进程握手一致性面）。
    const CodeTableManifest after = registry.manifest();
    EXPECT_EQ(after.entries.size(), before.entries.size());
    EXPECT_TRUE(after.digest != before.digest);

    // 无迁移目标的纯废弃。
    registry.deprecate("RPT-EXPORT-FAILED", {});
    const CodeDescriptor* plain = registry.find("RPT-EXPORT-FAILED");
    ASSERT_NE(plain, nullptr);
    EXPECT_TRUE(plain->deprecated);
    EXPECT_FALSE(plain->supersededBy.has_value());

    // 未注册码废弃→CodeUnknown；自迁移→Usage。
    expectThrowsWithCode([&] { registry.deprecate("RT-NOT-REGISTERED", {}); },
                         DiagnosticsErrorCode::CodeUnknown,
                         "废弃未注册码应 CodeUnknown");
    expectThrowsWithCode(
        [&] { registry.deprecate("RT-CANCELLED", std::string{"RT-CANCELLED"}); },
        DiagnosticsErrorCode::Usage,
        "supersededBy 自引用应 Usage");
}

/** DT-REG-5：含 tombstone 的装配清单——新表注册 deprecated 描述符成功且
 *  与原表 manifest 一致（"旧持久化文本解析保留映射"的跨进程形态：主/
 *  worker 各自装配同一清单〔含 tombstone〕，摘要一致——§9.1 manifest 契约）。 */
TEST(DiagCodesRegistry, DtReg5_TombstoneDescriptorRegistrableAndManifestConsistent)
{
    // 表 A：全量注册＋废弃修订。
    StableCodeRegistry a;
    registerBuiltinCodes(a);
    a.deprecate("EVI-SNAPSHOT-INCOMPLETE", std::string{"EVI-EVIDENCE-MISSING"});

    // 表 B：模拟另一进程按同一（修订后）清单装配——描述符自带 deprecated/
    // supersededBy 状态注册成功（tombstone 进初始清单是 §4.5.1 的装配形态）。
    StableCodeRegistry b;
    for (const CodeDescriptor& d : builtinCodeDescriptors()) {
        CodeDescriptor item = d;
        if (item.code == "EVI-SNAPSHOT-INCOMPLETE") {
            item.deprecated = true;
            item.supersededBy = std::string{"EVI-EVIDENCE-MISSING"};
        }
        b.registerCode(item);
    }

    // 两表 manifest 逐字节一致（跨进程码表一致——worker 握手通过）。
    EXPECT_TRUE(a.manifest() == b.manifest());
    // tombstone 解析：B 侧同样可只读映射（supersededBy 导航）。
    const CodeDescriptor* found = b.find("EVI-SNAPSHOT-INCOMPLETE");
    ASSERT_NE(found, nullptr);
    EXPECT_TRUE(found->deprecated);
    EXPECT_EQ(*found->supersededBy, std::string("EVI-EVIDENCE-MISSING"));
}

/** DT-REG-5／§9.1"非法调用"行：运行期（seal 后）registerCode/deprecate→
 *  Usage；查询路径不受影响；seal 幂等。 */
TEST(DiagCodesRegistry, DtReg5_RuntimeMutationRejectedAfterSeal)
{
    StableCodeRegistry registry;
    registerBuiltinCodes(registry);
    registry.seal();
    registry.seal();   // 幂等
    EXPECT_TRUE(registry.sealed());

    expectThrowsWithCode(
        [&] { registry.registerCode(makeValidDescriptor("RT-LATE-BINDING")); },
        DiagnosticsErrorCode::Usage,
        "运行期 registerCode 应 Usage（§9.1 原文）");
    expectThrowsWithCode(
        [&] { registry.deprecate("RT-INPUT-INVALID", {}); },
        DiagnosticsErrorCode::Usage,
        "运行期 deprecate 应 Usage（装配清单修订仅限装配期）");

    // 查询路径在运行期可用（"运行期 find/manifest 并发只读安全"）。
    EXPECT_NE(registry.find("RT-INPUT-INVALID"), nullptr);
    EXPECT_EQ(registry.manifest().entries.size(), 87u);
}

// =====================================================================
// acceptance 3（P-DIAG-3）：实现承载词表 15/4 值随码元数据登记，不新增
// 工程状态轴值。
// =====================================================================

/** P-DIAG-3：分类/严重词表逐值 token 断言（15/4 显式全列——任何新增轴值
 *  使本用例红；语义锚点＝§4.3 表行，见 DiagCodes.hpp 枚举注释）。 */
TEST(DiagCodesVocabulary, DtPdiag3_Category15Severity4TokensFrozen)
{
    // 15 分类值（§4.3 词表序——P-DIAG-3 实现承载，语义锚点逐项 §4.4 矩阵）。
    EXPECT_EQ(categoryToken(DiagnosticCategory::InputInvalid), std::string_view{"input-invalid"});
    EXPECT_EQ(categoryToken(DiagnosticCategory::FormatOrVersion), std::string_view{"format-or-version"});
    EXPECT_EQ(categoryToken(DiagnosticCategory::PermissionOrLock), std::string_view{"permission-or-lock"});
    EXPECT_EQ(categoryToken(DiagnosticCategory::ResourceMissing), std::string_view{"resource-missing"});
    EXPECT_EQ(categoryToken(DiagnosticCategory::PolicyDenied), std::string_view{"policy-denied"});
    EXPECT_EQ(categoryToken(DiagnosticCategory::Confirmable), std::string_view{"confirmable"});
    EXPECT_EQ(categoryToken(DiagnosticCategory::ExecutionFailed), std::string_view{"execution-failed"});
    EXPECT_EQ(categoryToken(DiagnosticCategory::Canceled), std::string_view{"canceled"});
    EXPECT_EQ(categoryToken(DiagnosticCategory::Interrupted), std::string_view{"interrupted"});
    EXPECT_EQ(categoryToken(DiagnosticCategory::Timeout), std::string_view{"timeout"});
    EXPECT_EQ(categoryToken(DiagnosticCategory::DataInsufficient), std::string_view{"data-insufficient"});
    EXPECT_EQ(categoryToken(DiagnosticCategory::EvidenceMissing), std::string_view{"evidence-missing"});
    EXPECT_EQ(categoryToken(DiagnosticCategory::InfeasibilityProof), std::string_view{"infeasibility-proof"});
    EXPECT_EQ(categoryToken(DiagnosticCategory::Internal), std::string_view{"internal"});
    EXPECT_EQ(categoryToken(DiagnosticCategory::SecurityOrRedaction), std::string_view{"security-or-redaction"});

    // 4 严重值（§4.3 词表序；Dev 不进用户目录/用户日志/报告——§4.3 表）。
    EXPECT_EQ(severityToken(DiagnosticSeverity::Error), std::string_view{"error"});
    EXPECT_EQ(severityToken(DiagnosticSeverity::Warning), std::string_view{"warning"});
    EXPECT_EQ(severityToken(DiagnosticSeverity::Info), std::string_view{"info"});
    EXPECT_EQ(severityToken(DiagnosticSeverity::Dev), std::string_view{"dev"});

    // 可重试性 3 值（§4.5 词表）。
    EXPECT_EQ(retryKindToken(RetryKind::Never), std::string_view{"never"});
    EXPECT_EQ(retryKindToken(RetryKind::UserRetry), std::string_view{"user-retry"});
    EXPECT_EQ(retryKindToken(RetryKind::AutoRetry), std::string_view{"auto-retry"});

    // token 全表互异（词表值即机器可读轴——重复 token 会造成呈现分组歧义）。
    const std::set<std::string_view> categories{
        categoryToken(DiagnosticCategory::InputInvalid), categoryToken(DiagnosticCategory::FormatOrVersion),
        categoryToken(DiagnosticCategory::PermissionOrLock), categoryToken(DiagnosticCategory::ResourceMissing),
        categoryToken(DiagnosticCategory::PolicyDenied), categoryToken(DiagnosticCategory::Confirmable),
        categoryToken(DiagnosticCategory::ExecutionFailed), categoryToken(DiagnosticCategory::Canceled),
        categoryToken(DiagnosticCategory::Interrupted), categoryToken(DiagnosticCategory::Timeout),
        categoryToken(DiagnosticCategory::DataInsufficient), categoryToken(DiagnosticCategory::EvidenceMissing),
        categoryToken(DiagnosticCategory::InfeasibilityProof), categoryToken(DiagnosticCategory::Internal),
        categoryToken(DiagnosticCategory::SecurityOrRedaction)};
    EXPECT_EQ(categories.size(), 15u);
}

/** P-DIAG-3 补充：内置 87 码的码元数据登记面（category/severity/retryable/
 *  flags）逐码落值——词表承载进码表（"随码元数据登记"acceptance 原文）。 */
TEST(DiagCodesVocabulary, DtPdiag3_VocabularyCarriedByBuiltinCodeMetadata)
{
    const auto registry = makeBuiltinRegistry();

    // Dev 码边界全表扫描：severity=Dev ⇔ userVisible/reportable/historical
    // 全 false（§4.5 注册期验证的全表投影——逐码登记不越用户级/开发级分界）。
    std::size_t devCount = 0;
    for (const CodeDescriptor& d : registry->manifest().entries) {
        EXPECT_EQ(categoryToken(d.category).empty(), false) << "分类 token 缺失: " << d.code;
        if (d.severity == DiagnosticSeverity::Dev) {
            ++devCount;
            EXPECT_FALSE(d.userVisible) << "Dev 码可见性越界: " << d.code;
            EXPECT_FALSE(d.reportable) << "Dev 码报告面越界: " << d.code;
            EXPECT_FALSE(d.historical) << "Dev 码历史面越界: " << d.code;
        } else {
            EXPECT_TRUE(d.userVisible) << "用户级码应可见: " << d.code;
        }
    }
    // Dev 码计数＝各卡登记的开发级合计（EX 4＋EVI 2＋DIAG 4＋PRJ 1＝11）。
    EXPECT_EQ(devCount, 11u);

    // 确认类纪律：阶段 A 内置表无可确认域码（§4.6 表尾行"阶段 B 起注册"），
    // 且 confirmable⇒requiresComparison 在全表成立（注册入口已强制——投影断言）。
    for (const CodeDescriptor& d : registry->manifest().entries) {
        if (d.confirmable) {
            EXPECT_TRUE(d.requiresComparison) << "可确认码须比较型（SA-15）: " << d.code;
        }
    }
}

// =====================================================================
// acceptance 4（P-DIAG-1/P-DIAG-9）：core v0.1 消费基线＋文案键/值分离。
// =====================================================================

/** P-DIAG-1：DiagCode 消费契约以 core.md v0.1 为基线——CodeDescriptor::code
 *  类型＝core::DiagCode（编译期钉住；core 冻结 diff 后按影响面增量同步，
 *  不私改 core）。 */
TEST(DiagCodesCoreBaseline, DtPdiag1_CodeFieldIsCoreDiagCode)
{
    static_assert(std::is_same<decltype(std::declval<CodeDescriptor>().code),
                               sdurws::ird::core::DiagCode>::value,
                  "P-DIAG-1：码字段必须以 core::DiagCode（core.md v0.1）承载");
    static_assert(std::is_same<decltype(std::declval<CodeDescriptor>().supersededBy),
                               std::optional<sdurws::ird::core::DiagCode>>::value,
                  "P-DIAG-1：迁移目标必须以 core::DiagCode 承载");
    SUCCEED() << "core::DiagCode 基线消费（编译期断言通过）";
}

/** P-DIAG-9：文案键随码表登记——命名约定 diag.<code-lower>.title/detail
 *  全表成立＋键注册期唯一；文案值不在本设施（键/值分离——UX-02）。 */
TEST(DiagCodesTextKeys, DtPdiag9_TextKeysFollowConventionAndAreUnique)
{
    const std::vector<CodeDescriptor> table = builtinCodeDescriptors();
    std::set<std::string> keys;

    for (const CodeDescriptor& d : table) {
        // 键形＝约定派生（小写码＋.title/.detail——P-DIAG-9 键体系冻结）。
        std::string lower;
        for (const char ch : d.code) {
            lower.push_back(ch >= 'A' && ch <= 'Z' ? static_cast<char>(ch - 'A' + 'a') : ch);
        }
        EXPECT_EQ(d.titleKey, "diag." + lower + ".title") << "titleKey 偏离约定: " << d.code;
        EXPECT_EQ(d.detailKey, "diag." + lower + ".detail") << "detailKey 偏离约定: " << d.code;
        // 键唯一（跨码全表 174 键无重复——NFR-MNT-03 文案单一权威）。
        EXPECT_TRUE(keys.insert(d.titleKey).second) << "titleKey 重复: " << d.titleKey;
        EXPECT_TRUE(keys.insert(d.detailKey).second) << "detailKey 重复: " << d.detailKey;
    }
    // 偏离约定的键在注册边界被拒（DtReg2 键唯一性之外的第二道闸——键形校验）。
    CodeDescriptor deviant = makeValidDescriptor("RT-KEY-DEV");
    deviant.titleKey = "ui.custom.title";   // 违反 diag.<code-lower>.title 约定
    expectThrowsWithCode(
        [&] {
            StableCodeRegistry r;
            r.registerCode(deviant);
        },
        DiagnosticsErrorCode::Usage,
        "偏离键命名约定应 Usage（P-DIAG-9 键体系冻结）");
}

/** §9.0：错误码 token 全表（11 值逐值钉住——设施异常轨的码面稳定）。 */
TEST(DiagnosticsErrors, DtReg_TokenTableComplete)
{
    EXPECT_EQ(token(DiagnosticsErrorCode::DuplicateCode), std::string_view{"diagnostics/duplicate-code"});
    EXPECT_EQ(token(DiagnosticsErrorCode::CodeUnknown), std::string_view{"diagnostics/code-unknown"});
    EXPECT_EQ(token(DiagnosticsErrorCode::CodeDeprecated), std::string_view{"diagnostics/code-deprecated"});
    EXPECT_EQ(token(DiagnosticsErrorCode::SubjectMissing), std::string_view{"diagnostics/subject-missing"});
    EXPECT_EQ(token(DiagnosticsErrorCode::ComparisonMissing), std::string_view{"diagnostics/comparison-missing"});
    EXPECT_EQ(token(DiagnosticsErrorCode::CategoryMismatch), std::string_view{"diagnostics/category-mismatch"});
    EXPECT_EQ(token(DiagnosticsErrorCode::ParamSchemaMismatch), std::string_view{"diagnostics/param-schema-mismatch"});
    EXPECT_EQ(token(DiagnosticsErrorCode::ContextMissing), std::string_view{"diagnostics/context-missing"});
    EXPECT_EQ(token(DiagnosticsErrorCode::InvalidState), std::string_view{"diagnostics/invalid-state"});
    EXPECT_EQ(token(DiagnosticsErrorCode::BindingMismatch), std::string_view{"diagnostics/binding-mismatch"});
    EXPECT_EQ(token(DiagnosticsErrorCode::Usage), std::string_view{"diagnostics/usage"});

    // DiagnosticsError 消息契约：what() ＝ "<token>: <detail>"；空 detail＝恰为 token。
    const DiagnosticsError withDetail{DiagnosticsErrorCode::DuplicateCode, "重复注册（码 X）"};
    EXPECT_EQ(std::string{withDetail.what()}, std::string{"diagnostics/duplicate-code: 重复注册（码 X）"});
    const DiagnosticsError bare{DiagnosticsErrorCode::Usage, ""};
    EXPECT_EQ(std::string{bare.what()}, std::string{"diagnostics/usage"});
    EXPECT_EQ(bare.code(), DiagnosticsErrorCode::Usage);
}
